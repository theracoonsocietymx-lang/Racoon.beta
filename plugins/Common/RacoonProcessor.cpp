#include <cstring>
#include "RacoonProcessor.h"

namespace
{
// Inside Racoon a plugin plays the song's clips: a Chinchilla that sings the MIDI, a Skunk whose grains follow the
// keyboard over a material of its library, a Mole that plays with the song. A sound (a factory or user preset) is
// about the timbre, so changing it there must not change how the plugin plays: those values are kept.
// (Chinchilla's presets are made for a recorded voice and turned singing off: the channel went silent.)
struct PlayModeGuard
{
    explicit PlayModeGuard (juce::AudioProcessor& p) : proc (p)
    {
        if (! racoon::maqueta::hostIsRacoon()) return;
        for (auto* prm : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (keep().contains (rp->paramID)) kept.push_back ({ rp, rp->getValue() });
    }
    ~PlayModeGuard()
    {
        // Skunk playing a material of its library: the sound may pick another material, but the capture stays frozen
        // (live capture records the silent input of a MIDI channel over the material) and the grains stay audible
        // (a spectral sound with the grains at 0 only processes the input, and there is none)
        bool library = false;
        for (auto& [rp, v] : kept) if (rp->paramID == "lib" && v > 1.0e-6f) library = true;
        for (auto& [rp, v] : kept)
        {
            const auto& id = rp->paramID;
            if ((id == "lib" || id == "gmix") && rp->getValue() > 1.0e-6f) continue;     // only when the sound left none
            if ((id == "recmode" || id == "freeze" || id == "gmix") && ! library) continue;
            if (std::abs (rp->getValue() - v) > 1.0e-6f) rp->setValueNotifyingHost (v);
        }
    }
    static const juce::StringArray& keep() { static const juce::StringArray k { "sing_on", "gmidi", "play", "lib", "recmode", "freeze", "gmix" }; return k; }
    juce::AudioProcessor& proc;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> kept;
};
}


namespace racoon
{
using namespace juce;

RacoonProcessor::RacoonProcessor (const BusesProperties& buses, PluginInfo pi, AudioProcessorValueTreeState::ParameterLayout layout)
    : AudioProcessor (buses), info (std::move (pi)), apvts (*this, &undoManager, "PARAMS", [&]
      {
          layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
          return std::move (layout);
      }())
{
    bypassParam = dynamic_cast<AudioParameterBool*> (apvts.getParameter ("bypass"));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            index.emplace_back (rp->paramID.toStdString(), apvts.getRawParameterValue (rp->paramID));
    std::sort (index.begin(), index.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
}

std::atomic<float>* RacoonProcessor::param (const char* id) const
{
    // binary search without building any string
    auto it = std::lower_bound (index.begin(), index.end(), id, [] (const auto& e, const char* key) { return std::strcmp (e.first.c_str(), key) < 0; });
    if (it != index.end() && it->first == id) return it->second;
    jassertfalse;   // unknown parameter id
    static std::atomic<float> none { 0.0f };
    return &none;
}

void RacoonProcessor::setRealValue (const String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

bool RacoonProcessor::acceptsMidi() const
{
    return info.kind != PluginKind::effect || wantsMidiAsEffect;
}

const StringArray& RacoonProcessor::divisionNames()
{
    static StringArray a { "1/1", "1/2", "1/2 .", "1/2 T", "1/4", "1/4 .", "1/4 T", "1/8", "1/8 .", "1/8 T",
                           "1/16", "1/16 .", "1/16 T", "1/32", "1/32 .", "1/32 T", "1/64", "1/64 T", "1/128" };
    return a;
}

double RacoonProcessor::syncSeconds (int idx, double tempo)
{
    static const double beats[] { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0,
                                  0.25, 0.375, 1.0 / 6.0, 0.125, 0.1875, 1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125 };
    idx = jlimit (0, (int) std::size (beats) - 1, idx);
    return beats[idx] * 60.0 / jlimit (20.0, 400.0, tempo);
}

bool RacoonProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (info.kind == PluginKind::midiEffect) return true;   // MIDI only: no audio buses
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (info.kind == PluginKind::instrument) return l.getMainInputChannelSet().isDisabled() || l.getMainInputChannelSet() == out;
    const auto in = l.getMainInputChannelSet();
    if (in == out) return true;
    return in == AudioChannelSet::mono() && out == AudioChannelSet::stereo();   // mono in, stereo out
}

void RacoonProcessor::prepareToPlay (double rate, int block)
{
    sr = rate;
    maxBlockSize = jmax (1, block);
    dry.setSize (2, maxBlockSize, false, true, false);
    dryDelay.setSize (2, (int) (rate * 1.0) + maxBlockSize + 8, false, true, false);
    dryDelayPos = 0;
    bypassGain = bypassParam->get() ? 1.0f : 0.0f;
    silentSamples = 0;
    sleeping = false;
    ensurePattern();
    extraMidi.ensureSize (2048);
    demoVoice.prepare (rate);
    automation.prepare (rate);
    demoVoice.setKind (demoSourceKind());
    ownVoice.prepare (rate);
    ownInstrument.prepare (rate);
    ownKit.prepare (rate);
    ownVoice.setKind (ownMidiKind());
    keyboardState.reset();
    seqWas = false;
    for (auto& s : seqSounding) s.reset();
    prepareDsp (rate, maxBlockSize);
}

void RacoonProcessor::updateTransport()
{
    hostPlaying = false;    // without a play head (an app on its own) only the built-in Play moves time
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hostBpm = jlimit (20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition()) hostPpq = *q;
            hostPlaying = pos->getIsPlaying();
        }
}

void RacoonProcessor::delayDry (int n, int channels)
{
    const int lat = getLatencySamples();
    if (lat <= 0) return;
    const int size = dryDelay.getNumSamples();
    for (int c = 0; c < channels; ++c)
    {
        auto* d = dry.getWritePointer (c);
        auto* line = dryDelay.getWritePointer (c);
        int w = dryDelayPos;
        for (int i = 0; i < n; ++i)
        {
            line[w] = d[i];
            int r = w - jmin (lat, size - 1);
            if (r < 0) r += size;
            d[i] = line[r];
            if (++w >= size) w = 0;
        }
    }
    dryDelayPos = (dryDelayPos + n) % size;
}

void RacoonProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    updateTransport();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    // a host (or a test) may hand over fewer channels than the buses say
    const int inCh = jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
    const int outCh = jmin (2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    for (int c = getTotalNumInputChannels(); c < jmin (getTotalNumOutputChannels(), buffer.getNumChannels()); ++c) buffer.clear (c, 0, n);
    if (inCh == 1 && outCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in, stereo out

    // "All sound off" (CC 120) from the host stops the plugin's own sequencer too: Racoon sends it when you close
    // an instrument that is still playing on its own and ask it to stop
    for (const auto m : midi)
        if (m.getMessage().isAllSoundOff()) { setSeqPlaying (false); break; }

    // the built-in sequencer and the on-screen keyboard: notes for the plugin, or a little song for an effect
    extraMidi.clear();
    const bool realHostPlaying = hostPlaying;
    const double realHostPpq = hostPpq;
    runSequencer (extraMidi, n);
    // automation: follows the DAW when it plays, the built-in Play otherwise; CC in the incoming MIDI moves its lane
    automation.process (midi, automation.clock (realHostPlaying, realHostPpq, seqPlay.load(), hostBpm, n), n);
    keyboardState.processNextMidiBuffer (extraMidi, 0, n, true);
    if (hasDemoSource() && outCh > 0 && demoSourceOn.load())
    {
        demoVoice.setBeat (seqPlay.load() && demoSourceKind() == PreviewSound::Kind::mix, hostBpm, seqBeat);
        demoVoice.render (buffer, extraMidi, 0, n, 0.6f);
    }
    if (acceptsMidi() && ! extraMidi.isEmpty()) midi.addEvents (extraMidi, 0, n, 0);

    if (n > dry.getNumSamples()) dry.setSize (2, n, false, false, true);    // host broke its promise; rare

    float pk = 0.0f;
    for (int c = 0; c < jmin (inCh, outCh); ++c) pk = jmax (pk, buffer.getMagnitude (c, 0, n));
    inPeak.store (jmax (pk, inPeak.load() * 0.9f));

    const bool wantBypass = bypassParam->get();
    const float target = wantBypass ? 1.0f : 0.0f;
    const float step = (float) (1.0 / (0.03 * sr));     // 30 ms crossfade

    if (bypassChangedFlag != wantBypass)
    {
        bypassChangedFlag = wantBypass;
        bypassStateChanged (wantBypass, midi);
    }

    // MIDI effects, and the instruments that write MIDI (Chipmunk, Mole): bypass means the incoming notes pass untouched
    if (info.kind == PluginKind::midiEffect || playsOwnMidi())
    {
        if (! wantBypass) runDsp (buffer, midi);
        else
        {
            bypassedMidi (midi);
            if (info.kind != PluginKind::midiEffect)
                for (int c = 0; c < outCh; ++c) buffer.clear (c, 0, n);
        }
        float op = 0.0f;
        for (int c = 0; c < outCh; ++c) op = jmax (op, buffer.getMagnitude (c, 0, n));
        outPeak.store (jmax (op, outPeak.load() * 0.9f));
        return;
    }

    // dry copy (aligned to the plugin latency) for the bypass crossfade
    for (int c = 0; c < outCh; ++c) dry.copyFrom (c, 0, buffer, c, 0, n);
    delayDry (n, outCh);

    // sleep on silence (effects only), keeping a margin past the tail
    if (info.canSleep && info.kind == PluginKind::effect)
    {
        // a note coming in wakes it too (the effects that play MIDI: Chinchilla sings, Skunk plays its grains)
        bool notes = false;
        if (acceptsMidi()) for (const auto m : midi) if (m.getMessage().isNoteOn()) { notes = true; break; }
        if (pk < 1.0e-6f && ! notes) silentSamples += n; else { silentSamples = 0; sleeping = false; }
        if (silentSamples > (juce::int64) ((info.tailSeconds + 0.5) * sr) && bypassGain == target)
        {
            if (! sleeping) { sleeping = true; resetDsp(); }
            for (int c = 0; c < outCh; ++c) buffer.copyFrom (c, 0, dry, c, 0, n);
            outPeak.store (outPeak.load() * 0.9f);
            return;
        }
    }

    const bool steadyBypass = bypassGain >= 1.0f && target >= 1.0f;
    if (steadyBypass && ! info.tailOnBypass)
    {
        if (! wasFullyBypassed) wasFullyBypassed = true;
        for (int c = 0; c < outCh; ++c) buffer.copyFrom (c, 0, dry, c, 0, n);
        if (info.kind == PluginKind::instrument) buffer.clear();
        outPeak.store (outPeak.load() * 0.9f);
        return;
    }
    if (wasFullyBypassed) { wasFullyBypassed = false; resetDsp(); }

    // per-sample bypass ramp
    auto rampAt = [&] (int i, float start) { return jlimit (0.0f, 1.0f, start + (target > start ? step : -step) * (float) (i + 1)); };
    const float startGain = bypassGain;

    if (info.tailOnBypass && (startGain > 0.0f || target > 0.0f))
    {
        // fade the input out of the effect, keep its tail, and fade the dry signal in
        for (int i = 0; i < n; ++i)
        {
            const float b = rampAt (i, startGain);
            for (int c = 0; c < outCh; ++c) buffer.getWritePointer (c)[i] *= (1.0f - b);
        }
        runDsp (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            const float b = rampAt (i, startGain);
            for (int c = 0; c < outCh; ++c) buffer.getWritePointer (c)[i] += dry.getReadPointer (c)[i] * b;
        }
    }
    else
    {
        runDsp (buffer, midi);
        if (startGain > 0.0f || target > 0.0f)
            for (int i = 0; i < n; ++i)
            {
                const float b = rampAt (i, startGain);
                for (int c = 0; c < outCh; ++c)
                {
                    auto* o = buffer.getWritePointer (c);
                    o[i] = o[i] * (1.0f - b) + (info.kind == PluginKind::instrument ? 0.0f : dry.getReadPointer (c)[i] * b);
                }
            }
    }
    bypassGain = rampAt (n - 1, startGain);
    if (std::abs (bypassGain - target) < step) bypassGain = target;

    // never let a NaN or an infinity reach the host
    bool bad = false;
    for (int c = 0; c < outCh && ! bad; ++c)
    {
        auto* o = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (o[i])) { bad = true; break; }
    }
    if (bad) { buffer.clear(); resetDsp(); }

    float op = 0.0f;
    for (int c = 0; c < outCh; ++c) op = jmax (op, buffer.getMagnitude (c, 0, n));
    outPeak.store (jmax (op, outPeak.load() * 0.9f));
}

// ===================================================================== built-in sequencer
std::vector<SeqNote> RacoonProcessor::demoPattern() const
{
    // a simple progression: Am, F, C, G (one bar each, two bars shown by default)
    std::vector<SeqNote> v;
    const int chords[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 3; ++k) v.push_back ({ c * 2.0, 1.9, chords[c][k], 96, 1 });
    return v;
}

void RacoonProcessor::ensurePattern()
{
    if (patternReady) return;
    patternReady = true;
    seqBars.store (jlimit (1, 8, demoBars()));
    seqBpm.store (demoBpm());
    const SpinLock::ScopedLockType l (seqLock);
    pattern = demoPattern();
}

void RacoonProcessor::restoreDemoPattern()
{
    setPattern (demoPattern());
    seqBars.store (jlimit (1, 8, demoBars()));
    seqBpm.store (demoBpm());
}

std::vector<SeqNote> RacoonProcessor::getPattern() const
{
    const_cast<RacoonProcessor*> (this)->ensurePattern();
    const SpinLock::ScopedLockType l (seqLock);
    return pattern;
}

// one edit of the sequencer, for undo / redo
class PatternAction : public UndoableAction
{
public:
    PatternAction (RacoonProcessor& p, std::vector<SeqNote> b, std::vector<SeqNote> a) : proc (p), before (std::move (b)), after (std::move (a)) {}
    bool perform() override { proc.setPattern (after, false); return true; }
    bool undo() override    { proc.setPattern (before, false); return true; }
    int getSizeInUnits() override { return (int) (before.size() + after.size()) * 4 + 8; }
private:
    RacoonProcessor& proc;
    std::vector<SeqNote> before, after;
};

void RacoonProcessor::setPattern (std::vector<SeqNote> notes, bool undoable)
{
    patternReady = true;
    if (undoable)
    {
        auto old = getPattern();
        if (old == notes) return;
        undoManager.perform (new PatternAction (*this, std::move (old), std::move (notes)));
        return;
    }
    // stable: notes that start together keep their order, so an undo gives back exactly the same list
    std::stable_sort (notes.begin(), notes.end(), [] (const SeqNote& a, const SeqNote& b) { return a.start < b.start; });
    const SpinLock::ScopedLockType l (seqLock);
    pattern.swap (notes);
}

void RacoonProcessor::setPatternBars (int bars) { seqBars.store (bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : bars <= 8 ? 8 : 16); }

void RacoonProcessor::loadFromSong (const maqueta::EditRequest& r)
{
    setSeqPlaying (false);
    const int bars = (int) std::ceil (jmin (64.0, r.beats) / 4.0 - 1.0e-6);
    setPatternBars (bars);
    const double len = 4.0 * patternBars();
    std::vector<SeqNote> notes;
    for (auto& n : r.notes())
        if (n.start < len - 1.0e-6)
            notes.push_back ({ n.start, jmin (n.length, len - n.start), n.pitch, n.velocity, n.channel });
    setPatternBpm (r.bpm);
    setPattern (std::move (notes), true);
}
void RacoonProcessor::setPatternBpm (double b)  { seqBpm.store (jlimit (40.0, 240.0, b)); }
void RacoonProcessor::setSeqPlaying (bool on)   { seqPlay.store (on); }

void RacoonProcessor::runSequencer (MidiBuffer& out, int n)
{
    hostRunning.store (hostPlaying);
    const bool play = seqPlay.load();
    if (! play)
    {
        if (seqWas)
        {
            for (int ch = 0; ch < 16; ++ch)
                for (int p = 0; p < 128; ++p)
                    if (seqSounding[ch][(size_t) p]) out.addEvent (MidiMessage::noteOff (ch + 1, p), 0);
            for (auto& s : seqSounding) s.reset();
            seqWas = false;
        }
        seqUiBeat.store (-1.0);
        return;
    }
    if (! seqWas) { seqBeat = 0.0; seqWas = true; }

    const double tempo = hostPlaying ? hostBpm : seqBpm.load();
    const double len = 4.0 * seqBars.load();
    const double bps = tempo / 60.0 / sr;
    const double b0 = std::fmod (seqBeat, len), span = bps * n;
    auto offsetOf = [&] (double pos) -> int
    {
        double d = pos - b0;
        if (d < 0.0) d += len;
        if (d >= span) return -1;
        return jlimit (0, n - 1, (int) (d / bps));
    };
    SpinLock::ScopedTryLockType lock (seqLock);
    if (lock.isLocked())
    {
        // note-offs first, so a note that ends where the next one starts is let go before it sounds again
        for (auto& nt : pattern)
        {
            if (nt.start >= len) continue;
            const int ch = jlimit (1, 16, nt.channel), p = jlimit (0, 127, nt.pitch);
            const int off = offsetOf (std::fmod (std::min (nt.start + nt.length, len), len));
            if (off >= 0 && seqSounding[ch - 1][(size_t) p]) { out.addEvent (MidiMessage::noteOff (ch, p), off); seqSounding[ch - 1][(size_t) p] = false; }
        }
        for (auto& nt : pattern)
        {
            if (nt.start >= len) continue;
            const int ch = jlimit (1, 16, nt.channel), p = jlimit (0, 127, nt.pitch);
            const int on = offsetOf (nt.start);
            if (on >= 0)
            {
                if (seqSounding[ch - 1][(size_t) p]) out.addEvent (MidiMessage::noteOff (ch, p), on);
                out.addEvent (MidiMessage::noteOn (ch, p, (uint8) jlimit (1, 127, nt.velocity)), on);
                seqSounding[ch - 1][(size_t) p] = true;
            }
        }
    }
    seqBeat = std::fmod (b0 + span, len);
    seqUiBeat.store (seqBeat);
    // the plugin sees a running transport, so arpeggiators, LFOs in sync and Mole's lanes move without a DAW
    if (! hostPlaying) { hostPlaying = true; hostPpq = b0; hostBpm = tempo; }
}

void RacoonProcessor::runDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    processDsp (buffer, midi);
    if (playsOwnMidi() && ownSoundOn() && getMainBusNumOutputChannels() > 0 && buffer.getNumChannels() > 0)
    {
        if (ownKit.hasKit()) ownKit.render (buffer, midi, 0, buffer.getNumSamples(), 0.8f);
        else if (ownInstrument.loaded()) ownInstrument.render (buffer, midi, 0, buffer.getNumSamples(), 0.8f);
        else ownVoice.render (buffer, midi, 0, buffer.getNumSamples(), 0.9f);
    }
}

// ===================================================================== state
ValueTree RacoonProcessor::buildState() const
{
    ValueTree root ("RACOON");
    root.setProperty ("plugin", info.animal, nullptr);
    root.setProperty ("version", info.stateVersion, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    // the recorded instrument it plays (Racoon's clean-track certificate reads it; on load the parameter brings it back)
    if (ownInstrument.loaded()) root.setProperty ("instrumentoGrabado", ownInstrument.id(), nullptr);
    root.appendChild (const_cast<AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    ValueTree extra ("EXTRA");
    saveExtraState (extra);
    root.appendChild (extra, nullptr);
    // the sequencer travels with the project and with presets
    ValueTree seq ("SEQ");
    seq.setProperty ("bars", seqBars.load(), nullptr);
    seq.setProperty ("bpm", seqBpm.load(), nullptr);
    for (auto& nt : getPattern())
    {
        ValueTree v ("NOTE");
        v.setProperty ("s", nt.start, nullptr); v.setProperty ("l", nt.length, nullptr); v.setProperty ("p", nt.pitch, nullptr);
        v.setProperty ("v", nt.velocity, nullptr); v.setProperty ("c", nt.channel, nullptr);
        seq.appendChild (v, nullptr);
    }
    root.appendChild (seq, nullptr);
    root.appendChild (automation.toTree(), nullptr);
    return root;
}

void RacoonProcessor::restoreState (const ValueTree& root)
{
    ValueTree params;
    int version = info.stateVersion;
    if (root.hasType ("RACOON"))
    {
        params = root.getChildWithName (apvts.state.getType()).createCopy();
        version = (int) root.getProperty ("version", 1);
        currentPresetName = root.getProperty ("preset", {}).toString();
    }
    else if (root.hasType (apvts.state.getType()))
        params = root.createCopy();

    if (params.isValid())
    {
        if (version < info.stateVersion) migrateState (params, version);
        // parameters missing in an older state keep their defaults
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
                if (! params.getChildWithProperty ("id", rp->paramID).isValid())
                    rp->setValueNotifyingHost (rp->getDefaultValue());
        apvts.replaceState (params);
    }
    if (root.hasType ("RACOON"))
    {
        loadExtraState (root.getChildWithName ("EXTRA"));
        auto seq = root.getChildWithName ("SEQ");
        if (seq.isValid())
        {
            std::vector<SeqNote> notes;
            for (auto v : seq)
                notes.push_back ({ (double) v.getProperty ("s"), (double) v.getProperty ("l", 1.0), (int) v.getProperty ("p", 60),
                                   (int) v.getProperty ("v", 100), (int) v.getProperty ("c", 1) });
            seqBars.store (jlimit (1, 16, (int) seq.getProperty ("bars", 2)));
            seqBpm.store (jlimit (40.0, 240.0, (double) seq.getProperty ("bpm", 120.0)));
            setPattern (notes, false);
        }
        automation.fromTree (root.getChildWithName (Automation::treeType));
    }
    if (onPresetLoaded) MessageManager::callAsync ([cb = onPresetLoaded] { if (cb) cb(); });
}

// ===================================================================== export
String RacoonProcessor::exportFileName() const
{
    return "Racoon " + info.animal + " - " + Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M") + ".mid";
}

double RacoonProcessor::exportNotes (MidiMessageSequence& track, double total, double tpq) const
{
    const double patLen = 4.0 * seqBars.load();
    const auto notes = getPattern();
    for (double off = 0.0; off < total - 1.0e-9; off += patLen)
        for (auto& n : notes)
        {
            if (n.start >= patLen) continue;
            const double on = off + n.start, end = off + jmin (n.start + n.length, patLen);
            if (on >= total) continue;
            const int ch = jlimit (1, 16, n.channel), p = jlimit (0, 127, n.pitch);
            track.addEvent (MidiMessage::noteOn (ch, p, (uint8) jlimit (1, 127, n.velocity)), on * tpq);
            track.addEvent (MidiMessage::noteOff (ch, p), jmin (end, total) * tpq - 1.0);
        }
    return patLen;
}

void RacoonProcessor::captureMidiOutput (RacoonProcessor& fresh, MidiMessageSequence& track, double beats, double tpq,
                                         std::function<void (RacoonProcessor&)> afterState) const
{
    MemoryBlock st;
    const_cast<RacoonProcessor*> (this)->getStateInformation (st);
    fresh.setStateInformation (st.getData(), (int) st.getSize());
    fresh.automation.clearAll();
    if (afterState) afterState (fresh);
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    fresh.setRateAndBufferSizeDetails (rate, block);
    fresh.prepareToPlay (rate, block);
    fresh.setSeqPlaying (true);
    const double bpm = jlimit (20.0, 400.0, seqBpm.load());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const auto totalSamples = (int64) std::ceil (beats * samplesPerBeat);
    AudioBuffer<float> buf (jmax (2, fresh.getTotalNumOutputChannels()), block);
    std::bitset<128> on[16];
    for (int64 pos = 0; pos < totalSamples; pos += block)
    {
        buf.clear();
        MidiBuffer m;
        fresh.processBlock (buf, m);
        for (const auto meta : m)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOnOrOff()) continue;
            const double t = (double) (pos + meta.samplePosition) / samplesPerBeat * tpq;
            const int ch = jlimit (1, 16, msg.getChannel()) - 1, nn = msg.getNoteNumber();
            if (msg.isNoteOn()) { if (on[ch][(size_t) nn]) track.addEvent (MidiMessage::noteOff (ch + 1, nn), t); on[ch][(size_t) nn] = true; }
            else if (! on[ch][(size_t) nn]) continue;
            else on[ch][(size_t) nn] = false;
            auto c = msg;
            c.setTimeStamp (t);
            track.addEvent (c);
        }
    }
    for (int ch = 0; ch < 16; ++ch)
        for (int nn = 0; nn < 128; ++nn)
            if (on[ch][(size_t) nn]) track.addEvent (MidiMessage::noteOff (ch + 1, nn), beats * tpq - 1.0);
    fresh.setSeqPlaying (false);
}

MidiFile RacoonProcessor::buildMidi (bool asPlayed) const
{
    // the loop is as long as the longer of the two: the sequence repeats under a longer automation
    const double patLen = 4.0 * seqBars.load();
    double total = jmax (patLen, automation.laneIds().isEmpty() ? 0.0 : automation.lengthBeats());
    if (asPlayed && playsOwnMidi()) total = jmax (total, 16.0);     // the generators export a whole phrase (4 bars)
    constexpr int tpq = 960;
    MidiMessageSequence track;
    track.addEvent (MidiMessage::textMetaEvent (3, "Racoon " + info.animal), 0.0);
    track.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / jlimit (20.0, 400.0, seqBpm.load()))), 0.0);
    track.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    if (asPlayed) exportNotes (track, total, tpq);
    else RacoonProcessor::exportNotes (track, total, tpq);
    automation.appendToMidi (track, tpq, total);
    track.addEvent (MidiMessage::endOfTrack(), total * tpq);
    track.sort();
    track.updateMatchedPairs();
    MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (track);
    return mf;
}

bool RacoonProcessor::exportMidi (const File& file) const
{
    const auto mf = buildMidi (true);
    file.getParentDirectory().createDirectory();
    TemporaryFile tmp (file);
    {
        FileOutputStream os (tmp.getFile());
        if (! os.openedOk() || ! mf.writeTo (os)) return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}

MidiFile RacoonProcessor::maquetaMidi() const
{
    // Mole's rhythm is what it plays; for the rest, the notes that drive the plugin (Chipmunk gets its roots back
    // and makes the chords again on its channel)
    return buildMidi (maquetaKind() == "generator");
}

void RacoonProcessor::updateTrackProperties (const TrackProperties& p)
{
    const ScopedLock sl (trackLock);
    if (p.name.has_value()) hostTrackName = *p.name;
}

String RacoonProcessor::trackName() const
{
    const ScopedLock sl (trackLock);
    return hostTrackName;
}

File RacoonProcessor::exportMidiForDrag() const
{
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Racoon MIDI");
    dir.createDirectory();
    auto f = dir.getChildFile ("Racoon " + info.animal + ".mid");
    return exportMidi (f) ? f : File();
}

void RacoonProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = buildState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RacoonProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        restoreState (ValueTree::fromXml (*xml));
}

// ===================================================================== presets
File RacoonProcessor::presetFolder() const
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
   #if JUCE_MAC
                 .getChildFile ("Application Support")
   #endif
                 .getChildFile ("Racoon/Presets").getChildFile (info.animal);
    f.createDirectory();
    return f;
}

StringArray RacoonProcessor::userPresetNames() const
{
    StringArray names;
    for (auto& f : presetFolder().findChildFiles (File::findFiles, false, "*.racoonpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool RacoonProcessor::saveUserPreset (const String& name)
{
    if (name.trim().isEmpty()) return false;
    auto f = presetFolder().getChildFile (File::createLegalFileName (name.trim()) + ".racoonpreset");
    currentPresetName = name.trim();
    if (auto xml = buildState().createXml())
        return xml->writeTo (f);
    return false;
}

bool RacoonProcessor::loadUserPreset (const String& name)
{
    auto f = presetFolder().getChildFile (name + ".racoonpreset");
    if (auto xml = parseXML (f))
    {
        PlayModeGuard guard (*this);
        restoreState (ValueTree::fromXml (*xml));
        currentPresetName = name;
        return true;
    }
    return false;
}

bool RacoonProcessor::deleteUserPreset (const String& name)
{
    return presetFolder().getChildFile (name + ".racoonpreset").deleteFile();
}

int RacoonProcessor::getNumPrograms()
{
    return jmax (1, (int) factoryPresets().size());
}

const String RacoonProcessor::getProgramName (int i)
{
    auto fp = factoryPresets();
    return isPositiveAndBelow (i, (int) fp.size()) ? fp[(size_t) i].name : String ("Inicial");
}


void RacoonProcessor::setCurrentProgram (int i)
{
    auto fp = factoryPresets();
    if (! isPositiveAndBelow (i, (int) fp.size())) return;
    currentProgram = i;
    applyFactoryPreset (i);
}

void RacoonProcessor::applyFactoryPreset (int index)
{
    auto fp = factoryPresets();
    if (! isPositiveAndBelow (index, (int) fp.size())) return;
    const auto& preset = fp[(size_t) index];
    PlayModeGuard guard (*this);
    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<RangedAudioParameter*> (p);
        if (rp == nullptr || rp == bypassParam) continue;
        float norm = rp->getDefaultValue();
        for (auto& [id, v] : preset.values)
            if (id == rp->paramID) norm = rp->convertTo0to1 (v);
        rp->setValueNotifyingHost (norm);
    }
    currentProgram = index;
    currentPresetName = preset.name;
    if (onPresetLoaded) onPresetLoaded();
}

void RacoonProcessor::selectAB (int slot)
{
    if (slot == activeAB || ! isPositiveAndBelow (slot, 2)) return;
    getStateInformation (abSlots[activeAB]);
    activeAB = slot;
    if (abSlots[slot].getSize() > 0)
        setStateInformation (abSlots[slot].getData(), (int) abSlots[slot].getSize());
}

void RacoonProcessor::copyAToB()
{
    MemoryBlock current;
    getStateInformation (current);
    abSlots[0] = current;
    abSlots[1] = current;
}

// ===================================================================== parameter helpers
namespace P
{
NormalisableRange<float> range (float lo, float hi, float centre, float step)
{
    NormalisableRange<float> r (lo, hi, step);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    return r;
}

static void addFloat (Layout& l, const String& id, const String& name, NormalisableRange<float> r, float def,
                      std::function<String (float, int)> fmt, const String& unit)
{
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (fmt)));
}

void flt (Layout& l, const String& id, const String& name, NormalisableRange<float> r, float def, const String& unit, int decimals)
{
    addFloat (l, id, name, r, def, [unit, decimals] (float v, int) { return String (v, decimals) + (unit.isNotEmpty() ? " " + unit : String()); }, unit);
}

void hz (Layout& l, const String& id, const String& name, float lo, float hi, float def)
{
    addFloat (l, id, name, range (lo, hi, std::sqrt (lo * hi)), def,
              [] (float v, int) { return v < 10.0f ? String (v, 2) + " Hz" : v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; }, "Hz");
}

void ms (Layout& l, const String& id, const String& name, float lo, float hi, float def, float centre)
{
    addFloat (l, id, name, range (lo, hi, centre > 0.0f ? centre : std::sqrt (jmax (0.01f, lo) * hi)), def,
              [] (float v, int) { return v < 1000.0f ? String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms" : String (v / 1000.0f, 2) + " s"; }, "ms");
}

void adsr (Layout& l, float attackMs, float decayMs, float sustainPct, float releaseMs)
{
    ms (l, "adsr_a", "Ataque", 0.5f, 20000.0f, attackMs, 60.0f);
    ms (l, "adsr_d", String::fromUTF8 ("Caída"), 5.0f, 20000.0f, decayMs, 400.0f);
    pct (l, "adsr_s", "Sostenido", 0.0f, 100.0f, sustainPct);
    ms (l, "adsr_r", String::fromUTF8 ("Relajación"), 5.0f, 20000.0f, releaseMs, 400.0f);
}

void db (Layout& l, const String& id, const String& name, float lo, float hi, float def)
{
    addFloat (l, id, name, range (lo, hi), def, [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }, "dB");
}

void pct (Layout& l, const String& id, const String& name, float lo, float hi, float def)
{
    addFloat (l, id, name, range (lo, hi), def, [] (float v, int) { return String (roundToInt (v)) + " %"; }, "%");
}

void choice (Layout& l, const String& id, const String& name, const StringArray& items, int def)
{
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
}

void toggle (Layout& l, const String& id, const String& name, bool def)
{
    l.add (std::make_unique<ToggleParam> (id, name, def));
}

void integer (Layout& l, const String& id, const String& name, int lo, int hi, int def)
{
    l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
}
}
}

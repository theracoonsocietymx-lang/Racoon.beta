#include <cstring>
#include "RacoonProcessor.h"

namespace racoon
{
using namespace juce;

RacoonProcessor::RacoonProcessor (const BusesProperties& buses, PluginInfo pi, AudioProcessorValueTreeState::ParameterLayout layout)
    : AudioPluginInstance (buses), info (std::move (pi)), apvts (*this, nullptr, "PARAMS", [&]
      {
          layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
          return std::move (layout);
      }())
{
    bypassParam = dynamic_cast<AudioParameterBool*> (apvts.getParameter ("bypass"));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            index.emplace_back (rp->paramID.toStdString(), apvts.getRawParameterValue (rp->paramID));
            hostSync.emplace_back (rp, apvts.getRawParameterValue (rp->paramID));
        }
    std::sort (index.begin(), index.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
}

void RacoonProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = d.descriptiveName = info.animal;
    d.pluginFormatName = "Racoon";
    d.category = info.function;
    d.manufacturerName = "Racoon";
    d.version = "1.0";
    d.fileOrIdentifier = effectId;
    d.uniqueId = d.deprecatedUid = effectId.hashCode();
    d.isInstrument = info.kind == PluginKind::instrument;
    d.numInputChannels = getTotalNumInputChannels();
    d.numOutputChannels = getTotalNumOutputChannels();
    d.hasSharedContainer = false;
    d.hasARAExtension = false;
}

void RacoonProcessor::syncHostParameters()
{
    for (auto& [p, a] : hostSync)
    {
        const float norm = p->getValue();
        if (p->convertFrom0to1 (norm) != a->load (std::memory_order_relaxed))
            p->sendValueChangedMessageToListeners (norm);
    }
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
    prepareDsp (rate, maxBlockSize);
}

void RacoonProcessor::updateTransport()
{
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
    syncHostParameters();
    updateTransport();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    const int inCh = getMainBusNumInputChannels();
    const int outCh = jmin (2, getMainBusNumOutputChannels());
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);
    if (inCh == 1 && outCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono in, stereo out

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

    // MIDI effects: bypass means the incoming notes pass untouched
    if (info.kind == PluginKind::midiEffect)
    {
        if (! wantBypass) processDsp (buffer, midi);
        else bypassedMidi (midi);
        outPeak.store (0.0f);
        return;
    }

    // dry copy (aligned to the plugin latency) for the bypass crossfade
    for (int c = 0; c < outCh; ++c) dry.copyFrom (c, 0, buffer, c, 0, n);
    delayDry (n, outCh);

    // sleep on silence (effects only), keeping a margin past the tail
    if (info.canSleep && info.kind == PluginKind::effect)
    {
        if (pk < 1.0e-6f) silentSamples += n; else { silentSamples = 0; sleeping = false; }
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
        processDsp (buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            const float b = rampAt (i, startGain);
            for (int c = 0; c < outCh; ++c) buffer.getWritePointer (c)[i] += dry.getReadPointer (c)[i] * b;
        }
    }
    else
    {
        processDsp (buffer, midi);
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

// ===================================================================== state
ValueTree RacoonProcessor::buildState() const
{
    ValueTree root ("RACOON");
    root.setProperty ("plugin", info.animal, nullptr);
    root.setProperty ("version", info.stateVersion, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    root.appendChild (const_cast<AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    ValueTree extra ("EXTRA");
    saveExtraState (extra);
    root.appendChild (extra, nullptr);
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
        loadExtraState (root.getChildWithName ("EXTRA"));
    if (onPresetLoaded) MessageManager::callAsync ([cb = onPresetLoaded] { if (cb) cb(); });
}

void RacoonProcessor::getStateInformation (MemoryBlock& dest)
{
    syncHostParameters();
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
              [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz"; }, "Hz");
}

void ms (Layout& l, const String& id, const String& name, float lo, float hi, float def, float centre)
{
    addFloat (l, id, name, range (lo, hi, centre > 0.0f ? centre : std::sqrt (jmax (0.01f, lo) * hi)), def,
              [] (float v, int) { return v < 1000.0f ? String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms" : String (v / 1000.0f, 2) + " s"; }, "ms");
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

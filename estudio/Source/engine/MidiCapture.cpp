#include "MidiCapture.h"

namespace racoon::capture
{
using namespace juce;

namespace
{
// a transport that always plays, for the offline render
struct OfflineHead : public AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0, seconds = 0.0;
    int64 samples = 0;
    int num = 4, den = 4;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setTimeInSeconds (seconds);
        p.setTimeInSamples (samples);
        p.setIsPlaying (true);
        p.setTimeSignature (TimeSignature { num, den });
        p.setPpqPositionOfLastBarStart (std::floor (ppq / (4.0 * num / den)) * (4.0 * num / den));
        return p;
    }
};
}

MidiMessageSequence render (AudioPluginInstance& p, const MidiMessageSequence& input, double sr, int numSamples,
                            std::function<double (double)> ppqAt, double bpm, int block)
{
    OfflineHead head;
    head.bpm = bpm;
    p.setPlayHead (&head);
    p.setNonRealtime (true);
    p.prepareToPlay (sr, block);
    const int chans = jmax (2, jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    AudioBuffer<float> audio (chans, block);
    MidiMessageSequence out;
    int idx = 0;
    for (int pos = 0; pos < numSamples; pos += block)
    {
        const int n = jmin (block, numSamples - pos);
        audio.setSize (chans, n, false, false, true);
        audio.clear();
        MidiBuffer mb;
        while (idx < input.getNumEvents() && input.getEventPointer (idx)->message.getTimeStamp() < pos + n)
        {
            const auto& m = input.getEventPointer (idx)->message;
            mb.addEvent (m, jlimit (0, n - 1, (int) m.getTimeStamp() - pos));
            ++idx;
        }
        head.seconds = pos / sr;
        head.samples = pos;
        head.ppq = ppqAt (head.seconds);
        p.processBlock (audio, mb);
        for (const auto ev : mb)
        {
            auto m = ev.getMessage();
            if (m.isNoteOnOrOff())
            {
                m.setTimeStamp ((double) (pos + ev.samplePosition));
                out.addEvent (m);
            }
        }
    }
    p.releaseResources();
    p.setPlayHead (nullptr);
    out.updateMatchedPairs();
    return out;
}

te::BeatRange defaultRange (Session& s, te::AudioTrack& t)
{
    auto& ts = s.getEdit().tempoSequence;
    std::optional<te::TimeRange> span;
    for (auto* c : t.getClips())
        if (dynamic_cast<te::MidiClip*> (c) != nullptr)
            span = span ? span->getUnionWith (c->getEditTimeRange()) : c->getEditTimeRange();
    if (span) return ts.toBeats (*span);
    const auto start = ts.toBeats (s.getTransport().getLoopRange().getStart());
    return { start, start + te::BeatDuration::fromBeats (8.0 * s.getTimeSigNumerator()) };
}

Result midiToClip (Session& s, te::AudioTrack& t, te::Plugin& fx, te::BeatRange range, bool replaceSource)
{
    Result r;
    auto* ext = dynamic_cast<te::ExternalPlugin*> (&fx);
    auto* live = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
    if (live == nullptr) { r.error = "Ese dispositivo no se puede pasar a clip."; return r; }

    auto& edit = s.getEdit();
    auto& ts = edit.tempoSequence;
    const double sr = 48000.0;
    String err;
    auto copy = s.engine.getPluginManager().pluginFormatManager.createPluginInstance (ext->desc, sr, 256, err);
    if (copy == nullptr) { r.error = "No se pudo abrir otra copia de " + fx.getName() + ": " + err; return r; }
    MemoryBlock state;
    live->getStateInformation (state);
    copy->setStateInformation (state.getData(), (int) state.getSize());
    copy->enableAllBuses();

    // the notes of the track's clips, from the start of the range, in samples
    const auto timeRange = ts.toTime (range);
    const double t0 = timeRange.getStart().inSeconds();
    MidiMessageSequence in;
    Array<te::Clip*> sources;
    for (auto* c : t.getClips())
        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
        {
            if (! mc->getEditTimeRange().overlaps (timeRange)) continue;
            sources.add (mc);
            for (auto* n : mc->getSequence().getNotes())
            {
                if (n->isMute()) continue;
                const auto tr = n->getEditTimeRange (*mc);
                const double a = tr.getStart().inSeconds() - t0, b = tr.getEnd().inSeconds() - t0;
                if (b <= 0.0 || a >= timeRange.getLength().inSeconds()) continue;
                in.addEvent (MidiMessage::noteOn (1, n->getNoteNumber(), (uint8) jlimit (1, 127, n->getVelocity())).withTimeStamp (jmax (0.0, a) * sr));
                in.addEvent (MidiMessage::noteOff (1, n->getNoteNumber()).withTimeStamp (jmax (0.0, b) * sr - 1.0));
            }
        }
    in.sort();

    // a little extra so notes that start inside the range can end
    const double lengthSec = timeRange.getLength().inSeconds();
    const int total = (int) ((lengthSec + 2.0) * sr);
    auto out = render (*copy, in, sr, total,
                       [&ts, t0] (double sec) { return ts.toBeats (te::TimePosition::fromSeconds (t0 + sec)).inBeats(); },
                       ts.getBpmAt (timeRange.getStart()));

    edit.getUndoManager().beginNewTransaction();
    auto* clip = s.createMidiClip (t, range, fx.getName() + " (MIDI)");
    if (clip == nullptr) { r.error = "No se pudo crear el clip."; return r; }
    auto* um = &edit.getUndoManager();
    const auto clipStart = range.getStart();
    for (int i = 0; i < out.getNumEvents(); ++i)
    {
        auto* e = out.getEventPointer (i);
        if (! e->message.isNoteOn()) continue;
        const double startSec = e->message.getTimeStamp() / sr;
        if (startSec >= lengthSec) continue;
        double endSec = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / sr : startSec + 0.1;
        endSec = jmin (endSec, lengthSec);
        const auto sb = ts.toBeats (te::TimePosition::fromSeconds (t0 + startSec)) - clipStart;
        const auto eb = ts.toBeats (te::TimePosition::fromSeconds (t0 + jmax (endSec, startSec + 0.005))) - clipStart;
        clip->getSequence().addNote (e->message.getNoteNumber(), te::BeatPosition::fromBeats (sb.inBeats()),
                                     te::BeatDuration::fromBeats (jmax (1.0 / 64.0, (eb - sb).inBeats())), e->message.getVelocity(), 0, um);
        ++r.notes;
    }
    if (replaceSource)
    {
        for (auto* c : sources) if (c != clip) s.deleteClip (*c);
        fx.setEnabled (false);
    }
    r.clip = clip;
    return r;
}
}

#include "Sequencer.h"

namespace be
{
// ============================================================ data
juce::ValueTree SeqData::toValueTree() const
{
    juce::ValueTree v ("SEQ");
    for (int p = 0; p < kSeqPatterns; ++p)
    {
        juce::StringArray parts;
        for (const auto& s : patterns[p].steps)
            parts.add (juce::String (s.on ? 1 : 0) + "," + juce::String (s.note) + "," + juce::String (s.octave) + ","
                       + juce::String (s.gate, 3) + "," + juce::String (s.vel, 3) + "," + juce::String (s.accent ? 1 : 0) + ","
                       + juce::String (s.slide ? 1 : 0) + "," + juce::String (s.tie ? 1 : 0) + "," + juce::String (s.prob, 3) + ","
                       + juce::String (s.ratchet) + "," + juce::String (s.micro, 3) + "," + juce::String (s.mod, 3)
                       + [&s]
                         {
                             if (s.numChord == 0) return juce::String();
                             juce::StringArray c;
                             for (int k = 0; k < s.numChord; ++k) c.add (juce::String (s.chord[k].pitch) + ":" + juce::String (s.chord[k].len, 3));
                             return "," + c.joinIntoString ("|");
                         }());
        juce::ValueTree pt ("PAT");
        pt.setProperty ("steps", parts.joinIntoString (";"), nullptr);
        v.appendChild (pt, nullptr);
    }
    return v;
}

void SeqData::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid()) return;
    for (int p = 0; p < kSeqPatterns && p < v.getNumChildren(); ++p)
    {
        auto parts = juce::StringArray::fromTokens (v.getChild (p).getProperty ("steps").toString(), ";", "");
        for (int i = 0; i < kSeqMaxSteps && i < parts.size(); ++i)
        {
            auto f = juce::StringArray::fromTokens (parts[i], ",", "");
            if (f.size() < 12) continue;
            auto& s = patterns[p].steps[i];
            s.on = f[0].getIntValue() != 0;
            s.note = juce::jlimit (0, 24, f[1].getIntValue());
            s.octave = juce::jlimit (-2, 2, f[2].getIntValue());
            s.gate = juce::jlimit (0.05f, 1.0f, f[3].getFloatValue());
            s.vel = juce::jlimit (0.0f, 1.0f, f[4].getFloatValue());
            s.accent = f[5].getIntValue() != 0;
            s.slide = f[6].getIntValue() != 0;
            s.tie = f[7].getIntValue() != 0;
            s.prob = juce::jlimit (0.0f, 1.0f, f[8].getFloatValue());
            s.ratchet = juce::jlimit (1, 4, f[9].getIntValue());
            s.micro = juce::jlimit (-0.5f, 0.5f, f[10].getFloatValue());
            s.mod = juce::jlimit (0.0f, 1.0f, f[11].getFloatValue());
            s.numChord = 0;
            if (f.size() > 12)
                for (const auto& c : juce::StringArray::fromTokens (f[12], "|", ""))
                    if (c.containsChar (':'))
                        s.addChord (juce::jlimit (-24, 48, c.upToFirstOccurrenceOf (":", false, false).getIntValue()),
                                    juce::jlimit (0.05f, 16.0f, c.fromFirstOccurrenceOf (":", false, false).getFloatValue()));
        }
    }
}

void SeqData::initDefault()
{
    for (auto& pat : patterns) for (auto& s : pat.steps) s = SeqStep();
    // A: rolling bass (rest on the kick, three sixteenths after it)
    for (int i = 0; i < kSeqMaxSteps; ++i)
    {
        auto& s = patterns[0].steps[i];
        s.on = (i % 4) != 0;
        s.gate = 0.55f; s.vel = (i % 4) == 2 ? 0.95f : 0.75f;
        s.note = (i % 16) == 14 ? 7 : ((i % 16) == 15 ? 10 : 0);
        s.mod = (float) (i % 4) / 3.0f;
    }
    // B: acid line
    const int acidNotes[16] { 0, 12, 0, 3, 0, 15, 12, 0, 7, 0, 10, 12, 0, 3, 5, 7 };
    const bool acidAcc[16]  { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    const bool acidSlide[16]{ 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    for (int i = 0; i < kSeqMaxSteps; ++i)
    {
        auto& s = patterns[1].steps[i];
        s.on = (i % 16) != 7;
        s.note = acidNotes[i % 16]; s.accent = acidAcc[i % 16]; s.slide = acidSlide[i % 16];
        s.gate = 0.5f; s.vel = 0.8f; s.mod = (float) ((i * 5) % 16) / 15.0f;
    }
    // C: offbeat house
    for (int i = 0; i < kSeqMaxSteps; ++i) { auto& s = patterns[2].steps[i]; s.on = (i % 4) == 2; s.gate = 0.7f; s.vel = 0.9f; }
    // D: indie dance octave gallop
    for (int i = 0; i < kSeqMaxSteps; ++i)
    {
        auto& s = patterns[3].steps[i];
        s.on = true; s.gate = 0.4f; s.octave = (i % 2) ? 1 : 0; s.vel = (i % 2) ? 0.7f : 0.9f;
        s.note = ((i / 8) % 2) ? 5 : 0;
    }
    // E: dark disco 8ths
    for (int i = 0; i < kSeqMaxSteps; ++i)
    {
        auto& s = patterns[4].steps[i];
        s.on = (i % 2) == 0; s.gate = 0.8f; s.vel = 0.85f;
        const int seq[8] { 0, 0, 12, 0, 0, 0, 10, 12 };
        s.note = seq[(i / 2) % 8] % 13; s.octave = seq[(i / 2) % 8] >= 12 ? 1 : 0; s.note = seq[(i / 2) % 8] >= 12 ? seq[(i / 2) % 8] - 12 : seq[(i / 2) % 8];
    }
    // F: techno rumble 16ths with ratchets
    for (int i = 0; i < kSeqMaxSteps; ++i)
    {
        auto& s = patterns[5].steps[i];
        s.on = (i % 4) != 0; s.gate = 0.35f; s.vel = 0.7f + 0.3f * (float) ((i % 4) == 1);
        s.ratchet = (i % 16) == 15 ? 2 : 1; s.mod = (float) (i % 16) / 15.0f;
    }
    // G/H: copies to edit
    patterns[6] = patterns[1];
    patterns[7] = patterns[0];
}

// ============================================================ engine
void Sequencer::prepare (double sampleRate) { sr = sampleRate; reset(); }

void Sequencer::reset()
{
    running = false; pending.clear(); lastStepIndex = -1; lastBlockEnd = -1.0; soundingNote = -1;
    numHeld = 0; numLatched = 0; arpIndex = 0; uiStep = -1;
}

void Sequencer::keyDown (int note, float vel)
{
    if (numHeld == 0) latchArmed = true;
    for (int i = 0; i < numHeld; ++i) if (held[i] == note) return;
    if (numHeld < 32) { held[numHeld] = note; heldVel[numHeld] = vel; ++numHeld; }
    lastKey = note;
    if (latchArmed) { numLatched = 0; latchArmed = false; }
    if (numLatched < 32) latched[numLatched++] = note;
}

void Sequencer::keyUp (int note)
{
    for (int i = 0; i < numHeld; ++i)
        if (held[i] == note)
        {
            for (int j = i; j < numHeld - 1; ++j) { held[j] = held[j + 1]; heldVel[j] = heldVel[j + 1]; }
            --numHeld;
            break;
        }
    if (numHeld > 0) lastKey = held[numHeld - 1];
    if (numHeld == 0) latchArmed = true;
}

int Sequencer::quantizeToScale (int semis, int scale)
{
    const auto& iv = scaleIntervals (scale);
    const int oct = (int) std::floor ((double) semis / 12.0);
    const int pc = semis - oct * 12;
    int best = iv[0], bestD = 99;
    for (auto v : iv) { const int d = std::abs (v - pc); if (d < bestD || (d == bestD && v < best)) { bestD = d; best = v; } }
    return oct * 12 + best;
}

int Sequencer::arpNoteFor (const SeqParams& p, long long k)
{
    int src[32]; int n = 0;
    if (p.latch) { for (int i = 0; i < numLatched; ++i) src[n++] = latched[i]; }
    else { for (int i = 0; i < numHeld; ++i) src[n++] = held[i]; }
    if (n == 0) return -1;
    int sorted[32];
    std::copy (src, src + n, sorted);
    if (p.arpMode != (int) ArpMode::AsPlayed) std::sort (sorted, sorted + n);
    const int total = n * std::max (1, p.arpOct);
    auto at = [&] (int i) { return sorted[i % n] + 12 * (i / n); };
    switch ((ArpMode) p.arpMode)
    {
        case ArpMode::Up: case ArpMode::AsPlayed: return at ((int) (k % total));
        case ArpMode::Down: return at (total - 1 - (int) (k % total));
        case ArpMode::UpDown:
        {
            if (total == 1) return at (0);
            const int period = 2 * total - 2;
            int i = (int) (k % period);
            if (i >= total) i = period - i;
            return at (i);
        }
        case ArpMode::Random: return at ((int) (rng.uni() * (float) total) % total);
    }
    return at (0);
}

void Sequencer::process (const SeqData& data, const SeqParams& p, int n, bool hostPlaying, double hostBpm,
                         double hostBeats, std::vector<SeqEvent>& out)
{
    const bool active = p.mode != (int) SeqMode::Off;
    const bool synced = p.hostSync && hostPlaying;
    const double bpm = synced && hostBpm > 1.0 ? hostBpm : (hostBpm > 1.0 && p.hostSync ? hostBpm : (double) p.bpm);
    const double bps = bpm / 60.0 / sr;   // beats per sample
    const double stepBeats = seqRateBeats (p.rate);

    const bool keysActive = p.mode == (int) SeqMode::Arpeggiator ? (p.latch ? numLatched > 0 : numHeld > 0) : numHeld > 0;
    bool shouldRun = active && (keysActive || (p.mode == (int) SeqMode::Sequencer && ((p.trigger == 0 && synced) || p.preview)));

    const double start = synced ? hostBeats : internalBeats;
    const double end = start + bps * (double) n;
    auto offsetOf = [&] (double beat) { return juce::jlimit (0, n - 1, (int) ((beat - start) / bps)); };

    auto flushAll = [&] (int offset)
    {
        for (auto& pd : pending) out.push_back ({ offset, false, pd.note, 0.0f, false, false, 0.0f });
        pending.clear();
        soundingNote = -1;
    };

    if (! shouldRun)
    {
        if (running) { flushAll (0); running = false; uiStep = -1; }
        internalBeats = end;
        lastBlockEnd = end;
        return;
    }

    // transport discontinuity
    if (running && synced && (start < lastBlockEnd - 1.0e-6 || start > lastBlockEnd + 0.05))
    {
        flushAll (0);
        lastStepIndex = (long long) std::ceil ((start - anchorBeats) / stepBeats) - 1;
    }
    if (! running)
    {
        running = true;
        if (synced && p.trigger == 0) { anchorBeats = 0.0; lastStepIndex = (long long) std::ceil ((start - 1.0e-9) / stepBeats) - 1; }
        else if (synced) { anchorBeats = std::floor (start / stepBeats) * stepBeats; lastStepIndex = (long long) std::ceil ((start - anchorBeats - 1.0e-9) / stepBeats) - 1; }
        else { anchorBeats = start; lastStepIndex = -1; }
        arpIndex = 0;
    }

    const int len = juce::jlimit (1, kSeqMaxSteps, p.length);
    const auto& pat = data.patterns[juce::jlimit (0, kSeqPatterns - 1, p.pattern)];

    // pending note offs that fall before the next step must be emitted in time order with new notes;
    // simple approach: gather all, then sort by offset at the end
    const size_t outStart = out.size();

    for (long long k = lastStepIndex + 1;; ++k)
    {
        const int idx = (int) (k % len);
        const auto& st = pat.steps[idx];
        const double swing = (k % 2 == 1) ? (double) p.swing * 0.5 : 0.0;
        const double t = anchorBeats + ((double) k + swing + (double) st.micro) * stepBeats;
        if (t >= end) break;
        lastStepIndex = k;
        uiStep = idx;
        lastMod = st.mod;

        if (! st.on) continue;
        if (st.prob < 1.0f && rng.uni() > st.prob) continue;

        // if the next step ties into this one, hold the note until just past the next step's start
        const auto& nx = pat.steps[(idx + 1) % len];
        const bool tieNext = nx.on && nx.tie && len > 1;
        const double nextT = anchorBeats + ((double) (k + 1) + ((k + 1) % 2 == 1 ? (double) p.swing * 0.5 : 0.0) + (double) nx.micro) * stepBeats;
        const double holdEnd = nextT + stepBeats * 0.02;

        // chord notes that start on this step (sequencer mode only)
        auto emitChord = [&] (int skipNote)
        {
            if (st.numChord == 0 || p.mode != (int) SeqMode::Sequencer) return;
            const int base = p.keyTranspose && lastKey >= 0 ? lastKey : 36 + p.root;
            for (int c = 0; c < st.numChord; ++c)
            {
                const int pc = p.quantize ? quantizeToScale (st.chord[c].pitch, p.scale) : st.chord[c].pitch;
                const int nn = juce::jlimit (0, 127, base + pc);
                if (nn == skipNote) continue;
                for (size_t q = 0; q < pending.size();)
                {
                    if (pending[q].note == nn) { out.push_back ({ offsetOf (t), false, nn, 0.0f, false, false, 0.0f }); pending.erase (pending.begin() + (long) q); }
                    else ++q;
                }
                SeqEvent ev;
                ev.sampleOffset = offsetOf (t);
                ev.noteOn = true; ev.note = nn;
                ev.accent = st.accent; ev.slide = false;
                ev.velocity = st.accent ? 1.0f : st.vel;
                ev.mod = st.mod;
                out.push_back (ev);
                pending.push_back ({ t + stepBeats * (double) st.chord[c].len * (double) p.gateScale, nn });
            }
        };

        // tie: extend currently sounding note through this step
        if (st.tie && soundingNote >= 0)
        {
            for (auto& pd : pending) if (pd.note == soundingNote) pd.beat = tieNext ? holdEnd : t + stepBeats * (double) st.gate * (double) p.gateScale;
            emitChord (soundingNote);
            continue;
        }

        int noteNum;
        if (p.mode == (int) SeqMode::Arpeggiator)
        {
            noteNum = arpNoteFor (p, k);
            if (noteNum < 0) continue;
            noteNum += 12 * st.octave;
        }
        else
        {
            const int base = p.keyTranspose && lastKey >= 0 ? lastKey : 36 + p.root;
            int semis = st.note;
            if (p.quantize) semis = quantizeToScale (semis, p.scale);
            noteNum = base + semis + 12 * st.octave;
        }
        noteNum = juce::jlimit (0, 127, noteNum);

        const int r = juce::jlimit (1, 4, st.ratchet);
        for (int j = 0; j < r; ++j)
        {
            const double ts = t + (double) j * stepBeats / (double) r;
            double gateLen = r > 1 ? stepBeats / (double) r * 0.5 : stepBeats * (double) st.gate * (double) p.gateScale;
            const bool slideOut = st.slide && j == r - 1;
            if (slideOut) gateLen = stepBeats * 1.08;   // overlap the next step -> legato slide
            else if (tieNext && j == r - 1) gateLen = std::max (gateLen, holdEnd - ts);
            // retrigger: remove pending off for same note, emit an off just before
            for (size_t q = 0; q < pending.size();)
            {
                if (pending[q].note == noteNum)
                {
                    if (! soundingSlideNext) out.push_back ({ offsetOf (ts), false, noteNum, 0.0f, false, false, 0.0f });
                    pending.erase (pending.begin() + (long) q);
                }
                else ++q;
            }
            SeqEvent ev;
            ev.sampleOffset = offsetOf (ts);
            ev.noteOn = true; ev.note = noteNum;
            ev.accent = st.accent; ev.slide = soundingSlideNext && j == 0;
            ev.velocity = st.accent ? 1.0f : st.vel;
            ev.mod = st.mod;
            out.push_back (ev);
            pending.push_back ({ ts + gateLen, noteNum });
            soundingNote = noteNum;
            soundingSlideNext = slideOut;
        }
        emitChord (noteNum);
    }

    // pending offs within this block
    for (size_t q = 0; q < pending.size();)
    {
        if (pending[q].beat < end)
        {
            out.push_back ({ offsetOf (pending[q].beat), false, pending[q].note, 0.0f, false, false, 0.0f });
            if (pending[q].note == soundingNote && ! soundingSlideNext) soundingNote = -1;
            pending.erase (pending.begin() + (long) q);
        }
        else ++q;
    }
    std::stable_sort (out.begin() + (long) outStart, out.end(), [] (const SeqEvent& a, const SeqEvent& b)
    {
        if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
        return ! a.noteOn && b.noteOn;   // offs before ons at the same sample
    });

    internalBeats = end;
    lastBlockEnd = end;
}

// ============================================================ export & variation
juce::MidiFile Sequencer::toMidiFile (const SeqData& data, const SeqParams& p, int patternIndex, double bpm)
{
    const int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / std::max (bpm, 20.0))), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    const auto& pat = data.patterns[juce::jlimit (0, kSeqPatterns - 1, patternIndex)];
    const int len = juce::jlimit (1, kSeqMaxSteps, p.length);
    const double stepTicks = seqRateBeats (p.rate) * tpq;
    int lastNote = -1; double lastOffTick = 0.0; juce::MidiMessageSequence::MidiEventHolder* lastOff = nullptr;
    for (int k = 0; k < len; ++k)
    {
        const auto& st = pat.steps[k];
        const double swing = (k % 2 == 1) ? (double) p.swing * 0.5 : 0.0;
        const double t = ((double) k + swing + (double) st.micro) * stepTicks;
        if (! st.on) continue;
        const auto& nx = pat.steps[(k + 1) % len];
        const bool tieNext = k + 1 < len && nx.on && nx.tie;
        const double nextT = ((double) (k + 1) + ((k + 1) % 2 == 1 ? (double) p.swing * 0.5 : 0.0) + (double) nx.micro) * stepTicks;
        auto addChord = [&] (int skipNote)
        {
            const auto vel = (juce::uint8) juce::jlimit (1, 127, st.accent ? 127 : (int) (st.vel * 110.0f));
            for (int c = 0; c < st.numChord; ++c)
            {
                const int pc = p.quantize ? quantizeToScale (st.chord[c].pitch, p.scale) : st.chord[c].pitch;
                const int nn = juce::jlimit (0, 127, 36 + p.root + pc);
                if (nn == skipNote) continue;
                const double ts = std::max (0.0, t);
                seq.addEvent (juce::MidiMessage::noteOn (1, nn, vel), ts);
                seq.addEvent (juce::MidiMessage::noteOff (1, nn), ts + stepTicks * st.chord[c].len * p.gateScale);
                lastOffTick = std::max (lastOffTick, ts + stepTicks * st.chord[c].len * p.gateScale);
            }
        };
        if (st.tie && lastNote >= 0 && lastOff != nullptr)
        {
            lastOff->message.setTimeStamp (tieNext ? nextT : t + stepTicks * st.gate * p.gateScale);
            lastOffTick = std::max (lastOffTick, lastOff->message.getTimeStamp());
            addChord (lastNote);
            continue;
        }
        int semis = st.note;
        if (p.quantize) semis = quantizeToScale (semis, p.scale);
        const int noteNum = juce::jlimit (0, 127, 36 + p.root + semis + 12 * st.octave);
        const int r = juce::jlimit (1, 4, st.ratchet);
        for (int j = 0; j < r; ++j)
        {
            const double ts = std::max (0.0, t + (double) j * stepTicks / r);
            double gl = r > 1 ? stepTicks / r * 0.5 : stepTicks * st.gate * p.gateScale;
            if (st.slide && j == r - 1) gl = stepTicks * 1.08;
            else if (tieNext && j == r - 1) gl = std::max (gl, nextT - ts);
            const auto vel = (juce::uint8) juce::jlimit (1, 127, st.accent ? 127 : (int) (st.vel * 110.0f));
            seq.addEvent (juce::MidiMessage::noteOn (1, noteNum, vel), ts);
            lastOff = seq.addEvent (juce::MidiMessage::noteOff (1, noteNum), ts + gl);
            lastOffTick = std::max (lastOffTick, ts + gl);
        }
        lastNote = noteNum;
        addChord (noteNum);
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), std::max (lastOffTick, (double) len * stepTicks));
    seq.updateMatchedPairs();
    seq.sort();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    return mf;
}

void Sequencer::makeVariation (SeqPattern& pat, int length, int scale, float amount, uint32_t seed)
{
    Rng r; r.seed (seed);
    const auto& iv = scaleIntervals (scale);
    for (int i = 0; i < length; ++i)
    {
        auto& s = pat.steps[i];
        if (r.uni() < amount * 0.35f) s.note = iv[(int) (r.uni() * (float) iv.size()) % iv.size()] + (r.uni() < 0.2f ? 12 : 0);
        if (r.uni() < amount * 0.15f) s.octave = juce::jlimit (-1, 1, s.octave + (r.uni() < 0.5f ? -1 : 1));
        if (r.uni() < amount * 0.2f) s.accent = ! s.accent;
        if (r.uni() < amount * 0.12f) s.slide = ! s.slide;
        if (i > 0 && r.uni() < amount * 0.1f) s.on = ! s.on;
        if (r.uni() < amount * 0.1f) s.ratchet = s.ratchet == 1 ? 2 : 1;
    }
}
} // namespace be

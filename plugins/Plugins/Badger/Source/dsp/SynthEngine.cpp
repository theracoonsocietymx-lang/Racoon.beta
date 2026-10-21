#include "SynthEngine.h"

namespace be
{
static constexpr float kHeadroom = 0.5f;   // -6 dB internal headroom before the FX chain

void SynthEngine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (block, 32);
    for (int q = 0; q < 3; ++q)
        for (int l = 0; l < kNumLayers; ++l)
        {
            layerOs[q][l] = std::make_unique<juce::dsp::Oversampling<float>> (2, q, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            layerOs[q][l]->initProcessing ((size_t) maxBlock);
        }
    zeroL.assign ((size_t) maxBlock, 0.0f); zeroR.assign ((size_t) maxBlock, 0.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        busL[l].assign ((size_t) maxBlock, 0.0f); busR[l].assign ((size_t) maxBlock, 0.0f);
        layerDelay[l][0].prepare ((int) (sampleRate * 0.006) + 8);
        layerDelay[l][1].prepare ((int) (sampleRate * 0.006) + 8);
    }
    mixL.assign ((size_t) maxBlock, 0.0f); mixR.assign ((size_t) maxBlock, 0.0f);
    preL.assign ((size_t) maxBlock, 0.0f); preR.assign ((size_t) maxBlock, 0.0f);
    distParL.assign ((size_t) maxBlock, 0.0f); distParR.assign ((size_t) maxBlock, 0.0f);
    for (int i = 0; i < kMaxVoices; ++i) voices[i].prepare (sampleRate * 4.0, 4, i);
    for (auto& d : dist) d.prepare (sampleRate, maxBlock);
    fx.prepare (sampleRate, maxBlock);
    ducker.prepare (sampleRate);
    sequencer.prepare (sampleRate);
    const float cr = (float) (sampleRate / 16.0);
    for (int i = 0; i < kNumLfos; ++i) { globalLfo[i].setRate (cr); globalLfo[i].seed (0xA11u + (uint32_t) i * 31u); }
    for (auto& s : analysisSplit) s.set (120.0f, (float) sampleRate);
    seqEvents.reserve (256);
    quality = -1;
    setQuality (1);
}

void SynthEngine::setQuality (int q)
{
    q = juce::jlimit (0, 2, q);
    if (q == quality) return;
    quality = q;
    osFactor = 1 << q;
    for (int i = 0; i < kMaxVoices; ++i) { voices[i].kill(); voices[i].setOversampling (sr * osFactor, osFactor); }
    for (int l = 0; l < kNumLayers; ++l) layerOs[q][l]->reset();
}

int SynthEngine::latencySamples() const
{
    return (int) std::round (layerOs[quality][0]->getLatencyInSamples());
}

void SynthEngine::allNotesOff()
{
    for (auto& v : voices) v.kill();
    numHeld = 0;
    sequencer.reset();
}

int SynthEngine::findVoiceToSteal (int maxVoices)
{
    int best = -1;
    // free voice first
    for (int i = 0; i < maxVoices; ++i) if (! voices[i].isActive()) return i;
    // oldest releasing
    juce::uint64 oldest = ~0ull;
    for (int i = 0; i < maxVoices; ++i)
        if (voices[i].isReleasing() && voices[i].getAge() < oldest) { oldest = voices[i].getAge(); best = i; }
    if (best >= 0) return best;
    oldest = ~0ull;
    for (int i = 0; i < maxVoices; ++i)
        if (voices[i].getAge() < oldest) { oldest = voices[i].getAge(); best = i; }
    return best < 0 ? 0 : best;
}

Voice* SynthEngine::leadVoice()
{
    Voice* lead = nullptr;
    for (auto& v : voices)
        if (v.isActive() && (lead == nullptr || v.getAge() > lead->getAge())) lead = &v;
    return lead;
}

void SynthEngine::pushHistory (int note, int vel, bool on)
{
    const int pos = telemetry.historyPos.load();
    telemetry.history[pos & 63] = { note, vel, on, timeSec };
    telemetry.historyPos.store (pos + 1);
}

void SynthEngine::noteOn (const PatchSnapshot& p, int note, float vel, int ch, bool fromSeq, bool accent, bool slide, float seqMod)
{
    const auto& vs = p.voice;
    if (note < vs.keyLo || note > vs.keyHi) return;
    pushHistory (note, (int) (vel * 127.0f), true);
    lastNoteHz = (float) noteToHz (note, vs.a4);
    if (! fromSeq) accent = accent || (int) std::round (vel * 127.0f) >= p.acid.accentThresh;

    // note stack (for mono priorities)
    bool wasHeld = numHeld > 0;
    for (int i = 0; i < numHeld; ++i) if (held[i].note == note && held[i].ch == ch) { for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1]; --numHeld; break; }
    if (numHeld < 32) held[numHeld++] = { note, vel, ch };

    const auto mode = (PlayMode) vs.mode;
    const uint32_t seed = seedCounter++ * 2654435761u;

    if (mode == PlayMode::Poly || vs.mpe)
    {
        const int maxV = juce::jlimit (1, kMaxVoices, vs.voices);
        // retrigger same note on same channel
        for (int i = 0; i < maxV; ++i)
            if (voices[i].isActive() && voices[i].getNote() == note && voices[i].getChannel() == ch && voices[i].isGateOn())
                voices[i].release();
        const int vi = findVoiceToSteal (maxV);
        voices[vi].start (p, note, vel, ch, false, accent, slide, seqMod, 0, 1, seed, lastPlayedNote);
        lastPlayedNote = (float) note;
        return;
    }

    // mono / legato / unison: decide priority note
    int target = note;
    if (vs.priority != 0 && numHeld > 1)
    {
        target = held[0].note;
        for (int i = 0; i < numHeld; ++i)
            target = vs.priority == 1 ? std::max (target, held[i].note) : std::min (target, held[i].note);
        if (target != note) return;   // new note doesn't win priority; keep current
    }
    const int stack = mode == PlayMode::Unison ? juce::jlimit (1, 8, vs.unison) : 1;
    Voice& v0 = voices[0];
    const bool legatoMode = mode == PlayMode::Legato || mode == PlayMode::Unison;
    const bool overlapping = wasHeld && v0.isActive() && v0.isGateOn();
    if ((legatoMode && overlapping) || (slide && v0.isActive()))
    {
        for (int s = 0; s < stack; ++s) voices[s].glideTo (p, note, vel, accent, true, seqMod);
    }
    else
    {
        const float startNote = v0.isActive() ? v0.getCurrentNote() : lastPlayedNote;
        for (int s = 0; s < stack; ++s)
            voices[s].start (p, note, vel, ch, false, accent, slide, seqMod, s, stack, seed + (uint32_t) s * 977u, startNote);
    }
    for (int s = stack; s < kMaxVoices; ++s) if (voices[s].isActive() && voices[s].isGateOn()) voices[s].release();
    lastPlayedNote = (float) note;
}

void SynthEngine::noteOff (const PatchSnapshot& p, int note, int ch)
{
    pushHistory (note, 0, false);
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note && (held[i].ch == ch || ! p.voice.mpe))
        {
            for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1];
            --numHeld;
            break;
        }
    const auto mode = (PlayMode) p.voice.mode;
    if (mode == PlayMode::Poly || p.voice.mpe)
    {
        for (auto& v : voices)
            if (v.isActive() && v.isGateOn() && v.getNote() == note && (! p.voice.mpe || v.getChannel() == ch)) v.release();
        return;
    }
    const int stack = mode == PlayMode::Unison ? juce::jlimit (1, 8, p.voice.unison) : 1;
    if (voices[0].getNote() != note) return;   // released note isn't sounding
    if (numHeld > 0)
    {
        // return to remaining note by priority
        int target = held[numHeld - 1].note; float vel = held[numHeld - 1].vel;
        if (p.voice.priority != 0)
            for (int i = 0; i < numHeld; ++i)
                if ((p.voice.priority == 1 && held[i].note > target) || (p.voice.priority == 2 && held[i].note < target)) { target = held[i].note; vel = held[i].vel; }
        if (mode == PlayMode::Mono)
            for (int s = 0; s < stack; ++s) voices[s].start (p, target, vel, ch, false, false, false, 0.0f, s, stack, seedCounter++, voices[0].getCurrentNote());
        else
            for (int s = 0; s < stack; ++s) voices[s].glideTo (p, target, vel, false, false, 0.0f);
        lastPlayedNote = (float) target;
    }
    else
        for (int s = 0; s < kMaxVoices; ++s) if (voices[s].isActive()) voices[s].release();
}

void SynthEngine::evalGlobalMod (const PatchSnapshot& p, float* out)
{
    const int count = ModDst::Count - ModDst::GlobalStart;
    std::fill (out, out + count, 0.0f);
    const Voice* lead = leadVoice();
    for (int i = 0; i < p.routes.count; ++i)
    {
        const auto& r = p.routes.r[i];
        if (! ModDst::isGlobal (r.dst)) continue;
        auto value = [&] (int src) -> float
        {
            if (src >= ModSrc::Macro1 && src < ModSrc::Macro1 + kNumMacros) return p.macros[src - ModSrc::Macro1];
            if (src >= ModSrc::Lfo1 && src < ModSrc::Lfo1 + kNumLfos && p.lfo[src - ModSrc::Lfo1].global) return gstate.lfo[src - ModSrc::Lfo1];
            switch (src)
            {
                case ModSrc::ModWheel: return gstate.modWheel;
                case ModSrc::Aftertouch: return gstate.aftertouch;
                case ModSrc::PitchBend: return gstate.pitchBend;
                case ModSrc::Expression: return gstate.expression;
                case ModSrc::Evolution: return gstate.evolution;
                case ModSrc::SeqMod: return gstate.seqMod;
                case ModSrc::XYX: return p.xyX;
                case ModSrc::XYY: return p.xyY;
                case ModSrc::Constant: return 1.0f;
                default: break;
            }
            return lead != nullptr ? lead->sourceValue (src) : 0.0f;
        };
        float s = value (r.src);
        if (r.via > 0) s *= value (r.via);
        if (r.quant > 0) s = std::round (s * (float) r.quant) / (float) r.quant;
        if (r.op == (int) ModOp::Invert) s = ModSrc::isBipolar (r.src) ? -s : 1.0f - s;
        if (r.op == (int) ModOp::Multiply) out[r.dst - ModDst::GlobalStart] += p.destBase[r.dst] * (std::abs (r.amount) * (s - 1.0f));
        else out[r.dst - ModDst::GlobalStart] += r.amount * s;
    }
}

void SynthEngine::process (const PatchSnapshot& p, const GlobalSettings& gs, const SeqData& seqData, const HostInfo& host,
                           juce::MidiBuffer& midi, float* outL, float* outR, int n,
                           float* const* layerOutL, float* const* layerOutR, const float* scL, const float* scR)
{
    jassert (n <= maxBlock);
    const double bpm = host.valid && host.bpm > 1.0 ? host.bpm : (double) gs.seq.bpm;
    const bool playing = host.valid && host.playing;
    const double blockBeats = playing ? host.beats : internalBeats;

    gstate.bpm = bpm;
    gstate.hostPlaying = playing;

    // --------------------------------------------------- sequencer events
    seqEvents.clear();
    const bool seqActive = gs.seq.mode != (int) SeqMode::Off;
    if (seqActive)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (gs.duck.source == (int) DuckSource::MidiNote && m.isNoteOnOrOff() && m.getNoteNumber() == gs.duck.note
                && (gs.duck.channel == 0 || m.getChannel() == gs.duck.channel)) continue;
            if (m.isNoteOn()) sequencer.keyDown (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) sequencer.keyUp (m.getNoteNumber());
        }
    }
    sequencer.process (seqData, gs.seq, n, playing, bpm, host.beats, seqEvents);
    telemetry.seqStep = sequencer.currentStep();
    gstate.seqMod = sequencer.currentMod();

    // preview phrase (from preset browser): simple 2-bar bass line
    if (previewOn)
    {
        static const int phrase[8] { 36, 36, 48, 36, 39, 36, 43, 34 };
        const int stepLen = (int) (sr * 0.1875);   // 1/8 at 160 bpm-ish
        for (int i = 0; i < n; ++i)
        {
            const int pos = previewPos + i;
            if (pos % stepLen == 0)
            {
                const int k = pos / stepLen;
                if (k >= 16) { previewOn = false; seqEvents.push_back ({ i, false, phrase[(k - 1) % 8], 0.0f }); break; }
                if (k > 0) seqEvents.push_back ({ i, false, phrase[(k - 1) % 8], 0.0f });
                seqEvents.push_back ({ i, true, phrase[k % 8], (k % 4 == 0) ? 1.0f : 0.75f, k % 4 == 0, false, 0.0f });
            }
        }
        previewPos += n;
        std::stable_sort (seqEvents.begin(), seqEvents.end(), [] (const SeqEvent& a, const SeqEvent& b) { return a.sampleOffset < b.sampleOffset; });
    }

    // --------------------------------------------------- oversampled layer buffers
    auto& osArr = layerOs[quality];
    juce::dsp::AudioBlock<float> osBlocks[kNumLayers];
    LayerBuses buses;
    for (int l = 0; l < kNumLayers; ++l)
    {
        float* z[2] { zeroL.data(), zeroR.data() };
        std::fill (zeroL.begin(), zeroL.begin() + n, 0.0f);
        std::fill (zeroR.begin(), zeroR.begin() + n, 0.0f);
        juce::dsp::AudioBlock<float> zb (z, 2, (size_t) n);
        osBlocks[l] = osArr[l]->processSamplesUp (zb);
        osBlocks[l].clear();
        buses.l[l] = osBlocks[l].getChannelPointer (0);
        buses.r[l] = osBlocks[l].getChannelPointer (1);
    }

    // --------------------------------------------------- render in control steps
    auto midiIt = midi.begin();
    size_t seqIt = 0;
    const int step = 16;
    float gmod[ModDst::Count - ModDst::GlobalStart] {};
    for (int pos = 0; pos < n; pos += step)
    {
        const int seg = std::min (step, n - pos);
        // MIDI events in this segment
        while (midiIt != midi.end() && (*midiIt).samplePosition < pos + seg)
        {
            const auto m = (*midiIt).getMessage();
            ++midiIt;
            const int ch = m.getChannel();
            if (m.isNoteOnOrOff() && gs.duck.source == (int) DuckSource::MidiNote && m.getNoteNumber() == gs.duck.note
                && (gs.duck.channel == 0 || ch == gs.duck.channel))
            {
                if (m.isNoteOn()) ducker.trigger (pos);
                continue;
            }
            if (m.isNoteOn())
            {
                if (! seqActive) noteOn (p, m.getNoteNumber(), m.getFloatVelocity(), ch, false, false, false, 0.0f);
            }
            else if (m.isNoteOff())
            {
                if (! seqActive) noteOff (p, m.getNoteNumber(), ch);
            }
            else if (m.isPitchWheel())
            {
                const float v = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
                if (p.voice.mpe && ch >= 2)
                {
                    mpeBend[ch] = v * (float) p.voice.mpeRange;
                    for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setPerNoteBend (mpeBend[ch]);
                }
                else bendNorm = v;
            }
            else if (m.isChannelPressure())
            {
                const float v = (float) m.getChannelPressureValue() / 127.0f;
                if (p.voice.mpe && ch >= 2) { for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setPressure (v); }
                else gstate.aftertouch = v;
            }
            else if (m.isAftertouch())
            {
                const float v = (float) m.getAfterTouchValue() / 127.0f;
                for (auto& vc : voices) if (vc.isActive() && vc.getNote() == m.getNoteNumber()) vc.setPressure (v);
            }
            else if (m.isController())
            {
                const int cc = m.getControllerNumber();
                const float v = (float) m.getControllerValue() / 127.0f;
                if (cc == 1) gstate.modWheel = v;
                else if (cc == 11) gstate.expression = v;
                else if (cc == 74 && p.voice.mpe) { for (auto& vc : voices) if (vc.isActive() && vc.getChannel() == ch) vc.setSlide (v); }
                else if (cc == 74) gstate.expression = gstate.expression;   // non-MPE CC74 is free for MIDI learn
                else if (cc == 123 || cc == 120) allNotesOff();
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
        }
        while (seqIt < seqEvents.size() && seqEvents[seqIt].sampleOffset < pos + seg)
        {
            const auto& e = seqEvents[seqIt++];
            if (e.noteOn) noteOn (p, e.note, e.velocity, 1, true, e.accent, e.slide, e.mod);
            else noteOff (p, e.note, 1);
        }

        // global modulators
        gstate.pitchBend = bendNorm;
        gstate.pitchBendSemis = bendNorm >= 0.0f ? bendNorm * (float) p.voice.bendUp : bendNorm * (float) p.voice.bendDown;
        const double beatsNow = blockBeats + (double) pos * bpm / 60.0 / sr;
        gstate.hostBeats = beatsNow;
        for (int l = 0; l < kNumLfos; ++l)
            if (p.lfo[l].global && p.lfoUsed[l])
                gstate.lfo[l] = globalLfo[l].tick (p.lfo[l], bpm, 1.0f, 0.0f, playing, beatsNow);
        {
            const double lenBeats = 4.0 * (double) (1 << juce::jlimit (0, 4, gs.evoLen));
            const float x = (float) std::fmod (beatsNow / lenBeats, 1.0);
            float e = x;
            switch (gs.evoShape)
            {
                case 1: e = 1.0f - x; break;
                case 2: e = x < 0.5f ? x * 2.0f : 2.0f - x * 2.0f; break;
                case 3: e = std::floor (x * 4.0f) / 3.0f; break;
                case 4: e = x * x * (3.0f - 2.0f * x); break;
                default: break;
            }
            gstate.evolution = clamp01 (e) * gs.evoAmount;
        }

        for (auto& v : voices)
            if (v.isActive()) v.render (p, gstate, buses, pos * osFactor, seg);
    }

    // --------------------------------------------------- downsample layers, align delays, mix
    std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + n, 0.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        float* d[2] { busL[l].data(), busR[l].data() };
        juce::dsp::AudioBlock<float> db (d, 2, (size_t) n);
        osArr[l]->processSamplesDown (db);
        const float dms = gs.layerDelayMs[l];
        for (int i = 0; i < n; ++i)
        {
            float a = busL[l][(size_t) i], b = busR[l][(size_t) i];
            if (dms > 0.001f)
            {
                layerDelay[l][0].push (a); layerDelay[l][1].push (b);
                const float ds = std::max (1.0f, dms * 0.001f * (float) sr);
                a = layerDelay[l][0].read (ds); b = layerDelay[l][1].read (ds);
                busL[l][(size_t) i] = a; busR[l][(size_t) i] = b;
            }
            mixL[(size_t) i] += a * kHeadroom; mixR[(size_t) i] += b * kHeadroom;
        }
    }

    // analysis taps: sub vs rest
    {
        float subE = 0.0f, restE = 0.0f, cross = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float sub = 0.5f * (busL[0][(size_t) i] + busR[0][(size_t) i]);
            const float rest = 0.5f * (mixL[(size_t) i] + mixR[(size_t) i]) - sub;
            float restLow, restHigh;
            analysisSplit[0].split (rest, restLow, restHigh);
            subE += sub * sub; restE += restLow * restLow; cross += sub * restLow;
        }
        lowA = 0.95f * lowA + 0.05f * subE; lowB = 0.95f * lowB + 0.05f * restE; lowCorrAcc = 0.95f * lowCorrAcc + 0.05f * cross;
        telemetry.lowCorrelation = (lowA > 1.0e-9f && lowB > 1.0e-9f) ? clampf (lowCorrAcc / std::sqrt (lowA * lowB), -1.0f, 1.0f) : 1.0f;
        telemetry.subTap.push (busL[0].data(), n);
    }

    // --------------------------------------------------- global modulation for FX
    evalGlobalMod (p, gmod);
    auto gval = [&] (int dst) { return destValue (p, dst, gmod[dst - ModDst::GlobalStart]); };
    DistSettings d0 = gs.dist[0], d1 = gs.dist[1];
    d0.drive = gval (ModDst::Dist1Drive); d0.mix = gval (ModDst::Dist1Mix);
    d1.drive = gval (ModDst::Dist2Drive); d1.mix = gval (ModDst::Dist2Mix);
    FxSettings fxs = gs.fx;
    fxs.chMix = gval (ModDst::ChorusMix); fxs.phMix = gval (ModDst::PhaserMix); fxs.flMix = gval (ModDst::FlangerMix);
    fxs.dlMix = gval (ModDst::DelayMix); fxs.dlFb = gval (ModDst::DelayFeedback); fxs.rvMix = gval (ModDst::ReverbMix);
    fxs.afCutoff = gval (ModDst::AutoFilterCutoff); fxs.imWidth = gval (ModDst::ImagerWidth);
    fxs.fsShift = gval (ModDst::FreqShift); fxs.rsMix = gval (ModDst::ResonatorMix);
    const float master = gval (ModDst::MasterVolume);
    const float duckMod = gmod[ModDst::DuckDepth - ModDst::GlobalStart];

    // layer outputs (pre FX)
    for (int l = 0; l < kNumLayers; ++l)
        if (layerOutL != nullptr && layerOutL[l] != nullptr)
            for (int i = 0; i < n; ++i) { layerOutL[l][i] = busL[l][(size_t) i] * master * kHeadroom; layerOutR[l][i] = busR[l][(size_t) i] * master * kHeadroom; }

    std::copy (mixL.begin(), mixL.begin() + n, preL.begin());
    std::copy (mixR.begin(), mixR.begin() + n, preR.begin());
    telemetry.scopePre.push (preL.data(), n);

    auto runDist = [&]
    {
        if (gs.distRouting == 0)
        {
            dist[0].process (d0, mixL.data(), mixR.data(), n, quality);
            dist[1].process (d1, mixL.data(), mixR.data(), n, quality);
        }
        else
        {
            std::copy (mixL.begin(), mixL.begin() + n, distParL.begin());
            std::copy (mixR.begin(), mixR.begin() + n, distParR.begin());
            dist[0].process (d0, mixL.data(), mixR.data(), n, quality);
            dist[1].process (d1, distParL.data(), distParR.data(), n, quality);
            const bool both = d0.on && d1.on;
            for (int i = 0; i < n; ++i)
            {
                if (both) { mixL[(size_t) i] = 0.5f * (mixL[(size_t) i] + distParL[(size_t) i]); mixR[(size_t) i] = 0.5f * (mixR[(size_t) i] + distParR[(size_t) i]); }
                else if (d1.on) { mixL[(size_t) i] = distParL[(size_t) i]; mixR[(size_t) i] = distParR[(size_t) i]; }
            }
        }
    };
    if (gs.distPosition == 0) runDist();
    Voice* lv = leadVoice();
    const float noteHz = lv != nullptr ? (float) noteToHz (lv->getCurrentNote(), p.voice.a4) : lastNoteHz;
    fx.process (fxs, mixL.data(), mixR.data(), n, bpm, blockBeats, playing, noteHz, nullptr);
    if (gs.distPosition == 1) runDist();
    ducker.process (gs.duck, mixL.data(), mixR.data(), n, bpm, blockBeats, playing, scL, scR, duckMod);

    // --------------------------------------------------- output & telemetry
    float pkL = 0, pkR = 0, sqL = 0, sqR = 0, cr = 0;
    for (int i = 0; i < n; ++i)
    {
        float a = mixL[(size_t) i] * master, b = mixR[(size_t) i] * master;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        outL[i] = a; outR[i] = b;
        pkL = std::max (pkL, std::abs (a)); pkR = std::max (pkR, std::abs (b));
        sqL += a * a; sqR += b * b; cr += a * b;
    }
    telemetry.scope.push (outL, n);
    telemetry.peakL = std::max (pkL, telemetry.peakL.load() * 0.92f);
    telemetry.peakR = std::max (pkR, telemetry.peakR.load() * 0.92f);
    telemetry.rmsL = 0.9f * telemetry.rmsL.load() + 0.1f * std::sqrt (sqL / (float) n);
    telemetry.rmsR = 0.9f * telemetry.rmsR.load() + 0.1f * std::sqrt (sqR / (float) n);
    corrL = 0.9f * corrL + 0.1f * sqL; corrR = 0.9f * corrR + 0.1f * sqR; corrAcc = 0.9f * corrAcc + 0.1f * cr;
    telemetry.correlation = (corrL > 1.0e-9f && corrR > 1.0e-9f) ? clampf (corrAcc / std::sqrt (corrL * corrR), -1.0f, 1.0f) : 1.0f;
    telemetry.duckGr = ducker.gainReductionDb();
    int active = 0;
    for (auto& v : voices) active += v.isActive() ? 1 : 0;
    telemetry.activeVoices = active;
    if (lv != nullptr)
    {
        telemetry.leadNote = lv->getCurrentNote();
        const float* ms = lv->modAdd();
        for (int d = 0; d < ModDst::GlobalStart; ++d) telemetry.modDisplay[d].store (ms[d], std::memory_order_relaxed);
        for (int s = 0; s < ModSrc::Count; ++s) telemetry.srcDisplay[s].store (lv->sourceValue (s), std::memory_order_relaxed);
    }
    else
    {
        telemetry.leadNote = -1.0f;
        for (int d = 0; d < ModDst::GlobalStart; ++d) telemetry.modDisplay[d].store (0.0f, std::memory_order_relaxed);
    }
    for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d) telemetry.modDisplay[d].store (gmod[d - ModDst::GlobalStart], std::memory_order_relaxed);
    for (int m = 0; m < kNumMacros; ++m) telemetry.srcDisplay[ModSrc::Macro1 + m].store (p.macros[m], std::memory_order_relaxed);
    for (int l = 0; l < kNumLfos; ++l) if (p.lfo[l].global) telemetry.srcDisplay[ModSrc::Lfo1 + l].store (gstate.lfo[l], std::memory_order_relaxed);

    if (! playing) internalBeats += (double) n * bpm / 60.0 / sr;
    else internalBeats = host.beats;
    timeSec += (double) n / sr;
}
} // namespace be

// Jerboa checks (included by Tests/FamilyTest.cpp)
#include "JerboaProcessor.h"

namespace jerboa_test
{
using namespace juce;

struct Head : AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

// renders `in` through Jerboa with the song playing from beat 0 at 120 BPM (one beat = 24000 samples at 48 kHz)
static AudioBuffer<float> play (racoon::jerboa::Processor& p, const AudioBuffer<float>& in, MidiBuffer* midi = nullptr, bool playing = true)
{
    const double sr = 48000.0;
    Head head; head.playing = playing;
    p.setPlayHead (&head);
    p.setRateAndBufferSizeDetails (sr, 256);
    p.prepareToPlay (sr, 256);
    AudioBuffer<float> out (2, in.getNumSamples());
    AudioBuffer<float> io (2, 256);
    for (int pos = 0; pos < in.getNumSamples(); pos += 256)
    {
        const int n = jmin (256, in.getNumSamples() - pos);
        head.ppq = pos / (60.0 / head.bpm * sr);
        io.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, n);
        MidiBuffer m;
        if (midi != nullptr)
            for (const auto ev : *midi)
                if (ev.samplePosition >= pos && ev.samplePosition < pos + n) m.addEvent (ev.getMessage(), ev.samplePosition - pos);
        p.processBlock (io, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    p.setPlayHead (nullptr);
    return out;
}
}

static void testJerboa (const juce::File& out)
{
    using namespace juce;
    using racoon::jerboa::Processor;
    namespace jb = racoon::jerboa;
    commonChecks ("Jerboa", [] { return new Processor(); }, out);
    std::cout << "tiempo y volumen" << std::endl;
    const double sr = 48000.0;
    const int beat = 24000;                       // at 120 BPM
    auto plain = [] (Processor& p, int slot) { p.setRealValue ("patron", (float) slot); p.setRealValue ("tono", 0); p.setRealValue ("salida", 0); };

    // a "time code": every sample says where it was in the input (a slow ramp), so the output tells what was read
    AudioBuffer<float> code (2, beat * 8);
    for (int i = 0; i < code.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) code.setSample (c, i, (float) i / (float) code.getNumSamples());
    auto slopeAt = [&] (const AudioBuffer<float>& o, int at) { return (o.getSample (0, at + 200) - o.getSample (0, at - 200)) / (400.0f / (float) code.getNumSamples()); };

    // Directo: with the song playing the input comes out bit for bit; stopped, too
    {
        AudioBuffer<float> in (2, beat * 4); tu::fillNoise (in, 0.5f, 7);
        Processor p; auto o = jerboa_test::play (p, in);
        Processor q; auto o2 = jerboa_test::play (q, in, nullptr, false);
        float diff = 0.0f, diff2 = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < in.getNumSamples(); ++i)
        { diff = jmax (diff, std::abs (o.getSample (c, i) - in.getSample (c, i))); diff2 = jmax (diff2, std::abs (o2.getSample (c, i) - in.getSample (c, i))); }
        check (diff == 0.0f && diff2 == 0.0f, "Directo deja pasar el audio exacto, con la canción sonando y parada (diferencia "_u + String (diff) + " / " + String (diff2) + ")");
    }

    // Media velocidad: the read point moves at half the speed of the song; a 440 Hz tone comes out at 220
    {
        AudioBuffer<float> in (2, beat * 8); tu::fillSine (in, 440.0, sr, 0.5f, 0.0);
        Processor q; q.setRealValue ("patron", 2); q.setRealValue ("tono", 0);
        auto s = jerboa_test::play (q, in);
        Processor r; r.setRealValue ("patron", 2); r.setRealValue ("tono", 0);
        auto t = jerboa_test::play (r, code);
        auto sp = tu::spectrum (s, 0, beat * 5 + 2000, 14);
        size_t best = 1; for (size_t k = 1; k < sp.size(); ++k) if (sp[k] > sp[best]) best = k;
        const double f = (double) best * sr / 16384.0;
        const float sl = slopeAt (t, beat * 6);
        check (std::abs (f - 220.0) < 6.0 && std::abs (sl - 0.5f) < 0.02f, "Media velocidad: 440 Hz salen en " + String (f, 1) + " Hz, el tiempo avanza a " + String (sl, 3) + "x");
    }

    // Reversa 1/4 (patrón 10): in the middle of each beat the time code goes backwards at normal speed
    {
        Processor p; plain (p, 10);
        auto o = jerboa_test::play (p, code);
        const float a = slopeAt (o, beat * 4 + beat / 2), b = slopeAt (o, beat * 5 + beat / 2);
        check (std::abs (a + 1.0f) < 0.02f && std::abs (b + 1.0f) < 0.02f, "Reversa 1/4: a mitad de cada tiempo el audio va hacia atrás ("_u + String (a, 3) + "x, " + String (b, 3) + "x)");
    }

    // Freno de cinta (patrón 20): normal speed in beat 1, a quarter at 3.5, and silence at the end of the bar
    {
        Processor p; plain (p, 20);
        auto o = jerboa_test::play (p, code);
        const float s1 = slopeAt (o, beat * 4 + beat / 2), s35 = slopeAt (o, beat * 4 + beat * 7 / 2);
        Processor q; plain (q, 20);
        AudioBuffer<float> in (2, beat * 8); tu::fillSine (in, 220.0, sr, 0.5f, 0.0);
        auto o2 = jerboa_test::play (q, in);
        const float endRms = tu::rms (o2, beat * 8 - beat / 64, beat / 64), startRms = tu::rms (o2, beat * 4 + 1000, beat / 4);
        check (std::abs (s1 - 1.0f) < 0.02f && std::abs (s35 - 0.25f) < 0.03f && endRms < startRms * 0.1f,
               "Freno de cinta: velocidad " + String (s1, 2) + "x al empezar, " + String (s35, 2) + "x en el tiempo 3.5, y se apaga al final del compás ("_u
               + String (Decibels::gainToDecibels (endRms / startRms + 1.0e-9f), 1) + " dB)");
    }

    // Compuerta 1/8 (patrón 4): open 60 % of each eighth, closed the rest, without clicks
    {
        Processor p; plain (p, 4);
        AudioBuffer<float> in (2, beat * 8); tu::fillSine (in, 220.0, sr, 0.5f, 0.0);
        auto o = jerboa_test::play (p, in);
        const int e = beat / 2;
        const float open = tu::rms (o, beat * 4 + e / 10, e / 3), closed = tu::rms (o, beat * 4 + e * 7 / 10, e / 5);
        check (Decibels::gainToDecibels (closed / open + 1.0e-9f) < -40.0f && tu::maxJump (o) < 0.06f,
               "Compuerta 1/8: cerrada " + String (Decibels::gainToDecibels (closed / open + 1.0e-9f), 1) + " dB, salto máximo "_u + String (tu::maxJump (o), 3));
    }

    // jumps are crossfaded: a stutter of 1/16 on a tone moves no more per sample than the tone plus a little
    {
        Processor p; plain (p, 8);
        AudioBuffer<float> in (2, beat * 8); tu::fillSine (in, 220.0, sr, 0.5f, 0.0);
        auto o = jerboa_test::play (p, in);
        const float j = tu::maxJump (o), ref = tu::maxJump (in);
        check (j < ref * 2.5f && tu::allFinite (o), "tartamudeo 1/16 sin clics: salto máximo "_u + String (j, 4) + " (el tono solo " + String (ref, 4) + ")");
    }

    // notes switch on the beat: Do 2 + 3 (Compuerta 1/8) in the middle of beat 5 with "En 1/4" starts the gate on beat 6
    {
        Processor p; plain (p, 1); p.setRealValue ("cambio", 3);
        AudioBuffer<float> in (2, beat * 8); tu::fillSine (in, 220.0, sr, 0.5f, 0.0);
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, Processor::firstNote + 3, (uint8) 100), beat * 4 + beat / 2);
        auto o = jerboa_test::play (p, in, &m);
        const int e = beat / 2;
        const float beforeGap = tu::rms (o, beat * 4 + beat / 2 + e * 7 / 10, e / 5);      // still Directo: no gap
        const float afterGap = tu::rms (o, beat * 5 + e * 7 / 10, e / 5);                   // gated from beat 6 on
        check (p.uiTimePattern.load() == 3 && beforeGap > 0.2f && afterGap < 0.01f, "la nota Re# 2 cambia a Compuerta 1/8 en el siguiente tiempo, no antes");
    }

    // hold: the last quarter repeats while it is on
    {
        Processor p; plain (p, 1); p.setRealValue ("mantener", 1); p.setRealValue ("mantlargo", 2);
        AudioBuffer<float> in (2, beat * 8); tu::fillNoise (in, 0.4f, 3);
        auto o = jerboa_test::play (p, in);
        float diff = 0.0f;
        for (int i = beat * 5; i < beat * 6; ++i) diff = jmax (diff, std::abs (o.getSample (0, i) - o.getSample (0, i + beat)));
        check (diff < 1.0e-5f, "Mantener repite el último 1/4 igual, vuelta tras vuelta (diferencia "_u + String (diff) + ")");
    }

    // the dark rule: no factory pattern plays forward faster than the original, except the scratches
    {
        String fast; int checked = 0;
        for (int b = 0; b < jb::numBanks; ++b)
            for (int k = 0; k < jb::patternsPerBank; ++k)
            {
                const auto& pt = jb::pattern (b, k);
                if (pt.time == jb::T::rascado || pt.time == jb::T::rascadoLento || pt.time == jb::T::rascado4) continue;
                ++checked;
                for (int i = 0; i < 4000; ++i)
                {
                    const double u0 = jb::barBeats * i / 4000.0, u1 = u0 + jb::barBeats / 4000.0;
                    const double dD = jb::timeAt (pt.time, u1) - jb::timeAt (pt.time, u0);
                    if (std::abs (dD) > 0.05) continue;                       // a jump: crossfaded, not played
                    if (1.0 - dD / (u1 - u0) > 1.001) { fast << pt.name << " "; break; }
                }
            }
        check (fast.isEmpty(), String (checked) + " patrones de fábrica nunca aceleran el audio (sin voces de pito)" + (fast.isEmpty() ? String() : ": " + fast));
    }

    // every preset with a beat going through it: finite and not louder than it came in
    {
        Processor probe;
        const auto presets = probe.factoryPresets();
        String bad;
        AudioBuffer<float> in (2, beat * 8); tu::fillNoise (in, 0.3f, 11);
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            Processor p;
            p.prepareToPlay (sr, 256);
            p.applyFactoryPreset (i);
            auto o = jerboa_test::play (p, in);
            if (! tu::allFinite (o) || tu::peak (o, 0, o.getNumSamples()) > 1.2f) bad << presets[(size_t) i].name << " ";
        }
        check (bad.isEmpty(), String ((int) presets.size()) + " presets estables con ruido pasando" + (bad.isEmpty() ? String() : ": " + bad));
    }
}

static RegisterTest regJerboa ("Jerboa", testJerboa);

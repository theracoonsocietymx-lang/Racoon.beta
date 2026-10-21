// Ferret checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Ferret/FerretProcessor.h"

static void testFerret (const juce::File& out)
{
    using namespace juce;
    using racoon::ferret::Processor;
    commonChecks ("Ferret", [] { return new Processor(); }, out);
    std::cout << "delay" << std::endl;
    const double sr = 48000.0;

    auto impulse = [&] (std::function<void (Processor&)> setup, int len)
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        setup (p);
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, len); in.clear();
        in.setSample (0, 0, 1.0f); in.setSample (1, 0, 1.0f);
        return tu::render (p, in, 256, nullptr);
    };
    auto peakIn = [] (const AudioBuffer<float>& b, int ch, int from, int to)
    {
        int at = from; float best = 0;
        for (int i = from; i < to; ++i) if (std::abs (b.getSample (ch, i)) > best) { best = std::abs (b.getSample (ch, i)); at = i; }
        return std::make_pair (at, best);
    };

    // 120 BPM (no host: Racoon's default), 1/4 = 0.5 s = 24000 samples; echoes exactly there
    {
        auto o = impulse ([] (Processor& p) { p.setRealValue ("divl", 4); p.setRealValue ("divr", 4); p.setRealValue ("feedback", 50); p.setRealValue ("hp", 20); p.setRealValue ("lp", 20000); p.setRealValue ("mix", 50); }, (int) sr * 2);
        auto e1 = peakIn (o, 0, 1000, 36000), e2 = peakIn (o, 0, 36000, 60000), e3 = peakIn (o, 0, 60000, 84000);
        check (e1.first == 24000 && e2.first == 48000 && e3.first == 72000,
               "impulso a 120 BPM, negra: ecos en " + String (e1.first) + ", " + String (e2.first) + ", " + String (e3.first) + " (esperados 24000, 48000, 72000)");
        check (std::abs (e2.second / e1.second - 0.5f) < 0.02f, "cada repetición baja según las repeticiones ("_u + String (e2.second / e1.second, 3) + ")");
    }

    // ping-pong alternates sides
    {
        auto o = impulse ([] (Processor& p) { p.setRealValue ("mode", 1); p.setRealValue ("divl", 7); p.setRealValue ("feedback", 60); p.setRealValue ("mix", 50); p.setRealValue ("hp", 20); p.setRealValue ("lp", 20000); }, (int) sr);
        const int step = (int) (sr * 0.25);
        const float l1 = std::abs (o.getSample (0, step)), r1 = std::abs (o.getSample (1, step));
        const float l2 = std::abs (o.getSample (0, 2 * step)), r2 = std::abs (o.getSample (1, 2 * step));
        check (l1 > 10.0f * r1 && r2 > 10.0f * l2, "ping-pong: primer eco a la izquierda, segundo a la derecha");
    }

    // maximum feedback never runs away
    {
        auto o = impulse ([] (Processor& p) { p.setRealValue ("feedback", 98); p.setRealValue ("divl", 13); p.setRealValue ("divr", 13); p.setRealValue ("drive", 100); p.setRealValue ("diffusion", 100); }, (int) sr * 20);
        float late = tu::peak (o, (int) sr * 18, (int) sr * 2);
        check (tu::allFinite (o) && late < 1.0f, "repeticiones al 98 % durante 20 s: estable (pico final " + String (late, 3) + ")");
    }

    // freeze keeps the loop at the same level without growing
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("divl", 7); p.setRealValue ("divr", 7); p.setRealValue ("feedback", 50); p.setRealValue ("mix", 100);
        AudioBuffer<float> in (2, (int) sr * 12);
        tu::fillNoise (in, 0.3f, 4);
        for (int c = 0; c < 2; ++c) in.clear (c, (int) sr, in.getNumSamples() - (int) sr);
        AudioBuffer<float> o (2, in.getNumSamples()), io (2, 256);
        for (int pos = 0; pos + 256 <= in.getNumSamples(); pos += 256)
        {
            if (pos == (int) (sr * 0.9) / 256 * 256) p.setRealValue ("freeze", 1);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, 256);
            MidiBuffer m; p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 256);
        }
        const float a = tu::rms (o, (int) sr * 3, (int) sr), b = tu::rms (o, (int) sr * 10, (int) sr);
        check (a > 0.01f && b / a > 0.7f && b / a < 1.3f, "congelar: el ciclo se mantiene (" + String (b / a, 3) + " entre los segundos 3 y 10)");
    }

    // changing the time while playing: no click
    {
        Processor p;
        p.prepareToPlay (sr, 128);
        p.setRealValue ("sync", 0); p.setRealValue ("timel", 300); p.setRealValue ("timer", 300); p.setRealValue ("mix", 100); p.setRealValue ("feedback", 30);
        AudioBuffer<float> sine (2, (int) sr * 2), io (2, 128), o (2, (int) sr * 2);
        tu::fillSine (sine, 220.0, sr, 0.3f, 0.0);
        for (int pos = 0; pos + 128 <= sine.getNumSamples(); pos += 128)
        {
            if (pos == 128 * 400) { p.setRealValue ("timel", 420); p.setRealValue ("timer", 180); }
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, sine, c, pos, 128);
            MidiBuffer m; p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 128);
        }
        AudioBuffer<float> seg (2, 8000);
        for (int c = 0; c < 2; ++c) seg.copyFrom (c, 0, o, c, 128 * 398, 8000);
        check (tu::maxJump (seg) < 0.06f, "cambiar el tiempo sin clic (salto " + String (tu::maxJump (seg), 3) + ")");
    }

    // tail keeps ringing after bypass
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("divl", 7); p.setRealValue ("divr", 7); p.setRealValue ("feedback", 60); p.setRealValue ("mix", 100);
        AudioBuffer<float> io (2, 256);
        float tail = 0;
        for (int k = 0; k < 200; ++k)
        {
            if (k < 40) tu::fillNoise (io, 0.3f, k); else io.clear();
            if (k == 40) p.getBypassParameter()->setValue (1.0f);
            MidiBuffer m; p.processBlock (io, m);
            if (k > 60 && k < 120) tail = jmax (tail, io.getMagnitude (0, 256));
        }
        check (tail > 0.01f, "con bypass la cola sigue sonando (" + String (tail, 3) + ")");
    }
}
static RegisterTest regFerret ("delay", testFerret);

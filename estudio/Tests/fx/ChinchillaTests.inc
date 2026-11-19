// Chinchilla checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Chinchilla/ChinchillaProcessor.h"

static void testChinchilla (const juce::File& out)
{
    using namespace juce;
    using racoon::chinchilla::Processor;
    commonChecks ("Chinchilla", [] { return new Processor(); }, out);
    std::cout << "reverb" << std::endl;
    const double sr = 48000.0;

    auto renderWith = [&] (std::function<void (Processor&)> setup, const AudioBuffer<float>& in)
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        setup (p);
        p.prepareToPlay (sr, 256);
        return tu::render (p, in, 256, nullptr);
    };

    // RT60 by Schroeder backward integration of the impulse response (T20 extrapolated)
    auto measureRT60 = [&] (const AudioBuffer<float>& ir, int from)
    {
        const int n = ir.getNumSamples() - from;
        std::vector<double> e ((size_t) n);
        double acc = 0;
        for (int i = n - 1; i >= 0; --i)
        {
            const double l = ir.getSample (0, from + i), r = ir.getSample (1, from + i);
            acc += l * l + r * r;
            e[(size_t) i] = acc;
        }
        if (e[0] <= 0) return 0.0;
        int i5 = -1, i25 = -1;
        for (int i = 0; i < n; ++i)
        {
            const double db = 10.0 * std::log10 (e[(size_t) i] / e[0] + 1e-30);
            if (i5 < 0 && db <= -5.0) i5 = i;
            if (i25 < 0 && db <= -25.0) { i25 = i; break; }
        }
        if (i5 < 0 || i25 < 0) return 0.0;
        return 3.0 * (i25 - i5) / sr;
    };

    for (float target : { 1.0f, 2.5f, 6.0f })
    {
        for (int q = 0; q < 2; ++q)
        {
            AudioBuffer<float> in (2, (int) (sr * (target * 1.6 + 1.0))); in.clear();
            in.setSample (0, 0, 1.0f); in.setSample (1, 0, 1.0f);
            auto o = renderWith ([target, q] (Processor& p)
            {
                p.setRealValue ("rt60", target); p.setRealValue ("mix", 100); p.setRealValue ("wetonly", 1);
                p.setRealValue ("damping", 0); p.setRealValue ("bassmult", 1); p.setRealValue ("hp", 20); p.setRealValue ("lp", 20000);
                p.setRealValue ("predelay", 0); p.setRealValue ("early", 0); p.setRealValue ("quality", q);
            }, in);
            const double rt = measureRT60 (o, 0);
            check (std::abs (rt / target - 1.0) < 0.3, "RT60 " + String (target, 1) + " s (" + String (q ? "HQ" : "ECO") + "): medido "
                   + String (rt, 2) + " s");
        }
    }

    // a loud input never produces numeric clipping inside the tank
    {
        AudioBuffer<float> in (2, (int) sr * 4);
        tu::fillNoise (in, 1.0f, 9);
        auto o = renderWith ([] (Processor& p) { p.setRealValue ("rt60", 20); p.setRealValue ("mix", 100); p.setRealValue ("damping", 0); }, in);
        check (tu::allFinite (o) && tu::peak (o, 0, o.getNumSamples()) < 8.0f, "ruido a 0 dBFS con cola de 20 s: sin desbordes (pico "
               + String (tu::peak (o, 0, o.getNumSamples()), 2) + ")");
    }

    // tail keeps ringing after the input stops, and the wet level sits near the input level
    {
        AudioBuffer<float> in (2, (int) sr * 4);
        tu::fillNoise (in, 0.3f, 2);
        for (int c = 0; c < 2; ++c) in.clear (c, (int) (sr * 0.5), in.getNumSamples() - (int) (sr * 0.5));
        auto o = renderWith ([] (Processor& p) { p.setRealValue ("rt60", 3); p.setRealValue ("mix", 100); p.setRealValue ("wetonly", 1); p.setRealValue ("damping", 20); }, in);
        const float inLvl = tu::rms (in, 0, (int) (sr * 0.5));
        const float t0 = tu::rms (o, (int) (sr * 0.25), (int) (sr * 0.25)), t1 = tu::rms (o, (int) (sr * 1.5), (int) (sr * 0.25));
        const float relDb = Decibels::gainToDecibels (t0 / inLvl), tailDb = Decibels::gainToDecibels (t1 / t0);
        check (relDb > -18.0f && relDb < 6.0f, "nivel del efecto frente a la entrada: "_u + String (relDb, 1) + " dB");
        check (tailDb > -40.0f && tailDb < -10.0f, "la cola continúa después de que la entrada se calla ("_u + String (tailDb, 1) + " dB un segundo después)"_u);
    }

    // freeze holds the level without growing
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("rt60", 4); p.setRealValue ("mix", 100); p.setRealValue ("wetonly", 1);
        AudioBuffer<float> in (2, (int) sr * 14);
        tu::fillNoise (in, 0.3f, 5);
        for (int c = 0; c < 2; ++c) in.clear (c, (int) sr * 2, in.getNumSamples() - (int) sr * 2);
        AudioBuffer<float> o (2, in.getNumSamples()), io (2, 256);
        for (int pos = 0; pos + 256 <= in.getNumSamples(); pos += 256)
        {
            if (pos == (int) (sr * 1.8) / 256 * 256) p.setRealValue ("freeze", 1);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, 256);
            MidiBuffer m; p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 256);
        }
        const float a = tu::rms (o, (int) sr * 4, (int) sr), b = tu::rms (o, (int) sr * 12, (int) sr);
        check (tu::allFinite (o) && a > 0.005f && b / a > 0.5f && b / a < 1.25f, "congelar: la cola se mantiene sin crecer ("
               + String (b / a, 3) + " entre los segundos 4 y 12)");
    }

    // with bypass on, the tail rings out (tailOnBypass)
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("rt60", 3); p.setRealValue ("mix", 50);
        AudioBuffer<float> io (2, 256), o (2, (int) sr * 2);
        Random r (3);
        for (int pos = 0; pos + 256 <= o.getNumSamples(); pos += 256)
        {
            if (pos == (int) sr / 256 * 256) p.setRealValue ("bypass", 1);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) io.setSample (c, i, pos < sr ? (r.nextFloat() * 2 - 1) * 0.3f : 0.0f);
            MidiBuffer m; p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 256);
        }
        check (tu::rms (o, (int) (sr * 1.3), (int) (sr * 0.3)) > 0.002f, "con bypass la cola se apaga sola en lugar de cortarse");
    }

    // clearing the tail empties it
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("rt60", 10); p.setRealValue ("mix", 100);
        AudioBuffer<float> io (2, 256);
        for (int k = 0; k < 100; ++k) { tu::fillNoise (io, 0.3f, k); MidiBuffer m; p.processBlock (io, m); }
        p.clearRequest = true;
        float last = 0;
        for (int k = 0; k < 40; ++k) { io.clear(); MidiBuffer m; p.processBlock (io, m); last = tu::rms (io, 0, 256); }
        check (last < 1.0e-4f, "VACIAR COLA deja el espacio en silencio");
    }

    // procedural IRs: each one exists, decays, and the convolution mode sounds
    {
        for (int k = 0; k < 5; ++k)
        {
            auto ir = Processor::makeIR (k, sr, 2.0f, 100.0f, 40.0f, false);
            const float head = tu::rms (ir, 0, (int) (sr * 0.2)), tail = tu::rms (ir, ir.getNumSamples() - (int) (sr * 0.2), (int) (sr * 0.2));
            check (ir.getNumSamples() > sr * 0.5 && tu::allFinite (ir) && tail < head * 0.05f,
                   "IR original " + Processor::irNames()[k] + ": " + String (ir.getNumSamples() / sr, 2) + " s, decae");
        }
        Processor p;
        p.prepareToPlay (sr, 512);
        p.setRealValue ("algo", 1); p.setRealValue ("ir", 0); p.setRealValue ("mix", 100); p.setRealValue ("wetonly", 1);
        Thread::sleep (1500);   // background build
        AudioBuffer<float> io (2, 512);
        float energy = 0;
        for (int k = 0; k < 200; ++k)
        {
            io.clear();
            if (k == 0) { io.setSample (0, 0, 1.0f); io.setSample (1, 0, 1.0f); }
            MidiBuffer m; p.processBlock (io, m);
            if (k > 2) energy += tu::rms (io, 0, 512);
            if (k % 20 == 0) Thread::sleep (10);
        }
        check (energy > 0.01f, "modo convolución: el impulso produce cola ("_u + String (energy, 3) + ")");
    }
}

static RegisterTest regChinchilla ("reverb", testChinchilla);

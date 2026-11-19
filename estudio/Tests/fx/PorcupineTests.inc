// Porcupine checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Porcupine/PorcupineProcessor.h"

static void testPorcupine (const juce::File& out)
{
    using namespace juce;
    using racoon::porcupine::Processor;
    commonChecks ("Porcupine", [] { return new Processor(); }, out);
    std::cout << "ecualizador" << std::endl;
    const double sr = 48000.0;
    auto B = [] (int b, const char* w) { return Processor::bandId (b, w); };

    // null test: flat EQ leaves the signal untouched
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, 48000);
        tu::fillNoise (in, 0.5f, 5);
        auto o = tu::render (p, in, 256, nullptr);
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < in.getNumSamples(); ++i) diff = jmax (diff, std::abs (o.getSample (c, i) - in.getSample (c, i)));
        check (diff < 1.0e-5f, "plano: la salida es igual a la entrada (dif. " + String (diff, 8) + ")");
    }

    auto gainAt = [&] (Processor& p, double f)
    {
        AudioBuffer<float> in (2, 48000);
        tu::fillSine (in, f, sr, 0.25f, 0.0);
        auto o = tu::render (p, in, 256, nullptr);
        return racoon::dsp::gainToDb (tu::tone (o, 0, 24000, 16384, f, sr) / 0.25f);
    };

    // a +6 dB bell at 1 kHz, measured against the design
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue (B (3, "freq"), 1000); p.setRealValue (B (3, "gain"), 6); p.setRealValue (B (3, "q"), 1);
        p.setRealValue (B (1, "gain"), -4); p.setRealValue (B (6, "gain"), 3);
        p.prepareToPlay (sr, 256);
        float worst = 0;
        for (double f : { 60.0, 300.0, 1000.0, 3000.0, 9000.0 })
        {
            const float meas = gainAt (p, f);
            const float design = (float) (20.0 * std::log10 (p.magnitudeAt (f, false)));
            worst = jmax (worst, std::abs (meas - design));
        }
        check (worst < 0.2f, "respuesta medida igual al diseño en 5 frecuencias (error máx "_u + String (worst, 3) + " dB)");
        check (std::abs (gainAt (p, 1000.0) - (float) (20.0 * std::log10 (p.magnitudeAt (1000.0, false)))) < 0.2f, "campana de +6 dB en 1 kHz");
    }

    // 48 dB/oct high pass
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue (B (0, "on"), 1); p.setRealValue (B (0, "freq"), 100); p.setRealValue (B (0, "slope"), 3);
        p.prepareToPlay (sr, 256);
        const float g25 = gainAt (p, 25.0), g400 = gainAt (p, 400.0);
        check (g25 < -40.0f && std::abs (g400) < 0.5f, "paso alto 48 dB/oct en 100 Hz: 25 Hz a " + String (g25, 1) + " dB, 400 Hz a " + String (g400, 2) + " dB");
    }

    // linear phase: latency, symmetric impulse response, same magnitude
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("phase", 1);
        p.setRealValue (B (3, "freq"), 1000); p.setRealValue (B (3, "gain"), 6); p.setRealValue (B (3, "q"), 1);
        p.prepareToPlay (sr, 256);
        Thread::sleep (400);
        AudioBuffer<float> warm (2, 256); warm.clear();
        for (int k = 0; k < 20; ++k) { MidiBuffer m; p.processBlock (warm, m); }   // let the new IR crossfade in
        Thread::sleep (200);
        for (int k = 0; k < 40; ++k) { MidiBuffer m; p.processBlock (warm, m); }
        check (p.getLatencySamples() == 2048, "fase lineal informa 2048 muestras de latencia");
        AudioBuffer<float> imp (2, 8192); imp.clear(); imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
        auto o = tu::render (p, imp, 256, nullptr);
        int peakAt = 0; float best = 0;
        for (int i = 0; i < 8192; ++i) if (std::abs (o.getSample (0, i)) > best) { best = std::abs (o.getSample (0, i)); peakAt = i; }
        float asym = 0;
        for (int k = 1; k < 1500; ++k) asym = jmax (asym, std::abs (o.getSample (0, peakAt + k) - o.getSample (0, peakAt - k)));
        check (peakAt == 2048 && asym < 0.01f * best, "respuesta al impulso simétrica alrededor de 2048 (pico en "_u + String (peakAt) + ")");
        const float g = gainAt (p, 1000.0);
        check (std::abs (g - 6.0f) < 0.5f, "fase lineal: misma campana de +6 dB (" + String (g, 2) + " dB)");
    }

    // dynamic band: only reacts when that zone is loud
    auto dyn = [&] (float level)
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue (B (5, "freq"), 7000); p.setRealValue (B (5, "dyn"), 1); p.setRealValue (B (5, "thr"), -30); p.setRealValue (B (5, "range"), 9);
        p.setRealValue (B (5, "q"), 2);
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, 48000);
        tu::fillSine (in, 7000.0, sr, level, 0.0);
        auto o = tu::render (p, in, 256, nullptr);
        return racoon::dsp::gainToDb (tu::tone (o, 0, 24000, 16384, 7000.0, sr) / level);
    };
    const float loud = dyn (0.5f), quiet = dyn (0.003f);
    check (loud < -5.0f && quiet > -1.0f, "banda dinámica: fuerte baja "_u + String (loud, 1) + " dB, suave " + String (quiet, 1) + " dB");

    // fast automation stays stable
    {
        Processor p;
        p.prepareToPlay (sr, 64);
        AudioBuffer<float> io (2, 64);
        bool ok = true;
        for (int k = 0; k < 3000; ++k)
        {
            p.setRealValue (B (3, "freq"), 30.0f + 15000.0f * (0.5f + 0.5f * std::sin (k * 0.05f)));
            p.setRealValue (B (3, "gain"), 24.0f * std::sin (k * 0.031f));
            p.setRealValue (B (3, "q"), 0.2f + 20.0f * (0.5f + 0.5f * std::sin (k * 0.017f)));
            tu::fillNoise (io, 0.3f, k);
            MidiBuffer m; p.processBlock (io, m);
            ok = ok && tu::allFinite (io) && io.getMagnitude (0, 64) < 20.0f;
        }
        check (ok, "automatización rápida de frecuencia, ganancia y Q: estable"_u);
    }
}
static RegisterTest regPorcupine ("eq-ocho", testPorcupine);

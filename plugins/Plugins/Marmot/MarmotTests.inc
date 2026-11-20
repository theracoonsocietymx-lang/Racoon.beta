// Marmot checks (included by Tests/FamilyTest.cpp)
#include "MarmotProcessor.h"

static void testMarmot (const juce::File& out)
{
    using namespace juce;
    using racoon::marmot::Processor;
    namespace mm = racoon::marmot;
    commonChecks ("Marmot", [] { return new Processor(); }, out);
    std::cout << "sintetizador" << std::endl;
    const double sr = 48000.0;

    // a plain patch: osc 1 only, no filter colour, no effects, no modulation
    auto plain = [] (Processor& p)
    {
        p.setRealValue ("osc1_wave", mm::waveSaw); p.setRealValue ("osc1_level", 100); p.setRealValue ("osc2_level", 0);
        p.setRealValue ("sub", 0); p.setRealValue ("noise", 0); p.setRealValue ("unison", 1); p.setRealValue ("drift", 0);
        p.setRealValue ("ftype", 0); p.setRealValue ("cutoff", 20000); p.setRealValue ("reso", 0); p.setRealValue ("drive", 0);
        p.setRealValue ("keytrack", 0); p.setRealValue ("fenv", 0); p.setRealValue ("velflt", 0); p.setRealValue ("velamp", 0);
        for (int k = 0; k < mm::numSlots; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0);
        p.setRealValue ("sat", 0); p.setRealValue ("chorus", 0); p.setRealValue ("eq_low", 0); p.setRealValue ("eq_high", 0);
        p.setRealValue ("dly_mix", 0); p.setRealValue ("rev_mix", 0);
        p.setRealValue ("adsr_a", 1); p.setRealValue ("adsr_d", 100); p.setRealValue ("adsr_s", 100); p.setRealValue ("adsr_r", 100);
    };
    auto run = [&] (std::function<void (Processor&)> setup, MidiBuffer& midi, int len, Processor* keep = nullptr)
    {
        std::unique_ptr<Processor> own;
        Processor* p = keep;
        if (p == nullptr) { own = std::make_unique<Processor>(); p = own.get(); }
        p->prepareToPlay (sr, 256);
        setup (*p);
        AudioBuffer<float> in (2, len); in.clear();
        return tu::render (*p, in, 256, &midi);
    };
    auto peakFreq = [&] (const AudioBuffer<float>& b, int start, int order = 15)
    {
        auto s = tu::spectrum (b, 0, start, order);
        size_t best = 1;
        for (size_t i = 1; i < s.size(); ++i) if (s[i] > s[best]) best = i;
        return (double) best * sr / (double) (1 << order);
    };
    // energy that is not a harmonic of f0, against all the energy (dB)
    auto inharm = [&] (const AudioBuffer<float>& b, double f0)
    {
        auto sp = tu::spectrum (b, 0, (int) (0.3 * sr), 15);
        const double bin = sr / 32768.0;
        double all = 0.0, bad = 0.0;
        for (size_t i = 2; i < sp.size(); ++i)
        {
            const double hz = (double) i * bin;
            const double nearest = std::round (hz / f0) * f0;
            all += sp[i];
            if (std::abs (hz - nearest) > 4.0 * bin) bad += sp[i];
        }
        return 10.0 * std::log10 (bad / jmax (1e-30, all) + 1e-30);
    };
    auto bandEnergy = [&] (const AudioBuffer<float>& b, double lo, double hi)
    {
        auto sp = tu::spectrum (b, 0, (int) (0.3 * sr), 15);
        double e = 0.0;
        for (size_t i = 1; i < sp.size(); ++i) { const double hz = (double) i * sr / 32768.0; if (hz >= lo && hz < hi) e += sp[i]; }
        return e;
    };

    // tuning: A4 plays 440 Hz
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 69, (uint8) 100), 0);
        auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("osc1_wave", mm::waveTri); }, m, (int) sr * 2);
        const double f = peakFreq (o, (int) sr);
        check (std::abs (f - 440.0) < 2.0, "La 4 suena en " + String (f, 1) + " Hz");
    }

    // band limited: saw, pulse and triangle at C7 (2093 Hz) keep what is not a harmonic far below
    {
        String txt; double worst = -300.0;
        for (int wave : { mm::waveSaw, mm::wavePulse, mm::waveTri })
        {
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 96, (uint8) 110), 0);
            auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("osc1_wave", (float) wave); p.setRealValue ("osc1_pw", 30); }, m, (int) (sr * 1.2));
            const double a = inharm (o, 2093.0);
            worst = jmax (worst, a);
            txt << Processor::waveNames()[wave] << " " << String (a, 1) << " dB; ";
        }
        check (worst < -40.0, "aliasing en Do 7 bajo -40 dB: " + txt);
    }

    // hard sync: osc 2 restarted by osc 1. The result repeats at osc 1's frequency, and the smoothed reset keeps
    // the folding low even with osc 2 a fifth and an octave up
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 84, (uint8) 110), 0);
        auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("osc1_level", 0); p.setRealValue ("osc2_level", 100); p.setRealValue ("sync", 1);
                                           p.setRealValue ("osc2_wave", mm::waveSaw); p.setRealValue ("osc2_semi", 7); p.setRealValue ("osc2_oct", 1);
                                           p.setRealValue ("osc2_fine", 0); }, m, (int) (sr * 1.2));
        const double f0 = 440.0 * std::pow (2.0, (84 - 69) / 12.0);
        const double a = inharm (o, f0);
        check (a < -30.0 && tu::allFinite (o), "sync en Do 6 con el oscilador 2 una quinta y una octava arriba: todo es armónico de la nota (fuera de los armónicos "_u + String (a, 1) + " dB)");
    }

    // pulse width: a square has almost no even harmonics, a 25 % pulse has a strong second one
    {
        auto second = [&] (float pw)
        {
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 57, (uint8) 100), 0);
            auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("osc1_wave", mm::wavePulse); p.setRealValue ("osc1_pw", pw); }, m, (int) sr);
            const double f0 = 220.0;
            return 10.0 * std::log10 (bandEnergy (o, 2 * f0 - 5, 2 * f0 + 5) / jmax (1e-30, bandEnergy (o, f0 - 5, f0 + 5)));
        };
        const double sq = second (50.0f), narrow = second (25.0f);
        check (sq < -30.0 && narrow > -12.0, "ancho de pulso: con 50 % el segundo armónico queda en "_u + String (sq, 1) + " dB, con 25 % en " + String (narrow, 1) + " dB");
    }

    // sub: one octave under osc 1
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 57, (uint8) 100), 0);
        auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("osc1_level", 0); p.setRealValue ("sub", 100); }, m, (int) sr);
        const double f = peakFreq (o, (int) (sr * 0.3));
        check (std::abs (f - 110.0) < 2.0, "el sub suena una octava abajo (La 3 da " + String (f, 1) + " Hz)");
    }

    // the low pass: a saw at 110 Hz with the cut at 400 Hz keeps little above 2 kHz
    {
        auto hiShare = [&] (float cut)
        {
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 45, (uint8) 100), 0);
            auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("ftype", 1); p.setRealValue ("cutoff", cut); }, m, (int) sr);
            return bandEnergy (o, 2000.0, 20000.0) / jmax (1e-30, bandEnergy (o, 20.0, 20000.0));
        };
        const double open = hiShare (20000.0f), closed = hiShare (400.0f);
        check (10.0 * std::log10 (closed / open) < -30.0, "filtro LP 24 en 400 Hz: los agudos bajan " + String (10.0 * std::log10 (closed / open), 1) + " dB");
    }

    // arpeggiator: a held chord comes out note by note, in order, at the tempo (1/16 at 120 BPM = 125 ms a step)
    {
        Processor p;
        MidiBuffer m;
        for (int n : { 52, 48, 55 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
        auto o = run ([&] (Processor& q) { plain (q); q.setRealValue ("osc1_wave", mm::waveTri); q.setRealValue ("arp_on", 1);
                                           q.setRealValue ("arp_mode", mm::arpUp); q.setRealValue ("arp_div", 3); q.setRealValue ("arp_gate", 80);
                                           q.setRealValue ("adsr_r", 5); }, m, (int) (sr * 1.2), &p);
        const double step = 0.125 * 120.0 / p.bpm();
        String got; bool ok = true;
        const int expect[8] { 48, 52, 55, 48, 52, 55, 48, 52 };
        for (int k = 0; k < 8; ++k)
        {
            const double f = peakFreq (o, (int) ((k * step + 0.012) * sr), 12);
            const int note = (int) std::round (69.0 + 12.0 * std::log2 (jmax (1.0, f) / 440.0));
            got << note << " ";
            ok = ok && note == expect[k];
        }
        check (ok && tu::allFinite (o), "arpegiador hacia arriba con Do Mi Sol: " + got);
    }

    // arpeggiator with hold: it keeps playing after the keys go up; switching it off stops it
    {
        Processor p;
        MidiBuffer m;
        for (int n : { 48, 55 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
        for (int n : { 48, 55 }) m.addEvent (MidiMessage::noteOff (1, n), (int) (0.2 * sr));
        auto o = run ([&] (Processor& q) { plain (q); q.setRealValue ("arp_on", 1); q.setRealValue ("arp_hold", 1); q.setRealValue ("adsr_r", 5); },
                      m, (int) sr, &p);
        const bool goingOn = tu::rms (o, (int) (0.8 * sr), (int) (0.15 * sr)) > 0.01f;
        p.setRealValue ("arp_on", 0);
        MidiBuffer none;
        AudioBuffer<float> in (2, (int) (sr * 0.5)); in.clear();
        auto after = tu::render (p, in, 256, &none);
        check (goingOn && tu::rms (after, (int) (0.3 * sr), (int) (0.15 * sr)) < 1.0e-4f, "Sostener deja sonando el arpegio al soltar; apagar el arpegiador lo calla");
    }

    // mono with glide: the second note does not take another voice and slides
    {
        Processor p;
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 57, (uint8) 100), 0);
        m.addEvent (MidiMessage::noteOn (1, 69, (uint8) 100), (int) (0.5 * sr));
        auto o = run ([&] (Processor& q) { plain (q); q.setRealValue ("voices", 1); q.setRealValue ("glide", 300); q.setRealValue ("osc1_wave", mm::waveTri); },
                      m, (int) (sr * 1.5), &p);
        const double mid = peakFreq (o, (int) (0.58 * sr), 12), end = peakFreq (o, (int) (1.2 * sr), 14);
        check (p.activeVoices.load() == 1 && mid > 240.0 && mid < 420.0 && std::abs (end - 440.0) < 6.0,
               "monofónico con glide: una sola voz, a mitad del deslizamiento " + String (mid, 0) + " Hz y llega a " + String (end, 0) + " Hz");
    }

    // 16 voices with 6 unison, sync and cross mod: stable, and how much of a core it takes
    {
        Processor p;
        MidiBuffer m;
        for (int k = 0; k < 16; ++k) m.addEvent (MidiMessage::noteOn (1, 36 + k * 3, (uint8) 90), k * 64);
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto o = run ([] (Processor& q) { q.setRealValue ("voices", 16); q.setRealValue ("unison", 6); q.setRealValue ("sync", 1); q.setRealValue ("xmod", 40);
                                          q.setRealValue ("rev_mix", 40); q.setRealValue ("dly_mix", 40); }, m, (int) sr * 4, &p);
        const double secs = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
        check (tu::allFinite (o) && p.activeVoices.load() == 16 && tu::peak (o, 0, o.getNumSamples()) < 4.0f,
               "16 voces con unison 6, sync y modulación cruzada: estables (pico "_u + String (tu::peak (o, 0, o.getNumSamples()), 2) + ", "
               + String (roundToInt (secs / 4.0 * 100.0)) + " % de un núcleo)"_u);
    }

    // deterministic: the same notes give the same sound
    {
        auto setup = [] (Processor& p) { p.setRealValue ("noise", 20); p.setRealValue ("drift", 60); p.setRealValue ("m1_src", mm::srcRandom); p.setRealValue ("m1_dst", mm::dstPan); p.setRealValue ("m1_amt", 80); };
        MidiBuffer m1, m2;
        for (auto* m : { &m1, &m2 }) { m->addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0); m->addEvent (MidiMessage::noteOn (1, 67, (uint8) 80), 9000); }
        auto a = run (setup, m1, (int) sr * 2), b = run (setup, m2, (int) sr * 2);
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) diff = jmax (diff, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        check (diff == 0.0f, "render determinista (diferencia " + String (diff) + ")");
    }

    // every factory preset sounds (a chord for the arpeggios), without clipping, and stays low: little energy above 5 kHz
    {
        Processor probe;
        const auto presets = probe.factoryPresets();
        String quiet, loud; int bright = 0; double worstHi = 0.0; String worstName;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            Processor p;
            p.prepareToPlay (sr, 256);
            p.applyFactoryPreset (i);
            MidiBuffer m;
            for (int n : { 45, 48, 52 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
            for (int n : { 45, 48, 52 }) m.addEvent (MidiMessage::noteOff (1, n), (int) (sr * 1.5));
            AudioBuffer<float> in (2, (int) (sr * 2)); in.clear();
            auto o = tu::render (p, in, 256, &m);
            const float r = tu::rms (o, 0, (int) (sr * 1.5)), pk = tu::peak (o, 0, o.getNumSamples());
            if (r < 0.005f) quiet << presets[(size_t) i].name << " ";
            if (pk > 1.5f || ! tu::allFinite (o)) loud << presets[(size_t) i].name << " ";
            auto sp = tu::spectrum (o, 0, (int) (sr * 0.1), 15);
            double hi = 0.0, all = 0.0;
            for (size_t k = 1; k < sp.size(); ++k) { all += sp[k]; if ((double) k * sr / 32768.0 > 5000.0) hi += sp[k]; }
            const double share = hi / jmax (1e-30, all);
            if (share > 0.05) ++bright;
            if (share > worstHi) { worstHi = share; worstName = presets[(size_t) i].name; }
        }
        check (quiet.isEmpty() && loud.isEmpty(), String ((int) presets.size()) + " presets suenan con un acorde, sin saturar" + (quiet.isEmpty() ? String() : ", callados: " + quiet) + (loud.isEmpty() ? String() : ", fuertes: " + loud));
        check (bright == 0, "los presets son graves: el más brillante (" + worstName + ") tiene " + String (worstHi * 100.0, 1) + " % de su energía arriba de 5 kHz"_u);
    }
}

static RegisterTest regMarmot ("Marmot", testMarmot);

// Opossum checks (included by Tests/FamilyTest.cpp)
#include "OpossumProcessor.h"

static void testOpossum (const juce::File& out)
{
    using namespace juce;
    using racoon::opossum::Processor;
    namespace op = racoon::opossum;
    commonChecks ("Opossum", [] { return new Processor(); }, out);
    std::cout << "FM" << std::endl;
    const double sr = 48000.0;

    // a bare patch: no filter, no effects, no modulation slots; operators all at zero
    auto bare = [] (Processor& p)
    {
        p.setRealValue ("ftype", 0); p.setRealValue ("drive", 0); p.setRealValue ("dist_type", 0); p.setRealValue ("mono_hz", 20);
        p.setRealValue ("eco_mix", 0); p.setRealValue ("sala_mix", 0); p.setRealValue ("wobble", 0); p.setRealValue ("metal", 0);
        p.setRealValue ("mordida", 100); p.setRealValue ("retro", 100); p.setRealValue ("penv_amt", 0);
        for (int k = 0; k < op::numSlots; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0);
        for (int o = 0; o < op::numOps; ++o)
        {
            p.setRealValue (Processor::opId (o, "level"), 0); p.setRealValue (Processor::opId (o, "fb"), 0); p.setRealValue (Processor::opId (o, "fine"), 0);
            p.setRealValue (Processor::opId (o, "ratio"), 1); p.setRealValue (Processor::opId (o, "s"), 100); p.setRealValue (Processor::opId (o, "wave"), 0);
        }
        p.setRealValue ("adsr_a", 1); p.setRealValue ("adsr_s", 100);
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
    auto band = [&] (const std::vector<float>& sp, double lo, double hi)
    {
        double e = 0.0;
        for (size_t i = 1; i < sp.size(); ++i) { const double hz = (double) i * sr / 32768.0; if (hz >= lo && hz < hi) e += sp[i]; }
        return e;
    };
    auto note = [] (int n, int vel = 127) { MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, n, (uint8) vel), 0); return m; };

    // tuning: a lone sine carrier on A2 plays 110 Hz
    {
        auto m = note (45);
        auto o = run ([&] (Processor& p) { bare (p); p.setRealValue ("algo", 9); p.setRealValue (Processor::opId (0, "level"), 100); }, m, (int) sr);
        auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
        size_t best = 1; for (size_t i = 1; i < sp.size(); ++i) if (sp[i] > sp[best]) best = i;
        const double f = (double) best * sr / 32768.0;
        check (std::abs (f - 110.0) < 1.0, "un operador solo en La 2 suena en " + String (f, 1) + " Hz");
    }

    // phase modulation by the book: index 1 (Nivel 35.36) gives the side lines J1(1)/J0(1) = 0.575 (-4.8 dB) and
    // J2(1)/J0(1) = 0.150 (-16.5 dB). The modulator at 5 times keeps the lines apart: f, 4f and 6f, 9f and 11f
    {
        auto m = note (45);
        auto o = run ([&] (Processor& p) { bare (p); p.setRealValue ("algo", 1); p.setRealValue (Processor::opId (0, "level"), 100);
                                           p.setRealValue (Processor::opId (1, "level"), 35.355f); p.setRealValue (Processor::opId (1, "ratio"), 5); }, m, (int) sr);
        auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
        const double f = 110.0;
        const double c = band (sp, f - 4, f + 4), s1 = band (sp, 6 * f - 4, 6 * f + 4), s2 = band (sp, 11 * f - 4, 11 * f + 4);
        const double d1 = 10.0 * std::log10 (s1 / c), d2 = 10.0 * std::log10 (s2 / c);
        check (std::abs (d1 + 4.8) < 0.6 && std::abs (d2 + 16.5) < 1.0, "índice 1: primeras bandas laterales a "_u + String (d1, 2) + " dB (Bessel -4.8), segundas a " + String (d2, 2) + " dB (Bessel -16.5)");
    }

    // Mordida: more index, more highs
    {
        auto bright = [&] (float mord)
        {
            auto m = note (40);
            auto o = run ([&] (Processor& p) { bare (p); p.setRealValue ("algo", 1); p.setRealValue ("mordida", mord); p.setRealValue (Processor::opId (0, "level"), 100);
                                               p.setRealValue (Processor::opId (1, "level"), 60); p.setRealValue (Processor::opId (2, "level"), 40); }, m, (int) sr);
            auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
            return band (sp, 1000, 20000) / band (sp, 20, 20000);
        };
        const double lo = bright (20.0f), hi = bright (180.0f);
        check (10.0 * std::log10 (hi / lo) > 15.0, "Mordida al 180 % tiene " + String (10.0 * std::log10 (hi / lo), 1) + " dB más arriba de 1 kHz que al 20 %"_u);
    }

    // Metal: the modulators go inharmonic, the energy between the harmonics grows
    {
        auto inharm = [&] (float metal)
        {
            auto m = note (45);
            auto o = run ([&] (Processor& p) { bare (p); p.setRealValue ("algo", 1); p.setRealValue ("metal", metal); p.setRealValue (Processor::opId (0, "level"), 100);
                                               p.setRealValue (Processor::opId (1, "level"), 55); p.setRealValue (Processor::opId (2, "level"), 40); }, m, (int) sr);
            auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
            double all = 0, bad = 0; const double bin = sr / 32768.0;
            for (size_t i = 2; i < sp.size(); ++i) { const double hz = (double) i * bin; all += sp[i]; if (std::abs (hz - std::round (hz / 110.0) * 110.0) > 4 * bin) bad += sp[i]; }
            return 10.0 * std::log10 (bad / all + 1e-30);
        };
        const double clean = inharm (0.0f), iron = inharm (100.0f);
        check (clean < -40.0 && iron > -15.0, "Metal: fuera de los armónicos de "_u + String (clean, 1) + " dB (en 0) a " + String (iron, 1) + " dB (en 100)");
    }

    // aliasing: the default patch on C5 keeps what is not a harmonic low (the voice runs at twice the sample rate)
    {
        auto m = note (72);
        auto o = run ([&] (Processor& p) { p.setRealValue ("ftype", 0); p.setRealValue ("sala_mix", 0); for (int k = 0; k < op::numSlots; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0); }, m, (int) sr);
        auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
        const double f0 = 440.0 * std::pow (2.0, 3.0 / 12.0), bin = sr / 32768.0;
        double all = 0, bad = 0;
        for (size_t i = 2; i < sp.size(); ++i) { const double hz = (double) i * bin; all += sp[i]; if (std::abs (hz - std::round (hz / (f0 * 0.5)) * f0 * 0.5) > 4 * bin) bad += sp[i]; }
        const double db = 10.0 * std::log10 (bad / all + 1e-30);
        check (db < -35.0, "aliasing del patch inicial en Do 5: fuera de los armónicos " + String (db, 1) + " dB");
    }

    // pitch envelope: an octave up at the attack, back in place after
    {
        auto m = note (45);
        auto o = run ([&] (Processor& p) { bare (p); p.setRealValue ("algo", 9); p.setRealValue (Processor::opId (0, "level"), 100);
                                           p.setRealValue ("penv_amt", 12); p.setRealValue ("penv_time", 60); }, m, (int) sr);
        auto zc = [&] (int a, int b)
        {
            const float* x = o.getReadPointer (0); int cnt = 0; double first = -1, last = -1;
            for (int i = a + 1; i < b; ++i) if (x[i - 1] < 0.0f && x[i] >= 0.0f) { const double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; else ++cnt; last = t; }
            return cnt > 0 ? cnt * sr / (last - first) : 0.0;
        };
        const double early = zc (200, 1400), late = zc ((int) (sr * 0.6), (int) (sr * 0.9));
        check (early > 170.0 && std::abs (late - 110.0) < 1.0, "caída de afinación: al ataque " + String (early, 0) + " Hz, después " + String (late, 1) + " Hz"_u);
    }

    // wobble: LFO 1 at 1/8 (120 BPM) opens and closes the sound four times a second
    {
        auto m = note (33);
        auto o = run ([&] (Processor& p) { p.setRealValue ("wobble", 100); p.setRealValue ("sala_mix", 0); }, m, (int) sr);
        auto hiAt = [&] (double t)
        {
            AudioBuffer<float> w (1, 1024); w.copyFrom (0, 0, o, 0, (int) (t * sr) - 512, 1024);
            double hi = 0, all = 0;
            for (int i = 1; i < 1024; ++i) { const float d = w.getSample (0, i) - w.getSample (0, i - 1); hi += d * d; all += w.getSample (0, i) * w.getSample (0, i); }
            return hi / (all + 1e-20);
        };
        const double open = hiAt (0.5 + 0.0625), closed = hiAt (0.5 + 0.1875);
        check (10.0 * std::log10 (open / closed) > 8.0, "Wobble: abierto " + String (10.0 * std::log10 (open / closed), 1) + " dB más brillante que cerrado, al tempo");
    }

    // the vowel filter: I is brighter than U
    {
        auto cen = [&] (float vowel)
        {
            auto m = note (40);
            auto o = run ([&] (Processor& p) { p.setRealValue ("ftype", 5); p.setRealValue ("vowel", vowel); p.setRealValue ("cutoff", 1000); p.setRealValue ("sala_mix", 0); p.setRealValue ("mordida", 180); }, m, (int) sr);
            auto sp = tu::spectrum (o, 0, (int) (sr * 0.3), 15);
            return band (sp, 1800, 3300) / band (sp, 20, 20000);
        };
        const double u = cen (0.0f), iv = cen (100.0f);
        check (10.0 * std::log10 (iv / u) > 6.0, "Formante: la I tiene " + String (10.0 * std::log10 (iv / u), 1) + " dB más entre 1.8 y 3.3 kHz que la U"_u);
    }

    // the lows stay mono with a wide unison
    {
        auto m = note (33);
        auto o = run ([] (Processor& p) { p.setRealValue ("unison", 6); p.setRealValue ("detune", 40); p.setRealValue ("spread", 100); p.setRealValue ("sala_mix", 0); }, m, (int) sr);
        AudioBuffer<float> side (1, o.getNumSamples()), mid (1, o.getNumSamples());
        for (int i = 0; i < o.getNumSamples(); ++i) { side.setSample (0, i, 0.5f * (o.getSample (0, i) - o.getSample (1, i))); mid.setSample (0, i, 0.5f * (o.getSample (0, i) + o.getSample (1, i))); }
        auto ss = tu::spectrum (side, 0, (int) (sr * 0.3), 15), ms = tu::spectrum (mid, 0, (int) (sr * 0.3), 15);
        const double db = 10.0 * std::log10 (band (ss, 20, 80) / (band (ms, 20, 80) + 1e-30) + 1e-30);
        check (db < -15.0, "graves en mono con unison 6 abierto: lo estéreo abajo de 80 Hz queda " + String (db, 1) + " dB bajo lo central"_u);
    }

    // mono legato with glide
    {
        Processor p;
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 33, (uint8) 100), 0);
        m.addEvent (MidiMessage::noteOn (1, 45, (uint8) 100), (int) (0.5 * sr));
        run ([] (Processor& q) { q.setRealValue ("glide", 200); }, m, (int) sr, &p);
        check (p.activeVoices.load() == 1, "monofónico con legato: la segunda nota liga en la misma voz"_u);
    }

    // 16 voices with unison 8 and destruction: stable, and the cost
    {
        Processor p;
        MidiBuffer m;
        for (int k = 0; k < 16; ++k) m.addEvent (MidiMessage::noteOn (1, 30 + k * 3, (uint8) 100), k * 64);
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto o = run ([] (Processor& q) { q.setRealValue ("voices", 16); q.setRealValue ("unison", 8); q.setRealValue ("dist_type", 2); q.setRealValue ("retro", 200);
                                          q.setRealValue ("eco_mix", 40); }, m, (int) sr * 3, &p);
        const double secs = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
        check (tu::allFinite (o) && p.activeVoices.load() == 16 && tu::peak (o, 0, o.getNumSamples()) < 4.0f,
               "16 voces con unison 8, retro al 200 % y plegado: estables (pico "_u + String (tu::peak (o, 0, o.getNumSamples()), 2) + ", " + String (roundToInt (secs / 3.0 * 100.0)) + " % de un núcleo)"_u);
    }

    // deterministic
    {
        auto setup = [] (Processor& p) { p.setRealValue ("unison", 3); p.setRealValue ("m4_src", op::srcRandom); p.setRealValue ("m4_dst", op::dstPan); p.setRealValue ("m4_amt", 80); };
        MidiBuffer m1 = note (40, 100), m2 = note (40, 100);
        auto a = run (setup, m1, (int) sr), b = run (setup, m2, (int) sr);
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) diff = jmax (diff, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        check (diff == 0.0f, "render determinista (diferencia " + String (diff) + ")");
    }

    // every preset sounds on a low note, without clipping, and stays dark
    {
        Processor probe;
        const auto presets = probe.factoryPresets();
        String quiet, loud; double worstHi = 0.0; String worstName;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            Processor p;
            p.prepareToPlay (sr, 256);
            p.applyFactoryPreset (i);
            MidiBuffer m;
            m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 100), 0);
            m.addEvent (MidiMessage::noteOff (1, 36), (int) (sr * 1.5));
            AudioBuffer<float> in (2, (int) (sr * 2.5)); in.clear();
            auto o = tu::render (p, in, 256, &m);
            const float r = tu::rms (o, 0, (int) (sr * 1.5)), pk = tu::peak (o, 0, o.getNumSamples());
            if (r < 0.004f) quiet << presets[(size_t) i].name << " ";
            if (pk > 1.5f || ! tu::allFinite (o)) loud << presets[(size_t) i].name << " ";
            auto sp = tu::spectrum (o, 0, (int) (sr * 0.2), 15);
            const double share = band (sp, 5000, 24000) / jmax (1e-30, band (sp, 1, 24000));
            if (share > worstHi) { worstHi = share; worstName = presets[(size_t) i].name; }
        }
        check (quiet.isEmpty() && loud.isEmpty(), String ((int) presets.size()) + " presets suenan sin saturar" + (quiet.isEmpty() ? String() : ", callados: " + quiet) + (loud.isEmpty() ? String() : ", fuertes: " + loud));
        check (worstHi < 0.05, "los presets son oscuros: el más brillante (" + worstName + ") tiene " + String (worstHi * 100.0, 1) + " % de su energía arriba de 5 kHz"_u);
    }
}

static RegisterTest regOpossum ("Opossum", testOpossum);

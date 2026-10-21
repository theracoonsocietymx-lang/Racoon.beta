// Dormouse checks (included by Tests/FamilyTest.cpp)
#include "DormouseProcessor.h"

static void testDormouse (const juce::File& out)
{
    using namespace juce;
    using racoon::dormouse::Processor;
    commonChecks ("Dormouse", [] { return new Processor(); }, out);
    std::cout << "sintetizador" << std::endl;
    const double sr = 48000.0;

    // a plain patch: one sine voice, no filter colour, no chorus, no modulation
    auto plain = [] (Processor& p)
    {
        p.setRealValue ("osc1_bank", 0); p.setRealValue ("osc1_pos", 0); p.setRealValue ("osc1_level", 100);
        p.setRealValue ("osc2_level", 0); p.setRealValue ("unison", 1); p.setRealValue ("chorus", 0);
        p.setRealValue ("ftype", 0); p.setRealValue ("cutoff", 20000); p.setRealValue ("reso", 0);
        p.setRealValue ("keytrack", 0); p.setRealValue ("fenv", 0); p.setRealValue ("velflt", 0); p.setRealValue ("velamp", 0);
        for (int k = 0; k < 8; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0);
        p.setRealValue ("env1_a", 0.01f); p.setRealValue ("env1_d", 0.1f); p.setRealValue ("env1_s", 100); p.setRealValue ("env1_r", 0.5f);
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
    auto peakFreq = [&] (const AudioBuffer<float>& b, int start)
    {
        auto s = tu::spectrum (b, 0, start, 15);
        size_t best = 1;
        for (size_t i = 1; i < s.size(); ++i) if (s[i] > s[best]) best = i;
        return (double) best * sr / 32768.0;
    };

    // tuning: A4 plays 440 Hz
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 69, (uint8) 100), 0);
        auto o = run (plain, m, (int) sr * 2);
        const double f = peakFreq (o, (int) sr);
        check (std::abs (f - 440.0) < 2.0, "La 4 suena en " + String (f, 1) + " Hz");
    }

    // 5 minutes of sustain: same level and same pitch at the start and at the end
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 57, (uint8) 100), 0);
        auto o = run (plain, m, (int) sr * 300);
        const float a = tu::rms (o, (int) sr * 2, (int) sr), b = tu::rms (o, (int) sr * 298, (int) sr);
        const double fa = peakFreq (o, (int) sr * 2), fb = peakFreq (o, (int) sr * 298 - 40000);
        check (tu::allFinite (o) && std::abs (Decibels::gainToDecibels (b / a)) < 0.1f && std::abs (fa - fb) < 0.01,
               "sustain de 5 minutos: nivel " + String (Decibels::gainToDecibels (b / a), 3) + " dB, afinación "_u + String (fa, 2) + " / " + String (fb, 2) + " Hz");
    }

    // note-off: the release follows its time and the voice is freed
    {
        Processor p;
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        m.addEvent (MidiMessage::noteOff (1, 60), (int) sr);
        auto o = run (plain, m, (int) (sr * 2.5), &p);
        const float held = tu::rms (o, (int) (sr * 0.5), (int) (sr * 0.2));
        const float mid = tu::rms (o, (int) (sr * 1.2), (int) (sr * 0.05));
        const float after = tu::rms (o, (int) (sr * 1.6), (int) (sr * 0.2));
        const float midDb = Decibels::gainToDecibels (mid / held);
        check (std::abs (midDb + 60.0f * 0.225f / 0.5f) < 3.0f && after < held * 0.001f && p.activeVoices.load() == 0,
               "note-off con relajación de 0.5 s: a 0.2 s "_u + String (Decibels::gainToDecibels (mid / held), 1) + " dB, a 0.6 s "
               + String (Decibels::gainToDecibels (after / held + 1.0e-9f), 1) + " dB, voces libres");
    }

    // stealing voices does not click: compare with the same notes and no stealing
    {
        auto notes = [] (MidiBuffer& m)
        {
            for (int k = 0; k < 16; ++k) m.addEvent (MidiMessage::noteOn (1, 60 + (k * 5) % 13, (uint8) 100), 100 + k * 4800);
        };
        auto setup = [&] (int voicesCount) { return [&plain, voicesCount] (Processor& p) { plain (p); p.setRealValue ("voices", (float) voicesCount); p.setRealValue ("env1_r", 3.0f); }; };
        MidiBuffer m1, m2; notes (m1); notes (m2);
        auto stolen = run (setup (3), m1, (int) sr * 2);
        auto free = run (setup (32), m2, (int) sr * 2);
        const float js = tu::maxJump (stolen), jf = tu::maxJump (free);
        check (js <= jf * 1.05f, "robar voces sin clic (salto " + String (js, 4) + " con 3 voces, " + String (jf, 4) + " sin robar)");
    }

    // 32 voices with 8 unison each: stable, and how much of real time it takes
    {
        Processor p;
        MidiBuffer m;
        for (int k = 0; k < 32; ++k) m.addEvent (MidiMessage::noteOn (1, 36 + k * 2, (uint8) 90), k * 64);
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto o = run ([] (Processor& q) { q.setRealValue ("voices", 32); q.setRealValue ("unison", 8); q.setRealValue ("texture", 30); }, m, (int) sr * 5, &p);
        const double secs = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
        check (tu::allFinite (o) && p.activeVoices.load() == 32 && tu::peak (o, 0, o.getNumSamples()) < 4.0f,
               "32 voces con unison 8: estables (pico " + String (tu::peak (o, 0, o.getNumSamples()), 2) + ", "
               + String (roundToInt (secs / 5.0 * 100.0)) + " % de un núcleo)"_u);
    }

    // band-limited: a bright wavetable at a high note has no energy between its harmonics
    for (int bank : { 0, 6 })
    {
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 96, (uint8) 100), 0);
        auto o = run ([&plain, bank] (Processor& p) { plain (p); p.setRealValue ("osc1_bank", (float) bank); p.setRealValue ("osc1_pos", bank == 0 ? 67.0f : 0.0f); },
                      m, (int) sr * 2);
        auto s = tu::spectrum (o, 0, (int) sr, 15);
        const double f0 = 440.0 * std::pow (2.0, (96 - 69) / 12.0);
        double harm = 0, other = 0;
        for (size_t i = 2; i < s.size(); ++i)
        {
            const double f = (double) i * sr / 32768.0;
            const double nearest = std::round (f / f0) * f0;
            (std::abs (f - nearest) < 25.0 ? harm : other) += s[i];
        }
        const double db = 10.0 * std::log10 (other / harm + 1e-30);
        const double limit = bank == 0 ? -60.0 : -30.0;
        check (db < limit, (bank == 0 ? "tabla Analógica"_u : String ("VA Sierra")) + " en C7: energía fuera de los armónicos "_u + String (db, 1) + " dB");
    }

    // deterministic: the same notes give the same sound (random sources are seeded per note)
    {
        auto setup = [] (Processor& p) { p.setRealValue ("texture", 50); p.setRealValue ("m4_src", 12); p.setRealValue ("m4_dst", 8); p.setRealValue ("m4_amt", 80); };
        MidiBuffer m1, m2;
        for (auto* m : { &m1, &m2 }) { m->addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0); m->addEvent (MidiMessage::noteOn (1, 67, (uint8) 80), 9000); }
        auto a = run (setup, m1, (int) sr * 2), b = run (setup, m2, (int) sr * 2);
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) diff = jmax (diff, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        check (diff == 0.0f, "render determinista (diferencia " + String (diff) + ")");
    }

    // sustain pedal and all-notes-off
    {
        Processor p;
        MidiBuffer m;
        m.addEvent (MidiMessage::controllerEvent (1, 64, 127), 0);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 10);
        m.addEvent (MidiMessage::noteOff (1, 60), 4800);
        auto o = run (plain, m, (int) sr, &p);
        const bool held = p.activeVoices.load() == 1 && tu::rms (o, (int) (sr * 0.8), 4800) > 0.01f;
        MidiBuffer m2; m2.addEvent (MidiMessage::controllerEvent (1, 64, 0), 0);
        AudioBuffer<float> in (2, (int) sr); in.clear();
        tu::render (p, in, 256, &m2);
        check (held && p.activeVoices.load() == 0, "pedal de sustain: sostiene la nota y la suelta al levantarlo");
    }

    // the mod wheel opens the filter (factory routing)
    {
        auto bright = [&] (int wheelValue)
        {
            MidiBuffer m;
            m.addEvent (MidiMessage::controllerEvent (1, 1, wheelValue), 0);
            m.addEvent (MidiMessage::noteOn (1, 48, (uint8) 100), 0);
            auto o = run ([] (Processor& p) { p.setRealValue ("osc1_bank", 6); p.setRealValue ("cutoff", 500); p.setRealValue ("chorus", 0); }, m, (int) sr * 2);
            auto s = tu::spectrum (o, 0, (int) sr, 14);
            double hi = 0, all = 0;
            for (size_t i = 1; i < s.size(); ++i) { all += s[i]; if ((double) i * sr / 16384.0 > 2000.0) hi += s[i]; }
            return hi / all;
        };
        const double closed = bright (0), open = bright (127);
        check (open > closed * 4.0, "la rueda de modulación abre el filtro ("_u + String (10.0 * std::log10 (open / closed), 1) + " dB más agudos)"_u);
    }
}

static RegisterTest regDormouse ("Dormouse", testDormouse);

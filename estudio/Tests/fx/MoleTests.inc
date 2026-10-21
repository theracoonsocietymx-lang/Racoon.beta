// Mole checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Mole/MoleProcessor.h"

static void testMole (const juce::File& out)
{
    using namespace juce;
    using racoon::mole::Processor;
    commonChecks ("Mole", [] { return new Processor(); }, out);
    std::cout << "filtro" << std::endl;
    const double sr = 48000.0;
    auto run = [&] (std::function<void (Processor&)> setup, std::function<void (AudioBuffer<float>&)> fill, MidiBuffer* midi = nullptr, int len = 48000)
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        setup (p);
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, len);
        fill (in);
        return tu::render (p, in, 256, midi);
    };
    auto sine = [&] (double f) { return [f, sr] (AudioBuffer<float>& b) { tu::fillSine (b, f, sr, 0.5f, 0.0); }; };
    auto lvl = [&] (const AudioBuffer<float>& b) { return racoon::dsp::gainToDb (tu::rms (b, (int) (sr * 0.5), (int) (sr * 0.4))); };
    const float ref = racoon::dsp::gainToDb (0.5f / std::sqrt (2.0f));

    const float lp5k = lvl (run ([] (Processor& p) { p.setRealValue ("cutoff", 500); p.setRealValue ("reso", 0); }, sine (5000.0)));
    check (lp5k < ref - 30.0f, "paso bajo a 500 Hz: 5 kHz baja " + String (ref - lp5k, 1) + " dB");
    const float lp100 = lvl (run ([] (Processor& p) { p.setRealValue ("cutoff", 2000); p.setRealValue ("reso", 0); }, sine (100.0)));
    check (std::abs (lp100 - ref) < 1.0f, "paso bajo deja pasar 100 Hz (" + String (lp100 - ref, 2) + " dB)");
    const float hp50 = lvl (run ([] (Processor& p) { p.setRealValue ("type", 1); p.setRealValue ("cutoff", 1000); p.setRealValue ("reso", 0); }, sine (50.0)));
    check (hp50 < ref - 30.0f, "paso alto a 1 kHz: 50 Hz baja " + String (ref - hp50, 1) + " dB");
    const float n1k = lvl (run ([] (Processor& p) { p.setRealValue ("type", 3); p.setRealValue ("cutoff", 1000); p.setRealValue ("reso", 50); p.setRealValue ("lfodepth", 0); }, sine (1000.0)));
    check (n1k < ref - 20.0f, "notch en 1 kHz quita 1 kHz (" + String (ref - n1k, 1) + " dB)");
    const float ap = lvl (run ([] (Processor& p) { p.setRealValue ("type", 4); p.setRealValue ("cutoff", 1000); p.setRealValue ("reso", 60); }, sine (1000.0)));
    check (std::abs (ap - ref) < 0.5f, "allpass conserva el nivel (" + String (ap - ref, 2) + " dB)");

    // stability: resonance near Nyquist with fast modulation, every type
    bool stable = true;
    for (int t = 0; t < 8; ++t)
    {
        auto o = run ([t] (Processor& p)
        {
            p.setRealValue ("type", (float) t); p.setRealValue ("cutoff", 19000); p.setRealValue ("reso", 98);
            p.setRealValue ("lfodepth", 100); p.setRealValue ("lforate", 30); p.setRealValue ("lfoshape", 4); p.setRealValue ("drive", 30);
        }, [] (AudioBuffer<float>& b) { tu::fillNoise (b, 0.5f, 3); });
        const bool ok = tu::allFinite (o) && tu::peak (o, 0, o.getNumSamples()) < 12.0f;
        if (! ok) std::cout << "      inestable tipo " << t << " pico " << tu::peak (o, 0, o.getNumSamples()) << std::endl;
        stable = stable && ok;
    }
    check (stable, "los 8 tipos estables con resonancia 98 % cerca de Nyquist y LFO a 30 Hz");

    // step modulator moves the level over time
    {
        auto o = run ([] (Processor& p) { p.setRealValue ("cutoff", 200); p.setRealValue ("stepamt", 100); p.setRealValue ("stepdiv", 7); p.setRealValue ("stepglide", 0); },
                      [] (AudioBuffer<float>& b) { tu::fillNoise (b, 0.5f, 9); });
        float lo = 1e9f, hi = 0;
        for (int k = 0; k < 16; ++k) { const float r = tu::rms (o, (int) (sr * 0.1) + k * 2000, 1500); lo = jmin (lo, r); hi = jmax (hi, r); }
        check (racoon::dsp::gainToDb (hi) - racoon::dsp::gainToDb (lo) > 10.0f, "modulador de pasos abre y cierra (" + String (racoon::dsp::gainToDb (hi) - racoon::dsp::gainToDb (lo), 1) + " dB)");
    }

    // key tracking follows MIDI notes
    auto bright = [&] (int note)
    {
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), 0);
        auto o = run ([] (Processor& p) { p.setRealValue ("cutoff", 500); p.setRealValue ("keytrack", 100); p.setRealValue ("reso", 0); }, sine (2000.0), &m);
        return lvl (o);
    };
    const float hiNote = bright (84), loNote = bright (36);
    check (hiNote > loNote + 20.0f, "seguir teclado: C6 abre " + String (hiNote - loNote, 1) + " dB más que C1"_u);
}
static RegisterTest regMole ("filtro-automatico", testMole);

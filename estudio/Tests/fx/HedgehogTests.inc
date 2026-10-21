// Hedgehog checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Hedgehog/HedgehogProcessor.h"

static void testHedgehog (const juce::File& out)
{
    using namespace juce;
    using racoon::hedgehog::Processor;
    commonChecks ("Hedgehog", [] { return new Processor(); }, out);
    std::cout << "dinámica" << std::endl;

    // static curve
    check (std::abs (Processor::gainComputer (-10.0f, -20.0f, 4.0f, 0.0f, false) + 7.5f) < 0.01f, "curva: 10 dB sobre el umbral a 4:1 reduce 7.5 dB");
    check (std::abs (Processor::gainComputer (-30.0f, -20.0f, 4.0f, 0.0f, false)) < 1e-6f, "curva: bajo el umbral no toca");
    const float kneeMid = Processor::gainComputer (-20.0f, -20.0f, 4.0f, 10.0f, false);
    check (kneeMid < 0.0f && kneeMid > -2.0f, "rodilla suave: en el umbral ya comprime un poco (" + String (kneeMid, 2) + " dB)");

    const double sr = 48000.0;
    auto steady = [&] (std::function<void (Processor&)> setup, float inDb) -> std::pair<float, float>
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        setup (p);
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, (int) sr);
        tu::fillSine (in, 1000.0, sr, racoon::dsp::dbToGain (inDb), 0.0);
        auto o = tu::render (p, in, 256, nullptr);
        const int lat = p.getLatencySamples();
        return { racoon::dsp::gainToDb (tu::peak (o, (int) (sr * 0.7), (int) (sr * 0.25))), (float) lat };
    };

    // unity without compression
    auto [u, lat0] = steady ([] (Processor& p) { p.setRealValue ("thresh", 6.0f); }, -20.0f);
    check (std::abs (u + 20.0f) < 0.05f, "ganancia unitaria sin compresión: "_u + String (u, 2) + " dBFS");

    // 4:1 above the threshold: -6 dBFS in, threshold -20 -> about -16.5 dBFS out
    auto [c4, lat1] = steady ([] (Processor& p)
    {
        p.setRealValue ("thresh", -20.0f); p.setRealValue ("ratio", 4.0f); p.setRealValue ("knee", 0.0f);
        p.setRealValue ("attack", 1.0f); p.setRealValue ("release", 200.0f);
    }, -6.0f);
    check (std::abs (c4 + 16.5f) < 1.5f, "4:1 con -6 dBFS y umbral -20: sale en " + String (c4, 2) + " dBFS (esperado -16.5)");

    // lookahead reports latency
    auto [lk, latL] = steady ([] (Processor& p) { p.setRealValue ("look", 5.0f); p.setRealValue ("thresh", 6.0f); }, -12.0f);
    check (std::abs (latL - 240.0f) < 2.0f, "lookahead de 5 ms informa " + String ((int) latL) + " muestras de latencia");

    // overshoot on a step and settling
    {
        Processor p;
        p.prepareToPlay (sr, 128);
        p.setRealValue ("thresh", -20.0f); p.setRealValue ("ratio", 8.0f); p.setRealValue ("knee", 0.0f);
        p.setRealValue ("attack", 20.0f); p.setRealValue ("release", 100.0f);
        AudioBuffer<float> in (2, (int) sr);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float a = i < (int) (sr * 0.3) ? racoon::dsp::dbToGain (-40.0f) : racoon::dsp::dbToGain (-6.0f);
            const float v = a * (float) std::sin (MathConstants<double>::twoPi * 500.0 * i / sr);
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        auto o = tu::render (p, in, 128, nullptr);
        const float early = racoon::dsp::gainToDb (tu::peak (o, (int) (sr * 0.3), (int) (sr * 0.005)));
        const float late = racoon::dsp::gainToDb (tu::peak (o, (int) (sr * 0.7), (int) (sr * 0.2)));
        check (early > late + 3.0f, "ataque de 20 ms deja pasar el golpe (" + String (early, 1) + " dB) y luego baja a " + String (late, 1) + " dB");
    }

    // external sidechain: kick pumps the bass
    auto pump = [&] (bool ext) -> float
    {
        Processor p;
        AudioProcessor::BusesLayout l;
        l.inputBuses.add (AudioChannelSet::stereo());
        l.inputBuses.add (AudioChannelSet::stereo());
        l.outputBuses.add (AudioChannelSet::stereo());
        p.setBusesLayout (l);
        p.prepareToPlay (sr, 256);
        p.setRealValue ("thresh", -30.0f); p.setRealValue ("ratio", 10.0f); p.setRealValue ("attack", 0.5f);
        p.setRealValue ("release", 120.0f); p.setRealValue ("scext", ext ? 1.0f : 0.0f);
        AudioBuffer<float> in (4, (int) sr);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float bass = 0.05f * (float) std::sin (MathConstants<double>::twoPi * 55.0 * i / sr);     // -26 dBFS: under the threshold
            const double t = std::fmod ((double) i / sr, 0.5);
            const float kick = t < 0.08 ? 0.9f * (float) std::sin (MathConstants<double>::twoPi * 60.0 * t) * (float) std::exp (-t * 30.0) : 0.0f;
            in.setSample (0, i, bass); in.setSample (1, i, bass);
            in.setSample (2, i, kick); in.setSample (3, i, kick);
        }
        auto o4 = tu::render (p, in, 256, nullptr);
        AudioBuffer<float> o (2, o4.getNumSamples());
        for (int c = 0; c < 2; ++c) o.copyFrom (c, 0, o4, c, 0, o4.getNumSamples());
        const float duringKick = tu::rms (o, (int) (sr * 0.505), (int) (sr * 0.03));
        const float between = tu::rms (o, (int) (sr * 0.86), (int) (sr * 0.03));
        return racoon::dsp::gainToDb (duringKick) - racoon::dsp::gainToDb (between);
    };
    const float pumped = pump (true), flat = pump (false);
    check (pumped < -6.0f, "sidechain externo: el bombo baja el bajo " + String (pumped, 1) + " dB");
    check (std::abs (flat) < 1.0f, "sin sidechain el bajo no se mueve (" + String (flat, 1) + " dB)");
    ignoreUnused (lat0, lat1, lk);
}
static RegisterTest regHedgehog ("compresor", testHedgehog);

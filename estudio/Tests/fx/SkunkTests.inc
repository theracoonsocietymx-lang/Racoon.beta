// Skunk checks (included by Tests/FamilyTest.cpp)
#include "fx/effects/Skunk/SkunkProcessor.h"

static void testSkunk (const juce::File& out)
{
    using namespace juce;
    using racoon::skunk::Processor;
    commonChecks ("Skunk", [] { return new Processor(); }, out);
    std::cout << "saturación"_u << std::endl;
    const double sr = 48000.0;

    auto run = [&] (std::function<void (Processor&)> setup, double freq, float level, int seconds = 1) -> std::pair<AudioBuffer<float>, int>
    {
        Processor p;
        p.prepareToPlay (sr, 512);
        setup (p);
        p.prepareToPlay (sr, 512);
        AudioBuffer<float> in (2, (int) sr * seconds);
        tu::fillSine (in, freq, sr, level, 0.0);
        auto o = tu::render (p, in, 512, nullptr);
        return { o, p.getLatencySamples() };
    };

    // THD rises with drive
    auto thd = [&] (float driveDb)
    {
        auto [o, lat] = run ([driveDb] (Processor& p) { p.setRealValue ("drive", driveDb); p.setRealValue ("autogain", 0); p.setRealValue ("os", 2); }, 1000.0, 0.25f);
        const int st = 24000, len = 16384;
        const float f = tu::tone (o, 0, st, len, 1000.0, sr);
        double h = 0;
        for (int k = 2; k <= 9; ++k) h += std::pow (tu::tone (o, 0, st, len, 1000.0 * k, sr), 2.0);
        return 100.0f * (float) std::sqrt (h) / jmax (1.0e-9f, f);
    };
    const float t0 = thd (0.0f), t12 = thd (12.0f), t30 = thd (30.0f);
    check (t0 < t12 && t12 < t30, "THD sube con el drive: " + String (t0, 2) + " % / " + String (t12, 1) + " % / " + String (t30, 1) + " %");

    // aliasing at 10 kHz with a hard clip: oversampling cuts it
    auto alias = [&] (int osIndex)
    {
        auto [o, lat] = run ([osIndex] (Processor& p) { p.setRealValue ("mode", 1); p.setRealValue ("drive", 24); p.setRealValue ("os", (float) osIndex); p.setRealValue ("autogain", 0); p.setRealValue ("hq", 0); }, 10000.0, 0.5f);
        auto spec = tu::spectrum (o, 0, 16384, 14);
        double harmonic = 0, other = 0;
        const double binHz = sr / 16384.0;
        for (size_t k = 2; k < spec.size(); ++k)
        {
            const double f = k * binHz;
            const double m = std::fmod (f, 10000.0);
            const bool isHarm = (m < 60.0 || m > 9940.0) && f < sr * 0.5;
            (isHarm ? harmonic : other) += spec[k];
        }
        return 10.0f * (float) std::log10 (other / jmax (1.0e-12, harmonic));
    };
    const float a0 = alias (0), a8 = alias (3);
    check (a8 < a0 - 10.0f, "aliasing a 10 kHz: " + String (a0, 1) + " dB sin oversampling, " + String (a8, 1) + " dB con 8x");
    const float a1k = [&] { auto [o, lat] = run ([] (Processor& p) { p.setRealValue ("mode", 1); p.setRealValue ("drive", 24); p.setRealValue ("os", 3); }, 1000.0, 0.5f);
                            auto sp = tu::spectrum (o, 0, 16384, 14); double h = 0, x = 0; const double bin = sr / 16384.0;
                            for (size_t k = 2; k < sp.size(); ++k) { const double m = std::fmod (k * bin, 1000.0); ((m < 8.0 || m > 992.0) ? h : x) += sp[k]; }
                            return 10.0f * (float) std::log10 (x / jmax (1.0e-12, h)); }();
    check (a1k < -40.0f, "aliasing a 1 kHz con 8x: " + String (a1k, 1) + " dB");

    // no DC after an asymmetric motor with bias
    {
        auto [o, lat] = run ([] (Processor& p) { p.setRealValue ("mode", 3); p.setRealValue ("drive", 24); p.setRealValue ("bias", 60); }, 200.0, 0.5f, 2);
        double mean = 0; for (int i = (int) sr; i < 2 * (int) sr; ++i) mean += o.getSample (0, i);
        mean /= sr;
        check (std::abs (mean) < 1.0e-3, "sin DC tras diodo con bias: " + String (mean, 5));
    }

    // latency matches what is reported (impulse at drive 0 dB)
    for (int k : { 0, 1, 2, 3 })
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("os", (float) k); p.setRealValue ("drive", 0.0f); p.setRealValue ("autogain", 0); p.setRealValue ("hq", 0);
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> in (2, 8192);
        in.clear();
        in.setSample (0, 1000, 0.5f); in.setSample (1, 1000, 0.5f);
        auto o = tu::render (p, in, 256, nullptr);
        int at = 0; float best = 0;
        for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > best) { best = std::abs (o.getSample (0, i)); at = i; }
        check (std::abs ((at - 1000) - p.getLatencySamples()) <= 1, "oversampling " + String (1 << k) + "x: latencia informada " + String (p.getLatencySamples()) + ", medida " + String (at - 1000));
    }

    // switching quality while playing does not click
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("drive", 6); p.setRealValue ("os", 1);
        AudioBuffer<float> sine (2, (int) sr), io (2, 256), o (2, (int) sr);
        tu::fillSine (sine, 220.0, sr, 0.3f, 0.0);
        for (int pos = 0; pos + 256 <= (int) sr; pos += 256)
        {
            if (pos == 256 * 80) p.setRealValue ("os", 3);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, sine, c, pos, 256);
            MidiBuffer m; p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 256);
        }
        AudioBuffer<float> seg (2, 6000);
        for (int c = 0; c < 2; ++c) seg.copyFrom (c, 0, o, c, 256 * 78, 6000);
        check (tu::maxJump (seg) < 0.08f, "cambiar de 2x a 8x sin clic (salto " + String (tu::maxJump (seg), 3) + ")");
    }
}
static RegisterTest regSkunk ("saturador", testSkunk);

#include "Wavetables.h"
#include "../Params.h"

namespace be
{
using namespace juce;

namespace
{
constexpr int kSpecKeep = 256;   // harmonics stored for display / spectral source

struct FftCache
{
    std::unique_ptr<dsp::FFT> ffts[12];
    dsp::FFT& get (int order)
    {
        if (! ffts[order]) ffts[order] = std::make_unique<dsp::FFT> (order);
        return *ffts[order];
    }
};

int orderOf (int len) { int o = 0; while ((1 << o) < len) ++o; return o; }
}

void WaveTable::allocate (int numFrames)
{
    frames = std::max (1, numFrames);
    frameStride = 0;
    for (int l = 0; l < kLevels; ++l) frameStride += lengthOf (l);
    data.assign ((size_t) (frameStride * frames), 0.0f);
    spectra.assign ((size_t) frames, std::vector<float> (kSpecKeep, 0.0f));
}

void WaveTable::buildFrameFromSpectrum (int frame, const float* re, const float* im)
{
    // store magnitude spectrum for display
    auto& sp = spectra[(size_t) frame];
    for (int h = 1; h <= kSpecKeep; ++h)
        sp[(size_t) h - 1] = std::sqrt (re[h] * re[h] + im[h] * im[h]);

    thread_local FftCache cache;
    std::vector<float> buf;
    for (int L = 0; L < kLevels; ++L)
    {
        const int len = lengthOf (L);
        const int H = std::min (kMaxHarm >> L, len / 2 - 1);
        auto& fft = cache.get (orderOf (len));
        buf.assign ((size_t) len * 2, 0.0f);
        for (int h = 1; h <= H; ++h)
        {
            buf[(size_t) (2 * h)] = re[h];
            buf[(size_t) (2 * h + 1)] = im[h];
        }
        fft.performRealOnlyInverseTransform (buf.data());   // scaled by 1/len
        const float levelScale = (float) len * 0.5f;          // bin value V -> sinusoid amplitude V
        float* dst = data.data() + offsetOf (frame, L);
        for (int i = 0; i < len; ++i) dst[i] = buf[(size_t) i] * levelScale;
    }
}

std::shared_ptr<WaveTable> WaveTable::fromSpectra (const std::vector<std::vector<float>>& amps,
                                                   const std::vector<std::vector<float>>* phases, bool normalise)
{
    auto wt = std::make_shared<WaveTable>();
    wt->allocate ((int) amps.size());
    std::vector<float> re (kMaxHarm + 2, 0.0f), im (kMaxHarm + 2, 0.0f);
    for (int f = 0; f < wt->frames; ++f)
    {
        std::fill (re.begin(), re.end(), 0.0f);
        std::fill (im.begin(), im.end(), 0.0f);
        const auto& a = amps[(size_t) f];
        for (int h = 1; h <= std::min ((int) a.size(), kMaxHarm - 1); ++h)
        {
            const float ph = phases != nullptr && (size_t) f < phases->size() && (size_t) (h - 1) < (*phases)[(size_t) f].size()
                               ? (*phases)[(size_t) f][(size_t) h - 1] : 0.0f;
            // sine phase convention: a*sin(wt+ph)  ->  re = a*sin(ph)?  use (re,im) = (0,-a) for pure sine
            re[(size_t) h] = a[(size_t) h - 1] * std::sin (ph);
            im[(size_t) h] = -a[(size_t) h - 1] * std::cos (ph);
        }
        wt->buildFrameFromSpectrum (f, re.data(), im.data());
    }
    if (normalise)
    {
        for (int f = 0; f < wt->frames; ++f)
        {
            float peak = 1.0e-9f;
            const float* base = wt->data.data() + wt->offsetOf (f, 0);
            for (int i = 0; i < kBaseSize; ++i) peak = std::max (peak, std::abs (base[i]));
            const float g = 1.0f / peak;
            float* fr = wt->data.data() + f * wt->frameStride;
            for (int i = 0; i < wt->frameStride; ++i) fr[i] *= g;
            for (auto& s : wt->spectra[(size_t) f]) s *= g;
        }
    }
    return wt;
}

std::shared_ptr<WaveTable> WaveTable::fromCycles (const std::vector<std::vector<float>>& cycles, bool normalise)
{
    auto wt = std::make_shared<WaveTable>();
    wt->allocate ((int) cycles.size());
    thread_local FftCache cache;
    auto& fft = cache.get (orderOf (kBaseSize));
    std::vector<float> buf ((size_t) kBaseSize * 2);
    std::vector<float> re (kMaxHarm + 2, 0.0f), im (kMaxHarm + 2, 0.0f);
    for (int f = 0; f < wt->frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        const auto& c = cycles[(size_t) f];
        for (int i = 0; i < kBaseSize; ++i) buf[(size_t) i] = c[(size_t) (i % (int) c.size())];
        fft.performRealOnlyForwardTransform (buf.data());
        // JUCE forward is unscaled; inverse divides by N: keep values consistent
        for (int h = 0; h <= kMaxHarm; ++h)
        {
            re[(size_t) h] = buf[(size_t) (2 * h)] * (2.0f / (float) kBaseSize);
            im[(size_t) h] = buf[(size_t) (2 * h + 1)] * (2.0f / (float) kBaseSize);
        }
        re[0] = im[0] = 0.0f;   // remove DC
        wt->buildFrameFromSpectrum (f, re.data(), im.data());
    }
    if (normalise)
    {
        for (int f = 0; f < wt->frames; ++f)
        {
            float peak = 1.0e-9f;
            const float* base = wt->data.data() + wt->offsetOf (f, 0);
            for (int i = 0; i < kBaseSize; ++i) peak = std::max (peak, std::abs (base[i]));
            const float g = 1.0f / peak;
            float* fr = wt->data.data() + f * wt->frameStride;
            for (int i = 0; i < wt->frameStride; ++i) fr[i] *= g;
            for (auto& s : wt->spectra[(size_t) f]) s *= g;
        }
    }
    return wt;
}

// ============================================================ factory generators
namespace
{
using CycleFn = std::function<float (float t, float x)>;
using SpecFn = std::function<float (float t, int h)>;

WaveTablePtr makeFromCycleFn (CycleFn fn, int frames = 16)
{
    std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> (WaveTable::kBaseSize));
    for (int f = 0; f < frames; ++f)
    {
        const float t = frames > 1 ? (float) f / (float) (frames - 1) : 0.0f;
        for (int i = 0; i < WaveTable::kBaseSize; ++i)
            cycles[(size_t) f][(size_t) i] = fn (t, (float) i / (float) WaveTable::kBaseSize);
    }
    return WaveTable::fromCycles (cycles);
}

WaveTablePtr makeFromSpecFn (SpecFn fn, int frames = 16, int maxH = 512)
{
    std::vector<std::vector<float>> amps ((size_t) frames, std::vector<float> ((size_t) maxH, 0.0f));
    for (int f = 0; f < frames; ++f)
    {
        const float t = frames > 1 ? (float) f / (float) (frames - 1) : 0.0f;
        for (int h = 1; h <= maxH; ++h) amps[(size_t) f][(size_t) h - 1] = fn (t, h);
    }
    return WaveTable::fromSpectra (amps);
}

float sawf (float x) { x -= std::floor (x); return 2.0f * x - 1.0f; }
float sqrf (float x, float pw = 0.5f) { x -= std::floor (x); return x < pw ? 1.0f : -1.0f; }
float trif (float x) { x -= std::floor (x); return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; }
float sinf2 (float x) { return std::sin (kTwoPi * x); }
float fold (float x) { return std::sin (x * kPi * 0.5f); }
float smooth (float e, float x) { return clamp01 (x / std::max (e, 1.0e-4f)); }

// formant amplitude at frequency (Hz) for vowel v (0..4), assuming harmonic spacing of 110 Hz
float formantGain (float hz, float vowel)
{
    static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    const int a = (int) vowel; const int b = std::min (a + 1, 4); const float t = vowel - (float) a;
    float g = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        const float fc = lerpf (F[a][k], F[b][k], t);
        const float bw = 80.0f + 40.0f * (float) k;
        g += (k == 0 ? 1.0f : 0.6f / (float) k) / (1.0f + ((hz - fc) / bw) * ((hz - fc) / bw));
    }
    return g;
}
}

FactoryBank& FactoryBank::get()
{
    static FactoryBank bank;
    return bank;
}

FactoryBank::FactoryBank()
{
    tables.resize (32);
    tables[0] = makeFromCycleFn ([] (float t, float x)
    {
        const float seg = t * 3.0f; const int s = std::min ((int) seg, 2); const float u = seg - (float) s;
        const float w[4] { sinf2 (x), trif (x + 0.25f), sawf (x + 0.5f), sqrf (x) };
        return lerpf (w[s], w[s + 1], u);
    });
    tables[1] = makeFromCycleFn ([] (float t, float x) { return sqrf (x, 0.5f - t * 0.46f); });
    tables[2] = makeFromCycleFn ([] (float t, float x)
    {
        return sawf (x) + t * 0.6f * sawf (2.0f * x) + smooth (0.5f, t - 0.3f) * 0.4f * sawf (3.0f * x) + smooth (0.3f, t - 0.7f) * 0.3f * sawf (4.0f * x);
    });
    tables[3] = makeFromSpecFn ([] (float t, int h)
    {
        const float n = 1.0f + t * 63.0f;
        return (1.0f / (float) h) * clamp01 (n - (float) h + 1.0f);
    });
    tables[4] = makeFromSpecFn ([] (float t, int h)
    {
        if (h % 2 == 0) return 0.0f;
        const float n = 1.0f + t * 63.0f;
        return (1.0f / (float) h) * clamp01 (n - (float) h + 1.0f);
    });
    tables[5] = makeFromSpecFn ([] (float t, int h) { return formantGain ((float) h * 110.0f, t * 4.0f) / std::sqrt ((float) h); }, 32);
    tables[6] = makeFromCycleFn ([] (float t, float x) { return std::sin (kTwoPi * x + t * 4.0f * std::sin (2.0f * kTwoPi * x) + t * 1.5f * std::sin (3.0f * kTwoPi * x)); });
    tables[7] = makeFromCycleFn ([] (float t, float x) { return sawf (x) - sawf (x + 0.02f + t * 0.48f); });
    tables[8] = makeFromCycleFn ([] (float t, float x) { return std::sin (kTwoPi * x + t * 6.0f * std::sin (2.0f * kTwoPi * x)); });
    tables[9] = makeFromCycleFn ([] (float t, float x) { return std::sin (kTwoPi * x + t * 3.5f * std::sin (3.0f * kTwoPi * x) + t * t * 1.5f * std::sin (kTwoPi * x)); });
    tables[10] = makeFromCycleFn ([] (float t, float x) { const float r = 1.0f + t * 5.0f; return sawf (x * r) * (1.0f - x * 0.3f); }, 32);
    tables[11] = makeFromCycleFn ([] (float t, float x) { return fold (std::sin (kTwoPi * x) * (1.0f + t * 6.0f)); });
    tables[12] = makeFromSpecFn ([] (float t, int h)
    {
        const int bars[] { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
        for (int k = 0; k < 9; ++k)
            if (bars[k] == h) return clamp01 (t * 9.0f - (float) k + 1.0f) * (k == 0 ? 1.0f : 0.8f / std::sqrt ((float) k));
        return 0.0f;
    });
    tables[13] = makeFromCycleFn ([] (float t, float x) { const float steps = std::round (32.0f - t * 28.0f); return std::round (sawf (x) * steps) / steps; });
    tables[14] = makeFromSpecFn ([] (float t, int h) { const float c = 2.0f + t * 10.0f; return (1.0f / (float) h) * (0.08f + 1.5f * std::exp (-0.5f * ((float) h - c) * ((float) h - c) / 4.0f)) + (h == 1 ? 0.6f : 0.0f); });
    tables[15] = makeFromCycleFn ([] (float t, float x) { const float s = sawf (x); return std::tanh ((s + 0.35f * t) * (1.0f + t * 12.0f)) - std::tanh (0.35f * t * (1.0f + t * 12.0f)); });
    tables[16] = makeFromSpecFn ([] (float t, int h)
    {
        const float fc = 40.0f * (1.0f - t) + 1.5f; const float r = (float) h / fc;
        return (1.0f / (float) h) / std::sqrt (1.0f + std::pow (r, 8.0f)) * (1.0f + 1.5f * std::exp (-std::pow (r - 1.0f, 2.0f) * 8.0f));
    });
    tables[17] = makeFromSpecFn ([] (float t, int h) { return (h % 2 == 1 ? 1.0f : t * 0.8f) / ((float) h * (float) h) * (float) h * 0.5f + (h == 1 ? 0.5f : 0.0f); });
    tables[18] = makeFromCycleFn ([] (float t, float x) { const float lv = std::exp2 (7.0f - t * 6.0f); return std::round (std::sin (kTwoPi * x) * lv) / lv; });
    tables[19] = makeFromSpecFn ([] (float t, int h) { const float pos = 1.0f + t * 15.0f; return (h == 1 ? 0.6f : 0.0f) + std::max (0.0f, 1.0f - std::abs ((float) h - pos)); });
    tables[20] = makeFromSpecFn ([] (float t, int h) { const float n = 1.0f + t * 127.0f; return h % 2 ? clamp01 (n - (float) h + 1.0f) / (float) h : 0.0f; });
    tables[21] = makeFromCycleFn ([] (float t, float x) { return fold (trif (x) * (1.0f + t * 5.0f)); });
    tables[22] = makeFromCycleFn ([] (float t, float x)
    {
        const float k = 0.5f - t * 0.47f;
        const float px = x < k ? 0.5f * x / k : 0.5f + 0.5f * (x - k) / (1.0f - k);
        return std::cos (kTwoPi * px) * -1.0f;
    });
    tables[23] = makeFromSpecFn ([] (float t, int h) { return (1.0f / (float) h) * (0.55f + 0.45f * std::cos (kTwoPi * (float) h * (0.05f + t * 0.4f))); });
    tables[24] = makeFromSpecFn ([] (float t, int h)
    {
        Rng r; r.seed ((uint32_t) h * 2654435761u);
        const float rnd = r.uni();
        return (1.0f / std::sqrt ((float) h)) * lerpf (1.0f / std::sqrt ((float) h), rnd, t) * (h < 160 ? 1.0f : 0.0f);
    });
    tables[25] = makeFromSpecFn ([] (float t, int h) { const float n = 8.0f + t * 120.0f; return clamp01 (n - (float) h + 1.0f) * (1.0f + std::cos (kTwoPi * (float) h * t * 0.25f)) * 0.5f; });
    tables[26] = makeFromCycleFn ([] (float t, float x) { const float e = 1.0f + t * 4.0f; return std::sin (kTwoPi * std::pow (x, e)); });
    tables[27] = makeFromSpecFn ([] (float t, int h)
    {
        const bool metal = (h == 5 || h == 7 || h == 11 || h == 13 || h == 17 || h == 19);
        return (1.0f / (float) h) + (metal ? t * 0.5f : 0.0f);
    });
    tables[28] = makeFromSpecFn ([] (float t, int h) { return (1.0f / ((float) h * (float) h)) * (1.0f + 3.0f * t * std::exp (-std::pow ((float) h * 55.0f - 250.0f, 2.0f) / 20000.0f)) * (float) h * 0.7f; });
    tables[29] = makeFromSpecFn ([] (float t, int h) { return h == 1 ? 1.0f : (h == 3 ? t * 0.25f : (h == 5 ? t * t * 0.12f : (h == 2 ? t * 0.1f : 0.0f))); });
    tables[30] = makeFromSpecFn ([] (float t, int h)
    {
        const float c = 4.0f + t * 14.0f;
        return (h == 1 ? 1.0f : 0.15f / std::sqrt ((float) h)) + 0.6f * std::exp (-0.5f * ((float) h - c) * ((float) h - c) / 3.0f);
    });
    tables[31] = makeFromCycleFn ([] (float t, float x) { const float d = 1.0f + t * 8.0f; return std::tanh (sawf (x) * d) / std::tanh (d); });

    sine = makeFromCycleFn ([] (float, float x) { return std::sin (kTwoPi * x); }, 1);

    // ------------------------------------------------ factory one-shot samples (synthesised, mono 48k)
    const double sr = 48000.0;
    auto makeSample = [sr] (const char* name, float root, float seconds, std::function<void (std::vector<float>&)> gen)
    {
        auto s = std::make_shared<SampleData>();
        s->sampleRate = sr; s->rootNote = root; s->name = name;
        s->data.assign ((size_t) (seconds * sr), 0.0f);
        gen (s->data);
        float peak = 1.0e-6f;
        for (auto v : s->data) peak = std::max (peak, std::abs (v));
        for (auto& v : s->data) v *= 0.9f / peak;
        return SamplePtr (s);
    };
    auto karplus = [sr] (std::vector<float>& out, float hz, float bright, float decay, uint32_t seed)
    {
        const int N = (int) (sr / hz);
        std::vector<float> line ((size_t) N);
        Rng r; r.seed (seed);
        float lp = 0.0f;
        for (int i = 0; i < N; ++i) { lp += bright * (r.bi() - lp); line[(size_t) i] = lp; }
        int idx = 0; float prev = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float cur = line[(size_t) idx];
            const float nxt = decay * (0.5f * (cur + prev));
            prev = cur;
            line[(size_t) idx] = nxt;
            out[i] = cur;
            idx = (idx + 1) % N;
        }
    };
    samples.push_back (makeSample ("Upright Pluck", 33.0f, 2.5f, [&] (auto& d) { karplus (d, 55.0f, 0.35f, 0.9985f, 7); }));
    samples.push_back (makeSample ("808 Boom", 31.0f, 2.5f, [sr] (auto& d)
    {
        double ph = 0.0;
        for (size_t i = 0; i < d.size(); ++i)
        {
            const double t = (double) i / sr;
            const double f = 49.0 + 70.0 * std::exp (-t * 30.0);
            ph += f / sr;
            d[i] = (float) (std::sin (2.0 * MathConstants<double>::pi * ph) * std::exp (-t * 1.6));
        }
    }));
    samples.push_back (makeSample ("Analog Hit", 33.0f, 1.5f, [sr] (auto& d)
    {
        Svf f; double ph = 0.0;
        for (size_t i = 0; i < d.size(); ++i)
        {
            const float t = (float) i / (float) sr;
            ph += 55.0 / sr; ph -= std::floor (ph);
            f.set (180.0f + 4000.0f * std::exp (-t * 9.0f), 1.6f, (float) sr);
            f.tick ((float) (2.0 * ph - 1.0));
            d[i] = f.lp * std::exp (-t * 2.0f);
        }
    }));
    samples.push_back (makeSample ("Picked Bass", 28.0f, 2.5f, [&] (auto& d) { karplus (d, 41.2f, 0.75f, 0.9982f, 11); }));
    samples.push_back (makeSample ("Slap Pop", 33.0f, 1.5f, [&] (auto& d)
    {
        karplus (d, 55.0f, 0.95f, 0.997f, 23);
        Rng r; r.seed (5);
        for (size_t i = 0; i < 800 && i < d.size(); ++i) d[i] += r.bi() * 0.8f * (1.0f - (float) i / 800.0f);
    }));
    samples.push_back (makeSample ("Vinyl Texture", 60.0f, 2.0f, [sr] (auto& d)
    {
        Rng r; r.seed (99); Svf bp; bp.set (2500.0f, 0.7f, (float) sr);
        for (size_t i = 0; i < d.size(); ++i)
        {
            bp.tick (r.bi() * 0.15f);
            float c = r.uni() > 0.9993f ? r.bi() * 2.0f : 0.0f;
            d[i] = bp.bp + c;
        }
    }));
    samples.push_back (makeSample ("Breath Air", 60.0f, 2.0f, [sr] (auto& d)
    {
        Rng r; r.seed (3); Svf bp;
        for (size_t i = 0; i < d.size(); ++i)
        {
            const float t = (float) i / (float) sr;
            bp.set (900.0f + 600.0f * std::sin (t * 2.0f), 1.2f, (float) sr);
            bp.tick (r.bi());
            d[i] = bp.bp * std::sin (kPi * t / 2.0f);
        }
    }));
    samples.push_back (makeSample ("Metal Strike", 45.0f, 2.0f, [sr] (auto& d)
    {
        const float ratios[] { 1.0f, 2.76f, 5.4f, 8.93f, 13.34f };
        for (size_t i = 0; i < d.size(); ++i)
        {
            const float t = (float) i / (float) sr; float s = 0.0f;
            for (int k = 0; k < 5; ++k) s += std::sin (kTwoPi * 110.0f * ratios[k] * t) * std::exp (-t * (1.5f + (float) k * 1.8f)) / (1.0f + (float) k);
            d[i] = s;
        }
    }));
}

// ============================================================ builders
namespace TableBuilders
{
WaveTablePtr additive (const std::vector<float>& harmonics, float tilt, float oddEven)
{
    const int F = 32, H = (int) harmonics.size();
    std::vector<std::vector<float>> amps ((size_t) F, std::vector<float> ((size_t) H, 0.0f));
    for (int f = 0; f < F; ++f)
    {
        const float n = 1.0f + (float) (H - 1) * (float) f / (float) (F - 1);
        for (int h = 1; h <= H; ++h)
        {
            float a = harmonics[(size_t) h - 1];
            a *= std::pow ((float) h, tilt * 1.5f);
            if (h > 1) a *= (h % 2 == 1) ? (1.0f + std::min (0.0f, oddEven)) : (1.0f - std::max (0.0f, oddEven));
            a *= clamp01 (n - (float) h + 1.0f);
            amps[(size_t) f][(size_t) h - 1] = a;
        }
    }
    return WaveTable::fromSpectra (amps);
}

WaveTablePtr spectral (const std::vector<float>& src, int mode, float param)
{
    const int F = 32, H = (int) src.size();
    auto at = [&src, H] (float hf) -> float
    {
        if (hf < 1.0f || hf > (float) H) return 0.0f;
        const int i = (int) hf; const float t = hf - (float) i;
        const float a = src[(size_t) i - 1];
        const float b = i < H ? src[(size_t) i] : 0.0f;
        return a + (b - a) * t;
    };
    std::vector<std::vector<float>> amps ((size_t) F, std::vector<float> ((size_t) H, 0.0f));
    std::vector<std::vector<float>> phases ((size_t) F, std::vector<float> ((size_t) H, 0.0f));
    Rng rng; rng.seed (1234);
    for (int f = 0; f < F; ++f)
    {
        const float a = (float) f / (float) (F - 1);
        for (int h = 1; h <= H; ++h)
        {
            float v = src[(size_t) h - 1];
            switch (mode)
            {
                case 0: v = at ((float) h / (1.0f + a * (0.2f + param * 2.0f))); break;                                          // stretch
                case 1: v = at ((float) h - a * (1.0f + param * 31.0f)); break;                                                   // shift
                case 2: v *= (h % 2 == 1 && h > 1) ? (1.0f - a * param) : (h % 2 == 0 ? 1.0f - a * (1.0f - param) : 1.0f); break; // odd/even
                case 3: v *= 0.5f + 0.5f * std::cos (kTwoPi * (float) h * (0.02f + a * (0.1f + param * 0.4f))); break;            // comb
                case 4: v *= std::pow ((float) h, (a * 2.0f - 1.0f) * (0.25f + param * 1.5f)); break;                             // tilt
                case 5: phases[(size_t) f][(size_t) h - 1] = rng.bi() * kPi * a * (0.2f + param); break;                          // smear
                case 6: { const float c = 2.0f + a * (8.0f + param * 40.0f); v *= 1.0f + 4.0f * std::exp (-0.5f * ((float) h - c) * ((float) h - c) / (2.0f + param * 6.0f)); break; }
                case 7: if (h > 2) v *= (1.0f - a * (0.3f + param * 0.7f)); else v *= 1.0f + a * 0.5f; break;                    // harmonic lock
                default: break;
            }
            amps[(size_t) f][(size_t) h - 1] = std::max (0.0f, v);
        }
    }
    return WaveTable::fromSpectra (amps, &phases);
}

WaveTablePtr fromCycleFunction (std::function<float (float)> fn, int numFrames)
{
    std::vector<std::vector<float>> cycles ((size_t) numFrames, std::vector<float> (WaveTable::kBaseSize));
    for (int f = 0; f < numFrames; ++f)
        for (int i = 0; i < WaveTable::kBaseSize; ++i)
            cycles[(size_t) f][(size_t) i] = fn ((float) i / (float) WaveTable::kBaseSize);
    return WaveTable::fromCycles (cycles);
}

WaveTablePtr fromAudio (const AudioBuffer<float>& audio, double sampleRate, int cycleLength)
{
    const int n = audio.getNumSamples();
    if (n < 64) return {};
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c) s += audio.getSample (c, i);
        mono[(size_t) i] = s / (float) std::max (1, audio.getNumChannels());
    }
    if (cycleLength <= 0)
    {
        const float hz = detectPitchHz (mono.data(), std::min (n, (int) (sampleRate * 0.5)), sampleRate);
        cycleLength = hz > 0.0f ? (int) std::round (sampleRate / hz) : WaveTable::kBaseSize;
    }
    cycleLength = jlimit (16, 16384, cycleLength);
    const int available = n / cycleLength;
    if (available < 1) return {};
    const int frames = std::min (64, available);
    std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> (WaveTable::kBaseSize));
    for (int f = 0; f < frames; ++f)
    {
        const int c = frames > 1 ? (int) ((float) f * (float) (available - 1) / (float) (frames - 1)) : 0;
        const int start = c * cycleLength;
        for (int i = 0; i < WaveTable::kBaseSize; ++i)
        {
            const float p = (float) i * (float) cycleLength / (float) WaveTable::kBaseSize;
            const int i0 = (int) p; const float t = p - (float) i0;
            const float a = mono[(size_t) std::min (n - 1, start + i0)];
            const float b = mono[(size_t) std::min (n - 1, start + i0 + 1)];
            cycles[(size_t) f][(size_t) i] = a + (b - a) * t;
        }
    }
    return WaveTable::fromCycles (cycles);
}
} // namespace TableBuilders

// ============================================================ YIN pitch detection
float detectPitchHz (const float* x, int n, double sr, float minHz, float maxHz)
{
    const int maxLag = std::min (n / 2, (int) (sr / minHz));
    const int minLag = std::max (2, (int) (sr / maxHz));
    if (maxLag <= minLag + 2) return 0.0f;
    const int W = std::min (n - maxLag, (int) (sr * 0.08));
    if (W < 32) return 0.0f;
    std::vector<float> d ((size_t) maxLag + 1, 0.0f);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < W; ++i) { const double df = (double) x[i] - (double) x[i + tau]; s += df * df; }
        d[(size_t) tau] = (float) s;
    }
    // cumulative mean normalised difference
    std::vector<float> c ((size_t) maxLag + 1, 1.0f);
    double run = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        run += d[(size_t) tau];
        c[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * (double) tau / run) : 1.0f;
    }
    int best = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
        if (c[(size_t) tau] < 0.15f) { while (tau + 1 < maxLag && c[(size_t) tau + 1] < c[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0)
    {
        float m = 1.0e9f;
        for (int tau = minLag; tau < maxLag; ++tau) if (c[(size_t) tau] < m) { m = c[(size_t) tau]; best = tau; }
        if (m > 0.6f) return 0.0f;
    }
    // parabolic interpolation
    float tauF = (float) best;
    if (best > 1 && best < maxLag)
    {
        const float a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1];
        const float den = a - 2.0f * b + cc;
        if (std::abs (den) > 1.0e-9f) tauF += 0.5f * (a - cc) / den;
    }
    return (float) (sr / tauF);
}
} // namespace be

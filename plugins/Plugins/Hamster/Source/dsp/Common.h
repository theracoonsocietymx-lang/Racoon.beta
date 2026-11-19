#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

namespace be
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }
inline double noteToHz (double note, double a4 = 440.0) { return a4 * std::pow (2.0, (note - 69.0) / 12.0); }
inline float semisToRatio (float s) { return std::exp2 (s * (1.0f / 12.0f)); }

// fast tanh (Padé) — accurate enough for saturation, bounded to +-1
inline float fastTanh (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

inline float fastSin (float phase01)   // phase in [0,1)
{
    return std::sin (phase01 * kTwoPi);
}

// PolyBLEP residual for band-limited discontinuities
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// PolyBLAMP residual for slope discontinuities (triangle corners)
inline float polyBlamp (float t, float dt)
{
    if (t < dt)
    {
        t = t / dt - 1.0f;
        return -1.0f / 3.0f * t * t * t;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt + 1.0f;
        return 1.0f / 3.0f * t * t * t;
    }
    return 0.0f;
}

// small, fast, deterministic RNG (xorshift32)
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }       // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                                   // [-1,1)
};

// one-pole smoother
struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void setTime (float seconds, float sr) { a = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sr)); }
    void setCutoff (float hz, float sr) { a = std::exp (-kTwoPi * hz / sr); }
    float process (float x) { z = x + a * (z - x); return z; }
    void reset (float v = 0.0f) { z = v; }
};

// TPT one-pole filter (LP/HP)
struct TptOnePole
{
    float s = 0.0f, g = 0.0f;
    void setCutoff (float hz, float sr)
    {
        hz = clampf (hz, 1.0f, sr * 0.49f);
        const float wd = std::tan (kPi * hz / sr);
        g = wd / (1.0f + wd);
    }
    float lp (float x) { const float v = (x - s) * g; const float y = v + s; s = y + v; return y; }
    float hp (float x) { return x - lp (x); }
    void reset() { s = 0.0f; }
};

// Cytomic TPT state variable filter (2-pole)
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f, g = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;
    void set (float hz, float q, float sr)
    {
        hz = clampf (hz, 5.0f, sr * 0.48f);
        g = std::tan (kPi * hz / sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void tick (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float notch() const { return lp + hp; }
    void reset() { ic1 = ic2 = 0.0f; }
};

// RBJ biquad (direct form TDF-II), used for EQ / shelves
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void setNorm (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void peak (double f, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double f, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                 (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void highShelf (double f, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double c = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                 (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void highPass (double f, double q, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double f, double q, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// 4th-order Linkwitz-Riley crossover split (two cascaded Butterworth 2nd-order)
struct LR4
{
    Svf l1, l2, h1, h2;
    void set (float hz, float sr) { l1.set (hz, 0.7071f, sr); l2.set (hz, 0.7071f, sr); h1.set (hz, 0.7071f, sr); h2.set (hz, 0.7071f, sr); }
    void split (float x, float& lo, float& hi)
    {
        l1.tick (x); l2.tick (l1.lp); lo = l2.lp;
        h1.tick (x); h2.tick (h1.hp); hi = h2.hp;
    }
    void reset() { l1.reset(); l2.reset(); h1.reset(); h2.reset(); }
};

// fractional delay line with cubic interpolation
struct DelayLine
{
    std::vector<float> buf;
    int w = 0, mask = 0;
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float delay) const     // delay in samples (>= 1)
    {
        const float rp = (float) w - delay;
        int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)],
                    y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
};

// Shared drive curves (used by layers, filters and distortion)
inline float shapeDrive (int type, float x, float drive)
{
    const float g = 1.0f + drive * drive * 24.0f;
    switch (type)
    {
        case 0: return fastTanh (x * g) / (drive > 0.0f ? fastTanh (g) * 0.85f + 0.15f : 1.0f);             // soft
        case 1: { const float y = x * g; return (y >= 0 ? fastTanh (y * 1.2f) : fastTanh (y * 0.8f)) * 0.95f; } // tube (asym)
        case 2: { const float y = x * g * 0.8f; return y / (1.0f + std::abs (y)) * 1.35f; }                      // tape
        case 3: { const float y = x * g; return y >= 0 ? 1.0f - std::exp (-y) : -0.6f * (1.0f - std::exp (0.6f * y)); } // diode
        case 4: { float y = x * (1.0f + drive * 6.0f); return std::sin (y * kPi * 0.5f); }                       // fold
        case 5: { const float y = x * g + drive * 0.3f; return fastTanh (y) - fastTanh (drive * 0.3f); }        // asym
        default: return x;
    }
}

// lock-free single-producer/single-consumer float ring (audio -> UI)
struct ScopeFifo
{
    static constexpr int size = 8192;
    float data[size] {};
    std::atomic<int> writePos { 0 };
    void push (const float* x, int n)
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { data[w] = x[i]; w = (w + 1) & (size - 1); }
        writePos.store (w, std::memory_order_release);
    }
    void copyLatest (float* dest, int n) const
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dest[i] = data[(w - n + i + size * 4) & (size - 1)];
    }
};
} // namespace be

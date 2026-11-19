#pragma once
// Shared DSP for Racoon and the 11 plugins (the same file lives in Racoon/Source/fx/core/RacoonDsp.h; keep them equal).
//
//  - ADAA: antiderivative antialiasing for memoryless saturation (Parker, Zavalishin & Le Bivic, DAFx 2016;
//    Bilbao, Esqueda, Parker & Välimäki, IEEE SPL 2017). First order: y = (F(x) - F(x1)) / (x - x1).
//    It removes most of the aliasing a waveshaper makes, at almost no cost, and works on top of oversampling.
//  - Verb: an 8-line feedback delay network reverb (Jot & Chaigne 1991) with a Hadamard matrix, per-line two-band
//    absorption set from the decay time, input diffusion and slowly modulated lines. It replaces Freeverb
//    (juce::Reverb), with the same knobs so it drops in where juce::Reverb::Parameters was used.
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace racoon::sdsp
{
// ===================================================================== saturation curves with their antiderivatives
namespace curve
{
// log(cosh(x)), stable for any x: the antiderivative of tanh
inline double logCosh (double x)
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
}
struct Tanh
{
    static double f (double x) { return std::tanh (x); }
    static double F (double x) { return logCosh (x); }
};
struct Hard   // clip at +-1
{
    static double f (double x) { return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); }
    static double F (double x) { const double a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
};
struct Atan   // atan(x), output up to +-pi/2
{
    static double f (double x) { return std::atan (x); }
    static double F (double x) { return x * std::atan (x) - 0.5 * std::log1p (x * x); }
};
struct Cubic  // x - x^3/3 inside +-1, +-2/3 outside
{
    static double f (double x) { if (x >= 1.0) return 2.0 / 3.0; if (x <= -1.0) return -2.0 / 3.0; return x - x * x * x / 3.0; }
    static double F (double x) { const double a = std::abs (x); if (a >= 1.0) return 2.0 / 3.0 * a - 0.25; return 0.5 * x * x - x * x * x * x / 12.0; }
};
struct Diode  // a soft diode pair, slightly asymmetric: tanh(x) above zero, 0.9/0.85 tanh(0.85 x) below
{
    static double f (double x) { return x > 0.0 ? std::tanh (x) : std::tanh (x * 0.85) / 0.85 * 0.9; }
    static double F (double x) { return x > 0.0 ? logCosh (x) : logCosh (0.85 * x) * (0.9 / (0.85 * 0.85)); }
};
struct Knee   // untouched below 0.8, rounds into 1.0 above (a safety at the top)
{
    static double f (double x) { const double a = std::abs (x); return a < 0.8 ? x : std::copysign (0.8 + 0.2 * std::tanh ((a - 0.8) / 0.2), x); }
    static double F (double x) { const double a = std::abs (x); return a < 0.8 ? 0.5 * x * x : 0.32 + 0.8 * (a - 0.8) + 0.04 * logCosh ((a - 0.8) / 0.2); }
};
struct Cubic15 // 1.5 x - 0.5 x^3 inside +-1, +-1 outside (reaches the ceiling with a round shoulder)
{
    static double f (double x) { if (x >= 1.0) return 1.0; if (x <= -1.0) return -1.0; return 1.5 * x - 0.5 * x * x * x; }
    static double F (double x) { const double a = std::abs (x); if (a >= 1.0) return a - 0.375; return 0.75 * x * x - x * x * x * x / 8.0; }
};
struct AsymClip // tanh moved off centre by 0.1, each half scaled back to exactly one
{
    static double t0() { return std::tanh (0.1); }
    static double f (double x) { const double v = std::tanh (x + 0.1) - t0(); return v >= 0.0 ? v / (1.0 - t0()) : v / (1.0 + t0()); }
    static double F (double x)
    {
        const double g = logCosh (x + 0.1) - t0() * x - logCosh (0.1);
        return x >= 0.0 ? g / (1.0 - t0()) : g / (1.0 + t0());
    }
};
struct Soft   // x / (1 + |x|)
{
    static double f (double x) { return x / (1.0 + std::abs (x)); }
    static double F (double x) { const double a = std::abs (x); return a - std::log1p (a); }
};
}

// First-order ADAA for one channel. Curve gives f and F. The output is half a sample late (that is how ADAA works).
template <typename Curve>
struct Adaa1
{
    double x1 = 0.0, F1 = 0.0;
    void reset() { x1 = 0.0; F1 = 0.0; }
    float process (float xf)
    {
        const double x = xf;
        const double Fx = Curve::F (x);
        const double d = x - x1;
        double y;
        if (std::abs (d) > 1.0e-5) y = (Fx - F1) / d;
        else y = Curve::f (0.5 * (x + x1));   // nearly the same input twice: the plain curve at the midpoint
        x1 = x; F1 = Fx;
        return (float) y;
    }
};

// tanh with a bias, minus its value at rest so silence stays silence: tanh(x + b) - tanh(b), with ADAA.
// F(x) = logcosh(x + b) - tanh(b) x. The bias can move every sample (a dynamic tube).
struct AdaaAsymTanh
{
    double x1 = 0.0, b1 = 0.0;
    void reset() { x1 = 0.0; b1 = 0.0; }
    float process (float xf, float bias)
    {
        const double x = xf, b = bias, tb = std::tanh (b);
        double y;
        const double d = x - x1;
        if (std::abs (d) > 1.0e-5)
        {
            // both ends with the current bias: a bias that moves slowly does not need its own term
            const double Fx = curve::logCosh (x + b) - tb * x;
            const double Fp = curve::logCosh (x1 + b) - tb * x1;
            y = (Fx - Fp) / d;
        }
        else y = std::tanh (0.5 * (x + x1) + b) - tb;
        x1 = x; b1 = b;
        return (float) y;
    }
};

// ADAA for any memoryless curve without writing its antiderivative: the average of f over the straight line from the
// last input to this one, (F(x) - F(x1)) / (x - x1), computed with 3-point Gauss-Legendre quadrature (exact for
// polynomials up to degree 5, and very close for tanh-like curves). Costs three calls of f.
template <typename Fn>
inline float adaaAverage (Fn&& f, float x, float& x1)
{
    const float a = x1;
    x1 = x;
    const float h = 0.5f * (x - a);
    const float m = 0.5f * (x + a);
    if (std::abs (h) < 1.0e-6f) return f (m);
    const float o = 0.7745966692f * h;
    return (5.0f * f (m - o) + 8.0f * f (m) + 5.0f * f (m + o)) * (1.0f / 18.0f);
}

// a stereo pair of ADAA shapers
template <typename Curve>
struct Adaa1Stereo
{
    Adaa1<Curve> ch[2];
    void reset() { ch[0].reset(); ch[1].reset(); }
    float process (float x, int c) { return ch[c & 1].process (x); }
};

// ===================================================================== reverb
class Verb
{
public:
    // the same knobs as juce::Reverb::Parameters (0..1), so call sites keep their settings
    struct Parameters
    {
        float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.33f, dryLevel = 0.4f, width = 1.0f, freezeMode = 0.0f;
    };

    Verb() { setSampleRate (44100.0); }

    void setSampleRate (double rate)
    {
        sr = rate;
        // room for the largest size plus the modulation
        const double maxMs = baseMs[N - 1] * 1.45 + 4.0;
        for (auto& l : lines) l.prepare ((int) std::ceil (maxMs * 0.001 * sr) + 8);
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < kDiff; ++k) diff[s][k].prepare ((int) std::ceil (diffMs[k] * 1.1 * 0.001 * sr) + 4);
        for (int k = 0; k < N; ++k)
        {
            const double w = 6.283185307179586 * modRate[k] / sr;
            rotC[k] = std::cos (w); rotS[k] = std::sin (w);
        }
        modDepth = modDepthMs * 0.001 * sr;
        reset();
        updateCoefficients (true);
    }

    void setParameters (const Parameters& p)
    {
        params = p;
        updateCoefficients (false);
    }
    const Parameters& getParameters() const { return params; }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : diff) for (auto& a : d) a.clear();
        for (auto& z : absorb) z = 0.0;
        for (int k = 0; k < N; ++k) { lenNow[k] = lenTarget[k]; modC[k] = std::cos (k * 0.77); modS[k] = std::sin (k * 0.77); }
        renorm = 0;
    }

    void processStereo (float* left, float* right, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float wl, wr;
            tick (left[i], right[i], wl, wr);
            const float dl = left[i], dr = right[i];
            left[i] = wl * wet1 + wr * wet2 + dl * dry;
            right[i] = wr * wet1 + wl * wet2 + dr * dry;
        }
    }
    void processMono (float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float wl, wr;
            tick (x[i], x[i], wl, wr);
            x[i] = (wl + wr) * 0.5f * (wet1 + wet2) + x[i] * dry;
        }
    }

    // decay time (seconds, to -60 dB) the room size gives: 0 -> 0.4 s, 0.5 -> 1.5 s, 1 -> 6 s
    static double decayFor (float roomSize) { return 0.4 * std::pow (15.0, (double) std::clamp (roomSize, 0.0f, 1.0f)); }

private:
    static constexpr int N = 8, kDiff = 4;
    // line lengths in ms at medium size: mutually prime-ish, from 29 to 74 ms
    static constexpr double baseMs[N] { 29.71, 37.13, 41.11, 43.73, 53.09, 59.93, 67.27, 73.71 };
    static constexpr double diffMs[kDiff] { 4.77, 3.59, 12.73, 9.31 };

    struct Line
    {
        std::vector<float> buf;
        int w = 0, mask = 0;
        void prepare (int len)
        {
            int size = 1;
            while (size < len + 4) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        // Hermite read, delay in samples (>= 2)
        float read (double d) const
        {
            const double pos = (double) w - d;
            int i = (int) std::floor (pos);
            const float t = (float) (pos - i);
            const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)], x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }
        float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    };

    struct Allpass
    {
        Line l;
        int len = 1;
        void prepare (int maxLen) { l.prepare (maxLen); }
        void clear() { l.clear(); }
        float process (float x, float g)
        {
            const float d = l.readInt (len);
            const float v = x + d * g;
            l.push (v);
            return d - v * g;
        }
    };

    void updateCoefficients (bool snap)
    {
        const bool freeze = params.freezeMode >= 0.5f;
        const float room = std::clamp (params.roomSize, 0.0f, 1.0f);
        const double t60 = decayFor (room);
        const double size = 0.55 + 0.9 * room;   // a bigger room has longer lines too
        const double damp = std::clamp ((double) params.damping, 0.0, 1.0);
        for (int k = 0; k < N; ++k)
        {
            lenTarget[k] = baseMs[k] * size * 0.001 * sr;
            if (snap) lenNow[k] = lenTarget[k];
            // gain per pass for the lows from the decay time; the highs decay up to 8x faster with damping
            const double secs = lenTarget[k] / sr;
            const double gLow = freeze ? 1.0 : std::pow (10.0, -3.0 * secs / t60);
            const double t60High = t60 / (1.0 + 7.0 * damp * damp + 0.6 * damp);
            const double gHigh = freeze ? 1.0 : std::pow (10.0, -3.0 * secs / t60High);
            // one-pole y = b0 x + a1 y1 with gain gLow at DC and gHigh at Nyquist
            a1[k] = (gLow - gHigh) / (gLow + gHigh);
            b0[k] = gLow * (1.0 - a1[k]);
        }
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < kDiff; ++k) diff[s][k].len = std::max (1, (int) std::lround (diffMs[k] * (s == 0 ? 1.0 : 1.07) * 0.001 * sr));
        inGain = freeze ? 0.0f : 1.0f;
        // the levels follow juce::Reverb: wet up to 3x, dry up to 2x, width mixes the two sides
        // a longer tail piles up more energy: the level is evened out so the room knob changes the length more than the loudness
        const float wet = params.wetLevel * 3.0f * outScale * (float) std::pow (t60 / 1.5, -0.35);
        wet1 = 0.5f * wet * (1.0f + params.width);
        wet2 = 0.5f * wet * (1.0f - params.width);
        dry = params.dryLevel * 2.0f;
    }

    void tick (float inL, float inR, float& outL, float& outR)
    {
        // input diffusion: four allpasses per side smear the attack before it enters the tank
        float l = inL * inGain, r = inR * inGain;
        for (int k = 0; k < kDiff; ++k) { l = diff[0][k].process (l, 0.62f); r = diff[1][k].process (r, 0.62f); }

        // read the lines (slowly moving read heads keep the tail from ringing metallic)
        float o[N];
        for (int k = 0; k < N; ++k)
        {
            lenNow[k] += (lenTarget[k] - lenNow[k]) * 0.0005;
            // a rotating phasor instead of sin() per sample: eight slow sines for the price of a few multiplies
            const double c = modC[k] * rotC[k] - modS[k] * rotS[k];
            modS[k] = modS[k] * rotC[k] + modC[k] * rotS[k];
            modC[k] = c;
            const double mod = modS[k] * modDepth;
            float y = lines[k].read (lenNow[k] + mod);
            // absorption: one pole, so lows and highs decay at their own rates
            absorb[k] = b0[k] * y + a1[k] * absorb[k];
            if (std::abs (absorb[k]) < 1.0e-20) absorb[k] = 0.0;
            o[k] = (float) absorb[k];
        }
        if (++renorm >= 4096)   // keep the phasors on the unit circle
        {
            renorm = 0;
            for (int k = 0; k < N; ++k) { const double m = 1.0 / std::sqrt (modC[k] * modC[k] + modS[k] * modS[k]); modC[k] *= m; modS[k] *= m; }
        }
        // outputs: two orthogonal sign patterns of the lines
        outL = (o[0] - o[1] + o[2] + o[3] - o[4] + o[5] - o[6] + o[7]) * 0.35355339f;
        outR = (o[0] + o[1] - o[2] + o[3] + o[4] - o[5] - o[6] - o[7]) * 0.35355339f;
        // Hadamard 8x8 (fast Walsh transform), scaled to stay lossless
        float h[N];
        for (int k = 0; k < N; ++k) h[k] = o[k];
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = h[j], b = h[j + len];
                    h[j] = a + b; h[j + len] = a - b;
                }
        // left feeds the even lines, right the odd ones
        for (int k = 0; k < N; ++k) lines[k].push (h[k] * 0.35355339f + ((k & 1) ? r : l) * 0.5f);
    }

    double sr = 44100.0;
    Parameters params;
    Line lines[N];
    Allpass diff[2][kDiff];
    double lenNow[N] {}, lenTarget[N] {}, a1[N] {}, b0[N] {}, absorb[N] {}, modC[N] {}, modS[N] {}, rotC[N] {}, rotS[N] {}, modDepth = 10.0;
    int renorm = 0;
    static constexpr double modRate[N] { 0.11, 0.17, 0.13, 0.23, 0.19, 0.29, 0.07, 0.31 };
    static constexpr double modDepthMs = 0.25;
    static constexpr float outScale = 1.17f;   // about as loud as juce::Reverb with the same knobs (measured with noise)
    float wet1 = 0.5f, wet2 = 0.0f, dry = 0.0f, inGain = 1.0f;
};

// ------------------------------------------------------------------ windowed-sinc reading for samplers
// Reads a sample at a fractional position with a 32-tap Kaiser-windowed sinc (beta 9), 512 phases with a
// linear blend between neighbouring phases. When the read runs faster than the output (a note pitched up),
// the kernel's cutoff drops with the speed, so the copies above Nyquist do not fold back as metallic tones:
// five cutoffs are prepared (1, 0.71, 0.5, 0.35 and 0.25 of Nyquist). At speed 1 and an integer position the
// output is the sample itself, bit for bit. Call SincTable::get() once outside the audio thread (prepare).
class SincTable
{
public:
    static constexpr int half = 16, taps = 2 * half, phases = 512, bands = 5;

    static const SincTable& get() { static const SincTable t; return t; }

    // which kernel to use for a given read speed (|frames of source per output frame|)
    static int bandFor (double speed)
    {
        if (speed <= 1.0) return 0;
        const int b = (int) std::ceil (2.0 * std::log2 (speed) - 0.3);
        return std::clamp (b, 0, bands - 1);
    }

    // one channel; outside [0, len) the sample is silence
    float read (const float* d, int len, double pos, int band) const
    {
        float a;
        read2 (d, nullptr, len, pos, band, a, a);
        return a;
    }

    // two channels with the same kernel (b may be null)
    void read2 (const float* a, const float* b, int len, double pos, int band, float& outA, float& outB) const
    {
        if (! (pos > -1.0e9 && pos < 1.0e9) || len <= 0) { outA = outB = 0.0f; return; }   // NaN, inf or nothing to read
        band = std::clamp (band, 0, bands - 1);
        const double fl = std::floor (pos);
        const int i = (int) fl;
        const double fr = (pos - fl) * phases;
        const int ph = std::min (phases - 1, (int) fr);
        const float t = (float) (fr - ph);
        const float* k0 = &tbl[((size_t) band * (phases + 1) + (size_t) ph) * taps];
        const float* k1 = k0 + taps;
        const int first = i - (half - 1);
        float sa = 0.0f, sb = 0.0f;
        if (first >= 0 && first + taps <= len)
        {
            const float* pa = a + first;
            const float* pb = b != nullptr ? b + first : nullptr;
            for (int j = 0; j < taps; ++j)
            {
                const float k = k0[j] + t * (k1[j] - k0[j]);
                sa += pa[j] * k;
                if (pb != nullptr) sb += pb[j] * k;
            }
        }
        else
        {
            const int j0 = std::max (0, -first), j1 = std::min (taps, len - first);
            for (int j = j0; j < j1; ++j)
            {
                const float k = k0[j] + t * (k1[j] - k0[j]);
                sa += a[first + j] * k;
                if (b != nullptr) sb += b[first + j] * k;
            }
        }
        outA = sa;
        outB = b != nullptr ? sb : sa;
    }

private:
    std::vector<float> tbl;

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < 1.0e-12 * sum) break; }
        return sum;
    }

    SincTable()
    {
        tbl.resize ((size_t) bands * (phases + 1) * taps);
        const double beta = 9.0, i0b = besselI0 (beta), pi = 3.14159265358979323846;
        for (int band = 0; band < bands; ++band)
        {
            const double cut = std::pow (2.0, -0.5 * band);
            for (int ph = 0; ph <= phases; ++ph)
            {
                const double f = (double) ph / phases;
                float* row = &tbl[((size_t) band * (phases + 1) + (size_t) ph) * taps];
                double sum = 0.0;
                for (int j = 0; j < taps; ++j)
                {
                    const double x = (double) (j - (half - 1)) - f;          // distance from the read point
                    const double sx = cut * x;
                    // exact zeros where sinc crosses zero (sin(pi n) in doubles is 1e-16, not 0: it would leak into silence)
                    const bool zero = std::abs (sx - std::round (sx)) < 1.0e-9;
                    const double sinc = std::abs (sx) < 1.0e-9 ? 1.0 : (zero ? 0.0 : std::sin (pi * sx) / (pi * sx));
                    const double r = x / (double) half;
                    const double w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                    row[j] = (float) (cut * sinc * w);
                    sum += row[j];
                }
                for (int j = 0; j < taps; ++j) row[j] = (float) (row[j] / sum);   // DC passes at exactly 1
            }
        }
    }
};

// equal-power crossfade gains for t in [0, 1]: out = a * fadeOut + b * fadeIn
inline void equalPower (float t, float& fadeOut, float& fadeIn)
{
    t = std::clamp (t, 0.0f, 1.0f);
    fadeOut = std::cos (t * 1.5707963f);
    fadeIn  = std::sin (t * 1.5707963f);
}
}

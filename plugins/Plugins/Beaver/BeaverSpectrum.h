#pragma once
#include "BeaverAnalysis.h"

// The partial levels of one frame: shapes, drawn bars, materials, and everything that bends them before they sound.
// Pure functions on arrays of 511 levels (index 0 = partial 1), nothing allocated.
namespace racoon::beaver
{
constexpr int numShapes = 8, numBars = 64;

inline float dbToLin (float db) { return db <= -119.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float linToDb (float a) { return a <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (a); }
inline uint32_t hash (uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

// a three-formant bump, used by the dark vowel and the vowel filter (dB)
inline float vowelDb (float f, float vowel, float scale)
{
    static const float F[5][3] { { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
    vowel = juce::jlimit (0.0f, 4.0f, vowel);
    const int a = juce::jmin (3, (int) vowel); const float t = vowel - (float) a;
    float lin = 0.003f;
    for (int b = 0; b < 3; ++b)
    {
        const float fc = (F[a][b] * (1.0f - t) + F[a + 1][b] * t) * scale;
        const float d = std::log2 (juce::jmax (1.0f, f) / fc) * 3.2f;
        lin += std::exp (-d * d) / (float) (1 + 2 * b);
    }
    return linToDb (lin);
}

// the eight shapes, swept by pos (0..1); f0 is needed by the ones with formants (they stay put in Hz)
inline void shape (int id, float pos, float f0, float* a)
{
    pos = juce::jlimit (0.0f, 1.0f, pos);
    for (int i = 0; i < numPartials; ++i)
    {
        const float k = (float) (i + 1);
        const bool odd = ((i + 1) & 1) != 0;
        float v = 0.0f;
        switch (id)
        {
            case 0: { const float kc = 511.0f * std::pow (24.0f / 511.0f, pos); const float r = k / kc; v = 1.0f / k / std::sqrt (1.0f + r * r * r * r * r * r * r * r); break; }
            case 1: v = odd ? 1.0f / k : pos / k; break;
            case 2: v = odd ? std::pow (k, -(2.0f - 0.5f * pos)) : pos * std::pow (k, -1.5f); break;
            case 3:
            {
                static const int bars[6] { 1, 2, 3, 4, 6, 8 };
                const float w[6] { 1.0f, 0.7f, 0.5f * (0.3f + 0.7f * pos), 0.4f * pos, 0.3f * pos, 0.25f * pos };
                for (int b = 0; b < 6; ++b) if (bars[b] == i + 1) v = w[b];
                break;
            }
            case 4: { const float vw = pos * 4.0f; const float tri = vw <= 2.0f ? vw : 4.0f - vw; v = std::pow (k, -0.5f) * dbToLin (vowelDb (k * f0, tri, 0.8f)); break; }
            case 5: { const float w = 0.5f - 0.45f * pos; v = std::abs (std::sin (k * juce::MathConstants<float>::pi * w)) / k; break; }
            case 6:
            {
                const float jit = (float) (hash ((uint32_t) (i * 7919 + (int) (pos * 7.0f) * 104729)) & 0xffff) / 65535.0f;
                const float d = std::log2 (juce::jmax (1.0f, k * f0) / 500.0f) * 1.1f;
                v = std::pow (k, -1.2f) * (0.25f + std::exp (-d * d)) * (0.5f + jit);
                break;
            }
            default: v = i == 0 ? 1.0f : (i < 3 ? 0.126f * pos : 0.0f); break;
        }
        a[i] = v;
    }
}

// drawn bars (dB, 64 of them); above 64 the average of the top sixteen keeps falling 6 dB per octave
inline void drawn (const float* barsDb, float* a)
{
    float top = 0.0f;
    for (int b = 48; b < 64; ++b) top += barsDb[b];
    top /= 16.0f;
    for (int i = 0; i < numPartials; ++i)
        a[i] = i < numBars ? dbToLin (barsDb[i] <= -59.5f ? -120.0f : barsDb[i]) : dbToLin (top - 6.0f * std::log2 ((float) (i + 1) / 64.0f));
}

// a material's frame (fractional, interpolated in dB) at the note: `ratio` is the source partial per note partial
// (k * ratio). With formants kept, ratio = f0 / (fRef * 2^(formant / 12)); without, ratio = 2^(-tono / 12)
inline void material (const FrameSet& fs, float frame, float ratio, float* a)
{
    const int f0i = juce::jlimit (0, fs.frames - 1, (int) frame);
    const int f1i = juce::jmin (fs.frames - 1, f0i + 1);
    const float t = juce::jlimit (0.0f, 1.0f, frame - (float) f0i);
    const uint8_t* L0 = fs.level.data() + (size_t) f0i * numPartials;
    const uint8_t* L1 = fs.level.data() + (size_t) f1i * numPartials;
    for (int i = 0; i < numPartials; ++i)
    {
        const float s = (float) (i + 1) * ratio - 1.0f;      // source index
        if (s < -0.5f || s > (float) numPartials - 1.0f) { a[i] = 0.0f; continue; }
        const int s0 = juce::jlimit (0, numPartials - 1, (int) std::floor (s));
        const int s1 = juce::jmin (numPartials - 1, s0 + 1);
        const float u = juce::jlimit (0.0f, 1.0f, s - (float) s0);
        const float d0 = FrameSet::dq (L0[s0]) + (FrameSet::dq (L0[s1]) - FrameSet::dq (L0[s0])) * u;
        const float d1 = FrameSet::dq (L1[s0]) + (FrameSet::dq (L1[s1]) - FrameSet::dq (L1[s0])) * u;
        const float db = d0 + (d1 - d0) * t;
        a[i] = db <= -95.0f ? 0.0f : dbToLin (db);
    }
}

// the harmonizer's patterns: which partials stay
inline bool harmonizerKeeps (int pattern, int k)
{
    switch (pattern)
    {
        case 1: return (k & (k - 1)) == 0;                                            // octaves
        case 2: { int m = k; while (m % 2 == 0) m /= 2; return m == 1 || m == 3; }     // octaves and fifths
        case 3: return (k & 1) != 0;                                                  // odd
        case 4: { static const int o[] { 1, 2, 3, 4, 6, 8, 10, 12, 16 }; for (int v : o) if (v == k) return true; return false; }
        case 5: { if (k == 1 || k == 2) return true; if (k % 2 == 0) return false; for (int d = 3; d * d <= k; d += 2) if (k % d == 0) return false; return true; }
        default: return true;
    }
}

// one partial filter as a gain curve in dB
struct FilterCurve
{
    int type = 0;          // 0 paso bajo, 1 paso banda, 2 paso alto, 3 rechazo, 4 peine, 5 vocal, 6 escalón
    float fc = 1000.0f, slope = 24.0f, res = 0.0f, width = 1.0f, vowel = 1.0f;
    inline float db (float f) const
    {
        const float o = std::log2 (juce::jmax (1.0f, f) / fc);
        const float bump = res * 24.0f * std::exp (-(o * o) / (2.0f * 0.08f * 0.08f));
        switch (type)
        {
            case 0: return (o > 0.0f ? -slope * o : 0.0f) + bump;
            case 1: return -slope * std::abs (o) + bump;
            case 2: return (o < 0.0f ? slope * o : 0.0f) + bump;
            case 3: return -slope * juce::jmax (0.0f, 1.0f - std::abs (o) / juce::jmax (0.05f, width));
            case 4: return -slope / 6.0f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * f / fc)) * 0.5f;
            case 5: return vowelDb (f * 1000.0f / fc, vowel, 0.85f) + 6.0f;
            default: return o > 0.0f ? -120.0f : 0.0f;
        }
    }
};

// a Gaussian across partials, in dB, edge-corrected
inline void blurAcross (float* a, float sigma, float* scratchDb, float* scratchOut)
{
    if (sigma < 0.3f) return;
    for (int i = 0; i < numPartials; ++i) scratchDb[i] = linToDb (a[i]);
    const int half = juce::jmin (36, (int) std::ceil (sigma * 3.0f));
    float w[73];
    for (int j = -half; j <= half; ++j) w[j + half] = std::exp (-(float) (j * j) / (2.0f * sigma * sigma));
    for (int i = 0; i < numPartials; ++i)
    {
        float s = 0.0f, ws = 0.0f;
        for (int j = -half; j <= half; ++j)
        {
            const int k = i + j;
            if (k < 0 || k >= numPartials) continue;
            s += scratchDb[k] * w[j + half]; ws += w[j + half];
        }
        scratchOut[i] = s / ws;
    }
    // the cloud keeps the energy the partials had (an average in dB alone would sink it)
    double before = 0.0, after = 0.0;
    for (int i = 0; i < numPartials; ++i) before += (double) a[i] * a[i];
    for (int i = 0; i < numPartials; ++i)
    {
        a[i] = (scratchDb[i] <= -119.0f || scratchOut[i] <= -119.0f) ? 0.0f : dbToLin (scratchOut[i]);
        after += (double) a[i] * a[i];
    }
    if (after > 1.0e-20)
    {
        const float g = (float) std::sqrt (before / after);
        for (int i = 0; i < numPartials; ++i) a[i] *= g;
    }
}
}

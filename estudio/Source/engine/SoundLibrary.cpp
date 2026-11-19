#include "SoundLibrary.h"
#include "Genres.h"
#include <cmath>
#include <map>
#include <thread>

// Every sound in the library is made from oscillators, filtered noise and envelopes, with a fixed seed per
// sound, so the same version of Racoon always writes exactly the same files.
namespace racoon::library
{
using namespace juce;

namespace
{
constexpr double SR = 44100.0;
constexpr int kVersion = 3;               // bump when the sounds change: the library is written again
constexpr float kPi = MathConstants<float>::pi, kTwoPi = MathConstants<float>::twoPi;

using Mono = std::vector<float>;
struct Stereo { Mono l, r; };

// --------------------------------------------------------------------------------------------- basics
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed != 0 ? seed : 0x9e3779b9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
    float in (float a, float b) { return a + (b - a) * uni(); }
    bool chance (float p) { return uni() < p; }
};

// zero-delay state variable filter (Simper)
struct Svf
{
    enum Mode { lp, bp, hp };
    float ic1 = 0.0f, ic2 = 0.0f;
    float run (float x, float fc, float q, Mode m)
    {
        fc = jlimit (20.0f, (float) (SR * 0.45), fc);
        const float g = std::tan (kPi * fc / (float) SR), k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m == lp ? v2 : m == bp ? v1 : x - k * v1 - v2;
    }
};

// fixed-frequency filter: the coefficient is computed once
struct FixedSvf
{
    Svf::Mode mode;
    float a1, a2, a3, k, ic1 = 0.0f, ic2 = 0.0f;
    FixedSvf (float fc, float q, Svf::Mode m) : mode (m)
    {
        fc = jlimit (20.0f, (float) (SR * 0.45), fc);
        const float g = std::tan (kPi * fc / (float) SR);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float run (float x)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return mode == Svf::lp ? v2 : mode == Svf::bp ? v1 : x - k * v1 - v2;
    }
};

Mono make (double secs) { return Mono ((size_t) std::max (1.0, secs * SR), 0.0f); }
inline float decay (float t, float time60) { return std::exp (-t * 6.9f / std::max (0.0005f, time60)); }
inline float sat (float x, float amt) { if (amt <= 0.0f) return x; const float d = 1.0f + amt * 3.0f; return std::tanh (x * d) / std::tanh (d); }

void fadeTail (Mono& m, double secs = 0.006)
{
    const int n = std::min ((int) m.size(), (int) (secs * SR));
    for (int i = 0; i < n; ++i) m[m.size() - 1 - (size_t) i] *= (float) i / (float) n;
}

void trimSilence (Mono& m)
{
    // stop where the sound has gone below -70 dB for good
    size_t end = m.size();
    while (end > 64 && std::abs (m[end - 1]) < 0.0003f) --end;
    m.resize (std::min (m.size(), end + 32));
    fadeTail (m, 0.004);
}

float peakOf (const Mono& m) { float p = 0.0f; for (auto v : m) p = std::max (p, std::abs (v)); return p; }

void normalise (Mono& m, float target = 0.89f)
{
    const float p = peakOf (m);
    if (p > 1.0e-6f) for (auto& v : m) v *= target / p;
}

void normalise (Stereo& s, float target = 0.89f)
{
    const float p = std::max (peakOf (s.l), peakOf (s.r));
    if (p > 1.0e-6f) { for (auto& v : s.l) v *= target / p; for (auto& v : s.r) v *= target / p; }
}

// lo-fi: fewer bits, a lower sample rate and a dull top
void crush (Mono& m, float amount)
{
    if (amount <= 0.0f) return;
    const float steps = std::pow (2.0f, 13.0f - 5.0f * amount);
    const int hold = 1 + (int) std::round (amount * 2.0f);
    FixedSvf lp (9000.0f - 4000.0f * amount, 0.7f, Svf::lp);
    float held = 0.0f;
    for (size_t i = 0; i < m.size(); ++i)
    {
        if (i % (size_t) hold == 0) held = std::round (m[i] * steps) / steps;
        m[i] = lp.run (held);
    }
}

// small stereo room (Freeverb shape) used for percussion air, impacts and loop glue
Stereo room (const Mono& in, float size, float damp, float wet, double tailSecs)
{
    static const int combL[] { 1116, 1188, 1277, 1356, 1422, 1491 };
    static const int apL[] { 556, 441, 341 };
    Stereo out;
    const size_t n = in.size() + (size_t) (tailSecs * SR);
    out.l.assign (n, 0.0f);
    out.r.assign (n, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& dst = ch == 0 ? out.l : out.r;
        std::vector<std::vector<float>> combs;
        std::vector<float> lowState (6, 0.0f);
        std::vector<size_t> idx (6, 0);
        for (int c = 0; c < 6; ++c) combs.emplace_back ((size_t) std::max (8, (int) ((float) (combL[c] + ch * 23) * size)), 0.0f);
        std::vector<std::vector<float>> aps;
        std::vector<size_t> aidx (3, 0);
        for (int a = 0; a < 3; ++a) aps.emplace_back ((size_t) (apL[a] + ch * 23), 0.0f);
        const float fb = 0.70f + 0.26f * std::min (1.0f, size * 0.6f);
        for (size_t i = 0; i < n; ++i)
        {
            const float x = i < in.size() ? in[i] * 0.03f : 0.0f;
            float s = 0.0f;
            for (int c = 0; c < 6; ++c)
            {
                auto& buf = combs[(size_t) c];
                const float y = buf[idx[(size_t) c]];
                lowState[(size_t) c] = y * (1.0f - damp) + lowState[(size_t) c] * damp;
                buf[idx[(size_t) c]] = x + lowState[(size_t) c] * fb;
                idx[(size_t) c] = (idx[(size_t) c] + 1) % buf.size();
                s += y;
            }
            for (int a = 0; a < 3; ++a)
            {
                auto& buf = aps[(size_t) a];
                const float b = buf[aidx[(size_t) a]];
                buf[aidx[(size_t) a]] = s + b * 0.5f;
                aidx[(size_t) a] = (aidx[(size_t) a] + 1) % buf.size();
                s = b - s;
            }
            dst[i] = (i < in.size() ? in[i] : 0.0f) * (1.0f - wet * 0.3f) + s * wet;
        }
    }
    return out;
}

Mono toMono (const Stereo& s)
{
    Mono m (s.l.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.l[i] + s.r[i]);
    return m;
}

// --------------------------------------------------------------------------------------------- voices
struct KickP  { float f0 = 260, f1 = 50, pitchT = 0.035f, dec = 0.45f, hold = 0.02f, click = 0.4f, clickHz = 4000, drive = 0.5f, noise = 0.0f, len = 0.9f, harm = 0.12f; };
struct SnareP { float f = 190, bodyDec = 0.12f, noiseDec = 0.22f, hp = 1800, lp = 9000, noise = 0.8f, body = 0.7f, drive = 0.3f, snap = 0.3f, len = 0.6f; };
struct ClapP  { int bursts = 4; float spacing = 0.011f, tail = 0.25f, freq = 1200, q = 1.1f, drive = 0.2f, len = 0.7f; };
struct HatP   { float dec = 0.06f, scale = 1.0f, hp = 7000, bp = 10000, noise = 0.4f, metal = 0.8f, len = 0.3f, attack = 0.0004f; };
struct DrumP  { float f = 200, drop = 0.15f, dropT = 0.012f, dec = 0.3f, overtone = 1.5f, otAmt = 0.25f, slap = 0.0f, noise = 0.05f, len = 0.6f; };

Mono kick (const KickP& p, Rng& r)
{
    auto out = make (p.len);
    float ph = 0.0f;
    FixedSvf clickF (p.clickHz, 0.8f, Svf::bp), noiseF (1400.0f, 0.7f, Svf::lp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        const float f = p.f1 + (p.f0 - p.f1) * std::exp (-t / p.pitchT);
        ph += kTwoPi * f / (float) SR;
        if (ph > kTwoPi) ph -= kTwoPi;
        const float amp = t < 0.0012f ? t / 0.0012f : (t < p.hold ? 1.0f : decay (t - p.hold, p.dec));
        const float body = (std::sin (ph) + p.harm * std::sin (2.0f * ph) * std::exp (-t / 0.04f)) * amp;
        const float click = p.click * 2.2f * clickF.run (r.bi()) * std::exp (-t / 0.0035f);
        const float nz = p.noise * noiseF.run (r.bi()) * std::exp (-t / 0.06f);
        out[i] = sat (body + click + nz, p.drive);
    }
    trimSilence (out);
    return out;
}

Mono snare (const SnareP& p, Rng& r)
{
    auto out = make (p.len);
    float ph1 = 0.0f, ph2 = 0.0f;
    FixedSvf hp (p.hp, 0.7f, Svf::hp), lp (p.lp, 0.7f, Svf::lp), snapF (5200.0f, 1.0f, Svf::bp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        const float bend = 1.0f + 0.35f * std::exp (-t / 0.012f);
        ph1 += kTwoPi * p.f * bend / (float) SR;
        ph2 += kTwoPi * p.f * 1.62f * bend / (float) SR;
        const float body = (std::sin (ph1) + 0.5f * std::sin (ph2)) * decay (t, p.bodyDec);
        const float att = std::min (1.0f, t / 0.001f);
        const float nz = lp.run (hp.run (r.bi())) * att * decay (t, p.noiseDec);
        const float sn = snapF.run (r.bi()) * std::exp (-t / 0.006f) * p.snap * 2.0f;
        out[i] = sat (body * p.body + nz * p.noise * 1.6f + sn, p.drive);
    }
    trimSilence (out);
    return out;
}

Mono clap (const ClapP& p, Rng& r)
{
    auto out = make (p.len);
    FixedSvf bp (p.freq, p.q, Svf::bp), hp (600.0f, 0.7f, Svf::hp);
    const float last = p.spacing * (float) (p.bursts - 1);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float env = 0.0f;
        for (int k = 0; k < p.bursts; ++k)
        {
            const float tk = p.spacing * (float) k * (1.0f + 0.08f * (float) ((k * 7) % 3 - 1));
            if (t >= tk) env = std::max (env, std::exp (-(t - tk) / 0.0032f) * (k == p.bursts - 1 ? 1.0f : 0.85f));
        }
        if (t >= last) env = std::max (env, 0.75f * decay (t - last, p.tail));
        out[i] = sat (hp.run (bp.run (r.bi()) * env * 3.0f), p.drive);
    }
    trimSilence (out);
    return out;
}

// six square waves at the ratios of the classic analog cymbal, through filters
struct Metal
{
    float ph[6] {}, inc[6] {};
    explicit Metal (float scale, Rng& r)
    {
        static const float base[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        for (int k = 0; k < 6; ++k) { inc[k] = base[k] * scale * 1.6f / (float) SR; ph[k] = r.uni(); }
    }
    float next()
    {
        float s = 0.0f;
        for (int k = 0; k < 6; ++k) { ph[k] += inc[k]; if (ph[k] >= 1.0f) ph[k] -= 1.0f; s += ph[k] < 0.5f ? 1.0f : -1.0f; }
        return s / 6.0f;
    }
};

Mono hat (const HatP& p, Rng& r)
{
    auto out = make (p.len);
    Metal m (p.scale, r);
    FixedSvf hp (p.hp, 0.7f, Svf::hp), bp (p.bp, 0.9f, Svf::bp), hp2 (p.hp * 0.8f, 0.7f, Svf::hp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        const float src = p.metal * m.next() + p.noise * r.bi();
        const float env = std::min (1.0f, t / p.attack) * decay (t, p.dec);
        const float filtered = hp.run (src) * 0.6f + bp.run (hp2.run (src)) * 0.9f;
        out[i] = filtered * env * 2.2f;
    }
    trimSilence (out);
    return out;
}

Mono ride (float scale, float dec, Rng& r)
{
    auto out = make (dec + 0.2);
    Metal m (scale * 0.85f, r);
    FixedSvf hp (4200.0f, 0.7f, Svf::hp), bp (7500.0f * scale, 1.2f, Svf::bp);
    float b1 = r.uni(), b2 = r.uni();
    const float bf1 = 3150.0f * scale, bf2 = 4720.0f * scale;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        const float metal = hp.run (m.next() * 0.8f + r.bi() * 0.25f);
        b1 += bf1 / (float) SR; b2 += bf2 / (float) SR;
        const float bell = (std::sin (kTwoPi * b1) + 0.6f * std::sin (kTwoPi * b2)) * decay (t, dec * 0.55f) * 0.22f;
        out[i] = (metal * decay (t, dec) + bp.run (metal) * decay (t, 0.12f) * 0.8f) * 1.4f + bell;
    }
    trimSilence (out);
    return out;
}

Stereo crash (float scale, float dec, Rng& r)
{
    Stereo s;
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rc (r.next());
        auto m = make (dec + 0.3);
        Metal metal (scale, rc);
        FixedSvf hp (3200.0f, 0.7f, Svf::hp), lp (12500.0f, 0.7f, Svf::lp);
        for (size_t i = 0; i < m.size(); ++i)
        {
            const float t = (float) i / (float) SR;
            const float env = std::min (1.0f, t / 0.002f) * decay (t, dec) * (0.6f + 0.4f * std::exp (-t / 0.08f));
            m[i] = lp.run (hp.run (rc.bi() * 0.8f + metal.next() * 0.5f)) * env * 1.6f;
        }
        (ch == 0 ? s.l : s.r) = std::move (m);
    }
    return s;
}

Mono rim (float f, float dec, Rng& r)
{
    auto out = make (0.25);
    float ph = 0.0f, ph2 = 0.0f;
    FixedSvf bp (3100.0f, 1.4f, Svf::bp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        ph += kTwoPi * f / (float) SR; ph2 += kTwoPi * f * 1.83f / (float) SR;
        const float body = (std::sin (ph) + 0.4f * std::sin (ph2)) * decay (t, dec);
        const float click = bp.run (r.bi()) * decay (t, 0.018f) * 1.8f;
        out[i] = sat (body + click, 0.3f);
    }
    trimSilence (out);
    return out;
}

Mono shaker (float f, float attack, float dec, int grains, float gap, Rng& r)
{
    auto out = make (attack + dec + gap * (float) grains + 0.1f);
    FixedSvf bp (f, 1.3f, Svf::bp), hp (3000.0f, 0.7f, Svf::hp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float env = 0.0f;
        for (int g = 0; g < grains; ++g)
        {
            const float tg = t - gap * (float) g;
            if (tg < 0.0f) continue;
            const float e = tg < attack ? std::pow (tg / attack, 1.5f) : decay (tg - attack, dec);
            env = std::max (env, e * (g == grains - 1 ? 1.0f : 0.65f));
        }
        out[i] = hp.run (bp.run (r.bi())) * env * 2.5f;
    }
    trimSilence (out);
    return out;
}

Mono skinDrum (const DrumP& p, Rng& r)
{
    auto out = make (p.len);
    float ph = 0.0f, ph2 = 0.0f;
    FixedSvf slapF (2600.0f, 0.9f, Svf::bp), nzF (900.0f, 0.7f, Svf::lp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        const float bend = 1.0f + p.drop * std::exp (-t / p.dropT);
        ph += kTwoPi * p.f * bend / (float) SR;
        ph2 += kTwoPi * p.f * p.overtone * bend / (float) SR;
        const float body = std::sin (ph) * decay (t, p.dec) + p.otAmt * std::sin (ph2) * decay (t, p.dec * 0.35f);
        const float slap = p.slap * slapF.run (r.bi()) * std::exp (-t / 0.008f) * 2.5f;
        const float nz = p.noise * nzF.run (r.bi()) * std::exp (-t / 0.03f);
        out[i] = sat (body * std::min (1.0f, t / 0.0008f) + slap + nz, 0.25f);
    }
    trimSilence (out);
    return out;
}

Mono tambourine (float dec, int hits, Rng& r)
{
    auto out = make (dec + 0.06f * (float) hits + 0.2f);
    float ph[7], fr[7], dk[7];
    for (int k = 0; k < 7; ++k) { ph[k] = r.uni(); fr[k] = r.in (5200.0f, 11000.0f); dk[k] = r.in (0.6f, 1.0f) * dec; }
    FixedSvf hp (7500.0f, 0.7f, Svf::hp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float env = 0.0f, envK[7] {};
        for (int h = 0; h < hits; ++h)
        {
            const float th = t - 0.055f * (float) h;
            if (th < 0.0f) continue;
            env = std::max (env, decay (th, dec * 0.6f) * (h == 0 ? 1.0f : 0.7f));
            for (int k = 0; k < 7; ++k) envK[k] = std::max (envK[k], decay (th, dk[k]));
        }
        float jingle = 0.0f;
        for (int k = 0; k < 7; ++k) { ph[k] += fr[k] / (float) SR; jingle += std::sin (kTwoPi * ph[k]) * envK[k]; }
        out[i] = (jingle * 0.18f + hp.run (r.bi()) * env * 0.9f) * std::min (1.0f, t / 0.001f);
    }
    trimSilence (out);
    return out;
}

Mono cowbell (float f, Rng& r)
{
    auto out = make (0.5);
    float a = r.uni(), b = r.uni();
    FixedSvf bp (f * 1.65f, 2.2f, Svf::bp);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        a += f / (float) SR; b += f * 1.48f / (float) SR;
        a -= std::floor (a); b -= std::floor (b);
        const float sq = (a < 0.5f ? 1.0f : -1.0f) + (b < 0.5f ? 1.0f : -1.0f);
        const float env = 0.6f * std::exp (-t / 0.012f) + 0.4f * decay (t, 0.32f);
        out[i] = bp.run (sq) * env * 1.6f;
    }
    trimSilence (out);
    return out;
}

Mono woodTone (float f, float ratio, float dec, Rng& r)
{
    auto out = make (dec + 0.1);
    float ph = r.uni(), ph2 = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        ph += kTwoPi * f / (float) SR; ph2 += kTwoPi * f * ratio / (float) SR;
        out[i] = (std::sin (ph) + 0.35f * std::sin (ph2) * std::exp (-t / (dec * 0.3f))) * decay (t, dec) * std::min (1.0f, t / 0.0005f);
    }
    trimSilence (out);
    return out;
}

Mono bass808 (float hz, float dec, float drive, Rng& r)
{
    KickP p;
    p.f0 = hz * 2.6f; p.f1 = hz; p.pitchT = 0.018f; p.dec = dec; p.hold = 0.05f; p.click = 0.25f; p.clickHz = 2500.0f;
    p.drive = drive; p.len = dec + 0.3f; p.harm = 0.2f;
    return kick (p, r);
}

Mono pluckBass (float hz, float cutoff, float dec, Rng& r)
{
    auto out = make (dec + 0.2);
    float ph1 = r.uni(), ph2 = r.uni();
    Svf lp;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        ph1 += hz / (float) SR; ph2 += hz * 1.004f / (float) SR;
        ph1 -= std::floor (ph1); ph2 -= std::floor (ph2);
        const float saw = (2.0f * ph1 - 1.0f) + (2.0f * ph2 - 1.0f);
        const float fc = hz * 1.5f + cutoff * std::exp (-t / 0.09f);
        out[i] = sat (lp.run (saw, fc, 1.6f, Svf::lp) * decay (t, dec) * std::min (1.0f, t / 0.002f), 0.4f);
    }
    trimSilence (out);
    return out;
}

// --------------------------------------------------------------------------------------------- transitions
Stereo riser (float secs, float fromHz, float toHz, bool down, Rng& r)
{
    Mono m = make (secs);
    Svf bp;
    float ph = 0.0f;
    for (size_t i = 0; i < m.size(); ++i)
    {
        float x = (float) i / (float) m.size();
        if (down) x = 1.0f - x;
        const float fc = fromHz * std::pow (toHz / fromHz, x);
        ph += kTwoPi * (180.0f * std::pow (8.0f, x)) / (float) SR;
        const float amp = down ? std::pow (x, 0.7f) : std::pow (x, 1.6f);
        m[i] = (bp.run (r.bi(), fc, 2.5f, Svf::bp) * 1.4f + std::sin (ph) * 0.18f) * amp;
    }
    fadeTail (m, 0.02);
    return room (m, 1.2f, 0.4f, 0.35f, 0.6);
}

Stereo impact (Rng& r)
{
    KickP k; k.f0 = 160; k.f1 = 38; k.pitchT = 0.09f; k.dec = 1.6f; k.hold = 0.05f; k.click = 0.6f; k.drive = 0.7f; k.len = 2.0f;
    auto boom = kick (k, r);
    Mono nz = make (1.2);
    FixedSvf lp (2500.0f, 0.7f, Svf::lp);
    for (size_t i = 0; i < nz.size(); ++i) nz[i] = lp.run (r.bi()) * decay ((float) i / (float) SR, 0.9f) * 0.5f;
    boom.resize (std::max (boom.size(), nz.size()), 0.0f);
    for (size_t i = 0; i < nz.size(); ++i) boom[i] += nz[i];
    return room (boom, 1.6f, 0.3f, 0.5f, 2.5);
}

Stereo reversed (Stereo s)
{
    std::reverse (s.l.begin(), s.l.end());
    std::reverse (s.r.begin(), s.r.end());
    return s;
}

// --------------------------------------------------------------------------------------------- a sung voice
// A soft "aah / ooh" made from a pulse and three formants, with a little vibrato and breath: the example vocal of the
// block, so Chinchilla has a voice to tune and harmonise the first time Racoon opens.
struct SungNote { double start, length; int pitch; int vowel; };

Stereo sungVoice (const std::vector<SungNote>& notes, double bpm, double totalBeats, Rng& r)
{
    static const float formants[5][3] { { 800, 1150, 2900 }, { 450, 1900, 2600 }, { 320, 2300, 3000 }, { 500, 850, 2800 }, { 340, 720, 2500 } };
    const double spb = 60.0 / bpm;
    Mono out = make (totalBeats * spb);
    float ph = 0.0f, vib = 0.0f, freq = 220.0f, lp = 0.0f;
    float bp[3][2] {};
    float f[3] { 800, 1150, 2900 };
    float env = 0.0f;
    size_t ni = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double beat = (double) i / SR / spb;
        while (ni + 1 < notes.size() && beat >= notes[ni + 1].start) ++ni;
        const auto& n = notes[ni];
        const bool on = beat >= n.start && beat < n.start + n.length;
        const float target = 440.0f * std::pow (2.0f, (float) (n.pitch - 69) / 12.0f);
        freq += (target - freq) * 0.0015f;                       // a short glide between notes
        const float age = (float) ((beat - n.start) * spb);
        vib += kTwoPi * 5.3f / (float) SR;
        const float vibDepth = on ? 0.006f * jlimit (0.0f, 1.0f, (age - 0.2f) * 2.5f) : 0.0f;
        ph += freq * (1.0f + vibDepth * std::sin (vib)) / (float) SR;
        if (ph >= 1.0f) ph -= 1.0f;
        const float envTarget = on ? 1.0f : 0.0f;
        env += (envTarget - env) * (on ? 0.0012f : 0.0004f);
        // the vowel moves smoothly to the next one
        for (int k = 0; k < 3; ++k) f[k] += (formants[n.vowel % 5][k] - f[k]) * 0.0008f;
        lp += 0.35f * ((2.0f * ph - 1.0f) - lp);
        const float src = lp + r.bi() * 0.03f;
        float y = 0.0f;
        const float gains[3] { 1.0f, 0.5f, 0.22f };
        for (int k = 0; k < 3; ++k)
        {
            const float w = kTwoPi * f[k] / (float) SR;
            const float rr = std::exp (-kPi * (55.0f + f[k] * 0.05f) / (float) SR);
            const float o = (1.0f - rr) * src + 2.0f * rr * std::cos (w) * bp[k][0] - rr * rr * bp[k][1];
            bp[k][1] = bp[k][0]; bp[k][0] = o;
            y += o * gains[k];
        }
        out[i] = y * env;
    }
    normalise (out, 0.7f);
    auto s = room (out, 1.0f, 0.35f, 0.22f, 1.5);
    s.l.resize (out.size()); s.r.resize (out.size());
    return s;
}

// --------------------------------------------------------------------------------------------- styles
enum Lane { K, SN, CL, CH, OH, RD, RIM, SH, CGL, CGH, BG, TB, CB, TML, TMH, numLanes };

struct Style
{
    const char* name;
    float bpm, swing, lofi, air;
    KickP kick; SnareP snare; ClapP clap; HatP ch, oh;
    float rideScale, rimHz, shakerHz, congaHz, bongoHz;
    std::map<Lane, const char*> pattern;   // 16 steps (repeated) or 32 steps (two bars)
};

KickP kickP (float f0, float f1, float pt, float dec, float click, float drive, float hold = 0.02f, float noise = 0.0f)
{ KickP p; p.f0 = f0; p.f1 = f1; p.pitchT = pt; p.dec = dec; p.click = click; p.drive = drive; p.hold = hold; p.noise = noise; p.len = dec + 0.25f; return p; }
SnareP snareP (float f, float bodyDec, float noiseDec, float hp, float lp, float body, float noise, float snap, float drive)
{ SnareP p; p.f = f; p.bodyDec = bodyDec; p.noiseDec = noiseDec; p.hp = hp; p.lp = lp; p.body = body; p.noise = noise; p.snap = snap; p.drive = drive; p.len = noiseDec + 0.2f; return p; }
ClapP clapP (int bursts, float spacing, float tail, float freq, float q)
{ ClapP p; p.bursts = bursts; p.spacing = spacing; p.tail = tail; p.freq = freq; p.q = q; p.len = tail + 0.15f; return p; }
HatP hatP (float dec, float scale, float hp, float bp, float noise, float metal)
{ HatP p; p.dec = dec; p.scale = scale; p.hp = hp; p.bp = bp; p.noise = noise; p.metal = metal; p.len = dec + 0.1f; return p; }

const std::vector<Style>& styles()
{
    static const std::vector<Style> s {
        { "Deep House", 122, 0.10f, 0.0f, 0.15f,
          kickP (230, 48, 0.040f, 0.42f, 0.25f, 0.35f, 0.03f), snareP (200, 0.10f, 0.20f, 1600, 8000, 0.6f, 0.7f, 0.2f, 0.2f),
          clapP (4, 0.010f, 0.30f, 1100, 1.0f), hatP (0.045f, 0.9f, 7500, 9500, 0.5f, 0.6f), hatP (0.30f, 0.9f, 7000, 9000, 0.5f, 0.6f),
          0.9f, 520, 7200, 210, 380,
          { { K, "X...X...X...X..." }, { CL, "....x.......x..." }, { CH, "g.og g.og g.og g.og" }, { OH, "..x...x...x...x." },
            { SH, "gogogogogogogogo" }, { RIM, "......o.....o..." } } },
        { "Organic House", 118, 0.14f, 0.0f, 0.30f,
          kickP (200, 46, 0.045f, 0.38f, 0.15f, 0.25f, 0.025f, 0.1f), snareP (180, 0.12f, 0.18f, 1200, 6500, 0.7f, 0.5f, 0.1f, 0.15f),
          clapP (3, 0.012f, 0.22f, 1000, 0.9f), hatP (0.035f, 0.8f, 8000, 10500, 0.7f, 0.3f), hatP (0.22f, 0.8f, 7500, 9500, 0.7f, 0.3f),
          0.8f, 640, 6800, 190, 360,
          { { K, "X...X...X...X..." }, { RIM, "...o..o....o..o." }, { SH, "goxogoxogoxogoxo" }, { CGL, "x.....o...x....." },
            { CGH, "...o.o...o..o.oo" }, { BG, ".o......o.o....." }, { TB, "....o.......o..." }, { CL, "....g.......g..." },
            { OH, "..o...o...o...o." } } },
        { "House", 124, 0.08f, 0.0f, 0.10f,
          kickP (280, 52, 0.032f, 0.40f, 0.45f, 0.55f), snareP (210, 0.09f, 0.18f, 1900, 9500, 0.6f, 0.8f, 0.3f, 0.3f),
          clapP (4, 0.011f, 0.26f, 1250, 1.2f), hatP (0.050f, 1.0f, 7200, 10000, 0.4f, 0.8f), hatP (0.32f, 1.0f, 7000, 9500, 0.4f, 0.8f),
          1.0f, 500, 7800, 230, 400,
          { { K, "X...X...X...X..." }, { CL, "....X.......X..." }, { OH, "..X...X...X...X." }, { CH, "x.xgx.xgx.xgx.xg" },
            { TB, "....o.......o..." } } },
        { "Tech House", 126, 0.06f, 0.0f, 0.08f,
          kickP (300, 50, 0.028f, 0.36f, 0.55f, 0.8f, 0.015f), snareP (220, 0.08f, 0.16f, 2200, 10000, 0.5f, 0.9f, 0.4f, 0.4f),
          clapP (5, 0.009f, 0.22f, 1400, 1.4f), hatP (0.040f, 1.1f, 8000, 11000, 0.5f, 0.8f), hatP (0.26f, 1.1f, 7600, 10500, 0.5f, 0.8f),
          1.1f, 560, 8200, 240, 420,
          { { K, "X...X...X...X..." }, { CL, "....X.......X..." }, { CH, "xgxgxgxgxgxgxgxg" }, { OH, "..o...o...o...o." },
            { RIM, "..o..o.....o.o.." }, { CB, "...........o...." } } },
        { "Techno", 130, 0.0f, 0.0f, 0.18f,
          kickP (320, 46, 0.030f, 0.50f, 0.6f, 1.0f, 0.01f, 0.15f), snareP (230, 0.07f, 0.15f, 2400, 11000, 0.4f, 0.9f, 0.4f, 0.5f),
          clapP (5, 0.010f, 0.32f, 1500, 1.5f), hatP (0.030f, 1.2f, 8500, 11500, 0.6f, 0.9f), hatP (0.20f, 1.2f, 8000, 11000, 0.6f, 0.9f),
          1.2f, 600, 8500, 250, 440,
          { { K, "X...X...X...X..." }, { CH, "xoxoxoxoxoxoxoxo" }, { OH, "..x...x...x...x." }, { RIM, "..o..o..o..o..o." },
            { CL, "............................x..." }, { RD, "o.o.o.o.o.o.o.o." } } },
        { "Afro House", 122, 0.12f, 0.0f, 0.25f,
          kickP (240, 50, 0.038f, 0.40f, 0.3f, 0.4f, 0.025f), snareP (190, 0.11f, 0.18f, 1500, 8000, 0.6f, 0.6f, 0.2f, 0.2f),
          clapP (4, 0.011f, 0.25f, 1100, 1.0f), hatP (0.040f, 0.9f, 7800, 10000, 0.6f, 0.5f), hatP (0.24f, 0.9f, 7400, 9600, 0.6f, 0.5f),
          0.9f, 580, 7000, 200, 370,
          { { K, "X...X...X...X..." }, { SH, "xgogxgogxgogxgog" }, { CGL, "x..o..x...o..x.." }, { CGH, "..x..o..x...x..o" },
            { BG, "o..o..o...o..o.." }, { RIM, "o..o...o..o.o..." }, { CL, "....o.......o..." }, { OH, "..o...o...o...o." } } },
        { "Disco", 120, 0.04f, 0.0f, 0.20f,
          kickP (180, 55, 0.045f, 0.32f, 0.2f, 0.2f, 0.02f, 0.1f), snareP (195, 0.13f, 0.24f, 1300, 7500, 0.8f, 0.6f, 0.2f, 0.1f),
          clapP (3, 0.012f, 0.20f, 1050, 0.9f), hatP (0.055f, 0.85f, 7000, 9000, 0.6f, 0.5f), hatP (0.34f, 0.85f, 6800, 8800, 0.6f, 0.5f),
          0.85f, 480, 7000, 220, 390,
          { { K, "X...X...X...X..." }, { SN, "....X.......X..." }, { OH, "..x...x...x...x." }, { CH, "x.g.x.g.x.g.x.g." },
            { TB, "gogogogogogogogo" }, { CL, "....o.......o..." } } },
        { "Breakbeat", 130, 0.05f, 0.0f, 0.15f,
          kickP (260, 52, 0.035f, 0.34f, 0.4f, 0.6f), snareP (210, 0.10f, 0.20f, 1700, 9000, 0.7f, 0.8f, 0.35f, 0.4f),
          clapP (4, 0.010f, 0.24f, 1200, 1.1f), hatP (0.045f, 1.0f, 7500, 10000, 0.5f, 0.7f), hatP (0.25f, 1.0f, 7200, 9600, 0.5f, 0.7f),
          1.0f, 520, 7600, 230, 400,
          { { K, "X.........X.....X.....x...X...o." }, { SN, "....X.......X.......X..g....X.gg" }, { CH, "x.x.x.x.x.x.x.x." },
            { OH, "..............o." } } },
        { "Hip Hop", 92, 0.22f, 0.15f, 0.12f,
          kickP (190, 47, 0.05f, 0.50f, 0.3f, 0.5f, 0.03f), snareP (185, 0.12f, 0.22f, 1400, 7500, 0.8f, 0.7f, 0.3f, 0.35f),
          clapP (4, 0.011f, 0.22f, 1150, 1.0f), hatP (0.050f, 0.9f, 7000, 9000, 0.5f, 0.6f), hatP (0.28f, 0.9f, 6800, 8800, 0.5f, 0.6f),
          0.9f, 500, 7000, 210, 380,
          { { K, "X......o..x.....X......o.ox....." }, { SN, "....X.......X..." }, { CH, "x.o.x.o.x.o.x.o." }, { RIM, "...........g...." } } },
        { "Lo-fi", 84, 0.28f, 0.6f, 0.25f,
          kickP (170, 48, 0.05f, 0.40f, 0.15f, 0.4f, 0.03f, 0.1f), snareP (175, 0.13f, 0.20f, 1200, 6000, 0.8f, 0.6f, 0.15f, 0.3f),
          clapP (3, 0.012f, 0.20f, 1000, 0.9f), hatP (0.050f, 0.8f, 6500, 8500, 0.7f, 0.4f), hatP (0.25f, 0.8f, 6300, 8200, 0.7f, 0.4f),
          0.8f, 470, 6500, 200, 360,
          { { K, "X.....o...X.....X.....o.o......." }, { SN, "....X.......X..g" }, { CH, "x.o.x.o.x.o.x.oo" }, { SH, "..g...g...g...g." } } },
        { "Trap", 140, 0.0f, 0.0f, 0.10f,
          kickP (150, 38, 0.07f, 1.10f, 0.3f, 0.8f, 0.06f), snareP (205, 0.09f, 0.22f, 1800, 10000, 0.6f, 0.8f, 0.4f, 0.3f),
          clapP (5, 0.010f, 0.30f, 1300, 1.3f), hatP (0.035f, 1.1f, 8500, 11500, 0.4f, 0.9f), hatP (0.22f, 1.1f, 8000, 11000, 0.4f, 0.9f),
          1.1f, 560, 8000, 240, 420,
          { { K, "X.......o..X....X......o..X..o.." }, { CL, "........X..............." "X......." }, { SN, "........x......." },
            { CH, "x.x.x.x.x.x.x.x.x.x.x.x.x.3.3333" }, { OH, "..............o." } } },
        { "UK Garage", 132, 0.18f, 0.0f, 0.15f,
          kickP (250, 50, 0.035f, 0.36f, 0.35f, 0.5f), snareP (215, 0.09f, 0.18f, 1900, 9500, 0.6f, 0.8f, 0.3f, 0.3f),
          clapP (4, 0.010f, 0.22f, 1250, 1.2f), hatP (0.040f, 1.0f, 7800, 10500, 0.5f, 0.7f), hatP (0.24f, 1.0f, 7500, 10000, 0.5f, 0.7f),
          1.0f, 540, 7800, 230, 410,
          { { K, "X.........X.....X......o..X....." }, { SN, "....X.......X..." }, { CH, "gox.gox.gox.gox." }, { RIM, "...o.....o....o." },
            { SH, "gogogogogogogogo" } } },
    };
    return s;
}

// one kit per style and variant: the parameters move a little so A and B do not sound the same
struct Kit { Mono s[numLanes]; };

Kit makeKit (const Style& st, int variant, uint32_t seed)
{
    Rng r (seed);
    auto j = [&] (float v, float amount) { return v * (1.0f + (variant == 0 ? 0.0f : r.in (-amount, amount))); };
    Kit k;
    auto kp = st.kick; kp.f0 = j (kp.f0, 0.15f); kp.f1 = j (kp.f1, 0.08f); kp.dec = j (kp.dec, 0.2f); kp.click = j (kp.click, 0.4f);
    k.s[K] = kick (kp, r);
    auto sp = st.snare; sp.f = j (sp.f, 0.12f); sp.noiseDec = j (sp.noiseDec, 0.25f); sp.hp = j (sp.hp, 0.2f);
    k.s[SN] = snare (sp, r);
    auto cp = st.clap; cp.freq = j (cp.freq, 0.15f); cp.tail = j (cp.tail, 0.25f);
    k.s[CL] = clap (cp, r);
    auto ch = st.ch; ch.dec = j (ch.dec, 0.3f); ch.scale = j (ch.scale, 0.1f);
    k.s[CH] = hat (ch, r);
    auto oh = st.oh; oh.dec = j (oh.dec, 0.25f); oh.scale = ch.scale;
    k.s[OH] = hat (oh, r);
    k.s[RD] = ride (j (st.rideScale, 0.1f), j (1.6f, 0.2f), r);
    k.s[RIM] = rim (j (st.rimHz, 0.12f), j (0.03f, 0.3f), r);
    k.s[SH] = shaker (j (st.shakerHz, 0.15f), j (0.012f, 0.4f), j (0.07f, 0.3f), 1, 0.0f, r);
    DrumP cl; cl.f = j (st.congaHz, 0.08f); cl.dec = 0.32f; cl.drop = 0.12f; cl.otAmt = 0.3f; cl.len = 0.6f;
    k.s[CGL] = skinDrum (cl, r);
    DrumP chh = cl; chh.f = cl.f * 1.5f;     // a fifth above the low conga (it was a tritone), so a loop moved to the key stays in it
    chh.dec = 0.22f; chh.slap = variant == 1 ? 0.5f : 0.15f;
    k.s[CGH] = skinDrum (chh, r);
    DrumP bg; bg.f = cl.f * 2.0f;            // the bongo an octave over the low conga
    bg.dec = 0.13f; bg.drop = 0.18f; bg.overtone = 1.7f; bg.otAmt = 0.2f; bg.len = 0.35f;
    k.s[BG] = skinDrum (bg, r);
    k.s[TB] = tambourine (j (0.16f, 0.2f), 1, r);
    k.s[CB] = cowbell (j (560.0f, 0.06f), r);
    DrumP tl; tl.f = j (95.0f, 0.1f); tl.drop = 0.45f; tl.dropT = 0.04f; tl.dec = 0.5f; tl.otAmt = 0.15f; tl.noise = 0.15f; tl.len = 0.8f;
    k.s[TML] = skinDrum (tl, r);
    DrumP th = tl; th.f = tl.f * 1.5f; th.dec = 0.38f;
    k.s[TMH] = skinDrum (th, r);

    for (auto& m : k.s)
    {
        if (st.lofi > 0.0f) crush (m, st.lofi);
        normalise (m);
    }
    // a little air around the hand percussion in the warmer styles
    if (st.air > 0.0f)
        for (auto lane : { RIM, CGL, CGH, BG, TB, SH, CL, SN })
        {
            auto wet = toMono (room (k.s[lane], 0.5f, 0.45f, st.air, 0.25));
            trimSilence (wet);
            normalise (wet);
            k.s[lane] = std::move (wet);
        }
    return k;
}

// --------------------------------------------------------------------------------------------- loops
float laneGain (Lane l)
{
    switch (l)
    {
        case K: return 1.0f;    case SN: return 0.62f;  case CL: return 0.58f;  case CH: return 0.30f;  case OH: return 0.30f;
        case RD: return 0.20f;  case RIM: return 0.36f; case SH: return 0.24f;  case CGL: return 0.46f; case CGH: return 0.42f;
        case BG: return 0.36f;  case TB: return 0.24f;  case CB: return 0.26f;  case TML: return 0.55f; case TMH: return 0.5f;
        default: return 0.4f;
    }
}

float lanePan (Lane l)
{
    switch (l)
    {
        case CH: return 0.18f;  case OH: return 0.22f;  case RD: return -0.25f; case RIM: return 0.12f;  case SH: return -0.3f;
        case CGL: return -0.25f; case CGH: return 0.3f; case BG: return 0.4f;   case TB: return -0.4f;   case CB: return 0.25f;
        case TML: return -0.2f; case TMH: return 0.2f;
        default: return 0.0f;
    }
}

float stepVelocity (char c)
{
    switch (c)
    {
        case 'X': return 1.0f; case 'x': return 0.85f; case 'o': return 0.62f; case 'g': return 0.36f; case '3': return 0.7f;
        default: return 0.0f;
    }
}

// two bars, exactly: anything that rings past the end comes back in at the start, so the loop repeats cleanly
Stereo renderLoop (const Style& st, const Kit& kit, const std::map<Lane, String>& lanes, uint32_t seed)
{
    Rng r (seed);
    const double stepSecs = 60.0 / st.bpm / 4.0;
    const size_t len = (size_t) std::llround (32.0 * stepSecs * SR);
    Stereo out { Mono (len, 0.0f), Mono (len, 0.0f) };

    auto place = [&] (const Mono& s, double at, float gain, float pan)
    {
        const float a = (pan + 1.0f) * 0.25f * kPi;
        const float gl = std::cos (a) * gain * 1.41f, gr = std::sin (a) * gain * 1.41f;
        const auto start = (size_t) std::max (0.0, std::round (at * SR));
        for (size_t i = 0; i < s.size(); ++i)
        {
            const size_t p = (start + i) % len;
            out.l[p] += s[i] * gl;
            out.r[p] += s[i] * gr;
        }
    };

    for (auto& [lane, pat] : lanes)
    {
        if (pat.isEmpty()) continue;
        const auto steps = pat.removeCharacters (" ");
        for (int s = 0; s < 32; ++s)
        {
            const char c = (char) steps[s % steps.length()];
            const float vel = stepVelocity (c);
            if (vel <= 0.0f) continue;
            double t = s * stepSecs;
            if (s % 2 == 1) t += st.swing * stepSecs;                       // swing on the off sixteenths
            if (lane != K) t += r.in (-0.0025f, 0.0025f);                   // hands are never exact
            const float v = vel * (1.0f + r.in (-0.07f, 0.07f));
            const float g = laneGain (lane) * v * (0.35f + 0.65f * v);
            place (kit.s[lane], std::max (0.0, t), g, lanePan (lane));
            if (c == '3') place (kit.s[lane], t + stepSecs * 0.5, g * 0.8f, lanePan (lane));   // a 1/32 roll
        }
    }

    // glue: a soft clip on the bus, then the same peak for every loop
    for (size_t i = 0; i < len; ++i) { out.l[i] = sat (out.l[i] * 0.8f, 0.35f); out.r[i] = sat (out.r[i] * 0.8f, 0.35f); }
    normalise (out, 0.89f);
    return out;
}

// variant B: a few more ghost notes, and a short fill on the last beat of bar two
std::map<Lane, String> varied (const std::map<Lane, const char*>& base, Rng& r)
{
    std::map<Lane, String> m;
    for (auto& [lane, p] : base)
    {
        auto s = String (p).removeCharacters (" ");
        if (s.length() == 16) s = s + s;
        if (lane == CH || lane == SH || lane == RIM)
            for (int i = 0; i < 32; ++i)
                if (s[i] == '.' && r.chance (0.18f)) s = s.replaceSection (i, 1, "g");
        m[lane] = s;
    }
    if (m.count (SN) == 0) m[SN] = String::repeatedString (".", 32);
    m[SN] = m[SN].replaceSection (28, 4, "g.og");
    return m;
}

std::map<Lane, String> asStrings (const std::map<Lane, const char*>& base)
{
    std::map<Lane, String> m;
    for (auto& [lane, p] : base) m[lane] = p;
    return m;
}

std::map<Lane, String> onlyLanes (const std::map<Lane, const char*>& base, std::initializer_list<Lane> keep)
{
    std::map<Lane, String> m;
    for (auto& [lane, p] : base)
        if (std::find (keep.begin(), keep.end(), lane) != keep.end()) m[lane] = p;
    return m;
}

// --------------------------------------------------------------------------------------------- writing
bool writeWav (const File& f, const Mono& l, const Mono* r = nullptr)
{
    // written next to the target and swapped in at the end, so a project that is playing the file never reads half of it
    f.getParentDirectory().createDirectory();
    TemporaryFile tmp (f);
    {
        WavAudioFormat wav;
        auto* os = tmp.getFile().createOutputStream().release();
        if (os == nullptr) return false;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os, SR, r != nullptr ? 2u : 1u, 16, {}, 0));
        if (w == nullptr) { delete os; return false; }
        const float* chans[2] { l.data(), r != nullptr ? r->data() : nullptr };
        if (! w->writeFromFloatArrays (chans, r != nullptr ? 2 : 1, (int) l.size())) return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}

const char* noteName (int semis)
{
    static const char* n[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    return n[((semis % 12) + 12) % 12];
}

struct Job { String path; std::function<void (const File&)> make; };

std::vector<Job> jobs()
{
    std::vector<Job> list;
    const auto& st = styles();

    // kits are made once per style and variant and shared by the one-shot folders and the loops
    auto kit = [] (int styleIndex, int variant) -> const Kit&
    {
        static std::map<int, Kit> cache;
        static CriticalSection lock;
        const ScopedLock sl (lock);
        const int key = styleIndex * 2 + variant;
        auto it = cache.find (key);
        if (it == cache.end())
            it = cache.emplace (key, makeKit (styles()[(size_t) styleIndex], variant, 0x5eed0000u + (uint32_t) key * 7919u)).first;
        return it->second;
    };

    static const std::pair<Lane, const char*> oneShots[] {
        { K, "01 Bombos/Bombo" }, { SN, "02 Cajas/Caja" }, { CL, "03 Palmas/Palmas" }, { CH, "04 Charles cerrados/Charles cerrado" },
        { OH, "05 Charles abiertos/Charles abierto" }, { RIM, "07 Aros/Aro" }, { SH, "08 Shakers/Shaker" } };
    for (int i = 0; i < (int) st.size(); ++i)
        for (int v = 0; v < 2; ++v)
            for (auto& [lane, prefix] : oneShots)
            {
                const auto name = String (prefix) + " " + st[(size_t) i].name + (v == 0 ? " A" : " B") + ".wav";
                const Lane l = lane;
                list.push_back ({ name, [kit, i, v, l] (const File& f) { writeWav (f, kit (i, v).s[l]); } });
            }

    // hand percussion and toms: one set per style
    static const std::pair<Lane, const char*> percs[] {
        { CGL, "09 Percusiones/Conga grave" }, { CGH, "09 Percusiones/Conga aguda" }, { BG, "09 Percusiones/Bongo" },
        { TB, "09 Percusiones/Pandero" } };
    for (int i = 0; i < (int) st.size(); ++i)
        for (auto& [lane, prefix] : percs)
        {
            const Lane l = lane;
            list.push_back ({ String (prefix) + " " + st[(size_t) i].name + ".wav", [kit, i, l] (const File& f) { writeWav (f, kit (i, 1).s[l]); } });
        }
    for (int i = 0; i < 6; ++i)
    {
        list.push_back ({ "09 Percusiones/Cencerro " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0xc0b0u + (uint32_t) i); auto m = cowbell (480.0f + 60.0f * (float) i, r); normalise (m); writeWav (f, m); } });
        list.push_back ({ "09 Percusiones/Bloque de madera " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0xb10cu + (uint32_t) i); auto m = woodTone (900.0f + 170.0f * (float) i, 2.71f, 0.05f, r); normalise (m); writeWav (f, m); } });
        list.push_back ({ "09 Percusiones/Clave " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0xc1a0u + (uint32_t) i); auto m = woodTone (2200.0f + 160.0f * (float) i, 3.9f, 0.07f, r); normalise (m); writeWav (f, m); } });
        list.push_back ({ "08 Shakers/Shaker doble " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0x5a4eu + (uint32_t) i); auto m = shaker (6000.0f + 600.0f * (float) i, 0.018f, 0.06f, 2, 0.07f + 0.01f * (float) i, r); normalise (m); writeWav (f, m); } });
        list.push_back ({ "09 Percusiones/Pandero sacudido " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0x7a3bu + (uint32_t) i); auto m = tambourine (0.15f + 0.02f * (float) i, 3, r); normalise (m); writeWav (f, m); } });
    }
    for (int i = 0; i < 12; ++i)
        list.push_back ({ "10 Toms/Tom " + String (i < 4 ? "grave " : i < 8 ? "medio " : "agudo ") + String (i % 4 + 1) + ".wav", [i] (const File& f)
        {
            Rng r (0x7077u + (uint32_t) i);
            DrumP p; p.f = (i < 4 ? 80.0f : i < 8 ? 125.0f : 185.0f) * (1.0f + 0.06f * (float) (i % 4)); p.drop = 0.45f; p.dropT = 0.035f;
            p.dec = i < 4 ? 0.6f : i < 8 ? 0.45f : 0.35f; p.otAmt = 0.15f; p.noise = 0.18f; p.len = p.dec + 0.2f;
            auto m = skinDrum (p, r); normalise (m); writeWav (f, m);
        } });

    // cymbals
    for (int i = 0; i < 6; ++i)
    {
        list.push_back ({ "06 Platillos/Ride " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0x41deu + (uint32_t) i); auto m = ride (0.8f + 0.08f * (float) i, 1.4f + 0.15f * (float) i, r); normalise (m); writeWav (f, m); } });
        list.push_back ({ "06 Platillos/Crash " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0xc4a5u + (uint32_t) i); auto s = crash (0.9f + 0.07f * (float) i, 1.8f + 0.25f * (float) i, r); normalise (s); writeWav (f, s.l, &s.r); } });
    }

    // extra kicks: long 808s and soft sub kicks
    for (int i = 0; i < 6; ++i)
        list.push_back ({ "01 Bombos/Bombo 808 largo " + String (i + 1) + ".wav", [i] (const File& f)
        {
            Rng r (0x808u + (uint32_t) i);
            auto m = kick (kickP (150.0f + 15.0f * (float) i, 40.0f + 2.0f * (float) i, 0.06f, 0.9f + 0.15f * (float) i, 0.25f, 0.6f + 0.1f * (float) i, 0.05f), r);
            normalise (m); writeWav (f, m);
        } });

    // loops: complete A and B, top (no kick, snare or clap) and percussion only
    for (int i = 0; i < (int) st.size(); ++i)
    {
        const auto& s = st[(size_t) i];
        const auto base = String (s.name) + " " + String ((int) s.bpm) + " BPM";
        list.push_back ({ String::fromUTF8 ("11 Loops de batería/") + base + " - Completo A.wav", [kit, i] (const File& f)
            { const auto& sty = styles()[(size_t) i]; auto o = renderLoop (sty, kit (i, 0), asStrings (sty.pattern), 0x100u + (uint32_t) i); writeWav (f, o.l, &o.r); } });
        list.push_back ({ String::fromUTF8 ("11 Loops de batería/") + base + " - Completo B.wav", [kit, i] (const File& f)
            { const auto& sty = styles()[(size_t) i]; Rng r (0x200u + (uint32_t) i); auto o = renderLoop (sty, kit (i, 1), varied (sty.pattern, r), 0x300u + (uint32_t) i); writeWav (f, o.l, &o.r); } });
        list.push_back ({ "12 Loops top/" + base + " - Top.wav", [kit, i] (const File& f)
            {
                const auto& sty = styles()[(size_t) i];
                auto o = renderLoop (sty, kit (i, 0), onlyLanes (sty.pattern, { CH, OH, RD, RIM, SH, CGL, CGH, BG, TB, CB }), 0x400u + (uint32_t) i);
                writeWav (f, o.l, &o.r);
            } });
        list.push_back ({ String::fromUTF8 ("13 Loops de percusión/") + base + String::fromUTF8 (" - Percusión.wav"), [kit, i] (const File& f)
            {
                const auto& sty = styles()[(size_t) i];
                auto lanes = onlyLanes (sty.pattern, { RIM, SH, CGL, CGH, BG, TB, CB });
                // styles without hand drums get a simple conga and shaker part, so every style has one
                if (lanes.count (CGL) == 0) lanes[CGL] = "x.....o...x...o.";
                if (lanes.count (CGH) == 0) lanes[CGH] = "...o.o.....o.o..";
                if (lanes.count (SH) == 0)  lanes[SH]  = "gogogogogogogogo";
                auto o = renderLoop (sty, kit (i, 1), lanes, 0x500u + (uint32_t) i);
                writeWav (f, o.l, &o.r);
            } });
    }

    // fills: a bar of time, then a bar that rolls into the next phrase
    for (int i = 0; i < 8; ++i)
        list.push_back ({ "14 Fills/Fill " + String (i + 1) + ".wav", [kit, i] (const File& f)
        {
            const int si = (i * 5) % (int) styles().size();
            auto sty = styles()[(size_t) si];
            std::map<Lane, String> lanes;
            // the first bar keeps time, the second rolls down the toms into a crash-free stop
            lanes[K]   = i % 2 == 0 ? "X...X...X...X...X...X..........." : "X.....X...X.....X...............";
            lanes[CH]  = "x.x.x.x.x.x.x.x.x.x.x.x.........";
            lanes[SN]  = i % 3 == 0 ? "....X.......X.......g.ogxoxxXxXX" : "....X.......X.......X...........";
            lanes[TMH] = i % 3 == 0 ? "................................" : "........................x.x.o...";
            lanes[TML] = i % 3 == 0 ? "................................" : "............................x.xX";
            auto o = renderLoop (sty, kit (si, i % 2), lanes, 0x600u + (uint32_t) i);
            writeWav (f, o.l, &o.r);
        } });

    // bass one-shots in every note of an octave, so they can be played from Squirrel or Hamster
    for (int n = 0; n < 12; ++n)
    {
        const float hz = 32.70f * std::pow (2.0f, (float) n / 12.0f);
        list.push_back ({ "15 Bajos/Bajo 808 " + String (noteName (n)) + ".wav", [hz, n] (const File& f)
            { Rng r (0xba55u + (uint32_t) n); auto m = bass808 (hz, 1.3f, 0.6f, r); normalise (m); writeWav (f, m); } });
        if (n % 2 == 0)
            list.push_back ({ "15 Bajos/Bajo pulsado " + String (noteName (n)) + ".wav", [hz, n] (const File& f)
                { Rng r (0xb1ccu + (uint32_t) n); auto m = pluckBass (hz * 2.0f, 2400.0f, 0.5f, r); normalise (m); writeWav (f, m); } });
    }

    // the example vocal of the block (Chinchilla's channel): A minor, 122 BPM, 16 bars, sung from bar 9
    list.push_back ({ String::fromUTF8 ("17 Voces/Voz La menor 122 BPM - Bloque.wav"), [] (const File& f)
    {
        std::vector<SungNote> notes;
        const SungNote phrase[] { { 0, 1.5, 69, 0 }, { 1.5, 0.5, 67, 3 }, { 2, 1.75, 64, 0 }, { 4, 1, 65, 3 }, { 5, 1, 64, 0 }, { 6, 1.75, 60, 4 },
                                  { 8, 1.5, 67, 0 }, { 9.5, 0.5, 65, 3 }, { 10, 1.75, 64, 0 }, { 12, 1, 62, 3 }, { 13, 1, 64, 0 }, { 14, 1.75, 59, 4 } };
        for (int p = 2; p < 4; ++p)
            for (auto n : phrase) { n.start += p * 16.0; notes.push_back (n); }
        Rng r (0x70c4u);
        auto s = sungVoice (notes, 122.0, 64.0, r);
        writeWav (f, s.l, &s.r);
    } });

    // transitions
    for (int i = 0; i < 4; ++i)
    {
        const float secs = (float) (60.0 / 124.0 * 4.0 * (i % 2 == 0 ? 2 : 4));   // 2 or 4 bars at 124 BPM
        list.push_back ({ "16 Transiciones/Subida " + String (i + 1) + ".wav", [i, secs] (const File& f)
            { Rng r (0x415eu + (uint32_t) i); auto s = riser (secs, 250.0f + 50.0f * (float) i, 9000.0f, false, r); normalise (s); writeWav (f, s.l, &s.r); } });
        list.push_back ({ "16 Transiciones/Bajada " + String (i + 1) + ".wav", [i, secs] (const File& f)
            { Rng r (0xd0e0u + (uint32_t) i); auto s = riser (secs * 0.5f, 200.0f, 8000.0f, true, r); normalise (s); writeWav (f, s.l, &s.r); } });
    }
    for (int i = 0; i < 3; ++i)
        list.push_back ({ "16 Transiciones/Impacto " + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0x1a9au + (uint32_t) i); auto s = impact (r); normalise (s); writeWav (f, s.l, &s.r); } });
    for (int i = 0; i < 3; ++i)
        list.push_back ({ String::fromUTF8 ("16 Transiciones/Platillo al revés ") + String (i + 1) + ".wav", [i] (const File& f)
            { Rng r (0x4e5eu + (uint32_t) i); auto s = reversed (crash (1.0f + 0.1f * (float) i, 2.2f, r)); normalise (s); writeWav (f, s.l, &s.r); } });

    return list;
}

File versionFile (const File& dest) { return dest.getChildFile (".racoon-biblioteca"); }

#include "GenreFx.inc"

File effectsVersionFile (const File& dest) { return dest.getChildFile (".racoon-efectos"); }
} // namespace

File effectsFolder()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile (String::fromUTF8 ("Efectos por género"));
}

bool effectsUpToDate (const File& dest)
{
    return effectsVersionFile (dest).loadFileAsString().trim().getIntValue() == gfx::kFxVersion;
}

int expectedEffectCount() { return (int) gfx::effectJobs().size(); }

Result buildEffects (const File& dest, std::function<bool (float)> progress)
{
    Result res;
    if (! dest.createDirectory()) { res.error = "No se pudo crear " + dest.getFullPathName(); return res; }
    effectsVersionFile (dest).deleteFile();
    const auto list = gfx::effectJobs();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto f = dest.getChildFile (list[i].path);
        list[i].make (f);
        if (f.existsAsFile() && f.getSize() > 44) ++res.files;
        if (progress && ! progress ((float) (i + 1) / (float) list.size())) { res.error = "cancelado"; return res; }
    }
    if (res.files == (int) list.size()) effectsVersionFile (dest).replaceWithText (String (gfx::kFxVersion));
    else res.error = "Solo se escribieron " + String (res.files) + " de " + String ((int) list.size());
    return res;
}

File folder()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Biblioteca Racoon");
}

bool isUpToDate (const File& dest)
{
    return versionFile (dest).loadFileAsString().trim().getIntValue() == kVersion;
}

int expectedFileCount() { return (int) jobs().size(); }

float dominantPitch (const File& file)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return -1.0f;
    const int total = (int) std::min ((juce::int64) r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));
    AudioBuffer<float> a ((int) jmax (1u, r->numChannels), total);
    r->read (&a, 0, total, 0, true, true);
    constexpr int order = 13, n = 1 << order, hop = n / 2;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    double chroma[12] {}, centsSum[12] {}, centsW[12] {};
    const double sr = r->sampleRate;
    for (int pos = 0; pos + n < total; pos += hop)
    {
        float e = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < a.getNumChannels(); ++c) x += a.getSample (c, pos + i);
            x /= (float) a.getNumChannels();
            e += x * x;
            buf[(size_t) i] = x * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) (n - 1)));
        }
        if (e / n < 1.0e-6f) continue;
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const int lo = (int) (60.0 * n / sr), hi = (int) (2500.0 * n / sr);
        float mx = 0.0f;
        for (int k = lo; k < hi; ++k) mx = jmax (mx, buf[(size_t) k]);
        for (int k = lo + 1; k < hi - 1; ++k)
        {
            const float y1 = buf[(size_t) k];
            if (y1 < mx * 0.12f || y1 <= buf[(size_t) k - 1] || y1 <= buf[(size_t) k + 1]) continue;
            const float y0 = std::log (buf[(size_t) k - 1] + 1.0e-9f), y = std::log (y1 + 1.0e-9f), y2 = std::log (buf[(size_t) k + 1] + 1.0e-9f);
            const float d = 0.5f * (y0 - y2) / (y0 - 2.0f * y + y2 - 1.0e-9f);
            const double hz = (k + jlimit (-0.5f, 0.5f, d)) * sr / n;
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int m = (int) std::lround (midi);
            const double w = (double) y1 * y1;
            chroma[((m % 12) + 12) % 12] += w;
            centsSum[((m % 12) + 12) % 12] += (midi - m) * w;
            centsW[((m % 12) + 12) % 12] += w;
        }
    }
    double totalW = 0.0;
    int best = -1;
    for (int pc = 0; pc < 12; ++pc) { totalW += chroma[pc]; if (best < 0 || chroma[pc] > chroma[best]) best = pc; }
    // a sound with a pitch has one class clearly above the rest
    if (totalW <= 0.0 || chroma[best] < 0.3 * totalW) return -1.0f;
    return (float) (60 + best) + (float) (centsSum[best] / jmax (1.0e-12, centsW[best]));
}

File songVoice (const String& name, double bpm, double totalBeats, const std::vector<VoiceNote>& notes, juce::uint32 seed)
{
    const auto f = folder().getChildFile ("17 Voces").getChildFile ("Canciones").getChildFile (File::createLegalFileName (name) + ".wav");
    if (f.existsAsFile() && f.getSize() > 44) return f;
    std::vector<SungNote> sung;
    for (auto& n : notes) sung.push_back ({ n.start, n.length, n.pitch, n.vowel });
    std::sort (sung.begin(), sung.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    if (sung.empty()) return {};
    Rng r (seed == 0 ? 1u : (uint32_t) seed);
    auto st = sungVoice (sung, bpm, totalBeats, r);
    writeWav (f, st.l, &st.r);
    return f.existsAsFile() ? f : File();
}

File ensureFile (const String& relativePath)
{
    const auto f = folder().getChildFile (relativePath);
    // a file from an older library is made again (the sounds changed), the rest of the library follows in the background
    if (f.existsAsFile() && f.getSize() > 44 && isUpToDate (folder())) return f;
    for (auto& j : jobs())
        if (j.path == relativePath) { j.make (f); break; }
    return f.existsAsFile() ? f : File();
}

Result build (const File& dest, std::function<bool (float)> progress)
{
    Result res;
    if (! dest.createDirectory()) { res.error = "No se pudo crear " + dest.getFullPathName(); return res; }
    versionFile (dest).deleteFile();
    const auto list = jobs();
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto f = dest.getChildFile (list[i].path);
        list[i].make (f);
        if (f.existsAsFile() && f.getSize() > 44) ++res.files;
        if (progress && ! progress ((float) (i + 1) / (float) list.size())) { res.error = "cancelado"; return res; }
    }
    if (res.files == (int) list.size()) versionFile (dest).replaceWithText (String (kVersion));
    else res.error = "Solo se escribieron " + String (res.files) + " de " + String ((int) list.size());
    return res;
}

namespace
{
std::thread worker;
std::atomic<bool> cancelBuild { false }, building { false };
}

void ensureAsync (std::function<void()> onDone)
{
    const auto dest = folder(), fxDest = effectsFolder();
    const bool needLibrary = ! isUpToDate (dest), needEffects = ! effectsUpToDate (fxDest);
    if ((! needLibrary && ! needEffects) || building.exchange (true)) return;
    if (worker.joinable()) worker.join();
    cancelBuild = false;
    worker = std::thread ([dest, fxDest, needLibrary, needEffects, onDone]
    {
        bool wrote = false;
        if (needLibrary)
        {
            const auto res = build (dest, [] (float) { return ! cancelBuild.load(); });
            wrote = res.files > 0 && res.error.isEmpty();
        }
        // the effects by genre come after: the library is what the first block needs
        if (needEffects && ! cancelBuild.load())
            buildEffects (fxDest, [] (float) { return ! cancelBuild.load(); });
        building = false;
        if (wrote && onDone && MessageManager::getInstanceWithoutCreating() != nullptr)
            MessageManager::callAsync (onDone);
    });
}

void shutdown()
{
    cancelBuild = true;
    if (worker.joinable()) worker.join();
}
}

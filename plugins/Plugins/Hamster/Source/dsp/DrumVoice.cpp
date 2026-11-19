#include "DrumVoice.h"

namespace be
{
namespace
{
inline float expMap (float x, float lo, float hi) { return lo * std::pow (hi / lo, clamp01 (x)); }

struct ModelTraits { float tri, click, warm, decay, drive, noiseBright, metalScale; };
const ModelTraits& traits (int model)
{
    static const ModelTraits t[] {
        { 0.00f, 0.35f, 14000.0f, 1.30f, 0.05f, 0.85f, 1.00f },   // 808
        { 0.35f, 1.00f, 18000.0f, 0.85f, 0.22f, 1.00f, 1.18f },   // 909
        { 0.15f, 0.60f, 19000.0f, 0.60f, 0.10f, 1.15f, 1.32f },   // 606
        { 0.00f, 0.15f,  5500.0f, 0.55f, 0.00f, 0.60f, 0.86f },   // CR-78
        { 0.10f, 0.70f, 20000.0f, 1.00f, 0.38f, 1.00f, 1.07f },   // Basalt (original)
    };
    return t[juce::jlimit (0, 4, model)];
}

const float kMetalSets[5][6] {
    { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f },
    { 263.0f, 400.0f, 421.0f, 474.0f, 587.0f, 845.0f },
    { 245.0f, 306.0f, 365.0f, 415.0f, 437.0f, 619.0f },
    { 317.0f, 433.0f, 501.0f, 590.0f, 688.0f, 902.0f },
    { 233.0f, 377.0f, 466.0f, 610.0f, 754.0f, 987.0f },     // Basalt: phi-related spacing
};
}

int mapFilterType (int c)
{
    switch (c)
    {
        case 1: return (int) FilterType::LP12;
        case 2: return (int) FilterType::LP24;
        case 3: return (int) FilterType::HP12;
        case 4: return (int) FilterType::HP24;
        case 5: return (int) FilterType::BP;
        case 6: return (int) FilterType::Notch;
        case 7: return (int) FilterType::Ladder;
        case 8: return (int) FilterType::SVF;
        case 9: return (int) FilterType::Comb;
        case 10: return (int) FilterType::Formant;
        default: return -1;
    }
}

// ===================================================================== analog circuit
void AnalogCircuit::configure (int role, int model, const float* P, float ds, float drift, Rng& r)
{
    bodyX = driveX = 0.0f;
    localRng.seed (r.next() | 1u);
    const auto& T = traits (model);
    age = 0; envMax = 1.0f;
    body = Osc(); body2 = Osc(); sub = Osc();
    noise.level = 0; noise.env = 1; noise.tailLevel = 0; noise.tailEnv = 0; noise.bursts = 1; noise.grain = 0; noise.side = 0; noise.hzEnd = -1; noise.attackCoef = 0; noise.attackEnv = 1;
    noise.f.reset(); noise.fS.reset(); noise.hp.reset(); noise.hpS.reset();
    clickLevel = 0; clickEnv = 1; punch = 0; punchEnv = 1;
    metal.level = 0; metal.count = 0; metal.env = 1; metal.side = 0;
    metal.bp.reset(); metal.bpS.reset(); metal.hp.reset(); metal.hpS.reset();
    part.count = 0; part.fm = 0; part.side = 0;
    // level calibration per role: default hits land around -4 dBFS (kick) .. -14 dBFS (hats)
    static const float trimDb[(int) Role::Count] { -5.3f, -6.4f, -3.5f, -6.6f, -8.4f, -9.3f, -19.6f, -8.4f, -7.2f, -6.6f, -9.6f, -12.6f, -11.2f, -11.1f, -9.5f, -13.7f, -8.4f };
    tone = T.warm; drive = T.drive; outGain = dbToGain (trimDb[juce::jlimit (0, (int) Role::Count - 1, role)]); bodyDrive = 0.0f;
    toneL.reset();
    pitchJitter = 1.0f + drift * 0.03f * r.bi();
    const float dm = T.decay * ds;
    auto setOsc = [this] (Osc& o, float f0, float f1, float sweepTau, float lvl, float tau, float tri)
    {
        o.f0 = f0; o.f1 = f1; o.level = lvl; o.env = 1.0f; o.coef = coefFor (tau); o.tri = tri; o.ph = 0.0;
        o.sweep = 1.0f; o.sweepCoef = coefFor (sweepTau);
    };
    auto setNoise = [this] (int mode, float hz, float q, float lvl, float tau)
    {
        noise.mode = mode; noise.hz = hz; noise.q = q; noise.level = lvl; noise.env = 1.0f; noise.coef = coefFor (tau);
        noise.hzEnv = 1.0f; noise.hzCoef = 1.0f;
    };
    auto setMetal = [this, model] (float scale, float lvl, float tau, float bpHz, float hpHz, float q, float spread)
    {
        metal.count = 6; metal.level = lvl; metal.env = 1.0f; metal.coef = coefFor (tau);
        for (int k = 0; k < 6; ++k)
        {
            const float base = kMetalSets[juce::jlimit (0, 4, model)][k];
            const float sp = 1.0f + spread * (float) (k - 2) * 0.06f;
            metal.f[k] = base * scale * sp;
            metal.ph[k] = localRng.uni();
        }
        metal.bpHz = bpHz; metal.hpHz = hpHz; metal.q = q;
        metal.bp.set (bpHz, q, sr); metal.bpS.set (bpHz * 1.04f, q, sr);
        metal.hp.set (hpHz, 0.7f, sr); metal.hpS.set (hpHz, 0.7f, sr);
    };
    auto setPart = [this] (int k, float f, float a, float tau)
    {
        part.f[k] = f; part.a[k] = a; part.env[k] = 1.0f; part.coef[k] = coefFor (tau); part.ph[k] = 0.0;
        part.count = std::max (part.count, k + 1);
    };
    auto setClick = [this] (float lvl, float hz, float tau)
    {
        clickLevel = lvl; clickEnv = 1.0f; clickCoef = coefFor (tau);
        clickF.reset(); clickF.set (hz, 0.8f, sr);
    };

    switch ((Role) role)
    {
        case Role::Kick:
        {
            const float f1 = expMap (P[0], 34.0f, 92.0f) * pitchJitter;
            const float f0 = f1 * std::exp2 (P[1] * 3.6f + (model == 1 ? 0.4f : 0.0f));
            const float sweep = expMap (P[2], 0.004f, 0.12f) * (model == 1 ? 0.6f : 1.0f);
            const float bodyTau = expMap (P[5], 0.05f, 0.9f) * dm;
            setOsc (body, f0, f1, sweep, 1.0f, bodyTau, T.tri);
            setOsc (sub, f1, f1, 1.0f, P[6] * 0.7f, expMap (P[7], 0.08f, 1.6f) * dm, 0.0f);
            setClick (P[3] * T.click * 1.4f, model == 1 ? 5000.0f : 3200.0f, model == 1 ? 0.003f : 0.0012f);
            if (model == 1) { setNoise (0, 6000.0f, 0.7f, P[3] * 0.25f, 0.006f); }
            punch = P[4] * 1.8f; punchEnv = 1.0f; punchCoef = coefFor (0.012f);
            bodyDrive = P[4] * 0.6f + T.drive;
            tone = std::min (T.warm, expMap (1.0f - P[7] * 0.4f, 2500.0f, 20000.0f));
            minLen = (int) (0.03f * sr);
            break;
        }
        case Role::Snare:
        {
            const float f = expMap (P[0], 140.0f, 330.0f) * pitchJitter;
            const float drop = std::exp2 (P[6] * 1.5f);
            setOsc (body, f * drop, f, 0.012f, model == 3 ? 0.6f : 0.9f, expMap (P[4], 0.025f, 0.32f) * dm, T.tri);
            setOsc (body2, f * 1.62f * drop, f * 1.62f, 0.012f, P[5] * 0.7f, expMap (P[5], 0.02f, 0.25f) * dm, 0.0f);
            const float nl = P[1] * 1.3f * (model == 0 ? 0.75f : 1.0f);
            setNoise (model == 3 ? 1 : 3, expMap (P[2], 900.0f, 7000.0f) * T.noiseBright, model == 3 ? 1.2f : 0.6f, nl, expMap (P[3], 0.04f, 0.45f) * dm);
            setClick (P[7] * T.click, 4500.0f, 0.0015f);
            noise.side = 0.12f;
            minLen = (int) (0.02f * sr);
            break;
        }
        case Role::Clap:
        {
            setNoise (1, expMap (P[3], 600.0f, 2600.0f) * pitchJitter, expMap (P[4], 0.6f, 3.0f), 1.3f, 1.0f);
            noise.bursts = 2 + juce::roundToInt (P[0] * 4.0f);
            noise.burstSpacing = (int) (expMap (P[1], 0.004f, 0.02f) * sr);
            noise.burstCoef = coefFor (expMap (1.0f - P[6], 0.0025f, 0.011f));
            noise.tailCoef = coefFor (expMap (P[2], 0.05f, 0.7f) * dm);
            noise.tailLevel = 1.0f;
            noise.side = P[5] * 0.6f;
            setClick (P[6] * 0.4f, 3000.0f, 0.001f);
            drive = P[7] * 0.8f;
            minLen = noise.bursts * noise.burstSpacing + (int) (0.01f * sr);
            break;
        }
        case Role::ClosedHat: case Role::OpenHat: case Role::Ride: case Role::Crash:
        {
            float tauLo = 0.008f, tauHi = 0.14f, scale = 1.0f, bpMul = 1.0f;
            if (role == (int) Role::OpenHat) { tauLo = 0.09f; tauHi = 1.3f; }
            if (role == (int) Role::Ride) { tauLo = 0.35f; tauHi = 3.2f; scale = 0.78f; bpMul = 0.55f; }
            if (role == (int) Role::Crash) { tauLo = 0.5f; tauHi = 4.2f; scale = 0.9f; bpMul = 0.62f; }
            const float tau = expMap (P[3], tauLo, tauHi) * dm;
            const float metalScale = T.metalScale * scale * expMap (P[4], 0.72f, 1.55f) * pitchJitter;
            const float hpHz = expMap (P[0], 2500.0f, 11000.0f);
            const float bpHz = expMap (P[1], 3500.0f, 14000.0f) * bpMul;
            const float shimmer = P[2];
            setMetal (metalScale, (0.35f + 0.9f * shimmer) * (model == 3 ? 0.6f : 1.0f), tau, bpHz, hpHz, 0.9f, P[4]);
            metal.side = P[7] * 0.5f;
            const float noiseAmt = (1.0f - shimmer * 0.7f) * (0.25f + P[5] * 1.1f) * T.noiseBright;
            setNoise (2, hpHz * 1.1f, 0.7f, noiseAmt * (role == (int) Role::Crash ? 1.3f : 0.8f), tau * (role == (int) Role::Crash ? 0.8f : 0.7f));
            noise.side = P[7] * 0.6f;
            if (role == (int) Role::Ride || P[6] > 0.02f)
            {
                const float bellBase = (role == (int) Role::Ride ? 2650.0f : 3400.0f) * scale;
                const float bellAmt = P[6] * (role == (int) Role::Ride ? 0.35f : 0.18f);
                setPart (0, bellBase, bellAmt, tau * 0.8f);
                setPart (1, bellBase * 1.404f, bellAmt * 0.7f, tau * 0.6f);
                setPart (2, bellBase * 2.13f, bellAmt * 0.4f, tau * 0.4f);
                part.fm = 0.2f;
            }
            setClick (0.25f * T.click, 9000.0f, 0.0008f);
            tone = juce::jlimit (12000.0f, 20000.0f, T.warm * 1.2f);   // the warm models stay dark without killing the hats
            if (model == 3) outGain *= dbToGain (9.0f);      // CR-78 cymbals are soft by design, keep them level with the others
            if (model == 0 && role != (int) Role::Crash) outGain *= dbToGain (2.5f);
            minLen = (int) (0.01f * sr);
            break;
        }
        case Role::Rim:
        {
            const float f = expMap (P[0], 700.0f, 2600.0f) * pitchJitter;
            const float damp = 1.0f - P[6] * 0.7f;
            setPart (0, f, 0.8f, expMap (P[2], 0.004f, 0.06f) * dm * damp);
            setPart (1, f * 0.29f, P[3] * 0.8f, expMap (P[2], 0.01f, 0.09f) * dm * damp);
            setPart (2, f * 2.3f, 0.2f * P[2], 0.006f * damp);
            setClick (0.3f + P[1] * T.click, 5000.0f, 0.0009f);
            setNoise (2, expMap (P[7], 2000.0f, 9000.0f), 0.7f, P[4] * 0.6f, 0.012f * dm * damp);
            tone = expMap (P[5], 2200.0f, 18000.0f);
            minLen = (int) (0.01f * sr);
            break;
        }
        case Role::TomLow: case Role::TomMid: case Role::TomHigh: case Role::Conga: case Role::Bongo:
        {
            float lo = 55.0f, hi = 140.0f;
            if (role == (int) Role::TomMid) { lo = 85.0f; hi = 210.0f; }
            if (role == (int) Role::TomHigh) { lo = 120.0f; hi = 310.0f; }
            if (role == (int) Role::Conga) { lo = 230.0f; hi = 460.0f; }
            if (role == (int) Role::Bongo) { lo = 340.0f; hi = 720.0f; }
            const float var = std::exp2 (P[7] * r.bi() / 12.0f);
            const float f = expMap (P[0], lo, hi) * pitchJitter * var;
            const float bend = std::exp2 (P[1] * 1.3f);
            const float tau = expMap (P[2], 0.05f, 1.0f) * dm * (role >= (int) Role::Conga ? 0.5f : 1.0f);
            setOsc (body, f * bend, f, 0.035f, 1.0f, tau, T.tri * 0.6f);
            setOsc (body2, f * 1.505f * bend, f * 1.505f, 0.03f, P[3] * 0.55f, tau * 0.45f, 0.0f);
            setOsc (sub, f, f, 1.0f, P[6] * 0.5f, tau * 1.6f, 0.0f);
            setClick ((0.2f + P[4]) * T.click, role >= (int) Role::Conga ? 4000.0f : 2500.0f, 0.0015f);
            setNoise (0, f * 6.0f, 0.7f, P[5] * 0.5f, 0.03f * dm);
            bodyDrive = P[6] * 0.3f;
            minLen = (int) (0.03f * sr);
            break;
        }
        case Role::Shaker:
        {
            setNoise (1, expMap (P[0], 2800.0f, 11000.0f), expMap (P[6], 0.6f, 4.0f), 0.8f + P[3] * 0.8f, expMap (P[7], 0.025f, 0.28f) * dm);
            noise.attackCoef = coefFor (expMap (P[2], 0.002f, 0.05f));
            noise.attackEnv = 0.0f;
            noise.grain = P[1];
            noise.side = P[5] * 0.6f;
            setClick (P[4] * 0.5f, 8000.0f, 0.0008f);
            minLen = (int) (0.02f * sr);
            break;
        }
        case Role::Cowbell:
        {
            const float f = expMap (P[0], 420.0f, 760.0f) * pitchJitter;
            const float ratio = 1.35f + P[1] * 0.3f;
            metal.count = 2; metal.level = 0.9f; metal.env = 1.0f;
            metal.f[0] = f; metal.f[1] = f * ratio; metal.ph[0] = 0; metal.ph[1] = 0.25;
            metal.coef = coefFor (expMap (P[3], 0.05f, 0.7f) * dm);
            metal.bpHz = expMap (P[2], 900.0f, 3200.0f); metal.hpHz = 300.0f; metal.q = 0.9f;
            metal.bp.set (metal.bpHz, 0.9f, sr); metal.bpS.set (metal.bpHz, 0.9f, sr);
            metal.hp.set (metal.hpHz, 0.7f, sr); metal.hpS.set (metal.hpHz, 0.7f, sr);
            // the fast initial drop of the classic bell
            setPart (0, f, 0.4f, 0.02f);
            setPart (1, f * ratio, 0.3f, 0.02f);
            if (P[6] > 0.02f) { setPart (2, f * 2.76f, P[6] * 0.3f, 0.15f * dm); setPart (3, f * 4.1f, P[6] * 0.2f, 0.1f * dm); }
            setClick ((0.15f + P[4] * 0.6f) * T.click, 3000.0f, 0.001f);
            setNoise (2, 3000.0f, 0.7f, P[7] * 0.4f, 0.03f);
            tone = expMap (P[5], 3000.0f, 16000.0f);
            minLen = (int) (0.01f * sr);
            break;
        }
        case Role::Metal:
        {
            const float f = expMap (P[0], 90.0f, 950.0f) * pitchJitter;
            const float inh = P[1];
            const float tau = expMap (P[2], 0.05f, 2.2f) * dm;
            for (int k = 0; k < 6; ++k)
            {
                const float rr = std::pow ((float) (k + 1), 1.0f + inh * 0.9f) * (1.0f + 0.013f * (float) k * inh);
                setPart (k, f * rr, (k == 0 ? P[5] : 1.0f) / (1.0f + (float) k * (1.0f - P[3]) * 0.8f), tau / (1.0f + (float) k * 0.35f));
            }
            part.fm = P[6] * 2.0f;
            part.side = P[7] * 0.5f;
            setClick (P[4] * T.click, 6000.0f, 0.0012f);
            setNoise (2, 4000.0f, 0.7f, P[4] * 0.3f, 0.02f);
            tone = expMap (P[3], 3000.0f, 20000.0f);
            minLen = (int) (0.01f * sr);
            break;
        }
        default: // custom
        {
            const float f = expMap (P[0], 40.0f, 2000.0f) * pitchJitter;
            setOsc (body, f * std::exp2 (P[1] * 4.0f), f, expMap (P[2], 0.003f, 0.2f), 1.0f, 0.3f * dm, P[3]);
            setNoise (1, expMap (P[5], 200.0f, 12000.0f), 0.8f, P[4], 0.12f * dm);
            setClick (P[6], 4000.0f, 0.0015f);
            bodyDrive = P[7];
            minLen = (int) (0.02f * sr);
            break;
        }
    }
    if (noise.mode == 3) { noise.f.set (noise.hz, noise.q, sr); noise.hp.set (noise.hz * 0.5f, 0.7f, sr); }
    else noise.f.set (noise.hz, noise.q, sr);
    noise.fS.set (noise.hz * 1.03f, noise.q, sr);
    noise.hpS.set (noise.hz * 0.5f, 0.7f, sr);
    toneL.setCutoff (tone, sr);
}

float AnalogCircuit::tick (float pm, float& side)
{
    ++age;
    float out = 0.0f;
    float sideOut = 0.0f;
    float mx = 0.0f;
    // bodies
    auto runOsc = [this, pm, &mx] (Osc& o) -> float
    {
        if (o.level <= 0.0f || o.env < 1.0e-5f) return 0.0f;
        o.sweep *= o.sweepCoef;
        const float f = (o.f1 + (o.f0 - o.f1) * o.sweep) * pm;
        o.ph += (double) (f / sr);
        o.ph -= std::floor (o.ph);
        const float x = (float) o.ph;
        float s = std::sin (x * kTwoPi);
        if (o.tri > 0.0f) s = lerpf (s, 4.0f * std::abs (x - 0.5f) - 1.0f, o.tri);
        o.env *= o.coef;
        mx = std::max (mx, o.env * o.level);
        return s * o.env * o.level;
    };
    float b = runOsc (body) + runOsc (body2);
    if (bodyDrive > 0.0f)
    {
        // ADAA: the tanh averaged between samples, so a driven kick body stays clean up top
        const float g = 1.0f + bodyDrive * 5.0f;
        b = racoon::sdsp::adaaAverage ([] (float v) { return fastTanh (v); }, b * g, bodyX) / fastTanh (g);
    }
    out += b + runOsc (sub);
    if (punch > 0.0f) { punchEnv *= punchCoef; out *= 1.0f + punch * punchEnv; }

    // noise
    if (noise.level > 0.0f)
    {
        float env;
        if (noise.bursts > 1)
        {
            // clap: repeated bursts then a diffuse tail
            const int idx = noise.burstSpacing > 0 ? age / noise.burstSpacing : 0;
            const int within = noise.burstSpacing > 0 ? age % noise.burstSpacing : age;
            if (idx < noise.bursts - 1)
                env = std::pow (noise.burstCoef, (float) within);
            else
            {
                noise.tailEnv = noise.tailEnv <= 0.0f ? 1.0f : noise.tailEnv * noise.tailCoef;
                env = noise.tailEnv;
            }
        }
        else
        {
            noise.env *= noise.coef;
            env = noise.env;
            if (noise.attackCoef > 0.0f) { noise.attackEnv = 1.0f - (1.0f - noise.attackEnv) * noise.attackCoef; env *= noise.attackEnv; }
        }
        if (noise.grain > 0.0f && localRng.uni() < noise.grain * 0.02f) noise.pinkB[0] = localRng.uni();
        if (noise.grain > 0.0f) { noise.pinkB[0] *= 0.995f; env *= (1.0f - noise.grain) + noise.grain * (0.3f + noise.pinkB[0] * 1.4f); }
        const float x = nextNoise();
        float y;
        noise.f.tick (x);
        switch (noise.mode)
        {
            case 0: y = noise.f.lp; break;
            case 1: y = noise.f.bp; break;
            case 3: noise.hp.tick (noise.f.lp); y = noise.hp.hp; break;
            default: y = noise.f.hp; break;
        }
        float ys = 0.0f;
        if (noise.side > 0.0f)
        {
            noise.fS.tick (nextNoise());
            ys = (noise.mode == 1 ? noise.fS.bp : (noise.mode == 0 ? noise.fS.lp : noise.fS.hp)) * noise.side;
        }
        out += y * env * noise.level;
        sideOut += ys * env * noise.level;
        mx = std::max (mx, env * noise.level);
    }
    // click
    if (clickLevel > 0.0f && clickEnv > 1.0e-4f)
    {
        clickF.tick (age < 3 ? 1.0f : nextNoise() * 0.5f);
        out += clickF.bp * clickEnv * clickLevel * 2.0f;
        clickEnv *= clickCoef;
    }
    // metal bank
    if (metal.count > 0 && metal.env > 1.0e-5f)
    {
        float s = 0.0f;
        for (int k = 0; k < metal.count; ++k)
        {
            const float dt = metal.f[k] * pm / sr;
            metal.ph[k] += dt;
            if (metal.ph[k] >= 1.0) metal.ph[k] -= 1.0;
            const float t = (float) metal.ph[k];
            float sq = t < 0.5f ? 1.0f : -1.0f;
            sq += polyBlep (t, dt);
            sq -= polyBlep (std::fmod (t + 0.5f, 1.0f), dt);
            s += sq;
        }
        s /= (float) std::max (1, metal.count / 2);
        metal.env *= metal.coef;
        metal.bp.tick (s); metal.hp.tick (metal.bp.bp);
        const float m = (metal.count > 2 ? metal.hp.hp : metal.bp.bp) * metal.env * metal.level;
        out += m;
        if (metal.side > 0.0f)
        {
            metal.bpS.tick (s * (1.0f + 0.05f * nextNoise())); metal.hpS.tick (metal.bpS.bp);
            sideOut += ((metal.count > 2 ? metal.hpS.hp : metal.bpS.bp) * metal.env * metal.level - m) * metal.side;
        }
        mx = std::max (mx, metal.env * metal.level);
    }
    // partials
    if (part.count > 0)
    {
        float s = 0.0f, prev = 0.0f;
        for (int k = 0; k < part.count; ++k)
        {
            if (part.env[k] < 1.0e-5f) continue;
            part.ph[k] += (double) (part.f[k] * pm / sr);
            part.ph[k] -= std::floor (part.ph[k]);
            const float v = std::sin (((float) part.ph[k] + part.fm * prev * 0.15f) * kTwoPi) * part.env[k] * part.a[k];
            part.env[k] *= part.coef[k];
            prev = v;
            s += v;
            mx = std::max (mx, part.env[k] * part.a[k]);
        }
        out += s;
        if (part.side > 0.0f) sideOut += s * part.side * ((age / 7) % 2 == 0 ? 1.0f : -1.0f) * 0.3f;
    }
    if (drive > 0.0f)
    {
        const float g = 1.0f + drive * 4.0f;
        out = racoon::sdsp::adaaAverage ([] (float v) { return fastTanh (v); }, out * g, driveX) / fastTanh (g) * 1.1f;
    }
    if (tone < 19500.0f) out = toneL.lp (out);
    envMax = mx;
    side = sideOut;
    return out * outGain;
}

// ===================================================================== FM
void FmLayer::configure (const float* p, float baseHz, float ds)
{
    algo = juce::jlimit (0, 7, (int) p[T_B_ALGO]);
    base = baseHz;
    fb = p[T_B_FB];
    fbState = 0.0f;
    envMax = 1.0f;
    for (int k = 0; k < 6; ++k)
    {
        ratio[k] = p[T_B_R1 + k];
        level[k] = p[T_B_L1 + k];
        env[k] = 1.0f;
        coef[k] = std::exp (-1.0f / (std::max (0.002f, p[T_B_D1 + k] * ds) * 0.25f * sr));   // decay value = time to ~ -35 dB
        ph[k] = 0.0;
    }
}

float FmLayer::tick (float pm, float morph)
{
    float out[6] {};
    const float idx = morph * 2.0f * 2.4f;     // modulation index scale
    auto op = [&] (int k, float mod) -> float
    {
        ph[k] += (double) (base * ratio[k] * pm / sr);
        ph[k] -= std::floor (ph[k]);
        const float v = std::sin (((float) ph[k] + mod) * kTwoPi) * env[k];
        env[k] *= coef[k];
        return v;
    };
    // index helpers: o[k] is the op's raw output, modulators scaled by level * idx, carriers by level
    auto M = [&] (int k) { return out[k] * level[k] * idx * 0.25f; };
    float y = 0.0f;
    const float fbm = fbState * fb * 0.4f;
    switch (algo)
    {
        case 0: out[1] = op (1, fbm); fbState = out[1]; out[0] = op (0, M (1)); y = out[0] * level[0]; break;
        case 1: out[2] = op (2, fbm); fbState = out[2]; out[1] = op (1, M (2)); out[0] = op (0, M (1)); y = out[0] * level[0]; break;
        case 2: out[3] = op (3, fbm); fbState = out[3]; out[2] = op (2, M (3)); out[1] = op (1, M (2)); out[0] = op (0, M (1)); y = out[0] * level[0]; break;
        case 3: out[3] = op (3, fbm); fbState = out[3]; out[2] = op (2, M (3)); out[1] = op (1, 0.0f); out[0] = op (0, M (1));
                y = out[0] * level[0] + out[2] * level[2]; break;
        case 4: out[3] = op (3, fbm); fbState = out[3]; out[2] = op (2, 0.0f); out[1] = op (1, 0.0f); out[0] = op (0, M (1) + M (2) + M (3));
                y = out[0] * level[0]; break;
        case 5: out[5] = op (5, fbm); fbState = out[5];
                for (int k = 4; k >= 0; --k) out[k] = op (k, M (k + 1));
                y = out[0] * level[0]; break;
        case 6: out[5] = op (5, fbm); fbState = out[5]; out[4] = op (4, M (5)); out[3] = op (3, 0.0f); out[2] = op (2, M (3)); out[1] = op (1, 0.0f); out[0] = op (0, M (1));
                y = (out[0] * level[0] + out[2] * level[2] + out[4] * level[4]) * 0.7f; break;
        default:
            out[0] = op (0, fbm); fbState = out[0];
            for (int k = 1; k < 6; ++k) out[k] = op (k, 0.0f);
            for (int k = 0; k < 6; ++k) y += out[k] * level[k];
            y *= 0.45f;
            break;
    }
    float m = 0.0f;
    for (int k = 0; k < 6; ++k) m = std::max (m, env[k] * level[k]);
    envMax = m;
    return y;
}

// ===================================================================== sample layer
void SampleLayer::configure (const float* p, const TrackAssets& a, const HitInfo& h, Rng& rng, bool roleDefault)
{
    s = nullptr;
    done = true;
    if (roleDefault) s = a.roleDefault;
    else
    {
        const int factoryCount = factorySampleNames().size() - 1;
        const int idx = (int) p[T_C_SAMPLE];
        if (idx < factoryCount) s = a.factory;
        else
        {
            const SampleData* loaded[kNumSlots] {};
            int n = 0;
            for (auto* sl : a.slots) if (sl != nullptr) loaded[n++] = sl;
            if (n > 0)
            {
                switch ((MultiMode) juce::jlimit (0, 3, (int) p[T_C_MULTI]))
                {
                    case MultiMode::Single: s = loaded[0]; break;
                    case MultiMode::Velocity: s = loaded[juce::jlimit (0, n - 1, (int) (h.velocity * (float) n * 0.999f))]; break;
                    case MultiMode::RoundRobin: s = loaded[h.rrIndex % n]; break;
                    case MultiMode::Random: s = loaded[(int) (rng.uni() * (float) n) % n]; break;
                }
            }
        }
    }
    inUse.store (s, std::memory_order_release);
    if (s == nullptr || s->size() < 4) return;
    const int len = s->size();
    srRatio = s->sampleRate / (double) sr;
    double a0 = clamp01 (p[T_C_START]) * len, a1 = clamp01 (p[T_C_END]) * len;
    if (roleDefault) { a0 = 0; a1 = len; }
    const int sliceMode = roleDefault ? 0 : (int) p[T_C_SLICEMODE];
    if (sliceMode > 0)
    {
        const int sl = (int) p[T_C_SLICE];
        double s0, s1;
        if (sliceMode == 1) { s0 = (double) len * sl / 16.0; s1 = (double) len * (sl + 1) / 16.0; }
        else
        {
            const auto& tr = s->transients;
            const int cnt = (int) tr.size();
            const int k = cnt > 0 ? sl % cnt : 0;
            s0 = cnt > 0 ? tr[(size_t) k] : 0;
            s1 = k + 1 < cnt ? tr[(size_t) k + 1] : len;
        }
        const double w = s1 - s0;
        a0 = s0 + clamp01 (p[T_C_START]) * w * 0.9;
        a1 = s0 + clamp01 (p[T_C_END]) * w;
    }
    if (a1 - a0 < 16) a1 = std::min ((double) len, a0 + 16);
    // start variation (humanised start point)
    const double var = (double) p[T_C_RR] * rng.uni() * 0.02 * s->sampleRate;
    regionStart = a0; regionEnd = a1;
    reverse = ! roleDefault && p[T_C_REVERSE] > 0.5f;
    loop = ! roleDefault && p[T_C_LOOP] > 0.5f;
    stretch = ! roleDefault && (int) p[T_C_STRETCHMODE] == 1;
    stretchFactor = roleDefault ? 1.0f : juce::jlimit (0.25f, 4.0f, p[T_C_STRETCH]);
    fadeIn = roleDefault ? 0.0f : p[T_C_FADEIN];
    fadeOut = roleDefault ? 0.002f : p[T_C_FADEOUT];
    pos = reverse ? regionEnd - 1.0 - var : regionStart + var;
    readHead = pos;
    played = 0.0;
    done = false;
    grainLen = std::max (64, (int) (0.045 * sr));
    grainHop = grainLen / 2;
    sinceSpawn = grainHop;
    grains[0] = grains[1] = Grain();
    nextGrain = 0;
}

void SampleLayer::read (double at, float& l, float& r) const
{
    // windowed sinc (was a cubic): cleaner tops on pitched drums, and pitched up it does not fold back
    racoon::sdsp::SincTable::get().read2 (s->l.data(), s->stereo() ? s->r.data() : nullptr, s->size(), at, readBand, l, r);
}

void SampleLayer::tick (float rateMul, float& l, float& r)
{
    l = r = 0.0f;
    if (done || s == nullptr) return;
    const double dir = reverse ? -1.0 : 1.0;
    float gain = 1.0f;
    const double outSr = (double) sr;
    if (fadeIn > 0.0f) gain *= clamp01 ((float) (played / (fadeIn * outSr)));
    if (! stretch)
    {
        const double step = srRatio * rateMul;
        readBand = racoon::sdsp::SincTable::bandFor (std::abs (step));
        // distance to the end of the region in output samples
        const double remain = reverse ? (pos - regionStart) / step : (regionEnd - pos) / step;
        if (fadeOut > 0.0f && ! loop) gain *= clamp01 ((float) (remain / (fadeOut * outSr)));
        read (pos, l, r);
        pos += dir * step;
        if ((! reverse && pos >= regionEnd) || (reverse && pos < regionStart))
        {
            if (loop) pos = reverse ? regionEnd - 1.0 : regionStart;
            else done = true;
        }
    }
    else
    {
        // two overlapping Hann grains read from a head moving at 1/stretch speed
        const double headStep = srRatio / stretchFactor;
        const double remain = reverse ? (readHead - regionStart) / headStep : (regionEnd - readHead) / headStep;
        if (fadeOut > 0.0f && ! loop) gain *= clamp01 ((float) (remain / (fadeOut * outSr)));
        if (++sinceSpawn >= grainHop)
        {
            sinceSpawn = 0;
            auto& g = grains[nextGrain];
            g.on = true; g.start = readHead; g.pos = 0.0; g.age = 0;
            nextGrain ^= 1;
        }
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) g.age / (float) grainLen);
            float gl, gr;
            readBand = racoon::sdsp::SincTable::bandFor (std::abs (srRatio * rateMul));
            read (g.start + dir * g.pos, gl, gr);
            l += gl * w; r += gr * w;
            g.pos += srRatio * rateMul;
            if (++g.age >= grainLen) g.on = false;
        }
        readHead += dir * headStep;
        if ((! reverse && readHead >= regionEnd) || (reverse && readHead < regionStart))
        {
            if (loop) readHead = reverse ? regionEnd - 1.0 : regionStart;
            else done = true;
        }
    }
    played += 1.0;
    l *= gain; r *= gain;
}

// ===================================================================== texture
void TextureLayer::configure (const float* p, Rng& r)
{
    rng.seed (r.next() | 1u);
    type = juce::jlimit (0, 5, (int) p[T_D_TYPE]);
    q = p[T_D_Q];
    tone = p[T_D_TONE];
    env = 1.0f;
    age = 0;
    coef = std::exp (-1.0f / (std::max (0.002f, p[T_D_DECAY]) * 0.145f * sr));
    f.reset();
    comb.clear();
    combLp = 0.0f;
    grainLeft = 0;
    lastHz = -1.0f;
    for (auto& v : pink) v = 0.0f;
    for (int k = 0; k < 6; ++k) mph[k] = rng.uni();
}

float TextureLayer::tick (float pm, float freq)
{
    ++age;
    env *= coef;
    const float atk = clamp01 ((float) age / (0.0008f * sr));
    float x = 0.0f;
    float hz = freq;
    switch ((TexType) type)
    {
        case TexType::White: x = rng.bi(); break;
        case TexType::Pink:
        {
            const float w = rng.bi();
            pink[0] = 0.99765f * pink[0] + w * 0.0990460f;
            pink[1] = 0.96300f * pink[1] + w * 0.2965164f;
            pink[2] = 0.57000f * pink[2] + w * 1.0526913f;
            x = (pink[0] + pink[1] + pink[2] + w * 0.1848f) * 0.25f;
            break;
        }
        case TexType::Metal:
        {
            static const float ratios[6] { 1.0f, 1.483f, 1.932f, 2.546f, 2.630f, 3.897f };
            for (int k = 0; k < 6; ++k)
            {
                mph[k] += (double) (freq * 0.25f * ratios[k] * pm / sr);
                mph[k] -= std::floor (mph[k]);
                x += mph[k] < 0.5 ? 0.33f : -0.33f;
            }
            break;
        }
        case TexType::Crackle:
            x = rng.uni() < 0.0005f + q * 0.01f ? rng.bi() * 3.0f : 0.0f;
            break;
        case TexType::Granular:
            if (grainLeft <= 0 && rng.uni() < 0.002f + q * 0.01f) { grainLeft = (int) ((0.004f + rng.uni() * 0.02f) * sr); grainGain = 0.4f + rng.uni(); }
            if (grainLeft > 0) { --grainLeft; x = rng.bi() * grainGain; }
            break;
        case TexType::Resonator:
        {
            const float d = clampf (sr / std::max (20.0f, freq * pm), 2.0f, (float) comb.buf.size() - 4.0f);
            const float in = age < (int) (0.003f * sr) ? rng.bi() : 0.0f;
            const float y = comb.read (d);
            combLp = combLp + (y - combLp) * (0.3f + 0.6f * (1.0f - std::abs (tone)));
            comb.push (in + combLp * (0.9f + q * 0.099f));
            return y * env * atk * 0.8f;
        }
    }
    if ((filterCounter++ & 15) == 0 && std::abs (hz - lastHz) > 0.5f)
    {
        f.set (clampf (hz, 20.0f, sr * 0.45f), 0.5f + q * 8.0f, sr);
        lastHz = hz;
    }
    f.tick (x);
    float y;
    if (tone < 0.0f) y = lerpf (f.bp, f.lp, -tone);
    else y = lerpf (f.bp, f.hp, tone);
    if (type == (int) TexType::Crackle) y = lerpf (y, x * 0.5f, 0.5f);
    return y * env * atk;
}

// ===================================================================== voice
void DrumVoice::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    analogA.prepare (sr); analogB.prepare (sr);
    fm.prepare (sr); smp.prepare (sr); tex.prepare (sr);
    filter.prepare (sampleRate, (int) (sampleRate / 20.0) + 8);
    stage = Idle;
}

void DrumVoice::start (int trk, const float* p, const HitInfo& h, const TrackAssets& assets, int delaySamples, float circuitW[4])
{
    track = trk;
    rng.seed (h.seed * 2654435761u + 17u);
    const int role = juce::jlimit (0, (int) Role::Count - 1, (int) p[T_ROLE]);
    const int model = juce::jlimit (0, 4, (int) p[T_MODEL]);
    const float ds = std::exp2 ((p[T_DECAY] - 0.5f) * 4.0f);
    float P[8];
    for (int k = 0; k < 8; ++k) P[k] = p[T_A_P1 + k];

    morphOn = p[T_CIRCUIT_ON] > 0.5f;
    if (morphOn)
    {
        for (int k = 0; k < 4; ++k) morphW[k] = circuitW[k];
        aOn = morphW[0] > 0.001f || morphW[1] > 0.001f;
        bOn = morphW[3] > 0.001f;
        cOn = morphW[2] > 0.001f;
        if (morphW[0] > 0.001f) analogA.configure (role, 0, P, ds, p[T_A_DRIFT], rng);
        if (morphW[1] > 0.001f) analogB.configure (role, 1, P, ds, p[T_A_DRIFT], rng);
    }
    else
    {
        aOn = p[T_A_ON] > 0.5f;
        bOn = p[T_B_ON] > 0.5f;
        cOn = p[T_C_ON] > 0.5f;
        if (aOn) analogA.configure (role, model, P, ds, p[T_A_DRIFT], rng);
    }
    dOn = p[T_D_ON] > 0.5f;
    if (bOn) fm.configure (p, roleBaseHz (role), ds);
    if (cOn) smp.configure (p, assets, h, rng, morphOn);
    if (dOn) tex.configure (p, rng);

    // envelopes
    envMode = juce::jlimit (0, 2, (int) p[T_ENV_MODE]);
    attack = p[T_ENV_ATTACK]; hold = envMode == 0 ? 0.0f : p[T_ENV_HOLD]; decay = std::max (0.005f, p[T_ENV_DECAY]);
    sustain = p[T_ENV_SUSTAIN]; release = std::max (0.003f, p[T_ENV_RELEASE]); curve = p[T_ENV_CURVE];
    gate = envMode == (int) EnvMode::ADSR ? (h.gateSamples < 0 ? (int) (0.25f * sr) : h.gateSamples) : -1;
    penvAmt = p[T_PENV_AMT]; penv = 1.0f; penvCoef = std::exp (-1.0f / (std::max (0.001f, p[T_PENV_TIME]) * 0.3f * sr));
    fenvAmt = p[T_FENV_AMT]; fenv = 1.0f; fenvCoef = std::exp (-1.0f / (std::max (0.002f, p[T_FENV_DECAY]) * 0.3f * sr));
    fType = mapFilterType ((int) p[T_F_TYPE]);
    keyTrack = p[T_F_KEY];
    fDrive = p[T_F_DRIVE];
    stepPitch = h.pitch;
    filter.reset();
    filterCounter = 0;

    const float vs = p[T_VELSENS];
    velGain = (1.0f - vs) + vs * h.velocity * h.velocity * 1.15f;
    accentGain = 1.0f + h.accent * 0.75f;
    envLevel = 0.0f; envT = 0.0f;
    delay = std::max (0, delaySamples);
    stage = delay > 0 ? Waiting : Attack;
    lastPeak = 1.0f; silentRun = 0.0f; age = 0;
}

void DrumVoice::choke (float ms)
{
    if (stage == Idle) return;
    if (stage == Waiting) { stage = Idle; return; }
    stage = Choke;
    chokeCoef = std::exp (-1.0f / (std::max (0.3f, ms) * 0.001f * sr * 0.25f));
}

float DrumVoice::envTick()
{
    const float dt = 1.0f / sr;
    switch (stage)
    {
        case Attack:
            envT += dt;
            if (attack <= 0.0005f || envT >= attack) { envLevel = 1.0f; envT = 0.0f; stage = hold > 0.0f ? Hold : Decay; }
            else envLevel = envT / attack;
            break;
        case Hold:
            envT += dt; envLevel = 1.0f;
            if (envT >= hold) { envT = 0.0f; stage = Decay; }
            break;
        case Decay:
        {
            envT += dt;
            const float target = envMode == (int) EnvMode::ADSR ? sustain : 0.0f;
            const float pr = envT / decay;
            envLevel = target + (1.0f - target) * (1.0f - curveFn (pr, curve));
            if (pr >= 1.0f) { envLevel = target; stage = envMode == (int) EnvMode::ADSR ? Sustain : Idle; envT = 0.0f; }
            break;
        }
        case Sustain: envLevel = sustain; break;
        case Release:
        {
            envT += dt;
            const float pr = envT / release;
            envLevel = releaseStart * (1.0f - curveFn (pr, 0.6f));
            if (pr >= 1.0f) { envLevel = 0.0f; stage = Idle; }
            break;
        }
        case Choke:
            envLevel *= chokeCoef;
            if (envLevel < 1.0e-4f) { envLevel = 0.0f; stage = Idle; }
            break;
        default: break;
    }
    return envLevel;
}

void DrumVoice::render (float* L, float* R, int n)
{
    if (stage == Idle) return;
    // continuous values: level, pan, tune, fine, a_level, b_level, b_morph, c_level, c_pitch, d_level, d_freq, cutoff, res, mix
    const float lvl = cont[0], pan = cont[1], tune = cont[2], fine = cont[3];
    const float aL = cont[4], bL = cont[5], bMorph = cont[6], cL = cont[7], cPitch = cont[8], dL = cont[9], dFreq = cont[10];
    const float cutoff = cont[11], res = cont[12], fmix = cont[13];
    const float pa = (pan + 1.0f) * 0.25f * kPi;
    panL = std::cos (pa) * 1.4142f; panR = std::sin (pa) * 1.4142f;
    const float gain = lvl * velGain * accentGain;
    const float baseSemis = tune + fine * 0.01f + stepPitch;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        if (stage == Waiting)
        {
            if (delay > 0) { --delay; continue; }
            stage = Attack;
        }
        if (stage == Idle) break;
        if (gate > 0 && --gate == 0) noteOff();
        const float env = envTick();
        penv *= penvCoef;
        const float pm = std::exp2 ((baseSemis + penvAmt * penv) * (1.0f / 12.0f));

        float mid = 0.0f, side = 0.0f, sl = 0.0f, srr = 0.0f;
        if (aOn)
        {
            float s1 = 0.0f;
            if (morphOn)
            {
                if (morphW[0] > 0.001f) { float sd; mid += analogA.tick (pm, sd) * morphW[0]; s1 += sd * morphW[0]; }
                if (morphW[1] > 0.001f) { float sd; mid += analogB.tick (pm, sd) * morphW[1]; s1 += sd * morphW[1]; }
                mid *= aL; s1 *= aL;
            }
            else { mid += analogA.tick (pm, s1) * aL; s1 *= aL; }
            side += s1;
        }
        if (bOn) mid += fm.tick (pm, bMorph) * bL * 0.45f * (morphOn ? morphW[3] : 1.0f);
        if (cOn)
        {
            float l, r;
            smp.tick (pm * std::exp2 (cPitch * (1.0f / 12.0f)), l, r);
            const float w = cL * 0.5f * (morphOn ? morphW[2] : 1.0f);
            sl = l * w; srr = r * w;
        }
        if (dOn) mid += tex.tick (pm, dFreq) * dL * 0.5f;

        float l = (mid + side) + sl, r = (mid - side) + srr;
        if (fType >= 0)
        {
            fenv *= fenvCoef;
            if ((filterCounter++ & 15) == 0)
            {
                const float oct = fenvAmt * fenv * 6.0f + keyTrack * (baseSemis / 12.0f);
                filter.set (fType, cutoff * std::exp2 (oct), res, fDrive, 0.5f, 0.0f, true);
            }
            const float dl = l, dr = r;
            filter.process (l, r);
            l = lerpf (dl, l, fmix); r = lerpf (dr, r, fmix);
        }
        const float g = env * gain;
        l *= g * panL; r *= g * panR;
        L[i] += l; R[i] += r;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
    }
    ++age;
    lastPeak = peak;
    // end the voice once every layer has died away
    if (stage != Idle && stage != Waiting)
    {
        bool layersDone = true;
        if (aOn) layersDone &= morphOn ? ((morphW[0] <= 0.001f || analogA.silent()) && (morphW[1] <= 0.001f || analogB.silent())) : analogA.silent();
        if (bOn) layersDone &= fm.silent();
        if (cOn) layersDone &= smp.silent();
        if (dOn) layersDone &= tex.silent();
        if (layersDone && peak < 1.0e-5f) { if ((silentRun += 1.0f) > 3.0f) stage = Idle; }
        else silentRun = 0.0f;
        if (! aOn && ! bOn && ! cOn && ! dOn) stage = Idle;
    }
}
} // namespace be

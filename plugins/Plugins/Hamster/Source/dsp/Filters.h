#pragma once
#include "Common.h"
#include "../Params.h"

namespace be
{
// ---------------------------------------------------------------- zero-delay-feedback ladder (N = 3 or 4 poles)
// Linear TPT stages solved implicitly with global feedback; tanh at the feedback summing node,
// optional per-stage saturation and an optional high-pass inside the feedback path (diode/303 voicing).
struct ZdfLadder
{
    float s[4] {};
    float hpState = 0.0f;
    float lastY = 0.0f;

    void reset() { for (auto& v : s) v = 0.0f; hpState = 0.0f; lastY = 0.0f; }

    // g = tan(pi fc / fs); k = feedback; returns stage outputs in y[0..3]
    inline void process (float x, float g, float k, int poles, bool stageSat, float hpCoef, float drive, float* y)
    {
        const float G = g / (1.0f + g);
        const float beta = 1.0f / (1.0f + g);
        float Gn = 1.0f, S = 0.0f;
        for (int i = poles - 1; i >= 0; --i)
        {
            S += Gn * beta * s[i];
            Gn *= G;
        }
        // Gn = G^poles
        float fbSignal;
        if (hpCoef > 0.0f)
        {
            // high-pass in the loop computed with previous output (removes bass from feedback)
            const float hp = lastY - hpState;
            hpState += hpCoef * hp;
            const float yEst = (Gn * x + S) / (1.0f + k * Gn);
            fbSignal = yEst - (hpState);   // approximately HP(yEst)
        }
        else
        {
            fbSignal = (Gn * x + S) / (1.0f + k * Gn);
        }
        float u = x - k * fbSignal;
        u = fastTanh (u * drive) / drive;

        float in = u;
        for (int i = 0; i < poles; ++i)
        {
            if (stageSat && i > 0) in = fastTanh (in * 1.15f) / 1.15f;
            const float v = (in - s[i]) * G;
            const float out = v + s[i];
            s[i] = out + v;
            y[i] = out;
            in = out;
        }
        for (int i = poles; i < 4; ++i) y[i] = y[poles - 1];
        lastY = y[poles - 1];
        // guard against blow-ups on extreme automation
        for (int i = 0; i < 4; ++i) if (! std::isfinite (s[i])) reset();
    }
};

// ---------------------------------------------------------------- multimode voice filter (stereo)
class VoiceFilter
{
public:
    void prepare (double sampleRate, int maxCombSamples)
    {
        sr = (float) sampleRate;
        for (auto& c : comb) c.prepare (maxCombSamples);
        reset();
    }
    void setSampleRate (double sampleRate) { sr = (float) sampleRate; gNow = -1.0f; reset(); }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            lad[c].reset();
            for (auto& v : svf[c]) v.reset();
            one[c].reset();
            for (auto& v : formant[c]) v.reset();
            comb[c].clear();
            combLp[c] = 0.0f;
            fb[c] = 0.0f;
            dcHp[c].reset();
        }
    }

    // set per control step. The cutoff then glides sample by sample over the next `rampSamples` (the length of the
    // control step), so a fast sweep with high resonance is smooth instead of moving in 16-sample stairs
    void set (int filterType, float cutoffHz, float res, float driveAmt, float morphAmt, float feedbackAmt, bool volumeComp, int rampSamples = 16)
    {
        const bool typeChanged = filterType != type;
        type = filterType;
        cutoff = clampf (cutoffHz, 8.0f, sr * 0.45f);
        reso = clamp01 (res);
        drive = 1.0f + driveAmt * driveAmt * 8.0f;
        morph = clamp01 (morphAmt);
        feedback = clamp01 (feedbackAmt);
        comp = volumeComp;
        const float gNew = std::tan (kPi * cutoff / sr);
        if (gNow < 0.0f || typeChanged || rampSamples <= 1) { gNow = gNew; rampLeft = 0; }
        else { gStep = (gNew - gNow) / (float) rampSamples; rampLeft = rampSamples; }
        g = gNow;

        const auto t = (FilterType) type;
        const float q = 0.5f + reso * reso * 24.0f;
        switch (t)
        {
            case FilterType::LP12: case FilterType::HP12: case FilterType::BP: case FilterType::Notch:
            case FilterType::SVF: case FilterType::Morph:
                for (int c = 0; c < 2; ++c) svf[c][0].set (cutoff, q, sr);
                break;
            case FilterType::LP18:
                for (int c = 0; c < 2; ++c) { svf[c][0].set (cutoff, q, sr); one[c].setCutoff (cutoff, sr); }
                break;
            case FilterType::LP24: case FilterType::HP24:
                for (int c = 0; c < 2; ++c) { svf[c][0].set (cutoff, q, sr); svf[c][1].set (cutoff, 0.54f, sr); }
                break;
            case FilterType::LP36:
                for (int c = 0; c < 2; ++c) { svf[c][0].set (cutoff, q, sr); svf[c][1].set (cutoff, 0.52f, sr); svf[c][2].set (cutoff, 0.7f, sr); }
                break;
            case FilterType::Formant:
            {
                // vowel morph with formants shifted by cutoff (1 kHz = neutral)
                static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float v = morph * 4.0f; const int a = std::min ((int) v, 3); const float tt = v - (float) a;
                const float shift = cutoff / 1000.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float fc = lerpf (F[a][k], F[a + 1][k], tt) * clampf (shift, 0.25f, 4.0f);
                    for (int c = 0; c < 2; ++c) formant[c][k].set (fc, 4.0f + reso * 20.0f, sr);
                }
                break;
            }
            case FilterType::Comb:
                combDelay = clampf (sr / cutoff, 2.0f, (float) comb[0].buf.size() - 4.0f);
                break;
            default: break;
        }
        applyGlide();
        // loop high-pass coefficient for diode/acid voicing
        hpCoef = (t == FilterType::Acid) ? 1.0f - std::exp (-kTwoPi * 140.0f / sr)
               : (t == FilterType::Diode ? 1.0f - std::exp (-kTwoPi * 60.0f / sr) : 0.0f);
        for (int c = 0; c < 2; ++c) dcHp[c].setCutoff (12.0f, sr);
    }

    inline void process (float& l, float& r)
    {
        if (rampLeft > 0)
        {
            gNow += gStep;
            if (--rampLeft == 0) gNow = std::tan (kPi * cutoff / sr);   // land exactly on the target
            g = gNow;
            applyGlide();
        }
        l = tick (0, l);
        r = tick (1, r);
    }

private:
    float gNow = -1.0f, gStep = 0.0f;
    int rampLeft = 0;
    // the state-variable stages take the gliding g (their damping k stays as set): a few multiplies per sample
    static inline void svfG (Svf& f, float gg)
    {
        f.g = gg;
        f.a1 = 1.0f / (1.0f + gg * (gg + f.k));
        f.a2 = gg * f.a1;
        f.a3 = gg * f.a2;
    }
    void applyGlide()
    {
        const auto t = (FilterType) type;
        int stages = 0;
        switch (t)
        {
            case FilterType::LP12: case FilterType::HP12: case FilterType::BP: case FilterType::Notch:
            case FilterType::SVF: case FilterType::Morph: case FilterType::LP18: stages = 1; break;
            case FilterType::LP24: case FilterType::HP24: stages = 2; break;
            case FilterType::LP36: stages = 3; break;
            default: break;
        }
        for (int c = 0; c < 2; ++c) for (int k = 0; k < stages; ++k) svfG (svf[c][k], gNow);
    }
    float sr = 48000.0f, cutoff = 1000.0f, reso = 0.0f, drive = 1.0f, morph = 1.0f, feedback = 0.0f, g = 0.1f, hpCoef = 0.0f, combDelay = 100.0f;
    int type = 0;
    bool comp = true;
    ZdfLadder lad[2];
    Svf svf[2][3];
    TptOnePole one[2];
    Svf formant[2][3];
    DelayLine comb[2];
    float combLp[2] {};
    float fb[2] {};
    TptOnePole dcHp[2];

    inline float tick (int c, float x)
    {
        const float dry = x;
        if (feedback > 0.0f && type != (int) FilterType::Comb)
            x += fastTanh (fb[c] * 1.5f) * feedback * 0.9f;
        const auto t = (FilterType) type;
        float y = 0.0f;
        switch (t)
        {
            case FilterType::LP12: { auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive); y = f.lp; break; }
            case FilterType::LP18: { auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive); y = one[c].lp (f.lp); break; }
            case FilterType::LP24: { svf[c][0].tick (fastTanh (x * drive) / drive); svf[c][1].tick (svf[c][0].lp); y = svf[c][1].lp; break; }
            case FilterType::LP36: { svf[c][0].tick (fastTanh (x * drive) / drive); svf[c][1].tick (svf[c][0].lp); svf[c][2].tick (svf[c][1].lp); y = svf[c][2].lp; break; }
            case FilterType::HP12: { auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive); y = f.hp; break; }
            case FilterType::HP24: { svf[c][0].tick (fastTanh (x * drive) / drive); svf[c][1].tick (svf[c][0].hp); y = svf[c][1].hp; break; }
            case FilterType::BP: { auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive); y = f.bp * f.k; break; }
            case FilterType::Notch: { auto& f = svf[c][0]; f.tick (x); y = f.notch(); break; }
            case FilterType::SVF:
            {
                auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive);
                const float m = morph * 2.0f;
                y = m < 1.0f ? lerpf (f.lp, f.bp * f.k, m) : lerpf (f.bp * f.k, f.hp, m - 1.0f);
                break;
            }
            case FilterType::Morph:
            {
                auto& f = svf[c][0]; f.tick (fastTanh (x * drive) / drive);
                const float m = morph * 3.0f; const int seg = std::min ((int) m, 2); const float tt = m - (float) seg;
                const float o[4] { f.lp, f.bp * f.k, f.hp, f.notch() };
                y = lerpf (o[seg], o[seg + 1], tt);
                break;
            }
            case FilterType::Ladder: case FilterType::Cascade:
            {
                float st[4];
                const bool cascade = t == FilterType::Cascade;
                const float k = reso * (cascade ? 3.6f : 4.1f);
                float in = x;
                if (comp) in *= 1.0f + reso * (cascade ? 1.2f : 2.0f);   // bass loss compensation
                lad[c].process (in, g, k, 4, cascade, 0.0f, drive, st);
                // slope / tap selection: 6, 12, 18, 24 dB
                const float m = morph * 3.0f; const int seg = std::min ((int) m, 2); const float tt = m - (float) seg;
                y = lerpf (st[seg], st[seg + 1], tt);
                break;
            }
            case FilterType::Diode: case FilterType::Acid:
            {
                float st[4];
                const bool acid = t == FilterType::Acid;
                const float k = reso * (acid ? 7.6f : 6.8f);
                float in = acid ? x + 0.08f * x * x : x;   // slight asymmetry of the transistor input pair
                if (comp) in *= 1.0f + reso * (acid ? 1.4f : 1.0f);
                lad[c].process (in, g, k, 3, true, hpCoef, drive * (acid ? 1.4f : 1.0f), st);
                y = lerpf (st[1], st[2], 0.35f + morph * 0.65f);
                break;
            }
            case FilterType::Comb:
            {
                const float d = comb[c].read (combDelay);
                combLp[c] += (1.0f - morph * 0.9f) * (d - combLp[c]);       // damping via morph
                const float polarity = 1.0f;
                const float fbk = (0.2f + reso * 0.78f) * polarity;
                const float w = fastTanh ((x * drive + combLp[c] * fbk) * 0.9f) / 0.9f;
                comb[c].push (w);
                y = w * 0.6f;
                break;
            }
            case FilterType::Formant:
            {
                float sum = 0.0f;
                const float gains[3] { 1.0f, 0.7f, 0.45f };
                const float xin = fastTanh (x * drive) / drive;
                for (int k = 0; k < 3; ++k) { formant[c][k].tick (xin); sum += formant[c][k].bp * formant[c][k].k * gains[k]; }
                y = sum * 1.2f;
                break;
            }
            default: y = x; break;
        }
        if (! std::isfinite (y)) { reset(); y = 0.0f; }
        fb[c] = y;
        (void) dry;
        return y;
    }
};
} // namespace be

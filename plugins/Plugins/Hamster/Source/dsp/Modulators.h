#pragma once
#include "Common.h"

#include "../Params.h"

namespace be
{
// ---------------------------------------------------------------- drawable shape (multi-stage envelope / custom LFO)
struct ShapePoint { float x = 0, y = 0, curve = 0; };

struct ShapeLut
{
    static constexpr int N = 512;
    float v[N + 1] {};
    inline float at (float p) const   // p in [0,1]
    {
        const float f = clamp01 (p) * (float) N;
        const int i = std::min ((int) f, N - 1);
        const float t = f - (float) i;
        return v[i] + (v[i + 1] - v[i]) * t;
    }
    static std::shared_ptr<ShapeLut> fromPoints (const std::vector<ShapePoint>& pts);
};

juce::String shapeToString (const std::vector<ShapePoint>& pts);
std::vector<ShapePoint> shapeFromString (const juce::String& s);
std::vector<ShapePoint> defaultEnvShape();
std::vector<ShapePoint> defaultLfoShape();

inline float curveFn (float t, float c)
{
    t = clamp01 (t);
    if (std::abs (c) < 1.0e-3f) return t;
    const float k = c * 7.0f;
    return (1.0f - std::exp (-k * t)) / (1.0f - std::exp (-k));
}

// ---------------------------------------------------------------- envelope settings snapshot (per block)
struct EnvSettings
{
    float delay = 0, attack = 0.002f, hold = 0, decay = 0.3f, sustain = 0.7f, release = 0.1f;
    float aCurve = 0, dCurve = 0.5f, rCurve = 0.5f;
    bool drawn = false, loop = false, retrig = false, sync = false;
    float velSens = 0, keyScale = 0, drawLen = 1.0f;
    const ShapeLut* lut = nullptr;
};

class Envelope
{
public:
    enum Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    void setRate (float controlRate) { cr = controlRate; }
    void noteOn (const EnvSettings& s, float velocity, int note, bool legato, float attackJitter)
    {
        if (legato && ! s.retrig && stage != Idle && stage != Release) return;
        vel = velocity; key = note; jitter = attackJitter;
        startLevel = level;
        t = 0.0f;
        drawnPos = 0.0f;
        stage = s.delay > 0.0f ? Delay : Attack;
    }
    void noteOff()
    {
        if (stage != Idle && stage != Release) { stage = Release; releaseStart = level; t = 0.0f; }
    }
    void kill() { stage = Idle; level = 0.0f; }
    bool active() const { return stage != Idle; }
    bool isReleasing() const { return stage == Release; }
    float value() const { return out; }

    // advance one control step; bpm used when synced
    float tick (const EnvSettings& s, double bpm)
    {
        const float tscale = (s.sync ? (float) (60.0 / std::max (bpm, 1.0)) : 1.0f) * std::exp2 (-s.keyScale * (float) (key - 60) / 24.0f);
        const float dt = 1.0f / cr;
        auto stageTime = [tscale] (float v) { return std::max (v * tscale, 1.0e-4f); };

        if (s.drawn && s.lut != nullptr && stage != Idle && stage != Release && stage != Delay)
        {
            const float len = std::max (0.005f, s.drawLen * tscale);
            drawnPos += dt / len;
            if (drawnPos >= 1.0f) drawnPos = s.loop ? drawnPos - std::floor (drawnPos) : 1.0f;
            level = s.lut->at (drawnPos);
        }
        else
        {
            switch (stage)
            {
                case Idle: level = 0.0f; break;
                case Delay:
                    t += dt;
                    if (t >= stageTime (s.delay)) { stage = Attack; t = 0.0f; startLevel = level; }
                    break;
                case Attack:
                {
                    const float at = stageTime (s.attack * jitter);
                    t += dt;
                    const float p = t / at;
                    level = startLevel + (1.0f - startLevel) * curveFn (p, -s.aCurve);
                    if (p >= 1.0f) { level = 1.0f; stage = s.hold > 0.0f ? Hold : Decay; t = 0.0f; }
                    break;
                }
                case Hold:
                    t += dt;
                    level = 1.0f;
                    if (t >= stageTime (s.hold)) { stage = Decay; t = 0.0f; }
                    break;
                case Decay:
                {
                    t += dt;
                    const float p = t / stageTime (s.decay);
                    const float target = s.loop ? 0.0f : s.sustain;
                    level = target + (1.0f - target) * (1.0f - curveFn (p, s.dCurve));
                    if (p >= 1.0f)
                    {
                        if (s.loop) { stage = Attack; startLevel = level; t = 0.0f; }
                        else { stage = Sustain; level = s.sustain; }
                    }
                    break;
                }
                case Sustain: level = s.sustain; break;
                case Release:
                {
                    t += dt;
                    const float p = t / stageTime (s.release);
                    level = releaseStart * (1.0f - curveFn (p, s.rCurve));
                    if (p >= 1.0f) { level = 0.0f; stage = Idle; }
                    break;
                }
            }
        }
        if (stage == Release && s.drawn) { /* release handled above using releaseStart */ }
        out = level * (1.0f - s.velSens + s.velSens * vel);
        return out;
    }

private:
    Stage stage = Idle;
    float level = 0.0f, startLevel = 0.0f, releaseStart = 0.0f, t = 0.0f, cr = 3000.0f, vel = 1.0f, out = 0.0f;
    float drawnPos = 0.0f, jitter = 1.0f;
    int key = 60;
};

// ---------------------------------------------------------------- LFO
struct LfoSettings
{
    int wave = 0;
    float rate = 1.0f;
    bool sync = false;
    double divBeats = 1.0;
    int trigger = 1;          // Free, Note, Legato
    bool oneShot = false;
    float phase = 0.0f, offset = 0.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    bool global = false, unipolar = false;
    float smooth = 0.0f, depth = 1.0f;
    const ShapeLut* lut = nullptr;
};

class Lfo
{
public:
    void setRate (float controlRate) { cr = controlRate; }
    void seed (uint32_t s) { rng.seed (s); shCur = rng.bi(); shNext = rng.bi(); }
    void trigger (const LfoSettings& s, bool legatoNote)
    {
        if (s.trigger == 0) return;
        if (s.trigger == 2 && legatoNote) return;
        phase = 0.0;
        finished = false;
        fadeT = 0.0f;
        released = false;
    }
    void release() { released = true; relT = 0.0f; }

    // rateMod: multiplier from modulation, depthMod: added to depth. hostPhaseBeats: for synced free-running LFOs
    float tick (const LfoSettings& s, double bpm, float rateMul, float depthAdd, bool hostLocked, double hostBeats)
    {
        const float dt = 1.0f / cr;
        double freq = s.sync ? (bpm / 60.0) / std::max (s.divBeats, 1.0e-3) : (double) s.rate;
        freq *= (double) rateMul;
        double ph;
        if (s.trigger == 0 && s.sync && hostLocked)
        {
            ph = std::fmod (hostBeats / std::max (s.divBeats, 1.0e-3), 1.0);
            if (ph < prevPh) wrap();
            prevPh = ph;
        }
        else
        {
            if (! finished) phase += freq * dt;
            if (phase >= 1.0)
            {
                if (s.oneShot) { phase = 1.0; finished = true; }
                else { phase -= std::floor (phase); wrap(); }
            }
            ph = phase;
        }
        double p = ph + (double) s.phase;
        p -= std::floor (p);
        if (finished) p = 0.9999;
        const float x = (float) p;

        float v = 0.0f;
        switch ((LfoWave) s.wave)
        {
            case LfoWave::Sine: v = std::sin (kTwoPi * x); break;
            case LfoWave::Triangle: v = x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f); break;
            case LfoWave::Square: v = x < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::SawUp: v = 2.0f * x - 1.0f; break;
            case LfoWave::SawDown: v = 1.0f - 2.0f * x; break;
            case LfoWave::SampleHold: v = shCur; break;
            case LfoWave::SmoothRandom: { const float k = 0.5f - 0.5f * std::cos (kPi * x); v = lerpf (shCur, shNext, k); break; }
            case LfoWave::Drawn: v = s.lut != nullptr ? s.lut->at (x) * 2.0f - 1.0f : 0.0f; break;
        }
        if (s.unipolar) v = 0.5f * (v + 1.0f);
        v += s.offset;

        float amp = clamp01 (s.depth + depthAdd);
        if (s.fadeIn > 0.0f) { fadeT += dt; amp *= clamp01 (fadeT / s.fadeIn); }
        if (released && s.fadeOut > 0.0f) { relT += dt; amp *= 1.0f - clamp01 (relT / s.fadeOut); }
        v *= amp;

        if (s.smooth > 0.0f)
        {
            const float a = std::exp (-1.0f / (cr * (0.001f + s.smooth * 0.25f)));
            sm = v + a * (sm - v);
            v = sm;
        }
        else sm = v;
        out = v;
        return v;
    }
    float value() const { return out; }
    float currentPhase() const { return (float) phase; }

private:
    double phase = 0.0, prevPh = 0.0;
    float cr = 3000.0f, fadeT = 0.0f, relT = 0.0f, sm = 0.0f, out = 0.0f;
    float shCur = 0.0f, shNext = 0.0f;
    bool finished = false, released = false;
    Rng rng;
    void wrap() { shCur = shNext; shNext = rng.bi(); }
};
} // namespace be

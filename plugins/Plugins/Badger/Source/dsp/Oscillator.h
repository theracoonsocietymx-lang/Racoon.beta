#pragma once
#include "Common.h"
#include "Wavetables.h"
#include "../Params.h"

namespace be
{
// Engine-specific macro parameter labels & defaults (A, B, C)
struct EngineParamInfo { const char* a; const char* b; const char* c; float da, db, dc; };
const EngineParamInfo& engineParamInfo (int engine);

// static settings captured once per block
struct OscSettings
{
    bool on = false;
    int engine = 0, vaWave = 2;
    const WaveTable* table = nullptr;        // wavetable / additive / spectral / VA custom source
    const SampleData* sample = nullptr;      // sample / granular source
    int unison = 1;
    float spread = 0.6f, blend = 0.7f, drift = 0.1f, slop = 0.0f;
    int syncMode = 0;
    float syncRatio = 1.0f;
    int xmodType = 0, xmodSrc = 0;
    float randPhase = 0.0f, startPhase = 0.0f;
    bool reset = true;
    float keyTrack = 1.0f;
    float pc = 0.0f;
};

// per control-step modulated values
struct OscMod
{
    float pitchSemis = 0.0f;   // total pitch (note + offsets), keytrack applied by caller
    float level = 0.75f, pan = 0.0f, pos = 0.0f, pa = 0.5f, pb = 0.5f, detune = 0.2f, fold = 0.0f, xmod = 0.0f, shape = 0.0f;
};

class Oscillator
{
public:
    void prepare (double maxSampleRate);          // allocates
    void setSampleRate (double r) { sr = r; }   // no allocation
    void noteOn (const OscSettings& s, float noteHz, uint32_t seed, bool legato);
    void setControl (const OscSettings& s, const OscMod& m, double a4, float imperfectionDrift);
    // render one sample: returns stereo, also writes mono for x-mod use
    inline void tick (const OscSettings& s, float xmodIn, float& outL, float& outR);
    float lastMono() const { return mono; }

private:
    double sr = 96000.0;
    float baseInc = 0.0f;
    float level = 0.0f, pan = 0.0f, pos = 0.0f, pa = 0.5f, pb = 0.5f, fold = 0.0f, shapeAmt = 0.0f, xmodAmt = 0.0f, detune = 0.0f;
    float targetLevel = 0.0f;
    float mono = 0.0f;
    int voices = 1;

    struct Uni
    {
        float phase = 0.0f, master = 0.0f, inc = 0.0f, gainL = 1.0f, gainR = 1.0f;
        float dir = 1.0f;
        float driftVal = 0.0f, driftTarget = 0.0f, slopCents = 0.0f;
        float fmMod = 0.0f, fmPhase = 0.0f;
        float lp = 0.0f;
        // sample playback
        double play = 0.0;
        // physical model
        DelayLine ks;
        float ksLp = 0.0f, ksPrev = 0.0f, bowNoise = 0.0f;
        Svf body1, body2;
        int exciteLeft = 0;
        Rng rng;
    } uni[kMaxUnison];
    std::vector<float> exScratch;   // the pluck of the physical engine (sized in prepare)

    // granular
    struct Grain { bool on = false; double pos = 0.0; double rate = 1.0; int len = 0, t = 0; float panL = 0.7f, panR = 0.7f; };
    Grain grains[24];
    float grainClock = 0.0f;
    Rng grng;
    int level0 = 0;

    float renderUnison (const OscSettings& s, Uni& u, float pmIn);
    void spawnGrain (const OscSettings& s);
    void exciteString (const OscSettings& s, Uni& u, float noteHz);
};

// ------------------------------------------------------------------ inline render
inline void Oscillator::tick (const OscSettings& s, float xmodIn, float& outL, float& outR)
{
    outL = outR = 0.0f;
    if (! s.on) { mono = 0.0f; return; }
    level += 0.002f * (targetLevel - level);

    float pmIn = 0.0f, amMul = 1.0f;
    switch ((XModType) s.xmodType)
    {
        case XModType::FM: pmIn = xmodIn * xmodAmt * xmodAmt * 2.5f; break;
        case XModType::AM: amMul = 1.0f + xmodIn * xmodAmt; break;
        case XModType::Ring: amMul = lerpf (1.0f, xmodIn, xmodAmt); break;
        default: break;
    }

    const auto engine = (Engine) s.engine;
    if (engine == Engine::Granular)
    {
        // grain scheduler: density from Param B (8 .. 64 grains/s)
        const float density = 8.0f * std::exp2 (pb * 3.0f);
        grainClock += density / (float) sr;
        if (grainClock >= 1.0f) { grainClock -= 1.0f; spawnGrain (s); }
        float l = 0.0f, r = 0.0f;
        if (s.sample != nullptr && ! s.sample->data.empty())
        {
            const auto& d = s.sample->data;
            const int n = (int) d.size();
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) g.t / (float) g.len);
                const int i0 = ((int) g.pos % n + n) % n;
                const float fr = (float) (g.pos - std::floor (g.pos));
                const float v = d[(size_t) i0] + (d[(size_t) ((i0 + 1) % n)] - d[(size_t) i0]) * fr;
                l += v * w * g.panL; r += v * w * g.panR;
                g.pos += g.rate;
                if (++g.t >= g.len) g.on = false;
            }
        }
        const float norm = 0.6f;
        mono = (l + r) * 0.5f * norm * amMul;
        outL = l * norm * amMul * level;
        outR = r * norm * amMul * level;
        return;
    }

    float sumL = 0.0f, sumR = 0.0f, sumM = 0.0f;
    for (int v = 0; v < voices; ++v)
    {
        auto& u = uni[v];
        float y = renderUnison (s, u, pmIn);
        // post shaping
        if (fold > 0.001f) y = std::sin (y * (1.0f + fold * 5.0f) * kPi * 0.5f);
        if (shapeAmt > 0.001f) { const float g = 1.0f + shapeAmt * 8.0f; y = fastTanh (y * g + shapeAmt * 0.2f) - fastTanh (shapeAmt * 0.2f); }
        sumL += y * u.gainL;
        sumR += y * u.gainR;
        sumM += y;
    }
    const float norm = voices > 1 ? 1.0f / std::sqrt ((float) voices) : 1.0f;
    mono = sumM * norm * amMul;
    outL = sumL * norm * amMul * level;
    outR = sumR * norm * amMul * level;
}
} // namespace be

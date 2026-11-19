#pragma once
#include "Common.h"
#include "Filters.h"
#include "RacoonDsp.h"
#include "Modulators.h"
#include "Assets.h"
#include "../Params.h"

namespace be
{
// ---------------------------------------------------------------- per-hit information
struct HitInfo
{
    float velocity = 0.8f;      // 0..1 after velocity curve
    float accent = 0.0f;        // 0..1 accent amount (0 = no accent)
    float pitch = 0.0f;         // semitone offset from the step / MIDI
    int gateSamples = -1;       // -1 = no gate (AD/AHD), otherwise note length
    uint32_t seed = 1;
    int rrIndex = 0;            // round robin counter
};

// samples available to a track
struct TrackAssets
{
    const SampleData* factory = nullptr;                  // selected factory sample (if any)
    const SampleData* slots[kNumSlots] {};                // user multisample slots
    const SampleData* roleDefault = nullptr;              // PCM used by Circuit Morph
};

// continuous parameters that follow modulation while a voice sounds
constexpr int kContParams[] { T_LEVEL, T_PAN, T_TUNE, T_FINE, T_A_LEVEL, T_B_LEVEL, T_B_MORPH, T_C_LEVEL, T_C_PITCH, T_D_LEVEL, T_D_FREQ,
                              T_F_CUTOFF, T_F_RES, T_F_MIX };
constexpr int kNumCont = (int) (sizeof (kContParams) / sizeof (kContParams[0]));

// ---------------------------------------------------------------- analog circuit models (layer A)
class AnalogCircuit
{
public:
    void prepare (float sampleRate) { sr = sampleRate; }
    void configure (int role, int model, const float* p8, float decayScale, float drift, Rng& rng);
    // returns mid, adds stereo side component
    float tick (float pitchMul, float& side);
    bool silent() const { return age > minLen && envMax < 1.0e-4f; }
private:
    float sr = 48000.0f;
    int age = 0, minLen = 0;
    float envMax = 1.0f;
    // body oscillators
    struct Osc { float f0 = 0, f1 = 0, level = 0, env = 0, coef = 1, tri = 0; double ph = 0; float sweep = 1, sweepCoef = 1; };
    Osc body, body2, sub;
    float bodyDrive = 0.0f;
    float bodyX = 0.0f, driveX = 0.0f;   // ADAA memory of the two drives
    // transient / punch
    float punch = 0, punchEnv = 0, punchCoef = 1;
    // noise
    struct Noise
    {
        float level = 0, env = 0, coef = 1, attackCoef = 0, attackEnv = 1, hz = 1000, hzEnd = -1, hzEnv = 0, hzCoef = 1, q = 0.7f;
        int mode = 2;            // 0 LP, 1 BP, 2 HP, 3 BP+HP
        int bursts = 1; int burstSpacing = 0; float burstCoef = 1, tailCoef = 1, tailLevel = 0, tailEnv = 0;
        float grain = 0, side = 0;
        Svf f, fS, hp, hpS;
        float pinkB[3] {};
    } noise;
    // click
    float clickLevel = 0, clickEnv = 0, clickCoef = 1;
    Svf clickF;
    // metallic square bank
    struct Metal { float level = 0, env = 0, coef = 1, side = 0; float f[6] {}; double ph[6] {}; int count = 0; Svf bp, bpS, hp, hpS; float bpHz = 8000, hpHz = 6000, q = 1; } metal;
    // sine partials (bells, membranes, rims, metal percussion)
    struct Partials { int count = 0; float f[6] {}, a[6] {}, env[6] {}, coef[6] {}; double ph[6] {}; float fm = 0, side = 0; } part;
    float tone = 20000.0f, drive = 0.0f, outGain = 1.0f;
    TptOnePole toneL;
    Rng localRng;
    float pitchJitter = 1.0f;
    float coefFor (float tau) const { return std::exp (-1.0f / (std::max (tau, 1.0e-4f) * sr)); }
    float nextNoise() { return localRng.bi(); }
};

// ---------------------------------------------------------------- FM 6-operator layer (layer B)
class FmLayer
{
public:
    void prepare (float sampleRate) { sr = sampleRate; }
    void configure (const float* p, float baseHz, float decayScale);
    float tick (float pitchMul, float morph);
    bool silent() const { return envMax < 1.0e-4f; }
private:
    float sr = 48000.0f, base = 100.0f, fb = 0.0f, fbState = 0.0f, envMax = 1.0f;
    int algo = 0;
    float ratio[6] {}, level[6] {}, env[6] {}, coef[6] {};
    double ph[6] {};
};

// ---------------------------------------------------------------- sample layer (layer C)
class SampleLayer
{
public:
    void prepare (float sampleRate) { sr = sampleRate; racoon::sdsp::SincTable::get(); }
    const SampleData* playing() const { return inUse.load (std::memory_order_acquire); }   // any thread
    void configure (const float* p, const TrackAssets& a, const HitInfo& h, Rng& rng, bool forceRoleDefault);
    void tick (float rateMul, float& l, float& r);
    bool silent() const { return done; }
private:
    float sr = 48000.0f;
    const SampleData* s = nullptr;
    std::atomic<const SampleData*> inUse { nullptr };   // what this layer last played, for the message thread (see AssetSlot)
    double pos = 0.0, regionStart = 0.0, regionEnd = 0.0, srRatio = 1.0;
    bool reverse = false, loop = false, done = true, stretch = false;
    float stretchFactor = 1.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    double played = 0.0;
    // granular stretch
    struct Grain { double start = 0, pos = 0; int age = 0; bool on = false; };
    Grain grains[2];
    int grainLen = 1920, grainHop = 960, sinceSpawn = 0, nextGrain = 0;
    double readHead = 0.0;
    void read (double at, float& l, float& r) const;
    int readBand = 0;   // which sinc kernel the reads use (it narrows when the sample is pitched up)
};

// ---------------------------------------------------------------- texture layer (layer D)
class TextureLayer
{
public:
    void prepare (float sampleRate) { sr = sampleRate; comb.prepare ((int) (sampleRate / 20.0f) + 8); }
    void configure (const float* p, Rng& rng);
    float tick (float pitchMul, float freq);
    bool silent() const { return env < 1.0e-4f && age > 64; }
private:
    float sr = 48000.0f;
    int type = 0, age = 0;
    float env = 0, coef = 1, q = 0.3f, tone = 0.0f;
    float pink[3] {};
    double mph[6] {};
    int grainLeft = 0; float grainGain = 0;
    Svf f;
    DelayLine comb;
    float combLp = 0.0f;
    Rng rng;
    int filterCounter = 0;
    float lastHz = -1.0f;
};

// ---------------------------------------------------------------- full drum voice
class DrumVoice
{
public:
    void prepare (double sampleRate);
    // p: latched real parameter values (locks applied), lockMask marks locked params
    void start (int track, const float* p, const HitInfo& h, const TrackAssets& assets, int delaySamples, float circuitW[4]);
    void setContinuous (const float* contValues) { std::copy (contValues, contValues + kNumCont, cont); }
    const SampleData* sampleInUse() const { return smp.playing(); }   // any thread
    void noteOff() { gate = 0; if (envMode == (int) EnvMode::ADSR && stage != Idle && stage != Release) beginRelease(); }
    void choke (float ms = 3.0f);
    void render (float* l, float* r, int n);
    bool isActive() const { return stage != Idle; }
    int getTrack() const { return track; }
    juce::int64 startedAt = 0;
    float lockNorm[T_COUNT] {};
    bool locked[T_COUNT] {};
    float level() const { return lastPeak; }

private:
    enum Stage { Idle, Waiting, Attack, Hold, Decay, Sustain, Release, Choke };
    float sr = 48000.0f;
    int track = 0;
    Stage stage = Idle;
    int delay = 0, gate = -1, envMode = 1;
    float envLevel = 0.0f, envT = 0.0f, attack = 0.0f, hold = 0.0f, decay = 1.0f, sustain = 0.7f, release = 0.1f, curve = 0.5f;
    float releaseStart = 0.0f, chokeCoef = 0.0f;
    float velGain = 1.0f, accentGain = 1.0f;
    float cont[kNumCont] {};
    // layers
    bool aOn = false, bOn = false, cOn = false, dOn = false, morphOn = false;
    float morphW[4] {};
    AnalogCircuit analogA, analogB;
    FmLayer fm;
    SampleLayer smp;
    TextureLayer tex;
    float stepPitch = 0.0f, penvAmt = 0.0f, penv = 0.0f, penvCoef = 1.0f;
    float fenvAmt = 0.0f, fenv = 0.0f, fenvCoef = 1.0f, keyTrack = 0.0f, fDrive = 0.0f;
    int fType = 0;
    VoiceFilter filter;
    int filterCounter = 0;
    float panL = 0.7071f, panR = 0.7071f;
    Rng rng;
    float lastPeak = 0.0f, silentRun = 0.0f;
    int age = 0;
    void beginRelease() { stage = Release; releaseStart = envLevel; envT = 0.0f; }
    float envTick();
};

int mapFilterType (int choiceIndex);   // track filter choice -> FilterType (or -1 = off)
} // namespace be

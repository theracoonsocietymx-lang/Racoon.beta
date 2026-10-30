#pragma once
#include "RacoonProcessor.h"
#include <complex>

// The engines of Skunk, each one a small class that knows nothing about parameters: the processor feeds them
// already modulated values every 32 samples.
namespace racoon::skunk
{
using cpx = std::complex<float>;

// ----------------------------------------------------------------------------------------------- small helpers
struct Rand
{
    juce::uint32 s = 1;
    void seed (juce::uint32 v) { s = v == 0 ? 0x9e3779b9u : v; }
    juce::uint32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() & 0xffffff) / 16777216.0f; }   // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }
};

struct OnePole
{
    float z = 0.0f;
    float lp (float x, float a) { z = x + (z - x) * a; return z; }
};

// topology-preserving state variable filter
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    // mode 0 low, 1 band, 2 high, 3 notch
    float process (float x, float g, float k, int mode)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        switch (mode) { case 0: return v2; case 1: return v1; case 2: return x - k * v1 - v2; default: return x - k * v1; }
    }
    static float gFor (float hz, double sr) { return std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, (float) (sr * 0.45), hz) / (float) sr); }
};

// circular stereo buffer with fractional reads
struct Ring
{
    juce::AudioBuffer<float> buf;
    int size = 1, write = 0;
    void prepare (int n) { size = juce::jmax (16, n); buf.setSize (2, size); buf.clear(); write = 0; }
    void clear() { buf.clear(); write = 0; }
    void push (float l, float r) { buf.setSample (0, write, l); buf.setSample (1, write, r); write = (write + 1) % size; }
    float at (int c, double pos) const   // absolute index (wraps)
    {
        while (pos < 0) pos += size;
        const int i = (int) pos % size;
        const float f = (float) (pos - std::floor (pos));
        const float a = buf.getSample (c, i), b = buf.getSample (c, (i + 1) % size);
        return a + (b - a) * f;
    }
    float back (int c, double delay) const { return at (c, (double) write - delay); }   // delay in samples behind the write head
};

float lfoWave (int shape, double phase, float rnd);   // shape: sine, tri, saw up, saw down, square, random (rnd held)
juce::StringArray lfoShapeNames();

// ----------------------------------------------------------------------------------------------- granular engine
enum class Window { hann = 0, gauss, tukey, rectSmooth };
float window (Window, float t);   // t 0..1

struct GrainParams
{
    double sizeMs = 80.0, density = 20.0, position = 0.2, sprayMs = 20.0, pitch = 0.0, randPitch = 0.0, randPitchProb = 0.0;
    double reverseProb = 0.0, jitter = 0.1, spread = 0.5, skipProb = 0.0, jumpProb = 0.0;
    Window win = Window::hann;
    int maxGrains = 128;
    bool live = true;          // positions measured back from the write head (live) or across the frozen buffer
    double scan = 0.0;         // frozen buffer: how far the scan point has moved (0..1)
};

class Granular
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& g : grains) g.active = false; untilNext = 0.0; }
    // renders n samples (added to out L/R) reading from `src`, which holds `valid` samples
    void render (const Ring& src, int valid, const GrainParams&, float* outL, float* outR, int n, Rand&);
    void burst (const Ring& src, int valid, const GrainParams&, int count, Rand&);   // transient triggered grains
    int activeCount() const { int c = 0; for (auto& g : grains) c += g.active ? 1 : 0; return c; }
    // for the screen: where the grains read (0..1 of the buffer, newest at 1 in live mode)
    std::atomic<float> uiPos[32] {};
private:
    struct Grain { bool active = false, reverse = false; double start = 0, ratio = 1, len = 1, age = 0; float panL = 1, panR = 1, amp = 1; };
    std::array<Grain, 256> grains;
    double sr = 48000.0, untilNext = 0.0;
    void spawn (const Ring& src, int valid, const GrainParams&, Rand&);
};

// ----------------------------------------------------------------------------------------------- spectral engine
struct SpectralParams
{
    bool freeze = false;
    float freezeMix = 1.0f, decay = 0.0f, blur = 0.0f, harmonicHold = 0.0f, phaseEvolution = 1.0f, texture = 0.0f;
    float shiftHz = 0.0f, stretch = 1.0f, dispersion = 0.0f;
    float lowHz = 20.0f, highHz = 20000.0f;
    float pitch = 0.0f;            // semitones, applied to the spectrum
    bool keepFormants = false;
    int morphMode = 0;             // 0 off, 1 crossfade, 2 envelope transfer, 3 harmonic blend, 4 noise transfer
    float morph = 0.0f;
};

class Spectral
{
public:
    static constexpr int maxOrder = 13;   // 8192
    void prepare (double sampleRate, int order);
    void setOrder (int order);            // 9..13; resets
    int latency() const { return n; }
    int order() const { return ord; }
    void reset();
    // per sample: main input L/R, morph source L/R; returns processed L/R (late by `latency()`)
    void process (float inL, float inR, float scL, float scR, float& outL, float& outR, const SpectralParams&, Rand&);
    std::atomic<float> uiSpectrum[64] {};   // log-spaced magnitudes for the screen
private:
    int ord = 11, n = 2048, hop = 512;
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, work, workSc;
    std::vector<float> in[2], sc[2], out[2];
    std::vector<float> frozenMag[2], phase[2], heldMag[2], lastMag[2];
    std::vector<cpx> spec, specSc;
    std::vector<float> tmpMag, tmpFreq, tmpEnv, tmpEnvSc;   // scratch, sized once
    int pos = 0, count = 0;
    bool wasFrozen = false;
    float frozenEnergy[2] { 0.0f, 0.0f };
    void frame (int c, const SpectralParams&, Rand&);
};

// ----------------------------------------------------------------------------------------------- glitch engine
struct GlitchParams
{
    double divisionBeats = 0.25, bpm = 120.0;
    float probability = 0.0f;
    int durationDivs = 2;
    float wRepeat = 1.0f, wStutter = 0.0f, wReverse = 0.0f, wTapeStop = 0.0f, wShuffle = 0.0f, wDropout = 0.0f;
    float tapeStopMs = 300.0f, smoothMs = 4.0f;
};

class Glitch
{
public:
    void prepare (double sampleRate);
    void reset();
    // ppq of this sample (for the division grid), running = transport moving
    void process (float& l, float& r, double ppq, bool running, const GlitchParams&, Rand&);
    std::atomic<int> uiType { -1 };   // -1 none, 0 repeat, 1 stutter, 2 reverse, 3 tape stop, 4 shuffle, 5 dropout
private:
    Ring hist;
    double sr = 48000.0;
    int type = -1;
    double eventStart = 0, eventLen = 0, sliceLen = 0, readBase = 0, tapePos = 0, tapeSpeed = 1;
    double elapsed = 0;
    juce::int64 lastDiv = std::numeric_limits<juce::int64>::min();
    float fade = 1.0f;
    bool dropMute = false;
    int dropLeft = 0;
};

// ----------------------------------------------------------------------------------------------- noise generator
class Noise
{
public:
    void prepare (double sampleRate) { sr = sampleRate; }
    // type: 0 white, 1 pink, 2 brown, 3 impulses, 4 modulated
    float next (int type, float color, Rand&);
private:
    double sr = 48000.0;
    float b[7] {}, brown = 0.0f, lp = 0.0f, modPhase = 0.0f, mod = 0.0f;
};

// ----------------------------------------------------------------------------------------------- creative filter bank
class FilterBank
{
public:
    void prepare (double sampleRate) { sr = sampleRate; comb[0].prepare ((int) (sampleRate / 20.0) + 8); comb[1].prepare ((int) (sampleRate / 20.0) + 8); reset(); }
    void reset() { for (auto& s : svf) s.reset(); for (auto& f : form) for (auto& s : f) s.reset(); comb[0].clear(); comb[1].clear(); for (auto& r : res) for (auto& s : r) s.reset(); }
    // type: 0 low, 1 band, 2 high, 3 notch, 4 formant (vowel 0..4), 5 comb, 6 resonators
    void process (float& l, float& r, int type, float freq, float reso, float vowel, float mix);
private:
    double sr = 48000.0;
    Svf svf[2], form[2][3], res[2][4];
    Ring comb[2];
};

// ----------------------------------------------------------------------------------------------- the FX rack
class FxRack
{
public:
    void prepare (double sampleRate);
    void reset();
    // kind: 1 distortion, 2 chorus, 3 phaser, 4 delay, 5 reverb; a, b, mix are 0..1 (delay time in beats in `beats`)
    void process (int kind, float* l, float* r, int n, float a, float b, float mix, double bpm, double beats);
private:
    double sr = 48000.0;
    Ring chorus, delay;
    double chPhase = 0.0, phPhase = 0.0;
    float apState[2][8] {};
    float phFb[2] {};
    float delFb[2] {};
    racoon::sdsp::Verb verb;
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> distSat[2];
    float dcIn[2] {}, dcOut[2] {};
};
}

#pragma once
#include "RacoonProcessor.h"

// The pieces of Hedgehog: loudness and true peak meters (EBU R128 / ITU-R BS.1770), the dynamic EQ, the multiband
// compressor, the resonance suppressor, exciter, imager, low end, transients, match EQ and the true-peak limiter.
namespace racoon::hedgehog
{
constexpr int numEqBands = 12, numMbBands = 6, numThirds = 31;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    enum Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf, allpass };
    void set (Type, double sampleRate, double hz, double q, double gainDb = 0.0);
    float magnitudeDb (double hz, double sampleRate) const;
};

// ------------------------------------------------------------------------------------------- meters
// 4x oversampled peak (inter-sample peaks)
class TruePeak
{
public:
    void reset();
    float process (float x);                 // returns the highest |value| among the 4 phases around this sample
    static constexpr int taps = 48, phases = 4, latency = taps / phases / 2;
private:
    float hist[taps / phases] {};
    int pos = 0;
};

class Loudness
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* L, const float* R, int n);
    float momentary() const { return mom; }       // LUFS, 400 ms
    float shortTerm() const { return st; }        // LUFS, 3 s
    float integrated() const;                     // LUFS (gated), -70 when nothing yet
    float range() const;                          // LRA in LU
    float maxShortTerm() const { return stMax; }
    double seconds() const { return totalSeconds; }
private:
    double sr = 48000.0;
    Biquad pre[2], rlb[2];
    int blockLen = 4800, blockPos = 0;
    double blockSum = 0.0;
    double sub[30] {};
    int subCount = 0, subPos = 0;
    float mom = -70.0f, st = -70.0f, stMax = -70.0f;
    // histograms in 0.1 dB steps from -70 to +10
    static constexpr int bins = 800;
    std::vector<juce::uint32> gateHist, stHist;
    double totalSeconds = 0.0;
    static float energyToLufs (double e) { return e <= 1.0e-12 ? -70.0f : (float) (-0.691 + 10.0 * std::log10 (e)); }
};

// third-octave long-term spectrum (31 bands, 20 Hz .. 20 kHz)
class ThirdOctave
{
public:
    void prepare (double sampleRate);
    void reset();
    void push (const float* mono, int n);
    float bandDb (int b) const { return longDb[b]; }       // long-term average
    float nowDb (int b) const { return fastDb[b]; }        // what sounds now
    int frames() const { return numFrames; }
    static float centre (int b) { return 20.0f * std::pow (2.0f, b / 3.0f); }
private:
    double sr = 48000.0;
    static constexpr int order = 12, size = 1 << order;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fifo, work, window;
    int fifoPos = 0, numFrames = 0;
    double longSum[numThirds] {};
    float longDb[numThirds] {}, fastDb[numThirds] {};
};

// ------------------------------------------------------------------------------------------- dynamic EQ band (works on mid / side)
struct EqBandSettings
{
    bool on = false;
    int type = 0;            // 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut
    float hz = 1000.0f, gainDb = 0.0f, q = 1.0f;
    int dynMode = 0;         // 0 static, 1 down when it passes the threshold, 2 up when it falls under
    float thresholdDb = -24.0f, rangeDb = 6.0f;
    int where = 0;           // 0 stereo, 1 mid, 2 side
};
class DynEqBand
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* mid, float* side, int n, const EqBandSettings&, float attackMs, float releaseMs);
    float dynamicDb() const { return dynNow; }
    float totalDb() const { return lastGain; }
private:
    double sr = 48000.0;
    Biquad f[2], detector;
    dsp::Follower env;
    float dynNow = 0.0f, lastGain = 1000.0f;
    int lastType = -1;
    float lastHz = 0.0f, lastQ = 0.0f;
    int countdown = 0;
    void design (const EqBandSettings&, float gainDb);
};

// ------------------------------------------------------------------------------------------- multiband compressor
struct MbBandSettings { bool on = true; float thresholdDb = -18.0f, ratio = 2.0f, attackMs = 20.0f, releaseMs = 150.0f, gainDb = 0.0f; };
class Multiband
{
public:
    void prepare (double sampleRate);
    void reset();
    void setCrossovers (const float* hz);          // 5 frequencies
    void process (float* L, float* R, int n, const MbBandSettings*, int solo);   // solo: -1 none
    float reductionDb (int band) const { return gr[band]; }
private:
    double sr = 48000.0;
    float xo[numMbBands - 1] { 80, 250, 1000, 4000, 10000 };
    // a split is a pair of 2nd-order Butterworth low-pass and high-pass filters, twice (Linkwitz-Riley 4)
    struct Split { Biquad lp[2][2], hp[2][2]; };
    Split splits[numMbBands - 1];
    Biquad comp[numMbBands][numMbBands][2];       // allpasses that keep the lower bands in phase
    float env[numMbBands] {}, gainLin[numMbBands] {}, gr[numMbBands] {};
    void design();
};

// ------------------------------------------------------------------------------------------- resonance suppressor (STFT)
class Resonance
{
public:
    void prepare (double sampleRate);
    void reset();
    // processes in place; always delays by latency() samples (also when off) so the whole chain stays aligned
    void process (float* L, float* R, int n, bool on, float depth, float sharpness, float loHz, float hiHz, float speed);
    static constexpr int size = 1024, hop = 256;
    int latency() const { return size; }
    float cutDb (int bin) const { return cut[(size_t) bin]; }
private:
    double sr = 48000.0;
    juce::dsp::FFT fft { 10 };
    std::vector<float> window, inL, inR, outL, outR, frame[2], mag, smooth, cut;
    int inPos = 0, outPos = 0, hopCount = 0;
    void frameProcess (bool on, float depth, float sharpness, float loHz, float hiHz, float speed);
};

// ------------------------------------------------------------------------------------------- true-peak limiter
class Limiter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, float inputGainDb, float ceilingDb, float releaseMs);
    int latency() const { return look + TruePeak::latency; }
    float reductionDb() const { return grDb; }
private:
    double sr = 48000.0;
    int look = 72;
    TruePeak tp[2];
    std::vector<float> delayL, delayR, req, rel, avg;
    int dPos = 0, rPos = 0;
    double avgSum = 0.0;
    float relNow = 1.0f, grDb = 0.0f;
    // window minimum (monotonic queue in fixed arrays: nothing allocated while playing)
    std::vector<juce::int64> qT;
    std::vector<float> qV;
    int qHead = 0, qTail = 0, qCount = 0;
    juce::int64 t = 0;
};

// ------------------------------------------------------------------------------------------- small processors
class Exciter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, float hz, float drive, float mix, int mode);
private:
    double sr = 48000.0;
    Biquad hp1[2], hp2[2];
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> exSoft[2];
    racoon::sdsp::AdaaAsymTanh exAsym[2];
    float lastHz = 0.0f;
};

class Imager
{
public:
    void prepare (double sampleRate);
    void reset();
    // widths 0..2 for lows, mids and highs; monoBelow cuts the side under that frequency (0 = off)
    void process (float* L, float* R, int n, float lowW, float midW, float highW, float monoBelowHz, float& correlation);
private:
    double sr = 48000.0;
    Biquad lowLp[2], lowHp[2], highLp[2], highHp[2], monoHp[2];
    float lastMono = -1.0f;
    double corrLR = 0, corrLL = 0, corrRR = 0;
};

class LowEnd
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, float punch, float tight, float hz);
private:
    double sr = 48000.0;
    Biquad lowBand, shelf[2];
    float fast = 0.0f, slow = 0.0f, lastGain = 0.0f;
    int countdown = 0;
};

class Transient
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, int n, float attack, float sustain);
private:
    double sr = 48000.0;
    float fast = 0.0f, slow = 0.0f, gain = 1.0f;
};

// ------------------------------------------------------------------------------------------- Partícula
// The one-knob mix-bus chain: a pre EQ (low, mid, high), three bands of compression that move together with the
// AMOUNT knob (0..200 %), a little saturation in the lows and mids, air on top, the lows narrowed and the
// mids and highs opened. The detector can listen to the signal itself or to an external sidechain.
struct ParticleSettings
{
    float amount = 1.0f;                    // 0..2
    float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;
    bool dynamics = true;
    bool external = false;
};
class Particle
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* L, float* R, const float* scL, const float* scR, int n, const ParticleSettings&);
    float reductionDb (int band) const { return gr[band]; }
    static constexpr float lowXo = 200.0f, highXo = 3000.0f;
    // where each band's reduction usually sits on a finished mix with AMOUNT at 100 % (the green marks)
    static constexpr float targetGr[3] { 2.5f, 2.0f, 2.5f };
private:
    double sr = 48000.0;
    Biquad preLow[2], preMid[2], preHigh[2];
    Biquad lp1[2][2], hp1[2][2], lp2[2][2], hp2[2][2], apLp[2][2], apHp[2][2];   // per channel, two stages each
    Biquad scLp1[2], scHp1[2], scLp2[2], scHp2[2];                               // the sidechain split (mono)
    Biquad air[2];
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> airSat[2];
    float env[3] {}, gainSm[3] { 1.0f, 1.0f, 1.0f }, gr[3] {};
    float atk[3] {}, rel[3] {}, smooth = 0.0f;
    float lastLow = 99.0f, lastMid = 99.0f, lastHigh = 99.0f;
};

// graphic EQ at the third-octave centres (match EQ / tonal correction)
class ThirdEq
{
public:
    void prepare (double sampleRate);
    void reset();
    void setGains (const float* db);             // 31 values
    void process (float* L, float* R, int n);
    bool flat() const { return isFlat; }
private:
    double sr = 48000.0;
    Biquad f[numThirds][2];
    bool isFlat = true;
};

// target tonal curves for the balance (dB per third octave, relative to their own average 200 Hz .. 5 kHz)
juce::StringArray genreNames();
float targetDb (int genre, int band);
}

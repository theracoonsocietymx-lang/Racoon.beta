#pragma once
#include "Common.h"
#include "FxChain.h"
#include "RacoonDsp.h"

namespace be
{
// ---------------------------------------------------------------- per-track insert chain
struct StripSettings
{
    int drvType = 0;
    float drive = 0, crush = 0, eqLow = 0, eqHigh = 0, comp = 0, gate = 0, transient = 0, faderDb = 0, phaseMs = 0;
    bool invert = false;
};

class TrackStrip
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const StripSettings& s, float* l, float* r, int n, int quality);
    float gainReduction = 0.0f;   // dB, for meters
private:
    double sr = 48000.0;
    Biquad lowSh[2], highSh[2];
    float eqCache[2] { -999.0f, -999.0f };
    float compEnv = 0.0f, gateEnv = 0.0f, gateGain = 1.0f, trFast = 0.0f, trSlow = 0.0f;
    float faderNow = -1.0f;   // the fader gain glides to its new value (no zipper)
    float crushHold[2] {}; int crushCount = 0;
    DelayLine phaseL, phaseR;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[3];
};

// ---------------------------------------------------------------- aux return bus
struct AuxSettings { int type = 0; float p1 = 0.5f, p2 = 0.5f, p3 = 0.7f, p4 = 0.2f, ret = 0.7f, lowCut = 180.0f, hiCut = 12000.0f; };

class AuxBus
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const AuxSettings& s, float* l, float* r, int n, double bpm);
    // message thread: rebuild the impulse response when a convolution type changes
    void updateImpulse (int type, float size, float tone);
    float level = 0.0f;
private:
    double sr = 48000.0;
    racoon::sdsp::Verb reverb;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 512 } };
    bool convReady = false;
    int irType = -1; float irSize = -1.0f, irTone = -1.0f;
    DelayLine dl[2], pre[2];
    float fb[2] {}; TptOnePole dlLp[2];
    float baseNow = -1.0f;   // delay time that glides to the new division (no click)
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    double lfo = 0.0;
    struct Allpass { float a = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; float process (float x) { const float y = a * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
    Allpass hilA[2][4], hilB[2][4]; float hilDelay[2] {}; double fsPhase = 0.0;
    DelayLine res[2][3]; float resLp[2][3] {};
    float crushHold[2] {}; int crushCount = 0;
    Svf lc[2], hc[2];
    float lastLc = -1, lastHc = -1;
    int lastType = -1;
    int silentFor = 0;
};

// ---------------------------------------------------------------- group bus
struct GroupSettings { float levelDb = 0, comp = 0, drive = 0, width = 1; };

class GroupBus
{
public:
    void prepare (double sampleRate) { sr = sampleRate; env = 0.0f; }
    void process (const GroupSettings& s, float* l, float* r, int n);
    float gainReduction = 0.0f;
private:
    double sr = 48000.0;
    float env = 0.0f;
};

// ---------------------------------------------------------------- master dynamics: glue, soft clip, true-peak limiter
struct MasterSettings
{
    bool glueOn = true; float glueThresh = -10, glueRatio = 2, glueAttack = 10, glueRelease = 150, glueMakeup = 0, glueMix = 1;
    float softclip = 0;
    bool tpOn = true; float tpCeiling = -0.5f;
    float volume = 0.8f;
};

class MasterDynamics
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const MasterSettings& s, float* l, float* r, int n);
    float glueGr = 0.0f, limGr = 0.0f, truePeak = 0.0f;
    int latency() const { return look; }
private:
    double sr = 48000.0;
    float glueEnv = 0.0f;
    float scX[2] {};   // ADAA memory of the soft clip
    DelayLine dL, dR;
    std::vector<float> req, box;
    int look = 64, reqPos = 0, boxPos = 0;
    double boxSum = 64.0;
    float limGain = 1.0f;
    float hist[2][3] {};
    float isp (int c, float x);     // inter-sample peak estimate
};

// ---------------------------------------------------------------- stutter (scatter) and break filter
class Scatter
{
public:
    void prepare (double sampleRate);
    void process (int division, float* l, float* r, int n, double bpm, double beats, bool playing);
private:
    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int w = 0, size = 0;
    bool active = false;
    int segStart = 0, segLen = 0, readPos = 0;
    float fade = 0.0f;
};

class BreakFilter
{
public:
    void prepare (double sampleRate) { sr = sampleRate; amt = 0.0f; for (auto& f : hp) f.reset(); }
    void process (bool on, float* l, float* r, int n);
private:
    double sr = 48000.0;
    float amt = 0.0f;
    Svf hp[4];
};

// ---------------------------------------------------------------- loudness (BS.1770), true peak, correlation
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* l, const float* r, int n);
    std::atomic<float> momentary { -70.0f }, shortTerm { -70.0f }, integrated { -70.0f }, peakDb { -70.0f }, crest { 0.0f }, correlation { 1.0f }, width { 0.0f };
    std::atomic<float> truePeakMax { 0.0f };
    std::atomic<bool> resetRequest { false };
private:
    double sr = 48000.0;
    Biquad pre[2], rlb[2];
    int blockLen = 4800, blockPos = 0;
    double blockSum = 0.0;
    float blocks[30] {};      // 100 ms energies, 3 s history
    int blockIdx = 0, blocksFilled = 0;
    static constexpr int kBins = 800;
    double histE[kBins] {}; int histN[kBins] {};
    double corrLR = 0, corrLL = 0, corrRR = 0, midE = 0, sideE = 0;
    float pkHold = 0.0f, rmsAcc = 0.0f;
};
} // namespace be

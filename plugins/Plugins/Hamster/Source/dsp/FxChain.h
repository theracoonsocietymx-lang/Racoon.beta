#pragma once
#include "Common.h"
#include "../Params.h"
#include "RacoonDsp.h"

namespace be
{
// --------------------------------------------------------------- settings snapshots
struct DistSettings
{
    bool on = false;
    int type = 0;
    float drive = 0.25f, tone = 0.0f, mix = 1.0f, outDb = 0.0f, shape = 0.5f, xLow = 120.0f, xHigh = 2500.0f, lowDrive = 0.0f, highDrive = 0.6f;
};

struct FxSettings
{
    bool on[kNumFx] {};
    // EQ
    float eqHpf = 10, eqLsF = 90, eqLsG = 0, eqP1F = 300, eqP1G = 0, eqP1Q = 1, eqP2F = 1800, eqP2G = 0, eqP2Q = 1, eqHsF = 6000, eqHsG = 0;
    // comp
    float cThresh = -12, cRatio = 4, cAttack = 5, cRelease = 80, cMakeup = 0, cMix = 1;
    // multiband
    float mbXLow = 120, mbXHigh = 2500, mbLow = -12, mbMid = -18, mbHigh = -18, mbRatio = 3, mbMix = 1;
    // limiter / clipper
    float limCeiling = -0.3f, limRelease = 50, clipDrive = 0, clipCeiling = -1, clipKnee = 0.3f;
    // modulation fx
    float chRate = 0.6f, chDepth = 0.35f, chDelay = 8, chFb = 0, chMix = 0.4f, chLowCut = 150;
    float phRate = 0.3f, phDepth = 0.6f, phCentre = 900, phFb = 0.4f, phMix = 0.5f, phLowCut = 150;
    float flRate = 0.2f, flDepth = 0.5f, flDelay = 2, flFb = 0.5f, flMix = 0.5f, flLowCut = 150;
    // delay
    float dlTime = 375; bool dlSync = true; int dlDiv = 12; float dlFb = 0.35f; bool dlPing = true; float dlHighCut = 5000, dlMix = 0.2f, dlLowCut = 150;
    // reverb
    float rvSize = 0.5f, rvDamp = 0.5f, rvWidth = 1, rvPre = 10, rvMix = 0.15f, rvLowCut = 150;
    // misc
    float fsShift = 5, fsMix = 0.5f;
    float rmFreq = 110; int rmMode = 1; float rmRatio = 2, rmMix = 0.3f;
    float trAttack = 0.3f, trSustain = 0, trSpeed = 0.5f;
    float imWidth = 1; bool imBassMono = true; float imMonoFreq = 120;
    int afType = 0; float afCutoff = 1500, afRes = 0.3f, afRate = 0.5f; bool afSync = true; int afDiv = 7; float afDepth = 0.4f, afMix = 1;
    float rsTune = 12, rsDecay = 0.6f, rsBright = 0.5f, rsMix = 0.3f;
    int order[kNumFx] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3 };
};

struct DuckSettings
{
    int source = 0, division = 7, band = 0, note = 36, channel = 16;
    float depth = 0.7f, attackMs = 2, holdMs = 20, releaseMs = 160, curve = 0.3f, freq = 180, threshDb = -24;
};

// --------------------------------------------------------------- distortion stage
class DistortionStage
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    // processes in place (stereo) at base rate, internally oversampled
    void process (const DistSettings& s, float* l, float* r, int n, int quality);
    int latencySamples (int quality) const;
private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os2, os4;
    float holdL = 0, holdR = 0; int holdCount = 0;
    float gDrive = -1.0f, gCached = 1.0f, adaaX[2] {};   // drive gain cache, ADAA memory per channel
    LR4 xLowL, xLowR, xHighL, xHighR;
    TptOnePole toneL, toneR;
    TptOnePole tapeL, tapeR;
    std::vector<float> dryL, dryR;
    float processSample (const DistSettings& s, float x, int ch, float osFactor);
};

// --------------------------------------------------------------- FX rack
class FxRack
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const FxSettings& s, float* l, float* r, int n, double bpm, double hostBeats, bool hostPlaying, float lastNoteHz,
                  const float* globalMod);
private:
    double sr = 48000.0;
    // EQ
    Biquad eqHp[2], eqLs[2], eqP1[2], eqP2[2], eqHs[2];
    float eqCache[11] {};
    // comp
    float compEnv = 0.0f;
    float mbEnv[3] {};
    LR4 mbLowL, mbLowR, mbHighL, mbHighR;
    // limiter
    DelayLine limL, limR; std::vector<float> limReq; int limPos = 0; float limGain = 1.0f; int limLook = 72;
    // modulation fx
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    DelayLine flL, flR; float flPhase = 0.0f, flFbL = 0.0f, flFbR = 0.0f;
    TptOnePole lcCh[2], lcPh[2], lcFl[2], lcDl[2], lcRv[2];
    std::vector<float> wetL, wetR;
    // delay
    DelayLine dlL, dlR; TptOnePole dlLp[2]; float dlFbL = 0, dlFbR = 0;
    float dlTimeNow = -1.0f;   // the delay time glides to a new value instead of jumping (no click)
    // reverb
    racoon::sdsp::Verb reverb; DelayLine preL, preR;   // 8-line FDN (was juce Freeverb)
    float clipX[2] {};   // ADAA memory of the clipper
    // freq shifter (Hilbert allpass pairs)
    struct Allpass
    {
        float a = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process (float x) { const float y = a * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };
    Allpass hilA[2][4], hilB[2][4]; float hilDelay[2] {}; double fsPhase = 0.0;
    // ring mod
    double rmPhase = 0.0;
    // transient
    float trFast = 0, trSlow = 0;
    // imager
    LR4 imLowL, imLowR;
    // auto filter
    Svf afL, afR; double afPhase = 0.0;
    // resonator
    DelayLine rsL[3], rsR[3]; float rsLpL[3] {}, rsLpR[3] {};
    float smoothedResHz = 110.0f;

    void processEq (const FxSettings& s, float* l, float* r, int n);
    void processComp (const FxSettings& s, float* l, float* r, int n);
    void processMultiband (const FxSettings& s, float* l, float* r, int n);
    void processLimiter (const FxSettings& s, float* l, float* r, int n);
    void processClipper (const FxSettings& s, float* l, float* r, int n);
    void processChorus (const FxSettings& s, float* l, float* r, int n, float mix);
    void processPhaser (const FxSettings& s, float* l, float* r, int n, float mix);
    void processFlanger (const FxSettings& s, float* l, float* r, int n, float mix);
    void processDelay (const FxSettings& s, float* l, float* r, int n, double bpm, float mix, float fb);
    void processReverb (const FxSettings& s, float* l, float* r, int n, float mix);
    void processFreqShift (const FxSettings& s, float* l, float* r, int n, float shift);
    void processRingMod (const FxSettings& s, float* l, float* r, int n, float noteHz);
    void processTransient (const FxSettings& s, float* l, float* r, int n);
    void processImager (const FxSettings& s, float* l, float* r, int n, float width);
    void processAutoFilter (const FxSettings& s, float* l, float* r, int n, double bpm, double beats, bool playing, float cutoff);
    void processResonator (const FxSettings& s, float* l, float* r, int n, float noteHz, float mix);
};

// --------------------------------------------------------------- ducking
class Ducker
{
public:
    void prepare (double sampleRate);
    void reset();
    void trigger (int sampleOffset) { if (numTriggers < 64) triggers[numTriggers++] = sampleOffset; }
    void process (const DuckSettings& s, float* l, float* r, int n, double bpm, double hostBeats, bool hostPlaying,
                  const float* scL, const float* scR, float depthMod);
    float gainReductionDb() const { return grDb.load(); }
    float currentGain() const { return lastGain; }
private:
    double sr = 48000.0;
    int triggers[64] {}; int numTriggers = 0;
    double sinceTrigger = 1.0e9;   // seconds
    double prevBeats = -1.0;
    float scEnv = 0.0f, lastGain = 1.0f;
    bool scGate = false;
    LR4 splitL, splitR;
    std::atomic<float> grDb { 0.0f };
    float gainAt (const DuckSettings& s, double t) const;
};
} // namespace be

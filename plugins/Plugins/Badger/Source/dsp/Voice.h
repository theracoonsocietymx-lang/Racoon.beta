#pragma once
#include "Snapshot.h"

namespace be
{
// values shared from the engine to every voice for one control step
struct GlobalModState
{
    float lfo[kNumLfos] {};          // global-scope LFO outputs
    float aftertouch = 0.0f, modWheel = 0.0f, pitchBend = 0.0f, expression = 0.0f, evolution = 0.0f;
    float seqMod = 0.0f;
    double bpm = 120.0, hostBeats = 0.0;
    bool hostPlaying = false;
    float pitchBendSemis = 0.0f;
};

// per-voice output buses (oversampled rate), 4 layers x stereo
struct LayerBuses
{
    float* l[kNumLayers] {};
    float* r[kNumLayers] {};
};

class Voice
{
public:
    void prepare (double osRate, int osFactor, int voiceIndex);
    void setOversampling (double osRate, int osFactor);

    // note control
    void start (const PatchSnapshot& p, int note, float velocity, int channel, bool legato, bool accent, bool slide,
                float seqModValue, int stackIndex, int stackSize, uint32_t seed, float startNote);
    void glideTo (const PatchSnapshot& p, int note, float velocity, bool accent, bool slide, float seqModValue);
    void release();
    void kill();

    bool isActive() const { return active; }
    bool isGateOn() const { return gate; }
    bool isReleasing() const { return active && ! gate; }
    int getNote() const { return note; }
    int getChannel() const { return channel; }
    int getStack() const { return stackIndex; }
    juce::uint64 getAge() const { return age; }
    float getCurrentNote() const { return curNote; }
    float ampLevel() const { return lastAmp; }

    // MPE / poly expression
    void setPerNoteBend (float semis) { mpeBend = semis; }
    void setPressure (float v) { pressure = v; }
    void setSlide (float v) { mpeSlide = v; }

    // render `numBase` base-rate samples worth of control steps (n = numBase * osFactor samples)
    void render (const PatchSnapshot& p, const GlobalModState& g, LayerBuses& out, int startOs, int numBaseSamples);

    // expose modulation of lead voice to UI / global evaluation
    const float* modAdd() const { return modSum; }
    float sourceValue (int src) const { return srcVal[src]; }

private:
    // identity
    int index = 0, note = 60, channel = 1, stackIndex = 0, stackSize = 1;
    bool active = false, gate = false, accent = false, slideFlag = false;
    float velocity = 1.0f, rawVelocity = 1.0f;
    juce::uint64 age = 0;
    static juce::uint64 ageCounter;
    double osRate = 96000.0;
    int osFactor = 2;
    float cr = 3000.0f;

    // pitch
    float curNote = 60.0f, targetNote = 60.0f, glideStart = 60.0f, glideTime = 0.0f, glidePos = 1.0f;
    float mpeBend = 0.0f, pressure = 0.0f, mpeSlide = 0.0f;
    float humanPitch = 0.0f, humanCut = 0.0f;
    float randomVal = 0.0f, seqModVal = 0.0f;
    float stackDetune = 0.0f, stackPan = 0.0f;
    bool layerActive[kNumLayers] { true, true, true, true };

    // modulation
    Envelope env[kNumEnvs];
    Lfo lfo[kNumLfos];
    float srcVal[ModSrc::Count] {};
    float modSum[ModDst::Count] {};
    float modMul[ModDst::Count] {};
    float routeSmooth[kNumRoutes] {};

    // generators
    Oscillator osc[kNumOsc];
    OscMod oscMod[kNumOsc];
    float subPhase = 0.0f, subInc = 0.0f, subLevel = 0.0f, subDrive = 0.0f;
    OnePole subTone; Svf subCeil;
    float noisePink[7] {}, noiseBrown = 0.0f, noiseHold = 0.0f, noiseLevel = 0.0f, noiseBurst = 0.0f;
    int noiseHoldCount = 0;
    TptOnePole noiseTilt[2];
    float noiseColor = 0.0f;
    Rng rng;

    // layers
    TptOnePole layLow[kNumLayers][2], layHigh[kNumLayers][2];
    float layLevel[kNumLayers] {}, layDrive[kNumLayers] {}, layGainL[kNumLayers] {}, layGainR[kNumLayers] {};
    float xfade = 0.0f;

    // filters
    VoiceFilter filt[kNumFilters];
    VoiceFilter acidFilt;
    float filtMix[kNumFilters] {};
    bool filtOn[kNumFilters] {};

    // amp
    float ampPrev = 0.0f, ampTarget = 0.0f, subEnvPrev = 0.0f, subEnvTarget = 0.0f, lastAmp = 0.0f;
    float ampVolume = 0.8f, ampPanL = 1.0f, ampPanR = 1.0f;

    // acid
    float acidMorph = 0.0f, acidEnv = 0.0f, acidAmp = 0.0f, acidAccentCap = 0.0f, acidDecayTime = 0.35f, acidEnvMod = 0.6f;
    float acidCalibCut = 0.0f, acidCalibRes = 0.0f;
    float impFilter = 0.0f, impAttack = 1.0f, impSat = 0.0f;

    void computeModulation (const PatchSnapshot& p, const GlobalModState& g);
    void controlStep (const PatchSnapshot& p, const GlobalModState& g);
    float pitchFor (const PatchSnapshot& p, float noteF) const;
};
} // namespace be

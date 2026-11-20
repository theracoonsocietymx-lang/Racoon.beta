#pragma once
#include "RacoonProcessor.h"

namespace racoon::opossum
{
constexpr int numOps = 6, maxVoices = 16, voicePool = 20, maxUnison = 8, numLfo = 2, numSlots = 6, numAlgos = 12;

enum ModSource { srcNone, srcLfo1, srcLfo2, srcFenv, srcVel, srcAt, srcWheel, srcKey, srcRandom, numSources };
enum ModDest { dstNone, dstPitch, dstIndex, dstFeedback, dstMetal, dstCutoff, dstReso, dstVowel, dstDrive, dstLevel, dstPan, numDests };

// one wiring of the six operators: who modulates whom, and which ones are heard
struct Algo
{
    const char* name;
    int numEdges;
    int from[6], to[6];
    int carriers;          // bit mask, bit 0 = operator 1
};
const Algo& algo (int i);

struct Env
{
    enum Stage { idle, attack, decay, sustain, release };
    Stage stage = idle;
    float level = 0.0f;
    void start() { stage = attack; }
    void noteOff() { if (stage != idle) stage = release; }
    float advance (int n, float a, float d, float s, float r, double sr);
};

struct Voice
{
    bool active = false, held = false, sustained = false, stealing = false;
    int note = -1, order = 0;
    float velocity = 0.0f, pressure = 0.0f, random = 0.0f;
    float pitch = 60.0f, glideFrom = 60.0f, glidePos = 1.0f;
    float stealGain = 1.0f;
    Env amp, fenv, op[numOps];
    float penvAge = 0.0f;
    double lfoPhase[numLfo] {};
    float lfoRand[numLfo][2] {};
    float phase[maxUnison][numOps] {};
    float out[maxUnison][numOps] {};       // this sample (and, read before writing, the previous one)
    float fb1[maxUnison][numOps] {}, fb2[maxUnison][numOps] {};
    float uniJit[maxUnison] {};
    dsp::SVF f1[2], f2[2], fm[3][2];       // filter (two stages) and the three formant bands
    float driveX[2] {};
    dsp::Rng rng;
    float ampNow = 0.0f, gNow = 0.0f, panL = 1.0f, panR = 1.0f;
    float opAmpNow[numOps] {};
};

// Opossum: six-operator FM (phase modulation) for dark metal basses, growls, bells and aggressive leads.
// Twelve wirings, feedback, a Metal knob that pushes the modulators inharmonic, a pitch envelope, a Wobble knob,
// a filter with a vowel mode, destruction (tube, fold, rectify), mono lows, echo and room. The voice runs at twice
// the sample rate so high indexes and feedback do not fold back.
class Processor : public RacoonProcessor
{
public:
    Processor();
    ~Processor() override;
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 140.0; }
    int demoBars() const override { return 4; }

    static const juce::StringArray& algoNames();
    static const juce::StringArray& waveNames();
    static const juce::StringArray& ratioNames();
    static const juce::StringArray& sourceNames();
    static const juce::StringArray& destNames();
    static float ratioValue (int index);
    static juce::String opId (int op, const char* what) { return "op" + juce::String (op + 1) + "_" + what; }
    static juce::String lfoId (int k, const char* what) { return "lfo" + juce::String (k + 1) + "_" + what; }
    static juce::String slotId (int k, const char* what) { return "m" + juce::String (k + 1) + "_" + what; }

    juce::MidiKeyboardState keyboard;
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> opView[numOps] {};    // how loud each operator is right now (the screen)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    Voice voices[voicePool];
    int orderCounter = 0, noteCounter = 0;
    float lastPitch = 60.0f;
    float wheel = 0.0f, bend = 0.0f, channelPressure = 0.0f;
    bool pedal = false;
    double globalLfo[numLfo] {};

    std::atomic<float>* pOp[numOps][9] {};     // ratio, fine, level, fb, wave, a, d, s, r
    std::atomic<float>* pLfo[numLfo][5] {};    // rate, sync, div, shape, retrig
    std::atomic<float>* pSlot[numSlots][3] {};
    static float rd (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); }

    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioBuffer<float> mix;

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderVoice (Voice&, float* L, float* R, int start, int n, int controlRate, double osRate, int factor);
    float lfoValue (Voice&, int k, float phase, bool newCycle);
};
}

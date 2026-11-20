#pragma once
#include "RacoonProcessor.h"
#include "OtterDsp.h"

namespace racoon::otter
{
constexpr int maxVoices = 16, voicePool = 20, numLfo = 2, numSlots = 8;

enum Model { modelString, modelBar, modelBoth };
enum Exciter { excPluck, excStrike, excBow, excScrape, excMagnet };
enum ModSource { srcNone, srcLfo1, srcLfo2, srcEnv2, srcVel, srcAt, srcWheel, srcKey, srcRandom, numSources };
enum ModDest { dstNone, dstPitch, dstVibrato, dstPressure, dstBowSpeed, dstPosition, dstHardness, dstDecay, dstDark, dstBodyMix, dstLevel, dstPan, numDests };

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
    bool active = false, held = false, sustained = false, stealing = false, released = false;
    int note = -1, order = 0;
    float velocity = 0.0f, pressure = 0.0f, random = 0.0f;
    float pitch = 60.0f, glideFrom = 60.0f, glidePos = 1.0f;
    float stealGain = 1.0f;
    Env amp, env2;
    float age = 0.0f, sinceRelease = 0.0f;
    double lfoPhase[numLfo] {};
    float lfoRand[numLfo][2] {};
    double vibPhase = 0.0;

    StringModel str;
    KS second;
    Modes modes;
    float modeW[numModes] {}, modeOutW[numModes] {}, modeCos[numModes] {}, modeSin[numModes] {}, modeR[numModes] {}, modeOn[numModes] {};
    Bq tube[2];
    Ring outHist;              // for the listening point of the string
    float listenD = 10.0f, listenNorm = 1.0f;

    // exciter state
    float exT = 1.0e9f;        // seconds since the last pluck or strike (a large value = no pulse running)
    float exLen = 0.001f, exAmp = 0.0f, exLp = 0.0f, exLp2 = 0.0f, exLpC = 0.5f;
    float noiseBp1 = 0.0f, noiseBp2 = 0.0f, scrapeImp = 0.0f;
    float magHp = 0.0f, magHpX = 0.0f, magRms = 0.0f, magX = 0.0f;
    float energy = 0.0f;       // 5 ms level of the string (tension)
    float outLevel = 0.0f;     // follower of the voice's output (to free it)
    float gate = 1.0f;         // the output gate for the bowed / scraped / magnet notes
    dsp::Rng rng;
    float panL = 1.0f, panR = 1.0f;
    float P = 100.0f;          // loop period now (samples)
    float bendNow = 0.0f;      // the pitch wheel as this voice follows it
};

// Otter: strings and bars that are plucked, struck, bowed, scraped or held by a magnet. Each voice is an exciter into
// a resonator (a waveguide string, a modal bar, or both coupled), then a shared body, six sympathetic strings tuned to
// a key, a darkening tone stage and a room.
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
    juce::String aleatoriaRol() const override;

    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 96.0; }
    int demoBars() const override { return 4; }

    static const juce::StringArray& modelNames();
    static const juce::StringArray& tableNames();
    static const juce::StringArray& exciterNames();
    static const juce::StringArray& bodyNames();
    static const juce::StringArray& sourceNames();
    static const juce::StringArray& destNames();
    static const juce::StringArray& keyNames();
    static juce::String lfoId (int k, const char* what) { return "lfo" + juce::String (k + 1) + "_" + what; }
    static juce::String slotId (int k, const char* what) { return "m" + juce::String (k + 1) + "_" + what; }

    juce::MidiKeyboardState keyboard;
    std::atomic<int> activeVoices { 0 };
    // the screen: the string's shape (64 points), the energy of each mode, the last note
    std::atomic<float> shapeView[64] {};
    std::atomic<float> modeView[numModes] {};
    std::atomic<int> lastNote { 45 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    Voice voices[voicePool];
    int orderCounter = 0, noteCounter = 0;
    float lastPitch = 45.0f;
    float wheel = 0.0f, bend = 0.0f, channelPressure = 0.0f;
    bool pedal = false;
    double globalLfo[numLfo] {};
    int maxDelay = 4096;

    std::atomic<float>* pLfo[numLfo][4] {};    // rate, sync, div, shape
    std::atomic<float>* pSlot[numSlots][3] {};
    static float rd (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); }

    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioBuffer<float> mix;

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void releaseVoice (Voice&);
    void excite (Voice&, bool fresh);
    void renderVoice (Voice&, float* L, float* R, int start, int n);
    float lfoValue (Voice&, int k, float phase, bool newCycle);
};
}

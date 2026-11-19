#pragma once
#include "RacoonProcessor.h"
#include "SkunkDsp.h"

namespace racoon::skunk
{
constexpr int numLfos = 16, numEnvs = 8, numSeqs = 4, numMacros = 8, numRoutes = 64, seqSteps = 16;

// modulation sources and destinations of the matrix
const juce::StringArray& sourceNames();
struct Dest { const char* id; const char* name; };
const std::vector<Dest>& destinations();
const juce::StringArray& destinationNames();   // "Nada" first
int destIndex (const char* id);                // 1-based index of a destination, 0 when unknown

// Skunk, Sound Design Lab: captures audio (live, one shot, continuous or frozen), turns it into grains, freezes and
// bends its spectrum, morphs it with a second signal, cuts it with glitches, and runs it through creative filters,
// space and effects. 16 LFO, 8 envelopes, 4 step sequencers, 8 macros and an XY pad drive 64 modulation routes.
class Processor : public RacoonProcessor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    Processor();
    ~Processor() override { stopTimer(); apvts.removeParameterListener ("lib", this); }
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 120.0; }
    int demoBars() const override { return 2; }

    static juce::String routeId (int r, const char* what) { return "r" + juce::String (r + 1) + "_" + what; }
    static juce::String lfoId (int i, const char* what) { return "lfo" + juce::String (i + 1) + "_" + what; }
    static juce::String envId (int i, const char* what) { return "env" + juce::String (i + 1) + "_" + what; }
    static juce::String seqId (int i, const char* what) { return "seq" + juce::String (i + 1) + "_" + what; }
    static juce::String seqStepId (int i, int s) { return "seq" + juce::String (i + 1) + "_s" + juce::String (s + 1); }

    // message thread actions
    juce::File saveResampleWav() const;          // ~/Music/Racoon/Skunk/... (empty when nothing recorded)
    void requestResampleToCapture() { toCapture.store (true); }
    void requestRecapture() { recapture.store (true); }
    bool loadFile (const juce::File&);           // WAV, AIFF or FLAC into the capture (frozen); message thread
    // the library: materials Skunk makes itself (no files), tuned to C4 so a MIDI note plays them at its pitch.
    // "Biblioteca" (lib) picks one; index 0 is your own capture or file.
    static const juce::StringArray& libraryNames();
    bool loadLibrary (int index);                // message thread

    // for the screen and the tests
    Granular granular;
    Spectral spectral;
    Glitch glitch;
    std::atomic<int> captureValid { 0 };         // samples of the capture buffer that hold audio
    std::atomic<int> captureState { 0 };         // 0 live, 1 waiting for the trigger, 2 recording one shot, 3 frozen
    std::atomic<float> modOut[numRoutes] {};     // current value of every route (for the matrix)
    std::atomic<float> sourceValue[64] {};       // current value of every source
    std::atomic<int> resampled { 0 };            // samples recorded from the output
    std::atomic<float> transientLevel { 0.0f };
    const Ring& captureRing() const { return capture; }
    int captureSize() const { return capture.size; }

    // the real value a destination has right now (with its modulation)
    float modulated (int destIndex) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override;
    void restoreCapture (const juce::ValueTree&);
    void parameterChanged (const juce::String&, float) override;   // "lib" on the message thread: the material is made right away
    int lastLibrary = 0;
    double preparedRate = 0.0;

    Ring capture, dryDelay, finalDry;
    juce::AudioBuffer<float> resample;
    Noise noise;
    FilterBank filter;
    FxRack fx[5];
    Rand rnd;

    // modulation state
    double lfoPhase[numLfos] {};
    float lfoHold[numLfos] {};
    struct Env { float level = 0.0f; int stage = 0; bool gate = false; float autoRelease = -1.0f; };
    Env env[numEnvs];
    float seqValue[numSeqs] {};
    float routeState[numRoutes] {};
    float modNorm[64] {};                        // per destination, normalised offset
    juce::RangedAudioParameter* destParam[64] {};
    std::atomic<float>* routeSrc[numRoutes] {}, * routeDst[numRoutes] {}, * routeDepth[numRoutes] {}, * routeCurve[numRoutes] {}, * routeSmooth[numRoutes] {};

    // MIDI and transients
    int held = 0, lastNote = 60;
    float lastVel = 0.0f;
    float trFast = 0.0f, trSlow = 0.0f, trEnv = 0.0f, follower = 0.0f;
    int trRefractory = 0;
    bool trOnsetThisChunk = false, noteTrigger = false, wasRecording = false;

    // capture
    int oneShotLeft = 0;
    double scanPos = 0.0;
    std::atomic<bool> toCapture { false }, recapture { false }, fileReady { false };
    juce::AudioBuffer<float> loaded;              // a file waiting to become the capture
    juce::SpinLock loadedLock;
    bool capturePrevFreeze = false;
    juce::int64 lastBar = std::numeric_limits<juce::int64>::min();

    // spatial
    Ring spaceDelay;
    float decor[2] {};
    double movePhase = 0.0;
    int lastFftOrder = -1;
    double runPpq = 0.0;

    void updateModulation (int numSamples, double ppq, bool playing);
    void processChunk (float* L, float* R, const float* scL, const float* scR, int n, double ppq, double bpb, bool playing);
};
}

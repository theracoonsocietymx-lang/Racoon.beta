#pragma once
#include "RacoonProcessor.h"
#include "BeaverSpectrum.h"

namespace racoon::beaver
{
constexpr int maxVoices = 16, voicePool = 20, maxUnison = 8, numLfo = 2, numSlots = 8, tableSize = 2048, numMaterials = 9;

enum Source { srcForma, srcDibujo, srcResintesis, srcImagen };
enum ModSource { msNone, msLfo1, msLfo2, msEnv2, msEnv3, msVel, msAt, msWheel, msKey, msRandom, numModSources };
enum ModDest { mdNone, mdMorph, mdPosA, mdPosB, mdCut1, mdCut2, mdRes1, mdTilt, mdEvenOdd, mdBlurT, mdBlurH, mdSweep, mdFormantA, mdPitch, mdDetune,
               mdLevel, mdPan, numModDests };

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
    Env amp, env2, env3;
    float age = 0.0f;
    float lfoRand[numLfo][2] {}; double lfoPhase[numLfo] {};
    double readPos[2] {};                         // frames into each layer's material
    std::shared_ptr<const FrameSet> set[2];       // the material each layer plays (taken at note-on)
    float phases[numPartials] {};                 // the partials' phases (fixed during the note)
    float blurState[numPartials] {};
    bool blurStarted = false;
    std::vector<float> tables[4];                 // previous / current, left (and right when stereo)
    int cur = 0;                                  // which pair is the current one
    int frameLeft = 0, frameLen = 128;            // samples to the next frame
    double uniPhase[maxUnison] {};
    float uniJit[maxUnison] {};
    float freqNow = 440.0f, f0Track = 1.0f;
    std::unique_ptr<juce::dsp::FFT> fft, noiseFft;
    std::vector<float> fftBuf, noiseBuf, noiseOla;
    int noisePos = 0;
    float noiseBands[numNoiseBands] {};
    bool noiseOn = false, stereoNow = false, stereoPrev = false, hasTable = false, tablesEqual = false;
    float builtLevels[numPartials] {};            // the levels the current table was made from
    dsp::Rng rng;
    float panL = 1.0f, panR = 1.0f, levelNow = 0.0f;
    float lastLevels[numPartials] {};             // what the screen shows
};

// Beaver: additive synth of up to 511 harmonics in two layers (shapes, drawn bars, audio turned into partials,
// pictures read as spectra), with filters that act on each partial, a harmonizer, a moving sweep, blur in time and
// across partials, a pluck, unison and stereo per partial. Each voice builds a 2048-point table per frame by inverse FFT.
class Processor : public RacoonProcessor, private juce::Timer
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
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::String aleatoriaRol() const override { return value ("voices") < 1.5f ? "bajo" : "pad"; }

    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 90.0; }
    int demoBars() const override { return 4; }

    static const juce::StringArray& sourceNames();
    static const juce::StringArray& shapeNames();
    static const juce::StringArray& materialNames();
    static const juce::StringArray& filterNames();
    static const juce::StringArray& modSourceNames();
    static const juce::StringArray& modDestNames();
    static juce::String lfoId (int k, const char* what) { return "lfo" + juce::String (k + 1) + "_" + what; }
    static juce::String slotId (int k, const char* what) { return "m" + juce::String (k + 1) + "_" + what; }
    static juce::String layerId (int layer, const char* what) { return juce::String (layer == 0 ? "a_" : "b_") + what; }

    // materials: built-ins are made off the audio thread; `wait` does it right here (tests, offline)
    void syncMaterials (bool wait);
    // the user's own audio or picture for a layer; true when it was read
    bool loadOwn (int layer, const juce::File&, bool wait);
    std::shared_ptr<const FrameSet> activeSet (int layer) const;
    std::atomic<int> analysing { 0 };       // 0 idle, 1..100 progress
    juce::String lastError;

    // drawn bars (dB, -60..0), two layers
    std::atomic<float> bars[2][numBars];
    juce::MidiKeyboardState keyboard;
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> partialsNow { 255 };
    std::atomic<float> levelView[numPartials] {};
    std::atomic<int> materialVersion { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override { syncMaterials (false); cleanGraveyard(); }
    void cleanGraveyard();

    Voice voices[voicePool];
    int orderCounter = 0, noteCounter = 0;
    float lastPitch = 57.0f;
    float wheel = 0.0f, bend = 0.0f, channelPressure = 0.0f;
    bool pedal = false;
    double globalLfo[numLfo] {};
    double sweepPhase = 0.0;
    float calib = 1.0f;                      // IFFT scale: a bin of 1 gives a cosine of this amplitude

    // materials (message thread writes under the lock; the audio thread copies at note-on with a try-lock)
    mutable juce::SpinLock setLock;
    std::shared_ptr<const FrameSet> active[2], own[2];
    int wanted[2] { -1, -1 };
    std::vector<std::shared_ptr<const FrameSet>> graveyard;
    juce::ThreadPool pool { 1 };
    void setActive (int layer, std::shared_ptr<const FrameSet>);

    std::atomic<float>* pLfo[numLfo][4] {};
    std::atomic<float>* pSlot[numSlots][3] {};
    static float rd (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); }

    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::AudioBuffer<float> mix;

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderVoice (Voice&, float* L, float* R, int start, int n);
    void buildFrame (Voice&, float frameSeconds, const float* mod, float env2, float env3);
    float lfoValue (Voice&, int k, float phase, bool newCycle);
};
}

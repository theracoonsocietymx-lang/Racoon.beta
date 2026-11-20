#pragma once
#include "RacoonProcessor.h"

namespace racoon::marmot
{
// band-limited single-cycle tables (saw and triangle), mipmapped by octave so high notes do not fold back.
// Built once, read only afterwards.
struct Tables
{
    static constexpr int size = 2048, levels = 11;
    static const Tables& get();
    static int levelFor (float freq, double sampleRate);
    float saw (float ph, int level) const { return read (sawData, ph, level); }
    float tri (float ph, int level) const { return read (triData, ph, level); }
private:
    Tables();
    std::vector<float> sawData, triData;
    static float read (const std::vector<float>& d, float ph, int level)
    {
        const float* t = d.data() + (size_t) juce::jlimit (0, levels - 1, level) * (size + 1);
        const float idx = ph * (float) size;
        const int i0 = juce::jlimit (0, size - 1, (int) idx);
        const float fr = idx - (float) i0;
        return t[i0] + fr * (t[i0 + 1] - t[i0]);
    }
};

constexpr int maxVoices = 16, voicePool = 24, maxUnison = 6, numLfo = 2, numSlots = 6, maxArpNotes = 32;

enum Wave { waveSaw, wavePulse, waveSquare, waveTri, numWaves };
enum ModSource { srcNone, srcLfo1, srcLfo2, srcEnv2, srcVel, srcAt, srcWheel, srcKey, srcRandom, numSources };
enum ModDest { dstNone, dstPitch, dstPitch2, dstPw, dstCutoff, dstReso, dstLevel, dstPan, dstXmod, dstNoise, dstOsc2, numDests };
enum ArpMode { arpUp, arpDown, arpUpDown, arpRandom, arpPlayed, arpChord, numArpModes };

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
    Env env[2];
    double lfoPhase[numLfo] {};
    float lfoRand[numLfo][2] {};
    float lfoAge = 0.0f;                 // seconds since the note started (LFO fade-in)
    float ph1[maxUnison] {}, ph2[maxUnison] {}, phSub[maxUnison] {};
    float out2[maxUnison] {};            // last sample of osc 2 (cross modulation)
    float syncPrev[maxUnison] {};        // osc 2 delayed one sample, so a sync reset can be smoothed on both sides
    float uniJit[maxUnison] {};
    float drift[maxUnison] {}, driftTarget[maxUnison] {};
    dsp::SVF hp[2], f1[2], f2[2];
    float driveX[2] {};
    dsp::Rng rng;
    float ampNow = 0.0f, gNow = 0.0f, panL = 1.0f, panR = 1.0f;
};

// a simple biquad (RBJ shelves) for the output EQ; coefficients are computed on the audio thread, no allocation
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    void shelf (bool high, double sr, double hz, double db);
    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Marmot: the family's polyphonic synth for leads, plucks, stabs and arpeggios. Two classic oscillators (saw, pulse
// with width, square, triangle), a sub one octave down, noise, hard sync and cross modulation; a high pass and a
// resonant low pass with drive; two envelopes, two LFOs with fade-in, a 6-slot matrix, an arpeggiator, and an output
// chain with saturation, ensemble chorus, two-band EQ, tempo delay and reverb.
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
    double demoBpm() const override { return 124.0; }
    int demoBars() const override { return 4; }

    static const juce::StringArray& waveNames();
    static const juce::StringArray& sourceNames();
    static const juce::StringArray& destNames();
    static const juce::StringArray& arpModeNames();
    static const juce::StringArray& arpDivNames();
    static const juce::StringArray& chorusNames();
    static juce::String lfoId (int k, const char* what) { return "lfo" + juce::String (k + 1) + "_" + what; }
    static juce::String slotId (int k, const char* what) { return "m" + juce::String (k + 1) + "_" + what; }

    juce::MidiKeyboardState keyboard;
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> arpStepView { -1 };     // for the display: the step the arpeggiator just played
    std::atomic<float> lfoView[numLfo] {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    Voice voices[voicePool];
    int orderCounter = 0, noteCounter = 0;
    float lastPitch = 60.0f;
    float wheel = 0.0f, bend = 0.0f, channelPressure = 0.0f;
    bool pedal = false;
    double globalLfo[numLfo] {};

    // arpeggiator: the keys held (in the order they were pressed) and the clock
    struct Held { int note = 0; float vel = 0.0f; };
    Held held[maxArpNotes]; int numHeld = 0;
    Held latched[maxArpNotes]; int numLatched = 0;
    bool latchArmed = false;                // with hold on: the next key after letting go of all starts a new chord
    double arpBeat = 0.0;                   // free clock (beats) when the host is not playing
    long long arpLastStep = -1;
    int arpIndex = 0, arpDir = 1;
    int arpPlaying[maxArpNotes]; int numArpPlaying = 0;
    double arpOffAt = -1.0;                 // beat position where the current step's notes are released
    bool arpWasOn = false;

    // output chain
    dsp::DelayLine chorusLine[2];
    double chorusPhase = 0.0;
    float satX[2] {};
    Biquad eqLow[2], eqHigh[2];
    float eqLowDb = 1000.0f, eqHighDb = 1000.0f;
    dsp::DelayLine delayLine[2];
    float delayLp[2] {};
    dsp::Smooth delayTime;
    dsp::Smooth volSmooth;
    juce::AudioBuffer<float> mix;

    std::atomic<float>* pLfo[numLfo][6] {};   // rate, sync, div, shape, delay, retrig
    std::atomic<float>* pSlot[numSlots][3] {};
    static float rd (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); }

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void allVoicesOff();
    void renderVoice (Voice&, int start, int n, int controlRate);
    float lfoValue (Voice&, int k, float phase, bool newCycle);
    void arpKeyDown (int note, float vel);
    void arpKeyUp (int note);
    void runArp (double beat);     // at the start of each small chunk: plays and releases the steps that are due
    void arpReleaseAll();
    void postChain (int n);

    struct VerbImpl;
    std::unique_ptr<VerbImpl> verb;
};
}

#pragma once
#include "RacoonProcessor.h"

namespace racoon::dormouse
{
// band-limited wavetables, built once and shared by every instance (read only after construction)
struct WaveBank
{
    static constexpr int size = 2048, frames = 8, levels = 11, tableBanks = 6;
    static const WaveBank& get();
    // level 0 holds up to 1023 harmonics, every level above halves them (mipmaps against aliasing)
    const float* table (int bank, int frame, int level) const
    {
        return data.data() + ((size_t) ((bank * frames + frame) * levels + level)) * (size + 1);
    }
    static int levelFor (float freq, double sampleRate);
    static const juce::StringArray& names();   // 6 wavetables + 2 analog (VA) shapes
private:
    WaveBank();
    std::vector<float> data;
};

constexpr int maxVoices = 32, voicePool = 40, maxUnison = 8, numEnv = 4, numLfo = 4, numSlots = 8;

enum ModSource { srcNone, srcLfo1, srcLfo2, srcLfo3, srcLfo4, srcEnv2, srcEnv3, srcEnv4, srcVel, srcAt, srcWheel, srcKey, srcRandom, numSources };
enum ModDest { dstNone, dstPitch, dstPitch2, dstPos1, dstPos2, dstCutoff, dstReso, dstLevel, dstPan, dstNoise, dstTexture, dstDetune, dstOsc1, dstOsc2, numDests };

struct Env
{
    enum Stage { idle, attack, decay, sustain, release };
    Stage stage = idle;
    float level = 0.0f;
    void start() { stage = attack; }
    void noteOff() { if (stage != idle) stage = release; }
    // advances `n` samples at once (control rate); returns the level at the end
    float advance (int n, float a, float d, float s, float r, double sr);
};

struct Voice
{
    bool active = false, held = false, sustained = false, stealing = false;
    int note = -1, order = 0;
    float velocity = 0.0f, pressure = 0.0f, random = 0.0f;
    float pitch = 60.0f, glideFrom = 60.0f, glidePos = 1.0f;
    float stealGain = 1.0f;
    Env env[numEnv];
    double lfoPhase[numLfo] {};
    float lfoRand[numLfo][2] {};
    float phase[maxUnison][2] {};      // oscillator phase per unison voice and oscillator
    float uniJit[maxUnison] {};        // small fixed detune per unison voice (less static)
    dsp::SVF f1[2], f2[2];
    float lastIn[2] {};
    // granular texture: a few short sine grains on the note's harmonics
    struct Grain { bool on = false; float phase = 0.0f, inc = 0.0f, pos = 0.0f, len = 1.0f, gl = 0.0f, gr = 0.0f; };
    Grain grains[4];
    float grainClock = 0.0f;
    float noiseLp = 0.0f;
    dsp::Rng rng;
    // smoothed control values (ramped across each control block)
    float ampNow = 0.0f, gNow = 0.0f, panL = 1.0f, panR = 1.0f;
    float driveX[2] {};   // ADAA memory of the drive
    float pos1 = 0.0f, pos2 = 0.0f;
};

// Dormouse: pad and atmosphere synth. Two wavetable / analog oscillators, noise and a granular texture,
// multimode filter, four envelopes, four slow LFOs, an 8-slot modulation matrix and a light chorus.
class Processor : public RacoonProcessor
{
public:
    Processor();
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 122.0; }
    int demoBars() const override { return 4; }

    static const juce::StringArray& sourceNames();
    static const juce::StringArray& destNames();
    static juce::String lfoId (int k, const char* what) { return "lfo" + juce::String (k + 1) + "_" + what; }
    static juce::String envId (int k, const char* what) { return "env" + juce::String (k + 1) + "_" + what; }
    static juce::String slotId (int k, const char* what) { return "m" + juce::String (k + 1) + "_" + what; }

    juce::MidiKeyboardState keyboard;            // on-screen keyboard (standalone and quick tests)
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> lfoView[numLfo] {};       // for the display

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    Voice voices[voicePool];
    int orderCounter = 0, noteCounter = 0;
    float lastPitch = 60.0f;
    float wheel = 0.0f, bend = 0.0f, channelPressure = 0.0f;
    bool pedal = false;
    double globalLfo[numLfo] {};
    // chorus
    dsp::DelayLine chorusLine[2];
    double chorusPhase = 0.0;
    dsp::Smooth volSmooth;
    juce::AudioBuffer<float> mix;

    // parameter pointers read on the audio thread (no string building there)
    std::atomic<float>* pOsc[2][6] {};       // bank, pos, oct, semi, fine, level
    std::atomic<float>* pEnv[numEnv][4] {};  // a, d, s, r
    std::atomic<float>* pLfo[numLfo][6] {};  // rate, sync, div, shape, phase, retrig
    std::atomic<float>* pSlot[numSlots][3] {};
    static float rd (std::atomic<float>* p) { return p->load (std::memory_order_relaxed); }

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void renderVoice (Voice&, int start, int n, int controlRate);
    float lfoValue (Voice&, int k, float phase, bool newCycle);
};
}

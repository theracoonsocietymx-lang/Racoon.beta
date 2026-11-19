#pragma once
#include "RacoonProcessor.h"
#include "ChinchillaDsp.h"

namespace racoon::chinchilla
{
constexpr int numHarmonies = 8, numDoubles = 4, numRoutes = 8, chopSteps = 16, pitchHistory = 512;

const juce::StringArray& modSources();
const juce::StringArray& modDestinations();

// Chinchilla, Vocal Lab: cleans the voice, finds its pitch and tunes it to a scale, to the notes you play or to the
// notes you drew in the editor; moves formants on their own; 8 harmony voices, doubles, vocoder, talkbox and robot;
// timing, chopper and effects; a modulation matrix and an analyzer.
class Processor : public RacoonProcessor
{
public:
    Processor();
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
    double demoBpm() const override { return 96.0; }
    int demoBars() const override { return 2; }
    racoon::PreviewSound::Kind demoSourceKind() const override { return racoon::PreviewSound::Kind::voice; }

    static juce::String hId (int voice, const char* what) { return "h" + juce::String (voice + 1) + "_" + what; }
    static juce::String mId (int route, const char* what) { return "m" + juce::String (route + 1) + "_" + what; }
    static juce::String chId (int step) { return "ch_s" + juce::String (step + 1); }

    // ------------------------------------------------------------------ for the screen and the tests
    std::atomic<float> detectedMidi { -1.0f }, correctedMidi { -1.0f }, targetMidi { -1.0f }, clarityNow { 0.0f };
    std::atomic<float> detectedHistory[pitchHistory] {}, correctedHistory[pitchHistory] {};
    std::atomic<int> historyPos { 0 };
    std::atomic<float> vocoderBands[Vocoder::bands] {};
    std::atomic<float> spectrum[64] {};
    void updateSpectrum();   // message thread: the last 1024 output samples to the 64 analyzer bands
    std::atomic<float> modValue[numRoutes] {};
    std::atomic<int> chopStepNow { 0 };
    int latency() const { return lat; }

    // ------------------------------------------------------------------ note editor
    struct Segment { double start, end; int note; int offset; };
    void startCapture();
    void stopCapture();                                // turns the captured pitch into notes
    bool capturing() const { return capture.load(); }
    std::vector<Segment> segments() const;
    void setSegmentOffset (int index, int semitones);
    void clearSegments();
    juce::MidiFile exportMidi() const;
    juce::File exportMidiFile() const;
    std::atomic<int> capturedFrames { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    int lat = 2160;
    History hist;
    Yin yin;
    PsolaVoice lead, harm[numHarmonies];
    CarrierSynth synth;
    Singer singer;                  // sings the MIDI notes (Cantar las notas MIDI)
    Vocoder vocoder;
    Talkbox talkbox;
    racoon::sdsp::Verb reverb;   // 8-line FDN (was juce Freeverb)
    float satX[2] {}, phoneX = 0.0f;   // ADAA memory
    // cleanup
    Biquad hp, deessBand, phone[2];
    float gateEnv = 0.0f, gateGain = 1.0f, deessEnv = 0.0f, compEnv = 0.0f, compGain = 1.0f;
    // pitch state
    float midiIn = -1.0f, centre = -1.0f, correction = 0.0f, heldTarget = -1.0f;
    double period = 0.0;
    // delays (dry, harmonies, doubles, fx delay)
    dsp::DelayLine dry[2], harmDelay[numHarmonies], dblLine, fxDelay[2];
    double dblPhase[numDoubles] {}, lfoPhase[2] {};
    float lfoHold[2] {}, dblRand[numDoubles] {};
    dsp::Rng rng;
    // chopper
    float chopGain = 1.0f;
    juce::int64 lastChopStep = -1;
    std::vector<float> stutter;
    int stutterLen = 0, stutterPos = 0;
    bool stuttering = false;
    // analyzer: the audio thread only writes the output into a ring; the FFT runs on the screen's timer (updateSpectrum)
    std::vector<float> fftIn;
    float scopeRing[2048] {};
    std::atomic<int> scopeWrite { 0 };
    int fftPos = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    float dlNow = -1.0f;   // delay time that glides to a new division (no click)
    float metalPhase = 0.0f;
    float lastVelocity = 0.8f, modWheel = 0.0f, voiceEnv = 0.0f;
    int historyDecim = 0;
    float hTarget[numHarmonies] {}, hGate[numHarmonies] {}, hVoiced = 0.0f, desiredCorrection = 0.0f;
    bool hHas[numHarmonies] {};

    // note editor (frames written by the audio thread while capturing)
    struct Frame { double ppq; float midi; };
    std::vector<Frame> frames;
    std::atomic<int> frameCount { 0 };
    std::atomic<bool> capture { false };
    mutable juce::SpinLock segLock;
    std::vector<Segment> segs;

    std::atomic<float>* hp_[numHarmonies][8] {};
    std::atomic<float>* mp_[numRoutes][3] {};
    std::atomic<float>* chp_[chopSteps] {};
    float modulation (int destination) const;
    float modNow[16] {};
    void updateModulation (int n);
};
}

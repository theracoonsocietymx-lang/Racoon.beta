#pragma once
#include "RacoonProcessor.h"
#include "PorcupineVoice.h"

namespace racoon::porcupine
{
enum Mode { satbMode = 0, harmonizeMode, unisonMode };
constexpr int maxVoices = 64, sylSteps = 16, numOutBuses = 8, keyswitchLow = 24;
juce::StringArray modeNames();
juce::StringArray busNames();

// Porcupine, Choir & Ensemble: a choir of formant voices in four sections that splits your chords into soprano, alto,
// tenor and bass with real voice leading (or harmonizes a melody in a key), sings vowels and syllables, and brings
// ensemble strings and brass along. Each section stands in its place on a stage with a hall, and can go to its own
// output (8 stereo outputs).
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
    double demoBpm() const override { return 80.0; }
    int demoBars() const override { return 4; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    static juce::String prefix (int section);          // sop, alt, ten, bas, str, br
    static juce::String sylId (int step) { return "syl" + juce::String (step + 1); }

    // for the screen and the tests
    std::atomic<int> satbShown[numChoir];
    std::atomic<float> sectionLevel[numSections];
    std::atomic<int> sylStepShown { 0 }, activeVoices { 0 }, artShown { 0 };
    std::atomic<int> degreeShown { -1 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static BusesProperties buses();

    Voice voices[maxVoices];
    juce::AudioBuffer<float> sec, hallIn;
    dsp::DelayLine preDelay[numSections][2];
    float stageLp[numSections][2] {};
    racoon::sdsp::Verb hall;
    juce::uint64 counter = 0;

    // keys and harmony
    float keyVel[128] {};
    int heldOrder[128] {}, heldCount = 0;
    bool sustained[128] {};
    bool sustainPedal = false, chordDirty = false;
    Satb satb;
    int previousDegree = -1;
    float lastVelocity = 0.8f, cc1 = 1.0f, cc11 = 1.0f, volume = 1.0f;
    int lastArtParam = -1, lastMode = -1;
    std::atomic<int> currentArt { 0 };

    // syllables
    int sylStep = 0;
    juce::int64 lastSylIndex = -1;
    double internalPpq = 0.0, vowelPhase = 0.0;
    bool internalRunning = false;

    VoiceSettings settingsFor (int section) const;
    Voice* findVoice (int section, int key);
    Voice* allocVoice();
    void currentSyllable (Consonant&, float& vowel, float& hum) const;
    void startVoice (int section, int key, int note, float vel);
    void applyChord();
    void releaseLines();
    void noteOn (int note, float vel);
    void noteOff (int note);
    bool sectionOn (int s) const;
    // parameters read on the audio thread, looked up once (no strings built while playing)
    std::atomic<float>* secOn[numSections] {}, * secLvl[numSections] {}, * secPan[numSections] {}, * secOct[numSections] {};
    std::atomic<float>* sylParam[sylSteps] {};
};
}

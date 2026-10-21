#pragma once
#include "RacoonProcessor.h"
#include "FerretGuitar.h"
#include "FerretFx.h"

namespace racoon::ferret
{
constexpr int userSteps = 64, numSets = 2, keyswitchLow = 24;   // MIDI 24..33 (C1..A1 with C4 = 60) pick the articulation, below the guitar
enum Mode { playerMode = 0, pickingMode, instrumentMode };

juce::StringArray modeNames();
juce::StringArray pickPatternNames();
juce::StringArray rateNames();
double rateBeats (int rateIndex);

// one string struck by the player (a strum is several of these with growing delays)
struct Hit
{
    int string = -1;            // -1 with choke: the hand mutes every string
    int fret = 0;
    float velocity = 0.0f;
    double delaySeconds = 0.0;
    Art art = sustain;
    bool choke = false;
};

// everything the rhythm player needs to decide a step; the same function plays live and writes the MIDI export
struct PlayerSettings
{
    int mode = playerMode, pickPattern = 0;
    float intensity = 0.6f, density = 0.5f, variation = 0.0f, swing = 0.0f, anticipation = 0.0f;
    float humanMs = 0.0f, humanVel = 0.0f, strumMs = 12.0f;
    juce::uint32 seed = 1;
    bool restMute = true;
    Art art = sustain;          // the articulation chosen by keyswitch or parameter
    double stepSeconds = 0.125;
};
// the token played at a step, after variation, density and anticipation
char stepToken (const PlayerSettings&, const char* pattern, int length, juce::int64 step);
int stepHits (const PlayerSettings&, const char* pattern, int length, juce::int64 step, const Voicing&, Hit* out, int maxHits);

// Ferret, Guitar Lab: a modelled guitar (six waveguide strings, body and pickups) played by a rhythm guitarist.
// It recognises the chord you hold, finds a real voicing on the fretboard and strums it with the patterns of funk,
// disco, indie dance, nu-disco, rock, post-punk and dark disco, or picks it, or plays your notes as they are.
// After the strings: pedals, amplifier and cabinet with microphones.
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
    double demoBpm() const override { return 100.0; }
    int demoBars() const override { return 4; }

    // the pattern of the Usuario style: 64 tokens, editable from the screen
    std::atomic<char> userPattern[userSteps];
    void setUserStep (int step, char token) { userPattern[(size_t) juce::jlimit (0, userSteps - 1, step)].store (token); }
    char userStep (int step) const { return userPattern[(size_t) juce::jlimit (0, userSteps - 1, step)].load(); }
    void copyStyleToUser();                       // the pattern playing now becomes the user pattern
    juce::String patternNow() const;              // tokens of the pattern being used (message thread)

    // tuning with capo, per-string offsets and the instrument's own transposition
    void openNotes (int* out) const;

    // for the screen
    std::atomic<int> shownFret[numStrings], voicingFret[numStrings];
    std::atomic<float> shownLevel[numStrings];
    std::atomic<int> chordRoot { -1 }, chordType { -1 }, chordBass { -1 };
    std::atomic<int> currentArt { 0 }, currentStep { -1 };
    juce::String chordName() const;

    // the chords that were played (with their position in the song) and the MIDI export of the guitar part
    struct LogEntry { double ppq; Voicing voicing; int root, type, bass; };
    static constexpr int maxLog = 512;
    int logSize() const { return logCount.load(); }
    void clearLog() { clearLogRequest.store (true); }
    juce::MidiFile exportMidi (int barsWhenNoLog) const;
    juce::File exportMidiFile (int barsWhenNoLog) const;

    // the player's decisions for the current settings (message thread or tests)
    PlayerSettings playerSettings (double bpmNow) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    StringVoice strings[numSets][numStrings];
    Body body[numSets];
    Amp amp;
    Cabinet cab;
    Pedals pedals;
    juce::AudioBuffer<float> work;

    // scheduled plucks (sample accurate)
    struct Ev { juce::int64 at; int set, string, fret; float velocity; Art art; bool choke, legato; double slideFromHz; };
    static constexpr int maxEvents = 1024;
    Ev events[maxEvents];
    int numEvents = 0;
    juce::int64 clock = 0;
    void schedule (const Ev&);
    void fire (const Ev&);

    // keys
    float keyVel[128] {};
    int heldOrder[128] {}, heldCount = 0;
    int noteString[128];
    int keyOnString[numStrings];
    bool sustainPedal = false, pendingRelease[numStrings] {};
    float wheelSemis = 0.0f, modWheel = 0.0f;
    int sameTimeIndex = 0, lastNoteSample = -1, lastArtParam = -1, lastMode = -1;
    void noteOn (int note, float vel, int sample);
    void noteOff (int note, int sample);
    void chordChanged (int sample);
    bool chordDirty = false;

    // player
    ChordInfo chord;
    Voicing voicing;
    bool chordActive = false;
    int handFret = 3;
    double internalPpq = 0.0;
    bool internalRunning = false;
    LogEntry chordLog[maxLog];
    std::atomic<int> logCount { 0 };
    std::atomic<bool> clearLogRequest { false };
    char livePattern[userSteps] {};
    int livePatternLen = 16;
    void refreshPattern();
    void runSteps (double ppqStart, double ppqEnd, juce::int64 chunkClock, double samplesPerPpq);
};
}

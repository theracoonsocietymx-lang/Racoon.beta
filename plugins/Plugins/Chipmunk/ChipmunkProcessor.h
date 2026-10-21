#pragma once
#include "RacoonProcessor.h"

namespace racoon::chipmunk
{
// harmony tables (versioned: a project remembers which tables it was made with)
namespace harmony
{
    constexpr int version = 1;
    const juce::StringArray& chordNames();
    const std::vector<int>& chordIntervals (int type);
    const juce::StringArray& scaleNames();
    const std::vector<int>& scale (int index);
    juce::String nameOf (const std::vector<int>& pitches);   // "Cmaj7", "Am", ... or empty
    juce::String noteName (int pitch);
}

constexpr int tableSteps = 16;

// fixed-size note lists (the audio thread never allocates)
template <int N> struct Notes
{
    int n = 0;
    std::array<int, N> p {};
    std::array<int, N> vel {};
    void clear() { n = 0; }
    void add (int pitch, int v = 100) { if (n < N) { p[(size_t) n] = pitch; vel[(size_t) n] = v; ++n; } }
    bool contains (int pitch) const { for (int i = 0; i < n; ++i) if (p[(size_t) i] == pitch) return true; return false; }
    void sort() { std::sort (p.begin(), p.begin() + n); }
};
using Chord = Notes<16>;

// Chipmunk: MIDI effect. Notes in, chords and arpeggios out: fixed or diatonic chords, chord memory, a progression
// played from the keyboard, inversions, voicing and voice leading inside a register; arpeggiator with a 16-step table
// (probability, ratchets, accents), swing, strum and humanize. Every note-on gets its note-off.
class Processor : public RacoonProcessor
{
public:
    Processor();
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    void bypassStateChanged (bool, juce::MidiBuffer&) override;
    void bypassedMidi (juce::MidiBuffer&) override;
    std::vector<FactoryPreset> factoryPresets() const override;
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 120.0; }
    int demoBars() const override { return 4; }
    bool playsOwnMidi() const override { return true; }
    // the export carries what it really plays (its own notes), not the notes that drive it
    double exportNotes (juce::MidiMessageSequence& seq, double beats, double tpq) const override
    {
        Processor fresh;
        captureMidiOutput (fresh, seq, beats, tpq);
        return 4.0 * patternBars();
    }
    bool ownSoundOn() const override { return value ("ownsound") > 0.5f; }
    bool producesMidi() const override { return true; }

    static juce::String stepId (int k, const char* what) { return "s" + juce::String (k + 1) + "_" + what; }
    static juce::String progId (int k) { return "prog" + juce::String (k + 1); }

    // chord for one input note with the current settings
    Chord makeChord (int note, const Chord* previous) const;
    std::vector<int> chordFor (int note) const;   // same, as a vector (tests, display)

    // chord memory and MIDI learn (armed from the editor, completed on the audio thread)
    std::atomic<bool> learnChord { false };
    std::atomic<int> memory[8] { { 0 }, { 4 }, { 7 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 } };
    std::atomic<int> learnParam { -1 };          // parameter index waiting for a CC
    std::atomic<int> ccMap[128];                  // CC -> parameter index (-1 none)
    std::atomic<bool> panicRequest { false };

    // for the display (written by the audio thread)
    std::atomic<int> currentStep { -1 };
    std::atomic<juce::uint64> soundingMask[2] {}; // output notes sounding (bits 0..127)
    std::atomic<juce::uint64> heldMask[2] {};     // input notes held
    std::atomic<int> lastChordPitches[8];
    std::atomic<int> lastChordSize { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    // at: samples from block start; src: input note (direct chords) or -2 (arp); id pairs an arp note-on with its note-off
    struct Pending { int pitch = -1, channel = 1, vel = 0; double at = 0; int src = -1; juce::int64 id = -1; };
    static constexpr int maxPending = 512;
    Pending pendingOn[maxPending], pendingOff[maxPending];
    int numOn = 0, numOff = 0;

    // inputs
    struct Held { int note = 0, vel = 0, order = 0; bool physical = false; };
    Held held[128];
    int numHeld = 0;
    int orderCounter = 0, physicalCount = 0;
    int channel = 1;
    bool rawOn[128] {};                           // notes that went through while bypassed

    // outputs
    bool sounding[128] {};
    juce::int64 soundingId[128] {};
    juce::int64 idCounter = 0;
    int chordRef[128] {};                         // direct chord mode: how many input notes use this pitch
    Chord mapping[128];                           // input note -> output pitches (direct mode)
    Chord lastChord;                              // for voice leading
    bool strumFlip = false;

    // clock
    double internalPpq = 0.0, lastPpqEnd = -1.0;
    bool wasPlaying = false, internalRunning = false, lastArp = false, lastKeep = false;
    double blockPpq0 = 0.0;
    juce::int64 lastStep = std::numeric_limits<juce::int64>::min();

    // chord memory capture
    Chord learning;

    std::atomic<float>* stepOn[tableSteps] {};
    std::atomic<float>* stepAcc[tableSteps] {};
    std::atomic<float>* stepProb[tableSteps] {};
    std::atomic<float>* stepRat[tableSteps] {};
    std::atomic<float>* prog[8] {};

    void emitOn (juce::MidiBuffer&, int pitch, int vel, int offset, juce::int64 id);
    void emitOff (juce::MidiBuffer&, int pitch, int offset);
    void scheduleOn (int pitch, int vel, double at, int src, juce::int64 id = 0);
    void scheduleOff (int pitch, double at, juce::int64 id = -1);
    void flushAll (juce::MidiBuffer&, int offset);
    void inputOn (juce::MidiBuffer&, int note, int vel, int offset);
    void inputOff (juce::MidiBuffer&, int note, int offset);
    void releaseDirect (juce::MidiBuffer&, int inputNote, int offset);
    void arpStep (juce::int64 k, double at, double stepSamples, int n);
    using Pool = Notes<128>;
    void buildPool (Pool& sorted, Pool& played) const;
    static double hash01 (juce::uint32 seed, juce::int64 k, int salt);
};
}

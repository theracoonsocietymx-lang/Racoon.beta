#pragma once
#include "Common.h"
#include "../Params.h"

namespace be
{
struct SeqStep
{
    bool on = true;
    int note = 0;          // semitone offset from root/key (0..24)
    int octave = 0;        // -2..+2
    float gate = 0.5f;     // fraction of step
    float vel = 0.8f;
    bool accent = false, slide = false, tie = false;
    float prob = 1.0f;
    int ratchet = 1;       // 1..4
    float micro = 0.0f;    // -0.5..0.5 step
    float mod = 0.0f;      // seq mod lane 0..1

    // extra notes that start on this step (chords). They share velocity, accent, probability and timing
    // with the step, and each one has its own pitch and length.
    struct ChordNote { int pitch = 0; float len = 0.5f; };   // pitch: semitones from root incl. octave (-24..48), len in steps
    static constexpr int kMaxChord = 6;
    int numChord = 0;
    ChordNote chord[kMaxChord];
    bool hasChordPitch (int p) const { for (int i = 0; i < numChord; ++i) if (chord[i].pitch == p) return true; return false; }
    bool addChord (int p, float len)
    {
        if (numChord >= kMaxChord || hasChordPitch (p)) return false;
        chord[numChord++] = { p, len };
        return true;
    }
    void removeChord (int i)
    {
        if (i < 0 || i >= numChord) return;
        for (int k = i; k < numChord - 1; ++k) chord[k] = chord[k + 1];
        --numChord;
    }
};

struct SeqPattern
{
    SeqStep steps[kSeqMaxSteps];
};

struct SeqData
{
    SeqPattern patterns[kSeqPatterns];
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);
    void initDefault();
};

struct SeqEvent
{
    int sampleOffset = 0;
    bool noteOn = true;
    int note = 60;
    float velocity = 0.8f;
    bool accent = false, slide = false;
    float mod = 0.0f;
};

struct SeqParams
{
    int mode = 0, pattern = 0, length = 16, rate = 4, root = 0, scale = 1, arpMode = 0, arpOct = 1, trigger = 1;
    float swing = 0.0f, gateScale = 1.0f, bpm = 124.0f;
    bool keyTranspose = true, quantize = true, latch = false, hostSync = true;
    bool preview = false;   // PLAY button: run the pattern without holding a key or starting the host
};

class Sequencer
{
public:
    void prepare (double sampleRate);
    void reset();

    // keyboard input (notes are consumed by the sequencer when active)
    void keyDown (int note, float vel);
    void keyUp (int note);
    bool hasHeldKeys() const { return numHeld > 0; }

    // generate events for one block
    void process (const SeqData& data, const SeqParams& p, int numSamples, bool hostPlaying, double hostBpm,
                  double hostBeatsAtStart, std::vector<SeqEvent>& out);

    int currentStep() const { return uiStep.load(); }
    float currentMod() const { return lastMod; }

    // build a MIDI file from a pattern (for drag & drop export)
    static juce::MidiFile toMidiFile (const SeqData& data, const SeqParams& p, int patternIndex, double bpm);
    // musical variation of a pattern respecting the scale
    static void makeVariation (SeqPattern& pat, int length, int scale, float amount, uint32_t seed);
    static int quantizeToScale (int semis, int scale);

private:
    double sr = 48000.0;
    // held keys
    int held[32] {};
    float heldVel[32] {};
    int numHeld = 0;
    int latched[32] {};
    int numLatched = 0;
    bool latchArmed = false;

    // timing
    bool running = false;
    double anchorBeats = 0.0;      // beat position corresponding to step 0
    double internalBeats = 0.0;
    double lastBlockEnd = -1.0;
    long long lastStepIndex = -1;
    int lastKey = -1;

    struct PendingOff { double beat; int note; };
    std::vector<PendingOff> pending;
    int soundingNote = -1;
    bool soundingSlideNext = false;
    Rng rng;
    std::atomic<int> uiStep { -1 };
    float lastMod = 0.0f;
    int arpIndex = 0;
    int arpDir = 1;

    int arpNoteFor (const SeqParams& p, long long stepIndex);
};
} // namespace be

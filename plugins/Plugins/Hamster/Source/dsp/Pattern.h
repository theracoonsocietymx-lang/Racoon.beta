#pragma once
#include "Common.h"
#include "../Params.h"

namespace be
{
// ---------------------------------------------------------------- step data
struct Lock
{
    uint8_t param = 255;      // local track param index, 255 = empty
    uint16_t value = 0;       // normalised 0..65535
    float norm() const { return (float) value / 65535.0f; }
};

enum class RetrigMode : uint8_t { Ratchet, Flam, RollUp, RollDown };

struct Step
{
    bool on = false;
    bool accent = false;
    uint8_t vel = 100;          // 1..127
    uint8_t cond = 0;           // index into condNames()
    uint8_t condA = 1, condB = 2;
    uint8_t prob = 100;         // percent
    int8_t micro = 0;           // -50..+50 percent of a step
    uint8_t retrig = 1;         // 1..8 hits per step (flam: 2)
    uint8_t retrigMode = 0;
    uint8_t len = 4;            // gate in quarter steps (1..64)
    int8_t pitch = 0;           // semitones -24..+24
    uint8_t numLocks = 0;
    Lock locks[kMaxLocks];

    bool hasLock (int p) const { for (int i = 0; i < numLocks; ++i) if (locks[i].param == p) return true; return false; }
    float lockValue (int p, float fallback) const { for (int i = 0; i < numLocks; ++i) if (locks[i].param == p) return locks[i].norm(); return fallback; }
    void setLock (int p, float norm01);
    void clearLock (int p);
    bool isDefault() const;
};

struct TrackPattern
{
    int length = 16;            // 1..128 steps
    int res = 3;                // index into seqResNames() (default 1/16)
    Step steps[kMaxSteps];
};

struct Pattern
{
    TrackPattern tracks[kNumTracks];
    int masterLength = 16;      // 1/16 steps, defines the pattern loop for chaining and switching (1..128)
    juce::String name;
    bool isEmpty() const;
};

struct SongEntry { int pattern = 0; int repeats = 1; };

struct Groove
{
    bool active = false;
    juce::String name;
    float timing[16] {};        // offset in fractions of a 1/16 step (-0.5..0.5)
    float velocity[16] {};      // velocity scale offset (-1..1)
};

struct SeqData
{
    Pattern pats[kNumPatterns];
    static constexpr int kMaxSong = 64;
    SongEntry song[kMaxSong];
    int songLength = 0;
    Groove groove;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);
    static juce::String patternToString (const Pattern& p);
    static void patternFromString (Pattern& p, const juce::String& s);
};
using SeqPtr = std::shared_ptr<const SeqData>;

// ---------------------------------------------------------------- pattern generators / transforms (message thread)
namespace PatternTools
{
    void euclid (TrackPattern& t, int hits, int steps, int rotate, int velocity = 100);
    void randomize (TrackPattern& t, int role, float density, float amount, juce::Random& rng);
    void humanize (TrackPattern& t, float amount, juce::Random& rng);
    void shift (TrackPattern& t, int by);
    void reverse (TrackPattern& t);
    void clear (TrackPattern& t);
    // generates a musically related variation of a whole pattern (Variation Engine)
    void variation (const Pattern& src, Pattern& dst, const int* roles, float intensity, int flavour, juce::Random& rng);
    // fill generator for the last bar of a pattern
    void makeFill (Pattern& p, const int* roles, float intensity, juce::Random& rng);
    // MIDI file of a pattern (notes from each track's MIDI note)
    juce::MidiFile toMidiFile (const Pattern& p, const int* notes, double bpm, int bars, float swing);
}
} // namespace be

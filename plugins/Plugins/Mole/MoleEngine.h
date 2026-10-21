#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>

// The rhythm brain of Mole, without audio or windows: from the lanes, their steps and the musical time it decides
// which notes sound and exactly when. It is a pure function of the song position, so the same bar always plays the
// same notes (random choices come from a hash of seed, lane and step, never from a running generator).
namespace racoon::mole
{
constexpr int numLanes = 16, maxSteps = 128, numSlots = 8;

// --------------------------------------------------------------------------- steps (packed for lock-free sharing)
enum Cond { always = 0, c1of2, c2of2, c1of4, c2of4, c3of4, c4of4, firstTime, notFirstTime, ifPrev, ifNotPrev, onlyFill, notFill, numConds };
juce::StringArray conditionNames();

struct Step
{
    bool on = false;
    int vel = 100;        // 1..127
    int prob = 100;       // 0..100
    int ratchet = 1;      // 1..16
    int micro = 0;        // -50..50 (% of the step)
    int cond = always;
    int len = 100;        // 10..400 (% of the step)
    juce::uint64 pack() const;
    static Step unpack (juce::uint64);
};

// --------------------------------------------------------------------------- per lane and global settings (from parameters)
enum class LaneMode { euclid = 0, manual, complement };

struct Lane
{
    bool on = true, mute = false, solo = false, fill = true;
    LaneMode mode = LaneMode::euclid;
    int steps = 16, pulses = 4, rotation = 0;
    double stepBeats = 0.25;      // length of one step (rate, or span / steps for polyrhythms)
    int note = 36, channel = 10, velocity = 100;
    double gate = 0.5;            // fraction of the step
    double prob = 1.0;            // lane probability
    int ratchet = 1;              // extra ratchet for every hit, applied with ratchetProb
    double ratchetProb = 0.0;
    double ratchetCurve = 0.0;    // -1 falling .. 1 rising velocities
    double ratchetAccel = 0.0;    // -1 slowing .. 1 accelerating
    double microMs = 0.0;
    double swing = 1.0;           // how much of the global swing this lane takes
    int refLane = 0;              // complement mode: the lane it answers
    double density = 0.5;         // complement mode
    int accentEvery = 0;          // every n-th hit accented (0 off)
    double accent = 0.25;         // accent amount (velocity boost)
};

enum class Humanize { tight = 0, looseFunk, humanPercussion, mechanical };
enum class BreakType { silence = 0, thinOut, roll, accentShift };
enum class GrooveMode { timing = 0, accents, both };

struct Groove
{
    std::array<float, 16> offset {};   // fraction of a 16th, -0.5..0.5, per 16th of the bar
    std::array<float, 16> velocity {}; // multiplier around 1
    Groove() { velocity.fill (1.0f); }
};
const juce::StringArray& grooveNames();          // built-in templates + "Aprendido"
Groove builtInGroove (int index);

struct Globals
{
    double swing = 0.5;           // 0.5 straight .. 0.75 heavy
    double swingGridBeats = 0.25; // 1/16 (0.5 = 1/8)
    Humanize profile = Humanize::tight;
    double humanAmount = 0.0;     // 0..1
    double humanTimeMs = 6.0, humanVel = 0.12;
    juce::uint32 seed = 1;
    double mutation = 0.0;        // 0..1
    int mutateEvery = 0;          // cycles between mutations (0 off)
    bool preserveKick = true, preserveSnare = true;
    int fillEvery = 0;            // bars (0 off)
    double fillBeats = 1.0, fillDensity = 0.5, fillComplexity = 0.5;
    int breakEvery = 0;           // bars (0 off)
    double breakBeats = 2.0;
    BreakType breakType = BreakType::silence;
    double probScale = 1.0, velScale = 1.0;
    double pushPullMs = 0.0, timingRangeMs = 60.0, quantize = 0.0;
    int groove = 0;
    double grooveAmount = 0.0;
    GrooveMode grooveMode = GrooveMode::both;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
    int transpose = 0;
    double startPpq = 0.0;        // where playback started (for the "first time" condition)
};

struct Event
{
    double ppq = 0.0;             // final position, with swing, groove and humanize
    double lengthBeats = 0.1;
    int note = 36, channel = 10, vel = 100, lane = 0;
};

// --------------------------------------------------------------------------- the engine
class Engine
{
public:
    Engine();

    // settings (written by the processor every block; the UI only reads them)
    std::array<Lane, numLanes> lanes;
    Globals g;
    Groove learned;

    // steps: [slot][lane][step], packed, readable from any thread
    std::atomic<juce::uint64> steps[numSlots][numLanes][maxSteps];
    std::atomic<int> slot { 0 };
    Step getStep (int lane, int step, int whichSlot = -1) const;
    void setStep (int lane, int step, const Step&, int whichSlot = -1);
    void clearLane (int lane, int whichSlot = -1);

    // every event whose final position falls in [from, to)
    void collect (double from, double to, const std::function<void (const Event&)>&) const;

    // building blocks (public for tests and the display)
    static bool euclid (int steps, int pulses, int rotation, int position);
    bool baseActive (int lane, juce::int64 absStep) const;          // the pattern before probability and conditions
    bool hitsAt (int lane, juce::int64 absStep) const;              // after conditions, probability, mutation, breaks
    bool inFill (double ppq) const;
    bool inBreak (double ppq) const;
    juce::int64 cycleOf (int lane, juce::int64 absStep) const { return lanes[(size_t) lane].steps > 0 ? floorDiv (absStep, lanes[(size_t) lane].steps) : 0; }
    int posOf (int lane, juce::int64 absStep) const { const int n = juce::jmax (1, lanes[(size_t) lane].steps); return (int) (((absStep % n) + n) % n); }
    static double hash01 (juce::uint32 seed, juce::int64 k, int salt);
    static juce::int64 floorDiv (juce::int64 a, juce::int64 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    bool anySolo() const;
    Groove currentGroove() const;

private:
    bool mutatedActive (int lane, juce::int64 absStep, int pos, juce::int64 cycle) const;
    double offsetBeats (int lane, juce::int64 absStep, double nominalPpq, int stepMicro, int subIndex) const;
    void emitStep (int lane, juce::int64 absStep, double from, double to, const std::function<void (const Event&)>&) const;
};
}

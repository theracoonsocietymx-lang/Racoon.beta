#pragma once
#include <JuceHeader.h>

class BassEngineProcessor;

namespace be
{
// ---------------------------------------------------------------- Bass DNA
struct DnaResult
{
    bool ok = false;
    float f0 = 0.0f, midiNote = 0.0f;
    float attack = 0.005f, decay = 0.3f, sustain = 0.6f, release = 0.1f;
    float brightness = 0.0f;       // spectral centroid in harmonics
    float brightnessDrop = 0.0f;   // first frame vs last frame
    float noisiness = 0.0f;        // 0..1
    float fundamentalRatio = 0.0f; // energy of h1 vs total
    float duration = 0.0f;
    std::vector<std::vector<float>> frames;   // 8 x 64 harmonic amplitudes
    juce::String summary;
};

namespace BassDNA
{
    DnaResult analyse (const juce::AudioBuffer<float>& audio, double sampleRate);
    DnaResult analyseFile (const juce::File& f);
    void apply (BassEngineProcessor& proc, const DnaResult& r, const juce::String& sourceName);
}

// ---------------------------------------------------------------- Intelligent randomizer
struct RandomLocks
{
    bool osc = false, sub = false, filter = false, env = false, lfo = false, fx = false, matrix = false;
    bool fundamental = true, volume = true, character = false;
};

namespace Randomizer
{
    void randomize (BassEngineProcessor& proc, float amount, const RandomLocks& locks, juce::uint32 seed);
}

// ---------------------------------------------------------------- Bass Focus diagnostics
struct FocusReport
{
    struct Item { int severity; juce::String text; };   // 0 ok, 1 note, 2 warning
    std::vector<Item> items;
};

namespace BassFocus
{
    // spectrum: magnitude per bin (linear), binHz: width of a bin
    FocusReport diagnose (const std::vector<float>& spectrum, float binHz, float lowCorrelation, float stereoCorrelation,
                          float subStability, float peak, float rms, float leadNote);
}
} // namespace be

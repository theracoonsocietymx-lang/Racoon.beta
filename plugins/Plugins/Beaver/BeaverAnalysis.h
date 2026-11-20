#pragma once
#include <JuceHeader.h>
#include "Utf8.h"

// Beaver's materials: an audio file or a picture turned into frames of harmonic levels (and, for audio, 32 bands of
// noise and the pitch of each frame). Everything here runs off the audio thread.
namespace racoon::beaver
{
constexpr int numPartials = 511, numNoiseBands = 32;

struct FrameSet
{
    float frameSeconds = 0.005f;   // the hop as time
    float refHz = 110.0f;          // the pitch the levels are measured against
    int frames = 0;
    std::vector<uint8_t> level;    // frames x 511, 0 = -96 dB, 255 = 0 dB
    std::vector<uint8_t> noise;    // frames x 32, same scale (empty for pictures)
    std::vector<float> f0;         // per frame, 0 = no pitch (empty for pictures)
    juce::String name;
    bool builtIn = false;

    static inline uint8_t q (float db) { return (uint8_t) juce::jlimit (0, 255, juce::roundToInt ((db + 96.0f) * 255.0f / 96.0f)); }
    static inline float dq (uint8_t v) { return (float) v * 96.0f / 255.0f - 96.0f; }
    float levelDb (int frame, int k) const { return dq (level[(size_t) frame * numPartials + (size_t) k]); }   // k = 0..510
    float noiseDb (int frame, int b) const { return noise.empty() ? -96.0f : dq (noise[(size_t) frame * numNoiseBands + (size_t) b]); }

    juce::MemoryBlock save() const;                       // gzipped
    static std::shared_ptr<FrameSet> load (const juce::MemoryBlock&);
};

// the 32 noise bands, 50 Hz to 16 kHz (log spaced): lower edge of band b, b = 0..32
float bandEdge (int b);

struct AnalysisOptions
{
    bool asSpectrum = false;       // Como espectro: read at a fixed note instead of following the audio's pitch
    int refNote = 33;              // for Como espectro
};

// YIN on a mono buffer at 48 kHz: one f0 per hop (0 = unvoiced)
std::vector<float> trackPitch (const float* x, int n, double sr, int hop);

// mono audio at 48 kHz to a FrameSet (the work of a few seconds for a 30 s file)
std::shared_ptr<FrameSet> analyse (const juce::AudioBuffer<float>& mono48k, const AnalysisOptions&, const juce::String& name);
// any audio file: read up to 30 s, mono, 48 kHz, peak at -1 dB, then analyse
std::shared_ptr<FrameSet> analyseFile (const juce::File&, const AnalysisOptions&, juce::String& error);
// a picture: columns are frames, rows are partials
std::shared_ptr<FrameSet> fromImage (const juce::Image&, bool logAxis, float rangeDb, float seconds, const juce::String& name);

// the built-in materials, made by code (index 1..8 of the material choice; 0 = the user's own)
std::shared_ptr<const FrameSet> builtInMaterial (int index);
juce::AudioBuffer<float> renderMaterialSource (int index);    // the audio a material is analysed from (tests use it)
juce::Image makeBuiltInImage (int which);                     // 0 horizonte, 1 escalera
}

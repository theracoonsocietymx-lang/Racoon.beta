#pragma once
#include <JuceHeader.h>
#include "../dsp/Assets.h"
#include "../dsp/Pattern.h"

class DrumEngineProcessor;

namespace be
{
// ---------------------------------------------------------------- Drum DNA: analyse a one-shot and rebuild it as a synth voice
struct DrumDnaResult
{
    bool ok = false;
    int role = 0;
    float pitchHz = 0.0f, startPitchHz = 0.0f, attackMs = 0.0f, decaySec = 0.0f, centroidHz = 0.0f, flatness = 0.0f, lowRatio = 0.0f, clickRatio = 0.0f;
    int bursts = 1;
    juce::String summary;
    SamplePtr sample;
};

namespace DrumDNA
{
    DrumDnaResult analyse (const juce::File& f);
    DrumDnaResult analyse (SamplePtr s);
    // mode 0: synth only, 1: synth + original as sample layer (hybrid), 2: sample only
    void apply (DrumEngineProcessor& p, int track, const DrumDnaResult& r, int mode);
}

// ---------------------------------------------------------------- Groove DNA: extract timing and accents from a loop
struct GrooveResult
{
    bool ok = false;
    Groove groove;
    double bpm = 0.0;
    int onsets = 0, bars = 0;
    juce::String summary;
};

namespace GrooveDNA
{
    GrooveResult analyse (const juce::File& f, double bpmHint = 0.0);
}

// ---------------------------------------------------------------- Kick / bass helpers
namespace KickBass
{
    juce::String noteName (float hz);
    // semitone offset that brings the kick fundamental onto the bass root (nearest octave)
    float tuneOffsetTo (float kickHz, float bassHz);
    juce::String advice (float kickHz, float bassHz, float collision);
}
} // namespace be

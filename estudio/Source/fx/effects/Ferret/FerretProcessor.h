#pragma once
#include "fx/core/RacoonProcessor.h"

namespace racoon::ferret
{
// Ferret: stereo / ping-pong / dual / tape / multitap delay, synced or free, with a safe feedback loop.
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

    static const juce::StringArray& modeNames();
    double delaySecondsFor (int channel) const;   // current setting (sync or free), for the display and the tests

    std::atomic<float> wetLevel { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    struct Head { float delay = 0.0f; };
    dsp::DelayLine line[2];
    // two read heads per channel for click-free time changes
    float headA[2] {}, headB[2] {}, xfade[2] {};
    bool fading[2] {};
    float tapeTime[2] {};
    dsp::SVF hp[2], lp[2];
    struct Allpass { dsp::DelayLine d; float g = 0.0f; int len = 100; float process (float x) { const float z = d.tapInt (len); const float y = -g * x + z; d.push (x + g * y); return y; } };
    Allpass diff[2][2];
    dsp::Follower duckFollower;
    dsp::Smooth mixSmooth, fbSmooth, outSmooth, widthSmooth;
    double lfoPhase = 0.0, revPhase[2] {};
    float jitterNow[2] {}, jitterTarget[2] {};
    int jitterCount = 0;
    dsp::Rng rng;
    float lastOut[2] {};
    float readHeads (int c, float target, bool tape);
};
}

#pragma once
#include "fx/core/RacoonProcessor.h"

namespace racoon::chinchilla
{
// Chinchilla: algorithmic reverb (feedback delay network) and convolution reverb with original IRs or your own.
class Processor : public RacoonProcessor
{
public:
    Processor();
    ~Processor() override;
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    static const juce::StringArray& irNames();
    void loadUserIR (const juce::File&);          // message thread
    juce::File getUserIR() const;
    std::atomic<bool> clearRequest { false };     // "vaciar cola" from the editor
    std::atomic<float> wetLevel { 0.0f };

    // builds one of the original impulse responses (also used by the tests)
    static juce::AudioBuffer<float> makeIR (int kind, double sampleRate, float rt60, float sizePct, float damping, bool eco);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static constexpr int maxLines = 16;
    dsp::DelayLine lines[maxLines];
    float baseLen[maxLines] {};
    float lowState[maxLines] {};
    dsp::DelayLine preDelay[2], early[2];
    struct Allpass { dsp::DelayLine d; float g = 0.5f; int len = 100; float process (float x) { const float z = d.tapInt (len); const float y = -g * x + z; d.push (x + g * y); return y; } };
    Allpass inDiff[2][4];
    dsp::SVF hp[2], lp[2];
    dsp::Follower duck;
    dsp::Smooth sizeSmooth, mixSmooth;
    double modPhase = 0.0;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 1024 } };
    juce::AudioBuffer<float> convBuf;
    juce::File userIR;
    juce::CriticalSection irLock;

    class IRBuilder;
    std::unique_ptr<IRBuilder> builder;
    friend class IRBuilder;
};
}

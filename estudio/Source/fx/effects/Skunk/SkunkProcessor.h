#pragma once
#include "fx/core/RacoonProcessor.h"
#include "fx/core/RacoonDsp.h"

namespace racoon::skunk
{
// Skunk: saturation and distortion with oversampling, bias, pre / post tone and Mid / Side.
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
    static float shape (int mode, float x);                  // the transfer curves (also drawn by the display)
    static float shapeBiased (int mode, float x, float bias); // with bias and its static offset removed
    static double shapeIntegral (int mode, double x);         // antiderivative of shape (for ADAA)

    // scope for the display
    static constexpr int scopeSize = 512;
    std::array<std::atomic<float>, scopeSize> scopeIn {}, scopeOut {};
    std::atomic<int> scopePos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::unique_ptr<juce::dsp::Oversampling<float>> os[4];
    int activeOs = 0, pendingOs = 0, fade = 0, fadeLen = 256;
    bool fadingOut = false, switchNow = false;
    std::atomic<bool> renderModeChanged { false };
public:
    // a render starting (or ending) switches the oversampling at once: no fade, nothing is playing yet
    void setNonRealtime (bool b) noexcept override { if (b != isNonRealtime()) renderModeChanged = true; RacoonProcessor::setNonRealtime (b); }
private:
    int fadeInDelay = 0;
    float shapeAdaa (int mode, float x, float bias, int c);
    double adaaX[2] {}, adaaF[2] {};
    bool adaaValid[2] {};
    int adaaMode[2] { -1, -1 };
    dsp::SVF pre[2], post[2];
    dsp::DCBlock dc[2];
    dsp::Smooth drive, mix, inTrim, outTrim;
    juce::AudioBuffer<float> work;
    dsp::DelayLine dryLine[2];
    float autoGain = 1.0f;
    int scopeDecim = 0;
    int osIndexWanted() const;
};
}

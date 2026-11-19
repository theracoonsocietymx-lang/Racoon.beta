#pragma once
#include "fx/core/RacoonProcessor.h"
#include "fx/core/RacoonDsp.h"

namespace racoon::hedgehog
{
// Hedgehog: compressor for tracks and buses, with external sidechain, lookahead and six characters.
class Processor : public RacoonProcessor
{
public:
    Processor();
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    static const juce::StringArray& modeNames();

    // static gain curve (dB in -> dB of gain change), shared with the display and the tests
    static float gainComputer (float levelDb, float thr, float ratio, float knee, bool upward);

    // for the display
    std::atomic<float> grDb { 0.0f }, levelDb { -100.0f };
    static constexpr int historySize = 256;
    std::array<std::atomic<float>, historySize> grHistory {}, inHistory {};
    std::atomic<int> historyPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    dsp::DelayLine look[2];
    dsp::SVF scFilter[2];
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> fetSat[2];
    float rms[2] {}, gainDb[2] {}, prevOut[2] {};
    int holdCount[2] {};
    float slowEnv = 0.0f;
    int lookSamples = 0;
    int historyCounter = 0;
    float histGr = 0.0f, histIn = -100.0f;
    juce::AudioBuffer<float> scBuf;
};
}

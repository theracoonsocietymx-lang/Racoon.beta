#pragma once
#include "fx/core/RacoonProcessor.h"
#include "fx/core/RacoonDsp.h"

namespace racoon::mole
{
// Mole: creative multimode filter with LFO, envelope follower, 16-step modulator, key tracking and sidechain envelope.
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

    static const juce::StringArray& typeNames();
    static constexpr int numSteps = 16;

    // magnitude response of the current type at a cutoff, for the display (dB)
    float responseDb (int type, int slope, float cutoff, float reso, float freq) const;

    std::atomic<float> modCutoff { 1000.0f };   // cutoff after modulation, for the display
    std::atomic<int> currentStep { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    dsp::SVF svfA[2], svfB[2];
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> preSat[2], postSat[2];
    juce::dsp::LadderFilter<float> ladder;
    dsp::DelayLine comb[2];
    dsp::Follower follower;
    dsp::Smooth cutSmooth[2], resoSmooth, mixSmooth, outSmooth;
    double lfoPhase = 0.0, stepPhase = 0.0;
    float sh = 0.0f, shLast = 0.0f;
    dsp::Rng rng;
    float stepValue = 0.0f;
    int heldNote = -1;
    std::atomic<float>* stepParams[numSteps] {};
    juce::AudioBuffer<float> ladderBuf, ladderAux;
    float lfoValue (double phase, int shape);
};
}

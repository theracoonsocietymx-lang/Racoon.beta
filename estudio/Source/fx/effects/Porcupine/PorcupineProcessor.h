#pragma once
#include "fx/core/RacoonProcessor.h"
#include <complex>

namespace racoon::porcupine
{
constexpr int numBands = 8;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    inline float process (float x, int c)
    {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    std::complex<double> response (double w) const;   // w in radians per sample
};

struct BandSettings { bool on; int type; float freq, gain, q; int slope; };

// Porcupine: 8-band parametric EQ, minimum or linear phase, dynamic bands, analyzer pre / post and band solo.
class Processor : public RacoonProcessor
{
public:
    Processor();
    ~Processor() override;
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    static const juce::StringArray& typeNames();
    static juce::String bandId (int band, const char* what) { return "b" + juce::String (band + 1) + "_" + what; }
    BandSettings band (int b) const;

    // filter design shared by the audio path, the linear-phase FIR and the display
    static int designBand (const BandSettings&, double sr, Biquad* out);   // returns number of sections (0..4)
    double magnitudeAt (double freq, bool includeDynamic) const;           // linear magnitude of the whole EQ
    float bandMagnitudeDb (int band, double freq) const;

    // analyzer: the editor reads the most recent samples (mono, pre and post), never blocking the audio
    static constexpr int fifoSize = 1 << 15;
    std::vector<float> fifoPre, fifoPost;
    std::atomic<int> fifoWrite { 0 };
    std::atomic<float> dynGainDb[numBands] {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    Biquad stages[numBands][4];
    int numStages[numBands] {};
    BandSettings current[numBands] {};
    dsp::SVF detector[numBands];
    dsp::Follower followers[numBands];
    float dynDb[numBands] {};
    dsp::Smooth outGain;
    juce::dsp::Convolution linear { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> soloBuf;
    dsp::DelayLine latencyDry[2];
    bool linearMode = false;
    int counter = 0;

    class Designer;    // background thread that builds the linear-phase FIR
    std::unique_ptr<Designer> designer;
    friend class Designer;
    void updateCoefficients (bool snap);
    void followersSet (float atk, float rel);
    std::atomic<float>* ptr[numBands][9] {};
};
}

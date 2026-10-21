#pragma once
#include "RacoonProcessor.h"
#include "HedgehogDsp.h"

namespace racoon::hedgehog
{
juce::StringArray platformNames();
float platformLufs (int index);
float platformCeiling (int index);

// Hedgehog, Mastering Lab. Its first screen is the Partícula: input, a pre EQ, one AMOUNT knob that drives a
// three-band bus compressor with saturation, air and width, the adaptive limiter and the output, with targets
// on the meters. Under it, for fine work: dynamic EQ (12 bands, mid / side), resonance suppressor, match EQ, multiband compressor
// (6 bands), low end, transients, exciter, imager and a true-peak limiter; LUFS, true peak and LRA meters, tonal
// balance against a target, references with matched loudness, an assistant, quality control, versions A / B / C,
// batch export of files and a report.
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

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 122.0; }
    int demoBars() const override { return 4; }
    bool canPassToMaqueta() const override { return false; }   // it lives on the Master: there is no clip to give it
    bool dockShowsRoll() const override { return false; }      // a mastering tool: Play and the example song, no piano roll

    static juce::String eqId (int band, const char* what) { return "eq" + juce::String (band + 1) + "_" + what; }
    static juce::String mbId (int band, const char* what) { return "mb" + juce::String (band + 1) + "_" + what; }

    // ------------------------------------------------------------------ meters (audio thread writes, screen reads)
    std::atomic<float> momentary { -70.0f }, shortTerm { -70.0f }, integrated { -70.0f }, lra { 0.0f }, inIntegrated { -70.0f };
    std::atomic<float> truePeakMax { -100.0f }, truePeakNow { -100.0f }, samplePeakMax { -100.0f }, correlation { 1.0f }, correlationMin { 1.0f };
    std::atomic<float> limiterGr { 0.0f }, mbGr[numMbBands] {}, eqDyn[numEqBands] {}, dcOffsetDb { -120.0f };
    // the Partícula screen: peak (held until clicked) and RMS in and out, and the reduction of its three bands
    std::atomic<float> inPeakHold { -100.0f }, inRms { -100.0f }, outPeakHold { -100.0f }, outRms { -100.0f }, inPeakNow { -100.0f }, outPeakNow { -100.0f };
    std::atomic<float> particleGr[3] {};
    void resetPeaks() { peakReset.store (true); }
    std::atomic<int> clippedIn { 0 };
    std::atomic<double> secondsMeasured { 0.0 };
    std::atomic<float> specIn[numThirds] {}, specOut[numThirds] {}, specOutNow[numThirds] {}, resCut[64] {};
    std::atomic<float> matchDiff[numThirds] {};       // learned difference (dB), scaled by the amount
    std::atomic<int> matchVersion { 0 };
    void resetMeters() { resetRequest.store (true); }

    // ------------------------------------------------------------------ references (message thread)
    struct Reference
    {
        juce::AudioBuffer<float> audio;
        float lufs = -70.0f, truePeak = -100.0f;
        float spectrum[numThirds] {};
        juce::String name;
        double sampleRate = 48000.0;
    };
    bool loadReference (int slot, const juce::File&);
    juce::String referenceName (int slot) const;
    float referenceLufs (int slot) const;
    void referenceSpectrum (int slot, float* out) const;

    // ------------------------------------------------------------------ helpers (message thread)
    void learnMatch();                                     // match the input to the genre curve or a reference
    juce::StringArray runAssistant();                     // suggests and applies settings; returns what it did
    struct Check { bool ok; juce::String text; };
    std::vector<Check> qualityChecks() const;
    juce::File writeReport() const;
    void saveVersion (int slot);
    void recallVersion (int slot);
    bool hasVersion (int slot) const { return versions[slot].isValid(); }
    float versionLufs (int slot) const { return versionLoudness[slot]; }
    int activeVersion = -1;

    // batch export: processes files with the current settings (a background job)
    void startExport (const juce::Array<juce::File>&);
    std::atomic<float> exportProgress { -1.0f };          // -1 idle, 0..1 working
    juce::String lastExportMessage;
    juce::File exportFolder() const;
    // renders one file through a copy of this chain (also used by the tests)
    bool renderFile (const juce::File& in, const juce::File& out) const;

    int latencyTotal() const { return resonance.latency() + limiter.latency(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    DynEqBand eq[numEqBands];
    Resonance resonance;
    ThirdEq match;
    Multiband multiband;
    LowEnd lowEnd;
    Transient transient;
    Exciter exciter;
    Imager imager;
    Limiter limiter;
    Particle particle;
    std::atomic<bool> peakReset { false };
    double rmsIn = 0.0, rmsOut = 0.0, rmsCoef = 0.999;
    Loudness meterOut, meterIn;
    ThirdOctave analyzerIn, analyzerOut;
    TruePeak tpMeter[2];
    juce::AudioBuffer<float> mono;
    std::atomic<bool> resetRequest { false };
    int lastMatchVersion = -1;
    float lastMatchAmount = -1.0f;
    double dcSum[2] {};
    juce::int64 dcCount = 0;
    std::atomic<float>* eqP[numEqBands][9] {};
    std::atomic<float>* mbP[numMbBands][6] {};

    mutable juce::SpinLock refLock;
    std::unique_ptr<Reference> refs[2];
    juce::int64 refPos[2] {};

    juce::ValueTree versions[3];
    float versionLoudness[3] { -70.0f, -70.0f, -70.0f };

    class ExportJob;
    std::unique_ptr<juce::Thread> exportJob;
};
}

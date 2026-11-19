#pragma once
#include "Voice.h"
#include "Sequencer.h"
#include "FxChain.h"

namespace be
{
struct HostInfo
{
    bool playing = false;
    double bpm = 120.0;
    double beats = 0.0;
    bool valid = false;
};

struct GlobalSettings
{
    SeqParams seq;
    DistSettings dist[kNumDist];
    int distRouting = 0, distPosition = 0;
    FxSettings fx;
    DuckSettings duck;
    float masterVolume = 0.75f;
    int evoLen = 2, evoShape = 0;
    float evoAmount = 1.0f;
    float layerDelayMs[kNumLayers] {};
};

// data the UI reads (written by the audio thread)
struct EngineTelemetry
{
    ScopeFifo scope, scopePre, subTap, restTap;
    std::atomic<float> peakL { 0 }, peakR { 0 }, rmsL { 0 }, rmsR { 0 }, correlation { 1 }, lowCorrelation { 1 };
    std::atomic<float> subEnergy { 0 }, lowMidEnergy { 0 }, bodyEnergy { 0 }, totalEnergy { 0 }, subStability { 1 };
    std::atomic<float> leadNote { -1 }, duckGr { 0 };
    std::atomic<int> activeVoices { 0 }, seqStep { -1 };
    std::atomic<float> modDisplay[ModDst::Count] {};
    std::atomic<float> srcDisplay[ModSrc::Count] {};
    // MIDI note history ring
    struct NoteEvt { int note; int vel; bool on; double time; };
    NoteEvt history[64] {};
    std::atomic<int> historyPos { 0 };
};

class SynthEngine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void setQuality (int quality);   // 0 eco, 1 normal, 2 high
    int getQuality() const { return quality; }
    void allNotesOff();

    void process (const PatchSnapshot& p, const GlobalSettings& gs, const SeqData& seqData, const HostInfo& host,
                  juce::MidiBuffer& midi, float* outL, float* outR, int n,
                  float* const* layerOutL, float* const* layerOutR, const float* scL, const float* scR);

    EngineTelemetry telemetry;
    Sequencer sequencer;
    int latencySamples() const;

    // preview phrase (preset browser audition)
    void startPreview() { previewPos = 0; previewOn = true; }
    void addDuckTrigger (int sampleOffset) { ducker.trigger (sampleOffset); }

private:
    double sr = 48000.0;
    int maxBlock = 512;
    int quality = 1, osFactor = 2;
    Voice voices[kMaxVoices];

    std::unique_ptr<juce::dsp::Oversampling<float>> layerOs[3][kNumLayers];   // per quality
    std::vector<float> zeroL, zeroR;

    std::vector<float> busL[kNumLayers], busR[kNumLayers];
    std::vector<float> mixL, mixR, preL, preR;
    DelayLine layerDelay[kNumLayers][2];

    DistortionStage dist[kNumDist];
    std::vector<float> distParL, distParR;
    FxRack fx;
    Ducker ducker;

    Lfo globalLfo[kNumLfos];
    GlobalModState gstate;

    // MIDI state
    struct Held { int note; float vel; int ch; };
    Held held[32]; int numHeld = 0;
    float bendNorm = 0.0f, mpeBend[17] {}, mpePressure[17] {}, mpeSlide[17] {};
    float lastNoteHz = 110.0f;
    double internalBeats = 0.0;
    double timeSec = 0.0;
    uint32_t seedCounter = 1;
    float lastPlayedNote = -1.0f;

    std::vector<SeqEvent> seqEvents;
    bool previewOn = false; int previewPos = 0;

    // telemetry accumulators
    float corrAcc = 0, corrL = 0, corrR = 0, lowCorrAcc = 0, lowA = 0, lowB = 0;
    LR4 analysisSplit[2];

    void noteOn (const PatchSnapshot& p, int note, float vel, int ch, bool fromSeq, bool accent, bool slide, float seqMod);
    void noteOff (const PatchSnapshot& p, int note, int ch);
    int findVoiceToSteal (int maxVoices);
    Voice* leadVoice();
    void evalGlobalMod (const PatchSnapshot& p, float* out);
    void pushHistory (int note, int vel, bool on);
};
} // namespace be

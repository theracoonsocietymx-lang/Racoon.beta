#pragma once
#include "Snapshot.h"
#include "Pattern.h"

namespace be
{
struct EngineOutputs
{
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* outL[kNumTracks] {};      // optional direct outputs (nullptr when the bus is disabled)
    float* outR[kNumTracks] {};
    const float* scL = nullptr;      // sidechain input (bass for collision / ducking)
    const float* scR = nullptr;
};

class DrumEngine
{
public:
    DrumEngine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (const EngineSnap& s, const SeqData* seq, const HostInfo& host, const juce::MidiBuffer& midi, EngineOutputs& out, int n);
    int latencySamples() const { return master.latency(); }

    // message thread: does any voice still read this sample? (an old kit's sample is freed only when none does)
    bool sampleInUse (const SampleData* p) const
    {
        for (auto& tr : voices) for (auto& v : tr) if (v.sampleInUse() == p) return true;
        return false;
    }

    // pads / preview (message thread -> audio thread)
    void triggerPad (int track, float velocity);
    void releasePad (int track);

    Telemetry telemetry;
    LoudnessMeter loudness;
    AuxBus aux[kNumAux];

    // live recording output (read on the message thread)
    juce::AbstractFifo recordFifo { 512 };
    RecordEvent recordBuf[512];

    // bridge information produced this block
    std::atomic<int> liveKickCount { 0 };
    float liveKickVel = 0.0f;
    std::atomic<float> bassEnvExternal { 0.0f };    // set by the processor from the bridge
    double currentBeat() const { return beatPos; }

private:
    double sr = 48000.0;
    int maxBlock = 512;
    juce::int64 clock = 0;            // absolute engine sample counter

    // voices and strips
    DrumVoice voices[kNumTracks][kVoicesPerTrack];
    TrackStrip strips[kNumTracks];
    GroupBus groups[kNumGroups];
    MasterDynamics master;
    FxRack fxRack;
    Ducker ducker;
    Scatter scatter;
    BreakFilter breakFilter;

    // buffers
    juce::AudioBuffer<float> trackBuf, sendBuf, groupBuf, mainBuf, duckGain, outDelay;
    std::vector<float> scratchL, scratchR;
    int outDelayPos = 0;

    // modulation
    Lfo glfo[kNumGLfos];
    Lfo vlfo[kNumTracks][kNumVLfos];
    float offs[kNumTracks][T_COUNT] {};
    float goffs[ModDst::Count] {};
    float srcGlobal[ModSrc::Count] {};
    struct TrackSrc { float vel = 0, accent = 0, random = 0, pitch = 0; } tsrc[kNumTracks];
    float modWheel = 0, aftertouch = 0, pressure = 0;
    void computeModulation (const EngineSnap& s, double beats, double bpm, bool playing, int len);
    float routeValue (int src, int track) const;
    void effective (const EngineSnap& s, int t, float* out) const;     // latched values incl. modulation

    // strip lock overrides (track-level parameters locked by the last hit)
    float stripLock[kNumTracks][T_COUNT] {};
    bool stripLocked[kNumTracks][T_COUNT] {};

    // sequencer state
    struct Pending { juce::int64 time = 0; int track = 0; float vel = 0; float accent = 0; float pitch = 0; int gate = -1; bool fromSeq = false; Step step; bool hasStep = false; };
    static constexpr int kMaxPending = 512;
    Pending pending[kMaxPending];
    int numPending = 0;
    void addPending (const Pending& p);
    bool lastCond[kNumTracks] {};
    int rrCounter[kNumTracks] {};
    int activePattern = 0, queuedPattern = -1;
    double switchBeat = -1.0, patternActiveSince = 0.0, playStartBeat = 0.0;
    float patternRamp = 0.0f;
    double beatPos = 0.0, expectedBeat = -1.0, internalBeats = 0.0, freeBeats = 0.0;
    bool wasPlaying = false;
    uint32_t salt = 0x1234567u;
    float heldVel[kNumTracks] {};
    bool padDown[kNumTracks] {};
    std::atomic<float> padQueue[kNumTracks] {};      // >0 = pending pad hit velocity, <0 = release
    void schedule (const EngineSnap& s, const SeqData* seq, double b0, double b1, double bpm, int n);
    void trigger (const EngineSnap& s, int track, const Pending& p, int offset);
    float collisionK = 0, collisionB = 0, kickEnv = 0, bassEnv = 0;
    Svf kickLp, bassLp[2];
    float lastPeak[kNumTracks] {};
};
} // namespace be

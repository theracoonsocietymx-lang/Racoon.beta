#pragma once
#include "Common.h"
#include "Modulators.h"
#include "FxChain.h"
#include "Mixer.h"
#include "DrumVoice.h"
#include "../Params.h"

namespace be
{
struct HostInfo
{
    bool valid = false, playing = false;
    double bpm = 120.0, beats = 0.0;
};

struct RouteSnap { int src = 0, trk = 0, dst = 0, curve = 0; float amt = 0.0f; };

// everything the audio thread needs for one block, filled from parameter atomics
struct EngineSnap
{
    float base[kNumTracks][T_COUNT] {};      // real values
    float norm[kNumTracks][T_COUNT] {};      // normalised 0..1
    TrackAssets assets[kNumTracks];

    bool play = false, followHost = true, songMode = false, fill = false, brk = false, record = false;
    float bpm = 124, swing = 0, morph = 0, groove = 0, humanize = 0, velCurve = 0, accent = 0.5f, xyX = 0.5f, xyY = 0.5f, sceneFade = 0;
    int pattern = 0, fillEvery = 0, scatter = 0, morphTarget = 1, quality = 1, noteRepeat = 0;
    float keyHz = 55.0f;                     // the note the resonator and the ring modulator follow (the kit's key)

    LfoSettings glfo[kNumGLfos];
    RouteSnap routes[kNumRoutes];
    int numRoutes = 0;
    float macros[kNumMacros] {};

    AuxSettings aux[kNumAux];
    GroupSettings grp[kNumGroups];
    MasterSettings master;
    FxSettings fx;
    DuckSettings duck;
    int duckTrack = 0, duckTarget = 1;
    bool bridgeOn = true;
    int bridgeTrack = 0;

    const juce::RangedAudioParameter* trackRange[T_COUNT] {};
    const juce::RangedAudioParameter* globalParam[ModDst::Count] {};
    float globalNorm[ModDst::Count] {};
};

struct RecordEvent { int track = 0, step = 0, vel = 100, pattern = 0; };

struct Telemetry
{
    std::atomic<float> trackPeak[kNumTracks] {};
    std::atomic<int> trackHits[kNumTracks] {};
    std::atomic<int> trackStep[kNumTracks] {};
    std::atomic<float> trackGr[kNumTracks] {};
    std::atomic<float> groupGr[kNumGroups] {};
    std::atomic<float> auxLevel[kNumAux] {};
    std::atomic<float> masterPeakL { 0 }, masterPeakR { 0 }, glueGr { 0 }, limGr { 0 }, truePeak { 0 }, duckGr { 0 };
    std::atomic<float> mod[kNumTracks][T_COUNT] {};
    std::atomic<float> gmod[ModDst::Count] {};
    std::atomic<float> src[ModSrc::Count] {};
    std::atomic<int> selectedTrack { 0 };
    std::atomic<bool> playing { false }, fillActive { false };
    std::atomic<int> activePattern { 0 }, queuedPattern { -1 }, songIndex { -1 };
    std::atomic<double> beat { 0.0 }, patternSince { 0.0 };
    std::atomic<float> collision { 0 }, kickLow { 0 }, bassLow { 0 };
    std::atomic<float> cpu { 0 };
    ScopeFifo scope;        // master mono
    ScopeFifo spectrumL, spectrumR;
    ScopeFifo kickLowFifo, bassLowFifo;    // low band envelopes for the collision view
};
} // namespace be

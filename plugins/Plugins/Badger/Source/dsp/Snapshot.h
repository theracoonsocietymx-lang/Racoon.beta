#pragma once
#include "Common.h"
#include "Oscillator.h"
#include "Modulators.h"
#include "Filters.h"
#include "../Params.h"

namespace be
{
// ---------------------------------------------------------------- routes
struct Route
{
    int src = 0, via = 0, dst = 0, curve = 0, op = 0, quant = 0;
    float amount = 0.0f, smooth = 0.0f;
    int index = 0;   // original slot (for per-voice smoothing state)
};

struct RouteList
{
    Route r[kNumRoutes];
    int count = 0;
};

// ---------------------------------------------------------------- per-block patch snapshot
struct OscStatic
{
    OscSettings s;
    int oct = 0, semi = 0;
    float fine = 0.0f;
    int layer = 1;
    // base normalised values of modulatable params (index = ModDst::OscSub)
    float baseNorm[ModDst::kOscBlock] {};
};

struct SubStatic
{
    bool on = true, align = true, mono = true, lock = true;
    int wave = 0, octave = 1, env = 0;
    float phase = 0.0f, limitHz = 160.0f;
};

struct NoiseStatic
{
    bool on = false;
    int type = 1, layer = 3;
    float pan = 0.0f, attack = 0.0f;
};

struct LayerStatic
{
    float pan = 0.0f, width = 1.0f, tune = 0.0f, lowCut = 10.0f, highCut = 22000.0f, delayMs = 0.0f;
    int driveType = 0, keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    bool mute = false, solo = false, bypassFilters = false, invert = false;
};

struct FilterStatic
{
    bool on = false;
    int type = 0, envSel = 1;
    float keyTrack = 0.5f, envAmt = 0.0f, velSens = 0.0f, feedback = 0.0f;
    bool comp = true;
};

struct VoiceStatic
{
    int mode = 1, voices = 8, priority = 0, glideMode = 1, glideCurve = 0;
    int bendUp = 2, bendDown = 2, mpeRange = 48, transpose = 0, keyLo = 0, keyHi = 127, unison = 2;
    bool mpe = false, retrig = false;
    float glide = 0.04f, velCurve = 0.0f, humanize = 0.0f, uniDetune = 0.15f;
    double a4 = 440.0;
    int ampEnv = 0;
    int quality = 1;
};

struct AcidStatic
{
    float cutoff = 500.0f, res = 0.7f, accent = 0.6f, slide = 0.06f, drive = 0.3f, calib = 0.2f, overdrive = 0.0f, nonlin = 0.5f;
    int accentThresh = 100;
};

struct ImpStatic { float amount = 0.25f, drift = 0.3f, filter = 0.3f, attack = 0.2f, sat = 0.2f; };

struct PatchSnapshot
{
    OscStatic osc[kNumOsc];
    SubStatic sub;
    NoiseStatic noise;
    LayerStatic layer[kNumLayers];
    int layerMode = 0;
    FilterStatic filter[kNumFilters];
    int filterRouting = 0;
    EnvSettings env[kNumEnvs];
    LfoSettings lfo[kNumLfos];
    RouteList routes;
    VoiceStatic voice;
    AcidStatic acid;
    ImpStatic imp;
    float macros[kNumMacros] {};
    float xyX = 0.0f, xyY = 0.0f;
    // modulation destination plumbing
    float destBase[ModDst::Count] {};                          // base normalised value
    const juce::RangedAudioParameter* destParam[ModDst::Count] {};
    bool envUsed[kNumEnvs] {}, lfoUsed[kNumLfos] {};
    // tuning: cents offset per pitch class relative to root (12 steps) or full .scl table
    double tuningCents[128] {};
    bool tuningActive = false;
};

// value of a modulated destination in parameter units
inline float destValue (const PatchSnapshot& p, int dst, float mod)
{
    const auto* prm = p.destParam[dst];
    if (prm == nullptr) return 0.0f;
    return prm->convertFrom0to1 (clamp01 (p.destBase[dst] + mod));
}
} // namespace be

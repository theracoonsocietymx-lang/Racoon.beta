#pragma once
#include <JuceHeader.h>

namespace be
{
// ------------------------------------------------------------------ sizes
constexpr int kNumTracks   = 16;
constexpr int kMaxSteps    = 128;
constexpr int kNumPatterns = 8;
constexpr int kNumGLfos    = 8;
constexpr int kNumVLfos    = 2;
constexpr int kNumRoutes   = 64;
constexpr int kNumMacros   = 16;
constexpr int kNumAux      = 4;
constexpr int kNumGroups   = 4;
constexpr int kNumScenes   = 8;
constexpr int kNumFx       = 16;
constexpr int kNumDist     = 2;
constexpr int kMaxLocks    = 8;
constexpr int kNumSlots    = 4;     // user sample slots per track
constexpr int kVoicesPerTrack = 4;
constexpr int kNumShapes   = kNumGLfos;   // drawable LFO shapes

// ------------------------------------------------------------------ enums shared with the Bass Engine DSP blocks
enum class FilterType { LP12, LP18, LP24, LP36, HP12, HP24, BP, Notch, Ladder, Cascade, Diode, SVF, Comb, Formant, Acid, Morph, Count };
enum class LfoWave { Sine, Triangle, Square, SawUp, SawDown, SampleHold, SmoothRandom, Drawn };
enum class DistType { SoftClip, HardClip, Tube, Tape, Transistor, Diode, Wavefolder, Bitcrusher, Downsample, Fuzz, Asymmetric, Multiband, Count };
enum class DuckSource { Off, Tempo, MidiNote, Sidechain, Track };
enum class FxId { EQ, Compressor, Multiband, Limiter, Clipper, Chorus, Phaser, Flanger, Delay, Reverb, FreqShift, RingMod, Transient, Imager, AutoFilter, Resonator };

// ------------------------------------------------------------------ drum roles and models
enum class Role { Kick, Snare, Clap, ClosedHat, OpenHat, Ride, Crash, Rim, TomLow, TomMid, TomHigh, Shaker, Conga, Bongo, Cowbell, Metal, Custom, Count };
enum class Model { TR808, TR909, TR606, CR78, Basalt, Count };
enum class Circuit { TR808, TR909, PCM, FM };
enum class TexType { White, Pink, Metal, Crackle, Granular, Resonator };
enum class EnvMode { AD, AHD, ADSR };
enum class MultiMode { Single, Velocity, RoundRobin, Random };

// ------------------------------------------------------------------ track parameters (local index)
enum TP : int
{
    T_ROLE, T_NOTE, T_LEVEL, T_PAN, T_TUNE, T_FINE, T_DECAY, T_VELSENS, T_CHOKE, T_POLY, T_CIRCUIT_ON, T_CIRCUIT, T_MODEL,
    T_A_ON, T_A_LEVEL, T_A_P1, T_A_P2, T_A_P3, T_A_P4, T_A_P5, T_A_P6, T_A_P7, T_A_P8, T_A_DRIFT,
    T_B_ON, T_B_LEVEL, T_B_ALGO, T_B_R1, T_B_R2, T_B_R3, T_B_R4, T_B_R5, T_B_R6, T_B_L1, T_B_L2, T_B_L3, T_B_L4, T_B_L5, T_B_L6,
    T_B_D1, T_B_D2, T_B_D3, T_B_D4, T_B_D5, T_B_D6, T_B_FB, T_B_MORPH,
    T_C_ON, T_C_LEVEL, T_C_SAMPLE, T_C_START, T_C_END, T_C_PITCH, T_C_REVERSE, T_C_STRETCHMODE, T_C_STRETCH, T_C_FADEIN, T_C_FADEOUT,
    T_C_SLICE, T_C_SLICEMODE, T_C_RR, T_C_MULTI, T_C_LOOP,
    T_D_ON, T_D_LEVEL, T_D_TYPE, T_D_FREQ, T_D_Q, T_D_DECAY, T_D_TONE,
    T_ENV_MODE, T_ENV_ATTACK, T_ENV_HOLD, T_ENV_DECAY, T_ENV_SUSTAIN, T_ENV_RELEASE, T_ENV_CURVE,
    T_PENV_AMT, T_PENV_TIME, T_FENV_AMT, T_FENV_DECAY,
    T_F_TYPE, T_F_CUTOFF, T_F_RES, T_F_DRIVE, T_F_KEY, T_F_MIX,
    T_DRV_TYPE, T_DRV_AMT, T_CRUSH, T_EQ_LOW, T_EQ_HIGH, T_COMP, T_GATE, T_TRANSIENT,
    T_FADER, T_MUTE, T_SOLO, T_SEND1, T_SEND2, T_SEND3, T_SEND4, T_GROUP, T_OUTPUT, T_INVERT, T_PHASE,
    T_VL1_RATE, T_VL1_WAVE, T_VL2_RATE, T_VL2_WAVE,
    T_COUNT
};

struct TrackParamDef
{
    const char* id;
    const char* name;
    int kind;            // 0 float, 1 choice, 2 bool, 3 int
    float lo, hi, def, centre;
    const juce::StringArray* choices;
    bool lockable;       // may be parameter-locked per step
    bool continuous;     // follows modulation during a hit
    int fmt;             // display format (see Params.cpp)
};
const TrackParamDef& trackParam (int localIndex);
juce::String trackParamId (int track, int localIndex);
int trackOfParamId (const juce::String& id, int* localIndexOut);   // -1 if not a track param

// ------------------------------------------------------------------ choice lists
const juce::StringArray& roleNames();
const juce::StringArray& modelNames();
const juce::StringArray& circuitNames();
const juce::StringArray& roleParamNames (int role);         // 8 macro names for the analog layer
const juce::StringArray& fmAlgoNames();
const juce::StringArray& factorySampleNames();
const juce::StringArray& syncDivisionNames();
double syncDivisionBeats (int index);
const juce::StringArray& seqResNames();
double seqResBeats (int index);
const juce::StringArray& condNames();
const juce::StringArray& distTypeNames();
const juce::StringArray& fxNames();
const juce::StringArray& auxTypeNames();
const juce::StringArray& groupNames();
const juce::StringArray& outputNames();
const juce::StringArray& modSourceNames();
const juce::StringArray& modDestNames();               // track params first, then globals
const juce::StringArray& genreNames();
int defaultNoteForRole (int role);
float roleBaseHz (int role);

inline juce::String fxId (const char* fx, const char* n) { return juce::String ("fx_") + fx + "_" + n; }
inline juce::String glfoId (int i, const char* n) { return "gl" + juce::String (i + 1) + "_" + n; }
inline juce::String routeId (int i, const char* n) { return "m" + juce::String (i + 1) + "_" + n; }
inline juce::String macroId (int i) { return "mac" + juce::String (i + 1); }
inline juce::String auxId (int i, const char* n) { return "aux" + juce::String (i + 1) + "_" + n; }
inline juce::String groupId (int i, const char* n) { return "grp" + juce::String (i + 1) + "_" + n; }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// ------------------------------------------------------------------ modulation
namespace ModSrc
{
    enum : int
    {
        None = 0,
        GLfo1 = 1,                         // 8
        VLfo1 = GLfo1 + kNumGLfos,         // 2
        Macro1 = VLfo1 + kNumVLfos,        // 16
        Velocity = Macro1 + kNumMacros,
        Accent, Random, StepPitch, Aftertouch, Pressure, ModWheel, Fill, BarRamp, PatternRamp, XYX, XYY, SceneFade, Constant,
        Count
    };
    bool isBipolar (int src);
}

namespace ModDst
{
    // 0 = none, 1..T_COUNT = track params (local index + 1), then globals
    constexpr int TrackBase = 1;
    enum : int
    {
        GlobalStart = TrackBase + T_COUNT,
        Aux1Return = GlobalStart, Aux2Return, Aux3Return, Aux4Return,
        GLfoRate1, GLfoDepth1 = GLfoRate1 + kNumGLfos,
        MasterVolume = GLfoDepth1 + kNumGLfos, GlueMix, Swing, Groove, SceneFade, ReverbMix, DelayMix, AutoFilterCutoff,
        Count
    };
    juce::String globalParamId (int dst);
}
} // namespace be

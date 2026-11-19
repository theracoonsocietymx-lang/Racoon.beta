#pragma once
#include <JuceHeader.h>

namespace be
{
// ---------------------------------------------------------------- sizes
constexpr int kNumOsc      = 3;
constexpr int kNumLayers   = 4;   // SUB, BODY, BITE, TEXTURE
constexpr int kNumFilters  = 3;
constexpr int kNumEnvs     = 8;
constexpr int kNumLfos     = 16;
constexpr int kNumRoutes   = 64;
constexpr int kNumMacros   = 16;
constexpr int kNumDist     = 2;
constexpr int kMaxVoices   = 16;
constexpr int kMaxUnison   = 8;
constexpr int kSeqMaxSteps = 64;
constexpr int kSeqPatterns = 8;
constexpr int kNumFx       = 16;
constexpr int kNumShapes   = kNumEnvs + kNumLfos + 1; // drawable shapes (+1 VA custom wave)

// ---------------------------------------------------------------- enums
enum class Engine { VA, Wavetable, FM, PhaseDist, Additive, Spectral, Granular, Sample, Physical, AcidCircuit, Count };
enum class VAWave { Sine, Triangle, Saw, Square, Pulse, Custom };
enum class Layer { Sub, Body, Bite, Texture };
enum class SyncMode { Off, Hard, Soft };
enum class XModType { Off, FM, AM, Ring };
enum class XModSrc { Osc1, Osc2, Osc3, Sub, Noise };
enum class SpectralMode { Stretch, Shift, OddEven, Comb, Tilt, Smear, Formant, HarmLock };
enum class SubWave { Sine, Triangle, Square, SoftTri, SoftSquare, RoundPulse };
enum class NoiseType { White, Pink, Brown, Crackle, Digital };
enum class DriveType { Soft, Tube, Tape, Diode, Fold, Asym };
enum class FilterType { LP12, LP18, LP24, LP36, HP12, HP24, BP, Notch, Ladder, Cascade, Diode, SVF, Comb, Formant, Acid, Morph, Count };
enum class FilterRouting { Serial, Parallel, PerLayer, Split };
enum class LfoWave { Sine, Triangle, Square, SawUp, SawDown, SampleHold, SmoothRandom, Drawn };
enum class LfoTrigger { Free, Note, Legato };
enum class ModCurve { Linear, Exp, Log, SCurve };
enum class ModOp { Add, Multiply, Invert };
enum class PlayMode { Mono, Legato, Poly, Unison };
enum class Priority { Last, High, Low };
enum class Quality { Eco, Normal, High };
enum class SeqMode { Off, Sequencer, Arpeggiator };
enum class ArpMode { Up, Down, UpDown, Random, AsPlayed };
enum class DistType { SoftClip, HardClip, Tube, Tape, Transistor, Diode, Wavefolder, Bitcrusher, Downsample, Fuzz, Asymmetric, Multiband, Count };
enum class DuckSource { Off, Tempo, MidiNote, Sidechain, DrumBridge };
enum class FxId { EQ, Compressor, Multiband, Limiter, Clipper, Chorus, Phaser, Flanger, Delay, Reverb, FreqShift, RingMod, Transient, Imager, AutoFilter, Resonator };

// ---------------------------------------------------------------- choice lists
const juce::StringArray& engineNames();
const juce::StringArray& vaWaveNames();
const juce::StringArray& layerNames();
const juce::StringArray& filterTypeNames();
const juce::StringArray& syncDivisionNames();
double syncDivisionBeats (int index);        // length in quarter notes
const juce::StringArray& seqRateNames();
double seqRateBeats (int index);
const juce::StringArray& scaleNames();
const juce::Array<int>& scaleIntervals (int scaleIndex);
const juce::StringArray& noteNames();
const juce::StringArray& distTypeNames();
const juce::StringArray& fxNames();
const juce::StringArray& modSourceNames();
const juce::StringArray& modDestNames();
const juce::StringArray& tuningNames();
const juce::StringArray& presetCategoryNames();
const juce::StringArray& wavetableNames();      // factory wavetables (index == table id)
const juce::StringArray& factorySampleNames();  // factory one-shots for Sample/Granular

// ---------------------------------------------------------------- id helpers
inline juce::String oscId (int i, const char* n)   { return "o" + juce::String (i + 1) + "_" + n; }
inline juce::String layId (int i, const char* n)   { return "ly" + juce::String (i + 1) + "_" + n; }
inline juce::String filtId (int i, const char* n)  { return "f" + juce::String (i + 1) + "_" + n; }
inline juce::String envId (int i, const char* n)   { return "e" + juce::String (i + 1) + "_" + n; }
inline juce::String lfoId (int i, const char* n)   { return "l" + juce::String (i + 1) + "_" + n; }
inline juce::String routeId (int i, const char* n) { return "m" + juce::String (i + 1) + "_" + n; }
inline juce::String macroId (int i)                { return "mac" + juce::String (i + 1); }
inline juce::String distId (int i, const char* n)  { return "d" + juce::String (i + 1) + "_" + n; }
inline juce::String fxId (const char* fx, const char* n) { return juce::String ("fx_") + fx + "_" + n; }

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// ---------------------------------------------------------------- modulation sources / destinations
namespace ModSrc
{
    enum : int
    {
        None = 0,
        Env1 = 1,                       // .. Env8 = 8
        Lfo1 = Env1 + kNumEnvs,         // 9 .. 24
        Macro1 = Lfo1 + kNumLfos,       // 25 .. 40
        Velocity = Macro1 + kNumMacros, // 41
        Note, Aftertouch, PolyPressure, ModWheel, PitchBend, MpeSlide, Random,
        SeqMod, SeqAccent, Evolution, Gate, XYX, XYY, Expression, Constant,
        Count
    };
    bool isBipolar (int src);
}

namespace ModDst
{
    constexpr int kOscBlock = 10;
    enum OscSub : int { OPitch, OLevel, OPan, OPos, OParamA, OParamB, ODetune, OFold, OXMod, OShape };
    enum : int
    {
        None = 0,
        Osc1 = 1,                                      // 3 osc x 10
        SubLevel = Osc1 + kNumOsc * kOscBlock,         // 31
        SubDrive, SubCutoff,
        NoiseLevel, NoiseColor,
        LayerLevel1,                                   // x4
        LayerDrive1 = LayerLevel1 + kNumLayers,        // x4
        LayerXFade = LayerDrive1 + kNumLayers,
        Filter1,                                       // 3 x 5 (cutoff, res, drive, mix, morph)
        AmpVolume = Filter1 + kNumFilters * 5,
        AmpPan, VoicePitch,
        AcidMorph, AcidEnvMod, AcidDecay,
        LfoRate1,                                      // x16
        LfoDepth1 = LfoRate1 + kNumLfos,               // x16
        GlobalStart = LfoDepth1 + kNumLfos,
        Dist1Drive = GlobalStart, Dist1Mix, Dist2Drive, Dist2Mix,
        ChorusMix, PhaserMix, FlangerMix, DelayMix, DelayFeedback, ReverbMix,
        AutoFilterCutoff, ImagerWidth, DuckDepth, MasterVolume, FreqShift, ResonatorMix,
        Count
    };
    // parameter id that a destination modulates ("" for none)
    juce::String paramIdFor (int dst);
    inline bool isGlobal (int dst) { return dst >= GlobalStart; }
}

} // namespace be

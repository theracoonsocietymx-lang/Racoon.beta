#include "Params.h"

namespace be
{
using namespace juce;

// ============================================================ choice lists
const StringArray& engineNames()
{
    static StringArray a { "Virtual Analog", "Wavetable", "FM", "Phase Dist", "Additive", "Spectral",
                           "Granular", "Sample", "Physical", "Acid Circuit" };
    return a;
}
const StringArray& vaWaveNames()   { static StringArray a { "Sine", "Triangle", "Saw", "Square", "Pulse", "Custom" }; return a; }
const StringArray& layerNames()    { static StringArray a { "SUB", "BODY", "BITE", "TEXTURE" }; return a; }
const StringArray& filterTypeNames()
{
    static StringArray a { "LP 12", "LP 18", "LP 24", "LP 36", "HP 12", "HP 24", "Band Pass", "Notch",
                           "Ladder", "Cascade", "Diode Ladder", "State Variable", "Comb", "Formant", "Acid", "Morph" };
    return a;
}
const StringArray& syncDivisionNames()
{
    static StringArray a { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/2D", "1/4", "1/4T", "1/4D",
                           "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/16D", "1/32", "1/32T", "1/64" };
    return a;
}
double syncDivisionBeats (int i)
{
    static const double v[] { 32, 16, 8, 4, 2, 4.0 / 3.0, 3, 1, 2.0 / 3.0, 1.5,
                              0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125, 1.0 / 12.0, 0.0625 };
    return v[jlimit (0, 18, i)];
}
const StringArray& seqRateNames()
{
    static StringArray a { "1/4", "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/16D", "1/32", "1/32T", "1/64" };
    return a;
}
double seqRateBeats (int i)
{
    static const double v[] { 1, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125, 1.0 / 12.0, 0.0625 };
    return v[jlimit (0, 9, i)];
}
const StringArray& scaleNames()
{
    static StringArray a { "Chromatic", "Minor", "Major", "Dorian", "Phrygian", "Mixolydian", "Harmonic Minor",
                           "Minor Pent", "Major Pent", "Blues", "Lydian", "Locrian" };
    return a;
}
const Array<int>& scaleIntervals (int s)
{
    static const Array<int> sc[] {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 11 },
        { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 1, 3, 5, 6, 8, 10 } };
    return sc[jlimit (0, 11, s)];
}
const StringArray& noteNames() { static StringArray a { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return a; }
const StringArray& distTypeNames()
{
    static StringArray a { "Soft Clip", "Hard Clip", "Tube", "Tape", "Transistor", "Diode", "Wavefolder",
                           "Bitcrusher", "Downsample", "Fuzz", "Asymmetric", "Multiband" };
    return a;
}
const StringArray& fxNames()
{
    static StringArray a { "EQ", "Compressor", "Multiband", "Limiter", "Clipper", "Chorus", "Phaser", "Flanger",
                           "Delay", "Reverb", "Freq Shift", "Ring Mod", "Transient", "Imager", "Auto Filter", "Resonator" };
    return a;
}
const StringArray& tuningNames()
{
    static StringArray a { "Equal 12", "Just", "Pythagorean", "Meantone", "Werckmeister III", "Equal 19", "Equal 24", "Custom .scl" };
    return a;
}
const StringArray& presetCategoryNames()
{
    static StringArray a { "Analog Bass", "Acid Bass", "Rolling Bass", "Indie Dance", "Dark Disco", "Techno Rumble",
                           "Sub Bass", "FM Bass", "Wavetable Bass", "Pluck Bass", "Reese Bass", "Funk Bass" };
    return a;
}
const StringArray& wavetableNames()
{
    static StringArray a { "Basic Morph", "PWM Sweep", "Saw Stack", "Harmonic Sweep", "Odd Sweep", "Formant AEIOU",
                           "Growl", "Reese Core", "FM Ladder", "FM Bell Bass", "Sync Sweep", "Fold Sweep",
                           "Organ Draw", "Digital Steps", "Vocal Bass", "Acid Shapes", "Ladder Steps", "Hollow Tube",
                           "Bitwave", "Sine Harmonics", "Square Harmonics", "Triangle Fold", "Phase Bend", "Comb Sweep",
                           "Noise Tone", "Pulse Train", "Rubber", "Metal Bass", "Dark Body", "Glass Sub",
                           "Overtone Choir", "Dirty Saw" };
    return a;
}
const StringArray& factorySampleNames()
{
    static StringArray a { "Upright Pluck", "808 Boom", "Analog Hit", "Picked Bass", "Slap Pop", "Vinyl Texture",
                           "Breath Air", "Metal Strike" };
    return a;
}

const StringArray& modSourceNames()
{
    static StringArray a = []
    {
        StringArray s { "None" };
        for (int i = 0; i < kNumEnvs; ++i)   s.add ("Env " + String (i + 1));
        for (int i = 0; i < kNumLfos; ++i)   s.add ("LFO " + String (i + 1));
        for (int i = 0; i < kNumMacros; ++i) s.add ("Macro " + String (i + 1));
        s.addArray (StringArray { "Velocity", "Note", "Aftertouch", "Pressure (MPE)", "Mod Wheel", "Pitch Bend", "Slide (MPE)",
                                  "Random", "Seq Mod", "Seq Accent", "Evolution", "Gate", "XY X", "XY Y", "Expression", "Constant" });
        return s;
    }();
    return a;
}

bool ModSrc::isBipolar (int src)
{
    if (src >= Lfo1 && src < Lfo1 + kNumLfos) return true;   // polarity handled by LFO itself
    return src == Note || src == PitchBend || src == Random;
}

const StringArray& modDestNames()
{
    static StringArray a = []
    {
        StringArray s { "None" };
        const char* oscSub[] { "Pitch", "Level", "Pan", "Position", "Param A", "Param B", "Detune", "Fold", "X-Mod", "Shape" };
        for (int o = 0; o < kNumOsc; ++o)
            for (auto* n : oscSub) s.add ("Osc " + String (o + 1) + " " + n);
        s.addArray (StringArray { "Sub Level", "Sub Drive", "Sub Cutoff", "Noise Level", "Noise Color" });
        for (int l = 0; l < kNumLayers; ++l) s.add (layerNames()[l] + " Level");
        for (int l = 0; l < kNumLayers; ++l) s.add (layerNames()[l] + " Drive");
        s.add ("Layer XFade");
        const char* fSub[] { "Cutoff", "Resonance", "Drive", "Mix", "Morph" };
        for (int f = 0; f < kNumFilters; ++f)
            for (auto* n : fSub) s.add ("Filter " + String (f + 1) + " " + n);
        s.addArray (StringArray { "Amp Volume", "Amp Pan", "Voice Pitch", "Acid Morph", "Acid Env Mod", "Acid Decay" });
        for (int l = 0; l < kNumLfos; ++l) s.add ("LFO " + String (l + 1) + " Rate");
        for (int l = 0; l < kNumLfos; ++l) s.add ("LFO " + String (l + 1) + " Depth");
        s.addArray (StringArray { "Dist 1 Drive", "Dist 1 Mix", "Dist 2 Drive", "Dist 2 Mix", "Chorus Mix", "Phaser Mix",
                                  "Flanger Mix", "Delay Mix", "Delay Feedback", "Reverb Mix", "AutoFilter Cutoff",
                                  "Imager Width", "Duck Depth", "Master Volume", "Freq Shift", "Resonator Mix" });
        jassert (s.size() == ModDst::Count);
        return s;
    }();
    return a;
}

String ModDst::paramIdFor (int d)
{
    if (d <= None || d >= Count) return {};
    if (d < SubLevel)
    {
        const int o = (d - Osc1) / kOscBlock, k = (d - Osc1) % kOscBlock;
        const char* ids[] { "coarse", "level", "pan", "pos", "pa", "pb", "detune", "fold", "xmodamt", "shape" };
        return oscId (o, ids[k]);
    }
    switch (d)
    {
        case SubLevel: return "sub_level";
        case SubDrive: return "sub_drive";
        case SubCutoff: return "sub_cutoff";
        case NoiseLevel: return "noise_level";
        case NoiseColor: return "noise_color";
        case LayerXFade: return "layer_xfade";
        case AmpVolume: return "amp_volume";
        case AmpPan: return "amp_pan";
        case VoicePitch: return {};
        case AcidMorph: return "acid_morph";
        case AcidEnvMod: return "acid_envmod";
        case AcidDecay: return "acid_decay";
        case Dist1Drive: return distId (0, "drive");
        case Dist1Mix: return distId (0, "mix");
        case Dist2Drive: return distId (1, "drive");
        case Dist2Mix: return distId (1, "mix");
        case ChorusMix: return fxId ("chorus", "mix");
        case PhaserMix: return fxId ("phaser", "mix");
        case FlangerMix: return fxId ("flanger", "mix");
        case DelayMix: return fxId ("delay", "mix");
        case DelayFeedback: return fxId ("delay", "feedback");
        case ReverbMix: return fxId ("reverb", "mix");
        case AutoFilterCutoff: return fxId ("autofilter", "cutoff");
        case ImagerWidth: return fxId ("imager", "width");
        case DuckDepth: return "duck_depth";
        case MasterVolume: return "master_volume";
        case FreqShift: return fxId ("freqshift", "shift");
        case ResonatorMix: return fxId ("resonator", "mix");
        default: break;
    }
    if (d >= LayerLevel1 && d < LayerLevel1 + kNumLayers) return layId (d - LayerLevel1, "level");
    if (d >= LayerDrive1 && d < LayerDrive1 + kNumLayers) return layId (d - LayerDrive1, "drive");
    if (d >= Filter1 && d < Filter1 + kNumFilters * 5)
    {
        const char* ids[] { "cutoff", "res", "drive", "mix", "morph" };
        return filtId ((d - Filter1) / 5, ids[(d - Filter1) % 5]);
    }
    if (d >= LfoRate1 && d < LfoRate1 + kNumLfos) return lfoId (d - LfoRate1, "rate");
    if (d >= LfoDepth1 && d < LfoDepth1 + kNumLfos) return lfoId (d - LfoDepth1, "depth");
    return {};
}

// ============================================================ formatting
namespace
{
String fmtHz (float v, int)  { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (v, v < 10 ? 2 : 0) + " Hz"; }
String fmtSec (float v, int) { return v < 1.0f ? String (v * 1000.0f, v < 0.01f ? 2 : 0) + " ms" : String (v, 2) + " s"; }
String fmtMs (float v, int)  { return String (v, 1) + " ms"; }
String fmtPct (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }
String fmtBip (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v * 100.0f)) + " %"; }
String fmtDb (float v, int)  { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; }
String fmtSemi (float v, int){ return (v > 0 ? "+" : "") + String (v, 2) + " st"; }
String fmtCent (float v, int){ return (v > 0 ? "+" : "") + String (roundToInt (v)) + " ct"; }
String fmtDeg (float v, int) { return String (roundToInt (v * 360.0f)) + " deg"; }
String fmtRatio (float v, int) { return String (v, 2) + ":1"; }
String fmtX (float v, int)   { return String (v, 2); }

struct Builder
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    static NormalisableRange<float> range (float lo, float hi, float centre = 0.0f, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        return r;
    }

    void f (const String& id, const String& name, NormalisableRange<float> r, float def,
            String (*fmt) (float, int) = fmtX, bool automatable = true)
    {
        auto attr = AudioParameterFloatAttributes().withStringFromValueFunction (fmt).withAutomatable (automatable);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, attr));
    }
    void c (const String& id, const String& name, const StringArray& choices, int def, bool automatable = true)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def,
                                                            AudioParameterChoiceAttributes().withAutomatable (automatable)));
    }
    void b (const String& id, const String& name, bool def, bool automatable = true)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def,
                                                          AudioParameterBoolAttributes().withAutomatable (automatable)));
    }
    void i (const String& id, const String& name, int lo, int hi, int def, bool automatable = true)
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def,
                                                         AudioParameterIntAttributes().withAutomatable (automatable)));
    }
};
} // namespace

// ============================================================ layout
AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Builder B;
    auto lin = [] (float lo, float hi) { return Builder::range (lo, hi); };
    auto skew = [] (float lo, float hi, float c) { return Builder::range (lo, hi, c); };
    const auto hz = skew (20.0f, 20000.0f, 800.0f);

    // ---------------------------------------------------------------- oscillators
    for (int o = 0; o < kNumOsc; ++o)
    {
        const String p = "Osc " + String (o + 1) + " ";
        B.b (oscId (o, "on"), p + "On", o == 0);
        B.c (oscId (o, "engine"), p + "Engine", engineNames(), 0);
        B.c (oscId (o, "wave"), p + "Wave", vaWaveNames(), 2);
        B.c (oscId (o, "table"), p + "Table", wavetableNames(), o);
        B.c (oscId (o, "sample"), p + "Sample", factorySampleNames(), 0);
        B.c (oscId (o, "spmode"), p + "Spectral Mode", StringArray { "Stretch", "Shift", "Odd/Even", "Comb", "Tilt", "Smear", "Formant", "Harm Lock" }, 0);
        B.i (oscId (o, "oct"), p + "Octave", -4, 4, 0);
        B.i (oscId (o, "semi"), p + "Semitone", -24, 24, 0);
        B.f (oscId (o, "fine"), p + "Fine", lin (-100.0f, 100.0f), 0.0f, fmtCent);
        B.f (oscId (o, "coarse"), p + "Coarse", lin (-24.0f, 24.0f), 0.0f, fmtSemi);
        B.f (oscId (o, "phase"), p + "Phase", lin (0.0f, 1.0f), 0.0f, fmtDeg);
        B.b (oscId (o, "reset"), p + "Phase Reset", true);
        B.f (oscId (o, "randphase"), p + "Random Phase", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "level"), p + "Level", lin (0.0f, 1.0f), 0.75f, fmtPct);
        B.f (oscId (o, "pan"), p + "Pan", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.i (oscId (o, "unison"), p + "Unison", 1, kMaxUnison, 1);
        B.f (oscId (o, "detune"), p + "Detune", skew (0.0f, 1.0f, 0.25f), 0.2f, fmtPct);
        B.f (oscId (o, "spread"), p + "Stereo Spread", lin (0.0f, 1.0f), 0.6f, fmtPct);
        B.f (oscId (o, "blend"), p + "Unison Blend", lin (0.0f, 1.0f), 0.7f, fmtPct);
        B.f (oscId (o, "drift"), p + "Drift", lin (0.0f, 1.0f), 0.1f, fmtPct);
        B.f (oscId (o, "slop"), p + "Slop", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.c (oscId (o, "syncmode"), p + "Sync Mode", StringArray { "Off", "Hard", "Soft" }, 0);
        B.f (oscId (o, "sync"), p + "Sync", skew (1.0f, 16.0f, 3.0f), 1.0f, fmtRatio);
        B.c (oscId (o, "xmodtype"), p + "X-Mod Type", StringArray { "Off", "FM", "AM", "Ring" }, 0);
        B.c (oscId (o, "xmodsrc"), p + "X-Mod Source", StringArray { "Osc 1", "Osc 2", "Osc 3", "Sub", "Noise" }, (o + 2) % 3);
        B.f (oscId (o, "xmodamt"), p + "X-Mod Amount", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "fold"), p + "Wavefold", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "shape"), p + "Waveshape", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "pos"), p + "Position", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "pa"), p + "Param A", lin (0.0f, 1.0f), 0.5f, fmtPct);
        B.f (oscId (o, "pb"), p + "Param B", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "pc"), p + "Param C", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (oscId (o, "keytrack"), p + "Key Track", lin (0.0f, 1.0f), 1.0f, fmtPct);
        B.c (oscId (o, "layer"), p + "Layer", layerNames(), o == 0 ? 1 : (o == 1 ? 2 : 3));
    }

    // ---------------------------------------------------------------- sub
    B.b ("sub_on", "Sub On", true);
    B.c ("sub_wave", "Sub Wave", StringArray { "Sine", "Triangle", "Square", "Soft Triangle", "Soft Square", "Round Pulse" }, 0);
    B.c ("sub_oct", "Sub Octave", StringArray { "0", "-1", "-2" }, 0);
    B.f ("sub_phase", "Sub Phase", lin (0.0f, 1.0f), 0.0f, fmtDeg);
    B.b ("sub_align", "Sub Phase Align", true);
    B.f ("sub_level", "Sub Level", lin (0.0f, 1.0f), 0.7f, fmtPct);
    B.f ("sub_drive", "Sub Drive", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.f ("sub_cutoff", "Sub Cutoff", skew (30.0f, 1200.0f, 180.0f), 300.0f, fmtHz);
    B.c ("sub_env", "Sub Envelope", StringArray { "Amp", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6", "Env 7", "Env 8" }, 0);
    B.b ("sub_mono", "Sub Mono", true);
    B.b ("sub_lock", "Sub Lock", true);
    B.f ("sub_limit", "Sub Ceiling", skew (40.0f, 400.0f, 120.0f), 160.0f, fmtHz);

    // ---------------------------------------------------------------- noise
    B.b ("noise_on", "Noise On", false);
    B.c ("noise_type", "Noise Type", StringArray { "White", "Pink", "Brown", "Crackle", "Digital" }, 1);
    B.f ("noise_level", "Noise Level", lin (0.0f, 1.0f), 0.2f, fmtPct);
    B.f ("noise_color", "Noise Color", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.f ("noise_pan", "Noise Pan", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.c ("noise_layer", "Noise Layer", layerNames(), 3);
    B.f ("noise_attack", "Noise Transient", lin (0.0f, 1.0f), 0.0f, fmtPct);

    // ---------------------------------------------------------------- layers
    const StringArray driveTypes { "Soft", "Tube", "Tape", "Diode", "Fold", "Asym" };
    for (int l = 0; l < kNumLayers; ++l)
    {
        const String p = layerNames()[l] + " ";
        B.f (layId (l, "level"), p + "Level", lin (0.0f, 1.0f), 0.8f, fmtPct);
        B.f (layId (l, "pan"), p + "Pan", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.f (layId (l, "width"), p + "Width", lin (0.0f, 2.0f), l == 0 ? 0.0f : 1.0f, fmtPct);
        B.f (layId (l, "tune"), p + "Tune", lin (-24.0f, 24.0f), 0.0f, fmtSemi);
        B.f (layId (l, "drive"), p + "Drive", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.c (layId (l, "drivetype"), p + "Drive Type", driveTypes, l == 2 ? 1 : 0);
        B.f (layId (l, "lowcut"), p + "Low Cut", skew (10.0f, 4000.0f, 200.0f), 10.0f, fmtHz);
        B.f (layId (l, "highcut"), p + "High Cut", skew (40.0f, 22000.0f, 2000.0f), 22000.0f, fmtHz);
        B.i (layId (l, "keylo"), p + "Key Low", 0, 127, 0);
        B.i (layId (l, "keyhi"), p + "Key High", 0, 127, 127);
        B.i (layId (l, "vello"), p + "Vel Low", 0, 127, 0);
        B.i (layId (l, "velhi"), p + "Vel High", 0, 127, 127);
        B.b (layId (l, "mute"), p + "Mute", false);
        B.b (layId (l, "solo"), p + "Solo", false);
        B.c (layId (l, "path"), p + "Filter Path", StringArray { "Filters", "Bypass" }, l == 0 ? 1 : 0);
        B.b (layId (l, "invert"), p + "Phase Invert", false);
        B.f (layId (l, "delay"), p + "Align Delay", lin (0.0f, 5.0f), 0.0f, fmtMs);
    }
    B.f ("layer_xfade", "Layer XFade", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.c ("layer_mode", "Layer Mix Mode", StringArray { "Parallel", "Serial" }, 0);

    // ---------------------------------------------------------------- filters
    for (int f = 0; f < kNumFilters; ++f)
    {
        const String p = "Filter " + String (f + 1) + " ";
        B.b (filtId (f, "on"), p + "On", f == 0);
        B.c (filtId (f, "type"), p + "Type", filterTypeNames(), f == 0 ? 8 : (f == 1 ? 4 : 13));
        B.f (filtId (f, "cutoff"), p + "Cutoff", hz, f == 1 ? 30.0f : 900.0f, fmtHz);
        B.f (filtId (f, "res"), p + "Resonance", lin (0.0f, 1.0f), 0.2f, fmtPct);
        B.f (filtId (f, "drive"), p + "Drive", lin (0.0f, 1.0f), 0.1f, fmtPct);
        B.f (filtId (f, "keytrack"), p + "Key Track", lin (-1.0f, 2.0f), 0.5f, fmtBip);
        B.f (filtId (f, "envamt"), p + "Env Amount", lin (-1.0f, 1.0f), f == 0 ? 0.35f : 0.0f, fmtBip);
        B.c (filtId (f, "envsel"), p + "Env Source", StringArray { "Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6", "Env 7", "Env 8" }, 1);
        B.f (filtId (f, "velsens"), p + "Velocity", lin (0.0f, 1.0f), 0.2f, fmtPct);
        B.f (filtId (f, "mix"), p + "Mix", lin (0.0f, 1.0f), 1.0f, fmtPct);
        B.f (filtId (f, "morph"), p + "Slope / Morph", lin (0.0f, 1.0f), 1.0f, fmtPct);
        B.f (filtId (f, "feedback"), p + "Feedback", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.b (filtId (f, "comp"), p + "Volume Comp", true);
    }
    B.c ("filter_routing", "Filter Routing", StringArray { "Serial", "Parallel", "Per Layer", "Split" }, 0);

    // ---------------------------------------------------------------- envelopes
    const StringArray onOffLoop { "Off", "Loop" };
    for (int e = 0; e < kNumEnvs; ++e)
    {
        const String p = "Env " + String (e + 1) + " ";
        const auto t = skew (0.0f, 20.0f, 0.8f);
        B.f (envId (e, "delay"), p + "Delay", t, 0.0f, fmtSec);
        B.f (envId (e, "attack"), p + "Attack", t, e == 0 ? 0.002f : 0.001f, fmtSec);
        B.f (envId (e, "hold"), p + "Hold", t, 0.0f, fmtSec);
        B.f (envId (e, "decay"), p + "Decay", t, e == 0 ? 0.6f : 0.35f, fmtSec);
        B.f (envId (e, "sustain"), p + "Sustain", lin (0.0f, 1.0f), e == 0 ? 0.8f : 0.0f, fmtPct);
        B.f (envId (e, "release"), p + "Release", t, e == 0 ? 0.08f : 0.15f, fmtSec);
        B.f (envId (e, "acurve"), p + "Attack Curve", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.f (envId (e, "dcurve"), p + "Decay Curve", lin (-1.0f, 1.0f), 0.5f, fmtBip);
        B.f (envId (e, "rcurve"), p + "Release Curve", lin (-1.0f, 1.0f), 0.5f, fmtBip);
        B.c (envId (e, "mode"), p + "Mode", StringArray { "ADSR", "Drawn" }, 0);
        B.c (envId (e, "loop"), p + "Loop", onOffLoop, 0);
        B.b (envId (e, "retrig"), p + "Legato Retrigger", false);
        B.b (envId (e, "sync"), p + "Tempo Sync", false);
        B.f (envId (e, "velsens"), p + "Velocity", lin (0.0f, 1.0f), e == 0 ? 0.3f : 0.0f, fmtPct);
        B.f (envId (e, "keyscale"), p + "Key Scaling", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.f (envId (e, "drawlen"), p + "Drawn Length", skew (0.01f, 16.0f, 1.0f), 1.0f, fmtSec);
    }
    B.c ("amp_env", "Amp Envelope", StringArray { "Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6", "Env 7", "Env 8" }, 0);
    B.f ("amp_volume", "Amp Volume", lin (0.0f, 1.0f), 0.8f, fmtPct);
    B.f ("amp_pan", "Amp Pan", lin (-1.0f, 1.0f), 0.0f, fmtBip);

    // ---------------------------------------------------------------- LFOs
    for (int l = 0; l < kNumLfos; ++l)
    {
        const String p = "LFO " + String (l + 1) + " ";
        B.c (lfoId (l, "wave"), p + "Wave", StringArray { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold", "Smooth Random", "Drawn" }, 0);
        B.f (lfoId (l, "rate"), p + "Rate", skew (0.01f, 60.0f, 2.0f), 1.0f, fmtHz);
        B.b (lfoId (l, "sync"), p + "Sync", l < 4);
        B.c (lfoId (l, "div"), p + "Division", syncDivisionNames(), 10);
        B.c (lfoId (l, "trig"), p + "Trigger", StringArray { "Free", "Note", "Legato" }, 1);
        B.b (lfoId (l, "oneshot"), p + "One Shot", false);
        B.f (lfoId (l, "phase"), p + "Phase", lin (0.0f, 1.0f), 0.0f, fmtDeg);
        B.f (lfoId (l, "offset"), p + "Offset", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.f (lfoId (l, "fadein"), p + "Fade In", skew (0.0f, 10.0f, 1.0f), 0.0f, fmtSec);
        B.f (lfoId (l, "fadeout"), p + "Fade Out", skew (0.0f, 10.0f, 1.0f), 0.0f, fmtSec);
        B.c (lfoId (l, "scope"), p + "Scope", StringArray { "Voice", "Global" }, 0);
        B.c (lfoId (l, "polarity"), p + "Polarity", StringArray { "Bipolar", "Unipolar" }, 0);
        B.f (lfoId (l, "smooth"), p + "Smooth", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (lfoId (l, "depth"), p + "Depth", lin (0.0f, 1.0f), 1.0f, fmtPct);
    }

    // ---------------------------------------------------------------- mod matrix
    for (int r = 0; r < kNumRoutes; ++r)
    {
        const String p = "Mod " + String (r + 1) + " ";
        B.b (routeId (r, "on"), p + "Active", true, false);
        B.c (routeId (r, "src"), p + "Source", modSourceNames(), 0, false);
        B.c (routeId (r, "via"), p + "Via", modSourceNames(), 0, false);
        B.c (routeId (r, "dst"), p + "Destination", modDestNames(), 0, false);
        B.f (routeId (r, "amt"), p + "Amount", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.c (routeId (r, "curve"), p + "Curve", StringArray { "Linear", "Exponential", "Logarithmic", "S-Curve" }, 0, false);
        B.c (routeId (r, "op"), p + "Operation", StringArray { "Add", "Multiply", "Invert" }, 0, false);
        B.i (routeId (r, "quant"), p + "Quantize", 0, 24, 0, false);
        B.f (routeId (r, "smooth"), p + "Smooth", lin (0.0f, 1.0f), 0.0f, fmtPct, false);
    }

    // ---------------------------------------------------------------- macros
    for (int m = 0; m < kNumMacros; ++m)
        B.f (macroId (m), "Macro " + String (m + 1), lin (0.0f, 1.0f), 0.0f, fmtPct);

    // ---------------------------------------------------------------- voice / MIDI
    B.c ("v_mode", "Play Mode", StringArray { "Mono", "Legato", "Poly", "Unison" }, 1);
    B.i ("v_voices", "Polyphony", 1, kMaxVoices, 8);
    B.c ("v_priority", "Note Priority", StringArray { "Last", "High", "Low" }, 0);
    B.f ("v_glide", "Glide Time", skew (0.0f, 2.0f, 0.15f), 0.04f, fmtSec);
    B.c ("v_glidemode", "Glide Mode", StringArray { "Always", "Legato" }, 1);
    B.c ("v_glidecurve", "Glide Curve", StringArray { "Constant Time", "Constant Rate" }, 0);
    B.i ("v_bendup", "Bend Up", 0, 48, 2);
    B.i ("v_benddown", "Bend Down", 0, 48, 2);
    B.b ("v_mpe", "MPE", false);
    B.i ("v_mperange", "MPE Bend Range", 1, 96, 48);
    B.f ("v_velcurve", "Velocity Curve", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.i ("v_transpose", "Transpose", -36, 36, 0);
    B.f ("v_tune", "Master Tune", lin (415.0f, 466.0f), 440.0f, [] (float v, int) { return String (v, 1) + " Hz"; });
    B.c ("v_tuning", "Tuning", tuningNames(), 0);
    B.c ("v_tuningroot", "Tuning Root", noteNames(), 0);
    B.i ("v_keylo", "Key Range Low", 0, 127, 0);
    B.i ("v_keyhi", "Key Range High", 0, 127, 127);
    B.f ("v_humanize", "Humanize", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.i ("v_unison", "Unison Voices", 1, 8, 2);
    B.f ("v_unidetune", "Unison Detune", lin (0.0f, 1.0f), 0.15f, fmtPct);
    B.b ("v_retrig", "Retrigger", false);
    B.c ("v_quality", "Quality", StringArray { "Eco", "Normal", "High" }, 1, false);
    B.f ("master_volume", "Master Volume", lin (0.0f, 1.0f), 0.75f, fmtPct);

    // ---------------------------------------------------------------- acid
    B.f ("acid_morph", "Acid Morph", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.f ("acid_cutoff", "Acid Cutoff", skew (40.0f, 8000.0f, 600.0f), 500.0f, fmtHz);
    B.f ("acid_res", "Acid Resonance", lin (0.0f, 1.0f), 0.7f, fmtPct);
    B.f ("acid_envmod", "Acid Env Mod", lin (0.0f, 1.0f), 0.6f, fmtPct);
    B.f ("acid_decay", "Acid Decay", skew (0.03f, 3.0f, 0.4f), 0.35f, fmtSec);
    B.f ("acid_accent", "Acid Accent", lin (0.0f, 1.0f), 0.6f, fmtPct);
    B.i ("acid_accthresh", "Accent Velocity", 1, 127, 100);
    B.f ("acid_slide", "Slide Time", skew (0.01f, 0.5f, 0.07f), 0.06f, fmtSec);
    B.f ("acid_drive", "Circuit Saturation", lin (0.0f, 1.0f), 0.3f, fmtPct);
    B.f ("acid_calib", "Calibration", lin (0.0f, 1.0f), 0.2f, fmtPct);
    B.f ("acid_overdrive", "Overdrive", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.f ("acid_nonlin", "Nonlinear Response", lin (0.0f, 1.0f), 0.5f, fmtPct);

    // ---------------------------------------------------------------- sequencer
    B.c ("seq_mode", "Seq Mode", StringArray { "Off", "Sequencer", "Arpeggiator" }, 0);
    B.i ("seq_pattern", "Seq Pattern", 1, kSeqPatterns, 1);
    B.i ("seq_length", "Seq Length", 1, kSeqMaxSteps, 16);
    B.c ("seq_rate", "Seq Rate", seqRateNames(), 4);
    B.f ("seq_swing", "Seq Swing", lin (0.0f, 0.75f), 0.0f, fmtPct);
    B.f ("seq_gate", "Seq Gate Scale", lin (0.1f, 2.0f), 1.0f, fmtPct);
    B.b ("seq_transpose", "Seq Key Transpose", true);
    B.c ("seq_root", "Seq Root", noteNames(), 0);
    B.c ("seq_scale", "Seq Scale", scaleNames(), 1);
    B.b ("seq_quantize", "Seq Scale Lock", true);
    B.c ("seq_arpmode", "Arp Mode", StringArray { "Up", "Down", "Up/Down", "Random", "As Played" }, 0);
    B.i ("seq_arpoct", "Arp Octaves", 1, 4, 1);
    B.b ("seq_latch", "Arp Latch", false);
    B.b ("seq_hostsync", "Seq Host Sync", true);
    B.f ("seq_bpm", "Internal BPM", lin (40.0f, 220.0f), 124.0f, [] (float v, int) { return String (v, 1) + " BPM"; });
    B.c ("seq_trigger", "Seq Trigger", StringArray { "Host Play", "Key Held" }, 1);

    // ---------------------------------------------------------------- distortion
    for (int d = 0; d < kNumDist; ++d)
    {
        const String p = "Dist " + String (d + 1) + " ";
        B.b (distId (d, "on"), p + "On", d == 0);
        B.c (distId (d, "type"), p + "Type", distTypeNames(), d == 0 ? 2 : 3);
        B.f (distId (d, "drive"), p + "Drive", lin (0.0f, 1.0f), 0.25f, fmtPct);
        B.f (distId (d, "tone"), p + "Tone", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.f (distId (d, "mix"), p + "Mix", lin (0.0f, 1.0f), 1.0f, fmtPct);
        B.f (distId (d, "out"), p + "Output", lin (-24.0f, 12.0f), 0.0f, fmtDb);
        B.f (distId (d, "shape"), p + "Character", lin (0.0f, 1.0f), 0.5f, fmtPct);
        B.f (distId (d, "xlow"), p + "Low Crossover", skew (40.0f, 1000.0f, 150.0f), 120.0f, fmtHz);
        B.f (distId (d, "xhigh"), p + "High Crossover", skew (400.0f, 10000.0f, 2500.0f), 2500.0f, fmtHz);
        B.f (distId (d, "lowdrive"), p + "Low Band Drive", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (distId (d, "highdrive"), p + "High Band Drive", lin (0.0f, 1.0f), 0.6f, fmtPct);
    }
    B.c ("dist_routing", "Dist Routing", StringArray { "Serial", "Parallel" }, 0);
    B.c ("dist_position", "Dist Position", StringArray { "Pre FX", "Post FX" }, 0);

    // ---------------------------------------------------------------- FX rack
    auto fxOn = [&B] (const char* fx, const char* name, bool def) { B.b (fxId (fx, "on"), String (name) + " On", def); };
    auto fxMix = [&B, &lin] (const char* fx, const char* name, float def) { B.f (fxId (fx, "mix"), String (name) + " Mix", lin (0.0f, 1.0f), def, fmtPct); };
    auto fxLowCut = [&B, &skew] (const char* fx, const char* name) { B.f (fxId (fx, "lowcut"), String (name) + " Low Cut", skew (20.0f, 2000.0f, 200.0f), 150.0f, fmtHz); };

    fxOn ("eq", "EQ", false);
    B.f (fxId ("eq", "hpf"), "EQ Low Cut", skew (10.0f, 500.0f, 60.0f), 10.0f, fmtHz);
    B.f (fxId ("eq", "lsfreq"), "EQ Low Shelf Freq", skew (30.0f, 500.0f, 100.0f), 90.0f, fmtHz);
    B.f (fxId ("eq", "lsgain"), "EQ Low Shelf Gain", lin (-18.0f, 18.0f), 0.0f, fmtDb);
    B.f (fxId ("eq", "p1freq"), "EQ Peak 1 Freq", skew (40.0f, 8000.0f, 400.0f), 300.0f, fmtHz);
    B.f (fxId ("eq", "p1gain"), "EQ Peak 1 Gain", lin (-18.0f, 18.0f), 0.0f, fmtDb);
    B.f (fxId ("eq", "p1q"), "EQ Peak 1 Q", skew (0.2f, 10.0f, 1.0f), 1.0f, fmtX);
    B.f (fxId ("eq", "p2freq"), "EQ Peak 2 Freq", skew (100.0f, 16000.0f, 2000.0f), 1800.0f, fmtHz);
    B.f (fxId ("eq", "p2gain"), "EQ Peak 2 Gain", lin (-18.0f, 18.0f), 0.0f, fmtDb);
    B.f (fxId ("eq", "p2q"), "EQ Peak 2 Q", skew (0.2f, 10.0f, 1.0f), 1.0f, fmtX);
    B.f (fxId ("eq", "hsfreq"), "EQ High Shelf Freq", skew (1000.0f, 18000.0f, 5000.0f), 6000.0f, fmtHz);
    B.f (fxId ("eq", "hsgain"), "EQ High Shelf Gain", lin (-18.0f, 18.0f), 0.0f, fmtDb);

    fxOn ("comp", "Compressor", false);
    B.f (fxId ("comp", "thresh"), "Comp Threshold", lin (-48.0f, 0.0f), -12.0f, fmtDb);
    B.f (fxId ("comp", "ratio"), "Comp Ratio", skew (1.0f, 20.0f, 4.0f), 4.0f, fmtRatio);
    B.f (fxId ("comp", "attack"), "Comp Attack", skew (0.1f, 100.0f, 10.0f), 5.0f, fmtMs);
    B.f (fxId ("comp", "release"), "Comp Release", skew (5.0f, 1000.0f, 100.0f), 80.0f, fmtMs);
    B.f (fxId ("comp", "makeup"), "Comp Makeup", lin (0.0f, 24.0f), 0.0f, fmtDb);
    fxMix ("comp", "Comp", 1.0f);

    fxOn ("mbcomp", "Multiband", false);
    B.f (fxId ("mbcomp", "xlow"), "MB Low Crossover", skew (40.0f, 600.0f, 150.0f), 120.0f, fmtHz);
    B.f (fxId ("mbcomp", "xhigh"), "MB High Crossover", skew (500.0f, 10000.0f, 2500.0f), 2500.0f, fmtHz);
    B.f (fxId ("mbcomp", "low"), "MB Low Threshold", lin (-48.0f, 0.0f), -12.0f, fmtDb);
    B.f (fxId ("mbcomp", "mid"), "MB Mid Threshold", lin (-48.0f, 0.0f), -18.0f, fmtDb);
    B.f (fxId ("mbcomp", "high"), "MB High Threshold", lin (-48.0f, 0.0f), -18.0f, fmtDb);
    B.f (fxId ("mbcomp", "ratio"), "MB Ratio", skew (1.0f, 20.0f, 4.0f), 3.0f, fmtRatio);
    fxMix ("mbcomp", "MB", 1.0f);

    fxOn ("limiter", "Limiter", true);
    B.f (fxId ("limiter", "ceiling"), "Limiter Ceiling", lin (-24.0f, 0.0f), -0.3f, fmtDb);
    B.f (fxId ("limiter", "release"), "Limiter Release", skew (1.0f, 500.0f, 50.0f), 50.0f, fmtMs);

    fxOn ("clipper", "Clipper", false);
    B.f (fxId ("clipper", "drive"), "Clipper Drive", lin (0.0f, 24.0f), 0.0f, fmtDb);
    B.f (fxId ("clipper", "ceiling"), "Clipper Ceiling", lin (-24.0f, 0.0f), -1.0f, fmtDb);
    B.f (fxId ("clipper", "knee"), "Clipper Softness", lin (0.0f, 1.0f), 0.3f, fmtPct);

    fxOn ("chorus", "Chorus", false);
    B.f (fxId ("chorus", "rate"), "Chorus Rate", skew (0.02f, 10.0f, 1.0f), 0.6f, fmtHz);
    B.f (fxId ("chorus", "depth"), "Chorus Depth", lin (0.0f, 1.0f), 0.35f, fmtPct);
    B.f (fxId ("chorus", "delay"), "Chorus Delay", lin (1.0f, 30.0f), 8.0f, fmtMs);
    B.f (fxId ("chorus", "feedback"), "Chorus Feedback", lin (-0.95f, 0.95f), 0.0f, fmtBip);
    fxMix ("chorus", "Chorus", 0.4f);
    fxLowCut ("chorus", "Chorus");

    fxOn ("phaser", "Phaser", false);
    B.f (fxId ("phaser", "rate"), "Phaser Rate", skew (0.02f, 10.0f, 1.0f), 0.3f, fmtHz);
    B.f (fxId ("phaser", "depth"), "Phaser Depth", lin (0.0f, 1.0f), 0.6f, fmtPct);
    B.f (fxId ("phaser", "centre"), "Phaser Centre", skew (100.0f, 8000.0f, 1000.0f), 900.0f, fmtHz);
    B.f (fxId ("phaser", "feedback"), "Phaser Feedback", lin (-0.95f, 0.95f), 0.4f, fmtBip);
    fxMix ("phaser", "Phaser", 0.5f);
    fxLowCut ("phaser", "Phaser");

    fxOn ("flanger", "Flanger", false);
    B.f (fxId ("flanger", "rate"), "Flanger Rate", skew (0.02f, 10.0f, 1.0f), 0.2f, fmtHz);
    B.f (fxId ("flanger", "depth"), "Flanger Depth", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.f (fxId ("flanger", "delay"), "Flanger Delay", lin (0.1f, 10.0f), 2.0f, fmtMs);
    B.f (fxId ("flanger", "feedback"), "Flanger Feedback", lin (-0.95f, 0.95f), 0.5f, fmtBip);
    fxMix ("flanger", "Flanger", 0.5f);
    fxLowCut ("flanger", "Flanger");

    fxOn ("delay", "Delay", false);
    B.f (fxId ("delay", "time"), "Delay Time", skew (1.0f, 2000.0f, 300.0f), 375.0f, fmtMs);
    B.b (fxId ("delay", "sync"), "Delay Sync", true);
    B.c (fxId ("delay", "div"), "Delay Division", syncDivisionNames(), 12);
    B.f (fxId ("delay", "feedback"), "Delay Feedback", lin (0.0f, 0.95f), 0.35f, fmtPct);
    B.b (fxId ("delay", "pingpong"), "Delay Ping Pong", true);
    B.f (fxId ("delay", "highcut"), "Delay High Cut", skew (500.0f, 20000.0f, 4000.0f), 5000.0f, fmtHz);
    fxMix ("delay", "Delay", 0.2f);
    fxLowCut ("delay", "Delay");

    fxOn ("reverb", "Reverb", false);
    B.f (fxId ("reverb", "size"), "Reverb Size", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.f (fxId ("reverb", "damp"), "Reverb Damping", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.f (fxId ("reverb", "width"), "Reverb Width", lin (0.0f, 1.0f), 1.0f, fmtPct);
    B.f (fxId ("reverb", "predelay"), "Reverb Pre-Delay", lin (0.0f, 200.0f), 10.0f, fmtMs);
    fxMix ("reverb", "Reverb", 0.15f);
    fxLowCut ("reverb", "Reverb");

    fxOn ("freqshift", "Freq Shift", false);
    B.f (fxId ("freqshift", "shift"), "Freq Shift Amount", lin (-500.0f, 500.0f), 5.0f, fmtHz);
    fxMix ("freqshift", "Freq Shift", 0.5f);

    fxOn ("ringmod", "Ring Mod", false);
    B.f (fxId ("ringmod", "freq"), "Ring Mod Freq", skew (1.0f, 5000.0f, 200.0f), 110.0f, fmtHz);
    B.c (fxId ("ringmod", "mode"), "Ring Mod Mode", StringArray { "Fixed", "Track" }, 1);
    B.f (fxId ("ringmod", "ratio"), "Ring Mod Ratio", skew (0.25f, 8.0f, 1.5f), 2.0f, fmtX);
    fxMix ("ringmod", "Ring Mod", 0.3f);

    fxOn ("transient", "Transient", false);
    B.f (fxId ("transient", "attack"), "Transient Attack", lin (-1.0f, 1.0f), 0.3f, fmtBip);
    B.f (fxId ("transient", "sustain"), "Transient Sustain", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.f (fxId ("transient", "speed"), "Transient Speed", lin (0.0f, 1.0f), 0.5f, fmtPct);

    fxOn ("imager", "Imager", true);
    B.f (fxId ("imager", "width"), "Imager Width", lin (0.0f, 2.0f), 1.0f, fmtPct);
    B.b (fxId ("imager", "bassmono"), "Bass Mono", true);
    B.f (fxId ("imager", "monofreq"), "Bass Mono Freq", skew (40.0f, 500.0f, 120.0f), 120.0f, fmtHz);

    fxOn ("autofilter", "Auto Filter", false);
    B.c (fxId ("autofilter", "type"), "Auto Filter Type", StringArray { "Low Pass", "Band Pass", "High Pass" }, 0);
    B.f (fxId ("autofilter", "cutoff"), "Auto Filter Cutoff", hz, 1500.0f, fmtHz);
    B.f (fxId ("autofilter", "res"), "Auto Filter Resonance", lin (0.0f, 1.0f), 0.3f, fmtPct);
    B.f (fxId ("autofilter", "rate"), "Auto Filter Rate", skew (0.02f, 20.0f, 1.0f), 0.5f, fmtHz);
    B.b (fxId ("autofilter", "sync"), "Auto Filter Sync", true);
    B.c (fxId ("autofilter", "div"), "Auto Filter Division", syncDivisionNames(), 7);
    B.f (fxId ("autofilter", "depth"), "Auto Filter Depth", lin (0.0f, 1.0f), 0.4f, fmtPct);
    fxMix ("autofilter", "Auto Filter", 1.0f);

    fxOn ("resonator", "Resonator", false);
    B.f (fxId ("resonator", "tune"), "Resonator Tune", lin (-24.0f, 24.0f), 12.0f, fmtSemi);
    B.f (fxId ("resonator", "decay"), "Resonator Decay", lin (0.0f, 1.0f), 0.6f, fmtPct);
    B.f (fxId ("resonator", "bright"), "Resonator Brightness", lin (0.0f, 1.0f), 0.5f, fmtPct);
    fxMix ("resonator", "Resonator", 0.3f);

    // ---------------------------------------------------------------- ducking / sidechain
    B.c ("duck_src", "Duck Source", StringArray { "Off", "Tempo", "MIDI Note", "Sidechain", "Hamster" }, 0);
    B.c ("duck_div", "Duck Rate", syncDivisionNames(), 7);
    B.f ("duck_depth", "Duck Depth", lin (0.0f, 1.0f), 0.7f, fmtPct);
    B.f ("duck_attack", "Duck Attack", skew (0.1f, 50.0f, 5.0f), 2.0f, fmtMs);
    B.f ("duck_hold", "Duck Hold", skew (0.0f, 300.0f, 40.0f), 20.0f, fmtMs);
    B.f ("duck_release", "Duck Recovery", skew (10.0f, 1000.0f, 150.0f), 160.0f, fmtMs);
    B.f ("duck_curve", "Duck Curve", lin (-1.0f, 1.0f), 0.3f, fmtBip);
    B.c ("duck_band", "Duck Band", StringArray { "Full", "Low Only" }, 0);
    B.f ("duck_freq", "Duck Band Freq", skew (40.0f, 600.0f, 150.0f), 180.0f, fmtHz);
    B.i ("duck_note", "Duck Trigger Note", 0, 127, 36);
    B.i ("duck_channel", "Duck Trigger Channel", 0, 16, 16);
    B.f ("duck_thresh", "Sidechain Threshold", lin (-60.0f, 0.0f), -24.0f, fmtDb);

    // ---------------------------------------------------------------- analog imperfection / evolution / morph
    B.f ("imp_amount", "Imperfection", lin (0.0f, 1.0f), 0.25f, fmtPct);
    B.f ("imp_drift", "Imp Pitch Drift", lin (0.0f, 1.0f), 0.3f, fmtPct);
    B.f ("imp_filter", "Imp Filter Variance", lin (0.0f, 1.0f), 0.3f, fmtPct);
    B.f ("imp_attack", "Imp Attack Variance", lin (0.0f, 1.0f), 0.2f, fmtPct);
    B.f ("imp_sat", "Imp Saturation Variance", lin (0.0f, 1.0f), 0.2f, fmtPct);

    B.c ("evo_len", "Evolution Length", StringArray { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 2);
    B.c ("evo_shape", "Evolution Shape", StringArray { "Rise", "Fall", "Rise-Fall", "Steps", "S-Curve" }, 0);
    B.f ("evo_amount", "Evolution Amount", lin (0.0f, 1.0f), 1.0f, fmtPct);

    B.f ("xy_x", "Morph X", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.f ("xy_y", "Morph Y", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.b ("xy_on", "Morph XY Active", false);

    return std::move (B.layout);
}

} // namespace be

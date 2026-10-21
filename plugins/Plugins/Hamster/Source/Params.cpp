#include "Params.h"

namespace be
{
using namespace juce;

// ============================================================ choice lists
const StringArray& roleNames()
{
    static StringArray a { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Ride", "Crash", "Rim", "Tom Low", "Tom Mid", "Tom High",
                           "Shaker", "Conga", "Bongo", "Cowbell", "Metal Perc", "Custom" };
    return a;
}
const StringArray& modelNames() { static StringArray a { "808", "909", "606", "CR-78", "Basalt" }; return a; }
const StringArray& circuitNames() { static StringArray a { "808", "909", "PCM", "FM" }; return a; }
const StringArray& roleParamNames (int role)
{
    static const StringArray kick { "TUNE", "DROP", "SWEEP", "CLICK", "PUNCH", "BODY", "SUB", "TAIL" };
    static const StringArray snare { "SHELL", "NOISE", "SNAP", "WIRES", "BODY", "RING", "PITCH ENV", "TRANSIENT" };
    static const StringArray clap { "DENSITY", "SPREAD", "DIFFUSE", "TONE", "NOISE", "WIDTH", "SNAP", "GRIT" };
    static const StringArray hat { "BRIGHT", "COLOR", "SHIMMER", "OPEN", "METAL", "NOISE", "BELL", "WIDTH" };
    static const StringArray rim { "PITCH", "CLICK", "RING", "BODY", "NOISE", "TONE", "DAMP", "SNAP" };
    static const StringArray tom { "PITCH", "BEND", "DAMPING", "MEMBRANE", "ATTACK", "BODY", "RESONANCE", "VARIATION" };
    static const StringArray shaker { "COLOR", "GRAIN", "SHAPE", "NOISE", "ATTACK", "WIDTH", "TONE", "SPREAD" };
    static const StringArray bell { "PITCH", "DETUNE", "TONE", "RING", "ATTACK", "BODY", "METAL", "NOISE" };
    static const StringArray metal { "PITCH", "INHARM", "RING", "BRIGHT", "STRIKE", "BODY", "MOD", "SPREAD" };
    static const StringArray custom { "PITCH", "DROP", "SWEEP", "TONE", "NOISE", "NOISE TONE", "CLICK", "DRIVE" };
    switch ((Role) jlimit (0, (int) Role::Count - 1, role))
    {
        case Role::Kick: return kick;
        case Role::Snare: return snare;
        case Role::Clap: return clap;
        case Role::ClosedHat: case Role::OpenHat: case Role::Ride: case Role::Crash: return hat;
        case Role::Rim: return rim;
        case Role::TomLow: case Role::TomMid: case Role::TomHigh: case Role::Conga: case Role::Bongo: return tom;
        case Role::Shaker: return shaker;
        case Role::Cowbell: return bell;
        case Role::Metal: return metal;
        default: return custom;
    }
}
const StringArray& fmAlgoNames()
{
    static StringArray a { "2>1", "3>2>1", "4>3>2>1", "2>1 + 4>3", "2+3+4>1", "6 Stack", "3 Pairs", "Additive 6" };
    return a;
}
const StringArray& factorySampleNames()
{
    static StringArray a { "PCM Kick 1", "PCM Kick 2", "PCM Kick 3", "PCM Snare 1", "PCM Snare 2", "PCM Clap", "PCM Rim", "PCM Closed Hat 1",
                           "PCM Closed Hat 2", "PCM Open Hat", "PCM Ride", "PCM Crash", "PCM Tom Low", "PCM Tom Mid", "PCM Tom High",
                           "PCM Conga", "PCM Bongo", "PCM Cowbell", "PCM Shaker", "PCM Tambourine", "PCM Clave", "PCM Metal Hit",
                           "PCM Zap", "PCM Glitch", "PCM Vinyl Hit", "PCM Bubble", "PCM Wood Block", "PCM Snap", "PCM Noise Burst",
                           "PCM Tape Kick", "PCM Lo-Fi Snare", "PCM Reverse Crash", "User Slots" };
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
    static const double v[] { 32, 16, 8, 4, 2, 4.0 / 3.0, 3, 1, 2.0 / 3.0, 1.5, 0.5, 1.0 / 3.0, 0.75, 0.25, 1.0 / 6.0, 0.375, 0.125, 1.0 / 12.0, 0.0625 };
    return v[jlimit (0, 18, i)];
}
const StringArray& seqResNames() { static StringArray a { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" }; return a; }
double seqResBeats (int i)
{
    static const double v[] { 1, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
    return v[jlimit (0, 7, i)];
}
const StringArray& condNames()
{
    static StringArray a { "Always", "Fill", "Not Fill", "Pre", "Not Pre", "Neighbour", "Not Neighbour", "First", "Not First", "Last", "Not Last", "A:B", "Not A:B" };
    return a;
}
const StringArray& distTypeNames()
{
    static StringArray a { "Soft Clip", "Hard Clip", "Tube", "Tape", "Transistor", "Diode", "Wavefolder", "Bitcrusher", "Downsample", "Fuzz", "Asymmetric", "Multiband" };
    return a;
}
const StringArray& fxNames()
{
    static StringArray a { "EQ", "Compressor", "Multiband", "Limiter", "Clipper", "Chorus", "Phaser", "Flanger", "Delay", "Reverb",
                           "Freq Shift", "Ring Mod", "Transient", "Imager", "Auto Filter", "Resonator" };
    return a;
}
const StringArray& auxTypeNames()
{
    static StringArray a { "Reverb", "Room (Conv)", "Plate (Conv)", "Hall (Conv)", "Delay", "Ping Pong", "Chorus", "Phaser", "Flanger",
                           "Freq Shift", "Resonator", "Bitcrush" };
    return a;
}
const StringArray& groupNames() { static StringArray a { "None", "Kicks", "Snares", "Hats", "Perc" }; return a; }
const StringArray& outputNames()
{
    static StringArray a = [] { StringArray s { "Main" }; for (int i = 1; i <= kNumTracks; ++i) s.add ("Out " + String (i)); return s; }();
    return a;
}
const StringArray& genreNames()
{
    static StringArray a { "House", "Tech House", "Techno", "Indie Dance", "Dark Disco", "Acid", "Electro", "EBM", "Minimal" };
    return a;
}
int defaultNoteForRole (int role)
{
    static const int n[] { 36, 38, 39, 42, 46, 51, 49, 37, 41, 45, 48, 70, 63, 60, 56, 75, 76 };
    return n[jlimit (0, (int) Role::Count - 1, role)];
}
float roleBaseHz (int role)
{
    static const float f[] { 50, 180, 1200, 400, 400, 450, 400, 450, 90, 130, 190, 2000, 240, 380, 540, 330, 120 };
    return f[jlimit (0, (int) Role::Count - 1, role)];
}

const StringArray& modSourceNames()
{
    static StringArray a = []
    {
        StringArray s { "None" };
        for (int i = 0; i < kNumGLfos; ++i) s.add ("LFO " + String (i + 1));
        for (int i = 0; i < kNumVLfos; ++i) s.add ("Voice LFO " + String (i + 1));
        for (int i = 0; i < kNumMacros; ++i) s.add ("Macro " + String (i + 1));
        s.addArray (StringArray { "Velocity", "Accent", "Random", "Step Pitch", "Aftertouch", "Pressure (MPE)", "Mod Wheel", "Fill",
                                  "Bar Ramp", "Pattern Ramp", "XY X", "XY Y", "Scene Fade", "Constant" });
        jassert (s.size() == ModSrc::Count);
        return s;
    }();
    return a;
}
bool ModSrc::isBipolar (int src)
{
    return (src >= GLfo1 && src < GLfo1 + kNumGLfos) || (src >= VLfo1 && src < VLfo1 + kNumVLfos) || src == Random || src == StepPitch;
}

const StringArray& modDestNames()
{
    static StringArray a = []
    {
        StringArray s { "None" };
        for (int i = 0; i < T_COUNT; ++i) s.add (String ("Track ") + trackParam (i).name);
        s.addArray (StringArray { "Aux 1 Return", "Aux 2 Return", "Aux 3 Return", "Aux 4 Return" });
        for (int i = 0; i < kNumGLfos; ++i) s.add ("LFO " + String (i + 1) + " Rate");
        for (int i = 0; i < kNumGLfos; ++i) s.add ("LFO " + String (i + 1) + " Depth");
        s.addArray (StringArray { "Master Volume", "Glue Mix", "Swing", "Groove Amount", "Scene Fade", "Reverb Mix", "Delay Mix", "Auto Filter Cutoff" });
        jassert (s.size() == ModDst::Count);
        return s;
    }();
    return a;
}

String ModDst::globalParamId (int d)
{
    if (d >= Aux1Return && d < Aux1Return + kNumAux) return auxId (d - Aux1Return, "return");
    if (d >= GLfoRate1 && d < GLfoRate1 + kNumGLfos) return glfoId (d - GLfoRate1, "rate");
    if (d >= GLfoDepth1 && d < GLfoDepth1 + kNumGLfos) return glfoId (d - GLfoDepth1, "depth");
    switch (d)
    {
        case MasterVolume: return "g_master";
        case GlueMix: return "glue_mix";
        case Swing: return "g_swing";
        case Groove: return "g_groove";
        case SceneFade: return "g_scenefade";
        case ReverbMix: return fxId ("reverb", "mix");
        case DelayMix: return fxId ("delay", "mix");
        case AutoFilterCutoff: return fxId ("autofilter", "cutoff");
        default: return {};
    }
}

// ============================================================ track parameter table
namespace
{
const StringArray chokeNames { "None", "A", "B", "C", "D" };
const StringArray polyNames { "Mono", "Poly 2", "Poly 4" };
const StringArray texNames { "White", "Pink", "Metal", "Crackle", "Granular", "Resonator" };
const StringArray stretchNames { "Repitch", "Stretch" };
const StringArray sliceNames { "Off", "Equal 16", "Transients" };
const StringArray multiNames { "Single", "Velocity", "Round Robin", "Random" };
const StringArray envModeNames { "AD", "AHD", "ADSR" };
const StringArray filtNames { "Off", "LP 12", "LP 24", "HP 12", "HP 24", "Band Pass", "Notch", "Ladder", "State Variable", "Comb", "Formant" };
const StringArray drvNames { "Soft", "Tube", "Tape", "Diode", "Fold", "Asym" };
const StringArray vlfoNames { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold", "Smooth Random" };
enum Fmt { F_X, F_PCT, F_BIP, F_HZ, F_SEC, F_MS, F_SEMI, F_CENT, F_DB, F_RATIO, F_NOTE, F_CIRC };

const TrackParamDef table[] {
    { "role", "Role", 1, 0, 0, 0, 0, &roleNames(), false, false, F_X },
    { "note", "MIDI Note", 3, 0, 127, 36, 0, nullptr, false, false, F_NOTE },
    { "level", "Level", 0, 0, 1, 0.8f, 0, nullptr, true, true, F_PCT },
    { "pan", "Pan", 0, -1, 1, 0, 0, nullptr, true, true, F_BIP },
    { "tune", "Tune", 0, -24, 24, 0, 0, nullptr, true, true, F_SEMI },
    { "fine", "Fine", 0, -100, 100, 0, 0, nullptr, true, true, F_CENT },
    { "decay", "Decay", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "velsens", "Velocity Sens", 0, 0, 1, 0.6f, 0, nullptr, true, false, F_PCT },
    { "choke", "Choke Group", 1, 0, 0, 0, 0, &chokeNames, false, false, F_X },
    { "poly", "Voice Mode", 1, 0, 0, 0, 0, &polyNames, false, false, F_X },
    { "circon", "Circuit Morph On", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "circuit", "Circuit Morph", 0, 0, 3, 0, 0, nullptr, true, false, F_CIRC },
    { "model", "Model", 1, 0, 0, 0, 0, &modelNames(), true, false, F_X },
    { "a_on", "Analog On", 2, 0, 1, 1, 0, nullptr, true, false, F_X },
    { "a_level", "Analog Level", 0, 0, 1, 0.8f, 0, nullptr, true, true, F_PCT },
    { "a_p1", "Analog P1", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p2", "Analog P2", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p3", "Analog P3", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p4", "Analog P4", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p5", "Analog P5", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p6", "Analog P6", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p7", "Analog P7", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_p8", "Analog P8", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "a_drift", "Analog Drift", 0, 0, 1, 0.15f, 0, nullptr, true, false, F_PCT },
    { "b_on", "FM On", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "b_level", "FM Level", 0, 0, 1, 0.7f, 0, nullptr, true, true, F_PCT },
    { "b_algo", "FM Algorithm", 1, 0, 0, 0, 0, &fmAlgoNames(), true, false, F_X },
    { "b_r1", "FM Ratio 1", 0, 0.25f, 16, 1, 2, nullptr, true, false, F_RATIO },
    { "b_r2", "FM Ratio 2", 0, 0.25f, 16, 2, 2, nullptr, true, false, F_RATIO },
    { "b_r3", "FM Ratio 3", 0, 0.25f, 16, 3, 2, nullptr, true, false, F_RATIO },
    { "b_r4", "FM Ratio 4", 0, 0.25f, 16, 1, 2, nullptr, true, false, F_RATIO },
    { "b_r5", "FM Ratio 5", 0, 0.25f, 16, 5.19f, 2, nullptr, true, false, F_RATIO },
    { "b_r6", "FM Ratio 6", 0, 0.25f, 16, 7.31f, 2, nullptr, true, false, F_RATIO },
    { "b_l1", "FM Level 1", 0, 0, 1, 1, 0, nullptr, true, false, F_PCT },
    { "b_l2", "FM Level 2", 0, 0, 1, 0.5f, 0, nullptr, true, false, F_PCT },
    { "b_l3", "FM Level 3", 0, 0, 1, 0.3f, 0, nullptr, true, false, F_PCT },
    { "b_l4", "FM Level 4", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "b_l5", "FM Level 5", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "b_l6", "FM Level 6", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "b_d1", "FM Decay 1", 0, 0.005f, 4, 0.4f, 0.3f, nullptr, true, false, F_SEC },
    { "b_d2", "FM Decay 2", 0, 0.005f, 4, 0.15f, 0.3f, nullptr, true, false, F_SEC },
    { "b_d3", "FM Decay 3", 0, 0.005f, 4, 0.08f, 0.3f, nullptr, true, false, F_SEC },
    { "b_d4", "FM Decay 4", 0, 0.005f, 4, 0.2f, 0.3f, nullptr, true, false, F_SEC },
    { "b_d5", "FM Decay 5", 0, 0.005f, 4, 0.2f, 0.3f, nullptr, true, false, F_SEC },
    { "b_d6", "FM Decay 6", 0, 0.005f, 4, 0.2f, 0.3f, nullptr, true, false, F_SEC },
    { "b_fb", "FM Feedback", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "b_morph", "FM Morph", 0, 0, 1, 0.5f, 0, nullptr, true, true, F_PCT },
    { "c_on", "Sample On", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "c_level", "Sample Level", 0, 0, 1, 0.8f, 0, nullptr, true, true, F_PCT },
    { "c_sample", "Sample", 1, 0, 0, 0, 0, &factorySampleNames(), true, false, F_X },
    { "c_start", "Sample Start", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "c_end", "Sample End", 0, 0, 1, 1, 0, nullptr, true, false, F_PCT },
    { "c_pitch", "Sample Pitch", 0, -24, 24, 0, 0, nullptr, true, true, F_SEMI },
    { "c_reverse", "Sample Reverse", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "c_stretchmode", "Pitch Mode", 1, 0, 0, 0, 0, &stretchNames, true, false, F_X },
    { "c_stretch", "Time Stretch", 0, 0.25f, 4, 1, 1, nullptr, true, false, F_RATIO },
    { "c_fadein", "Fade In", 0, 0, 0.2f, 0, 0.02f, nullptr, true, false, F_SEC },
    { "c_fadeout", "Fade Out", 0, 0, 1, 0.005f, 0.1f, nullptr, true, false, F_SEC },
    { "c_slice", "Slice", 3, 0, 15, 0, 0, nullptr, true, false, F_X },
    { "c_slicemode", "Slice Mode", 1, 0, 0, 0, 0, &sliceNames, true, false, F_X },
    { "c_rr", "Start Variation", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "c_multi", "Multisample Mode", 1, 0, 0, 0, 0, &multiNames, true, false, F_X },
    { "c_loop", "Sample Loop", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "d_on", "Texture On", 2, 0, 1, 0, 0, nullptr, true, false, F_X },
    { "d_level", "Texture Level", 0, 0, 1, 0.3f, 0, nullptr, true, true, F_PCT },
    { "d_type", "Texture Type", 1, 0, 0, 1, 0, &texNames, true, false, F_X },
    { "d_freq", "Texture Freq", 0, 40, 16000, 3000, 1500, nullptr, true, true, F_HZ },
    { "d_q", "Texture Resonance", 0, 0, 1, 0.3f, 0, nullptr, true, false, F_PCT },
    { "d_decay", "Texture Decay", 0, 0.005f, 2, 0.15f, 0.2f, nullptr, true, false, F_SEC },
    { "d_tone", "Texture Tone", 0, -1, 1, 0, 0, nullptr, true, false, F_BIP },
    { "env_mode", "Amp Env Mode", 1, 0, 0, 1, 0, &envModeNames, true, false, F_X },
    { "env_attack", "Amp Attack", 0, 0, 0.5f, 0, 0.02f, nullptr, true, false, F_SEC },
    { "env_hold", "Amp Hold", 0, 0, 1, 0, 0.1f, nullptr, true, false, F_SEC },
    { "env_decay", "Amp Decay", 0, 0.01f, 8, 2.5f, 0.5f, nullptr, true, false, F_SEC },
    { "env_sustain", "Amp Sustain", 0, 0, 1, 0.7f, 0, nullptr, true, false, F_PCT },
    { "env_release", "Amp Release", 0, 0.005f, 4, 0.1f, 0.3f, nullptr, true, false, F_SEC },
    { "env_curve", "Amp Curve", 0, -1, 1, 0.5f, 0, nullptr, true, false, F_BIP },
    { "penv_amt", "Pitch Env", 0, -48, 48, 0, 0, nullptr, true, false, F_SEMI },
    { "penv_time", "Pitch Env Time", 0, 0.002f, 1, 0.04f, 0.05f, nullptr, true, false, F_SEC },
    { "fenv_amt", "Filter Env", 0, -1, 1, 0, 0, nullptr, true, false, F_BIP },
    { "fenv_decay", "Filter Env Decay", 0, 0.005f, 2, 0.1f, 0.15f, nullptr, true, false, F_SEC },
    { "f_type", "Filter Type", 1, 0, 0, 0, 0, &filtNames, true, false, F_X },
    { "f_cutoff", "Filter Cutoff", 0, 20, 20000, 20000, 1000, nullptr, true, true, F_HZ },
    { "f_res", "Filter Resonance", 0, 0, 1, 0.1f, 0, nullptr, true, true, F_PCT },
    { "f_drive", "Filter Drive", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "f_key", "Filter Key Track", 0, -1, 2, 0, 0, nullptr, true, false, F_BIP },
    { "f_mix", "Filter Mix", 0, 0, 1, 1, 0, nullptr, true, true, F_PCT },
    { "drv_type", "Drive Type", 1, 0, 0, 0, 0, &drvNames, true, false, F_X },
    { "drv_amt", "Drive", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "crush", "Bitcrush", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "eq_low", "EQ Low", 0, -18, 18, 0, 0, nullptr, true, false, F_DB },
    { "eq_high", "EQ High", 0, -18, 18, 0, 0, nullptr, true, false, F_DB },
    { "comp", "Compression", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "gate", "Gate", 0, 0, 1, 0, 0, nullptr, true, false, F_PCT },
    { "transient", "Transient", 0, -1, 1, 0, 0, nullptr, true, false, F_BIP },
    { "fader", "Fader", 0, -60, 6, 0, -12, nullptr, true, true, F_DB },
    { "mute", "Mute", 2, 0, 1, 0, 0, nullptr, false, false, F_X },
    { "solo", "Solo", 2, 0, 1, 0, 0, nullptr, false, false, F_X },
    { "send1", "Send 1", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "send2", "Send 2", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "send3", "Send 3", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "send4", "Send 4", 0, 0, 1, 0, 0, nullptr, true, true, F_PCT },
    { "group", "Group", 1, 0, 0, 0, 0, &groupNames(), false, false, F_X },
    { "output", "Output", 1, 0, 0, 0, 0, &outputNames(), false, false, F_X },
    { "invert", "Polarity Invert", 2, 0, 1, 0, 0, nullptr, false, false, F_X },
    { "phase", "Phase Delay", 0, 0, 5, 0, 0, nullptr, false, false, F_MS },
    { "vl1_rate", "Voice LFO 1 Rate", 0, 0.05f, 40, 4, 3, nullptr, true, false, F_HZ },
    { "vl1_wave", "Voice LFO 1 Wave", 1, 0, 0, 0, 0, &vlfoNames, true, false, F_X },
    { "vl2_rate", "Voice LFO 2 Rate", 0, 0.05f, 40, 0.7f, 3, nullptr, true, false, F_HZ },
    { "vl2_wave", "Voice LFO 2 Wave", 1, 0, 0, 6, 0, &vlfoNames, true, false, F_X },
};
static_assert (sizeof (table) / sizeof (table[0]) == T_COUNT, "track param table size");
} // namespace

const TrackParamDef& trackParam (int i) { return table[jlimit (0, (int) T_COUNT - 1, i)]; }
String trackParamId (int track, int i) { return "t" + String (track + 1) + "_" + table[i].id; }

int trackOfParamId (const String& id, int* local)
{
    if (! id.startsWithChar ('t')) return -1;
    const int us = id.indexOfChar ('_');
    if (us < 2) return -1;
    const auto num = id.substring (1, us);
    if (! num.containsOnly ("0123456789")) return -1;
    const int t = num.getIntValue() - 1;
    if (t < 0 || t >= kNumTracks) return -1;
    const auto suffix = id.substring (us + 1);
    for (int i = 0; i < T_COUNT; ++i)
        if (suffix == table[i].id) { if (local != nullptr) *local = i; return t; }
    return -1;
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

    // ---------------------------------------------------------------- tracks
    auto fmtFor = [] (int f) -> String (*) (float, int)
    {
        switch (f)
        {
            case F_PCT: return fmtPct;
            case F_BIP: return fmtBip;
            case F_HZ: return fmtHz;
            case F_SEC: return fmtSec;
            case F_MS: return fmtMs;
            case F_SEMI: return fmtSemi;
            case F_CENT: return fmtCent;
            case F_DB: return [] (float v, int) { return v <= -59.9f ? String ("-inf dB") : (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
            case F_RATIO: return [] (float v, int) { return "x" + String (v, 2); };
            case F_CIRC: return [] (float v, int)
            {
                const int a = jlimit (0, 2, (int) v); const int pct = roundToInt ((v - (float) a) * 100.0f);
                return pct == 0 ? circuitNames()[a] : circuitNames()[a] + " > " + circuitNames()[a + 1] + " " + String (pct) + "%";
            };
            default: return fmtX;
        }
    };
    for (int t = 0; t < kNumTracks; ++t)
    {
        const String p = "T" + String (t + 1) + " ";
        for (int i = 0; i < T_COUNT; ++i)
        {
            const auto& d = table[i];
            const auto id = trackParamId (t, i);
            switch (d.kind)
            {
                case 0: B.f (id, p + d.name, Builder::range (d.lo, d.hi, d.centre), d.def, fmtFor (d.fmt)); break;
                case 1: B.c (id, p + d.name, *d.choices, (int) d.def); break;
                case 2: B.b (id, p + d.name, d.def > 0.5f); break;
                case 3:
                    if (d.fmt == F_NOTE) B.i (id, p + d.name, (int) d.lo, (int) d.hi, (int) d.def);
                    else B.i (id, p + d.name, (int) d.lo, (int) d.hi, (int) d.def);
                    break;
            }
        }
    }

    // ---------------------------------------------------------------- global sequencer / performance
    B.b ("g_play", "Play (standalone)", false, false);
    B.b ("g_follow", "Pattern follows the DAW", true, false);   // off when the song plays Hamster from a clip
    // the kick and the toms / congas measured and tuned to a key (the song's, when Racoon builds a block)
    B.c ("g_kitkey", "Afinar el kit a", StringArray { "Sin afinar", "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }, 0, false);
    B.c ("g_kitscale", "Escala del kit", StringArray { "Menor", "Mayor" }, 0, false);
    // "General MIDI": whatever kit is loaded, each drum answers to its GM note (Racoon's clips are written in GM)
    B.c ("g_notemap", "Notas del kit", StringArray { "Como viene el kit", "General MIDI" }, 0, false);
    B.f ("g_bpm", "Internal BPM", lin (40.0f, 240.0f), 124.0f, [] (float v, int) { return String (v, 1) + " BPM"; });
    B.f ("g_swing", "Swing", lin (0.0f, 0.75f), 0.0f, fmtPct);
    B.i ("g_pattern", "Pattern", 1, kNumPatterns, 1);
    B.b ("g_songmode", "Song Mode", false);
    B.b ("g_fill", "Fill", false);
    B.c ("g_fillevery", "Auto Fill", StringArray { "Off", "Every 2 bars", "Every 4 bars", "Every 8 bars", "Every 16 bars" }, 0);
    B.b ("g_break", "Break", false);
    B.c ("g_scatter", "Scatter", StringArray { "Off", "1/2", "1/4", "1/8", "1/16", "1/32" }, 0);
    B.f ("g_morph", "Pattern Morph", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.i ("g_morphtarget", "Morph Target", 1, kNumPatterns, 2);
    B.f ("g_groove", "Groove Amount", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.f ("g_humanize", "Humanize", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.c ("g_quality", "Quality", StringArray { "Eco", "Normal 2x", "High 4x", "Ultra 8x" }, 1, false);
    B.c ("g_noterepeat", "Note Repeat", StringArray { "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 0);
    B.f ("g_velcurve", "Velocity Curve", lin (-1.0f, 1.0f), 0.0f, fmtBip);
    B.b ("g_record", "Record", false, false);
    B.f ("g_accent", "Accent Amount", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.f ("g_master", "Master Volume", lin (0.0f, 1.0f), 0.8f, fmtPct);
    B.f ("g_xyx", "XY X", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.f ("g_xyy", "XY Y", lin (0.0f, 1.0f), 0.5f, fmtPct);
    B.i ("g_scenea", "Scene A", 1, kNumScenes, 1);
    B.i ("g_sceneb", "Scene B", 1, kNumScenes, 2);
    B.f ("g_scenefade", "Scene Fade", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.b ("g_sceneon", "Scenes Active", false);
    B.c ("g_mpe", "MIDI Mode", StringArray { "Standard", "MPE" }, 0);

    // ---------------------------------------------------------------- global LFOs
    for (int l = 0; l < kNumGLfos; ++l)
    {
        const String p = "LFO " + String (l + 1) + " ";
        B.c (glfoId (l, "wave"), p + "Wave", StringArray { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold", "Smooth Random", "Drawn" }, 0);
        B.f (glfoId (l, "rate"), p + "Rate", skew (0.01f, 40.0f, 1.0f), 1.0f, fmtHz);
        B.b (glfoId (l, "sync"), p + "Sync", true);
        B.c (glfoId (l, "div"), p + "Division", syncDivisionNames(), 7);
        B.f (glfoId (l, "depth"), p + "Depth", lin (0.0f, 1.0f), 1.0f, fmtPct);
        B.f (glfoId (l, "phase"), p + "Phase", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.c (glfoId (l, "polarity"), p + "Polarity", StringArray { "Bipolar", "Unipolar" }, 0);
    }

    // ---------------------------------------------------------------- matrix
    StringArray trackChoices { "All Tracks" };
    for (int t = 0; t < kNumTracks; ++t) trackChoices.add ("Track " + String (t + 1));
    for (int r = 0; r < kNumRoutes; ++r)
    {
        const String p = "Mod " + String (r + 1) + " ";
        B.b (routeId (r, "on"), p + "Active", true, false);
        B.c (routeId (r, "src"), p + "Source", modSourceNames(), 0, false);
        B.c (routeId (r, "trk"), p + "Track", trackChoices, 0, false);
        B.c (routeId (r, "dst"), p + "Destination", modDestNames(), 0, false);
        B.f (routeId (r, "amt"), p + "Amount", lin (-1.0f, 1.0f), 0.0f, fmtBip);
        B.c (routeId (r, "curve"), p + "Curve", StringArray { "Linear", "Exponential", "Logarithmic", "S-Curve" }, 0, false);
    }
    for (int m = 0; m < kNumMacros; ++m) B.f (macroId (m), "Macro " + String (m + 1), lin (0.0f, 1.0f), 0.0f, fmtPct);

    // ---------------------------------------------------------------- aux buses, groups, master
    for (int a = 0; a < kNumAux; ++a)
    {
        const String p = "Aux " + String (a + 1) + " ";
        B.c (auxId (a, "type"), p + "Type", auxTypeNames(), a == 0 ? 1 : (a == 1 ? 5 : (a == 2 ? 2 : 6)));
        B.f (auxId (a, "p1"), p + "Size / Time", lin (0.0f, 1.0f), 0.5f, fmtPct);
        B.f (auxId (a, "p2"), p + "Tone / Feedback", lin (0.0f, 1.0f), 0.5f, fmtPct);
        B.f (auxId (a, "p3"), p + "Width / Depth", lin (0.0f, 1.0f), 0.7f, fmtPct);
        B.f (auxId (a, "p4"), p + "Mod / Pre-Delay", lin (0.0f, 1.0f), 0.2f, fmtPct);
        B.f (auxId (a, "return"), p + "Return", lin (0.0f, 1.0f), 0.7f, fmtPct);
        B.f (auxId (a, "lowcut"), p + "Low Cut", skew (20.0f, 2000.0f, 200.0f), 180.0f, fmtHz);
        B.f (auxId (a, "hicut"), p + "High Cut", skew (500.0f, 20000.0f, 6000.0f), 12000.0f, fmtHz);
    }
    for (int g = 0; g < kNumGroups; ++g)
    {
        const String p = groupNames()[g + 1] + " Group ";
        B.f (groupId (g, "level"), p + "Level", Builder::range (-60.0f, 6.0f, -12.0f), 0.0f, [] (float v, int) { return v <= -59.9f ? String ("-inf dB") : String (v, 1) + " dB"; });
        B.f (groupId (g, "comp"), p + "Compression", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (groupId (g, "drive"), p + "Saturation", lin (0.0f, 1.0f), 0.0f, fmtPct);
        B.f (groupId (g, "width"), p + "Width", lin (0.0f, 2.0f), 1.0f, fmtPct);
    }
    B.b ("glue_on", "Glue On", true);
    B.f ("glue_thresh", "Glue Threshold", lin (-40.0f, 0.0f), -10.0f, fmtDb);
    B.f ("glue_ratio", "Glue Ratio", skew (1.0f, 10.0f, 3.0f), 2.0f, fmtRatio);
    B.f ("glue_attack", "Glue Attack", skew (0.1f, 100.0f, 10.0f), 10.0f, fmtMs);
    B.f ("glue_release", "Glue Release", skew (10.0f, 1000.0f, 150.0f), 150.0f, fmtMs);
    B.f ("glue_makeup", "Glue Makeup", lin (0.0f, 12.0f), 0.0f, fmtDb);
    B.f ("glue_mix", "Glue Mix", lin (0.0f, 1.0f), 1.0f, fmtPct);
    B.f ("softclip", "Soft Clip", lin (0.0f, 1.0f), 0.0f, fmtPct);
    B.b ("tp_on", "True Peak Limiter", true);
    B.f ("tp_ceiling", "True Peak Ceiling", lin (-6.0f, 0.0f), -0.5f, fmtDb);

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
    B.c ("duck_src", "Duck Source", StringArray { "Off", "Tempo", "MIDI Note", "Sidechain", "Track" }, 0);
    B.i ("duck_track", "Duck Trigger Track", 1, kNumTracks, 1);
    B.c ("duck_target", "Duck Target", StringArray { "Master", "All But Trigger", "Aux Returns" }, 1);
    B.c ("duck_div", "Duck Rate", syncDivisionNames(), 7);
    B.f ("duck_depth", "Duck Depth", lin (0.0f, 1.0f), 0.6f, fmtPct);
    B.f ("duck_attack", "Duck Attack", skew (0.1f, 50.0f, 5.0f), 2.0f, fmtMs);
    B.f ("duck_hold", "Duck Hold", skew (0.0f, 300.0f, 40.0f), 20.0f, fmtMs);
    B.f ("duck_release", "Duck Recovery", skew (10.0f, 1000.0f, 150.0f), 160.0f, fmtMs);
    B.f ("duck_curve", "Duck Curve", lin (-1.0f, 1.0f), 0.3f, fmtBip);
    B.c ("duck_band", "Duck Band", StringArray { "Full", "Low Only" }, 0);
    B.f ("duck_freq", "Duck Band Freq", skew (40.0f, 600.0f, 150.0f), 180.0f, fmtHz);
    B.i ("duck_note", "Duck Trigger Note", 0, 127, 24);
    B.i ("duck_channel", "Duck Trigger Channel", 0, 16, 16);
    B.f ("duck_thresh", "Sidechain Threshold", lin (-60.0f, 0.0f), -24.0f, fmtDb);

    // ---------------------------------------------------------------- Kick-Bass Bridge
    B.b ("bridge_on", "Kick-Bass Bridge", true);
    B.i ("bridge_track", "Bridge Kick Track", 1, kNumTracks, 1);

    return std::move (B.layout);
}
} // namespace be

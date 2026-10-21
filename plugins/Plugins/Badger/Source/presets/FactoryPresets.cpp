#include "PresetManager.h"
#include "../Params.h"

namespace be
{
namespace
{
// standard macro layout shared by every factory patch (routes 1..8)
void stdMacros (PatchBuilder& b)
{
    b.route ("Macro 1", "Filter 1 Cutoff", 0.45f);
    b.route ("Macro 2", "Filter 1 Resonance", 0.6f);
    b.route ("Macro 3", "Dist 1 Drive", 0.6f);
    b.route ("Macro 4", "Acid Morph", 1.0f);
    b.route ("Macro 5", "Sub Level", 0.35f);
    b.route ("Macro 6", "Reverb Mix", 0.5f);
    b.route ("Evolution", "Filter 1 Cutoff", 0.35f, "Macro 7");
    b.route ("Macro 8", "Layer XFade", 1.0f);
    b.set ("fx_reverb_on", 1); b.set ("fx_reverb_mix", 0.0f); b.set ("fx_reverb_lowcut", 250.0f); b.set ("fx_reverb_size", 0.55f);
    b.macroNames ({ "CUTOFF", "RESO", "DRIVE", "ACID", "SUB", "SPACE", "EVOLVE", "BODY/BITE", "MACRO 9", "MACRO 10", "MACRO 11", "MACRO 12", "MACRO 13", "MACRO 14", "MACRO 15", "MACRO 16" });
}

void osc (PatchBuilder& b, int o, int engine, int layer, float level = 0.75f)
{
    const juce::String p = "o" + juce::String (o) + "_";
    b.set (p + "on", 1); b.set (p + "engine", (float) engine); b.set (p + "layer", (float) layer); b.set (p + "level", level);
}

void filt (PatchBuilder& b, int f, int type, float cutoff, float res, float env, float drive = 0.1f)
{
    const juce::String p = "f" + juce::String (f) + "_";
    b.set (p + "on", 1); b.set (p + "type", (float) type); b.set (p + "cutoff", cutoff); b.set (p + "res", res); b.set (p + "envamt", env); b.set (p + "drive", drive);
}

void env (PatchBuilder& b, int e, float a, float d, float s, float r)
{
    const juce::String p = "e" + juce::String (e) + "_";
    b.set (p + "attack", a); b.set (p + "decay", d); b.set (p + "sustain", s); b.set (p + "release", r);
}

void dist (PatchBuilder& b, int d, int type, float drive, float mix = 1.0f, float tone = 0.0f)
{
    const juce::String p = "d" + juce::String (d) + "_";
    b.set (p + "on", 1); b.set (p + "type", (float) type); b.set (p + "drive", drive); b.set (p + "mix", mix); b.set (p + "tone", tone);
}

void lfo (PatchBuilder& b, int l, int wave, bool sync, int div, float rate = 1.0f, int trig = 1)
{
    const juce::String p = "l" + juce::String (l) + "_";
    b.set (p + "wave", (float) wave); b.set (p + "sync", sync ? 1.0f : 0.0f); b.set (p + "div", (float) div); b.set (p + "rate", rate); b.set (p + "trig", (float) trig);
}

// filter types
constexpr int LP12 = 0, LP24 = 2, HP12 = 4, BP = 6, LADDER = 8, CASCADE = 9, DIODE = 10, SVF = 11, COMB = 12, FORMANT = 13, ACID = 14, MORPH = 15;
// engines
constexpr int VA = 0, WT = 1, FM = 2, PD = 3, ADD = 4, SPEC = 5, GRAN = 6, SMP = 7, PHYS = 8, ACIDC = 9;
// layers
constexpr int SUB = 0, BODY = 1, BITE = 2, TEX = 3;
// distortion
constexpr int SOFT = 0, HARD = 1, TUBE = 2, TAPE = 3, TRANS = 4, DIODEd = 5, FOLD = 6, BITS = 7, FUZZ = 9, ASYM = 10, MULTI = 11;
}

void PresetManager::registerFactory()
{
    auto add = [this] (const char* name, const char* cat, const char* genre, const char* character, const char* synth, int inten, int cplx,
                       std::function<void (PatchBuilder&)> fn)
    {
        factory.push_back ({ name, cat, genre, character, synth, inten, cplx, [fn] (PatchBuilder& b) { stdMacros (b); fn (b); } });
    };

    add ("Init", "Analog Bass", "House", "Clean", "Virtual Analog", 1, 1, [] (PatchBuilder&) {});

    // ------------------------------------------------------------ ANALOG
    add ("Basalt Ladder", "Analog Bass", "House", "Warm", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        osc (b, 2, VA, BITE, 0.45f); b.set ("o2_wave", 3); b.set ("o2_fine", 7);
        filt (b, 1, LADDER, 360, 0.38f, 0.45f, 0.25f);
        env (b, 2, 0.001f, 0.32f, 0.18f, 0.12f);
        env (b, 1, 0.002f, 0.8f, 0.85f, 0.09f);
        b.set ("sub_level", 0.55f);
        dist (b, 1, TUBE, 0.2f);
    });
    add ("Obsidian Round", "Analog Bass", "Deep House", "Warm", "Virtual Analog", 2, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY, 0.7f); b.set ("o1_wave", 1); b.set ("o1_pb", 0.35f);
        filt (b, 1, LP24, 650, 0.15f, 0.25f);
        env (b, 2, 0.002f, 0.4f, 0.3f, 0.2f);
        b.set ("sub_level", 0.65f); b.set ("sub_wave", 4);
        b.set ("d1_drive", 0.12f);
    });
    add ("Temple Pulse", "Analog Bass", "Tech House", "Bright", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 4); b.set ("o1_pa", 0.3f);
        filt (b, 1, CASCADE, 700, 0.3f, 0.4f, 0.2f);
        env (b, 2, 0.001f, 0.22f, 0.1f, 0.1f);
        lfo (b, 1, 0, false, 10, 0.35f, 0);
        b.route ("LFO 1", "Osc 1 Param A", 0.25f);
        b.set ("fx_chorus_on", 1); b.set ("fx_chorus_mix", 0.25f);
        b.set ("sub_level", 0.5f);
    });
    add ("Copper Drift", "Analog Bass", "Indie Dance", "Warm", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2); b.set ("o1_unison", 3); b.set ("o1_detune", 0.12f);
        filt (b, 1, LADDER, 750, 0.25f, 0.3f, 0.3f);
        b.set ("imp_amount", 0.6f); b.set ("imp_drift", 0.6f);
        b.set ("sub_level", 0.5f);
        dist (b, 1, TAPE, 0.3f);
    });

    // ------------------------------------------------------------ ACID
    add ("Sun Glyph 303", "Acid Bass", "Acid House", "Squelchy", "Acid Circuit", 4, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, ACIDC, BODY, 0.8f); b.set ("o1_pa", 0.0f); b.set ("o1_pb", 0.5f); b.set ("o1_pc", 0.3f);
        b.set ("acid_morph", 1.0f); b.set ("acid_cutoff", 320); b.set ("acid_res", 0.8f); b.set ("acid_envmod", 0.7f); b.set ("acid_decay", 0.35f);
        b.set ("acid_accent", 0.8f);
        b.set ("sub_on", 0); b.set ("f1_on", 0);
        b.set ("v_mode", 1); b.set ("v_glide", 0.06f);
        dist (b, 1, TUBE, 0.32f);
        b.set ("fx_delay_on", 1); b.set ("fx_delay_mix", 0.12f); b.set ("fx_delay_div", 12);
    });
    add ("Square Acid Codex", "Acid Bass", "Acid Techno", "Dirty", "Acid Circuit", 4, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, ACIDC, BODY, 0.8f); b.set ("o1_pa", 1.0f); b.set ("o1_pb", 0.5f); b.set ("o1_pc", 0.4f);
        b.set ("acid_morph", 1.0f); b.set ("acid_cutoff", 260); b.set ("acid_res", 0.86f); b.set ("acid_envmod", 0.75f); b.set ("acid_decay", 0.28f);
        b.set ("acid_overdrive", 0.35f);
        b.set ("sub_on", 0); b.set ("f1_on", 0);
        dist (b, 1, DIODEd, 0.3f);
    });
    add ("Half Moon Acid", "Acid Bass", "House", "Squelchy", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        filt (b, 1, LADDER, 500, 0.3f, 0.35f);
        b.set ("acid_morph", 0.0f); b.set ("mac4", 0.5f);
        b.set ("acid_cutoff", 380); b.set ("acid_res", 0.75f);
        b.set ("sub_level", 0.45f);
    });
    add ("Rave Ritual", "Acid Bass", "Acid Techno", "Aggressive", "Acid Circuit", 5, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, ACIDC, BODY, 0.8f); b.set ("o1_pb", 0.5f); b.set ("o1_pc", 0.5f);
        b.set ("acid_morph", 1.0f); b.set ("acid_cutoff", 350); b.set ("acid_res", 0.82f); b.set ("acid_envmod", 0.8f); b.set ("acid_decay", 0.45f);
        b.set ("sub_on", 0); b.set ("f1_on", 0);
        dist (b, 1, TUBE, 0.35f); dist (b, 2, FUZZ, 0.3f, 0.35f);
        b.set ("fx_delay_on", 1); b.set ("fx_delay_mix", 0.2f); b.set ("fx_delay_div", 12); b.set ("fx_delay_feedback", 0.45f);
        b.set ("seq_mode", 1); b.seqPattern (2); b.set ("seq_rate", 4);
    });

    // ------------------------------------------------------------ ROLLING
    add ("Rolling Stone", "Rolling Bass", "Tech House", "Punchy", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        filt (b, 1, LADDER, 420, 0.3f, 0.5f, 0.2f);
        env (b, 1, 0.001f, 0.22f, 0.0f, 0.06f);
        env (b, 2, 0.001f, 0.12f, 0.0f, 0.06f);
        b.set ("sub_level", 0.6f); b.set ("sub_env", 0);
        b.set ("seq_mode", 1); b.seqPattern (1);
        b.set ("v_mode", 0);
    });
    add ("Sixteenth River", "Rolling Bass", "Techno", "Punchy", "Wavetable", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 0); b.set ("o1_pos", 0.62f);
        filt (b, 1, LADDER, 500, 0.25f, 0.45f);
        env (b, 1, 0.001f, 0.18f, 0.0f, 0.05f);
        env (b, 2, 0.001f, 0.1f, 0.0f, 0.05f);
        b.set ("duck_src", 1); b.set ("duck_div", 7); b.set ("duck_depth", 0.65f);
        b.set ("v_mode", 2);
    });
    add ("Offbeat Engine", "Rolling Bass", "House", "Clean", "Virtual Analog", 2, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        osc (b, 2, VA, BITE, 0.35f); b.set ("o2_wave", 3); b.set ("o2_oct", 1);
        filt (b, 1, LP24, 900, 0.2f, 0.3f);
        env (b, 1, 0.002f, 0.3f, 0.0f, 0.08f);
        b.set ("seq_mode", 1); b.seqPattern (3);
        b.set ("sub_level", 0.6f);
    });
    add ("Tide Roller", "Rolling Bass", "Progressive", "Smooth", "FM", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.32f); b.set ("o1_pb", 0.0334f);
        filt (b, 1, LP24, 1200, 0.15f, 0.25f);
        env (b, 3, 0.001f, 0.15f, 0.0f, 0.1f);
        b.route ("Env 3", "Osc 1 Param A", 0.35f);
        env (b, 1, 0.001f, 0.25f, 0.0f, 0.08f);
        b.set ("duck_src", 1); b.set ("duck_depth", 0.5f);
        b.set ("v_mode", 2);
    });

    // ------------------------------------------------------------ INDIE DANCE
    add ("Neon Monolith", "Indie Dance", "Indie Dance", "Bright", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        osc (b, 2, VA, BITE, 0.4f); b.set ("o2_wave", 3); b.set ("o2_oct", 1);
        filt (b, 1, LADDER, 1100, 0.3f, 0.3f, 0.25f);
        env (b, 2, 0.001f, 0.25f, 0.2f, 0.1f);
        b.set ("fx_chorus_on", 1); b.set ("fx_chorus_mix", 0.3f);
        b.set ("seq_mode", 1); b.seqPattern (4);
    });
    add ("Glass Pyramid", "Indie Dance", "Indie Dance", "Bright", "Phase Dist", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, PD, BODY); b.set ("o1_pa", 0.55f); b.set ("o1_pb", 0.7f); b.set ("o1_pc", 0.2f);
        env (b, 3, 0.001f, 0.3f, 0.1f, 0.1f);
        b.route ("Env 3", "Osc 1 Param A", 0.35f);
        filt (b, 1, LP12, 2500, 0.1f, 0.1f);
        b.set ("fx_delay_on", 1); b.set ("fx_delay_mix", 0.15f); b.set ("fx_delay_div", 12);
        b.set ("sub_level", 0.55f);
    });
    add ("Velvet Oracle", "Indie Dance", "Nu Disco", "Warm", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2); b.set ("o1_unison", 4); b.set ("o1_detune", 0.15f);
        filt (b, 1, CASCADE, 900, 0.25f, 0.35f);
        b.set ("fx_phaser_on", 1); b.set ("fx_phaser_mix", 0.35f);
        b.set ("sub_level", 0.55f);
    });
    add ("Dance of Jaguars", "Indie Dance", "Indie Dance", "Vocal", "Wavetable", 4, 4, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 14); b.set ("o1_pos", 0.3f);
        lfo (b, 1, 1, true, 10);
        b.route ("LFO 1", "Osc 1 Position", 0.3f);
        filt (b, 1, LADDER, 1400, 0.2f, 0.2f);
        filt (b, 3, FORMANT, 1000, 0.4f, 0.0f); b.set ("f3_mix", 0.35f);
        b.route ("LFO 1", "Filter 3 Morph", 0.4f);
        b.set ("filter_routing", 0);
        b.set ("sub_level", 0.5f);
    });

    // ------------------------------------------------------------ DARK DISCO
    add ("Midnight Codex", "Dark Disco", "Dark Disco", "Dark", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        osc (b, 2, PD, BITE, 0.35f); b.set ("o2_pa", 0.6f);
        filt (b, 1, LADDER, 420, 0.45f, 0.55f, 0.3f);
        env (b, 2, 0.001f, 0.25f, 0.1f, 0.1f);
        b.set ("seq_mode", 1); b.seqPattern (5); b.set ("seq_rate", 1);
        b.set ("sub_level", 0.55f);
        dist (b, 1, TUBE, 0.28f);
    });
    add ("Eclipse Pulse", "Dark Disco", "Dark Disco", "Dark", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 4); b.set ("o1_pa", 0.3f);
        filt (b, 1, LP24, 620, 0.25f, 0.4f);
        b.set ("fx_chorus_on", 1); b.set ("fx_chorus_mix", 0.3f);
        b.set ("fx_delay_on", 1); b.set ("fx_delay_mix", 0.12f); b.set ("fx_delay_div", 10);
        b.set ("sub_level", 0.6f);
    });
    add ("Black Sun Funk", "Dark Disco", "Dark Disco", "Metallic", "FM", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.45f); b.set ("o1_pb", 0.1f);
        filt (b, 1, LP24, 800, 0.2f, 0.4f);
        env (b, 2, 0.001f, 0.2f, 0.1f, 0.1f);
        b.set ("sub_level", 0.55f);
    });
    add ("Cult Fifths", "Dark Disco", "Dark Disco", "Dark", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        osc (b, 2, VA, BITE, 0.35f); b.set ("o2_wave", 2); b.set ("o2_semi", 7);
        filt (b, 1, LADDER, 650, 0.35f, 0.4f, 0.25f);
        b.set ("sub_level", 0.55f);
    });

    // ------------------------------------------------------------ TECHNO RUMBLE
    add ("Underworld Rumble", "Techno Rumble", "Techno", "Dark", "Virtual Analog", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY, 0.6f); b.set ("o1_wave", 0);
        b.set ("noise_on", 1); b.set ("noise_type", 2); b.set ("noise_level", 0.25f); b.set ("noise_layer", 3);
        filt (b, 1, LP24, 220, 0.2f, 0.2f);
        b.set ("fx_reverb_mix", 0.28f); b.set ("fx_reverb_size", 0.75f); b.set ("fx_reverb_lowcut", 60.0f);
        b.set ("duck_src", 1); b.set ("duck_div", 7); b.set ("duck_depth", 0.9f);
        dist (b, 1, TAPE, 0.5f);
        b.set ("sub_level", 0.8f);
    });
    add ("Stone Cathedral", "Techno Rumble", "Techno", "Dark", "Wavetable", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 28); b.set ("o1_pos", 0.5f);
        filt (b, 1, LP24, 300, 0.25f, 0.3f);
        b.set ("fx_reverb_mix", 0.3f); b.set ("fx_reverb_size", 0.8f); b.set ("fx_reverb_lowcut", 70.0f);
        b.set ("fx_delay_on", 1); b.set ("fx_delay_mix", 0.2f); b.set ("fx_delay_div", 10);
        b.set ("duck_src", 1); b.set ("duck_depth", 0.85f);
    });
    add ("Pressure Chamber", "Techno Rumble", "Techno", "Aggressive", "FM", 5, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.22f); b.set ("o1_pb", 0.0334f);
        filt (b, 1, LP24, 260, 0.3f, 0.5f);
        dist (b, 1, MULTI, 0.5f);
        b.set ("duck_src", 1); b.set ("duck_depth", 0.75f);
    });
    add ("Ritual Drone", "Techno Rumble", "Techno", "Atmospheric", "Granular", 4, 4, [] (PatchBuilder& b)
    {
        osc (b, 1, GRAN, BODY); b.set ("o1_sample", 1); b.set ("o1_pos", 0.15f); b.set ("o1_pa", 0.45f); b.set ("o1_pb", 0.6f); b.set ("o1_pc", 0.15f);
        lfo (b, 2, 6, false, 10, 0.2f, 0);
        b.route ("LFO 2", "Osc 1 Position", 0.15f);
        filt (b, 1, LP24, 400, 0.2f, 0.1f);
        b.set ("fx_reverb_mix", 0.5f);
        b.set ("sub_level", 0.6f);
    });

    // ------------------------------------------------------------ SUB
    add ("Pure Sub Glyph", "Sub Bass", "House", "Clean", "Virtual Analog", 1, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY, 0.35f); b.set ("o1_wave", 0);
        filt (b, 1, LP12, 400, 0.0f, 0.0f);
        b.set ("sub_level", 0.9f); b.set ("d1_drive", 0.08f);
    });
    add ("808 Monument", "Sub Bass", "Trap House", "Punchy", "Sample", 3, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, SMP, BODY, 0.9f); b.set ("o1_sample", 1); b.set ("o1_pos", 0.0f); b.set ("o1_pb", 0.0f);
        b.set ("f1_on", 0); b.set ("sub_on", 0);
        env (b, 1, 0.001f, 2.0f, 0.0f, 0.3f);
        b.set ("v_mode", 0); b.set ("v_glide", 0.08f); b.set ("v_glidemode", 1);
        dist (b, 1, SOFT, 0.3f);
    });
    add ("Deep Earth", "Sub Bass", "Deep House", "Warm", "Virtual Analog", 2, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY, 0.45f); b.set ("o1_wave", 0); b.set ("o1_fold", 0.15f);
        b.set ("sub_wave", 3); b.set ("sub_level", 0.8f);
        filt (b, 1, LP24, 500, 0.1f, 0.15f);
    });
    add ("Low Altar", "Sub Bass", "Techno", "Warm", "Virtual Analog", 2, 1, [] (PatchBuilder& b)
    {
        b.set ("o1_on", 0);
        b.set ("sub_level", 0.95f); b.set ("sub_drive", 0.35f); b.set ("sub_wave", 0);
        env (b, 1, 0.003f, 1.2f, 0.6f, 0.15f);
        b.set ("f1_on", 0);
    });

    // ------------------------------------------------------------ FM
    add ("Jade FM", "FM Bass", "Tech House", "Punchy", "FM", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.3f); b.set ("o1_pb", 0.0334f);
        env (b, 3, 0.001f, 0.25f, 0.0f, 0.1f);
        b.route ("Env 3", "Osc 1 Param A", 0.5f);
        b.set ("f1_on", 0);
        b.set ("sub_level", 0.5f);
    });
    add ("Bell Serpent", "FM Bass", "Progressive", "Metallic", "FM", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.3f); b.set ("o1_pb", 0.1667f);
        filt (b, 1, LP24, 1800, 0.1f, 0.2f);
        b.set ("sub_level", 0.65f);
    });
    add ("Feedback Glyph", "FM Bass", "Techno", "Aggressive", "FM", 4, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.3f); b.set ("o1_pb", 0.0334f); b.set ("o1_pc", 0.6f);
        filt (b, 1, LADDER, 900, 0.2f, 0.35f);
        b.set ("sub_level", 0.5f);
    });
    add ("Electric Jaguar", "FM Bass", "Indie Dance", "Bright", "FM", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, FM, BODY); b.set ("o1_pa", 0.35f); b.set ("o1_pb", 0.1f);
        lfo (b, 1, 1, true, 13);
        b.route ("LFO 1", "Osc 1 Param A", 0.2f);
        filt (b, 1, LP24, 1300, 0.2f, 0.3f);
        b.set ("sub_level", 0.55f);
    });

    // ------------------------------------------------------------ WAVETABLE
    add ("Growl Stela", "Wavetable Bass", "Bass House", "Aggressive", "Wavetable", 5, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 6); b.set ("o1_pos", 0.2f);
        lfo (b, 1, 0, true, 10);
        b.route ("LFO 1", "Osc 1 Position", 0.6f);
        filt (b, 1, LP24, 2200, 0.2f, 0.1f);
        dist (b, 1, MULTI, 0.45f);
        b.set ("sub_level", 0.6f);
    });
    add ("Formant Mask", "Wavetable Bass", "Bass House", "Vocal", "Wavetable", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 5); b.set ("o1_pos", 0.0f);
        b.route ("Macro 9", "Osc 1 Position", 1.0f);
        b.set ("mac9", 0.4f);
        filt (b, 1, LP24, 2500, 0.15f, 0.1f);
        b.set ("sub_level", 0.55f);
        PatchBuilder& bb = b; bb.macroNames ({ "CUTOFF", "RESO", "DRIVE", "ACID", "SUB", "SPACE", "EVOLVE", "BODY/BITE", "VOWEL", "MACRO 10", "MACRO 11", "MACRO 12", "MACRO 13", "MACRO 14", "MACRO 15", "MACRO 16" });
    });
    add ("Sync Obelisk", "Wavetable Bass", "Tech House", "Bright", "Wavetable", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 10); b.set ("o1_pos", 0.1f);
        env (b, 3, 0.001f, 0.3f, 0.0f, 0.1f);
        b.route ("Env 3", "Osc 1 Position", 0.7f);
        filt (b, 1, LP24, 2000, 0.15f, 0.2f);
        b.set ("sub_level", 0.55f);
    });
    add ("Fold Temple", "Wavetable Bass", "Techno", "Dirty", "Wavetable", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 11); b.set ("o1_pos", 0.35f);
        lfo (b, 1, 6, false, 10, 0.3f, 0);
        b.route ("LFO 1", "Osc 1 Position", 0.3f);
        filt (b, 1, LADDER, 900, 0.3f, 0.3f);
        b.set ("sub_level", 0.55f);
    });

    // ------------------------------------------------------------ PLUCK
    add ("Stone Pluck", "Pluck Bass", "Deep House", "Organic", "Physical", 2, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, PHYS, BODY, 0.8f); b.set ("o1_pa", 0.6f); b.set ("o1_pb", 0.1f); b.set ("o1_pc", 0.2f); b.set ("o1_pos", 0.3f);
        filt (b, 1, LP24, 2200, 0.1f, 0.1f);
        env (b, 1, 0.001f, 1.2f, 0.0f, 0.15f);
        b.set ("sub_level", 0.45f); b.set ("sub_env", 2);
        env (b, 3, 0.001f, 0.6f, 0.0f, 0.15f);
    });
    add ("Bone Thumb", "Pluck Bass", "Funk", "Organic", "Physical", 2, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, PHYS, BODY, 0.8f); b.set ("o1_pa", 0.75f); b.set ("o1_pb", 0.5f); b.set ("o1_pc", 0.5f); b.set ("o1_pos", 0.2f);
        filt (b, 1, LP24, 1500, 0.1f, 0.1f);
        env (b, 1, 0.001f, 0.9f, 0.0f, 0.12f);
        b.set ("sub_level", 0.4f);
    });
    add ("Picked Granite", "Pluck Bass", "Indie Dance", "Organic", "Sample", 2, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, SMP, BODY, 0.85f); b.set ("o1_sample", 3); b.set ("o1_pb", 0.0f);
        filt (b, 1, LP24, 3000, 0.1f, 0.1f);
        env (b, 1, 0.001f, 2.0f, 0.0f, 0.12f);
        b.set ("sub_level", 0.35f);
    });
    add ("Short Glyph", "Pluck Bass", "House", "Punchy", "Virtual Analog", 2, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        filt (b, 1, LADDER, 300, 0.3f, 0.65f, 0.2f);
        env (b, 1, 0.001f, 0.18f, 0.0f, 0.08f);
        env (b, 2, 0.001f, 0.12f, 0.0f, 0.08f);
        b.set ("sub_level", 0.6f);
        b.set ("v_mode", 2);
    });

    // ------------------------------------------------------------ REESE
    add ("Reese of Ra", "Reese Bass", "Breaks", "Dark", "Virtual Analog", 4, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2); b.set ("o1_unison", 4); b.set ("o1_detune", 0.3f);
        osc (b, 2, VA, BITE, 0.5f); b.set ("o2_wave", 2); b.set ("o2_unison", 2); b.set ("o2_detune", 0.35f); b.set ("o2_fine", -11);
        filt (b, 1, LP24, 1000, 0.2f, 0.15f);
        b.set ("fx_chorus_on", 1); b.set ("fx_chorus_mix", 0.25f);
        b.set ("sub_level", 0.7f);
    });
    add ("Twin Serpents", "Reese Bass", "Techno", "Dark", "Virtual Analog", 4, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2); b.set ("o1_fine", 12);
        osc (b, 2, VA, BITE, 0.7f); b.set ("o2_wave", 2); b.set ("o2_fine", -12);
        filt (b, 1, LADDER, 700, 0.25f, 0.1f);
        b.set ("fx_phaser_on", 1); b.set ("fx_phaser_mix", 0.3f);
        b.set ("sub_level", 0.65f);
    });
    add ("Reese Monolith", "Reese Bass", "Bass House", "Aggressive", "Wavetable", 5, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, WT, BODY); b.set ("o1_table", 7); b.set ("o1_pos", 0.3f); b.set ("o1_unison", 3); b.set ("o1_detune", 0.2f);
        lfo (b, 1, 0, true, 4);
        b.route ("LFO 1", "Osc 1 Position", 0.4f);
        filt (b, 1, LP24, 1500, 0.2f, 0.1f);
        dist (b, 1, MULTI, 0.4f);
        b.set ("sub_level", 0.65f);
    });
    add ("Hollow Reese", "Reese Bass", "Breaks", "Atmospheric", "Virtual Analog", 4, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2); b.set ("o1_unison", 6); b.set ("o1_detune", 0.25f);
        filt (b, 1, COMB, 220, 0.6f, 0.0f); b.set ("f1_mix", 0.5f);
        filt (b, 2, LP24, 1600, 0.1f, 0.0f);
        b.set ("sub_level", 0.65f);
    });

    // ------------------------------------------------------------ FUNK
    add ("Funk Hieroglyph", "Funk Bass", "Funk", "Bright", "Virtual Analog", 3, 2, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 3);
        osc (b, 2, VA, BITE, 0.4f); b.set ("o2_wave", 2);
        filt (b, 1, LP12, 350, 0.45f, 0.6f);
        env (b, 2, 0.001f, 0.16f, 0.1f, 0.1f);
        b.set ("f1_velsens", 0.6f);
        b.set ("sub_level", 0.5f);
        b.set ("v_mode", 2);
    });
    add ("Slap Codex", "Funk Bass", "Nu Disco", "Organic", "Sample", 3, 1, [] (PatchBuilder& b)
    {
        osc (b, 1, SMP, BODY, 0.85f); b.set ("o1_sample", 4); b.set ("o1_pb", 0.0f);
        filt (b, 1, LP24, 4000, 0.15f, 0.1f);
        env (b, 1, 0.001f, 1.2f, 0.0f, 0.1f);
        b.set ("sub_level", 0.35f);
        b.set ("v_mode", 2);
    });
    add ("Quack Totem", "Funk Bass", "Funk", "Vocal", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        filt (b, 1, FORMANT, 600, 0.5f, 0.0f);
        env (b, 3, 0.001f, 0.2f, 0.1f, 0.1f);
        b.route ("Env 3", "Filter 1 Morph", 0.7f);
        b.route ("Velocity", "Filter 1 Cutoff", 0.25f);
        b.set ("f1_morph", 0.1f);
        b.set ("sub_level", 0.55f);
    });
    add ("Wah Ritual", "Funk Bass", "Nu Disco", "Bright", "Virtual Analog", 3, 3, [] (PatchBuilder& b)
    {
        osc (b, 1, VA, BODY); b.set ("o1_wave", 2);
        filt (b, 1, SVF, 700, 0.6f, 0.0f); b.set ("f1_morph", 0.45f);
        lfo (b, 1, 0, true, 10);
        b.route ("LFO 1", "Filter 1 Cutoff", 0.25f);
        b.set ("sub_level", 0.5f);
    });
}
} // namespace be

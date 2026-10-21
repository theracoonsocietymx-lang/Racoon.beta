#include "PresetManager.h"
#include "Biblioteca.h"

namespace be
{
namespace
{
// models
constexpr int M808 = 0, M909 = 1, M606 = 2, MCR78 = 3, MBAS = 4;
// standard 16-track layout used by every factory kit
enum Slot { KICK, KICK2, SNARE, CLAP, RIM, CH, OH, RIDE, CRASH, SHAKER, PERC1, PERC2, PERC3, BELL, METAL, FX };
const int kRoles[16] { (int) Role::Kick, (int) Role::Kick, (int) Role::Snare, (int) Role::Clap, (int) Role::Rim, (int) Role::ClosedHat, (int) Role::OpenHat,
                       (int) Role::Ride, (int) Role::Crash, (int) Role::Shaker, (int) Role::Conga, (int) Role::Bongo, (int) Role::TomHigh,
                       (int) Role::Cowbell, (int) Role::Metal, (int) Role::Custom };

struct Genre
{
    juce::String name;
    int bpm;
    float swing;
    juce::String pat[16];        // pattern A per slot (empty = silent)
    int perc[3];                 // roles for the three percussion slots
    float kickTune, kickDecay, kickDrop, kickClick;
    float hatDecay, snareTone;
    float roomSend, echoSend;
    int aux4Type;
};

void stdMacros (KitBuilder& b)
{
    b.route ("Macro 1", KICK + 1, "Track Analog P1", 0.4f);
    b.route ("Macro 2", KICK + 1, "Track Analog P6", 0.5f);
    b.route ("Macro 3", SNARE + 1, "Track Analog P2", 0.4f);
    b.route ("Macro 3", CLAP + 1, "Track Analog P3", 0.4f);
    b.route ("Macro 4", CH + 1, "Track Analog P4", 0.45f);
    b.route ("Macro 4", OH + 1, "Track Analog P4", 0.35f);
    b.route ("Macro 5", 0, "Auto Filter Cutoff", -0.85f);
    b.route ("Macro 6", 0, "Track Drive", 0.45f);
    b.route ("Macro 7", 0, "Aux 1 Return", 0.45f);
    b.route ("Macro 7", 0, "Aux 3 Return", 0.4f);
    b.route ("Macro 8", 0, "Aux 2 Return", 0.6f);
    b.route ("Macro 9", 0, "Swing", 0.35f);
    b.route ("Macro 10", 0, "Track Compression", 0.5f);
    b.route ("Macro 11", 0, "Track Tune", 0.08f);
    b.route ("Macro 12", 0, "Track Bitcrush", 0.6f);
    b.route ("Macro 13", CH + 1, "Track Level", 0.3f);
    b.macroNames ({ "KICK TUNE", "KICK DECAY", "SNARE", "HAT DECAY", "FILTER", "DRIVE", "SPACE", "ECHO", "SWING", "PUNCH", "PITCH", "CRUSH",
                    "HATS UP", "MACRO 14", "MACRO 15", "MACRO 16" });
    // master: auto filter acts as a clean sweep for the FILTER macro
    b.set (fxId ("autofilter", "on"), 1.0f);
    b.set (fxId ("autofilter", "cutoff"), 20000.0f);
    b.set (fxId ("autofilter", "depth"), 0.0f);
    b.set (fxId ("autofilter", "res"), 0.15f);
    b.set ("glue_thresh", -12.0f);
    b.set ("glue_ratio", 2.0f);
    b.set ("glue_attack", 12.0f);
    b.set ("glue_release", 120.0f);
    b.set ("g_master", 0.8f);   // refined per variant in buildKit
}

void setAux (KitBuilder& b, int a, int type, float p1, float p2, float p3, float p4, float ret, float lowcut, float hicut)
{
    b.set (auxId (a, "type"), (float) type); b.set (auxId (a, "p1"), p1); b.set (auxId (a, "p2"), p2); b.set (auxId (a, "p3"), p3);
    b.set (auxId (a, "p4"), p4); b.set (auxId (a, "return"), ret); b.set (auxId (a, "lowcut"), lowcut); b.set (auxId (a, "hicut"), hicut);
}

// v: 0 punchy (909), 1 deep (808), 2 modern (Basalt + FM), 3 lo-fi (PCM / CR-78)
void buildKit (KitBuilder& b, const Genre& g, int v)
{
    const int drumModel = v == 0 ? M909 : (v == 1 ? M808 : (v == 2 ? MBAS : MCR78));
    const int hatModel = v == 0 ? M909 : (v == 1 ? M808 : (v == 2 ? MBAS : M606));
    for (int t = 0; t < 16; ++t)
    {
        int role = kRoles[t];
        if (t == PERC1) role = g.perc[0];
        if (t == PERC2) role = g.perc[1];
        if (t == PERC3) role = g.perc[2];
        int model = drumModel;
        if (role == (int) Role::ClosedHat || role == (int) Role::OpenHat || role == (int) Role::Ride || role == (int) Role::Crash) model = hatModel;
        if (role == (int) Role::Cowbell || role == (int) Role::Rim) model = v == 2 ? MBAS : M808;
        b.track (t, role, model, t == KICK2 ? juce::String ("Kick 2") : juce::String());
        b.tset (t, T_NOTE, (float) (36 + t));     // chromatic C1..D#2: the usual pad layout
    }
    // ---------------------------------------------------------------- kick
    b.tset (KICK, T_A_P1, g.kickTune);
    b.tset (KICK, T_A_P2, g.kickDrop + (v == 0 ? 0.05f : 0.0f));
    b.tset (KICK, T_A_P4, g.kickClick + (v == 0 ? 0.1f : (v == 1 ? -0.15f : 0.0f)));
    b.tset (KICK, T_A_P6, g.kickDecay + (v == 1 ? 0.12f : 0.0f));
    b.tset (KICK, T_A_P7, v == 1 ? 0.45f : 0.3f);
    b.tset (KICK, T_A_P8, v == 1 ? 0.6f : 0.4f);
    b.tset (KICK, T_A_P5, v == 2 ? 0.6f : 0.42f);
    b.tset (KICK, T_LEVEL, 0.9f);
    b.tset (KICK, T_INVERT, 0.0f);
    if (v == 2) { b.tset (KICK, T_DRV_TYPE, 1.0f); b.tset (KICK, T_DRV_AMT, 0.18f); b.tset (KICK, T_COMP, 0.25f); }
    if (v == 3)
    {
        b.tset (KICK, T_C_ON, 1.0f); b.tset (KICK, T_C_SAMPLE, 29.0f); b.tset (KICK, T_C_LEVEL, 0.75f);
        b.tset (KICK, T_A_LEVEL, 0.55f); b.tset (KICK, T_CRUSH, 0.12f);
    }
    // kick 2: long low tail used as rumble / layering, sent to the hall
    b.tset (KICK2, T_A_P1, g.kickTune - 0.08f);
    b.tset (KICK2, T_A_P2, 0.2f);
    b.tset (KICK2, T_A_P4, 0.05f);
    b.tset (KICK2, T_A_P6, 0.75f);
    b.tset (KICK2, T_A_P8, 0.7f);
    b.tset (KICK2, T_LEVEL, 0.55f);
    b.tset (KICK2, T_F_TYPE, 2.0f); b.tset (KICK2, T_F_CUTOFF, 220.0f); b.tset (KICK2, T_F_RES, 0.2f);
    b.tset (KICK2, T_SEND3, 0.55f);
    b.tset (KICK2, T_DRV_AMT, 0.25f);
    // ---------------------------------------------------------------- snare / clap / rim
    b.tset (SNARE, T_A_P1, g.snareTone);
    if (v == 2) { b.tset (SNARE, T_B_ON, 1.0f); b.tset (SNARE, T_B_LEVEL, 0.35f); b.tset (SNARE, T_B_ALGO, 1.0f); b.tset (SNARE, T_B_R1, 1.0f); b.tset (SNARE, T_B_R2, 2.76f); b.tset (SNARE, T_B_R3, 5.4f);
                  b.tset (SNARE, T_B_L2, 0.5f); b.tset (SNARE, T_B_L3, 0.6f); b.tset (SNARE, T_B_D1, 0.18f); b.tset (SNARE, T_B_D2, 0.06f); b.tset (SNARE, T_B_D3, 0.03f); }
    if (v == 3) { b.tset (SNARE, T_C_ON, 1.0f); b.tset (SNARE, T_C_SAMPLE, 30.0f); b.tset (SNARE, T_A_LEVEL, 0.5f); }
    b.tset (SNARE, T_SEND3, g.roomSend * 0.8f);
    b.tset (CLAP, T_SEND1, g.roomSend);
    b.tset (CLAP, T_LEVEL, 0.75f);
    b.tset (RIM, T_SEND2, g.echoSend);
    b.tset (RIM, T_LEVEL, 0.6f);
    // ---------------------------------------------------------------- hats
    b.tset (CH, T_A_P4, g.hatDecay);
    b.tset (OH, T_A_P4, 0.35f + g.hatDecay * 0.6f);
    if (v == 2) { b.tset (CH, T_D_ON, 1.0f); b.tset (CH, T_D_TYPE, 2.0f); b.tset (CH, T_D_FREQ, 9000.0f); b.tset (CH, T_D_LEVEL, 0.2f); b.tset (CH, T_D_DECAY, 0.04f); }
    if (v == 3) { b.tset (CH, T_CRUSH, 0.15f); b.tset (OH, T_CRUSH, 0.12f); }
    b.tset (OH, T_SEND1, g.roomSend * 0.5f);
    b.tset (RIDE, T_SEND1, g.roomSend * 0.6f);
    b.tset (CRASH, T_SEND3, 0.25f);
    // ---------------------------------------------------------------- percussion
    b.tset (SHAKER, T_SEND1, g.roomSend * 0.4f);
    for (int t : { PERC1, PERC2, PERC3 }) { b.tset (t, T_SEND1, g.roomSend * 0.7f); b.tset (t, T_LEVEL, 0.62f); }
    b.tset (BELL, T_SEND2, g.echoSend);
    b.tset (METAL, T_SEND2, g.echoSend * 1.2f);
    b.tset (METAL, T_SEND3, 0.2f);
    b.tset (FX, T_SEND2, g.echoSend);
    b.tset (FX, T_SEND4, 0.3f);
    b.tset (FX, T_LEVEL, 0.5f);
    b.tset (FX, T_A_P1, 0.75f); b.tset (FX, T_A_P2, 0.7f); b.tset (FX, T_A_P3, 0.5f);

    // ---------------------------------------------------------------- buses
    setAux (b, 0, 1, 0.35f, 0.45f, 0.8f, 0.1f, 0.55f, 250.0f, 9000.0f);           // room
    setAux (b, 1, 5, 0.42f, 0.38f, 0.7f, 0.15f, 0.5f, 400.0f, 6000.0f);           // ping pong (dotted 1/8)
    setAux (b, 2, v == 1 ? 3 : 2, 0.5f, 0.4f, 0.8f, 0.2f, 0.45f, 40.0f, 6000.0f);  // plate / hall
    setAux (b, 3, g.aux4Type, 0.25f, 0.5f, 0.5f, 0.3f, 0.5f, 300.0f, 12000.0f);
    if (g.name == "Techno" || g.name == "EBM")
    {
        // rumble: the hall only keeps the lows, and is ducked by the kick
        setAux (b, 2, 3, 0.65f, 0.2f, 0.4f, 0.0f, 0.65f, 35.0f, 260.0f);
        b.set ("duck_src", 4.0f); b.set ("duck_track", (float) (KICK + 1)); b.set ("duck_target", 2.0f);
        b.set ("duck_depth", 0.8f); b.set ("duck_release", 180.0f);
    }
    b.set (groupId (0, "comp"), 0.15f);
    b.set (groupId (2, "width"), 1.2f);
    b.set ("g_swing", g.swing);
    stdMacros (b);
    b.set ("g_master", v == 2 ? 0.6f : (v == 3 ? 0.72f : (v == 1 ? 0.74f : 0.78f)));
}


void buildPatterns (KitBuilder& b, const Genre& g, int v)
{
    // ---------------------------------------------------------------- A: main groove
    for (int t = 0; t < 16; ++t)
        b.steps (0, t, g.pat[t].isNotEmpty() ? g.pat[t] : juce::String ("................"));
    auto& seq = b.seq;
    int roles[16];
    for (int t = 0; t < 16; ++t) roles[t] = t == PERC1 ? g.perc[0] : (t == PERC2 ? g.perc[1] : (t == PERC3 ? g.perc[2] : kRoles[t]));
    juce::Random rng (0x5EED00 + g.bpm * 13 + v);

    // B: ghost groove, C: busy, H: conditional evolution
    PatternTools::variation (seq.pats[0], seq.pats[1], roles, 0.6f, 1, rng);
    PatternTools::variation (seq.pats[0], seq.pats[2], roles, 0.7f, 2, rng);
    PatternTools::variation (seq.pats[0], seq.pats[7], roles, 0.8f, 5, rng);

    // D: breakdown, no kicks, long crash at the start, wider room
    seq.pats[3] = seq.pats[0];
    PatternTools::clear (seq.pats[3].tracks[KICK]);
    PatternTools::clear (seq.pats[3].tracks[KICK2]);
    seq.pats[3].tracks[KICK].length = 16; seq.pats[3].tracks[KICK2].length = 16;
    b.steps (3, CRASH, "x...............");
    // E: build, snare roll speeding up across two bars
    seq.pats[4] = seq.pats[0];
    b.steps (4, SNARE, "x.......x.......x...x...x...x...x.x.x.x.x.x.x.x.xxxxxxxxRRRRRRRR");
    {
        auto& tp = seq.pats[4].tracks[SNARE];
        for (int s = 0; s < tp.length; ++s) tp.steps[s].vel = (uint8_t) juce::jlimit (30, 127, 40 + s * 85 / tp.length);
    }
    b.steps (4, KICK, g.pat[KICK].isNotEmpty() ? g.pat[KICK] : juce::String ("x...x...x...x..."));
    b.steps (4, OH, "................................................x...x...x...x...");
    seq.pats[4].masterLength = 64;
    // F: transition pattern, crash on the one, fill in the last beat on every 4th loop
    seq.pats[5] = seq.pats[0];
    PatternTools::makeFill (seq.pats[5], roles, 0.8f, rng);
    for (int t = 0; t < 16; ++t)
        for (int s = 0; s < seq.pats[5].tracks[t].length; ++s)
            if (seq.pats[5].tracks[t].steps[s].cond == 1) { auto& st = seq.pats[5].tracks[t].steps[s]; st.cond = 11; st.condA = 4; st.condB = 4; }
    b.steps (5, CRASH, "x...............");
    // G: skeleton, kick + hats only
    seq.pats[6] = Pattern();
    b.steps (6, KICK, g.pat[KICK].isNotEmpty() ? g.pat[KICK] : juce::String ("x...x...x...x..."));
    b.steps (6, CH, g.pat[CH].isNotEmpty() ? g.pat[CH] : juce::String ("..x...x...x...x."));
    b.steps (6, OH, g.pat[OH].isNotEmpty() ? g.pat[OH] : juce::String ("................"));
    b.steps (6, RIM, "...g......g.....");

    const char* names[8] { "Main", "Ghost", "Busy", "Break", "Build", "Fill", "Skeleton", "Evolve" };
    for (int p = 0; p < 8; ++p) seq.pats[p].name = names[p];
    for (int p = 0; p < 8; ++p)
    {
        int maxLen = 16;
        for (auto& tr : seq.pats[p].tracks) maxLen = std::max (maxLen, (int) std::round (tr.length * seqResBeats (tr.res) / 0.25));
        seq.pats[p].masterLength = std::min (kMaxSteps, maxLen);
    }
    // a simple arrangement for song mode
    const int song[][2] { { 6, 2 }, { 0, 4 }, { 1, 4 }, { 4, 1 }, { 2, 4 }, { 3, 2 }, { 5, 1 }, { 7, 4 }, { 0, 2 } };
    seq.songLength = 0;
    for (auto& e : song) { seq.song[seq.songLength].pattern = e[0]; seq.song[seq.songLength].repeats = e[1]; ++seq.songLength; }
}

std::vector<Genre> genres()
{
    std::vector<Genre> out;
    auto R = [] (Role r) { return (int) r; };
    {
        Genre g { "House", 124, 0.18f, {}, { R (Role::Conga), R (Role::Bongo), R (Role::TomHigh) }, 0.42f, 0.45f, 0.5f, 0.45f, 0.28f, 0.48f, 0.3f, 0.15f, 6 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[CLAP] = "....x.......x...";
        g.pat[RIM] = "...........g....";
        g.pat[CH] = "g.o.g.o.g.o.g.og";
        g.pat[OH] = "..x...x...x...x.";
        g.pat[SHAKER] = "o.g.o.g.o.g.o.g.";
        g.pat[PERC1] = "......o...o..o..";
        g.pat[PERC2] = "...o........o...";
        out.push_back (g);
    }
    {
        Genre g { "Tech House", 126, 0.1f, {}, { R (Role::TomLow), R (Role::Conga), R (Role::TomHigh) }, 0.36f, 0.5f, 0.45f, 0.5f, 0.22f, 0.5f, 0.2f, 0.18f, 6 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[CLAP] = "....x.......x...";
        g.pat[RIM] = "...x..x....x..x.";
        g.pat[CH] = "gogxgogxgogxgogx";
        g.pat[OH] = "..x...x...x...x.";
        g.pat[SHAKER] = "xgxgxgxgxgxgxgxg";
        g.pat[PERC1] = "..........o.....";
        g.pat[PERC3] = "...o.........?..";
        g.pat[METAL] = "......x.......?.";
        out.push_back (g);
    }
    {
        Genre g { "Techno", 132, 0.0f, {}, { R (Role::TomLow), R (Role::TomMid), R (Role::TomHigh) }, 0.32f, 0.55f, 0.55f, 0.55f, 0.2f, 0.45f, 0.18f, 0.12f, 8 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[KICK2] = "..o...o...o...o.";
        g.pat[CLAP] = "....x.......x...";
        g.pat[CH] = "goxggoxggoxggoxg";
        g.pat[OH] = "..x...x...x...x.";
        g.pat[RIDE] = "o.o.o.o.o.o.o.o.";
        g.pat[METAL] = "........x.....?.";
        g.pat[FX] = "...............?";
        out.push_back (g);
    }
    {
        Genre g { "Indie Dance", 118, 0.12f, {}, { R (Role::TomLow), R (Role::TomMid), R (Role::TomHigh) }, 0.45f, 0.4f, 0.4f, 0.35f, 0.3f, 0.55f, 0.25f, 0.2f, 7 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[SNARE] = "....x..g....x.g.";
        g.pat[CH] = "x.x.x.x.x.x.x.x.";
        g.pat[OH] = "......x.......x.";
        g.pat[SHAKER] = "gogogogogogogogo";
        g.pat[BELL] = "..o.....o..o....";
        out.push_back (g);
    }
    {
        Genre g { "Dark Disco", 115, 0.08f, {}, { R (Role::TomLow), R (Role::Conga), R (Role::TomHigh) }, 0.4f, 0.42f, 0.42f, 0.3f, 0.26f, 0.42f, 0.25f, 0.22f, 8 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[SNARE] = "....x.......x...";
        g.pat[CH] = "o.x.o.x.o.x.o.x.";
        g.pat[OH] = "..x...x...x...x.";
        g.pat[PERC1] = "......x.......x.";
        g.pat[PERC2] = "..o..o....o..o..";
        g.pat[METAL] = "...........g....";
        out.push_back (g);
    }
    {
        Genre g { "Acid", 128, 0.05f, {}, { R (Role::TomLow), R (Role::TomMid), R (Role::TomHigh) }, 0.38f, 0.48f, 0.5f, 0.55f, 0.24f, 0.5f, 0.2f, 0.2f, 7 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[CLAP] = "....x.......x...";
        g.pat[RIM] = "...x..x...x..x..";
        g.pat[CH] = "oxoxoxoxoxoxoxox";
        g.pat[OH] = "..x...x...x...x.";
        g.pat[BELL] = "......x......x..";
        out.push_back (g);
    }
    {
        Genre g { "Electro", 125, 0.0f, {}, { R (Role::TomLow), R (Role::TomMid), R (Role::TomHigh) }, 0.3f, 0.65f, 0.35f, 0.25f, 0.25f, 0.5f, 0.15f, 0.25f, 9 };
        g.pat[KICK] = "x.....x.x.....x.";
        g.pat[SNARE] = "....x.......x...";
        g.pat[CLAP] = "....x.......x..g";
        g.pat[CH] = "x.x.x.x.x.x.x.x.";
        g.pat[BELL] = "......x.......x.";
        g.pat[PERC3] = "...........x.x..";
        g.pat[FX] = "...x............";
        out.push_back (g);
    }
    {
        Genre g { "EBM", 122, 0.0f, {}, { R (Role::TomLow), R (Role::TomMid), R (Role::Metal) }, 0.36f, 0.5f, 0.6f, 0.6f, 0.2f, 0.4f, 0.35f, 0.1f, 11 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[SNARE] = "....x.......x...";
        g.pat[CH] = "..x...x...x...x.";
        g.pat[METAL] = "x.......x.......";
        g.pat[PERC3] = "..........x.....";
        g.pat[FX] = "......x........x";
        out.push_back (g);
    }
    {
        Genre g { "Minimal", 127, 0.12f, {}, { R (Role::Conga), R (Role::Bongo), R (Role::Rim) }, 0.4f, 0.38f, 0.4f, 0.6f, 0.18f, 0.6f, 0.22f, 0.2f, 10 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[RIM] = "..g..x....g..x..";
        g.pat[CH] = "g.o.g.o.g.o.g.o.";
        g.pat[SHAKER] = "?.?.?.?.?.?.?.?.";
        g.pat[BELL] = "......g........g";
        g.pat[METAL] = "...g.......g....";
        g.pat[PERC1] = "..........o.....";
        out.push_back (g);
    }
    return out;
}
} // namespace

void PresetManager::registerFactory()
{
    factory.push_back ({ "Init Kit", "Init", "Neutral", 124, [] (KitBuilder& b)
    {
        Genre g { "Init", 124, 0.0f, {}, { (int) Role::TomLow, (int) Role::TomMid, (int) Role::TomHigh }, 0.4f, 0.45f, 0.45f, 0.4f, 0.28f, 0.48f, 0.2f, 0.15f, 6 };
        g.pat[KICK] = "x...x...x...x...";
        g.pat[CLAP] = "....x.......x...";
        g.pat[CH] = "x.x.x.x.x.x.x.x.";
        g.pat[OH] = "..............x.";
        buildKit (b, g, 0);
        buildPatterns (b, g, 0);
    } });
    const char* character[4] { "Punchy", "Deep", "Modern", "Lo-Fi" };
    const char* suffix[4] { "909 Pressure", "808 Depth", "Basalt Engine", "Dust Machine" };
    for (const auto& g : genres())
        for (int v = 0; v < 4; ++v)
            factory.push_back ({ g.name + " / " + suffix[v], g.name, character[v], g.bpm, [g, v] (KitBuilder& b)
            {
                buildKit (b, g, v);
                buildPatterns (b, g, v);
            } });
    // Muestras Racoon: kits de muestras reales (si la biblioteca está instalada), con los patrones del género más cercano
    const auto all = genres();
    auto nearest = [&all] (const juce::StringArray& gs) -> const Genre&
    {
        static const std::pair<const char*, const char*> map[] {
            { "deep house", "House" }, { "organic house", "House" }, { "house", "House" }, { "tech house", "Tech House" }, { "techno", "Techno" },
            { "industrial", "EBM" }, { "ebm", "EBM" }, { "acid", "Acid" }, { "electro", "Electro" }, { "miami bass", "Electro" }, { "trap", "Electro" },
            { "hiphop", "Electro" }, { "boombap", "Electro" }, { "drill", "Electro" }, { "disco", "Dark Disco" }, { "nu disco", "Dark Disco" },
            { "italo", "Dark Disco" }, { "synthpop", "Indie Dance" }, { "minimal", "Minimal" }, { "afro", "Minimal" }, { "latino", "Minimal" } };
        for (auto& g : gs)
            for (auto& [k, v] : map)
                if (g == k)
                    for (auto& x : all) if (x.name == v) return x;
        for (auto& x : all) if (x.name == "Indie Dance") return x;
        return all.front();
    };
    for (const auto& k : racoon::biblio::Catalogo::get().kits)
    {
        const Genre g = nearest (k.generos);
        const auto kit = k;
        factory.push_back ({ "Muestras / " + k.nombre, "Muestras", k.generos.joinIntoString (", "), k.bpm, [g, kit] (KitBuilder& b)
        {
            buildKit (b, g, 0);
            buildPatterns (b, g, 0);
            for (auto& p : kit.pistas) b.library (p.pista, p.grupo);
            b.set ("g_bpm", (float) kit.bpm);
        } });
    }
}
} // namespace be

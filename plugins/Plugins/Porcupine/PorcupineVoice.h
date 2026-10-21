#pragma once
#include "RacoonProcessor.h"

// The singers and players of Porcupine: a choir of formant voices (soprano, alto, tenor, bass), ensemble strings and
// brass, the harmony that splits a chord into four lines, and the syllables they sing.
namespace racoon::porcupine
{
enum Section { soprano = 0, alto, tenor, bass, strings, brass, numSections };
constexpr int numChoir = 4, maxSingers = 16;

struct Range { int lo, hi; };
Range rangeOf (int section);                       // comfortable range of a choir section (MIDI notes)
juce::StringArray sectionNames();

// ------------------------------------------------------------------------------------------- vowels
struct Formants { float f[5], bw[5], gain[5]; };
// vowel 0..4 is A E I O U (in between morphs), hum 0..1 closes the mouth (Mm)
Formants formantsFor (int choirSection, float vowel, float hum, float brightness);
juce::StringArray vowelNames();

// ------------------------------------------------------------------------------------------- syllables
enum Consonant { cNone = 0, cL, cD, cN, cM, cS, cT, cK, cH };
struct Syllable { const char* text; Consonant consonant; float vowel; float hum; };
const std::vector<Syllable>& syllables();
juce::StringArray syllableNames();

// ------------------------------------------------------------------------------------------- harmony
// splits the chord (pitch classes) into soprano, alto, tenor and bass with smooth voice leading.
// prev holds the last notes (-1 when there were none); bassPc is the note the bass sings.
struct Satb { int note[numChoir] { -1, -1, -1, -1 }; bool valid() const { return note[0] >= 0; } };
// sopranoNote >= 0 fixes the top line (the melody when harmonizing)
Satb voiceLead (const int* pcs, int numPcs, int bassPc, const Satb& prev, bool open, float smooth, int sopranoNote = -1);
// the melody on top and a diatonic chord under it: returns the chord's pitch classes (3) and its bass
int harmonize (int melody, int key, bool minor, int* pcsOut, int& bassPc, int previousDegree);
juce::StringArray keyNames();

// ------------------------------------------------------------------------------------------- articulations
enum Articulation { sustained = 0, staccato, marcato, legato, crescendo, numArticulations };
juce::StringArray articulationNames();

// ------------------------------------------------------------------------------------------- one voice
// a singer group (or a string or brass line) on one note: several detuned players panned across the section
struct VoiceSettings
{
    float vowel = 0.0f, hum = 0.0f, brightness = 0.5f, dynamics = 0.7f;
    int size = 4;
    float detuneCents = 10.0f, driftCents = 4.0f, width = 0.7f, pan = 0.0f;
    float vibDepth = 20.0f, vibRate = 5.2f, vibDelay = 0.3f;
    float breath = 0.2f, attack = 0.15f, release = 0.4f;
    float glideSeconds = 0.08f;
    float scatterMs = 10.0f;
    juce::uint32 seed = 1;
};

class Voice
{
public:
    void prepare (double sampleRate);
    void start (int section, int key, int note, float velocity, Articulation, Consonant, const VoiceSettings&, bool glideFrom);
    void glideTo (int note, Consonant, float vowel, float hum);   // a new syllable or note without a new attack
    void release();
    void setVowel (float v, float hum) { vowelTarget = v; humVoice = hum; }
    void kill() { active = false; }
    void render (float* L, float* R, int n, const VoiceSettings&);
    bool active = false, gate = false;
    int section = 0, key = -1, note = 60;
    juce::uint64 age = 0;
    float vowelNow = 0.0f;
    float level() const { return env; }

private:
    double sr = 48000.0;
    struct Player { double phase = 0.0; float detune = 0.0f, pan = 0.5f, vibPhase = 0.0f, vibRate = 5.0f, driftPhase = 0.0f, driftRate = 0.1f; int delay = 0; float tilt = 0.0f; };
    Player players[maxSingers];
    int numPlayers = 1;
    double hz = 261.6, targetHz = 261.6, glideCoef = 0.0;
    float env = 0.0f, envTarget = 0.0f, attackStep = 0.0f, releaseCoef = 0.0f, accent = 0.0f, accentDecay = 0.0f;
    float velocity = 0.8f;
    Articulation art = sustained;
    int autoReleaseIn = -1;
    double age01 = 0.0;   // seconds since the start (vibrato delay, scoops)
    // formants (two channels) and the strings / brass filter
    struct Res { float a1 = 0, a2 = 0, b0 = 0, z1 = 0, z2 = 0; };
    Res res[2][5];
    float resGain[5] {};
    int coefCountdown = 0;
    dsp::SVF lp[2];
    // consonant
    Consonant consonant = cNone;
    int consLeft = 0, consTotal = 0;
    float consState[2] {};
    float vowelTarget = 0.0f, humVoice = 0.0f, drift = 0.0f;
    dsp::Rng rng;
    // a group of people is never in one point: two different allpasses open the left and the right
    float apBuf[2][2048] {};
    int apPos = 0, apDelay[2] { 180, 260 };
    void updateCoefficients (const VoiceSettings&);
    float source (Player&, double baseHz, float vib);
};
}

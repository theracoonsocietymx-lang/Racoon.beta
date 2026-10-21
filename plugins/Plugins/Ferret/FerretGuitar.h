#pragma once
#include "RacoonProcessor.h"

// The guitar itself: six strings modelled as digital waveguides (a delay line one period long with a loss filter),
// the fretboard that decides where each note is played, chord recognition and voicings, and the rhythm patterns.
namespace racoon::ferret
{
constexpr int numStrings = 6, numFrets = 24;

// ------------------------------------------------------------------------------------------- instruments
struct Model
{
    const char* name;
    float decay;        // seconds of an open low string
    float bright;       // loop filter (0 dark .. 1 bright)
    float body;         // acoustic body amount
    float bodyBright;
    float pickupHz;     // electric pickup resonance (0 acoustic)
    float pickupQ;
    float pickupPos;    // where the pickup reads the string (fraction)
    int transpose;      // baritone
    float pluckNoise;   // nylon is softer
};
const std::vector<Model>& models();
juce::StringArray modelNames();

// ------------------------------------------------------------------------------------------- articulations
enum Art { sustain = 0, palmMute, halfMute, deadNote, slide, hammerPull, vibrato, bend, harmonic, pickScrape, numArts };
juce::StringArray articulationNames();

// ------------------------------------------------------------------------------------------- one string
class StringVoice
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSeed (int s) { seedValue = s; rng.setSeed (s); }
    // a new pluck (or a legato change when `legato` is true): frequency, velocity 0..1
    void pluck (double hz, float velocity, Art, const Model&, float pickPos, float hardness, bool legato, double slideFromHz, float bendSemis);
    void choke (float ms = 15.0f);              // the hand stops the string
    void render (float* out, int n, const Model&, float vibratoDepth, float pickupPos);
    bool active() const { return energy > 1.0e-5f || excLeft > 0; }
    int fret = -1;                               // where it sounds (for the screen), -1 silent
    double offsetSemis = 0.0;                    // pitch wheel and the detune of the double take
    double currentHz() const { return sr / period; }
    float level() const { return energy; }
private:
    double sr = 48000.0;
    std::vector<float> line;
    int write = 0, mask = 0;
    double period = 100.0, targetPeriod = 100.0, glideSamples = 0.0;
    float loss = 0.99f, lp = 0.0f, bright = 0.5f, energy = 0.0f, chokeGain = 1.0f, chokeStep = 0.0f;
    double vibPhase = 0.0, bendTarget = 0.0, bendNow = 0.0, bendRate = 0.0;
    std::vector<float> exc;
    int excLeft = 0, excPos = 0;
    bool scrape = false;
    float scrapeFreq = 3000.0f, scrapeState[2] {};
    juce::Random rng;
    int seedValue = 1;
    float readFrac (double delay) const;
};

// ------------------------------------------------------------------------------------------- body and pickup
class Body
{
public:
    void prepare (double sampleRate);
    void reset();
    float process (float x, const Model&, float pickupTone);
private:
    double sr = 48000.0;
    struct Res { float b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; float g = 0; };
    Res modes[8];
    float pk1 = 0, pk2 = 0, tone = 0;
    const Model* lastModel = nullptr;
    void design (const Model&);
};

// ------------------------------------------------------------------------------------------- fretboard, chords, voicings
struct Tuning { const char* name; int notes[numStrings]; };   // low E string first, MIDI notes
const std::vector<Tuning>& tunings();

struct Voicing { int fret[numStrings] { -1, -1, -1, -1, -1, -1 }; bool valid() const; int lowest (const int* open) const; };

struct ChordInfo
{
    int root = -1, bass = -1;            // pitch classes
    int type = -1;                       // index into chordTypeNames
    int pcs[12] {}; int numPcs = 0;
    juce::String name() const;
};
juce::StringArray chordTypeNames();
ChordInfo recognise (const int* notes, int count);   // MIDI notes held

enum class Strategy { open = 0, nearHand, leastMovement, lowStrings, highStrings };
// where to play a single note: string index and fret (or -1)
std::pair<int, int> placeNote (int note, const int* open, Strategy, int handFret, const bool* busy);
// a playable chord shape: every chord tone if possible, root (or the given bass) lowest, inside 4 frets around `position`
Voicing findVoicing (const ChordInfo&, const int* open, int position, bool allowOpen);

// ------------------------------------------------------------------------------------------- rhythm patterns
// tokens per 16th: D down, U up, d ghost down, u ghost up, A accented down, x dead (muted chuck), m palm-muted down
// (low strings), '.' rest, '-' keep ringing, o single-note pick (arpeggio note)
struct Pattern { const char* style; const char* name; const char* steps; };
const std::vector<Pattern>& patterns();
juce::StringArray styleNames();               // the styles of the library + "Usuario"
const Pattern* patternFor (int style, int complexity);   // complexity 0..2
}

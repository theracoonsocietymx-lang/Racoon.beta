#pragma once
#include "RacoonProcessor.h"

// The voice tools of Chinchilla: pitch detection (YIN), a pitch-synchronous shifter that moves pitch and formants
// separately (PSOLA), the scales for tuning, a carrier synth, the vocoder and the talkbox (LPC).
namespace racoon::chinchilla
{
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    enum Type { lowpass, highpass, bandpass, peak, highShelf };
    void set (Type, double sampleRate, double hz, double q, double gainDb = 0.0);
};

// ------------------------------------------------------------------------------------------- scales
juce::StringArray scaleNames();
juce::StringArray keyNames();
void scaleMask (int scale, int key, bool* out12);          // which pitch classes are allowed
float nearestInScale (float midi, const bool* mask);        // the closest allowed note (fractional input)
juce::String noteName (int midi);

// ------------------------------------------------------------------------------------------- pitch detection
class Yin
{
public:
    void prepare (double sampleRate);
    void reset();
    // feeds full-rate samples; returns true when a new estimate is ready
    bool push (float x);
    float hz() const { return f0; }                 // 0 when unvoiced
    float clarity() const { return clar; }          // 0..1
    int decimation() const { return dec; }
private:
    double sr = 48000.0;
    int dec = 4, decCount = 0, hop = 64, hopCount = 0;
    static constexpr int win = 512, maxLag = 192;
    float buf[win + maxLag] {};
    int filled = 0;
    Biquad aa[2];
    float f0 = 0.0f, clar = 0.0f;
    float d[maxLag + 1] {};
    void analyse();
};

// ------------------------------------------------------------------------------------------- PSOLA voice
// History of the input shared by every voice (lead, harmonies, robot): a ring with the current period.
struct History
{
    std::vector<float> buf;
    int mask = 0;
    juce::int64 written = 0;                    // samples written so far (input time)
    void prepare (int size) { int n = 1; while (n < size) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; written = 0; }
    void push (float x) { buf[(size_t) (written & mask)] = x; ++written; }
    float at (double t) const                   // linear interpolation at input time t
    {
        if (t < 0.0 || t >= (double) written - 1.0) return 0.0f;
        const auto i = (juce::int64) t;
        const float f = (float) (t - (double) i);
        const float a = buf[(size_t) (i & mask)], b = buf[(size_t) ((i + 1) & mask)];
        return a + (b - a) * f;
    }
};

class PsolaVoice
{
public:
    void prepare (double sampleRate, int latency);
    void reset();
    // one output sample (delayed by latency); period in input samples (0 = unvoiced), pitch ratio, formant ratio
    float process (const History&, double period, float ratio, float formant);
private:
    double sr = 48000.0;
    int latency = 2048;
    std::vector<float> acc, wsum;
    int mask = 0;
    juce::int64 outTime = 0;                    // output samples produced
    double nextMark = 0.0;                      // next synthesis mark (output time)
    double anaMark = 0.0;                       // analysis grid (input time)
};

// ------------------------------------------------------------------------------------------- carrier synth
class CarrierSynth
{
public:
    void prepare (double sampleRate);
    void reset();
    void noteOn (int note, float vel);
    void noteOff (int note);
    void allOff();
    float process (int wave, float detuneCents, float glideMs);   // wave 0 saw, 1 square, 2 saw + square
    bool anyOn() const;
    int lowestHeld() const;
    int held (int index) const;                 // index-th held note from the bottom, -1 when none
    int heldCount() const;
private:
    double sr = 48000.0;
    struct V { int note = -1; float vel = 0.0f; double phase[2] {}; double hz = 0.0; float env = 0.0f; bool gate = false; };
    V voices[8];
    float noise = 0.0f;
    juce::uint32 rnd = 1;
    float blep (double t, double dt) const;
};

// ------------------------------------------------------------------------------------------- singer
// A voice that sings the MIDI notes it gets: a soft glottal pulse with breath, through the formants of a vowel,
// with a short glide between notes, vibrato that comes in after a moment and a consonant at the start of each
// syllable. One voice (the last note held wins, like a singer).
class Singer
{
public:
    void prepare (double sampleRate);
    void reset();
    void noteOn (int note, float vel);
    void noteOff (int note);
    // vowel: -1 changes on every syllable (a, o, e, i, u...), 0..4 fixed; size: formant scale (0.85 deep .. 1.15 light)
    float process (int vowel, float size, float breath);
    bool active() const { return env > 1.0e-4f || gate; }
private:
    double sr = 48000.0;
    int note = -1, syllable = 0, currentVowel = 0;
    bool gate = false;
    float vel = 0.0f, hz = 220.0f, targetHz = 220.0f, phase = 0.0f, vib = 0.0f, age = 0.0f, env = 0.0f, lp = 0.0f, cons = 0.0f;
    float f[3] { 800.0f, 1150.0f, 2900.0f };
    float bp[3][2] {}, consBp[2] {};
    juce::uint32 rnd = 12345;
    int held[16] {};
    int heldCount = 0;
    float noise();
};

// ------------------------------------------------------------------------------------------- vocoder
class Vocoder
{
public:
    static constexpr int bands = 20;
    void prepare (double sampleRate);
    void reset();
    float process (float voice, float carrier, float q, float sibilance, float shift);
    float bandLevel (int b) const { return env[b]; }
private:
    double sr = 48000.0;
    Biquad an[bands][2], sy[bands][2];
    Biquad sib;
    float env[bands] {};
    float lastQ = -1.0f, lastShift = -100.0f;
    float att = 0.0f, rel = 0.0f;
    void design (float q, float shift);
};

// ------------------------------------------------------------------------------------------- talkbox (LPC)
class Talkbox
{
public:
    static constexpr int order = 18, frame = 512;
    void prepare (double sampleRate);
    void reset();
    float process (float voice, float carrier, float brightness);
private:
    double sr = 48000.0;
    float buf[frame] {};
    int pos = 0;
    double coef[order + 1] {};
    float gain = 0.0f;
    float hist[order] {};
    float preState = 0.0f;
    void analyse (float brightness);
};
}

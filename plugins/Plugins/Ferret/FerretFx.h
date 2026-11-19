#pragma once
#include "RacoonProcessor.h"

// What the guitar goes through: a pedalboard (compressor, wah, overdrive before the amp; chorus, phaser, tremolo,
// delay and reverb after it), the amplifier and the cabinet with its microphones.
namespace racoon::ferret
{
// RBJ biquad, direct form II transposed
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    enum Type { lowpass, highpass, bandpass, peak, lowShelf, highShelf };
    void set (Type, double sampleRate, double hz, double q, double gainDb = 0.0);
};

// ------------------------------------------------------------------------------------------- amplifier
struct AmpModel
{
    const char* name;
    float minDb, maxDb;     // preamp gain range
    float bias;             // asymmetry (even harmonics)
    float hardness;         // 0 tube-soft .. 1 solid-state hard
    float tightHz;          // high-pass before the preamp
    float bassHz, midHz, trebleHz;
    float midQ;
    float postLp;           // the power amp and the output transformer
    float sagScale;
    bool clean;             // Directo: no clipping at all
};
const std::vector<AmpModel>& ampModels();
juce::StringArray ampNames();

class Amp
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    struct Settings { int model = 0; float gain = 0.3f, bass = 0.5f, mid = 0.5f, treble = 0.5f, presence = 0.5f, sag = 0.3f, masterDb = 0.0f; };
    void process (juce::AudioBuffer<float>&, int numSamples, const Settings&);
private:
    double sr = 48000.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    Biquad tight[2], bass[2], mid[2], treble[2], presence[2], post[2], dcb[2];
    dsp::Follower sagEnv[2];
    float preX[2] {};
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> powerSat[2];
    Settings last;
    bool designed = false;
    void design (const Settings&);
};

// ------------------------------------------------------------------------------------------- cabinet and microphones
juce::StringArray cabNames();

class Cabinet
{
public:
    void prepare (double sampleRate);
    void reset();
    struct Settings { int type = 0; float micPos = 0.3f, distance = 0.2f, ribbon = 0.0f, room = 0.1f, blend = 1.0f; };
    void process (juce::AudioBuffer<float>&, int numSamples, const Settings&);
private:
    double sr = 48000.0;
    Biquad hp[2], res[2], notch[2], pres[2], lp[2], prox[2], ribbonLp[2], ribbonLow[2];
    dsp::DelayLine room[2];
    Settings last;
    bool designed = false;
    void design (const Settings&);
};

// ------------------------------------------------------------------------------------------- pedals
class Pedals
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    struct Settings
    {
        bool comp = false; float compAmount = 0.5f;
        bool wah = false; int wahMode = 0; float wahPos = 0.5f, wahRate = 1.0f, wahQ = 0.6f;
        bool od = false; float odDrive = 0.4f, odTone = 0.5f, odLevel = 0.0f;
        bool chorus = false; float chRate = 0.8f, chDepth = 0.5f, chMix = 0.5f;
        bool phaser = false; float phRate = 0.4f, phDepth = 0.7f, phMix = 0.5f;
        bool trem = false; float trRate = 5.0f, trDepth = 0.5f;
        bool delay = false; float dlSeconds = 0.375f, dlFeedback = 0.35f, dlMix = 0.25f, dlTone = 0.5f;
        bool reverb = false; float rvSize = 0.5f, rvDamp = 0.5f, rvMix = 0.25f;
    };
    // before the amp: compressor, wah and overdrive
    void processPre (juce::AudioBuffer<float>&, int numSamples, const Settings&);
    // after the cabinet: modulation, delay, reverb
    void processPost (juce::AudioBuffer<float>&, int numSamples, const Settings&);
    float wahNow() const { return wahShown; }      // 0..1, for the screen
private:
    double sr = 48000.0;
    // compressor
    float compEnv = 0.0f, compGain = 1.0f;
    // wah
    dsp::SVF wahF[2];
    double wahPhase = 0.0;
    float wahEnv = 0.0f, wahSmooth = 0.5f, wahShown = 0.0f;
    // overdrive
    Biquad odHp[2], odMid[2], odTone[2];
    // chorus
    dsp::DelayLine chLine[2];
    double chPhase = 0.0;
    // phaser
    float apState[2][6] {};
    float phFb[2] {};
    double phPhase = 0.0;
    // tremolo
    double trPhase = 0.0;
    // delay
    dsp::DelayLine dl[2];
    float dlLp[2] {};
    dsp::Smooth dlTime;
    // reverb
    racoon::sdsp::Verb verb;
    racoon::sdsp::Adaa1<racoon::sdsp::curve::Tanh> odSat[2];
};
}

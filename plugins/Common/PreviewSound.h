#pragma once
#include <JuceHeader.h>

// The sound every Racoon plugin can make on its own, so pressing Play always plays something:
//  - synth: a small polyphonic synth (pad, keys, pluck or bass) for the plugins that write notes (Chipmunk)
//  - drums: a synthesized kit on the General MIDI drum notes (Mole, the demo beat)
//  - voice: a sung "aah / ooh" made with formants, for Chinchilla
//  - mix:   drums + bass + keys, a small song for the effects (Skunk, Hedgehog) to work on
// It runs on the audio thread and never allocates while playing.
namespace racoon
{
class PreviewSound
{
public:
    enum class Kind { synth, drums, voice, mix };
    enum class Patch { pad, keys, pluck, bass, organ, epiano, bell, supersaw, strings, marimba };
    static constexpr int numPatches = 10;

    void prepare (double sampleRate);
    void reset();
    void setKind (Kind k) { kind = k; }
    void setPatch (Patch p) { patch = p; }
    Kind getKind() const { return kind; }

    // adds what the MIDI plays into the buffer (on top of what is already there)
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int startSample, int numSamples, float gain = 1.0f);
    // the "mix" kind plays its own four-on-the-floor under the notes while this is on
    void setBeat (bool on, double bpm, double beatPosition);
    bool isSounding() const;
    // the drums with a pitch (kick, toms, congas, bongos) land on notes of this key; keyPc -1 leaves them as they are
    void setDrumKey (int keyPc, bool major) { drumKey = keyPc; drumMajor = major; }
    // Ataque, Caída, Sostenido y Relajación (seconds, seconds, 0..1, seconds): the instrument's ADSR. The synth uses it
    // instead of its patch's own envelope; the drums are shaped by it on top of their natural decay (and a note-off
    // starts the release). useEnvelope (false) goes back to the patch envelopes and untouched drums.
    void setEnvelope (float a, float d, float s, float r) { envA = a; envD = d; envS = s; envR = r; customEnv = true; }
    void useEnvelope (bool on) { customEnv = on; }
    // the envelope each synth patch had before the ADSR knobs, in the knobs' units (ms, ms, %, ms; times to -60 dB)
    static void patchEnvelope (Patch, float& attackMs, float& decayMs, float& sustainPct, float& releaseMs);

private:
    struct Voice
    {
        int note = -1, channel = 1;
        bool active = false, released = false, drum = false;
        float vel = 0.0f, phase[6] {}, freq = 440.0f, env = 0.0f, fenv = 0.0f, age = 0.0f, pitchEnv = 0.0f;
        float lp1 = 0.0f, lp2 = 0.0f, bp[3][2] {};
        float svf[2][2] {};        // two state-variable filters (left / right, or two bands on a drum)
        float pan = 0.0f;          // where a drum sits (hats a little right, ride a little left)
        juce::uint32 rng = 1;
        int drumType = 0;
        float shape = 1.0f;        // the ADSR on a drum (1 = untouched)
        bool drumReleased = false;
    };
    static constexpr int numVoices = 24;
    Voice voices[numVoices];
    double sr = 48000.0;
    int drumKey = -1;
    bool drumMajor = false;
    float tunedHz (float hz, bool rootOrFifth) const;
    Kind kind = Kind::synth;
    Patch patch = Patch::pad;
    // beat for the mix
    bool beatOn = false;
    double beatBpm = 120.0, beatPos = 0.0;
    float vibPhase = 0.0f;
    bool customEnv = false;
    float envA = 0.005f, envD = 0.5f, envS = 1.0f, envR = 0.3f;
    float drumShape (Voice&, float t);

    void noteOn (int channel, int note, float velocity);
    void noteOff (int channel, int note);
    void allOff();
    void startDrum (int gmNote, float velocity);
    void renderVoice (Voice&, float& l, float& r);
    float renderDrum (Voice&);
    static float noise (juce::uint32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 8388608.0f - 1.0f; }
};
}

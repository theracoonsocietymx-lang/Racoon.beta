#pragma once
#include "Common.h"
#include <optional>
#include <array>

namespace racoon { class Session; }

// Audio editing and analysis for clips: silence, transients, pitch to notes, key, loudness and the offline
// processes (normalize, phase, DC, gain). The detection algorithms are ported from SoundCraft
// (github.com/storytold/soundcraft, MIT/Apache-2.0): see ATRIBUCIONES.txt.
namespace racoon::audio
{
// ------------------------------------------------------------------ on buffers (what the tests drive)
float peakOf (const juce::AudioBuffer<float>&);
void applyGainDb (juce::AudioBuffer<float>&, float db);
void invertPhase (juce::AudioBuffer<float>&);
void removeDC (juce::AudioBuffer<float>&);
// the gain (linear) that brings the peak to targetDb
float normalizeGain (const juce::AudioBuffer<float>&, float targetDb);

// Strip silence: the ranges [start, end) whose peak across channels passes thresholdDb. Quiet gaps shorter than
// minGap samples do not split a region; each region is padded and overlapping ones are merged.
std::vector<juce::Range<juce::int64>> nonSilentRanges (const juce::AudioBuffer<float>&, float thresholdDb,
                                                       juce::int64 minGap, juce::int64 padBefore, juce::int64 padAfter);

// Onsets (the sample where each attack starts). Spectral flux over 24 bands an octave, with the attack edge found
// in the waveform; sensitivity 0..1 (0.5 is the default, higher finds softer hits).
std::vector<juce::int64> detectTransients (const juce::AudioBuffer<float>&, double sampleRate, float sensitivity = 0.5f);

// Monophonic pitch (YIN, de Cheveigné & Kawahara 2002): the fundamental of one frame, or nothing when unvoiced
std::optional<float> yin (const float* frame, int n, double sampleRate, float minHz = 50.0f, float maxHz = 1600.0f, float threshold = 0.15f);
struct DetectedNote { juce::int64 start, end; int pitch, velocity; };
// one voice (a sung line, a bass, a guitar note by note) to notes
std::vector<DetectedNote> audioToNotes (const float* mono, juce::int64 n, double sampleRate);

// Key: Krumhansl-Kessler profiles against the pitch-class weight of the material
struct KeyGuess
{
    int root = -1;               // 0 = Do ... 11 = Si; -1 when there is not enough to tell
    bool minor = false;
    float confidence = 0.0f;     // 0..1, how much the winner beats the second best
    juce::String name() const;   // "La menor", "Fa# mayor"
};
using Chroma = std::array<double, 12>;
KeyGuess keyFromChroma (const Chroma&);
Chroma chromaOfAudio (const juce::AudioBuffer<float>&, double sampleRate);
Chroma chromaOfNotes (const te::MidiClip&);

// Loudness (ITU-R BS.1770-4 / EBU R128): K-weighted, gated integrated loudness and the true peak at 4x
struct Loudness
{
    float peakDb = -100.0f, truePeakDb = -100.0f;
    float integratedLufs = -70.0f, shortTermMaxLufs = -70.0f;
    double seconds = 0.0;
    juce::String summary() const;    // "-9,8 LUFS integrados, pico real -0,6 dBTP"
};
Loudness measureLoudness (const juce::AudioBuffer<float>&, double sampleRate);

// ------------------------------------------------------------------ files
bool readFile (te::Engine&, const juce::File&, juce::AudioBuffer<float>&, double& sampleRate);
bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, int bits = 24);

// ------------------------------------------------------------------ on clips of the song
// the clip as it sounds (gain, pitch, warp, reverse, fades; no plugins) from its start to its end
bool renderClip (Session&, te::AudioClipBase&, juce::AudioBuffer<float>&, double& sampleRate);

enum class Process { normalize, invertPhase, removeDC, gainUp, gainDown };
// rewrites the clip's audio into a new file next to the project (the original stays on disk) and points the clip to it
juce::Result processClip (Session&, te::WaveAudioClip&, Process);

struct StripOptions { float thresholdDb = -45.0f; double minGapSeconds = 0.25, padBefore = 0.01, padAfter = 0.08; };
// cuts the clip where it is silent: the clip becomes one clip per region with sound (returns how many)
juce::Result stripSilence (Session&, te::WaveAudioClip&, const StripOptions&, int* pieces = nullptr);

// a new MIDI channel under the clip's, with the notes of the clip's melody; returns the new clip
te::MidiClip* clipToMidi (Session&, te::AudioClipBase&, juce::String* message = nullptr);

// the hits of a drum loop, each one a short file, with the beat where it starts in the song
struct Slice { juce::File file; double startBeat, lengthBeats; float peak; };
std::vector<Slice> sliceAtTransients (Session&, te::AudioClipBase&, float sensitivity, int maxSlices, juce::String* message = nullptr);

// a new Hamster channel that plays the slices: each slice on one of Hamster's 16 tracks (Hamster takes them from
// kitInbox()), and a MIDI clip that plays them where they were. The source clip is muted.
te::AudioTrack* slicesToHamster (Session&, te::AudioClipBase& source, const std::vector<Slice>&, juce::String* message = nullptr);
juce::File kitInbox();                       // Maqueta/Kits
// a guess of what a hit is, from how bright it is: "kick", "snare" or "hat"
juce::String roleOfHit (const juce::File&, te::Engine&);

// splits the clip at each hit, on its own channel (returns how many clips it made)
int splitAtTransients (Session&, te::AudioClipBase&, float sensitivity, juce::String* message = nullptr);

KeyGuess keyOfClip (Session&, te::Clip&);
}

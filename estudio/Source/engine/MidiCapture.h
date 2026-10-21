#pragma once
#include "Session.h"

// Turns what a MIDI effect plays (Chipmunk's chords, Mole's grooves, any VST3 that sends MIDI) into a MIDI clip.
// The plug-in is rendered offline: a second copy with the same settings receives the notes of the track's clips
// and a running transport, and its output notes are written into a new clip.
namespace racoon::capture
{
struct Result
{
    te::MidiClip* clip = nullptr;
    int notes = 0;
    juce::String error;
};

// the whole span of the track's MIDI clips (or 8 bars from the loop start when there are none)
te::BeatRange defaultRange (Session&, te::AudioTrack&);

// replaceSource: the clips that fed the effect are removed and the effect is switched off, so it sounds the same
Result midiToClip (Session&, te::AudioTrack&, te::Plugin& midiEffect, te::BeatRange, bool replaceSource = true);

// the core, usable without an Edit (tests): input and output positions are in samples from the start
juce::MidiMessageSequence render (juce::AudioPluginInstance&, const juce::MidiMessageSequence& input, double sampleRate,
                                  int numSamples, std::function<double (double seconds)> ppqAt, double bpm, int blockSize = 256);
}

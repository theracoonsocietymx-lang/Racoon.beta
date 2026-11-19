#pragma once
#include "Common.h"
#include <functional>

// Racoon's own sound library: drum and percussion one-shots, drum, top and percussion loops in twelve styles,
// fills, bass one-shots and transitions. Every sound is synthesized here (no third-party audio), so the
// library is written to disk the first time Racoon opens instead of travelling inside the installer.
namespace racoon::library
{
// ~/Music/Racoon/Biblioteca Racoon
juce::File folder();

// true when the library on disk was written by this version of the generator
bool isUpToDate (const juce::File& dest);

struct Result
{
    int files = 0;
    juce::String error;
};

// writes every sound into dest (replacing what was there); progress gets 0..1 and can return false to stop.
// Runs on any thread.
Result build (const juce::File& dest, std::function<bool (float)> progress = {});

// builds the library in the background when it is missing or old; onDone runs on the message thread
// (only when something was written)
void ensureAsync (std::function<void()> onDone);

// stops a build in progress (call before the app quits)
void shutdown();

// one sound of the library, written now if it is not there yet (the example block needs a few right away);
// an empty File if that path is not part of the library
juce::File ensureFile (const juce::String& relativePath);

// a voice sung by Racoon for one song (the vocal of a genre's block): written once into "17 Voces/Canciones",
// then reused. Notes: start and length in beats, MIDI pitch, vowel 0..4 (a, e, i, o, u).
struct VoiceNote { double start, length; int pitch, vowel; };
// the strongest pitch in a sound (MIDI note with cents), or -1 when it has none: what the congas of a loop sing
float dominantPitch (const juce::File&);
juce::File songVoice (const juce::String& name, double bpm, double totalBeats, const std::vector<VoiceNote>&, juce::uint32 seed);

// what is in the library, for the tests and the "about" text
int expectedFileCount();

// Efectos por género (~/Music/Racoon/Efectos por género): for every genre of Racoon, its risers, downlifters, rollers,
// reverse, sweeps, impacts, sub drops, fills, incidentals and textures, at its tempo and in its key, one folder per
// genre and one per kind of effect. Synthesized here too; ensureAsync writes them after the library. The installer
// can add recorded effects (free libraries) into the same folders.
juce::File effectsFolder();
bool effectsUpToDate (const juce::File& dest);
Result buildEffects (const juce::File& dest, std::function<bool (float)> progress = {});
int expectedEffectCount();
}

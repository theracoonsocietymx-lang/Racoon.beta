#pragma once
#include "Common.h"

// The genres Racoon can start a song in. Each one is a recipe for the 16-bar block (4 phrases of 4 bars):
// tempo, progression, and the rhythm of every channel written as steps of one bar.
//
// Steps: 16 characters = sixteenths, 32 = thirty-seconds (trap hats).
//   drums:  x hit, X accent, o ghost, - nothing
//   bass:   r root, o octave up, f fifth, t third, _ keep the note sounding, - nothing
//   chords: x chord, _ keep it sounding, - nothing
//   melody: x note, - nothing
// Each genre is its own song: its key, its progression, its melody and a voice sung for it (made by Racoon).
namespace racoon
{
struct Genre
{
    const char* id;
    const char* name;
    const char* family;
    double bpm;
    bool major;
    int key;                     // pitch class of the tonic (0 = C ... 11 = B): each genre has its own song, in its own key
    int prog[4];                 // scale degree of each bar's chord (0 = tonic)
    bool harmonic;               // minor keys: the V chord is major (the 7th of the scale goes up a half step on it)
    int color;                   // chords: 0 triads, 1 sevenths, 2 ninths, 3 sus2, 4 fifths and octaves
    const char* kick;
    const char* snare;
    const char* clap;
    const char* hat;
    const char* openHat;
    const char* rim;
    const char* extra;           // one more voice of the kit (cowbell, ride, tom...)
    int extraNote;               // its General MIDI note
    float swing;                 // how late the off sixteenths come (0..0.4 of a sixteenth)
    const char* bass;
    const char* chords;          // the rhythm of the chord stabs (Chipmunk; Ferret when the genre has guitar)
    const char* melody;
    const char* shaker;          // Mole's percussion
    const char* conga;
    const char* loop;            // the library loop under the song (Skunk)
    const char* kit;             // Hamster's kit
    bool guitar;                 // the guitar plays the rhythm (otherwise long chords in the second half)
    bool voice;                  // the example vocal (Chinchilla)
    const char* blurb;           // one line for the picker
};

const std::vector<Genre>& genres();
const Genre& defaultGenre();                       // Afro House: the block Racoon opened with before
const Genre* findGenre (const juce::String& id);
juce::StringArray genreFamilies();                 // in the order the picker shows them
// the key on the Camelot wheel ("8A" = A minor, "8B" = C major): DJs mix keys that are next to each other
juce::String camelot (int keyPc, bool major);
juce::String keyName (int keyPc, bool major);       // "La menor", "Mib mayor"
}

#pragma once
#include "engine/Common.h"

// MIDI transformations shared by the piano roll and the arrangement (the usual MIDI tools):
// each works on the notes given, or on every note of the clip when the list is empty.
namespace racoon::ui::notes
{
juce::Array<te::MidiNote*> orAll (te::MidiClip&, const juce::Array<te::MidiNote*>&);
juce::Range<double> playingRange (te::MidiClip&);                  // the clip's playing part, in content beats

void transpose (te::MidiClip&, const juce::Array<te::MidiNote*>&, int semitones, juce::UndoManager*);
// backwards in time inside the span of the notes (or of the clip when it gets all of them)
void reverse (te::MidiClip&, const juce::Array<te::MidiNote*>&, juce::UndoManager*);
// upside down: the highest note becomes the lowest
void invert (te::MidiClip&, const juce::Array<te::MidiNote*>&, juce::UndoManager*);
// every note lasts until the next one starts
void legato (te::MidiClip&, const juce::Array<te::MidiNote*>&, juce::UndoManager*);
void toggleMute (te::MidiClip&, const juce::Array<te::MidiNote*>&, juce::UndoManager*);

// Quantize the way SoundCraft does it (ported from SoundCraft's midi/ops.rs, MIT/Apache-2.0)
struct QuantizeOptions
{
    double grid = 0.25;          // in beats; a 1/16 is 0.25
    double strength = 1.0;       // 0..1, how far each note moves towards its grid line
    double swing = 0.0;          // 0..1, every second grid line is late by swing * grid / 3 (1 = triplet feel)
    double includeWithin = 1.0;  // only notes this close to their line (fraction of half a step) move; 1 = all
    double excludeWithin = 0.0;  // notes already this close stay as they are; 0 = none left alone
    bool ends = false;           // the note ends go to the grid too
    bool keepLength = true;      // when the start moves, the length stays (otherwise the end stays put)
    double random = 0.0;         // 0..1 of half a step, a random push after quantizing
    juce::int64 seed = 1;
    double offset = 0.0;         // content beat + offset = song beat: the lines (and the swing) follow the song's bars
};
double swungLine (double beat, double grid, double swing);    // the swung grid line nearest to `beat`
void quantize (te::MidiClip&, const juce::Array<te::MidiNote*>&, const QuantizeOptions&, juce::UndoManager*);

// velocities (1..127)
void velocityRamp (te::MidiClip&, const juce::Array<te::MidiNote*>&, int from, int to, juce::UndoManager*);   // along time
void velocityScale (te::MidiClip&, const juce::Array<te::MidiNote*>&, double factor, juce::UndoManager*);
void velocityLimit (te::MidiClip&, const juce::Array<te::MidiNote*>&, int lo, int hi, juce::UndoManager*);
void velocitySet (te::MidiClip&, const juce::Array<te::MidiNote*>&, int v, juce::UndoManager*);
// each note gets a random push of up to +-amount
void velocityHumanize (te::MidiClip&, const juce::Array<te::MidiNote*>&, int amount, juce::int64 seed, juce::UndoManager*);
// pulls every velocity towards the average (amount 0..1): an even, compressed performance
void velocityCompress (te::MidiClip&, const juce::Array<te::MidiNote*>&, double amount, juce::UndoManager*);

// a note that runs into the next one of the same pitch is cut where that one starts; returns how many were cut
int removeOverlaps (te::MidiClip&, const juce::Array<te::MidiNote*>&, juce::UndoManager*);

// Spanish note names, octave numbers with C3 = MIDI 60 (the most common convention in DAWs is "Do3")
juce::String pitchName (int midiNote);
const char* noteName (int pitchClass);
}

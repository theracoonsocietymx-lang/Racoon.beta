#pragma once
#include "Common.h"
#include <array>
#include <vector>

// Tarareo a rola: a hummed (or sung, or whistled) melody and a beatbox, recorded at the song's tempo, become notes.
// The melody is followed with YIN every 10 ms, cut into notes where the pitch jumps or a new attack comes, placed on
// the sixteenth grid and fitted to the key it is in; the beatbox is cut at each attack and every hit is told kick,
// snare or hat by where its energy is. With the melody comes a progression: per bar, the chord that holds its notes.
namespace racoon::tarareo
{
struct Nota { double beat = 0.0, len = 0.25; int pitch = 60; float vel = 0.8f; };
struct Golpe { double beat = 0.0; int tipo = 0; float vel = 0.8f; };     // tipo: 0 bombo, 1 caja, 2 charles
struct Tono { int root = 9; bool major = false; double score = 0.0; };

// the fundamental every hop samples (0 where there is no clear pitch); YIN with a parabolic refinement
std::vector<float> seguirTono (const float* x, int n, double sr, int hop, double minHz = 65.0, double maxHz = 1100.0);

// the melody: notes in beats from the start of the audio, quantized to sixteenths, not yet fitted to a key
std::vector<Nota> transcribir (const juce::AudioBuffer<float>& audio, double sr, double bpm);

// the key that holds the notes best (Krumhansl-Schmuckler, weighted by length)
Tono detectarTono (const std::vector<Nota>&);

// every note to the nearest note of the key (in minor the leading tone stays, it is the V chord's third);
// the melody is moved by octaves so its middle sits around `centro`
void ajustarATono (std::vector<Nota>&, const Tono&, int centro = 67);

// the beatbox: hits in beats, quantized to sixteenths
std::vector<Golpe> beatbox (const juce::AudioBuffer<float>& audio, double sr, double bpm);

// the chord degree of each of the first four bars (0 = tonic), for the block's progression
std::array<int, 4> progresion (const std::vector<Nota>&, const Tono&, bool harmonic);

juce::String nombreNota (int pitch);
} // namespace racoon::tarareo

#pragma once
#include "Common.h"
#include "Genres.h"
#include <vector>

// Modo DJ: a song dragged in is measured (tempo, key, energy) and Racoon makes a track to mix with it, at its tempo and
// in its key, with the DJ intro and outro of kick and percussion; several songs become a set ordered by the Camelot
// wheel and the tempo, the energy going up through the night.
namespace racoon::dj
{
struct Analisis
{
    juce::File file;
    juce::String titulo;
    bool ok = false;
    juce::String error;
    double segundos = 0.0;
    double bpm = 0.0, confianza = 0.0;      // confidence 0..1: how clearly the beat repeats
    double primerTiempo = 0.0;              // seconds to the first beat of the grid
    int key = 0; bool major = false; double claridadTono = 0.0;
    double lufs = -99.0, energia = 0.0;     // energy 0..1: loudness, how busy the beat is and how bright
    juce::String camelot() const;
    juce::String resumen() const;           // "128.00 BPM, 8A (La menor), energía 7/10"
};

// the tempo of a song: the onsets' autocorrelation (between 60 and 200 BPM, the human tempo preferred), then the grid
// that lines up best with the whole song, to a hundredth of a BPM; the phase is the first beat
double estimarTempo (const juce::AudioBuffer<float>&, double sampleRate, double* confianza = nullptr, double* fase = nullptr);
Analisis analizarAudio (const juce::AudioBuffer<float>&, double sampleRate);
Analisis analizar (te::Engine&, const juce::File&);

// the Camelot wheel: 1..12 and A (minor) or B (major)
int numeroCamelot (int key, bool major);
// 0 the same key, 1 a neighbour (one number up or down, or the other letter), 2 two steps (an energy boost), 3+ clashes
int distanciaCamelot (int keyA, bool majorA, int keyB, bool majorB);
// how far two tempos are for a DJ, in percent (a half or double tempo counts as the same: 87 and 174 mix)
double distanciaTempo (double a, double b);
// the order of a set: from the calmest song, each next one close on the wheel and in tempo, the energy climbing
std::vector<int> ordenarSet (const std::vector<Analisis>&);
juce::String transicion (const Analisis& a, const Analisis& b);     // "8A → 9A, +2 BPM: vecinas en la rueda"
// the genre for a track that mixes with this song: the nearest tempo among the club genres, the energy as a tie-break
const Genre& generoPara (const Analisis&);
} // namespace racoon::dj

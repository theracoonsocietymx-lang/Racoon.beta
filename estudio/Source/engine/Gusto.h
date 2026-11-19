#pragma once
#include "Common.h"

// Aleatoria que aprende tu gusto. Every time Racoon picks the sounds of a song (a new block, ALEATORIA en todos los
// canales), it notes what it chose: the kit, the bass, the pads, the guitar and its amp, the voice... When you say
// "Me gusta" or "No me gusta", each of those choices gains or loses a point, and from then on the picks lean: a sound
// with points comes up more often (up to 8 times), one without comes up less (down to a tenth). Nothing is ever
// ruled out, so the songs still surprise. The points live in one small file next to Racoon's settings, only on this computer.
namespace racoon::gusto
{
void empezar();                                      // a new round of choices (forgets the pending list)
// one of the options, leaning on the points; the choice joins the pending list as "categoría=valor"
juce::String elegir (juce::Random&, const juce::String& categoria, const juce::StringArray& opciones);
juce::StringArray elecciones();                      // the pending list of this round
void calificar (const juce::StringArray& elecciones, bool meGusta);
double puntaje (const juce::String& categoria, const juce::String& valor);
double peso (const juce::String& categoria, const juce::String& valor);   // how much more often than the rest (1 = the same)
juce::StringArray favoritos (int n);                 // "bajo: Acid 303 (+3)", the best scored first
void olvidar();                                      // all the points to zero (and the file deleted)
juce::File archivo();                                // RACOON_GUSTO overrides it (the tests use their own)
}

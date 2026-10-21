#pragma once
#include <JuceHeader.h>
#include "../Params.h"
#include "../dsp/Assets.h"

// Hamster y Muestras Racoon: cómo se vuelve una pista una muestra de la biblioteca (capas de velocidad en las ranuras,
// envolvente a la medida de la muestra, sin capas sintéticas encima) y cómo se cargan esas muestras sin normalizarlas.
namespace be
{
struct LibraryVoice
{
    bool ok = false;
    juce::String grupo, name;
    int role = -1;
    juce::StringArray slotIds;                         // ids de muestra, una por ranura
    std::vector<std::pair<int, float>> params;         // parámetro local de pista -> valor real
};

LibraryVoice libraryVoice (const juce::String& grupo);
SamplePtr loadLibrarySample (const juce::String& muestraId);    // nullptr si la biblioteca o el archivo no están
int roleForCategory (const juce::String& cat);
} // namespace be

#pragma once
#include "Common.h"
#include "Biblioteca.h"
#include <functional>

// Muestras Racoon en el estudio: la copia a Música/Racoon/Muestras Racoon (donde la encuentran los plugins en cualquier DAW),
// el kit de muestras de cada género y los nombres bonitos para el navegador.
namespace racoon::muestras
{
// copia la biblioteca a la carpeta del usuario si allá no está o es más vieja; onDone corre en el hilo de mensajes
void instalarAsync (std::function<void (int archivos)> onDone = {});
void shutdown();
// el kit de muestras de Hamster para un género de Racoon (vacío si la biblioteca no está)
juce::String kitParaGenero (const juce::String& genreId);
// nombre que se lee en el navegador para un archivo o carpeta de la biblioteca ("" si no es de ahí)
juce::String nombreVisible (const juce::File&);
}

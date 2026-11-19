#pragma once
#include <juce_core/juce_core.h>

// Spanish names for the parameters and values that come from Tracktion's own devices (modulators, volume, rack)
namespace racoon::ui
{
inline juce::String es (const juce::String& s)
{
    static const char* map[][2] {
        { "Wave", "Onda" }, { "Sync type", "Sincronía" }, { "Rate", "Velocidad" }, { "Rate Type", "Ritmo" }, { "Depth", "Cantidad" },
        { "Bipoloar", "Polaridad" }, { "Bipolar", "Polaridad" }, { "Phase", "Fase" }, { "Offset", "Centro" }, { "Gain", "Ganancia" },
        { "Attack", "Ataque" }, { "Hold", "Hold" }, { "Release", "Relajación" }, { "Low-pass Enabled", "Pasa bajos" },
        { "High-pass Enabled", "Pasa altos" }, { "Low-pass Frequency", "Frec. pasa bajos" }, { "High-pass Frequency", "Frec. pasa altos" },
        { "Num points", "Puntos" }, { "Stage zero value", "Inicio" }, { "Stage one value", "Punto 1" }, { "Stage two value", "Punto 2" },
        { "Stage three value", "Punto 3" }, { "Stage four value", "Punto 4" }, { "Stage one time", "Tiempo 1" }, { "Stage two time", "Tiempo 2" },
        { "Stage three time", "Tiempo 3" }, { "Stage four time", "Tiempo 4" }, { "Stage one curve", "Curva 1" }, { "Stage two curve", "Curva 2" },
        { "Stage three curve", "Curva 3" }, { "Stage four curve", "Curva 4" }, { "Enabled", "Encendido" },
        { "Volume", "Volumen" }, { "Pan", "Paneo" }, { "Wet level", "Efecto" }, { "Dry level", "Original" },
        // values
        { "Sine", "Seno" }, { "Triangle", "Triángulo" }, { "Saw Up", "Sierra arriba" }, { "Saw Down", "Sierra abajo" }, { "Square", "Cuadrada" },
        { "4 Steps Up", "4 pasos arriba" }, { "4 Steps Down", "4 pasos abajo" }, { "8 Steps Up", "8 pasos arriba" }, { "8 Steps Down", "8 pasos abajo" },
        { "Random", "Azar" }, { "Noise", "Ruido" }, { "Uni-polar", "Unipolar" }, { "Bi-polar", "Bipolar" }, { "Hertz", "Hz libre" },
        { "4 Bars", "4 compases" }, { "2 Bars", "2 compases" }, { "1 Bar", "1 compás" }, { "Free", "Libre" }, { "Transport", "Al transporte" },
        { "Note", "Por nota" }, { "On", "Sí" }, { "Off", "No" }, { "Centre", "Centro" }, { "Center", "Centro" },
    };
    for (auto& m : map)
        if (s == m[0]) return juce::String::fromUTF8 (m[1]);
    return s;
}
}

#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

// Jerboa's patterns: each one is a pair of shapes over one bar (4 beats). The time shape says how far back in the
// recording to read (in beats), the volume shape how loud. Shapes are formulas, evaluated per sample, so they cost
// nothing to store and never allocate.
namespace racoon::jerboa
{
enum class T   // time shapes: D(u) in beats behind "now", u = position in the bar (0..4)
{
    directo, mitad, medio, cuarto, freno, frenoCorto, frenoLargo, arranque, rev1, rev2, rev4,
    tart4, tart8, tart16, tart32, tart8t, tart16t, tart16en4, acelera, primero, rascado, rascadoLento,
    rascado4, rebob, eco, baja
};

enum class V   // volume shapes: v(u), 0..1
{
    lleno, puerta4, puerta8, puerta16, puerta8t, puerta16t, puerta8corta, puerta16larga, p332, cierra, bombeo,
    bombeoLento, abrir, cerrar, silencio4, corte, respiro, finCorto, finRebob, finBaja
};

struct Pattern { T time; V vol; const char* name; };
constexpr int patternsPerBank = 24, numBanks = 6;
constexpr double barBeats = 4.0;

inline double frac (double x) { return x - std::floor (x); }

inline double timeAt (T s, double u)
{
    auto tart = [] (double u, double q) { const double f = frac (u); return q * std::floor (f / q + 1.0e-9); };
    switch (s)
    {
        case T::directo:     return 0.0;
        case T::mitad:       return u * 0.5;                                      // half speed for the whole bar
        case T::medio:       return std::fmod (u, 2.0) * 0.5;                     // each half bar: its first beat, slowed
        case T::cuarto:      return u * 0.75;
        case T::freno:       return u < 2.0 ? 0.0 : (u - 2.0) * (u - 2.0) * 0.25;  // speed 1 -> 0 over two beats
        case T::frenoCorto:  return u < 3.0 ? 0.0 : (u - 3.0) * (u - 3.0) * 0.5;
        case T::frenoLargo:  return u * u / 8.0;
        case T::arranque:    return u < 1.0 ? u - u * u * 0.5 : 0.5;              // speed 0 -> 1 in the first beat
        case T::rev1:        return 2.0 * frac (u);                               // every beat backwards
        case T::rev2:        return 2.0 * std::fmod (u, 2.0);
        case T::rev4:        return u < 3.0 ? 0.0 : 2.0 * (u - 3.0);
        case T::tart4:       return std::floor (u);                               // same as "repeat the first beat"
        case T::tart8:       return tart (u, 0.5);
        case T::tart16:      return tart (u, 0.25);
        case T::tart32:      return tart (u, 0.125);
        case T::tart8t:      return tart (u, 1.0 / 3.0);
        case T::tart16t:     return tart (u, 1.0 / 6.0);
        case T::tart16en4:   return u < 3.0 ? 0.0 : tart (u, 0.25);
        case T::acelera:
        {
            const double q = u < 1.0 ? 0.5 : u < 2.0 ? 0.25 : u < 3.0 ? 0.125 : 0.0625;
            return u - std::fmod (u, q);                                          // the bar's first slice, shorter each beat
        }
        case T::primero:     return std::floor (u);
        case T::rascado:     return 0.1 * (1.0 - std::cos (4.0 * juce::MathConstants<double>::pi * u));
        case T::rascadoLento:return 0.1 * (1.0 - std::cos (2.0 * juce::MathConstants<double>::pi * u));
        case T::rascado4:    return u < 3.0 ? 0.0 : 0.1 * (1.0 - std::cos (4.0 * juce::MathConstants<double>::pi * u));
        case T::rebob:       return u < 3.0 ? 0.0 : 4.0 * (u - 3.0);              // rewind at speed -3 on the 4
        case T::eco:         return 1.0;
        case T::baja:        return 2.0 * std::pow (u / 4.0, 1.6);                // the speed keeps dropping
    }
    return 0.0;
}

inline double volAt (V s, double u)
{
    auto gate = [] (double u, double q, double duty) { return std::fmod (u, q) < duty * q ? 1.0 : 0.0; };
    const double f = frac (u);
    switch (s)
    {
        case V::lleno:        return 1.0;
        case V::puerta4:      return gate (u, 1.0, 0.6);
        case V::puerta8:      return gate (u, 0.5, 0.6);
        case V::puerta16:     return gate (u, 0.25, 0.6);
        case V::puerta8t:     return gate (u, 1.0 / 3.0, 0.6);
        case V::puerta16t:    return gate (u, 1.0 / 6.0, 0.6);
        case V::puerta8corta: return gate (u, 0.5, 0.3);
        case V::puerta16larga:return gate (u, 0.25, 0.9);
        case V::p332:
        {
            const int st = (int) std::floor (u * 4.0) % 16;
            static const bool on[16] { 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1 };   // 3+3+2, each hit two sixteenths
            return on[st] ? 1.0 : 0.0;
        }
        case V::cierra:       return u < 2.0 ? gate (u, 0.5, 0.6) : u < 3.0 ? gate (u, 0.25, 0.6) : gate (u, 0.125, 0.6);
        case V::bombeo:       return 1.0 - 0.85 * std::exp (-f / 0.12);
        case V::bombeoLento:  return 1.0 - 0.7 * std::exp (-std::fmod (u, 2.0) / 0.24);
        case V::abrir:        return (u / 4.0) * (u / 4.0);
        case V::cerrar:       return 1.0 - (u / 4.0) * (u / 4.0);
        case V::silencio4:    return u < 3.0 ? 1.0 : 0.0;
        case V::corte:        return f < 0.875 ? 1.0 : 0.0;
        case V::respiro:
        {
            const int beat = (int) u;
            if (beat == 1 || beat == 3) { const double sn = std::sin (juce::MathConstants<double>::pi * f); return 1.0 - 0.65 * sn * sn; }
            return 1.0;
        }
        case V::finCorto:     return u > 3.875 ? (4.0 - u) / 0.125 : 1.0;           // a tape stop fades out its last 1/8
        case V::finRebob:     return u > 3.75 ? (4.0 - u) / 0.25 : 1.0;
        case V::finBaja:      return u < 3.0 ? 1.0 : 4.0 - u;
    }
    return 1.0;
}

// the deepest a time shape reads (beats), for the history size and the display
inline double timeDepth (T s)
{
    double m = 0.0;
    for (int i = 0; i <= 400; ++i) m = std::max (m, timeAt (s, barBeats * i / 400.0));
    return m;
}

// six factory banks of 24. Slot 1 is always Directo; 2-6 bases that can play whole bars; 7-18 fills; 19-24 endings
inline const Pattern& pattern (int bank, int slot)
{
    static const Pattern banks[numBanks][patternsPerBank] = {
        {   // Inicial
            { T::directo, V::lleno, "Directo" }, { T::mitad, V::lleno, "Media velocidad" }, { T::medio, V::lleno, "Medio tiempo" },
            { T::directo, V::puerta8, "Compuerta 1/8" }, { T::directo, V::bombeo, "Bombeo" }, { T::directo, V::respiro, "Respiro" },
            { T::tart8, V::lleno, "Tartamudeo 1/8" }, { T::tart16, V::lleno, "Tartamudeo 1/16" }, { T::tart16en4, V::lleno, "Tartamudeo 1/16 en el 4" },
            { T::rev1, V::lleno, "Reversa 1/4" }, { T::rev4, V::lleno, "Reversa en el 4" }, { T::primero, V::lleno, "Repetir el primer tiempo" },
            { T::rascado4, V::lleno, "Rascado en el 4" }, { T::tart8t, V::lleno, "Tartamudeo 1/8T" }, { T::directo, V::corte, "Corte en cada tiempo" },
            { T::directo, V::p332, "Compuerta 3+3+2" }, { T::eco, V::lleno, "Eco de un tiempo" }, { T::tart4, V::lleno, "Tartamudeo 1/4" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::freno, V::finCorto, "Freno de cinta" }, { T::directo, V::silencio4, "Silencio en el 4" },
            { T::frenoLargo, V::finCorto, "Freno largo" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" } },
        {   // Build y drop
            { T::directo, V::lleno, "Directo" }, { T::directo, V::puerta8, "Compuerta 1/8" }, { T::directo, V::puerta16, "Compuerta 1/16" },
            { T::directo, V::cierra, "Compuerta que se cierra" }, { T::directo, V::bombeo, "Bombeo" }, { T::directo, V::abrir, "Abrir" },
            { T::tart4, V::lleno, "Tartamudeo 1/4" }, { T::acelera, V::lleno, "Tartamudeo que acelera" }, { T::tart16en4, V::lleno, "Tartamudeo 1/16 en el 4" },
            { T::rev4, V::lleno, "Reversa en el 4" }, { T::primero, V::lleno, "Repetir el primer tiempo" }, { T::rascado4, V::lleno, "Rascado en el 4" },
            { T::tart32, V::lleno, "Tartamudeo 1/32" }, { T::rev2, V::lleno, "Reversa de medio compás" }, { T::tart8, V::puerta16, "Compuerta 1/16 y tartamudeo 1/8" },
            { T::directo, V::corte, "Corte en cada tiempo" }, { T::tart16t, V::lleno, "Tartamudeo 1/16T" }, { T::tart4, V::abrir, "Abrir y tartamudeo 1/4" },
            { T::freno, V::finCorto, "Freno de cinta" }, { T::baja, V::finBaja, "Media velocidad que cae" }, { T::directo, V::silencio4, "Silencio en el 4" },
            { T::frenoLargo, V::finCorto, "Freno largo" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" } },
        {   // Medio tiempo
            { T::directo, V::lleno, "Directo" }, { T::mitad, V::lleno, "Media velocidad" }, { T::medio, V::lleno, "Medio tiempo" },
            { T::cuarto, V::lleno, "Cuarto de velocidad" }, { T::medio, V::respiro, "Medio tiempo con respiro" }, { T::mitad, V::bombeoLento, "Media velocidad con bombeo lento" },
            { T::tart8, V::lleno, "Tartamudeo 1/8" }, { T::tart16, V::lleno, "Tartamudeo 1/16" }, { T::tart16en4, V::lleno, "Tartamudeo 1/16 en el 4" },
            { T::rev1, V::lleno, "Reversa 1/4" }, { T::rev4, V::lleno, "Reversa en el 4" }, { T::primero, V::lleno, "Repetir el primer tiempo" },
            { T::rascado4, V::lleno, "Rascado en el 4" }, { T::tart8t, V::lleno, "Tartamudeo 1/8T" }, { T::directo, V::corte, "Corte en cada tiempo" },
            { T::directo, V::p332, "Compuerta 3+3+2" }, { T::eco, V::lleno, "Eco de un tiempo" }, { T::tart4, V::lleno, "Tartamudeo 1/4" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::freno, V::finCorto, "Freno de cinta" }, { T::directo, V::silencio4, "Silencio en el 4" },
            { T::frenoLargo, V::finCorto, "Freno largo" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" } },
        {   // Compuertas
            { T::directo, V::lleno, "Directo" }, { T::directo, V::puerta8, "Compuerta 1/8" }, { T::directo, V::puerta16, "Compuerta 1/16" },
            { T::directo, V::p332, "Compuerta 3+3+2" }, { T::directo, V::bombeo, "Bombeo" }, { T::directo, V::bombeoLento, "Bombeo lento" },
            { T::directo, V::puerta4, "Compuerta 1/4" }, { T::directo, V::puerta8t, "Compuerta 1/8T" }, { T::directo, V::puerta16t, "Compuerta 1/16T" },
            { T::directo, V::puerta8corta, "Compuerta 1/8 corta" }, { T::directo, V::puerta16larga, "Compuerta 1/16 larga" }, { T::directo, V::cierra, "Compuerta que se cierra" },
            { T::directo, V::corte, "Corte en cada tiempo" }, { T::directo, V::respiro, "Respiro" }, { T::tart8, V::puerta16, "Compuerta 1/16 y tartamudeo 1/8" },
            { T::directo, V::abrir, "Abrir" }, { T::directo, V::cerrar, "Cerrar" }, { T::tart16en4, V::puerta8, "Compuerta 1/8 y tartamudeo en el 4" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::freno, V::finCorto, "Freno de cinta" }, { T::directo, V::silencio4, "Silencio en el 4" },
            { T::frenoLargo, V::finCorto, "Freno largo" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" } },
        {   // Cinta
            { T::directo, V::lleno, "Directo" }, { T::mitad, V::lleno, "Media velocidad" }, { T::eco, V::lleno, "Eco de un tiempo" },
            { T::cuarto, V::lleno, "Cuarto de velocidad" }, { T::baja, V::finBaja, "Media velocidad que cae" }, { T::arranque, V::lleno, "Arranque de cinta" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::arranque, V::lleno, "Arranque de cinta" }, { T::rev4, V::lleno, "Reversa en el 4" },
            { T::freno, V::finCorto, "Freno de cinta" }, { T::medio, V::lleno, "Medio tiempo" }, { T::primero, V::lleno, "Repetir el primer tiempo" },
            { T::rascadoLento, V::lleno, "Rascado lento" }, { T::rev1, V::lleno, "Reversa 1/4" }, { T::tart4, V::lleno, "Tartamudeo 1/4" },
            { T::eco, V::bombeoLento, "Eco con bombeo lento" }, { T::mitad, V::respiro, "Media velocidad con respiro" }, { T::rebob, V::finRebob, "Rebobinado en el 4" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::freno, V::finCorto, "Freno de cinta" }, { T::frenoLargo, V::finCorto, "Freno largo" },
            { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" }, { T::baja, V::finBaja, "Media velocidad que cae" } },
        {   // Rascados
            { T::directo, V::lleno, "Directo" }, { T::rascado, V::lleno, "Rascado" }, { T::rascadoLento, V::lleno, "Rascado lento" },
            { T::rascado, V::puerta8, "Rascado con compuerta 1/8" }, { T::rev1, V::lleno, "Reversa 1/4" }, { T::rev2, V::lleno, "Reversa de medio compás" },
            { T::rascado4, V::lleno, "Rascado en el 4" }, { T::rascado4, V::puerta16, "Rascado en el 4 cortado" }, { T::rascadoLento, V::corte, "Rascado lento cortado" },
            { T::rev4, V::lleno, "Reversa en el 4" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::primero, V::lleno, "Repetir el primer tiempo" },
            { T::tart8, V::lleno, "Tartamudeo 1/8" }, { T::tart16, V::lleno, "Tartamudeo 1/16" }, { T::tart16en4, V::lleno, "Tartamudeo 1/16 en el 4" },
            { T::directo, V::corte, "Corte en cada tiempo" }, { T::tart8t, V::lleno, "Tartamudeo 1/8T" }, { T::eco, V::lleno, "Eco de un tiempo" },
            { T::frenoCorto, V::finCorto, "Freno corto" }, { T::freno, V::finCorto, "Freno de cinta" }, { T::directo, V::silencio4, "Silencio en el 4" },
            { T::frenoLargo, V::finCorto, "Freno largo" }, { T::rebob, V::finRebob, "Rebobinado en el 4" }, { T::arranque, V::lleno, "Arranque de cinta" } },
    };
    return banks[juce::jlimit (0, numBanks - 1, bank)][juce::jlimit (0, patternsPerBank - 1, slot)];
}
}

#include "OpossumEditor.h"
#include "I18n.h"

namespace racoon::opossum
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p), keys (p.keyboard, MidiKeyboardComponent::horizontalKeyboard)
{
    keys.setAvailableRange (12, 96);
    keys.setWantsKeyboardFocus (false);
    addChildComponent (keys);
    startTimerHz (25);
}

void Display::resized()
{
    auto r = getLocalBounds();
    keys.setBounds ({});
    r.removeFromBottom (6);
    screen = r.toFloat();
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    auto r = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    auto algoArea = r.removeFromLeft (r.getWidth() * 0.45f).reduced (4, 0);
    auto levelArea = r.removeFromLeft (r.getWidth() * 0.45f).reduced (8, 0);
    auto filtArea = r.reduced (4, 0);
    g.setFont (Fonts::label (11.5f));
    const int ai = (int) proc.value ("algo");
    const auto& A = algo (ai);

    // wiring: each operator sits one row above the one it modulates; carriers on the bottom row
    {
        g.setColour (C::ridge);
        g.drawRect (algoArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("ALGORITMO  " + upper (::racoon::i18n::tr (String::fromUTF8 (A.name)))), algoArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        int depth[numOps] {};
        for (int pass = 0; pass < numOps; ++pass)
            for (int e = 0; e < A.numEdges; ++e)
                if (A.from[e] > A.to[e]) depth[A.from[e]] = jmax (depth[A.from[e]], depth[A.to[e]] + 1);
        int maxDepth = 0; for (int o = 0; o < numOps; ++o) maxDepth = jmax (maxDepth, depth[o]);
        auto box = algoArea.reduced (10, 22).withTrimmedBottom (-10);
        const float rowH = box.getHeight() / (float) (maxDepth + 1);
        int perRow[numOps] {}, idxInRow[numOps] {};
        for (int o = 0; o < numOps; ++o) idxInRow[o] = perRow[depth[o]]++;
        auto centre = [&] (int o)
        {
            const float colW = box.getWidth() / (float) perRow[depth[o]];
            return Point<float> (box.getX() + colW * ((float) idxInRow[o] + 0.5f), box.getBottom() - rowH * ((float) depth[o] + 0.5f));
        };
        for (int e = 0; e < A.numEdges; ++e)
        {
            g.setColour (C::ridge.brighter (0.6f));
            g.drawLine (Line<float> (centre (A.from[e]), centre (A.to[e])), 1.6f);
        }
        for (int o = 0; o < numOps; ++o)
        {
            const auto c = centre (o);
            const bool heard = (A.carriers & (1 << o)) != 0;
            const float lit = jlimit (0.0f, 1.0f, std::sqrt (proc.opView[o].load()));
            auto rr = Rectangle<float> (30.0f, 22.0f).withCentre (c);
            g.setColour ((heard ? C::amber : C::cyan).withAlpha (0.35f + 0.65f * lit));
            g.fillRoundedRectangle (rr, 5.0f);
            g.setColour (C::void_);
            g.drawText (String (o + 1), rr, Justification::centred);
        }
    }

    // levels of the six operators
    {
        g.setColour (C::ridge);
        g.drawRect (levelArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("OPERADORES"), levelArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        auto bars = levelArea.reduced (10, 24).withTrimmedBottom (-12);
        const float w = bars.getWidth() / (float) numOps;
        for (int o = 0; o < numOps; ++o)
        {
            const float lvl = proc.value (Processor::opId (o, "level")) * 0.01f;
            auto b = Rectangle<float> (bars.getX() + w * (float) o + 3.0f, bars.getY(), w - 6.0f, bars.getHeight() - 14.0f);
            g.setColour (C::ridge.brighter (0.15f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour ((A.carriers & (1 << o)) ? C::amber : C::cyan);
            g.fillRoundedRectangle (b.withTop (b.getBottom() - b.getHeight() * lvl), 3.0f);
            g.setColour (C::boneDim);
            g.drawText (String (o + 1) + " x" + Processor::ratioNames()[(int) proc.value (Processor::opId (o, "ratio"))], b.withY (b.getBottom() + 1).withHeight (12).expanded (6, 0),
                        Justification::centred);
        }
    }

    // filter
    {
        g.setColour (C::ridge);
        g.drawRect (filtArea, 1.0f);
        const int type = (int) proc.value ("ftype");
        const float fc = proc.value ("cutoff");
        const float k = 2.0f - 2.0f * proc.value ("reso") * 0.01f;
        auto mag = [&] (float f)
        {
            const float w = f / fc, w2 = w * w;
            const float d1 = std::sqrt ((1.0f - w2) * (1.0f - w2) + (k * w) * (k * w));
            const float d2 = std::sqrt ((1.0f - w2) * (1.0f - w2) + 2.0f * w2);
            switch (type)
            {
                case 0: return 1.0f;
                case 1: return 1.0f / d1;
                case 2: return 1.0f / (d1 * d2);
                case 3: return k * w / d1;
                case 4: return w2 / d1;
                default:
                {
                    // the vowel: three bumps
                    static const float F[5][3] { { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
                    const float vw = proc.value ("vowel") * 0.04f;
                    const int a0 = jmin (3, (int) vw); const float t = vw - (float) a0;
                    float m = 0.05f;
                    for (int b = 0; b < 3; ++b)
                    {
                        const float ff = F[a0][b] * (1.0f - t) + F[a0 + 1][b] * t;
                        const float d = std::log2 (f / ff) * 4.0f;
                        m += std::exp (-d * d) / (float) (1 + b);
                    }
                    return m;
                }
            }
        };
        auto fr = filtArea.reduced (6, 18).withTrimmedBottom (-12);
        Path p;
        for (int x = 0; x <= 160; ++x)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) x / 160.0f);
            const float db = jlimit (-36.0f, 18.0f, Decibels::gainToDecibels (mag (f)));
            const float y = jmap (db, 18.0f, -36.0f, fr.getY(), fr.getBottom());
            const float xx = fr.getX() + fr.getWidth() * (float) x / 160.0f;
            if (x == 0) p.startNewSubPath (xx, y); else p.lineTo (xx, y);
        }
        Draw::glowPath (g, p, C::amber, 1.6f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("FILTRO  " + upper (StringArray { "Apagado", "LP 12", "LP 24", "BP", "HP 12", "Formante" }[type])), filtArea.withHeight (14).translated (6, 2), Justification::centredLeft);
    }
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (String (proc.activeVoices.load()) + " / " + String ((int) proc.value ("voices")) + " VOCES"), screen.reduced (16, 4).withHeight (14), Justification::topRight);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1040;
    s.easyHeight = 660;
    s.advancedHeight = 260;
    s.displayEasy = 300;
    s.displayFull = 240;
    auto op = [] (int o)
    {
        return std::vector<Ctl> {
            { Processor::opId (o, "ratio"), "Relación"_u, "Cuántas veces la frecuencia de la nota. Enteros: armónico; 0.5 suena una octava abajo."_u },
            { Processor::opId (o, "fine"), "Fino", "Suma a la relación: 1 + 0.41 suena metálico, inarmónico."_u },
            { Processor::opId (o, "level"), "Nivel", "Si suena, su volumen. Si modula, cuánto ensucia al que modula."_u },
            { Processor::opId (o, "fb"), "Retro", "Se modula a sí mismo: de un poco de filo a ruido áspero."_u },
            { Processor::opId (o, "wave"), "Forma", "Seno, medio seno, seno absoluto o cuarto de seno. Las tres últimas dan más armónicos."_u },
            { Processor::opId (o, "a"), "Ataque", "Envolvente de este operador: si modula, mueve el brillo en el tiempo."_u },
            { Processor::opId (o, "d"), "Caída"_u, "Tiempo en bajar al sostenido."_u },
            { Processor::opId (o, "s"), "Sostenido", "Nivel mientras mantienes la tecla."_u },
            { Processor::opId (o, "r"), "Relajación"_u, "Tiempo en apagarse al soltar."_u } };
    };
    auto join = [] (std::vector<Ctl> a, const std::vector<Ctl>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto slots = [] (int from, int count)
    {
        std::vector<Ctl> c;
        for (int k = from; k < from + count; ++k)
        {
            c.push_back ({ Processor::slotId (k, "src"), "Fuente " + String (k + 1), "Qué se mueve: un LFO, la envolvente del filtro, la velocidad, el aftertouch, la rueda, la tecla o un valor al azar por nota."_u });
            c.push_back ({ Processor::slotId (k, "dst"), "Destino " + String (k + 1), "Qué parámetro mueve."_u });
            c.push_back ({ Processor::slotId (k, "amt"), "Cantidad " + String (k + 1), "Cuánto y en qué sentido. Negativo invierte."_u });
        }
        return c;
    };
    auto lfo = [] (int k)
    {
        return std::vector<Ctl> {
            { Processor::lfoId (k, "rate"), "LFO " + String (k + 1), "Velocidad, de 0.01 a 30 Hz."_u },
            { Processor::lfoId (k, "sync"), "Tempo", "Sigue el tempo del proyecto."_u },
            { Processor::lfoId (k, "div"), "División"_u, "Duración de un ciclo cuando va a tempo. El LFO 1 es el del wobble."_u },
            { Processor::lfoId (k, "shape"), "Forma", "Seno, triángulo, sierra que baja, sierra que sube, cuadrada o S&H."_u },
            { Processor::lfoId (k, "retrig"), "Por nota", "Cada nota arranca su propio ciclo."_u } };
    };
    s.steps = { "Dale Play: un bajo gruñón de medio tiempo."_u, "Cambia de sonido con las flechas: bajos metálicos, gruñidos, campanas oscuras, leads, subs y drones."_u,
                "Mueve Mordida para ensuciarlo y Wobble para que hable al tempo."_u, "Toca con el teclado de abajo o con las letras A S D F de tu compu."_u };
    s.sections = {
        { "Sonido", {
            { "algo", "Algoritmo", "Cómo se conectan los seis operadores: quién modula a quién y cuáles se oyen."_u },
            { "mordida", "Mordida", "Cuánto ensucian los moduladores. El control principal: de limpio a feroz."_u },
            { "retro", "Retro", "Escala la retroalimentación de todos los operadores. Arriba de 150 % se vuelve áspero."_u },
            { "metal", "Metal", "Desafina los moduladores a relaciones inarmónicas: de gruñido a hierro."_u } } },
        { "Filtro", {
            { "cutoff", "Corte", "Brillo del sonido. Con Formante mueve las vocales."_u },
            { "reso", "Resonancia", "Resalta la zona del corte."_u },
            { "ftype", "Tipo", "Paso bajos, paso banda, paso altos, o Formante: el filtro que hace que el bajo hable."_u },
            { "vowel", "Vocal", "Con Formante: U, O, A, E, I. Las oscuras primero."_u } } },
        { "Movimiento", {
            { "wobble", "Wobble", "El LFO 1 abre y cierra el filtro y la mordida juntos, al tempo."_u },
            { "penv_amt", "Caída de tono", "La nota empieza este número de semitonos arriba (o abajo) y cae a su lugar."_u },
            { "penv_time", "Tiempo", "Qué tan rápido cae la afinación."_u } } },
        adsrSection(),
        { "Salida", {
            { "dist_type", "Destrucción"_u, "Tubo (cálido), Plegado (metálico), Rectificar (una octava de mugre)."_u },
            { "dist_amt", "Cantidad", "Cuánta destrucción."_u },
            { "sala_mix", "Sala", "Espacio alrededor del sonido."_u },
            { "volume", "Volumen", "Volumen final."_u } } },
        { "Op 1 · 2"_u, join (op (0), op (1)), true },
        { "Op 3 · 4"_u, join (op (2), op (3)), true },
        { "Op 5 · 6"_u, join (op (4), op (5)), true },
        { "Voces", {
            { "octave", "Octava", "Baja o sube todo. Las campanas suenan mejor una o dos octavas abajo."_u },
            { "voices", "Voces", "Notas a la vez. 1 es monofónico, para bajos."_u },
            { "legato", "Legato", "Con una voz, la nota nueva liga sin volver a atacar."_u },
            { "glide", "Glide", "Deslizamiento entre notas."_u },
            { "unison", "Unison", "Copias por nota, de 1 a 8, desafinadas entre sí."_u },
            { "detune", "Detune", "Cuánto se desafinan las copias."_u },
            { "spread", "Apertura", "Qué tan abiertas quedan las copias en estéreo."_u },
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u } }, true },
        { "Filtro +", {
            { "fenv", "Envolvente", "Cuánto abre la envolvente del filtro."_u },
            { "keytrack", "Teclado", "Las notas agudas abren más el filtro."_u },
            { "drive", "Drive", "Satura antes del filtro, de 0 a 36 dB."_u },
            { "fenv_a", "Ataque", "Envolvente del filtro: ataque."_u },
            { "fenv_d", "Caída"_u, "Envolvente del filtro: caída."_u },
            { "fenv_s", "Sostenido", "Envolvente del filtro: sostenido."_u },
            { "fenv_r", "Relajación"_u, "Envolvente del filtro: relajación."_u } }, true },
        { "LFO 1 · 2"_u, join (lfo (0), lfo (1)), true },
        { "Mod 1-3", slots (0, 3), true },
        { "Mod 4-6", slots (3, 3), true },
        { "Efectos", {
            { "dist_mix", "Mezcla", "Cuánto de la destrucción se mezcla con lo limpio."_u },
            { "mono_hz", "Graves mono", "Abajo de esta frecuencia todo queda en mono: el sub no se abre."_u },
            { "eco_mix", "Eco", "Repeticiones oscuras al tempo."_u },
            { "eco_div", "Tiempo eco", "Distancia entre repeticiones."_u },
            { "eco_fb", "Repeticiones", "Cuántas repeticiones se oyen."_u },
            { "sala_size", "Tamaño"_u, "Tamaño de la sala."_u },
            { "quality", "Calidad", "ECO limita el unison a 4 y calcula la modulación más espaciada."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

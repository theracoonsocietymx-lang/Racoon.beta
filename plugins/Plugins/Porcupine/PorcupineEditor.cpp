#include "PorcupineEditor.h"
#include "I18n.h"

namespace racoon::porcupine
{
using namespace juce;
using namespace racoon::ui;

static String noteName (int n)
{
    static const char* names[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return n < 0 ? String ("--") : String (names[n % 12]) + String (n / 12 - 1);
}

Display::Display (Processor& p) : proc (p) { startTimerHz (30); }

void Display::resized()
{
    auto r = getLocalBounds().reduced (10);
    sylArea = r.removeFromBottom (54);
    r.removeFromBottom (8);
    stageArea = r.removeFromLeft (r.getWidth() * 38 / 100);
    r.removeFromLeft (12);
    vowelArea = r.removeFromRight (r.getWidth() * 40 / 100);
    r.removeFromRight (12);
    linesArea = r;
}

Rectangle<float> Display::sylCell (int i) const
{
    const float w = (sylArea.getWidth() - 60.0f) / (float) sylSteps;
    return { sylArea.getX() + 60.0f + i * w, sylArea.getY() + 18.0f, w - 3.0f, sylArea.getHeight() - 20.0f };
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    const auto names = sectionNames();

    // the stage: the audience at the bottom, the sections in an arc
    {
        auto r = stageArea.toFloat();
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (12.0f));
        g.drawText (::racoon::i18n::tr ("ESCENARIO"), r.removeFromTop (16.0f), Justification::left);
        const float cx = r.getCentreX(), base = r.getBottom() - 8.0f, rad = jmin (r.getWidth() * 0.46f, r.getHeight() - 16.0f);
        const float depth = proc.value ("depth") * 0.01f, width = proc.value ("stagewidth") * 0.01f;
        Path arc;
        arc.addCentredArc (cx, base, rad, rad, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        g.setColour (C::ridge);
        g.strokePath (arc, PathStrokeType (1.0f));
        g.drawLine (cx - rad, base, cx + rad, base, 1.0f);
        static const char* ids[] { "sop_pan", "alt_pan", "ten_pan", "bas_pan", "str_pan", "br_pan" };
        static const char* onIds[] { "sop_on", "alt_on", "ten_on", "bas_on", "str_on", "br_on" };
        for (int s = 0; s < numSections; ++s)
        {
            const bool on = proc.value (onIds[s]) > 0.5f;
            const float pan = proc.value (ids[s]) * 0.01f * width;
            const float ringR = rad * (s < numChoir ? 0.62f : 0.9f) * (0.75f + 0.25f * depth);
            const float ang = pan * MathConstants<float>::halfPi * 0.9f;
            const Point<float> at { cx + std::sin (ang) * ringR, base - std::cos (ang) * ringR };
            const float lv = jlimit (0.0f, 1.0f, proc.sectionLevel[s].load() * 3.0f);
            const float size = 16.0f + 18.0f * lv;
            g.setColour (! on ? C::stone : (s < numChoir ? C::amber : C::cyan).withAlpha (0.35f + 0.65f * lv));
            g.fillEllipse (Rectangle<float> (size, size).withCentre (at));
            g.setColour (on ? C::bone : C::ash);
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr (upper (names[s]).substring (0, 4)), Rectangle<float> (60.0f, 14.0f).withCentre (at.translated (0.0f, size * 0.5f + 8.0f)), Justification::centred);
        }
        g.setColour (C::boneDim);
        g.setFont (Fonts::body (11.0f));
        g.drawText (::racoon::i18n::tr ("público"_u), Rectangle<float> (cx - 40.0f, base - 16.0f, 80.0f, 14.0f), Justification::centred);
    }

    // the four lines
    {
        auto r = linesArea.toFloat();
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (12.0f));
        const int mode = (int) proc.value ("mode");
        String head = upper (modeNames()[mode]);
        if (mode == harmonizeMode)
        {
            static const char* roman[] { "I", "II", "III", "IV", "V", "VI", "VII" };
            const int d = proc.degreeShown.load();
            head << "  ·  "_u << keyNames()[(int) proc.value ("key")] << ((int) proc.value ("scale") == 1 ? " menor" : " mayor");
            if (d >= 0) head << "  ·  GRADO "_u << roman[d];
        }
        g.drawText (::racoon::i18n::tr (head), r.removeFromTop (16.0f), Justification::left);
        const float rowH = r.getHeight() / 4.0f;
        for (int v = 0; v < numChoir; ++v)
        {
            auto row = r.removeFromTop (rowH).reduced (0.0f, 3.0f);
            const int n = proc.satbShown[v].load();
            g.setColour (C::stone);
            g.fillRect (row);
            g.setColour (C::boneDim);
            g.setFont (Fonts::label (13.0f));
            g.drawText (::racoon::i18n::tr (upper (names[v])), row.removeFromLeft (90.0f).reduced (8.0f, 0.0f), Justification::left);
            const auto range = rangeOf (v);
            auto bar = row.reduced (6.0f, rowH * 0.36f);
            g.setColour (C::ridge);
            g.fillRect (bar);
            if (n >= 0)
            {
                const float x = bar.getX() + bar.getWidth() * jlimit (0.0f, 1.0f, (float) (n - range.lo) / (float) (range.hi - range.lo));
                g.setColour (C::amberHot);
                g.fillEllipse (x - 6.0f, bar.getCentreY() - 6.0f, 12.0f, 12.0f);
                g.setFont (Fonts::title (16.0f));
                g.drawText (::racoon::i18n::tr (noteName (n)), Rectangle<float> (x - 40.0f, row.getY(), 80.0f, rowH * 0.36f), Justification::centred);
            }
        }
    }

    // the vowel: the shape of the voice (tenor) right now
    {
        auto r = vowelArea.toFloat();
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (12.0f));
        const float vw = proc.value ("vowel");
        const bool syl = (int) proc.value ("sylon") != 0;
        float vowel = vw, hum = proc.value ("mouth") * 0.01f;
        if (syl)
        {
            const int idx = (int) proc.value (Processor::sylId (proc.sylStepShown.load()));
            const auto& sy = syllables()[(size_t) jlimit (0, (int) syllables().size() - 1, idx)];
            vowel = sy.vowel; hum = jmax (hum, sy.hum);
        }
        g.drawText (::racoon::i18n::tr ("VOCAL: " + vowelNames()[jlimit (0, 4, roundToInt (vowel))]
                        + (hum > 0.5f ? "  (BOCA CERRADA)" : "")), r.removeFromTop (16.0f), Justification::left);
        const auto f = formantsFor (tenor, vowel, hum, 0.6f);
        Path p;
        const int pts = 160;
        for (int i = 0; i < pts; ++i)
        {
            const float hz = 100.0f * std::pow (60.0f, (float) i / (pts - 1));   // 100 Hz .. 6 kHz
            float mag = 0.0f;
            for (int k = 0; k < 5; ++k)
            {
                const float d = (hz - f.f[k]) / (f.bw[k] * 0.5f);
                mag += f.gain[k] / (1.0f + d * d);
            }
            const float db = jlimit (-60.0f, 6.0f, dsp::gainToDb (mag));
            const float x = r.getX() + r.getWidth() * (float) i / (pts - 1);
            const float y = r.getY() + r.getHeight() * (1.0f - (db + 60.0f) / 66.0f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        drawScreenGrid (g, r, 6, 4);
        Draw::glowPath (g, p, C::cyan);
        g.setColour (C::boneDim);
        g.setFont (Fonts::mono (10.0f));
        g.drawText (::racoon::i18n::tr ("100 Hz"), r.withTrimmedTop (r.getHeight() - 12.0f), Justification::left);
        g.drawText (::racoon::i18n::tr ("6 kHz"), r.withTrimmedTop (r.getHeight() - 12.0f), Justification::right);
        g.setColour (C::bone);
        g.drawText (::racoon::i18n::tr (String (proc.activeVoices.load()) + " voces  ·  "_u + upper (articulationNames()[proc.artShown.load()])),
                    r.withHeight (14.0f).translated (0.0f, 2.0f), Justification::right);
    }

    // the syllables
    {
        const int sylon = (int) proc.value ("sylon");
        const int len = (int) proc.value ("syllen");
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (12.0f));
        g.drawText (::racoon::i18n::tr (sylon == 0 ? String ("SÍLABAS APAGADAS: CANTA LA VOCAL. CLIC EN UN PASO PARA ELEGIR SÍLABA"_u)
                               : sylon == 1 ? String ("SÍLABAS: UNA POR ACORDE"_u) : String ("SÍLABAS: AL COMPÁS"_u)),
                    sylArea.getX(), sylArea.getY(), sylArea.getWidth(), 14, Justification::left);
        const int cur = proc.sylStepShown.load();
        for (int i = 0; i < sylSteps; ++i)
        {
            const auto c = sylCell (i);
            const bool inUse = i < len;
            g.setColour (inUse ? (i == cur && sylon != 0 ? C::amber.withAlpha (0.4f) : C::stoneHi) : C::stone);
            g.fillRect (c);
            g.setColour (inUse ? C::bone : C::ash);
            g.setFont (Fonts::label (15.0f));
            const int idx = (int) proc.value (Processor::sylId (i));
            g.drawText (::racoon::i18n::tr (syllableNames()[idx]), c, Justification::centred);
        }
    }
}

void Display::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < sylSteps; ++i)
        if (sylCell (i).contains (e.position))
        {
            PopupMenu m;
            const auto names = syllableNames();
            const int cur = (int) proc.value (Processor::sylId (i));
            for (int k = 0; k < names.size(); ++k)
                m.addItem (names[k], true, k == cur, [this, i, k] { proc.setRealValue (Processor::sylId (i), (float) k); repaint(); });
            m.addSeparator();
            m.addItem ("Usar " + String (i + 1) + " pasos", [this, i] { proc.setRealValue ("syllen", (float) (i + 1)); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y el coro canta los acordes del ejemplo."_u, "En Armonía eliges si reparte tus acordes, armoniza tu melodía o canta al unísono."_u, "Voz cambia la vocal y Ensamble el número de cantantes."_u, "Escribe tus acordes en la secuencia de abajo."_u };
    s.sections = {
        { "Armonía"_u, { { "mode", "Modo", "SATB reparte tu acorde en cuatro voces; armonizar pone acordes bajo tu melodía; unísono canta tus notas."_u },
                         { "key", "Tonalidad", "Para armonizar." }, { "scale", "Escala", "" }, { "spacing", "Voces", "Cerradas juntas o abiertas como en un himno." } } },
        { "Voz", { { "vowel", "Vocal", "A E I O U y todo lo de en medio." }, { "mouth", "Boca", "Cierra la boca hasta un Mm." },
                   { "sylon", "Sílabas"_u, "Canta las sílabas de la fila de abajo, una por acorde o al compás."_u },
                   { "art", "Articulación"_u, "También con las notas MIDI 24 a 28."_u } } },
        { "Expresión"_u, { { "dyn", "Dinámica"_u, "Más fuerte suena más brillante."_u }, { "attack", "Ataque", "" }, { "release", "Soltar", "" }, { "vibdepth", "Vibrato", "" } } },
        { "Ensamble", { { "size", "Cantantes", "Por sección."_u }, { "detune", "Desafinación"_u, "" }, { "width", "Ancho", "" },
                        { "str_on", "Cuerdas", "Cuerdas que doblan al coro." }, { "br_on", "Metales", "" } } },
        { "Escenario", { { "depth", "Distancia", "Más lejos: más tarde, más oscuro y con más sala."_u }, { "stagewidth", "Ancho", "" },
                         { "hall_mix", "Sala", "" }, { "outgain", "Volumen", "" } } },
        { "Secciones"_u, { { "sop_on", "Soprano", "" }, { "sop_lvl", "Vol.", "" }, { "sop_pan", "Lugar", "" }, { "alt_on", "Contralto", "" }, { "alt_lvl", "Vol.", "" },
                           { "alt_pan", "Lugar", "" }, { "ten_on", "Tenor", "" }, { "ten_lvl", "Vol.", "" }, { "ten_pan", "Lugar", "" }, { "bas_on", "Bajo", "" },
                           { "bas_lvl", "Vol.", "" }, { "bas_pan", "Lugar", "" } }, true },
        { "Cuerdas", { { "str_lvl", "Volumen", "" }, { "str_pan", "Lugar", "" }, { "str_oct", "Octava", "" }, { "str_size", "Músicos"_u, "" },
                       { "str_att", "Ataque", "" }, { "str_rel", "Soltar", "" }, { "str_bright", "Brillo", "" } }, true },
        { "Metales", { { "br_lvl", "Volumen", "" }, { "br_pan", "Lugar", "" }, { "br_oct", "Octava", "" }, { "br_size", "Músicos"_u, "" },
                       { "br_att", "Ataque", "" }, { "br_bright", "Brillo", "" } }, true },
        { "Humano", { { "drift", "Afinación"_u, "Cada cantante se mueve un poco de la nota." }, { "scatter", "Entradas", "No todos entran al mismo tiempo." },
                      { "breath", "Aire", "" }, { "vibrate", "Vib. vel.", "" }, { "vibdelay", "Vib. retraso", "" }, { "glide", "Legato", "" },
                      { "smooth", "Mov. suave", "Cuánto evitan saltar las voces."_u }, { "dynsrc", "Dinámica con"_u, "" } }, true },
        { "Vocal y sílabas"_u, { { "vmove", "Movimiento", "La vocal se mueve sola despacio." }, { "vrate", "Velocidad", "" }, { "sylrate", "Rejilla", "" },
                                 { "syllen", "Pasos", "" }, { "ks", "Keyswitch", "" } }, true },
        { "Sala y salidas", { { "hall_size", "Tamaño"_u, "" }, { "hall_damp", "Apagado", "" },
                              { "multiout", "Multisalida", "Cada sección a su salida (2 a 7) y la sala a la 8. Actívalas en tu DAW."_u } }, true },
    };
    s.width = 1240;
    s.displayEasy = 320;
    s.displayFull = 320;
    s.easyHeight = 660;
    s.advancedHeight = 200;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

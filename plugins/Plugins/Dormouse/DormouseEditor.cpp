#include "DormouseEditor.h"
#include "I18n.h"

namespace racoon::dormouse
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p), keys (p.keyboard, MidiKeyboardComponent::horizontalKeyboard)
{
    keys.setAvailableRange (24, 108);
    keys.setLowestVisibleKey (36);
    keys.setKeyWidth (14.0f);
    keys.setColour (MidiKeyboardComponent::whiteNoteColourId, C::bone);
    keys.setColour (MidiKeyboardComponent::blackNoteColourId, C::void_);
    keys.setColour (MidiKeyboardComponent::keySeparatorLineColourId, C::ridge);
    keys.setColour (MidiKeyboardComponent::keyDownOverlayColourId, C::amber);
    keys.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::amber.withAlpha (0.3f));
    keys.setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keys.setWantsKeyboardFocus (false);
    addChildComponent (keys);   // the keyboard lives in the dock at the bottom, the same in every plugin
    startTimerHz (25);
}

void Display::resized()
{
    auto r = getLocalBounds();
    keys.setBounds ({});
    r.removeFromBottom (6);
    screen = r.toFloat();
}

static float waveSample (int bank, float pos, float ph)
{
    if (bank < WaveBank::tableBanks)
    {
        const auto& wb = WaveBank::get();
        const float fpos = jlimit (0.0f, 1.0f, pos) * (float) (WaveBank::frames - 1);
        const int f = jmin (WaveBank::frames - 2, (int) fpos);
        const float ff = fpos - (float) f;
        const int i = jlimit (0, WaveBank::size - 1, (int) (ph * WaveBank::size));
        return wb.table (bank, f, 2)[i] * (1.0f - ff) + wb.table (bank, f + 1, 2)[i] * ff;
    }
    auto saw = [] (float x) { x -= std::floor (x); return 2.0f * x - 1.0f; };
    if (bank == WaveBank::tableBanks) return (saw (ph) - pos * saw (ph + 0.5f)) * 0.8f;
    const float shift = 0.5f - 0.45f * pos;
    return (saw (ph) - saw (ph + shift) + 1.0f - 2.0f * shift) * 0.7f;
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    auto r = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    const float colW = r.getWidth() / 3.0f;
    auto oscArea = r.removeFromLeft (colW).reduced (4, 0);
    auto filtArea = r.removeFromLeft (colW).reduced (8, 0);
    auto envArea = r.reduced (4, 0);
    g.setFont (Fonts::label (11.5f));

    // oscillators
    for (int o = 0; o < 2; ++o)
    {
        auto box = oscArea.removeFromTop (oscArea.getHeight() * (o == 0 ? 0.5f : 1.0f)).reduced (0, 3);
        const String id = "osc" + String (o + 1) + "_";
        const int bank = (int) proc.value (id + "bank");
        const float pos = proc.value (id + "pos") * 0.01f;
        const float lvl = proc.value (id + "level") * 0.01f;
        g.setColour (C::ridge);
        g.drawRect (box, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("OSC " + String (o + 1) + "  " + upper (WaveBank::names()[bank])), box.withHeight (14).translated (6, 2), Justification::centredLeft);
        auto wv = box.reduced (6, 18).withTrimmedBottom (-12);
        Path p;
        for (int x = 0; x <= 160; ++x)
        {
            const float ph = (float) x / 160.0f;
            const float y = wv.getCentreY() - waveSample (bank, pos, ph) * wv.getHeight() * 0.45f * jmax (0.15f, lvl);
            const float xx = wv.getX() + wv.getWidth() * ph;
            if (x == 0) p.startNewSubPath (xx, y); else p.lineTo (xx, y);
        }
        Draw::glowPath (g, p, o == 0 ? C::amber : C::cyan, 1.6f);
    }

    // filter response
    {
        g.setColour (C::ridge);
        g.drawRect (filtArea, 1.0f);
        const int type = (int) proc.value ("ftype");
        const float fc = proc.value ("cutoff");
        const float k = 2.0f - 2.0f * proc.value ("reso") * 0.01f * 0.97f;
        auto mag = [&] (float f)
        {
            const float w = f / fc, w2 = w * w;
            const float d1 = std::sqrt ((1.0f - w2) * (1.0f - w2) + (k * w) * (k * w));
            const float d2 = std::sqrt ((1.0f - w2) * (1.0f - w2) + 2.0f * w2);
            switch (type)
            {
                case 0: return 1.0f / d1;
                case 1: return 1.0f / (d1 * d2);
                case 2: return k * w / d1;
                case 3: return w2 / d1;
                default: return (w2 / d1) * (w2 / d2);
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
        g.drawText (::racoon::i18n::tr ("FILTRO  " + StringArray { "LP 12", "LP 24", "BP", "HP 12", "HP 24" }[type] + "  " + String (roundToInt (fc)) + " HZ"),
                    filtArea.withHeight (14).translated (6, 2), Justification::centredLeft);
    }

    // amp envelope and LFOs
    {
        auto envBox = envArea.removeFromTop (envArea.getHeight() * 0.6f).reduced (0, 3);
        g.setColour (C::ridge);
        g.drawRect (envBox, 1.0f);
        const float a = proc.value ("env1_a"), d = proc.value ("env1_d"), s = proc.value ("env1_s") * 0.01f, rl = proc.value ("env1_r");
        auto er = envBox.reduced (8, 20).withTrimmedBottom (-12);
        // time axis compressed so 1 ms and 60 s both fit
        auto tx = [] (float t) { return std::log1p (t * 4.0f); };
        const float total = tx (a) + tx (d) + 0.8f + tx (rl);
        auto X = [&] (float t) { return er.getX() + er.getWidth() * t / total; };
        Path p;
        p.startNewSubPath (X (0), er.getBottom());
        p.lineTo (X (tx (a)), er.getY());
        p.quadraticTo (X (tx (a) + tx (d) * 0.3f), er.getY() + er.getHeight() * (1.0f - s) * 0.9f, X (tx (a) + tx (d)), er.getBottom() - er.getHeight() * s);
        p.lineTo (X (tx (a) + tx (d) + 0.8f), er.getBottom() - er.getHeight() * s);
        p.quadraticTo (X (tx (a) + tx (d) + 0.8f + tx (rl) * 0.2f), er.getBottom(), X (total), er.getBottom());
        Draw::glowPath (g, p, C::cyan, 1.6f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("VOLUMEN  A " + String (a, a < 1 ? 2 : 1) + " S  ·  R "_u + String (rl, rl < 1 ? 2 : 1) + " S"), envBox.withHeight (14).translated (6, 2), Justification::centredLeft);

        auto lfoBox = envArea.reduced (0, 3);
        g.setColour (C::ridge);
        g.drawRect (lfoBox, 1.0f);
        const float w = (lfoBox.getWidth() - 12.0f) / (float) numLfo;
        for (int k = 0; k < numLfo; ++k)
        {
            auto cell = Rectangle<float> (lfoBox.getX() + 6.0f + w * (float) k, lfoBox.getY() + 18.0f, w - 6.0f, lfoBox.getHeight() - 24.0f);
            const float v = proc.lfoView[k].load();
            g.setColour (C::ridge.brighter (0.2f));
            g.fillRect (cell.withHeight (1.0f).withY (cell.getCentreY()));
            g.setColour (C::amber);
            g.fillEllipse (cell.getCentreX() - 3.0f, cell.getCentreY() - v * cell.getHeight() * 0.45f - 3.0f, 6.0f, 6.0f);
        }
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("LFO 1 · 2 · 3 · 4"_u), lfoBox.withHeight (14).translated (6, 2), Justification::centredLeft);
    }
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (String (proc.activeVoices.load()) + " / " + String ((int) proc.value ("voices")) + " VOCES"), screen.reduced (16, 4).withHeight (14),
                Justification::topRight);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1000;
    s.easyHeight = 640;
    s.advancedHeight = 250;
    s.displayEasy = 300;
    s.displayFull = 240;
    auto osc = [] (int o)
    {
        const String id = "osc" + String (o) + "_";
        return std::vector<Ctl> {
            { id + "bank", "Tabla", "La forma de onda. Las seis primeras son tablas que cambian con Posición; VA Sierra y VA Pulso son osciladores analógicos."_u },
            { id + "pos", "Posición"_u, "Recorre la tabla: de suave a brillante, de una vocal a otra, de un registro de órgano al siguiente."_u },
            { id + "oct", "Octava", "Sube o baja octavas completas."_u },
            { id + "semi", "Semitonos", "Afinación en semitonos. 7 es una quinta, 12 una octava."_u },
            { id + "fine", "Fino", "Afinación fina en cents. Un poco de diferencia entre osciladores hace el pad más ancho."_u },
            { id + "level", "Nivel", "Volumen de este oscilador."_u } };
    };
    auto env = [] (int k, const String& what)
    {
        return std::vector<Ctl> {
            { Processor::envId (k, "a"), "Ataque", "Tiempo en llegar al máximo, de 1 ms a 30 s. "_u + what },
            { Processor::envId (k, "d"), "Caída"_u, "Tiempo en bajar al nivel de sostenido."_u },
            { Processor::envId (k, "s"), "Sostenido", "Nivel mientras mantienes la tecla."_u },
            { Processor::envId (k, "r"), "Relajación"_u, "Tiempo en apagarse al soltar, de 5 ms a 60 s."_u } };
    };
    auto join = [] (std::vector<Ctl> a, const std::vector<Ctl>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto lfo = [] (int k)
    {
        return std::vector<Ctl> {
            { Processor::lfoId (k, "rate"), "LFO " + String (k + 1), "Velocidad, de 0.001 Hz (un ciclo cada 16 minutos) a 20 Hz."_u },
            { Processor::lfoId (k, "sync"), "Tempo", "Sigue el tempo del proyecto."_u },
            { Processor::lfoId (k, "div"), "División"_u, "Duración de un ciclo cuando va a tempo."_u },
            { Processor::lfoId (k, "shape"), "Forma", "Seno, triángulo, sierra, cuadrada, S&H (escalones al azar) o azar suave (deriva lenta)."_u },
            { Processor::lfoId (k, "phase"), "Fase", "Punto de arranque del ciclo."_u },
            { Processor::lfoId (k, "retrig"), "Por nota", "Cada nota arranca su propio ciclo. Apagado, todas las notas comparten el mismo."_u } };
    };
    auto slots = [] (int from)
    {
        std::vector<Ctl> c;
        for (int k = from; k < from + 4; ++k)
        {
            c.push_back ({ Processor::slotId (k, "src"), "Fuente " + String (k + 1), "Qué se mueve: un LFO, una envolvente, la velocidad, el aftertouch, la rueda, la tecla o un valor al azar por nota."_u });
            c.push_back ({ Processor::slotId (k, "dst"), "Destino " + String (k + 1), "Qué parámetro mueve."_u });
            c.push_back ({ Processor::slotId (k, "amt"), "Cantidad " + String (k + 1), "Cuánto y en qué sentido. Negativo invierte."_u });
        }
        return c;
    };
    s.steps = { "Dale Play y escucha el ejemplo con sonido de pads."_u, "Cambia de sonido con las flechas de arriba o gira las perillas mientras suena."_u, "En la secuencia de abajo, doble clic escribe una nota y doble clic en una nota la borra."_u, "Toca con el teclado de abajo o con las letras A S D F de tu compu."_u };
    s.sections = {
        { "Sonido", {
            { "osc1_bank", "Tabla 1", "Forma de onda del oscilador 1."_u },
            { "osc1_pos", "Posición 1"_u, "Recorre la tabla del oscilador 1."_u },
            { "osc2_bank", "Tabla 2", "Forma de onda del oscilador 2."_u },
            { "osc2_pos", "Posición 2"_u, "Recorre la tabla del oscilador 2."_u },
            { "unison", "Unison", "Voces por nota, de 1 a 8, repartidas en estéreo."_u },
            { "detune", "Detune", "Cuánto se desafinan las voces de unison entre sí."_u } } },
        { "Filtro", {
            { "cutoff", "Corte", "Brillo del pad."_u },
            { "reso", "Resonancia", "Resalta la zona del corte."_u },
            { "fenv", "Envolvente", "Cuánto abre (o cierra) la envolvente 2 el filtro en cada nota."_u } } },
        { "Forma", {
            { "env1_a", "Ataque", "Tiempo en que el pad aparece, de 1 ms a 30 s."_u },
            { "env1_r", "Relajación"_u, "Tiempo en que se apaga al soltar, de 5 ms a 60 s."_u } } },
        { "Salida", {
            { "chorus", "Chorus", "Chorus ligero para abrir el estéreo."_u },
            { "volume", "Volumen", "Volumen final."_u } } },
        { "Osc 1", osc (1), true },
        { "Osc 2", osc (2), true },
        { "Ruido y voces", {
            { "noise", "Ruido", "Ruido mezclado con los osciladores: aire, soplo."_u },
            { "noisecolor", "Color", "Negativo oscurece el ruido, positivo lo hace más fino."_u },
            { "texture", "Textura", "Granos cortos sobre los armónicos de la nota. Brillos que aparecen y desaparecen."_u },
            { "texdensity", "Densidad", "Cuántos granos por segundo."_u },
            { "voices", "Voces", "Notas a la vez, de 1 a 32. Al pasarte, la nota más vieja se apaga suave. Con 1 es monofónico y liga las notas."_u },
            { "spread", "Apertura", "Qué tan abiertas quedan las voces de unison en estéreo."_u },
            { "glide", "Glide", "Deslizamiento de afinación entre notas."_u },
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u } }, true },
        { "Filtro +", {
            { "ftype", "Tipo", "LP deja pasar graves (12 o 24 dB por octava), BP una banda, HP agudos."_u },
            { "drive", "Drive", "Satura antes del filtro, de 0 a 18 dB."_u },
            { "keytrack", "Teclado", "Las notas agudas abren más el filtro."_u },
            { "velflt", "Vel filtro", "Tocar más fuerte abre el filtro."_u },
            { "velamp", "Vel volumen", "Tocar más fuerte suena más fuerte."_u },
            { "chorusrate", "Chorus vel", "Velocidad del chorus."_u },
            { "chorusdepth", "Chorus prof", "Profundidad del chorus."_u },
            { "quality", "Calidad", "ECO limita el unison a 4 y calcula la modulación más espaciada. HQ filtra al doble de frecuencia de muestreo."_u } }, true },
        { "Env 1 · 2"_u, join (env (0, "Envolvente 1: volumen."), env (1, "Envolvente 2: filtro.")), true },
        { "Env 3 · 4"_u, join (env (2, "Envolvente 3: libre, se usa en Modulación."_u), env (3, "Envolvente 4: libre, se usa en Modulación."_u)), true },
        { "LFO 1 · 2"_u, join (lfo (0), lfo (1)), true },
        { "LFO 3 · 4"_u, join (lfo (2), lfo (3)), true },
        { "Mod 1-4", slots (0), true },
        { "Mod 5-8", slots (4), true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

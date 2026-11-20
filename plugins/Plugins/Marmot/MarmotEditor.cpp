#include "MarmotEditor.h"
#include "I18n.h"

namespace racoon::marmot
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p), keys (p.keyboard, MidiKeyboardComponent::horizontalKeyboard)
{
    keys.setAvailableRange (24, 108);
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

// the shape of one oscillator, drawn without band limiting (only for the screen)
static float shapeAt (int wave, float pw, float ph)
{
    ph -= std::floor (ph);
    switch (wave)
    {
        case waveSaw: return 2.0f * ph - 1.0f;
        case waveTri: return ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f;
        case waveSquare: pw = 0.5f; [[fallthrough]];
        default: return ph < 1.0f - pw ? -0.8f : 0.8f;
    }
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    auto r = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    const float colW = r.getWidth() / 3.0f;
    auto oscArea = r.removeFromLeft (colW).reduced (4, 0);
    auto filtArea = r.removeFromLeft (colW).reduced (8, 0);
    auto arpArea = r.reduced (4, 0);
    g.setFont (Fonts::label (11.5f));

    // oscillators: osc 2 drawn as it really sounds when synced (restarted by every cycle of osc 1)
    {
        g.setColour (C::ridge);
        g.drawRect (oscArea, 1.0f);
        const int w1 = (int) proc.value ("osc1_wave"), w2 = (int) proc.value ("osc2_wave");
        const float pw1 = proc.value ("osc1_pw") * 0.01f, pw2 = proc.value ("osc2_pw") * 0.01f;
        const bool sync = proc.value ("sync") > 0.5f;
        const float ratio = std::pow (2.0f, (proc.value ("osc2_oct") * 12.0f + proc.value ("osc2_semi") - proc.value ("osc1_oct") * 12.0f) / 12.0f);
        const float l1 = proc.value ("osc1_level") * 0.01f, l2 = proc.value ("osc2_level") * 0.01f;
        auto wv = oscArea.reduced (8, 20).withTrimmedBottom (-10);
        Path a, b;
        for (int x = 0; x <= 200; ++x)
        {
            const float t = (float) x / 200.0f * 2.0f;                // two cycles of osc 1
            const float y1 = shapeAt (w1, pw1, t) * jmax (0.15f, l1);
            const float ph2 = sync ? (t - std::floor (t)) * ratio : t * ratio;
            const float y2 = shapeAt (w2, pw2, ph2) * jmax (0.15f, l2);
            const float xx = wv.getX() + wv.getWidth() * (float) x / 200.0f;
            const float ya = wv.getY() + wv.getHeight() * 0.27f - y1 * wv.getHeight() * 0.22f;
            const float yb = wv.getY() + wv.getHeight() * 0.73f - y2 * wv.getHeight() * 0.22f;
            if (x == 0) { a.startNewSubPath (xx, ya); b.startNewSubPath (xx, yb); } else { a.lineTo (xx, ya); b.lineTo (xx, yb); }
        }
        Draw::glowPath (g, a, C::amber, 1.6f);
        Draw::glowPath (g, b, C::cyan, 1.6f);
        g.setColour (C::boneDim);
        String head = "OSC  " + upper (Processor::waveNames()[w1]) + " + " + upper (Processor::waveNames()[w2]);
        if (sync) head << "  SYNC";
        if (proc.value ("xmod") > 0.5f) head << "  XMOD";
        g.drawText (::racoon::i18n::tr (head), oscArea.withHeight (14).translated (6, 2), Justification::centredLeft);
    }

    // filter: high pass and low pass together
    {
        g.setColour (C::ridge);
        g.drawRect (filtArea, 1.0f);
        const int type = (int) proc.value ("ftype");
        const float fc = proc.value ("cutoff"), hp = proc.value ("hpf");
        const float k = 2.0f - 2.0f * proc.value ("reso") * 0.01f;
        auto mag = [&] (float f)
        {
            const float w = f / fc, w2 = w * w;
            const float d1 = std::sqrt ((1.0f - w2) * (1.0f - w2) + (k * w) * (k * w));
            const float d2 = std::sqrt ((1.0f - w2) * (1.0f - w2) + 2.0f * w2);
            float m = type == 0 ? 1.0f / d1 : type == 1 ? 1.0f / (d1 * d2) : k * w / d1;
            if (hp > 21.0f)
            {
                const float u = f / hp, u2 = u * u;
                m *= u2 / std::sqrt ((1.0f - u2) * (1.0f - u2) + 2.0f * u2);
            }
            return m;
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
        g.drawText (::racoon::i18n::tr ("FILTRO  " + StringArray { "LP 12", "LP 24", "BP" }[type] + "  " + String (roundToInt (fc)) + " HZ"),
                    filtArea.withHeight (14).translated (6, 2), Justification::centredLeft);
    }

    // arpeggiator steps and LFOs
    {
        auto arpBox = arpArea.removeFromTop (arpArea.getHeight() * 0.6f).reduced (0, 3);
        g.setColour (C::ridge);
        g.drawRect (arpBox, 1.0f);
        const bool on = proc.value ("arp_on") > 0.5f;
        const int octs = (int) proc.value ("arp_oct");
        const int step = proc.arpStepView.load();
        const int cells = 8 * octs;
        auto cr = arpBox.reduced (8, 22).withTrimmedBottom (-12);
        const float cw = cr.getWidth() / (float) cells;
        for (int c = 0; c < cells; ++c)
        {
            auto cell = Rectangle<float> (cr.getX() + cw * (float) c + 1.0f, cr.getBottom() - cr.getHeight() * (0.3f + 0.7f * (float) (c % 8) / 7.0f),
                                          cw - 2.0f, cr.getHeight() * (0.3f + 0.7f * (float) (c % 8) / 7.0f));
            g.setColour (on && step >= 0 && c == step % cells ? C::amber : C::ridge.brighter (on ? 0.4f : 0.1f));
            g.fillRect (cell);
        }
        g.setColour (C::boneDim);
        const String arpText = on ? "ARPEGIO  " + upper (Processor::arpModeNames()[(int) proc.value ("arp_mode")]) + "  "
                                        + Processor::arpDivNames()[(int) proc.value ("arp_div")]
                                  : String ("ARPEGIO APAGADO");
        g.drawText (::racoon::i18n::tr (arpText), arpBox.withHeight (14).translated (6, 2), Justification::centredLeft);

        auto lfoBox = arpArea.reduced (0, 3);
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
        g.drawText (::racoon::i18n::tr ("LFO 1 · 2"_u), lfoBox.withHeight (14).translated (6, 2), Justification::centredLeft);
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
    auto lfo = [] (int k)
    {
        return std::vector<Ctl> {
            { Processor::lfoId (k, "rate"), "LFO " + String (k + 1), "Velocidad, de 0.01 a 30 Hz."_u },
            { Processor::lfoId (k, "sync"), "Tempo", "Sigue el tempo del proyecto."_u },
            { Processor::lfoId (k, "div"), "División"_u, "Duración de un ciclo cuando va a tempo."_u },
            { Processor::lfoId (k, "shape"), "Forma", "Seno, triángulo, sierra, cuadrada, S&H (escalones al azar) o azar suave."_u },
            { Processor::lfoId (k, "delay"), "Entrada lenta", "Tarda este tiempo en llegar a su máximo después de cada nota: el vibrato que entra tarde."_u },
            { Processor::lfoId (k, "retrig"), "Por nota", "Cada nota arranca su propio ciclo. Apagado, todas comparten el mismo."_u } };
    };
    auto join = [] (std::vector<Ctl> a, const std::vector<Ctl>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto slots = [] (int from, int count)
    {
        std::vector<Ctl> c;
        for (int k = from; k < from + count; ++k)
        {
            c.push_back ({ Processor::slotId (k, "src"), "Fuente " + String (k + 1), "Qué se mueve: un LFO, la envolvente 2, la velocidad, el aftertouch, la rueda, la tecla o un valor al azar por nota."_u });
            c.push_back ({ Processor::slotId (k, "dst"), "Destino " + String (k + 1), "Qué parámetro mueve."_u });
            c.push_back ({ Processor::slotId (k, "amt"), "Cantidad " + String (k + 1), "Cuánto y en qué sentido. Negativo invierte."_u });
        }
        return c;
    };
    s.steps = { "Dale Play y escucha el ejemplo: una línea grave de lead."_u, "Cambia de sonido con las flechas de arriba: leads, plucks, stabs, arpegios y polifónicos."_u,
                "Prende el Arpegiador y deja sostenido un acorde: Marmot lo toca nota por nota al tempo."_u, "Toca con el teclado de abajo o con las letras A S D F de tu compu."_u };
    s.sections = {
        { "Sonido", {
            { "osc1_wave", "Forma 1", "Oscilador 1: sierra (brillante), pulso (hueco, con ancho), cuadrada o triángulo (suave)."_u },
            { "osc2_wave", "Forma 2", "Oscilador 2, con las mismas formas."_u },
            { "osc2_semi", "Semitonos 2", "Afinación del oscilador 2 en semitonos. 7 es una quinta, 12 una octava."_u },
            { "sync", "Sync", "El oscilador 2 vuelve a empezar con cada ciclo del 1. Al cambiar su afinación sale el sonido rasposo de los leads con sync."_u },
            { "xmod", "Cruzada", "El oscilador 2 mueve la frecuencia del 1: de un poco de grano a un gruñido metálico."_u },
            { "sub", "Sub", "Una cuadrada una octava abajo del oscilador 1. Le da peso al sonido."_u } } },
        { "Filtro", {
            { "cutoff", "Corte", "Brillo del sonido."_u },
            { "reso", "Resonancia", "Resalta la zona del corte. Arriba de 90 % el filtro empieza a silbar solo."_u },
            { "fenv", "Envolvente", "Cuánto abre (o cierra) la envolvente 2 el filtro en cada nota. Alto con caída corta es un pluck."_u },
            { "drive", "Drive", "Satura antes del filtro, de 0 a 24 dB."_u } } },
        adsrSection(),
        { "Arpegiador", {
            { "arp_on", "Arpegiador", "Toca las notas que sostienes una por una, al tempo."_u },
            { "arp_mode", "Modo", "Arriba, abajo, arriba y abajo, al azar, en el orden en que tocaste, o el acorde completo en cada paso."_u },
            { "arp_div", "División"_u, "Duración de cada paso: de negras a fusas, con tresillos."_u },
            { "arp_oct", "Octavas", "Sube el patrón 1, 2, 3 o 4 octavas antes de volver a empezar."_u } } },
        { "Salida", {
            { "chorus", "Chorus", "El chorus de ensamble: I lento, II más profundo, I + II rápido y brillante."_u },
            { "dly_mix", "Delay", "Repeticiones al tempo que rebotan de un lado al otro."_u },
            { "rev_mix", "Reverb", "Espacio alrededor del sonido."_u },
            { "volume", "Volumen", "Volumen final."_u } } },
        { "Osc 1", {
            { "osc1_wave", "Forma", "Forma del oscilador 1."_u },
            { "osc1_pw", "Ancho", "Ancho del pulso, de 5 a 95 %. 50 % es una cuadrada; más angosto suena más nasal."_u },
            { "osc1_oct", "Octava", "Sube o baja octavas completas."_u },
            { "osc1_level", "Nivel", "Volumen del oscilador 1."_u } }, true },
        { "Osc 2", {
            { "osc2_wave", "Forma", "Forma del oscilador 2."_u },
            { "osc2_pw", "Ancho", "Ancho del pulso del oscilador 2."_u },
            { "osc2_oct", "Octava", "Sube o baja octavas completas."_u },
            { "osc2_semi", "Semitonos", "Afinación en semitonos."_u },
            { "osc2_fine", "Fino", "Afinación fina en cents. Un poco de diferencia con el oscilador 1 lo hace más ancho."_u },
            { "osc2_level", "Nivel", "Volumen del oscilador 2."_u } }, true },
        { "Mezcla y voces", {
            { "noise", "Ruido", "Ruido blanco mezclado con los osciladores: aire, soplo, ataque."_u },
            { "voices", "Voces", "Notas a la vez, de 1 a 16. Con 1 es monofónico y liga las notas."_u },
            { "unison", "Unison", "Voces por nota, de 1 a 6, repartidas en estéreo."_u },
            { "detune", "Detune", "Cuánto se desafinan las voces de unison entre sí."_u },
            { "spread", "Apertura", "Qué tan abiertas quedan las voces de unison en estéreo."_u },
            { "drift", "Deriva", "Cada voz se mueve unos cents, despacio, como un sinte analógico."_u },
            { "glide", "Glide", "Deslizamiento de afinación entre notas."_u },
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u } }, true },
        { "Filtro +", {
            { "ftype", "Tipo", "LP deja pasar graves (12 o 24 dB por octava), BP solo una banda."_u },
            { "hpf", "Paso altos", "Quita graves antes del filtro principal, para que el sonido deje lugar al bajo."_u },
            { "keytrack", "Teclado", "Las notas agudas abren más el filtro."_u },
            { "velflt", "Vel filtro", "Tocar más fuerte abre el filtro."_u },
            { "velamp", "Vel volumen", "Tocar más fuerte suena más fuerte."_u },
            { "quality", "Calidad", "ECO calcula la modulación más espaciada. HQ pasa el filtro dos veces por muestra."_u } }, true },
        { "Env 2", {
            { "fenv_a", "Ataque", "Envolvente 2 (filtro): tiempo en llegar al máximo."_u },
            { "fenv_d", "Caída"_u, "Tiempo en bajar al nivel de sostenido."_u },
            { "fenv_s", "Sostenido", "Nivel mientras mantienes la tecla."_u },
            { "fenv_r", "Relajación"_u, "Tiempo en volver a cero al soltar."_u } }, true },
        { "LFO 1 · 2"_u, join (lfo (0), lfo (1)), true },
        { "Mod 1-3", slots (0, 3), true },
        { "Mod 4-6", slots (3, 3), true },
        { "Arpegio +", {
            { "arp_gate", "Duración"_u, "Cuánto suena cada paso: corto es staccato, 100 % liga un paso con el siguiente."_u },
            { "arp_hold", "Sostener", "El arpegio sigue sonando aunque sueltes las teclas. El siguiente acorde lo reemplaza."_u },
            { "arp_swing", "Swing", "Retrasa los pasos a contratiempo."_u } }, true },
        { "Efectos", {
            { "sat", "Saturación"_u, "Calienta y redondea la salida."_u },
            { "eq_low", "Graves", "Estante de graves en 220 Hz, de -12 a +12 dB."_u },
            { "eq_high", "Agudos", "Estante de agudos en 3.5 kHz, de -12 a +12 dB."_u },
            { "dly_div", "Delay tiempo", "Distancia entre repeticiones, al tempo."_u },
            { "dly_fb", "Repeticiones", "Cuántas repeticiones se oyen."_u },
            { "dly_tone", "Tono delay", "Las repeticiones se oscurecen arriba de esta frecuencia."_u },
            { "rev_size", "Tamaño"_u, "Tamaño del cuarto: de 0.4 a 6 segundos de cola."_u },
            { "rev_damp", "Amortiguación"_u, "Qué tanto se apagan los agudos en la cola."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

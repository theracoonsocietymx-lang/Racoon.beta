#include "OtterEditor.h"
#include "I18n.h"

namespace racoon::otter
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
    auto a = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    instrArea = a.removeFromLeft (a.getWidth() * 0.46f).reduced (4, 0);
    modesArea = a.removeFromLeft (a.getWidth() * 0.48f).reduced (8, 0);
    bodyArea = a.reduced (4, 0);
    stringLine = instrArea.reduced (26, 40).withTrimmedTop (10);
}

// the two markers: played (Limón) and heard (Hielo), both measured from the bridge on the right
void Display::mouseDown (const MouseEvent& e)
{
    dragging = 0;
    if (! stringLine.expanded (0, 30).contains (e.position)) return;
    auto xFor = [this] (float frac) { return stringLine.getRight() - stringLine.getWidth() * frac; };
    const float dp = std::abs (e.position.x - xFor (proc.value ("posicion") * 0.01f));
    const float dh = std::abs (e.position.x - xFor (proc.value ("escucha") * 0.01f));
    dragging = dp <= dh ? 1 : 2;
    mouseDrag (e);
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (dragging == 0) return;
    const float frac = jlimit (0.02f, 0.5f, (stringLine.getRight() - e.position.x) / stringLine.getWidth());
    proc.setRealValue (dragging == 1 ? "posicion" : "escucha", frac * 100.0f);
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    g.setFont (Fonts::label (11.5f));
    const int model = (int) proc.value ("modelo");
    const int exc = (int) proc.value ("exc");
    const int note = proc.lastNote.load();
    auto xFor = [this] (float frac) { return stringLine.getRight() - stringLine.getWidth() * frac; };

    // ---- the instrument
    {
        g.setColour (C::ridge);
        g.drawRect (instrArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("INSTRUMENTO  " + upper (::racoon::i18n::tr (Processor::modelNames()[model]))), instrArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        const float cy = stringLine.getCentreY();
        if (model != modelBar)
        {
            // nut and bridge, then the string as it moves now
            g.setColour (C::ridge.brighter (0.4f));
            g.fillRect (stringLine.getX() - 4.0f, cy - 16.0f, 3.0f, 32.0f);
            g.fillRect (stringLine.getRight() + 1.0f, cy - 16.0f, 3.0f, 32.0f);
            float peak = 1.0e-4f;
            for (auto& s : proc.shapeView) peak = jmax (peak, std::abs (s.load()));
            const float scale = jmin (40.0f, 18.0f / peak) * jmin (1.0f, peak * 40.0f);
            Path p;
            for (int k = 0; k < 64; ++k)
            {
                const float x = stringLine.getX() + stringLine.getWidth() * (float) k / 63.0f;
                const float edge = std::sin (MathConstants<float>::pi * (float) k / 63.0f);
                const float y = cy - proc.shapeView[k].load() * scale * edge;
                if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Draw::glowPath (g, p, C::amber, 1.6f);
        }
        if (model != modelString)
        {
            // the bar, with the node lines of its first three modes
            auto bar = stringLine.withHeight (18.0f).withCentre ({ stringLine.getCentreX(), model == modelBoth ? cy + 26.0f : cy });
            g.setColour (C::ridge.brighter (0.25f));
            g.fillRoundedRectangle (bar, 4.0f);
            static const float nodes[3][4] { { 0.224f, 0.776f, -1, -1 }, { 0.132f, 0.5f, 0.868f, -1 }, { 0.094f, 0.356f, 0.644f, 0.906f } };
            for (int m = 0; m < 3; ++m)
                for (float nx : nodes[m])
                    if (nx > 0.0f)
                    {
                        g.setColour (C::cyan.withAlpha (0.25f + 0.5f * jmin (1.0f, proc.modeView[m].load() * 0.5f)));
                        const float x = bar.getX() + bar.getWidth() * nx;
                        g.drawLine (x, bar.getY() + 2, x, bar.getBottom() - 2, 1.2f);
                    }
        }
        auto marker = [&] (float frac, Colour c, const char* label)
        {
            const float x = xFor (frac);
            g.setColour (c);
            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ x, stringLine.getBottom() + 14.0f }));
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (label), Rectangle<float> (90.0f, 14.0f).withCentre ({ x, stringLine.getBottom() + 28.0f }), Justification::centred);
        };
        marker (proc.value ("posicion") * 0.01f, C::amber, "toca");
        marker (proc.value ("escucha") * 0.01f, C::cyan, "escucha");
        static const char* names[12] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr (upper (::racoon::i18n::tr (Processor::exciterNames()[exc])) + "  ·  " + names[(note % 12 + 12) % 12] + " " + String (note / 12 - 1)),
                    instrArea.withTop (instrArea.getBottom() - 18).translated (6, -2), Justification::centredLeft);
    }

    // ---- the overtones of the last note on a log line
    {
        g.setColour (C::ridge);
        g.drawRect (modesArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("MODOS"), modesArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        auto line = modesArea.reduced (10, 26).withTrimmedBottom (-8);
        const float base = line.getBottom();
        g.setColour (C::ridge.brighter (0.3f));
        g.drawLine (line.getX(), base, line.getRight(), base, 1.0f);
        const auto& T = modeTable ((int) proc.value ("modos"));
        const float stretch = std::pow (2.0f, proc.value ("afinmodos") * 0.01f);
        const float B = proc.value ("rigidez") * 0.01f * 0.002f;
        for (int k = 0; k < numModes; ++k)
        {
            const float ratio = model == modelString ? (float) (k + 1) * std::sqrt (1.0f + B * (float) ((k + 1) * (k + 1)))
                                                     : 1.0f + (T.ratios[k] - 1.0f) * stretch;
            if (ratio > 64.0f) break;
            const float x = line.getX() + line.getWidth() * std::log2 (ratio) / 6.0f;
            const float e = model == modelString ? 1.0f / (float) (k + 1) : jmin (1.0f, proc.modeView[k].load() * 0.6f + 0.15f);
            g.setColour ((model == modelString ? C::amber : C::cyan).withAlpha (0.4f + 0.6f * e));
            g.drawLine (x, base, x, base - line.getHeight() * jmax (0.08f, e), 2.0f);
        }
    }

    // ---- body curve and Oscuridad
    {
        g.setColour (C::ridge);
        g.drawRect (bodyArea, 1.0f);
        const auto& Bd = body ((int) proc.value ("cuerpo"));
        const float size = proc.value ("cuerpotam") * 0.01f, mixAmt = proc.value ("cuerpomix") * 0.01f;
        const float dark = proc.value ("oscuro") * 0.01f;
        const float fcDark = 18000.0f * (1.0f - 0.92f * dark) * (1.0f - 0.92f * dark) + 600.0f;
        auto fr = bodyArea.reduced (6, 22).withTrimmedBottom (-10);
        Path pb, pd;
        for (int x = 0; x <= 140; ++x)
        {
            const float f = 40.0f * std::pow (400.0f, (float) x / 140.0f);
            float m = 1.0f - 0.5f * mixAmt;
            for (int k = 0; k < Bd.count; ++k)
            {
                const float fc = Bd.hz[k] / size, q = Bd.q[k];
                const float d = q * (f / fc - fc / f);
                m += mixAmt * 0.35f * Decibels::decibelsToGain (Bd.db[k]) / std::sqrt (1.0f + d * d);
            }
            const float db = jlimit (-24.0f, 18.0f, Decibels::gainToDecibels (m));
            const float w = f / fcDark;
            const float dd = jlimit (-24.0f, 18.0f, Decibels::gainToDecibels (1.0f / std::sqrt (1.0f + w * w * w * w)) - 10.0f * dark * jlimit (0.0f, 1.0f, std::log2 (f / 350.0f) / 2.0f));
            const float xx = fr.getX() + fr.getWidth() * (float) x / 140.0f;
            const float y1 = jmap (db, 18.0f, -24.0f, fr.getY(), fr.getBottom()), y2 = jmap (dd, 18.0f, -24.0f, fr.getY(), fr.getBottom());
            if (x == 0) { pb.startNewSubPath (xx, y1); pd.startNewSubPath (xx, y2); } else { pb.lineTo (xx, y1); pd.lineTo (xx, y2); }
        }
        Draw::glowPath (g, pb, C::amber, 1.6f);
        g.setColour (C::cyan.withAlpha (0.8f));
        g.strokePath (pd, PathStrokeType (1.2f));
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("CUERPO  " + upper (::racoon::i18n::tr (Processor::bodyNames()[(int) proc.value ("cuerpo")]))), bodyArea.withHeight (14).translated (6, 2), Justification::centredLeft);
    }
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (String (proc.activeVoices.load()) + " / " + String ((int) proc.value ("voices")) + " VOCES"), screen.reduced (16, 4).withHeight (14), Justification::topRight);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1040;
    s.easyHeight = 650;
    s.advancedHeight = 230;
    s.displayEasy = 300;
    s.displayFull = 240;
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
    auto lfo = [] (int k)
    {
        return std::vector<Ctl> {
            { Processor::lfoId (k, "rate"), "LFO " + String (k + 1), "Velocidad, de 0.01 a 20 Hz."_u },
            { Processor::lfoId (k, "sync"), "Tempo", "Sigue el tempo del proyecto."_u },
            { Processor::lfoId (k, "div"), "División"_u, "Duración de un ciclo cuando va a tempo."_u },
            { Processor::lfoId (k, "shape"), "Forma", "Seno, triángulo, sierra, cuadrada, S&H o azar suave."_u } };
    };
    s.steps = { "Dale Play y escucha el arpa de ejemplo."_u, "Cambia el Excitador a Frotar y sostén una nota grave: ahora es un arco."_u,
                "Cambia el Resonador a Barra y prueba Marimba o Campana tubular."_u, "Mueve Posición y Dureza mientras suena: es dónde y con qué tocas."_u };
    s.sections = {
        { "Instrumento", {
            { "modelo", "Resonador", "Qué vibra: una cuerda, una barra o las dos juntas."_u },
            { "modos", "Modos", "La afinación de los armónicos de la barra. Solo con Barra o Cuerda + barra."_u },
            { "exc", "Excitador", "Cómo se toca: con el dedo, con baqueta, con arco, raspando o con un imán que no deja de sonar."_u } } },
        { "Toque", {
            { "dureza", "Dureza", "Dedo o uña, baqueta suave o dura, arco liso o con resina. Más dureza, más brillo."_u },
            { "posicion", "Posición"_u, "Dónde se toca: cerca del puente suena delgado, al centro suena redondo."_u },
            { "presion", "Presión"_u, "Arco ligero suena a soplo, arco pesado raspa. Solo con Frotar."_u },
            { "arco", "Arco", "Qué tan rápido pasa el arco (o el raspador, o la fuerza del imán)."_u } } },
        { "Resonancia", {
            { "caida", "Caída"_u, "Cuánto tarda en apagarse la nota grave."_u },
            { "cuerpo", "Cuerpo", "La caja que resuena con la cuerda o la barra."_u },
            { "oscuro", "Oscuridad", "Quita brillo al final, después del cuerpo."_u } } },
        adsrSection(),
        { "Salida", {
            { "espacio", "Espacio", "Sala alrededor del instrumento."_u },
            { "volume", "Volumen", "Volumen final."_u } } },
        { "Cuerda", {
            { "agudos", "Agudos", "Cuánto más rápido se apagan los agudos que el grave."_u },
            { "escucha", "Escucha", "Dónde se escucha la cuerda o la barra."_u },
            { "rigidez", "Rigidez", "Cuerda rígida: los armónicos se estiran hacia arriba, suena a alambre."_u },
            { "tension", "Tensión"_u, "Un golpe fuerte sube la nota un instante, como una cuerda floja."_u },
            { "doble", "Segunda", "Una segunda cuerda por nota, como en los instrumentos de cuerdas dobles."_u },
            { "dobleint", "Intervalo", "A qué distancia va la segunda cuerda."_u },
            { "dobleaf", "Afinación"_u, "Un poco desafinada hace el sonido más ancho."_u } }, true },
        { "Barra", {
            { "afinmodos", "Estirar", "Negativo acerca los modos a armónicos limpios; positivo los separa."_u } }, true },
        { "Cuerpo +", {
            { "cuerpomix", "Cantidad", "Cuánto suena la caja."_u },
            { "cuerpotam", "Tamaño"_u, "Caja más grande, resonancias más graves."_u },
            { "ancho", "Ancho", "Graves a un lado, agudos al otro, como un arpa vista de frente."_u },
            { "espaciotam", "Sala", "Tamaño del espacio."_u } }, true },
        { "Simpáticas"_u, {
            { "simp", "Cantidad", "Seis cuerdas afinadas que vibran solas con lo que tocas."_u },
            { "simpraiz", "Tónica"_u, "En Racoon sigue la tonalidad de la rola."_u },
            { "simpmodo", "Afinación"_u, "Cómo se afinan las seis cuerdas."_u } }, true },
        { "Expresión"_u, {
            { "velbrillo", "Brillo", "Tocar fuerte abre el sonido."_u },
            { "velamp", "Volumen", "Tocar fuerte suena más fuerte."_u },
            { "vibrato", "Vibrato", "Profundidad del vibrato."_u },
            { "vibvel", "Velocidad", "Velocidad del vibrato."_u },
            { "vibret", "Retardo", "El vibrato entra tarde, como lo hace un músico."_u },
            { "voices", "Voces", "Notas a la vez. Con 1 liga las notas."_u },
            { "glide", "Glide", "Deslizamiento entre notas con una voz."_u },
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u } }, true },
        { "Env 2 · LFO"_u, join ({ { "env2_a", "Ataque", "Envolvente libre para la modulación: ataque."_u },
                                   { "env2_d", "Caída"_u, "Envolvente libre: caída."_u },
                                   { "env2_s", "Sostenido", "Envolvente libre: sostenido."_u },
                                   { "env2_r", "Relajación"_u, "Envolvente libre: relajación."_u } }, join (lfo (0), lfo (1))), true },
        { "Mod 1-4", slots (0, 4), true },
        { "Mod 5-8", slots (4, 4), true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

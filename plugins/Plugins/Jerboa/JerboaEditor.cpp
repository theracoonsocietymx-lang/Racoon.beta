#include "JerboaEditor.h"
#include "I18n.h"

namespace racoon::jerboa
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p) { startTimerHz (30); }

void Display::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (6);
    screen = r.toFloat();
    auto in = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    pads = in.removeFromTop (in.getHeight() * 0.36f);
    in.removeFromTop (8);
    timeLane = in.removeFromTop (in.getHeight() * 0.62f);
    in.removeFromTop (6);
    volLane = in;
}

Rectangle<float> Display::padRect (int k) const
{
    const float w = pads.getWidth() / 12.0f, h = (pads.getHeight() - 18.0f) / 2.0f;
    return { pads.getX() + w * (float) (k % 12) + 2.0f, pads.getY() + 18.0f + h * (float) (k / 12) + 2.0f, w - 4.0f, h - 4.0f };
}

void Display::mouseDown (const MouseEvent& e)
{
    for (int k = 0; k < patternsPerBank; ++k)
        if (padRect (k).contains (e.position))
            if (auto* p = proc.apvts.getParameter ("patron"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (k + 1)));
                p->endChangeGesture();
            }
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    const int bank = (int) proc.value ("banco");
    const int tp = proc.uiTimePattern.load(), vp = proc.uiVolPattern.load();
    g.setFont (Fonts::label (11.5f));
    g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr ("BANCO  " + upper (Processor::bankNames()[bank]) + "   ·   NOTAS DO 2 A SI 3"_u), pads.withHeight (14), Justification::centredLeft);

    // the pads: the time pattern lights up, a borrowed volume pattern gets an outline
    for (int k = 0; k < patternsPerBank; ++k)
    {
        auto r = padRect (k);
        const bool on = k == tp;
        g.setColour (on ? C::amber : C::ridge.brighter (0.15f));
        g.fillRoundedRectangle (r, 5.0f);
        if (k == vp && vp != tp) { g.setColour (C::cyan); g.drawRoundedRectangle (r, 5.0f, 1.6f); }
        g.setColour (on ? C::void_ : C::bone);
        g.setFont (Fonts::label (10.5f));
        g.drawFittedText (String (k + 1) + "\n" + ::racoon::i18n::tr (String::fromUTF8 (pattern (bank, k).name)), r.reduced (3).toNearestInt(), Justification::centred, 3, 0.7f);
    }

    // the lanes of what plays, over one bar, with the read point
    const auto& tpat = pattern (bank, tp);
    const auto& vpat = pattern (bank, vp);
    const float u = proc.uiPhase.load();
    auto lane = [&] (Rectangle<float> box, bool time)
    {
        g.setColour (C::ridge);
        g.drawRect (box, 1.0f);
        auto pr = box.reduced (6, 18).withTrimmedBottom (-12);
        for (int b = 1; b < 4; ++b)
        {
            g.setColour (C::ridge.brighter (0.25f));
            g.fillRect (pr.getX() + pr.getWidth() * (float) b / 4.0f, pr.getY(), 1.0f, pr.getHeight());
        }
        const double depth = jmax (1.0, timeDepth (tpat.time));
        Path p;
        for (int x = 0; x <= 400; ++x)
        {
            const double uu = barBeats * x / 400.0;
            const double v = time ? timeAt (tpat.time, uu) / depth : volAt (vpat.vol, uu);
            const float yy = time ? pr.getY() + pr.getHeight() * (float) v : pr.getBottom() - pr.getHeight() * (float) v;
            const float xx = pr.getX() + pr.getWidth() * (float) x / 400.0f;
            if (x == 0) p.startNewSubPath (xx, yy); else p.lineTo (xx, yy);
        }
        Draw::glowPath (g, p, time ? C::amber : C::cyan, 1.6f);
        const float px = pr.getX() + pr.getWidth() * u / (float) barBeats;
        g.setColour (C::bone.withAlpha (0.8f));
        g.fillRect (px, pr.getY(), 1.5f, pr.getHeight());
        g.setColour (C::boneDim);
        const String head = time ? "TIEMPO  " + upper (::racoon::i18n::tr (String::fromUTF8 (tpat.name))) + "   ·   VELOCIDAD " + String (proc.uiSpeed.load(), 2) + "X"
                                 : "VOLUMEN  " + upper (::racoon::i18n::tr (String::fromUTF8 (vpat.name)));
        g.drawText (::racoon::i18n::tr (head), box.withHeight (14).translated (6, 2), Justification::centredLeft);
    };
    lane (timeLane, true);
    lane (volLane, false);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1060;
    s.easyHeight = 660;
    s.advancedHeight = 220;
    s.displayEasy = 330;
    s.displayFull = 260;
    s.steps = { "Dale Play: suena un ritmo de ejemplo y pasa por Jerboa."_u, "Toca las teclas Do 2 a Si 3 (o los cuadros de la pantalla) para cambiar de patrón mientras suena."_u,
                "Prueba los bancos: Build y drop para las subidas, Cinta para frenos y cámara lenta, Compuertas para cortar al tempo."_u,
                "En la maqueta, escribe notas en el canal de Jerboa para que cambie de patrón en el momento justo."_u };
    s.sections = {
        { "Patrón"_u, {
            { "patron", "Patrón"_u, "El patrón que suena. También lo cambian las notas Do 2 a Si 3."_u },
            { "banco", "Banco", "Carga 24 patrones de fábrica: Inicial, Build y drop, Medio tiempo, Compuertas, Cinta o Rascados."_u } } },
        { "Tiempo", {
            { "tmix", "Mezcla", "Cuánto del tiempo del patrón se oye. En 0 solo queda el volumen."_u },
            { "suave", "Suavizado", "Cada salto en el tiempo se funde en estos milisegundos. Más alto, menos chasquido."_u },
            { "tono", "Tono de cinta", "Cuando el sonido va lento se oscurece, como una cinta que pierde velocidad."_u } } },
        { "Volumen", {
            { "vmix", "Cantidad", "Qué tanto corta la línea de volumen."_u } } },
        { "Salida", {
            { "salida", "Salida", "Volumen final."_u } } },
        { "Patrones", {
            { "cambio", "Cambio", "Cuándo entra el patrón que pediste: al instante, en el siguiente 1/16, 1/8, 1/4 o en el compás."_u },
            { "arranque", "Arranque", "Sigue el compás: el patrón va pegado a la rola. Desde la nota: empieza donde tocas."_u },
            { "soltar", "Al soltar", "Qué pasa cuando sueltas la tecla que cambió el patrón."_u },
            { "velvol", "Vel volumen", "Tocar suave baja el volumen del patrón."_u },
            { "notas", "Con notas", "Apágalo con Jerboa en el Master o en un bus: ahí llegan las notas de todos los canales y lo cambiarían sin querer."_u } }, true },
        { "Volumen +", {
            { "vatk", "Ataque", "Qué tan rápido abre la línea de volumen."_u },
            { "vrel", "Relajación"_u, "Qué tan rápido cierra."_u } }, true },
        { "Manos", {
            { "rascaron", "Rascar a mano", "El plato manda en lugar del patrón."_u },
            { "rascar", "Rascar", "Dónde está el plato: de ahora a un tiempo atrás. Automatízalo para rascar."_u },
            { "inercia", "Inercia", "Más inercia, el plato tarda más en seguir tu mano."_u },
            { "mantener", "Mantener", "Repite el último pedazo mientras está encendido."_u },
            { "mantlargo", "Largo", "El pedazo que se repite."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

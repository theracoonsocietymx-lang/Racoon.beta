#include "MoleEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::mole
{
using namespace juce;
using namespace racoon::ui;

void Display::resized()
{
    auto inner = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (12.0f);
    stepArea = inner.removeFromBottom (inner.getHeight() * 0.32f);
    inner.removeFromBottom (8.0f);
    curveArea = inner;
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    auto c = curveArea;
    drawScreenGrid (g, c, 10, 4);
    const int type = (int) proc.value ("type");
    const int slope = (int) proc.value ("slope");
    const float reso = proc.value ("reso") * 0.01f;
    const float base = proc.value ("cutoff");
    const float mod = proc.modCutoff.load();
    auto fx = [&] (float f) { return c.getX() + c.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
    auto dy = [&] (float db) { return c.getCentreY() - c.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, db / 30.0f); };
    auto curve = [&] (float fc, Colour col, float w)
    {
        Path p;
        for (int i = 0; i <= 240; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 240.0f);
            const float db = proc.responseDb (type, slope, fc, reso, f);
            if (i == 0) p.startNewSubPath (fx (f), dy (db)); else p.lineTo (fx (f), dy (db));
        }
        g.setColour (col);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    };
    curve (base, Pro::textFaint, 1.2f);
    curve (mod, Pro::value, 2.2f);
    g.setColour (Pro::input);
    g.fillRect (fx (jlimit (20.0f, 20000.0f, mod)) - 0.5f, c.getY(), 1.5f, c.getHeight());
    g.setColour (Pro::textDim);
    g.setFont (Fonts::label (11.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
        g.drawText (::racoon::i18n::tr (f < 1000.0f ? String ((int) f) : String ((int) f / 1000) + "k"), Rectangle<float> (fx (f) + 3.0f, c.getBottom() - 14.0f, 40.0f, 12.0f), Justification::centredLeft);
    g.drawText (::racoon::i18n::tr (String (roundToInt (mod)) + " Hz"), c.reduced (6.0f, 4.0f), Justification::topRight);

    // steps
    auto s = stepArea;
    const float w = s.getWidth() / (float) Processor::numSteps;
    const int cur = proc.currentStep.load();
    const bool active = std::abs (proc.value ("stepamt")) > 0.5f;
    for (int i = 0; i < Processor::numSteps; ++i)
    {
        auto cell = Rectangle<float> (s.getX() + w * (float) i, s.getY(), w, s.getHeight()).reduced (2.0f, 0.0f);
        g.setColour (i == cur && active ? Pro::lineHi : Pro::raised);
        g.fillRect (cell);
        const float v = proc.value ("step" + String (i + 1)) * 0.01f;
        const float mid = cell.getCentreY();
        const float h = cell.getHeight() * 0.5f * std::abs (v);
        g.setColour (active ? (v >= 0.0f ? Pro::value : Pro::reduce) : Pro::textFaint);
        g.fillRect (v >= 0.0f ? Rectangle<float> (cell.getX(), mid - h, cell.getWidth(), h) : Rectangle<float> (cell.getX(), mid, cell.getWidth(), h));
    }
    g.setColour (Pro::textDim);
    g.drawText (::racoon::i18n::tr (active ? "PASOS" : "PASOS (sube «Pasos cantidad» para oírlos)"_u), s.reduced (4.0f, 2.0f), Justification::topLeft);
}

void Display::setStepFrom (Point<float> p)
{
    const float w = stepArea.getWidth() / (float) Processor::numSteps;
    const int i = jlimit (0, Processor::numSteps - 1, (int) ((p.x - stepArea.getX()) / w));
    const float v = jlimit (-100.0f, 100.0f, (stepArea.getCentreY() - p.y) / (stepArea.getHeight() * 0.5f) * 100.0f);
    auto* prm = proc.apvts.getParameter ("step" + String (i + 1));
    if (dragging != i)
    {
        if (dragging >= 0) proc.apvts.getParameter ("step" + String (dragging + 1))->endChangeGesture();
        prm->beginChangeGesture();
        dragging = i;
    }
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void Display::mouseDown (const MouseEvent& e) { if (stepArea.contains (e.position)) { dragging = -1; setStepFrom (e.position); } }
void Display::mouseDrag (const MouseEvent& e) { if (dragging >= 0) setStepFrom (e.position); }
void Display::mouseUp (const MouseEvent&)
{
    if (dragging >= 0) proc.apvts.getParameter ("step" + String (dragging + 1))->endChangeGesture();
    dragging = -1;
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 960;
    s.easyHeight = 610;
    s.advancedHeight = 190;
    s.sections = {
        { "Filtro", {
            { "type", "Tipo", "Paso bajo quita agudos, paso alto quita graves, banda deja una franja, Notch quita una franja, Peine da un color metálico, Ladder es redondo y gordo."_u },
            { "cutoff", "Corte", "Frecuencia donde actúa el filtro."_u },
            { "reso", "Resonancia", "Realza la zona del corte. Alto silba o canta."_u },
            { "drive", "Drive", "Ensucia un poco la señal en el filtro."_u },
            { "mix", "Mezcla", "Original contra filtrado."_u } } },
        { "Movimiento", {
            { "lfodepth", "LFO", "Cuánto mueve el LFO el corte, hacia arriba o hacia abajo."_u },
            { "lforate", "Velocidad", "Velocidad del LFO cuando no va a tempo."_u },
            { "envamt", "Envolvente", "El volumen de lo que entra abre o cierra el filtro (efecto wah)."_u },
            { "stepamt", "Pasos", "Cuánto mueve el modulador de 16 pasos. Dibuja los pasos en la pantalla."_u } } },
        { "LFO", {
            { "lfosync", "A tempo", "El LFO sigue el tempo del proyecto."_u },
            { "lfodiv", "División"_u, "Duración de un ciclo cuando va a tempo."_u },
            { "lfoshape", "Forma", "Forma de la onda del LFO. S&H salta a valores al azar."_u },
            { "lfophase", "Desfase", "Desfase entre izquierda y derecha: abre el estéreo."_u } }, true },
        { "Envolvente y teclado", {
            { "envatk", "Ataque", "Qué tan rápido sigue la envolvente al subir."_u },
            { "envrel", "Relajación"_u, "Qué tan rápido regresa."_u },
            { "envsrc", "Fuente", "Entrada usa el audio de la pista; Sidechain usa otra pista."_u },
            { "keytrack", "Teclado", "El corte sigue las notas MIDI que tocas en la pista."_u } }, true },
        { "Pasos y salida", {
            { "stepdiv", "División"_u, "Duración de cada paso."_u },
            { "stepglide", "Glide", "Suaviza el salto entre pasos."_u },
            { "slope", "Pendiente", "12 dB es suave; 24 dB corta más."_u },
            { "drivepos", "Drive en", "Saturar antes o después del filtro."_u },
            { "output", "Salida", "Volumen final."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

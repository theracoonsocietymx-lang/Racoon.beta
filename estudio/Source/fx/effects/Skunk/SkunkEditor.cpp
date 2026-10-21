#include "SkunkEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::skunk
{
using namespace juce;
using namespace racoon::ui;

void Display::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto inner = r.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    g.setFont (NumFont::medium (9.5f));

    // ---- the curve: input across, output up, -1..+1
    auto curve = inner.removeFromLeft (inner.getHeight() - 16.0f).withTrimmedLeft (24.0f).withTrimmedBottom (16.0f);
    const int mode = (int) proc.value ("mode");
    const float drive = dsp::dbToGain (proc.value ("drive"));
    const float bias = proc.value ("bias") * 0.01f * 0.8f;
    auto cx = [&] (float x) { return curve.getX() + curve.getWidth() * (x + 1.0f) * 0.5f; };
    auto cy = [&] (float y) { return curve.getCentreY() - curve.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, y); };
    for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        g.setColour (v == 0.0f ? Pro::line : Pro::grid);
        g.fillRect (cx (v), curve.getY(), 1.0f, curve.getHeight());
        g.fillRect (curve.getX(), cy (v), curve.getWidth(), 1.0f);
        g.setColour (Pro::textFaint);
        const String t = v == 0.0f ? String ("0") : String (v, 1);
        g.drawText (::racoon::i18n::tr (t), Rectangle<float> (curve.getX() - 26.0f, cy (v) - 6.0f, 22.0f, 12.0f), Justification::centredRight);
        g.drawText (::racoon::i18n::tr (t), Rectangle<float> (cx (v) - 14.0f, curve.getBottom() + 3.0f, 28.0f, 12.0f), Justification::centred);
    }
    g.setColour (Pro::line);
    g.drawLine (cx (-1.0f), cy (-1.0f), cx (1.0f), cy (1.0f), 1.0f);
    Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 200.0f;
        const float y = Processor::shapeBiased (mode, x * drive, bias);
        if (i == 0) p.startNewSubPath (cx (x), cy (y)); else p.lineTo (cx (x), cy (y));
    }
    g.setColour (Pro::value);
    g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    // how far the signal reaches on the curve right now: a band over the input range it is using
    const int N = Processor::scopeSize, pos = proc.scopePos.load();
    float pkIn = 0.0f, pkOut = 0.0f;
    for (int i = 0; i < N; ++i) { pkIn = jmax (pkIn, std::abs (proc.scopeIn[(size_t) i].load())); pkOut = jmax (pkOut, std::abs (proc.scopeOut[(size_t) i].load())); }
    if (pkIn > 1.0e-4f)
    {
        const float reach = jmin (1.0f, pkIn);
        g.setColour (Pro::input.withAlpha (0.10f));
        g.fillRect (Rectangle<float> (cx (-reach), curve.getY(), cx (reach) - cx (-reach), curve.getHeight()));
        g.setColour (Pro::input);
        g.fillRect (cx (reach) - 0.5f, curve.getBottom() - 6.0f, 1.0f, 6.0f);
        g.fillRect (cx (-reach) - 0.5f, curve.getBottom() - 6.0f, 1.0f, 6.0f);
    }
    {
        auto lab = curve.withHeight (16.0f).translated (6.0f, 3.0f);
        ProDraw::caps (g, "Drive", lab.removeFromLeft (44.0f), Justification::centredLeft, Pro::value, 9.5f);
        g.setFont (NumFont::medium (11.0f));
        g.setColour (Pro::text);
        g.drawText (::racoon::i18n::tr (String (proc.value ("drive"), 1) + " dB"), lab, Justification::centredLeft);
    }

    // ---- meters
    inner.removeFromLeft (16.0f);
    auto meters = inner.removeFromRight (84.0f);
    inner.removeFromRight (14.0f);
    auto dbOf = [] (float v) { return jmax (-99.0f, Decibels::gainToDecibels (v + 1.0e-9f)); };
    ProDraw::meter (g, meters.removeFromLeft (42.0f), dbOf (pkIn), Pro::input, "In");
    ProDraw::meter (g, meters, dbOf (pkOut), Pro::value, "Out");

    // ---- the wave in and out, standing still on a rising zero crossing, zoomed so a quiet signal is visible
    auto head = inner.removeFromTop (18.0f);
    auto wr = inner;
    for (int k = 1; k < 8; ++k) { g.setColour (Pro::grid); g.fillRect (wr.getX() + wr.getWidth() * (float) k / 8.0f, wr.getY(), 1.0f, wr.getHeight()); }
    g.setColour (Pro::line);
    g.fillRect (wr.getX(), wr.getCentreY(), wr.getWidth(), 1.0f);
    int start = 0;
    for (int i = 1; i < N / 2; ++i)
    {
        const float a = proc.scopeIn[(size_t) ((pos + i - 1) % N)].load(), b = proc.scopeIn[(size_t) ((pos + i) % N)].load();
        if (a <= 0.0f && b > 0.0f) { start = i; break; }
    }
    const float peak = jmax (pkIn, pkOut, 1.0e-3f);
    const float zoom = jlimit (1.0f, 16.0f, std::pow (2.0f, std::floor (std::log2 (0.9f / peak))));
    auto wave = [&] (std::array<std::atomic<float>, Processor::scopeSize>& src, Colour col, float w)
    {
        Path w1;
        const int len = N / 2;
        for (int i = 0; i < len; ++i)
        {
            const float v = jlimit (-1.1f, 1.1f, src[(size_t) ((pos + start + i) % N)].load() * zoom);
            const float x = wr.getX() + wr.getWidth() * (float) i / (float) (len - 1);
            const float y = wr.getCentreY() - wr.getHeight() * 0.45f * v;
            if (i == 0) w1.startNewSubPath (x, y); else w1.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (w1, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    };
    wave (proc.scopeIn, Pro::input.withAlpha (0.55f), 1.2f);
    wave (proc.scopeOut, Pro::value, 1.8f);
    {
        auto leg = head;
        auto item = [&] (const String& name, Colour col)
        {
            g.setColour (col);
            g.fillRect (leg.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 2.0f));
            leg.removeFromLeft (5.0f);
            ProDraw::caps (g, name, leg.removeFromLeft (70.0f), Justification::centredLeft, Pro::textDim, 9.5f);
        };
        item ("Entrada", Pro::input);
        item ("Salida", Pro::value);
        if (zoom > 1.0f)
        {
            g.setColour (Pro::textDim);
            g.setFont (NumFont::medium (10.5f));
            g.drawText (::racoon::i18n::tr ("x" + String ((int) zoom)), head, Justification::centredRight);
        }
    }
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 920;
    s.easyHeight = 580;
    s.advancedHeight = 170;
    s.sections = {
        { "Color", {
            { "mode", "Motor", "Tipo de saturación: Soft clip redondo, Hard clip recorta seco, Waveshaper intermedio, Diodo asimétrico y sucio, Foldback dobla la onda, Cinta cálida, Válvula con armónicos pares, Rectificador casi borra la mitad de abajo y Plegado seno dobla la onda una y otra vez."_u },
            { "drive", "Drive", "Cuánto empuja la señal contra la curva. Más drive, más armónicos."_u },
            { "bias", "Bias", "Desplaza la curva para que sature distinto arriba y abajo: añade armónicos pares."_u },
            { "tone", "Tono", "Quita brillo después de saturar. A la derecha no corta nada."_u },
            { "mix", "Mezcla", "Original contra saturado. Bajo 100 % hace saturación en paralelo."_u } } },
        { "Nivel", {
            { "autogain", "Nivel auto", "Iguala el volumen de salida al de entrada para que juzgues el color y no el volumen."_u },
            { "outtrim", "Salida", "Volumen final."_u } } },
        { "Entrada y filtro"_u, {
            { "intrim", "Entrada", "Volumen antes de saturar."_u },
            { "lowcut", "Corte de graves", "Quita graves antes de saturar para que el bajo no se ensucie."_u } }, true },
        { "Calidad", {
            { "os", "Oversampling", "Procesa a mayor frecuencia para evitar aliasing (ruido metálico). Añade un poco de latencia que Racoon compensa."_u },
            { "hq", "HQ al exportar", "Al exportar usa 8x aunque en vivo uses menos."_u } }, true },
        { "Estéreo"_u, {
            { "stereo", "Modo", "Mid / Side satura por separado el centro y los lados."_u },
            { "sidedrive", "Drive lados", "En Mid / Side, cuánto drive reciben los lados respecto al centro."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

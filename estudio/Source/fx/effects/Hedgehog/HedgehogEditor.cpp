#include "HedgehogEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::hedgehog
{
using namespace juce;
using namespace racoon::ui;

void Display::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto inner = r.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    const float thr = proc.value ("thresh"), ratio = proc.value ("ratio"), knee = proc.value ("knee");
    const bool up = (int) proc.value ("mode") == 4;
    g.setFont (NumFont::medium (9.5f));

    // ---- transfer curve: input dB across, output dB up, with numbers
    auto curve = inner.removeFromLeft (inner.getHeight() - 16.0f).withTrimmedLeft (26.0f).withTrimmedBottom (16.0f);
    auto toX = [&] (float db) { return curve.getX() + curve.getWidth() * (db + 72.0f) / 78.0f; };
    auto toY = [&] (float db) { return curve.getBottom() - curve.getHeight() * (jlimit (-72.0f, 6.0f, db) + 72.0f) / 78.0f; };
    for (float d = -72.0f; d <= 0.01f; d += 12.0f)
    {
        g.setColour (Pro::grid);
        g.fillRect (toX (d), curve.getY(), 1.0f, curve.getHeight());
        g.fillRect (curve.getX(), toY (d), curve.getWidth(), 1.0f);
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr (String ((int) d)), Rectangle<float> (curve.getX() - 28.0f, toY (d) - 6.0f, 24.0f, 12.0f), Justification::centredRight);
        g.drawText (::racoon::i18n::tr (String ((int) d)), Rectangle<float> (toX (d) - 14.0f, curve.getBottom() + 3.0f, 28.0f, 12.0f), Justification::centred);
    }
    g.setColour (Pro::line);
    g.drawLine (toX (-72.0f), toY (-72.0f), toX (6.0f), toY (6.0f), 1.0f);
    const float dash[] { 3.0f, 3.0f };
    g.setColour (Pro::reduce.withAlpha (0.7f));
    g.drawDashedLine ({ toX (thr), curve.getY(), toX (thr), curve.getBottom() }, dash, 2, 1.0f);
    Path p;
    for (float in = -72.0f; in <= 6.0f; in += 0.5f)
    {
        const float out = in + Processor::gainComputer (in, thr, ratio, knee, up);
        if (in == -72.0f) p.startNewSubPath (toX (in), toY (out)); else p.lineTo (toX (in), toY (out));
    }
    g.saveState();
    g.reduceClipRegion (curve.toNearestInt());
    g.setColour (Pro::value);
    g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const float lvl = proc.levelDb.load();
    if (lvl > -72.0f)
    {
        const float out = lvl + Processor::gainComputer (lvl, thr, ratio, knee, up);
        g.setColour (Pro::input);
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ toX (lvl), toY (out) }));
    }
    g.restoreState();
    {
        auto lab = curve.withHeight (16.0f).translated (6.0f, 3.0f);
        ProDraw::caps (g, "Umbral", lab.removeFromLeft (48.0f), Justification::centredLeft, Pro::reduce, 9.5f);
        g.setFont (NumFont::medium (11.0f));
        g.setColour (Pro::text);
        g.drawText (::racoon::i18n::tr (String (thr, 1) + " dB   " + String (ratio, 1) + ":1"), lab, Justification::centredLeft);
    }

    // ---- reduction meter with its number
    inner.removeFromLeft (16.0f);
    const float gr = proc.grDb.load();
    ProDraw::meter (g, inner.removeFromLeft (44.0f), -std::abs (gr), up ? Pro::second : Pro::reduce, up ? "+dB" : "GR", true, 24.0f);
    inner.removeFromLeft (16.0f);

    // ---- history: input level as a line, reduction hanging from the top; scales on both sides
    auto head = inner.removeFromTop (18.0f);
    auto hr = inner.withTrimmedBottom (16.0f).withTrimmedRight (30.0f);
    g.setFont (NumFont::medium (9.5f));
    for (float d : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
    {
        const float y = hr.getY() + hr.getHeight() * -d / 24.0f;
        g.setColour (Pro::grid);
        g.fillRect (hr.getX(), y, hr.getWidth(), 1.0f);
        g.setColour (up ? Pro::second.withAlpha (0.8f) : Pro::reduce.withAlpha (0.8f));
        g.drawText (::racoon::i18n::tr (String ((int) d)), Rectangle<float> (hr.getRight() + 4.0f, y - 6.0f, 26.0f, 12.0f), Justification::centredLeft);
    }
    const int N = Processor::historySize, pos = proc.historyPos.load();
    for (int k = 0; k <= 4; ++k)
    {
        const float x = hr.getX() + hr.getWidth() * (float) k / 4.0f;
        g.setColour (Pro::grid);
        g.fillRect (x, hr.getY(), 1.0f, hr.getHeight());
    }
    g.setColour (Pro::textFaint);
    g.drawText (::racoon::i18n::tr ("ahora"), Rectangle<float> (hr.getRight() - 40.0f, hr.getBottom() + 3.0f, 40.0f, 12.0f), Justification::centredRight);
    Path in, grPath;
    grPath.startNewSubPath (hr.getX(), hr.getY());
    for (int i = 0; i < N; ++i)
    {
        const int k = (pos + i) % N;
        const float x = hr.getX() + hr.getWidth() * (float) i / (float) (N - 1);
        const float lv = jlimit (-60.0f, 0.0f, proc.inHistory[(size_t) k].load());
        const float y = hr.getBottom() - hr.getHeight() * (lv + 60.0f) / 60.0f;
        if (i == 0) in.startNewSubPath (x, y); else in.lineTo (x, y);
        const float gv = jlimit (0.0f, 24.0f, std::abs (proc.grHistory[(size_t) k].load()));
        grPath.lineTo (x, hr.getY() + hr.getHeight() * gv / 24.0f);
    }
    grPath.lineTo (hr.getRight(), hr.getY());
    grPath.closeSubPath();
    g.setColour ((up ? Pro::second : Pro::reduce).withAlpha (0.35f));
    g.fillPath (grPath);
    g.setColour ((up ? Pro::second : Pro::reduce));
    g.strokePath (grPath, PathStrokeType (1.0f));
    g.setColour (Pro::input.withAlpha (0.8f));
    g.strokePath (in, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
    // legend in its own strip, so nothing writes over the traces
    {
        auto leg = head;
        auto item = [&] (const String& name, Colour col)
        {
            g.setColour (col);
            g.fillRect (leg.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 2.0f));
            leg.removeFromLeft (5.0f);
            ProDraw::caps (g, name, leg.removeFromLeft (110.0f), Justification::centredLeft, Pro::textDim, 9.5f);
        };
        item ("Entrada", Pro::input);
        item (up ? String::fromUTF8 ("Ganancia sumada") : String::fromUTF8 ("Reducción"), up ? Pro::second : Pro::reduce);
        g.setColour (Pro::text);
        g.setFont (NumFont::medium (11.0f));
        g.drawText (::racoon::i18n::tr (String (gr, 1) + " dB"), head.withTrimmedRight (30.0f), Justification::centredRight);
    }
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 960;
    s.easyHeight = 600;
    s.advancedHeight = 180;
    s.sections = {
        { "Compresión"_u, {
            { "mode", "Modo", "Carácter del compresor: VCA limpio, Opto suave, FET rápido y con color, Bus glue para pegar mezclas, Upward sube lo bajo, Paralelo mezcla con el original."_u },
            { "thresh", "Umbral", "Nivel a partir del cual empieza a comprimir. Más bajo, comprime más."_u },
            { "ratio", "Ratio", "Cuánto aplasta lo que pasa del umbral. 2:1 suave, 4:1 normal, 10:1 o más casi limita."_u },
            { "attack", "Ataque", "Qué tan rápido reacciona. Lento deja pasar el golpe del bombo o la caja; rápido lo doma."_u },
            { "release", "Relajación"_u, "Qué tan rápido suelta. Corto suena más vivo y bombea; largo suena más parejo."_u },
            { "makeup", "Ganancia", "Recupera el volumen perdido al comprimir."_u },
            { "mix", "Mezcla", "Original contra comprimido. Menos de 100 % hace compresión en paralelo."_u } } },
        { "Sidechain", {
            { "scext", "Sidechain", "Usa otra pista para mandar la compresión, por ejemplo el bombo apagando el bajo. En Racoon: clic derecho en el efecto > Sidechain desde."_u },
            { "schp", "HP detector", "Quita graves de lo que escucha el detector para que el bombo no lo haga bombear de más."_u } } },
        { "Detector", {
            { "knee", "Rodilla", "Suaviza la entrada a la compresión. 0 dB es un corte duro."_u },
            { "detector", "Detector", "Pico reacciona a cada golpe; RMS escucha el volumen promedio, más musical."_u },
            { "rms", "Ventana RMS", "Tiempo que promedia el detector RMS."_u },
            { "topology", "Topología"_u, "Feedforward es preciso; Feedback escucha la salida y suena más vintage."_u },
            { "hold", "Hold", "Tiempo que mantiene la reducción antes de soltar."_u },
            { "autorel", "Auto", "Relajación automática: rápida en golpes sueltos, lenta en pasajes sostenidos."_u },
            { "automakeup", "Ganancia auto", "Sube sola la salida según el umbral y la ratio, para comparar sin que engañe el volumen."_u } }, true },
        { "Estéreo y lookahead"_u, {
            { "link", "Enlace", "100 % comprime los dos canales igual para que la imagen no se mueva."_u },
            { "stereo", "Modo", "Mid / Side comprime por separado el centro y los lados."_u },
            { "look", "Lookahead", "Mira hacia adelante para atrapar picos. Añade latencia que Racoon compensa."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

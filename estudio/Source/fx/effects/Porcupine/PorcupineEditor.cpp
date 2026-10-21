#include "PorcupineEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::porcupine
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p)
{
    startTimerHz (30);
    setWantsKeyboardFocus (false);
}

Rectangle<float> Display::plot() const
{
    return getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (12.0f, 10.0f);
}

float Display::freqToX (float f) const { auto p = plot(); return p.getX() + p.getWidth() * std::log (jmax (10.0f, f) / 20.0f) / std::log (1100.0f); }
float Display::xToFreq (float x) const { auto p = plot(); return 20.0f * std::pow (1100.0f, (x - p.getX()) / p.getWidth()); }
float Display::dbToY (float db) const { auto p = plot(); return p.getCentreY() - p.getHeight() * 0.5f * db / 24.0f; }
float Display::yToDb (float y) const { auto p = plot(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * 24.0f; }

Point<float> Display::handle (int b) const
{
    const auto s = proc.band (b);
    const bool hasGain = s.type <= 2;
    return { freqToX (s.freq), dbToY (hasGain ? s.gain : 0.0f) };
}

int Display::hitBand (Point<float> p) const
{
    int best = -1; float bestD = 14.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const float d = p.getDistanceFrom (handle (b));
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void Display::setParam (int b, const char* what, float v)
{
    if (auto* prm = proc.apvts.getParameter (Processor::bandId (b, what)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void Display::computeSpectrum (const std::vector<float>& fifo, std::vector<float>& out, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    const int w = proc.fifoWrite.load();
    for (int i = 0; i < n; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) (n - 1));
        data[(size_t) i] = fifo[(size_t) ((w - n + i + Processor::fifoSize) & (Processor::fifoSize - 1))] * win;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    const int points = 220;
    if ((int) out.size() != points) out.assign ((size_t) points, -120.0f);
    const double sr = jmax (8000.0, proc.getSampleRate());
    for (int k = 0; k < points; ++k)
    {
        const float f = 20.0f * std::pow (1100.0f, (float) k / (float) (points - 1));
        const int bin = jlimit (1, n / 2 - 1, (int) std::round (f * n / sr));
        const float mag = data[(size_t) bin] * 4.0f / (float) n;
        const float db = 20.0f * std::log10 (jmax (1.0e-7f, mag)) + 4.5f * std::log2 (f / 1000.0f);   // tilt like most analyzers
        out[(size_t) k] = jmax (db, out[(size_t) k] - 1.5f);   // fall slowly
    }
}

void Display::timerCallback()
{
    const int mode = (int) proc.value ("analyzer");
    const int order = 10 + (int) proc.value ("fftsize");
    if (mode == 1 || mode == 3) computeSpectrum (proc.fifoPre, preDb, order); else preDb.clear();
    if (mode == 2 || mode == 3) computeSpectrum (proc.fifoPost, postDb, order); else postDb.clear();
    repaint();
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    auto p = plot();
    // grid with its numbers: every line says what it is
    g.setFont (NumFont::medium (9.5f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        g.setColour (f == 100.0f || f == 1000.0f || f == 10000.0f ? Pro::line : Pro::grid);
        g.fillRect (freqToX (f), p.getY(), 1.0f, p.getHeight());
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr (f < 1000.0f ? String ((int) f) : String ((int) f / 1000) + "k"), Rectangle<float> (freqToX (f) + 3, p.getBottom() - 13, 40, 12), Justification::centredLeft);
    }
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (db == 0.0f ? Pro::line : Pro::grid);
        g.fillRect (p.getX(), dbToY (db), p.getWidth(), 1.0f);
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr ((db > 0 ? "+" : "") + String ((int) db)), Rectangle<float> (p.getX() + 3, dbToY (db) - 13, 40, 12), Justification::centredLeft);
    }

    // analyzer: -90..0 dB mapped onto the plot height
    auto spec = [&] (const std::vector<float>& db, Colour c, bool fill)
    {
        if (db.empty()) return;
        Path path;
        for (size_t k = 0; k < db.size(); ++k)
        {
            const float f = 20.0f * std::pow (1100.0f, (float) k / (float) (db.size() - 1));
            const float y = p.getBottom() - p.getHeight() * jlimit (0.0f, 1.0f, (db[k] + 90.0f) / 90.0f);
            if (k == 0) path.startNewSubPath (freqToX (f), y); else path.lineTo (freqToX (f), y);
        }
        if (fill)
        {
            Path f2 (path);
            f2.lineTo (p.getRight(), p.getBottom()); f2.lineTo (p.getX(), p.getBottom()); f2.closeSubPath();
            g.setColour (c.withAlpha (0.18f));
            g.fillPath (f2);
        }
        g.setColour (c.withAlpha (0.6f));
        g.strokePath (path, PathStrokeType (1.0f));
    };
    spec (preDb, Pro::textDim, true);
    spec (postDb, Pro::second, false);

    // selected band's own curve, then the full curve
    auto curve = [&] (std::function<float (float)> dbAt, Colour c, float w)
    {
        Path path;
        for (int i = 0; i <= 300; ++i)
        {
            const float f = 20.0f * std::pow (1100.0f, (float) i / 300.0f);
            const float y = dbToY (jlimit (-26.0f, 26.0f, dbAt (f)));
            if (i == 0) path.startNewSubPath (freqToX (f), y); else path.lineTo (freqToX (f), y);
        }
        if (w > 2.0f)
        {
            // the whole EQ: a soft fill between the curve and 0 dB
            Path f2 (path);
            f2.lineTo (p.getRight(), dbToY (0.0f)); f2.lineTo (p.getX(), dbToY (0.0f)); f2.closeSubPath();
            g.setColour (c.withAlpha (0.12f));
            g.fillPath (f2);
        }
        g.setColour (c);
        g.strokePath (path, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    };
    if (proc.band (selected).on)
        curve ([this] (float f) { return proc.bandMagnitudeDb (selected, f); }, Pro::reduce.withAlpha (0.8f), 1.2f);
    curve ([this] (float f) { return (float) (20.0 * std::log10 (jmax (1.0e-9, proc.magnitudeAt (f, true)))); }, Pro::value, 2.2f);

    for (int b = 0; b < numBands; ++b)
    {
        const auto s = proc.band (b);
        auto h = handle (b);
        const float r = b == selected ? 10.0f : 8.0f;
        auto box = Rectangle<float> (r * 2, r * 2).withCentre (h);
        // a round handle: filled when selected, an outline when on, faint when off
        g.setColour (Pro::screen);
        g.fillEllipse (box);
        if (b == selected && s.on) { g.setColour (Pro::value); g.fillEllipse (box); }
        g.setColour (! s.on ? Pro::textFaint : (b == selected ? Pro::value : Pro::text));
        g.drawEllipse (box.reduced (0.75f), 1.5f);
        g.setColour (! s.on ? Pro::textFaint : (b == selected ? Pro::win : Pro::text));
        g.setFont (NumFont::semi (11.0f));
        g.drawText (::racoon::i18n::tr (String (b + 1)), box, Justification::centred);
        if (proc.value (Processor::bandId (b, "dyn")) > 0.5f && s.on)
        {
            g.setColour (Pro::input);
            g.fillRect (box.getX(), box.getBottom() + 2, box.getWidth() * jlimit (0.0f, 1.0f, std::abs (proc.dynGainDb[b].load()) / 12.0f), 2.5f);
        }
    }
    if (hover >= 0 || dragBand >= 0)
    {
        const int b = dragBand >= 0 ? dragBand : hover;
        const auto s = proc.band (b);
        String t = String (b + 1) + "  " + Processor::typeNames()[s.type] + "  " + (s.freq < 1000 ? String (roundToInt (s.freq)) + " Hz" : String (s.freq / 1000.0f, 2) + " kHz");
        if (s.type <= 2) t << "  " << (s.gain > 0 ? "+" : "") << String (s.gain, 1) << " dB";
        t << "  Q " << String (s.q, 2);
        g.setColour (Pro::text);
        g.setFont (NumFont::medium (12.0f));
        g.drawText (::racoon::i18n::tr (t), p.reduced (6, 4), Justification::topRight);
    }
    if ((int) proc.value ("solo") > 0)
    {
        ProDraw::caps (g, "Solo banda " + String ((int) proc.value ("solo")), p.reduced (6, 4).withHeight (16), Justification::topLeft, Pro::reduce, 11.0f);
    }
}

void Display::mouseMove (const MouseEvent& e) { hover = hitBand (e.position); }

void Display::mouseDown (const MouseEvent& e)
{
    dragBand = hitBand (e.position);
    if (dragBand < 0) return;
    selected = dragBand;
    if (onSelect) onSelect (selected);
    if (e.mods.isPopupMenu()) { showBandMenu (dragBand); dragBand = -1; return; }
    for (auto* w : { "freq", "gain" }) proc.apvts.getParameter (Processor::bandId (dragBand, w))->beginChangeGesture();
    if (! proc.band (dragBand).on) setParam (dragBand, "on", 1.0f);
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (dragBand < 0) return;
    setParam (dragBand, "freq", jlimit (10.0f, 24000.0f, xToFreq (e.position.x)));
    if (proc.band (dragBand).type <= 2) setParam (dragBand, "gain", jlimit (-24.0f, 24.0f, yToDb (e.position.y)));
}

void Display::mouseUp (const MouseEvent&)
{
    if (dragBand >= 0)
        for (auto* w : { "freq", "gain" }) proc.apvts.getParameter (Processor::bandId (dragBand, w))->endChangeGesture();
    dragBand = -1;
}

void Display::mouseDoubleClick (const MouseEvent& e)
{
    const int b = hitBand (e.position);
    if (b >= 0) { setParam (b, "on", proc.band (b).on ? 0.0f : 1.0f); return; }
    // empty space: switch on the first free band right here as a bell
    for (int k = 0; k < numBands; ++k)
        if (! proc.band (k).on)
        {
            setParam (k, "type", 0.0f);
            setParam (k, "freq", xToFreq (e.position.x));
            setParam (k, "gain", jlimit (-24.0f, 24.0f, yToDb (e.position.y)));
            setParam (k, "on", 1.0f);
            selected = k;
            if (onSelect) onSelect (k);
            return;
        }
}

void Display::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int b = hitBand (e.position) >= 0 ? hitBand (e.position) : selected;
    const float q = proc.band (b).q * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f);
    setParam (b, "q", jlimit (0.1f, 36.0f, q));
}

void Display::showBandMenu (int b)
{
    PopupMenu m;
    const auto s = proc.band (b);
    PopupMenu types;
    for (int t = 0; t < Processor::typeNames().size(); ++t)
        types.addItem (Processor::typeNames()[t], true, s.type == t, [this, b, t] { setParam (b, "type", (float) t); });
    m.addSubMenu ("Tipo", types);
    if (s.type == 3 || s.type == 4)
    {
        PopupMenu sl;
        const char* names[] { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };
        for (int k = 0; k < 4; ++k) sl.addItem (names[k], true, s.slope == k, [this, b, k] { setParam (b, "slope", (float) k); });
        m.addSubMenu ("Pendiente", sl);
    }
    const bool dyn = proc.value (Processor::bandId (b, "dyn")) > 0.5f;
    m.addItem ("Dinámica"_u, true, dyn, [this, b, dyn] { setParam (b, "dyn", dyn ? 0.0f : 1.0f); });
    const bool solo = (int) proc.value ("solo") == b + 1;
    m.addItem ("Escuchar solo esta banda", true, solo, [this, b, solo]
    {
        if (auto* prm = proc.apvts.getParameter ("solo")) prm->setValueNotifyingHost (prm->convertTo0to1 (solo ? 0.0f : (float) (b + 1)));
    });
    m.addItem ("Ganancia a 0 dB", [this, b] { setParam (b, "gain", 0.0f); });
    m.addSeparator();
    m.addItem (s.on ? "Apagar banda" : "Encender banda", [this, b, s] { setParam (b, "on", s.on ? 0.0f : 1.0f); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1000;
    s.easyHeight = 640;
    s.advancedHeight = 150;
    s.displayEasy = 330;
    s.displayFull = 290;
    for (int b = 0; b < numBands; ++b)
    {
        Sec sec;
        sec.title = "Banda " + String (b + 1);
        auto id = [b] (const char* w) { return Processor::bandId (b, w); };
        sec.controls = {
            { id ("on"), "Activa", "Enciende o apaga la banda. Doble clic en su cuadro de la pantalla hace lo mismo."_u },
            { id ("type"), "Tipo", "Campana sube o baja una zona; los shelf mueven todo lo grave o todo lo agudo; paso alto y bajo cortan."_u },
            { id ("freq"), "Frecuencia", "Dónde actúa. También puedes arrastrar el cuadro en la pantalla."_u },
            { id ("gain"), "Ganancia", "Cuánto sube o baja."_u },
            { id ("q"), "Q", "Ancho: bajo es amplio y musical, alto es estrecho y quirúrgico. Rueda del ratón sobre el cuadro."_u },
            { id ("slope"), "Pendiente", "Para paso alto y bajo: cuánto corta por octava."_u },
            { id ("dyn"), "Dinámica"_u, "La banda solo actúa cuando esa zona suena fuerte (por ejemplo, para quitar eses)."_u },
            { id ("thr"), "Umbral", "Nivel de la zona a partir del cual la banda dinámica empieza a bajar."_u },
            { id ("range"), "Rango", "Cuánto baja como máximo la banda dinámica."_u } };
        s.sections.push_back (sec);
    }
    s.sections.push_back ({ "Salida", {
        { "phase", "Fase", "Mínima para mezclar sin latencia; Lineal para master, sin cambiar la fase (añade latencia que Racoon compensa; sin bandas dinámicas)."_u },
        { "output", "Salida", "Volumen final."_u } } });
    s.sections.push_back ({ "Analizador", {
        { "analyzer", "Mostrar", "Qué espectro se dibuja: antes del EQ, después, o los dos."_u },
        { "fftsize", "Resolución"_u, "Más alta separa mejor los graves y se mueve más lento."_u } }, true });
    s.sections.push_back ({ "Dinámica y solo"_u, {
        { "dynatk", "Ataque", "Qué tan rápido reaccionan las bandas dinámicas."_u },
        { "dynrel", "Relajación"_u, "Qué tan rápido regresan."_u },
        { "solo", "Solo", "Escucha solo la zona de una banda para encontrar lo que molesta. 0 apaga el solo."_u } }, true });
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p))
{
    disp = dynamic_cast<Display*> (getDisplay());
    disp->onSelect = [this] (int) { resized(); repaint(); };
    resized();
}

bool Editor::showEssential (const SectionCard& c) const
{
    if (! c.title.startsWith ("Banda ")) return true;
    return disp != nullptr && c.title == "Banda " + String (disp->selected + 1);
}
}

#include "HedgehogEditor.h"
#include "I18n.h"

namespace racoon::hedgehog
{
using namespace juce;
using namespace racoon::ui;

namespace
{
const char* eqKeysUi[] { "on", "type", "freq", "gain", "q", "dyn", "thr", "range", "ms" };
const char* eqLabels[] { "Encendida", "Tipo", "Frecuencia", "Ganancia", "Q", "Dinámica", "Umbral", "Rango", "Canal" };
const char* mbKeysUi[] { "on", "thr", "ratio", "att", "rel", "gain" };
const char* mbLabels[] { "Comprime", "Umbral", "Ratio", "Ataque", "Soltar", "Ganancia" };
String lufsText (float v) { return v <= -69.0f ? String ("--") : String (v, 1); }
}

Display::Display (Processor& p) : proc (p)
{
    const std::pair<const char*, int> pages[] { { "PARTÍCULA", particlePage }, { "MEDIDORES", 0 }, { "BALANCE TONAL", 1 }, { "EQ DINÁMICO", 2 },
                                                { "MULTIBANDA", 3 }, { "ASISTENTE Y CONTROL", 4 } };
    for (auto [name, pg] : pages)
    {
        auto* b = tabs.add (new TextButton (String::fromUTF8 (name)));
        const int target = pg;
        tabPages.add (pg);
        b->setClickingTogglesState (false);
        b->onClick = [this, target] { showPage (target); };
        addAndMakeVisible (b);
    }
    history.reserve (600);
    showPage (particlePage);
    startTimerHz (10);
}

void Display::timerCallback()
{
    history.push_back (proc.shortTerm.load());
    if (history.size() > 600) history.erase (history.begin());
    repaint();
}

void Display::showPage (int p)
{
    page = p;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (tabPages[i] == page, dontSendNotification);
    rebuild();
    resized();
    repaint();
}

void Display::rebuild()
{
    controls.clear();
    buttons.clear();
    auto addCtl = [this] (const String& id, const String& label, const String& help = {})
    {
        addAndMakeVisible (controls.add (new ParamControl (proc, Ctl { id, label, help })));
    };
    auto addButton = [this] (const String& text, std::function<void()> f)
    {
        auto* b = buttons.add (new TextButton (text));
        b->onClick = std::move (f);
        addAndMakeVisible (b);
    };
    switch (page)
    {
        case particlePage:
            addCtl ("ingain", "Entrada", "Lo que entra. Busca que el RMS quede dentro de la marca verde.");
            addCtl ("pa_high", "Agudos", "Antes de la Partícula: aire."_u);
            addCtl ("pa_mid", "Medios", "Antes de la Partícula: cuerpo."_u);
            addCtl ("pa_low", "Graves", "Antes de la Partícula: calor."_u);
            addCtl ("pa_amount", "Cantidad", "Cuánto carácter: compresión de las tres bandas, saturación, aire y ancho. 100 % es el punto de partida."_u);
            addCtl ("pa_on", "Partícula"_u, "");
            addCtl ("pa_dyn", "Bandas", "Enciende o apaga la compresión de las tres bandas."_u);
            addCtl ("pa_sc", "Sidechain", "Interno: la propia mezcla decide cuánto comprimir. Externo: otra señal lo decide."_u);
            addCtl ("lim_gain", "Limitador", "Cuánto empuja el volumen contra el techo, sin pasarse de 0 dB."_u);
            addCtl ("outgain", "Salida", "Déjala en 0 dB con el limitador encendido."_u);
            break;
        case 1:
            addButton ("APRENDER MATCH EQ", [this] { proc.learnMatch(); proc.setRealValue ("match_on", 1.0f); repaint(); });
            addButton ("MEDIR DE NUEVO", [this] { proc.resetMeters(); history.clear(); });
            addCtl ("genre", "Género"_u); addCtl ("match_src", "Igualar a"); addCtl ("match_amt", "Cantidad");
            break;
        case 2:
            for (int k = 0; k < 9; ++k) addCtl (Processor::eqId (eqBand, eqKeysUi[k]), String::fromUTF8 (eqLabels[k]));
            break;
        case 3:
            for (int k = 0; k < 6; ++k) addCtl (Processor::mbId (mbBand, mbKeysUi[k]), String::fromUTF8 (mbLabels[k]));
            addCtl ("mb_on", "Multibanda"); addCtl ("mb_solo", "Escuchar");
            break;
        case 4:
            addButton ("MEDIR DE NUEVO", [this] { proc.resetMeters(); history.clear(); assistantLog.clear(); });
            addButton ("APLICAR SUGERENCIAS", [this] { assistantLog = proc.runAssistant(); repaint(); });
            addButton ("REPORTE", [this] { proc.writeReport().revealToUser(); });
            addButton ("EXPORTAR ARCHIVOS...", [this]
            {
                chooser = std::make_unique<FileChooser> ("Archivos para masterizar", File(), "*.wav;*.aif;*.aiff;*.flac");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                                      [this] (const FileChooser& fc) { if (fc.getResults().size() > 0) proc.startExport (fc.getResults()); });
            });
            for (int r = 0; r < 2; ++r)
                addButton ("REFERENCIA " + String (r + 1) + "...", [this, r]
                {
                    chooser = std::make_unique<FileChooser> ("Referencia", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                          [this, r] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadReference (r, fc.getResult()); repaint(); });
                });
            for (int v = 0; v < 3; ++v)
            {
                const String letter = String::charToString ((juce_wchar) ('A' + v));
                addButton ("GUARDAR " + letter, [this, v] { proc.saveVersion (v); repaint(); });
                addButton (letter, [this, v] { proc.recallVersion (v); repaint(); });
            }
            addCtl ("target", "Destino"); addCtl ("genre", "Género"_u);
            break;
        default:
            addButton ("MEDIR DE NUEVO", [this] { proc.resetMeters(); history.clear(); });
            addCtl ("target", "Destino"); addCtl ("ceiling", "Techo"); addCtl ("lim_gain", "Limitador"); addCtl ("listen", "Escuchar");
            break;
    }
}

void Display::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto tabRow = r.removeFromTop (26);
    const int tw = tabRow.getWidth() / tabs.size();
    for (auto* t : tabs) t->setBounds (tabRow.removeFromLeft (tw).reduced (2, 0));
    r.removeFromTop (8);
    content = r;
    controlsArea = {};
    graphArea = r;
    sideArea = {};
    if (page == particlePage) { layoutParticle(); return; }
    switch (page)
    {
        case 1:
        {
            sideArea = graphArea.removeFromRight (250);
            graphArea.removeFromRight (10);
            auto s = sideArea.reduced (6);
            for (auto* b : buttons) { b->setBounds (s.removeFromTop (28)); s.removeFromTop (6); }
            controlsArea = s.removeFromBottom (110);
            auto c = controlsArea.reduced (6).withTrimmedTop (4);
            for (int i = 0; i < controls.size(); ++i) controls[i]->setBounds (c.removeFromLeft (c.getWidth() / (controls.size() - i)));
            break;
        }
        case 2: case 3:
        {
            controlsArea = graphArea.removeFromBottom (104);
            graphArea.removeFromBottom (8);
            auto c = controlsArea.reduced (8, 6).withTrimmedTop (14);
            for (int i = 0; i < controls.size(); ++i) controls[i]->setBounds (c.removeFromLeft (c.getWidth() / (controls.size() - i)));
            break;
        }
        case 4:
        {
            sideArea = graphArea.removeFromRight (graphArea.getWidth() / 2);
            graphArea.removeFromRight (10);
            auto s = graphArea;
            auto row1 = s.removeFromTop (28); s.removeFromTop (6);
            auto row2 = s.removeFromTop (28); s.removeFromTop (6);
            auto row3 = s.removeFromTop (28); s.removeFromTop (6);
            for (int i = 0; i < 4; ++i) buttons[i]->setBounds (row1.removeFromLeft (row1.getWidth() / (4 - i)).reduced (2, 0));
            for (int i = 4; i < 6; ++i) buttons[i]->setBounds (row2.removeFromLeft (row2.getWidth() / (6 - i)).reduced (2, 0));
            for (int i = 6; i < 12; ++i) buttons[i]->setBounds (row3.removeFromLeft (row3.getWidth() / (12 - i)).reduced (2, 0));
            controlsArea = s.removeFromBottom (96);
            auto c = controlsArea.reduced (8, 6).withTrimmedTop (10);
            for (int i = 0; i < controls.size(); ++i) controls[i]->setBounds (c.removeFromLeft (c.getWidth() / (controls.size() - i)));
            graphArea = s;
            break;
        }
        default:
        {
            sideArea = graphArea.removeFromRight (340);
            graphArea.removeFromRight (10);
            auto s = sideArea;
            controlsArea = s.removeFromBottom (100);
            auto c = controlsArea.reduced (6).withTrimmedTop (6);
            for (int i = 0; i < controls.size(); ++i) controls[i]->setBounds (c.removeFromLeft (c.getWidth() / (controls.size() - i)));
            s.removeFromBottom (6);
            if (! buttons.isEmpty()) buttons[0]->setBounds (s.removeFromBottom (26).removeFromRight (160));
            break;
        }
    }
}

float Display::hzToX (float hz) const { return graphArea.getX() + graphArea.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float Display::xToHz (float x) const { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - graphArea.getX()) / (float) graphArea.getWidth())); }
float Display::dbToY (float db, float range) const { return graphArea.getCentreY() - db / range * graphArea.getHeight() * 0.5f; }

// ===================================================================== Partícula page
void Display::layoutParticle()
{
    auto r = content;
    graphArea = {};
    const int w = r.getWidth();
    const int widths[6] { w * 13 / 100, w * 12 / 100, w * 26 / 100, w * 20 / 100, w * 14 / 100, 0 };
    for (int i = 0; i < 5; ++i) { paCols[i] = r.removeFromLeft (widths[i]).reduced (4, 0); }
    paCols[5] = r.reduced (4, 0);
    auto ctl = [this] (const char* id) -> ParamControl* { for (auto* c : controls) if (c->spec.id == id) return c; return nullptr; };

    // input: meter and numbers on top, the input knob below
    {
        auto c = paCols[0].reduced (8).withTrimmedTop (22);
        if (auto* k = ctl ("ingain")) k->setBounds (c.removeFromBottom (96));
        paInMeter = c.removeFromLeft (26);
        c.removeFromLeft (6);
        paInPeak = c.removeFromTop (c.getHeight() / 2);
    }
    // pre EQ: three knobs stacked
    {
        auto c = paCols[1].reduced (6).withTrimmedTop (22);
        const int h = c.getHeight() / 3;
        for (auto id : { "pa_high", "pa_mid", "pa_low" }) if (auto* k = ctl (id)) k->setBounds (c.removeFromTop (h));
    }
    // AMOUNT: the big one
    {
        auto c = paCols[2].reduced (10).withTrimmedTop (22);
        if (auto* k = ctl ("pa_on")) k->setBounds (c.removeFromBottom (56).withSizeKeepingCentre (130, 56));
        if (auto* k = ctl ("pa_amount")) k->setBounds (c);
    }
    // gain reduction of the three bands
    {
        auto c = paCols[3].reduced (8).withTrimmedTop (22);
        auto bottom = c.removeFromBottom (56);
        if (auto* k = ctl ("pa_dyn")) k->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2));
        if (auto* k = ctl ("pa_sc")) k->setBounds (bottom);
        paGrMeters = c;
    }
    // limiter
    {
        auto c = paCols[4].reduced (8).withTrimmedTop (22);
        paLimText = c.removeFromBottom (44);
        if (auto* k = ctl ("lim_gain")) k->setBounds (c);
    }
    // output
    {
        auto c = paCols[5].reduced (8).withTrimmedTop (22);
        if (auto* k = ctl ("outgain")) k->setBounds (c.removeFromBottom (96));
        paOutMeter = c.removeFromLeft (26);
        c.removeFromLeft (6);
        paOutPeak = c.removeFromTop (c.getHeight() / 2);
    }
}

void Display::paintParticle (Graphics& g)
{
    const char* titles[6] { "Entrada", "EQ", "Cantidad", "Bandas", "Limitador", "Salida" };
    for (int i = 0; i < 6; ++i) Draw::panel (g, paCols[i].toFloat(), titles[i]);

    // a level meter: dark well, RMS bar, peak line and the green target
    auto meter = [&g] (Rectangle<int> area, float rmsDb, float peakDb, float targetLo, float targetHi)
    {
        auto r = area.toFloat();
        g.setColour (C::void_);
        g.fillRect (r);
        auto yOf = [&r] (float db) { return r.getBottom() - r.getHeight() * jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };
        g.setColour (C::jade.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (r.getX(), yOf (targetHi), r.getWidth(), yOf (targetLo) - yOf (targetHi)));
        g.setColour (peakDb > -0.05f ? C::ochre : C::amber);
        g.fillRect (r.reduced (5.0f, 0.0f).withTop (yOf (rmsDb)));
        if (peakDb > -60.0f)
        {
            g.setColour (peakDb > -0.05f ? C::ochre : C::bone);
            g.fillRect (r.getX(), yOf (peakDb) - 1.0f, r.getWidth(), 2.0f);
        }
    };
    auto numbers = [&g] (Rectangle<int> area, float peak, float rms)
    {
        auto r = area.toFloat();
        auto txt = [] (float v) { return v <= -99.0f ? String ("-inf") : String (v, 1); };
        g.setColour (S::inkSoft);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("PICO"), r.removeFromTop (14.0f), Justification::centredLeft);
        auto pk = r.removeFromTop (26.0f);
        g.setColour (peak > -0.05f ? C::ochre : C::void_);
        g.fillRect (pk);
        g.setColour (peak > -0.05f ? S::ink : C::bone);
        g.setFont (Fonts::mono (15.0f));
        g.drawText (::racoon::i18n::tr (txt (peak)), pk, Justification::centred);
        r.removeFromTop (6.0f);
        g.setColour (S::inkSoft);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("RMS"), r.removeFromTop (14.0f), Justification::centredLeft);
        g.setColour (S::ink);
        g.setFont (Fonts::mono (15.0f));
        g.drawText (::racoon::i18n::tr (txt (rms)), r.removeFromTop (22.0f), Justification::centredLeft);
    };

    // the green zone on the input is where a mix usually sits before it enters the chain
    meter (paInMeter, proc.inRms.load(), proc.inPeakNow.load(), -18.0f, -13.0f);
    numbers (paInPeak, proc.inPeakHold.load(), proc.inRms.load());
    meter (paOutMeter, proc.outRms.load(), proc.outPeakNow.load(), -11.0f, -7.0f);
    numbers (paOutPeak, proc.outPeakHold.load(), proc.outRms.load());

    // the three bands: reduction grows down from the top, the green mark is where it usually lands at 100 %
    {
        auto r = paGrMeters.toFloat();
        const float bw = r.getWidth() / 3.0f;
        const char* names[3] { "GRAVES", "MEDIOS", "AGUDOS" };
        for (int b = 0; b < 3; ++b)
        {
            auto col = r.removeFromLeft (bw).reduced (8.0f, 0.0f);
            auto label = col.removeFromBottom (16.0f);
            auto val = col.removeFromTop (20.0f);
            const float grDb = proc.particleGr[b].load();
            g.setColour (C::void_);
            g.fillRect (col);
            const float scale = 12.0f;
            auto yOf = [&col, scale] (float db) { return col.getY() + col.getHeight() * jlimit (0.0f, 1.0f, db / scale); };
            g.setColour (C::amber);
            g.fillRect (col.reduced (5.0f, 0.0f).withBottom (yOf (grDb)));
            g.setColour (C::jade);
            g.fillRect (col.getX(), yOf (Particle::targetGr[b]) - 1.0f, col.getWidth(), 3.0f);
            g.setColour (S::ink);
            g.setFont (Fonts::mono (14.0f));
            g.drawText (::racoon::i18n::tr (grDb > 0.05f ? "-" + String (grDb, 1) : String ("0.0")), val, Justification::centred);
            g.setColour (S::inkSoft);
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr (names[b]), label, Justification::centred);
        }
    }

    // limiter reduction
    {
        auto r = paLimText.toFloat();
        const float gr = proc.limiterGr.load();
        g.setColour (S::inkSoft);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("REDUCCIÓN")), r.removeFromTop (14.0f), Justification::centred);
        g.setColour (S::ink);
        g.setFont (Fonts::mono (16.0f));
        g.drawText (::racoon::i18n::tr ((gr > 0.05f ? "-" + String (gr, 1) : String ("0.0")) + " dB"), r, Justification::centred);
    }
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    if (page == particlePage) { paintParticle (g); return; }
    if (! controlsArea.isEmpty()) Draw::panel (g, controlsArea.toFloat());
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    if (page == 0)
    {
        // loudness over time and the numbers
        auto r = graphArea.toFloat();
        drawScreenGrid (g, r, 10, 6);
        const float target = platformLufs ((int) proc.value ("target"));
        auto yOf = [&] (float lufs) { return r.getY() + r.getHeight() * (1.0f - (jlimit (-40.0f, 0.0f, lufs) + 40.0f) / 40.0f); };
        g.setColour (C::ochre.withAlpha (0.7f));
        g.drawHorizontalLine ((int) yOf (target), r.getX(), r.getRight());
        g.setFont (Fonts::mono (11.0f));
        g.drawText (::racoon::i18n::tr ("destino " + String (target, 0) + " LUFS"), r.withY (yOf (target) - 14.0f).withHeight (13.0f), Justification::right);
        Path p;
        for (size_t i = 0; i < history.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 600.0f, y = yOf (history[i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        Draw::glowPath (g, p, C::amber);
        g.setColour (C::boneDim);
        for (int db : { -10, -20, -30 }) g.drawText (::racoon::i18n::tr (String (db)), Rectangle<float> (r.getX() + 4.0f, yOf ((float) db) - 7.0f, 40.0f, 14.0f), Justification::left);

        auto s = sideArea.toFloat().withTrimmedBottom (140.0f);
        struct Row { const char* name; String value; Colour c; };
        const float tp = proc.truePeakMax.load(), lufs = proc.integrated.load();
        const Row rows[] {
            { "INTEGRADO", lufsText (lufs) + " LUFS", C::amberHot },
            { "CORTO (3 s)", lufsText (proc.shortTerm.load()) + " LUFS", C::bone },
            { "MOMENTÁNEO", lufsText (proc.momentary.load()) + " LUFS", C::bone },
            { "RANGO (LRA)", String (proc.lra.load(), 1) + " LU", C::bone },
            { "PICO REAL", (tp < -99.0f ? String ("--") : String (tp, 1)) + " dBTP", tp > proc.value ("ceiling") + 0.1f ? C::ochre : C::cyan },
            { "PLR", lufs > -69.0f ? String (tp - lufs, 1) + " dB" : String ("--"), C::bone },
            { "FASE", String (proc.correlation.load(), 2), proc.correlation.load() < 0.0f ? C::ochre : C::jade },
            { "LIMITADOR", (proc.limiterGr.load() > 0.05f ? "-" + String (proc.limiterGr.load(), 1) : String ("0.0")) + " dB", C::bone },
        };
        const float rh = s.getHeight() / (float) std::size (rows);
        for (auto& row : rows)
        {
            auto line = s.removeFromTop (rh);
            g.setColour (C::boneDim);
            g.setFont (Fonts::label (12.0f));
            g.drawText (::racoon::i18n::tr (String::fromUTF8 (row.name)), line.removeFromLeft (120.0f), Justification::centredLeft);
            g.setColour (row.c);
            g.setFont (Fonts::title (jmin (22.0f, rh * 0.7f)));
            g.drawText (::racoon::i18n::tr (row.value), line, Justification::centredRight);
        }
        return;
    }

    if (page == 1)
    {
        // the long-term spectrum against the genre and the reference
        auto r = graphArea.toFloat();
        drawScreenGrid (g, r, 10, 6);
        float out[numThirds], tgt[numThirds], ref[numThirds];
        for (int b = 0; b < numThirds; ++b) { out[b] = proc.specOut[b].load(); tgt[b] = targetDb ((int) proc.value ("genre"), b); }
        proc.referenceSpectrum (0, ref);
        auto norm = [] (float* v)
        {
            double s = 0.0; int k = 0;
            for (int b = 0; b < numThirds; ++b) { const float c = ThirdOctave::centre (b); if (c >= 200.0f && c <= 5000.0f && v[b] > -99.0f) { s += v[b]; ++k; } }
            const float m = k > 0 ? (float) (s / k) : 0.0f;
            for (int b = 0; b < numThirds; ++b) v[b] = v[b] > -99.0f ? v[b] - m : -100.0f;
        };
        norm (out); norm (tgt); norm (ref);
        const float range = 24.0f;
        auto yOf = [&] (float db) { return r.getCentreY() - jlimit (-range, range, db) / range * r.getHeight() * 0.5f; };
        const float bw = r.getWidth() / numThirds;
        for (int b = 0; b < numThirds; ++b)
        {
            if (out[b] < -99.0f) continue;
            const float d = out[b] - tgt[b];
            const auto bar = Rectangle<float> (r.getX() + b * bw + 2.0f, jmin (yOf (out[b]), r.getCentreY()), bw - 4.0f, std::abs (yOf (out[b]) - r.getCentreY()));
            g.setColour ((std::abs (d) < 3.0f ? C::jade : C::ochre).withAlpha (0.75f));
            g.fillRect (bar);
        }
        auto curve = [&] (const float* v, Colour c)
        {
            Path p; bool started = false;
            for (int b = 0; b < numThirds; ++b)
            {
                if (v[b] < -99.0f) continue;
                const float x = r.getX() + (b + 0.5f) * bw, y = yOf (v[b]);
                if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
            }
            Draw::glowPath (g, p, c);
        };
        curve (tgt, C::amber);
        if (proc.referenceName (0).isNotEmpty()) curve (ref, C::cyan);
        float m[numThirds];
        for (int b = 0; b < numThirds; ++b) m[b] = proc.matchDiff[b].load() * (proc.value ("match_on") > 0.5f ? proc.value ("match_amt") * 0.01f : 0.0f);
        curve (m, C::bone.withAlpha (0.6f));
        g.setFont (Fonts::label (11.0f));
        g.setColour (C::amber); g.drawText (::racoon::i18n::tr ("CURVA DE " + upper (genreNames()[(int) proc.value ("genre")])), r.reduced (6).removeFromTop (14), Justification::left);
        g.setColour (C::cyan); if (proc.referenceName (0).isNotEmpty()) g.drawText (::racoon::i18n::tr ("REFERENCIA: " + proc.referenceName (0)), r.reduced (6).withTrimmedTop (16).removeFromTop (14), Justification::left);
        g.setColour (C::bone); g.drawText (::racoon::i18n::tr ("BARRAS: TU MASTER  ·  BLANCO: MATCH EQ"_u), r.reduced (6).removeFromTop (14), Justification::right);
        for (int hz : { 50, 200, 1000, 5000, 15000 })
        {
            const int b = roundToInt (3.0f * std::log2 (hz / 20.0f));
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (hz >= 1000 ? String (hz / 1000) + "k" : String (hz)), Rectangle<float> (r.getX() + b * bw - 10.0f, r.getBottom() - 28.0f, bw + 20.0f, 12.0f), Justification::centred);
        }
        return;
    }

    if (page == 2)
    {
        // the response of the 12 bands
        auto r = graphArea.toFloat();
        drawScreenGrid (g, r, 10, 6);
        const float range = 18.0f;
        Biquad bands[numEqBands];
        bool on[numEqBands];
        for (int b = 0; b < numEqBands; ++b)
        {
            on[b] = proc.value (Processor::eqId (b, "on")) > 0.5f;
            const int type = (int) proc.value (Processor::eqId (b, "type"));
            const float q = proc.value (Processor::eqId (b, "q")), gain = proc.value (Processor::eqId (b, "gain")) + proc.eqDyn[b].load();
            const Biquad::Type t = type == 1 ? Biquad::lowShelf : type == 2 ? Biquad::highShelf : type == 3 ? Biquad::highpass : type == 4 ? Biquad::lowpass : Biquad::peak;
            bands[b].set (t, sr, proc.value (Processor::eqId (b, "freq")), type == 1 || type == 2 ? jmin (2.0f, q) * 0.7f : q, gain);
        }
        Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = r.getX() + r.getWidth() * i / 200.0f, hz = xToHz (x);
            float db = 0.0f;
            for (int b = 0; b < numEqBands; ++b) if (on[b]) db += bands[b].magnitudeDb (hz, sr);
            const float y = dbToY (jlimit (-range, range, db), range);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (C::ridge);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        Draw::glowPath (g, p, C::amber);
        for (int b = 0; b < numEqBands; ++b)
        {
            const float x = hzToX (proc.value (Processor::eqId (b, "freq")));
            const float y = dbToY (proc.value (Processor::eqId (b, "gain")), range);
            const auto c = Rectangle<float> (16.0f, 16.0f).withCentre ({ x, y });
            g.setColour (b == eqBand ? C::amberHot : on[b] ? C::cyan : C::ash);
            if (on[b]) g.fillEllipse (c); else g.drawEllipse (c, 1.5f);
            g.setColour (C::void_);
            g.setFont (Fonts::label (10.0f));
            if (on[b]) g.drawText (::racoon::i18n::tr (String (b + 1)), c, Justification::centred);
            else { g.setColour (C::ash); g.drawText (::racoon::i18n::tr (String (b + 1)), c, Justification::centred); }
            const float dyn = proc.eqDyn[b].load();
            if (on[b] && std::abs (dyn) > 0.1f)
            {
                g.setColour (C::ochre);
                g.drawLine (x, y, x, dbToY (proc.value (Processor::eqId (b, "gain")) + dyn, range), 2.0f);
            }
        }
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("ARRASTRA UN PUNTO: FRECUENCIA Y GANANCIA. DOBLE CLIC: ENCENDER O APAGAR LA BANDA"), r.reduced (6).removeFromTop (14), Justification::left);
        g.setColour (C::bone);
        g.drawText (::racoon::i18n::tr ("BANDA " + String (eqBand + 1)), controlsArea.reduced (12, 2).removeFromTop (14), Justification::left);
        return;
    }

    if (page == 3)
    {
        auto r = graphArea.toFloat();
        const float bw = r.getWidth() / numMbBands;
        float edges[numMbBands + 1] { 20.0f, proc.value ("mb_x1"), proc.value ("mb_x2"), proc.value ("mb_x3"), proc.value ("mb_x4"), proc.value ("mb_x5"), 20000.0f };
        for (int b = 0; b < numMbBands; ++b)
        {
            auto col = Rectangle<float> (r.getX() + b * bw, r.getY(), bw, r.getHeight()).reduced (4.0f);
            g.setColour (b == mbBand ? C::stoneHi : C::stone);
            g.fillRect (col);
            const float grDb = proc.mbGr[b].load();
            const float h = col.getHeight() - 40.0f;
            g.setColour (C::ochre);
            g.fillRect (col.withTrimmedTop (24.0f).withHeight (h * jlimit (0.0f, 1.0f, grDb / 18.0f)).reduced (col.getWidth() * 0.3f, 0.0f));
            const float thr = proc.value (Processor::mbId (b, "thr"));
            g.setColour (C::amber);
            const float ty = col.getY() + 24.0f + h * (-thr / 60.0f);
            g.drawLine (col.getX() + 6.0f, ty, col.getRight() - 6.0f, ty, 1.5f);
            g.setColour (C::bone);
            g.setFont (Fonts::label (12.0f));
            auto fmt = [] (float hz) { return hz >= 1000.0f ? String (hz / 1000.0f, 1) + "k" : String (roundToInt (hz)); };
            g.drawText (::racoon::i18n::tr (fmt (edges[b]) + " - " + fmt (edges[b + 1])), col.removeFromTop (20.0f), Justification::centred);
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr ((grDb > 0.05f ? "-" + String (grDb, 1) : String ("0.0")) + " dB"), col.removeFromBottom (16.0f), Justification::centred);
        }
        g.setColour (C::bone);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("BANDA " + String (mbBand + 1) + " (CLIC EN UNA COLUMNA PARA ELEGIR)"), controlsArea.reduced (12, 2).removeFromTop (14), Justification::left);
        return;
    }

    // assistant and quality control
    {
        auto r = graphArea.toFloat().reduced (2.0f);
        g.setColour (C::bone);
        g.setFont (Fonts::label (12.0f));
        g.drawText (::racoon::i18n::tr ("ASISTENTE"), r.removeFromTop (16.0f), Justification::left);
        g.setFont (Fonts::body (12.0f));
        StringArray lines = assistantLog;
        if (lines.isEmpty()) lines.add ("Toca la parte más fuerte de la canción unos segundos y pulsa APLICAR SUGERENCIAS."_u);
        if (proc.exportProgress.load() >= 0.0f) lines.add ("Exportando... " + String (roundToInt (proc.exportProgress.load() * 100.0f)) + " %");
        else if (proc.lastExportMessage.isNotEmpty()) lines.add ("Exportados: " + proc.lastExportMessage);
        for (int i = 0; i < 2; ++i)
            if (proc.referenceName (i).isNotEmpty()) lines.add ("Referencia " + String (i + 1) + ": " + proc.referenceName (i) + "  (" + String (proc.referenceLufs (i), 1) + " LUFS)");
        String vers = "Versiones:";
        for (int v = 0; v < 3; ++v)
            vers << "  " << String::charToString ((juce_wchar) ('A' + v)) << (proc.hasVersion (v) ? " " + String (proc.versionLufs (v), 1) + " LUFS" : String (" vacía"_u))
                 << (proc.activeVersion == v ? " (activa)" : "");
        lines.add (vers);
        for (auto& l : lines)
        {
            g.setColour (C::bone);
            const int h = l.length() > 70 ? 30 : 16;
            g.drawFittedText (::racoon::i18n::tr (l), r.removeFromTop ((float) h).toNearestInt(), Justification::topLeft, 2);
            r.removeFromTop (3.0f);
        }

        auto s = sideArea.toFloat().reduced (4.0f);
        g.setFont (Fonts::label (12.0f));
        g.setColour (C::bone);
        g.drawText (::racoon::i18n::tr ("CONTROL DE CALIDAD"), s.removeFromTop (18.0f), Justification::left);
        g.setFont (Fonts::body (12.0f));
        for (auto& c : proc.qualityChecks())
        {
            auto line = s.removeFromTop (22.0f);
            g.setColour (c.ok ? C::jade : C::ochre);
            g.fillEllipse (line.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 9.0f));
            line.removeFromLeft (6.0f);
            g.setColour (C::bone);
            g.drawText (::racoon::i18n::tr (c.text), line, Justification::centredLeft);
        }
    }
}

void Display::mouseDown (const MouseEvent& e)
{
    if (page == particlePage)
    {
        if (paInPeak.contains (e.getPosition()) || paOutPeak.contains (e.getPosition()) || paInMeter.contains (e.getPosition())
             || paOutMeter.contains (e.getPosition()))
            proc.resetPeaks();
        return;
    }
    dragBand = -1;
    const auto pos = e.position;
    if (page == 2 && graphArea.contains (e.getPosition()))
    {
        float best = 400.0f;
        for (int b = 0; b < numEqBands; ++b)
        {
            const float x = hzToX (proc.value (Processor::eqId (b, "freq"))), y = dbToY (proc.value (Processor::eqId (b, "gain")), 18.0f);
            const float d = pos.getDistanceFrom ({ x, y });
            if (d < 14.0f && d < best) { best = d; dragBand = b; }
        }
        if (dragBand >= 0 && dragBand != eqBand) { eqBand = dragBand; rebuild(); resized(); }
        repaint();
    }
    else if (page == 3 && graphArea.contains (e.getPosition()))
    {
        const int b = jlimit (0, numMbBands - 1, (int) ((pos.x - graphArea.getX()) / (graphArea.getWidth() / (float) numMbBands)));
        if (b != mbBand) { mbBand = b; rebuild(); resized(); repaint(); }
    }
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (page != 2 || dragBand < 0) return;
    proc.setRealValue (Processor::eqId (dragBand, "freq"), jlimit (20.0f, 20000.0f, xToHz (e.position.x)));
    const float db = -(e.position.y - graphArea.getCentreY()) / (graphArea.getHeight() * 0.5f) * 18.0f;
    proc.setRealValue (Processor::eqId (dragBand, "gain"), jlimit (-18.0f, 18.0f, db));
    if (proc.value (Processor::eqId (dragBand, "on")) < 0.5f) proc.setRealValue (Processor::eqId (dragBand, "on"), 1.0f);
    repaint();
}

void Display::mouseDoubleClick (const MouseEvent&)
{
    if (page == 2 && dragBand >= 0)
        proc.setRealValue (Processor::eqId (dragBand, "on"), proc.value (Processor::eqId (dragBand, "on")) > 0.5f ? 0.0f : 1.0f);
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y una mezcla de ejemplo pasa por la Partícula."_u, "Sube Cantidad para más pegada y deja las barras cerca de la marca verde."_u, "Sube el Limitador hasta el volumen que quieras, sin pasar de 0 dB."_u, "En tu DAW ponlo en el Master y apaga Sonido de ejemplo."_u };
    s.sections = {
        { "Destino", { { "target", "Destino", "Volumen y techo pensados para cada plataforma." }, { "ceiling", "Techo", "Pico real máximo (dBTP), medido con sobremuestreo 4x."_u },
                       { "lim_on", "Limitador", "" }, { "lim_rel", "Soltar", "" } } },
        { "Comparar", { { "refplay", "Escuchar", "Tu master o una referencia, con el mismo volumen." }, { "refmatch", "Igualar", "" },
                        { "listen", "Monitor", "Estéreo, mono, solo centro o solo lados."_u } } },
        { "Color", { { "ex_on", "Excitador", "" }, { "ex_drive", "Drive", "" }, { "tr_on", "Transitorios", "" }, { "tr_att", "Ataque", "Más o menos golpe."_u } }, true },
        { "Espacio", { { "im_on", "Imagen", "" }, { "im_mid", "Medios", "Ancho de los medios." }, { "im_high", "Agudos", "Ancho de los agudos." },
                       { "lo_on", "Graves", "" }, { "lo_mono", "Mono hasta", "Los graves en mono hasta esa frecuencia." } }, true },
        { "Género"_u, { { "genre", "Género"_u, "La curva de balance tonal con la que se compara."_u } }, true },
        { "Resonancias", { { "res_on", "Encendido", "Baja picos finos y molestos que aparecen y desaparecen." }, { "res_depth", "Cantidad", "" },
                           { "res_sharp", "Finura", "" }, { "res_lo", "Desde", "" }, { "res_hi", "Hasta", "" }, { "res_speed", "Velocidad", "" } }, true },
        { "Match y EQ", { { "match_on", "Match EQ", "Iguala la curva a la del género o a una referencia (APRENDER en la pantalla)."_u }, { "match_amt", "Cantidad", "" },
                          { "match_src", "Igualar a", "" }, { "eq_att", "EQ ataque", "" }, { "eq_rel", "EQ soltar", "" } }, true },
        { "Graves y transitorios", { { "lo_freq", "Graves hasta", "" }, { "lo_punch", "Golpe", "" }, { "lo_tight", "Firmeza", "" }, { "tr_sus", "Cuerpo", "" } }, true },
        { "Excitador e imagen", { { "ex_freq", "Desde", "" }, { "ex_mix", "Mezcla", "" }, { "ex_mode", "Color", "" }, { "im_low", "Ancho graves", "" } }, true },
        { "Multibanda", { { "mb_on", "Encendido", "" }, { "mb_solo", "Escuchar", "" }, { "mb_x1", "Corte 1", "" }, { "mb_x2", "Corte 2", "" },
                          { "mb_x3", "Corte 3", "" }, { "mb_x4", "Corte 4", "" }, { "mb_x5", "Corte 5", "" } }, true },
    };
    s.width = 1240;
    s.displayEasy = 430;
    s.displayFull = 360;
    s.easyHeight = 720;
    s.advancedHeight = 200;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

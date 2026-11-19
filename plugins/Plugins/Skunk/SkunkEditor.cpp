#include "../../Shared/PanelMuestras.h"
#include "I18n.h"
#include "SkunkEditor.h"
#include "../../Shared/Biblioteca.h"

namespace racoon::skunk
{
using namespace juce;
using namespace racoon::ui;

// ===================================================================== XY pad
XYPad::XYPad (Processor& p) : proc (p)
{
    px = dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter ("xy_x"));
    py = dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter ("xy_y"));
    startTimerHz (25);
}

void XYPad::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (C::stone);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (C::boneDim.withAlpha (0.3f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight());
    }
    const float x = r.getX() + r.getWidth() * (px != nullptr ? px->getValue() : 0.5f);
    const float y = r.getBottom() - r.getHeight() * (py != nullptr ? py->getValue() : 0.5f);
    g.setColour (C::amber.withAlpha (0.3f));
    g.fillEllipse (x - 16, y - 16, 32, 32);
    g.setColour (C::amber);
    g.fillEllipse (x - 7, y - 7, 14, 14);
    g.setColour (C::boneDim);
    g.setFont (Fonts::label (10.0f));
    g.drawText (::racoon::i18n::tr ("PAD XY  (fuentes Pad X y Pad Y de la matriz)"), r.reduced (6).removeFromBottom (12), Justification::centredLeft);
}

void XYPad::set (Point<float> p)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    if (px != nullptr) px->setValueNotifyingHost (jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()));
    if (py != nullptr) py->setValueNotifyingHost (jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()));
}

void XYPad::mouseDown (const MouseEvent& e)
{
    if (px != nullptr) px->beginChangeGesture();
    if (py != nullptr) py->beginChangeGesture();
    set (e.position);
}
void XYPad::mouseDrag (const MouseEvent& e) { set (e.position); }
void XYPad::mouseUp (const MouseEvent&)
{
    if (px != nullptr) px->endChangeGesture();
    if (py != nullptr) py->endChangeGesture();
}

// ===================================================================== route row
RouteRow::RouteRow (Processor& p, int r) : route (r), proc (p)
{
    src.addItemList (sourceNames(), 1);
    dst.addItemList (destinationNames(), 1);
    for (auto* sl : { &depth, &curve, &smooth })
    {
        sl->setSliderStyle (Slider::LinearBar);
        sl->setTextBoxStyle (Slider::TextBoxLeft, false, 0, 0);
        addAndMakeVisible (sl);
    }
    addAndMakeVisible (src);
    addAndMakeVisible (dst);
    src.onPopupMenu = [this] { racoon::showParamMenu (*this, Processor::routeId (route, "src")); };
    dst.onPopupMenu = [this] { racoon::showParamMenu (*this, Processor::routeId (route, "dst")); };
    depth.onPopupMenu = [this] { racoon::showParamMenu (*this, Processor::routeId (route, "depth")); };
    curve.onPopupMenu = [this] { racoon::showParamMenu (*this, Processor::routeId (route, "curve")); };
    smooth.onPopupMenu = [this] { racoon::showParamMenu (*this, Processor::routeId (route, "smooth")); };
    aSrc = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, Processor::routeId (r, "src"), src);
    aDst = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, Processor::routeId (r, "dst"), dst);
    aDepth = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, Processor::routeId (r, "depth"), depth);
    aCurve = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, Processor::routeId (r, "curve"), curve);
    aSmooth = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, Processor::routeId (r, "smooth"), smooth);
    depth.setTooltip ("Cantidad: cuánto mueve el destino (negativo, al revés)."_u);
    curve.setTooltip ("Curva: el movimiento crece lento y acelera al final (o al revés)."_u);
    smooth.setTooltip ("Suavizado: quita saltos bruscos (milisegundos).");
}

void RouteRow::resized()
{
    auto r = getLocalBounds().reduced (2, 3);
    r.removeFromLeft (30);
    src.setBounds (r.removeFromLeft (200)); r.removeFromLeft (6);
    dst.setBounds (r.removeFromLeft (240)); r.removeFromLeft (10);
    const int w = (r.getWidth() - 20) / 3;
    depth.setBounds (r.removeFromLeft (w)); r.removeFromLeft (10);
    curve.setBounds (r.removeFromLeft (w)); r.removeFromLeft (10);
    smooth.setBounds (r);
}

void RouteRow::paint (Graphics& g)
{
    g.setColour (C::boneDim);
    g.setFont (Fonts::label (12.0f));
    g.drawText (::racoon::i18n::tr (String (route + 1)), getLocalBounds().removeFromLeft (28), Justification::centred);
}

// ===================================================================== display
Display::Display (Processor& p) : proc (p), xy (p)
{
    for (auto* name : { "SONIDO", "MATRIZ", "MODULADORES", "SECUENCIAS" })
    {
        auto* b = tabs.add (new TextButton (name));
        const int idx = tabs.size() - 1;
        b->setClickingTogglesState (false);
        b->onClick = [this, idx] { showPage (idx); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &recapture, &toCapture, &saveWav, &loadButton }) addChildComponent (b);
    // the library: what Skunk can play right away, without recording anything
    library.addItemList (Processor::libraryNames(), 1);
    library.setTooltip ("Muestras: materiales listos para tocar con notas MIDI (afinados en Do). Elige uno y toca el teclado o un clip; con «Mi captura o mi archivo» usa lo que grabaste o cargaste."_u);
    addChildComponent (library);
    aLibrary = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "lib", library);
    addChildComponent (xy);
    recapture.setTooltip ("Graba lo que le llega a Skunk desde su canal (vacía la captura y empieza otra vez; en Un disparo espera el umbral o una nota)."_u);
    toCapture.setTooltip ("Lo que grabaste de la salida se vuelve el material de los granos.");
    saveWav.setTooltip ("Clic: guarda lo grabado de la salida en ~/Música/Racoon/Skunk. Arrástralo a una pista de Racoon."_u);
    loadButton.setTooltip ("Carga un WAV, AIFF o FLAC como material (también puedes soltarlo sobre la pantalla), o una textura de Muestras Racoon."_u);
    loadButton.onClick = [this]
    {
        PopupMenu m, lib;
        m.addItem ("Archivo...", [this]
        {
            chooser = std::make_unique<FileChooser> ("Elige un audio", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) proc.loadFile (fc.getResult());
            });
        });
        // texturas, voces, efectos y loops de la biblioteca: material real para los granos
        racoon::biblio::menuBiblioteca (lib, { "textura", "voz", "efecto", "loop", "campana", "orquesta" }, [this] (const String& grupo)
        {
            const auto& c = racoon::biblio::Catalogo::get();
            if (const auto* g = c.grupo (grupo); g != nullptr && ! g->muestras.empty())
            {
                int best = g->muestras.front();
                for (int idx : g->muestras) if (c.muestras[(size_t) idx].rr == 1) best = idx;
                if (auto* p = proc.apvts.getParameter ("lib")) p->setValueNotifyingHost (0.0f);
                proc.loadFile (c.archivo (c.muestras[(size_t) best].archivo));
            }
        });
        m.addSubMenu ("Muestras Racoon", lib, racoon::biblio::Catalogo::get().disponible());
        m.addItem ("Muestras Racoon en un panel..."_u, racoon::biblio::Catalogo::get().disponible(), false, [this]
        {
            racoon::biblio::PanelMuestras::mostrar (*this, "Muestras para los granos", { "textura", "voz", "efecto", "loop", "campana", "orquesta" }, [this] (const String& grupo)
            {
                const auto& c = racoon::biblio::Catalogo::get();
                if (const auto* g = c.grupo (grupo); g != nullptr && ! g->muestras.empty())
                {
                    int best = g->muestras.front();
                    for (int idx : g->muestras) if (c.muestras[(size_t) idx].rr == 1) best = idx;
                    if (auto* p = proc.apvts.getParameter ("lib")) p->setValueNotifyingHost (0.0f);
                    proc.loadFile (c.archivo (c.muestras[(size_t) best].archivo));
                }
            });
        });
        if (! racoon::biblio::Catalogo::get().disponible()) m.addItem (racoon::biblio::avisoSinBiblioteca(), false, false, [] {});
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&loadButton));
    };
    recapture.onClick = [this] { proc.requestRecapture(); };
    toCapture.onClick = [this] { proc.requestResampleToCapture(); };
    saveWav.onClick = [this] { lastWav = proc.saveResampleWav(); if (lastWav.existsAsFile()) lastWav.revealToUser(); };
    saveWav.addMouseListener (this, false);
    showPage (0);
    startTimerHz (25);
}

void Display::showPage (int p)
{
    page = p;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == page, dontSendNotification);
    rebuildPage();
}

void Display::rebuildPage()
{
    pageControls.clear();
    pickers.clear();
    routeRows.clear();
    panels.clear();
    panelTitles.clear();
    const bool sound = page == 0;
    for (auto* b : { &recapture, &toCapture, &saveWav, &loadButton }) b->setVisible (sound);
    library.setVisible (sound);
    xy.setVisible (sound);
    auto addCtl = [this] (const String& id, const String& label, const String& help = {})
    {
        addAndMakeVisible (pageControls.add (new ParamControl (proc, Ctl { id, label, help })));
    };
    auto addPickers = [this] (int count, int selected, std::function<void (int)> pick, const String& prefix)
    {
        for (int i = 0; i < count; ++i)
        {
            auto* b = pickers.add (new TextButton (prefix + String (i + 1)));
            b->setToggleState (i == selected, dontSendNotification);
            b->onClick = [pick, i] { pick (i); };
            addAndMakeVisible (b);
        }
    };
    switch (page)
    {
        case 0:
            addCtl ("rsrec", "Grabar salida", "Graba lo que sale de Skunk para volverlo captura o archivo.");
            break;
        case 1:
            addPickers (8, routePage, [this] (int i) { routePage = i; rebuildPage(); }, "Rutas ");
            for (int r = routePage * 8; r < routePage * 8 + 8; ++r) addAndMakeVisible (routeRows.add (new RouteRow (proc, r)));
            break;
        case 2:
            addPickers (numLfos, lfoPick, [this] (int i) { lfoPick = i; rebuildPage(); }, "LFO ");
            addPickers (numEnvs, envPick, [this] (int i) { envPick = i; rebuildPage(); }, "ENV ");
            addCtl (Processor::lfoId (lfoPick, "rate"), "Velocidad"_u);
            addCtl (Processor::lfoId (lfoPick, "shape"), "Forma");
            addCtl (Processor::lfoId (lfoPick, "sync"), "Al tempo");
            addCtl (Processor::envId (envPick, "att"), "Ataque");
            addCtl (Processor::envId (envPick, "dec"), "Caída"_u);
            addCtl (Processor::envId (envPick, "sus"), "Sostenido");
            addCtl (Processor::envId (envPick, "rel"), "Relajación"_u);
            addCtl (Processor::envId (envPick, "trig"), "Disparo", "Con el teclado, con cada transitorio del audio o con los dos.");
            for (int m = 0; m < numMacros; ++m) addCtl ("macro" + String (m + 1), "Macro " + String (m + 1));
            break;
        default:
            addPickers (numSeqs, seqPick, [this] (int i) { seqPick = i; rebuildPage(); }, "SEC ");
            addCtl (Processor::seqId (seqPick, "div"), "Rejilla");
            addCtl (Processor::seqId (seqPick, "len"), "Pasos");
            addCtl (Processor::seqId (seqPick, "glide"), "Deslizar");
            break;
    }
    resized();
}

void Display::resized()
{
    auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4).reduced (12, 10);
    auto top = r.removeFromTop (24);
    for (auto* t : tabs) { t->setBounds (top.removeFromLeft (130)); top.removeFromLeft (6); }
    r.removeFromTop (8);
    statusArea = r.removeFromBottom (18);
    switch (page)
    {
        case 0:
        {
            auto right = r.removeFromRight (250);
            xy.setBounds (right.removeFromTop (right.getHeight() - 80));
            right.removeFromTop (6);
            auto row = right;
            panels = { row };
            panelTitles = { "Remuestreo" };
            if (! pageControls.isEmpty()) pageControls[0]->setBounds (row.reduced (8, 4).withTrimmedTop (18).removeFromLeft (130));
            r.removeFromRight (10);
            auto buttons = r.removeFromBottom (28);
            libraryCaption = buttons.removeFromLeft (220).withHeight (12).translated (0, -14);
            library.setBounds (libraryCaption.withY (buttons.getY()).withHeight (28));
            buttons.removeFromLeft (8);
            const int bw = (buttons.getWidth() - 18) / 4;
            for (auto* b : { &loadButton, &recapture, &toCapture, &saveWav }) { b->setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (6); }
            r.removeFromBottom (6);
            waveArea = r.removeFromTop (r.getHeight() / 2);
            r.removeFromTop (6);
            specArea = r;
            break;
        }
        case 1:
        {
            auto pick = r.removeFromTop (24);
            for (auto* b : pickers) { b->setBounds (pick.removeFromLeft (90)); pick.removeFromLeft (4); }
            r.removeFromTop (20);   // column titles
            const int rowH = r.getHeight() / 8;
            for (int i = 0; i < routeRows.size(); ++i) routeRows[i]->setBounds (r.getX(), r.getY() + i * rowH, r.getWidth() - 160, rowH);
            break;
        }
        case 2:
        {
            auto p1 = r.removeFromTop (24), p2 = r.removeFromTop (28);
            for (int i = 0; i < pickers.size(); ++i)
            {
                auto& row = i < numLfos ? p1 : p2;
                pickers[i]->setBounds ((i < numLfos ? row.removeFromLeft (row.getWidth() / jmax (1, numLfos - i)) : row.removeFromLeft (90)).reduced (2, 2));
            }
            r.removeFromTop (8);
            auto upper = r.removeFromTop (r.getHeight() / 2);
            auto lfoP = upper.removeFromLeft (upper.getWidth() * 2 / 5).reduced (2), envP = upper.withTrimmedLeft (8).reduced (2);
            auto macroP = r.withTrimmedTop (8).reduced (2);
            panels = { lfoP, envP, macroP };
            panelTitles = { "LFO " + String (lfoPick + 1), "Envolvente " + String (envPick + 1), "Macros" };
            auto inside = [] (Rectangle<int> p) { return p.reduced (10, 6).withTrimmedTop (22); };
            auto a = inside (lfoP), b = inside (envP), c = inside (macroP);
            for (int i = 0; i < 3; ++i) pageControls[i]->setBounds (a.removeFromLeft (a.getWidth() / (3 - i)));
            for (int i = 3; i < 8; ++i) pageControls[i]->setBounds (b.removeFromLeft (b.getWidth() / (8 - i)));
            for (int i = 8; i < pageControls.size(); ++i) pageControls[i]->setBounds (c.removeFromLeft (c.getWidth() / (pageControls.size() - i)));
            break;
        }
        default:
        {
            auto pick = r.removeFromTop (24);
            for (auto* b : pickers) { b->setBounds (pick.removeFromLeft (90)); pick.removeFromLeft (4); }
            r.removeFromTop (6);
            auto ctl = r.removeFromBottom (110).withTrimmedTop (6);
            ctl = ctl.removeFromLeft (520);
            panels = { ctl };
            panelTitles = { "Secuencia " + String (seqPick + 1) };
            auto in = ctl.reduced (10, 6).withTrimmedTop (22);
            for (int i = 0; i < pageControls.size(); ++i) pageControls[i]->setBounds (in.removeFromLeft (in.getWidth() / (pageControls.size() - i)));
            seqArea = r.withTrimmedBottom (6);
            break;
        }
    }
}

bool Display::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")) return true;
    return false;
}

void Display::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files) if (proc.loadFile (File (f))) break;
}

void Display::setSeqStep (Point<int> p)
{
    if (! seqArea.contains (p)) return;
    const int s = jlimit (0, seqSteps - 1, (p.x - seqArea.getX()) * seqSteps / seqArea.getWidth());
    const float v = jlimit (-100.0f, 100.0f, (seqArea.getCentreY() - p.y) / (seqArea.getHeight() * 0.5f) * 100.0f);
    proc.setRealValue (Processor::seqStepId (seqPick, s), std::round (v));
}

void Display::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent == &saveWav) return;
    if (page == 3) setSeqStep (e.getPosition());
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (e.eventComponent == &saveWav)
    {
        if (e.getDistanceFromDragStart() > 8)
        {
            lastWav = proc.saveResampleWav();
            if (lastWav.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ lastWav.getFullPathName() }, false, this);
        }
        return;
    }
    if (page == 3) setSeqStep (e.getPosition());
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    for (int i = 0; i < panels.size(); ++i) Draw::panel (g, panels[i].toFloat(), panelTitles[i]);
    g.setFont (Fonts::label (11.0f));
    if (page == 0)
    {
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("MUESTRAS"), libraryCaption, Justification::centredLeft);
    }
    // status line
    static const char* states[] { "CAPTURANDO EN VIVO", "ESPERANDO EL DISPARO", "GRABANDO UN DISPARO", "BÚFER CONGELADO" };
    static const char* glitches[] { "REPETIR", "STUTTER", "AL REVÉS", "TAPE STOP", "BARAJAR", "CORTES" };
    String st = String::fromUTF8 (states[jlimit (0, 3, proc.captureState.load())]);
    st << "  ·  "_u << String (proc.captureValid.load() / proc.getSampleRate(), 1) << " s  ·  "_u << proc.granular.activeCount() << " granos";
    const int gt = proc.glitch.uiType.load();
    if (gt >= 0) st << "  ·  GLITCH: "_u << String::fromUTF8 (glitches[gt]);
    if (proc.resampled.load() > 0) st << "  ·  SALIDA GRABADA: "_u << String (proc.resampled.load() / proc.getSampleRate(), 1) << " s";
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (st), statusArea, Justification::centredLeft);

    if (page == 0)
    {
        // the capture buffer, oldest at the left, with the grains on top
        auto w = waveArea.toFloat();
        g.setColour (C::stone);
        g.fillRect (w);
        const int valid = proc.captureValid.load();
        const auto& ring = proc.captureRing();
        if (valid > 0)
        {
            g.setColour (C::cyan.withAlpha (0.8f));
            const int cols = (int) w.getWidth();
            for (int x = 0; x < cols; ++x)
            {
                float mx = 0.0f;
                const int a = valid - (int) ((int64) valid * x / cols), b = valid - (int) ((int64) valid * (x + 1) / cols);
                const int step = jmax (1, (a - b) / 24);
                for (int i = b; i < a; i += step) mx = jmax (mx, std::abs (ring.back (0, i)));
                const float h = jmin (1.0f, mx) * w.getHeight() * 0.48f;
                g.fillRect (w.getX() + x, w.getCentreY() - h, 1.0f, h * 2.0f + 1.0f);
            }
        }
        g.setColour (C::amber);
        for (auto& p : proc.granular.uiPos)
        {
            const float v = p.load();
            if (v < 0.0f) continue;
            g.fillEllipse (w.getX() + w.getWidth() * v - 3.0f, w.getY() + 4.0f, 6.0f, 6.0f);
        }
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("CAPTURA"), w.reduced (6).removeFromTop (12), Justification::topLeft);

        // spectrum of the spectral engine
        auto s = specArea.toFloat();
        g.setColour (C::stone);
        g.fillRect (s);
        Path path;
        for (int b = 0; b < 64; ++b)
        {
            const float db = Decibels::gainToDecibels (proc.spectral.uiSpectrum[b].load() + 1.0e-6f);
            const float y = jmap (jlimit (-90.0f, 0.0f, db), -90.0f, 0.0f, s.getBottom(), s.getY());
            const float x = s.getX() + s.getWidth() * b / 63.0f;
            if (b == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        Draw::glowPath (g, path, C::amber, 1.4f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr (proc.value ("spon") > 0.5f ? String ("ESPECTRO") : String ("ESPECTRO (APAGADO: PRENDE ESPECTRO EN LA SECCIÓN ESPECTRAL)"_u)), s.reduced (6).removeFromTop (12), Justification::topLeft);
    }
    else if (page == 1)
    {
        // column titles and a meter per route showing what it does right now
        auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4).reduced (12, 10).withTrimmedTop (62).withTrimmedBottom (18);
        {
            g.setColour (C::boneDim);
            g.setFont (Fonts::label (11.0f));
            auto t = r.removeFromTop (20).withTrimmedLeft (34);
            const int w = (r.getWidth() - 160 - 34 - 200 - 6 - 240 - 10 - 20) / 3;
            for (auto [txt, wd] : std::initializer_list<std::pair<const char*, int>> { { "FUENTE", 206 }, { "DESTINO", 250 }, { "CANTIDAD", w + 10 }, { "CURVA", w + 10 }, { "SUAVIZADO", w } })
            { g.drawText (::racoon::i18n::tr (txt), t.removeFromLeft (wd), Justification::centredLeft); }
            g.drawText (::racoon::i18n::tr ("AHORA"), r.getRight() - 150, t.getY(), 140, 20, Justification::centredLeft);
        }
        const int rowH = r.getHeight() / 8;
        for (int i = 0; i < 8; ++i)
        {
            const int route = routePage * 8 + i;
            const float v = proc.modOut[route].load();
            auto m = Rectangle<float> ((float) r.getRight() - 150.0f, (float) (r.getY() + i * rowH + rowH / 2 - 5), 140.0f, 10.0f);
            g.setColour (C::stone);
            g.fillRect (m);
            g.setColour (v >= 0.0f ? C::amber : C::cyan);
            const float half = m.getWidth() * 0.5f;
            g.fillRect (v >= 0.0f ? m.getCentreX() : m.getCentreX() - half * jmin (1.0f, -v), m.getY(), half * jmin (1.0f, std::abs (v)), m.getHeight());
        }
    }
    else if (page == 3)
    {
        auto s = seqArea.toFloat();
        g.setColour (C::stone);
        g.fillRect (s);
        const int len = (int) proc.value (Processor::seqId (seqPick, "len"));
        const float cw = s.getWidth() / seqSteps;
        for (int i = 0; i < seqSteps; ++i)
        {
            const float v = proc.value (Processor::seqStepId (seqPick, i)) * 0.01f;
            auto cell = Rectangle<float> (s.getX() + i * cw + 2, s.getY(), cw - 4, s.getHeight());
            g.setColour (i < len ? C::amber : C::ash);
            const float h = std::abs (v) * s.getHeight() * 0.5f;
            g.fillRect (cell.getX(), v >= 0 ? s.getCentreY() - h : s.getCentreY(), cell.getWidth(), h);
        }
        g.setColour (C::boneDim);
        g.drawHorizontalLine ((int) s.getCentreY(), s.getX(), s.getRight());
        g.drawText (::racoon::i18n::tr ("Dibuja los 16 pasos con el ratón. Se usan como fuentes Secuencia 1 a 4 en la matriz."_u), s.reduced (6).removeFromTop (12), Justification::topLeft);
    }
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y una canción de ejemplo pasa por Skunk."_u, "Mueve Granos, Espectro y Glitch mientras suena."_u, "La X y la Y de Salida cambian todo con un solo movimiento."_u, "Apaga Sonido de ejemplo para procesar tu propio audio."_u };
    s.sections = {
        { "Captura", { { "recmode", "Modo", "En vivo lee lo que acaba de sonar; un disparo graba al pasar el umbral o con una nota; congelado deja quieto el material."_u },
                       { "caplen", "Largo", "Cuántos segundos guarda (hasta 120)."_u }, { "freeze", "Congelar", "Deja de grabar: los granos usan lo que ya hay." },
                       { "gscan", "Lectura", "Qué tan rápido recorre el material congelado: estira o encoge el tiempo (negativo, al revés)."_u } } },
        { "Granos", { { "gsize", "Tamaño"_u, "Largo de cada grano." }, { "gdens", "Densidad", "Granos por segundo." }, { "gpos", "Posición"_u, "De dónde salen los granos."_u },
                      { "gspray", "Dispersión"_u, "Qué tan desordenada es la posición."_u }, { "gpitch", "Afinación"_u, "Sube o baja los granos." },
                      { "gmix", "Mezcla", "Granos contra la señal que entra."_u } } },
        { "Espectro", { { "spon", "Encender", "Prende el motor espectral (añade latencia según el tamaño de FFT)."_u }, { "sfreeze", "Congelar", "Sostiene el sonido como un pad." },
                        { "sblur", "Difuminar", "Mancha el sonido en el tiempo." }, { "spitch", "Afinación"_u, "Cambia la nota sin cambiar la velocidad."_u },
                        { "smix", "Mezcla", "" } } },
        { "Glitch", { { "glprob", "Probabilidad", "Qué tan seguido hay un glitch en cada división."_u }, { "gldiv", "Rejilla", "" }, { "gldur", "Duración"_u, "" } } },
        { "Salida", { { "drywet", "Mezcla", "" }, { "outgain", "Volumen", "" }, { "xy_x", "X", "" }, { "xy_y", "Y", "" } } },
        { "Granos (detalle)", { { "gfine", "Fino", "" }, { "grev", "Al revés"_u, "" }, { "gjitter", "Jitter", "" }, { "gspread", "Estéreo"_u, "" }, { "gwin", "Ventana", "" },
                                { "grandp", "Azar afin.", "" }, { "grandprob", "Prob. azar", "" }, { "gskip", "Silencios", "" }, { "gjump", "Saltos", "" },
                                { "gmax", "Máximo"_u, "" }, { "profile", "Perfil", "Percusivo, melódico o experimental."_u }, { "gmidi", "Teclado", "Los granos suenan con el teclado y siguen su nota." } }, true },
        { "Espectro (detalle)", { { "fftsize", "FFT", "Más grande: más definición en graves, más latencia."_u }, { "sfreezemix", "Congelado", "" }, { "sdecay", "Caída"_u, "" },
                                  { "shold", "Armónicos"_u, "" }, { "sphase", "Fase", "" }, { "stexture", "Textura", "" }, { "sshift", "Correr", "" },
                                  { "sstretch", "Estirar", "" }, { "sdisp", "Dispersión"_u, "" }, { "slow", "Desde", "" }, { "shigh", "Hasta", "" }, { "sformant", "Formantes", "" } }, true },
        { "Morphing", { { "morphmode", "Modo", "Mezcla este sonido con lo que llega por sidechain." }, { "morph", "Cantidad", "" } }, true },
        { "Glitch (detalle)", { { "glrep", "Repetir", "" }, { "glstut", "Stutter", "" }, { "glrev", "Al revés"_u, "" }, { "gltape", "Tape stop", "" },
                                { "glshuf", "Barajar", "" }, { "gldrop", "Cortes", "" }, { "gltapems", "Tiempo tape", "" }, { "glsmooth", "Suavizado", "" } }, true },
        { "Transitorios", { { "trmode", "Modo", "Conserva el golpe original, lo quita, lo resalta o lo usa para disparar granos." }, { "trsens", "Sensibilidad", "" },
                            { "tramount", "Cantidad", "" }, { "trhold", "Duración"_u, "" }, { "ingain", "Entrada", "" }, { "trig", "Umbral", "Para el modo un disparo." } }, true },
        { "Ruido y filtro", { { "ntype", "Ruido", "" }, { "nlevel", "Volumen", "" }, { "ncolor", "Color", "" }, { "ndest", "Destino", "" },
                              { "ftype", "Filtro", "" }, { "ffreq", "Frecuencia", "" }, { "freso", "Resonancia", "" }, { "fvowel", "Vocal", "" }, { "fkey", "Teclado", "" }, { "fmix", "Mezcla", "" } }, true },
        { "Espacio", { { "width", "Ancho", "" }, { "move", "Velocidad", "" }, { "mdepth", "Movimiento", "" }, { "disp", "Dispersión"_u, "" }, { "decor", "Decorrelación"_u, "" },
                       { "seedlock", "Fijar semilla", "Cada compás repite las mismas decisiones al azar."_u }, { "seed", "Semilla", "" }, { "loop", "Bucle", "" } }, true },
        { "Efectos", { { "fx1", "1", "" }, { "fx2", "2", "" }, { "fx3", "3", "" }, { "fx4", "4", "" }, { "fx5", "5", "" }, { "dist_drive", "Drive", "" }, { "dist_mix", "Dist.", "" },
                       { "ch_mix", "Chorus", "" }, { "ph_mix", "Phaser", "" }, { "dl_div", "Delay t.", "" }, { "dl_fb", "Repet.", "" }, { "dl_mix", "Delay", "" },
                       { "rv_size", "Tamaño"_u, "" }, { "rv_mix", "Reverb", "" } }, true },
    };
    s.width = 1240;
    s.displayEasy = 400;
    s.displayFull = 400;
    s.easyHeight = 740;
    s.advancedHeight = 200;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

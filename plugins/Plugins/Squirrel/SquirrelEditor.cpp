#include "../../Shared/PanelMuestras.h"
#include "I18n.h"
#include "SquirrelEditor.h"
#include "../../Shared/Biblioteca.h"

namespace racoon::squirrel
{
using namespace juce;
using namespace racoon::ui;

static String noteName (double n) { return MidiMessage::getMidiNoteName (roundToInt (n), true, true, 3); }

// ===================================================================== number box
NumberBox::NumberBox (const String& l, double a, double b, double s, std::function<String (double)> f)
    : label (l), lo (a), hi (b), step (s), format (std::move (f))
{
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

void NumberBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::boneDim);
    g.setFont (Fonts::label (10.5f));
    g.drawText (::racoon::i18n::tr (upper (label)), r.removeFromTop (12), Justification::centred);
    r = r.reduced (1, 1);
    g.setColour (C::stone);
    g.fillRect (r);
    g.setColour (C::ridge);
    g.drawRect (r, 1.0f);
    g.setColour (C::amber);
    g.setFont (Fonts::mono (14.0f));
    g.drawText (::racoon::i18n::tr (format ? format (value) : String (roundToInt (value))), r, Justification::centred);
}

void NumberBox::mouseDown (const MouseEvent&) { dragStart = value; }

void NumberBox::mouseDrag (const MouseEvent& e)
{
    const double v = jlimit (lo, hi, dragStart + std::round ((double) -e.getDistanceFromDragStartY() / 4.0) * step);
    if (v != value) { value = v; repaint(); if (onChange) onChange (v); }
}

void NumberBox::mouseDoubleClick (const MouseEvent&)
{
    auto* ed = new TextEditor();
    ed->setText (String (value), false);
    ed->setBounds (getLocalBounds().withTrimmedTop (12));
    ed->setJustification (Justification::centred);
    addAndMakeVisible (ed);
    ed->grabKeyboardFocus();
    ed->selectAll();
    auto done = [this, ed]
    {
        const double v = jlimit (lo, hi, ed->getText().getDoubleValue());
        value = v;
        if (onChange) onChange (v);
        MessageManager::callAsync ([ed] { delete ed; });
        repaint();
    };
    ed->onReturnKey = done;
    ed->onFocusLost = done;
    ed->onEscapeKey = [ed] { MessageManager::callAsync ([ed] { delete ed; }); };
}

// ===================================================================== waveform
float WaveView::xFor (int frame, int len) const { return 6.0f + (getWidth() - 12.0f) * (float) frame / (float) jmax (1, len); }
int WaveView::frameFor (float x, int len) const { return jlimit (0, len, roundToInt ((x - 6.0f) / (getWidth() - 12.0f) * len)); }

int WaveView::hit (float x) const
{
    const auto& prog = proc.program();
    if (selected >= (int) prog.zones.size() || prog.zones[(size_t) selected].sample == nullptr) return -1;
    const auto& z = prog.zones[(size_t) selected];
    const int len = z.len();
    int best = -1; float bestD = 7.0f;
    auto test = [&] (int id, int frame) { const float d = std::abs (xFor (frame, len) - x); if (d < bestD) { bestD = d; best = id; } };
    if ((int) proc.value ("mode") == 1 && selected == 0)
        for (size_t k = 1; k < prog.slices.size(); ++k) test (10 + (int) k, prog.slices[k]);
    test (0, z.startF()); test (1, z.endF());
    if (z.loopMode != loopOff) { test (2, z.loopStartF()); test (3, z.loopEndF()); }
    return best;
}

void WaveView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::void_.darker (0.2f));
    g.fillRect (r);
    const auto& prog = proc.program();
    g.setFont (Fonts::label (12.0f));
    if (prog.zones.empty() || selected >= (int) prog.zones.size())
    {
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("ARRASTRA AQUÍ WAV, AIFF O FLAC  ·  O CARGA UNO DE FÁBRICA"_u), r, Justification::centred);
        return;
    }
    const auto& z = prog.zones[(size_t) selected];
    if (z.sample == nullptr)
    {
        g.setColour (z.missing ? C::ochre : C::boneDim);
        g.drawText (::racoon::i18n::tr (z.missing ? "FALTA EL ARCHIVO: "_u + z.path + "  ·  USA BUSCAR FALTANTES"_u : String ("CARGANDO...")), r.reduced (10, 0), Justification::centred);
        return;
    }
    const auto& s = *z.sample;
    const int len = s.length();
    const float mid = r.getCentreY(), h = r.getHeight() * 0.45f;
    const float sx = xFor (z.startF(), len), ex = xFor (z.endF(), len);

    // loop region
    if (z.loopMode != loopOff)
    {
        g.setColour (C::cyan.withAlpha (0.12f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (xFor (z.loopStartF(), len), r.getY(), xFor (z.loopEndF(), len), r.getBottom()));
    }
    // waveform from the overview
    const int bins = (int) s.overview.size();
    const float scale = z.normalize ? 1.0f / s.peak : 1.0f;
    for (int x = 6; x < getWidth() - 6; ++x)
    {
        const int b0 = (int) ((double) (x - 6) / (getWidth() - 12) * bins), b1 = jmax (b0 + 1, (int) ((double) (x - 5) / (getWidth() - 12) * bins));
        float m = 0.0f;
        for (int b = b0; b < jmin (b1, bins); ++b) m = jmax (m, s.overview[(size_t) b]);
        m = jmin (1.0f, m * scale);
        const bool inside = (float) x >= sx && (float) x <= ex;
        g.setColour (inside ? C::amber : C::amberDim.withAlpha (0.5f));
        g.fillRect ((float) x, mid - m * h, 1.0f, jmax (1.0f, 2.0f * m * h));
    }
    // slices
    if ((int) proc.value ("mode") == 1 && selected == 0)
    {
        g.setFont (Fonts::mono (11.0f));
        for (size_t k = 0; k < prog.slices.size(); ++k)
        {
            const float x = xFor (prog.slices[k], len);
            g.setColour (C::bone.withAlpha (0.7f));
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
            g.drawText (::racoon::i18n::tr (noteName (36 + (double) k)), Rectangle<float> (x + 3, r.getY() + 2, 40, 12), Justification::centredLeft);
        }
    }
    // markers
    auto marker = [&] (int frame, Colour c, const String& tag)
    {
        const float x = xFor (frame, len);
        g.setColour (c);
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.fillRect (x - 1.0f, r.getBottom() - 14.0f, 22.0f, 14.0f);
        g.setColour (C::void_);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (tag), Rectangle<float> (x + 1, r.getBottom() - 14.0f, 20, 14), Justification::centred);
    };
    marker (z.startF(), C::bone, "S");
    marker (z.endF(), C::bone, "E");
    if (z.loopMode != loopOff) { marker (z.loopStartF(), C::cyan, "L"); marker (z.loopEndF(), C::cyan, "L"); }
    // playheads
    g.setColour (C::ochre);
    for (auto& ph : proc.playhead)
    {
        const float p = ph.load();
        if (p >= 0.0f) g.fillRect (6.0f + (getWidth() - 12.0f) * p, r.getY(), 1.5f, r.getHeight());
    }
    // info
    g.setColour (C::boneDim);
    g.setFont (Fonts::label (11.5f));
    const String info = upper (z.name) + "  ·  "_u + String (len / s.rate, 2) + " S  ·  "_u + String (s.rate / 1000.0, 1) + " KHZ  ·  "_u
                      + (s.audio.getNumChannels() > 1 ? "ESTÉREO"_u : String ("MONO"));
    g.drawText (::racoon::i18n::tr (info), r.reduced (10, 4).withHeight (14), Justification::topLeft);
}

void WaveView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (hit ((float) e.x) >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void WaveView::mouseDown (const MouseEvent& e)
{
    dragging = hit ((float) e.x);
    if (dragging >= 10 && e.mods.isPopupMenu())
    {
        const int k = dragging - 10;
        proc.edit ([k] (Program& p) { if (k > 0 && k < (int) p.slices.size()) p.slices.erase (p.slices.begin() + k); });
        dragging = -1;
    }
}

void WaveView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const auto& prog = proc.program();
    if (selected >= (int) prog.zones.size() || prog.zones[(size_t) selected].sample == nullptr) return;
    const int len = prog.zones[(size_t) selected].len();
    const int f = frameFor ((float) e.x, len);
    const auto id = prog.zones[(size_t) selected].id;
    const int what = dragging;
    proc.edit ([=] (Program& p)
    {
        if (what >= 10)
        {
            const int k = what - 10;
            if (k > 0 && k < (int) p.slices.size())
            {
                const int lo = p.slices[(size_t) k - 1] + 64, hi = (size_t) k + 1 < p.slices.size() ? p.slices[(size_t) k + 1] - 64 : len - 64;
                p.slices[(size_t) k] = jlimit (lo, jmax (lo, hi), f);
            }
            return;
        }
        for (auto& z : p.zones)
            if (z.id == id)
            {
                switch (what)
                {
                    case 0: z.start = jlimit (0, z.endF() - 64, f); break;
                    case 1: z.end = jlimit (z.startF() + 64, len, f); break;
                    case 2: z.loopStart = jlimit (z.startF(), z.loopEndF() - 64, f); break;
                    default: z.loopEnd = jlimit (z.loopStartF() + 64, z.endF(), f); break;
                }
            }
    });
}

void WaveView::mouseDoubleClick (const MouseEvent& e)
{
    // in slice mode, a double click adds a slice there
    if ((int) proc.value ("mode") != 1 || selected != 0) return;
    const auto& prog = proc.program();
    if (prog.zones.empty() || prog.zones[0].sample == nullptr) return;
    const int f = frameFor ((float) e.x, prog.zones[0].len());
    proc.edit ([f] (Program& p)
    {
        if (p.slices.empty()) p.slices.push_back (p.zones[0].startF());
        p.slices.insert (std::upper_bound (p.slices.begin(), p.slices.end(), f), f);
    });
}

// ===================================================================== display
Display::Display (Processor& p)
    : proc (p), wave (p), keys (p.keyboard, MidiKeyboardComponent::horizontalKeyboard),
      root ("Raíz"_u, 0, 127, 1, noteName), lowKey ("Nota baja", 0, 127, 1, noteName), highKey ("Nota alta", 0, 127, 1, noteName),
      lowVel ("Vel baja", 1, 127, 1), highVel ("Vel alta", 1, 127, 1),
      tune ("Afinar", -48, 48, 0.1, [] (double v) { return String (v, 1); }), gain ("Ganancia", -36, 12, 0.5, [] (double v) { return String (v, 1) + " dB"; }),
      choke ("Choke", 0, 8, 1, [] (double v) { return v < 0.5 ? String ("-") : String (roundToInt (v)); }),
      rr ("Round robin", 0, 8, 1, [] (double v) { return v < 0.5 ? String ("-") : String (roundToInt (v)); }),
      fadeIn ("Fade in", 0, 2000, 1, [] (double v) { return String (roundToInt (v)) + " ms"; }),
      fadeOut ("Fade out", 0, 2000, 1, [] (double v) { return String (roundToInt (v)) + " ms"; })
{
    addAndMakeVisible (wave);
    keys.setAvailableRange (24, 108);
    keys.setLowestVisibleKey (36);
    keys.setKeyWidth (14.0f);
    keys.setColour (MidiKeyboardComponent::whiteNoteColourId, C::bone);
    keys.setColour (MidiKeyboardComponent::blackNoteColourId, C::void_);
    keys.setColour (MidiKeyboardComponent::keySeparatorLineColourId, C::ridge);
    keys.setColour (MidiKeyboardComponent::keyDownOverlayColourId, C::amber);
    keys.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::amber.withAlpha (0.3f));
    keys.setWantsKeyboardFocus (false);
    addChildComponent (keys);   // the keyboard lives in the dock at the bottom, the same in every plugin

    for (auto* b : { &loadButton, &layerButton, &factoryButton, &libraryButton, &removeButton, &sliceButton, &findButton, &embedButton, &revButton, &oneShotButton, &normButton })
        addAndMakeVisible (b);
    addAndMakeVisible (zoneBox);
    addAndMakeVisible (loopBox);
    loopBox.addItemList ({ "Sin loop", "Loop", "Ping-pong" }, 1);
    for (auto* n : { &root, &lowKey, &highKey, &lowVel, &highVel, &tune, &gain, &choke, &rr, &fadeIn, &fadeOut }) addAndMakeVisible (n);

    loadButton.setTooltip ("Carga uno o varios archivos. Uno ocupa todo el teclado; varios se reparten una tecla cada uno desde C1.");
    layerButton.setTooltip ("Agrega archivos como capas encima de lo que ya hay.");
    embedButton.setTooltip ("Guarda las muestras dentro del proyecto para que viaje completo. Solo con muestras que tengas derecho a compartir."_u);
    findButton.setTooltip ("Si moviste las muestras, elige la carpeta donde están ahora y se vuelven a enlazar."_u);
    sliceButton.setTooltip ("Busca los golpes de la primera zona y pone un corte en cada uno (modo Cortes: un corte por tecla desde C1)."_u);

    loadButton.onClick = [this] { choose (false); };
    layerButton.onClick = [this] { choose (true); };
    factoryButton.onClick = [this]
    {
        PopupMenu m;
        auto names = Processor::builtinNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (factoryButton), [this] (int r) { if (r > 0) { proc.loadBuiltin (r - 1); wave.selected = 0; } });
    };
    libraryButton.setTooltip ("Muestras Racoon: pianos, bajos, guitarras, cuerdas, metales, láminas, kits de batería y miles de golpes con licencia libre"_u);
    libraryButton.onClick = [this]
    {
        PopupMenu m, inst, sueltas, kits;
        racoon::biblio::menuInstrumentos (inst, [this] (const String& id) { proc.loadLibraryInstrument (id); wave.selected = 0; });
        racoon::biblio::menuBiblioteca (sueltas, {}, [this] (const String& g) { proc.loadLibraryGroup (g); wave.selected = 0; });
        for (auto& k : racoon::biblio::Catalogo::get().kits)
        {
            const auto id = k.id;
            kits.addItem (k.nombre, [this, id] { proc.loadLibraryKit (id); wave.selected = 0; });
        }
        m.addSubMenu ("Instrumentos", inst);
        m.addSubMenu ("Golpes y sonidos sueltos", sueltas);
        m.addItem ("Golpes y sonidos sueltos en un panel..."_u, [this]
        {
            racoon::biblio::PanelMuestras::mostrar (*this, "Muestras Racoon", {}, [this] (const String& g) { proc.loadLibraryGroup (g); wave.selected = 0; });
        });
        m.addSubMenu ("Kits de batería en el teclado (desde C1)"_u, kits, ! racoon::biblio::Catalogo::get().kits.empty());
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (libraryButton));
    };
    removeButton.onClick = [this]
    {
        const auto& prog = proc.program();
        if (wave.selected >= (int) prog.zones.size()) return;
        const auto id = prog.zones[(size_t) wave.selected].id;
        proc.edit ([id] (Program& p) { p.zones.erase (std::remove_if (p.zones.begin(), p.zones.end(), [id] (const Zone& z) { return z.id == id; }), p.zones.end()); });
        wave.selected = jmax (0, wave.selected - 1);
    };
    sliceButton.onClick = [this] { proc.detectSlices(); };
    findButton.onClick = [this]
    {
        chooser = std::make_shared<FileChooser> ("Carpeta donde están las muestras"_u, File::getSpecialLocation (File::userMusicDirectory));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
        {
            if (fc.getResult().isDirectory()) proc.relocateMissing (fc.getResult());
        });
    };
    embedButton.setClickingTogglesState (true);
    embedButton.onClick = [this] { const bool on = embedButton.getToggleState(); proc.edit ([on] (Program& p) { p.embed = on; }); };
    zoneBox.onChange = [this] { wave.selected = jmax (0, zoneBox.getSelectedItemIndex()); lastVersion = -1; };

    root.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.root = (int) v; }); };
    lowKey.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.lowKey = (int) v; z.highKey = jmax (z.highKey, z.lowKey); }); };
    highKey.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.highKey = (int) v; z.lowKey = jmin (z.highKey, z.lowKey); }); };
    lowVel.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.lowVel = (int) v; z.highVel = jmax (z.highVel, z.lowVel); }); };
    highVel.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.highVel = (int) v; z.lowVel = jmin (z.highVel, z.lowVel); }); };
    tune.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.tune = (float) v; }); };
    gain.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.gainDb = (float) v; }); };
    choke.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.choke = (int) v; }); };
    rr.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.roundRobin = (int) v; }); };
    fadeIn.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.fadeInMs = (float) v; }); };
    fadeOut.onChange = [this] (double v) { editZone ([v] (Zone& z) { z.fadeOutMs = (float) v; }); };
    loopBox.onChange = [this] { const int m = loopBox.getSelectedItemIndex(); if (m >= 0) editZone ([m] (Zone& z) { z.loopMode = m; }); };
    for (auto* b : { &revButton, &oneShotButton, &normButton }) b->setClickingTogglesState (true);
    revButton.onClick = [this] { const bool on = revButton.getToggleState(); editZone ([on] (Zone& z) { z.reverse = on; }); };
    oneShotButton.onClick = [this] { const bool on = oneShotButton.getToggleState(); editZone ([on] (Zone& z) { z.oneShot = on; }); };
    normButton.onClick = [this] { const bool on = normButton.getToggleState(); editZone ([on] (Zone& z) { z.normalize = on; }); };
    root.setTooltip ("Nota en la que la muestra suena a su altura original."_u);
    choke.setTooltip ("Las zonas del mismo grupo se cortan entre sí (hi-hat abierto y cerrado)."_u);
    rr.setTooltip ("Las zonas del mismo grupo se turnan nota a nota, para que un golpe repetido no suene idéntico."_u);
    startTimerHz (20);
}

void Display::choose (bool layers)
{
    chooser = std::make_shared<FileChooser> ("Muestras", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [this, layers] (const FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty()) proc.loadFiles (fc.getResults(), layers);
                          });
}

void Display::editZone (std::function<void (Zone&)> fn)
{
    const auto& prog = proc.program();
    if (wave.selected >= (int) prog.zones.size()) return;
    const auto id = prog.zones[(size_t) wave.selected].id;
    proc.edit ([id, fn] (Program& p) { for (auto& z : p.zones) if (z.id == id) fn (z); });
}

bool Display::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg")) return true;
    return false;
}

void Display::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    Array<File> list;
    for (auto& f : files) if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg")) list.add (File (f));
    proc.loadFiles (list, ModifierKeys::currentModifiers.isShiftDown());
    repaint();
}

void Display::timerCallback()
{
    if (proc.programVersion.load() != lastVersion) refresh();
    wave.repaint();
}

void Display::refresh()
{
    lastVersion = proc.programVersion.load();
    const auto& prog = proc.program();
    zoneBox.clear (dontSendNotification);
    for (size_t i = 0; i < prog.zones.size(); ++i)
    {
        const auto& z = prog.zones[i];
        zoneBox.addItem (String ((int) i + 1) + ". " + (z.name.isNotEmpty() ? z.name : String ("zona")) + "  " + noteName (z.lowKey) + "-" + noteName (z.highKey)
                         + (z.missing ? "  (falta)" : ""), (int) i + 1);
    }
    wave.selected = jlimit (0, jmax (0, (int) prog.zones.size() - 1), wave.selected);
    zoneBox.setSelectedItemIndex (wave.selected, dontSendNotification);
    embedButton.setToggleState (prog.embed, dontSendNotification);
    bool missing = false;
    for (auto& z : prog.zones) missing = missing || z.missing;
    findButton.setEnabled (missing);
    const bool has = wave.selected < (int) prog.zones.size();
    for (auto* c : std::initializer_list<Component*> { &root, &lowKey, &highKey, &lowVel, &highVel, &tune, &gain, &choke, &rr, &fadeIn, &fadeOut,
                                                        &loopBox, &revButton, &oneShotButton, &normButton, &removeButton })
        c->setEnabled (has);
    if (has)
    {
        const auto& z = prog.zones[(size_t) wave.selected];
        root.setValue (z.root); lowKey.setValue (z.lowKey); highKey.setValue (z.highKey);
        lowVel.setValue (z.lowVel); highVel.setValue (z.highVel); tune.setValue (z.tune); gain.setValue (z.gainDb);
        choke.setValue (z.choke); rr.setValue (z.roundRobin); fadeIn.setValue (z.fadeInMs); fadeOut.setValue (z.fadeOutMs);
        loopBox.setSelectedItemIndex (z.loopMode, dontSendNotification);
        revButton.setToggleState (z.reverse, dontSendNotification);
        oneShotButton.setToggleState (z.oneShot, dontSendNotification);
        normButton.setToggleState (z.normalize, dontSendNotification);
    }
    wave.repaint();
}

void Display::resized()
{
    auto r = getLocalBounds();
    keys.setBounds ({});
    r.removeFromBottom (6);
    auto inner = r.reduced (12, 10);
    auto bar = inner.removeFromTop (26);
    for (auto* b : { &loadButton, &layerButton, &factoryButton, &libraryButton, &removeButton })
    {
        b->setBounds (bar.removeFromLeft (b == &factoryButton || b == &libraryButton ? 100 : 82));
        bar.removeFromLeft (6);
    }
    embedButton.setBounds (bar.removeFromRight (92));
    bar.removeFromRight (6);
    findButton.setBounds (bar.removeFromRight (150));
    bar.removeFromRight (6);
    sliceButton.setBounds (bar.removeFromRight (134));
    bar.removeFromRight (6);
    zoneBox.setBounds (bar);
    inner.removeFromTop (8);
    zoneRow = inner.removeFromBottom (40);
    inner.removeFromBottom (8);
    wave.setBounds (inner);

    auto z = zoneRow;
    const int w = 66;
    for (auto* n : { &root, &lowKey, &highKey, &lowVel, &highVel, &tune, &gain, &choke, &rr, &fadeIn, &fadeOut })
    {
        n->setBounds (z.removeFromLeft (w));
        z.removeFromLeft (4);
    }
    z.removeFromLeft (4);
    auto b = z.withTrimmedTop (12);
    loopBox.setBounds (b.removeFromLeft (jmin (110, b.getWidth() / 3)));
    b.removeFromLeft (4);
    const int bw = (b.getWidth() - 8) / 3;
    revButton.setBounds (b.removeFromLeft (bw)); b.removeFromLeft (4);
    oneShotButton.setBounds (b.removeFromLeft (bw)); b.removeFromLeft (4);
    normButton.setBounds (b);
}

void Display::paint (Graphics& g)
{
    auto screen = getLocalBounds().withTrimmedBottom (54).toFloat();
    drawScreen (g, screen);
    if (dragOver)
    {
        g.setColour (C::amber);
        g.drawRect (screen.reduced (6), 2.0f);
    }
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1080;
    s.easyHeight = 700;
    s.advancedHeight = 190;
    s.displayEasy = 400;
    s.displayFull = 360;
    s.steps = { "Dale Play y escucha el sonido que trae Squirrel."_u, "Arrastra un WAV a la pantalla y se reparte por todo el teclado."_u, "En la secuencia de abajo, doble clic escribe una nota y doble clic en una nota la borra."_u, "Toca con el teclado de abajo o con las letras A S D F de tu compu."_u };
    s.sections = {
        { "Reproducción"_u, {
            { "mode", "Modo", "Cromático: la muestra se toca en todo el teclado. Cortes: cada corte va a una tecla desde C1."_u },
            { "rate", "Velocidad", "Más rápido o más lento, como una cinta: también cambia la altura. De 0.25x a 4x."_u },
            { "pitch", "Semitonos", "Transposición de ±48 semitonos."_u },
            { "fine", "Fino", "Afinación fina en cents."_u },
            { "startoffset", "Inicio", "Empieza más adentro de la muestra."_u },
            { "reverse", "Reverso", "Toca la muestra al revés."_u } } },
        { "Volumen", {
            { "amp_a", "Ataque", "De 0.1 ms a 30 s. Al mínimo la muestra arranca tal cual."_u },
            { "amp_d", "Caída"_u, "Tiempo en bajar al sostenido."_u },
            { "amp_s", "Sostenido", "Nivel mientras mantienes la tecla."_u },
            { "amp_r", "Relajación"_u, "Tiempo en apagarse al soltar."_u } } },
        { "Filtro", {
            { "ftype", "Tipo", "Apagado deja la muestra intacta."_u },
            { "cutoff", "Corte", "Frecuencia del filtro."_u },
            { "reso", "Resonancia", "Resalta la zona del corte."_u } } },
        { "Salida", {
            { "volume", "Volumen", "Volumen final."_u },
            { "voices", "Voces", "Notas a la vez, de 1 a 128."_u } } },
        { "Loop y cortes", {
            { "loopxf", "Crossfade", "Mezcla el final del loop con su inicio para que no se note el salto. De 0 a 500 ms."_u },
            { "velstart", "Vel inicio", "Tocar más fuerte empieza más adentro de la muestra."_u },
            { "slthresh", "Umbral", "Nivel mínimo de un golpe para cortar ahí."_u },
            { "slsens", "Sensibilidad", "Más alta encuentra golpes más suaves."_u },
            { "slgrid", "Rejilla", "Acomoda los cortes a la rejilla del tempo de la muestra."_u },
            { "slbpm", "Tempo", "Tempo de la muestra, para la rejilla de cortes."_u },
            { "slgate", "Gate", "Encendido: el corte suena mientras tengas la tecla. Apagado: suena completo."_u } }, true },
        { "Filtro +", {
            { "fenv", "Envolvente", "Cuánto mueve el filtro su envolvente."_u },
            { "flt_a", "Ataque", "Ataque de la envolvente del filtro."_u },
            { "flt_d", "Caída"_u, "Caída de la envolvente del filtro."_u },
            { "flt_s", "Sostenido", "Sostenido de la envolvente del filtro."_u },
            { "flt_r", "Relajación"_u, "Relajación de la envolvente del filtro."_u },
            { "velflt", "Vel filtro", "Tocar más fuerte abre el filtro."_u },
            { "velamp", "Vel volumen", "Tocar más fuerte suena más fuerte."_u } }, true },
        { "LFO", {
            { "lforate", "Velocidad", "Velocidad del LFO."_u },
            { "lfoshape", "Forma", "Forma de onda del LFO."_u },
            { "lfopitch", "Afinación"_u, "Vibrato, en cents."_u },
            { "lfocutoff", "Filtro", "Cuánto mueve el filtro."_u },
            { "lfoamp", "Volumen", "Trémolo."_u } }, true },
        { "Ajustes", {
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u },
            { "pan", "Paneo", "Paneo general."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

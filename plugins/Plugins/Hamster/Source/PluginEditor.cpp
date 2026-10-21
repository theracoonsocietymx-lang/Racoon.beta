#include "installer/SelfInstaller.h"
#include "I18n.h"
#include "PluginEditor.h"
#include "presets/PresetManager.h"

using namespace juce;
using namespace be::ui;

namespace
{
// computer keys for pads 1..16 (two rows, like a hardware 2 x 8 layout)
const char* kPadKeys = "asdfghjkzxcvbnm,";
int padForKey (juce_wchar k)
{
    k = CharacterFunctions::toLowerCase (k);
    for (int i = 0; i < 16; ++i) if (kPadKeys[i] == (char) k) return i;
    return -1;
}
}

// ===================================================================== root / logo
void DrumEngineEditor::Root::paint (Graphics& g)
{
    g.fillAll (be::ui::Pro::win);   // the 2026 look: a dark working surface
    auto r = getLocalBounds().toFloat();
    auto head = r.removeFromTop ((float) headerH);
    g.setColour (S::bg());          // the header keeps the animal's colour
    g.fillRect (head);
    g.setColour (S::shade);
    g.fillRect (head.removeFromBottom (3.0f));
    auto pads = r.removeFromBottom ((float) padsH);
    g.setColour (S::shade);
    g.fillRect (pads);
    if (! ed.easyMode)
    {
        auto foot = r.removeFromBottom ((float) footerH);
        g.setColour (S::card);
        g.fillRect (foot);
        g.setColour (juce::Colour (0xff3a3236));
        g.fillRect (foot.removeFromTop (1.0f));
        g.setFont (Fonts::label (11.0f));
        g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr ("MACROS  ·  ARRASTRA EL CUADRITO SOBRE CUALQUIER PERILLA PARA ASIGNAR"_u), foot.removeFromBottom (13.0f).reduced (12.0f, 0.0f), Justification::centredLeft);
    }
}

void DrumEngineEditor::Logo::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto mark = r.removeFromLeft (r.getHeight()).reduced (2.0f);
    Draw::box (g, mark, S::shade, 0.0f, 0.0f);
    Draw::racoonMark (g, mark.reduced (4.0f), Brand::rosa, S::shade, S::card);
    r.removeFromLeft (10.0f);
    auto top = r.removeFromTop (r.getHeight() * 0.62f);
    g.setColour (S::shade);
    g.setFont (Fonts::title (26.0f));
    g.drawText (::racoon::i18n::tr ("Racoon"), top, Justification::bottomLeft);
    const float w = GlyphArrangement::getStringWidth (Fonts::title (26.0f), ::racoon::i18n::tr ("Racoon "));
    g.setColour (S::shade.withAlpha (0.7f));
    g.drawText (::racoon::i18n::tr ("Hamster"), top.withTrimmedLeft (w), Justification::bottomLeft);
    g.setColour (S::shade);
    g.setFont (Fonts::label (11.5f));
    g.drawText (::racoon::i18n::tr ("CAJA DE RITMOS  ·  v2.0"_u), r, Justification::centredLeft);
}

// ===================================================================== macro cell
DrumEngineEditor::MacroCell::MacroCell (Ctx& c, int i) : ctx (c), index (i), knob (c, be::macroId (i)), chip (c, be::ModSrc::Macro1 + i, " ")
{
    addAndMakeVisible (knob);
    addAndMakeVisible (chip);
    chip.setTooltip ("Arrastra este cuadrito sobre una perilla para que esta macro la mueva.");
    addChildComponent (editor);
    editor.setFont (Fonts::label (12.0f));
    editor.setJustification (Justification::centred);
    editor.onReturnKey = [this] { ctx.proc.setMacroName (index, editor.getText().toUpperCase()); editor.setVisible (false); repaint(); };
    editor.onFocusLost = [this] { editor.setVisible (false); };
    editor.onEscapeKey = [this] { editor.setVisible (false); };
}

void DrumEngineEditor::MacroCell::resized()
{
    auto r = getLocalBounds();
    auto lbl = r.removeFromBottom (15);
    editor.setBounds (lbl);
    chip.setBounds (r.removeFromRight (14).removeFromTop (14));
    const int s = std::min (r.getWidth(), r.getHeight());
    knob.setBounds (r.withSizeKeepingCentre (s, s));
}

void DrumEngineEditor::MacroCell::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Draw::engraved (g, ctx.proc.getMacroName (index), r.removeFromBottom (15.0f), Fonts::label (10.5f), Justification::centred, C::bone.withAlpha (0.8f));
}

void DrumEngineEditor::MacroCell::mouseDoubleClick (const MouseEvent& e)
{
    if (e.y > getHeight() - 18)
    {
        editor.setText (ctx.proc.getMacroName (index), false);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }
}

// ===================================================================== pad strip
DrumEngineEditor::PadStrip::PadStrip (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
DrumEngineEditor::PadStrip::~PadStrip() { ctx.unregisterAnimated (this); }

void DrumEngineEditor::PadStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float w = r.getWidth() / be::kNumTracks;
    for (int t = 0; t < be::kNumTracks; ++t)
    {
        auto p = Rectangle<float> ((float) t * w, 0.0f, w, r.getHeight()).reduced (2.5f, 2.0f);
        const bool sel = t == ctx.track();
        g.setColour (sel ? S::hi : C::stoneHi);
        g.fillRect (p);
        if (flash[t] > 0.01f) { g.setColour (S::accent().withAlpha (0.85f * flash[t])); g.fillRect (p.reduced (2.0f)); }
        g.setFont (Fonts::mono (9.5f));
        g.setColour (sel ? S::ink : C::boneDim);
        g.drawText (::racoon::i18n::tr (String (t + 1) + "  " + String::charToString ((juce_wchar) kPadKeys[t]).toUpperCase()), p.reduced (5.0f, 3.0f), Justification::topLeft);
        g.setFont (Fonts::label (12.0f));
        g.setColour (ctx.proc.trackValue (t, be::T_MUTE) > 0.5f ? C::ash : (sel ? S::ink : C::bone));
        g.drawFittedText (::racoon::i18n::tr (ctx.proc.getTrackName (t).toUpperCase()), p.reduced (4.0f, 4.0f).toNearestInt(), Justification::centredBottom, 1, 0.5f);
    }
}

void DrumEngineEditor::PadStrip::mouseDown (const MouseEvent& e)
{
    const int t = jlimit (0, be::kNumTracks - 1, (int) ((float) e.x / ((float) getWidth() / be::kNumTracks)));
    const float vel = jlimit (0.2f, 1.0f, 0.3f + 0.8f * (1.0f - (float) e.y / (float) getHeight()));
    ctx.proc.padDown (t, vel);
    ctx.selectTrack (t);
    down = t;
    repaint();
}

void DrumEngineEditor::PadStrip::mouseUp (const MouseEvent&)
{
    if (down >= 0) ctx.proc.padUp (down);
    down = -1;
}

void DrumEngineEditor::PadStrip::animate()
{
    if (! animVisible (this)) return;
    bool any = false;
    for (int t = 0; t < be::kNumTracks; ++t)
    {
        const int h = ctx.proc.engine.telemetry.trackHits[t].load (std::memory_order_relaxed);
        if (h != lastHits[t]) { lastHits[t] = h; flash[t] = 1.0f; any = true; }
        else if (flash[t] > 0.01f) { flash[t] *= 0.78f; any = true; }
    }
    if (any) repaint();
}

// ===================================================================== editor
DrumEngineEditor::DrumEngineEditor (DrumEngineProcessor& p)
    : AudioProcessorEditor (p), proc (p), ctx (p),
      quality (ctx, "g_quality"), miniScope (ctx, true), meter (ctx), master (ctx, "g_master", "VOLUMEN"),
      tabs ({}, [this] (int i) { setLive (false); auto v = visibleTabs(); if (i >= 0 && i < (int) v.size()) showPage (v[(size_t) i]); }),
      simplePage (ctx), kitPage (ctx), synthPage (ctx), samplePage (ctx), seqPage (ctx), modPage (ctx), fxPage (ctx), mixPage (ctx), analyzePage (ctx), presetPage (ctx), livePage (ctx)
{
    setLookAndFeel (&look);
    LookAndFeel::setDefaultLookAndFeel (&look);
    doceveinte::runSelfInstallerIfNeeded (JucePlugin_Name, p.wrapperType, { "Drum Engine", "Racoon Hamster", "Racoon Hamster Drums" });
    addAndMakeVisible (root);
    pages = { &simplePage, &kitPage, &synthPage, &samplePage, &seqPage, &modPage, &fxPage, &mixPage, &analyzePage, &presetPage };
    for (auto* pg : pages) root.addChildComponent (pg);
    root.addChildComponent (livePage);
    playBtn = std::make_unique<ParamButton> (ctx, "g_play", "PLAY", false, C::amber);
    recBtn = std::make_unique<ParamButton> (ctx, "g_record", "REC", false, Brand::rosa);
    playBtn->setTooltip ("Arranca y detiene el ritmo cuando tu DAW está parado. Barra espaciadora hace lo mismo."_u);
    recBtn->setTooltip ("Graba pads, notas MIDI y movimientos de perilla dentro del patrón mientras suena."_u);
    for (Component* c : std::initializer_list<Component*> { &logo, &prevBtn, &nextBtn, &presetName, &saveBtn, &abA, &abB, &abCopy, &undoBtn, &redoBtn, &liveBtn, &easyBtn, &fullBtn,
                                                            playBtn.get(), recBtn.get(), &quality, &cpu, &transport, &miniScope, &meter, &master, &tabs, &padStrip })
        root.addAndMakeVisible (c);
    for (int i = 0; i < be::kNumMacros; ++i) root.addAndMakeVisible (macros.add (new MacroCell (ctx, i)));
    root.addAndMakeVisible (toSong);
    toSong.setButtonText ("PEGAR EN MAQUETA");
    toSong.setColour (TextButton::buttonColourId, Brand::limon);
    toSong.makeMidi = [this] { return proc.maquetaMidi(); };
    toSong.animal = [] { return String ("Hamster"); };
    toSong.trackName = [this] { return proc.trackName(); };
    // from now on the clip in the song plays it: the plugin's own pattern stops following the DAW
    toSong.binding = &proc.songClip;
    toSong.onEdit = [this] (const racoon::maqueta::EditRequest& r)
    {
        // the clip is in the song already: the pattern only plays with its own PLAY
        if (auto* f = proc.apvts.getParameter ("g_follow")) f->setValueNotifyingHost (0.0f);
        proc.loadFromSong (r);
        setLive (false);
        showPage (pSeq);
    };
    toSong.afterSend = [this]
    {
        if (auto* f = proc.apvts.getParameter ("g_follow")) f->setValueNotifyingHost (0.0f);
        if (auto* pl = proc.apvts.getParameter ("g_play")) pl->setValueNotifyingHost (0.0f);
    };
    root.addAndMakeVisible (exportBtn);
    root.addAndMakeVisible (autoBtn);
    exportBtn.setTooltip (String::fromUTF8 ("Exportar el patrón con su automatización como archivo MIDI (la automatización viaja como MIDI CC)"));
    autoBtn.setTooltip (String::fromUTF8 ("Ver y dibujar la automatización. También: clic derecho en cualquier perilla > Ver automatización."));
    exportBtn.onClick = [this]
    {
        racoon::exportMidiWithChooser (chooser, "Racoon Hamster - " + Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M") + ".mid",
                                       [this] (const File& f) { const int pat = jlimit (0, be::kNumPatterns - 1, roundToInt (proc.getParamReal ("g_pattern")) - 1); const int bars = jmax (1, (proc.getSeqData().pats[pat].masterLength + 15) / 16); auto tmp = proc.exportPatternToMidi (pat, bars); return tmp.existsAsFile() && tmp.copyFileTo (f); });
    };
    autoBtn.onClick = [this] { auto ids = proc.automation.laneIds(); openAutomation (ids.isEmpty() ? String() : ids[0], false); };

    cpu.setFont (Fonts::mono (12.0f));
    cpu.setColour (Label::textColourId, S::inkSoft);
    transport.setFont (Fonts::mono (13.0f));
    transport.setColour (Label::textColourId, S::ink);
    transport.setJustificationType (Justification::centred);
    presetName.setTooltip ("Kit actual. Click para ver todos los kits.");
    presetName.onClick = [this] { showPage (pPresets); };
    for (auto* b : std::initializer_list<juce::Button*> { &prevBtn, &nextBtn, &presetName, &saveBtn, &abA, &abB, &abCopy, &undoBtn, &redoBtn, &liveBtn, &easyBtn, &fullBtn })
        b->getProperties().set ("onBrand", true);   // they sit on the animal's coloured header
    prevBtn.onClick = [this] { proc.presets().step (-1); };
    nextBtn.onClick = [this] { proc.presets().step (1); };
    prevBtn.setTooltip ("Kit anterior"); nextBtn.setTooltip ("Kit siguiente");
    saveBtn.setTooltip ("Guarda tu kit con sus patrones.");
    saveBtn.onClick = [this] { showPage (pPresets); };
    easyBtn.setButtonText ("PRINCIPIANTE"_u);
    easyBtn.setTooltip ("Vista PRINCIPIANTE: la cuadrícula de 16 pasos, el sonido elegido y los kits."_u);
    fullBtn.setTooltip ("Vista AVANZADA: síntesis, samples, secuenciador avanzado, modulación, efectos, mezcla y análisis."_u);
    easyBtn.setClickingTogglesState (false); fullBtn.setClickingTogglesState (false);
    easyBtn.onClick = [this] { setEasyMode (true); };
    fullBtn.onClick = [this] { setEasyMode (false); };
    undoBtn.setTooltip ("Deshacer (Cmd+Z)"); redoBtn.setTooltip ("Rehacer");
    abA.setTooltip ("Comparar: versión A"_u); abB.setTooltip ("Comparar: versión B"_u); abCopy.setTooltip ("Copia la versión A en la B"_u);
    abA.setClickingTogglesState (false); abB.setClickingTogglesState (false);
    abA.onClick = [this] { proc.recallAB (0); refreshAfterPresetChange(); };
    abB.onClick = [this] { proc.recallAB (1); refreshAfterPresetChange(); };
    abCopy.onClick = [this] { proc.copyAtoB(); };
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    liveBtn.onClick = [this] { setLive (! live); };
    liveBtn.setTooltip ("Modo en vivo: pads grandes, cambio de patrones, fills, scatter y escenas.");
    proc.onPresetLoaded = [this] { MessageManager::callAsync ([sp = Component::SafePointer<DrumEngineEditor> (this)] { if (sp != nullptr) sp->refreshAfterPresetChange(); }); };
    ctx.showPage = [this] (int pg) { setLive (false); showPage (pg); };
    ctx.refreshAll = [this] { refreshAll(); };

    // like every Racoon plugin: as wide as the screen and as tall as fits above the Dock / taskbar;
    // the design scales to fit and stays centred
    {
        Rectangle<int> area (0, 0, 1440, 860);
        if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userArea;
        constrainer.setSizeLimits (800, 560, 6000, 4000);
        setConstrainer (&constrainer);
        setResizable (true, true);
        setSize (jmax (960, area.getWidth()), jmax (600, area.getHeight() - 56));
    }
    setWantsKeyboardFocus (true);
    {
        PropertiesFile::Options o; o.applicationName = "Racoon Hamster"; o.folderName = "Racoon"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        PropertiesFile settings (o);
        easyMode = settings.getBoolValue ("easyMode", true);
    }
    setEasyMode (easyMode);
    showPage (pInicio);
    refreshAfterPresetChange();
    startTimerHz (30);
}

DrumEngineEditor::~DrumEngineEditor()
{
    stopTimer();
    proc.onPresetLoaded = nullptr;
    setLookAndFeel (nullptr);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void DrumEngineEditor::paint (Graphics& g) { g.fillAll (be::ui::Pro::win); }

std::vector<int> DrumEngineEditor::visibleTabs() const
{
    if (easyMode) return { pInicio, pSeq, pKit, pPresets };
    return { pInicio, pKit, pSynth, pSample, pSeq, pMod, pFx, pMix, pAnalyze, pPresets };
}

void DrumEngineEditor::setEasyMode (bool e)
{
    easyMode = e;
    {
        PropertiesFile::Options o; o.applicationName = "Racoon Hamster"; o.folderName = "Racoon"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        PropertiesFile settings (o);
        settings.setValue ("easyMode", e);
    }
    static const String names[] { "INICIO", "KIT", "SÍNTESIS"_u, "SAMPLE", "PATRÓN"_u, "MODULACIÓN"_u, "EFECTOS", "MEZCLA", "ANÁLISIS"_u, "KITS" };
    StringArray n;
    for (auto p : visibleTabs()) n.add (names[p]);
    tabs.setNames (n);
    easyBtn.setToggleState (e, dontSendNotification);
    fullBtn.setToggleState (! e, dontSendNotification);
    for (auto* m : macros) m->setVisible (! e);
    auto v = visibleTabs();
    if (std::find (v.begin(), v.end(), currentPage) == v.end()) showPage (pInicio); else showPage (currentPage);
    resized();
    root.repaint();
}

void DrumEngineEditor::openAutomation (const String& paramId, bool record)
{
    if (autoPanel == nullptr)
    {
        racoon::AutomationStyle st;
        st.label = &Fonts::label;
        st.body = &Fonts::body;
        st.mono = &Fonts::mono;
        st.surface = S::card;
        st.ink = S::ink;
        st.inkSoft = S::inkSoft;
        st.accent = S::accent();
        autoPanel = std::make_unique<racoon::AutomationPanel> (proc.automation, st);
        autoPanel->onClose = [this] { autoPanel->setVisible (false); resized(); };
        addChildComponent (*autoPanel);
    }
    autoPanel->showParam (paramId);
    autoPanel->setRecording (record);
    autoPanel->setVisible (true);
    resized();
}

void DrumEngineEditor::resized()
{
    const int panelH = autoPanel != nullptr && autoPanel->isVisible() ? racoon::AutomationPanel::preferredHeight : 0;
    if (panelH > 0) autoPanel->setBounds (0, getHeight() - panelH, getWidth(), panelH);
    const float s = jmin ((float) getWidth() / (float) baseW, (float) (getHeight() - panelH) / (float) baseH);
    root.setBounds (0, 0, baseW, baseH);
    root.setTransform (AffineTransform::scale (s).translated (((float) getWidth() - (float) baseW * s) * 0.5f, 0.0f));
    proc.extra.setProperty ("uiScale", s, nullptr);

    auto r = Rectangle<int> (0, 0, baseW, baseH);
    auto head = r.removeFromTop (headerH);
    auto row1 = head.removeFromTop (52).reduced (10, 6);
    logo.setBounds (row1.removeFromLeft (270));
    row1.removeFromLeft (10);
    prevBtn.setBounds (row1.removeFromLeft (30));
    presetName.setBounds (row1.removeFromLeft (250).reduced (3, 0));
    nextBtn.setBounds (row1.removeFromLeft (30));
    row1.removeFromLeft (6);
    saveBtn.setBounds (row1.removeFromLeft (80));
    row1.removeFromLeft (10);
    abA.setBounds (row1.removeFromLeft (30)); row1.removeFromLeft (3); abB.setBounds (row1.removeFromLeft (30)); row1.removeFromLeft (3);
    abCopy.setBounds (row1.removeFromLeft (42));
    row1.removeFromLeft (10);
    undoBtn.setBounds (row1.removeFromLeft (78)); row1.removeFromLeft (3);
    redoBtn.setBounds (row1.removeFromLeft (74));
    row1.removeFromLeft (10);
    transport.setBounds (row1.removeFromLeft (104));
    master.setBounds (row1.removeFromRight (60).withY (row1.getY() - 6).withHeight (52));
    row1.removeFromRight (6);
    meter.setBounds (row1.removeFromRight (16));
    row1.removeFromRight (6);
    miniScope.setBounds (row1.removeFromRight (96));
    row1.removeFromRight (6);
    cpu.setBounds (row1.removeFromRight (56));
    quality.setBounds (row1.removeFromRight (84).reduced (0, 5));
    auto row2 = head.reduced (10, 2).withTrimmedBottom (4);
    easyBtn.setBounds (row2.removeFromLeft (112)); row2.removeFromLeft (2);
    fullBtn.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (14);
    liveBtn.setBounds (row2.removeFromRight (96));
    row2.removeFromRight (8);
    recBtn->setBounds (row2.removeFromRight (64));
    row2.removeFromRight (4);
    playBtn->setBounds (row2.removeFromRight (80));
    row2.removeFromRight (8);
    autoBtn.setBounds (row2.removeFromRight (118));
    row2.removeFromRight (4);
    exportBtn.setBounds (row2.removeFromRight (96));
    row2.removeFromRight (4);
    toSong.setBounds (row2.removeFromRight (156));
    row2.removeFromRight (14);
    tabs.setBounds (easyMode ? row2.withWidth (std::min (row2.getWidth(), 4 * 150)) : row2);

    padStrip.setBounds (Rectangle<int> (0, baseH - padsH, baseW, padsH).reduced (10, 6));
    r.removeFromBottom (padsH);
    if (! easyMode)
    {
        auto foot = r.removeFromBottom (footerH).reduced (10, 6);
        foot.removeFromBottom (12);
        const int w = foot.getWidth() / be::kNumMacros;
        for (int i = 0; i < macros.size(); ++i) macros[i]->setBounds (foot.getX() + i * w + 3, foot.getY() + 2, w - 6, foot.getHeight() - 2);
    }
    for (auto* pg : pages) pg->setBounds (r.withTrimmedTop (4));
    livePage.setBounds (r.withTrimmedTop (4));
}

void DrumEngineEditor::showPage (int p)
{
    currentPage = jlimit (0, (int) numPages - 1, p);
    auto v = visibleTabs();
    if (std::find (v.begin(), v.end(), currentPage) == v.end()) { setEasyMode (false); v = visibleTabs(); }
    for (int i = 0; i < (int) pages.size(); ++i) pages[(size_t) i]->setVisible (! live && i == currentPage);
    livePage.setVisible (live);
    tabs.setSelected (live ? -1 : (int) (std::find (v.begin(), v.end(), currentPage) - v.begin()));
}

void DrumEngineEditor::setLive (bool l)
{
    live = l;
    proc.liveMode = l;
    liveBtn.setToggleState (live, dontSendNotification);
    showPage (currentPage);
}

void DrumEngineEditor::refreshAll()
{
    simplePage.refresh();
    for (auto* pg : { (Component*) &kitPage, (Component*) &synthPage, (Component*) &samplePage, (Component*) &seqPage, (Component*) &fxPage })
        if (auto* tp = dynamic_cast<TrackPage*> (pg)) { tp->rebuild(); tp->resized(); }
    root.repaint();
}

void DrumEngineEditor::refreshAfterPresetChange()
{
    presetName.setButtonText (proc.currentPresetName);
    abA.setToggleState (proc.currentABSlot() == 0, dontSendNotification);
    abB.setToggleState (proc.currentABSlot() == 1, dontSendNotification);
    refreshAll();
    presetPage.refresh();
    for (auto* m : macros) m->repaint();
}

void DrumEngineEditor::timerCallback()
{
    for (auto* a : ctx.animated) a->animate();
    cpu.setText ("CPU " + String (roundToInt (proc.cpuLoad.load() * 100.0f)) + "%", dontSendNotification);
    auto& tel = proc.engine.telemetry;
    const bool playing = tel.playing.load();
    const double beat = tel.beat.load();
    const int bar = (int) std::floor (beat / 4.0) + 1, b = (int) std::floor (std::fmod (std::max (0.0, beat), 4.0)) + 1;
    const int key = playing ? bar * 10 + b : -1;
    if (key != lastTransportKey)
    {
        lastTransportKey = key;
        transport.setText (playing ? String (bar) + "." + String (b) + "  " + String::charToString ((juce_wchar) ('A' + tel.activePattern.load()))
                                   : String (proc.getParamReal ("g_bpm"), 1) + " BPM", dontSendNotification);
    }
    if (! focusGrabbed && isShowing() && getPeer() != nullptr)
    {
        focusGrabbed = true;
        grabKeyboardFocus();
    }
}

bool DrumEngineEditor::keyPressed (const KeyPress& k)
{
    if (dynamic_cast<TextEditor*> (Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown())
    {
        // Cmd+Z / Cmd+Shift+Z (or Cmd+Y): the same history as DESHACER and REHACER, for the knobs and the patterns
        const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) k.getKeyCode());
        if (ch == 'z' && ! mods.isShiftDown()) { proc.undo.undo(); return true; }
        if ((ch == 'z' && mods.isShiftDown()) || ch == 'y') { proc.undo.redo(); return true; }
        // Cmd+C, Cmd+V, Cmd+D, Cmd+A go to the grid on screen even when it does not have the focus
        std::function<Component* (Component&)> grid = [&grid] (Component& c) -> Component*
        {
            if (c.isVisible() && (dynamic_cast<be::ui::BeatGrid*> (&c) != nullptr || dynamic_cast<be::ui::StepGrid*> (&c) != nullptr)) return &c;
            for (auto* ch2 : c.getChildren()) if (ch2->isVisible()) if (auto* g = grid (*ch2)) return g;
            return nullptr;
        };
        if (auto* g = grid (*this)) return g->keyPressed (k);
        return false;                       // never a pad: Cmd+C is not the pad on C
    }
    const int t = padForKey (k.getTextCharacter());
    if (t >= 0)
    {
        if (! padStrip.keyDown[t]) { padStrip.keyDown[t] = true; proc.padDown (t, 0.85f); ctx.selectTrack (t); }
        return true;
    }
    if (k == KeyPress::spaceKey) { if (auto* pp = proc.apvts.getParameter ("g_play")) pp->setValueNotifyingHost (pp->getValue() > 0.5f ? 0.0f : 1.0f); return true; }
    return false;
}

bool DrumEngineEditor::keyStateChanged (bool)
{
    bool used = false;
    for (int t = 0; t < be::kNumTracks; ++t)
        if (padStrip.keyDown[t] && ! KeyPress::isKeyCurrentlyDown ((int) CharacterFunctions::toUpperCase ((juce_wchar) kPadKeys[t])))
        {
            padStrip.keyDown[t] = false;
            proc.padUp (t);
            used = true;
        }
    return used;
}

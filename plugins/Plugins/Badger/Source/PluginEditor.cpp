#include "installer/SelfInstaller.h"
#include "I18n.h"
#include "PluginEditor.h"
#include "presets/PresetManager.h"

using namespace juce;
using namespace be::ui;

namespace
{
constexpr int headerH = 86, footerH = 96, keysH = 80;
}

// ===================================================================== root / logo
void BassEngineEditor::Root::paint (Graphics& g)
{
    g.fillAll (be::ui::Pro::win);   // the 2026 look: a dark working surface
    auto r = getLocalBounds().toFloat();
    // header: Blanco Leche band with a Ciruela rule
    auto head = r.removeFromTop ((float) headerH);
    g.setColour (S::bg());          // the header keeps the animal's colour
    g.fillRect (head);
    g.setColour (S::shade);
    g.fillRect (head.removeFromBottom (3.0f));
    // keyboard band
    auto keys = r.removeFromBottom ((float) keysH);
    g.setColour (S::shade);
    g.fillRect (keys);
    // macro band (only in the full view)
    if (! ed.easyMode)
    {
        auto foot = r.removeFromBottom ((float) footerH);
        g.setColour (S::card);
        g.fillRect (foot);
        g.setColour (juce::Colour (0xff3a3236));
        g.fillRect (foot.removeFromTop (1.0f));
        g.setFont (Fonts::label (11.0f));
        g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr ("MACROS  ·  ARRASTRA EL CUADRITO SOBRE CUALQUIER PERILLA PARA ASIGNAR"_u), foot.removeFromBottom (14.0f).reduced (12.0f, 0.0f), Justification::centredLeft);
    }
}

void BassEngineEditor::Logo::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto mark = r.removeFromLeft (r.getHeight()).reduced (2.0f);
    Draw::box (g, mark, S::shade, 0.0f, 0.0f);
    Draw::racoonMark (g, mark.reduced (4.0f), S::accent(), S::shade, S::card);
    g.setColour (S::card);
    g.drawRect (mark.reduced (4.0f).withTrimmedTop (mark.getHeight() * 0.0f), 0.0f);
    r.removeFromLeft (10.0f);
    auto top = r.removeFromTop (r.getHeight() * 0.62f);
    g.setColour (S::shade);
    g.setFont (Fonts::title (26.0f));
    const String name = "Badger";
    g.drawText (::racoon::i18n::tr ("Racoon"), top, Justification::bottomLeft);
    const float w = GlyphArrangement::getStringWidth (Fonts::title (26.0f), ::racoon::i18n::tr ("Racoon "));
    g.setColour (S::shade.withAlpha (0.7f));
    g.drawText (::racoon::i18n::tr (name), top.withTrimmedLeft (w), Justification::bottomLeft);
    g.setColour (S::shade);
    g.setFont (Fonts::label (11.5f));
    g.drawText (::racoon::i18n::tr ("SINTETIZADOR DE BAJOS  ·  v2.0"_u), r, Justification::centredLeft);
}

// ===================================================================== macro cell
BassEngineEditor::MacroCell::MacroCell (Ctx& c, int i) : ctx (c), index (i), knob (c, be::macroId (i)), chip (c, be::ModSrc::Macro1 + i, " ")
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

void BassEngineEditor::MacroCell::resized()
{
    auto r = getLocalBounds();
    auto lbl = r.removeFromBottom (16);
    editor.setBounds (lbl);
    chip.setBounds (r.removeFromRight (16).removeFromTop (16));
    knob.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight())));
}

void BassEngineEditor::MacroCell::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Draw::engraved (g, ctx.proc.getMacroName (index), r.removeFromBottom (16.0f), Fonts::label (11.0f), Justification::centred, C::bone.withAlpha (0.8f));
}

void BassEngineEditor::MacroCell::mouseDoubleClick (const MouseEvent& e)
{
    if (e.y > getHeight() - 18)
    {
        editor.setText (ctx.proc.getMacroName (index), false);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }
}

// ===================================================================== editor
BassEngineEditor::BassEngineEditor (BassEngineProcessor& p)
    : AudioProcessorEditor (p), proc (p), ctx (p),
      headerPlay (ctx, false),
      quality (ctx, "v_quality"), miniScope (ctx, true), meter (ctx), master (ctx, "master_volume", "VOLUMEN"),
      tabs ({}, [this] (int i) { auto v = visibleTabs(); if (i >= 0 && i < (int) v.size()) showPage (v[(size_t) i]); }),
      simplePage (ctx), synthPage (ctx), modPage (ctx), acidPage (ctx), seqPage (ctx), fxPage (ctx), mixPage (ctx), analyzePage (ctx), presetPage (ctx),
      keyboard (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&look);
    LookAndFeel::setDefaultLookAndFeel (&look);
    doceveinte::runSelfInstallerIfNeeded (JucePlugin_Name, p.wrapperType, { "Bass Engine", "Racoon Badger", "Racoon Badger Bass" });
    addAndMakeVisible (root);
    pages = { &simplePage, &synthPage, &modPage, &acidPage, &seqPage, &fxPage, &mixPage, &analyzePage, &presetPage };
    for (auto* pg : pages) root.addChildComponent (pg);
    for (Component* c : std::initializer_list<Component*> { &logo, &prevBtn, &nextBtn, &presetName, &saveBtn, &abA, &abB, &abCopy, &undoBtn, &redoBtn,
                                                            &easyBtn, &fullBtn, &headerPlay, &quality, &cpu, &miniScope, &meter, &master, &tabs })
        root.addAndMakeVisible (c);
    for (int i = 0; i < be::kNumMacros; ++i) root.addAndMakeVisible (macros.add (new MacroCell (ctx, i)));
    root.addAndMakeVisible (toSong);
    toSong.setButtonText ("PEGAR EN MAQUETA");
    toSong.setColour (TextButton::buttonColourId, Brand::limon);
    toSong.makeMidi = [this] { return proc.maquetaMidi(); };
    toSong.animal = [] { return String ("Badger"); };
    toSong.trackName = [this] { return proc.trackName(); };
    // from now on the clip in the song plays it: the plugin's own pattern stops following the DAW
    toSong.binding = &proc.songClip;
    toSong.onEdit = [this] (const racoon::maqueta::EditRequest& r) { proc.loadFromSong (r); showPage (pSeq); };
    toSong.afterSend = [this]
    {
        proc.clipEditing = false;
        proc.extra.removeProperty ("clipEditing", nullptr);
        if (auto* m = proc.apvts.getParameter ("seq_mode")) if (m->getValue() > 0.0f && m->getValue() < 0.75f) m->setValueNotifyingHost (0.0f);
        proc.seqPreview = false;
    };
    root.addAndMakeVisible (exportBtn);
    root.addAndMakeVisible (autoBtn);
    exportBtn.setTooltip (String::fromUTF8 ("Exportar el patrón con su automatización como archivo MIDI (la automatización viaja como MIDI CC)"));
    autoBtn.setTooltip (String::fromUTF8 ("Ver y dibujar la automatización. También: clic derecho en cualquier perilla > Ver automatización."));
    exportBtn.onClick = [this]
    {
        racoon::exportMidiWithChooser (chooser, "Racoon Badger - " + Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M") + ".mid",
                                       [this] (const File& f) { auto tmp = proc.exportPatternToMidi (jmax (0, roundToInt (proc.apvts.getRawParameterValue ("seq_pattern")->load()) - 1)); return tmp.existsAsFile() && tmp.copyFileTo (f); });
    };
    autoBtn.onClick = [this] { auto ids = proc.automation.laneIds(); openAutomation (ids.isEmpty() ? String() : ids[0], false); };

    // keyboard
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, S::card);
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, S::ink);
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, S::ink);
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, S::hi.withAlpha (0.5f));
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, S::accent());
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, S::inkSoft);
    keyboard.setColour (MidiKeyboardComponent::upDownButtonBackgroundColourId, S::hi);
    keyboard.setColour (MidiKeyboardComponent::upDownButtonArrowColourId, S::ink);
    keyboard.setAvailableRange (12, 96);
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyWidth (22.0f);
    keyboard.setVelocity (0.85f, true);
    keyboard.setWantsKeyboardFocus (true);
    keyboard.setMouseClickGrabsKeyboardFocus (true);
    root.addAndMakeVisible (keyboard);
    for (auto* b : { &octDown, &octUp }) root.addAndMakeVisible (b);
    octDown.onClick = [this] { setKbOctave (kbOctave - 1); };
    octUp.onClick = [this] { setKbOctave (kbOctave + 1); };
    kbHint.setFont (Fonts::body (12.5f));
    kbHint.setColour (Label::textColourId, S::cardDim);
    kbHint.setJustificationType (Justification::centredLeft);
    root.addAndMakeVisible (kbHint);
    setKbOctave (3);

    presetName.setTooltip ("Sonido actual. Click para ver todos los sonidos.");
    presetName.onClick = [this] { showPage (pPresets); };
    for (auto* b : std::initializer_list<juce::Button*> { &prevBtn, &nextBtn, &presetName, &saveBtn, &abA, &abB, &abCopy, &undoBtn, &redoBtn, &easyBtn, &fullBtn })
        b->getProperties().set ("onBrand", true);   // they sit on the animal's coloured header
    prevBtn.onClick = [this] { proc.presets().step (-1); };
    nextBtn.onClick = [this] { proc.presets().step (1); };
    prevBtn.setTooltip ("Sonido anterior"); nextBtn.setTooltip ("Sonido siguiente");
    saveBtn.setTooltip ("Guarda tu sonido con nombre y categoría."_u);
    saveBtn.onClick = [this] { showPage (pPresets); };
    easyBtn.setTooltip ("Vista PRINCIPIANTE: lo esencial para hacer sonar un bajo en cinco minutos."_u);
    fullBtn.setTooltip ("Vista AVANZADA: todas las páginas, osciladores, modulación, efectos y análisis."_u);
    easyBtn.setClickingTogglesState (false); fullBtn.setClickingTogglesState (false);
    easyBtn.onClick = [this] { setEasyMode (true); };
    fullBtn.onClick = [this] { setEasyMode (false); };
    abA.setClickingTogglesState (false); abB.setClickingTogglesState (false);
    abA.setTooltip ("Comparar: versión A"_u); abB.setTooltip ("Comparar: versión B"_u); abCopy.setTooltip ("Copia la versión A en la B"_u);
    abA.onClick = [this] { if (proc.currentABSlot() != 0) proc.recallAB (0); refreshAfterPresetChange(); };
    abB.onClick = [this] { if (proc.currentABSlot() != 1) proc.recallAB (1); refreshAfterPresetChange(); };
    abCopy.onClick = [this] { proc.copyAtoB(); };
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    undoBtn.setTooltip ("Deshacer (Cmd+Z)"); redoBtn.setTooltip ("Rehacer (Shift+Cmd+Z)");
    quality.box.setTooltip ("Calidad. Eco gasta menos CPU, Normal sirve para casi todo y High se usa siempre al exportar.");
    cpu.setFont (Fonts::mono (12.0f));
    cpu.setColour (Label::textColourId, S::inkSoft);
    cpu.setJustificationType (Justification::centred);

    ctx.showPage = [this] (int pg) { showPage (pg); };
    ctx.showRoute = [this] (int r) { if (easyMode) setEasyMode (false); showPage (pMod); modPage.showRoute (r); };
    proc.onPresetLoaded = [this] { refreshAfterPresetChange(); };

    {
        PropertiesFile::Options o; o.applicationName = "Racoon Badger"; o.folderName = "Racoon"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        PropertiesFile settings (o);
        easyMode = settings.getBoolValue ("easyMode", true);
    }
    setEasyMode (easyMode);
    showPage (pInicio);
    refreshAfterPresetChange();

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
    startTimerHz (30);
}

BassEngineEditor::~BassEngineEditor()
{
    stopTimer();
    proc.onPresetLoaded = nullptr;
    setLookAndFeel (nullptr);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void BassEngineEditor::paint (Graphics& g) { g.fillAll (be::ui::Pro::win); }

std::vector<int> BassEngineEditor::visibleTabs() const
{
    if (easyMode) return { pInicio, pSeq, pPresets };
    return { pInicio, pSynth, pMod, pAcid, pSeq, pFx, pMix, pAnalyze, pPresets };
}

void BassEngineEditor::setEasyMode (bool e)
{
    easyMode = e;
    {
        PropertiesFile::Options o; o.applicationName = "Racoon Badger"; o.folderName = "Racoon"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        PropertiesFile settings (o);
        settings.setValue ("easyMode", e);
    }
    static const char* names[] { "INICIO", "SÍNTESIS", "MODULACIÓN", "ACID", "PATRÓN", "EFECTOS", "MEZCLA", "ANÁLISIS", "SONIDOS" };
    StringArray n;
    for (auto p : visibleTabs()) n.add (String::fromUTF8 (names[p]));
    tabs.setNames (n);
    easyBtn.setToggleState (e, dontSendNotification);
    fullBtn.setToggleState (! e, dontSendNotification);
    for (auto* m : macros) m->setVisible (! e);
    auto v = visibleTabs();
    if (std::find (v.begin(), v.end(), currentPage) == v.end()) showPage (pInicio);
    else showPage (currentPage);
    resized();
    root.repaint();
}

void BassEngineEditor::openAutomation (const String& paramId, bool record)
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

void BassEngineEditor::resized()
{
    const int panelH = autoPanel != nullptr && autoPanel->isVisible() ? racoon::AutomationPanel::preferredHeight : 0;
    if (panelH > 0) autoPanel->setBounds (0, getHeight() - panelH, getWidth(), panelH);
    const float s = jmin ((float) getWidth() / (float) baseW, (float) (getHeight() - panelH) / (float) baseH);
    root.setBounds (0, 0, baseW, baseH);
    root.setTransform (AffineTransform::scale (s).translated (((float) getWidth() - (float) baseW * s) * 0.5f, 0.0f));
    proc.extra.setProperty ("uiScale", s, nullptr);

    auto r = Rectangle<int> (0, 0, baseW, baseH);
    auto head = r.removeFromTop (headerH);
    auto row1 = head.removeFromTop (50).reduced (10, 6);
    logo.setBounds (row1.removeFromLeft (270));
    row1.removeFromLeft (10);
    prevBtn.setBounds (row1.removeFromLeft (30));
    presetName.setBounds (row1.removeFromLeft (260).reduced (3, 0));
    nextBtn.setBounds (row1.removeFromLeft (30));
    row1.removeFromLeft (6);
    saveBtn.setBounds (row1.removeFromLeft (80));
    row1.removeFromLeft (12);
    abA.setBounds (row1.removeFromLeft (30)); row1.removeFromLeft (3); abB.setBounds (row1.removeFromLeft (30)); row1.removeFromLeft (3);
    abCopy.setBounds (row1.removeFromLeft (42));
    row1.removeFromLeft (12);
    undoBtn.setBounds (row1.removeFromLeft (78)); row1.removeFromLeft (3);
    redoBtn.setBounds (row1.removeFromLeft (74));
    // right side
    master.setBounds (row1.removeFromRight (60).withY (row1.getY() - 6).withHeight (50));
    row1.removeFromRight (6);
    meter.setBounds (row1.removeFromRight (16));
    row1.removeFromRight (6);
    miniScope.setBounds (row1.removeFromRight (96));
    row1.removeFromRight (8);
    cpu.setBounds (row1.removeFromRight (56));
    quality.setBounds (row1.removeFromRight (78).reduced (0, 5));
    auto row2 = head.reduced (10, 2).withTrimmedBottom (4);
    headerPlay.setBounds (row2.removeFromRight (120));
    row2.removeFromRight (8);
    autoBtn.setBounds (row2.removeFromRight (118));
    row2.removeFromRight (4);
    exportBtn.setBounds (row2.removeFromRight (96));
    row2.removeFromRight (4);
    toSong.setBounds (row2.removeFromRight (156));
    row2.removeFromRight (14);
    easyBtn.setBounds (row2.removeFromLeft (112)); row2.removeFromLeft (2);
    fullBtn.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (14);
    tabs.setBounds (easyMode ? row2.withWidth (std::min (row2.getWidth(), 3 * 160)) : row2);

    r.removeFromBottom (keysH);
    if (! easyMode)
    {
        auto foot = r.removeFromBottom (footerH).reduced (10, 8);
        foot.removeFromBottom (14);
        const int w = foot.getWidth() / be::kNumMacros;
        for (int i = 0; i < macros.size(); ++i) macros[i]->setBounds (foot.getX() + i * w + 3, foot.getY() + 2, w - 6, foot.getHeight() - 2);
    }
    auto kb = Rectangle<int> (0, baseH - keysH, baseW, keysH).reduced (10, 8);
    auto side = kb.removeFromLeft (190);
    auto ob = side.removeFromTop (28);
    octDown.setBounds (ob.removeFromLeft (64)); ob.removeFromLeft (6); octUp.setBounds (ob.removeFromLeft (64));
    kbHint.setBounds (side);
    kb.removeFromLeft (8);
    keyboard.setBounds (kb);
    for (auto* pg : pages) pg->setBounds (r.withTrimmedTop (4));
}

void BassEngineEditor::showPage (int p)
{
    currentPage = jlimit (0, (int) numPages - 1, p);
    auto v = visibleTabs();
    if (std::find (v.begin(), v.end(), currentPage) == v.end()) { setEasyMode (false); v = visibleTabs(); }
    for (int i = 0; i < (int) pages.size(); ++i) pages[(size_t) i]->setVisible (i == currentPage);
    tabs.setSelected ((int) (std::find (v.begin(), v.end(), currentPage) - v.begin()));
}

void BassEngineEditor::refreshAfterPresetChange()
{
    presetName.setButtonText (proc.currentPresetName);
    abA.setToggleState (proc.currentABSlot() == 0, dontSendNotification);
    abB.setToggleState (proc.currentABSlot() == 1, dontSendNotification);
    for (auto* m : macros) m->repaint();
    presetPage.refresh();
    simplePage.refresh();
    seqPage.visibilityChanged();
    repaint();
}

void BassEngineEditor::timerCallback()
{
    // copy: animate() of a component may (rarely) rebuild children that register themselves
    auto list = ctx.animated;
    for (auto* a : list)
        if (std::find (ctx.animated.begin(), ctx.animated.end(), a) != ctx.animated.end()) a->animate();
    if (! focusGrabbed && isShowing()) { keyboard.grabKeyboardFocus(); focusGrabbed = true; }
    static int tick = 0;
    if (++tick % 10 == 0)
    {
        cpu.setText ("CPU " + String (roundToInt (proc.cpuLoad.load() * 100.0f)) + "%", dontSendNotification);
        if (presetName.getButtonText() != proc.currentPresetName) presetName.setButtonText (proc.currentPresetName);
        undoBtn.setEnabled (proc.undo.canUndo());
        redoBtn.setEnabled (proc.undo.canRedo());
    }
}

void BassEngineEditor::setKbOctave (int o)
{
    kbOctave = jlimit (0, 6, o);
    keyboard.setKeyPressBaseOctave (kbOctave);
    keyboard.setLowestVisibleKey (jlimit (12, 60, kbOctave * 12 - 12));
    const String root = MidiMessage::getMidiNoteName (kbOctave * 12, true, true, 3);
    kbHint.setText ("A S D F tocan desde " + root + "\nEspacio toca el patrón"_u, dontSendNotification);
    kbHint.setTooltip ("Z y X bajan y suben de octava el teclado de tu compu.");
}

bool BassEngineEditor::keyPressed (const KeyPress& k)
{
    if (! k.getModifiers().isAnyModifierKeyDown())
    {
        if (k.getTextCharacter() == 'z' || k.getTextCharacter() == 'Z') { setKbOctave (kbOctave - 1); return true; }
        if (k.getTextCharacter() == 'x' || k.getTextCharacter() == 'X') { setKbOctave (kbOctave + 1); return true; }
    }
    if (k == KeyPress ('z', ModifierKeys::commandModifier, 0)) { proc.undo.undo(); return true; }
    if (k == KeyPress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0) || k == KeyPress ('y', ModifierKeys::commandModifier, 0)) { proc.undo.redo(); return true; }
    if (k.getModifiers().isCommandDown())
    {
        // Cmd+C, Cmd+X, Cmd+V, Cmd+D, Cmd+A go to the piano roll on screen even when it does not have the focus
        const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) k.getKeyCode());
        if (ch == 'c' || ch == 'x' || ch == 'v' || ch == 'd' || ch == 'a')
        {
            std::function<Component* (Component&)> roll = [&roll] (Component& c) -> Component*
            {
                if (c.isVisible() && dynamic_cast<be::ui::StepGrid*> (&c) != nullptr) return &c;
                for (auto* x : c.getChildren()) if (x->isVisible()) if (auto* g = roll (*x)) return g;
                return nullptr;
            };
            if (auto* g = roll (*this)) return g->keyPressed (k);
        }
    }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '9') { showPage (k.getKeyCode() - '1'); return true; }
    if (k == KeyPress::spaceKey) { be::ui::PlayButton::toggle (ctx); return true; }
    return false;
}

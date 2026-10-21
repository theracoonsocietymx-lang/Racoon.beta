#include "RacoonEditor.h"
#include "I18n.h"
#include <BinaryData.h>
#include "SelfInstaller.h"

namespace racoon
{
using namespace juce;
using namespace racoon::ui;

namespace
{
constexpr int headerH = 60, footerH = 30, cardPad = 10;
}

void drawScreen (Graphics& g, Rectangle<float> r)
{
    ProDraw::screen (g, r);
}

void drawScreenGrid (Graphics& g, Rectangle<float> r, int v, int h)
{
    g.setColour (Pro::grid);
    for (int i = 1; i < v; ++i) g.fillRect (r.getX() + r.getWidth() * (float) i / (float) v, r.getY(), 1.0f, r.getHeight());
    for (int i = 1; i < h; ++i) g.fillRect (r.getX(), r.getY() + r.getHeight() * (float) i / (float) h, r.getWidth(), 1.0f);
}

// ===================================================================== control
ParamControl::ParamControl (RacoonProcessor& p, const Ctl& c) : spec (c), proc (p)
{
    param = dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (c.id));
    jassert (param != nullptr);   // the layout names a parameter that does not exist
    setTooltip (c.help);

    if (dynamic_cast<AudioParameterChoice*> (param) != nullptr)
    {
        combo = std::make_unique<ui::PopupCombo>();
        combo->onPopupMenu = [this] { showParamMenu (*this, spec.id); };
        combo->addItemList (dynamic_cast<AudioParameterChoice*> (param)->choices, 1);
        combo->setTooltip (c.help);
        addAndMakeVisible (*combo);
        cAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, c.id, *combo);
    }
    else if (dynamic_cast<AudioParameterBool*> (param) != nullptr || dynamic_cast<ToggleParam*> (param) != nullptr)
    {
        toggle = std::make_unique<ui::PopupTextButton> (upper (c.label));
        toggle->onPopupMenu = [this] { showParamMenu (*this, spec.id); };
        toggle->setClickingTogglesState (true);
        toggle->setTooltip (c.help);
        addAndMakeVisible (*toggle);
        bAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, c.id, *toggle);
    }
    else
    {
        knob = std::make_unique<racoon::ui::GrabSlider> (Slider::RotaryHorizontalVerticalDrag, Slider::NoTextBox);
        knob->setTooltip (c.help);
        knob->onPopupMenu = [this] { showParamMenu (*this, spec.id); };
        knob->setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        knob->setMouseDragSensitivity (220);
        if (param->getNormalisableRange().getRange().getStart() < 0.0f && param->getNormalisableRange().getRange().getEnd() > 0.0f)
            knob->getProperties().set ("bipolar", true);
        addAndMakeVisible (*knob);
        sAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, c.id, *knob);
        knob->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        valueLabel.setJustificationType (Justification::centred);
        valueLabel.setFont (NumFont::medium (12.0f));
        valueLabel.setColour (Label::textColourId, Pro::text);
        valueLabel.setEditable (false, true);   // double click: type a value
        valueLabel.setTooltip (String::fromUTF8 ("Doble clic para escribir el valor"));
        valueLabel.onTextChange = [this]
        {
            auto t = valueLabel.getText().trim();
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getValueForText (t.upToFirstOccurrenceOf (" ", false, false)));
            param->endChangeGesture();
        };
        addAndMakeVisible (valueLabel);
    }
    startTimerHz (12);
    timerCallback();
}

ParamControl::~ParamControl() { sAtt.reset(); cAtt.reset(); bAtt.reset(); }

void ParamControl::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showParamMenu (*this, spec.id);
}

void ParamControl::timerCallback()
{
    if (const bool a = proc.automation.hasPoints (spec.id); a != automated) { automated = a; repaint(); }
    if (knob == nullptr || valueLabel.isBeingEdited()) return;
    auto t = param->getCurrentValueAsText();
    if (t != valueLabel.getText()) valueLabel.setText (t, dontSendNotification);
}

String ParamControl::helpLine() const
{
    return upper (spec.label) + "  ·  "_u + param->getCurrentValueAsText() + "   " + spec.help;
}

int ParamControl::preferredWidth() const
{
    if (combo) return 128;
    if (toggle) return 104;
    return 76;
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    if (combo)
    {
        r.removeFromTop (18);
        combo->setBounds (r.removeFromTop (28).reduced (2, 0));
        return;
    }
    if (toggle)
    {
        toggle->setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, 30).withY (r.getY() + 22));
        return;
    }
    r.removeFromTop (16);
    valueLabel.setBounds (r.removeFromBottom (18));
    const int s = jmin (r.getWidth() - 8, r.getHeight());
    knob->setBounds (r.withSizeKeepingCentre (s, s));
}

void ParamControl::paint (Graphics& g)
{
    if (automated)
    {
        // a pink dot: this control has an automation line
        g.setColour (Brand::rosa);
        g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withPosition ((float) getWidth() - 10.0f, 2.0f));
        g.setColour (Pro::win);
        g.drawEllipse (Rectangle<float> (8.0f, 8.0f).withPosition ((float) getWidth() - 10.0f, 2.0f), 1.0f);
    }
    if (toggle) return;
    ProDraw::caps (g, spec.label, getLocalBounds().removeFromTop (16).toFloat(), combo ? Justification::centredLeft : Justification::centred);
}

// ===================================================================== section
SectionCard::SectionCard (RacoonProcessor& p, const Sec& s) : title (s.title)
{
    for (auto& c : s.controls) addAndMakeVisible (controls.add (new ParamControl (p, c)));
}

int SectionCard::preferredWidth (int rows) const
{
    int w = 0;
    for (auto* c : controls) w += c->preferredWidth() + 4;
    return (w + rows - 1) / jmax (1, rows) + cardPad * 2 + 8;
}

void SectionCard::resized()
{
    auto r = getLocalBounds().reduced (cardPad, 6).withTrimmedTop (22).withTrimmedRight (4).withTrimmedBottom (4);
    if (controls.isEmpty()) return;
    // as many rows as needed for the controls to fit
    int total = 0;
    for (auto* c : controls) total += c->preferredWidth() + 4;
    const int rows = jlimit (1, 3, (int) std::ceil ((double) total / (double) jmax (1, r.getWidth())));
    const int rowH = r.getHeight() / rows;
    int idx = 0;
    for (int row = 0; row < rows; ++row)
    {
        auto line = r.removeFromTop (rowH);
        const int count = (int) std::ceil ((double) (controls.size() - idx) / (double) (rows - row));
        int lineW = 0;
        for (int i = idx; i < idx + count; ++i) lineW += controls[i]->preferredWidth() + 4;
        const float scale = jmin (1.6f, (float) line.getWidth() / (float) jmax (1, lineW));
        for (int i = idx; i < idx + count; ++i)
        {
            const int w = (int) ((float) (controls[i]->preferredWidth() + 4) * scale);
            controls[i]->setBounds (line.removeFromLeft (w).reduced (2, 0)
                                      .withSizeKeepingCentre (jmin (w - 4, (int) (controls[i]->preferredWidth() * 1.35f)), jmin (line.getHeight(), 126)));
        }
        idx += count;
    }
}

void SectionCard::paint (Graphics& g)
{
    ProDraw::card (g, getLocalBounds().toFloat().reduced (2.0f), title, mark);
}

// ===================================================================== editor
PropertiesFile& RacoonEditor::settings()
{
    static std::unique_ptr<PropertiesFile> p = []
    {
        PropertiesFile::Options o;
        o.applicationName = "Plugins";
        o.filenameSuffix = ".settings";
        o.folderName = "Racoon";
        o.osxLibrarySubFolder = "Application Support";
        return std::make_unique<PropertiesFile> (o);
    }();
    return *p;
}

Image RacoonEditor::badge (const String& animal)
{
    int size = 0;
    const auto name = animal.toLowerCase() + "_256_png";
    if (auto* data = BinaryData::getNamedResource (name.toRawUTF8(), size))
        return ImageCache::getFromMemory (data, size);
    return {};
}

static RacoonProcessor& withSurface (RacoonProcessor& p)
{
    ui::gSurface = { p.info.colour, p.info.colour };
    return p;
}

RacoonEditor::RacoonEditor (RacoonProcessor& p, EditorSpec s, std::unique_ptr<Component> disp)
    : AudioProcessorEditor (withSurface (p)), proc (p), spec (std::move (s)), display (std::move (disp))
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);
    if (display) body.addAndMakeVisible (*display);

    const Colour marks[] { Brand::rosa, Brand::menta, Brand::limon };
    int cardIndex = 0;
    for (auto& sec : spec.sections)
    {
        auto* card = new SectionCard (proc, sec);
        card->mark = marks[cardIndex++ % 3];
        (sec.advanced ? advanced : essential).add (card);
        body.addChildComponent (card);
    }
    for (int i = 0; i < advanced.size(); ++i)
    {
        auto* b = advTabs.add (new TextButton (upper (advanced[i]->title)));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4242);
        b->onClick = [this, i] { advIndex = i; layoutBody(); };
        body.addChildComponent (b);
    }
    bodyView.setViewedComponent (&body, false);
    bodyView.setScrollBarsShown (true, false);
    bodyView.setScrollBarThickness (10);
    addAndMakeVisible (bodyView);
    dock = std::make_unique<SequencerDock> (proc, spec.steps);
    addAndMakeVisible (*dock);
    addMouseListener (this, true);
    if (! advTabs.isEmpty()) advTabs[0]->setToggleState (true, dontSendNotification);

    presetButton.setTooltip ("Presets: los de fábrica por categoría y los tuyos"_u);
    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.setTooltip ("Preset anterior");
    nextButton.setTooltip ("Preset siguiente");
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    saveButton.setTooltip (String::fromUTF8 ("Guardar el sonido actual como preset tuyo"));
    saveButton.onClick = [this]
    {
        auto* w = new AlertWindow ("Guardar preset", "Tus presets aparecen en el menú, en «Mis presets»."_u, MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", proc.currentPresetName, "Nombre");
        w->addButton ("GUARDAR", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("CANCELAR", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1) { proc.saveUserPreset (w->getTextEditorContents ("n")); refreshPresetName(); }
        }), true);
    };
    aButton.setTooltip (String::fromUTF8 ("Comparar: el sonido A"));
    bButton.setTooltip ("Comparar: el sonido B. Cambia entre A y B para oír la diferencia."_u);
    copyButton.setTooltip (String::fromUTF8 ("Copia el sonido actual en A y en B"));
    for (auto* b : { &aButton, &bButton }) { b->setClickingTogglesState (true); b->setRadioGroupId (77); }
    (proc.activeAB == 0 ? aButton : bButton).setToggleState (true, dontSendNotification);
    aButton.onClick = [this] { proc.selectAB (0); refreshPresetName(); };
    bButton.onClick = [this] { proc.selectAB (1); refreshPresetName(); };
    copyButton.onClick = [this] { proc.copyAToB(); hint = String::fromUTF8 ("A y B quedaron iguales"); repaint (footer); };

    for (auto* b : { &easyButton, &fullButton }) { b->setClickingTogglesState (true); b->setRadioGroupId (78); }
    easyButton.setTooltip ("PRINCIPIANTE: solo los controles esenciales"_u);
    fullButton.setTooltip ("AVANZADO: también los ajustes avanzados, por pestañas"_u);
    easyButton.onClick = [this] { setEasy (true); };
    fullButton.onClick = [this] { setEasy (false); };

    for (auto* b : { &presetButton, &prevButton, &nextButton, &saveButton, &aButton, &bButton, &copyButton, &easyButton, &fullButton })
    {
        b->getProperties().set ("onBrand", true);   // they sit on the animal's coloured header
        addAndMakeVisible (b);
    }

    proc.onPresetLoaded = [sp = SafePointer<RacoonEditor> (this)] { if (sp != nullptr) sp->refreshPresetName(); };
    refreshPresetName();

    easy = settings().getBoolValue (proc.info.animal + ".easy", true) || advanced.isEmpty();
    // every plugin opens the same way: as wide as the screen and as tall as fits above the Dock / taskbar
    {
        Rectangle<int> area (0, 0, 1440, 860);
        if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userArea;
        const int w = jmax (960, area.getWidth());
        const int h = jmax (600, area.getHeight() - 56);      // the host's title bar
        setResizable (true, true);
        setResizeLimits (900, 560, 6000, 4000);
        setSize (w, h);
    }
    setEasy (easy);
    startTimerHz (10);
    {
        // older names of the same plugin ("Racoon Mole", "Racoon Mole Rhythm") are cleaned up by the installer
        const auto title = proc.getName().fromFirstOccurrenceOf ("Racoon - ", false, false);
        StringArray legacy;
        if (title.isNotEmpty()) legacy.add ("Racoon " + title), legacy.add ("Racoon " + title.upToFirstOccurrenceOf (" ", false, false));
        doceveinte::runSelfInstallerIfNeeded (proc.getName(), proc.wrapperType, legacy);
    }
}

RacoonEditor::~RacoonEditor()
{
    proc.onPresetLoaded = nullptr;
    setLookAndFeel (nullptr);
}

void RacoonEditor::setEasy (bool e)
{
    easy = e || advanced.isEmpty();
    settings().setValue (proc.info.animal + ".easy", easy);
    easyButton.setToggleState (easy, dontSendNotification);
    fullButton.setToggleState (! easy, dontSendNotification);
    fullButton.setEnabled (! advanced.isEmpty());
    resized();
    repaint();
}

void RacoonEditor::refreshPresetName()
{
    auto n = proc.currentPresetName.isNotEmpty() ? proc.currentPresetName : String ("Inicial");
    presetButton.setButtonText (n);
    aButton.setToggleState (proc.activeAB == 0, dontSendNotification);
    bButton.setToggleState (proc.activeAB == 1, dontSendNotification);
}

void RacoonEditor::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    if (n <= 1 && proc.factoryPresets().empty()) return;
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
    refreshPresetName();
}

void RacoonEditor::showPresetMenu()
{
    PopupMenu m;
    auto fp = proc.factoryPresets();
    std::map<String, PopupMenu> cats;
    StringArray order;
    for (int i = 0; i < (int) fp.size(); ++i)
    {
        auto c = fp[(size_t) i].category.isNotEmpty() ? fp[(size_t) i].category : String ("Fábrica"_u);
        if (! order.contains (c)) order.add (c);
        cats[c].addItem (fp[(size_t) i].name, true, proc.currentPresetName == fp[(size_t) i].name,
                         [this, i] { proc.setCurrentProgram (i); refreshPresetName(); });
    }
    m.addSectionHeader ("De fábrica"_u);
    for (auto& c : order) m.addSubMenu (c, cats[c]);
    auto user = proc.userPresetNames();
    m.addSectionHeader ("Mis presets");
    if (user.isEmpty()) m.addItem ("Todavía no guardas ninguno"_u, false, false, [] {});
    for (auto& u : user)
        m.addItem (u, true, proc.currentPresetName == u, [this, u] { proc.loadUserPreset (u); refreshPresetName(); });
    if (! user.isEmpty())
    {
        PopupMenu del;
        for (auto& u : user) del.addItem (u, [this, u] { proc.deleteUserPreset (u); });
        m.addSubMenu ("Borrar", del);
    }
    m.addSeparator();
    m.addItem ("Abrir carpeta de presets", [this] { proc.presetFolder().revealToUser(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (presetButton));
}

bool RacoonEditor::keyPressed (const KeyPress& k)
{
    if (dynamic_cast<TextEditor*> (Component::getCurrentlyFocusedComponent()) != nullptr) return false;
    const auto mods = k.getModifiers();
    if (! (mods.isCommandDown() || mods.isCtrlDown())) return false;
    const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) k.getKeyCode());
    if (ch == 'z' && ! mods.isShiftDown()) { proc.undoManager.undo(); return true; }
    if ((ch == 'z' && mods.isShiftDown()) || ch == 'y') { proc.undoManager.redo(); return true; }
    if (ch == 'c' || ch == 'x' || ch == 'v' || ch == 'd' || ch == 'a')
    {
        if (display != nullptr && display->isVisible() && display->keyPressed (k)) return true;
        if (dock != nullptr && dock->roll.isVisible() && dock->roll.keyPressed (k)) return true;
    }
    return false;
}

bool RacoonEditor::keyStateChanged (bool isKeyDown)
{
    return dock != nullptr && dock->roll.keysBelow != nullptr && dock->roll.keysBelow->keyStateChanged (isKeyDown);
}

void RacoonEditor::timerCallback()
{
    if (! focusGrabbed && isShowing() && getPeer() != nullptr)
    {
        // the keys (Cmd+Z, Cmd+C...) reach the plugin from the moment its window opens
        focusGrabbed = true;
        if (Component::getCurrentlyFocusedComponent() == nullptr) grabKeyboardFocus();
    }
    String t;
    if (auto* c = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
        for (auto* p = c; p != nullptr && p != this; p = p->getParentComponent())
        {
            if (auto* pc = dynamic_cast<ParamControl*> (p)) { t = pc->helpLine(); break; }
            if (auto* tc = dynamic_cast<TooltipClient*> (p)) { t = tc->getTooltip(); if (t.isNotEmpty()) break; }
        }
    if (t.isNotEmpty() && t != hint) { hint = t; repaint (footer); }
    else repaint (footer.withLeft (footer.getRight() - 140));
}

void RacoonEditor::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (headerH).reduced (14, 15);
    brandArea = head.removeFromLeft (250);
    fullButton.setBounds (head.removeFromRight (94));
    easyButton.setBounds (head.removeFromRight (108));
    head.removeFromRight (14);
    copyButton.setBounds (head.removeFromRight (56));
    head.removeFromRight (4);
    bButton.setBounds (head.removeFromRight (34));
    aButton.setBounds (head.removeFromRight (34));
    head.removeFromRight (14);
    saveButton.setBounds (head.removeFromRight (84));
    head.removeFromRight (4);
    nextButton.setBounds (head.removeFromRight (32));
    presetButton.setBounds (head.removeFromRight (jmin (220, head.getWidth() - 40)));
    prevButton.setBounds (head.removeFromRight (32));

    footer = r.removeFromBottom (footerH);
    if (dock != nullptr) dock->setBounds (r.removeFromBottom (dock->preferredHeight()));
    if (autoPanel != nullptr && autoPanel->isVisible()) autoPanel->setBounds (r.removeFromBottom (AutomationPanel::preferredHeight));
    bodyView.setBounds (r.reduced (0, 4));
    layoutBody();
}

void RacoonEditor::openAutomation (const String& paramId, bool record)
{
    if (autoPanel == nullptr)
    {
        AutomationStyle st;
        st.label = &Fonts::label;
        st.body = &Fonts::body;
        st.mono = &Fonts::mono;
        st.surface = S::card;
        st.ink = S::ink;
        st.inkSoft = S::inkSoft;
        st.accent = S::accent();
        autoPanel = std::make_unique<AutomationPanel> (proc.automation, st);
        autoPanel->onClose = [this] { autoPanel->setVisible (false); resized(); };
        addChildComponent (*autoPanel);
    }
    autoPanel->showParam (paramId);
    autoPanel->setRecording (record);
    autoPanel->setVisible (true);
    resized();
    if (record && ! proc.isSeqPlaying() && ! proc.hostDrivesTempo()) proc.setSeqPlaying (true);
}

void RacoonEditor::mouseDown (const MouseEvent&)
{
    proc.undoManager.beginNewTransaction();
}

void RacoonEditor::layoutBody()
{
    // what the screen and the cards were designed to need; extra room goes to the screen, a short window scrolls
    const int cardsH = jmax (150, spec.easyHeight - headerH - footerH - 20 - spec.displayEasy);
    const int advH = easy || advanced.isEmpty() ? 0 : spec.advancedHeight + 36;
    int displayBase = easy ? spec.displayEasy : spec.displayFull;
    const int visibleH = bodyView.getHeight();
    int naturalH = (display ? displayBase + 8 : 0) + cardsH + advH + 12;
    // on a short screen the screen gives up some height first, so the knobs stay in view; only then it scrolls
    if (display && naturalH > visibleH)
    {
        const int floorH = spec.displayMin > 0 ? jmin (displayBase, spec.displayMin) : displayBase * 65 / 100;
        const int give = jmax (0, jmin (naturalH - visibleH, displayBase - floorH));
        displayBase -= give;
        naturalH -= give;
    }
    const int extra = jmax (0, visibleH - naturalH);
    naturalH += extra;
    const bool scrolls = naturalH > visibleH;
    body.setSize (bodyView.getWidth() - (scrolls ? bodyView.getScrollBarThickness() : 0), naturalH);

    auto r = body.getLocalBounds().reduced (12, 6);
    if (display)
    {
        display->setBounds (r.removeFromTop (displayBase + extra));
        r.removeFromTop (8);
    }
    auto essentialArea = r.removeFromTop (cardsH);
    // essential cards share the row by their preferred width
    Array<SectionCard*> shown;
    for (auto* c : essential) { const bool v = showEssential (*c); c->setVisible (v); if (v) shown.add (c); }
    int total = 0;
    for (auto* c : shown) total += c->preferredWidth (1);
    const float fullW = (float) essentialArea.getWidth();
    for (int i = 0; i < shown.size(); ++i)
    {
        auto* c = shown[i];
        const int w = i == shown.size() - 1 ? essentialArea.getWidth()
                                              : (int) (fullW * (float) c->preferredWidth (1) / (float) jmax (1, total));
        c->setBounds (essentialArea.removeFromLeft (w));
    }

    for (auto* b : advTabs) b->setVisible (! easy);
    for (int i = 0; i < advanced.size(); ++i) advanced[i]->setVisible (! easy && i == advIndex);
    if (! easy && ! advanced.isEmpty())
    {
        r.removeFromTop (6);
        auto tabs = r.removeFromTop (26);
        const int tw = jmin (150, tabs.getWidth() / jmax (1, advTabs.size()));
        for (auto* b : advTabs) { b->setBounds (tabs.removeFromLeft (tw).reduced (2, 0)); }
        r.removeFromTop (4);
        advanced[advIndex]->setBounds (r.removeFromTop (spec.advancedHeight));
    }
}

void RacoonEditor::paint (Graphics& g)
{
    g.fillAll (Pro::win);
    // the header keeps the animal's colour, with the mascot and the name in Ciruela and a hard line under it
    auto head = getLocalBounds().removeFromTop (headerH);
    g.setColour (S::bg());
    g.fillRect (head);
    g.setColour (Pro::win);
    g.fillRect (head.removeFromBottom (3));

    auto b = brandArea;
    auto img = badge (proc.info.animal);
    if (img.isValid()) g.drawImage (img, b.removeFromLeft (b.getHeight() + 4).toFloat(), RectanglePlacement::centred);
    else Draw::racoonMark (g, b.removeFromLeft (b.getHeight()).toFloat(), Brand::leche, Pro::win, Brand::leche);
    b.removeFromLeft (10);
    ProDraw::caps (g, "Racoon  /  " + proc.info.function, b.removeFromTop (14).toFloat(), Justification::bottomLeft, Pro::win.withAlpha (0.7f), 9.5f);
    g.setColour (Pro::win);
    g.setFont (Fonts::title (21.0f));
    g.drawFittedText (::racoon::i18n::tr (proc.info.animal), b, Justification::centredLeft, 1, 0.8f);

    // footer: help for what is under the mouse, plus in / out meters with their numbers
    g.setColour (Pro::head);
    g.fillRect (footer);
    g.setColour (Pro::line);
    g.fillRect (footer.withHeight (1));
    auto f = footer.reduced (12, 0);
    auto meters = f.removeFromRight (250);
    g.setColour (Pro::textDim);
    g.setFont (Fonts::body (12.5f));
    g.drawFittedText (::racoon::i18n::tr (hint.isNotEmpty() ? hint : "Pasa el ratón sobre un control para ver qué hace. Doble clic en una perilla: valor inicial."_u),
                      f, Justification::centredLeft, 1, 0.85f);
    auto meter = [&g] (Rectangle<int> r, float v, const char* label)
    {
        r.removeFromLeft (10);
        ProDraw::caps (g, label, r.removeFromLeft (28).toFloat(), Justification::centredLeft, Pro::textFaint, 9.5f);
        const float db = jmax (-99.0f, dsp::gainToDb (v));
        g.setFont (NumFont::medium (11.0f));
        g.setColour (db > 0.0f ? Pro::reduce : Pro::textDim);
        g.drawText (::racoon::i18n::tr (db <= -60.0f ? String::fromUTF8 ("-\xe2\x88\x9e") : String (db, 1)), r.removeFromRight (40), Justification::centredRight);
        auto bar = r.reduced (4, 10).toFloat();
        g.setColour (Pro::raised);
        g.fillRect (bar);
        g.setColour (db > 0.0f ? Pro::reduce : (label[0] == 'I' ? Pro::input : Pro::value));
        g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f)));
        g.setColour (Pro::textFaint);
        g.fillRect (bar.getX() + bar.getWidth() * 60.0f / 66.0f, bar.getY() - 2.0f, 1.0f, bar.getHeight() + 4.0f);
    };
    meter (meters.removeFromLeft (115), proc.inPeak.load(), "IN");
    meter (meters, proc.outPeak.load(), "OUT");
}

void RacoonEditor::paintOverChildren (Graphics&) {}
}

#include "MainComponent.h"
#include "engine/I18n.h"
#include "SongMaker.h"
#include "Aleatorio.h"
#include "engine/CrashLog.h"
#include "Menus.h"
#include "NoteTools.h"
#include <map>

namespace racoon::ui
{
using namespace juce;

// ===================================================================== header bar
class MainComponent::Header : public Component, private Timer
{
public:
    Header (MainComponent& o) : owner (o), vs (o.vs), s (o.session)
    {
        auto setupGlyph = [this] (GlyphButton& b, const String& help, std::function<void()> fn)
        {
            b.setTooltip (help);
            b.onClick = std::move (fn);
            addAndMakeVisible (b);
        };
        setupGlyph (toStart, String::fromUTF8 ("Volver al principio (Inicio)"), [this] { s.returnToStart(); });
        setupGlyph (play, String::fromUTF8 ("Reproducir / parar (Espacio)"), [this] { s.togglePlay(); });
        setupGlyph (stop, String::fromUTF8 ("Parar. Pulsa otra vez para volver al principio."), [this] { s.stop(); });
        setupGlyph (rec, String::fromUTF8 ("Grabar (F9). Arma antes una pista con su botón redondo."), [this] { record(); });
        setupGlyph (loop, String::fromUTF8 ("Loop (L): repite la zona rosa de la regla"), [this] { s.setLooping (! s.getTransport().looping); });
        setupGlyph (metro, String::fromUTF8 ("Metrónomo"), [this] { s.setMetronome (! s.isMetronomeOn()); });
        setupGlyph (undo, String::fromUTF8 ("Deshacer (Cmd+Z)"), [this] { owner.commandManager.invokeDirectly (editUndo, false); });
        setupGlyph (redo, String::fromUTF8 ("Rehacer (Cmd+Mayús+Z)"), [this] { owner.commandManager.invokeDirectly (editRedo, false); });
        setupGlyph (browserBtn, String::fromUTF8 ("Mostrar u ocultar el navegador (Cmd+1)"), [this] { owner.commandManager.invokeDirectly (viewBrowser, false); });
        setupGlyph (editorBtn, String::fromUTF8 ("Editor de notas y clips (Cmd+2)"), [this] { owner.commandManager.invokeDirectly (viewEditor, false); });
        setupGlyph (mixerBtn, String::fromUTF8 ("Master: todas las pistas y sus medidores (Cmd+3)"), [this] { owner.commandManager.invokeDirectly (viewMixer, false); });
        play.onColour = Brand::menta;
        rec.onColour = Brand::rosa;
        loop.onColour = Brand::rosa;
        metro.onColour = Brand::hielo;
        for (auto* b : { &browserBtn, &editorBtn, &mixerBtn }) b->onColour = Brand::rosa;

        tempo.setSliderStyle (Slider::LinearBar);
        tempo.setRange (40.0, 250.0, 0.1);
        tempo.setTextValueSuffix (" BPM");
        tempo.setTooltip (String::fromUTF8 ("Tempo: arrastra arriba o abajo, o haz doble clic para escribirlo"));
        tempo.setSliderSnapsToMousePosition (false);
        tempo.setVelocityBasedMode (true);
        tempo.setColour (Slider::trackColourId, Brand::limon.withAlpha (0.5f));
        tempo.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
        tempo.onValueChange = [this] { s.setBpm (tempo.getValue()); };
        addAndMakeVisible (tempo);

        for (int n : { 2, 3, 4, 5, 6, 7 }) signature.addItem (String (n) + "/4", n);
        signature.setTooltip (String::fromUTF8 ("Compás"));
        signature.onChange = [this] { vs.edit().getUndoManager().beginNewTransaction(); s.setTimeSigNumerator (signature.getSelectedId()); vs.sendChangeMessage(); };
        addAndMakeVisible (signature);

        snap.addItem ("Libre", 1);
        snap.addItem ("4 compases", 7);
        snap.addItem ("2 compases", 8);
        snap.addItem (String::fromUTF8 ("1 compás"), 2);
        snap.addItem ("1/2", 9);
        snap.addItem ("1/4", 3);
        snap.addItem ("1/8", 4);
        snap.addItem ("1/16", 5);
        snap.addItem ("1/32", 6);
        snap.setTooltip (String::fromUTF8 ("Rejilla: dónde se pegan clips y notas al moverlos (Cmd mientras arrastras: libre)"));
        snap.onChange = [this]
        {
            const double bar = (double) s.getTimeSigNumerator();
            const double v[] { 0.0, 0.0, bar, 1.0, 0.5, 0.25, 0.125, 4.0 * bar, 2.0 * bar, 2.0 };
            s.snapBeats = v[jlimit (1, 9, snap.getSelectedId())];
            vs.sendChangeMessage();
        };
        addAndMakeVisible (snap);

        easy.setClickingTogglesState (true);
        full.setClickingTogglesState (true);
        easy.setRadioGroupId (91);
        full.setRadioGroupId (91);
        easy.setTooltip (String::fromUTF8 ("PRINCIPIANTE: solo lo esencial a la vista"));
        full.setTooltip (String::fromUTF8 ("AVANZADO: envíos, grupos, automatización, congelar, sidechain y fundidos"));
        easy.onClick = [this] { owner.setEasyMode (true); };
        full.onClick = [this] { owner.setEasyMode (false); };
        addAndMakeVisible (easy);
        addAndMakeVisible (full);

        harmBtn.setTooltip (String::fromUTF8 ("Armonizar: elige en la rueda de Camelot la tonalidad de la rola y todo se afina a ella (Cmd+Mayús+K)"));
        harmBtn.onClick = [this] { owner.commandManager.invokeDirectly (editHarmonize, false); };
        addAndMakeVisible (harmBtn);
        exportBtn.setTooltip (String::fromUTF8 ("Exportar la canción o los stems a WAV / FLAC (Cmd+Mayús+E)"));
        exportBtn.onClick = [this] { owner.commandManager.invokeDirectly (fileExport, false); };
        addAndMakeVisible (exportBtn);
        menuBtn.setTooltip (String::fromUTF8 ("Archivo, plantillas, audio y ayuda"));
        menuBtn.onClick = [this] { showMainMenu(); };
        addAndMakeVisible (menuBtn);
        // dark buttons on the Limón band get a light outline (their black shadow would swallow a grey one)
        for (auto* ch : getChildren()) ch->getProperties().set ("onHeader", true);

        refresh();
        startTimerHz (15);
    }

    void record()
    {
        bool anyArmed = false;
        for (auto* t : te::getAudioTracks (vs.edit())) anyArmed = anyArmed || s.isArmed (*t);
        if (! anyArmed && ! s.getTransport().isRecording())
        {
            // nothing armed: arm the selected track (or the first instrument) so the button always does something
            te::AudioTrack* target = nullptr;
            for (auto* t : s.getSelection().getItemsOfType<te::AudioTrack>()) { target = t; break; }
            if (target == nullptr)
                for (auto* t : te::getAudioTracks (vs.edit())) if (kindOf (*t) == TrackKind::instrument) { target = t; break; }
            if (target == nullptr) { if (vs.showHint) vs.showHint (String::fromUTF8 ("Crea o selecciona una pista para grabar.")); return; }
            s.setArmed (*target, true);
            if (vs.showHint) vs.showHint ("Grabando en " + target->getName());
        }
        s.toggleRecord();
    }

    void showMainMenu()
    {
        PopupMenu m;
        auto* cm = &owner.commandManager;
        m.addCommandItem (cm, fileNew);
        m.addCommandItem (cm, fileNewHousito);
        PopupMenu tpl;
        for (auto& n : s.userTemplates())
            tpl.addItem (n, [this, n] { owner.maybeSaveThen ([this, n] { s.newFromUserTemplate (n); }); });
        if (tpl.getNumItems() > 0) m.addSubMenu ("Nuevo desde mis plantillas", tpl);
        m.addCommandItem (cm, fileOpen);
        m.addCommandItem (cm, fileSave);
        m.addCommandItem (cm, fileSaveAs);
        m.addCommandItem (cm, fileSaveTemplate);
        m.addSeparator();
        m.addCommandItem (cm, fileAudioSettings);
        m.addCommandItem (cm, fileScanPlugins);
        {
            PopupMenu lang;
            const bool en = i18n::current() == i18n::Lang::en;
            lang.addItem (String::fromUTF8 ("Español"), true, ! en, [this] { owner.setLanguage (false); });
            lang.addItem ("English", true, en, [this] { owner.setLanguage (true); });
            m.addSubMenu (String::fromUTF8 ("Idioma / Language"), lang);
        }
        {
            const int missing = s.countMissingAudio();
            m.addItem (String::fromUTF8 ("Buscar los audios que faltan") + (missing > 0 ? " (" + String (missing) + ")" : String()),
                       missing > 0, false, [this] { owner.searchMissingAudio(); });
        }
        m.addSeparator();
        m.addCommandItem (cm, helpShortcuts);
        m.addCommandItem (cm, helpAbout);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (menuBtn));
    }

    void refresh()
    {
        auto& tr = s.getTransport();
        play.setToggleState (tr.isPlaying(), dontSendNotification);
        rec.setToggleState (tr.isRecording(), dontSendNotification);
        loop.setToggleState (tr.looping, dontSendNotification);
        metro.setToggleState (s.isMetronomeOn(), dontSendNotification);
        play.setGlyph (Glyph::play);
        undo.setEnabled (vs.edit().getUndoManager().canUndo());
        redo.setEnabled (vs.edit().getUndoManager().canRedo());
        if (! tempo.isMouseButtonDown()) tempo.setValue (s.getBpm(), dontSendNotification);
        signature.setSelectedId (s.getTimeSigNumerator(), dontSendNotification);
        const double g = s.snapBeats;
        const double bar = (double) s.getTimeSigNumerator();
        snap.setSelectedId (g <= 0.0 ? 1 : g >= 4.0 * bar ? 7 : g >= 2.0 * bar ? 8 : g >= bar ? 2 : g >= 2.0 ? 9 : g >= 1.0 ? 3
                            : g >= 0.5 ? 4 : g >= 0.25 ? 5 : 6, dontSendNotification);
        easy.setToggleState (vs.easyMode, dontSendNotification);
        full.setToggleState (! vs.easyMode, dontSendNotification);
        browserBtn.setToggleState (owner.browserVisible, dontSendNotification);
        editorBtn.setToggleState (owner.bottom == Bottom::editor, dontSendNotification);
        mixerBtn.setToggleState (owner.bottom == Bottom::mixer, dontSendNotification);
        metro.setVisible (! vs.easyMode || s.isMetronomeOn() || true);
        repaint (display);
    }

    void timerCallback() override { refresh(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 9);
        const int b = r.getHeight();
        const int w = getWidth();
        // narrow windows drop the extras first: wordmark text, then the view buttons
        const bool roomy = w >= 1360, medium = w >= 1220;
        brand = r.removeFromLeft (roomy ? 176 : b);
        r.removeFromLeft (6);
        for (auto* btn : { &toStart, &play, &stop, &rec, &loop, &metro })
            btn->setBounds (r.removeFromLeft (b + 3).withTrimmedRight (3));
        // undo / redo always stay, next to the transport: they are used all the time
        r.removeFromLeft (8);
        undo.setBounds (r.removeFromLeft (b + 3).withTrimmedRight (3));
        redo.setBounds (r.removeFromLeft (b + 3).withTrimmedRight (3));
        r.removeFromLeft (8);
        display = r.removeFromLeft (roomy ? 152 : 136);
        r.removeFromLeft (8);
        tempo.setBounds (r.removeFromLeft (96).withTrimmedTop (14));
        r.removeFromLeft (4);
        signature.setBounds (r.removeFromLeft (60).withTrimmedTop (14));
        r.removeFromLeft (4);
        snap.setBounds (r.removeFromLeft (86).withTrimmedTop (14));
        r.removeFromLeft (10);

        auto right = r;
        menuBtn.setBounds (right.removeFromRight (b));
        right.removeFromRight (6);
        exportBtn.setBounds (right.removeFromRight (100));
        right.removeFromRight (6);
        harmBtn.setBounds (right.removeFromRight (112));
        right.removeFromRight (10);
        full.setBounds (right.removeFromRight (94));
        easy.setBounds (right.removeFromRight (108));
        right.removeFromRight (10);
        const bool viewsFit = medium || right.getWidth() >= 3 * (b + 3);
        for (auto* btn : { &mixerBtn, &editorBtn, &browserBtn })
        {
            btn->setVisible (viewsFit);
            if (viewsFit) btn->setBounds (right.removeFromRight (b + 3).withTrimmedLeft (3));
        }
        showWordmark = roomy;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (S::bg());
        g.setColour (S::shade);
        g.fillRect (0, getHeight() - 2, getWidth(), 2);

        // brand block: mark, wordmark, project name
        auto r = brand;
        auto mark = r.removeFromLeft (r.getHeight()).toFloat();
        drawAnimalBadge (g, mark, "Racoon", Brand::rosa);
        r.removeFromLeft (8);
        if (! showWordmark) r = {};
        g.setColour (S::shade);
        g.setFont (Fonts::title (22.0f));
        g.drawText (::racoon::i18n::tr ("RACOON"), r.removeFromTop (r.getHeight() / 2 + 4), Justification::bottomLeft);
        g.setFont (Fonts::bodyBold (13.0f));
        g.drawFittedText (::racoon::i18n::tr (s.getProjectName() + (s.hasUnsavedChanges() ? "  *" : "")), r, Justification::topLeft, 1, 0.8f);

        // position screen
        Draw::box (g, display.toFloat(), C::void_, 1.5f, 3.0f);
        auto d = display.withTrimmedRight (3).withTrimmedBottom (3).reduced (10, 2);
        auto& tr = s.getTransport();
        g.setColour (tr.isRecording() ? Brand::rosa : Brand::limon);
        g.setFont (Fonts::mono (21.0f));
        g.drawText (::racoon::i18n::tr (timeToBarsBeats (vs.edit(), tr.getPosition())), d.removeFromLeft (d.getWidth() * 3 / 5), Justification::centredLeft);
        const double secs = tr.getPosition().inSeconds();
        g.setColour (C::boneDim);
        g.setFont (Fonts::mono (13.0f));
        g.drawText (::racoon::i18n::tr (String ((int) secs / 60) + ":" + String ((int) secs % 60).paddedLeft ('0', 2) + "." + String ((int) (secs * 10.0) % 10)),
                    d, Justification::centredRight);

        drawCaption (g, "TEMPO", tempo.getBounds().withY (6).withHeight (14), 11.0f, S::shade.withAlpha (0.7f));
        drawCaption (g, String::fromUTF8 ("COMPÁS"), signature.getBounds().withY (6).withHeight (14), 11.0f, S::shade.withAlpha (0.7f));
        drawCaption (g, "REJILLA", snap.getBounds().withY (6).withHeight (14), 11.0f, S::shade.withAlpha (0.7f));
    }

    MainComponent& owner;
    ViewState& vs;
    Session& s;
    GlyphButton toStart { Glyph::toStart }, play { Glyph::play }, stop { Glyph::stop }, rec { Glyph::record }, loop { Glyph::loop }, metro { Glyph::metronome },
                undo { Glyph::undo }, redo { Glyph::redo }, browserBtn { Glyph::folder }, editorBtn { Glyph::keyboard }, mixerBtn { Glyph::mixer },
                menuBtn { Glyph::gear };
    Slider tempo;
    ComboBox signature, snap;
    TextButton easy { String::fromUTF8 ("PRINCIPIANTE") }, full { "AVANZADO" }, exportBtn { "EXPORTAR" }, harmBtn { "ARMONIZAR" };
    Rectangle<int> brand, display;
    bool showWordmark = true;
};

// ===================================================================== small pieces
void MainComponent::Divider::paint (Graphics& g)
{
    g.fillAll (S::shade);
    g.setColour (C::stoneHi);
    g.fillRect (getWidth() / 2 - 20, 2, 40, 2);
}

void MainComponent::Welcome::paint (Graphics& g)
{
    Draw::panel (g, getLocalBounds().toFloat().reduced (4.0f));
    auto r = getLocalBounds().reduced (18, 8).withTrimmedRight (130);
    g.setColour (S::ink);
    g.setFont (Fonts::title (17.0f));
    g.drawText (::racoon::i18n::tr ("Primeros pasos"), r.removeFromLeft (170), Justification::centredLeft);
    const char* steps[] { "1  Cada pista es un canal. Crea uno con el botón +",
                          "2  Clic en su nombre y abajo, en Canal, ves su instrumento",
                          "3  + Efecto pone efectos encima del instrumento",
                          "4  Doble clic en un clip para escribir notas" };
    const int w = r.getWidth() / 4;
    g.setFont (Fonts::body (13.0f));
    for (auto* st : steps) g.drawFittedText (::racoon::i18n::tr (String::fromUTF8 (st)), r.removeFromLeft (w).reduced (6, 0), Justification::centredLeft, 2, 1.0f);
}

// ===================================================================== main component
MainComponent::MainComponent (Session& s)
    : session (s), vs (s), arrangement (vs), pianoRoll (vs), audioPanel (vs), mixer (vs), channel (vs), browser (vs)
{
    vs.easyMode = prefs().getBoolValue ("easyMode", true);
    vs.openClipEditor = [this] (te::Clip* c) { openClipEditor (c); };
    vs.setPianoScale = [this] (int root, bool minor) { pianoRoll.setScale (root, minor); };
    vs.showHint = [this] (const String& t) { transientHint = t; transientUntil = Time::getMillisecondCounter() + 6000; repaint (statusBounds()); };
    vs.focusArrangement = [this] { arrangement.grabKeyboardFocus(); };
    vs.automateParameter = [this] (te::Track& t, te::AutomatableParameter& p) { arrangement.showAutomationFor (t, p); };
    vs.trackChosen = [this] (te::Track& t)
    {
        channel.showTrack (&t);
        // the Piano tab follows the channel too: its clip under the playhead (or its first one) is loaded,
        // so switching to Piano never shows another channel's notes
        loadClipOf (t);
        if (bottom == Bottom::editor && editorClipTrack() == &t) { resized(); repaint (bottomArea); return; }
        if (bottom != Bottom::mixer) showBottom (Bottom::channel);
    };
    vs.addEditItems = [this] (PopupMenu& m)
    {
        for (auto id : { editCopy, editCut, editPaste, editDuplicate, editDelete }) m.addCommandItem (&commandManager, id);
        // two or more MIDI clips of one channel selected: they can become one (a part A and a part B, then duplicated)
        if (session.getSelection().getItemsOfType<te::MidiClip>().size() >= 2 || (vs.hasRange() && midiClipsInRange().size() >= 2))
            m.addCommandItem (&commandManager, editConsolidate);
        m.addSeparator();
        m.addCommandItem (&commandManager, editMakeSong);
        m.addCommandItem (&commandManager, editRandomAll);
    };
    vs.clipChosen = [this] (te::Clip& c)
    {
        // one click on a clip: with Piano open, that clip's notes; otherwise its channel below. Either way the
        // Piano tab now holds this clip (double click opens it in its instrument)
        if (bottom == Bottom::editor)
        {
            // the keys stay with the arrangement: Supr / Cmd+D right after this click act on the clip, not on its notes
            openClipEditor (&c, false);
            channel.showTrack (c.getTrack());
            return;
        }
        if (auto* t = c.getTrack()) { channel.showTrack (t); if (bottom != Bottom::mixer) showBottom (Bottom::channel); }
        loadClip (&c);
    };

    header = std::make_unique<Header> (*this);
    addAndMakeVisible (*header);
    addAndMakeVisible (arrangement);
    addAndMakeVisible (browser);
    addChildComponent (pianoRoll);
    addChildComponent (audioPanel);
    addChildComponent (mixer);
    addChildComponent (channel);
    addAndMakeVisible (divider);
    for (auto* b : { &editorTab, &channelTab, &mixerTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (55);
        addAndMakeVisible (b);
    }
    editorTab.setTooltip (String::fromUTF8 ("Las notas del clip seleccionado (o su audio): doble clic en un clip para abrirlo aquí"));
    channelTab.setTooltip (String::fromUTF8 ("El canal seleccionado: su instrumento y los efectos encima, en orden"));
    channelTab.onClick = [this] { showBottom (Bottom::channel); };
    mixerTab.setTooltip (String::fromUTF8 ("Master: cada pista con su medidor, los retornos (reverb, delay) y la salida"));
    editorTab.onClick = [this]
    {
        // Piano shows the notes of the channel you are looking at, not whatever was opened last
        if (auto* t = channel.getTrack(); t != nullptr && editorClipTrack() != t) loadClipOf (*t);
        showBottom (Bottom::editor);
    };
    mixerTab.onClick = [this] { showBottom (Bottom::mixer); };
    closeBottom.setTooltip ("Ocultar el panel de abajo");
    closeBottom.onClick = [this] { showBottom (Bottom::none); };
    addAndMakeVisible (closeBottom);
    addAndMakeVisible (instrumentIcon);
    divider.setMouseCursor (MouseCursor::UpDownResizeCursor);
    divider.onDrag = [this] (int y) { bottomHeight = jlimit (160, getHeight() - 260, getHeight() - 26 - y - 6); resized(); };

    welcome.onClose = [this] { prefs().setValue ("welcomeDone", true); welcome.setVisible (false); resized(); };
    addChildComponent (welcome);
    welcome.setVisible (! prefs().getBoolValue ("welcomeDone", false));

    browser.onInsertItem = [this] (const CatalogItem& it)
    {
        session.getEdit().getUndoManager().beginNewTransaction();
        te::Track* target = channel.getTrack();
        if (target == nullptr)
            for (auto* t : session.getSelection().getItemsOfType<te::Track>()) { target = t; break; }
        if (target == nullptr) { vs.showHint (String::fromUTF8 ("Selecciona primero un canal (clic en su nombre) para ponérselo.")); return; }
        if (auto* at = dynamic_cast<te::AudioTrack*> (target)) session.insertPlugin (*at, it);
        else session.insertItem (*target, it);
        channel.showTrack (target);
        showBottom (Bottom::channel);
        vs.showHint ((it.source == CatalogItem::Source::racoon ? Catalog::titleFor (it.name) : it.name) + String::fromUTF8 (" añadido a ") + target->getName());
    };
    browser.onAddItem = [this] (const CatalogItem& it)
    {
        if (Catalog::isMasterProduct (it))
        {
            if (! it.available) { vs.showHint (it.name + String::fromUTF8 (" no está instalado.")); return; }
            session.getEdit().getUndoManager().beginNewTransaction();
            if (session.insertOnMaster (it) != nullptr)
            {
                showBottom (Bottom::mixer);
                vs.showHint (Catalog::titleFor (it.name) + String::fromUTF8 (" va en el Master: lo ves en la pestaña Master, en la salida."));
            }
            return;
        }
        if (Catalog::listedAsInstrument (it)) { session.getEdit().getUndoManager().beginNewTransaction(); addInstrument (it); return; }
        browser.onInsertItem (it);
    };
    browser.onAddFile = [this] (const File& f)
    {
        session.getEdit().getUndoManager().beginNewTransaction();
        if (auto* t = session.addAudioTrack (f.getFileNameWithoutExtension()))
            session.addAudioFile (*t, f, session.getTransport().getPosition());
    };

    commandManager.registerAllCommandsForTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    session.addChangeListener (this);
    session.addListener (this);
    showBottom (Bottom::editor);
    setEasyMode (vs.easyMode);
    startTimerHz (8);
    setSize (1440, 900);
    editReplaced();
}

MainComponent::~MainComponent()
{
    session.removeChangeListener (this);
    session.removeListener (this);
    removeKeyListener (commandManager.getKeyMappings());
}

void MainComponent::changeListenerCallback (ChangeBroadcaster*) { editReplaced(); }

void MainComponent::editAboutToBeReplaced()
{
    pianoRoll.setClip (nullptr);
    audioPanel.setClip (nullptr);
    arrangement.detach();
    mixer.detach();
    channel.detach();
}

void MainComponent::editReplaced()
{
    pianoRoll.setClip (nullptr);
    audioPanel.setClip (nullptr);
    arrangement.editChanged();
    mixer.editChanged();
    if (onTitleChanged) onTitleChanged();
    // the whole song in view (the block that opens Racoon is 16 bars)
    MessageManager::callAsync ([a = Component::SafePointer<ArrangementView> (&arrangement)]
    {
        if (a != nullptr && a->getWidth() > 0) a->zoomToFit();
    });
    // open the first MIDI clip so the editor is never an empty box
    for (auto* t : te::getAudioTracks (session.getEdit()))
        for (auto* c : t->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c)) { pianoRoll.setClip (mc); return; }
    repaint();
}

void MainComponent::setEasyMode (bool easy)
{
    vs.easyMode = easy;
    prefs().setValue ("easyMode", easy);
    vs.sendChangeMessage();
    header->refresh();
    if (! easy) vs.showHint (String::fromUTF8 ("Modo AVANZADO: envíos, grupos, automatización (botón A), congelar y fundidos a la vista."));
}

void MainComponent::showBottom (Bottom b)
{
    bottom = b;
    repaint (bottomArea);
    editorTab.setToggleState (b == Bottom::editor, dontSendNotification);
    channelTab.setToggleState (b == Bottom::channel, dontSendNotification);
    mixerTab.setToggleState (b == Bottom::mixer, dontSendNotification);
    resized();
}

te::Track* MainComponent::editorClipTrack() const
{
    if (auto* mc = pianoRoll.getClip()) return mc->getTrack();
    if (auto* ac = audioPanel.getClip()) return ac->getTrack();
    return nullptr;
}

void MainComponent::loadClip (te::Clip* c)
{
    if (auto* mc = dynamic_cast<te::MidiClip*> (c)) { pianoRoll.setClip (mc); audioPanel.setClip (nullptr); }
    else if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c)) { pianoRoll.setClip (nullptr); audioPanel.setClip (wc); }
    resized();
    repaint (bottomArea);
}

void MainComponent::loadClipOf (te::Track& t)
{
    auto* ct = dynamic_cast<te::ClipTrack*> (&t);
    if (ct == nullptr) return;
    const auto now = session.getTransport().getPosition();
    te::Clip* best = nullptr;
    for (auto* c : ct->getClips())
    {
        if (dynamic_cast<te::MidiClip*> (c) == nullptr && dynamic_cast<te::WaveAudioClip*> (c) == nullptr) continue;
        if (c->getPosition().time.contains (now)) { best = c; break; }
        if (best == nullptr) best = c;
    }
    if (best != nullptr) loadClip (best);
}

void MainComponent::openClipEditor (te::Clip* c, bool focusNotes)
{
    if (auto* mc = dynamic_cast<te::MidiClip*> (c))
    {
        pianoRoll.setClip (mc);
        showBottom (Bottom::editor);
        if (focusNotes) pianoRoll.grabKeyboardFocus();
    }
    else if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
    {
        pianoRoll.setClip (nullptr);
        audioPanel.setClip (wc);
        showBottom (Bottom::editor);
    }
    resized();
}

void MainComponent::addInstrument (const CatalogItem& it)
{
    if (! it.available) { vs.showHint (it.name + String::fromUTF8 (" no está instalado: abre su instalador .dmg y vuelve a abrir Racoon.")); return; }
    if (auto* t = session.addChannelFor (it))
    {
        session.getSelection().selectOnly (t);
        const auto title = it.source == CatalogItem::Source::racoon ? Catalog::titleFor (it.name) : it.name;
        if (Session::instrumentOf (*t) == nullptr)
        {
            // an audio channel (Skunk, Chinchilla): it works on what you record or drop there
            channel.showTrack (t);
            showBottom (Bottom::channel);
            vs.showHint (title + String::fromUTF8 (" listo en su canal: arrastra ahí un audio, o arma el canal y graba."));
            return;
        }
        // a ready-to-write clip at the playhead bar
        const double bar = vs.beatsPerBar();
        const double start = std::floor (vs.timeToBeat (session.getTransport().getPosition()) / bar) * bar;
        if (auto* c = session.createMidiClip (*t, { te::BeatPosition::fromBeats (start), te::BeatPosition::fromBeats (start + bar * 4) }))
            openClipEditor (c);
        vs.showHint (it.source == CatalogItem::Source::racoon
                         ? title + String::fromUTF8 (" listo: escribe notas abajo o abre su ventana con el botón de la pista.")
                         : title + String::fromUTF8 (" listo: escribe notas en el piano roll."));
    }
}

void MainComponent::resized()
{
    if (genrePicker != nullptr) genrePicker->setBounds (getLocalBounds());
    if (languagePicker != nullptr) languagePicker->setBounds (getLocalBounds());
    if (harmonizePanel != nullptr) harmonizePanel->setBounds (getLocalBounds());
    auto r = getLocalBounds();
    header->setBounds (r.removeFromTop (60));
    r.removeFromBottom (26);    // status bar

    if (browserVisible) browser.setBounds (r.removeFromLeft (290));
    browser.setVisible (browserVisible);

    if (welcome.isVisible()) welcome.setBounds (r.removeFromTop (58));

    auto tabsRow = Rectangle<int>();
    if (bottom != Bottom::none)
    {
        const int wanted = bottom == Bottom::mixer ? jmax (bottomHeight, 440) : bottomHeight;
        auto panel = r.removeFromBottom (jmin (wanted, r.getHeight() - 200));
        divider.setBounds (r.removeFromBottom (6));
        bottomArea = panel;
        tabsRow = panel.removeFromTop (30);
        pianoRoll.setBounds (panel);
        audioPanel.setBounds (panel);
        mixer.setBounds (panel);
        channel.setBounds (panel);
    }
    else
    {
        tabsRow = r.removeFromBottom (30);
        bottomArea = tabsRow;
        divider.setBounds ({});
    }
    divider.setVisible (bottom != Bottom::none);

    auto t = tabsRow.reduced (6, 3);
    // top left corner: the icon of the instrument of the channel shown below (always square)
    instrumentIcon.setBounds (t.removeFromLeft (t.getHeight()));
    t.removeFromLeft (6);
    editorTab.setBounds (t.removeFromLeft (100));
    t.removeFromLeft (4);
    channelTab.setBounds (t.removeFromLeft (100));
    t.removeFromLeft (4);
    mixerTab.setBounds (t.removeFromLeft (120));
    closeBottom.setBounds (t.removeFromRight (90));
    closeBottom.setVisible (bottom != Bottom::none);

    const bool showAudio = bottom == Bottom::editor && pianoRoll.getClip() == nullptr && audioPanel.getClip() != nullptr;
    pianoRoll.setVisible (bottom == Bottom::editor && ! showAudio);
    audioPanel.setVisible (showAudio);
    mixer.setVisible (bottom == Bottom::mixer);
    channel.setVisible (bottom == Bottom::channel);

    arrangement.setBounds (r);
    if (header) header->refresh();
}

te::Track* MainComponent::currentBottomTrack()
{
    te::Track* t = nullptr;
    if (bottom == Bottom::channel) t = channel.getTrack();
    else if (bottom == Bottom::editor)
    {
        if (auto* mc = pianoRoll.getClip()) t = mc->getTrack();
        else if (auto* ac = audioPanel.getClip()) t = ac->getTrack();
    }
    return t;
}

Colour MainComponent::currentBottomTint()
{
    auto* t = currentBottomTrack();
    return t != nullptr ? colourForTrack (*t) : S::bg();
}

void MainComponent::refreshInstrumentIcon()
{
    String name;
    Colour fill = S::card;
    auto* t = currentBottomTrack();
    if (t == nullptr && bottom != Bottom::mixer)
        if (auto* sel = session.getSelection().getFirstItemOfType<te::Track>()) t = sel;
    if (bottom == Bottom::mixer) name = "Master";
    else if (auto* at = dynamic_cast<te::AudioTrack*> (t))
    {
        if (auto* inst = Session::instrumentOf (*at)) name = inst->getName();
        else
        {
            // an audio channel with a Racoon effect on it (Skunk, Chinchilla) wears that animal
            for (auto* p : Session::insertsOf (*at)) if (Catalog::animalFor (p->getName()).isNotEmpty()) { name = p->getName(); break; }
            if (name.isEmpty()) name = "Audio";
        }
    }
    else if (t != nullptr) name = "Grupo";
    if (t != nullptr) fill = colourForTrack (*t).brighter (0.2f);
    if (name != instrumentIcon.name || fill != instrumentIcon.fill)
    {
        instrumentIcon.name = name;
        instrumentIcon.fill = fill;
        instrumentIcon.setTooltip (name == "Master" ? String ("Master") : (t != nullptr ? trackTitle (*t) : String()));
        instrumentIcon.repaint();
    }
}

void MainComponent::InstrumentIcon::paint (Graphics& g)
{
    if (name.isEmpty()) return;
    // a square in the middle, whatever the bounds: the picture keeps its shape
    const float side = (float) jmin (getWidth(), getHeight());
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (side, side);
    drawAnimalBadge (g, r, name == "Master" ? String ("Racoon") : name, fill);
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (S::shade);
    bottomTint = currentBottomTint();
    if (! bottomArea.isEmpty())
    {
        // the channel's colour as a faint wash and a thin band on top: the panel says whose it is
        g.setColour (S::shade.interpolatedWith (bottomTint, 0.10f));
        g.fillRect (bottomArea);
        g.setColour (bottomTint);
        g.fillRect (bottomArea.withHeight (3));
    }
    auto st = statusBounds();
    g.setColour (Colour (0xff1a1618));
    g.fillRect (st);
    auto r = st.reduced (10, 0);
    const bool transient = Time::getMillisecondCounter() < transientUntil;
    g.setColour (transient ? Brand::limon : C::boneDim);
    g.setFont (Fonts::body (13.0f));
    auto right = r.removeFromRight (360);
    g.drawFittedText (::racoon::i18n::tr (transient ? transientHint : hint), r, Justification::centredLeft, 1, 0.85f);

    auto& dm = session.engine.getDeviceManager();
    String info;
    info << "CPU " << String (roundToInt (dm.getCpuUsage() * 100.0)) << "%   ";
    if (dm.getSampleRate() > 0) info << String (dm.getSampleRate() / 1000.0, 1) << " kHz   ";
    info << (session.isUntitled() ? String::fromUTF8 ("Sin guardar (autoguardado activo)") : (session.hasUnsavedChanges() ? "Cambios sin guardar" : "Guardado"));
    g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr (info), right, Justification::centredRight);
}

void MainComponent::timerCallback()
{
    // the status bar explains whatever is under the mouse
    String text;
    if (auto* c = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
        for (auto* p = c; p != nullptr && text.isEmpty(); p = p->getParentComponent())
            if (auto* tc = dynamic_cast<TooltipClient*> (p))
                text = tc->getTooltip();
    if (text != hint) { hint = text; repaint (statusBounds()); }
    if (currentBottomTint() != bottomTint) repaint (bottomArea);
    refreshInstrumentIcon();
    static int n = 0;
    if (++n % 8 == 0) { repaint (statusBounds()); if (onTitleChanged) onTitleChanged(); session.pushTrackNames(); }
    // a plugin pressed "A la maqueta": its sequence lands on its channel
    if (n % 2 == 0)
    {
        const auto got = session.pollMaqueta();
        if (got.isNotEmpty())
        {
            vs.showHint (got);
            arrangement.zoomToFit();          // the new part is at the end of the channel: the whole song in view
            if (auto* c = session.getSelection().getFirstItemOfType<te::Clip>())
                if (auto* t = c->getTrack()) { if (vs.trackChosen) vs.trackChosen (*t); }
        }
    }
}

String MainComponent::windowTitle() const
{
    return "Racoon  ·  "_u + session.getProjectName() + (session.hasUnsavedChanges() ? " *" : "");
}

// ===================================================================== file operations
void MainComponent::openProject (const File& f)
{
    maybeSaveThen ([this, f]
    {
        if (! session.openProject (f))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "No se pudo abrir", f.getFullPathName());
            return;
        }
        prefs().setValue ("lastProject", f.getFullPathName());
        checkSessionHealth();
        if (auto rec = session.findRecoveryFile(); rec.existsAsFile())
        {
            auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon).withTitle ("Recuperar")
                            .withMessage (String::fromUTF8 ("Hay una copia autoguardada más reciente de este proyecto. ¿Quieres recuperarla?"))
                            .withButton ("RECUPERAR").withButton ("NO");
            AlertWindow::showAsync (opts, [this, rec, f] (int r)
            {
                if (r != 1) return;
                if (session.openProject (rec))
                {
                    session.saveAs (f);  // the recovered state becomes the project again
                }
            });
        }
    });
}

namespace
{
// BUSCAR: looks for the missing audio by name in Racoon's folders and the usual places, with a bar and a Cancel
struct AudioSearch : public ThreadWithProgressWindow
{
    AudioSearch (std::vector<Session::Wanted> w, Array<File> r, std::function<void (std::map<String, File>, int, bool)> d)
        : ThreadWithProgressWindow (::racoon::i18n::tr (String::fromUTF8 ("Buscando los audios que faltan...")), true, true),
          wanted (std::move (w)), roots (std::move (r)), done (std::move (d)) {}
    void run() override
    {
        setProgress (-1.0);
        for (int i = 0; i < roots.size() && ! threadShouldExit(); ++i)
        {
            setStatusMessage (::racoon::i18n::tr (String::fromUTF8 ("Buscando en ")) + roots[i].getFullPathName());
            int n = 0;
            for (auto& [k, f] : Session::searchFiles (wanted, { roots[i] }, [this] { return threadShouldExit(); }, &n))
                if (found.count (k) == 0) found[k] = f;
            looked += n;
            if (found.size() == wanted.size()) break;
        }
    }
    void threadComplete (bool cancelled) override
    {
        if (done) done (found, looked, cancelled);
        delete this;
    }
    std::vector<Session::Wanted> wanted;
    Array<File> roots;
    std::function<void (std::map<String, File>, int, bool)> done;
    std::map<String, File> found;
    int looked = 0;
};
}

void MainComponent::searchMissingAudio()
{
    const auto wanted = session.missingAudio();
    if (wanted.empty()) { vs.showHint (String::fromUTF8 ("No falta ningún audio.")); return; }
    auto* job = new AudioSearch (wanted, session.audioSearchRoots(),
                                 [sp = SafePointer<MainComponent> (this)] (std::map<String, File> found, int looked, bool cancelled)
    {
        if (sp == nullptr) return;
        const int fixed = sp->session.relinkFound (found);
        const int still = sp->session.countMissingAudio();
        String msg = String::fromUTF8 ("Encontré ") + String (fixed) + (fixed == 1 ? " audio" : " audios")
                     + String::fromUTF8 (" (revisé ") + String (looked) + " archivos)"
                     + (still > 0 ? String::fromUTF8 ("; faltan ") + String (still) + "." : String::fromUTF8 (". Ya no falta nada."));
        if (cancelled) msg = String::fromUTF8 ("Búsqueda detenida. ") + msg;
        sp->vs.showHint (msg);
        if (still == 0) return;
        // what was not found anywhere: the person can point to the folder where it is
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon).withTitle (String::fromUTF8 ("Audios que faltan"))
                        .withMessage (msg + String::fromUTF8 ("\n\nSi sabes en qué carpeta están, elígela y los busco ahí."))
                        .withButton ("ELEGIR CARPETA...").withButton (String::fromUTF8 ("SEGUIR ASÍ"));
        AlertWindow::showAsync (opts, [sp] (int r) { if (sp != nullptr && r == 1) sp->chooseFolderForMissingAudio(); });
    });
    job->launchThread();
}

void MainComponent::chooseFolderForMissingAudio()
{
    auto chooser = std::make_shared<FileChooser> (String::fromUTF8 ("¿Dónde están los audios?"), session.getFile().getParentDirectory());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this, chooser] (const FileChooser& fc)
    {
        if (fc.getResult() == File()) return;
        const int fixed = session.relinkMissingAudio (fc.getResult());
        const int still = session.countMissingAudio();
        vs.showHint (String::fromUTF8 ("Encontré ") + String (fixed) + (fixed == 1 ? " audio" : " audios")
                     + (still > 0 ? String::fromUTF8 ("; faltan ") + String (still) + "." : String::fromUTF8 (". Ya no falta nada.")));
    });
}

void MainComponent::checkSessionHealth()
{
    const auto issues = session.healthReport();
    if (issues.isEmpty()) return;
    String text;
    for (int i = 0; i < jmin (8, issues.size()); ++i) text << "- " << issues[i] << "\n";
    if (issues.size() > 8) text << String::fromUTF8 ("... y ") << (issues.size() - 8) << String::fromUTF8 (" más.\n");
    const bool canSearch = session.countMissingAudio() > 0;
    if (canSearch) text << String::fromUTF8 ("\nBUSCAR los busca por su nombre en tus carpetas y en las de Racoon, y los vuelve a poner en su lugar.");
    auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon).withTitle (String::fromUTF8 ("Revisión del proyecto"))
                    .withMessage (text.trimEnd());
    opts = canSearch ? opts.withButton ("BUSCAR").withButton ("ELEGIR CARPETA...").withButton ("SEGUIR ASÍ"_u) : opts.withButton ("ENTENDIDO");
    AlertWindow::showAsync (opts, [this, canSearch] (int r)
    {
        if (! canSearch) return;
        if (r == 1) searchMissingAudio();
        else if (r == 2) chooseFolderForMissingAudio();
    });
}

void MainComponent::saveAs (std::function<void (bool)> done)
{
    auto start = session.isUntitled() ? Session::projectsFolder().getChildFile ("Mi tema.racoon") : session.getFile();
    auto chooser = std::make_shared<FileChooser> ("Guardar proyecto", start, "*.racoon");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser, done] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        bool ok = false;
        if (f != File())
        {
            ok = session.saveAs (f);
            if (ok) prefs().setValue ("lastProject", session.getFile().getFullPathName());
            else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "No se pudo guardar", f.getFullPathName());
        }
        if (onTitleChanged) onTitleChanged();
        if (done) done (ok);
    });
}

void MainComponent::maybeSaveThen (std::function<void()> next)
{
    if (! session.hasUnsavedChanges()) { next(); return; }
    auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                    .withTitle (String::fromUTF8 ("¿Guardar los cambios?"))
                    .withMessage (String::fromUTF8 ("«") + session.getProjectName() + String::fromUTF8 ("» tiene cambios sin guardar."))
                    .withButton ("GUARDAR").withButton ("NO GUARDAR").withButton ("CANCELAR");
    AlertWindow::showAsync (opts, [this, next] (int r)
    {
        if (r == 1)
        {
            if (session.isUntitled()) saveAs ([next] (bool ok) { if (ok) next(); });
            else if (session.save()) next();
        }
        else if (r == 2)
        {
            if (session.isUntitled()) session.getFile().deleteFile();
            next();
        }
    });
}

// ===================================================================== editing helpers
void MainComponent::deleteSelection()
{
    if (vs.hasRange())
    {
        // only the selected piece of time goes; what is around it stays where it is
        session.getEdit().getUndoManager().beginNewTransaction();
        pianoRoll.setClip (nullptr);
        audioPanel.setClip (nullptr);
        session.deleteRange (vs.rangeAudioTracks(), vs.rangeStart, vs.rangeEnd);
        vs.clearRange();
        return;
    }
    auto clips = session.getSelection().getItemsOfType<te::Clip>();
    if (clips.isEmpty()) return;
    session.getEdit().getUndoManager().beginNewTransaction();
    for (auto* c : clips)
    {
        if (pianoRoll.getClip() == c) pianoRoll.setClip (nullptr);
        if (audioPanel.getClip() == c) audioPanel.setClip (nullptr);
        session.deleteClip (*c);
    }
}

void MainComponent::setLanguage (bool english)
{
    i18n::set (english ? i18n::Lang::en : i18n::Lang::es);
    commandManager.commandStatusChanged();
    menuItemsChanged();
    if (auto* top = getTopLevelComponent()) top->repaint();
    repaint();
}

void MainComponent::showHarmonize()
{
    if (harmonizePanel != nullptr) return;
    // the key the song is in: Racoon knows it from the genre (or a previous ARMONIZAR); otherwise it is read from the notes
    int root = 9;
    bool major = false;
    bool known = session.songKey (root, major);
    if (! known)
    {
        std::vector<aleatorio::Nota> all;
        for (auto* t : te::getAudioTracks (session.getEdit()))
        {
            auto* inst = Session::instrumentOf (*t);
            if (inst != nullptr && (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole"))) continue;
            for (auto* c : t->getClips())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                    for (auto* n : mc->getSequence().getNotes())
                        all.push_back ({ n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
        }
        bool minor = true;
        known = aleatorio::tonalidadDe (all, root, minor);
        major = ! minor;
        if (! known) { root = 9; major = false; }
    }
    session.beginHarmonize (root, major);
    harmonizePanel = std::make_unique<HarmonizePanel> (root, major, known);
    harmonizePanel->onTry = [this] (int r, bool m) { session.previewHarmonize (r, m); };
    harmonizePanel->onKeep = [this] (int r, bool m)
    {
        session.commitHarmonize (r, m);
        vs.setPianoScale (r, ! m);
        vs.showHint (String::fromUTF8 ("La rola quedó en ") + keyName (r, m) + " (" + camelot (r, m) + String::fromUTF8 ("): instrumentos, loops y plugins afinados. Cmd+Z lo regresa."));
        closeHarmonize();
    };
    harmonizePanel->onCancel = [this]
    {
        session.cancelHarmonize();
        vs.showHint (String::fromUTF8 ("La rola se quedó en su tonalidad."));
        closeHarmonize();
    };
    addAndMakeVisible (*harmonizePanel);
    harmonizePanel->setBounds (getLocalBounds());
    harmonizePanel->toFront (true);
    harmonizePanel->grabKeyboardFocus();
}

void MainComponent::closeHarmonize()
{
    MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)]
    {
        if (sp == nullptr) return;
        sp->harmonizePanel.reset();
        sp->arrangement.repaint();
        sp->pianoRoll.repaint();
        sp->grabKeyboardFocus();
    });
}

void MainComponent::showGenrePicker (bool atStart)
{
    if (atStart && ! i18n::chosen())
    {
        languagePicker = std::make_unique<LanguagePicker>();
        languagePicker->onPick = [this] (bool english)
        {
            setLanguage (english);
            MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)]
            {
                if (sp == nullptr) return;
                sp->languagePicker.reset();
                sp->showGenrePicker (true);
            });
        };
        addAndMakeVisible (*languagePicker);
        languagePicker->setBounds (getLocalBounds());
        languagePicker->toFront (true);
        languagePicker->grabKeyboardFocus();
        return;
    }
    genrePicker = std::make_unique<GenrePicker>();
    auto closePicker = [this]
    {
        MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)] { if (sp != nullptr) { sp->genrePicker.reset(); sp->grabKeyboardFocus(); } });
    };
    genrePicker->onPick = [this, closePicker] (const String& id)
    {
        auto* g = findGenre (id);
        if (g == nullptr) return;
        genrePicker->setBusy (String::fromUTF8 (g->name));
        // building loads every plugin: the message shows first, the block is made right after
        Timer::callAfterDelay (60, [sp = SafePointer<MainComponent> (this), g, closePicker]
        {
            if (sp == nullptr) return;
            sp->session.newProject (true, false, g);
            for (auto* t : te::getAudioTracks (sp->session.getEdit()))       // the drums' notes open below, as always
                for (auto* c : t->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c)) { sp->pianoRoll.setClip (mc); sp->showBottom (Bottom::editor); goto opened; }
            opened:
            sp->arrangement.zoomToFit();
            sp->vs.showHint (String::fromUTF8 ("Tu bloque de ") + String::fromUTF8 (g->name) + " a " + String (roundToInt (g->bpm))
                             + " BPM en " + keyName (g->key, g->major) + " (" + camelot (g->key, g->major) + ")"
                             + String::fromUTF8 (": 16 compases, todo afinado en esa tonalidad. Dale Play (espacio)."));
            closePicker();
        });
    };
    genrePicker->onEmpty = [this, closePicker] { session.newProject (false); closePicker(); };
    genrePicker->onOpenLast = [this, closePicker]
    {
        const File last (prefs().getValue ("lastProject"));
        closePicker();
        if (last.existsAsFile()) openProject (last);
    };
    // at start, closing without choosing still gives music: the Afro House block
    genrePicker->onClose = [this, closePicker, atStart]
    {
        if (atStart) session.newProject (true);
        closePicker();
    };
    addAndMakeVisible (*genrePicker);
    genrePicker->setBounds (getLocalBounds());
    genrePicker->toFront (true);
    genrePicker->grabKeyboardFocus();
}

void MainComponent::copySelection (bool cut)
{
    const auto bpb = vs.beatsPerBar();
    if (vs.hasRange())
    {
        session.copyRange (vs.rangeAudioTracks(), vs.rangeStart, vs.rangeEnd);
        vs.showHint ((cut ? "Cortado: "_u : "Copiado: "_u) + String (vs.rangeEnd - vs.rangeStart, 2) + " tiempos de "
                     + String (vs.rangeTracks.size()) + String::fromUTF8 (" canal(es). Haz clic donde lo quieres y Cmd+V."));
        if (cut) deleteSelection();
        return;
    }
    auto clips = session.getSelection().getItemsOfType<te::Clip>();
    if (clips.isEmpty()) { vs.showHint (String::fromUTF8 ("Selecciona clips, o arrastra sobre la maqueta para elegir un pedazo, y luego Cmd+C.")); return; }
    session.copyClips (clips);
    vs.showHint ((cut ? "Cortado: "_u : "Copiado: "_u) + String (clips.size()) + String::fromUTF8 (" clip(s). Haz clic donde los quieres y Cmd+V."));
    if (cut) deleteSelection();
    ignoreUnused (bpb);
}

void MainComponent::pasteClipboard()
{
    if (! session.hasClipboard()) { vs.showHint (String::fromUTF8 ("Primero copia algo (Cmd+C).")); return; }
    // on the selected channel (or the channel of the selected clip / range), at the cursor
    te::AudioTrack* target = nullptr;
    if (auto tracks = session.getSelection().getItemsOfType<te::AudioTrack>(); ! tracks.isEmpty()) target = tracks.getFirst();
    if (target == nullptr) if (auto clips = session.getSelection().getItemsOfType<te::Clip>(); ! clips.isEmpty()) target = dynamic_cast<te::AudioTrack*> (clips.getFirst()->getTrack());
    if (target == nullptr && vs.hasRange()) target = vs.rangeAudioTracks().getFirst();
    if (target == nullptr) { vs.showHint (String::fromUTF8 ("Haz clic en el canal y en el compás donde quieres pegar.")); return; }
    const double at = vs.timeToBeat (session.getTransport().getPosition());
    session.getEdit().getUndoManager().beginNewTransaction();
    auto pasted = session.pasteAt (*target, at);
    te::SelectableList list;
    for (auto* c : pasted) list.add (c);
    session.getSelection().select (list);
    vs.clearRange();
    vs.showHint ("Pegado en "_u + ViewState::positionText (at, vs.beatsPerBar()) + " de «"_u + target->getName() + "»."_u);
}

void MainComponent::duplicateSelection()
{
    if (vs.hasRange())
    {
        // the piece is repeated right after itself, and the selection jumps to the copy (press again to keep going)
        auto tracks = vs.rangeAudioTracks();
        session.copyRange (tracks, vs.rangeStart, vs.rangeEnd);
        session.getEdit().getUndoManager().beginNewTransaction();
        auto all = te::getAudioTracks (session.getEdit());
        te::AudioTrack* first = nullptr;
        for (auto* t : tracks) if (first == nullptr || all.indexOf (t) < all.indexOf (first)) first = t;
        if (first != nullptr) session.pasteAt (*first, vs.rangeEnd);
        const double len = vs.rangeEnd - vs.rangeStart;
        vs.rangeStart = vs.rangeEnd;
        vs.rangeEnd += len;
        vs.sendChangeMessage();
        return;
    }
    auto clips = session.getSelection().getItemsOfType<te::Clip>();
    session.getEdit().getUndoManager().beginNewTransaction();
    if (! clips.isEmpty())
    {
        te::SelectableList copies;
        for (auto* c : clips) if (auto* n = session.duplicateClip (*c)) copies.add (n);
        session.getSelection().select (copies);
        return;
    }
    if (auto tracks = session.getSelection().getItemsOfType<te::Track>(); ! tracks.isEmpty())
        session.duplicateTrack (*tracks.getFirst());
}

void MainComponent::reverseSelection()
{
    // with the Piano open and focused its notes are reversed there; here, the selected clips: audio plays backwards,
    // a MIDI clip's notes are mirrored in time
    auto clips = session.getSelection().getItemsOfType<te::Clip>();
    if (clips.isEmpty())
    {
        if (bottom == Bottom::editor && pianoRoll.getClip() != nullptr) { pianoRoll.reverseNotes(); return; }
        vs.showHint (String::fromUTF8 ("Selecciona un clip y pulsa R para voltearlo."));
        return;
    }
    auto& um = session.getEdit().getUndoManager();
    um.beginNewTransaction();
    int audio = 0, midi = 0;
    for (auto* c : clips)
    {
        if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) { w->setIsReversed (! w->getIsReversed()); ++audio; }
        else if (auto* mc = dynamic_cast<te::MidiClip*> (c)) { notes::reverse (*mc, {}, &um); ++midi; }
    }
    vs.showHint ("Reverso: "_u + (audio > 0 ? String (audio) + " audio(s) al revés"_u : String()) + (audio > 0 && midi > 0 ? ", " : "")
                 + (midi > 0 ? String (midi) + " clip(s) MIDI con las notas al revés"_u : String()) + ". R otra vez lo regresa."_u);
    arrangement.repaint();
}

void MainComponent::transposeSelectedClips (int st)
{
    auto clips = session.getSelection().getItemsOfType<te::MidiClip>();
    if (clips.isEmpty() && pianoRoll.getClip() != nullptr && bottom == Bottom::editor) { pianoRoll.moveOctave (st / 12); return; }
    if (clips.isEmpty()) { vs.showHint (String::fromUTF8 ("Selecciona clips MIDI para subirlos o bajarlos.")); return; }
    auto& um = session.getEdit().getUndoManager();
    um.beginNewTransaction();
    for (auto* c : clips) notes::transpose (*c, {}, st, &um);
    vs.showHint (String (clips.size()) + (st > 0 ? " clip(s) subieron " : " clip(s) bajaron ") + String (std::abs (st) / 12) + " octava(s).");
}

void MainComponent::makeSong()
{
    // the loop: the dragged range; or else the selected clips (their channels, from the first to the last bar)
    Array<te::AudioTrack*> tracks;
    double a = 0.0, b = 0.0;
    if (vs.hasRange())
    {
        tracks = vs.rangeAudioTracks();
        a = vs.rangeStart; b = vs.rangeEnd;
    }
    else
    {
        a = 1.0e12; b = -1.0e12;
        for (auto* c : session.getSelection().getItemsOfType<te::Clip>())
            if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
            {
                tracks.addIfNotAlreadyThere (t);
                a = jmin (a, vs.timeToBeat (c->getPosition().getStart()));
                b = jmax (b, vs.timeToBeat (c->getPosition().getEnd()));
            }
    }
    String why;
    auto done = [this] (const arranger::Result& r)
    {
        if (! r.ok) { vs.showHint (r.message); return; }
        vs.clearRange();
        session.getSelection().deselectAll();
        arrangement.zoomToFit();
        vs.showHint (r.message + String::fromUTF8 (" Cmd+Z la deshace."));
    };
    if (! SongMakerPanel::show (session, tracks, a, b, done, why)) vs.showHint (why);
}

void MainComponent::randomAll()
{
    // every MIDI clip with notes (in the selected range, or in the whole song) gets its variation, in one undo step;
    // the held ones (FIJAR) stay as they are
    Array<te::MidiClip*> clips;
    for (auto* t : te::getAudioTracks (session.getEdit()))
    {
        if (vs.hasRange() && ! vs.rangeTracks.contains (t)) continue;
        for (auto* c : t->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc->getSequence().getNumNotes() > 0)
            {
                if (vs.hasRange())
                {
                    const double a = vs.timeToBeat (mc->getPosition().getStart()), b = vs.timeToBeat (mc->getPosition().getEnd());
                    if (b <= vs.rangeStart || a >= vs.rangeEnd) continue;
                }
                clips.add (mc);
            }
    }
    if (clips.isEmpty()) { vs.showHint (String::fromUTF8 ("No hay clips con notas para ALEATORIA.")); return; }
    auto* before = pianoRoll.getClip();
    session.getEdit().getUndoManager().beginNewTransaction();
    int varied = 0, held = 0;
    const auto seed = (int64) Time::getHighResolutionTicks();
    for (int i = 0; i < clips.size(); ++i)
    {
        if (ui::PianoRoll::isHeld (*clips[i])) { ++held; continue; }
        pianoRoll.setClip (clips[i]);
        if (pianoRoll.generateRandom (std::nullopt, seed + i * 7919, false, false)) ++varied;
    }
    pianoRoll.setClip (before);
    vs.showHint (String::fromUTF8 ("ALEATORIA en ") + String (varied) + (varied == 1 ? String (" clip") : String (" clips"))
                 + (held > 0 ? String::fromUTF8 (", ") + String (held) + String::fromUTF8 (held == 1 ? " fijado se quedó igual" : " fijados se quedaron igual") : String())
                 + String::fromUTF8 (". Un Cmd+Z los regresa a todos."));
}

Array<te::MidiClip*> MainComponent::midiClipsInRange()
{
    Array<te::MidiClip*> out;
    if (! vs.hasRange()) return out;
    for (auto* t : vs.rangeTracks)
        if (auto* at = dynamic_cast<te::AudioTrack*> (t))
            for (auto* c : at->getClips())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                {
                    const double a = vs.timeToBeat (mc->getPosition().getStart()), b = vs.timeToBeat (mc->getPosition().getEnd());
                    if (b > vs.rangeStart + 1.0e-6 && a < vs.rangeEnd - 1.0e-6) out.add (mc);
                }
    return out;
}

void MainComponent::consolidateSelection()
{
    // Cmd+J: the selected MIDI clips of one channel become a single clip with all their notes (with a piece of the
    // arrangement chosen instead, the clips it touches)
    auto clips = session.getSelection().getItemsOfType<te::MidiClip>();
    if (clips.size() < 2 && vs.hasRange()) clips = midiClipsInRange();
    std::map<te::AudioTrack*, Array<te::MidiClip*>> byTrack;
    for (auto* c : clips) if (auto* at = dynamic_cast<te::AudioTrack*> (c->getTrack())) byTrack[at].add (c);
    int made = 0;
    auto& um = session.getEdit().getUndoManager();
    um.beginNewTransaction();
    te::SelectableList result;
    for (auto& [at, list] : byTrack)
    {
        if (list.size() < 2) continue;
        double s = 1.0e12, e = -1.0e12;
        for (auto* c : list) { s = jmin (s, vs.timeToBeat (c->getPosition().getStart())); e = jmax (e, vs.timeToBeat (c->getPosition().getEnd())); }
        struct N { double beat, len; int pitch, vel; bool mute; };
        std::vector<N> all;
        for (auto* c : list)
        {
            const auto play = notes::playingRange (*c);
            for (auto* n : c->getSequence().getNotes())
            {
                const double cb = n->getStartBeat().inBeats();
                if (cb < play.getStart() - 1.0e-6 || cb >= play.getEnd()) continue;
                const double eb = vs.timeToBeat (c->getTimeOfContentBeat (n->getStartBeat()));
                all.push_back ({ eb - s, jmin (n->getLengthBeats().inBeats(), play.getEnd() - cb), n->getNoteNumber(), n->getVelocity(), n->isMute() });
            }
        }
        const auto name = list.getFirst()->getName();
        for (auto* c : list)
        {
            if (pianoRoll.getClip() == c) pianoRoll.setClip (nullptr);
            session.deleteClip (*c);
        }
        if (auto* nc = session.createMidiClip (*at, { te::BeatPosition::fromBeats (s), te::BeatPosition::fromBeats (e) }, name))
        {
            for (auto& n : all)
                if (auto* added = nc->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (n.beat), te::BeatDuration::fromBeats (n.len), n.vel, 0, &um))
                    if (n.mute) added->setMute (true, &um);
            result.add (nc);
            ++made;
        }
    }
    if (made == 0) { vs.showHint (String::fromUTF8 ("Selecciona dos o más clips MIDI del mismo canal (Mayús+clic) y pulsa Cmd+J, o clic derecho > Unir los clips en uno.")); return; }
    session.getSelection().select (result);
    vs.showHint (String::fromUTF8 ("Unidos en ") + String (made) + String::fromUTF8 (made == 1 ? " clip con todas sus notas. Ya lo puedes duplicar, copiar y pegar como uno solo." : " clips con todas sus notas."));
}

void MainComponent::splitSelection()
{
    const auto t = session.getTransport().getPosition();
    session.getEdit().getUndoManager().beginNewTransaction();
    auto clips = session.getSelection().getItemsOfType<te::Clip>();
    if (clips.isEmpty())
        for (auto* tr : session.getSelection().getItemsOfType<te::AudioTrack>())
            for (auto* c : tr->getClips()) if (c->getEditTimeRange().contains (t)) clips.add (c);
    int n = 0;
    for (auto* c : clips) if (session.splitClip (*c, t) != nullptr) ++n;
    if (n == 0) vs.showHint (String::fromUTF8 ("Pon el cursor sobre un clip seleccionado para dividirlo."));
}

// ===================================================================== commands
void MainComponent::getAllCommands (Array<CommandID>& c)
{
    c.addArray ({ fileNew, fileNewHousito, fileOpen, fileSave, fileSaveAs, fileExport, fileSaveTemplate, fileAudioSettings, fileScanPlugins,
                  editUndo, editRedo, editDelete, editDuplicate, editSplit, editQuantise, editCopy, editCut, editPaste,
                  trackAudio, trackInstrument, trackBadger, trackHamster,
                  editReverse, editMuteClips, editLoopSelection, editConsolidate, editOctaveUp, editOctaveDown,
                  trackGroup, trackMidiClip, viewDetail,
                  playToggle, playRecord, playLoop, playToStart, playMetronome,
                  viewBrowser, viewEditor, viewMixer, viewZoomIn, viewZoomOut, viewZoomFit, viewEasy, viewFollow,
                  helpShortcuts, helpAbout, editMakeSong, editRandomAll, editHarmonize });
}

void MainComponent::getCommandInfo (CommandID id, ApplicationCommandInfo& i)
{
    const auto cmd = ModifierKeys::commandModifier, shift = ModifierKeys::shiftModifier;
    auto set = [&i] (const char* name, const char* cat) { i.setInfo (i18n::tr (name), i18n::tr (name), i18n::tr (cat), 0); };
    switch (id)
    {
        case fileNew:           set ("Nuevo: elegir género", "Archivo"); i.addDefaultKeypress ('n', cmd); break;
        case fileNewHousito:    set ("Nuevo proyecto vacío", "Archivo"); i.addDefaultKeypress ('n', cmd | shift); break;
        case fileOpen:          set ("Abrir...", "Archivo"); i.addDefaultKeypress ('o', cmd); break;
        case fileSave:          set ("Guardar", "Archivo"); i.addDefaultKeypress ('s', cmd); break;
        case fileSaveAs:        set ("Guardar como...", "Archivo"); i.addDefaultKeypress ('s', cmd | shift); break;
        case fileExport:        set ("Exportar audio / stems...", "Archivo"); i.addDefaultKeypress ('e', cmd | shift); break;
        case fileSaveTemplate:  set ("Guardar como plantilla...", "Archivo"); break;
        case fileAudioSettings: set ("Audio y MIDI...", "Archivo"); i.addDefaultKeypress (',', cmd); break;
        case fileScanPlugins:   set ("Buscar plugins...", "Archivo"); break;
        case editUndo:          set ("Deshacer", "Editar"); i.addDefaultKeypress ('z', cmd); i.setActive (session.getEdit().getUndoManager().canUndo()); break;
        case editRedo:          set ("Rehacer", "Editar"); i.addDefaultKeypress ('z', cmd | shift); i.setActive (session.getEdit().getUndoManager().canRedo()); break;
        case editDelete:        set ("Borrar", "Editar"); i.addDefaultKeypress (KeyPress::deleteKey, 0); i.addDefaultKeypress (KeyPress::backspaceKey, 0); break;
        case editDuplicate:     set ("Duplicar", "Editar"); i.addDefaultKeypress ('d', cmd); break;
        case editCopy:          set ("Copiar", "Editar"); i.addDefaultKeypress ('c', cmd); break;
        case editCut:           set ("Cortar", "Editar"); i.addDefaultKeypress ('x', cmd); break;
        case editPaste:         set ("Pegar en el cursor", "Editar"); i.addDefaultKeypress ('v', cmd); break;
        case editSplit:         set ("Dividir en el cursor", "Editar"); i.addDefaultKeypress ('e', cmd); break;
        case editQuantise:      set ("Cuantizar clip MIDI", "Editar"); i.addDefaultKeypress ('u', cmd); break;
        // the shortcuts producers already have in their fingers
        case editReverse:       set ("Reverso (al revés)", "Editar"); i.addDefaultKeypress ('r', 0); i.addDefaultKeypress ('r', cmd); break;
        case editMuteClips:     set ("Activar / desactivar clip", "Editar"); i.addDefaultKeypress ('0', 0); break;
        case editLoopSelection: set ("Loop a la selección", "Editar"); i.addDefaultKeypress ('l', cmd); break;
        case editConsolidate:   set ("Unir los clips en uno", "Editar"); i.addDefaultKeypress ('j', cmd); break;
        case editOctaveUp:      set ("Subir una octava los clips MIDI", "Editar"); break;
        case editOctaveDown:    set ("Bajar una octava los clips MIDI", "Editar"); break;
        case editMakeSong:      set ("Crear la rola completa con la selección...", "Editar"); i.addDefaultKeypress ('r', cmd | shift); break;
        case editRandomAll:     set ("ALEATORIA en todos los canales (menos los fijados)", "Editar"); i.addDefaultKeypress ('a', cmd | shift); break;
        case editHarmonize:     set ("Armonizar: cambiar la tonalidad de la rola...", "Editar"); i.addDefaultKeypress ('k', cmd | shift); break;
        case trackGroup:        set ("Agrupar las pistas seleccionadas", "Pista"); i.addDefaultKeypress ('g', cmd); break;
        case trackMidiClip:     set ("Clip MIDI nuevo en el cursor", "Pista"); i.addDefaultKeypress ('m', cmd | shift); break;
        case viewDetail:        set ("Cambiar entre Piano y Canal", "Ver"); i.addDefaultKeypress (KeyPress::tabKey, shift); break;
        case trackAudio:        set ("Nueva pista de audio", "Pista"); i.addDefaultKeypress ('t', cmd); break;
        case trackInstrument:   set ("Nueva pista de instrumento...", "Pista"); i.addDefaultKeypress ('t', cmd | shift); break;
        case trackBadger:       set ("Nueva pista con Badger", "Pista"); break;
        case trackHamster:      set ("Nueva pista con Hamster", "Pista"); break;
        case playToggle:        set ("Reproducir / parar", "Transporte"); i.addDefaultKeypress (KeyPress::spaceKey, 0); break;
        case playRecord:        set ("Grabar", "Transporte"); i.addDefaultKeypress (KeyPress::F9Key, 0); break;
        case playLoop:          set ("Loop", "Transporte"); i.addDefaultKeypress ('l', 0); i.setTicked (session.getTransport().looping); break;
        case playToStart:       set ("Volver al principio", "Transporte"); i.addDefaultKeypress (KeyPress::homeKey, 0); break;
        case playMetronome:     set ("Metrónomo", "Transporte"); i.setTicked (session.isMetronomeOn()); break;
        case viewBrowser:       set ("Navegador", "Ver"); i.addDefaultKeypress ('1', cmd); i.addDefaultKeypress ('b', cmd | ModifierKeys::altModifier); i.setTicked (browserVisible); break;
        case viewEditor:        set ("Editor", "Ver"); i.addDefaultKeypress ('2', cmd); i.setTicked (bottom == Bottom::editor); break;
        case viewMixer:         set ("Master (mezcla)", "Ver"); i.addDefaultKeypress ('3', cmd); i.setTicked (bottom == Bottom::mixer); break;
        case viewZoomIn:        set ("Acercar", "Ver"); i.addDefaultKeypress ('=', 0); i.addDefaultKeypress ('+', 0); break;
        case viewZoomOut:       set ("Alejar", "Ver"); i.addDefaultKeypress ('-', 0); break;
        case viewZoomFit:       set ("Ver toda la canción", "Ver"); i.addDefaultKeypress ('z', 0); break;
        case viewEasy:          set ("Modo fácil", "Ver"); i.setTicked (vs.easyMode); break;
        case viewFollow:        set ("Seguir el cursor", "Ver"); i.addDefaultKeypress ('f', cmd); i.setTicked (arrangement.followPlayhead); break;
        case helpShortcuts:     set ("Atajos de teclado", "Ayuda"); break;
        case helpAbout:         set ("Acerca de Racoon", "Ayuda"); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    {
        ApplicationCommandInfo ci (info.commandID);
        getCommandInfo (info.commandID, ci);
        crashlog::note ("Comando: " + ci.shortName);
    }
    auto& um = session.getEdit().getUndoManager();
    switch (info.commandID)
    {
        case fileNew:        maybeSaveThen ([this] { showGenrePicker(); }); break;
        case fileNewHousito: maybeSaveThen ([this] { session.newProject (false); }); break;
        case fileOpen:
        {
            auto chooser = std::make_shared<FileChooser> ("Abrir proyecto", Session::projectsFolder(), "*.racoon");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, chooser] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) openProject (fc.getResult());
            });
            break;
        }
        case fileSave:
            if (session.isUntitled()) saveAs();
            else if (! session.save()) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "No se pudo guardar", session.getFile().getFullPathName());
            if (onTitleChanged) onTitleChanged();
            break;
        case fileSaveAs:       saveAs(); break;
        case fileExport:       ExportPanel::show (session); break;
        case fileSaveTemplate:
        {
            auto* w = new AlertWindow ("Guardar como plantilla", String::fromUTF8 ("Tus plantillas aparecen en el menú, en «Nuevo desde mis plantillas»."), MessageBoxIconType::NoIcon);
            w->addTextEditor ("n", session.getProjectName(), "Nombre");
            w->addButton ("GUARDAR", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("CANCELAR", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
            {
                if (r == 1 && w->getTextEditorContents ("n").isNotEmpty())
                    if (session.saveAsTemplate (w->getTextEditorContents ("n"))) vs.showHint ("Plantilla guardada");
            }), true);
            break;
        }
        case fileAudioSettings: showAudioSettings (session.engine); break;
        case fileScanPlugins:   browser.showPluginScanner(); break;
        case editUndo:
            session.getEdit().undo();
            break;
        case editRedo:
            session.getEdit().redo();
            break;
        case editDelete:     deleteSelection(); break;
        case editDuplicate:  duplicateSelection(); break;
        case editCopy:       copySelection (false); break;
        case editCut:        copySelection (true); break;
        case editPaste:      pasteClipboard(); break;
        case editSplit:      splitSelection(); break;
        case editQuantise:
            for (auto* c : session.getSelection().getItemsOfType<te::MidiClip>())
            {
                um.beginNewTransaction();
                session.quantise (*c, session.snapBeats > 0 ? session.snapBeats : 0.25);
            }
            break;
        case trackAudio:      um.beginNewTransaction(); session.getSelection().selectOnly (session.addAudioTrack()); break;
        case editReverse:     reverseSelection(); break;
        case editMuteClips:
        {
            auto clips = session.getSelection().getItemsOfType<te::Clip>();
            if (clips.isEmpty()) { vs.showHint (String::fromUTF8 ("Selecciona un clip y pulsa 0 para apagarlo o prenderlo.")); break; }
            um.beginNewTransaction();
            bool anyOn = false;
            for (auto* c : clips) anyOn = anyOn || ! c->isMuted();
            for (auto* c : clips) c->setMuted (anyOn);
            vs.showHint (anyOn ? String::fromUTF8 ("Clip apagado: no suena hasta que pulses 0 otra vez.") : String ("Clip prendido."));
            arrangement.repaint();
            break;
        }
        case editLoopSelection:
        {
            te::TimeRange r;
            if (vs.hasRange()) r = { vs.beatToTime (vs.rangeStart), vs.beatToTime (vs.rangeEnd) };
            else
            {
                auto clips = session.getSelection().getItemsOfType<te::Clip>();
                for (auto* c : clips) r = r.isEmpty() ? c->getEditTimeRange() : r.getUnionWith (c->getEditTimeRange());
            }
            if (r.isEmpty()) { vs.showHint (String::fromUTF8 ("Selecciona clips o un pedazo de la maqueta y pulsa Cmd+L.")); break; }
            session.setLoopRange (r);
            session.setLooping (true);
            arrangement.repaint();
            vs.showHint (String::fromUTF8 ("Loop sobre la selección."));
            break;
        }
        case editConsolidate: consolidateSelection(); break;
        case editMakeSong:    makeSong(); break;
        case editHarmonize:   showHarmonize(); break;
        case editRandomAll:   randomAll(); break;
        case editOctaveUp:    transposeSelectedClips (12); break;
        case editOctaveDown:  transposeSelectedClips (-12); break;
        case trackGroup:
        {
            auto tracks = session.getSelection().getItemsOfType<te::Track>();
            if (tracks.size() < 1) { vs.showHint (String::fromUTF8 ("Selecciona pistas (Mayús+clic en sus nombres) y pulsa Cmd+G.")); break; }
            um.beginNewTransaction();
            session.groupTracks (tracks);
            break;
        }
        case trackMidiClip:
        {
            te::AudioTrack* t = nullptr;
            if (auto tracks = session.getSelection().getItemsOfType<te::AudioTrack>(); ! tracks.isEmpty()) t = tracks.getFirst();
            if (t == nullptr) if (auto clips = session.getSelection().getItemsOfType<te::Clip>(); ! clips.isEmpty()) t = dynamic_cast<te::AudioTrack*> (clips.getFirst()->getTrack());
            if (t == nullptr) t = dynamic_cast<te::AudioTrack*> (channel.getTrack());
            if (t == nullptr || kindOf (*t) != TrackKind::instrument) { vs.showHint (String::fromUTF8 ("Elige un canal de instrumento y pulsa Cmd+Mayús+M.")); break; }
            const double bar = vs.beatsPerBar();
            const double at = std::floor (vs.timeToBeat (session.getTransport().getPosition()) / bar + 1.0e-9) * bar;
            um.beginNewTransaction();
            if (auto* c = session.createMidiClip (*t, { te::BeatPosition::fromBeats (at), te::BeatPosition::fromBeats (at + bar) }))
                openClipEditor (c);
            break;
        }
        case viewDetail:
            showBottom (bottom == Bottom::editor ? Bottom::channel : Bottom::editor);
            if (bottom == Bottom::editor) if (auto* t = channel.getTrack(); t != nullptr && editorClipTrack() != t) loadClipOf (*t);
            break;
        case trackInstrument:
            Menus::instrumentMenu (session, [this] (const CatalogItem& it) { session.getEdit().getUndoManager().beginNewTransaction(); addInstrument (it); })
                .showMenuAsync (PopupMenu::Options().withMousePosition());
            break;
        case trackBadger:
        case trackHamster:
            for (auto& it : Catalog::racoonItems (session.engine))
                if (it.name.contains (info.commandID == trackBadger ? "Badger" : "Hamster"))
                {
                    um.beginNewTransaction();
                    if (it.available) addInstrument (it);
                    else session.addRacoonInstrument (it.name);
                }
            break;
        case playToggle:    session.togglePlay(); break;
        case playRecord:    header->record(); break;
        case playLoop:      session.setLooping (! session.getTransport().looping); arrangement.repaint(); break;
        case playToStart:   session.returnToStart(); break;
        case playMetronome: session.setMetronome (! session.isMetronomeOn()); break;
        case viewBrowser:   browserVisible = ! browserVisible; resized(); break;
        case viewEditor:    showBottom (bottom == Bottom::editor ? Bottom::none : Bottom::editor); break;
        case viewMixer:     showBottom (bottom == Bottom::mixer ? Bottom::none : Bottom::mixer); break;
        case viewZoomIn:    vs.zoomAround (1.25, 0.0f); break;
        case viewZoomOut:   vs.zoomAround (0.8, 0.0f); break;
        case viewZoomFit:   arrangement.zoomToFit(); break;
        case viewEasy:      setEasyMode (! vs.easyMode); break;
        case viewFollow:    arrangement.followPlayhead = ! arrangement.followPlayhead; break;
        case helpShortcuts: showShortcuts(); break;
        case helpAbout:     showAbout(); break;
        default: return false;
    }
    header->refresh();
    return true;
}

StringArray MainComponent::getMenuBarNames() { return { i18n::tr ("Archivo"), i18n::tr ("Editar"), i18n::tr ("Pista"), i18n::tr ("Transporte"), i18n::tr ("Ver"), i18n::tr ("Ayuda") }; }

PopupMenu MainComponent::getMenuForIndex (int index, const String&)
{
    PopupMenu m;
    auto* cm = &commandManager;
    switch (index)
    {
        case 0:
            for (auto id : { fileNew, fileNewHousito, fileOpen }) m.addCommandItem (cm, id);
            {
                const File last (ui::prefs().getValue ("lastProject"));
                m.addItem (i18n::tr ("Abrir el último proyecto"), last.existsAsFile(),  false,
                           [this, last] { openProject (last); });
            }
            m.addSeparator();
            for (auto id : { fileSave, fileSaveAs, fileSaveTemplate }) m.addCommandItem (cm, id);
            m.addSeparator();
            m.addCommandItem (cm, fileExport);
            m.addSeparator();
            m.addCommandItem (cm, fileAudioSettings);
            m.addCommandItem (cm, fileScanPlugins);
            break;
        case 1:
            for (auto id : { editUndo, editRedo, editCopy, editCut, editPaste, editDelete, editDuplicate, editSplit, editQuantise }) m.addCommandItem (cm, id);
            m.addSeparator();
            for (auto id : { editReverse, editOctaveUp, editOctaveDown, editMuteClips, editLoopSelection, editConsolidate }) m.addCommandItem (cm, id);
            m.addSeparator();
            m.addCommandItem (cm, editMakeSong);
            m.addCommandItem (cm, editRandomAll);
            m.addCommandItem (cm, editHarmonize);
            break;
        case 2: for (auto id : { trackAudio, trackInstrument, trackMidiClip, trackGroup, trackBadger, trackHamster }) m.addCommandItem (cm, id); break;
        case 3: for (auto id : { playToggle, playRecord, playLoop, playToStart, playMetronome }) m.addCommandItem (cm, id); break;
        case 4: for (auto id : { viewBrowser, viewEditor, viewMixer, viewDetail, viewZoomIn, viewZoomOut, viewZoomFit, viewFollow, viewEasy }) m.addCommandItem (cm, id); break;
        case 5: m.addCommandItem (cm, helpShortcuts); m.addCommandItem (cm, helpAbout); break;
        default: break;
    }
    return m;
}
}

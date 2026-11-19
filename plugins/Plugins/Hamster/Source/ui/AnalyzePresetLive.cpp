#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== analyze
AnalyzePage::AnalyzePage (Ctx& c) : ctx (c), spectrum (c), scope (c), collision (c)
{
    for (Component* comp : std::initializer_list<Component*> { &specPanel, &scopePanel, &loudPanel, &trackPanel, &kbPanel, &groovePanel })
        addAndMakeVisible (comp);
    for (Component* comp : std::initializer_list<Component*> { &spectrum, &scope, &collision, &grooveView, &resetLufs, &tuneKick, &duckBass })
        addAndMakeVisible (comp);
    bridgeOn = std::make_unique<ParamButton> (ctx, "bridge_on", "KICK-BASS BRIDGE", false, C::cyan);
    bridgeTrack = std::make_unique<Stepper> (ctx, "bridge_track", "KICK TRACK");
    addAndMakeVisible (*bridgeOn);
    addAndMakeVisible (*bridgeTrack);
    bridgeOn->setTooltip ("Publishes the kick pattern so Bass Engine can duck itself to it (Bass Engine: DUCK SOURCE = Drum Engine)");
    resetLufs.onClick = [this] { ctx.proc.engine.loudness.resetRequest = true; };
    tuneKick.onClick = [this]
    {
        const int kt = jlimit (0, kNumTracks - 1, (int) ctx.getParam ("bridge_track") - 1);
        const float bass = ctx.proc.bridgeBassHz.load();
        if (bass <= 0.0f) return;
        const float off = KickBass::tuneOffsetTo (ctx.proc.kickFundamental (kt), bass);
        ctx.proc.undo.beginNewTransaction ("Tune kick");
        ctx.proc.setTrackValue (kt, T_TUNE, jlimit (-24.0f, 24.0f, ctx.proc.trackValue (kt, T_TUNE) + off));
    };
    tuneKick.setTooltip ("Moves the kick tune to the bass root reported by Bass Engine");
    duckBass.onClick = [this]
    {
        ctx.setParam ("bridge_on", 1.0f);
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Kick-Bass Bridge",
            "The bridge is on. In Bass Engine open FX and set DUCK SOURCE to \"Drum Engine\". The bass will dip exactly on every kick of track "
            + String ((int) ctx.getParam ("bridge_track")) + ", even ahead of time, with no sidechain routing.");
    };
    ctx.registerAnimated (this);
}

AnalyzePage::~AnalyzePage() { ctx.unregisterAnimated (this); }

void AnalyzePage::animate()
{
    if (! animVisible (this)) return;
    for (int t = 0; t < kNumTracks; ++t) trackHold[t] = std::max (ctx.proc.engine.telemetry.trackPeak[t].load(), trackHold[t] * 0.92f);
    if (++frame % 2 == 0) { grooveView.groove = ctx.proc.getSeqData().groove; grooveView.repaint(); }
    repaint (loudArea); repaint (trackArea); repaint (kbInfo);
}

void AnalyzePage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (r.getHeight() / 2);
    auto spec = top.removeFromLeft (top.getWidth() * 62 / 100);
    specPanel.setBounds (spec);
    spectrum.setBounds (specPanel.content() + spec.getPosition());
    top.removeFromLeft (8);
    loudPanel.setBounds (top);
    auto lc = loudPanel.content() + top.getPosition();
    resetLufs.setBounds (lc.removeFromBottom (24).removeFromRight (90));
    loudArea = lc;
    r.removeFromTop (8);
    auto kb = r.removeFromLeft (r.getWidth() * 45 / 100);
    kbPanel.setBounds (kb);
    auto kc = kbPanel.content() + kb.getPosition();
    auto kTop = kc.removeFromTop (34);
    bridgeOn->setBounds (kTop.removeFromLeft (170).reduced (0, 3));
    kTop.removeFromLeft (8);
    bridgeTrack->setBounds (kTop.removeFromLeft (110).withTrimmedTop (-6));
    kc.removeFromTop (6);
    collision.setBounds (kc.removeFromTop (kc.getHeight() / 2));
    kc.removeFromTop (6);
    auto kbtn = kc.removeFromBottom (26);
    tuneKick.setBounds (kbtn.removeFromLeft (170));
    kbtn.removeFromLeft (6);
    duckBass.setBounds (kbtn.removeFromLeft (150));
    kbInfo = kc;
    r.removeFromLeft (8);
    auto mid = r.removeFromLeft (r.getWidth() / 2 - 4);
    trackPanel.setBounds (mid);
    trackArea = trackPanel.content() + mid.getPosition();
    r.removeFromLeft (8);
    auto sc = r.removeFromTop (r.getHeight() / 2 - 4);
    scopePanel.setBounds (sc);
    scope.setBounds (scopePanel.content() + sc.getPosition());
    r.removeFromTop (8);
    groovePanel.setBounds (r);
    grooveView.setBounds (groovePanel.content() + r.getPosition());
}

void AnalyzePage::paintOverChildren (Graphics& g)
{
    auto& L = ctx.proc.engine.loudness;
    auto& tel = ctx.proc.engine.telemetry;
    // loudness readouts
    {
        auto a = loudArea;
        auto cell = [&] (const String& name, const String& value, Colour c)
        {
            auto row = a.removeFromTop (28);
            g.setFont (Fonts::label (12.0f)); g.setColour (S::inkSoft);
            g.drawText (::racoon::i18n::tr (name), row.removeFromLeft (130), Justification::centredLeft);
            g.setFont (Fonts::mono (18.0f)); g.setColour (c);
            g.drawText (::racoon::i18n::tr (value), row, Justification::centredRight);
        };
        auto fmt = [] (float v, const char* unit) { return v <= -69.9f ? String ("-inf ") + unit : String (v, 1) + " " + unit; };
        const Colour warn = Brand::rosa.darker (0.45f);
        cell ("MOMENTARY", fmt (L.momentary.load(), "LUFS"), S::ink);
        cell ("SHORT TERM", fmt (L.shortTerm.load(), "LUFS"), S::ink);
        cell ("INTEGRATED", fmt (L.integrated.load(), "LUFS"), S::ink);
        cell ("TRUE PEAK MAX", fmt (gainToDb (L.truePeakMax.load()), "dBTP"), L.truePeakMax.load() > 0.99f ? warn : S::ink);
        cell ("CREST", String (L.crest.load(), 1) + " dB", S::ink);
        cell ("CORRELATION", String (L.correlation.load(), 2), L.correlation.load() < 0.0f ? warn : S::ink);
        cell ("STEREO WIDTH", String (roundToInt (L.width.load() * 100)) + " %", S::ink);
        cell ("GLUE  /  LIMIT", String (tel.glueGr.load(), 1) + "  /  " + String (tel.limGr.load(), 1) + " dB", S::ink);
    }
    // track activity bars
    {
        auto a = trackArea;
        const float rh = (float) a.getHeight() / kNumTracks;
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto row = Rectangle<float> ((float) a.getX(), (float) a.getY() + (float) t * rh, (float) a.getWidth(), rh).reduced (0.0f, 1.0f);
            g.setFont (Fonts::label (11.0f)); g.setColour (S::ink);
            g.drawFittedText (::racoon::i18n::tr (String (t + 1) + " " + ctx.proc.getTrackName (t).toUpperCase()), row.removeFromLeft (110.0f).toNearestInt(), Justification::centredLeft, 1, 0.6f);
            g.setColour (C::engrave); g.fillRect (row);
            const float n = jlimit (0.0f, 1.0f, (gainToDb (trackHold[t]) + 60.0f) / 66.0f);
            g.setColour (C::amber.withAlpha (0.8f)); g.fillRect (row.withWidth (row.getWidth() * n));
            const float gr = jlimit (0.0f, 18.0f, -tel.trackGr[t].load());
            if (gr > 0.1f) { g.setColour (C::cyan); g.fillRect (row.withX (row.getRight() - row.getWidth() * gr / 36.0f).withWidth (row.getWidth() * gr / 36.0f).withHeight (3.0f)); }
        }
    }
    // kick / bass info
    {
        auto a = kbInfo;
        const int kt = jlimit (0, kNumTracks - 1, (int) ctx.getParam ("bridge_track") - 1);
        const float kHz = ctx.proc.kickFundamental (kt), bHz = ctx.proc.bridgeBassHz.load();
        const float col = tel.collision.load();
        g.setFont (Fonts::mono (13.0f));
        g.setColour (S::ink);
        g.drawText (::racoon::i18n::tr ("KICK  " + String (kHz, 1) + " Hz  " + KickBass::noteName (kHz)), a.removeFromTop (20), Justification::centredLeft);
        g.setColour (S::ink);
        g.drawText (::racoon::i18n::tr ("BASS  " + (bHz > 0 ? String (bHz, 1) + " Hz  " + KickBass::noteName (bHz) : String (ctx.proc.bridgeBassSeen.load() ? "silent" : "not connected"))),
                    a.removeFromTop (20), Justification::centredLeft);
        g.setColour (col > 0.45f ? Brand::rosa.darker (0.45f) : S::ink);
        g.drawText (::racoon::i18n::tr ("LOW-END OVERLAP  " + String (roundToInt (col * 100)) + " %"), a.removeFromTop (20), Justification::centredLeft);
        g.setFont (Fonts::body (13.0f)); g.setColour (S::inkSoft);
        g.drawFittedText (::racoon::i18n::tr (KickBass::advice (kHz, bHz, col)), a, Justification::topLeft, 3);
    }
}

// ===================================================================== presets
PresetPage::PresetPage (Ctx& c) : ctx (c)
{
    for (Component* comp : std::initializer_list<Component*> { &filterPanel, &listPanel, &dnaPanel, &groovePanel, &soundPanel }) addAndMakeVisible (comp);
    for (Component* comp : std::initializer_list<Component*> { &search, &genre, &character, &loadMode, &favOnly, &list, &loadBtn, &saveBtn, &deleteBtn, &favBtn,
                                                               &dnaLoad, &dnaMode, &dnaSummary, &grooveLoad, &grooveClear, &grooveSummary, &grooveView,
                                                               &soundList, &soundSave, &soundLoad })
        addAndMakeVisible (comp);
    search.setTextToShowWhenEmpty (::racoon::i18n::tr ("buscar"), S::inkFaint);
    search.setColour (TextEditor::textColourId, S::ink);
    search.onTextChange = [this] { applyFilter(); };
    genre.addItem ("All genres", 1);
    genre.addItemList (genreNames(), 2);
    genre.addItem ("Init", 20);
    genre.setSelectedId (1, dontSendNotification);
    genre.onChange = [this] { applyFilter(); };
    character.addItemList ({ "All characters", "Punchy", "Deep", "Modern", "Lo-Fi", "Neutral" }, 1);
    character.setSelectedId (1, dontSendNotification);
    character.onChange = [this] { applyFilter(); };
    loadMode.addItemList ({ "Load kit + patterns", "Load kit only", "Load patterns only" }, 1);
    loadMode.setSelectedId (1, dontSendNotification);
    favOnly.onClick = [this] { applyFilter(); };
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (ListBox::backgroundColourId, C::void_.withAlpha (0.6f));
    loadBtn.onClick = [this] { const int r = list.getSelectedRow(); if (r >= 0 && r < (int) shown.size()) ctx.proc.presets().load (shown[(size_t) r], loadMode.getSelectedItemIndex()); };
    saveBtn.onClick = [this] { saveDialog(); };
    deleteBtn.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r >= 0 && r < (int) shown.size() && ! shown[(size_t) r].factory) { ctx.proc.presets().deleteUser (shown[(size_t) r]); refresh(); }
    };
    favBtn.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r >= 0 && r < (int) shown.size()) { ctx.proc.presets().setFavourite (shown[(size_t) r], ! shown[(size_t) r].favourite); refresh(); }
    };
    dnaMode.addItemList ({ "Synth only", "Synth + sample (hybrid)", "Sample only" }, 1);
    dnaMode.setSelectedId (2, dontSendNotification);
    dnaSummary.setFont (Fonts::body (13.0f));
    dnaSummary.setColour (Label::textColourId, S::inkSoft);
    dnaSummary.setJustificationType (Justification::topLeft);
    dnaSummary.setText ("Drop a one-shot here, or click ANALYSE. Drum DNA detects the role, pitch, decay, brightness and noise, "
                        "then rebuilds the hit on the selected track.", dontSendNotification);
    dnaLoad.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Drum DNA: choose a one-shot", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) analyseDna (fc.getResult()); });
    };
    grooveSummary.setFont (Fonts::body (13.0f));
    grooveSummary.setColour (Label::textColourId, S::inkSoft);
    grooveSummary.setJustificationType (Justification::topLeft);
    grooveSummary.setText ("Drop a drum loop here. Groove DNA measures the push and pull of every 1/16 and the accents, then applies it to all patterns.", dontSendNotification);
    grooveLoad.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Groove DNA: choose a loop", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) analyseGroove (fc.getResult()); });
    };
    grooveClear.onClick = [this] { ctx.proc.editSeq ([] (SeqData& d) { d.groove = Groove(); }); grooveView.groove = Groove(); grooveView.repaint(); };
    grooveAmt = std::make_unique<ParamKnob> (ctx, "g_groove", "AMOUNT");
    addAndMakeVisible (*grooveAmt);
    soundSave.onClick = [this]
    {
        auto* w = new AlertWindow ("Save sound", "Name for track " + String (ctx.track() + 1), AlertWindow::NoIcon);
        w->addTextEditor ("name", ctx.proc.getTrackName (ctx.track()));
        w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1) { ctx.proc.presets().saveSound (ctx.track(), w->getTextEditorContents ("name")); refresh(); }
            delete w;
        }), false);
    };
    soundLoad.onClick = [this]
    {
        const auto name = soundList.getText();
        if (name.isNotEmpty()) { ctx.proc.undo.beginNewTransaction ("Load sound"); ctx.proc.presets().loadSound (ctx.track(), name); if (ctx.refreshAll) ctx.refreshAll(); }
    };
    refresh();
}

void PresetPage::refresh()
{
    all = ctx.proc.presets().list();
    applyFilter();
    soundList.clear (dontSendNotification);
    soundList.addItemList (ctx.proc.presets().soundNames(), 1);
    grooveView.groove = ctx.proc.getSeqData().groove;
    grooveView.repaint();
}

void PresetPage::applyFilter()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    const auto gsel = genre.getSelectedId() == 1 ? String() : genre.getText();
    const auto csel = character.getSelectedId() == 1 ? String() : character.getText();
    for (auto& p : all)
    {
        if (q.isNotEmpty() && ! p.name.toLowerCase().contains (q) && ! p.genre.toLowerCase().contains (q)) continue;
        if (gsel.isNotEmpty() && p.genre != gsel) continue;
        if (csel.isNotEmpty() && p.character != csel) continue;
        if (favOnly.getToggleState() && ! p.favourite) continue;
        shown.push_back (p);
    }
    list.updateContent();
    list.repaint();
}

void PresetPage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row];
    const bool current = ctx.proc.presets().currentKey == p.key();
    g.setColour (selected ? S::hi : (row % 2 ? S::ink.withAlpha (0.05f) : Colours::transparentBlack));
    g.fillRect (0, 0, w, h);
    if (current) g.setColour (S::ink), g.fillRect (0, 0, 4, h);
    g.setFont (current ? Fonts::bodyBold (15.0f) : Fonts::body (15.0f));
    g.setColour (S::ink);
    g.drawText (::racoon::i18n::tr (p.name), 10, 0, w / 2, h, Justification::centredLeft);
    g.setFont (Fonts::mono (11.0f));
    g.setColour (S::inkSoft);
    g.drawText (::racoon::i18n::tr (p.genre + "  /  " + p.character + "  /  " + String (p.bpm) + " BPM"), w / 2, 0, w / 2 - 40, h, Justification::centredLeft);
    g.setColour (p.factory ? S::inkFaint : S::ink);
    g.drawText (::racoon::i18n::tr (p.factory ? "FACTORY" : "USER"), w - 90, 0, 60, h, Justification::centredRight);
    if (p.favourite) { g.setColour (S::ink); g.drawText (::racoon::i18n::tr ("*"), w - 24, 0, 20, h, Justification::centred); }
}

void PresetPage::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    if (row >= 0 && row < (int) shown.size()) { ctx.proc.presets().load (shown[(size_t) row], loadMode.getSelectedItemIndex()); list.repaint(); }
}

void PresetPage::saveDialog()
{
    auto* w = new AlertWindow ("Save kit", "Kit + patterns are saved together", AlertWindow::NoIcon);
    w->addTextEditor ("name", ctx.proc.currentPresetName);
    w->addComboBox ("genre", genreNames(), "Genre");
    w->addComboBox ("character", { "Punchy", "Deep", "Modern", "Lo-Fi", "Neutral" }, "Character");
    w->addTextEditor ("bank", "User", "Bank / folder");
    w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
    w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r == 1)
        {
            ctx.proc.presets().saveUser (w->getTextEditorContents ("name"), w->getComboBoxComponent ("genre")->getText(),
                                         w->getComboBoxComponent ("character")->getText(), w->getTextEditorContents ("bank"));
            refresh();
        }
        delete w;
    }), false);
}

void PresetPage::analyseDna (const File& f)
{
    auto r = DrumDNA::analyse (f);
    if (! r.ok) { dnaSummary.setText (r.summary, dontSendNotification); return; }
    DrumDNA::apply (ctx.proc, ctx.track(), r, dnaMode.getSelectedItemIndex());
    dnaSummary.setText ("Track " + String (ctx.track() + 1) + ": " + r.summary, dontSendNotification);
    if (ctx.refreshAll) ctx.refreshAll();
}

void PresetPage::analyseGroove (const File& f)
{
    auto r = GrooveDNA::analyse (f, ctx.getParam ("g_bpm") > 0 ? 0.0 : 0.0);
    if (! r.ok) { grooveSummary.setText (r.summary, dontSendNotification); return; }
    ctx.proc.editSeq ([&r] (SeqData& d) { d.groove = r.groove; });
    if (ctx.getParam ("g_groove") < 0.05f) ctx.setParam ("g_groove", 1.0f);
    grooveSummary.setText ("Groove captured: " + r.summary, dontSendNotification);
    grooveView.groove = r.groove;
    grooveView.repaint();
}

void PresetPage::filesDropped (const StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    const File f (files[0]);
    if (groovePanel.getBounds().contains (x, y)) analyseGroove (f);
    else analyseDna (f);
}

void PresetPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto right = r.removeFromRight (440);
    auto left = r;
    auto fArea = left.removeFromTop (78);
    filterPanel.setBounds (fArea);
    auto fc = filterPanel.content() + fArea.getPosition();
    auto f1 = fc.removeFromTop (26);
    search.setBounds (f1.removeFromLeft (220)); f1.removeFromLeft (6);
    genre.setBounds (f1.removeFromLeft (150)); f1.removeFromLeft (6);
    character.setBounds (f1.removeFromLeft (150)); f1.removeFromLeft (6);
    favOnly.setBounds (f1.removeFromLeft (120));
    left.removeFromTop (8);
    listPanel.setBounds (left);
    auto lc = listPanel.content() + left.getPosition();
    auto lb = lc.removeFromBottom (30);
    loadMode.setBounds (lb.removeFromLeft (200)); lb.removeFromLeft (8);
    layoutRow (lb, { &loadBtn, &saveBtn, &deleteBtn, &favBtn }, 6);
    lc.removeFromBottom (6);
    list.setBounds (lc);

    auto dArea = right.removeFromTop (right.getHeight() * 36 / 100);
    right.removeFromTop (8);
    dnaPanel.setBounds (dArea.withTrimmedLeft (8));
    auto dc = dnaPanel.content() + dnaPanel.getPosition();
    auto d1 = dc.removeFromTop (26);
    dnaLoad.setBounds (d1.removeFromLeft (170)); d1.removeFromLeft (6);
    dnaMode.setBounds (d1);
    dc.removeFromTop (6);
    dnaSummary.setBounds (dc);
    auto gArea = right.removeFromTop (right.getHeight() * 62 / 100);
    right.removeFromTop (8);
    groovePanel.setBounds (gArea.withTrimmedLeft (8));
    auto gc = groovePanel.content() + groovePanel.getPosition();
    auto g1 = gc.removeFromTop (26);
    grooveLoad.setBounds (g1.removeFromLeft (150)); g1.removeFromLeft (6);
    grooveClear.setBounds (g1.removeFromLeft (130));
    gc.removeFromTop (6);
    auto gk = gc.removeFromRight (80);
    grooveAmt->setBounds (gk.removeFromTop (90));
    grooveSummary.setBounds (gc.removeFromTop (44));
    grooveView.setBounds (gc);
    soundPanel.setBounds (right.withTrimmedLeft (8));
    auto sc = soundPanel.content() + soundPanel.getPosition();
    soundList.setBounds (sc.removeFromTop (26));
    sc.removeFromTop (6);
    layoutRow (sc.removeFromTop (26), { &soundSave, &soundLoad }, 6);
}

void PresetPage::paintOverChildren (Graphics& g)
{
    juce::ignoreUnused (g);
}

// ===================================================================== live
LivePage::LivePage (Ctx& c) : ctx (c), pads (c, true), xy (c)
{
    for (Component* comp : std::initializer_list<Component*> { &padPanel, &patPanel, &fxPanel, &mutePanel }) addAndMakeVisible (comp);
    addAndMakeVisible (pads);
    addAndMakeVisible (xy);
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto* b = patBtns.add (new TextButton (String::charToString ((juce_wchar) ('A' + p))));
        b->onClick = [this, p] { ctx.setParam ("g_pattern", (float) (p + 1)); };
        addAndMakeVisible (b);
    }
    for (int t = 0; t < kNumTracks; ++t)
        addAndMakeVisible (mutes.add (new ParamButton (ctx, trackParamId (t, T_MUTE), String (t + 1), false, C::ochre)));
    fill = std::make_unique<ParamButton> (ctx, "g_fill", "FILL", true, C::cyan);
    brk = std::make_unique<ParamButton> (ctx, "g_break", "BREAK", true, C::cyan);
    rec = std::make_unique<ParamButton> (ctx, "g_record", "REC", false, C::ochre);
    songMode = std::make_unique<ParamButton> (ctx, "g_songmode", "SONG");
    sceneOn = std::make_unique<ParamButton> (ctx, "g_sceneon", "SCENES");
    playBtn = std::make_unique<ParamButton> (ctx, "g_play", "PLAY", false, C::jade);
    noteRepeat = std::make_unique<ChoiceBox> (ctx, "g_noterepeat", "NOTE REPEAT");
    morph = std::make_unique<ParamKnob> (ctx, "g_morph", "MORPH");
    sceneFade = std::make_unique<ParamKnob> (ctx, "g_scenefade", "SCENE A/B");
    swing = std::make_unique<ParamKnob> (ctx, "g_swing", "SWING");
    humanize = std::make_unique<ParamKnob> (ctx, "g_humanize", "HUMANIZE");
    morphTarget = std::make_unique<Stepper> (ctx, "g_morphtarget", "MORPH TO", [] (int v) { return String::charToString ((juce_wchar) ('A' + v - 1)); });
    sceneA = std::make_unique<Stepper> (ctx, "g_scenea", "SCENE A");
    sceneB = std::make_unique<Stepper> (ctx, "g_sceneb", "SCENE B");
    for (Component* comp : std::initializer_list<Component*> { fill.get(), brk.get(), rec.get(), songMode.get(), sceneOn.get(), playBtn.get(), noteRepeat.get(), morph.get(),
                                                               sceneFade.get(), swing.get(), humanize.get(), morphTarget.get(), sceneA.get(), sceneB.get() })
        addAndMakeVisible (comp);
    const char* sc[5] { "1/2", "1/4", "1/8", "1/16", "1/32" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = scatterBtns.add (new TextButton (sc[i]));
        b->setTooltip ("Hold to repeat the last " + String (sc[i]) + " (stutter)");
        b->onStateChange = [this, i, b]
        {
            const bool down = b->isDown();
            if (down) ctx.setParam ("g_scatter", (float) (i + 1));
            else if ((int) ctx.getParam ("g_scatter") == i + 1) ctx.setParam ("g_scatter", 0.0f);
        };
        addAndMakeVisible (b);
    }
    ctx.registerAnimated (this);
}

LivePage::~LivePage() { ctx.unregisterAnimated (this); }

void LivePage::animate()
{
    if (! animVisible (this)) return;
    if (++tick % 15 == 0)
        for (int t = 0; t < kNumTracks; ++t) mutes[t]->setText (String (t + 1) + "  " + ctx.proc.getTrackName (t).toUpperCase());
    const int ap = ctx.proc.engine.telemetry.activePattern.load(), q = ctx.proc.engine.telemetry.queuedPattern.load();
    if (ap != lastPattern || q != lastQueued) { lastPattern = ap; lastQueued = q; repaint(); }
}

void LivePage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (std::min (r.getHeight() + 10, 620));
    padPanel.setBounds (left);
    pads.setBounds (padPanel.content() + left.getPosition());
    r.removeFromLeft (8);
    auto right = r.removeFromRight (340);
    auto pArea = r.removeFromTop (150);
    patPanel.setBounds (pArea);
    auto pc = patPanel.content() + pArea.getPosition();
    auto prow = pc.removeFromTop (54);
    const int pw = prow.getWidth() / kNumPatterns;
    for (int p = 0; p < kNumPatterns; ++p) patBtns[p]->setBounds (prow.getX() + p * pw + 2, prow.getY(), pw - 4, 54);
    pc.removeFromTop (10);
    layoutRow (pc.removeFromTop (36), { playBtn.get(), rec.get(), songMode.get(), fill.get(), brk.get() }, 6);
    r.removeFromTop (8);
    auto fArea = r.removeFromTop (r.getHeight() - 170);
    fxPanel.setBounds (fArea);
    auto fc = fxPanel.content() + fArea.getPosition();
    auto srow = fc.removeFromTop (44);
    const int sw = srow.getWidth() / 5;
    for (int i = 0; i < 5; ++i) scatterBtns[i]->setBounds (srow.getX() + i * sw + 2, srow.getY(), sw - 4, 44);
    fc.removeFromTop (10);
    auto k1 = fc.removeFromTop (100);
    layoutRow (k1, { morph.get(), swing.get(), humanize.get(), sceneFade.get() });
    fc.removeFromTop (6);
    layoutRow (fc.removeFromTop (40), { morphTarget.get(), noteRepeat.get(), sceneA.get(), sceneB.get() }, 8);
    fc.removeFromTop (8);
    sceneOn->setBounds (fc.removeFromTop (30).removeFromLeft (140));
    r.removeFromTop (8);
    mutePanel.setBounds (r);
    auto mc = mutePanel.content() + r.getPosition();
    const int mw = mc.getWidth() / 8;
    for (int t = 0; t < kNumTracks; ++t) mutes[t]->setBounds (mc.getX() + (t % 8) * mw + 2, mc.getY() + (t / 8) * (mc.getHeight() / 2), mw - 4, mc.getHeight() / 2 - 4);
    right.removeFromLeft (8);
    xy.setBounds (right.removeFromTop (right.getWidth()));
}

void LivePage::paintOverChildren (Graphics& g)
{
    const int ap = ctx.proc.engine.telemetry.activePattern.load(), q = ctx.proc.engine.telemetry.queuedPattern.load();
    const int edit = (int) ctx.getParam ("g_pattern") - 1;
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto r = patBtns[p]->getBounds().toFloat();
        if (p == edit) { g.setColour (C::amber); g.drawRect (r, 1.8f); }
        if (ctx.proc.engine.telemetry.playing.load() && p == ap) Draw::glowLine (g, { r.getX() + 6.0f, r.getBottom() - 4.0f }, { r.getRight() - 6.0f, r.getBottom() - 4.0f }, C::cyan, 1.6f);
        if (p == q) { g.setColour (C::cyan); g.drawRect (r.reduced (4.0f), 1.0f); }
    }
    g.setFont (Fonts::body (12.5f)); g.setColour (C::ash);
    auto xr = xy.getBounds().translated (0, xy.getHeight() + 8).withHeight (60);
    g.drawFittedText (::racoon::i18n::tr ("XY X / Y are modulation sources: route them in MOD to anything. Pads follow velocity by height. "
                      "Keys A..K / Z..M play pads 1-16."), xr, Justification::topLeft, 4);
    juce::ignoreUnused (g);
}
} // namespace be::ui

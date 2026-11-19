#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

namespace
{
constexpr int kNameW = 132;
const char* kPatNames = "ABCDEFGH";
}

// ===================================================================== step grid
StepGrid::StepGrid (Ctx& c) : ctx (c)
{
    data = ctx.proc.getSeqData();
    setWantsKeyboardFocus (true);
    editor.hit = [this] (Point<float> p, int& t, int& s) { return hit (p, t, s); };
    editor.cellRect = [this] (int t, int s) { return cell (t, s); };
    editor.visibleSteps = [this] { return Range<int> (page * 16, page * 16 + 16); };
    editor.getData = [this] { return ctx.proc.getSeqData(); };
    editor.setData = [this] (const SeqData& d, bool undoable) { data = d; commit (undoable); };
    editor.pattern = [this] { return pattern(); };
    editor.onChoose = [this] (int t, int s)
    {
        if (t != ctx.track()) ctx.selectTrack (t);
        ctx.selStep = s;
        if (onSelect) onSelect();
    };
    editor.onHear = [this] (int t) { ctx.proc.padDown (t, 0.8f); ctx.proc.padUp (t); };
    editor.repaint = [this] { repaint(); };
    ctx.registerAnimated (this);
}

StepGrid::~StepGrid() { ctx.unregisterAnimated (this); }

int StepGrid::pattern() const { return jlimit (0, kNumPatterns - 1, (int) ctx.getParam ("g_pattern") - 1); }

Rectangle<float> StepGrid::cell (int t, int s) const
{
    const float rh = (float) getHeight() / kNumTracks;
    const float cw = (float) (getWidth() - kNameW) / 16.0f;
    const int col = s - page * 16;
    return Rectangle<float> ((float) kNameW + (float) col * cw, (float) t * rh, cw, rh).reduced (1.5f, 1.5f);
}

bool StepGrid::hit (Point<float> p, int& t, int& s) const
{
    const float rh = (float) getHeight() / kNumTracks;
    t = jlimit (0, kNumTracks - 1, (int) (p.y / rh));
    if (p.x < kNameW) { s = -1; return true; }
    const float cw = (float) (getWidth() - kNameW) / 16.0f;
    s = page * 16 + jlimit (0, 15, (int) ((p.x - (float) kNameW) / cw));
    return true;
}

void StepGrid::paint (Graphics& g)
{
    const int pat = pattern();
    const auto& P = data.pats[pat];
    const float rh = (float) getHeight() / kNumTracks;
    bool anySolo = false;
    for (int t = 0; t < kNumTracks; ++t) anySolo |= ctx.proc.trackValue (t, T_SOLO) > 0.5f;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = P.tracks[t];
        const bool sel = t == ctx.track();
        const bool muted = ctx.proc.trackValue (t, T_MUTE) > 0.5f;
        const bool soloed = ctx.proc.trackValue (t, T_SOLO) > 0.5f;
        // name column
        auto nameR = Rectangle<float> (0.0f, (float) t * rh, (float) kNameW - 4.0f, rh).reduced (1.0f);
        g.setColour (sel ? C::stoneHi : C::void_.withAlpha (0.6f));
        g.fillRect (nameR);
        if (sel) g.setColour (C::amber), g.fillRect (nameR.withWidth (2.0f));
        auto ms = nameR.removeFromRight (40.0f);
        auto mR = ms.removeFromLeft (20.0f).reduced (2.0f), sR = ms.reduced (2.0f);
        g.setColour (muted ? C::ochre.withAlpha (0.8f) : C::void_); g.fillRect (mR);
        g.setColour (soloed ? C::jade.withAlpha (0.8f) : C::void_); g.fillRect (sR);
        g.setColour (C::ridge); g.drawRect (mR, 0.8f); g.drawRect (sR, 0.8f);
        g.setFont (Fonts::mono (9.5f)); g.setColour (C::bone);
        g.drawText (::racoon::i18n::tr ("M"), mR, Justification::centred); g.drawText (::racoon::i18n::tr ("S"), sR, Justification::centred);
        g.setFont (Fonts::label (12.0f));
        g.setColour ((muted || (anySolo && ! soloed)) ? C::ash : (sel ? C::amberHot : C::bone));
        g.drawFittedText (::racoon::i18n::tr (String (t + 1).paddedLeft ('0', 2) + "  " + ctx.proc.getTrackName (t).toUpperCase()), nameR.reduced (6.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1, 0.6f);
        // steps
        for (int c = 0; c < 16; ++c)
        {
            const int s = page * 16 + c;
            auto r = cell (t, s);
            const bool inLen = s < tp.length;
            const auto& st = tp.steps[s];
            const bool beat = c % 4 == 0;
            g.setColour (inLen ? (beat ? C::stone.brighter (0.08f) : C::stone) : C::void_.withAlpha (0.6f));
            g.fillRect (r);
            if (inLen && st.on)
            {
                const float v = (float) st.vel / 127.0f;
                g.setColour (C::amber.withAlpha (0.35f + 0.65f * v));
                auto fill = r.reduced (2.0f);
                g.fillRect (fill.withTop (fill.getBottom() - fill.getHeight() * (0.35f + 0.65f * v)));
                if (st.accent) { g.setColour (C::amberHot); g.drawRect (r.reduced (1.0f), 1.5f); }
                if (st.cond != 0) { Path tri; tri.addTriangle (r.getRight() - 9.0f, r.getY() + 1.0f, r.getRight() - 1.0f, r.getY() + 1.0f, r.getRight() - 1.0f, r.getY() + 9.0f); g.setColour (C::cyan); g.fillPath (tri); }
                if (st.prob < 100) { g.setColour (C::void_); g.setFont (Fonts::mono (8.5f)); g.drawText (::racoon::i18n::tr (String (st.prob)), r.reduced (2.0f), Justification::topLeft); }
                if (st.retrig > 1)
                {
                    g.setColour (C::void_.withAlpha (0.8f));
                    for (int k = 0; k < st.retrig; ++k) g.fillRect (r.getX() + 3.0f + (float) k * 3.0f, r.getBottom() - 6.0f, 2.0f, 3.0f);
                }
                if (st.numLocks > 0) { g.setColour (C::cyan); g.fillEllipse (r.getX() + 3.0f, r.getY() + 3.0f, 5.0f, 5.0f); }
                if (st.micro != 0) { g.setColour (C::void_); const float mx = r.getCentreX() + (float) st.micro / 100.0f * r.getWidth(); g.drawVerticalLine ((int) mx, r.getY() + 2.0f, r.getY() + 7.0f); }
            }
            if (playing && playStep[t] == s && inLen) { g.setColour (C::cyan.withAlpha (0.7f)); g.drawRect (r, 1.5f); }
            if (sel && s == ctx.selStep) { g.setColour (Colours::white); g.drawRect (r.expanded (0.5f), 1.6f); }
        }
    }
    editor.paintOverlay (g, C::cyan);
    if (editor.drawMode)
    {
        g.setColour (C::cyan);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("LÁPIZ (B)")), getLocalBounds().removeFromTop (14).reduced (4, 0), Justification::topRight);
    }
}

void StepGrid::commit (bool undoable)
{
    ctx.proc.setSeqData (data, undoable);
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    data = ctx.proc.getSeqData();
    int t, s;
    if (! hit (e.position, t, s)) return;
    dragTrack = -1;
    if (s < 0)
    {
        // name column: select / mute / solo
        if (e.x > kNameW - 44 && e.x < kNameW - 24) ctx.proc.setTrackValue (t, T_MUTE, ctx.proc.trackValue (t, T_MUTE) > 0.5f ? 0.0f : 1.0f);
        else if (e.x >= kNameW - 24 && e.x < kNameW - 4) ctx.proc.setTrackValue (t, T_SOLO, ctx.proc.trackValue (t, T_SOLO) > 0.5f ? 0.0f : 1.0f);
        else { ctx.selectTrack (t); ctx.proc.padDown (t, 0.85f); }
        repaint();
        return;
    }
    const int pat = pattern();
    auto& tp = data.pats[pat].tracks[t];
    if (s >= tp.length) return;
    auto& st = tp.steps[s];
    if (e.mods.isPopupMenu())
    {
        // right click: the step's own things
        ctx.selectTrack (t);
        ctx.selStep = s;
        if (onSelect) onSelect();
        PopupMenu m;
        m.addItem (String::fromUTF8 ("Acento"), true, st.on && st.accent, [this, t, s]
        {
            ctx.proc.editSeq ([&] (SeqData& d) { auto& x = d.pats[pattern()].tracks[t].steps[s]; x.accent = ! x.accent; if (x.accent) x.on = true; }, true);
            reload();
        });
        m.addItem (st.on ? String::fromUTF8 ("Quitar el golpe") : String::fromUTF8 ("Poner un golpe"), [this, t, s]
        {
            ctx.proc.editSeq ([&] (SeqData& d) { auto& x = d.pats[pattern()].tracks[t].steps[s]; x.on = ! x.on; if (x.on && x.vel == 0) x.vel = 100; }, true);
            reload();
        });
        m.showMenuAsync (PopupMenu::Options());
        repaint();
        return;
    }
    if (e.mods.isCommandDown() && ! e.mods.isShiftDown() && st.on)
    {
        // Cmd + drag up or down on a hit: its velocity
        dragVel = true; dragTrack = t; dragStep = s; dragStartVel = st.vel;
        ctx.proc.editSeq ([] (SeqData&) {}, true);
        return;
    }
    dragVel = false;
    editor.mouseDown (e);
    repaint();
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    if (dragVel && dragTrack >= 0)
    {
        auto& st = data.pats[pattern()].tracks[dragTrack].steps[dragStep];
        st.vel = (uint8_t) jlimit (1, 127, dragStartVel - e.getDistanceFromDragStartY());
        commit (false);
        repaint();
        if (onSelect) onSelect();
        return;
    }
    editor.mouseDrag (e);
}

void StepGrid::mouseUp (const MouseEvent& e)
{
    if (dragVel) { dragVel = false; dragTrack = -1; return; }
    editor.mouseUp (e);
}

void StepGrid::mouseDoubleClick (const MouseEvent& e) { editor.mouseDoubleClick (e); }
bool StepGrid::keyPressed (const KeyPress& k) { return editor.keyPressed (k); }

void StepGrid::animate()
{
    if (! animVisible (this)) return;
    auto& tel = ctx.proc.engine.telemetry;
    bool changed = tel.playing.load() != playing;
    playing = tel.playing.load();
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int s = tel.trackStep[t].load();
        if (s != playStep[t]) { playStep[t] = s; changed = true; }
    }
    if (changed) repaint();
}

// ===================================================================== export handle
void ExportHandle::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (dragging ? C::stoneHi : C::void_);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (audio ? C::cyan : C::amber);
    g.drawRoundedRectangle (r, 3.0f, 1.2f);
    g.setFont (Fonts::label (12.0f));
    g.drawFittedText (::racoon::i18n::tr (audio ? "DRAG AUDIO" : "DRAG MIDI"), r.toNearestInt(), Justification::centred, 1, 0.6f);
}

void ExportHandle::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    repaint();
    const int pat = jlimit (0, kNumPatterns - 1, (int) ctx.getParam ("g_pattern") - 1);
    const int b = bars ? bars() : 4;
    File f = audio ? ctx.proc.exportPatternToAudio (pat, b, solo ? solo() : -1) : ctx.proc.exportPatternToMidi (pat, b);
    if (f.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; repaint(); });
    else { dragging = false; repaint(); }
}

// ===================================================================== step editor
StepEditor::StepEditor (Ctx& c) : ctx (c)
{
    for (Component* comp : std::initializer_list<Component*> { &vel, &prob, &micro, &retrig, &len, &pitch, &condA, &condB, &cond, &mode, &on, &accent, &addLock, &clearLocks })
        addAndMakeVisible (comp);
    cond.addItemList (condNames(), 1);
    mode.addItemList ({ "Ratchet", "Flam", "Roll Up", "Roll Down" }, 1);
    micro.setTooltip ("Micro timing in percent of a step");
    vel.onChange = [this] (int v) { write ([v] (Step& s) { s.vel = (uint8_t) v; }); };
    prob.onChange = [this] (int v) { write ([v] (Step& s) { s.prob = (uint8_t) v; }); };
    micro.onChange = [this] (int v) { write ([v] (Step& s) { s.micro = (int8_t) v; }); };
    retrig.onChange = [this] (int v) { write ([v] (Step& s) { s.retrig = (uint8_t) v; }); };
    len.onChange = [this] (int v) { write ([v] (Step& s) { s.len = (uint8_t) v; }); };
    len.setTooltip ("Gate length in quarter steps (used by ADSR envelopes)");
    pitch.onChange = [this] (int v) { write ([v] (Step& s) { s.pitch = (int8_t) v; }); };
    condA.onChange = [this] (int v) { write ([v] (Step& s) { s.condA = (uint8_t) v; }); };
    condB.onChange = [this] (int v) { write ([v] (Step& s) { s.condB = (uint8_t) v; }); };
    cond.onChange = [this] { write ([this] (Step& s) { s.cond = (uint8_t) (cond.getSelectedItemIndex()); }); };
    mode.onChange = [this] { write ([this] (Step& s) { s.retrigMode = (uint8_t) (mode.getSelectedItemIndex()); }); };
    on.onClick = [this] { write ([this] (Step& s) { s.on = on.getToggleState(); }); };
    accent.onClick = [this] { write ([this] (Step& s) { s.accent = accent.getToggleState(); }); };
    clearLocks.onClick = [this] { write ([] (Step& s) { while (s.numLocks > 0) s.clearLock (s.locks[0].param); }); load(); };
    addLock.onClick = [this]
    {
        PopupMenu m;
        struct Grp { const char* name; int from, to; };
        const Grp groups[] { { "Common", T_LEVEL, T_VELSENS }, { "Analog", T_A_LEVEL, T_A_DRIFT }, { "FM", T_B_LEVEL, T_B_MORPH }, { "Sample", T_C_LEVEL, T_C_LOOP },
                             { "Texture", T_D_LEVEL, T_D_TONE }, { "Envelopes", T_ENV_MODE, T_FENV_DECAY }, { "Filter", T_F_TYPE, T_F_MIX },
                             { "Inserts / Mix", T_DRV_TYPE, T_SEND4 } };
        const int role = (int) ctx.proc.trackValue (ctx.track(), T_ROLE);
        for (auto& gr : groups)
        {
            PopupMenu sub;
            for (int p = gr.from; p <= gr.to; ++p)
            {
                if (! trackParam (p).lockable) continue;
                String name = trackParam (p).name;
                if (p >= T_A_P1 && p <= T_A_P8) name = "Analog " + roleParamNames (role)[p - T_A_P1];
                sub.addItem (p + 1, name);
            }
            m.addSubMenu (gr.name, sub);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addLock), [this] (int r)
        {
            if (r <= 0) return;
            const int p = r - 1;
            const float v = ctx.proc.trackParamObj (ctx.track(), p)->getValue();
            write ([p, v] (Step& s) { s.on = true; s.setLock (p, v); });
            load();
        });
    };
    load();
}

void StepEditor::write (std::function<void (Step&)> fn)
{
    if (loading || ctx.selStep < 0) return;
    const int pat = jlimit (0, kNumPatterns - 1, (int) ctx.getParam ("g_pattern") - 1);
    const int t = ctx.track(), s = ctx.selStep;
    ctx.proc.editSeq ([&] (SeqData& d) { fn (d.pats[pat].tracks[t].steps[s]); }, true);
    if (onChanged) onChanged();
}

void StepEditor::load()
{
    loading = true;
    const bool has = ctx.selStep >= 0;
    for (Component* c : std::initializer_list<Component*> { &vel, &prob, &micro, &retrig, &len, &pitch, &condA, &condB, &cond, &mode, &on, &accent, &addLock, &clearLocks })
        c->setEnabled (has);
    lockSliders.clear();
    lockRemove.clear();
    lockNames.clear();
    if (! has)
    {
        vel.setValue (100); prob.setValue (100); micro.setValue (0); retrig.setValue (1); len.setValue (4); pitch.setValue (0);
        condA.setValue (1); condB.setValue (2); cond.setSelectedItemIndex (0, dontSendNotification); mode.setSelectedItemIndex (0, dontSendNotification);
    }
    if (has)
    {
        const auto d = ctx.proc.getSeqData();
        const int pat = jlimit (0, kNumPatterns - 1, (int) ctx.getParam ("g_pattern") - 1);
        const auto& st = d.pats[pat].tracks[ctx.track()].steps[ctx.selStep];
        vel.setValue (st.vel); prob.setValue (st.prob); micro.setValue (st.micro); retrig.setValue (st.retrig); len.setValue (st.len);
        pitch.setValue (st.pitch); condA.setValue (st.condA); condB.setValue (st.condB);
        cond.setSelectedItemIndex (st.cond, dontSendNotification);
        mode.setSelectedItemIndex (st.retrigMode, dontSendNotification);
        on.setToggleState (st.on, dontSendNotification);
        accent.setToggleState (st.accent, dontSendNotification);
        const int role = (int) ctx.proc.trackValue (ctx.track(), T_ROLE);
        for (int i = 0; i < st.numLocks; ++i)
        {
            const int p = st.locks[i].param;
            auto* sl = lockSliders.add (new GrabSlider (Slider::LinearHorizontal, Slider::NoTextBox));
            sl->setRange (0.0, 1.0);
            sl->setValue (st.locks[i].norm(), dontSendNotification);
            auto* param = ctx.proc.trackParamObj (ctx.track(), p);
            sl->textFromValueFunction = [param] (double v) { return param->getText ((float) v, 32); };
            sl->setPopupDisplayEnabled (true, true, this);
            sl->onValueChange = [this, sl, p] { const float v = (float) sl->getValue(); write ([p, v] (Step& s) { s.setLock (p, v); }); };
            addAndMakeVisible (sl);
            auto* x = lockRemove.add (new TextButton ("X"));
            x->onClick = [this, p] { write ([p] (Step& s) { s.clearLock (p); }); MessageManager::callAsync ([sp = Component::SafePointer<StepEditor> (this)] { if (sp != nullptr) sp->load(); }); };
            addAndMakeVisible (x);
            String name = trackParam (p).name;
            if (p >= T_A_P1 && p <= T_A_P8) name = roleParamNames (role)[p - T_A_P1];
            lockNames.add (name.toUpperCase());
        }
    }
    const bool ab = cond.getSelectedItemIndex() >= 11;
    condA.setEnabled (has && ab); condB.setEnabled (has && ab);
    loading = false;
    resized();
    repaint();
}

void StepEditor::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromTop (22);
    on.setBounds (row.removeFromLeft (56));
    accent.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (6);
    mode.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (6);
    cond.setBounds (row);
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (38), { &vel, &prob, &micro, &retrig }, 6);
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (38), { &len, &pitch, &condA, &condB }, 6);
    r.removeFromTop (6);
    auto lr = r.removeFromTop (22);
    addLock.setBounds (lr.removeFromLeft (90));
    lr.removeFromLeft (6);
    clearLocks.setBounds (lr.removeFromLeft (110));
    r.removeFromTop (4);
    for (int i = 0; i < lockSliders.size(); ++i)
    {
        auto l = r.removeFromTop (20);
        if (l.getHeight() < 20) break;
        lockRemove[i]->setBounds (l.removeFromRight (22));
        l.removeFromLeft (110);
        lockSliders[i]->setBounds (l.reduced (2, 2));
        r.removeFromTop (2);
    }
}

void StepEditor::paint (Graphics& g)
{
    if (ctx.selStep < 0)
    {
        g.setFont (Fonts::body (13.0f)); g.setColour (C::ash);
        g.drawFittedText (::racoon::i18n::tr ("Right-click (or alt-click) a step to edit it: velocity, trig condition, probability, micro timing, ratchets and parameter locks."),
                          getLocalBounds().withTrimmedTop (150), Justification::centredTop, 3);
        return;
    }
    g.setFont (Fonts::label (11.5f));
    g.setColour (C::boneDim);
    for (int i = 0; i < lockSliders.size(); ++i)
        g.drawFittedText (::racoon::i18n::tr (lockNames[i]), Rectangle<int> (0, lockSliders[i]->getY() - 2, 108, 22), Justification::centredLeft, 1, 0.6f);
}

// ===================================================================== seq page
SeqPage::SeqPage (Ctx& c) : TrackPage (c), grid (c), stepEditor (c), midiDrag (c, false), audioDrag (c, true)
{
    for (Component* comp : std::initializer_list<Component*> { &topPanel, &gridPanel, &stepPanel, &trackPanel, &toolPanel, &songPanel })
        addAndMakeVisible (comp), comp->toBack();
    selector.setVisible (false);   // the grid itself selects tracks
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto* b = patBtns.add (new TextButton (String::charToString ((juce_wchar) kPatNames[p])));
        b->onClick = [this, p] { ctx.setParam ("g_pattern", (float) (p + 1)); grid.repaint(); stepEditor.load(); refreshTrackSettings(); };
        addAndMakeVisible (b);
    }
    for (int p = 0; p < 8; ++p)
    {
        auto* b = pageBtns.add (new TextButton (String (p * 16 + 1) + "-" + String (p * 16 + 16)));
        b->onClick = [this, p] { grid.page = p; grid.repaint(); repaint(); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &copyPat, &pastePat, &clearPat, &undoSeq, &redoSeq }) addAndMakeVisible (b);
    copyPat.onClick = [this] { clipboard = ctx.proc.getSeqData().pats[grid.pattern()]; hasClip = true; };
    pastePat.onClick = [this] { if (hasClip) { const int p = grid.pattern(); ctx.proc.editSeq ([&] (SeqData& d) { d.pats[p] = clipboard; }); grid.reload(); } };
    clearPat.onClick = [this] { const int p = grid.pattern(); ctx.proc.editSeq ([&] (SeqData& d) { auto name = d.pats[p].name; d.pats[p] = Pattern(); d.pats[p].name = name; }); grid.reload(); };
    undoSeq.onClick = [this] { ctx.proc.undoSeq(); grid.reload(); stepEditor.load(); };
    redoSeq.onClick = [this] { ctx.proc.redoSeq(); grid.reload(); stepEditor.load(); };

    playBtn = std::make_unique<ParamButton> (ctx, "g_play", "PLAY", false, C::jade);
    recBtn = std::make_unique<ParamButton> (ctx, "g_record", "REC", false, C::ochre);
    songMode = std::make_unique<ParamButton> (ctx, "g_songmode", "SONG");
    fillBtn = std::make_unique<ParamButton> (ctx, "g_fill", "FILL", true, C::cyan);
    breakBtn = std::make_unique<ParamButton> (ctx, "g_break", "BREAK", true, C::cyan);
    fillEvery = std::make_unique<ChoiceBox> (ctx, "g_fillevery", "AUTO FILL");
    scatter = std::make_unique<ChoiceBox> (ctx, "g_scatter", "SCATTER");
    noteRepeat = std::make_unique<ChoiceBox> (ctx, "g_noterepeat", "NOTE REPEAT");
    swing = std::make_unique<ParamKnob> (ctx, "g_swing", "SWING");
    grooveAmt = std::make_unique<ParamKnob> (ctx, "g_groove", "GROOVE");
    humanize = std::make_unique<ParamKnob> (ctx, "g_humanize", "HUMANIZE");
    morph = std::make_unique<ParamKnob> (ctx, "g_morph", "MORPH");
    bpm = std::make_unique<ParamKnob> (ctx, "g_bpm", "TEMPO");
    morphTarget = std::make_unique<Stepper> (ctx, "g_morphtarget", "TO", [] (int v) { return String::charToString ((juce_wchar) kPatNames[jlimit (1, 8, v) - 1]); });
    for (Component* comp : std::initializer_list<Component*> { playBtn.get(), recBtn.get(), songMode.get(), fillBtn.get(), breakBtn.get(), fillEvery.get(), scatter.get(),
                                                               noteRepeat.get(), swing.get(), grooveAmt.get(), humanize.get(), morph.get(), bpm.get(), morphTarget.get() })
        addAndMakeVisible (comp);
    bpm->knob.setTooltip ("Internal tempo, used when the host is stopped or in the standalone app");

    addAndMakeVisible (grid);
    addAndMakeVisible (stepEditor);
    grid.onSelect = [this] { stepEditor.load(); refreshTrackSettings(); };
    stepEditor.onChanged = [this] { grid.reload(); };

    // track settings
    for (Component* comp : std::initializer_list<Component*> { &trkLen, &masterLen, &trkRes, &shiftL, &shiftR, &reverseBtn, &clearTrk, &humanBtn })
        addAndMakeVisible (comp);
    trkRes.addItemList (seqResNames(), 1);
    trkLen.onChange = [this] (int v) { editTrack ([v] (TrackPattern& t) { t.length = v; }); };
    masterLen.onChange = [this] (int v) { const int p = grid.pattern(); ctx.proc.editSeq ([&] (SeqData& d) { d.pats[p].masterLength = v; }); };
    masterLen.setTooltip ("Pattern length in 1/16 steps: sets when patterns switch and chain");
    trkRes.onChange = [this] { const int v = trkRes.getSelectedItemIndex(); editTrack ([v] (TrackPattern& t) { t.res = v; }); };
    shiftL.onClick = [this] { editTrack ([] (TrackPattern& t) { PatternTools::shift (t, -1); }); };
    shiftR.onClick = [this] { editTrack ([] (TrackPattern& t) { PatternTools::shift (t, 1); }); };
    reverseBtn.onClick = [this] { editTrack ([] (TrackPattern& t) { PatternTools::reverse (t); }); };
    clearTrk.onClick = [this] { editTrack ([] (TrackPattern& t) { PatternTools::clear (t); }); };
    humanBtn.onClick = [this] { Random rng; editTrack ([&rng] (TrackPattern& t) { PatternTools::humanize (t, 0.5f, rng); }); };

    // generators
    for (Component* comp : std::initializer_list<Component*> { &eHits, &eSteps, &eRot, &euclidBtn, &randBtn, &varBtn, &fillGenBtn, &density, &intensity, &flavour, &varTarget })
        addAndMakeVisible (comp);
    eHits.setValue (5); eSteps.setValue (16); eRot.setValue (0);
    euclidBtn.onClick = [this] { const int h = eHits.getValue(), s = eSteps.getValue(), ro = eRot.getValue(); editTrack ([=] (TrackPattern& t) { PatternTools::euclid (t, h, s, ro); }); };
    density.setSliderStyle (Slider::LinearHorizontal); density.setTextBoxStyle (Slider::NoTextBox, true, 0, 0); density.setRange (0.0, 1.0); density.setValue (0.5);
    density.setTooltip ("Cuántos golpes pone ALEATORIA en el sonido elegido."_u); randBtn.setTooltip ("ALEATORIA: un patrón nuevo para el sonido elegido.");
    intensity.setSliderStyle (Slider::LinearHorizontal); intensity.setTextBoxStyle (Slider::NoTextBox, true, 0, 0); intensity.setRange (0.0, 1.0); intensity.setValue (0.6);
    intensity.setTooltip ("Cuánto cambia ALEATORIA el patrón, y qué tan cargado sale CREAR FILL."_u); varBtn.setTooltip ("ALEATORIA: todo el patrón, cambiado, en el patrón que eliges al lado (A a H)."_u);
    randBtn.onClick = [this]
    {
        Random rng;
        const int role = (int) ctx.proc.trackValue (ctx.track(), T_ROLE);
        const float d = (float) density.getValue();
        editTrack ([&] (TrackPattern& t) { PatternTools::randomize (t, role, d, 1.0f, rng); });
    };
    flavour.addItemList ({ "Subtle", "Ghost Groove", "Busy", "Sparse", "Shuffle", "Evolving" }, 1);
    flavour.setSelectedItemIndex (1, dontSendNotification);
    for (int p = 0; p < kNumPatterns; ++p) varTarget.addItem (String ("INTO ") + kPatNames[p], p + 1);
    varTarget.setSelectedId (2, dontSendNotification);
    varBtn.onClick = [this]
    {
        const int src = grid.pattern(), dst = varTarget.getSelectedId() - 1;
        int roles[kNumTracks];
        for (int t = 0; t < kNumTracks; ++t) roles[t] = (int) ctx.proc.trackValue (t, T_ROLE);
        Random rng;
        const float in = (float) intensity.getValue();
        const int fl = flavour.getSelectedItemIndex();
        ctx.proc.editSeq ([&] (SeqData& d) { Pattern out; PatternTools::variation (d.pats[src], out, roles, in, fl, rng); d.pats[dst] = out; });
        grid.reload();
    };
    fillGenBtn.onClick = [this]
    {
        const int p = grid.pattern();
        int roles[kNumTracks];
        for (int t = 0; t < kNumTracks; ++t) roles[t] = (int) ctx.proc.trackValue (t, T_ROLE);
        Random rng;
        const float in = (float) intensity.getValue();
        ctx.proc.editSeq ([&] (SeqData& d) { PatternTools::makeFill (d.pats[p], roles, in, rng); });
        grid.reload();
    };

    // song
    for (Component* comp : std::initializer_list<Component*> { &songAdd, &songDel, &songClear, &songText, &midiDrag, &audioDrag, &exportBars, &soloExport })
        addAndMakeVisible (comp);
    songText.setFont (Fonts::mono (12.0f));
    songText.setColour (Label::textColourId, C::bone);
    songText.setJustificationType (Justification::topLeft);
    songAdd.onClick = [this]
    {
        const int p = grid.pattern();
        ctx.proc.editSeq ([&] (SeqData& d)
        {
            if (d.songLength > 0 && d.song[d.songLength - 1].pattern == p) d.song[d.songLength - 1].repeats++;
            else if (d.songLength < SeqData::kMaxSong) { d.song[d.songLength].pattern = p; d.song[d.songLength].repeats = 1; d.songLength++; }
        });
        refreshSong();
    };
    songDel.onClick = [this] { ctx.proc.editSeq ([] (SeqData& d) { if (d.songLength > 0) { if (d.song[d.songLength - 1].repeats > 1) d.song[d.songLength - 1].repeats--; else d.songLength--; } }); refreshSong(); };
    songClear.onClick = [this] { ctx.proc.editSeq ([] (SeqData& d) { d.songLength = 0; }); refreshSong(); };
    exportBars.setValue (4);
    midiDrag.bars = [this] { return exportBars.getValue(); };
    audioDrag.bars = [this] { return exportBars.getValue(); };
    audioDrag.solo = [this] { return soloExport.getToggleState() ? ctx.track() : -1; };
    ctx.registerAnimated (this);
    refreshTrackSettings();
    refreshSong();
}

SeqPage::~SeqPage() { ctx.unregisterAnimated (this); }

void SeqPage::rebuild() { builtFor = ctx.track(); refreshTrackSettings(); stepEditor.load(); grid.repaint(); }

void SeqPage::editTrack (std::function<void (TrackPattern&)> fn)
{
    const int p = grid.pattern(), t = ctx.track();
    ctx.proc.editSeq ([&] (SeqData& d) { fn (d.pats[p].tracks[t]); });
    grid.reload();
    refreshTrackSettings();
}

void SeqPage::refreshTrackSettings()
{
    const auto d = ctx.proc.getSeqData();
    const auto& tp = d.pats[grid.pattern()].tracks[ctx.track()];
    trkLen.setValue (tp.length);
    trkRes.setSelectedItemIndex (tp.res, dontSendNotification);
    masterLen.setValue (d.pats[grid.pattern()].masterLength);
    trackPanel.setTitle ("TRACK " + String (ctx.track() + 1) + "  /  " + ctx.proc.getTrackName (ctx.track()).toUpperCase());
}

void SeqPage::refreshSong()
{
    const auto d = ctx.proc.getSeqData();
    String s;
    for (int i = 0; i < d.songLength; ++i) s << kPatNames[d.song[i].pattern] << "x" << d.song[i].repeats << (i < d.songLength - 1 ? "  " : "");
    songText.setText (s.isEmpty() ? String ("empty: add patterns, then enable SONG") : s, dontSendNotification);
}

void SeqPage::animate()
{
    if (! animVisible (this)) return;
    const int ap = ctx.proc.engine.telemetry.activePattern.load(), q = ctx.proc.engine.telemetry.queuedPattern.load();
    if (ap != lastPattern || q != lastQueued)
    {
        lastPattern = ap; lastQueued = q;
        repaint();
    }
    static int lastEdit = -1;
    const int edit = grid.pattern();
    if (edit != lastEdit) { lastEdit = edit; grid.reload(); refreshTrackSettings(); }
}

void SeqPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (102);
    topPanel.setBounds (top);
    auto tc = topPanel.content() + top.getPosition();
    auto pats = tc.removeFromLeft (8 * 40);
    auto prow = pats.removeFromTop (34);
    for (int p = 0; p < kNumPatterns; ++p) patBtns[p]->setBounds (prow.getX() + p * 40, prow.getY(), 38, 34);
    pats.removeFromTop (6);
    layoutRow (pats.removeFromTop (24), { &copyPat, &pastePat, &clearPat, &undoSeq, &redoSeq }, 4);
    tc.removeFromLeft (12);
    auto btns = tc.removeFromLeft (190);
    layoutRow (btns.removeFromTop (30), { playBtn.get(), recBtn.get(), songMode.get() }, 4);
    btns.removeFromTop (6);
    layoutRow (btns.removeFromTop (30), { fillBtn.get(), breakBtn.get() }, 4);
    tc.removeFromLeft (12);
    auto choices = tc.removeFromLeft (140);
    fillEvery->setBounds (choices.removeFromTop (36));
    choices.removeFromTop (2);
    scatter->setBounds (choices.removeFromTop (36));
    tc.removeFromLeft (8);
    auto ch2 = tc.removeFromLeft (130);
    noteRepeat->setBounds (ch2.removeFromTop (36));
    ch2.removeFromTop (2);
    morphTarget->setBounds (ch2.removeFromTop (36));
    tc.removeFromLeft (8);
    layoutRow (tc, { swing.get(), grooveAmt.get(), humanize.get(), morph.get(), bpm.get() });

    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (232);
    gridPanel.setBounds (r);
    auto gc = gridPanel.content() + r.getPosition();
    auto pr = gridPanel.getBounds().withHeight (22).withTrimmedLeft (90).withTrimmedRight (12).translated (0, 2);
    pr = pr.removeFromLeft (8 * 64);
    for (int p = 0; p < 8; ++p) pageBtns[p]->setBounds (pr.getX() + p * 64, pr.getY(), 62, 18);
    gc.removeFromTop (12);
    beatRow = gc.removeFromTop (12).withTrimmedLeft (kNameW);
    grid.setBounds (gc);

    bottom.removeFromTop (8);
    auto sArea = bottom.removeFromLeft (450);
    stepPanel.setBounds (sArea);
    stepEditor.setBounds (stepPanel.content() + sArea.getPosition());
    bottom.removeFromLeft (8);
    auto tArea = bottom.removeFromLeft (270);
    trackPanel.setBounds (tArea);
    auto tcc = trackPanel.content() + tArea.getPosition();
    auto t1 = tcc.removeFromTop (38);
    trkLen.setBounds (t1.removeFromLeft (120));
    t1.removeFromLeft (8);
    trkRes.setBounds (t1.withTrimmedTop (14));
    tcc.removeFromTop (6);
    masterLen.setBounds (tcc.removeFromTop (38).removeFromLeft (120));
    tcc.removeFromTop (8);
    layoutRow (tcc.removeFromTop (24), { &shiftL, &shiftR, &reverseBtn }, 4);
    tcc.removeFromTop (4);
    layoutRow (tcc.removeFromTop (24), { &clearTrk, &humanBtn }, 4);
    bottom.removeFromLeft (8);
    auto gArea = bottom.removeFromLeft (330);
    toolPanel.setBounds (gArea);
    auto gcc = toolPanel.content() + gArea.getPosition();
    auto g1 = gcc.removeFromTop (38);
    layoutRow (g1.removeFromLeft (230), { &eHits, &eSteps, &eRot }, 4);
    g1.removeFromLeft (6);
    euclidBtn.setBounds (g1.withTrimmedTop (14));
    gcc.removeFromTop (8);
    auto g2 = gcc.removeFromTop (24);
    randBtn.setBounds (g2.removeFromRight (90));
    density.setBounds (g2.reduced (0, 2));
    gcc.removeFromTop (8);
    auto g3 = gcc.removeFromTop (24);
    flavour.setBounds (g3.removeFromLeft (130));
    g3.removeFromLeft (4);
    varTarget.setBounds (g3.removeFromLeft (86));
    g3.removeFromLeft (4);
    varBtn.setBounds (g3);
    gcc.removeFromTop (6);
    auto g4 = gcc.removeFromTop (24);
    fillGenBtn.setBounds (g4.removeFromRight (90));
    intensity.setBounds (g4.reduced (0, 2));
    bottom.removeFromLeft (8);
    songPanel.setBounds (bottom);
    auto scc = songPanel.content() + bottom.getPosition();
    layoutRow (scc.removeFromTop (24), { &songAdd, &songDel, &songClear }, 4);
    scc.removeFromTop (4);
    songText.setBounds (scc.removeFromTop (54));
    scc.removeFromTop (4);
    auto e1 = scc.removeFromTop (38);
    exportBars.setBounds (e1.removeFromLeft (100));
    e1.removeFromLeft (8);
    soloExport.setBounds (e1.withTrimmedTop (14));
    scc.removeFromTop (6);
    layoutRow (scc.removeFromTop (30), { &midiDrag, &audioDrag }, 6);
}

void SeqPage::paintOverChildren (Graphics& g)
{
    const int ap = ctx.proc.engine.telemetry.activePattern.load(), q = ctx.proc.engine.telemetry.queuedPattern.load();
    const int edit = grid.pattern();
    const bool playing = ctx.proc.engine.telemetry.playing.load();
    const auto d = ctx.proc.getSeqData();
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto r = patBtns[p]->getBounds().toFloat();
        if (p == edit) { g.setColour (C::amber); g.drawRect (r, 1.6f); }
        if (playing && p == ap) Draw::glowLine (g, { r.getX() + 4.0f, r.getBottom() - 3.0f }, { r.getRight() - 4.0f, r.getBottom() - 3.0f }, C::cyan, 1.4f);
        if (p == q) { g.setColour (C::cyan.withAlpha (0.8f)); g.drawRect (r.reduced (3.0f), 1.0f); }
        if (d.pats[p].isEmpty()) { g.setColour (C::void_.withAlpha (0.45f)); g.fillRect (r.reduced (2.0f)); }
    }
    for (int p = 0; p < 8; ++p)
        if (p == grid.page) { g.setColour (C::amber); g.drawRect (pageBtns[p]->getBounds().toFloat(), 1.4f); }
    // dim pages beyond the longest track
    int maxLen = 0;
    for (auto& t : d.pats[edit].tracks) maxLen = std::max (maxLen, t.length);
    for (int p = 0; p < 8; ++p)
        if (p * 16 >= maxLen) { g.setColour (C::void_.withAlpha (0.5f)); g.fillRect (pageBtns[p]->getBounds()); }
    g.setFont (Fonts::body (12.0f)); g.setColour (C::ash);
    auto gb = gridPanel.getBounds();
    g.drawText (::racoon::i18n::tr ("click: golpe   shift: acento   click derecho: editar   cmd + arrastrar: velocity   nombre: escuchar / M / S"), gb.reduced (12, 4), Justification::topRight);
    // step numbers above the grid
    g.setFont (Fonts::mono (9.5f));
    const float cw = (float) beatRow.getWidth() / 16.0f;
    for (int c = 0; c < 16; ++c)
    {
        g.setColour (c % 4 == 0 ? C::boneDim : C::ash);
        g.drawText (::racoon::i18n::tr (String (grid.page * 16 + c + 1)), Rectangle<float> ((float) beatRow.getX() + (float) c * cw, (float) beatRow.getY(), cw, (float) beatRow.getHeight()), Justification::centred);
    }
}
} // namespace be::ui

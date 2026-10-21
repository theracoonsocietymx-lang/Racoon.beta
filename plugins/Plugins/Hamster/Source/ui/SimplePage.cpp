#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== beat grid (16 x 16)
BeatGrid::BeatGrid (Ctx& c) : ctx (c)
{
    setTooltip ("Doble clic en un cuadro pone o quita un golpe. Clic en un golpe lo selecciona (Mayús suma) y arrastrarlo lo mueve; "
                "arrastrar en un hueco selecciona varios; Alt+arrastrar copia. Supr borra, Cmd+C, Cmd+V y Cmd+D copian, pegan y duplican, "
                "las flechas mueven. Tecla B: lápiz (clic pone, arrastrar pinta). Clic en el nombre escucha el sonido y lo elige."_u);
    setWantsKeyboardFocus (true);
    editor.hit = [this] (Point<float> p, int& t, int& s)
    {
        t = jlimit (0, kNumTracks - 1, (int) (p.y / ((float) getHeight() / (float) kNumTracks)));
        s = p.x < (float) kLabelW ? -1 : jlimit (0, 15, (int) ((p.x - (float) kLabelW) / (((float) getWidth() - (float) kLabelW) / 16.0f)));
        return true;
    };
    editor.cellRect = [this] (int t, int s) { return cell (t, s).reduced (2.0f, 1.5f); };
    editor.visibleSteps = [] { return Range<int> (0, 16); };
    editor.getData = [this] { return ctx.proc.getSeqData(); };
    editor.setData = [this] (const SeqData& d, bool undoable)
    {
        ctx.proc.setSeqData (d, undoable);
        data = d;
        seen = ctx.proc.seqVersion.load();
    };
    editor.pattern = [this] { return jlimit (0, kNumPatterns - 1, (int) ctx.getParam ("g_pattern") - 1); };
    editor.onChoose = [this] (int t, int) { ctx.selectTrack (t); };
    editor.onHear = [this] (int t) { ctx.proc.padDown (t, 0.8f); ctx.proc.padUp (t); };
    editor.repaint = [this] { repaint(); };
    ctx.registerAnimated (this);
}

BeatGrid::~BeatGrid() { ctx.unregisterAnimated (this); }

Rectangle<float> BeatGrid::cell (int t, int s) const
{
    const float w = ((float) getWidth() - (float) kLabelW) / 16.0f;
    const float h = (float) getHeight() / (float) kNumTracks;
    return { (float) kLabelW + (float) s * w, (float) t * h, w, h };
}

void BeatGrid::animate()
{
    if (! animVisible (this)) return;
    bool dirty = false;
    const int pat = (int) ctx.getParam ("g_pattern") - 1;
    if (ctx.proc.seqVersion.load() != seen || pat != pattern) { seen = ctx.proc.seqVersion.load(); pattern = pat; data = ctx.proc.getSeqData(); dirty = true; }
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int st = ctx.proc.engine.telemetry.playing.load() ? ctx.proc.engine.telemetry.trackStep[t].load() : -1;
        if (st != playStep[t]) { playStep[t] = st; dirty = true; }
    }
    if (dirty) repaint();
}

void BeatGrid::paint (Graphics& g)
{
    if (seen < 0) { seen = ctx.proc.seqVersion.load(); pattern = (int) ctx.getParam ("g_pattern") - 1; data = ctx.proc.getSeqData(); }
    const auto& pat = data.pats[jlimit (0, kNumPatterns - 1, pattern)];
    auto screen = getLocalBounds().toFloat().withTrimmedLeft ((float) kLabelW);
    g.setColour (C::void_);
    g.fillRect (screen);
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = pat.tracks[t];
        const bool sel = t == ctx.track();
        const bool muted = ctx.proc.trackValue (t, T_MUTE) > 0.5f;
        auto lab = Rectangle<float> (0.0f, cell (t, 0).getY(), (float) kLabelW - 6.0f, cell (t, 0).getHeight()).reduced (0.0f, 1.0f);
        g.setColour (sel ? S::hi : (t % 2 ? S::card : S::cardDim));
        g.fillRect (lab);
        g.setColour (S::ink);
        if (sel) g.drawRect (lab, 1.5f);
        g.setFont (Fonts::label (12.0f));
        g.setColour (muted ? S::inkFaint : S::ink);
        g.drawFittedText (::racoon::i18n::tr (String (t + 1) + "  " + ctx.proc.getTrackName (t).toUpperCase()), lab.reduced (6.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1, 0.6f);
        for (int s = 0; s < 16; ++s)
        {
            auto r = cell (t, s).reduced (2.0f, 1.5f);
            const bool inLen = s < tp.length;
            const auto& st = tp.steps[s];
            Colour c = s / 4 % 2 ? C::stoneHi : C::stone;
            if (! inLen) c = C::basalt;
            if (inLen && st.on) c = st.accent ? S::accent() : C::amber;
            if (inLen && st.on && muted) c = c.withAlpha (0.4f);
            g.setColour (c);
            g.fillRect (r);
            if (inLen && st.on && st.vel < 100) { g.setColour (C::void_.withAlpha (0.25f)); g.fillRect (r.withTrimmedTop (r.getHeight() * (float) st.vel / 127.0f)); }
            if (playStep[t] >= 0 && playStep[t] % 16 == s && playStep[t] < 16)
            {
                g.setColour (C::cyan.withAlpha (st.on ? 0.9f : 0.35f));
                g.drawRect (r, 2.0f);
            }
        }
        if (tp.length > 16)
        {
            g.setColour (C::bone.withAlpha (0.6f));
            g.setFont (Fonts::label (10.0f));
            g.drawText (::racoon::i18n::tr ("+" + String (tp.length - 16)), cell (t, 15).withTrimmedLeft (cell (t, 15).getWidth() * 0.4f), Justification::centredRight);
        }
    }
    g.setColour (S::ink);
    g.drawRect (screen, 2.0f);
    editor.paintOverlay (g, C::cyan);
    if (editor.drawMode)
    {
        g.setColour (C::cyan);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("LÁPIZ (B)")), screen.removeFromTop (14.0f).reduced (4.0f, 0.0f), Justification::topRight);
    }
}

void BeatGrid::setStep (int t, int s, bool on)
{
    const int pat = jlimit (0, kNumPatterns - 1, pattern);
    ctx.proc.editSeq ([&] (SeqData& d)
    {
        auto& st = d.pats[pat].tracks[t].steps[s];
        st.on = on;
        if (on && st.vel == 0) st.vel = 100;
    }, dragTrack < 0);
    data.pats[pat].tracks[t].steps[s].on = on;
    seen = ctx.proc.seqVersion.load();
    repaint();
}

void BeatGrid::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const int t = jlimit (0, kNumTracks - 1, (int) (e.position.y / ((float) getHeight() / (float) kNumTracks)));
    if (e.position.x < (float) kLabelW)
    {
        ctx.selectTrack (t);
        ctx.proc.padDown (t, 0.85f);
        ctx.proc.padUp (t);
        repaint();
        return;
    }
    if (seen < 0) data = ctx.proc.getSeqData();
    pattern = (int) ctx.getParam ("g_pattern") - 1;
    editor.mouseDown (e);
}

void BeatGrid::mouseDrag (const MouseEvent& e) { editor.mouseDrag (e); }
void BeatGrid::mouseUp (const MouseEvent& e) { editor.mouseUp (e); }
void BeatGrid::mouseDoubleClick (const MouseEvent& e) { editor.mouseDoubleClick (e); }
bool BeatGrid::keyPressed (const KeyPress& k) { return editor.keyPressed (k); }

// ===================================================================== big play
BigPlay::BigPlay (Ctx& c) : ctx (c)
{
    setTooltip ("Arranca y detiene el ritmo. Si tu DAW está tocando, Hamster lo sigue solo. Barra espaciadora hace lo mismo."_u);
    setMouseCursor (MouseCursor::PointingHandCursor);
    ctx.registerAnimated (this);
}

BigPlay::~BigPlay() { ctx.unregisterAnimated (this); }

void BigPlay::animate()
{
    const bool on = ctx.getParam ("g_play") > 0.5f;
    if (on != last) { last = on; repaint(); }
}

void BigPlay::mouseDown (const MouseEvent&)
{
    if (auto* p = ctx.apvts.getParameter ("g_play")) p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void BigPlay::paint (Graphics& g)
{
    const bool on = ctx.getParam ("g_play") > 0.5f;
    auto r = getLocalBounds().toFloat();
    Draw::box (g, r, on ? S::accent() : (isMouseOver() ? Colours::white : S::hi), 2.0f, 5.0f);
    auto body = r.withTrimmedRight (5.0f).withTrimmedBottom (5.0f);
    const float ih = body.getHeight() * 0.38f;
    auto icon = body.removeFromLeft (body.getHeight()).withSizeKeepingCentre (ih, ih);
    g.setColour (S::ink);
    if (on) g.fillRect (icon.reduced (ih * 0.06f));
    else { Path t; t.addTriangle (icon.getX(), icon.getY(), icon.getX(), icon.getBottom(), icon.getRight(), icon.getCentreY()); g.fillPath (t); }
    g.setFont (Fonts::title (std::min (22.0f, body.getHeight() * 0.42f)));
    g.drawFittedText (::racoon::i18n::tr (on ? "DETENER" : "TOCAR"), body.withTrimmedRight (8.0f).toNearestInt(), Justification::centredLeft, 1, 0.7f);
}

// ===================================================================== simple page
SimplePage::SimplePage (Ctx& c)
    : ctx (c), grid (c), play (c), tempo (c, "g_bpm", "TEMPO"), swing (c, "g_swing", "SWING")
{
    for (Component* comp : std::initializer_list<Component*> { &beatPanel, &soundPanel, &playPanel, &kitPanel, &helpPanel, &grid, &play, &tempo, &swing,
                                                               &variation, &clear, &prevBtn, &nextBtn, &editBtn, &genre, &list })
        addAndMakeVisible (comp);
    fill = std::make_unique<ParamButton> (c, "g_fill", "FILL", true, C::amber);
    fill->setTooltip ("Sostén para tocar un fill. Suelta y regresa el patrón."_u);
    addAndMakeVisible (*fill);
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = patternBtns.add (new TextButton (String::charToString ((juce_wchar) ('A' + i))));
        b->setTooltip ("Patrón "_u + String::charToString ((juce_wchar) ('A' + i)) + ". Cambia al terminar el compás. Cada kit trae 8."_u);
        b->onClick = [this, i] { ctx.setParam ("g_pattern", (float) (i + 1)); };
        addAndMakeVisible (b);
    }
    variation.setTooltip ("Inventa una variación del patrón actual respetando el papel de cada sonido. Si no te gusta, DESHACER."_u);
    variation.onClick = [this]
    {
        const int p = (int) ctx.getParam ("g_pattern") - 1;
        int roles[kNumTracks];
        for (int t = 0; t < kNumTracks; ++t) roles[t] = (int) ctx.proc.trackValue (t, T_ROLE);
        Random rng;
        ctx.proc.editSeq ([&] (SeqData& d) { Pattern out; PatternTools::variation (d.pats[p], out, roles, 0.5f, 0, rng); d.pats[p] = out; });
    };
    clear.setTooltip ("Borra todos los golpes del patrón actual."_u);
    clear.onClick = [this]
    {
        const int p = (int) ctx.getParam ("g_pattern") - 1;
        ctx.proc.editSeq ([&] (SeqData& d) { for (auto& t : d.pats[p].tracks) PatternTools::clear (t); });
    };
    editBtn.setTooltip ("Abre la página KIT con todos los controles de este sonido."_u);
    editBtn.onClick = [this] { if (ctx.showPage) ctx.showPage (1); };
    prevBtn.onClick = [this] { ctx.proc.presets().step (-1); };
    nextBtn.onClick = [this] { ctx.proc.presets().step (1); };
    prevBtn.setTooltip ("Kit anterior"); nextBtn.setTooltip ("Kit siguiente");
    genre.addItem ("Todos los géneros"_u, 1);
    for (int i = 0; i < genreNames().size(); ++i) genre.addItem (genreNames()[i], i + 2);
    genre.setSelectedId (1, dontSendNotification);
    genre.onChange = [this] { fillList(); };
    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (ListBox::backgroundColourId, S::card);
    ctx.onTrackChanged ([this] { buildTrackKnobs(); resized(); repaint(); });
    ctx.registerAnimated (this);
    buildTrackKnobs();
    refresh();
}

SimplePage::~SimplePage() { ctx.unregisterAnimated (this); list.setModel (nullptr); }

void SimplePage::buildTrackKnobs()
{
    const int t = ctx.track();
    if (t == builtFor && level != nullptr) return;
    builtFor = t;
    level = std::make_unique<ParamKnob> (ctx, trackParamId (t, T_LEVEL), "VOLUMEN");
    tune = std::make_unique<ParamKnob> (ctx, trackParamId (t, T_TUNE), "AFINACIÓN"_u);
    decay = std::make_unique<ParamKnob> (ctx, trackParamId (t, T_DECAY), "LARGO");
    tone = std::make_unique<ParamKnob> (ctx, trackParamId (t, T_F_CUTOFF), "TONO");
    for (auto* k : { level.get(), tune.get(), decay.get(), tone.get() }) addAndMakeVisible (k);
}

void SimplePage::refresh()
{
    fillList();
    buildTrackKnobs();
    repaint();
}

void SimplePage::animate()
{
    if (! animVisible (this)) return;
    const int p = (int) ctx.getParam ("g_pattern") - 1;
    for (int i = 0; i < patternBtns.size(); ++i)
        if (patternBtns[i]->getToggleState() != (i == p)) patternBtns[i]->setToggleState (i == p, dontSendNotification);
    if (lastPreset != ctx.proc.currentPresetName) { lastPreset = ctx.proc.currentPresetName; list.repaint(); repaint(); }
}

void SimplePage::fillList()
{
    const int gi = genre.getSelectedId() - 2;
    shown.clear();
    for (auto& p : ctx.proc.presets().list())
        if (gi < 0 || p.genre == genreNames()[gi]) shown.push_back (p);
    list.updateContent();
    list.repaint();
}

void SimplePage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row];
    const bool current = p.name == ctx.proc.currentPresetName;
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f, 1.0f);
    if (current) { g.setColour (S::hi); g.fillRect (r); }
    else if (selected) { g.setColour (S::cardDim); g.fillRect (r); }
    g.setColour (S::ink.withAlpha (0.12f));
    g.drawHorizontalLine (h - 1, 4.0f, (float) w - 4.0f);
    g.setColour (S::ink);
    g.setFont (current ? Fonts::bodyBold (15.0f) : Fonts::body (15.0f));
    g.drawText (::racoon::i18n::tr (p.name), r.withTrimmedLeft (8.0f).withTrimmedRight (110.0f), Justification::centredLeft, true);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::label (11.0f));
    g.drawText (::racoon::i18n::tr (p.genre.toUpperCase() + "  " + String (p.bpm)), r.withTrimmedRight (8.0f), Justification::centredRight, true);
}

void SimplePage::listBoxItemClicked (int row, const MouseEvent&)
{
    if (row < 0 || row >= (int) shown.size()) return;
    ctx.proc.presets().load (shown[(size_t) row]);
    list.repaint();
}

void SimplePage::paintOverChildren (Graphics& g)
{
    // selected sound name
    {
        auto r = soundPanel.getBounds().toFloat().withTrimmedRight (4.0f).withTrimmedBottom (4.0f).reduced (14.0f, 8.0f).withTrimmedTop (22.0f);
        g.setColour (S::ink);
        g.setFont (Fonts::title (20.0f));
        g.drawText (::racoon::i18n::tr (String (ctx.track() + 1) + "  " + ctx.proc.getTrackName (ctx.track())), r.removeFromTop (28.0f), Justification::centredLeft);
    }
    auto r = helpPanel.getBounds().toFloat().withTrimmedRight (4.0f).withTrimmedBottom (4.0f).reduced (16.0f, 12.0f).withTrimmedTop (24.0f);
    const String steps[][2] {
        { "1", "Elige un kit de la lista. Cada kit trae 8 patrones ya hechos." },
        { "2", "Dale TOCAR. Doble clic en un cuadro pone o quita un golpe; clic lo selecciona y arrastrarlo lo mueve."_u },
        { "3", "Click en el nombre de un sonido para elegirlo y moverle sus perillas."_u },
        { "4", "En PATRÓN arrastra DRAG MIDI o DRAG AUDIO a tu DAW."_u },
    };
    const float rowH = r.getHeight() / 4.0f;
    for (auto& st : steps)
    {
        auto row = r.removeFromTop (rowH);
        auto num = row.removeFromLeft (30.0f).withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (S::accent()); g.fillEllipse (num);
        g.setColour (S::ink); g.drawEllipse (num, 1.5f);
        g.setFont (Fonts::title (14.0f)); g.drawText (::racoon::i18n::tr (st[0]), num, Justification::centred);
        g.setFont (Fonts::body (13.5f));
        g.drawFittedText (::racoon::i18n::tr (st[1]), row.withTrimmedLeft (10.0f).toNearestInt(), Justification::centredLeft, 3, 0.9f);
    }
}

void SimplePage::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto right = r.removeFromRight (400);
    r.removeFromRight (12);
    // left: beat grid on top, play strip below
    auto bottom = r.removeFromBottom (150);
    r.removeFromBottom (12);
    beatPanel.setBounds (r);
    grid.setBounds ((beatPanel.content() + r.getPosition()).withTrimmedRight (4).withTrimmedBottom (4));

    auto help = bottom.removeFromRight (330);
    bottom.removeFromRight (12);
    playPanel.setBounds (bottom);
    auto pc = (playPanel.content() + bottom.getPosition()).withTrimmedRight (4).withTrimmedBottom (4);
    play.setBounds (pc.removeFromLeft (190).withTrimmedRight (12).withSizeKeepingCentre (178, 64));
    auto knobs = pc.removeFromRight (170);
    tempo.setBounds (knobs.removeFromLeft (85));
    swing.setBounds (knobs);
    pc.removeFromRight (10);
    auto pats = pc.removeFromTop (34).withTrimmedTop (4);
    for (int i = 0; i < patternBtns.size(); ++i)
        patternBtns[i]->setBounds (pats.getX() + i * pats.getWidth() / 8, pats.getY(), pats.getWidth() / 8 - 3, pats.getHeight());
    pc.removeFromTop (12);
    auto btns = pc.removeFromTop (30);
    const int bw = (btns.getWidth() - 12) / 3;
    fill->setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (6);
    variation.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (6);
    clear.setBounds (btns);
    helpPanel.setBounds (help);

    // right: selected sound + kits
    auto snd = right.removeFromTop (210);
    right.removeFromTop (12);
    soundPanel.setBounds (snd);
    auto sc = (soundPanel.content() + snd.getPosition()).withTrimmedRight (4).withTrimmedBottom (4);
    sc.removeFromTop (32);
    auto eb = sc.removeFromBottom (28);
    editBtn.setBounds (eb.removeFromRight (150));
    sc.removeFromBottom (4);
    const int kw = sc.getWidth() / 4;
    int i = 0;
    for (auto* k : { level.get(), tune.get(), decay.get(), tone.get() })
        if (k != nullptr) k->setBounds (sc.getX() + (i++) * kw, sc.getY(), kw, sc.getHeight());
    kitPanel.setBounds (right);
    auto kc = (kitPanel.content() + right.getPosition()).withTrimmedRight (4).withTrimmedBottom (4);
    auto krow = kc.removeFromTop (28);
    prevBtn.setBounds (krow.removeFromLeft (30));
    nextBtn.setBounds (krow.removeFromRight (30));
    genre.setBounds (krow.reduced (6, 1));
    kc.removeFromTop (6);
    list.setBounds (kc);
}
} // namespace be::ui

#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== master rack list
FxRackList::FxRackList (Ctx& c, std::function<void (int)> cb) : ctx (c), onSelect (std::move (cb))
{
    const char* keys[kNumFx] { "eq", "comp", "mbcomp", "limiter", "clipper", "chorus", "phaser", "flanger", "delay", "reverb", "freqshift", "ringmod", "transient", "imager", "autofilter", "resonator" };
    for (int i = 0; i < kNumFx; ++i) addAndMakeVisible (toggles.add (new Toggle (c, fxId (keys[i], "on"))));
}

int FxRackList::rowAt (int y) const { return jlimit (0, kNumFx - 1, y / std::max (1, getHeight() / kNumFx)); }

void FxRackList::resized()
{
    const auto order = ctx.proc.getFxOrder();
    const int rh = getHeight() / kNumFx;
    for (int k = 0; k < kNumFx; ++k) toggles[order[(size_t) k]]->setBounds (6, k * rh, 22, rh);
}

void FxRackList::paint (Graphics& g)
{
    const auto order = ctx.proc.getFxOrder();
    const int rh = getHeight() / kNumFx;
    for (int k = 0; k < kNumFx; ++k)
    {
        const int id = order[(size_t) k];
        auto row = Rectangle<int> (0, k * rh, getWidth(), rh).reduced (0, 1);
        const bool sel = id == selected;
        g.setColour (sel ? S::hi : (k % 2 ? S::ink.withAlpha (0.05f) : Colours::transparentBlack)); g.fillRect (row);
        if (k == dragTo && dragFrom >= 0) { g.setColour (S::ink); g.fillRect (row.getX(), row.getY(), row.getWidth(), 2); }
        g.setFont (Fonts::mono (10.0f)); g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr (String (k + 1).paddedLeft ('0', 2)), row.withTrimmedLeft (32).removeFromLeft (20), Justification::centredLeft);
        g.setFont (Fonts::label (13.0f));
        const bool on = toggles[id]->getToggleState();
        g.setColour (on ? S::ink : S::inkFaint);
        g.drawText (::racoon::i18n::tr (fxNames()[id].toUpperCase()), row.withTrimmedLeft (56), Justification::centredLeft);
        g.setColour (S::inkSoft);
        for (int d = 0; d < 3; ++d) g.fillRect ((float) row.getRight() - 16.0f, (float) row.getCentreY() - 4.0f + (float) d * 3.0f, 10.0f, 1.0f);
    }
}

void FxRackList::mouseDown (const MouseEvent& e)
{
    const auto order = ctx.proc.getFxOrder();
    const int k = rowAt (e.y);
    if (e.x > getWidth() - 24) { dragFrom = k; dragTo = k; return; }
    selected = order[(size_t) k];
    repaint();
    if (onSelect) onSelect (selected);
}

void FxRackList::mouseDrag (const MouseEvent& e) { if (dragFrom >= 0) { dragTo = rowAt (e.y); repaint(); } }

void FxRackList::mouseUp (const MouseEvent&)
{
    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo)
    {
        auto order = ctx.proc.getFxOrder();
        std::vector<int> v (order.begin(), order.end());
        const int id = v[(size_t) dragFrom];
        v.erase (v.begin() + dragFrom);
        v.insert (v.begin() + dragTo, id);
        for (int k = 0; k < kNumFx; ++k) order[(size_t) k] = v[(size_t) k];
        ctx.proc.setFxOrder (order);
        resized();
    }
    dragFrom = dragTo = -1;
    repaint();
}

// ===================================================================== aux strip
AuxStrip::AuxStrip (Ctx& c, int index)
    : Panel ("AUX " + String (index + 1)), ctx (c), a (index), type (c, auxId (index, "type"), "TYPE"),
      p1 (c, auxId (index, "p1"), "P1"), p2 (c, auxId (index, "p2"), "P2"), p3 (c, auxId (index, "p3"), "P3"), p4 (c, auxId (index, "p4"), "P4"),
      ret (c, auxId (index, "return"), "RETURN"), lowcut (c, auxId (index, "lowcut"), "LOW CUT"), hicut (c, auxId (index, "hicut"), "HIGH CUT")
{
    for (Component* comp : std::initializer_list<Component*> { &type, &p1, &p2, &p3, &p4, &ret, &lowcut, &hicut }) addAndMakeVisible (comp);
    setInterceptsMouseClicks (false, true);
    ctx.apvts.addParameterListener (auxId (a, "type"), &labels);
    updateLabels();
}

AuxStrip::~AuxStrip() { ctx.apvts.removeParameterListener (auxId (a, "type"), &labels); }

void AuxStrip::updateLabels()
{
    const int t = (int) ctx.getParam (auxId (a, "type"));
    static const char* L[12][4] {
        { "SIZE", "DAMP", "WIDTH", "PRE-DLY" }, { "SIZE", "TONE", "WIDTH", "PRE-DLY" }, { "SIZE", "TONE", "WIDTH", "PRE-DLY" }, { "SIZE", "TONE", "WIDTH", "PRE-DLY" },
        { "TIME", "FEEDBACK", "SPREAD", "WOBBLE" }, { "TIME", "FEEDBACK", "SPREAD", "WOBBLE" }, { "RATE", "DEPTH", "FEEDBACK", "DELAY" },
        { "RATE", "DEPTH", "CENTRE", "FEEDBACK" }, { "RATE", "DEPTH", "FEEDBACK", "MANUAL" }, { "SHIFT", "FEEDBACK", "STEREO", "-" },
        { "ROOT", "CHORD", "DECAY", "BRIGHT" }, { "BITS", "RATE", "DRIVE", "-" } };
    const int k = jlimit (0, 11, t);
    p1.setLabel (L[k][0]); p2.setLabel (L[k][1]); p3.setLabel (L[k][2]); p4.setLabel (L[k][3]);
}

void AuxStrip::resized()
{
    auto r = content();
    type.setBounds (r.removeFromTop (40));
    r.removeFromTop (4);
    const int h = (r.getHeight() - 8) / 3;
    layoutRow (r.removeFromTop (h), { &p1, &p2 });
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (h), { &p3, &p4 });
    r.removeFromTop (4);
    layoutRow (r, { &ret, &lowcut, &hicut });
}

void AuxStrip::paint (Graphics& g)
{
    Panel::paint (g);
    const float lvl = ctx.proc.engine.telemetry.auxLevel[a].load();
    auto bar = getLocalBounds().toFloat().removeFromRight (6.0f).reduced (1.0f, 24.0f);
    g.setColour (C::engrave); g.fillRect (bar);
    const float n = jlimit (0.0f, 1.0f, (gainToDb (lvl) + 60.0f) / 60.0f);
    g.setColour (C::cyan.withAlpha (0.8f)); g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * n));
}

// ===================================================================== fx page
FxPage::FxPage (Ctx& c)
    : TrackPage (c), a1 (c, 0), a2 (c, 1), a3 (c, 2), a4 (c, 3), rack (c, [this] (int id) { showFx (id); })
{
    for (Component* comp : std::initializer_list<Component*> { &insertPanel, &rackPanel, &editPanel, &masterPanel, &duckPanel })
        addAndMakeVisible (comp), comp->toBack();
    for (Component* comp : std::initializer_list<Component*> { &a1, &a2, &a3, &a4, &rack }) addAndMakeVisible (comp);
    glueOn = std::make_unique<Toggle> (ctx, "glue_on", "GLUE");
    tpOn = std::make_unique<Toggle> (ctx, "tp_on", "TRUE PEAK");
    glueThr = std::make_unique<ParamKnob> (ctx, "glue_thresh", "THRESH");
    glueRatio = std::make_unique<ParamKnob> (ctx, "glue_ratio", "RATIO");
    glueAtt = std::make_unique<ParamKnob> (ctx, "glue_attack", "ATTACK");
    glueRel = std::make_unique<ParamKnob> (ctx, "glue_release", "RELEASE");
    glueMake = std::make_unique<ParamKnob> (ctx, "glue_makeup", "MAKEUP");
    glueMix = std::make_unique<ParamKnob> (ctx, "glue_mix", "MIX");
    softclip = std::make_unique<ParamKnob> (ctx, "softclip", "SOFT CLIP");
    ceiling = std::make_unique<ParamKnob> (ctx, "tp_ceiling", "CEILING");
    duckSrc = std::make_unique<ChoiceBox> (ctx, "duck_src", "TRIGGER");
    duckTarget = std::make_unique<ChoiceBox> (ctx, "duck_target", "DUCKS");
    duckDiv = std::make_unique<ChoiceBox> (ctx, "duck_div", "RATE");
    duckBand = std::make_unique<ChoiceBox> (ctx, "duck_band", "BAND");
    duckTrack = std::make_unique<Stepper> (ctx, "duck_track", "FROM TRACK");
    duckNote = std::make_unique<Stepper> (ctx, "duck_note", "MIDI NOTE", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); });
    duckDepth = std::make_unique<ParamKnob> (ctx, "duck_depth", "DEPTH");
    duckAttack = std::make_unique<ParamKnob> (ctx, "duck_attack", "ATTACK");
    duckHold = std::make_unique<ParamKnob> (ctx, "duck_hold", "HOLD");
    duckRelease = std::make_unique<ParamKnob> (ctx, "duck_release", "RECOVERY");
    duckCurve = std::make_unique<ParamKnob> (ctx, "duck_curve", "CURVE");
    duckThresh = std::make_unique<ParamKnob> (ctx, "duck_thresh", "SC THRESH");
    for (Component* comp : std::initializer_list<Component*> { glueOn.get(), tpOn.get(), glueThr.get(), glueRatio.get(), glueAtt.get(), glueRel.get(), glueMake.get(),
                                                               glueMix.get(), softclip.get(), ceiling.get(), duckSrc.get(), duckTarget.get(), duckDiv.get(),
                                                               duckBand.get(), duckTrack.get(), duckNote.get(), duckDepth.get(), duckAttack.get(), duckHold.get(),
                                                               duckRelease.get(), duckCurve.get(), duckThresh.get() })
        addAndMakeVisible (comp);
    rebuild();
    showFx (9);
}

void FxPage::rebuild()
{
    builtFor = ctx.track();
    drvType = box (T_DRV_TYPE, "DRIVE TYPE");
    drv = knob (T_DRV_AMT, "DRIVE"); crush = knob (T_CRUSH, "CRUSH"); eqLow = knob (T_EQ_LOW, "LOW"); eqHigh = knob (T_EQ_HIGH, "HIGH");
    comp = knob (T_COMP, "COMP"); gate = knob (T_GATE, "GATE"); trans = knob (T_TRANSIENT, "TRANSIENT"); phase = knob (T_PHASE, "ALIGN");
    s1 = knob (T_SEND1, "SEND 1"); s2 = knob (T_SEND2, "SEND 2"); s3 = knob (T_SEND3, "SEND 3"); s4 = knob (T_SEND4, "SEND 4");
    invert = toggle (T_INVERT, "INVERT");
    insertPanel.setTitle ("TRACK " + String (ctx.track() + 1) + " INSERTS  /  " + ctx.proc.getTrackName (ctx.track()).toUpperCase());
}

void FxPage::showFx (int id)
{
    fxControls.clear();
    struct K { const char* id; const char* label; int kind; };
    std::vector<K> ks;
    const char* key = "";
    switch ((FxId) id)
    {
        case FxId::EQ: key = "eq"; ks = { { "hpf", "LOW CUT", 0 }, { "lsfreq", "LS FREQ", 0 }, { "lsgain", "LS GAIN", 0 }, { "p1freq", "P1 FREQ", 0 }, { "p1gain", "P1 GAIN", 0 },
                                           { "p1q", "P1 Q", 0 }, { "p2freq", "P2 FREQ", 0 }, { "p2gain", "P2 GAIN", 0 }, { "p2q", "P2 Q", 0 }, { "hsfreq", "HS FREQ", 0 }, { "hsgain", "HS GAIN", 0 } }; break;
        case FxId::Compressor: key = "comp"; ks = { { "thresh", "THRESH", 0 }, { "ratio", "RATIO", 0 }, { "attack", "ATTACK", 0 }, { "release", "RELEASE", 0 }, { "makeup", "MAKEUP", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Multiband: key = "mbcomp"; ks = { { "xlow", "LOW X", 0 }, { "xhigh", "HIGH X", 0 }, { "low", "LOW THR", 0 }, { "mid", "MID THR", 0 }, { "high", "HIGH THR", 0 }, { "ratio", "RATIO", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Limiter: key = "limiter"; ks = { { "ceiling", "CEILING", 0 }, { "release", "RELEASE", 0 } }; break;
        case FxId::Clipper: key = "clipper"; ks = { { "drive", "DRIVE", 0 }, { "ceiling", "CEILING", 0 }, { "knee", "SOFTNESS", 0 } }; break;
        case FxId::Chorus: key = "chorus"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "delay", "DELAY", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Phaser: key = "phaser"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "centre", "CENTRE", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Flanger: key = "flanger"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "delay", "DELAY", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Delay: key = "delay"; ks = { { "time", "TIME", 0 }, { "feedback", "FEEDBACK", 0 }, { "highcut", "HIGH CUT", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 }, { "div", "DIVISION", 2 }, { "sync", "SYNC", 1 }, { "pingpong", "PING PONG", 1 } }; break;
        case FxId::Reverb: key = "reverb"; ks = { { "size", "SIZE", 0 }, { "damp", "DAMPING", 0 }, { "width", "WIDTH", 0 }, { "predelay", "PRE-DELAY", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::FreqShift: key = "freqshift"; ks = { { "shift", "SHIFT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::RingMod: key = "ringmod"; ks = { { "freq", "FREQ", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Transient: key = "transient"; ks = { { "attack", "ATTACK", 0 }, { "sustain", "SUSTAIN", 0 }, { "speed", "SPEED", 0 } }; break;
        case FxId::Imager: key = "imager"; ks = { { "width", "WIDTH", 0 }, { "monofreq", "MONO FREQ", 0 }, { "bassmono", "BASS MONO", 1 } }; break;
        case FxId::AutoFilter: key = "autofilter"; ks = { { "cutoff", "CUTOFF", 0 }, { "res", "RESO", 0 }, { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "mix", "MIX", 0 }, { "type", "TYPE", 2 }, { "div", "DIVISION", 2 }, { "sync", "SYNC", 1 } }; break;
        case FxId::Resonator: key = "resonator"; ks = { { "tune", "TUNE", 0 }, { "decay", "DECAY", 0 }, { "bright", "BRIGHT", 0 }, { "mix", "MIX", 0 } }; break;
    }
    for (auto& k : ks)
    {
        Component* comp = nullptr;
        if (k.kind == 0) comp = new ParamKnob (ctx, fxId (key, k.id), k.label);
        else if (k.kind == 1) comp = new Toggle (ctx, fxId (key, k.id), k.label);
        else comp = new ChoiceBox (ctx, fxId (key, k.id), k.label);
        comp->getProperties().set ("kind", k.kind);
        fxControls.add (comp);
        addAndMakeVisible (comp);
    }
    editPanel.setTitle ("MASTER  /  " + fxNames()[id].toUpperCase());
    rack.selected = id;
    rack.repaint();
    resized();
}

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    selector.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    auto top = r.removeFromTop (196);
    insertPanel.setBounds (top);
    auto ic = insertPanel.content() + top.getPosition();
    auto ih = ic.removeFromLeft (120);
    drvType->setBounds (ih.removeFromTop (40));
    ih.removeFromTop (10);
    invert->setBounds (ih.removeFromTop (24));
    ic.removeFromLeft (8);
    const int kh = (ic.getHeight() - 4) / 2;
    layoutRow (ic.removeFromTop (kh), { drv.get(), crush.get(), eqLow.get(), eqHigh.get(), comp.get(), gate.get() });
    ic.removeFromTop (4);
    layoutRow (ic, { trans.get(), phase.get(), s1.get(), s2.get(), s3.get(), s4.get() });
    r.removeFromTop (8);
    auto left = r.removeFromLeft (r.getWidth() * 50 / 100);
    layoutRow (left, { &a1, &a2, &a3, &a4 }, 8);
    r.removeFromLeft (8);
    auto rackArea = r.removeFromLeft (180);
    rackPanel.setBounds (rackArea);
    rack.setBounds (rackPanel.content() + rackArea.getPosition());
    r.removeFromLeft (8);
    // effect editor: six knobs per row
    auto editArea = r.removeFromTop (178);
    editPanel.setBounds (editArea);
    auto ec = editPanel.content() + editArea.getPosition();
    std::vector<Component*> knobs, others;
    for (auto* c : fxControls) ((int) c->getProperties()["kind"] == 0 ? knobs : others).push_back (c);
    const int perRow = 6;
    const int kw = ec.getWidth() / perRow;
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (ec.getX() + (int) (i % perRow) * kw, ec.getY() + (int) (i / perRow) * 66, kw, 64);
    const int rowsUsed = (int) (knobs.size() + perRow - 1) / perRow;
    auto orow = Rectangle<int> (ec.getX(), ec.getY() + rowsUsed * 66, ec.getWidth(), 38);
    for (size_t i = 0; i < others.size(); ++i)
        others[i]->setBounds (orow.getX() + (int) i * 104, orow.getY() + ((int) others[i]->getProperties()["kind"] == 1 ? 14 : 0), 100, (int) others[i]->getProperties()["kind"] == 1 ? 22 : 38);
    r.removeFromTop (8);
    auto mArea = r.removeFromTop (114);
    masterPanel.setBounds (mArea);
    auto mc = masterPanel.content() + mArea.getPosition();
    auto mt = mc.removeFromTop (20);
    glueOn->setBounds (mt.removeFromLeft (80));
    tpOn->setBounds (mt.removeFromLeft (110));
    layoutRow (mc, { glueThr.get(), glueRatio.get(), glueAtt.get(), glueRel.get(), glueMake.get(), glueMix.get(), softclip.get(), ceiling.get() }, 2);
    r.removeFromTop (8);
    duckPanel.setBounds (r);
    auto dc = duckPanel.content() + r.getPosition();
    auto d1 = dc.removeFromTop (38);
    layoutRow (d1, { duckSrc.get(), duckTarget.get(), duckDiv.get(), duckBand.get(), duckTrack.get(), duckNote.get() }, 4);
    dc.removeFromTop (2);
    layoutRow (dc, { duckDepth.get(), duckAttack.get(), duckHold.get(), duckRelease.get(), duckCurve.get(), duckThresh.get() }, 2);
}

// ===================================================================== channel strip
ChannelStrip::ChannelStrip (Ctx& c, int track)
    : ctx (c), t (track), pan (c, trackParamId (track, T_PAN)), s1 (c, trackParamId (track, T_SEND1)), s2 (c, trackParamId (track, T_SEND2)),
      s3 (c, trackParamId (track, T_SEND3)), s4 (c, trackParamId (track, T_SEND4)),
      mute (c, trackParamId (track, T_MUTE), "M", false, C::ochre), solo (c, trackParamId (track, T_SOLO), "S", false, C::jade)
{
    fader.setSliderStyle (Slider::LinearVertical);
    fader.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    faderA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (ctx.apvts, trackParamId (t, T_FADER), fader);
    fader.setDoubleClickReturnValue (true, 0.0);
    group.addItemList (groupNames(), 1);
    output.addItemList (outputNames(), 1);
    groupA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.apvts, trackParamId (t, T_GROUP), group);
    outA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.apvts, trackParamId (t, T_OUTPUT), output);
    group.setTooltip ("Group bus"); output.setTooltip ("Output: main mix or a separate host output (enable it in your DAW's track I/O)");
    for (Component* comp : std::initializer_list<Component*> { &fader, &pan, &s1, &s2, &s3, &s4, &mute, &solo, &group, &output }) addAndMakeVisible (comp);
    ctx.registerAnimated (this);
}

ChannelStrip::~ChannelStrip() { ctx.unregisterAnimated (this); }

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (2, 2);
    r.removeFromTop (34);
    auto sends = r.removeFromTop (88);
    const int sw = sends.getWidth() / 2;
    s1.setBounds (sends.getX(), sends.getY(), sw, 42); s2.setBounds (sends.getX() + sw, sends.getY(), sw, 42);
    s3.setBounds (sends.getX(), sends.getY() + 44, sw, 42); s4.setBounds (sends.getX() + sw, sends.getY() + 44, sw, 42);
    pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (38, 38));
    auto bottom = r.removeFromBottom (78);
    group.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (2);
    output.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (4);
    layoutRow (bottom.removeFromTop (24), { &mute, &solo }, 3);
    r.removeFromBottom (16);
    auto fr = r.withTrimmedLeft (r.getWidth() / 2 - 2);
    fader.setBounds (fr.removeFromLeft (fr.getWidth()).reduced (0, 2));
}

void ChannelStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool sel = t == ctx.track();
    g.setColour (sel ? S::hi.withAlpha (0.35f) : S::ink.withAlpha (0.04f));
    g.fillRect (r);
    g.setColour (S::ink.withAlpha (sel ? 1.0f : 0.35f));
    g.drawRect (r, sel ? 2.0f : 1.0f);
    auto head = r.removeFromTop (32.0f);
    g.setColour (sel ? S::hi : S::cardDim); g.fillRect (head.reduced (2.0f));
    if (flash > 0.01f) { g.setColour (S::accent().withAlpha (0.9f * flash)); g.fillRect (head.reduced (2.0f)); }
    g.setFont (Fonts::mono (10.0f)); g.setColour (S::inkSoft);
    g.drawText (::racoon::i18n::tr (String (t + 1)), head.removeFromTop (12.0f), Justification::centred);
    g.setFont (Fonts::label (11.5f)); g.setColour (S::ink);
    g.drawFittedText (::racoon::i18n::tr (ctx.proc.getTrackName (t).toUpperCase()), head.toNearestInt(), Justification::centred, 1, 0.5f);
    // meter beside the fader
    auto fb = fader.getBounds().toFloat();
    auto m = Rectangle<float> (fb.getX() - 9.0f, fb.getY() + 4.0f, 5.0f, fb.getHeight() - 8.0f);
    g.setColour (C::engrave); g.fillRect (m);
    const float n = jlimit (0.0f, 1.0f, (gainToDb (lastPk) + 60.0f) / 66.0f);
    g.setColour (lastPk > 0.98f ? C::ochre : C::amber);
    g.fillRect (m.withTop (m.getBottom() - m.getHeight() * n));
    const float hn = jlimit (0.0f, 1.0f, (gainToDb (hold) + 60.0f) / 66.0f);
    g.setColour (C::amberHot); g.drawHorizontalLine ((int) (m.getBottom() - m.getHeight() * hn), m.getX(), m.getRight());
    auto* p = ctx.apvts.getParameter (trackParamId (t, T_FADER));
    g.setFont (Fonts::mono (9.5f)); g.setColour (S::inkSoft);
    g.drawText (::racoon::i18n::tr (p->getCurrentValueAsText()), Rectangle<float> (r.getX(), fb.getBottom(), r.getWidth(), 14.0f), Justification::centred);
}

void ChannelStrip::animate()
{
    if (! animVisible (this)) return;
    const float pk = ctx.proc.engine.telemetry.trackPeak[t].load();
    hold = std::max (pk, hold * 0.96f);
    const int h = ctx.proc.engine.telemetry.trackHits[t].load();
    if (h != lastHits) { lastHits = h; flash = 1.0f; } else flash *= 0.8f;
    if (std::abs (pk - lastPk) > 0.001f || flash > 0.01f) { lastPk = pk; repaint(); }
}

void ChannelStrip::mouseDown (const MouseEvent& e)
{
    ctx.selectTrack (t);
    if (e.y < 32) ctx.proc.padDown (t, 0.85f);
    if (auto* par = getParentComponent()) par->repaint();
}

// ===================================================================== bus strip
BusStrip::BusStrip (Ctx& c, int i, bool g) : ctx (c), index (i), isGroup (g)
{
    if (isGroup)
    {
        k1 = std::make_unique<ParamKnob> (ctx, groupId (i, "level"), "LEVEL");
        k2 = std::make_unique<ParamKnob> (ctx, groupId (i, "comp"), "COMP");
        k3 = std::make_unique<ParamKnob> (ctx, groupId (i, "drive"), "SATURATE");
        k4 = std::make_unique<ParamKnob> (ctx, groupId (i, "width"), "WIDTH");
    }
    else
    {
        k1 = std::make_unique<ParamKnob> (ctx, auxId (i, "return"), "RETURN");
        k2 = std::make_unique<ParamKnob> (ctx, auxId (i, "lowcut"), "LOW CUT");
        k3 = std::make_unique<ParamKnob> (ctx, auxId (i, "hicut"), "HIGH CUT");
        k4 = std::make_unique<ParamKnob> (ctx, auxId (i, "p1"), "SIZE/TIME");
    }
    for (auto* k : { k1.get(), k2.get(), k3.get(), k4.get() }) addAndMakeVisible (k);
    ctx.registerAnimated (this);
}

BusStrip::~BusStrip() { ctx.unregisterAnimated (this); }

void BusStrip::resized()
{
    auto r = getLocalBounds().reduced (4);
    r.removeFromTop (22);
    r.removeFromRight (10);
    const int h = r.getHeight() / 2;
    layoutRow (r.removeFromTop (h), { k1.get(), k2.get() });
    layoutRow (r, { k3.get(), k4.get() });
}

void BusStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (S::ink.withAlpha (0.04f)); g.fillRect (r);
    g.setColour (S::ink.withAlpha (0.35f)); g.drawRect (r, 1.0f);
    g.setFont (Fonts::label (12.0f)); g.setColour (S::ink);
    const String name = isGroup ? groupNames()[index + 1].toUpperCase() : "AUX " + String (index + 1) + "  " + auxTypeNames()[(int) ctx.getParam (auxId (index, "type"))].toUpperCase();
    g.drawFittedText (::racoon::i18n::tr (name), r.removeFromTop (20.0f).toNearestInt().reduced (6, 0), Justification::centredLeft, 1, 0.6f);
    auto bar = getLocalBounds().toFloat().removeFromRight (9.0f).reduced (2.0f, 24.0f);
    g.setColour (C::engrave); g.fillRect (bar);
    if (isGroup)
    {
        const float gr = jlimit (0.0f, 18.0f, -ctx.proc.engine.telemetry.groupGr[index].load());
        g.setColour (C::cyan); g.fillRect (bar.withHeight (bar.getHeight() * gr / 18.0f));
    }
    else
    {
        const float n = jlimit (0.0f, 1.0f, (gainToDb (ctx.proc.engine.telemetry.auxLevel[index].load()) + 60.0f) / 60.0f);
        g.setColour (C::amber); g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * n));
    }
}

// ===================================================================== mix page
MixPage::MixPage (Ctx& c) : ctx (c), meter (c), master (c, "g_master", "MASTER")
{
    for (Component* comp : std::initializer_list<Component*> { &chanPanel, &busPanel, &auxPanel, &masterPanel }) addAndMakeVisible (comp);
    for (int t = 0; t < kNumTracks; ++t) addAndMakeVisible (strips.add (new ChannelStrip (ctx, t)));
    for (int gI = 0; gI < kNumGroups; ++gI) addAndMakeVisible (groupStrips.add (new BusStrip (ctx, gI, true)));
    for (int a = 0; a < kNumAux; ++a) addAndMakeVisible (auxStrips.add (new BusStrip (ctx, a, false)));
    addAndMakeVisible (meter);
    addAndMakeVisible (master);
    addAndMakeVisible (outInfo);
    outInfo.setFont (Fonts::body (12.5f));
    outInfo.setColour (Label::textColourId, S::inkSoft);
    outInfo.setJustificationType (Justification::topLeft);
    outInfo.setText ("Set a channel OUTPUT to Out 1-16 to send it to its own track in your DAW: add an audio track, choose Drum Engine as input and pick the output there.", dontSendNotification);
}

void MixPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bottom = r.removeFromBottom (190);
    chanPanel.setBounds (r);
    auto cc = chanPanel.content() + r.getPosition();
    const int w = cc.getWidth() / kNumTracks;
    for (int t = 0; t < kNumTracks; ++t) strips[t]->setBounds (cc.getX() + t * w, cc.getY(), w - 2, cc.getHeight());
    bottom.removeFromTop (8);
    auto mArea = bottom.removeFromRight (300);
    auto gArea = bottom.removeFromLeft (bottom.getWidth() / 2 - 4);
    busPanel.setBounds (gArea);
    auto gc = busPanel.content() + gArea.getPosition();
    layoutRow (gc, { groupStrips[0], groupStrips[1], groupStrips[2], groupStrips[3] }, 4);
    bottom.removeFromLeft (8);
    auxPanel.setBounds (bottom);
    auto ac = auxPanel.content() + bottom.getPosition();
    layoutRow (ac, { auxStrips[0], auxStrips[1], auxStrips[2], auxStrips[3] }, 4);
    mArea.removeFromLeft (8);
    masterPanel.setBounds (mArea);
    auto mc = masterPanel.content() + mArea.getPosition();
    meter.setBounds (mc.removeFromLeft (22));
    mc.removeFromLeft (8);
    master.setBounds (mc.removeFromLeft (80).removeFromTop (100));
    mc.removeFromLeft (6);
    outInfo.setBounds (mc);
}
} // namespace be::ui

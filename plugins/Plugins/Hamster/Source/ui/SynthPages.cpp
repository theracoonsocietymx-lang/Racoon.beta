#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== synth page
SynthPage::SynthPage (Ctx& c) : TrackPage (c), algoView (c), envSketch (c), filterCurve (c)
{
    for (Component* comp : std::initializer_list<Component*> { &aPanel, &bPanel, &dPanel, &envPanel, &filtPanel, &lfoPanel })
        addAndMakeVisible (comp), comp->toBack();
    addAndMakeVisible (algoView);
    addAndMakeVisible (envSketch);
    addAndMakeVisible (filterCurve);
    rebuild();
}

void SynthPage::rebuild()
{
    builtFor = ctx.track();
    const int role = (int) ctx.proc.trackValue (ctx.track(), T_ROLE);
    const auto& names = roleParamNames (role);
    aOn = toggle (T_A_ON, "ON");
    model = box (T_MODEL, "MODEL");
    for (int k = 0; k < 8; ++k) aP[k] = knob (T_A_P1 + k, names[k]);
    aLvl = knob (T_A_LEVEL, "LEVEL");
    drift = knob (T_A_DRIFT, "DRIFT");
    bOn = toggle (T_B_ON, "ON");
    algo = box (T_B_ALGO, "ALGORITHM");
    for (int k = 0; k < 6; ++k)
    {
        ratio[k] = knob (T_B_R1 + k, "RATIO " + String (k + 1));
        lvl[k] = knob (T_B_L1 + k, "LEVEL " + String (k + 1));
        dcy[k] = knob (T_B_D1 + k, "DECAY " + String (k + 1));
    }
    fb = knob (T_B_FB, "FEEDBACK"); morph = knob (T_B_MORPH, "MORPH"); bLvl = knob (T_B_LEVEL, "LEVEL");
    dOn = toggle (T_D_ON, "ON");
    texType = box (T_D_TYPE, "TYPE");
    dLvl = knob (T_D_LEVEL, "LEVEL"); dFreq = knob (T_D_FREQ, "FREQ"); dQ = knob (T_D_Q, "RESO"); dDecay = knob (T_D_DECAY, "DECAY"); dTone = knob (T_D_TONE, "TONE");
    envMode = box (T_ENV_MODE, "AMP MODE");
    att = knob (T_ENV_ATTACK, "ATTACK"); hold = knob (T_ENV_HOLD, "HOLD"); dec = knob (T_ENV_DECAY, "DECAY"); sus = knob (T_ENV_SUSTAIN, "SUSTAIN");
    rel = knob (T_ENV_RELEASE, "RELEASE"); curve = knob (T_ENV_CURVE, "CURVE");
    pAmt = knob (T_PENV_AMT, "PITCH ENV"); pTime = knob (T_PENV_TIME, "P TIME"); fAmt = knob (T_FENV_AMT, "FILT ENV"); fDec = knob (T_FENV_DECAY, "F DECAY");
    fType = box (T_F_TYPE, "TYPE");
    cutoff = knob (T_F_CUTOFF, "CUTOFF"); res = knob (T_F_RES, "RESO"); drive = knob (T_F_DRIVE, "DRIVE"); key = knob (T_F_KEY, "KEY TRK"); mix = knob (T_F_MIX, "MIX");
    vl1w = box (T_VL1_WAVE, "LFO 1"); vl2w = box (T_VL2_WAVE, "LFO 2");
    vl1r = knob (T_VL1_RATE, "RATE 1"); vl2r = knob (T_VL2_RATE, "RATE 2");
    vl1chip = std::make_unique<ModChip> (ctx, ModSrc::VLfo1, "V-LFO 1"); addAndMakeVisible (*vl1chip);
    vl2chip = std::make_unique<ModChip> (ctx, ModSrc::VLfo1 + 1, "V-LFO 2"); addAndMakeVisible (*vl2chip);
    aPanel.setTitle ("A  /  ANALOG MODEL  /  " + roleNames()[role].toUpperCase());
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    selector.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    auto top = r.removeFromTop (r.getHeight() / 2 + 20);
    auto aArea = top.removeFromLeft (520);
    aPanel.setBounds (aArea);
    auto ac = aPanel.content() + aArea.getPosition();
    auto ah = ac.removeFromTop (40);
    aOn->setBounds (ah.removeFromLeft (60).withTrimmedTop (14));
    ah.removeFromLeft (8);
    model->setBounds (ah.removeFromLeft (130));
    ac.removeFromTop (6);
    const int kh = (ac.getHeight() - 6) / 2;
    layoutRow (ac.removeFromTop (kh), { aP[0].get(), aP[1].get(), aP[2].get(), aP[3].get(), aP[4].get() });
    ac.removeFromTop (6);
    layoutRow (ac, { aP[5].get(), aP[6].get(), aP[7].get(), aLvl.get(), drift.get() });
    top.removeFromLeft (8);
    bPanel.setBounds (top);
    auto bc = bPanel.content() + top.getPosition();
    auto bLeft = bc.removeFromLeft (190);
    auto bh = bLeft.removeFromTop (40);
    bOn->setBounds (bh.removeFromLeft (50).withTrimmedTop (14));
    algo->setBounds (bh);
    bLeft.removeFromTop (4);
    algoView.setBounds (bLeft.removeFromTop (bLeft.getHeight() - 92));
    layoutRow (bLeft.removeFromTop (88), { fb.get(), morph.get(), bLvl.get() });
    bc.removeFromLeft (8);
    const int rh = (bc.getHeight() - 8) / 3;
    layoutRow (bc.removeFromTop (rh), { ratio[0].get(), ratio[1].get(), ratio[2].get(), ratio[3].get(), ratio[4].get(), ratio[5].get() }, 2);
    bc.removeFromTop (4);
    layoutRow (bc.removeFromTop (rh), { lvl[0].get(), lvl[1].get(), lvl[2].get(), lvl[3].get(), lvl[4].get(), lvl[5].get() }, 2);
    bc.removeFromTop (4);
    layoutRow (bc, { dcy[0].get(), dcy[1].get(), dcy[2].get(), dcy[3].get(), dcy[4].get(), dcy[5].get() }, 2);

    r.removeFromTop (8);
    auto dArea = r.removeFromLeft (250);
    dPanel.setBounds (dArea);
    auto dc = dPanel.content() + dArea.getPosition();
    auto dh = dc.removeFromTop (40);
    dOn->setBounds (dh.removeFromLeft (50).withTrimmedTop (14));
    texType->setBounds (dh);
    dc.removeFromTop (4);
    const int dkh = (dc.getHeight() - 4) / 2;
    layoutRow (dc.removeFromTop (dkh), { dLvl.get(), dFreq.get(), dQ.get() });
    dc.removeFromTop (4);
    layoutRow (dc, { dDecay.get(), dTone.get(), nullptr });
    r.removeFromLeft (8);
    auto eArea = r.removeFromLeft (470);
    envPanel.setBounds (eArea);
    auto ec = envPanel.content() + eArea.getPosition();
    auto eTop = ec.removeFromTop (ec.getHeight() / 2 - 10);
    envMode->setBounds (eTop.removeFromLeft (100).removeFromTop (40));
    eTop.removeFromLeft (6);
    envSketch.setBounds (eTop);
    ec.removeFromTop (6);
    const int ekh = (ec.getHeight() - 4) / 2;
    layoutRow (ec.removeFromTop (ekh), { att.get(), hold.get(), dec.get(), sus.get(), rel.get() });
    ec.removeFromTop (4);
    layoutRow (ec, { curve.get(), pAmt.get(), pTime.get(), fAmt.get(), fDec.get() });
    r.removeFromLeft (8);
    auto lArea = r.removeFromBottom (150);
    filtPanel.setBounds (r);
    auto fc = filtPanel.content() + r.getPosition();
    auto fTop = fc.removeFromTop (std::max (44, fc.getHeight() - 92));
    fType->setBounds (fTop.removeFromLeft (110).removeFromTop (40));
    fTop.removeFromLeft (6);
    filterCurve.setBounds (fTop);
    fc.removeFromTop (4);
    layoutRow (fc, { cutoff.get(), res.get(), drive.get(), key.get(), mix.get() });
    lArea.removeFromTop (8);
    lfoPanel.setBounds (lArea);
    auto lc = lfoPanel.content() + lArea.getPosition();
    auto l1 = lc.removeFromLeft (lc.getWidth() / 2 - 4);
    auto l2 = lc.withTrimmedLeft (8);
    auto place = [] (Rectangle<int> area, ChoiceBox& box, ParamKnob& k, ModChip& chip)
    {
        auto left = area.removeFromLeft (area.getWidth() - 90);
        box.setBounds (left.removeFromTop (40));
        left.removeFromTop (8);
        chip.setBounds (left.removeFromTop (22));
        k.setBounds (area);
    };
    place (l1, *vl1w, *vl1r, *vl1chip);
    place (l2, *vl2w, *vl2r, *vl2chip);
}

// ===================================================================== sample page
SamplePage::SamplePage (Ctx& c) : TrackPage (c), wave (c)
{
    for (Component* comp : std::initializer_list<Component*> { &wavePanel, &slotPanel, &playPanel, &slicePanel })
        addAndMakeVisible (comp), comp->toBack();
    addAndMakeVisible (wave);
    addAndMakeVisible (dnaBtn);
    for (int k = 0; k < kNumSlots; ++k)
    {
        auto* b = slotBtns.add (new TextButton ("SLOT " + String (k + 1)));
        b->setClickingTogglesState (false);
        b->onClick = [this, k] { wave.slot = k; refreshSlots(); wave.repaint(); };
        addAndMakeVisible (b);
        auto* l = slotLoad.add (new TextButton ("LOAD"));
        l->onClick = [this, k]
        {
            chooser = std::make_unique<FileChooser> ("Load sample into slot " + String (k + 1), File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, k] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) { ctx.proc.loadTrackSample (ctx.track(), k, fc.getResult()); wave.slot = k; refreshSlots(); }
            });
        };
        addAndMakeVisible (l);
        auto* x = slotClear.add (new TextButton ("X"));
        x->onClick = [this, k] { ctx.proc.clearTrackSample (ctx.track(), k); refreshSlots(); };
        addAndMakeVisible (x);
    }
    dnaBtn.onClick = [this]
    {
        auto* s = ctx.proc.trackSample (ctx.track(), wave.slot);
        if (s == nullptr) return;
        auto copy = std::make_shared<SampleData> (*s);
        auto res = DrumDNA::analyse (copy);
        if (res.ok) { DrumDNA::apply (ctx.proc, ctx.track(), res, 1); rebuild(); resized(); if (ctx.refreshAll) ctx.refreshAll(); }
    };
    ctx.registerAnimated (this);
    rebuild();
}

SamplePage::~SamplePage() { ctx.unregisterAnimated (this); }

void SamplePage::rebuild()
{
    builtFor = ctx.track();
    cOn = toggle (T_C_ON, "ON");
    reverse = toggle (T_C_REVERSE, "REVERSE");
    loop = toggle (T_C_LOOP, "LOOP");
    sample = box (T_C_SAMPLE, "SOURCE");
    stretchMode = box (T_C_STRETCHMODE, "PITCH MODE");
    sliceMode = box (T_C_SLICEMODE, "SLICE MODE");
    multi = box (T_C_MULTI, "SLOT MODE");
    slice = stepper (T_C_SLICE, "SLICE", [] (int v) { return String (v + 1); });
    cLvl = knob (T_C_LEVEL, "LEVEL"); start = knob (T_C_START, "START"); end = knob (T_C_END, "END"); pitch = knob (T_C_PITCH, "PITCH");
    stretch = knob (T_C_STRETCH, "STRETCH"); fadeIn = knob (T_C_FADEIN, "FADE IN"); fadeOut = knob (T_C_FADEOUT, "FADE OUT"); rr = knob (T_C_RR, "START VAR");
    lastSlotsKey = -1;
    refreshSlots();
}

void SamplePage::refreshSlots()
{
    for (int k = 0; k < kNumSlots; ++k)
    {
        const auto* s = ctx.proc.trackSample (ctx.track(), k);
        slotBtns[k]->setButtonText (s != nullptr ? s->name.substring (0, 16).toUpperCase() : "SLOT " + String (k + 1) + "  (EMPTY)");
        slotBtns[k]->setColour (TextButton::buttonColourId, k == wave.slot ? C::stoneHi : C::stone);
        slotBtns[k]->setColour (TextButton::textColourOffId, k == wave.slot ? C::amberHot : (s != nullptr ? C::bone : C::ash));
        slotClear[k]->setEnabled (s != nullptr);
    }
    wave.repaint();
}

void SamplePage::animate()
{
    if (! animVisible (this)) return;
    int key = ctx.track() * 16;
    for (int k = 0; k < kNumSlots; ++k) key += (ctx.proc.trackSample (ctx.track(), k) != nullptr ? 1 : 0) << k;
    if (key != lastSlotsKey) { lastSlotsKey = key; refreshSlots(); }
}

void SamplePage::resized()
{
    auto r = getLocalBounds().reduced (8);
    selector.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    auto top = r.removeFromTop (r.getHeight() * 52 / 100);
    wavePanel.setBounds (top);
    auto wc = wavePanel.content() + top.getPosition();
    auto wh = wc.removeFromTop (40);
    cOn->setBounds (wh.removeFromLeft (60).withTrimmedTop (14));
    wh.removeFromLeft (8);
    sample->setBounds (wh.removeFromLeft (220));
    wh.removeFromLeft (8);
    dnaBtn.setBounds (wh.removeFromRight (220).withTrimmedTop (14));
    wc.removeFromTop (6);
    wave.setBounds (wc);
    r.removeFromTop (8);
    auto sArea = r.removeFromLeft (360);
    slotPanel.setBounds (sArea);
    auto sc = slotPanel.content() + sArea.getPosition();
    multi->setBounds (sc.removeFromTop (40).removeFromLeft (180));
    sc.removeFromTop (8);
    for (int k = 0; k < kNumSlots; ++k)
    {
        auto row = sc.removeFromTop (30);
        slotClear[k]->setBounds (row.removeFromRight (30));
        row.removeFromRight (4);
        slotLoad[k]->setBounds (row.removeFromRight (64));
        row.removeFromRight (4);
        slotBtns[k]->setBounds (row);
        sc.removeFromTop (6);
    }
    r.removeFromLeft (8);
    auto slArea = r.removeFromRight (300);
    playPanel.setBounds (r);
    auto pc = playPanel.content() + r.getPosition();
    auto ph = pc.removeFromTop (40);
    stretchMode->setBounds (ph.removeFromLeft (150));
    ph.removeFromLeft (10);
    reverse->setBounds (ph.removeFromLeft (100).withTrimmedTop (14));
    loop->setBounds (ph.removeFromLeft (80).withTrimmedTop (14));
    pc.removeFromTop (6);
    const int kh = (pc.getHeight() - 6) / 2;
    layoutRow (pc.removeFromTop (kh), { cLvl.get(), pitch.get(), stretch.get(), rr.get() });
    pc.removeFromTop (6);
    layoutRow (pc, { start.get(), end.get(), fadeIn.get(), fadeOut.get() });
    slArea.removeFromLeft (8);
    slicePanel.setBounds (slArea);
    auto slc = slicePanel.content() + slArea.getPosition();
    sliceMode->setBounds (slc.removeFromTop (40));
    slc.removeFromTop (8);
    slice->setBounds (slc.removeFromTop (40));
}

void SamplePage::paintOverChildren (Graphics& g)
{
    auto r = slicePanel.getBounds().reduced (14, 10);
    r.removeFromTop (120);
    g.setFont (Fonts::body (13.0f));
    g.setColour (C::boneDim);
    g.drawFittedText (::racoon::i18n::tr ("EQUAL 16 cuts the region into sixteen parts. TRANSIENTS follows the hits found in the file. "
                      "Lock SLICE per step in the sequencer to play a break in any order."), r, Justification::topLeft, 6);
}
} // namespace be::ui

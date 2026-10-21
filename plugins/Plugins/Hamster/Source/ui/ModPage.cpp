#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== matrix row
MatrixRow::MatrixRow (Ctx& c, int row) : ctx (c), r (row), on (c, routeId (row, "on")), curve (c, routeId (row, "curve"))
{
    src.addItemList (modSourceNames(), 1);
    StringArray trks { "All Tracks" };
    for (int t = 0; t < kNumTracks; ++t) trks.add ("T" + String (t + 1));
    trk.addItemList (trks, 1);
    // destinations grouped in a menu
    auto* menu = dst.getRootMenu();
    const auto& names = modDestNames();
    menu->addItem (1, names[0]);
    struct Grp { const char* name; int from, to; };
    // every destination is listed in parameter order: the attachment maps by item index
    const Grp groups[] { { "Track: common", T_ROLE, T_MODEL }, { "Track: analog", T_A_ON, T_A_DRIFT }, { "Track: FM", T_B_ON, T_B_MORPH },
                         { "Track: sample", T_C_ON, T_C_LOOP }, { "Track: texture", T_D_ON, T_D_TONE }, { "Track: envelopes", T_ENV_MODE, T_FENV_DECAY },
                         { "Track: filter", T_F_TYPE, T_F_MIX }, { "Track: inserts", T_DRV_TYPE, T_TRANSIENT }, { "Track: mix", T_FADER, T_VL2_WAVE } };
    for (auto& g : groups)
    {
        PopupMenu sub;
        for (int p = g.from; p <= g.to; ++p)
            sub.addItem (ModDst::TrackBase + p + 1, names[ModDst::TrackBase + p], trackParam (p).kind == 0 || trackParam (p).kind == 3);
        menu->addSubMenu (g.name, sub);
    }
    PopupMenu glob;
    for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d) glob.addItem (d + 1, names[d]);
    menu->addSubMenu ("Global", glob);
    srcA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.apvts, routeId (r, "src"), src);
    trkA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.apvts, routeId (r, "trk"), trk);
    dstA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.apvts, routeId (r, "dst"), dst);
    amount.setSliderStyle (Slider::LinearBar);
    amount.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    amtA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (ctx.apvts, routeId (r, "amt"), amount);
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.getProperties().set ("bipolar", true);
    amount.setPopupDisplayEnabled (true, true, nullptr);
    amount.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + " %"; };
    for (Component* comp : std::initializer_list<Component*> { &on, &src, &trk, &dst, &amount, &curve }) addAndMakeVisible (comp);
    ctx.registerAnimated (this);
}

MatrixRow::~MatrixRow() { ctx.unregisterAnimated (this); }

void MatrixRow::resized()
{
    auto b = getLocalBounds().reduced (2, 2);
    b.removeFromLeft (24);
    on.setBounds (b.removeFromLeft (26));
    src.setBounds (b.removeFromLeft (124)); b.removeFromLeft (4);
    trk.setBounds (b.removeFromLeft (70)); b.removeFromLeft (4);
    dst.setBounds (b.removeFromLeft (184)); b.removeFromLeft (4);
    curve.setBounds (b.removeFromRight (92)); b.removeFromRight (4);
    b.removeFromRight (34);
    amount.setBounds (b.reduced (0, 3));
}

void MatrixRow::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (highlight ? C::stoneHi : ((r % 2) ? C::void_.withAlpha (0.35f) : C::void_.withAlpha (0.18f)));
    g.fillRect (b);
    g.setFont (Fonts::mono (10.0f));
    g.setColour (C::ash);
    g.drawText (::racoon::i18n::tr (String (r + 1).paddedLeft ('0', 2)), b.removeFromLeft (24.0f), Justification::centred);
    auto meter = getLocalBounds().toFloat().reduced (2.0f).removeFromRight (130.0f).removeFromLeft (34.0f).reduced (4.0f, 8.0f);
    g.setColour (C::engrave); g.fillRect (meter);
    g.setColour (C::cyan);
    const float v = jlimit (-1.0f, 1.0f, live);
    g.fillRect (meter.withWidth (meter.getWidth() * 0.5f * std::abs (v)).withX (v >= 0 ? meter.getCentreX() : meter.getCentreX() - meter.getWidth() * 0.5f * std::abs (v)));
}

void MatrixRow::animate()
{
    if (! animVisible (this)) return;
    const int s = (int) ctx.getParam (routeId (r, "src"));
    const float v = s > 0 ? ctx.proc.engine.telemetry.src[s].load() * ctx.getParam (routeId (r, "amt")) : 0.0f;
    if (std::abs (v - live) > 0.01f) { live = v; repaint(); }
}

// ===================================================================== mod page
ModPage::ModPage (Ctx& c)
    : ctx (c), lfoTabs ({ "1", "2", "3", "4", "5", "6", "7", "8" }, [this] (int i) { curLfo = i; lfoView.setLfo (i); lfoChip.setSource (ModSrc::GLfo1 + i, "LFO " + String (i + 1)); buildLfoControls(); resized(); }),
      lfoView (c), lfoChip (c, ModSrc::GLfo1, "LFO 1"), xy (c)
{
    for (Component* comp : std::initializer_list<Component*> { &lfoPanel, &srcPanel, &matPanel, &scenePanel, &lfoTabs, &lfoView, &lfoChip, &matView, &xy })
        addAndMakeVisible (comp);
    lfoPanel.toBack(); srcPanel.toBack(); matPanel.toBack(); scenePanel.toBack();
    for (int s = 1; s < ModSrc::Count; ++s)
        if (! (s >= ModSrc::Macro1 && s < ModSrc::Macro1 + kNumMacros)) addAndMakeVisible (chips.add (new ModChip (ctx, s)));
    matView.setViewedComponent (&matContent, false);
    matView.setScrollBarsShown (true, false);
    for (int i = 0; i < kNumRoutes; ++i) matContent.addAndMakeVisible (rows.add (new MatrixRow (ctx, i)));
    sceneA = std::make_unique<Stepper> (ctx, "g_scenea", "SCENE A");
    sceneB = std::make_unique<Stepper> (ctx, "g_sceneb", "SCENE B");
    sceneFade = std::make_unique<ParamKnob> (ctx, "g_scenefade", "A  /  B");
    sceneOn = std::make_unique<ParamButton> (ctx, "g_sceneon", "SCENES ON");
    for (Component* comp : std::initializer_list<Component*> { sceneA.get(), sceneB.get(), sceneFade.get(), sceneOn.get() }) addAndMakeVisible (comp);
    for (int s = 0; s < kNumScenes; ++s)
    {
        auto* b = sceneBtns.add (new TextButton (String (s + 1)));
        b->setTooltip ("Click: recall scene " + String (s + 1) + ". Shift-click: store the current sound. Alt-click: clear.");
        b->onClick = [this, s, b]
        {
            const auto mods = ModifierKeys::getCurrentModifiers();
            if (mods.isShiftDown()) ctx.proc.captureScene (s);
            else if (mods.isAltDown()) ctx.proc.clearScene (s);
            else if (ctx.proc.sceneSet (s)) ctx.proc.recallScene (s);
            else ctx.proc.captureScene (s);
            b->setColour (TextButton::textColourOffId, ctx.proc.sceneSet (s) ? C::amberHot : C::ash);
            repaint();
        };
        b->setColour (TextButton::textColourOffId, ctx.proc.sceneSet (s) ? C::amberHot : C::ash);
        addAndMakeVisible (b);
    }
    ctx.refreshMatrix = [this] { for (auto* r : rows) r->repaint(); };
    ctx.showRoute = [this] (int r) { showRoute (r); };
    buildLfoControls();
}

void ModPage::buildLfoControls()
{
    lfoControls.clear();
    auto add = [this] (Component* c) { lfoControls.add (c); addAndMakeVisible (c); };
    add (new ChoiceBox (ctx, glfoId (curLfo, "wave"), "WAVE"));
    add (new ChoiceBox (ctx, glfoId (curLfo, "div"), "DIVISION"));
    add (new ChoiceBox (ctx, glfoId (curLfo, "polarity"), "POLARITY"));
    add (new Toggle (ctx, glfoId (curLfo, "sync"), "SYNC"));
    add (new ParamKnob (ctx, glfoId (curLfo, "rate"), "RATE"));
    add (new ParamKnob (ctx, glfoId (curLfo, "depth"), "DEPTH"));
    add (new ParamKnob (ctx, glfoId (curLfo, "phase"), "PHASE"));
}

void ModPage::showRoute (int r)
{
    if (ctx.showPage) ctx.showPage (5);
    for (int i = 0; i < rows.size(); ++i) rows[i]->setHighlight (i == r);
    matView.setViewPosition (0, std::max (0, r * 30 - 60));
}

void ModPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (420);
    auto lfoArea = left.removeFromTop (330);
    lfoPanel.setBounds (lfoArea);
    auto lc = lfoPanel.content() + lfoArea.getPosition();
    lfoTabs.setBounds (lc.removeFromTop (22));
    lc.removeFromTop (6);
    auto ctl = lc.removeFromBottom (92);
    auto chipRow = lc.removeFromBottom (22);
    lfoChip.setBounds (chipRow.removeFromLeft (110));
    lc.removeFromBottom (4);
    auto boxes = lc.removeFromRight (130);
    for (int i = 0; i < 3; ++i) lfoControls[i]->setBounds (boxes.removeFromTop (40));
    lfoControls[3]->setBounds (boxes.removeFromTop (24));
    lfoView.setBounds (lc.withTrimmedRight (6));
    layoutRow (ctl, { lfoControls[4], lfoControls[5], lfoControls[6] });
    left.removeFromTop (8);
    srcPanel.setBounds (left);
    auto sc = srcPanel.content() + left.getPosition();
    const int cw = (sc.getWidth() - 8) / 3;
    for (int i = 0; i < chips.size(); ++i)
        chips[i]->setBounds (sc.getX() + (i % 3) * (cw + 4), sc.getY() + (i / 3) * 26, cw, 22);
    r.removeFromLeft (8);
    auto sceneArea = r.removeFromRight (300);
    scenePanel.setBounds (sceneArea);
    auto scc = scenePanel.content() + sceneArea.getPosition();
    xy.setBounds (scc.removeFromTop (scc.getWidth()));
    scc.removeFromTop (8);
    const int bw = scc.getWidth() / 8;
    auto brow = scc.removeFromTop (28);
    for (int s = 0; s < kNumScenes; ++s) sceneBtns[s]->setBounds (brow.getX() + s * bw + 1, brow.getY(), bw - 2, 28);
    scc.removeFromTop (8);
    sceneOn->setBounds (scc.removeFromTop (28));
    scc.removeFromTop (6);
    auto srow = scc.removeFromTop (92);
    layoutRow (srow, { sceneA.get(), sceneFade.get(), sceneB.get() }, 6);
    sceneA->setBounds (sceneA->getBounds().withSizeKeepingCentre (sceneA->getWidth(), 40));
    sceneB->setBounds (sceneB->getBounds().withSizeKeepingCentre (sceneB->getWidth(), 40));
    r.removeFromRight (8);
    matPanel.setBounds (r);
    auto mc = matPanel.content() + r.getPosition();
    matHeader = mc.removeFromTop (16);
    matView.setBounds (mc);
    matContent.setSize (mc.getWidth() - 10, kNumRoutes * 30);
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 30, matContent.getWidth(), 30);
}

void ModPage::paintOverChildren (Graphics& g)
{
    g.setFont (Fonts::label (11.0f));
    g.setColour (C::boneDim);
    auto h = matHeader;
    h.removeFromLeft (52);
    g.drawText (::racoon::i18n::tr ("SOURCE"), h.removeFromLeft (128), Justification::centredLeft);
    g.drawText (::racoon::i18n::tr ("TRACK"), h.removeFromLeft (74), Justification::centredLeft);
    g.drawText (::racoon::i18n::tr ("DESTINATION"), h.removeFromLeft (188), Justification::centredLeft);
    g.drawText (::racoon::i18n::tr ("AMOUNT"), h, Justification::centredLeft);
    auto sb = scenePanel.getBounds().reduced (14, 10);
    g.setFont (Fonts::body (12.0f));
    g.setColour (C::ash);
    g.drawFittedText (::racoon::i18n::tr ("Shift-click a number to store a scene. With SCENES ON the A / B fader blends them. XY X and Y are sources in the matrix."),
                      sb.removeFromBottom (54), Justification::bottomLeft, 3);
}
} // namespace be::ui

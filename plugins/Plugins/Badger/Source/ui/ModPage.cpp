#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

namespace
{
void fillSourceMenu (ComboBox& b)
{
    auto* root = b.getRootMenu();
    const auto& n = modSourceNames();
    root->addItem (1, n[0]);
    PopupMenu envs, lfos, macros, perf;
    for (int i = ModSrc::Env1; i < ModSrc::Env1 + kNumEnvs; ++i) envs.addItem (i + 1, n[i]);
    for (int i = ModSrc::Lfo1; i < ModSrc::Lfo1 + kNumLfos; ++i) lfos.addItem (i + 1, n[i]);
    for (int i = ModSrc::Macro1; i < ModSrc::Macro1 + kNumMacros; ++i) macros.addItem (i + 1, n[i]);
    for (int i = ModSrc::Velocity; i < ModSrc::Count; ++i) perf.addItem (i + 1, n[i]);
    root->addSubMenu ("Envelopes", envs);
    root->addSubMenu ("LFOs", lfos);
    root->addSubMenu ("Macros", macros);
    root->addSubMenu ("Performance", perf);
}

void fillDestMenu (ComboBox& b)
{
    auto* root = b.getRootMenu();
    const auto& n = modDestNames();
    root->addItem (1, n[0]);
    auto range = [&n] (const String& name, int from, int to, PopupMenu& parent)
    {
        PopupMenu m;
        for (int i = from; i < to; ++i) m.addItem (i + 1, n[i]);
        parent.addSubMenu (name, m);
    };
    for (int o = 0; o < kNumOsc; ++o) range ("Osc " + String (o + 1), ModDst::Osc1 + o * ModDst::kOscBlock, ModDst::Osc1 + (o + 1) * ModDst::kOscBlock, *root);
    range ("Sub / Noise", ModDst::SubLevel, ModDst::LayerLevel1, *root);
    range ("Layers", ModDst::LayerLevel1, ModDst::Filter1, *root);
    range ("Filters", ModDst::Filter1, ModDst::AmpVolume, *root);
    range ("Amp / Pitch / Acid", ModDst::AmpVolume, ModDst::LfoRate1, *root);
    range ("LFO Rate", ModDst::LfoRate1, ModDst::LfoDepth1, *root);
    range ("LFO Depth", ModDst::LfoDepth1, ModDst::GlobalStart, *root);
    range ("FX / Global", ModDst::GlobalStart, ModDst::Count, *root);
}
}

// ===================================================================== matrix row
MatrixRow::MatrixRow (Ctx& c, int route)
    : ctx (c), r (route), on (c, routeId (route, "on")),
      curve (c, routeId (route, "curve")), op (c, routeId (route, "op")), quant (c, routeId (route, "quant"), {}, [] (int v) { return v == 0 ? String ("--") : String (v); })
{
    fillSourceMenu (src); fillSourceMenu (via); fillDestMenu (dst);
    srcA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (c.apvts, routeId (r, "src"), src);
    viaA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (c.apvts, routeId (r, "via"), via);
    dstA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (c.apvts, routeId (r, "dst"), dst);
    amount.setSliderStyle (Slider::LinearHorizontal);
    amount.setTextBoxStyle (Slider::TextBoxRight, false, 44, 18);
    amount.getProperties().set ("bipolar", true);
    amtA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (c.apvts, routeId (r, "amt"), amount);
    amount.setTextValueSuffix ("");
    amount.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    amount.updateText();
    smooth.setSliderStyle (Slider::LinearHorizontal);
    smooth.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    smA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (c.apvts, routeId (r, "smooth"), smooth);
    src.setTooltip ("Source"); via.setTooltip ("Via: multiplies the source (modulation of modulation)"); dst.setTooltip ("Destination");
    smooth.setTooltip ("Smoothing"); quant.setTooltip ("Quantize steps");
    for (Component* comp : std::initializer_list<Component*> { &on, &src, &via, &dst, &amount, &curve, &op, &quant, &smooth }) addAndMakeVisible (comp);
    ctx.registerAnimated (this);
}

MatrixRow::~MatrixRow() { ctx.unregisterAnimated (this); }

void MatrixRow::resized()
{
    auto b = getLocalBounds().reduced (2, 2);
    b.removeFromLeft (26);
    on.setBounds (b.removeFromLeft (22));
    const int w = b.getWidth();
    src.setBounds (b.removeFromLeft (w * 15 / 100).reduced (2, 0));
    via.setBounds (b.removeFromLeft (w * 12 / 100).reduced (2, 0));
    dst.setBounds (b.removeFromLeft (w * 20 / 100).reduced (2, 0));
    amount.setBounds (b.removeFromLeft (w * 20 / 100).reduced (2, 0));
    curve.setBounds (b.removeFromLeft (w * 9 / 100).reduced (2, 0));
    op.setBounds (b.removeFromLeft (w * 8 / 100).reduced (2, 0));
    quant.setBounds (b.removeFromLeft (w * 6 / 100).reduced (2, 0));
    smooth.setBounds (b.removeFromLeft (w * 7 / 100).reduced (2, 0));
}

void MatrixRow::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (highlight ? S::hi.withAlpha (0.45f) : (r % 2 ? S::ink.withAlpha (0.05f) : Colours::transparentBlack));
    g.fillRect (b);
    g.setFont (Fonts::mono (11.0f));
    const bool used = (int) ctx.getParam (routeId (r, "src")) != 0;
    g.setColour (used ? S::ink : S::inkFaint);
    g.drawText (::racoon::i18n::tr (String (r + 1).paddedLeft ('0', 2)), b.removeFromLeft (24.0f), Justification::centred);
    // live meter on the right
    auto m = b.removeFromRight (b.getWidth() * 0.03f + 4.0f).reduced (2.0f, 6.0f);
    g.setColour (S::ink); g.fillRect (m);
    g.setColour (C::cyan);
    const float v = jlimit (-1.0f, 1.0f, live);
    g.fillRect (m.withTrimmedTop (m.getHeight() * (1.0f - std::abs (v))));
}

void MatrixRow::animate()
{
    if (! animVisible (this)) return;
    const int s = (int) ctx.getParam (routeId (r, "src"));
    const float v = s > 0 ? ctx.proc.engine.telemetry.srcDisplay[s].load() * ctx.getParam (routeId (r, "amt")) : 0.0f;
    if (std::abs (v - live) > 0.01f) { live = v; repaint(); }
}

// ===================================================================== mod page
ModPage::ModPage (Ctx& c)
    : ctx (c),
      envTabs ({ "1", "2", "3", "4", "5", "6", "7", "8" }, [this] (int i) { curEnv = i; buildEnvControls(); }),
      lfoTabsA ({ "1", "2", "3", "4", "5", "6", "7", "8" }, [this] (int i) { curLfo = i; lfoTabsB.setSelected (-1); buildLfoControls(); }),
      lfoTabsB ({ "9", "10", "11", "12", "13", "14", "15", "16" }, [this] (int i) { curLfo = 8 + i; lfoTabsA.setSelected (-1); buildLfoControls(); }),
      envView (c), lfoView (c), envChip (c, ModSrc::Env1), lfoChip (c, ModSrc::Lfo1),
      ampEnv (c, "amp_env", "AMP ENVELOPE")
{
    for (Component* comp : std::initializer_list<Component*> { &envPanel, &lfoPanel, &srcPanel, &matPanel, &envTabs, &lfoTabsA, &lfoTabsB, &envView, &lfoView, &envChip, &lfoChip, &ampEnv, &matView })
        addAndMakeVisible (comp);
    lfoTabsB.setSelected (-1);
    // sources rack
    for (int s = 1; s < ModSrc::Count; ++s)
    {
        auto* ch = chips.add (new ModChip (c, s));
        addAndMakeVisible (ch);
    }
    // matrix rows
    for (int i = 0; i < kNumRoutes; ++i)
    {
        auto* row = rows.add (new MatrixRow (c, i));
        matContent.addAndMakeVisible (row);
    }
    matView.setViewedComponent (&matContent, false);
    matView.setScrollBarsShown (true, false);
    buildEnvControls();
    buildLfoControls();
}

void ModPage::showRoute (int r)
{
    for (auto* row : rows) row->setHighlight (false);
    if (r >= 0 && r < rows.size())
    {
        rows[r]->setHighlight (true);
        matView.setViewPosition (0, std::max (0, rows[r]->getY() - 40));
    }
}

void ModPage::buildEnvControls()
{
    envControls.clear();
    const int e = curEnv;
    auto k = [&] (const char* id, const char* l) { auto* c = envControls.add (new ParamKnob (ctx, envId (e, id), l)); addAndMakeVisible (c); };
    k ("delay", "DELAY"); k ("attack", "ATTACK"); k ("hold", "HOLD"); k ("decay", "DECAY"); k ("sustain", "SUSTAIN"); k ("release", "RELEASE");
    k ("acurve", "A CURVE"); k ("dcurve", "D CURVE"); k ("rcurve", "R CURVE"); k ("velsens", "VELOCITY"); k ("keyscale", "KEY SCALE"); k ("drawlen", "DRAW LEN");
    auto* mode = envControls.add (new ChoiceBox (ctx, envId (e, "mode"), "MODE")); addAndMakeVisible (mode);
    auto* loop = envControls.add (new ChoiceBox (ctx, envId (e, "loop"), "LOOP")); addAndMakeVisible (loop);
    auto* retrig = envControls.add (new Toggle (ctx, envId (e, "retrig"), "LEGATO RETRIG")); addAndMakeVisible (retrig);
    auto* sync = envControls.add (new Toggle (ctx, envId (e, "sync"), "BPM SYNC")); addAndMakeVisible (sync);
    envView.setEnv (e);
    envChip.setSource (ModSrc::Env1 + e, "ENV " + String (e + 1));
    envPanel.setTitle ("ENVELOPE " + String (e + 1) + (e == 0 ? "  /  AMP" : (e == 1 ? "  /  FILTER" : (e == 2 ? "  /  PITCH" : ""))));
    if (! getLocalBounds().isEmpty()) resized();
}

void ModPage::buildLfoControls()
{
    lfoControls.clear();
    const int l = curLfo;
    auto k = [&] (const char* id, const char* lb) { auto* c = lfoControls.add (new ParamKnob (ctx, lfoId (l, id), lb)); addAndMakeVisible (c); };
    k ("rate", "RATE"); k ("phase", "PHASE"); k ("offset", "OFFSET"); k ("depth", "DEPTH"); k ("smooth", "SMOOTH"); k ("fadein", "FADE IN"); k ("fadeout", "FADE OUT");
    auto b = [&] (const char* id, const char* lb) { auto* c = lfoControls.add (new ChoiceBox (ctx, lfoId (l, id), lb)); addAndMakeVisible (c); };
    b ("wave", "WAVE"); b ("div", "DIVISION"); b ("trig", "TRIGGER"); b ("scope", "SCOPE"); b ("polarity", "POLARITY");
    auto* sync = lfoControls.add (new Toggle (ctx, lfoId (l, "sync"), "BPM SYNC")); addAndMakeVisible (sync);
    auto* one = lfoControls.add (new Toggle (ctx, lfoId (l, "oneshot"), "ONE SHOT")); addAndMakeVisible (one);
    lfoView.setLfo (l);
    lfoChip.setSource (ModSrc::Lfo1 + l, "LFO " + String (l + 1));
    lfoPanel.setTitle ("LFO " + String (l + 1));
    if (! getLocalBounds().isEmpty()) resized();
}

void ModPage::layoutEnv (Rectangle<int> r)
{
    envPanel.setBounds (r);
    auto c = envPanel.content() + r.getPosition();
    auto top = c.removeFromTop (22);
    envChip.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    envTabs.setBounds (top);
    c.removeFromTop (6);
    envView.setBounds (c.removeFromTop (120));
    c.removeFromTop (4);
    const int rh = 86;
    auto row1 = c.removeFromTop (rh), row2 = c.removeFromTop (rh);
    for (int i = 0; i < 6; ++i)
    {
        envControls[i]->setBounds (row1.getX() + i * row1.getWidth() / 6, row1.getY(), row1.getWidth() / 6, rh);
        envControls[6 + i]->setBounds (row2.getX() + i * row2.getWidth() / 6, row2.getY(), row2.getWidth() / 6, rh);
    }
    auto last = c.removeFromTop (38);
    const int w = last.getWidth() / 5 - 6;
    envControls[12]->setBounds (last.removeFromLeft (w).reduced (3, 0));
    envControls[13]->setBounds (last.removeFromLeft (w).reduced (3, 0));
    ampEnv.setBounds (last.removeFromLeft (w).reduced (3, 0));
    envControls[14]->setBounds (last.removeFromLeft (w + 24).withTrimmedTop (14));
    envControls[15]->setBounds (last.withTrimmedTop (14));
}

void ModPage::layoutLfo (Rectangle<int> r)
{
    lfoPanel.setBounds (r);
    auto c = lfoPanel.content() + r.getPosition();
    auto top = c.removeFromTop (22);
    lfoChip.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    lfoTabsA.setBounds (top.removeFromLeft (top.getWidth() / 2));
    lfoTabsB.setBounds (top);
    c.removeFromTop (6);
    lfoView.setBounds (c.removeFromTop (120));
    c.removeFromTop (4);
    auto row1 = c.removeFromTop (86);
    for (int i = 0; i < 7; ++i) lfoControls[i]->setBounds (row1.getX() + i * row1.getWidth() / 7, row1.getY(), row1.getWidth() / 7, 86);
    c.removeFromTop (6);
    auto row2 = c.removeFromTop (38);
    for (int i = 0; i < 5; ++i) lfoControls[7 + i]->setBounds (row2.getX() + i * row2.getWidth() / 5 + 2, row2.getY(), row2.getWidth() / 5 - 4, 38);
    c.removeFromTop (6);
    auto row3 = c.removeFromTop (24);
    lfoControls[12]->setBounds (row3.removeFromLeft (row3.getWidth() / 2));
    lfoControls[13]->setBounds (row3);
}

void ModPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (420);
    const int w = (top.getWidth() - 16) / 3;
    layoutEnv (top.removeFromLeft (w + 40));
    top.removeFromLeft (8);
    layoutLfo (top.removeFromLeft (w + 20));
    top.removeFromLeft (8);
    srcPanel.setBounds (top);
    auto sc = srcPanel.content() + top.getPosition();
    const int cols = 4;
    const int ch = 21;
    for (int i = 0; i < chips.size(); ++i)
    {
        const int col = i % cols, row = i / cols;
        chips[i]->setBounds (sc.getX() + col * sc.getWidth() / cols + 2, sc.getY() + row * (ch + 3), sc.getWidth() / cols - 4, ch);
    }
    r.removeFromTop (8);
    matPanel.setBounds (r);
    auto mc = matPanel.content() + r.getPosition();
    matHeader = mc.removeFromTop (14);
    matView.setBounds (mc);
    const int rowH = 28;
    matContent.setSize (mc.getWidth() - 10, rowH * kNumRoutes);
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * rowH, matContent.getWidth(), rowH);
}
} // namespace be::ui

namespace be::ui
{
void ModPage::paintOverChildren (juce::Graphics& g)
{
    auto b = matHeader.reduced (2, 0);
    b.removeFromLeft (26 + 22);
    const int w = b.getWidth() - 10;
    const char* names[] { "SOURCE", "VIA", "DESTINATION", "AMOUNT", "CURVE", "OP", "QUANT", "SMOOTH" };
    const int pct[] { 15, 12, 20, 20, 9, 8, 6, 7 };
    for (int i = 0; i < 8; ++i)
    {
        auto c = b.removeFromLeft (w * pct[i] / 100);
        Draw::engraved (g, names[i], c.toFloat().reduced (3.0f, 0.0f), Fonts::label (10.5f), juce::Justification::centredLeft, C::boneDim);
    }
}
}

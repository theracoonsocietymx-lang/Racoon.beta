#include "Widgets.h"
#include "I18n.h"
#include "Help.h"

namespace be::ui
{
using namespace juce;
bool gForceAnimation = false;

// ===================================================================== context
Ctx::Ctx (BassEngineProcessor& p) : proc (p), apvts (p.apvts)
{
    for (int d = 1; d < ModDst::Count; ++d)
    {
        auto id = ModDst::paramIdFor (d);
        if (id.isNotEmpty()) destMap[id] = d;
    }
}

int Ctx::destFor (const String& id) const
{
    auto it = destMap.find (id);
    return it != destMap.end() ? it->second : 0;
}

void Ctx::setParam (const String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

int Ctx::addRoute (int src, int dst, float amount)
{
    // reuse an existing identical route
    for (int i = 0; i < kNumRoutes; ++i)
        if ((int) getParam (routeId (i, "src")) == src && (int) getParam (routeId (i, "dst")) == dst) return i;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        if ((int) getParam (routeId (i, "src")) != 0) continue;
        proc.undo.beginNewTransaction ("Add modulation");
        setParam (routeId (i, "on"), 1.0f);
        setParam (routeId (i, "src"), (float) src);
        setParam (routeId (i, "via"), 0.0f);
        setParam (routeId (i, "dst"), (float) dst);
        setParam (routeId (i, "amt"), amount);
        setParam (routeId (i, "curve"), 0.0f);
        setParam (routeId (i, "op"), 0.0f);
        setParam (routeId (i, "quant"), 0.0f);
        setParam (routeId (i, "smooth"), 0.0f);
        if (refreshMatrix) refreshMatrix();
        return i;
    }
    return -1;
}

void Ctx::removeRoutesTo (int dst)
{
    proc.undo.beginNewTransaction ("Remove modulation");
    for (int i = 0; i < kNumRoutes; ++i)
        if ((int) getParam (routeId (i, "dst")) == dst)
        {
            setParam (routeId (i, "src"), 0.0f);
            setParam (routeId (i, "dst"), 0.0f);
            setParam (routeId (i, "via"), 0.0f);
            setParam (routeId (i, "amt"), 0.0f);
        }
    if (refreshMatrix) refreshMatrix();
}

void Ctx::routesTo (int dst, Array<int>& out) const
{
    for (int i = 0; i < kNumRoutes; ++i)
        if ((int) getParam (routeId (i, "dst")) == dst && (int) getParam (routeId (i, "src")) != 0) out.add (i);
}

float Ctx::routeAmountTo (int dst) const
{
    float s = 0.0f;
    for (int i = 0; i < kNumRoutes; ++i)
        if ((int) getParam (routeId (i, "dst")) == dst && (int) getParam (routeId (i, "src")) != 0 && getParam (routeId (i, "on")) > 0.5f)
            s += getParam (routeId (i, "amt"));
    return s;
}

// ===================================================================== knob
Knob::Knob (Ctx& c, const String& paramId) : ctx (c), pid (paramId)
{
    setSliderStyle (RotaryHorizontalVerticalDrag);
    setTextBoxStyle (NoTextBox, true, 0, 0);
    setRotaryParameters (MathConstants<float>::pi * 1.22f, MathConstants<float>::pi * 2.78f, true);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (ctx.apvts, pid, *this);
    if (auto* p = ctx.apvts.getParameter (pid))
    {
        const auto r = p->getNormalisableRange();
        getProperties().set ("bipolar", r.start < 0.0f && r.end > 0.0f && std::abs (r.start + r.end) < 1.0e-3f * (r.end - r.start));
        setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
        setTooltip (helpFor (pid, p->getName (64)));
    }
    dest = ctx.destFor (pid);
    if (dest > 0) ctx.registerAnimated (this);
    setPopupDisplayEnabled (false, false, nullptr);
}

Knob::~Knob() { ctx.unregisterAnimated (this); }

void Knob::paint (Graphics& g)
{
    Slider::paint (g);
    if (ctx.proc.automation.hasPoints (pid))
    {
        // a pink dot: this knob has an automation line
        auto dot = getLocalBounds().toFloat().removeFromTop (9.0f).removeFromRight (9.0f);
        g.setColour (Brand::rosa);
        g.fillEllipse (dot);
        g.setColour (S::ink);
        g.drawEllipse (dot, 1.0f);
    }
    if (dest > 0 && (lastStatic != 0.0f || std::abs (lastMod) > 1.0e-4f))
    {
        auto b = getLocalBounds().toFloat().reduced (3.0f);
        const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f + 2.0f;
        const auto c = b.getCentre();
        const auto rp = getRotaryParameters();
        auto* p = ctx.apvts.getParameter (pid);
        const float norm = p != nullptr ? p->getValue() : 0.0f;
        auto angOf = [&] (float v) { return rp.startAngleRadians + jlimit (0.0f, 1.0f, v) * (rp.endAngleRadians - rp.startAngleRadians); };
        // static modulation depth
        if (lastStatic != 0.0f)
        {
            Path arc;
            const float a0 = angOf (norm), a1 = angOf (norm + lastStatic);
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
            g.setColour (C::cyan.withAlpha (0.35f));
            g.strokePath (arc, PathStrokeType (2.0f));
        }
        // live modulated value
        const float a = angOf (norm + lastMod);
        const Point<float> pt (c.x + std::sin (a) * r, c.y - std::cos (a) * r);
        g.setColour (C::cyan.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (pt));
        g.setColour (C::cyan);
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (pt));
    }
    if (dropHover)
    {
        g.setColour (C::cyan.withAlpha (0.6f));
        g.drawEllipse (getLocalBounds().toFloat().reduced (1.0f), 1.5f);
    }
    if (ctx.proc.isLearning() && isMouseOver())
    {
        g.setColour (C::jade);
        g.drawEllipse (getLocalBounds().toFloat().reduced (1.0f), 1.0f);
    }
}

void Knob::animate()
{
    if (! animVisible (this)) return;
    const float st = ctx.routeAmountTo (dest);
    const float m = ctx.proc.engine.telemetry.modDisplay[dest].load (std::memory_order_relaxed);
    if (std::abs (m - lastMod) > 0.002f || st != lastStatic)
    {
        lastMod = m; lastStatic = st;
        repaint();
    }
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    ctx.proc.undo.beginNewTransaction();
    if (e.mods.isShiftDown()) setMouseDragSensitivity (1600);
    Slider::mouseDown (e);
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    setMouseDragSensitivity (e.mods.isShiftDown() ? 1600 : 220);
    Slider::mouseDrag (e);
}

void Knob::mouseUp (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) Slider::mouseUp (e);
    setMouseDragSensitivity (220);
}

bool Knob::isInterestedInDragSource (const SourceDetails& d) { return dest > 0 && d.description.toString().startsWith ("modsrc:"); }

void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (src > 0 && dest > 0) ctx.addRoute (src, dest, 0.3f);
    repaint();
}

void Knob::showMenu()
{
    PopupMenu m;
    auto* p = ctx.apvts.getParameter (pid);
    m.addSectionHeader (p != nullptr ? p->getName (40) : pid);
    const auto learned = ctx.proc.midiLearnFor (pid);
    m.addItem (1, "Aprender MIDI");
    m.addItem (2, "Quitar MIDI " + (learned.isNotEmpty() ? "(" + learned + ")" : String()), learned.isNotEmpty());
    m.addItem (3, "Valor inicial");
    m.addSeparator();
    racoon::addAutomationItems (m, ctx.proc.automation, pid);
    m.addSeparator();
    if (dest > 0)
    {
        Array<int> routes; ctx.routesTo (dest, routes);
        PopupMenu mods;
        for (auto r : routes)
        {
            const int src = (int) ctx.getParam (routeId (r, "src"));
            mods.addItem (100 + r, "Editar ruta " + String (r + 1) + ": " + modSourceNames()[src] + "  " + String (roundToInt (ctx.getParam (routeId (r, "amt")) * 100.0f)) + "%");
        }
        if (routes.size() > 0) { m.addSubMenu ("Modulaciones", mods); m.addItem (4, "Quitar todas las modulaciones"); }
        PopupMenu add;
        const int quick[] { ModSrc::Env1 + 1, ModSrc::Env1 + 2, ModSrc::Lfo1, ModSrc::Lfo1 + 1, ModSrc::Macro1, ModSrc::Macro1 + 1, ModSrc::Velocity, ModSrc::ModWheel, ModSrc::Evolution, ModSrc::SeqMod };
        for (auto q : quick) add.addItem (1000 + q, modSourceNames()[q]);
        m.addSubMenu (String::fromUTF8 ("Agregar modulación"), add);
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (racoon::handleAutomationMenu (r, *this, pid)) { repaint(); return; }
        if (r == 1) ctx.proc.startMidiLearn (pid);
        else if (r == 2) ctx.proc.clearMidiLearn (pid);
        else if (r == 3) { if (auto* p = ctx.apvts.getParameter (pid)) p->setValueNotifyingHost (p->getDefaultValue()); }
        else if (r == 4) ctx.removeRoutesTo (dest);
        else if (r >= 1000) ctx.addRoute (r - 1000, dest, 0.3f);
        else if (r >= 100 && ctx.showRoute) ctx.showRoute (r - 100);
        repaint();
    });
}

// ===================================================================== param knob
ParamKnob::ParamKnob (Ctx& c, const String& paramId, const String& l) : knob (c, paramId), ctx (c), label (l)
{
    addAndMakeVisible (knob);
    knob.onValueChange = [this] { repaint(); };
    ctx.registerAnimated (this);
}

ParamKnob::~ParamKnob() { ctx.unregisterAnimated (this); }

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (13);
    r.removeFromBottom (13);
    const int s = std::min (r.getWidth(), r.getHeight());
    knob.setBounds (r.withSizeKeepingCentre (s, s));
}

void ParamKnob::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Draw::engraved (g, label.toUpperCase(), r.removeFromTop (13.0f), Fonts::label (11.0f), Justification::centred, S::ink);
    auto* p = ctx.apvts.getParameter (knob.getParamId());
    const bool active = knob.isMouseOverOrDragging();
    g.setFont (Fonts::mono (11.0f));
    g.setColour (active ? S::ink : S::inkSoft);
    lastText = p != nullptr ? p->getCurrentValueAsText() : String();
    g.drawText (::racoon::i18n::tr (lastText), r.removeFromBottom (13.0f), Justification::centred, false);
}

void ParamKnob::animate()
{
    if (! animVisible (this)) return;
    auto* p = ctx.apvts.getParameter (knob.getParamId());
    if (p != nullptr && p->getCurrentValueAsText() != lastText) repaint();
}

// ===================================================================== choice box
ChoiceBox::ChoiceBox (Ctx& c, const String& paramId, const String& l) : label (l)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (c.apvts.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (c.apvts, paramId, box);
    box.onPopupMenu = [this, paramId] { racoon::showParamMenu (*this, paramId); };
}

void ChoiceBox::resized()
{
    auto r = getLocalBounds();
    if (label.isNotEmpty()) r.removeFromTop (13);
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 22)));
}

void ChoiceBox::paint (Graphics& g)
{
    if (label.isNotEmpty()) Draw::engraved (g, label.toUpperCase(), getLocalBounds().removeFromTop (13).toFloat(), Fonts::label (11.0f), Justification::centredLeft, S::ink);
}

// ===================================================================== toggle
Toggle::Toggle (Ctx& c, const String& paramId, const String& text) : pid (paramId)
{
    setButtonText (text);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (c.apvts, paramId, *this);
    if (auto* p = c.apvts.getParameter (paramId)) setTooltip (p->getName (64));
}

void Toggle::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { racoon::showParamMenu (*this, pid); return; }
    ToggleButton::mouseDown (e);
}

void Toggle::mouseUp (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) ToggleButton::mouseUp (e);
}

// ===================================================================== stepper
Stepper::Stepper (Ctx& c, const String& paramId, const String& l, std::function<String (int)> f)
    : ctx (c), pid (paramId), label (l), fmt (std::move (f)), param (c.apvts.getParameter (paramId))
{
    ctx.apvts.addParameterListener (pid, this);
    if (param != nullptr) setTooltip (param->getName (64));
}

Stepper::~Stepper() { ctx.apvts.removeParameterListener (pid, this); }

int Stepper::getInt() const { return param != nullptr ? (int) std::round (param->convertFrom0to1 (param->getValue())) : 0; }

void Stepper::setInt (int v)
{
    if (param == nullptr) return;
    const auto r = param->getNormalisableRange();
    v = jlimit ((int) r.start, (int) r.end, v);
    param->setValueNotifyingHost (param->convertTo0to1 ((float) v));
}

void Stepper::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (label.isNotEmpty()) Draw::engraved (g, label.toUpperCase(), r.removeFromTop (13.0f), Fonts::label (11.0f), Justification::centred, S::ink);
    auto box = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 22.0f));
    g.setColour (S::card); g.fillRect (box);
    g.setColour (S::ink); g.drawRect (box, 1.5f);
    auto lb = box.removeFromLeft (15.0f), rb = box.removeFromRight (15.0f);
    g.setColour (S::hi); g.fillRect (lb.reduced (1.5f)); g.fillRect (rb.reduced (1.5f));
    g.setColour (S::ink);
    g.drawVerticalLine ((int) lb.getRight(), lb.getY(), lb.getBottom());
    g.drawVerticalLine ((int) rb.getX(), rb.getY(), rb.getBottom());
    g.setFont (Fonts::label (13.0f));
    g.drawText (::racoon::i18n::tr ("<"), lb, Justification::centred);
    g.drawText (::racoon::i18n::tr (">"), rb, Justification::centred);
    const int v = getInt();
    g.setFont (Fonts::mono (14.0f));
    g.setColour (S::ink);
    g.drawText (::racoon::i18n::tr (fmt ? fmt (v) : String (v)), box, Justification::centred);
}

void Stepper::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { racoon::showParamMenu (*this, pid); return; }
    dragStartValue = getInt();
    ctx.proc.undo.beginNewTransaction();
    if (e.x < 16) setInt (getInt() - 1);
    else if (e.x > getWidth() - 16) setInt (getInt() + 1);
}

void Stepper::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (e.mouseDownPosition.x < 16 || e.mouseDownPosition.x > getWidth() - 16) return;
    setInt (dragStartValue - e.getDistanceFromDragStartY() / 6);
}

void Stepper::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) { setInt (getInt() + (w.deltaY > 0 ? 1 : -1)); }

// ===================================================================== mod chip
ModChip::ModChip (Ctx& c, int source, const String& t) : ctx (c), src (source), text (t)
{
    if (text.isEmpty()) text = modSourceNames()[src].toUpperCase();
    setTooltip ("Drag onto any knob to modulate it");
    setMouseCursor (MouseCursor::DraggingHandCursor);
    ctx.registerAnimated (this);
}

ModChip::~ModChip() { ctx.unregisterAnimated (this); }

void ModChip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (C::void_); g.fillRect (r);
    g.setColour (isMouseOver() ? C::cyan : C::cyanDim); g.drawRect (r, 1.0f);
    // live value bar
    const float v = jlimit (-1.0f, 1.0f, last);
    auto bar = r.reduced (2.0f).removeFromBottom (2.0f);
    g.setColour (C::cyan.withAlpha (0.8f));
    if (ModSrc::isBipolar (src)) g.fillRect (bar.withWidth (bar.getWidth() * 0.5f * std::abs (v)).withX (v >= 0 ? bar.getCentreX() : bar.getCentreX() - bar.getWidth() * 0.5f * std::abs (v)));
    else g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, v)));
    // grip glyph
    Path d; d.addPolygon ({ r.getX() + 8.0f, r.getCentreY() - 1.0f }, 4, 3.5f, MathConstants<float>::pi / 4.0f);
    g.setColour (C::cyan); g.fillPath (d);
    g.setFont (Fonts::label (11.5f));
    g.setColour (C::bone);
    g.drawFittedText (::racoon::i18n::tr (text), r.withTrimmedLeft (15.0f).withTrimmedBottom (2.0f).toNearestInt(), Justification::centredLeft, 1, 0.7f);
}

void ModChip::mouseDrag (const MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4) return;
    if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive())
        {
            Image img (Image::ARGB, getWidth(), getHeight(), true);
            Graphics g (img);
            paint (g);
            dc->startDragging ("modsrc:" + String (src), this, ScaledImage (img), true);
        }
}

void ModChip::animate()
{
    if (! animVisible (this)) return;
    const float v = ctx.proc.engine.telemetry.srcDisplay[src].load (std::memory_order_relaxed);
    if (std::abs (v - last) > 0.01f) { last = v; repaint(); }
}

// ===================================================================== tabs
TabStrip::TabStrip (StringArray n, std::function<void (int)> cb) : names (std::move (n)), onChange (std::move (cb)) {}

void TabStrip::paint (Graphics& g)
{
    const int n = names.size();
    if (n == 0) return;
    const float w = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (2.0f, 0.0f);
        const bool s = i == sel;
        Draw::box (g, r, s ? S::hi : S::card, 1.5f, 2.0f);
        g.setFont (Fonts::label (std::min (13.0f, (float) getHeight() * 0.6f)));
        g.setColour (s ? S::ink : S::inkSoft);
        g.drawFittedText (::racoon::i18n::tr (names[i]), r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f).toNearestInt(), Justification::centred, 1, 0.6f);
    }
}

void TabStrip::mouseDown (const MouseEvent& e)
{
    const int n = names.size();
    if (n == 0) return;
    const int i = jlimit (0, n - 1, (int) ((float) e.x / ((float) getWidth() / (float) n)));
    sel = i;
    repaint();
    if (onChange) onChange (i);
}

// ===================================================================== play button
PlayButton::PlayButton (Ctx& c, bool b) : ctx (c), big (b)
{
    setTooltip ("Toca el patrón del piano roll. No necesitas sostener una tecla ni darle play en tu DAW. Barra espaciadora hace lo mismo."_u);
    setMouseCursor (MouseCursor::PointingHandCursor);
    ctx.registerAnimated (this);
}

PlayButton::~PlayButton() { ctx.unregisterAnimated (this); }

void PlayButton::toggle (Ctx& c)
{
    const bool on = ! c.proc.seqPreview.load();
    if (on && (int) c.getParam ("seq_mode") != 1) c.setParam ("seq_mode", 1.0f);   // Sequencer
    c.proc.seqPreview = on;
}

void PlayButton::mouseDown (const MouseEvent&) { toggle (ctx); repaint(); }

void PlayButton::animate()
{
    const bool on = ctx.proc.seqPreview.load();
    if (on != lastState) { lastState = on; repaint(); }
}

void PlayButton::paint (Graphics& g)
{
    const bool on = ctx.proc.seqPreview.load();
    auto r = getLocalBounds().toFloat();
    const float sh = big ? 5.0f : 3.0f;
    Draw::box (g, r, on ? S::accent() : (isMouseOver() ? Colours::white : S::hi), 2.0f, sh);
    auto body = r.withTrimmedRight (sh).withTrimmedBottom (sh);
    const float ih = body.getHeight() * (big ? 0.38f : 0.42f);
    auto icon = body.removeFromLeft (body.getHeight()).withSizeKeepingCentre (ih, ih);
    g.setColour (S::ink);
    if (on) g.fillRect (icon.reduced (ih * 0.06f));
    else { Path t; t.addTriangle (icon.getX(), icon.getY(), icon.getX(), icon.getBottom(), icon.getRight(), icon.getCentreY()); g.fillPath (t); }
    g.setFont (big ? Fonts::title (std::min (22.0f, body.getHeight() * 0.42f)) : Fonts::label (std::min (14.0f, body.getHeight() * 0.55f)));
    g.drawFittedText (::racoon::i18n::tr (on ? (big ? "DETENER" : "STOP") : (big ? "TOCAR PATRÓN"_u : "PLAY")), body.withTrimmedRight (8.0f).toNearestInt(), Justification::centredLeft, 1, 0.7f);
}

void layoutRow (Rectangle<int> r, std::initializer_list<Component*> comps, int gap)
{
    const int n = (int) comps.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    int x = r.getX();
    for (auto* c : comps)
    {
        if (c != nullptr) c->setBounds (x, r.getY(), w, r.getHeight());
        x += w + gap;
    }
}
} // namespace be::ui

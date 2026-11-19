#include "Widgets.h"
#include "I18n.h"
#include "Help.h"

namespace be::ui
{
using namespace juce;
bool gForceAnimation = false;

// < value > box used by the steppers; leaves box as the middle area
static void stepperBox (Graphics& g, Rectangle<float>& box, bool enabled)
{
    g.setColour (enabled ? S::card : S::cardDim); g.fillRect (box);
    g.setColour (S::ink); g.drawRect (box, 1.5f);
    auto lb = box.removeFromLeft (15.0f), rb = box.removeFromRight (15.0f);
    g.setColour (enabled ? S::hi : S::cardDim); g.fillRect (lb.reduced (1.5f)); g.fillRect (rb.reduced (1.5f));
    g.setColour (S::ink);
    g.drawVerticalLine ((int) lb.getRight(), lb.getY(), lb.getBottom());
    g.drawVerticalLine ((int) rb.getX(), rb.getY(), rb.getBottom());
    g.setFont (Fonts::label (13.0f));
    g.drawText (::racoon::i18n::tr ("<"), lb, Justification::centred);
    g.drawText (::racoon::i18n::tr (">"), rb, Justification::centred);
}

void slab (Graphics& g, Rectangle<float> r)
{
    g.setColour (C::void_);
    g.fillRect (r);
    g.setColour (S::ink);
    g.drawRect (r, 2.0f);
    g.setColour (C::ridge.withAlpha (0.4f));
    g.drawLine (r.getX(), r.getBottom() + 0.5f, r.getRight(), r.getBottom() + 0.5f, 0.6f);
    g.setColour (Colours::white.withAlpha (0.025f));
    for (int k = 1; k < 8; ++k) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 8.0f), r.getY(), r.getBottom());
    for (int k = 1; k < 4; ++k) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.0f), r.getX(), r.getRight());
}

juce::String fmtStepIndex (int v) { return String (v); }

// ===================================================================== context
Ctx::Ctx (DrumEngineProcessor& p) : proc (p), apvts (p.apvts)
{
    for (int i = 0; i < kNumRoutes; ++i)
        rp[i] = { apvts.getRawParameterValue (routeId (i, "on")), apvts.getRawParameterValue (routeId (i, "src")), apvts.getRawParameterValue (routeId (i, "trk")),
                  apvts.getRawParameterValue (routeId (i, "dst")), apvts.getRawParameterValue (routeId (i, "amt")) };
    for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d)
    {
        auto id = ModDst::globalParamId (d);
        if (id.isNotEmpty()) destMap[id] = d;
    }
}

void Ctx::selectTrack (int t)
{
    t = jlimit (0, kNumTracks - 1, t);
    if (t == proc.selectedTrack) return;
    proc.selectedTrack = t;
    proc.engine.telemetry.selectedTrack = t;
    selStep = -1;
    for (auto& f : trackListeners) f();
}

int Ctx::destFor (const String& id, int* trackOut) const
{
    int local = -1;
    const int t = trackOfParamId (id, &local);
    if (t >= 0)
    {
        if (trackOut != nullptr) *trackOut = t;
        return ModDst::TrackBase + local;
    }
    if (trackOut != nullptr) *trackOut = -1;
    auto it = destMap.find (id);
    return it != destMap.end() ? it->second : 0;
}

void Ctx::setParam (const String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

int Ctx::addRoute (int src, int trk, int dst, float amount)
{
    const int trkVal = trk < 0 ? 0 : trk + 1;
    for (int i = 0; i < kNumRoutes; ++i)
        if ((int) getParam (routeId (i, "src")) == src && (int) getParam (routeId (i, "dst")) == dst && (int) getParam (routeId (i, "trk")) == trkVal) return i;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        if ((int) getParam (routeId (i, "src")) != 0 || (int) getParam (routeId (i, "dst")) != 0) continue;
        proc.undo.beginNewTransaction ("Add modulation");
        setParam (routeId (i, "on"), 1.0f);
        setParam (routeId (i, "src"), (float) src);
        setParam (routeId (i, "trk"), (float) trkVal);
        setParam (routeId (i, "dst"), (float) dst);
        setParam (routeId (i, "amt"), amount);
        setParam (routeId (i, "curve"), 0.0f);
        if (refreshMatrix) refreshMatrix();
        return i;
    }
    return -1;
}

void Ctx::removeRoutesTo (int dst, int trk)
{
    proc.undo.beginNewTransaction ("Remove modulation");
    Array<int> rs;
    routesTo (dst, trk, rs);
    for (auto i : rs)
    {
        setParam (routeId (i, "src"), 0.0f);
        setParam (routeId (i, "dst"), 0.0f);
        setParam (routeId (i, "trk"), 0.0f);
        setParam (routeId (i, "amt"), 0.0f);
    }
    if (refreshMatrix) refreshMatrix();
}

void Ctx::routesTo (int dst, int trk, Array<int>& out) const
{
    for (int i = 0; i < kNumRoutes; ++i)
    {
        if ((int) rp[i].dst->load() != dst || (int) rp[i].src->load() == 0) continue;
        const int rt = (int) rp[i].trk->load();
        if (trk < 0 || rt == 0 || rt == trk + 1) out.add (i);
    }
}

float Ctx::routeAmountTo (int dst, int trk) const
{
    float s = 0.0f;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        if ((int) rp[i].dst->load() != dst || (int) rp[i].src->load() == 0 || rp[i].on->load() < 0.5f) continue;
        const int rt = (int) rp[i].trk->load();
        if (trk < 0 || rt == 0 || rt == trk + 1) s += rp[i].amt->load();
    }
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
    dest = ctx.destFor (pid, &trk);
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
        if (lastStatic != 0.0f)
        {
            Path arc;
            const float a0 = angOf (norm), a1 = angOf (norm + lastStatic);
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
            g.setColour (C::cyan.withAlpha (0.35f));
            g.strokePath (arc, PathStrokeType (2.0f));
        }
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
    const float st = ctx.routeAmountTo (dest, trk);
    float m = 0.0f;
    auto& tel = ctx.proc.engine.telemetry;
    if (trk >= 0) m = tel.mod[trk][dest - ModDst::TrackBase].load (std::memory_order_relaxed);
    else if (dest < ModDst::Count) m = tel.gmod[dest].load (std::memory_order_relaxed);
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
    if (src > 0 && dest > 0) ctx.addRoute (src, trk, dest, 0.3f);
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
    int local = -1;
    const int t = trackOfParamId (pid, &local);
    const bool canLock = t >= 0 && trackParam (local).lockable && ctx.selStep >= 0;
    if (canLock) m.addItem (5, "Fijar este valor en el paso " + String (ctx.selStep + 1));
    if (t >= 0) m.addItem (6, "Aplicar el valor a todas las pistas");
    if (dest > 0)
    {
        Array<int> routes; ctx.routesTo (dest, trk, routes);
        PopupMenu mods;
        for (auto r : routes)
        {
            const int src = (int) ctx.getParam (routeId (r, "src"));
            mods.addItem (100 + r, "Editar ruta " + String (r + 1) + ": " + modSourceNames()[src] + "  " + String (roundToInt (ctx.getParam (routeId (r, "amt")) * 100.0f)) + "%");
        }
        if (routes.size() > 0) { m.addSubMenu ("Modulaciones", mods); m.addItem (4, "Quitar todas las modulaciones"); }
        PopupMenu add;
        const int quick[] { ModSrc::GLfo1, ModSrc::GLfo1 + 1, ModSrc::VLfo1, ModSrc::Macro1, ModSrc::Macro1 + 1, ModSrc::Velocity, ModSrc::Random,
                            ModSrc::Accent, ModSrc::XYX, ModSrc::XYY, ModSrc::BarRamp, ModSrc::ModWheel };
        for (auto q : quick) add.addItem (1000 + q, modSourceNames()[q]);
        m.addSubMenu (String::fromUTF8 ("Agregar modulación"), add);
    }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, t, local] (int r)
    {
        if (racoon::handleAutomationMenu (r, *this, pid)) { repaint(); return; }
        if (r == 1) ctx.proc.startMidiLearn (pid);
        else if (r == 2) ctx.proc.clearMidiLearn (pid);
        else if (r == 3) { if (auto* p = ctx.apvts.getParameter (pid)) p->setValueNotifyingHost (p->getDefaultValue()); }
        else if (r == 4) ctx.removeRoutesTo (dest, trk);
        else if (r == 5)
        {
            const int step = ctx.selStep;
            const float v = ctx.apvts.getParameter (pid)->getValue();
            const int pat = (int) ctx.getParam ("g_pattern") - 1;
            ctx.proc.editSeq ([=] (SeqData& d) { auto& st = d.pats[pat].tracks[t].steps[step]; st.on = true; st.setLock (local, v); });
            if (ctx.refreshAll) ctx.refreshAll();
        }
        else if (r == 6)
        {
            const float v = ctx.apvts.getParameter (pid)->getValue();
            ctx.proc.undo.beginNewTransaction ("Apply to all");
            for (int u = 0; u < kNumTracks; ++u) ctx.proc.trackParamObj (u, local)->setValueNotifyingHost (v);
        }
        else if (r >= 1000) ctx.addRoute (r - 1000, trk, dest, 0.3f);
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
    g.drawFittedText (::racoon::i18n::tr (lastText), r.removeFromBottom (13.0f).toNearestInt(), Justification::centred, 1, 0.7f);
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

// ===================================================================== param button
ParamButton::ParamButton (Ctx& c, const String& paramId, const String& t, bool mom, Colour lit)
    : ctx (c), pid (paramId), text (t), momentary (mom), litColour (lit)
{
    if (auto* p = ctx.apvts.getParameter (pid)) setTooltip (p->getName (64) + (momentary ? " (hold)" : ""));
    ctx.registerAnimated (this);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

ParamButton::~ParamButton() { ctx.unregisterAnimated (this); }

void ParamButton::paint (Graphics& g)
{
    const bool on = ctx.getParam (pid) > 0.5f;
    auto r = getLocalBounds().toFloat();
    const float sh = r.getHeight() >= 20.0f ? 3.0f : 2.0f;
    Draw::box (g, r, on ? (litColour == C::amber || litColour == C::amberHot ? S::hi : litColour) : (isMouseOver() ? Colours::white : S::card), 1.5f, sh);
    r = r.withTrimmedRight (sh).withTrimmedBottom (sh);
    g.setFont (Fonts::label (std::min (14.0f, r.getHeight() * 0.55f)));
    g.setColour (S::ink);
    g.drawFittedText (::racoon::i18n::tr (text), r.toNearestInt(), Justification::centred, 1, 0.6f);
}

void ParamButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { racoon::showParamMenu (*this, pid); return; }
    auto* p = ctx.apvts.getParameter (pid);
    if (p == nullptr) return;
    if (momentary) p->setValueNotifyingHost (1.0f);
    else p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    repaint();
}

void ParamButton::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (! momentary) return;
    if (auto* p = ctx.apvts.getParameter (pid)) p->setValueNotifyingHost (0.0f);
    repaint();
}

void ParamButton::animate()
{
    if (! animVisible (this)) return;
    const bool on = ctx.getParam (pid) > 0.5f;
    if (on != last) { last = on; repaint(); }
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
    stepperBox (g, box, true);
    const int v = getInt();
    g.setFont (Fonts::mono (14.0f));
    g.setColour (S::ink);
    g.drawFittedText (::racoon::i18n::tr (fmt ? fmt (v) : String (v)), box.toNearestInt(), Justification::centred, 1, 0.6f);
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

// ===================================================================== value stepper
ValueStepper::ValueStepper (const String& l, int a, int b, std::function<String (int)> f) : label (l), lo (a), hi (b), value (a), fmt (std::move (f)) {}

void ValueStepper::setValue (int v, bool notify)
{
    v = jlimit (lo, hi, v);
    if (v == value) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void ValueStepper::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (label.isNotEmpty()) Draw::engraved (g, label.toUpperCase(), r.removeFromTop (13.0f), Fonts::label (11.0f), Justification::centred, S::ink);
    auto box = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 22.0f));
    stepperBox (g, box, isEnabled());
    g.setFont (Fonts::mono (14.0f));
    g.setColour (isEnabled() ? S::ink : S::inkFaint);
    g.drawFittedText (::racoon::i18n::tr (fmt ? fmt (value) : String (value)), box.toNearestInt(), Justification::centred, 1, 0.6f);
}

void ValueStepper::mouseDown (const MouseEvent& e)
{
    dragStart = value;
    if (e.x < 16) setValue (value - 1, true);
    else if (e.x > getWidth() - 16) setValue (value + 1, true);
}

void ValueStepper::mouseDrag (const MouseEvent& e)
{
    if (e.mouseDownPosition.x < 16 || e.mouseDownPosition.x > getWidth() - 16) return;
    const int range = hi - lo;
    const int div = range > 60 ? 2 : 6;
    setValue (dragStart - e.getDistanceFromDragStartY() / div, true);
}

void ValueStepper::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) { setValue (value + (w.deltaY > 0 ? 1 : -1), true); }

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
    const float v = jlimit (-1.0f, 1.0f, last);
    auto bar = r.reduced (2.0f).removeFromBottom (2.0f);
    g.setColour (C::cyan.withAlpha (0.8f));
    if (ModSrc::isBipolar (src)) g.fillRect (bar.withWidth (bar.getWidth() * 0.5f * std::abs (v)).withX (v >= 0 ? bar.getCentreX() : bar.getCentreX() - bar.getWidth() * 0.5f * std::abs (v)));
    else g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, v)));
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
    const float v = ctx.proc.engine.telemetry.src[src].load (std::memory_order_relaxed);
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
        r = r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
        g.setFont (Fonts::label (std::min (13.0f, (float) getHeight() * 0.6f)));
        g.setColour (s ? S::ink : S::inkSoft);
        g.drawFittedText (::racoon::i18n::tr (names[i]), r.toNearestInt(), Justification::centred, 1, 0.6f);
        if (dotColour)
        {
            const auto c = dotColour (i);
            if (! c.isTransparent()) { g.setColour (c); g.fillEllipse (r.getRight() - 8.0f, r.getY() + 4.0f, 4.0f, 4.0f); }
        }
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

// ===================================================================== track selector
TrackSelector::TrackSelector (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
TrackSelector::~TrackSelector() { ctx.unregisterAnimated (this); }

void TrackSelector::paint (Graphics& g)
{
    const float w = (float) getWidth() / kNumTracks;
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto r = Rectangle<float> ((float) t * w, 0.0f, w, (float) getHeight()).reduced (1.5f, 1.0f);
        const bool sel = t == ctx.track();
        const bool muted = ctx.proc.trackValue (t, T_MUTE) > 0.5f;
        Draw::box (g, r, sel ? S::hi : (muted ? S::cardDim : S::card), 1.5f, 2.0f);
        r = r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
        if (flash[t] > 0.01f)
        {
            g.setColour (S::accent().darker (0.3f).withAlpha (0.9f * flash[t]));
            g.fillRect (r.reduced (2.0f).removeFromBottom (4.0f));
        }
        g.setFont (Fonts::mono (10.0f));
        g.setColour (muted ? S::inkFaint : S::ink);
        auto top = r.removeFromTop (r.getHeight() * 0.45f);
        g.drawText (::racoon::i18n::tr (String (t + 1)), top, Justification::centred);
        g.setFont (Fonts::label (11.0f));
        g.drawFittedText (::racoon::i18n::tr (ctx.proc.getTrackName (t).toUpperCase()), r.toNearestInt().reduced (2, 0), Justification::centredTop, 1, 0.5f);
    }
}

void TrackSelector::mouseDown (const MouseEvent& e)
{
    const int t = jlimit (0, kNumTracks - 1, (int) ((float) e.x / ((float) getWidth() / kNumTracks)));
    ctx.selectTrack (t);
    if (e.mods.isAltDown() || e.mods.isRightButtonDown()) ctx.proc.padDown (t, 0.85f);
    repaint();
}

void TrackSelector::animate()
{
    if (! animVisible (this)) return;
    bool any = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int h = ctx.proc.engine.telemetry.trackHits[t].load (std::memory_order_relaxed);
        if (h != lastHits[t]) { lastHits[t] = h; flash[t] = 1.0f; any = true; }
        else if (flash[t] > 0.01f) { flash[t] *= 0.75f; any = true; }
    }
    if (any) repaint();
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

void layoutGrid (Rectangle<int> r, const Array<Component*>& comps, int columns, int rowHeight, int gap)
{
    if (columns <= 0) return;
    const int w = (r.getWidth() - gap * (columns - 1)) / columns;
    for (int i = 0; i < comps.size(); ++i)
    {
        const int row = i / columns, col = i % columns;
        if (comps[i] != nullptr) comps[i]->setBounds (r.getX() + col * (w + gap), r.getY() + row * (rowHeight + gap), w, rowHeight);
    }
}
} // namespace be::ui

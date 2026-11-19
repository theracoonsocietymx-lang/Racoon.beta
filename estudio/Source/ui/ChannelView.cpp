#include "ChannelView.h"
#include "engine/I18n.h"
#include "Menus.h"
#include "fx/NativeFx.h"
#include "engine/Devices.h"
#include "engine/MidiCapture.h"
#include "RackEditor.h"
#include "Arrangement.h"
#include "Spanish.h"
#include <BinaryData.h>

namespace racoon::ui
{
using namespace juce;

namespace
{
constexpr int titleH = 46, knobW = 70, cardGap = 26, headerH = 40;

RacoonProcessor* racoonProcessorOf (te::Plugin& p)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&p))
        return dynamic_cast<RacoonProcessor*> (ext->getAudioPluginInstance());
    return nullptr;
}
}

// ===================================================================== knob
ParamKnob::ParamKnob (ViewState& v, te::Track& t, te::AutomatableParameter& p) : param (&p), vs (v), track (t)
{
    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    // the wheel scrolls the row of cards, as everywhere else; knobs move by dragging
    slider.setScrollWheelEnabled (false);
    slider.setRange (p.valueRange.start, p.valueRange.end);
    slider.setDoubleClickReturnValue (true, p.getDefaultValue().value_or (p.getCurrentValue()));
    slider.setValue (p.getCurrentValue(), dontSendNotification);
    slider.setPopupMenuEnabled (false);
    slider.onDragStart = [this] { dragging = true; param->parameterChangeGestureBegin(); };
    slider.onDragEnd = [this] { dragging = false; param->parameterChangeGestureEnd(); };
    slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), sendNotification); repaint(); };
    slider.onRightClick = [this] { showMenu(); };
    addAndMakeVisible (slider);
    setTooltip (p.getParameterName() + ". Clic derecho: automatizar."_u);
    slider.setTooltip (getTooltip());
    startTimerHz (15);
}

ParamKnob::~ParamKnob() = default;

void ParamKnob::timerCallback()
{
    if (dragging) return;
    const float v = param->getCurrentValue();
    if (std::abs ((float) slider.getValue() - v) > 1.0e-6f) { slider.setValue (v, dontSendNotification); repaint(); }
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (28);
    const int sz = jmin (r.getWidth() - 10, r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (sz, sz));
}

String ParamKnob::labelFor (te::AutomatableParameter& p)
{
    if (dynamic_cast<te::MacroParameter*> (&p) != nullptr) return p.getParameterName();
    if (auto* plug = p.getPlugin())
        if (auto* rp = racoonProcessorOf (*plug))
            if (rp->shortLabels.containsKey (p.paramID)) return rp->shortLabels[p.paramID];
    const auto n = p.getParameterName();
    const auto t = es (n);
    return t != n ? t : p.getParameterShortName (14);
}

String ParamKnob::valueText (te::AutomatableParameter& p)
{
    const float v = p.getCurrentValue();
    if (p.hasLabels()) return es (p.getLabelForValue (v));
    if (dynamic_cast<te::MacroParameter*> (&p) != nullptr) return String (roundToInt (v * 100.0f)) + " %";
    if (p.getPlugin() == nullptr)
    {
        // modulator knobs: readable units
        const auto& id = p.paramID;
        if (id == "depth" || id == "phase" || id == "offset" || id.startsWith ("stage")) return String (roundToInt (v * 100.0f)) + " %";
        if (id == "rate") return String (v, 2);
        if (id == "attack" || id == "hold" || id == "release") return String (roundToInt (v)) + " ms";
        if (id == "gainDb") return String (v, 1) + " dB";
        if (id == "numActivePoints") return String (roundToInt (v));
    }
    return es (p.getCurrentValueAsString());
}

void ParamKnob::paint (Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (28);
    const bool automated = param->isAutomationActive();
    const bool modulated = ! param->getAssignments().isEmpty();
    g.setColour (S::ink);
    g.setFont (Fonts::label (11.0f));
    g.drawFittedText (::racoon::i18n::tr (upper (labelFor (*param))), r.removeFromTop (13), Justification::centred, 1, 0.75f);
    g.setColour (automated ? Brand::rosa.darker (0.4f) : S::inkSoft);
    g.setFont (Fonts::body (11.0f));
    g.drawFittedText (::racoon::i18n::tr (valueText (*param)), r, Justification::centred, 1, 0.75f);
    if (automated)
    {
        g.setColour (Brand::rosa);
        g.fillEllipse (slider.getBounds().toFloat().removeFromTop (8).removeFromRight (8));
    }
    if (modulated)
    {
        // a modulator or a macro moves it
        g.setColour (C::cyan.darker (0.3f));
        g.fillEllipse (slider.getBounds().toFloat().removeFromTop (8).removeFromLeft (8));
    }
}

void ParamKnob::showMenu()
{
    PopupMenu m;
    m.addSectionHeader (param->getParameterName());
    m.addItem ("Automatizar en la línea de tiempo"_u, [this]
    {
        if (vs.automateParameter) vs.automateParameter (track, *param);
    });
    m.addItem ("Valor inicial", [this]
    {
        if (auto d = param->getDefaultValue()) param->setParameter (*d, sendNotification);
    });
    if (param->getCurve().getNumPoints() > 0)
        m.addItem ("Borrar su automatización"_u, [this]
        {
            vs.edit().getUndoManager().beginNewTransaction();
            param->getCurve().clear();
        });
    // modulators of this channel
    auto mods = devices::modulatorsOf (track);
    if (! mods.isEmpty())
    {
        PopupMenu sub;
        for (auto* mod : mods)
        {
            te::Modifier::Ptr keep (mod);
            const bool on = devices::amountFor (*mod, *param) != 0.0f;
            sub.addItem (devices::modulatorName (*mod), true, on, [this, keep, on]
            {
                vs.edit().getUndoManager().beginNewTransaction();
                if (on) devices::unmap (*keep, *param); else devices::map (*keep, *param, 0.5f);
                repaint();
            });
        }
        m.addSubMenu ("Mover con un modulador", sub);
    }
    if (extraMenu) extraMenu (m);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

// ===================================================================== card
Array<te::AutomatableParameter*> DeviceCard::cardParameters (te::Plugin& p)
{
    Array<te::AutomatableParameter*> out;
    if (devices::isFxRack (p)) return devices::macros (p);
    if (auto* rp = racoonProcessorOf (p))
    {
        for (auto& id : rp->keyParams)
            for (auto* ap : p.getAutomatableParameters())
                if (ap->paramID == id) { out.add (ap); break; }
        return out;
    }
    return out;
}

DeviceCard::DeviceCard (ViewState& v, te::Track& t, te::Plugin& p, int idx) : plugin (&p), index (idx), vs (v), track (t)
{
    instrument = Session::isInstrument (p);
    if (instrument) kindLabel = "INSTRUMENTO";
    else if (Session::isMidiEffect (p)) kindLabel = "EFECTO MIDI · VA ANTES DEL INSTRUMENTO"_u;
    else if (auto* rp = racoonProcessorOf (p)) kindLabel = "EFECTO · "_u + upper (fx::find (rp->effectId) != nullptr ? fx::find (rp->effectId)->category : String());
    else if (devices::isFxRack (p)) kindLabel = "RACK · ABRIR PARA VER SUS CADENAS"_u;
    else if (dynamic_cast<te::InsertPlugin*> (&p) != nullptr) kindLabel = "EFECTO EXTERNO · POR TU INTERFAZ"_u;
    else kindLabel = "EFECTO";

    power.setClickingTogglesState (true);
    power.setToggleState (p.isEnabled(), dontSendNotification);
    power.setButtonText ("ON");
    power.setTooltip ("Encender o apagar este dispositivo (bypass)");
    power.onClick = [this]
    {
        vs.edit().getUndoManager().beginNewTransaction();
        plugin->setEnabled (power.getToggleState());
        repaint();
    };
    open.setTooltip ("Abrir su ventana con todos los controles");
    open.onClick = [this] { openDevice (vs, track, *plugin); };
    remove.setButtonText ("X");
    remove.setTooltip ("Quitar del canal");
    remove.onClick = [this]
    {
        auto p = plugin;
        MessageManager::callAsync ([p, &edit = vs.edit()]
        {
            edit.getUndoManager().beginNewTransaction();
            p->deleteFromParent();
        });
    };
    for (auto* b : { &power, &open, &remove }) addAndMakeVisible (b);
    for (auto* ap : cardParameters (p))
        addAndMakeVisible (knobs.add (new ParamKnob (vs, t, *ap)));
    if (! instrument && p.canSidechain())
    {
        listenTo = std::make_unique<ComboBox>();
        listenTo->setTooltip (String::fromUTF8 ("Sidechain: el canal que este efecto escucha. Con el bombo, este canal baja en cada golpe."));
        listenTo->onChange = [this]
        {
            const int id = listenTo->getSelectedId();
            te::Track* src = id >= 2 && id - 2 < listenTracks.size() ? listenTracks[id - 2] : nullptr;
            vs.edit().getUndoManager().beginNewTransaction();
            vs.session.setSidechain (*plugin, src);
            if (vs.showHint)
                vs.showHint (src != nullptr ? Catalog::displayName (*plugin) + String::fromUTF8 (" escucha a «") + src->getName() + String::fromUTF8 ("».")
                                            : Catalog::displayName (*plugin) + String::fromUTF8 (" ya no escucha a otro canal."));
        };
        addAndMakeVisible (*listenTo);
        fillListenTo();
    }
    setTooltip ("Doble clic: abrirlo. Clic derecho: opciones. Arrastra la barra de arriba para cambiarlo de lugar."_u);
    startTimerHz (4);
}

void DeviceCard::fillListenTo()
{
    if (listenTo == nullptr) return;
    String sig;
    Array<te::Track*> list;
    for (auto* at : te::getAudioTracks (vs.edit()))
        if (at != &track && kindOf (*at) != TrackKind::returnBus) { list.add (at); sig << at->itemID.toString() << at->getName(); }
    sig << plugin->getSidechainSourceID().toString();
    if (sig == listenSignature) return;
    listenSignature = sig;
    listenTracks = list;
    listenTo->clear (dontSendNotification);
    listenTo->addItem (String::fromUTF8 ("Escucha a: nadie (su propia señal)"), 1);
    int selected = 1;
    for (int i = 0; i < list.size(); ++i)
    {
        listenTo->addItem (String::fromUTF8 ("Escucha a: ") + list[i]->getName(), i + 2);
        if (plugin->getSidechainSourceID() == list[i]->itemID) selected = i + 2;
    }
    listenTo->setSelectedId (selected, dontSendNotification);
}

void DeviceCard::timerCallback()
{
    power.setToggleState (plugin->isEnabled(), dontSendNotification);
    fillListenTo();
    repaint();
}

int DeviceCard::preferredWidth() const
{
    if (knobs.isEmpty()) return instrument ? 230 : 200;
    const int perRow = (knobs.size() + rows() - 1) / rows();
    return jmax (200, 24 + perRow * knobW);
}

void DeviceCard::openDevice (ViewState& vs, te::Track& track, te::Plugin& p)
{
    if (devices::isFxRack (p)) { RackEditor::open (vs, track, p); return; }
    if (auto* ins = dynamic_cast<te::InsertPlugin*> (&p))
    {
        // the external effect: where the sound leaves and where it comes back
        te::Plugin::Ptr keep (&p);
        PopupMenu m, outs, ins2;
        StringArray devs, aliases;
        BigInteger hasAudio, hasMidi;
        te::InsertPlugin::getPossibleDeviceNames (vs.session.engine, devs, aliases, hasAudio, hasMidi, false);
        for (int i = 0; i < devs.size(); ++i)
            outs.addItem (aliases[i].isNotEmpty() ? aliases[i] : devs[i], true, ins->outputDevice.get() == devs[i],
                          [keep, d = devs[i]] { if (auto* x = dynamic_cast<te::InsertPlugin*> (keep.get())) x->outputDevice = d; });
        StringArray idevs, ialiases;
        te::InsertPlugin::getPossibleDeviceNames (vs.session.engine, idevs, ialiases, hasAudio, hasMidi, true);
        for (int i = 0; i < idevs.size(); ++i)
            ins2.addItem (ialiases[i].isNotEmpty() ? ialiases[i] : idevs[i], true, ins->inputDevice.get() == idevs[i],
                          [keep, d = idevs[i]] { if (auto* x = dynamic_cast<te::InsertPlugin*> (keep.get())) x->inputDevice = d; });
        if (outs.getNumItems() == 0) outs.addItem ("(no hay salidas: elige tu interfaz en Ajustes)", false, false, [] {});
        if (ins2.getNumItems() == 0) ins2.addItem ("(no hay entradas: elige tu interfaz en Ajustes)", false, false, [] {});
        m.addSectionHeader ("Efecto externo");
        m.addSubMenu ("El sonido sale por", outs);
        m.addSubMenu ("Y vuelve por", ins2);
        PopupMenu lat;
        for (double ms : { -5.0, -2.0, -1.0, 0.0, 1.0, 2.0, 5.0, 10.0 })
            lat.addItem ((ms > 0 ? "+" : "") + String (ms, 0) + " ms", true, std::abs (ins->manualAdjustMs.get() - ms) < 0.01,
                         [keep, ms] { if (auto* x = dynamic_cast<te::InsertPlugin*> (keep.get())) x->manualAdjustMs = ms; });
        m.addSubMenu ("Corregir el retraso a mano", lat);
        m.addItem ("Medir el retraso (suena una serie de clics)", [keep, &vs]
        {
            if (auto* x = dynamic_cast<te::InsertPlugin*> (keep.get()))
                devices::measureInsertLatency (vs.session.engine, *x, [&vs] (bool, double, String msg) { if (vs.showHint) vs.showHint (msg); });
        });
        m.showMenuAsync (PopupMenu::Options());
        return;
    }
    p.showWindowExplicitly();
}

void DeviceCard::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (22);
    power.setBounds (top.removeFromLeft (38));
    remove.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    open.setBounds (top.removeFromRight (62));
    r.removeFromTop (titleH - 22);
    if (listenTo != nullptr) listenTo->setBounds (r.removeFromBottom (18 + 26).removeFromTop (24).reduced (2, 0));
    if (knobs.isEmpty()) return;
    auto area = r.reduced (4, 2).withTrimmedBottom (listenTo != nullptr ? 2 : 18);
    const int nr = rows(), perRow = (knobs.size() + nr - 1) / nr;
    const int rowH = area.getHeight() / nr;
    for (int row = 0; row < nr; ++row)
    {
        auto line = area.removeFromTop (rowH);
        const int w = line.getWidth() / perRow;
        for (int i = row * perRow; i < jmin (knobs.size(), (row + 1) * perRow); ++i)
            knobs[i]->setBounds (line.removeFromLeft (w).withSizeKeepingCentre (jmin (w, knobW), jmin (line.getHeight(), 110)));
    }
}

void DeviceCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = plugin->isEnabled();
    Draw::box (g, r, on ? S::card : S::cardDim, 2.0f, 3.0f);
    // title strip, the handle for dragging
    auto title = r.reduced (3.0f).removeFromTop ((float) titleH);
    g.setColour (instrument ? colourForTrack (track) : S::cardDim);
    g.fillRect (title.withTrimmedTop (24));
    g.setColour (S::ink);
    g.setFont (Fonts::bodyBold (14.0f));
    auto nameArea = title.withTrimmedTop (24).reduced (8.0f, 0.0f).toNearestInt();
    if (instrument)
    {
        int size = 0;
        const auto animal = Catalog::animalFor (plugin->getName()).toLowerCase();
        if (auto* data = BinaryData::getNamedResource ((animal + "_png").toRawUTF8(), size))
        {
            g.drawImage (ImageCache::getFromMemory (data, size), nameArea.removeFromLeft (20).toFloat(), RectanglePlacement::centred);
            nameArea.removeFromLeft (6);
        }
    }
    g.drawFittedText (::racoon::i18n::tr (Catalog::displayName (*plugin)), nameArea, Justification::centredLeft, 1, 0.8f);
    g.setFont (Fonts::label (10.0f));
    g.setColour (S::inkSoft);
    g.drawFittedText (::racoon::i18n::tr (kindLabel), getLocalBounds().reduced (12, 0).withTop (getHeight() - 22).withHeight (16), Justification::centredLeft, 1, 0.7f);
    if (knobs.isEmpty())
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (12.5f));
        String text = instrument ? "Doble clic o ABRIR para ver el instrumento completo.\nSus notas se escriben en PIANO."_u
                                 : "Doble clic o ABRIR para ver todos sus controles."_u;
        if (auto* ins = dynamic_cast<te::InsertPlugin*> (plugin.get()))
            text = "Sale por: "_u + (ins->outputDevice.get().isNotEmpty() ? ins->outputDevice.get() : String ("sin elegir"))
                 + "\nVuelve por: "_u + (ins->inputDevice.get().isNotEmpty() ? ins->inputDevice.get() : String ("sin elegir"))
                 + "\nPulsa ABRIR para elegir."_u;
        g.drawFittedText (::racoon::i18n::tr (text),
                          getLocalBounds().reduced (14, 0).withTrimmedTop (titleH + 10).withTrimmedBottom (24), Justification::centred, 3);
    }
}

void DeviceCard::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem ("Abrir (o doble clic)", [this] { openDevice (vs, track, *plugin); });
        m.addItem (plugin->isEnabled() ? "Apagar (bypass)" : "Encender", [this] { vs.edit().getUndoManager().beginNewTransaction(); plugin->setEnabled (! plugin->isEnabled()); });
        if (Session::isMidiEffect (*plugin))
            if (auto* at = dynamic_cast<te::AudioTrack*> (&track))
                m.addItem ("Pasar a clip MIDI lo que toca"_u, [this, at]
                {
                    auto r = capture::midiToClip (vs.session, *at, *plugin, capture::defaultRange (vs.session, *at));
                    if (vs.showHint)
                        vs.showHint (r.error.isNotEmpty() ? r.error
                                     : "Listo: " + String (r.notes) + " notas en un clip nuevo. El efecto quedó apagado para que no se dupliquen; Cmd+Z lo deshace."_u);
                });
        m.addSeparator();
        m.addItem ("Quitar", [this] { remove.triggerClick(); });
        m.showMenuAsync (PopupMenu::Options().withMousePosition());
    }
}

void DeviceCard::mouseDoubleClick (const MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) openDevice (vs, track, *plugin);
}

void DeviceCard::mouseDrag (const MouseEvent& e)
{
    if (e.mouseDownPosition.y > titleH + 4 || e.getDistanceFromDragStart() < 6) return;
    if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive())
            dc->startDragging ("device:" + String (index), this, ScaledImage (createComponentSnapshot (getLocalBounds(), true, 0.6f)));
}

// ===================================================================== shape editor (Forma)
namespace
{
// draws the breakpoint curve; drag a point to move it, double click to add one, right click a point to remove it,
// drag a line up or down (with Alt) to bend it
class ShapeEditor : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit ShapeEditor (te::BreakpointOscillatorModifier& m) : mod (&m)
    {
        setTooltip ("Arrastra los puntos para dibujar la forma. Doble clic agrega un punto, clic derecho lo quita, Alt+arrastrar curva una línea."_u);
        startTimerHz (20);
    }
    struct Pt { float t, v; };
    te::AutomatableParameter* valueParam (int i) const
    {
        te::AutomatableParameter::Ptr v[] { mod->stageZeroValueParam, mod->stageOneValueParam, mod->stageTwoValueParam, mod->stageThreeValueParam, mod->stageFourValueParam };
        return v[i].get();
    }
    te::AutomatableParameter* timeParam (int i) const
    {
        te::AutomatableParameter::Ptr v[] { nullptr, mod->stageOneTimeParam, mod->stageTwoTimeParam, mod->stageThreeTimeParam, mod->stageFourTimeParam };
        return v[i].get();
    }
    te::AutomatableParameter* curveParam (int i) const
    {
        te::AutomatableParameter::Ptr v[] { nullptr, mod->stageOneCurveParam, mod->stageTwoCurveParam, mod->stageThreeCurveParam, mod->stageFourCurveParam };
        return v[i].get();
    }
    int count() const { return jlimit (1, 4, roundToInt (mod->numActivePointsParam->getCurrentValue())); }
    Pt point (int i) const { return { i == 0 ? 0.0f : timeParam (i)->getCurrentValue(), valueParam (i)->getCurrentValue() }; }
    Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (6.0f, 5.0f); }
    Point<float> toScreen (Pt p) const { auto a = area(); return { a.getX() + a.getWidth() * p.t, a.getBottom() - a.getHeight() * p.v }; }
    void paint (Graphics& g) override
    {
        g.setColour (S::cardDim);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        auto a = area();
        g.setColour (S::inkSoft.withAlpha (0.3f));
        for (int i = 1; i < 4; ++i) g.drawVerticalLine (roundToInt (a.getX() + a.getWidth() * i / 4.0f), a.getY(), a.getBottom());
        // the curve, sampled like the modulator does it (straight lines bent by the curve amount)
        Path p;
        const int n = count();
        for (int s = 0; s <= 200; ++s)
        {
            const float x = s / 200.0f;
            float y = point (n).v;
            for (int i = 1; i <= n; ++i)
            {
                auto a0 = point (i - 1), a1 = point (i);
                if (x <= a1.t)
                {
                    const float span = jmax (1.0e-4f, a1.t - a0.t), f = jlimit (0.0f, 1.0f, (x - a0.t) / span);
                    const float c = curveParam (i)->getCurrentValue();   // -0.5 .. 0.5
                    const float shaped = c >= 0.0f ? std::pow (f, 1.0f + c * 4.0f) : 1.0f - std::pow (1.0f - f, 1.0f - c * 4.0f);
                    y = a0.v + (a1.v - a0.v) * shaped;
                    break;
                }
            }
            auto sp = toScreen ({ x, y });
            if (s == 0) p.startNewSubPath (sp); else p.lineTo (sp);
        }
        g.setColour (S::ink);
        g.strokePath (p, PathStrokeType (2.0f));
        for (int i = 0; i <= n; ++i)
        {
            auto sp = toScreen (point (i));
            g.setColour (i == dragging ? Brand::rosa : S::ink);
            g.fillEllipse (sp.x - 4.5f, sp.y - 4.5f, 9.0f, 9.0f);
        }
        // where the modulator is now
        const float ph = mod->getCurrentPhase();
        g.setColour (C::cyan.darker (0.2f));
        g.drawVerticalLine (roundToInt (a.getX() + a.getWidth() * ph), a.getY(), a.getBottom());
    }
    int hit (Point<float> pos) const
    {
        for (int i = 0; i <= count(); ++i) if (toScreen (point (i)).getDistanceFrom (pos) < 9.0f) return i;
        return -1;
    }
    void mouseDown (const MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (e.mods.isPopupMenu())
        {
            if (h > 0 && count() > 1) removePoint (h);
            return;
        }
        dragging = h;
        bendSegment = -1;
        if (h < 0 && e.mods.isAltDown())
        {
            const float t = jlimit (0.0f, 1.0f, (e.position.x - area().getX()) / area().getWidth());
            for (int i = 1; i <= count(); ++i) if (t <= point (i).t) { bendSegment = i; bendStart = curveParam (i)->getCurrentValue(); break; }
        }
        for (auto* p : { valueParam (jmax (0, h)), curveParam (jmax (1, bendSegment)) }) if (p != nullptr) p->parameterChangeGestureBegin();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        auto a = area();
        if (bendSegment > 0)
        {
            curveParam (bendSegment)->setParameter (jlimit (-0.5f, 0.5f, bendStart + e.getDistanceFromDragStartY() / a.getHeight()), sendNotification);
            repaint();
            return;
        }
        if (dragging < 0) return;
        const float v = jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        valueParam (dragging)->setParameter (v, sendNotification);
        if (dragging > 0)
        {
            const float lo = point (dragging - 1).t + 0.01f, hi = dragging < count() ? point (dragging + 1).t - 0.01f : 1.0f;
            const float t = jlimit (lo, jmax (lo, hi), (e.position.x - a.getX()) / a.getWidth());
            timeParam (dragging)->setParameter (dragging == count() && count() == 4 ? jmax (t, lo) : t, sendNotification);
        }
        repaint();
    }
    void mouseUp (const MouseEvent&) override
    {
        for (auto* p : { valueParam (jmax (0, dragging)), curveParam (jmax (1, bendSegment)) }) if (p != nullptr) p->parameterChangeGestureEnd();
        dragging = -1; bendSegment = -1;
        repaint();
    }
    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int n = count();
        if (n >= 4) return;
        auto a = area();
        const float t = jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth());
        const float v = jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        // insert in time order: shift the later points one place up
        int at = n + 1;
        for (int i = 1; i <= n; ++i) if (t < point (i).t) { at = i; break; }
        for (int i = n; i >= at; --i)
        {
            valueParam (i + 1)->setParameter (valueParam (i)->getCurrentValue(), sendNotification);
            timeParam (i + 1)->setParameter (timeParam (i)->getCurrentValue(), sendNotification);
            curveParam (i + 1)->setParameter (curveParam (i)->getCurrentValue(), sendNotification);
        }
        valueParam (at)->setParameter (v, sendNotification);
        timeParam (at)->setParameter (t, sendNotification);
        curveParam (at)->setParameter (0.0f, sendNotification);
        mod->numActivePointsParam->setParameter ((float) (n + 1), sendNotification);
        repaint();
    }
    void removePoint (int h)
    {
        const int n = count();
        for (int i = h; i < n; ++i)
        {
            valueParam (i)->setParameter (valueParam (i + 1)->getCurrentValue(), sendNotification);
            timeParam (i)->setParameter (timeParam (i + 1)->getCurrentValue(), sendNotification);
            curveParam (i)->setParameter (curveParam (i + 1)->getCurrentValue(), sendNotification);
        }
        mod->numActivePointsParam->setParameter ((float) (n - 1), sendNotification);
        repaint();
    }
private:
    te::BreakpointOscillatorModifier::Ptr mod;
    int dragging = -1, bendSegment = -1;
    float bendStart = 0.0f;
    void timerCallback() override { repaint(); }
};
}

// ===================================================================== modulator card
ModulatorCard::ModulatorCard (ViewState& v, te::Track& t, te::Modifier& m) : modifier (&m), vs (v), track (t)
{
    power.setButtonText ("ON");
    power.setClickingTogglesState (true);
    power.setToggleState (m.enabled.get() > 0.5f, dontSendNotification);
    power.setTooltip ("Encender o apagar el modulador");
    power.onClick = [this] { modifier->enabledParam->setParameter (power.getToggleState() ? 1.0f : 0.0f, sendNotification); };
    map.setTooltip ("Elige qué perillas mueve este modulador"_u);
    map.onClick = [this] { mapMenu (vs, track, *modifier).showMenuAsync (PopupMenu::Options().withTargetComponent (map)); };
    remove.setButtonText ("X");
    remove.setTooltip ("Quitar el modulador");
    remove.onClick = [this]
    {
        auto m = modifier;
        MessageManager::callAsync ([m, &edit = vs.edit()]
        {
            edit.getUndoManager().beginNewTransaction();
            for (auto* t : te::getAllTracks (edit))
                for (auto* p : devices::allParams (*t)) devices::unmap (*m, *p);
            m->remove();
        });
    };
    for (auto* b : { &power, &map, &remove }) addAndMakeVisible (b);
    if (auto* bp = dynamic_cast<te::BreakpointOscillatorModifier*> (&m))
    {
        shape = std::make_unique<ShapeEditor> (*bp);
        addAndMakeVisible (*shape);
        for (auto p : { bp->rateTypeParam, bp->rateParam, bp->depthParam, bp->bipolarParam })
            if (p != nullptr) addAndMakeVisible (knobs.add (new ParamKnob (vs, t, *p)));
    }
    else
        for (auto* ap : devices::modulatorKnobs (m))
            addAndMakeVisible (knobs.add (new ParamKnob (vs, t, *ap)));
    setTooltip ("Un modulador mueve otras perillas del canal. Pulsa MAPEAR para elegirlas.");
    startTimerHz (20);
}

int ModulatorCard::preferredWidth() const
{
    if (shape != nullptr) return 320;
    const int perRow = (knobs.size() + rows() - 1) / rows();
    return jmax (240, 24 + perRow * knobW);
}

PopupMenu ModulatorCard::mapMenu (ViewState& vs, te::Track& track, te::Modifier& mod)
{
    PopupMenu m;
    te::Modifier::Ptr keep (&mod);
    m.addSectionHeader ("Elige qué perilla mueve"_u);
    auto addParams = [&] (PopupMenu& sub, te::Plugin& plug)
    {
        for (auto* p : plug.getAutomatableParameters())
        {
            te::AutomatableParameter::Ptr prm (p);
            const bool on = devices::amountFor (mod, *p) != 0.0f;
            sub.addItem (ParamKnob::labelFor (*p), true, on, [&vs, keep, prm, on]
            {
                vs.edit().getUndoManager().beginNewTransaction();
                if (on) devices::unmap (*keep, *prm); else devices::map (*keep, *prm, 0.5f);
            });
        }
    };
    for (auto* plug : Session::chainOf (track))
    {
        PopupMenu sub;
        if (auto* r = devices::rackOf (*plug); r != nullptr && devices::isFxRack (*plug))
        {
            for (auto* inner : r->getPlugins())
            {
                if (dynamic_cast<te::VolumeAndPanPlugin*> (inner) != nullptr) continue;
                PopupMenu in;
                addParams (in, *inner);
                if (in.getNumItems() > 0) sub.addSubMenu (Catalog::displayName (*inner), in);
            }
            for (auto* mac : devices::macros (*plug))
            {
                te::AutomatableParameter::Ptr prm (mac);
                const bool on = devices::amountFor (mod, *mac) != 0.0f;
                sub.addItem (mac->getParameterName(), true, on, [&vs, keep, prm, on]
                { vs.edit().getUndoManager().beginNewTransaction(); if (on) devices::unmap (*keep, *prm); else devices::map (*keep, *prm, 0.5f); });
            }
        }
        else addParams (sub, *plug);
        if (sub.getNumItems() > 0) m.addSubMenu (Catalog::displayName (*plug), sub);
    }
    // the channel volume and pan
    if (auto* at = dynamic_cast<te::AudioTrack*> (&track))
        if (auto vp = at->getVolumePlugin())
        {
            PopupMenu sub;
            for (auto* p : { vp->volParam.get(), vp->panParam.get() })
            {
                te::AutomatableParameter::Ptr prm (p);
                const bool on = devices::amountFor (mod, *p) != 0.0f;
                sub.addItem (p == vp->volParam.get() ? "Volumen" : "Paneo", true, on, [&vs, keep, prm, on]
                { vs.edit().getUndoManager().beginNewTransaction(); if (on) devices::unmap (*keep, *prm); else devices::map (*keep, *prm, 0.5f); });
            }
            m.addSubMenu ("Canal", sub);
        }
    // how much it moves each target
    auto targets = devices::targetsOf (mod, track);
    if (! targets.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("Cuánto mueve"_u);
        for (auto* p : targets)
        {
            te::AutomatableParameter::Ptr prm (p);
            PopupMenu amt;
            const float now = devices::amountFor (mod, *p);
            for (float a : { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f })
                amt.addItem ((a > 0 ? "+" : "") + String (roundToInt (a * 100.0f)) + " %", true, std::abs (now - a) < 0.001f,
                             [&vs, keep, prm, a] { vs.edit().getUndoManager().beginNewTransaction(); devices::map (*keep, *prm, a); });
            amt.addSeparator();
            amt.addItem ("Dejar de moverla", [&vs, keep, prm] { vs.edit().getUndoManager().beginNewTransaction(); devices::unmap (*keep, *prm); });
            String owner = p->getPlugin() != nullptr ? Catalog::displayName (*p->getPlugin()) + " · "_u : String();
            m.addSubMenu (owner + ParamKnob::labelFor (*p) + "  (" + String (roundToInt (now * 100.0f)) + " %)", amt);
        }
    }
    return m;
}

void ModulatorCard::timerCallback()
{
    power.setToggleState (modifier->enabled.get() > 0.5f, dontSendNotification);
    history = modifier->getValues (te::TimeDuration::fromSeconds (2.0));
    repaint();
}

void ModulatorCard::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (22);
    power.setBounds (top.removeFromLeft (38));
    remove.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    map.setBounds (top.removeFromRight (74));
    r.removeFromTop (titleH - 22);
    if (shape != nullptr) shape->setBounds (r.removeFromTop (jmin (86, r.getHeight() / 2)).reduced (4, 2));
    if (knobs.isEmpty()) return;
    auto area = r.reduced (4, 2).withTrimmedBottom (18);
    const int nr = rows(), perRow = (knobs.size() + nr - 1) / nr;
    const int rowH = area.getHeight() / nr;
    for (int row = 0; row < nr; ++row)
    {
        auto line = area.removeFromTop (rowH);
        const int w = line.getWidth() / perRow;
        for (int i = row * perRow; i < jmin (knobs.size(), (row + 1) * perRow); ++i)
            knobs[i]->setBounds (line.removeFromLeft (w).withSizeKeepingCentre (jmin (w, knobW), jmin (line.getHeight(), 110)));
    }
}

void ModulatorCard::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = modifier->enabled.get() > 0.5f;
    Draw::box (g, r, on ? S::card : S::cardDim, 2.0f, 3.0f);
    auto title = r.reduced (3.0f).removeFromTop ((float) titleH).withTrimmedTop (24.0f);
    g.setColour (C::cyan.withAlpha (0.35f));
    g.fillRect (title);
    // the last two seconds of its output
    auto graph = title.removeFromRight (90.0f).reduced (4.0f, 3.0f);
    if (history.size() > 2)
    {
        Path p;
        const int n = (int) history.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = graph.getX() + graph.getWidth() * (float) i / (float) (n - 1);
            const float y = graph.getBottom() - graph.getHeight() * jlimit (0.0f, 1.0f, history[(size_t) i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (S::ink);
        g.strokePath (p, PathStrokeType (1.5f));
    }
    g.setColour (S::ink);
    g.setFont (Fonts::bodyBold (14.0f));
    g.drawFittedText (::racoon::i18n::tr (devices::modulatorName (*modifier)), title.reduced (8.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    // what it moves
    auto targets = devices::targetsOf (*modifier, track);
    String line;
    if (targets.isEmpty()) line = "MODULADOR · TODAVÍA NO MUEVE NADA: PULSA MAPEAR"_u;
    else
    {
        line = "MUEVE: "_u;
        for (int i = 0; i < targets.size(); ++i)
        {
            if (i > 0) line << ", ";
            if (auto* pl = targets[i]->getPlugin()) line << upper (Catalog::displayName (*pl)) << " ";
            line << upper (ParamKnob::labelFor (*targets[i]));
        }
    }
    g.setFont (Fonts::label (10.0f));
    g.setColour (targets.isEmpty() ? Brand::rosa.darker (0.3f) : S::inkSoft);
    g.drawFittedText (::racoon::i18n::tr (line), getLocalBounds().reduced (12, 0).withTop (getHeight() - 22).withHeight (16), Justification::centredLeft, 1, 0.6f);
}

// ===================================================================== channel view
// ===================================================================== volume box
VolumeBox::VolumeBox()
{
    fader.setSliderStyle (Slider::LinearHorizontal);
    fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    fader.setRange (0.0, 1.0);
    fader.setDoubleClickReturnValue (true, dbToVolumeFader (0.0f));
    fader.setTooltip (String::fromUTF8 ("Volumen del canal. Doble clic: 0 dB."));
    fader.onDragStart = [this] { if (auto* p = plugin()) p->edit.getUndoManager().beginNewTransaction(); };
    fader.onValueChange = [this]
    {
        if (auto* p = plugin()) p->setSliderPos ((float) fader.getValue());
        number.setText (dbText (volumeFaderToDb ((float) fader.getValue())), dontSendNotification);
    };
    number.setEditable (true, true, false);
    number.setJustificationType (Justification::centred);
    number.setFont (Fonts::mono (14.0f));
    number.setColour (Label::textColourId, S::ink);
    number.setColour (Label::backgroundColourId, C::engrave);
    number.setColour (Label::outlineColourId, S::ink);
    number.setColour (Label::textWhenEditingColourId, S::ink);
    number.setColour (TextEditor::backgroundColourId, C::engrave);
    number.setColour (TextEditor::textColourId, S::ink);
    number.setColour (TextEditor::highlightColourId, Brand::limon);
    number.setTooltip (String::fromUTF8 ("Clic y escribe el volumen en dB: -6, 0, +3 o -inf. Enter para aplicar."));
    number.onTextChange = [this]
    {
        auto* p = plugin();
        if (p == nullptr) return;
        if (auto db = parseDb (number.getText()))
        {
            p->edit.getUndoManager().beginNewTransaction();
            const float pos = dbToVolumeFader (jlimit (-100.0f, 6.0f, *db));
            p->setSliderPos (pos);
            show (pos);
        }
        else show (p->getSliderPos());      // not a number: back to what it was
    };
    addAndMakeVisible (fader);
    addAndMakeVisible (number);
    startTimerHz (20);
}

String VolumeBox::dbText (float db)
{
    if (db <= -99.0f) return "-inf dB";
    if (std::abs (db) < 0.05f) db = 0.0f;
    return (db > 0.0f ? "+" : "") + String (db, 1) + " dB";
}

std::optional<float> VolumeBox::parseDb (const String& raw)
{
    auto t = raw.trim().toLowerCase().replace (",", ".").replace ("db", "").trim();
    if (t.startsWith ("-inf") || t == String::fromUTF8 ("-∞")) return -100.0f;
    if (t.isEmpty() || ! t.containsAnyOf ("0123456789")) return {};
    if (t.retainCharacters ("+-.0123456789") != t) return {};
    return (float) t.getDoubleValue();
}

void VolumeBox::setTrack (te::Track* t)
{
    volume = nullptr;
    if (auto* at = dynamic_cast<te::AudioTrack*> (t)) volume = at->getVolumePlugin();
    else if (auto* f = dynamic_cast<te::FolderTrack*> (t)) volume = f->getVolumePlugin();
    setVisible (volume != nullptr);
    if (volume != nullptr) show (volume->getSliderPos());
}

void VolumeBox::show (float pos)
{
    if (! fader.isMouseButtonDown()) fader.setValue (pos, dontSendNotification);
    if (! number.isBeingEdited()) number.setText (dbText (volumeFaderToDb (pos)), dontSendNotification);
}

void VolumeBox::timerCallback()
{
    // the fader in the song or the mixer, automation, undo: the number follows
    if (auto* p = plugin()) if (std::abs ((float) fader.getValue() - p->getSliderPos()) > 1.0e-5f || ! number.isBeingEdited()) show (p->getSliderPos());
}

void VolumeBox::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (64);                      // "VOLUMEN"
    number.setBounds (r.removeFromRight (78).reduced (0, 2));
    r.removeFromRight (8);
    fader.setBounds (r);
}

void VolumeBox::paint (Graphics& g)
{
    g.setColour (S::ink);
    g.setFont (Fonts::label (12.0f));
    g.drawText (::racoon::i18n::tr ("VOLUMEN"), getLocalBounds().removeFromLeft (64), Justification::centredLeft);
}

ChannelView::ChannelView (ViewState& v) : vs (v)
{
    addChildComponent (volumeBox);
    strip.owner = this;
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    for (auto* b : { &addInstrument, &addEffect }) addAndMakeVisible (b);
    addChildComponent (newSequence);
    newSequence.setTooltip ("Abre el instrumento de este canal para escribir una secuencia; con «Pegar en maqueta» cae en la canción, en el compás donde está el cursor."_u);
    newSequence.setColour (TextButton::buttonColourId, Brand::limon);
    newSequence.onClick = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            createSequence (vs, *at, vs.timeToBeat (vs.session.getTransport().getPosition()));
    };
    addInstrument.setTooltip ("Pone un instrumento en este canal (reemplaza al que tenga)");
    addEffect.setTooltip ("Agrega un efecto al final de la cadena. También puedes arrastrarlo aquí desde EFECTOS, a la izquierda."_u);
    addInstrument.onClick = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            Menus::instrumentMenu (vs.session, [this, at] (const CatalogItem& it)
            {
                vs.edit().getUndoManager().beginNewTransaction();
                vs.session.insertPlugin (*at, it);
            }).showMenuAsync (PopupMenu::Options().withTargetComponent (addInstrument));
    };
    addEffect.onClick = [this]
    {
        if (track == nullptr) return;
        Menus::effectMenu (vs.session, [this] (const CatalogItem& it)
        {
            vs.edit().getUndoManager().beginNewTransaction();
            if (auto* at = dynamic_cast<te::AudioTrack*> (track.get())) vs.session.insertPlugin (*at, it);
            else vs.session.insertItem (*track, it);
        }).showMenuAsync (PopupMenu::Options().withTargetComponent (addEffect));
    };
    vs.session.getSelection().addChangeListener (this);
    startTimerHz (4);
}

ChannelView::~ChannelView()
{
    vs.session.getSelection().removeChangeListener (this);
}

void ChannelView::detach()
{
    volumeBox.setTrack (nullptr);
    RackEditor::closeAll();
    modCards.clear();
    cards.clear();
    track = nullptr;
    signature = {};
}

void ChannelView::changeListenerCallback (ChangeBroadcaster*)
{
    auto& sel = vs.session.getSelection();
    te::Track* t = nullptr;
    for (auto* x : sel.getItemsOfType<te::Track>()) { t = x; break; }
    if (t == nullptr)
        for (auto* c : sel.getItemsOfType<te::Clip>()) { t = c->getTrack(); break; }
    if (t != nullptr && t != track.get()) showTrack (t);
}

void ChannelView::showTrack (te::Track* t)
{
    track = t;
    volumeBox.setTrack (t);
    signature = {};
    rebuild();
}

String ChannelView::chainSignature() const
{
    if (track == nullptr) return "none";
    String s;
    for (auto* p : Session::chainOf (*track)) s << String::toHexString ((pointer_sized_int) p) << (p->isEnabled() ? "+" : "-");
    for (auto* m : devices::modulatorsOf (*track)) s << "m" << String::toHexString ((pointer_sized_int) m);
    s << track->getName();
    return s;
}

void ChannelView::timerCallback()
{
    // the track may have been deleted
    if (track != nullptr)
    {
        bool alive = false;
        for (auto* t : te::getAllTracks (vs.edit())) alive = alive || t == track.get();
        if (! alive) { track = nullptr; volumeBox.setTrack (nullptr); rebuild(); return; }
    }
    if (track == nullptr)
        for (auto* t : vs.session.getSelection().getItemsOfType<te::Track>()) { showTrack (t); return; }
    if (chainSignature() != signature) rebuild();
}

void ChannelView::rebuild()
{
    cards.clear();
    modCards.clear();
    signature = chainSignature();
    auto* at = dynamic_cast<te::AudioTrack*> (track.get());
    addInstrument.setVisible (at != nullptr && kindOf (*at) != TrackKind::returnBus);
    addEffect.setVisible (track != nullptr);
    newSequence.setVisible (at != nullptr && canCreateSequence (vs, *at));
    if (track != nullptr)
    {
        int i = 0;
        for (auto* p : Session::chainOf (*track))
            strip.addAndMakeVisible (cards.add (new DeviceCard (vs, *track, *p, i++)));
        for (auto* m : devices::modulatorsOf (*track))
            strip.addAndMakeVisible (modCards.add (new ModulatorCard (vs, *track, *m)));
    }
    resized();
    repaint();
}

void ChannelView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (headerH).reduced (12, 6);
    addEffect.setBounds (head.removeFromRight (110));
    head.removeFromRight (8);
    addInstrument.setBounds (head.removeFromRight (140));
    head.removeFromRight (8);
    if (newSequence.isVisible()) newSequence.setBounds (head.removeFromRight (170));
    // after the channel's name: its volume, in a fader and in dB
    head.removeFromLeft (18 + 20 + jmin (300, titleWidth()));
    volumeBox.setBounds (head.removeFromLeft (jmin (330, jmax (0, head.getWidth() - 10))).withSizeKeepingCentre (jmin (330, jmax (0, head.getWidth() - 10)), 26));
    viewport.setBounds (r.reduced (8, 4));
    int x = 8;
    const int h = jmax (120, viewport.getHeight() - (viewport.getHeight() > 200 ? 12 : 4));
    for (auto* c : cards)
    {
        c->setBounds (x, 4, c->preferredWidth(), jmin (h - 8, 230));
        x += c->preferredWidth() + cardGap;
    }
    if (! modCards.isEmpty())
    {
        x += 40;   // room for the divider that says MODULADORES
        modStartX = x - 30;
        for (auto* c : modCards)
        {
            c->setBounds (x, 4, c->preferredWidth(), jmin (h - 8, 230));
            x += c->preferredWidth() + 14;
        }
    }
    strip.setSize (jmax (viewport.getWidth(), x + 200), jmax (40, viewport.getHeight() - 10));
}

int ChannelView::titleWidth() const
{
    if (track == nullptr) return 0;
    return GlyphArrangement::getStringWidthInt (Fonts::title (16.0f), ::racoon::i18n::tr (trackTitle (*track))) + 8;
}

void ChannelView::paint (Graphics& g)
{
    // the panel is dressed in the colour of its channel
    const auto tc = track != nullptr ? colourForTrack (*track) : S::bg();
    g.fillAll (S::shade.interpolatedWith (tc, 0.10f));
    g.setColour (tc);
    g.fillRect (getLocalBounds().withHeight (3));
    auto head = getLocalBounds().removeFromTop (headerH).reduced (14, 0);
    if (track != nullptr)
    {
        g.setColour (tc);
        g.fillRect (head.removeFromLeft (8).reduced (0, 10));
        head.removeFromLeft (8);
        g.setColour (S::ink);
        g.setFont (Fonts::title (16.0f));
        g.drawText (::racoon::i18n::tr (trackTitle (*track)), head.removeFromLeft (jmin (300, titleWidth())), Justification::centredLeft);
        // the hint, only when there is room after the volume
        auto hint = head.withLeft (volumeBox.isVisible() ? volumeBox.getRight() + 16 : head.getX())
                        .withRight (jmin (head.getRight(), (newSequence.isVisible() ? newSequence : addInstrument).getX() - 12));
        if (hint.getWidth() > 220)
        {
            g.setFont (Fonts::body (12.5f));
            g.setColour (S::inkSoft);
            g.drawFittedText (::racoon::i18n::tr ("La señal va de izquierda a derecha. Arrastra la barra de una tarjeta para cambiar el orden."_u), hint, Justification::centredLeft, 2);
        }
    }
}

void ChannelView::Strip::paint (Graphics& g)
{
    auto& o = *owner;
    if (o.track == nullptr)
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (15.0f));
        g.drawText (::racoon::i18n::tr ("Elige un canal de la lista de pistas para ver su instrumento y sus efectos."_u), getLocalBounds().withWidth (o.viewport.getWidth()), Justification::centred);
        return;
    }
    if (o.cards.isEmpty())
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (15.0f));
        g.drawFittedText (::racoon::i18n::tr ("Este canal todavía no tiene nada. Pon un instrumento con + INSTRUMENTO o arrastra uno o un efecto desde la izquierda."_u),
                          getLocalBounds().withWidth (o.viewport.getWidth()).reduced (40, 0), Justification::centred, 2);
    }
    // arrows: the order the sound follows
    g.setColour (S::ink);
    for (int i = 0; i + 1 < o.cards.size(); ++i)
    {
        auto a = o.cards[i]->getBounds();
        const float y = (float) a.getCentreY(), x0 = (float) a.getRight() + 4.0f, x1 = x0 + (float) cardGap - 8.0f;
        g.drawLine (x0, y, x1 - 4.0f, y, 2.5f);
        Path p; p.addTriangle (x1, y, x1 - 7.0f, y - 6.0f, x1 - 7.0f, y + 6.0f);
        g.fillPath (p);
    }
    if (! o.modCards.isEmpty())
    {
        // divider between the signal chain and the modulators
        g.setColour (S::inkSoft);
        const float x = (float) o.modStartX;
        for (float y = 6.0f; y < (float) getHeight() - 6.0f; y += 10.0f) g.drawLine (x, y, x, y + 5.0f, 1.5f);
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, x + 14.0f, (float) getHeight() * 0.5f));
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("MODULADORES"), Rectangle<float> (x + 14.0f - 80.0f, (float) getHeight() * 0.5f - 7.0f, 160.0f, 14.0f), Justification::centred);
    }
    if (o.dropIndex >= 0)
    {
        g.setColour (Brand::rosa);
        g.fillRect (o.markerX() - 3.0f, 2.0f, 6.0f, (float) getHeight() - 4.0f);
    }
}

int ChannelView::indexForX (int x) const
{
    for (int i = 0; i < cards.size(); ++i)
        if (x < cards[i]->getBounds().getCentreX()) return i;
    return cards.size();
}

float ChannelView::markerX() const
{
    if (cards.isEmpty()) return 8.0f;
    if (dropIndex < cards.size()) return (float) cards[dropIndex]->getX() - (float) cardGap * 0.5f;
    return (float) cards.getLast()->getRight() + (float) cardGap * 0.5f;
}

bool ChannelView::isInterestedInDragSource (const SourceDetails& d)
{
    if (track == nullptr) return false;
    return d.description.toString().startsWith ("device:") || DragData::isPlugin (d.description);
}

void ChannelView::itemDragMove (const SourceDetails& d)
{
    const auto p = strip.getLocalPoint (this, d.localPosition);
    const int i = indexForX (p.x);
    if (i != dropIndex) { dropIndex = i; strip.repaint(); }
}

void ChannelView::itemDropped (const SourceDetails& d)
{
    const int target = dropIndex < 0 ? cards.size() : dropIndex;
    dropIndex = -1;
    if (track == nullptr) return;
    auto& um = vs.edit().getUndoManager();
    const auto desc = d.description.toString();
    if (desc.startsWith ("device:"))
    {
        const int from = desc.fromFirstOccurrenceOf (":", false, false).getIntValue();
        auto chain = Session::chainOf (*track);
        if (auto* p = chain[from])
        {
            um.beginNewTransaction();
            vs.session.movePlugin (*track, *p, target > from ? target - 1 : target);
        }
    }
    else if (DragData::isPlugin (d.description))
    {
        auto it = DragData::item (d.description);
        if (! it.available) { if (vs.showHint) vs.showHint (it.name + " no está instalado en este Mac."_u); return; }
        um.beginNewTransaction();
        te::Plugin::Ptr p;
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get())) p = vs.session.insertPlugin (*at, it);
        else p = vs.session.insertItem (*track, it);
        if (p != nullptr && ! Session::isInstrument (*p)) vs.session.movePlugin (*track, *p, target);
    }
    rebuild();
}
}

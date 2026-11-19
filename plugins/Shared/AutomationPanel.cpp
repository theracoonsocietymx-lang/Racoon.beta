#include "AutomationPanel.h"
#include "I18n.h"

namespace racoon
{
using namespace juce;

namespace
{
const Colour screen { 0xff1f1a1c }, ridge { 0xff4a4146 }, ridgeSoft { 0xff332b2f }, limon { 0xffd9c452 },
             hielo { 0xff5cf2e8 }, bone { 0xfff7f5f2 }, boneDim { 0xffb9b1b4 }, rosa { 0xffe0789a };
const String u8 (const char* s) { return String::fromUTF8 (s); }
Font fontOr (Font (*f) (float), float h) { return f != nullptr ? f (h) : Font (FontOptions (h)); }
}

// ===================================================================== lane editor
AutomationLaneEditor::AutomationLaneEditor (Automation& a, const AutomationStyle& s) : autom (a), st (s)
{
    setTooltip (u8 ("Clic: un punto. Arrastra: dibuja la línea. Arrastra un punto para moverlo. Doble clic o clic derecho en un punto: lo borra. Mayúsculas: sin rejilla."));
}

Rectangle<float> AutomationLaneEditor::area() const { return getLocalBounds().toFloat().reduced (1.0f).withTrimmedTop (16.0f); }

float AutomationLaneEditor::xOf (double beat) const
{
    auto a = area();
    return a.getX() + (float) (beat / autom.lengthBeats()) * a.getWidth();
}

double AutomationLaneEditor::beatOf (float x, bool snap) const
{
    auto a = area();
    const double len = autom.lengthBeats();
    double b = jlimit (0.0, len - 1.0e-6, (double) ((x - a.getX()) / jmax (1.0f, a.getWidth())) * len);
    if (snap) b = jlimit (0.0, len - grid, std::round (b / grid) * grid);
    return b;
}

float AutomationLaneEditor::yOf (float v) const
{
    auto a = area().reduced (0.0f, 6.0f);
    return a.getBottom() - v * a.getHeight();
}

float AutomationLaneEditor::valueOf (float y) const
{
    auto a = area().reduced (0.0f, 6.0f);
    float v = jlimit (0.0f, 1.0f, (a.getBottom() - y) / jmax (1.0f, a.getHeight()));
    if (auto* p = autom.parameter (paramId))
        if (p->isDiscrete() || p->isBoolean())
        {
            const int steps = jmax (1, p->getNumSteps() - 1);
            v = (float) roundToInt (v * (float) steps) / (float) steps;
        }
    return v;
}

int AutomationLaneEditor::pointAt (juce::Point<float> pos) const
{
    const auto pts = autom.points (paramId);
    int best = -1;
    float bestD = 9.0f;
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const float d = pos.getDistanceFrom ({ xOf (pts[(size_t) i].beat), yOf (pts[(size_t) i].value) });
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void AutomationLaneEditor::begin()
{
    before = autom.points (paramId);
    existedBefore = autom.hasLane (paramId);
    editing = true;
}

void AutomationLaneEditor::finish()
{
    if (! editing) return;
    editing = false;
    autom.commitEdit (paramId, before, existedBefore);
}

void AutomationLaneEditor::mouseDown (const MouseEvent& e)
{
    if (autom.parameter (paramId) == nullptr) return;
    mode = Mode::none;
    const int hit = pointAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            begin();
            auto pts = autom.points (paramId);
            pts.erase (pts.begin() + hit);
            autom.setPoints (paramId, pts, false);
            finish();
            return;
        }
        PopupMenu m;
        m.addItem (1, u8 ("Borrar toda la línea"), autom.hasPoints (paramId));
        m.addItem (2, u8 ("Línea recta con el valor de ahora"));
        m.addItem (3, u8 ("Subida en todo el loop"));
        m.addItem (4, u8 ("Bajada en todo el loop"));
        m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp = SafePointer<AutomationLaneEditor> (this)] (int r)
        {
            if (sp == nullptr || r == 0) return;
            auto& a = sp->autom;
            const auto& id = sp->paramId;
            sp->begin();
            const double end = a.lengthBeats() - sp->grid;
            if (r == 1) a.setPoints (id, {}, false);
            if (r == 2) if (auto* p = a.parameter (id)) a.setPoints (id, { { 0.0, p->getValue() } }, false);
            if (r == 3) a.setPoints (id, { { 0.0, 0.0f }, { end, 1.0f } }, false);
            if (r == 4) a.setPoints (id, { { 0.0, 1.0f }, { end, 0.0f } }, false);
            sp->finish();
        });
        return;
    }
    begin();
    if (hit >= 0)
    {
        mode = Mode::move;
        moving = autom.points (paramId)[(size_t) hit];
        return;
    }
    mode = Mode::draw;
    lastDrawBeat = -1.0;
    mouseDrag (e);
}

void AutomationLaneEditor::mouseDrag (const MouseEvent& e)
{
    if (mode == Mode::none) return;
    const bool snap = ! e.mods.isShiftDown();
    auto pts = autom.points (paramId);
    if (mode == Mode::move)
    {
        auto it = std::find (pts.begin(), pts.end(), moving);
        if (it == pts.end()) return;
        moving = { beatOf (e.position.x, snap), valueOf (e.position.y) };
        *it = moving;
        autom.setPoints (paramId, pts, false);
        return;
    }
    // drawing: the swept range gets one point per grid step, on the line between the last position and this one
    const double b = beatOf (e.position.x, snap);
    const float v = valueOf (e.position.y);
    const double from = lastDrawBeat < 0.0 ? b : lastDrawBeat;
    const float fromV = lastDrawBeat < 0.0 ? v : lastDrawValue;
    const double lo = jmin (from, b), hi = jmax (from, b);
    pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (auto& q) { return q.beat >= lo - 1.0e-9 && q.beat <= hi + 1.0e-9; }), pts.end());
    const double step = snap ? grid : jmax (1.0e-3, (hi - lo) / 8.0);
    for (double x = lo; x <= hi + 1.0e-9; x += step)
    {
        const double t = hi > lo ? (x - from) / (b - from) : 1.0;
        pts.push_back ({ x, (float) (fromV + jlimit (0.0, 1.0, t) * (v - fromV)) });
        if (hi <= lo) break;
    }
    lastDrawBeat = b;
    lastDrawValue = v;
    autom.setPoints (paramId, pts, false);
}

void AutomationLaneEditor::mouseUp (const MouseEvent&)
{
    if (mode != Mode::none) finish();
    mode = Mode::none;
}

void AutomationLaneEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int hit = pointAt (e.position);
    if (hit < 0) return;
    begin();
    auto pts = autom.points (paramId);
    pts.erase (pts.begin() + hit);
    autom.setPoints (paramId, pts, false);
    finish();
}

void AutomationLaneEditor::mouseMove (const MouseEvent& e)
{
    hover = e.position;
    setMouseCursor (pointAt (e.position) >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
    repaint();
}

void AutomationLaneEditor::paint (Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (screen);
    g.fillRoundedRectangle (full, 4.0f);
    auto a = area();
    const double len = autom.lengthBeats();
    const int bars = autom.bars();

    // grid: sixteenths, beats, bars with their number
    for (double b = 0.0; b < len + 1.0e-9; b += 0.25)
    {
        const float x = xOf (b);
        const bool bar = std::fmod (b, 4.0) < 1.0e-9, beat = std::fmod (b, 1.0) < 1.0e-9;
        if (! beat && a.getWidth() / (float) (len * 4.0) < 7.0f) continue;
        g.setColour (bar ? ridge.brighter (0.4f) : beat ? ridge : ridgeSoft);
        g.fillRect (x, a.getY(), bar ? 1.5f : 1.0f, a.getHeight());
        if (bar && b < len - 1.0e-9)
        {
            g.setColour (boneDim);
            g.setFont (fontOr (st.mono, 13.0f));
            g.drawText (::racoon::i18n::tr (String ((int) (b / 4.0) + 1)), Rectangle<float> (x + 3.0f, full.getY() + 1.0f, 30.0f, 14.0f), Justification::centredLeft);
        }
    }
    g.setColour (ridge);
    g.drawHorizontalLine ((int) yOf (0.5f), a.getX(), a.getRight());

    auto* p = autom.parameter (paramId);
    if (p == nullptr)
    {
        g.setColour (boneDim);
        g.setFont (fontOr (st.body, 13.0f));
        g.drawText (::racoon::i18n::tr (u8 ("Clic derecho en cualquier perilla > Ver automatización")), a, Justification::centred);
        return;
    }
    const auto pts = autom.points (paramId);
    const bool discrete = p->isDiscrete() || p->isBoolean();
    if (pts.empty())
    {
        g.setColour (boneDim);
        g.setFont (fontOr (st.body, 13.0f));
        g.drawText (::racoon::i18n::tr (u8 ("Haz clic para poner el primer punto, o arrastra para dibujar la línea de ") + p->getName (40) + "."),
                    a, Justification::centred);
    }
    else
    {
        Path line;
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const float y = yOf (Automation::valueAt (pts, beatOf (x, false), len, discrete));
            if (x == a.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        Path fill (line);
        fill.lineTo (a.getRight(), a.getBottom());
        fill.lineTo (a.getX(), a.getBottom());
        fill.closeSubPath();
        g.setColour (limon.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (limon);
        g.strokePath (line, PathStrokeType (2.0f));
        for (auto& q : pts)
        {
            auto c = Rectangle<float> (9.0f, 9.0f).withCentre ({ xOf (q.beat), yOf (q.value) });
            g.setColour (limon);
            g.fillEllipse (c);
            g.setColour (screen);
            g.drawEllipse (c, 1.5f);
        }
    }

    // where the loop is now
    const double ph = autom.playheadBeat();
    if (ph >= 0.0)
    {
        g.setColour (hielo);
        g.fillRect (xOf (ph) - 1.0f, a.getY(), 2.0f, a.getHeight());
    }
    // the value under the mouse
    if (a.contains (hover))
    {
        const float v = valueOf (hover.y);
        const auto txt = p->getText (v, 40) + (p->getLabel().isNotEmpty() ? " " + p->getLabel() : String());
        g.setFont (fontOr (st.mono, 14.0f));
        auto box = Rectangle<float> (GlyphArrangement::getStringWidth (g.getCurrentFont(), ::racoon::i18n::tr (txt)) + 12.0f, 18.0f)
                       .withPosition (jmin (hover.x + 10.0f, a.getRight() - 140.0f), jmax (a.getY(), hover.y - 22.0f));
        g.setColour (bone);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (screen);
        g.drawText (::racoon::i18n::tr (txt), box, Justification::centred);
    }
    ignoreUnused (bars);
}

// ===================================================================== panel
AutomationPanel::AutomationPanel (Automation& a, AutomationStyle s) : lane (a, style), autom (a), style (s)
{
    which.setTooltip (u8 ("El parámetro que estás viendo. Los que ya tienen línea aparecen primero."));
    which.onChange = [this]
    {
        const int i = which.getSelectedItemIndex();
        if (i >= 0 && i < listed.size()) showParam (listed[i]);
    };
    for (int b : { 1, 2, 4, 8, 16 }) bars.addItem (String (b) + (b == 1 ? u8 (" compás") : String (" compases")), b);
    bars.setSelectedId (autom.bars(), dontSendNotification);
    bars.setTooltip (u8 ("Largo del loop de la automatización. 4 compases son una frase; 16, un bloque."));
    bars.onChange = [this] { autom.setBars (bars.getSelectedId()); };
    record.setClickingTogglesState (true);
    record.setTooltip (u8 ("Encendido: mientras suena, lo que muevas con el ratón queda grabado en su línea."));
    record.onClick = [this] { setRecording (record.getToggleState()); };
    record.setColour (TextButton::buttonOnColourId, Colour (rosa));
    clear.setTooltip (u8 ("Borra la línea de este parámetro (se puede deshacer)"));
    clear.onClick = [this] { autom.removeLane (lane.getParam()); };
    other.setTooltip (u8 ("Automatizar otro parámetro de este plugin"));
    other.onClick = [this] { showOtherMenu(); };
    close.setTooltip (u8 ("Ocultar la automatización. Las líneas siguen sonando."));
    close.onClick = [this] { setRecording (false); if (onClose) onClose(); };
    for (Component* c : { (Component*) &which, (Component*) &bars, (Component*) &record, (Component*) &clear, (Component*) &other,
                          (Component*) &close, (Component*) &lane })
        addAndMakeVisible (c);
    autom.addChangeListener (this);
    refreshList();
    startTimerHz (30);
}

AutomationPanel::~AutomationPanel()
{
    autom.recording.store (false);
    autom.removeChangeListener (this);
}

void AutomationPanel::setRecording (bool on)
{
    autom.recording.store (on);
    record.setToggleState (on, dontSendNotification);
    record.setButtonText (on ? u8 ("Grabando") : u8 ("Grabar"));
}

void AutomationPanel::showParam (const String& id)
{
    lane.setParam (id);
    refreshList();
    repaint();
}

void AutomationPanel::refreshList()
{
    listed.clear();
    which.clear (dontSendNotification);
    for (auto& id : autom.laneIds()) listed.addIfNotAlreadyThere (id);
    if (lane.getParam().isNotEmpty()) listed.addIfNotAlreadyThere (lane.getParam());
    for (int i = 0; i < listed.size(); ++i)
        which.addItem ((autom.hasPoints (listed[i]) ? String() : String ("(sin línea) ")) + autom.nameOf (listed[i]), i + 1);
    which.setSelectedItemIndex (listed.indexOf (lane.getParam()), dontSendNotification);
    bars.setSelectedId (autom.bars(), dontSendNotification);
    clear.setEnabled (autom.hasLane (lane.getParam()));
}

void AutomationPanel::showOtherMenu()
{
    PopupMenu m;
    const auto ids = autom.automatableIds();
    // long lists go in groups so the menu fits on screen
    const int per = 30;
    if (ids.size() <= per)
        for (auto& id : ids) m.addItem (autom.nameOf (id), [this, id] { showParam (id); });
    else
        for (int start = 0; start < ids.size(); start += per)
        {
            PopupMenu sub;
            for (int i = start; i < jmin (ids.size(), start + per); ++i)
            {
                const auto id = ids[i];
                sub.addItem (autom.nameOf (id), [this, id] { showParam (id); });
            }
            m.addSubMenu (autom.nameOf (ids[start]).substring (0, 22) + "...", sub);
        }
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (other));
}

void AutomationPanel::changeListenerCallback (ChangeBroadcaster*)
{
    refreshList();
    lane.repaint();
    repaint();
}

void AutomationPanel::timerCallback()
{
    autom.recordTick();
    if (autom.playheadBeat() >= 0.0 || lane.isMouseOverOrDragging()) lane.repaint();
    if (record.getToggleState() != autom.recording.load()) setRecording (autom.recording.load());
}

void AutomationPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto top = r.removeFromTop (30);
    titleArea = top.removeFromLeft (150);
    which.setBounds (top.removeFromLeft (jmin (300, top.getWidth() / 3)).reduced (0, 2));
    top.removeFromLeft (6);
    other.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (14);
    record.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (6);
    clear.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (14);
    bars.setBounds (top.removeFromLeft (130).reduced (0, 2));
    close.setBounds (top.removeFromRight (84));
    ccArea = top.reduced (10, 0);
    r.removeFromTop (6);
    lane.setBounds (r);
}

void AutomationPanel::paint (Graphics& g)
{
    g.setColour (style.surface);
    g.fillRect (getLocalBounds());
    g.setColour (style.ink);
    g.fillRect (getLocalBounds().removeFromTop (2));
    g.setFont (fontOr (style.label, 13.0f));
    g.drawText (::racoon::i18n::tr (u8 ("AUTOMATIZACIÓN")), titleArea.withTrimmedLeft (2), Justification::centredLeft);
    const int cc = autom.ccFor (lane.getParam());
    g.setColour (style.inkSoft);
    g.setFont (fontOr (style.body, 12.5f));
    String info;
    if (cc >= 0) info << u8 ("Al exportar viaja como MIDI CC ") << cc << ". ";
    info << u8 ("Suena con Play y con tu DAW.");
    g.drawFittedText (::racoon::i18n::tr (info), ccArea, Justification::centredRight, 2, 0.9f);
}
}

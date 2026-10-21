#include "Dock.h"
#include "I18n.h"

namespace racoon
{
using namespace juce;
using namespace racoon::ui;

namespace
{
bool isBlack (int p) { const int k = ((p % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
String noteName (int p)
{
    static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (n[((p % 12) + 12) % 12]) + String (p / 12 - 1);
}
}

// ===================================================================== symbols
void SymbolButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = symbol == Symbol::play && playing;
    // flat dark block with a hairline; Menta when playing
    g.setColour (on ? Brand::menta.withAlpha (0.18f) : (down ? Pro::line : Pro::raised));
    g.fillRect (r.reduced (0.5f));
    g.setColour (on ? Brand::menta : (over ? Pro::lineHi : Pro::line));
    g.drawRect (r.reduced (0.5f), 1.0f);
    auto c = r;
    // undo and redo say it in words too, next to the arrow
    String word;
    if (symbol == Symbol::undo || symbol == Symbol::redo)
    {
        word = ::racoon::i18n::tr (symbol == Symbol::undo ? "Deshacer" : "Rehacer");
        if (c.getWidth() > c.getHeight() * 2.0f)
        {
            g.setColour (! isEnabled() ? Pro::textFaint : Pro::text);
            g.setFont (Fonts::label (13.0f));
            g.drawText (word, c.withTrimmedLeft (c.getHeight() * 0.9f).toNearestInt(), Justification::centredLeft);
            c = c.withWidth (c.getHeight() * 1.05f);
        }
    }
    auto s = c.withSizeKeepingCentre (jmin (c.getWidth(), c.getHeight()) * 0.5f, jmin (c.getWidth(), c.getHeight()) * 0.5f);
    g.setColour (! isEnabled() ? Pro::textFaint : (on ? Brand::menta : Pro::text));
    Path p;
    switch (symbol)
    {
        case Symbol::play:
            if (playing) p.addRectangle (s.reduced (s.getWidth() * 0.08f));
            else p.addTriangle (s.getX() + s.getWidth() * 0.12f, s.getY(), s.getX() + s.getWidth() * 0.12f, s.getBottom(), s.getRight(), s.getCentreY());
            g.fillPath (p);
            break;
        case Symbol::undo:
        case Symbol::redo:
        {
            // a hook arrow, the one everybody knows
            const bool u = symbol == Symbol::undo;
            Path arc;
            const float cx = s.getCentreX(), cy = s.getCentreY() + s.getHeight() * 0.12f, rad = s.getWidth() * 0.42f;
            arc.addCentredArc (cx, cy, rad, rad, 0.0f, u ? -MathConstants<float>::pi * 0.15f : MathConstants<float>::pi * 0.15f,
                               u ? MathConstants<float>::pi * 0.95f : -MathConstants<float>::pi * 0.95f, true);
            g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            const float ax = u ? cx - rad * std::sin (MathConstants<float>::pi * 0.15f) : cx + rad * std::sin (MathConstants<float>::pi * 0.15f);
            const float ay = cy - rad * std::cos (MathConstants<float>::pi * 0.15f);
            Path head;
            const float h = s.getWidth() * 0.3f;
            if (u) head.addTriangle (ax - h, ay - h * 0.1f, ax + h * 0.25f, ay - h * 0.75f, ax + h * 0.25f, ay + h * 0.55f);
            else   head.addTriangle (ax + h, ay - h * 0.1f, ax - h * 0.25f, ay - h * 0.75f, ax - h * 0.25f, ay + h * 0.55f);
            g.fillPath (head);
            break;
        }
        case Symbol::octDown:
        case Symbol::octUp:
        {
            g.setFont (Fonts::label (13.0f));
            g.drawText (::racoon::i18n::tr (symbol == Symbol::octDown ? "Oct -" : "Oct +"), c.toNearestInt(), Justification::centred);
            break;
        }
    }
}

// ===================================================================== piano roll
PianoRollStrip::PianoRollStrip (RacoonProcessor& p) : proc (p)
{
    notes = proc.getPattern();
    setTooltip ("Secuencia: doble clic escribe una nota y doble clic en una nota la borra. Clic en una nota: seleccionarla (Mayús suma); "
                "arrastrar en un hueco: seleccionar varias. Arrastra para mover las seleccionadas, su borde derecho para alargarlas, "
                "Alt+arrastrar copia. Flechas: medio tono (Mayús, octava); Cmd+Mayús+flecha: copia una octava arriba o abajo; Cmd+D duplica. "
                "Tecla B: lápiz (un clic escribe). Las teclas de la izquierda suenan. La rueda sube y baja; Shift + rueda (o la barra de abajo) "
                "avanza en el tiempo; Cmd + rueda acerca y aleja."_u);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (hbar);
    hbar.setAutoHide (false);
    hbar.setColour (ScrollBar::thumbColourId, Brand::limon);
    hbar.setColour (ScrollBar::trackColourId, Colour (0xff1f1a1c));
    hbar.addListener (this);
    showFromStart();
    startTimerHz (30);
}

void PianoRollStrip::resized()
{
    hbar.setBounds (keysW, getHeight() - barH, getWidth() - keysW, barH);
    centreOnNotes();
}

void PianoRollStrip::clampView()
{
    const double len = lengthBeats();
    viewBeats = jlimit (jmin (len, 1.0), len, viewBeats);
    viewStart = jlimit (0.0, jmax (0.0, len - viewBeats), viewStart);
    hbar.setRangeLimits (0.0, len, dontSendNotification);
    hbar.setCurrentRange (viewStart, viewBeats, dontSendNotification);
}

void PianoRollStrip::showFromStart()
{
    viewStart = 0.0;
    viewBeats = jmin (lengthBeats(), 16.0);       // four bars on screen; the rest is a scroll away
    clampView();
    repaint();
}

void PianoRollStrip::zoomBy (double factor, double aroundBeat)
{
    const double rel = (aroundBeat - viewStart) / jmax (1.0e-6, viewBeats);
    viewBeats = viewBeats * factor;
    clampView();
    viewStart = aroundBeat - rel * viewBeats;
    clampView();
    repaint();
}

void PianoRollStrip::scrollBarMoved (ScrollBar*, double newRangeStart)
{
    viewStart = newRangeStart;
    clampView();
    repaint();
}

void PianoRollStrip::mouseMagnify (const MouseEvent& e, float scale)
{
    if (scale > 0.0f) zoomBy (1.0 / scale, xToBeat (e.position.x));
}

void PianoRollStrip::centreOnNotes()
{
    if (drag == Drag::none) notes = proc.getPattern();
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
    const float rows = (float) getHeight() / rowH;
    if (lo > hi) { topPitch = 72.0f + rows * 0.5f; return; }
    topPitch = (float) (lo + hi) * 0.5f + rows * 0.5f;
}

Rectangle<float> PianoRollStrip::gridArea() const { return getLocalBounds().toFloat().withTrimmedLeft ((float) keysW).withTrimmedBottom ((float) barH); }
double PianoRollStrip::xToBeat (float x) const { auto g = gridArea(); return viewStart + (double) (x - g.getX()) / g.getWidth() * viewBeats; }
float PianoRollStrip::beatToX (double b) const { auto g = gridArea(); return g.getX() + (float) ((b - viewStart) / viewBeats) * g.getWidth(); }
int PianoRollStrip::yToPitch (float y) const { return jlimit (0, 127, (int) std::floor (topPitch - y / rowH) + 1); }
float PianoRollStrip::pitchToY (int p) const { return (topPitch - (float) p) * rowH; }

int PianoRollStrip::noteAt (Point<float> pos) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(size_t) i];
        Rectangle<float> r (beatToX (n.start), pitchToY (n.pitch), beatToX (n.start + n.length) - beatToX (n.start), rowH);
        if (r.expanded (0.0f, 1.0f).contains (pos)) return i;
    }
    return -1;
}

void PianoRollStrip::timerCallback()
{
    if (drag == Drag::none)
    {
        auto now = proc.getPattern();
        if (now != notes) notes = std::move (now);
    }
    if (std::abs (hbar.getMaximumRangeLimit() - lengthBeats()) > 1.0e-6) clampView();
    repaint();
}

void PianoRollStrip::paint (Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.setColour (Colour (0xff2c2629));
    g.fillRect (all);
    auto ga = gridArea();
    const int lo = yToPitch ((float) getHeight()), hi = yToPitch (0.0f);
    // rows: black keys darker, a line at every C
    for (int p = lo; p <= hi; ++p)
    {
        const float y = pitchToY (p);
        g.setColour (isBlack (p) ? Colour (0xff2f292c) : Colour (0xff3b3437));
        g.fillRect (ga.getX(), y, ga.getWidth(), rowH);
        if (p % 12 == 0) { g.setColour (C::bone.withAlpha (0.16f)); g.fillRect (ga.getX(), y + rowH - 1.0f, ga.getWidth(), 1.0f); }
    }
    // beats and bars (with the bar number, so you know where you are after scrolling)
    const double len = lengthBeats();
    g.saveState();
    g.reduceClipRegion (ga.toNearestInt());
    const double step = viewBeats > 32.0 && grid < 0.5 ? 1.0 : grid;
    for (double b = std::floor (viewStart / step) * step; b <= jmin (len, viewStart + viewBeats) + 1.0e-6; b += step)
    {
        const bool bar = std::abs (std::fmod (b, 4.0)) < 1.0e-6, beat = std::abs (b - std::round (b)) < 1.0e-6;
        g.setColour (C::bone.withAlpha (bar ? 0.3f : beat ? 0.12f : 0.05f));
        g.fillRect (beatToX (b), ga.getY(), 1.0f, ga.getHeight());
        if (bar)
        {
            g.setColour (C::bone.withAlpha (0.5f));
            g.setFont (Fonts::mono (10.0f));
            g.drawText (::racoon::i18n::tr (String ((int) std::lround (b / 4.0) + 1)), (int) beatToX (b) + 3, (int) ga.getY() + 1, 30, 12, Justification::centredLeft);
        }
    }
    // notes
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (n.start >= len) continue;
        Rectangle<float> r (beatToX (n.start), pitchToY (n.pitch), jmax (4.0f, beatToX (jmin (len, n.start + n.length)) - beatToX (n.start)), rowH);
        const bool sel = (int) i == dragIndex || isSelected (n);
        g.setColour (sel ? Brand::limon : Brand::menta.withMultipliedAlpha (0.55f + 0.45f * (float) n.velocity / 127.0f));
        g.fillRoundedRectangle (r.reduced (0.5f, 0.5f), 2.0f);
        g.setColour (S::shade);
        g.drawRoundedRectangle (r.reduced (0.5f, 0.5f), 2.0f, sel ? 2.0f : 1.0f);
    }
    if (! rubberRect.isEmpty())
    {
        g.setColour (Brand::limon.withAlpha (0.12f));
        g.fillRect (rubberRect);
        g.setColour (Brand::limon);
        g.drawRect (rubberRect, 1.0f);
    }
    // playhead
    const double ph = proc.seqPositionBeats();
    if (ph >= 0.0)
    {
        g.setColour (Brand::hielo);
        g.fillRect (beatToX (ph) - 0.5f, ga.getY(), 2.0f, ga.getHeight());
    }
    g.restoreState();
    // keys
    auto keys = all.withWidth ((float) keysW);
    for (int p = lo; p <= hi; ++p)
    {
        const float y = pitchToY (p);
        auto k = Rectangle<float> (keys.getX(), y, keys.getWidth(), rowH);
        g.setColour (isBlack (p) ? S::shade : Colours::white);
        g.fillRect (isBlack (p) ? k.withTrimmedRight (14.0f) : k);
        g.setColour (S::shade.withAlpha (0.25f));
        g.fillRect (k.getX(), k.getBottom() - 1.0f, k.getWidth(), 1.0f);
        if (p % 12 == 0 && rowH >= 8.0f)
        {
            g.setColour (S::shade);
            g.setFont (Fonts::mono (jmin (10.5f, rowH)));
            g.drawText (::racoon::i18n::tr (noteName (p)), k.reduced (3.0f, 0.0f), Justification::centredRight);
        }
    }
    g.setColour (S::shade);
    g.fillRect (keys.getRight() - 1.0f, 0.0f, 1.0f, all.getHeight());
    if (notes.empty())
    {
        g.setColour (C::boneDim);
        g.setFont (Fonts::body (13.0f));
        g.drawText (::racoon::i18n::tr ("Doble clic aquí para escribir notas, o pulsa EJEMPLO."_u), ga.toNearestInt(), Justification::centred);
    }
}

bool PianoRollStrip::isSelected (const SeqNote& n) const
{
    for (auto& x : selected) if (x == n) return true;
    return false;
}

void PianoRollStrip::mouseDown (const MouseEvent& e)
{
    const auto pos = e.position;
    if (pos.x < (float) keysW)
    {
        auditionNote = yToPitch (pos.y);
        proc.keyboardState.noteOn (1, auditionNote, 0.8f);
        return;
    }
    grabKeyboardFocus();
    const int hit = noteAt (pos);
    if (e.mods.isPopupMenu())
    {
        if (hit >= 0) { notes.erase (notes.begin() + hit); selected.clear(); proc.setPattern (notes); }
        return;
    }
    changed = false;
    moved = false;
    downPos = pos;
    if (! drawMode)
    {
        if (hit >= 0)
        {
            const auto n = notes[(size_t) hit];
            downNote = n;
            downShift = e.mods.isShiftDown();
            if (downShift)
            {
                if (isSelected (n)) { selected.erase (std::remove (selected.begin(), selected.end(), n), selected.end()); repaint(); return; }
                selected.push_back (n);
            }
            else if (! isSelected (n)) selected = { n };
            dragNotes = notes;
            dragSel = selected;
            dragCopy = e.mods.isAltDown();
            drag = pos.x > beatToX (n.start + n.length) - 6.0f ? Drag::resizeSel : Drag::moveSel;
            proc.keyboardState.noteOn (1, n.pitch, 0.8f);
            auditionNote = n.pitch;
            repaint();
            return;
        }
        selectedAtDown = e.mods.isShiftDown() ? selected : std::vector<SeqNote>();
        selected = selectedAtDown;
        rubberRect = {};
        drag = Drag::rubber;
        repaint();
        return;
    }
    // the pencil: a click on a note erases it (on release, if it did not move), a click on a hole writes one
    if (hit >= 0)
    {
        dragIndex = hit;
        const auto& n = notes[(size_t) hit];
        drag = pos.x > beatToX (n.start + n.length) - 6.0f ? Drag::resize : Drag::move;
    }
    else
    {
        // a new note, on the grid, one grid step long
        SeqNote n;
        n.start = jlimit (0.0, lengthBeats() - grid, std::floor (xToBeat (pos.x) / grid) * grid);
        n.length = grid;
        n.pitch = yToPitch (pos.y);
        n.velocity = 100;
        notes.push_back (n);
        dragIndex = (int) notes.size() - 1;
        drag = Drag::resize;
        changed = true;
        proc.keyboardState.noteOn (1, n.pitch, 0.8f);
        auditionNote = n.pitch;
    }
    dragStartBeat = xToBeat (pos.x);
    noteStartAtDrag = notes[(size_t) dragIndex].start;
    lengthAtDrag = notes[(size_t) dragIndex].length;
    pitchAtDrag = notes[(size_t) dragIndex].pitch;
}

void PianoRollStrip::applyDrag (double dBeats, int dPitch, double dLen)
{
    // the notes selected when the drag began move together; none leaves the pattern or the keyboard
    const double len = lengthBeats();
    double first = len, last = 0.0;
    int lo = 127, hi = 0;
    for (auto& n : dragSel) { first = jmin (first, n.start); last = jmax (last, n.start); lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
    if (dragSel.empty()) return;
    dBeats = jlimit (-first, len - grid - last, dBeats);
    dPitch = jlimit (-lo, 127 - hi, dPitch);
    std::vector<SeqNote> out, now;
    std::vector<SeqNote> pending = dragSel;
    for (auto& n : dragNotes)
    {
        auto it = std::find (pending.begin(), pending.end(), n);
        const bool sel = it != pending.end();
        if (sel) pending.erase (it);
        if (! sel || dragCopy) out.push_back (n);
        if (! sel) continue;
        auto m = n;
        m.start = jmax (0.0, n.start + dBeats);
        m.pitch = n.pitch + dPitch;
        m.length = jlimit (grid * 0.5, len - m.start, n.length + dLen);
        out.push_back (m);
        now.push_back (m);
    }
    notes = out;
    selected = now;
    changed = true;
    repaint();
}

void PianoRollStrip::mouseDrag (const MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 3) moved = true;
    if (drag == Drag::rubber)
    {
        rubberRect = Rectangle<float> (downPos, e.position);
        selected = selectedAtDown;
        for (auto& n : notes)
        {
            Rectangle<float> r (beatToX (n.start), pitchToY (n.pitch), jmax (4.0f, beatToX (n.start + n.length) - beatToX (n.start)), rowH);
            if (r.intersects (rubberRect) && ! isSelected (n)) selected.push_back (n);
        }
        repaint();
        return;
    }
    if (drag == Drag::moveSel || drag == Drag::resizeSel)
    {
        if (! moved) return;
        const double d = std::round ((xToBeat (e.position.x) - xToBeat (downPos.x)) / grid) * grid;
        if (drag == Drag::moveSel)
        {
            const int dp = (int) std::round ((downPos.y - e.position.y) / rowH);
            applyDrag (d, dp, 0.0);
            const int p = jlimit (0, 127, downNote.pitch + dp);
            if (p != auditionNote) { proc.keyboardState.allNotesOff (1); proc.keyboardState.noteOn (1, p, 0.7f); auditionNote = p; }
        }
        else applyDrag (0.0, 0, d);
        return;
    }
    if (dragIndex < 0 || drag == Drag::none) return;
    auto& n = notes[(size_t) dragIndex];
    const double d = xToBeat (e.position.x) - dragStartBeat;
    if (drag == Drag::move)
    {
        n.start = jlimit (0.0, lengthBeats() - grid, std::round ((noteStartAtDrag + d) / grid) * grid);
        const int p = jlimit (0, 127, pitchAtDrag + (int) std::round ((e.getMouseDownY() - e.y) / rowH));
        if (p != n.pitch) { n.pitch = p; proc.keyboardState.allNotesOff (1); proc.keyboardState.noteOn (1, p, 0.7f); auditionNote = p; }
    }
    else
        n.length = jlimit (grid, lengthBeats() - n.start, std::round ((lengthAtDrag + d) / grid) * grid);
    changed = true;
    repaint();
}

void PianoRollStrip::mouseUp (const MouseEvent&)
{
    if (auditionNote >= 0) { proc.keyboardState.noteOff (1, auditionNote, 0.0f); auditionNote = -1; }
    // the pencil: a click on a note (no drag) erases it
    if (drawMode && drag == Drag::move && ! changed && dragIndex >= 0) { notes.erase (notes.begin() + dragIndex); changed = true; }
    // a click (no drag) on one of several selected notes leaves only that one selected
    if (drag == Drag::moveSel && ! moved && ! downShift) selected = { downNote };
    if (changed) proc.setPattern (notes);
    drag = Drag::none;
    dragIndex = -1;
    changed = false;
    rubberRect = {};
    repaint();
}

void PianoRollStrip::mouseDoubleClick (const MouseEvent& e)
{
    if (e.position.x < (float) keysW) return;
    const int hit = noteAt (e.position);
    if (hit >= 0)
    {
        notes.erase (notes.begin() + hit);
        selected.clear();
        proc.setPattern (notes);
        return;
    }
    if (drawMode) return;
    // double click on an empty spot writes a note, one grid step long
    SeqNote n;
    n.start = jlimit (0.0, lengthBeats() - grid, std::floor (xToBeat (e.position.x) / grid) * grid);
    n.length = grid;
    n.pitch = yToPitch (e.position.y);
    n.velocity = 100;
    notes.push_back (n);
    selected = { n };
    proc.setPattern (notes);
    repaint();
}

void PianoRollStrip::changeSelected (bool keepOriginals, const std::function<void (SeqNote&)>& f)
{
    if (selected.empty()) return;
    std::vector<SeqNote> out, now, pending = selected;
    for (auto& n : notes)
    {
        auto it = std::find (pending.begin(), pending.end(), n);
        const bool sel = it != pending.end();
        if (sel) pending.erase (it);
        if (! sel || keepOriginals) out.push_back (n);
        if (! sel) continue;
        auto m = n;
        f (m);
        out.push_back (m);
        now.push_back (m);
    }
    notes = out;
    selected = now;
    proc.setPattern (notes);
    repaint();
}

bool PianoRollStrip::keyPressed (const KeyPress& k)
{
    proc.undoManager.beginNewTransaction();          // every key is its own Deshacer step
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) code);
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown();
    const double len = lengthBeats();
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey)
    {
        if (selected.empty()) return false;
        std::vector<SeqNote> out, pending = selected;
        for (auto& n : notes)
        {
            auto it = std::find (pending.begin(), pending.end(), n);
            if (it != pending.end()) pending.erase (it); else out.push_back (n);
        }
        notes = out; selected.clear(); proc.setPattern (notes); repaint();
        return true;
    }
    if (code == KeyPress::escapeKey) { selected.clear(); repaint(); return true; }
    if (! cmd && ! shift && ch == 'b') { setDrawMode (! drawMode); return true; }
    if (cmd && ch == 'a') { selected = notes; repaint(); return true; }
    if (cmd && ch == 'c') { clipboard = selected; return true; }
    if (cmd && (ch == 'v' || ch == 'd'))
    {
        // Cmd+D: a copy right after the selection; Cmd+V: the copied notes right after the selection (or where they were)
        const auto src = ch == 'd' ? selected : clipboard;
        if (src.empty()) return true;
        double first = len, end = 0.0, selEnd = 0.0;
        for (auto& n : src) { first = jmin (first, n.start); end = jmax (end, n.start + n.length); }
        for (auto& n : selected) selEnd = jmax (selEnd, n.start + n.length);
        const double span = std::ceil ((end - first) / grid - 1.0e-6) * grid;
        const double at = ch == 'd' ? first + span : (selEnd > 0.0 ? std::ceil (selEnd / grid - 1.0e-6) * grid : first);
        std::vector<SeqNote> now;
        for (auto n : src)
        {
            n.start += at - first;
            if (n.start >= len - 1.0e-6) continue;
            n.length = jmin (n.length, len - n.start);
            notes.push_back (n);
            now.push_back (n);
        }
        selected = now;
        proc.setPattern (notes);
        repaint();
        return true;
    }
    if (code == KeyPress::upKey || code == KeyPress::downKey)
    {
        const int dir = code == KeyPress::upKey ? 1 : -1;
        if (selected.empty()) return false;
        const int semis = (shift ? 12 : 1) * dir;
        changeSelected (cmd && shift, [semis] (SeqNote& n) { n.pitch = jlimit (0, 127, n.pitch + semis); });
        return true;
    }
    if (code == KeyPress::leftKey || code == KeyPress::rightKey)
    {
        const int dir = code == KeyPress::leftKey ? -1 : 1;
        if (selected.empty()) return false;
        if (shift) changeSelected (false, [this, dir, len] (SeqNote& n) { n.length = jlimit (grid, len - n.start, n.length + dir * grid); });
        else
        {
            double first = len, last = 0.0;
            for (auto& n : selected) { first = jmin (first, n.start); last = jmax (last, n.start); }
            const double d = jlimit (-first, len - grid - last, dir * grid);
            changeSelected (false, [d] (SeqNote& n) { n.start += d; });
        }
        return true;
    }
    return false;
}

void PianoRollStrip::mouseMove (const MouseEvent& e)
{
    const int hit = noteAt (e.position);
    if (hit < 0) { setMouseCursor (e.position.x < (float) keysW ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor); return; }
    const auto& n = notes[(size_t) hit];
    setMouseCursor (e.position.x > beatToX (n.start + n.length) - 6.0f ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
}

void PianoRollStrip::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (w.deltaY > 0.0f ? 0.8 : 1.25, xToBeat (e.position.x));
        return;
    }
    const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;
    if (std::abs (dx) > 0.0f)
    {
        viewStart -= dx * viewBeats * 0.5;
        clampView();
    }
    if (! e.mods.isShiftDown() && std::abs (w.deltaY) > 0.0f)
        topPitch = jlimit ((float) getHeight() / rowH, 127.0f, topPitch + w.deltaY * 12.0f);
    repaint();
}

// ===================================================================== export
ExportButton::ExportButton (RacoonProcessor& p) : TextButton ("Exportar"), proc (p)
{
    setTooltip ("Exportar la secuencia con su automatización como archivo MIDI. Clic: guardarlo. Arrástralo: suéltalo en tu DAW o en Racoon."_u);
}

void ExportButton::mouseDown (const MouseEvent& e)
{
    dragged = false;
    TextButton::mouseDown (e);
}

void ExportButton::mouseDrag (const MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 8) return;
    dragged = true;
    const auto f = proc.exportMidiForDrag();
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void ExportButton::clicked()
{
    if (dragged) { dragged = false; return; }
    exportMidiWithChooser (chooser, proc.exportFileName(), [p = &proc] (const File& f) { return p->exportMidi (f); });
}

// ===================================================================== dock
SequencerDock::SequencerDock (RacoonProcessor& p, const StringArray& s)
    : roll (p), keys (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard), proc (p), steps (s)
{
    roll.keysBelow = &keys;
    play.setTooltip ("Tocar / parar la secuencia (sin DAW también)"_u);
    play.onClick = [this] { proc.setSeqPlaying (! proc.isSeqPlaying()); play.playing = proc.isSeqPlaying(); play.repaint(); };
    undo.setTooltip ("Deshacer (perillas y notas). Cmd+Z");
    redo.setTooltip ("Rehacer. Cmd+Mayús+Z"_u);
    undo.onClick = [this] { proc.undoManager.undo(); };
    redo.onClick = [this] { proc.undoManager.redo(); };

    tempo.setSliderStyle (Slider::LinearBar);
    tempo.setRange (40.0, 240.0, 1.0);
    tempo.setTextValueSuffix (" BPM");
    tempo.setValue (proc.patternBpm(), dontSendNotification);
    tempo.setTooltip ("Tempo de la secuencia: arrastra o haz doble clic para escribirlo. Dentro de tu DAW manda el tempo del DAW."_u);
    tempo.onValueChange = [this] { proc.setPatternBpm (tempo.getValue()); };
    tempo.setColour (Slider::textBoxTextColourId, Pro::text);
    tempo.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);

    for (int b : { 1, 2, 4, 8, 16 }) bars.addItem (String (b) + (b == 1 ? " compás"_u : String (" compases")), b);
    bars.setSelectedId (proc.patternBars(), dontSendNotification);
    bars.setTooltip ("Largo de la secuencia");
    bars.onChange = [this] { proc.setPatternBars (bars.getSelectedId()); roll.showFromStart(); };

    gridBox.addItem ("1/4", 1); gridBox.addItem ("1/8", 2); gridBox.addItem ("1/16", 3); gridBox.addItem ("1/32", 4);
    gridBox.setSelectedId (3, dontSendNotification);
    gridBox.setTooltip ("Rejilla: dónde caen las notas que escribes"_u);
    gridBox.onChange = [this] { const double g[] { 1.0, 0.5, 0.25, 0.125 }; roll.grid = g[jlimit (0, 3, gridBox.getSelectedId() - 1)]; roll.repaint(); };
    roll.grid = 0.25;

    example.setButtonText ("Ejemplo");
    example.setTooltip ("Vuelve a poner la secuencia de ejemplo de este plugin"_u);
    example.onClick = [this] { proc.restoreDemoPattern(); bars.setSelectedId (proc.patternBars(), dontSendNotification); tempo.setValue (proc.patternBpm(), dontSendNotification); roll.centreOnNotes(); };
    clear.setButtonText ("Borrar notas");
    clear.setTooltip ("Deja la secuencia vacía (se puede deshacer)"_u);
    clear.onClick = [this] { proc.setPattern ({}); };
    randomButton.setButtonText ("Aleatoria");
    randomButton.setTooltip ("Una secuencia nueva al azar, en tono y a la medida de este plugin. Otra vez: otra distinta. Deshacer la quita."_u);
    randomButton.setColour (TextButton::buttonColourId, Brand::limon);
    randomButton.onClick = [this] { generateRandom(); };
    demoSource.setButtonText ("Sonido de ejemplo");
    demoSource.setClickingTogglesState (true);
    demoSource.setToggleState (proc.demoSourceOn.load(), dontSendNotification);
    demoSource.setTooltip ("Encendido: Play y el teclado tocan un sonido de ejemplo que pasa por el efecto. Apagado: solo suena lo que le llega de tu DAW o tu micrófono."_u);
    demoSource.onClick = [this] { proc.demoSourceOn.store (demoSource.getToggleState()); };

    toSong.makeMidi = [this] { return proc.maquetaMidi(); };
    toSong.animal = [this] { return proc.info.animal; };
    toSong.trackName = [this] { return proc.trackName(); };
    toSong.kind = proc.maquetaKind();
    toSong.afterSend = [this]
    {
        proc.setSeqPlaying (false);                // from now on the song plays it, not the plugin's own Play
        proc.handedToSong();
    };
    toSong.binding = &proc.songClip;
    toSong.onEdit = [this] (const maqueta::EditRequest& r)
    {
        proc.loadFromSong (r);
        bars.setSelectedId (proc.patternBars(), dontSendNotification);
        tempo.setValue (proc.patternBpm(), dontSendNotification);
        play.playing = false;
        play.repaint();
        roll.showFromStart();
        roll.centreOnNotes();
        roll.repaint();
    };
    toSong.setColour (TextButton::buttonColourId, Brand::limon);
    addChildComponent (toSong);
    toSong.setVisible (proc.canPassToMaqueta());

    autoButton.setButtonText ("Automatización"_u);
    autoButton.setTooltip ("Ver y dibujar la automatización. También: clic derecho en cualquier perilla > Ver automatización."_u);
    autoButton.onClick = [this]
    {
        if (auto* o = findParentComponentOfClass<AutomationOpener>())
        {
            auto ids = proc.automation.laneIds();
            o->openAutomation (ids.isEmpty() ? String() : ids[0], false);
        }
    };

    octDown.setTooltip ("Teclado una octava abajo");
    octUp.setTooltip ("Teclado una octava arriba");
    octDown.onClick = [this] { baseOctave = jmax (0, baseOctave - 1); layoutKeys(); };
    octUp.onClick = [this] { baseOctave = jmin (7, baseOctave + 1); layoutKeys(); };

    keys.setKeyPressBaseOctave (5);
    keys.setColour (MidiKeyboardComponent::keyDownOverlayColourId, Brand::menta);
    keys.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, Brand::limon.withAlpha (0.5f));

    for (Component* c : { (Component*) &play, (Component*) &undo, (Component*) &redo, (Component*) &tempo, (Component*) &bars, (Component*) &gridBox,
                          (Component*) &example, (Component*) &clear, (Component*) &randomButton, (Component*) &octDown, (Component*) &octUp, (Component*) &keys,
                          (Component*) &exportButton, (Component*) &autoButton })
        addAndMakeVisible (c);
    addChildComponent (demoSource);
    demoSource.setVisible (proc.hasDemoSource());
    addChildComponent (roll);
    roll.setVisible (proc.dockShowsRoll());
    for (auto* c : { (Component*) &example, (Component*) &clear, (Component*) &randomButton, (Component*) &bars, (Component*) &gridBox }) c->setVisible (proc.dockShowsRoll());
    startTimerHz (10);
}

SequencerDock::~SequencerDock() { proc.keyboardState.allNotesOff (1); }

void SequencerDock::generateRandom (int64 seed)
{
    aleatorio::Opciones o;
    o.rol = aleatorio::rolDe (proc.info.animal);
    o.estilo = aleatorio::estiloDe ({}, proc.patternBpm());
    if (o.rol == aleatorio::Rol::percusion) { o.drums.congaLo = o.drums.congaHi; o.drums.bongoLo = o.drums.bongoHi; }   // Mole: one conga, one bongo
    o.bars = jlimit (1, 16, proc.patternBars());
    o.beatsPerBar = 4;
    o.seed = seed != 0 ? seed : (int64) Time::getHighResolutionTicks();
    // the key of the notes already written; with nothing written, one of the keys dance music uses most
    std::vector<aleatorio::Nota> now;
    for (auto& n : proc.getPattern()) now.push_back ({ n.start, n.length, n.pitch, n.velocity });
    if (! aleatorio::tonalidadDe (now, o.root, o.minor))
    {
        static const int roots[] { 9, 0, 2, 4, 5, 7, 11 };
        Random r (o.seed);
        o.root = roots[r.nextInt (7)];
        o.minor = r.nextInt (100) < 70;
    }
    // what the pattern already has: Aleatoria varies it (same bars, rhythm, register, harmony) instead of replacing it
    o.molde = now;
    std::vector<SeqNote> notes;
    for (auto& n : aleatorio::generar (o))
    {
        SeqNote s;
        s.start = n.start; s.length = n.length; s.pitch = n.pitch; s.velocity = n.velocity;
        notes.push_back (s);
    }
    proc.undoManager.beginNewTransaction ("Aleatoria");      // each click is its own step: Deshacer takes back only the last one
    proc.setPattern (notes);
    proc.undoManager.beginNewTransaction();
    roll.centreOnNotes();
    roll.showFromStart();
    toSong.refreshText();
}

int SequencerDock::preferredHeight() const { return proc.dockShowsRoll() ? 296 + jmin (proc.dockExtra, maxExtraHeight()) : 138; }

int SequencerDock::maxExtraHeight() const
{
    // the page above keeps at least 170 px (it scrolls)
    auto* parent = getParentComponent();
    if (parent == nullptr || ! proc.dockShowsRoll()) return 0;
    return jmax (0, parent->getHeight() - 296 - 170 - 60);
}

void SequencerDock::setExtraHeight (int extra)
{
    extra = jlimit (0, maxExtraHeight(), extra);
    if (extra == proc.dockExtra) return;
    proc.dockExtra = extra;
    if (auto* parent = getParentComponent()) parent->resized();
    roll.centreOnNotes();
}

void SequencerDock::mouseMove (const MouseEvent& e)
{
    setMouseCursor (proc.dockShowsRoll() && e.y < handleH ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
}

void SequencerDock::mouseDown (const MouseEvent& e)
{
    resizing = proc.dockShowsRoll() && e.y < handleH;
    extraAtDown = proc.dockExtra;
    yAtDown = e.getScreenY();
}

void SequencerDock::mouseDrag (const MouseEvent& e)
{
    if (resizing) setExtraHeight (extraAtDown + (yAtDown - e.getScreenY()));
}

void SequencerDock::mouseDoubleClick (const MouseEvent& e)
{
    // double click on the edge: back to the usual size, or as tall as it goes
    if (proc.dockShowsRoll() && e.y < handleH) setExtraHeight (proc.dockExtra > 0 ? 0 : maxExtraHeight());
}

void SequencerDock::timerCallback()
{
    if (play.playing != proc.isSeqPlaying()) { play.playing = proc.isSeqPlaying(); play.repaint(); }
    undo.setEnabled (proc.undoManager.canUndo());
    redo.setEnabled (proc.undoManager.canRedo());
    const bool host = proc.hostDrivesTempo();
    tempo.setEnabled (! host);
    if (! tempo.isMouseButtonDown() && std::abs (tempo.getValue() - proc.patternBpm()) > 0.5) tempo.setValue (proc.patternBpm(), dontSendNotification);
    if (bars.getSelectedId() != proc.patternBars()) bars.setSelectedId (proc.patternBars(), dontSendNotification);
}

void SequencerDock::layoutKeys()
{
    // as many whole octaves as fit, starting at the chosen one
    const int octaves = jlimit (2, 7, keys.getWidth() / 150);
    const int lo = jlimit (0, 120 - 12 * octaves, baseOctave * 12 + 12);
    keys.setAvailableRange (lo, jmin (127, lo + 12 * octaves));
    keys.setKeyWidth ((float) keys.getWidth() / (7.0f * (float) octaves + 1.0f));
    keys.setLowestVisibleKey (lo);
    keys.setKeyPressBaseOctave (jlimit (0, 10, lo / 12));
}

void SequencerDock::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    stepsArea = r.removeFromRight (jmin (330, r.getWidth() / 4));
    r.removeFromRight (10);
    auto top = r.removeFromTop (34);
    play.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (6);
    undo.setBounds (top.removeFromLeft (112));
    top.removeFromLeft (4);
    redo.setBounds (top.removeFromLeft (104));
    top.removeFromLeft (12);
    tempo.setBounds (top.removeFromLeft (100).withTrimmedTop (12));
    tempoCaption = tempo.getBounds().withY (top.getY() - 2).withHeight (13);
    top.removeFromLeft (8);
    if (proc.dockShowsRoll())
    {
        bars.setBounds (top.removeFromLeft (118).withTrimmedTop (12));
        barsCaption = bars.getBounds().withY (top.getY() - 2).withHeight (13);
        top.removeFromLeft (8);
        gridBox.setBounds (top.removeFromLeft (76).withTrimmedTop (12));
        gridCaption = gridBox.getBounds().withY (top.getY() - 2).withHeight (13);
        top.removeFromLeft (10);
        example.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (4);
        clear.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (4);
        randomButton.setBounds (top.removeFromLeft (96));
        top.removeFromLeft (10);
    }
    else barsCaption = gridCaption = {};
    if (proc.canPassToMaqueta()) { toSong.setBounds (top.removeFromLeft (148)); top.removeFromLeft (4); }
    exportButton.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (4);
    autoButton.setBounds (top.removeFromLeft (128));
    top.removeFromLeft (10);
    octUp.setBounds (top.removeFromRight (58));
    top.removeFromRight (4);
    octDown.setBounds (top.removeFromRight (58));
    top.removeFromRight (10);
    if (proc.hasDemoSource()) demoSource.setBounds (top.removeFromRight (jmin (170, top.getWidth())));
    r.removeFromTop (8);
    keys.setBounds (r.removeFromBottom (proc.dockShowsRoll() ? 70 : 74));
    if (proc.dockShowsRoll())
    {
        r.removeFromBottom (6);
        roll.setBounds (r);
    }
    layoutKeys();
}

void SequencerDock::paint (Graphics& g)
{
    g.fillAll (Pro::head);
    g.setColour (Pro::line);
    g.fillRect (getLocalBounds().removeFromTop (1));
    if (proc.dockShowsRoll())
    {
        // the grip: drag it up to give the piano roll more room
        g.setColour (Pro::textFaint);
        const float cx = (float) getWidth() * 0.5f;
        for (int i = 0; i < 2; ++i) g.fillRoundedRectangle (cx - 22.0f, 3.5f + (float) i * 2.5f, 44.0f, 1.2f, 0.6f);
    }
    {
        ProDraw::caps (g, "Tempo", tempoCaption.toFloat(), Justification::centredLeft, Pro::textFaint, 9.5f);
        if (! barsCaption.isEmpty()) ProDraw::caps (g, "Largo", barsCaption.toFloat(), Justification::centredLeft, Pro::textFaint, 9.5f);
        if (! gridCaption.isEmpty()) ProDraw::caps (g, "Rejilla", gridCaption.toFloat(), Justification::centredLeft, Pro::textFaint, 9.5f);
    }
    // Empieza aquí
    ProDraw::card (g, stepsArea.toFloat(), "Empieza aquí"_u, S::accent());
    auto s = stepsArea.reduced (12, 10).withTrimmedTop (22);
    const int n = jmax (1, steps.size());
    const int h = s.getHeight() / n;
    for (int i = 0; i < steps.size(); ++i)
    {
        auto row = s.removeFromTop (h);
        auto num = row.removeFromLeft (22).withSizeKeepingCentre (20, 20).toFloat();
        g.setColour (S::accent());
        g.fillEllipse (num);
        g.setColour (Pro::win);
        g.setFont (NumFont::semi (11.0f));
        g.drawText (::racoon::i18n::tr (String (i + 1)), num.toNearestInt(), Justification::centred);
        row.removeFromLeft (8);
        g.setColour (Pro::textDim);
        g.setFont (Fonts::body (12.5f));
        g.drawFittedText (::racoon::i18n::tr (steps[i]), row, Justification::centredLeft, 4, 0.95f);
    }
}
}

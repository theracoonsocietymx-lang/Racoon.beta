#include "PianoRoll.h"
#include "engine/I18n.h"
#include "engine/Genres.h"
#include "NoteTools.h"
#include "Arrangement.h"
#include <set>
#include <map>

namespace racoon::ui
{
using namespace juce;

namespace
{
const char* noteNames[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
bool isBlack (int p) { const int n = p % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
struct CopiedNote { double start, length; int pitch, velocity; bool mute; };
std::vector<CopiedNote>& noteClipboard() { static std::vector<CopiedNote> v; return v; }

// General MIDI drum map (the notes Hamster answers to), in Spanish
String drumName (int p)
{
    switch (p)
    {
        case 35: case 36: return "Bombo";
        case 37: return "Aro";
        case 38: case 40: return "Caja";
        case 39: return "Palmas";
        case 41: return "Tom grave";
        case 42: return "Charles cerrado";
        case 44: return "Charles pedal";
        case 45: return "Tom medio";
        case 46: return "Charles abierto";
        case 48: case 50: return "Tom agudo";
        case 49: case 57: return "Crash";
        case 51: case 59: return "Ride";
        case 56: return "Cencerro";
        case 60: return "Bongo";
        case 63: return "Conga";
        case 70: return "Shaker";
        case 75: return "Claves";
        case 76: return "Metal";
        default: return {};
    }
}
String pitchName (int p) { return notes::pitchName (p); }

struct ScaleDef { const char* name; std::vector<int> steps; };
const std::vector<ScaleDef>& scales()
{
    static std::vector<ScaleDef> s {
        { "Sin escala", {} },
        { "Mayor", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Menor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dórica", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Frigia", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Mixolidia", { 0, 2, 4, 5, 7, 9, 10 } },
        { "Pentatónica menor", { 0, 3, 5, 7, 10 } },
        { "Pentatónica mayor", { 0, 2, 4, 7, 9 } },
        { "Blues", { 0, 3, 5, 6, 7, 10 } },
    };
    return s;
}
constexpr int keyboardWidth = 100, rulerHeight = 22, velocityHeight = 74, toolbarHeight = 38;
}

// ===================================================================== keyboard
class PianoRoll::Keyboard : public Component, public SettableTooltipClient
{
public:
    explicit Keyboard (PianoRoll& o) : roll (o) { setTooltip ("Clic: escuchar la nota. Arrastra arriba o abajo para cambiar de octava, a los lados para agrandar las teclas. La rueda también sube y baja. "
                                                              "En la batería, «fijar» a la derecha de cada sonido: ALEATORIA ya no mueve su patrón."_u); }
    void paint (Graphics& g) override
    {
        g.fillAll (S::shade);
        for (int row = 0; row < roll.rowCount(); ++row)
        {
            const int p = roll.pitchForRow (row);
            const float y = (float) row * roll.keyHeight - roll.scrollOffsetY();
            if (y > (float) getHeight() || y + roll.keyHeight < 0.0f) continue;
            auto r = Rectangle<float> (0.0f, y, (float) getWidth(), roll.keyHeight);
            g.setColour (isBlack (p) ? S::shade : Colour (0xffd8d1d4));
            g.fillRect (roll.folded ? r : (isBlack (p) ? r.withTrimmedRight ((float) getWidth() * 0.35f) : r));
            g.setColour (S::shade.withAlpha (0.35f));
            g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
            const auto drum = roll.isDrumClip() ? roll.drumRowName (p) : String();
            if (drum.isNotEmpty() && roll.keyHeight >= 11.0f)
            {
                const bool held = roll.isRowHeld (p);
                g.setColour (held ? S::cardDim.interpolatedWith (Brand::limon, 0.18f) : S::cardDim);
                g.fillRect (r.withTrimmedBottom (1.0f));
                g.setColour (S::ink);
                g.setFont (Fonts::body (jmin (11.5f, roll.keyHeight - 2.0f)));
                g.drawFittedText (::racoon::i18n::tr (drum), r.reduced (3.0f, 0.0f).withTrimmedRight ((float) lockWidth).toNearestInt(), Justification::centredLeft, 1, 0.7f);
                // the row's lock: on, ALEATORIA leaves this sound's pattern as it is
                auto lock = Rectangle<float> (r.getRight() - (float) lockWidth, r.getY(), (float) lockWidth, r.getHeight()).reduced (3.0f, jmax (1.5f, (r.getHeight() - 12.0f) * 0.5f));
                g.setColour (held ? Brand::limon : S::inkFaint);
                if (held) g.fillRoundedRectangle (lock, 2.0f);
                else g.drawRoundedRectangle (lock, 2.0f, 1.0f);
                g.setColour (held ? S::shade : S::inkFaint);
                g.setFont (Fonts::label (8.5f));
                g.drawText (::racoon::i18n::tr (held ? "FIJO" : "fijar"), lock, Justification::centred);
            }
            else if (p % 12 == 0 || roll.folded || roll.octaveView || roll.keyHeight >= 16.0f)
            {
                // in the one-octave view every key says its name (Do, Do#, Re...)
                g.setColour (isBlack (p) && ! roll.folded ? S::ink : S::shade);
                g.setFont (Fonts::mono (jlimit (8.0f, 12.0f, roll.keyHeight - 2.0f)));
                // a black key's name sits on the black part, in white
                const auto area = isBlack (p) && ! roll.folded ? r.withTrimmedRight ((float) getWidth() * 0.35f) : r;
                g.drawText (::racoon::i18n::tr (pitchName (p)), area.reduced (4.0f, 0.0f), Justification::centredRight);
            }
        }
        g.setColour (C::stoneHi);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }
    // click a key to hear it, drag up or down to move through the octaves,
    // drag left or right to make the keys bigger or smaller; the wheel also scrolls (Cmd + wheel zooms)
    void mouseDown (const MouseEvent& e) override
    {
        // the lock at the right end of a drum row: hold that sound's pattern (ALEATORIA leaves it alone)
        if (roll.isDrumClip() && e.x >= getWidth() - lockWidth)
        {
            const int p = roll.pitchForRow ((int) ((e.position.y + roll.scrollOffsetY()) / roll.keyHeight));
            if (roll.drumRowName (p).isNotEmpty())
            {
                roll.setRowHeld (p, ! roll.isRowHeld (p));
                moved = true;
                repaint();
                return;
            }
        }
        startScroll = roll.scrollOffsetY();
        startPitch = roll.scrollPitch;
        startFold = roll.foldScroll;
        startKey = roll.keyHeight;
        anchorPitch = roll.pitchForRow ((int) ((e.position.y + roll.scrollOffsetY()) / roll.keyHeight));
        moved = false;
        dragOctaves = 0;
        lastPitch = anchorPitch;
        roll.audition (anchorPitch, 100);
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 4 && ! moved) return;
        moved = true;
        setMouseCursor (MouseCursor::UpDownLeftRightResizeCursor);
        const float dx = (float) e.getDistanceFromDragStartX(), dy = (float) e.getDistanceFromDragStartY();
        if (roll.octaveView)
        {
            // one octave per 40 px: drag down to see higher notes, like pulling the keyboard
            const int steps = (int) std::round (dy / 40.0f);
            if (steps != dragOctaves) { roll.shiftOctave (steps - dragOctaves); dragOctaves = steps; }
            return;
        }
        if (std::abs (dx) > 6.0f)
            roll.keyHeight = jlimit (7.0f, 34.0f, startKey * std::pow (1.02f, dx));
        if (roll.folded) roll.foldScroll = jlimit (0.0f, jmax (0.0f, (float) roll.rowCount() * roll.keyHeight - (float) roll.gridHeight()), startFold - dy);
        else roll.scrollPitch = jlimit ((float) roll.gridHeight() / roll.keyHeight - 1.0f, 127.0f, startPitch + dy / roll.keyHeight);
        roll.updateScrollBars();
        roll.repaint();
    }
    void mouseUp (const MouseEvent&) override { setMouseCursor (MouseCursor::NormalCursor); }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            roll.keyHeight = jlimit (7.0f, 34.0f, roll.keyHeight * (w.deltaY > 0 ? 1.1f : 1.0f / 1.1f));
        else
            roll.scrollRows (-w.deltaY * 24.0f);
        roll.updateScrollBars();
        roll.repaint();
    }
    static constexpr int lockWidth = 30;
    PianoRoll& roll;
    float startScroll = 0.0f, startPitch = 0.0f, startFold = 0.0f, startKey = 14.0f;
    int anchorPitch = 60, lastPitch = 60, dragOctaves = 0;
    bool moved = false;
};

// ===================================================================== note grid
class PianoRoll::NoteGrid : public Component, public SettableTooltipClient
{
public:
    explicit NoteGrid (PianoRoll& o) : roll (o)
    {
        setTooltip (String::fromUTF8 ("Doble clic en un hueco escribe una nota y doble clic en una nota la borra. "
                                      "Clic y arrastrar en un hueco: seleccionar varias (Mayús suma a la selección). Arrastra notas para moverlas; borde derecho: duración. "
                                      "Alt+arrastrar: copiar. Tecla B: lápiz (clic escribe, arrastrar pinta). "
                                      "La línea clara del final del clip se arrastra para alargar la secuencia (por compases; con Cmd, por rejilla)."));
        setWantsKeyboardFocus (false);
    }

    te::MidiNote* noteAt (Point<float> p) const
    {
        if (roll.clip == nullptr) return nullptr;
        auto& notes = roll.clip->getSequence().getNotes();
        for (int i = notes.size(); --i >= 0;)
            if (noteRect (*notes[i]).contains (p)) return notes[i];
        return nullptr;
    }

    Rectangle<float> noteRect (te::MidiNote& n) const
    {
        const int row = roll.rowForPitch (n.getNoteNumber());
        if (row < 0) return {};
        const float x1 = roll.beatToX (n.getStartBeat().inBeats()), x2 = roll.beatToX (n.getEndBeat().inBeats());
        return { x1, (float) row * roll.keyHeight - roll.scrollOffsetY() + 1.0f, jmax (3.0f, x2 - x1), roll.keyHeight - 2.0f };
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff2c2629));
        if (roll.clip == nullptr) return;

        // rows: white keys a touch lighter than black keys, a clear line at every C, nothing else
        for (int row = 0; row < roll.rowCount(); ++row)
        {
            const int p = roll.pitchForRow (row);
            const float y = (float) row * roll.keyHeight - roll.scrollOffsetY();
            if (y > (float) getHeight() || y + roll.keyHeight < 0.0f) continue;
            const Colour white (0xff3b3437), black (0xff2f292c);
            Colour c = isBlack (p) ? black : white;
            if (roll.scaleMode.getSelectedItemIndex() > 0)
                c = roll.inScale (p) ? (p % 12 == roll.scaleRoot.getSelectedItemIndex() ? Colour (0xff4a4246) : white) : Colour (0xff262123);
            g.setColour (c);
            g.fillRect (0.0f, y, (float) getWidth(), roll.keyHeight);
            if (p % 12 == 0 && ! roll.folded)
            {
                g.setColour (C::bone.withAlpha (0.16f));
                g.fillRect (0.0f, y + roll.keyHeight - 1.0f, (float) getWidth(), 1.0f);
            }
            else if (p % 12 == 5 || roll.folded)
            {
                g.setColour (Colours::black.withAlpha (0.18f));
                g.fillRect (0.0f, y + roll.keyHeight - 1.0f, (float) getWidth(), 1.0f);
            }
        }

        // beats
        const double step = roll.gridBeats();
        const int bpb = roll.vs.beatsPerBar();
        const double offset = roll.contentStartEditBeat();
        double first = std::floor (roll.xToBeat (0.0f) / step) * step;
        for (double b = first; ; b += step)
        {
            const float x = roll.beatToX (b);
            if (x > (float) getWidth()) break;
            const double eb = b + offset;
            const bool bar = std::abs (std::fmod (eb, (double) bpb)) < 1e-6;
            const bool beat = std::abs (eb - std::round (eb)) < 1e-6;
            const float stepPx = roll.beatToX (b + step) - x;
            const float alpha = bar ? 0.24f : beat ? 0.10f : (stepPx >= 9.0f ? 0.045f : 0.0f);
            if (alpha <= 0.0f) continue;
            g.setColour (C::bone.withAlpha (alpha));
            g.fillRect (x, 0.0f, 1.0f, (float) getHeight());
        }

        // clip region (outside of it notes do not play)
        const auto range = roll.clipContentRange();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (0.0f, 0.0f, roll.beatToX (range.getStart()), (float) getHeight()));
        g.fillRect (Rectangle<float> (roll.beatToX (range.getEnd()), 0.0f, (float) getWidth(), (float) getHeight()));
        g.setColour (Brand::hielo.withAlpha (0.7f));
        g.fillRect (roll.beatToX (range.getEnd()) - 1.0f, 0.0f, 2.0f, (float) getHeight());

        // notes
        const auto col = roll.trackColour();
        for (auto* n : roll.clip->getSequence().getNotes())
        {
            auto r = noteRect (*n);
            if (r.isEmpty() || r.getRight() < 0 || r.getX() > (float) getWidth()) continue;
            const bool sel = roll.selection.contains (n);
            const Colour fill = n->isMute() ? C::ash : (sel ? Brand::limon : col);
            g.setColour (fill.withMultipliedAlpha (n->isMute() ? 0.6f : 0.55f + 0.45f * (float) n->getVelocity() / 127.0f));
            g.fillRect (r);
            g.setColour (S::ink);
            g.drawRect (r, sel ? 2.0f : 1.0f);
            if (r.getWidth() > 28.0f && roll.keyHeight >= 12.0f)
            {
                g.setFont (Fonts::mono (jmin (10.5f, roll.keyHeight - 3.0f)));
                g.drawText (::racoon::i18n::tr (pitchName (n->getNoteNumber())), r.reduced (3.0f, 0.0f), Justification::centredLeft, false);
            }
        }

        // in the one-octave view, what is written above or below shows as ticks on the top or bottom edge
        if (roll.octaveView)
            for (auto* n : roll.clip->getSequence().getNotes())
            {
                const int p = n->getNoteNumber();
                if (p >= roll.octaveBase && p < roll.octaveBase + 12 * roll.viewOctaves) continue;
                const float x1 = roll.beatToX (n->getStartBeat().inBeats()), x2 = roll.beatToX (n->getEndBeat().inBeats());
                if (x2 < 0.0f || x1 > (float) getWidth()) continue;
                g.setColour (col.withAlpha (0.8f));
                g.fillRect (Rectangle<float> (x1, p > roll.octaveBase ? 0.0f : (float) getHeight() - 4.0f, jmax (3.0f, x2 - x1), 4.0f));
            }

        // playhead
        auto& tr = roll.vs.session.getTransport();
        const double ph = roll.clip->getContentBeatAtTime (tr.getPosition()).inBeats();
        if (roll.clip->getEditTimeRange().contains (tr.getPosition()))
        {
            g.setColour (Brand::hielo);
            g.fillRect (roll.beatToX (ph) - 0.5f, 0.0f, 2.0f, (float) getHeight());
        }

        if (! band.isEmpty())
        {
            g.setColour (Brand::limon.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (Brand::limon);
            g.drawRect (band, 1.0f);
        }

        if (roll.clip->getSequence().getNumNotes() == 0)
        {
            g.setColour (C::boneDim);
            g.setFont (Fonts::body (15.0f));
            g.drawText (::racoon::i18n::tr (String::fromUTF8 ("Doble clic para escribir notas (B: lápiz). Pulsa espacio para escuchar.")), getLocalBounds().reduced (20), Justification::centred);
        }
    }

    bool nearClipEnd (Point<float> p) const
    {
        if (roll.clip == nullptr) return false;
        return std::abs (p.x - roll.beatToX (roll.clipContentRange().getEnd())) < 6.0f;
    }

    void mouseMove (const MouseEvent& e) override
    {
        if (nearClipEnd (e.position) && noteAt (e.position) == nullptr) { setMouseCursor (MouseCursor::LeftRightResizeCursor); return; }
        if (auto* n = noteAt (e.position))
        {
            auto r = noteRect (*n);
            setMouseCursor (e.position.x > r.getRight() - 6.0f ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
        }
        else setMouseCursor (MouseCursor::CrosshairCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (roll.clip == nullptr) return;
        roll.grabKeyboardFocus();
        auto* um = &roll.vs.edit().getUndoManager();
        um->beginNewTransaction();
        copied = false;
        band = {};

        auto* n = noteAt (e.position);
        if (e.mods.isPopupMenu())
        {
            roll.showNoteMenu (n);
            return;
        }
        // the light line at the end of the clip: drag it to make the sequence longer or shorter
        if (n == nullptr && nearClipEnd (e.position)) { mode = Mode::clipEnd; return; }

        if (n == nullptr && ! roll.drawMode)
        {
            // a click in a hole starts a rubber band (Shift keeps what was selected); a double click writes
            bandBase.clearQuick();
            if (e.mods.isShiftDown()) bandBase.addArray (roll.selection);
            else roll.selection.clearQuick();
            mode = Mode::band;
            bandStart = e.position;
            roll.repaint();
            return;
        }
        if (n != nullptr && roll.drawMode && ! e.mods.isShiftDown())
        {
            // the pencil on a note erases it (Draw Mode)
            roll.selection.removeFirstMatchingValue (n);
            roll.clip->getSequence().removeNote (*n, um);
            mode = Mode::none;
            roll.repaint();
            return;
        }
        if (n == nullptr)
        {
            // pencil: the click writes, and dragging along the row paints one note per grid step
            n = writeNoteAt (e.position, um);
            if (n == nullptr) return;
            mode = Mode::paint;
            paintPitch = n->getNoteNumber();
            lastPaintBeat = n->getStartBeat().inBeats();
            roll.repaint();
            return;
        }
        else
        {
            if (e.mods.isShiftDown()) { if (roll.selection.contains (n)) roll.selection.removeFirstMatchingValue (n); else roll.selection.add (n); }
            else if (! roll.selection.contains (n)) { roll.selection.clearQuick(); roll.selection.add (n); }
            auto r = noteRect (*n);
            mode = e.position.x > r.getRight() - 6.0f ? Mode::resize : Mode::move;
            roll.audition (n->getNoteNumber(), n->getVelocity());
        }

        anchor = n;
        originals.clear();
        for (auto* s : roll.selection)
            originals.add ({ s->getStartBeat().inBeats(), s->getLengthBeats().inBeats(), s->getNoteNumber() });
        roll.repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (roll.clip == nullptr) return;
        auto* um = &roll.vs.edit().getUndoManager();
        if (mode == Mode::band)
        {
            band = Rectangle<float> (bandStart, e.position);
            roll.selection.clearQuick();
            roll.selection.addArray (bandBase);
            for (auto* n : roll.clip->getSequence().getNotes())
                if (noteRect (*n).intersects (band)) roll.selection.add (n);
            roll.repaint();
            return;
        }
        if (mode == Mode::paint)
        {
            const double beat = jmax (0.0, roll.snapFloor (roll.xToBeat (e.position.x)));
            if (std::abs (beat - lastPaintBeat) > 1.0e-6)
            {
                lastPaintBeat = beat;
                bool taken = false;
                for (auto* x : roll.clip->getSequence().getNotes())
                    if (x->getNoteNumber() == paintPitch && std::abs (x->getStartBeat().inBeats() - beat) < 1.0e-6) taken = true;
                if (! taken)
                    roll.selection.add (roll.clip->getSequence().addNote (paintPitch, te::BeatPosition::fromBeats (beat),
                                                                          te::BeatDuration::fromBeats (jmin (roll.noteLength, roll.gridBeats())),
                                                                          roll.newVelocity, 0, um));
                roll.repaint();
            }
            return;
        }
        if (mode == Mode::clipEnd)
        {
            const double beat = roll.xToBeat (e.position.x);
            const double step = e.mods.isCommandDown() || roll.gridFree() ? roll.gridBeats() : (double) roll.vs.beatsPerBar();
            const double start = roll.clipContentRange().getStart();
            const double end = jmax (start + step, start + std::round ((beat - start) / step) * step);
            roll.setClipEnd (end, false);     // one undo step for the whole drag (mouseDown opened it)
            return;
        }
        if (anchor == nullptr || ! roll.selection.contains (anchor)) return;
        const int ai = roll.selection.indexOf (anchor);
        const bool free = e.mods.isCommandDown();

        if (mode == Mode::move)
        {
            if (e.mods.isAltDown() && ! copied)
            {
                for (int i = 0; i < roll.selection.size(); ++i)
                    roll.clip->getSequence().addNote (originals[i].pitch, te::BeatPosition::fromBeats (originals[i].start),
                                                      te::BeatDuration::fromBeats (originals[i].length), roll.selection[i]->getVelocity(), 0, um);
                copied = true;
            }
            double delta = (double) e.getDistanceFromDragStartX() / roll.pxPerBeat;
            const double target = free ? originals[ai].start + delta : roll.snapRound (originals[ai].start + delta);
            delta = target - originals[ai].start;
            const int startRow = roll.rowForPitch (originals[ai].pitch);
            const int nowRow = jlimit (0, roll.rowCount() - 1, (int) ((e.position.y + roll.scrollOffsetY()) / roll.keyHeight));
            const int dp = roll.pitchForRow (nowRow) - roll.pitchForRow (startRow);
            for (int i = 0; i < roll.selection.size(); ++i)
            {
                auto* s = roll.selection[i];
                s->setStartAndLength (te::BeatPosition::fromBeats (jmax (0.0, originals[i].start + delta)), te::BeatDuration::fromBeats (originals[i].length), um);
                s->setNoteNumber (jlimit (0, 127, originals[i].pitch + dp), um);
            }
            if (dp != lastAuditionOffset) { lastAuditionOffset = dp; roll.audition (anchor->getNoteNumber(), anchor->getVelocity()); }
        }
        else if (mode == Mode::resize)
        {
            const double delta = (double) e.getDistanceFromDragStartX() / roll.pxPerBeat;
            const double end = originals[ai].start + originals[ai].length + delta;
            const double snappedEnd = free ? end : roll.snapRound (end);
            const double dl = snappedEnd - (originals[ai].start + originals[ai].length);
            for (int i = 0; i < roll.selection.size(); ++i)
            {
                const double len = jmax (free ? 1.0 / 64.0 : roll.gridBeats(), originals[i].length + dl);
                roll.selection[i]->setStartAndLength (te::BeatPosition::fromBeats (originals[i].start), te::BeatDuration::fromBeats (len), um);
            }
            roll.noteLength = jmax (roll.gridBeats(), originals[ai].length + dl);
        }
        roll.repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        mode = Mode::none;
        band = {};
        anchor = nullptr;
        lastAuditionOffset = 0;
        repaint();
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (roll.clip == nullptr || e.mods.isPopupMenu() || roll.drawMode) return;
        auto* um = &roll.vs.edit().getUndoManager();
        if (auto* n = noteAt (e.position))
        {
            roll.selection.removeFirstMatchingValue (n);
            roll.clip->getSequence().removeNote (*n, um);
        }
        else if (! nearClipEnd (e.position))
        {
            // a double click in a hole writes a note there, one grid step long (or the last length used)
            um->beginNewTransaction();
            if (auto* n = writeNoteAt (e.position, um)) { roll.selection.clearQuick(); roll.selection.add (n); }
        }
        mode = Mode::none;
        band = {};
        roll.repaint();
    }

    // a note at the cell under p (pitch of the row, start on the grid), selected and heard
    te::MidiNote* writeNoteAt (Point<float> p, UndoManager* um)
    {
        const int row = (int) ((p.y + roll.scrollOffsetY()) / roll.keyHeight);
        if (row < 0 || row >= roll.rowCount()) return nullptr;
        const int pitch = roll.pitchForRow (row);
        const double beat = jmax (0.0, roll.snapFloor (roll.xToBeat (p.x)));
        auto* n = roll.clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (roll.noteLength),
                                                    roll.newVelocity, 0, um);
        roll.selection.clearQuick();
        roll.selection.add (n);
        roll.audition (pitch, roll.newVelocity);
        return n;
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double anchorBeat = roll.xToBeat (e.position.x);
            roll.pxPerBeat = jlimit (8.0, 600.0, roll.pxPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            roll.scrollBeat = jmax (0.0, anchorBeat - (double) e.position.x / roll.pxPerBeat);
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            roll.scrollBeat = jmax (0.0, roll.scrollBeat - (std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY) * 200.0 / roll.pxPerBeat);
        else
            roll.scrollRows (-w.deltaY * 24.0f);
        roll.updateScrollBars();
        roll.repaint();
    }

    enum class Mode { none, move, resize, band, clipEnd, paint } mode = Mode::none;
    Array<te::MidiNote*> bandBase;     // what was selected before a Shift rubber band
    int paintPitch = 0;
    double lastPaintBeat = -1.0;
    struct Orig { double start, length; int pitch; };
    Array<Orig> originals;
    te::MidiNote* anchor = nullptr;
    bool copied = false;
    int lastAuditionOffset = 0;
    Point<float> bandStart;
    Rectangle<float> band;
    PianoRoll& roll;
};

// ===================================================================== velocity lane
// The lane under the notes: velocity, or one MIDI controller drawn as a line (mod wheel, expression, sustain,
// filter, pitch bend). Click the name at the top left to choose. Velocity: drag the bars. Controllers: drag to draw
// the curve, right click (or Alt) and drag to erase.
class PianoRoll::VelocityLane : public Component, public SettableTooltipClient
{
public:
    struct Mode { int type; const char* name; const char* label; };
    static const std::vector<Mode>& modes()
    {
        static const std::vector<Mode> m {
            { -1, "Velocidad", "VEL" },
            { 1, "Modulación (CC 1)", "MOD" },
            { 11, "Expresión (CC 11)", "EXPR" },
            { 74, "Filtro (CC 74)", "FILTRO" },
            { 64, "Sustain (CC 64)", "SUSTAIN" },
            { 7, "Volumen (CC 7)", "VOL" },
            { te::MidiControllerEvent::pitchWheelType, "Pitch bend", "BEND" },
        };
        return m;
    }
    explicit VelocityLane (PianoRoll& o) : roll (o) { updateTooltip(); }
    int mode = 0;
    int type() const { return modes()[(size_t) mode].type; }
    bool isBend() const { return type() == te::MidiControllerEvent::pitchWheelType; }
    int maxValue() const { return isBend() ? 16383 : 127 << 7; }
    int restValue() const { return isBend() ? 8192 : 0; }
    juce::Rectangle<int> labelArea() const { return { 2, 1, 76, 16 }; }

    void updateTooltip()
    {
        setTooltip (mode == 0 ? String::fromUTF8 ("Velocidad: arrastra las barras arriba o abajo (afecta a las notas seleccionadas). Clic en VEL para dibujar un controlador.")
                              : String::fromUTF8 ("Dibuja la curva arrastrando. Clic derecho y arrastra para borrar. Clic en el nombre para cambiar de controlador."));
    }

    float yFor (int value) const { return ((float) getHeight() - 4.0f) - ((float) getHeight() - 22.0f) * (float) value / (float) maxValue(); }
    int valueFor (float y) const { return jlimit (0, maxValue(), (int) std::round ((float) maxValue() * (((float) getHeight() - 4.0f) - y) / ((float) getHeight() - 22.0f))); }

    void paint (Graphics& g) override
    {
        g.fillAll (C::basalt);
        g.setColour (C::ridge);
        g.fillRect (0, 0, getWidth(), 1);
        if (roll.clip != nullptr)
        {
            const auto col = roll.trackColour();
            if (mode == 0)
            {
                for (auto* n : roll.clip->getSequence().getNotes())
                {
                    const float x = roll.beatToX (n->getStartBeat().inBeats());
                    if (x < -4.0f || x > (float) getWidth()) continue;
                    const float h = ((float) getHeight() - 8.0f) * (float) n->getVelocity() / 127.0f;
                    const bool sel = roll.selection.contains (n);
                    g.setColour (sel ? Brand::limon : col);
                    g.fillRect (x, (float) getHeight() - h, 4.0f, h);
                    g.fillRect (x - 2.0f, (float) getHeight() - h - 2.0f, 8.0f, 4.0f);
                }
            }
            else
            {
                // the rest value (bend centre, controller at 0) as a faint line
                g.setColour (C::ridge);
                g.fillRect (0.0f, yFor (restValue()), (float) getWidth(), 1.0f);
                // the controller as steps: each event holds until the next one
                Path p;
                float lastY = yFor (restValue());
                bool started = false;
                for (auto* e : roll.clip->getSequence().getControllerEvents())
                {
                    if (e->getType() != type()) continue;
                    const float x = roll.beatToX (e->getBeatPosition().inBeats()), y = yFor (e->getControllerValue());
                    if (! started) { p.startNewSubPath (jmin (x, 0.0f), lastY); started = true; }
                    p.lineTo (x, lastY);
                    p.lineTo (x, y);
                    lastY = y;
                    if (x >= -3.0f && x <= (float) getWidth() + 3.0f)
                    {
                        g.setColour (col);
                        g.fillRect (x - 1.5f, y - 1.5f, 3.0f, 3.0f);
                    }
                }
                if (started)
                {
                    p.lineTo ((float) getWidth(), lastY);
                    g.setColour (col);
                    g.strokePath (p, PathStrokeType (1.6f));
                }
            }
        }
        // the name: click to choose what this lane shows
        auto la = labelArea().toFloat();
        g.setColour (C::stone);
        g.fillRect (la);
        g.setColour (mode == 0 ? C::boneDim : Brand::limon);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (String (modes()[(size_t) mode].label) + String::fromUTF8 (" ▾")), la.reduced (4.0f, 0.0f), Justification::centredLeft);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (labelArea().contains (e.getPosition()))
        {
            PopupMenu m;
            for (int k = 0; k < (int) modes().size(); ++k) m.addItem (k + 1, String::fromUTF8 (modes()[(size_t) k].name), true, k == mode);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe = Component::SafePointer<VelocityLane> (this)] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                safe->mode = r - 1;
                safe->updateTooltip();
                safe->repaint();
            });
            return;
        }
        roll.vs.edit().getUndoManager().beginNewTransaction();
        lastBeat = -1.0;
        mouseDrag (e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (roll.clip == nullptr || labelArea().contains (e.getMouseDownPosition())) return;
        auto* um = &roll.vs.edit().getUndoManager();
        if (mode == 0)
        {
            const int v = jlimit (1, 127, (int) std::round (127.0f * ((float) getHeight() - e.position.y) / ((float) getHeight() - 8.0f)));
            te::MidiNote* nearest = nullptr;
            float best = 8.0f;
            for (auto* n : roll.clip->getSequence().getNotes())
            {
                const float d = std::abs (roll.beatToX (n->getStartBeat().inBeats()) + 2.0f - e.position.x);
                if (d < best) { best = d; nearest = n; }
            }
            if (nearest == nullptr) return;
            if (roll.selection.contains (nearest))
                for (auto* n : roll.selection) n->setVelocity (v, um);
            else
                nearest->setVelocity (v, um);
            repaint();
            roll.grid->repaint();
            return;
        }
        // controllers: one point per step (a 1/32 or the grid, the finer of the two), from the last position to here
        const bool erase = e.mods.isPopupMenu() || e.mods.isAltDown();
        const double step = jmin (0.125, jmax (1.0 / 64.0, roll.gridBeats() * 0.5));
        const auto range = roll.clipContentRange();
        const double beat = jlimit (range.getStart(), jmax (range.getStart(), range.getEnd() - step), std::floor (roll.xToBeat (e.position.x) / step) * step);
        const int value = valueFor (e.position.y);
        const double from = lastBeat < 0.0 ? beat : lastBeat;
        const int fromValue = lastBeat < 0.0 ? value : lastValue;
        const double lo = jmin (from, beat), hi = jmax (from, beat);
        auto& seq = roll.clip->getSequence();
        // what was there in this stretch goes away
        Array<te::MidiControllerEvent*> gone;
        for (auto* ev : seq.getControllerEvents())
            if (ev->getType() == type() && ev->getBeatPosition().inBeats() >= lo - 1.0e-6 && ev->getBeatPosition().inBeats() < hi + step - 1.0e-6)
                gone.add (ev);
        for (auto* ev : gone) seq.removeControllerEvent (*ev, um);
        if (! erase)
        {
            const int steps = (int) std::round ((hi - lo) / step);
            for (int k = 0; k <= steps; ++k)
            {
                const double b = lo + k * step;
                const double t = hi > lo ? (b - lo) / (hi - lo) : 1.0;
                // the value runs from where the mouse was to where it is now, in the direction it moved
                const double a = from <= beat ? t : 1.0 - t;
                const int v = (int) std::round (fromValue + (value - fromValue) * a);
                seq.addControllerEvent (te::BeatPosition::fromBeats (b), type(), isBend() ? v : (v >> 7) << 7, um);
            }
        }
        lastBeat = beat;
        lastValue = value;
        repaint();
    }

    PianoRoll& roll;
    double lastBeat = -1.0;
    int lastValue = 0;
};

void PianoRoll::showLane (int controllerType)
{
    const auto& m = VelocityLane::modes();
    for (int k = 0; k < (int) m.size(); ++k)
        if (m[(size_t) k].type == controllerType) { velocity->mode = k; velocity->updateTooltip(); velocity->repaint(); }
}

Component* PianoRoll::laneComponent() const { return velocity.get(); }

void PianoRoll::setDrawMode (bool on)
{
    drawMode = on;
    if (vs.showHint) vs.showHint (on ? String::fromUTF8 ("Lápiz: clic escribe una nota, arrastrar pinta, clic en una nota la borra. B para volver.")
                                     : String::fromUTF8 ("Selección: clic y arrastrar selecciona, doble clic escribe o borra una nota."));
    if (grid != nullptr) grid->repaint();
}

// ===================================================================== piano roll
PianoRoll::PianoRoll (ViewState& v) : vs (v)
{
    keyboard = std::make_unique<Keyboard> (*this);
    grid = std::make_unique<NoteGrid> (*this);
    velocity = std::make_unique<VelocityLane> (*this);
    addAndMakeVisible (*keyboard);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*velocity);

    title.setFont (Fonts::bodyBold (15.0f));
    title.setColour (Label::textColourId, S::ink);
    addAndMakeVisible (title);

    gridChoice.addItemList ({ "4 compases", "2 compases", "1 compás"_u, "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
                              "1/4 T", "1/8 T", "1/16 T", "1/32 T", "Libre" }, 1);
    gridChoice.setSelectedItemIndex (6, dontSendNotification);
    gridChoice.setTooltip (String::fromUTF8 ("Rejilla: dónde caen y cuánto duran las notas nuevas. 1/32 sirve para dobles golpes; T son tresillos; Libre no ajusta a nada."));
    gridChoice.onChange = [this]
    {
        if (! gridFree()) noteLength = gridBeats();
        if (vs.showHint) vs.showHint ("Rejilla "_u + gridChoice.getText() + (gridFree() ? String (": las notas caen donde hagas clic.") : String (": las notas nuevas duran eso y caen ahí."_u)));
        repaint();
    };
    addAndMakeVisible (gridChoice);
    finerButton.setTooltip (String::fromUTF8 ("Rejilla más fina: notas más cortas y juntas, para dobles golpes (Ctrl/Cmd + 1)"));
    coarserButton.setTooltip (String::fromUTF8 ("Rejilla más gruesa (Ctrl/Cmd + 2)"));
    finerButton.onClick = [this] { stepGrid (-1); };
    coarserButton.onClick = [this] { stepGrid (1); };
    splitButton.setTooltip (String::fromUTF8 ("Divide las notas seleccionadas en dos golpes iguales (doble golpe). Clic derecho en una nota: en 3 o 4."));
    splitButton.onClick = [this] { splitSelected (2); };
    for (auto* b : { &finerButton, &coarserButton, &splitButton }) addAndMakeVisible (b);

    for (auto* n : noteNames) scaleRoot.addItem (n, scaleRoot.getNumItems() + 1);
    scaleRoot.setSelectedItemIndex (9, dontSendNotification); // A
    scaleRoot.setTooltip ("Nota base de la escala");
    for (auto& s : scales()) scaleMode.addItem (String::fromUTF8 (s.name), scaleMode.getNumItems() + 1);
    scaleMode.setSelectedItemIndex (0, dontSendNotification);
    scaleMode.setTooltip (String::fromUTF8 ("Ayuda de escala: ilumina las filas que pertenecen a la escala"));
    scaleRoot.onChange = scaleMode.onChange = [this] { if (folded) refreshFold(); repaint(); };
    addAndMakeVisible (scaleRoot);
    addAndMakeVisible (scaleMode);

    velocitySlider.setSliderStyle (Slider::LinearHorizontal);
    velocitySlider.setRange (1, 127, 1);
    velocitySlider.setValue (newVelocity, dontSendNotification);
    velocitySlider.setTextBoxStyle (Slider::TextBoxRight, false, 34, 20);
    velocitySlider.setTooltip ("Velocidad de las notas nuevas");
    velocitySlider.onValueChange = [this] { newVelocity = (int) velocitySlider.getValue(); };
    addAndMakeVisible (velocitySlider);

    quantiseButton.setTooltip (String::fromUTF8 ("Cuantizar: lleva las notas seleccionadas (o todas) a la rejilla (Q)"));
    quantiseButton.onClick = [this] { quantiseSelected(); };
    doubleButton.setTooltip (String::fromUTF8 ("Duplicar: copia las notas seleccionadas justo detrás (Cmd+D)"));
    doubleButton.onClick = [this] { duplicateSelected(); };
    foldButton.setClickingTogglesState (true);
    foldButton.setTooltip (String::fromUTF8 ("Plegar: muestra solo las notas que usas (o las de la escala)"));
    foldButton.onClick = [this] { setViewMode (false, foldButton.getToggleState()); };
    quantiseOptions.setTooltip (String::fromUTF8 ("Opciones de cuantizar: fuerza, swing, finales y un toque humano"));
    quantiseOptions.onClick = [this] { showQuantizeOptions(); };
    addAndMakeVisible (quantiseButton);
    addAndMakeVisible (quantiseOptions);
    addAndMakeVisible (doubleButton);
    randomButton.setTooltip (String::fromUTF8 ("Secuencia aleatoria en la tonalidad de la canción: batería, bajo, acordes o melodía según el instrumento. "
                                               "Otra vez: otra distinta. Cmd+Z la quita. Clic derecho: elegir qué genera."));
    randomButton.setColour (TextButton::buttonColourId, Brand::limon);
    randomButton.onClick = [this] { generateRandom(); };
    randomButton.onRightClick = [this] { randomMenu().showMenuAsync (PopupMenu::Options().withTargetComponent (randomButton)); };
    addAndMakeVisible (randomButton);
    holdButton.setClickingTogglesState (true);
    holdButton.setTooltip (String::fromUTF8 ("Fijar este clip: ALEATORIA (aquí o en todos los canales) ya no lo cambia. Para cuando un patrón ya te gustó."));
    holdButton.onClick = [this]
    {
        if (clip == nullptr) { holdButton.setToggleState (false, dontSendNotification); return; }
        setHeld (*clip, holdButton.getToggleState());
        vs.sendChangeMessage();
        if (vs.showHint) vs.showHint (holdButton.getToggleState() ? String::fromUTF8 ("Clip fijado: ALEATORIA no lo va a cambiar.")
                                                                  : String::fromUTF8 ("Clip suelto: ALEATORIA puede variarlo otra vez."));
    };
    addAndMakeVisible (holdButton);
    addAndMakeVisible (foldButton);
    octaveButton.setClickingTogglesState (true);
    octaveButton.setTooltip (String::fromUTF8 ("Una octava a la vez, con teclas grandes y sus nombres (Do, Re, Mi...). Apagado: todo el teclado."));
    octaveButton.onClick = [this] { preferOctave = octaveButton.getToggleState(); setViewMode (preferOctave, false); };
    octUp.setTooltip (String::fromUTF8 ("Una octava arriba (Re Pág). El número dice cuántas notas hay escritas más arriba."));
    octDown.setTooltip (String::fromUTF8 ("Una octava abajo (Av Pág). El número dice cuántas notas hay escritas más abajo."));
    octUp.onClick = [this] { shiftOctave (1); };
    octDown.onClick = [this] { shiftOctave (-1); };
    for (auto* b : { &octaveButton, &octUp, &octDown }) addAndMakeVisible (b);

    addAndMakeVisible (vScroll);
    addAndMakeVisible (hScroll);
    struct SL : ScrollBar::Listener
    {
        PianoRoll& r;
        explicit SL (PianoRoll& o) : r (o) {}
        void scrollBarMoved (ScrollBar* sb, double start) override
        {
            if (sb == &r.vScroll) r.scrollPitch = 127.0f - (float) start;
            else r.scrollBeat = start;
            r.repaint();
        }
    };
    scrollListener = std::make_unique<SL> (*this);
    vScroll.addListener (scrollListener.get());
    hScroll.addListener (scrollListener.get());
    setWantsKeyboardFocus (true);
}

PianoRoll::~PianoRoll() { watched.removeListener (this); }

void PianoRoll::setClip (te::MidiClip* c)
{
    watched.removeListener (this);
    clip = c;
    selection.clearQuick();
    if (clip != nullptr)
    {
        watched = clip->state;
        watched.addListener (this);
        title.setText (clip->getName() + "  ·  "_u + (clip->getTrack() != nullptr ? clip->getTrack()->getName() : String()), dontSendNotification);
        holdButton.setToggleState (isHeld (*clip), dontSendNotification);
        scrollBeat = jmax (0.0, clipContentRange().getStart() - 0.5);
        if (vs.session.snapBeats > 0.0)
        {
            const double g = vs.session.snapBeats;
            const double bar = vs.beatsPerBar();
            gridChoice.setSelectedItemIndex (g >= 4.0 * bar ? 0 : g >= 2.0 * bar ? 1 : g >= bar ? 2 : g >= 2.0 ? 3 : g >= 1.0 ? 4
                                             : g >= 0.5 ? 5 : g >= 0.25 ? 6 : g >= 0.125 ? 7 : 8, dontSendNotification);
        }
        if (! gridFree()) noteLength = gridBeats();
        // drum clips open folded to the kit's own rows, with their names (Bombo, Caja...); melodic clips open on
        // the octave where most of their notes are, with the note names (Do, Re, Mi...)
        const bool drums = isDrumClip();
        readDrumRows();
        folded = drums;
        octaveView = ! drums && preferOctave;
        foldButton.setToggleState (folded, dontSendNotification);
        octaveButton.setToggleState (octaveView, dontSendNotification);
        keyHeight = drums ? 20.0f : 14.0f;
        if (folded) refreshFold();
        if (octaveView) chooseOctave();
        refreshOctaveButtons();
        resized();
        centreOnNotes();
        needsCentre = grid == nullptr || grid->getHeight() <= 60;
    }
    else title.setText ({}, dontSendNotification);
    updateScrollBars();
    repaint();
}

void PianoRoll::handleAsyncUpdate()
{
    if (clip == nullptr) return;
    // the clip was deleted (or undone away): close it
    if (! clip->state.getParent().isValid()) { setClip (nullptr); return; }
    auto& notes = clip->getSequence().getNotes();
    for (int i = selection.size(); --i >= 0;)
        if (! notes.contains (selection[i])) selection.remove (i);
    if (folded) refreshFold();
    refreshOctaveButtons();
    repaint();
}

double PianoRoll::gridBeats() const
{
    const double bar = vs.beatsPerBar();
    const double g[] { 4.0 * bar, 2.0 * bar, bar, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0, 1.0 / 64.0 };
    return g[jlimit (0, 13, gridChoice.getSelectedItemIndex())];
}

bool PianoRoll::gridFree() const { return gridChoice.getSelectedItemIndex() == 13; }

void PianoRoll::setGridIndex (int i)
{
    gridChoice.setSelectedItemIndex (jlimit (0, 13, i), sendNotificationSync);
}

void PianoRoll::stepGrid (int dir)
{
    // straight values and triplets each move within their own family
    const int i = gridChoice.getSelectedItemIndex();
    if (i == 13) { setGridIndex (dir < 0 ? 8 : 6); return; }
    if (i <= 8) setGridIndex (jlimit (0, 8, i + (dir < 0 ? 1 : -1)));
    else        setGridIndex (jlimit (9, 12, i + (dir < 0 ? 1 : -1)));
}

void PianoRoll::splitSelected (int parts)
{
    if (clip == nullptr || selection.isEmpty() || parts < 2) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    Array<te::MidiNote*> added;
    for (auto* n : Array<te::MidiNote*> (selection))
    {
        const double start = n->getStartBeat().inBeats(), len = n->getLengthBeats().inBeats() / parts;
        const int pitch = n->getNoteNumber(), vel = n->getVelocity();
        clip->getSequence().removeNote (*n, um);
        for (int k = 0; k < parts; ++k)
            added.add (clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (start + k * len), te::BeatDuration::fromBeats (len), vel, 0, um));
    }
    selection = added;
    if (vs.showHint) vs.showHint (parts == 2 ? String::fromUTF8 ("Doble golpe: cada nota quedó partida en dos.") : "Cada nota quedó partida en "_u + String (parts) + ".");
    repaint();
}

void PianoRoll::showNoteMenu (te::MidiNote* n)
{
    if (n != nullptr && ! selection.contains (n)) { selection.clearQuick(); selection.add (n); repaint(); }
    PopupMenu m;
    auto item = [&m] (const String& text, const String& keys, bool enabled, std::function<void()> f)
    {
        PopupMenu::Item it (text);
        it.shortcutKeyDescription = keys;
        it.isEnabled = enabled;
        it.action = std::move (f);
        m.addItem (std::move (it));
    };
    const bool sel = ! selection.isEmpty();
    const bool any = clip != nullptr && clip->getSequence().getNumNotes() > 0;
    m.addSectionHeader (sel ? "Notas seleccionadas (" + String (selection.size()) + ")" : String ("Todas las notas del clip"));
    item ("Subir una octava", String::fromUTF8 ("Mayús+↑"), any, [this] { moveOctave (1); });
    item ("Bajar una octava", String::fromUTF8 ("Mayús+↓"), any, [this] { moveOctave (-1); });
    item ("Subir un semitono", String::fromUTF8 ("↑"), sel, [this] { transposeSelected (1); });
    item ("Bajar un semitono", String::fromUTF8 ("↓"), sel, [this] { transposeSelected (-1); });
    m.addSeparator();
    item ("Reverso (al revés en el tiempo)"_u, "R", any, [this] { reverseNotes(); });
    item ("Invertir (agudas abajo, graves arriba)", {}, any, [this] { invertNotes(); });
    item ("Legato (cada nota hasta la siguiente)", {}, any, [this] { legatoNotes(); });
    item ("Cuantizar", "Cmd+U", any, [this] { quantiseSelected(); });
    item ("Opciones de cuantizar (fuerza, swing)...", {}, any, [this] { showQuantizeOptions(); });
    item ("Quitar solapes", {}, any, [this] { removeOverlapNotes(); });
    item ("Detectar la tonalidad", {}, any, [this] { if (clip != nullptr) showKeyOf (vs, *clip); });
    item ("Silenciar / activar", "0", any, [this] { toggleMuteNotes(); });
    {
        PopupMenu v;
        const StringArray names { "Crescendo (de suave a fuerte)", "Decrescendo (de fuerte a suave)", "Humanizar poco (+-8)",
                                  "Humanizar mucho (+-20)", String::fromUTF8 ("Más fuerte (+15 %)"), String::fromUTF8 ("Más suave (-15 %)"),
                                  "Emparejar (acercar al promedio)", "Todas a la velocidad de VEL" };
        for (int i = 0; i < names.size(); ++i) v.addItem (names[i], any, false, [this, i] { velocityTool (i); });
        m.addSubMenu ("Velocidad", v, any);
    }
    m.addSeparator();
    item ("Doble golpe (dividir en 2)", "Cmd+E", sel, [this] { splitSelected (2); });
    item ("Dividir en 3", {}, sel, [this] { splitSelected (3); });
    item ("Dividir en 4", {}, sel, [this] { splitSelected (4); });
    item ("Alargar un paso de rejilla", String::fromUTF8 ("Mayús+→"), sel, [this] { changeLength (1); });
    item ("Acortar un paso de rejilla", String::fromUTF8 ("Mayús+←"), sel, [this] { changeLength (-1); });
    m.addSeparator();
    item ("Copiar", "Cmd+C", sel, [this] { copyNotes (false); });
    item ("Cortar", "Cmd+X", sel, [this] { copyNotes (true); });
    item ("Pegar", "Cmd+V", ! noteClipboard().empty(), [this] { pasteNotes(); });
    item ("Duplicar", "Cmd+D", any, [this] { duplicateSelected(); });
    m.addSubMenu ("Secuencia aleatoria", randomMenu(), clip != nullptr);
    item ("Seleccionar todo", "Cmd+A", any, [this] { selectAll(); });
    item ("Borrar", "Supr", sel, [this] { deleteSelected(); });
    m.addSeparator();
    PopupMenu lengthMenu;
    if (clip != nullptr)
    {
        const double bar = vs.beatsPerBar(), now = clipContentRange().getLength();
        for (int bars : { 1, 2, 4, 8, 16, 32 })
            lengthMenu.addItem (String (bars) + (bars == 1 ? String::fromUTF8 (" compás") : String (" compases")), true, std::abs (now - bars * bar) < 1.0e-6,
                                [this, bars, bar] { setClipEnd (clipContentRange().getStart() + bars * bar); });
        lengthMenu.addSeparator();
        lengthMenu.addItem ("Al doble, repitiendo las notas", [this]
        {
            selection.clearQuick();
            const auto r = clipContentRange();
            for (auto* x : clip->getSequence().getNotes()) if (x->getStartBeat().inBeats() >= r.getStart() - 1.0e-6 && x->getStartBeat().inBeats() < r.getEnd()) selection.add (x);
            if (! selection.isEmpty()) duplicateSelected();
            setClipEnd (r.getStart() + r.getLength() * 2.0);
        });
    }
    m.addSubMenu ("Largo del clip", lengthMenu, clip != nullptr);
    PopupMenu gridMenu;
    for (int i = 0; i < gridChoice.getNumItems(); ++i)
        gridMenu.addItem (gridChoice.getItemText (i), true, i == gridChoice.getSelectedItemIndex(), [this, i] { setGridIndex (i); });
    m.addSubMenu ("Rejilla", gridMenu);
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void PianoRoll::setClipEnd (double contentBeat, bool newUndoStep)
{
    if (clip == nullptr) return;
    const auto r = clipContentRange();
    if (std::abs (contentBeat - r.getEnd()) < 1.0e-6) return;
    auto& um = vs.edit().getUndoManager();
    if (newUndoStep) um.beginNewTransaction();
    clip->setEnd (clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (contentBeat)), true);
    if (vs.showHint)
    {
        const double bars = (contentBeat - r.getStart()) / vs.beatsPerBar();
        vs.showHint ("La secuencia dura "_u + String (bars, std::abs (bars - std::round (bars)) < 1.0e-6 ? 0 : 2) + (std::abs (bars - 1.0) < 1.0e-6 ? String::fromUTF8 (" compás.") : String (" compases.")));
    }
    updateScrollBars();
    repaint();
}

double PianoRoll::contentStartEditBeat() const
{
    return clip != nullptr ? vs.timeToBeat (clip->getTimeOfContentBeat (te::BeatPosition())) : 0.0;
}

Range<double> PianoRoll::clipContentRange() const
{
    if (clip == nullptr) return {};
    auto pos = clip->getPosition();
    return { clip->getContentBeatAtTime (pos.getStart()).inBeats(), clip->getContentBeatAtTime (pos.getEnd()).inBeats() };
}

double PianoRoll::snapFloor (double b) const
{
    if (gridFree()) return b;
    const double g = gridBeats(), o = contentStartEditBeat();
    return std::floor ((b + o) / g + 1e-6) * g - o;
}

double PianoRoll::snapRound (double b) const
{
    if (gridFree()) return b;
    const double g = gridBeats(), o = contentStartEditBeat();
    return std::round ((b + o) / g) * g - o;
}

bool PianoRoll::isDrumClip() const
{
    if (clip == nullptr) return false;
    if (auto* at = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
        if (auto* inst = Session::instrumentOf (*at))
            return Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || inst->getName().containsIgnoreCase ("Hamster")
                   || inst->getName().containsIgnoreCase ("Mole");
    return false;
}

void PianoRoll::readDrumRows()
{
    drumRows.clear();
    if (! isDrumClip()) return;
    auto* at = dynamic_cast<te::AudioTrack*> (clip->getTrack());
    auto* ext = at != nullptr ? dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*at)) : nullptr;
    auto* pi = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
    if (pi == nullptr) return;
    static const std::map<String, const char*> roleEs { { "Kick", "Bombo" }, { "Snare", "Caja" }, { "Clap", "Palmas" }, { "Closed Hat", "Charles cerrado" },
        { "Open Hat", "Charles abierto" }, { "Ride", "Ride" }, { "Crash", "Crash" }, { "Rim", "Aro" }, { "Tom Low", "Tom grave" },
        { "Tom Mid", "Tom medio" }, { "Tom High", "Tom agudo" }, { "Shaker", "Shaker" }, { "Conga", "Conga" }, { "Bongo", "Bongo" },
        { "Cowbell", "Cencerro" }, { "Metal Perc", "Metal" }, { "Custom", "Libre" } };
    std::map<String, juce::AudioProcessorParameter*> byName;
    for (auto* prm : pi->getParameters()) byName[prm->getName (64)] = prm;
    std::map<String, int> seen;
    if (pi->getName().containsIgnoreCase ("Hamster"))
    {
        for (int t = 1; t <= 16; ++t)
        {
            auto role = byName.find ("T" + String (t) + " Role"), note = byName.find ("T" + String (t) + " MIDI Note");
            if (role == byName.end() || note == byName.end()) continue;
            const auto r = role->second->getCurrentValueAsText();
            String name = roleEs.count (r) ? String::fromUTF8 (roleEs.at (r)) : r;
            const int count = ++seen[name];
            if (count > 1) name << " " << count;
            drumRows.push_back ({ note->second->getCurrentValueAsText().getIntValue(), name });
        }
    }
    else
    {
        // Mole: its lanes are "Bombo: nota", "Caja: nota"...
        for (auto* prm : pi->getParameters())
        {
            const auto n = prm->getName (64);
            if (n.endsWith (": nota") && ! n.containsIgnoreCase ("complement"))
                drumRows.push_back ({ prm->getCurrentValueAsText().getIntValue(), n.upToLastOccurrenceOf (":", false, false) });
        }
    }
}

String PianoRoll::drumRowName (int pitch) const
{
    for (auto& r : drumRows) if (r.note == pitch) return r.name;
    return String::fromUTF8 (drumName (pitch).toRawUTF8());
}

Colour PianoRoll::trackColour() const
{
    if (clip != nullptr && clip->getTrack() != nullptr) return colourForTrack (*clip->getTrack());
    return Brand::rosa;
}

bool PianoRoll::inScale (int pitch) const
{
    const int mode = scaleMode.getSelectedItemIndex();
    if (mode <= 0) return true;
    const int rel = ((pitch - scaleRoot.getSelectedItemIndex()) % 12 + 12) % 12;
    for (int s : scales()[(size_t) mode].steps) if (s == rel) return true;
    return false;
}

void PianoRoll::refreshFold()
{
    foldedPitches.clearQuick();
    if (clip == nullptr) return;
    std::set<int> used;
    for (auto* n : clip->getSequence().getNotes()) used.insert (n->getNoteNumber());
    if (scaleMode.getSelectedItemIndex() > 0)
    {
        int lo = used.empty() ? 36 : *used.begin() - 5, hi = used.empty() ? 72 : *used.rbegin() + 5;
        for (int p = lo; p <= hi; ++p) if (inScale (p)) used.insert (p);
    }
    if (! drumRows.empty())
    {
        // the kit's own rows, top to bottom as the instrument shows them; notes the kit doesn't have go underneath
        for (auto& r : drumRows) foldedPitches.addIfNotAlreadyThere (r.note);
        for (auto it = used.rbegin(); it != used.rend(); ++it) foldedPitches.addIfNotAlreadyThere (*it);
        return;
    }
    if (isDrumClip())
        for (int p : { 36, 38, 39, 42, 46, 37, 41, 45, 49, 51 }) used.insert (p);   // the basic kit is always there to draw on
    if (used.empty()) for (int p = 48; p < 60; ++p) used.insert (p);
    for (auto it = used.rbegin(); it != used.rend(); ++it) foldedPitches.add (*it);
}

int PianoRoll::rowCount() const { return folded ? foldedPitches.size() : octaveView ? 12 * viewOctaves : 128; }
int PianoRoll::pitchForRow (int row) const
{
    if (folded) return foldedPitches[jlimit (0, jmax (0, foldedPitches.size() - 1), row)];
    if (octaveView) return octaveBase + 12 * viewOctaves - 1 - jlimit (0, 12 * viewOctaves - 1, row);
    return 127 - row;
}
int PianoRoll::rowForPitch (int pitch) const
{
    if (folded) return foldedPitches.indexOf (pitch);
    if (octaveView) return (pitch >= octaveBase && pitch < octaveBase + 12 * viewOctaves) ? octaveBase + 12 * viewOctaves - 1 - pitch : -1;
    return 127 - pitch;
}

String PianoRoll::keyLabel (int row) const
{
    const int p = pitchForRow (row);
    if (isDrumClip()) { const auto d = drumRowName (p); if (d.isNotEmpty()) return d; }
    return pitchName (p);
}

void PianoRoll::chooseOctave()
{
    // the octave that holds most of the notes (Do3, the middle, when the clip is empty)
    std::map<int, int> count;
    if (clip != nullptr) for (auto* n : clip->getSequence().getNotes()) ++count[n->getNoteNumber() / 12];
    int best = 5, most = 0;
    for (auto& [o, c] : count) if (c > most) { most = c; best = o; }
    octaveBase = jlimit (0, maxOctaveBase(), best * 12);
}

void PianoRoll::refreshOctaveButtons()
{
    int above = 0, below = 0;
    if (clip != nullptr)
        for (auto* n : clip->getSequence().getNotes())
        {
            if (n->getNoteNumber() >= octaveBase + 12 * viewOctaves) ++above;
            else if (n->getNoteNumber() < octaveBase) ++below;
        }
    octUp.setButtonText (above > 0 ? "+ " + String (above) : String ("+"));
    octDown.setButtonText (below > 0 ? "- " + String (below) : String ("-"));
    for (auto* b : { &octUp, &octDown }) b->setVisible (octaveView && clip != nullptr);
}

void PianoRoll::shiftOctave (int k)
{
    if (! octaveView || k == 0) return;
    octaveBase = jlimit (0, maxOctaveBase(), octaveBase + 12 * k);
    refreshOctaveButtons();
    if (vs.showHint) vs.showHint ((viewOctaves > 1 ? String ("Octavas ") : String ("Octava ")) + pitchName (octaveBase) + " a " + pitchName (octaveBase + 12 * viewOctaves - 1));
    repaint();
}

void PianoRoll::setViewMode (bool octave, bool fold)
{
    octaveView = octave;
    folded = fold;
    if (folded) octaveView = false;
    foldButton.setToggleState (folded, dontSendNotification);
    octaveButton.setToggleState (octaveView, dontSendNotification);
    if (! folded && ! octaveView) keyHeight = 14.0f;
    if (folded) refreshFold();
    if (octaveView) chooseOctave();
    refreshOctaveButtons();
    centreOnNotes();
    resized();
    repaint();
}

void PianoRoll::afterVerticalMove (int semitones)
{
    // a move by whole octaves takes the view along; any other move only when the notes left the screen
    refreshOctaveButtons();
    if (! octaveView || selection.isEmpty()) return;
    if (semitones != 0 && semitones % 12 == 0)
    {
        octaveBase = jlimit (0, maxOctaveBase(), octaveBase + semitones);
        refreshOctaveButtons();
        return;
    }
    for (auto* n : selection) if (rowForPitch (n->getNoteNumber()) >= 0) return;
    octaveBase = jlimit (0, maxOctaveBase(), (selection[0]->getNoteNumber() / 12) * 12);
    refreshOctaveButtons();
}

float PianoRoll::scrollOffsetY() const
{
    if (octaveView) return 0.0f;
    if (folded) return jmax (0.0f, foldScroll);
    return (127.0f - scrollPitch) * keyHeight;
}

void PianoRoll::scrollRows (float rows)
{
    // `rows` are note rows (positive goes down)
    if (octaveView)
    {
        // the wheel moves a whole octave once it has turned a few rows' worth
        wheelRows += rows;
        if (std::abs (wheelRows) >= 3.0f) { shiftOctave (wheelRows > 0 ? -1 : 1); wheelRows = 0.0f; }
        return;
    }
    if (folded) foldScroll = jlimit (0.0f, jmax (0.0f, (float) rowCount() * keyHeight - (float) grid->getHeight()), foldScroll + rows * keyHeight);
    else scrollPitch = jlimit ((float) grid->getHeight() / keyHeight - 1.0f, 127.0f, scrollPitch - rows);
}

void PianoRoll::centreOnNotes()
{
    if (clip == nullptr || grid == nullptr) return;
    if (octaveView)
    {
        // a taller panel shows more octaves and the notes keep their size: a row never grows past 18 px, another
        // octave comes in instead (126 px of grid: one octave of 10.5 px rows; 420 px: two octaves of 17.5 px)
        const int gh = grid->getHeight();
        const int was = viewOctaves;
        viewOctaves = jlimit (1, 5, (int) std::ceil ((float) gh / (12.0f * 18.0f)));
        if (viewOctaves != was)
        {
            // grow around the notes: the octave that was at the bottom goes down only if the top runs out of keys
            if (viewOctaves > was && clip != nullptr)
            {
                int lo = 127, hi = 0;
                for (auto* n : clip->getSequence().getNotes()) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
                if (hi >= lo && lo < octaveBase) octaveBase = (lo / 12) * 12;
            }
            octaveBase = jlimit (0, maxOctaveBase(), octaveBase);
            refreshOctaveButtons();
        }
        keyHeight = jmax (8.0f, (float) gh / (12.0f * (float) viewOctaves));
        updateScrollBars();
        return;
    }
    if (folded && grid->getHeight() > 40)
    {
        // folded rows are few: make them fit the editor when they can
        keyHeight = jlimit (12.0f, isDrumClip() ? 24.0f : 18.0f, (float) grid->getHeight() / (float) jmax (1, rowCount()));
        // the kit's rows start at the top (the kick first, like in the drum machine); the old fold shows the bottom
        foldScroll = drumRows.empty() ? jmax (0.0f, (float) rowCount() * keyHeight - (float) grid->getHeight()) : 0.0f;
        updateScrollBars();
        return;
    }
    auto r = clip->getSequence().getNoteNumberRange();
    const float visible = (float) jmax (100, grid->getHeight()) / keyHeight;
    const float mid = clip->getSequence().getNumNotes() > 0 ? (float) (r.getStart() + r.getEnd()) * 0.5f : 60.0f;
    scrollPitch = jlimit (visible - 1.0f, 127.0f, mid + visible * 0.5f);
    foldScroll = 0.0f;
    updateScrollBars();
}

void PianoRoll::updateScrollBars()
{
    const float visibleRows = (float) jmax (1, grid->getHeight()) / keyHeight;
    vScroll.setRangeLimits (0.0, 128.0, dontSendNotification);
    vScroll.setCurrentRange (127.0 - scrollPitch, visibleRows, dontSendNotification);
    vScroll.setVisible (! folded && ! octaveView);
    const auto range = clipContentRange();
    const double visibleBeats = (double) jmax (1, grid->getWidth()) / pxPerBeat;
    hScroll.setRangeLimits (0.0, jmax (range.getEnd() + 8.0, scrollBeat + visibleBeats), dontSendNotification);
    hScroll.setCurrentRange (scrollBeat, visibleBeats, dontSendNotification);
}

Rectangle<int> PianoRoll::gridBounds() const { return grid->getBounds(); }
int PianoRoll::gridHeight() const { return grid != nullptr ? grid->getHeight() : 200; }

Point<int> PianoRoll::pointFor (double clipBeat, int pitch) const
{
    const int row = rowForPitch (pitch);
    return grid->getPosition() + Point<int> ((int) beatToX (clipBeat) + 2, (int) ((float) row * keyHeight - scrollOffsetY() + keyHeight * 0.5f));
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).reduced (8, 6);
    foldButton.setBounds (bar.removeFromRight (70)); bar.removeFromRight (6);
    octaveButton.setBounds (bar.removeFromRight (74)); bar.removeFromRight (6);
    doubleButton.setBounds (bar.removeFromRight (80)); bar.removeFromRight (6);
    holdButton.setBounds (bar.removeFromRight (64)); bar.removeFromRight (2);
    randomButton.setBounds (bar.removeFromRight (96)); bar.removeFromRight (6);
    quantiseOptions.setBounds (bar.removeFromRight (26)); bar.removeFromRight (2);
    quantiseButton.setBounds (bar.removeFromRight (90)); bar.removeFromRight (14);
    velocitySlider.setBounds (bar.removeFromRight (112)); bar.removeFromRight (36);     // room for "VEL"
    scaleMode.setBounds (bar.removeFromRight (124)); bar.removeFromRight (4);
    scaleRoot.setBounds (bar.removeFromRight (56)); bar.removeFromRight (62);           // room for "ESCALA"
    splitButton.setBounds (bar.removeFromRight (78)); bar.removeFromRight (10);
    coarserButton.setBounds (bar.removeFromRight (26)); bar.removeFromRight (2);
    gridChoice.setBounds (bar.removeFromRight (80)); bar.removeFromRight (2);
    finerButton.setBounds (bar.removeFromRight (26)); bar.removeFromRight (62);         // room for "REJILLA"
    title.setBounds (bar);

    {
        // over the keyboard: one octave down, the octave shown, one octave up
        auto oct = r.withHeight (rulerHeight).withWidth (keyboardWidth).reduced (2, 2);
        octDown.setBounds (oct.removeFromLeft (30));
        octUp.setBounds (oct.removeFromRight (30));
    }
    r.removeFromTop (rulerHeight);
    auto vel = r.removeFromBottom (velocityHeight);
    auto hs = r.removeFromBottom (10);
    keyboard->setBounds (r.removeFromLeft (keyboardWidth));
    vScroll.setBounds (r.removeFromRight (10));
    grid->setBounds (r);
    hScroll.setBounds (hs.withTrimmedLeft (keyboardWidth).withTrimmedRight (10));
    velocity->setBounds (vel.withTrimmedLeft (keyboardWidth).withTrimmedRight (10));
    // a clip opened while the editor was hidden (or small) is framed again once the editor has its real size,
    // so its notes are on screen without scrolling
    if (clip != nullptr && (scrollPitch > 126.0f || folded || octaveView || (needsCentre && grid->getHeight() > 60)))
    {
        centreOnNotes();
        needsCentre = grid->getHeight() <= 60;
    }
    updateScrollBars();
}

void PianoRoll::paint (Graphics& g)
{
    // the panel wears the colour of the channel whose notes it shows
    // the panel carries the colour of the channel whose notes it shows: a faint wash and a band on top
    g.fillAll (clip != nullptr ? S::shade.interpolatedWith (trackColour(), 0.10f) : S::shade);
    if (clip != nullptr) { g.setColour (trackColour()); g.fillRect (0, 0, getWidth(), 3); }
    g.setColour (C::stoneHi);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
    drawCaption (g, "REJILLA", finerButton.getBounds().translated (-58, 0).withWidth (54), 11.0f, S::inkSoft, Justification::centredRight);
    drawCaption (g, "ESCALA", scaleRoot.getBounds().translated (-58, 0).withWidth (54), 11.0f, S::inkSoft, Justification::centredRight);
    drawCaption (g, "VEL", velocitySlider.getBounds().translated (-34, 0).withWidth (30), 11.0f, S::inkSoft, Justification::centredRight);

    // bar ruler over the grid
    auto ruler = Rectangle<int> (grid->getX(), toolbarHeight, grid->getWidth(), rulerHeight);
    g.setColour (C::basalt);
    g.fillRect (ruler.withX (0).withWidth (getWidth()));
    if (clip == nullptr)
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (16.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("Haz doble clic en un clip MIDI de la línea de tiempo para editar sus notas.")),
                    getLocalBounds().withTrimmedTop (toolbarHeight + rulerHeight), Justification::centred);
        return;
    }
    if (octaveView)
    {
        g.setColour (C::bone);
        g.setFont (Fonts::mono (11.0f));
        g.drawText (::racoon::i18n::tr (pitchName (octaveBase)), Rectangle<int> (32, toolbarHeight, keyboardWidth - 64, rulerHeight), Justification::centred);
    }
    const int bpb = vs.beatsPerBar();
    const double offset = contentStartEditBeat();
    g.setFont (Fonts::mono (11.0f));
    for (double b = std::floor ((xToBeat (0.0f) + offset) / bpb) * bpb - offset; ; b += 1.0)
    {
        const float x = (float) ruler.getX() + beatToX (b);
        if (x > (float) ruler.getRight()) break;
        if (x < (float) ruler.getX()) continue;
        const double eb = b + offset;
        const bool isBar = std::abs (std::fmod (eb, (double) bpb)) < 1e-6;
        g.setColour (isBar ? C::bone : C::ash);
        g.fillRect (x, (float) ruler.getY() + (isBar ? 0.0f : 12.0f), 1.0f, (float) rulerHeight);
        if (isBar) g.drawText (::racoon::i18n::tr (String ((int) std::round (eb) / bpb + 1)), Rectangle<float> (x + 3.0f, (float) ruler.getY(), 40.0f, 14.0f), Justification::centredLeft);
    }
}

void PianoRoll::audition (int pitch, int vel)
{
    if (clip == nullptr || vs.session.getTransport().isPlaying()) return;
    if (auto* at = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
    {
        vs.session.getTransport().ensureContextAllocated();
        at->injectLiveMidiMessage (MidiMessage::noteOn (1, pitch, (uint8) vel), te::MPESourceID {});
        te::AudioTrack::Ptr keep (at);
        Timer::callAfterDelay (220, [keep, pitch] { keep->injectLiveMidiMessage (MidiMessage::noteOff (1, pitch), te::MPESourceID {}); });
    }
}

void PianoRoll::selectAll()
{
    if (clip == nullptr) return;
    selection = clip->getSequence().getNotes();
    repaint();
}

void PianoRoll::deleteSelected()
{
    if (clip == nullptr || selection.isEmpty()) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    for (auto* n : selection) clip->getSequence().removeNote (*n, um);
    selection.clearQuick();
    repaint();
}

void PianoRoll::transposeSelected (int st)
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    for (auto* n : selection) n->setNoteNumber (jlimit (0, 127, n->getNoteNumber() + st), um);
    if (selection.size() == 1) audition (selection[0]->getNoteNumber(), selection[0]->getVelocity());
    afterVerticalMove (st);
    repaint();
}

void PianoRoll::duplicateSelectedPitch (int st)
{
    // the selected notes stay and a copy of each one sounds an octave away (thicker bass, doubled melody);
    // the copies end up selected, so pressing again stacks another octave
    if (clip == nullptr || selection.isEmpty()) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    auto& seq = clip->getSequence();
    Array<te::MidiNote*> made;
    for (auto* n : Array<te::MidiNote*> (selection))
    {
        const int p = n->getNoteNumber() + st;
        if (p < 0 || p > 127) continue;
        if (auto* c = seq.addNote (p, n->getStartBeat(), n->getLengthBeats(), n->getVelocity(), n->getColour(), um)) made.add (c);
    }
    selection = made;
    afterVerticalMove (st);
    if (vs.showHint) vs.showHint (String (made.size()) + (st > 0 ? String::fromUTF8 (" notas copiadas una octava arriba.") : String::fromUTF8 (" notas copiadas una octava abajo.")));
    repaint();
}

// ---------------------------------------------------------------- MIDI tools

void PianoRoll::moveOctave (int k)
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    const bool all = selection.isEmpty();
    notes::transpose (*clip, selection, 12 * k, um);
    if (all) { if (octaveView) octaveBase = jlimit (0, maxOctaveBase(), octaveBase + 12 * k); refreshOctaveButtons(); }
    else afterVerticalMove (12 * k);
    if (vs.showHint) vs.showHint (String (all ? "Todas las notas" : "Las notas seleccionadas") + (k > 0 ? " subieron " : " bajaron ") + String (std::abs (k)) + (std::abs (k) == 1 ? " octava." : " octavas."));
    repaint();
}

void PianoRoll::reverseNotes()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    notes::reverse (*clip, selection, um);
    if (vs.showHint) vs.showHint (String::fromUTF8 (selection.isEmpty() ? "Reverso: el clip completo quedó al revés." : "Reverso: las notas seleccionadas quedaron al revés."));
    repaint();
}

void PianoRoll::invertNotes()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    notes::invert (*clip, selection, um);
    if (vs.showHint) vs.showHint (String::fromUTF8 ("Invertir: la nota más aguda quedó abajo y la más grave arriba."));
    afterVerticalMove();
    repaint();
}

void PianoRoll::legatoNotes()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    notes::legato (*clip, selection, um);
    if (vs.showHint) vs.showHint (String::fromUTF8 ("Legato: cada nota dura hasta que empieza la siguiente."));
    repaint();
}

void PianoRoll::toggleMuteNotes()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    notes::toggleMute (*clip, selection, um);
    repaint();
}

void PianoRoll::changeLength (int steps)
{
    if (clip == nullptr || selection.isEmpty()) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    const double g = gridFree() ? 0.25 : gridBeats();
    for (auto* n : selection)
        n->setStartAndLength (n->getStartBeat(), te::BeatDuration::fromBeats (jmax (g * 0.5, n->getLengthBeats().inBeats() + steps * g)), um);
    repaint();
}

void PianoRoll::copyNotes (bool cut)
{
    if (clip == nullptr || selection.isEmpty()) { if (vs.showHint) vs.showHint (String::fromUTF8 ("Selecciona notas para copiarlas (Cmd+A selecciona todas).")); return; }
    double s = 1.0e9;
    for (auto* n : selection) s = jmin (s, n->getStartBeat().inBeats());
    auto& cb = noteClipboard();
    cb.clear();
    for (auto* n : selection) cb.push_back ({ n->getStartBeat().inBeats() - s, n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity(), n->isMute() });
    if (vs.showHint) vs.showHint ((cut ? "Cortadas "_u : "Copiadas "_u) + String ((int) cb.size()) + String::fromUTF8 (" notas. Cmd+V las pega en el cursor (o detrás de lo seleccionado)."));
    if (cut) deleteSelected();
}

void PianoRoll::pasteNotes()
{
    auto& cb = noteClipboard();
    if (clip == nullptr || cb.empty()) return;
    // at the playhead when it is inside this clip; otherwise right after the selection, or at the start of the clip
    const auto range = clipContentRange();
    double at = range.getStart();
    auto& tr = vs.session.getTransport();
    if (clip->getEditTimeRange().contains (tr.getPosition())) at = snapFloor (clip->getContentBeatAtTime (tr.getPosition()).inBeats());
    else if (! selection.isEmpty()) { at = -1.0e9; for (auto* n : selection) at = jmax (at, n->getEndBeat().inBeats()); }
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    Array<te::MidiNote*> added;
    for (auto& c : cb)
        if (auto* n = clip->getSequence().addNote (c.pitch, te::BeatPosition::fromBeats (jmax (0.0, at + c.start)), te::BeatDuration::fromBeats (c.length), c.velocity, 0, um))
        {
            if (c.mute) n->setMute (true, um);
            added.add (n);
        }
    selection = added;
    afterVerticalMove();
    repaint();
}

void PianoRoll::duplicateSelected()
{
    if (clip == nullptr) return;
    if (selection.isEmpty()) selectAll();
    if (selection.isEmpty()) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    double s = 1e9, e = -1e9;
    for (auto* n : selection) { s = jmin (s, n->getStartBeat().inBeats()); e = jmax (e, n->getEndBeat().inBeats()); }
    // the copy starts right where the selected notes end, rounded up to the grid (not to the bar), so
    // the empty time around the notes is not copied with them
    const double g = jmax (1.0 / 32.0, gridBeats());
    double len = std::ceil ((e - s) / g - 1e-6) * g;
    if (len <= 0.0) len = g;
    Array<te::MidiNote*> added;
    for (auto* n : Array<te::MidiNote*> (selection))
        added.add (clip->getSequence().addNote (n->getNoteNumber(), n->getStartBeat() + te::BeatDuration::fromBeats (len), n->getLengthBeats(), n->getVelocity(), 0, um));
    // grow the clip if the copy runs past its end
    const auto newEnd = clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (e + len));
    if (newEnd > clip->getPosition().getEnd()) clip->setEnd (newEnd, true);
    selection = added;
    repaint();
}

namespace
{
// the quantize settings live in the app settings, so Q does the same thing tomorrow
notes::QuantizeOptions quantizeSettings (double grid)
{
    auto& p = prefs();
    notes::QuantizeOptions o;
    o.grid = grid;
    o.strength = p.getDoubleValue ("qStrength", 100.0) / 100.0;
    o.swing = p.getDoubleValue ("qSwing", 0.0) / 100.0;
    o.random = p.getDoubleValue ("qRandom", 0.0) / 100.0;
    o.ends = p.getBoolValue ("qEnds", false);
    o.keepLength = p.getBoolValue ("qKeepLength", true);
    o.seed = Time::currentTimeMillis();
    return o;
}

class QuantizePanel : public Component
{
public:
    explicit QuantizePanel (std::function<void()> applyFn) : onApply (std::move (applyFn))
    {
        auto& p = prefs();
        auto setup = [this] (Slider& s, Label& l, const String& name, const String& key, double def, const String& help)
        {
            l.setText (name, dontSendNotification);
            l.setFont (Fonts::label (12.0f));
            l.setColour (Label::textColourId, S::ink);
            addAndMakeVisible (l);
            s.setSliderStyle (Slider::LinearHorizontal);
            s.setRange (0.0, 100.0, 1.0);
            s.setTextValueSuffix (" %");
            s.setTextBoxStyle (Slider::TextBoxRight, false, 52, 20);
            s.setValue (prefs().getDoubleValue (key, def), dontSendNotification);
            s.setTooltip (help);
            s.onValueChange = [&s, key] { prefs().setValue (key, s.getValue()); };
            addAndMakeVisible (s);
        };
        setup (strength, strengthL, "FUERZA", "qStrength", 100.0, String::fromUTF8 ("Cuánto se acerca cada nota a la rejilla. 100 %: justo encima. 50 %: a medio camino, conserva algo de tu toque."));
        setup (swing, swingL, "SWING", "qSwing", 0.0, String::fromUTF8 ("Retrasa una de cada dos líneas de la rejilla. 100 % es sentir de tresillo; 20 a 35 % suena a house."));
        setup (random, randomL, "AZAR", "qRandom", 0.0, String::fromUTF8 ("Después de cuantizar, mueve cada nota un poco al azar para que no suene a máquina."));
        ends.setButtonText ("Cuantizar los finales");
        ends.setToggleState (p.getBoolValue ("qEnds", false), dontSendNotification);
        ends.onClick = [this] { prefs().setValue ("qEnds", ends.getToggleState()); keep.setEnabled (! ends.getToggleState()); };
        keep.setButtonText (String::fromUTF8 ("Conservar la duración"));
        keep.setToggleState (p.getBoolValue ("qKeepLength", true), dontSendNotification);
        keep.setEnabled (! ends.getToggleState());
        keep.setTooltip (String::fromUTF8 ("Encendido: la nota se mueve entera. Apagado: el final se queda donde estaba."));
        keep.onClick = [this] { prefs().setValue ("qKeepLength", keep.getToggleState()); };
        apply.setButtonText ("CUANTIZAR");
        apply.onClick = [this] { if (onApply) onApply(); };
        reset.setButtonText ("COMO ANTES");
        reset.setTooltip (String::fromUTF8 ("Fuerza 100 %, sin swing ni azar"));
        reset.onClick = [this] { strength.setValue (100.0); swing.setValue (0.0); random.setValue (0.0);
                                 ends.setToggleState (false, sendNotification); keep.setToggleState (true, sendNotification); };
        for (auto* c : std::initializer_list<Component*> { &ends, &keep, &apply, &reset }) addAndMakeVisible (c);
        setSize (330, 210);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        for (auto [s, l] : { std::pair<Slider*, Label*> { &strength, &strengthL }, { &swing, &swingL }, { &random, &randomL } })
        {
            auto row = r.removeFromTop (28);
            l->setBounds (row.removeFromLeft (64));
            s->setBounds (row);
            r.removeFromTop (4);
        }
        ends.setBounds (r.removeFromTop (24));
        keep.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
        auto b = r.removeFromTop (28);
        apply.setBounds (b.removeFromRight (110));
        b.removeFromRight (8);
        reset.setBounds (b.removeFromRight (110));
    }
    Slider strength, swing, random;
    Label strengthL, swingL, randomL;
    ToggleButton ends, keep;
    TextButton apply, reset;
    std::function<void()> onApply;
};
}

void PianoRoll::quantiseSelected()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    const double step = gridFree() ? 0.25 : gridBeats();
    auto o = quantizeSettings (step);
    // the notes are stored in content beats; the grid is counted from the start of the song
    o.offset = contentStartEditBeat();
    notes::quantize (*clip, selection, o, um);
    if (vs.showHint && (o.strength < 0.999 || o.swing > 0.0 || o.random > 0.0))
        vs.showHint ("Cuantizado: fuerza "_u + String (roundToInt (o.strength * 100)) + " %, swing " + String (roundToInt (o.swing * 100))
                     + " %" + (o.random > 0.0 ? ", azar " + String (roundToInt (o.random * 100)) + " %" : String()) + ".");
    repaint();
}

void PianoRoll::showQuantizeOptions()
{
    auto panel = std::make_unique<QuantizePanel> ([sp = SafePointer<PianoRoll> (this)] { if (sp != nullptr) sp->quantiseSelected(); });
    // inside the main window (not a window of its own): it never ends up behind the app
    auto* top = getTopLevelComponent();
    CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&quantiseButton, quantiseButton.getLocalBounds()), top);
}

void PianoRoll::velocityTool (int which)
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    auto list = notes::orAll (*clip, selection);
    if (list.isEmpty()) return;
    int lo = 127, hi = 1;
    for (auto* n : list) { lo = jmin (lo, n->getVelocity()); hi = jmax (hi, n->getVelocity()); }
    // a ramp over notes that are all alike goes from soft to strong; otherwise it keeps the range they already use
    if (hi - lo < 20) { lo = jmax (1, jmin (lo, 100) - 50); hi = jmin (127, jmax (hi, 100) + 10); }
    switch (which)
    {
        case 0: notes::velocityRamp (*clip, selection, lo, hi, um); break;
        case 1: notes::velocityRamp (*clip, selection, hi, lo, um); break;
        case 2: notes::velocityHumanize (*clip, selection, 8, Time::currentTimeMillis(), um); break;
        case 3: notes::velocityHumanize (*clip, selection, 20, Time::currentTimeMillis(), um); break;
        case 4: notes::velocityScale (*clip, selection, 1.15, um); break;
        case 5: notes::velocityScale (*clip, selection, 0.85, um); break;
        case 6: notes::velocityCompress (*clip, selection, 0.5, um); break;
        case 7: notes::velocitySet (*clip, selection, newVelocity, um); break;
        default: break;
    }
    repaint();
    if (velocity != nullptr) velocity->repaint();
}

void PianoRoll::setScale (int root, bool minor)
{
    scaleRoot.setSelectedItemIndex (jlimit (0, 11, root), dontSendNotification);
    scaleMode.setSelectedItemIndex (minor ? 2 : 1, sendNotificationSync);
    repaint();
}

// ===================================================================== ALEATORIA
aleatorio::Rol PianoRoll::defaultRole() const
{
    using aleatorio::Rol;
    if (clip == nullptr) return Rol::melodia;
    if (auto* at = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
    {
        if (auto* inst = Session::instrumentOf (*at))
        {
            const auto animal = Catalog::animalFor (inst->getName());
            if (animal.isNotEmpty()) return aleatorio::rolDe (animal);
        }
        for (auto* p : at->pluginList)                    // Skunk and Chinchilla sit on audio channels as effects
            if (Catalog::isRacoonPlugin (*p))
                if (auto animal = Catalog::animalFor (p->getName()); animal.isNotEmpty()) return aleatorio::rolDe (animal);
        // other instruments: what the channel is called says it
        const auto n = at->getName().toLowerCase();
        auto has = [&n] (std::initializer_list<const char*> w) { for (auto* x : w) if (n.contains (String::fromUTF8 (x))) return true; return false; };
        if (has ({ "shaker", "conga", "bongo", "perc" })) return Rol::percusion;
        if (isDrumClip() || has ({ "bater", "drum", "kick", "bombo" })) return Rol::bateria;
        if (has ({ "bajo", "bass", "808" })) return Rol::bajo;
        if (has ({ "coro", "choir", "voces" })) return Rol::coro;
        if (has ({ "voz", "vocal", "vox" })) return Rol::voz;
        if (has ({ "pad", "cuerda", "string" })) return Rol::pad;
        if (has ({ "guitar" })) return Rol::guitarra;
        if (has ({ "acorde", "chord", "piano", "keys", "stab" })) return Rol::acordes;
        if (has ({ "arp" })) return Rol::arpegio;
        if (has ({ "textura", "texture", "drone", "fx", "atm" })) return Rol::textura;
    }
    if (isDrumClip()) return Rol::bateria;
    return Rol::melodia;
}

void PianoRoll::randomKey (int& root, bool& minor, String* where) const
{
    // 1. the scale helper of the Piano, when it is on
    if (scaleMode.getSelectedItemIndex() > 0)
    {
        root = scaleRoot.getSelectedItemIndex();
        const auto name = scaleMode.getText().toLowerCase();
        minor = ! (name.contains ("mayor") || name.contains ("mixolidia"));
        if (where != nullptr) *where = "la escala del Piano";
        return;
    }
    // 2. the notes the other melodic channels of the song already play
    std::vector<aleatorio::Nota> all;
    for (auto* t : te::getAudioTracks (vs.edit()))
    {
        auto* inst = Session::instrumentOf (*t);
        if (inst != nullptr && (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole"))) continue;
        for (auto* c : t->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc != clip)
                for (auto* n : mc->getSequence().getNotes())
                    all.push_back ({ n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
    }
    if (aleatorio::tonalidadDe (all, root, minor)) { if (where != nullptr) *where = "la de la canción"_u; return; }
    // 3. this clip's own notes
    std::vector<aleatorio::Nota> own;
    if (clip != nullptr)
        for (auto* n : clip->getSequence().getNotes()) own.push_back ({ n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
    if (aleatorio::tonalidadDe (own, root, minor)) { if (where != nullptr) *where = "la de este clip"; return; }
    root = 9; minor = true;
    if (where != nullptr) *where = "La menor, para empezar";
}

aleatorio::Bateria PianoRoll::drumMap() const
{
    // the kit's own rows (Hamster's tracks, Mole's lanes); the first track of each role plays it
    aleatorio::Bateria d;
    std::map<String, bool> seen;
    auto take = [&seen] (const String& key, int& slot, int note) { if (! seen[key]) { slot = note; seen[key] = true; } };
    for (auto& r : drumRows)
    {
        const auto n = r.name.toLowerCase();
        if (n.startsWith ("bombo")) take ("kick", d.kick, r.note);
        else if (n.startsWith ("caja")) take ("snare", d.snare, r.note);
        else if (n.startsWith ("palmas")) take ("clap", d.clap, r.note);
        else if (n.startsWith ("charles cerrado")) take ("ch", d.closedHat, r.note);
        else if (n.startsWith ("charles abierto")) take ("oh", d.openHat, r.note);
        else if (n.startsWith ("aro")) take ("rim", d.rim, r.note);
        else if (n.startsWith ("tom")) take ("tom", d.tom, r.note);
        else if (n.startsWith ("shaker") || n.startsWith ("maraca")) { take ("shaker", d.shaker, r.note); take ("perc", d.perc, r.note); }
        else if (n.startsWith ("conga")) { take ("congaHi", d.congaHi, r.note); take ("congaLo", d.congaLo, r.note); take ("perc", d.perc, r.note); }
        else if (n.startsWith ("bongo")) { take ("bongoHi", d.bongoHi, r.note); take ("bongoLo", d.bongoLo, r.note); take ("perc", d.perc, r.note); }
        else if (n.startsWith ("cencerro")) { take ("cowbell", d.cowbell, r.note); take ("perc", d.perc, r.note); }
        else if (n.startsWith ("metal")) take ("perc", d.perc, r.note);
    }
    if (! drumRows.empty())
    {
        // a kit without claps or open hats uses what it has
        if (! seen["clap"]) d.clap = d.snare;
        if (! seen["oh"]) d.openHat = d.closedHat;
    }
    return d;
}

PopupMenu PianoRoll::randomMenu()
{
    using aleatorio::Rol;
    PopupMenu m;
    const auto def = defaultRole();
    const bool has = clip != nullptr && clip->getSequence().getNumNotes() > 0;
    m.addItem (String::fromUTF8 ("Aleatoria sobre lo que hay (mantiene compases, ritmo y registro)"), clip != nullptr && has, false, [this] { generateRandom(); });
    m.addItem (String::fromUTF8 ("Aleatoria con patrón nuevo en todo el clip (") + aleatorio::nombreRol (def) + ")", clip != nullptr, false, [this] { generateRandom (std::nullopt, 0, true); });
    m.addItem (String::fromUTF8 ("Fijar este clip (ALEATORIA no lo cambia)"), clip != nullptr, clip != nullptr && isHeld (*clip),
               [this] { holdButton.setToggleState (! holdButton.getToggleState(), sendNotificationSync); });
    m.addSeparator();
    PopupMenu roles;
    for (auto r : { Rol::bateria, Rol::percusion, Rol::bajo, Rol::pad, Rol::acordes, Rol::coro, Rol::guitarra, Rol::arpegio,
                    Rol::melodia, Rol::voz, Rol::textura })
        roles.addItem (aleatorio::nombreRol (r) + (r == def ? String::fromUTF8 ("  (la de este instrumento)") : String()),
                       clip != nullptr, false, [this, r] { generateRandom (r, 0, true); });
    m.addSubMenu (String::fromUTF8 ("Aleatoria con patrón nuevo de..."), roles, clip != nullptr);
    return m;
}

bool PianoRoll::isHeld (const te::Clip& c) { return (bool) c.state.getProperty ("racoonHold", false); }

std::set<int> PianoRoll::heldRows() const
{
    std::set<int> out;
    if (clip != nullptr)
        for (auto& s : StringArray::fromTokens (clip->state.getProperty ("racoonHoldRows").toString(), ",", ""))
            if (s.isNotEmpty()) out.insert (s.getIntValue());
    return out;
}
bool PianoRoll::isRowHeld (int pitch) const { return heldRows().count (pitch) > 0; }
void PianoRoll::setRowHeld (int pitch, bool on)
{
    if (clip == nullptr) return;
    auto rows = heldRows();
    if (on) rows.insert (pitch); else rows.erase (pitch);
    String s;
    for (int x : rows) s << x << ",";
    clip->state.setProperty ("racoonHoldRows", s, nullptr);
    if (vs.showHint)
        vs.showHint (on ? drumRowName (pitch) + String::fromUTF8 (" fijado: ALEATORIA ya no mueve su patrón; el resto del kit sí.")
                        : drumRowName (pitch) + String::fromUTF8 (" suelto: ALEATORIA puede moverlo otra vez."));
}
void PianoRoll::setHeld (te::Clip& c, bool on) { c.state.setProperty ("racoonHold", on, nullptr); }

namespace
{
String notesKey (const std::vector<aleatorio::Nota>& v)
{
    auto s = v;
    std::sort (s.begin(), s.end(), [] (const aleatorio::Nota& a, const aleatorio::Nota& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    String k;
    for (auto& n : s) k << String (n.start, 3) << ":" << n.pitch << ":" << String (n.length, 3) << ":" << n.velocity << ";";
    return String (k.hashCode64());
}
String serialise (const std::vector<aleatorio::Nota>& v)
{
    String k;
    for (auto& n : v) k << String (n.start, 5) << "," << String (n.length, 5) << "," << n.pitch << "," << n.velocity << ";";
    return k;
}
std::vector<aleatorio::Nota> deserialise (const String& s)
{
    std::vector<aleatorio::Nota> out;
    for (auto& item : StringArray::fromTokens (s, ";", ""))
    {
        auto f = StringArray::fromTokens (item, ",", "");
        if (f.size() == 4) out.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), f[3].getIntValue() });
    }
    return out;
}
}

bool PianoRoll::generateRandom (std::optional<aleatorio::Rol> role, int64 seed, bool fresh, bool ownUndoStep)
{
    if (clip == nullptr)
    {
        if (vs.showHint) vs.showHint (String::fromUTF8 ("Abre un clip en el Piano (clic en un clip de la maqueta) para llenarlo con una secuencia aleatoria."));
        return false;
    }
    if (isHeld (*clip))
    {
        if (vs.showHint) vs.showHint (String::fromUTF8 ("Este clip está fijado: ALEATORIA no lo cambia. Quita FIJAR para variarlo."));
        return false;
    }
    aleatorio::Opciones o;
    o.rol = role.value_or (defaultRole());
    o.beatsPerBar = jmax (1, vs.beatsPerBar());
    const auto range = clipContentRange();
    // the whole clip, however long: the generator makes up to 16 bars and longer clips repeat them
    const int clipBars = jmax (1, (int) std::ceil (range.getLength() / (double) o.beatsPerBar - 1.0e-6));
    o.bars = jmin (16, clipBars);
    String where;
    randomKey (o.root, o.minor, &where);
    o.drums = drumMap();
    o.seed = seed != 0 ? seed : (int64) Time::getHighResolutionTicks();
    // the song's style and its chords: every part generated here plays over the same harmony
    const auto genreId = vs.edit().state.getProperty ("racoonGenre").toString();
    o.estilo = aleatorio::estiloDe (genreId, vs.session.getBpm());
    const auto* genre = findGenre (genreId);
    {
        // the song's key (the genre's, or the one ARMONIZAR left), unless the Piano's scale helper says another
        int sr = 0; bool sm = false;
        if (scaleMode.getSelectedItemIndex() <= 0 && vs.session.songKey (sr, sm)) { o.root = sr; o.minor = ! sm; where = "la de la rola"_u; }
    }
    // where the clip sits in the song: its first bar, so a four-bar progression lines up with the song's
    const double clipStartBeat = vs.timeToBeat (clip->getTimeOfContentBeat (te::BeatPosition::fromBeats (range.getStart())));
    const int firstBar = jmax (0, (int) std::round (clipStartBeat / o.beatsPerBar));
    std::vector<int> four;
    if (genre != nullptr && genre->major == ! o.minor) four = { genre->prog[0], genre->prog[1], genre->prog[2], genre->prog[3] };
    else
    {
        const auto stored = StringArray::fromTokens (vs.edit().state.getProperty ("racoonRandomProg").toString(), ",", "");
        if (stored.size() == 4) for (auto& x : stored) four.push_back (x.getIntValue());
        else
        {
            Random r (o.seed);
            four = aleatorio::detail::progression (r, o.minor);
            vs.edit().state.setProperty ("racoonRandomProg", String (four[0]) + "," + String (four[1]) + "," + String (four[2]) + "," + String (four[3]), nullptr);
        }
    }
    // bar by bar: what the other channels play there decides the chord (the song's own harmony); where nobody
    // plays, the progression
    {
        const double bpb = o.beatsPerBar;
        std::vector<std::array<double, 12>> weight ((size_t) o.bars);
        std::vector<int> lowest ((size_t) o.bars, 200);
        for (auto* t : te::getAudioTracks (vs.edit()))
        {
            auto* inst = Session::instrumentOf (*t);
            if (inst != nullptr && (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole"))) continue;
            for (auto* c : t->getClips())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc != clip && ! mc->isMuted())
                {
                    const double content = mc->getContentStartBeat().inBeats();
                    for (auto* n : mc->getSequence().getNotes())
                    {
                        const double at = n->getStartBeat().inBeats() + content;
                        const double endAt = at + n->getLengthBeats().inBeats();
                        for (int b = 0; b < o.bars; ++b)
                        {
                            const double b0 = clipStartBeat + b * bpb, b1 = b0 + bpb;
                            const double overlap = jmin (endAt, b1) - jmax (at, b0);
                            if (overlap <= 0.0) continue;
                            weight[(size_t) b][(size_t) (n->getNoteNumber() % 12)] += overlap;
                            if (at >= b0 - 1.0e-6 && at < b0 + 1.0) lowest[(size_t) b] = jmin (lowest[(size_t) b], n->getNoteNumber());
                        }
                    }
                }
        }
        for (int b = 0; b < o.bars; ++b)
        {
            int bestDeg = four[(size_t) ((firstBar + b) % 4)];
            double bestScore = 0.0, total = 0.0;
            for (double w : weight[(size_t) b]) total += w;
            if (total > 0.5)
                for (int d = 0; d < 7; ++d)
                {
                    double score = 0.0;
                    for (int k : { 0, 2, 4 }) score += weight[(size_t) b][(size_t) aleatorio::detail::pc (aleatorio::detail::degreePitch (0, o.root, o.minor, d + k))];
                    if (lowest[(size_t) b] < 200 && aleatorio::detail::pc (lowest[(size_t) b]) == aleatorio::detail::pc (aleatorio::detail::degreePitch (0, o.root, o.minor, d))) score += total * 0.25;
                    if (score > bestScore + 1.0e-9) { bestScore = score; bestDeg = d; }
                }
            o.progresion.push_back (bestDeg);
        }
    }
    // what the clip already has: Aleatoria varies it (same bars, rhythm, register and harmony) instead of replacing it.
    // If those notes are the last variation (nobody touched them since), the variation starts again from the clip
    // as it was before the first ALEATORIA: pressing many times gives many variations of the same idea, not a copy
    // of a copy that wears away
    std::vector<aleatorio::Nota> now;
    for (auto* n : clip->getSequence().getNotes())
    {
        const double s = n->getStartBeat().inBeats();
        if (s >= range.getStart() - 1.0e-6 && s < range.getEnd() - 1.0e-6)
            now.push_back ({ s - range.getStart(), n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
    }
    if (! fresh && ! now.empty())
    {
        const auto made = clip->state.getProperty ("racoonAleaHecho").toString();
        const auto origin = clip->state.getProperty ("racoonAleaOrigen").toString();
        if (made.isNotEmpty() && made == notesKey (now) && origin.isNotEmpty()) o.molde = deserialise (origin);
        else
        {
            o.molde = now;
            clip->state.setProperty ("racoonAleaOrigen", serialise (now), nullptr);
        }
    }
    else clip->state.removeProperty ("racoonAleaOrigen", nullptr);
    if (! o.molde.empty())
    {
        // a few notes are not a pattern yet: three notes drawn in an 8-beat clip ask for a whole part, so ALEATORIA
        // writes one for the full length of the clip
        // (a pad or a chord part holds few notes that ring for long: those are a pattern; a handful of short notes is not)
        const double perBar = (double) o.molde.size() / (double) clipBars;
        double sounding = 0.0;
        for (auto& n : o.molde) sounding += n.length;
        const double soundFrac = sounding / (clipBars * (double) o.beatsPerBar);
        if (o.molde.size() < 4 || (perBar < 1.0 && soundFrac < 0.25))
        {
            o.molde.clear();
            clip->state.removeProperty ("racoonAleaOrigen", nullptr);
        }
        else
        {
            // notes only at the start of a longer clip: that idea repeats until the end, then it is varied, so the
            // second half is not left empty
            double lastEnd = 0.0;
            for (auto& n : o.molde) lastEnd = jmax (lastEnd, n.start + jmin (n.length, (double) o.beatsPerBar));
            const int usedBars = jlimit (1, clipBars, (int) std::ceil (lastEnd / o.beatsPerBar - 1.0e-6));
            int period = 1;
            while (period < usedBars) period *= 2;
            if (period < o.bars)
            {
                const auto idea = o.molde;
                for (int k = 1; k * period < o.bars; ++k)
                    for (auto n : idea)
                    {
                        n.start += k * period * o.beatsPerBar;
                        if (n.start < o.bars * o.beatsPerBar - 1.0e-6) o.molde.push_back (n);
                    }
            }
        }
    }
    auto notes = aleatorio::generar (o);
    // longer than 16 bars: the 16 made repeat to the end of the clip
    if (clipBars > o.bars && ! notes.empty())
    {
        const auto block = notes;
        const double blockBeats = o.bars * (double) o.beatsPerBar;
        for (int k = 1; k * o.bars < clipBars; ++k)
            for (auto n : block) { n.start += k * blockBeats; notes.push_back (n); }
    }
    // the kit split in two channels: a Kick channel gets only the kick, the Groove channel everything but the kick
    if (o.rol == aleatorio::Rol::bateria && o.molde.empty())
        if (auto* t = clip->getTrack())
        {
            const auto n = t->getName().toLowerCase();
            const bool kickOnly = n.contains ("kick") || n.contains ("bombo"), noKick = n.contains ("groove");
            auto isKick = [&o] (int p) { return p == o.drums.kick || p == 35 || p == 36; };
            if (kickOnly || noKick)
                notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const aleatorio::Nota& x) { return kickOnly ? ! isKick (x.pitch) : isKick (x.pitch); }), notes.end());
        }

    // the drum sounds held with their lock keep their notes: nothing new is written on those rows
    const auto keepRows = isDrumClip() ? heldRows() : std::set<int>();
    if (! keepRows.empty())
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const aleatorio::Nota& x) { return keepRows.count (x.pitch) > 0; }), notes.end());

    auto* um = &vs.edit().getUndoManager();
    if (ownUndoStep) um->beginNewTransaction();
    selection.clearQuick();
    auto& seq = clip->getSequence();
    Array<te::MidiNote*> gone;
    for (auto* n : seq.getNotes())
        if (n->getStartBeat().inBeats() >= range.getStart() - 1.0e-6 && n->getStartBeat().inBeats() < range.getEnd() - 1.0e-6
            && keepRows.count (n->getNoteNumber()) == 0) gone.add (n);
    for (auto* n : gone) seq.removeNote (*n, um);
    int added = 0;
    for (auto& n : notes)
    {
        const double start = range.getStart() + n.start;
        if (start >= range.getEnd() - 1.0e-6) continue;
        const double len = jmin (n.length, range.getEnd() - start);
        seq.addNote (n.pitch, te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (jmax (1.0 / 32.0, len)), n.velocity, 0, um);
        ++added;
    }
    if (o.rol != aleatorio::Rol::bateria && octaveView && ! notes.empty())
    {
        // the view starts at the octave of the lowest new note, so the line is on screen from the bottom up
        int lowest = 127;
        for (auto& n : notes) lowest = jmin (lowest, n.pitch);
        octaveBase = jlimit (0, maxOctaveBase(), (lowest / 12) * 12);
    }
    refreshOctaveButtons();
    if (folded) refreshFold();
    centreOnNotes();
    repaint();
    if (vs.showHint)
        vs.showHint ((o.molde.empty() ? String::fromUTF8 ("Secuencia aleatoria de ") : String::fromUTF8 ("Variación de "))
                     + aleatorio::nombreRol (o.rol) + " (" + aleatorio::nombreEstilo (o.estilo) + ")"
                     + (o.rol == aleatorio::Rol::bateria ? String() : " en " + aleatorio::nombreTonalidad (o.root, o.minor) + " (" + where + ")")
                     + ", " + String (clipBars) + (clipBars == 1 ? String::fromUTF8 (" compás: ") : String (" compases: ")) + String (added)
                     + String::fromUTF8 (" notas. Otra vez: otra distinta. Cmd+Z la quita."));
    // remember what this ALEATORIA wrote: if the next one finds exactly this, it varies the original again
    std::vector<aleatorio::Nota> after;
    for (auto* n : clip->getSequence().getNotes())
    {
        const double s = n->getStartBeat().inBeats();
        if (s >= range.getStart() - 1.0e-6 && s < range.getEnd() - 1.0e-6)
            after.push_back ({ s - range.getStart(), n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
    }
    clip->state.setProperty ("racoonAleaHecho", notesKey (after), nullptr);
    return true;
}

String PianoRoll::scaleText() const
{
    return scaleMode.getSelectedItemIndex() > 0 ? scaleRoot.getText() + " " + scaleMode.getText() : String();
}

void PianoRoll::removeOverlapNotes()
{
    if (clip == nullptr) return;
    auto* um = &vs.edit().getUndoManager();
    um->beginNewTransaction();
    const int cut = notes::removeOverlaps (*clip, selection, um);
    if (vs.showHint)
        vs.showHint (cut == 0 ? String::fromUTF8 ("No había notas encimadas.") : (cut == 1 ? String::fromUTF8 ("Se recortó 1 nota encimada.")
                                                                         : "Se recortaron "_u + String (cut) + " notas encimadas."));
    repaint();
}

bool PianoRoll::keyPressed (const KeyPress& k)
{
    // the usual shortcuts of a MIDI editor
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) code);
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown();
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == KeyPress::escapeKey) { selection.clearQuick(); repaint(); return true; }
    if (! cmd && ! shift && ch == 'b') { setDrawMode (! drawMode); return true; }
    if (cmd && ch == 'a') { selectAll(); return true; }
    if (cmd && ch == 'd') { duplicateSelected(); return true; }
    if (cmd && ch == 'c') { copyNotes (false); return true; }
    if (cmd && ch == 'x') { copyNotes (true); return true; }
    if (cmd && ch == 'v') { pasteNotes(); return true; }
    if (cmd && ch == 'u') { quantiseSelected(); return true; }
    if (cmd && ch == '1') { stepGrid (-1); return true; }
    if (cmd && ch == '2') { stepGrid (1); return true; }
    if (cmd && ch == '3')
    {
        // straight <-> triplet of the same size
        const int i = gridChoice.getSelectedItemIndex();
        if (i >= 4 && i <= 7) setGridIndex (i + 5);
        else if (i >= 9 && i <= 12) setGridIndex (i - 5);
        return true;
    }
    if (cmd && (ch == '4' || ch == '5'))
    {
        // snap on / off
        const int i = gridChoice.getSelectedItemIndex();
        if (i == 13) setGridIndex (lastStraightGrid);
        else { lastStraightGrid = i; setGridIndex (13); }
        return true;
    }
    if (cmd && ch == 'e') { splitSelected (2); return true; }
    if (ch == 'r' && ! shift) { reverseNotes(); return true; }          // R, and Cmd+R too
    if (! cmd && ch == '0') { toggleMuteNotes(); return true; }
    if (! cmd && ch == 'q') { quantiseSelected(); return true; }
    if (code == KeyPress::pageUpKey)   { shiftOctave (1); return octaveView; }
    if (code == KeyPress::pageDownKey) { shiftOctave (-1); return octaveView; }
    if (cmd && shift && (code == KeyPress::upKey || code == KeyPress::downKey)) { duplicateSelectedPitch (code == KeyPress::upKey ? 12 : -12); return true; }
    if (code == KeyPress::upKey)   { if (shift && selection.isEmpty()) moveOctave (1); else transposeSelected (shift ? 12 : 1); return true; }
    if (code == KeyPress::downKey) { if (shift && selection.isEmpty()) moveOctave (-1); else transposeSelected (shift ? -12 : -1); return true; }
    if (code == KeyPress::leftKey || code == KeyPress::rightKey)
    {
        if (clip == nullptr || selection.isEmpty()) return false;
        const int dir = code == KeyPress::leftKey ? -1 : 1;
        if (shift) { changeLength (dir); return true; }       // Shift: shorter / longer
        const double d = dir * (gridFree() ? 0.25 : gridBeats());
        auto* um = &vs.edit().getUndoManager();
        um->beginNewTransaction();
        for (auto* n : selection)
            n->setStartAndLength (te::BeatPosition::fromBeats (jmax (0.0, n->getStartBeat().inBeats() + d)), n->getLengthBeats(), um);
        repaint();
        return true;
    }
    ignoreUnused (mods);
    return false;
}
}

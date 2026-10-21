#pragma once
#include <JuceHeader.h>
#include <set>
#include <functional>
#include "../dsp/Pattern.h"

// The hands of Hamster's step grids (INICIO and PATRÓN), the same as the piano roll's: a click on a hit selects it
// (Shift adds or takes it out), dragging on an empty spot draws a box that selects, dragging a selected hit moves the
// selection (left and right in time, up and down to another sound), Alt+drag copies, double click writes or erases.
// The pencil (B) keeps the quick way: a click writes, dragging paints. Keys: Supr, Cmd+A, Cmd+C, Cmd+V, Cmd+D,
// the arrows (left and right move a step, up and down move to the sound above or below), Esc.
namespace be::ui
{
class DrumGridEditor
{
public:
    using Cell = std::pair<int, int>;               // track, step

    // what the grid gives: where a point falls (step -1: outside the steps), the box of a cell, the steps on screen,
    // the patterns and how to save them (undoable or not), and what to do when a cell is chosen or heard
    std::function<bool (juce::Point<float>, int&, int&)> hit;
    std::function<juce::Rectangle<float> (int, int)> cellRect;
    std::function<juce::Range<int>()> visibleSteps;
    std::function<SeqData()> getData;
    std::function<void (const SeqData&, bool)> setData;
    std::function<int()> pattern;
    std::function<void (int, int)> onChoose;        // a cell was clicked (select the track and the step)
    std::function<void (int)> onHear;
    std::function<void()> repaint;

    std::set<Cell> selection;
    bool drawMode = false;

    bool isSelected (int t, int s) const { return selection.count ({ t, s }) > 0; }

    // ---- mouse
    bool mouseDown (const juce::MouseEvent& e)
    {
        int t = 0, s = -1;
        if (! hit (e.position, t, s) || s < 0) return false;
        auto d = getData();
        auto& P = d.pats[pattern()];
        if (s >= P.tracks[t].length) return true;
        const bool on = P.tracks[t].steps[s].on;
        downPos = e.position;
        downCell = { t, s };
        moved = false;
        if (drawMode)
        {
            paintTo = ! on;
            P.tracks[t].steps[s].on = paintTo;
            if (paintTo && P.tracks[t].steps[s].vel == 0) P.tracks[t].steps[s].vel = 100;
            setData (d, true);
            if (paintTo && onHear) onHear (t);
            lastPaint = s;
            mode = Mode::paint;
            if (onChoose) onChoose (t, s);
            return true;
        }
        if (on)
        {
            downShift = e.mods.isShiftDown();
            if (downShift)
            {
                if (isSelected (t, s)) { selection.erase ({ t, s }); refresh(); mode = Mode::none; return true; }
                selection.insert ({ t, s });
            }
            else if (! isSelected (t, s)) selection = { { t, s } };
            snapshot = d;
            dragSel = selection;
            copy = e.mods.isAltDown();
            lastDt = lastDs = 0;
            mode = Mode::move;
            if (onHear) onHear (t);
            if (onChoose) onChoose (t, s);
            refresh();
            return true;
        }
        selectionAtDown = e.mods.isShiftDown() ? selection : std::set<Cell>();
        selection = selectionAtDown;
        rubber = {};
        mode = Mode::rubber;
        if (onChoose) onChoose (t, s);
        refresh();
        return true;
    }

    bool mouseDrag (const juce::MouseEvent& e)
    {
        if (mode == Mode::none) return false;
        if (e.getDistanceFromDragStart() > 3) moved = true;
        if (mode == Mode::rubber)
        {
            rubber = juce::Rectangle<float> (downPos, e.position);
            selection = selectionAtDown;
            const auto d = getData();
            const auto& P = d.pats[pattern()];
            const auto vis = visibleSteps();
            for (int t = 0; t < kNumTracks; ++t)
                for (int s = vis.getStart(); s < juce::jmin (vis.getEnd(), P.tracks[t].length); ++s)
                    if (P.tracks[t].steps[s].on && cellRect (t, s).intersects (rubber)) selection.insert ({ t, s });
            refresh();
            return true;
        }
        if (mode == Mode::paint)
        {
            int t = 0, s = -1;
            if (! hit (e.position, t, s) || s < 0 || s == lastPaint) return true;
            auto d = getData();
            auto& tp = d.pats[pattern()].tracks[downCell.first];
            for (int k = juce::jmin (s, lastPaint); k <= juce::jmax (s, lastPaint); ++k)
                if (k < tp.length) { tp.steps[k].on = paintTo; if (paintTo && tp.steps[k].vel == 0) tp.steps[k].vel = 100; }
            lastPaint = s;
            setData (d, false);
            refresh();
            return true;
        }
        if (mode == Mode::move && moved)
        {
            int t = 0, s = -1;
            hit (e.position, t, s);
            if (s < 0) return true;
            const int dt = t - downCell.first, ds = s - downCell.second;
            if (dt == lastDt && ds == lastDs) return true;
            auto d = snapshot;
            selection = moveCells (d.pats[pattern()], snapshot.pats[pattern()], dragSel, dt, ds, copy);
            setData (d, false);
            if (dt != lastDt && onHear && ! selection.empty()) onHear (selection.begin()->first);
            lastDt = dt; lastDs = ds;
            refresh();
            return true;
        }
        return true;
    }

    bool mouseUp (const juce::MouseEvent&)
    {
        if (mode == Mode::none) return false;
        if (mode == Mode::move)
        {
            if (moved && (lastDt != 0 || lastDs != 0))
            {
                // one undo step for the whole move
                auto final = getData();
                setData (snapshot, false);
                setData (final, true);
            }
            else if (! downShift) selection = { downCell };
        }
        mode = Mode::none;
        rubber = {};
        refresh();
        return true;
    }

    bool mouseDoubleClick (const juce::MouseEvent& e)
    {
        int t = 0, s = -1;
        if (drawMode || ! hit (e.position, t, s) || s < 0) return false;
        auto d = getData();
        auto& tp = d.pats[pattern()].tracks[t];
        if (s >= tp.length) return true;
        auto& st = tp.steps[s];
        if (st.on) { st = Step(); selection.erase ({ t, s }); }
        else
        {
            st.on = true;
            if (st.vel == 0) st.vel = 100;
            selection = { { t, s } };
            if (onHear) onHear (t);
        }
        setData (d, true);
        refresh();
        return true;
    }

    // ---- keys
    bool keyPressed (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        const int ch = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        const bool cmd = mods.isCommandDown();
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (selection.empty()) return false;
            auto d = getData();
            for (auto& c : selection) d.pats[pattern()].tracks[c.first].steps[c.second] = Step();
            selection.clear();
            setData (d, true);
            refresh();
            return true;
        }
        if (code == juce::KeyPress::escapeKey) { if (selection.empty()) return false; selection.clear(); refresh(); return true; }
        if (! cmd && ! mods.isShiftDown() && ! mods.isAltDown() && ch == 'b') { drawMode = ! drawMode; refresh(); return true; }
        if (cmd && ch == 'a')
        {
            selection.clear();
            const auto d = getData();
            const auto& P = d.pats[pattern()];
            const auto vis = visibleSteps();
            for (int t = 0; t < kNumTracks; ++t)
                for (int s = vis.getStart(); s < juce::jmin (vis.getEnd(), P.tracks[t].length); ++s)
                    if (P.tracks[t].steps[s].on) selection.insert ({ t, s });
            refresh();
            return true;
        }
        if (cmd && (ch == 'c' || ch == 'x'))
        {
            if (selection.empty()) return true;
            const auto d = getData();
            clipboard.clear();
            for (auto& c : selection) clipboard.push_back ({ c.first, c.second, d.pats[pattern()].tracks[c.first].steps[c.second] });
            if (ch == 'x')
            {
                auto e = d;
                for (auto& c : selection) e.pats[pattern()].tracks[c.first].steps[c.second] = Step();
                selection.clear();
                setData (e, true);
                refresh();
            }
            return true;
        }
        if (cmd && ch == 'v')
        {
            if (clipboard.empty()) return true;
            int first = kMaxSteps, end = -1;
            for (auto& c : clipboard) first = juce::jmin (first, c.step);
            for (auto& c : selection) end = juce::jmax (end, c.second);
            const int at = end >= 0 ? end + 1 : first;
            auto d = getData();
            auto& P = d.pats[pattern()];
            std::set<Cell> now;
            for (auto& c : clipboard)
            {
                const int s = c.step - first + at;
                if (s < 0 || s >= P.tracks[c.track].length) continue;
                P.tracks[c.track].steps[s] = c.data;
                now.insert ({ c.track, s });
            }
            selection = now;
            setData (d, true);
            refresh();
            return true;
        }
        if (cmd && ch == 'd')
        {
            if (selection.empty()) return true;
            int lo = kMaxSteps, hi = -1;
            for (auto& c : selection) { lo = juce::jmin (lo, c.second); hi = juce::jmax (hi, c.second); }
            const int span = hi - lo + 1;
            auto d = getData();
            const auto before = d.pats[pattern()];
            auto& P = d.pats[pattern()];
            std::set<Cell> now;
            for (auto& c : selection)
            {
                const int s = c.second + span;
                if (s >= P.tracks[c.first].length) continue;
                P.tracks[c.first].steps[s] = before.tracks[c.first].steps[c.second];
                now.insert ({ c.first, s });
            }
            selection = now;
            setData (d, true);
            refresh();
            return true;
        }
        if (! cmd && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            if (selection.empty()) return false;
            const int dt = code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0;
            const int ds = code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0;
            auto d = getData();
            const auto before = d;
            selection = moveCells (d.pats[pattern()], before.pats[pattern()], selection, dt, ds, false);
            setData (d, true);
            refresh();
            return true;
        }
        return false;
    }

    void paintOverlay (juce::Graphics& g, juce::Colour colour) const
    {
        g.setColour (colour);
        const auto vis = visibleSteps();
        for (auto& c : selection)
            if (vis.contains (c.second)) g.drawRect (cellRect (c.first, c.second).expanded (1.0f), 2.2f);
        if (! rubber.isEmpty())
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRect (rubber);
            g.setColour (colour);
            g.drawRect (rubber, 1.0f);
        }
    }

    // the selected hits of `from` moved by (tracks, steps) into `to` (and kept where they were with copy); returns
    // where they landed. None leaves the 16 sounds or its track's length
    static std::set<Cell> moveCells (Pattern& to, const Pattern& from, const std::set<Cell>& cells, int dt, int ds, bool copy)
    {
        if (cells.empty()) return {};
        int minT = kNumTracks, maxT = -1;
        for (auto& c : cells) { minT = juce::jmin (minT, c.first); maxT = juce::jmax (maxT, c.first); }
        dt = juce::jlimit (-minT, kNumTracks - 1 - maxT, dt);
        int lo = -kMaxSteps, hi = kMaxSteps;
        for (auto& c : cells)
        {
            lo = juce::jmax (lo, -c.second);
            hi = juce::jmin (hi, from.tracks[c.first + dt].length - 1 - c.second);
        }
        ds = lo > hi ? 0 : juce::jlimit (lo, hi, ds);
        if (! copy) for (auto& c : cells) to.tracks[c.first].steps[c.second] = Step();
        std::set<Cell> out;
        for (auto& c : cells)
        {
            const int t = c.first + dt, s = c.second + ds;
            if (s < 0 || s >= to.tracks[t].length) continue;
            to.tracks[t].steps[s] = from.tracks[c.first].steps[c.second];
            out.insert ({ t, s });
        }
        return out;
    }

private:
    enum class Mode { none, rubber, move, paint } mode = Mode::none;
    struct Clip { int track, step; Step data; };
    std::vector<Clip> clipboard;
    std::set<Cell> selectionAtDown, dragSel;
    SeqData snapshot;
    juce::Point<float> downPos;
    juce::Rectangle<float> rubber;
    Cell downCell { 0, 0 };
    bool moved = false, copy = false, downShift = false, paintTo = true;
    int lastDt = 0, lastDs = 0, lastPaint = -1;
    void refresh() { if (repaint) repaint(); }
};
}

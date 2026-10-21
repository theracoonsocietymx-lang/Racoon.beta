#pragma once
#include <JuceHeader.h>
#include <set>
#include <vector>
#include <functional>

// The hands of a step grid (rows of steps, like a drum machine), the same as the piano roll's: a click on a hit selects
// it (Shift adds or takes it out), dragging on an empty spot draws a box that selects, dragging a selected hit moves the
// selection (left and right in time, up and down to another row), Alt+drag copies, double click writes or erases.
// The pencil (B) keeps the quick way: a click writes, dragging paints. Keys: Supr, Cmd+A, Cmd+C, Cmd+X, Cmd+V, Cmd+D,
// the arrows, Esc. Every change is one undo step (begin / end around it).
namespace racoon
{
template <typename StepT>
class GridSelection
{
public:
    using Cell = std::pair<int, int>;               // row, step

    std::function<bool (juce::Point<float>, int&, int&)> hit;      // step -1: outside the steps
    std::function<juce::Rectangle<float> (int, int)> cellRect;
    std::function<juce::Range<int>()> visibleSteps;
    std::function<int()> numRows;
    std::function<int (int)> length;                // steps of a row
    std::function<StepT (int, int)> get;
    std::function<void (int, int, const StepT&)> set;
    std::function<bool (const StepT&)> isOn;
    std::function<StepT (int, int)> newHit;         // what double click writes
    std::function<StepT()> empty;
    std::function<void()> begin, end;               // one undo step
    std::function<void (int, int)> onChoose;
    std::function<void (int)> onHear;
    std::function<void()> repaint;

    std::set<Cell> selection;
    bool drawMode = false;

    bool isSelected (int r, int s) const { return selection.count ({ r, s }) > 0; }

    bool mouseDown (const juce::MouseEvent& e)
    {
        int r = 0, s = -1;
        if (! hit (e.position, r, s) || s < 0) return false;
        if (s >= length (r)) return true;
        const bool on = isOn (get (r, s));
        downPos = e.position;
        downCell = { r, s };
        moved = false;
        if (drawMode)
        {
            paintTo = ! on;
            begin();
            set (r, s, paintTo ? newHit (r, s) : empty());
            if (paintTo && onHear) onHear (r);
            lastPaint = s;
            mode = Mode::paint;
            if (onChoose) onChoose (r, s);
            refresh();
            return true;
        }
        if (on)
        {
            downShift = e.mods.isShiftDown();
            if (downShift)
            {
                if (isSelected (r, s)) { selection.erase ({ r, s }); refresh(); mode = Mode::none; return true; }
                selection.insert ({ r, s });
            }
            else if (! isSelected (r, s)) selection = { { r, s } };
            takeSnapshot();
            dragSel = selection;
            copy = e.mods.isAltDown();
            lastDr = lastDs = 0;
            begun = false;
            mode = Mode::move;
            if (onHear) onHear (r);
            if (onChoose) onChoose (r, s);
            refresh();
            return true;
        }
        selectionAtDown = e.mods.isShiftDown() ? selection : std::set<Cell>();
        selection = selectionAtDown;
        rubber = {};
        mode = Mode::rubber;
        if (onChoose) onChoose (r, s);
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
            const auto vis = visibleSteps();
            for (int r = 0; r < numRows(); ++r)
                for (int s = vis.getStart(); s < juce::jmin (vis.getEnd(), length (r)); ++s)
                    if (isOn (get (r, s)) && cellRect (r, s).intersects (rubber)) selection.insert ({ r, s });
            refresh();
            return true;
        }
        if (mode == Mode::paint)
        {
            int r = 0, s = -1;
            if (! hit (e.position, r, s) || s < 0 || s == lastPaint) return true;
            const int row = downCell.first;
            for (int k = juce::jmin (s, lastPaint); k <= juce::jmax (s, lastPaint); ++k)
                if (k < length (row)) set (row, k, paintTo ? newHit (row, k) : empty());
            lastPaint = s;
            refresh();
            return true;
        }
        if (mode == Mode::move && moved)
        {
            int r = 0, s = -1;
            hit (e.position, r, s);
            if (s < 0) return true;
            const int dr = r - downCell.first, ds = s - downCell.second;
            if (dr == lastDr && ds == lastDs) return true;
            if (! begun) { begin(); begun = true; }
            restoreSnapshot();
            selection = moveCells (dragSel, dr, ds, copy);
            if (dr != lastDr && onHear && ! selection.empty()) onHear (selection.begin()->first);
            lastDr = dr; lastDs = ds;
            refresh();
            return true;
        }
        return true;
    }

    bool mouseUp (const juce::MouseEvent&)
    {
        if (mode == Mode::none) return false;
        if (mode == Mode::paint) end();
        if (mode == Mode::move)
        {
            if (begun) end();
            else if (! downShift) selection = { downCell };
        }
        mode = Mode::none;
        rubber = {};
        refresh();
        return true;
    }

    bool mouseDoubleClick (const juce::MouseEvent& e)
    {
        int r = 0, s = -1;
        if (drawMode || ! hit (e.position, r, s) || s < 0 || s >= length (r)) return false;
        begin();
        if (isOn (get (r, s))) { set (r, s, empty()); selection.erase ({ r, s }); }
        else
        {
            set (r, s, newHit (r, s));
            selection = { { r, s } };
            if (onHear) onHear (r);
        }
        end();
        refresh();
        return true;
    }

    bool keyPressed (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        const int ch = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        const bool cmd = mods.isCommandDown();
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        {
            if (selection.empty()) return false;
            begin();
            for (auto& c : selection) set (c.first, c.second, empty());
            end();
            selection.clear();
            refresh();
            return true;
        }
        if (code == juce::KeyPress::escapeKey) { if (selection.empty()) return false; selection.clear(); refresh(); return true; }
        if (! cmd && ! mods.isShiftDown() && ! mods.isAltDown() && ch == 'b') { drawMode = ! drawMode; refresh(); return true; }
        if (cmd && ch == 'a')
        {
            selection.clear();
            const auto vis = visibleSteps();
            for (int r = 0; r < numRows(); ++r)
                for (int s = vis.getStart(); s < juce::jmin (vis.getEnd(), length (r)); ++s)
                    if (isOn (get (r, s))) selection.insert ({ r, s });
            refresh();
            return true;
        }
        if (cmd && (ch == 'c' || ch == 'x'))
        {
            if (selection.empty()) return true;
            clipboard.clear();
            for (auto& c : selection) clipboard.push_back ({ c.first, c.second, get (c.first, c.second) });
            if (ch == 'x')
            {
                begin();
                for (auto& c : selection) set (c.first, c.second, empty());
                end();
                selection.clear();
                refresh();
            }
            return true;
        }
        if (cmd && ch == 'v')
        {
            if (clipboard.empty()) return true;
            int first = 1 << 20, last = -1;
            for (auto& c : clipboard) first = juce::jmin (first, c.step);
            for (auto& c : selection) last = juce::jmax (last, c.second);
            const int at = last >= 0 ? last + 1 : first;
            begin();
            std::set<Cell> now;
            for (auto& c : clipboard)
            {
                const int s = c.step - first + at;
                if (c.row >= numRows() || s < 0 || s >= length (c.row)) continue;
                set (c.row, s, c.data);
                now.insert ({ c.row, s });
            }
            end();
            selection = now;
            refresh();
            return true;
        }
        if (cmd && ch == 'd')
        {
            if (selection.empty()) return true;
            int lo = 1 << 20, hi = -1;
            for (auto& c : selection) { lo = juce::jmin (lo, c.second); hi = juce::jmax (hi, c.second); }
            const int span = hi - lo + 1;
            std::vector<Clip> copies;
            for (auto& c : selection) copies.push_back ({ c.first, c.second, get (c.first, c.second) });
            begin();
            std::set<Cell> now;
            for (auto& c : copies)
            {
                const int s = c.step + span;
                if (s >= length (c.row)) continue;
                set (c.row, s, c.data);
                now.insert ({ c.row, s });
            }
            end();
            selection = now;
            refresh();
            return true;
        }
        if (! cmd && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            if (selection.empty()) return false;
            const int dr = code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0;
            const int ds = code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0;
            takeSnapshot();
            begin();
            selection = moveCells (selection, dr, ds, false);
            end();
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

private:
    enum class Mode { none, rubber, move, paint } mode = Mode::none;
    struct Clip { int row, step; StepT data; };
    std::vector<Clip> clipboard;
    std::vector<std::vector<StepT>> snapshot;
    std::set<Cell> selectionAtDown, dragSel;
    juce::Point<float> downPos;
    juce::Rectangle<float> rubber;
    Cell downCell { 0, 0 };
    bool moved = false, copy = false, downShift = false, paintTo = true, begun = false;
    int lastDr = 0, lastDs = 0, lastPaint = -1;
    void refresh() { if (repaint) repaint(); }

    void takeSnapshot()
    {
        snapshot.assign ((size_t) numRows(), {});
        for (int r = 0; r < numRows(); ++r)
            for (int s = 0; s < length (r); ++s) snapshot[(size_t) r].push_back (get (r, s));
    }
    void restoreSnapshot()
    {
        for (int r = 0; r < (int) snapshot.size(); ++r)
            for (int s = 0; s < (int) snapshot[(size_t) r].size(); ++s)
                if (! sameStep (get (r, s), snapshot[(size_t) r][(size_t) s])) set (r, s, snapshot[(size_t) r][(size_t) s]);
    }
    static bool sameStep (const StepT& a, const StepT& b) { return std::memcmp (&a, &b, sizeof (StepT)) == 0; }
    StepT from (int r, int s) const { return r < (int) snapshot.size() && s < (int) snapshot[(size_t) r].size() ? snapshot[(size_t) r][(size_t) s] : get (r, s); }

    // the cells (as they are in the snapshot) moved by (rows, steps), copied with copy; none leaves the grid
    std::set<Cell> moveCells (const std::set<Cell>& cells, int dr, int ds, bool keep)
    {
        if (cells.empty()) return {};
        int minR = 1 << 20, maxR = -1;
        for (auto& c : cells) { minR = juce::jmin (minR, c.first); maxR = juce::jmax (maxR, c.first); }
        dr = juce::jlimit (-minR, numRows() - 1 - maxR, dr);
        int lo = -(1 << 20), hi = 1 << 20;
        for (auto& c : cells) { lo = juce::jmax (lo, -c.second); hi = juce::jmin (hi, length (c.first + dr) - 1 - c.second); }
        ds = lo > hi ? 0 : juce::jlimit (lo, hi, ds);
        std::vector<Clip> moving;
        for (auto& c : cells) moving.push_back ({ c.first, c.second, from (c.first, c.second) });
        if (! keep) for (auto& c : cells) set (c.first, c.second, empty());
        std::set<Cell> out;
        for (auto& c : moving)
        {
            const int r = c.row + dr, s = c.step + ds;
            if (s < 0 || s >= length (r)) continue;
            set (r, s, c.data);
            out.insert ({ r, s });
        }
        return out;
    }
};
}

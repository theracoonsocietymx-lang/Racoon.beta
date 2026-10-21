#pragma once
#include "RacoonProcessor.h"
#include "Theme.h"
#include "ProLook.h"
#include "../Shared/Maqueta.h"
#include "../Shared/Aleatorio.h"

// The bottom of every Racoon plugin, always the same: Play, tempo, bars, grid, undo / redo, the example,
// a piano roll to write a sequence, the keyboard, and "Empieza aquí".
namespace racoon
{
// a button that draws its own symbol (play / stop, undo, redo)
class SymbolButton : public juce::Button
{
public:
    enum class Symbol { play, undo, redo, octDown, octUp };
    SymbolButton (Symbol s, const juce::String& name) : juce::Button (name), symbol (s) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool playing = false;
private:
    Symbol symbol;
};

// the sequence, with the same hands as Racoon's piano roll: a click on a note selects it (Shift adds), dragging moves
// every selected note, its right edge makes them longer, Alt+drag copies, dragging on an empty spot draws a box,
// double click writes or erases, the arrows transpose (Shift: an octave), Cmd+Shift+arrow copies an octave away.
// The pencil (B) keeps the quick way: a click writes a note. The keys on the left play the note.
class PianoRollStrip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit PianoRollStrip (RacoonProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;
    // the computer keys (A S D F...) keep playing the keyboard below while the sequence has the focus
    bool keyStateChanged (bool isKeyDown) override { return keysBelow != nullptr && keysBelow->keyStateChanged (isKeyDown); }
    juce::MidiKeyboardComponent* keysBelow = nullptr;
    void setDrawMode (bool on) { drawMode = on; repaint(); }
    bool isDrawMode() const { return drawMode; }
    int numSelected() const { return (int) selected.size(); }
    // for the tests: where a note sits on screen
    juce::Point<float> posFor (double beat, int pitch) const { return { beatToX (beat), pitchToY (pitch) + rowH * 0.5f }; }
    void showPitch (int p) { topPitch = juce::jlimit ((float) getHeight() / rowH, 127.0f, (float) p + (float) getHeight() / rowH * 0.5f); repaint(); }
    // what part of the sequence is on screen (beats); the bar under the notes scrolls it, Cmd + rueda acerca
    void showFromStart();
    void zoomBy (double factor, double aroundBeat);
    double viewStart = 0.0, viewBeats = 16.0;
    void resized() override;
    void centreOnNotes();
    double grid = 0.25;                         // beats
    static constexpr int keysW = 46;
private:
    RacoonProcessor& proc;
    std::vector<SeqNote> notes;
    float rowH = 11.0f;
    float topPitch = 84.0f;                     // pitch at the top edge (scrolls)
    int dragIndex = -1, auditionNote = -1;
    enum class Drag { none, move, resize, rubber, moveSel, resizeSel } drag = Drag::none;
    std::vector<SeqNote> selected, selectedAtDown, dragNotes, dragSel, clipboard;
    bool drawMode = false, dragCopy = false, downShift = false, moved = false;
    SeqNote downNote;
    juce::Point<float> downPos;
    juce::Rectangle<float> rubberRect;
    bool isSelected (const SeqNote&) const;
    void applyDrag (double dBeats, int dPitch, double dLen);
    void changeSelected (bool keepOriginals, const std::function<void (SeqNote&)>&);
    double dragStartBeat = 0.0, noteStartAtDrag = 0.0, lengthAtDrag = 0.0;
    int pitchAtDrag = 60;
    bool changed = false;
    void timerCallback() override;
    juce::Rectangle<float> gridArea() const;
    double xToBeat (float x) const;
    float beatToX (double b) const;
    int yToPitch (float y) const;
    float pitchToY (int p) const;
    int noteAt (juce::Point<float>) const;
    double lengthBeats() const { return 4.0 * proc.patternBars(); }
    juce::ScrollBar hbar { false };
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void clampView();
    static constexpr int barH = 14;
};

// "Exportar": a click saves the sequence and its automation as a .mid; dragging it drops the file into a DAW
class ExportButton : public juce::TextButton
{
public:
    explicit ExportButton (RacoonProcessor&);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void clicked() override;
private:
    RacoonProcessor& proc;
    bool dragged = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class SequencerDock : public juce::Component, private juce::Timer
{
public:
    SequencerDock (RacoonProcessor&, const juce::StringArray& steps);
    ~SequencerDock() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;
    // the top edge is a handle: drag it up and the piano roll grows (the page above scrolls in less room)
    static constexpr int handleH = 8;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void setExtraHeight (int);                     // clamped to what the window allows; relayouts the editor
    int maxExtraHeight() const;
    PianoRollStrip roll;
    juce::MidiKeyboardComponent keys;
    SymbolButton play { SymbolButton::Symbol::play, "play" }, undo { SymbolButton::Symbol::undo, "undo" }, redo { SymbolButton::Symbol::redo, "redo" },
                 octDown { SymbolButton::Symbol::octDown, "oct-" }, octUp { SymbolButton::Symbol::octUp, "oct+" };
private:
    RacoonProcessor& proc;
    juce::StringArray steps;
    ui::GrabSlider tempo;
    juce::ComboBox bars, gridBox;
    juce::TextButton example, clear, demoSource, autoButton;
public:
    juce::TextButton randomButton;               // "Aleatoria": a random sequence in key, for the role of this plugin
    void generateRandom (juce::int64 seed = 0);
private:
    ExportButton exportButton { proc };
    maqueta::Button toSong;
    juce::Rectangle<int> stepsArea, tempoCaption, barsCaption, gridCaption;
    int baseOctave = 3;
    int extraAtDown = 0, yAtDown = 0;
    bool resizing = false;
    void timerCallback() override;
    void layoutKeys();
};
}

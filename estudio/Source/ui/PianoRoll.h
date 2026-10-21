#pragma once
#include <set>
#include "Widgets.h"
#include "Aleatorio.h"
#include <optional>

namespace racoon::ui
{
// Editor for one MIDI clip: notes, velocity lane, grid, quantize and a scale helper.
class PianoRoll : public juce::Component, private juce::ValueTree::Listener, private juce::AsyncUpdater
{
public:
    explicit PianoRoll (ViewState&);
    ~PianoRoll() override;

    void setClip (te::MidiClip*);
    te::MidiClip* getClip() const { return clip.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void selectAll();
    void deleteSelected();
    void transposeSelected (int semitones);
    void duplicateSelectedPitch (int semitones);        // Cmd+Shift+Up/Down: a copy an octave up or down, on top
    void duplicateSelected();
    void quantiseSelected();                   // with the options of the quantize panel (strength, swing...)
    void showQuantizeOptions();
    void velocityTool (int which);             // 0 crescendo, 1 decrescendo, 2/3 humanize a little/a lot, 4 louder, 5 softer, 6 even out, 7 all to the slider
    void removeOverlapNotes();
    void setScale (int root, bool minor);      // the scale helper on that key (0 = Do)
    // ALEATORIA: a random sequence in the song's key for this clip (drums, bass, chords, melody... as the instrument asks)
    // a clip with notes is varied (same bars, rhythm, register, harmony), always from what it had before the first
    // ALEATORIA, so pressing again and again never wears it down; `fresh` writes a whole new pattern instead.
    // Returns false when the clip is held (FIJAR)
    bool generateRandom (std::optional<aleatorio::Rol> role = std::nullopt, juce::int64 seed = 0, bool fresh = false, bool ownUndoStep = true);
    // FIJAR: a clip you like; ALEATORIA (here or on every channel) leaves it as it is
    static bool isHeld (const te::Clip&);
    // in a drum clip, one sound (a row: the closed hat, the snare...) can be held on its own
    std::set<int> heldRows() const;
    bool isRowHeld (int pitch) const;
    void setRowHeld (int pitch, bool);
    static void setHeld (te::Clip&, bool);
    aleatorio::Rol defaultRole() const;
    void randomKey (int& root, bool& minor, juce::String* where = nullptr) const;   // the key it would use
    aleatorio::Bateria drumMap() const;
    juce::Range<double> playingRange() const { return clipContentRange(); }
    juce::PopupMenu randomMenu();
    juce::String scaleText() const;            // "La Menor", empty without a scale (the tests)
    void centreOnNotes();
    void splitSelected (int parts);            // one note becomes `parts` equal hits (double hit, triple...)
    void stepGrid (int direction);             // -1 finer, +1 coarser
    void setGridIndex (int);
    int getGridIndex() const { return gridChoice.getSelectedItemIndex(); }
    double currentGridBeats() const { return gridBeats(); }
    // what the lane under the notes shows: -1 velocity, a CC number, or te::MidiControllerEvent::pitchWheelType
    void showLane (int controllerType);
    // Draw Mode (B): a click writes a note, dragging paints, a click on a note erases it.
    // Off (the normal mode): a click in a hole selects with a rubber band and a double click writes.
    bool drawMode = false;
    void setDrawMode (bool);
    juce::Component* laneComponent() const;
    juce::Array<te::MidiNote*>& getSelection() { return selection; }

    // used by the tests
    juce::Rectangle<int> gridBounds() const;
    juce::Point<int> pointFor (double clipBeat, int pitch) const;
    juce::String drumRowName (int pitch) const;     // the name the kit gives that note ("Bombo", "Charles cerrado"...)
    int visibleRowCount() const { return rowCount(); }
    bool isOctaveView() const { return octaveView; }
    int viewOctaveBase() const { return octaveBase; }   // the Do at the bottom of the octave on screen
    int visibleOctaves() const { return viewOctaves; }
    float rowHeight() const { return keyHeight; }
    juce::String keyLabel (int row) const;              // what the keyboard says on that row
    void shiftOctave (int octaves);                      // the octave on screen, one up or down

    // MIDI tools (on the selection, or on every note when nothing is selected)
    void moveOctave (int octaves);
    void reverseNotes();
    void invertNotes();
    void legatoNotes();
    void toggleMuteNotes();
    void changeLength (int gridSteps);
    void copyNotes (bool cut);
    void pasteNotes();
    void setClipEnd (double contentBeat, bool newUndoStep = true);               // the sequence gets longer or shorter (the clip in the song too)

private:
    ViewState& vs;
    te::MidiClip::Ptr clip;
    juce::ValueTree watched;

    class Keyboard;
    class NoteGrid;
    class VelocityLane;
    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<NoteGrid> grid;
    std::unique_ptr<VelocityLane> velocity;

    juce::Label title;
    juce::ComboBox gridChoice, scaleRoot, scaleMode;
    juce::TextButton octaveButton { "OCTAVA" }, octDown { "-" }, octUp { "+" };
    juce::TextButton quantiseOptions { "..." };
    struct RandomButton : public juce::TextButton
    {
        RandomButton() : juce::TextButton ("ALEATORIA") {}
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; } juce::TextButton::mouseDown (e); }
    } randomButton;
    juce::TextButton holdButton { "FIJAR" };
    juce::TextButton quantiseButton { "CUANTIZAR" }, doubleButton { "DUPLICAR" }, foldButton { "PLEGAR" },
                     finerButton { "-" }, coarserButton { "+" }, splitButton { "DIVIDIR" };
    GrabSlider velocitySlider;
    juce::ScrollBar vScroll { true }, hScroll { false };

    // view
    double pxPerBeat = 64.0, scrollBeat = 0.0;
    float keyHeight = 14.0f;
    float scrollPitch = 84.0f;      // pitch at the top edge
    bool folded = false, needsCentre = false;
    // melodic clips show one octave at a time with big keys; the buttons over the keyboard move it
    bool octaveView = false, preferOctave = true;
    int octaveBase = 60;
    // with more room the octave view shows more octaves (rows keep a comfortable height): 1 to 4
    int viewOctaves = 1;
    int maxOctaveBase() const { return ((128 - 12 * viewOctaves) / 12) * 12; }
    float wheelRows = 0.0f;
    double lastGridBeforeFree = -1.0;
    int lastStraightGrid = 6;
    void chooseOctave();
    void refreshOctaveButtons();
    void setViewMode (bool octave, bool fold);
    void afterVerticalMove (int semitones = 0);
    juce::Array<int> foldedPitches;
    double noteLength = 0.25;
    int newVelocity = 100;

    juce::Array<te::MidiNote*> selection;
    friend class NoteGrid;
    friend class VelocityLane;
    friend class Keyboard;

    float foldScroll = 0.0f;
    std::unique_ptr<juce::ScrollBar::Listener> scrollListener;

    double gridBeats() const;
    bool gridFree() const;
    void showNoteMenu (te::MidiNote*);
    double contentStartEditBeat() const;            // edit beat where the clip's content beat 0 sits
    juce::Range<double> clipContentRange() const;   // the playing part of the clip, in content beats
    double snapFloor (double contentBeat) const;
    double snapRound (double contentBeat) const;
    juce::Colour trackColour() const;
    bool isDrumClip() const;
    // the drum machine's own rows (Hamster's 16 tracks or Mole's lanes): their names and the notes that play them,
    // in the order the instrument shows them, so the editor paints the same grid as the instrument
    struct DrumRow { int note; juce::String name; };
    std::vector<DrumRow> drumRows;
    void readDrumRows();

    float scrollOffsetY() const;
    int gridHeight() const;
    void scrollRows (float rows);
    bool inScale (int pitch) const;
    int rowCount() const;
    int pitchForRow (int row) const;    // row 0 is the top
    int rowForPitch (int pitch) const;
    float beatToX (double clipBeat) const     { return (float) ((clipBeat - scrollBeat) * pxPerBeat); }
    double xToBeat (float x) const            { return scrollBeat + (double) x / pxPerBeat; }
    void audition (int pitch, int vel);
    void updateScrollBars();
    void refreshFold();
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override       { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeParentChanged (juce::ValueTree&) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
};
}

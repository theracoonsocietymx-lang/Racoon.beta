#pragma once
#include "Widgets.h"
#include "DragData.h"

namespace racoon::ui
{
class ArrangementView;

// ------------------------------------------------------------------ ruler: bars, loop brace, markers, playhead scrub
class Ruler : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Ruler (ViewState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    static constexpr int loopStripHeight = 14;
private:
    ViewState& vs;
    enum class Drag { none, scrub, loopNew, loopStart, loopEnd, loopMove } drag = Drag::none;
    double dragStartBeat = 0, loopStartAtDown = 0, loopEndAtDown = 0;
    void showMenu (const juce::MouseEvent&);
};

// ------------------------------------------------------------------ time ruler under the tracks: minutes and seconds
// Follows the zoom and the tempo map (a tempo change stretches the seconds, the bars stay put). Click or drag: moves the cursor.
class TimeRuler : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TimeRuler (ViewState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static constexpr int height = 20;
    // the spacing between labels, in seconds, for this many pixels per second (at least ~70 px apart)
    static double stepFor (double pxPerSecond);
    // 0:05, 1:30, 12:00; with tenths when the step is under a second (0:01.5)
    static juce::String format (double seconds, double step);
private:
    ViewState& vs;
};

// ------------------------------------------------------------------ a clip on the timeline
class ClipComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    ClipComponent (ViewState&, te::Clip::Ptr);
    ~ClipComponent() override;
    te::Clip::Ptr clip;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void updateThumbnail();

private:
    ViewState& vs;
    std::unique_ptr<te::SmartThumbnail> thumbnail;
    enum class Mode { none, move, trimStart, trimEnd, fadeIn, fadeOut, range } mode = Mode::none;
    double downStartBeat = 0, downEndBeat = 0;
    std::vector<std::pair<te::Clip::Ptr, double>> group;   // the selected clips that move together, with their start
    te::TimeDuration downFadeIn, downFadeOut;
    bool duplicated = false, dragged = false;
    Mode modeFor (juce::Point<int>) const;
    void showMenu();
    void drawWaveform (juce::Graphics&, juce::Rectangle<int>);
    void drawNotes (juce::Graphics&, juce::Rectangle<int>);
};

// ------------------------------------------------------------------ left part of a track row
class TrackHeader : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    TrackHeader (ViewState&, te::Track::Ptr, int depth);
    ~TrackHeader() override;
    te::Track::Ptr track;
    int depth;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onAutomationToggled;
private:
    ViewState& vs;
    GlyphButton mute { Glyph::none, "M", Brand::limon }, solo { Glyph::none, "S", Brand::hielo }, arm { Glyph::record, {}, Brand::rosa };
    juce::TextButton instrumentButton, foldButton, autoButton { "A" };
    FaderSlider volume;
    LevelMeter meter;
    void timerCallback() override;
    void showMenu();
    void rename();
};

// ------------------------------------------------------------------ automation lane under a track
// A channel can show several lanes; each draws one parameter (volume, pan or any parameter of any device).
// The list lives in the track state ("racoonAutoLanes") so it is saved with the project.
// "Crear secuencia" on a channel: opens its Racoon plugin (its "A la maqueta" lands at that beat), or an empty clip
void createSequence (ViewState&, te::AudioTrack&, double beat);
juce::String createSequenceLabel (ViewState&, te::AudioTrack&);
bool canCreateSequence (ViewState&, te::AudioTrack&);
bool editClipInInstrument (ViewState&, te::MidiClip&);

namespace AutoLanes
{
    juce::String keyOf (te::AutomatableParameter&);
    te::AutomatableParameter* find (te::Track&, const juce::String& key);
    juce::StringArray keys (te::Track&);
    void setKeys (te::Track&, const juce::StringArray&);
    void add (te::Track&, te::AutomatableParameter&);
    juce::String label (te::AutomatableParameter&);
    // every parameter of the channel, grouped by device
    juce::PopupMenu menu (te::Track&, std::function<void (te::AutomatableParameter&)>);
}

class AutomationLane : public juce::Component, public juce::SettableTooltipClient
{
public:
    AutomationLane (ViewState&, te::Track::Ptr, const juce::String& key);
    te::Track::Ptr track;
    te::AutomatableParameter::Ptr param;
    juce::String key;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    static constexpr int laneHeight = 70;
private:
    ViewState& vs;
    juce::TextButton pick, close { "X" }, more { "+" };
    int dragIndex = -1;
    float valueToY (float) const;
    float yToValue (float) const;
    int hitPoint (juce::Point<float>) const;
};

// ------------------------------------------------------------------ the timeline part of a track row
class TrackLane : public juce::Component,
                  public juce::DragAndDropTarget,
                  public juce::FileDragAndDropTarget,
                  public juce::SettableTooltipClient
{
public:
    TrackLane (ViewState&, te::Track::Ptr);
    te::Track::Ptr track;
    void paint (juce::Graphics&) override;
    void resized() override;
    void syncClips();
    void layoutClips();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override   { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override    { dropHighlight = false; repaint(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override  { dropHighlight = false; repaint(); }

private:
    ViewState& vs;
    juce::OwnedArray<ClipComponent> clips;
    bool dropHighlight = false;
};

// ------------------------------------------------------------------ the whole arrangement
class ArrangementView : public juce::Component,
                        public juce::DragAndDropTarget,
                        public juce::FileDragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::ChangeListener,
                        private juce::AsyncUpdater,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView (ViewState&);
    ~ArrangementView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void editChanged();                 // a new Edit was loaded
    void detach();                      // drop everything that points into the current Edit
    void rebuild();                     // track list changed
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { trackDropY = -1; repaint(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    bool moveTrackByDrag (te::Track&, int yInView);   // what a track drop does (also used by the tests)

    void showAddTrackMenu (juce::Component* target);
    void showAutomationFor (te::Track&, te::AutomatableParameter&);   // opens (or adds) the lane of that parameter
    void zoomToFit();
    bool followPlayhead = true;
    // dragging over the lanes (or the body of clips) selects a range of time on one or more channels
    void beginRange (juce::Point<int> posInThis);
    void dragRange (juce::Point<int> posInThis);
    void endRange();
    bool keyPressed (const juce::KeyPress&) override;   // arrows nudge the selection by the grid (Shift: a bar)
    TrackLane* laneAt (juce::Point<int> posInThis);

private:
    ViewState& vs;
    Ruler ruler;
    TimeRuler timeRuler;
    juce::Viewport viewport;
    // the dark space under the tracks: a "+" right under the last track, and a right click anywhere there, make a new track
    struct Content : public juce::Component
    {
        ArrangementView* owner = nullptr;
        GlyphButton plus { Glyph::plus };
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    } content;
    juce::ScrollBar hScroll { false };
    struct Row
    {
        te::Track::Ptr track;
        std::unique_ptr<TrackHeader> header;
        std::unique_ptr<TrackLane> lane;
        std::vector<std::unique_ptr<AutomationLane>> automation;
    };
    std::vector<std::unique_ptr<Row>> rows;
    juce::ValueTree watched;
    bool needsRebuild = true;
    bool clipsDirty = true;      // false while only plugin values changed (a fader drag): the clips need no resync
    int trackDropY = -1;
    int dropIndexFor (te::Track& dragged, int yInView, int* lineY) const;

    void layoutRows();
    int rowHeight (te::Track&) const;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void updateScrollBar();
    float lastPlayheadX = -1.0f;
    bool lastEasy = true;
    double songLengthBeats() const;
    int rangeAnchorRow = -1;
    double rangeAnchorBeat = 0.0;
    int rowOf (TrackLane*) const;
};

void showClipContextMenu (ViewState&, te::Clip&);
// detects the key of a clip (its notes, or its audio), says it and lights that scale in the Piano
void showKeyOf (ViewState&, te::Clip&);
void startStemSeparation (ViewState&, te::WaveAudioClip::Ptr);   // Separar en stems (asks to download the model the first time)
}

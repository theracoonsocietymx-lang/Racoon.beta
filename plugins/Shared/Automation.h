#pragma once
#include <JuceHeader.h>

// Automation inside every Racoon plugin.
// One lane per parameter, drawn over a loop of 1 to 16 bars. The lanes play with the plugin's own Play and with the
// DAW's transport, and each lane carries a MIDI CC number: an exported .mid file takes the movements with it as CC,
// and the plugin follows that CC back when the file plays into it (in Racoon or any DAW).
// Shared by the family plugins and by Badger and Hamster, so it only depends on JUCE.
namespace racoon
{
class Automation : public juce::ChangeBroadcaster, private juce::AudioProcessorParameter::Listener
{
public:
    struct Point
    {
        double beat = 0.0;     // position inside the loop
        float value = 0.0f;    // normalised 0..1
        bool operator== (const Point& o) const { return beat == o.beat && value == o.value; }
    };

    Automation (juce::AudioProcessor&, juce::UndoManager*, int defaultBars = 4);
    ~Automation() override;

    // ---------------------------------------------------------------- message thread
    bool hasLane (const juce::String& id) const;
    bool hasPoints (const juce::String& id) const;
    std::vector<Point> points (const juce::String& id) const;
    void setPoints (const juce::String& id, std::vector<Point>, bool undoable = true);
    void removeLane (const juce::String& id, bool undoable = true);
    void clearAll();
    juce::StringArray laneIds() const;                // in the order they were created
    int ccFor (const juce::String& id) const;          // -1 when the lane has no CC (more than 42 lanes)
    juce::String nameOf (const juce::String& id) const;
    juce::RangedAudioParameter* parameter (const juce::String& id) const;
    juce::StringArray automatableIds() const;          // every parameter a lane can be made for

    int bars() const { return loopBars.load(); }
    void setBars (int);                                // 1, 2, 4, 8 or 16
    double lengthBeats() const { return 4.0 * loopBars.load(); }
    double playheadBeat() const { return uiBeat.load(); }   // -1 when stopped

    // recording: while it is on and the loop plays, moving a control writes its lane
    std::atomic<bool> recording { false };
    void recordTick();                                 // the editor calls it ~30 times a second
    // registers a finished edit (from the lane editor) as one undo step
    void commitEdit (const juce::String& id, const std::vector<Point>& before, bool existedBefore);

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);            // missing tree: no lanes
    static constexpr const char* treeType = "AUTOMATION";

    // the CC movements of every lane, sampled every 1/32 note, from beat 0 to totalBeats (loops when longer)
    void appendToMidi (juce::MidiMessageSequence&, double ticksPerBeat, double totalBeats, int channel = 1) const;
    // for plugins that already build their own .mid: repeats its notes to cover the automation loop and adds the CC
    void addToMidiFile (juce::MidiFile&, double patternBeats = 0.0) const;   // 0: from its last event, rounded to a bar
    static juce::File exportFolder();                  // Música/Racoon/Patrones

    // ---------------------------------------------------------------- audio thread
    void prepare (double sampleRate);
    // once per block: incoming CC moves the parameters of its lane; while the loop runs (beat >= 0, in beats on the
    // timeline), each lane sets its parameter. A control the user is holding is left alone.
    void process (const juce::MidiBuffer& incoming, double beat, int numSamples);
    // a clock for plugins without their own: call with "playing" and the tempo, it returns the beat to pass to process()
    double clock (bool hostPlaying, double hostBeat, bool ownPlaying, double ownBpm, int numSamples);

    // value of a lane at a beat (cyclic; discrete parameters hold their value until the next point)
    static float valueAt (const std::vector<Point>&, double beat, double length, bool discrete);

private:
    struct Lane
    {
        juce::String id;
        int cc = -1;
        std::vector<Point> pts;
        juce::RangedAudioParameter* param = nullptr;
        int index = -1;
        bool discrete = false;
    };
    using Lanes = std::vector<Lane>;

    juce::AudioProcessor& proc;
    juce::UndoManager* undo = nullptr;
    Lanes lanes;                                       // message-thread master
    std::shared_ptr<const Lanes> live;                 // what the audio thread reads
    std::vector<std::shared_ptr<const Lanes>> retired; // freed on the message thread, never on the audio thread
    juce::SpinLock liveLock;
    std::atomic<int> loopBars { 4 };
    std::atomic<double> uiBeat { -1.0 };
    std::vector<juce::RangedAudioParameter*> params;
    juce::StringArray paramIds;                        // same order as params
    std::unique_ptr<std::atomic<bool>[]> touched;
    int numTouched = 1;
    int ccHold[128] {};
    double sr = 44100.0;
    // the clock
    double ownBeat = 0.0;
    bool ownWas = false;
    // recording
    std::map<int, std::pair<std::vector<Point>, bool>> recBefore;    // param index -> lane before the gesture
    std::map<int, double> recLast;

    Lane* find (const juce::String& id);
    const Lane* find (const juce::String& id) const;
    int nextFreeCC() const;
    void publish();
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    friend class AutomationEdit;
};

// what a plugin editor offers so any control can open its lane (right click > Ver automatización)
struct AutomationOpener
{
    virtual ~AutomationOpener() = default;
    virtual Automation& automation() = 0;
    virtual void openAutomation (const juce::String& paramId, bool record) = 0;
};

// a ComboBox that opens a menu on right click instead of its list
class RightClickComboBox : public juce::ComboBox
{
public:
    std::function<void()> onPopupMenu;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onPopupMenu) { onPopupMenu(); return; } juce::ComboBox::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override   { if (e.mods.isPopupMenu() && onPopupMenu) return; juce::ComboBox::mouseUp (e); }
};

// adds the automation items to a control's right-click menu (ids from 9000 up; call handleAutomationMenu with the result)
void addAutomationItems (juce::PopupMenu&, Automation&, const juce::String& paramId);
bool handleAutomationMenu (int result, juce::Component& from, const juce::String& paramId);
// "Exportar": asks where to save (Música/Racoon/Patrones by default), writes the .mid and shows it in the Finder
void exportMidiWithChooser (std::unique_ptr<juce::FileChooser>& holder, const juce::String& defaultName,
                            std::function<bool (const juce::File&)> write);
// the whole menu for controls that had none: section header, automation items and "Valor inicial"
void showParamMenu (juce::Component& from, const juce::String& paramId);
}

#pragma once
#include <JuceHeader.h>
#include <optional>

// "A la maqueta": a plugin hands its sequence to the song in Racoon.
// The plugin drops a .mid and a small note (which plugin, which channel) in a folder Racoon watches; Racoon puts
// the clip on that plugin's channel. In other DAWs the same button is dragged onto a track instead.
namespace racoon::maqueta
{
// the folder Racoon watches (RACOON_MAQUETA_DIR overrides it, for the tests)
juce::File inbox();
// true when the plugin runs inside Racoon
bool hostIsRacoon();
// kind: "notes" (the notes that drive the plugin) or "generator" (what a rhythm generator plays: Racoon puts it on a
// drum machine). Returns the request's .mid, or an empty File if it could not be written.
// clip: when the sequence came from a clip of the song (double click on it in Racoon), the clip it goes back to
juce::File send (const juce::MidiFile&, const juce::String& animal, const juce::String& trackName, const juce::String& kind = "notes",
                 const juce::String& clip = {});

// Double click on a clip in Racoon: Racoon leaves the clip's notes in inbox()/Editar for the plugin of that channel,
// and opens the plugin. The plugin loads them in its own sequencer; "A la maqueta" then sends them back to that clip.
struct EditRequest
{
    juce::MidiFile midi;
    juce::String clip;            // the clip in the song (Racoon's id)
    juce::String clipName;
    double beats = 16.0;          // how long the clip is
    double bpm = 120.0;
    // the notes, in beats from the start of the clip (only notes that start before "beats")
    struct Note { double start, length; int pitch, velocity, channel; };
    std::vector<Note> notes() const;
};
juce::File editInbox();
// while a plugin edits a clip: "<clip>.cambios" in statusInbox() says its sequence is not the clip's any more. When
// its window closes, Racoon asks whether to keep the changes: it writes "<clip>.devolver" (the plugin sends the
// sequence back to the clip) or "<clip>.olvidar" (the plugin lets go of the clip)
juce::File statusInbox();
// Racoon side: writes the request (returns false if it could not)
bool requestEdit (const juce::MidiFile&, const juce::String& animal, const juce::String& trackName, const juce::String& clip,
                  const juce::String& clipName, double beats, double bpm);
// plugin side: the request for this plugin on this channel, if there is one (it is taken: the files are deleted)
// justOpened: the plugin's window opened a moment ago (Racoon opens it right after leaving the request), so a fresh
// request for this animal is taken even if the channel's name has not reached the plugin yet
std::optional<EditRequest> takeEdit (const juce::String& animal, const juce::String& trackName, bool justOpened = false);

// where an instrument keeps the clip it is editing, so "A la maqueta" sends the sequence back there
struct Binding
{
    juce::String clip, clipName;
    double beats = 0.0;
    bool active() const { return clip.isNotEmpty(); }
    void clear() { clip.clear(); clipName.clear(); beats = 0.0; }
};

// the button: a click hands the sequence to Racoon, dragging it drops the .mid in any DAW
class Button : public juce::TextButton, private juce::Timer
{
public:
    Button();
    ~Button() override;
    std::function<juce::MidiFile()> makeMidi;            // what goes to the song
    std::function<juce::String()> animal, trackName;
    std::function<void()> afterSend;                      // e.g. stop the plugin's own Play so the song doesn't play it twice
    juce::String kind = "notes";
    // double click on a clip in Racoon: the button watches for the request and hands it to the plugin
    std::function<void (const EditRequest&)> onEdit;
    Binding* binding = nullptr;                           // the processor's: survives closing the window
    juce::String normalText;                              // "A la maqueta" (each plugin writes it its own way)
    void refreshText();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void clicked() override;
private:
    bool dragged = false;
    juce::uint32 createdAt = juce::Time::getMillisecondCounter();
    std::size_t baseline = 0;                            // the sequence as it was when the clip arrived (or last went back)
    bool haveBaseline = false;
    juce::String flaggedClip;
    void watchChanges();
    std::unique_ptr<juce::BubbleMessageComponent> tip;   // owned here: it goes away with the window
    void bubble (const juce::String&);
    void timerCallback() override;
};
}

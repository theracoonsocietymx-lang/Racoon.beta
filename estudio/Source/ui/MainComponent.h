#pragma once
#include "GenrePicker.h"
#include "LanguagePicker.h"
#include "HarmonizePanel.h"
#include "Arrangement.h"
#include "PianoRoll.h"
#include "Mixer.h"
#include "Browser.h"
#include "Panels.h"
#include "ChannelView.h"

namespace racoon::ui
{
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::ChangeListener,
                      private Session::Listener,
                      private juce::Timer
{
public:
    explicit MainComponent (Session&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    enum CommandIDs
    {
        fileNew = 0x2000, fileNewHousito, fileOpen, fileSave, fileSaveAs, fileExport, fileSaveTemplate,
        fileAudioSettings, fileScanPlugins,
        editUndo, editRedo, editDelete, editDuplicate, editSplit, editQuantise, editCopy, editCut, editPaste,
        trackAudio, trackInstrument, trackBadger, trackHamster,
        editReverse, editMuteClips, editLoopSelection, editConsolidate, editOctaveUp, editOctaveDown,
        trackGroup, trackMidiClip, viewDetail,
        playToggle, playRecord, playLoop, playToStart, playMetronome,
        viewBrowser, viewEditor, viewMixer, viewZoomIn, viewZoomOut, viewZoomFit, viewEasy, viewFollow,
        helpShortcuts, helpAbout, editMakeSong, editRandomAll, editHarmonize
    };

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    juce::ApplicationCommandManager commandManager;

    // file operations (also used by the application on quit / open)
    void openProject (const juce::File&);
    void checkSessionHealth();          // after opening: missing audio, clips longer than their audio, plugins that did not load
    void searchMissingAudio();
    juce::Array<te::MidiClip*> midiClipsInRange();     // the MIDI clips a chosen piece of the arrangement touches          // BUSCAR: finds the missing audio by name in the usual folders, by itself
    void chooseFolderForMissingAudio(); // ELEGIR CARPETA...: the same, in a folder the person points to
    void saveAs (std::function<void (bool)> done = {});
    void maybeSaveThen (std::function<void()> next);   // asks to save unsaved changes first
    void openClipEditor (te::Clip*, bool focusNotes = true);
    void reverseSelection();
    void transposeSelectedClips (int semitones);
    void consolidateSelection();
    void makeSong();
    void randomAll();
    void loadClip (te::Clip*);                 // into the Piano tab without switching to it
    void loadClipOf (te::Track&);              // that channel's clip under the playhead, or its first one
    te::Track* editorClipTrack() const;
    void clickPianoTab() { if (editorTab.onClick) editorTab.onClick(); }   // what a click on the Piano tab does (tests)
    juce::String windowTitle() const;
    std::function<void()> onTitleChanged;

    enum class Bottom { none, editor, channel, mixer };
    void showBottom (Bottom);
    Bottom getBottom() const { return bottom; }
    void setEasyMode (bool);

    // for the tests
    ViewState& getViewState()             { return vs; }
    ArrangementView& getArrangement()     { return arrangement; }
    PianoRoll& getPianoRoll()             { return pianoRoll; }
    AudioClipPanel& getAudioPanel()       { return audioPanel; }
    MixerView& getMixer()                 { return mixer; }
    Browser& getBrowser()                 { return browser; }
    ChannelView& getChannelView()         { return channel; }

private:
    Session& session;
    ViewState vs;

    // header
    class Header;
    std::unique_ptr<Header> header;
    ArrangementView arrangement;
    PianoRoll pianoRoll;
    AudioClipPanel audioPanel;
    MixerView mixer;
    ChannelView channel;
    Browser browser;

    juce::TextButton editorTab { "PIANO" }, channelTab { "CANAL" }, mixerTab { "MASTER" }, closeBottom { "OCULTAR" };
    // the icon of the instrument of the channel shown below, in the corner of the tabs: a component of its own, square,
    // so it moves with the panel and never leaves a trail or stretches when the panel is resized
    struct InstrumentIcon : public juce::Component, public juce::SettableTooltipClient
    {
        juce::String name;               // product name, "Master", "Audio", "Grupo" or empty
        juce::Colour fill;
        void paint (juce::Graphics&) override;
    } instrumentIcon;
    Bottom bottom = Bottom::editor;
    bool browserVisible = true;
    int bottomHeight = 300;
public:
    // the height of the panel below the song (what the divider sets); the tests use it too
    void setBottomHeight (int h) { bottomHeight = juce::jlimit (160, juce::jmax (160, getHeight() - 260), h); resized(); repaint(); }
private:
    struct Divider : public juce::Component
    {
        std::function<void (int)> onDrag;
        int start = 0;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { start = getY(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (start + e.getDistanceFromDragStartY()); }
    } divider;

    // first steps banner
    struct Welcome : public juce::Component
    {
        std::function<void()> onClose;
        juce::TextButton close { "ENTENDIDO" };
        Welcome() { addAndMakeVisible (close); close.onClick = [this] { if (onClose) onClose(); }; }
        void paint (juce::Graphics&) override;
        void resized() override { close.setBounds (getLocalBounds().removeFromRight (120).reduced (10, 12)); }
    } welcome;

    // status bar
    juce::String hint, transientHint;
    juce::uint32 transientUntil = 0;
    juce::TooltipWindow tooltipWindow { nullptr, 900 };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
public:
    void addInstrument (const CatalogItem&);   // new instrument track with a clip ready to write (menus, browser, tests)
private:
    void editReplaced();
    void editAboutToBeReplaced() override;
    void deleteSelection();
    void duplicateSelection();
    void copySelection (bool cut);
public:
    // "¿Qué vamos a producir?": the genres; picking one builds its block
    void showGenrePicker (bool atStart = false);
    GenrePicker* getGenrePicker() const { return genrePicker.get(); }
    // "¿En qué idioma...? / Which language...?": asked once, before the genres
    LanguagePicker* getLanguagePicker() const { return languagePicker.get(); }
    void setLanguage (bool english);
    // ARMONIZAR: the Camelot wheel; a click moves the song to that key, Armonizar keeps it, Cancelar puts it back
    void showHarmonize();
    HarmonizePanel* getHarmonizePanel() const { return harmonizePanel.get(); }              // saves it for Racoon and the plugins and repaints everything
private:
    std::unique_ptr<GenrePicker> genrePicker;
    std::unique_ptr<LanguagePicker> languagePicker;
    std::unique_ptr<HarmonizePanel> harmonizePanel;
    void closeHarmonize();
    void pasteClipboard();
    void splitSelection();
    // the bottom panel takes the colour of the channel it shows (Master keeps the window's colour)
    juce::Rectangle<int> bottomArea;
    juce::Colour bottomTint;
    juce::Colour currentBottomTint();
    te::Track* currentBottomTrack();
    void refreshInstrumentIcon();
public:
    juce::Rectangle<int> instrumentIconBounds() const { return instrumentIcon.getBounds(); }
    juce::String instrumentIconName() const { return instrumentIcon.name; }
private:
    juce::Rectangle<int> statusBounds() const { return getLocalBounds().removeFromBottom (26); }
};
}

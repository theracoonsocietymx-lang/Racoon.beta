#pragma once
#include <map>
#include "Common.h"
#include "PluginCatalog.h"

namespace racoon
{
class RacoonUIBehaviour;

struct ExportOptions
{
    juce::File destination;     // mix: the file; stems: a folder
    int format = 0;             // 0 WAV, 1 FLAC
    int bitDepth = 24;          // 16, 24, 32 (WAV float)
    double sampleRate = 48000.0;
    bool normalise = false;
    bool dither = true;         // only applies when going down to 16 bits
    bool stems = false;
    bool loopOnly = false;      // render the loop region instead of the whole song
    double tailSeconds = 2.0;   // room for reverbs and delays to ring out
};

// Owns the open Edit and every operation the interface performs on it.
// Keeping the operations here lets the test harness drive the same code the windows use.
struct Genre;

class Session : public juce::ChangeBroadcaster,
                private juce::Timer
{
public:
    Session (te::Engine&, RacoonUIBehaviour*);
    ~Session() override;

    te::Engine& engine;
    te::Edit& getEdit() const                   { return *edit; }
    te::SelectionManager& getSelection()        { return selection; }
    te::TransportControl& getTransport() const  { return edit->getTransport(); }

    // ---------------------------------------------------------------- project
    // withTemplate: the example block in that genre (Afro House when none); housito: the old 8-bar demo
    void newProject (bool withTemplate, bool housito = false, const Genre* genre = nullptr);
    bool openProject (const juce::File&);
    bool save();                                // false when untitled (caller asks for a name)
    bool saveAs (const juce::File&);
    juce::File getFile() const                  { return currentFile; }
    bool isUntitled() const                     { return untitled; }
    juce::String getProjectName() const;
    juce::File audioFolder() const;
    // Session check on open (SoundCraft's "Session Audio Health"): audio files that are not where the project says,
    // clips that ask for more audio than their file has, plugins that did not load. One line per problem.
    juce::StringArray healthReport() const;
    // looks for the missing audio files by name inside `folder` (and its subfolders); returns how many it found
    int relinkMissingAudio (const juce::File& folder);
    // BUSCAR: the missing audio files, looked for by name in the places where Racoon and its user keep sound (the
    // project's folder, Racoon's own folders, Música, Documentos, Escritorio, Descargas). The names are read here,
    // the search runs on a thread (searchFiles touches no project) and relinkFound puts the clips back on them
    struct Wanted { juce::String name, parent; };
    std::vector<Wanted> missingAudio() const;
    juce::Array<juce::File> audioSearchRoots() const;
    static std::map<juce::String, juce::File> searchFiles (const std::vector<Wanted>&, const juce::Array<juce::File>& roots,
                                                           std::function<bool()> shouldStop, int* filesLooked = nullptr);
    int relinkFound (const std::map<juce::String, juce::File>& byName);
    int countMissingAudio() const;             // where this project's own audio goes ("<proyecto> Audio"), created if missing
    bool hasUnsavedChanges() const;
    void autosaveNow();
    juce::File findRecoveryFile() const;        // newer autosave of the current project, if any

    static juce::File projectsFolder();         // ~/Music/Racoon (default place for new projects)
    static juce::File appDataFolder();          // Application Support/Racoon
    static juce::File templatesFolder();

    bool saveAsTemplate (const juce::String& name);
    juce::StringArray userTemplates() const;
    bool newFromUserTemplate (const juce::String& name);

    // ---------------------------------------------------------------- tracks
    te::AudioTrack* addAudioTrack (const juce::String& name = {});
    te::AudioTrack* addInstrumentTrack (const CatalogItem&);
    te::AudioTrack* addRacoonInstrument (const juce::String& productName); // falls back to the 4-osc synth
    // a new channel for anything in the Instruments tab: an instrument as is; Chipmunk and Mole with the
    // instrument they play (Dormouse Pads, Hamster Drums); Skunk and Chinchilla on an audio channel
    te::AudioTrack* addChannelFor (const CatalogItem&);
    // Hedgehog Mastering and other master effects go at the end of the master chain
    te::Plugin::Ptr insertOnMaster (const CatalogItem&);
    te::AudioTrack* addReturnTrack (int bus, const juce::String& name, const juce::String& fxType);
    te::FolderTrack* groupTracks (const juce::Array<te::Track*>&, const juce::String& name = "Grupo");
    void deleteTrack (te::Track&);
    te::Track* duplicateTrack (te::Track&);
    void moveTrack (te::Track&, int delta);
    void moveTrackTo (te::Track&, int index);                  // index among the tracks of the same level

    te::Plugin::Ptr insertPlugin (te::AudioTrack&, const CatalogItem&, int index = -1);
    // any item of the browser: plug-ins, native effects, the effect rack, the external effect and modulators
    // (modulators are added to the track's modulator list and return nullptr)
    te::Plugin::Ptr insertItem (te::Track&, const CatalogItem&, int index = -1);
    te::Plugin::Ptr insertPlugin (te::Track&, te::Plugin::Ptr, int index = -1);
    static te::Plugin* instrumentOf (te::AudioTrack&);
    static juce::Array<te::Plugin*> insertsOf (te::Track&);   // without volume / meter / sends
    static juce::Array<te::Plugin*> chainOf (te::Track&);     // what the channel view shows: MIDI effects, instrument, effects
    // moves a device of the chain to position `index` of chainOf(); MIDI effects stay before the instrument and audio effects after it
    void movePlugin (te::Track&, te::Plugin&, int index);
    static bool isMidiEffect (te::Plugin&);
    static bool isInstrument (te::Plugin&);   // a synth (an effect rack says it is one, but it is not)
    te::AuxSendPlugin* getSend (te::AudioTrack&, int bus, bool createIfMissing);
    juce::StringArray returnBusNames() const;

    // sidechain: feeds `source` into a plugin that accepts a sidechain (e.g. the compressor)
    bool setSidechain (te::Plugin&, te::Track* source);
    // the channel a new Sidechain listens to: one called Bombo or Kick, else the drum machine (Hamster, Mole)
    te::AudioTrack* guessKickTrack (te::Track& except) const;

    // ---------------------------------------------------------------- clips
    te::MidiClip* createMidiClip (te::AudioTrack&, te::BeatRange, const juce::String& name = {});
    te::WaveAudioClip* addAudioFile (te::AudioTrack&, const juce::File&, te::TimePosition);
    // a .mid (from a Racoon plugin's Exportar or anywhere): notes and CC into a MIDI clip; the CC moves the
    // plugin parameters that the plugin's automation lanes export
    te::MidiClip* addMidiFile (te::AudioTrack&, const juce::File&, te::TimePosition);
    // the channel for a dropped .mid: "Racoon Dormouse ....mid" gets a Dormouse channel, anything else a plain one
    te::AudioTrack* trackForMidiFile (const juce::File&);
    te::Clip* duplicateClip (te::Clip&);
    te::Clip* splitClip (te::Clip&, te::TimePosition);
    void deleteClip (te::Clip&);
    bool moveClipToTrack (te::Clip&, te::AudioTrack&);

    // ---------------------------------------------------------------- copy, paste and ranges of the song
    // a range is a piece of time on one or more channels; it can cut through clips (half a clip, two beats of four)
    void copyRange (const juce::Array<te::AudioTrack*>& tracks, double startBeat, double endBeat);
    void copyClips (const juce::Array<te::Clip*>&);
    bool hasClipboard() const { return ! clipboard.empty(); }
    double clipboardBeats() const { return clipboardLength; }
    // pastes with the first copied channel on "first" (the others keep their distance below); returns the new clips
    juce::Array<te::Clip*> pasteAt (te::AudioTrack& first, double beat);
    void deleteRange (const juce::Array<te::AudioTrack*>& tracks, double startBeat, double endBeat);
    // moves clips together: in time and, keeping their distance, to other channels
    void moveClips (const juce::Array<te::Clip*>&, double deltaBeats, int trackDelta);
    // overwrite: whatever lies under a clip that was put somewhere (pasted, duplicated, moved) is cut
    // away on that channel. Clips in "keep" are left alone (the ones being placed together)
    void clearUnder (te::ClipTrack&, te::TimeRange, const juce::Array<te::Clip*>& keep);
    void settleClips (const juce::Array<te::Clip*>&);   // clearUnder for each one, keeping the whole group
    void quantise (te::MidiClip&, double gridBeats, float strength = 1.0f);
    juce::Result consolidate (te::AudioTrack&, te::TimeRange);   // renders a region of one track to a single audio clip

    // ---------------------------------------------------------------- transport / tempo
    void togglePlay();
    void stop();
    void toggleRecord();
    void returnToStart();
    void setLooping (bool);
    void setLoopRange (te::TimeRange);
    double getBpm() const;
    void setBpm (double);
    int getTimeSigNumerator() const;
    void setTimeSigNumerator (int);
    bool isMetronomeOn() const;
    void setMetronome (bool);
    te::TimeDuration barLength() const;
    double snapBeats = 1.0;     // grid used by the timeline and the piano roll (0 = free)

    // ---------------------------------------------------------------- recording
    juce::StringArray audioInputNames();
    bool isArmed (te::AudioTrack&);
    void setArmed (te::AudioTrack&, bool armed, const juce::String& audioInput = {});
    juce::String inputOf (te::AudioTrack&);

    // ---------------------------------------------------------------- render
    juce::Result exportMix (const ExportOptions&);
    juce::Result exportStems (const ExportOptions&, juce::Array<juce::File>* written = nullptr);
    bool setFrozen (te::AudioTrack&, bool);
    te::AudioTrack* bounceToAudio (te::AudioTrack&);

    // ---------------------------------------------------------------- Racoon family
    int splitHamsterOutputs (te::AudioTrack& hamsterTrack, int numOutputs = 8);
    void buildHousitoTemplate();
    void buildBlockTemplate (const Genre&);

    // ---------------------------------------------------------------- "A la maqueta"
    // a Racoon plugin hands its sequence to the song: it leaves a .mid and a note in this folder, Racoon picks it up
    static juce::File maquetaInbox();
    juce::String pollMaqueta();                       // message thread, a few times a second; returns a hint (or empty)
    // puts a sequence from plugin "animal" on its channel; returns the hint for the status bar
    // clipId: the sequence comes back to the clip it was opened from (double click), instead of going at the end
    juce::String receiveSequence (const juce::File& mid, const juce::String& animal, const juce::String& trackName, const juce::String& kind,
                                  const juce::String& clipId = {});
    // double click on a clip: its notes go to the channel's Racoon instrument, which shows them in its own sequencer.
    // Returns the plugin to open (nullptr when the channel has no Racoon instrument to take them).
    te::Plugin* editClipInInstrument (te::MidiClip&);

    // ARMONIZAR: the song in another key. The notes of the instruments (not the drums), the loops and the keys of
    // Racoon's plugins move; begin takes a picture of how it is, preview changes it on the fly (playing), cancel
    // puts it back, commit leaves it as one Cmd+Z step
    bool songKey (int& root, bool& major) const;          // the key the song is in, when Racoon knows it
    void beginHarmonize (int oldRoot, bool oldMajor);
    void previewHarmonize (int root, bool major);
    void cancelHarmonize();
    void commitHarmonize (int root, bool major);
    bool isHarmonizing() const { return harm.active; }
    static juce::File editInbox();                    // maquetaInbox()/Editar
    // right click on a channel > Crear secuencia: the next sequence that plugin hands over lands here
    void expectSequence (te::AudioTrack&, double beat);
    te::Plugin* familyPluginOf (te::AudioTrack&) const;   // the first Racoon plugin on the channel (the one to open)
    // tells every Racoon plugin the name of its channel, so its sequence finds the way back
    void pushTrackNames();

    // callback for the interface when something changed outside of a ValueTree
    std::function<void()> onStructureChanged;

    // ---------------------------------------------------------------- mute and solo
    // On audio and instrument channels, mute and solo act on what the song plays (the clips), not on the channel:
    // a muted channel still sounds when you play its instrument live or press keys in its window.
    // Returns, groups' mute and Hamster's separate outputs keep the usual channel mute.
    bool isTrackMuted (const te::Track&) const;
    bool isTrackSoloed (const te::Track&) const;
    void setTrackMute (te::Track&, bool);
    void setTrackSolo (te::Track&, bool);
    void applyMuteSolo();                       // brings every clip in line with its channel (also on a timer)
    static bool mutesByClips (const te::Track&);
    static bool clipMutedByTrack (const te::Clip&);

    // the interface must drop every Track / Clip / Plugin pointer before an Edit is destroyed
    struct Listener { virtual ~Listener() = default; virtual void editAboutToBeReplaced() = 0; };
    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    // the clipboard as a value: tools that copy and paste on their own (the song maker) keep the user's one intact
    struct Piece { int trackOffset = 0; double startBeat = 0.0; juce::ValueTree state; };
    struct Clipboard { std::vector<Piece> pieces; double length = 0.0; int tracks = 0; };
    Clipboard getClipboard() const              { return { clipboard, clipboardLength, clipboardTracks }; }
    void setClipboard (const Clipboard& c)      { clipboard = c.pieces; clipboardLength = c.length; clipboardTracks = c.tracks; }

private:
    struct HarmClip { te::MidiClip::Ptr clip; std::vector<std::pair<te::MidiNote*, int>> notes; };
    struct Harm
    {
        bool active = false, major = false, hadRoot = false;
        int root = 0;
        juce::String keyText;
        std::vector<HarmClip> clips;
        std::vector<std::pair<te::WaveAudioClip::Ptr, float>> audio;
        std::vector<std::pair<juce::AudioProcessorParameter*, float>> params;
    } harm;
    void applyHarmonize (int root, bool major, juce::UndoManager*);

    std::vector<Piece> clipboard;
    double clipboardLength = 0.0;
    int clipboardTracks = 0;          // channels a copied range spans (0: the clipboard holds whole clips)
    std::map<te::EditItemID, double> expectedSequences;
    std::map<te::EditItemID, juce::String> pushedNames;
    std::unique_ptr<te::Edit> edit;
    te::SelectionManager selection;
    RacoonUIBehaviour* ui = nullptr;
    juce::File currentFile;
    bool untitled = true;
    juce::ListenerList<Listener> listeners;

    void adopt (std::unique_ptr<te::Edit>, const juce::File&, bool isUntitled);
    void prepareNewEdit();
    void timerCallback() override;
    struct MuteSoloTimer : juce::Timer { Session& s; explicit MuteSoloTimer (Session& x) : s (x) {} void timerCallback() override { s.applyMuteSolo(); } };
    MuteSoloTimer muteSoloTimer { *this };
    void structureChanged() { if (onStructureChanged) onStructureChanged(); }
    juce::File autosaveFileFor (const juce::File&) const;
    juce::Result renderTracks (const juce::BigInteger& tracks, const juce::File&, const ExportOptions&, te::TimeRange);
    te::TimeRange exportRange (const ExportOptions&) const;
};

// helpers shared by the interface and the tests
juce::Colour colourForTrack (te::Track&);
juce::String trackTitle (te::Track&);
juce::String timeToBarsBeats (te::Edit&, te::TimePosition);
}

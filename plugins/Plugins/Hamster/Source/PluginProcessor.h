#pragma once
#include <JuceHeader.h>
#include "Automation.h"
#include "Maqueta.h"
#include "Params.h"
#include "dsp/DrumEngine.h"
#include "dsp/Bridge.h"

namespace be { class PresetManager; }

class DrumEngineProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DrumEngineProcessor();
    ~DrumEngineProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------ model
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    // lanes inside the plugin (right click on a control > Ver automatización); they travel in the exported .mid as CC
    racoon::Automation automation { *this, &undo, 4 };
    // "A la maqueta": the current pattern (with its automation as CC) for the song in Racoon
    juce::MidiFile maquetaMidi();
    // double click on a clip in Racoon: its hits come into the current pattern (sixteenths, up to 8 bars), one track per note
    void loadFromSong (const racoon::maqueta::EditRequest&);
    racoon::maqueta::Binding songClip;
    void updateTrackProperties (const TrackProperties& p) override { const juce::ScopedLock sl (trackLock); if (p.name.has_value()) hostTrackName = *p.name; }
    juce::String trackName() const { const juce::ScopedLock sl (trackLock); return hostTrackName; }
    juce::CriticalSection trackLock;
    juce::String hostTrackName;
    be::DrumEngine engine;
    juce::MidiKeyboardState keyboardState;
    juce::ValueTree extra { "EXTRA" };

    // parameters
    juce::RangedAudioParameter* trackParamObj (int track, int local) const { return trackParams[track][local]; }
    float trackValue (int track, int local) const { return trackAtoms[track][local]->load(); }
    void setTrackValue (int track, int local, float real);
    float getParamReal (const juce::String& id) const;
    void setParamReal (const juce::String& id, float real);

    // patterns
    be::SeqData getSeqData() const;
    void setSeqData (const be::SeqData& d, bool undoable = true);
    std::atomic<int> seqVersion { 0 };   // bumps on every pattern change (UI refresh)
    void editSeq (const std::function<void (be::SeqData&)>& fn, bool undoable = true);
    juce::File exportPatternToMidi (int pattern, int bars);
    juce::File exportPatternToAudio (int pattern, int bars, int soloTrack = -1);   // offline render
    bool undoSeq();
    bool redoSeq();

    // samples
    bool loadTrackSample (int track, int slot, const juce::File& file);
    void setTrackSample (int track, int slot, be::SamplePtr s, const juce::String& libraryId = {});
    // Muestras Racoon: la pista toma un sonido de la biblioteca (sus capas de velocidad o sus alternancias)
    bool loadLibraryGroup (int track, const juce::String& grupo);
    juce::String trackLibraryGroup (int track) const;
    bool loadLibraryKit (const juce::String& kitIdOrName, bool keepPatterns = true);
    void clearTrackSample (int track, int slot);
    // Racoon > Trocear en golpes a Hamster: each file on its own track, played by note firstNote + track; the rest go quiet
    void loadSliceKit (const juce::StringArray& files, const juce::StringArray& names, const juce::StringArray& roles, int firstNote);
    // a slice kit Racoon left for this channel in Maqueta/Kits (the timer looks twice a second); true if one was taken
    bool takeSliceKit();
    const be::SampleData* trackSample (int track, int slot) const { return slots[track][slot].get(); }
    const be::SampleData* displaySample (int track) const;     // what the sample layer plays (first slot or factory)
    int numLoadedSlots (int track) const;

    // drawable LFO shapes (one per global LFO)
    std::vector<be::ShapePoint> getShape (int index) const;
    void setShape (int index, const std::vector<be::ShapePoint>& pts);

    // FX order (master rack)
    std::array<int, be::kNumFx> getFxOrder() const;
    void setFxOrder (const std::array<int, be::kNumFx>& order);

    // names
    juce::String getMacroName (int i) const;
    void setMacroName (int i, const juce::String& n);
    juce::String getTrackName (int t) const;
    void setTrackName (int t, const juce::String& n);

    // scenes
    void captureScene (int scene);
    void recallScene (int scene);
    bool sceneSet (int scene) const;
    void clearScene (int scene);

    // tracks
    void copyTrack (int from, int to);
    void resetTrack (int t, int role);
    void applyRoleDefaults (int t, int role, int model);
    void mapNotes (bool chromatic);   // 36..51 or General MIDI (unique)

    // MIDI learn
    void startMidiLearn (const juce::String& paramId) { const juce::SpinLock::ScopedLockType l (learnLock); learnTarget = paramId; }
    void clearMidiLearn (const juce::String& paramId);
    juce::String midiLearnFor (const juce::String& paramId) const;
    bool isLearning() const { return learnTarget.isNotEmpty(); }

    // A/B
    void storeAB (int slot);
    void recallAB (int slot);
    void copyAtoB();
    int currentABSlot() const { return abSlot; }

    // state / presets
    juce::ValueTree createPatchState() const;
    void loadPatchState (const juce::ValueTree& v, bool keepPatterns = false, bool keepKit = false);
    be::PresetManager& presets() { return *presetManager; }
    juce::String currentPresetName { "Init Kit" };
    std::function<void()> onPresetLoaded;

    // live mode / UI state
    int selectedTrack = 0;
    bool liveMode = false;
    std::atomic<float> cpuLoad { 0.0f };

    // bridge info for the UI
    std::atomic<float> bridgeBassHz { 0.0f };
    std::atomic<bool> bridgeBassSeen { false };
    float kickFundamental (int track) const;
    float kickDecaySeconds (int track) const;

    // pads
    void padDown (int track, float vel) { engine.triggerPad (track, vel); }
    void padUp (int track) { engine.releasePad (track); }

    static juce::String roleName (int role);
    void publishBridgeNow() { publishBridge(); }

private:
    std::unique_ptr<be::PresetManager> presetManager;
    std::atomic<float>* trackAtoms[be::kNumTracks][be::T_COUNT] {};
    juce::RangedAudioParameter* trackParams[be::kNumTracks][be::T_COUNT] {};
    struct Cache;
    std::unique_ptr<Cache> cache;
    be::EngineSnap snap;

    be::AssetSlot<be::SeqData> seqSlot;
    be::SeqData msgSeq;
    juce::SpinLock seqLock;

    be::AssetSlot<be::SampleData> slots[be::kNumTracks][be::kNumSlots];
    std::array<be::AssetSlot<be::ShapeLut>, be::kNumShapes> shapeSlots;
    std::atomic<int> fxOrder[be::kNumFx];

    be::KickBassBridge bridge;
    int lastLiveKick = 0;

    // MIDI learn
    juce::SpinLock learnLock;
    juce::String learnTarget;
    std::atomic<int> ccMap[128];
    juce::Array<juce::AudioProcessorParameter*> paramList;

    juce::ValueTree abState[2];
    int abSlot = 0;
    int currentProgram = 0;

    // message thread bookkeeping
    float lastTrackNorm[be::kNumTracks][be::T_COUNT] {};
    bool suppressMotion = false;
    int sceneApplyA = -1, sceneApplyB = -1;
    float sceneApplyFade = -1.0f;
    int timerTick = 0;
    std::atomic<bool> stopPlayRequest { false };   // CC 120/123 arrived: the timer turns Play off

    void timerCallback() override;
    void fillSnapshot (be::EngineSnap& target);
    // "Afinar el kit a": the pitched drums (kick, toms, congas, bongos) are rendered one hit at a time, their pitch
    // is measured and their Tune moved so they land on the key (the kick on the root or the fifth)
    int lastKitKey = 0, lastKitScale = 0, lastNoteMap = 0;
    juce::int64 lastMapSignature = 0;
    juce::int64 lastKitSignature = 0;
    juce::int64 kitSignature() const;
public:
    void tuneKitToKey (int keyPc, bool major);
    float measureTrackPitch (int track, double loHz = 30.0, double hiHz = 1500.0, double fromSeconds = 0.025);
    std::vector<std::pair<float, float>> measureTrackPeaks (int track, double loHz, double hiHz, double fromSeconds);   // (note, energy), loudest first   // MIDI note (with cents), or -1 when the hit has no clear pitch
private:
    void syncMotionBaseline();
    void handleMidiLearn (juce::MidiBuffer& midi);
    void applyExtraToAssets();
    void drainRecordFifo();
    void motionRecord();
    void applySceneFade();
    void publishBridge();
    void updateAuxImpulses();
    double sampleRateCache = 48000.0;
    static BusesProperties makeBuses();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEngineProcessor)
};

#pragma once
#include <JuceHeader.h>
#include "Automation.h"
#include "Maqueta.h"
#include "Params.h"
#include "dsp/SynthEngine.h"
#include "dsp/Wavetables.h"
#include "dsp/Modulators.h"
#include "dsp/Bridge.h"

namespace be { class PresetManager; }

class BassEngineProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BassEngineProcessor();
    ~BassEngineProcessor() override;

    // AudioProcessor
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
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------ public model
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    // lanes inside the plugin (right click on a control > Ver automatización); they travel in the exported .mid as CC
    racoon::Automation automation { *this, &undo, 4 };
    // "A la maqueta": the current pattern (with its automation as CC) for the song in Racoon
    juce::MidiFile maquetaMidi();
    // double click on a clip in Racoon: its notes come into the current pattern (sixteenths, up to 4 bars; notes that
    // start together become a chord on the step). While the clip is being edited the pattern plays only with PLAY,
    // so the song's own clip keeps playing the synth as usual.
    void loadFromSong (const racoon::maqueta::EditRequest&);
    racoon::maqueta::Binding songClip;
    std::atomic<bool> clipEditing { false };
    void updateTrackProperties (const TrackProperties& p) override { const juce::ScopedLock sl (trackLock); if (p.name.has_value()) hostTrackName = *p.name; }
    juce::String trackName() const { const juce::ScopedLock sl (trackLock); return hostTrackName; }
    juce::CriticalSection trackLock;
    juce::String hostTrackName;
    be::SynthEngine engine;
    juce::MidiKeyboardState keyboardState;   // on-screen / computer keyboard

    // extra (non-parameter) state, edited on the message thread
    juce::ValueTree extra { "EXTRA" };

    // drawable shapes: env 0..7, lfo 8..23, VA custom 24
    std::vector<be::ShapePoint> getShape (int index) const;
    void setShape (int index, const std::vector<be::ShapePoint>& pts);
    const be::ShapeLut* shapeLut (int index) const { return shapeSlots[(size_t) index].get(); }

    // sequencer data
    be::SeqData getSeqData() const;
    void setSeqData (const be::SeqData& d);
    void setSeqDataUndoable (const be::SeqData& d);   // the same, as one Deshacer step
    std::atomic<int> seqVersion { 0 };
    std::atomic<bool> seqPreview { false };   // PLAY button in the piano roll / simple page   // bumps on every pattern data change (UI refresh)
    juce::File exportPatternToMidi (int patternIndex);

    // assets per oscillator
    void loadUserWavetable (int osc, const juce::File& file, int cycleLength = 0);   // 0: lo detecta
    void loadUserSample (int osc, const juce::File& file);
    void setUserTable (int osc, be::WaveTablePtr t, const juce::String& name);
    void clearUserTable (int osc);
    bool hasUserTable (int osc) const;
    bool hasUserSample (int osc) const;
    juce::String userTableName (int osc) const;
    juce::String userSampleName (int osc) const;
    std::vector<float> getAdditiveHarmonics (int osc) const;
    void setAdditiveHarmonics (int osc, const std::vector<float>& h);
    const be::WaveTable* displayTable (int osc) const;     // what the osc currently plays (for drawing)
    const be::SampleData* displaySample (int osc) const;

    // tuning
    bool loadScl (const juce::File& f);
    juce::String sclName() const { return extra.getProperty ("sclName").toString(); }

    // FX order
    std::array<int, be::kNumFx> getFxOrder() const;
    void setFxOrder (const std::array<int, be::kNumFx>& order);

    // macro names
    juce::String getMacroName (int i) const;
    void setMacroName (int i, const juce::String& n);

    // MIDI learn
    void startMidiLearn (const juce::String& paramId) { const juce::SpinLock::ScopedLockType l (learnLock); learnTarget = paramId; }
    void clearMidiLearn (const juce::String& paramId);
    juce::String midiLearnFor (const juce::String& paramId) const;
    bool isLearning() const { return learnTarget.isNotEmpty(); }

    // A/B compare
    void storeAB (int slot);
    void recallAB (int slot);
    void copyAtoB();
    int currentABSlot() const { return abSlot; }

    // XY morph corners
    void captureMorphCorner (int corner);
    bool morphCornerSet (int corner) const;
    void applyMorph (float x, float y);

    // state helpers for presets
    juce::ValueTree createPatchState() const;   // parameters + extra (no UI-only bits)
    void loadPatchState (const juce::ValueTree& v, bool keepSequencer = false);
    be::PresetManager& presets() { return *presetManager; }

    // telemetry
    std::atomic<float> cpuLoad { 0.0f };
    juce::String currentPresetName { "Init" };
    std::function<void()> onPresetLoaded;

    void triggerPreview() { engine.startPreview(); }

private:
    std::unique_ptr<be::PresetManager> presetManager;

    // param cache for fast snapshot building
    struct Cache;
    std::unique_ptr<Cache> cache;
    be::PatchSnapshot snap;
    be::GlobalSettings gset;
    be::SeqData audioSeq;                // audio-thread copy
    be::SeqData msgSeq;                  // message-thread master
    juce::SpinLock seqLock;
    std::atomic<bool> seqDirty { true };

    std::array<be::AssetSlot<be::ShapeLut>, be::kNumShapes> shapeSlots;
    be::AssetSlot<be::WaveTable> userTable[be::kNumOsc], derivedTable[be::kNumOsc];
    be::AssetSlot<be::SampleData> userSample[be::kNumOsc];
    be::AssetSlot<be::WaveTable> vaCustomTable;

    struct TuningTable { double cents[128] {}; bool active = false; };
    be::AssetSlot<TuningTable> tuning;
    std::vector<double> sclCents;        // custom scale (cents per degree, last = period)

    std::atomic<int> fxOrder[be::kNumFx];

    // Kick-Bass Bridge with Drum Engine
    be::KickBassBridge bridge;
    be::BridgeKickSchedule kickSched;
    bool kickSchedValid = false;
    juce::uint32 lastLiveKick = 0;
    bool liveKickPrimed = false;
    int bridgePublishCounter = 0;
public:
    std::atomic<bool> drumBridgeSeen { false };
private:

    // MIDI learn
    juce::SpinLock learnLock;
    juce::String learnTarget;
    std::atomic<int> ccMap[128];         // cc -> parameter index (-1 none)
    juce::Array<juce::AudioProcessorParameter*> paramList;

    // A/B
    juce::ValueTree abState[2];
    int abSlot = 0;

    // rebuild tracking (message thread)
    struct DerivedKey { int engine = -1, table = -1, mode = -1; float a = -1, b = -1, c = -1; juce::int64 hash = 0; };
    DerivedKey derivedKeys[be::kNumOsc];
    int lastTuning = -1, lastTuningRoot = -1;
    float lastXY[2] { -1, -1 };
    int currentProgram = 0;

    void timerCallback() override;
    void rebuildDerivedTables (bool force);
    void rebuildTuning();
    void fillSnapshot();
    void applyExtraToAssets();
    void handleMidiLearn (juce::MidiBuffer& midi);
    double sampleRateCache = 48000.0;
    static BusesProperties makeBuses();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassEngineProcessor)
};

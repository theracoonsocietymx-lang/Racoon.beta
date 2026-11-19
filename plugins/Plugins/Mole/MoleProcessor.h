#pragma once
#include "RacoonProcessor.h"
#include "MoleEngine.h"

namespace racoon::mole
{
// names of the 16 lanes (the Hamster roles, in its note map)
const juce::StringArray& laneNames();
const juce::StringArray& rateNames();
double rateBeats (int index);
const juce::StringArray& spanNames();
double spanBeats (int index);

// Mole, Groove & Rhythm Lab: 16 lanes of up to 128 steps (euclidean, drawn by hand or answering another lane),
// polyrhythms, probability with conditions, ratchets, microtiming, swing and groove templates, groove learned from
// MIDI or audio, humanize profiles, fills, breaks and mutations, all sent as MIDI to the next instrument (Hamster,
// Badger, Squirrel...). It also slices audio: it captures a loop and plays its pieces in a new order.
class Processor : public RacoonProcessor
{
public:
    Processor();
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    void bypassStateChanged (bool, juce::MidiBuffer&) override;
    std::vector<FactoryPreset> factoryPresets() const override;
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 120.0; }
    int demoBars() const override { return 1; }
    bool dockShowsRoll() const override { return false; }      // the lanes on screen are its sequencer
    bool playsOwnMidi() const override { return true; }
    juce::String maquetaKind() const override { return "generator"; }   // what it plays goes to the song
    void handedToSong() override { setRealValue ("play", 4.0f); }       // from then on it plays the clip ("Lo que dice la maqueta")
    // a clip of the song comes into the lanes: one lane per note, drawn by hand, sixteenths (up to 8 bars)
    void loadFromSong (const racoon::maqueta::EditRequest&) override;
    // the export carries what it really plays (its own notes), not the notes that drive it
    double exportNotes (juce::MidiMessageSequence& seq, double beats, double tpq) const override
    {
        Processor fresh;
        // the copy generates its rhythm even when this one plays the song's clip
        captureMidiOutput (fresh, seq, beats, tpq, [] (RacoonProcessor& f) { f.setRealValue ("play", 0.0f); });
        return 4.0 * patternBars();
    }
    racoon::PreviewSound::Kind ownMidiKind() const override { return racoon::PreviewSound::Kind::drums; }
    bool ownSoundOn() const override { return value ("ownsound") > 0.5f; }
    // la batería con la que Mole toca sola: la sintetizada ("") o un kit de Muestras Racoon
    bool setOwnKit (const juce::String& kitId) { return ownKit.loadKit (kitId); }
    juce::String ownKitId() const { return ownKit.kitId(); }
    int ownKitNotes() const { return ownKit.notesMapped(); }
    bool producesMidi() const override { return true; }

    static juce::String laneId (int lane, const char* what) { return "l" + juce::String (lane + 1) + "_" + what; }

    Engine engine;

    // message thread actions
    void complementHamster();                       // lanes 12..16 answer the kick and the snare
    void bakeMutation();                            // writes a mutated copy of every lane as hand-drawn steps
    void copySlot (int from, int to);
    juce::MidiFile exportMidi (int bars) const;     // what Mole plays from bar 1, at the current settings
    juce::File exportMidiFile (int bars) const;     // saved in ~/Music/Racoon/Mole
    void syncEngineFromParams();                    // also used by the message thread before exporting

    // for the screen
    std::atomic<double> uiPpq { 0.0 };
    std::atomic<bool> uiRunning { false };
    std::atomic<juce::uint32> laneFlash[numLanes] {};   // counter bumped on each hit
    std::atomic<int> sliceState { 0 };              // 0 empty, 1 armed, 2 recording, 3 playing
    std::atomic<int> numSlices { 0 };
    std::atomic<int> learnedCount { 0 };

    // groove learning (tests can feed it directly)
    void learnOnset (double ppq, float velocity);
    void finishLearning();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float>* laneParam[numLanes][27] {};

    // notes in flight
    struct Off { int note = 0, channel = 1; juce::int64 at = 0; };
    static constexpr int maxOffs = 1024;
    Off offs[maxOffs];
    int numOffs = 0;
    juce::int64 clock = 0;            // samples since start
    bool soundingNote[16][128] {};

    // clock
    double internalPpq = 0.0, lastEnd = -1.0;
    bool wasRunning = false;
    int heldKeys = 0, lastKey = 60;
    bool keyStarted = false;

    // groove learning
    struct Onset { double ppq; float vel; };
    std::array<Onset, 512> onsets {};
    int numOnsets = 0;
    bool learning = false;
    float envFast = 0.0f, envSlow = 0.0f;
    int refractory = 0;

    // audio slicer
    juce::AudioBuffer<float> capture;
    int capLength = 0, capWrite = 0;
    double capStartPpq = 0.0, capBpm = 120.0, capBeats = 4.0;
    bool captureArmedEdge = false, capturePrev = false;
    std::array<int, 65> sliceStart {};
    void detectSlices();
    void processSlicer (juce::AudioBuffer<float>&, double ppqStart, double beatsPerSample, bool running);

    void scheduleOff (int note, int channel, juce::int64 at);
    void flushOffs (juce::MidiBuffer&, int offset);
};
}

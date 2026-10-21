#pragma once
#include "RacoonProcessor.h"

namespace racoon::squirrel
{
// one loaded sound, in memory (the audio thread never touches the disk)
struct Sample
{
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    juce::String name;
    juce::File file;
    int builtin = -1;                  // factory sound index, or -1 for a file
    float peak = 1.0f;
    std::vector<float> overview;       // max |x| every 256 frames, for drawing
    std::atomic<int> voiceRefs { 0 };  // voices playing it right now (it is kept alive while > 0)
    int length() const { return audio.getNumSamples(); }
    void buildOverview();
};

enum LoopMode { loopOff, loopForward, loopPingPong };

struct Zone
{
    juce::uint32 id = 0;
    std::shared_ptr<Sample> sample;
    juce::String path;                 // where the file was (kept when it is missing)
    juce::String name;
    bool missing = false;
    int lowKey = 0, highKey = 127, lowVel = 1, highVel = 127, root = 60;
    int start = 0, end = -1, loopStart = -1, loopEnd = -1;   // frames; -1 = whole sample
    int loopMode = loopOff;
    bool reverse = false, oneShot = false, normalize = false;
    int choke = 0, roundRobin = 0;     // groups, 0 = none
    float tune = 0.0f, gainDb = 0.0f, pan = 0.0f, fadeInMs = 0.0f, fadeOutMs = 0.0f;
    int len() const { return sample ? sample->length() : 0; }
    int startF() const { return juce::jlimit (0, juce::jmax (0, len() - 1), start); }
    int endF() const { return end < 0 ? len() : juce::jlimit (startF() + 1, len(), end); }
    int loopStartF() const { return loopStart < 0 ? startF() : juce::jlimit (startF(), endF() - 2, loopStart); }
    int loopEndF() const { return loopEnd < 0 ? endF() : juce::jlimit (loopStartF() + 2, endF(), loopEnd); }
};

// everything the audio thread reads; replaced as a whole (never edited in place)
struct Program
{
    std::vector<Zone> zones;
    std::vector<int> slices;           // slice start frames in zone 0 (slice mode)
    bool embed = false;
    juce::int64 serial = 0;            // changes on every edit (the audio thread compares it, not the address)
};

struct SEnv
{
    enum Stage { idle, attack, decay, sustain, release };
    Stage stage = idle;
    float level = 0.0f;
    float next (float a, float d, float s, float r, double sr);   // per sample
};

struct SVoice
{
    bool active = false, released = false, stealing = false;
    int note = -1, order = 0;
    float velocity = 0.0f, stealGain = 1.0f;
    float gPrev = -1.0f;   // filter g at the end of the last control step (the next one glides from it)
    juce::uint32 zoneId = 0;
    Sample* sample = nullptr;
    // copy of the zone values used while playing (updated when the zone is edited)
    int start = 0, end = 0, loopStart = 0, loopEnd = 0, loopMode = loopOff;
    bool oneShot = false;
    int choke = 0;
    float gain = 1.0f, panL = 1.0f, panR = 1.0f, fadeIn = 0.0f, fadeOut = 0.0f;
    double pos = 0.0, inc = 1.0, rootRatio = 1.0;
    int dir = 1;
    bool sliceVoice = false;
    double played = 0.0;
    SEnv amp, flt;
    dsp::SVF f[2];
};

// Squirrel: sampler. Chromatic zones by note and velocity, loops with crossfade, one-shot, reverse, choke and
// round-robin; slicing by transients onto pads; amp and filter envelopes, LFO; background loading.
class Processor : public RacoonProcessor
{
public:
    Processor();
    ~Processor() override;
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    void saveExtraState (juce::ValueTree&) const override;
    void loadExtraState (const juce::ValueTree&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---------------------------------------------------------------- example and first steps (the dock at the bottom)
    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 110.0; }
    int demoBars() const override { return 2; }

    // ---- editing (message thread or loader; never the audio thread)
    const Program& program() const { return *live.load(); }       // read only, message thread
    void edit (std::function<void (Program&)>);                     // copy, change, publish
    std::atomic<int> programVersion { 0 };                          // bumps on every publish (editor refresh)
    static juce::uint32 newZoneId();

    void loadFiles (const juce::Array<juce::File>&, bool addLayers);   // asynchronous
    bool loadFileNow (const juce::File&, bool addLayer);              // synchronous (tests, drag on message thread)
    void loadBuiltin (int kind);
    // SFZ (el formato de Muestras Racoon y de casi todas las bibliotecas libres): regiones con teclas, velocidades y bucles
    int loadSfz (const juce::File&, bool replace = true);           // devuelve cuántas zonas cargó; el audio llega en segundo plano
    // Muestras Racoon
    bool loadLibraryInstrument (const juce::String& id);            // un instrumento cromático (piano, bajo, cuerdas...)
    bool loadLibraryGroup (const juce::String& grupo);              // un sonido suelto en todo el teclado, con sus capas
    bool loadLibraryKit (const juce::String& kit);                  // un kit de batería: una pista por tecla desde C1
    static juce::String libraryPath (const juce::File&);            // "biblioteca:..." si el archivo está en la biblioteca
    static juce::File resolvePath (const juce::String&);
    void addBuffer (const juce::AudioBuffer<float>&, double rate, const juce::String& name, std::function<void (Zone&)> setup = {}, bool replace = true);
    int relocateMissing (const juce::File& folder);                    // returns how many were found
    void detectSlices();
    static juce::StringArray builtinNames();
    bool isLoading() const;
    void waitUntilIdle (int timeoutMs = 10000) const;
    juce::AudioFormatManager formats;

    juce::MidiKeyboardState keyboard;
    std::atomic<int> activeVoices { 0 };
    std::atomic<float> playhead[4] { -1.0f, -1.0f, -1.0f, -1.0f };   // normalised positions of up to 4 voices (display)

    static constexpr int maxVoices = 128, voicePool = 144;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static std::shared_ptr<Sample> makeBuiltin (int kind);
    std::shared_ptr<Sample> readFile (const juce::File&);

    // lock-free hand over to the audio thread
    std::atomic<Program*> live { nullptr };
    std::vector<std::pair<std::unique_ptr<Program>, juce::int64>> retired;
    std::unique_ptr<Program> current;      // owner of what `live` points to
    juce::CriticalSection editLock;
    std::atomic<bool> audioBusy { false };
    std::atomic<juce::int64> blocksDone { 0 };
    void collectGarbage();

    class Loader;
    std::unique_ptr<Loader> loader;
    friend class Loader;
    class Janitor;
    std::unique_ptr<Janitor> janitor;

    // audio thread state
    SVoice voices[voicePool];
    juce::int64 seenSerial = -1;
    int orderCounter = 0;
    int rrCounter[9] {};
    double lfoPhase = 0.0;
    float lfoHold = 0.0f;
    dsp::Rng lfoRng;
    float lastNote = 60.0f;
    float bend = 0.0f;
    bool pedal = false;
    bool pedalHeld[128] {};
    dsp::Smooth volSmooth;

    void noteOn (const Program&, int note, float vel);
    void noteOff (int note);
    void startVoice (const Zone&, int note, float vel, int sliceStart, int sliceEnd);
    void syncVoices (const Program&);
};
}

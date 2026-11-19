#pragma once
#include "engine/Common.h"
#include "fx/core/Upper.h"

namespace racoon
{
enum class PluginKind { effect, instrument, midiEffect };

struct PluginInfo
{
    juce::String animal;            // "Chinchilla"
    juce::String function;          // "Reverb" (Spanish, shown under the name)
    juce::Colour colour;            // animal colour (window surface)
    PluginKind kind = PluginKind::effect;
    int stateVersion = 1;
    double tailSeconds = 0.0;       // reported to the host (reverbs, delays)
    bool tailOnBypass = false;      // when bypassed, keep the tail ringing instead of cutting it
    bool canSleep = true;           // stop processing while the input stays silent past the tail
};

// A factory preset stores real (not normalised) parameter values; anything not listed goes back to its default.
struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
};

// Shared base for every plugin of the family:
// parameters (APVTS), bypass with a smooth crossfade and latency-aligned dry path, tail on bypass,
// sleeping on silence, NaN / denormal guard, factory and user presets, A/B compare and versioned state.
class RacoonProcessor : public juce::AudioPluginInstance
{
public:
    RacoonProcessor (const BusesProperties&, PluginInfo, juce::AudioProcessorValueTreeState::ParameterLayout);
    ~RacoonProcessor() override = default;

    const PluginInfo info;
    juce::AudioProcessorValueTreeState apvts;

    // ---------------------------------------------------------------- for subclasses
    virtual void prepareDsp (double sampleRate, int maxBlock) = 0;
    virtual void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual void resetDsp() {}
    virtual std::vector<FactoryPreset> factoryPresets() const { return {}; }
    virtual void saveExtraState (juce::ValueTree&) const {}
    virtual void loadExtraState (const juce::ValueTree&) {}
    virtual void migrateState (juce::ValueTree& /*params*/, int /*fromVersion*/) {}
    virtual void bypassStateChanged (bool /*bypassed*/, juce::MidiBuffer&) {}   // audio thread (e.g. send note-offs)
    virtual void bypassedMidi (juce::MidiBuffer&) {}                             // MIDI effects: what passes while bypassed

    // fast parameter access (audio thread safe)
    // the const char* versions never allocate (audio thread); the String versions are for the message thread
    std::atomic<float>* param (const char* id) const;
    std::atomic<float>* param (const juce::String& id) const { return param (id.toRawUTF8()); }
    float value (const char* id) const { return param (id)->load (std::memory_order_relaxed); }
    float value (const juce::String& id) const { return param (id)->load (std::memory_order_relaxed); }
    void setRealValue (const juce::String& id, float realValue);   // message thread, notifies the host

    // transport, refreshed every block
    double bpm() const noexcept          { return hostBpm; }
    double ppq() const noexcept          { return hostPpq; }
    bool isPlaying() const noexcept      { return hostPlaying; }
    double sampleRate() const noexcept   { return sr; }
    static double syncSeconds (int divisionIndex, double bpm);     // index into divisionNames()
    static const juce::StringArray& divisionNames();               // 1/1 .. 1/128 with dotted and triplet

    // meters for the editor (linear peak)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    // ---------------------------------------------------------------- presets
    juce::StringArray userPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::File presetFolder() const;
    void applyFactoryPreset (int index);
    juce::String currentPresetName;
    std::function<void()> onPresetLoaded;   // editor refresh

    // A/B compare: two complete parameter snapshots
    void selectAB (int slot);               // stores the current sound in the active slot and recalls the other
    void copyAToB();
    int activeAB = 0;

    // ---------------------------------------------------------------- AudioProcessor
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
    const juce::String getName() const override { return info.animal; }
    void fillInPluginDescription (juce::PluginDescription&) const override;   // hosted by Racoon's own plugin format
    juce::String effectId;                                                      // "racoon:<id>", set by the registry
    juce::StringArray keyParams;                                                // knobs shown on the device card in the channel view
    juce::StringPairArray shortLabels;                                          // paramID -> the short label of its knob (cards)
    bool acceptsMidi() const override;
    bool producesMidi() const override  { return info.kind == PluginKind::midiEffect; }
    bool isMidiEffect() const override  { return info.kind == PluginKind::midiEffect; }
    double getTailLengthSeconds() const override { return info.tailSeconds; }
    int getNumPrograms() override;
    int getCurrentProgram() override    { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

protected:
    juce::AudioParameterBool* bypassParam = nullptr;
    bool wantsMidiAsEffect = false;     // effects that follow MIDI notes (Mole's key tracking)
    double sr = 44100.0;
    int maxBlockSize = 512;

private:
    double hostBpm = 120.0, hostPpq = 0.0;
    bool hostPlaying = false;
    int currentProgram = 0;

    juce::AudioBuffer<float> dry, dryDelay;
    int dryDelayPos = 0;
    float bypassGain = 0.0f;           // 0 processed, 1 bypassed
    bool sleeping = false, wasFullyBypassed = false, bypassChangedFlag = false;
    juce::int64 silentSamples = 0;
    juce::MemoryBlock abSlots[2];

    juce::ValueTree buildState() const;
    void restoreState (const juce::ValueTree&);
    void updateTransport();
    void delayDry (int numSamples, int channels);
    std::vector<std::pair<std::string, std::atomic<float>*>> index;   // sorted by id, built once
    std::vector<std::pair<juce::RangedAudioParameter*, std::atomic<float>*>> hostSync;
    // a host that calls setValue() directly (Tracktion does) skips the listeners that keep the
    // APVTS atomics and the state tree up to date; this pushes those changes through
    void syncHostParameters();
};

// on / off parameter that always holds exactly 0 or 1 (AudioParameterBool keeps whatever a host writes, e.g. 0.29,
// and then a saved project comes back different from what the host set)
class ToggleParam : public juce::AudioParameterFloat
{
public:
    ToggleParam (const juce::String& id, const juce::String& name, bool def)
        : juce::AudioParameterFloat (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), def ? 1.0f : 0.0f,
                                     juce::AudioParameterFloatAttributes()
                                         .withStringFromValueFunction ([] (float v, int) { return v >= 0.5f ? juce::String::fromUTF8 ("Sí") : juce::String ("No"); })
                                         .withValueFromStringFunction ([] (const juce::String& t)
                                         {
                                             const auto x = t.trim().toLowerCase();
                                             return (x.startsWith ("s") || x == "1" || x == "on" || x.startsWith ("y")) ? 1.0f : 0.0f;
                                         })) {}
    bool isBoolean() const override { return true; }
};

// helpers to build parameter layouts with readable Spanish units
namespace P
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    juce::NormalisableRange<float> range (float lo, float hi, float centre = 0.0f, float step = 0.0f);
    void flt (Layout&, const juce::String& id, const juce::String& name, juce::NormalisableRange<float>, float def,
              const juce::String& unit = {}, int decimals = 1);
    void hz (Layout&, const juce::String& id, const juce::String& name, float lo, float hi, float def);
    void ms (Layout&, const juce::String& id, const juce::String& name, float lo, float hi, float def, float centre = 0.0f);
    void db (Layout&, const juce::String& id, const juce::String& name, float lo, float hi, float def);
    void pct (Layout&, const juce::String& id, const juce::String& name, float lo, float hi, float def);
    void choice (Layout&, const juce::String& id, const juce::String& name, const juce::StringArray&, int def);
    void toggle (Layout&, const juce::String& id, const juce::String& name, bool def);
    void integer (Layout&, const juce::String& id, const juce::String& name, int lo, int hi, int def);
}

// small DSP helpers shared by several plugins
namespace dsp
{
    inline float dbToGain (float db) { return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g)  { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g); }

    // one-pole smoother for per-sample parameter changes (no zipper noise)
    struct Smooth
    {
        float current = 0.0f, target = 0.0f, coeff = 0.0f;
        void prepare (double sampleRate, double ms = 20.0) { coeff = (float) std::exp (-1.0 / (sampleRate * ms * 0.001)); }
        void reset (float v) { current = target = v; }
        float next() { current = target + (current - target) * coeff; return current; }
    };

    // topology-preserving state variable filter (Zavalishin), stable under fast modulation
    struct SVF
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        void reset() { ic1 = ic2 = 0.0f; }
        inline void process (float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
        static float gFor (float hz, double sampleRate)
        {
            const float f = juce::jlimit (5.0f, (float) (sampleRate * 0.49), hz);
            return std::tan (juce::MathConstants<float>::pi * f / (float) sampleRate);
        }
    };

    // fractional delay line with cubic (Hermite) interpolation
    struct DelayLine
    {
        std::vector<float> buf;
        int write = 0, mask = 0;
        void prepare (int maxSamples)
        {
            int n = 1; while (n < maxSamples + 4) n <<= 1;
            buf.assign ((size_t) n, 0.0f); mask = n - 1; write = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void push (float x) { buf[(size_t) write] = x; write = (write + 1) & mask; }
        float tap (float delaySamples) const
        {
            const float d = juce::jlimit (2.0f, (float) (mask - 3), delaySamples);
            const int di = (int) d; const float f = d - (float) di;
            auto at = [this] (int k) { return buf[(size_t) ((write - k) & mask)]; };
            const float xm1 = at (di - 1 + 0), x0 = at (di), x1 = at (di + 1), x2 = at (di + 2);
            // Hermite between x0 (delay di) and x1 (delay di+1)
            const float c0 = x0, c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2,
                        c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + c0;
        }
        float tapInt (int d) const { return buf[(size_t) ((write - juce::jlimit (1, mask, d)) & mask)]; }
    };

    // simple DC blocker
    struct DCBlock
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
        void prepare (double sampleRate) { r = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 10.0 / sampleRate); }
        void reset() { x1 = y1 = 0.0f; }
        float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    };

    // envelope follower (attack / release in ms)
    struct Follower
    {
        float env = 0.0f, a = 0.0f, r = 0.0f;
        void set (double sampleRate, float attackMs, float releaseMs)
        {
            a = (float) std::exp (-1.0 / (sampleRate * juce::jmax (0.01f, attackMs) * 0.001));
            r = (float) std::exp (-1.0 / (sampleRate * juce::jmax (0.1f, releaseMs) * 0.001));
        }
        float process (float x) { const float c = x > env ? a : r; env = x + (env - x) * c; return env; }
    };

    // deterministic random for reproducible patterns (seeded)
    struct Rng
    {
        juce::uint32 s = 1;
        void seed (juce::uint32 v) { s = v == 0 ? 1u : v; }
        juce::uint32 nextInt() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float next01() { return (float) (nextInt() & 0xffffff) / 16777216.0f; }
        float nextBipolar() { return next01() * 2.0f - 1.0f; }
    };
}
}

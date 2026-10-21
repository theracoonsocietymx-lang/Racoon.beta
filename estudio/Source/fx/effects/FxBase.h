#pragma once
#include "fx/core/RacoonProcessor.h"
#include "fx/core/RacoonEditor.h"
#include "fx/core/RacoonDsp.h"

// Small base for Racoon's native effects: the processor declares its parameters, its controls (sections)
// and what the screen shows; the window, presets, A/B and PRINCIPIANTE / AVANZADO come from RacoonEditor.
namespace racoon::fx
{
class FxBase : public RacoonProcessor
{
public:
    FxBase (const juce::String& name, const juce::String& category, juce::AudioProcessorValueTreeState::ParameterLayout,
            double tailSeconds = 0.0, bool sidechain = false, bool tailOnBypass = false, bool canSleep = true);

    // ---- what each effect writes
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual void prepare (double sampleRate, int maxBlock) { juce::ignoreUnused (sampleRate, maxBlock); }
    virtual void reset() {}
    virtual EditorSpec editorSpec() const = 0;
    virtual std::unique_ptr<juce::Component> makeDisplay();       // default: scope + meters
    virtual juce::String displayText() const { return {}; }       // a line on the screen (state, note, time...)
    std::vector<FactoryPreset> factoryPresets() const override { return { FactoryPreset { "Inicial", {} } }; }   // all defaults

    // ---- RacoonProcessor
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---- for the screen (written by the audio thread)
    static constexpr int scopeSize = 1024;
    float scope[scopeSize] {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> grDb { 0.0f };       // gain reduction, for dynamics
    bool showsGR = false;
    int scopeDecimate = 8;

protected:
    static BusesProperties busesFor (bool sidechain);

    // LFO helpers: frequency in Hz (free or a tempo division), and phase locked to the song position while playing
    double lfoHz (bool sync, int division, double freeHz) const { return sync ? 1.0 / syncSeconds (division, bpm()) : freeHz; }
    void lockPhase (bool sync, int division, double& phase) const
    {
        if (! sync || ! isPlaying()) return;
        const double beats = syncSeconds (division, bpm()) * juce::jlimit (20.0, 400.0, bpm()) / 60.0;
        const double p = ppq() / beats;
        phase = p - std::floor (p);
    }
    juce::AudioBuffer<float> sidechainCopy;   // filled when the effect has a sidechain bus
    bool hasSidechain = false;
    const float* sidechain (int ch) const;    // null when nothing is connected
    bool sidechainActive = false;
};

// helpers shared by the effects
namespace dsp2
{
    // RBJ biquad, transposed direct form II, per channel state
    // When set() gives new coefficients, each channel glides to them over the next 64 samples instead of jumping at
    // the block edge, so a swept or automated filter does not step (zipper).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // the target (what set() designed)
        float z1[2] {}, z2[2] {};
        float cur[2][5] { { 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 } }, inc[2][5] {};
        int left[2] {};
        bool fresh = true;
        static constexpr int rampLen = 64;
        inline float process (float x, int c)
        {
            float* k = cur[c];
            if (left[c] > 0)
            {
                if (--left[c] == 0) { k[0] = b0; k[1] = b1; k[2] = b2; k[3] = a1; k[4] = a2; }
                else for (int i = 0; i < 5; ++i) k[i] += inc[c][i];
            }
            const float y = k[0] * x + z1[c];
            z1[c] = k[1] * x - k[3] * y + z2[c];
            z2[c] = k[2] * x - k[4] * y;
            return y;
        }
        void reset()
        {
            z1[0] = z1[1] = z2[0] = z2[1] = 0.0f;
            snap();
        }
        // jump to the target now (after a reset, or the first time)
        void snap()
        {
            for (int c = 0; c < 2; ++c) { cur[c][0] = b0; cur[c][1] = b1; cur[c][2] = b2; cur[c][3] = a1; cur[c][4] = a2; left[c] = 0; }
        }
        void startRamp()
        {
            if (fresh) { fresh = false; snap(); return; }
            const float t[5] { b0, b1, b2, a1, a2 };
            for (int c = 0; c < 2; ++c)
            {
                bool same = true;
                for (int i = 0; i < 5; ++i) if (std::abs (t[i] - cur[c][i]) > 1.0e-9f) same = false;
                if (same) { left[c] = 0; continue; }
                // a big jump of the poles (a preset, a random value) is taken at once like before: gliding a transposed
                // form II across very different filters can pump energy. Knob moves and automation are small steps and glide.
                if (std::abs (t[3] - cur[c][3]) > 0.15f || std::abs (t[4] - cur[c][4]) > 0.15f)
                {
                    for (int i = 0; i < 5; ++i) cur[c][i] = t[i];
                    left[c] = 0;
                    continue;
                }
                for (int i = 0; i < 5; ++i) inc[c][i] = (t[i] - cur[c][i]) / (float) rampLen;
                left[c] = rampLen;
            }
        }
        enum Type { lowpass, highpass, bandpass, notch, peak, lowshelf, highshelf, allpass };
        void set (Type, double sr, double freq, double q, double gainDb = 0.0);
        double magnitudeDb (double freq, double sr) const;   // for response curves on the screen
    };

    // the screen of a compressor: the transfer curve (in dB to out dB) with the live input on it, and the last
    // seconds of input level, output level and gain reduction scrolling by. curve: input dB -> output dB (static)
    struct CompressorView
    {
        std::function<float (float inDb)> curve;
        std::function<float()> threshold;
        std::function<juce::String()> caption;   // "4:1  ·  rodilla 6 dB"
    };
    std::unique_ptr<juce::Component> makeCompressorDisplay (FxBase&, CompressorView);

    // the screen of a saturator: its curve (input -1..1 to output) with the level coming in, and the output wave
    std::unique_ptr<juce::Component> makeTransferDisplay (FxBase&, std::function<float (float)> curve, std::function<juce::String()> caption);

    // the screen of a delay: each echo as a bar on a time line, left above and right below, fading by the feedback
    struct EchoView
    {
        double leftSeconds = 0.25, rightSeconds = 0.25;
        float feedback = 0.4f, mix = 0.3f;
        bool pingPong = false;
        juce::String caption;
    };
    std::unique_ptr<juce::Component> makeEchoDisplay (FxBase&, std::function<EchoView()>);

    // the screen of a reverb: the shape of the tail (predelay, early reflections, decay to -60 dB) and the level out
    struct DecayView
    {
        double predelay = 0.02, decay = 2.0;   // seconds
        float early = 0.5f, damping = 0.4f, mix = 0.3f;
        bool freeze = false;
        juce::String caption;
    };
    std::unique_ptr<juce::Component> makeDecayDisplay (FxBase&, std::function<DecayView()>);

    // a screen that draws a frequency response (dB as a function of Hz), computed on the message thread
    std::unique_ptr<juce::Component> makeResponseDisplay (FxBase&, std::function<float (double hz)> responseDb, float rangeDb = 18.0f);

    // simple LFO with the usual shapes; phase in [0,1)
    float lfoShape (int shape, double phase, float& holdValue, float& fromValue, juce::uint32& rngState, bool newCycle);
    juce::StringArray lfoShapeNames();

    // one-pole smoother coefficient for a time in ms
    inline float coeffMs (double sr, double ms) { return (float) std::exp (-1.0 / (juce::jmax (0.01, ms) * 0.001 * sr)); }

    inline float softClip (float x) { return std::tanh (x); }

    // 2x oversampling around a per-sample function (IIR halfband, very low latency). Dry and wet mixed inside the
    // function stay aligned because both go through the same filters.
    struct Oversampler2
    {
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        int maxBlock = 0, stages = 1;
        // stages: 1 = 2x, 2 = 4x (two halfband stages in cascade)
        void prepare (int maxSamples, int numStages = 1)
        {
            maxBlock = juce::jmax (512, maxSamples);
            stages = juce::jlimit (1, 3, numStages);
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            os->initProcessing ((size_t) maxBlock);
        }
        int factor() const { return 1 << stages; }
        void reset() { if (os) os->reset(); }
        template <typename F> void process (juce::AudioBuffer<float>& b, F&& fn)   // fn (float x, int channel) -> float
        {
            if (! os) prepare (b.getNumSamples());
            const int ch = juce::jmin (2, b.getNumChannels());
            juce::dsp::AudioBlock<float> all (b);
            auto block = all.getSubsetChannelBlock (0, (size_t) ch);
            for (size_t start = 0; start < block.getNumSamples(); start += (size_t) maxBlock)
            {
                auto part = block.getSubBlock (start, juce::jmin ((size_t) maxBlock, block.getNumSamples() - start));
                auto up = os->processSamplesUp (part);
                const int n = (int) up.getNumSamples();
                for (int c = 0; c < ch; ++c)
                {
                    auto* d = up.getChannelPointer ((size_t) c);
                    for (int i = 0; i < n; ++i) d[i] = fn (d[i], c);
                }
                os->processSamplesDown (part);
            }
        }
        // the same, one frame at a time: fn (float* frame, int channels) changes all the channels of one sample together
        // (for detectors that must see left and right at once)
        template <typename F> void processFrames (juce::AudioBuffer<float>& b, F&& fn)
        {
            if (! os) prepare (b.getNumSamples());
            const int ch = juce::jmin (2, b.getNumChannels());
            juce::dsp::AudioBlock<float> all (b);
            auto block = all.getSubsetChannelBlock (0, (size_t) ch);
            for (size_t start = 0; start < block.getNumSamples(); start += (size_t) maxBlock)
            {
                auto part = block.getSubBlock (start, juce::jmin ((size_t) maxBlock, block.getNumSamples() - start));
                auto up = os->processSamplesUp (part);
                const int n = (int) up.getNumSamples();
                float* d[2] = { up.getChannelPointer (0), ch > 1 ? up.getChannelPointer (1) : nullptr };
                for (int i = 0; i < n; ++i)
                {
                    float frame[2] = { d[0][i], ch > 1 ? d[1][i] : 0.0f };
                    fn (frame, ch);
                    d[0][i] = frame[0];
                    if (ch > 1) d[1][i] = frame[1];
                }
                os->processSamplesDown (part);
            }
        }
    };

    // linear interpolated read from a circular buffer
    inline float readFrac (const float* buf, int size, double pos)
    {
        while (pos < 0) pos += size;
        const int i = (int) pos;
        const float f = (float) (pos - i);
        const float a = buf[i % size], b = buf[(i + 1) % size];
        return a + (b - a) * f;
    }
}
}

#pragma once
#include "Widgets.h"
#include "../dsp/Modulators.h"

namespace be::ui
{
// editable multi-point shape (drawn LFOs)
class ShapeEditor : public juce::Component
{
public:
    void setPoints (const std::vector<ShapePoint>& p) { pts = p; repaint(); }
    const std::vector<ShapePoint>& getPoints() const { return pts; }
    std::function<void (const std::vector<ShapePoint>&)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    float playhead = -1.0f;
    juce::Colour colour = C::amber;
private:
    std::vector<ShapePoint> pts;
    int dragIndex = -1;
    bool dragCurve = false;
    float curveStart = 0.0f;
    juce::Point<float> toScreen (const ShapePoint& p) const;
    ShapePoint fromScreen (juce::Point<float> s) const;
    int hit (juce::Point<float> s) const;
};

// global LFO view (wave or drawn editor)
class LfoView : public juce::Component, public Animated
{
public:
    explicit LfoView (Ctx& c);
    ~LfoView() override;
    void setLfo (int l);
    void paint (juce::Graphics&) override;
    void resized() override;
    void animate() override;
private:
    Ctx& ctx;
    int lfo = 0, lastWave = -1;
    ShapeEditor editor;
    float live = 0.0f;
};

class ScopeView : public juce::Component, public Animated
{
public:
    ScopeView (Ctx& c, bool small = false);
    ~ScopeView() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
private:
    Ctx& ctx;
    bool small;
    std::vector<float> buf;
};

class SpectrumView : public juce::Component, public Animated
{
public:
    explicit SpectrumView (Ctx& c);
    ~SpectrumView() override;
    void paint (juce::Graphics&) override;
    void animate() override;
    const std::vector<float>& magnitudes() const { return smooth; }
    float binHz() const { return (float) (sr / (double) fftSize); }
private:
    Ctx& ctx;
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> work, mag, smooth;
    double sr = 48000.0;
};

// stereo master meter
class MasterMeter : public juce::Component, public Animated
{
public:
    explicit MasterMeter (Ctx& c, bool horizontal = false);
    ~MasterMeter() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
private:
    Ctx& ctx;
    bool horizontal;
    float holdL = 0, holdR = 0;
};

// XY performance pad (g_xyx / g_xyy)
class XYPad : public juce::Component, public Animated
{
public:
    explicit XYPad (Ctx& c);
    ~XYPad() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void animate() override;
private:
    Ctx& ctx;
    float lx = -1, ly = -1;
};

// sample waveform with region, slices and draggable start / end
class WaveformView : public juce::Component, public Animated, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (Ctx& c);
    ~WaveformView() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void animate() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    int slot = 0;
private:
    Ctx& ctx;
    const SampleData* lastSample = nullptr;
    juce::Path cacheL;
    juce::String lastKey;
    int dragWhat = 0;   // 1 start, 2 end
    bool hover = false;
};

// FM algorithm diagram
class FmAlgoView : public juce::Component, public Animated
{
public:
    explicit FmAlgoView (Ctx& c);
    ~FmAlgoView() override;
    void paint (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    int last = -1;
};

// amp / pitch / filter envelope sketch for the selected track
class EnvSketch : public juce::Component, public Animated
{
public:
    explicit EnvSketch (Ctx& c);
    ~EnvSketch() override;
    void paint (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    juce::int64 last = 0;
};

// filter response for the selected track
class TrackFilterCurve : public juce::Component, public Animated
{
public:
    explicit TrackFilterCurve (Ctx& c);
    ~TrackFilterCurve() override;
    void paint (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    juce::int64 last = 0;
};

// circuit morph position 808 > 909 > PCM > FM
class CircuitView : public juce::Component, public Animated
{
public:
    explicit CircuitView (Ctx& c);
    ~CircuitView() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void animate() override;
private:
    Ctx& ctx;
    float last = -1.0f;
    bool lastOn = false;
};

// kick / bass low band timeline for collision analysis
class CollisionView : public juce::Component, public Animated
{
public:
    explicit CollisionView (Ctx& c);
    ~CollisionView() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
private:
    Ctx& ctx;
    std::vector<float> k, b;
};

// groove template bars
class GrooveView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    Groove groove;
};
} // namespace be::ui

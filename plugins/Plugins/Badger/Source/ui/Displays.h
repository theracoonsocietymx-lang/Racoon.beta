#pragma once
#include "Widgets.h"
#include "../dsp/Oscillator.h"
#include "../dsp/Modulators.h"

namespace be::ui
{
// ------------------------------------------------------------------ oscillator waveform preview (renders with a real Oscillator)
class WaveDisplay : public juce::Component, public Animated
{
public:
    WaveDisplay (Ctx& c, int osc);
    ~WaveDisplay() override;
    void paint (juce::Graphics&) override;
    void animate() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClick;
private:
    Ctx& ctx;
    int osc;
    be::Oscillator renderer;
    std::vector<float> cycle;
    juce::int64 lastHash = 0;
    void render();
    juce::int64 stateHash() const;
};

// ------------------------------------------------------------------ editable multi-point shape
class ShapeEditor : public juce::Component
{
public:
    ShapeEditor() = default;
    void setPoints (const std::vector<ShapePoint>& p) { pts = p; repaint(); }
    const std::vector<ShapePoint>& getPoints() const { return pts; }
    std::function<void (const std::vector<ShapePoint>&)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    float playhead = -1.0f;
    bool loopLook = false;
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

// ------------------------------------------------------------------ envelope view / editor
class EnvView : public juce::Component, public Animated
{
public:
    EnvView (Ctx& c);
    ~EnvView() override;
    void setEnv (int e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void animate() override;
private:
    Ctx& ctx;
    int env = 0;
    ShapeEditor editor;
    float live = 0.0f;
    juce::String lastKey;
    juce::String key() const;
};

// ------------------------------------------------------------------ lfo view / editor
class LfoView : public juce::Component, public Animated
{
public:
    LfoView (Ctx& c);
    ~LfoView() override;
    void setLfo (int l);
    void paint (juce::Graphics&) override;
    void resized() override;
    void animate() override;
private:
    Ctx& ctx;
    int lfo = 0;
    ShapeEditor editor;
    float live = 0.0f;
    int lastWave = -1;
};

// ------------------------------------------------------------------ analysis views
class ScopeView : public juce::Component, public Animated
{
public:
    ScopeView (Ctx& c, bool small = false);
    ~ScopeView() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
    bool showPre = false;
private:
    Ctx& ctx;
    bool small;
    std::vector<float> buf;
};

class SpectrumView : public juce::Component, public Animated
{
public:
    SpectrumView (Ctx& c);
    ~SpectrumView() override;
    void paint (juce::Graphics&) override;
    void animate() override;
    bool comparePre = true;
    const std::vector<float>& magnitudes() const { return post; }
    float binHz() const { return (float) (sr / (double) fftSize); }
private:
    Ctx& ctx;
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> work, post, pre, smoothPost, smoothPre;
    double sr = 48000.0;
    void compute (const be::ScopeFifo& f, std::vector<float>& out);
};

class HarmonicsView : public juce::Component
{
public:
    explicit HarmonicsView (SpectrumView& s) : spec (s) {}
    void paint (juce::Graphics&) override;
    float f0 = 0.0f;
private:
    SpectrumView& spec;
};

class Meter : public juce::Component, public Animated
{
public:
    Meter (Ctx& c, bool horizontal = false);
    ~Meter() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
private:
    Ctx& ctx;
    bool horizontal;
    float holdL = 0, holdR = 0;
};

// XY morph pad
class XYPad : public juce::Component, public Animated
{
public:
    XYPad (Ctx& c);
    ~XYPad() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void animate() override;
private:
    Ctx& ctx;
    float lx = -1, ly = -1;
};

// filter response sketch
class FilterCurve : public juce::Component, public Animated
{
public:
    FilterCurve (Ctx& c, int filter);
    ~FilterCurve() override;
    void paint (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    int f;
    juce::int64 last = 0;
};
} // namespace be::ui

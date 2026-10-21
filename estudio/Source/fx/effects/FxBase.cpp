#include "FxBase.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"
#include <complex>

namespace racoon::fx
{
using namespace juce;
using namespace racoon::ui;

AudioProcessor::BusesProperties FxBase::busesFor (bool sidechain)
{
    auto b = BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                              .withOutput ("Output", AudioChannelSet::stereo(), true);
    if (sidechain) b = b.withInput ("Sidechain", AudioChannelSet::stereo(), false);
    return b;
}

FxBase::FxBase (const String& name, const String& category, AudioProcessorValueTreeState::ParameterLayout layout,
                double tail, bool sc, bool tailOnBypass, bool canSleep)
    : RacoonProcessor (busesFor (sc), PluginInfo { name, category, Brand::limon, PluginKind::effect, 1, tail, tailOnBypass, canSleep },
                       std::move (layout)),
      hasSidechain (sc)
{
}

void FxBase::prepareDsp (double rate, int block)
{
    std::fill (std::begin (scope), std::end (scope), 0.0f);
    scopeDecimate = jmax (1, (int) (rate / 6000.0));
    prepare (rate, block);
}

void FxBase::resetDsp() { reset(); }

const float* FxBase::sidechain (int ch) const
{
    if (! sidechainActive) return nullptr;
    return sidechainCopy.getReadPointer (jmin (ch, sidechainCopy.getNumChannels() - 1));
}

void FxBase::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, false, 0);
    sidechainActive = false;
    if (hasSidechain && getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
        {
            sidechainCopy = AudioBuffer<float> (sc.getArrayOfWritePointers(), sc.getNumChannels(), n);   // refers, no copy
            sidechainActive = true;
        }
    }
    process (main, midi);

    // scope: a decimated mono picture of the output
    int pos = scopePos.load();
    const int chans = main.getNumChannels();
    for (int i = 0; i < n; i += scopeDecimate)
    {
        float v = 0.0f;
        for (int c = 0; c < chans; ++c) v += main.getSample (c, i);
        scope[pos] = v / (float) jmax (1, chans);
        pos = (pos + 1) % scopeSize;
    }
    scopePos.store (pos);
}

// ===================================================================== default screen
namespace
{
class ScopeDisplay : public Component, private Timer
{
public:
    explicit ScopeDisplay (FxBase& p) : proc (p) { startTimerHz (60); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (proc.showsGR ? 132.0f : 90.0f);
        r.removeFromRight (14.0f);
        drawScreenGrid (g, r, 8, 4);
        // dB scale of the wave on the left edge
        g.setFont (NumFont::medium (9.5f));
        g.setColour (Pro::textFaint);
        for (float lv : { 0.5f, -0.5f })
            g.drawText (::racoon::i18n::tr (lv > 0 ? "-6" : "-6"), Rectangle<float> (r.getX() + 3.0f, r.getCentreY() - lv * r.getHeight() * 0.42f - 6.0f, 24.0f, 12.0f), Justification::left);
        // the output wave, triggered on a rising zero crossing so it stands still on a steady tone
        const int pos = proc.scopePos.load();
        const int n = FxBase::scopeSize;
        int startIdx = 0;
        for (int i = n / 4; i < n / 2; ++i)
        {
            const float a = proc.scope[(pos + i - 1) % n], b = proc.scope[(pos + i) % n];
            if (a < 0.0f && b >= 0.0f) { startIdx = i; break; }
        }
        const int shown = n / 2;
        Path p;
        for (int i = 0; i < shown; ++i)
        {
            const float v = jlimit (-1.2f, 1.2f, proc.scope[(pos + startIdx + i) % n]);
            const float x = r.getX() + r.getWidth() * (float) i / (float) (shown - 1);
            const float y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (Pro::value.withAlpha (0.12f));
        g.strokePath (p, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Pro::value);
        g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        // meters with their numbers
        const float w = proc.showsGR ? meters.getWidth() / 3.0f : meters.getWidth() / 2.0f;
        auto dbOf = [] (float lin) { return jmax (-99.0f, Decibels::gainToDecibels (lin + 1.0e-9f)); };
        ProDraw::meter (g, meters.removeFromLeft (w), dbOf (proc.inPeak.load()), Pro::input, "In");
        ProDraw::meter (g, meters.removeFromLeft (w), dbOf (proc.outPeak.load()), Pro::value, "Out");
        if (proc.showsGR) ProDraw::meter (g, meters, proc.grDb.load(), Pro::reduce, "GR", true, 24.0f);
        const auto text = proc.displayText();
        if (text.isNotEmpty())
        {
            g.setColour (Pro::text);
            g.setFont (NumFont::medium (12.0f));
            g.drawText (::racoon::i18n::tr (text), r.reduced (8, 6).removeFromTop (18), Justification::topLeft);
        }
    }
private:
    FxBase& proc;
    void timerCallback() override { repaint(); }
};
}

std::unique_ptr<Component> FxBase::makeDisplay() { return std::make_unique<ScopeDisplay> (*this); }

AudioProcessorEditor* FxBase::createEditor()
{
    return new RacoonEditor (*this, editorSpec(), makeDisplay());
}

// ===================================================================== helpers
namespace dsp2
{
void Biquad::set (Type t, double sr, double f, double q, double gainDb)
{
    f = jlimit (5.0, sr * 0.49, f);
    q = jmax (0.05, q);
    const double A = std::pow (10.0, gainDb / 40.0), w = MathConstants<double>::twoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
    const double alpha = sw / (2.0 * q);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (t)
    {
        case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case allpass:  B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
        case lowshelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
            A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case highshelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }
    const float nb0 = (float) (B0 / A0), nb1 = (float) (B1 / A0), nb2 = (float) (B2 / A0), na1 = (float) (A1 / A0), na2 = (float) (A2 / A0);
    if (! fresh && nb0 == b0 && nb1 == b1 && nb2 == b2 && na1 == a1 && na2 == a2) return;   // same filter: nothing to glide
    b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2;
    startRamp();
}

double Biquad::magnitudeDb (double f, double sr) const
{
    const double w = MathConstants<double>::twoPi * f / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto h = ((double) b0 + (double) b1 * z1 + (double) b2 * z2) / (1.0 + (double) a1 * z1 + (double) a2 * z2);
    return 20.0 * std::log10 (jmax (1.0e-9, std::abs (h)));
}

namespace
{
class ResponseDisplay : public Component, private Timer
{
public:
    ResponseDisplay (FxBase& p, std::function<float (double)> f, float range) : proc (p), fn (std::move (f)), rangeDb (range) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (90.0f);
        r.removeFromRight (14.0f);
        auto plot = r.withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
        // grid: octaves and dB steps, labelled
        g.setFont (NumFont::medium (9.5f));
        for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xFor (plot, hz);
            g.setColour (Pro::grid);
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (hz >= 1000 ? String ((int) (hz / 1000)) + "k" : String ((int) hz)), Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), Justification::centred);
        }
        const float step = rangeDb >= 18.0f ? 6.0f : 3.0f;
        for (float db = -rangeDb; db <= rangeDb + 0.01f; db += step)
        {
            const float y = yFor (plot, db);
            g.setColour (std::abs (db) < 0.01f ? Pro::line : Pro::grid);
            g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr ((db > 0 ? "+" : "") + String ((int) db)), Rectangle<float> (r.getX(), y - 6, 26, 12), Justification::centredRight);
        }
        Path p, fill;
        for (int i = 0; i <= 300; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0, i / 300.0);
            const float db = jlimit (-rangeDb, rangeDb, fn (hz));
            const float x = xFor (plot, hz), y = yFor (plot, db);
            if (i == 0) { p.startNewSubPath (x, y); fill.startNewSubPath (x, yFor (plot, 0.0f)); }
            p.lineTo (x, y); fill.lineTo (x, y);
        }
        fill.lineTo (plot.getRight(), yFor (plot, 0.0f));
        fill.closeSubPath();
        g.setColour (Pro::value.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (Pro::value);
        g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        auto dbOf = [] (float lin) { return jmax (-99.0f, Decibels::gainToDecibels (lin + 1.0e-9f)); };
        ProDraw::meter (g, meters.removeFromLeft (meters.getWidth() * 0.5f), dbOf (proc.inPeak.load()), Pro::input, "In");
        ProDraw::meter (g, meters, dbOf (proc.outPeak.load()), Pro::value, "Out");
        const auto text = proc.displayText();
        if (text.isNotEmpty())
        {
            g.setColour (Pro::text);
            g.setFont (NumFont::medium (12.0f));
            g.drawText (::racoon::i18n::tr (text), plot.reduced (8, 6).removeFromTop (18), Justification::topLeft);
        }
    }
private:
    FxBase& proc;
    std::function<float (double)> fn;
    float rangeDb;
    static float xFor (Rectangle<float> r, double hz) { return r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); }
    float yFor (Rectangle<float> r, float db) const { return r.getCentreY() - db / rangeDb * r.getHeight() * 0.48f; }
    void timerCallback() override { repaint(); }
};
}

namespace
{
class CompressorDisplay : public Component, private Timer
{
public:
    CompressorDisplay (FxBase& p, CompressorView v) : proc (p), view (std::move (v))
    {
        hist[0].fill (-99.0f); hist[1].fill (-99.0f); hist[2].fill (0.0f);
        startTimerHz (60);
    }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (132.0f);
        r.removeFromRight (14.0f);
        // ---- transfer curve: a square on the left
        auto tf = r.removeFromLeft (jmin (r.getHeight(), r.getWidth() * 0.38f)).withTrimmedLeft (24.0f).withTrimmedBottom (14.0f);
        r.removeFromLeft (16.0f);
        const float lo = -60.0f;
        auto px = [&] (float db) { return tf.getX() + tf.getWidth() * (db - lo) / -lo; };
        auto py = [&] (float db) { return tf.getBottom() - tf.getHeight() * (jlimit (lo, 6.0f, db) - lo) / -lo; };
        g.setFont (NumFont::medium (9.5f));
        for (float d = -60.0f; d <= 0.01f; d += 12.0f)
        {
            g.setColour (Pro::grid);
            g.fillRect (px (d), tf.getY(), 1.0f, tf.getHeight());
            g.fillRect (tf.getX(), py (d), tf.getWidth(), 1.0f);
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (String ((int) d)), Rectangle<float> (tf.getX() - 26.0f, py (d) - 6.0f, 22.0f, 12.0f), Justification::centredRight);
            g.drawText (::racoon::i18n::tr (String ((int) d)), Rectangle<float> (px (d) - 14.0f, tf.getBottom() + 2.0f, 28.0f, 12.0f), Justification::centred);
        }
        g.setColour (Pro::line);   // 1:1
        g.drawLine (px (lo), py (lo), px (0.0f), py (0.0f), 1.0f);
        const float th = view.threshold ? view.threshold() : 0.0f;
        g.setColour (Pro::reduce.withAlpha (0.6f));
        const float dash[] { 3.0f, 3.0f };
        g.drawDashedLine ({ px (th), tf.getY(), px (th), tf.getBottom() }, dash, 2, 1.0f);
        Path curve;
        for (int i = 0; i <= 120; ++i)
        {
            const float in = lo + (-lo) * (float) i / 120.0f;
            const float out = view.curve ? view.curve (in) : in;
            if (i == 0) curve.startNewSubPath (px (in), py (out)); else curve.lineTo (px (in), py (out));
        }
        g.saveState();
        g.reduceClipRegion (tf.toNearestInt());
        g.setColour (Pro::value);
        g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        // where the signal is now
        const float inNow = jmax (lo, levelIn);
        const float outNow = view.curve ? view.curve (inNow) : inNow;
        g.setColour (Pro::input);
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre ({ px (inNow), py (outNow) }));
        g.restoreState();
        {
            auto lab = tf.withHeight (16.0f).translated (6.0f, 3.0f);
            ProDraw::caps (g, "Umbral", lab.removeFromLeft (48.0f), Justification::centredLeft, Pro::reduce, 9.5f);
            g.setFont (NumFont::medium (11.0f));
            g.setColour (Pro::text);
            g.drawText (::racoon::i18n::tr (String (th, 1) + " dB"), lab, Justification::centredLeft);
        }

        // ---- history: the last 4 seconds (legend and caption on a strip above, so nothing covers the traces)
        auto strip = r.removeFromTop (18.0f);
        r.removeFromTop (4.0f);
        auto hr = r.withTrimmedBottom (14.0f);
        g.setColour (Pro::grid);
        for (int k = 1; k < 4; ++k) g.fillRect (hr.getX() + hr.getWidth() * (float) k / 4.0f, hr.getY(), 1.0f, hr.getHeight());
        g.setFont (NumFont::medium (9.5f));
        g.setColour (Pro::textFaint);
        for (int k = 0; k <= 4; ++k)
            g.drawText (::racoon::i18n::tr ((k == 4 ? String ("0 s") : "-" + String (4 - k) + " s")), Rectangle<float> (hr.getX() + hr.getWidth() * (float) k / 4.0f - 18.0f, hr.getBottom() + 2.0f, 36.0f, 12.0f), Justification::centred);
        auto hy = [&] (float db) { return hr.getBottom() - hr.getHeight() * (jlimit (lo, 0.0f, db) - lo) / -lo; };
        auto trace = [&] (int which, Colour col, float width)
        {
            Path p;
            for (int i = 0; i < histLen; ++i)
            {
                const float v = hist[(size_t) which][(size_t) ((head + i) % histLen)];
                const float x = hr.getX() + hr.getWidth() * (float) i / (float) (histLen - 1);
                if (i == 0) p.startNewSubPath (x, hy (v)); else p.lineTo (x, hy (v));
            }
            g.setColour (col);
            g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
        };
        // gain reduction hangs from the top, filled
        Path gr;
        gr.startNewSubPath (hr.getX(), hr.getY());
        for (int i = 0; i < histLen; ++i)
        {
            const float v = hist[2][(size_t) ((head + i) % histLen)];
            gr.lineTo (hr.getX() + hr.getWidth() * (float) i / (float) (histLen - 1), hr.getY() + hr.getHeight() * jlimit (0.0f, 1.0f, -v / 24.0f));
        }
        gr.lineTo (hr.getRight(), hr.getY());
        gr.closeSubPath();
        g.setColour (Pro::reduce.withAlpha (0.35f));
        g.fillPath (gr);
        trace (0, Pro::input.withAlpha (0.7f), 1.2f);
        trace (1, Pro::value, 1.6f);
        {
            // legend: a short colour bar and the name, along the bottom edge
            auto leg = strip;
            auto item = [&] (const String& name, Colour col)
            {
                g.setColour (col);
                g.fillRect (leg.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 2.0f));
                leg.removeFromLeft (5.0f);
                ProDraw::caps (g, name, leg.removeFromLeft (78.0f), Justification::centredLeft, Pro::textDim, 9.5f);
            };
            item ("Entrada", Pro::input);
            item ("Salida", Pro::value);
            item (String::fromUTF8 ("Reducción"), Pro::reduce);
        }
        if (view.caption)
        {
            g.setColour (Pro::text);
            g.setFont (NumFont::medium (12.0f));
            g.drawText (::racoon::i18n::tr (view.caption()), strip, Justification::centredRight);
        }
        // ---- meters with numbers
        const float w = meters.getWidth() / 3.0f;
        ProDraw::meter (g, meters.removeFromLeft (w), levelIn, Pro::input, "In");
        ProDraw::meter (g, meters.removeFromLeft (w), levelOut, Pro::value, "Out");
        ProDraw::meter (g, meters, grNow, Pro::reduce, "GR", true, 24.0f);
    }
private:
    FxBase& proc;
    CompressorView view;
    static constexpr int histLen = 240;   // 4 s at 60 frames per second
    std::array<std::array<float, histLen>, 3> hist {};
    int head = 0;
    float levelIn = -99.0f, levelOut = -99.0f, grNow = 0.0f;
    void timerCallback() override
    {
        auto dbOf = [] (float lin) { return jmax (-99.0f, Decibels::gainToDecibels (lin + 1.0e-9f)); };
        levelIn = dbOf (proc.inPeak.load());
        levelOut = dbOf (proc.outPeak.load());
        grNow = jmin (0.0f, proc.grDb.load());
        hist[0][(size_t) head] = levelIn;
        hist[1][(size_t) head] = levelOut;
        hist[2][(size_t) head] = grNow;
        head = (head + 1) % histLen;
        repaint();
    }
};
}

std::unique_ptr<Component> makeCompressorDisplay (FxBase& p, CompressorView v) { return std::make_unique<CompressorDisplay> (p, std::move (v)); }

namespace
{
float dbOfLin (float lin) { return jmax (-99.0f, Decibels::gainToDecibels (lin + 1.0e-9f)); }

// ---------------------------------------------------------------- saturation: the curve and the wave
class TransferDisplay : public Component, private Timer
{
public:
    TransferDisplay (FxBase& p, std::function<float (float)> c, std::function<String()> cap) : proc (p), curve (std::move (c)), caption (std::move (cap)) { startTimerHz (60); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (90.0f);
        r.removeFromRight (14.0f);
        auto tf = r.removeFromLeft (jmin (r.getHeight(), r.getWidth() * 0.38f)).withTrimmedLeft (8.0f);
        r.removeFromLeft (16.0f);
        // the curve
        g.setColour (Pro::grid);
        for (int k = 1; k < 4; ++k)
        {
            g.fillRect (tf.getX() + tf.getWidth() * (float) k / 4.0f, tf.getY(), 1.0f, tf.getHeight());
            g.fillRect (tf.getX(), tf.getY() + tf.getHeight() * (float) k / 4.0f, tf.getWidth(), 1.0f);
        }
        g.setColour (Pro::line);
        g.drawLine (tf.getX(), tf.getBottom(), tf.getRight(), tf.getY(), 1.0f);   // a clean wire
        Path p;
        float peakOut = 0.0f;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 160.0f;
            const float y = curve ? curve (x) : x;
            peakOut = jmax (peakOut, std::abs (y));
        }
        const float scale = 1.0f / jmax (1.0f, peakOut);
        for (int i = 0; i <= 160; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 160.0f;
            const float y = (curve ? curve (x) : x) * scale;
            const float px = tf.getX() + tf.getWidth() * (x + 1.0f) * 0.5f, py = tf.getCentreY() - y * tf.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        // the part of the curve the signal uses now
        const float in = jlimit (0.0f, 1.0f, proc.inPeak.load());
        g.setColour (Pro::input.withAlpha (0.10f));
        g.fillRect (Rectangle<float> (tf.getCentreX() - tf.getWidth() * 0.5f * in, tf.getY(), tf.getWidth() * in, tf.getHeight()));
        g.setColour (Pro::value);
        g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        ProDraw::caps (g, "Curva", tf.withHeight (16.0f).translated (6.0f, 3.0f), Justification::centredLeft, Pro::textDim, 9.5f);
        // the wave out, held still on a rising zero crossing
        const int pos = proc.scopePos.load(), n = FxBase::scopeSize;
        int start = 0;
        for (int i = n / 4; i < n / 2; ++i) { const float a = proc.scope[(pos + i - 1) % n], b = proc.scope[(pos + i) % n]; if (a < 0.0f && b >= 0.0f) { start = i; break; } }
        drawScreenGrid (g, r, 6, 4);
        Path w;
        const int shown = n / 2;
        for (int i = 0; i < shown; ++i)
        {
            const float v = jlimit (-1.2f, 1.2f, proc.scope[(pos + start + i) % n]);
            const float x = r.getX() + r.getWidth() * (float) i / (float) (shown - 1), y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
        }
        g.setColour (Pro::value.withAlpha (0.12f));
        g.strokePath (w, PathStrokeType (4.0f));
        g.setColour (Pro::value);
        g.strokePath (w, PathStrokeType (1.5f));
        if (caption)
        {
            g.setColour (Pro::text);
            g.setFont (NumFont::medium (12.0f));
            g.drawText (::racoon::i18n::tr (caption()), r.reduced (8.0f, 6.0f).removeFromTop (18.0f), Justification::topRight);
        }
        ProDraw::meter (g, meters.removeFromLeft (meters.getWidth() * 0.5f), dbOfLin (proc.inPeak.load()), Pro::input, "In");
        ProDraw::meter (g, meters, dbOfLin (proc.outPeak.load()), Pro::value, "Out");
    }
private:
    FxBase& proc;
    std::function<float (float)> curve;
    std::function<String()> caption;
    void timerCallback() override { repaint(); }
};

// ---------------------------------------------------------------- delay: the echoes on a time line
class EchoDisplay : public Component, private Timer
{
public:
    EchoDisplay (FxBase& p, std::function<EchoView()> f) : proc (p), get (std::move (f)) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (90.0f);
        r.removeFromRight (14.0f);
        const auto v = get ? get() : EchoView();
        auto plot = r.withTrimmedBottom (16.0f).withTrimmedTop (22.0f);
        // the window: enough time for the echoes to fall 48 dB, at least 1 s
        const double fbk = jlimit (0.0, 0.99, (double) v.feedback);
        const double step = jmax (v.leftSeconds, v.rightSeconds);
        const int repeats = fbk <= 0.001 ? 1 : jlimit (1, 24, (int) std::ceil (std::log (0.004) / std::log (fbk)));
        const double span = jlimit (1.0, 8.0, step * (repeats + 1));
        auto xFor = [&] (double t) { return plot.getX() + plot.getWidth() * (float) (t / span); };
        g.setColour (Pro::grid);
        g.setFont (NumFont::medium (9.5f));
        const double tick = span > 4.0 ? 1.0 : (span > 2.0 ? 0.5 : 0.25);
        for (double t = 0.0; t <= span + 1e-6; t += tick)
        {
            g.setColour (Pro::grid);
            g.fillRect (xFor (t), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (String (t, t < 1.0 ? 2 : 1) + " s"), Rectangle<float> (xFor (t) - 22.0f, plot.getBottom() + 2.0f, 44.0f, 12.0f), Justification::centred);
        }
        const float mid = plot.getCentreY();
        g.setColour (Pro::line);
        g.fillRect (plot.getX(), mid, plot.getWidth(), 1.0f);
        // the dry hit at 0
        g.setColour (Pro::text);
        g.fillRect (Rectangle<float> (xFor (0.0) - 1.5f, plot.getY() + 4.0f, 3.0f, plot.getHeight() - 8.0f));
        // echoes: left above, right below; ping pong alternates sides
        for (int k = 1; k <= repeats; ++k)
        {
            const float lvl = (float) std::pow (fbk, k - 1) * jmax (0.15f, v.mix * 1.5f);
            const float h = (plot.getHeight() * 0.5f - 6.0f) * jlimit (0.0f, 1.0f, lvl);
            auto bar = [&] (double t, bool up, Colour c)
            {
                if (t > span) return;
                const float x = xFor (t);
                g.setColour (c);
                g.fillRect (up ? Rectangle<float> (x - 2.0f, mid - h, 4.0f, h) : Rectangle<float> (x - 2.0f, mid + 1.0f, 4.0f, h));
            };
            if (v.pingPong) bar (k * v.leftSeconds, (k % 2) == 1, (k % 2) == 1 ? Pro::input : Pro::value);
            else { bar (k * v.leftSeconds, true, Pro::input); bar (k * v.rightSeconds, false, Pro::value); }
        }
        ProDraw::caps (g, v.pingPong ? "Ping pong" : "Izquierda", plot.withHeight (14.0f).translated (6.0f, 2.0f), Justification::centredLeft, Pro::input, 9.5f);
        if (! v.pingPong) ProDraw::caps (g, "Derecha", plot.withTrimmedTop (plot.getHeight() - 16.0f).translated (6.0f, 0.0f), Justification::centredLeft, Pro::value, 9.5f);
        g.setColour (Pro::text);
        g.setFont (NumFont::medium (12.0f));
        g.drawText (::racoon::i18n::tr (v.caption), r.withHeight (18.0f), Justification::topRight);
        ProDraw::meter (g, meters.removeFromLeft (meters.getWidth() * 0.5f), dbOfLin (proc.inPeak.load()), Pro::input, "In");
        ProDraw::meter (g, meters, dbOfLin (proc.outPeak.load()), Pro::value, "Out");
    }
private:
    FxBase& proc;
    std::function<EchoView()> get;
    void timerCallback() override { repaint(); }
};

// ---------------------------------------------------------------- reverb: the shape of the tail
class DecayDisplay : public Component, private Timer
{
public:
    DecayDisplay (FxBase& p, std::function<DecayView()> f) : proc (p), get (std::move (f)) { hist.fill (-99.0f); startTimerHz (60); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto meters = r.removeFromRight (90.0f);
        r.removeFromRight (14.0f);
        const auto v = get ? get() : DecayView();
        auto plot = r.withTrimmedBottom (16.0f).withTrimmedLeft (28.0f).withTrimmedTop (22.0f);
        const double span = jlimit (0.5, 12.0, v.predelay + v.decay * 1.15);
        auto xFor = [&] (double t) { return plot.getX() + plot.getWidth() * (float) (t / span); };
        auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * jlimit (0.0f, 1.0f, -db / 66.0f); };
        g.setFont (NumFont::medium (9.5f));
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            g.setColour (Pro::grid);
            g.fillRect (plot.getX(), yFor (db), plot.getWidth(), 1.0f);
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (String ((int) db)), Rectangle<float> (r.getX(), yFor (db) - 6.0f, 24.0f, 12.0f), Justification::centredRight);
        }
        const double tick = span > 6.0 ? 2.0 : (span > 3.0 ? 1.0 : 0.5);
        for (double t = 0.0; t <= span + 1e-6; t += tick)
        {
            g.setColour (Pro::grid);
            g.fillRect (xFor (t), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (String (t, 1) + " s"), Rectangle<float> (xFor (t) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), Justification::centred);
        }
        // the envelope: silence during the predelay, early reflections, then the decay (the highs fade faster)
        Path low, high;
        const int steps = 200;
        for (int i = 0; i <= steps; ++i)
        {
            const double t = span * i / steps;
            float dbLow = -99.0f, dbHigh = -99.0f;
            if (t >= v.predelay)
            {
                const double tt = t - v.predelay;
                const double rise = jmin (1.0, tt / 0.012);
                dbLow = (float) (v.freeze ? 0.0 : -60.0 * tt / jmax (0.05, v.decay)) + (float) Decibels::gainToDecibels (jmax (1.0e-4, rise));
                dbHigh = (float) (v.freeze ? 0.0 : -60.0 * tt / jmax (0.05, v.decay * (1.0 - 0.75 * v.damping))) + (float) Decibels::gainToDecibels (jmax (1.0e-4, rise));
            }
            const float x = xFor (t);
            if (i == 0) { low.startNewSubPath (x, yFor (dbLow)); high.startNewSubPath (x, yFor (dbHigh)); }
            else { low.lineTo (x, yFor (dbLow)); high.lineTo (x, yFor (dbHigh)); }
        }
        Path fill (low);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (Pro::second.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (Pro::second);
        g.strokePath (low, PathStrokeType (2.0f));
        g.setColour (Pro::input.withAlpha (0.8f));
        g.strokePath (high, PathStrokeType (1.2f));
        // early reflections as short ticks
        if (v.early > 0.01f)
            for (int k = 0; k < 8; ++k)
            {
                const double t = v.predelay + 0.006 + k * 0.011 * (1.0 + 0.3 * std::sin (k * 1.7));
                if (t > span) break;
                const float h = plot.getHeight() * 0.35f * v.early * (float) std::pow (0.82, k);
                g.setColour (Pro::text.withAlpha (0.8f));
                g.fillRect (xFor (t) - 1.0f, yFor (0.0f), 2.0f, h);
            }
        // predelay marker
        g.setColour (Pro::reduce.withAlpha (0.7f));
        const float dash[] { 3.0f, 3.0f };
        g.drawDashedLine ({ xFor (v.predelay), plot.getY(), xFor (v.predelay), plot.getBottom() }, dash, 2, 1.0f);
        // the level coming out, last 2 s, scrolling along the top
        auto lv = r.withHeight (16.0f);
        Path out;
        for (int i = 0; i < histLen; ++i)
        {
            const float d = hist[(size_t) ((head + i) % histLen)];
            const float x = lv.getX() + 28.0f + (lv.getWidth() - 200.0f) * (float) i / (float) (histLen - 1);
            const float y = lv.getBottom() - lv.getHeight() * jlimit (0.0f, 1.0f, (d + 60.0f) / 60.0f);
            if (i == 0) out.startNewSubPath (x, y); else out.lineTo (x, y);
        }
        g.setColour (Pro::value);
        g.strokePath (out, PathStrokeType (1.2f));
        g.setColour (Pro::text);
        g.setFont (NumFont::medium (12.0f));
        g.drawText (::racoon::i18n::tr (v.caption), lv, Justification::centredRight);
        ProDraw::caps (g, "Graves", plot.withHeight (14.0f).translated (plot.getWidth() - 150.0f, 4.0f), Justification::centredLeft, Pro::second, 9.5f);
        ProDraw::caps (g, "Agudos", plot.withHeight (14.0f).translated (plot.getWidth() - 80.0f, 4.0f), Justification::centredLeft, Pro::input, 9.5f);
        ProDraw::meter (g, meters.removeFromLeft (meters.getWidth() * 0.5f), dbOfLin (proc.inPeak.load()), Pro::input, "In");
        ProDraw::meter (g, meters, dbOfLin (proc.outPeak.load()), Pro::value, "Out");
    }
private:
    FxBase& proc;
    std::function<DecayView()> get;
    static constexpr int histLen = 120;
    std::array<float, histLen> hist {};
    int head = 0;
    void timerCallback() override { hist[(size_t) head] = dbOfLin (proc.outPeak.load()); head = (head + 1) % histLen; repaint(); }
};
}

std::unique_ptr<Component> makeTransferDisplay (FxBase& p, std::function<float (float)> c, std::function<String()> cap) { return std::make_unique<TransferDisplay> (p, std::move (c), std::move (cap)); }
std::unique_ptr<Component> makeEchoDisplay (FxBase& p, std::function<EchoView()> f) { return std::make_unique<EchoDisplay> (p, std::move (f)); }
std::unique_ptr<Component> makeDecayDisplay (FxBase& p, std::function<DecayView()> f) { return std::make_unique<DecayDisplay> (p, std::move (f)); }

std::unique_ptr<Component> makeResponseDisplay (FxBase& p, std::function<float (double)> f, float range)
{
    return std::make_unique<ResponseDisplay> (p, std::move (f), range);
}

StringArray lfoShapeNames() { return { "Seno", "Triángulo"_u, "Sierra arriba", "Sierra abajo", "Cuadrada", "Azar (S&H)", "Azar suave" }; }

float lfoShape (int shape, double ph, float& hold, float& from, uint32& rng, bool newCycle)
{
    auto rnd = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xffffff) / 8388608.0f - 1.0f; };
    switch (shape)
    {
        case 0: return (float) std::sin (MathConstants<double>::twoPi * ph);
        case 1: return ph < 0.5 ? (float) (4.0 * ph - 1.0) : (float) (3.0 - 4.0 * ph);
        case 2: return (float) (2.0 * ph - 1.0);
        case 3: return (float) (1.0 - 2.0 * ph);
        case 4: return ph < 0.5 ? 1.0f : -1.0f;
        case 5: if (newCycle) hold = rnd(); return hold;
        default:
        {
            // smooth random: a new target every cycle, eased in with a cosine
            if (newCycle) { from = hold; hold = rnd(); }
            const float w = 0.5f - 0.5f * (float) std::cos (MathConstants<double>::pi * ph);
            return from + (hold - from) * w;
        }
    }
}
}
}

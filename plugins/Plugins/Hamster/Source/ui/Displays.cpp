#include "Displays.h"
#include "I18n.h"
#include "../dsp/DrumVoice.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== shape editor
Point<float> ShapeEditor::toScreen (const ShapePoint& p) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    return { r.getX() + p.x * r.getWidth(), r.getBottom() - p.y * r.getHeight() };
}

ShapePoint ShapeEditor::fromScreen (Point<float> s) const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    return { jlimit (0.0f, 1.0f, (s.x - r.getX()) / r.getWidth()), jlimit (0.0f, 1.0f, (r.getBottom() - s.y) / r.getHeight()), 0.0f };
}

int ShapeEditor::hit (Point<float> s) const
{
    for (int i = 0; i < (int) pts.size(); ++i)
        if (toScreen (pts[(size_t) i]).getDistanceFrom (s) < 7.0f) return i;
    return -1;
}

void ShapeEditor::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    if (pts.size() < 2) return;
    Path p;
    auto lut = ShapeLut::fromPoints (pts);
    auto inner = r.reduced (6.0f);
    for (int x = 0; x <= (int) inner.getWidth(); ++x)
    {
        const float t = (float) x / inner.getWidth();
        const Point<float> pt (inner.getX() + (float) x, inner.getBottom() - lut->at (t) * inner.getHeight());
        if (x == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    Path fill (p);
    fill.lineTo (inner.getRight(), inner.getBottom()); fill.lineTo (inner.getX(), inner.getBottom()); fill.closeSubPath();
    g.setColour (colour.withAlpha (0.07f)); g.fillPath (fill);
    Draw::glowPath (g, p, colour, 1.4f);
    for (auto& pt : pts)
    {
        const auto s = toScreen (pt);
        Path d; d.addPolygon (s, 4, 4.5f, MathConstants<float>::pi / 4.0f);
        g.setColour (C::void_); g.fillPath (d);
        g.setColour (C::bone); g.strokePath (d, PathStrokeType (1.0f));
    }
    if (playhead >= 0.0f)
    {
        const float x = inner.getX() + inner.getWidth() * playhead;
        g.setColour (C::cyan.withAlpha (0.7f));
        g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
    }
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::ash);
    g.drawText (::racoon::i18n::tr ("dbl-click add/remove  /  alt-drag curve"), r.reduced (6.0f, 2.0f), Justification::bottomRight);
}

void ShapeEditor::mouseDown (const MouseEvent& e)
{
    dragIndex = hit (e.position);
    dragCurve = e.mods.isAltDown() || e.mods.isRightButtonDown();
    if (dragCurve)
    {
        const auto sp = fromScreen (e.position);
        for (int i = 1; i < (int) pts.size(); ++i) if (sp.x <= pts[(size_t) i].x) { dragIndex = i; break; }
        if (dragIndex > 0) curveStart = pts[(size_t) dragIndex].curve;
    }
}

void ShapeEditor::mouseDrag (const MouseEvent& e)
{
    if (dragIndex < 0) return;
    auto& p = pts[(size_t) dragIndex];
    if (dragCurve) p.curve = jlimit (-1.0f, 1.0f, curveStart - (float) e.getDistanceFromDragStartY() / 80.0f);
    else
    {
        auto np = fromScreen (e.position);
        const bool first = dragIndex == 0, last = dragIndex == (int) pts.size() - 1;
        const float lo = first ? 0.0f : pts[(size_t) dragIndex - 1].x + 0.001f;
        const float hi = last ? 1.0f : pts[(size_t) dragIndex + 1].x - 0.001f;
        p.x = first ? 0.0f : (last ? 1.0f : jlimit (lo, hi, np.x));
        p.y = np.y;
    }
    repaint();
    if (onChange) onChange (pts);
}

void ShapeEditor::mouseUp (const MouseEvent&) { dragIndex = -1; }

void ShapeEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int h = hit (e.position);
    if (h > 0 && h < (int) pts.size() - 1) pts.erase (pts.begin() + h);
    else if (h < 0 && pts.size() < 32)
    {
        pts.push_back (fromScreen (e.position));
        std::sort (pts.begin(), pts.end(), [] (const ShapePoint& a, const ShapePoint& b) { return a.x < b.x; });
    }
    repaint();
    if (onChange) onChange (pts);
}

// ===================================================================== lfo view
LfoView::LfoView (Ctx& c) : ctx (c)
{
    addChildComponent (editor);
    editor.colour = C::cyan;
    editor.onChange = [this] (const std::vector<ShapePoint>& p) { ctx.proc.setShape (lfo, p); };
    ctx.registerAnimated (this);
}

LfoView::~LfoView() { ctx.unregisterAnimated (this); }

void LfoView::setLfo (int l) { lfo = l; editor.setPoints (ctx.proc.getShape (l)); lastWave = -1; repaint(); }
void LfoView::resized() { editor.setBounds (getLocalBounds()); }

void LfoView::animate()
{
    if (! animVisible (this)) return;
    const int wave = (int) ctx.getParam (glfoId (lfo, "wave"));
    if (wave != lastWave)
    {
        lastWave = wave;
        editor.setVisible (wave == (int) LfoWave::Drawn);
        if (wave == (int) LfoWave::Drawn) editor.setPoints (ctx.proc.getShape (lfo));
    }
    const float v = ctx.proc.engine.telemetry.src[ModSrc::GLfo1 + lfo].load();
    if (std::abs (v - live) > 0.005f) { live = v; repaint(); }
}

void LfoView::paint (Graphics& g)
{
    if (editor.isVisible()) return;
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (8.0f);
    const int wave = (int) ctx.getParam (glfoId (lfo, "wave"));
    Path p;
    Random rnd (7);
    float sh[9];
    for (auto& s : sh) s = rnd.nextFloat() * 2.0f - 1.0f;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = (float) i / 200.0f;
        float v = 0.0f;
        switch ((LfoWave) wave)
        {
            case LfoWave::Sine: v = std::sin (kTwoPi * x); break;
            case LfoWave::Triangle: v = x < 0.25f ? 4 * x : (x < 0.75f ? 2 - 4 * x : 4 * x - 4); break;
            case LfoWave::Square: v = x < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::SawUp: v = 2 * x - 1; break;
            case LfoWave::SawDown: v = 1 - 2 * x; break;
            case LfoWave::SampleHold: v = sh[(i * 8) / 201]; break;
            case LfoWave::SmoothRandom: { const float f = x * 8.0f; const int k = std::min (7, (int) f); v = lerpf (sh[k], sh[k + 1], 0.5f - 0.5f * std::cos (kPi * (f - (float) k))); break; }
            default: break;
        }
        const Point<float> pt (in.getX() + in.getWidth() * x, in.getCentreY() - v * in.getHeight() * 0.45f);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (C::ridge.withAlpha (0.5f));
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    Draw::glowPath (g, p, C::cyan, 1.4f);
    const float y = in.getCentreY() - jlimit (-1.0f, 1.0f, live) * in.getHeight() * 0.45f;
    g.setColour (C::amberHot);
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ in.getRight() - 4.0f, y }));
    g.setFont (Fonts::mono (10.0f)); g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr ("LFO " + String (lfo + 1)), in, Justification::topLeft);
}

// ===================================================================== scope
ScopeView::ScopeView (Ctx& c, bool s) : ctx (c), small (s) { buf.resize (4096); ctx.registerAnimated (this); }
ScopeView::~ScopeView() { ctx.unregisterAnimated (this); }

void ScopeView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    ctx.proc.engine.telemetry.scope.copyLatest (buf.data(), (int) buf.size());
    const int span = small ? 2048 : 4096;
    const int start = (int) buf.size() - span;
    auto in = r.reduced (4.0f);
    Path p;
    float pk = 0.05f;
    for (int i = 0; i < span; ++i) pk = std::max (pk, std::abs (buf[(size_t) (start + i)]));
    const float scale = small ? 0.9f / pk : 0.9f;
    const int step = std::max (1, span / (int) std::max (1.0f, in.getWidth()));
    bool first = true;
    for (int i = 0; i < span; i += step)
    {
        float mx = 0.0f, mn = 0.0f;
        for (int j = i; j < std::min (span, i + step); ++j) { mx = std::max (mx, buf[(size_t) (start + j)]); mn = std::min (mn, buf[(size_t) (start + j)]); }
        const float x = in.getX() + in.getWidth() * (float) i / (float) span;
        const float y1 = in.getCentreY() - jlimit (-1.0f, 1.0f, mx * scale) * in.getHeight() * 0.5f;
        const float y2 = in.getCentreY() - jlimit (-1.0f, 1.0f, mn * scale) * in.getHeight() * 0.5f;
        if (first) { p.startNewSubPath (x, y1); first = false; }
        p.lineTo (x, y1); p.lineTo (x, y2);
    }
    Draw::glowPath (g, p, C::amber, small ? 0.9f : 1.1f);
}

// ===================================================================== spectrum
SpectrumView::SpectrumView (Ctx& c) : ctx (c)
{
    work.resize (fftSize * 2);
    mag.assign (fftSize / 2, 0.0f); smooth = mag;
    ctx.registerAnimated (this);
}
SpectrumView::~SpectrumView() { ctx.unregisterAnimated (this); }

void SpectrumView::animate()
{
    if (! animVisible (this)) return;
    sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    std::vector<float> r ((size_t) fftSize);
    std::fill (work.begin(), work.end(), 0.0f);
    ctx.proc.engine.telemetry.spectrumL.copyLatest (work.data(), fftSize);
    ctx.proc.engine.telemetry.spectrumR.copyLatest (r.data(), fftSize);
    for (int i = 0; i < fftSize; ++i) work[(size_t) i] = 0.5f * (work[(size_t) i] + r[(size_t) i]) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (fftSize - 1)));
    fft.performFrequencyOnlyForwardTransform (work.data());
    for (int i = 0; i < fftSize / 2; ++i)
    {
        mag[(size_t) i] = work[(size_t) i] * (4.0f / (float) fftSize);
        smooth[(size_t) i] = std::max (mag[(size_t) i], smooth[(size_t) i] * 0.88f);
    }
    repaint();
}

void SpectrumView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (6.0f);
    auto xOf = [&] (float hz) { return in.getX() + in.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yOf = [&] (float m) { const float db = jlimit (-96.0f, 6.0f, 20.0f * std::log10 (std::max (m, 1.0e-7f))); return in.getY() + in.getHeight() * (6.0f - db) / 102.0f; };
    g.setFont (Fonts::mono (9.5f));
    for (float hz : { 30.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (hz);
        g.setColour (Colours::white.withAlpha (0.05f)); g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
        g.setColour (C::ash); g.drawText (::racoon::i18n::tr (hz >= 1000 ? String (hz / 1000.0f, 0) + "k" : String ((int) hz)), Rectangle<float> (x + 2, in.getBottom() - 12, 30, 12), Justification::left);
    }
    Path p;
    const float bin = (float) (sr / fftSize);
    bool started = false;
    for (int x = 0; x < (int) in.getWidth(); ++x)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) x / in.getWidth());
        const int k = jlimit (1, (int) smooth.size() - 2, (int) (hz / bin));
        float mx = smooth[(size_t) k];
        const float hz2 = 20.0f * std::pow (1000.0f, (float) (x + 1) / in.getWidth());
        for (int j = k + 1; j < (int) (hz2 / bin) && j < (int) smooth.size(); ++j) mx = std::max (mx, smooth[(size_t) j]);
        const Point<float> pt (in.getX() + (float) x, yOf (mx));
        if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
    }
    Path f (p); f.lineTo (in.getRight(), in.getBottom()); f.lineTo (in.getX(), in.getBottom()); f.closeSubPath();
    g.setColour (C::amber.withAlpha (0.08f)); g.fillPath (f);
    Draw::glowPath (g, p, C::amber, 1.2f);
}

// ===================================================================== master meter
MasterMeter::MasterMeter (Ctx& c, bool h) : ctx (c), horizontal (h) { ctx.registerAnimated (this); }
MasterMeter::~MasterMeter() { ctx.unregisterAnimated (this); }

void MasterMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto& t = ctx.proc.engine.telemetry;
    const float pl = t.masterPeakL.load(), pr = t.masterPeakR.load();
    holdL = std::max (pl, holdL * 0.97f); holdR = std::max (pr, holdR * 0.97f);
    auto norm = [] (float v) { return jlimit (0.0f, 1.0f, (20.0f * std::log10 (std::max (v, 1.0e-5f)) + 60.0f) / 63.0f); };
    auto bar = [&] (Rectangle<float> b, float pk, float hold)
    {
        g.setColour (C::engrave); g.fillRect (b);
        auto f = horizontal ? b.withWidth (b.getWidth() * norm (pk)) : b.withTop (b.getBottom() - b.getHeight() * norm (pk));
        g.setColour (pk > 0.98f ? C::ochre : C::amber); g.fillRect (f);
        const float hv = norm (hold);
        g.setColour (hold > 0.98f ? C::ochre : C::amberHot);
        if (horizontal) g.drawVerticalLine ((int) (b.getX() + b.getWidth() * hv), b.getY(), b.getBottom());
        else g.drawHorizontalLine ((int) (b.getBottom() - b.getHeight() * hv), b.getX(), b.getRight());
    };
    if (horizontal)
    {
        auto a = r.removeFromTop (r.getHeight() * 0.5f).reduced (0, 1);
        bar (a, pl, holdL); bar (r.reduced (0, 1), pr, holdR);
    }
    else
    {
        auto a = r.removeFromLeft (r.getWidth() * 0.5f).reduced (1, 0);
        bar (a, pl, holdL); bar (r.reduced (1, 0), pr, holdR);
    }
}

// ===================================================================== XY pad
XYPad::XYPad (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
XYPad::~XYPad() { ctx.unregisterAnimated (this); }

void XYPad::animate()
{
    if (! animVisible (this)) return;
    const float x = ctx.getParam ("g_xyx"), y = ctx.getParam ("g_xyy");
    if (x != lx || y != ly) { lx = x; ly = y; repaint(); }
}

void XYPad::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (12.0f);
    g.setColour (C::ridge.withAlpha (0.6f));
    g.drawRect (in, 1.0f);
    for (int k = 1; k <= 4; ++k)
    {
        g.setColour (C::amber.withAlpha (0.04f * (float) k));
        g.drawEllipse (in.withSizeKeepingCentre (in.getWidth() * (float) k / 4.0f, in.getHeight() * (float) k / 4.0f), 0.8f);
    }
    const Point<float> puck (in.getX() + in.getWidth() * lx, in.getY() + in.getHeight() * (1.0f - ly));
    Draw::glowLine (g, { puck.x, in.getY() }, { puck.x, in.getBottom() }, C::cyan.withAlpha (0.4f), 0.6f);
    Draw::glowLine (g, { in.getX(), puck.y }, { in.getRight(), puck.y }, C::cyan.withAlpha (0.4f), 0.6f);
    g.setColour (C::cyan);
    g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (puck));
    g.setColour (C::void_);
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (puck));
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::ash);
    g.drawText (::racoon::i18n::tr ("X " + String (roundToInt (lx * 100)) + "  Y " + String (roundToInt (ly * 100))), r.reduced (6.0f, 2.0f), Justification::bottomRight);
}

void XYPad::mouseDown (const MouseEvent& e) { ctx.proc.undo.beginNewTransaction ("XY"); mouseDrag (e); }

void XYPad::mouseDrag (const MouseEvent& e)
{
    auto in = getLocalBounds().toFloat().reduced (12.0f);
    ctx.setParam ("g_xyx", jlimit (0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth()));
    ctx.setParam ("g_xyy", jlimit (0.0f, 1.0f, 1.0f - (e.position.y - in.getY()) / in.getHeight()));
}

// ===================================================================== waveform
WaveformView::WaveformView (Ctx& c) : ctx (c) { ctx.registerAnimated (this); setMouseCursor (MouseCursor::IBeamCursor); }
WaveformView::~WaveformView() { ctx.unregisterAnimated (this); }

void WaveformView::animate()
{
    if (! animVisible (this)) return;
    const int t = ctx.track();
    const auto* s = ctx.proc.trackSample (t, slot);
    if (s == nullptr) s = ctx.proc.displaySample (t);
    const String key = String ((pointer_sized_int) s) + "|" + String (ctx.getParam (ctx.tid (T_C_START)), 3) + String (ctx.getParam (ctx.tid (T_C_END)), 3)
                       + String ((int) ctx.getParam (ctx.tid (T_C_SLICEMODE))) + String ((int) ctx.getParam (ctx.tid (T_C_SLICE)))
                       + String ((int) ctx.getParam (ctx.tid (T_C_REVERSE))) + String (ctx.proc.engine.telemetry.trackHits[t].load());
    if (key != lastKey) { lastKey = key; repaint(); }
}

void WaveformView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    const int t = ctx.track();
    const auto* s = ctx.proc.trackSample (t, slot);
    if (s == nullptr) s = ctx.proc.displaySample (t);
    auto in = r.reduced (6.0f, 8.0f);
    if (hover) { g.setColour (C::cyan.withAlpha (0.15f)); g.fillRect (r); }
    if (s == nullptr || s->size() < 2)
    {
        g.setFont (Fonts::body (15.0f)); g.setColour (C::ash);
        g.drawText (::racoon::i18n::tr ("Drop an audio file here (WAV / AIFF / FLAC)"), in, Justification::centred);
        return;
    }
    const int n = s->size();
    const int w = (int) in.getWidth();
    Path p;
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((int64) n * x / w), b = std::max (a + 1, (int) ((int64) n * (x + 1) / w));
        float mx = 0.0f;
        for (int i = a; i < b && i < n; ++i) mx = std::max (mx, std::abs (s->l[(size_t) i]));
        const float h = mx * in.getHeight() * 0.48f;
        p.addRectangle (in.getX() + (float) x, in.getCentreY() - h, 1.0f, std::max (1.0f, h * 2.0f));
    }
    g.setColour (C::amber.withAlpha (0.75f));
    g.fillPath (p);
    // region
    float st = ctx.getParam (ctx.tid (T_C_START)), en = ctx.getParam (ctx.tid (T_C_END));
    const int sm = (int) ctx.getParam (ctx.tid (T_C_SLICEMODE));
    if (sm > 0)
    {
        const int sl = (int) ctx.getParam (ctx.tid (T_C_SLICE));
        double s0, s1;
        if (sm == 1) { s0 = sl / 16.0; s1 = (sl + 1) / 16.0; }
        else
        {
            const auto& tr = s->transients;
            const int cnt = (int) tr.size(), k = cnt > 0 ? sl % cnt : 0;
            s0 = cnt > 0 ? (double) tr[(size_t) k] / n : 0.0;
            s1 = k + 1 < cnt ? (double) tr[(size_t) k + 1] / n : 1.0;
        }
        g.setColour (C::cyanDim.withAlpha (0.8f));
        if (sm == 1) for (int k = 1; k < 16; ++k) g.drawVerticalLine ((int) (in.getX() + in.getWidth() * k / 16.0f), in.getY(), in.getBottom());
        else for (auto tp : s->transients) g.drawVerticalLine ((int) (in.getX() + in.getWidth() * (float) tp / (float) n), in.getY(), in.getBottom());
        const float w2 = (float) (s1 - s0);
        st = (float) s0 + st * w2 * 0.9f; en = (float) s0 + en * w2;
    }
    const float x0 = in.getX() + in.getWidth() * st, x1 = in.getX() + in.getWidth() * en;
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRect (Rectangle<float> (in.getX(), in.getY(), x0 - in.getX(), in.getHeight()));
    g.fillRect (Rectangle<float> (x1, in.getY(), in.getRight() - x1, in.getHeight()));
    Draw::glowLine (g, { x0, in.getY() }, { x0, in.getBottom() }, C::cyan, 1.4f);
    Draw::glowLine (g, { x1, in.getY() }, { x1, in.getBottom() }, C::cyan, 1.4f);
    g.setFont (Fonts::mono (10.0f));
    g.setColour (C::bone);
    String info = s->name + "   " + String ((double) n / s->sampleRate, 2) + " s   " + (s->stereo() ? "stereo" : "mono");
    if (s->pitchHz > 0.0f) info << "   " << String (s->pitchHz, 1) << " Hz";
    info << "   " << (int) s->transients.size() << " transients";
    g.drawText (::racoon::i18n::tr (info), r.reduced (8.0f, 3.0f), Justification::topLeft);
    if (ctx.getParam (ctx.tid (T_C_REVERSE)) > 0.5f) { g.setColour (C::amberHot); g.drawText (::racoon::i18n::tr ("REVERSE"), r.reduced (8.0f, 3.0f), Justification::topRight); }
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    auto in = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
    const float pos = (e.position.x - in.getX()) / in.getWidth();
    const float st = ctx.getParam (ctx.tid (T_C_START)), en = ctx.getParam (ctx.tid (T_C_END));
    dragWhat = std::abs (pos - st) < std::abs (pos - en) ? 1 : 2;
    ctx.proc.undo.beginNewTransaction ("Sample region");
    mouseDrag (e);
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (ctx.getParam (ctx.tid (T_C_SLICEMODE)) > 0.5f) return;
    auto in = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
    const float pos = jlimit (0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth());
    if (dragWhat == 1) ctx.setParam (ctx.tid (T_C_START), std::min (pos, ctx.getParam (ctx.tid (T_C_END)) - 0.01f));
    else ctx.setParam (ctx.tid (T_C_END), std::max (pos, ctx.getParam (ctx.tid (T_C_START)) + 0.01f));
}

void WaveformView::filesDropped (const StringArray& files, int, int)
{
    hover = false;
    if (! files.isEmpty()) ctx.proc.loadTrackSample (ctx.track(), slot, File (files[0]));
    lastKey = {};
    repaint();
}

// ===================================================================== FM algorithm
FmAlgoView::FmAlgoView (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
FmAlgoView::~FmAlgoView() { ctx.unregisterAnimated (this); }

void FmAlgoView::animate()
{
    if (! animVisible (this)) return;
    const int a = (int) ctx.getParam (ctx.tid (T_B_ALGO)) + 100 * ctx.track();
    if (a != last) { last = a; repaint(); }
}

void FmAlgoView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    const int algo = (int) ctx.getParam (ctx.tid (T_B_ALGO));
    // columns of operator stacks; each entry lists the chain from carrier upward
    std::vector<std::vector<int>> stacks;
    switch (algo)
    {
        case 0: stacks = { { 1, 2 } }; break;
        case 1: stacks = { { 1, 2, 3 } }; break;
        case 2: stacks = { { 1, 2, 3, 4 } }; break;
        case 3: stacks = { { 1, 2 }, { 3, 4 } }; break;
        case 4: stacks = { { 1, 2 }, { 0, 3 }, { 0, 4 } }; break;
        case 5: stacks = { { 1, 2, 3, 4, 5, 6 } }; break;
        case 6: stacks = { { 1, 2 }, { 3, 4 }, { 5, 6 } }; break;
        default: stacks = { { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 } }; break;
    }
    auto in = r.reduced (8.0f);
    const float bw = 22.0f, bh = 15.0f;
    const float colW = in.getWidth() / (float) stacks.size();
    const float rowH = std::min (24.0f, in.getHeight() / 6.5f);
    Point<float> carrierPos[6];
    int nc = 0;
    for (size_t c = 0; c < stacks.size(); ++c)
    {
        Point<float> prev;
        for (size_t k = 0; k < stacks[c].size(); ++k)
        {
            const int op = stacks[c][k];
            if (op == 0) continue;
            const float cx = in.getX() + colW * ((float) c + 0.5f);
            const float cy = in.getBottom() - 12.0f - rowH * (float) k;
            auto box = Rectangle<float> (bw, bh).withCentre ({ cx, cy });
            if (k > 0 && stacks[c][k - 1] != 0) Draw::glowLine (g, prev, { cx, cy + bh * 0.5f }, C::cyanDim, 1.0f);
            if (algo == 4 && c > 0) Draw::glowLine (g, { cx, cy + bh * 0.5f }, { in.getX() + colW * 0.5f, in.getBottom() - 12.0f - bh * 0.5f }, C::cyanDim, 1.0f);
            const bool carrier = k == 0;
            g.setColour (carrier ? C::amber.withAlpha (0.25f) : C::void_);
            g.fillRect (box);
            g.setColour (carrier ? C::amber : C::cyan);
            g.drawRect (box, 1.0f);
            g.setFont (Fonts::mono (10.0f));
            g.drawText (::racoon::i18n::tr (String (op)), box, Justification::centred);
            prev = { cx, cy - bh * 0.5f };
            if (carrier && nc < 6) carrierPos[nc++] = { cx, cy + bh * 0.5f };
        }
    }
    g.setColour (C::ash);
    g.setFont (Fonts::mono (9.5f));
    g.drawText (::racoon::i18n::tr (fmAlgoNames()[algo]), r.reduced (6.0f, 2.0f), Justification::topLeft);
}

// ===================================================================== envelope sketch
EnvSketch::EnvSketch (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
EnvSketch::~EnvSketch() { ctx.unregisterAnimated (this); }

void EnvSketch::animate()
{
    if (! animVisible (this)) return;
    int64 h = ctx.track();
    for (int p : { T_ENV_MODE, T_ENV_ATTACK, T_ENV_HOLD, T_ENV_DECAY, T_ENV_SUSTAIN, T_ENV_RELEASE, T_ENV_CURVE, T_PENV_AMT, T_PENV_TIME, T_FENV_AMT, T_FENV_DECAY })
        h = h * 31 + (int64) (ctx.getParam (ctx.tid (p)) * 1000.0f);
    if (h != last) { last = h; repaint(); }
}

void EnvSketch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (8.0f, 10.0f);
    const int mode = (int) ctx.getParam (ctx.tid (T_ENV_MODE));
    const float a = ctx.getParam (ctx.tid (T_ENV_ATTACK)), h = mode == 0 ? 0.0f : ctx.getParam (ctx.tid (T_ENV_HOLD));
    const float d = ctx.getParam (ctx.tid (T_ENV_DECAY)), s = ctx.getParam (ctx.tid (T_ENV_SUSTAIN)), rel = ctx.getParam (ctx.tid (T_ENV_RELEASE));
    const float c = ctx.getParam (ctx.tid (T_ENV_CURVE));
    const float gate = mode == 2 ? 0.25f : 0.0f;
    const float total = std::max (0.05f, a + h + std::min (d, 2.0f) + gate + (mode == 2 ? rel : 0.0f));
    auto X = [&] (float t) { return in.getX() + in.getWidth() * t / total; };
    auto Y = [&] (float v) { return in.getBottom() - v * in.getHeight(); };
    Path p;
    p.startNewSubPath (X (0), Y (0));
    p.lineTo (X (a), Y (1));
    if (h > 0) p.lineTo (X (a + h), Y (1));
    const float dEnd = std::min (d, 2.0f);
    const float target = mode == 2 ? s : 0.0f;
    float lvl = 1.0f;
    for (int i = 1; i <= 40; ++i)
    {
        const float t = (float) i / 40.0f;
        lvl = target + (1.0f - target) * (1.0f - curveFn (t * dEnd / d, c));
        p.lineTo (X (a + h + dEnd * t), Y (lvl));
    }
    if (mode == 2)
    {
        p.lineTo (X (a + h + dEnd + gate), Y (lvl));
        for (int i = 1; i <= 20; ++i)
        {
            const float t = (float) i / 20.0f;
            p.lineTo (X (a + h + dEnd + gate + rel * t), Y (lvl * (1.0f - curveFn (t, 0.6f))));
        }
    }
    Path f (p); f.lineTo (X (total), Y (0)); f.closeSubPath();
    g.setColour (C::amber.withAlpha (0.07f)); g.fillPath (f);
    Draw::glowPath (g, p, C::amber, 1.4f);
    // pitch envelope
    const float pa = ctx.getParam (ctx.tid (T_PENV_AMT)), pt = ctx.getParam (ctx.tid (T_PENV_TIME));
    if (std::abs (pa) > 0.05f)
    {
        Path pp;
        for (int i = 0; i <= 60; ++i)
        {
            const float t = total * (float) i / 60.0f;
            const float v = 0.5f + 0.45f * (pa / 48.0f) * std::exp (-t / std::max (0.001f, pt * 0.3f));
            if (i == 0) pp.startNewSubPath (X (t), Y (v)); else pp.lineTo (X (t), Y (v));
        }
        Draw::glowPath (g, pp, C::cyan, 1.0f);
    }
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::ash);
    g.drawText (::racoon::i18n::tr (String (total, 2) + " s"), r.reduced (6.0f, 2.0f), Justification::bottomRight);
    g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr ("AMP"), r.reduced (6.0f, 2.0f), Justification::topLeft);
    if (std::abs (pa) > 0.05f) { g.setColour (C::cyan); g.drawText (::racoon::i18n::tr ("PITCH"), r.reduced (40.0f, 2.0f), Justification::topLeft); }
}

// ===================================================================== track filter curve
TrackFilterCurve::TrackFilterCurve (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
TrackFilterCurve::~TrackFilterCurve() { ctx.unregisterAnimated (this); }

void TrackFilterCurve::animate()
{
    if (! animVisible (this)) return;
    const int t = ctx.track();
    int64 h = t;
    for (int p : { T_F_TYPE, T_F_CUTOFF, T_F_RES, T_F_DRIVE }) h = h * 31 + (int64) (ctx.getParam (ctx.tid (p)) * 100.0f);
    h = h * 31 + (int64) (ctx.proc.engine.telemetry.mod[t][T_F_CUTOFF].load() * 300.0f);
    if (h != last) { last = h; repaint(); }
}

void TrackFilterCurve::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (6.0f);
    const int choice = (int) ctx.getParam (ctx.tid (T_F_TYPE));
    const int type = mapFilterType (choice);
    if (type < 0)
    {
        g.setFont (Fonts::body (13.0f)); g.setColour (C::ash);
        g.drawText (::racoon::i18n::tr ("Filter off"), in, Justification::centred);
        return;
    }
    auto* cp = ctx.proc.trackParamObj (ctx.track(), T_F_CUTOFF);
    const float mod = ctx.proc.engine.telemetry.mod[ctx.track()][T_F_CUTOFF].load();
    const float cutoff = cp->convertFrom0to1 (jlimit (0.0f, 1.0f, cp->getValue() + mod));
    const float res = ctx.getParam (ctx.tid (T_F_RES));
    const double sr = 48000.0;
    const float q = 0.5f + res * res * 24.0f;
    // magnitude of a 2-pole SVF prototype (doubled for 24 dB types)
    const bool steep = choice == 2 || choice == 4 || choice == 7;
    Path p;
    for (int x = 0; x <= (int) in.getWidth(); ++x)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) x / in.getWidth());
        const std::complex<double> s (0.0, hz / cutoff);
        const auto den = s * s + s / (double) q + 1.0;
        std::complex<double> H;
        switch ((FilterType) type)
        {
            case FilterType::HP12: case FilterType::HP24: H = s * s / den; break;
            case FilterType::BP: H = s / (double) q / den * (double) q * 0.5; break;
            case FilterType::Notch: H = (s * s + 1.0) / den; break;
            case FilterType::Comb: H = std::complex<double> (0.5 + 0.5 * std::cos (2.0 * MathConstants<double>::pi * hz / cutoff), 0.0); break;
            default: H = 1.0 / den; break;
        }
        double m = std::abs (H);
        if (steep) m *= std::abs (H);
        juce::ignoreUnused (sr);
        const float db = jlimit (-48.0f, 24.0f, (float) (20.0 * std::log10 (std::max (m, 1.0e-6))));
        const Point<float> pt (in.getX() + (float) x, in.getY() + in.getHeight() * (24.0f - db) / 72.0f);
        if (x == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    Draw::glowPath (g, p, C::cyan, 1.3f);
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr (cp->getCurrentValueAsText()), r.reduced (6.0f, 2.0f), Justification::topRight);
}

// ===================================================================== circuit morph
CircuitView::CircuitView (Ctx& c) : ctx (c) { ctx.registerAnimated (this); setMouseCursor (MouseCursor::LeftRightResizeCursor); }
CircuitView::~CircuitView() { ctx.unregisterAnimated (this); }

void CircuitView::animate()
{
    if (! animVisible (this)) return;
    const float v = ctx.getParam (ctx.tid (T_CIRCUIT)) + ctx.proc.engine.telemetry.mod[ctx.track()][T_CIRCUIT].load() * 3.0f + (float) ctx.track() * 10.0f;
    const bool on = ctx.getParam (ctx.tid (T_CIRCUIT_ON)) > 0.5f;
    if (std::abs (v - last) > 0.002f || on != lastOn) { last = v; lastOn = on; repaint(); }
}

void CircuitView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (16.0f, 10.0f);
    const bool on = ctx.getParam (ctx.tid (T_CIRCUIT_ON)) > 0.5f;
    const float v = jlimit (0.0f, 3.0f, ctx.getParam (ctx.tid (T_CIRCUIT)));
    const float y = in.getCentreY() + 4.0f;
    Draw::glowLine (g, { in.getX(), y }, { in.getRight(), y }, on ? C::amberDim : C::ridge, 1.2f);
    const char* names[4] { "808", "909", "PCM", "FM" };
    for (int k = 0; k < 4; ++k)
    {
        const float x = in.getX() + in.getWidth() * (float) k / 3.0f;
        const float w = 1.0f - std::min (1.0f, std::abs (v - (float) k));
        Path d; d.addPolygon ({ x, y }, 4, 6.0f + 4.0f * w, MathConstants<float>::pi / 4.0f);
        g.setColour (C::void_); g.fillPath (d);
        g.setColour (on ? C::amber.withAlpha (0.4f + 0.6f * w) : C::ash); g.strokePath (d, PathStrokeType (1.2f));
        g.setFont (Fonts::title (12.0f));
        g.setColour (on && w > 0.01f ? C::amberHot.withAlpha (0.5f + 0.5f * w) : C::boneDim);
        g.drawText (::racoon::i18n::tr (names[k]), Rectangle<float> (x - 30.0f, in.getY() - 6.0f, 60.0f, 14.0f), Justification::centred);
    }
    const float px = in.getX() + in.getWidth() * v / 3.0f;
    g.setColour (on ? C::cyan : C::ash);
    g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre ({ px, y }));
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::ash);
    g.drawText (::racoon::i18n::tr (on ? "drag to morph the circuit" : "circuit morph off  (layers A / B / C used as set)"), r.reduced (8.0f, 2.0f), Justification::bottomLeft);
}

void CircuitView::mouseDown (const MouseEvent& e)
{
    ctx.proc.undo.beginNewTransaction ("Circuit morph");
    if (ctx.getParam (ctx.tid (T_CIRCUIT_ON)) < 0.5f) ctx.setParam (ctx.tid (T_CIRCUIT_ON), 1.0f);
    mouseDrag (e);
}

void CircuitView::mouseDrag (const MouseEvent& e)
{
    auto in = getLocalBounds().toFloat().reduced (16.0f, 10.0f);
    ctx.setParam (ctx.tid (T_CIRCUIT), jlimit (0.0f, 3.0f, 3.0f * (e.position.x - in.getX()) / in.getWidth()));
}

// ===================================================================== collision
CollisionView::CollisionView (Ctx& c) : ctx (c) { k.resize (400); b.resize (400); ctx.registerAnimated (this); }
CollisionView::~CollisionView() { ctx.unregisterAnimated (this); }

void CollisionView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto& tel = ctx.proc.engine.telemetry;
    tel.kickLowFifo.copyLatest (k.data(), (int) k.size());
    tel.bassLowFifo.copyLatest (b.data(), (int) b.size());
    auto in = r.reduced (6.0f, 10.0f);
    float mx = 0.05f;
    for (size_t i = 0; i < k.size(); ++i) mx = std::max ({ mx, k[i], b[i] });
    auto draw = [&] (const std::vector<float>& v, Colour c)
    {
        Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = in.getX() + in.getWidth() * (float) i / (float) (v.size() - 1);
            const float y = in.getBottom() - in.getHeight() * jlimit (0.0f, 1.0f, v[i] / mx);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        Path f (p); f.lineTo (in.getRight(), in.getBottom()); f.lineTo (in.getX(), in.getBottom()); f.closeSubPath();
        g.setColour (c.withAlpha (0.12f)); g.fillPath (f);
        Draw::glowPath (g, p, c, 1.1f);
    };
    draw (b, C::cyan);
    draw (k, C::amber);
    // overlap shading
    for (size_t i = 0; i < k.size(); ++i)
    {
        const float o = std::min (k[i], b[i]) / mx;
        if (o < 0.05f) continue;
        const float x = in.getX() + in.getWidth() * (float) i / (float) (k.size() - 1);
        g.setColour (C::ochre.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, in.getBottom() - in.getHeight() * o, in.getBottom());
    }
    g.setFont (Fonts::mono (9.5f));
    g.setColour (C::amber); g.drawText (::racoon::i18n::tr ("KICK < 150 Hz"), r.reduced (6.0f, 1.0f), Justification::topLeft);
    g.setColour (C::cyan); g.drawText (::racoon::i18n::tr ("BASS < 150 Hz"), r.reduced (6.0f, 1.0f).withTrimmedLeft (90.0f), Justification::topLeft);
}

// ===================================================================== groove
void GrooveView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (6.0f, 8.0f);
    const float w = in.getWidth() / 16.0f;
    g.setColour (C::ridge);
    g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
    for (int i = 0; i < 16; ++i)
    {
        const float x = in.getX() + w * ((float) i + 0.5f);
        const float t = groove.active ? groove.timing[i] : 0.0f;
        const float v = groove.active ? groove.velocity[i] : 0.0f;
        const float tx = x + t * w;
        g.setColour (C::cyan.withAlpha (0.8f));
        g.fillRect (Rectangle<float> (tx - 1.5f, in.getCentreY() - 10.0f, 3.0f, 20.0f));
        const float h = (0.5f + 0.5f * v) * (in.getHeight() * 0.5f - 12.0f);
        g.setColour (C::amber.withAlpha (0.6f));
        g.fillRect (Rectangle<float> (x - w * 0.3f, in.getBottom() - h, w * 0.6f, h));
        g.setColour (i % 4 == 0 ? C::boneDim : C::ash);
        g.drawVerticalLine ((int) x, in.getY(), in.getY() + 6.0f);
    }
    g.setFont (Fonts::mono (9.5f)); g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr (groove.active ? groove.name : String ("no groove captured")), r.reduced (6.0f, 2.0f), Justification::topLeft);
}
} // namespace be::ui

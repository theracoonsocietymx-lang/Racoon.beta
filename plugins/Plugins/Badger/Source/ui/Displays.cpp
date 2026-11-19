#include "Displays.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

namespace
{
void slab (Graphics& g, Rectangle<float> r)
{
    g.setColour (C::void_);
    g.fillRect (r);
    g.setColour (S::ink);
    g.drawRect (r, 2.0f);
    g.setColour (C::ridge.withAlpha (0.4f));
    g.drawLine (r.getX(), r.getBottom() + 0.5f, r.getRight(), r.getBottom() + 0.5f, 0.6f);
    // faint lattice
    g.setColour (Colours::white.withAlpha (0.025f));
    for (int k = 1; k < 8; ++k) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 8.0f), r.getY(), r.getBottom());
    for (int k = 1; k < 4; ++k) g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.0f), r.getX(), r.getRight());
}
}

// ===================================================================== wave display
WaveDisplay::WaveDisplay (Ctx& c, int o) : ctx (c), osc (o)
{
    renderer.prepare (48000.0);
    ctx.registerAnimated (this);
    setMouseCursor (MouseCursor::PointingHandCursor);
}
WaveDisplay::~WaveDisplay() { ctx.unregisterAnimated (this); }

int64 WaveDisplay::stateHash() const
{
    const char* ids[] { "engine", "wave", "table", "sample", "pos", "pa", "pb", "pc", "fold", "shape", "sync", "syncmode", "phase", "spmode" };
    int64 h = 1469598103934665603LL;
    for (auto* id : ids) h = (h ^ (int64) (ctx.getParam (oscId (osc, id)) * 10000.0f)) * 1099511628211LL;
    h ^= (int64) (pointer_sized_int) ctx.proc.displayTable (osc);
    h ^= (int64) (pointer_sized_int) ctx.proc.displaySample (osc) * 3;
    return h;
}

void WaveDisplay::animate()
{
    if (! animVisible (this)) return;
    const auto h = stateHash();
    if (h != lastHash) { lastHash = h; render(); repaint(); }
}

void WaveDisplay::render()
{
    OscSettings s;
    s.on = true;
    s.engine = (int) ctx.getParam (oscId (osc, "engine"));
    s.vaWave = (int) ctx.getParam (oscId (osc, "wave"));
    s.table = ctx.proc.displayTable (osc);
    s.sample = ctx.proc.displaySample (osc);
    s.unison = 1; s.spread = 0; s.blend = 1; s.drift = 0; s.slop = 0;
    s.syncMode = (int) ctx.getParam (oscId (osc, "syncmode"));
    s.syncRatio = ctx.getParam (oscId (osc, "sync"));
    s.xmodType = 0;
    s.startPhase = ctx.getParam (oscId (osc, "phase"));
    s.reset = true; s.keyTrack = 1.0f;
    s.pc = ctx.getParam (oscId (osc, "pc"));
    OscMod m;
    const int N = 512;
    const double hz = 48000.0 / N;
    m.pitchSemis = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
    m.level = 1.0f; m.pan = 0.0f;
    m.pos = ctx.getParam (oscId (osc, "pos"));
    m.pa = ctx.getParam (oscId (osc, "pa"));
    m.pb = ctx.getParam (oscId (osc, "pb"));
    m.detune = 0.0f;
    m.fold = ctx.getParam (oscId (osc, "fold"));
    m.shape = ctx.getParam (oscId (osc, "shape"));
    m.xmod = 0.0f;
    renderer.setSampleRate (48000.0);
    renderer.setControl (s, m, 440.0, 0.0f);
    renderer.noteOn (s, (float) hz, 1234, false);
    renderer.setControl (s, m, 440.0, 0.0f);
    const bool physical = s.engine == (int) Engine::Physical;
    const int cycles = physical ? 8 : 2;
    cycle.assign ((size_t) N, 0.0f);
    for (int c = 0; c < cycles; ++c)
        for (int i = 0; i < N; ++i)
        {
            float l, r;
            renderer.tick (s, 0.0f, l, r);
            if (c == cycles - 1) cycle[(size_t) i] = 0.5f * (l + r);
        }
    float pk = 1.0e-6f;
    for (auto v : cycle) pk = std::max (pk, std::abs (v));
    for (auto& v : cycle) v /= pk;
}

void WaveDisplay::mouseDown (const MouseEvent&) { if (onClick) onClick(); }

void WaveDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto inner = r.reduced (8.0f, 6.0f);
    const int engine = (int) ctx.getParam (oscId (osc, "engine"));
    const bool on = ctx.getParam (oscId (osc, "on")) > 0.5f;
    const Colour main = on ? C::amber : C::ash;

    if (engine == (int) Engine::Sample || engine == (int) Engine::Granular)
    {
        if (const auto* sd = ctx.proc.displaySample (osc))
        {
            const int n = (int) sd->data.size();
            Path p;
            const int W = (int) inner.getWidth();
            for (int x = 0; x < W; ++x)
            {
                const int a = (int) ((float) x / (float) W * (float) n), b = std::max (a + 1, (int) ((float) (x + 1) / (float) W * (float) n));
                float mn = 0, mx = 0;
                for (int i = a; i < b && i < n; i += std::max (1, (b - a) / 32)) { mn = std::min (mn, sd->data[(size_t) i]); mx = std::max (mx, sd->data[(size_t) i]); }
                const float cy = inner.getCentreY();
                g.setColour (main.withAlpha (0.65f));
                g.drawVerticalLine ((int) inner.getX() + x, cy - mx * inner.getHeight() * 0.48f, cy - mn * inner.getHeight() * 0.48f + 1.0f);
            }
            const float pos = ctx.getParam (oscId (osc, "pos"));
            const float px = inner.getX() + inner.getWidth() * pos;
            Draw::glowLine (g, { px, inner.getY() }, { px, inner.getBottom() }, C::cyan, 1.2f);
            if (engine == (int) Engine::Sample && ctx.getParam (oscId (osc, "pb")) > 0.001f)
            {
                const float ls = ctx.getParam (oscId (osc, "pa"));
                const float ll = ctx.getParam (oscId (osc, "pb")) * (1.0f - ls);
                g.setColour (C::jade.withAlpha (0.15f));
                g.fillRect (Rectangle<float> (inner.getX() + inner.getWidth() * ls, inner.getY(), inner.getWidth() * ll, inner.getHeight()));
            }
            g.setFont (Fonts::mono (10.0f)); g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (sd->name + "  root " + MidiMessage::getMidiNoteName ((int) std::round (sd->rootNote), true, true, 3)), inner, Justification::topLeft);
        }
        return;
    }

    // wavetable perspective stack
    const auto* wt = ctx.proc.displayTable (osc);
    const bool tableEngine = engine == (int) Engine::Wavetable || engine == (int) Engine::Additive || engine == (int) Engine::Spectral;
    if (tableEngine && wt != nullptr && wt->numFrames() > 1)
    {
        const int frames = wt->numFrames();
        const int show = std::min (10, frames);
        for (int k = show - 1; k >= 0; --k)
        {
            const int f = (int) std::round ((float) k / (float) std::max (1, show - 1) * (float) (frames - 1));
            const float* d = wt->frameData (f, 0);
            const float ox = (float) k * 2.5f, oy = -(float) k * 2.2f;
            Path p;
            const int W = (int) (inner.getWidth() * 0.86f);
            for (int x = 0; x <= W; x += 2)
            {
                const float v = d[(size_t) (x * (WaveTable::kBaseSize - 1) / W)];
                const Point<float> pt (inner.getX() + (float) x + ox, inner.getCentreY() + oy - v * inner.getHeight() * 0.32f);
                if (x == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (C::cyanDim.withAlpha (0.25f + 0.04f * (float) (show - k)));
            g.strokePath (p, PathStrokeType (0.8f));
        }
    }
    if (cycle.empty()) return;
    Path p;
    const int N = (int) cycle.size();
    for (int i = 0; i < N; ++i)
    {
        const Point<float> pt (inner.getX() + inner.getWidth() * (float) i / (float) (N - 1), inner.getCentreY() - cycle[(size_t) i] * inner.getHeight() * 0.44f);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (C::ridge.withAlpha (0.6f));
    g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
    Draw::glowPath (g, p, main, 1.5f);
    if (tableEngine)
    {
        g.setFont (Fonts::mono (10.0f)); g.setColour (C::boneDim);
        const String name = ctx.proc.hasUserTable (osc) && engine == (int) Engine::Wavetable ? ctx.proc.userTableName (osc) : String();
        g.drawText (::racoon::i18n::tr ((name.isNotEmpty() ? name + "  " : String()) + "POS " + String (roundToInt (ctx.getParam (oscId (osc, "pos")) * 100.0f))), inner, Justification::topLeft);
    }
}

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
        // curve of the segment under the mouse (stored on its right point)
        const auto sp = fromScreen (e.position);
        for (int i = 1; i < (int) pts.size(); ++i) if (sp.x <= pts[(size_t) i].x) { dragIndex = i; break; }
        if (dragIndex > 0) curveStart = pts[(size_t) dragIndex].curve;
    }
}

void ShapeEditor::mouseDrag (const MouseEvent& e)
{
    if (dragIndex < 0) return;
    auto& p = pts[(size_t) dragIndex];
    if (dragCurve)
    {
        p.curve = jlimit (-1.0f, 1.0f, curveStart - (float) e.getDistanceFromDragStartY() / 80.0f);
    }
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
        auto np = fromScreen (e.position);
        pts.push_back (np);
        std::sort (pts.begin(), pts.end(), [] (const ShapePoint& a, const ShapePoint& b) { return a.x < b.x; });
    }
    repaint();
    if (onChange) onChange (pts);
}

// ===================================================================== envelope view
EnvView::EnvView (Ctx& c) : ctx (c)
{
    addChildComponent (editor);
    editor.onChange = [this] (const std::vector<ShapePoint>& p) { ctx.proc.setShape (env, p); };
    ctx.registerAnimated (this);
}
EnvView::~EnvView() { ctx.unregisterAnimated (this); }

void EnvView::setEnv (int e) { env = e; editor.setPoints (ctx.proc.getShape (e)); lastKey = {}; repaint(); }
void EnvView::resized() { editor.setBounds (getLocalBounds()); }

String EnvView::key() const
{
    String k;
    for (auto* id : { "delay", "attack", "hold", "decay", "sustain", "release", "acurve", "dcurve", "rcurve", "mode", "loop" })
        k << String (ctx.getParam (envId (env, id)), 4) << ",";
    return k;
}

void EnvView::animate()
{
    if (! animVisible (this)) return;
    const bool drawn = ctx.getParam (envId (env, "mode")) > 0.5f;
    editor.setVisible (drawn);
    const float v = ctx.proc.engine.telemetry.srcDisplay[ModSrc::Env1 + env].load();
    const auto k = key();
    if (k != lastKey || std::abs (v - live) > 0.01f) { lastKey = k; live = v; repaint(); }
}

void EnvView::paint (Graphics& g)
{
    if (editor.isVisible()) return;
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (8.0f, 8.0f);
    const float d = ctx.getParam (envId (env, "delay")), a = ctx.getParam (envId (env, "attack")), h = ctx.getParam (envId (env, "hold"));
    const float dc = ctx.getParam (envId (env, "decay")), s = ctx.getParam (envId (env, "sustain")), rl = ctx.getParam (envId (env, "release"));
    const float ac = ctx.getParam (envId (env, "acurve")), dcc = ctx.getParam (envId (env, "dcurve")), rc = ctx.getParam (envId (env, "rcurve"));
    // map times on a compressed axis
    auto tw = [] (float t) { return std::sqrt (std::max (0.0f, t)) + 0.04f; };
    const float sus = 0.35f;
    const float total = tw (d) + tw (a) + tw (h) + tw (dc) + sus + tw (rl);
    float x = in.getX();
    const float W = in.getWidth();
    Path p;
    p.startNewSubPath (x, in.getBottom());
    auto seg = [&] (float dur, std::function<float (float)> lv)
    {
        const float w = dur / total * W;
        for (int i = 1; i <= 24; ++i) { const float t = (float) i / 24.0f; p.lineTo (x + w * t, in.getBottom() - lv (t) * in.getHeight()); }
        x += w;
    };
    seg (tw (d), [] (float) { return 0.0f; });
    seg (tw (a), [ac] (float t) { return curveFn (t, -ac); });
    seg (tw (h), [] (float) { return 1.0f; });
    seg (tw (dc), [s, dcc] (float t) { return s + (1.0f - s) * (1.0f - curveFn (t, dcc)); });
    seg (sus, [s] (float) { return s; });
    seg (tw (rl), [s, rc] (float t) { return s * (1.0f - curveFn (t, rc)); });
    Path fill (p); fill.lineTo (x, in.getBottom()); fill.closeSubPath();
    g.setColour (C::amber.withAlpha (0.06f)); g.fillPath (fill);
    Draw::glowPath (g, p, C::amber, 1.5f);
    // live level
    if (live > 0.001f)
    {
        const float y = in.getBottom() - jlimit (0.0f, 1.0f, live) * in.getHeight();
        g.setColour (C::cyan.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, in.getX(), in.getRight());
    }
    g.setFont (Fonts::mono (10.0f)); g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr ("ENV " + String (env + 1) + (env == (int) ctx.getParam ("amp_env") ? "  /  AMP" : "")), in, Justification::topLeft);
}

// ===================================================================== lfo view
LfoView::LfoView (Ctx& c) : ctx (c)
{
    addChildComponent (editor);
    editor.colour = C::cyan;
    editor.onChange = [this] (const std::vector<ShapePoint>& p) { ctx.proc.setShape (kNumEnvs + lfo, p); };
    ctx.registerAnimated (this);
}
LfoView::~LfoView() { ctx.unregisterAnimated (this); }

void LfoView::setLfo (int l) { lfo = l; editor.setPoints (ctx.proc.getShape (kNumEnvs + l)); lastWave = -1; repaint(); }
void LfoView::resized() { editor.setBounds (getLocalBounds()); }

void LfoView::animate()
{
    if (! animVisible (this)) return;
    const int w = (int) ctx.getParam (lfoId (lfo, "wave"));
    editor.setVisible (w == (int) LfoWave::Drawn);
    const float v = ctx.proc.engine.telemetry.srcDisplay[ModSrc::Lfo1 + lfo].load();
    if (w != lastWave || std::abs (v - live) > 0.01f) { lastWave = w; live = v; repaint(); }
}

void LfoView::paint (Graphics& g)
{
    if (editor.isVisible()) return;
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (8.0f, 10.0f);
    const int w = (int) ctx.getParam (lfoId (lfo, "wave"));
    const float ph = ctx.getParam (lfoId (lfo, "phase"));
    Path p;
    Random rnd (7);
    float sh[9]; for (auto& v : sh) v = rnd.nextFloat() * 2.0f - 1.0f;
    for (int i = 0; i <= 200; ++i)
    {
        float x = (float) i / 200.0f * 2.0f + ph;   // two cycles
        x -= std::floor (x);
        float v = 0.0f;
        switch ((LfoWave) w)
        {
            case LfoWave::Sine: v = std::sin (kTwoPi * x); break;
            case LfoWave::Triangle: v = x < 0.25f ? 4 * x : (x < 0.75f ? 2 - 4 * x : 4 * x - 4); break;
            case LfoWave::Square: v = x < 0.5f ? 1.0f : -1.0f; break;
            case LfoWave::SawUp: v = 2 * x - 1; break;
            case LfoWave::SawDown: v = 1 - 2 * x; break;
            case LfoWave::SampleHold: v = sh[(i * 8) / 201]; break;
            case LfoWave::SmoothRandom: { const float f = (float) i / 200.0f * 8.0f; const int k = std::min (7, (int) f); v = lerpf (sh[k], sh[k + 1], 0.5f - 0.5f * std::cos (kPi * (f - (float) k))); break; }
            default: break;
        }
        const Point<float> pt (in.getX() + in.getWidth() * (float) i / 200.0f, in.getCentreY() - v * in.getHeight() * 0.45f);
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
    auto& tel = ctx.proc.engine.telemetry;
    (showPre ? tel.scopePre : tel.scope).copyLatest (buf.data(), (int) buf.size());
    const float note = tel.leadNote.load();
    const double sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    int span = 1024;
    if (note > 0.0f) span = jlimit (64, 2000, (int) (2.0 * sr / noteToHz (note)));
    // trigger: rising zero crossing within the older part of the buffer
    int start = (int) buf.size() - span - 1;
    for (int i = start; i > start - std::min (2000, start - 1); --i)
        if (buf[(size_t) i - 1] < 0.0f && buf[(size_t) i] >= 0.0f) { start = i; break; }
    auto in = r.reduced (4.0f);
    Path p;
    float pk = 0.05f;
    for (int i = 0; i < span; ++i) pk = std::max (pk, std::abs (buf[(size_t) (start + i)]));
    const float scale = small ? 0.9f / pk : 0.45f;
    for (int i = 0; i < span; ++i)
    {
        const float v = jlimit (-1.0f, 1.0f, buf[(size_t) (start + i)] * scale * (small ? 1.0f : 2.0f));
        const Point<float> pt (in.getX() + in.getWidth() * (float) i / (float) (span - 1), in.getCentreY() - v * in.getHeight() * 0.5f);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    Draw::glowPath (g, p, showPre ? C::cyan : C::amber, small ? 1.1f : 1.5f);
}

// ===================================================================== spectrum
SpectrumView::SpectrumView (Ctx& c) : ctx (c)
{
    work.resize (fftSize * 2);
    post.assign (fftSize / 2, 0.0f); pre = post; smoothPost = post; smoothPre = post;
    ctx.registerAnimated (this);
}
SpectrumView::~SpectrumView() { ctx.unregisterAnimated (this); }

void SpectrumView::compute (const be::ScopeFifo& f, std::vector<float>& out)
{
    std::fill (work.begin(), work.end(), 0.0f);
    f.copyLatest (work.data(), fftSize);
    for (int i = 0; i < fftSize; ++i) work[(size_t) i] *= 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (fftSize - 1));
    fft.performFrequencyOnlyForwardTransform (work.data());
    for (int i = 0; i < fftSize / 2; ++i) out[(size_t) i] = work[(size_t) i] * (4.0f / (float) fftSize);
}

void SpectrumView::animate()
{
    if (! animVisible (this)) return;
    sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
    compute (ctx.proc.engine.telemetry.scope, post);
    if (comparePre) compute (ctx.proc.engine.telemetry.scopePre, pre);
    for (size_t i = 0; i < post.size(); ++i)
    {
        smoothPost[i] = std::max (post[i], smoothPost[i] * 0.85f);
        smoothPre[i] = std::max (pre[i], smoothPre[i] * 0.85f);
    }
    repaint();
}

void SpectrumView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (6.0f);
    auto xOf = [&] (float hz) { return in.getX() + in.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto yOf = [&] (float mag) { const float db = jlimit (-96.0f, 6.0f, 20.0f * std::log10 (std::max (mag, 1.0e-7f))); return in.getY() + in.getHeight() * (6.0f - db) / 102.0f; };
    g.setFont (Fonts::mono (9.5f));
    for (float hz : { 30.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (hz);
        g.setColour (Colours::white.withAlpha (0.05f)); g.drawVerticalLine ((int) x, in.getY(), in.getBottom());
        g.setColour (C::ash); g.drawText (::racoon::i18n::tr (hz >= 1000 ? String (hz / 1000.0f, 0) + "k" : String ((int) hz)), Rectangle<float> (x + 2, in.getBottom() - 12, 30, 12), Justification::left);
    }
    auto draw = [&] (const std::vector<float>& m, Colour c, bool fill)
    {
        Path p;
        const float bin = (float) (sr / fftSize);
        bool started = false;
        for (int x = 0; x < (int) in.getWidth(); ++x)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) x / in.getWidth());
            const int k = jlimit (1, (int) m.size() - 2, (int) (hz / bin));
            float mx = m[(size_t) k];
            const float hz2 = 20.0f * std::pow (1000.0f, (float) (x + 1) / in.getWidth());
            for (int j = k + 1; j < (int) (hz2 / bin) && j < (int) m.size(); ++j) mx = std::max (mx, m[(size_t) j]);
            const Point<float> pt (in.getX() + (float) x, yOf (mx));
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
        }
        if (fill)
        {
            Path f (p); f.lineTo (in.getRight(), in.getBottom()); f.lineTo (in.getX(), in.getBottom()); f.closeSubPath();
            g.setColour (c.withAlpha (0.08f)); g.fillPath (f);
        }
        Draw::glowPath (g, p, c, 1.2f);
    };
    if (comparePre) draw (smoothPre, C::cyanDim, false);
    draw (smoothPost, C::amber, true);
}

void HarmonicsView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (8.0f, 6.0f);
    in.removeFromBottom (12.0f);
    const auto& m = spec.magnitudes();
    const float bin = spec.binHz();
    if (f0 <= 0.0f || m.empty())
    {
        g.setFont (Fonts::body (13.0f)); g.setColour (C::ash);
        g.drawText (::racoon::i18n::tr ("Play a note"), in, Justification::centred);
        return;
    }
    float amps[16]; float mx = 1.0e-7f;
    for (int h = 1; h <= 16; ++h)
    {
        const int k = (int) std::round ((float) h * f0 / bin);
        float a = 0.0f;
        for (int j = k - 2; j <= k + 2; ++j) if (j > 0 && j < (int) m.size()) a = std::max (a, m[(size_t) j]);
        amps[h - 1] = a; mx = std::max (mx, a);
    }
    const float bw = in.getWidth() / 16.0f;
    g.setFont (Fonts::mono (9.5f));
    for (int h = 0; h < 16; ++h)
    {
        const float db = 20.0f * std::log10 (amps[h] / mx);
        const float hgt = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * in.getHeight();
        auto bar = Rectangle<float> (in.getX() + (float) h * bw + 2.0f, in.getBottom() - hgt, bw - 4.0f, hgt);
        g.setColour ((h == 0 ? C::cyan : C::amber).withAlpha (0.75f));
        g.fillRect (bar);
        g.setColour (C::ash);
        g.drawText (::racoon::i18n::tr (String (h + 1)), Rectangle<float> (bar.getX(), r.getBottom() - 16.0f, bar.getWidth(), 12.0f), Justification::centred);
    }
}

// ===================================================================== meter
Meter::Meter (Ctx& c, bool h) : ctx (c), horizontal (h) { ctx.registerAnimated (this); }
Meter::~Meter() { ctx.unregisterAnimated (this); }

void Meter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto& t = ctx.proc.engine.telemetry;
    const float pl = t.peakL.load(), pr = t.peakR.load(), rl = t.rmsL.load(), rr = t.rmsR.load();
    holdL = std::max (pl, holdL * 0.97f); holdR = std::max (pr, holdR * 0.97f);
    auto norm = [] (float v) { return jlimit (0.0f, 1.0f, (20.0f * std::log10 (std::max (v, 1.0e-5f)) + 60.0f) / 63.0f); };
    auto bar = [&] (Rectangle<float> b, float pk, float rms, float hold)
    {
        g.setColour (C::engrave); g.fillRect (b);
        auto fillTo = [&] (float v, Colour c)
        {
            auto f = horizontal ? b.withWidth (b.getWidth() * norm (v)) : b.withTop (b.getBottom() - b.getHeight() * norm (v));
            g.setColour (c); g.fillRect (f);
        };
        fillTo (pk, C::amberDim);
        fillTo (rms, pk > 0.99f ? C::ochre : C::amber);
        const float hv = norm (hold);
        g.setColour (hold > 0.99f ? C::ochre : C::amberHot);
        if (horizontal) g.drawVerticalLine ((int) (b.getX() + b.getWidth() * hv), b.getY(), b.getBottom());
        else g.drawHorizontalLine ((int) (b.getBottom() - b.getHeight() * hv), b.getX(), b.getRight());
    };
    if (horizontal)
    {
        auto a = r.removeFromTop (r.getHeight() * 0.5f).reduced (0, 1);
        bar (a, pl, rl, holdL); bar (r.reduced (0, 1), pr, rr, holdR);
    }
    else
    {
        auto a = r.removeFromLeft (r.getWidth() * 0.5f).reduced (1, 0);
        bar (a, pl, rl, holdL); bar (r.reduced (1, 0), pr, rr, holdR);
    }
}

// ===================================================================== XY pad
XYPad::XYPad (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
XYPad::~XYPad() { ctx.unregisterAnimated (this); }

void XYPad::animate()
{
    if (! animVisible (this)) return;
    const float x = ctx.getParam ("xy_x"), y = ctx.getParam ("xy_y");
    if (x != lx || y != ly) { lx = x; ly = y; repaint(); }
}

void XYPad::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (14.0f);
    g.setColour (C::ridge.withAlpha (0.6f));
    g.drawRect (in, 1.0f);
    // concentric rings like a sun stone
    for (int k = 1; k <= 4; ++k)
    {
        g.setColour (C::amber.withAlpha (0.04f * (float) k));
        g.drawEllipse (in.withSizeKeepingCentre (in.getWidth() * (float) k / 4.0f, in.getHeight() * (float) k / 4.0f), 0.8f);
    }
    const char* names[4] { "A", "B", "C", "D" };
    const Point<float> corners[4] { in.getTopLeft(), in.getTopRight(), in.getBottomLeft(), in.getBottomRight() };
    for (int i = 0; i < 4; ++i)
    {
        const bool set = ctx.proc.morphCornerSet (i);
        auto box = Rectangle<float> (20.0f, 20.0f).withCentre (corners[i]);
        g.setColour (C::void_); g.fillRect (box);
        g.setColour (set ? C::amber : C::ash); g.drawRect (box, 1.0f);
        g.setFont (Fonts::title (12.0f));
        g.drawText (::racoon::i18n::tr (names[i]), box, Justification::centred);
    }
    const Point<float> puck (in.getX() + in.getWidth() * lx, in.getY() + in.getHeight() * ly);
    const bool on = ctx.getParam ("xy_on") > 0.5f;
    Draw::glowLine (g, { puck.x, in.getY() }, { puck.x, in.getBottom() }, (on ? C::cyan : C::ash).withAlpha (0.4f), 0.6f);
    Draw::glowLine (g, { in.getX(), puck.y }, { in.getRight(), puck.y }, (on ? C::cyan : C::ash).withAlpha (0.4f), 0.6f);
    g.setColour (on ? C::cyan : C::ash);
    g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre (puck));
    g.setColour (C::void_);
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (puck));
}

void XYPad::mouseDown (const MouseEvent& e) { ctx.proc.undo.beginNewTransaction ("Morph"); mouseDrag (e); }

void XYPad::mouseDrag (const MouseEvent& e)
{
    auto in = getLocalBounds().toFloat().reduced (14.0f);
    ctx.setParam ("xy_x", jlimit (0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth()));
    ctx.setParam ("xy_y", jlimit (0.0f, 1.0f, (e.position.y - in.getY()) / in.getHeight()));
}

// ===================================================================== filter curve
FilterCurve::FilterCurve (Ctx& c, int filter) : ctx (c), f (filter) { ctx.registerAnimated (this); }
FilterCurve::~FilterCurve() { ctx.unregisterAnimated (this); }

void FilterCurve::animate()
{
    if (! animVisible (this)) return;
    int64 h = 0;
    for (auto* id : { "on", "type", "cutoff", "res", "morph", "mix" }) h = h * 31 + (int64) (ctx.getParam (filtId (f, id)) * 1000.0f);
    h = h * 31 + (int64) (ctx.proc.engine.telemetry.modDisplay[ModDst::Filter1 + f * 5].load() * 500.0f);
    if (h != last) { last = h; repaint(); }
}

void FilterCurve::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    slab (g, r);
    auto in = r.reduced (4.0f);
    const bool on = ctx.getParam (filtId (f, "on")) > 0.5f;
    const int type = (int) ctx.getParam (filtId (f, "type"));
    auto* cp = ctx.apvts.getParameter (filtId (f, "cutoff"));
    const float modN = ctx.proc.engine.telemetry.modDisplay[ModDst::Filter1 + f * 5].load();
    const float fc = cp->convertFrom0to1 (jlimit (0.0f, 1.0f, cp->getValue() + modN));
    const float res = ctx.getParam (filtId (f, "res"));
    const float morph = ctx.getParam (filtId (f, "morph"));
    const float q = 0.5f + res * res * 12.0f;
    auto resp = [&] (float hz) -> float
    {
        const float w = hz / fc;
        auto lp2 = [&] (float qq) { return 1.0f / std::sqrt ((1 - w * w) * (1 - w * w) + (w / qq) * (w / qq)); };
        auto hp2 = [&] (float qq) { return w * w * lp2 (qq); };
        auto bp2 = [&] (float qq) { return (w / qq) * lp2 (qq); };
        switch ((FilterType) type)
        {
            case FilterType::LP12: return lp2 (q);
            case FilterType::LP18: return lp2 (q) / std::sqrt (1 + w * w);
            case FilterType::LP24: case FilterType::Ladder: case FilterType::Cascade: return lp2 (q) * lp2 (0.54f);
            case FilterType::LP36: return lp2 (q) * lp2 (0.52f) * lp2 (0.7f);
            case FilterType::HP12: return hp2 (q);
            case FilterType::HP24: return hp2 (q) * hp2 (0.54f);
            case FilterType::BP: return bp2 (q);
            case FilterType::Notch: return std::abs (1 - w * w) * lp2 (q);
            case FilterType::Diode: case FilterType::Acid: return lp2 (q * 0.8f) / std::sqrt (1 + w * w) * (w < 0.2f ? 0.6f + 2.0f * w : 1.0f);
            case FilterType::SVF: return morph < 0.5f ? lerpf (lp2 (q), bp2 (q), morph * 2) : lerpf (bp2 (q), hp2 (q), morph * 2 - 1);
            case FilterType::Comb: return 0.6f + 0.4f * std::cos (kTwoPi * hz / fc) * (0.3f + res * 0.7f);
            case FilterType::Formant: return 0.3f + bp2 (6.0f + res * 10.0f) * 0.5f + bp2 (5.0f) * 0.3f * (w > 1.5f ? 1.0f : 0.0f);
            case FilterType::Morph: { const float m3 = morph * 3; const int s = std::min (2, (int) m3); const float t = m3 - (float) s;
                                      const float o[4] { lp2 (q), bp2 (q), hp2 (q), std::abs (1 - w * w) * lp2 (q) }; return lerpf (o[s], o[s + 1], t); }
            default: return 1.0f;
        }
    };
    Path p;
    for (int x = 0; x <= (int) in.getWidth(); x += 2)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) x / in.getWidth());
        const float db = jlimit (-36.0f, 24.0f, 20.0f * std::log10 (std::max (1.0e-4f, resp (hz))));
        const Point<float> pt (in.getX() + (float) x, in.getY() + in.getHeight() * (24.0f - db) / 60.0f);
        if (x == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (C::ridge.withAlpha (0.4f));
    g.drawHorizontalLine ((int) (in.getY() + in.getHeight() * 24.0f / 60.0f), in.getX(), in.getRight());
    Draw::glowPath (g, p, on ? C::amber : C::ash, 1.3f);
}
} // namespace be::ui

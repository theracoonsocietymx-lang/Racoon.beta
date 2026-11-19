#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== ACID
AcidPage::AcidPage (Ctx& c)
    : ctx (c),
      morph (c, "acid_morph", "ACID MORPH"), cutoff (c, "acid_cutoff", "CUTOFF"), res (c, "acid_res", "RESONANCE"), envMod (c, "acid_envmod", "ENV MOD"),
      decay (c, "acid_decay", "DECAY"), accent (c, "acid_accent", "ACCENT"), slide (c, "acid_slide", "SLIDE TIME"), drive (c, "acid_drive", "SATURATION"),
      calib (c, "acid_calib", "CALIBRATION"), overdrive (c, "acid_overdrive", "OVERDRIVE"), nonlin (c, "acid_nonlin", "NONLINEAR"),
      accVel (c, "acid_accthresh", "ACCENT VEL"),
      mode (c, "v_mode", "PLAY MODE"), priority (c, "v_priority", "PRIORITY"), glideMode (c, "v_glidemode", "GLIDE MODE"), glideCurve (c, "v_glidecurve", "GLIDE CURVE"),
      voices (c, "v_voices", "POLYPHONY"), unison (c, "v_unison", "UNISON STACK"),
      glide (c, "v_glide", "GLIDE"), uniDetune (c, "v_unidetune", "STACK DETUNE"), retrig (c, "v_retrig", "RETRIGGER ENVELOPES")
{
    for (Component* comp : std::initializer_list<Component*> { &acidPanel, &playPanel, &envPanel, &morph, &cutoff, &res, &envMod, &decay, &accent, &slide, &drive,
                                                               &calib, &overdrive, &nonlin, &accVel, &mode, &priority, &glideMode, &glideCurve, &voices,
                                                               &unison, &glide, &uniDetune, &retrig })
        addAndMakeVisible (comp);
    morph.knob.setTooltip ("Ácido\n"_u + "Lleva el bajo de un sonido limpio a un 303 moviendo filtro, envolvente, resonancia y saturación al mismo tiempo."_u);
    ctx.registerAnimated (this);
}

AcidPage::~AcidPage() { ctx.unregisterAnimated (this); }

void AcidPage::animate()
{
    if (! animVisible (this)) return;
    const float m = ctx.getParam ("acid_morph") + ctx.getParam ("acid_decay") + ctx.getParam ("acid_envmod") + ctx.getParam ("acid_accent");
    if (m != lastMorph) { lastMorph = m; repaint (curveArea); }
}

void AcidPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (r.getWidth() * 62 / 100);
    r.removeFromLeft (8);
    acidPanel.setBounds (left);
    auto a = acidPanel.content() + left.getPosition();
    // big morph knob in the centre with a ring of controls
    auto centre = a.withSizeKeepingCentre (220, 250);
    morph.setBounds (centre);
    const Point<int> c = centre.getCentre();
    ParamKnob* ring[] { &cutoff, &res, &envMod, &decay, &accent, &slide, &drive, &calib, &overdrive, &nonlin };
    const int n = 10;
    const float rx = (float) a.getWidth() * 0.37f, ry = (float) a.getHeight() * 0.38f;
    for (int i = 0; i < n; ++i)
    {
        const float ang = MathConstants<float>::twoPi * (float) i / (float) n - MathConstants<float>::halfPi;
        const int x = c.x + (int) (std::cos (ang) * rx), y = c.y + (int) (std::sin (ang) * ry);
        ring[i]->setBounds (Rectangle<int> (86, 92).withCentre ({ x, y }));
    }
    accVel.setBounds (Rectangle<int> (96, 40).withCentre ({ a.getRight() - 60, a.getY() + 30 }));

    auto right = r;
    playPanel.setBounds (right.removeFromTop (right.getHeight() * 55 / 100));
    auto p = playPanel.content() + playPanel.getPosition();
    layoutRow (p.removeFromTop (38), { &mode, &priority }, 8);
    p.removeFromTop (8);
    layoutRow (p.removeFromTop (40), { &voices, &unison }, 8);
    p.removeFromTop (8);
    layoutRow (p.removeFromTop (38), { &glideMode, &glideCurve }, 8);
    p.removeFromTop (6);
    layoutRow (p.removeFromTop (92), { &glide, &uniDetune });
    retrig.setBounds (p.removeFromTop (24));
    right.removeFromTop (8);
    envPanel.setBounds (right);
    curveArea = envPanel.content() + envPanel.getPosition();
}

void AcidPage::paintOverChildren (Graphics& g)
{
    // sun-stone rings behind the morph knob
    auto c = morph.getBounds().toFloat().getCentre();
    for (int k = 0; k < 4; ++k)
    {
        const float rr = 128.0f + (float) k * 9.0f;
        g.setColour (C::amber.withAlpha (0.10f - 0.02f * (float) k));
        g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.0f);
        for (int t = 0; t < 24; ++t)
        {
            const float a = MathConstants<float>::twoPi * (float) t / 24.0f + (float) k * 0.13f;
            g.drawLine (c.x + std::cos (a) * (rr - 4.0f), c.y + std::sin (a) * (rr - 4.0f), c.x + std::cos (a) * rr, c.y + std::sin (a) * rr, 0.8f);
        }
    }
    // accent / slide envelope sketch
    if (curveArea.isEmpty()) return;
    auto r = curveArea.toFloat().reduced (6.0f);
    g.setColour (C::void_); g.fillRect (r);
    const float decayT = ctx.getParam ("acid_decay"), envmod = ctx.getParam ("acid_envmod"), acc = ctx.getParam ("acid_accent");
    auto drawEnv = [&] (float startX, float w, bool accentNote, Colour col)
    {
        Path p;
        const float d = accentNote ? 0.2f : decayT;
        for (int i = 0; i <= 80; ++i)
        {
            const float t = (float) i / 80.0f * 0.8f;
            float v = std::exp (-t / std::max (0.01f, d * 0.35f)) * envmod;
            if (accentNote) v = std::min (1.0f, v + acc * 0.5f * std::exp (-t / 0.6f));
            const Point<float> pt (startX + w * (float) i / 80.0f, r.getBottom() - 10.0f - v * (r.getHeight() - 30.0f));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        Draw::glowPath (g, p, col, 1.4f);
    };
    const float w = r.getWidth() / 3.0f;
    drawEnv (r.getX() + 6.0f, w - 12.0f, false, C::amber);
    drawEnv (r.getX() + w + 6.0f, w - 12.0f, true, C::amberHot);
    // slide: pitch line
    Path s;
    s.startNewSubPath (r.getX() + 2.0f * w + 6.0f, r.getBottom() - 20.0f);
    const float sl = ctx.getParam ("acid_slide");
    s.lineTo (r.getX() + 2.0f * w + 6.0f + (w - 12.0f) * 0.25f, r.getBottom() - 20.0f);
    s.quadraticTo (r.getX() + 2.0f * w + 6.0f + (w - 12.0f) * (0.25f + sl), r.getY() + 20.0f, r.getRight() - 6.0f, r.getY() + 20.0f);
    Draw::glowPath (g, s, C::cyan, 1.4f);
    g.setFont (Fonts::label (11.0f)); g.setColour (C::boneDim);
    g.drawText (::racoon::i18n::tr ("NORMAL"), Rectangle<float> (r.getX(), r.getY() + 2.0f, w, 14.0f), Justification::centred);
    g.drawText (::racoon::i18n::tr ("ACCENT"), Rectangle<float> (r.getX() + w, r.getY() + 2.0f, w, 14.0f), Justification::centred);
    g.drawText (::racoon::i18n::tr ("SLIDE"), Rectangle<float> (r.getX() + 2.0f * w, r.getY() + 2.0f, w, 14.0f), Justification::centred);
}

// ===================================================================== PIANO ROLL
namespace
{
    const char* kLaneNames[] { "VELOCITY", "LARGO", "PROBABILIDAD", "RATCHET", "MICROTIEMPO", "MOD" };
    bool isBlackKey (int pc) { pc = ((pc % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    constexpr int kKbW = 58, kRulerH = 22, kBarH = 10, kTabH = 22, kFlagRowH = 17;
}

StepGrid::StepGrid (Ctx& c)
    : ctx (c),
      laneTabs ({ kLaneNames[0], kLaneNames[1], kLaneNames[2], kLaneNames[3], kLaneNames[4], kLaneNames[5] }, [this] (int l) { lane = l; repaint(); })
{
    data = ctx.proc.getSeqData();
    addAndMakeVisible (laneTabs);
    for (auto* sb : { &hbar, &vbar })
    {
        sb->setAutoHide (false);
        sb->setColour (ScrollBar::thumbColourId, C::ridge.brighter (0.25f));
        sb->setColour (ScrollBar::trackColourId, C::void_.withAlpha (0.6f));
        sb->addListener (this);
        addAndMakeVisible (sb);
    }
    scrollY = (double) (kMaxPitch - 26);   // default view: root .. two octaves above
    setRepaintsOnMouseActivity (false);
    setWantsKeyboardFocus (true);
    setTooltip ("Clic en una nota: seleccionarla (Mayús suma). Arrastrar: mover las seleccionadas; su borde derecho: largo. "
                "Alt+arrastrar: copiar. Arrastrar en un hueco: seleccionar con un recuadro. Doble clic: escribir o borrar. "
                "Flechas: medio tono (Mayús, octava). Cmd+Mayús+flecha: copia una octava arriba o abajo. Cmd+D: duplicar. "
                "Tecla B: lápiz (clic escribe, arrastrar pinta)."_u);
    ctx.registerAnimated (this);
}

StepGrid::~StepGrid() { ctx.unregisterAnimated (this); }

int StepGrid::pattern() const { return jlimit (0, kSeqPatterns - 1, (int) ctx.getParam ("seq_pattern") - 1); }
int StepGrid::length() const  { return jlimit (1, kSeqMaxSteps, (int) ctx.getParam ("seq_length")); }

bool StepGrid::isHead (int i) const
{
    const auto& p = data.patterns[pattern()];
    const auto& st = p.steps[i];
    return st.on && (! st.tie || i == 0 || ! p.steps[i - 1].on);
}

int StepGrid::headOf (int i) const
{
    const auto& p = data.patterns[pattern()];
    if (! p.steps[i].on) return -1;
    while (i > 0 && ! isHead (i)) --i;
    return i;
}

int StepGrid::chainEnd (int head) const
{
    const auto& p = data.patterns[pattern()];
    const int len = length();
    int e = head;
    while (e + 1 < len && p.steps[e + 1].on && p.steps[e + 1].tie) ++e;
    return e;
}

int StepGrid::pitchOf (int head) const
{
    const auto& st = data.patterns[pattern()].steps[head];
    int semis = st.note;
    if (ctx.getParam ("seq_quantize") > 0.5f) semis = Sequencer::quantizeToScale (semis, (int) ctx.getParam ("seq_scale"));
    return semis + 12 * st.octave;
}

void StepGrid::setPitch (SeqStep& st, int p)
{
    p = jlimit ((int) kMinPitch, (int) kMaxPitch, p);
    int oct = 0;
    if (p > 24) oct = (p - 24 + 11) / 12;
    else if (p < 0) oct = -((-p + 11) / 12);
    st.octave = oct;
    st.note = jlimit (0, 24, p - 12 * oct);
}

float StepGrid::xOfStep (double step) const { return (float) rollArea.getX() + (float) ((step - scrollX) * stepW); }
double StepGrid::stepAtX (float x) const    { return scrollX + (double) ((x - (float) rollArea.getX()) / stepW); }
float StepGrid::yOfPitch (int p) const      { return (float) rollArea.getY() + (float) (((double) (kMaxPitch - p) - scrollY) * rowH); }
int StepGrid::pitchAtY (float y) const
{
    const double row = scrollY + (double) ((y - (float) rollArea.getY()) / rowH);
    return jlimit ((int) kMinPitch, (int) kMaxPitch, kMaxPitch - (int) std::floor (row));
}

Rectangle<float> StepGrid::noteRect (int head) const
{
    const auto& p = data.patterns[pattern()];
    const int e = chainEnd (head);
    const float x0 = xOfStep ((double) head + (double) p.steps[head].micro);
    const float x1 = xOfStep ((double) e + (double) jlimit (0.05f, 1.0f, p.steps[e].gate));
    return { x0 + 1.0f, yOfPitch (pitchOf (head)) + 1.0f, std::max (4.0f, x1 - x0 - 2.0f), rowH - 2.0f };
}

Rectangle<float> StepGrid::chordRect (int step, int c) const
{
    const auto& st = data.patterns[pattern()].steps[step];
    const float x0 = xOfStep ((double) step + (double) st.micro);
    const float x1 = xOfStep ((double) step + (double) st.micro + (double) st.chord[c].len);
    return { x0 + 1.0f, yOfPitch (chordPitch (step, c)) + 1.0f, std::max (4.0f, x1 - x0 - 2.0f), rowH - 2.0f };
}

int StepGrid::chordPitch (int step, int c) const
{
    const int p = data.patterns[pattern()].steps[step].chord[c].pitch;
    return ctx.getParam ("seq_quantize") > 0.5f ? Sequencer::quantizeToScale (p, (int) ctx.getParam ("seq_scale")) : p;
}

int StepGrid::rawPitchAt (int step) const
{
    const int h = headOf (step);
    if (h < 0) return -999;
    const auto& st = data.patterns[pattern()].steps[h];
    return st.note + 12 * st.octave;
}

int StepGrid::hitNote (Point<float> pos, bool& onRightEdge, int& chordIdx) const
{
    onRightEdge = false;
    chordIdx = -1;
    const int len = length();
    const auto& pt = data.patterns[pattern()];
    auto edge = [&pos] (Rectangle<float> r) { return pos.x > r.getRight() - std::min (8.0f, r.getWidth() * 0.35f); };
    // chord notes are drawn on top, so they win
    for (int s = 0; s < len; ++s)
    {
        if (! pt.steps[s].on) continue;
        for (int c = 0; c < pt.steps[s].numChord; ++c)
        {
            const auto r = chordRect (s, c).expanded (0.0f, 1.0f);
            if (r.contains (pos)) { onRightEdge = edge (r); chordIdx = c; return s; }
        }
    }
    for (int s = 0; s < len; ++s)
    {
        if (! isHead (s)) continue;
        const auto r = noteRect (s).expanded (0.0f, 1.0f);
        if (r.contains (pos)) { onRightEdge = edge (r); return s; }
    }
    return -1;
}

void StepGrid::eraseChain (int head)
{
    auto& p = pat();
    const int e = chainEnd (head);
    for (int i = head; i <= e; ++i)
    {
        auto& st = p.steps[i];
        st.tie = false;
        if (st.numChord > 0)
        {   // a chord note on this step takes the main slot so it keeps sounding
            setPitch (st, st.chord[0].pitch);
            st.gate = jlimit (0.05f, 1.0f, st.chord[0].len);
            st.removeChord (0);
            st.on = true;
        }
        else st.on = false;
    }
}

bool StepGrid::addNoteAt (int step, int pitch)
{
    auto& st = pat().steps[step];
    if (! st.on)
    {
        st.on = true; st.tie = false;
        st.numChord = 0;
        setPitch (st, pitch);
        if (st.gate < 0.1f) st.gate = 0.5f;
        return true;
    }
    if (rawPitchAt (step) == pitch || st.hasChordPitch (pitch)) return false;
    if (! st.addChord (jlimit ((int) kMinPitch, (int) kMaxPitch, pitch), st.tie ? 0.5f : jlimit (0.25f, 1.0f, st.gate)))
    {
        notice = "Máximo 7 notas por paso"_u;
        noticeTime = Time::getMillisecondCounter();
        return false;
    }
    ensurePoly();
    return true;
}

void StepGrid::ensurePoly()
{
    if ((int) ctx.getParam ("v_mode") == 2) return;
    ctx.setParam ("v_mode", 2.0f);
    notice = "Modo POLY activado para que suenen los acordes";
    noticeTime = Time::getMillisecondCounter();
}

void StepGrid::reload()
{
    if (pattern() != lastPattern) selection.clear();
    seenVersion = ctx.proc.seqVersion.load();
    data = ctx.proc.getSeqData();
    // keep the used notes in view when the pattern changes
    if (rollArea.getHeight() > 0 && drag == Drag::none)
    {
        lastPattern = pattern();
        int lo = 999, hi = -999;
        for (int s = 0; s < length(); ++s)
        {
            if (isHead (s)) { lo = std::min (lo, pitchOf (s)); hi = std::max (hi, pitchOf (s)); }
            if (data.patterns[pattern()].steps[s].on)
                for (int c = 0; c < data.patterns[pattern()].steps[s].numChord; ++c) { lo = std::min (lo, chordPitch (s, c)); hi = std::max (hi, chordPitch (s, c)); }
        }
        if (lo <= hi)
        {
            // widen the vertical view (smaller rows) when the pattern spans more than two octaves
            const float need = (float) (hi - lo + 3);
            const float want = jlimit (25.0f, 49.0f, std::max (rowsInView, need));
            if (want > rowsInView) { rowsInView = want; resized(); }
        }
        const double visRows = (double) rollArea.getHeight() / (double) rowH;
        if (lo <= hi)
        {
            const double topRow = (double) (kMaxPitch - hi);
            const double botRow = (double) (kMaxPitch - lo) + 1.0;
            if (topRow < scrollY || botRow > scrollY + visRows)
                scrollY = jlimit (0.0, std::max (0.0, kNumRows - visRows), (topRow + botRow) * 0.5 - visRows * 0.5);
        }
    }
    resized();
    repaint();
}

void StepGrid::resized()
{
    auto r = getLocalBounds();
    const int laneH = jlimit (60, 110, getHeight() / 5);
    auto bottom = r.removeFromBottom (kTabH + laneH + 2 * kFlagRowH + 6);
    auto ruler = r.removeFromTop (kRulerH);
    auto hb = r.removeFromBottom (kBarH);
    auto vb = r.removeFromRight (kBarH);
    kbArea = r.removeFromLeft (kKbW);
    rollArea = r;
    rulerArea = ruler.withTrimmedLeft (kKbW).withTrimmedRight (kBarH);
    hbar.setBounds (hb.withTrimmedLeft (kKbW).withTrimmedRight (kBarH));
    vbar.setBounds (vb);

    bottom.removeFromTop (6);
    laneTabArea = bottom.removeFromTop (kTabH).withTrimmedLeft (kKbW).withTrimmedRight (kBarH);
    laneTabs.setBounds (laneTabArea);
    auto rest = bottom.withTrimmedRight (kBarH);
    laneLabelArea = rest.removeFromLeft (kKbW);
    laneArea = rest.removeFromTop (laneH);
    flagArea = rest;

    // horizontal zoom: the loop fills the view (rounded up to a bar), down to 18 px per step
    const int len = length();
    const int visSteps = jlimit (16, kSeqMaxSteps, ((len + 15) / 16) * 16);
    stepW = std::max (18.0f, (float) rollArea.getWidth() / (float) visSteps);
    rowH = jlimit (7.0f, 20.0f, (float) rollArea.getHeight() / rowsInView);
    lastLen = len;
    updateScrollBars();
}

void StepGrid::updateScrollBars()
{
    const double visSteps = rollArea.getWidth() / (double) stepW;
    const double visRows = rollArea.getHeight() / (double) rowH;
    scrollX = jlimit (0.0, std::max (0.0, (double) kSeqMaxSteps - visSteps), scrollX);
    scrollY = jlimit (0.0, std::max (0.0, (double) kNumRows - visRows), scrollY);
    hbar.setRangeLimits (0.0, (double) kSeqMaxSteps, dontSendNotification);
    hbar.setCurrentRange (scrollX, visSteps, dontSendNotification);
    vbar.setRangeLimits (0.0, (double) kNumRows, dontSendNotification);
    vbar.setCurrentRange (scrollY, visRows, dontSendNotification);
}

void StepGrid::scrollBarMoved (ScrollBar* sb, double start)
{
    if (sb == &hbar) scrollX = start; else scrollY = start;
    lastUserScroll = Time::getMillisecondCounter();
    repaint();
}

void StepGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (! rollArea.contains (e.getPosition()) && ! kbArea.contains (e.getPosition()) && ! rulerArea.contains (e.getPosition()) && ! laneArea.contains (e.getPosition()))
        return;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {   // vertical zoom around the mouse
        const int anchor = pitchAtY (e.position.y);
        const float anchorY = e.position.y;
        rowsInView = jlimit (12.0f, 49.0f, rowsInView * (w.deltaY > 0 ? 0.85f : 1.18f));
        resized();
        scrollY = (double) (kMaxPitch - anchor) - (double) ((anchorY - (float) rollArea.getY()) / rowH) + 0.5;
        lastUserScroll = Time::getMillisecondCounter();
        updateScrollBars();
        repaint();
        return;
    }
    const bool horiz = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
    const float d = horiz ? (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) : w.deltaY;
    if (horiz || laneArea.contains (e.getPosition())) scrollX -= (double) d * 12.0;
    else scrollY -= (double) d * 10.0;
    lastUserScroll = Time::getMillisecondCounter();
    updateScrollBars();
    repaint();
}

void StepGrid::ensureVisible()
{
    if (playStep < 0 || Time::getMillisecondCounter() - lastUserScroll < 2000) return;
    const double visSteps = rollArea.getWidth() / (double) stepW;
    if ((double) playStep < scrollX || (double) playStep + 1.0 > scrollX + visSteps)
    {
        scrollX = std::floor ((double) playStep / 16.0) * 16.0;
        updateScrollBars();
    }
}

void StepGrid::animate()
{
    if (! animVisible (this)) return;
    if (length() != lastLen) { resized(); repaint(); }
    if (notice.isNotEmpty() && Time::getMillisecondCounter() - noticeTime > 4000) { notice.clear(); repaint(); }
    if (drag == Drag::none && ctx.proc.seqVersion.load() != seenVersion) reload();   // preset, undo, host state
    const int s = ctx.proc.engine.telemetry.seqStep.load();
    if (s != playStep) { playStep = s; ensureVisible(); repaint(); }
}

// ---------------------------------------------------------------- painting
void StepGrid::paint (Graphics& g)
{
    g.setColour (C::void_.withAlpha (0.9f));
    g.fillRect (getLocalBounds());
    paintRuler (g);
    paintRoll (g);
    paintKeyboard (g);
    paintLanes (g);
}

void StepGrid::paintKeyboard (Graphics& g)
{
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (kbArea);
    const int root = (int) ctx.getParam ("seq_root");
    for (int p = kMaxPitch; p >= kMinPitch; --p)
    {
        const float y = yOfPitch (p);
        if (y > (float) kbArea.getBottom() || y + rowH < (float) kbArea.getY()) continue;
        const int pc = (root + p) % 12 < 0 ? (root + p) % 12 + 12 : (root + p) % 12;
        const bool black = isBlackKey (root + p);
        auto r = Rectangle<float> ((float) kbArea.getX(), y, (float) kbArea.getWidth() - 2.0f, rowH);
        g.setColour (black ? C::engrave : C::bone.withAlpha (p == auditionNote ? 1.0f : 0.82f));
        if (black) g.fillRect (r.withWidth (r.getWidth() * 0.62f));
        else g.fillRect (r.reduced (0.0f, 0.5f));
        if (black)
        {
            g.setColour (C::bone.withAlpha (0.82f));
            g.fillRect (r.withTrimmedLeft (r.getWidth() * 0.62f).reduced (0.0f, 0.5f));
            g.setColour (p == auditionNote ? C::amber : C::stone);
            g.fillRect (r.withWidth (r.getWidth() * 0.62f).reduced (0.0f, 1.0f));
        }
        if (p == auditionNote) { g.setColour (C::amber.withAlpha (0.6f)); g.fillRect (r); }
        if (pc == 0 || p == 0)
        {
            const int midi = 36 + root + p;
            g.setFont (Fonts::mono (std::min (11.0f, rowH - 2.0f)));
            g.setColour (p == 0 ? C::amberDim.darker (0.3f) : C::engrave);
            g.drawText (::racoon::i18n::tr (noteNames()[midi % 12] + String (midi / 12 - 2)), r.withTrimmedRight (3.0f), Justification::centredRight);
        }
    }
    g.setColour (C::ridge);
    g.drawVerticalLine (kbArea.getRight() - 1, (float) kbArea.getY(), (float) kbArea.getBottom());
}

void StepGrid::paintRuler (Graphics& g)
{
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (rulerArea);
    const int len = length();
    auto r = rulerArea.toFloat();
    g.setColour (C::stone); g.fillRect (r);
    // loop brace
    const float lx0 = xOfStep (0.0), lx1 = xOfStep ((double) len);
    g.setColour (C::amberDim.withAlpha (0.85f));
    g.fillRect (Rectangle<float> (lx0, r.getY() + 1.0f, lx1 - lx0, 6.0f));
    g.setColour (C::amber);
    g.fillRect (Rectangle<float> (lx1 - 3.0f, r.getY() + 1.0f, 3.0f, 6.0f));
    g.setFont (Fonts::mono (10.5f));
    for (int s = 0; s <= kSeqMaxSteps; ++s)
    {
        const float x = xOfStep ((double) s);
        if (x < r.getX() - 40.0f || x > r.getRight()) continue;
        const bool bar = s % 16 == 0, beat = s % 4 == 0;
        g.setColour (bar ? C::bone.withAlpha (0.7f) : C::ash);
        g.drawVerticalLine ((int) x, r.getBottom() - (bar ? 13.0f : beat ? 8.0f : 4.0f), r.getBottom());
        if (beat && s < kSeqMaxSteps && (stepW >= 22.0f || bar))
        {
            g.setColour (s < len ? (bar ? C::bone : C::boneDim) : C::ash);
            g.drawText (::racoon::i18n::tr (String (s / 16 + 1) + (bar ? "" : "." + String ((s % 16) / 4 + 1))),
                        Rectangle<float> (x + 3.0f, r.getY() + 7.0f, 40.0f, 13.0f), Justification::centredLeft);
        }
    }
    if (playStep >= 0)
    {
        const float x = xOfStep ((double) playStep);
        Path tri; tri.addTriangle (x, r.getBottom() - 7.0f, x + stepW, r.getBottom() - 7.0f, x + stepW * 0.5f, r.getBottom());
        g.setColour (C::cyan); g.fillPath (tri);
    }
}

void StepGrid::paintRoll (Graphics& g)
{
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (rollArea);
    const int len = length();
    const int root = (int) ctx.getParam ("seq_root");
    const bool quant = ctx.getParam ("seq_quantize") > 0.5f;
    const auto& iv = scaleIntervals ((int) ctx.getParam ("seq_scale"));
    auto ra = rollArea.toFloat();

    // pitch rows
    for (int p = kMaxPitch; p >= kMinPitch; --p)
    {
        const float y = yOfPitch (p);
        if (y > ra.getBottom() || y + rowH < ra.getY()) continue;
        const int pcRel = ((p % 12) + 12) % 12;
        const bool inScale = iv.contains (pcRel);
        Colour c = isBlackKey (root + p) ? C::void_ : C::basalt;
        if (quant && ! inScale) c = c.darker (0.6f);
        if (pcRel == 0) c = c.interpolatedWith (C::amberDim, 0.10f);
        g.setColour (c);
        g.fillRect (Rectangle<float> (ra.getX(), y, ra.getWidth(), rowH));
        g.setColour (pcRel == 0 ? C::ridge.brighter (0.2f) : C::engrave);
        g.drawHorizontalLine ((int) (y + rowH) - 1, ra.getX(), ra.getRight());
    }
    // step columns
    for (int s = 0; s <= kSeqMaxSteps; ++s)
    {
        const float x = xOfStep ((double) s);
        if (x < ra.getX() - 1.0f || x > ra.getRight() + 1.0f) continue;
        g.setColour (s % 16 == 0 ? C::ridge.brighter (0.35f) : s % 4 == 0 ? C::ridge : C::stone);
        g.drawVerticalLine ((int) x, ra.getY(), ra.getBottom());
    }
    // outside the loop
    const float lx = xOfStep ((double) len);
    if (lx < ra.getRight())
    {
        g.setColour (C::engrave.withAlpha (0.7f));
        g.fillRect (Rectangle<float> (lx, ra.getY(), ra.getRight() - lx, ra.getHeight()));
    }
    // playhead column
    if (playStep >= 0)
    {
        const float x = xOfStep ((double) playStep);
        g.setColour (C::cyan.withAlpha (0.08f));
        g.fillRect (Rectangle<float> (x, ra.getY(), stepW, ra.getHeight()));
        Draw::glowLine (g, { x, ra.getY() }, { x, ra.getBottom() }, C::cyan, 1.2f);
    }

    const auto& pt = data.patterns[pattern()];
    // slides first (under the notes)
    for (int s = 0; s < len; ++s)
    {
        if (! isHead (s) || ! pt.steps[chainEnd (s)].slide) continue;
        const int e = chainEnd (s);
        const int n = e + 1;
        if (n >= len || ! isHead (n)) continue;
        const auto a = noteRect (s), b = noteRect (n);
        Path sl;
        sl.startNewSubPath (a.getRight() - 2.0f, a.getCentreY());
        sl.cubicTo (b.getX(), a.getCentreY(), a.getRight(), b.getCentreY(), b.getX() + 4.0f, b.getCentreY());
        Draw::glowPath (g, sl, C::cyan, 1.6f);
    }
    // notes
    g.setFont (Fonts::mono (std::min (10.5f, rowH - 1.0f)));
    for (int s = 0; s < len; ++s)
    {
        if (! isHead (s)) continue;
        const auto& st = pt.steps[s];
        const auto r = noteRect (s);
        const float alpha = (0.35f + 0.65f * st.vel) * (0.45f + 0.55f * st.prob);
        Colour body = st.accent ? C::amberHot : C::amber;
        g.setColour (body.withAlpha (alpha));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (body.brighter (0.3f));
        g.drawRoundedRectangle (r, 2.0f, st.accent ? 1.6f : 1.0f);
        if (selection.count ({ s, rawPitchAt (s) }) > 0) { g.setColour (C::cyan); g.drawRoundedRectangle (r.expanded (1.0f), 2.5f, 2.2f); }
        // tie joints
        for (int t = s + 1; t <= chainEnd (s); ++t)
        {
            const float x = xOfStep ((double) t);
            g.setColour (C::void_.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, r.getY() + 3.0f, r.getBottom() - 3.0f);
        }
        // ratchets
        if (st.ratchet > 1)
        {
            g.setColour (C::void_.withAlpha (0.7f));
            for (int k = 1; k < st.ratchet; ++k)
            {
                const float x = xOfStep ((double) s + (double) st.micro + (double) k / (double) st.ratchet);
                g.fillRect (Rectangle<float> (x - 0.75f, r.getY() + 1.0f, 1.5f, r.getHeight() - 2.0f));
            }
        }
        if (r.getWidth() > 24.0f && rowH >= 10.0f)
        {
            const int midi = 36 + root + pitchOf (s);
            g.setColour (C::void_);
            g.drawText (::racoon::i18n::tr (noteNames()[((midi % 12) + 12) % 12] + String (midi / 12 - 2)), r.reduced (3.0f, 0.0f), Justification::centredLeft, false);
        }
    }
    // chord notes
    for (int s = 0; s < len; ++s)
    {
        const auto& st = pt.steps[s];
        if (! st.on) continue;
        const float alpha = (0.35f + 0.65f * st.vel) * (0.45f + 0.55f * st.prob);
        for (int c = 0; c < st.numChord; ++c)
        {
            const auto r = chordRect (s, c);
            const Colour body = st.accent ? C::amberHot : C::amber;
            g.setColour (body.withAlpha (alpha));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (body.brighter (0.3f));
            g.drawRoundedRectangle (r, 2.0f, st.accent ? 1.6f : 1.0f);
            if (selection.count ({ s, st.chord[c].pitch }) > 0) { g.setColour (C::cyan); g.drawRoundedRectangle (r.expanded (1.0f), 2.5f, 2.2f); }
            if (r.getWidth() > 24.0f && rowH >= 10.0f)
            {
                const int midi = 36 + root + chordPitch (s, c);
                g.setColour (C::void_);
                g.drawText (::racoon::i18n::tr (noteNames()[((midi % 12) + 12) % 12] + String (midi / 12 - 2)), r.reduced (3.0f, 0.0f), Justification::centredLeft, false);
            }
        }
    }
    if (! rubberRect.isEmpty())
    {
        g.setColour (C::cyan.withAlpha (0.12f));
        g.fillRect (rubberRect);
        g.setColour (C::cyan);
        g.drawRect (rubberRect, 1.0f);
    }
    if (notice.isNotEmpty() && Time::getMillisecondCounter() - noticeTime < 4000)
    {
        g.setFont (Fonts::label (13.0f));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), ::racoon::i18n::tr (notice)) + 24.0f;
        auto nr = Rectangle<float> (ra.getRight() - w - 10.0f, ra.getY() + 8.0f, w, 24.0f);
        g.setColour (C::cyan);
        g.fillRect (nr);
        g.setColour (C::void_);
        g.drawText (::racoon::i18n::tr (notice), nr, Justification::centred);
    }
}

void StepGrid::paintLanes (Graphics& g)
{
    const int len = length();
    const auto& pt = data.patterns[pattern()];
    // lane labels column
    g.setFont (Fonts::label (11.0f));
    g.setColour (C::boneDim);
    auto lbl = laneLabelArea.toFloat();
    auto laneLbl = lbl.removeFromTop ((float) laneArea.getHeight());
    g.drawFittedText (::racoon::i18n::tr (kLaneNames[lane]), laneLbl.reduced (4.0f).toNearestInt(), Justification::centredLeft, 2);
    const char* flagNames[] { "ACENTO", "SLIDE" };
    const Colour flagCols[] { C::amberHot, C::cyan };
    for (int k = 0; k < 2; ++k)
    {
        g.setColour (flagCols[k].withAlpha (0.8f));
        g.drawText (::racoon::i18n::tr (flagNames[k]), lbl.removeFromTop ((float) kFlagRowH).withTrimmedLeft (4.0f), Justification::centredLeft);
    }

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (laneArea.getUnion (flagArea));
    auto la = laneArea.toFloat();
    g.setColour (C::basalt); g.fillRect (la);
    const bool bip = lane == 4;
    if (bip) { g.setColour (C::ridge); g.drawHorizontalLine ((int) la.getCentreY(), la.getX(), la.getRight()); }
    for (int s = 0; s <= kSeqMaxSteps; ++s)
    {
        const float x = xOfStep ((double) s);
        if (x < la.getX() - 1.0f || x > la.getRight() + 1.0f) continue;
        g.setColour (s % 16 == 0 ? C::ridge.brighter (0.35f) : s % 4 == 0 ? C::ridge : C::stone);
        g.drawVerticalLine ((int) x, la.getY(), (float) flagArea.getBottom());
    }
    auto inner = la.withTrimmedTop (16.0f).withTrimmedBottom (5.0f);
    for (int s = 0; s < kSeqMaxSteps; ++s)
    {
        const float x = xOfStep ((double) s + 0.5);
        if (x < la.getX() - stepW || x > la.getRight() + stepW) continue;
        const auto& st = pt.steps[s];
        const bool inLoop = s < len;
        const bool head = inLoop && isHead (s);
        const bool tieStep = inLoop && st.on && ! head;
        float v = 0.0f; String txt;
        switch (lane)
        {
            case 0: v = st.vel; txt = String (roundToInt (st.vel * 127)); break;
            case 1: v = st.gate; txt = String (roundToInt (st.gate * 100)); break;
            case 2: v = st.prob; txt = String (roundToInt (st.prob * 100)) + "%"; break;
            case 3: v = (float) (st.ratchet - 1) / 3.0f; txt = "x" + String (st.ratchet); break;
            case 4: v = st.micro * 2.0f; txt = String (roundToInt (st.micro * 100)); break;
            case 5: v = st.mod; txt = String (roundToInt (st.mod * 100)); break;
            default: break;
        }
        const Colour col = head ? (st.accent ? C::amberHot : C::amber) : tieStep ? C::amberDim : C::ash.withAlpha (inLoop ? 0.6f : 0.25f);
        float yv;
        if (bip) yv = inner.getCentreY() - v * inner.getHeight() * 0.5f;
        else yv = inner.getBottom() - jlimit (0.0f, 1.0f, v) * inner.getHeight();
        const float base = bip ? inner.getCentreY() : inner.getBottom();
        g.setColour (col);
        g.drawLine (x, base, x, yv, head ? 2.0f : 1.2f);
        g.fillEllipse (x - 3.5f, yv - 3.5f, 7.0f, 7.0f);
        if (head && stepW >= 30.0f)
        {
            g.setFont (Fonts::mono (10.0f));
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (txt), Rectangle<float> (x - stepW * 0.5f, la.getY() + 2.0f, stepW, 12.0f), Justification::centred);
        }
        // flags
        auto fa = flagArea.toFloat();
        const bool fv[] { st.accent, st.slide };
        for (int k = 0; k < 2; ++k)
        {
            auto fr = Rectangle<float> (xOfStep ((double) s) + 2.0f, fa.getY() + (float) (k * kFlagRowH) + 2.0f, stepW - 4.0f, (float) kFlagRowH - 4.0f);
            g.setColour (fv[k] && inLoop ? flagCols[k].withAlpha (st.on ? 0.85f : 0.35f) : C::void_.withAlpha (0.7f));
            g.fillRect (fr);
        }
    }
    if (len < kSeqMaxSteps)
    {
        const float lx = xOfStep ((double) len);
        g.setColour (C::engrave.withAlpha (0.6f));
        g.fillRect (Rectangle<float> (lx, la.getY(), la.getRight() - lx, (float) flagArea.getBottom() - la.getY()));
    }
    if (playStep >= 0)
    {
        const float x = xOfStep ((double) playStep);
        g.setColour (C::cyan.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, la.getY(), (float) flagArea.getBottom());
    }
}

// ---------------------------------------------------------------- editing
void StepGrid::setLaneValue (int step, float y)
{
    auto inner = laneArea.toFloat().withTrimmedTop (16.0f).withTrimmedBottom (5.0f);
    const float t = jlimit (0.0f, 1.0f, (inner.getBottom() - y) / inner.getHeight());
    auto& st = pat().steps[step];
    switch (lane)
    {
        case 0: st.vel = t; break;
        case 1: st.gate = jlimit (0.05f, 1.0f, t); break;
        case 2: st.prob = t; break;
        case 3: st.ratchet = jlimit (1, 4, 1 + roundToInt (t * 3.0f)); break;
        case 4: st.micro = jlimit (-0.5f, 0.5f, t - 0.5f); break;
        case 5: st.mod = t; break;
        default: break;
    }
}

void StepGrid::mouseMove (const MouseEvent& e)
{
    bool edge = false; int ci = -1;
    if (rollArea.contains (e.getPosition()) && hitNote (e.position, edge, ci) >= 0)
        setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor : MouseCursor::UpDownResizeCursor);
    else if (rulerArea.contains (e.getPosition()))
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (MouseCursor::NormalCursor);
}

void StepGrid::mouseDoubleClick (const MouseEvent& e)
{
    bool edge = false; int ci = -1;
    if (! rollArea.contains (e.getPosition())) return;
    const int h = hitNote (e.position, edge, ci);
    if (h < 0)
    {
        // double click on an empty spot writes a note (the pencil writes with one click)
        if (drawMode) return;
        const int st = (int) std::floor (stepAtX (e.position.x));
        if (st < 0 || st >= length()) return;
        const int p = pitchAtY (e.position.y);
        if (addNoteAt (st, p)) { selection = { { st, p } }; commit(); }
        repaint();
        return;
    }
    if (ci >= 0) pat().steps[h].removeChord (ci); else eraseChain (h);
    selection.clear();
    commit(); repaint();
}

namespace
{
    void keyOn (Ctx& ctx, int pitch, float vel)  { ctx.proc.keyboardState.noteOn (1, jlimit (0, 127, 36 + (int) ctx.getParam ("seq_root") + pitch), vel); }
    void keyOff (Ctx& ctx, int pitch)            { ctx.proc.keyboardState.noteOff (1, jlimit (0, 127, 36 + (int) ctx.getParam ("seq_root") + pitch), 0.0f); }
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    const auto pos = e.getPosition();
    const int len = length();
    dragMoved = false;
    drag = Drag::none;
    dragChord = -1;

    if (kbArea.contains (pos))
    {
        auditionNote = pitchAtY (e.position.y);
        keyOn (ctx, auditionNote, 0.8f);
        drag = Drag::key;
        repaint();
        return;
    }
    if (rulerArea.contains (pos)) { drag = Drag::ruler; mouseDrag (e); return; }
    if (rollArea.contains (pos) && ! drawMode)
    {
        grabKeyboardFocus();
        bool edge = false; int ci = -1;
        const int h = hitNote (e.position, edge, ci);
        downPos = e.position;
        lastDStep = lastDPitch = 0; lastDLen = 0.0;
        if (h >= 0)
        {
            if (e.mods.isRightButtonDown())
            {
                if (ci >= 0) pat().steps[h].removeChord (ci); else eraseChain (h);
                selection.clear();
                commit(); repaint(); return;
            }
            const Key k { h, ci >= 0 ? pat().steps[h].chord[ci].pitch : rawPitchAt (h) };
            downKey = k;
            downShift = e.mods.isShiftDown();
            if (e.mods.isShiftDown())
            {
                if (selection.count (k) > 0) { selection.erase (k); repaint(); return; }
                selection.insert (k);
            }
            else if (selection.count (k) == 0) selection = { k };
            dragItems = items();
            dragSel = selection;
            dragCopy = e.mods.isAltDown();
            drag = edge ? Drag::resizeSel : Drag::moveSel;
            auditionNote = ci >= 0 ? chordPitch (h, ci) : pitchOf (h);
            keyOn (ctx, auditionNote, 0.8f);
            repaint();
            return;
        }
        selectionAtDown = e.mods.isShiftDown() ? selection : std::set<Key>();
        selection = selectionAtDown;
        rubberRect = {};
        drag = Drag::rubber;
        repaint();
        return;
    }
    if (rollArea.contains (pos))
    {
        bool edge = false; int ci = -1;
        const int h = hitNote (e.position, edge, ci);
        const bool eraseMods = e.mods.isRightButtonDown() || e.mods.isAltDown();
        if (h >= 0)
        {
            if (eraseMods)
            {
                if (ci >= 0) pat().steps[h].removeChord (ci); else eraseChain (h);
                drag = Drag::erase; repaint(); return;
            }
            dragHead = h; dragChord = ci;
            dragStartPitch = dragPitch = ci >= 0 ? pat().steps[h].chord[ci].pitch : rawPitchAt (h);
            dragStartLen = ci >= 0 ? pat().steps[h].chord[ci].len : 0.0f;
            drag = edge ? Drag::resize : Drag::move;
            return;
        }
        const int s = (int) std::floor (stepAtX (e.position.x));
        if (s < 0 || s >= len) return;
        if (eraseMods) { drag = Drag::erase; return; }
        dragPitch = pitchAtY (e.position.y);
        addNoteAt (s, dragPitch);
        lastPaintStep = s;
        drag = Drag::paint;
        auditionNote = dragPitch;
        const auto& st = pat().steps[s];
        keyOn (ctx, auditionNote, st.accent ? 1.0f : st.vel);
        repaint();
        return;
    }
    if (laneArea.contains (pos))
    {
        const int s = (int) std::floor (stepAtX (e.position.x));
        if (s < 0 || s >= kSeqMaxSteps) return;
        drag = Drag::lane;
        lastPaintStep = s;
        setLaneValue (s, e.position.y);
        repaint();
        return;
    }
    if (flagArea.contains (pos))
    {
        const int s = (int) std::floor (stepAtX (e.position.x));
        if (s < 0 || s >= kSeqMaxSteps) return;
        flagRow = jlimit (0, 1, (pos.y - flagArea.getY()) / kFlagRowH);
        auto& st = pat().steps[s];
        bool& f = flagRow == 0 ? st.accent : st.slide;
        flagSetTo = ! f; f = flagSetTo;
        drag = Drag::flag;
        repaint();
    }
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    const int len = length();
    if (e.getDistanceFromDragStart() > 3) dragMoved = true;
    auto& p = pat();
    switch (drag)
    {
        case Drag::ruler:
        {
            const int newLen = jlimit (1, kSeqMaxSteps, (int) std::round (stepAtX (e.position.x)));
            if (newLen != len) ctx.setParam ("seq_length", (float) newLen);
            repaint();
            return;
        }
        case Drag::key:
        {
            const int n = pitchAtY (e.position.y);
            if (n != auditionNote) { keyOff (ctx, auditionNote); auditionNote = n; keyOn (ctx, n, 0.8f); repaint(); }
            return;
        }
        case Drag::paint:
        {
            const int s = jlimit (0, len - 1, (int) std::floor (stepAtX (e.position.x)));
            if (s == lastPaintStep) return;
            const int a = std::min (s, lastPaintStep), b = std::max (s, lastPaintStep);
            for (int i = a; i <= b; ++i) addNoteAt (i, dragPitch);
            lastPaintStep = s;
            repaint();
            return;
        }
        case Drag::erase:
        {
            if (! rollArea.contains (e.getPosition())) return;
            bool edge = false; int ci = -1;
            const int h = hitNote (e.position, edge, ci);
            if (h >= 0) { if (ci >= 0) p.steps[h].removeChord (ci); else eraseChain (h); repaint(); }
            return;
        }
        case Drag::move:
        {
            if (dragHead < 0) return;
            const int np = jlimit ((int) kMinPitch, (int) kMaxPitch, dragStartPitch + (int) std::round ((e.position.y - (float) e.getMouseDownY()) / -rowH));
            if (np == dragPitch) return;
            auto& st = p.steps[dragHead];
            if (dragChord >= 0)
            {
                if (np == rawPitchAt (dragHead) || st.hasChordPitch (np)) return;
                st.chord[dragChord].pitch = np;
            }
            else
            {
                if (st.hasChordPitch (np)) return;
                const int e2 = chainEnd (dragHead);
                for (int i = dragHead; i <= e2; ++i) setPitch (p.steps[i], np);
            }
            dragPitch = np;
            if (auditionNote != -999) keyOff (ctx, auditionNote);
            auditionNote = dragChord >= 0 ? chordPitch (dragHead, dragChord) : pitchOf (dragHead);
            keyOn (ctx, auditionNote, 0.8f);
            repaint();
            return;
        }
        case Drag::resize:
        {
            if (dragHead < 0) return;
            if (dragChord >= 0)
            {
                auto& st = p.steps[dragHead];
                const double start = (double) dragHead + (double) st.micro;
                st.chord[dragChord].len = (float) jlimit (0.05, std::max (0.05, (double) len - start), stepAtX (e.position.x) - start);
                repaint();
                return;
            }
            const double endPos = jlimit ((double) dragHead + 0.05, (double) len, stepAtX (e.position.x));
            int last = jlimit (dragHead, len - 1, (int) std::ceil (endPos) - 1);
            const int oldEnd = chainEnd (dragHead);
            const auto headSt = p.steps[dragHead];
            for (int i = dragHead + 1; i <= last; ++i)
            {
                auto& st = p.steps[i];
                if (st.on && ! st.tie && i > oldEnd)
                {   // absorbing another note: keep it as a chord note when it has a different pitch
                    const int other = st.note + 12 * st.octave;
                    if (other != headSt.note + 12 * headSt.octave) st.addChord (other, jlimit (0.05f, 1.0f, st.gate));
                }
                st.on = true; st.tie = true;
                st.note = headSt.note; st.octave = headSt.octave;
                st.prob = 1.0f; st.ratchet = 1;
            }
            for (int i = last + 1; i <= oldEnd; ++i)
            {
                auto& st = p.steps[i];
                st.tie = false;
                if (st.numChord > 0) { setPitch (st, st.chord[0].pitch); st.gate = jlimit (0.05f, 1.0f, st.chord[0].len); st.removeChord (0); }
                else st.on = false;
            }
            p.steps[last].gate = jlimit (0.05f, 1.0f, (float) (endPos - (double) last));
            repaint();
            return;
        }
        case Drag::lane:
        {
            const int s = jlimit (0, kSeqMaxSteps - 1, (int) std::floor (stepAtX (e.position.x)));
            const int a = std::min (s, lastPaintStep), b = std::max (s, lastPaintStep);
            for (int i = a; i <= b; ++i) setLaneValue (i, e.position.y);
            lastPaintStep = s;
            repaint();
            return;
        }
        case Drag::flag:
        {
            const int s = jlimit (0, kSeqMaxSteps - 1, (int) std::floor (stepAtX (e.position.x)));
            auto& st = p.steps[s];
            (flagRow == 0 ? st.accent : st.slide) = flagSetTo;
            repaint();
            return;
        }
        case Drag::rubber:
        {
            rubberRect = Rectangle<float> (downPos, e.position);
            selection = selectionAtDown;
            const int L = length();
            for (int st = 0; st < L; ++st)
            {
                if (isHead (st) && noteRect (st).intersects (rubberRect)) selection.insert ({ st, rawPitchAt (st) });
                if (p.steps[st].on)
                    for (int c = 0; c < p.steps[st].numChord; ++c)
                        if (chordRect (st, c).intersects (rubberRect)) selection.insert ({ st, p.steps[st].chord[c].pitch });
            }
            repaint();
            return;
        }
        case Drag::moveSel:
        {
            const int dStep = (int) std::round (stepAtX (e.position.x) - stepAtX (downPos.x));
            const int dPitch = (int) std::round ((e.position.y - downPos.y) / -rowH);
            if (dStep == lastDStep && dPitch == lastDPitch) return;
            applyDrag (dStep, dPitch, 0.0, dragCopy);
            if (dPitch != lastDPitch && auditionNote != -999) { keyOff (ctx, auditionNote); auditionNote += dPitch - lastDPitch; keyOn (ctx, auditionNote, 0.8f); }
            lastDStep = dStep; lastDPitch = dPitch;
            return;
        }
        case Drag::resizeSel:
        {
            const double dLen = std::round ((stepAtX (e.position.x) - stepAtX (downPos.x)) * 20.0) / 20.0;
            if (std::abs (dLen - lastDLen) < 1.0e-9) return;
            applyDrag (0, 0, dLen, false);
            lastDLen = dLen;
            return;
        }
        case Drag::none: default: return;
    }
}

void StepGrid::mouseUp (const MouseEvent&)
{
    if (drag == Drag::rubber || drag == Drag::moveSel || drag == Drag::resizeSel)
    {
        if (auditionNote != -999) keyOff (ctx, auditionNote);
        auditionNote = -999;
        if (drag != Drag::rubber && dragMoved) commit();
        // a click (no drag) on one of several selected notes leaves only that one selected
        if (drag == Drag::moveSel && ! dragMoved && ! downShift) selection = { downKey };
        drag = Drag::none;
        rubberRect = {};
        dragItems.clear();
        repaint();
        return;
    }
    if (auditionNote != -999 && (drag == Drag::key || drag == Drag::paint || drag == Drag::move)) keyOff (ctx, auditionNote);
    auditionNote = -999;
    // a plain click on a note (no drag) removes it, like a draw mode
    if (drag == Drag::move && ! dragMoved && dragHead >= 0)
    {
        if (dragChord >= 0) pat().steps[dragHead].removeChord (dragChord);
        else eraseChain (dragHead);
    }
    if (drag != Drag::none && drag != Drag::key && drag != Drag::ruler) commit();
    drag = Drag::none;
    dragHead = -1; dragChord = -1;
    repaint();
}

void StepGrid::commit() { ctx.proc.setSeqDataUndoable (data); seenVersion = ctx.proc.seqVersion.load(); }

// ---- notes as a list (start, length, pitch): moving, copying and transposing work on the list and the steps are
// written again from it. A note that lands where another one starts becomes a chord note of that step
std::vector<StepGrid::Item> StepGrid::items() const
{
    std::vector<Item> out;
    const auto& pt = data.patterns[pattern()];
    const int L = length();
    for (int st = 0; st < L; ++st)
    {
        if (isHead (st))
        {
            const int e = chainEnd (st);
            Item it;
            it.start = st;
            it.len = (double) (e - st) + (double) jlimit (0.05f, 1.0f, pt.steps[e].gate);
            it.pitch = pt.steps[st].note + 12 * pt.steps[st].octave;
            it.attrs = pt.steps[st];
            it.slide = pt.steps[e].slide;
            out.push_back (it);
        }
        if (pt.steps[st].on)
            for (int c = 0; c < pt.steps[st].numChord; ++c)
            {
                Item it;
                it.start = st;
                it.len = (double) pt.steps[st].chord[c].len;
                it.pitch = pt.steps[st].chord[c].pitch;
                it.attrs = pt.steps[st];
                it.slide = false;
                out.push_back (it);
            }
    }
    return out;
}

void StepGrid::rebuild (std::vector<Item> list)
{
    auto& pt = pat();
    const int L = length();
    for (int st = 0; st < kSeqMaxSteps; ++st)
    {
        auto& x = pt.steps[st];
        x.on = false; x.tie = false; x.numChord = 0; x.slide = false;
    }
    std::stable_sort (list.begin(), list.end(), [] (const Item& a, const Item& b)
    {
        if (a.start != b.start) return a.start < b.start;
        return a.len > b.len;
    });
    bool chords = false;
    for (auto& it : list)
    {
        if (it.start < 0 || it.start >= L) continue;
        auto& st = pt.steps[it.start];
        const int pitch = jlimit ((int) kMinPitch, (int) kMaxPitch, it.pitch);
        if (! st.on)
        {
            st.on = true; st.tie = false;
            st.vel = it.attrs.vel; st.prob = it.attrs.prob; st.ratchet = it.attrs.ratchet;
            st.micro = it.attrs.micro; st.mod = it.attrs.mod; st.accent = it.attrs.accent;
            setPitch (st, pitch);
            const int span = jmax (1, (int) std::ceil (it.len - 1.0e-6));
            int e = it.start;
            for (int i = it.start + 1; i < jmin (L, it.start + span); ++i)
            {
                auto& t = pt.steps[i];
                if (t.on) break;
                t.on = true; t.tie = true;
                setPitch (t, pitch);
                t.prob = 1.0f; t.ratchet = 1;
                e = i;
            }
            pt.steps[e].gate = jlimit (0.05f, 1.0f, (float) (it.len - (double) (e - it.start)));
            pt.steps[e].slide = it.slide;
        }
        else
        {
            if (st.note + 12 * st.octave == pitch || st.hasChordPitch (pitch)) continue;
            if (st.addChord (pitch, (float) jlimit (0.05, (double) (L - it.start), it.len))) chords = true;
        }
    }
    if (chords) ensurePoly();
}

void StepGrid::applyDrag (int dStep, int dPitch, double dLen, bool copy)
{
    // the notes selected when the drag began move together, and none leaves the loop or the keyboard
    const int L = length();
    int minStart = L, maxStart = 0, lo = kMaxPitch, hi = kMinPitch;
    for (auto& it : dragItems)
        if (dragSel.count (keyOf (it)) > 0)
        {
            minStart = jmin (minStart, it.start); maxStart = jmax (maxStart, it.start);
            lo = jmin (lo, it.pitch); hi = jmax (hi, it.pitch);
        }
    if (minStart > maxStart) return;
    dStep = jlimit (-minStart, L - 1 - maxStart, dStep);
    dPitch = jlimit ((int) kMinPitch - lo, (int) kMaxPitch - hi, dPitch);
    std::vector<Item> list;
    std::set<Key> moved;
    for (auto& it : dragItems)
    {
        const bool sel = dragSel.count (keyOf (it)) > 0;
        if (! sel || copy) list.push_back (it);
        if (! sel) continue;
        Item m = it;
        m.start += dStep; m.pitch += dPitch;
        m.len = jlimit (0.05, (double) (L - m.start), it.len + dLen);
        list.push_back (m);
        moved.insert (keyOf (m));
    }
    rebuild (list);
    selection = moved;
    repaint();
}

void StepGrid::setDrawMode (bool on)
{
    drawMode = on;
    notice = on ? "Lápiz: clic escribe, arrastrar pinta, clic en una nota la borra (B para salir)"_u
                : "Selección: clic selecciona, arrastrar mueve, doble clic escribe (B: lápiz)"_u;
    noticeTime = Time::getMillisecondCounter();
    if (onDrawModeChanged) onDrawModeChanged();
    repaint();
}

void StepGrid::selectAllNotes()
{
    selection.clear();
    for (auto& it : items()) selection.insert (keyOf (it));
    repaint();
}

namespace
{
    // the list with the selected notes changed by f (and, with keep, the originals left where they were)
    template <typename F>
    std::pair<std::vector<StepGrid::Item>, std::set<std::pair<int, int>>> changeSelected (const std::vector<StepGrid::Item>& all, const std::set<std::pair<int, int>>& sel, bool keep, F f)
    {
        std::vector<StepGrid::Item> list;
        std::set<std::pair<int, int>> now;
        for (auto& it : all)
        {
            const bool s = sel.count ({ it.start, it.pitch }) > 0;
            if (! s || keep) list.push_back (it);
            if (! s) continue;
            auto m = it;
            f (m);
            list.push_back (m);
            now.insert ({ m.start, m.pitch });
        }
        return { list, now };
    }
}

void StepGrid::transposeSelected (int semis)
{
    if (selection.empty()) return;
    auto [list, now] = changeSelected (items(), selection, false, [semis] (Item& m) { m.pitch = jlimit ((int) kMinPitch, (int) kMaxPitch, m.pitch + semis); });
    rebuild (list); selection = now; commit(); repaint();
}

void StepGrid::duplicateSelectedPitch (int semis)
{
    if (selection.empty()) return;
    auto [list, now] = changeSelected (items(), selection, true, [semis] (Item& m) { m.pitch = jlimit ((int) kMinPitch, (int) kMaxPitch, m.pitch + semis); });
    rebuild (list); selection = now; commit(); repaint();
}

void StepGrid::duplicateSelectedInTime()
{
    if (selection.empty()) return;
    const auto all = items();
    int first = kSeqMaxSteps; double end = 0.0;
    for (auto& it : all) if (selection.count (keyOf (it)) > 0) { first = jmin (first, it.start); end = jmax (end, it.start + it.len); }
    const int span = jmax (1, (int) std::ceil (end - first - 1.0e-6));
    auto [list, now] = changeSelected (all, selection, true, [span] (Item& m) { m.start += span; });
    if (first + span * 2 > length()) ctx.setParam ("seq_length", (float) jmin (kSeqMaxSteps, first + span * 2));
    rebuild (list); selection = now; commit(); repaint();
}

void StepGrid::moveSelected (int steps)
{
    if (selection.empty()) return;
    const auto all = items();
    int lo = kSeqMaxSteps, hi = 0;
    for (auto& it : all) if (selection.count (keyOf (it)) > 0) { lo = jmin (lo, it.start); hi = jmax (hi, it.start); }
    steps = jlimit (-lo, length() - 1 - hi, steps);
    if (steps == 0) return;
    auto [list, now] = changeSelected (all, selection, false, [steps] (Item& m) { m.start += steps; });
    rebuild (list); selection = now; commit(); repaint();
}

void StepGrid::deleteSelected()
{
    if (selection.empty()) return;
    std::vector<Item> list;
    for (auto& it : items()) if (selection.count (keyOf (it)) == 0) list.push_back (it);
    rebuild (list); selection.clear(); commit(); repaint();
}

bool StepGrid::keyPressed (const KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const int ch = (int) CharacterFunctions::toLowerCase ((juce_wchar) code);
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown();
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == KeyPress::escapeKey) { clearSelection(); return true; }
    if (! cmd && ! shift && ch == 'b') { setDrawMode (! drawMode); return true; }
    if (cmd && ch == 'a') { selectAllNotes(); return true; }
    if (cmd && ch == 'd') { duplicateSelectedInTime(); return true; }
    if (cmd && ch == 'c')
    {
        clipboardItems.clear();
        for (auto& it : items()) if (selection.count (keyOf (it)) > 0) clipboardItems.push_back (it);
        return true;
    }
    if (cmd && ch == 'v')
    {
        if (clipboardItems.empty()) return true;
        // right after the selection (or where the notes were copied from, with nothing selected)
        int at = clipboardItems.front().start, first = kSeqMaxSteps;
        for (auto& it : clipboardItems) first = jmin (first, it.start);
        double end = 0.0;
        for (auto& it : items()) if (selection.count (keyOf (it)) > 0) end = jmax (end, it.start + it.len);
        at = end > 0.0 ? (int) std::ceil (end - 1.0e-6) : first;
        auto list = items();
        std::set<Key> now;
        for (auto it : clipboardItems) { it.start += at - first; list.push_back (it); now.insert (keyOf (it)); }
        rebuild (list); selection = now; commit(); repaint();
        return true;
    }
    if (code == KeyPress::upKey || code == KeyPress::downKey)
    {
        const int dir = code == KeyPress::upKey ? 1 : -1;
        if (cmd && shift) duplicateSelectedPitch (12 * dir);
        else transposeSelected ((shift ? 12 : 1) * dir);
        return true;
    }
    if (code == KeyPress::leftKey || code == KeyPress::rightKey)
    {
        const int dir = code == KeyPress::leftKey ? -1 : 1;
        if (shift)
        {
            if (selection.empty()) return true;
            auto [list, now] = changeSelected (items(), selection, false, [this, dir] (Item& m) { m.len = jlimit (0.25, (double) (length() - m.start), m.len + dir); });
            rebuild (list); selection = now; commit(); repaint();
        }
        else moveSelected (dir);
        return true;
    }
    return false;
}

void ExportHandle::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Draw::box (g, r, S::ink, 0.0f, 3.0f, dragging);
    auto body = r.withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
    g.setColour (C::cyan);
    g.setFont (Fonts::label (12.0f));
    g.drawFittedText (::racoon::i18n::tr ("DRAG MIDI"), body.toNearestInt(), Justification::centred, 1);
}

void ExportHandle::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const int pat = jlimit (0, kSeqPatterns - 1, (int) ctx.getParam ("seq_pattern") - 1);
    const auto f = ctx.proc.exportPatternToMidi (pat);
    DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
}

// ===================================================================== SEQ PAGE
SeqPage::SeqPage (Ctx& c)
    : ctx (c),
      mode (c, "seq_mode", "MODO"), rate (c, "seq_rate", "VALOR DEL PASO"), root (c, "seq_root", "TONALIDAD"), scale (c, "seq_scale", "ESCALA"),
      trigger (c, "seq_trigger", "CORRE CON"), arpMode (c, "seq_arpmode", "ARPEGIO"),
      length (c, "seq_length", "PASOS"), arpOct (c, "seq_arpoct", "OCTAVAS ARP"),
      swing (c, "seq_swing", "SWING"), gate (c, "seq_gate", "LARGO"), bpm (c, "seq_bpm", "TEMPO"),
      scaleLock (c, "seq_quantize", "BLOQUEAR ESCALA"), keyTranspose (c, "seq_transpose", "TRANSPONER CON TECLA"), hostSync (c, "seq_hostsync", "SEGUIR TEMPO DEL DAW"), latch (c, "seq_latch", "RETENER NOTAS"),
      exportHandle (c), play (c),
      grid (c)
{
    for (Component* comp : std::initializer_list<Component*> { &top, &gridPanel, &mode, &rate, &root, &scale, &trigger, &arpMode, &length, &arpOct, &swing, &gate, &bpm,
                                                               &scaleLock, &keyTranspose, &hostSync, &latch, &variation, &clear, &copy, &paste, &shiftL, &shiftR,
                                                               &exportHandle, &play, &grid, &pencil })
        addAndMakeVisible (comp);
    pencil.setButtonText (String::fromUTF8 ("LÁPIZ (B)"));
    pencil.setClickingTogglesState (true);
    pencil.setTooltip ("Lápiz: un clic escribe una nota, arrastrar pinta varias y un clic en una nota la borra. "
                       "Apagado, el piano roll selecciona y mueve como el de Racoon. Tecla B."_u);
    pencil.onClick = [this] { grid.setDrawMode (pencil.getToggleState()); };
    grid.onDrawModeChanged = [this] { pencil.setToggleState (grid.isDrawMode(), dontSendNotification); };
    for (int i = 0; i < kSeqPatterns; ++i)
    {
        auto* b = patternBtns.add (new TextButton (String::charToString ((juce_wchar) ('A' + i))));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { ctx.setParam ("seq_pattern", (float) (i + 1)); };
        addAndMakeVisible (b);
    }
    variation.setTooltip ("Inventa una variación del patrón actual sin salirse de la escala."_u);
    clear.setTooltip ("Borra todas las notas del patrón actual."_u);
    copy.setTooltip ("Copia el patrón actual."_u); paste.setTooltip ("Pega el patrón copiado sobre el actual."_u);
    shiftL.setTooltip ("Recorre todas las notas un paso a la izquierda."); shiftR.setTooltip ("Recorre todas las notas un paso a la derecha.");
    variation.onClick = [this]
    {
        auto d = ctx.proc.getSeqData();
        const int p = (int) ctx.getParam ("seq_pattern") - 1;
        Sequencer::makeVariation (d.patterns[p], (int) ctx.getParam ("seq_length"), (int) ctx.getParam ("seq_scale"), 0.6f, (uint32) Time::getMillisecondCounter());
        ctx.proc.setSeqDataUndoable (d);
        grid.reload();
    };
    clear.onClick = [this]
    {
        auto d = ctx.proc.getSeqData();
        const int p = (int) ctx.getParam ("seq_pattern") - 1;
        for (auto& s : d.patterns[p].steps) { s = SeqStep(); s.on = false; }
        ctx.proc.setSeqDataUndoable (d); grid.reload();
    };
    copy.onClick = [this] { auto d = ctx.proc.getSeqData(); clipboard = d.patterns[(int) ctx.getParam ("seq_pattern") - 1]; hasClip = true; };
    paste.onClick = [this]
    {
        if (! hasClip) return;
        auto d = ctx.proc.getSeqData();
        d.patterns[(int) ctx.getParam ("seq_pattern") - 1] = clipboard;
        ctx.proc.setSeqDataUndoable (d); grid.reload();
    };
    auto rotate = [this] (int dir)
    {
        auto d = ctx.proc.getSeqData();
        auto& pat = d.patterns[(int) ctx.getParam ("seq_pattern") - 1];
        const int len = (int) ctx.getParam ("seq_length");
        std::vector<SeqStep> tmp (pat.steps, pat.steps + len);
        for (int i = 0; i < len; ++i) pat.steps[(i + dir + len) % len] = tmp[(size_t) i];
        ctx.proc.setSeqDataUndoable (d); grid.reload();
    };
    shiftL.onClick = [rotate] { rotate (-1); };
    shiftR.onClick = [rotate] { rotate (1); };
    ctx.apvts.addParameterListener ("seq_pattern", this);
    ctx.apvts.addParameterListener ("seq_length", this);
    handleAsyncUpdate();
}

SeqPage::~SeqPage()
{
    ctx.apvts.removeParameterListener ("seq_pattern", this);
    ctx.apvts.removeParameterListener ("seq_length", this);
}

void SeqPage::handleAsyncUpdate()
{
    const int p = (int) ctx.getParam ("seq_pattern") - 1;
    for (int i = 0; i < patternBtns.size(); ++i) patternBtns[i]->setToggleState (i == p, dontSendNotification);
    grid.reload();
}

void SeqPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto t = r.removeFromTop (172);
    top.setBounds (t);
    auto c = top.content() + t.getPosition();
    auto row1 = c.removeFromTop (40);
    play.setBounds (row1.removeFromLeft (110).withTrimmedTop (8));
    row1.removeFromLeft (10);
    mode.setBounds (row1.removeFromLeft (120).reduced (2, 0));
    row1.removeFromLeft (8);
    auto pats = row1.removeFromLeft (240).withTrimmedTop (14);
    for (int i = 0; i < patternBtns.size(); ++i) patternBtns[i]->setBounds (pats.getX() + i * pats.getWidth() / 8 + 1, pats.getY(), pats.getWidth() / 8 - 2, pats.getHeight());
    row1.removeFromLeft (10);
    length.setBounds (row1.removeFromLeft (90));
    row1.removeFromLeft (6);
    rate.setBounds (row1.removeFromLeft (90).reduced (2, 0));
    row1.removeFromLeft (6);
    root.setBounds (row1.removeFromLeft (70).reduced (2, 0));
    scale.setBounds (row1.removeFromLeft (130).reduced (2, 0));
    trigger.setBounds (row1.removeFromLeft (110).reduced (2, 0));
    arpMode.setBounds (row1.removeFromLeft (110).reduced (2, 0));
    arpOct.setBounds (row1.removeFromLeft (90));
    c.removeFromTop (6);
    auto row2 = c.removeFromTop (88);
    swing.setBounds (row2.removeFromLeft (80));
    gate.setBounds (row2.removeFromLeft (80));
    bpm.setBounds (row2.removeFromLeft (80));
    row2.removeFromLeft (14);
    auto toggles = row2.removeFromLeft (380);
    const int th = toggles.getHeight() / 2;
    scaleLock.setBounds (toggles.getX(), toggles.getY() + 6, 175, th - 6);
    keyTranspose.setBounds (toggles.getX() + 180, toggles.getY() + 6, 200, th - 6);
    hostSync.setBounds (toggles.getX(), toggles.getY() + th, 175, th - 6);
    latch.setBounds (toggles.getX() + 180, toggles.getY() + th, 200, th - 6);
    row2.removeFromLeft (14);
    auto btns = row2.withTrimmedTop (20).withHeight (28);
    for (Component* b : std::initializer_list<Component*> { &pencil, &variation, &clear, &copy, &paste, &shiftL, &shiftR })
    {
        const int bw = (b == &shiftL || b == &shiftR) ? 40 : 96;
        b->setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (6);
    }
    exportHandle.setBounds (btns.removeFromLeft (120));
    r.removeFromTop (8);
    gridPanel.setBounds (r);
    grid.setBounds (gridPanel.content() + r.getPosition());
}

// ===================================================================== FX: distortion
DistPanel::DistPanel (Ctx& c, int d)
    : Panel ("DRIVE " + String (d + 1)), on (c, distId (d, "on")), type (c, distId (d, "type")),
      drive (c, distId (d, "drive"), "DRIVE"), tone (c, distId (d, "tone"), "TONE"), mix (c, distId (d, "mix"), "MIX"), out (c, distId (d, "out"), "OUTPUT"),
      shape (c, distId (d, "shape"), "CHARACTER"), xlow (c, distId (d, "xlow"), "LOW X"), xhigh (c, distId (d, "xhigh"), "HIGH X"),
      lowDrive (c, distId (d, "lowdrive"), "LOW DRV"), highDrive (c, distId (d, "highdrive"), "HIGH DRV")
{
    for (Component* comp : std::initializer_list<Component*> { &on, &type, &drive, &tone, &mix, &out, &shape, &xlow, &xhigh, &lowDrive, &highDrive }) addAndMakeVisible (comp);
}

void DistPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (22);
    head.removeFromLeft (76);
    on.setBounds (head.removeFromLeft (24));
    type.setBounds (head.removeFromRight (150));
    r.removeFromTop (4);
    const int rh = r.getHeight() / 2;
    layoutRow (r.removeFromTop (rh), { &drive, &tone, &mix, &out, &shape });
    layoutRow (r, { &xlow, &xhigh, &lowDrive, &highDrive, nullptr });
}

// ===================================================================== FX rack list
FxRackList::FxRackList (Ctx& c, std::function<void (int)> cb) : ctx (c), onSelect (std::move (cb))
{
    const char* keys[kNumFx] { "eq", "comp", "mbcomp", "limiter", "clipper", "chorus", "phaser", "flanger", "delay", "reverb", "freqshift", "ringmod", "transient", "imager", "autofilter", "resonator" };
    for (int i = 0; i < kNumFx; ++i)
    {
        auto* t = toggles.add (new Toggle (c, fxId (keys[i], "on")));
        addAndMakeVisible (t);
    }
}

int FxRackList::rowAt (int y) const { return jlimit (0, kNumFx - 1, y / std::max (1, getHeight() / kNumFx)); }

void FxRackList::resized()
{
    const auto order = ctx.proc.getFxOrder();
    const int rh = getHeight() / kNumFx;
    for (int k = 0; k < kNumFx; ++k) toggles[order[(size_t) k]]->setBounds (6, k * rh, 22, rh);
}

void FxRackList::paint (Graphics& g)
{
    const auto order = ctx.proc.getFxOrder();
    const int rh = getHeight() / kNumFx;
    for (int k = 0; k < kNumFx; ++k)
    {
        const int id = order[(size_t) k];
        auto row = Rectangle<int> (0, k * rh, getWidth(), rh).reduced (0, 1);
        const bool sel = id == selected;
        g.setColour (sel ? S::hi : (k % 2 ? S::ink.withAlpha (0.05f) : Colours::transparentBlack)); g.fillRect (row);
        if (k == dragTo && dragFrom >= 0) { g.setColour (S::ink); g.fillRect (row.getX(), row.getY(), row.getWidth(), 2); }
        g.setFont (Fonts::mono (10.0f)); g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr (String (k + 1).paddedLeft ('0', 2)), row.withTrimmedLeft (32).removeFromLeft (20), Justification::centredLeft);
        g.setFont (Fonts::label (13.0f));
        const bool on = toggles[id]->getToggleState();
        g.setColour (on ? S::ink : S::inkFaint);
        g.drawText (::racoon::i18n::tr (fxNames()[id].toUpperCase()), row.withTrimmedLeft (56), Justification::centredLeft);
        // grip
        g.setColour (S::inkSoft);
        for (int d = 0; d < 3; ++d) g.fillRect ((float) row.getRight() - 16.0f, (float) row.getCentreY() - 4.0f + (float) d * 3.0f, 10.0f, 1.0f);
    }
}

void FxRackList::mouseDown (const MouseEvent& e)
{
    const auto order = ctx.proc.getFxOrder();
    const int k = rowAt (e.y);
    if (e.x > getWidth() - 24) { dragFrom = k; dragTo = k; return; }
    selected = order[(size_t) k];
    repaint();
    if (onSelect) onSelect (selected);
}

void FxRackList::mouseDrag (const MouseEvent& e)
{
    if (dragFrom < 0) return;
    dragTo = rowAt (e.y);
    repaint();
}

void FxRackList::mouseUp (const MouseEvent&)
{
    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo)
    {
        auto order = ctx.proc.getFxOrder();
        std::vector<int> v (order.begin(), order.end());
        const int id = v[(size_t) dragFrom];
        v.erase (v.begin() + dragFrom);
        v.insert (v.begin() + dragTo, id);
        for (int k = 0; k < kNumFx; ++k) order[(size_t) k] = v[(size_t) k];
        ctx.proc.setFxOrder (order);
        resized();
    }
    dragFrom = dragTo = -1;
    repaint();
}

// ===================================================================== FX page
void FxPage::GrMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::engrave); g.fillRect (r);
    const float gr = jlimit (0.0f, 24.0f, -ctx.proc.engine.telemetry.duckGr.load());
    g.setColour (C::cyan);
    g.fillRect (r.withHeight (r.getHeight() * gr / 24.0f));
    g.setFont (Fonts::mono (10.0f)); g.setColour (C::bone);
    g.drawText (::racoon::i18n::tr (String (gr, 1)), r.removeFromBottom (14.0f), Justification::centred);
    if ((int) ctx.getParam ("duck_src") == (int) be::DuckSource::DrumBridge)
    {
        const bool linked = ctx.proc.drumBridgeSeen.load();
        g.setColour (linked ? C::cyan : C::ochre);
        g.drawFittedText (::racoon::i18n::tr (linked ? "LINK" : "SIN LINK"), r.removeFromTop (14.0f).toNearestInt(), Justification::centred, 1, 0.5f);
    }
}

FxPage::FxPage (Ctx& c)
    : ctx (c), d1 (c, 0), d2 (c, 1), distRouting (c, "dist_routing", "ROUTING"), distPosition (c, "dist_position", "POSITION"),
      rack (c, [this] (int id) { showFx (id); }),
      duckSrc (c, "duck_src", "TRIGGER"), duckDiv (c, "duck_div", "RATE"), duckBand (c, "duck_band", "BAND"),
      duckDepth (c, "duck_depth", "DEPTH"), duckAttack (c, "duck_attack", "ATTACK"), duckHold (c, "duck_hold", "HOLD"), duckRelease (c, "duck_release", "RECOVERY"),
      duckCurve (c, "duck_curve", "CURVE"), duckFreq (c, "duck_freq", "BAND FREQ"), duckThresh (c, "duck_thresh", "SC THRESH"),
      duckNote (c, "duck_note", "TRIG NOTE", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); }),
      duckChannel (c, "duck_channel", "TRIG CH", [] (int v) { return v == 0 ? String ("ANY") : String (v); }),
      gr (c)
{
    for (Component* comp : std::initializer_list<Component*> { &d1, &d2, &distGlobal, &rackPanel, &editPanel, &duckPanel, &distRouting, &distPosition, &rack,
                                                               &duckSrc, &duckDiv, &duckBand, &duckDepth, &duckAttack, &duckHold, &duckRelease, &duckCurve,
                                                               &duckFreq, &duckThresh, &duckNote, &duckChannel, &gr })
        addAndMakeVisible (comp);
    duckSrc.box.setTooltip ("Hace espacio para el bombo bajando el bajo. Tempo lo hace a tiempo, MIDI Note con una nota, Sidechain con la entrada lateral y Hamster lo hace solo, sin ruteo, leyendo el patrón del bombo de Racoon Hamster."_u);
    showFx (13);
}

void FxPage::showFx (int id)
{
    fxControls.clear();
    struct K { const char* id; const char* label; int kind; };   // 0 knob, 1 toggle, 2 choice
    std::vector<K> ks;
    const char* key = "";
    switch ((FxId) id)
    {
        case FxId::EQ: key = "eq"; ks = { { "hpf", "LOW CUT", 0 }, { "lsfreq", "LS FREQ", 0 }, { "lsgain", "LS GAIN", 0 }, { "p1freq", "P1 FREQ", 0 }, { "p1gain", "P1 GAIN", 0 },
                                           { "p1q", "P1 Q", 0 }, { "p2freq", "P2 FREQ", 0 }, { "p2gain", "P2 GAIN", 0 }, { "p2q", "P2 Q", 0 }, { "hsfreq", "HS FREQ", 0 }, { "hsgain", "HS GAIN", 0 } }; break;
        case FxId::Compressor: key = "comp"; ks = { { "thresh", "THRESH", 0 }, { "ratio", "RATIO", 0 }, { "attack", "ATTACK", 0 }, { "release", "RELEASE", 0 }, { "makeup", "MAKEUP", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Multiband: key = "mbcomp"; ks = { { "xlow", "LOW X", 0 }, { "xhigh", "HIGH X", 0 }, { "low", "LOW THR", 0 }, { "mid", "MID THR", 0 }, { "high", "HIGH THR", 0 }, { "ratio", "RATIO", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Limiter: key = "limiter"; ks = { { "ceiling", "CEILING", 0 }, { "release", "RELEASE", 0 } }; break;
        case FxId::Clipper: key = "clipper"; ks = { { "drive", "DRIVE", 0 }, { "ceiling", "CEILING", 0 }, { "knee", "SOFTNESS", 0 } }; break;
        case FxId::Chorus: key = "chorus"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "delay", "DELAY", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Phaser: key = "phaser"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "centre", "CENTRE", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Flanger: key = "flanger"; ks = { { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "delay", "DELAY", 0 }, { "feedback", "FEEDBACK", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::Delay: key = "delay"; ks = { { "time", "TIME", 0 }, { "div", "DIVISION", 2 }, { "feedback", "FEEDBACK", 0 }, { "highcut", "HIGH CUT", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 }, { "sync", "SYNC", 1 }, { "pingpong", "PING PONG", 1 } }; break;
        case FxId::Reverb: key = "reverb"; ks = { { "size", "SIZE", 0 }, { "damp", "DAMPING", 0 }, { "width", "WIDTH", 0 }, { "predelay", "PRE-DELAY", 0 }, { "lowcut", "LOW CUT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::FreqShift: key = "freqshift"; ks = { { "shift", "SHIFT", 0 }, { "mix", "MIX", 0 } }; break;
        case FxId::RingMod: key = "ringmod"; ks = { { "freq", "FREQ", 0 }, { "ratio", "RATIO", 0 }, { "mix", "MIX", 0 }, { "mode", "MODE", 2 } }; break;
        case FxId::Transient: key = "transient"; ks = { { "attack", "ATTACK", 0 }, { "sustain", "SUSTAIN", 0 }, { "speed", "SPEED", 0 } }; break;
        case FxId::Imager: key = "imager"; ks = { { "width", "WIDTH", 0 }, { "monofreq", "MONO FREQ", 0 }, { "bassmono", "BASS MONO", 1 } }; break;
        case FxId::AutoFilter: key = "autofilter"; ks = { { "cutoff", "CUTOFF", 0 }, { "res", "RESO", 0 }, { "rate", "RATE", 0 }, { "depth", "DEPTH", 0 }, { "mix", "MIX", 0 }, { "type", "TYPE", 2 }, { "div", "DIVISION", 2 }, { "sync", "SYNC", 1 } }; break;
        case FxId::Resonator: key = "resonator"; ks = { { "tune", "TUNE", 0 }, { "decay", "DECAY", 0 }, { "bright", "BRIGHT", 0 }, { "mix", "MIX", 0 } }; break;
    }
    for (auto& k : ks)
    {
        Component* comp = nullptr;
        if (k.kind == 0) comp = new ParamKnob (ctx, fxId (key, k.id), k.label);
        else if (k.kind == 1) comp = new Toggle (ctx, fxId (key, k.id), k.label);
        else comp = new ChoiceBox (ctx, fxId (key, k.id), k.label);
        comp->getProperties().set ("kind", k.kind);
        fxControls.add (comp);
        addAndMakeVisible (comp);
    }
    editPanel.setTitle (fxNames()[id].toUpperCase());
    rack.selected = id;
    rack.repaint();
    resized();
}

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (470);
    const int dh = (left.getHeight() - 84) / 2;
    d1.setBounds (left.removeFromTop (dh));
    left.removeFromTop (6);
    d2.setBounds (left.removeFromTop (dh));
    left.removeFromTop (6);
    distGlobal.setBounds (left);
    auto dg = distGlobal.content() + left.getPosition();
    layoutRow (dg, { &distRouting, &distPosition }, 10);
    r.removeFromLeft (8);
    auto mid = r.removeFromLeft (250);
    rackPanel.setBounds (mid);
    rack.setBounds (rackPanel.content() + mid.getPosition());
    r.removeFromLeft (8);
    auto top = r.removeFromTop (r.getHeight() * 48 / 100);
    editPanel.setBounds (top);
    auto ec = editPanel.content() + top.getPosition();
    // knobs first, then toggles/choices row
    std::vector<Component*> knobs, others;
    for (auto* c : fxControls) ((int) c->getProperties()["kind"] == 0 ? knobs : others).push_back (c);
    const int perRow = 6;
    const int kw = ec.getWidth() / perRow;
    auto krow = ec.removeFromTop (std::min (190, ec.getHeight() - 40));
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (krow.getX() + (int) (i % perRow) * kw, krow.getY() + (int) (i / perRow) * 92, kw, 90);
    auto orow = ec.removeFromTop (38);
    for (size_t i = 0; i < others.size(); ++i)
        others[i]->setBounds (orow.getX() + (int) i * 130, orow.getY() + ((int) others[i]->getProperties()["kind"] == 1 ? 14 : 0), 124, (int) others[i]->getProperties()["kind"] == 1 ? 22 : 38);
    r.removeFromTop (8);
    duckPanel.setBounds (r);
    auto dc = duckPanel.content() + r.getPosition();
    gr.setBounds (dc.removeFromRight (34).reduced (4, 4));
    auto c1 = dc.removeFromTop (40);
    layoutRow (c1, { &duckSrc, &duckDiv, &duckBand, &duckNote, &duckChannel }, 8);
    dc.removeFromTop (6);
    layoutRow (dc.removeFromTop (92), { &duckDepth, &duckAttack, &duckHold, &duckRelease, &duckCurve, &duckFreq, &duckThresh });
}
} // namespace be::ui

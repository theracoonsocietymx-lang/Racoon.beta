#include "Mixer.h"
#include "Assets.h"

namespace be
{
using namespace juce;

// ===================================================================== track strip
void TrackStrip::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    phaseL.prepare ((int) (sr * 0.006) + 8); phaseR.prepare ((int) (sr * 0.006) + 8);
    for (int k = 0; k < 3; ++k)
    {
        os[k] = std::make_unique<dsp::Oversampling<float>> (2, (size_t) (k + 1), dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
        os[k]->initProcessing ((size_t) maxBlock);
    }
    reset();
}

void TrackStrip::reset()
{
    for (auto& b : lowSh) b.reset();
    for (auto& b : highSh) b.reset();
    eqCache[0] = eqCache[1] = -999.0f;
    compEnv = gateEnv = trFast = trSlow = 0.0f; gateGain = 1.0f; faderNow = -1.0f;
    crushHold[0] = crushHold[1] = 0.0f; crushCount = 0;
    phaseL.clear(); phaseR.clear();
    for (auto& o : os) if (o) o->reset();
    gainReduction = 0.0f;
}

void TrackStrip::process (const StripSettings& s, float* l, float* r, int n, int quality)
{
    const float fs = (float) sr;
    // polarity and alignment delay
    if (s.invert) for (int i = 0; i < n; ++i) { l[i] = -l[i]; r[i] = -r[i]; }
    if (s.phaseMs > 0.01f)
    {
        const float d = std::max (1.0f, s.phaseMs * 0.001f * fs);
        for (int i = 0; i < n; ++i) { phaseL.push (l[i]); phaseR.push (r[i]); l[i] = phaseL.read (d); r[i] = phaseR.read (d); }
    }
    // drive (oversampled)
    if (s.drive > 0.001f)
    {
        auto shape = [&s] (float x) { return shapeDrive (s.drvType, x, s.drive) * (1.0f / (1.0f + s.drive * 0.6f)); };
        if (quality <= 0)
            for (int i = 0; i < n; ++i) { l[i] = shape (l[i]); r[i] = shape (r[i]); }
        else
        {
            float* ch[2] { l, r };
            dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            auto& o = *os[(size_t) jlimit (0, 2, quality - 1)];
            auto up = o.processSamplesUp (block);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape (d[i]);
            }
            o.processSamplesDown (block);
        }
    }
    // bit / rate crush
    if (s.crush > 0.001f)
    {
        const float bits = 16.0f - s.crush * 13.0f;
        const float q = std::exp2 (bits - 1.0f);
        const int hold = 1 + (int) (s.crush * s.crush * 30.0f);
        for (int i = 0; i < n; ++i)
        {
            if (crushCount++ % hold == 0) { crushHold[0] = std::round (l[i] * q) / q; crushHold[1] = std::round (r[i] * q) / q; }
            l[i] = lerpf (l[i], crushHold[0], std::min (1.0f, s.crush * 3.0f));
            r[i] = lerpf (r[i], crushHold[1], std::min (1.0f, s.crush * 3.0f));
        }
    }
    // shelving EQ
    if (std::abs (s.eqLow) > 0.05f || std::abs (s.eqHigh) > 0.05f)
    {
        if (s.eqLow != eqCache[0]) { for (auto& b : lowSh) b.lowShelf (120.0, s.eqLow, sr); eqCache[0] = s.eqLow; }
        if (s.eqHigh != eqCache[1]) { for (auto& b : highSh) b.highShelf (7000.0, s.eqHigh, sr); eqCache[1] = s.eqHigh; }
        for (int i = 0; i < n; ++i)
        {
            l[i] = highSh[0].process (lowSh[0].process (l[i]));
            r[i] = highSh[1].process (lowSh[1].process (r[i]));
        }
    }
    // transient shaper
    if (std::abs (s.transient) > 0.01f)
    {
        const float af = std::exp (-1.0f / (0.0008f * fs)), rf = std::exp (-1.0f / (0.03f * fs));
        const float as = std::exp (-1.0f / (0.025f * fs)), rs = std::exp (-1.0f / (0.25f * fs));
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            trFast = x + (x > trFast ? af : rf) * (trFast - x);
            trSlow = x + (x > trSlow ? as : rs) * (trSlow - x);
            const float ratio = (trFast + 1.0e-6f) / (trSlow + 1.0e-6f);
            const float g = clampf (std::pow (ratio, s.transient * 1.6f), 0.2f, 5.0f);
            l[i] *= g; r[i] *= g;
        }
    }
    // compressor (one knob)
    float minG = 1.0f;
    if (s.comp > 0.001f)
    {
        const float thr = -s.comp * 30.0f, ratio = 1.0f + s.comp * 7.0f;
        const float att = std::exp (-1.0f / (0.003f * fs)), rel = std::exp (-1.0f / (0.09f * fs));
        const float makeup = dbToGain (-thr * (1.0f - 1.0f / ratio) * 0.45f);
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            compEnv = x + (x > compEnv ? att : rel) * (compEnv - x);
            const float db = gainToDb (compEnv);
            const float over = db - thr;
            const float g = over > 0.0f ? dbToGain (-over * (1.0f - 1.0f / ratio)) : 1.0f;
            minG = std::min (minG, g);
            l[i] *= g * makeup; r[i] *= g * makeup;
        }
    }
    gainReduction = gainToDb (minG);
    // gate (tightens tails)
    if (s.gate > 0.001f)
    {
        const float thr = dbToGain (-70.0f + s.gate * 58.0f);
        const float env = std::exp (-1.0f / (0.005f * fs));
        const float open = std::exp (-1.0f / (0.0004f * fs)), close = std::exp (-1.0f / ((0.004f + (1.0f - s.gate) * 0.05f) * fs));
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            gateEnv = std::max (x, gateEnv * env);
            const float target = gateEnv > thr ? 1.0f : 0.0f;
            gateGain = target + (target > gateGain ? open : close) * (gateGain - target);
            l[i] *= gateGain; r[i] *= gateGain;
        }
    }
    // fader
    // the fader moves sample by sample toward its value in about 20 ms: a moved fader or automation does not zipper
    const float fader = s.faderDb <= -59.9f ? 0.0f : dbToGain (s.faderDb);
    if (faderNow < 0.0f) faderNow = fader;
    if (std::abs (fader - faderNow) < 1.0e-6f)
    {
        faderNow = fader;
        if (std::abs (fader - 1.0f) > 1.0e-5f) for (int i = 0; i < n; ++i) { l[i] *= fader; r[i] *= fader; }
    }
    else
    {
        const float k = 1.0f - std::exp (-1.0f / (0.020f * fs));
        for (int i = 0; i < n; ++i) { faderNow += (fader - faderNow) * k; l[i] *= faderNow; r[i] *= faderNow; }
    }
}

// ===================================================================== aux bus
void AuxBus::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    dsp::ProcessSpec spec { sampleRate, (uint32) maxBlock, 2 };
    reverb.setSampleRate (sampleRate);
    conv.prepare (spec);
    chorus.prepare (spec);
    phaser.prepare (spec);
    for (int c = 0; c < 2; ++c)
    {
        dl[c].prepare ((int) (sr * 2.6));
        pre[c].prepare ((int) (sr * 0.15));
        for (int k = 0; k < 3; ++k) res[c][k].prepare ((int) (sr / 25.0));
    }
    const float ca[4] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    const float cb[4] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) { hilA[c][k].a = ca[k] * ca[k]; hilB[c][k].a = cb[k] * cb[k]; }
    irType = -1;
    reset();
}

void AuxBus::reset()
{
    reverb.reset(); conv.reset(); chorus.reset(); phaser.reset();
    for (int c = 0; c < 2; ++c)
    {
        dl[c].clear(); pre[c].clear(); fb[c] = 0.0f; dlLp[c].reset(); baseNow = -1.0f;
        for (int k = 0; k < 3; ++k) { res[c][k].clear(); resLp[c][k] = 0.0f; }
        lc[c].reset(); hc[c].reset();
    }
    lastLc = lastHc = -1.0f;
}

void AuxBus::updateImpulse (int type, float size, float tone)
{
    if (type < 1 || type > 3) return;
    if (type == irType && std::abs (size - irSize) < 0.04f && std::abs (tone - irTone) < 0.04f) return;
    irType = type; irSize = size; irTone = tone;
    auto ir = makeImpulseResponse (type - 1, size, tone, sr);
    conv.loadImpulseResponse (std::move (ir), sr, dsp::Convolution::Stereo::yes, dsp::Convolution::Trim::no, dsp::Convolution::Normalise::no);
    convReady = true;
}

void AuxBus::process (const AuxSettings& s, float* l, float* r, int n, double bpm)
{
    const float fs = (float) sr;
    // skip all processing once the input has been silent for longer than any tail
    float inPk = 0.0f;
    for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::max (std::abs (l[i]), std::abs (r[i])));
    if (inPk < 1.0e-6f) silentFor += n; else silentFor = 0;
    if (silentFor > (int) (sr * 6.0) || s.ret <= 0.0f) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); level *= 0.9f; return; }
    if (s.type != lastType) { if (lastType >= 0) { for (int c = 0; c < 2; ++c) { dl[c].clear(); fb[c] = 0.0f; } } lastType = s.type; }
    float* ch[2] { l, r };
    switch (s.type)
    {
        case 0:
        {
            racoon::sdsp::Verb::Parameters p;   // the new 8-line FDN reverb, same knobs as the old Freeverb
            p.roomSize = 0.3f + s.p1 * 0.69f; p.damping = s.p2; p.width = s.p3; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
            reverb.setParameters (p);
            const float d = 1.0f + s.p4 * 0.12f * fs;
            for (int i = 0; i < n; ++i) { pre[0].push (l[i]); pre[1].push (r[i]); l[i] = pre[0].read (d); r[i] = pre[1].read (d); }
            reverb.processStereo (l, r, n);
            break;
        }
        case 1: case 2: case 3:
        {
            const float d = 1.0f + s.p4 * 0.1f * fs;
            for (int i = 0; i < n; ++i) { pre[0].push (l[i]); pre[1].push (r[i]); l[i] = pre[0].read (d); r[i] = pre[1].read (d); }
            if (convReady && conv.getCurrentIRSize() > 0)
            {
                dsp::AudioBlock<float> block (ch, 2, (size_t) n);
                conv.process (dsp::ProcessContextReplacing<float> (block));
                // width
                for (int i = 0; i < n; ++i) { const float m = 0.5f * (l[i] + r[i]), sd = 0.5f * (l[i] - r[i]) * s.p3 * 1.4f; l[i] = m + sd; r[i] = m - sd; }
            }
            else for (int i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
            break;
        }
        case 4: case 5:
        {
            static const double divs[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
            const double beats = divs[jlimit (0, 7, roundToInt (s.p1 * 7.0f))];
            const float base = (float) (beats * 60.0 / std::max (bpm, 20.0) * sr);
            const float fbAmt = s.p2 * 0.92f;
            const float spread = s.p3 * 0.012f * fs;
            for (auto& f : dlLp) f.setCutoff (std::min (s.hiCut, 9000.0f), fs);
            if (baseNow < 0.0f) baseNow = base;
            const float glide = 1.0f - std::exp (-1.0f / (0.060f * fs));   // a new division is reached in about 60 ms
            for (int i = 0; i < n; ++i)
            {
                baseNow += (base - baseNow) * glide;
                lfo += 0.6 / sr; lfo -= std::floor (lfo);
                const float wob = s.p4 * 0.003f * fs * std::sin ((float) lfo * kTwoPi);
                const float dLv = clampf (baseNow + wob, 2.0f, (float) dl[0].buf.size() - 8.0f);
                const float dRv = clampf (baseNow + wob + (s.type == 4 ? spread : 0.0f), 2.0f, (float) dl[1].buf.size() - 8.0f);
                const float yl = dl[0].read (dLv), yr = dl[1].read (dRv);
                const float inl = l[i], inr = r[i];
                if (s.type == 4)
                {
                    dl[0].push (inl + dlLp[0].lp (yl) * fbAmt);
                    dl[1].push (inr + dlLp[1].lp (yr) * fbAmt);
                }
                else
                {
                    dl[0].push (0.5f * (inl + inr) + dlLp[0].lp (yr) * fbAmt);
                    dl[1].push (dlLp[1].lp (yl) * fbAmt);
                }
                l[i] = yl; r[i] = yr;
            }
            break;
        }
        case 6:
        {
            chorus.setRate (0.05f + s.p1 * 5.0f); chorus.setDepth (s.p2); chorus.setCentreDelay (5.0f + s.p4 * 20.0f);
            chorus.setFeedback (s.p3 * 0.6f - 0.3f); chorus.setMix (1.0f);
            dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            chorus.process (dsp::ProcessContextReplacing<float> (block));
            break;
        }
        case 7:
        {
            phaser.setRate (0.02f + s.p1 * 4.0f); phaser.setDepth (s.p2); phaser.setCentreFrequency (150.0f + s.p3 * 3000.0f);
            phaser.setFeedback (s.p4 * 0.9f); phaser.setMix (1.0f);
            dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            phaser.process (dsp::ProcessContextReplacing<float> (block));
            break;
        }
        case 8:
        {
            const float rate = 0.02f + s.p1 * 3.0f;
            for (int i = 0; i < n; ++i)
            {
                lfo += rate / sr; lfo -= std::floor (lfo);
                for (int c = 0; c < 2; ++c)
                {
                    const float ph = (float) lfo + (c == 1 ? 0.25f : 0.0f);
                    const float d = (0.4f + s.p4 * 3.0f + s.p2 * 4.0f * (0.5f + 0.5f * std::sin (ph * kTwoPi))) * 0.001f * fs;
                    const float y = dl[c].read (std::max (1.0f, d));
                    dl[c].push (ch[c][i] + y * s.p3 * 0.9f);
                    ch[c][i] = 0.5f * (ch[c][i] + y);
                }
            }
            break;
        }
        case 9:
        {
            const double shift = ((double) s.p1 * 2.0 - 1.0) * 600.0;
            for (int i = 0; i < n; ++i)
            {
                fsPhase += shift / sr; fsPhase -= std::floor (fsPhase);
                for (int k = 0; k < 2; ++k)
                {
                    const double ph = fsPhase * (k == 1 ? -1.0 : 1.0) * (k == 1 ? s.p3 * 2.0 - 1.0 : 1.0);
                    const float c = (float) std::cos (MathConstants<double>::twoPi * ph), sn = (float) std::sin (MathConstants<double>::twoPi * ph);
                    float a = ch[k][i] + fb[k] * s.p2 * 0.7f, b = a;
                    for (int j = 0; j < 4; ++j) { a = hilA[k][j].process (a); b = hilB[k][j].process (b); }
                    const float bd = hilDelay[k]; hilDelay[k] = b;
                    const float y = a * c - bd * sn;
                    fb[k] = y;
                    ch[k][i] = y;
                }
            }
            break;
        }
        case 10:
        {
            static const float chords[4][3] { { 0, 7, 12 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 5, 10 } };
            const int ci = jlimit (0, 3, (int) (s.p2 * 3.99f));
            const float root = 24.0f + s.p1 * 48.0f;
            const float fbk = 0.9f + s.p3 * 0.095f;
            const float damp = 0.15f + s.p4 * 0.8f;
            float d[3];
            for (int k = 0; k < 3; ++k) d[k] = clampf (fs / (float) noteToHz (root + chords[ci][k]), 2.0f, (float) res[0][k].buf.size() - 4.0f);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    float y = 0.0f;
                    const float in = ch[c][i] * 0.3f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float v = res[c][k].read (d[k] * (c == 1 ? 1.003f : 1.0f));
                        resLp[c][k] += (v - resLp[c][k]) * damp;
                        res[c][k].push (in + resLp[c][k] * fbk);
                        y += resLp[c][k];
                    }
                    ch[c][i] = y * 0.4f;
                }
            break;
        }
        case 11:
        {
            const float q = std::exp2 (15.0f - s.p1 * 12.0f);
            const int hold = 1 + (int) (s.p2 * s.p2 * 40.0f);
            const float g = 1.0f + s.p3 * 6.0f;
            for (int i = 0; i < n; ++i)
            {
                if (crushCount++ % hold == 0) { crushHold[0] = std::round (fastTanh (l[i] * g) * q) / q; crushHold[1] = std::round (fastTanh (r[i] * g) * q) / q; }
                l[i] = crushHold[0]; r[i] = crushHold[1];
            }
            break;
        }
        default: break;
    }
    if (s.lowCut != lastLc) { for (auto& f : lc) f.set (s.lowCut, 0.7071f, fs); lastLc = s.lowCut; }
    if (s.hiCut != lastHc) { for (auto& f : hc) f.set (s.hiCut, 0.7071f, fs); lastHc = s.hiCut; }
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        lc[0].tick (l[i]); hc[0].tick (lc[0].hp); l[i] = hc[0].lp * s.ret;
        lc[1].tick (r[i]); hc[1].tick (lc[1].hp); r[i] = hc[1].lp * s.ret;
        pk = std::max (pk, std::max (std::abs (l[i]), std::abs (r[i])));
    }
    level = std::max (pk, level * 0.9f);
}

// ===================================================================== group bus
void GroupBus::process (const GroupSettings& s, float* l, float* r, int n)
{
    const float fs = (float) sr;
    float minG = 1.0f;
    const float g = s.levelDb <= -59.9f ? 0.0f : dbToGain (s.levelDb);
    const float thr = -s.comp * 24.0f, ratio = 1.0f + s.comp * 5.0f;
    const float att = std::exp (-1.0f / (0.008f * fs)), rel = std::exp (-1.0f / (0.12f * fs));
    const float makeup = dbToGain (-thr * (1.0f - 1.0f / ratio) * 0.4f);
    const float dg = 1.0f + s.drive * 5.0f, dn = 1.0f / fastTanh (dg);
    for (int i = 0; i < n; ++i)
    {
        float L = l[i], R = r[i];
        if (s.comp > 0.001f)
        {
            const float x = std::max (std::abs (L), std::abs (R));
            env = x + (x > env ? att : rel) * (env - x);
            const float over = gainToDb (env) - thr;
            const float gc = over > 0.0f ? dbToGain (-over * (1.0f - 1.0f / ratio)) : 1.0f;
            minG = std::min (minG, gc);
            L *= gc * makeup; R *= gc * makeup;
        }
        if (s.drive > 0.001f) { L = lerpf (L, fastTanh (L * dg) * dn, s.drive); R = lerpf (R, fastTanh (R * dg) * dn, s.drive); }
        if (std::abs (s.width - 1.0f) > 0.01f) { const float m = 0.5f * (L + R), sd = 0.5f * (L - R) * s.width; L = m + sd; R = m - sd; }
        l[i] = L * g; r[i] = R * g;
    }
    gainReduction = gainToDb (minG);
}

// ===================================================================== master dynamics
void MasterDynamics::prepare (double sampleRate, int)
{
    sr = sampleRate;
    look = std::max (8, (int) (sr * 0.0015));
    dL.prepare (look + 8); dR.prepare (look + 8);
    req.assign ((size_t) look, 1.0f);
    box.assign ((size_t) look, 1.0f);
    reset();
}

void MasterDynamics::reset()
{
    glueEnv = 0.0f; limGain = 1.0f; reqPos = 0;
    std::fill (req.begin(), req.end(), 1.0f);
    std::fill (box.begin(), box.end(), 1.0f); boxSum = (double) look; boxPos = 0;
    dL.clear(); dR.clear();
    for (auto& h : hist) for (auto& v : h) v = 0.0f;
}

float MasterDynamics::isp (int c, float x)
{
    // 4x interpolated peak estimate between the two middle samples of the last four
    auto& h = hist[c];
    const float y0 = h[0], y1 = h[1], y2 = h[2], y3 = x;
    h[0] = y1; h[1] = y2; h[2] = y3;
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    float pk = std::max (std::abs (y2), std::abs (y3));
    for (int k = 1; k < 4; ++k)
    {
        const float t = (float) k * 0.25f;
        pk = std::max (pk, std::abs (((c3 * t + c2) * t + c1) * t + c0));
    }
    return pk;
}

void MasterDynamics::process (const MasterSettings& s, float* l, float* r, int n)
{
    const float fs = (float) sr;
    // bus compressor
    float minG = 1.0f;
    if (s.glueOn)
    {
        const float att = std::exp (-1.0f / (s.glueAttack * 0.001f * fs)), rel = std::exp (-1.0f / (s.glueRelease * 0.001f * fs));
        const float knee = 6.0f, mk = dbToGain (s.glueMakeup);
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::abs (l[i]), std::abs (r[i]));
            glueEnv = x + (x > glueEnv ? att : rel) * (glueEnv - x);
            const float db = gainToDb (glueEnv);
            const float over = db - s.glueThresh;
            float red = 0.0f;
            if (over > knee * 0.5f) red = over * (1.0f - 1.0f / s.glueRatio);
            else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; red = (1.0f - 1.0f / s.glueRatio) * k * k / (2.0f * knee); }
            const float g = dbToGain (-red);
            minG = std::min (minG, g);
            l[i] = lerpf (l[i], l[i] * g * mk, s.glueMix);
            r[i] = lerpf (r[i], r[i] * g * mk, s.glueMix);
        }
    }
    glueGr = gainToDb (minG);
    // soft clip
    if (s.softclip > 0.001f)
    {
        const float t = 1.0f - s.softclip * 0.6f;
        auto sc = [t] (float x)
        {
            const float a = std::abs (x);
            if (a <= t) return x;
            const float y = t + (1.0f - t) * fastTanh ((a - t) / (1.0f - t));
            return x < 0.0f ? -y : y;
        };
        const float pre = 1.0f + s.softclip * 0.5f;
        // with ADAA: the knee is averaged between samples, so the clip does not fold fizz into the highs
        for (int i = 0; i < n; ++i) { l[i] = racoon::sdsp::adaaAverage (sc, l[i] * pre, scX[0]); r[i] = racoon::sdsp::adaaAverage (sc, r[i] * pre, scX[1]); }
    }
    // master volume before the limiter keeps the ceiling honest
    for (int i = 0; i < n; ++i) { l[i] *= s.volume * 1.25f; r[i] *= s.volume * 1.25f; }
    // true-peak limiter with look-ahead: window minimum, box-smoothed attack, exponential release
    float minL = 1.0f, tp = 0.0f;
    const float ceil = dbToGain (s.tpCeiling);
    const float relC = std::exp (-1.0f / (0.06f * fs));
    for (int i = 0; i < n; ++i)
    {
        const float pk = std::max (isp (0, l[i]), isp (1, r[i]));
        tp = std::max (tp, pk);
        req[(size_t) reqPos] = s.tpOn && pk > ceil ? ceil / pk : 1.0f;
        reqPos = (reqPos + 1) % look;
        float target = 1.0f;
        for (auto v : req) target = std::min (target, v);
        boxSum += target - box[(size_t) boxPos];
        box[(size_t) boxPos] = target;
        boxPos = (boxPos + 1) % look;
        const float smooth = std::min (target * 0.0f + (float) (boxSum / look), 1.0f);
        limGain = smooth < limGain ? smooth : smooth + relC * (limGain - smooth);
        dL.push (l[i]); dR.push (r[i]);
        float yl = dL.read ((float) look) * limGain, yr = dR.read ((float) look) * limGain;
        if (s.tpOn) { yl = clampf (yl, -ceil, ceil); yr = clampf (yr, -ceil, ceil); }
        l[i] = yl; r[i] = yr;
        minL = std::min (minL, limGain);
    }
    limGr = gainToDb (minL);
    truePeak = tp;
}

// ===================================================================== scatter
void Scatter::prepare (double sampleRate)
{
    sr = sampleRate;
    size = (int) (sr * 9.0);
    bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
    w = 0; active = false; fade = 0.0f;
}

void Scatter::process (int division, float* l, float* r, int n, double bpm, double beats, bool)
{
    static const double divBeats[] { 0.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    const bool want = division > 0;
    const double spb = 60.0 / std::max (bpm, 20.0) * sr;
    if (want && ! active)
    {
        const double d = divBeats[jlimit (0, 5, division)];
        segLen = std::max (64, (int) (d * spb));
        const double into = std::fmod (std::max (0.0, beats), d) * spb;
        segStart = ((w - (int) into - segLen) % size + size) % size;
        readPos = 0;
        active = true;
    }
    else if (want && active)
    {
        const double d = divBeats[jlimit (0, 5, division)];
        const int len = std::max (64, (int) (d * spb));
        if (len != segLen) { segLen = len; readPos %= segLen; }
    }
    if (! want) active = false;
    const float fstep = 1.0f / (0.004f * (float) sr);
    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) w] = l[i]; bufR[(size_t) w] = r[i];
        w = (w + 1) % size;
        fade = active ? std::min (1.0f, fade + fstep) : std::max (0.0f, fade - fstep);
        if (fade > 0.0f)
        {
            const int idx = (segStart + readPos) % size;
            // short fades at the loop seam
            const int edge = std::min (readPos, segLen - 1 - readPos);
            const float seam = clamp01 ((float) edge / 96.0f);
            const float yl = bufL[(size_t) idx] * seam, yr = bufR[(size_t) idx] * seam;
            l[i] = lerpf (l[i], yl, fade); r[i] = lerpf (r[i], yr, fade);
            if (active) readPos = (readPos + 1) % std::max (1, segLen);
        }
    }
}

void BreakFilter::process (bool on, float* l, float* r, int n)
{
    const float target = on ? 1.0f : 0.0f;
    const float k = 1.0f / (0.25f * (float) sr / (float) std::max (1, n)) ;
    amt += (target - amt) * clamp01 (k * 4.0f);
    if (amt < 0.001f) return;
    const float hz = 20.0f * std::pow (16.0f, amt);
    for (int c = 0; c < 4; ++c) hp[c].set (hz, 0.7071f, (float) sr);
    for (int i = 0; i < n; ++i)
    {
        hp[0].tick (l[i]); hp[1].tick (hp[0].hp); l[i] = hp[1].hp;
        hp[2].tick (r[i]); hp[3].tick (hp[2].hp); r[i] = hp[3].hp;
    }
}

// ===================================================================== loudness
void LoudnessMeter::prepare (double sampleRate)
{
    sr = sampleRate;
    const double pi = MathConstants<double>::pi;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : pre) b.setNorm ((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : rlb) b.setNorm (1.0, -2.0, 1.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
    }
    blockLen = (int) (sr * 0.1);
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& b : pre) b.reset();
    for (auto& b : rlb) b.reset();
    blockPos = 0; blockSum = 0.0; blockIdx = 0; blocksFilled = 0;
    std::fill (std::begin (blocks), std::end (blocks), 0.0f);
    std::fill (std::begin (histE), std::end (histE), 0.0);
    std::fill (std::begin (histN), std::end (histN), 0);
    corrLR = corrLL = corrRR = midE = sideE = 0.0;
    pkHold = 0.0f; rmsAcc = 0.0f;
    momentary = -70.0f; shortTerm = -70.0f; integrated = -70.0f; truePeakMax = 0.0f;
}

void LoudnessMeter::process (const float* l, const float* r, int n)
{
    if (resetRequest.exchange (false)) reset();
    for (int i = 0; i < n; ++i)
    {
        const float kl = rlb[0].process (pre[0].process (l[i]));
        const float kr = rlb[1].process (pre[1].process (r[i]));
        blockSum += (double) kl * kl + (double) kr * kr;
        corrLR = corrLR * 0.9999 + (double) l[i] * r[i];
        corrLL = corrLL * 0.9999 + (double) l[i] * l[i];
        corrRR = corrRR * 0.9999 + (double) r[i] * r[i];
        const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
        midE = midE * 0.9999 + (double) m * m; sideE = sideE * 0.9999 + (double) s * s;
        pkHold = std::max (pkHold, std::max (std::abs (l[i]), std::abs (r[i])));
        if (++blockPos >= blockLen)
        {
            blocks[blockIdx] = (float) (blockSum / blockLen);
            blockIdx = (blockIdx + 1) % 30;
            blocksFilled = std::min (30, blocksFilled + 1);
            blockPos = 0; blockSum = 0.0;
            auto lufs = [] (double e) { return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : -70.0f; };
            double m4 = 0.0, s30 = 0.0;
            for (int k = 0; k < 4; ++k) m4 += blocks[(blockIdx - 1 - k + 30) % 30];
            for (int k = 0; k < blocksFilled; ++k) s30 += blocks[k];
            const float mom = lufs (m4 / 4.0);
            momentary = mom;
            shortTerm = lufs (s30 / std::max (1, blocksFilled));
            if (mom > -70.0f)
            {
                const int bin = jlimit (0, kBins - 1, (int) ((mom + 70.0f) * 10.0f));
                histE[bin] += m4 / 4.0; histN[bin]++;
            }
            // integrated with absolute and relative gates
            double e = 0.0; int cnt = 0;
            for (int b = 0; b < kBins; ++b) { e += histE[b]; cnt += histN[b]; }
            if (cnt > 0)
            {
                const float rel = lufs (e / cnt) - 10.0f;
                double e2 = 0.0; int c2 = 0;
                for (int b = jlimit (0, kBins - 1, (int) ((rel + 70.0f) * 10.0f)); b < kBins; ++b) { e2 += histE[b]; c2 += histN[b]; }
                integrated = c2 > 0 ? lufs (e2 / c2) : -70.0f;
            }
            // peak / crest over the short-term window
            const float pkDb = gainToDb (pkHold);
            peakDb = pkDb;
            const float rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, s30 / std::max (1, blocksFilled) / 2.0)));
            crest = pkDb - rmsDb;
            pkHold *= 0.7f;
            const double den = std::sqrt (corrLL * corrRR);
            correlation = den > 1.0e-9 ? (float) (corrLR / den) : 1.0f;
            width = midE > 1.0e-12 ? (float) std::sqrt (sideE / midE) : 0.0f;
        }
    }
}
} // namespace be

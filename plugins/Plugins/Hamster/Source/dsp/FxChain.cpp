#include "FxChain.h"

namespace be
{
// ================================================================ distortion
void DistortionStage::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    os2 = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os4 = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os2->initProcessing ((size_t) maxBlock);
    os4->initProcessing ((size_t) maxBlock);
    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void DistortionStage::reset()
{
    adaaX[0] = adaaX[1] = 0.0f;
    if (os2) os2->reset();
    if (os4) os4->reset();
    holdL = holdR = 0; holdCount = 0;
    xLowL.reset(); xLowR.reset(); xHighL.reset(); xHighR.reset();
    toneL.reset(); toneR.reset(); tapeL.reset(); tapeR.reset();
}

int DistortionStage::latencySamples (int quality) const
{
    if (quality == 1 && os2) return (int) std::round (os2->getLatencyInSamples());
    if (quality == 2 && os4) return (int) std::round (os4->getLatencyInSamples());
    return 0;
}

float DistortionStage::processSample (const DistSettings& s, float x, int ch, float osFactor)
{
    const float drive = s.drive;
    if (drive != gDrive) { gDrive = drive; gCached = dbToGain (drive * 36.0f); }   // the pow once, not per sample
    const float g = gCached;
    const float sh = s.shape;
    switch ((DistType) s.type)
    {
        case DistType::SoftClip: return fastTanh (x * g) * (0.6f + 0.4f / (1.0f + drive * 3.0f));
        case DistType::HardClip: { const float c = 0.3f + sh * 0.7f; return clampf (x * g, -c, c) / c * 0.7f; }
        case DistType::Tube:
        {
            const float bias = (sh - 0.5f) * 0.5f;
            const float y = x * g + bias;
            return (y >= 0.0f ? fastTanh (y) : fastTanh (y * 0.6f) / 0.6f * 0.7f) - (bias >= 0.0f ? fastTanh (bias) : fastTanh (bias * 0.6f) / 0.6f * 0.7f);
        }
        case DistType::Tape:
        {
            auto& lp = ch == 0 ? tapeL : tapeR;
            const float y = x * g * 0.8f;
            const float sat = y / (1.0f + std::abs (y));
            lp.setCutoff (18000.0f - sh * 14000.0f, (float) sr * osFactor);
            return lp.lp (sat) * 1.3f;
        }
        case DistType::Transistor:
        {
            float y = clampf (x * g * 0.7f, -1.5f, 1.5f);
            y = y - (y * y * y) / 3.0f * (0.6f + sh * 0.4f);
            return y * 1.2f;
        }
        case DistType::Diode:
        {
            const float y = x * g;
            const float v = y >= 0.0f ? 1.0f - std::exp (-y) : -(1.0f - std::exp (y * (0.3f + sh * 0.7f))) * (0.3f + sh * 0.7f);
            return v;
        }
        case DistType::Wavefolder:
        {
            const float y = x * (1.0f + drive * 8.0f) + (sh - 0.5f) * 0.6f;
            return std::sin (y * kPi * 0.5f);
        }
        case DistType::Bitcrusher:
        {
            const float bits = 16.0f - drive * 14.0f;
            const float lv = std::exp2 (bits - 1.0f);
            const float y = std::round (x * lv) / lv;
            return lerpf (x, y, 0.5f + sh * 0.5f);
        }
        case DistType::Downsample:
        {
            const int factor = 1 + (int) (drive * drive * 48.0f * osFactor);
            auto& hold = ch == 0 ? holdL : holdR;
            if (ch == 0) { if (++holdCount >= factor) { holdCount = 0; holdL = x; holdR = 0.0f; } }
            else if (holdCount == 0) holdR = x;
            return lerpf (x, hold, 0.5f + sh * 0.5f);
        }
        case DistType::Fuzz:
        {
            const float y = x * g * 6.0f;
            const float gate = clamp01 (std::abs (x) * (8.0f + sh * 40.0f));
            return fastTanh (y + 0.3f * std::abs (y)) * gate * 0.8f;
        }
        case DistType::Asymmetric:
        {
            const float y = x * g;
            return y > 0.0f ? fastTanh (y) : fastTanh (y * (0.2f + sh * 0.8f)) * 0.8f;
        }
        default: return x;
    }
}

void DistortionStage::process (const DistSettings& s, float* l, float* r, int n, int quality)
{
    if (! s.on) return;
    std::copy (l, l + n, dryL.data());
    std::copy (r, r + n, dryR.data());

    float* chans[2] { l, r };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    juce::dsp::AudioBlock<float> work = block;
    juce::dsp::Oversampling<float>* os = quality == 1 ? os2.get() : (quality == 2 ? os4.get() : nullptr);
    if (os != nullptr) work = os->processSamplesUp (block);
    const float osF = os != nullptr ? (float) os->getOversamplingFactor() : 1.0f;
    const int m = (int) work.getNumSamples();
    float* wl = work.getChannelPointer (0);
    float* wr = work.getChannelPointer (1);

    if (s.type == (int) DistType::Multiband)
    {
        const float rate = (float) sr * osF;
        xLowL.set (s.xLow, rate); xLowR.set (s.xLow, rate);
        xHighL.set (s.xHigh, rate); xHighR.set (s.xHigh, rate);
        DistSettings band = s;
        band.type = (int) DistType::Tube;
        for (int i = 0; i < m; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                float* w = c == 0 ? wl : wr;
                float lo, rest, mid, hi;
                (c == 0 ? xLowL : xLowR).split (w[i], lo, rest);
                (c == 0 ? xHighL : xHighR).split (rest, mid, hi);
                band.drive = s.lowDrive; const float lo2 = s.lowDrive > 0.001f ? processSample (band, lo, c, osF) : lo;
                band.drive = s.drive; const float mid2 = processSample (band, mid, c, osF);
                band.drive = s.highDrive; band.type = (int) DistType::SoftClip; const float hi2 = processSample (band, hi, c, osF);
                band.type = (int) DistType::Tube;
                w[i] = lo2 + mid2 + hi2;
            }
        }
    }
    else
    {
        // the curves without memory get ADAA on top of the oversampling (the average of the curve between two samples):
        // much less fizz folded back. Tape has its own filter, and the bit and rate reducers are meant to alias.
        const auto t = (DistType) s.type;
        const bool memoryless = t != DistType::Tape && t != DistType::Bitcrusher && t != DistType::Downsample;
        if (memoryless)
        {
            for (int i = 0; i < m; ++i)
            {
                wl[i] = racoon::sdsp::adaaAverage ([&] (float v) { return processSample (s, v, 0, osF); }, wl[i], adaaX[0]);
                wr[i] = racoon::sdsp::adaaAverage ([&] (float v) { return processSample (s, v, 1, osF); }, wr[i], adaaX[1]);
            }
        }
        else
            for (int i = 0; i < m; ++i)
            {
                wl[i] = processSample (s, wl[i], 0, osF);
                wr[i] = processSample (s, wr[i], 1, osF);
            }
    }
    if (os != nullptr) os->processSamplesDown (block);

    // tone (tilt) + mix + output
    const float t = s.tone;
    const float fc = t < 0.0f ? 20000.0f * std::exp2 (t * 4.5f) : 20.0f * std::exp2 (t * 7.0f);
    toneL.setCutoff (fc, (float) sr); toneR.setCutoff (fc, (float) sr);
    const float out = dbToGain (s.outDb);
    for (int i = 0; i < n; ++i)
    {
        float a = l[i], b = r[i];
        if (t < -0.01f) { a = toneL.lp (a); b = toneR.lp (b); }
        else if (t > 0.01f) { a = a + 0.7f * toneL.hp (a) * t; b = b + 0.7f * toneR.hp (b) * t; }
        l[i] = lerpf (dryL[(size_t) i], a, s.mix) * out;
        r[i] = lerpf (dryR[(size_t) i], b, s.mix) * out;
    }
}

// ================================================================ FX rack
void FxRack::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
    limLook = std::max (8, (int) (sampleRate * 0.0015));
    limL.prepare (limLook + 8); limR.prepare (limLook + 8);
    limReq.assign ((size_t) limLook, 1.0f);
    chorus.prepare (spec);
    phaser.prepare (spec);
    reverb.setSampleRate (sampleRate);
    flL.prepare ((int) (sampleRate * 0.03)); flR.prepare ((int) (sampleRate * 0.03));
    dlL.prepare ((int) (sampleRate * 4.2)); dlR.prepare ((int) (sampleRate * 4.2));
    preL.prepare ((int) (sampleRate * 0.25)); preR.prepare ((int) (sampleRate * 0.25));
    for (int k = 0; k < 3; ++k) { rsL[k].prepare ((int) (sampleRate / 15.0)); rsR[k].prepare ((int) (sampleRate / 15.0)); }
    wetL.assign ((size_t) maxBlock, 0.0f);
    wetR.assign ((size_t) maxBlock, 0.0f);
    const float ca[4] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    const float cb[4] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 4; ++k) { hilA[c][k].a = ca[k] * ca[k]; hilB[c][k].a = cb[k] * cb[k]; }
    std::fill (std::begin (eqCache), std::end (eqCache), -1.0e9f);
    reset();
}

void FxRack::reset()
{
    clipX[0] = clipX[1] = 0.0f;
    for (int c = 0; c < 2; ++c) { eqHp[c].reset(); eqLs[c].reset(); eqP1[c].reset(); eqP2[c].reset(); eqHs[c].reset(); }
    compEnv = 0; std::fill (std::begin (mbEnv), std::end (mbEnv), 0.0f);
    limL.clear(); limR.clear(); std::fill (limReq.begin(), limReq.end(), 1.0f); limGain = 1.0f; chorus.reset(); phaser.reset(); reverb.reset();
    flL.clear(); flR.clear(); dlL.clear(); dlR.clear(); preL.clear(); preR.clear(); dlTimeNow = -1.0f;
    for (int k = 0; k < 3; ++k) { rsL[k].clear(); rsR[k].clear(); rsLpL[k] = rsLpR[k] = 0; }
    dlFbL = dlFbR = flFbL = flFbR = 0;
    trFast = trSlow = 0;
}

void FxRack::process (const FxSettings& s, float* l, float* r, int n, double bpm, double beats, bool playing, float noteHz, const float*)
{
    for (int k = 0; k < kNumFx; ++k)
    {
        const int id = s.order[k];
        if (id < 0 || id >= kNumFx || ! s.on[id]) continue;
        switch ((FxId) id)
        {
            case FxId::EQ: processEq (s, l, r, n); break;
            case FxId::Compressor: processComp (s, l, r, n); break;
            case FxId::Multiband: processMultiband (s, l, r, n); break;
            case FxId::Limiter: processLimiter (s, l, r, n); break;
            case FxId::Clipper: processClipper (s, l, r, n); break;
            case FxId::Chorus: processChorus (s, l, r, n, s.chMix); break;
            case FxId::Phaser: processPhaser (s, l, r, n, s.phMix); break;
            case FxId::Flanger: processFlanger (s, l, r, n, s.flMix); break;
            case FxId::Delay: processDelay (s, l, r, n, bpm, s.dlMix, s.dlFb); break;
            case FxId::Reverb: processReverb (s, l, r, n, s.rvMix); break;
            case FxId::FreqShift: processFreqShift (s, l, r, n, s.fsShift); break;
            case FxId::RingMod: processRingMod (s, l, r, n, noteHz); break;
            case FxId::Transient: processTransient (s, l, r, n); break;
            case FxId::Imager: processImager (s, l, r, n, s.imWidth); break;
            case FxId::AutoFilter: processAutoFilter (s, l, r, n, bpm, beats, playing, s.afCutoff); break;
            case FxId::Resonator: processResonator (s, l, r, n, noteHz, s.rsMix); break;
        }
    }
}

void FxRack::processEq (const FxSettings& s, float* l, float* r, int n)
{
    const float v[11] { s.eqHpf, s.eqLsF, s.eqLsG, s.eqP1F, s.eqP1G, s.eqP1Q, s.eqP2F, s.eqP2G, s.eqP2Q, s.eqHsF, s.eqHsG };
    if (! std::equal (std::begin (v), std::end (v), std::begin (eqCache)))
    {
        std::copy (std::begin (v), std::end (v), std::begin (eqCache));
        for (int c = 0; c < 2; ++c)
        {
            eqHp[c].highPass (s.eqHpf, 0.7071, sr);
            eqLs[c].lowShelf (s.eqLsF, s.eqLsG, sr);
            eqP1[c].peak (s.eqP1F, s.eqP1Q, s.eqP1G, sr);
            eqP2[c].peak (s.eqP2F, s.eqP2Q, s.eqP2G, sr);
            eqHs[c].highShelf (s.eqHsF, s.eqHsG, sr);
        }
    }
    const bool hp = s.eqHpf > 10.5f, ls = std::abs (s.eqLsG) > 0.05f, p1 = std::abs (s.eqP1G) > 0.05f, p2 = std::abs (s.eqP2G) > 0.05f, hs = std::abs (s.eqHsG) > 0.05f;
    float* ch[2] { l, r };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            float x = ch[c][i];
            if (hp) x = eqHp[c].process (x);
            if (ls) x = eqLs[c].process (x);
            if (p1) x = eqP1[c].process (x);
            if (p2) x = eqP2[c].process (x);
            if (hs) x = eqHs[c].process (x);
            ch[c][i] = x;
        }
}

namespace
{
inline float compGainDb (float levelDb, float thresh, float ratio, float knee = 6.0f)
{
    const float over = levelDb - thresh;
    if (over <= -knee * 0.5f) return 0.0f;
    if (over >= knee * 0.5f) return -over * (1.0f - 1.0f / ratio);
    const float x = over + knee * 0.5f;
    return -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee);
}
}

void FxRack::processComp (const FxSettings& s, float* l, float* r, int n)
{
    const float at = std::exp (-1.0f / (0.001f * s.cAttack * (float) sr));
    const float rl = std::exp (-1.0f / (0.001f * s.cRelease * (float) sr));
    const float makeup = dbToGain (s.cMakeup);
    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
        const float coeff = peak > compEnv ? at : rl;
        compEnv = peak + coeff * (compEnv - peak);
        const float g = dbToGain (compGainDb (gainToDb (compEnv), s.cThresh, s.cRatio)) * makeup;
        l[i] = lerpf (l[i], l[i] * g, s.cMix);
        r[i] = lerpf (r[i], r[i] * g, s.cMix);
    }
}

void FxRack::processMultiband (const FxSettings& s, float* l, float* r, int n)
{
    mbLowL.set (s.mbXLow, (float) sr); mbLowR.set (s.mbXLow, (float) sr);
    mbHighL.set (s.mbXHigh, (float) sr); mbHighR.set (s.mbXHigh, (float) sr);
    const float at = std::exp (-1.0f / (0.004f * (float) sr)), rl = std::exp (-1.0f / (0.12f * (float) sr));
    const float th[3] { s.mbLow, s.mbMid, s.mbHigh };
    for (int i = 0; i < n; ++i)
    {
        float lb[2], mb[2], hb[2], rest;
        mbLowL.split (l[i], lb[0], rest); mbHighL.split (rest, mb[0], hb[0]);
        mbLowR.split (r[i], lb[1], rest); mbHighR.split (rest, mb[1], hb[1]);
        float* bands[3] { lb, mb, hb };
        float outL = 0, outR = 0;
        for (int b = 0; b < 3; ++b)
        {
            const float pk = std::max (std::abs (bands[b][0]), std::abs (bands[b][1]));
            mbEnv[b] = pk + (pk > mbEnv[b] ? at : rl) * (mbEnv[b] - pk);
            const float g = dbToGain (compGainDb (gainToDb (mbEnv[b]), th[b], s.mbRatio) * 0.9f + 1.5f);
            outL += bands[b][0] * g; outR += bands[b][1] * g;
        }
        l[i] = lerpf (l[i], outL, s.mbMix);
        r[i] = lerpf (r[i], outR, s.mbMix);
    }
}

void FxRack::processLimiter (const FxSettings& s, float* l, float* r, int n)
{
    // look-ahead peak limiter: the gain reaches its target before the delayed peak arrives
    const float ceil = dbToGain (s.limCeiling);
    const float att = std::exp (-1.0f / ((float) limLook * 0.4f));
    const float rel = std::exp (-1.0f / (0.001f * s.limRelease * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float pk = std::max (std::abs (l[i]), std::abs (r[i]));
        limReq[(size_t) limPos] = pk > ceil ? ceil / pk : 1.0f;
        limPos = (limPos + 1) % limLook;
        float target = 1.0f;
        for (auto v : limReq) target = std::min (target, v);
        limGain = target < limGain ? target + att * (limGain - target) : target + rel * (limGain - target);
        limL.push (l[i]); limR.push (r[i]);
        const float dl = limL.read ((float) limLook), dr = limR.read ((float) limLook);
        l[i] = clampf (dl * limGain, -ceil, ceil);
        r[i] = clampf (dr * limGain, -ceil, ceil);
    }
}

void FxRack::processClipper (const FxSettings& s, float* l, float* r, int n)
{
    const float drive = dbToGain (s.clipDrive), ceil = dbToGain (s.clipCeiling);
    const float k = 0.05f + s.clipKnee * 2.0f;
    auto clip = [ceil, k] (float x)
    {
        const float y = x / ceil;
        const float soft = fastTanh (y * (1.0f + k)) / fastTanh (1.0f + k);
        return clampf (lerpf (clampf (y, -1.0f, 1.0f), soft, std::min (1.0f, k)), -1.0f, 1.0f) * ceil;
    };
    // ADAA (the curve averaged between samples): the clipper runs at the base rate, so this is what keeps it clean.
    // The ceiling still holds: an average of values under the ceiling stays under it.
    for (int i = 0; i < n; ++i)
    {
        l[i] = racoon::sdsp::adaaAverage (clip, l[i] * drive, clipX[0]);
        r[i] = racoon::sdsp::adaaAverage (clip, r[i] * drive, clipX[1]);
    }
}

void FxRack::processChorus (const FxSettings& s, float* l, float* r, int n, float mix)
{
    std::copy (l, l + n, wetL.data()); std::copy (r, r + n, wetR.data());
    lcCh[0].setCutoff (s.chLowCut, (float) sr); lcCh[1].setCutoff (s.chLowCut, (float) sr);
    for (int i = 0; i < n; ++i) { wetL[(size_t) i] = lcCh[0].hp (wetL[(size_t) i]); wetR[(size_t) i] = lcCh[1].hp (wetR[(size_t) i]); }
    chorus.setRate (s.chRate); chorus.setDepth (s.chDepth); chorus.setCentreDelay (s.chDelay); chorus.setFeedback (s.chFb); chorus.setMix (1.0f);
    float* ch[2] { wetL.data(), wetR.data() };
    juce::dsp::AudioBlock<float> b (ch, 2, (size_t) n);
    chorus.process (juce::dsp::ProcessContextReplacing<float> (b));
    for (int i = 0; i < n; ++i) { l[i] += (wetL[(size_t) i] - lcCh[0].s * 0.0f) * mix; r[i] += wetR[(size_t) i] * mix; }
}

void FxRack::processPhaser (const FxSettings& s, float* l, float* r, int n, float mix)
{
    std::copy (l, l + n, wetL.data()); std::copy (r, r + n, wetR.data());
    lcPh[0].setCutoff (s.phLowCut, (float) sr); lcPh[1].setCutoff (s.phLowCut, (float) sr);
    for (int i = 0; i < n; ++i) { wetL[(size_t) i] = lcPh[0].hp (wetL[(size_t) i]); wetR[(size_t) i] = lcPh[1].hp (wetR[(size_t) i]); }
    phaser.setRate (s.phRate); phaser.setDepth (s.phDepth); phaser.setCentreFrequency (s.phCentre); phaser.setFeedback (s.phFb); phaser.setMix (1.0f);
    float* ch[2] { wetL.data(), wetR.data() };
    juce::dsp::AudioBlock<float> b (ch, 2, (size_t) n);
    phaser.process (juce::dsp::ProcessContextReplacing<float> (b));
    // phaser output contains dry + notches: blend against the band that was sent
    for (int i = 0; i < n; ++i) { l[i] = l[i] * (1.0f - mix * 0.5f) + wetL[(size_t) i] * mix * 0.5f; r[i] = r[i] * (1.0f - mix * 0.5f) + wetR[(size_t) i] * mix * 0.5f; }
}

void FxRack::processFlanger (const FxSettings& s, float* l, float* r, int n, float mix)
{
    lcFl[0].setCutoff (s.flLowCut, (float) sr); lcFl[1].setCutoff (s.flLowCut, (float) sr);
    const float inc = s.flRate / (float) sr;
    for (int i = 0; i < n; ++i)
    {
        flPhase += inc; if (flPhase >= 1.0f) flPhase -= 1.0f;
        const float modL = 0.5f + 0.5f * std::sin (kTwoPi * flPhase);
        const float modR = 0.5f + 0.5f * std::sin (kTwoPi * (flPhase + 0.25f));
        const float dL = std::max (1.5f, (s.flDelay * (0.15f + 0.85f * modL * s.flDepth + (1.0f - s.flDepth) * 0.5f)) * 0.001f * (float) sr);
        const float dR = std::max (1.5f, (s.flDelay * (0.15f + 0.85f * modR * s.flDepth + (1.0f - s.flDepth) * 0.5f)) * 0.001f * (float) sr);
        const float inL = lcFl[0].hp (l[i]), inR = lcFl[1].hp (r[i]);
        const float wl = flL.read (dL), wr = flR.read (dR);
        flL.push (inL + fastTanh (wl * s.flFb));
        flR.push (inR + fastTanh (wr * s.flFb));
        l[i] += wl * mix; r[i] += wr * mix;
    }
}

void FxRack::processDelay (const FxSettings& s, float* l, float* r, int n, double bpm, float mix, float fb)
{
    const float timeSec = s.dlSync ? (float) (syncDivisionBeats (s.dlDiv) * 60.0 / std::max (bpm, 20.0)) : s.dlTime * 0.001f;
    const float d = clampf (timeSec * (float) sr, 2.0f, (float) dlL.buf.size() - 8.0f);
    dlLp[0].setCutoff (s.dlHighCut, (float) sr); dlLp[1].setCutoff (s.dlHighCut, (float) sr);
    lcDl[0].setCutoff (s.dlLowCut, (float) sr); lcDl[1].setCutoff (s.dlLowCut, (float) sr);
    // a new time (knob, tempo, division) is reached in about 60 ms, like a tape delay sliding in pitch; before it jumped and clicked
    if (dlTimeNow < 0.0f) dlTimeNow = d;
    const float glide = 1.0f - std::exp (-1.0f / (0.060f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        dlTimeNow += (d - dlTimeNow) * glide;
        const float inL = lcDl[0].hp (l[i]), inR = lcDl[1].hp (r[i]);
        const float wl = dlL.read (dlTimeNow), wr = dlR.read (dlTimeNow);
        const float fl = dlLp[0].lp (wl) * fb, fr = dlLp[1].lp (wr) * fb;
        if (s.dlPing) { dlL.push (0.5f * (inL + inR) + fr); dlR.push (fl); }
        else { dlL.push (inL + fl); dlR.push (inR + fr); }
        l[i] += wl * mix; r[i] += wr * mix;
    }
}

void FxRack::processReverb (const FxSettings& s, float* l, float* r, int n, float mix)
{
    racoon::sdsp::Verb::Parameters p;   // the new reverb (8-line FDN), with the same knobs Freeverb had
    p.roomSize = s.rvSize; p.damping = s.rvDamp; p.width = s.rvWidth; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
    reverb.setParameters (p);
    lcRv[0].setCutoff (s.rvLowCut, (float) sr); lcRv[1].setCutoff (s.rvLowCut, (float) sr);
    const float pd = std::max (1.0f, s.rvPre * 0.001f * (float) sr);
    for (int i = 0; i < n; ++i)
    {
        preL.push (lcRv[0].hp (l[i])); preR.push (lcRv[1].hp (r[i]));
        wetL[(size_t) i] = preL.read (pd); wetR[(size_t) i] = preR.read (pd);
    }
    reverb.processStereo (wetL.data(), wetR.data(), n);
    for (int i = 0; i < n; ++i) { l[i] += wetL[(size_t) i] * mix; r[i] += wetR[(size_t) i] * mix; }
}

void FxRack::processFreqShift (const FxSettings& s, float* l, float* r, int n, float shift)
{
    const double inc = (double) shift / sr;
    float* ch[2] { l, r };
    for (int i = 0; i < n; ++i)
    {
        fsPhase += inc; fsPhase -= std::floor (fsPhase);
        const float c = (float) std::cos (2.0 * juce::MathConstants<double>::pi * fsPhase);
        const float sn = (float) std::sin (2.0 * juce::MathConstants<double>::pi * fsPhase);
        for (int k = 0; k < 2; ++k)
        {
            float a = ch[k][i], b = ch[k][i];
            for (int j = 0; j < 4; ++j) { a = hilA[k][j].process (a); b = hilB[k][j].process (b); }
            const float bd = hilDelay[k]; hilDelay[k] = b;
            const float shifted = a * c - bd * sn;
            ch[k][i] = lerpf (ch[k][i], shifted, s.fsMix);
        }
    }
}

void FxRack::processRingMod (const FxSettings& s, float* l, float* r, int n, float noteHz)
{
    const double f = s.rmMode == 1 ? (double) noteHz * (double) s.rmRatio : (double) s.rmFreq;
    const double inc = f / sr;
    for (int i = 0; i < n; ++i)
    {
        rmPhase += inc; rmPhase -= std::floor (rmPhase);
        const float m = (float) std::sin (2.0 * juce::MathConstants<double>::pi * rmPhase);
        l[i] = lerpf (l[i], l[i] * m, s.rmMix);
        r[i] = lerpf (r[i], r[i] * m, s.rmMix);
    }
}

void FxRack::processTransient (const FxSettings& s, float* l, float* r, int n)
{
    const float fastT = 0.0008f + (1.0f - s.trSpeed) * 0.004f, slowT = 0.03f + (1.0f - s.trSpeed) * 0.15f;
    const float af = std::exp (-1.0f / (fastT * (float) sr)), as = std::exp (-1.0f / (slowT * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float x = std::max (std::abs (l[i]), std::abs (r[i]));
        trFast = x + af * (trFast - x);
        trSlow = x + as * (trSlow - x);
        const float ratio = (trFast + 1.0e-5f) / (trSlow + 1.0e-5f);
        float gDb = 0.0f;
        if (ratio > 1.0f) gDb += s.trAttack * clampf (gainToDb (ratio), 0.0f, 24.0f) * 1.2f;
        else gDb += s.trSustain * clampf (-gainToDb (ratio), 0.0f, 24.0f) * 1.2f;
        const float g = dbToGain (clampf (gDb, -24.0f, 18.0f));
        l[i] *= g; r[i] *= g;
    }
}

void FxRack::processImager (const FxSettings& s, float* l, float* r, int n, float width)
{
    imLowL.set (s.imMonoFreq, (float) sr); imLowR.set (s.imMonoFreq, (float) sr);
    for (int i = 0; i < n; ++i)
    {
        float a = l[i], b = r[i];
        float lo = 0.0f;
        if (s.imBassMono)
        {
            float loL, hiL, loR, hiR;
            imLowL.split (a, loL, hiL); imLowR.split (b, loR, hiR);
            lo = 0.5f * (loL + loR);
            a = hiL; b = hiR;
        }
        const float m = 0.5f * (a + b), sd = 0.5f * (a - b) * width;
        l[i] = m + sd + lo; r[i] = m - sd + lo;
    }
}

void FxRack::processAutoFilter (const FxSettings& s, float* l, float* r, int n, double bpm, double beats, bool playing, float cutoff)
{
    double lfo;
    if (s.afSync)
    {
        const double div = syncDivisionBeats (s.afDiv);
        if (playing) afPhase = std::fmod (beats / div, 1.0);
        else afPhase += (double) n * (bpm / 60.0) / div / sr;
    }
    else afPhase += (double) n * (double) s.afRate / sr;
    afPhase -= std::floor (afPhase);
    lfo = std::sin (2.0 * juce::MathConstants<double>::pi * afPhase);
    const float fc = clampf (cutoff * std::exp2 ((float) lfo * s.afDepth * 4.0f), 20.0f, 20000.0f);
    const float q = 0.6f + s.afRes * 12.0f;
    afL.set (fc, q, (float) sr); afR.set (fc, q, (float) sr);
    for (int i = 0; i < n; ++i)
    {
        afL.tick (l[i]); afR.tick (r[i]);
        float a, b;
        switch (s.afType)
        {
            case 1: a = afL.bp * afL.k; b = afR.bp * afR.k; break;
            case 2: a = afL.hp; b = afR.hp; break;
            default: a = afL.lp; b = afR.lp; break;
        }
        l[i] = lerpf (l[i], a, s.afMix);
        r[i] = lerpf (r[i], b, s.afMix);
    }
}

void FxRack::processResonator (const FxSettings& s, float* l, float* r, int n, float noteHz, float mix)
{
    smoothedResHz += 0.2f * (noteHz - smoothedResHz);
    const float base = clampf (smoothedResHz * semisToRatio (s.rsTune), 20.0f, 8000.0f);
    const float mult[3] { 1.0f, 2.0f, 3.0f };
    const float fb = 0.6f + s.rsDecay * 0.395f;
    const float damp = 0.15f + s.rsBright * 0.8f;
    for (int i = 0; i < n; ++i)
    {
        float wl = 0.0f, wr = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float d = clampf ((float) sr / (base * mult[k]), 2.0f, (float) rsL[k].buf.size() - 8.0f);
            const float yl = rsL[k].read (d), yr = rsR[k].read (d);
            rsLpL[k] += damp * (yl - rsLpL[k]); rsLpR[k] += damp * (yr - rsLpR[k]);
            rsL[k].push (l[i] * 0.3f + fastTanh (rsLpL[k] * fb));
            rsR[k].push (r[i] * 0.3f + fastTanh (rsLpR[k] * fb));
            wl += yl / (1.0f + (float) k); wr += yr / (1.0f + (float) k);
        }
        l[i] += wl * mix * 0.5f; r[i] += wr * mix * 0.5f;
    }
}

// ================================================================ ducking
void Ducker::prepare (double sampleRate) { sr = sampleRate; reset(); }
void Ducker::reset() { sinceTrigger = 1.0e9; numTriggers = 0; prevBeats = -1.0; scEnv = 0.0f; lastGain = 1.0f; splitL.reset(); splitR.reset(); }

float Ducker::gainAt (const DuckSettings& s, double t) const
{
    const double a = s.attackMs * 0.001, h = s.holdMs * 0.001, r = s.releaseMs * 0.001;
    float duck;
    if (t < a) duck = (float) (t / std::max (a, 1.0e-5));
    else if (t < a + h) duck = 1.0f;
    else if (t < a + h + r)
    {
        const float x = (float) ((t - a - h) / std::max (r, 1.0e-5));
        const float e = std::exp2 (s.curve * 2.5f);      // curve > 0: slow start of recovery
        duck = 1.0f - std::pow (x, e);
    }
    else duck = 0.0f;
    return 1.0f - s.depth * clamp01 (duck);
}

void Ducker::process (const DuckSettings& s, float* l, float* r, int n, double bpm, double beats, bool playing,
                      const float* scL, const float* scR, float depthMod)
{
    DuckSettings st = s;
    st.depth = clamp01 (s.depth + depthMod);
    if (s.source == (int) DuckSource::Off) { numTriggers = 0; grDb = 0.0f; lastGain = 1.0f; return; }

    // tempo triggers (grid crossings)
    const double bps = bpm / 60.0 / sr;
    const double div = syncDivisionBeats (s.division);
    if (s.source == (int) DuckSource::Tempo)
    {
        const double startB = playing ? beats : (prevBeats < 0.0 ? 0.0 : prevBeats);
        const double endB = startB + bps * n;
        double next = std::ceil (startB / div) * div;
        while (next < endB) { trigger ((int) ((next - startB) / bps)); next += div; }
        prevBeats = endB;
    }
    splitL.set (s.freq, (float) sr); splitR.set (s.freq, (float) sr);
    const float scAtt = std::exp (-1.0f / (0.001f * (float) sr)), scRel = std::exp (-1.0f / (0.05f * (float) sr));
    const float thr = dbToGain (s.threshDb);
    int ti = 0;
    std::sort (triggers, triggers + numTriggers);
    float minGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        while (ti < numTriggers && triggers[ti] <= i) { sinceTrigger = 0.0; ++ti; }
        float g;
        if (s.source == (int) DuckSource::Sidechain && scL != nullptr)
        {
            const float x = std::max (std::abs (scL[i]), scR != nullptr ? std::abs (scR[i]) : 0.0f);
            scEnv = x + (x > scEnv ? scAtt : scRel) * (scEnv - x);
            const bool above = scEnv > thr;
            if (above && ! scGate) sinceTrigger = 0.0;
            scGate = above;
            g = gainAt (st, sinceTrigger);
        }
        else g = gainAt (st, sinceTrigger);
        sinceTrigger += 1.0 / sr;
        minGain = std::min (minGain, g);
        if (s.band == 1)
        {
            float lo, hi;
            splitL.split (l[i], lo, hi); l[i] = lo * g + hi;
            splitR.split (r[i], lo, hi); r[i] = lo * g + hi;
        }
        else { l[i] *= g; r[i] *= g; }
        lastGain = g;
    }
    numTriggers = 0;
    grDb = gainToDb (minGain);
}
} // namespace be

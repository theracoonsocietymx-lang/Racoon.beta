#include "SkunkDsp.h"

namespace racoon::skunk
{
using namespace juce;
constexpr double twoPi = MathConstants<double>::twoPi;

// ===================================================================== helpers
StringArray lfoShapeNames() { return { "Seno", "Triángulo"_u, "Sierra arriba", "Sierra abajo", "Cuadrada", "Azar" }; }

float lfoWave (int shape, double ph, float rnd)
{
    switch (shape)
    {
        case 0: return (float) std::sin (twoPi * ph);
        case 1: return ph < 0.5 ? (float) (4.0 * ph - 1.0) : (float) (3.0 - 4.0 * ph);
        case 2: return (float) (2.0 * ph - 1.0);
        case 3: return (float) (1.0 - 2.0 * ph);
        case 4: return ph < 0.5 ? 1.0f : -1.0f;
        default: return rnd;
    }
}

float window (Window w, float t)
{
    t = jlimit (0.0f, 1.0f, t);
    auto tukey = [t] (float a)
    {
        if (t < a * 0.5f) return 0.5f * (1.0f - std::cos (MathConstants<float>::twoPi * t / a));
        if (t > 1.0f - a * 0.5f) return 0.5f * (1.0f - std::cos (MathConstants<float>::twoPi * (1.0f - t) / a));
        return 1.0f;
    };
    switch (w)
    {
        case Window::hann: return 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * t);
        case Window::gauss: { const float d = (t - 0.5f) / 0.18f; return std::exp (-0.5f * d * d); }
        case Window::tukey: return tukey (0.5f);
        default: return tukey (0.1f);
    }
}

// ===================================================================== granular
void Granular::spawn (const Ring& src, int valid, const GrainParams& p, Rand& rnd)
{
    if (valid < 64) return;
    if (p.skipProb > 0.0 && rnd.uni() < p.skipProb) return;   // a silence instead of a grain
    Grain* g = nullptr;
    int active = 0;
    for (auto& x : grains) { if (x.active) ++active; else if (g == nullptr) g = &x; }
    if (g == nullptr || active >= jlimit (1, (int) grains.size(), p.maxGrains)) return;

    double semis = p.pitch;
    if (p.randPitchProb > 0.0 && rnd.uni() < p.randPitchProb) semis += p.randPitch * rnd.bi();
    const double ratio = std::pow (2.0, semis / 12.0);
    const double len = jlimit (16.0, (double) valid * 0.9, p.sizeMs * 0.001 * sr);
    double where = p.position;
    if (p.jumpProb > 0.0 && rnd.uni() < p.jumpProb) where = rnd.uni();
    const double spray = p.sprayMs * 0.001 * sr * rnd.bi();
    double start;
    if (p.live)
    {
        // back from the write head; a grain never reads past what is being written
        const double minDelay = jmax (2.0, len * (ratio - 1.0) + 2.0), maxDelay = jmax (minDelay, valid - len * ratio - 2.0);
        const double delay = jlimit (minDelay, maxDelay, where * valid + spray + len * jmax (0.0, ratio - 1.0));
        start = (double) src.write - delay;
    }
    else
    {
        double f = where + p.scan;
        f -= std::floor (f);
        start = (double) src.write - valid + f * valid + spray;
    }
    g->active = true;
    g->reverse = p.reverseProb > 0.0 && rnd.uni() < p.reverseProb;
    g->start = start;
    g->ratio = ratio;
    g->len = len;
    g->age = 0.0;
    const float pan = (float) (p.spread * rnd.bi());
    const float a = (pan + 1.0f) * MathConstants<float>::pi * 0.25f;
    g->panL = std::cos (a) * MathConstants<float>::sqrt2;
    g->panR = std::sin (a) * MathConstants<float>::sqrt2;
    g->amp = 1.0f;
}

void Granular::burst (const Ring& src, int valid, const GrainParams& p, int count, Rand& rnd)
{
    for (int i = 0; i < count; ++i) spawn (src, valid, p, rnd);
}

void Granular::render (const Ring& src, int valid, const GrainParams& p, float* outL, float* outR, int n, Rand& rnd)
{
    const double overlap = jmax (1.0, p.density * p.sizeMs * 0.001);
    const float norm = (float) (1.0 / std::sqrt (overlap));
    const double interval = sr / jmax (0.1, p.density);
    for (int i = 0; i < n; ++i)
    {
        untilNext -= 1.0;
        if (untilNext <= 0.0)
        {
            spawn (src, valid, p, rnd);
            untilNext += jmax (1.0, interval * (1.0 + p.jitter * rnd.bi()));
        }
        float l = 0.0f, r = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = window (p.win, (float) (g.age / g.len)) * g.amp;
            const double at = g.reverse ? g.start + (g.len - g.age) * g.ratio : g.start + g.age * g.ratio;
            l += src.at (0, at) * w * g.panL;
            r += src.at (1, at) * w * g.panR;
            g.age += 1.0;
            if (g.age >= g.len) g.active = false;
        }
        outL[i] += l * norm;
        outR[i] += r * norm;
    }
    // positions for the screen
    int k = 0;
    for (auto& g : grains)
        if (g.active && k < 32)
        {
            double rel = ((double) src.write - (g.start + g.age * g.ratio)) / jmax (1, valid);
            uiPos[k++].store ((float) jlimit (0.0, 1.0, 1.0 - rel));
        }
    while (k < 32) uiPos[k++].store (-1.0f);
}

// ===================================================================== spectral
void Spectral::prepare (double sampleRate, int o)
{
    sr = sampleRate;
    const int maxN = 1 << maxOrder;
    work.assign ((size_t) maxN * 2, 0.0f);
    workSc.assign ((size_t) maxN * 2, 0.0f);
    win.assign ((size_t) maxN, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        in[c].assign ((size_t) maxN, 0.0f); sc[c].assign ((size_t) maxN, 0.0f); out[c].assign ((size_t) maxN, 0.0f);
        frozenMag[c].assign ((size_t) maxN / 2 + 1, 0.0f); phase[c].assign ((size_t) maxN / 2 + 1, 0.0f);
        heldMag[c].assign ((size_t) maxN / 2 + 1, 0.0f); lastMag[c].assign ((size_t) maxN / 2 + 1, 0.0f);
    }
    spec.assign ((size_t) maxN / 2 + 1, {});
    for (auto* v : { &tmpMag, &tmpFreq, &tmpEnv, &tmpEnvSc }) v->assign ((size_t) maxN / 2 + 1, 0.0f);
    specSc.assign ((size_t) maxN / 2 + 1, {});
    setOrder (o);
}

void Spectral::setOrder (int o)
{
    ord = jlimit (9, maxOrder, o);
    n = 1 << ord;
    hop = n / 4;
    fft = std::make_unique<juce::dsp::FFT> (ord);
    for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (twoPi * i / n);
    reset();
}

void Spectral::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (in[c].begin(), in[c].end(), 0.0f); std::fill (sc[c].begin(), sc[c].end(), 0.0f); std::fill (out[c].begin(), out[c].end(), 0.0f);
        std::fill (frozenMag[c].begin(), frozenMag[c].end(), 0.0f); std::fill (phase[c].begin(), phase[c].end(), 0.0f);
        std::fill (lastMag[c].begin(), lastMag[c].end(), 0.0f);
    }
    pos = 0; count = 0; wasFrozen = false;
}

void Spectral::process (float inL, float inR, float scL, float scR, float& outL, float& outR, const SpectralParams& p, Rand& rnd)
{
    in[0][(size_t) pos] = inL; in[1][(size_t) pos] = inR;
    sc[0][(size_t) pos] = scL; sc[1][(size_t) pos] = scR;
    outL = out[0][(size_t) pos]; outR = out[1][(size_t) pos];
    out[0][(size_t) pos] = 0.0f; out[1][(size_t) pos] = 0.0f;
    pos = (pos + 1) % n;
    if (++count == hop)
    {
        count = 0;
        for (int c = 0; c < 2; ++c) frame (c, p, rnd);
        wasFrozen = p.freeze;
    }
}

void Spectral::frame (int c, const SpectralParams& p, Rand& rnd)
{
    const int bins = n / 2 + 1;
    const bool useSc = p.morphMode > 0 && p.morph > 0.0f;
    for (int i = 0; i < n; ++i) work[(size_t) i] = in[c][(size_t) ((pos + i) % n)] * win[(size_t) i];
    std::fill (work.begin() + n, work.begin() + 2 * n, 0.0f);
    fft->performRealOnlyForwardTransform (work.data(), true);
    for (int k = 0; k < bins; ++k) spec[(size_t) k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };
    if (useSc)
    {
        for (int i = 0; i < n; ++i) workSc[(size_t) i] = sc[c][(size_t) ((pos + i) % n)] * win[(size_t) i];
        std::fill (workSc.begin() + n, workSc.begin() + 2 * n, 0.0f);
        fft->performRealOnlyForwardTransform (workSc.data(), true);
        for (int k = 0; k < bins; ++k) specSc[(size_t) k] = { workSc[(size_t) (2 * k)], workSc[(size_t) (2 * k + 1)] };
    }

    const float binHz = (float) (sr / n);
    const float ratio = std::pow (2.0f, p.pitch / 12.0f) * jmax (0.1f, p.stretch);
    const float shiftBins = p.shiftHz / binHz;
    const bool remap = std::abs (ratio - 1.0f) > 1.0e-4f || std::abs (shiftBins) > 1.0e-3f;
    const bool anything = remap || p.freeze || wasFrozen || p.blur > 0.0f || p.harmonicHold > 0.0f || p.dispersion > 0.0f
                          || useSc || p.lowHz > 25.0f || p.highHz < 19500.0f || p.texture > 0.0f;

    // envelope (for formants and morphing): magnitudes smoothed over neighbouring bins
    auto envelope = [bins] (const std::vector<cpx>& s, std::vector<float>& e, int width)
    {
        double acc = 0.0;
        int a = 0, b = -1;
        for (int k = 0; k < bins; ++k)
        {
            while (b < jmin (bins - 1, k + width)) { ++b; acc += std::abs (s[(size_t) b]); }
            while (a < k - width) { acc -= std::abs (s[(size_t) a]); ++a; }
            e[(size_t) k] = (float) (acc / (b - a + 1)) + 1.0e-9f;
        }
    };
    auto& env = heldMag[c];   // reused as scratch for the main envelope

    if (anything)
    {
        if (p.keepFormants || (useSc && p.morphMode == 2) || (useSc && p.morphMode == 4)) envelope (spec, env, jmax (2, bins / 128));

        // remap (pitch, stretch, shift) with a phase vocoder
        if (remap)
        {
            auto& mag = tmpMag; auto& freq = tmpFreq; auto& envTmp = tmpEnv;
            std::fill (mag.begin(), mag.begin() + bins, 0.0f); std::fill (freq.begin(), freq.begin() + bins, 0.0f);
            auto& last = lastMag[c];   // previous input phase per bin (reused storage)
            for (int j = 0; j < bins; ++j)
            {
                const float m = std::abs (spec[(size_t) j]), ph = std::arg (spec[(size_t) j]);
                const double expected = twoPi * j * hop / n;
                double dev = ph - last[(size_t) j] - expected;
                last[(size_t) j] = ph;
                dev -= twoPi * std::floor (dev / twoPi + 0.5);
                const double trueBin = j + dev * n / (twoPi * hop);
                const double target = j * ratio + shiftBins;
                const int k = (int) std::floor (target + 0.5);
                if (k < 0 || k >= bins) continue;
                if (m > mag[(size_t) k]) freq[(size_t) k] = (float) (trueBin * ratio + shiftBins);
                mag[(size_t) k] += m;
            }
            if (p.keepFormants)
            {
                // envelope of the remapped magnitudes, then put the original envelope back
                double acc = 0.0; int a = 0, b = -1; const int width = jmax (2, bins / 128);
                for (int k = 0; k < bins; ++k)
                {
                    while (b < jmin (bins - 1, k + width)) { ++b; acc += mag[(size_t) b]; }
                    while (a < k - width) { acc -= mag[(size_t) a]; ++a; }
                    envTmp[(size_t) k] = (float) (acc / (b - a + 1)) + 1.0e-9f;
                }
                for (int k = 0; k < bins; ++k) mag[(size_t) k] *= env[(size_t) k] / envTmp[(size_t) k];
            }
            auto& ph = phase[c];
            for (int k = 0; k < bins; ++k)
            {
                ph[(size_t) k] += (float) (twoPi * freq[(size_t) k] * hop / n);
                ph[(size_t) k] = (float) std::fmod (ph[(size_t) k], twoPi);
                spec[(size_t) k] = std::polar (mag[(size_t) k], ph[(size_t) k]);
            }
        }

        // morph with the sidechain
        if (useSc)
        {
            auto& envSc = tmpEnvSc;
            if (p.morphMode == 2) envelope (specSc, envSc, jmax (2, bins / 128));
            for (int k = 0; k < bins; ++k)
            {
                const float m = std::abs (spec[(size_t) k]), ms = std::abs (specSc[(size_t) k]), ph = std::arg (spec[(size_t) k]);
                float out = m;
                switch (p.morphMode)
                {
                    case 1: spec[(size_t) k] = spec[(size_t) k] * (1.0f - p.morph) + specSc[(size_t) k] * p.morph; continue;   // crossfade
                    case 2: out = m * std::pow (envSc[(size_t) k] / env[(size_t) k], p.morph); break;           // envelope transfer
                    case 3: out = jmax (m, ms * p.morph) * (1.0f - 0.3f * p.morph) + m * 0.3f * p.morph; break; // harmonic blend
                    default:                                                                                     // noise transfer
                        spec[(size_t) k] = std::polar (m, ph) * (1.0f - p.morph) + std::polar (ms + 1.0e-9f, (float) (rnd.uni() * twoPi)) * p.morph;
                        continue;
                }
                spec[(size_t) k] = std::polar (out, ph);
            }
        }

        // freeze: hold the magnitudes, keep the phases turning
        if (p.freeze && ! wasFrozen)
            for (int k = 0; k < bins; ++k) { frozenMag[c][(size_t) k] = std::abs (spec[(size_t) k]); phase[c][(size_t) k] = std::arg (spec[(size_t) k]); }
        if (p.freeze)
        {
            const float keep = 1.0f - p.decay * 0.05f;
            for (int k = 0; k < bins; ++k)
            {
                float& fm = frozenMag[c][(size_t) k];
                fm *= keep;
                float m = fm;
                if (p.texture > 0.0f) m *= 1.0f + p.texture * 0.6f * rnd.bi();
                auto& ph = phase[c][(size_t) k];
                ph += (float) (twoPi * k * hop / n * p.phaseEvolution) + p.dispersion * (float) (rnd.bi() * MathConstants<double>::pi);
                ph = (float) std::fmod (ph, twoPi);
                spec[(size_t) k] = spec[(size_t) k] * (1.0f - p.freezeMix) + std::polar (m, ph) * p.freezeMix;
            }
        }
        else if (p.dispersion > 0.0f)
            for (int k = 0; k < bins; ++k)
                spec[(size_t) k] *= std::polar (1.0f, (float) (p.dispersion * rnd.bi() * MathConstants<double>::pi));

        // blur in time and harmonic hold (only the peaks ring)
        if (p.blur > 0.0f || p.harmonicHold > 0.0f)
        {
            auto& lm = lastMag[c];
            for (int k = 0; k < bins; ++k)
            {
                float m = std::abs (spec[(size_t) k]);
                if (p.harmonicHold > 0.0f && k > 0 && k + 1 < bins)
                {
                    const bool peak = m >= std::abs (spec[(size_t) k - 1]) && m >= std::abs (spec[(size_t) k + 1]);
                    if (! peak) m *= 1.0f - p.harmonicHold;
                }
                if (p.blur > 0.0f && ! remap) { m = jmax (m, lm[(size_t) k] * p.blur); lm[(size_t) k] = m; }
                spec[(size_t) k] = std::polar (m, std::arg (spec[(size_t) k]));
            }
        }

        // region filter
        const int lo = (int) (p.lowHz / binHz), hi = (int) (p.highHz / binHz);
        for (int k = 0; k < bins; ++k) if (k < lo || k > hi) spec[(size_t) k] = {};
    }

    // screen
    if (c == 0)
        for (int b = 0; b < 64; ++b)
        {
            const double hz = 30.0 * std::pow (600.0, b / 63.0);
            const int k = jlimit (0, bins - 1, (int) (hz / binHz));
            uiSpectrum[b].store (std::abs (spec[(size_t) k]) * 4.0f / n);
        }

    for (int k = 0; k < bins; ++k) { work[(size_t) (2 * k)] = spec[(size_t) k].real(); work[(size_t) (2 * k + 1)] = spec[(size_t) k].imag(); }
    for (int k = bins; k < n; ++k) { work[(size_t) (2 * k)] = spec[(size_t) (n - k)].real(); work[(size_t) (2 * k + 1)] = -spec[(size_t) (n - k)].imag(); }
    fft->performRealOnlyInverseTransform (work.data());
    const float g = 1.0f / 1.5f;
    for (int i = 0; i < n; ++i) out[c][(size_t) ((pos + i) % n)] += work[(size_t) i] * win[(size_t) i] * g;
}

// ===================================================================== glitch
void Glitch::prepare (double sampleRate) { sr = sampleRate; hist.prepare ((int) (sampleRate * 8.0)); reset(); }

void Glitch::reset()
{
    hist.clear();
    type = -1; elapsed = 0; lastDiv = std::numeric_limits<int64>::min(); fade = 1.0f; dropLeft = 0; dropMute = false;
    uiType.store (-1);
}

void Glitch::process (float& l, float& r, double ppq, bool running, const GlitchParams& p, Rand& rnd)
{
    hist.push (l, r);
    if (type >= 0 && elapsed >= eventLen) { type = -1; uiType.store (-1); }
    const double divSamples = p.divisionBeats * 60.0 / jmax (20.0, p.bpm) * sr;
    const int64 div = (int64) std::floor (ppq / p.divisionBeats + 1.0e-9);
    const double smooth = jmax (1.0, p.smoothMs * 0.001 * sr);
    if (running && div != lastDiv)
    {
        lastDiv = div;
        if (type < 0 && p.probability > 0.0f && rnd.uni() < p.probability)
        {
            const float w[] { p.wRepeat, p.wStutter, p.wReverse, p.wTapeStop, p.wShuffle, p.wDropout };
            float total = 0.0f;
            for (float x : w) total += jmax (0.0f, x);
            if (total > 0.0f)
            {
                float pick = rnd.uni() * total;
                type = 0;
                for (int i = 0; i < 6; ++i) { pick -= jmax (0.0f, w[i]); if (pick <= 0.0f) { type = i; break; } }
                eventLen = divSamples * jmax (1, p.durationDivs);
                elapsed = 0;
                sliceLen = jmax (32.0, type == 1 ? divSamples / std::pow (2.0, 1 + (int) (rnd.uni() * 3)) : divSamples);
                const double histLen = hist.size - 16.0;
                switch (type)
                {
                    // the newest sample (this one) sits at write - 1: the slice is the division that just ended
                    case 4: readBase = (double) hist.write - 1.0 - sliceLen * (1 + (int) (rnd.uni() * jmin (8.0, histLen / sliceLen - 1.0))); break;
                    default: readBase = (double) hist.write - 1.0 - sliceLen; break;
                }
                tapePos = 0; tapeSpeed = 1.0;
                uiType.store (type);
            }
        }
    }
    if (type < 0) return;

    float gl = 0.0f, gr = 0.0f;
    const double inSlice = std::fmod (elapsed, sliceLen);
    const float edge = (float) jmin (1.0, jmin (inSlice, sliceLen - inSlice) / jmin (smooth, sliceLen * 0.25));
    switch (type)
    {
        case 0: case 1: case 4:
            gl = hist.at (0, readBase + inSlice) * edge; gr = hist.at (1, readBase + inSlice) * edge; break;
        case 2:
            gl = hist.at (0, readBase + sliceLen - 1.0 - inSlice) * edge; gr = hist.at (1, readBase + sliceLen - 1.0 - inSlice) * edge; break;
        case 3:
        {
            // the tape slows down to a stop: the read point falls further and further behind
            tapeSpeed = jmax (0.0, 1.0 - elapsed / jmax (1.0, p.tapeStopMs * 0.001 * sr));
            tapePos += 1.0 - tapeSpeed;
            gl = hist.back (0, 1.0 + tapePos) * (float) jmin (1.0, tapeSpeed * 4.0);
            gr = hist.back (1, 1.0 + tapePos) * (float) jmin (1.0, tapeSpeed * 4.0);
            break;
        }
        default:
        {
            if (dropLeft <= 0) { dropLeft = (int) ((0.01 + rnd.uni() * 0.05) * sr); dropMute = rnd.uni() < 0.5f; }
            --dropLeft;
            gl = dropMute ? 0.0f : l; gr = dropMute ? 0.0f : r;
            break;
        }
    }
    // cross-fade in and out of the event
    const float w = (float) jmin (1.0, jmin (elapsed / smooth, (eventLen - elapsed) / smooth));
    l = l * (1.0f - w) + gl * w;
    r = r * (1.0f - w) + gr * w;
    elapsed += 1.0;
}

// ===================================================================== noise
float Noise::next (int type, float color, Rand& rnd)
{
    float x = 0.0f;
    const float w = rnd.bi();
    switch (type)
    {
        case 0: x = w; break;
        case 1:
            // Paul Kellet's pink filter
            b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
            b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
            x = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f; b[6] = w * 0.115926f;
            break;
        case 2: brown = (brown + w * 0.02f) * 0.998f; x = brown * 3.5f; break;
        case 3: x = rnd.uni() < (0.0005f + color * 0.01f) ? rnd.bi() : 0.0f; return x;
        default:
            modPhase += (float) (3.0 / sr);
            if (modPhase >= 1.0f) { modPhase -= 1.0f; mod = rnd.uni(); }
            x = w * (0.2f + 0.8f * mod);
            break;
    }
    // color: brighter (1) to darker (0)
    const float a = std::exp (-MathConstants<float>::twoPi * (200.0f + 18000.0f * color * color) / (float) sr);
    lp = x + (lp - x) * a;
    return lp;
}

// ===================================================================== filter bank
void FilterBank::process (float& l, float& r, int type, float freq, float reso, float vowel, float mix)
{
    const float in[2] { l, r };
    float out[2] {};
    const float g = Svf::gFor (freq, sr);
    const float k = 2.0f - 1.96f * jlimit (0.0f, 1.0f, reso);
    for (int c = 0; c < 2; ++c)
    {
        const float x = in[c];
        float y = x;
        switch (type)
        {
            case 0: case 1: case 2: case 3: y = svf[c].process (x, g, k, type); break;
            case 4:
            {
                // vowels A E I O U (first three formants), morphed continuously
                static const float F[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                const float v = jlimit (0.0f, 4.0f, vowel);
                const int a = jmin (3, (int) v);
                const float f = v - a;
                y = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    const float hz = F[a][i] + (F[a + 1][i] - F[a][i]) * f;
                    y += form[c][i].process (x, Svf::gFor (hz, sr), 0.15f + (1.0f - reso) * 0.4f, 1) * (i == 0 ? 1.0f : 0.7f);
                }
                y *= 1.5f;
                break;
            }
            case 5:
            {
                const double d = sr / jmax (20.0f, freq);
                const float fb = 0.97f * reso;
                y = x + comb[c].back (0, d) * fb;
                comb[c].push (y, 0.0f);
                y *= 1.0f - fb * 0.5f;
                break;
            }
            default:
            {
                static const float ratios[] { 1.0f, 2.01f, 3.03f, 4.1f };
                y = 0.0f;
                for (int i = 0; i < 4; ++i) y += res[c][i].process (x, Svf::gFor (freq * ratios[i], sr), 0.02f + (1.0f - reso) * 0.2f, 1) / (1.0f + i);
                y *= 0.8f;
                break;
            }
        }
        out[c] = y;
    }
    l = in[0] * (1.0f - mix) + out[0] * mix;
    r = in[1] * (1.0f - mix) + out[1] * mix;
}

// ===================================================================== fx rack
void FxRack::prepare (double sampleRate)
{
    sr = sampleRate;
    chorus.prepare ((int) (sampleRate * 0.05));
    delay.prepare ((int) (sampleRate * 4.0));
    verb.setSampleRate (sampleRate);
    reset();
}

void FxRack::reset()
{
    chorus.clear(); delay.clear();
    verb.reset(); distSat[0].reset(); distSat[1].reset();
    for (auto& a : apState) for (auto& x : a) x = 0.0f;
    phFb[0] = phFb[1] = delFb[0] = delFb[1] = 0.0f;
    chPhase = phPhase = 0.0;
    dcIn[0] = dcIn[1] = dcOut[0] = dcOut[1] = 0.0f;
}

void FxRack::process (int kind, float* L, float* R, int n, float a, float b, float mix, double bpm, double beats)
{
    if (mix <= 0.0f || kind <= 0) return;
    float* io[2] { L, R };
    switch (kind)
    {
        case 1:   // distortion: a = drive, b = tone
        {
            const float g = 1.0f + a * 40.0f, norm = 1.0f / std::tanh (g);
            const float tone = std::exp (-MathConstants<float>::twoPi * (800.0f + 15000.0f * b * b) / (float) sr);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = io[c][i];
                    float y = distSat[c].process (x * g) * norm;   // ADAA: up to 41x of drive without the fizz
                    dcOut[c] = y + (dcOut[c] - y) * tone;
                    io[c][i] = x * (1.0f - mix) + dcOut[c] * mix;
                }
            break;
        }
        case 2:   // chorus: a = rate, b = depth
        {
            const double inc = (0.05 + a * 4.0) / sr;
            const float depth = (float) ((1.0 + b * 9.0) * 0.001 * sr);
            for (int i = 0; i < n; ++i)
            {
                chPhase += inc; if (chPhase >= 1.0) chPhase -= 1.0;
                chorus.push (L[i], R[i]);
                const float m = (float) std::sin (twoPi * chPhase), m2 = (float) std::sin (twoPi * (chPhase + 0.25));
                const float wl = chorus.back (0, 2.0 + depth * (1.0f + m)), wr = chorus.back (1, 2.0 + depth * (1.0f + m2));
                L[i] = L[i] * (1.0f - mix * 0.5f) + wl * mix * 0.7f;
                R[i] = R[i] * (1.0f - mix * 0.5f) + wr * mix * 0.7f;
            }
            break;
        }
        case 3:   // phaser: a = rate, b = feedback
        {
            const double inc = (0.03 + a * 3.0) / sr;
            for (int i = 0; i < n; ++i)
            {
                phPhase += inc; if (phPhase >= 1.0) phPhase -= 1.0;
                for (int c = 0; c < 2; ++c)
                {
                    const double m = 0.5 + 0.5 * std::sin (twoPi * (phPhase + c * 0.25));
                    const double fc = 200.0 * std::pow (20.0, m);
                    const float t = (float) std::tan (MathConstants<double>::pi * fc / sr);
                    const float coef = (t - 1.0f) / (t + 1.0f);
                    float y = io[c][i] + phFb[c] * b * 0.9f;
                    for (int s = 0; s < 6; ++s)
                    {
                        const float o = coef * y + apState[c][s];
                        apState[c][s] = y - coef * o;
                        y = o;
                    }
                    phFb[c] = y;
                    io[c][i] = io[c][i] * (1.0f - mix * 0.5f) + y * mix * 0.5f;
                }
            }
            break;
        }
        case 4:   // delay: a = feedback, time in beats
        {
            const double d = jlimit (8.0, (double) delay.size - 8.0, beats * 60.0 / jmax (20.0, bpm) * sr);
            for (int i = 0; i < n; ++i)
            {
                const float yl = delay.back (0, d), yr = delay.back (1, d);
                delay.push (L[i] + std::tanh (yr * a * 0.95f), R[i] + std::tanh (yl * a * 0.95f));   // ping-pong
                L[i] += yl * mix; R[i] += yr * mix;
            }
            break;
        }
        case 5:   // reverb: a = size, b = damping (the family's 8-line FDN reverb, with modulated lines and real absorption)
        {
            racoon::sdsp::Verb::Parameters p;
            p.roomSize = a; p.damping = b; p.wetLevel = 0.33f; p.dryLevel = 0.0f; p.width = 1.0f;
            verb.setParameters (p);
            for (int i = 0; i < n; ++i)
            {
                float wl = L[i], wr = R[i];
                verb.processStereo (&wl, &wr, 1);
                L[i] = L[i] * (1.0f - mix * 0.5f) + wl * mix;
                R[i] = R[i] * (1.0f - mix * 0.5f) + wr * mix;
            }
            break;
        }
        default: break;
    }
}
}

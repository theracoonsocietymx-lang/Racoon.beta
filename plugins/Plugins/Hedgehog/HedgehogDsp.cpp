#include "HedgehogDsp.h"

namespace racoon::hedgehog
{
using namespace juce;

// ===================================================================== biquad
void Biquad::set (Type t, double sr, double hz, double q, double gainDb)
{
    const double w = MathConstants<double>::twoPi * jlimit (5.0, sr * 0.49, hz) / sr;
    const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * jmax (0.05, q));
    const double A = std::pow (10.0, gainDb / 40.0);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (t)
    {
        case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case allpass:  B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
        case lowShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
            A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case highShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

float Biquad::magnitudeDb (double hz, double sr) const
{
    const double w = MathConstants<double>::twoPi * hz / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const auto num = (double) b0 + (double) b1 * z1 + (double) b2 * z2;
    const auto den = 1.0 + (double) a1 * z1 + (double) a2 * z2;
    return (float) (20.0 * std::log10 (std::abs (num / den) + 1.0e-12));
}

// ===================================================================== true peak
namespace
{
struct TpTable
{
    float h[TruePeak::taps];
    TpTable()
    {
        const double centre = (TruePeak::taps - 1) * 0.5;
        for (int k = 0; k < TruePeak::taps; ++k)
        {
            const double x = (k - centre) / TruePeak::phases;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (MathConstants<double>::pi * x) / (MathConstants<double>::pi * x);
            const double win = 0.42 - 0.5 * std::cos (MathConstants<double>::twoPi * (k + 0.5) / TruePeak::taps) + 0.08 * std::cos (2.0 * MathConstants<double>::twoPi * (k + 0.5) / TruePeak::taps);
            h[k] = (float) (sinc * win);
        }
        for (int p = 0; p < TruePeak::phases; ++p)
        {
            double sum = 0.0;
            for (int j = 0; j < TruePeak::taps / TruePeak::phases; ++j) sum += h[j * TruePeak::phases + p];
            for (int j = 0; j < TruePeak::taps / TruePeak::phases; ++j) h[j * TruePeak::phases + p] = (float) (h[j * TruePeak::phases + p] / sum);
        }
    }
};
const TpTable& tpTable() { static const TpTable t; return t; }
}

void TruePeak::reset() { std::fill (std::begin (hist), std::end (hist), 0.0f); pos = 0; }

float TruePeak::process (float x)
{
    constexpr int n = taps / phases;
    hist[pos] = x;
    const auto& h = tpTable().h;
    float best = 0.0f;
    for (int p = 0; p < phases; ++p)
    {
        float y = 0.0f;
        for (int j = 0; j < n; ++j) y += h[j * phases + p] * hist[(pos - j + n) % n];
        best = jmax (best, std::abs (y));
    }
    pos = (pos + 1) % n;
    return best;
}

// ===================================================================== loudness (BS.1770 / R128)
void Loudness::prepare (double sampleRate)
{
    sr = sampleRate;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (MathConstants<double>::pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : pre)
        {
            b.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0); b.b1 = (float) (2.0 * (K * K - Vh) / a0); b.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            b.a1 = (float) (2.0 * (K * K - 1.0) / a0); b.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (MathConstants<double>::pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : rlb)
        {
            b.b0 = 1.0f; b.b1 = -2.0f; b.b2 = 1.0f;
            b.a1 = (float) (2.0 * (K * K - 1.0) / a0); b.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
    }
    blockLen = jmax (1, (int) std::round (0.1 * sr));
    gateHist.assign (bins, 0);
    stHist.assign (bins, 0);
    reset();
}

void Loudness::reset()
{
    for (auto& b : pre) b.reset();
    for (auto& b : rlb) b.reset();
    blockPos = 0; blockSum = 0.0; subCount = 0; subPos = 0;
    std::fill (std::begin (sub), std::end (sub), 0.0);
    mom = st = stMax = -70.0f;
    std::fill (gateHist.begin(), gateHist.end(), 0u);
    std::fill (stHist.begin(), stHist.end(), 0u);
    totalSeconds = 0.0;
}

void Loudness::process (const float* L, const float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float l = rlb[0].process (pre[0].process (L[i]));
        const float r = rlb[1].process (pre[1].process (R[i]));
        blockSum += (double) l * l + (double) r * r;
        if (++blockPos < blockLen) continue;
        sub[subPos] = blockSum / blockLen;
        subPos = (subPos + 1) % 30;
        subCount = jmin (30, subCount + 1);
        blockSum = 0.0; blockPos = 0;
        totalSeconds += 0.1;
        auto meanOf = [this] (int count)
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k) s += sub[(subPos - k + 30) % 30];
            return s / count;
        };
        const double mE = meanOf (jmin (4, subCount));
        mom = energyToLufs (mE);
        st = energyToLufs (meanOf (subCount));
        if (subCount >= 4 && mom > -70.0f) ++gateHist[(size_t) jlimit (0, bins - 1, (int) ((mom + 70.0f) * 10.0f))];
        if (subCount >= 30)
        {
            stMax = jmax (stMax, st);
            if (st > -70.0f) ++stHist[(size_t) jlimit (0, bins - 1, (int) ((st + 70.0f) * 10.0f))];
        }
    }
}

float Loudness::integrated() const
{
    auto energyOf = [] (int bin) { return std::pow (10.0, ((bin + 0.5) / 10.0 - 70.0 + 0.691) / 10.0); };
    double sum = 0.0, count = 0.0;
    for (int b = 0; b < bins; ++b) if (gateHist[(size_t) b] > 0) { sum += gateHist[(size_t) b] * energyOf (b); count += gateHist[(size_t) b]; }
    if (count <= 0.0) return -70.0f;
    const float relGate = energyToLufs (sum / count) - 10.0f;
    sum = 0.0; count = 0.0;
    for (int b = 0; b < bins; ++b)
        if (gateHist[(size_t) b] > 0 && (b + 0.5f) / 10.0f - 70.0f >= relGate) { sum += gateHist[(size_t) b] * energyOf (b); count += gateHist[(size_t) b]; }
    return count > 0.0 ? energyToLufs (sum / count) : -70.0f;
}

float Loudness::range() const
{
    auto energyOf = [] (int bin) { return std::pow (10.0, ((bin + 0.5) / 10.0 - 70.0 + 0.691) / 10.0); };
    double sum = 0.0, count = 0.0;
    for (int b = 0; b < bins; ++b) if (stHist[(size_t) b] > 0) { sum += stHist[(size_t) b] * energyOf (b); count += stHist[(size_t) b]; }
    if (count <= 0.0) return 0.0f;
    const float relGate = energyToLufs (sum / count) - 20.0f;
    double total = 0.0;
    for (int b = 0; b < bins; ++b) if ((b + 0.5f) / 10.0f - 70.0f >= relGate) total += stHist[(size_t) b];
    if (total <= 0.0) return 0.0f;
    float lo = 0.0f, hi = 0.0f;
    double acc = 0.0;
    bool loSet = false;
    for (int b = 0; b < bins; ++b)
    {
        if ((b + 0.5f) / 10.0f - 70.0f < relGate) continue;
        acc += stHist[(size_t) b];
        const float lufs = (b + 0.5f) / 10.0f - 70.0f;
        if (! loSet && acc >= 0.10 * total) { lo = lufs; loSet = true; }
        if (acc >= 0.95 * total) { hi = lufs; break; }
    }
    return jmax (0.0f, hi - lo);
}

// ===================================================================== third-octave spectrum
void ThirdOctave::prepare (double sampleRate)
{
    sr = sampleRate;
    fft = std::make_unique<juce::dsp::FFT> (order);
    fifo.assign (size, 0.0f);
    work.assign (size * 2, 0.0f);
    window.resize (size);
    for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / size);
    reset();
}

void ThirdOctave::reset()
{
    fifoPos = 0; numFrames = 0;
    std::fill (std::begin (longSum), std::end (longSum), 0.0);
    std::fill (std::begin (longDb), std::end (longDb), -100.0f);
    std::fill (std::begin (fastDb), std::end (fastDb), -100.0f);
    std::fill (fifo.begin(), fifo.end(), 0.0f);
}

void ThirdOctave::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i)
    {
        fifo[(size_t) fifoPos++] = x[i];
        if (fifoPos < size) continue;
        for (int k = 0; k < size; ++k) work[(size_t) k] = fifo[(size_t) k] * window[(size_t) k];
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (work.data());
        const double binHz = sr / size;
        for (int b = 0; b < numThirds; ++b)
        {
            const double c = centre (b), lo = c / std::pow (2.0, 1.0 / 6.0), hi = c * std::pow (2.0, 1.0 / 6.0);
            if (lo >= sr * 0.5) { fastDb[b] = -100.0f; continue; }
            int k0 = jmax (1, (int) std::ceil (lo / binHz)), k1 = jmin (size / 2 - 1, (int) std::floor (hi / binHz));
            if (k1 < k0) k0 = k1 = jlimit (1, size / 2 - 1, (int) std::round (c / binHz));
            double p = 0.0;
            for (int k = k0; k <= k1; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
            p /= (double) size * size * 0.25;
            const float db = (float) (10.0 * std::log10 (p + 1.0e-20));
            fastDb[b] = fastDb[b] < -99.0f ? db : fastDb[b] * 0.6f + db * 0.4f;
            longSum[b] += p;
        }
        ++numFrames;
        for (int b = 0; b < numThirds; ++b) longDb[b] = (float) (10.0 * std::log10 (longSum[b] / numFrames + 1.0e-20));
        // half overlap
        std::copy (fifo.begin() + size / 2, fifo.end(), fifo.begin());
        fifoPos = size / 2;
    }
}

// ===================================================================== dynamic EQ
void DynEqBand::prepare (double sampleRate) { sr = sampleRate; reset(); }
void DynEqBand::reset() { f[0].reset(); f[1].reset(); detector.reset(); env.env = 0.0f; dynNow = 0.0f; lastGain = 1000.0f; lastType = -1; countdown = 0; }

void DynEqBand::design (const EqBandSettings& s, float gainDb)
{
    Biquad::Type t = Biquad::peak;
    double q = s.q;
    switch (s.type)
    {
        case 1: t = Biquad::lowShelf; q = jmin (2.0, (double) s.q) * 0.7; break;
        case 2: t = Biquad::highShelf; q = jmin (2.0, (double) s.q) * 0.7; break;
        case 3: t = Biquad::highpass; break;
        case 4: t = Biquad::lowpass; break;
        default: break;
    }
    const float z[4] { f[0].z1, f[0].z2, f[1].z1, f[1].z2 };
    f[0].set (t, sr, s.hz, q, gainDb);
    f[1] = f[0];
    f[0].z1 = z[0]; f[0].z2 = z[1]; f[1].z1 = z[2]; f[1].z2 = z[3];
    lastGain = gainDb; lastType = s.type; lastHz = s.hz; lastQ = s.q;
}

void DynEqBand::process (float* mid, float* side, int n, const EqBandSettings& s, float attackMs, float releaseMs)
{
    if (! s.on) { dynNow = 0.0f; return; }
    env.set (sr, attackMs, releaseMs);
    const bool cut = s.type >= 3;
    const bool dynamic = s.dynMode != 0 && ! cut;
    if (dynamic && (s.type != lastType || s.hz != lastHz || s.q != lastQ)) detector.set (s.type == 0 ? Biquad::bandpass : s.type == 1 ? Biquad::lowpass : Biquad::highpass, sr, s.hz, jmax (0.5f, s.q));
    for (int i = 0; i < n; ++i)
    {
        if (dynamic)
        {
            const float src = s.where == 2 ? side[i] : mid[i];
            const float lv = dsp::gainToDb (env.process (std::abs (detector.process (src))) * 1.414f);
            if (s.dynMode == 1) dynNow = -jlimit (0.0f, s.rangeDb, lv - s.thresholdDb);
            else dynNow = lv < -90.0f ? 0.0f : jlimit (0.0f, s.rangeDb, s.thresholdDb - lv);
        }
        else dynNow = 0.0f;
        if (--countdown <= 0)
        {
            const float g = cut ? 0.0f : s.gainDb + dynNow;
            if (std::abs (g - lastGain) > 0.02f || s.type != lastType || s.hz != lastHz || s.q != lastQ) design (s, g);
            countdown = 16;
        }
        if (s.where != 2) mid[i] = f[0].process (mid[i]);
        if (s.where != 1) side[i] = f[1].process (side[i]);
    }
}

// ===================================================================== multiband
void Multiband::prepare (double sampleRate) { sr = sampleRate; design(); reset(); }

void Multiband::reset()
{
    for (auto& s : splits) for (auto& c : s.lp) for (auto& b : c) b.reset();
    for (auto& s : splits) for (auto& c : s.hp) for (auto& b : c) b.reset();
    for (auto& a : comp) for (auto& b : a) for (auto& c : b) c.reset();
    for (int b = 0; b < numMbBands; ++b) { env[b] = 0.0f; gainLin[b] = 1.0f; gr[b] = 0.0f; }
}

void Multiband::setCrossovers (const float* hz)
{
    bool changed = false;
    float last = 20.0f;
    for (int k = 0; k < numMbBands - 1; ++k)
    {
        const float v = jlimit (last * 1.2f, 18000.0f, hz[k]);
        if (std::abs (v - xo[k]) > 0.01f) { xo[k] = v; changed = true; }
        last = v;
    }
    if (changed) design();
}

void Multiband::design()
{
    for (int k = 0; k < numMbBands - 1; ++k)
        for (int c = 0; c < 2; ++c)
            for (int st = 0; st < 2; ++st)
            {
                auto& lp = splits[k].lp[c][st]; auto& hp = splits[k].hp[c][st];
                const float z[4] { lp.z1, lp.z2, hp.z1, hp.z2 };
                lp.set (Biquad::lowpass, sr, xo[k], 0.70710678);
                hp.set (Biquad::highpass, sr, xo[k], 0.70710678);
                lp.z1 = z[0]; lp.z2 = z[1]; hp.z1 = z[2]; hp.z2 = z[3];
            }
    for (int b = 0; b < numMbBands; ++b)
        for (int k = 0; k < numMbBands - 1; ++k)
            for (int c = 0; c < 2; ++c)
            {
                auto& a = comp[b][k][c];
                const float z1 = a.z1, z2 = a.z2;
                a.set (Biquad::allpass, sr, xo[k], 0.70710678);
                a.z1 = z1; a.z2 = z2;
            }
}

void Multiband::process (float* L, float* R, int n, const MbBandSettings* s, int solo)
{
    float* io[2] { L, R };
    float a[numMbBands], r[numMbBands], mk[numMbBands], th[numMbBands], ra[numMbBands];
    for (int b = 0; b < numMbBands; ++b)
    {
        a[b] = std::exp (-1.0f / (jmax (0.1f, s[b].attackMs) * 0.001f * (float) sr));
        r[b] = std::exp (-1.0f / (jmax (1.0f, s[b].releaseMs) * 0.001f * (float) sr));
        mk[b] = dsp::dbToGain (s[b].gainDb);
        th[b] = s[b].thresholdDb; ra[b] = jmax (1.0f, s[b].ratio);
        gr[b] = 0.0f;
    }
    for (int i = 0; i < n; ++i)
    {
        float band[numMbBands][2];
        for (int c = 0; c < 2; ++c)
        {
            float rest = io[c][i];
            for (int k = 0; k < numMbBands - 1; ++k)
            {
                auto& sp = splits[k];
                band[k][c] = sp.lp[c][1].process (sp.lp[c][0].process (rest));
                rest = sp.hp[c][1].process (sp.hp[c][0].process (rest));
            }
            band[numMbBands - 1][c] = rest;
            for (int b = 0; b < numMbBands - 2; ++b)
                for (int k = b + 1; k < numMbBands - 1; ++k) band[b][c] = comp[b][k][c].process (band[b][c]);
        }
        float outL = 0.0f, outR = 0.0f;
        for (int b = 0; b < numMbBands; ++b)
        {
            float g = 1.0f;
            if (s[b].on)
            {
                const float lv = jmax (std::abs (band[b][0]), std::abs (band[b][1]));
                env[b] = lv + (env[b] - lv) * (lv > env[b] ? a[b] : r[b]);
                const float over = dsp::gainToDb (env[b]) - th[b];
                // soft knee of 6 dB
                float red = 0.0f;
                if (over > 3.0f) red = over * (1.0f - 1.0f / ra[b]);
                else if (over > -3.0f) red = (1.0f - 1.0f / ra[b]) * (over + 3.0f) * (over + 3.0f) / 12.0f;
                g = dsp::dbToGain (-red) * mk[b];
                gr[b] = jmax (gr[b], red);
            }
            if (solo >= 0 && solo != b) continue;
            outL += band[b][0] * g;
            outR += band[b][1] * g;
        }
        L[i] = outL; R[i] = outR;
    }
}

// ===================================================================== resonance suppressor
void Resonance::prepare (double sampleRate)
{
    sr = sampleRate;
    window.resize (size);
    for (int i = 0; i < size; ++i) window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / size));
    inL.assign (size, 0.0f); inR.assign (size, 0.0f);
    outL.assign (size * 2, 0.0f); outR.assign (size * 2, 0.0f);
    frame[0].assign (size * 2, 0.0f); frame[1].assign (size * 2, 0.0f);
    mag.assign (size / 2 + 1, 0.0f); smooth.assign (size / 2 + 2, 0.0f); cut.assign (size / 2 + 1, 0.0f);
    reset();
}

void Resonance::reset()
{
    std::fill (inL.begin(), inL.end(), 0.0f); std::fill (inR.begin(), inR.end(), 0.0f);
    std::fill (outL.begin(), outL.end(), 0.0f); std::fill (outR.begin(), outR.end(), 0.0f);
    std::fill (cut.begin(), cut.end(), 0.0f);
    inPos = 0; outPos = 0; hopCount = 0;
}

void Resonance::process (float* L, float* R, int n, bool on, float depth, float sharpness, float loHz, float hiHz, float speed)
{
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) inPos] = L[i]; inR[(size_t) inPos] = R[i];
        inPos = (inPos + 1) % size;
        L[i] = outL[(size_t) outPos]; R[i] = outR[(size_t) outPos];
        outL[(size_t) outPos] = 0.0f; outR[(size_t) outPos] = 0.0f;
        outPos = (outPos + 1) % (size * 2);
        if (++hopCount == hop)
        {
            hopCount = 0;
            frameProcess (on, depth, sharpness, loHz, hiHz, speed);
        }
    }
}

void Resonance::frameProcess (bool on, float depth, float sharpness, float loHz, float hiHz, float speed)
{
    const int half = size / 2;
    std::vector<float>* in[2] { &inL, &inR };
    for (int c = 0; c < 2; ++c)
    {
        auto& fr = frame[c];
        for (int k = 0; k < size; ++k) fr[(size_t) k] = (*in[c])[(size_t) ((inPos + k) % size)] * window[(size_t) k];
        std::fill (fr.begin() + size, fr.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fr.data(), true);
    }
    if (on && depth > 0.0f)
    {
        for (int b = 0; b <= half; ++b)
        {
            float m = 0.0f;
            for (int c = 0; c < 2; ++c) { const float re = frame[c][(size_t) (2 * b)], im = frame[c][(size_t) (2 * b + 1)]; m = jmax (m, re * re + im * im); }
            mag[(size_t) b] = 10.0f * std::log10 (m + 1.0e-20f);
        }
        // the smooth shape of the spectrum (constant-Q average); what stands above it is a resonance
        smooth[0] = 0.0f;
        for (int b = 0; b <= half; ++b) smooth[(size_t) b + 1] = smooth[(size_t) b] + mag[(size_t) b];
        const float octaves = 1.0f - 0.83f * sharpness;   // 1 octave .. 1/6
        const double binHz = sr / size;
        const float att = 0.3f + 0.6f * speed, rel = 0.05f + 0.3f * speed;
        for (int b = 1; b < half; ++b)
        {
            const int w = jmax (2, (int) (b * (std::pow (2.0f, octaves * 0.5f) - 1.0f)));
            const int lo = jmax (0, b - w), hi = jmin (half, b + w);
            const float avg = (smooth[(size_t) hi + 1] - smooth[(size_t) lo]) / (float) (hi - lo + 1);
            const double hz = b * binHz;
            float target = 0.0f;
            if (hz >= loHz && hz <= hiHz && mag[(size_t) b] > -100.0f)
            {
                const float excess = mag[(size_t) b] - avg - 3.0f;
                if (excess > 0.0f) target = -jmin (depth * 18.0f, excess * depth * 1.5f);
            }
            auto& cb = cut[(size_t) b];
            cb += (target - cb) * (target < cb ? att : rel);
            const float g = dsp::dbToGain (cb);
            for (int c = 0; c < 2; ++c) { frame[c][(size_t) (2 * b)] *= g; frame[c][(size_t) (2 * b + 1)] *= g; }
        }
    }
    else std::fill (cut.begin(), cut.end(), 0.0f);

    // back to time, windowed again; sqrt-Hann twice at 75 % overlap adds up to 2
    std::vector<float>* out[2] { &outL, &outR };
    for (int c = 0; c < 2; ++c)
    {
        auto& fr = frame[c];
        fft.performRealOnlyInverseTransform (fr.data());
        for (int k = 0; k < size; ++k)
            (*out[c])[(size_t) ((outPos + k) % (size * 2))] += fr[(size_t) k] * window[(size_t) k] * 0.5f;
    }
}

// ===================================================================== limiter
void Limiter::prepare (double sampleRate)
{
    sr = sampleRate;
    look = jmax (8, (int) std::round (0.0015 * sr));
    const int d = look + TruePeak::latency + 1;
    delayL.assign ((size_t) d, 0.0f); delayR.assign ((size_t) d, 0.0f);
    avg.assign ((size_t) look, 1.0f);
    qT.assign ((size_t) look + 4, 0); qV.assign ((size_t) look + 4, 1.0f);
    reset();
}

void Limiter::reset()
{
    tp[0].reset(); tp[1].reset();
    std::fill (delayL.begin(), delayL.end(), 0.0f); std::fill (delayR.begin(), delayR.end(), 0.0f);
    std::fill (avg.begin(), avg.end(), 1.0f);
    avgSum = (double) look;
    dPos = 0; rPos = 0; relNow = 1.0f; grDb = 0.0f; t = 0;
    qHead = qTail = qCount = 0;
}

void Limiter::process (float* L, float* R, int n, float inputGainDb, float ceilingDb, float releaseMs)
{
    const float g = dsp::dbToGain (inputGainDb), ceil = dsp::dbToGain (ceilingDb);
    const float relCoef = 1.0f - std::exp (-1.0f / (jmax (1.0f, releaseMs) * 0.001f * (float) sr));
    const int cap = (int) qT.size();
    const int dLen = (int) delayL.size();
    float minGain = 1.0f;
    for (int i = 0; i < n; ++i, ++t)
    {
        const float xl = L[i] * g, xr = R[i] * g;
        const float p = jmax (tp[0].process (xl), tp[1].process (xr));
        const float need = p > ceil ? ceil / p : 1.0f;
        // minimum of the last look + 1 needs
        while (qCount > 0 && qV[(size_t) ((qTail - 1 + cap) % cap)] >= need) { qTail = (qTail - 1 + cap) % cap; --qCount; }
        qT[(size_t) qTail] = t; qV[(size_t) qTail] = need; qTail = (qTail + 1) % cap; ++qCount;
        while (qCount > 0 && qT[(size_t) qHead] <= t - (look + 1)) { qHead = (qHead + 1) % cap; --qCount; }
        const float m = qV[(size_t) qHead];
        relNow = m < relNow ? m : relNow + (m - relNow) * relCoef;
        avgSum += relNow - avg[(size_t) rPos];
        avg[(size_t) rPos] = relNow;
        rPos = (rPos + 1) % look;
        const float s = (float) jmin (1.0, avgSum / look);
        // the audio waits for the gain
        const int readPos = (dPos + 1) % dLen;   // oldest = look + latency samples ago
        const float yl = delayL[(size_t) readPos] * s, yr = delayR[(size_t) readPos] * s;
        delayL[(size_t) dPos] = xl; delayR[(size_t) dPos] = xr;
        dPos = (dPos + 1) % dLen;
        L[i] = jlimit (-ceil, ceil, yl);
        R[i] = jlimit (-ceil, ceil, yr);
        minGain = jmin (minGain, s);
    }
    grDb = -dsp::gainToDb (minGain);
}

// ===================================================================== exciter
void Exciter::prepare (double sampleRate) { sr = sampleRate; lastHz = 0.0f; reset(); }
void Exciter::reset() { for (int c = 0; c < 2; ++c) { hp1[c].reset(); hp2[c].reset(); } }

void Exciter::process (float* L, float* R, int n, float hz, float drive, float mix, int mode)
{
    if (hz != lastHz)
    {
        for (int c = 0; c < 2; ++c) { hp1[c].set (Biquad::highpass, sr, hz, 0.7071); hp2[c].set (Biquad::highpass, sr, hz, 0.7071); }
        lastHz = hz;
    }
    const float k = 1.0f + drive * 20.0f, norm = 1.0f / (1.0f + drive * 3.0f);
    float* io[2] { L, R };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float x = hp1[c].process (io[c][i]);
            // ADAA: the new harmonics are averaged between samples, so the top octave gets harmonics, not aliases
            const float y = mode == 1 ? exAsym[c].process (x * k, 0.3f) : exSoft[c].process (x * k);
            io[c][i] += hp2[c].process (y * norm) * mix;
        }
}

// ===================================================================== imager
void Imager::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int st = 0; st < 2; ++st)
    {
        lowLp[st].set (Biquad::lowpass, sr, 250.0, 0.70710678); lowHp[st].set (Biquad::highpass, sr, 250.0, 0.70710678);
        highLp[st].set (Biquad::lowpass, sr, 4000.0, 0.70710678); highHp[st].set (Biquad::highpass, sr, 4000.0, 0.70710678);
    }
    lastMono = -1.0f;
    reset();
}

void Imager::reset()
{
    for (int st = 0; st < 2; ++st) { lowLp[st].reset(); lowHp[st].reset(); highLp[st].reset(); highHp[st].reset(); monoHp[st].reset(); }
    corrLR = corrLL = corrRR = 0.0;
}

void Imager::process (float* L, float* R, int n, float lowW, float midW, float highW, float monoBelowHz, float& correlation)
{
    const bool bands = std::abs (lowW - 1.0f) > 0.001f || std::abs (midW - 1.0f) > 0.001f || std::abs (highW - 1.0f) > 0.001f;
    const bool mono = monoBelowHz > 0.0f;
    if (mono && monoBelowHz != lastMono)
    {
        for (auto& b : monoHp) { const float z1 = b.z1, z2 = b.z2; b.set (Biquad::highpass, sr, monoBelowHz, 0.70710678); b.z1 = z1; b.z2 = z2; }
        lastMono = monoBelowHz;
    }
    const double decay = std::exp (-1.0 / (0.3 * sr));
    for (int i = 0; i < n; ++i)
    {
        const float m = (L[i] + R[i]) * 0.5f;
        float s = (L[i] - R[i]) * 0.5f;
        if (bands)
        {
            const float lo = lowLp[1].process (lowLp[0].process (s));
            const float rest = lowHp[1].process (lowHp[0].process (s));
            const float hi = highHp[1].process (highHp[0].process (rest));
            const float mi = highLp[1].process (highLp[0].process (rest));
            s = lo * lowW + mi * midW + hi * highW;
        }
        if (mono) s = monoHp[1].process (monoHp[0].process (s));
        L[i] = m + s; R[i] = m - s;
        corrLR = corrLR * decay + (double) L[i] * R[i];
        corrLL = corrLL * decay + (double) L[i] * L[i];
        corrRR = corrRR * decay + (double) R[i] * R[i];
    }
    const double den = std::sqrt (corrLL * corrRR);
    correlation = den > 1.0e-9 ? (float) (corrLR / den) : 1.0f;
}

// ===================================================================== low end
void LowEnd::prepare (double sampleRate) { sr = sampleRate; reset(); }
void LowEnd::reset() { lowBand.reset(); shelf[0].reset(); shelf[1].reset(); fast = slow = 0.0f; lastGain = 1000.0f; countdown = 0; }

void LowEnd::process (float* L, float* R, int n, float punch, float tight, float hz)
{
    lowBand.set (Biquad::lowpass, sr, hz, 0.7071);
    const float fa = std::exp (-1.0f / (0.004f * (float) sr)), fr = std::exp (-1.0f / (0.04f * (float) sr));
    const float sa = std::exp (-1.0f / (0.06f * (float) sr)), sr2 = std::exp (-1.0f / (0.2f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float lv = std::abs (lowBand.process ((L[i] + R[i]) * 0.5f));
        fast = lv + (fast - lv) * (lv > fast ? fa : fr);
        slow = lv + (slow - lv) * (lv > slow ? sa : sr2);
        if (--countdown <= 0)
        {
            const float d = dsp::gainToDb (fast + 1.0e-9f) - dsp::gainToDb (slow + 1.0e-9f);
            const float boost = punch * jlimit (0.0f, 9.0f, d);
            const float cut = tight * jlimit (0.0f, 12.0f, dsp::gainToDb (slow + 1.0e-9f) + 24.0f) * 0.5f;
            const float g = jlimit (-12.0f, 9.0f, boost - cut);
            if (std::abs (g - lastGain) > 0.05f)
            {
                for (auto& b : shelf) { const float z1 = b.z1, z2 = b.z2; b.set (Biquad::lowShelf, sr, hz, 0.7071, g); b.z1 = z1; b.z2 = z2; }
                lastGain = g;
            }
            countdown = 32;
        }
        L[i] = shelf[0].process (L[i]);
        R[i] = shelf[1].process (R[i]);
    }
}

// ===================================================================== transients
void Transient::prepare (double sampleRate) { sr = sampleRate; reset(); }
void Transient::reset() { fast = slow = 0.0f; gain = 1.0f; }

void Transient::process (float* L, float* R, int n, float attack, float sustain)
{
    const float fa = std::exp (-1.0f / (0.0005f * (float) sr)), fr = std::exp (-1.0f / (0.03f * (float) sr));
    const float sa = std::exp (-1.0f / (0.02f * (float) sr)), srl = std::exp (-1.0f / (0.12f * (float) sr));
    const float sm = std::exp (-1.0f / (0.001f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float lv = jmax (std::abs (L[i]), std::abs (R[i]));
        fast = lv + (fast - lv) * (lv > fast ? fa : fr);
        slow = lv + (slow - lv) * (lv > slow ? sa : srl);
        const float d = dsp::gainToDb (fast + 1.0e-9f) - dsp::gainToDb (slow + 1.0e-9f);
        const float gDb = attack * jlimit (0.0f, 12.0f, d) * 0.5f + sustain * jlimit (0.0f, 12.0f, -d) * 0.5f;
        const float target = dsp::dbToGain (jlimit (-12.0f, 12.0f, gDb));
        gain = target + (gain - target) * sm;
        L[i] *= gain; R[i] *= gain;
    }
}

// ===================================================================== third-octave EQ
void ThirdEq::prepare (double sampleRate) { sr = sampleRate; float z[numThirds] {}; setGains (z); reset(); }
void ThirdEq::reset() { for (auto& b : f) { b[0].reset(); b[1].reset(); } }

void ThirdEq::setGains (const float* db)
{
    isFlat = true;
    for (int b = 0; b < numThirds; ++b)
    {
        if (std::abs (db[b]) > 0.01f) isFlat = false;
        const double c = ThirdOctave::centre (b);
        for (auto& x : f[b])
        {
            const float z1 = x.z1, z2 = x.z2;
            if (c < sr * 0.45) x.set (Biquad::peak, sr, c, 4.32, db[b]);
            else x = Biquad {};
            x.z1 = z1; x.z2 = z2;
        }
    }
}

void ThirdEq::process (float* L, float* R, int n)
{
    if (isFlat) return;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        for (auto& b : f) { l = b[0].process (l); r = b[1].process (r); }
        L[i] = l; R[i] = r;
    }
}

// ===================================================================== tonal targets
StringArray genreNames() { return { "Pop", "Electrónica"_u, "Hip-hop", "Rock", "Acústico"_u, "Cine / orquesta" }; }

float targetDb (int genre, int b)
{
    const float hz = ThirdOctave::centre (b);
    const float oct = std::log2 (hz / 1000.0f);
    // a gentle tilt (about -3 dB per octave above 1 kHz), a little lift in the lows and the roll-off under 40 Hz
    float db = oct > 0.0f ? -3.2f * oct : -1.2f * oct;
    if (hz < 40.0f) db -= (40.0f - hz) * 0.35f;
    switch (genre)
    {
        case 1: if (hz < 120.0f) db += 3.5f; if (hz > 6000.0f) db += 1.0f; break;          // electrónica: sub y aire
        case 2: if (hz < 100.0f) db += 4.5f; if (hz > 1500.0f && hz < 5000.0f) db -= 1.0f; break;
        case 3: if (hz > 800.0f && hz < 4000.0f) db += 1.5f; if (hz < 60.0f) db -= 2.0f; break;
        case 4: if (hz < 80.0f) db -= 4.0f; if (hz > 2000.0f && hz < 8000.0f) db += 1.0f; break;
        case 5: if (hz < 50.0f) db -= 2.0f; if (hz > 5000.0f) db -= 1.5f; break;
        default: break;
    }
    return db;
}

// ===================================================================== Partícula
void Particle::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < 2; ++s)
        {
            lp1[c][s].set (Biquad::lowpass, sr, lowXo, 0.7071);  hp1[c][s].set (Biquad::highpass, sr, lowXo, 0.7071);
            lp2[c][s].set (Biquad::lowpass, sr, highXo, 0.7071); hp2[c][s].set (Biquad::highpass, sr, highXo, 0.7071);
            apLp[c][s].set (Biquad::lowpass, sr, highXo, 0.7071); apHp[c][s].set (Biquad::highpass, sr, highXo, 0.7071);
        }
    for (int s = 0; s < 2; ++s)
    {
        scLp1[s].set (Biquad::lowpass, sr, lowXo, 0.7071);  scHp1[s].set (Biquad::highpass, sr, lowXo, 0.7071);
        scLp2[s].set (Biquad::lowpass, sr, highXo, 0.7071); scHp2[s].set (Biquad::highpass, sr, highXo, 0.7071);
    }
    for (auto& a : air) a.set (Biquad::highpass, sr, 7000.0, 0.7071);
    // low band slow, highs quick
    const float atkMs[3] { 30.0f, 12.0f, 4.0f }, relMs[3] { 220.0f, 140.0f, 90.0f };
    for (int b = 0; b < 3; ++b)
    {
        atk[b] = (float) std::exp (-1.0 / (sr * atkMs[b] * 0.001));
        rel[b] = (float) std::exp (-1.0 / (sr * relMs[b] * 0.001));
    }
    smooth = (float) std::exp (-1.0 / (sr * 0.004));
    lastLow = lastMid = lastHigh = 99.0f;
    reset();
}

void Particle::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        preLow[c].reset(); preMid[c].reset(); preHigh[c].reset(); air[c].reset();
        for (int s = 0; s < 2; ++s) { lp1[c][s].reset(); hp1[c][s].reset(); lp2[c][s].reset(); hp2[c][s].reset(); apLp[c][s].reset(); apHp[c][s].reset(); }
    }
    for (int s = 0; s < 2; ++s) { scLp1[s].reset(); scHp1[s].reset(); scLp2[s].reset(); scHp2[s].reset(); }
    for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gainSm[b] = 1.0f; gr[b] = 0.0f; }
}

void Particle::process (float* L, float* R, const float* scL, const float* scR, int n, const ParticleSettings& s)
{
    if (s.lowDb != lastLow)   { for (auto& f : preLow) f.set (Biquad::lowShelf, sr, 110.0, 0.7071, s.lowDb); lastLow = s.lowDb; }
    if (s.midDb != lastMid)   { for (auto& f : preMid) f.set (Biquad::peak, sr, 1000.0, 0.6, s.midDb); lastMid = s.midDb; }
    if (s.highDb != lastHigh) { for (auto& f : preHigh) f.set (Biquad::highShelf, sr, 9000.0, 0.7071, s.highDb); lastHigh = s.highDb; }

    const float A = juce::jlimit (0.0f, 2.0f, s.amount);
    const float color = std::min (1.0f, A);                     // saturation and width fade in up to 100 %
    // the compression gets firmer and starts earlier as AMOUNT rises
    const float ratio = 1.0f + 1.5f * A;
    const float baseThr[3] { -12.0f, -15.0f, -21.0f };
    float thr[3];
    for (int b = 0; b < 3; ++b) thr[b] = baseThr[b] - 3.0f * A;
    const float makeup[3] { dsp::dbToGain (1.6f * A), dsp::dbToGain (1.4f * A), dsp::dbToGain (1.8f * A) };
    const float knee = 6.0f;
    const bool external = s.external && scL != nullptr;
    float maxGr[3] {};

    auto satOf = [] (float x, float k) { return std::tanh (x * k) / k; };

    for (int i = 0; i < n; ++i)
    {
        float band[3][2];
        float x[2] { preHigh[0].process (preMid[0].process (preLow[0].process (L[i]))),
                     preHigh[1].process (preMid[1].process (preLow[1].process (R[i]))) };
        for (int c = 0; c < 2; ++c)
        {
            // Linkwitz-Riley 4 at 200 Hz and 3 kHz; the low band goes through the 3 kHz pair too, so the sum is flat
            float lo = lp1[c][1].process (lp1[c][0].process (x[c]));
            float rest = hp1[c][1].process (hp1[c][0].process (x[c]));
            const float mid = lp2[c][1].process (lp2[c][0].process (rest));
            const float hi = hp2[c][1].process (hp2[c][0].process (rest));
            lo = apLp[c][1].process (apLp[c][0].process (lo)) + apHp[c][1].process (apHp[c][0].process (lo));
            band[0][c] = lo; band[1][c] = mid; band[2][c] = hi;
        }

        // what each band's compressor listens to
        float det[3];
        if (external)
        {
            const float m = 0.5f * (scL[i] + (scR != nullptr ? scR[i] : scL[i]));
            const float lo = scLp1[1].process (scLp1[0].process (m));
            const float rest = scHp1[1].process (scHp1[0].process (m));
            det[0] = std::abs (lo);
            det[1] = std::abs (scLp2[1].process (scLp2[0].process (rest)));
            det[2] = std::abs (scHp2[1].process (scHp2[0].process (rest)));
        }
        else
            for (int b = 0; b < 3; ++b) det[b] = std::max (std::abs (band[b][0]), std::abs (band[b][1]));

        for (int b = 0; b < 3; ++b)
        {
            float g = 1.0f;
            if (s.dynamics && A > 0.0f)
            {
                env[b] = det[b] + (env[b] - det[b]) * (det[b] > env[b] ? atk[b] : rel[b]);
                const float lv = dsp::gainToDb (env[b]);
                const float over = lv - thr[b];
                float red = 0.0f;
                if (over > knee * 0.5f) red = over * (1.0f - 1.0f / ratio);
                else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee); }
                g = dsp::dbToGain (-red) * makeup[b];
                maxGr[b] = std::max (maxGr[b], red);
            }
            gainSm[b] = g + (gainSm[b] - g) * smooth;
            band[b][0] *= gainSm[b];
            band[b][1] *= gainSm[b];
        }

        if (color > 0.0f)
        {
            // warmth in the lows and the body, a touch of air on top
            const float kLo = 1.0f + 1.2f * A, kMid = 1.0f + 0.7f * A;
            for (int c = 0; c < 2; ++c)
            {
                band[0][c] += (satOf (band[0][c], kLo) - band[0][c]) * color;
                band[1][c] += (satOf (band[1][c], kMid) - band[1][c]) * color;
                const float h = air[c].process (band[2][c]);
                band[2][c] += 0.06f * A * airSat[c].process (h * 4.0f);   // ADAA (it only adds harmonics, the delay of half a sample does not matter)
            }
            // lows tighter in the middle, mids and highs a little wider
            const float lowSide = 1.0f - 0.35f * color, wide = 1.0f + 0.1f * A;
            const float lm = 0.5f * (band[0][0] + band[0][1]), ls = 0.5f * (band[0][0] - band[0][1]) * lowSide;
            band[0][0] = lm + ls; band[0][1] = lm - ls;
            for (int b = 1; b < 3; ++b)
            {
                const float m = 0.5f * (band[b][0] + band[b][1]), sd = 0.5f * (band[b][0] - band[b][1]) * wide;
                band[b][0] = m + sd; band[b][1] = m - sd;
            }
        }

        L[i] = band[0][0] + band[1][0] + band[2][0];
        R[i] = band[0][1] + band[1][1] + band[2][1];
    }
    for (int b = 0; b < 3; ++b) gr[b] = maxGr[b] > gr[b] ? maxGr[b] : gr[b] * 0.85f + maxGr[b] * 0.15f;
}
}

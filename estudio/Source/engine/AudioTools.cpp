#include "AudioTools.h"
#include "Session.h"
#include <juce_dsp/juce_dsp.h>
#include <numeric>

namespace racoon::audio
{
using namespace juce;

namespace
{
constexpr double pi = MathConstants<double>::pi;
float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
float gainToDb (float g)  { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

// the clip's sound as one mono line (for pitch and key)
std::vector<float> monoOf (const AudioBuffer<float>& b)
{
    std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
    const float k = 1.0f / (float) jmax (1, b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const auto* x = b.getReadPointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] += x[i] * k;
    }
    return m;
}
}

// ===================================================================== basic processes
float peakOf (const AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) p = jmax (p, b.getMagnitude (c, 0, b.getNumSamples()));
    return p;
}

void applyGainDb (AudioBuffer<float>& b, float db) { b.applyGain (dbToGain (db)); }
void invertPhase (AudioBuffer<float>& b)           { b.applyGain (-1.0f); }

void removeDC (AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* x = b.getWritePointer (c);
        double mean = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i) mean += x[i];
        mean /= jmax (1, b.getNumSamples());
        const float m = (float) mean;
        for (int i = 0; i < b.getNumSamples(); ++i) x[i] -= m;
    }
}

float normalizeGain (const AudioBuffer<float>& b, float targetDb)
{
    const float p = peakOf (b);
    return p > 1.0e-6f ? dbToGain (targetDb) / p : 1.0f;
}

// ===================================================================== strip silence (SoundCraft's SilenceScan)
std::vector<Range<int64>> nonSilentRanges (const AudioBuffer<float>& b, float thresholdDb, int64 minGap, int64 padBefore, int64 padAfter)
{
    const float thr = dbToGain (thresholdDb);
    const int64 len = b.getNumSamples();
    std::vector<Range<int64>> raw;
    int64 start = -1, lastLoud = 0;
    for (int64 i = 0; i < len; ++i)
    {
        bool loud = false;
        for (int c = 0; c < b.getNumChannels() && ! loud; ++c) loud = std::abs (b.getSample (c, (int) i)) > thr;
        if (! loud) continue;
        if (start >= 0 && i - lastLoud > jmax ((int64) 1, minGap)) { raw.push_back ({ start, lastLoud + 1 }); start = i; }
        else if (start < 0) start = i;
        lastLoud = i;
    }
    if (start >= 0) raw.push_back ({ start, lastLoud + 1 });
    std::vector<Range<int64>> out;
    for (auto r : raw)
    {
        const int64 s = jmax ((int64) 0, r.getStart() - padBefore), e = jmin (len, r.getEnd() + padAfter);
        if (! out.empty() && s <= out.back().getEnd()) out.back().setEnd (jmax (out.back().getEnd(), e));
        else out.push_back ({ s, e });
    }
    return out;
}

// ===================================================================== transients (SoundCraft's detect_transients)
namespace
{
// triangular filters 24 to the octave from 30 Hz to 20 kHz over the bins of an n-point FFT; each sums to 1
std::vector<std::vector<std::pair<int, float>>> logBands (int n, double sr)
{
    const double binHz = sr / n, top = jmin (20000.0, sr * 0.45);
    std::vector<int> centres;
    for (double f = 30.0; f <= top; f *= std::pow (2.0, 1.0 / 24.0))
    {
        const int k = (int) std::round (f / binHz);
        if (centres.empty() || k > centres.back()) centres.push_back (k);
    }
    std::vector<std::vector<std::pair<int, float>>> bands;
    for (size_t w = 0; w + 2 < centres.size(); ++w)
    {
        const int lo = centres[w], mid = centres[w + 1], hi = centres[w + 2];
        std::vector<std::pair<int, float>> tri;
        float sum = 0.0f;
        for (int k = lo; k <= hi; ++k)
        {
            const float v = k <= mid ? (float) (k - lo) / (float) jmax (1, mid - lo) : (float) (hi - k) / (float) jmax (1, hi - mid);
            if (v > 0.0f) { tri.push_back ({ k, v }); sum += v; }
        }
        if (sum > 0.0f)
        {
            for (auto& t : tri) t.second /= sum;
            bands.push_back (std::move (tri));
        }
    }
    return bands;
}

// where the attack found in the frame at `frame` starts: the step edge of the high-passed energy (the sample where the
// next 3 ms gain most over the 6 ms before), then back to where the energy first crosses the midpoint between the two
std::pair<int64, double> attackStart (const AudioBuffer<float>& ch, int64 frame, int n, double sr)
{
    const int64 before = jmax ((int64) 1, (int64) (sr * 0.006)), after = jmax ((int64) 1, (int64) (sr * 0.003)),
                shortW = jmax ((int64) 1, (int64) (sr * 0.0005));
    const int64 len = ch.getNumSamples();
    const int64 from = jmax ((int64) 0, frame - before), to = frame + n + n / 2 + after;
    std::vector<double> cum ((size_t) (to - from + 1), 0.0);
    for (int64 i = from, j = 0; i < to; ++i, ++j)
    {
        double e = 0.0;
        if (i < len)
            for (int c = 0; c < ch.getNumChannels(); ++c)
            {
                const float cur = ch.getSample (c, (int) i), prev = i > 0 ? ch.getSample (c, (int) (i - 1)) : 0.0f;
                e += (double) (cur - prev) * (cur - prev);
            }
        cum[(size_t) j + 1] = cum[(size_t) j] + e;
    }
    auto at = [&] (int64 i) { const int64 k = jlimit ((int64) 0, (int64) cum.size() - 1, i - from); return cum[(size_t) k]; };
    auto mean = [&] (int64 a, int64 b) { return (at (b) - at (a)) / (double) jmax ((int64) 1, b - a); };
    auto contrast = [&] (int64 k) { return std::log ((mean (k, k + after) + 1.0e-12) / (mean (jmax ((int64) 0, k - before), k) + 1.0e-12)); };
    int64 best = frame;
    double bestC = -1.0e30;
    for (int64 k = frame; k < frame + n + n / 2; ++k)
    {
        const double c = contrast (k);
        if (c > bestC) { bestC = c; best = k; }
    }
    const double level = std::sqrt (mean (jmax ((int64) 0, best - before), best) * mean (best, best + after));
    for (int64 i = jmax ((int64) 0, best - after); i <= best; ++i)
        if (mean (i, i + shortW) > level) return { i, bestC };
    return { best, bestC };
}
}

std::vector<int64> detectTransients (const AudioBuffer<float>& input, double sampleRate, float sensitivity)
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float sens = jlimit (0.0f, 1.0f, std::isfinite (sensitivity) ? sensitivity : 0.5f);
    const int n = nextPowerOfTwo (jlimit (64, 8192, (int) (sr * 0.012)));
    const int order = roundToInt (std::log2 ((double) n));
    const int hop = n / 4;
    const int64 len = input.getNumSamples();
    if (len < n || input.getNumChannels() == 0) return {};
    const int nch = jmin (2, input.getNumChannels());

    const auto bands = logBands (n, sr);
    dsp::FFT fft (order);
    std::vector<float> window ((size_t) n);
    for (int i = 0; i < n; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / n));
    // magnitudes relative to a full-scale sine; below about -80 dB a band's changes barely count
    const float gain = 1.0e4f / ((float) n / 4.0f);
    const int64 frames = (len - n) / hop + 1;
    std::vector<float> buf ((size_t) n * 2), power ((size_t) n / 2 + 1), prev, cur (bands.size()), flux, energy;
    flux.reserve ((size_t) frames);
    energy.reserve ((size_t) frames);
    for (int64 m = 0; m < frames; ++m)
    {
        std::fill (power.begin(), power.end(), 0.0f);
        for (int c = 0; c < nch; ++c)
        {
            const auto* x = input.getReadPointer (c);
            for (int i = 0; i < n; ++i) buf[(size_t) i] = x[m * hop + i] * window[(size_t) i];
            std::fill (buf.begin() + n, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (size_t k = 0; k < power.size(); ++k) power[k] += buf[k] * buf[k] / (float) nch;
        }
        energy.push_back (std::accumulate (power.begin(), power.end(), 0.0f));
        for (size_t b = 0; b < bands.size(); ++b)
        {
            float s = 0.0f;
            for (auto& [k, w] : bands[b]) s += std::sqrt (power[(size_t) k]) * w;
            cur[b] = std::log10 (1.0f + gain * s);
        }
        float f = 0.0f;
        if (! prev.empty())
            for (size_t b = 0; b < cur.size(); ++b)
            {
                float around = 0.0f;
                for (size_t j = b > 0 ? b - 1 : 0; j < jmin (prev.size(), b + 2); ++j) around = jmax (around, prev[j]);
                f += jmax (0.0f, cur[b] - around);
            }
        flux.push_back (f);
        prev = cur;
    }
    auto framesOf = [&] (double secs) { return jmax ((int64) 1, (int64) (secs * sr) / hop); };
    const int64 nearF = framesOf (0.03), beforeF = framesOf (0.1), afterF = framesOf (0.07), gapF = framesOf (0.03);
    const float delta = 2.5f * std::pow (2.0f, (0.5f - sens) * 4.0f);
    std::vector<int64> out;
    int64 last = -1;
    for (int64 m = 0; m < frames; ++m)
    {
        const float v = flux[(size_t) m];
        float peak = 0.0f, sum = 0.0f;
        for (int64 j = jmax ((int64) 0, m - nearF); j <= jmin (frames - 1, m + nearF); ++j) peak = jmax (peak, flux[(size_t) j]);
        const int64 a = jmax ((int64) 0, m - beforeF), b = jmin (frames - 1, m + afterF);
        for (int64 j = a; j <= b; ++j) sum += flux[(size_t) j];
        const float meanF = sum / (float) jmax ((int64) 1, b - a + 1);
        if (v <= 0.0f || v < peak || v < meanF + delta || (last >= 0 && m - last < gapF)) continue;
        auto [startAt, contrast] = attackStart (input, m * hop, n, sr);
        // a fast release smears energy between a note's partials, which looks like a rise: its attack edge is weak and
        // the energy does not grow, unlike a real onset's
        const float was = m > 0 ? energy[(size_t) m - 1] : 0.0f;
        bool grows = false;
        for (int64 j = m; j < jmin (frames, m + 2); ++j) grows = grows || energy[(size_t) j] > was;
        if (contrast < std::log (2.0) && ! grows) continue;
        last = m;
        if (out.empty() || startAt > out.back()) out.push_back (startAt);
    }
    return out;
}

// ===================================================================== pitch (YIN) and notes
std::optional<float> yin (const float* frame, int n, double sampleRate, float minHz, float maxHz, float threshold)
{
    if (! (sampleRate > 0.0) || n < 64) return {};
    const int maxTau = jmin ((int) (sampleRate / jmax (20.0f, minHz)), n / 2);
    const int minTau = jmax (2, (int) (sampleRate / jmax (maxHz, minHz + 1.0f)));
    if (maxTau <= minTau + 2) return {};
    const int w = n - maxTau;
    std::vector<float> d ((size_t) maxTau + 1, 0.0f), cmnd ((size_t) maxTau + 1, 1.0f);
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < w; ++j) { const float diff = frame[j] - frame[j + tau]; sum += diff * diff; }
        d[(size_t) tau] = sum;
    }
    float running = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
    }
    for (int tau = minTau; tau < maxTau; ++tau)
    {
        if (cmnd[(size_t) tau] >= threshold) continue;
        while (tau + 1 < maxTau && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
        const float a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
        const float denom = a - 2.0f * b + c;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
        const float t = (float) tau + jlimit (-1.0f, 1.0f, shift);
        if (t > 0.0f) return (float) sampleRate / t;
        return {};
    }
    return {};
}

std::vector<DetectedNote> audioToNotes (const float* audio, int64 n, double sampleRate)
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int win = jlimit (256, 8192, (int) (sr * 0.046));
    const int hop = win / 4;
    struct Frame { int64 pos; int pitch; float rms; };
    std::vector<Frame> frames;
    float loudest = 0.0f;
    for (int64 pos = 0; pos + win <= n && frames.size() < 2000000; pos += hop)
    {
        double e = 0.0;
        for (int i = 0; i < win; ++i) e += (double) audio[pos + i] * audio[pos + i];
        const float rms = (float) std::sqrt (e / win);
        loudest = jmax (loudest, rms);
        frames.push_back ({ pos, -1, rms });
    }
    // voiced only above the noise: relative to the loudest moment, so a quiet recording works too
    const float gate = jmax (0.003f, loudest * 0.12f);
    for (auto& f : frames)
        if (f.rms > gate)
            if (auto hz = yin (audio + f.pos, win, sr, 50.0f, 1600.0f, 0.15f))
                f.pitch = jlimit (0, 127, roundToInt (69.0 + 12.0 * std::log2 (*hz / 440.0)));
    // one stray frame (an octave jump, a breath) inside a note does not cut it
    for (size_t i = 1; i + 1 < frames.size(); ++i)
        if (frames[i - 1].pitch >= 0 && frames[i - 1].pitch == frames[i + 1].pitch && frames[i].pitch != frames[i - 1].pitch)
            frames[i].pitch = frames[i - 1].pitch;

    std::vector<DetectedNote> notes;
    const size_t minFrames = 3;
    size_t i = 0;
    while (i < frames.size())
    {
        const int p = frames[i].pitch;
        if (p < 0) { ++i; continue; }
        size_t j = i;
        float peak = 0.0f;
        while (j < frames.size() && frames[j].pitch == p)
        {
            // the same note played again: the level dips and comes back strongly
            if (j > i + 2 && frames[j].rms > 1.8f * jmin (frames[j - 1].rms, frames[j - 2].rms) && frames[j].rms > gate * 2.0f) break;
            peak = jmax (peak, frames[j].rms);
            ++j;
        }
        if (j - i >= minFrames)
        {
            const int64 end = j < frames.size() ? frames[j].pos : n;
            const int vel = jlimit (1, 127, roundToInt (40.0f + 87.0f * jmin (1.0f, peak / jmax (1.0e-6f, loudest))));
            notes.push_back ({ frames[i].pos, end, p, vel });
        }
        i = jmax (j, i + 1);
    }
    return notes;
}

// ===================================================================== key
String KeyGuess::name() const
{
    static const char* names[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    if (root < 0) return String::fromUTF8 ("sin tonalidad clara");
    return String (names[root % 12]) + (minor ? " menor" : " mayor");
}

KeyGuess keyFromChroma (const Chroma& c)
{
    // Krumhansl & Kessler (1982)
    static const double major[] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minor[] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    KeyGuess g;
    const double total = std::accumulate (c.begin(), c.end(), 0.0);
    if (total <= 1.0e-9) return g;
    auto corr = [&] (const double* prof, int root)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += c[(size_t) i]; my += prof[(i - root + 12) % 12]; }
        mx /= 12.0; my /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = c[(size_t) i] - mx, y = prof[(i - root + 12) % 12] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    double best = -2.0, second = -2.0;
    for (int m = 0; m < 2; ++m)
        for (int r = 0; r < 12; ++r)
        {
            const double v = corr (m == 0 ? major : minor, r);
            if (v > best) { second = best; best = v; g.root = r; g.minor = m == 1; }
            else if (v > second) second = v;
        }
    if (best < 0.3) { g.root = -1; return g; }
    g.confidence = (float) jlimit (0.0, 1.0, (best - second) * 4.0);
    return g;
}

Chroma chromaOfAudio (const AudioBuffer<float>& b, double sampleRate)
{
    Chroma c {};
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int order = sr >= 44100.0 ? 13 : 12;
    const int n = 1 << order, hop = n / 2;
    const auto mono = monoOf (b);
    if ((int) mono.size() < n) return c;
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    // a precomputed bin -> pitch class map, 55 Hz to 2 kHz
    std::vector<int> pcOf ((size_t) n / 2, -1);
    for (int k = 1; k < n / 2; ++k)
    {
        const double f = k * sr / n;
        if (f < 55.0 || f > 2000.0) continue;
        pcOf[(size_t) k] = ((int) std::round (69.0 + 12.0 * std::log2 (f / 440.0)) % 12 + 12) % 12;
    }
    for (size_t pos = 0; pos + (size_t) n <= mono.size(); pos += (size_t) hop)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = mono[pos + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / n));
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        Chroma frame {};
        double sum = 0.0;
        for (int k = 1; k < n / 2; ++k)
            if (pcOf[(size_t) k] >= 0) { frame[(size_t) pcOf[(size_t) k]] += buf[(size_t) k]; sum += buf[(size_t) k]; }
        if (sum <= 1.0e-6) continue;
        // each moment counts the same, loud or soft
        for (int i = 0; i < 12; ++i) c[(size_t) i] += frame[(size_t) i] / sum;
    }
    return c;
}

Chroma chromaOfNotes (const te::MidiClip& clip)
{
    Chroma c {};
    for (auto* n : const_cast<te::MidiClip&> (clip).getSequence().getNotes())
        if (! n->isMute()) c[(size_t) (n->getNoteNumber() % 12)] += n->getLengthBeats().inBeats() * (0.5 + n->getVelocity() / 254.0);
    return c;
}

// ===================================================================== loudness (BS.1770)
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// the K-weighting curve for any sample rate (the formulas libebur128 uses)
std::pair<Biquad, Biquad> kWeighting (double sr)
{
    Biquad shelf, hp;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
    return { shelf, hp };
}

double lufsOf (double power) { return power > 1.0e-12 ? -0.691 + 10.0 * std::log10 (power) : -70.0; }

// the highest level between the samples, at four times the rate (a 24-tap windowed sinc)
float truePeakOf (const AudioBuffer<float>& b, float samplePeak)
{
    constexpr int half = 12;
    float best = samplePeak;
    double table[3][2 * half];
    for (int p = 1; p <= 3; ++p)
        for (int k = -half + 1; k <= half; ++k)
        {
            const double d = (double) p / 4.0 - k;            // distance from sample n+k to the point n + p/4
            const double sinc = std::abs (d) < 1.0e-9 ? 1.0 : std::sin (pi * d) / (pi * d);
            const double w = 0.5 * (1.0 + std::cos (pi * d / half));
            table[p - 1][k + half - 1] = sinc * w;
        }
    const float floor = samplePeak * 0.5f;   // a peak between samples needs big samples around it
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const auto* x = b.getReadPointer (c);
        const int len = b.getNumSamples();
        for (int i = 0; i + 1 < len; ++i)
        {
            if (std::abs (x[i]) < floor && std::abs (x[i + 1]) < floor) continue;
            for (int p = 0; p < 3; ++p)
            {
                double y = 0.0;
                for (int k = -half + 1; k <= half; ++k)
                {
                    const int j = i + k;
                    if (j >= 0 && j < len) y += x[j] * table[p][k + half - 1];
                }
                best = jmax (best, (float) std::abs (y));
            }
        }
    }
    return best;
}
}

String Loudness::summary() const
{
    return String (integratedLufs, 1) + " LUFS integrados, pico real " + String (truePeakDb, 1) + " dBTP, pico " + String (peakDb, 1) + " dB";
}

Loudness measureLoudness (const AudioBuffer<float>& b, double sampleRate)
{
    Loudness L;
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int len = b.getNumSamples(), nch = jmin (2, b.getNumChannels());
    L.seconds = len / sr;
    if (len == 0 || nch == 0) return L;
    const float sp = peakOf (b);
    L.peakDb = gainToDb (sp);
    L.truePeakDb = gainToDb (truePeakOf (b, sp));

    // mean square of the K-weighted signal in 100 ms pieces
    const int seg = jmax (1, (int) std::round (sr * 0.1));
    const int nSeg = len / seg;
    std::vector<double> segPower ((size_t) jmax (1, nSeg), 0.0);
    for (int c = 0; c < nch; ++c)
    {
        auto [shelf, hp] = kWeighting (sr);
        const auto* x = b.getReadPointer (c);
        double acc = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const float y = hp.process (shelf.process (x[i]));
            acc += (double) y * y;
            if ((i + 1) % seg == 0 && (i + 1) / seg <= nSeg) { segPower[(size_t) ((i + 1) / seg - 1)] += acc / seg; acc = 0.0; }
        }
        if (nSeg == 0) segPower[0] += acc / len;
    }
    if (nSeg < 4)
    {
        double p = 0.0;
        for (auto v : segPower) p += v;
        L.integratedLufs = L.shortTermMaxLufs = (float) lufsOf (p / (double) segPower.size());
        return L;
    }
    // 400 ms blocks every 100 ms, gated at -70 LUFS and then 10 LU under their own mean
    std::vector<double> blocks;
    for (int i = 0; i + 4 <= nSeg; ++i) blocks.push_back ((segPower[(size_t) i] + segPower[(size_t) i + 1] + segPower[(size_t) i + 2] + segPower[(size_t) i + 3]) / 4.0);
    double sum = 0.0; int count = 0;
    for (auto p : blocks) if (lufsOf (p) > -70.0) { sum += p; ++count; }
    if (count > 0)
    {
        const double rel = lufsOf (sum / count) - 10.0;
        double s2 = 0.0; int c2 = 0;
        for (auto p : blocks) if (lufsOf (p) > -70.0 && lufsOf (p) > rel) { s2 += p; ++c2; }
        L.integratedLufs = (float) (c2 > 0 ? lufsOf (s2 / c2) : -70.0);
    }
    // short term: 3 s windows
    const int w = jmin (30, nSeg);
    double run = 0.0;
    for (int i = 0; i < nSeg; ++i)
    {
        run += segPower[(size_t) i];
        if (i >= w) run -= segPower[(size_t) (i - w)];
        if (i >= w - 1) L.shortTermMaxLufs = jmax (L.shortTermMaxLufs, (float) lufsOf (run / w));
    }
    return L;
}

// ===================================================================== files
bool readFile (te::Engine& engine, const File& f, AudioBuffer<float>& out, double& sr)
{
    std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const auto n = (int) jmin (r->lengthInSamples, (int64) (r->sampleRate * 60.0 * 30.0));
    out.setSize ((int) jmax (1u, r->numChannels), n);
    r->read (&out, 0, n, 0, true, true);
    sr = r->sampleRate;
    return true;
}

bool writeWav (const File& f, const AudioBuffer<float>& b, double sr, int bits)
{
    f.deleteFile();
    auto* os = new FileOutputStream (f);
    if (! os->openedOk()) { delete os; return false; }
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os, sr, (unsigned int) b.getNumChannels(), bits, {}, 0));
    if (w == nullptr) { delete os; return false; }
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// ===================================================================== clips
bool renderClip (Session& s, te::AudioClipBase& clip, AudioBuffer<float>& out, double& sr)
{
    auto& edit = s.getEdit();
    auto* track = clip.getTrack();
    if (track == nullptr) return false;
    auto all = te::getAllTracks (edit);
    BigInteger bits;
    bits.setBit (all.indexOf (track));
    auto tmp = File::createTempFile (".wav");
    te::Renderer::Parameters p (edit);
    p.destFile = tmp;
    p.audioFormat = s.engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 32;
    p.sampleRateForAudio = s.engine.getDeviceManager().getSampleRate() > 0 ? s.engine.getDeviceManager().getSampleRate() : 48000.0;
    p.time = clip.getEditTimeRange();
    p.tracksToDo = bits;
    p.allowedClips.add (&clip);
    p.usePlugins = false;
    p.useMasterPlugins = false;
    p.canRenderInMono = false;
    p.checkNodesForAudio = false;
    s.getTransport().stop (false, true);
    s.getTransport().freePlaybackContext();
    const auto f = te::Renderer::renderToFile ("Analizando", p);
    const bool ok = f.existsAsFile() && readFile (s.engine, f, out, sr);
    tmp.deleteFile();
    f.deleteFile();
    return ok;
}

namespace
{
// the part of the source file a plain clip plays (no warp, no speed change): the processes measure only that
Range<int64> usedRange (te::WaveAudioClip& c, double sr, int64 fileLen)
{
    if (c.getAutoTempo() || std::abs (c.getSpeedRatio() - 1.0) > 1.0e-6 || c.isLooping()) return { 0, fileLen };
    const auto pos = c.getPosition();
    const int64 s = (int64) (pos.getOffset().inSeconds() * sr), e = s + (int64) (pos.getLength().inSeconds() * sr);
    return Range<int64> (jlimit ((int64) 0, fileLen, s), jlimit ((int64) 0, fileLen, e));
}
}

Result processClip (Session& s, te::WaveAudioClip& clip, Process what)
{
    const auto src = clip.getOriginalFile();
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! readFile (s.engine, src, b, sr)) return Result::fail (String::fromUTF8 ("No se pudo leer el audio del clip."));
    String suffix;
    switch (what)
    {
        case Process::normalize:
        {
            const auto r = usedRange (clip, sr, b.getNumSamples());
            AudioBuffer<float> part (b.getArrayOfWritePointers(), b.getNumChannels(), (int) r.getStart(), (int) jmax ((int64) 1, r.getLength()));
            const float g = normalizeGain (part, -0.3f);
            if (peakOf (part) < 1.0e-6f) return Result::fail (String::fromUTF8 ("El clip está en silencio: no hay nada que normalizar."));
            b.applyGain (g);
            suffix = "normalizado";
            break;
        }
        case Process::invertPhase: invertPhase (b); suffix = "fase invertida"; break;
        case Process::removeDC:    removeDC (b);    suffix = "sin DC"; break;
        case Process::gainUp:      applyGainDb (b, 3.0f);  suffix = "+3 dB"; break;
        case Process::gainDown:    applyGainDb (b, -3.0f); suffix = "-3 dB"; break;
    }
    auto dest = s.audioFolder().getNonexistentChildFile (File::createLegalFileName (src.getFileNameWithoutExtension() + " " + suffix), ".wav", false);
    if (! writeWav (dest, b, sr)) return Result::fail ("No se pudo escribir " + dest.getFileName());
    s.getEdit().getUndoManager().beginNewTransaction();
    clip.getSourceFileReference().setToDirectFileReference (dest, false);
    return Result::ok();
}

Result stripSilence (Session& s, te::WaveAudioClip& clip, const StripOptions& o, int* pieces)
{
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! renderClip (s, clip, b, sr)) return Result::fail (String::fromUTF8 ("No se pudo leer el audio del clip."));
    const auto ranges = nonSilentRanges (b, o.thresholdDb, (int64) (o.minGapSeconds * sr), (int64) (o.padBefore * sr), (int64) (o.padAfter * sr));
    if (ranges.empty()) return Result::fail (String::fromUTF8 ("Todo el clip queda bajo el umbral: no se cortó nada."));
    const auto start = clip.getPosition().getStart();
    auto timeOf = [&] (int64 smp) { return start + te::TimeDuration::fromSeconds ((double) smp / sr); };
    if (ranges.size() == 1 && ranges[0].getStart() == 0 && ranges[0].getEnd() >= b.getNumSamples() - 1)
    {
        if (pieces != nullptr) *pieces = 1;
        return Result::fail (String::fromUTF8 ("No hay silencios más largos que ") + String (o.minGapSeconds, 2) + " s en el clip.");
    }
    s.getEdit().getUndoManager().beginNewTransaction();
    // split at every edge, from the end so the clip on the left stays the one we hold
    std::vector<te::TimePosition> cuts;
    for (auto& r : ranges) { cuts.push_back (timeOf (r.getStart())); cuts.push_back (timeOf (r.getEnd())); }
    std::sort (cuts.begin(), cuts.end());
    te::Clip* current = &clip;
    std::vector<te::Clip*> parts;
    for (auto t : cuts)
    {
        if (current == nullptr) break;
        const auto er = current->getEditTimeRange();
        if (t <= er.getStart() + te::TimeDuration::fromSeconds (0.001) || t >= er.getEnd() - te::TimeDuration::fromSeconds (0.001)) continue;
        auto* right = s.splitClip (*current, t);
        if (right == nullptr) break;
        parts.push_back (current);
        current = right;
    }
    if (current != nullptr) parts.push_back (current);
    int kept = 0;
    for (auto* c : parts)
    {
        const double mid = (c->getEditTimeRange().getCentre() - start).inSeconds() * sr;
        bool loud = false;
        for (auto& r : ranges) loud = loud || (mid >= (double) r.getStart() && mid < (double) r.getEnd());
        if (! loud) { s.deleteClip (*c); continue; }
        if (auto* ac = dynamic_cast<te::AudioClipBase*> (c))
        {
            // short fades so no piece starts or stops with a click
            if (ac->getFadeIn() < te::TimeDuration::fromSeconds (0.004)) ac->setFadeIn (te::TimeDuration::fromSeconds (0.004));
            if (ac->getFadeOut() < te::TimeDuration::fromSeconds (0.01)) ac->setFadeOut (te::TimeDuration::fromSeconds (0.01));
        }
        ++kept;
    }
    if (pieces != nullptr) *pieces = kept;
    return Result::ok();
}

te::MidiClip* clipToMidi (Session& s, te::AudioClipBase& clip, String* message)
{
    auto say = [message] (const String& m) { if (message != nullptr) *message = m; };
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! renderClip (s, clip, b, sr)) { say (String::fromUTF8 ("No se pudo leer el audio del clip.")); return nullptr; }
    const auto mono = monoOf (b);
    const auto found = audioToNotes (mono.data(), (int64) mono.size(), sr);
    if (found.empty()) { say (String::fromUTF8 ("No encontré una melodía en este clip (¿es una batería o un acorde?).")); return nullptr; }

    auto* src = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    te::AudioTrack* t = nullptr;
    for (auto& it : Catalog::builtInItems())
        if (it.builtInType == te::FourOscPlugin::xmlTypeName) { t = s.addInstrumentTrack (it); break; }
    if (t == nullptr) t = s.addAudioTrack();
    if (t == nullptr) { say ("No se pudo crear el canal."); return nullptr; }
    t->setName (clip.getName() + " (MIDI)");
    if (src != nullptr) s.getEdit().moveTrack (t, te::TrackInsertPoint (src->getParentFolderTrack(), src));

    auto& tempo = s.getEdit().tempoSequence;
    const auto start = clip.getPosition().getStart();
    const double b0 = tempo.toBeats (start).inBeats(), b1 = tempo.toBeats (clip.getPosition().getEnd()).inBeats();
    auto* mc = s.createMidiClip (*t, { te::BeatPosition::fromBeats (b0), te::BeatPosition::fromBeats (b1) }, clip.getName() + " (notas)");
    if (mc == nullptr) { say ("No se pudo crear el clip MIDI."); return nullptr; }
    auto& um = s.getEdit().getUndoManager();
    for (auto& n : found)
    {
        const double ns = tempo.toBeats (start + te::TimeDuration::fromSeconds ((double) n.start / sr)).inBeats() - b0;
        const double ne = tempo.toBeats (start + te::TimeDuration::fromSeconds ((double) n.end / sr)).inBeats() - b0;
        mc->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (jmax (0.0, ns)), te::BeatDuration::fromBeats (jmax (1.0 / 32.0, ne - ns)), n.velocity, 0, &um);
    }
    say (String ((int) found.size()) + String::fromUTF8 (" notas en el canal nuevo «") + t->getName() + String::fromUTF8 ("»."));
    return mc;
}

std::vector<Slice> sliceAtTransients (Session& s, te::AudioClipBase& clip, float sensitivity, int maxSlices, String* message)
{
    auto say = [message] (const String& m) { if (message != nullptr) *message = m; };
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! renderClip (s, clip, b, sr)) { say (String::fromUTF8 ("No se pudo leer el audio del clip.")); return {}; }
    auto hits = detectTransients (b, sr, sensitivity);
    if (hits.empty()) { say (String::fromUTF8 ("No encontré golpes en este clip.")); return {}; }
    // the first slice starts at the clip's start if there is sound before the first hit
    if (hits.front() > (int64) (sr * 0.03))
    {
        AudioBuffer<float> head (b.getArrayOfWritePointers(), b.getNumChannels(), 0, (int) hits.front());
        if (peakOf (head) > 0.05f) hits.insert (hits.begin(), 0);
    }
    auto& tempo = s.getEdit().tempoSequence;
    const auto start = clip.getPosition().getStart();
    std::vector<Slice> out;
    const auto dir = s.audioFolder().getChildFile (File::createLegalFileName (clip.getName()) + " golpes");
    dir.createDirectory();
    const int64 maxLen = (int64) (sr * 2.0);
    for (size_t i = 0; i < hits.size() && (int) out.size() < maxSlices; ++i)
    {
        const int64 a = hits[i];
        const int64 e = std::min ({ i + 1 < hits.size() ? hits[i + 1] : (int64) b.getNumSamples(), a + maxLen, (int64) b.getNumSamples() });
        if (e - a < (int64) (sr * 0.01)) continue;
        AudioBuffer<float> piece (b.getNumChannels(), (int) (e - a));
        for (int c = 0; c < b.getNumChannels(); ++c) piece.copyFrom (c, 0, b, c, (int) a, (int) (e - a));
        const int fade = jmin (piece.getNumSamples() / 4, (int) (sr * 0.012));
        piece.applyGainRamp (piece.getNumSamples() - fade, fade, 1.0f, 0.0f);
        Slice sl;
        sl.peak = peakOf (piece);
        sl.file = dir.getChildFile ("golpe " + String ((int) out.size() + 1).paddedLeft ('0', 2) + ".wav");
        if (! writeWav (sl.file, piece, sr)) continue;
        sl.startBeat = tempo.toBeats (start + te::TimeDuration::fromSeconds ((double) a / sr)).inBeats();
        sl.lengthBeats = tempo.toBeats (start + te::TimeDuration::fromSeconds ((double) e / sr)).inBeats() - sl.startBeat;
        out.push_back (sl);
    }
    if (hits.size() > (size_t) maxSlices)
        say (String ((int) hits.size()) + String::fromUTF8 (" golpes encontrados; Hamster tiene 16 pistas y van los primeros 16."));
    else say (String ((int) out.size()) + " golpes.");
    return out;
}

KeyGuess keyOfClip (Session& s, te::Clip& clip)
{
    if (auto* mc = dynamic_cast<te::MidiClip*> (&clip)) return keyFromChroma (chromaOfNotes (*mc));
    if (auto* ac = dynamic_cast<te::AudioClipBase*> (&clip))
    {
        AudioBuffer<float> b;
        double sr = 0.0;
        if (renderClip (s, *ac, b, sr)) return keyFromChroma (chromaOfAudio (b, sr));
    }
    return {};
}

// ===================================================================== slices to Hamster
File kitInbox()
{
    auto f = Session::maquetaInbox().getChildFile ("Kits");
    f.createDirectory();
    return f;
}

String roleOfHit (const File& f, te::Engine& engine)
{
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! readFile (engine, f, b, sr)) return "snare";
    const auto m = monoOf (b);
    // zero crossings per second over the loud part: a kick is slow, a hi-hat crosses all the time
    const int n = jmin ((int) m.size(), (int) (sr * 0.08));
    int crossings = 0;
    for (int i = 1; i < n; ++i) crossings += (m[(size_t) i - 1] < 0.0f) != (m[(size_t) i] < 0.0f) ? 1 : 0;
    const double zcr = crossings * sr / jmax (1, n);
    return zcr < 900.0 ? "kick" : (zcr > 4500.0 ? "hat" : "snare");
}

te::AudioTrack* slicesToHamster (Session& s, te::AudioClipBase& source, const std::vector<Slice>& slices, String* message)
{
    auto say = [message] (const String& m) { if (message != nullptr) *message = m; };
    if (slices.empty()) { say (String::fromUTF8 ("No hay golpes que mandar.")); return nullptr; }
    auto* t = s.addRacoonInstrument ("Racoon Hamster");
    auto* inst = t != nullptr ? Session::instrumentOf (*t) : nullptr;
    if (inst == nullptr || ! Catalog::isRacoonPlugin (*inst, "Racoon Hamster"))
    {
        if (t != nullptr) s.deleteTrack (*t);      // the stand-in synth would not play the slices
        say (String::fromUTF8 ("Hamster no está instalado: instala la familia Racoon para trocear a Hamster."));
        return nullptr;
    }
    auto name = File::createLegalFileName (source.getName()).upToFirstOccurrenceOf (".", false, false).trim();
    if (name.isEmpty()) name = "Loop";
    t->setName (name + " golpes");
    if (auto* src = source.getTrack()) s.getEdit().moveTrack (t, te::TrackInsertPoint (src->getParentFolderTrack(), src));

    // Hamster plays what the clip asks, note by note: its own pattern stays quiet and its notes are not remapped
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (inst))
        if (auto* pi = ext->getAudioPluginInstance())
            for (auto* prm : pi->getParameters())
            {
                const auto pn = prm->getName (64);
                if (pn == "Pattern follows the DAW" || pn == "Notas del kit") prm->setValueNotifyingHost (0.0f);
            }
    s.pushTrackNames();

    const int count = jmin (16, (int) slices.size());
    Array<var> files, names, roles;
    for (int i = 0; i < count; ++i)
    {
        files.add (slices[(size_t) i].file.getFullPathName());
        names.add ("Golpe " + String (i + 1));
        roles.add (roleOfHit (slices[(size_t) i].file, s.engine));
    }
    DynamicObject::Ptr o (new DynamicObject());
    o->setProperty ("animal", "Hamster");
    o->setProperty ("track", t->getName());
    o->setProperty ("files", files);
    o->setProperty ("names", names);
    o->setProperty ("roles", roles);
    o->setProperty ("firstNote", 36);
    const auto dir = kitInbox();
    const auto stamp = String (Time::currentTimeMillis()) + "-" + String::toHexString (Random::getSystemRandom().nextInt());
    auto tmp = dir.getChildFile (stamp + ".tmp");
    if (! tmp.replaceWithText (JSON::toString (var (o.get()))) || ! tmp.moveFileTo (dir.getChildFile (stamp + ".json")))
        say (String::fromUTF8 ("No se pudo dejar el kit para Hamster."));

    // the clip that plays them, in the loop's place
    auto& tempo = s.getEdit().tempoSequence;
    const double b0 = tempo.toBeats (source.getPosition().getStart()).inBeats(), b1 = tempo.toBeats (source.getPosition().getEnd()).inBeats();
    float loudest = 0.0f;
    for (int i = 0; i < count; ++i) loudest = jmax (loudest, slices[(size_t) i].peak);
    if (auto* mc = s.createMidiClip (*t, { te::BeatPosition::fromBeats (b0), te::BeatPosition::fromBeats (b1) }, name + " golpes"))
        for (int i = 0; i < count; ++i)
        {
            const auto& sl = slices[(size_t) i];
            const int vel = jlimit (1, 127, roundToInt (50.0f + 77.0f * sl.peak / jmax (1.0e-6f, loudest)));
            mc->getSequence().addNote (36 + i, te::BeatPosition::fromBeats (jmax (0.0, sl.startBeat - b0)),
                                       te::BeatDuration::fromBeats (jlimit (1.0 / 16.0, 1.0, sl.lengthBeats)), vel, 0, &s.getEdit().getUndoManager());
        }
    source.setMuted (true);
    say (String (count) + String::fromUTF8 (" golpes en el canal «") + t->getName() + String::fromUTF8 ("» de Hamster, uno por pista (notas 36 a ")
         + String (36 + count - 1) + String::fromUTF8 ("). El loop original quedó silenciado."));
    return t;
}

int splitAtTransients (Session& s, te::AudioClipBase& clip, float sensitivity, String* message)
{
    auto say = [message] (const String& m) { if (message != nullptr) *message = m; };
    AudioBuffer<float> b;
    double sr = 0.0;
    if (! renderClip (s, clip, b, sr)) { say (String::fromUTF8 ("No se pudo leer el audio del clip.")); return 0; }
    const auto hits = detectTransients (b, sr, sensitivity);
    if (hits.empty()) { say (String::fromUTF8 ("No encontré golpes en este clip.")); return 0; }
    const auto start = clip.getPosition().getStart();
    s.getEdit().getUndoManager().beginNewTransaction();
    te::Clip* current = &clip;
    int made = 1;
    for (auto h : hits)
    {
        const auto t = start + te::TimeDuration::fromSeconds ((double) h / sr);
        const auto er = current->getEditTimeRange();
        if (t <= er.getStart() + te::TimeDuration::fromSeconds (0.005) || t >= er.getEnd() - te::TimeDuration::fromSeconds (0.005)) continue;
        if (auto* right = s.splitClip (*current, t)) { current = right; ++made; }
    }
    say (String (made) + String::fromUTF8 (" clips, uno por golpe. Muévelos o bórralos para rehacer el ritmo."));
    return made;
}
}


#include "Assets.h"
#include "../Params.h"

namespace be
{
using namespace juce;

// ============================================================ YIN pitch detection
float detectPitchHz (const float* x, int n, double sr, float minHz, float maxHz)
{
    const int maxLag = std::min (n / 2, (int) (sr / minHz));
    const int minLag = std::max (2, (int) (sr / maxHz));
    if (maxLag <= minLag + 2) return 0.0f;
    const int W = std::min (n - maxLag, (int) (sr * 0.08));
    if (W < 32) return 0.0f;
    std::vector<float> d ((size_t) maxLag + 1, 0.0f);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < W; ++i) { const double df = (double) x[i] - (double) x[i + tau]; s += df * df; }
        d[(size_t) tau] = (float) s;
    }
    std::vector<float> c ((size_t) maxLag + 1, 1.0f);
    double run = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        run += d[(size_t) tau];
        c[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * (double) tau / run) : 1.0f;
    }
    int best = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
        if (c[(size_t) tau] < 0.15f) { while (tau + 1 < maxLag && c[(size_t) tau + 1] < c[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0)
    {
        float m = 1.0e9f;
        for (int tau = minLag; tau < maxLag; ++tau) if (c[(size_t) tau] < m) { m = c[(size_t) tau]; best = tau; }
        if (m > 0.6f) return 0.0f;
    }
    float tauF = (float) best;
    if (best > 1 && best < maxLag)
    {
        const float a = c[(size_t) best - 1], b = c[(size_t) best], cc = c[(size_t) best + 1];
        const float den = a - 2.0f * b + cc;
        if (std::abs (den) > 1.0e-9f) tauF += 0.5f * (a - cc) / den;
    }
    return (float) (sr / tauF);
}

// ============================================================ onset detection (high-frequency-weighted energy flux)
std::vector<int> detectTransients (const float* x, int n, double sr, int maxCount)
{
    std::vector<int> out;
    if (n < 512) { out.push_back (0); return out; }
    const int hop = 128, win = 512;
    const int frames = (n - win) / hop + 1;
    std::vector<float> e ((size_t) std::max (frames, 1));
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        const float* p = x + f * hop;
        for (int i = 1; i < win; ++i) { const float d = p[i] - 0.6f * p[i - 1]; s += (double) (d * d); }
        e[(size_t) f] = (float) std::sqrt (s / win);
    }
    const int minGap = (int) (0.045 * sr / hop);
    float avg = e.empty() ? 0.0f : e[0];
    float peak = 0.0f;
    for (auto v : e) peak = std::max (peak, v);
    int last = -minGap;
    for (int f = 1; f < frames; ++f)
    {
        const float flux = e[(size_t) f] - e[(size_t) f - 1];
        avg = 0.92f * avg + 0.08f * e[(size_t) f];
        if (flux > 0.12f * peak && e[(size_t) f] > 1.5f * avg * 0.8f && f - last >= minGap)
        {
            // refine to the sample of steepest rise
            int pos = f * hop;
            float best = 0.0f;
            for (int i = std::max (1, f * hop - hop); i < std::min (n - 1, f * hop + win); ++i)
            {
                const float d = std::abs (x[i]) - std::abs (x[i - 1]);
                if (d > best) { best = d; pos = i; }
            }
            out.push_back (std::max (0, pos - 32));
            last = f;
            if ((int) out.size() >= maxCount) break;
        }
    }
    if (out.empty() || out.front() > (int) (0.02 * sr)) out.insert (out.begin(), 0);
    return out;
}

SamplePtr makeSample (std::vector<float> l, std::vector<float> r, double sr, const String& name)
{
    auto s = std::make_shared<SampleData>();
    s->l = std::move (l);
    s->r = std::move (r);
    s->sampleRate = sr;
    s->name = name;
    const int n = s->size();
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = s->stereo() ? 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]) : s->l[(size_t) i];
    // pitch from the body (skip the attack)
    const int skip = std::min (n / 4, (int) (sr * 0.015));
    s->pitchHz = n - skip > (int) (sr * 0.05) ? detectPitchHz (mono.data() + skip, std::min (n - skip, (int) (sr * 0.4)), sr, 25.0f, 2000.0f) : 0.0f;
    s->transients = detectTransients (mono.data(), n, sr, 64);
    return s;
}

SamplePtr loadSampleFile (const File& f, double maxSeconds)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return {};
    const int len = (int) std::min<int64> (r->lengthInSamples, (int64) (r->sampleRate * maxSeconds));
    AudioBuffer<float> buf ((int) std::min (2u, std::max (1u, r->numChannels)), len);
    r->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
    std::vector<float> l ((size_t) len), rr;
    std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + len, l.begin());
    if (buf.getNumChannels() > 1)
    {
        rr.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
        bool same = true;
        for (int i = 0; i < len && same; i += 7) same = std::abs (l[(size_t) i] - rr[(size_t) i]) < 1.0e-5f;
        if (same) rr.clear();
    }
    // normalise peak to -1 dBFS for consistent layering
    float pk = 0.0f;
    for (auto v : l) pk = std::max (pk, std::abs (v));
    for (auto v : rr) pk = std::max (pk, std::abs (v));
    if (pk > 1.0e-4f)
    {
        const float g = 0.89f / pk;
        for (auto& v : l) v *= g;
        for (auto& v : rr) v *= g;
    }
    return makeSample (std::move (l), std::move (rr), r->sampleRate, f.getFileNameWithoutExtension());
}

// ============================================================ factory PCM synthesis
namespace
{
constexpr double kSr = 48000.0;

struct Gen
{
    Rng rng;
    std::vector<float> l, r;
    explicit Gen (double seconds, uint32_t seed, bool stereo = false)
    {
        rng.seed (seed);
        l.assign ((size_t) (seconds * kSr), 0.0f);
        if (stereo) r.assign (l.size(), 0.0f);
    }
    int n() const { return (int) l.size(); }
    static float t (int i) { return (float) i / (float) kSr; }
    static float ex (float tt, float tau) { return std::exp (-tt / std::max (tau, 1.0e-4f)); }

    // swept sine body: f(t) = f1 + (f0 - f1) * exp(-t/sweep)
    void body (float f0, float f1, float sweep, float tau, float amp, float drive = 0.0f, float tri = 0.0f)
    {
        double ph = 0.0;
        for (int i = 0; i < n(); ++i)
        {
            const float tt = t (i);
            const float f = f1 + (f0 - f1) * ex (tt, sweep);
            ph += f / kSr; ph -= std::floor (ph);
            float s = std::sin ((float) ph * kTwoPi);
            if (tri > 0.0f) { const float tw = 4.0f * std::abs ((float) ph - 0.5f) - 1.0f; s = lerpf (s, tw, tri); }
            if (drive > 0.0f) s = fastTanh (s * (1.0f + drive * 6.0f)) / fastTanh (1.0f + drive * 6.0f);
            add (i, s * amp * ex (tt, tau));
        }
    }
    void add (int i, float v, float pan = 0.0f)
    {
        if (r.empty()) { l[(size_t) i] += v; return; }
        l[(size_t) i] += v * (1.0f - std::max (0.0f, pan));
        r[(size_t) i] += v * (1.0f + std::min (0.0f, pan));
    }
    // filtered noise burst. mode 0 LP, 1 BP, 2 HP
    void noise (int mode, float hz, float q, float tau, float amp, float attack = 0.0f, float hzEnd = -1.0f, float sweep = 0.05f)
    {
        Svf f[2];
        for (int i = 0; i < n(); ++i)
        {
            const float tt = t (i);
            if ((i & 31) == 0)
            {
                const float h = hzEnd > 0.0f ? hzEnd + (hz - hzEnd) * ex (tt, sweep) : hz;
                f[0].set (h, q, (float) kSr); f[1].set (h, q, (float) kSr);
            }
            float env = ex (tt, tau);
            if (attack > 0.0f) env *= clamp01 (tt / attack);
            for (int c = 0; c < (r.empty() ? 1 : 2); ++c)
            {
                const float x = rng.bi();
                f[c].tick (x);
                const float y = mode == 0 ? f[c].lp : (mode == 1 ? f[c].bp : f[c].hp);
                auto& dst = c == 0 ? l : r;
                dst[(size_t) i] += y * env * amp;
            }
        }
    }
    // inharmonic square bank (metallic)
    void metal (const std::vector<float>& freqs, float hp, float bp, float tau, float amp, float bpQ = 1.2f)
    {
        std::vector<double> ph (freqs.size(), 0.0);
        for (size_t k = 0; k < ph.size(); ++k) ph[k] = rng.uni();
        Svf h[2], b[2];
        for (int c = 0; c < 2; ++c) { h[c].set (hp, 0.7f, (float) kSr); b[c].set (bp, bpQ, (float) kSr); }
        for (int i = 0; i < n(); ++i)
        {
            float s = 0.0f;
            for (size_t k = 0; k < freqs.size(); ++k)
            {
                ph[k] += freqs[k] / kSr; ph[k] -= std::floor (ph[k]);
                s += ph[k] < 0.5 ? 1.0f : -1.0f;
            }
            s /= (float) freqs.size();
            const float env = ex (t (i), tau);
            for (int c = 0; c < (r.empty() ? 1 : 2); ++c)
            {
                b[c].tick (s + (c == 1 ? 0.02f * rng.bi() : 0.0f));
                h[c].tick (b[c].bp);
                auto& dst = c == 0 ? l : r;
                dst[(size_t) i] += h[c].hp * env * amp;
            }
        }
    }
    void click (float amp, float hz, float tau = 0.0015f)
    {
        Svf f; f.set (hz, 0.8f, (float) kSr);
        for (int i = 0; i < std::min (n(), (int) (kSr * 0.02)); ++i)
        {
            f.tick (rng.bi());
            add (i, f.bp * amp * ex (t (i), tau));
        }
    }
    void clapBursts (int count, float spacing, float tail, float hz, float amp)
    {
        Svf f[2];
        f[0].set (hz, 1.6f, (float) kSr); f[1].set (hz * 1.05f, 1.6f, (float) kSr);
        for (int i = 0; i < n(); ++i)
        {
            const float tt = t (i);
            float env = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                const float t0 = (float) k * spacing;
                if (tt >= t0) env = std::max (env, ex (tt - t0, k == count - 1 ? tail : 0.006f));
            }
            for (int c = 0; c < (r.empty() ? 1 : 2); ++c)
            {
                f[c].tick (rng.bi());
                auto& dst = c == 0 ? l : r;
                dst[(size_t) i] += f[c].bp * env * amp;
            }
        }
    }
    void lowpass (float hz)
    {
        Svf f[2]; f[0].set (hz, 0.7f, (float) kSr); f[1].set (hz, 0.7f, (float) kSr);
        for (auto& v : l) { f[0].tick (v); v = f[0].lp; }
        for (auto& v : r) { f[1].tick (v); v = f[1].lp; }
    }
    void crush (int bits, double holdRate)
    {
        const float q = (float) (1 << (bits - 1));
        const int hold = std::max (1, (int) std::round (kSr / holdRate));
        auto run = [&] (std::vector<float>& v)
        {
            float h = 0.0f;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if ((int) i % hold == 0) h = std::round (v[i] * q) / q;
                v[i] = h;
            }
        };
        run (l); if (! r.empty()) run (r);
    }
    void saturate (float d)
    {
        const float g = 1.0f + d * 5.0f, norm = 1.0f / fastTanh (g);
        for (auto& v : l) v = fastTanh (v * g) * norm;
        for (auto& v : r) v = fastTanh (v * g) * norm;
    }
    void reverse() { std::reverse (l.begin(), l.end()); std::reverse (r.begin(), r.end()); }
    void fadeOut (float seconds)
    {
        const int fl = std::min (n(), (int) (seconds * kSr));
        for (int i = 0; i < fl; ++i)
        {
            const float g = (float) i / (float) fl;
            l[(size_t) (n() - 1 - i)] *= g;
            if (! r.empty()) r[(size_t) (n() - 1 - i)] *= g;
        }
    }
    SamplePtr finish (const String& name)
    {
        fadeOut (0.004f);
        float pk = 0.0f;
        for (auto v : l) pk = std::max (pk, std::abs (v));
        for (auto v : r) pk = std::max (pk, std::abs (v));
        if (pk > 1.0e-6f) { const float g = 0.89f / pk; for (auto& v : l) v *= g; for (auto& v : r) v *= g; }
        // trim trailing silence
        int end = n();
        while (end > 64 && std::abs (l[(size_t) end - 1]) < 1.0e-4f && (r.empty() || std::abs (r[(size_t) end - 1]) < 1.0e-4f)) --end;
        l.resize ((size_t) end); if (! r.empty()) r.resize ((size_t) end);
        return makeSample (std::move (l), std::move (r), kSr, name);
    }
};

const std::vector<float> k808Metal { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
} // namespace

FactoryPcm::FactoryPcm()
{
    const auto& names = factorySampleNames();
    for (int i = 0; i < names.size() - 1; ++i)
    {
        const auto nm = names[i].fromFirstOccurrenceOf ("PCM ", false, false);
        const uint32_t seed = 0x51ED0000u + (uint32_t) i * 7919u;
        SamplePtr s;
        switch (i)
        {
            case 0: { Gen g (0.7, seed); g.body (190, 52, 0.028f, 0.32f, 1.0f, 0.25f); g.click (0.5f, 3500); g.crush (12, 32000); s = g.finish (nm); break; }
            case 1: { Gen g (0.5, seed); g.body (260, 58, 0.018f, 0.22f, 1.0f, 0.6f, 0.15f); g.noise (1, 2400, 0.9f, 0.004f, 0.6f); g.crush (12, 26000); s = g.finish (nm); break; }
            case 2: { Gen g (1.2, seed); g.body (120, 44, 0.06f, 0.7f, 1.0f, 0.1f); g.body (88, 44, 0.04f, 0.9f, 0.3f); s = g.finish (nm); break; }
            case 3: { Gen g (0.4, seed); g.body (330, 185, 0.02f, 0.09f, 0.8f); g.body (510, 330, 0.02f, 0.05f, 0.35f); g.noise (2, 1800, 0.7f, 0.13f, 0.9f); g.crush (12, 30000); s = g.finish (nm); break; }
            case 4: { Gen g (0.45, seed); g.body (260, 210, 0.01f, 0.07f, 0.6f, 0.4f); g.noise (1, 5200, 0.5f, 0.17f, 1.1f); g.click (0.4f, 6000); s = g.finish (nm); break; }
            case 5: { Gen g (0.6, seed, true); g.clapBursts (4, 0.0105f, 0.16f, 1150, 1.0f); g.crush (12, 30000); s = g.finish (nm); break; }
            case 6: { Gen g (0.12, seed); g.body (1700, 1650, 0.01f, 0.012f, 0.7f, 0.3f, 0.6f); g.body (480, 470, 0.01f, 0.02f, 0.5f); g.click (0.6f, 4500, 0.001f); s = g.finish (nm); break; }
            case 7: { Gen g (0.12, seed); g.metal (k808Metal, 7000, 9000, 0.03f, 1.0f); g.noise (2, 8000, 0.7f, 0.02f, 0.3f); g.crush (12, 32000); s = g.finish (nm); break; }
            case 8: { Gen g (0.09, seed); g.noise (2, 9500, 0.8f, 0.018f, 1.0f); g.noise (1, 12000, 1.5f, 0.012f, 0.4f); s = g.finish (nm); break; }
            case 9: { Gen g (0.8, seed, true); g.metal (k808Metal, 6500, 8800, 0.32f, 1.0f); g.noise (2, 7500, 0.7f, 0.25f, 0.35f); g.crush (12, 32000); s = g.finish (nm); break; }
            case 10:
            {
                Gen g (2.0, seed, true);
                g.metal ({ 315.0f, 469.0f, 532.0f, 646.0f, 811.0f, 1047.0f }, 3000, 5500, 1.1f, 0.8f, 0.8f);
                g.body (2650, 2650, 1.0f, 0.9f, 0.08f); g.body (3720, 3720, 1.0f, 0.6f, 0.05f);
                g.noise (2, 6000, 0.7f, 0.4f, 0.12f);
                s = g.finish (nm); break;
            }
            case 11: { Gen g (2.6, seed, true); g.metal ({ 263.0f, 401.0f, 561.0f, 713.0f, 897.0f, 1189.0f }, 4000, 7000, 1.2f, 0.9f, 0.6f); g.noise (2, 5000, 0.6f, 0.9f, 0.7f); s = g.finish (nm); break; }
            case 12: { Gen g (0.8, seed); g.body (160, 92, 0.05f, 0.38f, 1.0f, 0.1f); g.noise (0, 900, 0.7f, 0.03f, 0.25f); s = g.finish (nm); break; }
            case 13: { Gen g (0.7, seed); g.body (220, 132, 0.05f, 0.32f, 1.0f, 0.1f); g.noise (0, 1200, 0.7f, 0.03f, 0.25f); s = g.finish (nm); break; }
            case 14: { Gen g (0.6, seed); g.body (300, 188, 0.04f, 0.26f, 1.0f, 0.1f); g.noise (0, 1500, 0.7f, 0.03f, 0.25f); s = g.finish (nm); break; }
            case 15: { Gen g (0.5, seed); g.body (390, 330, 0.012f, 0.16f, 1.0f); g.body (690, 660, 0.01f, 0.05f, 0.25f); g.click (0.4f, 2500, 0.002f); s = g.finish (nm); break; }
            case 16: { Gen g (0.35, seed); g.body (560, 470, 0.01f, 0.09f, 1.0f); g.body (1010, 960, 0.01f, 0.03f, 0.2f); g.click (0.4f, 3500, 0.0015f); s = g.finish (nm); break; }
            case 17:
            {
                Gen g (0.6, seed);
                g.metal ({ 540.0f, 800.0f }, 400, 1700, 0.11f, 1.0f, 0.7f);
                g.metal ({ 540.0f, 800.0f }, 400, 1500, 0.4f, 0.4f, 0.7f);
                s = g.finish (nm); break;
            }
            case 18: { Gen g (0.25, seed, true); g.noise (2, 6500, 0.9f, 0.05f, 1.0f, 0.03f); s = g.finish (nm); break; }
            case 19:
            {
                Gen g (0.4, seed, true);
                g.metal ({ 3100.0f, 4200.0f, 5300.0f, 6900.0f }, 5000, 8000, 0.12f, 0.6f, 1.5f);
                g.noise (2, 7000, 0.8f, 0.09f, 0.8f, 0.004f);
                s = g.finish (nm); break;
            }
            case 20: { Gen g (0.12, seed); g.body (2500, 2480, 0.01f, 0.025f, 1.0f); g.body (5100, 5100, 0.01f, 0.01f, 0.2f); s = g.finish (nm); break; }
            case 21:
            {
                Gen g (1.2, seed, true);
                g.metal ({ 187.0f, 311.0f, 433.0f, 709.0f, 1031.0f, 1419.0f }, 250, 1200, 0.45f, 1.0f, 0.5f);
                g.click (0.6f, 3000);
                g.saturate (0.4f);
                s = g.finish (nm); break;
            }
            case 22: { Gen g (0.3, seed); g.body (4200, 90, 0.025f, 0.15f, 1.0f, 0.3f); s = g.finish (nm); break; }
            case 23:
            {
                Gen g (0.3, seed);
                for (int k = 0; k < 6; ++k)
                {
                    const int st = (int) (k * 0.035 * kSr);
                    const float f = 400.0f * std::exp2 ((float) ((k * 5) % 7) * 0.7f);
                    double ph = 0.0;
                    for (int j = st; j < std::min (g.n(), st + (int) (0.02 * kSr)); ++j) { ph += f / kSr; g.l[(size_t) j] += (std::fmod (ph, 1.0) < 0.5 ? 0.6f : -0.6f) * Gen::ex (Gen::t (j - st), 0.01f); }
                }
                g.crush (6, 11025);
                s = g.finish (nm); break;
            }
            case 24: { Gen g (0.7, seed); g.body (140, 55, 0.03f, 0.25f, 0.9f, 0.2f); g.noise (1, 1400, 0.7f, 0.12f, 0.5f); g.lowpass (3200); g.crush (10, 22050); s = g.finish (nm); break; }
            case 25: { Gen g (0.25, seed); g.body (300, 1600, 0.04f, 0.08f, 1.0f); s = g.finish (nm); break; }
            case 26: { Gen g (0.15, seed); g.body (1250, 1200, 0.01f, 0.03f, 1.0f, 0.2f); g.body (2950, 2900, 0.01f, 0.012f, 0.3f); g.click (0.3f, 4000, 0.001f); s = g.finish (nm); break; }
            case 27: { Gen g (0.25, seed); g.clapBursts (2, 0.006f, 0.035f, 2400, 1.0f); g.click (0.7f, 5000, 0.0008f); s = g.finish (nm); break; }
            case 28: { Gen g (0.35, seed, true); g.noise (1, 3000, 0.4f, 0.09f, 1.0f, 0.0f, 600.0f, 0.1f); s = g.finish (nm); break; }
            case 29: { Gen g (0.8, seed); g.body (150, 48, 0.035f, 0.4f, 1.0f, 0.8f); g.lowpass (2500); g.saturate (0.5f); s = g.finish (nm); break; }
            case 30: { Gen g (0.35, seed); g.body (240, 190, 0.015f, 0.08f, 0.8f); g.noise (1, 3400, 0.6f, 0.11f, 1.0f); g.lowpass (6000); g.crush (8, 16000); s = g.finish (nm); break; }
            case 31: { Gen g (1.6, seed, true); g.metal ({ 263.0f, 401.0f, 561.0f, 713.0f, 897.0f, 1189.0f }, 4000, 7000, 0.7f, 0.9f, 0.6f); g.noise (2, 5000, 0.6f, 0.5f, 0.7f); g.reverse(); s = g.finish (nm); break; }
            default: { Gen g (0.1, seed); g.click (1.0f, 2000); s = g.finish (nm); break; }
        }
        samples.push_back (s);
    }
}

const FactoryPcm& FactoryPcm::get()
{
    static FactoryPcm f;
    return f;
}

const SampleData* FactoryPcm::sample (int index) const
{
    if (index < 0 || index >= (int) samples.size()) return nullptr;
    return samples[(size_t) index].get();
}

int FactoryPcm::defaultForRole (int role)
{
    static const int m[] { 0, 3, 5, 7, 9, 10, 11, 6, 12, 13, 14, 18, 15, 16, 17, 21, 22 };
    return m[jlimit (0, (int) Role::Count - 1, role)];
}

// ============================================================ impulse responses
AudioBuffer<float> makeImpulseResponse (int type, float size, float tone, double sr)
{
    const float lenSec = type == 0 ? 0.25f + size * 1.1f : (type == 1 ? 0.8f + size * 3.0f : 1.4f + size * 4.6f);
    const int n = (int) (lenSec * sr);
    AudioBuffer<float> b (2, n);
    b.clear();
    Rng rng; rng.seed (0xC0FFEEu + (uint32_t) type);
    const float rt = lenSec / 6.9f;     // time constant giving -60 dB at the end
    const float pre = type == 2 ? 0.018f + size * 0.03f : (type == 0 ? 0.004f : 0.0f);
    const int preS = (int) (pre * sr);
    for (int c = 0; c < 2; ++c)
    {
        auto* d = b.getWritePointer (c);
        Svf damp; float lastHz = -1.0f;
        for (int i = preS; i < n; ++i)
        {
            const float t = (float) (i - preS) / (float) sr;
            // brightness falls over time; tone shifts the whole curve
            const float hz = (2500.0f + tone * 12000.0f) * std::exp (-t * (type == 1 ? 0.6f : 1.6f)) + 400.0f;
            if ((i & 63) == 0 && std::abs (hz - lastHz) > 1.0f) { damp.set (hz, 0.6f, (float) sr); lastHz = hz; }
            float x = rng.bi();
            // sparse early reflections for rooms
            if (type == 0 && t < 0.06f) x *= (rng.uni() > 0.93f ? 3.0f : 0.25f);
            damp.tick (x);
            const float env = std::exp (-t / rt) * (type == 1 ? 1.0f : clamp01 (t / 0.004f + 0.2f));
            d[i] = damp.lp * env;
        }
    }
    // normalise energy
    double e = 0.0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) e += (double) b.getSample (c, i) * b.getSample (c, i);
    if (e > 0.0) b.applyGain ((float) (0.7 / std::sqrt (e / 2.0)));
    return b;
}
} // namespace be

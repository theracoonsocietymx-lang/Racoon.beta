#include "BeaverAnalysis.h"

// Beaver's built-in materials: short dark sounds made by code and analysed like any file, plus two pictures.
// Nothing ships as a file; each one is built the first time it is needed and kept for the whole process.
namespace racoon::beaver
{
using namespace juce;

namespace
{
constexpr double sr = 48000.0;
inline double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

// a two-pole resonator (constant peak gain), for formants
struct Reso
{
    double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
    void set (double hz, double bw) { const double r = std::exp (-MathConstants<double>::pi * bw / sr); a1 = -2.0 * r * std::cos (MathConstants<double>::twoPi * hz / sr); a2 = r * r; b0 = 1.0 - r; }
    double process (double x) { const double y = b0 * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
};

// a sung vowel: a Rosenberg pulse train through five formants, moving from "o" to "u", with breath
void voice (float* out, int n, double note, double vibHz, double vibCents, double vibPhase, double detuneCents, double vowelDrift, uint32 seed, float gain)
{
    static const double o[5] { 570, 840, 2410, 3300, 3750 }, u[5] { 300, 870, 2240, 3300, 3750 }, bw[5] { 80, 90, 120, 150, 200 }, g[5] { 1.0, 0.6, 0.25, 0.1, 0.05 };
    Reso rs[5];
    Random r ((int64) seed);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double f = midiHz (note) * std::pow (2.0, (detuneCents + vibCents * std::sin (MathConstants<double>::twoPi * vibHz * t + vibPhase)) / 1200.0);
        ph += f / sr;
        if (ph >= 1.0) ph -= 1.0;
        // Rosenberg glottal pulse: opening 40 %, closing 16 %, then closed
        double p;
        if (ph < 0.4) p = 0.5 * (1.0 - std::cos (MathConstants<double>::pi * ph / 0.4));
        else if (ph < 0.56) p = std::cos (MathConstants<double>::halfPi * (ph - 0.4) / 0.16);
        else p = 0.0;
        if ((i & 63) == 0)
        {
            const double m = jlimit (0.0, 1.0, t / (n / sr) + vowelDrift * std::sin (t * 0.7 + seed));
            for (int k = 0; k < 5; ++k) rs[k].set ((o[k] + (u[k] - o[k]) * m) * 0.85, bw[k]);
        }
        const double src = p - 0.45 + 0.03 * (r.nextDouble() * 2.0 - 1.0);
        double y = 0.0;
        for (int k = 0; k < 5; ++k) y += g[k] * rs[k].process (src);
        const double env = jmin (1.0, t / 0.08) * jmin (1.0, (n / sr - t) / 0.15);
        out[i] += (float) (y * env * gain);
    }
}
}

AudioBuffer<float> renderMaterialSource (int index)
{
    const int n = (int) (sr * 3.0);
    AudioBuffer<float> b (1, n);
    b.clear();
    float* x = b.getWritePointer (0);
    Random rng (1234 + index);
    switch (index)
    {
        case 1: voice (x, n, 45.0, 5.2, 25.0, 0.0, 0.0, 0.0, 1, 1.0f); break;
        case 2:
            for (int k = 0; k < 5; ++k) voice (x, n, 45.0, 4.8 + 0.3 * k, 20.0, k * 1.3, -9.0 + 4.5 * k, 0.15, (uint32) (k + 3), 0.35f);
            break;
        case 3:
        {
            // a tubular bell at La 1: twelve inharmonic partials, the high ones dying first
            static const double ratio[12] { 1.0, 2.76, 5.40, 8.93, 13.34, 18.64, 3.0, 4.2, 6.6, 10.1, 15.6, 24.0 };
            for (int k = 0; k < 12; ++k)
            {
                const double f = 55.0 * ratio[k] * 1.64, a = 0.3 / (1.0 + k * 0.4), tau = 2.5 / (1.0 + ratio[k] * 0.4);
                if (f > 0.45 * sr) continue;
                for (int i = 0; i < n; ++i) x[i] += (float) (a * std::exp (-i / sr / tau) * std::sin (MathConstants<double>::twoPi * f * i / sr));
            }
            break;
        }
        case 4:
        {
            // a bowed saw at Mi 1 with a slow crescendo, bow noise and a late vibrato
            double ph = 0.0;
            Reso bow; bow.set (900.0, 1400.0);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                const double vib = t > 1.0 ? 15.0 * std::sin (MathConstants<double>::twoPi * 4.5 * t) * jmin (1.0, t - 1.0) : 0.0;
                const double f = midiHz (28.0) * std::pow (2.0, vib / 1200.0);
                ph += f / sr; ph -= std::floor (ph);
                double s = 0.0;
                for (int k = 1; k * f < 12000.0; ++k) s += std::sin (MathConstants<double>::twoPi * k * ph) / k;
                const double env = jmin (1.0, 0.35 + t / 2.0) * jmin (1.0, t / 0.05) * jmin (1.0, (3.0 - t) / 0.2);
                x[i] = (float) (env * (0.35 * s + 0.04 * bow.process (rng.nextDouble() * 2.0 - 1.0)));
            }
            break;
        }
        case 5:
        {
            // tunnel wind: brown noise through a slowly moving band, a faint whistle at La 1
            double brown = 0.0;
            Reso band;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                if ((i & 63) == 0) band.set (120.0 * std::pow (5.0, 0.5 + 0.5 * std::sin (t * 1.3)), 150.0);
                brown = 0.995 * brown + 0.05 * (rng.nextDouble() * 2.0 - 1.0);
                const double env = jmin (1.0, t / 0.5) * jmin (1.0, (3.0 - t) / 0.3);
                x[i] = (float) (env * (2.0 * band.process (brown) + 0.03 * std::sin (MathConstants<double>::twoPi * 55.0 * t)));
            }
            break;
        }
        case 6:
        {
            // a basalt organ: drawbars 1, 2, 3, 4, 6, 8 at La 1 coming in one by one, a key click at the start
            static const int bars[6] { 1, 2, 3, 4, 6, 8 };
            for (int k = 0; k < 6; ++k)
            {
                const double start = k * 0.45;
                for (int i = (int) (start * sr); i < n; ++i)
                {
                    const double t = i / sr - start;
                    x[i] += (float) (0.22 / std::sqrt ((double) bars[k]) * jmin (1.0, t / 0.05) * std::sin (MathConstants<double>::twoPi * 55.0 * bars[k] * i / sr));
                }
            }
            for (int i = 0; i < 240; ++i) x[i] += (float) (0.2 * (rng.nextDouble() * 2.0 - 1.0) * (1.0 - i / 240.0));
            break;
        }
        default: break;
    }
    const float pk = b.getMagnitude (0, 0, n);
    if (pk > 1.0e-6f) b.applyGain (0.891f / pk);
    return b;
}

Image makeBuiltInImage (int which)
{
    Image img (Image::RGB, 512, numPartials, true);
    Image::BitmapData bd (img, Image::BitmapData::writeOnly);
    auto setRow = [&] (int x, int partial, float l)
    {
        const int y = numPartials - partial;             // partial 1 on the bottom row
        if (y < 0 || y >= numPartials) return;
        const auto c = bd.getPixelColour (x, y);
        const float v = jmax (c.getFloatRed(), jlimit (0.0f, 1.0f, l));
        bd.setPixelColour (x, y, Colour::fromFloatRGBA (v, v, v, 1.0f));
    };
    for (int x = 0; x < 512; ++x)
    {
        const float t = (float) x / 511.0f;
        if (which == 0)
        {
            // three soft bands that swell from left to right
            for (int k = 1; k <= 3; ++k) setRow (x, k, 0.55f + 0.45f * t);
            for (int k = 6; k <= 9; ++k) setRow (x, k, (0.25f + 0.55f * t) * (1.0f - 0.08f * (float) (k - 6)));
            for (int k = 14; k <= 20; ++k) setRow (x, k, (0.1f + 0.45f * t * t) * (1.0f - 0.06f * (float) (k - 14)));
        }
        else
        {
            // a staircase of harmonics up and back
            static const int steps[8] { 2, 3, 4, 5, 6, 8, 5, 3 };
            setRow (x, 1, 0.7f);
            setRow (x, steps[jmin (7, x / 64)], 1.0f);
        }
    }
    return img;
}

std::shared_ptr<const FrameSet> builtInMaterial (int index)
{
    static CriticalSection lock;
    static std::shared_ptr<const FrameSet> cache[9];
    index = jlimit (1, 8, index);
    {
        const ScopedLock sl (lock);
        if (cache[index] != nullptr) return cache[index];
    }
    static const char* names[9] { "", "Voz de garganta", "Coro de ceniza", "Campana hundida", "Arco de piedra", "Viento de túnel", "Órgano de basalto",
                                  "Imagen: horizonte", "Imagen: escalera" };
    std::shared_ptr<FrameSet> fs;
    if (index >= 7) fs = fromImage (makeBuiltInImage (index - 7), false, 60.0f, 8.0f, String::fromUTF8 (names[index]));
    else
    {
        AnalysisOptions opt;
        opt.asSpectrum = index == 3 || index == 5;
        opt.refNote = 33;
        fs = analyse (renderMaterialSource (index), opt, String::fromUTF8 (names[index]));
    }
    fs->builtIn = true;
    const ScopedLock sl (lock);
    if (cache[index] == nullptr) cache[index] = fs;
    return cache[index];
}
}

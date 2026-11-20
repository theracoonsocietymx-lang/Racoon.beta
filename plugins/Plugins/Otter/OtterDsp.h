#pragma once
#include <JuceHeader.h>
#include <complex>
#include <cmath>

// Otter's sound parts: the string (a two-segment waveguide), the bar (a bank of modes), the exciters' friction curve,
// the body resonators and the sympathetic strings. No parameters here: the processor hands every value in.
namespace racoon::otter
{
constexpr int numModes = 20, numTables = 8, numBodies = 8, numSymp = 6, dispSections = 4;

// ------------------------------------------------------------------ a ring buffer read at a whole or fractional delay
struct Ring
{
    std::vector<float> buf;
    int write = 0, mask = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; write = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) write] = x; write = (write + 1) & mask; }
    // delay 1 = the sample pushed last
    inline float at (int d) const { return buf[(size_t) ((write - d) & mask)]; }
    // third-order Lagrange around d (d >= 2): flat phase, smooth under modulation
    inline float frac (float d) const
    {
        d = juce::jlimit (2.0f, (float) (mask - 4), d);
        const int i = (int) d - 1;           // taps i, i+1, i+2, i+3; the point sits between i+1 and i+2
        const float t = d - (float) i;       // in [1, 2)
        const float x0 = at (i), x1 = at (i + 1), x2 = at (i + 2), x3 = at (i + 3);
        const float tm1 = t - 1.0f, tm2 = t - 2.0f, tm3 = t - 3.0f;
        return -x0 * tm1 * tm2 * tm3 / 6.0f + x1 * t * tm2 * tm3 * 0.5f - x2 * t * tm1 * tm3 * 0.5f + x3 * t * tm1 * tm2 / 6.0f;
    }
};

// ------------------------------------------------------------------ the loop's filters
// loss: g (1 - a) / (1 - a z^-1). The magnitude at w0 and at the high reference set g and a.
struct Loss
{
    float g = 0.999f, a = 0.0f, y = 0.0f;
    inline float process (float x) { y = g * (1.0f - a) * x + a * y; return y; }
    static double mag (double a, double w) { return (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w) + a * a); }
    static double phaseDelay (double a, double w) { return std::atan2 (a * std::sin (w), 1.0 - a * std::cos (w)) / w; }
    // T60 of the fundamental (t0 s) and of the high reference (t1 s) at a loop rate of f0 round trips per second
    static void design (double f0, double w0, double w1, double t0, double t1, float& gOut, float& aOut)
    {
        const double h0 = std::pow (10.0, -3.0 / (std::max (0.02, t0) * f0));
        const double h1 = std::pow (10.0, -3.0 / (std::max (0.01, std::min (t0, t1)) * f0));
        const double want = h1 / h0;          // ratio of the loop gains, high over low (<= 1)
        double lo = 0.0, hi = 0.95;
        if (mag (0.0, w1) / mag (0.0, w0) <= want + 1.0e-9) hi = 0.0;
        for (int k = 0; k < 24 && hi > 0.0; ++k)
        {
            const double m = 0.5 * (lo + hi);
            if (mag (m, w1) / mag (m, w0) > want) lo = m; else hi = m;
        }
        const double a = 0.5 * (lo + hi);
        aOut = (float) a;
        gOut = (float) std::min (0.99999, h0 / mag (a, w0));
    }
};

// stiffness: first-order allpass (c + z^-1) / (1 + c z^-1)
struct Allpass
{
    float c = 0.0f, x1 = 0.0f, y1 = 0.0f;
    inline float process (float x) { const float y = c * x + x1 - c * y1; x1 = x; y1 = y; return y; }
    static double phaseDelay (double c, double w)
    {
        const std::complex<double> z1 = std::polar (1.0, -w);
        const auto h = (c + z1) / (1.0 + c * z1);
        double ph = -std::arg (h);
        if (ph < 0.0) ph += juce::MathConstants<double>::twoPi;
        return ph / w;
    }
};

// ------------------------------------------------------------------ the string: nut | bridge side | excitation point | nut side
// bridge segment length Lb (whole samples), the reflection filters at the bridge, the nut segment (fractional).
struct StringModel
{
    Ring bridge, neck;
    Loss loss;
    Allpass disp[dispSections];
    float Lb = 10.0f, Ln = 10.0f;     // delays (Ln fractional)
    float bridgeOut = 0.0f;           // the wave arriving at the bridge (it drives the bar and the screen)
    float lastVel = 0.0f;             // the string's velocity at the excitation point (what the bow feels)
    void prepare (int maxDelay) { bridge.prepare (maxDelay); neck.prepare (maxDelay); }
    void clear()
    {
        bridge.clear(); neck.clear(); loss.y = 0.0f;
        for (auto& a : disp) { a.x1 = a.y1 = 0.0f; }
        bridgeOut = lastVel = bridgeRefl = nutRefl = 0.0f;
        tuned = false;
    }
    // sets the delays for a loop period P (samples) with the excitation at beta (0 = at the bridge)
    void tune (double P, double beta, double w0)
    {
        const double pdF = Loss::phaseDelay (loss.a, w0) + dispSections * Allpass::phaseDelay (disp[0].c, w0);
        const double total = std::max (8.0, P);
        double lb = std::round (beta * total - pdF);
        lb = juce::jlimit (1.0, (double) bridge.mask - 4.0, lb);
        // the bridge side keeps its whole length while the note bends or vibrates (the nut side takes the change, smoothly);
        // it moves only when the excitation point really moves
        if (tuned && std::abs (lb - Lb) < 0.03 * total) lb = Lb;
        tuned = true;
        double ln = total - lb - pdF;
        if (ln < 2.0) { lb = std::max (1.0, lb - (2.0 - ln)); ln = total - lb - pdF; }
        Lb = (float) lb;
        LnTarget = (float) juce::jlimit (2.0, (double) neck.mask - 5.0, ln);
    }
    bool tuned = false;
    float LnTarget = 10.0f;
    // over the next n samples the nut side glides to its new length
    inline void glide (int n, float& step) { step = (LnTarget - Ln) / (float) juce::jmax (1, n); }
    // one sample in two halves: pre() reflects at both ends and returns the string's velocity at the excitation point
    // (what a bow feels); post() adds the exciter's force there
    float bridgeRefl = 0.0f, nutRefl = 0.0f;
    inline float pre()
    {
        float b = bridge.at ((int) Lb);
        bridgeOut = b;
        b = loss.process (b);
        for (auto& a : disp) b = a.process (b);
        bridgeRefl = -b;
        nutRefl = -neck.frac (Ln);
        lastVel = bridgeRefl + nutRefl;
        return lastVel;
    }
    inline void post (float force)
    {
        neck.push (bridgeRefl + force);
        bridge.push (nutRefl + force);
    }
};

// a simple second string (one loop, the same loss)
struct KS
{
    Ring line; Loss loss; float P = 100.0f;
    void prepare (int maxDelay) { line.prepare (maxDelay); }
    void clear() { line.clear(); loss.y = 0.0f; }
    inline float tick (float in)
    {
        const float y = loss.process (line.frac (P));
        line.push (y + in);
        return y;
    }
};

// ------------------------------------------------------------------ the bar's mode tables
struct ModeTable { const char* name; float ratios[numModes]; int weights; };   // weights: 0 beam, 1 clamped-free, 2 flat
const ModeTable& modeTable (int i);
// |phi_k (x)| for the free-free beam (shape 0) or the clamped-free reed (shape 1), x in 0..1, normalised to 1 at the end
float modeShape (int shape, int k, float x);

struct Modes
{
    float re[numModes] {}, im[numModes] {};
    void clear() { std::fill (re, re + numModes, 0.0f); std::fill (im, im + numModes, 0.0f); }
};

// ------------------------------------------------------------------ a biquad (band pass, low pass, shelves)
struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    inline float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void bandpass (double sr, double hz, double q);       // peak gain 1
    void lowpass (double sr, double hz, double q);
    void highShelf (double sr, double hz, double db);
};

struct Body { const char* name; float hz[numBodies], q[numBodies], db[numBodies]; int count; };
const Body& body (int i);
}

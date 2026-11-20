#include "OtterDsp.h"

namespace racoon::otter
{
using namespace juce;

// ===================================================================== mode tables
namespace
{
struct Tables
{
    ModeTable t[numTables];
    Tables()
    {
        auto fill = [] (ModeTable& m, const char* name, std::initializer_list<float> first, int weights, float growth)
        {
            m.name = name; m.weights = weights;
            int k = 0;
            for (float r : first) m.ratios[k++] = r;
            // past the listed ratios: the last gap keeps growing by `growth` (each table's own spread)
            float gap = m.ratios[k - 1] - m.ratios[k - 2];
            for (; k < numModes; ++k) { gap *= growth; m.ratios[k] = m.ratios[k - 1] + gap; }
        };
        // free-free beam: (b_k / b_1)^2 with b_1..4 from the beam equation, then (2k + 1) pi / 2
        {
            auto& m = t[0];
            m.name = "Barra libre"; m.weights = 0;
            const double b1 = 4.7300;
            const double b[4] { 4.7300, 7.8532, 10.9956, 14.1372 };
            for (int k = 0; k < numModes; ++k)
            {
                const double bk = k < 4 ? b[k] : (2.0 * (k + 1) + 1.0) * MathConstants<double>::pi * 0.5;
                m.ratios[k] = (float) ((bk / b1) * (bk / b1));
            }
        }
        fill (t[1], "Marimba", { 1.0f, 3.99f, 9.14f, 15.8f, 23.6f, 31.5f }, 0, 1.0f);
        fill (t[2], "Vibráfono", { 1.0f, 4.0f, 10.0f, 17.6f, 27.2f }, 0, 1.12f);
        fill (t[3], "Xilófono", { 1.0f, 3.0f, 6.0f, 9.9f, 14.6f }, 0, 1.1f);
        // a reed fixed at one end
        {
            auto& m = t[4];
            m.name = "Lengüeta"; m.weights = 1;
            const double b[4] { 1.8751, 4.6941, 7.8548, 10.9955 };
            for (int k = 0; k < numModes; ++k)
            {
                const double bk = k < 4 ? b[k] : (2.0 * (k + 1) - 1.0) * MathConstants<double>::pi * 0.5;
                m.ratios[k] = (float) ((bk / b[0]) * (bk / b[0]));
            }
        }
        fill (t[5], "Placa", { 1.0f, 1.47f, 2.09f, 2.56f, 2.96f, 3.61f, 4.17f, 4.75f }, 2, 1.04f);
        fill (t[6], "Campana tubular", { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f }, 0, 1.1f);
        // a bowl: ten modes, each split in a pair 0.4 % apart (the slow beating)
        {
            auto& m = t[7];
            m.name = "Cuenco"; m.weights = 2;
            const float base[10] { 1.0f, 2.71f, 5.15f, 8.43f, 12.53f, 17.4f, 23.0f, 29.5f, 36.7f, 44.6f };
            for (int k = 0; k < 10; ++k) { m.ratios[2 * k] = base[k]; m.ratios[2 * k + 1] = base[k] * 1.004f; }
        }
    }
};
}

const ModeTable& modeTable (int i)
{
    static const Tables tables;
    return tables.t[jlimit (0, numTables - 1, i)];
}

float modeShape (int shape, int k, float x)
{
    // beam roots: free-free 4.730, 7.853, ...; clamped-free 1.875, 4.694, ...
    static const double ff[4] { 4.7300, 7.8532, 10.9956, 14.1372 }, cf[4] { 1.8751, 4.6941, 7.8548, 10.9955 };
    const double pi = MathConstants<double>::pi;
    const double b = shape == 0 ? (k < 4 ? ff[k] : (2.0 * (k + 1) + 1.0) * pi * 0.5)
                                : (k < 4 ? cf[k] : (2.0 * (k + 1) - 1.0) * pi * 0.5);
    const double bx = b * jlimit (0.0, 1.0, (double) x);
    double phi;
    if (b < 30.0)
    {
        if (shape == 0)
        {
            const double s = (std::cosh (b) - std::cos (b)) / (std::sinh (b) - std::sin (b));
            phi = std::cosh (bx) + std::cos (bx) - s * (std::sinh (bx) + std::sin (bx));
        }
        else
        {
            const double s = (std::cosh (b) + std::cos (b)) / (std::sinh (b) + std::sin (b));
            phi = std::cosh (bx) - std::cos (bx) - s * (std::sinh (bx) - std::sin (bx));
        }
    }
    else   // high modes: the hyperbolic parts cancel to a decaying edge term
        phi = shape == 0 ? std::cos (bx) - std::sin (bx) + std::exp (-bx) : -std::cos (bx) + std::sin (bx) + std::exp (-bx);
    return (float) (std::abs (phi) * 0.5);
}

// ===================================================================== biquads
void Bq::bandpass (double sr, double hz, double q)
{
    hz = jlimit (10.0, sr * 0.45, hz);
    const double w = MathConstants<double>::twoPi * hz / sr, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    b0 = (float) (al / a0); b1 = 0.0f; b2 = (float) (-al / a0);
    a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - al) / a0);
}

void Bq::lowpass (double sr, double hz, double q)
{
    hz = jlimit (10.0, sr * 0.45, hz);
    const double w = MathConstants<double>::twoPi * hz / sr, al = std::sin (w) / (2.0 * q), cw = std::cos (w), a0 = 1.0 + al;
    b0 = (float) ((1.0 - cw) * 0.5 / a0); b1 = (float) ((1.0 - cw) / a0); b2 = b0;
    a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - al) / a0);
}

void Bq::highShelf (double sr, double hz, double db)
{
    hz = jlimit (10.0, sr * 0.45, hz);
    const double A = std::pow (10.0, db / 40.0), w = MathConstants<double>::twoPi * hz / sr;
    const double cw = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / 0.7 - 1.0) + 2.0), sa = 2.0 * std::sqrt (A) * al;
    const double a0 = (A + 1) - (A - 1) * cw + sa;
    b0 = (float) (A * ((A + 1) + (A - 1) * cw + sa) / a0);
    b1 = (float) (-2 * A * ((A - 1) + (A + 1) * cw) / a0);
    b2 = (float) (A * ((A + 1) + (A - 1) * cw - sa) / a0);
    a1 = (float) (2 * ((A - 1) - (A + 1) * cw) / a0);
    a2 = (float) (((A + 1) - (A - 1) * cw - sa) / a0);
}

// ===================================================================== bodies (Hz, Q, dB)
const Body& body (int i)
{
    static const Body b[numBodies] = {
        { "Sin cuerpo", {}, {}, {}, 0 },
        { "Violonchelo", { 98, 185, 220, 275, 400, 560, 760, 1100 }, { 12, 15, 18, 16, 10, 9, 8, 6 }, { 6, 5, 4, 3, 2, 1, -1, -3 }, 8 },
        { "Contrabajo", { 62, 118, 145, 190, 270, 390, 540, 800 }, { 12, 14, 16, 14, 10, 9, 7, 6 }, { 6, 5, 4, 3, 2, 0, -2, -4 }, 8 },
        { "Arpa", { 110, 170, 260, 390, 600, 900, 1400, 2200 }, { 8, 10, 12, 10, 8, 7, 6, 5 }, { 4, 3, 3, 2, 1, 0, -2, -4 }, 8 },
        { "Calabaza", { 140, 290, 450, 700, 1100, 1700, 2400, 3300 }, { 20, 14, 10, 8, 8, 8, 8, 8 }, { 6, 3, 2, 0, -6, -6, -6, -6 }, 8 },
        { "Caja de madera", { 130, 210, 330, 480, 700, 1000, 1500, 2300 }, { 10, 12, 10, 9, 8, 7, 6, 5 }, { 4, 3, 3, 2, 1, 0, -2, -4 }, 8 },
        { "Tanque de metal", { 75, 168, 301, 455, 690, 930, 1330, 1810 }, { 40, 35, 30, 28, 25, 22, 20, 18 }, { 5, 4, 4, 3, 2, 1, 0, -1 }, 8 },
        { "Tubo afinado", {}, {}, {}, 0 },
    };
    return b[jlimit (0, numBodies - 1, i)];
}
}

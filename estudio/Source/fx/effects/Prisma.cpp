// Prisma: the sound split in four bands (Sub, Cuerpo, Filo, Aire), each with its own saturation, punch, width,
// level and sends to a shared echo and a shared reverb. "Refracción" delays each band a fraction of a millisecond
// on one side, so the bands spread across the stereo field like light through a prism.
//
// The DSP, in order:
//  1. Linkwitz-Riley 4th-order crossovers in a tree (split at the middle crossover, then each half), with allpass
//     compensation on each branch, so the four bands add back up flat (an allpass of the original).
//  2. Each band is oversampled 4x (polyphase IIR) and goes through a tanh with first-order ADAA. Every band uses
//     the same oversampler and the same half-sample average, so their phase stays matched and the sum stays flat
//     at any drive. The dry signal goes through an identical oversampler, so the Mezcla knob does not comb.
//  3. At the base rate: punch (a transient shaper: fast follower over slow follower, linked across L and R),
//     width (mid/side), refraction (a short delay on one side), level, and the sends.
//  4. Echo (tempo synced, ping-pong, a low-pass in the loop) and reverb (the Racoon FDN) on the sends.
#include "AllEffects.h"
#include "engine/I18n.h"
#include "FxBase.h"
#include "ui/ProLook.h"
#include "fx/core/RacoonEditor.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }

constexpr int numBands = 4;
const char* const bandIds[numBands] { "s", "b", "c", "a" };
const char* const bandNames[numBands] { "Sub", "Cuerpo", "Filo", "Aire" };

// how long the band's followers are: the lows need slower ones or they follow the waveform itself
const double fastRelMs[numBands] { 60.0, 35.0, 25.0, 20.0 };
const double slowAtkMs[numBands] { 40.0, 25.0, 18.0, 15.0 };
// refraction: how far each band moves (ms at 100 %) and to which side (+1 delays the right)
const float refractMs[numBands] { 0.0f, 0.22f, 0.48f, 0.80f };
const float refractSide[numBands] { 0.0f, 1.0f, -1.0f, 1.0f };

juce::Colour bandColour (int b)
{
    switch (b)
    {
        case 0: return ui::Brand::rosa;
        case 1: return ui::Brand::limon;
        case 2: return ui::Brand::menta;
        default: return ui::Brand::hielo;
    }
}
}

class Prisma : public FxBase
{
public:
    Prisma() : FxBase ("Prisma", "Creativos y espectrales", layout(), 12.0, false, true)
    {
        // the band parameters are looked up once here: the audio thread never builds a string
        for (int k = 0; k < numBands; ++k)
        {
            const String id (bandIds[k]);
            bp[k] = { param (id + "_drive"), param (id + "_punch"), param (id + "_width"), param (id + "_level"), param (id + "_echo"), param (id + "_verb") };
        }
    }

    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "x1", "Cruce Sub", 40.0f, 400.0f, 120.0f);
        P::hz (l, "x2", "Cruce Cuerpo", 250.0f, 4000.0f, 1000.0f);
        P::hz (l, "x3", "Cruce Aire", 2000.0f, 14000.0f, 6000.0f);
        for (int k = 0; k < numBands; ++k)
        {
            const String id (bandIds[k]), n = String (bandNames[k]) + " ";
            P::pct (l, id + "_drive", n + "saturación"_u, 0.0f, 100.0f, 0.0f);
            P::pct (l, id + "_punch", n + "golpe", -100.0f, 100.0f, 0.0f);
            P::pct (l, id + "_width", n + "ancho", 0.0f, 200.0f, k == 0 ? 0.0f : 100.0f);
            P::db (l, id + "_level", n + "nivel", -24.0f, 12.0f, 0.0f);
            P::pct (l, id + "_echo", n + "eco", 0.0f, 100.0f, 0.0f);
            P::pct (l, id + "_verb", n + "reverb", 0.0f, 100.0f, 0.0f);
        }
        P::pct (l, "refract", "Refracción"_u, 0.0f, 100.0f, 0.0f);
        P::choice (l, "echodiv", "Eco división"_u, RacoonProcessor::divisionNames(), 8);
        P::pct (l, "echofb", "Eco repeticiones", 0.0f, 90.0f, 35.0f);
        P::hz (l, "echotone", "Eco tono", 1000.0f, 18000.0f, 6000.0f);
        P::toggle (l, "pingpong", "Eco ping-pong", true);
        P::pct (l, "verbsize", "Reverb tamaño"_u, 0.0f, 100.0f, 55.0f);
        P::pct (l, "verbdamp", "Reverb amortiguación"_u, 0.0f, 100.0f, 40.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "out", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }

    void prepare (double rate, int maxBlock) override
    {
        block = jmax (64, maxBlock);
        for (int k = 0; k < numBands; ++k)
        {
            band[k].setSize (2, block);
            os[k].prepare (block, 2);
            refr[k][0].prepare ((int) (rate * 0.003) + 8);
            refr[k][1].prepare ((int) (rate * 0.003) + 8);
        }
        osDry.prepare (block, 2);
        dry.setSize (2, block);
        eBus.setSize (2, block);
        vBus.setSize (2, block);
        echo[0].prepare ((int) (rate * 4.1) + 8);
        echo[1].prepare ((int) (rate * 4.1) + 8);
        verb.setSampleRate (rate);
        reset();
    }

    void reset() override
    {
        for (auto* f : allFilters()) f->reset();
        for (int k = 0; k < numBands; ++k)
        {
            os[k].reset();
            for (int c = 0; c < 2; ++c) { sat[k][c].reset(); prevX[k][c] = 0.0f; refr[k][c].clear(); }
            fast[k] = slow[k] = 0.0f;
            punchG[k] = 1.0f;
            levelNow[k] = -1.0f;
            gNow[k] = -1.0f;
            refrNow[k] = 0.0f;
            shown[k].store (-120.0f);
        }
        osDry.reset();
        echo[0].clear(); echo[1].clear();
        echoLp[0] = echoLp[1] = 0.0f;
        echoNow = -1.0;
        verb.reset();
        curX[0] = curX[1] = curX[2] = 0.0;
    }

    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        if (n == 0 || ch == 0) return;
        if (n > block)   // a host that sends more than it promised: grow once (rare)
        {
            block = n;
            for (auto& x : band) x.setSize (2, n, false, false, true);
            dry.setSize (2, n, false, false, true); eBus.setSize (2, n, false, false, true); vBus.setSize (2, n, false, false, true);
        }
        auto* L = b.getWritePointer (0);
        auto* R = ch > 1 ? b.getWritePointer (1) : L;

        // ---- crossovers (they glide: a jump in a crossover's coefficients rings)
        double want[3] { value ("x1"), value ("x2"), value ("x3") };
        want[1] = jmax (want[1], want[0] * 1.6);
        want[2] = jmax (want[2], want[1] * 1.6);
        for (int k = 0; k < 3; ++k) curX[k] = curX[k] <= 0.0 ? want[k] : curX[k] * std::pow (want[k] / curX[k], 0.25);
        const double nyq = sr * 0.45;
        const double x1 = jmin (curX[0], nyq), x2 = jmin (curX[1], nyq), x3 = jmin (curX[2], nyq);
        for (int s = 0; s < 2; ++s)
        {
            lp2[s].set (Biquad::lowpass, sr, x2, 0.7071); hp2[s].set (Biquad::highpass, sr, x2, 0.7071);
            lp1[s].set (Biquad::lowpass, sr, x1, 0.7071); hp1[s].set (Biquad::highpass, sr, x1, 0.7071);
            lp3[s].set (Biquad::lowpass, sr, x3, 0.7071); hp3[s].set (Biquad::highpass, sr, x3, 0.7071);
        }
        // LP4 + HP4 at the same frequency add up to a second-order allpass with the Butterworth Q
        apLow.set (Biquad::allpass, sr, x3, 0.7071);
        apHigh.set (Biquad::allpass, sr, x1, 0.7071);

        // ---- split
        for (int c = 0; c < ch; ++c)
        {
            const float* in = b.getReadPointer (c);
            float* d[numBands] { band[0].getWritePointer (c), band[1].getWritePointer (c), band[2].getWritePointer (c), band[3].getWritePointer (c) };
            float* dd = dry.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float x = in[i];
                float lo = lp2[1].process (lp2[0].process (x, c), c);
                float hi = hp2[1].process (hp2[0].process (x, c), c);
                lo = apLow.process (lo, c);
                hi = apHigh.process (hi, c);
                d[0][i] = lp1[1].process (lp1[0].process (lo, c), c);
                d[1][i] = hp1[1].process (hp1[0].process (lo, c), c);
                d[2][i] = lp3[1].process (lp3[0].process (hi, c), c);
                d[3][i] = hp3[1].process (hp3[0].process (hi, c), c);
                dd[i] = d[0][i] + d[1][i] + d[2][i] + d[3][i];   // the dry signal is the bands added back: the same phase turn
            }
        }

        // ---- saturation, 4x (at 2x the half-sample average of ADAA already takes 1.6 dB at 18 kHz), every band (and the dry) through the same oversampler
        for (int k = 0; k < numBands; ++k)
        {
            const float drive = bp[k].drive->load() * 0.01f;
            const float gTarget = toGain (drive * 30.0f);
            const float wetTarget = jmin (1.0f, drive * 5.0f);
            if (gNow[k] < 0.0f) { gNow[k] = gTarget; wetNow[k] = wetTarget; }
            const int upN = n * os[k].factor();
            const float gInc = (gTarget - gNow[k]) / (float) upN, wInc = (wetTarget - wetNow[k]) / (float) upN;
            AudioBuffer<float> view (band[k].getArrayOfWritePointers(), ch, n);
            // the gain glides over the oversampled block; each channel walks the same ramp
            float gStart = gNow[k], wStart = wetNow[k];
            int count[2] {};
            os[k].process (view, [&, k] (float x, int c)
            {
                const float g = gStart + gInc * (float) count[c];
                const float w = wStart + wInc * (float) count[c];
                ++count[c];
                // the half-sample average is what ADAA does to a straight line: the clean path gets the same delay
                const float clean = 0.5f * (x + prevX[k][c]);
                prevX[k][c] = x;
                const float s = sat[k][c].process (x * g) * std::pow (g, -0.8f);
                return clean + w * (s - clean);
            });
            gNow[k] = gTarget; wetNow[k] = wetTarget;
        }
        {
            AudioBuffer<float> view (dry.getArrayOfWritePointers(), ch, n);
            float prev[2] { dryPrev[0], dryPrev[1] };
            osDry.process (view, [&] (float x, int c) { const float y = 0.5f * (x + prev[c]); prev[c] = x; return y; });
            dryPrev[0] = prev[0]; dryPrev[1] = prev[1];
        }

        // ---- punch, width, refraction, level, sends; sum
        eBus.clear(); vBus.clear();
        const float refract = value ("refract") * 0.01f;
        float bandPk[numBands] {};
        for (int i = 0; i < n; ++i) { L[i] = 0.0f; if (ch > 1) R[i] = 0.0f; }
        for (int k = 0; k < numBands; ++k)
        {
            const float punch = bp[k].punch->load() * 0.01f;
            const float width = bp[k].width->load() * 0.01f;
            const float lvTarget = toGain (bp[k].level->load());
            const float eSend = bp[k].echo->load() * 0.01f, vSend = bp[k].verb->load() * 0.01f;
            if (levelNow[k] < 0.0f) levelNow[k] = lvTarget;
            const float lvInc = (lvTarget - levelNow[k]) / (float) n;
            const float rTarget = refract * refractMs[k] * 0.001f * (float) sr;
            const float rInc = (rTarget - refrNow[k]) / (float) n;
            const float fA = dsp2::coeffMs (sr, 0.4), fR = dsp2::coeffMs (sr, fastRelMs[k]);
            const float sA = dsp2::coeffMs (sr, slowAtkMs[k]), sR = dsp2::coeffMs (sr, 260.0);
            const float gSm = dsp2::coeffMs (sr, 2.0);
            float* dl = band[k].getWritePointer (0);
            float* dr = band[k].getWritePointer (ch > 1 ? 1 : 0);
            for (int i = 0; i < n; ++i)
            {
                float l = dl[i], r = ch > 1 ? dr[i] : l;
                // punch: how far the fast follower is above the slow one is the transient
                const float a = jmax (std::abs (l), std::abs (r));
                fast[k] = a > fast[k] ? a + (fast[k] - a) * fA : a + (fast[k] - a) * fR;
                slow[k] = a > slow[k] ? a + (slow[k] - a) * sA : a + (slow[k] - a) * sR;
                float pg = 1.0f;
                if (punch != 0.0f && slow[k] > 1.0e-5f)
                {
                    const float ratioDb = jlimit (0.0f, 24.0f, toDb (fast[k] / slow[k]));
                    pg = toGain (jlimit (-15.0f, 12.0f, punch * ratioDb * 1.2f));
                }
                punchG[k] = pg + (punchG[k] - pg) * gSm;
                l *= punchG[k]; r *= punchG[k];
                // width
                if (ch > 1)
                {
                    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
                    l = m + s; r = m - s;
                }
                // refraction: one side of the band a little later
                refrNow[k] += rInc;
                if (k > 0 && ch > 1)
                {
                    refr[k][0].push (l); refr[k][1].push (r);
                    if (refrNow[k] > 0.0001f)
                    {
                        // after the push, delay index 1 is the sample itself: D samples late is index D + 1
                        auto& line = refr[k][refractSide[k] > 0.0f ? 1 : 0];
                        const float D = refrNow[k];
                        const float late = D < 1.0f ? line.tapInt (1) + D * (line.tapInt (2) - line.tapInt (1)) : line.tap (D + 1.0f);
                        if (refractSide[k] > 0.0f) r = late; else l = late;
                    }
                }
                levelNow[k] += lvInc;
                l *= levelNow[k]; r *= levelNow[k];
                bandPk[k] = jmax (bandPk[k], jmax (std::abs (l), std::abs (r)));
                L[i] += l;
                if (ch > 1) R[i] += r;
                if (eSend > 0.0f) { eBus.getWritePointer (0)[i] += l * eSend; eBus.getWritePointer (1)[i] += r * eSend; }
                if (vSend > 0.0f) { vBus.getWritePointer (0)[i] += l * vSend; vBus.getWritePointer (1)[i] += r * vSend; }
            }
            levelNow[k] = lvTarget;
            const float shownNow = shown[k].load();
            const float pkDb = toDb (bandPk[k]);
            shown[k].store (pkDb > shownNow ? pkDb : jmax (pkDb, shownNow - 2.5f));
        }

        // ---- echo
        {
            const double want = jlimit (2.0, sr * 4.0, syncSeconds ((int) value ("echodiv"), bpm()) * sr);
            if (echoNow < 0.0) echoNow = want;
            const float fb = value ("echofb") * 0.01f;
            const bool pp = value ("pingpong") > 0.5f;
            const float tone = 1.0f - std::exp (-MathConstants<float>::twoPi * jmin (value ("echotone"), (float) (sr * 0.45)) / (float) sr);
            const double glide = 1.0 - dsp2::coeffMs (sr, 60.0);
            const float* el = eBus.getReadPointer (0);
            const float* er = eBus.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                echoNow += (want - echoNow) * glide;
                const float tl = echo[0].tap ((float) echoNow), tr = echo[1].tap ((float) echoNow);
                echoLp[0] += tone * (tl - echoLp[0]);
                echoLp[1] += tone * (tr - echoLp[1]);
                if (pp)
                {
                    echo[0].push (0.5f * (el[i] + er[i]) + echoLp[1] * fb);
                    echo[1].push (echoLp[0] * fb);
                }
                else
                {
                    echo[0].push (el[i] + echoLp[0] * fb);
                    echo[1].push (er[i] + echoLp[1] * fb);
                }
                L[i] += tl;
                if (ch > 1) R[i] += tr;
            }
        }

        // ---- reverb
        {
            sdsp::Verb::Parameters vp;
            vp.roomSize = value ("verbsize") * 0.01f;
            vp.damping = value ("verbdamp") * 0.01f;
            vp.wetLevel = 1.0f / 3.0f;
            vp.dryLevel = 0.0f;
            vp.width = 1.0f;
            verb.setParameters (vp);
            verb.processStereo (vBus.getWritePointer (0), vBus.getWritePointer (1), n);
            for (int i = 0; i < n; ++i)
            {
                L[i] += vBus.getSample (0, i);
                if (ch > 1) R[i] += vBus.getSample (1, i);
            }
        }

        // ---- mix and output
        const float mix = value ("mix") * 0.01f, out = toGain (value ("out"));
        for (int c = 0; c < ch; ++c)
        {
            auto* y = b.getWritePointer (c);
            const float* d = dry.getReadPointer (c);
            for (int i = 0; i < n; ++i) y[i] = (d[i] + mix * (y[i] - d[i])) * out;
        }
    }

    float bandLevelDb (int k) const { return shown[k].load(); }
    float bandDrive (int k) const { return bp[k].drive->load() * 0.01f; }
    float bandWidth (int k) const { return bp[k].width->load() * 0.01f; }

    std::unique_ptr<Component> makeDisplay() override;

    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.width = 1120;
        s.easyHeight = 620;
        auto bandSec = [] (int k)
        {
            const String id (bandIds[k]);
            return Sec { bandNames[k], { { id + "_drive", "Saturación"_u, "Calienta solo esta banda: armónicos sin ensuciar las demás."_u },
                                         { id + "_punch", "Golpe", "A la derecha más ataque, a la izquierda lo suaviza."_u },
                                         { id + "_level", "Nivel", "Volumen de la banda."_u } }, false, bandColour (k) };
        };
        s.sections = {
            bandSec (0), bandSec (1), bandSec (2), bandSec (3),
            { "Prisma", { { "refract", "Refracción"_u, "Atrasa un lado de cada banda una fracción de milisegundo: los agudos se abren y el Sub se queda al centro."_u },
                          { "mix", "Mezcla", "Con la señal original."_u }, { "out", "Salida", "Volumen final."_u } } },
            { "Ancho", { { "s_width", "Sub", "0 % deja los graves en mono, como en un club."_u }, { "b_width", "Cuerpo", "Ancho de los medios graves."_u },
                         { "c_width", "Filo", "Ancho de los medios agudos."_u }, { "a_width", "Aire", "Ancho del brillo."_u } }, true },
            { "Envíos"_u, { { "s_echo", "Sub eco", "Cuánto del Sub va al eco."_u }, { "b_echo", "Cuerpo eco", "Cuánto del Cuerpo va al eco."_u },
                            { "c_echo", "Filo eco", "Cuánto del Filo va al eco."_u }, { "a_echo", "Aire eco", "Cuánto del Aire va al eco."_u },
                            { "s_verb", "Sub reverb", "Cuánto del Sub va a la reverb."_u }, { "b_verb", "Cuerpo reverb", "Cuánto del Cuerpo va a la reverb."_u },
                            { "c_verb", "Filo reverb", "Cuánto del Filo va a la reverb."_u }, { "a_verb", "Aire reverb", "Cuánto del Aire va a la reverb."_u } }, true },
            { "Eco y reverb", { { "echodiv", "División"_u, "Tiempo del eco al tempo."_u }, { "echofb", "Repeticiones", "Cuántas vueltas da."_u },
                                { "echotone", "Tono", "Cada vuelta pierde agudos por encima de aquí."_u }, { "pingpong", "Ping-pong", "Las repeticiones saltan de un lado al otro."_u },
                                { "verbsize", "Tamaño"_u, "Qué tan grande es la sala."_u }, { "verbdamp", "Amortiguación"_u, "Apaga los agudos de la cola."_u } }, true },
            { "Cruces", { { "x1", "Sub", "Dónde termina el Sub."_u }, { "x2", "Cuerpo", "Dónde termina el Cuerpo y empieza el Filo."_u },
                          { "x3", "Aire", "Dónde empieza el Aire."_u } }, true },
        };
        return s;
    }

    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Bajo con filo", "Bajo", { { "s_drive", 10 }, { "b_drive", 45 }, { "c_drive", 60 }, { "c_level", 2 }, { "a_level", -3 }, { "s_width", 0 } } },
                 { "Batería con golpe"_u, "Batería"_u, { { "s_punch", 35 }, { "b_punch", 50 }, { "c_punch", 40 }, { "c_drive", 25 }, { "a_drive", 15 } } },
                 { "Voz que brilla", "Voz", { { "c_drive", 20 }, { "a_drive", 30 }, { "a_width", 140 }, { "a_verb", 25 }, { "c_echo", 15 }, { "refract", 30 } } },
                 { "Prisma abierto", "Creativos", { { "refract", 80 }, { "b_width", 130 }, { "c_width", 160 }, { "a_width", 180 }, { "a_verb", 35 }, { "c_echo", 25 } } },
                 { "Pad lejano", "Creativos", { { "s_level", -6 }, { "b_verb", 40 }, { "c_verb", 55 }, { "a_verb", 70 }, { "c_echo", 30 }, { "echofb", 55 }, { "verbsize", 80 }, { "refract", 50 } } } };
    }

    // what the screen shows
    double crossover (int k) const { return curX[k] > 0.0 ? curX[k] : (k == 0 ? 120.0 : k == 1 ? 1000.0 : 6000.0); }

private:
    struct BandParams { std::atomic<float>* drive, *punch, *width, *level, *echo, *verb; };
    BandParams bp[numBands] {};
    int block = 512;
    AudioBuffer<float> band[numBands], dry, eBus, vBus;
    dsp2::Oversampler2 os[numBands], osDry;
    Biquad lp1[2], hp1[2], lp2[2], hp2[2], lp3[2], hp3[2], apLow, apHigh;
    sdsp::Adaa1<sdsp::curve::Tanh> sat[numBands][2];
    float prevX[numBands][2] {}, dryPrev[2] {};
    float gNow[numBands] { -1, -1, -1, -1 }, wetNow[numBands] {}, levelNow[numBands] { -1, -1, -1, -1 };
    float fast[numBands] {}, slow[numBands] {}, punchG[numBands] { 1, 1, 1, 1 }, refrNow[numBands] {};
    racoon::dsp::DelayLine refr[numBands][2], echo[2];
    float echoLp[2] {};
    double echoNow = -1.0;
    sdsp::Verb verb;
    double curX[3] {};
    std::atomic<float> shown[numBands] { -120.0f, -120.0f, -120.0f, -120.0f };

    std::vector<Biquad*> allFilters()
    {
        std::vector<Biquad*> v;
        for (int s = 0; s < 2; ++s) for (auto* f : { &lp1[s], &hp1[s], &lp2[s], &hp2[s], &lp3[s], &hp3[s] }) v.push_back (f);
        v.push_back (&apLow); v.push_back (&apHigh);
        return v;
    }
};

// ===================================================================== screen
// The spectrum from 20 Hz to 20 kHz, split where the crossovers are. Each band is a column in its colour with its
// level as a bar, its saturation as a glow at the bottom and its width as two marks that open apart.
class PrismaDisplay : public Component, private Timer
{
public:
    explicit PrismaDisplay (Prisma& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        using namespace ui;
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
        auto plot = r.withTrimmedBottom (18.0f).withTrimmedTop (20.0f);
        auto xFor = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
        auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * jlimit (0.0f, 1.0f, -db / 48.0f); };
        g.setFont (NumFont::medium (9.5f));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (Pro::grid);
            g.fillRect (xFor (hz), plot.getY(), 1.0f, plot.getHeight());
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (fmtHz ((float) hz)), Rectangle<float> (xFor (hz) - 24.0f, plot.getBottom() + 3.0f, 48.0f, 12.0f), Justification::centred);
        }
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f })
        {
            g.setColour (Pro::grid);
            g.fillRect (plot.getX(), yFor (db), plot.getWidth(), 1.0f);
        }
        const double edges[5] { 20.0, proc.crossover (0), proc.crossover (1), proc.crossover (2), 20000.0 };
        for (int k = 0; k < numBands; ++k)
        {
            const float x0 = xFor (edges[k]), x1 = xFor (edges[k + 1]);
            auto col = Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight());
            const auto c = bandColour (k);
            const float drive = proc.bandDrive (k);
            // the zone, a little warmer the more it is driven
            g.setColour (c.withAlpha (0.07f + 0.18f * drive));
            g.fillRect (col);
            if (drive > 0.0f)
            {
                ColourGradient glow (c.withAlpha (0.55f * drive), col.getCentreX(), col.getBottom(), c.withAlpha (0.0f), col.getCentreX(), col.getBottom() - col.getHeight() * 0.6f, false);
                g.setGradientFill (glow);
                g.fillRect (col);
            }
            // level bar
            const float db = jmax (-60.0f, proc.bandLevelDb (k));
            const float bw = jmin (26.0f, col.getWidth() * 0.35f);
            auto bar = Rectangle<float> (col.getCentreX() - bw * 0.5f, yFor (db), bw, plot.getBottom() - yFor (db));
            g.setColour (c);
            g.fillRect (bar);
            // width: two ticks that open with the band's width
            const float w = proc.bandWidth (k);
            const float half = jmin (col.getWidth() * 0.45f, bw * 0.5f + 4.0f + w * col.getWidth() * 0.18f);
            g.setColour (Pro::text.withAlpha (0.8f));
            g.fillRect (col.getCentreX() - half - 1.0f, plot.getY() + 6.0f, 2.0f, 10.0f);
            g.fillRect (col.getCentreX() + half - 1.0f, plot.getY() + 6.0f, 2.0f, 10.0f);
            // name and numbers
            ProDraw::caps (g, bandNames[k], Rectangle<float> (x0 + 4.0f, r.getY(), x1 - x0 - 8.0f, 14.0f), Justification::centredLeft, c, 10.5f);
            g.setColour (Pro::textDim);
            g.setFont (NumFont::medium (10.0f));
            const String info = db <= -59.0f ? String ("-inf") : fmtDb (db);
            g.drawText (::racoon::i18n::tr (info), Rectangle<float> (x0 + 4.0f, r.getY(), x1 - x0 - 8.0f, 14.0f), Justification::centredRight);
        }
        // the crossovers
        for (int k = 1; k <= 3; ++k)
        {
            g.setColour (Pro::lineHi);
            g.fillRect (xFor (edges[k]) - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        }
    }
private:
    Prisma& proc;
    void timerCallback() override { repaint(); }
};

std::unique_ptr<Component> Prisma::makeDisplay() { return std::make_unique<PrismaDisplay> (*this); }

void registerPrisma (std::vector<FxDef>& v)
{
    v.push_back ({ "prisma", "Prisma", "Creativos y espectrales",
                   "Separa el sonido en cuatro bandas y le da a cada una su saturación, golpe, ancho, eco y reverb."_u, Kind::effect,
                   [] { return std::make_unique<Prisma>(); }, { "b_drive", "c_drive", "c_punch", "refract", "a_verb", "out" } });
}
}

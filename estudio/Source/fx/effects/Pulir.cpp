// Pulir: one knob and four modes that leave a voice, a bass, a pad or an instrument finished. Three bands of
// compression, saturation, air and width, all scaled by the knob: Hedgehog's Particle made for a channel, with the
// fixed numbers of the master version turned into a voicing per mode.
#include "AllEffects.h"
#include "engine/I18n.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
struct Voicing
{
    float maxA = 1.5f;
    float lowXo = 200.0f, highXo = 3000.0f;
    float baseThr[3] { -12.0f, -15.0f, -21.0f };
    float thrPerAmount = 3.0f, ratioPerAmount = 1.5f;
    float atkMs[3] { 30.0f, 12.0f, 4.0f }, relMs[3] { 220.0f, 140.0f, 90.0f };
    float makeupPerAmount[3] { 1.6f, 1.4f, 1.8f };
    float satLo = 1.2f, satMid = 0.7f;
    float airHz = 7000.0f, airAmount = 0.06f;
    float lowSideCut = 0.35f, midHighWiden = 0.1f;
    float monoBelowHz = 0.0f;
};

const Voicing& voicing (int mode)
{
    static const Voicing v[4] = {
        // A · Voz
        { 1.2f, 250.0f, 4500.0f, { -18.0f, -20.0f, -24.0f }, 3.0f, 2.0f, { 15.0f, 6.0f, 1.5f }, { 160.0f, 110.0f, 60.0f }, { 1.6f, 1.4f, 1.8f },
          0.6f, 0.9f, 9000.0f, 0.08f, 0.6f, 0.05f, 0.0f },
        // B · Bajo
        { 1.4f, 120.0f, 1500.0f, { -14.0f, -18.0f, -22.0f }, 3.0f, 1.5f, { 25.0f, 10.0f, 3.0f }, { 200.0f, 120.0f, 80.0f }, { 1.6f, 1.4f, 1.8f },
          1.4f, 1.0f, 9000.0f, 0.0f, 0.0f, 0.0f, 120.0f },
        // C · Pad
        { 1.0f, 200.0f, 5000.0f, { -16.0f, -18.0f, -22.0f }, 3.0f, 1.2f, { 40.0f, 30.0f, 15.0f }, { 300.0f, 250.0f, 200.0f }, { 1.6f, 1.4f, 1.8f },
          0.4f, 0.4f, 10000.0f, 0.07f, 0.8f, 0.25f, 0.0f },
        // D · Instrumento (Hedgehog's own numbers)
        Voicing{},
    };
    return v[jlimit (0, 3, mode)];
}

// the Particle with a voicing (the same steps as Hedgehog's, on the family's ramped biquads)
class Particula
{
public:
    void prepare (double sampleRate, const Voicing& vc)
    {
        sr = sampleRate; v = vc;
        for (int s = 0; s < 2; ++s)
        {
            lp1[s].set (Biquad::lowpass, sr, v.lowXo, 0.7071); hp1[s].set (Biquad::highpass, sr, v.lowXo, 0.7071);
            lp2[s].set (Biquad::lowpass, sr, v.highXo, 0.7071); hp2[s].set (Biquad::highpass, sr, v.highXo, 0.7071);
            apLp[s].set (Biquad::lowpass, sr, v.highXo, 0.7071); apHp[s].set (Biquad::highpass, sr, v.highXo, 0.7071);
        }
        air.set (Biquad::highpass, sr, v.airHz, 0.7071);
        if (v.monoBelowHz > 0.0f) for (auto& f : monoHp) f.set (Biquad::highpass, sr, v.monoBelowHz, 0.7071);
        for (int b = 0; b < 3; ++b)
        {
            atk[b] = (float) std::exp (-1.0 / (sr * v.atkMs[b] * 0.001));
            rel[b] = (float) std::exp (-1.0 / (sr * v.relMs[b] * 0.001));
        }
        smooth = (float) std::exp (-1.0 / (sr * 0.004));
        lastLow = lastHigh = 99.0f;
        reset();
    }
    void reset()
    {
        for (auto* f : { &preLow, &preHigh, &air, &lp1[0], &lp1[1], &hp1[0], &hp1[1], &lp2[0], &lp2[1], &hp2[0], &hp2[1], &apLp[0], &apLp[1], &apHp[0], &apHp[1], &monoHp[0], &monoHp[1] })
            f->reset();
        for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gainSm[b] = 1.0f; gr[b] = 0.0f; }
        for (auto& a : airSat) a.reset();
    }
    // A: the amount after the mode's scale; air and width multiply the voicing's own
    void process (float* L, float* R, int n, float A, float lowDb, float highDb, float airMul, float widthMul)
    {
        if (lowDb != lastLow) { preLow.set (Biquad::lowshelf, sr, 110.0, 0.7071, lowDb); lastLow = lowDb; }
        if (highDb != lastHigh) { preHigh.set (Biquad::highshelf, sr, 9000.0, 0.7071, highDb); lastHigh = highDb; }
        A = jlimit (0.0f, 2.0f, A);
        const float color = std::min (1.0f, A);
        const float ratio = 1.0f + v.ratioPerAmount * A;
        float thr[3], makeup[3];
        for (int b = 0; b < 3; ++b) { thr[b] = v.baseThr[b] - v.thrPerAmount * A; makeup[b] = dsp::dbToGain (v.makeupPerAmount[b] * A); }
        const float knee = 6.0f;
        float maxGr[3] {};
        auto satOf = [] (float x, float k) { return std::tanh (x * k) / k; };
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            float x[2] { preHigh.process (preLow.process (L[i], 0), 0), preHigh.process (preLow.process (R[i], 1), 1) };
            for (int c = 0; c < 2; ++c)
            {
                float lo = lp1[1].process (lp1[0].process (x[c], c), c);
                const float rest = hp1[1].process (hp1[0].process (x[c], c), c);
                const float mid = lp2[1].process (lp2[0].process (rest, c), c);
                const float hi = hp2[1].process (hp2[0].process (rest, c), c);
                lo = apLp[1].process (apLp[0].process (lo, c), c) + apHp[1].process (apHp[0].process (lo, c), c);
                band[0][c] = lo; band[1][c] = mid; band[2][c] = hi;
            }
            for (int b = 0; b < 3; ++b)
            {
                const float det = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                float g = 1.0f;
                if (A > 0.0f)
                {
                    env[b] = det + (env[b] - det) * (det > env[b] ? atk[b] : rel[b]);
                    const float over = dsp::gainToDb (env[b]) - thr[b];
                    float red = 0.0f;
                    if (over > knee * 0.5f) red = over * (1.0f - 1.0f / ratio);
                    else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; red = (1.0f - 1.0f / ratio) * k * k / (2.0f * knee); }
                    g = dsp::dbToGain (-red) * makeup[b];
                    maxGr[b] = std::max (maxGr[b], red);
                }
                gainSm[b] = g + (gainSm[b] - g) * smooth;
                band[b][0] *= gainSm[b]; band[b][1] *= gainSm[b];
            }
            if (color > 0.0f)
            {
                const float kLo = 1.0f + v.satLo * A, kMid = 1.0f + v.satMid * A;
                for (int c = 0; c < 2; ++c)
                {
                    band[0][c] += (satOf (band[0][c], kLo) - band[0][c]) * color;
                    band[1][c] += (satOf (band[1][c], kMid) - band[1][c]) * color;
                    if (v.airAmount > 0.0f && airMul > 0.0f)
                    {
                        const float h = air.process (band[2][c], c);
                        band[2][c] += v.airAmount * airMul * A * airSat[c].process (h * 4.0f);
                    }
                }
                // lows to the middle (all of them in mono below the bass mode's frequency), mids and highs wider
                if (v.monoBelowHz > 0.0f)
                {
                    float s = 0.5f * (band[0][0] - band[0][1]);
                    const float m = 0.5f * (band[0][0] + band[0][1]);
                    const float hs = monoHp[1].process (monoHp[0].process (s, 0), 0);
                    s = s + (hs - s) * color;
                    band[0][0] = m + s; band[0][1] = m - s;
                }
                const float lowSide = 1.0f - jmin (1.0f, v.lowSideCut * widthMul) * color, wide = 1.0f + v.midHighWiden * widthMul * A;
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
    float reductionDb (int b) const { return gr[b]; }
private:
    double sr = 48000.0;
    Voicing v;
    Biquad preLow, preHigh, lp1[2], hp1[2], lp2[2], hp2[2], apLp[2], apHp[2], air, monoHp[2];
    sdsp::Adaa1<sdsp::curve::Tanh> airSat[2];
    float env[3] {}, gainSm[3] { 1.0f, 1.0f, 1.0f }, gr[3] {};
    float atk[3] {}, rel[3] {}, smooth = 0.0f;
    float lastLow = 99.0f, lastHigh = 99.0f;
};

class Polish : public FxBase
{
public:
    Polish() : FxBase ("Pulir", "Dinámica"_u, layout()) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "amount", "Pulir", 0.0f, 100.0f, 50.0f);
        P::choice (l, "mode", "Modo", { "A · Voz"_u, "B · Bajo"_u, "C · Pad"_u, "D · Instrumento"_u }, 3);
        P::toggle (l, "match", "Igualar volumen", true);
        P::db (l, "tone", "Tono", -3.0f, 3.0f, 0.0f);
        P::pct (l, "air", "Aire", 0.0f, 150.0f, 100.0f);
        P::pct (l, "width", "Ancho", 0.0f, 150.0f, 100.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -12.0f, 12.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int block) override
    {
        mode = (int) value ("mode");
        active = 0; other = 1; modeOfActive = mode;
        for (auto& p : part) p.prepare (rate, voicing (mode));
        for (auto& b : bufs) b.setSize (2, jmax (block, 1024), false, false, true);
        fadeLeft = 0;
        matchC = (float) std::exp (-1.0 / (rate * 3.0));
        reset();
    }
    void reset() override
    {
        for (auto& p : part) p.reset();
        inPow = outPow = 1.0e-6; matchGain = 1.0f;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (auto& bb : bufs) if (bb.getNumSamples() < n) bb.setSize (2, n, false, false, true);
        const int want = (int) value ("mode");
        if (want != mode && fadeLeft == 0)
        {
            // the new mode starts on the other Particle and fades in over 20 ms, so switching does not click
            other = 1 - active;
            part[other].prepare (sr, voicing (want));
            mode = want;
            fadeLeft = fadeLen = jmax (1, (int) (0.02 * sr));
        }
        const float tone = value ("tone"), airMul = value ("air") * 0.01f, widthMul = value ("width") * 0.01f;
        const float mixAmt = value ("mix") * 0.01f, out = dsp::dbToGain (value ("output"));
        const bool match = value ("match") > 0.5f;
        auto run = [&] (int which, int modeOf)
        {
            auto& w = bufs[which];
            for (int c = 0; c < 2; ++c) w.copyFrom (c, 0, b, jmin (c, ch - 1), 0, n);
            const float A = value ("amount") * 0.01f * voicing (modeOf).maxA;
            part[which].process (w.getWritePointer (0), w.getWritePointer (1), n, A, -tone * 0.5f, tone * 0.5f, airMul, widthMul);
        };
        const int prevMode = fadeLeft > 0 ? modeOfActive : mode;
        run (active, prevMode);
        if (fadeLeft > 0) run (other, mode);
        for (int i = 0; i < n; ++i)
        {
            float y[2];
            for (int c = 0; c < 2; ++c) y[c] = bufs[active].getSample (c, i);
            if (fadeLeft > 0)
            {
                const float t = 1.0f - (float) fadeLeft / (float) fadeLen;
                for (int c = 0; c < 2; ++c) y[c] += (bufs[other].getSample (c, i) - y[c]) * t;
                if (--fadeLeft == 0) { active = other; modeOfActive = mode; }
            }
            // a slow volume match (3 s): the knob changes the sound, not the level
            float x2 = 0.0f, y2 = 0.0f;
            for (int c = 0; c < ch; ++c) { const float xi = b.getSample (c, i); x2 += xi * xi; y2 += y[c] * y[c]; }
            inPow = x2 + (inPow - x2) * matchC; outPow = y2 + (outPow - y2) * matchC;
            const float target = match && outPow > 1.0e-9 ? jlimit (0.25f, 4.0f, (float) std::sqrt (inPow / outPow)) : 1.0f;
            matchGain = target + (matchGain - target) * 0.9995f;
            for (int c = 0; c < ch; ++c)
            {
                const float xi = b.getSample (c, i);
                b.setSample (c, i, (xi + (y[c] * matchGain - xi) * mixAmt) * out);
            }
        }
        float worst = 0.0f;
        for (int k = 0; k < 3; ++k) worst = jmax (worst, part[active].reductionDb (k));
        grDb.store (-worst);
    }
    String displayText() const override
    {
        static const char* names[4] { "A · Voz", "B · Bajo", "C · Pad", "D · Instrumento" };
        return ::racoon::i18n::tr (String::fromUTF8 (names[jlimit (0, 3, mode)])) + " · " + String (grDb.load(), 1) + " dB";
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Pulir", { { "amount", "Pulir", "De 0 a 100: compresión por bandas, saturación, aire y ancho, todo junto."_u },
                         { "mode", "Modo", "A · Voz: junta la voz y le pone aire. B · Bajo: parejo y firme, graves en mono. C · Pad: suave y abierto. D · Instrumento: la Partícula de Hedgehog."_u } } },
            { "Detalles", { { "match", "Igualar", "La perilla cambia el sonido sin cambiar el volumen."_u },
                            { "tone", "Tono", "Inclina el sonido: negativo más cálido, positivo más brillante."_u },
                            { "air", "Aire", "Cuánto aire arriba."_u }, { "width", "Ancho", "Cuánto abre los lados y junta los graves."_u },
                            { "mix", "Mezcla", "Con la señal original."_u }, { "output", "Salida", "Volumen final."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Voz al frente", "Voz", { { "mode", 0 }, { "amount", 70 } } },
                 { "Voz suave", "Voz", { { "mode", 0 }, { "amount", 35 }, { "tone", -1 } } },
                 { "Bajo de club", "Bajo", { { "mode", 1 }, { "amount", 75 } } },
                 { "Bajo 808", "Bajo", { { "mode", 1 }, { "amount", 60 }, { "air", 0 }, { "width", 0 } } },
                 { "Pad abierto", "Pads", { { "mode", 2 }, { "amount", 60 }, { "width", 140 } } },
                 { "Acordes brillantes", "Instrumentos", { { "mode", 3 }, { "amount", 50 }, { "tone", 1.5f } } },
                 { "Guitarra lista", "Instrumentos", { { "mode", 3 }, { "amount", 45 }, { "air", 80 } } },
                 { "Coro", "Voz", { { "mode", 2 }, { "amount", 50 }, { "air", 120 } } } };
    }
private:
    Particula part[2];
    AudioBuffer<float> bufs[2];
    int active = 0, other = 1, mode = 3, modeOfActive = 3, fadeLeft = 0, fadeLen = 1;
    double inPow = 1.0e-6, outPow = 1.0e-6;
    float matchC = 0.9999f, matchGain = 1.0f;
};
}

void registerPulir (std::vector<FxDef>& v)
{
    v.push_back ({ "pulir", "Pulir", "Dinámica"_u, "Una perilla que deja terminada una voz, un bajo, un pad o un instrumento: compresión por bandas, saturación, aire y ancho."_u,
                   Kind::effect, [] { return std::make_unique<Polish>(); }, { "amount", "mode", "match", "tone", "mix", "output" } });
}
}

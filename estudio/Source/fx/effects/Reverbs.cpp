// Reverb y espacio: Reverb híbrida, Cuerpo resonante, Resonadores
#include "AllEffects.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toGain (float db) { return db <= -69.5f ? 0.0f : std::pow (10.0f, db * 0.05f); }
constexpr double twoPi = MathConstants<double>::twoPi;

// ===================================================================== Reverb híbrida
// an algorithmic tank (8-line feedback delay network) blended with a convolution of a generated space
class HybridReverb : public FxBase, private Timer
{
public:
    HybridReverb() : FxBase ("Reverb híbrida"_u, "Reverb y espacio", layout(), 10.0, false, true) { startTimerHz (5); }
    ~HybridReverb() override { stopTimer(); }
    static StringArray spaces() { return { "Cuarto", "Estudio", "Sala", "Salón grande"_u, "Catedral", "Placa", "Resorte", "Cueva" }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "space", "Espacio", spaces(), 2);
        P::flt (l, "irdecay", "Largo del espacio", P::range (10.0f, 200.0f, 100.0f), 100.0f, "%", 0);
        P::flt (l, "decay", "Caída del tanque"_u, P::range (0.2f, 30.0f, 2.5f), 2.5f, " s", 2);
        P::pct (l, "size", "Tamaño"_u, 5.0f, 100.0f, 60.0f);
        P::pct (l, "damping", "Amortiguar agudos", 0.0f, 100.0f, 40.0f);
        P::pct (l, "diffusion", "Difusión"_u, 0.0f, 100.0f, 70.0f);
        P::pct (l, "mod", "Movimiento", 0.0f, 100.0f, 20.0f);
        P::ms (l, "predelay", "Predelay", 0.0f, 250.0f, 15.0f, 40.0f);
        P::flt (l, "blend", "Mezcla tanque / espacio", P::range (0.0f, 100.0f), 50.0f, "%", 0);
        P::choice (l, "routing", "Ruta", { "Paralelo", "Serie (espacio al tanque)", "Solo espacio", "Solo tanque" }, 0);
        P::hz (l, "lowcut", "Corte de graves", 20.0f, 1000.0f, 120.0f);
        P::hz (l, "highcut", "Corte de agudos", 1000.0f, 20000.0f, 12000.0f);
        P::pct (l, "width", "Ancho", 0.0f, 150.0f, 100.0f);
        P::toggle (l, "freeze", "Congelar", false);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 30.0f);
        return l;
    }
    void prepare (double rate, int block) override
    {
        for (int k = 0; k < 8; ++k) lines[k].prepare ((int) (rate * 0.35));
        for (auto& a : inAp) a.prepare ((int) (rate * 0.02));
        juce::ignoreUnused (block);
        pre.prepare ((int) (rate * 0.26) + 64);
        juce::dsp::ProcessSpec spec { rate, (uint32) cb, 2 };
        conv.prepare (spec);
        convIn.setSize (2, cb);
        convOut.setSize (2, cb);
        loadedKey = {};
        loadIr();
        reset();
    }
    void reset() override
    {
        for (auto& d : lines) d.clear();
        for (auto& a : inAp) a.clear();
        pre.clear();
        for (auto& f : damp) f = 0.0f;
        modPh = 0.0;
        for (auto& f : lc) f.reset();
        for (auto& f : hc) f.reset();
        conv.reset();
        convIn.clear(); convOut.clear();
    }
    // the space is rebuilt off the audio thread when its controls move
    void timerCallback() override { loadIr(); }
    String irKey() const { return String ((int) value ("space")) + ":" + String ((int) value ("irdecay")) + ":" + String ((int) sampleRate()); }
    void loadIr()
    {
        const auto key = irKey();
        if (key == loadedKey) return;
        loadedKey = key;
        conv.loadImpulseResponse (makeIr ((int) value ("space"), value ("irdecay") * 0.01, sampleRate()), sampleRate(),
                                  juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
    }
    static AudioBuffer<float> makeIr (int space, double scale, double rate)
    {
        // seconds of decay, brightness (how slowly the highs die), density of early echoes
        struct S { double t60, bright, early, gap; };
        static const S s[] { { 0.45, 0.5, 0.9, 0.006 }, { 0.8, 0.6, 0.7, 0.009 }, { 1.8, 0.55, 0.5, 0.014 }, { 3.2, 0.5, 0.4, 0.022 },
                             { 6.0, 0.4, 0.3, 0.035 }, { 2.2, 0.85, 0.0, 0.0 }, { 2.0, 0.7, 0.0, 0.0 }, { 4.5, 0.3, 0.6, 0.05 } };
        const auto& p = s[jlimit (0, 7, space)];
        const double t60 = p.t60 * scale;
        const int len = jlimit (256, (int) (rate * 6.0), (int) (rate * t60 * 1.1));
        AudioBuffer<float> ir (2, len);
        Random r ((int64) space * 7919 + (int64) (scale * 1000));
        for (int c = 0; c < 2; ++c)
        {
            auto* d = ir.getWritePointer (c);
            float lp = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                const double t = i / rate;
                const double env = std::exp (-6.9078 * t / t60);
                // the lowpass closes as time goes on: highs decay faster than lows
                const float cutoff = (float) jlimit (0.02, 0.99, p.bright * std::exp (-t / (t60 * 0.5)) + 0.05);
                const float nz = r.nextFloat() * 2.0f - 1.0f;
                lp += (nz - lp) * cutoff;
                float v = (float) (lp * env);
                if (space == 6)   // spring: a dispersive chirp that repeats every 33 ms
                {
                    const double cyc = std::fmod (t, 0.033) / 0.033;
                    v = (float) (v * 0.4 + std::sin (twoPi * (200.0 + 3000.0 * cyc) * cyc * 0.033) * std::exp (-6.9078 * t / t60) * 0.6 * (1.0 - cyc));
                }
                d[i] = v;
            }
            // early reflections
            if (p.early > 0.0)
                for (int k = 1; k <= 10; ++k)
                {
                    const int at = (int) ((p.gap * k * (1.0 + 0.3 * r.nextDouble()) + c * 0.0013) * rate);
                    if (at < len) d[at] += (float) (p.early * std::pow (0.8, k) * (r.nextBool() ? 1 : -1));
                }
            // a short fade in so the start is soft
            for (int i = 0; i < jmin (len, 48); ++i) d[i] *= i / 48.0f;
        }
        return ir;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        static const int baseLen[8] { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        // while exporting nobody waits for the timer: the space follows its knob right here
        if (isNonRealtime() && irKey() != loadedKey) loadIr();
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const bool freeze = value ("freeze") > 0.5f;
        const float size = 0.2f + value ("size") * 0.01f * 1.6f;
        int len[8];
        float g[8];
        const double decay = value ("decay");
        for (int k = 0; k < 8; ++k)
        {
            len[k] = jlimit (16, (int) (sr * 0.34), (int) (baseLen[k] * size * sr / 44100.0));
            g[k] = freeze ? 1.0f : (float) std::pow (10.0, -3.0 * len[k] / (decay * sr));
        }
        const float dampC = freeze ? 0.0f : value ("damping") * 0.01f * 0.85f;
        const float diff = value ("diffusion") * 0.01f * 0.7f;
        const float modAmt = value ("mod") * 0.01f * (float) (sr * 0.0008);
        const double modInc = 0.7 / sr;
        const int preD = (int) (value ("predelay") * 0.001 * sr);
        const int routing = (int) value ("routing");
        const float blend = value ("blend") * 0.01f;
        const float mix = value ("mix") * 0.01f, width = value ("width") * 0.01f;
        for (auto& f : lc) f.set (Biquad::highpass, sr, value ("lowcut"), 0.707);
        for (auto& f : hc) f.set (Biquad::lowpass, sr, value ("highcut"), 0.707);

        // in pieces of up to cb samples: first the predelayed input of the whole piece, then the convolution of it
        // (zero latency, so the space starts exactly with the predelay, also below 11 ms), then the tank sample by sample.
        // Before, the space ran in its own 512-sample blocks and arrived up to 11.6 ms late when the predelay was short.
        for (int start = 0; start < n; start += cb)
        {
        const int cnt = jmin (cb, n - start);
        for (int j = 0; j < cnt; ++j)
        {
            const int i = start + j;
            const float x = 0.5f * (b.getSample (0, i) + (ch > 1 ? b.getSample (1, i) : b.getSample (0, i)));
            pre.push (freeze ? 0.0f : x);
            const float d = pre.tapInt (preD + 1);
            convIn.setSample (0, j, d);
            convIn.setSample (1, j, d);
        }
        if (routing != 3)
        {
            for (int c = 0; c < 2; ++c) convOut.copyFrom (c, 0, convIn, c, 0, cnt);
            juce::dsp::AudioBlock<float> blk (convOut);
            auto part = blk.getSubBlock (0, (size_t) cnt);
            conv.process (juce::dsp::ProcessContextReplacing<float> (part));
        }
        else convOut.clear();
        for (int j = 0; j < cnt; ++j)
        {
            const int i = start + j;
            modPh += modInc; if (modPh >= 1.0) modPh -= 1.0;
            float in = convIn.getSample (0, j);
            const float spaceL = convOut.getSample (0, j), spaceR = convOut.getSample (1, j);
            if (routing == 1) in = 0.5f * (spaceL + spaceR);
            // input diffusion
            for (int k = 0; k < 4; ++k)
            {
                static const int apLen[4] { 142, 107, 379, 277 };
                const int al = (int) (apLen[k] * sr / 44100.0);
                const float dl = inAp[k].tapInt (al);
                const float v = in + dl * diff;
                inAp[k].push (v);
                in = dl - v * diff;
            }
            // FDN with a Householder matrix
            float o[8], sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float m = (k & 1 ? 1.0f : -1.0f) * (float) std::sin (twoPi * (modPh + k * 0.125));
                o[k] = lines[k].tap ((float) len[k] + modAmt * m);
                damp[k] = o[k] + (damp[k] - o[k]) * dampC;
                o[k] = damp[k] * g[k];
                sum += o[k];
            }
            sum *= 0.25f;
            float tankL = 0.0f, tankR = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                lines[k].push (o[k] - sum + (freeze ? 0.0f : in * 0.35f));
                (k & 1 ? tankR : tankL) += o[k];
            }
            tankL *= 0.5f; tankR *= 0.5f;
            float wl, wr;
            switch (routing)
            {
                case 1:  wl = tankL; wr = tankR; break;
                case 2:  wl = spaceL; wr = spaceR; break;
                case 3:  wl = tankL; wr = tankR; break;
                default: wl = tankL * (1.0f - blend) + spaceL * blend; wr = tankR * (1.0f - blend) + spaceR * blend; break;
            }
            wl = hc[0].process (lc[0].process (wl, 0), 0);
            wr = hc[1].process (lc[1].process (wr, 1), 1);
            const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * width;
            wl = m + s; wr = m - s;
            const float xl = b.getSample (0, i);
            b.setSample (0, i, xl * (1.0f - mix) + wl * mix);
            if (ch > 1) b.setSample (1, i, b.getSample (1, i) * (1.0f - mix) + wr * mix);
        }
        }
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeDecayDisplay (*this, [this]
        {
            dsp2::DecayView v;
            v.predelay = value ("predelay") * 0.001;
            v.decay = value ("decay");
            v.damping = value ("damping") * 0.01f;
            v.early = (int) value ("space") <= 4 ? 0.6f : 0.0f;
            v.mix = value ("mix") * 0.01f;
            v.freeze = value ("freeze") > 0.5f;
            v.caption = String::fromUTF8 ("caída ") + String (v.decay, 1) + " s   predelay " + String (value ("predelay"), 0) + " ms";
            return v;
        });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Reverb", { { "space", "Espacio", "El lugar que se simula con convolución: de un cuarto a una cueva."_u },
                          { "decay", "Caída"_u, "Cuánto dura la cola del tanque algorítmico."_u }, { "size", "Tamaño"_u, "Qué tan grande suena."_u },
                          { "predelay", "Predelay", "Espera antes de que llegue la reverb: separa la voz del espacio."_u },
                          { "blend", "Tanque / espacio", "Mezcla las dos reverbs."_u }, { "mix", "Mezcla", "Cuánta reverb se oye."_u } } },
            { "Color", { { "damping", "Amortiguar", "Los agudos se apagan antes: más cálido."_u }, { "diffusion", "Difusión"_u, "Más alta, menos ecos separados."_u },
                         { "mod", "Movimiento", "Una ondulación suave en la cola."_u }, { "lowcut", "Graves", "Quita graves de la reverb."_u },
                         { "highcut", "Agudos", "Quita agudos de la reverb."_u }, { "width", "Ancho", ""_u } }, true },
            { "Ruta", { { "routing", "Ruta", "En serie el espacio alimenta al tanque."_u }, { "irdecay", "Largo", "Estira o encoge el espacio de convolución."_u },
                        { "freeze", "Congelar", "Sostiene la cola para siempre."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine lines[8], inAp[4], pre;
    float damp[8] {};
    double modPh = 0.0;
    Biquad lc[2], hc[2];
    static constexpr int cb = 512;
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { cb } };
    AudioBuffer<float> convIn, convOut;
    String loadedKey;
};

// ===================================================================== Cuerpo resonante
class ResonantBody : public FxBase
{
public:
    ResonantBody() : FxBase ("Cuerpo resonante", "Reverb y espacio", layout(), 6.0, false, true) { wantsMidiAsEffect = true; }
    static StringArray types() { return { "Viga", "Marimba", "Cuerda", "Membrana", "Placa", "Tubo abierto", "Tubo cerrado" }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "type", "Cuerpo", types(), 1);
        P::hz (l, "freq", "Frecuencia", 30.0f, 2000.0f, 220.0f);
        P::flt (l, "fine", "Ajuste fino", P::range (-50.0f, 50.0f), 0.0f, " cents", 0);
        P::toggle (l, "midi", "Seguir notas MIDI", false);
        P::flt (l, "decay", "Caída"_u, P::range (0.05f, 10.0f, 1.0f), 1.0f, " s", 2);
        P::pct (l, "material", "Material (amortigua agudos)", 0.0f, 100.0f, 40.0f);
        P::pct (l, "hit", "Punto de golpe", 1.0f, 99.0f, 30.0f);
        P::pct (l, "listen", "Punto de escucha", 1.0f, 99.0f, 60.0f);
        P::pct (l, "spread", "Estéreo"_u, 0.0f, 100.0f, 50.0f);
        P::hz (l, "inlp", "Filtro de entrada", 200.0f, 20000.0f, 8000.0f);
        P::db (l, "gain", "Ganancia", -24.0f, 24.0f, 6.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    static constexpr int maxModes = 24;
    void reset() override { for (auto& m : modes) m.reset(); inF.reset(); midiHz = 0.0f; }
    void process (AudioBuffer<float>& b, MidiBuffer& midi) override
    {
        if (value ("midi") > 0.5f)
            for (const auto ev : midi)
                if (ev.getMessage().isNoteOn()) midiHz = (float) MidiMessage::getMidiNoteInHertz (ev.getMessage().getNoteNumber());
        const int type = (int) value ("type");
        double f0 = (value ("midi") > 0.5f && midiHz > 0.0f ? midiHz : value ("freq")) * std::pow (2.0, value ("fine") / 1200.0);
        const double decay = value ("decay"), material = value ("material") * 0.01;
        const double hit = value ("hit") * 0.01, lis = value ("listen") * 0.01, spread = value ("spread") * 0.01;
        int count = 0;
        for (int k = 0; k < maxModes; ++k)
        {
            const double ratio = modeRatio (type, k);
            if (ratio <= 0.0) break;
            const double f = f0 * ratio;
            if (f > sr * 0.45) break;
            // higher modes die sooner, more so with a soft material
            const double t = decay / (1.0 + material * 6.0 * (ratio - 1.0));
            const double q = jlimit (1.0, 3000.0, MathConstants<double>::pi * f * t / 6.9078);
            modes[k].set (Biquad::bandpass, sr, f, q);
            // how much each mode is excited (hit point) and heard (listening points, left and right a bit apart)
            const double ex = std::sin (MathConstants<double>::pi * (k + 1) * hit);
            ampL[k] = (float) (ex * std::sin (MathConstants<double>::pi * (k + 1) * jlimit (0.01, 0.99, lis - spread * 0.2)));
            ampR[k] = (float) (ex * std::sin (MathConstants<double>::pi * (k + 1) * jlimit (0.01, 0.99, lis + spread * 0.2)));
            if (type == 2 || type == 5 || type == 6) { ampL[k] /= (float) (k + 1) * 0.5f + 0.5f; ampR[k] /= (float) (k + 1) * 0.5f + 0.5f; }
            // a soft material also rings its upper modes quieter, not only shorter
            // (a band-pass rings louder the wider it is, so a short mode is scaled back to the energy of a long one)
            const float soft = (float) (std::pow (1.0 - 0.9 * material, (double) k) * std::sqrt (t / decay) * ratio);   // hard: the upper modes ring bright
            ampL[k] *= soft; ampR[k] *= soft;
            ++count;
        }
        numModes = count;
        inF.set (Biquad::lowpass, sr, value ("inlp"), 0.707);
        const float gain = toGain (value ("gain")) * 4.0f / std::sqrt ((float) jmax (1, count)), mix = value ("mix") * 0.01f;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float x = inF.process (0.5f * (b.getSample (0, i) + (ch > 1 ? b.getSample (1, i) : b.getSample (0, i))), 0);
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < numModes; ++k)
            {
                const float y = modes[k].process (x, 0);
                l += y * ampL[k]; r += y * ampR[k];
            }
            l = std::tanh (l * gain); r = std::tanh (r * gain);
            b.setSample (0, i, b.getSample (0, i) * (1.0f - mix) + l * mix);
            if (ch > 1) b.setSample (1, i, b.getSample (1, i) * (1.0f - mix) + r * mix);
        }
    }
    static double modeRatio (int type, int k)
    {
        static const double beam[] { 1, 2.756, 5.404, 8.933, 13.345, 18.638, 24.81, 31.86 };
        static const double marimba[] { 1, 3.93, 9.24, 15.6, 22.9, 31.0 };
        static const double membrane[] { 1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.6, 3.652, 4.06, 4.154, 4.601, 4.832 };
        static const double plate[] { 1, 2.5, 4, 5, 6.5, 8.5, 9, 10, 12.5, 13, 16.25, 17 };
        switch (type)
        {
            case 0: return k < 8 ? beam[k] : 0.0;
            case 1: return k < 6 ? marimba[k] : 0.0;
            case 2: return k + 1.0;
            case 3: return k < 14 ? membrane[k] : 0.0;
            case 4: return k < 12 ? plate[k] : 0.0;
            case 5: return k + 1.0;
            default: return 2.0 * k + 1.0;
        }
    }
    String displayText() const override { return String (numModes) + " modos"; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Cuerpo", { { "type", "Cuerpo", "El objeto que resuena: una viga, una marimba, una cuerda, un parche, una placa o un tubo."_u },
                          { "freq", "Frecuencia", "La nota del objeto."_u }, { "decay", "Caída"_u, "Cuánto suena después del golpe."_u },
                          { "material", "Material", "Blando apaga los agudos rápido; duro los deja sonar."_u },
                          { "gain", "Ganancia", "Cuánto entra al cuerpo."_u }, { "mix", "Mezcla", "Cuánto cuerpo se oye."_u } } },
            { "Detalles", { { "fine", "Fino", ""_u }, { "midi", "MIDI", "Afina el cuerpo a la última nota que llega."_u },
                            { "hit", "Golpe", "Dónde se golpea: cambia qué armónicos suenan."_u }, { "listen", "Escucha", "Dónde se escucha."_u },
                            { "spread", "Estéreo"_u, "Separa los puntos de escucha izquierdo y derecho."_u }, { "inlp", "Entrada", "Suaviza lo que entra."_u } }, true },
        };
        return s;
    }
private:
    Biquad modes[maxModes], inF;
    float ampL[maxModes] {}, ampR[maxModes] {};
    int numModes = 0;
    float midiHz = 0.0f;
};

// ===================================================================== Resonadores (cinco cuerdas afinadas)
class Resonators : public FxBase
{
public:
    Resonators() : FxBase ("Resonadores", "Reverb y espacio", layout(), 6.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::integer (l, "note", "Nota base (MIDI)", 24, 96, 48);
        P::flt (l, "fine", "Ajuste fino", P::range (-50.0f, 50.0f), 0.0f, " cents", 0);
        static const int defs[] { 0, 7, 12, 16, 19 };
        for (int k = 2; k <= 5; ++k)
        {
            P::integer (l, "p" + String (k), "Resonador " + String (k) + ": intervalo", -24, 24, defs[k - 1]);
            P::db (l, "g" + String (k), "Resonador " + String (k) + ": volumen", -70.0f, 0.0f, -3.0f);
        }
        P::db (l, "g1", "Resonador 1: volumen", -70.0f, 0.0f, 0.0f);
        P::flt (l, "decay", "Caída"_u, P::range (0.05f, 15.0f, 1.5f), 2.0f, " s", 2);
        P::pct (l, "color", "Color", 0.0f, 100.0f, 50.0f);
        P::choice (l, "mode", "Modo", { "A (cuerda)", "B (brillante)" }, 0);
        P::hz (l, "inhp", "Entrada: corte de graves", 20.0f, 2000.0f, 50.0f);
        P::pct (l, "width", "Ancho", 0.0f, 100.0f, 60.0f);
        P::db (l, "gain", "Ganancia", -24.0f, 12.0f, -6.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate / 25.0) + 8); reset(); }
    void reset() override { for (auto& d : line) d.clear(); for (auto& s : lpState) s = 0.0f; inF.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const double root = value ("note") + value ("fine") / 100.0;
        double hz[5];
        float g[5], fbk[5], pan[5];
        const double decay = value ("decay");
        const bool bright = value ("mode") > 0.5f;
        const float color = value ("color") * 0.01f;
        const float lpc = bright ? 0.15f + color * 0.8f : 0.05f + color * 0.6f;   // loop lowpass: how fast the highs die
        const float width = value ("width") * 0.01f;
        static const float pans[] { 0.0f, -0.7f, 0.7f, -0.35f, 0.35f };
        // fixed parameter names: nothing is allocated on the audio thread
        static const char* pIds[5] { "p1", "p2", "p3", "p4", "p5" };
        static const char* gIds[5] { "g1", "g2", "g3", "g4", "g5" };
        for (int k = 0; k < 5; ++k)
        {
            const double st = k == 0 ? 0.0 : (double) value (pIds[k]);
            hz[k] = 440.0 * std::pow (2.0, (root + st - 69.0) / 12.0);
            dl[k] = (float) jlimit (2.5, (double) line[k].mask - 4.0, sr / hz[k]);
            fbk[k] = (float) std::pow (10.0, -3.0 * (dl[k] / sr) / decay);
            g[k] = toGain (value (gIds[k]));
            pan[k] = pans[k] * width;
        }
        inF.set (Biquad::highpass, sr, value ("inhp"), 0.707);
        const float gain = toGain (value ("gain")), mix = value ("mix") * 0.01f;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float x = inF.process (0.5f * (b.getSample (0, i) + (ch > 1 ? b.getSample (1, i) : b.getSample (0, i))), 0) * gain;
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < 5; ++k)
            {
                if (g[k] <= 0.0f) { line[k].push (0.0f); continue; }
                // Karplus-Strong loop: delay of one period, a gentle lowpass and the decay gain
                const float y = line[k].tap (dl[k]);
                lpState[k] += (y - lpState[k]) * (1.0f - lpc * 0.9f);
                const float fbv = (bright ? y * 0.5f + lpState[k] * 0.5f : lpState[k]) * fbk[k];
                line[k].push (x + fbv);
                const float o = y * g[k];
                l += o * (1.0f - jmax (0.0f, pan[k]));
                r += o * (1.0f + jmin (0.0f, pan[k]));
            }
            l = std::tanh (l * 0.5f); r = std::tanh (r * 0.5f);
            b.setSample (0, i, b.getSample (0, i) * (1.0f - mix) + l * mix);
            if (ch > 1) b.setSample (1, i, b.getSample (1, i) * (1.0f - mix) + r * mix);
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Resonadores", { { "note", "Nota", "La nota del primer resonador."_u }, { "decay", "Caída"_u, "Cuánto vibran después de excitarse."_u },
                               { "color", "Color", "Oscuro o brillante."_u }, { "mode", "Modo", "A suena a cuerda, B más metálico."_u },
                               { "gain", "Ganancia", "Cuánto entra."_u }, { "mix", "Mezcla", "Cuánto resonador se oye."_u } } },
            { "Acorde", { { "p2", "II", "Intervalo en semitonos del resonador 2."_u }, { "p3", "III", ""_u }, { "p4", "IV", ""_u }, { "p5", "V", ""_u },
                          { "g1", "Vol I", ""_u }, { "g2", "Vol II", ""_u }, { "g3", "Vol III", ""_u }, { "g4", "Vol IV", ""_u }, { "g5", "Vol V", ""_u } }, true },
            { "Más"_u, { { "fine", "Fino", ""_u }, { "inhp", "Entrada", "Quita graves antes de los resonadores."_u }, { "width", "Ancho", ""_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine line[5];
    float lpState[5] {}, dl[5] {};
    Biquad inF;
};
}

void registerReverbs (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Reverb y espacio", detail, Kind::effect, std::move (make), keys }); };
    add ("reverb-hibrida", "Reverb híbrida"_u, "Un espacio real por convolución mezclado con una reverb algorítmica."_u, [] { return std::make_unique<HybridReverb>(); },
         { "space", "decay", "size", "predelay", "blend", "mix" });
    add ("cuerpo", "Cuerpo resonante", "Hace sonar el audio dentro de una marimba, un tubo, una cuerda o un parche."_u, [] { return std::make_unique<ResonantBody>(); },
         { "type", "freq", "decay", "material", "hit", "mix" });
    add ("resonadores", "Resonadores", "Cinco cuerdas afinadas que vibran con lo que entra: convierte ruido en acordes."_u, [] { return std::make_unique<Resonators>(); },
         { "note", "decay", "color", "p2", "p3", "mix" });
}
}

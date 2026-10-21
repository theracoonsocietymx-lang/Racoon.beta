// Distorsión y color: Amplificador, Gabinete, Válvula dinámica, Erosión, Overdrive, Pedal, Reductor, Vinilo
#include "AllEffects.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float asym (float x, float bias) { return std::tanh (x + bias) - std::tanh (bias); }

// ===================================================================== Amplificador
class Amp : public FxBase
{
public:
    Amp() : FxBase ("Amplificador", "Distorsión y color"_u, layout()) {}
    static StringArray models() { return { "Limpio", "Boost", "Blues", "Rock", "Lead", "Pesado", "Bajo" }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "model", "Modelo", models(), 3);
        P::pct (l, "gain", "Ganancia", 0.0f, 100.0f, 40.0f);
        P::flt (l, "bass", "Graves", P::range (0.0f, 10.0f), 5.0f, "", 1);
        P::flt (l, "middle", "Medios", P::range (0.0f, 10.0f), 5.0f, "", 1);
        P::flt (l, "treble", "Agudos", P::range (0.0f, 10.0f), 5.0f, "", 1);
        P::flt (l, "presence", "Presencia", P::range (0.0f, 10.0f), 5.0f, "", 1);
        P::pct (l, "volume", "Volumen", 0.0f, 100.0f, 50.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    // up to +56 dB into the preamp: 4x oversampling plus ADAA on both tube stages keeps the fizz out of the highs
    void prepare (double rate, int block) override { os.prepare (block, 2); for (auto& d : dc) d.prepare (rate * os.factor()); reset(); }
    void reset() override { os.reset(); for (auto* f : { &inHp, &bassF, &midF, &trebF, &presF, &lp1, &lp2 }) f->reset(); for (auto& d : dc) d.reset();
                            for (auto& a : preStage) a.reset(); for (auto& a : powerStage) a.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        // per model: input gain range (dB), bias, low cut, mid center, top roll-off
        struct M { float gmin, gmax, bias, hp, mid, top, power; };
        static const M m[] {
            { 0, 18, 0.00f, 70, 600, 9000, 0.3f },  { 6, 30, 0.05f, 90, 700, 8000, 0.5f },  { 6, 34, 0.15f, 80, 650, 6500, 0.6f },
            { 12, 42, 0.10f, 100, 750, 6000, 0.7f }, { 18, 50, 0.20f, 110, 800, 5500, 0.8f }, { 22, 56, 0.25f, 130, 500, 5000, 0.9f },
            { 0, 30, 0.05f, 35, 400, 4500, 0.6f } };
        const auto& md = m[jlimit (0, 6, (int) value ("model"))];
        const double r2 = sr * os.factor();
        const float pre = toGain (md.gmin + (md.gmax - md.gmin) * value ("gain") * 0.01f);
        inHp.set (Biquad::highpass, r2, md.hp, 0.707);
        bassF.set (Biquad::lowshelf, r2, 120.0, 0.7, (value ("bass") - 5.0f) * 2.4f);
        midF.set (Biquad::peak, r2, md.mid, 0.8, (value ("middle") - 5.0f) * 2.4f);
        trebF.set (Biquad::highshelf, r2, 2500.0, 0.7, (value ("treble") - 5.0f) * 2.4f);
        presF.set (Biquad::peak, r2, 4000.0, 1.0, (value ("presence") - 5.0f) * 1.6f);
        lp1.set (Biquad::lowpass, r2, md.top, 0.6);
        lp2.set (Biquad::lowpass, r2, md.top * 1.6, 0.7);
        const float vol = 0.3f + value ("volume") * 0.01f * 3.0f * md.power, out = toGain (value ("output")), mix = value ("mix") * 0.01f;
        // make up so that the knob mostly changes color, not loudness
        const float norm = 1.0f / std::tanh (vol);
        os.process (b, [&] (float x, int c)
        {
            float y = inHp.process (x, c) * pre;
            y = preStage[c].process (y, md.bias);                     // preamp stage (asymmetric tube, ADAA)
            y = trebF.process (midF.process (bassF.process (y, c), c), c);   // tone stack
            y = presF.process (y, c);
            y = powerStage[c].process (y * vol) * norm * 0.5f;        // power stage (ADAA)
            y = lp2.process (lp1.process (y, c), c);
            y = dc[c].process (y);
            return (x * (1.0f - mix) + y * mix) * out;
        });
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            static const float gmin[] { 0, 6, 6, 12, 18, 22, 0 }, gmax[] { 18, 30, 34, 42, 50, 56, 30 }, bias[] { 0.0f, 0.05f, 0.15f, 0.10f, 0.20f, 0.25f, 0.05f };
            const int m = jlimit (0, 6, (int) value ("model"));
            const float pre = toGain (gmin[m] + (gmax[m] - gmin[m]) * value ("gain") * 0.01f);
            return asym (x * pre * 0.25f, bias[m]);
        }, [this] { return models()[jlimit (0, 6, (int) value ("model"))] + "   " + String (value ("gain"), 0) + " %   4x + ADAA"; });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Amplificador", { { "model", "Modelo", "El carácter del amplificador: de limpio a muy distorsionado. Bajo es para bajo eléctrico."_u },
                                { "gain", "Ganancia", "Cuánta distorsión."_u }, { "volume", "Volumen", "Empuja la etapa de potencia: más saturación y compresión."_u },
                                { "output", "Salida", "Volumen final." } } },
            { "Tono", { { "bass", "Graves", "Cuerpo." }, { "middle", "Medios", "Presencia en la mezcla." }, { "treble", "Agudos", "Brillo." },
                        { "presence", "Presencia", "Mordida en los agudos altos." }, { "mix", "Mezcla", "Mezcla con la señal limpia."_u } } },
        };
        return s;
    }
private:
    dsp2::Oversampler2 os;
    Biquad inHp, bassF, midF, trebF, presF, lp1, lp2;
    racoon::dsp::DCBlock dc[2];
    sdsp::AdaaAsymTanh preStage[2];
    sdsp::Adaa1<sdsp::curve::Tanh> powerStage[2];
};

// ===================================================================== Gabinete
class Cabinet : public FxBase
{
public:
    Cabinet() : FxBase ("Gabinete", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "speaker", "Bocinas", { "1x12", "2x12", "4x12", "4x10 bajo", "1x8 chico" }, 2);
        P::choice (l, "mic", "Micrófono"_u, { "Dinámico"_u, "Condensador" }, 0);
        P::choice (l, "pos", "Posición del micrófono"_u, { "Centro", "Borde", "Lejos" }, 0);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double rate, int) override { room.prepare ((int) (rate * 0.01)); reset(); }
    void reset() override { for (auto& f : f_) f.reset(); room.clear(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        struct S { float hp, reso, resoDb, notch, top, pk, pkDb; };
        static const S sp[] { { 85, 110, 4, 1200, 5200, 2600, 3 }, { 75, 100, 5, 1000, 5000, 2400, 3 }, { 65, 95, 6, 900, 4600, 2200, 4 },
                              { 45, 80, 5, 700, 4000, 1600, 3 }, { 140, 180, 3, 1500, 4800, 2800, 4 } };
        const auto& s = sp[jlimit (0, 4, (int) value ("speaker"))];
        const int pos = (int) value ("pos");
        const bool cond = value ("mic") > 0.5f;
        const float top = s.top * (pos == 0 ? 1.15f : pos == 1 ? 0.75f : 0.85f) * (cond ? 1.25f : 1.0f);
        f_[0].set (Biquad::highpass, sr, s.hp, 0.8);
        f_[1].set (Biquad::peak, sr, s.reso, 1.4, s.resoDb);
        f_[2].set (Biquad::peak, sr, s.notch, 1.2, -4.0);
        f_[3].set (Biquad::peak, sr, s.pk, 1.5, s.pkDb + (pos == 0 ? 2.0f : 0.0f) + (cond ? 1.5f : 0.0f));
        f_[4].set (Biquad::lowpass, sr, top, 0.9);
        f_[5].set (Biquad::lowpass, sr, top * 1.2, 0.6);
        f_[6].set (Biquad::highshelf, sr, 8000.0, 0.7, cond ? -6.0f : -14.0f);
        const float out = toGain (value ("output")), mix = value ("mix") * 0.01f, far = pos == 2 ? 0.35f : 0.0f;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int roomD = (int) (sr * 0.0047);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float y = x;
                for (auto& f : f_) y = f.process (y, c);
                if (c == 0) room.push (y);
                if (far > 0.0f) y = y * 0.8f + room.tapInt (roomD + c * 37) * far;   // a near reflection: the mic further back
                b.setSample (c, i, (x * (1.0f - mix) + y * mix * 1.6f) * out);
            }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = { { "Gabinete", { { "speaker", "Bocinas", "El tamaño de la caja: más bocinas, más grave y más cuerpo."_u },
                                       { "mic", "Micrófono"_u, "Dinámico es más medio; condensador más abierto."_u },
                                       { "pos", "Posición"_u, "Al centro es brillante, en el borde más oscuro, lejos con algo de cuarto."_u },
                                       { "mix", "Mezcla", "Mezcla con la señal sin gabinete."_u }, { "output", "Salida", "Volumen final." } } } };
        return s;
    }
private:
    Biquad f_[7];
    racoon::dsp::DelayLine room;
};

// ===================================================================== Válvula dinámica
class DynTube : public FxBase
{
public:
    DynTube() : FxBase ("Válvula dinámica"_u, "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "tube", "Tipo de válvula"_u, { "A (limpia)", "B (cálida)"_u, "C (sucia)" }, 1);
        P::db (l, "drive", "Drive", 0.0f, 36.0f, 6.0f);
        P::pct (l, "bias", "Bias", 0.0f, 100.0f, 30.0f);
        P::flt (l, "env", "Envolvente", P::range (-300.0f, 300.0f), 0.0f, "%", 0);
        P::ms (l, "attack", "Ataque", 0.1f, 100.0f, 10.0f, 10.0f);
        P::ms (l, "release", "Relajación"_u, 1.0f, 3000.0f, 200.0f, 200.0f);
        P::flt (l, "tone", "Tono", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::db (l, "output", "Salida", -36.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double rate, int block) override { os.prepare (block, 2); for (auto& d : dc) d.prepare (rate * os.factor()); reset(); }   // 4x and ADAA: tube C at 36 dB is almost a square wave
    void reset() override { os.reset(); fol.env = 0.0f; for (auto& d : dc) d.reset(); tilt[0].reset(); tilt[1].reset(); for (auto& t : tube) t.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int type = (int) value ("tube");
        const float drive = toGain (value ("drive")), bias0 = value ("bias") * 0.01f * (type == 0 ? 0.3f : type == 1 ? 0.6f : 1.0f);
        const float envAmt = value ("env") * 0.01f, out = toGain (value ("output")), mix = value ("mix") * 0.01f;
        fol.set (sr * os.factor(), value ("attack"), value ("release"));
        const float t = value ("tone") * 0.01f;
        tilt[0].set (Biquad::highshelf, sr * os.factor(), 1500.0, 0.6, t * 9.0f);
        tilt[1].set (Biquad::lowshelf, sr * os.factor(), 300.0, 0.6, -t * 6.0f);
        const float hardness = type == 0 ? 1.0f : type == 1 ? 1.4f : 2.2f;
        // sample by sample with both channels together: one follower for the pair, so left and right share the same bias
        // (before, the right channel got the bias the left one ended the block with)
        os.processFrames (b, [&] (float* f, int ch)
        {
            float lvl = 0.0f;
            for (int c = 0; c < ch; ++c) lvl = jmax (lvl, std::abs (f[c]));
            fol.process (lvl);
            const float bias = jlimit (-1.5f, 1.5f, bias0 + envAmt * fol.env);
            const float k = drive * hardness;
            const float norm = 1.0f / jmax (0.3f, hardness * (1.0f - std::tanh (bias) * std::tanh (bias)) * drive);
            for (int c = 0; c < ch; ++c)
            {
                const float x = f[c];
                float y = tube[c].process (x * k, bias) * norm;   // ADAA: the bias moves, the aliasing does not come back
                y = tilt[1].process (tilt[0].process (y, c), c);
                y = dc[c].process (y);
                f[c] = (x * (1.0f - mix) + y * mix) * out;
            }
        });
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            const int type = (int) value ("tube");
            const float hardness = type == 0 ? 1.0f : type == 1 ? 1.4f : 2.2f;
            const float bias = value ("bias") * 0.01f * (type == 0 ? 0.3f : type == 1 ? 0.6f : 1.0f);
            return asym (x * toGain (value ("drive")) * hardness, bias);
        }, [this] { return String ("Drive ") + String (value ("drive"), 1) + " dB   4x + ADAA"; });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Válvula"_u, { { "tube", "Tipo", "A casi no distorsiona, C se ensucia rápido."_u }, { "drive", "Drive", "Cuánto empuja la válvula."_u },
                             { "bias", "Bias", "Más bias, más armónicos pares: suena más cálido."_u }, { "tone", "Tono", "Más oscuro o más brillante."_u },
                             { "output", "Salida", "Volumen final." } } },
            { "Envolvente", { { "env", "Envolvente", "El bias sigue al volumen: los golpes fuertes se ensucian más (o menos, en negativo)."_u },
                              { "attack", "Ataque", "Qué tan rápido sigue a los golpes."_u }, { "release", "Relajación"_u, "Qué tan rápido vuelve."_u },
                              { "mix", "Mezcla", "Mezcla con la señal limpia."_u } }, true },
        };
        return s;
    }
private:
    dsp2::Oversampler2 os;
    racoon::dsp::Follower fol;
    racoon::dsp::DCBlock dc[2];
    Biquad tilt[2];
    sdsp::AdaaAsymTanh tube[2];
};

// ===================================================================== Erosión
class Erosion : public FxBase
{
public:
    Erosion() : FxBase ("Erosión"_u, "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Ruido", "Ruido ancho", "Seno" }, 0);
        P::hz (l, "freq", "Frecuencia", 30.0f, 18000.0f, 1000.0f);
        P::flt (l, "width", "Ancho", P::range (0.1f, 10.0f, 1.0f), 1.0f, "", 2);
        P::pct (l, "amount", "Cantidad", 0.0f, 100.0f, 30.0f);
        return l;
    }
    void reset() override { for (auto& f : bp) f.reset(); phase = 0.0; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int mode = (int) value ("mode");
        const double f = value ("freq");
        const float amount = value ("amount") * 0.01f;
        for (auto& x : bp) x.set (Biquad::bandpass, sr, f, 1.0 / value ("width"));
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double inc = f / sr;
        for (int i = 0; i < n; ++i)
        {
            float mod[2];
            if (mode == 2) { mod[0] = mod[1] = (float) std::sin (MathConstants<double>::twoPi * phase); phase += inc; if (phase >= 1.0) phase -= 1.0; }
            else
            {
                const float nz = rng.nextBipolar();
                mod[0] = jlimit (-3.0f, 3.0f, bp[0].process (nz, 0) * 3.0f);
                mod[1] = mode == 1 ? jlimit (-3.0f, 3.0f, bp[1].process (rng.nextBipolar(), 1) * 3.0f) : mod[0];
            }
            for (int c = 0; c < ch; ++c)
            {
                // ring modulation of the signal with a filtered modulator: a gritty, digital roughness
                const float x = b.getSample (c, i);
                b.setSample (c, i, x + x * mod[c] * amount * 2.0f);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = { { "Erosión"_u, { { "mode", "Modo", "Ruido raspa, ruido ancho separa los lados, seno da un timbre metálico."_u },
                                        { "freq", "Frecuencia", "Dónde se nota el desgaste."_u }, { "width", "Ancho", "Qué tan amplia es la zona afectada."_u },
                                        { "amount", "Cantidad", "Cuánto se degrada el sonido."_u } } } };
        return s;
    }
private:
    Biquad bp[2];
    double phase = 0.0;
    racoon::dsp::Rng rng;
};

// ===================================================================== Overdrive
class Overdrive : public FxBase
{
public:
    Overdrive() : FxBase ("Overdrive", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "freq", "Filtro: centro", 50.0f, 20000.0f, 800.0f);
        P::flt (l, "width", "Filtro: ancho (octavas)", P::range (0.5f, 9.0f), 6.0f, " oct", 1);
        P::pct (l, "drive", "Drive", 0.0f, 100.0f, 50.0f);
        P::pct (l, "tone", "Tono", 0.0f, 100.0f, 50.0f);
        P::pct (l, "dyn", "Dinámica"_u, 0.0f, 100.0f, 50.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double, int block) override { os.prepare (block); reset(); }
    void reset() override { os.reset(); for (auto* f : { &hp, &lp, &toneF }) f->reset(); for (auto& f : fin) f.env = 0.0f; for (auto& f : fout) f.env = 0.0f; for (auto& d : diode) d.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const double r2 = sr * 2.0, f = value ("freq"), half = value ("width") * 0.5;
        hp.set (Biquad::highpass, r2, f / std::pow (2.0, half), 0.6);
        lp.set (Biquad::lowpass, r2, jmin (r2 * 0.45, f * std::pow (2.0, half)), 0.6);
        toneF.set (Biquad::lowpass, r2, 800.0 * std::pow (20.0, value ("tone") * 0.01), 0.7);
        const float g = toGain (value ("drive") * 0.45f), dyn = value ("dyn") * 0.01f;
        const float out = toGain (value ("output")), mix = value ("mix") * 0.01f;
        for (auto& x : fin) x.set (r2, 5.0f, 80.0f);
        for (auto& x : fout) x.set (r2, 5.0f, 80.0f);
        os.process (b, [&] (float x, int c)
        {
            float y = lp.process (hp.process (x, c), c) * g;
            // diode pair: soft, slightly asymmetric
            y = diode[c].process (y);   // ADAA
            y = toneF.process (y, c);
            // dynamics: give back part of the input's level changes that the clipping squashed
            const float ei = fin[c].process (std::abs (x)), eo = fout[c].process (std::abs (y));
            y *= 1.0f + dyn * (jlimit (0.0f, 4.0f, ei * g / (eo + 1.0e-4f)) - 1.0f) * 0.25f;
            return (x * (1.0f - mix) + y * mix * 0.7f) * out;
        });
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            return (float) sdsp::curve::Diode::f (x * toGain (value ("drive") * 0.45f));
        }, [this] { return String ("Drive ") + String (value ("drive"), 0) + " %   2x + ADAA"; });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Overdrive", { { "drive", "Drive", "Cuánta distorsión."_u }, { "tone", "Tono", "Oscuro o brillante."_u },
                             { "dyn", "Dinámica"_u, "Más alto respeta la diferencia entre notas suaves y fuertes."_u },
                             { "mix", "Mezcla", "Mezcla con la señal limpia."_u }, { "output", "Salida", "Volumen final." } } },
            { "Filtro antes", { { "freq", "Centro", "Qué zona del sonido entra a la distorsión."_u },
                                { "width", "Ancho", "Qué tan amplia es esa zona. Estrecho suena a radio."_u } }, true },
        };
        return s;
    }
private:
    dsp2::Oversampler2 os;
    sdsp::Adaa1<sdsp::curve::Diode> diode[2];
    Biquad hp, lp, toneF;
    racoon::dsp::Follower fin[2], fout[2];
};

// ===================================================================== Pedal
class Pedal : public FxBase
{
public:
    Pedal() : FxBase ("Pedal", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "type", "Tipo", { "Overdrive", "Distorsión"_u, "Fuzz" }, 0);
        P::pct (l, "gain", "Ganancia", 0.0f, 100.0f, 50.0f);
        P::flt (l, "bass", "Graves", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::flt (l, "mid", "Medios", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::choice (l, "midfreq", "Frecuencia de medios", { "500 Hz", "1 kHz", "2 kHz" }, 1);
        P::flt (l, "treble", "Agudos", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::toggle (l, "sub", "Sub (refuerzo de graves)", false);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    // 4x: the fuzz is almost a square wave. The overdrive and the fuzz's tube also use ADAA
    void prepare (double rate, int block) override { os.prepare (block, 2); for (auto& d : dc) d.prepare (rate * os.factor()); reset(); }
    void reset() override { os.reset(); for (auto* f : { &pre, &bassF, &midF, &trebF, &subF, &post }) f->reset(); for (auto& d : dc) d.reset(); for (auto& t : tube) t.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const double r2 = sr * os.factor();
        const int type = (int) value ("type");
        static const double midHz[] { 500.0, 1000.0, 2000.0 };
        pre.set (Biquad::highpass, r2, type == 0 ? 500.0 : type == 1 ? 200.0 : 80.0, 0.6);
        bassF.set (Biquad::lowshelf, r2, 200.0, 0.7, value ("bass") * 0.12f);
        midF.set (Biquad::peak, r2, midHz[(int) value ("midfreq")], 0.9, value ("mid") * 0.12f);
        trebF.set (Biquad::highshelf, r2, 2500.0, 0.7, value ("treble") * 0.12f);
        subF.set (Biquad::lowshelf, r2, 90.0, 0.8, 9.0);
        post.set (Biquad::lowpass, r2, type == 2 ? 5000.0 : 7000.0, 0.7);
        const float gdb = type == 0 ? 6.0f + value ("gain") * 0.3f : type == 1 ? 12.0f + value ("gain") * 0.36f : 20.0f + value ("gain") * 0.4f;
        const float g = toGain (gdb), out = toGain (value ("output")), mix = value ("mix") * 0.01f;
        const bool sub = value ("sub") > 0.5f;
        const float lvl = type == 0 ? 0.6f : type == 1 ? 0.45f : 0.35f;
        os.process (b, [&] (float x, int c)
        {
            // the low end skips the pre-filter partly, so the overdrive keeps some body
            float y = (pre.process (x, c) + x * (type == 0 ? 0.15f : 0.3f)) * g;
            switch (type)
            {
                case 0: y = tube[c].process (y, 0.1f); break;
                case 1: y = y / std::pow (1.0f + std::pow (std::abs (y), 2.5f), 1.0f / 2.5f); break;
                default:
                {
                    // fuzz: asymmetric, almost square, with a dash of rectification
                    const float s = tube[c].process (y * 2.0f, 0.4f);
                    y = jlimit (-0.7f, 1.0f, s * 1.2f) + 0.15f * std::abs (s);
                    break;
                }
            }
            y = dc[c].process (y);
            y = trebF.process (midF.process (bassF.process (y, c), c), c);
            if (sub) y = subF.process (y, c);
            y = post.process (y, c) * lvl;
            return (x * (1.0f - mix) + y * mix) * out;
        });
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            const int type = (int) value ("type");
            const float gdb = type == 0 ? 6.0f + value ("gain") * 0.3f : type == 1 ? 12.0f + value ("gain") * 0.36f : 20.0f + value ("gain") * 0.4f;
            const float y = x * toGain (gdb) * 0.3f;
            if (type == 0) return std::tanh (y + 0.1f) - std::tanh (0.1f);
            if (type == 1) return y / std::pow (1.0f + std::pow (std::abs (y), 2.5f), 1.0f / 2.5f);
            const float sv = std::tanh (y * 2.0f + 0.4f) - std::tanh (0.4f);
            return jlimit (-0.7f, 1.0f, sv * 1.2f) + 0.15f * std::abs (sv);
        }, [this] { static const char* t[] { "Overdrive", "Distorsión", "Fuzz" }; return String::fromUTF8 (t[jlimit (0, 2, (int) value ("type"))]) + "   4x + ADAA"; });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Pedal", { { "type", "Tipo", "Overdrive es cálido, distorsión más dura, fuzz casi una onda cuadrada."_u },
                         { "gain", "Ganancia", "Cuánta distorsión."_u }, { "output", "Salida", "Volumen final." }, { "mix", "Mezcla", "Mezcla con la señal limpia."_u } } },
            { "Tono", { { "bass", "Graves", "Cuerpo." }, { "mid", "Medios", "Presencia." }, { "midfreq", "Frecuencia", "Dónde actúa Medios."_u },
                        { "treble", "Agudos", "Brillo." }, { "sub", "Sub", "Refuerza los graves más bajos."_u } } },
        };
        return s;
    }
private:
    dsp2::Oversampler2 os;
    Biquad pre, bassF, midF, trebF, subF, post;
    racoon::dsp::DCBlock dc[2];
    sdsp::AdaaAsymTanh tube[2];
};

// ===================================================================== Reductor (bits y frecuencia de muestreo)
class Redux : public FxBase
{
public:
    Redux() : FxBase ("Reductor", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "rate", "Frecuencia de muestreo", 50.0f, 48000.0f, 11025.0f);
        P::pct (l, "jitter", "Jitter", 0.0f, 100.0f, 0.0f);
        P::flt (l, "bits", "Bits", P::range (1.0f, 16.0f, 0.0f, 0.1f), 8.0f, "", 1);
        P::toggle (l, "bitson", "Reducir bits", true);
        P::pct (l, "shape", "Forma", 0.0f, 100.0f, 0.0f);
        P::flt (l, "dc", "Desplazamiento DC", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::toggle (l, "filter", "Suavizar escalones", false);
        P::flt (l, "filteroct", "Suavizado (octavas)", P::range (-2.0f, 2.0f), 0.0f, " oct", 1);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void reset() override { phase = 1.0; held[0] = held[1] = 0.0f; for (auto& f : lp) f.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const double target = value ("rate");
        const float jit = value ("jitter") * 0.01f, mix = value ("mix") * 0.01f;
        const bool bitsOn = value ("bitson") > 0.5f, filt = value ("filter") > 0.5f;
        const float levels = std::pow (2.0f, value ("bits") - 1.0f);
        const float shape = value ("shape") * 0.01f, dcs = value ("dc") * 0.01f / jmax (1.0f, levels);
        if (filt) for (auto& f : lp) f.set (Biquad::lowpass, sr, target * 0.5 * std::pow (2.0, value ("filteroct")), 0.707);
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            phase += target / sr;
            const bool take = phase >= 1.0;
            if (take) phase -= 1.0 + jit * rng.next01() * 0.9;   // jitter: the next sample comes at a slightly random moment
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                if (take) held[c] = x;
                float y = held[c];
                if (bitsOn)
                {
                    // shape bends the steps: finer near zero, coarser at the top
                    const float s = shape > 0.0f ? std::copysign (std::pow (std::abs (y), 1.0f / (1.0f + shape * 2.0f)), y) : y;
                    float q = std::floor ((s + dcs) * levels + 0.5f) / levels;
                    if (shape > 0.0f) q = std::copysign (std::pow (std::abs (q), 1.0f + shape * 2.0f), q);
                    y = q;
                }
                if (filt) y = lp[c].process (y, c);
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Reductor", { { "rate", "Muestreo", "Baja la frecuencia de muestreo: sonido de sampler viejo o videojuego."_u },
                            { "bits", "Bits", "Menos bits, más ruido y escalones."_u }, { "bitson", "Bits", "Prende o apaga la reducción de bits."_u },
                            { "mix", "Mezcla", "Mezcla con la señal original."_u } } },
            { "Detalles", { { "jitter", "Jitter", "Hace irregular el momento de cada muestra: más sucio."_u },
                            { "shape", "Forma", "Escalones finos cerca del silencio y gruesos en lo fuerte."_u },
                            { "dc", "DC", "Mueve los escalones: cambia el carácter del ruido."_u },
                            { "filter", "Suavizar", "Quita parte de los escalones con un filtro."_u }, { "filteroct", "Cuánto"_u, "Dónde corta ese filtro."_u } }, true },
        };
        return s;
    }
private:
    double phase = 1.0;
    float held[2] {};
    Biquad lp[2];
    racoon::dsp::Rng rng;
};

// ===================================================================== Vinilo
class Vinyl : public FxBase
{
public:
    Vinyl() : FxBase ("Vinilo", "Distorsión y color"_u, layout(), 0.0, false, false, false) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "tfreq", "Aguja: frecuencia", 50.0f, 18000.0f, 3000.0f);
        P::flt (l, "twidth", "Aguja: ancho", P::range (0.1f, 4.0f, 1.0f), 1.0f, "", 2);
        P::pct (l, "tdrive", "Aguja: distorsión"_u, 0.0f, 100.0f, 0.0f);
        P::hz (l, "pfreq", "Pellizco: frecuencia", 50.0f, 18000.0f, 1500.0f);
        P::flt (l, "pwidth", "Pellizco: ancho", P::range (0.1f, 4.0f, 1.0f), 1.0f, "", 2);
        P::pct (l, "pdrive", "Pellizco: cantidad", 0.0f, 100.0f, 0.0f);
        P::pct (l, "crackle", "Crujidos", 0.0f, 100.0f, 40.0f);
        P::pct (l, "density", "Densidad", 0.0f, 100.0f, 30.0f);
        P::pct (l, "hiss", "Ruido de superficie", 0.0f, 100.0f, 25.0f);
        P::pct (l, "wow", "Ondulación"_u, 0.0f, 100.0f, 10.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : wowLine) d.prepare ((int) (rate * 0.02)); reset(); }
    void reset() override
    {
        for (auto& f : tbp) f.reset();
        for (auto& f : pbp) f.reset();
        for (auto& f : clickF) f.reset();
        for (auto& f : hissF) f.reset();
        for (auto& f : popF) f.reset();
        for (auto& d : wowLine) d.clear();
        clickL = clickR = pop = 0.0f; wowPh = 0.0; pinchPh = 0.0;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        for (auto& f : tbp) f.set (Biquad::bandpass, sr, value ("tfreq"), 1.0 / value ("twidth"));
        for (auto& f : pbp) f.set (Biquad::bandpass, sr, value ("pfreq"), 1.0 / value ("pwidth"));
        clickF[0].set (Biquad::highpass, sr, 250.0, 0.7);
        clickF[1].set (Biquad::lowpass, sr, 9000.0, 0.7);
        for (int c = 0; c < 2; ++c) { hissF[c].set (Biquad::bandpass, sr, 3500.0, 0.35); popF[c].set (Biquad::lowpass, sr, 900.0, 0.9); }
        const float td = value ("tdrive") * 0.01f, pd = value ("pdrive") * 0.01f;
        const float crack = value ("crackle") * 0.01f, hiss = value ("hiss") * 0.01f, wow = value ("wow") * 0.01f;
        // density: from a couple of clicks per second to a dusty record with hundreds
        const float perSecond = 2.0f + std::pow (value ("density") * 0.01f, 2.0f) * 400.0f;
        const float clickProb = (float) (perSecond / sr), clickDecay = dsp2::coeffMs (sr, 0.12), popDecay = dsp2::coeffMs (sr, 4.0);
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double wowInc = 0.55 / sr, pinchInc = 3.0 / sr;
        for (int i = 0; i < n; ++i)
        {
            // crackle: sparse random clicks, every one with its own size and place in the stereo field,
            // and now and then a dull pop from a scratch
            float clickOut[2] {};
            if (crack > 0.0f)
            {
                if (rng.next01() < clickProb)
                {
                    const float size = std::pow (rng.next01(), 3.0f);          // most are tiny, a few are loud
                    const float a = rng.nextBipolar() > 0.0f ? 1.0f : -1.0f;
                    const float pan = rng.next01();
                    clickL += a * (0.08f + 0.92f * size) * std::sqrt (1.0f - pan);
                    clickR += a * (0.08f + 0.92f * size) * std::sqrt (pan);
                    if (rng.next01() < 0.04f) pop += a * (0.3f + 0.7f * rng.next01());
                }
                const float pl = popF[0].process (pop, 0), pr = popF[1].process (pop, 1);
                clickOut[0] = (clickF[1].process (clickF[0].process (clickL, 0), 0) + pl * 2.5f) * crack * 0.7f;
                clickOut[1] = (clickF[1].process (clickF[0].process (clickR, 1), 1) + pr * 2.5f) * crack * 0.7f;
                clickL *= clickDecay; clickR *= clickDecay; pop *= popDecay;
            }
            // surface noise: a soft band of hiss, different on each side
            float noise[2] {};
            if (hiss > 0.0f)
                for (int c = 0; c < 2; ++c) noise[c] = hissF[c].process (rng.nextBipolar(), c) * hiss * hiss * 0.09f + rng.nextBipolar() * hiss * 0.004f;
            wowPh += wowInc; if (wowPh >= 1.0) wowPh -= 1.0;
            pinchPh += pinchInc; if (pinchPh >= 1.0) pinchPh -= 1.0;
            const float wowD = 2.0f + wow * (float) (sr * 0.0025) * (0.5f + 0.5f * (float) std::sin (MathConstants<double>::twoPi * wowPh));
            for (int c = 0; c < ch; ++c)
            {
                float x = b.getSample (c, i);
                if (wow > 0.0f) { wowLine[c].push (x); x = wowLine[c].tap (wowD); }
                float y = x;
                // tracing: the needle misreads one band and distorts it
                if (td > 0.0f) { const float band = tbp[c].process (x, c); y += (std::tanh (band * (1.0f + td * 8.0f) + 0.2f) - std::tanh (0.2f) - band) * td; }
                // pinch: a band that moves out of phase between left and right
                if (pd > 0.0f)
                {
                    const float band = pbp[c].process (x, c);
                    const float sgn = c == 0 ? 1.0f : -1.0f;
                    y += band * pd * sgn * (float) std::sin (MathConstants<double>::twoPi * pinchPh) * 0.8f + std::tanh (band * 3.0f) * pd * 0.2f;
                }
                b.setSample (c, i, y + clickOut[c] + noise[c]);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Disco", { { "crackle", "Crujidos", "Cuánto crujen los clics del disco."_u }, { "density", "Densidad", "Cuántos clics por segundo."_u },
                         { "hiss", "Superficie", "El soplido constante del vinilo."_u }, { "wow", "Ondulación"_u, "El disco gira chueco: la afinación ondula."_u } } },
            { "Aguja", { { "tdrive", "Distorsión"_u, "La aguja lee mal una banda y la ensucia."_u }, { "tfreq", "Frecuencia", "Qué banda."_u },
                         { "twidth", "Ancho", "Qué tan amplia."_u } } },
            { "Pellizco", { { "pdrive", "Cantidad", "Una banda que se mueve entre izquierda y derecha."_u }, { "pfreq", "Frecuencia", "Qué banda."_u },
                            { "pwidth", "Ancho", "Qué tan amplia."_u } }, true },
        };
        return s;
    }
private:
    Biquad tbp[2], pbp[2], clickF[2], hissF[2], popF[2];
    racoon::dsp::DelayLine wowLine[2];
    float clickL = 0.0f, clickR = 0.0f, pop = 0.0f;
    double wowPh = 0.0, pinchPh = 0.0;
    racoon::dsp::Rng rng;
};
}

void registerDistortion (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Distorsión y color"_u, detail, Kind::effect, std::move (make), keys }); };
    add ("amplificador", "Amplificador", "Siete amplificadores de guitarra y bajo, de limpio a pesado."_u, [] { return std::make_unique<Amp>(); },
         { "model", "gain", "bass", "middle", "treble", "volume" });
    add ("gabinete", "Gabinete", "La caja de bocinas y el micrófono que van después del amplificador."_u, [] { return std::make_unique<Cabinet>(); },
         { "speaker", "mic", "pos", "mix" });
    add ("valvula", "Válvula dinámica"_u, "Saturación de válvula que reacciona al volumen."_u, [] { return std::make_unique<DynTube>(); },
         { "tube", "drive", "bias", "env", "tone", "output" });
    add ("erosion", "Erosión"_u, "Desgasta el sonido con ruido o un seno: textura digital."_u, [] { return std::make_unique<Erosion>(); },
         { "mode", "freq", "width", "amount" });
    add ("overdrive", "Overdrive", "Saturación cálida con un filtro antes: de cálido a sonido de radio."_u, [] { return std::make_unique<Overdrive>(); },
         { "drive", "tone", "freq", "dyn", "mix", "output" });
    add ("pedal", "Pedal", "Tres pedales de guitarra: overdrive, distorsión y fuzz."_u, [] { return std::make_unique<Pedal>(); },
         { "type", "gain", "bass", "mid", "treble", "output" });
    add ("reductor", "Reductor", "Menos bits y menos muestras: sonido lo-fi y de consola vieja."_u, [] { return std::make_unique<Redux>(); },
         { "rate", "bits", "jitter", "mix" });
    add ("vinilo", "Vinilo", "Crujidos, soplido y la distorsión de un tocadiscos."_u, [] { return std::make_unique<Vinyl>(); },
         { "crackle", "density", "hiss", "wow", "tdrive", "pdrive" });
}
}

// Modulación: Chorus y ensamble, Phaser y flanger, Paneo automático
#include "AllEffects.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }
constexpr double twoPi = MathConstants<double>::twoPi;

// ===================================================================== Chorus y ensamble
class Chorus : public FxBase
{
public:
    Chorus() : FxBase ("Chorus y ensamble", "Modulación"_u, layout(), 0.05) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Clásico"_u, "Ensamble", "Vibrato" }, 0);
        P::flt (l, "rate", "Velocidad", P::range (0.05f, 15.0f, 1.0f), 0.8f, " Hz", 2);
        P::pct (l, "amount", "Profundidad", 0.0f, 100.0f, 50.0f);
        P::pct (l, "feedback", "Realimentación"_u, 0.0f, 95.0f, 0.0f);
        P::toggle (l, "fbinv", "Invertir realimentación"_u, false);
        P::pct (l, "width", "Ancho", 0.0f, 200.0f, 100.0f);
        P::pct (l, "warmth", "Calidez", 0.0f, 100.0f, 0.0f);
        P::hz (l, "hp", "Corte de graves del efecto", 20.0f, 2000.0f, 20.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate * 0.05)); reset(); }
    void reset() override { for (auto& d : line) d.clear(); phase = 0.0; fb[0] = fb[1] = 0.0f; for (auto& f : hpF) f.reset(); for (auto& f : warmF) f.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int mode = (int) value ("mode");
        const double inc = value ("rate") / sr;
        const float amount = value ("amount") * 0.01f;
        const float fbk = value ("feedback") * 0.01f * (value ("fbinv") > 0.5f ? -1.0f : 1.0f);
        const float width = value ("width") * 0.01f, warm = value ("warmth") * 0.01f;
        const float mix = mode == 2 ? 1.0f : value ("mix") * 0.01f;
        for (auto& f : hpF) f.set (Biquad::highpass, sr, value ("hp"), 0.707);
        for (auto& f : warmF) f.set (Biquad::lowpass, sr, 18000.0 - warm * 13000.0, 0.707);
        // base delay and depth (ms): classic chorus, three-voice ensemble, pure vibrato
        const float base = mode == 0 ? 8.0f : mode == 1 ? 12.0f : 3.0f;
        const float depth = (mode == 2 ? 3.0f : 5.0f) * amount;
        const int voices = mode == 1 ? 3 : 1;
        const float ms = (float) (sr * 0.001);
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            float wet[2] {};
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                line[c].push (hpF[c].process (x + fb[c] * fbk, c));
                float sum = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    // the right side runs a quarter cycle apart (times the width) for a wide image
                    const double ph = phase + v / 3.0 + (c == 1 ? 0.25 * width : 0.0);
                    const float m = (float) std::sin (twoPi * ph);
                    sum += line[c].tap ((base + depth * (0.5f + 0.5f * m)) * ms);
                }
                sum /= std::sqrt ((float) voices);
                fb[c] = sum;
                if (warm > 0.0f) sum = warmF[c].process (std::tanh (sum * (1.0f + warm * 2.0f)) / (1.0f + warm * 0.8f), c);
                wet[c] = sum;
            }
            if (ch == 2 && width > 1.0f)
            {
                // above 100 %: widen the wet signal
                const float m = 0.5f * (wet[0] + wet[1]), s = 0.5f * (wet[0] - wet[1]) * width;
                wet[0] = m + s; wet[1] = m - s;
            }
            for (int c = 0; c < ch; ++c) b.setSample (c, i, b.getSample (c, i) * (1.0f - mix) + wet[c] * mix);
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Chorus", { { "mode", "Modo", "Clásico duplica la voz, ensamble suena a cuerdas, vibrato ondula la afinación."_u },
                          { "rate", "Velocidad", "Qué tan rápido se mueve."_u }, { "amount", "Profundidad", "Cuánto se mueve."_u },
                          { "width", "Ancho", "Separación entre izquierda y derecha."_u }, { "mix", "Mezcla", "Cuánto efecto se oye."_u } } },
            { "Color", { { "feedback", "Realimentación"_u, "Vuelve a pasar el efecto por sí mismo: más metálico."_u },
                         { "fbinv", "Invertir", "Cambia el timbre de la realimentación."_u },
                         { "warmth", "Calidez", "Satura y oscurece un poco, como un chorus analógico."_u },
                         { "hp", "Graves", "Los graves no pasan por el efecto: la base queda firme."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine line[2];
    double phase = 0.0;
    float fb[2] {};
    Biquad hpF[2], warmF[2];
};

// ===================================================================== Phaser y flanger
class PhaserFlanger : public FxBase
{
public:
    PhaserFlanger() : FxBase ("Phaser y flanger", "Modulación"_u, layout(), 0.1) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Phaser", "Flanger", "Doblador" }, 0);
        P::hz (l, "freq", "Frecuencia", 30.0f, 12000.0f, 800.0f);
        P::flt (l, "feedback", "Realimentación"_u, P::range (-95.0f, 95.0f), 30.0f, "%", 0);
        P::integer (l, "notches", "Muescas", 1, 12, 4);
        P::flt (l, "rate", "Velocidad", P::range (0.01f, 20.0f, 1.0f), 0.5f, " Hz", 2);
        P::toggle (l, "sync", "Al tempo", false);
        P::choice (l, "div", "División"_u, RacoonProcessor::divisionNames(), 1);
        P::pct (l, "amount", "Cantidad", 0.0f, 100.0f, 50.0f);
        P::flt (l, "spread", "Desfase estéreo"_u, P::range (0.0f, 180.0f), 90.0f, "°"_u, 0);
        P::choice (l, "shape", "Forma", dsp2::lfoShapeNames(), 0);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate * 0.05)); reset(); }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& c : ap) for (auto& s : c) s = {};
        phase = 0.0; fb[0] = fb[1] = 0.0f; lastPh = 0.0;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int mode = (int) value ("mode");
        const bool sync = value ("sync") > 0.5f;
        const int div = (int) value ("div");
        lockPhase (sync, div, phase);
        const double inc = lfoHz (sync, div, value ("rate")) / sr;
        const float amount = value ("amount") * 0.01f, fbk = value ("feedback") * 0.01f, mix = value ("mix") * 0.01f;
        const double spread = value ("spread") / 360.0;
        const int stages = (int) value ("notches") * 2, shape = (int) value ("shape");
        const float f0 = value ("freq");
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float ms = (float) (sr * 0.001);
        for (int i = 0; i < n; ++i)
        {
            const double prev = phase;
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            const bool newCycle = phase < prev;
            for (int c = 0; c < ch; ++c)
            {
                double ph = phase + (c == 1 ? spread : 0.0); ph -= std::floor (ph);
                const float m = dsp2::lfoShape (shape, ph, hold[c], from[c], rng, newCycle);   // -1..1
                const float x = b.getSample (c, i);
                float wet;
                if (mode == 0)
                {
                    // phaser: chain of first-order allpasses swept around the center frequency (up to 3 octaves each way)
                    const double fc = jlimit (20.0, sr * 0.45, (double) f0 * std::pow (2.0, m * amount * 3.0));
                    const float t = (float) std::tan (MathConstants<double>::pi * fc / sr);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float y = x + fb[c] * fbk;
                    for (int s = 0; s < stages; ++s)
                    {
                        auto& st = ap[c][s];
                        const float o = a * y + st.x1 - a * st.y1;
                        st.x1 = y; st.y1 = o; y = o;
                    }
                    fb[c] = y;
                    wet = y;
                }
                else
                {
                    // flanger: a short delay swept by the LFO; doubler: a longer one, gently moved
                    const float baseMs = mode == 1 ? (float) (1000.0 / jmax (20.0f, f0)) : 22.0f;
                    const float depthMs = mode == 1 ? baseMs * 0.9f * amount : 6.0f * amount;
                    const float d = jmax (0.05f, baseMs + depthMs * m) * ms;
                    line[c].push (x + fb[c] * fbk);
                    wet = line[c].tap (d);
                    fb[c] = wet;
                }
                b.setSample (c, i, x * (1.0f - mix) + wet * mix);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Phaser y flanger", { { "mode", "Modo", "Phaser barre muescas suaves, flanger es el sonido de jet, doblador simula dos tomas."_u },
                                    { "freq", "Frecuencia", "Alrededor de dónde se mueve."_u }, { "amount", "Cantidad", "Cuánto se mueve."_u },
                                    { "feedback", "Realimentación"_u, "Más intenso y resonante."_u }, { "mix", "Mezcla", "Cuánto efecto se oye."_u } } },
            { "Movimiento", { { "rate", "Velocidad", "Qué tan rápido se mueve."_u }, { "sync", "Al tempo", "Sigue el tempo de la canción."_u },
                              { "div", "División"_u, "Duración de un ciclo cuando va al tempo."_u }, { "shape", "Forma", "La figura del movimiento."_u },
                              { "spread", "Estéreo"_u, "Desfase entre izquierda y derecha."_u }, { "notches", "Muescas", "Cuántas muescas tiene el phaser."_u } }, true },
        };
        return s;
    }
private:
    struct AP { float x1 = 0.0f, y1 = 0.0f; };
    AP ap[2][24];
    racoon::dsp::DelayLine line[2];
    double phase = 0.0, lastPh = 0.0;
    float fb[2] {}, hold[2] {}, from[2] {};
    uint32 rng = 12345;
};

// ===================================================================== Paneo automático
class AutoPan : public FxBase
{
public:
    AutoPan() : FxBase ("Paneo automático"_u, "Modulación"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "amount", "Cantidad", 0.0f, 100.0f, 50.0f);
        P::flt (l, "rate", "Velocidad", P::range (0.01f, 40.0f, 1.5f), 1.0f, " Hz", 2);
        P::toggle (l, "sync", "Al tempo", true);
        P::choice (l, "div", "División"_u, RacoonProcessor::divisionNames(), 4);
        P::flt (l, "phase", "Fase entre lados", P::range (0.0f, 360.0f), 180.0f, "°"_u, 0);
        P::flt (l, "offset", "Inicio", P::range (0.0f, 360.0f), 0.0f, "°"_u, 0);
        P::choice (l, "shape", "Forma", dsp2::lfoShapeNames(), 0);
        P::toggle (l, "invert", "Invertir", false);
        return l;
    }
    void reset() override { phase = 0.0; for (auto& s : sm) s.reset (1.0f); }
    void prepare (double rate, int) override { for (auto& s : sm) s.prepare (rate, 3.0); reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const bool sync = value ("sync") > 0.5f;
        const int div = (int) value ("div"), shape = (int) value ("shape");
        lockPhase (sync, div, phase);
        const double inc = lfoHz (sync, div, value ("rate")) / sr;
        const float amount = value ("amount") * 0.01f, inv = value ("invert") > 0.5f ? -1.0f : 1.0f;
        const double sp = value ("phase") / 360.0, off = value ("offset") / 360.0;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const double prev = phase;
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            const bool newCycle = phase < prev;
            for (int c = 0; c < ch; ++c)
            {
                double ph = phase + off + (c == 1 ? sp : 0.0); ph -= std::floor (ph);
                const float m = dsp2::lfoShape (shape, ph, hold[c], from[c], rng, newCycle) * inv;
                // gain goes from 1 down to 1 - amount following the LFO (0 at full amount and the bottom of the wave)
                sm[c].target = 1.0f - amount * (0.5f - 0.5f * m);
                b.setSample (c, i, b.getSample (c, i) * sm[c].next());
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Paneo", { { "amount", "Cantidad", "Cuánto se mueve el sonido."_u }, { "rate", "Velocidad", "Ciclos por segundo cuando no va al tempo."_u },
                         { "sync", "Al tempo", "Sigue el tempo de la canción."_u }, { "div", "División"_u, "Duración de un ciclo al tempo."_u },
                         { "phase", "Fase", "180° pasea de izquierda a derecha; 0° sube y baja el volumen (trémolo)."_u } } },
            { "Forma", { { "shape", "Forma", "La figura del movimiento: cuadrada corta, azar salta."_u }, { "offset", "Inicio", "En qué punto del ciclo empieza."_u },
                         { "invert", "Invertir", "Da la vuelta al movimiento."_u } }, true },
        };
        return s;
    }
private:
    double phase = 0.0;
    float hold[2] {}, from[2] {};
    uint32 rng = 777;
    racoon::dsp::Smooth sm[2];
};
}

void registerModulation (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Modulación"_u, detail, Kind::effect, std::move (make), keys }); };
    add ("chorus", "Chorus y ensamble", "Duplica y ensancha: de un chorus clásico a cuerdas de ensamble o un vibrato."_u, [] { return std::make_unique<Chorus>(); },
         { "mode", "rate", "amount", "feedback", "width", "mix" });
    add ("phaser-flanger", "Phaser y flanger", "Barridos de fase, jets de flanger y un doblador de voces."_u, [] { return std::make_unique<PhaserFlanger>(); },
         { "mode", "freq", "feedback", "rate", "amount", "mix" });
    add ("paneo-automatico", "Paneo automático"_u, "Mueve el sonido de izquierda a derecha o sube y baja el volumen al ritmo."_u, [] { return std::make_unique<AutoPan>(); },
         { "amount", "rate", "sync", "div", "phase", "shape" });
}
}

// Delay y eco: Alinear, Eco, Delay filtrado, Delay granular, Repetidor, Looper
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

// equal-power pan, -1..1
inline void panGains (float pan, float& l, float& r)
{
    const float a = (jlimit (-1.0f, 1.0f, pan) + 1.0f) * MathConstants<float>::pi * 0.25f;
    l = std::cos (a) * MathConstants<float>::sqrt2; r = std::sin (a) * MathConstants<float>::sqrt2;
    l = jmin (1.0f, l); r = jmin (1.0f, r);
}

// ===================================================================== Alinear (delay de alineación)
class Align : public FxBase
{
public:
    Align() : FxBase ("Alinear", "Delay y eco", layout(), 1.0) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Unidad", { "Milisegundos", "Muestras", "Metros" }, 0);
        P::flt (l, "msl", "Izquierda (ms)", P::range (0.0f, 1000.0f, 20.0f), 0.0f, " ms", 2);
        P::flt (l, "msr", "Derecha (ms)", P::range (0.0f, 1000.0f, 20.0f), 0.0f, " ms", 2);
        P::integer (l, "sml", "Izquierda (muestras)", 0, 20000, 0);
        P::integer (l, "smr", "Derecha (muestras)", 0, 20000, 0);
        P::flt (l, "ml", "Izquierda (metros)", P::range (0.0f, 300.0f, 10.0f), 0.0f, " m", 2);
        P::flt (l, "mr", "Derecha (metros)", P::range (0.0f, 300.0f, 10.0f), 0.0f, " m", 2);
        P::flt (l, "temp", "Temperatura", P::range (-20.0f, 50.0f), 20.0f, " °C"_u, 0);
        P::toggle (l, "link", "Unir lados", true);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate * 1.05) + 20002); reset(); }
    void reset() override { for (auto& d : line) d.clear(); for (auto& s : sm) s = -1.0f; }
    float samplesFor (int c) const
    {
        const bool link = value ("link") > 0.5f;
        const int mode = (int) value ("mode");
        const char* ids[3][2] { { "msl", "msr" }, { "sml", "smr" }, { "ml", "mr" } };
        const float v = value (ids[mode][link ? 0 : c]);
        if (mode == 0) return v * 0.001f * (float) sr;
        if (mode == 1) return v;
        const float speed = 331.3f * std::sqrt (1.0f + value ("temp") / 273.15f);   // speed of sound for the temperature
        return v / speed * (float) sr;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float glide = 1.0f - dsp2::coeffMs (sr, 22.7);   // the same glide time at any sample rate (was 0.001 per sample)
        for (int c = 0; c < ch; ++c)
        {
            const float target = samplesFor (c);
            if (sm[c] < 0.0f) sm[c] = target;
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                sm[c] += (target - sm[c]) * glide;          // glide instead of clicking when the time moves
                if (std::abs (sm[c] - target) < 0.01f) sm[c] = target;
                line[c].push (d[i]);
                d[i] = sm[c] < 0.5f ? d[i] : (std::abs (sm[c] - std::round (sm[c])) < 1.0e-4f ? line[c].tapInt ((int) std::round (sm[c]) + 1) : line[c].tap (sm[c] + 1.0f));
            }
        }
    }
    String displayText() const override
    {
        return "I: " + String (samplesFor (0) / sr * 1000.0, 2) + " ms   D: " + String (samplesFor (1) / sr * 1000.0, 2) + " ms";
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Alinear", { { "mode", "Unidad", "Mide el retraso en tiempo, en muestras o en metros de distancia."_u },
                           { "msl", "Izq ms", "Retraso del lado izquierdo."_u }, { "msr", "Der ms", "Retraso del lado derecho."_u },
                           { "link", "Unir", "Los dos lados usan el valor de la izquierda."_u } } },
            { "Otras unidades", { { "sml", "Izq muestras", "" }, { "smr", "Der muestras", "" }, { "ml", "Izq metros", "Distancia entre micrófonos o bocinas."_u },
                                  { "mr", "Der metros", "" }, { "temp", "Temperatura", "Cambia la velocidad del sonido en metros."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine line[2];
    float sm[2] { -1.0f, -1.0f };
};

// ===================================================================== Eco
class Echo : public FxBase
{
public:
    Echo() : FxBase ("Eco", "Delay y eco", layout(), 8.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::toggle (l, "sync", "Al tempo", true);
        P::choice (l, "divl", "División izquierda"_u, RacoonProcessor::divisionNames(), 8);
        P::choice (l, "divr", "División derecha"_u, RacoonProcessor::divisionNames(), 8);
        P::ms (l, "msl", "Tiempo izquierdo", 1.0f, 3000.0f, 300.0f, 300.0f);
        P::ms (l, "msr", "Tiempo derecho", 1.0f, 3000.0f, 300.0f, 300.0f);
        P::toggle (l, "link", "Unir lados", true);
        P::choice (l, "chmode", "Modo", { "Estéreo"_u, "Ping pong", "Medio y lados" }, 0);
        P::pct (l, "feedback", "Realimentación"_u, 0.0f, 120.0f, 45.0f);
        P::toggle (l, "fbinv", "Invertir realimentación"_u, false);
        P::db (l, "input", "Entrada", -24.0f, 12.0f, 0.0f);
        P::hz (l, "hp", "Corte de graves", 20.0f, 2000.0f, 100.0f);
        P::hz (l, "lp", "Corte de agudos", 500.0f, 20000.0f, 8000.0f);
        P::flt (l, "reso", "Resonancia", P::range (0.5f, 6.0f, 1.0f), 0.71f, "", 2);
        P::flt (l, "modrate", "Modulación: velocidad"_u, P::range (0.05f, 10.0f, 1.0f), 0.5f, " Hz", 2);
        P::pct (l, "moddepth", "Modulación: cantidad"_u, 0.0f, 100.0f, 0.0f);
        P::pct (l, "diffuse", "Reverb en los ecos", 0.0f, 100.0f, 0.0f);
        P::pct (l, "duck", "Ducking", 0.0f, 100.0f, 0.0f);
        P::pct (l, "noise", "Ruido", 0.0f, 100.0f, 0.0f);
        P::pct (l, "wobble", "Wobble", 0.0f, 100.0f, 0.0f);
        P::toggle (l, "repitch", "Cambiar afinación al mover el tiempo"_u, true);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 35.0f);
        return l;
    }
    void prepare (double rate, int) override
    {
        for (auto& d : line) d.prepare ((int) (rate * 6.2));
        // the diffusers were tuned at 44.1 kHz: their lengths follow the sample rate so the "room" keeps its size
        for (int k = 0; k < 4; ++k) diffN[k] = jmax (1, (int) std::round (diffLen[k] * rate / 44100.0));
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) diff[c][k].prepare (diffN[k] * 3 + 16);
        duckF.set (rate, 5.0f, 250.0f);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& c : diff) for (auto& d : c) d.clear();
        for (auto& f : hpF) f.reset();
        for (auto& f : lpF) f.reset();
        fb[0] = fb[1] = 0.0f; t[0] = t[1] = -1.0f; modPh = 0.0; wob[0] = wob[1] = 0.0f; duckF.env = 0.0f; xfade = 1.0f;
    }
    float targetSamples (int c) const
    {
        const bool link = value ("link") > 0.5f;
        const int side = link ? 0 : c;
        double sec = value ("sync") > 0.5f ? syncSeconds ((int) value (side == 0 ? "divl" : "divr"), bpm()) : value (side == 0 ? "msl" : "msr") * 0.001;
        return (float) jlimit (2.0, sr * 6.0, sec * sr);
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int mode = (int) value ("chmode");
        const float fbk = value ("feedback") * 0.01f * (value ("fbinv") > 0.5f ? -1.0f : 1.0f);
        const float in = toGain (value ("input")), mix = value ("mix") * 0.01f;
        const float modD = value ("moddepth") * 0.01f * (float) (sr * 0.004);
        const double modInc = value ("modrate") / sr;
        const float diffuse = value ("diffuse") * 0.01f, duck = value ("duck") * 0.01f;
        const float noise = value ("noise") * 0.01f * 0.01f, wobble = value ("wobble") * 0.01f;
        const bool repitch = value ("repitch") > 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            hpF[c].set (Biquad::highpass, sr, value ("hp"), 0.707);
            lpF[c].set (Biquad::lowpass, sr, value ("lp"), value ("reso"));
        }
        const float tgt[2] { targetSamples (0), targetSamples (1) };
        for (int c = 0; c < 2; ++c) if (t[c] < 0.0f) t[c] = tgt[c];
        // without repitch a time change crossfades to the new tap instead of gliding
        if (! repitch && (std::abs (tgt[0] - t[0]) > 1.0f || std::abs (tgt[1] - t[1]) > 1.0f) && xfade >= 1.0f)
        {
            old[0] = t[0]; old[1] = t[1]; t[0] = tgt[0]; t[1] = tgt[1]; xfade = 0.0f;
        }
        const float glide = dsp2::coeffMs (sr, 120.0);
        for (int i = 0; i < n; ++i)
        {
            modPh += modInc; if (modPh >= 1.0) modPh -= 1.0;
            const float m = (float) std::sin (twoPi * modPh);
            const float xl = b.getSample (0, i), xr = ch > 1 ? b.getSample (1, i) : xl;
            const float level = duckF.process (0.5f * (std::abs (xl) + std::abs (xr)));
            float inL = xl * in, inR = xr * in;
            if (mode == 2) { const float mm = 0.5f * (inL + inR), ss = 0.5f * (inL - inR); inL = mm; inR = ss; }
            if (mode == 1) { inL = 0.5f * (inL + inR); inR = 0.0f; }   // ping pong: everything enters on the left
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                if (repitch) t[c] = tgt[c] + (t[c] - tgt[c]) * glide;
                // wobble: slow irregular drift, like tape
                wob[c] += (rng.nextBipolar() - wob[c]) * 0.0002f;
                const float wd = wobble * wob[c] * (float) (sr * 0.006);
                const float mod = modD * (c == 0 ? m : -m) + wd;
                float y = line[c].tap (t[c] + mod);
                if (xfade < 1.0f) y = y * xfade + line[c].tap (old[c] + mod) * (1.0f - xfade);
                // diffusion: allpasses smear each echo into a small room
                if (diffuse > 0.0f)
                {
                    float d = y;
                    for (int k = 0; k < 4; ++k)
                    {
                        const float delayed = diff[c][k].tapInt (diffN[k] + c * 7);
                        const float v = d + delayed * 0.6f;
                        diff[c][k].push (v);
                        d = delayed - v * 0.6f;
                    }
                    y = y * (1.0f - diffuse) + d * diffuse;
                }
                wet[c] = y;
            }
            if (xfade < 1.0f) xfade = jmin (1.0f, xfade + (float) (1.0 / (sr * 0.05)));
            for (int c = 0; c < 2; ++c)
            {
                float f = hpF[c].process (lpF[c].process (wet[c], c), c);
                f = std::tanh (f);                         // keeps feedback above 100 % from exploding
                fb[c] = f + (noise > 0.0f ? rng.nextBipolar() * noise : 0.0f);
            }
            const float inC[2] { inL, inR };
            for (int c = 0; c < 2; ++c)
            {
                const float other = fb[1 - c];
                line[c].push (inC[c] + (mode == 1 ? other : fb[c]) * fbk);
            }
            float oL = wet[0], oR = wet[1];
            if (mode == 2) { const float mm = oL, ss = oR; oL = mm + ss; oR = mm - ss; }
            const float dg = 1.0f - duck * jlimit (0.0f, 1.0f, level * 4.0f);
            b.setSample (0, i, xl * (1.0f - mix) + oL * mix * dg);
            if (ch > 1) b.setSample (1, i, xr * (1.0f - mix) + oR * mix * dg);
        }
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeEchoDisplay (*this, [this]
        {
            dsp2::EchoView v;
            v.leftSeconds = targetSamples (0) / jmax (1.0, sr);
            v.rightSeconds = targetSamples (1) / jmax (1.0, sr);
            v.feedback = value ("feedback") * 0.01f;
            v.mix = value ("mix") * 0.01f;
            v.pingPong = (int) value ("chmode") == 1;
            v.caption = String (v.leftSeconds * 1000.0, 0) + " ms" + (std::abs (v.leftSeconds - v.rightSeconds) > 1.0e-4 ? " / " + String (v.rightSeconds * 1000.0, 0) + " ms" : String())
                      + String::fromUTF8 ("   realim. ") + String (value ("feedback"), 0) + " %";
            return v;
        });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Eco", { { "sync", "Al tempo", "Los ecos caen en el ritmo de la canción."_u }, { "divl", "Izquierda", "Duración del eco izquierdo al tempo."_u },
                       { "divr", "Derecha", "Duración del eco derecho."_u }, { "link", "Unir", "Los dos lados usan el tiempo de la izquierda."_u },
                       { "feedback", "Repeticiones", "Cuántas veces se repite. Más de 100 % crece y se satura."_u },
                       { "mix", "Mezcla", "Cuánto eco se oye."_u } } },
            { "Filtro y color", { { "hp", "Graves", "Los ecos pierden graves en cada vuelta."_u }, { "lp", "Agudos", "Los ecos se oscurecen en cada vuelta."_u },
                                  { "reso", "Resonancia", "Acentúa el corte de agudos."_u }, { "chmode", "Modo", "Ping pong rebota entre lados."_u },
                                  { "diffuse", "Reverb", "Cada eco se vuelve una pequeña sala."_u }, { "duck", "Ducking", "Baja los ecos mientras tocas, suben en los silencios."_u } }, true },
            { "Movimiento", { { "msl", "Izq ms", "Tiempo libre izquierdo."_u }, { "msr", "Der ms", "Tiempo libre derecho."_u },
                              { "modrate", "Velocidad", "Modulación del tiempo: chorus en los ecos."_u }, { "moddepth", "Cantidad", ""_u },
                              { "wobble", "Wobble", "Deriva irregular, como una cinta gastada."_u }, { "noise", "Ruido", "Soplido de cinta en las repeticiones."_u },
                              { "repitch", "Repitch", "Al cambiar el tiempo la afinación se desliza; apagado hace un fundido."_u },
                              { "fbinv", "Invertir", "Invierte la polaridad de las repeticiones."_u }, { "input", "Entrada", "Cuánto entra al eco."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine line[2], diff[2][4];
    static constexpr int diffLen[4] { 142, 107, 379, 277 };   // at 44.1 kHz
    int diffN[4] { 142, 107, 379, 277 };
    Biquad hpF[2], lpF[2];
    float fb[2] {}, t[2] { -1.0f, -1.0f }, old[2] {}, wob[2] {}, xfade = 1.0f;
    double modPh = 0.0;
    racoon::dsp::Follower duckF;
    racoon::dsp::Rng rng;
};

// ===================================================================== Delay filtrado (tres líneas: izquierda, centro, derecha)
class FilterDelay : public FxBase
{
public:
    FilterDelay() : FxBase ("Delay filtrado", "Delay y eco", layout(), 8.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        static const char* names[] { "Izquierda", "Centro", "Derecha" };
        static const int divs[] { 7, 4, 10 };
        static const float freqs[] { 500.0f, 1000.0f, 3000.0f };
        static const float pans[] { -1.0f, 0.0f, 1.0f };
        P::toggle (l, "sync", "Al tempo", true);
        for (int k = 0; k < 3; ++k)
        {
            const String id (k + 1), nm = String (names[k]) + ": ";
            P::toggle (l, "on" + id, nm + "encendida", true);
            P::choice (l, "div" + id, nm + "división"_u, RacoonProcessor::divisionNames(), divs[k]);
            P::ms (l, "ms" + id, nm + "tiempo", 1.0f, 3000.0f, 250.0f * (k + 1), 300.0f);
            P::hz (l, "freq" + id, nm + "filtro", 50.0f, 18000.0f, freqs[k]);
            P::flt (l, "width" + id, nm + "ancho del filtro", P::range (0.5f, 9.0f), 4.0f, " oct", 1);
            P::pct (l, "fb" + id, nm + "repeticiones", 0.0f, 95.0f, 40.0f);
            P::flt (l, "pan" + id, nm + "paneo", P::range (-50.0f, 50.0f), pans[k] * 50.0f, "", 0);
            P::db (l, "vol" + id, nm + "volumen", -70.0f, 6.0f, 0.0f);
        }
        P::db (l, "dry", "Señal original"_u, -70.0f, 6.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate * 3.2)); reset(); }
    void reset() override { for (auto& d : line) d.clear(); for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); for (auto& f : fb) f = 0.0f; for (auto& t : tm) t = -1.0f; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const bool sync = value ("sync") > 0.5f;
        const float dry = toGain (value ("dry"));
        float tgt[3], fbk[3], vol[3], gl[3], gr[3];
        bool on[3];
        // fixed parameter names: nothing is allocated on the audio thread
        static const char* ids[7][3] { { "on1", "on2", "on3" }, { "div1", "div2", "div3" }, { "ms1", "ms2", "ms3" }, { "freq1", "freq2", "freq3" },
                                       { "width1", "width2", "width3" }, { "fb1", "fb2", "fb3" }, { "vol1", "vol2", "vol3" } };
        static const char* panIds[3] { "pan1", "pan2", "pan3" };
        for (int k = 0; k < 3; ++k)
        {
            on[k] = value (ids[0][k]) > 0.5f;
            const double sec = sync ? syncSeconds ((int) value (ids[1][k]), bpm()) : value (ids[2][k]) * 0.001;
            tgt[k] = (float) jlimit (2.0, sr * 3.0, sec * sr);
            if (tm[k] < 0.0f) tm[k] = tgt[k];
            const double f = value (ids[3][k]), half = value (ids[4][k]) * 0.5;
            hp[k].set (Biquad::highpass, sr, f / std::pow (2.0, half), 0.707);
            lp[k].set (Biquad::lowpass, sr, jmin (sr * 0.45, f * std::pow (2.0, half)), 0.707);
            fbk[k] = value (ids[5][k]) * 0.01f;
            vol[k] = on[k] ? toGain (value (ids[6][k])) : 0.0f;
            panGains (value (panIds[k]) / 50.0f, gl[k], gr[k]);
        }
        const float glide = dsp2::coeffMs (sr, 100.0);
        for (int i = 0; i < n; ++i)
        {
            const float xl = b.getSample (0, i), xr = ch > 1 ? b.getSample (1, i) : xl;
            const float ins[3] { xl, 0.5f * (xl + xr), xr };
            float oL = xl * dry, oR = xr * dry;
            for (int k = 0; k < 3; ++k)
            {
                tm[k] = tgt[k] + (tm[k] - tgt[k]) * glide;
                const float y = line[k].tap (tm[k]);
                const float f = lp[k].process (hp[k].process (y, 0), 0);
                fb[k] = f;
                line[k].push ((on[k] ? ins[k] : 0.0f) + std::tanh (f * fbk[k]));
                oL += f * vol[k] * gl[k];
                oR += f * vol[k] * gr[k];
            }
            b.setSample (0, i, oL);
            if (ch > 1) b.setSample (1, i, oR);
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections.push_back ({ "General", { { "sync", "Al tempo", "Los tres delays siguen el tempo."_u }, { "dry", "Original", "Volumen de la señal sin eco."_u } } });
        static const char* names[] { "Izquierda", "Centro", "Derecha" };
        for (int k = 0; k < 3; ++k)
        {
            const String id (k + 1);
            s.sections.push_back ({ names[k], { { "on" + id, "Entrada", "Prende esta línea."_u }, { "div" + id, "División"_u, "Tiempo al tempo."_u },
                                                { "freq" + id, "Filtro", "Centro del filtro de esta línea."_u }, { "fb" + id, "Repeticiones", ""_u },
                                                { "pan" + id, "Paneo", ""_u }, { "vol" + id, "Volumen", ""_u } }, k == 1 });
        }
        s.sections.push_back ({ "Tiempos libres y anchos", { { "ms1", "Izq ms", "" }, { "ms2", "Centro ms", "" }, { "ms3", "Der ms", "" },
                                                            { "width1", "Ancho izq", "" }, { "width2", "Ancho centro", "" }, { "width3", "Ancho der", "" } }, true });
        return s;
    }
private:
    racoon::dsp::DelayLine line[3];
    Biquad hp[3], lp[3];
    float fb[3] {}, tm[3] { -1.0f, -1.0f, -1.0f };
};

// ===================================================================== Delay granular
class GrainDelay : public FxBase
{
public:
    GrainDelay() : FxBase ("Delay granular", "Delay y eco", layout(), 5.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::toggle (l, "sync", "Al tempo", false);
        P::choice (l, "div", "División"_u, RacoonProcessor::divisionNames(), 7);
        P::ms (l, "time", "Tiempo", 1.0f, 2000.0f, 120.0f, 200.0f);
        P::ms (l, "spray", "Dispersión"_u, 0.0f, 500.0f, 10.0f, 50.0f);
        P::flt (l, "freq", "Granos por segundo", P::range (1.0f, 150.0f, 20.0f), 30.0f, " Hz", 1);
        P::flt (l, "pitch", "Afinación"_u, P::range (-36.0f, 12.0f), 0.0f, " st", 1);
        P::flt (l, "rpitch", "Afinación al azar"_u, P::range (0.0f, 12.0f), 0.0f, " st", 1);
        P::pct (l, "feedback", "Realimentación"_u, 0.0f, 95.0f, 20.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    void prepare (double rate, int) override { for (auto& d : line) d.prepare ((int) (rate * 3.5)); reset(); }
    void reset() override { for (auto& d : line) d.clear(); for (auto& g : grains) g.active = false; untilNext = 0.0; fb[0] = fb[1] = 0.0f; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double delay = (value ("sync") > 0.5f ? syncSeconds ((int) value ("div"), bpm()) : value ("time") * 0.001) * sr;
        const double spray = value ("spray") * 0.001 * sr;
        const double period = sr / value ("freq");
        const float pitch = value ("pitch"), rp = value ("rpitch"), fbk = value ("feedback") * 0.01f, mix = value ("mix") * 0.01f;
        for (int i = 0; i < n; ++i)
        {
            untilNext -= 1.0;
            if (untilNext <= 0.0)
            {
                untilNext += period;
                for (auto& g : grains)
                    if (! g.active)
                    {
                        // each grain lasts two periods (so grains overlap) and reads at its own speed
                        g.active = true;
                        g.len = jmax (64.0, period * 2.0);
                        g.age = 0.0;
                        g.ratio = std::pow (2.0, (pitch + rp * rng.nextBipolar()) / 12.0);
                        // start far enough back that a faster grain never overtakes the write head
                        const double need = jmax (0.0, (g.ratio - 1.0) * g.len);
                        g.start = delay + spray * rng.next01() + need + 4.0;
                        break;
                    }
            }
            float wet[2] {};
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float w = 0.5f - 0.5f * (float) std::cos (twoPi * g.age / g.len);
                const double back = g.start - g.age * (g.ratio - 1.0) + 0.0;   // distance behind the write head
                for (int c = 0; c < ch; ++c) wet[c] += line[c].tap ((float) jlimit (2.0, sr * 3.4, back)) * w;
                g.age += 1.0;
                if (g.age >= g.len) g.active = false;
            }
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                const float y = wet[c] * 0.7f;
                line[c].push (x + std::tanh (y * fbk));
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Delay granular", { { "time", "Tiempo", "Cuánto tarda en sonar el eco."_u }, { "pitch", "Afinación"_u, "Sube o baja la afinación de los ecos."_u },
                                  { "spray", "Dispersión"_u, "Cada grano sale de un momento un poco distinto: se desdibuja."_u },
                                  { "freq", "Granos", "Granos por segundo. Pocos se oyen cortados, muchos se funden."_u },
                                  { "feedback", "Repeticiones", "Los ecos vuelven a entrar: con afinación crean escaleras."_u },
                                  { "mix", "Mezcla", "Cuánto efecto se oye."_u } } },
            { "Más"_u, { { "rpitch", "Azar", "Cada grano con una afinación al azar."_u }, { "sync", "Al tempo", ""_u }, { "div", "División"_u, ""_u } }, true },
        };
        return s;
    }
private:
    struct Grain { bool active = false; double start = 0, len = 0, age = 0, ratio = 1; };
    Grain grains[16];
    racoon::dsp::DelayLine line[2];
    double untilNext = 0.0;
    float fb[2] {};
    racoon::dsp::Rng rng;
};

// ===================================================================== Repetidor (stutter)
class BeatRepeat : public FxBase
{
public:
    BeatRepeat() : FxBase ("Repetidor", "Delay y eco", layout(), 0.0, false, false, false) {}
    static StringArray intervals() { return { "1/32", "1/16", "1/8", "1/4", "1/2", "1 compás"_u, "2 compases", "4 compases" }; }
    static StringArray grids() { return { "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64" }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "interval", "Cada", intervals(), 5);
        P::integer (l, "offset", "Desplazamiento (1/16)", 0, 15, 8);
        P::choice (l, "grid", "Rejilla", grids(), 4);
        P::pct (l, "variation", "Variación"_u, 0.0f, 100.0f, 0.0f);
        P::pct (l, "chance", "Probabilidad", 0.0f, 100.0f, 100.0f);
        P::integer (l, "gate", "Duración (1/16)"_u, 1, 64, 8);
        P::pct (l, "decay", "Caída"_u, 0.0f, 100.0f, 0.0f);
        P::flt (l, "pitch", "Afinación"_u, P::range (-48.0f, 0.0f), 0.0f, " st", 0);
        P::pct (l, "pdecay", "Caída de afinación"_u, 0.0f, 100.0f, 0.0f);
        P::choice (l, "mode", "Modo", { "Mezclar", "Insertar", "Solo repeticiones"_u }, 1);
        P::db (l, "volume", "Volumen", -36.0f, 6.0f, 0.0f);
        P::toggle (l, "filter", "Filtro", false);
        P::hz (l, "ffreq", "Filtro: frecuencia", 50.0f, 18000.0f, 1000.0f);
        P::flt (l, "fwidth", "Filtro: ancho", P::range (0.5f, 9.0f), 4.0f, " oct", 1);
        P::toggle (l, "repeat", "Repetir ya", false);
        return l;
    }
    void prepare (double rate, int) override { cap[0].assign ((size_t) (rate * 2.2), 0.0f); cap[1].assign ((size_t) (rate * 2.2), 0.0f); reset(); }
    void reset() override { active = false; pos16 = -1.0; freePos = 0.0; for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); manualWas = false; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        static const double intervals16[] { 0.5, 1, 2, 4, 8, 16, 32, 64 };
        static const double grid16[] { 4, 16.0 / 6.0, 2, 4.0 / 3.0, 1, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25 };
        const double spb = 60.0 / jlimit (20.0, 400.0, bpm()) * sr;   // samples per beat
        const double sp16 = spb / 4.0;
        const double interval = intervals16[(int) value ("interval")], offset = value ("offset");
        const int gridIdx = (int) value ("grid");
        const float chance = value ("chance") * 0.01f, variation = value ("variation") * 0.01f;
        const float decay = value ("decay") * 0.01f, pitch = value ("pitch"), pdecay = value ("pdecay") * 0.01f;
        const int mode = (int) value ("mode");
        const float vol = toGain (value ("volume"));
        const bool filt = value ("filter") > 0.5f, manual = value ("repeat") > 0.5f;
        if (filt)
        {
            const double f = value ("ffreq"), half = value ("fwidth") * 0.5;
            for (int c = 0; c < 2; ++c)
            {
                hp[c].set (Biquad::highpass, sr, f / std::pow (2.0, half), 0.707);
                lp[c].set (Biquad::lowpass, sr, jmin (sr * 0.45, f * std::pow (2.0, half)), 0.707);
            }
        }
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        double p = isPlaying() ? ppq() * 4.0 : freePos;
        const double inc = 1.0 / sp16;
        for (int i = 0; i < n; ++i, p += inc)
        {
            // trigger at every interval + offset (or when the switch is pressed)
            bool trigger = false;
            if (pos16 >= 0.0)
            {
                const double a = std::floor ((pos16 - offset) / interval), c2 = std::floor ((p - offset) / interval);
                if (c2 > a && rng.next01() < chance) trigger = true;
            }
            if (manual && ! manualWas) trigger = true;
            manualWas = manual;
            pos16 = p;
            if (trigger)
            {
                active = true;
                elapsed = 0.0;
                gateLen = value ("gate") * sp16;
                newSlice (grid16[gridIdx], variation, sp16);
                captured = 0;
                repeatN = 0;
                readPos = 0.0;
            }
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float rep = 0.0f;
                bool inRepeat = false;
                if (active)
                {
                    inRepeat = true;
                    // first pass: record the slice (and let it through)
                    if (captured < sliceLen) { cap[c][(size_t) captured] = x; rep = x; }
                    else
                    {
                        const float gain = std::pow (1.0f - decay * 0.6f, (float) repeatN);
                        const int ip = (int) readPos;
                        const float fr = (float) (readPos - ip);
                        const float a = cap[c][(size_t) jmin (ip, sliceLen - 1)], bb = cap[c][(size_t) jmin (ip + 1, sliceLen - 1)];
                        // short fades at both ends of the slice so repeats never click
                        const float edge = jmin (1.0f, (float) jmin (readPos, sliceLen - readPos) / 48.0f);
                        rep = (a + (bb - a) * fr) * gain * edge;
                    }
                    if (filt) rep = lp[c].process (hp[c].process (rep, c), c);
                }
                float y;
                if (mode == 0) y = x + (inRepeat && captured >= sliceLen ? rep * vol : 0.0f);
                else if (mode == 1) y = inRepeat ? (captured < sliceLen ? x : rep * vol) : x;
                else y = inRepeat && captured >= sliceLen ? rep * vol : 0.0f;
                b.setSample (c, i, y);
            }
            if (active)
            {
                if (captured < sliceLen) ++captured;
                else
                {
                    const float st = pitch * (1.0f + pdecay * (float) repeatN * 0.3f);
                    readPos += std::pow (2.0, st / 12.0);
                    if (readPos >= sliceLen)
                    {
                        readPos = 0.0; ++repeatN;
                        if (variation > 0.0f) newSlice (grid16[gridIdx], variation, sp16, false);
                    }
                }
                elapsed += 1.0;
                if (elapsed >= gateLen) active = false;
            }
        }
        if (! isPlaying()) freePos = p;
    }
    void newSlice (double grid, float variation, double sp16, bool first = true)
    {
        double g = grid;
        if (variation > 0.0f && rng.next01() < variation)
        {
            static const double options[] { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1, 4.0 / 3.0, 2 };
            g = options[(int) (rng.next01() * 6.99f)];
        }
        const int len = jlimit (16, (int) cap[0].size() - 2, (int) (g * sp16));
        if (first) sliceLen = len;
        else sliceLen = jmin (sliceLen, len);   // later repeats can only get shorter (the slice is already recorded)
    }
    String displayText() const override { return active ? "REPITIENDO" : ""; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Repetidor", { { "interval", "Cada", "Cada cuánto puede empezar una repetición."_u }, { "offset", "Desplazamiento", "En qué dieciseisavo dentro del intervalo empieza."_u },
                             { "grid", "Rejilla", "El tamaño del pedazo que se repite: más chico, más rápido el redoble."_u },
                             { "gate", "Duración"_u, "Cuántos dieciseisavos dura la repetición."_u }, { "chance", "Probabilidad", "Qué tanto pasa en cada intervalo."_u },
                             { "repeat", "Repetir ya", "Al prenderlo empieza una repetición en ese momento."_u } } },
            { "Color", { { "mode", "Modo", "Mezclar suma, insertar reemplaza el sonido, solo repeticiones calla lo demás."_u },
                         { "decay", "Caída"_u, "Cada repetición más bajita."_u }, { "pitch", "Afinación"_u, "Las repeticiones más graves."_u },
                         { "pdecay", "Bajada", "La afinación baja más con cada repetición."_u }, { "variation", "Variación"_u, "Cambia la rejilla al azar."_u },
                         { "volume", "Volumen", "Volumen de las repeticiones."_u } }, true },
            { "Filtro", { { "filter", "Filtro", ""_u }, { "ffreq", "Frecuencia", ""_u }, { "fwidth", "Ancho", ""_u } }, true },
        };
        return s;
    }
private:
    std::vector<float> cap[2];
    bool active = false, manualWas = false;
    double pos16 = -1.0, freePos = 0.0, elapsed = 0.0, gateLen = 0.0, readPos = 0.0;
    int sliceLen = 1000, captured = 0, repeatN = 0;
    Biquad hp[2], lp[2];
    racoon::dsp::Rng rng;
};

// ===================================================================== Looper
class Looper : public FxBase
{
public:
    Looper() : FxBase ("Looper", "Delay y eco", layout(), 0.0, false, false, false) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "state", "Estado", { "Detenido", "Grabar", "Reproducir", "Sobregrabar" }, 0);
        P::choice (l, "length", "Largo", { "Libre", "1 compás"_u, "2 compases", "4 compases", "8 compases", "16 compases" }, 0);
        P::flt (l, "speed", "Velocidad", P::range (-12.0f, 12.0f), 0.0f, " st", 1);
        P::toggle (l, "reverse", "Al revés"_u, false);
        P::pct (l, "feedback", "Conservar al sobregrabar", 0.0f, 100.0f, 100.0f);
        P::choice (l, "monitor", "Escuchar la entrada", { "Siempre", "Nunca", "Solo grabando" }, 0);
        P::db (l, "volume", "Volumen del loop", -70.0f, 6.0f, 0.0f);
        P::toggle (l, "clear", "Borrar", false);
        return l;
    }
    static constexpr double maxSeconds = 60.0;
    void prepare (double rate, int) override { for (auto& l : loop) l.assign ((size_t) (rate * maxSeconds), 0.0f); reset(); }
    void reset() override { len = 0; pos = 0.0; recPos = 0; lastState = 0; for (auto& l : loop) std::fill (l.begin(), l.end(), 0.0f); takePending(); }

    // the recording travels with the project (FLAC inside the plug-in state)
    void saveExtraState (ValueTree& v) const override
    {
        const int n = len;
        if (n <= 0 || loop[0].empty()) return;
        AudioBuffer<float> buf (2, n);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, loop[c].data(), n);
        MemoryBlock moBlock;
        {
            FlacAudioFormat flac;
            std::unique_ptr<AudioFormatWriter> w (flac.createWriterFor (new MemoryOutputStream (moBlock, false), sr, 2, 24, {}, 0));
            if (w == nullptr) return;
            w->writeFromAudioSampleBuffer (buf, 0, n);
        }
        v.setProperty ("loop", moBlock.toBase64Encoding(), nullptr);
        v.setProperty ("loopRate", sr, nullptr);
    }
    void loadExtraState (const ValueTree& v) override
    {
        MemoryBlock mb;
        if (! mb.fromBase64Encoding (v.getProperty ("loop").toString()) || mb.getSize() == 0) return;
        FlacAudioFormat flac;
        std::unique_ptr<AudioFormatReader> r (flac.createReaderFor (new MemoryInputStream (mb, false), true));
        if (r == nullptr) return;
        AudioBuffer<float> buf (2, (int) r->lengthInSamples);
        r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
        const ScopedLock sl (pendingLock);
        pending = std::move (buf);
        pendingRate = (double) v.getProperty ("loopRate", 48000.0);
        hasPending = true;
    }
    void takePending()
    {
        const ScopedTryLock sl (pendingLock);
        if (! sl.isLocked() || ! hasPending || loop[0].empty()) return;
        hasPending = false;
        // a different sample rate: read it at the right speed
        const double ratio = pendingRate / sr;
        const int n = jmin ((int) loop[0].size(), (int) (pending.getNumSamples() / ratio));
        for (int c = 0; c < 2; ++c)
        {
            const float* src = pending.getReadPointer (jmin (c, pending.getNumChannels() - 1));
            for (int i = 0; i < n; ++i)
            {
                const double x = i * ratio;
                const int k = (int) x;
                const float f = (float) (x - k);
                const float a = src[jmin (k, pending.getNumSamples() - 1)], b = src[jmin (k + 1, pending.getNumSamples() - 1)];
                loop[c][(size_t) i] = a + (b - a) * f;
            }
        }
        len = n; pos = 0.0; recPos = n;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        if (loop[0].empty()) return;
        if (hasPending) takePending();
        const int state = (int) value ("state");
        const bool clearNow = value ("clear") > 0.5f;
        if (clearNow && ! clearWas) { len = 0; pos = 0.0; recPos = 0; for (auto& l : loop) std::fill (l.begin(), l.end(), 0.0f); }
        clearWas = clearNow;
        static const int bars[] { 0, 1, 2, 4, 8, 16 };
        const int barsN = bars[(int) value ("length")];
        const int fixedLen = barsN > 0 ? jmin ((int) loop[0].size(), (int) std::round (barsN * 4.0 * 60.0 / jlimit (20.0, 400.0, bpm()) * sr)) : 0;
        // entering record starts a new loop
        if (state == 1 && lastState != 1) { recPos = 0; pos = 0.0; if (fixedLen > 0) len = fixedLen; else len = 0; }
        // leaving the first recording closes the loop at that length
        if (lastState == 1 && state != 1 && barsN == 0) { len = jmax (len, recPos); pos = 0.0; }
        lastState = state;
        const int monitor = (int) value ("monitor");
        const float vol = toGain (value ("volume")), keep = value ("feedback") * 0.01f;
        const double speed = std::pow (2.0, value ("speed") / 12.0) * (value ("reverse") > 0.5f ? -1.0 : 1.0);
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int cap = (int) loop[0].size();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float out = 0.0f;
                if (state == 1)
                {
                    const int at = fixedLen > 0 ? recPos % fixedLen : jmin (recPos, cap - 1);
                    loop[c][(size_t) at] = x;
                }
                else if ((state == 2 || state == 3) && len > 0)
                {
                    const int ip = (int) pos;
                    const float fr = (float) (pos - ip);
                    const float a = loop[c][(size_t) (ip % len)], bb = loop[c][(size_t) ((ip + 1) % len)];
                    out = (a + (bb - a) * fr) * vol;
                    if (state == 3) loop[c][(size_t) (ip % len)] = loop[c][(size_t) (ip % len)] * keep + x;
                }
                const bool hear = monitor == 0 || (monitor == 2 && (state == 1 || state == 3));
                b.setSample (c, i, (hear ? x : 0.0f) + out);
            }
            if (state == 1) ++recPos;
            else if ((state == 2 || state == 3) && len > 0)
            {
                pos += (state == 3 ? (speed > 0 ? 1.0 : -1.0) : speed);
                while (pos >= len) pos -= len;
                while (pos < 0.0) pos += len;
            }
        }
        if (fixedLen == 0 && state == 1) len = jmin (recPos, cap);
    }
    String displayText() const override
    {
        static const char* names[] { "DETENIDO", "GRABANDO", "REPRODUCIENDO", "SOBREGRABANDO" };
        const int s = (int) value ("state");
        return String (names[jlimit (0, 3, s)]) + (len > 0 ? "   " + String (len / sr, 1) + " s" : String());
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Looper", { { "state", "Estado", "Graba una frase, luego reprodúcela o graba encima."_u },
                          { "length", "Largo", "Libre se cierra al dejar de grabar; en compases sigue el tempo."_u },
                          { "speed", "Velocidad", "Más rápido y agudo, o más lento y grave."_u }, { "reverse", "Al revés"_u, ""_u },
                          { "clear", "Borrar", "Vacía el loop."_u } } },
            { "Mezcla", { { "feedback", "Conservar", "Cuánto queda de lo anterior al grabar encima."_u },
                          { "monitor", "Entrada", "Si se oye lo que entra."_u }, { "volume", "Volumen", "Volumen del loop."_u } }, true },
        };
        return s;
    }
private:
    std::vector<float> loop[2];
    AudioBuffer<float> pending;
    double pendingRate = 48000.0;
    std::atomic<bool> hasPending { false };
    CriticalSection pendingLock;
    int len = 0, recPos = 0, lastState = 0;
    double pos = 0.0;
    bool clearWas = false;
};
}

void registerDelays (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Delay y eco", detail, Kind::effect, std::move (make), keys }); };
    add ("eco", "Eco", "Delay de cinta con filtro, modulación, reverb y ducking."_u, [] { return std::make_unique<Echo>(); },
         { "divl", "divr", "feedback", "lp", "diffuse", "mix" });
    add ("delay-filtrado", "Delay filtrado", "Tres delays con su propio filtro: izquierda, centro y derecha."_u, [] { return std::make_unique<FilterDelay>(); },
         { "div1", "div2", "div3", "fb1", "fb3", "dry" });
    add ("delay-granular", "Delay granular", "Corta el eco en granos y los reafina: texturas y escaleras."_u, [] { return std::make_unique<GrainDelay>(); },
         { "time", "spray", "freq", "pitch", "feedback", "mix" });
    add ("repetidor", "Repetidor", "Repite pedacitos al ritmo: redobles, tartamudeos y cortes."_u, [] { return std::make_unique<BeatRepeat>(); },
         { "interval", "grid", "gate", "chance", "pitch", "repeat" });
    add ("looper", "Looper", "Graba una frase y repítela; graba encima para armar capas."_u, [] { return std::make_unique<Looper>(); },
         { "state", "length", "speed", "reverse", "clear" });
    add ("alinear", "Alinear", "Retrasa un lado unos milisegundos para corregir fase entre micrófonos."_u, [] { return std::make_unique<Align>(); },
         { "mode", "msl", "msr", "link" });
}
}

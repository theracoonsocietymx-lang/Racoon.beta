// More of Racoon's own effects:
// Transitorios, De-esser, Clipper, Sub grave, Excitador, Bombeo, Imagen estéreo, Medidor
#include "AllEffects.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;
using racoon::dsp::Follower;

namespace
{
inline float toDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }

// a 4th order Linkwitz-Riley split; low + high adds back to an allpass of the input
struct LR4
{
    Biquad lp[2], hp[2];
    void set (double sr, double f) { for (int s = 0; s < 2; ++s) { lp[s].set (Biquad::lowpass, sr, f, 0.7071); hp[s].set (Biquad::highpass, sr, f, 0.7071); } }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    void split (float x, int c, float& lo, float& hi)
    {
        lo = lp[1].process (lp[0].process (x, c), c);
        hi = hp[1].process (hp[0].process (x, c), c);
    }
    float allpass (float x, int c) { float a, b; split (x, c, a, b); return a + b; }
};

// soft safety at the top: untouched below 0.8, rounds into 1.0
inline float safety (float y)
{
    const float a = std::abs (y);
    if (a < 0.8f) return y;
    return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), y);
}

// ===================================================================== Transitorios
class Transients : public FxBase
{
public:
    Transients() : FxBase ("Transitorios", "Dinámica"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::flt (l, "attack", "Ataque", P::range (-100.0f, 100.0f), 30.0f, " %", 0);
        P::flt (l, "sustain", "Cuerpo", P::range (-100.0f, 100.0f), 0.0f, " %", 0);
        P::ms (l, "speed", "Rapidez", 1.0f, 50.0f, 8.0f, 10.0f);
        P::ms (l, "length", "Largo de la cola", 20.0f, 1000.0f, 200.0f, 200.0f);
        P::hz (l, "split", "Graves sin tocar hasta", 20.0f, 500.0f, 20.0f);
        P::toggle (l, "clip", "Recorte de salida", true);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int) override { gainSm.prepare (rate, 1.0); reset(); }
    void reset() override { xo.reset(); for (auto* f : { &fastA, &slowA, &fastS, &slowS }) f->env = 0.0f; gainSm.reset (1.0f); knee[0].reset(); knee[1].reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float att = value ("attack") * 0.01f, sus = value ("sustain") * 0.01f;
        const float speed = value ("speed"), len = value ("length");
        // attack: a quick follower against one that needs "speed" ms to rise; sustain: a short release against a long one
        fastA.set (sr, 0.3f, 40.0f);  slowA.set (sr, speed, 40.0f);
        fastS.set (sr, 1.0f, 15.0f);  slowS.set (sr, 1.0f, len);
        const float splitHz = value ("split");
        const bool split = splitHz > 21.0f;
        if (split) xo.set (sr, splitHz);
        const bool clip = value ("clip") > 0.5f;
        const float mix = value ("mix") * 0.01f, out = toGain (value ("output"));
        float shown = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float lo[2] {}, hi[2] {};
            float det = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                if (split) xo.split (x, c, lo[c], hi[c]); else hi[c] = x;
                det = jmax (det, std::abs (hi[c]));
            }
            const float a1 = fastA.process (det), a2 = slowA.process (det);
            const float s1 = fastS.process (det), s2 = slowS.process (det);
            const float attDb = jmax (0.0f, toDb (a1) - toDb (a2));      // how far the hit is ahead of the body
            const float susDb = jmax (0.0f, toDb (s2) - toDb (s1));      // how much tail there is after it
            float gDb = att * jmin (attDb, 24.0f) * 1.2f + sus * jmin (susDb, 24.0f) * 1.2f;
            gDb = jlimit (-24.0f, 18.0f, det < 1.0e-5f ? 0.0f : gDb);
            gainSm.target = toGain (gDb);
            const float g = gainSm.next();
            if (std::abs (gDb) > std::abs (shown)) shown = gDb;
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float y = lo[c] + hi[c] * g;
                if (clip) y = knee[c].process (y);   // the safety curve with ADAA
                b.setSample (c, i, (x * (1.0f - mix) + y * mix) * out);
            }
        }
        shownDb.store (shown);
    }
    String displayText() const override { const float v = shownDb.load(); return (v >= 0 ? "+" : "") + String (v, 1) + " dB"; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Golpe y cuerpo", { { "attack", "Ataque", "Más: el golpe salta al frente. Menos: el golpe se suaviza."_u },
                                  { "sustain", "Cuerpo", "Más: se oye más la cola y la sala. Menos: el sonido queda seco y corto."_u },
                                  { "speed", "Rapidez", "Qué tan largo es el golpe que se toca. Corto para baterías electrónicas."_u },
                                  { "length", "Cola", "Qué tanto de la cola cuenta como cuerpo."_u } } },
            { "Salida", { { "split", "Graves", "Debajo de esta frecuencia el sonido pasa igual. En 20 Hz está apagado."_u },
                          { "clip", "Recorte", "Redondea los picos nuevos para que no pasen de 0 dB."_u },
                          { "mix", "Mezcla", "Con la señal original."_u }, { "output", "Salida", "Volumen final."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Bombo con más golpe"_u, "Batería"_u, { { "attack", 60 }, { "speed", 6 }, { "split", 20 } } },
                 { "Caja seca", "Batería"_u, { { "attack", 20 }, { "sustain", -60 }, { "length", 150 } } },
                 { "Sala de batería"_u, "Batería"_u, { { "attack", -10 }, { "sustain", 70 }, { "length", 500 } } },
                 { "Guitarra sin púa"_u, "Instrumentos", { { "attack", -50 }, { "speed", 12 } } } };
    }
private:
    LR4 xo;
    Follower fastA, slowA, fastS, slowS;
    racoon::dsp::Smooth gainSm;
    sdsp::Adaa1<sdsp::curve::Knee> knee[2];
    std::atomic<float> shownDb { 0.0f };
};

// ===================================================================== De-esser
class DeEsser : public FxBase
{
public:
    DeEsser() : FxBase ("De-esser", "Dinámica"_u, layout()) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "freq", "Frecuencia", 2000.0f, 16000.0f, 6500.0f);
        P::db (l, "thresh", "Umbral", -60.0f, 0.0f, -30.0f);
        P::db (l, "range", "Reducción máxima"_u, 0.0f, 24.0f, 10.0f);
        P::choice (l, "detect", "Detección"_u, { "Relativa", "Absoluta" }, 0);
        P::choice (l, "band", "Rango", { "Solo la banda", "Toda la señal"_u }, 0);
        P::choice (l, "filter", "Filtro de detección"_u, { "Paso alto", "Campana" }, 0);
        P::toggle (l, "listen", "Escuchar las eses", false);
        return l;
    }
    void prepare (double rate, int) override { redSm.prepare (rate, 2.0); reset(); }
    void reset() override { for (auto& f : det) f.reset(); for (auto& f : dyn) f.reset(); band.env = full.env = 0.0f; redSm.reset (0.0f); counter = 0; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double f = jmin ((double) value ("freq"), sr * 0.45);
        const bool bell = (int) value ("filter") == 1;
        for (auto& d : det) d.set (bell ? Biquad::bandpass : Biquad::highpass, sr, f, bell ? 1.4 : 0.7071);
        band.set (sr, 0.5f, 60.0f);
        full.set (sr, 0.5f, 60.0f);
        const float thr = value ("thresh"), range = value ("range");
        const bool relative = (int) value ("detect") == 0, wide = (int) value ("band") == 1, listen = value ("listen") > 0.5f;
        float maxRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s[2] {}, lvlS = 0.0f, lvlF = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                s[c] = det[c & 1].process (x, c & 1);
                lvlS = jmax (lvlS, std::abs (s[c]));
                lvlF = jmax (lvlF, std::abs (x));
            }
            const float eb = band.process (lvlS), ef = full.process (lvlF);
            // absolute: the band against the threshold; relative: the band against the whole signal, so a quiet
            // passage with loud esses is caught as well as a loud one
            const float over = relative ? (toDb (eb) - toDb (ef)) - thr / 3.0f : toDb (eb) - thr;
            const float red = eb < 1.0e-5f ? 0.0f : jlimit (0.0f, range, over * 0.8f);
            redSm.target = red;
            const float r = redSm.next();
            maxRed = jmax (maxRed, r);
            if (! wide && (counter++ & 15) == 0)
            {
                const double gainDb = -r;
                for (int c = 0; c < 2; ++c)
                {
                    const auto z1 = dyn[c].z1[c], z2 = dyn[c].z2[c];
                    if (bell) dyn[c].set (Biquad::peak, sr, f, 1.4, gainDb);
                    else dyn[c].set (Biquad::highshelf, sr, f * 0.8, 0.7071, gainDb);
                    dyn[c].z1[c] = z1; dyn[c].z2[c] = z2;
                }
            }
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float y;
                if (listen) y = s[c];
                else if (wide) y = x * toGain (-r);
                else y = dyn[c].process (x, c);
                b.setSample (c, i, y);
            }
        }
        grDb.store (-maxRed);
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Eses", { { "freq", "Frecuencia", "Dónde están las eses de esta voz: casi siempre entre 5 y 9 kHz."_u },
                        { "thresh", "Umbral", "Desde dónde empieza a bajar. Más abajo, más eses toca."_u },
                        { "range", "Máximo"_u, "Lo más que baja una ese, en dB."_u },
                        { "listen", "Escuchar", "Oye solo lo que el detector escucha para encontrar la frecuencia."_u } } },
            { "Detector", { { "detect", "Detección"_u, "Relativa compara las eses con toda la voz; absoluta solo mira su nivel."_u },
                            { "band", "Rango", "Solo la banda baja únicamente las eses; toda la señal baja la voz completa."_u },
                            { "filter", "Filtro", "Paso alto escucha todo lo agudo; campana solo alrededor de la frecuencia."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Voz femenina", "Voces", { { "freq", 7500 }, { "thresh", -28 }, { "range", 8 } } },
                 { "Voz masculina", "Voces", { { "freq", 5500 }, { "thresh", -30 }, { "range", 8 } } },
                 { "Platillos ásperos"_u, "Batería"_u, { { "freq", 9000 }, { "filter", 1 }, { "detect", 1 }, { "thresh", -24 }, { "range", 6 } } } };
    }
private:
    Biquad det[2], dyn[2];
    Follower band, full;
    racoon::dsp::Smooth redSm;
    int counter = 0;
};

// ===================================================================== Clipper
class Clipper : public FxBase
{
public:
    Clipper() : FxBase ("Clipper", "Dinámica"_u, layout()) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "drive", "Drive", 0.0f, 24.0f, 3.0f);
        P::db (l, "ceiling", "Techo", -12.0f, 0.0f, -0.3f);
        P::choice (l, "shape", "Forma", { "Dura", "Suave", "Cúbica"_u, "Analógica"_u }, 1);
        P::choice (l, "os", "Sobremuestreo", { "1x", "2x", "4x", "8x" }, 2);
        P::toggle (l, "delta", "Escuchar lo recortado", false);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -12.0f, 12.0f, 0.0f);
        return l;
    }
    void prepare (double, int block) override
    {
        for (int k = 1; k <= 3; ++k)
        {
            os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, k, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
            os[k]->initProcessing ((size_t) jmax (1, block));
        }
        maxBlock = jmax (1, block);
        // report the latency of the oversampling before the first block, so the host lines the track up from the start
        active = jlimit (0, 3, (int) value ("os"));
        setLatencySamples (active > 0 ? (int) std::round (os[active]->getLatencyInSamples()) : 0);
        reset();
    }
    void reset() override
    {
        for (int k = 1; k <= 3; ++k) if (os[k]) os[k]->reset();
        for (int c = 0; c < 2; ++c) { hardA[c].reset(); tanhA[c].reset(); cubicA[c].reset(); asymA[c].reset(); xPrev[c] = 0.0f; }
    }
    // the four shapes with ADAA: on top of the oversampling, the corners stop folding fizz back into the band
    float shapeAdaa (int shape, float u, int c)
    {
        switch (shape)
        {
            case 0:  return hardA[c].process (u);
            case 1:  return tanhA[c].process (u);
            case 2:  return cubicA[c].process (u);
            default: return asymA[c].process (u);
        }
    }
    static float shapeOf (int shape, float u)
    {
        switch (shape)
        {
            case 0:  return jlimit (-1.0f, 1.0f, u);
            case 1:  return std::tanh (u);
            case 2:  return std::abs (u) >= 1.0f ? std::copysign (1.0f, u) : 1.5f * u - 0.5f * u * u * u;
            default:
            {
                // a little second harmonic: tanh moved off centre, each half scaled back to exactly one
                const float t0 = std::tanh (0.1f), v = std::tanh (u + 0.1f) - t0;
                return v >= 0.0f ? v / (1.0f - t0) : v / (1.0f + t0);
            }
        }
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int want = (int) value ("os");
        if (want != active)
        {
            active = want;
            setLatencySamples (active > 0 && os[active] ? (int) std::round (os[active]->getLatencyInSamples()) : 0);
        }
        const float drive = toGain (value ("drive")), ceil = toGain (value ("ceiling")), out = toGain (value ("output"));
        const int shape = (int) value ("shape");
        const bool delta = value ("delta") > 0.5f;
        const float mix = value ("mix") * 0.01f;
        const int ch = jmin (2, b.getNumChannels());
        float maxRed = 0.0f;
        auto fn = [&] (float x, int c)
        {
            const float u = x * drive / ceil;
            const float y = shapeAdaa (shape, u, c);
            const float clipped = y * ceil;
            // ADAA answers half a sample late: the clean signal is read half a sample late too, so delta and the mix line up
            const float xd = 0.5f * (x + xPrev[c]);
            xPrev[c] = x;
            maxRed = jmax (maxRed, std::abs (u * ceil) - std::abs (shapeOf (shape, u) * ceil));
            // delta: what the ceiling took away, at the level it came in
            const float wet = delta ? xd - clipped / drive : clipped;
            return (xd * (1.0f - mix) + wet * mix) * out;
        };
        if (active == 0 || os[active] == nullptr)
        {
            for (int c = 0; c < ch; ++c) { auto* d = b.getWritePointer (c); for (int i = 0; i < b.getNumSamples(); ++i) d[i] = fn (d[i], c); }
        }
        else
        {
            juce::dsp::AudioBlock<float> all (b);
            auto block = all.getSubsetChannelBlock (0, (size_t) ch);
            for (size_t start = 0; start < block.getNumSamples(); start += (size_t) maxBlock)
            {
                auto part = block.getSubBlock (start, jmin ((size_t) maxBlock, block.getNumSamples() - start));
                auto up = os[active]->processSamplesUp (part);
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    auto* d = up.getChannelPointer (c);
                    for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = fn (d[i], (int) c);
                }
                os[active]->processSamplesDown (part);
            }
        }
        grDb.store (-toDb (1.0f + maxRed));
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            const float ceil = toGain (value ("ceiling"));
            return shapeOf ((int) value ("shape"), x * toGain (value ("drive")) / ceil) * ceil;
        }, [this]
        {
            static const char* os[] { "1x", "2x", "4x", "8x" };
            return String::fromUTF8 ("Techo ") + String (value ("ceiling"), 1) + " dB   " + os[jlimit (0, 3, (int) value ("os"))] + " + ADAA";
        });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Recorte", { { "drive", "Drive", "Empuja la señal contra el techo: más volumen, picos cortados."_u },
                           { "ceiling", "Techo", "Nada pasa de aquí."_u },
                           { "shape", "Forma", "Dura corta en seco; suave y cúbica redondean; analógica agrega algo de color."_u },
                           { "delta", "Delta", "Escucha solo lo que se está cortando, para no pasarte."_u } } },
            { "Calidad", { { "os", "Sobremuestreo", "Más alto, menos aliasing en los agudos. 4x alcanza casi siempre."_u },
                           { "mix", "Mezcla", "Con la señal original."_u }, { "output", "Salida", "Volumen final."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Bombo al frente", "Batería"_u, { { "drive", 6 }, { "shape", 0 }, { "os", 2 } } },
                 { "Master caliente", "Master", { { "drive", 4 }, { "shape", 2 }, { "ceiling", -0.5f }, { "os", 3 } } },
                 { "Bajo con mordida", "Bajo", { { "drive", 10 }, { "shape", 3 }, { "mix", 70 } } } };
    }
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os[4];
    int active = -1, maxBlock = 512;
    sdsp::Adaa1<sdsp::curve::Hard> hardA[2];
    sdsp::Adaa1<sdsp::curve::Tanh> tanhA[2];
    sdsp::Adaa1<sdsp::curve::Cubic15> cubicA[2];
    sdsp::Adaa1<sdsp::curve::AsymClip> asymA[2];
    float xPrev[2] {};
};

// ===================================================================== Sub grave
class SubBass : public FxBase
{
public:
    SubBass() : FxBase ("Sub grave", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "sub", "Sub", 0.0f, 100.0f, 25.0f);
        P::choice (l, "octave", "Octava", { "-1 octava", "-2 octavas" }, 0);
        P::hz (l, "tone", "Tono del sub", 40.0f, 200.0f, 90.0f);
        P::hz (l, "feed", "Banda que lo alimenta", 40.0f, 250.0f, 120.0f);
        P::pct (l, "harm", "Armónicos"_u, 0.0f, 100.0f, 0.0f);
        P::pct (l, "dry", "Original", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void reset() override
    {
        for (auto& f : lowF) f.reset();
        for (auto& f : toneF) f.reset();
        for (auto& f : harmF) f.reset();
        env.env = 0.0f; ff1 = ff2 = 1.0f; above = false;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double feed = value ("feed"), tone = value ("tone");
        for (auto& f : lowF) f.set (Biquad::lowpass, sr, feed, 0.7071);
        for (auto& f : toneF) f.set (Biquad::lowpass, sr, tone, 0.7071);
        harmF[0].set (Biquad::highpass, sr, feed * 1.3, 0.7071);
        harmF[1].set (Biquad::lowpass, sr, jmin (1200.0, sr * 0.4), 0.7071);
        env.set (sr, 3.0f, 60.0f);
        const float sub = value ("sub") * 0.01f, harm = value ("harm") * 0.01f, dry = value ("dry") * 0.01f, out = toGain (value ("output"));
        const bool two = (int) value ("octave") == 1;
        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < ch; ++c) mono += b.getSample (c, i);
            mono /= (float) ch;
            const float low = lowF[1].process (lowF[0].process (mono, 0), 0);
            const float e = env.process (std::abs (low));
            // a flip-flop on every upward zero crossing halves the frequency (a second one halves it again), with
            // a little hysteresis so noise near zero does not trip it
            const float hyst = e * 0.08f + 1.0e-5f;
            if (! above && low > hyst) { above = true; ff1 = -ff1; if (ff1 > 0.0f) ff2 = -ff2; }
            else if (above && low < -hyst) above = false;
            const float sq = two ? ff2 : ff1;
            float s = toneF[1].process (toneF[0].process (sq * e * 1.6f, 0), 0);
            // virtual bass: harmonics of the low band so it reads on small speakers
            const float h = harmF[1].process (harmF[0].process (std::tanh (std::abs (low) * 4.0f) * 0.5f, 0), 0);
            const float add = s * sub + h * harm * 2.0f;
            for (int c = 0; c < ch; ++c) b.setSample (c, i, (b.getSample (c, i) * dry + add) * out);
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Sub", { { "sub", "Sub", "Una octava abajo del bajo que entra, siguiendo su volumen."_u },
                       { "octave", "Octava", "Una o dos octavas abajo."_u }, { "tone", "Tono", "Qué tan redondo queda el sub."_u },
                       { "feed", "Banda", "Solo lo que está debajo de aquí manda al sub."_u } } },
            { "Bocinas chicas", { { "harm", "Armónicos"_u, "Armónicos del grave para que se oiga en un celular o una laptop."_u },
                                  { "dry", "Original", "Cuánto queda del sonido que entra."_u }, { "output", "Salida", "Volumen final."_u } } },
        };
        return s;
    }
private:
    Biquad lowF[2], toneF[2], harmF[2];
    Follower env;
    float ff1 = 1.0f, ff2 = 1.0f;
    bool above = false;
};

// ===================================================================== Excitador
class Exciter : public FxBase
{
public:
    Exciter() : FxBase ("Excitador", "Distorsión y color"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "freq", "Frecuencia", 1000.0f, 12000.0f, 3000.0f);
        P::pct (l, "amount", "Armónicos"_u, 0.0f, 100.0f, 35.0f);
        P::flt (l, "color", "Color (pares a impares)", P::range (-100.0f, 100.0f), 0.0f, "", 0);
        P::db (l, "air", "Aire", 0.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void reset() override { for (auto& f : pre) f.reset(); for (auto& f : post) f.reset(); airF.reset(); for (int c = 0; c < 2; ++c) { odd[c].reset(); dPrev[c] = 0.0f; } }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double f = jmin ((double) value ("freq"), sr * 0.4);
        for (auto& x : pre) x.set (Biquad::highpass, sr, f, 0.7071);
        for (auto& x : post) x.set (Biquad::highpass, sr, f, 0.7071);
        airF.set (Biquad::highshelf, sr, jmin (12000.0, sr * 0.4), 0.7071, value ("air"));
        const float amt = value ("amount") * 0.01f, oddAmt = (value ("color") * 0.01f + 1.0f) * 0.5f, evenAmt = 1.0f - oddAmt;
        const float mix = value ("mix") * 0.01f, out = toGain (value ("output"));
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                const float h = pre[1].process (pre[0].process (x, c), c);
                // new harmonics only: the square makes the even ones, tanh minus the linear part the odd ones
                // both with ADAA (the square's antiderivative is d^3/3, so its average over the step is exact),
                // the linear part read half a sample late like the curves
                const float d = h * 6.0f, dp = dPrev[c];
                dPrev[c] = d;
                const float sq = (d * d + d * dp + dp * dp) * (1.0f / 3.0f);
                const float gen = evenAmt * sq * 0.25f + oddAmt * (odd[c].process (d) - 0.5f * (d + dp) * 0.6f) * 0.5f;
                const float hx = post[1].process (post[0].process (gen, c), c);
                float y = airF.process (x + hx * amt * 2.0f, c);   // 2.0 (was 1.5): without the aliasing the new harmonics measured 2.5 dB lower
                b.setSample (c, i, (x * (1.0f - mix) + y * mix) * out);
            }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = { { "Brillo", { { "freq", "Frecuencia", "Lo que está arriba de aquí recibe armónicos nuevos."_u },
                                     { "amount", "Armónicos"_u, "Cuánto brillo se agrega."_u },
                                     { "color", "Color", "Izquierda: armónicos pares, cálidos. Derecha: impares, más filosos."_u },
                                     { "air", "Aire", "Un estante suave arriba de 12 kHz."_u },
                                     { "mix", "Mezcla", "Con la señal original."_u }, { "output", "Salida", "Volumen final."_u } } } };
        return s;
    }
private:
    Biquad pre[2], post[2], airF;
    sdsp::Adaa1<sdsp::curve::Tanh> odd[2];
    float dPrev[2] {};
};

// ===================================================================== Bombeo
class Pump : public FxBase
{
public:
    Pump() : FxBase ("Bombeo", "Dinámica"_u, layout(), 0.0, true) {}
    static StringArray shapes() { return { "Clásica"_u, "Suave", "Dura", "Rebote", "Rampa", "Corte" }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "shape", "Forma", shapes(), 0);
        P::choice (l, "div", "Ciclo", RacoonProcessor::divisionNames(), 4);
        P::pct (l, "depth", "Profundidad", 0.0f, 100.0f, 80.0f);
        P::flt (l, "curve", "Curva", P::range (-100.0f, 100.0f), 0.0f, "", 0);
        P::pct (l, "offset", "Desfase", 0.0f, 75.0f, 0.0f);
        P::choice (l, "trigger", "Disparo", { "Al tempo", "Por sidechain" }, 0);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    static float curveAt (int shape, float t)
    {
        switch (shape)
        {
            case 0:  return t < 0.015f ? 1.0f - t / 0.015f : jmin (1.0f, (t - 0.015f) / 0.55f);
            case 1:  return 0.5f - 0.5f * std::cos (MathConstants<float>::pi * jmin (1.0f, t / 0.85f));
            case 2:  return t < 0.3f ? 0.0f : jmin (1.0f, (t - 0.3f) / 0.08f);
            case 3:  return t < 0.4f ? t / 0.4f : 1.0f - 0.35f * std::sin (MathConstants<float>::pi * (t - 0.4f) / 0.6f);
            case 4:  return t;
            default: return t < 0.5f ? 0.0f : 1.0f;
        }
    }
    void prepare (double rate, int) override { gainSm.prepare (rate, 1.0); reset(); }
    void reset() override { freePhase = 0.0; gainSm.reset (1.0f); scEnv.env = 0.0f; scArmed = true; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int shape = (int) value ("shape");
        const double cycleSec = syncSeconds ((int) value ("div"), bpm());
        const double beatsPerCycle = cycleSec * bpm() / 60.0;
        const float depth = value ("depth") * 0.01f, k = std::pow (2.0f, value ("curve") / 50.0f);
        const double offset = value ("offset") * 0.01;
        const bool bySc = (int) value ("trigger") == 1 && sidechain (0) != nullptr;
        const float mix = value ("mix") * 0.01f;
        scEnv.set (sr, 0.5f, 80.0f);
        const double beatsPerSample = bpm() / 60.0 / sr;
        for (int i = 0; i < n; ++i)
        {
            double ph;
            if (bySc)
            {
                // the real kick restarts the curve
                float d = 0.0f;
                for (int c = 0; c < 2; ++c) if (auto* s = sidechain (c)) d = jmax (d, std::abs (s[i]));
                const float prev = scEnv.env, e = scEnv.process (d);
                if (scArmed && e > 0.1f && e > prev * 1.5f) { freePhase = 0.0; scArmed = false; }
                if (e < 0.05f) scArmed = true;
                freePhase += 1.0 / (cycleSec * sr);
                ph = jmin (0.9999, freePhase);
            }
            else if (isPlaying())
            {
                const double beats = ppq() + i * beatsPerSample;
                ph = beats / beatsPerCycle;
            }
            else
            {
                freePhase += 1.0 / (cycleSec * sr);
                ph = freePhase;
            }
            if (! bySc) { ph += offset; ph -= std::floor (ph); }
            const float g = std::pow (jlimit (0.0f, 1.0f, curveAt (shape, (float) ph)), k);
            gainSm.target = 1.0f - depth * (1.0f - g);
            const float gain = gainSm.next();
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                b.setSample (c, i, x * (1.0f - mix) + x * gain * mix);
            }
        }
        if (! bySc) freePhase -= std::floor (freePhase);
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Bombeo", { { "shape", "Forma", "Cómo baja y sube el volumen en cada ciclo. Clásica es el sidechain de house."_u },
                          { "div", "Ciclo", "Cada cuánto bombea. Un cuarto es cada bombo en 4/4."_u },
                          { "depth", "Profundidad", "Cuánto baja el volumen."_u }, { "curve", "Curva", "Más: sube tarde y rápido. Menos: sube pronto y lento."_u } } },
            { "Disparo", { { "offset", "Desfase", "Corre la curva dentro del ciclo."_u },
                           { "trigger", "Disparo", "Al tempo sigue la canción; por sidechain la dispara un bombo real conectado."_u },
                           { "mix", "Mezcla", "Con la señal original."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "House a negras", "Sidechain", { { "shape", 0 }, { "div", 4 }, { "depth", 85 } } },
                 { "Pad respirando", "Sidechain", { { "shape", 1 }, { "div", 4 }, { "depth", 60 }, { "curve", -30 } } },
                 { "Bajo cortado", "Sidechain", { { "shape", 2 }, { "div", 4 }, { "depth", 100 } } },
                 { "Corcheas", "Rítmicos"_u, { { "shape", 0 }, { "div", 7 }, { "depth", 70 } } } };
    }
private:
    double freePhase = 0.0;
    racoon::dsp::Smooth gainSm;
    Follower scEnv;
    bool scArmed = true;
};

// ===================================================================== Imagen estéreo
class Imager : public FxBase
{
public:
    Imager() : FxBase ("Imagen estéreo"_u, "Utilidades", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "xlow", "Cruce bajo", 40.0f, 1000.0f, 200.0f);
        P::hz (l, "xhigh", "Cruce alto", 1000.0f, 16000.0f, 4000.0f);
        P::pct (l, "wlow", "Ancho de graves", 0.0f, 200.0f, 0.0f);
        P::pct (l, "wmid", "Ancho de medios", 0.0f, 200.0f, 110.0f);
        P::pct (l, "whigh", "Ancho de agudos", 0.0f, 200.0f, 140.0f);
        P::ms (l, "haas", "Haas", 0.0f, 30.0f, 0.0f, 8.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int) override { haasLine.prepare ((int) (rate * 0.035) + 8); reset(); }
    void reset() override { lowX.reset(); highX.reset(); comp.reset(); haasLine.clear(); sLL = sRR = sLR = 0.0; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples();
        if (b.getNumChannels() < 2) return;
        const double xl = value ("xlow"), xh = jmax ((double) value ("xhigh"), xl * 1.5);
        lowX.set (sr, xl); highX.set (sr, xh); comp.set (sr, xh);
        const float w[3] { value ("wlow") * 0.01f, value ("wmid") * 0.01f, value ("whigh") * 0.01f };
        const float haas = value ("haas") * 0.001f * (float) sr, out = toGain (value ("output"));
        const double decay = std::exp (-1.0 / (0.3 * sr));
        for (int i = 0; i < n; ++i)
        {
            float bands[3][2];
            for (int c = 0; c < 2; ++c)
            {
                float lo, rest, mid, hi;
                lowX.split (b.getSample (c, i), c, lo, rest);
                highX.split (rest, c, mid, hi);
                bands[0][c] = comp.allpass (lo, c);   // the lows turn their phase like the rest, so the sum stays flat
                bands[1][c] = mid; bands[2][c] = hi;
            }
            float L = 0.0f, R = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float m = (bands[k][0] + bands[k][1]) * 0.5f, s = (bands[k][0] - bands[k][1]) * 0.5f * w[k];
                L += m + s; R += m - s;
            }
            if (haas > 0.5f) { haasLine.push (R); R = haasLine.tap (haas); }
            else haasLine.push (R);
            L *= out; R *= out;
            b.setSample (0, i, L); b.setSample (1, i, R);
            sLL = sLL * decay + L * L; sRR = sRR * decay + R * R; sLR = sLR * decay + L * R;
        }
        corr.store ((float) (sLR / std::sqrt (sLL * sRR + 1.0e-12)));
    }
    String displayText() const override { return "Correlación "_u + String (corr.load(), 2); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Ancho por banda", { { "wlow", "Graves", "0 % deja los graves en mono, como en un disco de club."_u },
                                   { "wmid", "Medios", "100 % es como entra. Más abre, menos cierra."_u },
                                   { "whigh", "Agudos", "Abrir los agudos da aire sin perder el centro."_u },
                                   { "haas", "Haas", "Retrasa un lado unos milisegundos para abrir un sonido mono. Revisa en mono."_u } } },
            { "Cruces", { { "xlow", "Cruce bajo", "Dónde terminan los graves."_u }, { "xhigh", "Cruce alto", "Dónde empiezan los agudos."_u },
                          { "output", "Salida", "Volumen final."_u } }, true },
        };
        return s;
    }
private:
    LR4 lowX, highX, comp;
    racoon::dsp::DelayLine haasLine;
    double sLL = 0, sRR = 0, sLR = 0;
    std::atomic<float> corr { 1.0f };
};

// ===================================================================== Medidor (loudness, ITU-R BS.1770-4 / EBU R 128)
class LoudnessMeter : public FxBase
{
public:
    LoudnessMeter() : FxBase ("Medidor", "Utilidades", layout(), 0.0, false, false, false) {}
    static StringArray targets() { return { "-14 LUFS (streaming)", "-9 LUFS (club)", "-16 LUFS (podcast)", "-23 LUFS (televisión)"_u }; }
    static constexpr float targetValue (int i) { return i == 1 ? -9.0f : i == 2 ? -16.0f : i == 3 ? -23.0f : -14.0f; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "target", "Objetivo", targets(), 0);
        P::toggle (l, "reset", "Reiniciar", false);
        return l;
    }
    void prepare (double rate, int block) override
    {
        blockLen = jmax (1, (int) (rate * 0.1));
        tp = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
        tp->initProcessing ((size_t) jmax (1, block));
        scratch.setSize (2, jmax (1, block));
        clearAll();
    }
    void reset() override { clearAll(); }
    void clearAll()
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : hpf) f.reset();
        if (tp) tp->reset();
        std::fill (std::begin (ring), std::end (ring), 0.0);
        ringPos = 0; filled = 0; acc = 0.0; accN = 0;
        std::fill (std::begin (histI), std::end (histI), 0); std::fill (std::begin (histIe), std::end (histIe), 0.0);
        std::fill (std::begin (histS), std::end (histS), 0);
        peakTp = 0.0f; sLL = sRR = sLR = 0.0;
        mom.store (-100.0f); shortT.store (-100.0f); integ.store (-100.0f); lra.store (0.0f); truePeak.store (-100.0f); corr.store (1.0f);
    }
    static float lufsOf (double meanSquare) { return meanSquare <= 1.0e-12 ? -100.0f : (float) (-0.691 + 10.0 * std::log10 (meanSquare)); }
    static int bin (float lufs) { return jlimit (0, bins - 1, (int) std::floor ((lufs + 70.0f) * 10.0f)); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const bool rst = value ("reset") > 0.5f;
        if (rst && ! resetWas) clearAll();
        resetWas = rst;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        // K weighting: a +4 dB shelf at 1.68 kHz and a high-pass at 38 Hz
        for (auto& f : shelf) f.set (Biquad::highshelf, sr, 1681.97, 0.7071752, 3.99984);
        for (auto& f : hpf) f.set (Biquad::highpass, sr, 38.1355, 0.5003270);
        const double decay = std::exp (-1.0 / (0.3 * sr));
        for (int i = 0; i < n; ++i)
        {
            double e = 0.0;
            for (int c = 0; c < ch; ++c)
            {
                const float k = hpf[c].process (shelf[c].process (b.getSample (c, i), c), c);
                e += (double) k * k;
            }
            if (ch == 1) e *= 2.0;
            const float L = b.getSample (0, i), R = ch > 1 ? b.getSample (1, i) : L;
            sLL = sLL * decay + L * L; sRR = sRR * decay + R * R; sLR = sLR * decay + L * R;
            acc += e;
            if (++accN >= blockLen) closeBlock();
        }
        // true peak: 4x oversampled
        if (tp != nullptr && n <= scratch.getNumSamples())
        {
            for (int c = 0; c < 2; ++c) scratch.copyFrom (c, 0, b, jmin (c, ch - 1), 0, n);
            juce::dsp::AudioBlock<float> blk (scratch.getArrayOfWritePointers(), 2, (size_t) n);
            auto up = tp->processSamplesUp (blk);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i) peakTp = jmax (peakTp, std::abs (d[i]));
            }
            truePeak.store (toDb (peakTp));
        }
        corr.store ((float) (sLR / std::sqrt (sLL * sRR + 1.0e-12)));
    }
    void closeBlock()
    {
        ring[ringPos] = acc / (double) accN;
        ringPos = (ringPos + 1) % ringSize;
        filled = jmin (filled + 1, ringSize);
        acc = 0.0; accN = 0;
        auto meanOf = [this] (int blocks)
        {
            double s = 0.0;
            const int k = jmin (blocks, filled);
            for (int j = 1; j <= k; ++j) s += ring[(ringPos - j + ringSize) % ringSize];
            return k > 0 ? s / k : 0.0;
        };
        const float m = lufsOf (meanOf (4)), st = lufsOf (meanOf (30));
        mom.store (m);
        shortT.store (st);
        // integrated: 400 ms blocks every 100 ms, absolute gate at -70, relative gate 10 LU under the gated mean
        if (filled >= 4 && m > -70.0f) { ++histI[bin (m)]; histIe[bin (m)] += std::pow (10.0, (m + 0.691) / 10.0); }
        if (filled >= 30 && st > -70.0f) ++histS[bin (st)];
        double sum = 0.0; long cnt = 0;
        for (int k = 0; k < bins; ++k) { sum += histIe[k]; cnt += histI[k]; }
        if (cnt > 0)
        {
            const float rel = lufsOf (sum / (double) cnt) - 10.0f;
            double s2 = 0.0; long c2 = 0;
            for (int k = bin (rel); k < bins; ++k) { s2 += histIe[k]; c2 += histI[k]; }
            if (c2 > 0) integ.store (lufsOf (s2 / (double) c2));
        }
        // loudness range: 10th to 95th percentile of the short-term values above a gate 20 LU under them
        long total = 0;
        for (auto v : histS) total += v;
        if (total > 10)
        {
            double se = 0.0; for (int k = 0; k < bins; ++k) se += histS[k] * std::pow (10.0, ((k / 10.0f - 70.0f) + 0.691) / 10.0);
            const int gate = bin (lufsOf (se / (double) total) - 20.0f);
            long g = 0; for (int k = gate; k < bins; ++k) g += histS[k];
            if (g > 0)
            {
                long run = 0; int lo = -1, hi = -1;
                for (int k = gate; k < bins; ++k)
                {
                    run += histS[k];
                    if (lo < 0 && run >= g * 0.10) lo = k;
                    if (hi < 0 && run >= g * 0.95) hi = k;
                }
                if (lo >= 0 && hi >= 0) lra.store ((hi - lo) / 10.0f);
            }
        }
    }
    std::unique_ptr<Component> makeDisplay() override;
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.displayEasy = 320;
        s.sections = { { "Medidor", { { "target", "Objetivo", "La sonoridad a la que apuntas: la barra se pinta contra este valor."_u },
                                      { "reset", "Reiniciar", "Empieza de cero la medición integrada."_u } } } };
        return s;
    }
    std::atomic<float> mom { -100.0f }, shortT { -100.0f }, integ { -100.0f }, lra { 0.0f }, truePeak { -100.0f }, corr { 1.0f };
private:
    static constexpr int ringSize = 30, bins = 760;
    Biquad shelf[2], hpf[2];
    double ring[ringSize] {};
    int ringPos = 0, filled = 0, blockLen = 4800, accN = 0;
    double acc = 0.0;
    int histI[bins] {}, histS[bins] {};
    double histIe[bins] {};
    double sLL = 0, sRR = 0, sLR = 0;
    float peakTp = 0.0f;
    bool resetWas = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> tp;
    AudioBuffer<float> scratch;
};

class LoudnessDisplay : public Component, private Timer
{
public:
    explicit LoudnessDisplay (LoudnessMeter& m) : meter (m) { startTimerHz (15); }
    void paint (Graphics& g) override
    {
        using namespace racoon::ui;
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (18.0f, 14.0f);
        const float target = LoudnessMeter::targetValue ((int) meter.value ("target"));
        auto fmt = [] (float v, const char* unit) { return v <= -99.0f ? String ("--") : String (v, 1) + unit; };
        // three big numbers
        auto top = r.removeFromTop (r.getHeight() * 0.55f);
        const float w = top.getWidth() / 3.0f;
        auto number = [&] (Rectangle<float> a, const String& label, float v, bool main)
        {
            g.setColour (Pro::textDim);
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr (label), a.removeFromTop (16), Justification::centredLeft);
            const bool hot = v > target + 1.0f;
            g.setColour (main ? (hot ? Pro::reduce : Pro::value) : Pro::text);
            g.setFont (NumFont::semi (main ? 40.0f : 30.0f));
            g.drawText (::racoon::i18n::tr (fmt (v, "")), a.removeFromTop (a.getHeight() * 0.7f), Justification::centredLeft);
            g.setColour (Pro::textDim);
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr ("LUFS"), a, Justification::topLeft);
        };
        number (top.removeFromLeft (w), "INTEGRADO", meter.integ.load(), true);
        number (top.removeFromLeft (w), "CORTO (3 s)", meter.shortT.load(), false);
        number (top, "MOMENTÁNEO"_u, meter.mom.load(), false);
        // bar against the target
        auto bar = r.removeFromTop (22.0f);
        g.setColour (Pro::raised);
        g.fillRect (bar);
        auto pos = [&] (float lufs) { return bar.getX() + bar.getWidth() * jlimit (0.0f, 1.0f, (lufs + 40.0f) / 40.0f); };
        const float st = meter.shortT.load();
        g.setColour (st > target + 1.0f ? Pro::reduce : Pro::input);
        g.fillRect (bar.withRight (pos (st)));
        g.setColour (Pro::text);
        g.fillRect (Rectangle<float> (pos (target) - 1.0f, bar.getY() - 4.0f, 2.0f, bar.getHeight() + 8.0f));
        r.removeFromTop (10.0f);
        g.setFont (Fonts::label (13.0f));
        g.setColour (Pro::text);
        const float tpv = meter.truePeak.load();
        g.drawText (::racoon::i18n::tr ("Pico real " + fmt (tpv, " dBTP") + "     Rango " + String (meter.lra.load(), 1) + " LU     Correlación "_u + String (meter.corr.load(), 2)
                    + "     Objetivo " + String (target, 0) + " LUFS"), r.removeFromTop (20.0f), Justification::centredLeft);
        if (tpv > -1.0f)
        {
            g.setColour (Pro::reduce);
            g.drawText (::racoon::i18n::tr ("El pico real pasa de -1 dBTP: las plataformas lo pueden distorsionar al convertirlo."), r.removeFromTop (20.0f), Justification::centredLeft);
        }
    }
private:
    LoudnessMeter& meter;
    void timerCallback() override { repaint(); }
};

std::unique_ptr<Component> LoudnessMeter::makeDisplay() { return std::make_unique<LoudnessDisplay> (*this); }
}

// what the loudness meter reads (0 momentary, 1 short-term, 2 integrated, 3 range, 4 true peak, 5 correlation); for the tests
float meterReading (RacoonProcessor& p, int which)
{
    auto* m = dynamic_cast<LoudnessMeter*> (&p);
    if (m == nullptr) return 0.0f;
    switch (which)
    {
        case 0: return m->mom.load();
        case 1: return m->shortT.load();
        case 2: return m->integ.load();
        case 3: return m->lra.load();
        case 4: return m->truePeak.load();
        default: return m->corr.load();
    }
}

void registerExtras1 (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& cat, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, cat, detail, Kind::effect, std::move (make), keys }); };
    add ("transitorios", "Transitorios", "Dinámica"_u, "Más golpe o más cuerpo en baterías, sin umbral: sube o baja el ataque y la cola."_u,
         [] { return std::make_unique<Transients>(); }, { "attack", "sustain", "speed", "split", "mix", "output" });
    add ("de-esser", "De-esser", "Dinámica"_u, "Baja las eses y los siseos de una voz sin tocar lo demás."_u,
         [] { return std::make_unique<DeEsser>(); }, { "freq", "thresh", "range", "detect", "band", "listen" });
    add ("clipper", "Clipper", "Dinámica"_u, "Corta los picos contra un techo: más volumen sin el bombeo de un limitador."_u,
         [] { return std::make_unique<Clipper>(); }, { "drive", "ceiling", "shape", "delta", "mix", "output" });
    add ("bombeo", "Bombeo", "Dinámica"_u, "El sidechain sin sidechain: baja y sube el volumen al tempo con la forma que elijas."_u,
         [] { return std::make_unique<Pump>(); }, { "shape", "div", "depth", "curve", "offset", "mix" });
    add ("sub-grave", "Sub grave", "Distorsión y color"_u, "Una octava abajo del bajo, y armónicos para que se oiga en bocinas chicas."_u,
         [] { return std::make_unique<SubBass>(); }, { "sub", "octave", "tone", "feed", "harm", "dry" });
    add ("excitador", "Excitador", "Distorsión y color"_u, "Brillo con armónicos nuevos arriba de una frecuencia, no con EQ."_u,
         [] { return std::make_unique<Exciter>(); }, { "freq", "amount", "color", "air", "mix", "output" });
    add ("imagen-estereo", "Imagen estéreo"_u, "Utilidades", "Ancho por bandas: graves al centro, agudos abiertos, y Haas para lo mono."_u,
         [] { return std::make_unique<Imager>(); }, { "wlow", "wmid", "whigh", "haas", "xlow", "xhigh" });
    add ("medidor", "Medidor", "Utilidades", "LUFS integrado, corto y momentáneo, pico real, rango y correlación."_u,
         [] { return std::make_unique<LoudnessMeter>(); }, { "target", "reset" });
}
}

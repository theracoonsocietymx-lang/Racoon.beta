// Dynamics: Puerta, Compresor de bus, Limitador, Dinámica multibanda, Bus de batería
#include "AllEffects.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using racoon::dsp::Follower;

namespace
{
inline float toDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }

// ===================================================================== Puerta (gate)
class Gate : public FxBase
{
public:
    Gate() : FxBase ("Puerta", "Dinámica"_u, layout(), 0.0, true) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "thresh", "Umbral", -80.0f, 0.0f, -40.0f);
        P::db (l, "hyst", "Histéresis"_u, 0.0f, 24.0f, 3.0f);
        P::ms (l, "attack", "Ataque", 0.02f, 150.0f, 0.1f, 5.0f);
        P::ms (l, "hold", "Hold", 0.0f, 500.0f, 10.0f, 50.0f);
        P::ms (l, "release", "Relajación"_u, 1.0f, 2000.0f, 100.0f, 200.0f);
        P::db (l, "floor", "Rango", -80.0f, 0.0f, -80.0f);
        P::toggle (l, "flip", "Invertir (ducking)", false);
        P::toggle (l, "scext", "Sidechain externo", false);
        P::hz (l, "schp", "Detector: corte de graves", 20.0f, 2000.0f, 20.0f);
        P::hz (l, "sclp", "Detector: corte de agudos", 200.0f, 20000.0f, 20000.0f);
        // at the top it is a gate; lower, a downward expander that turns quiet things down gradually
        P::flt (l, "ratio", "Ratio", P::range (1.5f, 20.0f, 4.0f), 20.0f, ":1", 1);
        return l;
    }
    void prepare (double, int) override { reset(); }
    void reset() override { env = 0.0f; gain = 0.0f; holdLeft = 0; open = false; for (auto& f : det) f.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        const float thr = toGain (value ("thresh")), close = toGain (value ("thresh") - value ("hyst"));
        const float a = dsp2::coeffMs (sr, value ("attack")), r = dsp2::coeffMs (sr, value ("release"));
        const int holdN = (int) (value ("hold") * 0.001 * sr);
        const float floorG = value ("floor") <= -79.5f ? 0.0f : toGain (value ("floor"));
        const bool flip = value ("flip") > 0.5f, ext = value ("scext") > 0.5f && sidechain (0) != nullptr;
        const float ratio = value ("ratio");
        const bool expander = ratio < 19.9f;
        det[0].set (dsp2::Biquad::highpass, sr, value ("schp"), 0.707);
        det[1].set (dsp2::Biquad::lowpass, sr, value ("sclp"), 0.707);
        const float envFall = dsp2::coeffMs (sr, 45.35);   // the level the gate watches falls in about 45 ms, at any sample rate
        float minG = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            // the detector filters work on the audio of each channel (before, they got the already rectified level,
            // so the low and high cut of the detector did not do what they say); then the loudest channel
            float x = 0.0f;
            for (int c = 0; c < jmin (2, ch); ++c)
            {
                const float in = ext ? sidechain (c)[i] : b.getSample (c, i);
                x = jmax (x, std::abs (det[1].process (det[0].process (in, c), c)));
            }
            env = jmax (x, env * envFall);
            if (env > thr) { open = true; holdLeft = holdN; }
            else if (env < close) { if (holdLeft > 0) --holdLeft; else open = false; }
            float target = (open != flip) ? 1.0f : floorG;
            if (expander && ! open)
            {
                const float e = jmax (1.0e-6f, env) / thr;
                const float down = jmax (floorG, std::pow (jmin (1.0f, e), ratio - 1.0f));
                target = flip ? 1.0f : down;
            }
            gain = target > gain ? target + (gain - target) * a : target + (gain - target) * r;
            for (int c = 0; c < ch; ++c) b.setSample (c, i, b.getSample (c, i) * gain);
            minG = jmin (minG, gain);
        }
        grDb.store (toDb (jmax (minG, 1.0e-4f)));
    }
    String displayText() const override { return open ? "ABIERTA" : "CERRADA"; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Puerta", { { "thresh", "Umbral", "Por debajo de este nivel la puerta se cierra y calla el sonido." },
                          { "attack", "Ataque", "Qué tan rápido abre."_u }, { "hold", "Hold", "Cuánto se queda abierta después de bajar del umbral."_u },
                          { "release", "Relajación"_u, "Qué tan rápido cierra."_u }, { "floor", "Rango", "Cuánto baja al cerrar. Al mínimo calla del todo."_u },
                          { "ratio", "Ratio", "Arriba es puerta. Más abajo es expansor: lo bajito baja poco a poco en vez de cortarse."_u } } },
            { "Detector", { { "hyst", "Histéresis"_u, "Diferencia entre el nivel que abre y el que cierra; evita que tiemble." },
                            { "flip", "Invertir", "Al revés: baja el sonido cuando la señal pasa el umbral (ducking)."_u },
                            { "scext", "Sidechain", "Escucha otra pista para decidir cuándo abrir."_u },
                            { "schp", "Graves", "Filtra los graves de lo que escucha el detector." }, { "sclp", "Agudos", "Filtra los agudos de lo que escucha el detector." } }, true },
        };
        return s;
    }
private:
    float env = 0.0f, gain = 0.0f;
    int holdLeft = 0;
    bool open = false;
    dsp2::Biquad det[2];
};

// ===================================================================== Compresor de bus (glue)
class Glue : public FxBase
{
public:
    Glue() : FxBase ("Compresor de bus", "Dinámica"_u, layout(), 0.0, true) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "thresh", "Umbral", -40.0f, 0.0f, -15.0f);
        P::choice (l, "ratio", "Ratio", { "2:1", "4:1", "10:1" }, 0);
        P::choice (l, "attack", "Ataque", { "0.01 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 4);
        P::choice (l, "release", "Relajación"_u, { "0.1 s", "0.2 s", "0.4 s", "0.6 s", "0.8 s", "1.2 s", "Auto" }, 2);
        P::db (l, "makeup", "Ganancia", 0.0f, 20.0f, 0.0f);
        P::db (l, "range", "Rango", 0.0f, 70.0f, 70.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::toggle (l, "clip", "Soft clip", false);
        P::choice (l, "schp", "Detector: corte de graves", { "Apagado", "60 Hz", "90 Hz", "150 Hz" }, 0);
        P::toggle (l, "scext", "Sidechain externo", false);
        return l;
    }
    void reset() override { envDb = 0.0f; slow = 0.0f; for (auto& f : hp) f.reset(); for (auto& a : clipper) a.reset(); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        static const float ratios[] { 2.0f, 4.0f, 10.0f }, atks[] { 0.01f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f }, rels[] { 100, 200, 400, 600, 800, 1200, 0 };
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        const float thr = value ("thresh"), ratio = ratios[(int) value ("ratio")];
        const float atk = dsp2::coeffMs (sr, atks[(int) value ("attack")]);
        const int ri = (int) value ("release");
        const bool autoRel = ri == 6;
        const float rel = dsp2::coeffMs (sr, autoRel ? 600.0f : rels[ri]), relFast = dsp2::coeffMs (sr, 80.0), relSlow = dsp2::coeffMs (sr, 1500.0);
        const float range = value ("range"), makeup = value ("makeup"), mix = value ("mix") * 0.01f;
        const bool clip = value ("clip") > 0.5f, ext = value ("scext") > 0.5f && sidechain (0) != nullptr;
        static const float hpf[] { 0, 60, 90, 150 };
        const int hpi = (int) value ("schp");
        if (hpi > 0) for (auto& f : hp) f.set (dsp2::Biquad::highpass, sr, hpf[hpi], 0.707);
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float d = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                float x = ext ? sidechain (c)[i] : b.getSample (c, i);
                if (hpi > 0) x = hp[c & 1].process (x, c & 1);
                d = jmax (d, std::abs (x));
            }
            const float lvl = toDb (d);
            // soft knee of 6 dB around the threshold
            const float over = lvl - thr;
            float gr = 0.0f;
            if (over > 3.0f) gr = over * (1.0f - 1.0f / ratio);
            else if (over > -3.0f) gr = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
            gr = jmin (gr, range);
            if (gr > envDb) envDb = gr + (envDb - gr) * atk;
            else
            {
                if (autoRel)
                {
                    // two release times: quick after short peaks, slow after long reduction
                    slow = gr + (slow - gr) * relSlow;
                    const float fast = gr + (envDb - gr) * relFast;
                    envDb = jmax (fast, slow * 0.6f);
                }
                else envDb = gr + (envDb - gr) * rel;
            }
            maxGr = jmax (maxGr, envDb);
            const float g = toGain (makeup - envDb);
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float y = x * g;
                if (clip) y = clipper[c].process (y);   // soft clip with ADAA: round peaks without the fizz
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
        grDb.store (-maxGr);
    }
    // the screen: transfer curve with the soft knee, and 4 s of in / out / reduction
    std::unique_ptr<Component> makeDisplay() override
    {
        dsp2::CompressorView v;
        v.threshold = [this] { return value ("thresh"); };
        v.curve = [this] (float in)
        {
            static const float ratios[] { 2.0f, 4.0f, 10.0f };
            const float thr = value ("thresh"), ratio = ratios[jlimit (0, 2, (int) value ("ratio"))];
            const float over = in - thr;
            float gr = 0.0f;
            if (over > 3.0f) gr = over * (1.0f - 1.0f / ratio);
            else if (over > -3.0f) gr = (1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
            return in - jmin (gr, value ("range")) + value ("makeup");
        };
        v.caption = [this]
        {
            static const char* r[] { "2:1", "4:1", "10:1" };
            return String (r[jlimit (0, 2, (int) value ("ratio"))]) + String::fromUTF8 ("   rodilla 6 dB   +") + String (value ("makeup"), 1) + " dB";
        };
        return dsp2::makeCompressorDisplay (*this, std::move (v));
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Compresor de bus", { { "thresh", "Umbral", "Desde qué nivel empieza a pegar la mezcla."_u }, { "ratio", "Ratio", "Cuánto comprime: 2:1 suave, 10:1 fuerte."_u },
                                    { "attack", "Ataque", "Tiempo en reaccionar. Más lento deja pasar el golpe."_u },
                                    { "release", "Relajación"_u, "Tiempo en soltar. Auto se adapta a la música."_u },
                                    { "makeup", "Ganancia", "Recupera el volumen perdido." }, { "mix", "Mezcla", "Compresión en paralelo: mezcla con la señal sin comprimir."_u } } },
            { "Detalles", { { "range", "Rango", "Máximo que puede bajar."_u }, { "clip", "Soft clip", "Redondea los picos por encima de 0 dB." },
                            { "schp", "Graves", "Que el bombo no haga bombear todo." }, { "scext", "Sidechain", "Comprime según otra pista."_u } }, true },
        };
        return s;
    }
private:
    float envDb = 0.0f, slow = 0.0f;
    dsp2::Biquad hp[2];
    sdsp::Adaa1<sdsp::curve::Tanh> clipper[2];
};

// ===================================================================== Limitador
class Limiter : public FxBase
{
public:
    Limiter() : FxBase ("Limitador", "Dinámica"_u, layout()) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "gain", "Ganancia", 0.0f, 24.0f, 0.0f);
        P::db (l, "ceiling", "Techo", -24.0f, 0.0f, -0.3f);
        P::ms (l, "release", "Relajación"_u, 1.0f, 1000.0f, 80.0f, 100.0f);
        P::choice (l, "look", "Anticipación"_u, { "1.5 ms", "3 ms", "6 ms" }, 1);
        P::toggle (l, "link", "Estéreo enlazado"_u, true);
        P::toggle (l, "truepeak", "Picos reales (true peak)", true);
        return l;
    }
    void prepare (double rate, int) override
    {
        maxLook = (int) std::ceil (rate * 0.006) + 2;
        for (auto& d : delay) d.assign ((size_t) maxLook + 1, 0.0f);
        for (auto& d : det)
        {
            d.avg.assign ((size_t) maxLook + 1, 1.0f);
            d.dqv.assign ((size_t) maxLook + 2, 0.0f);
            d.dqi.assign ((size_t) maxLook + 2, 0);
        }
        setLook();
        reset();
    }
    void setLook()
    {
        static const double ms[] { 1.5, 3.0, 6.0 };
        look = jlimit (1, maxLook, (int) std::round (ms[(int) value ("look")] * 0.001 * sr));
        setLatencySamples (look);
    }
    void reset() override
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : det) d.reset (look);
        w = 0; counter = 0;
        for (auto& p : prev) for (auto& v : p) v = 0.0f;
    }
    float truePeak (int c, float x)
    {
        // estimate the peaks between samples with a cubic through the last four samples
        auto& p = prev[c];
        const float xm1 = p[0], x0 = p[1], x1 = p[2], x2 = x;
        p[0] = p[1]; p[1] = p[2]; p[2] = x;
        float m = std::abs (x1);
        for (float t : { 0.25f, 0.5f, 0.75f })
        {
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            m = jmax (m, std::abs (((c3 * t + c2) * t + c1) * t + x0));
        }
        return m;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int oldLook = look;
        const bool oldLinked = linked;
        setLook();
        linked = value ("link") > 0.5f;
        if (look != oldLook || linked != oldLinked) reset();
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float in = toGain (value ("gain")), ceil = toGain (value ("ceiling"));
        const float rel = dsp2::coeffMs (sr, value ("release"));
        const bool tp = value ("truepeak") > 0.5f;
        const int size = (int) delay[0].size();
        // linked: both sides follow the louder one and the image stays put; unlinked: each side has its own gain
        const int detectors = linked ? 1 : ch;
        float minG = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float pk[2] {};
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i) * in;
                delay[c][(size_t) w] = x;
                pk[c] = tp ? truePeak (c, x) : std::abs (x);
            }
            if (linked && ch > 1) pk[0] = jmax (pk[0], pk[1]);
            float g[2] {};
            for (int d = 0; d < detectors; ++d)
                g[d] = det[d].step (pk[d] > ceil ? ceil / pk[d] : 1.0f, rel, look, w, counter);
            if (linked) g[1] = g[0];
            const int rd = (w - look + size * 2) % size;   // exactly `look` samples late
            for (int c = 0; c < ch; ++c)
            {
                float y = delay[c][(size_t) rd] * g[c];
                y = jlimit (-ceil, ceil, y);   // last safety: the ceiling is never crossed
                b.setSample (c, i, y);
                minG = jmin (minG, g[c]);
            }
            w = (w + 1) % size;
            ++counter;
        }
        for (int d = 0; d < detectors; ++d) det[d].resum (look, w, size);
        grDb.store (toDb (minG));
    }
    // the screen: what goes in against what comes out (the ceiling is the flat top) and 4 s of reduction
    std::unique_ptr<Component> makeDisplay() override
    {
        dsp2::CompressorView v;
        v.threshold = [this] { return value ("ceiling") - value ("gain"); };
        v.curve = [this] (float in) { return jmin (in + value ("gain"), value ("ceiling")); };
        v.caption = [this] { return String::fromUTF8 ("Techo ") + String (value ("ceiling"), 1) + " dB" + (value ("truepeak") > 0.5f ? "   true peak" : ""); };
        return dsp2::makeCompressorDisplay (*this, std::move (v));
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Limitador", { { "gain", "Ganancia", "Sube el volumen hacia el techo." }, { "ceiling", "Techo", "El nivel que nunca se pasa. -0.3 dB es seguro para distribuir." },
                             { "release", "Relajación"_u, "Qué tan rápido suelta después de un pico."_u } } },
            { "Detalles", { { "look", "Anticipación"_u, "Cuánto ve hacia adelante para bajar suave. Más largo, más transparente."_u },
                            { "link", "Enlazado", "Los dos lados bajan juntos para que la imagen no se mueva." },
                            { "truepeak", "True peak", "También vigila los picos entre muestras."_u } }, true },
        };
        return s;
    }
private:
    // the gain one side needs: sliding minimum over the look-ahead window, slow release, moving-average attack
    struct Detector
    {
        std::vector<float> avg, dqv;
        std::vector<int> dqi;
        int head = 0, tail = 0;
        float held = 1.0f;
        double avgSum = 0.0;
        void reset (int look)
        {
            std::fill (avg.begin(), avg.end(), 1.0f);
            head = tail = 0; held = 1.0f; avgSum = (double) look;
        }
        float step (float r, float rel, int look, int w, int counter)
        {
            const int qs = (int) dqv.size();
            // sliding minimum of the needed gain (monotonic queue, no allocation)
            while (tail > head && dqv[(size_t) ((tail - 1) % qs)] >= r) --tail;
            dqv[(size_t) (tail % qs)] = r; dqi[(size_t) (tail % qs)] = counter; ++tail;
            while (dqi[(size_t) (head % qs)] < counter - look) ++head;   // window of look + 1 samples
            const float winMin = dqv[(size_t) (head % qs)];
            // release back up slowly, never above the window minimum
            held = jmin (winMin, winMin + (held - winMin) * rel);
            // moving average over the window: smooth attack that still reaches the minimum at the peak
            avgSum += held - avg[(size_t) w];
            avg[(size_t) w] = held;
            return jmin (1.0f, (float) (avgSum / look));
        }
        void resum (int look, int w, int size)
        {
            // the average uses the last `look` entries only: keep the sum honest
            double s = 0; for (int k = 0; k < look; ++k) s += avg[(size_t) ((w - 1 - k + size * 2) % size)];
            avgSum = s;
        }
    };
    std::vector<float> delay[2];
    Detector det[2];
    int maxLook = 64, look = 64, w = 0, counter = 0;
    bool linked = true;
    float prev[2][3] {};
};

// ===================================================================== Dinámica multibanda
class Multiband : public FxBase
{
public:
    Multiband() : FxBase ("Dinámica multibanda"_u, "Dinámica"_u, layout(), 0.0, true) { showsGR = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "xlow", "Cruce graves", 30.0f, 1000.0f, 120.0f);
        P::hz (l, "xhigh", "Cruce agudos", 500.0f, 15000.0f, 2500.0f);
        static const char* bandNames[] { "Graves", "Medios", "Agudos" };
        for (int k = 0; k < 3; ++k)
        {
            const String id = "b" + String (k + 1), n = String (bandNames[k]) + " ";
            P::db (l, id + "_ta", n + "umbral arriba", -60.0f, 0.0f, -24.0f);
            P::flt (l, id + "_ra", n + "ratio arriba", P::range (1.0f, 20.0f, 3.0f), 3.0f, ":1", 1);
            P::db (l, id + "_tb", n + "umbral abajo", -80.0f, 0.0f, -50.0f);
            P::flt (l, id + "_rb", n + "ratio abajo", P::range (0.3f, 4.0f, 1.0f), 1.3f, ":1", 2);
            P::db (l, id + "_gain", n + "ganancia", -24.0f, 24.0f, 0.0f);
        }
        P::pct (l, "amount", "Cantidad", 0.0f, 100.0f, 100.0f);
        P::flt (l, "time", "Tiempo", P::range (10.0f, 1000.0f, 100.0f), 100.0f, "%", 0);
        P::db (l, "output", "Salida", -24.0f, 24.0f, 0.0f);
        return l;
    }
    void reset() override { for (auto& f : lp1) f.reset(); for (auto& f : hp1) f.reset(); for (auto& f : lp2) f.reset(); for (auto& f : hp2) f.reset(); for (auto& f : apLp) f.reset(); for (auto& f : apHp) f.reset(); for (auto& e : env) e = -120.0f; for (auto& g : gdb) g = 0.0f; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        // the crossovers glide to a new place (a jump in the filters' coefficients rings loudly)
        const double wantL = value ("xlow"), wantH = jmax ((double) value ("xhigh"), wantL * 1.5);
        curXl = curXl <= 0.0 ? wantL : curXl * std::pow (wantL / curXl, 0.25);
        curXh = curXh <= 0.0 ? wantH : curXh * std::pow (wantH / curXh, 0.25);
        const double xl = curXl, xh = jmax (curXh, xl * 1.5);
        // Linkwitz-Riley 4th order: two Butterworth sections in series
        for (int s = 0; s < 2; ++s)
        {
            lp1[s].set (dsp2::Biquad::lowpass, sr, xl, 0.7071); hp1[s].set (dsp2::Biquad::highpass, sr, xl, 0.7071);
            lp2[s].set (dsp2::Biquad::lowpass, sr, xh, 0.7071); hp2[s].set (dsp2::Biquad::highpass, sr, xh, 0.7071);
            apLp[s].set (dsp2::Biquad::lowpass, sr, xh, 0.7071); apHp[s].set (dsp2::Biquad::highpass, sr, xh, 0.7071);
        }
        const float t = value ("time") * 0.01f;
        const float atk = dsp2::coeffMs (sr, 5.0 * t), rel = dsp2::coeffMs (sr, 80.0 * t);
        const float amount = value ("amount") * 0.01f, outDb = value ("output"), out = toGain (outDb);
        float ta[3], ra[3], tb[3], rb[3], mk[3];
        static const char* ids[3][5] { { "b1_ta", "b1_ra", "b1_tb", "b1_rb", "b1_gain" }, { "b2_ta", "b2_ra", "b2_tb", "b2_rb", "b2_gain" }, { "b3_ta", "b3_ra", "b3_tb", "b3_rb", "b3_gain" } };
        for (int k = 0; k < 3; ++k) { ta[k] = value (ids[k][0]); ra[k] = value (ids[k][1]); tb[k] = value (ids[k][2]); rb[k] = value (ids[k][3]); mk[k] = value (ids[k][4]); }
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float lo = lp1[1].process (lp1[0].process (x, c), c);
                // the lows go through the same phase turn as the split above them, so the bands add up flat
                lo = apLp[1].process (apLp[0].process (lo, c), c) + apHp[1].process (apHp[0].process (lo, c), c);
                const float rest = hp1[1].process (hp1[0].process (x, c), c);
                const float mid = lp2[1].process (lp2[0].process (rest, c), c);
                const float hi = hp2[1].process (hp2[0].process (rest, c), c);
                band[0][c] = lo; band[1][c] = mid; band[2][c] = hi;
            }
            float y[2] {};
            for (int k = 0; k < 3; ++k)
            {
                float pk = 0.0f;
                for (int c = 0; c < ch; ++c) pk = jmax (pk, std::abs (band[k][c]));
                const float lvl = toDb (pk);
                env[k] = lvl > env[k] ? lvl + (env[k] - lvl) * atk : lvl + (env[k] - lvl) * rel;
                float target = 0.0f;
                if (env[k] > ta[k]) target = (ta[k] + (env[k] - ta[k]) / ra[k]) - env[k];
                else if (env[k] < tb[k] && env[k] > -90.0f) target = (tb[k] - (tb[k] - env[k]) / rb[k]) - env[k];
                target = jlimit (-36.0f, 24.0f, target * amount);
                gdb[k] = target;
                maxGr = jmax (maxGr, -target);
                const float g = toGain (jmin (30.0f - jmax (0.0f, outDb), gdb[k] + mk[k]));   // never more than +30 dB in all
                for (int c = 0; c < ch; ++c) y[c] += band[k][c] * g;
            }
            for (int c = 0; c < ch; ++c) b.setSample (c, i, y[c] * out);
        }
        grDb.store (-maxGr);
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.width = 980;
        auto band = [] (int k, const String& name)
        {
            const String id = "b" + String (k);
            return Sec { name, { { id + "_ta", "Umbral arriba", "Por encima, esta banda se comprime." }, { id + "_ra", "Ratio arriba", "Cuánto baja lo que pasa el umbral de arriba."_u },
                                 { id + "_tb", "Umbral abajo", "Por debajo, esta banda se expande o se sube." },
                                 { id + "_rb", "Ratio abajo", "Menos de 1 calla lo suave (expansión); más de 1 lo sube (compresión hacia arriba)."_u },
                                 { id + "_gain", "Ganancia", "Volumen de la banda." } }, k != 2 };
        };
        s.sections = {
            { "General", { { "xlow", "Cruce bajo", "Dónde terminan los graves."_u }, { "xhigh", "Cruce alto", "Dónde empiezan los agudos."_u },
                           { "amount", "Cantidad", "Cuánto de todo el proceso se aplica."_u }, { "time", "Tiempo", "Escala los tiempos de ataque y relajación."_u },
                           { "output", "Salida", "Volumen final." } } },
            band (2, "Medios"), band (1, "Graves"), band (3, "Agudos"),
        };
        s.sections[1].advanced = false;
        s.sections[2].advanced = true;
        s.sections[3].advanced = true;
        return s;
    }
private:
    dsp2::Biquad lp1[2], hp1[2], lp2[2], hp2[2], apLp[2], apHp[2];
    double curXl = 0.0, curXh = 0.0;
    float env[3] { -120.0f, -120.0f, -120.0f }, gdb[3] {};
};

// ===================================================================== Bus de batería
class DrumBus : public FxBase
{
public:
    DrumBus() : FxBase ("Bus de batería"_u, "Dinámica"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "drive", "Drive", 0.0f, 100.0f, 20.0f);
        P::choice (l, "type", "Tipo", { "Suave", "Media", "Dura" }, 1);
        P::pct (l, "crunch", "Crunch", 0.0f, 100.0f, 0.0f);
        P::hz (l, "damp", "Amortiguar", 500.0f, 20000.0f, 20000.0f);
        P::flt (l, "trans", "Transitorios", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::hz (l, "boomfreq", "Boom frecuencia", 30.0f, 90.0f, 50.0f);
        P::pct (l, "boom", "Boom", 0.0f, 100.0f, 20.0f);
        P::pct (l, "boomdecay", "Boom caída"_u, 0.0f, 100.0f, 50.0f);
        P::db (l, "trim", "Entrada", -24.0f, 12.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void reset() override { for (auto& f : dampF) f.reset(); for (auto& f : crunchBp) f.reset(); boomF.reset(); fast = slow = 0.0f;
                            for (int c = 0; c < 2; ++c) { soft[c].reset(); mid[c].reset(); hard[c].reset(); crunchSat[c].reset(); } }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float drive = 1.0f + value ("drive") * 0.01f * 9.0f, trim = toGain (value ("trim")), outG = toGain (value ("output"));
        const int type = (int) value ("type");
        const float crunch = value ("crunch") * 0.01f, trans = value ("trans") * 0.01f, mix = value ("mix") * 0.01f;
        const float boom = value ("boom") * 0.01f;
        for (int c = 0; c < 2; ++c) { dampF[c].set (dsp2::Biquad::lowpass, sr, value ("damp"), 0.707); crunchBp[c].set (dsp2::Biquad::bandpass, sr, 3000.0, 0.8); }
        boomF.set (dsp2::Biquad::bandpass, sr, value ("boomfreq"), 2.0 + value ("boomdecay") * 0.4);
        const float fa = dsp2::coeffMs (sr, 1.0), sa = dsp2::coeffMs (sr, 40.0);
        // release of the two followers in time, not per sample: the same at 44.1, 48 or 96 kHz (were 0.9995 and 0.9998 per sample)
        const float fr = dsp2::coeffMs (sr, 45.35), sr2 = dsp2::coeffMs (sr, 113.4);
        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < ch; ++c) mono += b.getSample (c, i) * 0.5f;
            // transient shaper: difference between a fast and a slow follower
            const float a = std::abs (mono);
            fast = a > fast ? a + (fast - a) * fa : a + (fast - a) * fr;
            slow = a > slow ? a + (slow - a) * sa : a + (slow - a) * sr2;
            const float tg = jlimit (0.25f, 4.0f, 1.0f + trans * jlimit (-1.0f, 3.0f, (fast - slow) / (slow + 1.0e-4f)));
            const float boomOut = boomF.process (mono, 0) * boom * 4.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float y = x * trim * tg;
                // the three curves with ADAA (antiderivative antialiasing): the drive no longer folds fizz back into the highs
                switch (type)
                {
                    case 0: y = soft[c].process (y * drive) / std::tanh (drive); break;
                    case 1: y = (2.0f / MathConstants<float>::pi) * mid[c].process (y * drive * MathConstants<float>::halfPi); break;
                    default: y = hard[c].process (y * drive) * 0.95f; break;
                }
                if (crunch > 0.0f) y += crunchSat[c].process (crunchBp[c].process (y, c) * 8.0f) * crunch * 0.3f;
                y = dampF[c].process (y, c) + boomOut;
                b.setSample (c, i, (x * (1.0f - mix) + y * mix) * outG);
            }
        }
    }
    std::unique_ptr<Component> makeDisplay() override
    {
        return dsp2::makeTransferDisplay (*this, [this] (float x)
        {
            const float drive = 1.0f + value ("drive") * 0.01f * 9.0f;
            const float y = x * toGain (value ("trim"));
            switch ((int) value ("type"))
            {
                case 0: return std::tanh (y * drive) / std::tanh (drive);
                case 1: return (2.0f / MathConstants<float>::pi) * std::atan (y * drive * MathConstants<float>::halfPi);
                default: return jlimit (-1.0f, 1.0f, y * drive) * 0.95f;
            }
        }, [this] { static const char* t[] { "Suave", "Media", "Dura" }; return String (t[jlimit (0, 2, (int) value ("type"))]) + "   ADAA"; });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Golpe", { { "drive", "Drive", "Satura la batería entera para que suene junta y gorda."_u }, { "type", "Tipo", "Suave, media o dura." },
                         { "crunch", "Crunch", "Distorsión en los medios-agudos: más mordida en cajas y charles."_u },
                         { "trans", "Transitorios", "Positivo: más golpe. Negativo: más cuerpo y menos ataque."_u }, { "damp", "Amortiguar", "Quita brillo." } } },
            { "Boom", { { "boom", "Boom", "Un grave resonante que sigue al bombo." }, { "boomfreq", "Frecuencia", "La nota del boom." }, { "boomdecay", "Caída"_u, "Cuánto dura."_u } } },
            { "Niveles", { { "trim", "Entrada", "Nivel de entrada." }, { "mix", "Mezcla", "Mezcla con la batería original."_u }, { "output", "Salida", "Volumen final." } }, true },
        };
        return s;
    }
private:
    dsp2::Biquad dampF[2], crunchBp[2], boomF;
    float fast = 0.0f, slow = 0.0f;
    sdsp::Adaa1<sdsp::curve::Tanh> soft[2], crunchSat[2];
    sdsp::Adaa1<sdsp::curve::Atan> mid[2];
    sdsp::Adaa1<sdsp::curve::Hard> hard[2];
};
}

void registerDynamics (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Dinámica"_u, detail, Kind::effect, std::move (make), keys }); };
    add ("puerta", "Puerta", "Calla el sonido cuando baja de un umbral: limpia ruido o recorta colas."_u, [] { return std::make_unique<Gate>(); },
         { "thresh", "attack", "hold", "release", "floor" });
    add ("compresor-bus", "Compresor de bus", "Compresor para pegar una mezcla o un grupo: pocos controles, sonido de consola."_u, [] { return std::make_unique<Glue>(); },
         { "thresh", "ratio", "attack", "release", "makeup", "mix" });
    add ("limitador", "Limitador", "Nada pasa del techo: sube el volumen final sin picos."_u, [] { return std::make_unique<Limiter>(); },
         { "gain", "ceiling", "release", "look" });
    add ("multibanda", "Dinámica multibanda"_u, "Comprime o expande graves, medios y agudos por separado."_u, [] { return std::make_unique<Multiband>(); },
         { "xlow", "xhigh", "b1_ta", "b2_ta", "b3_ta", "amount" });
    add ("bus-bateria", "Bus de batería"_u, "Gordura, golpe y un grave resonante para baterías."_u, [] { return std::make_unique<DrumBus>(); },
         { "drive", "crunch", "trans", "boom", "boomfreq", "mix" });
}
}

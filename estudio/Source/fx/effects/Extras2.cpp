// More of Racoon's own effects:
// Compuerta rítmica, Efectos DJ, Rotativo, Wah, Afinación automática, Shimmer
#include "AllEffects.h"
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
constexpr double twoPi = MathConstants<double>::twoPi;

// two overlapping grains read from a delay line at a moving distance: the classic doppler pitch shifter.
// Hann windows half a grain apart add up to one.
struct GrainShifter
{
    racoon::dsp::DelayLine line;
    double phase = 0.0;
    void prepare (int maxSamples) { line.prepare (maxSamples); phase = 0.0; }
    void clear() { line.clear(); phase = 0.0; }
    float process (float x, double ratio, double grain)
    {
        line.push (x);
        phase += (1.0 - ratio) / grain;
        phase -= std::floor (phase);
        float y = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            double p = phase + k * 0.5;
            p -= std::floor (p);
            const float w = (float) (0.5 - 0.5 * std::cos (twoPi * p));
            y += w * line.tap ((float) (3.0 + p * grain));
        }
        return y;
    }
};

// ===================================================================== Compuerta rítmica (trance gate)
class StepGate : public FxBase
{
public:
    StepGate() : FxBase ("Compuerta rítmica"_u, "Modulación"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        static const bool pattern[16] { 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1 };
        for (int i = 0; i < 16; ++i) P::toggle (l, "step" + String (i + 1), "Paso " + String (i + 1), pattern[i]);
        P::choice (l, "div", "División"_u, RacoonProcessor::divisionNames(), 10);
        P::integer (l, "steps", "Pasos", 1, 16, 16);
        P::pct (l, "len", "Largo de cada paso", 5.0f, 100.0f, 70.0f);
        P::ms (l, "attack", "Ataque", 0.1f, 50.0f, 1.0f, 5.0f);
        P::ms (l, "release", "Relajación"_u, 1.0f, 200.0f, 12.0f, 30.0f);
        P::pct (l, "depth", "Profundidad", 0.0f, 100.0f, 100.0f);
        P::pct (l, "swing", "Swing", 0.0f, 75.0f, 0.0f);
        return l;
    }
    void reset() override { env = 0.0f; freeBeats = 0.0; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double stepBeats = syncSeconds ((int) value ("div"), bpm()) * bpm() / 60.0;
        const int steps = jlimit (1, 16, (int) value ("steps"));
        bool on[16];
        // the parameter names are fixed text: no String is built on the audio thread
        static const char* stepIds[16] { "step1", "step2", "step3", "step4", "step5", "step6", "step7", "step8",
                                         "step9", "step10", "step11", "step12", "step13", "step14", "step15", "step16" };
        for (int i = 0; i < 16; ++i) on[i] = value (stepIds[i]) > 0.5f;
        const float len = value ("len") * 0.01f, depth = value ("depth") * 0.01f, swing = value ("swing") * 0.01f;
        const float aC = dsp2::coeffMs (sr, value ("attack")), rC = dsp2::coeffMs (sr, value ("release"));
        const double beatsPerSample = bpm() / 60.0 / sr;
        float lastStep = -1.0f;
        for (int i = 0; i < n; ++i)
        {
            const double beats = isPlaying() ? ppq() + i * beatsPerSample : (freeBeats += beatsPerSample);
            double pos = beats / stepBeats;
            // swing: every second step starts late
            const long long pairIdx = (long long) std::floor (pos / 2.0);
            double inPair = pos - (double) pairIdx * 2.0;
            const double split = 1.0 + swing * 0.5;
            int idx; double frac;
            if (inPair < split) { idx = 0; frac = inPair / split; }
            else { idx = 1; frac = (inPair - split) / (2.0 - split); }
            const long long stepNo = pairIdx * 2 + idx;
            const int s = (int) (((stepNo % steps) + steps) % steps);
            const bool open = on[s] && frac < len;
            const float target = open ? 1.0f : 0.0f;
            env = target + (env - target) * (target > env ? aC : rC);
            const float g = 1.0f - depth * (1.0f - env);
            for (int c = 0; c < ch; ++c) b.setSample (c, i, b.getSample (c, i) * g);
            lastStep = (float) s;
        }
        current.store ((int) lastStep);
    }
    String displayText() const override { const int s = current.load(); return s >= 0 ? "Paso " + String (s + 1) : String(); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.width = 1040;
        std::vector<Ctl> a, b;
        for (int i = 1; i <= 8; ++i) a.push_back ({ "step" + String (i), String (i), "Prende o apaga este paso."_u });
        for (int i = 9; i <= 16; ++i) b.push_back ({ "step" + String (i), String (i), "Prende o apaga este paso."_u });
        s.sections = {
            { "Pasos 1 a 8", a }, { "Pasos 9 a 16", b },
            { "Ritmo", { { "div", "División"_u, "Cuánto dura cada paso."_u }, { "steps", "Pasos", "Cuántos pasos tiene el patrón antes de repetirse."_u },
                         { "len", "Largo", "Qué tanto de cada paso queda abierto."_u }, { "depth", "Profundidad", "Cuánto baja en los pasos apagados."_u } } },
            { "Forma", { { "attack", "Ataque", "Qué tan rápido abre."_u }, { "release", "Relajación"_u, "Qué tan rápido cierra."_u },
                         { "swing", "Swing", "Atrasa los pasos pares."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        auto pat = [] (const char* p) { std::vector<std::pair<String, float>> v; for (int i = 0; i < 16; ++i) v.push_back ({ "step" + String (i + 1), p[i] == 'x' ? 1.0f : 0.0f }); return v; };
        auto with = [] (std::vector<std::pair<String, float>> v, std::initializer_list<std::pair<String, float>> more) { for (auto& m : more) v.push_back (m); return v; };
        return { { "Inicial", {} },
                 { "Trance clásico"_u, "Trance", with (pat ("x.xxx.x.x.xxxx.x"), { { "div", 10 }, { "len", 70 } }) },
                 { "Corcheas", "Básicos"_u, with (pat ("x.x.x.x.x.x.x.x."), { { "div", 10 }, { "len", 90 } }) },
                 { "Tresillos", "Rítmicos"_u, with (pat ("xx.xx.xx.xx.xx.x"), { { "div", 12 }, { "len", 60 } }) },
                 { "Picado", "Rítmicos"_u, with (pat ("xxxxxxxxxxxxxxxx"), { { "div", 13 }, { "len", 35 }, { "release", 5 } }) },
                 { "Respiración"_u, "Suaves", with (pat ("xxx.xxx.xxx.xx.."), { { "div", 10 }, { "attack", 15 }, { "release", 60 }, { "depth", 70 } }) } };
    }
private:
    float env = 0.0f;
    double freeBeats = 0.0;
    std::atomic<int> current { -1 };
};

// ===================================================================== Efectos DJ (tape stop, backspin, stutter...)
class DjFx : public FxBase
{
public:
    DjFx() : FxBase ("Efectos DJ", "Creativos y espectrales", layout(), 0.0, false, false, false) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "effect", "Efecto", { "Freno de cinta", "Arranque de cinta", "Rebobinado", "Tartamudeo", "Reversa", "Media velocidad" }, 0);
        P::toggle (l, "active", "Activo", false);
        P::choice (l, "time", "Tiempo", { "1/16", "1/8", "1/4", "1/2", "1 compás"_u, "2 compases" }, 3);
        P::choice (l, "repeat", "Repetición"_u, { "1/4", "1/8", "1/16", "1/32", "1/64" }, 2);
        P::pct (l, "filter", "Filtro", 0.0f, 100.0f, 50.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    static constexpr double maxSeconds = 8.0;
    void prepare (double rate, int) override
    {
        size = (int) (rate * maxSeconds) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        write = 0; running = false; xf = 0.0f; t = 0.0; delay = 0.0; for (auto& f : flt) f.reset();
    }
    float readAt (int c, double d) const
    {
        // Hermite read d samples behind the write head
        double pos = (double) write - d;
        while (pos < 0) pos += size;
        const int i = (int) pos;
        const float f = (float) (pos - i);
        auto at = [&] (int k) { return buf[(size_t) c][(size_t) (((i + k) % size + size) % size)]; };
        const float xm1 = at (-1), x0 = at (0), x1 = at (1), x2 = at (2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        if (size <= 0) return;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const bool act = value ("active") > 0.5f;
        const int effect = (int) value ("effect");
        static const double timeBeats[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 }, repBeats[] { 1.0, 0.5, 0.25, 0.125, 0.0625 };
        const double secPerBeat = 60.0 / jlimit (20.0, 400.0, bpm());
        const double T = timeBeats[jlimit (0, 5, (int) value ("time"))] * secPerBeat * sr;
        const double L = jmax (64.0, repBeats[jlimit (0, 4, (int) value ("repeat"))] * secPerBeat * sr);
        const float filt = value ("filter") * 0.01f, mix = value ("mix") * 0.01f;
        const float xfStep = (float) (1.0 / (0.005 * sr));
        if (act && ! running) { running = true; t = 0.0; delay = 0.0; mode = effect; loopLen = jmin (L, (double) size - 8.0); }
        if (! act && running && xf <= 0.0f) running = false;
        const double maxDelay = size - 8.0;
        for (int i = 0; i < n; ++i)
        {
            float in[2];
            for (int c = 0; c < 2; ++c)
            {
                in[c] = b.getSample (jmin (c, ch - 1), i);
                buf[(size_t) c][(size_t) write] = in[c];
            }
            write = (write + 1) % size;
            xf = act ? jmin (1.0f, xf + xfStep) : jmax (0.0f, xf - xfStep);
            if (xf <= 0.0f && ! act) { continue; }
            // speed of the read head for each effect; the delay behind the live input grows by (1 - speed)
            double speed = 1.0;
            float gate = 1.0f;
            switch (mode)
            {
                case 0: speed = t < T ? std::pow (1.0 - t / T, 1.4) : 0.0; if (t >= T) gate = 0.0f; break;
                case 1: speed = t < T ? std::pow (t / T, 1.4) : 1.0; break;
                case 2: speed = t < T ? -3.0 * (1.0 - t / T) : 0.0; if (t >= T) gate = 0.0f; break;
                case 4: speed = -1.0; break;
                case 5: speed = 0.5; break;
                default: speed = 1.0; break;
            }
            float wet[2];
            if (mode == 3)
            {
                // stutter: the last "repeat" of sound before it was switched on, looped, with a short fade at the seam
                const double inLoop = std::fmod (t, loopLen);
                const double back = t + loopLen - inLoop + 1.0;
                if (back >= maxDelay) gate = 0.0f;
                const float fade = (float) jmin (1.0, jmin (inLoop, loopLen - inLoop) / (0.002 * sr));
                for (int c = 0; c < 2; ++c) wet[c] = readAt (c, jmin (back, maxDelay)) * fade * gate;
            }
            else
            {
                delay = jlimit (1.0, maxDelay, delay + (1.0 - speed));
                if (delay >= maxDelay - 1.0) gate = 0.0f;
                for (int c = 0; c < 2; ++c) wet[c] = readAt (c, delay) * gate;
            }
            // the filter closes as the tape slows down
            const double absSp = jmin (1.0, std::abs (speed));
            const double cut = 20000.0 * (1.0 - filt) + filt * (250.0 + 19750.0 * absSp * absSp);
            const float gF = dsp::SVF::gFor ((float) jmin (cut, sr * 0.45), sr);
            for (int c = 0; c < 2; ++c)
            {
                if (filt > 0.001f) { flt[c].process (wet[c], gF, 1.2f); wet[c] = flt[c].lp; }
                const float a = xf * mix;
                if (c < ch) b.setSample (c, i, in[c] * (1.0f - a) + wet[c] * a);
            }
            t += 1.0;
        }
        state.store (running ? 1 : 0);
    }
    String displayText() const override { return state.load() ? "ACTIVO" : String(); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Efecto", { { "effect", "Efecto", "Freno y arranque de cinta, rebobinado de tornamesa, tartamudeo, reversa o media velocidad."_u },
                          { "active", "Activo", "Préndelo en vivo o con automatización: el efecto empieza en ese momento."_u },
                          { "time", "Tiempo", "Cuánto tarda el freno, el arranque o el rebobinado."_u },
                          { "repeat", "Repetición"_u, "El pedazo que repite el tartamudeo."_u } } },
            { "Salida", { { "filter", "Filtro", "Cierra los agudos mientras la cinta se frena."_u }, { "mix", "Mezcla", "Con la señal original."_u } } },
        };
        return s;
    }
private:
    std::vector<float> buf[2];
    int size = 0, write = 0, mode = 0;
    bool running = false;
    float xf = 0.0f;
    double t = 0.0, delay = 0.0, loopLen = 4800.0;
    racoon::dsp::SVF flt[2];
    std::atomic<int> state { 0 };
};

// ===================================================================== Rotativo (Leslie)
class Rotary : public FxBase
{
public:
    Rotary() : FxBase ("Rotativo", "Modulación"_u, layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "speed", "Velocidad", { "Lenta", "Rápida"_u, "Freno" }, 0);
        P::flt (l, "accel", "Aceleración"_u, P::range (0.2f, 5.0f, 1.2f), 1.2f, " s", 2);
        P::flt (l, "balance", "Bocina / tambor", P::range (-100.0f, 100.0f), 0.0f, "", 0);
        P::pct (l, "doppler", "Doppler", 0.0f, 100.0f, 60.0f);
        P::pct (l, "am", "Amplitud", 0.0f, 100.0f, 50.0f);
        P::flt (l, "mics", "Separación de micrófonos"_u, P::range (0.0f, 180.0f), 120.0f, "°"_u, 0);
        P::pct (l, "drive", "Drive", 0.0f, 100.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double rate, int) override { horn.prepare ((int) (rate * 0.01) + 8); drum.prepare ((int) (rate * 0.01) + 8); reset(); }
    void reset() override { horn.clear(); drum.clear(); xo[0].reset(); xo[1].reset(); xo[2].reset(); xo[3].reset(); phH = 0.0; phD = 0.25; rateH = 0.83; rateD = 0.67; }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int sp = (int) value ("speed");
        const double tH = sp == 0 ? 0.83 : sp == 1 ? 6.7 : 0.0, tD = sp == 0 ? 0.67 : sp == 1 ? 5.8 : 0.0;
        const double acc = value ("accel");
        // the horn is light and follows quickly; the drum is heavy and takes three times as long
        const double kH = std::exp (-1.0 / (acc * 0.35 * sr)), kD = std::exp (-1.0 / (acc * 1.1 * sr));
        for (int s = 0; s < 2; ++s) { xo[s].set (Biquad::lowpass, sr, 800.0, 0.7071); xo[s + 2].set (Biquad::highpass, sr, 800.0, 0.7071); }
        const float bal = value ("balance") * 0.01f, gh = jmin (1.0f, 1.0f + bal), gd = jmin (1.0f, 1.0f - bal);
        const float dop = value ("doppler") * 0.01f, am = value ("am") * 0.01f;
        const double micOff = value ("mics") / 360.0;
        const float drive = value ("drive") * 0.01f, dg = 1.0f + drive * 5.0f, mix = value ("mix") * 0.01f;
        const float depthH = (float) (0.00035 * sr) * dop, depthD = (float) (0.00015 * sr) * dop, baseD = (float) (0.0015 * sr);
        for (int i = 0; i < n; ++i)
        {
            rateH = tH + (rateH - tH) * kH;
            rateD = tD + (rateD - tD) * kD;
            phH += rateH / sr; phH -= std::floor (phH);
            phD += rateD / sr; phD -= std::floor (phD);
            float x = 0.5f * (b.getSample (0, i) + b.getSample (jmin (1, ch - 1), i));
            if (drive > 0.0f) x = std::tanh (x * dg) / std::sqrt (dg);
            const float lo = xo[1].process (xo[0].process (x, 0), 0), hi = xo[3].process (xo[2].process (x, 0), 0);
            horn.push (hi); drum.push (lo);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const double aH = twoPi * (phH + c * micOff), aD = twoPi * (phD + c * micOff);
                const float h = horn.tap (baseD + depthH * (float) std::sin (aH)) * (1.0f - am * 0.5f * (1.0f - (float) std::cos (aH)));
                const float d = drum.tap (baseD + depthD * (float) std::sin (aD)) * (1.0f - am * 0.3f * (1.0f - (float) std::cos (aD)));
                out[c] = h * gh + d * gd;
            }
            for (int c = 0; c < ch; ++c)
            {
                const float dry = b.getSample (c, i);
                b.setSample (c, i, dry * (1.0f - mix) + out[c] * mix);
            }
        }
    }
    String displayText() const override { return String (rateH, 1) + " Hz"; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Rotores", { { "speed", "Velocidad", "Lenta (coral), rápida (trémolo) o freno. Cambiarla en vivo hace la subida clásica."_u },
                           { "accel", "Aceleración"_u, "Cuánto tardan en cambiar de velocidad."_u },
                           { "balance", "Balance", "Izquierda: más tambor de graves. Derecha: más bocina de agudos."_u } } },
            { "Gabinete", { { "doppler", "Doppler", "El cambio de afinación al girar."_u }, { "am", "Amplitud", "El cambio de volumen al girar."_u },
                            { "mics", "Micrófonos"_u, "Qué tan separados están: más, más estéreo."_u },
                            { "drive", "Drive", "El preamp de válvula del gabinete."_u }, { "mix", "Mezcla", "Con la señal original."_u } } },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine horn, drum;
    Biquad xo[4];
    double phH = 0.0, phD = 0.25, rateH = 0.83, rateD = 0.67;
};

// ===================================================================== Wah
class Wah : public FxBase
{
public:
    Wah() : FxBase ("Wah", "EQ y filtros", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Envolvente", "LFO", "Pedal" }, 0);
        P::pct (l, "pedal", "Pedal", 0.0f, 100.0f, 50.0f);
        P::hz (l, "fmin", "Frecuencia mínima"_u, 100.0f, 1000.0f, 350.0f);
        P::hz (l, "fmax", "Frecuencia máxima"_u, 1000.0f, 6000.0f, 2500.0f);
        P::pct (l, "reso", "Resonancia", 0.0f, 100.0f, 70.0f);
        P::db (l, "sens", "Sensibilidad", -24.0f, 24.0f, 0.0f);
        P::ms (l, "attack", "Ataque", 0.5f, 100.0f, 5.0f, 10.0f);
        P::ms (l, "release", "Relajación"_u, 10.0f, 1000.0f, 120.0f, 150.0f);
        P::flt (l, "rate", "Velocidad del LFO", P::range (0.05f, 10.0f, 1.0f), 1.5f, " Hz", 2);
        P::toggle (l, "sync", "LFO al tempo", false);
        P::choice (l, "div", "División del LFO"_u, RacoonProcessor::divisionNames(), 4);
        P::choice (l, "type", "Tipo", { "Clásico"_u, "Paso bajo", "Opto" }, 0);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double rate, int) override { posSm.prepare (rate, 2.0); reset(); }
    void reset() override { for (auto& f : svf) f.reset(); env.env = 0.0f; opto = 0.0f; phase = 0.0; posSm.reset (0.0f); }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const int mode = (int) value ("mode"), type = (int) value ("type");
        const float fmin = value ("fmin"), fmax = jmax (fmin * 1.2f, value ("fmax"));
        const float k = 2.0f - 1.9f * value ("reso") * 0.01f;
        const float sens = value ("sens"), mix = value ("mix") * 0.01f;
        env.set (sr, value ("attack"), value ("release"));
        const float optoC = dsp2::coeffMs (sr, 90.0);
        const bool sync = value ("sync") > 0.5f;
        lockPhase (sync, (int) value ("div"), phase);
        const double inc = lfoHz (sync, (int) value ("div"), value ("rate")) / sr;
        const float pedal = value ("pedal") * 0.01f;
        float shownPos = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0.0f;
            for (int c = 0; c < ch; ++c) lvl = jmax (lvl, std::abs (b.getSample (c, i)));
            const float e = env.process (lvl);
            phase += inc; phase -= std::floor (phase);
            float pos;
            if (mode == 0) pos = jlimit (0.0f, 1.0f, (toDb (e) + 36.0f + sens) / 36.0f);
            else if (mode == 1) pos = 0.5f - 0.5f * (float) std::cos (twoPi * phase);
            else pos = pedal;
            if (type == 2) { opto = pos + (opto - pos) * optoC; pos = opto; }   // a photocell is slow and smooth
            posSm.target = pos;
            const float p = posSm.next();
            shownPos = p;
            const float fc = fmin * std::pow (fmax / fmin, p);
            const float g = dsp::SVF::gFor (jmin (fc, (float) (sr * 0.45)), sr);
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                svf[c].process (x, g, k);
                const float y = type == 1 ? svf[c].lp : svf[c].bp * k * 1.6f;
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
        position.store (shownPos);
    }
    String displayText() const override { return "Pedal " + String (roundToInt (position.load() * 100.0f)) + " %"; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Wah", { { "mode", "Modo", "Envolvente: se abre cuando tocas fuerte. LFO: se mueve solo. Pedal: tú lo mueves o lo automatizas."_u },
                       { "pedal", "Pedal", "La posición del pedal en modo Pedal."_u }, { "reso", "Resonancia", "Qué tan marcado es el pico: más, más \"cuac\"."_u },
                       { "sens", "Sensibilidad", "En modo envolvente: cuánto lo abre tu volumen."_u }, { "mix", "Mezcla", "Con la señal original."_u } } },
            { "Rango", { { "fmin", "Mínima"_u, "El talón del pedal."_u }, { "fmax", "Máxima"_u, "La punta del pedal."_u }, { "type", "Tipo", "Clásico es el pico de banda; paso bajo es más redondo; opto es lento y suave."_u } } },
            { "Movimiento", { { "attack", "Ataque", "Qué tan rápido abre con el golpe."_u }, { "release", "Relajación"_u, "Qué tan rápido regresa."_u },
                              { "rate", "LFO", "Velocidad del LFO."_u }, { "sync", "Al tempo", "El LFO sigue la canción."_u }, { "div", "División"_u, "Un ciclo al tempo."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::SVF svf[2];
    Follower env;
    float opto = 0.0f;
    double phase = 0.0;
    racoon::dsp::Smooth posSm;
    std::atomic<float> position { 0.0f };
};

// ===================================================================== Afinación automática
class PitchCorrect : public FxBase
{
public:
    PitchCorrect() : FxBase ("Afinación automática"_u, "Creativos y espectrales", layout()) {}
    static StringArray keys() { return { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" }; }
    static StringArray scales() { return { "Cromática"_u, "Mayor", "Menor", "Menor armónica"_u, "Pentatónica mayor"_u, "Pentatónica menor"_u, "Blues", "Dórica"_u }; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "key", "Tónica"_u, keys(), 0);
        P::choice (l, "scale", "Escala", scales(), 0);
        P::ms (l, "speed", "Rapidez de corrección"_u, 0.0f, 400.0f, 20.0f, 50.0f);
        P::flt (l, "tol", "Tolerancia", P::range (0.0f, 50.0f), 0.0f, " cents", 0);
        P::pct (l, "human", "Humanizar", 0.0f, 100.0f, 0.0f);
        P::choice (l, "range", "Voz", { "Grave", "Normal" }, 1);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    static constexpr int dec = 4, win = 256, maxTau = 300;
    void prepare (double rate, int) override
    {
        for (auto& l : lines) l.prepare ((int) (rate * 0.06));
        // the latency is known before the first block, so the host lines the track up from the start
        latency = (int) (((int) value ("range") == 0 ? 0.021 : 0.014) * rate) + 4;
        setLatencySamples (latency);
        reset();
    }
    void reset() override
    {
        for (auto& l : lines) l.clear();
        shPhase = 0.5;
        for (auto& f : aa) f.reset();
        std::fill (std::begin (ana), std::end (ana), 0.0f);
        std::fill (std::begin (fullBuf), std::end (fullBuf), 0.0f);
        fullPos = 0;
        anaPos = 0; decCount = 0; hopCount = 0; ratio = 1.0; targetRatio = 1.0; period = 0.01 * sr; detected = 0.0f; shownMidi.store (-1.0f);
    }
    bool allowed (int midi) const
    {
        static const int sc[8][12] {
            { 1,1,1,1,1,1,1,1,1,1,1,1 }, { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 }, { 1,0,1,1,0,1,0,1,1,0,0,1 },
            { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 }, { 1,0,0,1,0,1,1,1,0,0,1,0 }, { 1,0,1,1,0,1,0,1,0,1,1,0 } };
        const int s = jlimit (0, 7, curScale);
        const int deg = ((midi - curKey) % 12 + 12) % 12;
        return sc[s][deg] != 0;
    }
    // YIN on the decimated analysis buffer; returns the period in decimated samples, or 0 when unvoiced
    double yin (int tauMin, int tauMax)
    {
        float d[maxTau + 2] {};
        const int start = anaPos;   // oldest sample
        auto at = [&] (int k) { return ana[(start + k) % anaSize]; };
        double energy = 0.0;
        for (int j = 0; j < win; ++j) energy += at (j) * at (j);
        if (energy / win < 1.0e-6) return 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            for (int j = 0; j < win; ++j) { const float v = at (j) - at (j + tau); s += v * v; }
            d[tau] = (float) s;
        }
        double run = 0.0;
        float cm[maxTau + 2] {};
        cm[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau) { run += d[tau]; cm[tau] = run > 0.0 ? (float) (d[tau] * tau / run) : 1.0f; }
        int best = -1;
        // a real period shows as a dip after the curve has climbed; dull noise stays low from the start and is not a note
        bool risen = false;
        for (int tau = 1; tau < tauMax; ++tau)
        {
            if (cm[tau] > 0.5f) risen = true;
            if (risen && tau >= tauMin && cm[tau] < 0.15f) { while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) ++tau; best = tau; break; }
        }
        if (best < 0) return 0.0;
        const float a = cm[best - 1], b2 = cm[best], c = cm[best + 1];
        const float den = a + c - 2.0f * b2;
        const double shift = std::abs (den) > 1.0e-9f ? 0.5 * (a - c) / den : 0.0;
        return best + jlimit (-0.5, 0.5, shift);
    }
    // the decimated estimate is only good to about 20 cents: search again at the full rate around it
    double refine (double T0)
    {
        const int lo = jmax (2, (int) T0 - 6), hi = jmin (fullSize - fullWin - 2, (int) T0 + 6);
        if (hi <= lo + 2) return T0;
        double dv[16] {};
        int best = lo;
        const int newest = fullPos;   // oldest sample of the ring
        auto at = [&] (int k) { return fullBuf[(newest + k) % fullSize]; };
        const int start = fullSize - fullWin - hi - 1;
        for (int lag = lo; lag <= hi; ++lag)
        {
            double s = 0.0;
            for (int j = 0; j < fullWin; ++j) { const float v = at (start + j) - at (start + j + lag); s += v * v; }
            dv[lag - lo] = s;
            if (s < dv[best - lo] || lag == lo) best = lag;
        }
        if (best <= lo || best >= hi) return (double) best;
        const double a = dv[best - 1 - lo], b2 = dv[best - lo], c = dv[best + 1 - lo], den = a + c - 2.0 * b2;
        return best + (std::abs (den) > 1.0e-12 ? jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0);
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        curKey = (int) value ("key"); curScale = (int) value ("scale");
        const float speed = value ("speed"), tol = value ("tol"), human = value ("human") * 0.01f, mix = value ("mix") * 0.01f;
        const bool low = (int) value ("range") == 0;
        const int L = (int) ((low ? 0.021 : 0.014) * sr) + 4;
        if (L != latency) { latency = L; setLatencySamples (L); }
        const double dsr = sr / dec;
        const int tauMin = (int) (dsr / (low ? 600.0 : 1000.0)), tauMax = jmin (maxTau, (int) (dsr / (low ? 50.0 : 80.0)));
        for (auto& f : aa) f.set (Biquad::lowpass, sr, dsr * 0.4, 0.7071);
        const double hopSec = (double) hop / sr;
        const double smooth = speed < 1.0f ? 0.0 : std::exp (-hopSec / (speed * 0.001));
        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < ch; ++c) mono += b.getSample (c, i);
            mono /= (float) ch;
            const float lp = aa[1].process (aa[0].process (mono, 0), 0);
            if (++decCount >= dec) { decCount = 0; ana[anaPos] = lp; anaPos = (anaPos + 1) % anaSize; }
            fullBuf[fullPos] = mono; fullPos = (fullPos + 1) % fullSize;
            if (++hopCount >= hop)
            {
                hopCount = 0;
                const double tau = yin (jmax (2, tauMin), tauMax);
                if (tau > 0.0)
                {
                    const double f0 = sr / refine (tau * dec);
                    period = sr / f0;
                    const double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
                    // the nearest note of the scale
                    int best = (int) std::round (midi);
                    double bestDist = 1.0e9;
                    for (int dn = -6; dn <= 6; ++dn)
                    {
                        const int cand = (int) std::round (midi) + dn;
                        if (allowed (cand) && std::abs ((double) cand - midi) < bestDist) { bestDist = std::abs ((double) cand - midi); best = cand; }
                    }
                    const double dev = midi - best;
                    double corrected = best + dev * human;
                    if (std::abs (dev) * 100.0 < tol) corrected = midi;
                    targetRatio = std::pow (2.0, (corrected - midi) / 12.0);
                    detected = (float) midi;
                    shownMidi.store ((float) best);
                }
                else { targetRatio = 1.0; shownMidi.store (-1.0f); }
                ratio = targetRatio + (ratio - targetRatio) * smooth;
            }
            // two grains a half apart, read around a fixed delay (the latency the host compensates). The grain is a
            // whole number of periods, so each jump back lands in phase and the pitch stays exact.
            const double grain = jmin (2.0 * (L - 4), period * jmax (2.0, std::round (0.025 * sr / period)));
            shPhase += (1.0 - ratio) / grain;
            if (std::abs (ratio - 1.0) < 1.0e-4)
            {
                // nothing to correct: one grain slides to the middle and plays alone, so the voice passes clean
                const double rest = shPhase < 0.25 ? 0.0 : shPhase < 0.75 ? 0.5 : 1.0;
                shPhase += (rest - shPhase) * 0.0005;
            }
            shPhase -= std::floor (shPhase);
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                lines[c].push (x);
                float y = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    double p = shPhase + k * 0.5;
                    p -= std::floor (p);
                    const float w = (float) (0.5 - 0.5 * std::cos (twoPi * p));
                    y += w * lines[c].tap ((float) (L + 1 + (p - 0.5) * grain));   // tap (1) is the newest sample
                }
                const float dry = lines[c].tap ((float) (L + 1));
                b.setSample (c, i, dry * (1.0f - mix) + y * mix);
            }
        }
    }
    String displayText() const override
    {
        const float m = shownMidi.load();
        if (m < 0.0f) return "Sin nota";
        static const char* names[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const int mi = (int) m;
        return String (names[((mi % 12) + 12) % 12]) + String (mi / 12 - 1);
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Escala", { { "key", "Tónica"_u, "La nota base de la canción. Racoon la marca en el género que elegiste."_u },
                          { "scale", "Escala", "Las notas permitidas. Cromática corrige a la nota más cercana, cualquiera."_u },
                          { "range", "Voz", "Grave para voces bajas y bajos; normal para casi todo."_u } } },
            { "Corrección"_u, { { "speed", "Rapidez", "0 ms salta de nota en nota: el efecto robot. 30 a 80 ms suena natural."_u },
                                { "tol", "Tolerancia", "Dentro de estos cents no corrige nada."_u },
                                { "human", "Humanizar", "Deja pasar parte del vibrato y las curvas de la voz."_u },
                                { "mix", "Mezcla", "Con la voz original."_u } } },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", {} },
                 { "Robot", "Voces", { { "speed", 0 }, { "human", 0 } } },
                 { "Natural", "Voces", { { "speed", 60 }, { "tol", 10 }, { "human", 30 } } },
                 { "Trap en menor", "Voces", { { "scale", 2 }, { "speed", 5 } } } };
    }
private:
    static constexpr int anaSize = win + maxTau + 4, hop = 256, fullWin = 1024, fullSize = 1024 + maxTau * dec + 64;
    float fullBuf[fullSize] {};
    int fullPos = 0;
    racoon::dsp::DelayLine lines[2];
    double shPhase = 0.5;
    int latency = -1;
    Biquad aa[2];
    float ana[anaSize] {};
    int anaPos = 0, decCount = 0, hopCount = 0, curKey = 0, curScale = 0;
    double ratio = 1.0, targetRatio = 1.0, period = 480.0;
    float detected = 0.0f;
    std::atomic<float> shownMidi { -1.0f };
};

// ===================================================================== Shimmer
class Shimmer : public FxBase
{
public:
    Shimmer() : FxBase ("Shimmer", "Reverb y espacio", layout(), 30.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "size", "Tamaño"_u, 0.0f, 100.0f, 70.0f);
        P::flt (l, "decay", "Caída"_u, P::range (0.5f, 30.0f, 4.0f), 6.0f, " s", 1);
        P::ms (l, "predelay", "Predelay", 0.0f, 250.0f, 20.0f, 60.0f);
        P::pct (l, "shimmer", "Shimmer", 0.0f, 100.0f, 50.0f);
        P::choice (l, "interval", "Intervalo", { "+12", "+7", "+5", "-12", "+12 y +7" }, 0);
        P::pct (l, "damping", "Amortiguación"_u, 0.0f, 100.0f, 40.0f);
        P::pct (l, "mod", "Modulación"_u, 0.0f, 100.0f, 30.0f);
        P::hz (l, "lowcut", "Corte de graves", 20.0f, 1000.0f, 200.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 35.0f);
        return l;
    }
    static constexpr int N = 8;
    void prepare (double rate, int) override
    {
        for (auto& d : lines) d.prepare ((int) (rate * 0.25));
        for (auto& a : diff) a.prepare ((int) (rate * 0.02));
        pre.prepare ((int) (rate * 0.26) + 8);
        for (auto& s : sh) s.prepare ((int) (rate * 0.2));
        reset();
    }
    void reset() override
    {
        for (auto& d : lines) d.clear();
        for (auto& a : diff) a.clear();
        pre.clear();
        for (auto& s : sh) s.clear();
        for (auto& f : damp) f = 0.0f;
        for (auto& f : lc) f.reset();
        fbShift = 0.0f; modPh = 0.0;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        static const int base[N] { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        static const int apLen[4] { 142, 107, 379, 277 };
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const float size = 0.3f + value ("size") * 0.01f * 1.4f;
        const double decay = value ("decay");
        int len[N]; float g[N];
        for (int k = 0; k < N; ++k)
        {
            len[k] = jlimit (16, (int) (sr * 0.24), (int) (base[k] * size * sr / 44100.0));
            g[k] = (float) std::pow (10.0, -3.0 * len[k] / (decay * sr));
        }
        const float dampC = value ("damping") * 0.01f * 0.8f;
        const float shim = value ("shimmer") * 0.01f;
        const int interval = (int) value ("interval");
        static const double ratios[5][2] { { 2.0, 2.0 }, { 1.4983, 1.4983 }, { 1.3348, 1.3348 }, { 0.5, 0.5 }, { 2.0, 1.4983 } };
        const float modAmt = value ("mod") * 0.01f * (float) (sr * 0.0006);
        const int preD = (int) (value ("predelay") * 0.001 * sr);
        for (auto& f : lc) f.set (Biquad::highpass, sr, value ("lowcut"), 0.7071);
        const float m = value ("mix") * 0.01f, dryG = jmin (1.0f, 2.0f * (1.0f - m)), wetG = jmin (1.0f, 2.0f * m);
        const double grain = 0.06 * sr;
        for (int i = 0; i < n; ++i)
        {
            modPh += 0.5 / sr; modPh -= std::floor (modPh);
            const float inL = b.getSample (0, i), inR = ch > 1 ? b.getSample (1, i) : inL;
            pre.push (0.5f * (inL + inR));
            float x = pre.tapInt (preD + 1);
            // input diffusion: four allpasses smear the attack into a cloud
            for (int a = 0; a < 4; ++a)
            {
                const float dl = diff[a].tapInt ((int) std::round (apLen[a] * sr / 44100.0));   // scaled exactly (48 kHz was rounded to 44.1)
                const float v = x + 0.6f * dl;
                diff[a].push (v);
                x = dl - 0.6f * v;
            }
            // the pitch shifter lives in the loop: each turn climbs the interval again
            const float into = x + fbShift * shim * 0.4f;
            float o[N];
            for (int k = 0; k < N; ++k)
            {
                const float mod = (k < 2) ? modAmt * (float) std::sin (twoPi * (modPh + k * 0.5)) : 0.0f;
                o[k] = lines[k].tap ((float) len[k] + mod + 2.0f);
                damp[k] = o[k] + (damp[k] - o[k]) * dampC;
                o[k] = damp[k] * g[k];
            }
            // Householder feedback matrix: x - 2/N * sum
            float sum = 0.0f;
            for (int k = 0; k < N; ++k) sum += o[k];
            sum *= 2.0f / N;
            for (int k = 0; k < N; ++k) lines[k].push (o[k] - sum + into * (k % 2 == 0 ? 0.5f : -0.5f));
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < N; ++k) { if (k % 2 == 0) wl += o[k]; else wr += o[k]; }
            wl *= 0.6f; wr *= 0.6f;
            const float mono = 0.5f * (wl + wr);
            const float s1 = sh[0].process (mono, ratios[interval][0], grain);
            const float s2 = interval == 4 ? sh[1].process (mono, ratios[interval][1], grain) : s1;
            fbShift = std::tanh ((s1 + s2) * 0.5f);
            wl = lc[0].process (wl + fbShift * shim * 0.3f, 0);
            wr = lc[1].process (wr + fbShift * shim * 0.3f, 1);
            b.setSample (0, i, inL * dryG + wl * wetG);
            if (ch > 1) b.setSample (1, i, inR * dryG + wr * wetG);
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
            v.early = 0.0f;
            v.mix = value ("mix") * 0.01f;
            v.caption = String::fromUTF8 ("caída ") + String (v.decay, 1) + " s   shimmer " + String (value ("shimmer"), 0) + " %";
            return v;
        });
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Shimmer", { { "shimmer", "Shimmer", "Cuánto de la cola sube de afinación y vuelve a entrar."_u },
                           { "interval", "Intervalo", "Una octava arriba es el clásico; +7 suena a quinta; -12 baja."_u },
                           { "decay", "Caída"_u, "Cuánto dura la cola."_u }, { "mix", "Mezcla", "Con la señal original."_u } } },
            { "Espacio", { { "size", "Tamaño"_u, "Qué tan grande es el cuarto."_u }, { "predelay", "Predelay", "Silencio antes de la cola."_u },
                           { "damping", "Amortiguación"_u, "Apaga los agudos de la cola."_u }, { "mod", "Modulación"_u, "Un vaivén suave que la hace más ancha."_u },
                           { "lowcut", "Graves", "Quita graves de la cola para que no ensucie."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine lines[N], diff[4], pre;
    GrainShifter sh[2];
    float damp[N] {};
    Biquad lc[2];
    float fbShift = 0.0f;
    double modPh = 0.0;
};
}

void registerExtras2 (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& cat, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, cat, detail, Kind::effect, std::move (make), keys }); };
    add ("compuerta-ritmica", "Compuerta rítmica"_u, "Modulación"_u, "Trance gate de 16 pasos al tempo: corta el sonido en un ritmo."_u,
         [] { return std::make_unique<StepGate>(); }, { "div", "len", "depth", "swing", "attack", "release" });
    add ("rotativo", "Rotativo", "Modulación"_u, "Un gabinete Leslie: bocina y tambor que giran, lento, rápido o frenando."_u,
         [] { return std::make_unique<Rotary>(); }, { "speed", "accel", "balance", "doppler", "am", "mix" });
    add ("wah", "Wah", "EQ y filtros", "El pedal wah: por tu volumen, con un LFO o moviendo el pedal."_u,
         [] { return std::make_unique<Wah>(); }, { "mode", "fmin", "fmax", "reso", "sens", "mix" });
    add ("shimmer", "Shimmer", "Reverb y espacio", "Reverb que sube de octava en cada vuelta: colas que brillan."_u,
         [] { return std::make_unique<Shimmer>(); }, { "shimmer", "interval", "decay", "size", "damping", "mix" });
    add ("afinacion-automatica", "Afinación automática"_u, "Creativos y espectrales", "Lleva la voz a la escala de la canción, de natural a robot."_u,
         [] { return std::make_unique<PitchCorrect>(); }, { "key", "scale", "speed", "tol", "human", "mix" });
    add ("efectos-dj", "Efectos DJ", "Creativos y espectrales", "Freno y arranque de cinta, rebobinado, tartamudeo y reversa, para tocar en vivo."_u,
         [] { return std::make_unique<DjFx>(); }, { "active", "effect", "time", "repeat", "filter", "mix" });
}
}

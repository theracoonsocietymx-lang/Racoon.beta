// Utilidades: Utilidad, Espectro, Afinador
#include "AllEffects.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using namespace racoon::ui;

namespace
{
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }

// keeps the last samples of the input (mono) for analysis on the message thread
struct Ring
{
    static constexpr int size = 16384;
    float data[size] {};
    std::atomic<int> pos { 0 };
    void push (const AudioBuffer<float>& b, int chanMode)
    {
        int p = pos.load (std::memory_order_relaxed);
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float v;
            if (chanMode == 1 || ch == 1) v = b.getSample (0, i);
            else if (chanMode == 2) v = b.getSample (1, i);
            else v = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
            data[p] = v;
            p = (p + 1) & (size - 1);
        }
        pos.store (p, std::memory_order_release);
    }
    void latest (float* dest, int n) const
    {
        const int p = pos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dest[i] = data[(p - n + i + size) & (size - 1)];
    }
};

// ===================================================================== Utilidad
class Utility : public FxBase
{
public:
    Utility() : FxBase ("Utilidad", "Utilidades", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "gain", "Ganancia", -70.0f, 35.0f, 0.0f);
        P::pct (l, "width", "Ancho", 0.0f, 400.0f, 100.0f);
        P::flt (l, "balance", "Balance", P::range (-50.0f, 50.0f), 0.0f, "", 0);
        P::toggle (l, "mono", "Mono", false);
        P::toggle (l, "bassmono", "Graves en mono", false);
        P::hz (l, "bassfreq", "Graves en mono hasta", 50.0f, 500.0f, 120.0f);
        P::toggle (l, "invl", "Invertir fase izquierda", false);
        P::toggle (l, "invr", "Invertir fase derecha", false);
        P::choice (l, "chmode", "Canales", { "Estéreo"_u, "Solo izquierdo", "Solo derecho", "Intercambiar" }, 0);
        P::toggle (l, "mute", "Silencio", false);
        P::toggle (l, "dc", "Quitar DC", false);
        return l;
    }
    void prepare (double rate, int) override
    {
        for (auto* s : { &gl, &gr, &wd }) s->prepare (rate, 20.0);
        for (auto& d : dcb) d.prepare (rate);
        reset();
    }
    void reset() override
    {
        gl.reset (target (0)); gr.reset (target (1)); wd.reset (value ("width") * 0.01f);
        for (auto& d : dcb) d.reset();
        for (auto& f : bassLp) f.reset();
        for (auto& f : bassHp) f.reset();
    }
    float target (int side) const
    {
        if (value ("mute") > 0.5f || value ("gain") <= -69.5f) return 0.0f;
        const float g = toGain (value ("gain")), bal = value ("balance") / 50.0f;
        // equal-power balance that keeps the center at unity
        const float pan = side == 0 ? jmin (1.0f, 1.0f - bal) : jmin (1.0f, 1.0f + bal);
        const float inv = side == 0 ? (value ("invl") > 0.5f ? -1.0f : 1.0f) : (value ("invr") > 0.5f ? -1.0f : 1.0f);
        return g * pan * inv;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples();
        if (b.getNumChannels() < 2)
        {
            gl.target = target (0);
            for (int i = 0; i < n; ++i) { float x = b.getSample (0, i); if (value ("dc") > 0.5f) x = dcb[0].process (x); b.setSample (0, i, x * gl.next()); }
            return;
        }
        gl.target = target (0); gr.target = target (1);
        wd.target = value ("mono") > 0.5f ? 0.0f : value ("width") * 0.01f;
        const int mode = (int) value ("chmode");
        const bool dc = value ("dc") > 0.5f, bm = value ("bassmono") > 0.5f;
        if (bm)
            for (int k = 0; k < 2; ++k)
            {
                bassLp[k].set (dsp2::Biquad::lowpass, sr, value ("bassfreq"), 0.70710678);
                bassHp[k].set (dsp2::Biquad::highpass, sr, value ("bassfreq"), 0.70710678);
            }
        auto* L = b.getWritePointer (0);
        auto* R = b.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (dc) { l = dcb[0].process (l); r = dcb[1].process (r); }
            switch (mode)
            {
                case 1: r = l; break;
                case 2: l = r; break;
                case 3: std::swap (l, r); break;
                default: break;
            }
            const float w = wd.next();
            float m = 0.5f * (l + r), s = 0.5f * (l - r);
            if (bm)
            {
                // the side below the frequency goes away (LR4 split keeps the rest intact)
                s = bassHp[1].process (bassHp[0].process (s, 0), 0);
            }
            s *= w;
            l = m + s; r = m - s;
            L[i] = l * gl.next();
            R[i] = r * gr.next();
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Utilidad", { { "gain", "Ganancia", "Sube o baja el volumen. Al mínimo, silencio."_u },
                            { "width", "Ancho", "0 es mono, 100 normal, más de 100 abre el estéreo."_u },
                            { "balance", "Balance", "Mueve el sonido a la izquierda o a la derecha." },
                            { "mono", "Mono", "Junta los dos lados en uno." }, { "mute", "Silencio", "Calla la pista." } } },
            { "Detalles", { { "bassmono", "Graves mono", "Los graves quedan al centro aunque el resto sea ancho." },
                            { "bassfreq", "Hasta", "Hasta qué frecuencia los graves van en mono."_u },
                            { "invl", "Fase I", "Invierte la polaridad del lado izquierdo." }, { "invr", "Fase D", "Invierte la polaridad del lado derecho." },
                            { "chmode", "Canales", "Usa un solo lado o intercámbialos."_u }, { "dc", "Quitar DC", "Elimina un desplazamiento de la señal que no se oye pero quita espacio."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::Smooth gl, gr, wd;
    racoon::dsp::DCBlock dcb[2];
    dsp2::Biquad bassLp[2], bassHp[2];
};

// ===================================================================== Espectro
class Spectrum : public FxBase
{
public:
    Spectrum() : FxBase ("Espectro", "Utilidades", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "size", "Resolución"_u, { "1024", "2048", "4096", "8192" }, 2);
        P::pct (l, "avg", "Suavizado", 0.0f, 99.0f, 70.0f);
        P::choice (l, "chan", "Canal", { "Izquierdo + derecho", "Izquierdo", "Derecho" }, 0);
        P::db (l, "range", "Rango", -120.0f, -30.0f, -90.0f);
        P::toggle (l, "hold", "Mantener picos", false);
        return l;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override { ring.push (b, (int) value ("chan")); }
    Ring ring;
    std::unique_ptr<Component> makeDisplay() override;
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Espectro", { { "size", "Resolución"_u, "Más alto separa mejor los graves pero reacciona más lento."_u },
                            { "avg", "Suavizado", "Promedia para que la curva no tiemble." }, { "chan", "Canal", "Qué lado se mide."_u },
                            { "range", "Rango", "Qué tan abajo llega la escala."_u }, { "hold", "Picos", "Deja marcado el máximo de cada frecuencia."_u } } },
        };
        s.displayEasy = 330;
        return s;
    }
};

class SpectrumDisplay : public Component, private Timer
{
public:
    explicit SpectrumDisplay (Spectrum& p) : proc (p) { startTimerHz (30); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
        drawScreenGrid (g, r, 10, 6);
        const float floorDb = proc.value ("range");
        g.setColour (Pro::textDim);
        g.setFont (Fonts::label (10.5f));
        for (double hz : { 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 })
            g.drawText (::racoon::i18n::tr (hz >= 1000 ? String ((int) (hz / 1000)) + "k" : String ((int) hz)), Rectangle<float> (xFor (r, hz) + 3, r.getBottom() - 14, 40, 12), Justification::left);
        auto curve = [&] (const std::vector<float>& v, Colour c, float thick)
        {
            if (v.empty()) return;
            Path p;
            bool started = false;
            const int bins = (int) v.size();
            for (int px = 0; px <= (int) r.getWidth(); px += 2)
            {
                const double hz = 20.0 * std::pow (1000.0, px / (double) r.getWidth());
                const double bin = hz / (proc.sampleRate() / (2.0 * (bins - 1)));
                const int i = jlimit (0, bins - 2, (int) bin);
                const float f = (float) (bin - i);
                const float db = v[(size_t) i] + (v[(size_t) i + 1] - v[(size_t) i]) * f;
                const float y = jmap (jlimit (floorDb, 0.0f, db), floorDb, 0.0f, r.getBottom(), r.getY());
                if (! started) { p.startNewSubPath (r.getX() + px, y); started = true; } else p.lineTo (r.getX() + px, y);
            }
            g.setColour (c);
            g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        };
        if (proc.value ("hold") > 0.5f) curve (peaks, Pro::reduce, 1.0f);
        curve (smooth, Pro::value, 1.5f);
        if (! smooth.empty())
        {
            // name of the loudest frequency
            int best = 1;
            for (int i = 2; i < (int) smooth.size(); ++i) if (smooth[(size_t) i] > smooth[(size_t) best]) best = i;
            const double hz = best * proc.sampleRate() / (2.0 * (smooth.size() - 1));
            if (smooth[(size_t) best] > floorDb + 6.0f)
            {
                g.setColour (Pro::text);
                g.setFont (Fonts::label (13.0f));
                g.drawText (::racoon::i18n::tr ("Pico: " + String (hz, 0) + " Hz"), r.reduced (8, 4).removeFromTop (18), Justification::topLeft);
            }
        }
    }
private:
    Spectrum& proc;
    std::vector<float> smooth, peaks, window, work;
    int order = 0;
    static float xFor (Rectangle<float> r, double hz) { return r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); }
    void timerCallback() override
    {
        const int ord = 10 + (int) proc.value ("size");
        const int n = 1 << ord;
        if (ord != order)
        {
            order = ord;
            fft = std::make_unique<juce::dsp::FFT> (ord);
            window.resize ((size_t) n);
            for (int i = 0; i < n; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (float) (n - 1));
            smooth.assign ((size_t) n / 2 + 1, -140.0f);
            peaks.assign ((size_t) n / 2 + 1, -140.0f);
        }
        work.assign ((size_t) n * 2, 0.0f);
        proc.ring.latest (work.data(), n);
        for (int i = 0; i < n; ++i) work[(size_t) i] *= window[(size_t) i];
        fft->performFrequencyOnlyForwardTransform (work.data(), true);
        const float a = proc.value ("avg") * 0.01f;
        const float norm = 4.0f / (float) n;
        for (size_t i = 0; i < smooth.size(); ++i)
        {
            const float db = 20.0f * std::log10 (jmax (1.0e-7f, work[i] * norm));
            smooth[i] = db > smooth[i] ? db * (1.0f - a * 0.5f) + smooth[i] * a * 0.5f : db * (1.0f - a) + smooth[i] * a;
            peaks[i] = jmax (peaks[i] - 0.05f, smooth[i]);
        }
        repaint();
    }
    std::unique_ptr<juce::dsp::FFT> fft;
};

std::unique_ptr<Component> Spectrum::makeDisplay() { return std::make_unique<SpectrumDisplay> (*this); }

// ===================================================================== Afinador
class Tuner : public FxBase
{
public:
    Tuner() : FxBase ("Afinador", "Utilidades", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::flt (l, "ref", "Referencia (La)", P::range (410.0f, 480.0f, 0.0f, 0.1f), 440.0f, " Hz", 1);
        P::toggle (l, "mute", "Silenciar la salida", false);
        return l;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        ring.push (b, 0);
        if (value ("mute") > 0.5f) b.clear();
    }
    // YIN on the latest window; 0 when there is no clear pitch
    static float detect (const float* x, int n, double rate)
    {
        const int maxTau = jmin (n / 2, (int) (rate / 30.0)), minTau = jmax (2, (int) (rate / 2000.0));
        const int w = n - maxTau;
        float energy = 0.0f;
        for (int i = 0; i < n; ++i) energy += x[i] * x[i];
        if (energy / (float) n < 1.0e-7f) return 0.0f;
        std::vector<float> d ((size_t) maxTau + 1, 0.0f);
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float s = 0.0f;
            for (int i = 0; i < w; ++i) { const float e = x[i] - x[i + tau]; s += e * e; }
            d[(size_t) tau] = s;
        }
        // cumulative mean normalized difference
        float run = 0.0f;
        d[0] = 1.0f;
        for (int tau = 1; tau <= maxTau; ++tau) { run += d[(size_t) tau]; d[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0f; }
        int tau = -1;
        for (int t = minTau; t < maxTau; ++t)
            if (d[(size_t) t] < 0.12f)
            {
                while (t + 1 < maxTau && d[(size_t) t + 1] < d[(size_t) t]) ++t;
                tau = t; break;
            }
        if (tau < 0) return 0.0f;
        // parabolic interpolation
        const float a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
        const float den = a - 2.0f * b + c;
        const float t = (float) tau + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
        return (float) (rate / t);
    }
    float currentHz() const
    {
        const int n = 4096;
        std::vector<float> buf ((size_t) n);
        ring.latest (buf.data(), n);
        return detect (buf.data(), n, sampleRate());
    }
    static String noteName (int midi)
    {
        static const char* names[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        return String (names[((midi % 12) + 12) % 12]) + String (midi / 12 - 1);
    }
    // note and cents for a frequency with the current reference
    std::pair<int, float> noteFor (float hz) const
    {
        const double m = 69.0 + 12.0 * std::log2 (hz / value ("ref"));
        const int note = (int) std::round (m);
        return { note, (float) ((m - note) * 100.0) };
    }
    String displayText() const override
    {
        const float hz = lastHz.load();
        if (hz <= 0.0f) return "Sin nota";
        auto [note, cents] = noteFor (hz);
        return noteName (note) + "  " + (cents >= 0 ? "+" : "") + String (cents, 0) + " cents  (" + String (hz, 1) + " Hz)";
    }
    mutable std::atomic<float> lastHz { 0.0f };
    Ring ring;
    std::unique_ptr<Component> makeDisplay() override;
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = { { "Afinador", { { "ref", "Referencia", "La frecuencia del La. Casi siempre 440." },
                                       { "mute", "Silenciar", "Calla la salida mientras afinas." } } } };
        s.displayEasy = 330;
        return s;
    }
};

class TunerDisplay : public Component, private Timer
{
public:
    explicit TunerDisplay (Tuner& t) : proc (t) { startTimerHz (15); }
    void paint (Graphics& g) override
    {
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (18.0f, 14.0f);
        const float hz = proc.lastHz.load();
        String name = "--";
        float cents = 0.0f;
        if (hz > 0.0f) { auto nc = proc.noteFor (hz); name = Tuner::noteName (nc.first); cents = nc.second; }
        const bool inTune = hz > 0.0f && std::abs (cents) < 3.0f;
        g.setColour (inTune ? Pro::second : Pro::text);
        g.setFont (NumFont::semi (60.0f));
        g.drawText (::racoon::i18n::tr (name), r.removeFromTop (r.getHeight() * 0.55f), Justification::centred);
        // the needle scale: -50 .. +50 cents
        auto scale = r.removeFromTop (34.0f).reduced (20.0f, 0.0f);
        g.setColour (Pro::textDim);
        for (int c = -50; c <= 50; c += 10)
        {
            const float x = scale.getX() + scale.getWidth() * (c + 50) / 100.0f;
            g.fillRect (x - 0.5f, scale.getY() + (c == 0 ? 0.0f : 8.0f), 1.0f, c == 0 ? scale.getHeight() : scale.getHeight() - 16.0f);
        }
        if (hz > 0.0f)
        {
            const float x = scale.getX() + scale.getWidth() * (jlimit (-50.0f, 50.0f, needle) + 50.0f) / 100.0f;
            g.setColour (inTune ? Pro::second : Pro::value);
            g.fillRoundedRectangle (x - 3.0f, scale.getY() - 2.0f, 6.0f, scale.getHeight() + 4.0f, 3.0f);
        }
        g.setColour (Pro::textDim);
        g.setFont (Fonts::label (13.0f));
        g.drawText (::racoon::i18n::tr (hz > 0.0f ? String (hz, 1) + " Hz   " + (cents >= 0 ? "+" : "") + String (cents, 0) + " cents" : "Toca una nota"), r.removeFromTop (24.0f), Justification::centred);
    }
private:
    Tuner& proc;
    float needle = 0.0f;
    void timerCallback() override
    {
        const float hz = proc.currentHz();
        proc.lastHz.store (hz);
        if (hz > 0.0f) needle = needle * 0.6f + proc.noteFor (hz).second * 0.4f;
        repaint();
    }
};

std::unique_ptr<Component> Tuner::makeDisplay() { return std::make_unique<TunerDisplay> (*this); }
}

// used by the tests: pitch of a mono signal (0 when none)
float tunerDetect (const float* x, int n, double rate) { return Tuner::detect (x, n, rate); }

void registerUtility (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Utilidades", detail, Kind::effect, std::move (make), keys }); };
    add ("utilidad", "Utilidad", "Volumen, ancho estéreo, mono, balance e inversión de fase. La navaja suiza."_u,
         [] { return std::make_unique<Utility>(); }, { "gain", "width", "balance", "mono", "bassmono", "mute" });
    add ("espectro", "Espectro", "Muestra qué frecuencias suenan. No cambia el sonido."_u,
         [] { return std::make_unique<Spectrum>(); }, { "size", "avg", "range" });
    add ("afinador", "Afinador", "Dice qué nota tocas y cuánto le falta para estar afinada."_u,
         [] { return std::make_unique<Tuner>(); }, { "ref", "mute" });
}
}

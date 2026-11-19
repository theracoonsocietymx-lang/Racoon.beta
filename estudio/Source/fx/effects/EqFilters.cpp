// EQ y filtros: EQ de tres bandas, EQ de canal
#include "AllEffects.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }

// Linkwitz-Riley split: lp + hp sum to an allpass. order 4 = two Butterworth sections, order 8 = two 4th-order Butterworths.
struct LRSplit
{
    Biquad lp[4], hp[4];
    int sections = 2;
    void set (double sr, double f, bool steep)
    {
        sections = steep ? 4 : 2;
        static const double q4[] { 0.70710678, 0.70710678 }, q8[] { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
        for (int k = 0; k < sections; ++k)
        {
            const double q = steep ? q8[k] : q4[k];
            lp[k].set (Biquad::lowpass, sr, f, q);
            hp[k].set (Biquad::highpass, sr, f, q);
        }
    }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    float low (float x, int c)  { for (int k = 0; k < sections; ++k) x = lp[k].process (x, c); return x; }
    float high (float x, int c) { for (int k = 0; k < sections; ++k) x = hp[k].process (x, c); return x; }
    double lowDb (double f, double sr) const  { double d = 0; for (int k = 0; k < sections; ++k) d += lp[k].magnitudeDb (f, sr); return d; }
    double highDb (double f, double sr) const { double d = 0; for (int k = 0; k < sections; ++k) d += hp[k].magnitudeDb (f, sr); return d; }
};

// ===================================================================== EQ de tres bandas (DJ style)
class EqThree : public FxBase
{
public:
    EqThree() : FxBase ("EQ de tres bandas", "EQ y filtros", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::db (l, "low", "Graves", -70.0f, 6.0f, 0.0f);
        P::db (l, "mid", "Medios", -70.0f, 6.0f, 0.0f);
        P::db (l, "high", "Agudos", -70.0f, 6.0f, 0.0f);
        P::toggle (l, "onlow", "Graves encendidos", true);
        P::toggle (l, "onmid", "Medios encendidos", true);
        P::toggle (l, "onhigh", "Agudos encendidos", true);
        P::hz (l, "flow", "Corte graves", 50.0f, 5000.0f, 250.0f);
        P::hz (l, "fhigh", "Corte agudos", 200.0f, 18000.0f, 2500.0f);
        P::choice (l, "slope", "Pendiente", { "24 dB/oct", "48 dB/oct" }, 0);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void reset() override { a.reset(); b.reset(); apLow.reset(); for (auto& s : sm) s.reset (1.0f); }
    void prepare (double rate, int) override { for (auto& s : sm) s.prepare (rate, 15.0); reset(); }
    void process (AudioBuffer<float>& buf, MidiBuffer&) override
    {
        const bool steep = value ("slope") > 0.5f;
        const double fl = value ("flow"), fh = jmax ((double) value ("fhigh"), fl * 1.05);
        a.set (sr, fl, steep); b.set (sr, fh, steep); apLow.set (sr, fh, steep);
        auto g = [this] (const char* gid, const char* on) { return value (on) > 0.5f && value (gid) > -69.5f ? toGain (value (gid)) : 0.0f; };
        sm[0].target = g ("low", "onlow"); sm[1].target = g ("mid", "onmid"); sm[2].target = g ("high", "onhigh");
        const float out = toGain (value ("output"));
        const int n = buf.getNumSamples(), ch = jmin (2, buf.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float gl = sm[0].next(), gm = sm[1].next(), gh = sm[2].next();
            for (int c = 0; c < ch; ++c)
            {
                const float x = buf.getSample (c, i);
                // the low band goes through the same allpass the other two get from the second split
                const float lo0 = a.low (x, c);
                const float lo = apLow.low (lo0, c) + apLow.high (lo0, c);
                const float rest = a.high (x, c);
                const float mi = b.low (rest, c), hi = b.high (rest, c);
                buf.setSample (c, i, (lo * gl + mi * gm + hi * gh) * out);
            }
        }
    }
    float responseDb (double f) const
    {
        LRSplit x, y;
        const bool steep = value ("slope") > 0.5f;
        const double fl = value ("flow"), fh = jmax ((double) value ("fhigh"), fl * 1.05);
        x.set (sr, fl, steep); y.set (sr, fh, steep);
        auto g = [this] (const char* gid, const char* on) { return value (on) > 0.5f && value (gid) > -69.5f ? (double) toGain (value (gid)) : 0.0; };
        // magnitudes only: a fair picture of the band levels (phase ignored)
        const double l = std::pow (10.0, x.lowDb (f, sr) / 20.0), r = std::pow (10.0, x.highDb (f, sr) / 20.0);
        const double m = r * std::pow (10.0, y.lowDb (f, sr) / 20.0), h = r * std::pow (10.0, y.highDb (f, sr) / 20.0);
        const double sum = l * l * g ("low", "onlow") + m * m * g ("mid", "onmid") + h * h * g ("high", "onhigh");
        return (float) (10.0 * std::log10 (jmax (1.0e-9, sum)) + value ("output"));
    }
    std::unique_ptr<Component> makeDisplay() override { return dsp2::makeResponseDisplay (*this, [this] (double f) { return responseDb (f); }, 24.0f); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Bandas", { { "low", "Graves", "Volumen de los graves. Al mínimo desaparecen."_u }, { "mid", "Medios", "Volumen de los medios." },
                          { "high", "Agudos", "Volumen de los agudos." },
                          { "onlow", "Graves", "Apaga o prende la banda de golpe, como en una mezcladora de DJ." },
                          { "onmid", "Medios", "Apaga o prende la banda de golpe." }, { "onhigh", "Agudos", "Apaga o prende la banda de golpe." } } },
            { "Cortes", { { "flow", "Corte graves", "Dónde terminan los graves y empiezan los medios."_u },
                          { "fhigh", "Corte agudos", "Dónde terminan los medios y empiezan los agudos."_u },
                          { "slope", "Pendiente", "48 separa más las bandas; 24 suena más natural."_u }, { "output", "Salida", "Volumen final." } }, true },
        };
        return s;
    }
private:
    LRSplit a, b, apLow;
    racoon::dsp::Smooth sm[3];
};

// ===================================================================== EQ de canal (sencillo, de consola)
class ChannelEq : public FxBase
{
public:
    ChannelEq() : FxBase ("EQ de canal", "EQ y filtros", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::toggle (l, "hp", "Corte de graves (80 Hz)", false);
        P::db (l, "low", "Graves", -15.0f, 15.0f, 0.0f);
        P::hz (l, "midfreq", "Frecuencia de medios", 120.0f, 7500.0f, 1000.0f);
        P::db (l, "mid", "Medios", -12.0f, 12.0f, 0.0f);
        P::db (l, "high", "Agudos", -15.0f, 15.0f, 0.0f);
        P::db (l, "output", "Salida", -24.0f, 24.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int) override { out.prepare (rate, 20.0); reset(); }
    void reset() override { for (auto& f : f_) f.reset(); out.reset (1.0f); }
    void design (Biquad* f, double rate) const
    {
        f[0].set (Biquad::highpass, rate, 80.0, 0.707);
        f[1].set (Biquad::highpass, rate, 80.0, 0.707);
        f[2].set (Biquad::lowshelf, rate, 100.0, 0.707, value ("low"));
        // the mid gets wider as the gain gets smaller, like a console
        const float mg = value ("mid");
        f[3].set (Biquad::peak, rate, value ("midfreq"), 0.5 + std::abs (mg) / 12.0 * 0.5, mg);
        // high: boost is a shelf, cut also closes a gentle low-pass, like the Channel EQ of consoles
        const float hg = value ("high");
        f[4].set (Biquad::highshelf, rate, 6000.0, 0.707, hg > 0.0f ? hg : hg * 0.5f);
        f[5].set (Biquad::lowpass, rate, hg < 0.0f ? jmap (-hg, 0.0f, 15.0f, 22000.0f, 4000.0f) : rate * 0.49, 0.6);
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        design (f_, sr);
        const bool hp = value ("hp") > 0.5f, lp = value ("high") < 0.0f;
        out.target = toGain (value ("output"));
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float g = out.next();
            for (int c = 0; c < ch; ++c)
            {
                float x = b.getSample (c, i);
                if (hp) x = f_[1].process (f_[0].process (x, c), c);
                x = f_[4].process (f_[3].process (f_[2].process (x, c), c), c);
                if (lp) x = f_[5].process (x, c);
                b.setSample (c, i, x * g);
            }
        }
    }
    float responseDb (double f) const
    {
        Biquad d[6];
        design (d, sr);
        double db = value ("output");
        if (value ("hp") > 0.5f) db += d[0].magnitudeDb (f, sr) + d[1].magnitudeDb (f, sr);
        for (int k = 2; k < 5; ++k) db += d[k].magnitudeDb (f, sr);
        if (value ("high") < 0.0f) db += d[5].magnitudeDb (f, sr);
        return (float) db;
    }
    std::unique_ptr<Component> makeDisplay() override { return dsp2::makeResponseDisplay (*this, [this] (double f) { return responseDb (f); }, 18.0f); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "EQ de canal", { { "hp", "Corte 80 Hz", "Quita los graves que no sirven (ruido, golpes del micrófono)."_u },
                               { "low", "Graves", "Más o menos cuerpo."_u }, { "midfreq", "Frecuencia", "Qué zona de los medios mueves."_u },
                               { "mid", "Medios", "Sube o baja esa zona." }, { "high", "Agudos", "Más brillo o más suave."_u },
                               { "output", "Salida", "Volumen final." } } },
        };
        return s;
    }
private:
    Biquad f_[6];
    racoon::dsp::Smooth out;
};

// ===================================================================== EQ de mezcla (paramétrico, el de la mezcla automática)
// A cut of lows with its frequency and slope, two shelves, three bells and a cut of highs: what an engineer reaches for
// on every channel. Racoon's automatic mix uses it to carve each part its place.
class MixEq : public FxBase
{
public:
    MixEq() : FxBase ("EQ de mezcla", "EQ y filtros", layout()) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::toggle (l, "hpon", "Corte de graves", false);
        P::hz (l, "hpfreq", "Corte de graves: frecuencia", 20.0f, 1000.0f, 30.0f);
        P::choice (l, "hpslope", "Corte de graves: pendiente", { "12 dB/oct", "24 dB/oct" }, 1);
        P::hz (l, "lsfreq", "Graves: frecuencia", 30.0f, 500.0f, 100.0f);
        P::db (l, "lsgain", "Graves", -12.0f, 12.0f, 0.0f);
        P::hz (l, "b1freq", "Banda 1: frecuencia", 40.0f, 2000.0f, 300.0f);
        P::db (l, "b1gain", "Banda 1", -12.0f, 12.0f, 0.0f);
        P::flt (l, "b1q", "Banda 1: Q", P::range (0.2f, 10.0f, 1.0f), 1.0f, "", 2);
        P::hz (l, "b2freq", "Banda 2: frecuencia", 200.0f, 8000.0f, 1000.0f);
        P::db (l, "b2gain", "Banda 2", -12.0f, 12.0f, 0.0f);
        P::flt (l, "b2q", "Banda 2: Q", P::range (0.2f, 10.0f, 1.0f), 1.0f, "", 2);
        P::hz (l, "b3freq", "Banda 3: frecuencia", 1000.0f, 16000.0f, 3500.0f);
        P::db (l, "b3gain", "Banda 3", -12.0f, 12.0f, 0.0f);
        P::flt (l, "b3q", "Banda 3: Q", P::range (0.2f, 10.0f, 1.0f), 1.0f, "", 2);
        P::hz (l, "hsfreq", "Agudos: frecuencia", 2000.0f, 16000.0f, 9000.0f);
        P::db (l, "hsgain", "Agudos", -12.0f, 12.0f, 0.0f);
        P::toggle (l, "lpon", "Corte de agudos", false);
        P::hz (l, "lpfreq", "Corte de agudos: frecuencia", 1000.0f, 20000.0f, 18000.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }
    void prepare (double rate, int) override { out.prepare (rate, 20.0); reset(); }
    void reset() override { for (auto& f : f_) f.reset(); out.reset (1.0f); primed = false; }
    // the values the filters use: frequencies and Q in log, gains in dB
    static constexpr int numSm = 13;
    void targets (float* t) const
    {
        const char* ids[numSm] = { "hpfreq", "lsfreq", "lsgain", "b1freq", "b1gain", "b1q", "b2freq", "b2gain", "b2q", "b3freq", "b3gain", "b3q", "hsfreq" };
        for (int k = 0; k < numSm; ++k)
        {
            const float v = value (ids[k]);
            t[k] = String (ids[k]).endsWith ("gain") ? v : std::log (jmax (0.01f, v));
        }
    }
    void designFrom (Biquad* f, double rate, const float* v, float hs, float hsg, float lpf) const
    {
        const double ny = rate * 0.45;
        auto e = [v] (int k) { return (double) std::exp (v[k]); };
        f[0].set (Biquad::highpass, rate, jmin (e (0), ny), 0.5412);
        f[1].set (Biquad::highpass, rate, jmin (e (0), ny), value ("hpslope") > 0.5f ? 1.3066 : 0.7071);
        f[2].set (Biquad::lowshelf, rate, e (1), 0.707, v[2]);
        f[3].set (Biquad::peak, rate, jmin (e (3), ny), e (5), v[4]);
        f[4].set (Biquad::peak, rate, jmin (e (6), ny), e (8), v[7]);
        f[5].set (Biquad::peak, rate, jmin (e (9), ny), e (11), v[10]);
        f[6].set (Biquad::highshelf, rate, jmin ((double) hs, ny), 0.707, hsg);
        f[7].set (Biquad::lowpass, rate, jmin ((double) lpf, ny), 0.707);
    }
    void design (Biquad* f, double rate) const
    {
        float t[numSm];
        targets (t);
        designFrom (f, rate, t, std::exp (t[12]), value ("hsgain"), value ("lpfreq"));
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        float t[numSm];
        targets (t);
        const float hsgT = value ("hsgain"), lpT = std::log (value ("lpfreq"));
        if (! primed) { std::copy (t, t + numSm, sm); hsg = hsgT; lpLog = lpT; primed = true; }
        const bool hp = value ("hpon") > 0.5f, steep = value ("hpslope") > 0.5f, lp = value ("lpon") > 0.5f;
        bool used[8] { hp && steep, hp, std::abs (value ("lsgain")) > 0.01f || std::abs (sm[2]) > 0.01f,
                       std::abs (value ("b1gain")) > 0.01f || std::abs (sm[4]) > 0.01f, std::abs (value ("b2gain")) > 0.01f || std::abs (sm[7]) > 0.01f,
                       std::abs (value ("b3gain")) > 0.01f || std::abs (sm[10]) > 0.01f, std::abs (hsgT) > 0.01f || std::abs (hsg) > 0.01f, lp };
        for (int k = 0; k < 8; ++k) if (used[k] && ! wasUsed[k]) f_[k].reset();   // a filter that comes in starts clean
        std::copy (used, used + 8, wasUsed);
        // five boosts stacked on the same frequency would add 60 dB: past 30 dB of boosts, the output gives back the rest
        float boosts = jmax (0.0f, value ("output"));
        for (auto* id : { "lsgain", "b1gain", "b2gain", "b3gain", "hsgain" }) boosts += jmax (0.0f, value (id));
        out.target = toGain (value ("output") - jmax (0.0f, boosts - 30.0f));
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        // the filters move toward the knobs every 32 samples (about 4 ms to get most of the way): a jump in frequency
        // or Q with signal inside the filter would ring
        constexpr int sub = 32;
        const float a = 0.25f;
        for (int i0 = 0; i0 < n; i0 += sub)
        {
            for (int k = 0; k < numSm; ++k) sm[k] += a * (t[k] - sm[k]);
            hsg += a * (hsgT - hsg);
            lpLog += a * (lpT - lpLog);
            designFrom (f_, sr, sm, std::exp (sm[12]), hsg, std::exp (lpLog));
            const int end = jmin (n, i0 + sub);
            for (int i = i0; i < end; ++i)
            {
                const float g = out.next();
                for (int c = 0; c < ch; ++c)
                {
                    float x = b.getSample (c, i);
                    for (int k = 0; k < 8; ++k) if (used[k]) x = f_[k].process (x, c);
                    b.setSample (c, i, x * g);
                }
            }
        }
    }
    float responseDb (double f) const
    {
        Biquad d[8];
        design (d, sr);
        double db = value ("output");
        const bool hp = value ("hpon") > 0.5f;
        if (hp && value ("hpslope") > 0.5f) db += d[0].magnitudeDb (f, sr);
        if (hp) db += d[1].magnitudeDb (f, sr);
        for (int k = 2; k < 7; ++k) db += d[k].magnitudeDb (f, sr);
        if (value ("lpon") > 0.5f) db += d[7].magnitudeDb (f, sr);
        return (float) db;
    }
    std::unique_ptr<Component> makeDisplay() override { return dsp2::makeResponseDisplay (*this, [this] (double f) { return responseDb (f); }, 18.0f); }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Cortes", { { "hpon", "Corte de graves", "Quita lo que está debajo de la frecuencia: deja los graves al bombo y al bajo."_u },
                          { "hpfreq", "Desde", "Hasta dónde corta los graves."_u }, { "hpslope", "Pendiente", "Qué tan de golpe corta."_u },
                          { "lpon", "Corte de agudos", "Quita lo de arriba: más oscuro, más lejos."_u }, { "lpfreq", "Hasta", "Dónde empieza a cortar."_u } } },
            { "Bandas", { { "lsfreq", "Graves Hz", "Dónde empieza el estante de graves."_u }, { "lsgain", "Graves", "Más o menos cuerpo."_u },
                          { "b1freq", "Banda 1 Hz", "Los medios graves: el lodo vive entre 200 y 500 Hz."_u }, { "b1gain", "Banda 1", "Súbela o bájala."_u }, { "b1q", "Q 1", "Qué tan angosta."_u },
                          { "b2freq", "Banda 2 Hz", "Los medios: el cuerpo de casi todo."_u }, { "b2gain", "Banda 2", "Súbela o bájala."_u }, { "b2q", "Q 2", "Qué tan angosta."_u },
                          { "b3freq", "Banda 3 Hz", "La presencia: entre 2 y 6 kHz se oye el golpe y la claridad."_u }, { "b3gain", "Banda 3", "Súbela o bájala."_u }, { "b3q", "Q 3", "Qué tan angosta."_u },
                          { "hsfreq", "Agudos Hz", "Dónde empieza el aire."_u }, { "hsgain", "Agudos", "Más brillo o más suave."_u }, { "output", "Salida", "Volumen final." } } },
        };
        return s;
    }
private:
    Biquad f_[8];
    racoon::dsp::Smooth out;
    float sm[numSm] {}, hsg = 0.0f, lpLog = 0.0f;
    bool primed = false, wasUsed[8] {};
};
}

void registerEqFilters (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "EQ y filtros", detail, Kind::effect, std::move (make), keys }); };
    add ("eq-tres", "EQ de tres bandas", "Graves, medios y agudos con botones para quitar cada banda, como en una mezcladora de DJ."_u,
         [] { return std::make_unique<EqThree>(); }, { "low", "mid", "high", "flow", "fhigh" });
    add ("eq-canal", "EQ de canal", "Tres perillas y un corte de graves. Lo primero que pones en cualquier pista."_u,
         [] { return std::make_unique<ChannelEq>(); }, { "hp", "low", "midfreq", "mid", "high", "output" });
    add ("eq-mezcla", "EQ de mezcla", "Paramétrico: corte de graves y de agudos con su frecuencia, dos estantes y tres campanas. El de la mezcla automática."_u,
         [] { return std::make_unique<MixEq>(); }, { "hpfreq", "b1freq", "b1gain", "b3freq", "b3gain", "hsgain" });
}
}

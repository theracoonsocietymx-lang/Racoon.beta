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
}

void registerEqFilters (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "EQ y filtros", detail, Kind::effect, std::move (make), keys }); };
    add ("eq-tres", "EQ de tres bandas", "Graves, medios y agudos con botones para quitar cada banda, como en una mezcladora de DJ."_u,
         [] { return std::make_unique<EqThree>(); }, { "low", "mid", "high", "flow", "fhigh" });
    add ("eq-canal", "EQ de canal", "Tres perillas y un corte de graves. Lo primero que pones en cualquier pista."_u,
         [] { return std::make_unique<ChannelEq>(); }, { "hp", "low", "midfreq", "mid", "high", "output" });
}
}

// Sidechain: one channel lowers another. The key (the "clave") is the signal of another channel connected to this
// effect (the kick, usually); when it hits, this channel goes down and comes back. Two ways to do it:
//  - Ducking: every hit over the threshold lowers the channel by Profundidad, holds, and comes back with its own curve
//    (the house pump, the bass that gets out of the kick's way, the music that goes down under a voice).
//  - Compresor: a real compressor (threshold, ratio, knee) whose detector listens to the key.
// The key can be filtered (only the kick's low end, or only the hi-hats), listened to alone, and the channel can be
// delayed a few milliseconds so the reduction is already there when the hit lands (anticipar).
#include "AllEffects.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;

namespace
{
inline float toDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
inline float toGain (float db) { return std::pow (10.0f, db * 0.05f); }
}

class Sidechain : public FxBase
{
public:
    Sidechain() : FxBase ("Sidechain", "Dinámica"_u, layout(), 0.0, true) { showsGR = true; }

    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Ducking (bombeo)", "Compresor" }, 0);
        P::db (l, "thresh", "Umbral", -60.0f, 0.0f, -30.0f);
        P::db (l, "depth", "Profundidad", -48.0f, 0.0f, -18.0f);
        P::flt (l, "ratio", "Ratio", P::range (1.0f, 20.0f, 4.0f), 6.0f, ":1", 1);
        P::db (l, "knee", "Rodilla", 0.0f, 24.0f, 6.0f);
        P::ms (l, "attack", "Ataque", 0.1f, 100.0f, 1.0f, 5.0f);
        P::ms (l, "hold", "Retención"_u, 0.0f, 500.0f, 30.0f, 60.0f);
        P::ms (l, "release", "Relajación"_u, 5.0f, 2000.0f, 220.0f, 250.0f);
        P::pct (l, "curve", "Curva de subida", 0.0f, 100.0f, 50.0f);
        P::hz (l, "keyhp", "Clave: quitar graves", 20.0f, 2000.0f, 20.0f);
        P::hz (l, "keylp", "Clave: quitar agudos", 60.0f, 20000.0f, 20000.0f);
        P::toggle (l, "listen", "Escuchar la clave", false);
        P::choice (l, "source", "Clave", { "Otro canal (el conectado)", "Este mismo canal" }, 0);
        P::toggle (l, "look", "Anticipar 5 ms", false);
        P::toggle (l, "invert", "Al revés (suena con la clave)"_u, false);
        P::db (l, "makeup", "Ganancia", -12.0f, 24.0f, 0.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }

    void prepare (double rate, int) override
    {
        maxLook = (int) std::ceil (rate * 0.005);
        for (auto& d : delay) d.assign ((size_t) maxLook + 1, 0.0f);
        reset();
    }

    void reset() override
    {
        env = 0.0f; grEnv = 0.0f; holdLeft = 0; releaseFrom = 0.0f; releasePos = 1.0f; keyLevel = 0.0f;
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        w = 0;
    }

    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const bool own = (int) value ("source") == 1;
        const bool external = ! own && sidechain (0) != nullptr;
        usingExternal.store (external);
        listeningOwn.store (own);
        // "Otro canal" with nobody connected: nothing to listen to, the channel passes untouched
        const bool silentKey = ! own && ! external;
        const bool ducking = (int) value ("mode") == 0;
        const float thr = value ("thresh"), depth = value ("depth"), ratio = jmax (1.0f, value ("ratio")), knee = value ("knee");
        const float atk = dsp2::coeffMs (sr, value ("attack")), keyRel = dsp2::coeffMs (sr, 30.0);
        const float relMs = jmax (5.0f, value ("release"));
        const int holdSamples = (int) (value ("hold") * 0.001f * (float) sr);
        const float curve = value ("curve") * 0.01f;
        const float makeup = value ("makeup"), mix = value ("mix") * 0.01f;
        const bool listen = value ("listen") > 0.5f, invert = value ("invert") > 0.5f;
        const float hpHz = value ("keyhp"), lpHz = value ("keylp");
        const bool useHp = hpHz > 21.0f, useLp = lpHz < 19900.0f;
        if (useHp && std::abs (hpHz - lastHp) > 0.5f) { for (auto& f : hp) f.set (Biquad::highpass, sr, hpHz, 0.707); lastHp = hpHz; }
        if (useLp && std::abs (lpHz - lastLp) > 0.5f) { for (auto& f : lp) f.set (Biquad::lowpass, sr, jmin ((double) lpHz, sr * 0.45), 0.707); lastLp = lpHz; }

        // anticipar: the channel is heard 5 ms late (the host compensates), so the reduction is ready on the hit
        const int look = value ("look") > 0.5f ? maxLook : 0;
        if (look != latency) { latency = look; setLatencySamples (look); }

        float maxGr = 0.0f, peakKey = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // the key, filtered
            float k = 0.0f, keyOut[2] { 0.0f, 0.0f };
            for (int c = 0; c < 2; ++c)
            {
                const int sc = jmin (c, ch - 1);
                float x = silentKey ? 0.0f : (external ? sidechain (c)[i] : b.getSample (sc, i));
                if (useHp) x = hp[c].process (x, c);
                if (useLp) x = lp[c].process (x, c);
                keyOut[c] = x;
                k = jmax (k, std::abs (x));
            }
            // a peak follower: instant up, 30 ms down (the detector's own smoothing; attack and release shape the gain)
            keyLevel = k > keyLevel ? k : k + (keyLevel - k) * keyRel;
            const float lvl = toDb (keyLevel);
            peakKey = jmax (peakKey, lvl);

            float target;            // reduction wanted now, in dB (>= 0)
            if (ducking)
            {
                // over the threshold: full depth; the knee fades it in just under the threshold
                const float over = lvl - thr;
                const float amount = knee > 0.01f ? jlimit (0.0f, 1.0f, (over + knee) / knee) : (over >= 0.0f ? 1.0f : 0.0f);
                target = -depth * amount;
            }
            else
            {
                const float over = lvl - thr;
                float gr = 0.0f;
                const float half = knee * 0.5f;
                if (over > half) gr = over * (1.0f - 1.0f / ratio);
                else if (knee > 0.01f && over > -half) gr = (1.0f - 1.0f / ratio) * (over + half) * (over + half) / (2.0f * knee);
                target = jmin (gr, -depth);
            }

            // attack towards more reduction; hold; then release along a curve (0 % fast at first, 100 % slow at first: more pump)
            if (target >= env)
            {
                env = target + (env - target) * atk;
                holdLeft = holdSamples;
                releasePos = 1.0f;
            }
            else if (holdLeft > 0) --holdLeft;
            else
            {
                if (releasePos >= 1.0f) { releasePos = 0.0f; releaseFrom = env; }
                releasePos = jmin (1.0f, releasePos + 1.0f / (relMs * 0.001f * (float) sr));
                const float t = releasePos;
                const float shapeFast = 1.0f - (1.0f - t) * (1.0f - t);          // comes back quickly, then slows
                const float shapeSlow = t * t;                                    // stays down, then comes back
                const float s = shapeFast + (shapeSlow - shapeFast) * curve;
                env = jmax (target, releaseFrom * (1.0f - s));
            }
            grEnv = env;
            maxGr = jmax (maxGr, invert ? jmax (0.0f, -depth - env) : env);

            // al revés: down while the key is quiet, up on each hit (never above unity, even right after the depth changes)
            const float gainDb = invert ? jmin (0.0f, depth + env) : -env;
            const float g = toGain (gainDb + makeup);

            // the channel, delayed when anticipating
            for (int c = 0; c < ch; ++c)
            {
                float x = b.getSample (c, i);
                if (look > 0)
                {
                    auto& d = delay[(size_t) c];
                    const float out = d[(size_t) ((w + d.size() - (size_t) look) % d.size())];
                    d[w % d.size()] = x;
                    x = out;
                }
                const float y = listen ? keyOut[c] : x * (1.0f - mix) + x * g * mix;
                b.setSample (c, i, y);
            }
            if (look > 0) ++w;
        }
        grDb.store (-maxGr);

        // history for the screen: one point per block
        const int pos = histPos.load();
        keyHist[pos] = peakKey;
        grHist[pos] = -maxGr;
        histPos.store ((pos + 1) % histSize);
    }

    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Sidechain", { { "mode", "Modo", "Ducking baja el canal una cantidad fija con cada golpe (el bombeo del house). Compresor comprime según la clave."_u },
                             { "thresh", "Umbral", "Desde qué nivel de la clave empieza a bajar."_u },
                             { "depth", "Profundidad", "Cuánto baja el canal (en Compresor, lo máximo que puede bajar)."_u },
                             { "attack", "Ataque", "Qué tan rápido baja. 1 ms casi al instante."_u },
                             { "release", "Relajación"_u, "Qué tan rápido vuelve. Al tempo: 1/8 a 120 BPM son 250 ms."_u },
                             { "mix", "Mezcla", "Cuánto del efecto se oye. 50 % es un bombeo más disimulado."_u } } },
            { "Clave y forma", { { "source", "Clave", "Qué escucha: el canal conectado (en la tarjeta: Escucha a) o este mismo canal."_u },
                                 { "keyhp", "Quitar graves", "Para que solo los agudos de la clave (charles, voz) disparen." },
                                 { "keylp", "Quitar agudos", "A 150 Hz solo el bombo dispara, aunque la batería traiga todo."_u },
                                 { "listen", "Escuchar", "Oyes la clave filtrada en vez del canal, para ajustar los filtros." },
                                 { "hold", "Retención"_u, "Cuánto se queda abajo antes de volver."_u },
                                 { "curve", "Curva", "0 %: vuelve rápido y luego despacio. 100 %: se queda abajo y sube al final (más bombeo)."_u },
                                 { "ratio", "Ratio", "Solo en Compresor: 4:1 suave, 20:1 casi un limitador." },
                                 { "knee", "Rodilla", "Qué tan suave entra cerca del umbral."_u },
                                 { "look", "Anticipar", "Retrasa este canal 5 ms para que baje justo antes del golpe. Racoon compensa el retraso." },
                                 { "invert", "Al revés"_u, "Este canal solo suena cuando la clave golpea (efectos de compuerta)." },
                                 { "makeup", "Ganancia", "Sube o baja el canal después."_u } }, true },
        };
        s.advancedHeight = 260;
        return s;
    }

    String displayText() const override
    {
        if (usingExternal.load()) return String::fromUTF8 ("Escuchando el canal conectado");
        if (listeningOwn.load()) return String::fromUTF8 ("Escuchando su propia señal");
        return String::fromUTF8 ("Nadie conectado: elige en la tarjeta del canal a quién escucha");
    }

    std::unique_ptr<Component> makeDisplay() override;

    static constexpr int histSize = 240;
    float keyHist[histSize] {}, grHist[histSize] {};
    std::atomic<int> histPos { 0 };
    std::atomic<bool> usingExternal { false }, listeningOwn { false };

private:
    float env = 0.0f, grEnv = 0.0f, keyLevel = 0.0f, releaseFrom = 0.0f, releasePos = 1.0f;
    int holdLeft = 0, maxLook = 1, latency = 0;
    float lastHp = -1.0f, lastLp = -1.0f;
    Biquad hp[2], lp[2];
    std::vector<float> delay[2];
    size_t w = 0;
};

// the screen: the key's level (cyan) against the threshold, and how much the channel goes down (yellow)
class SidechainDisplay : public Component, private Timer
{
public:
    explicit SidechainDisplay (Sidechain& s) : fx (s) { startTimerHz (25); }
    void paint (Graphics& g) override
    {
        using namespace racoon::ui;
        drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (16.0f, 12.0f);
        auto info = r.removeFromTop (20.0f);
        g.setFont (Fonts::label (12.0f));
        g.setColour (fx.usingExternal.load() ? Pro::input : Pro::reduce);
        g.drawText (::racoon::i18n::tr (fx.displayText()), info, Justification::centredLeft);
        g.setColour (Pro::value);
        g.drawText (::racoon::i18n::tr ("BAJA " + String (-fx.grDb.load(), 1) + " dB"), info, Justification::centredRight);
        r.removeFromTop (6.0f);
        drawScreenGrid (g, r, 8, 4);
        // dB scale: 0 at the top, -60 at the bottom
        auto yOf = [&r] (float db) { return r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, -db / 60.0f); };
        const int pos = fx.histPos.load();
        const int N = Sidechain::histSize;
        Path key, gr;
        for (int i = 0; i < N; ++i)
        {
            const int k = (pos + i) % N;
            const float x = r.getX() + r.getWidth() * (float) i / (float) (N - 1);
            const float ky = yOf (jmax (-60.0f, fx.keyHist[k]));
            const float gy = r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, -fx.grHist[k] / 48.0f);
            if (i == 0) { key.startNewSubPath (x, ky); gr.startNewSubPath (x, gy); }
            else { key.lineTo (x, ky); gr.lineTo (x, gy); }
        }
        g.setColour (Pro::input.withAlpha (0.8f));
        g.strokePath (key, PathStrokeType (1.5f));
        g.setColour (Pro::value);
        g.strokePath (gr, PathStrokeType (2.2f));
        // the threshold
        const float ty = yOf (fx.value ("thresh"));
        g.setColour (Pro::text.withAlpha (0.7f));
        g.drawHorizontalLine ((int) ty, r.getX(), r.getRight());
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("UMBRAL " + String (fx.value ("thresh"), 0) + " dB"), Rectangle<float> (r.getX() + 4.0f, ty - 16.0f, 140.0f, 14.0f), Justification::centredLeft);
        g.setColour (Pro::textDim);
        g.drawText (::racoon::i18n::tr ("CLAVE"), Rectangle<float> (r.getRight() - 140.0f, r.getY() + 2.0f, 136.0f, 14.0f), Justification::centredRight);
    }
private:
    Sidechain& fx;
    void timerCallback() override { repaint(); }
};

std::unique_ptr<Component> Sidechain::makeDisplay() { return std::make_unique<SidechainDisplay> (*this); }

void registerSidechain (std::vector<FxDef>& v)
{
    v.push_back ({ "sidechain", "Sidechain", "Dinámica"_u,
                   "Un canal baja cuando otro golpea: el bombeo del house, el bajo que le deja sitio al bombo. Elige en la tarjeta a quién escucha."_u,
                   Kind::effect, [] { return std::make_unique<Sidechain>(); }, { "thresh", "depth", "attack", "release", "keylp", "mix" } });
}
}

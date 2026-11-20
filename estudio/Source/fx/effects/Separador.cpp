// Separador de golpe: splits the sound into the hit (the attack of each note or drum) and the body (what remains:
// the held note, the tail, the room). Each part has its own colour, tilt, width, mix and level; the hit's envelope can
// move the body (or the hit) inside the effect. Without colours, hit + body is the input exactly.
#include "AllEffects.h"
#include "engine/I18n.h"
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

struct LR4
{
    Biquad lp[2], hp[2];
    void set (double sr, double f) { for (int s = 0; s < 2; ++s) { lp[s].set (Biquad::lowpass, sr, f, 0.7071); hp[s].set (Biquad::highpass, sr, f, 0.7071); } }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    void split (float x, int c, float& lo, float& hi) { lo = lp[1].process (lp[0].process (x, c), c); hi = hp[1].process (hp[0].process (x, c), c); }
};

enum Colour { clean, warm, clip, lofi, diffuse, echo };

// one part's chain: colour, tilt, width (the level and the mix are applied by the caller)
struct Part
{
    sdsp::Adaa1<sdsp::curve::Tanh> warmC[2];
    sdsp::Adaa1<sdsp::curve::Cubic15> clipC[2];
    Biquad lowSh, highSh;
    float tiltNow = 1000.0f;
    // lo-fi
    float held[2] {}; int holdCount = 0;
    // diffusion: four allpasses
    std::vector<float> ap[2][4]; int apPos[2][4] {}; int apLen[4] {};
    // echo
    std::vector<float> dl[2]; int dlPos = 0; float dlLp[2] {};
    float half[2] {};   // the half-sample delay for the part without ADAA when the other one has it

    void prepare (double sr)
    {
        static const double ms[4] { 3.1, 5.3, 11.7, 23.9 };
        for (int k = 0; k < 4; ++k) { apLen[k] = jmax (1, (int) (ms[k] * 0.001 * sr)); for (int c = 0; c < 2; ++c) ap[c][k].assign ((size_t) apLen[k], 0.0f); }
        for (int c = 0; c < 2; ++c) dl[c].assign ((size_t) (sr * 2.1) + 4, 0.0f);
        reset();
        tiltNow = 1000.0f;
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            warmC[c].reset(); clipC[c].reset(); held[c] = 0.0f; dlLp[c] = 0.0f; half[c] = 0.0f;
            for (int k = 0; k < 4; ++k) { std::fill (ap[c][k].begin(), ap[c][k].end(), 0.0f); apPos[c][k] = 0; }
            std::fill (dl[c].begin(), dl[c].end(), 0.0f);
        }
        lowSh.reset(); highSh.reset(); holdCount = 0; dlPos = 0;
    }
    void setTilt (double sr, float db)
    {
        if (std::abs (db - tiltNow) < 0.01f) return;
        tiltNow = db;
        lowSh.set (Biquad::lowshelf, sr, 1000.0, 0.7071, -db * 0.5);
        highSh.set (Biquad::highshelf, sr, 1000.0, 0.7071, db * 0.5);
    }
    // x: the stereo pair (in place)
    void colour (int type, float amt, float* x, int echoLen)
    {
        switch (type)
        {
            case warm: case clip:
            {
                const float drive = 1.0f + 7.0f * amt;
                const float comp = 1.0f / std::tanh (drive);
                for (int c = 0; c < 2; ++c) x[c] = (type == warm ? warmC[c].process (x[c] * drive) : clipC[c].process (x[c] * drive)) * comp;
                break;
            }
            case lofi:
            {
                const int hold = 1 + (int) std::round (amt * 7.0f);
                const float steps = std::pow (2.0f, 16.0f - 12.0f * amt - 1.0f);
                if (++holdCount >= hold) { holdCount = 0; for (int c = 0; c < 2; ++c) held[c] = std::round (x[c] * steps) / steps; }
                for (int c = 0; c < 2; ++c) x[c] = held[c];
                break;
            }
            case diffuse:
            {
                const float g = 0.25f + 0.5f * amt;
                for (int c = 0; c < 2; ++c)
                {
                    float v = x[c];
                    for (int k = 0; k < 4; ++k)
                    {
                        auto& buf = ap[c][k]; int& p = apPos[c][k];
                        const float d = buf[(size_t) p];
                        const float y = -g * v + d;
                        buf[(size_t) p] = v + g * y;
                        p = (p + 1) % apLen[k];
                        v = y;
                    }
                    x[c] = v;
                }
                break;
            }
            case echo:
            {
                const int len = (int) dl[0].size();
                const int rd = (dlPos - jlimit (1, len - 1, echoLen) + len) % len;
                for (int c = 0; c < 2; ++c)
                {
                    const float e = dl[c][(size_t) rd];
                    dlLp[c] += 0.55f * (e - dlLp[c]);           // about 6 kHz in the loop
                    dl[c][(size_t) dlPos] = x[c] + 0.7f * amt * dlLp[c];
                    x[c] += e * amt;
                }
                dlPos = (dlPos + 1) % len;
                break;
            }
            default: break;
        }
    }
    void tiltAndWidth (float* x, float width)
    {
        if (std::abs (tiltNow) > 0.01f) for (int c = 0; c < 2; ++c) x[c] = highSh.process (lowSh.process (x[c], c), c);
        if (std::abs (width - 1.0f) > 1.0e-4f)
        {
            const float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]) * width;
            x[0] = m + s; x[1] = m - s;
        }
    }
    void halfSample (float* x) { for (int c = 0; c < 2; ++c) { const float y = 0.5f * (x[c] + half[c]); half[c] = x[c]; x[c] = y; } }
};

const StringArray& colourNames() { static StringArray a { "Limpio", "Calor", "Recorte", "Lo-fi", "Difusión"_u, "Eco" }; return a; }
const StringArray& destNames() { static StringArray a { "Nada", "Cuerpo: nivel", "Cuerpo: inclinación"_u, "Cuerpo: cantidad", "Golpe: cantidad", "Cuerpo: ancho" }; return a; }

class TransientSplitter : public FxBase
{
public:
    TransientSplitter() : FxBase ("Separador de golpe", "Dinámica"_u, layout(), 0.0, false, false) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::pct (l, "sens", "Sensibilidad", 0.0f, 100.0f, 50.0f);
        P::choice (l, "mode", "Modo de detección"_u, { "Suave", "Equilibrado", "Denso" }, 1);
        P::ms (l, "hold", "Sostener", 0.0f, 50.0f, 5.0f);
        P::ms (l, "release", "Soltar", 5.0f, 500.0f, 60.0f, 60.0f);
        P::toggle (l, "look", "Anticipación (5 ms)"_u, false);
        P::hz (l, "detlo", "Detectar desde", 20.0f, 500.0f, 20.0f);
        P::hz (l, "split", "Graves al cuerpo hasta", 20.0f, 300.0f, 20.0f);
        P::pct (l, "link", "Enlace estéreo"_u, 0.0f, 100.0f, 100.0f);
        for (const char* part : { "t", "s" })
        {
            const String id (part), n = id == "t" ? "Golpe: " : "Cuerpo: ";
            P::db (l, id + "_level", n + "nivel", -60.0f, 12.0f, 0.0f);
            P::choice (l, id + "_color", n + "color", colourNames(), 0);
            P::pct (l, id + "_amt", n + "cantidad", 0.0f, 100.0f, 30.0f);
            P::db (l, id + "_tilt", n + "inclinación"_u, -6.0f, 6.0f, 0.0f);
            P::pct (l, id + "_width", n + "ancho", 0.0f, 200.0f, 100.0f);
            P::pct (l, id + "_mix", n + "mezcla", 0.0f, 100.0f, 100.0f);
        }
        P::ms (l, "env_smooth", "Envolvente: suavizado", 1.0f, 200.0f, 10.0f);
        P::choice (l, "d1_dest", "Envolvente: destino 1", destNames(), 0);
        P::flt (l, "d1_amt", "Envolvente: cantidad 1", P::range (-100.0f, 100.0f), 0.0f, " %", 0);
        P::choice (l, "d2_dest", "Envolvente: destino 2", destNames(), 0);
        P::flt (l, "d2_amt", "Envolvente: cantidad 2", P::range (-100.0f, 100.0f), 0.0f, " %", 0);
        P::choice (l, "listen", "Escuchar", { "Todo", "Solo golpe", "Solo cuerpo" }, 0);
        P::toggle (l, "clip", "Recorte de salida", false);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
        return l;
    }

    void prepare (double rate, int) override
    {
        look = (int) std::round (0.005 * rate);
        for (int c = 0; c < 2; ++c) dly[c].assign ((size_t) look + 2, 0.0f);
        hit.prepare (rate); body.prepare (rate);
        setLatencySamples (value ("look") > 0.5f ? look : 0);
        lookOn = value ("look") > 0.5f;
        reset();
    }
    void reset() override
    {
        xo.reset(); detHp[0].reset(); detHp[1].reset();
        for (auto* f : { &fast[0], &fast[1], &slow[0], &slow[1] }) f->env = 0.0f;
        for (int c = 0; c < 2; ++c) { mask[c] = 0.0f; holdV[c] = 0.0f; holdLeft[c] = 0; std::fill (dly[c].begin(), dly[c].end(), 0.0f); knee[c].reset(); }
        dPos = 0; envS = 0.0f;
        hit.reset(); body.reset();
        hitAcc = bodyAcc = inAcc = 0.0; accN = 0;
    }

    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const bool wantLook = value ("look") > 0.5f;
        if (wantLook != lookOn) { lookOn = wantLook; setLatencySamples (lookOn ? look : 0); }
        const int L = lookOn ? look : 0;
        const int mode = (int) value ("mode");
        static const float slowAtk[3] { 12.0f, 6.0f, 2.5f };
        for (int c = 0; c < 2; ++c) { fast[c].set (sr, 0.1f, 30.0f); slow[c].set (sr, slowAtk[jlimit (0, 2, mode)], 30.0f); }
        const float thr = 9.0f - 8.5f * value ("sens") * 0.01f;
        const int holdN = (int) (value ("hold") * 0.001 * sr);
        // Soltar is the time the mask takes to fall 60 dB
        const float relC = std::exp (-6.9f / (float) (jmax (1.0, value ("release") * 0.001 * sr)));
        // the envelope follows the hit at once and lets go over Soltar or its smoothing, whichever is longer
        const float envRelC = std::exp (-1.0f / (float) (jmax (1.0, jmax ((double) value ("release") * 1.5, (double) value ("env_smooth")) * 0.001 * sr)));
        const float upC = std::exp (-1.0f / (float) (0.0003 * sr));
        const float envC = std::exp (-1.0f / (float) (jmax (1.0, value ("env_smooth") * 0.001 * sr)));
        const float detLo = value ("detlo");
        const bool detFilter = detLo > 21.0f;
        if (detFilter) for (auto& h : detHp) h.set (Biquad::highpass, sr, detLo, 0.7071);
        const float splitHz = value ("split");
        const bool split = splitHz > 21.0f;
        if (split) xo.set (sr, splitHz);
        const float link = value ("link") * 0.01f;
        const int tCol = (int) value ("t_color"), sCol = (int) value ("s_color");
        const float tAmt0 = value ("t_amt") * 0.01f, sAmt0 = value ("s_amt") * 0.01f;
        const float tTilt = value ("t_tilt"), sTilt0 = value ("s_tilt");
        const float tWidth = value ("t_width") * 0.01f, sWidth0 = value ("s_width") * 0.01f;
        const float tLev = value ("t_level") <= -59.9f ? 0.0f : toGain (value ("t_level"));
        const float sLev0 = value ("s_level") <= -59.9f ? 0.0f : toGain (value ("s_level"));
        const float tMix = value ("t_mix") * 0.01f, sMix = value ("s_mix") * 0.01f;
        const int d1 = (int) value ("d1_dest"), d2 = (int) value ("d2_dest");
        const float a1 = value ("d1_amt") * 0.01f, a2 = value ("d2_amt") * 0.01f;
        const int listen = (int) value ("listen");
        const bool clipOut = value ("clip") > 0.5f;
        const float mix = value ("mix") * 0.01f, out = toGain (value ("output"));
        const bool adaaT = tCol == warm || tCol == clip, adaaS = sCol == warm || sCol == clip;
        const int echoLen = (int) (syncSeconds (3, bpm()) * sr);     // 1/16 at the song's tempo
        hit.setTilt (sr, tTilt);
        float maxMask = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // ---- the mask, from the audio as it comes in
            float x[2], det[2];
            for (int c = 0; c < 2; ++c) x[c] = b.getSample (jmin (c, ch - 1), i);
            for (int c = 0; c < 2; ++c) det[c] = std::abs (detFilter ? detHp[c].process (x[c], c) : x[c]);
            const float both = jmax (det[0], det[1]);
            for (int c = 0; c < 2; ++c)
            {
                const float d = link * both + (1.0f - link) * det[c];
                const float f = fast[c].process (d), s = slow[c].process (d);
                const float jump = jmax (0.0f, toDb (f) - toDb (s));
                const float raw = d < 1.0e-6f ? 0.0f : jlimit (0.0f, 1.0f, (jump - thr) / 4.0f);
                if (raw >= holdV[c]) { holdV[c] = raw; holdLeft[c] = holdN; }
                else if (holdLeft[c] > 0) --holdLeft[c];
                else holdV[c] = raw;
                // up fast (0.3 ms, no click), down with Soltar
                mask[c] = holdV[c] > mask[c] ? holdV[c] + (mask[c] - holdV[c]) * upC : holdV[c] + (mask[c] - holdV[c]) * relC;
            }
            maxMask = jmax (maxMask, mask[0], mask[1]);
            const float m = 0.5f * (mask[0] + mask[1]);
            envS = m > envS ? m + (envS - m) * envC : m + (envS - m) * envRelC;

            // ---- the audio, late by the look-ahead
            float dry[2];
            for (int c = 0; c < 2; ++c)
            {
                if (L > 0) { auto& d = dly[c]; dry[c] = d[(size_t) dPos]; d[(size_t) dPos] = x[c]; }
                else dry[c] = x[c];
            }
            if (L > 0) dPos = (dPos + 1) % L;

            // the envelope moves the parts
            float sLev = sLev0, sTilt = sTilt0, sAmt = sAmt0, tAmt = tAmt0, sWidth = sWidth0;
            auto apply = [&] (int dest, float amt)
            {
                switch (dest)
                {
                    case 1: sLev *= toGain (24.0f * amt * envS); break;
                    case 2: sTilt += 6.0f * amt * envS; break;
                    case 3: sAmt = jlimit (0.0f, 1.0f, sAmt + amt * envS); break;
                    case 4: tAmt = jlimit (0.0f, 1.0f, tAmt + amt * envS); break;
                    case 5: sWidth = jlimit (0.0f, 2.0f, sWidth + amt * envS); break;
                    default: break;
                }
            };
            if (d1 > 0) apply (d1, a1);
            if (d2 > 0) apply (d2, a2);
            if ((i & 15) == 0) body.setTilt (sr, jlimit (-12.0f, 12.0f, sTilt));

            float th[2], bd[2];
            for (int c = 0; c < 2; ++c)
            {
                float lo = 0.0f, hi = dry[c];
                if (split) xo.split (dry[c], c, lo, hi);
                const float mc = lookOn ? (L > 0 ? mask[c] : mask[c]) : mask[c];
                th[c] = hi * mc;
                bd[c] = lo + hi * (1.0f - mc);
            }
            // each part through its chain; the clean copy is kept for the part's mix
            float tp[2] { th[0], th[1] }, sp[2] { bd[0], bd[1] };
            hit.colour (tCol, tAmt, tp, echoLen); hit.tiltAndWidth (tp, tWidth);
            body.colour (sCol, sAmt, sp, echoLen); body.tiltAndWidth (sp, sWidth);
            float tc[2] { th[0], th[1] }, sc[2] { bd[0], bd[1] };
            // ADAA is half a sample late: the other part (and the clean copies) wait the same half sample
            if (adaaT != adaaS)
            {
                if (! adaaT) hit.halfSample (tp);
                if (! adaaS) body.halfSample (sp);
            }
            if (adaaT || adaaS) { halfT.process (tc); halfS.process (sc); }
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                const float tPart = (tc[c] + (tp[c] - tc[c]) * tMix) * tLev;
                const float sPart = (sc[c] + (sp[c] - sc[c]) * sMix) * sLev;
                y[c] = listen == 1 ? tPart : listen == 2 ? sPart : tPart + sPart;
                if (clipOut) y[c] = knee[c].process (y[c]);
                // the dry for the general mix waits like the parts
                const float dAligned = (adaaT || adaaS || clipOut) ? halfD.at (c, dry[c]) : dry[c];
                y[c] = (dAligned + (y[c] - dAligned) * mix) * out;
                hitAcc += (double) tPart * tPart; bodyAcc += (double) sPart * sPart; inAcc += (double) dry[c] * dry[c];
            }
            halfD.step();
            ++accN;
            for (int c = 0; c < ch; ++c) b.setSample (c, i, y[c]);
        }
        maskShown.store (maxMask);
        if (accN > (int) (0.3 * sr))
        {
            hitDb.store (10.0f * std::log10 ((float) (hitAcc / jmax (1.0e-12, inAcc)) + 1.0e-12f));
            bodyDb.store (10.0f * std::log10 ((float) (bodyAcc / jmax (1.0e-12, inAcc)) + 1.0e-12f));
            hitAcc = bodyAcc = inAcc = 0.0; accN = 0;
        }
        grDb.store (-maxMask * 12.0f);
    }

    String displayText() const override
    {
        auto f = [] (float v) { return (v >= 0.0f ? "+" : "") + String (v, 1) + " dB"; };
        return ::racoon::i18n::tr ("golpe " + f (hitDb.load()) + " · cuerpo "_u + f (bodyDb.load()));
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Detección"_u, { { "sens", "Sensibilidad", "Más alta: hasta los golpes suaves cuentan como golpe."_u },
                               { "mode", "Modo", "Suave para voces y pads, Denso para baterías rápidas."_u },
                               { "hold", "Sostener", "Cuánto se queda abierto el golpe antes de soltar."_u },
                               { "release", "Soltar", "Qué tan rápido el golpe le regresa el sonido al cuerpo."_u },
                               { "look", "Anticipación"_u, "Retrasa el sonido 5 ms para que el golpe entre entero. Racoon compensa el retraso."_u },
                               { "detlo", "Detectar desde", "El detector solo escucha arriba de esta frecuencia: un bajo no dispara el golpe del charles."_u } } },
            { "Golpe", { { "t_level", "Nivel", "Volumen del golpe."_u }, { "t_color", "Color", "Limpio, Calor, Recorte, Lo-fi, Difusión o Eco, solo para el golpe."_u },
                         { "t_amt", "Cantidad", "Cuánto color."_u }, { "t_tilt", "Inclinación"_u, "Más brillo o más cuerpo en el golpe."_u },
                         { "t_width", "Ancho", "Golpe en 0 % y cuerpo abierto: el golpe al centro y la sala a los lados."_u }, { "t_mix", "Mezcla", "El golpe con color contra el golpe limpio."_u } } },
            { "Cuerpo", { { "s_level", "Nivel", "Volumen del cuerpo: la nota sostenida, la cola, la sala."_u }, { "s_color", "Color", "El color solo para el cuerpo."_u },
                          { "s_amt", "Cantidad", "Cuánto color."_u }, { "s_tilt", "Inclinación"_u, "Más brillo o más grave en el cuerpo."_u },
                          { "s_width", "Ancho", "Qué tan abierto queda el cuerpo."_u }, { "s_mix", "Mezcla", "El cuerpo con color contra el cuerpo limpio."_u } } },
            { "Envolvente", { { "env_smooth", "Suavizado", "Qué tan suave sigue la envolvente al golpe."_u },
                              { "d1_dest", "Destino 1", "Qué mueve el golpe dentro del efecto."_u }, { "d1_amt", "Cantidad 1", "Cuánto y hacia dónde. -100 en Cuerpo: nivel baja la cola 24 dB en cada golpe."_u },
                              { "d2_dest", "Destino 2", "Otro destino."_u }, { "d2_amt", "Cantidad 2", "Cuánto y hacia dónde."_u } }, true },
            { "Salida", { { "split", "Graves", "Debajo de esta frecuencia todo va al cuerpo. En 20 Hz está apagado."_u },
                          { "link", "Enlace", "En 100 % los dos lados comparten el golpe."_u },
                          { "listen", "Escuchar", "Todo, solo el golpe o solo el cuerpo."_u },
                          { "clip", "Recorte", "Redondea los picos para que no pasen de 0 dB."_u },
                          { "mix", "Mezcla", "Con la señal original."_u }, { "output", "Salida", "Volumen final."_u } }, true },
        };
        return s;
    }
    std::vector<FactoryPreset> factoryPresets() const override
    {
        return { { "Inicial", "Básicos"_u, {} },
                 { "Bombo con punta", "Batería"_u, { { "sens", 55 }, { "mode", 2 }, { "t_level", 3 }, { "s_level", -1 } } },
                 { "Caja seca", "Batería"_u, { { "t_level", 1 }, { "s_level", -6 }, { "release", 40 } } },
                 { "Caja con sala abierta", "Batería"_u, { { "s_level", 3 }, { "s_width", 160 }, { "t_width", 60 }, { "s_color", diffuse }, { "s_amt", 25 } } },
                 { "Charles más suaves"_u, "Batería"_u, { { "t_level", -4 }, { "s_tilt", 1.5f }, { "mode", 2 } } },
                 { "Bajo: clic limpio, cuerpo caliente", "Bajo", { { "t_level", 1 }, { "s_color", warm }, { "s_amt", 40 }, { "s_width", 0 }, { "t_width", 0 } } },
                 { "Voz: consonantes suaves", "Voz", { { "sens", 40 }, { "t_level", -3 }, { "t_tilt", -2 }, { "s_color", warm }, { "s_amt", 15 } } },
                 { "Guitarra: púa al frente"_u, "Instrumentos", { { "t_level", 2 }, { "t_tilt", 2 }, { "s_width", 130 } } },
                 { "Golpe con eco", "Creativos", { { "t_color", echo }, { "t_amt", 45 }, { "t_mix", 50 } } },
                 { "Cuerpo lo-fi", "Creativos", { { "s_color", lofi }, { "s_amt", 60 }, { "s_mix", 70 } } },
                 { "El golpe abre el cuerpo", "Creativos", { { "s_tilt", -4 }, { "d1_dest", 2 }, { "d1_amt", 80 }, { "env_smooth", 40 } } },
                 { "Cola que se aparta", "Creativos", { { "d1_dest", 1 }, { "d1_amt", -60 }, { "release", 120 } } } };
    }

    std::atomic<float> maskShown { 0.0f }, hitDb { 0.0f }, bodyDb { 0.0f };

private:
    // half a sample of delay for a stereo pair, used to keep the clean copies with the coloured ones
    struct Half { float z[2] {}; void process (float* x) { for (int c = 0; c < 2; ++c) { const float y = 0.5f * (x[c] + z[c]); z[c] = x[c]; x[c] = y; } } };
    struct HalfD { float z[2] {}, nz[2] {}; float at (int c, float x) { nz[c] = x; return 0.5f * (x + z[c]); } void step() { z[0] = nz[0]; z[1] = nz[1]; } };
    Half halfT, halfS;
    HalfD halfD;
    LR4 xo;
    Biquad detHp[2];
    Follower fast[2], slow[2];
    float mask[2] {}, holdV[2] {}; int holdLeft[2] {};
    float envS = 0.0f;
    std::vector<float> dly[2]; int dPos = 0, look = 240; bool lookOn = false;
    Part hit, body;
    sdsp::Adaa1<sdsp::curve::Knee> knee[2];
    double hitAcc = 0.0, bodyAcc = 0.0, inAcc = 0.0; int accN = 0;
};
}

// the largest mask value of the last block (0 = all body, 1 = all hit); for the tests
float separatorMask (RacoonProcessor& p)
{
    auto* s = dynamic_cast<TransientSplitter*> (&p);
    return s != nullptr ? s->maskShown.load() : 0.0f;
}

void registerSeparador (std::vector<FxDef>& v)
{
    v.push_back ({ "separador-golpe", "Separador de golpe", "Dinámica"_u,
                   "Parte el sonido en golpe y cuerpo: nivel, color y ancho para cada uno, y la envolvente del golpe para mover otras perillas."_u,
                   Kind::effect, [] { return std::make_unique<TransientSplitter>(); }, { "t_level", "s_level", "t_tilt", "s_tilt", "t_color", "s_color" } });
}
}

#include "LibraryKits.h"
#include "Biblioteca.h"

namespace be
{
using racoon::biblio::Catalogo;

int roleForCategory (const juce::String& c)
{
    static const std::map<juce::String, Role> m {
        { "bombo", Role::Kick }, { "bajo_808", Role::Kick }, { "caja", Role::Snare }, { "palmas", Role::Clap }, { "chasquido", Role::Clap },
        { "aro", Role::Rim }, { "clave", Role::Rim }, { "bloque", Role::Rim }, { "hihat_cerrado", Role::ClosedHat }, { "hihat_pedal", Role::ClosedHat },
        { "hihat_abierto", Role::OpenHat }, { "ride", Role::Ride }, { "crash", Role::Crash }, { "platillo_fx", Role::Crash },
        { "tom_grave", Role::TomLow }, { "tom_medio", Role::TomMid }, { "tom_agudo", Role::TomHigh }, { "shaker", Role::Shaker },
        { "pandereta", Role::Shaker }, { "cabasa", Role::Shaker }, { "guiro", Role::Shaker }, { "cascabeles", Role::Shaker },
        { "conga", Role::Conga }, { "djembe", Role::Conga }, { "tambor", Role::Conga }, { "cajon", Role::Conga }, { "bongo", Role::Bongo },
        { "darbuka", Role::Bongo }, { "tabla", Role::Bongo }, { "timbal", Role::Bongo }, { "cencerro", Role::Cowbell }, { "agogo", Role::Cowbell },
        { "triangulo", Role::Metal }, { "campana", Role::Metal }, { "metal", Role::Metal } };
    auto it = m.find (c);
    return (int) (it != m.end() ? it->second : Role::Custom);
}

namespace
{
// la pista suena solo con su muestra (sin capas sintéticas), con una envolvente a la medida de su duración
void voiceParams (LibraryVoice& v, int multi, float durSeconds, float velSens)
{
    const float dur = juce::jlimit (0.05f, 12.0f, durSeconds);
    auto& p = v.params;
    p.push_back ({ T_ROLE, (float) v.role });
    p.push_back ({ T_CIRCUIT_ON, 0.0f });
    p.push_back ({ T_A_ON, 0.0f });
    p.push_back ({ T_B_ON, 0.0f });
    p.push_back ({ T_D_ON, 0.0f });
    p.push_back ({ T_C_ON, 1.0f });
    p.push_back ({ T_C_LEVEL, 1.0f });
    p.push_back ({ T_C_SAMPLE, (float) (factorySampleNames().size() - 1) });
    p.push_back ({ T_C_START, 0.0f });
    p.push_back ({ T_C_END, 1.0f });
    p.push_back ({ T_C_PITCH, 0.0f });
    p.push_back ({ T_C_REVERSE, 0.0f });
    p.push_back ({ T_C_SLICEMODE, 0.0f });
    p.push_back ({ T_C_LOOP, 0.0f });
    p.push_back ({ T_C_FADEIN, 0.0f });
    p.push_back ({ T_C_MULTI, (float) multi });
    // envolvente que deja sonar la muestra completa (la cola ya viene recortada en la biblioteca)
    p.push_back ({ T_ENV_MODE, (float) EnvMode::AHD });
    p.push_back ({ T_ENV_ATTACK, 0.0f });
    p.push_back ({ T_ENV_HOLD, juce::jmin (1.0f, dur * 0.7f) });
    p.push_back ({ T_ENV_DECAY, juce::jlimit (0.12f, 8.0f, dur * 1.3f) });
    p.push_back ({ T_ENV_CURVE, 0.5f });
    p.push_back ({ T_PENV_AMT, 0.0f });
    p.push_back ({ T_FENV_AMT, 0.0f });
    // las capas de velocidad ya cambian el volumen: la sensibilidad baja para no castigar dos veces los golpes suaves
    p.push_back ({ T_VELSENS, velSens });
}
}

LibraryVoice libraryVoice (const juce::String& grupo)
{
    if (grupo.startsWith ("receta:"))
    {
        const auto rest = grupo.fromFirstOccurrenceOf ("receta:", false, false);
        return recetaVoice (rest.upToLastOccurrenceOf ("/", false, false), rest.fromLastOccurrenceOf ("/", false, false).getIntValue());
    }
    LibraryVoice v;
    const auto& cat = Catalogo::get();
    const auto* g = cat.grupo (grupo);
    if (g == nullptr || g->muestras.empty()) return v;
    v.ok = true;
    v.grupo = grupo;
    v.name = g->nombre;
    v.role = roleForCategory (g->cat);
    int multi = (int) MultiMode::Single;
    if (g->capas > 1)
    {
        // una capa por ranura (la primera alternancia de cada capa); si hay más de cuatro, se reparten
        std::vector<const racoon::biblio::Muestra*> capas;
        for (int idx : g->muestras)
            if (cat.muestras[(size_t) idx].rr == 1) capas.push_back (&cat.muestras[(size_t) idx]);
        const int n = (int) capas.size();
        for (int k = 0; k < juce::jmin (kNumSlots, n); ++k)
        {
            const int pick = n <= kNumSlots ? k : juce::roundToInt ((float) k * (float) (n - 1) / (float) (kNumSlots - 1));
            v.slotIds.add (capas[(size_t) pick]->id);
        }
        multi = (int) MultiMode::Velocity;
    }
    else
    {
        for (int idx : g->muestras)
            if (v.slotIds.size() < kNumSlots) v.slotIds.add (cat.muestras[(size_t) idx].id);
        multi = v.slotIds.size() > 1 ? (int) MultiMode::RoundRobin : (int) MultiMode::Single;
    }
    voiceParams (v, multi, g->dur, g->capas > 1 ? 0.3f : 0.6f);
    return v;
}

LibraryVoice recetaVoice (const juce::String& kitId, int pad)
{
    LibraryVoice v;
    const auto* kit = Catalogo::get().receta (kitId);
    const auto* p = kit != nullptr ? kit->pad (pad) : nullptr;
    if (p == nullptr) return v;
    static const std::map<juce::String, juce::String> catDeRol {
        { "Kick", "bombo" }, { "Snare", "caja" }, { "Clap", "palmas" }, { "Rim", "aro" }, { "Closed Hat", "hihat_cerrado" },
        { "Open Hat", "hihat_abierto" }, { "Ride", "ride" }, { "Crash", "crash" }, { "Shaker", "shaker" }, { "Conga", "conga" },
        { "Bongo", "bongo" }, { "Tom High", "tom_agudo" }, { "Tom Mid", "tom_medio" }, { "Tom Low", "tom_grave" },
        { "Cowbell", "cencerro" }, { "Metal Perc", "metal" } };
    auto it = catDeRol.find (p->rol);
    v.ok = true;
    v.grupo = "receta:" + kit->id + "/" + juce::String (pad);
    v.name = p->nombre;
    v.role = roleForCategory (it != catDeRol.end() ? it->second : juce::String());
    v.slotIds.add (v.grupo);
    // un solo golpe ya procesado: la velocidad mueve el volumen
    voiceParams (v, (int) MultiMode::Single, p->cola, 0.6f);
    return v;
}

// ---------------------------------------------------------------- Kits Racoon: el taller que arma cada golpe
namespace
{
struct Bq                             // un biquad RBJ por canal, forma directa transpuesta
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    enum Tipo { pasaAltos, pasaBajos, estanteBajo, campana, estanteAlto };
    void set (Tipo t, double sr, double f, double q, double db = 0.0)
    {
        f = juce::jlimit (10.0, sr * 0.45, f);
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), sn = std::sin (w);
        const double al = sn / (2.0 * q);
        double B0, B1, B2, A0, A1, A2;
        switch (t)
        {
            case pasaAltos:  B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case pasaBajos:  B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case campana:    B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            case estanteBajo:
            {
                const double sq = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq);
                A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; break;
            }
            default:
            {
                const double sq = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
                A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void run (std::vector<float>& x, int ch)
    {
        for (auto& v : x)
        {
            const double y = b0 * v + z1[ch];
            z1[ch] = b1 * v - a1 * y + z2[ch];
            z2[ch] = b2 * v - a2 * y;
            v = (float) y;
        }
    }
};

constexpr double kTallerSr = 48000.0;

// una capa: leída, desde su primer transitorio, afinada y llevada a 48 kHz (interpolación cúbica de Hermite)
bool capaAlineada (const racoon::biblio::Receta::Capa& c, std::vector<float>& l, std::vector<float>& r)
{
    const auto& cat = Catalogo::get();
    const auto* m = cat.muestra (c.m);
    if (m == nullptr) return false;
    juce::AudioBuffer<float> buf;
    double rate = 48000.0;
    if (! Catalogo::leer (cat.archivo (m->archivo), buf, rate, 8.0) || buf.getNumSamples() < 16) return false;
    const int n = buf.getNumSamples(), chs = juce::jmin (2, buf.getNumChannels());
    float peak = 0.0f;
    for (int ch = 0; ch < chs; ++ch) peak = juce::jmax (peak, buf.getMagnitude (ch, 0, n));
    if (peak <= 1.0e-6f) return false;
    int onset = 0;
    for (; onset < n; ++onset)
    {
        float v = 0.0f;
        for (int ch = 0; ch < chs; ++ch) v = juce::jmax (v, std::abs (buf.getSample (ch, onset)));
        if (v > 0.1f * peak) break;
    }
    onset = juce::jmax (0, onset - (int) (rate * 0.0005));         // medio milisegundo antes del golpe
    const double step = rate / kTallerSr * std::pow (2.0, c.st / 12.0);
    const double maxOut = c.largo > 0.0f ? c.largo * kTallerSr : 8.0 * kTallerSr;
    const int out = (int) juce::jmin (maxOut, (n - onset - 2) / step);
    if (out < 8) return false;
    l.assign ((size_t) out, 0.0f);
    r.assign ((size_t) out, 0.0f);
    auto at = [&] (int ch, int i) { return buf.getSample (juce::jmin (ch, chs - 1), juce::jlimit (0, n - 1, i)); };
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& dst = ch == 0 ? l : r;
        for (int i = 0; i < out; ++i)
        {
            const double pos = onset + i * step;
            const int k = (int) pos;
            const float t = (float) (pos - k);
            const float y0 = at (ch, k - 1), y1 = at (ch, k), y2 = at (ch, k + 1), y3 = at (ch, k + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            dst[(size_t) i] = ((c3 * t + c2) * t + c1) * t + y1;
        }
    }
    // la capa recortada (el clic, una caja corta debajo de la palma) termina con un fundido de 5 ms
    if (c.largo > 0.0f)
    {
        const int f = juce::jmin (out, (int) (0.005 * kTallerSr));
        for (int i = 0; i < f; ++i)
        {
            const float g = (float) i / (float) f;
            l[(size_t) (out - 1 - i)] *= g; r[(size_t) (out - 1 - i)] *= g;
        }
    }
    // los filtros de la capa: el sub solo abajo de su corte, el clic solo arriba (24 dB por octava)
    for (int pass = 0; pass < 2; ++pass)
    {
        if (c.lp > 0.0f) { Bq b; b.set (Bq::pasaBajos, kTallerSr, c.lp, 0.7071); b.run (l, 0); b.run (r, 1); }
        if (c.hp > 0.0f) { Bq b; b.set (Bq::pasaAltos, kTallerSr, c.hp, 0.7071); b.run (l, 0); b.run (r, 1); }
    }
    const float g = juce::Decibels::decibelsToGain (c.db);
    for (auto& v : l) v *= g;
    for (auto& v : r) v *= g;
    return true;
}

float rmsInicio (const std::vector<float>& l, const std::vector<float>& r)
{
    const int n = juce::jmin ((int) l.size(), (int) (0.15 * kTallerSr));
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += 0.5 * ((double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i]);
    return n > 0 ? (float) std::sqrt (acc / n) : 0.0f;
}

std::map<juce::String, std::weak_ptr<const SampleData>>& tallerCache()
{
    static std::map<juce::String, std::weak_ptr<const SampleData>> c;
    return c;
}
juce::CriticalSection tallerLock;
} // namespace

SamplePtr renderRecetaPad (const racoon::biblio::Receta& kit, const racoon::biblio::Receta::Pad& p)
{
    std::vector<float> L, R;
    for (size_t k = 0; k < p.capas.size(); ++k)
    {
        std::vector<float> l, r;
        if (! capaAlineada (p.capas[k], l, r)) continue;
        if (L.empty()) { L = std::move (l); R = std::move (r); continue; }
        // en fase: si la capa nueva va al revés de lo que ya suena en los primeros 30 ms, se invierte
        const int w = juce::jmin ((int) L.size(), (int) l.size(), (int) (0.03 * kTallerSr));
        double dot = 0.0;
        for (int i = 0; i < w; ++i) dot += (double) (L[(size_t) i] + R[(size_t) i]) * (l[(size_t) i] + r[(size_t) i]);
        const float sign = dot < 0.0 ? -1.0f : 1.0f;
        if (l.size() > L.size()) { L.resize (l.size(), 0.0f); R.resize (l.size(), 0.0f); }
        for (size_t i = 0; i < l.size(); ++i) { L[i] += sign * l[i]; R[i] += sign * r[i]; }
    }
    if (L.empty()) return {};
    // la cola: largo del pad y fundido en su último 30 %
    const int len = juce::jlimit (16, (int) L.size(), (int) (juce::jmax (0.05f, p.cola) * kTallerSr));
    L.resize ((size_t) len); R.resize ((size_t) len);
    const int fade = juce::jmax (8, (int) (len * 0.3f));
    for (int i = 0; i < fade; ++i)
    {
        const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fade);
        L[(size_t) (len - 1 - i)] *= g; R[(size_t) (len - 1 - i)] *= g;
    }
    const int fin = juce::jmin (len, 48);                          // 1 ms de entrada: sin clic de arranque
    for (int i = 0; i < fin; ++i) { const float g = (float) i / (float) fin; L[(size_t) i] *= g; R[(size_t) i] *= g; }
    // EQ del carácter: graves a 100 Hz, presencia a 3.5 kHz, agudos desde 8 kHz, y el paso bajos de los kits oscuros
    auto eq = [&] (Bq::Tipo t, double f, double q, double db) { if (std::abs (db) < 0.05 && t != Bq::pasaBajos) return; Bq b; b.set (t, kTallerSr, f, q, db); b.run (L, 0); b.run (R, 1); };
    eq (Bq::estanteBajo, 100.0, 0.7071, p.ls);
    eq (Bq::campana, 3500.0, 0.9, p.pk);
    eq (Bq::estanteAlto, 8000.0, 0.7071, p.hs);
    if (p.lp > 0.0f) eq (Bq::pasaBajos, p.lp, 0.7071, 0.0);
    // saturación: tanh con la entrada al nivel del pad, mezclada según la receta
    {
        const float pre = juce::Decibels::decibelsToGain (p.nivel + 6.0f) / juce::jmax (1.0e-6f, rmsInicio (L, R));
        const float drive = 1.0f + 5.0f * juce::jlimit (0.0f, 1.0f, p.sat), mix = juce::jlimit (0.0f, 1.0f, p.sat * 2.0f);
        for (auto* v : { &L, &R })
            for (auto& x : *v)
            {
                const float in = x * pre;
                x = ((1.0f - mix) * in + mix * std::tanh (drive * in) / std::tanh (drive)) / pre;
            }
    }
    // nivel parejo por papel (RMS de los primeros 150 ms) sin pasar de -0.5 dBFS de pico
    float g = juce::Decibels::decibelsToGain (p.nivel) / juce::jmax (1.0e-6f, rmsInicio (L, R));
    float peak = 0.0f;
    for (size_t i = 0; i < L.size(); ++i) peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
    g = juce::jmin (g, juce::Decibels::decibelsToGain (-0.5f) / juce::jmax (1.0e-6f, peak));
    bool mono = true;
    for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; mono = mono && std::abs (L[i] - R[i]) < 1.0e-5f; }
    if (mono) R.clear();
    return makeSample (std::move (L), std::move (R), kTallerSr, p.nombre + " (" + kit.nombre + ")");
}

SamplePtr renderReceta (const juce::String& id)
{
    const auto rest = id.fromFirstOccurrenceOf ("receta:", false, false);
    const auto* kit = Catalogo::get().receta (rest.upToLastOccurrenceOf ("/", false, false));
    if (kit == nullptr) return {};
    const auto* p = kit->pad (rest.fromLastOccurrenceOf ("/", false, false).getIntValue());
    if (p == nullptr) return {};
    {
        const juce::ScopedLock sl (tallerLock);
        if (auto s = tallerCache()[id].lock()) return s;      // el Kick y el Groove abren el mismo kit: se arma una vez
    }
    auto s = renderRecetaPad (*kit, *p);
    const juce::ScopedLock sl (tallerLock);
    if (s != nullptr) tallerCache()[id] = s;
    return s;
}

SamplePtr loadLibrarySample (const juce::String& id)
{
    if (id.startsWith ("receta:")) return renderReceta (id);
    const auto& cat = Catalogo::get();
    const auto* m = cat.muestra (id);
    if (m == nullptr) return {};
    juce::AudioBuffer<float> buf;
    double rate = 48000.0;
    if (! Catalogo::leer (cat.archivo (m->archivo), buf, rate, 20.0)) return {};
    const int n = buf.getNumSamples();
    std::vector<float> l (buf.getReadPointer (0), buf.getReadPointer (0) + n), r;
    if (buf.getNumChannels() > 1) r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + n);
    auto s = makeSample (std::move (l), std::move (r), rate, m->nombre);
    return s;
}
} // namespace be

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

LibraryVoice libraryVoice (const juce::String& grupo)
{
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
    const float dur = juce::jlimit (0.05f, 12.0f, g->dur);
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
    p.push_back ({ T_VELSENS, g->capas > 1 ? 0.3f : 0.6f });
    return v;
}

SamplePtr loadLibrarySample (const juce::String& id)
{
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

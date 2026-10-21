#include "LibrarySampler.h"
#include "../Shared/Biblioteca.h"
#include "../Shared/RacoonDsp.h"

namespace racoon
{
using namespace juce;

LibrarySampler::LibrarySampler() { for (auto& b : busy) b.store (nullptr); }
LibrarySampler::~LibrarySampler() { live.store (nullptr); }

void LibrarySampler::prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); racoon::sdsp::SincTable::get(); }
void LibrarySampler::reset() { for (auto& v : voices) v = Voice(); for (auto& b : busy) b.store (nullptr); }

bool LibrarySampler::kitInUse (const Kit& kit) const
{
    for (auto& b : busy)
    {
        const auto* l = b.load (std::memory_order_acquire);
        if (l == nullptr) continue;
        for (auto& s : kit.byNote)
            if (s != nullptr)
                for (auto& layer : s->layers)
                    if (layer.get() == l) return true;
    }
    return false;
}

void LibrarySampler::collect (uint32 now)
{
    // un kit viejo se va dos segundos después de cambiarlo, y nunca mientras una voz todavía lo toca
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [this, now] (auto& r) { return now - r.second > 2000 && ! kitInUse (*r.first); }), retired.end());
}

int LibrarySampler::gmNoteFor (const String& role, int nth)
{
    static const std::map<String, std::vector<int>> gm {
        { "Kick", { 36, 35 } }, { "Snare", { 38, 40 } }, { "Clap", { 39 } }, { "Rim", { 37, 75 } }, { "Closed Hat", { 42, 44 } },
        { "Open Hat", { 46 } }, { "Ride", { 51, 59, 53 } }, { "Crash", { 49, 57, 55, 52 } }, { "Tom Low", { 41, 43 } },
        { "Tom Mid", { 45, 47 } }, { "Tom High", { 48, 50 } }, { "Shaker", { 70, 82, 54, 69 } }, { "Conga", { 63, 64, 62 } },
        { "Bongo", { 60, 61, 65, 66 } }, { "Cowbell", { 56, 67, 68 } }, { "Metal Perc", { 81, 80, 84 } }, { "Custom", { 58, 71, 72 } } };
    auto it = gm.find (role);
    if (it == gm.end() || nth >= (int) it->second.size()) return -1;
    return it->second[(size_t) nth];
}

bool LibrarySampler::loadKit (const String& id)
{
    const auto now = Time::getMillisecondCounter();
    collect (now);
    if (id.isEmpty())
    {
        live.store (nullptr);
        if (owner) retired.push_back ({ owner, now });
        owner.reset();
        currentKit = {};
        return true;
    }
    const auto& cat = biblio::Catalogo::get();
    const auto* k = cat.kit (id);
    if (k == nullptr) return false;
    auto kit = std::make_shared<Kit>();
    std::map<String, int> used;               // cuántas notas lleva cada rol
    std::vector<std::pair<int, std::shared_ptr<Sound>>> extras;   // notas GM sobrantes del mismo rol (se llenan al final)
    for (auto& p : k->pistas)
    {
        const auto* g = cat.grupo (p.grupo);
        if (g == nullptr) continue;
        auto snd = std::make_shared<Sound>();
        for (int idx : g->muestras)
        {
            const auto& m = cat.muestras[(size_t) idx];
            if (m.rr > 1) continue;
            auto layer = std::make_shared<Layer>();
            if (biblio::Catalogo::leer (cat.archivo (m.archivo), layer->audio, layer->rate, 8.0)) snd->layers.push_back (layer);
        }
        if (snd->layers.empty()) continue;
        if (p.rol == "Closed Hat" || p.rol == "Open Hat") { snd->chokeGroup = 1; snd->chokes = p.rol == "Closed Hat"; }
        int nth = used[p.rol]++;
        int note = gmNoteFor (p.rol, nth);
        while (note >= 0 && kit->byNote[note] != nullptr) note = gmNoteFor (p.rol, ++nth);
        if (note >= 0) kit->byNote[note] = snd;
        for (int more = nth + 1; gmNoteFor (p.rol, more) >= 0; ++more) extras.push_back ({ gmNoteFor (p.rol, more), snd });
        // y la tecla cromática de la pista (C1 = pista 1), como en Hamster, si nadie la ocupa
        extras.push_back ({ 36 + p.pista, snd });
    }
    for (auto& [n, s] : extras)
        if (n >= 0 && n < 128 && kit->byNote[n] == nullptr) kit->byNote[n] = s;
    if (owner) retired.push_back ({ owner, now });
    owner = kit;
    live.store (owner.get());
    currentKit = k->id;
    return true;
}

int LibrarySampler::notesMapped() const
{
    const auto* k = live.load();
    if (k == nullptr) return 0;
    int n = 0;
    for (auto& s : k->byNote) n += s != nullptr ? 1 : 0;
    return n;
}

void LibrarySampler::start (const Kit& kit, int note, float velocity)
{
    const auto* snd = kit.byNote[note].get();
    if (snd == nullptr || snd->layers.empty()) return;
    // el hi-hat cerrado corta al abierto
    if (snd->chokes)
        for (auto& v : voices)
            if (v.layer != nullptr && v.choke == snd->chokeGroup && v.note != note) v.fading = true;
    const int nl = (int) snd->layers.size();
    const int li = jlimit (0, nl - 1, (int) (velocity * (float) nl * 0.999f));
    const auto* layer = snd->layers[(size_t) li].get();
    Voice* free = nullptr;
    for (auto& v : voices) if (v.layer == nullptr) { free = &v; break; }
    if (free == nullptr)
    {
        free = &voices[0];
        for (auto& v : voices) if (v.pos > free->pos) free = &v;
    }
    free->layer = layer;
    free->pos = 0.0;
    free->inc = layer->rate / sr;
    // las capas ya traen su dinámica; la velocidad solo afina un poco
    free->gain = 0.55f + 0.45f * velocity;
    free->fade = 1.0f;
    free->fading = false;
    free->note = note;
    free->choke = snd->chokeGroup;
}

void LibrarySampler::render (AudioBuffer<float>& buffer, const MidiBuffer& midi, int startSample, int numSamples, float gain)
{
    const auto* kit = live.load();
    if (kit == nullptr) return;
    const int nch = buffer.getNumChannels();
    auto* L = buffer.getWritePointer (0);
    auto* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    auto it = midi.findNextSamplePosition (startSample);
    const float fadeStep = 1.0f / (float) jmax (1.0, 0.012 * sr);
    const auto& sinc = racoon::sdsp::SincTable::get();
    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) start (*kit, m.getNoteNumber(), m.getFloatVelocity());
        }
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (v.layer == nullptr) continue;
            const auto& a = v.layer->audio;
            const int len = a.getNumSamples();
            const int k = (int) v.pos;
            if (k + 1 >= len) { v.layer = nullptr; continue; }
            const float* c0 = a.getReadPointer (0);
            const float* c1 = a.getNumChannels() > 1 ? a.getReadPointer (1) : nullptr;
            float g = v.gain;
            if (v.fading) { v.fade -= fadeStep; if (v.fade <= 0.0f) { v.layer = nullptr; continue; } g *= v.fade; }
            // windowed sinc (was a straight line between two samples, which dulls the top and aliases when pitched up)
            float sl, sr2;
            sinc.read2 (c0, c1, len, v.pos, racoon::sdsp::SincTable::bandFor (v.inc), sl, sr2);
            l += sl * g;
            r += sr2 * g;
            v.pos += v.inc;
        }
        L[i] += l * gain;
        if (R != nullptr) R[i] += r * gain;
    }
    for (int v = 0; v < numVoices; ++v) busy[v].store (voices[v].layer, std::memory_order_release);
}
} // namespace racoon

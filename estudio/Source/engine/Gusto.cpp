#include "Gusto.h"
#include "I18n.h"
#include <map>

namespace racoon::gusto
{
using namespace juce;

namespace
{
struct Estado
{
    bool cargado = false;
    std::map<String, std::map<String, double>> puntos;
    StringArray pendientes;
    CriticalSection lock;
};
Estado& estado() { static Estado e; return e; }

void cargar (Estado& e)
{
    if (e.cargado) return;
    e.cargado = true;
    const auto json = JSON::parse (archivo());
    if (auto* obj = json.getDynamicObject())
        for (auto& cat : obj->getProperties())
            if (auto* vals = cat.value.getDynamicObject())
                for (auto& v : vals->getProperties())
                    e.puntos[cat.name.toString()][v.name.toString()] = (double) v.value;
}

void guardar (Estado& e)
{
    auto* root = new DynamicObject();
    for (auto& [cat, vals] : e.puntos)
    {
        auto* o = new DynamicObject();
        for (auto& [v, p] : vals) if (std::abs (p) > 1.0e-9) o->setProperty (Identifier (v), p);
        root->setProperty (Identifier (cat), var (o));
    }
    archivo().getParentDirectory().createDirectory();
    archivo().replaceWithText (JSON::toString (var (root)));
}
}

File archivo()
{
    const auto env = SystemStats::getEnvironmentVariable ("RACOON_GUSTO", {});
    if (env.isNotEmpty()) return File (env);
    return i18n::settingsFile().getParentDirectory().getChildFile ("gusto.json");
}

void empezar()
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    e.pendientes.clear();
}

double puntaje (const String& categoria, const String& valor)
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    cargar (e);
    auto c = e.puntos.find (categoria);
    if (c == e.puntos.end()) return 0.0;
    auto v = c->second.find (valor);
    return v == c->second.end() ? 0.0 : v->second;
}

double peso (const String& categoria, const String& valor)
{
    // each point is about twice as often; never out, never more than 8 times the rest
    return jlimit (0.1, 8.0, std::exp (0.7 * puntaje (categoria, valor)));
}

String elegir (Random& r, const String& categoria, const StringArray& opciones)
{
    if (opciones.isEmpty()) return {};
    std::vector<double> w;
    double total = 0.0;
    bool flat = true;
    for (auto& o : opciones) { w.push_back (peso (categoria, o)); total += w.back(); flat = flat && std::abs (w.back() - 1.0) < 1.0e-9; }
    int pick = opciones.size() - 1;
    if (flat) pick = r.nextInt (opciones.size());             // no points yet: the same draw as always (same seed, same song)
    else
    {
        double x = r.nextDouble() * total;
        for (int i = 0; i < opciones.size(); ++i)
        {
            if (x < w[(size_t) i]) { pick = i; break; }
            x -= w[(size_t) i];
        }
    }
    auto& e = estado();
    const ScopedLock sl (e.lock);
    // a category chosen twice in one round (two Hamster channels) is noted once
    for (int i = e.pendientes.size(); --i >= 0;)
        if (e.pendientes[i].upToFirstOccurrenceOf ("=", false, false) == categoria) e.pendientes.remove (i);
    e.pendientes.add (categoria + "=" + opciones[pick]);
    return opciones[pick];
}

StringArray elecciones()
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    return e.pendientes;
}

void calificar (const StringArray& lista, bool meGusta)
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    cargar (e);
    for (auto& item : lista)
    {
        const auto cat = item.upToFirstOccurrenceOf ("=", false, false), val = item.fromFirstOccurrenceOf ("=", false, false);
        if (cat.isEmpty() || val.isEmpty()) continue;
        auto& p = e.puntos[cat][val];
        p = jlimit (-4.0, 4.0, p + (meGusta ? 1.0 : -1.0));
    }
    guardar (e);
}

StringArray favoritos (int n)
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    cargar (e);
    std::vector<std::pair<double, String>> all;
    for (auto& [cat, vals] : e.puntos)
        for (auto& [v, p] : vals)
            if (p > 0.0) all.push_back ({ p, i18n::tr (cat) + ": " + v + " (+" + String ((int) p) + ")" });
    std::stable_sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.first > b.first; });
    StringArray out;
    for (size_t i = 0; i < all.size() && (int) i < n; ++i) out.add (all[i].second);
    return out;
}

void olvidar()
{
    auto& e = estado();
    const ScopedLock sl (e.lock);
    e.puntos.clear();
    e.cargado = true;
    archivo().deleteFile();
}
}

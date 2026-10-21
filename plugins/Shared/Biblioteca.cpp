#include "Biblioteca.h"

namespace racoon::biblio
{
using namespace juce;

namespace
{
StringArray strings (const var& v)
{
    StringArray a;
    if (auto* arr = v.getArray())
        for (auto& x : *arr) a.add (x.toString());
    return a;
}
String nombreCorto (const String& nombre, const String& pack)
{
    // "Frankensnare 14x5 arce rimshot" dentro del submenú "Frankensnare" se lee mejor sin repetir el paquete
    auto n = nombre;
    if (n.startsWithIgnoreCase (pack + " ")) n = n.substring (pack.length() + 1);
    for (auto w : { "Virtuosity ", "Sonic Pi ", "VCSL " })
        if (n.startsWith (w)) n = n.substring ((int) strlen (w));
    return n.isEmpty() ? nombre : n;
}
}

File Catalogo::carpetaUsuario()
{
    return File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Muestras Racoon");
}

Array<File> Catalogo::carpetasPosibles()
{
    Array<File> a;
    auto env = SystemStats::getEnvironmentVariable ("RACOON_MUESTRAS", {});
    if (env.isNotEmpty()) a.add (File (env));
    a.add (carpetaUsuario());
    a.add (File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Racoon").getChildFile ("Muestras Racoon"));
    a.add (File::getSpecialLocation (File::commonApplicationDataDirectory).getChildFile ("Racoon").getChildFile ("Muestras Racoon"));
    // junto al ejecutable o al plugin (una instalación portátil)
    auto exe = File::getSpecialLocation (File::currentExecutableFile);
    for (auto d = exe.getParentDirectory(); d.exists() && ! d.isRoot(); d = d.getParentDirectory())
    {
        a.add (d.getChildFile ("Muestras Racoon"));
        a.add (d.getChildFile ("muestras"));
        a.add (d.getChildFile ("Resources").getChildFile ("Muestras Racoon"));   // dentro de Racoon.app (el .dmg la trae)
        if (a.size() > 16) break;
    }
   #ifdef RACOON_MUESTRAS_DEV
    a.add (File (RACOON_MUESTRAS_DEV));
   #endif
    return a;
}

File Catalogo::buscarCarpeta()
{
    for (auto& f : carpetasPosibles())
        if (f.getChildFile ("catalogo.json").existsAsFile()) return f;
    return {};
}

const Catalogo& Catalogo::get()
{
    static Catalogo c;
    return c;
}

Catalogo::Catalogo()
{
    auto d = buscarCarpeta();
    if (d.exists()) cargar (d);
    else error = avisoSinBiblioteca();
}

void Catalogo::cargar (const File& carpeta)
{
    raiz = carpeta;
    auto json = JSON::parse (carpeta.getChildFile ("catalogo.json"));
    if (! json.isObject()) { error = "El catálogo de la biblioteca no se pudo leer."; return; }
    if (auto* obj = json["fuentes"].getDynamicObject())
        for (auto& p : obj->getProperties())
        {
            Fuente f;
            f.id = p.name.toString();
            f.nombre = p.value["nombre"].toString(); f.autor = p.value["autor"].toString();
            f.licencia = p.value["licencia"].toString(); f.url = p.value["url"].toString();
            fuentes[f.id] = f;
        }
    if (auto* obj = json["categorias"].getDynamicObject())
        for (auto& p : obj->getProperties())
            nombresCategoria[p.name.toString()] = p.value["nombre"].toString();
    if (auto* arr = json["muestras"].getArray())
    {
        muestras.reserve ((size_t) arr->size());
        for (auto& v : *arr)
        {
            Muestra m;
            m.id = v["id"]; m.archivo = v["archivo"]; m.nombre = v["nombre"]; m.cat = v["cat"]; m.grupo = v["grupo"];
            m.pack = v["pack"]; m.fuente = v["fuente"];
            m.capa = (int) v.getProperty ("capa", 1); m.capas = (int) v.getProperty ("capas", 1); m.rr = (int) v.getProperty ("rr", 1);
            m.canales = (int) v.getProperty ("canales", 1);
            m.dur = (float) (double) v.getProperty ("dur", 0.0); m.tonoHz = (float) (double) v.getProperty ("tono_hz", 0.0);
            m.rmsDb = (float) (double) v.getProperty ("rms_db", -60.0); m.brillo = (int) v.getProperty ("brillo", 0);
            m.bpm = (double) v.getProperty ("bpm", 0.0);
            m.generos = strings (v["generos"]); m.etiquetas = strings (v["etiquetas"]);
            indiceMuestra[m.id] = (int) muestras.size();
            muestras.push_back (std::move (m));
        }
    }
    for (int i = 0; i < (int) muestras.size(); ++i)
    {
        const auto& m = muestras[(size_t) i];
        auto it = indiceGrupo.find (m.grupo);
        if (it == indiceGrupo.end())
        {
            Grupo g;
            g.id = m.grupo; g.nombre = m.nombre; g.cat = m.cat; g.pack = m.pack; g.fuente = m.fuente;
            g.generos = m.generos; g.etiquetas = m.etiquetas; g.tonoHz = m.tonoHz; g.dur = m.dur;
            indiceGrupo[m.grupo] = (int) grupos.size();
            grupos.push_back (g);
            it = indiceGrupo.find (m.grupo);
        }
        auto& g = grupos[(size_t) it->second];
        g.muestras.push_back (i);
        g.capas = jmax (g.capas, m.capa);
        g.alternancias = jmax (g.alternancias, m.rr);
    }
    for (auto& g : grupos)
        std::sort (g.muestras.begin(), g.muestras.end(), [this] (int a, int b)
        {
            const auto& x = muestras[(size_t) a]; const auto& y = muestras[(size_t) b];
            return x.capa != y.capa ? x.capa < y.capa : x.rr < y.rr;
        });
    if (auto* arr = json["instrumentos"].getArray())
        for (auto& v : *arr)
        {
            Instrumento in;
            in.id = v["id"]; in.archivo = v["archivo"]; in.nombre = v["nombre"]; in.cat = v["cat"]; in.pack = v["pack"]; in.fuente = v["fuente"];
            in.notas = (int) v.getProperty ("notas", 0); in.capas = (int) v.getProperty ("capas", 1); in.bucle = (bool) v.getProperty ("bucle", false);
            if (auto* r = v["rango"].getArray(); r != nullptr && r->size() == 2) { in.rangoBajo = (*r)[0]; in.rangoAlto = (*r)[1]; }
            in.generos = strings (v["generos"]);
            instrumentos.push_back (in);
        }
    if (auto* arr = json["kits"].getArray())
        for (auto& v : *arr)
        {
            Kit k;
            k.id = v["id"]; k.nombre = v["nombre"]; k.generos = strings (v["generos"]); k.bpm = (int) v.getProperty ("bpm", 120);
            if (auto* ps = v["pistas"].getArray())
                for (auto& p : *ps)
                {
                    Kit::Pista t;
                    t.pista = (int) p["pista"]; t.nombre = p["nombre"]; t.rol = p["rol"]; t.grupo = p["grupo"];
                    if (indiceGrupo.count (t.grupo)) k.pistas.push_back (t);
                }
            if (! k.pistas.empty()) kits.push_back (k);
        }
}

const Grupo* Catalogo::grupo (const String& id) const
{
    auto it = indiceGrupo.find (id);
    return it != indiceGrupo.end() ? &grupos[(size_t) it->second] : nullptr;
}
const Muestra* Catalogo::muestra (const String& id) const
{
    auto it = indiceMuestra.find (id);
    return it != indiceMuestra.end() ? &muestras[(size_t) it->second] : nullptr;
}
const Instrumento* Catalogo::instrumento (const String& id) const
{
    for (auto& i : instrumentos) if (i.id == id || i.nombre == id) return &i;
    return nullptr;
}
const Kit* Catalogo::kit (const String& id) const
{
    for (auto& k : kits) if (k.id == id || k.nombre == id) return &k;
    return nullptr;
}
std::vector<const Grupo*> Catalogo::gruposDe (const StringArray& cats) const
{
    std::vector<const Grupo*> v;
    for (auto& g : grupos) if (cats.contains (g.cat)) v.push_back (&g);
    return v;
}
std::vector<const Kit*> Catalogo::kitsDeGenero (const String& genero) const
{
    std::vector<const Kit*> v;
    for (auto& k : kits) if (k.generos.contains (genero.toLowerCase())) v.push_back (&k);
    return v;
}
String Catalogo::nombreCategoria (const String& cat) const
{
    auto it = nombresCategoria.find (cat);
    return it != nombresCategoria.end() ? it->second : cat;
}
String Catalogo::credito (const String& f) const
{
    auto it = fuentes.find (f);
    if (it == fuentes.end()) return f;
    return it->second.nombre + ", " + it->second.autor + " (" + it->second.licencia + ")";
}

const std::vector<Onda>& Catalogo::ondas() const
{
    const ScopedLock sl (ondasLock);
    if (! ondasLeidas)
    {
        ondasLeidas = true;
        auto json = JSON::parse (raiz.getChildFile ("ondas.json"));
        if (auto* arr = json["ondas"].getArray())
            for (auto& v : *arr)
            {
                Onda o;
                o.id = v["id"]; o.archivo = v["archivo"]; o.nombre = v["nombre"]; o.grupo = v["grupo"]; o.fuente = v["fuente"];
                o.ciclo = (int) v.getProperty ("ciclo", 600); o.cuadros = (int) v.getProperty ("cuadros", 1);
                ondasCache.push_back (o);
            }
    }
    return ondasCache;
}

bool Catalogo::leer (const File& f, AudioBuffer<float>& out, double& rate, double maxSegundos)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int len = (int) std::min<int64> (r->lengthInSamples, (int64) (r->sampleRate * maxSegundos));
    out.setSize ((int) jlimit (1u, 2u, r->numChannels), len);
    r->read (&out, 0, len, 0, true, out.getNumChannels() > 1);
    rate = r->sampleRate;
    return true;
}

StringArray Catalogo::categoriasDeRol (const String& rol)
{
    const auto r = rol.toLowerCase();
    if (r.contains ("kick")) return { "bombo", "bajo_808", "cajon", "tambor" };
    if (r.contains ("snare")) return { "caja", "aro", "palmas", "timbal" };
    if (r.contains ("clap")) return { "palmas", "chasquido", "cuerpo" };
    if (r.contains ("closed")) return { "hihat_cerrado", "hihat_pedal", "shaker" };
    if (r.contains ("open")) return { "hihat_abierto", "platillo_fx" };
    if (r.contains ("ride")) return { "ride", "crash" };
    if (r.contains ("crash")) return { "crash", "platillo_fx", "ride" };
    if (r.contains ("rim")) return { "aro", "clave", "bloque" };
    if (r.contains ("tom")) return { "tom_grave", "tom_medio", "tom_agudo", "timbal" };
    if (r.contains ("shaker")) return { "shaker", "pandereta", "cabasa", "guiro", "cascabeles" };
    if (r.contains ("conga")) return { "conga", "djembe", "tambor", "cajon" };
    if (r.contains ("bongo")) return { "bongo", "darbuka", "tabla", "timbal" };
    if (r.contains ("cowbell")) return { "cencerro", "agogo" };
    if (r.contains ("metal")) return { "triangulo", "campana", "metal" };
    return { "efecto", "voz", "cuerpo", "percusion", "orquesta" };
}

// ---------------------------------------------------------------------------- menús
void menuGrupos (PopupMenu& m, const StringArray& cats, std::function<void (const String&)> onElegir, const String& actual)
{
    const auto& c = Catalogo::get();
    for (auto& cat : cats)
    {
        std::map<String, std::vector<const Grupo*>> porPack;
        for (auto& g : c.grupos) if (g.cat == cat) porPack[g.pack].push_back (&g);
        if (porPack.empty()) continue;
        PopupMenu sub;
        int total = 0;
        for (auto& [pack, gs] : porPack)
        {
            PopupMenu pm;
            bool tiene = false;
            for (auto* g : gs)
            {
                String extra;
                if (g->capas > 1) extra << "  (" << g->capas << " capas)";
                const auto id = g->id;
                pm.addItem (nombreCorto (g->nombre, pack) + extra, true, id == actual, [onElegir, id] { onElegir (id); });
                tiene = tiene || id == actual;
                ++total;
            }
            sub.addSubMenu (pack + "  " + String ((int) gs.size()), pm, true, nullptr, tiene);
        }
        m.addSubMenu (c.nombreCategoria (cat) + "  " + String (total), sub);
    }
}

void menuBiblioteca (PopupMenu& m, const StringArray& sugeridas, std::function<void (const String&)> onElegir, const String& actual)
{
    const auto& c = Catalogo::get();
    if (! c.disponible()) { m.addItem (avisoSinBiblioteca(), false, false, [] {}); return; }
    menuGrupos (m, sugeridas, onElegir, actual);
    StringArray resto;
    for (auto& g : c.grupos)
        if (! sugeridas.contains (g.cat) && ! resto.contains (g.cat) && g.cat != "loop") resto.add (g.cat);
    if (! resto.isEmpty())
    {
        PopupMenu todo;
        menuGrupos (todo, resto, onElegir, actual);
        m.addSeparator();
        m.addSubMenu ("Todo lo demás", todo);
    }
}

void menuInstrumentos (PopupMenu& m, std::function<void (const String&)> onElegir, const String& actual)
{
    const auto& c = Catalogo::get();
    if (c.instrumentos.empty()) { m.addItem (avisoSinBiblioteca(), false, false, [] {}); return; }
    std::map<String, std::vector<const Instrumento*>> porCat;
    for (auto& i : c.instrumentos) porCat[c.nombreCategoria (i.cat)].push_back (&i);
    for (auto& [cat, is] : porCat)
    {
        PopupMenu sub;
        for (auto* i : is)
        {
            const auto id = i->id;
            sub.addItem (i->nombre + "  (" + i->rangoBajo + "-" + i->rangoAlto + ")", true, id == actual, [onElegir, id] { onElegir (id); });
        }
        m.addSubMenu (cat, sub);
    }
}

void menuOndas (PopupMenu& m, bool tablas, std::function<void (const Onda&)> onElegir)
{
    const auto& c = Catalogo::get();
    std::map<String, std::vector<const Onda*>> porGrupo;
    for (auto& o : c.ondas())
        if ((o.cuadros > 1) == tablas) porGrupo[o.grupo].push_back (&o);
    if (porGrupo.empty()) { m.addItem (avisoSinBiblioteca(), false, false, [] {}); return; }
    for (auto& [g, os] : porGrupo)
    {
        // menús de 40 en 40 para que no se salgan de la pantalla
        PopupMenu sub;
        for (int i = 0; i < (int) os.size(); i += 40)
        {
            PopupMenu part;
            for (int k = i; k < jmin ((int) os.size(), i + 40); ++k)
            {
                const Onda o = *os[(size_t) k];
                part.addItem (o.nombre, [onElegir, o] { onElegir (o); });
            }
            if (os.size() <= 40) sub = part;
            else sub.addSubMenu (os[(size_t) i]->nombre + "...", part);
        }
        m.addSubMenu (g + "  " + String ((int) os.size()), sub);
    }
}

String avisoSinBiblioteca()
{
    return "No encontré Muestras Racoon. Copia la carpeta a " + Catalogo::carpetaUsuario().getFullPathName();
}
} // namespace racoon::biblio

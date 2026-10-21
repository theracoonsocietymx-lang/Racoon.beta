#include "Muestras.h"
#include "I18n.h"

namespace racoon::muestras
{
using namespace juce;
using biblio::Catalogo;

namespace
{
class Copia : public Thread
{
public:
    Copia (File from, File to, std::function<void (int)> done) : Thread ("Muestras Racoon"), src (from), dst (to), onDone (std::move (done)) {}
    void run() override
    {
        int n = 0;
        const auto tmp = dst.getSiblingFile (dst.getFileName() + " (copiando)");
        tmp.deleteRecursively();
        for (auto& f : src.findChildFiles (File::findFiles, true))
        {
            if (threadShouldExit()) { tmp.deleteRecursively(); return; }
            if (f.getFileName().startsWithChar ('.')) continue;
            const auto rel = f.getRelativePathFrom (src);
            auto t = tmp.getChildFile (rel);
            t.getParentDirectory().createDirectory();
            if (f.copyFileTo (t)) ++n;
        }
        // el catálogo al final: una carpeta a medias nunca parece completa
        dst.deleteRecursively();
        tmp.moveFileTo (dst);
        MessageManager::callAsync ([cb = onDone, n] { if (cb) cb (n); });
    }
    File src, dst;
    std::function<void (int)> onDone;
};
std::unique_ptr<Copia> copia;
}

void instalarAsync (std::function<void (int)> onDone)
{
    const auto user = Catalogo::carpetaUsuario();
    File from;
    for (auto& f : Catalogo::carpetasPosibles())
        if (f != user && f.getChildFile ("catalogo.json").existsAsFile()) { from = f; break; }
    if (! from.exists()) return;
    const auto a = from.getChildFile ("catalogo.json"), b = user.getChildFile ("catalogo.json");
    if (b.existsAsFile() && b.getSize() == a.getSize() && b.getLastModificationTime() >= a.getLastModificationTime()) return;
    if (SystemStats::getEnvironmentVariable ("RACOON_NO_COPIAR_MUESTRAS", {}).isNotEmpty()) return;
    shutdown();
    copia = std::make_unique<Copia> (from, user, std::move (onDone));
    copia->startThread (Thread::Priority::low);
}

void shutdown()
{
    if (copia != nullptr) copia->stopThread (10000);
    copia.reset();
}

String kitParaGenero (const String& id)
{
    static const std::map<String, const char*> m {
        { "house", "Deep house" }, { "deephouse", "Deep house" }, { "techhouse", "Deep house" }, { "afrohouse", "Afro" },
        { "organichouse", "House orgánico" }, { "progressive", "Deep house" }, { "nudisco", "Disco" }, { "indiedance", "Synthpop 80" },
        { "melodictechno", "Techno" }, { "techno", "Techno" }, { "hardtechno", "Techno" }, { "minimal", "Techno" }, { "acid", "TR-808" },
        { "trance", "Techno" }, { "psytrance", "Techno" }, { "dnb", "Gogodze 73" }, { "liquid", "Gogodze 73" }, { "jungle", "Gogodze 73" },
        { "dubstep", "Trap 808" }, { "futurebass", "Trap 808" }, { "trap", "Trap duro" }, { "drill", "Trap duro" }, { "phonk", "Trap 808" },
        { "reggaeton", "Reggaetón y dembow" }, { "dembow", "Reggaetón y dembow" }, { "moombahton", "Reggaetón y dembow" },
        { "cumbia", "Cumbia" }, { "afrobeats", "Afro" }, { "amapiano", "Afro" }, { "dancehall", "Reggaetón y dembow" },
        { "hiphop", "Boom bap" }, { "lofi", "Lo-fi" }, { "rnb", "TR-808" }, { "jerseyclub", "Bounce" }, { "ukgarage", "Deep house" },
        { "breakbeat", "Gogodze 73" }, { "hardstyle", "Techno" }, { "electro", "Drumtraks" }, { "synthwave", "Synthpop 80" },
        { "disco", "Disco" }, { "funk", "Virtuosity" }, { "pop", "Virtuosity" }, { "downtempo", "Lo-fi" }, { "ambient", "Mundo: tablas y darbuka" } };
    auto it = m.find (id);
    if (it == m.end()) return {};
    const auto nombre = String::fromUTF8 (it->second);
    return Catalogo::get().kit (nombre) != nullptr ? nombre : String();
}

String nombreVisible (const File& f)
{
    const auto& c = Catalogo::get();
    if (! c.disponible() || ! (f.isAChildOf (c.raiz))) return {};
    const auto rel = f.getRelativePathFrom (c.raiz).replaceCharacter ('\\', '/');
    if (f.isDirectory())
    {
        static const std::map<String, const char*> carpetas {
            { "bateria", "Batería" }, { "percusion", "Percusión" }, { "efectos", "Efectos y voces" }, { "instrumentos", "Instrumentos (SFZ)" },
            { "texturas", "Texturas" }, { "loops", "Loops" }, { "ondas", "Ondas de un ciclo" }, { "LICENCIAS", "Licencias" } };
        if (auto it = carpetas.find (rel); it != carpetas.end()) return String::fromUTF8 (it->second);
        const auto n = c.nombreCategoria (f.getFileName());
        return n != f.getFileName() ? n : String();
    }
    const auto id = rel.upToLastOccurrenceOf (".", false, false);
    if (const auto* m = c.muestra (id))
    {
        // translated here, piece by piece: the name, then its layer and round robin
        String s = i18n::tr (m->nombre);
        if (m->capas > 1) s << String::fromUTF8 ("  · ") << i18n::tr ("capa") << " " << m->capa << "/" << m->capas;
        if (m->rr > 1) s << String::fromUTF8 ("  · ") << i18n::tr ("alterna") << " " << m->rr;
        if (m->bpm > 0.0) s << "  · " << String (m->bpm, 1) << " BPM";
        return s;
    }
    return {};
}
}

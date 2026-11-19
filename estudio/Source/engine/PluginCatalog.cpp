#include "PluginCatalog.h"
#include "fx/NativeFx.h"
#include "Devices.h"

namespace racoon::Catalog
{
using namespace juce;

const StringArray& racoonProducts()
{
    // the name says the animal and what it is
    // "Racoon - " first so they all sit together in the Applications folder and in any plugin list
    static StringArray a { "Racoon - Badger Bass", "Racoon - Hamster Drums", "Racoon - Dormouse Pads", "Racoon - Squirrel Sampler", "Racoon - Ferret Guitar",
                           "Racoon - Porcupine Choir", "Racoon - Chipmunk Chords", "Racoon - Mole Rhythm", "Racoon - Skunk Sound Design",
                           "Racoon - Chinchilla Vocals", "Racoon - Hedgehog Mastering" };
    return a;
}

// "Racoon - Badger Bass", "Racoon Badger Bass" (before) and "Racoon Badger" (first version) all become "Badger Bass" / "Badger"
static String withoutBrand (const String& productName)
{
    auto s = productName.trim();
    if (s.startsWithIgnoreCase ("Racoon - ")) return s.substring (9).trim();
    if (s.startsWithIgnoreCase ("Racoon ")) return s.substring (7).trim();
    return s;
}

String animalFor (const String& productName) { return withoutBrand (productName).upToFirstOccurrenceOf (" ", false, false); }

String titleFor (const String& productName) { return withoutBrand (productName); }

// what each one is (the browser line) and whether it goes first in the chain as an instrument
static String detailFor (const String& p)
{
    if (p.contains ("Badger"))     return "Sinte de bajo ácido con secuenciador y piano roll"_u;
    if (p.contains ("Hamster"))    return "Caja de ritmos de 16 pistas con salidas separadas";
    if (p.contains ("Dormouse"))   return "Sinte de pads y atmósferas"_u;
    if (p.contains ("Squirrel"))   return "Sampler: zonas, loops y cortes por transitorios";
    if (p.contains ("Chipmunk"))   return "Acordes y arpegios con su propio sonido";
    if (p.contains ("Hedgehog"))   return "Mastering para el Master: LUFS, referencias, EQ dinámica y limitador"_u;
    if (p.contains ("Skunk"))      return "Diseño sonoro sobre audio: granular, congelado y glitch"_u;
    if (p.contains ("Mole"))       return "Ritmos euclidianos y fills con su propia batería"_u;
    if (p.contains ("Porcupine"))  return "Coros SATB, cuerdas y metales";
    if (p.contains ("Ferret"))     return "Guitarras: rasgueos, arpegios, amplificador y pedales";
    if (p.contains ("Chinchilla")) return "Voces: canal para grabar, con afinación, armonías y vocoder"_u;
    return {};
}

static bool isInstrumentProduct (const String& p)
{
    return p.contains ("Badger") || p.contains ("Hamster") || p.contains ("Dormouse") || p.contains ("Squirrel")
        || p.contains ("Ferret") || p.contains ("Porcupine") || p.contains ("Chipmunk") || p.contains ("Mole");
}

Colour colourFor (const String& productName)
{
    for (auto [animal, colour] : std::initializer_list<std::pair<const char*, uint32>> {
             { "Hamster", 0xff7fbfa8 }, { "Badger", 0xffe0789a }, { "Dormouse", 0xff7fbfa8 }, { "Squirrel", 0xffd9c452 },
             { "Chipmunk", 0xffe0789a }, { "Hedgehog", 0xffd9c452 }, { "Skunk", 0xffe0789a }, { "Mole", 0xff9a7b5c },
             { "Porcupine", 0xff7fbfa8 }, { "Ferret", 0xffd98a4e }, { "Chinchilla", 0xffb9a3d9 } })
        if (productName.containsIgnoreCase (animal)) return Colour (colour);
    return Colour (0xffd9c452);
}

Array<File> vst3Folders()
{
    Array<File> dirs;
    // a development / CI override: RACOON_PLUGIN_PATH=/path/one:/path/two
    for (auto p : StringArray::fromTokens (SystemStats::getEnvironmentVariable ("RACOON_PLUGIN_PATH", {}), ":;", {}))
        if (p.isNotEmpty()) dirs.add (File (p));
   #if JUCE_MAC
    dirs.add (File ("~/Library/Audio/Plug-Ins/VST3"));
    dirs.add (File ("/Library/Audio/Plug-Ins/VST3"));
   #elif JUCE_WINDOWS
    dirs.add (File::getSpecialLocation (File::globalApplicationsDirectory).getChildFile ("Common Files/VST3"));
   #else
    dirs.add (File ("~/.vst3"));
    dirs.add (File ("/usr/local/lib/vst3"));
    dirs.add (File ("/usr/lib/vst3"));
   #endif
    return dirs;
}

// "Racoon Hamster" (the old name) and "Racoon Hamster Drums" (the new one) are the same product
static bool sameName (const String& fullA, const String& fullB)
{
    const auto a = withoutBrand (fullA), b = withoutBrand (fullB);
    if (! (fullA.startsWithIgnoreCase ("Racoon") && fullB.startsWithIgnoreCase ("Racoon"))) return false;
    return a.equalsIgnoreCase (b) || a.startsWithIgnoreCase (b + " ") || b.startsWithIgnoreCase (a + " ");
}

static bool sameProduct (const PluginDescription& d, const String& productName)
{
    return sameName (d.name, productName) || sameName (d.descriptiveName, productName);
}

std::optional<PluginDescription> findRacoon (te::Engine& engine, const String& productName)
{
    auto& list = engine.getPluginManager().knownPluginList;

    // a remembered description can be from an older plugin with the same name (Ferret was a delay before it was a
    // guitar): when the file changed, or the kind does not match, it is scanned again
    auto stale = [&] (const PluginDescription& d)
    {
        for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
            if (format->getName() == d.pluginFormatName && format->pluginNeedsRescanning (d)) return true;
        return d.isInstrument != isInstrumentProduct (productName);
    };
    for (auto& d : list.getTypes())
        if (sameProduct (d, productName) && stale (d))
            list.removeType (d);

    // VST3 first (the format the family is built around), then AU on macOS
    for (auto fmt : { String ("VST3"), String ("AudioUnit") })
        for (auto& d : list.getTypes())
            if (d.pluginFormatName == fmt && sameProduct (d, productName)
                 && (fmt != "VST3" || File (d.fileOrIdentifier).exists()))
                return d;

    for (auto& dir : vst3Folders())
    {
        auto bundle = dir.getChildFile (productName + ".vst3");
        if (! bundle.exists())
            continue;

        for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
        {
            if (format->getName() != "VST3")
                continue;

            OwnedArray<PluginDescription> found;
            format->findAllTypesForFile (found, bundle.getFullPathName());

            for (auto* d : found)
            {
                list.addType (*d);
                if (sameProduct (*d, productName))
                    return *d;
            }
        }
    }
    return std::nullopt;
}

void registerInstalledRacoonPlugins (te::Engine& engine)
{
    fx::registerWithEngine (engine);
    for (auto& p : racoonProducts())
        findRacoon (engine, p);
}

Array<CatalogItem> racoonItems (te::Engine& engine)
{
    Array<CatalogItem> items;
    for (auto& p : racoonProducts())
    {
        CatalogItem it;
        it.source = CatalogItem::Source::racoon;
        it.name = p;
        it.isInstrument = isInstrumentProduct (p);
        it.detail = detailFor (p);
        if (auto d = findRacoon (engine, p)) it.desc = *d;
        else it.available = false;
        items.add (it);
    }
    return items;
}

Array<CatalogItem> builtInItems()
{
    auto mk = [] (const char* type, const char* name, const char* detail, bool inst)
    {
        CatalogItem it;
        it.source = CatalogItem::Source::builtIn;
        it.builtInType = type;
        it.name = String::fromUTF8 (name);
        it.detail = String::fromUTF8 (detail);
        it.isInstrument = inst;
        return it;
    };
    return {
        mk (te::FourOscPlugin::xmlTypeName,     "Sinte 4 osciladores", "Sinte básico de reserva, incluido en Racoon", true),
        mk (te::SamplerPlugin::xmlTypeName,     "Sampler",             "Reproduce muestras de audio por nota", true),
        mk (te::MidiModifierPlugin::xmlTypeName,"Modificador MIDI",    "Transpone y ajusta velocidad antes del instrumento", false),
    };
}

Array<CatalogItem> nativeEffects (const String& category)
{
    Array<CatalogItem> items;
    for (auto& d : fx::registry())
    {
        if (category.isNotEmpty() && d.category != category) continue;
        CatalogItem it;
        it.source = CatalogItem::Source::native;
        it.name = d.name;
        it.detail = d.detail;
        it.builtInType = d.id;
        it.desc = fx::describe (d);
        it.isInstrument = false;
        it.available = d.make != nullptr || d.kind != fx::Kind::effect;
        items.add (it);
    }
    return items;
}

Array<CatalogItem> externalItems (te::Engine& engine)
{
    Array<CatalogItem> items;
    auto types = engine.getPluginManager().knownPluginList.getTypes();
    std::sort (types.begin(), types.end(), [] (const PluginDescription& a, const PluginDescription& b)
               { return a.manufacturerName + a.name < b.manufacturerName + b.name; });

    for (auto& d : types)
    {
        if (te::PluginManager::isBuiltInPlugin (d) || d.pluginFormatName == fx::NativeFormat::formatName)
            continue;
        bool isFamily = false;
        for (auto& p : racoonProducts()) isFamily = isFamily || sameProduct (d, p);
        if (isFamily) continue;

        CatalogItem it;
        it.source = CatalogItem::Source::external;
        it.name = d.name;
        it.detail = d.manufacturerName + " · "_u + d.pluginFormatName;
        it.desc = d;
        it.isInstrument = d.isInstrument;
        items.add (it);
    }
    return items;
}

te::Plugin::Ptr create (te::Edit& edit, const CatalogItem& it)
{
    if (it.source == CatalogItem::Source::builtIn)
        return edit.getPluginCache().createNewPlugin (it.builtInType, {});

    if (! it.available)
        return {};

    if (it.source == CatalogItem::Source::native)
        if (auto* d = fx::find (it.builtInType); d != nullptr && d->kind != fx::Kind::effect)
        {
            if (it.builtInType == "rack") return devices::createFxRack (edit);
            if (it.builtInType == "efecto-externo") return edit.getPluginCache().createNewPlugin (te::InsertPlugin::create());
            return {};   // modulators are not plug-ins: Session::insertItem puts them on the track
        }

    return edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, it.desc);
}

bool isRacoonPlugin (const te::Plugin& p, const String& productName)
{
    if (auto* ext = dynamic_cast<const te::ExternalPlugin*> (&p))
    {
        const auto& d = ext->desc;
        if (productName.isNotEmpty()) return sameProduct (d, productName);
        for (auto& n : racoonProducts()) if (sameProduct (d, n)) return true;
    }
    return false;
}

bool isMasterProduct (const CatalogItem& it)
{
    return it.source == CatalogItem::Source::racoon && animalFor (it.name) == "Hedgehog";
}

bool listedAsInstrument (const CatalogItem& it)
{
    if (it.source == CatalogItem::Source::racoon) return ! isMasterProduct (it);
    return it.isInstrument;
}

String displayName (te::Plugin& p)
{
    const auto type = p.getPluginType();
    for (auto& it : builtInItems())
        if (it.builtInType == type) return it.name;
    if (type == te::AuxSendPlugin::xmlTypeName)   return "Envío"_u;
    if (type == te::AuxReturnPlugin::xmlTypeName) return "Retorno";
    if (type == te::RackInstance::xmlTypeName)    return devices::isFxRack (p) ? String ("Rack de efectos") : p.getName();
    if (type == te::InsertPlugin::xmlTypeName)    return "Efecto externo";
    return p.getName();
}
}

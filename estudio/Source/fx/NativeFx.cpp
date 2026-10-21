#include "NativeFx.h"
#include "fx/effects/FxBase.h"
#include "fx/effects/Hedgehog/HedgehogProcessor.h"
#include "fx/effects/Skunk/SkunkProcessor.h"
#include "fx/effects/Mole/MoleProcessor.h"
#include "fx/effects/Porcupine/PorcupineProcessor.h"
#include "fx/effects/Ferret/FerretProcessor.h"
#include "fx/effects/Chinchilla/ChinchillaProcessor.h"
#include "fx/effects/AllEffects.h"

namespace racoon::fx
{
using namespace juce;

const StringArray& categories()
{
    static StringArray a { "Dinámica"_u, "EQ y filtros", "Distorsión y color"_u, "Modulación"_u, "Delay y eco", "Reverb y espacio",
                           "Creativos y espectrales", "Utilidades", "Moduladores", "Ruteo" };
    return a;
}

template <typename T> static std::function<std::unique_ptr<RacoonProcessor>()> maker()
{
    return [] { return std::unique_ptr<RacoonProcessor> (new T()); };
}

const std::vector<FxDef>& registry()
{
    static const std::vector<FxDef> list = []
    {
        std::vector<FxDef> v {
            { "compresor", "Compresor", "Dinámica"_u, "Baja los picos y empareja el nivel. VCA, opto, FET, bus, upward y paralelo, con sidechain."_u, Kind::effect, maker<hedgehog::Processor>(), { "thresh", "ratio", "attack", "release", "makeup", "mix" } },
            { "saturador", "Saturador", "Distorsión y color"_u, "Calienta o ensucia: siete curvas de saturación con oversampling."_u, Kind::effect, maker<skunk::Processor>(), { "mode", "drive", "bias", "tone", "mix", "outtrim" } },
            { "filtro-automatico", "Filtro automático"_u, "EQ y filtros", "Filtro que se mueve solo con LFO, envolvente o secuencia de 16 pasos."_u, Kind::effect, maker<mole::Processor>(), { "type", "cutoff", "reso", "lfodepth", "lforate", "mix" } },
            { "eq-ocho", "EQ de ocho bandas", "EQ y filtros", "Ecualizador de 8 bandas con analizador, fase lineal y bandas dinámicas."_u, Kind::effect, maker<porcupine::Processor>(), { "b2_gain", "b3_gain", "b4_gain", "b5_gain", "b7_gain", "output" } },
            { "delay", "Delay", "Delay y eco", "Repeticiones a tempo: estéreo, ping-pong, doble, cinta y multitap."_u, Kind::effect, maker<ferret::Processor>(), { "mode", "divl", "feedback", "hp", "lp", "mix" } },
            { "reverb", "Reverb", "Reverb y espacio", "Espacio y cola: algorítmica o por convolución."_u, Kind::effect, maker<chinchilla::Processor>(), { "rt60", "size", "predelay", "damping", "mix", "freeze" } },
        };
        appendAllEffects (v);
        return v;
    }();
    return list;
}

const FxDef* find (const String& idOrIdentifier)
{
    const auto id = idOrIdentifier.startsWith ("racoon:") ? idOrIdentifier.substring (7) : idOrIdentifier;
    for (auto& d : registry()) if (d.id == id) return &d;
    return nullptr;
}

std::unique_ptr<RacoonProcessor> create (const String& id)
{
    if (auto* d = find (id))
        if (d->make)
        {
            auto p = d->make();
            p->effectId = "racoon:" + d->id;
            p->keyParams = d->keys;
            if (auto* fb = dynamic_cast<FxBase*> (p.get()))
                for (auto& sec : fb->editorSpec().sections)
                    for (auto& c : sec.controls)
                        if (c.label.isNotEmpty() && ! p->shortLabels.containsKey (c.id)) p->shortLabels.set (c.id, c.label);
            if (p->keyParams.isEmpty())
                for (auto* prm : p->getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && rp->paramID != "bypass" && p->keyParams.size() < 6)
                        p->keyParams.add (rp->paramID);
            return p;
        }
    return {};
}

PluginDescription describe (const FxDef& d)
{
    PluginDescription p;
    p.name = p.descriptiveName = d.name;
    p.pluginFormatName = NativeFormat::formatName;
    p.category = d.category;
    p.manufacturerName = "Racoon";
    p.version = "1.0";
    p.fileOrIdentifier = "racoon:" + d.id;
    p.uniqueId = p.deprecatedUid = p.fileOrIdentifier.hashCode();
    p.isInstrument = false;
    p.numInputChannels = 2;
    p.numOutputChannels = 2;
    return p;
}

void NativeFormat::findAllTypesForFile (OwnedArray<PluginDescription>& out, const String& id)
{
    if (auto* d = find (id)) out.add (new PluginDescription (describe (*d)));
}

String NativeFormat::getNameOfPluginFromIdentifier (const String& id)
{
    if (auto* d = find (id)) return d->name;
    return id;
}

void NativeFormat::createPluginInstance (const PluginDescription& desc, double rate, int block, PluginCreationCallback callback)
{
    auto p = create (desc.fileOrIdentifier);
    if (p == nullptr) { callback (nullptr, "Efecto de Racoon desconocido: "_u + desc.fileOrIdentifier); return; }
    p->setRateAndBufferSizeDetails (rate, block);
    callback (std::move (p), {});
}

void registerWithEngine (te::Engine& engine)
{
    auto& pm = engine.getPluginManager();
    bool has = false;
    for (auto* f : pm.pluginFormatManager.getFormats()) has = has || f->getName() == NativeFormat::formatName;
    if (! has) pm.pluginFormatManager.addFormat (new NativeFormat());
    for (auto& d : registry())
        if (d.kind == Kind::effect && d.make)
            pm.knownPluginList.addType (describe (d));
}
}

#include "PresetManager.h"
#include "../PluginProcessor.h"
#include "../Params.h"

namespace be
{
// ================================================================ builder
void PatchBuilder::set (const juce::String& id, float v)
{
    auto existing = params.getChildWithProperty ("id", id);
    if (existing.isValid()) { existing.setProperty ("value", v, nullptr); return; }
    juce::ValueTree p ("PARAM");
    p.setProperty ("id", id, nullptr);
    p.setProperty ("value", v, nullptr);
    params.appendChild (p, nullptr);
}

void PatchBuilder::route (const juce::String& src, const juce::String& dst, float amount, const juce::String& via)
{
    if (nextRoute >= kNumRoutes) return;
    const int s = modSourceNames().indexOf (src), d = modDestNames().indexOf (dst), v = modSourceNames().indexOf (via);
    jassert (s > 0 && d > 0);
    if (s <= 0 || d <= 0) return;
    const int r = nextRoute++;
    set (routeId (r, "src"), (float) s);
    set (routeId (r, "dst"), (float) d);
    set (routeId (r, "via"), (float) std::max (0, v));
    set (routeId (r, "amt"), amount);
}

void PatchBuilder::macroNames (const juce::StringArray& names) { extra.setProperty ("macroNames", names.joinIntoString ("|"), nullptr); }
void PatchBuilder::seqPattern (int pattern) { set ("seq_pattern", (float) pattern); }

// ================================================================ manager
PresetManager::PresetManager (BassEngineProcessor& p) : proc (p)
{
    registerFactory();
    loadFavourites();
}

juce::String PresetManager::factoryName (int i) const { return juce::isPositiveAndBelow (i, (int) factory.size()) ? factory[(size_t) i].name : juce::String(); }

juce::ValueTree PresetManager::buildFactory (int i) const
{
    PatchBuilder b;
    if (juce::isPositiveAndBelow (i, (int) factory.size()) && factory[(size_t) i].build) factory[(size_t) i].build (b);
    juce::ValueTree root ("BASSENGINE");
    root.setProperty ("presetName", factory[(size_t) i].name, nullptr);
    root.appendChild (b.params, nullptr);
    root.appendChild (b.extra, nullptr);
    return root;
}

void PresetManager::loadFactory (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) factory.size())) return;
    proc.undo.beginNewTransaction();
    proc.loadPatchState (buildFactory (i), false);
    currentKey = "factory:" + factory[(size_t) i].name;
}

juce::File PresetManager::userFolder() const
{
#if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Studio DOCEVEiNTE/Bass Engine");
#else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Studio DOCEVEiNTE/Bass Engine/Presets");
#endif
    dir.createDirectory();
    return dir;
}

void PresetManager::loadFavourites()
{
    favourites = juce::StringArray::fromLines (userFolder().getChildFile ("favourites.txt").loadFileAsString());
    favourites.removeEmptyStrings();
}

void PresetManager::saveFavourites() { userFolder().getChildFile ("favourites.txt").replaceWithText (favourites.joinIntoString ("\n")); }

std::vector<PresetInfo> PresetManager::list()
{
    std::vector<PresetInfo> out;
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        const auto& d = factory[(size_t) i];
        PresetInfo pi;
        pi.name = d.name; pi.category = d.category; pi.genre = d.genre; pi.character = d.character; pi.synthType = d.synth;
        pi.intensity = d.intensity; pi.complexity = d.complexity; pi.factory = true; pi.factoryIndex = i; pi.bank = "Factory";
        pi.author = "DOCEVEiNTE";
        pi.favourite = favourites.contains (pi.key());
        out.push_back (pi);
    }
    for (auto& f : userFolder().findChildFiles (juce::File::findFiles, true, "*.bepreset"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) continue;
        auto meta = xml->getChildByName ("META");
        PresetInfo pi;
        pi.factory = false; pi.file = f;
        pi.name = f.getFileNameWithoutExtension();
        pi.bank = f.getParentDirectory() == userFolder() ? "User" : f.getParentDirectory().getFileName();
        if (meta != nullptr)
        {
            pi.category = meta->getStringAttribute ("category", "Analog Bass");
            pi.genre = meta->getStringAttribute ("genre");
            pi.character = meta->getStringAttribute ("character");
            pi.synthType = meta->getStringAttribute ("synth");
            pi.intensity = meta->getIntAttribute ("intensity", 3);
            pi.complexity = meta->getIntAttribute ("complexity", 3);
            pi.author = meta->getStringAttribute ("author", "User");
        }
        pi.favourite = favourites.contains (pi.key());
        out.push_back (pi);
    }
    return out;
}

void PresetManager::load (const PresetInfo& info)
{
    if (info.factory) { loadFactory (info.factoryIndex); return; }
    auto xml = juce::XmlDocument::parse (info.file);
    if (xml == nullptr) return;
    auto v = juce::ValueTree::fromXml (*xml);
    v.setProperty ("presetName", info.name, nullptr);
    proc.undo.beginNewTransaction();
    proc.loadPatchState (v, false);
    currentKey = info.key();
}

bool PresetManager::saveUser (const juce::String& name, const juce::String& category, const juce::String& genre, const juce::String& character,
                              int intensity, int complexity, const juce::String& bank)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto dir = userFolder();
    if (bank.isNotEmpty() && bank != "User") { dir = dir.getChildFile (juce::File::createLegalFileName (bank)); dir.createDirectory(); }
    proc.currentPresetName = name;
    auto state = proc.createPatchState();
    juce::ValueTree meta ("META");
    meta.setProperty ("category", category, nullptr);
    meta.setProperty ("genre", genre, nullptr);
    meta.setProperty ("character", character, nullptr);
    meta.setProperty ("intensity", intensity, nullptr);
    meta.setProperty ("complexity", complexity, nullptr);
    meta.setProperty ("author", "User", nullptr);
    // synthesis type from oscillator 1 engine
    meta.setProperty ("synth", engineNames()[(int) proc.apvts.getRawParameterValue ("o1_engine")->load()], nullptr);
    state.appendChild (meta, nullptr);
    auto f = dir.getChildFile (safe + ".bepreset");
    if (auto xml = state.createXml()) { currentKey = f.getFullPathName(); return xml->writeTo (f); }
    return false;
}

bool PresetManager::deleteUser (const PresetInfo& info) { return ! info.factory && info.file.deleteFile(); }

void PresetManager::setFavourite (const PresetInfo& info, bool fav)
{
    if (fav) favourites.addIfNotAlreadyThere (info.key());
    else favourites.removeString (info.key());
    saveFavourites();
}

void PresetManager::step (int delta)
{
    auto all = list();
    if (all.empty()) return;
    int idx = -1;
    for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].key() == currentKey) idx = i;
    idx = (idx + delta + (int) all.size()) % (int) all.size();
    load (all[(size_t) idx]);
}

bool PresetManager::saveLayerPreset (int layer, const juce::String& name)
{
    juce::ValueTree v ("LAYERPRESET");
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto& id = rp->paramID;
            const bool layerParam = id.startsWith ("ly" + juce::String (layer + 1) + "_");
            bool oscParam = false;
            for (int o = 0; o < kNumOsc; ++o)
                if (id.startsWith ("o" + juce::String (o + 1) + "_") && (int) proc.apvts.getRawParameterValue (oscId (o, "layer"))->load() == layer)
                    oscParam = true;
            const bool subParam = layer == 0 && id.startsWith ("sub_");
            if (layerParam || oscParam || subParam)
            {
                juce::ValueTree c ("P");
                c.setProperty ("id", id.substring (id.indexOf ("_") + 1), nullptr);
                c.setProperty ("full", id, nullptr);
                c.setProperty ("osc", oscParam ? id.substring (1, 2).getIntValue() - 1 : -1, nullptr);
                c.setProperty ("v", rp->getValue(), nullptr);
                v.appendChild (c, nullptr);
            }
        }
    auto dir = userFolder().getChildFile ("Layers");
    dir.createDirectory();
    if (auto xml = v.createXml()) return xml->writeTo (dir.getChildFile (juce::File::createLegalFileName (name) + ".belayer"));
    return false;
}

juce::StringArray PresetManager::layerPresetNames() const
{
    juce::StringArray names;
    for (auto& f : userFolder().getChildFile ("Layers").findChildFiles (juce::File::findFiles, false, "*.belayer")) names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

void PresetManager::loadLayerPreset (int layer, const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userFolder().getChildFile ("Layers").getChildFile (name + ".belayer"));
    if (xml == nullptr) return;
    auto v = juce::ValueTree::fromXml (*xml);
    proc.undo.beginNewTransaction();
    // map stored oscillators onto the oscillators currently assigned to this layer (in order), else first free
    juce::Array<int> targets;
    for (int o = 0; o < kNumOsc; ++o) if ((int) proc.apvts.getRawParameterValue (oscId (o, "layer"))->load() == layer) targets.add (o);
    std::map<int, int> oscMap;
    for (auto c : v)
    {
        const int so = (int) c.getProperty ("osc");
        if (so >= 0 && oscMap.find (so) == oscMap.end())
        {
            int t = oscMap.size() < (size_t) targets.size() ? targets[(int) oscMap.size()] : -1;
            if (t < 0) for (int o = 0; o < kNumOsc; ++o) if (! targets.contains (o) && proc.apvts.getRawParameterValue (oscId (o, "on"))->load() < 0.5f) { t = o; targets.add (o); break; }
            oscMap[so] = t;
        }
    }
    for (auto c : v)
    {
        const int so = (int) c.getProperty ("osc");
        juce::String id = c.getProperty ("full").toString();
        if (so >= 0)
        {
            const int t = oscMap[so];
            if (t < 0) continue;
            id = oscId (t, c.getProperty ("id").toString().toRawUTF8());
        }
        else if (id.startsWith ("ly")) id = layId (layer, c.getProperty ("id").toString().toRawUTF8());
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost ((float) c.getProperty ("v"));
    }
    for (auto& kv : oscMap) if (kv.second >= 0) if (auto* p = proc.apvts.getParameter (oscId (kv.second, "layer"))) p->setValueNotifyingHost (p->convertTo0to1 ((float) layer));
}
} // namespace be

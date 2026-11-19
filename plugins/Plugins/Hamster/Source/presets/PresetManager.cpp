#include "PresetManager.h"
#include "../PluginProcessor.h"
#include "../dsp/Assets.h"
#include "LibraryKits.h"

namespace be
{
// ================================================================ role defaults
namespace
{
const float kRoleP[(int) Role::Count][8] {
    { 0.38f, 0.45f, 0.35f, 0.35f, 0.40f, 0.50f, 0.30f, 0.40f },   // kick
    { 0.45f, 0.60f, 0.55f, 0.45f, 0.45f, 0.30f, 0.30f, 0.40f },   // snare
    { 0.60f, 0.45f, 0.40f, 0.50f, 0.45f, 0.40f, 0.50f, 0.20f },   // clap
    { 0.60f, 0.55f, 0.55f, 0.38f, 0.50f, 0.40f, 0.00f, 0.20f },   // closed hat
    { 0.55f, 0.50f, 0.55f, 0.45f, 0.50f, 0.45f, 0.00f, 0.30f },   // open hat
    { 0.40f, 0.50f, 0.70f, 0.45f, 0.40f, 0.30f, 0.50f, 0.30f },   // ride
    { 0.45f, 0.50f, 0.50f, 0.50f, 0.55f, 0.60f, 0.10f, 0.50f },   // crash
    { 0.50f, 0.50f, 0.40f, 0.30f, 0.20f, 0.60f, 0.30f, 0.50f },   // rim
    { 0.50f, 0.35f, 0.45f, 0.40f, 0.30f, 0.30f, 0.30f, 0.10f },   // tom low
    { 0.50f, 0.35f, 0.45f, 0.40f, 0.30f, 0.30f, 0.30f, 0.10f },   // tom mid
    { 0.50f, 0.35f, 0.45f, 0.40f, 0.30f, 0.30f, 0.30f, 0.10f },   // tom high
    { 0.50f, 0.40f, 0.35f, 0.50f, 0.30f, 0.40f, 0.40f, 0.35f },   // shaker
    { 0.50f, 0.25f, 0.45f, 0.45f, 0.40f, 0.20f, 0.30f, 0.15f },   // conga
    { 0.50f, 0.25f, 0.40f, 0.45f, 0.40f, 0.20f, 0.30f, 0.15f },   // bongo
    { 0.50f, 0.50f, 0.50f, 0.40f, 0.40f, 0.50f, 0.10f, 0.10f },   // cowbell
    { 0.40f, 0.50f, 0.45f, 0.50f, 0.40f, 0.40f, 0.20f, 0.40f },   // metal
    { 0.40f, 0.40f, 0.40f, 0.20f, 0.20f, 0.50f, 0.30f, 0.20f },   // custom
};
}

void roleDefaults (int role, int model, const std::function<void (int, float)>& set)
{
    role = juce::jlimit (0, (int) Role::Count - 1, role);
    set (T_ROLE, (float) role);
    set (T_MODEL, (float) model);     // the MIDI note is kept: tracks never collide when a role changes
    for (int k = 0; k < 8; ++k) set (T_A_P1 + k, kRoleP[role][k]);
    const auto r = (Role) role;
    int group = 4;
    if (r == Role::Kick) group = 1;
    else if (r == Role::Snare || r == Role::Clap || r == Role::Rim) group = 2;
    else if (r == Role::ClosedHat || r == Role::OpenHat || r == Role::Ride || r == Role::Crash || r == Role::Shaker) group = 3;
    set (T_GROUP, (float) group);
    set (T_CHOKE, (r == Role::ClosedHat || r == Role::OpenHat) ? 1.0f : 0.0f);
    set (T_POLY, (r == Role::OpenHat || r == Role::Ride || r == Role::Crash || r == Role::Clap) ? 1.0f : 0.0f);
    float pan = 0.0f;
    switch (r)
    {
        case Role::ClosedHat: pan = 0.12f; break;
        case Role::OpenHat: pan = 0.18f; break;
        case Role::Ride: pan = -0.25f; break;
        case Role::Crash: pan = -0.2f; break;
        case Role::TomLow: pan = -0.3f; break;
        case Role::TomHigh: pan = 0.3f; break;
        case Role::Conga: pan = -0.35f; break;
        case Role::Bongo: pan = 0.35f; break;
        case Role::Shaker: pan = 0.3f; break;
        case Role::Cowbell: pan = -0.15f; break;
        case Role::Rim: pan = -0.1f; break;
        case Role::Metal: pan = 0.2f; break;
        default: break;
    }
    set (T_PAN, pan);
    float level = 0.8f;
    if (r == Role::ClosedHat || r == Role::Shaker) level = 0.55f;
    if (r == Role::OpenHat) level = 0.6f;
    if (r == Role::Ride || r == Role::Crash) level = 0.5f;
    if (r == Role::Cowbell || r == Role::Metal) level = 0.55f;
    set (T_LEVEL, level);
    set (T_C_SAMPLE, (float) FactoryPcm::defaultForRole (role));
}

// ================================================================ builder
void KitBuilder::set (const juce::String& id, float v)
{
    auto existing = params.getChildWithProperty ("id", id);
    if (existing.isValid()) { existing.setProperty ("value", v, nullptr); return; }
    juce::ValueTree p ("PARAM");
    p.setProperty ("id", id, nullptr);
    p.setProperty ("value", v, nullptr);
    params.appendChild (p, nullptr);
}

void KitBuilder::track (int t, int role, int model, const juce::String& name)
{
    roleDefaults (role, model, [this, t] (int local, float v) { tset (t, local, v); });
    tset (t, T_NOTE, (float) (36 + t));
    trackNames.set (t, name);
}

void KitBuilder::route (const juce::String& src, int trk, const juce::String& dst, float amount)
{
    if (nextRoute >= kNumRoutes) return;
    const int s = modSourceNames().indexOf (src), d = modDestNames().indexOf (dst);
    jassert (s > 0 && d > 0);
    if (s <= 0 || d <= 0) return;
    const int r = nextRoute++;
    set (routeId (r, "src"), (float) s);
    set (routeId (r, "trk"), (float) trk);
    set (routeId (r, "dst"), (float) d);
    set (routeId (r, "amt"), amount);
}

void KitBuilder::library (int t, const juce::String& grupo)
{
    auto v = libraryVoice (grupo);
    if (! v.ok) return;
    for (auto& [local, value] : v.params) tset (t, local, value);
    auto smp = extra.getOrCreateChildWithName ("SAMPLES", nullptr);
    for (int k = 0; k < v.slotIds.size(); ++k)
    {
        juce::ValueTree s ("t" + juce::String (t) + "s" + juce::String (k));
        s.setProperty ("name", v.name, nullptr);
        s.setProperty ("lib", v.slotIds[k], nullptr);
        smp.appendChild (s, nullptr);
    }
    smp.setProperty ("g" + juce::String (t), grupo, nullptr);
    trackNames.set (t, v.name.substring (0, 40));
}

void KitBuilder::macroNames (const juce::StringArray& names) { extra.setProperty ("macroNames", names.joinIntoString ("|"), nullptr); }

void KitBuilder::steps (int pattern, int t, const juce::String& str, int res)
{
    auto& tp = seq.pats[pattern].tracks[t];
    auto s = str.removeCharacters (" |");
    tp.length = juce::jlimit (1, kMaxSteps, s.length());
    tp.res = res;
    for (int i = 0; i < tp.length; ++i)
    {
        auto& st = tp.steps[i];
        st = Step();
        const auto c = s[i];
        switch (c)
        {
            case 'x': st.on = true; st.vel = 100; break;
            case 'X': st.on = true; st.vel = 120; st.accent = true; break;
            case 'o': st.on = true; st.vel = 72; break;
            case 'g': st.on = true; st.vel = 42; break;
            case '?': st.on = true; st.vel = 90; st.prob = 50; break;
            case 'r': st.on = true; st.vel = 95; st.retrig = 2; break;
            case 'R': st.on = true; st.vel = 95; st.retrig = 3; st.retrigMode = (uint8_t) RetrigMode::RollUp; break;
            case 'f': st.on = true; st.vel = 100; st.retrig = 2; st.retrigMode = (uint8_t) RetrigMode::Flam; break;
            case 'F': st.on = true; st.vel = 100; st.cond = 1; break;
            case 'n': st.on = true; st.vel = 100; st.cond = 2; break;
            case 'a': st.on = true; st.vel = 95; st.cond = 11; st.condA = 2; st.condB = 2; break;     // second of every two loops
            case 'b': st.on = true; st.vel = 95; st.cond = 11; st.condA = 4; st.condB = 4; break;     // last of four
            default: break;
        }
    }
    int maxLen = 0;
    for (auto& tr : seq.pats[pattern].tracks) maxLen = std::max (maxLen, (int) std::round (tr.length * seqResBeats (tr.res) / 0.25));
    seq.pats[pattern].masterLength = juce::jlimit (1, kMaxSteps, std::max (16, maxLen));
}

juce::ValueTree KitBuilder::build (const juce::String& name) const
{
    juce::ValueTree root ("DRUMENGINE");
    root.setProperty ("presetName", name, nullptr);
    juce::ValueTree p = params.createCopy();
    root.appendChild (p, nullptr);
    auto ex = extra.createCopy();
    ex.setProperty ("trackNames", trackNames.joinIntoString ("|"), nullptr);
    ex.appendChild (seq.toValueTree(), nullptr);
    root.appendChild (ex, nullptr);
    return root;
}

// ================================================================ manager
PresetManager::PresetManager (DrumEngineProcessor& p) : proc (p)
{
    registerFactory();
    loadFavourites();
}

juce::String PresetManager::factoryName (int i) const { return juce::isPositiveAndBelow (i, (int) factory.size()) ? factory[(size_t) i].name : juce::String(); }

juce::ValueTree PresetManager::buildFactory (int i) const
{
    KitBuilder b;
    if (juce::isPositiveAndBelow (i, (int) factory.size()) && factory[(size_t) i].build) factory[(size_t) i].build (b);
    b.set ("g_bpm", (float) factory[(size_t) i].bpm);
    return b.build (factory[(size_t) i].name);
}

void PresetManager::loadFactory (int i, int mode)
{
    if (! juce::isPositiveAndBelow (i, (int) factory.size())) return;
    proc.undo.beginNewTransaction();
    proc.loadPatchState (buildFactory (i), mode == 1, mode == 2);
    currentKey = "factory:" + factory[(size_t) i].name;
}

void PresetManager::loadInitKit()
{
    for (int i = 0; i < (int) factory.size(); ++i)
        if (factory[(size_t) i].genre == "Init") { loadFactory (i); return; }
    if (! factory.empty()) loadFactory (0);
}

juce::File PresetManager::userFolder() const
{
#if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Studio DOCEVEiNTE/Drum Engine");
#else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Studio DOCEVEiNTE/Drum Engine/Presets");
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
        pi.name = d.name; pi.genre = d.genre; pi.character = d.character; pi.kind = "Kit + Patterns";
        pi.factory = true; pi.factoryIndex = i; pi.bank = "Factory"; pi.author = "DOCEVEiNTE"; pi.bpm = d.bpm;
        pi.favourite = favourites.contains (pi.key());
        out.push_back (pi);
    }
    for (auto& f : userFolder().findChildFiles (juce::File::findFiles, true, "*.dekit"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) continue;
        auto meta = xml->getChildByName ("META");
        PresetInfo pi;
        pi.factory = false; pi.file = f;
        pi.name = f.getFileNameWithoutExtension();
        pi.bank = f.getParentDirectory() == userFolder() ? "User" : f.getParentDirectory().getFileName();
        pi.kind = "Kit + Patterns";
        if (meta != nullptr)
        {
            pi.genre = meta->getStringAttribute ("genre");
            pi.character = meta->getStringAttribute ("character");
            pi.author = meta->getStringAttribute ("author", "User");
            pi.bpm = meta->getIntAttribute ("bpm", 124);
        }
        pi.favourite = favourites.contains (pi.key());
        out.push_back (pi);
    }
    return out;
}

void PresetManager::load (const PresetInfo& info, int mode)
{
    if (info.factory) { loadFactory (info.factoryIndex, mode); return; }
    auto xml = juce::XmlDocument::parse (info.file);
    if (xml == nullptr) return;
    auto v = juce::ValueTree::fromXml (*xml);
    v.setProperty ("presetName", info.name, nullptr);
    proc.undo.beginNewTransaction();
    proc.loadPatchState (v, mode == 1, mode == 2);
    currentKey = info.key();
}

bool PresetManager::saveUser (const juce::String& name, const juce::String& genre, const juce::String& character, const juce::String& bank)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto dir = userFolder();
    if (bank.isNotEmpty() && bank != "User") { dir = dir.getChildFile (juce::File::createLegalFileName (bank)); dir.createDirectory(); }
    proc.currentPresetName = name;
    auto state = proc.createPatchState();
    juce::ValueTree meta ("META");
    meta.setProperty ("genre", genre, nullptr);
    meta.setProperty ("character", character, nullptr);
    meta.setProperty ("author", "User", nullptr);
    meta.setProperty ("bpm", juce::roundToInt (proc.getParamReal ("g_bpm")), nullptr);
    state.appendChild (meta, nullptr);
    auto f = dir.getChildFile (safe + ".dekit");
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

bool PresetManager::saveSound (int t, const juce::String& name)
{
    juce::ValueTree v ("DESOUND");
    for (int p = 0; p < T_COUNT; ++p)
    {
        if (p == T_NOTE || p == T_OUTPUT || p == T_MUTE || p == T_SOLO) continue;
        v.setProperty (trackParam (p).id, proc.trackParamObj (t, p)->getValue(), nullptr);
    }
    v.setProperty ("trackName", proc.getTrackName (t), nullptr);
    auto dir = userFolder().getChildFile ("Sounds");
    dir.createDirectory();
    if (auto xml = v.createXml()) return xml->writeTo (dir.getChildFile (juce::File::createLegalFileName (name) + ".desound"));
    return false;
}

juce::StringArray PresetManager::soundNames() const
{
    juce::StringArray out;
    for (auto& f : userFolder().getChildFile ("Sounds").findChildFiles (juce::File::findFiles, false, "*.desound"))
        out.add (f.getFileNameWithoutExtension());
    out.sort (true);
    return out;
}

void PresetManager::loadSound (int t, const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userFolder().getChildFile ("Sounds").getChildFile (name + ".desound"));
    if (xml == nullptr) return;
    auto v = juce::ValueTree::fromXml (*xml);
    for (int p = 0; p < T_COUNT; ++p)
        if (v.hasProperty (trackParam (p).id))
            proc.trackParamObj (t, p)->setValueNotifyingHost ((float) v.getProperty (trackParam (p).id));
    proc.setTrackName (t, v.getProperty ("trackName").toString());
}
} // namespace be

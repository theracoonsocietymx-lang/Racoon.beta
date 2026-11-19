#pragma once
#include <JuceHeader.h>

class BassEngineProcessor;

namespace be
{
struct PresetInfo
{
    juce::String name, category, genre, character, synthType, bank, author;
    int intensity = 3, complexity = 3;
    bool factory = true, favourite = false;
    int factoryIndex = -1;
    juce::File file;
    juce::String key() const { return factory ? "factory:" + name : file.getFullPathName(); }
};

class PresetManager
{
public:
    explicit PresetManager (BassEngineProcessor& p);

    // factory
    int numFactory() const { return (int) factory.size(); }
    juce::String factoryName (int i) const;
    void loadFactory (int i);

    // listing (factory + user)
    std::vector<PresetInfo> list();
    void load (const PresetInfo& info);
    bool saveUser (const juce::String& name, const juce::String& category, const juce::String& genre, const juce::String& character,
                   int intensity, int complexity, const juce::String& bank);
    bool deleteUser (const PresetInfo& info);
    void setFavourite (const PresetInfo& info, bool fav);
    juce::File userFolder() const;
    void step (int delta);   // next / previous in the full list

    // layer presets: save / load one layer (layer params + oscillators assigned to it)
    bool saveLayerPreset (int layer, const juce::String& name);
    juce::StringArray layerPresetNames() const;
    void loadLayerPreset (int layer, const juce::String& name);

    // build a patch state from a factory definition
    juce::ValueTree buildFactory (int i) const;

    struct Def
    {
        juce::String name, category, genre, character, synth;
        int intensity, complexity;
        std::function<void (struct PatchBuilder&)> build;
    };

private:
    BassEngineProcessor& proc;
    std::vector<Def> factory;
    juce::StringArray favourites;
    juce::String currentKey;
    void loadFavourites();
    void saveFavourites();
    void registerFactory();
};

// small helper to write patches in code
struct PatchBuilder
{
    juce::ValueTree params { "PARAMS" };
    juce::ValueTree extra { "EXTRA" };
    int nextRoute = 0;
    void set (const juce::String& id, float realValue);
    void route (const juce::String& src, const juce::String& dst, float amount, const juce::String& via = "None");
    void macroNames (const juce::StringArray& names);
    void seqPattern (int pattern);
};
} // namespace be

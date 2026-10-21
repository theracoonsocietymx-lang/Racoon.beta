#pragma once
#include <JuceHeader.h>
#include "../dsp/Pattern.h"

class DrumEngineProcessor;

namespace be
{
struct PresetInfo
{
    juce::String name, genre, character, kind, bank, author;
    bool factory = true, favourite = false;
    int factoryIndex = -1;
    int bpm = 124;
    juce::File file;
    juce::String key() const { return factory ? "factory:" + name : file.getFullPathName(); }
};

// role defaults shared by the processor (track reset) and the factory builder
void roleDefaults (int role, int model, const std::function<void (int local, float real)>& set);

// helper to describe kits and patterns in code
struct KitBuilder
{
    juce::ValueTree params { "PARAMS" };
    juce::ValueTree extra { "EXTRA" };
    SeqData seq;
    int nextRoute = 0;
    juce::StringArray trackNames;
    KitBuilder() { for (int i = 0; i < kNumTracks; ++i) trackNames.add ({}); }
    void set (const juce::String& id, float realValue);
    void tset (int track, int local, float realValue) { set (trackParamId (track, local), realValue); }
    void track (int t, int role, int model, const juce::String& name = {});
    void route (const juce::String& src, int trk, const juce::String& dst, float amount);
    void macroNames (const juce::StringArray& names);
    void library (int track, const juce::String& grupo);     // la pista suena con un grupo de Muestras Racoon
    // step string: x hit, X accent, o soft, g ghost, ? 50 %, r ratchet 2, R ratchet 3, f flam, F fill only, n not-fill, '.' rest
    void steps (int pattern, int track, const juce::String& s, int res = 3);
    juce::ValueTree build (const juce::String& name) const;
};

class PresetManager
{
public:
    explicit PresetManager (DrumEngineProcessor& p);

    int numFactory() const { return (int) factory.size(); }
    juce::String factoryName (int i) const;
    void loadFactory (int i, int mode = 0);       // 0 kit + patterns, 1 kit only, 2 patterns only
    void loadInitKit();

    std::vector<PresetInfo> list();
    void load (const PresetInfo& info, int mode = 0);
    bool saveUser (const juce::String& name, const juce::String& genre, const juce::String& character, const juce::String& bank);
    bool deleteUser (const PresetInfo& info);
    void setFavourite (const PresetInfo& info, bool fav);
    juce::File userFolder() const;
    void step (int delta);
    juce::String currentKey;

    // single-track sounds
    bool saveSound (int track, const juce::String& name);
    juce::StringArray soundNames() const;
    void loadSound (int track, const juce::String& name);

    juce::ValueTree buildFactory (int i) const;

    struct Def
    {
        juce::String name, genre, character;
        int bpm;
        std::function<void (KitBuilder&)> build;
    };

private:
    DrumEngineProcessor& proc;
    std::vector<Def> factory;
    juce::StringArray favourites;
    void loadFavourites();
    void saveFavourites();
    void registerFactory();
};
} // namespace be

#pragma once
#include "fx/core/RacoonProcessor.h"

// Racoon's own effects. They are juce::AudioPluginInstances served by a plugin format called "Racoon",
// so Tracktion hosts them exactly like a VST3: state in the project, every parameter automatable,
// sidechain, latency compensation and their own window.
namespace racoon::fx
{
enum class Kind { effect, modulator, external };

struct FxDef
{
    juce::String id;          // stable, stored in projects ("compresor")
    juce::String name;        // shown in Spanish
    juce::String category;    // browser group
    juce::String detail;      // one line of help
    Kind kind = Kind::effect;
    std::function<std::unique_ptr<RacoonProcessor>()> make;
    juce::StringArray keys;   // the knobs of its card in the channel view (first parameters when empty)
};

const std::vector<FxDef>& registry();
const FxDef* find (const juce::String& idOrIdentifier);       // "compresor" or "racoon:compresor"
juce::PluginDescription describe (const FxDef&);
std::unique_ptr<RacoonProcessor> create (const juce::String& id);
const juce::StringArray& categories();                        // in browser order

// adds the "Racoon" format to the engine and lists every effect in the known plug-in list
void registerWithEngine (te::Engine&);

class NativeFormat : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "Racoon";
    juce::String getName() const override { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override;
    bool fileMightContainThisPluginType (const juce::String& id) override { return id.startsWith ("racoon:"); }
    juce::String getNameOfPluginFromIdentifier (const juce::String&) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override { return find (d.fileOrIdentifier) != nullptr; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
protected:
    void createPluginInstance (const juce::PluginDescription&, double rate, int block, PluginCreationCallback) override;
};
}

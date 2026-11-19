#pragma once
#include "Common.h"

namespace racoon
{
// What the browser can offer and how to create each item.
struct CatalogItem
{
    enum class Source { racoon, builtIn, native, external };
    Source source = Source::builtIn;
    juce::String name;          // shown in Spanish
    juce::String detail;        // one line of help
    juce::String builtInType;   // te xmlTypeName for built-in plugins
    juce::PluginDescription desc; // for racoon / external plugins
    bool isInstrument = false;
    bool available = true;      // false when a Racoon plugin is not installed on this computer
};

namespace Catalog
{
    // product names of the family, in the order the browser shows them
    const juce::StringArray& racoonProducts();
    juce::String animalFor (const juce::String& productName); // "Badger", "Hamster"
    juce::String titleFor (const juce::String& productName);  // "Badger Bass", "Hamster Drums"
    juce::Colour colourFor (const juce::String& productName);

    // folders where the installers of the family put their VST3 bundles
    juce::Array<juce::File> vst3Folders();

    // finds an installed Racoon plugin and registers it in the known list (no full scan needed)
    std::optional<juce::PluginDescription> findRacoon (te::Engine&, const juce::String& productName);
    void registerInstalledRacoonPlugins (te::Engine&);

    juce::Array<CatalogItem> racoonItems (te::Engine&);
    juce::Array<CatalogItem> builtInItems();
    juce::Array<CatalogItem> nativeEffects (const juce::String& category = {});   // Racoon's own effects (fx::registry)
    juce::Array<CatalogItem> externalItems (te::Engine&); // scanned VST3 / AU, without the Racoon ones

    te::Plugin::Ptr create (te::Edit&, const CatalogItem&);
    bool isRacoonPlugin (const te::Plugin&, const juce::String& productName = {});
    // Hedgehog Mastering goes on the master; every other member of the family is listed with the instruments
    bool isMasterProduct (const CatalogItem&);
    bool listedAsInstrument (const CatalogItem&);
    juce::String displayName (te::Plugin&);
}
}

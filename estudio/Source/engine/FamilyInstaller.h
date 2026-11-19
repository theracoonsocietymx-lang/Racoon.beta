#pragma once
#include "Common.h"

namespace racoon
{
// When Racoon.app carries the family plug-ins (Contents/Resources/Plugins), the first launch copies the
// VST3 and Audio Unit bundles into ~/Library/Audio/Plug-Ins, the same way Badger and Hamster install themselves.
// An already installed copy with the same binary is left alone.
struct FamilyInstallResult
{
    bool attempted = false;
    bool ok = false;
    juce::StringArray installed;
    juce::String error;
};

FamilyInstallResult installBundledFamilyPlugins (const juce::File& appBundle, const juce::File& pluginRoot);
juce::File userPluginRoot();   // ~/Library/Audio/Plug-Ins

// When the .dmg carries Muestras Racoon (Contents/Resources/Muestras Racoon), the first launch copies it to
// Música/Racoon/Muestras Racoon in the background, where the plug-ins look for it. A copy with the same catalogue is left
// alone; a different one is replaced whole, except that the complete library (perfil "completo", built on this Mac by
// the installer) is never swapped for the normal one. Returns true when a copy started.
bool installBundledSamples (const juce::File& appBundle, const juce::File& destination);

// The same for the recorded effects by genre (Contents/Resources/Efectos por género): copied once into
// Música/Racoon/Efectos por género, next to the effects Racoon synthesizes itself. Returns true when a copy started.
bool installBundledEffects (const juce::File& appBundle, const juce::File& destination);
}

#pragma once
#include <JuceHeader.h>

// When the standalone app runs from its .app bundle on macOS, it installs (or updates) the
// VST3 and Audio Unit copies bundled in Contents/Resources/Plugins into the user's plug-in folders.
// That way the whole installation is: open the .dmg, drag the app to Applications, open it once.
namespace doceveinte
{
struct InstallResult
{
    bool attempted = false;   // bundle found and something needed installing
    bool ok = false;
    juce::StringArray installed;
    juce::String error;
};

// Does the copying. Safe to call from any thread; no UI.
InstallResult installBundledPlugins (const juce::File& appBundle, const juce::StringArray& legacyNames = {});

// Runs once per process from the message thread (standalone only) and shows a short confirmation.
// legacyNames: older product names whose plug-ins share the same plug-in codes; they are removed so the host does not see duplicates
void runSelfInstallerIfNeeded (const juce::String& productName, juce::AudioProcessor::WrapperType wrapper, const juce::StringArray& legacyNames = {});
}

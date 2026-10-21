#pragma once
#include "Common.h"

namespace racoon
{
// Engine policies: no clip slots (Racoon has no Session View), plugin scan in a separate process,
// short fades on clip edges so cuts never click.
class RacoonEngineBehaviour : public te::EngineBehaviour
{
public:
    explicit RacoonEngineBehaviour (bool headless) : headlessMode (headless) {}
    bool areClipSlotsEnabled() override                 { return false; }
    bool canScanPluginsOutOfProcess() override          { return ! headlessMode; }
    bool autoInitialiseDeviceManager() override         { return ! headlessMode; }
    bool autoAddClipEdgeFades() override                { return true; }
    bool shouldProcessMutedTracks() override            { return false; }
    bool shouldPlayMidiGuideNotes() override            { return true; }
private:
    bool headlessMode;
};

// Plugin windows, progress for long tasks (render, freeze) and alerts in Spanish.
class RacoonUIBehaviour : public te::UIBehaviour
{
public:
    explicit RacoonUIBehaviour (bool headless) : headlessMode (headless) {}

    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState&) override;
    void recreatePluginWindowContentAsync (te::Plugin&) override;
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress&) override;
    void showWarningAlert (const juce::String& title, const juce::String& message) override;
    void showWarningMessage (const juce::String& message) override;
    void showInfoMessage (const juce::String& message) override;
    te::Edit* getCurrentlyFocusedEdit() override        { return focusedEdit; }
    te::Edit* getLastFocusedEdit() override             { return focusedEdit; }
    te::SelectionManager* getCurrentlyFocusedSelectionManager() override { return focusedSelection; }

    te::Edit* focusedEdit = nullptr;
    te::SelectionManager* focusedSelection = nullptr;
    juce::StringArray messages;     // collected in headless mode instead of showing dialogs

private:
    bool headlessMode;
};
}

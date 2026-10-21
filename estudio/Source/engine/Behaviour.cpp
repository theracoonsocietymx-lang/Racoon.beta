#include "Behaviour.h"
#include "CrashLog.h"
#include "PluginCatalog.h"
#include "Session.h"

namespace racoon
{
using namespace juce;

namespace
{
// hosts the plugin's own editor (or a generic one when it has none)
struct HostedEditor : public te::Plugin::EditorComponent
{
    explicit HostedEditor (te::ExternalPlugin& p) : plugin (p)
    {
        JUCE_AUTORELEASEPOOL
        {
            if (auto pi = plugin.getAudioPluginInstance())
            {
                editor.reset (pi->createEditorIfNeeded());
                if (editor == nullptr)
                    editor = std::make_unique<GenericAudioProcessorEditor> (*pi);
                addAndMakeVisible (*editor);
            }
        }
        fit();
    }

    bool allowWindowResizing() override { return false; }
    ComponentBoundsConstrainer* getBoundsConstrainer() override { return editor != nullptr ? editor->getConstrainer() : nullptr; }
    void resized() override { if (editor != nullptr) editor->setBounds (getLocalBounds()); }
    void childBoundsChanged (Component* c) override
    {
        if (c == editor.get()) { plugin.edit.pluginChanged (plugin); fit(); }
    }
    void fit() { setSize (jmax (8, editor ? editor->getWidth() : 0), jmax (8, editor ? editor->getHeight() : 0)); }

    te::ExternalPlugin& plugin;
    std::unique_ptr<AudioProcessorEditor> editor;
};

#if JUCE_LINUX
constexpr bool addWindowToDesktop = false;
#else
constexpr bool addWindowToDesktop = true;
#endif

class PluginWindow : public DocumentWindow, private Timer
{
public:
    explicit PluginWindow (te::Plugin& p)
        : DocumentWindow (Catalog::displayName (p) + "  ·  "_u + p.getOwnerTrack()->getName(),
                          Colour (0xff1f1a1c), DocumentWindow::closeButton, addWindowToDesktop),
          plugin (p)
    {
        crashlog::note ("Abrir la ventana de " + p.getName());
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
        setResizeLimits (100, 50, 4000, 4000);
        setUsingNativeTitleBar (true);
        recreate();
        // listens to its channel's meter, to know whether the instrument is still sounding when the window closes
        if (auto* at = dynamic_cast<te::AudioTrack*> (plugin.getOwnerTrack()))
            if (auto* lm = at->getLevelMeterPlugin())
            {
                meterPlugin = lm;
                lm->measurer.addClient (meterClient);
                startTimer (150);
            }
        setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());
       #if JUCE_LINUX
        setAlwaysOnTop (true);
        addToDesktop();
       #endif
        storeBounds = true;
    }

    ~PluginWindow() override
    {
        stopTimer();
        if (auto* lm = dynamic_cast<te::LevelMeterPlugin*> (meterPlugin.get())) lm->measurer.removeClient (meterClient);
        storeBounds = false;
        plugin.edit.flushPluginStateIfNeeded (plugin);
        setEditor (nullptr);
    }

    void recreate()
    {
        setEditor (nullptr);
        setEditor (plugin.createEditor());
        if (editor == nullptr)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin))
                setEditor (std::make_unique<HostedEditor> (*ext));
    }

    void setEditor (std::unique_ptr<te::Plugin::EditorComponent> e)
    {
        JUCE_AUTORELEASEPOOL
        {
            setConstrainer (nullptr);
            editor.reset();
            if (e != nullptr)
            {
                editor = std::move (e);
                setContentNonOwned (editor.get(), true);
            }
            setResizable (editor == nullptr || editor->allowWindowResizing(), false);
            if (editor != nullptr && editor->allowWindowResizing())
                setConstrainer (editor->getBoundsConstrainer());
        }
    }

    bool hasEditor() const { return editor != nullptr; }
    void moved() override
    {
        if (storeBounds)
        {
            plugin.windowState->lastWindowBounds = getBounds();
            plugin.edit.pluginChanged (plugin);
        }
    }
    void closeButtonPressed() override
    {
        // the instrument holds a clip of the song (double click on it) and its sequence changed: keep the changes?
        const auto clipId = plugin.state["racoonEditingClip"].toString();
        if (clipId.isNotEmpty() && ! asking)
        {
            const auto status = Session::maquetaInbox().getChildFile ("Estado");
            if (status.getChildFile (clipId + ".cambios").existsAsFile())
            {
                asking = true;
                const auto name = plugin.state["racoonEditingClipName"].toString();
                auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                                .withTitle (String::fromUTF8 ("¿Guardar los cambios en el clip?"))
                                .withMessage (String::fromUTF8 ("Cambiaste la secuencia del clip «") + name + String::fromUTF8 ("» en ")
                                              + Catalog::displayName (plugin) + String::fromUTF8 (". ¿La paso a la maqueta?"))
                                .withButton (String::fromUTF8 ("Guardar en el clip"))
                                .withButton (String::fromUTF8 ("No guardar"))
                                .withButton (String::fromUTF8 ("Cancelar"))
                                .withAssociatedComponent (this);
                AlertWindow::showAsync (opts, [sp = SafePointer<PluginWindow> (this), clipId, status] (int r)
                {
                    if (sp == nullptr) return;
                    if (r == 0) { sp->asking = false; return; }          // Cancelar: the window stays open
                    status.getChildFile (clipId + (r == 1 ? ".devolver" : ".olvidar")).create();
                    sp->plugin.state.removeProperty ("racoonEditingClip", nullptr);
                    sp->waitForAnswer (status.getChildFile (clipId + (r == 1 ? ".devolver" : ".olvidar")), 0);
                });
                return;
            }
        }
        // an instrument playing on its own (its own Play, with the song stopped): ask before closing whether it should stop
        const bool songPlaying = plugin.edit.getTransport().isPlaying();
        if (songPlaying || recentDb < -50.0f || asking) { plugin.windowState->closeWindowExplicitly(); return; }
        asking = true;
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                        .withTitle (Catalog::displayName (plugin) + String::fromUTF8 (" sigue sonando"))
                        .withMessage (String::fromUTF8 ("¿Lo detengo al cerrar la ventana?"))
                        .withButton (String::fromUTF8 ("Sí, detenerlo"))
                        .withButton (String::fromUTF8 ("No, que siga sonando"))
                        .withAssociatedComponent (this);
        AlertWindow::showAsync (opts, [sp = SafePointer<PluginWindow> (this)] (int r)
        {
            if (sp == nullptr) return;
            sp->asking = false;
            if (r == 1) sp->silence();
            sp->plugin.windowState->closeWindowExplicitly();
        });
    }
    // the instrument takes the answer within half a second (it looks every 400 ms); then the window closes
    void waitForAnswer (const File& request, int tries)
    {
        if (request.existsAsFile() && tries < 30)
        {
            Timer::callAfterDelay (100, [sp = SafePointer<PluginWindow> (this), request, tries] { if (sp != nullptr) sp->waitForAnswer (request, tries + 1); });
            return;
        }
        request.deleteFile();
        asking = false;
        closeButtonPressed();
    }
    // all sound off on every channel: voices stop, and Racoon's own plugins also stop their sequencer
    void silence()
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (plugin.getOwnerTrack()))
        {
            plugin.edit.getTransport().ensureContextAllocated();
            for (int ch = 1; ch <= 16; ++ch)
            {
                at->injectLiveMidiMessage (MidiMessage::allSoundOff (ch), te::MPESourceID {});
                at->injectLiveMidiMessage (MidiMessage::allNotesOff (ch), te::MPESourceID {});
            }
        }
    }
    void timerCallback() override
    {
        float db = -100.0f;
        for (int c = 0; c < 2; ++c) db = jmax (db, meterClient.getAndClearAudioLevel (c).dB);
        // the loudest of the last half second
        recentDb = jmax (db, recentDb - 6.0f);
    }
    float getDesktopScaleFactor() const override { return 1.0f; }

    te::Plugin& plugin;
    std::unique_ptr<te::Plugin::EditorComponent> editor;
    bool storeBounds = false, asking = false;
    te::Plugin::Ptr meterPlugin;
    te::LevelMeasurer::Client meterClient;
    float recentDb = -100.0f;
};
}

std::unique_ptr<Component> RacoonUIBehaviour::createPluginWindow (te::PluginWindowState& pws)
{
    if (headlessMode)
        return {};

    auto* ws = dynamic_cast<te::Plugin::WindowState*> (&pws);
    if (ws == nullptr)
        return {};

    auto& plugin = ws->plugin;
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&plugin))
        if (ext->getAudioPluginInstance() == nullptr)
            return {};

    std::unique_ptr<PluginWindow> w;
    {
        struct Blocker : public Component { void inputAttemptWhenModal() override {} };
        Blocker blocker;
        blocker.enterModalState (false);
        w = std::make_unique<PluginWindow> (plugin);
    }

    if (! w->hasEditor())
        return {};

    w->setVisible (true);
    w->toFront (false);
    return w;
}

void RacoonUIBehaviour::recreatePluginWindowContentAsync (te::Plugin& p)
{
    if (auto* w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
    {
        Component::SafePointer<PluginWindow> sp (w);
        w->setEditor (nullptr);
        Timer::callAfterDelay (50, [sp] { if (sp != nullptr) sp->recreate(); });
        return;
    }
    UIBehaviour::recreatePluginWindowContentAsync (p);
}

void RacoonUIBehaviour::runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task)
{
    if (headlessMode || ! MessageManager::getInstance()->isThisTheMessageThread())
    {
        while (task.runJob() == ThreadPoolJob::jobNeedsRunningAgain) {}
        return;
    }

    struct Runner : public ThreadWithProgressWindow
    {
        Runner (te::ThreadPoolJobWithProgress& t)
            : ThreadWithProgressWindow (t.getJobName().isNotEmpty() ? t.getJobName() : String ("Procesando..."), true, true),
              job (t) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                if (job.runJob() != ThreadPoolJob::jobNeedsRunningAgain)
                    break;
                setProgress (job.getCurrentTaskProgress());
            }
        }
        te::ThreadPoolJobWithProgress& job;
    };

    Runner r (task);
    r.runThread();
}

void RacoonUIBehaviour::showWarningAlert (const String& title, const String& message)
{
    messages.add (title + ": " + message);
    if (! headlessMode)
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, title, message);
}

void RacoonUIBehaviour::showWarningMessage (const String& message) { showWarningAlert ("Racoon", message); }

void RacoonUIBehaviour::showInfoMessage (const String& message)
{
    messages.add (message);
    if (! headlessMode)
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Racoon", message);
}
}

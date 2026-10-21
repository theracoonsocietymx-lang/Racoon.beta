// Racoon: el DAW de la familia Racoon.
#include "engine/SoundLibrary.h"
#include "engine/CrashLog.h"
#include "engine/Muestras.h"
#include "engine/Session.h"
#include "engine/Behaviour.h"
#include "engine/FamilyInstaller.h"
#include "ui/MainComponent.h"

using namespace juce;

namespace racoon
{
class MainWindow : public DocumentWindow
{
public:
    MainWindow (Session& s)
        : DocumentWindow ("Racoon", ui::S::bg(), DocumentWindow::allButtons), session (s)
    {
        setUsingNativeTitleBar (true);
        content = new ui::MainComponent (s);
        content->onTitleChanged = [this] { setName (content->windowTitle()); };
        setContentOwned (content, true);
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (content);
       #else
        setMenuBar (content);
       #endif
        setResizable (true, false);
        setResizeLimits (1100, 700, 10000, 10000);
        auto area = Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        centreWithSize (jmin (1500, area.getWidth() - 40), jmin (940, area.getHeight() - 40));
        setVisible (true);
        content->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    ui::MainComponent* content = nullptr;
    Session& session;
};

class RacoonApplication : public JUCEApplication
{
public:
    const String getApplicationName() override       { return "Racoon"; }
    const String getApplicationVersion() override    { return RACOON_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override       { return false; }

    void initialise (const String& commandLine) override
    {
        // the plugin scanner runs in a child copy of the app so a crashing plugin cannot take Racoon down
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        crashlog::install();
        look = std::make_unique<ui::AncientLook>();
        LookAndFeel::setDefaultLookAndFeel (look.get());

        // Badger and Hamster travel inside Racoon.app: install them the first time (or when they are newer)
        FamilyInstallResult installed;
       #if JUCE_MAC
        if (auto app = File::getSpecialLocation (File::currentApplicationFile); app.hasFileExtension ("app"))
        {
            installed = installBundledFamilyPlugins (app, userPluginRoot());
            installBundledSamples (app, File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Muestras Racoon"));
            installBundledEffects (app, File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile (String::fromUTF8 ("Efectos por género")));
        }
       #endif
        auto* uiB = new RacoonUIBehaviour (false);
        engine = std::make_unique<te::Engine> ("Racoon", std::unique_ptr<te::UIBehaviour> (uiB), std::make_unique<RacoonEngineBehaviour> (false));
        engine->getPluginManager().setUsesSeparateProcessForScanning (true);
        session = std::make_unique<Session> (*engine, uiB);

        // Racoon asks which genre you want and opens with a full block of it (16 bars, every channel playing);
        // the last project is one click away in Archivo > Abrir el último proyecto
        auto requested = File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted().trim());
        const bool openedFile = commandLine.trim().isNotEmpty() && requested.hasFileExtension ("racoon") && requested.existsAsFile();
        if (openedFile) session->openProject (requested);
        else session->newProject (false);          // empty for a moment: the genre picker builds the block

        window = std::make_unique<MainWindow> (*session);
        window->content->onTitleChanged();
        if (! openedFile) window->content->showGenrePicker (true);   // "¿Qué vamos a producir?"
        else window->content->checkSessionHealth();

        if (auto rec = session->findRecoveryFile(); rec.existsAsFile())
            window->content->openProject (session->getFile());

        // the last time Racoon closed by itself: say so once, with the report at hand
        if (auto report = crashlog::takeNewReport(); report.existsAsFile())
        {
            auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::WarningIcon).withTitle ("Racoon")
                            .withMessage (String::fromUTF8 ("La última vez Racoon se cerró sin que lo pidieras. Dejé un informe con lo que estaba pasando "
                                                            "(") + report.getFileName() + String::fromUTF8 ("). Si se repite, mándalo y lo arreglamos."))
                            .withButton ("Ver el informe").withButton ("Seguir");
            AlertWindow::showAsync (opts, [report] (int r) { if (r == 1) report.revealToUser(); });
        }

        if (installed.attempted)
        {
            StringArray names;
            for (auto& n : installed.installed) names.addIfNotAlreadyThere (n);
            if (installed.error.isEmpty())
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Familia Racoon instalada",
                    names.joinIntoString (", ") + String::fromUTF8 (" quedaron instalados en tu Mac (VST3 y Audio Unit).\n\n"
                    "Ya aparecen en el navegador de Racoon. En tu DAW, vuelve a buscar plugins (Rescan) en sus ajustes."), "Listo");
            else
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Racoon", installed.error);
        }
    }

    void shutdown() override
    {
        library::shutdown();
        muestras::shutdown();
        window.reset();
        if (session != nullptr)
        {
            if (! session->isUntitled() || session->getFile().existsAsFile())
                ui::prefs().setValue ("lastProject", session->getFile().getFullPathName());
            session->autosaveNow();
        }
        ui::prefs().saveIfNeeded();
        session.reset();
        engine.reset();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        crashlog::cleanExit();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr || window->content == nullptr) { quit(); return; }
        window->content->maybeSaveThen ([] { JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const String& commandLine) override
    {
        File f (commandLine.unquoted().trim());
        if (window != nullptr && f.hasFileExtension ("racoon") && f.existsAsFile())
            window->content->openProject (f);
    }

private:
    std::unique_ptr<ui::AncientLook> look;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION (racoon::RacoonApplication)

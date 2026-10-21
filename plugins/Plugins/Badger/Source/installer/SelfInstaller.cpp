#include "SelfInstaller.h"
#include "../ui/Utf8.h"

namespace doceveinte
{
using namespace juce;

#if JUCE_MAC || DOCEVEINTE_INSTALLER_TEST
namespace
{
    File mainBinary (const File& bundle)
    {
        auto macos = bundle.getChildFile ("Contents/MacOS");
        auto files = macos.findChildFiles (File::findFiles, false);
        return files.isEmpty() ? File() : files.getFirst();
    }

    String fingerprint (const File& bundle)
    {
        auto bin = mainBinary (bundle);
        if (! bin.existsAsFile()) return {};
        MemoryBlock mb;
        if (! bin.loadFileAsData (mb)) return {};
        uint64 h = 1469598103934665603ull;   // FNV-1a
        auto* d = static_cast<const uint8*> (mb.getData());
        for (size_t i = 0; i < mb.getSize(); ++i) { h ^= d[i]; h *= 1099511628211ull; }
        return String::toHexString ((int64) h) + ":" + String ((int64) mb.getSize());
    }

    bool run (const StringArray& args, int timeoutMs = 60000)
    {
        ChildProcess p;
        if (! p.start (args, ChildProcess::wantStdOut | ChildProcess::wantStdErr)) return false;
        if (! p.waitForProcessToFinish (timeoutMs)) { p.kill(); return false; }
        return p.getExitCode() == 0;
    }
}
#endif

InstallResult installBundledPlugins (const File& appBundle, const StringArray& legacyNames)
{
    InstallResult res;
   #if JUCE_MAC || DOCEVEINTE_INSTALLER_TEST
    const auto src = appBundle.getChildFile ("Contents/Resources/Plugins");
    if (! src.isDirectory()) return res;

    const auto lib = File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins");
    bool anyAU = false;
    // old names of this instrument use the same plug-in codes: remove them or the host lists it twice
    for (const auto& legacy : legacyNames)
        for (auto f : { lib.getChildFile ("VST3/" + legacy + ".vst3"), lib.getChildFile ("Components/" + legacy + ".component") })
            if (f.exists())
            {
                res.attempted = true;
                f.deleteRecursively();
                anyAU = anyAU || f.hasFileExtension ("component");
            }
    for (const auto& bundle : src.findChildFiles (File::findDirectories, false))
    {
        File destDir;
        if (bundle.hasFileExtension ("vst3"))           destDir = lib.getChildFile ("VST3");
        else if (bundle.hasFileExtension ("component")) destDir = lib.getChildFile ("Components");
        else continue;

        const auto dest = destDir.getChildFile (bundle.getFileName());
        if (dest.isDirectory() && fingerprint (dest) == fingerprint (bundle)) continue;   // already up to date

        res.attempted = true;
        if (! destDir.createDirectory()) { res.error = "No se pudo crear " + destDir.getFullPathName(); return res; }
        if (dest.exists() && ! dest.deleteRecursively()) { res.error = "No se pudo reemplazar " + dest.getFullPathName(); return res; }
        // ditto keeps permissions, symlinks and the code signature intact
        if (! run ({ "/usr/bin/ditto", bundle.getFullPathName(), dest.getFullPathName() }) || ! dest.isDirectory())
        {
            res.error = "No se pudo copiar " + bundle.getFileName();
            return res;
        }
        run ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", dest.getFullPathName() });
        res.installed.add (dest.getFullPathName());
        anyAU = anyAU || bundle.hasFileExtension ("component");
    }
    if (anyAU) run ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }, 5000);   // refresh the AU cache
    res.ok = res.attempted;
   #else
    ignoreUnused (appBundle, legacyNames);
   #endif
    return res;
}

void runSelfInstallerIfNeeded (const String& productName, AudioProcessor::WrapperType wrapper, const StringArray& legacyNames)
{
   #if JUCE_MAC || DOCEVEINTE_INSTALLER_TEST
    static bool done = false;
    if (done || wrapper != AudioProcessor::wrapperType_Standalone) return;
    done = true;
    const auto app = File::getSpecialLocation (File::currentApplicationFile);
    if (! app.hasFileExtension ("app")) return;

    MessageManager::callAsync ([productName, app, legacyNames]
    {
        const auto res = installBundledPlugins (app, legacyNames);
        if (! res.attempted) return;
        if (res.ok || res.error.isEmpty())
        {
            String msg;
            msg << "El VST3 y el Audio Unit de " << productName << " quedaron instalados en tu Mac.\n\n"
                << "En tu DAW, vuelve a buscar plugins (Rescan) en sus ajustes. "_u
                << productName << " aparece en Plug-Ins, dentro de Racoon.\n\n"
                << "Esta app también funciona sola, sin DAW."_u;
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, productName + " instalado", msg, "Listo");
        }
        else
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, productName,
                                              "No se pudieron instalar los plugins.\n\n" + res.error, "OK");
        }
    });
   #else
    ignoreUnused (productName, wrapper, legacyNames);
   #endif
}
}

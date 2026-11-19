#include "FamilyInstaller.h"

namespace racoon
{
using namespace juce;

namespace
{
File mainBinary (const File& bundle)
{
    // VST3: Contents/MacOS or Contents/x86_64-linux; AU: Contents/MacOS
    for (auto sub : { "Contents/MacOS", "Contents/x86_64-linux", "Contents/arm64-linux" })
    {
        auto files = bundle.getChildFile (sub).findChildFiles (File::findFiles, false);
        if (! files.isEmpty()) return files.getFirst();
    }
    return {};
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

bool copyBundle (const File& from, const File& to)
{
   #if JUCE_MAC
    // ditto keeps permissions, symlinks and the code signature intact
    return run ({ "/usr/bin/ditto", from.getFullPathName(), to.getFullPathName() }) && to.isDirectory();
   #else
    return from.copyDirectoryTo (to);
   #endif
}
}

File userPluginRoot()
{
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins");
}

FamilyInstallResult installBundledFamilyPlugins (const File& appBundle, const File& lib)
{
    FamilyInstallResult res;
    const auto src = appBundle.getChildFile ("Contents/Resources/Plugins");
    if (! src.isDirectory()) return res;

    bool anyAU = false;
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
        if (! copyBundle (bundle, dest)) { res.error = "No se pudo copiar " + bundle.getFileName(); return res; }
       #if JUCE_MAC
        run ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", dest.getFullPathName() });
       #endif
        // the same plugin under its older names ("Racoon Badger", then "Racoon Badger Bass", now "Racoon - Badger Bass")
        // would show up two or three times
        const auto base = bundle.getFileNameWithoutExtension();
        if (base.startsWith ("Racoon - "))
        {
            const auto title = base.substring (9).trim();                      // "Badger Bass"
            const auto animal = title.upToFirstOccurrenceOf (" ", false, false);  // "Badger"
            for (auto oldName : { "Racoon " + title, "Racoon " + animal })
            {
                const auto old = destDir.getChildFile (oldName + bundle.getFileExtension());
                if (old != dest && old.isDirectory()) old.deleteRecursively();
            }
        }
        res.installed.add (bundle.getFileNameWithoutExtension());
        anyAU = anyAU || bundle.hasFileExtension ("component");
    }
   #if JUCE_MAC
    if (anyAU) run ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }, 5000);   // refresh the AU cache
   #endif
    res.ok = res.attempted;
    return res;
}

bool installBundledSamples (const File& appBundle, const File& dest)
{
    const auto src = appBundle.getChildFile ("Contents/Resources/Muestras Racoon");
    const auto srcCat = src.getChildFile ("catalogo.json");
    if (! srcCat.existsAsFile()) return false;
    const auto destCat = dest.getChildFile ("catalogo.json");
    if (destCat.existsAsFile())
    {
        const auto have = destCat.loadFileAsString(), bring = srcCat.loadFileAsString();
        if (have == bring) return false;   // already there
        // the complete library (built on this Mac by the installer) is never swapped for the normal one
        const auto full = [] (const String& c) { return c.contains ("\"perfil\":\"completo\""); };
        if (full (have) && ! full (bring)) return false;
    }

    // 420 MB: in the background, into a side folder that only takes the real name when it is complete
    Thread::launch ([src, dest]
    {
        const auto tmp = dest.getSiblingFile (dest.getFileName() + " (copiando)");
        tmp.deleteRecursively();
        if (! dest.getParentDirectory().createDirectory()) return;
        if (! copyBundle (src, tmp) || ! tmp.getChildFile ("catalogo.json").existsAsFile()) { tmp.deleteRecursively(); return; }
        const auto old = dest.getSiblingFile (dest.getFileName() + " (anterior)");
        old.deleteRecursively();
        if (dest.exists() && ! dest.moveFileTo (old)) { tmp.deleteRecursively(); return; }
        if (! tmp.moveFileTo (dest)) { old.moveFileTo (dest); return; }
        old.deleteRecursively();
    });
    return true;
}

bool installBundledEffects (const File& appBundle, const File& dest)
{
    const auto src = appBundle.getChildFile (String::fromUTF8 ("Contents/Resources/Efectos por género"));
    const auto mark = src.getChildFile (".racoon-efectos-grabados");
    if (! mark.existsAsFile()) return false;
    const auto have = dest.getChildFile (".racoon-efectos-grabados");
    if (have.existsAsFile() && have.loadFileAsString().trim() == mark.loadFileAsString().trim()) return false;
    // merged into the folder (the synthesized effects that are already there stay), in the background
    Thread::launch ([src, dest]
    {
        if (! dest.createDirectory()) return;
        copyBundle (src, dest);
    });
    return true;
}
}

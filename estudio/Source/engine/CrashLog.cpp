#include "CrashLog.h"
#include <csignal>

namespace racoon::crashlog
{
using namespace juce;

namespace
{
constexpr int keep = 5, lines = 200;
SpinLock lock;
String ring[lines];
std::atomic<int> next { 0 };
bool installed = false;

File markFile() { return folder().getChildFile ("sesion-abierta.txt"); }
File toldFile() { return folder().getChildFile ("ultimo-avisado.txt"); }

void handler (void*)
{
    writeReport ("Racoon se cerró de golpe", SystemStats::getStackBacktrace());
}
}

File folder()
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
                #if JUCE_MAC
                 .getChildFile ("Application Support")
                #endif
                 .getChildFile ("Racoon").getChildFile ("Fallos");
    f.createDirectory();
    return f;
}

void note (const String& s)
{
    const SpinLock::ScopedLockType sl (lock);
    const int i = next.fetch_add (1) % lines;
    ring[i] = Time::getCurrentTime().formatted ("%H:%M:%S  ") + s;
}

Array<File> reports()
{
    auto list = folder().findChildFiles (File::findFiles, false, "fallo *.txt");
    list.sort();
    std::reverse (list.begin(), list.end());
    return list;
}

File writeReport (const String& reason, const String& stack)
{
    String text;
    text << reason << "\n" << Time::getCurrentTime().toString (true, true, true, true) << "\n"
         << SystemStats::getOperatingSystemName() << ", " << SystemStats::getCpuModel() << ", "
         << SystemStats::getMemorySizeInMegabytes() << " MB\n\n";
    text << "Lo último que pasó:\n";
    {
        // no lock here: in a crash the lock may be held by the thread that broke
        const int n = next.load();
        for (int k = jmax (0, n - lines); k < n; ++k)
            if (ring[k % lines].isNotEmpty()) text << "  " << ring[k % lines] << "\n";
    }
    text << "\nPila:\n" << stack << "\n";
    auto f = folder().getChildFile ("fallo " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".txt");
    f = f.getNonexistentSibling (false);
    f.replaceWithText (text);
    auto all = reports();
    for (int i = keep; i < all.size(); ++i) all[i].deleteFile();
    return f;
}

void install()
{
    if (installed) return;
    installed = true;
    SystemStats::setApplicationCrashHandler (handler);
    // the last run did not quit cleanly (a crash, or it was killed): there may be no report, but there is a mark
    const auto mark = markFile();
    const auto all = reports();
    if (mark.existsAsFile() && (all.isEmpty() || all[0].getLastModificationTime() < mark.getLastModificationTime()))
        writeReport ("La sesión anterior no se cerró bien (sin pila: Racoon fue detenido o se colgó)", {});
    markFile().replaceWithText (Time::getCurrentTime().toISO8601 (true));
    note ("Racoon arranca");
}

void cleanExit()
{
    note ("Racoon se cierra bien");
    markFile().deleteFile();
}

File takeNewReport()
{
    auto all = reports();
    if (all.isEmpty()) return {};
    const auto told = toldFile().loadFileAsString().trim();
    if (all[0].getFileName() == told) return {};
    toldFile().replaceWithText (all[0].getFileName());
    return all[0];
}
}

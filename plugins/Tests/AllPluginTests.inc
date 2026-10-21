// Pulls in every plugin's own tests (Plugins/<Name>/<Name>Tests.inc) and runs them.
#include <functional>
#include <vector>
struct PluginTestEntry { const char* name; std::function<void (const juce::File&)> run; };
static std::vector<PluginTestEntry>& pluginTests() { static std::vector<PluginTestEntry> v; return v; }
struct RegisterTest { RegisterTest (const char* n, std::function<void (const juce::File&)> f) { pluginTests().push_back ({ n, std::move (f) }); } };

#if __has_include("HedgehogTests.inc")
 #include "HedgehogTests.inc"
#endif
#if __has_include("SkunkTests.inc")
 #include "SkunkTests.inc"
#endif
#if __has_include("MoleTests.inc")
 #include "MoleTests.inc"
#endif
#if __has_include("PorcupineTests.inc")
 #include "PorcupineTests.inc"
#endif
#if __has_include("FerretTests.inc")
 #include "FerretTests.inc"
#endif
#if __has_include("ChinchillaTests.inc")
 #include "ChinchillaTests.inc"
#endif
#if __has_include("DormouseTests.inc")
 #include "DormouseTests.inc"
#endif
#if __has_include("SquirrelTests.inc")
 #include "SquirrelTests.inc"
#endif
#if __has_include("ChipmunkTests.inc")
 #include "ChipmunkTests.inc"
#endif

static void runAllPluginTests (const juce::File& out, const juce::StringArray& only)
{
    for (auto& t : pluginTests())
        if (only.isEmpty() || only.contains (t.name))
        {
            std::cout << "=========== " << t.name << std::endl;
            t.run (out);
        }
}

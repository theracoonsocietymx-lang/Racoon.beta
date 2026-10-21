// Every effect of the registry runs the common contract; the ones with their own file add the checks of their spec.
#include <functional>
#include <vector>
struct PluginTestEntry { const char* name; std::function<void (const juce::File&)> run; };
static std::vector<PluginTestEntry>& pluginTests() { static std::vector<PluginTestEntry> v; return v; }
struct RegisterTest { RegisterTest (const char* n, std::function<void (const juce::File&)> f) { pluginTests().push_back ({ n, std::move (f) }); } };

#include "HedgehogTests.inc"
#include "SkunkTests.inc"
#include "MoleTests.inc"
#include "PorcupineTests.inc"
#include "FerretTests.inc"
#include "ChinchillaTests.inc"
#if __has_include("NewEffectsTests.inc")
 #include "NewEffectsTests.inc"
#endif
#include "ExtrasTests.inc"
#include "SidechainTests.inc"

static void runAllPluginTests (const juce::File& out, const juce::StringArray& only)
{
    using namespace juce;
    for (auto& def : racoon::fx::registry())
    {
        if (! only.isEmpty() && ! only.contains (def.id)) continue;
        if (def.kind != racoon::fx::Kind::effect || ! def.make) continue;
        std::cout << "=========== " << def.id << " (" << def.name << ")" << std::endl;
        bool own = false;
        for (auto& t : pluginTests())
            if (def.id == t.name) { t.run (out); own = true; }
        if (! own)
        {
            const auto id = def.id;
            commonChecks (id, [id] { return racoon::fx::create (id).release(); }, out);
        }
    }
}

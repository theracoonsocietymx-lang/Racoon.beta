#include "I18n.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <vector>

using namespace juce;

namespace racoon::i18n
{
namespace
{
struct Pair { const char* es; const char* en; };
// the dictionary: Spanish as written in the code, English as shown (generated from the sources, reviewed by hand)
const Pair dictionary[] = {
#include "Diccionario.inc"
};

std::atomic<int> lang { -1 };      // -1: not read yet
uint32 lastCheck = 0;               // Racoon and each plugin are separate modules: each one looks at the file now and then
Time lastModified;

File folder()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Application Support").getChildFile ("Racoon");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Racoon");
   #endif
}

// a sentence with its numbers taken out: "Rola de 216 compases (7:05)" -> "Rola de {n} compases ({n})"
String withoutNumbers (const String& s, StringArray* numbers)
{
    String out;
    auto t = s.getCharPointer();
    juce_wchar prev = 0;
    while (! t.isEmpty())
    {
        const juce_wchar c = *t;
        const bool sign = (c == '-' || c == '+' || c == 0x2212) && CharacterFunctions::isDigit (*(t + 1)) && ! CharacterFunctions::isLetterOrDigit (prev);
        if (CharacterFunctions::isDigit (c) || sign)
        {
            String num;
            num << String::charToString (c);
            ++t;
            for (;;)
            {
                const juce_wchar d = *t;
                if (CharacterFunctions::isDigit (d)) { num << String::charToString (d); ++t; continue; }
                if ((d == '.' || d == ',' || d == ':') && CharacterFunctions::isDigit (*(t + 1))) { num << String::charToString (d); ++t; continue; }
                break;
            }
            if (numbers != nullptr) numbers->add (num);
            out << "{n}";
            prev = '0';
            continue;
        }
        out << String::charToString (c);
        prev = c;
        ++t;
    }
    return out;
}

String withNumbers (const String& pattern, const StringArray& numbers)
{
    String out;
    int k = 0, from = 0;
    for (;;)
    {
        const int i = pattern.indexOf (from, "{n}");
        if (i < 0) { out << pattern.substring (from); break; }
        out << pattern.substring (from, i) << (k < numbers.size() ? numbers[k] : String());
        ++k;
        from = i + 3;
    }
    return out;
}

// lower and upper case that also handle the accents (the C library may not, depending on the locale)
String lowerEs (const String& s)
{
    auto r = s.toLowerCase();
    const auto up = String::fromUTF8 ("ÁÉÍÓÚÑÜ"), lo = String::fromUTF8 ("áéíóúñü");
    for (int i = 0; i < up.length(); ++i) r = r.replaceCharacter (up[i], lo[i]);
    return r;
}
String upperEs (const String& s)
{
    auto r = s.toUpperCase();
    const auto up = String::fromUTF8 ("ÁÉÍÓÚÑÜ"), lo = String::fromUTF8 ("áéíóúñü");
    for (int i = 0; i < up.length(); ++i) r = r.replaceCharacter (lo[i], up[i]);
    return r;
}
bool allCaps (const String& s) { return s.containsAnyOf (String::fromUTF8 ("ABCDEFGHIJKLMNOPQRSTUVWXYZÁÉÍÓÚÑ")) && s == upperEs (s); }

struct Table
{
    std::map<String, String> exact, lower, patterns;
    std::vector<std::pair<String, String>> phrases, phrasesLower;     // longest first, for text built from pieces
    // sentences with a piece that changes ("Variación de {s} ({s}), {n} compases..."): the pieces go through the
    // dictionary on their own and land in the same order in the English
    struct Wild { StringArray pieces; String en; };
    std::vector<Wild> wild;
    Table()
    {
        for (auto& p : dictionary)
        {
            const auto es = String::fromUTF8 (p.es), en = String::fromUTF8 (p.en);
            if (es.isEmpty() || en.isEmpty()) continue;
            if (es.contains ("{s}"))
            {
                const auto pes = withoutNumbers (es, nullptr);
                Wild w;
                for (int from = 0;;)
                {
                    const int i = pes.indexOf (from, "{s}");
                    if (i < 0) { w.pieces.add (pes.substring (from)); break; }
                    w.pieces.add (pes.substring (from, i));
                    from = i + 3;
                }
                w.en = withoutNumbers (en, nullptr);
                wild.push_back (w);
                continue;
            }
            exact[es] = en;
            lower[lowerEs (es)] = en;
            const auto pes = withoutNumbers (es, nullptr);
            if (pes != es) patterns[pes] = withoutNumbers (en, nullptr);
            if (es.trim().length() >= 4 && pes == es)
            {
                phrases.push_back ({ es.trim(), en.trim() });
                phrasesLower.push_back ({ lowerEs (es.trim()), en.trim() });
            }
        }
        auto longest = [] (const auto& a, const auto& b) { return a.first.length() > b.first.length(); };
        std::stable_sort (phrases.begin(), phrases.end(), longest);
        std::stable_sort (phrasesLower.begin(), phrasesLower.end(), longest);
        std::stable_sort (wild.begin(), wild.end(), [] (const Wild& a, const Wild& b) { return a.pieces.joinIntoString ("").length() > b.pieces.joinIntoString ("").length(); });
    }

    // a sentence against the patterns with pieces that change (numbers already as {n})
    bool wildMatch (const String& p, String& out) const
    {
        for (auto& w : wild)
        {
            if (! p.startsWith (w.pieces[0]) || ! p.endsWith (w.pieces[w.pieces.size() - 1])) continue;
            StringArray caps;
            int at = w.pieces[0].length();
            bool ok = true;
            for (int k = 1; k < w.pieces.size() && ok; ++k)
            {
                const auto& lit = w.pieces[k];
                const int i = k == w.pieces.size() - 1 ? p.length() - lit.length() : p.indexOf (at + 1, lit);
                if (i <= at) { ok = false; break; }
                caps.add (p.substring (at, i));
                at = i + lit.length();
            }
            if (! ok || at != p.length()) continue;
            out.clear();
            int k = 0;
            for (int from = 0;;)
            {
                const int i = w.en.indexOf (from, "{s}");
                if (i < 0) { out << w.en.substring (from); break; }
                String piece;
                const auto c = caps[k++];
                out << w.en.substring (from, i) << (whole (c.trim(), piece) ? c.upToFirstOccurrenceOf (c.trim(), false, false) + piece : phrasesIn (c));
                from = i + 3;
            }
            return true;
        }
        return false;
    }

    bool whole (const String& s, String& out) const
    {
        if (auto it = exact.find (s); it != exact.end()) { out = it->second; return true; }
        if (auto it = lower.find (lowerEs (s)); it != lower.end())
        {
            out = allCaps (s) ? upperEs (it->second) : it->second;
            return true;
        }
        StringArray nums;
        const auto p = withoutNumbers (s, &nums);
        if (! nums.isEmpty())
            if (auto it = patterns.find (p); it != patterns.end()) { out = withNumbers (it->second, nums); return true; }
        if (! wild.empty() && wildMatch (p, out)) { out = withNumbers (out, nums); return true; }
        return false;
    }

    // the text with its spaces around kept
    bool trimmed (const String& s, String& out) const
    {
        const auto t = s.trim();
        if (t.isEmpty()) { out = s; return true; }
        String r;
        if (! whole (t, r))
        {
            // a trailing period, colon or ellipsis often changes between a label and a sentence
            for (auto* tail : { ".", ":", "...", "\xe2\x80\xa6" })
            {
                const auto tl = String::fromUTF8 (tail);
                if (t.endsWith (tl) && whole (t.dropLastCharacters (tl.length()).trim(), r)) { r << tl; break; }
            }
            if (r.isEmpty()) return false;
        }
        const int a = s.indexOf (t);
        out = s.substring (0, a) + r + s.substring (a + t.length());
        return true;
    }

    // known phrases inside a text built from pieces ("Variación de " + rol + " en " + tonalidad...)
    String phrasesIn (const String& s) const
    {
        if (allCaps (s)) return upperEs (phrasesIn (lowerEs (s), phrasesLower));
        return phrasesIn (s, phrases);
    }

    String phrasesIn (const String& s, const std::vector<std::pair<String, String>>& list) const
    {
        struct Seg { String text; bool done; };
        std::vector<Seg> segs { { s, false } };
        auto boundary = [] (const String& str, int i) { return i < 0 || i >= str.length() || ! CharacterFunctions::isLetterOrDigit (str[i]); };
        for (auto& [es, en] : list)
        {
            bool any = false;
            for (auto& sg : segs) if (! sg.done && sg.text.contains (es)) { any = true; break; }
            if (! any) continue;
            std::vector<Seg> next;
            for (auto& sg : segs)
            {
                if (sg.done) { next.push_back (sg); continue; }
                int from = 0;
                for (;;)
                {
                    const int i = sg.text.indexOf (from, es);
                    if (i < 0) { next.push_back ({ sg.text.substring (from), false }); break; }
                    if (! boundary (sg.text, i - 1) || ! boundary (sg.text, i + es.length()))
                    {
                        next.push_back ({ sg.text.substring (from, i + 1), false });
                        from = i + 1;
                        continue;
                    }
                    if (i > from) next.push_back ({ sg.text.substring (from, i), false });
                    next.push_back ({ en, true });
                    from = i + es.length();
                }
            }
            // glue the untranslated neighbours back together (a phrase may span what was split above)
            segs.clear();
            for (auto& n : next)
            {
                if (! segs.empty() && ! segs.back().done && ! n.done) segs.back().text << n.text;
                else segs.push_back (n);
            }
        }
        String out;
        for (auto& sg : segs) out << sg.text;
        return out;
    }

    String translate (const String& s) const
    {
        String out;
        if (trimmed (s, out)) return out;
        // numbers and units (meters, readouts): nothing to translate
        {
            int letters = 0;
            for (auto ch : withoutNumbers (s, nullptr).replace ("{n}", "")) if (CharacterFunctions::isLetter (ch)) ++letters;
            if (letters < 4) return s;
        }
        // sentence by sentence (and line by line)
        StringArray parts;
        String cur;
        for (int i = 0; i < s.length(); ++i)
        {
            const juce_wchar c = s[i];
            cur << String::charToString (c);
            const bool end = c == '\n' || ((c == '.' || c == '?' || c == '!' || c == ':' || c == ';') && (i + 1 >= s.length() || s[i + 1] == ' '));
            if (end) { parts.add (cur); cur.clear(); }
        }
        if (cur.isNotEmpty()) parts.add (cur);
        if (parts.size() > 1)
        {
            String joined;
            for (auto& p : parts)
            {
                String r;
                if (trimmed (p, r)) joined << r;
                else joined << phrasesIn (p);
            }
            return joined;
        }
        return s.length() >= 6 ? phrasesIn (s) : s;
    }
};

const Table& table() { static Table t; return t; }

SpinLock cacheLock;
std::map<String, String>& cache() { static std::map<String, String> c; return c; }
}

File settingsFile() { return folder().getChildFile ("idioma.txt"); }
String code (Lang l) { return l == Lang::en ? "en" : "es"; }

namespace
{
int readChoice()
{
    auto env = SystemStats::getEnvironmentVariable ("RACOON_LANG", {}).trim().toLowerCase();
    if (env.isEmpty())
    {
        lastModified = settingsFile().getLastModificationTime();
        env = settingsFile().loadFileAsString().trim().toLowerCase();
    }
    return env == "en" ? 1 : 0;
}

void repaintEverything()
{
    auto& d = Desktop::getInstance();
    for (int i = 0; i < d.getNumComponents(); ++i)
        if (auto* c = d.getComponent (i)) { c->repaint(); }
}
}

Lang current()
{
    int v = lang.load();
    if (v < 0)
    {
        v = readChoice();
        lang.store (v);
        lastCheck = Time::getMillisecondCounter();
    }
    else if (MessageManager::existsAndIsCurrentThread() && Time::getMillisecondCounter() - lastCheck > 1500
             && SystemStats::getEnvironmentVariable ("RACOON_LANG", {}).isEmpty())
    {
        // the language changed in Racoon or in another plugin: this window follows
        lastCheck = Time::getMillisecondCounter();
        if (settingsFile().getLastModificationTime() != lastModified)
        {
            const int now = readChoice();
            if (now != v)
            {
                lang.store (now);
                v = now;
                { const SpinLock::ScopedLockType sl (cacheLock); cache().clear(); }
                MessageManager::callAsync ([] { repaintEverything(); });
            }
        }
    }
    return v == 1 ? Lang::en : Lang::es;
}

bool chosen()
{
    return SystemStats::getEnvironmentVariable ("RACOON_LANG", {}).isNotEmpty() || settingsFile().existsAsFile();
}

void set (Lang l)
{
    folder().createDirectory();
    settingsFile().replaceWithText (code (l));
    lastModified = settingsFile().getLastModificationTime();
    lang.store (l == Lang::en ? 1 : 0);
    { const SpinLock::ScopedLockType sl (cacheLock); cache().clear(); }
    if (MessageManager::getInstanceWithoutCreating() != nullptr) MessageManager::callAsync ([] { repaintEverything(); });
}

namespace
{
std::atomic<bool> recording { false };
SpinLock recordLock;
StringArray& recorded() { static StringArray a; return a; }
void record (const String& shown)
{
    if (! recording.load() || ! looksSpanish (shown)) return;
    const SpinLock::ScopedLockType sl (recordLock);
    recorded().addIfNotAlreadyThere (shown);
}
}

void recordSpanish (bool on)
{
    const SpinLock::ScopedLockType sl (recordLock);
    if (on) recorded().clear();
    recording.store (on);
}

StringArray recordedSpanish()
{
    const SpinLock::ScopedLockType sl (recordLock);
    return recorded();
}

String tr (const String& s)
{
    if (current() == Lang::es || s.isEmpty()) return s;
    {
        const SpinLock::ScopedLockType sl (cacheLock);
        if (auto it = cache().find (s); it != cache().end()) { record (it->second); return it->second; }
    }
    const auto r = table().translate (s);
    record (r);
    const SpinLock::ScopedLockType sl (cacheLock);
    if (cache().size() > 20000) cache().clear();
    cache()[s] = r;
    return r;
}

bool looksSpanish (const String& s)
{
    if (s.containsAnyOf (String::fromUTF8 ("áéíóúñÁÉÍÓÚÑ¿¡"))) return true;
    static const StringArray words { "de", "la", "el", "los", "las", "para", "con", "una", "que", "del", "y", "en", "por", "sin", "clic", "compases", "pista", "canal" };
    auto tokens = StringArray::fromTokens (s.toLowerCase(), " ,.;:()[]/\n\t\"'", "");
    for (auto& w : tokens) if (words.contains (w)) return true;
    return false;
}

int dictionarySize() { return (int) (sizeof (dictionary) / sizeof (dictionary[0])); }
}

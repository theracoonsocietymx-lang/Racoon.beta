#pragma once
#include <juce_core/juce_core.h>

// The language of the interface (Spanish or English), shared by Racoon and the whole family.
// The code keeps its texts in Spanish: names, parameter names and anything the program compares stay as they are.
// What changes is what is shown: every text goes through tr() when it is drawn (labels, buttons, menus, tooltips,
// hints, windows), so switching the language repaints the interface in the other one.
// The choice lives in one small file that Racoon and every plugin read (Application Support/Racoon/idioma.txt);
// RACOON_LANG=en|es overrides it (the tests use that).
// The same files live in Racoon (Source/engine) and in the family (Shared): keep them equal.
namespace racoon::i18n
{
enum class Lang { es, en };

Lang current();
void set (Lang);                    // saves the choice for Racoon and the plugins, and switches right away
bool chosen();                      // the person already answered the first-launch question
juce::File settingsFile();
juce::String code (Lang);           // "es", "en"

// the text in the current language. Exact entries first, then the same sentence with other numbers
// ("Rola de 216 compases" -> "216-bar track"), then sentence by sentence, then the known phrases inside it
juce::String tr (const juce::String&);
inline juce::String tr (const char* utf8) { return tr (juce::String::fromUTF8 (utf8)); }

// for the tests: texts shown in English that still look Spanish (words with accents, ñ, ¿, ¡, or common Spanish words)
bool looksSpanish (const juce::String&);
int dictionarySize();
// for the tests: while on, every text shown in English that still looks Spanish is kept (once)
void recordSpanish (bool on);
juce::StringArray recordedSpanish();
}

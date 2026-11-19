#pragma once
#include <JuceHeader.h>

// Spanish text in the source is UTF-8: "patrón"_u builds a juce::String from it correctly.
inline juce::String operator""_u (const char* s, size_t n) { return juce::String::fromUTF8 (s, (int) n); }

#pragma once
#include "engine/Common.h"

namespace racoon
{
// toUpperCase() leaves accented letters alone under the C locale; this one also handles á é í ó ú ñ ü
inline juce::String upper (const juce::String& s)
{
    juce::String r;
    r.preallocateBytes (s.getNumBytesAsUTF8() + 4);
    for (auto p = s.getCharPointer(); ! p.isEmpty(); )
    {
        auto c = p.getAndAdvance();
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 0x20;
        r += juce::String::charToString (c);
    }
    return r;
}
}

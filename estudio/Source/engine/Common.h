#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace te = tracktion;
using namespace std::literals;
using namespace tracktion::literals;

namespace racoon
{
// UTF-8 literals for Spanish text: "Canción"_u
inline juce::String operator""_u (const char* s, size_t n) { return juce::String::fromUTF8 (s, (int) n); }

// kind of track, stored in the track state so it survives save / load
namespace TrackKind
{
    const juce::Identifier prop { "racoonKind" };
    constexpr int audio = 0, instrument = 1, returnBus = 2, group = 3, hamsterOut = 4;
}

inline int kindOf (const te::Track& t)
{
    if (dynamic_cast<const te::FolderTrack*> (&t) != nullptr) return TrackKind::group;
    return (int) t.state.getProperty (TrackKind::prop, TrackKind::audio);
}

inline bool isAudioTrack (const te::Track& t) { return dynamic_cast<const te::AudioTrack*> (&t) != nullptr; }
}

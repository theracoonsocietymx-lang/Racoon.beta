#pragma once
#include <JuceHeader.h>
#include "Utf8.h"

namespace be::ui
{
// Spanish help text for a parameter: what it does, in one or two sentences, plus the gestures.
juce::String helpFor (const juce::String& paramId, const juce::String& paramName);
}

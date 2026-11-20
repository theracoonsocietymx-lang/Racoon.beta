#include "BeaverProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::beaver::Processor(); }

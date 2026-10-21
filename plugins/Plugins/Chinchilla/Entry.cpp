#include "ChinchillaProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::chinchilla::Processor(); }

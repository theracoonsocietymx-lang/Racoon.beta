#include "HedgehogProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::hedgehog::Processor(); }

#include "JerboaProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::jerboa::Processor(); }

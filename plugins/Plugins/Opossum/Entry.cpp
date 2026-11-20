#include "OpossumProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::opossum::Processor(); }

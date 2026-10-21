#include "DormouseProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::dormouse::Processor(); }

#include "MarmotProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::marmot::Processor(); }

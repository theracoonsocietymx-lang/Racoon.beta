#include "SquirrelProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::squirrel::Processor(); }

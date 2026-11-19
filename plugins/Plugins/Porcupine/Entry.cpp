#include "PorcupineProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new racoon::porcupine::Processor(); }

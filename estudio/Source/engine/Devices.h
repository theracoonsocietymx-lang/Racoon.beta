#pragma once
#include "Common.h"

// Devices that are not a single plug-in: modulators (LFO, envelope follower, shape) that live on a track and move
// any parameter, and the effect rack (parallel chains of effects inside a Tracktion rack, with eight macros).
namespace racoon::devices
{
// ---------------------------------------------------------------- modulators
bool isModulatorId (const juce::String& nativeId);                 // "lfo", "seguidor-envolvente", "forma"
te::Modifier::Ptr addModulator (te::Track&, const juce::String& nativeId);
juce::String modulatorName (te::Modifier&);
juce::Array<te::AutomatableParameter*> modulatorKnobs (te::Modifier&);
juce::Array<te::AutomatableParameter*> targetsOf (te::Modifier&, te::Track&);   // parameters of the track it moves
float amountFor (te::Modifier&, te::AutomatableParameter& target);              // 0 when not mapped
void map (te::Modifier&, te::AutomatableParameter& target, float amount);        // amount -1..1 of the parameter range
void unmap (te::Modifier&, te::AutomatableParameter& target);
juce::Array<te::Modifier*> modulatorsOf (te::Track&);

// ---------------------------------------------------------------- effect rack
te::Plugin::Ptr createFxRack (te::Edit&);
bool isFxRack (const te::Plugin&);
te::RackType* rackOf (te::Plugin&);                                // the RackType behind a RackInstance
int numChains (te::RackType&);
juce::Array<te::Plugin*> chain (te::RackType&, int chainIndex);    // effects in signal order (without the chain volume)
te::Plugin* chainVolume (te::RackType&, int chainIndex);           // a volume / pan device at the end of each chain
void addChain (te::RackType&);
void removeChain (te::RackType&, int chainIndex);
te::Plugin::Ptr addToChain (te::RackType&, int chainIndex, te::Plugin::Ptr, int position = -1);
void moveInChain (te::RackType&, te::Plugin&, int newPosition);
void removeFromRack (te::RackType&, te::Plugin&);
void rebuildConnections (te::RackType&);
juce::Array<te::AutomatableParameter*> macros (te::Plugin& rackInstance);
void mapToMacro (te::AutomatableParameter& macro, te::AutomatableParameter& target, float amount);
void unmapFromMacro (te::AutomatableParameter& macro, te::AutomatableParameter& target);
juce::Array<te::AutomatableParameter*> mappedToMacro (te::AutomatableParameter& macro, te::Plugin& rackInstance);

// ---------------------------------------------------------------- external effect
// plays a short click train out of the effect's output, records what comes back on its input and sets the delay
// correction; the callback gets the measured milliseconds, or an explanation when it could not measure
void measureInsertLatency (te::Engine&, te::InsertPlugin&, std::function<void (bool ok, double ms, juce::String message)>);
// the analysis, for tests: where the clicks of `sent` show up in `received` (samples), -1 when they do not
int findDelay (const juce::AudioBuffer<float>& sent, const juce::AudioBuffer<float>& received, int maxDelay);

// every parameter a track can automate, including the effects inside its racks
juce::Array<te::AutomatableParameter*> allParams (te::Track&);
}

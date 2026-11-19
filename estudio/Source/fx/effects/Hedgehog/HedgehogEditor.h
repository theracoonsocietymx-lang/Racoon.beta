#pragma once
#include "fx/core/RacoonEditor.h"
#include "HedgehogProcessor.h"

namespace racoon::hedgehog
{
// screen: transfer curve on the left, gain reduction over time on the right, GR meter in between
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    Processor& proc;
    void timerCallback() override { repaint(); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

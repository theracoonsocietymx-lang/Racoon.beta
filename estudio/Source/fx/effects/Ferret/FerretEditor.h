#pragma once
#include "fx/core/RacoonEditor.h"
#include "FerretProcessor.h"

namespace racoon::ferret
{
// screen: the echoes on a time line (left and right), with their level after the feedback filters
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor& p) : proc (p) { startTimerHz (20); }
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

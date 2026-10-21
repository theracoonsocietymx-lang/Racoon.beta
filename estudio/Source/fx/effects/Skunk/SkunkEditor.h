#pragma once
#include "fx/core/RacoonEditor.h"
#include "SkunkProcessor.h"

namespace racoon::skunk
{
// screen: transfer curve of the chosen motor (with drive and bias) and a scope of what comes in and out
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

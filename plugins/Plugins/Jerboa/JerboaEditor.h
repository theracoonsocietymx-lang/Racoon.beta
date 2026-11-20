#pragma once
#include "RacoonEditor.h"
#include "JerboaProcessor.h"

namespace racoon::jerboa
{
// screen: the 24 pads of the bank (a click plays that pattern), the time lane and the volume lane of what plays,
// with the read point moving over them, and the speed
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    Processor& proc;
    juce::Rectangle<float> screen, pads, timeLane, volLane;
    juce::Rectangle<float> padRect (int k) const;
    void timerCallback() override { repaint (screen.toNearestInt()); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

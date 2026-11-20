#pragma once
#include "RacoonEditor.h"
#include "MarmotProcessor.h"

namespace racoon::marmot
{
// screen: the two oscillators (and the sub), the filter curve, the arpeggiator steps and the LFOs
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Processor& proc;
    juce::MidiKeyboardComponent keys;
    juce::Rectangle<float> screen;
    void timerCallback() override { repaint (screen.toNearestInt()); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

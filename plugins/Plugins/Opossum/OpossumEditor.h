#pragma once
#include "RacoonEditor.h"
#include "OpossumProcessor.h"

namespace racoon::opossum
{
// screen: the wiring of the six operators (who modulates whom; the heard ones at the bottom), how loud each one is
// right now, and the filter curve
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

#pragma once
#include "RacoonEditor.h"
#include "DormouseProcessor.h"

namespace racoon::dormouse
{
// screen: both oscillators' waves, the filter curve, the amp envelope, the LFOs and a playable keyboard
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

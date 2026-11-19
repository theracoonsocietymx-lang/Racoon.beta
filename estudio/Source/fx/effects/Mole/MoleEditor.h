#pragma once
#include "fx/core/RacoonEditor.h"
#include "MoleProcessor.h"

namespace racoon::mole
{
// screen: response curve that moves with the modulation, and the 16-step modulator you can draw on
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    Processor& proc;
    juce::Rectangle<float> curveArea, stepArea;
    int dragging = -1;
    void setStepFrom (juce::Point<float>);
    void timerCallback() override { repaint(); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

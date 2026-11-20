#pragma once
#include "RacoonEditor.h"
#include "OtterProcessor.h"

namespace racoon::otter
{
// screen: the instrument (the string moving, or the bar with its node lines) with where it is played and heard,
// the overtones of the last note, and the body's curve with the darkening on top
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    Processor& proc;
    juce::MidiKeyboardComponent keys;
    juce::Rectangle<float> screen, instrArea, modesArea, bodyArea, stringLine;
    int dragging = 0;     // 1 = where it is played, 2 = where it is heard
    void timerCallback() override { repaint (screen.toNearestInt()); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

#pragma once
#include "RacoonEditor.h"
#include "PorcupineProcessor.h"

namespace racoon::porcupine
{
// the screen: the stage with the sections in their places, the four lines of the harmony, the vowel and the syllables
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    Processor& proc;
    juce::Rectangle<int> stageArea, linesArea, vowelArea, sylArea;
    juce::Rectangle<float> sylCell (int step) const;
    void timerCallback() override { repaint(); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

#pragma once
#include "RacoonEditor.h"
#include "ChinchillaProcessor.h"

namespace racoon::chinchilla
{
// the screen, five pages: tuning (pitch over time), the note editor, harmonies, the modulation matrix, chopper and analyzer
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    ~Display() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void showPage (int);
    int page = 0;
private:
    Processor& proc;
    juce::OwnedArray<juce::TextButton> tabs, buttons;
    juce::OwnedArray<ParamControl> controls;
    juce::Rectangle<int> graphArea, controlsArea, stepsArea;
    int voice = 0, dragSeg = -1, dragStartOffset = 0;
    float viewCentre = 60.0f;
    juce::File lastExport;
    void rebuild();
    void timerCallback() override { proc.updateSpectrum(); repaint(); }
    juce::Rectangle<float> stepCell (int) const;
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

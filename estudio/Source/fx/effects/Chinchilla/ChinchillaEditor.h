#pragma once
#include "fx/core/RacoonEditor.h"
#include "ChinchillaProcessor.h"

namespace racoon::chinchilla
{
// screen: how the space decays (predelay, early reflections, the tail in lows / mids / highs), with tail controls
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Processor& proc;
    juce::TextButton clearButton { "VACIAR COLA" }, irButton { "CARGAR IR..." };
    std::shared_ptr<juce::FileChooser> chooser;
    void timerCallback() override { repaint(); irButton.setVisible ((int) proc.value ("algo") == 1); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

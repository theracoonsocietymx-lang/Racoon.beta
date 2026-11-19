#pragma once
#include "RacoonEditor.h"
#include "ChipmunkProcessor.h"

namespace racoon::chipmunk
{
// screen: the chord that sounds (name and keys), the 16-step table of the arpeggiator, chord memory and MIDI learn
class Display : public juce::Component, private juce::Timer, private juce::AudioProcessorParameter::Listener
{
public:
    explicit Display (Processor&);
    ~Display() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    Processor& proc;
    juce::TextButton learnChordButton { "APRENDER ACORDE" }, midiLearnButton { "MIDI LEARN" }, clearLearnButton { "BORRAR MIDI LEARN" },
                     panicButton { "PÁNICO"_u };
    juce::Rectangle<float> keysArea, gridArea, infoArea;
    int dragStep = -1;
    juce::String learnedName;
    void timerCallback() override { repaint(); midiLearnButton.setToggleState (proc.learnParam.load() >= 0 || armed, juce::dontSendNotification);
                                    learnChordButton.setToggleState (proc.learnChord.load(), juce::dontSendNotification); }
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    std::atomic<bool> armed { false };
    int stepAt (juce::Point<float>) const;
    void setStep (int k, const char* what, float value);
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

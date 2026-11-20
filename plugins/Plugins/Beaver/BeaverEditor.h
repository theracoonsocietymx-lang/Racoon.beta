#pragma once
#include "RacoonEditor.h"
#include "BeaverProcessor.h"

namespace racoon::beaver
{
// screen: the partials of the last note (log frequency across, dB up) with the filter on top, the drawn bars when a
// layer is Dibujo, and the material panel (its spectrogram or picture, its name, and the buttons to load audio or a
// picture into layer A or B). Audio and pictures can also be dropped on it.
class Display : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
private:
    Processor& proc;
    juce::MidiKeyboardComponent keys;
    juce::TextButton loadAudio, loadImage, layerA, layerB;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> screen, specArea, matArea, thumbArea;
    juce::Image thumb;
    int thumbVersion = -1, thumbLayer = -1;
    int target = 0;            // the layer that receives a file
    int drawLayer() const;     // the layer being drawn on screen (-1: none)
    void drawAt (juce::Point<float>, bool reset);
    void choose (bool picture);
    void timerCallback() override;
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

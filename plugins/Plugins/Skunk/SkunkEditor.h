#pragma once
#include "RacoonEditor.h"
#include "SkunkProcessor.h"

namespace racoon::skunk
{
// XY pad: two parameters moved together; the host records the movement as automation
class XYPad : public juce::Component, private juce::Timer
{
public:
    explicit XYPad (Processor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    Processor& proc;
    juce::RangedAudioParameter* px = nullptr, * py = nullptr;
    void set (juce::Point<float>);
    void timerCallback() override { repaint(); }
};

// one route of the matrix in a single line: source, destination, amount, curve and smoothing
class RouteRow : public juce::Component
{
public:
    RouteRow (Processor&, int route);
    void resized() override;
    void paint (juce::Graphics&) override;
    const int route;
private:
    Processor& proc;
    racoon::ui::PopupCombo src, dst;
    racoon::ui::GrabSlider depth, curve, smooth;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aSrc, aDst;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aDepth, aCurve, aSmooth;
};

// the screen, with four pages: the sound (capture, grains, spectrum, XY), the matrix, the modulators and the sequences
class Display : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Display (Processor&);
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void showPage (int);
    int page = 0;
private:
    Processor& proc;
    juce::OwnedArray<juce::TextButton> tabs;
    XYPad xy;
    juce::ComboBox library;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aLibrary;
    juce::Rectangle<int> libraryCaption;
    juce::TextButton recapture { "GRABAR LO QUE ENTRA" }, toCapture { "SALIDA A LA CAPTURA" }, saveWav { "GUARDAR WAV (ARRASTRA)" }, loadButton { "CARGAR..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::OwnedArray<ParamControl> pageControls;
    juce::OwnedArray<RouteRow> routeRows;
    juce::Array<juce::Rectangle<int>> panels;
    juce::StringArray panelTitles;
    juce::OwnedArray<juce::TextButton> pickers;   // route pages, LFO / envelope / sequence numbers
    int routePage = 0, lfoPick = 0, envPick = 0, seqPick = 0;
    juce::Rectangle<int> waveArea, specArea, seqArea, statusArea;
    juce::File lastWav;
    void rebuildPage();
    void timerCallback() override { repaint(); }
    void setSeqStep (juce::Point<int>);
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

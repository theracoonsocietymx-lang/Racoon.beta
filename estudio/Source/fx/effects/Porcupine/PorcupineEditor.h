#pragma once
#include "fx/core/RacoonEditor.h"
#include "PorcupineProcessor.h"

namespace racoon::porcupine
{
// screen: analyzer (before / after), the EQ curve and one handle per band you can drag
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
    int selected = 2;

private:
    Processor& proc;
    std::vector<float> preDb, postDb;
    int dragBand = -1, hover = -1;
    juce::Rectangle<float> plot() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    juce::Point<float> handle (int b) const;
    int hitBand (juce::Point<float>) const;
    void setParam (int band, const char* what, float value);
    void showBandMenu (int band);
    void timerCallback() override;
    void computeSpectrum (const std::vector<float>& fifo, std::vector<float>& outDb, int order);
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
    bool showEssential (const SectionCard&) const override;
private:
    Display* disp = nullptr;
};
}

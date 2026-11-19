#pragma once
#include "RacoonEditor.h"
#include "HedgehogProcessor.h"

namespace racoon::hedgehog
{
// the screen: the Partícula (first), then meters, tonal balance, dynamic EQ, multiband, and the assistant with quality control
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void showPage (int);
    static constexpr int particlePage = 5;      // shown first
    int page = particlePage;
private:
    Processor& proc;
    juce::OwnedArray<juce::TextButton> tabs, buttons;
    juce::OwnedArray<ParamControl> controls;
    juce::Rectangle<int> content, controlsArea, graphArea, sideArea;
    // Partícula page
    juce::Rectangle<int> paCols[6], paInMeter, paOutMeter, paInPeak, paOutPeak, paGrMeters, paLimText;
    juce::Array<int> tabPages;
    void layoutParticle();
    void paintParticle (juce::Graphics&);
    int eqBand = 0, mbBand = 0, dragBand = -1;
    std::vector<float> history;            // short-term loudness, one point every 100 ms
    juce::StringArray assistantLog;
    std::unique_ptr<juce::FileChooser> chooser;
    void rebuild();
    void timerCallback() override;
    float xToHz (float x) const;
    float hzToX (float hz) const;
    float dbToY (float db, float range) const;
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

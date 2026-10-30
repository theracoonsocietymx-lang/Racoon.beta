#pragma once
#include "ViewState.h"

namespace racoon::ui
{
enum class Glyph { none, play, stop, record, loop, metronome, toStart, undo, redo, plus, close, folder, save, gear, scissors, keyboard, mixer, wave, follow, back, forward, brush };

void drawGlyph (juce::Graphics&, Glyph, juce::Rectangle<float>, juce::Colour);

// square brand button with a drawn glyph and optional text; toggle colour is configurable
class GlyphButton : public juce::Button
{
public:
    GlyphButton (Glyph g, const juce::String& text = {}, juce::Colour onColour = Brand::rosa);
    void setGlyph (Glyph g) { glyph = g; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour onColour;
    bool blink = false;
private:
    Glyph glyph;
};

// text button with the brand box look and a help text
juce::TextButton* makeButton (juce::OwnedArray<juce::Component>&, juce::Component& parent, const juce::String& text, const juce::String& help);

// vertical stereo meter reading a te::LevelMeasurer
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter() { startTimerHz (30); setInterceptsMouseClicks (false, false); }
    ~LevelMeter() override { attach (nullptr); }
    void attach (te::LevelMeasurer*, te::Plugin::Ptr keepAlive = {});
    void forget() { measurer = nullptr; owner = nullptr; }   // the measurer is already gone
    void paint (juce::Graphics&) override;
    // the loudest the channel has been since the last reset (what the number above the fader shows)
    float getMaxDb() const { return maxDb; }
    void resetMax() { maxDb = -100.0f; }
    // where a level sits on the meter (the scale beside it uses the same mapping)
    static float yFor (float db, juce::Rectangle<float> area)
    {
        auto inner = area.reduced (2.0f);
        return inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    }
private:
    void timerCallback() override;
    te::LevelMeasurer* measurer = nullptr;
    te::Plugin::Ptr owner;
    te::LevelMeasurer::Client client;
    float level[2] { -100.0f, -100.0f }, peak[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int peakHold[2] {};
};

// decibel fader as used in the mixer
class Fader : public GrabSlider
{
public:
    Fader();
    juce::String getTextFromValue (double v) override;
    bool hitTest (int, int) override { return true; }
};

// small knob with a caption under it
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, const juce::String& help);
    GrabSlider slider;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::String caption;
};

// a label in the brand font on a surface
void drawCaption (juce::Graphics&, const juce::String&, juce::Rectangle<int>, float size = 12.0f,
                  juce::Colour c = S::ink, juce::Justification j = juce::Justification::centredLeft);

// app settings (Application Support/Racoon/Racoon.settings)
juce::PropertiesFile& prefs();

float volumeFaderToDb (float pos);
float dbToVolumeFader (float db);
}

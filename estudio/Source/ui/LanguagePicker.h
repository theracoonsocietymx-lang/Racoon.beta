#pragma once
#include "Theme.h"

// The very first question, asked in both languages: which language Racoon (and its plugins) should speak.
namespace racoon::ui
{
class LanguagePicker : public juce::Component
{
public:
    LanguagePicker();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override { return true; }
    void mouseDown (const juce::MouseEvent&) override {}
    std::function<void (bool english)> onPick;
    juce::TextButton spanish { juce::String::fromUTF8 ("Español") }, english { "English" };
private:
    juce::Rectangle<int> card;
};
}

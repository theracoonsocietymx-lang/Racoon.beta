#pragma once
#include "Theme.h"
#include "engine/Genres.h"

// "¿Qué vamos a producir?": the first thing Racoon asks. Each genre starts a 16-bar block already made in that style.
namespace racoon::ui
{
class GenrePicker : public juce::Component
{
public:
    GenrePicker();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}         // clicks around the card don't reach the song
    std::function<void (const juce::String& genreId)> onPick;
    std::function<void()> onEmpty, onOpenLast, onClose;
    void setBusy (const juce::String& genreName);                 // "Armando tu bloque de ..."
    juce::Button* buttonFor (const juce::String& id);             // for the tests
private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::StringArray ids, families;
    juce::TextButton empty, last, surprise, close { "X" };
    juce::Rectangle<int> card;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> captions;
    juce::String busy;
};
}

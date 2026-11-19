#pragma once
#include "Theme.h"

// ARMONIZAR: the Camelot wheel over the song. Outside the major keys (B), inside the minor ones (A), each with its
// number and its name. A click on a key moves the whole song there right away (while it plays); another click tries
// another. "Armonizar en esta tonalidad" keeps it; Cancelar (or Esc, or closing) leaves the song as it was.
namespace racoon::ui
{
class HarmonizePanel : public juce::Component
{
public:
    HarmonizePanel (int currentRoot, bool currentMajor, bool known);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverPos = -1; repaint(); }
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (int root, bool major)> onTry;      // a key was touched: hear it
    std::function<void (int root, bool major)> onKeep;     // Armonizar en esta tonalidad
    std::function<void()> onCancel;

    // the wheel: position 0..11 is Camelot number 1..12; ring 0 = A (minor), 1 = B (major)
    static int rootOf (int number, bool major);             // number 1..12 -> pitch class
    static int numberOf (int root, bool major);             // pitch class -> 1..12
    void tryKey (int root, bool major);                     // also for tests
    int chosenRoot() const { return chosenRootPc; }
    bool chosenMajor() const { return chosenIsMajor; }
    bool hasChoice() const { return chosen; }
    juce::Point<float> centreOf (int root, bool major) const;   // for tests
    juce::TextButton keep, cancel;

private:
    int currentRootPc = 0;
    bool currentIsMajor = false, currentKnown = false;
    int chosenRootPc = 0;
    bool chosenIsMajor = false, chosen = false;
    int hoverPos = -1;
    juce::Rectangle<int> card;
    juce::Point<float> centre;
    float outerR = 0.0f, midR = 0.0f, innerR = 0.0f;
    int hitTest (juce::Point<float>) const;                 // ring * 12 + position, or -1
    juce::Path segment (int ring, int pos) const;
};
}

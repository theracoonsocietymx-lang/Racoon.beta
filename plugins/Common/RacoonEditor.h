#pragma once
#include "RacoonProcessor.h"
#include "Theme.h"
#include "ProLook.h"
#include "Dock.h"
#include "../Shared/AutomationPanel.h"

namespace racoon
{
// What each plugin declares: sections of controls. Essential sections are always visible;
// advanced ones live on the second layer (AVANZADO), one tab per section.
struct Ctl { juce::String id; juce::String label; juce::String help; };
struct Sec { juce::String title; std::vector<Ctl> controls; bool advanced = false; };

struct EditorSpec
{
    std::vector<Sec> sections;
    int width = 940;
    int easyHeight = 600;          // window height in PRINCIPIANTE
    int advancedHeight = 200;      // extra height for the second layer in AVANZADO
    int displayEasy = 270;         // height of the screen (display) in PRINCIPIANTE
    int displayFull = 200;
    juce::StringArray steps;       // "Empieza aquí": what to do first, in order
    int displayMin = 0;            // the screen never gets shorter than this on a small monitor (0: 65 % of its height)
};

// one control bound to a parameter (knob, menu or switch, picked from the parameter type)
class ParamControl : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ParamControl (RacoonProcessor&, const Ctl&);
    ~ParamControl() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    int preferredWidth() const;
    juce::String helpLine() const;
    void mouseDown (const juce::MouseEvent&) override;
    const Ctl spec;

private:
    RacoonProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<ui::GrabSlider> knob;
    std::unique_ptr<ui::PopupCombo> combo;
    std::unique_ptr<ui::PopupTextButton> toggle;
    juce::Label valueLabel;
    bool automated = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    void timerCallback() override;
};

// card with a title sticker and a row (or grid) of controls
class SectionCard : public juce::Component
{
public:
    SectionCard (RacoonProcessor&, const Sec&);
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredWidth (int rows) const;
    juce::OwnedArray<ParamControl> controls;
    juce::String title;
    juce::Colour mark { 0xffe0789a };   // the card's brand colour (Rosa, Menta, Limón in turn)
};

class RacoonEditor : public juce::AudioProcessorEditor, public AutomationOpener, private juce::Timer
{
public:
    RacoonEditor (RacoonProcessor&, EditorSpec, std::unique_ptr<juce::Component> display);
    ~RacoonEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void setEasy (bool);
    void mouseDown (const juce::MouseEvent&) override;     // every click starts a new undo step
    // Cmd+Z / Cmd+Shift+Z undo and redo (knobs and notes); Cmd+C, Cmd+X, Cmd+V, Cmd+D, Cmd+A go to the grid or the
    // sequence on screen even when it does not have the focus
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;          // A S D F... play the keyboard below
    SequencerDock* getDock() const { return dock.get(); }
    bool isEasy() const { return easy; }
    juce::Component* getDisplay() const { return display.get(); }
    virtual bool showEssential (const SectionCard&) const { return true; }   // plugins can hide cards (e.g. other EQ bands)

    static juce::Image badge (const juce::String& animal);

    // automation: right click on any control > Ver automatización opens the strip above the Dock
    Automation& automation() override { return proc.automation; }
    void openAutomation (const juce::String& paramId, bool record) override;
    AutomationPanel* getAutomationPanel() const { return autoPanel.get(); }
    static juce::PropertiesFile& settings();

protected:
    RacoonProcessor& proc;
    ui::ProLook look;
    EditorSpec spec;
    std::unique_ptr<juce::Component> display;
    juce::Viewport bodyView;
    juce::Component body;                                   // the screen and the cards: scrolls if the window is short
    std::unique_ptr<SequencerDock> dock;
    std::unique_ptr<AutomationPanel> autoPanel;
    juce::OwnedArray<SectionCard> essential, advanced;
    juce::OwnedArray<juce::TextButton> advTabs;
    int advIndex = 0;
    bool easy = true;
    bool focusGrabbed = false;

    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "GUARDAR" },
                     aButton { "A" }, bButton { "B" }, copyButton { "A = B" },
                     easyButton { juce::String::fromUTF8 ("PRINCIPIANTE") }, fullButton { "AVANZADO" };
    juce::String hint;
    juce::Rectangle<int> brandArea, footer;
    juce::TooltipWindow tooltips { this, 800 };

    void timerCallback() override;
    void layoutBody();
    void showPresetMenu();
    void refreshPresetName();
    void stepPreset (int delta);
};

// the dark "screen" used by every display: Ciruela glass with a brand border
void drawScreen (juce::Graphics&, juce::Rectangle<float>);
void drawScreenGrid (juce::Graphics&, juce::Rectangle<float>, int vLines, int hLines);
}

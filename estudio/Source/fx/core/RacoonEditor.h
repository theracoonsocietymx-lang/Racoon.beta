#pragma once
#include "RacoonProcessor.h"
#include "ui/Theme.h"
#include "ui/ProLook.h"

namespace racoon
{
// What each plugin declares: sections of controls. Essential sections are always visible;
// advanced ones live on the second layer (AVANZADO), one tab per section.
struct Ctl { juce::String id; juce::String label; juce::String help; };
struct Sec { juce::String title; std::vector<Ctl> controls; bool advanced = false; juce::Colour mark {}; };   // mark: the card's colour (empty: Rosa, Menta, Limón in turn)

struct EditorSpec
{
    std::vector<Sec> sections;
    int width = 940;
    int easyHeight = 600;          // window height in PRINCIPIANTE
    int advancedHeight = 200;      // extra height for the second layer in AVANZADO
    int displayEasy = 270;         // height of the screen (display) in PRINCIPIANTE
    int displayFull = 200;
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
    const Ctl spec;

private:
    RacoonProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::Slider> knob;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::TextButton> toggle;
    juce::Label valueLabel;
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

class RacoonEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    RacoonEditor (RacoonProcessor&, EditorSpec, std::unique_ptr<juce::Component> display);
    ~RacoonEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void setEasy (bool);
    bool isEasy() const { return easy; }
    juce::Component* getDisplay() const { return display.get(); }
    virtual bool showEssential (const SectionCard&) const { return true; }   // plugins can hide cards (e.g. other EQ bands)

    static juce::Image badge (const juce::String& animal);
    static juce::PropertiesFile& settings();

protected:
    RacoonProcessor& proc;
    ui::ProLook look;
    EditorSpec spec;
    std::unique_ptr<juce::Component> display;
    juce::OwnedArray<SectionCard> essential, advanced;
    juce::OwnedArray<juce::TextButton> advTabs;
    int advIndex = 0;
    bool easy = true;

    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "GUARDAR" },
                     aButton { "A" }, bButton { "B" }, copyButton { "A = B" },
                     easyButton { juce::String::fromUTF8 ("PRINCIPIANTE") }, fullButton { "AVANZADO" };
    juce::String hint;
    juce::Rectangle<int> brandArea, footer;
    juce::TooltipWindow tooltips { this, 800 };

    void timerCallback() override;
    void showPresetMenu();
    void refreshPresetName();
    void stepPreset (int delta);
};

// the dark "screen" used by every display: Ciruela glass with a brand border
void drawScreen (juce::Graphics&, juce::Rectangle<float>);
void drawScreenGrid (juce::Graphics&, juce::Rectangle<float>, int vLines, int hLines);
}

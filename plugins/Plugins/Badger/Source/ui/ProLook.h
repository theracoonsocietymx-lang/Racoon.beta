#pragma once
#include "Theme.h"

// The working look of Racoon 2026 ("pro"): the brand stays (Ciruela, Limón, Rosa, Menta, Hielo, the mascot and
// Dela Gothic for names), but the surface is dark, the brand colours mean something (Limón = value / output,
// Hielo = input signal, Rosa = gain reduction or warning, Menta = a second band or source), every number is in a
// monospaced face with its unit, and borders are 1 px hairlines. Thick outlines are kept for the window frame only.
namespace be::ui
{
namespace Pro
{
    const juce::Colour win       { 0xff141113 };   // window
    const juce::Colour head      { 0xff1a1618 };   // header and footer bands
    const juce::Colour panel     { 0xff1f1a1c };   // cards (Ciruela)
    const juce::Colour raised    { 0xff2a2326 };   // knob bodies, buttons
    const juce::Colour screen    { 0xff0e0b0d };   // displays
    const juce::Colour grid      { 0xff262023 };   // grid lines on displays
    const juce::Colour line      { 0xff3a3236 };   // hairlines
    const juce::Colour lineHi    { 0xff5a5055 };   // hover
    const juce::Colour text      { 0xfff2eeef };
    const juce::Colour textDim   { 0xffa39a9e };
    const juce::Colour textFaint { 0xff6e6569 };
    // signal roles
    const juce::Colour value     = Brand::limon;   // values, output, the thing you set
    const juce::Colour input     = Brand::hielo;   // what comes in
    const juce::Colour reduce    = Brand::rosa;    // gain reduction, clipping, warnings
    const juce::Colour second    = Brand::menta;   // a second band / source
}

// numbers: JetBrains Mono (OFL), tabular, so readouts do not jump while they change
struct NumFont
{
    static juce::Font medium (float h);
    static juce::Font semi (float h);
};

// "-3.2 dB", "1.84 kHz", "12 ms": the value with its unit, in a fixed format
juce::String fmtDb (float db, int decimals = 1);
juce::String fmtHz (float hz);

class ProLook : public AncientLook
{
public:
    ProLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked, bool hasSub,
                            const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};

namespace ProDraw
{
    // a card: panel fill, 1 px hairline, the title as a small caps label with a brand square
    void card (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour mark = Brand::rosa);
    // the dark screen of a display with its hairline
    void screen (juce::Graphics&, juce::Rectangle<float>);
    // a vertical meter with a dB scale and the number on top: level in dB, -60..+6 (or 0..range for reduction)
    void meter (juce::Graphics&, juce::Rectangle<float>, float db, juce::Colour, const juce::String& label, bool reduction = false, float range = 24.0f);
    // small caps label
    void caps (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, juce::Colour = Pro::textDim, float size = 10.5f);
}
}

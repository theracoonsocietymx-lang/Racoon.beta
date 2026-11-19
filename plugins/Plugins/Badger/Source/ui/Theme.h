#pragma once
#include <JuceHeader.h>
#include "Utf8.h"

namespace be::ui
{
// ------------------------------------------------------------------ Racoon brand (Bandido de Callejón)
// Rosa Limonada #E0789A, Limón #D9C452, Menta #7FBFA8, Blanco Leche #F7F5F2, Ciruela #1F1A1C, Hielo #5CF2E8
namespace Brand
{
    const juce::Colour rosa    { 0xffe0789a };
    const juce::Colour limon   { 0xffd9c452 };
    const juce::Colour menta   { 0xff7fbfa8 };
    const juce::Colour leche   { 0xfff7f5f2 };
    const juce::Colour ciruela { 0xff1f1a1c };
    const juce::Colour hielo   { 0xff5cf2e8 };
}

// ------------------------------------------------------------------ screen palette
// Displays, scopes, the piano roll and other "screens" are Ciruela glass with Limón traces and Hielo signals.
// The names come from the first version of the interface and are kept so every screen keeps working.
namespace C
{
    const juce::Colour void_      { 0xff1f1a1c };   // screen background (Ciruela)
    const juce::Colour basalt     { 0xff262023 };
    const juce::Colour stone      { 0xff2e272a };
    const juce::Colour stoneHi    { 0xff3a3236 };
    const juce::Colour ridge      { 0xff4a4146 };   // grid lines on screens
    const juce::Colour engrave    { 0xff141012 };
    const juce::Colour amber      { 0xffd9c452 };   // main trace (Limón)
    const juce::Colour amberHot   { 0xfff0dc6a };
    const juce::Colour amberDim   { 0xff7d7031 };
    const juce::Colour cyan       { 0xff5cf2e8 };   // signal (Hielo)
    const juce::Colour cyanDim    { 0xff1f6b67 };
    const juce::Colour jade       { 0xff7fbfa8 };   // Menta
    const juce::Colour ochre      { 0xffe0789a };   // Rosa
    const juce::Colour bone       { 0xfff7f5f2 };   // text on screens (Blanco Leche)
    const juce::Colour boneDim    { 0xffb9b1b4 };
    const juce::Colour ash        { 0xff6b6166 };
}

// ------------------------------------------------------------------ surface palette (panels, buttons, labels)
struct SurfaceTheme
{
    juce::Colour bg     = Brand::rosa;      // window colour, changes per plugin (Badger rosa, Hamster menta)
    juce::Colour accent = Brand::rosa;      // chips, selected marks
};
extern SurfaceTheme gSurface;
namespace S
{
    inline juce::Colour bg()       { return gSurface.bg; }
    inline juce::Colour accent()   { return gSurface.accent; }
    // the 2026 look: cards are dark (Ciruela) and "ink" is the light text on them. The brand colours stay for
    // the header, stickers and signals. (Before: cream cards with Ciruela ink.)
    const juce::Colour card       { 0xff1f1a1c };
    const juce::Colour cardDim    { 0xff2a2326 };
    const juce::Colour ink        { 0xfff2eeef };
    const juce::Colour inkSoft    { 0xffa39a9e };
    const juce::Colour inkFaint   { 0xff6e6569 };
    const juce::Colour shade      { 0xff141113 };   // the dark that used to be "ink" when it filled (shadows, black keys)
    const juce::Colour knob       { 0xffd9c452 };
    const juce::Colour hi         { 0xffd9c452 };   // selected / on state (Limón)
}

// ------------------------------------------------------------------ fonts
struct Fonts
{
    static juce::Font title (float h);    // Dela Gothic One from 20 px (logotype), Golos SemiBold below
    static juce::Font label (float h);    // Golos Text Medium (labels, buttons, tabs)
    static juce::Font body (float h);     // Golos Text (reading text, menus)
    static juce::Font bodyBold (float h); // Golos Text SemiBold
    static juce::Font mono (float h);     // Alumni Sans SemiBold (numbers and readouts)
};

// button and tab text in reading case ("+ EFECTO" is drawn "+ Efecto")
juce::String friendlyCase (const juce::String&);

// ------------------------------------------------------------------ drawing helpers
namespace Draw
{
    // flat card with a Ciruela border, a hard shadow and the title as a sticker chip
    void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {}, bool lit = false);
    // label text on a surface (light colours are turned into ink so text is always readable)
    void engraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Font f,
                   juce::Justification j = juce::Justification::centred, juce::Colour col = S::inkSoft);
    // lines on screens (flat, with a faint halo)
    void glowLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float width = 1.5f);
    void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width = 1.6f);
    // small square decoration (kept for older call sites)
    void glyph (juce::Graphics& g, juce::Rectangle<float> r, int seed, juce::Colour c, float thickness = 1.2f);
    // window background
    juce::Image background (int w, int h);
    // brutalist box: fill + border + hard shadow, drawn inside r
    void box (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float border = 2.0f, float shadow = 3.0f, bool pressed = false);
    // the Racoon mask (antifaz) used in the header
    void racoonMark (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, juce::Colour mask, juce::Colour eyes);
}

// ------------------------------------------------------------------ look and feel
class AncientLook : public juce::LookAndFeel_V4
{
public:
    AncientLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked, bool hasSub,
                            const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    // the interface language: what labels, alerts and menus show goes through i18n::tr
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::AlertWindow* createAlertWindow (const juce::String& title, const juce::String& message, const juce::String& button1,
                                          const juce::String& button2, const juce::String& button3, juce::MessageBoxIconType,
                                          int numButtons, juce::Component* associatedComponent) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
};

// ------------------------------------------------------------------ sliders you have to grab
// A fader or knob only reacts when the pointer is on the cap or the knob itself, never on the empty
// track around it, and grabbing it never makes it jump to the click. The value box, if any, still works.
inline bool grabHit (juce::Slider& s, int x, int y)
{
    auto* lf = dynamic_cast<juce::Slider::LookAndFeelMethods*> (&s.getLookAndFeel());
    if (lf == nullptr) return true;
    const auto layout = lf->getSliderLayout (s);
    if (layout.textBoxBounds.contains (x, y)) return true;
    const auto st = s.getSliderStyle();
    const auto sb = layout.sliderBounds.toFloat();
    if (s.isRotary())
    {
        const float r = std::min (sb.getWidth(), sb.getHeight()) * 0.5f + 1.0f;
        return sb.getCentre().getDistanceFrom ({ (float) x, (float) y }) <= r;
    }
    if (st == juce::Slider::LinearVertical)
    {
        const float pos = s.getPositionOfValue (s.getValue());
        const float halfW = std::min (sb.getWidth(), 26.0f) * 0.5f + 3.0f;
        return std::abs ((float) y - pos) <= 9.0f && std::abs ((float) x - sb.getCentreX()) <= halfW;
    }
    if (st == juce::Slider::LinearHorizontal)
    {
        const float pos = s.getPositionOfValue (s.getValue());
        const float halfH = std::min (sb.getHeight(), 18.0f) * 0.5f + 3.0f;
        return std::abs ((float) x - pos) <= 8.0f && std::abs ((float) y - sb.getCentreY()) <= halfH;
    }
    return true; // value bars and anything else keep the whole box
}

class GrabSlider : public juce::Slider
{
public:
    GrabSlider() { setSliderSnapsToMousePosition (false); }
    GrabSlider (juce::Slider::SliderStyle st, juce::Slider::TextEntryBoxPosition tb) : juce::Slider (st, tb) { setSliderSnapsToMousePosition (false); }
    bool hitTest (int x, int y) override { return grabHit (*this, x, y); }
};
} // namespace be::ui

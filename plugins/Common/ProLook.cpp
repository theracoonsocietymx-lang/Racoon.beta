#include "ProLook.h"
#include "I18n.h"
#include "Utf8.h"
#include <BinaryData.h>

namespace racoon::ui
{
using namespace juce;

namespace
{
Typeface::Ptr load (const char* data, int size) { return Typeface::createSystemTypefaceFor (data, (size_t) size); }
Typeface::Ptr tfMonoMed()  { static auto t = load (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize); return t; }
Typeface::Ptr tfMonoSemi() { static auto t = load (BinaryData::JetBrainsMonoSemiBold_ttf, BinaryData::JetBrainsMonoSemiBold_ttfSize); return t; }
}

Font NumFont::medium (float h) { return Font (FontOptions (tfMonoMed()).withHeight (h)); }
Font NumFont::semi (float h)   { return Font (FontOptions (tfMonoSemi()).withHeight (h)); }

String fmtDb (float db, int decimals)
{
    if (db <= -99.0f) return String::fromUTF8 ("-\xe2\x88\x9e dB");   // -inf
    return (db > 0.05f ? "+" : "") + String (db, decimals) + " dB";
}

String fmtHz (float hz)
{
    if (hz >= 1000.0f) return String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return String (roundToInt (hz)) + " Hz";
}

// ===================================================================== look
ProLook::ProLook()
{
    setColour (PopupMenu::backgroundColourId, Pro::panel);
    setColour (PopupMenu::textColourId, Pro::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Pro::value.withAlpha (0.16f));
    setColour (PopupMenu::highlightedTextColourId, Pro::value);
    setColour (ComboBox::textColourId, Pro::text);
    setColour (ComboBox::backgroundColourId, Pro::raised);
    setColour (ComboBox::outlineColourId, Pro::line);
    setColour (ComboBox::arrowColourId, Pro::textDim);
    setColour (TextButton::textColourOffId, Pro::text);
    setColour (TextButton::textColourOnId, Pro::value);
    setColour (Label::textColourId, Pro::text);
    setColour (Label::textWhenEditingColourId, Pro::text);
    setColour (TextEditor::textColourId, Pro::text);
    setColour (TextEditor::backgroundColourId, Pro::screen);
    setColour (TextEditor::highlightColourId, Pro::value.withAlpha (0.3f));
    setColour (TextEditor::outlineColourId, Pro::line);
    setColour (TextEditor::focusedOutlineColourId, Pro::value);
    setColour (CaretComponent::caretColourId, Pro::value);
    setColour (TooltipWindow::textColourId, Pro::text);
    setColour (AlertWindow::backgroundColourId, Pro::panel);
    setColour (AlertWindow::textColourId, Pro::text);
    setColour (ScrollBar::thumbColourId, Pro::lineHi);
}

void ProLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto area = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rArc = size * 0.5f - 3.0f;
    const float rBody = rArc - 6.0f;
    const bool over = s.isMouseOverOrDragging();
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float angle = start + pos * (end - start);

    // track and value arcs
    Path track;
    track.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, start, end, true);
    g.setColour (Pro::line);
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::butt));
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        Path val;
        val.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (over ? Pro::value.brighter (0.15f) : Pro::value);
        g.strokePath (val, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::butt));
    }
    // body: flat, with a hairline
    g.setColour (Pro::raised);
    g.fillEllipse (Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (c));
    g.setColour (over ? Pro::lineHi : Pro::line);
    g.drawEllipse (Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (c), 1.0f);
    // pointer
    const Point<float> a = c.getPointOnCircumference (rBody * 0.25f, angle), b = c.getPointOnCircumference (rBody * 0.92f, angle);
    g.setColour (Pro::text);
    g.drawLine ({ a, b }, 2.0f);
}

void ProLook::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if ((bool) b.getProperties().getWithDefault ("onBrand", false))
    {
        // buttons on the Limón header: Ciruela blocks, Rosa when on, the brand's hard shadow
        auto body = r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
        if (down) body = body.translated (2.0f, 2.0f);
        else { g.setColour (Pro::win); g.fillRect (body.translated (2.0f, 2.0f)); }
        g.setColour (on ? Brand::rosa : (over ? Pro::raised.brighter (0.15f) : Pro::panel));
        g.fillRect (body);
        g.setColour (Pro::win);
        g.drawRect (body, 1.5f);
        return;
    }
    g.setColour (on ? Pro::value.withAlpha (0.14f) : (down ? Pro::line : Pro::raised));
    g.fillRect (r);
    g.setColour (on ? Pro::value : (over ? Pro::lineHi : Pro::line));
    g.drawRect (r, 1.0f);
    if (! b.isEnabled()) { g.setColour (Pro::win.withAlpha (0.5f)); g.fillRect (r); }
}

Font ProLook::getTextButtonFont (TextButton&, int h) { return Fonts::label (jmin (13.0f, (float) h * 0.5f)); }

void ProLook::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool brand = (bool) b.getProperties().getWithDefault ("onBrand", false);
    auto col = brand ? (b.getToggleState() ? Pro::win : Brand::leche) : (b.getToggleState() ? Pro::value : Pro::text);
    g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    auto r = b.getLocalBounds().reduced (6, 2);
    if (brand) r = r.withTrimmedRight (2).withTrimmedBottom (2);
    g.drawFittedText (::racoon::i18n::tr (friendlyCase (b.getButtonText())), r, Justification::centred, 1, 0.8f);
}

void ProLook::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (Pro::raised);
    g.fillRect (r);
    g.setColour (box.isMouseOver (true) ? Pro::lineHi : Pro::line);
    g.drawRect (r, 1.0f);
    // chevron
    const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
    Path p;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f);
    p.lineTo (cx, cy + 2.0f);
    p.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (Pro::textDim);
    g.strokePath (p, PathStrokeType (1.5f));
}

Font ProLook::getComboBoxFont (ComboBox&) { return Fonts::body (13.0f); }

void ProLook::positionComboBoxText (ComboBox& box, Label& l)
{
    l.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
    l.setColour (Label::textColourId, Pro::text);
}

void ProLook::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (Pro::panel);
    g.setColour (Pro::line);
    g.drawRect (0, 0, w, h, 1);
}

void ProLook::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool sep, bool active, bool highlighted, bool ticked, bool hasSub,
                                 const String& text, const String& shortcut, const Drawable*, const Colour*)
{
    if (sep)
    {
        g.setColour (Pro::line);
        g.fillRect (area.reduced (8, 0).withHeight (1).withCentre (area.getCentre()));
        return;
    }
    auto r = area.reduced (2, 0);
    if (highlighted && active) { g.setColour (Pro::value.withAlpha (0.16f)); g.fillRect (r); }
    if (ticked) { g.setColour (Pro::value); g.fillRect (r.removeFromLeft (3).reduced (0, 5)); } else r.removeFromLeft (3);
    r.removeFromLeft (9);
    g.setFont (Fonts::body (14.0f));
    g.setColour (! active ? Pro::textFaint : (highlighted ? Pro::value : Pro::text));
    if (hasSub)
    {
        const float cx = (float) r.getRight() - 10.0f, cy = (float) r.getCentreY();
        Path p; p.startNewSubPath (cx - 2.0f, cy - 4.0f); p.lineTo (cx + 2.0f, cy); p.lineTo (cx - 2.0f, cy + 4.0f);
        g.strokePath (p, PathStrokeType (1.5f));
        r.removeFromRight (20);
    }
    if (shortcut.isNotEmpty())
    {
        g.setFont (NumFont::medium (11.5f));
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr (shortcut), r.removeFromRight (90), Justification::centredRight);
        g.setFont (Fonts::body (14.0f));
        g.setColour (! active ? Pro::textFaint : (highlighted ? Pro::value : Pro::text));
    }
    g.drawFittedText (::racoon::i18n::tr (text), r, Justification::centredLeft, 1, 0.9f);
}

void ProLook::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& text)
{
    ProDraw::caps (g, text, area.toFloat().reduced (12, 0), Justification::bottomLeft, Pro::textFaint, 10.5f);
}

void ProLook::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (Pro::panel);
    g.setColour (Pro::line);
    g.drawRect (0, 0, w, h, 1);
    g.setColour (Pro::text);
    g.setFont (Fonts::body (13.5f));
    g.drawFittedText (::racoon::i18n::tr (text), Rectangle<int> (w, h).reduced (10, 6), Justification::centredLeft, 4);
}

void ProLook::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&) { g.setColour (Pro::screen); g.fillRect (0, 0, w, h); }
void ProLook::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& t)
{
    g.setColour (t.hasKeyboardFocus (true) ? Pro::value : Pro::line);
    g.drawRect (0, 0, w, h, 1);
}

// ===================================================================== drawing
namespace ProDraw
{
void caps (Graphics& g, const String& text, Rectangle<float> r, Justification j, Colour col, float size)
{
    g.setColour (col);
    g.setFont (Fonts::label (size).withExtraKerningFactor (0.09f));
    // a long word ("Amortiguación" under a narrow knob) shrinks to fit instead of losing its end
    g.drawFittedText (::racoon::i18n::tr (racoon::upper (text)), r.toNearestInt(), j, 1, 0.7f);
}

void card (Graphics& g, Rectangle<float> r, const String& title, Colour mark)
{
    g.setColour (Pro::panel);
    g.fillRect (r);
    g.setColour (Pro::line);
    g.drawRect (r, 1.0f);
    // the brand colour of the card: a band along the top and the title as a sticker, like the labels of the brand
    g.setColour (mark);
    g.fillRect (r.withHeight (3.0f));
    if (title.isNotEmpty())
    {
        const auto f = Fonts::label (10.5f).withExtraKerningFactor (0.09f);
        const auto text = racoon::upper (title);
        const float w = GlyphArrangement::getStringWidth (f, ::racoon::i18n::tr (text)) + 14.0f;
        auto chip = Rectangle<float> (r.getX() + 10.0f, r.getY() + 9.0f, w, 17.0f);
        g.setColour (mark);
        g.fillRect (chip);
        g.setColour (Pro::win);
        g.setFont (f);
        g.drawText (::racoon::i18n::tr (text), chip, Justification::centred, false);
    }
}

void screen (Graphics& g, Rectangle<float> r)
{
    g.setColour (Pro::screen);
    g.fillRect (r);
    g.setColour (Pro::line);
    g.drawRect (r, 1.0f);
}

void meter (Graphics& g, Rectangle<float> r, float db, Colour col, const String& label, bool reduction, float range)
{
    // number on top, label at the bottom, the bar in between with ticks
    auto numArea = r.removeFromTop (16.0f);
    auto labArea = r.removeFromBottom (14.0f);
    r.removeFromBottom (4.0f);
    auto bar = r.withSizeKeepingCentre (jmin (10.0f, r.getWidth()), r.getHeight());
    g.setColour (Pro::raised);
    g.fillRect (bar);
    float frac;
    if (reduction) frac = jlimit (0.0f, 1.0f, -db / range);
    else frac = jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    g.setColour (col);
    if (reduction) g.fillRect (bar.withHeight (bar.getHeight() * frac));      // reduction grows down from the top
    else g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * frac));
    // ticks
    g.setColour (Pro::textFaint);
    auto tick = [&] (float f) { const float y = reduction ? bar.getY() + bar.getHeight() * f : bar.getBottom() - bar.getHeight() * f;
                                g.fillRect (bar.getRight() + 2.0f, y, 3.0f, 1.0f); };
    if (reduction) for (float d : { 3.0f, 6.0f, 12.0f, 18.0f }) { if (d < range) tick (d / range); }
    else for (float d : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f }) tick ((d + 60.0f) / 66.0f);
    g.setColour (reduction ? (db < -0.05f ? col : Pro::textDim) : (db > 0.0f ? Pro::reduce : Pro::text));
    g.setFont (NumFont::medium (11.0f));
    g.drawText (::racoon::i18n::tr (db <= -59.5f && ! reduction ? String::fromUTF8 ("-\xe2\x88\x9e") : String (db, 1)), numArea, Justification::centred);
    caps (g, label, labArea, Justification::centred, Pro::textFaint, 9.5f);
}
}
}

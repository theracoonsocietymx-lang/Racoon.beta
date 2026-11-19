#include "Theme.h"
#include "engine/I18n.h"
#include "fx/core/Upper.h"
#include <BinaryData.h>

namespace racoon::ui
{
using namespace juce;
// Racoon (the mother brand) wears Limón with Rosa stickers
SurfaceTheme gSurface { Brand::limon, Brand::rosa };

// ===================================================================== fonts
namespace
{
Typeface::Ptr load (const char* data, int size) { return Typeface::createSystemTypefaceFor (data, (size_t) size); }
Typeface::Ptr tfTitle()    { static auto t = load (BinaryData::DelaGothicOne_ttf, BinaryData::DelaGothicOne_ttfSize); return t; }
[[maybe_unused]] Typeface::Ptr tfLabel()    { static auto t = load (BinaryData::AlumniSansExtraBold_ttf, BinaryData::AlumniSansExtraBold_ttfSize); return t; }
Typeface::Ptr tfBody()     { static auto t = load (BinaryData::GolosTextRegular_ttf, BinaryData::GolosTextRegular_ttfSize); return t; }
Typeface::Ptr tfBodyBold() { static auto t = load (BinaryData::GolosTextSemiBold_ttf, BinaryData::GolosTextSemiBold_ttfSize); return t; }
Typeface::Ptr tfMedium()   { static auto t = load (BinaryData::GolosTextMedium_ttf, BinaryData::GolosTextMedium_ttfSize); return t; }

bool isLight (Colour c) { return c.getPerceivedBrightness() > 0.55f; }
}

// "+ EFECTO" -> "+ Efecto", "HAMSTER DRUMS" -> "Hamster Drums", "ABRIR" -> "Abrir"; acronyms (MIDI, LFO, EQ, A/B)
// and short marks (M, S, A) stay as they are. Text that already has lower case is left alone.
String friendlyCase (const String& s)
{
    if (s.isEmpty() || s != s.toUpperCase() || s.toLowerCase() == s) return s;
    static const StringArray keep { "MIDI", "LFO", "BPM", "EQ", "FX", "LUFS", "ADSR", "DJ", "WAV", "FLAC", "MP3", "OK", "CPU", "SATB",
                                    "VST", "VST3", "AU", "DC", "XY", "FM", "AM", "RMS", "HP", "LP", "BP", "BPF", "HPF", "LPF", "UI", "IA", "AI", "TR", "RB", "UK", "DNB" };
    static const StringArray names { "RACOON", "BADGER", "HAMSTER", "DORMOUSE", "SQUIRREL", "FERRET", "PORCUPINE", "CHIPMUNK", "MOLE",
                                     "SKUNK", "CHINCHILLA", "HEDGEHOG" };
    auto words = StringArray::fromTokens (s, " ", "");
    const bool titleCase = names.contains (words[0].retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ")) || names.contains (words[0]);
    bool first = true;
    for (auto& w : words)
    {
        String letters;
        for (int i = 0; i < w.length(); ++i) if (CharacterFunctions::isLetter (w[i])) letters += String::charToString (w[i]);
        const bool isWord = letters.length() > 0;
        // "Y", "E", "O", "U" joining words read in lower case ("Match y EQ"); other single letters are marks (M, S, A, B)
        const bool conjunction = ! first && words.size() > 1 && (letters == "Y" || letters == "E" || letters == "O" || letters == "U");
        if (conjunction) { w = w.toLowerCase(); continue; }
        if (! isWord || keep.contains (letters) || letters.length() == 1 || w.containsAnyOf ("0123456789/")) { if (isWord) first = false; continue; }
        auto lower = w.toLowerCase();
        // the C library does not lower accented capitals everywhere
        for (auto pair : { "ÁáÉéÍíÓóÚúÑñÜü" })
        {
            const auto map = String::fromUTF8 (pair);
            for (int k = 0; k + 1 < map.length(); k += 2)
                lower = lower.replaceCharacter (map[k], map[k + 1]);
        }
        if (first || titleCase)
        {
            // capitalise the first letter (after any "+", "(" or similar)
            for (int i = 0; i < lower.length(); ++i)
                if (CharacterFunctions::isLetter (lower[i])) { lower = lower.substring (0, i) + String::charToString (CharacterFunctions::toUpperCase (lower[i])) + lower.substring (i + 1); break; }
        }
        w = lower;
        first = false;
    }
    return words.joinIntoString (" ");
}

// Reading comes first: Dela Gothic only for big display words (the wordmark, empty-state titles); every smaller
// title, button, tab and caption is Golos Text, Medium or SemiBold, which stays clear at small sizes
Font Fonts::title (float h)
{
    if (h >= 20.0f) return Font (FontOptions (tfTitle()).withHeight (h * 0.92f));
    return Font (FontOptions (tfBodyBold()).withHeight (h * 0.98f));
}
Font Fonts::label (float h)    { return Font (FontOptions (tfMedium()).withHeight (h * 0.98f)).withExtraKerningFactor (0.015f); }
Font Fonts::body (float h)     { return Font (FontOptions (tfBody()).withHeight (h * 0.92f)); }
Font Fonts::bodyBold (float h) { return Font (FontOptions (tfBodyBold()).withHeight (h * 0.92f)); }
Font Fonts::mono (float h)     { return Font (FontOptions (tfBodyBold()).withHeight (h * 0.95f)); }

// ===================================================================== drawing helpers
namespace Draw
{
void engraved (Graphics& g, const String& text, Rectangle<float> r, Font f, Justification j, Colour col)
{
    // surfaces are light now: light label colours from the old palette become ink
    if (isLight (col)) col = col.getPerceivedBrightness() > 0.8f ? S::ink : S::inkSoft;
    else if (col == C::amber || col == C::amberHot) col = S::ink;
    g.setFont (f);
    g.setColour (col);
    g.drawText (::racoon::i18n::tr (text), r, j, false);
}

void glowLine (Graphics& g, Point<float> a, Point<float> b, Colour c, float w)
{
    g.setColour (c.withAlpha (c.getFloatAlpha() * 0.18f));
    g.drawLine ({ a, b }, w + 3.0f);
    g.setColour (c);
    g.drawLine ({ a, b }, w);
}

void glowPath (Graphics& g, const Path& p, Colour c, float w)
{
    g.setColour (c.withAlpha (c.getFloatAlpha() * 0.18f));
    g.strokePath (p, PathStrokeType (w + 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (c);
    g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
}

void glyph (Graphics& g, Rectangle<float> r, int, Colour c, float)
{
    g.setColour (c);
    g.fillRect (r.withSizeKeepingCentre (r.getWidth() * 0.5f, r.getHeight() * 0.5f));
}

void box (Graphics& g, Rectangle<float> r, Colour fill, float border, float shadow, bool pressed)
{
    auto body = r.withTrimmedRight (shadow).withTrimmedBottom (shadow);
    if (pressed) body = body.translated (shadow, shadow);
    else if (shadow > 0.0f)
    {
        g.setColour (S::shade);
        g.fillRect (body.translated (shadow, shadow));
    }
    g.setColour (fill);
    g.fillRect (body);
    if (border > 0.0f)
    {
        // a hairline on dark fills, the brand's hard border on coloured ones
        const bool dark = fill.getPerceivedBrightness() < 0.35f;
        g.setColour (dark ? juce::Colour (0xff3a3236) : S::shade);
        g.drawRect (body, dark ? 1.0f : border);
    }
}

void racoonMark (Graphics& g, Rectangle<float> r, Colour face, Colour mask, Colour eyes)
{
    // brand isotipo "Luna antifaz" on a 64 x 64 grid (viewBox -3 -3 72 72)
    const float s = std::min (r.getWidth(), r.getHeight()) / 70.0f;
    const auto o = r.getCentre() - Point<float> (32.0f * s, 34.0f * s);
    auto P = [&] (float x, float y) { return Point<float> (o.x + x * s, o.y + y * s); };
    Path head;
    head.addTriangle (P (11, 28), P (18, 7), P (27, 21));
    head.addTriangle (P (53, 28), P (46, 7), P (37, 21));
    head.addEllipse (o.x + 11.0f * s, o.y + 16.0f * s, 42.0f * s, 42.0f * s);
    g.setColour (face);
    g.fillPath (head);
    g.setColour (mask);
    g.fillRect (o.x + 4.0f * s, o.y + 30.0f * s, 56.0f * s, 13.0f * s);
    g.setColour (eyes);
    g.fillRect (o.x + 16.0f * s, o.y + 33.0f * s, 7.0f * s, 7.0f * s);
    g.fillRect (o.x + 41.0f * s, o.y + 33.0f * s, 7.0f * s, 7.0f * s);
    g.setColour (mask);
    g.fillRect (o.x + 29.0f * s, o.y + 48.0f * s, 6.0f * s, 6.0f * s);
}

void panel (Graphics& g, Rectangle<float> r, const String& title, bool lit)
{
    // a dark card with a hairline; the title is a sticker in the accent colour
    g.setColour (lit ? S::cardDim : S::card);
    g.fillRect (r);
    g.setColour (juce::Colour (0xff3a3236));
    g.drawRect (r, 1.0f);
    g.setColour (S::accent());
    g.fillRect (r.withHeight (3.0f));
    if (title.isNotEmpty())
    {
        const auto f = Fonts::label (10.5f).withExtraKerningFactor (0.09f);
        const String t = upper (title);
        const float tw = GlyphArrangement::getStringWidth (f, ::racoon::i18n::tr (t)) + 14.0f;
        auto chip = Rectangle<float> (r.getX() + 10.0f, r.getY() + 7.0f, std::min (tw, r.getWidth() - 24.0f), 17.0f);
        g.setColour (S::accent());
        g.fillRect (chip);
        g.setColour (S::shade);
        g.setFont (f);
        g.drawFittedText (::racoon::i18n::tr (t), chip.toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

Image background (int w, int h)
{
    Image img (Image::RGB, std::max (1, w), std::max (1, h), true);
    Graphics g (img);
    g.fillAll (S::bg());
    // a very light paper grain so the flat colour does not look like a placeholder
    Random rnd (2026);
    for (int i = 0; i < w * h / 40; ++i)
    {
        const float x = rnd.nextFloat() * (float) w, y = rnd.nextFloat() * (float) h;
        g.setColour (rnd.nextBool() ? Colours::white.withAlpha (0.05f) : Colours::black.withAlpha (0.035f));
        g.fillRect (x, y, 1.0f, 1.0f);
    }
    return img;
}
} // namespace Draw

// ===================================================================== look and feel
AncientLook::AncientLook()
{
    setColour (ResizableWindow::backgroundColourId, S::bg());
    setColour (Label::textColourId, S::ink);
    setColour (ComboBox::textColourId, S::ink);
    setColour (ComboBox::backgroundColourId, S::card);
    setColour (ComboBox::outlineColourId, S::ink);
    setColour (ComboBox::arrowColourId, S::ink);
    setColour (PopupMenu::backgroundColourId, S::card);
    setColour (PopupMenu::textColourId, S::ink);
    setColour (PopupMenu::highlightedBackgroundColourId, S::hi);
    setColour (PopupMenu::highlightedTextColourId, S::ink);
    setColour (TextButton::buttonColourId, S::card);
    setColour (TextButton::textColourOffId, S::ink);
    setColour (TextButton::textColourOnId, S::ink);
    setColour (TextEditor::backgroundColourId, Colours::white);
    setColour (TextEditor::textColourId, S::ink);
    setColour (TextEditor::highlightColourId, S::hi.withAlpha (0.6f));
    setColour (TextEditor::outlineColourId, S::ink);
    setColour (TextEditor::focusedOutlineColourId, S::ink);
    setColour (CaretComponent::caretColourId, S::ink);
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (ListBox::textColourId, S::ink);
    setColour (ScrollBar::thumbColourId, S::ink);
    setColour (Slider::textBoxTextColourId, S::ink);
    setColour (Slider::textBoxOutlineColourId, S::ink);
    setColour (Slider::textBoxBackgroundColourId, S::card);
    setColour (Slider::textBoxHighlightColourId, S::hi);
    setColour (Label::backgroundWhenEditingColourId, Colours::white);
    setColour (Label::textWhenEditingColourId, S::ink);
    setColour (Label::outlineWhenEditingColourId, S::ink);
    setColour (ToggleButton::textColourId, S::ink);
    setColour (ToggleButton::tickColourId, S::ink);
    setColour (TooltipWindow::backgroundColourId, S::card);
    setColour (TooltipWindow::textColourId, S::ink);
    setColour (AlertWindow::backgroundColourId, S::card);
    setColour (AlertWindow::textColourId, S::ink);
    setColour (AlertWindow::outlineColourId, S::ink);
    setColour (HyperlinkButton::textColourId, S::ink);
    setDefaultSansSerifTypeface (Fonts::body (14.0f).getTypefacePtr());
}

void AncientLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    // faceted Limón knob with a Ciruela border and hard shadow, value arc in ink around it
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const bool enabled = s.isEnabled();
    const float ang = start + pos * (end - start);

    Path track;
    track.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, start, end, true);
    g.setColour (S::ink.withAlpha (0.16f));
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::mitered, PathStrokeType::butt));
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (ang - from) > 0.01f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (enabled ? S::ink : S::inkFaint);
        g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::mitered, PathStrokeType::butt));
    }

    // a 20-sided knob: round enough to read as turning, with facets that turn with it, and the hard shadow
    const float side = r * 1.12f;
    const float sh = std::max (2.0f, side * 0.07f);
    const auto kc = c.translated (-sh * 0.5f, -sh * 0.5f);
    const float pr = side * 0.5f;
    Path poly;
    constexpr int faces = 20;
    for (int i = 0; i < faces; ++i)
    {
        const float a = ang + MathConstants<float>::twoPi * ((float) i + 0.5f) / (float) faces;
        const Point<float> v (kc.x + std::sin (a) * pr, kc.y - std::cos (a) * pr);
        if (i == 0) poly.startNewSubPath (v); else poly.lineTo (v);
    }
    poly.closeSubPath();
    g.setColour (S::shade);
    g.fillPath (poly, AffineTransform::translation (sh, sh));
    g.setColour (enabled ? S::knob : S::cardDim);
    g.fillPath (poly);
    g.setColour (S::shade);
    g.strokePath (poly, PathStrokeType (std::max (1.5f, side * 0.045f), PathStrokeType::mitered));
    // indicator
    const float ir = side * 0.40f;
    const Point<float> p1 (kc.x + std::sin (ang) * ir * 0.25f, kc.y - std::cos (ang) * ir * 0.25f);
    const Point<float> p2 (kc.x + std::sin (ang) * ir, kc.y - std::cos (ang) * ir);
    g.setColour (S::shade);
    g.drawLine ({ p1, p2 }, std::max (2.0f, side * 0.09f));
}

void AncientLook::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    const auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const bool vertical = style == Slider::LinearVertical;
    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    if (vertical)
    {
        auto tr = r.withSizeKeepingCentre (6.0f, r.getHeight());
        g.setColour (S::ink.withAlpha (0.18f)); g.fillRect (tr);
        const float zero = bipolar ? r.getCentreY() : r.getBottom();
        g.setColour (S::ink);
        g.fillRect (Rectangle<float> (tr.getX(), std::min (zero, pos), tr.getWidth(), std::abs (zero - pos)));
        auto thumb = Rectangle<float> (std::min (r.getWidth(), 26.0f), 12.0f).withCentre ({ r.getCentreX(), pos });
        Draw::box (g, thumb.withSize (thumb.getWidth() + 2.0f, thumb.getHeight() + 2.0f), S::knob, 1.5f, 2.0f);
    }
    else
    {
        auto tr = r.withSizeKeepingCentre (r.getWidth(), 5.0f);
        g.setColour (S::ink.withAlpha (0.18f)); g.fillRect (tr);
        const float zero = bipolar ? r.getCentreX() : r.getX();
        g.setColour (S::ink);
        g.fillRect (Rectangle<float> (std::min (zero, pos), tr.getY(), std::abs (zero - pos), tr.getHeight()));
        auto thumb = Rectangle<float> (10.0f, std::min (r.getHeight(), 18.0f)).withCentre ({ pos, r.getCentreY() });
        Draw::box (g, thumb.withSize (thumb.getWidth() + 2.0f, thumb.getHeight() + 2.0f), S::knob, 1.5f, 2.0f);
    }
}

// buttons on the Limón header: a dark one gets a light outline so it does not melt into its shadow
void headerOutline (Graphics& g, Component& c, Rectangle<float> r, Colour fill, float sh, bool down)
{
    if (! (bool) c.getProperties().getWithDefault ("onHeader", false) || fill.getPerceivedBrightness() >= 0.35f) return;
    auto body = r.withTrimmedRight (sh).withTrimmedBottom (sh);
    if (down) body = body.translated (sh, sh);
    g.setColour (S::ink);
    g.drawRect (body, 1.0f);
}

void AncientLook::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const float sh = r.getHeight() >= 20.0f ? 3.0f : 2.0f;
    Colour fill = on ? S::hi : (over ? C::stone : S::card);
    if (! b.isEnabled()) fill = S::cardDim;
    Draw::box (g, r, fill, 1.5f, sh, down);
    headerOutline (g, b, r, fill, sh, down);
}

void AncientLook::drawButtonText (Graphics& g, TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    // on a light fill (a button that is on, in Limón) the text turns dark so it reads
    g.setColour (! b.isEnabled() ? S::inkFaint : (b.getToggleState() ? S::shade : S::ink));
    const float sh = b.getHeight() >= 20 ? 3.0f : 2.0f;
    auto r = b.getLocalBounds().toFloat().withTrimmedRight (sh).withTrimmedBottom (sh);
    if (down) r = r.translated (sh, sh);
    g.drawFittedText (::racoon::i18n::tr (friendlyCase (b.getButtonText())), r.toNearestInt().reduced (3, 0), Justification::centred, 1, 0.85f);
}

Font AncientLook::getTextButtonFont (TextButton&, int h) { return Fonts::label (std::min (14.0f, (float) h * 0.56f)); }

void AncientLook::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float s = std::min (14.0f, r.getHeight() - 4.0f);
    auto bx = Rectangle<float> (s, s).withCentre ({ r.getX() + s * 0.5f + 2.0f, r.getCentreY() });
    g.setColour (over ? Colours::white : S::card);
    g.fillRect (bx);
    g.setColour (S::ink);
    g.drawRect (bx, 1.5f);
    if (b.getToggleState())
    {
        g.setColour (S::accent());
        g.fillRect (bx.reduced (3.0f));
        g.setColour (S::ink);
        g.drawRect (bx.reduced (3.0f), 1.0f);
    }
    if (b.getButtonText().isNotEmpty())
    {
        g.setFont (Fonts::label (12.5f));
        g.setColour (b.getToggleState() ? S::ink : S::inkSoft);
        g.drawText (::racoon::i18n::tr (b.getButtonText()), r.withTrimmedLeft (s + 9.0f), Justification::centredLeft, true);
    }
}

void AncientLook::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (box.isMouseOver (true) ? C::stone : S::card);
    g.fillRect (r);
    g.setColour (S::ink);
    g.drawRect (r, 1.5f);
    // arrow block
    auto ab = r.removeFromRight (16.0f);
    g.setColour (S::hi);
    g.fillRect (ab.reduced (1.5f));
    g.setColour (S::ink);
    g.drawVerticalLine ((int) ab.getX(), 0.0f, (float) h);
    Path a;
    const float cx = ab.getCentreX(), cy = (float) h * 0.5f;
    a.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
    g.setColour (box.isEnabled() ? S::shade : S::inkFaint);   // on the Limón arrow block
    g.fillPath (a);
}

Font AncientLook::getComboBoxFont (ComboBox& b) { return Fonts::bodyBold (std::min (14.5f, (float) b.getHeight() * 0.68f)); }

void AncientLook::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 21, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (Label::textColourId, S::ink);
    label.setJustificationType (Justification::centredLeft);
}

Font AncientLook::getPopupMenuFont() { return Fonts::body (15.0f); }

void AncientLook::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (S::card);
    g.setColour (S::ink);
    g.drawRect (0, 0, w, h, 2);
}

void AncientLook::drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& text)
{
    g.setFont (Fonts::label (12.0f));
    g.setColour (S::inkSoft);
    g.drawFittedText (::racoon::i18n::tr (upper (text)), area.reduced (12, 0), Justification::bottomLeft, 1);
}

void AncientLook::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool sep, bool active, bool hi, bool ticked, bool hasSub,
                                     const String& text, const String& shortcut, const Drawable* icon, const Colour* tc)
{
    if (sep)
    {
        g.setColour (S::ink.withAlpha (0.3f));
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 8.0f, (float) area.getRight() - 8.0f);
        return;
    }
    auto r = area.reduced (2, 1);
    if (hi && active) { g.setColour (S::hi); g.fillRect (r); }
    if (ticked)
    {
        g.setColour (S::accent());
        g.fillRect (Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 10.0f, (float) r.getCentreY() }));
        g.setColour (S::ink);
        g.drawRect (Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 10.0f, (float) r.getCentreY() }), 1.0f);
    }
    g.setFont (ticked ? Fonts::bodyBold (15.0f) : getPopupMenuFont());
    // the item under the mouse is lit in Limón: its text goes dark
    g.setColour (! active ? S::inkFaint : (hi ? S::shade : (tc != nullptr && ! isLight (*tc) ? *tc : S::ink)));
    g.drawFittedText (::racoon::i18n::tr (text), r.withTrimmedLeft (20).withTrimmedRight (hasSub ? 18 : 6), Justification::centredLeft, 1);
    if (hasSub)
    {
        Path a; const float cx = (float) r.getRight() - 9.0f, cy = (float) r.getCentreY();
        a.addTriangle (cx - 2.0f, cy - 3.5f, cx - 2.0f, cy + 3.5f, cx + 2.5f, cy);
        g.setColour (S::ink); g.fillPath (a);
    }
    if (shortcut.isNotEmpty()) { g.setColour (S::inkSoft); g.drawText (::racoon::i18n::tr (shortcut), r.withTrimmedRight (8), Justification::centredRight); }
    (void) icon;
}

Label* AncientLook::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (Label::textColourId, S::ink);
    l->setColour (Label::backgroundColourId, S::card);
    l->setColour (Label::outlineColourId, S::ink);
    l->setColour (TextEditor::textColourId, S::ink);
    l->setColour (TextEditor::backgroundColourId, Colours::white);
    l->setColour (TextEditor::highlightColourId, S::hi);
    l->setFont (Fonts::mono (12.0f));
    return l;
}

Font AncientLook::getLabelFont (Label& l) { return Fonts::body ((float) std::min (15, std::max (10, l.getHeight() - 4))); }

Rectangle<int> AncientLook::getTooltipBounds (const String& text, Point<int> pos, Rectangle<int> parent)
{
    // wrap long help texts into a box of at most 320 px
    const auto f = Fonts::body (14.0f);
    const float maxW = 320.0f;
    AttributedString a; a.append (::racoon::i18n::tr (text), f, S::card); a.setWordWrap (AttributedString::byWord);
    TextLayout tl; tl.createLayout (a, maxW);
    const int w = (int) std::ceil (std::min (maxW, tl.getWidth())) + 20;
    const int h = (int) std::ceil (tl.getHeight()) + 14;
    return Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                           pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 14, w, h).constrainedWithin (parent);
}

void AncientLook::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (S::cardDim);
    g.setColour (S::accent());
    g.fillRect (0, 0, 4, h);
    AttributedString a; a.append (::racoon::i18n::tr (text), Fonts::body (14.0f), S::ink); a.setWordWrap (AttributedString::byWord);
    TextLayout tl; tl.createLayout (a, (float) w - 20.0f);
    tl.draw (g, Rectangle<float> (12.0f, 7.0f, (float) w - 20.0f, (float) h - 14.0f));
}

void AncientLook::drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    g.setColour (S::ink.withAlpha (0.12f));
    g.fillRect (x, y, w, h);
    g.setColour (over ? S::ink : S::inkSoft);
    if (vertical) g.fillRect ((float) x + (float) w * 0.2f, (float) ts, (float) w * 0.6f, (float) tsz);
    else g.fillRect ((float) ts, (float) y + (float) h * 0.2f, (float) tsz, (float) h * 0.6f);
}

void AncientLook::fillTextEditorBackground (Graphics& g, int w, int h, TextEditor&) { g.setColour (C::engrave); g.fillRect (0, 0, w, h); }
void AncientLook::drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& t)
{
    g.setColour (t.hasKeyboardFocus (true) ? Brand::limon : C::stoneHi);
    g.drawRect (0, 0, w, h, 1);
}

void AncientLook::drawCallOutBoxBackground (CallOutBox&, Graphics& g, const Path& path, Image&)
{
    g.setColour (S::card);
    g.fillPath (path);
    g.setColour (S::ink);
    g.strokePath (path, PathStrokeType (2.0f));
}

void AncientLook::drawAlertBox (Graphics& g, AlertWindow& a, const Rectangle<int>& textArea, TextLayout& layout)
{
    g.fillAll (S::bg());
    Draw::box (g, a.getLocalBounds().toFloat().reduced (6.0f), S::card, 2.0f, 5.0f);
    layout.draw (g, textArea.toFloat());
}

Font AncientLook::getAlertWindowTitleFont() { return Fonts::title (19.0f); }
Font AncientLook::getAlertWindowMessageFont() { return Fonts::body (15.0f); }
Font AncientLook::getAlertWindowFont() { return Fonts::body (15.0f); }

// ===================================================================== interface language
void AncientLook::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    if (! label.isBeingEdited())
    {
        const float alpha = label.isEnabled() ? 1.0f : 0.5f;
        const juce::Font font (getLabelFont (label));
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        auto area = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (::racoon::i18n::tr (label.getText()), area, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())), label.getMinimumHorizontalScale());
        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    }
    else if (label.isEnabled())
        g.setColour (label.findColour (juce::Label::outlineColourId));
    g.drawRect (label.getLocalBounds());
}

juce::AlertWindow* AncientLook::createAlertWindow (const juce::String& title, const juce::String& message, const juce::String& b1,
                                                   const juce::String& b2, const juce::String& b3, juce::MessageBoxIconType icon,
                                                   int numButtons, juce::Component* associated)
{
    using ::racoon::i18n::tr;
    return LookAndFeel_V4::createAlertWindow (tr (title), tr (message), tr (b1), tr (b2), tr (b3), icon, numButtons, associated);
}

void AncientLook::getIdealPopupMenuItemSize (const juce::String& text, bool sep, int standard, int& w, int& h)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (::racoon::i18n::tr (text), sep, standard, w, h);
}

} // namespace racoon::ui

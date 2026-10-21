#include "Widgets.h"
#include "engine/I18n.h"

namespace racoon::ui
{
using namespace juce;

PropertiesFile& prefs()
{
    static std::unique_ptr<PropertiesFile> p = []
    {
        PropertiesFile::Options o;
        o.applicationName = "Racoon";
        o.filenameSuffix = ".settings";
        o.folderName = "Racoon";
        o.osxLibrarySubFolder = "Application Support";
        return std::make_unique<PropertiesFile> (o);
    }();
    return *p;
}

float volumeFaderToDb (float pos) { return te::volumeFaderPositionToDB (pos); }
float dbToVolumeFader (float db)  { return te::decibelsToVolumeFaderPosition (db); }

void drawGlyph (Graphics& g, Glyph glyph, Rectangle<float> r, Colour c)
{
    g.setColour (c);
    const auto s = jmin (r.getWidth(), r.getHeight());
    auto b = r.withSizeKeepingCentre (s, s);
    Path p;
    switch (glyph)
    {
        case Glyph::none: break;
        case Glyph::play:
            p.addTriangle (b.getX() + s * 0.22f, b.getY() + s * 0.12f, b.getX() + s * 0.22f, b.getBottom() - s * 0.12f, b.getRight() - s * 0.12f, b.getCentreY());
            g.fillPath (p); break;
        case Glyph::stop:
            g.fillRect (b.reduced (s * 0.18f)); break;
        case Glyph::record:
            g.fillEllipse (b.reduced (s * 0.15f)); break;
        case Glyph::toStart:
            g.fillRect (b.getX() + s * 0.14f, b.getY() + s * 0.16f, s * 0.12f, s * 0.68f);
            p.addTriangle (b.getRight() - s * 0.14f, b.getY() + s * 0.16f, b.getRight() - s * 0.14f, b.getBottom() - s * 0.16f, b.getX() + s * 0.3f, b.getCentreY());
            g.fillPath (p); break;
        case Glyph::loop:
        {
            auto rr = b.reduced (s * 0.14f, s * 0.24f);
            p.addRoundedRectangle (rr, s * 0.14f);
            g.strokePath (p, PathStrokeType (s * 0.1f));
            Path a; a.addTriangle (rr.getRight() - s * 0.2f, rr.getY() - s * 0.13f, rr.getRight() - s * 0.2f, rr.getY() + s * 0.13f, rr.getRight() + s * 0.02f, rr.getY());
            g.fillPath (a);
            Path a2; a2.addTriangle (rr.getX() + s * 0.2f, rr.getBottom() - s * 0.13f, rr.getX() + s * 0.2f, rr.getBottom() + s * 0.13f, rr.getX() - s * 0.02f, rr.getBottom());
            g.fillPath (a2);
            break;
        }
        case Glyph::metronome:
            p.addTriangle (b.getCentreX(), b.getY() + s * 0.1f, b.getX() + s * 0.18f, b.getBottom() - s * 0.1f, b.getRight() - s * 0.18f, b.getBottom() - s * 0.1f);
            g.strokePath (p, PathStrokeType (s * 0.09f));
            g.drawLine (b.getCentreX(), b.getBottom() - s * 0.25f, b.getRight() - s * 0.2f, b.getY() + s * 0.25f, s * 0.09f);
            break;
        case Glyph::undo:
        case Glyph::redo:
        {
            // the curved arrow everybody knows: a hook that turns back (undo) or forward (redo)
            const bool u = glyph == Glyph::undo;
            const float cx = b.getCentreX() + (u ? s * 0.06f : -s * 0.06f), cy = b.getCentreY() + s * 0.06f, rad = s * 0.26f;
            // from the tip (left for undo) over the top and down the other side
            const float a0 = u ? -MathConstants<float>::halfPi : MathConstants<float>::halfPi;
            const float a1 = u ? MathConstants<float>::pi * 0.8f : -MathConstants<float>::pi * 0.8f;
            p.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a1, true);
            g.strokePath (p, PathStrokeType (s * 0.11f, PathStrokeType::curved, PathStrokeType::rounded));
            // arrow head at the start of the arc, pointing down
            const float tx = cx + std::sin (a0) * rad, ty = cy - std::cos (a0) * rad;
            Path a;
            a.addTriangle (tx - s * 0.16f, ty - s * 0.03f, tx + s * 0.16f, ty - s * 0.03f, tx, ty + s * 0.2f);
            g.fillPath (a);
            break;
        }
        case Glyph::plus:
            g.fillRect (b.getCentreX() - s * 0.06f, b.getY() + s * 0.2f, s * 0.12f, s * 0.6f);
            g.fillRect (b.getX() + s * 0.2f, b.getCentreY() - s * 0.06f, s * 0.6f, s * 0.12f);
            break;
        case Glyph::close:
            g.drawLine (b.getX() + s * 0.25f, b.getY() + s * 0.25f, b.getRight() - s * 0.25f, b.getBottom() - s * 0.25f, s * 0.11f);
            g.drawLine (b.getRight() - s * 0.25f, b.getY() + s * 0.25f, b.getX() + s * 0.25f, b.getBottom() - s * 0.25f, s * 0.11f);
            break;
        case Glyph::folder:
            g.fillRect (b.getX() + s * 0.12f, b.getY() + s * 0.3f, s * 0.76f, s * 0.5f);
            g.fillRect (b.getX() + s * 0.12f, b.getY() + s * 0.22f, s * 0.3f, s * 0.1f);
            break;
        case Glyph::save:
            g.fillRect (b.reduced (s * 0.16f));
            g.setColour (c.contrasting());
            g.fillRect (b.getX() + s * 0.3f, b.getY() + s * 0.16f, s * 0.4f, s * 0.22f);
            break;
        case Glyph::gear:
            g.fillEllipse (b.reduced (s * 0.2f));
            for (int i = 0; i < 8; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / 8.0f;
                g.fillRect (Rectangle<float> (s * 0.14f, s * 0.14f).withCentre ({ b.getCentreX() + std::cos (a) * s * 0.36f, b.getCentreY() + std::sin (a) * s * 0.36f }));
            }
            g.setColour (c.contrasting());
            g.fillEllipse (b.reduced (s * 0.38f));
            break;
        case Glyph::scissors:
            g.drawEllipse (b.getX() + s * 0.12f, b.getBottom() - s * 0.4f, s * 0.26f, s * 0.26f, s * 0.08f);
            g.drawEllipse (b.getRight() - s * 0.38f, b.getBottom() - s * 0.4f, s * 0.26f, s * 0.26f, s * 0.08f);
            g.drawLine (b.getX() + s * 0.32f, b.getBottom() - s * 0.38f, b.getRight() - s * 0.3f, b.getY() + s * 0.12f, s * 0.08f);
            g.drawLine (b.getRight() - s * 0.32f, b.getBottom() - s * 0.38f, b.getX() + s * 0.3f, b.getY() + s * 0.12f, s * 0.08f);
            break;
        case Glyph::keyboard:
            g.drawRect (b.reduced (s * 0.1f, s * 0.2f), s * 0.07f);
            for (int i = 1; i < 4; ++i)
                g.fillRect (b.getX() + s * 0.1f + (s * 0.8f) * (float) i / 4.0f - s * 0.04f, b.getY() + s * 0.2f, s * 0.08f, s * 0.36f);
            break;
        case Glyph::mixer:
            for (int i = 0; i < 3; ++i)
            {
                const float x = b.getX() + s * (0.25f + 0.25f * (float) i);
                g.fillRect (x - s * 0.03f, b.getY() + s * 0.12f, s * 0.06f, s * 0.76f);
                g.fillRect (x - s * 0.1f, b.getY() + s * (0.3f + 0.2f * (float) ((i + 1) % 3)), s * 0.2f, s * 0.12f);
            }
            break;
        case Glyph::wave:
        {
            p.startNewSubPath (b.getX() + s * 0.1f, b.getCentreY());
            for (int i = 1; i <= 16; ++i)
            {
                const float x = b.getX() + s * 0.1f + s * 0.8f * (float) i / 16.0f;
                p.lineTo (x, b.getCentreY() + std::sin ((float) i * 0.9f) * s * 0.3f * ((i % 2) ? 1.0f : 0.5f));
            }
            g.strokePath (p, PathStrokeType (s * 0.08f));
            break;
        }
        case Glyph::back:
        case Glyph::forward:
        {
            const bool bk = glyph == Glyph::back;
            const float x0 = b.getX() + s * (bk ? 0.68f : 0.32f), x1 = b.getX() + s * (bk ? 0.32f : 0.68f);
            p.startNewSubPath (x0, b.getY() + s * 0.2f);
            p.lineTo (x1, b.getCentreY());
            p.lineTo (x0, b.getBottom() - s * 0.2f);
            g.strokePath (p, PathStrokeType (s * 0.13f, PathStrokeType::mitered, PathStrokeType::square));
            break;
        }
        case Glyph::follow:
            p.addTriangle (b.getX() + s * 0.2f, b.getY() + s * 0.2f, b.getX() + s * 0.2f, b.getBottom() - s * 0.2f, b.getCentreX(), b.getCentreY());
            p.addTriangle (b.getCentreX(), b.getY() + s * 0.2f, b.getCentreX(), b.getBottom() - s * 0.2f, b.getRight() - s * 0.2f, b.getCentreY());
            g.fillPath (p);
            break;
    }
}

GlyphButton::GlyphButton (Glyph g, const String& text, Colour on)
    : Button (text), onColour (on), glyph (g) {}

void GlyphButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    Colour fill = on ? onColour : (over ? C::stone : S::card);
    if (! isEnabled()) fill = S::cardDim;
    const float sh = r.getHeight() >= 24.0f ? 3.0f : 2.0f;
    Draw::box (g, r, fill, 1.5f, sh, down);
    headerOutline (g, *this, r, fill, sh, down);
    auto body = r.withTrimmedRight (sh).withTrimmedBottom (sh);
    if (down) body.translate (sh, sh);

    const auto ink = ! isEnabled() ? S::inkFaint : (on && onColour.getPerceivedBrightness() > 0.45f ? S::shade : S::ink);
    if (glyph == Glyph::none)
    {
        g.setColour (ink);
        g.setFont (Fonts::label (jmin (15.0f, body.getHeight() * 0.62f)));
        g.drawFittedText (::racoon::i18n::tr (getButtonText()), body.toNearestInt(), Justification::centred, 1, 0.8f);
    }
    else if (getButtonText().isEmpty())
        drawGlyph (g, glyph, body.reduced (body.getHeight() * 0.2f), ink);
    else
    {
        auto gr = body.removeFromLeft (body.getHeight()).reduced (body.getHeight() * 0.24f);
        drawGlyph (g, glyph, gr, ink);
        g.setColour (ink);
        g.setFont (Fonts::label (jmin (14.0f, body.getHeight() * 0.5f)));
        g.drawFittedText (::racoon::i18n::tr (getButtonText()), body.toNearestInt().withTrimmedRight (6), Justification::centredLeft, 1, 0.8f);
    }
}

TextButton* makeButton (OwnedArray<Component>& owner, Component& parent, const String& text, const String& help)
{
    auto* b = new TextButton (text);
    b->setTooltip (help);
    parent.addAndMakeVisible (b);
    owner.add (b);
    return b;
}

// ------------------------------------------------------------------ meter
void LevelMeter::attach (te::LevelMeasurer* m, te::Plugin::Ptr keepAlive)
{
    if (measurer == m) return;
    if (measurer != nullptr) measurer->removeClient (client);
    measurer = m;
    owner = keepAlive;
    if (measurer != nullptr) measurer->addClient (client);
}

void LevelMeter::timerCallback()
{
    bool changed = false;
    for (int c = 0; c < 2; ++c)
    {
        float db = -100.0f;
        if (measurer != nullptr)
            db = client.getAndClearAudioLevel (c).dB;
        maxDb = jmax (maxDb, db);
        const float decayed = level[c] - 1.6f;
        const float nl = jmax (db, decayed, -100.0f);
        if (std::abs (nl - level[c]) > 0.05f) { level[c] = nl; changed = true; }
        if (nl >= peak[c]) { peak[c] = nl; peakHold[c] = 40; }
        else if (--peakHold[c] <= 0) { peak[c] = jmax (-100.0f, peak[c] - 1.0f); changed = true; }
    }
    if (changed) repaint();
}

void LevelMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (S::shade);
    g.fillRect (r);
    auto inner = r.reduced (2.0f);
    const float w = (inner.getWidth() - 2.0f) / 2.0f;
    auto toY = [&] (float db) { return inner.getBottom() - inner.getHeight() * jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };

    for (int c = 0; c < 2; ++c)
    {
        auto bar = Rectangle<float> (inner.getX() + (float) c * (w + 2.0f), inner.getY(), w, inner.getHeight());
        const float y = toY (level[c]);
        const float y0 = toY (0.0f), y6 = toY (-6.0f);
        if (y < bar.getBottom())
        {
            g.setColour (Brand::menta);
            g.fillRect (bar.withTop (jmax (y, y6)));
            if (y < y6) { g.setColour (Brand::limon); g.fillRect (bar.withTop (jmax (y, y0)).withBottom (y6)); }
            if (y < y0) { g.setColour (Brand::rosa); g.fillRect (bar.withBottom (y0).withTop (y)); }
        }
        if (peak[c] > -59.0f)
        {
            g.setColour (peak[c] > 0.0f ? Brand::rosa : Brand::leche);
            g.fillRect (bar.getX(), toY (peak[c]), bar.getWidth(), 1.5f);
        }
    }
    g.setColour (Brand::leche.withAlpha (0.25f));
    g.fillRect (inner.getX(), toY (0.0f), inner.getWidth(), 1.0f);
}

// ------------------------------------------------------------------ fader & knob
Fader::Fader() : GrabSlider (Slider::LinearVertical, Slider::TextBoxBelow)
{
    setRange (0.0, 1.0);
    setTextBoxStyle (Slider::TextBoxBelow, false, 56, 18);
    setDoubleClickReturnValue (true, dbToVolumeFader (0.0f));
    setTooltip (String::fromUTF8 ("Volumen de la pista. Clic en cualquier punto: el fader va ahí y sigue al puntero. Doble clic: 0 dB."));
    setSliderSnapsToMousePosition (true);
}

String Fader::getTextFromValue (double v)
{
    const float db = volumeFaderToDb ((float) v);
    return db <= -99.0f ? String::fromUTF8 ("-∞") : String (db, 1) + " dB";
}

Knob::Knob (const String& cap, const String& help) : caption (cap)
{
    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setTooltip (help);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    slider.setBounds (r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), jmin (r.getWidth(), r.getHeight())));
}

void Knob::paint (Graphics& g)
{
    drawCaption (g, caption, getLocalBounds().removeFromBottom (14), 11.0f, S::ink, Justification::centred);
}

void drawCaption (Graphics& g, const String& t, Rectangle<int> r, float size, Colour c, Justification j)
{
    g.setColour (c);
    g.setFont (Fonts::label (size));
    g.drawFittedText (::racoon::i18n::tr (t), r, j, 1, 0.8f);
}
}

#include "HarmonizePanel.h"
#include "Widgets.h"
#include "engine/I18n.h"
#include "engine/Genres.h"

namespace racoon::ui
{
using namespace juce;

namespace
{
// a soft colour for each number of the wheel (the usual rainbow of the Camelot wheel, toned down for the dark card)
Colour wheelColour (int pos, bool major)
{
    const float hue = std::fmod (0.33f - (float) pos / 12.0f + 1.0f, 1.0f);
    return Colour::fromHSV (hue, major ? 0.42f : 0.55f, major ? 0.62f : 0.5f, 1.0f);
}

String shortName (int root, bool major)
{
    static const char* es[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return String::fromUTF8 (es[((root % 12) + 12) % 12]) + (major ? " mayor" : " menor");
}
}

int HarmonizePanel::rootOf (int number, bool major)
{
    // 8A is A minor and 8B is C major; each number up is a fifth up
    const int base = major ? 0 : 9;
    return ((base + (number - 8) * 7) % 12 + 12) % 12;
}

int HarmonizePanel::numberOf (int root, bool major)
{
    for (int n = 1; n <= 12; ++n) if (rootOf (n, major) == ((root % 12) + 12) % 12) return n;
    return 8;
}

HarmonizePanel::HarmonizePanel (int r, bool m, bool known) : currentRootPc (((r % 12) + 12) % 12), currentIsMajor (m), currentKnown (known)
{
    setWantsKeyboardFocus (true);
    keep.setButtonText (String::fromUTF8 ("Armonizar en esta tonalidad"));
    cancel.setButtonText (String::fromUTF8 ("Cancelar"));
    keep.setEnabled (false);
    keep.onClick = [this] { if (chosen && onKeep) onKeep (chosenRootPc, chosenIsMajor); };
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (keep);
    addAndMakeVisible (cancel);
}

void HarmonizePanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (jmin (getWidth() - 32, 720), jmin (getHeight() - 32, 760));
    auto r = card.reduced (28, 22);
    auto bottom = r.removeFromBottom (44);
    cancel.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (10);
    keep.setBounds (bottom.removeFromRight (270));
    r.removeFromTop (96);
    r.removeFromBottom (10);
    const float size = (float) jmin (r.getWidth(), r.getHeight());
    centre = r.toFloat().getCentre();
    outerR = size * 0.5f;
    midR = outerR * 0.66f;
    innerR = outerR * 0.34f;
}

Path HarmonizePanel::segment (int ring, int pos) const
{
    // number 12 at the top, going clockwise
    const float step = MathConstants<float>::twoPi / 12.0f;
    const float a0 = (float) pos * step - step * 0.5f, a1 = a0 + step;
    const float ro = ring == 1 ? outerR : midR, ri = ring == 1 ? midR : innerR;
    Path p;
    p.addCentredArc (centre.x, centre.y, ro, ro, 0.0f, a0, a1, true);
    p.addCentredArc (centre.x, centre.y, ri, ri, 0.0f, a1, a0, false);
    p.closeSubPath();
    return p;
}

Point<float> HarmonizePanel::centreOf (int root, bool major) const
{
    const int pos = numberOf (root, major) % 12;
    const float a = (float) pos * MathConstants<float>::twoPi / 12.0f;
    const float rr = major ? (outerR + midR) * 0.5f : (midR + innerR) * 0.5f;
    return { centre.x + rr * std::sin (a), centre.y - rr * std::cos (a) };
}

int HarmonizePanel::hitTest (Point<float> p) const
{
    const float d = p.getDistanceFrom (centre);
    if (d < innerR || d > outerR) return -1;
    const int ring = d >= midR ? 1 : 0;
    float a = std::atan2 (p.x - centre.x, centre.y - p.y);
    if (a < 0.0f) a += MathConstants<float>::twoPi;
    const int pos = (int) std::floor (a / (MathConstants<float>::twoPi / 12.0f) + 0.5f) % 12;
    return ring * 12 + pos;
}

void HarmonizePanel::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1f1a1c).withAlpha (0.82f));
    Draw::panel (g, card.toFloat(), {}, false);
    auto r = card.reduced (28, 22);
    g.setColour (S::ink);
    g.setFont (Fonts::title (30.0f));
    g.drawText (::racoon::i18n::tr ("Armonizar"), r.removeFromTop (40), Justification::centredLeft);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::body (14.0f));
    String line = ::racoon::i18n::tr (String::fromUTF8 ("Toca una tonalidad: los instrumentos, los loops y los plugins se van a ella en ese momento (si la rola suena, la oyes). "
                                                        "La batería y el ritmo no cambian. Afuera las mayores (B), adentro las menores (A)."));
    if (currentKnown)
        line << " " << ::racoon::i18n::tr (String::fromUTF8 ("Ahora la rola está en ") + String (numberOf (currentRootPc, currentIsMajor)) + (currentIsMajor ? "B" : "A")
                                           + ", " + shortName (currentRootPc, currentIsMajor) + ".");
    g.drawFittedText (line, r.removeFromTop (52), Justification::topLeft, 3);

    for (int ring = 0; ring < 2; ++ring)
        for (int pos = 0; pos < 12; ++pos)
        {
            const bool major = ring == 1;
            const int number = pos == 0 ? 12 : pos;
            const int root = rootOf (number, major);
            const bool isChosen = chosen && chosenRootPc == root && chosenIsMajor == major;
            const bool isCurrent = currentKnown && currentRootPc == root && currentIsMajor == major;
            const bool hover = hoverPos == ring * 12 + pos;
            auto path = segment (ring, pos);
            auto col = wheelColour (pos, major);
            if (hover) col = col.brighter (0.25f);
            if (isChosen) col = Brand::limon;
            g.setColour (col);
            g.fillPath (path);
            g.setColour (Colour (0xff1f1a1c));
            g.strokePath (path, PathStrokeType (2.0f));
            if (isCurrent && ! isChosen)
            {
                g.setColour (Colours::white);
                g.strokePath (path, PathStrokeType (3.0f));
            }
            // the number, big, and the name under it
            const float a = (float) pos * MathConstants<float>::twoPi / 12.0f;
            const float rr = major ? (outerR + midR) * 0.5f : (midR + innerR) * 0.5f;
            const Point<float> c (centre.x + rr * std::sin (a), centre.y - rr * std::cos (a));
            g.setColour (isChosen ? Colour (0xff1f1a1c) : Colours::white);
            g.setFont (Fonts::title (major ? 19.0f : 16.0f));
            g.drawText (String (number) + (major ? "B" : "A"), Rectangle<float> (c.x - 40.0f, c.y - 19.0f, 80.0f, 22.0f), Justification::centred);
            g.setFont (Fonts::body (major ? 12.0f : 10.5f));
            g.drawText (::racoon::i18n::tr (shortName (root, major)), Rectangle<float> (c.x - 44.0f, c.y + 2.0f, 88.0f, 16.0f), Justification::centred);
        }
    // the middle: what is sounding
    g.setColour (S::ink);
    g.setFont (Fonts::title (17.0f));
    const String mid = chosen ? String (numberOf (chosenRootPc, chosenIsMajor)) + (chosenIsMajor ? "B" : "A") : String::fromUTF8 ("Elige");
    g.drawText (::racoon::i18n::tr (mid), Rectangle<float> (centre.x - innerR, centre.y - 20.0f, innerR * 2.0f, 22.0f), Justification::centred);
    g.setFont (Fonts::body (12.0f));
    g.setColour (S::inkSoft);
    g.drawFittedText (::racoon::i18n::tr (chosen ? shortName (chosenRootPc, chosenIsMajor) : String::fromUTF8 ("una tonalidad"))
                      , Rectangle<int> ((int) (centre.x - innerR), (int) centre.y + 2, (int) (innerR * 2.0f), 32), Justification::centredTop, 2);
}

void HarmonizePanel::tryKey (int root, bool major)
{
    chosen = true;
    chosenRootPc = ((root % 12) + 12) % 12;
    chosenIsMajor = major;
    keep.setEnabled (true);
    repaint();
    if (onTry) onTry (chosenRootPc, chosenIsMajor);
}

void HarmonizePanel::mouseDown (const MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    const bool major = h >= 12;
    const int pos = h % 12;
    tryKey (rootOf (pos == 0 ? 12 : pos, major), major);
}

void HarmonizePanel::mouseMove (const MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hoverPos) { hoverPos = h; repaint(); }
}

bool HarmonizePanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (k == KeyPress::returnKey && chosen) { if (onKeep) onKeep (chosenRootPc, chosenIsMajor); return true; }
    return false;   // space and the rest reach the song (Play)
}
}

#include "LanguagePicker.h"
#include "Widgets.h"

namespace racoon::ui
{
using namespace juce;

LanguagePicker::LanguagePicker()
{
    setWantsKeyboardFocus (true);
    spanish.onClick = [this] { if (onPick) onPick (false); };
    english.onClick = [this] { if (onPick) onPick (true); };
    for (auto* b : { &spanish, &english }) addAndMakeVisible (b);
}

void LanguagePicker::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (jmin (getWidth() - 32, 620), jmin (getHeight() - 32, 250));
    auto r = card.reduced (32, 28);
    r.removeFromTop (150);
    auto row = r.removeFromTop (52);
    const int w = (row.getWidth() - 16) / 2;
    spanish.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (16);
    english.setBounds (row);
}

void LanguagePicker::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1f1a1c).withAlpha (0.82f));
    Draw::panel (g, card.toFloat(), {}, false);
    auto r = card.reduced (32, 28);
    g.setColour (S::ink);
    g.setFont (Fonts::title (28.0f));
    g.drawText (String::fromUTF8 ("¿En qué idioma quieres usar Racoon?"), r.removeFromTop (44), Justification::centredLeft);
    g.setColour (S::inkSoft);
    g.drawText ("Which language do you want to use?", r.removeFromTop (40), Justification::centredLeft);
    g.setFont (Fonts::body (14.0f));
    g.drawFittedText (String::fromUTF8 ("Puedes cambiarlo después en el menú del engrane.\nYou can change it later from the gear menu."),
                      r.removeFromTop (54), Justification::centredLeft, 2);
}
}

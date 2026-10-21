#include "GenrePicker.h"
#include "engine/I18n.h"
#include "Widgets.h"

namespace racoon::ui
{
using namespace juce;

GenrePicker::GenrePicker()
{
    setWantsKeyboardFocus (true);
    families = genreFamilies();
    for (auto& g : genres())
    {
        auto* b = buttons.add (new TextButton (String::fromUTF8 (g.name) + "   " + String (roundToInt (g.bpm))));
        b->setTooltip (String::fromUTF8 (g.blurb) + "  " + keyName (g.key, g.major) + " (" + camelot (g.key, g.major) + ")");
        const String id (g.id);
        b->onClick = [this, id] { if (onPick) onPick (id); };
        ids.add (id);
        addAndMakeVisible (b);
    }
    empty.setButtonText (String::fromUTF8 ("Proyecto vacío"));
    last.setButtonText (String::fromUTF8 ("Abrir el último proyecto"));
    surprise.setButtonText ("ALEATORIA");
    surprise.setTooltip (String::fromUTF8 ("Elige un género al azar y arma su bloque"));
    empty.onClick = [this] { if (onEmpty) onEmpty(); };
    last.onClick = [this] { if (onOpenLast) onOpenLast(); };
    close.onClick = [this] { if (onClose) onClose(); };
    surprise.onClick = [this] { if (onPick) onPick (ids[Random::getSystemRandom().nextInt (ids.size())]); };
    const File lastFile (prefs().getValue ("lastProject"));
    last.setEnabled (lastFile.existsAsFile());
    for (auto* b : { &empty, &last, &surprise, &close }) addAndMakeVisible (b);
}

Button* GenrePicker::buttonFor (const String& id)
{
    const int i = ids.indexOf (id);
    return i >= 0 ? buttons[i] : nullptr;
}

void GenrePicker::setBusy (const String& name)
{
    busy = String::fromUTF8 ("Armando tu bloque de ") + name + "...";
    for (auto* b : buttons) b->setEnabled (false);
    for (auto* b : { &empty, &last, &surprise, &close }) b->setEnabled (false);
    repaint();
}

bool GenrePicker::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey && onClose) { onClose(); return true; }
    return true;   // the song behind does not get keys while choosing
}

void GenrePicker::resized()
{
    card = getLocalBounds().reduced (jmax (16, getWidth() / 20), jmax (16, getHeight() / 16));
    auto r = card.reduced (28, 22);
    close.setBounds (card.getRight() - 46, card.getY() + 12, 34, 30);
    r.removeFromTop (92);                                         // title and line under it
    auto bottom = r.removeFromBottom (40);
    surprise.setBounds (bottom.removeFromLeft (170));
    bottom.removeFromLeft (10);
    empty.setBounds (bottom.removeFromLeft (170));
    bottom.removeFromLeft (10);
    last.setBounds (bottom.removeFromLeft (230));
    r.removeFromBottom (16);

    // one column per family; buttons stacked under each name
    captions.clear();
    const int cols = families.size();
    const int colW = r.getWidth() / jmax (1, cols);
    const int btnH = jlimit (26, 36, (r.getHeight() - 24) / 9);
    for (int c = 0; c < cols; ++c)
    {
        auto col = r.withX (r.getX() + c * colW).withWidth (colW - 10);
        captions.push_back ({ families[c], col.removeFromTop (22) });
        col.removeFromTop (4);
        int i = 0;
        for (auto& g : genres())
        {
            if (String::fromUTF8 (g.family) != families[c]) { ++i; continue; }
            buttons[i]->setBounds (col.removeFromTop (btnH).reduced (0, 2));
            ++i;
        }
    }
}

void GenrePicker::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1f1a1c).withAlpha (0.82f));
    Draw::panel (g, card.toFloat(), {}, false);
    auto r = card.reduced (28, 22);
    g.setColour (S::ink);
    g.setFont (Fonts::title (34.0f));
    g.drawText (::racoon::i18n::tr (String::fromUTF8 ("¿Qué vamos a producir?")), r.removeFromTop (46), Justification::centredLeft);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::body (15.0f));
    g.drawFittedText (::racoon::i18n::tr (busy.isNotEmpty() ? busy
                                        : String::fromUTF8 ("Elige un género y Racoon arma un bloque de 16 compases en ese estilo: batería, bajo, pads, acordes, "
                                                            "melodía, guitarra, coro, percusión, un loop y una voz, cada uno en su canal. Pasa el ratón por un género para saber cómo suena.")),
                      r.removeFromTop (40), Justification::centredLeft, 2);
    g.setFont (Fonts::label (13.0f));
    for (auto& cap : captions)
    {
        auto a = cap.second;
        g.setColour (S::ink);
        g.drawText (::racoon::i18n::tr (cap.first.toUpperCase()), a.withTrimmedBottom (4), Justification::bottomLeft);
        g.fillRect (a.removeFromBottom (2));
    }
}
}

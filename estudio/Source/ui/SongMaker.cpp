#include "SongMaker.h"
#include "engine/I18n.h"

using namespace juce;

namespace racoon::ui
{
namespace
{
Colour colourFor (arranger::Part p)
{
    switch (p)
    {
        case arranger::Part::intro:     return Brand::menta.withAlpha (0.55f);
        case arranger::Part::build:     return Brand::limon.withAlpha (0.55f);
        case arranger::Part::drop:      return Brand::rosa;
        case arranger::Part::breakdown: return Brand::hielo.withAlpha (0.55f);
        case arranger::Part::build2:    return Brand::limon.withAlpha (0.75f);
        case arranger::Part::drop2:     return Brand::rosa.brighter (0.2f);
        case arranger::Part::outro:     return Brand::menta.withAlpha (0.4f);
    }
    return C::ash;
}
}

SongMakerPanel::SongMakerPanel (Session& s, Array<te::AudioTrack*> t, double a, double b)
    : session (s), tracks (std::move (t)), start (a), end (b)
{
    found = arranger::describe (session, tracks, start, end);
    const double bpm = session.getBpm();
    const double lo = arranger::minutesForBars (arranger::minBars(), bpm), hi = arranger::minutesForBars (arranger::maxBars(), bpm);
    const double step = arranger::minutesForBars (arranger::phraseBars, bpm);
    minutes.setSliderStyle (Slider::LinearHorizontal);
    minutes.setRange (lo, hi, 0.0);
    minutes.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    minutes.setDoubleClickReturnValue (true, arranger::minutesForBars (224, bpm));
    minutes.onValueChange = [this, step, lo]
    {
        // whole phrases only: the knob jumps 8 bars at a time
        const double snapped = lo + std::round ((minutes.getValue() - lo) / step) * step;
        if (std::abs (snapped - minutes.getValue()) > 1.0e-9) minutes.setValue (snapped, dontSendNotification);
        repaint();
    };
    setMinutes (arranger::minutesForBars (224, bpm));
    addAndMakeVisible (minutes);
    transitions.setToggleState (true, dontSendNotification);
    sweep.setToggleState (true, dontSendNotification);
    addAndMakeVisible (transitions);
    addAndMakeVisible (sweep);
    addAndMakeVisible (createButton);
    addAndMakeVisible (cancelButton);
    createButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    setSize (760, 520);
}

int SongMakerPanel::totalBars() const
{
    return arranger::barsForMinutes (minutes.getValue(), session.getBpm());
}

void SongMakerPanel::setMinutes (double m)
{
    minutes.setValue (arranger::minutesForBars (arranger::barsForMinutes (m, session.getBpm()), session.getBpm()), sendNotificationSync);
}

arranger::Result SongMakerPanel::create()
{
    arranger::Options o;
    o.totalBars = totalBars();
    o.transitions = transitions.getToggleState();
    o.sweep = sweep.getToggleState();
    return arranger::build (session, tracks, start, end, o);
}

void SongMakerPanel::accept()
{
    auto r = create();
    auto done = onDone;
    auto close = onClose;
    if (close) close();                 // the window (and this panel) may be gone after this
    if (done) done (r);
}

void SongMakerPanel::resized()
{
    auto r = getLocalBounds().reduced (24);
    r.removeFromTop (44);
    auto top = r.removeFromTop (30);
    minutes.setBounds (top.withTrimmedLeft (130).withTrimmedRight (210));
    r.removeFromTop (12);
    planArea = r.removeFromTop (96);
    r.removeFromTop (14);
    auto buttons = r.removeFromBottom (36);
    createButton.setBounds (buttons.removeFromRight (170));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (120));
    r.removeFromBottom (12);
    sweep.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);
    transitions.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (10);
    foundArea = r;
}

void SongMakerPanel::paint (Graphics& g)
{
    g.fillAll (S::shade);
    Draw::box (g, getLocalBounds().toFloat().reduced (8.0f), S::card, 2.0f, 4.0f);
    g.setColour (S::ink);
    g.setFont (Fonts::title (22.0f));
    g.drawText (::racoon::i18n::tr (String::fromUTF8 ("Crear la rola completa")), 24, 18, 500, 30, Justification::centredLeft);

    const double bpm = session.getBpm();
    const int bars = totalBars();
    drawCaption (g, String::fromUTF8 ("DURACIÓN"), minutes.getBounds().withX (24).withWidth (110), 13.0f, S::inkSoft);
    g.setColour (S::ink);
    g.setFont (Fonts::mono (20.0f));
    g.drawText (::racoon::i18n::tr (arranger::clock (arranger::minutesForBars (bars, bpm) * 60.0) + "  ·  " + String (bars) + String::fromUTF8 (" compases")),
                minutes.getRight() + 12, minutes.getY(), getWidth() - minutes.getRight() - 36, minutes.getHeight(), Justification::centredLeft);

    // the sections to scale: name on top, the minute it starts below
    const auto secs = arranger::plan (bars);
    auto pa = planArea.toFloat();
    const float bandY = pa.getY() + 22.0f, bandH = 34.0f;
    for (auto& sec : secs)
    {
        const float x0 = pa.getX() + pa.getWidth() * (float) sec.startBar / (float) bars;
        const float x1 = pa.getX() + pa.getWidth() * (float) (sec.startBar + sec.bars) / (float) bars;
        auto box = Rectangle<float> (x0, bandY, x1 - x0, bandH).reduced (1.0f, 0.0f);
        g.setColour (colourFor (sec.part));
        g.fillRect (box);
        g.setColour (S::shade);
        g.setFont (Fonts::label (12.0f));
        g.drawFittedText (::racoon::i18n::tr (String (sec.bars)), box.toNearestInt().reduced (2, 0), Justification::centred, 1);
        g.setColour (S::ink);
        g.setFont (Fonts::label (11.5f));
        g.drawFittedText (::racoon::i18n::tr (sec.name), Rectangle<float> (x0, pa.getY(), x1 - x0, 20.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
        g.setColour (S::inkSoft);
        g.setFont (Fonts::mono (13.0f));
        g.drawText (::racoon::i18n::tr (arranger::clock (arranger::minutesForBars (sec.startBar, bpm) * 60.0)),
                    Rectangle<float> (x0, bandY + bandH + 3.0f, 60.0f, 16.0f), Justification::centredLeft);
    }
    g.setColour (S::inkSoft);
    g.setFont (Fonts::body (12.0f));
    g.drawText (::racoon::i18n::tr (String::fromUTF8 ("Cada número es de compases. Los cambios caen en el uno de una frase de 8, a ")
                    + String (bpm, 0) + " BPM y en 4/4."),
                planArea.withTrimmedTop (planArea.getHeight() - 16), Justification::centredLeft);

    // what the selection holds
    auto fa = foundArea;
    drawCaption (g, String::fromUTF8 ("LO QUE USA DE TU SELECCIÓN"), fa.removeFromTop (18), 13.0f, S::inkSoft);
    g.setColour (S::ink);
    g.setFont (Fonts::body (13.0f));
    String text;
    for (auto& f : found) text << "· " << f << "\n";
    if (found.isEmpty()) text = String::fromUTF8 ("Nada: la selección no tiene notas ni audio.");
    g.drawFittedText (::racoon::i18n::tr (text), fa.reduced (0, 2), Justification::topLeft, jmax (1, fa.getHeight() / 17));
}

bool SongMakerPanel::show (Session& s, Array<te::AudioTrack*> tracks, double a, double b,
                           std::function<void (const arranger::Result&)> done, String& why)
{
    if (tracks.isEmpty() || b <= a)
    {
        why = String::fromUTF8 ("Primero selecciona el loop: arrastra sobre la maqueta los compases y canales que quieres usar.");
        return false;
    }
    if (arranger::describe (s, tracks, a, b).isEmpty())
    {
        why = String::fromUTF8 ("La selección no tiene notas ni audio. Arrastra sobre los clips que quieres usar.");
        return false;
    }
    DialogWindow::LaunchOptions o;
    auto* p = new SongMakerPanel (s, tracks, a, b);
    p->onDone = std::move (done);
    o.content.setOwned (p);
    o.dialogTitle = ::racoon::i18n::tr (String::fromUTF8 ("Crear la rola completa"));
    o.dialogBackgroundColour = S::shade;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    auto* w = o.launchAsync();
    Component::SafePointer<DialogWindow> sw (w);
    p->onClose = [sw] { if (sw != nullptr) sw->exitModalState (0); };
    return true;
}
}

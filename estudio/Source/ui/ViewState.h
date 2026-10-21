#pragma once
#include "../engine/Session.h"
#include "Theme.h"

namespace racoon::ui
{
// Shared view settings for the timeline, the ruler and the clip editors.
struct ViewState : public juce::ChangeBroadcaster
{
    explicit ViewState (Session& s) : session (s) {}

    Session& session;
    te::Edit& edit() const                  { return session.getEdit(); }

    double pxPerBeat = 22.0;    // horizontal zoom
    double scrollBeats = 0.0;   // beat at the left edge of the lanes
    int headerWidth = 236;
    bool easyMode = true;

    te::Track* clipDragTarget = nullptr;   // lane highlighted while a clip is dragged across tracks

    // set by the main window
    std::function<void (te::Clip*)> openClipEditor;
    std::function<void (const juce::String&)> showHint;
    std::function<void (int root, bool minor)> setPianoScale;   // the Piano's scale helper (a detected key)
    std::function<void()> focusArrangement;
    std::function<void (te::Track&, te::AutomatableParameter&)> automateParameter;
    std::function<void (te::Track&)> trackChosen;
    std::function<void (te::Clip&)> clipChosen;
    std::function<void (juce::PopupMenu&)> addEditItems;   // Copiar, Cortar, Pegar, Duplicar, Borrar (with their shortcuts)     // one click on a clip: its channel below (unless its notes are already open)   // a click on a track name: show its channel   // shows that parameter's lane

    double timeToBeat (te::TimePosition t) const { return edit().tempoSequence.toBeats (t).inBeats(); }
    te::TimePosition beatToTime (double b) const { return edit().tempoSequence.toTime (te::BeatPosition::fromBeats (b)); }
    float beatToX (double b) const               { return (float) ((b - scrollBeats) * pxPerBeat); }
    double xToBeat (float x) const               { return scrollBeats + (double) x / pxPerBeat; }
    float timeToX (te::TimePosition t) const     { return beatToX (timeToBeat (t)); }
    te::TimePosition xToTime (float x) const     { return beatToTime (juce::jmax (0.0, xToBeat (x))); }

    int beatsPerBar() const                      { return session.getTimeSigNumerator(); }

    // snaps to the grid; holding Cmd / Ctrl (bypass) moves freely
    double snap (double beats, bool bypass = false) const
    {
        const double g = session.snapBeats;
        if (bypass || g <= 0.0) return beats;
        return std::round (beats / g) * g;
    }
    double snapFloor (double beats) const
    {
        const double g = session.snapBeats > 0.0 ? session.snapBeats : 0.25;
        return std::floor (beats / g) * g;
    }

    void setScroll (double b)                    { b = juce::jmax (0.0, b); if (b != scrollBeats) { scrollBeats = b; sendChangeMessage(); } }
    void zoomAround (double factor, float anchorX)
    {
        const double anchorBeat = xToBeat (anchorX);
        pxPerBeat = juce::jlimit (0.5, 400.0, pxPerBeat * factor);
        scrollBeats = juce::jmax (0.0, anchorBeat - (double) anchorX / pxPerBeat);
        sendChangeMessage();
    }

    // the range: a piece of time on one or more channels, selected by dragging (it can cut through clips)
    double rangeStart = 0.0, rangeEnd = 0.0;
    juce::Array<te::Track*> rangeTracks;
    bool hasRange() const { return rangeEnd > rangeStart && ! rangeTracks.isEmpty(); }
    void clearRange()     { if (hasRange() || ! rangeTracks.isEmpty()) { rangeTracks.clear(); rangeStart = rangeEnd = 0.0; sendChangeMessage(); } }
    juce::Array<te::AudioTrack*> rangeAudioTracks() const
    {
        juce::Array<te::AudioTrack*> out;
        for (auto* t : rangeTracks) if (auto* a = dynamic_cast<te::AudioTrack*> (t)) out.add (a);
        return out;
    }

    // snapping on the timeline follows the lines you see (zoomed out: bars; zoomed in: down to the Rejilla value);
    // Cmd / Ctrl moves freely
    double snapToView (double beats, bool bypass = false) const
    {
        if (bypass) return beats;
        const double g = gridStep();
        return std::round (beats / g) * g;
    }
    // 5.2.3 = bar 5, beat 2, sixteenth 3
    static juce::String positionText (double beats, int beatsPerBar = 4)
    {
        beats = juce::jmax (0.0, beats + 1.0e-6);
        const int bar = (int) (beats / beatsPerBar) + 1;
        const double inBar = std::fmod (beats, (double) beatsPerBar);
        const int beat = (int) inBar + 1;
        const int six = (int) ((inBar - std::floor (inBar)) * 4.0) + 1;
        return juce::String (bar) + "." + juce::String (beat) + "." + juce::String (six);
    }

    // grid step that keeps lines at least ~10 px apart
    double gridStep() const
    {
        double step = session.snapBeats > 0.0 ? session.snapBeats : 0.25;
        while (step * pxPerBeat < 10.0) step *= 2.0;
        return step;
    }
};

// The tracks the arrangement shows, in order, with their nesting depth (groups)
struct VisibleTrack { te::Track* track; int depth; };
inline juce::Array<VisibleTrack> visibleTracks (te::Edit& e)
{
    juce::Array<VisibleTrack> out;
    for (auto* t : te::getAllTracks (e))
    {
        if (dynamic_cast<te::AudioTrack*> (t) == nullptr && dynamic_cast<te::FolderTrack*> (t) == nullptr)
            continue;
        int depth = 0;
        bool hidden = false;
        for (auto* f = t->getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
        {
            ++depth;
            if (f->state.getProperty ("racoonFolded", false)) hidden = true;
        }
        if (! hidden) out.add ({ t, depth });
    }
    return out;
}

// draws the instrument / animal glyph used on track headers and browser cards
void drawAnimalBadge (juce::Graphics&, juce::Rectangle<float>, const juce::String& productOrName, juce::Colour fill);
// right click on a fader, a pan or a send: see or erase its automation line
inline void showAutomationMenu (ViewState& vs, te::Track& t, te::AutomatableParameter* p, juce::Component& target)
{
    if (p == nullptr) return;
    te::Track::Ptr keepT (&t);
    te::AutomatableParameter::Ptr keepP (p);
    juce::PopupMenu m;
    m.addSectionHeader (t.getName() + ": " + p->getParameterName());
    m.addItem (juce::String::fromUTF8 ("Ver automatización"), [&vs, keepT, keepP] { if (vs.automateParameter) vs.automateParameter (*keepT, *keepP); });
    if (p->getCurve().getNumPoints() > 0)
        m.addItem (juce::String::fromUTF8 ("Borrar su automatización"), [&vs, keepP]
        {
            vs.edit().getUndoManager().beginNewTransaction();
            keepP->getCurve().clear();
        });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}
}

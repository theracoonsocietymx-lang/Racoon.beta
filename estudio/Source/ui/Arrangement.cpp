#include "Arrangement.h"
#include "engine/I18n.h"
#include "PianoRoll.h"
#include "engine/Devices.h"
#include "engine/MidiCapture.h"
#include "ChannelView.h"
#include "Menus.h"
#include "NoteTools.h"
#include "engine/AudioTools.h"
#include "engine/Stems.h"

namespace racoon::ui
{
using namespace juce;

namespace
{
te::AudioTrack* asAudio (te::Track* t) { return dynamic_cast<te::AudioTrack*> (t); }

bool isCmd (const ModifierKeys& m) { return m.isCommandDown(); }

String askForText (const String& title, const String& label, const String& current)
{
    AlertWindow w (title, {}, MessageBoxIconType::NoIcon);
    w.addTextEditor ("t", current, label);
    w.addButton ("ACEPTAR", 1, KeyPress (KeyPress::returnKey));
    w.addButton ("CANCELAR", 0, KeyPress (KeyPress::escapeKey));
    if (w.runModalLoop() == 1)
        return w.getTextEditorContents ("t").trim();
    return {};
}

void drawGrid (Graphics& g, ViewState& vs, Rectangle<int> area, bool strongBars)
{
    // soft lines: bars clear, beats faint, finer steps only when there is room between them
    const double step = vs.gridStep();
    const int bpb = jmax (1, vs.beatsPerBar());
    const float stepPx = vs.beatToX (step) - vs.beatToX (0.0);
    const float beatPx = vs.beatToX (1.0) - vs.beatToX (0.0);
    const double first = std::floor (vs.xToBeat (0.0f) / step) * step;
    for (double b = first; ; b += step)
    {
        const float x = vs.beatToX (b);
        if (x > (float) area.getRight()) break;
        if (x < 0.0f) continue;
        const bool bar = std::abs (std::fmod (b, (double) bpb)) < 1e-6;
        const bool beat = std::abs (b - std::round (b)) < 1e-6;
        float alpha = 0.0f;
        if (bar)       alpha = strongBars ? 0.20f : 0.14f;
        else if (beat) alpha = beatPx >= 14.0f ? 0.075f : 0.0f;
        else           alpha = stepPx >= 12.0f ? 0.035f : 0.0f;
        if (alpha <= 0.0f) continue;
        g.setColour (C::bone.withAlpha (alpha));
        g.fillRect ((float) area.getX() + x, (float) area.getY(), 1.0f, (float) area.getHeight());
    }
}
}

// ===================================================================== ruler
Ruler::Ruler (ViewState& v) : vs (v)
{
    setTooltip (String::fromUTF8 ("Franja de arriba: arrastra para marcar el loop, doble clic lo activa. "
                                  "Abajo: haz clic o arrastra para mover el cursor. Clic derecho: marcadores."));
}

void Ruler::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (C::basalt);
    auto& tr = vs.session.getTransport();

    // loop brace
    auto strip = r.removeFromTop (loopStripHeight);
    g.setColour (C::engrave);
    g.fillRect (strip);
    auto loop = tr.getLoopRange();
    const float x1 = vs.timeToX (loop.getStart()), x2 = vs.timeToX (loop.getEnd());
    if (x2 > x1)
    {
        auto lr = Rectangle<float> (x1, (float) strip.getY() + 2.0f, x2 - x1, (float) strip.getHeight() - 4.0f);
        g.setColour (tr.looping ? Brand::rosa : C::ash);
        g.fillRect (lr);
        g.setColour (S::ink);
        g.drawRect (lr, 1.0f);
        if (lr.getWidth() > 40.0f)
        {
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr (tr.looping ? "LOOP" : "LOOP (APAGADO)"), lr.reduced (6.0f, 0.0f), Justification::centredLeft);
        }
    }

    // bars and beats
    const int bpb = jmax (1, vs.beatsPerBar());
    double barStep = 1.0;
    while (barStep * bpb * vs.pxPerBeat < 46.0) barStep *= 2.0;
    const double firstBar = std::floor (vs.xToBeat (0.0f) / bpb);
    g.setFont (Fonts::mono (12.0f));
    for (double bar = firstBar; ; bar += 1.0)
    {
        const float x = vs.beatToX (bar * bpb);
        if (x > (float) getWidth()) break;
        const bool labelled = std::fmod (bar, barStep) == 0.0;
        g.setColour (labelled ? C::boneDim : C::ash);
        g.fillRect (x, (float) r.getY() + (labelled ? 0.0f : (float) r.getHeight() * 0.55f), 1.0f, (float) r.getHeight());
        // a marker on this bar puts its name there instead of the number
        bool marked = false;
        for (auto& m : vs.edit().getMarkerManager().getMarkers())
            if (std::abs (vs.timeToX (m->getPosition().getStart()) - x) < 2.0f) marked = true;
        if (labelled && x >= -30.0f && ! marked)
        {
            g.setColour (C::bone);
            g.drawText (::racoon::i18n::tr (String ((int) bar + 1)), Rectangle<float> (x + 4.0f, (float) r.getY(), 40.0f, (float) r.getHeight() * 0.6f), Justification::centredLeft);
        }
        if (vs.pxPerBeat >= 12.0)
            for (int b = 1; b < bpb; ++b)
            {
                g.setColour (C::ash);
                g.fillRect (vs.beatToX (bar * bpb + b), (float) r.getBottom() - 6.0f, 1.0f, 6.0f);
            }
    }

    // markers
    for (auto& m : vs.edit().getMarkerManager().getMarkers())
    {
        const float x = vs.timeToX (m->getPosition().getStart());
        g.setColour (Brand::hielo);
        Path flag;
        flag.addTriangle (x, (float) r.getY(), x + 8.0f, (float) r.getY() + 5.0f, x, (float) r.getY() + 10.0f);
        g.fillPath (flag);
        g.fillRect (x, (float) r.getY(), 1.5f, (float) r.getHeight());
        g.setFont (Fonts::label (11.0f));
        const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), ::racoon::i18n::tr (m->getName())) + 6.0f;
        auto lr = Rectangle<float> (x + 9.0f, (float) r.getY(), w, 13.0f);
        g.setColour (C::basalt);
        g.fillRect (lr);
        g.setColour (Brand::hielo);
        g.drawText (::racoon::i18n::tr (m->getName()), lr.withTrimmedLeft (2.0f), Justification::centredLeft);
    }

    g.setColour (C::stoneHi);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

// ===================================================================== time ruler
TimeRuler::TimeRuler (ViewState& v) : vs (v)
{
    setTooltip (String::fromUTF8 ("Tiempo en minutos y segundos. Haz clic o arrastra para mover el cursor."));
}

double TimeRuler::stepFor (double pxPerSecond)
{
    static const double steps[] { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    for (auto st : steps) if (st * pxPerSecond >= 70.0) return st;
    return 1200.0;
}

String TimeRuler::format (double seconds, double step)
{
    seconds = jmax (0.0, seconds);
    if (step < 1.0)
    {
        const int tenths = roundToInt (seconds * 10.0);
        const int m = tenths / 600, sec = (tenths / 10) % 60, t = tenths % 10;
        return String (m) + ":" + String (sec).paddedLeft ('0', 2) + "." + String (t);
    }
    const int total = roundToInt (seconds);
    return String (total / 60) + ":" + String (total % 60).paddedLeft ('0', 2);
}

void TimeRuler::paint (Graphics& g)
{
    g.fillAll (C::basalt);
    g.setColour (C::stoneHi);
    g.fillRect (0, 0, getWidth(), 1);
    const double t0 = vs.xToTime (0.0f).inSeconds(), t1 = vs.xToTime ((float) getWidth()).inSeconds();
    if (t1 <= t0) return;
    // the local scale where the view starts; with tempo changes later on the spacing still comes out readable
    const double pxPerSec = (double) getWidth() / jmax (1.0e-6, t1 - t0);
    const double step = stepFor (pxPerSec);
    const double minor = step / (step >= 60.0 || step == 15.0 || step == 0.25 ? 3.0 : (step == 2.0 ? 4.0 : 5.0));
    g.setFont (Fonts::mono (11.0f));
    for (double t = std::floor (t0 / minor) * minor; t <= t1 + minor; t += minor)
    {
        if (t < 0.0) continue;
        const float x = vs.timeToX (te::TimePosition::fromSeconds (t));
        const bool major = std::abs (std::remainder (t, step)) < minor * 0.01;
        g.setColour (major ? C::boneDim : C::ash);
        g.fillRect (x, major ? 1.0f : (float) getHeight() - 5.0f, 1.0f, major ? (float) getHeight() : 5.0f);
        if (major)
        {
            g.setColour (C::bone);
            g.drawText (::racoon::i18n::tr (format (t, step)), Rectangle<float> (x + 4.0f, 2.0f, 70.0f, (float) getHeight() - 4.0f), Justification::centredLeft);
        }
    }
}

void TimeRuler::mouseDown (const MouseEvent& e)
{
    auto& tr = vs.session.getTransport();
    tr.setUserDragging (true);
    tr.setPosition (vs.xToTime ((float) e.x));
}

void TimeRuler::mouseDrag (const MouseEvent& e) { vs.session.getTransport().setPosition (vs.xToTime ((float) jmax (0, e.x))); }
void TimeRuler::mouseUp (const MouseEvent&)     { vs.session.getTransport().setUserDragging (false); }

void Ruler::mouseMove (const MouseEvent& e)
{
    if (e.y < loopStripHeight)
    {
        auto loop = vs.session.getTransport().getLoopRange();
        const float x1 = vs.timeToX (loop.getStart()), x2 = vs.timeToX (loop.getEnd());
        if (std::abs ((float) e.x - x1) < 6.0f || std::abs ((float) e.x - x2) < 6.0f)
            setMouseCursor (MouseCursor::LeftRightResizeCursor);
        else if ((float) e.x > x1 && (float) e.x < x2)
            setMouseCursor (MouseCursor::DraggingHandCursor);
        else
            setMouseCursor (MouseCursor::NormalCursor);
    }
    else setMouseCursor (MouseCursor::IBeamCursor);
}

void Ruler::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu (e); return; }
    auto& tr = vs.session.getTransport();
    const double beat = vs.snap (vs.xToBeat ((float) e.x), isCmd (e.mods));
    dragStartBeat = beat;

    if (e.y < loopStripHeight)
    {
        auto loop = tr.getLoopRange();
        loopStartAtDown = vs.timeToBeat (loop.getStart());
        loopEndAtDown = vs.timeToBeat (loop.getEnd());
        const float x1 = vs.beatToX (loopStartAtDown), x2 = vs.beatToX (loopEndAtDown);
        if (std::abs ((float) e.x - x1) < 6.0f) drag = Drag::loopStart;
        else if (std::abs ((float) e.x - x2) < 6.0f) drag = Drag::loopEnd;
        else if ((float) e.x > x1 && (float) e.x < x2) drag = Drag::loopMove;
        else drag = Drag::loopNew;
        vs.edit().getUndoManager().beginNewTransaction();
    }
    else
    {
        drag = Drag::scrub;
        tr.setUserDragging (true);
        tr.setPosition (vs.beatToTime (jmax (0.0, beat)));
    }
}

void Ruler::mouseDrag (const MouseEvent& e)
{
    auto& tr = vs.session.getTransport();
    const double beat = jmax (0.0, vs.snap (vs.xToBeat ((float) e.x), isCmd (e.mods)));
    auto setLoop = [&] (double a, double b)
    {
        if (b - a < 0.25) return;
        tr.setLoopRange ({ vs.beatToTime (a), vs.beatToTime (b) });
        tr.looping = true;
    };
    switch (drag)
    {
        case Drag::scrub:     tr.setPosition (vs.beatToTime (beat)); break;
        case Drag::loopNew:   setLoop (jmin (dragStartBeat, beat), jmax (dragStartBeat, beat)); break;
        case Drag::loopStart: setLoop (jmin (beat, loopEndAtDown - 0.25), loopEndAtDown); break;
        case Drag::loopEnd:   setLoop (loopStartAtDown, jmax (beat, loopStartAtDown + 0.25)); break;
        case Drag::loopMove:
        {
            const double d = jmax (-loopStartAtDown, beat - dragStartBeat);
            setLoop (loopStartAtDown + d, loopEndAtDown + d);
            break;
        }
        case Drag::none: break;
    }
    repaint();
}

void Ruler::mouseUp (const MouseEvent&)
{
    if (drag == Drag::scrub) vs.session.getTransport().setUserDragging (false);
    drag = Drag::none;
}

void Ruler::mouseDoubleClick (const MouseEvent& e)
{
    if (e.y < loopStripHeight)
    {
        auto& tr = vs.session.getTransport();
        tr.looping = ! tr.looping;
        repaint();
    }
}

void Ruler::showMenu (const MouseEvent& e)
{
    const auto t = vs.beatToTime (jmax (0.0, vs.snap (vs.xToBeat ((float) e.x))));
    auto& mm = vs.edit().getMarkerManager();
    PopupMenu m;
    m.addSectionHeader ("Marcadores");
    m.addItem (String::fromUTF8 ("Añadir marcador aquí"), [this, t, &mm]
    {
        const int n = mm.getMarkers().size() + 1;
        if (auto mk = mm.createMarker (n, t, vs.session.barLength(), nullptr))
            mk->setName ("Parte " + String (n));
        repaint();
    });
    PopupMenu go;
    for (auto& mk : mm.getMarkers())
    {
        auto pos = mk->getPosition().getStart();
        go.addItem (mk->getName(), [this, pos] { vs.session.getTransport().setPosition (pos); });
    }
    m.addSubMenu ("Ir a", go, go.getNumItems() > 0);
    m.addItem ("Borrar marcadores", mm.getMarkers().size() > 0, false, [this, &mm]
    {
        for (auto& mk : mm.getMarkers()) mk->removeFromParent();
        repaint();
    });
    m.addSectionHeader ("Loop");
    m.addItem (String::fromUTF8 ("Loop de 4 compases desde aquí"), [this, t]
    {
        vs.session.setLoopRange ({ t, t + vs.session.barLength() * 4.0 });
        vs.session.setLooping (true);
        repaint();
    });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

// ===================================================================== clip
ClipComponent::ClipComponent (ViewState& v, te::Clip::Ptr c) : clip (c), vs (v)
{
    updateThumbnail();
    setTooltip (String::fromUTF8 ("Arrastra para mover (Alt duplica, Cmd sin rejilla). Bordes: recortar. "
                                  "Doble clic: editar. Clic derecho: más opciones."));
}

ClipComponent::~ClipComponent() = default;

void ClipComponent::updateThumbnail()
{
    if (auto* wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
    {
        te::AudioFile af (wac->getAudioFile());
        if (af.isValid())
        {
            const te::AudioFile proxy (wac->getPlaybackFile());
            if (thumbnail == nullptr) thumbnail = std::make_unique<te::SmartThumbnail> (wac->edit.engine, proxy, *this, &wac->edit);
            else thumbnail->setNewFile (proxy);
        }
    }
}

void ClipComponent::drawWaveform (Graphics& g, Rectangle<int> area)
{
    auto* c = dynamic_cast<te::AudioClipBase*> (clip.get());
    if (c == nullptr || thumbnail == nullptr) return;
    if (thumbnail->isGeneratingProxy() || thumbnail->isOutOfDate())
    {
        g.setColour (S::shade.withAlpha (0.5f));
        g.setFont (Fonts::body (12.0f));
        g.drawText (::racoon::i18n::tr ("Procesando audio..."), area, Justification::centred);
        return;
    }

    auto pos = c->getPosition();
    auto drawStart = pos.getStart();
    auto drawEnd = pos.getEnd();
    // source time for the visible part, ignoring warping (proxy files already carry it)
    te::TimeRange src;
    if (c->usesTimeStretchedProxy())
        src = { te::TimePosition() , te::TimePosition() + (drawEnd - drawStart) };
    else
    {
        const auto ratio = c->getSpeedRatio();
        src = { te::TimePosition() + pos.getOffset() * ratio, te::TimePosition() + (pos.getOffset() + (drawEnd - drawStart)) * ratio };
    }
    g.setColour (S::shade.withAlpha (0.78f));
    const float gain = te::dbToGain (c->getGainDB());
    if (thumbnail->getNumChannels() > 1 && area.getHeight() > 30)
    {
        auto top = area.removeFromTop (area.getHeight() / 2);
        thumbnail->drawChannel (g, top, src, 0, gain);
        thumbnail->drawChannel (g, area, src, 1, gain);
    }
    else
        thumbnail->drawChannel (g, area, src, 0, gain);
}

void ClipComponent::drawNotes (Graphics& g, Rectangle<int> area)
{
    auto* mc = dynamic_cast<te::MidiClip*> (clip.get());
    if (mc == nullptr) return;
    auto& seq = mc->getSequence();
    if (seq.getNumNotes() == 0)
    {
        g.setColour (S::shade.withAlpha (0.45f));
        g.setFont (Fonts::body (12.0f));
        g.drawFittedText (::racoon::i18n::tr (String::fromUTF8 ("Vacío · doble clic para escribir notas")), area.reduced (4, 0), Justification::centredLeft, 2);
        return;
    }
    // a small piano roll: only the pitches the clip uses get a row (drums read as rows of hits, a bass line as its
    // contour), and the notes fill the clip's height as bars you can read, not dots
    Array<int> pitches;
    for (auto* n : seq.getNotes()) pitches.addIfNotAlreadyThere (n->getNoteNumber());
    pitches.sort();
    const int rows = jmax (1, pitches.size());
    const float rowH = jlimit (3.0f, 10.0f, (float) area.getHeight() / (float) rows);
    const float used = rowH * (float) rows;
    const float bottom = (float) area.getBottom() - jmax (0.0f, ((float) area.getHeight() - used) * 0.5f);
    const float startX = (float) getX();
    g.setColour (S::shade.withAlpha (0.82f));
    for (auto* n : seq.getNotes())
    {
        const auto t1 = n->getEditStartTime (*mc), t2 = n->getEditEndTime (*mc);
        float x1 = vs.timeToX (t1) - startX, x2 = vs.timeToX (t2) - startX;
        if (x2 < 0 || x1 > (float) getWidth()) continue;
        const float y = bottom - (float) (pitches.indexOf (n->getNoteNumber()) + 1) * rowH;
        g.fillRoundedRectangle (x1, y + 0.5f, jmax (3.0f, x2 - x1 - 1.0f), jmax (2.0f, rowH - 1.0f), 1.0f);
    }
}

void ClipComponent::paint (Graphics& g)
{
    auto r = getLocalBounds();
    auto* track = clip->getTrack();
    const auto col = track != nullptr ? colourForTrack (*track) : Brand::limon;
    const bool selected = vs.session.getSelection().isSelected (clip.get());
    const bool muted = clip->isMuted();

    g.setColour (muted ? C::ash : col);
    g.fillRect (r);
    if (muted)
    {
        g.setColour (S::shade.withAlpha (0.25f));
        for (int x = -r.getHeight(); x < r.getWidth(); x += 8)
            g.drawLine ((float) x, (float) r.getBottom(), (float) (x + r.getHeight()), (float) r.getY(), 1.0f);
    }

    auto head = r.removeFromTop (16);
    g.setColour (S::shade.withAlpha (0.12f));
    g.fillRect (head);
    g.setColour (S::shade);
    g.setFont (Fonts::label (12.0f));
    String name = clip->getName();
    if (clip->hasAnyTakes()) name << "  [toma " << (clip->getCurrentTake() + 1) << "]";
    if (auto* ac = dynamic_cast<te::AudioClipBase*> (clip.get()); ac != nullptr && ac->getAutoTempo()) name << "  WARP";
    if (PianoRoll::isHeld (*clip))
    {
        // FIJO: ALEATORIA leaves this clip alone
        auto badge = head.removeFromRight (40).reduced (2, 2);
        g.setColour (S::shade);
        g.fillRoundedRectangle (badge.toFloat(), 2.0f);
        g.setColour (Brand::limon);
        g.setFont (Fonts::label (10.0f));
        g.drawText (::racoon::i18n::tr ("FIJO"), badge, Justification::centred);
        g.setColour (S::shade);
        g.setFont (Fonts::label (12.0f));
    }
    g.drawText (::racoon::i18n::tr (name), head.reduced (5, 0), Justification::centredLeft, true);

    auto body = r.reduced (1, 2);
    if (dynamic_cast<te::MidiClip*> (clip.get()) != nullptr) drawNotes (g, body);
    else drawWaveform (g, body);

    // fades
    if (auto* ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
    {
        const float pxPerSec = (float) (vs.pxPerBeat * vs.session.getBpm() / 60.0);
        const float fi = (float) ac->getFadeIn().inSeconds() * pxPerSec, fo = (float) ac->getFadeOut().inSeconds() * pxPerSec;
        // the part each fade takes away, drawn with its own curve (lineal, convexa, cóncava, en S)
        g.setColour (S::shade.withAlpha (0.35f));
        const float top = 16.0f, h = (float) getHeight() - top;
        auto shade = [&] (float x0, float w, bool in, te::AudioFadeCurve::Type type)
        {
            Path p;
            p.startNewSubPath (in ? x0 : x0 + w, top);
            const int steps = jlimit (4, 48, (int) w / 3);
            for (int i = 0; i <= steps; ++i)
            {
                const float a = (float) i / (float) steps;                       // 0 at the silent end
                const float gain = te::AudioFadeCurve::alphaToGainForType (type, a);
                const float x = in ? x0 + a * w : x0 + w - a * w;
                p.lineTo (x, top + h * (1.0f - gain));
            }
            p.lineTo (in ? x0 + w : x0, top);
            p.closeSubPath();
            g.fillPath (p);
        };
        if (fi > 1.0f) shade (0.0f, fi, true, ac->getFadeInType());
        if (fo > 1.0f) shade ((float) getWidth() - fo, fo, false, ac->getFadeOutType());
        if (! vs.easyMode && isMouseOver())
        {
            g.setColour (Brand::leche);
            g.fillRect (Rectangle<float> (7, 7).withCentre ({ jmax (4.0f, fi), 4.0f }));
            g.fillRect (Rectangle<float> (7, 7).withCentre ({ jmin ((float) getWidth() - 4.0f, (float) getWidth() - fo), 4.0f }));
        }
    }

    g.setColour (selected ? Brand::leche : S::shade);
    g.drawRect (getLocalBounds(), selected ? 3 : 1);

    // while it moves or changes size: exactly where it starts and ends (bar.beat.sixteenth)
    if (dragged && (mode == Mode::move || mode == Mode::trimStart || mode == Mode::trimEnd))
    {
        const auto bpb = vs.beatsPerBar();
        const auto a = ViewState::positionText (vs.timeToBeat (clip->getPosition().getStart()), bpb);
        const auto b = ViewState::positionText (vs.timeToBeat (clip->getPosition().getEnd()), bpb);
        g.setFont (Fonts::mono (15.0f));
        auto tag = [&] (const String& t, bool right)
        {
            const int w = GlyphArrangement::getStringWidthInt (g.getCurrentFont(), ::racoon::i18n::tr (t)) + 10;
            auto box = Rectangle<int> (right ? getWidth() - w - 2 : 2, 18, w, 18);
            g.setColour (S::shade);
            g.fillRect (box);
            g.setColour (Brand::limon);
            g.drawText (::racoon::i18n::tr (t), box, Justification::centred);
        };
        if (mode != Mode::trimEnd) tag (a, false);
        if (mode != Mode::trimStart && getWidth() > 120) tag (b, true);
    }
}

ClipComponent::Mode ClipComponent::modeFor (Point<int> p) const
{
    if (! vs.easyMode && dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr && p.y < 9)
    {
        auto* ac = dynamic_cast<te::AudioClipBase*> (clip.get());
        const float pxPerSec = (float) (vs.pxPerBeat * vs.session.getBpm() / 60.0);
        const float fi = jmax (4.0f, (float) ac->getFadeIn().inSeconds() * pxPerSec);
        const float fo = jmin ((float) getWidth() - 4.0f, (float) getWidth() - (float) ac->getFadeOut().inSeconds() * pxPerSec);
        if (std::abs ((float) p.x - fi) < 7.0f) return Mode::fadeIn;
        if (std::abs ((float) p.x - fo) < 7.0f) return Mode::fadeOut;
    }
    const int edge = jmin (8, getWidth() / 4);
    if (p.x < edge) return Mode::trimStart;
    if (p.x > getWidth() - edge) return Mode::trimEnd;
    return Mode::move;
}

void ClipComponent::mouseMove (const MouseEvent& e)
{
    auto m = modeFor (e.getPosition());
    setMouseCursor (m == Mode::trimStart || m == Mode::trimEnd ? MouseCursor::LeftRightResizeCursor
                  : m == Mode::move ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
    repaint();
}

// "Crear secuencia": with a Racoon plugin, its window opens and its "A la maqueta" lands where you asked;
// with any other instrument, an empty clip of 4 bars opens in the editor below
void createSequence (ViewState& vs, te::AudioTrack& at, double beat)
{
    const double bar = vs.beatsPerBar();
    beat = jmax (0.0, std::floor (beat / bar + 1.0e-9) * bar);
    if (auto* p = vs.session.familyPluginOf (at))
    {
        vs.session.expectSequence (at, beat);
        if (vs.trackChosen) vs.trackChosen (at);
        p->showWindowExplicitly();
        if (vs.showHint)
            vs.showHint ("Escribe la secuencia en "_u + Catalog::displayName (*p) + " y pulsa «Pegar en maqueta»: cae en el compás "_u
                         + String ((int) (beat / bar) + 1) + " de este canal.");
        return;
    }
    vs.edit().getUndoManager().beginNewTransaction();
    if (auto* c = vs.session.createMidiClip (at, { te::BeatPosition::fromBeats (beat), te::BeatPosition::fromBeats (beat + 4.0 * bar) }, "Secuencia"))
        if (vs.openClipEditor) vs.openClipEditor (c);
}

String createSequenceLabel (ViewState& vs, te::AudioTrack& at)
{
    if (auto* p = vs.session.familyPluginOf (at)) return "Crear secuencia con "_u + Catalog::displayName (*p);
    return "Crear secuencia (4 compases)"_u;
}

bool canCreateSequence (ViewState& vs, te::AudioTrack& at)
{
    return vs.session.familyPluginOf (at) != nullptr || kindOf (at) == TrackKind::instrument;
}

void ClipComponent::mouseDown (const MouseEvent& e)
{
    auto& sel = vs.session.getSelection();
    dragged = false;
    duplicated = false;
    if (e.mods.isPopupMenu())
    {
        if (! sel.isSelected (clip.get())) sel.selectOnly (clip.get());
        showMenu();
        return;
    }
    if (vs.focusArrangement) vs.focusArrangement();
    const bool add = e.mods.isShiftDown() || e.mods.isCommandDown();
    mode = modeFor (e.getPosition());
    // the body (under the name) works like empty space: dragging there selects a range, even through the clip;
    // the name strip moves it. Alt (or holding Shift / Cmd) on the body still moves.
    if (mode == Mode::move && e.y >= 16 && ! add && ! e.mods.isAltDown()) mode = Mode::range;

    if (add)
    {
        if (e.mods.isCommandDown() && sel.isSelected (clip.get())) sel.deselect (clip.get());
        else sel.addToSelection (clip.get());
    }
    else if (! sel.isSelected (clip.get())) sel.selectOnly (clip.get());

    if (mode == Mode::range)
    {
        if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->beginRange (e.getEventRelativeTo (arr).getPosition());
        return;
    }
    vs.clearRange();
    vs.edit().getUndoManager().beginNewTransaction();
    downStartBeat = vs.timeToBeat (clip->getPosition().getStart());
    downEndBeat = vs.timeToBeat (clip->getPosition().getEnd());
    if (auto* ac = dynamic_cast<te::AudioClipBase*> (clip.get())) { downFadeIn = ac->getFadeIn(); downFadeOut = ac->getFadeOut(); }
    // everything selected moves with this one
    group.clear();
    if (mode == Mode::move)
    {
        for (auto* c : sel.getItemsOfType<te::Clip>())
            group.push_back ({ c, vs.timeToBeat (c->getPosition().getStart()) });
        if (group.empty()) group.push_back ({ clip, downStartBeat });
    }
}

void ClipComponent::mouseDrag (const MouseEvent& e)
{
    if (mode == Mode::none || e.mods.isPopupMenu()) return;
    if (! dragged && e.getDistanceFromDragStart() < 3) return;
    dragged = true;
    if (mode == Mode::range)
    {
        if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->dragRange (e.getEventRelativeTo (arr).getPosition());
        return;
    }
    const bool free = isCmd (e.mods);
    const double delta = (double) e.getDistanceFromDragStartX() / vs.pxPerBeat;

    switch (mode)
    {
        case Mode::move:
        {
            if (e.mods.isAltDown() && ! duplicated)
            {
                for (auto& [c, b] : group) te::duplicateClip (*c);   // the copies stay where the originals were
                duplicated = true;
            }
            // this clip lands on the grid; the others keep their distance to it
            double nb = jmax (0.0, vs.snapToView (downStartBeat + delta, free));
            double shift = nb - downStartBeat;
            for (auto& [c, b] : group) shift = jmax (shift, -b);
            for (auto& [c, b] : group) c->setStart (vs.beatToTime (b + shift), false, true);
            if (vs.showHint)
                vs.showHint ("Inicio "_u + ViewState::positionText (downStartBeat + shift, vs.beatsPerBar())
                             + (group.size() > 1 ? "  ·  "_u + String ((int) group.size()) + " clips juntos" : String())
                             + "  ·  Cmd/Ctrl: sin rejilla  ·  Alt: copia"_u);

            // vertical: remember which track is under the mouse; the move happens on release
            vs.clipDragTarget = nullptr;
            if (auto* arr = findParentComponentOfClass<ArrangementView>())
            {
                auto p = arr->getLocalPoint (this, e.getPosition());
                if (auto* lane = arr->laneAt (p); lane != nullptr && lane->track != clip->getTrack())
                    vs.clipDragTarget = lane->track.get();
                arr->repaint();
            }
            break;
        }
        case Mode::trimStart:
        {
            double nb = jlimit (0.0, downEndBeat - 0.0625, vs.snapToView (downStartBeat + delta, free));
            clip->setStart (vs.beatToTime (nb), true, false);
            if (vs.showHint) vs.showHint ("Empieza en "_u + ViewState::positionText (nb, vs.beatsPerBar()));
            break;
        }
        case Mode::trimEnd:
        {
            double nb = jmax (downStartBeat + 0.0625, vs.snapToView (downEndBeat + delta, free));
            clip->setEnd (vs.beatToTime (nb), true);
            if (vs.showHint) vs.showHint ("Termina en "_u + ViewState::positionText (nb, vs.beatsPerBar())
                                          + "  ·  largo "_u + String (nb - downStartBeat, 2) + " tiempos");
            break;
        }
        case Mode::fadeIn:
        case Mode::fadeOut:
        {
            auto* ac = dynamic_cast<te::AudioClipBase*> (clip.get());
            const double secs = (double) e.getDistanceFromDragStartX() / (vs.pxPerBeat * vs.session.getBpm() / 60.0);
            if (mode == Mode::fadeIn) ac->setFadeIn (te::TimeDuration::fromSeconds (jmax (0.0, downFadeIn.inSeconds() + secs)));
            else                      ac->setFadeOut (te::TimeDuration::fromSeconds (jmax (0.0, downFadeOut.inSeconds() - secs)));
            break;
        }
        case Mode::range:
        case Mode::none: break;
    }
    repaint();
}

void ClipComponent::mouseUp (const MouseEvent& e)
{
    if (mode == Mode::range)
    {
        if (auto* arr = findParentComponentOfClass<ArrangementView>())
        {
            if (dragged) arr->endRange();
            else vs.clearRange();
        }
        if (! dragged && vs.clipChosen) vs.clipChosen (*clip);
        mode = Mode::none;
        return;
    }
    // a move in time on the same channel: overwrite what the clips now cover (a move to another channel does it there)
    if (mode == Mode::move && dragged && vs.clipDragTarget == nullptr)
    {
        Array<te::Clip*> clips;
        for (auto& [c, b] : group) clips.add (c.get());
        vs.session.settleClips (clips);
    }
    if (mode == Mode::move && vs.clipDragTarget != nullptr)
    {
        auto all = te::getAudioTracks (vs.edit());
        const int delta = all.indexOf (asAudio (vs.clipDragTarget)) - all.indexOf (asAudio (clip->getTrack()));
        vs.clipDragTarget = nullptr;
        if (delta != 0)
        {
            Array<te::Clip::Ptr> keep;
            for (auto& [c, b] : group) keep.add (c);
            // deferred: this component belongs to the old lane and is deleted when the clip leaves it
            MessageManager::callAsync ([&s = vs.session, keep, delta]
            {
                Array<te::Clip*> clips;
                for (auto& c : keep) clips.add (c.get());
                s.moveClips (clips, 0.0, delta);
            });
        }
        if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->repaint();
    }
    if (mode != Mode::none && ! dragged && ! e.mods.isPopupMenu() && vs.clipChosen) vs.clipChosen (*clip);
    mode = Mode::none;
    group.clear();
    repaint();
}

void ClipComponent::mouseDoubleClick (const MouseEvent&)
{
    if (auto* mc = dynamic_cast<te::MidiClip*> (clip.get()))
        if (editClipInInstrument (vs, *mc)) return;
    if (vs.openClipEditor) vs.openClipEditor (clip.get());
}

// double click on a clip of a Racoon instrument: the instrument opens with the clip's notes in its own sequencer;
// "Devolver al clip" in the plugin sends the changes back here. The clip also shows in the editor below.
bool editClipInInstrument (ViewState& vs, te::MidiClip& clip)
{
    te::Clip::Ptr keep (&clip);
    auto* p = vs.session.editClipInInstrument (clip);
    if (p == nullptr) return false;
    if (vs.openClipEditor) vs.openClipEditor (&clip);
    p->showWindowExplicitly();
    if (vs.showHint)
        vs.showHint ("«"_u + clip.getName() + "» está abierto en "_u + Catalog::displayName (*p)
                     + ": edítalo ahí y pulsa «Devolver al clip» para traer los cambios."_u);
    return true;
}

void ClipComponent::showMenu() { showClipContextMenu (vs, *clip); }

void showClipContextMenu (ViewState& vs, te::Clip& clipRef)
{
    te::Clip::Ptr clip (&clipRef);
    auto& s = vs.session;
    PopupMenu m;
    if (vs.addEditItems)
    {
        // with a range selected these act on the range (a piece of this clip); otherwise on the selected clips
        if (vs.hasRange() && vs.rangeTracks.contains (clip->getTrack())) m.addSectionHeader (String::fromUTF8 ("Selección"));
        vs.addEditItems (m);
        m.addSeparator();
    }
    if (auto* mc = dynamic_cast<te::MidiClip*> (clip.get()))
        if (auto* at = dynamic_cast<te::AudioTrack*> (mc->getTrack()))
            if (auto* fp = s.familyPluginOf (*at); fp != nullptr && Catalog::animalFor (fp->getName()) != "Hedgehog")
                m.addItem ("Editar en "_u + Catalog::displayName (*fp), [&vs, clip]
                {
                    if (auto* c = dynamic_cast<te::MidiClip*> (clip.get())) editClipInInstrument (vs, *c);
                });
    m.addItem ("Abrir en el editor", [&vs, clip] { if (vs.openClipEditor) vs.openClipEditor (clip.get()); });
    m.addItem ("Renombrar...", [clip]
    {
        auto n = askForText ("Renombrar clip", "Nombre", clip->getName());
        if (n.isNotEmpty()) clip->setName (n);
    });
    m.addItem ("Dividir en el cursor", clip->getEditTimeRange().contains (s.getTransport().getPosition()), false,
               [&s, clip] { s.getEdit().getUndoManager().beginNewTransaction(); s.splitClip (*clip, s.getTransport().getPosition()); });
    m.addItem ("Duplicar", [&s, clip] { s.getEdit().getUndoManager().beginNewTransaction(); s.duplicateClip (*clip); });
    if (Session::clipMutedByTrack (*clip))
        m.addItem (String::fromUTF8 ("Callado porque su canal está en mute o hay otro en solo"), false, false, [] {});
    else
        m.addItem (clip->isMuted() ? "Activar clip" : "Silenciar clip", [clip] { clip->setMuted (! clip->isMuted()); });
    m.addItem ("Loop alrededor del clip", [&s, clip] { s.setLoopRange (clip->getEditTimeRange()); s.setLooping (true); });
    if (dynamic_cast<te::MidiClip*> (clip.get()) != nullptr)
        m.addItem (String::fromUTF8 ("Fijar (ALEATORIA no lo cambia)"), true, PianoRoll::isHeld (*clip), [&vs, clip]
        {
            PianoRoll::setHeld (*clip, ! PianoRoll::isHeld (*clip));
            vs.sendChangeMessage();
        });
    // the clips this acts on: every selected clip when this one is among them, otherwise only this one
    auto targets = [&s, clip]
    {
        Array<te::Clip::Ptr> list;
        auto sel = s.getSelection().getItemsOfType<te::Clip>();
        if (sel.contains (clip.get())) for (auto* c : sel) list.add (c); else list.add (clip);
        return list;
    };
    if (auto* w = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        m.addItem (PopupMenu::Item ("Reverso (al revés)"_u).setTicked (w->getIsReversed()).setAction ([&s, targets]
        {
            s.getEdit().getUndoManager().beginNewTransaction();
            for (auto& c : targets()) if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c.get())) wc->setIsReversed (! wc->getIsReversed());
        }));
    if (dynamic_cast<te::MidiClip*> (clip.get()) != nullptr)
    {
        auto midiDo = [&s, targets] (std::function<void (te::MidiClip&, UndoManager*)> f)
        {
            auto& um = s.getEdit().getUndoManager();
            um.beginNewTransaction();
            for (auto& c : targets()) if (auto* mc = dynamic_cast<te::MidiClip*> (c.get())) f (*mc, &um);
        };
        m.addSeparator();
        m.addItem ("Subir una octava", [midiDo] { midiDo ([] (te::MidiClip& c, UndoManager* um) { notes::transpose (c, {}, 12, um); }); });
        m.addItem ("Bajar una octava", [midiDo] { midiDo ([] (te::MidiClip& c, UndoManager* um) { notes::transpose (c, {}, -12, um); }); });
        m.addItem ("Reverso (notas al revés)"_u, [midiDo] { midiDo ([] (te::MidiClip& c, UndoManager* um) { notes::reverse (c, {}, um); }); });
        m.addItem ("Invertir (agudas abajo)", [midiDo] { midiDo ([] (te::MidiClip& c, UndoManager* um) { notes::invert (c, {}, um); }); });
        m.addItem ("Legato", [midiDo] { midiDo ([] (te::MidiClip& c, UndoManager* um) { notes::legato (c, {}, um); }); });
    }

    if (auto* mc = dynamic_cast<te::MidiClip*> (clip.get()))
    {
        m.addSeparator();
        te::MidiClip::Ptr mp (mc);
        m.addItem ("Cuantizar a la rejilla", [&s, mp] { s.getEdit().getUndoManager().beginNewTransaction(); s.quantise (*mp, s.snapBeats > 0 ? s.snapBeats : 0.25); });
        m.addItem ("Quitar solapes", [&s, mp]
        {
            s.getEdit().getUndoManager().beginNewTransaction();
            notes::removeOverlaps (*mp, {}, &s.getEdit().getUndoManager());
        });
        m.addItem (String::fromUTF8 ("Exportar como MIDI..."), [&s, mp]
        {
            auto chooser = std::make_shared<FileChooser> ("Exportar MIDI", Session::projectsFolder().getChildFile (mp->getName() + ".mid"), "*.mid");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles, [&s, mp, chooser] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                te::Renderer::Parameters p (s.getEdit());
                p.destFile = f.withFileExtension (".mid");
                p.createMidiFile = true;
                p.time = mp->getEditTimeRange();
                p.tracksToDo.setBit (te::getAllTracks (s.getEdit()).indexOf (mp->getTrack()));
                p.allowedClips.add (mp.get());
                te::Renderer::renderToFile ("MIDI", p);
            });
        });
    }

    if (auto* ac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
    {
        te::WaveAudioClip::Ptr ap (ac);
        m.addSeparator();
        m.addItem ("Seguir el tempo (warp)", true, ac->getAutoTempo(), [ap] { ap->setAutoTempo (! ap->getAutoTempo()); });
        PopupMenu tr;
        for (int st : { 12, 7, 5, 2, 1, 0, -1, -2, -5, -7, -12 })
            tr.addItem ((st > 0 ? "+" : "") + String (st) + " semitonos", true, std::abs (ap->getPitchChange() - (float) st) < 0.01f,
                        [ap, st] { ap->setPitchChange ((float) st); });
        m.addSubMenu ("Transponer", tr);
        PopupMenu gain;
        for (int db : { 6, 3, 0, -3, -6, -12 })
            gain.addItem ((db > 0 ? "+" : "") + String (db) + " dB", true, std::abs (ap->getGainDB() - (float) db) < 0.01f, [ap, db] { ap->setGainDB ((float) db); });
        m.addSubMenu ("Ganancia del clip", gain);
        {
            // the shape of each fade, and the quick ones
            PopupMenu fades;
            const std::pair<te::AudioFadeCurve::Type, const char*> shapes[] { { te::AudioFadeCurve::linear, "Lineal" }, { te::AudioFadeCurve::convex, "Convexa (rápida al principio)" },
                                                                             { te::AudioFadeCurve::concave, "Cóncava (lenta al principio)" }, { te::AudioFadeCurve::sCurve, "En S (suave en las dos puntas)" } };
            fades.addSectionHeader ("Curva de entrada");
            for (auto& shape : shapes)
            {
                const auto type = shape.first;
                fades.addItem (String::fromUTF8 (shape.second), true, ap->getFadeInType() == type, [&s, ap, type] { s.getEdit().getUndoManager().beginNewTransaction(); ap->setFadeInType (type); });
            }
            fades.addSectionHeader ("Curva de salida");
            for (auto& shape : shapes)
            {
                const auto type = shape.first;
                fades.addItem (String::fromUTF8 (shape.second) + " ", true, ap->getFadeOutType() == type, [&s, ap, type] { s.getEdit().getUndoManager().beginNewTransaction(); ap->setFadeOutType (type); });
            }
            fades.addSeparator();
            fades.addItem ("Fundidos cortos (10 ms)", [ap] { ap->setFadeIn (0.01s); ap->setFadeOut (0.01s); });
            fades.addItem ("Fundidos de medio segundo", [ap] { ap->setFadeIn (0.5s); ap->setFadeOut (0.5s); });
            fades.addItem ("Quitar fundidos", [ap] { ap->setFadeIn (0s); ap->setFadeOut (0s); });
            m.addSubMenu ("Fundidos", fades);
        }
        {
            // offline processes: a new file next to the project, the original stays on disk
            PopupMenu proc;
            auto run = [&vs, ap] (audio::Process what, const String& done)
            {
                const auto r = audio::processClip (vs.session, *ap, what);
                if (vs.showHint) vs.showHint (r.wasOk() ? done : r.getErrorMessage());
            };
            proc.addItem ("Normalizar (pico a -0.3 dB)", [run] { run (audio::Process::normalize, String::fromUTF8 ("Normalizado: el pico del clip quedó en -0.3 dB.")); });
            proc.addItem ("Invertir la fase", [run] { run (audio::Process::invertPhase, String::fromUTF8 ("Fase invertida.")); });
            proc.addItem ("Quitar DC (centrar la onda)", [run] { run (audio::Process::removeDC, String::fromUTF8 ("Se quitó el desplazamiento de DC.")); });
            proc.addItem ("Subir 3 dB", [run] { run (audio::Process::gainUp, String::fromUTF8 ("El audio subió 3 dB.")); });
            proc.addItem ("Bajar 3 dB", [run] { run (audio::Process::gainDown, String::fromUTF8 ("El audio bajó 3 dB.")); });
            m.addSubMenu ("Procesar audio", proc);
        }
        {
            PopupMenu strip;
            const std::pair<float, const char*> levels[] { { -35.0f, "Solo lo fuerte (umbral -35 dB)" }, { -45.0f, "Voz normal (umbral -45 dB)" },
                                                          { -55.0f, "Deja hasta las respiraciones (-55 dB)" } };
            for (auto& level : levels)
                strip.addItem (String::fromUTF8 (level.second), [&vs, ap, db = level.first]
                {
                    audio::StripOptions o;
                    o.thresholdDb = db;
                    int pieces = 0;
                    const auto r = audio::stripSilence (vs.session, *ap, o, &pieces);
                    if (vs.showHint) vs.showHint (r.wasOk() ? String::fromUTF8 ("Quedaron ") + String (pieces) + String::fromUTF8 (pieces == 1 ? " trozo con sonido." : " trozos con sonido.") : r.getErrorMessage());
                });
            m.addSubMenu ("Quitar silencios", strip);
        }
        m.addSeparator();
        m.addItem ("Convertir a MIDI (melodía)"_u, [&vs, ap]
        {
            String msg;
            auto* mc = audio::clipToMidi (vs.session, *ap, &msg);
            if (vs.showHint) vs.showHint (msg);
            if (mc != nullptr && vs.openClipEditor) vs.openClipEditor (mc);
        });
        m.addItem ("Dividir en los golpes", [&vs, ap]
        {
            String msg;
            audio::splitAtTransients (vs.session, *ap, 0.5f, &msg);
            if (vs.showHint) vs.showHint (msg);
        });
        m.addItem ("Trocear en golpes a Hamster", [&vs, ap]
        {
            String msg;
            auto slices = audio::sliceAtTransients (vs.session, *ap, 0.5f, 16, &msg);
            if (! slices.empty()) audio::slicesToHamster (vs.session, *ap, slices, &msg);
            if (vs.showHint) vs.showHint (msg);
        });
        if (stems::running())
            m.addItem (String::fromUTF8 ("Cancelar la separación en stems"), [] { stems::cancel(); });
        else
            m.addItem (String::fromUTF8 ("Separar en stems (voz, batería, bajo, otros)"), [&vs, ap] { startStemSeparation (vs, ap); });

        if (ac->hasAnyTakes())
        {
            PopupMenu takes;
            auto names = ac->getTakeDescriptions();
            for (int i = 0; i < names.size(); ++i)
                takes.addItem ("Toma " + String (i + 1), true, ac->getCurrentTake() == i, [ap, i] { ap->setCurrentTake (i); });
            takes.addSeparator();
            takes.addItem ("Separar tomas en pistas", [ap] { ap->unpackTakes (true); });
            m.addSubMenu ("Tomas (comping)", takes);
        }
    }
    else if (clip->hasAnyTakes())
    {
        PopupMenu takes;
        for (int i = 0; i < clip->getNumTakes (false); ++i)
            takes.addItem ("Toma " + String (i + 1), true, clip->getCurrentTake() == i, [clip, i] { clip->setCurrentTake (i); });
        m.addSubMenu ("Tomas (comping)", takes);
    }

    if (dynamic_cast<te::MidiClip*> (clip.get()) != nullptr || dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr)
        m.addItem ("Detectar la tonalidad", [&vs, clip] { showKeyOf (vs, *clip); });

    m.addSeparator();
    m.addItem ("Borrar", [&s, clip] { s.getEdit().getUndoManager().beginNewTransaction(); s.deleteClip (*clip); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

// Separar en stems: the first time, the model (81 MB) comes down; then the work runs in the background and the
// status bar says how far it is. At the end the four channels are in the song and the original is silenced.
void startStemSeparation (ViewState& vs, te::WaveAudioClip::Ptr clip)
{
    auto run = [&vs, clip]
    {
        if (clip == nullptr || ! clip->state.getParent().isValid()) return;
        auto hint = vs.showHint;
        const bool started = stems::start (vs.session, *clip, [hint] (const String& t) { if (hint) hint (t); },
                                           [hint] (bool, const String& msg) { if (hint) hint (msg); });
        if (hint) hint (started ? String::fromUTF8 ("Separando en stems... (sigue trabajando, Racoon te avisa al terminar)")
                                : String::fromUTF8 ("Ya hay una separación en curso"));
    };
    if (stems::modelReady()) { run(); return; }
    if (stems::downloading()) { if (vs.showHint) vs.showHint (String::fromUTF8 ("El modelo se está descargando")); return; }
    auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::QuestionIcon)
                    .withTitle (String::fromUTF8 ("Separar en stems"))
                    .withMessage (String::fromUTF8 ("Para separar voz, batería, bajo y otros, Racoon usa Demucs (Meta, licencia MIT). "
                                                    "El modelo pesa 81 MB y se descarga una sola vez a Música/Racoon/Modelos.\n\n¿Lo descargo ahora?"))
                    .withButton ("Descargar").withButton ("Cancelar");
    AlertWindow::showAsync (opts, [&vs, run] (int r)
    {
        if (r != 1) return;
        auto hint = vs.showHint;
        stems::downloadModel ([hint] (float p) { if (hint) hint (String::fromUTF8 ("Descargando el modelo de separación: ") + String (roundToInt (p * 100.0f)) + " %"); },
                              [hint, run] (bool ok, const String& msg) { if (hint) hint (msg); if (ok) run(); });
    });
}

void showKeyOf (ViewState& vs, te::Clip& clip)
{
    const auto k = audio::keyOfClip (vs.session, clip);
    if (k.root < 0)
    {
        if (vs.showHint) vs.showHint (String::fromUTF8 ("No encontré una tonalidad clara en «") + clip.getName() + String::fromUTF8 ("» (¿solo batería?)."));
        return;
    }
    const int sure = roundToInt (k.confidence * 100.0f);
    if (vs.setPianoScale) vs.setPianoScale (k.root, k.minor);
    if (vs.showHint)
        vs.showHint (String::fromUTF8 ("Tonalidad de «") + clip.getName() + String::fromUTF8 ("»: ") + k.name()
                     + (sure < 35 ? String::fromUTF8 (" (dudosa: podría ser su relativa)") : String())
                     + String::fromUTF8 (". La escala del Piano ya la ilumina."));
}

// ===================================================================== track header
TrackHeader::TrackHeader (ViewState& v, te::Track::Ptr t, int d) : track (t), depth (d), vs (v)
{
    auto* at = asAudio (track.get());
    mute.setClickingTogglesState (false);
    solo.setClickingTogglesState (false);
    arm.setClickingTogglesState (false);
    mute.setTooltip ("Silenciar (M)");
    solo.setTooltip ("Solo: escuchar solo esta pista");
    arm.setTooltip (String::fromUTF8 ("Armar para grabar. Pistas de instrumento graban MIDI; de audio, la entrada elegida."));
    mute.onClick = [this] { vs.edit().getUndoManager().beginNewTransaction(); vs.session.setTrackMute (*track, ! vs.session.isTrackMuted (*track)); refresh(); };
    solo.onClick = [this] { vs.edit().getUndoManager().beginNewTransaction(); vs.session.setTrackSolo (*track, ! vs.session.isTrackSoloed (*track)); refresh(); };
    arm.onClick = [this, at]
    {
        if (at == nullptr) return;
        vs.session.setArmed (*at, ! vs.session.isArmed (*at));
        if (vs.session.isArmed (*at) && vs.showHint)
            vs.showHint (kindOf (*at) == TrackKind::instrument ? String::fromUTF8 ("Pista armada: toca tu teclado MIDI y pulsa grabar (F9).")
                                                               : "Pista armada con la entrada " + vs.session.inputOf (*at) + ". Pulsa grabar (F9).");
        refresh();
    };

    addAndMakeVisible (mute);
    addAndMakeVisible (solo);
    if (at != nullptr && kindOf (*at) != TrackKind::returnBus && kindOf (*at) != TrackKind::hamsterOut)
        addAndMakeVisible (arm);

    instrumentButton.onClick = [this, at]
    {
        if (at == nullptr) return;
        if (auto* p = Session::instrumentOf (*at)) { p->showWindowExplicitly(); return; }
        if (kindOf (*at) == TrackKind::audio)
        {
            PopupMenu m;
            m.addSectionHeader ("Entrada de audio");
            for (auto& n : vs.session.audioInputNames())
                m.addItem (n, true, vs.session.inputOf (*at) == n, [this, at, n] { vs.session.setArmed (*at, true, n); refresh(); });
            if (m.getNumItems() <= 1) m.addItem (String::fromUTF8 ("No hay entradas: revisa Preferencias de audio"), false, false, [] {});
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (instrumentButton));
        }
    };
    addAndMakeVisible (instrumentButton);

    if (dynamic_cast<te::FolderTrack*> (track.get()) != nullptr)
    {
        foldButton.onClick = [this]
        {
            track->state.setProperty ("racoonFolded", ! (bool) track->state.getProperty ("racoonFolded", false), nullptr);
        };
        addAndMakeVisible (foldButton);
    }

    autoButton.setTooltip (String::fromUTF8 ("Automatización: dibuja cambios de volumen, pan o parámetros a lo largo del tiempo"));
    autoButton.onClick = [this]
    {
        // first time: a volume lane; afterwards the button shows / hides this channel's lanes
        if (AutoLanes::keys (*track).isEmpty())
        {
            for (auto* p : track->getAllAutomatableParams())
                if (auto* plug = p->getPlugin(); plug != nullptr && dynamic_cast<te::VolumeAndPanPlugin*> (plug) != nullptr
                     && ! p->getParameterName().containsIgnoreCase ("pan")) { AutoLanes::add (*track, *p); break; }
        }
        else track->state.setProperty ("racoonAuto", ! (bool) track->state.getProperty ("racoonAuto", false), nullptr);
        if (onAutomationToggled) onAutomationToggled();
    };
    addChildComponent (autoButton);

    volume.setSliderStyle (Slider::LinearHorizontal);
    volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setDoubleClickReturnValue (true, dbToVolumeFader (0.0f));
    volume.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
    volume.onValueChange = [this]
    {
        te::VolumeAndPanPlugin* vp = nullptr;
        if (auto* a = asAudio (track.get())) vp = a->getVolumePlugin();
        else if (auto* f = dynamic_cast<te::FolderTrack*> (track.get())) vp = f->getVolumePlugin();
        if (vp != nullptr) vp->setSliderPos ((float) volume.getValue());
        volume.setTooltip ("Volumen: " + String (volumeFaderToDb ((float) volume.getValue()), 1) + " dB");
    };
    addAndMakeVisible (volume);
    volume.onPopupMenu = [this]
    {
        te::VolumeAndPanPlugin* vp = nullptr;
        if (auto* a = asAudio (track.get())) vp = a->getVolumePlugin();
        else if (auto* f = dynamic_cast<te::FolderTrack*> (track.get())) vp = f->getVolumePlugin();
        if (vp != nullptr) showAutomationMenu (vs, *track, vp->volParam.get(), volume);
    };

    if (at != nullptr)
        if (auto* lm = at->getLevelMeterPlugin())
            meter.attach (&lm->measurer, lm);
    addAndMakeVisible (meter);

    setTooltip (String::fromUTF8 ("Clic: seleccionar. Doble clic en el nombre: renombrar. Clic derecho: opciones de la pista."));
    refresh();
    startTimerHz (5);
}

TrackHeader::~TrackHeader() { meter.attach (nullptr); }

void TrackHeader::refresh()
{
    mute.setToggleState (vs.session.isTrackMuted (*track), dontSendNotification);
    solo.setToggleState (vs.session.isTrackSoloed (*track), dontSendNotification);
    if (auto* at = asAudio (track.get()))
    {
        arm.setToggleState (vs.session.isArmed (*at), dontSendNotification);
        if (auto* p = Session::instrumentOf (*at))
        {
            instrumentButton.setButtonText ((Catalog::isRacoonPlugin (*p) ? Catalog::titleFor (p->getName()) : Catalog::displayName (*p)).toUpperCase());
            instrumentButton.setTooltip ("Abrir la ventana de " + Catalog::displayName (*p));
        }
        else if (kindOf (*at) == TrackKind::returnBus)
        {
            instrumentButton.setButtonText ("RETORNO");
            instrumentButton.setTooltip (String::fromUTF8 ("Pista de retorno: recibe los envíos de otras pistas"));
        }
        else if (kindOf (*at) == TrackKind::hamsterOut)
        {
            instrumentButton.setButtonText ("SALIDA");
            instrumentButton.setTooltip ("Salida separada de Hamster");
        }
        else
        {
            auto in = vs.session.inputOf (*at);
            instrumentButton.setButtonText (in.isNotEmpty() ? in.toUpperCase() : "ENTRADA");
            instrumentButton.setTooltip ("Elegir la entrada de audio para grabar");
        }
        if (auto* vp = at->getVolumePlugin(); vp != nullptr && ! volume.isMouseButtonDown()) volume.setValue (vp->getSliderPos(), dontSendNotification);
    }
    else if (auto* f = dynamic_cast<te::FolderTrack*> (track.get()))
    {
        instrumentButton.setButtonText ("GRUPO");
        instrumentButton.setTooltip ("Bus de grupo: suma las pistas que contiene");
        foldButton.setButtonText (track->state.getProperty ("racoonFolded", false) ? "+" : "-");
        if (auto* vp = f->getVolumePlugin(); vp != nullptr && ! volume.isMouseButtonDown()) volume.setValue (vp->getSliderPos(), dontSendNotification);
    }
    if (! autoButton.isVisible()) { autoButton.setVisible (true); resized(); }
    autoButton.setToggleState (track->state.getProperty ("racoonAuto", false), dontSendNotification);
    repaint();
}

void TrackHeader::timerCallback()
{
    if (auto* at = asAudio (track.get()))
        if (arm.getToggleState() != vs.session.isArmed (*at)) refresh();
    if (mute.getToggleState() != vs.session.isTrackMuted (*track) || solo.getToggleState() != vs.session.isTrackSoloed (*track)) refresh();
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    r.removeFromLeft (6 + depth * 12);
    meter.setBounds (r.removeFromRight (8));
    r.removeFromRight (6);
    auto top = r.removeFromTop (24);
    if (foldButton.isVisible()) { foldButton.setBounds (top.removeFromLeft (22)); top.removeFromLeft (4); }
    instrumentButton.setBounds (top.removeFromRight (jmin (96, top.getWidth() / 2)));
    r.removeFromTop (4);
    auto row = r.removeFromTop (22);
    mute.setBounds (row.removeFromLeft (26)); row.removeFromLeft (3);
    solo.setBounds (row.removeFromLeft (26)); row.removeFromLeft (3);
    if (arm.isVisible()) { arm.setBounds (row.removeFromLeft (26)); row.removeFromLeft (3); }
    if (autoButton.isVisible()) { autoButton.setBounds (row.removeFromRight (24)); row.removeFromRight (3); }
    volume.setBounds (row);
}

void TrackHeader::paint (Graphics& g)
{
    auto r = getLocalBounds();
    const bool selected = vs.session.getSelection().isSelected (track.get());
    g.fillAll (selected ? C::stone : S::card);
    const auto col = colourForTrack (*track);
    g.setColour (col);
    g.fillRect (r.removeFromLeft (6 + depth * 12).withTrimmedLeft (depth * 12));
    if (depth > 0) { g.setColour (C::ash.withAlpha (0.3f)); g.fillRect (0, 0, depth * 12, getHeight()); }

    auto nameArea = getLocalBounds().reduced (6, 5).withTrimmedLeft (6 + depth * 12).removeFromTop (24);
    if (foldButton.isVisible()) nameArea.removeFromLeft (26);
    nameArea.removeFromRight (jmin (96, nameArea.getWidth() / 2) + 18);
    g.setColour (S::ink);
    g.setFont (Fonts::bodyBold (14.5f));
    g.drawFittedText (::racoon::i18n::tr (track->getName()), nameArea, Justification::centredLeft, 1, 0.7f);

    if (auto* at = asAudio (track.get()); at != nullptr && at->isFrozen (te::Track::anyFreeze))
    {
        g.setColour (Brand::hielo);
        g.fillRect (getWidth() - 22, 2, 18, 10);
        g.setColour (S::ink);
        g.setFont (Fonts::label (9.0f));
        g.drawText (::racoon::i18n::tr ("HIELO"), getWidth() - 22, 2, 18, 10, Justification::centred);
    }
    g.setColour (C::stoneHi);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void TrackHeader::mouseDown (const MouseEvent& e)
{
    auto& sel = vs.session.getSelection();
    if (e.mods.isShiftDown() || e.mods.isCommandDown()) sel.addToSelection (track.get());
    else sel.selectOnly (track.get());
    if (e.mods.isPopupMenu()) showMenu();
    else if (vs.trackChosen) vs.trackChosen (*track);
}

void TrackHeader::mouseDrag (const MouseEvent& e)
{
    // hold and drag a channel name to put the channel somewhere else
    if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8) return;
    if (auto* dc = DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive())
            dc->startDragging ("track:" + track->itemID.toString(), this, ScaledImage (createComponentSnapshot (getLocalBounds(), true, 0.7f)));
}

void TrackHeader::mouseDoubleClick (const MouseEvent& e)
{
    if (e.y < 30) rename();
}

void TrackHeader::rename()
{
    auto n = askForText ("Renombrar pista", "Nombre", track->getName());
    if (n.isNotEmpty()) { vs.edit().getUndoManager().beginNewTransaction(); track->setName (n); }
}

void TrackHeader::showMenu()
{
    auto& s = vs.session;
    te::Track::Ptr t = track;
    auto* at = asAudio (t.get());
    const bool easy = vs.easyMode;
    PopupMenu m;
    if (at != nullptr && canCreateSequence (vs, *at))
    {
        m.addItem (createSequenceLabel (vs, *at), [this, at]
        {
            createSequence (vs, *at, vs.timeToBeat (vs.session.getTransport().getPosition()));
        });
        m.addSeparator();
    }
    m.addItem ("Renombrar...", [this] { rename(); });
    const std::pair<const char*, Colour> palette[] { { "Rosa", Brand::rosa }, { "Limón", Brand::limon },
                                                     { "Menta", Brand::menta }, { "Hielo", Brand::hielo }, { "Leche", Brand::leche }, { "Gris", C::boneDim } };
    PopupMenu colourMenu;
    for (auto& [name, col] : palette) { auto c = col; colourMenu.addItem (String::fromUTF8 (name), [t, c] { t->setColour (c); }); }
    m.addSubMenu ("Color", colourMenu);
    m.addItem ("Duplicar pista", [&s, t] { s.getEdit().getUndoManager().beginNewTransaction(); s.duplicateTrack (*t); });
    m.addItem ("Subir", [&s, t] { s.getEdit().getUndoManager().beginNewTransaction(); s.moveTrack (*t, -1); });
    m.addItem ("Bajar", [&s, t] { s.getEdit().getUndoManager().beginNewTransaction(); s.moveTrack (*t, 1); });
    m.addItem ("Nueva pista...", [this] { if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->showAddTrackMenu (nullptr); });

    if (at != nullptr)
    {
        m.addSeparator();
        if (auto* inst = Session::instrumentOf (*at))
        {
            te::Plugin::Ptr ip (inst);
            m.addItem ("Abrir " + Catalog::displayName (*inst), [ip] { ip->showWindowExplicitly(); });
        }
        if (kindOf (*at) != TrackKind::returnBus && kindOf (*at) != TrackKind::hamsterOut)
            m.addSubMenu (Session::instrumentOf (*at) != nullptr ? "Cambiar instrumento" : "Poner instrumento",
                          Menus::instrumentMenu (s, [&s, at] (const CatalogItem& it) { s.getEdit().getUndoManager().beginNewTransaction(); s.insertPlugin (*at, it); }));
        m.addSubMenu (String::fromUTF8 ("Añadir efecto"),
                      Menus::effectMenu (s, [&s, at] (const CatalogItem& it) { s.getEdit().getUndoManager().beginNewTransaction(); s.insertPlugin (*at, it); }));
        for (auto* p : Session::chainOf (*at))
            if (Session::isMidiEffect (*p) && p->isEnabled())
            {
                te::Plugin::Ptr fx (p);
                auto& vsRef = vs;
                m.addItem ("Pasar a clip MIDI lo que toca " + Catalog::displayName (*p), [&s, at, fx, &vsRef]
                {
                    auto r = capture::midiToClip (s, *at, *fx, capture::defaultRange (s, *at));
                    if (vsRef.showHint)
                        vsRef.showHint (r.error.isNotEmpty() ? r.error : "Listo: " + String (r.notes) + " notas en un clip nuevo. Cmd+Z lo deshace."_u);
                });
            }

        if (! easy)
        {
            m.addSeparator();
            const bool frozen = at->isFrozen (te::Track::individualFreeze);
            m.addItem (frozen ? "Descongelar" : "Congelar (ahorra CPU)", [&s, at, frozen] { s.setFrozen (*at, ! frozen); });
            m.addItem ("Pasar a audio (bounce)", [&s, at] { s.getEdit().getUndoManager().beginNewTransaction(); s.bounceToAudio (*at); });
            if (auto* inst = Session::instrumentOf (*at); inst != nullptr && Catalog::isRacoonPlugin (*inst, "Racoon Hamster"))
                m.addItem ("Separar salidas de Hamster (8 pistas)", [&s, at]
                {
                    s.getEdit().getUndoManager().beginNewTransaction();
                    s.splitHamsterOutputs (*at, 8);
                });
        }
    }

    if (! easy)
    {
        auto selected = s.getSelection().getItemsOfType<te::Track>();
        if (! selected.contains (t.get())) selected.add (t.get());
        m.addItem ("Agrupar seleccionadas en un bus", [&s, selected]
        {
            s.getEdit().getUndoManager().beginNewTransaction();
            s.groupTracks (selected);
        });
    }
    m.addItem (String::fromUTF8 ("Automatizar..."), [this]
    {
        AutoLanes::menu (*track, [this] (te::AutomatableParameter& p) { AutoLanes::add (*track, p); if (onAutomationToggled) onAutomationToggled(); })
            .showMenuAsync (PopupMenu::Options().withMousePosition());
    });

    m.addSeparator();
    m.addItem ("Borrar pista", [&s, t] { s.getEdit().getUndoManager().beginNewTransaction(); s.deleteTrack (*t); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

// ===================================================================== automation lanes
namespace AutoLanes
{
String keyOf (te::AutomatableParameter& p)
{
    if (auto* plug = p.getPlugin()) return plug->itemID.toString() + ":" + p.paramID;
    return p.getOwnerID().toString() + ":" + p.paramID;   // modulators and macros
}

te::AutomatableParameter* find (te::Track& t, const String& key)
{
    for (auto* p : devices::allParams (t)) if (keyOf (*p) == key) return p;
    return nullptr;
}

StringArray keys (te::Track& t)
{
    return StringArray::fromTokens (t.state.getProperty ("racoonAutoLanes").toString(), "|", {});
}

void setKeys (te::Track& t, const StringArray& k)
{
    t.state.setProperty ("racoonAutoLanes", k.joinIntoString ("|"), nullptr);
}

void add (te::Track& t, te::AutomatableParameter& p)
{
    auto k = keys (t);
    k.addIfNotAlreadyThere (keyOf (p));
    setKeys (t, k);
    t.state.setProperty ("racoonAuto", true, nullptr);
}

String label (te::AutomatableParameter& p)
{
    if (auto* plug = p.getPlugin())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (plug) != nullptr)
            return p.getParameterName().containsIgnoreCase ("pan") ? String ("Pan") : String ("Volumen");
        return Catalog::displayName (*plug) + ": " + ParamKnob::labelFor (p);
    }
    for (auto* t : te::getAllTracks (p.getEdit()))
        for (auto* m : devices::modulatorsOf (*t))
            if (m->itemID == p.getOwnerID()) return devices::modulatorName (*m) + ": " + ParamKnob::labelFor (p);
    return ParamKnob::labelFor (p);
}

PopupMenu menu (te::Track& t, std::function<void (te::AutomatableParameter&)> cb)
{
    PopupMenu m;
    m.addSectionHeader ("Canal");
    auto all = t.getAllAutomatableParams();
    for (auto* p : all)
        if (auto* plug = p->getPlugin(); plug != nullptr && dynamic_cast<te::VolumeAndPanPlugin*> (plug) != nullptr)
            m.addItem (label (*p), true, p->getCurve().getNumPoints() > 0, [cb, p] { cb (*p); });
    m.addSectionHeader ("Dispositivos");
    for (auto* plug : Session::chainOf (t))
    {
        PopupMenu sub;
        for (auto* p : plug->getAutomatableParameters())
            sub.addItem (ParamKnob::labelFor (*p), true, p->getCurve().getNumPoints() > 0, [cb, p] { cb (*p); });
        for (auto* p : plug->getMacroParameters())
            sub.addItem (p->getParameterName(), true, p->getCurve().getNumPoints() > 0, [cb, p] { cb (*p); });
        if (auto* r = devices::rackOf (*plug); r != nullptr && devices::isFxRack (*plug))
            for (auto* inner : r->getPlugins())
            {
                PopupMenu in;
                for (auto* p : inner->getAutomatableParameters())
                    in.addItem (ParamKnob::labelFor (*p), true, p->getCurve().getNumPoints() > 0, [cb, p] { cb (*p); });
                if (in.getNumItems() > 0) sub.addSubMenu (Catalog::displayName (*inner), in);
            }
        if (sub.getNumItems() > 0) m.addSubMenu (Catalog::displayName (*plug), sub);
    }
    auto mods = devices::modulatorsOf (t);
    if (! mods.isEmpty())
    {
        m.addSectionHeader ("Moduladores");
        for (auto* mod : mods)
        {
            PopupMenu sub;
            for (auto* p : devices::modulatorKnobs (*mod))
                sub.addItem (ParamKnob::labelFor (*p), true, p->getCurve().getNumPoints() > 0, [cb, p] { cb (*p); });
            m.addSubMenu (devices::modulatorName (*mod), sub);
        }
    }
    if (Session::chainOf (t).isEmpty()) m.addItem ("(este canal no tiene dispositivos)", false, false, [] {});
    return m;
}
}

AutomationLane::AutomationLane (ViewState& v, te::Track::Ptr t, const String& k) : track (t), key (k), vs (v)
{
    param = AutoLanes::find (*track, key);
    pick.setButtonText (param != nullptr ? AutoLanes::label (*param) : String ("(parámetro quitado)"_u));
    pick.setTooltip ("Parámetro que dibuja este carril. Clic para cambiarlo por otro de este canal."_u);
    pick.onClick = [this]
    {
        AutoLanes::menu (*track, [this] (te::AutomatableParameter& p)
        {
            auto k = AutoLanes::keys (*track);
            const int i = k.indexOf (key);
            if (i >= 0) k.set (i, AutoLanes::keyOf (p)); else k.add (AutoLanes::keyOf (p));
            k.removeDuplicates (false);
            AutoLanes::setKeys (*track, k);
        }).showMenuAsync (PopupMenu::Options().withTargetComponent (pick));
    };
    close.setTooltip ("Ocultar este carril (la automatización se queda)"_u);
    close.onClick = [this]
    {
        auto k = AutoLanes::keys (*track);
        k.removeString (key);
        AutoLanes::setKeys (*track, k);
        if (k.isEmpty()) track->state.setProperty ("racoonAuto", false, nullptr);
    };
    more.setTooltip ("Otro carril para automatizar otro parámetro de este canal"_u);
    more.onClick = [this]
    {
        AutoLanes::menu (*track, [this] (te::AutomatableParameter& p) { AutoLanes::add (*track, p); })
            .showMenuAsync (PopupMenu::Options().withTargetComponent (more));
    };
    for (auto* b : { &pick, &close, &more }) addAndMakeVisible (b);
    setTooltip ("Clic: añadir punto. Arrastrar: mover. Doble clic en un punto: borrarlo."_u);
}

void AutomationLane::resized()
{
    auto r = Rectangle<int> (0, 0, vs.headerWidth, getHeight()).reduced (12, 8);
    auto top = r.removeFromTop (24);
    close.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    more.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    pick.setBounds (top);
}

float AutomationLane::valueToY (float v) const
{
    if (param == nullptr) return 0.0f;
    const auto norm = param->valueRange.convertTo0to1 (v);
    return 6.0f + (1.0f - norm) * ((float) getHeight() - 12.0f);
}

float AutomationLane::yToValue (float y) const
{
    const float norm = jlimit (0.0f, 1.0f, 1.0f - (y - 6.0f) / ((float) getHeight() - 12.0f));
    return param->valueRange.convertFrom0to1 (norm);
}

int AutomationLane::hitPoint (Point<float> p) const
{
    if (param == nullptr) return -1;
    auto& curve = param->getCurve();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const float x = (float) vs.headerWidth + vs.timeToX (curve.getPointTime (i));
        const float y = valueToY (curve.getPointValue (i));
        if (p.getDistanceFrom ({ x, y }) < 7.0f) return i;
    }
    return -1;
}

void AutomationLane::paint (Graphics& g)
{
    auto header = Rectangle<int> (0, 0, vs.headerWidth, getHeight());
    g.setColour (S::cardDim);
    g.fillRect (header);
    g.setColour (C::stoneHi);
    g.fillRect (header.getRight() - 1, 0, 1, getHeight());

    auto lane = getLocalBounds().withTrimmedLeft (vs.headerWidth);
    g.setColour (C::basalt);
    g.fillRect (lane);
    g.saveState();
    g.reduceClipRegion (lane);
    g.setOrigin (lane.getX(), 0);
    drawGrid (g, vs, lane.withX (0), true);

    if (param != nullptr)
    {
        auto& curve = param->getCurve();
        const auto col = colourForTrack (*track);
        Path p;
        if (curve.getNumPoints() == 0)
        {
            const float y = valueToY (param->getCurrentValue());
            p.startNewSubPath (0.0f, y);
            p.lineTo ((float) lane.getWidth(), y);
        }
        else
        {
            p.startNewSubPath (0.0f, valueToY (curve.getPointValue (0)));
            for (int i = 0; i < curve.getNumPoints(); ++i)
                p.lineTo (vs.timeToX (curve.getPointTime (i)), valueToY (curve.getPointValue (i)));
            p.lineTo ((float) lane.getWidth(), valueToY (curve.getPointValue (curve.getNumPoints() - 1)));
        }
        Draw::glowPath (g, p, col, 2.0f);
        for (int i = 0; i < curve.getNumPoints(); ++i)
        {
            auto pt = Point<float> (vs.timeToX (curve.getPointTime (i)), valueToY (curve.getPointValue (i)));
            g.setColour (col);
            g.fillRect (Rectangle<float> (8.0f, 8.0f).withCentre (pt));
            g.setColour (S::ink);
            g.drawRect (Rectangle<float> (8.0f, 8.0f).withCentre (pt), 1.0f);
        }
        g.setColour (C::boneDim);
        g.setFont (Fonts::body (12.0f));
        g.drawText (::racoon::i18n::tr (param->getCurrentValueAsString()), 6, getHeight() - 18, 160, 14, Justification::centredLeft);
    }
    g.restoreState();
    g.setColour (C::stoneHi);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void AutomationLane::mouseDown (const MouseEvent& e)
{
    if (param == nullptr || e.x < vs.headerWidth) return;
    vs.edit().getUndoManager().beginNewTransaction();
    dragIndex = hitPoint (e.position);
    if (dragIndex < 0)
    {
        const auto t = vs.beatToTime (jmax (0.0, vs.snap (vs.xToBeat ((float) (e.x - vs.headerWidth)), isCmd (e.mods))));
        dragIndex = param->getCurve().addPoint (t, yToValue ((float) e.y), 0.0f);
    }
    repaint();
}

void AutomationLane::mouseDrag (const MouseEvent& e)
{
    if (param == nullptr || dragIndex < 0) return;
    const auto t = vs.beatToTime (jmax (0.0, vs.snap (vs.xToBeat ((float) (e.x - vs.headerWidth)), isCmd (e.mods))));
    dragIndex = param->getCurve().movePoint (dragIndex, t, yToValue ((float) e.y), false);
    repaint();
}

void AutomationLane::mouseUp (const MouseEvent&) { dragIndex = -1; }

void AutomationLane::mouseDoubleClick (const MouseEvent& e)
{
    if (param == nullptr) return;
    const int i = hitPoint (e.position);
    if (i >= 0) { param->getCurve().removePoint (i); repaint(); }
}

// ===================================================================== lane
TrackLane::TrackLane (ViewState& v, te::Track::Ptr t) : track (t), vs (v)
{
    const bool inst = asAudio (track.get()) != nullptr && kindOf (*track) == TrackKind::instrument;
    setTooltip (inst ? String::fromUTF8 ("Doble clic: nuevo clip MIDI. Arrastra aquí instrumentos, efectos o audios.")
                     : String::fromUTF8 ("Arrastra aquí archivos de audio, desde el navegador o desde el Finder."));
    syncClips();
}

void TrackLane::syncClips()
{
    auto* at = asAudio (track.get());
    te::Clip::Array current;
    if (at != nullptr)
        for (auto* c : at->getClips()) current.add (c);

    for (int i = clips.size(); --i >= 0;)
        if (! current.contains (clips[i]->clip.get()))
            clips.remove (i);

    for (auto* c : current)
    {
        bool found = false;
        for (auto* cc : clips) found = found || cc->clip.get() == c;
        if (! found)
        {
            auto* cc = clips.add (new ClipComponent (vs, c));
            addAndMakeVisible (cc);
        }
    }
    for (auto* cc : clips) cc->updateThumbnail();
    layoutClips();
}

void TrackLane::layoutClips()
{
    for (auto* cc : clips)
    {
        auto pos = cc->clip->getPosition();
        const float x1 = vs.timeToX (pos.getStart()), x2 = vs.timeToX (pos.getEnd());
        cc->setBounds (Rectangle<float> (x1, 2.0f, jmax (4.0f, x2 - x1), (float) getHeight() - 4.0f).getSmallestIntegerContainer());
    }
    repaint();
}

void TrackLane::resized() { layoutClips(); }

void TrackLane::paint (Graphics& g)
{
    const bool selected = vs.session.getSelection().isSelected (track.get());
    g.fillAll (selected ? Colour (0xff3a3236) : Colour (0xff2c2629));
    drawGrid (g, vs, getLocalBounds(), true);

    auto& tr = vs.session.getTransport();
    if (tr.looping)
    {
        auto loop = tr.getLoopRange();
        g.setColour (Brand::rosa.withAlpha (0.06f));
        g.fillRect (Rectangle<float> (vs.timeToX (loop.getStart()), 0.0f, vs.timeToX (loop.getEnd()) - vs.timeToX (loop.getStart()), (float) getHeight()));
    }

    if (auto* at = asAudio (track.get()); at != nullptr && tr.isRecording() && vs.session.isArmed (*at))
    {
        const float x1 = vs.timeToX (tr.getTimeWhenStarted()), x2 = vs.timeToX (tr.getPosition());
        g.setColour (Brand::rosa.withAlpha (0.45f));
        g.fillRect (Rectangle<float> (x1, 2.0f, jmax (2.0f, x2 - x1), (float) getHeight() - 4.0f));
        g.setColour (S::ink);
        g.setFont (Fonts::label (12.0f));
        g.drawText (::racoon::i18n::tr ("GRABANDO"), Rectangle<float> (x1 + 4.0f, 2.0f, 120.0f, 16.0f), Justification::centredLeft);
    }

    if (dropHighlight || vs.clipDragTarget == track.get())
    {
        g.setColour (Brand::limon.withAlpha (0.25f));
        g.fillAll();
        g.setColour (Brand::limon);
        g.drawRect (getLocalBounds(), 2);
    }

    if (clips.isEmpty() && asAudio (track.get()) != nullptr && getWidth() > 300)
    {
        g.setColour (C::boneDim.withAlpha (0.75f));
        g.setFont (Fonts::body (13.0f));
        const auto kind = kindOf (*track);
        String hint = kind == TrackKind::instrument ? String::fromUTF8 ("Doble clic para crear un clip MIDI")
                    : kind == TrackKind::returnBus ? String::fromUTF8 ("Retorno: sube los envíos en MASTER para alimentarlo")
                    : kind == TrackKind::hamsterOut ? String::fromUTF8 ("Salida separada de Hamster")
                    : String::fromUTF8 ("Arrastra aquí un audio o arma la pista y graba");
        g.drawText (::racoon::i18n::tr (hint), getLocalBounds().reduced (12, 0), Justification::centredLeft);
    }
    g.setColour (C::stoneHi);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void TrackLane::mouseDown (const MouseEvent& e)
{
    auto& sel = vs.session.getSelection();
    sel.selectOnly (track.get());
    if (vs.focusArrangement) vs.focusArrangement();

    if (e.mods.isPopupMenu())
    {
        auto* at = asAudio (track.get());
        if (at == nullptr) return;
        const double beat = jmax (0.0, vs.snapFloor (vs.xToBeat ((float) e.x)));
        PopupMenu m;
        if (vs.hasRange() && vs.rangeTracks.contains (track.get()) && vs.addEditItems)
        {
            m.addSectionHeader (String::fromUTF8 ("Selección"));
            vs.addEditItems (m);
            m.addSeparator();
        }
        else if (vs.session.hasClipboard() && vs.addEditItems)
        {
            // paste goes where you right-clicked
            vs.session.getTransport().setPosition (vs.beatToTime (beat));
            m.addItem (String::fromUTF8 ("Pegar aquí"), [this, at, beat]
            {
                vs.edit().getUndoManager().beginNewTransaction();
                vs.session.pasteAt (*at, beat);
            });
            m.addSeparator();
        }
        if (canCreateSequence (vs, *at))
            m.addItem (createSequenceLabel (vs, *at) + " aquí"_u, [this, at, beat] { createSequence (vs, *at, beat); });
        if (kindOf (*at) == TrackKind::instrument)
            m.addItem (String::fromUTF8 ("Nuevo clip MIDI aquí (1 compás)"), [this, at, beat]
            {
                vs.edit().getUndoManager().beginNewTransaction();
                if (auto* c = vs.session.createMidiClip (*at, { te::BeatPosition::fromBeats (beat), te::BeatPosition::fromBeats (beat + vs.beatsPerBar()) }))
                    if (vs.openClipEditor) vs.openClipEditor (c);
            });
        m.addItem ("Importar audio aqui...", [this, at, beat]
        {
            auto chooser = std::make_shared<FileChooser> ("Importar audio", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, at, beat, chooser] (const FileChooser& fc)
            {
                if (fc.getResult() == File()) return;
                vs.edit().getUndoManager().beginNewTransaction();
                vs.session.addAudioFile (*at, fc.getResult(), vs.beatToTime (beat));
            });
        });
        m.addSeparator();
        m.addItem ("Nueva pista...", [this] { if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->showAddTrackMenu (nullptr); });
        m.showMenuAsync (PopupMenu::Options().withMousePosition());
        return;
    }

    sel.deselectAll();
    sel.selectOnly (track.get());
    if (vs.trackChosen) vs.trackChosen (*track);      // a click on the song shows everything that is on this channel
    auto& tr = vs.session.getTransport();
    if (! tr.isPlaying())
        tr.setPosition (vs.beatToTime (jmax (0.0, vs.snapToView (vs.xToBeat ((float) e.x), isCmd (e.mods)))));
    vs.clearRange();
    if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->beginRange (e.getEventRelativeTo (arr).getPosition());
}

void TrackLane::mouseDrag (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 3) return;
    if (auto* arr = findParentComponentOfClass<ArrangementView>()) arr->dragRange (e.getEventRelativeTo (arr).getPosition());
}

void TrackLane::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (auto* arr = findParentComponentOfClass<ArrangementView>(); arr != nullptr && e.getDistanceFromDragStart() >= 3) arr->endRange();
}

void TrackLane::paintOverChildren (Graphics& g)
{
    // the selected range: a light veil with a frame, over clips and empty space alike
    if (! vs.hasRange() || ! vs.rangeTracks.contains (track.get())) return;
    const float x0 = vs.beatToX (vs.rangeStart), x1 = vs.beatToX (vs.rangeEnd);
    auto r = Rectangle<float> (x0, 0.0f, x1 - x0, (float) getHeight());
    g.setColour (Brand::hielo.withAlpha (0.28f));
    g.fillRect (r);
    g.setColour (Brand::hielo);
    g.drawRect (r, 2.0f);
}

void TrackLane::mouseDoubleClick (const MouseEvent& e)
{
    auto* at = asAudio (track.get());
    if (at == nullptr || kindOf (*at) != TrackKind::instrument) return;
    const double bar = vs.beatsPerBar();
    const double beat = jmax (0.0, std::floor (vs.xToBeat ((float) e.x) / bar) * bar);
    vs.edit().getUndoManager().beginNewTransaction();
    if (auto* c = vs.session.createMidiClip (*at, { te::BeatPosition::fromBeats (beat), te::BeatPosition::fromBeats (beat + bar) }))
        if (vs.openClipEditor) vs.openClipEditor (c);
}

void TrackLane::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (auto* arr = findParentComponentOfClass<ArrangementView>())
        arr->mouseWheelMove (e.getEventRelativeTo (arr), w);
}

bool TrackLane::isInterestedInDragSource (const SourceDetails& d)
{
    return asAudio (track.get()) != nullptr && (DragData::isPlugin (d.description) || DragData::isFile (d.description));
}

void TrackLane::itemDropped (const SourceDetails& d)
{
    dropHighlight = false;
    auto* at = asAudio (track.get());
    if (at == nullptr) return;
    vs.edit().getUndoManager().beginNewTransaction();
    if (DragData::isPlugin (d.description))
    {
        auto it = DragData::item (d.description);
        if (! it.available) { if (vs.showHint) vs.showHint (it.name + String::fromUTF8 (" no está instalado en este Mac.")); return; }
        if (it.isInstrument && kindOf (*at) == TrackKind::audio && ! at->getClips().isEmpty())
        {
            // an audio track with audio on it keeps its audio: make a new instrument track instead
            vs.session.addInstrumentTrack (it);
            return;
        }
        vs.session.insertPlugin (*at, it);
        if (it.isInstrument && it.source == CatalogItem::Source::racoon)
        {
            at->setColour (Catalog::colourFor (it.name));
            if (at->getName().startsWith ("Audio")) at->setName (Catalog::titleFor (it.name));
        }
    }
    else if (DragData::isFile (d.description))
    {
        const auto beat = jmax (0.0, vs.snap (vs.xToBeat ((float) d.localPosition.x)));
        vs.session.addAudioFile (*at, DragData::file (d.description), vs.beatToTime (beat));
    }
    repaint();
}

bool TrackLane::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (DragData::isAudioFile (File (f)) || DragData::isMidiFile (File (f))) return asAudio (track.get()) != nullptr;
    return false;
}

void TrackLane::filesDropped (const StringArray& files, int x, int)
{
    dropHighlight = false;
    auto* at = asAudio (track.get());
    if (at == nullptr) return;
    vs.edit().getUndoManager().beginNewTransaction();
    double beat = jmax (0.0, vs.snap (vs.xToBeat ((float) x)));
    for (auto& f : files)
    {
        te::Clip* c = DragData::isMidiFile (File (f)) ? (te::Clip*) vs.session.addMidiFile (*at, File (f), vs.beatToTime (beat))
                                                      : (te::Clip*) vs.session.addAudioFile (*at, File (f), vs.beatToTime (beat));
        if (c != nullptr) beat = vs.timeToBeat (c->getPosition().getEnd());
    }
}

// ===================================================================== arrangement
void ArrangementView::Content::paint (Graphics& g)
{
    g.fillAll (Colour (0xff221d1f));
}

void ArrangementView::Content::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && owner != nullptr) owner->showAddTrackMenu (nullptr);
}

void ArrangementView::Content::mouseDoubleClick (const MouseEvent&)
{
    if (owner != nullptr) owner->showAddTrackMenu (nullptr);
}

ArrangementView::ArrangementView (ViewState& v) : vs (v), ruler (v), timeRuler (v)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (ruler);
    addAndMakeVisible (timeRuler);
    content.owner = this;
    content.plus.setTooltip (String::fromUTF8 ("Nueva pista"));
    content.plus.onClick = [this] { showAddTrackMenu (&content.plus); };
    content.addAndMakeVisible (content.plus);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    hScroll.addListener (this);
    hScroll.setAutoHide (false);
    addAndMakeVisible (hScroll);
    vs.addChangeListener (this);
    editChanged();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    watched.removeListener (this);
    vs.removeChangeListener (this);
    vs.session.getSelection().removeChangeListener (this);
}

void ArrangementView::detach()
{
    cancelPendingUpdate();
    watched.removeListener (this);
    watched = {};
    vs.clipDragTarget = nullptr;
    rows.clear();
}

void ArrangementView::editChanged()
{
    watched.removeListener (this);
    watched = vs.edit().state;
    watched.addListener (this);
    vs.session.getSelection().removeChangeListener (this);
    vs.session.getSelection().addChangeListener (this);
    rows.clear();
    rebuild();
}

void ArrangementView::rebuild()
{
    needsRebuild = false;
    auto list = visibleTracks (vs.edit());

    std::vector<std::unique_ptr<Row>> newRows;
    for (auto& vt : list)
    {
        // the returns (Reverb, Delay...) belong to MASTER, not to the channels: here only instruments and sounds
        if (kindOf (*vt.track) == TrackKind::returnBus) continue;
        std::unique_ptr<Row> row;
        for (auto& r : rows)
            if (r != nullptr && r->track.get() == vt.track && r->header->depth == vt.depth)
            { row = std::move (r); break; }

        if (row == nullptr)
        {
            row = std::make_unique<Row>();
            row->track = vt.track;
            row->header = std::make_unique<TrackHeader> (vs, vt.track, vt.depth);
            row->header->onAutomationToggled = [this] { needsRebuild = true; triggerAsyncUpdate(); };
            row->lane = std::make_unique<TrackLane> (vs, vt.track);
            content.addAndMakeVisible (*row->header);
            content.addAndMakeVisible (*row->lane);
        }
        const bool wantsAuto = (bool) vt.track->state.getProperty ("racoonAuto", false);
        row->automation.clear();
        if (wantsAuto)
            for (auto& k : AutoLanes::keys (*vt.track))
            {
                row->automation.push_back (std::make_unique<AutomationLane> (vs, vt.track, k));
                content.addAndMakeVisible (*row->automation.back());
            }

        row->header->refresh();
        row->lane->syncClips();
        newRows.push_back (std::move (row));
    }
    rows = std::move (newRows);
    layoutRows();
    updateScrollBar();
    repaint();
}

int ArrangementView::rowHeight (te::Track& t) const
{
    if (dynamic_cast<te::FolderTrack*> (&t) != nullptr) return 44;
    return (int) t.state.getProperty ("racoonHeight", 64);
}

void ArrangementView::layoutRows()
{
    const int w = viewport.getMaximumVisibleWidth();
    int y = 0;
    for (auto& r : rows)
    {
        const int h = rowHeight (*r->track);
        r->header->setBounds (0, y, vs.headerWidth, h);
        r->lane->setBounds (vs.headerWidth, y, jmax (10, w - vs.headerWidth), h);
        y += h;
        for (auto& a : r->automation)
        {
            a->setBounds (0, y, w, AutomationLane::laneHeight);
            y += AutomationLane::laneHeight;
        }
    }
    content.plus.setBounds (8, y + 10, 44, 36);
    content.setSize (w, jmax (y + 120, viewport.getHeight()));
}

void ArrangementView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (34);
    top.removeFromLeft (vs.headerWidth);
    ruler.setBounds (top);
    hScroll.setBounds (r.removeFromBottom (12).withTrimmedLeft (vs.headerWidth));
    timeRuler.setBounds (r.removeFromBottom (TimeRuler::height).withTrimmedLeft (vs.headerWidth));
    viewport.setBounds (r);
    layoutRows();
    updateScrollBar();
}

void ArrangementView::paint (Graphics& g)
{
    g.fillAll (C::engrave);
    g.setColour (S::card);
    g.fillRect (0, 0, vs.headerWidth, 34);
    g.setColour (C::stoneHi);
    g.fillRect (0, 33, getWidth(), 1);
    g.fillRect (vs.headerWidth - 1, 0, 1, 34);
    g.setColour (S::card);
    g.fillRect (0, getHeight() - 12 - TimeRuler::height, vs.headerWidth, 12 + TimeRuler::height);
    g.setColour (C::stoneHi);
    g.fillRect (0, getHeight() - 12 - TimeRuler::height, vs.headerWidth, 1);
    // what time the cursor is at, next to the minutes ruler
    const double now = vs.session.getTransport().getPosition().inSeconds();
    g.setFont (Fonts::mono (12.0f));
    g.drawText (::racoon::i18n::tr (TimeRuler::format (now, 0.1)), Rectangle<int> (8, getHeight() - 12 - TimeRuler::height, vs.headerWidth - 16, TimeRuler::height),
                Justification::centredRight);
    g.setFont (Fonts::label (11.0f));
    g.drawText (::racoon::i18n::tr ("TIEMPO"), Rectangle<int> (8, getHeight() - 12 - TimeRuler::height, vs.headerWidth - 16, TimeRuler::height), Justification::centredLeft);
}

void ArrangementView::paintOverChildren (Graphics& g)
{
    if (trackDropY >= 0)
    {
        g.setColour (Brand::rosa);
        g.fillRect (0, trackDropY - 2, getWidth(), 4);
    }

    // playhead across ruler and lanes
    const float x = (float) vs.headerWidth + vs.timeToX (vs.session.getTransport().getPosition());
    if (x >= (float) vs.headerWidth && x < (float) getWidth())
    {
        g.setColour (Brand::hielo);
        g.fillRect (x - 0.5f, 0.0f, 2.0f, (float) getHeight() - 12.0f);
        Path tri;
        tri.addTriangle (x - 6.0f, (float) Ruler::loopStripHeight, x + 7.0f, (float) Ruler::loopStripHeight, x + 0.5f, (float) Ruler::loopStripHeight + 8.0f);
        g.fillPath (tri);
    }

    if (rows.empty())
    {
        auto r = viewport.getBounds().reduced (40);
        g.setColour (C::boneDim);
        g.setFont (Fonts::title (22.0f));
        g.drawFittedText (::racoon::i18n::tr ("Proyecto sin pistas"), r.removeFromTop (40), Justification::centredTop, 1);
        g.setFont (Fonts::body (15.0f));
        g.drawFittedText (::racoon::i18n::tr (String::fromUTF8 ("Pulsa + para crear un canal, o arrastra aquí un instrumento de la izquierda o un audio.")),
                          r.removeFromTop (60), Justification::centredTop, 3);
    }
}

void ArrangementView::timerCallback()
{
    auto& tr = vs.session.getTransport();
    const float x = (float) vs.headerWidth + vs.timeToX (tr.getPosition());
    if (std::abs (x - lastPlayheadX) > 0.5f)
    {
        repaint (Rectangle<int> ((int) lastPlayheadX - 8, 0, 18, getHeight()));
        repaint (Rectangle<int> ((int) x - 8, 0, 18, getHeight()));
        lastPlayheadX = x;
        repaint (Rectangle<int> (0, getHeight() - 12 - TimeRuler::height, vs.headerWidth, TimeRuler::height));

        // page along while playing
        const float laneW = (float) (getWidth() - vs.headerWidth);
        const float rel = x - (float) vs.headerWidth;
        if (followPlayhead && tr.isPlaying() && ! tr.isUserDragging() && (rel > laneW - 20.0f || rel < 0.0f))
            vs.setScroll (vs.timeToBeat (tr.getPosition()) - 2.0);
    }
    if (tr.isRecording())
        for (auto& r : rows) r->lane->repaint();
}

void ArrangementView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const float anchor = (float) (e.getEventRelativeTo (this).x - vs.headerWidth);
        vs.zoomAround (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, jmax (0.0f, anchor));
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        vs.setScroll (vs.scrollBeats - d * 300.0 / vs.pxPerBeat);
    }
    else
    {
        // vertical scroll done here: Viewport::mouseWheelMove hands the wheel back to its parent (this view) when it
        // cannot move any further, and calling it from here looped forever until the stack ran out (the crash on macOS)
        const int maxY = jmax (0, viewport.getViewedComponent() != nullptr ? viewport.getViewedComponent()->getHeight() - viewport.getViewHeight() : 0);
        const int step = roundToInt ((w.isInertial ? 0.5f : 1.0f) * w.deltaY * 260.0f);
        const int y = jlimit (0, maxY, viewport.getViewPositionY() - step);
        if (y != viewport.getViewPositionY()) viewport.setViewPosition (viewport.getViewPositionX(), y);
    }
}

void ArrangementView::showAutomationFor (te::Track& t, te::AutomatableParameter& p)
{
    AutoLanes::add (t, p);
    rebuild();
    for (auto& r : rows)
        if (r->track.get() == &t)
            for (auto& a : r->automation)
                if (a->key == AutoLanes::keyOf (p))
                    viewport.setViewPosition (viewport.getViewPositionX(), jmax (0, a->getY() - 80));
    if (vs.showHint) vs.showHint ("Carril de "_u + AutoLanes::label (p) + ": clic para poner puntos, arrástralos para dibujar el cambio."_u);
}

double ArrangementView::songLengthBeats() const
{
    return vs.timeToBeat (te::TimePosition() + vs.edit().getLength()) + 16.0 * vs.beatsPerBar();
}

void ArrangementView::updateScrollBar()
{
    const double visible = (double) (getWidth() - vs.headerWidth) / vs.pxPerBeat;
    const double total = jmax (songLengthBeats(), vs.scrollBeats + visible);
    hScroll.setRangeLimits (0.0, total, dontSendNotification);
    hScroll.setCurrentRange (vs.scrollBeats, visible, dontSendNotification);
}

void ArrangementView::scrollBarMoved (ScrollBar*, double start) { vs.setScroll (start); }

void ArrangementView::changeListenerCallback (ChangeBroadcaster* src)
{
    if (src == &vs)
    {
        for (auto& r : rows) { r->lane->layoutClips(); for (auto& a : r->automation) a->repaint(); }
        ruler.repaint();
        timeRuler.repaint();
        updateScrollBar();
        if ((bool) vs.easyMode != (bool) lastEasy) { lastEasy = vs.easyMode; needsRebuild = true; triggerAsyncUpdate(); }
        repaint();
        return;
    }
    // selection
    for (auto& r : rows) { r->header->repaint(); r->lane->repaint(); for (auto* c : r->lane->getChildren()) c->repaint(); }
}

static bool isTrackTree (const ValueTree& v) { return te::TrackList::isTrack (v); }

void ArrangementView::valueTreePropertyChanged (ValueTree& v, const Identifier& id)
{
    if (v.hasType (te::IDs::TRANSPORT)) { ruler.repaint(); timeRuler.repaint(); return; }
    if (isTrackTree (v) && (id.toString() == "racoonFolded" || id.toString() == "racoonHeight" || id.toString() == "racoonAuto"
                             || id.toString() == "racoonAutoLanes"))
        needsRebuild = true;
    // a plugin value (a fader, a knob, automation reading back) only needs the headers and lanes repainted
    const bool pluginOnly = v.hasType (te::IDs::PLUGIN) || v.getParent().hasType (te::IDs::PLUGIN);
    if (! pluginOnly) clipsDirty = true;
    triggerAsyncUpdate();
}

void ArrangementView::valueTreeChildAdded (ValueTree&, ValueTree& c)
{
    clipsDirty = true;
    if (isTrackTree (c)) needsRebuild = true;
    triggerAsyncUpdate();
}

void ArrangementView::valueTreeChildRemoved (ValueTree&, ValueTree& c, int)
{
    clipsDirty = true;
    if (isTrackTree (c)) needsRebuild = true;
    triggerAsyncUpdate();
}

void ArrangementView::valueTreeChildOrderChanged (ValueTree& p, int, int)
{
    clipsDirty = true;
    for (auto c : p) if (isTrackTree (c)) { needsRebuild = true; break; }
    triggerAsyncUpdate();
}

void ArrangementView::handleAsyncUpdate()
{
    if (needsRebuild) { rebuild(); return; }
    const bool clips = std::exchange (clipsDirty, false);
    for (auto& r : rows) { r->header->refresh(); if (clips) r->lane->syncClips(); for (auto& a : r->automation) a->repaint(); }
    ruler.repaint();
    timeRuler.repaint();
    updateScrollBar();
}

int ArrangementView::rowOf (TrackLane* l) const
{
    for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i]->lane.get() == l) return i;
    return -1;
}

void ArrangementView::beginRange (Point<int> p)
{
    auto* lane = laneAt (p);
    rangeAnchorRow = rowOf (lane);
    if (lane != nullptr) rangeAnchorBeat = jmax (0.0, vs.snapToView (vs.xToBeat ((float) lane->getLocalPoint (this, p).x)));
}

void ArrangementView::dragRange (Point<int> p)
{
    if (rangeAnchorRow < 0) return;
    auto* lane = laneAt (p);
    int row = rowOf (lane);
    if (row < 0)   // above or below the lanes: the nearest one
        row = p.y < rows.front()->lane->getScreenY() - getScreenY() ? 0 : (int) rows.size() - 1;
    auto* ref = rows[(size_t) row]->lane.get();
    const double beat = jmax (0.0, vs.snapToView (vs.xToBeat ((float) ref->getLocalPoint (this, p).x), ModifierKeys::currentModifiers.isCommandDown()));
    vs.rangeStart = jmin (rangeAnchorBeat, beat);
    vs.rangeEnd = jmax (rangeAnchorBeat, beat);
    vs.rangeTracks.clear();
    for (int i = jmin (row, rangeAnchorRow); i <= jmax (row, rangeAnchorRow); ++i) vs.rangeTracks.add (rows[(size_t) i]->track.get());
    for (auto& r : rows) r->lane->repaint();
    if (vs.showHint && vs.hasRange())
        vs.showHint ("Selección "_u + ViewState::positionText (vs.rangeStart, vs.beatsPerBar()) + " a "
                     + ViewState::positionText (vs.rangeEnd, vs.beatsPerBar()) + "  ·  "_u + String (vs.rangeEnd - vs.rangeStart, 2)
                     + " tiempos en " + String (vs.rangeTracks.size()) + (vs.rangeTracks.size() == 1 ? " canal" : " canales"));
}

void ArrangementView::endRange()
{
    rangeAnchorRow = -1;
    if (! vs.hasRange()) { vs.clearRange(); return; }
    // the empty time at the edges is not part of the selection: it shrinks to what the clips inside it hold (an
    // edge that cuts through a clip stays where it is). Dragging from 3 bars after a clip selects the clip, so
    // Cmd+D puts the copy right after it, not after the silence
    {
        double first = 1.0e12, last = -1.0e12;
        for (auto* t : vs.rangeAudioTracks())
            for (auto* c : t->getClips())
            {
                const double cs = vs.timeToBeat (c->getPosition().getStart()), ce = vs.timeToBeat (c->getPosition().getEnd());
                if (ce <= vs.rangeStart + 1.0e-6 || cs >= vs.rangeEnd - 1.0e-6) continue;
                first = jmin (first, jmax (cs, vs.rangeStart));
                last = jmax (last, jmin (ce, vs.rangeEnd));
            }
        if (last > first && (first > vs.rangeStart + 1.0e-6 || last < vs.rangeEnd - 1.0e-6))
        {
            vs.rangeStart = first;
            vs.rangeEnd = last;
            vs.sendChangeMessage();
            for (auto& r : rows) r->lane->repaint();
        }
    }
    if (vs.showHint)
        vs.showHint ("Selección lista: Cmd+C copia, Cmd+X corta, Cmd+D la duplica al lado, Supr la borra. Cmd+V pega donde hagas clic."_u);
}

bool ArrangementView::keyPressed (const KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == KeyPress::escapeKey && vs.hasRange()) { vs.clearRange(); return true; }
    if (code != KeyPress::leftKey && code != KeyPress::rightKey) return false;
    const double step = (k.getModifiers().isShiftDown() ? (double) vs.beatsPerBar() : vs.gridStep()) * (code == KeyPress::leftKey ? -1.0 : 1.0);
    if (vs.hasRange())
    {
        const double len = vs.rangeEnd - vs.rangeStart;
        vs.rangeStart = jmax (0.0, vs.rangeStart + step);
        vs.rangeEnd = vs.rangeStart + len;
        vs.sendChangeMessage();
        return true;
    }
    auto clips = vs.session.getSelection().getItemsOfType<te::Clip>();
    if (clips.isEmpty()) return false;
    vs.edit().getUndoManager().beginNewTransaction();
    vs.session.moveClips (clips, step, 0);
    if (vs.showHint) vs.showHint ("Movido a "_u + ViewState::positionText (vs.timeToBeat (clips.getFirst()->getPosition().getStart()), vs.beatsPerBar()));
    return true;
}

TrackLane* ArrangementView::laneAt (Point<int> p)
{
    for (auto* c = getComponentAt (p); c != nullptr; c = c->getParentComponent())
        if (auto* l = dynamic_cast<TrackLane*> (c)) return l;
    return nullptr;
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& d)
{
    return DragData::isPlugin (d.description) || DragData::isFile (d.description) || d.description.toString().startsWith ("track:");
}

static te::Track* trackFromDrag (te::Edit& e, const String& desc)
{
    const auto id = desc.fromFirstOccurrenceOf ("track:", false, false);
    for (auto* t : te::getAllTracks (e)) if (t->itemID.toString() == id) return t;
    return nullptr;
}

int ArrangementView::dropIndexFor (te::Track& dragged, int yInView, int* lineY) const
{
    // index among the channels that share the dragged channel's group
    const int y = yInView - viewport.getY() + viewport.getViewPositionY();
    int index = 0, i = 0, line = 0;
    bool found = false;
    for (auto& r : rows)
    {
        if (r->track->getParentFolderTrack() != dragged.getParentFolderTrack()) continue;
        const auto b = r->header->getBounds();
        if (! found && y < b.getCentreY()) { index = i; line = b.getY(); found = true; }
        if (! found) line = b.getBottom() + (int) r->automation.size() * AutomationLane::laneHeight;
        ++i;
    }
    if (! found) index = i;
    if (lineY != nullptr) *lineY = line - viewport.getViewPositionY() + viewport.getY();
    return index;
}

void ArrangementView::itemDragMove (const SourceDetails& d)
{
    if (! d.description.toString().startsWith ("track:")) return;
    if (auto* t = trackFromDrag (vs.edit(), d.description.toString()))
    {
        int line = 0;
        dropIndexFor (*t, d.localPosition.y, &line);
        if (line != trackDropY) { trackDropY = line; repaint(); }
    }
}

bool ArrangementView::moveTrackByDrag (te::Track& t, int yInView)
{
    int target = dropIndexFor (t, yInView, nullptr);
    int current = 0, i = 0;
    for (auto& r : rows)
        if (r->track->getParentFolderTrack() == t.getParentFolderTrack()) { if (r->track.get() == &t) current = i; ++i; }
    if (target > current) --target;
    if (target == current) return false;
    vs.edit().getUndoManager().beginNewTransaction();
    vs.session.moveTrackTo (t, target);
    return true;
}

void ArrangementView::itemDropped (const SourceDetails& d)
{
    trackDropY = -1;
    if (d.description.toString().startsWith ("track:"))
    {
        if (auto* t = trackFromDrag (vs.edit(), d.description.toString())) moveTrackByDrag (*t, d.localPosition.y);
        repaint();
        return;
    }
    vs.edit().getUndoManager().beginNewTransaction();
    if (DragData::isPlugin (d.description))
    {
        auto it = DragData::item (d.description);
        if (! it.available) { if (vs.showHint) vs.showHint (it.name + String::fromUTF8 (" no está instalado en este ordenador.")); return; }
        if (Catalog::isMasterProduct (it)) vs.session.insertOnMaster (it);
        else if (Catalog::listedAsInstrument (it)) vs.session.addChannelFor (it);
        else if (vs.showHint) vs.showHint (String::fromUTF8 ("Suelta los efectos sobre una pista o en MASTER."));
    }
    else if (DragData::isFile (d.description))
    {
        const double beat = jmax (0.0, vs.snap (vs.xToBeat ((float) (d.localPosition.x - vs.headerWidth))));
        if (auto* t = vs.session.addAudioTrack (DragData::file (d.description).getFileNameWithoutExtension()))
            vs.session.addAudioFile (*t, DragData::file (d.description), vs.beatToTime (beat));
    }
}

bool ArrangementView::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (DragData::isAudioFile (File (f)) || DragData::isMidiFile (File (f))) return true;
    return false;
}

void ArrangementView::filesDropped (const StringArray& files, int x, int)
{
    vs.edit().getUndoManager().beginNewTransaction();
    const double beat = jmax (0.0, vs.snap (vs.xToBeat ((float) (x - vs.headerWidth))));
    for (auto& f : files)
    {
        if (DragData::isAudioFile (File (f)))
            if (auto* t = vs.session.addAudioTrack (File (f).getFileNameWithoutExtension()))
                vs.session.addAudioFile (*t, File (f), vs.beatToTime (beat));
        // a pattern exported from a Racoon plugin lands on a channel with that plugin
        if (DragData::isMidiFile (File (f)))
            if (auto* t = vs.session.trackForMidiFile (File (f)))
                vs.session.addMidiFile (*t, File (f), vs.beatToTime (beat));
    }
}

void ArrangementView::showAddTrackMenu (Component* target)
{
    auto& s = vs.session;
    PopupMenu m;
    m.addItem ("Pista de audio", [&s] { s.getEdit().getUndoManager().beginNewTransaction(); s.addAudioTrack(); });
    m.addSubMenu ("Pista de instrumento", Menus::instrumentMenu (s, [&s] (const CatalogItem& it)
    {
        s.getEdit().getUndoManager().beginNewTransaction();
        s.addChannelFor (it);
    }));
    if (! vs.easyMode)
    {
        m.addSeparator();
        m.addItem ("Grupo con las pistas seleccionadas", [&s]
        {
            auto sel = s.getSelection().getItemsOfType<te::Track>();
            if (sel.isEmpty()) return;
            s.getEdit().getUndoManager().beginNewTransaction();
            s.groupTracks (sel);
        });
    }
    m.showMenuAsync (target != nullptr ? PopupMenu::Options().withTargetComponent (target) : PopupMenu::Options().withMousePosition());
}

void ArrangementView::zoomToFit()
{
    const double len = jmax (4.0 * vs.beatsPerBar(), vs.timeToBeat (te::TimePosition() + vs.edit().getLength()));
    vs.pxPerBeat = jlimit (0.5, 400.0, (double) (getWidth() - vs.headerWidth - 40) / len);
    vs.scrollBeats = 0.0;
    vs.sendChangeMessage();
}
}

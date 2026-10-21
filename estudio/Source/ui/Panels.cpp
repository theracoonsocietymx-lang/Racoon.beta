#include "Panels.h"
#include "engine/I18n.h"
#include "engine/AudioTools.h"

namespace racoon::ui
{
using namespace juce;

// ===================================================================== export
ExportPanel::ExportPanel (Session& s) : session (s)
{
    format.addItem ("WAV", 1);
    format.addItem ("FLAC", 2);
    format.addItem (String::fromUTF8 ("MP3 (no incluido)"), 3);
    format.setItemEnabled (3, false);
    format.setSelectedId (1, dontSendNotification);
    format.setTooltip (String::fromUTF8 ("WAV para máster y entregas; FLAC ocupa la mitad sin perder calidad. "
                                         "MP3 no viene incluido: su codificador necesita una licencia aparte."));
    bits.addItem ("16 bits", 16);
    bits.addItem ("24 bits", 24);
    bits.addItem ("32 bits float (solo WAV)", 32);
    bits.setSelectedId (24, dontSendNotification);
    rate.addItem ("44.1 kHz", 44100);
    rate.addItem ("48 kHz", 48000);
    rate.addItem ("96 kHz", 96000);
    rate.setSelectedId (48000, dontSendNotification);
    range.addItem (String::fromUTF8 ("Canción completa"), 1);
    range.addItem ("Solo el loop", 2);
    range.setSelectedId (session.getTransport().looping ? 2 : 1, dontSendNotification);
    format.onChange = [this] { if (format.getSelectedId() == 2 && bits.getSelectedId() == 32) bits.setSelectedId (24); };
    dither.setToggleState (true, dontSendNotification);
    stems.setTooltip (String::fromUTF8 ("Exporta cada pista por separado a una carpeta (las reverbs y delays van dentro de cada stem)"));

    for (auto* c : std::initializer_list<Component*> { &format, &bits, &rate, &range, &stems, &normalise, &dither, &exportButton, &cancelButton })
        addAndMakeVisible (c);

    exportButton.onClick = [this] { run(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    setSize (480, 440);
}

void ExportPanel::run()
{
    ExportOptions o;
    o.format = format.getSelectedId() == 2 ? 1 : 0;
    o.bitDepth = bits.getSelectedId();
    o.sampleRate = (double) rate.getSelectedId();
    o.loopOnly = range.getSelectedId() == 2;
    o.stems = stems.getToggleState();
    o.normalise = normalise.getToggleState();
    o.dither = dither.getToggleState();
    const auto ext = o.format == 1 ? String (".flac") : String (".wav");
    const auto base = Session::projectsFolder().getChildFile (File::createLegalFileName (session.getProjectName()));

    auto chooser = std::make_shared<FileChooser> (o.stems ? "Carpeta para los stems" : "Exportar mezcla",
                                                  o.stems ? base.getParentDirectory() : base.withFileExtension (ext),
                                                  o.stems ? String() : "*" + ext);
    const int flags = o.stems ? (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories)
                              : (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting);
    SafePointer<ExportPanel> sp (this);
    chooser->launchAsync (flags, [sp, o, ext, chooser] (const FileChooser& fc) mutable
    {
        if (sp == nullptr || fc.getResult() == File()) return;
        auto& s = sp->session;
        auto target = fc.getResult();
        if (o.stems) target = target.getChildFile (File::createLegalFileName (s.getProjectName()) + " stems");
        else target = target.withFileExtension (ext);
        o.destination = target;

        Array<File> written;
        auto r = o.stems ? s.exportStems (o, &written) : s.exportMix (o);
        if (sp == nullptr) return;
        if (r.wasOk())
        {
            sp->status = o.stems ? "Listo: " + String (written.size()) + " stems en " + target.getFileName()
                                 : "Listo: " + target.getFileName();
            if (! o.stems)
            {
                // the loudness report: what a mastering engineer or a platform would measure
                AudioBuffer<float> b;
                double sr = 0.0;
                if (audio::readFile (s.engine, target, b, sr))
                {
                    const auto L = audio::measureLoudness (b, sr);
                    sp->report = L;
                    sp->status << ". " << L.summary() << ".";
                    if (L.truePeakDb > -1.0f)
                        sp->status << String::fromUTF8 (" El pico real pasa de -1 dBTP: al convertir a MP3 o AAC puede distorsionar; baja el limitador un poco.");
                    else if (L.integratedLufs > -7.0f)
                        sp->status << String::fromUTF8 (" Está muy fuerte: Spotify y YouTube la bajarán a unos -14 LUFS.");
                }
            }
            target.revealToUser();
        }
        else sp->status = r.getErrorMessage();
        sp->repaint();
    });
}

void ExportPanel::resized()
{
    auto r = getLocalBounds().reduced (24);
    r.removeFromTop (44);
    auto row = [&r] (int h = 30) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    format.setBounds (row().withTrimmedLeft (120));
    bits.setBounds (row().withTrimmedLeft (120));
    rate.setBounds (row().withTrimmedLeft (120));
    range.setBounds (row().withTrimmedLeft (120));
    stems.setBounds (row (24));
    normalise.setBounds (row (24));
    dither.setBounds (row (24));
    auto buttons = r.removeFromBottom (36);
    exportButton.setBounds (buttons.removeFromRight (140));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (120));
}

void ExportPanel::paint (Graphics& g)
{
    g.fillAll (S::shade);
    Draw::box (g, getLocalBounds().toFloat().reduced (8.0f), S::card, 2.0f, 4.0f);
    g.setColour (S::ink);
    g.setFont (Fonts::title (22.0f));
    g.drawText (::racoon::i18n::tr ("Exportar"), 24, 18, 300, 30, Justification::centredLeft);
    auto label = [&g] (const char* t, Component& c) { drawCaption (g, String::fromUTF8 (t), c.getBounds().withX (24).withWidth (110), 13.0f, S::inkSoft); };
    label ("FORMATO", format);
    label ("RESOLUCIÓN", bits);
    label ("FRECUENCIA", rate);
    label ("QUÉ EXPORTAR", range);
    if (status.isNotEmpty())
    {
        g.setColour (S::ink);
        g.setFont (Fonts::bodyBold (13.0f));
        g.drawFittedText (::racoon::i18n::tr (status), Rectangle<int> (24, getHeight() - 124, getWidth() - 48, 66), Justification::centredLeft, 4);
    }
}

void ExportPanel::show (Session& s)
{
    DialogWindow::LaunchOptions o;
    auto* p = new ExportPanel (s);
    o.content.setOwned (p);
    o.dialogTitle = ::racoon::i18n::tr ("Exportar");
    o.dialogBackgroundColour = S::shade;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    auto* w = o.launchAsync();
    Component::SafePointer<DialogWindow> sw (w);
    p->onClose = [sw] { if (sw != nullptr) sw->exitModalState (0); };
}

// ===================================================================== audio clip inspector
AudioClipPanel::AudioClipPanel (ViewState& v) : vs (v)
{
    auto setup = [this] (Slider& s, double lo, double hi, double step, const String& suffix, const String& help)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setRange (lo, hi, step);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 70, 22);
        s.setTextValueSuffix (suffix);
        s.setTooltip (help);
        addAndMakeVisible (s);
    };
    setup (gain, -24.0, 12.0, 0.1, " dB", "Ganancia del clip");
    setup (pitch, -24.0, 24.0, 1.0, " st", String::fromUTF8 ("Transponer en semitonos sin cambiar la duración"));
    setup (fadeIn, 0.0, 4.0, 0.01, " s", "Fundido de entrada");
    setup (fadeOut, 0.0, 4.0, 0.01, " s", "Fundido de salida");
    gain.setDoubleClickReturnValue (true, 0.0);
    pitch.setDoubleClickReturnValue (true, 0.0);

    gain.onDragStart = pitch.onDragStart = fadeIn.onDragStart = fadeOut.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
    gain.onValueChange = [this] { if (clip) clip->setGainDB ((float) gain.getValue()); };
    pitch.onValueChange = [this] { if (clip) clip->setPitchChange ((float) pitch.getValue()); };
    fadeIn.onValueChange = [this] { if (clip) clip->setFadeIn (te::TimeDuration::fromSeconds (fadeIn.getValue())); };
    fadeOut.onValueChange = [this] { if (clip) clip->setFadeOut (te::TimeDuration::fromSeconds (fadeOut.getValue())); };
    warp.setTooltip (String::fromUTF8 ("Warp: el audio sigue el tempo del proyecto sin cambiar de tono (estiramiento no destructivo)"));
    warp.onClick = [this] { if (clip) { vs.edit().getUndoManager().beginNewTransaction(); clip->setAutoTempo (warp.getToggleState()); } };
    reverse.onClick = [this] { if (clip) { vs.edit().getUndoManager().beginNewTransaction(); clip->setIsReversed (reverse.getToggleState()); } };
    addAndMakeVisible (warp);
    addAndMakeVisible (reverse);
    title.setFont (Fonts::bodyBold (15.0f));
    addAndMakeVisible (title);
    startTimerHz (4);
}

void AudioClipPanel::setClip (te::WaveAudioClip* c)
{
    clip = c;
    thumb.reset();
    if (clip != nullptr)
    {
        te::AudioFile af (clip->getAudioFile());
        if (af.isValid()) thumb = std::make_unique<te::SmartThumbnail> (vs.session.engine, af, *this, &vs.edit());
        title.setText (clip->getName() + "  ·  "_u + af.getFile().getFileName(), dontSendNotification);
    }
    refresh();
}

void AudioClipPanel::refresh()
{
    if (clip == nullptr) return;
    if (! clip->state.getParent().isValid()) { setClip (nullptr); return; }
    gain.setValue (clip->getGainDB(), dontSendNotification);
    pitch.setValue (clip->getPitchChange(), dontSendNotification);
    fadeIn.setValue (clip->getFadeIn().inSeconds(), dontSendNotification);
    fadeOut.setValue (clip->getFadeOut().inSeconds(), dontSendNotification);
    warp.setToggleState (clip->getAutoTempo(), dontSendNotification);
    reverse.setToggleState (clip->getIsReversed(), dontSendNotification);
}

void AudioClipPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    title.setBounds (r.removeFromTop (26));
    auto controls = r.removeFromRight (jmin (380, r.getWidth() / 2));
    controls.removeFromLeft (16);
    auto row = [&controls] { auto x = controls.removeFromTop (28); controls.removeFromTop (6); return x.withTrimmedLeft (90); };
    gain.setBounds (row());
    pitch.setBounds (row());
    fadeIn.setBounds (row());
    fadeOut.setBounds (row());
    auto t = controls.removeFromTop (26);
    warp.setBounds (t.removeFromLeft (t.getWidth() * 3 / 5));
    reverse.setBounds (t);
}

void AudioClipPanel::paint (Graphics& g)
{
    const auto tc = clip != nullptr && clip->getTrack() != nullptr ? colourForTrack (*clip->getTrack()) : S::bg();
    g.fillAll (S::shade.interpolatedWith (tc, 0.10f));
    g.setColour (tc);
    g.fillRect (0, 0, getWidth(), 3);
    auto wave = waveArea();
    g.setColour (C::void_);
    g.fillRect (wave);
    if (thumb != nullptr && clip != nullptr)
    {
        g.setColour (clip->getTrack() != nullptr ? colourForTrack (*clip->getTrack()) : Brand::hielo);
        // the waveform as it plays: between two warp markers, that stretch of the file fills the time between them
        struct Seg { double s0, s1, w0, w1; };
        std::vector<Seg> segs;
        if (clip->getWarpTime())
        {
            const auto& ms = clip->getWarpTimeManager().getMarkers();
            for (int i = 0; i + 1 < ms.size(); ++i)
                segs.push_back ({ ms[i]->sourceTime.inSeconds(), ms[i + 1]->sourceTime.inSeconds(), ms[i]->warpTime.inSeconds(), ms[i + 1]->warpTime.inSeconds() });
        }
        if (segs.empty()) segs.push_back ({ 0.0, thumb->getTotalLength(), 0.0, thumb->getTotalLength() });
        const int chans = jmin (2, thumb->getNumChannels());
        for (auto& sg : segs)
        {
            if (sg.s1 <= sg.s0 || sg.w1 <= sg.w0) continue;
            const float x0 = xForWarpTime (sg.w0), x1 = xForWarpTime (sg.w1);
            const te::TimeRange src { te::TimePosition::fromSeconds (sg.s0), te::TimePosition::fromSeconds (sg.s1) };
            auto area = Rectangle<int> ((int) std::floor (x0), wave.getY() + 4, jmax (1, (int) std::ceil (x1) - (int) std::floor (x0)), wave.getHeight() - 8);
            if (chans > 1)
            {
                thumb->drawChannel (g, area.withHeight (area.getHeight() / 2 - 2), src, 0, 1.0f);
                thumb->drawChannel (g, area.withTrimmedTop (area.getHeight() / 2 + 2), src, 1, 1.0f);
            }
            else thumb->drawChannel (g, area, src, 0, 1.0f);
        }
        // the markers: a Limón line with a handle on top; the stretch of each part as a number
        if (clip->getWarpTime())
        {
            const auto& ms = clip->getWarpTimeManager().getMarkers();
            for (int i = 0; i < ms.size(); ++i)
            {
                const float x = xForWarpTime (ms[i]->warpTime.inSeconds());
                g.setColour (i == dragIndex ? Brand::rosa : Brand::limon);
                g.fillRect (x - 0.75f, (float) wave.getY(), 1.5f, (float) wave.getHeight());
                Path tri;
                tri.addTriangle (x - 6.0f, (float) wave.getY(), x + 6.0f, (float) wave.getY(), x, (float) wave.getY() + 8.0f);
                g.fillPath (tri);
                if (i + 1 < ms.size())
                {
                    const double src = (ms[i + 1]->sourceTime - ms[i]->sourceTime).inSeconds(), wt = (ms[i + 1]->warpTime - ms[i]->warpTime).inSeconds();
                    const float xn = xForWarpTime (ms[i + 1]->warpTime.inSeconds());
                    if (src > 0.0 && std::abs (wt / src - 1.0) > 0.005 && xn - x > 44.0f)
                    {
                        g.setColour (C::bone);
                        g.setFont (Fonts::mono (12.0f));
                        g.drawText (::racoon::i18n::tr (String (roundToInt (100.0 * wt / src)) + " %"), Rectangle<float> (x + 4.0f, (float) wave.getBottom() - 16.0f, xn - x - 8.0f, 14.0f), Justification::centred);
                    }
                }
            }
        }
    }
    auto labels = getLocalBounds().reduced (12, 8);
    labels.removeFromTop (26);
    auto col = labels.removeFromRight (jmin (380, labels.getWidth() / 2)).withTrimmedLeft (16);
    for (auto* t : { "GANANCIA", "TONO", "FADE IN", "FADE OUT" })
    {
        drawCaption (g, t, col.removeFromTop (28).withWidth (86), 12.0f, S::inkSoft);
        col.removeFromTop (6);
    }
}


// ---------------------------------------------------------------- warp markers
Rectangle<int> AudioClipPanel::waveArea() const
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (30);
    return r.removeFromLeft (r.getWidth() - jmin (380, r.getWidth() / 2));
}

double AudioClipPanel::viewSpan() const
{
    double span = thumb != nullptr ? thumb->getTotalLength() : 1.0;
    if (clip != nullptr && clip->getWarpTime())
        for (auto* m : clip->getWarpTimeManager().getMarkers()) span = jmax (span, m->warpTime.inSeconds());
    return jmax (0.01, span * 1.02);
}

float AudioClipPanel::xForWarpTime (double t) const
{
    const auto w = waveArea().toFloat().reduced (4.0f, 0.0f);
    return w.getX() + w.getWidth() * (float) (t / viewSpan());
}

double AudioClipPanel::warpTimeForX (float x) const
{
    const auto w = waveArea().toFloat().reduced (4.0f, 0.0f);
    return jmax (0.0, (double) (x - w.getX()) / (double) w.getWidth() * viewSpan());
}

int AudioClipPanel::markerAt (float x) const
{
    if (clip == nullptr || ! clip->getWarpTime()) return -1;
    const auto& ms = clip->getWarpTimeManager().getMarkers();
    int best = -1; float bestD = 7.0f;
    for (int i = 0; i < ms.size(); ++i)
    {
        const float d = std::abs (xForWarpTime (ms[i]->warpTime.inSeconds()) - x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int AudioClipPanel::addWarpMarker (double sourceSeconds)
{
    if (clip == nullptr) return -1;
    vs.edit().getUndoManager().beginNewTransaction();
    if (! clip->getWarpTime()) clip->setWarpTime (true);
    auto& wm = clip->getWarpTimeManager();
    // where that moment of the file sounds now (with the markers there already), so adding a marker moves nothing
    const auto src = te::TimePosition::fromSeconds (sourceSeconds);
    const int i = wm.insertMarker (te::WarpMarker (src, wm.sourceTimeToWarpTime (src)));
    repaint();
    return i;
}

void AudioClipPanel::moveWarpMarker (int index, double warpSeconds)
{
    if (clip == nullptr || ! clip->getWarpTime()) return;
    clip->getWarpTimeManager().moveMarker (index, te::TimePosition::fromSeconds (warpSeconds));
    repaint();
}

void AudioClipPanel::mouseMove (const MouseEvent& e)
{
    const bool onWave = waveArea().contains (e.getPosition());
    setMouseCursor (onWave && markerAt (e.position.x) >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
    setTooltip (onWave ? String::fromUTF8 ("Doble clic: marcador de warp donde cae un golpe. Arrástralo para mover ese momento; el audio de alrededor se estira. Clic derecho: quitarlo.")
                       : String());
}

void AudioClipPanel::mouseDown (const MouseEvent& e)
{
    dragIndex = -1;
    if (clip == nullptr || ! waveArea().contains (e.getPosition())) return;
    const int m = markerAt (e.position.x);
    if (e.mods.isPopupMenu())
    {
        PopupMenu menu;
        if (m > 0 && m < clip->getWarpTimeManager().getMarkers().size() - 1) menu.addItem (1, "Quitar este marcador");
        menu.addItem (3, String::fromUTF8 ("Poner un marcador en cada golpe"));
        if (clip->getWarpTime() && clip->getWarpTimeManager().getMarkers().size() > 2)
            menu.addItem (4, String::fromUTF8 ("Llevar los marcadores a la rejilla (1/16)"));
        if (clip->getWarpTime()) menu.addItem (2, "Quitar todo el warp (el audio vuelve a su tiempo)");
        menu.showMenuAsync (PopupMenu::Options(), [safe = Component::SafePointer<AudioClipPanel> (this), m] (int r)
        {
            if (safe == nullptr || safe->clip == nullptr || r == 0) return;
            safe->vs.edit().getUndoManager().beginNewTransaction();
            if (r == 1) safe->clip->getWarpTimeManager().removeMarker (m);
            if (r == 2) { safe->clip->getWarpTimeManager().removeAllMarkers(); safe->clip->setWarpTime (false); }
            if (r == 3) safe->markersAtHits();
            if (r == 4) safe->markersToGrid (0.25);
            safe->repaint();
        });
        return;
    }
    if (m > 0)
    {
        vs.edit().getUndoManager().beginNewTransaction();
        dragIndex = m;
    }
}

void AudioClipPanel::mouseDrag (const MouseEvent& e)
{
    if (dragIndex > 0) moveWarpMarker (dragIndex, warpTimeForX (e.position.x));
}

void AudioClipPanel::mouseUp (const MouseEvent&) { dragIndex = -1; repaint(); }

void AudioClipPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (clip == nullptr || thumb == nullptr || ! waveArea().contains (e.getPosition()) || markerAt (e.position.x) >= 0) return;
    const double wt = warpTimeForX (e.position.x);
    const double src = clip->getWarpTime() ? clip->getWarpTimeManager().warpTimeToSourceTime (te::TimePosition::fromSeconds (wt)).inSeconds() : wt;
    if (src <= 0.0 || src >= thumb->getTotalLength()) return;
    addWarpMarker (src);
}


int AudioClipPanel::markersAtHits (float sensitivity)
{
    if (clip == nullptr) return 0;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (clip->getAudioFile().getFile()));
    if (r == nullptr || r->lengthInSamples < 1024) return 0;
    AudioBuffer<float> buf ((int) jmin (2u, r->numChannels), (int) r->lengthInSamples);
    r->read (&buf, 0, buf.getNumSamples(), 0, true, buf.getNumChannels() > 1);
    const auto hits = audio::detectTransients (buf, r->sampleRate, sensitivity);
    vs.edit().getUndoManager().beginNewTransaction();
    if (! clip->getWarpTime()) clip->setWarpTime (true);
    auto& wm = clip->getWarpTimeManager();
    int added = 0;
    for (auto h : hits)
    {
        const double src = (double) h / r->sampleRate;
        if (src < 0.02 || src > r->lengthInSamples / r->sampleRate - 0.02) continue;
        bool near = false;   // a marker that is already there (or a hit within 40 ms of it) stays as it is
        for (auto* mk : wm.getMarkers()) near = near || std::abs (mk->sourceTime.inSeconds() - src) < 0.04;
        if (near) continue;
        const auto s = te::TimePosition::fromSeconds (src);
        wm.insertMarker (te::WarpMarker (s, wm.sourceTimeToWarpTime (s)));
        ++added;
    }
    if (vs.showHint) vs.showHint (String (added) + String::fromUTF8 (added == 1 ? " marcador de warp, en el golpe" : " marcadores de warp, uno en cada golpe")
                                  + String::fromUTF8 (". Arrástralos, o clic derecho > Llevar los marcadores a la rejilla."));
    repaint();
    return added;
}

int AudioClipPanel::markersToGrid (double gridBeats)
{
    if (clip == nullptr || ! clip->getWarpTime()) return 0;
    auto& wm = clip->getWarpTimeManager();
    auto& ts = vs.edit().tempoSequence;
    const auto pos = clip->getPosition();
    vs.edit().getUndoManager().beginNewTransaction();
    int moved = 0;
    // left to right, each marker to the nearest step of the grid in the song (the ones at both ends stay)
    for (int i = 1; i + 1 < wm.getMarkers().size(); ++i)
    {
        const double w = wm.getMarkers()[i]->warpTime.inSeconds();
        const auto editTime = pos.getStart() + te::TimeDuration::fromSeconds (w - pos.getOffset().inSeconds());
        const double beat = ts.toBeats (editTime).inBeats();
        const double snapped = std::round (beat / gridBeats) * gridBeats;
        const double newW = (ts.toTime (te::BeatPosition::fromBeats (snapped)) - pos.getStart()).inSeconds() + pos.getOffset().inSeconds();
        if (std::abs (newW - w) < 1.0e-4) continue;
        wm.moveMarker (i, te::TimePosition::fromSeconds (jmax (0.0, newW)));
        ++moved;
    }
    if (vs.showHint) vs.showHint (String (moved) + String::fromUTF8 (" marcadores en la rejilla: los golpes caen en tiempo"));
    repaint();
    return moved;
}

// ===================================================================== small windows
void showAudioSettings (te::Engine& engine)
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = ::racoon::i18n::tr (String::fromUTF8 ("Audio y MIDI"));
    o.dialogBackgroundColour = S::shade;
    o.useNativeTitleBar = true;
    o.escapeKeyTriggersCloseButton = true;
    o.resizable = true;
    auto* sel = new AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager, 0, 256, 0, 256, true, false, true, false);
    sel->setSize (560, 480);
    o.content.setOwned (sel);
    o.launchAsync();
}

void showShortcuts()
{
    // the keys most producers already know, wherever Racoon has the same action
    const char* text =
        "Espacio\tReproducir / parar\n"
        "F9\tGrabar\n"
        "L\tActivar loop\n"
        "Cmd+L\tLoop a la selección\n"
        "Inicio\tVolver al principio\n"
        "Cmd+Z / Cmd+Mayús+Z\tDeshacer / rehacer\n"
        "Cmd+S\tGuardar\n"
        "Cmd+Mayús+E\tExportar\n"
        "Cmd+T\tPista de audio nueva\n"
        "Cmd+Mayús+T\tPista de instrumento nueva\n"
        "Cmd+Mayús+M\tClip MIDI nuevo en el cursor\n"
        "Cmd+G\tAgrupar pistas\n"
        "Cmd+C / X / V\tCopiar / cortar / pegar\n"
        "Cmd+E\tDividir el clip en el cursor\n"
        "Cmd+D\tDuplicar\n"
        "Cmd+J\tUnir los clips MIDI seleccionados en uno\n"
        "Cmd+Mayús+R\tCrear la rola completa con el loop seleccionado\n"
        "Cmd+Mayús+A\tALEATORIA en todos los canales (menos los fijados)\n"
        "R o Cmd+R\tReverso: audio al revés, notas al revés\n"
        "0\tApagar / prender el clip o las notas\n"
        "Supr\tBorrar lo seleccionado\n"
        "Cmd+1 / 2 / 3\tNavegador / editor / master\n"
        "Cmd+Alt+B\tNavegador\n"
        "Mayús+Tab\tCambiar entre Piano y Canal\n"
        "Cmd+F\tSeguir el cursor\n"
        "+ / -\tZoom de la línea de tiempo\n"
        "Z\tVer toda la canción\n"
        "\nEn el Piano\n"
        "Doble clic en un hueco\tEscribir nota\n"
        "Doble clic en una nota\tBorrarla\n"
        "Clic y arrastrar en un hueco\tSeleccionar varias (Mayús: sumar)\n"
        "B\tLápiz: clic escribe, arrastrar pinta\n"
        "Clic derecho\tSubir o bajar octava, reverso, invertir, legato...\n"
        "Flechas arriba / abajo\tSemitono (Mayús: octava)\n"
        "Cmd+Mayús+arriba / abajo\tCopiar las notas una octava arriba o abajo\n"
        "Flechas izquierda / derecha\tMover un paso (Mayús: alargar o acortar)\n"
        "Re Pág / Av Pág\tVer la octava de arriba o de abajo\n"
        "Cmd+U o Q\tCuantizar\n"
        "Cmd+1 / Cmd+2\tRejilla más fina / más gruesa\n"
        "Cmd+3\tTresillos\n"
        "Cmd+4\tRejilla libre (sin ajustar)\n"
        "Cmd+A\tSeleccionar todo\n"
        "Esc\tQuitar la selección";
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, "Atajos de teclado", String::fromUTF8 (text).replace ("\t", "   ·   "_u));
}

void showAbout()
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, "Racoon " RACOON_VERSION_STRING,
        String::fromUTF8 ("El DAW de la familia Racoon. Producción por arreglo, sin Session View.\n\n"
                          "Motor de audio: Tracktion Engine (GPLv3) sobre JUCE. Racoon se distribuye bajo GPLv3; "
                          "el código fuente acompaña a cada versión.\n\n"
                          "Tipografías: Dela Gothic One, Golos Text, Alumni Sans y JetBrains Mono (SIL Open Font License)."));
}
}

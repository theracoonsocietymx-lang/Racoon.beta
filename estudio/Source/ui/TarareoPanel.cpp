#include "TarareoPanel.h"
#include "Widgets.h"
#include "engine/I18n.h"
#include "engine/Tarareo.h"
#include "engine/AudioTools.h"

namespace racoon::ui
{
using namespace juce;

namespace
{
constexpr int compasesMelodia = 4, compasesBeatbox = 2;

String u (const char* s) { return ::racoon::i18n::tr (String::fromUTF8 (s)); }
}

// the recorder: a bar of clicks, then the take (the clicks go on, so the tempo is kept); what comes in is moved back by
// the interface's latency (the click is heard late, the voice arrives late)
struct TarareoPanel::Grabadora : public AudioIODeviceCallback
{
    Grabadora (te::Engine& e, double bpm, int bars, bool click) : engine (e), clickOn (click)
    {
        auto& dm = engine.getDeviceManager().deviceManager;
        if (auto* dev = dm.getCurrentAudioDevice())
        {
            rate = dev->getCurrentSampleRate();
            latency = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();
            inputs = dev->getActiveInputChannels().countNumberOfSetBits();
        }
        beat = (int) std::lround (rate * 60.0 / bpm);
        countIn = beat * 4;
        length = beat * 4 * bars;
        take.setSize (1, countIn + length + latency + 1);
        take.clear();
        if (inputs > 0) dm.addAudioCallback (this);
    }
    ~Grabadora() override { engine.getDeviceManager().deviceManager.removeAudioCallback (this); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) FloatVectorOperations::clear (out[c], n);
        int p = pos.load();
        for (int i = 0; i < n && p < take.getNumSamples(); ++i, ++p)
        {
            float x = 0.0f;
            int k = 0;
            for (int c = 0; c < numIn; ++c) if (in[c] != nullptr) { x += in[c][i]; ++k; }
            take.setSample (0, p, k > 0 ? x / (float) k : 0.0f);
            // the click: a short blip on every beat, higher on the bar's first one
            if (clickOn && p < countIn + length)
            {
                const int inBeat = p % beat;
                if (inBeat < (int) (rate * 0.03))
                {
                    const bool down = (p / beat) % 4 == 0;
                    const float env = 1.0f - (float) inBeat / (float) (rate * 0.03);
                    const float s = 0.25f * env * std::sin (MathConstants<float>::twoPi * (down ? 1760.0f : 1320.0f) * (float) inBeat / (float) rate);
                    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) out[c][i] += s;
                }
            }
        }
        pos.store (p);
    }
    void audioDeviceAboutToStart (AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    bool done() const { return pos.load() >= take.getNumSamples(); }
    // where the take is: "1 2 3 4" while the clicks count in, then the bar being recorded
    String where() const
    {
        const int p = pos.load();
        if (p < countIn) return u ("Prepárate: ") + String (p / beat + 1) + " / 4";
        return ::racoon::i18n::tr (String::fromUTF8 ("Grabando, compás ") + String (jmin (length / (beat * 4), (p - countIn) / (beat * 4) + 1))
                                   + " de " + String (length / (beat * 4)));
    }
    AudioBuffer<float> result() const
    {
        AudioBuffer<float> b (1, length);
        b.copyFrom (0, 0, take, 0, jmin (take.getNumSamples() - length, countIn + latency), length);
        return b;
    }

    te::Engine& engine;
    bool clickOn;
    double rate = 48000.0;
    int latency = 0, inputs = 0, beat = 24000, countIn = 0, length = 0;
    AudioBuffer<float> take;
    std::atomic<int> pos { 0 };
};

TarareoPanel::TarareoPanel (Session& s, const String& genreId) : session (s)
{
    setWantsKeyboardFocus (true);
    int sel = 1, k = 1;
    for (auto& fam : genreFamilies())
    {
        genero.addSectionHeading (::racoon::i18n::tr (fam));
        for (auto& g : genres())
            if (String::fromUTF8 (g.family) == fam)
            {
                lista.add (&g);
                genero.addItem (String::fromUTF8 (g.name) + "  (" + String (roundToInt (g.bpm)) + " BPM)", k);
                if (genreId == g.id) sel = k;
                ++k;
            }
    }
    genero.setSelectedId (sel, dontSendNotification);
    genero.onChange = [this]
    {
        // the takes were made at the other genre's tempo: they are kept, and Racoon builds the song at their tempo
        actualizar();
    };
    grabarMelodia.setButtonText (u ("Grabar melodía"));
    importarMelodia.setButtonText (u ("Importar..."));
    grabarBeatbox.setButtonText (u ("Grabar ritmo"));
    importarBeatbox.setButtonText (u ("Importar..."));
    armar.setButtonText (u ("Armar la rola"));
    clic.setButtonText (u ("Clic mientras grabas"));
    clic.setToggleState (true, dontSendNotification);
    grabarMelodia.onClick = [this] { grabar (true); };
    grabarBeatbox.onClick = [this] { grabar (false); };
    for (auto* b : { &importarMelodia, &importarBeatbox })
        b->onClick = [this, melodia = b == &importarMelodia]
        {
            auto chooser = std::make_shared<FileChooser> (melodia ? u ("Tu melodía (wav, aiff, flac, mp3)") : u ("Tu ritmo con la boca (wav, aiff, flac, mp3)"),
                                                          File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<TarareoPanel> (this), chooser, melodia] (const FileChooser& fc)
                                  {
                                      if (sp != nullptr && fc.getResult().existsAsFile()) sp->importFile (melodia, fc.getResult());
                                  });
        };
    armar.onClick = [this] { build(); };
    cerrar.onClick = [this] { if (onClose) onClose(); };
    for (Component* c : std::initializer_list<Component*> { &genero, &grabarMelodia, &importarMelodia, &grabarBeatbox, &importarBeatbox, &armar, &cerrar, &clic })
        addAndMakeVisible (c);
    estadoMelodia = u ("Sin grabar.");
    estadoBeatbox = u ("Sin grabar (opcional: sin ritmo, Hamster toca el del género).");
    actualizar();
}

TarareoPanel::~TarareoPanel() { grabadora.reset(); }

const Genre* TarareoPanel::genre() const
{
    const int i = genero.getSelectedId() - 1;
    return isPositiveAndBelow (i, lista.size()) ? lista[i] : &defaultGenre();
}

void TarareoPanel::actualizar()
{
    const bool busy = grabadora != nullptr;
    for (auto* b : { &grabarMelodia, &importarMelodia, &grabarBeatbox, &importarBeatbox, &armar }) b->setEnabled (! busy);
    genero.setEnabled (! busy && ! hasToma (true) && ! hasToma (false));
    armar.setEnabled (! busy && (hasToma (true) || hasToma (false)));
    if (busy) (grabandoMelodia ? grabarMelodia : grabarBeatbox).setEnabled (true);
    grabarMelodia.setButtonText (busy && grabandoMelodia ? u ("Parar") : hasToma (true) ? u ("Grabar otra vez") : u ("Grabar melodía"));
    grabarBeatbox.setButtonText (busy && ! grabandoMelodia ? u ("Parar") : hasToma (false) ? u ("Grabar otra vez") : u ("Grabar ritmo"));
    repaint();
}

void TarareoPanel::grabar (bool melodia)
{
    if (grabadora != nullptr) { grabadora.reset(); stopTimer(); aviso = u ("Grabación cancelada."); actualizar(); return; }
    const auto* g = genre();
    const double bpm = tomaBpm > 0.0 ? tomaBpm : g->bpm;
    grabandoMelodia = melodia;
    grabadora = std::make_unique<Grabadora> (session.engine, bpm, melodia ? compasesMelodia : compasesBeatbox, clic.getToggleState());
    if (grabadora->inputs == 0)
    {
        grabadora.reset();
        aviso = u ("No hay entrada de audio activa. Elige tu micrófono en Archivo > Audio y MIDI, o importa un archivo.");
        actualizar();
        return;
    }
    aviso = {};
    startTimerHz (15);
    actualizar();
}

void TarareoPanel::timerCallback()
{
    if (grabadora == nullptr) { stopTimer(); return; }
    (grabandoMelodia ? estadoMelodia : estadoBeatbox) = grabadora->where();
    if (grabadora->done()) terminarGrabacion();
    repaint();
}

void TarareoPanel::terminarGrabacion()
{
    stopTimer();
    const auto take = grabadora->result();
    const double rate = grabadora->rate;
    const double bpm = 60.0 * rate / grabadora->beat;
    grabadora.reset();
    if (tomaBpm <= 0.0) tomaBpm = bpm;
    setToma (grabandoMelodia, take, rate);
}

void TarareoPanel::importFile (bool melodia, const File& f)
{
    AudioBuffer<float> b;
    double sr = 48000.0;
    if (! audio::readFile (session.engine, f, b, sr) || b.getNumSamples() == 0)
    {
        aviso = u ("No se pudo leer el archivo: ") + f.getFileName();
        repaint();
        return;
    }
    // a file starts where the sound starts (a few milliseconds before): its first note is the first beat
    float peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) peak = jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
    int start = 0;
    for (; start < b.getNumSamples(); ++start)
    {
        float v = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) v = jmax (v, std::abs (b.getSample (c, start)));
        if (v > peak * 0.1f) break;
    }
    start = jmax (0, start - (int) (sr * 0.03));
    AudioBuffer<float> mono (1, b.getNumSamples() - start);
    mono.clear();
    for (int c = 0; c < b.getNumChannels(); ++c) mono.addFrom (0, 0, b, c, start, mono.getNumSamples(), 1.0f / (float) b.getNumChannels());
    if (tomaBpm <= 0.0) tomaBpm = genre()->bpm;
    setToma (melodia, mono, sr);
}

void TarareoPanel::setToma (bool melodia, const AudioBuffer<float>& audio, double sampleRate)
{
    // both takes at one rate: the second is resampled to the first one's
    AudioBuffer<float> b;
    const auto& other = melodia ? beatboxAudio : melodiaAudio;
    if (other.getNumSamples() > 0 && std::abs (sampleRate - tomaRate) > 1.0)
    {
        const double ratio = sampleRate / tomaRate;
        b.setSize (1, (int) (audio.getNumSamples() / ratio));
        LagrangeInterpolator li;
        li.process (ratio, audio.getReadPointer (0), b.getWritePointer (0), b.getNumSamples());
    }
    else
    {
        b.makeCopyOf (audio);
        if (b.getNumChannels() > 1)
        {
            AudioBuffer<float> m (1, b.getNumSamples());
            m.clear();
            for (int c = 0; c < b.getNumChannels(); ++c) m.addFrom (0, 0, b, c, 0, b.getNumSamples(), 1.0f / (float) b.getNumChannels());
            b = std::move (m);
        }
        tomaRate = sampleRate;
    }
    if (tomaBpm <= 0.0) tomaBpm = genre()->bpm;
    (melodia ? melodiaAudio : beatboxAudio) = std::move (b);
    analizar (melodia);
    actualizar();
}

void TarareoPanel::analizar (bool melodia)
{
    // what Racoon heard, right away, so a bad take can be done again before building
    if (melodia)
    {
        auto notas = tarareo::transcribir (melodiaAudio, tomaRate, tomaBpm);
        if (notas.size() < 3) { estadoMelodia = u ("No se oyen notas claras: tararea más fuerte o más cerca del micrófono."); return; }
        const auto t = tarareo::detectarTono (notas);
        tarareo::ajustarATono (notas, t, 67);
        StringArray n;
        for (size_t i = 0; i < std::min<size_t> (notas.size(), 8); ++i) n.add (tarareo::nombreNota (notas[i].pitch));
        estadoMelodia = String ((int) notas.size()) + " " + u ("notas en") + " " + ::racoon::i18n::tr (keyName (t.root, t.major)) + " (" + camelot (t.root, t.major) + "): "
                        + n.joinIntoString (" ") + (notas.size() > 8 ? "..." : "");
    }
    else
    {
        const auto h = tarareo::beatbox (beatboxAudio, tomaRate, tomaBpm);
        int k[3] {};
        for (auto& x : h) ++k[jlimit (0, 2, x.tipo)];
        estadoBeatbox = h.empty() ? u ("No se oyen golpes claros.")
                                  : String (k[0]) + " " + u ("bombos") + ", " + String (k[1]) + " " + u ("cajas") + ", " + String (k[2]) + " " + u ("charles") + ".";
    }
}

void TarareoPanel::build()
{
    if (! hasToma (true) && ! hasToma (false)) return;
    Genre g = *genre();
    if (tomaBpm > 0.0) g.bpm = tomaBpm;
    const auto r = session.buildFromHum (g, hasToma (true) ? &melodiaAudio : nullptr, hasToma (false) ? &beatboxAudio : nullptr, tomaRate);
    if (onBuilt) onBuilt (r);
}

bool TarareoPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void TarareoPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (jmin (getWidth() - 32, 680), jmin (getHeight() - 32, 470));
    auto r = card.reduced (28, 22);
    cerrar.setBounds (card.getRight() - 44, card.getY() + 12, 30, 30);
    r.removeFromTop (40 + 54);
    auto row = r.removeFromTop (34);
    genero.setBounds (row.removeFromLeft (360));
    row.removeFromLeft (16);
    clic.setBounds (row);
    r.removeFromTop (18);
    filaMelodia = r.removeFromTop (92);
    {
        auto f = filaMelodia.withTrimmedTop (26).removeFromTop (34);
        grabarMelodia.setBounds (f.removeFromLeft (170));
        f.removeFromLeft (10);
        importarMelodia.setBounds (f.removeFromLeft (120));
    }
    r.removeFromTop (8);
    filaBeatbox = r.removeFromTop (92);
    {
        auto f = filaBeatbox.withTrimmedTop (26).removeFromTop (34);
        grabarBeatbox.setBounds (f.removeFromLeft (170));
        f.removeFromLeft (10);
        importarBeatbox.setBounds (f.removeFromLeft (120));
    }
    auto bottom = r.removeFromBottom (44);
    armar.setBounds (bottom.removeFromRight (200));
}

void TarareoPanel::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1f1a1c).withAlpha (0.82f));
    Draw::panel (g, card.toFloat(), {}, false);
    auto r = card.reduced (28, 22);
    g.setColour (S::ink);
    g.setFont (Fonts::title (30.0f));
    g.drawText (u ("Tarareo a rola"), r.removeFromTop (40), Justification::centredLeft);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::body (14.0f));
    const double bpm = tomaBpm > 0.0 ? tomaBpm : genre()->bpm;
    g.drawFittedText (::racoon::i18n::tr (String::fromUTF8 ("Tararea, canta o silba una melodía y haz el ritmo con la boca, a ") + String (roundToInt (bpm))
                                          + String::fromUTF8 (" BPM (primero suena un compás de clic). Racoon arma el bloque del género en tu tonalidad, con tu melodía y tu ritmo.")),
                      r.removeFromTop (54), Justification::topLeft, 3);
    auto rowText = [&] (Rectangle<int> fila, const String& title, const String& sub, const String& st)
    {
        g.setColour (S::ink);
        g.setFont (Fonts::title (17.0f));
        g.drawText (title, fila.removeFromTop (24), Justification::centredLeft);
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (12.0f));
        auto right = fila.removeFromTop (34);
        right.removeFromLeft (310);
        g.drawFittedText (sub, right, Justification::centredLeft, 2);
        g.setColour (S::ink);
        g.setFont (Fonts::body (13.0f));
        g.drawFittedText (::racoon::i18n::tr (st), fila.reduced (0, 4), Justification::topLeft, 2);
    };
    rowText (filaMelodia, u ("1. La melodía"), u ("4 compases. Con \"mmm\", \"la\" o silbando."), estadoMelodia);
    rowText (filaBeatbox, u ("2. El ritmo con la boca"), u ("2 compases. \"Bu\" es bombo, \"pa\" o \"ka\" caja, \"ts\" charles."), estadoBeatbox);
    if (aviso.isNotEmpty())
    {
        g.setColour (Brand::rosa);
        g.setFont (Fonts::body (13.0f));
        auto a = card.reduced (28, 22).removeFromBottom (44);
        a.removeFromRight (214);
        g.drawFittedText (aviso, a, Justification::centredLeft, 2);
    }
}
}

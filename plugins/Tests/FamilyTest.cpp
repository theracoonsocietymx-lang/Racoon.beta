// Offline checks for the Racoon family: every processor is driven without a host or a window.
#include "../Shared/PanelMuestras.h"
#include "../Shared/Biblioteca.h"
#include <JuceHeader.h>
#include "../Shared/Maqueta.h"
#include "RacoonProcessor.h"
#include "RacoonEditor.h"

using namespace juce;
using namespace racoon;

int failures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++failures;
}

// ---------------------------------------------------------------- helpers shared by the plugin tests
namespace tu
{
MidiBuffer midiOut;

void fillNoise (AudioBuffer<float>& b, float level, int seed)
{
    Random r (seed);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * level);
}

void fillSine (AudioBuffer<float>& b, double freq, double sr, float level, double phase0)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = level * (float) std::sin (phase0 + MathConstants<double>::twoPi * freq * i / sr);
        for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, v);
    }
}

// runs a long signal through the processor in blocks; returns the output
AudioBuffer<float> render (AudioProcessor& p, const AudioBuffer<float>& in, int block, MidiBuffer* midi)
{
    AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
    out.clear();
    const int total = jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    AudioBuffer<float> io (total, block);
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        io.setSize (total, n, false, false, true);
        io.clear();
        for (int c = 0; c < jmin (total, in.getNumChannels()); ++c) io.copyFrom (c, 0, in, c, pos, n);
        MidiBuffer m;
        if (midi != nullptr)
            for (const auto ev : *midi)
                if (ev.samplePosition >= pos && ev.samplePosition < pos + n) m.addEvent (ev.getMessage(), ev.samplePosition - pos);
        p.processBlock (io, m);
        if (midi != nullptr && p.producesMidi())
            for (const auto ev : m) midiOut.addEvent (ev.getMessage(), ev.samplePosition + pos);
        for (int c = 0; c < jmin (in.getNumChannels(), io.getNumChannels()); ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

bool allFinite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

float peak (const AudioBuffer<float>& b, int start, int len)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) p = jmax (p, b.getMagnitude (c, start, jmin (len, b.getNumSamples() - start)));
    return p;
}

float rms (const AudioBuffer<float>& b, int start, int len)
{
    double s = 0; int k = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < jmin (b.getNumSamples(), start + len); ++i) { s += (double) b.getSample (c, i) * b.getSample (c, i); ++k; }
    return (float) std::sqrt (s / jmax (1, k));
}

float maxJump (const AudioBuffer<float>& b)
{
    float j = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 1; i < b.getNumSamples(); ++i) j = jmax (j, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return j;
}

// amplitude of one frequency (Goertzel), Hann window
float tone (const AudioBuffer<float>& b, int ch, int start, int len, double freq, double sr)
{
    const double w = MathConstants<double>::twoPi * freq / sr, cw = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0, wsum = 0;
    for (int i = 0; i < len; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (MathConstants<double>::twoPi * i / (len - 1));
        wsum += win;
        s0 = b.getSample (ch, start + i) * win + cw * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return (float) (2.0 * std::sqrt (re * re + im * im) / wsum);
}

// power spectrum (Hann) of one channel, 2^order bins
std::vector<float> spectrum (const AudioBuffer<float>& b, int ch, int start, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i)
        data[(size_t) i] = b.getSample (ch, start + i) * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (n - 1)));
    fft.performFrequencyOnlyForwardTransform (data.data());
    std::vector<float> mag ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) mag[(size_t) i] = data[(size_t) i] * data[(size_t) i];
    return mag;
}

void setParam (RacoonProcessor& p, const String& id, float real) { p.setRealValue (id, real); }

void shot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}
}

// ---------------------------------------------------------------- contract checks for every plugin
static void commonChecks (const String& name, std::function<RacoonProcessor*()> make, const File& shots)
{
    std::cout << "contrato " << name << std::endl;
    std::unique_ptr<RacoonProcessor> p (make());
    const bool instrument = p->info.kind == PluginKind::instrument;
    const bool midiFx = p->info.kind == PluginKind::midiEffect;

    // block sizes and sample rates from the spec
    bool finite = true, ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 16, 64, 256, 1024 })
        {
            p->setRateAndBufferSizeDetails (sr, block);
            p->prepareToPlay (sr, block);
            AudioBuffer<float> in (2, (int) (sr * 0.25));
            tu::fillNoise (in, 0.3f, block);
            MidiBuffer midi;
            if (instrument || midiFx)
            {
                midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 10);
                midi.addEvent (MidiMessage::noteOn (1, 64, (uint8) 90), 20);
                midi.addEvent (MidiMessage::noteOff (1, 60), (int) (sr * 0.15));
                midi.addEvent (MidiMessage::noteOff (1, 64), (int) (sr * 0.15));
                if (instrument) in.clear();
            }
            auto out = tu::render (*p, in, block, &midi);
            finite = finite && tu::allFinite (out);
            ok = ok && tu::peak (out, 0, out.getNumSamples()) < 8.0f;
        }
    check (finite, "sin NaN ni infinitos con bloques de 1 a 1024 y 44.1 / 48 / 96 kHz");
    check (ok, "sin explosiones de nivel");

    // parameter fuzz: random values every block
    p->prepareToPlay (48000.0, 256);
    Random r (7);
    AudioBuffer<float> fz (2, 256);
    bool fuzzOk = true;
    for (int k = 0; k < 400; ++k)
    {
        for (auto* prm : p->getParameters())
            if (prm != p->getBypassParameter() && r.nextFloat() < 0.3f) prm->setValue (r.nextFloat());
        tu::fillNoise (fz, 0.5f, k);
        MidiBuffer m;
        if (k % 7 == 0) m.addEvent (MidiMessage::noteOn (1, 40 + (k % 40), (uint8) 100), 0);
        if (k % 7 == 3) m.addEvent (MidiMessage::allNotesOff (1), 0);
        p->processBlock (fz, m);
        fuzzOk = fuzzOk && tu::allFinite (fz) && fz.getMagnitude (0, 256) < 40.0f;
    }
    check (fuzzOk, "parámetros al azar en 400 bloques: estable"_u);

    // state round trip
    for (auto* prm : p->getParameters()) if (prm != p->getBypassParameter()) prm->setValueNotifyingHost (r.nextFloat());
    MemoryBlock state;
    p->getStateInformation (state);
    std::unique_ptr<RacoonProcessor> q (make());
    q->setStateInformation (state.getData(), (int) state.getSize());
    bool same = true;
    for (int i = 0; i < p->getParameters().size(); ++i)
        if (p->getParameters()[i]->getCurrentValueAsText() != q->getParameters()[i]->getCurrentValueAsText())
        {
            same = false;
            std::cout << "      distinto: " << p->getParameters()[i]->getName (40) << " " << p->getParameters()[i]->getValue()
                      << " vs " << q->getParameters()[i]->getValue() << std::endl;
        }
    check (same, "estado: guardar y abrir devuelve los mismos parámetros"_u);

    // presets
    const int np = p->getNumPrograms();
    bool presetsOk = np >= 1;
    for (int i = 0; i < np; ++i)
    {
        p->setCurrentProgram (i);
        presetsOk = presetsOk && p->getProgramName (i).isNotEmpty();
    }
    check (presetsOk, String (np) + " presets de fábrica cargan"_u);
    check (p->saveUserPreset ("Prueba test") && p->loadUserPreset ("Prueba test") && p->deleteUserPreset ("Prueba test"), "preset de usuario: guardar, abrir y borrar");

    // bypass without clicks (effects)
    if (! instrument && ! midiFx)
    {
        p->setCurrentProgram (0);
        p->prepareToPlay (48000.0, 128);
        AudioBuffer<float> sine (2, 48000);
        tu::fillSine (sine, 220.0, 48000.0, 0.4f, 0.0);
        AudioBuffer<float> io (2, 128);
        AudioBuffer<float> out (2, 48000);
        for (int pos = 0; pos < 48000; pos += 128)
        {
            if (pos == 24000) p->getBypassParameter()->setValue (1.0f);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, sine, c, pos, 128);
            MidiBuffer m;
            p->processBlock (io, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, 128);
        }
        p->getBypassParameter()->setValue (0.0f);
        const float sineStep = 0.4f * (float) (MathConstants<double>::twoPi * 220.0 / 48000.0);
        AudioBuffer<float> around (2, 4000);
        for (int c = 0; c < 2; ++c) around.copyFrom (c, 0, out, c, 22000, 4000);
        check (tu::maxJump (around) < jmax (0.08f, sineStep * 6.0f), "bypass sin clic (salto máx "_u + String (tu::maxJump (around), 3) + ")");
    }

    // Play: every plugin sounds on its own with its example, without a DAW and without loading anything
    {
        std::unique_ptr<RacoonProcessor> s (make());
        s->setRateAndBufferSizeDetails (48000.0, 512);
        s->prepareToPlay (48000.0, 512);
        s->setSeqPlaying (true);
        AudioBuffer<float> silence (2, 48000 * 4);
        silence.clear();
        auto out = tu::render (*s, silence, 512, nullptr);
        double sum = 0.0;
        for (int i = 0; i < out.getNumSamples(); ++i) sum += (double) out.getSample (0, i) * out.getSample (0, i);
        const float rms = (float) std::sqrt (sum / out.getNumSamples());
        check (tu::allFinite (out) && rms > 0.004f, "Play suena solo, con el ejemplo cargado (RMS " + String (rms, 4) + ", "
               + String ((int) s->getPattern().size()) + " notas, " + String (s->patternBars()) + " compases)");
        if (s->hasDemoSource())
        {
            std::unique_ptr<RacoonProcessor> b (make());
            b->setRateAndBufferSizeDetails (48000.0, 512);
            b->prepareToPlay (48000.0, 512);
            b->getBypassParameter()->setValueNotifyingHost (1.0f);
            b->setSeqPlaying (true);
            auto o2 = tu::render (*b, silence, 512, nullptr);
            double s2 = 0.0;
            for (int i = 0; i < o2.getNumSamples(); ++i) s2 += (double) o2.getSample (0, i) * o2.getSample (0, i);
            const float rms2 = (float) std::sqrt (s2 / o2.getNumSamples());
            check (rms2 > 0.02f && rms2 < 0.3f && rms < 0.45f, "el sonido de ejemplo entra a un nivel sano (" + String (rms2, 3) + " sin el efecto, " + String (rms, 3) + " con el efecto)");
        }
        {
            // Racoon closes a window and asks to stop it: "all sound off" (CC 120) stops its own Play
            AudioBuffer<float> blk (jmax (2, s->getTotalNumInputChannels(), s->getTotalNumOutputChannels()), 512);   // sidechain buses too
            blk.clear();
            MidiBuffer m;
            m.addEvent (MidiMessage::allSoundOff (1), 0);
            if (getenv ("SKIP_CC120") == nullptr) s->processBlock (blk, m);
            check (! s->isSeqPlaying(), "\"All sound off\" del anfitrion detiene su Play");
        }
        s->setSeqPlaying (false);
        // the sequence is part of the state, and its edits can be undone
        auto pat = s->getPattern();
        std::vector<SeqNote> edited = pat;
        edited.push_back ({ 1.0, 0.5, 72, 100, 1 });
        s->undoManager.beginNewTransaction();
        s->setPattern (edited);
        MemoryBlock st;
        s->getStateInformation (st);
        std::unique_ptr<RacoonProcessor> t2 (make());
        t2->setStateInformation (st.getData(), (int) st.getSize());
        const bool saved = t2->getPattern().size() == edited.size();
        s->undoManager.undo();
        check (saved && s->getPattern().size() == pat.size(), "la secuencia se guarda con el proyecto y se puede deshacer");
    }

    // automation: a line moves its parameter with Play, survives save / open, can be undone, follows its CC,
    // stays out of the way while the user holds the control, records movements and travels in the exported .mid
    {
        std::unique_ptr<RacoonProcessor> a (make());
        a->setRateAndBufferSizeDetails (48000.0, 512);
        a->prepareToPlay (48000.0, 512);
        String id;
        for (auto& cand : a->automation.automatableIds())
            if (auto* prm = a->automation.parameter (cand); prm != nullptr && ! prm->isDiscrete() && ! prm->isBoolean()) { id = cand; break; }
        check (id.isNotEmpty() && a->automation.automatableIds().size() >= 3, "parámetros automatizables: " + String (a->automation.automatableIds().size()));
        auto* prm = a->automation.parameter (id);
        a->automation.setBars (1);                                        // 4 beats = 2 s at 120
        a->undoManager.beginNewTransaction();
        a->automation.setPoints (id, { { 0.0, 0.05f }, { 3.75, 0.95f } });
        a->setPatternBpm (120.0);
        a->setSeqPlaying (true);
        AudioBuffer<float> blk (2, 512);
        float lo = 1.0f, hi = 0.0f, maxErr = 0.0f;
        for (int k = 0; k < 160; ++k)   // ~1.7 s
        {
            blk.clear();
            MidiBuffer m;
            a->processBlock (blk, m);
            const double pos = a->automation.playheadBeat();
            const float want = Automation::valueAt (a->automation.points (id), pos, 4.0, false);
            const float got = prm->getValue();
            lo = jmin (lo, got); hi = jmax (hi, got);
            if (k > 2) maxErr = jmax (maxErr, std::abs (want - got));
        }
        check (hi - lo > 0.6f && maxErr < 0.03f, "la línea mueve " + a->automation.nameOf (id) + " con Play (de " + String (lo, 2) + " a " + String (hi, 2)
               + ", error " + String (maxErr, 3) + ")");
        // touching the control wins over the line
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (0.5f);
        for (int k = 0; k < 20; ++k) { blk.clear(); MidiBuffer m; a->processBlock (blk, m); }
        check (std::abs (prm->getValue() - 0.5f) < 1.0e-4f, "mientras sostienes la perilla, la línea no la mueve");
        // recording
        a->automation.recording.store (true);
        prm->setValueNotifyingHost (0.8f);
        for (int k = 0; k < 6; ++k) { blk.clear(); MidiBuffer m; a->processBlock (blk, m); a->automation.recordTick(); }
        prm->endChangeGesture();
        a->automation.recordTick();
        a->automation.recording.store (false);
        bool recorded = false;
        for (auto& q : a->automation.points (id)) recorded |= std::abs (q.value - 0.8f) < 0.01f;
        check (recorded, "grabar: lo que mueves mientras suena queda en la línea");
        a->setSeqPlaying (false);
        // CC from an exported file
        const int cc = a->automation.ccFor (id);
        {
            blk.clear();
            MidiBuffer m;
            m.addEvent (MidiMessage::controllerEvent (1, cc, 100), 0);
            a->processBlock (blk, m);
        }
        check (cc >= 0 && std::abs (prm->getValue() - 100.0f / 127.0f) < 0.01f, "el plugin sigue su MIDI CC " + String (cc));
        // state
        MemoryBlock st;
        a->getStateInformation (st);
        std::unique_ptr<RacoonProcessor> b (make());
        b->setStateInformation (st.getData(), (int) st.getSize());
        bool samePts = b->automation.points (id).size() == a->automation.points (id).size();
        for (size_t i = 0; samePts && i < a->automation.points (id).size(); ++i)
            samePts = std::abs (b->automation.points (id)[i].beat - a->automation.points (id)[i].beat) < 1.0e-5
                      && std::abs (b->automation.points (id)[i].value - a->automation.points (id)[i].value) < 1.0e-5f;
        check (samePts && b->automation.bars() == 1 && b->automation.ccFor (id) == cc,
               "la automatización se guarda con el proyecto");
        // export
        auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("racoon_auto_" + name + ".mid");
        check (a->exportMidi (f), "Exportar escribe el .mid");
        FileInputStream is (f);
        MidiFile mf;
        int notes = 0, ccs = 0;
        if (mf.readFrom (is))
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (auto* e : *mf.getTrack (t))
                {
                    notes += e->message.isNoteOn() ? 1 : 0;
                    ccs += e->message.isController() && e->message.getControllerNumber() == cc ? 1 : 0;
                }
        check (notes >= jmax (1, (int) a->getPattern().size()) && ccs > 4, "el .mid trae " + String (notes) + " notas y " + String (ccs) + " movimientos de CC " + String (cc));
        f.deleteFile();
        // "A la maqueta": the sequence and its automation go to the song as a request Racoon picks up
        if (a->canPassToMaqueta())
        {
            a->updateTrackProperties ({ String ("Canal de ") + name, std::nullopt });
            const auto mid = maqueta::send (a->maquetaMidi(), a->info.animal, a->trackName(), a->maquetaKind());
            const auto note = JSON::parse (maqueta::inbox().getChildFile (mid.getFileName().fromLastOccurrenceOf (" ", false, false)
                                                                              .upToLastOccurrenceOf (".", false, false) + ".json"));
            FileInputStream ms (mid);
            MidiFile m2;
            int n2 = 0, c2 = 0;
            if (m2.readFrom (ms))
                for (int t = 0; t < m2.getNumTracks(); ++t)
                    for (auto* e : *m2.getTrack (t)) { n2 += e->message.isNoteOn() ? 1 : 0; c2 += e->message.isController() ? 1 : 0; }
            const int wantNotes = a->maquetaKind() == "generator" ? 1 : (int) a->getPattern().size();
            check (mid.existsAsFile() && note["animal"].toString() == a->info.animal && note["track"].toString() == "Canal de " + name
                       && n2 >= wantNotes && c2 > 4,
                   "Pegar en maqueta deja su secuencia para Racoon (" + String (n2) + " notas, " + String (c2) + " CC, canal «" + note["track"].toString() + "»)");
        }
        // double click on a clip in Racoon: the plugin's window loads the clip, "Devolver al clip" sends it back there
        if (a->canPassToMaqueta())
        {
            setenv ("RACOON_HOST_IS_RACOON", "1", 1);
            MidiMessageSequence cs;
            Array<int> sentPitches;
            for (int k = 0; k < 12; ++k)
            {
                const int pitch = 48 + (k * 5) % 24;
                cs.addEvent (MidiMessage::noteOn (1, pitch, (uint8) (90 + k)), k * 0.75 * 960.0);
                cs.addEvent (MidiMessage::noteOff (1, pitch), k * 0.75 * 960.0 + 300.0);
                sentPitches.add (pitch);
            }
            cs.updateMatchedPairs();
            MidiFile cf;
            cf.setTicksPerQuarterNote (960);
            cf.addTrack (cs);
            maqueta::requestEdit (cf, a->info.animal, a->trackName(), "clip-77", "Mi clip", 12.0, 101.0);
            std::unique_ptr<AudioProcessorEditor> ce (a->createEditorIfNeeded());
            for (int i = 0; i < 30 && ! maqueta::editInbox().findChildFiles (File::findFiles, false, "*.json").isEmpty(); ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (100);
            std::function<maqueta::Button* (Component&)> findButton = [&] (Component& c) -> maqueta::Button*
            {
                if (auto* b = dynamic_cast<maqueta::Button*> (&c)) return b;
                for (auto* ch : c.getChildren()) if (auto* b = findButton (*ch)) return b;
                return nullptr;
            };
            auto* button = ce != nullptr ? findButton (*ce) : nullptr;
            const bool loaded = a->dockShowsRoll() ? (int) a->getPattern().size() == 12 && a->patternBars() == 4 && a->getPattern()[0].pitch == 48
                                                   : a->songClip.active();
            check (maqueta::editInbox().findChildFiles (File::findFiles, false, "*").isEmpty() && loaded && a->songClip.clip == "clip-77"
                       && button != nullptr && button->getButtonText().containsIgnoreCase ("Devolver al clip"),
                   "doble clic en un clip de la maqueta: se abre aquí con sus 12 notas y el botón dice «" + (button != nullptr ? button->getButtonText() : String()) + "»");
            if (button != nullptr)
            {
                for (auto& j : maqueta::inbox().findChildFiles (File::findFiles, false, "*")) j.deleteFile();
                button->triggerClick();
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                var back;
                File backMid;
                for (auto& j : maqueta::inbox().findChildFiles (File::findFiles, false, "*.json"))
                {
                    auto v = JSON::parse (j.loadFileAsString());
                    if (v["clip"].toString() == "clip-77") { back = v; backMid = maqueta::inbox().getChildFile (v["midi"].toString()); }
                }
                Array<int> gotPitches;
                FileInputStream bs (backMid);
                MidiFile bm;
                if (backMid.existsAsFile() && bm.readFrom (bs))
                    for (int t = 0; t < bm.getNumTracks(); ++t)
                        for (auto* e : *bm.getTrack (t))
                            if (e->message.isNoteOn() && e->message.getTimeStamp() < 12.0 * bm.getTimeFormat()) gotPitches.add (e->message.getNoteNumber());
                auto sortedSent = sentPitches;
                sortedSent.sort();
                gotPitches.sort();
                auto intsToText = [] (const Array<int>& a) { String t; for (auto v : a) t << v << " "; return t; };
                check (back.isObject() && gotPitches == sortedSent && ! a->songClip.active() && ! button->getButtonText().containsIgnoreCase ("clip"),
                       "Devolver al clip le regresa las mismas notas a ese clip (" + String (gotPitches.size()) + ") y el botón vuelve a «"
                       + button->getButtonText() + "»" + (gotPitches == sortedSent ? String() : " enviadas " + intsToText (sortedSent) + " recibidas " + intsToText (gotPitches)) + ")");
                for (auto& j : maqueta::inbox().findChildFiles (File::findFiles, false, "*")) j.deleteFile();
                if (a->dockShowsRoll())
                {
                    // the clip opened again and changed here: the plugin says so, and when Racoon asks for it on
                    // closing (Guardar en el clip) it goes back by itself
                    maqueta::requestEdit (cf, a->info.animal, a->trackName(), "clip-78", "Otro clip", 12.0, 101.0);
                    for (int i = 0; i < 30 && ! a->songClip.active(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (100);
                    MessageManager::getInstance()->runDispatchLoopUntil (600);
                    const auto status = maqueta::statusInbox();
                    const auto flag = status.getChildFile ("clip-78.cambios");
                    const bool cleanAtStart = a->songClip.clip == "clip-78" && ! flag.existsAsFile();
                    auto pat = a->getPattern();
                    if (! pat.empty()) pat[0].pitch += 2;
                    a->setPattern (pat);
                    MessageManager::getInstance()->runDispatchLoopUntil (700);
                    const bool flagged = flag.existsAsFile();
                    status.getChildFile ("clip-78.devolver").create();
                    for (int i = 0; i < 20 && status.getChildFile ("clip-78.devolver").existsAsFile(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (100);
                    bool sentBack = false;
                    for (auto& j : maqueta::inbox().findChildFiles (File::findFiles, false, "*.json"))
                        if (JSON::parse (j.loadFileAsString())["clip"].toString() == "clip-78") sentBack = true;
                    check (cleanAtStart && flagged && sentBack && ! flag.existsAsFile() && ! a->songClip.active(),
                           "editar un clip y cerrar: el plugin avisa que cambió (" + String ((int) flagged) + ") y con «Guardar en el clip» lo devuelve solo ("
                           + String ((int) sentBack) + ")");
                    for (auto& j : maqueta::inbox().findChildFiles (File::findFiles, false, "*")) j.deleteFile();
                }
            }
            a->editorBeingDeleted (ce.get());
            ce.reset();
            unsetenv ("RACOON_HOST_IS_RACOON");
        }
        else check (true, "Hedgehog vive en el Master: sin botón Pegar en maqueta");

        // undo removes the line made in this step
        std::unique_ptr<RacoonProcessor> u (make());
        u->undoManager.beginNewTransaction();
        u->automation.setPoints (id, { { 0.0, 0.2f } });
        const bool had = u->automation.hasLane (id);
        u->undoManager.undo();
        check (had && ! u->automation.hasLane (id), "dibujar una línea se puede deshacer");

        // the strip opens from the editor (right click > Ver automatización)
        std::unique_ptr<AudioProcessorEditor> ed (a->createEditorIfNeeded());
        if (auto* re = dynamic_cast<RacoonEditor*> (ed.get()))
        {
            re->openAutomation (id, false);
            MessageManager::getInstance()->runDispatchLoopUntil (120);
            auto* panel = re->getAutomationPanel();
            check (panel != nullptr && panel->isVisible() && panel->getHeight() > 150 && panel->currentParam() == id,
                   "Ver automatización abre la línea de " + a->automation.nameOf (id));
            tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_automatizacion.png"));
        }
    }

    // editor builds and draws, in both layers
    {
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditorIfNeeded());
        if (auto* re = dynamic_cast<RacoonEditor*> (ed.get()))
        {
            re->setEasy (true);
            MessageManager::getInstance()->runDispatchLoopUntil (150);
            {
                auto area = Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
                check (re->getDock() != nullptr && re->getWidth() == area.getWidth() && re->getHeight() <= area.getHeight(),
                       "ventana al ancho de la pantalla (" + String (re->getWidth()) + " x " + String (re->getHeight()) + ") con Play, deshacer, secuencia y teclado");
            }
            tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_facil.png"));
            re->setEasy (false);
            MessageManager::getInstance()->runDispatchLoopUntil (150);
            tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_completo.png"));
            check (true, "editor en PRINCIPIANTE y AVANZADO"_u);
            if (auto* dock = re->getDock(); dock != nullptr && p->dockShowsRoll())
            {
                // the piano roll at the bottom grows when its top edge is pulled up
                const int before = dock->roll.getHeight(), dockY = dock->getY();
                auto ev = [&] (Point<int> at, int clicks)
                {
                    const auto sp = dock->localPointToGlobal (at).toFloat();
                    return MouseEvent (Desktop::getInstance().getMainMouseSource(), at.toFloat(), ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0,
                                       dock, dock, Time::getCurrentTime(), Point<float> ((float) dock->getWidth() / 2.0f, 3.0f), Time::getCurrentTime(), clicks, false)
                        .withNewPosition (at.toFloat());
                    ignoreUnused (sp);
                };
                dock->mouseDown (ev ({ dock->getWidth() / 2, 3 }, 1));
                dock->mouseDrag (ev ({ dock->getWidth() / 2, -150 }, 1));
                MessageManager::getInstance()->runDispatchLoopUntil (60);
                const int grown = dock->roll.getHeight() - before;
                check (grown >= 140 && dock->getY() < dockY - 140, "arrastrar el borde de arriba hace el piano roll más alto (+"_u + String (grown) + " px)");
                tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_piano_alto.png"));
                dock->setExtraHeight (0);
                MessageManager::getInstance()->runDispatchLoopUntil (40);
                check (dock->roll.getHeight() == before, "y vuelve a su tamaño");
            }
            if (auto* dock = re->getDock(); dock != nullptr && p->dockShowsRoll())
            {
                // Aleatoria: a new sequence for this plugin, different each time, that Deshacer takes away
                const auto before = p->getPattern();
                dock->generateRandom (7);
                const auto first = p->getPattern();
                dock->generateRandom (8);
                const auto second = p->getPattern();
                int lo = 127, hi = 0;
                for (auto& n : first) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
                check (! first.empty() && first != before && second != first && dock->randomButton.isVisible(),
                       "Aleatoria escribe una secuencia nueva (" + String ((int) first.size()) + " notas, de " + String (lo) + " a " + String (hi) + ") y otra distinta cada vez");
                tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_aleatoria.png"));
                p->undoManager.undo();
                p->undoManager.undo();
                auto sorted = [] (std::vector<SeqNote> v)
                {
                    std::sort (v.begin(), v.end(), [] (const SeqNote& a, const SeqNote& b)
                               { return std::tie (a.start, a.pitch, a.length, a.velocity) < std::tie (b.start, b.pitch, b.length, b.velocity); });
                    return v;
                };
                check (sorted (p->getPattern()) == sorted (before), "Deshacer dos veces deja la secuencia de antes");
            }
            if (auto* dock = re->getDock(); dock != nullptr && p->dockShowsRoll())
            {
                // the sequence works like Racoon's piano roll: a click selects (it does not write), a box selects
                // several, dragging moves them, arrows transpose, Cmd+Shift+arrow copies an octave away, Cmd+D
                // duplicates, double click writes; the pencil (B) writes with one click
                auto& roll = dock->roll;
                const auto before = p->getPattern();
                p->setPattern ({ { 0.0, 0.5, 60, 100, 1 }, { 1.0, 0.5, 63, 100, 1 }, { 2.0, 0.5, 65, 100, 1 } }, false);
                MessageManager::getInstance()->runDispatchLoopUntil (80);
                roll.showFromStart();
                roll.showPitch (63);
                auto src = Desktop::getInstance().getMainMouseSource();
                auto ev = [&] (Point<float> a, Point<float> down, bool dragged, int clicks = 1, ModifierKeys m = ModifierKeys (ModifierKeys::leftButtonModifier))
                { return MouseEvent (src, a, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, Time::getCurrentTime(), down, Time::getCurrentTime(), clicks, dragged); };
                auto click = [&] (Point<float> a) { roll.mouseDown (ev (a, a, false)); roll.mouseUp (ev (a, a, false)); };
                auto dragTo = [&] (Point<float> a, Point<float> b, ModifierKeys m = ModifierKeys (ModifierKeys::leftButtonModifier))
                { roll.mouseDown (ev (a, a, false, 1, m)); for (int k = 1; k <= 8; ++k) roll.mouseDrag (ev (a + (b - a) * (k / 8.0f), a, true, 1, m)); roll.mouseUp (ev (b, a, true, 1, m)); };
                auto has = [&] (double start, int pitch) { for (auto& n : p->getPattern()) if (std::abs (n.start - start) < 1.0e-6 && n.pitch == pitch) return true; return false; };
                click (roll.posFor (3.1, 62));
                const bool noWrite = p->getPattern().size() == 3;
                dragTo (roll.posFor (-0.0 + 0.02, 64), roll.posFor (1.6, 59));
                const bool box = roll.numSelected() == 2;
                dragTo (roll.posFor (0.1, 60), roll.posFor (1.1, 62));
                const bool movedOk = has (1.0, 62) && has (2.0, 65) && has (2.0, 65) && ! has (0.0, 60) && p->getPattern().size() == 3;
                roll.keyPressed (KeyPress (KeyPress::upKey, ModifierKeys::shiftModifier, 0));
                const bool octave = has (1.0, 74) && has (2.0, 77) && has (2.0, 65);
                roll.keyPressed (KeyPress (KeyPress::downKey, ModifierKeys (ModifierKeys::shiftModifier | ModifierKeys::commandModifier), 0));
                const bool dupOct = has (1.0, 74) && has (1.0, 62) && has (2.0, 77) && has (2.0, 65) && p->getPattern().size() == 5;
                roll.keyPressed (KeyPress ('d', ModifierKeys::commandModifier, 0));
                const bool dup = has (2.5, 62) && has (3.5, 65) && p->getPattern().size() == 7;
                roll.mouseDoubleClick (ev (roll.posFor (3.1, 70), roll.posFor (3.1, 70), false, 2));
                const bool dbl = p->getPattern().size() == 8;
                roll.keyPressed (KeyPress ('b', ModifierKeys(), 0));
                click (roll.posFor (0.6, 70));
                const bool pencil = roll.isDrawMode() && p->getPattern().size() == 9;
                roll.setDrawMode (false);
                check (noWrite && box && movedOk && octave && dupOct && dup && dbl && pencil,
                       "secuencia como el piano de Racoon: clic no escribe ("_u + String ((int) noWrite) + "), recuadro (" + String ((int) box)
                       + "), mover varias (" + String ((int) movedOk) + "), Mayús+flecha octava ("_u + String ((int) octave) + "), Cmd+Mayús+flecha copia una octava ("_u
                       + String ((int) dupOct) + "), Cmd+D (" + String ((int) dup) + "), doble clic escribe (" + String ((int) dbl) + "), lápiz ("_u + String ((int) pencil) + ")");
                p->setPattern (before, false);
            }
            if (name == "Squirrel" && racoon::biblio::Catalogo::get().disponible())
            {
                // the panel of Muestras Racoon: opens over the plugin, a click loads, CERRAR puts it away
                String elegido;
                racoon::biblio::PanelMuestras::mostrar (*re, "Muestras para Bombo", { "bombo" }, [&elegido] (const String& g) { elegido = g; });
                MessageManager::getInstance()->runDispatchLoopUntil (150);
                auto panels = re->getChildren();
                racoon::biblio::PanelMuestras* panel = nullptr;
                for (auto* c : panels) if (auto* pm = dynamic_cast<racoon::biblio::PanelMuestras*> (c)) panel = pm;
                check (panel != nullptr && panel->isVisible() && panel->getWidth() > 300, "MUESTRAS abre un panel sobre el plugin");
                if (panel != nullptr)
                {
                    for (auto* c : panel->getChildren()) if (auto* lb = dynamic_cast<ListBox*> (c)) lb->selectRow (2);
                    tu::shot (*re, shots.getChildFile ("panel_muestras.png"));
                    check (elegido.isNotEmpty(), "un clic en la lista carga el sonido: " + elegido);
                    panel->cerrar();
                    MessageManager::getInstance()->runDispatchLoopUntil (100);
                    bool still = false;
                    for (auto* c : re->getChildren()) still = still || dynamic_cast<racoon::biblio::PanelMuestras*> (c) != nullptr;
                    check (! still, "CERRAR lo guarda");
                }
            }
        }
        else check (false, "editor");
    }
}

// each plugin's own checks live in its folder (PluginTests.inc files are pulled in here)
#include "AllPluginTests.inc"


// ===================================================================== tanda 4: preview synth and sample reading
#include "PreviewSound.h"
#include "ref/OldPreviewSound.inc"
namespace tanda4
{
// power that is not the tone or its harmonics, relative to all of it
static double inharmDb (const std::vector<float>& x, double f0, double sr)
{
    const int order = 14, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> d ((size_t) n * 2, 0.0f);
    const int start = (int) x.size() - n - 2000;
    for (int i = 0; i < n; ++i) d[(size_t) i] = x[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (n - 1)));
    fft.performFrequencyOnlyForwardTransform (d.data());
    double total = 0, alias = 0;
    for (int k = 2; k < n / 2; ++k)
    {
        const double hz = k * sr / n, pw = (double) d[(size_t) k] * d[(size_t) k];
        total += pw;
        bool harm = false;
        for (int h = 1; h * f0 < sr * 0.5; ++h) if (std::abs (hz - h * f0) < 6.0 * sr / n) harm = true;
        if (! harm) alias += pw;
    }
    return 10.0 * std::log10 (alias / std::max (1.0e-30, total) + 1.0e-30);
}

template <class PS>
static std::vector<float> renderNote (int note, double sr, int len, typename PS::Patch patch)
{
    PS ps;
    ps.prepare (sr);
    ps.setKind (PS::Kind::synth);
    ps.setPatch (patch);
    MidiBuffer m;
    m.addEvent (MidiMessage::noteOn (1, note, (uint8) 110), 0);
    AudioBuffer<float> b (2, len);
    b.clear();
    ps.render (b, m, 0, len, 1.0f);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + len);
}

static float hermite (const float* d, int len, double pos)
{
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto at = [d, len] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

static void run()
{
    std::cout << "-- tanda 4: sinte de vista previa y lectura de muestras" << std::endl;
    const double sr = 48000.0;
    // 1. the preview synth's oscillators are band-limited now: a high bass-patch note (no detune, so every partial is a harmonic)
    for (int note : { 84, 96 })
    {
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const auto nw = renderNote<racoon::PreviewSound> (note, sr, 24000, racoon::PreviewSound::Patch::bass);
        const auto od = renderNote<racoon_old::PreviewSound> (note, sr, 24000, racoon_old::PreviewSound::Patch::bass);
        // the bass patch has a sine an octave down: the harmonic series starts there
        const double a = inharmDb (nw, f0 * 0.5, sr), b = inharmDb (od, f0 * 0.5, sr);
        std::cout << "   nota " << note << ": aliasing " << String (b, 1) << " dB -> " << String (a, 1) << " dB" << std::endl;
        check (a < b - 10.0 && a < -40.0, "la vista previa (nota " + String (note) + ") ya no dobla armonicos: " + String (a, 1) + " dB");
    }
    // 2. every patch and every drum stays finite and makes sound
    {
        for (int pi = 0; pi < 4; ++pi)
        {
            const auto x = renderNote<racoon::PreviewSound> (60, sr, 12000, (racoon::PreviewSound::Patch) pi);
            float pk = 0.0f; bool fin = true;
            for (auto s : x) { fin &= std::isfinite (s); pk = std::max (pk, std::abs (s)); }
            check (fin && pk > 0.02f && pk < 1.5f, "el patch " + String (pi) + " suena y no se pasa (pico " + String (pk, 3) + ")");
        }
        racoon::PreviewSound ps;
        ps.prepare (sr);
        ps.setKind (racoon::PreviewSound::Kind::drums);
        bool ok = true;
        for (int gm = 35; gm <= 82; ++gm)
        {
            ps.reset();
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (10, gm, (uint8) 127), 0);
            AudioBuffer<float> b (2, 24000); b.clear();
            ps.render (b, m, 0, 24000, 1.0f);
            float pk = b.getMagnitude (0, 24000);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 24000; ++i) ok &= std::isfinite (b.getSample (c, i));
            if (! (pk > 0.01f && pk < 1.6f)) { ok = false; std::cout << "   tambor " << gm << " pico " << pk << std::endl; }
        }
        check (ok, "los 48 tambores GM de la vista previa suenan, finitos y sin pasarse");
    }
    // 3. sinc reading: bit for bit at speed 1, far more exact than Hermite when pitched, and it filters before folding
    {
        const auto& st = racoon::sdsp::SincTable::get();
        Random rnd (7);
        std::vector<float> src (4096);
        for (auto& s : src) s = rnd.nextFloat() * 2.0f - 1.0f;
        bool exact = true;
        for (int i = 0; i < 4096; ++i) exact &= st.read (src.data(), 4096, (double) i, 0) == src[(size_t) i];
        check (exact, "la lectura sinc a velocidad 1 devuelve la muestra bit a bit");

        const int len = 96000;
        auto sine = [&] (double hz) { std::vector<float> v ((size_t) len); for (int i = 0; i < len; ++i) v[(size_t) i] = (float) std::sin (MathConstants<double>::twoPi * hz * i / sr); return v; };
        // a 3 kHz tone read a fifth up (as a sampler plays a note seven semitones above its root)
        {
            const auto s = sine (3000.0);
            const double speed = std::pow (2.0, 7.0 / 12.0);
            const int band = racoon::sdsp::SincTable::bandFor (speed);
            double eS = 0, eH = 0, eL = 0, ref = 0;
            for (int i = 2000; i < 40000; ++i)
            {
                const double pos = i * speed + 0.37;
                const double ideal = std::sin (MathConstants<double>::twoPi * 3000.0 * pos / sr);
                const int k = (int) pos; const double fr = pos - k;
                const double lin = s[(size_t) k] + (s[(size_t) k + 1] - s[(size_t) k]) * fr;
                eS += std::pow (st.read (s.data(), len, pos, band) - ideal, 2.0);
                eH += std::pow (hermite (s.data(), len, pos) - ideal, 2.0);
                eL += std::pow (lin - ideal, 2.0);
                ref += ideal * ideal;
            }
            const double dS = 10 * std::log10 (eS / ref), dH = 10 * std::log10 (eH / ref), dL = 10 * std::log10 (eL / ref);
            std::cout << "   error leyendo 3 kHz una quinta arriba: lineal " << String (dL, 1) << " dB, Hermite " << String (dH, 1) << " dB, sinc " << String (dS, 1) << " dB" << std::endl;
            check (dS < dH - 20.0 && dS < -70.0, "la lectura sinc es mas exacta que Hermite (" + String (dS, 1) + " vs " + String (dH, 1) + " dB)");
        }
        // a 15 kHz tone read an octave up: there is no 30 kHz at 48 kHz, so whatever comes out is a fold-back
        {
            const auto s = sine (15000.0);
            const int band = racoon::sdsp::SincTable::bandFor (2.0);
            double pS = 0, pH = 0;
            for (int i = 2000; i < 40000; ++i)
            {
                const double pos = i * 2.0 + 0.5;
                pS += std::pow (st.read (s.data(), len, pos, band), 2.0);
                pH += std::pow (hermite (s.data(), len, pos), 2.0);
            }
            const double dS = 10 * std::log10 (pS / 38000.0 / 0.5), dH = 10 * std::log10 (pH / 38000.0 / 0.5);
            std::cout << "   15 kHz una octava arriba (todo lo que sale es doblez): Hermite " << String (dH, 1) << " dB, sinc " << String (dS, 1) << " dB" << std::endl;
            check (dS < dH - 20.0, "pitcheado hacia arriba, la lectura sinc filtra antes de doblar (" + String (dS, 1) + " dB)");
        }
        float a, b; bool ep = true;
        for (int i = 0; i <= 100; ++i) { racoon::sdsp::equalPower (i / 100.0f, a, b); ep &= std::abs (a * a + b * b - 1.0f) < 1.0e-5f; }
        check (ep, "el cruce de loop es de igual potencia");
    }
}
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    auto out = File::getCurrentWorkingDirectory().getChildFile ("family_test_out");
    // the maqueta requests go to a test folder, not to the real one Racoon watches
    const auto inboxDir = File::getCurrentWorkingDirectory().getChildFile ("family_test_maqueta");
    inboxDir.deleteRecursively();
    setenv ("RACOON_MAQUETA_DIR", inboxDir.getFullPathName().toRawUTF8(), 1);
    out.createDirectory();
    StringArray only;
    for (int i = 1; i < argc; ++i) only.add (argv[i]);
    if (only.isEmpty() || only.contains ("tanda4")) tanda4::run();
    only.removeString ("tanda4");
    if (! (argc == 2 && String (argv[1]) == "tanda4")) runAllPluginTests (out, only);
    std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}

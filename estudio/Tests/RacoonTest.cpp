#include "engine/SamplePreview.h"
// Run with RACOON_PLUGIN_PATH pointing at folders that contain "Racoon Badger.vst3" / "Racoon Hamster.vst3".
#include "../Source/engine/SamplePreview.h"
#include "engine/I18n.h"
#include "engine/Genres.h"
#include "engine/Arranger.h"
#include "engine/AudioTools.h"
#include "engine/Session.h"
#include "engine/Behaviour.h"
#include "engine/FamilyInstaller.h"
#include "engine/SoundLibrary.h"
#include "engine/Muestras.h"
#include "engine/MidiCapture.h"
#include "ui/Aleatorio.h"
#include "fx/NativeFx.h"
#if RACOON_WITH_UI_TESTS
 #include "ui/MainComponent.h"
#endif

using namespace racoon;
using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++failures;
}

struct FileStats { double seconds = 0, rms = 0, peak = 0; int channels = 0; };

static FileStats analyse (te::Engine& engine, const File& f)
{
    FileStats st;
    std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
    if (r == nullptr) return st;
    st.channels = (int) r->numChannels;
    st.seconds = (double) r->lengthInSamples / r->sampleRate;
    AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        st.peak = jmax (st.peak, (double) b.getMagnitude (c, 0, b.getNumSamples()));
        sum += std::pow ((double) b.getRMSLevel (c, 0, b.getNumSamples()), 2.0);
    }
    st.rms = std::sqrt (sum / jmax (1, b.getNumChannels()));
    return st;
}

static int countNotes (te::Edit& e)
{
    int n = 0;
    for (auto* t : te::getAudioTracks (e))
        for (auto* c : t->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c)) n += mc->getSequence().getNumNotes();
    return n;
}

static te::AudioTrack* findTrack (te::Edit& e, const String& namePart)
{
    for (auto* t : te::getAudioTracks (e))
        if (t->getName().contains (namePart)) return t;
    return nullptr;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    auto out = File::getCurrentWorkingDirectory().getChildFile ("racoon_test_out");
    out.deleteRecursively();
    out.createDirectory();

    auto* uiB = new RacoonUIBehaviour (true);
    te::Engine engine { "Racoon", std::unique_ptr<te::UIBehaviour> (uiB), std::make_unique<RacoonEngineBehaviour> (true) };
    // the browser's preview lets go of the audio device before the engine goes (it is a singleton)
    struct PreviewGuard { ~PreviewGuard() { SamplePreview::shutdown(); } } previewGuard;

    std::cout << "Racoon test" << std::endl;
    // las pruebas no copian Muestras Racoon (400 MB) a la carpeta de Música
    setenv ("RACOON_NO_COPIAR_MUESTRAS", "1", 1);
    // the taste points of the tests live in their own file, new every run
    setenv ("RACOON_GUSTO", out.getChildFile ("gusto.json").getFullPathName().toRawUTF8(), 1);
    // the blocks pick their sounds with a seed: the tests use a fixed one, so every run renders the same songs
    setenv ("RACOON_SEMILLA_SONIDOS", "20261025", 0);
    {
        const auto& cat = biblio::Catalogo::get();
        if (! cat.disponible()) std::cout << "  (sin Muestras Racoon) " << cat.error << std::endl;
        else
        {
            int conKit = 0;
            for (auto& g : genres()) conKit += muestras::kitParaGenero (g.id).isNotEmpty() ? 1 : 0;
            check (conKit == (int) genres().size(), "cada género tiene su kit de Muestras Racoon (" + String (conKit) + ")");
            const auto& m = cat.muestras.front();
            const auto vis = muestras::nombreVisible (cat.archivo (m.archivo));
            const auto carpeta = muestras::nombreVisible (cat.archivo (m.archivo).getParentDirectory());
            check (vis.startsWith (i18n::tr (m.nombre)) && carpeta.isNotEmpty(), "el navegador muestra los nombres de la biblioteca: " + vis + " en " + carpeta);
            int faltan = 0;
            for (auto& x : cat.muestras) if (! cat.archivo (x.archivo).existsAsFile()) ++faltan;
            for (auto& x : cat.instrumentos) if (! cat.archivo (x.archivo).existsAsFile()) ++faltan;
            check (faltan == 0 && cat.fuentes.size() >= 40, "los " + String ((int) cat.muestras.size()) + " archivos del catálogo existen y hay "
                   + String ((int) cat.fuentes.size()) + " fuentes con su licencia");
        }
    }
    std::cout << "plugins Racoon encontrados:" << std::endl;
    for (auto& it : Catalog::racoonItems (engine))
        std::cout << "  " << it.name << (it.available ? "  -> " + it.desc.fileOrIdentifier : String ("  (no instalado)")) << std::endl;
    const bool havePlugins = Catalog::findRacoon (engine, "Racoon Badger").has_value()
                              && Catalog::findRacoon (engine, "Racoon Hamster").has_value();

    // RACOON_EFECTOS=/carpeta: only writes the effects by genre there (to listen to them and measure them)
    if (auto dir = SystemStats::getEnvironmentVariable ("RACOON_EFECTOS", {}); dir.isNotEmpty())
    {
        auto res = library::buildEffects (File (dir));
        std::cout << "efectos: " << res.files << " " << res.error << std::endl;
        return res.error.isEmpty() ? 0 : 1;
    }

    // RACOON_MEZCLA="trap,techno": the block of each genre measured before the automatic mix and after it
    if (SystemStats::getEnvironmentVariable ("RACOON_MEZCLA", {}).isNotEmpty())
    {
        Session s (engine, uiB);
        auto measure = [&] (const String& tag, const String& id)
        {
            ExportOptions o;
            o.destination = File::getCurrentWorkingDirectory().getChildFile ("mezcla " + id + " " + tag + ".wav");
            o.sampleRate = 48000.0; o.loopOnly = true; o.tailSeconds = 0.5;
            s.exportMix (o);
            AudioBuffer<float> b; double sr = 48000.0;
            if (audio::readFile (engine, o.destination, b, sr))
                std::cout << "  " << id << " " << tag << ": " << audio::measureLoudness (b, sr).summary() << "; " << audio::measureBalance (b, sr).summary() << std::endl;
        };
        for (auto& id : StringArray::fromTokens (SystemStats::getEnvironmentVariable ("RACOON_MEZCLA", {}), ",", ""))
            if (auto* g = findGenre (id))
            {
                setenv ("RACOON_SIN_MEZCLA", "1", 1);
                s.newProject (true, false, g);
                for (int i = 0; i < 40; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
                measure ("antes", id);
                s.autoMix (true);
                const auto t0 = Time::getMillisecondCounterHiRes();
                for (auto& line : s.mixAndMaster (3)) std::cout << "    " << line.upToFirstOccurrenceOf (";", false, false) << std::endl;
                if (SystemStats::getEnvironmentVariable ("RACOON_MEZCLA_STEMS", {}) == id)
                {
                    ExportOptions so; so.destination = File::getCurrentWorkingDirectory().getChildFile ("stems " + id); so.sampleRate = 48000.0; so.stems = true; so.loopOnly = true;
                    Array<File> written;
                    s.exportStems (so, &written);
                    for (auto& f : written)
                    {
                        AudioBuffer<float> sb; double ssr = 48000.0;
                        if (audio::readFile (engine, f, sb, ssr))
                            std::cout << "      " << f.getFileNameWithoutExtension() << ": " << audio::measureLoudness (sb, ssr).summary().upToFirstOccurrenceOf (",", false, false)
                                      << "; " << audio::measureBalance (sb, ssr).summary() << std::endl;
                    }
                }
                if (SystemStats::getEnvironmentVariable ("RACOON_CERT", {}).isNotEmpty())
                {
                    const auto c = s.cleanTrackCertificate();
                    for (auto& l : c.lineas) std::cout << "      CERT " << l << std::endl;
                    for (auto& l : c.propios) std::cout << "      PROPIO " << l << std::endl;
                    for (auto& l : c.revisar) std::cout << "      REVISAR " << l << std::endl;
                }
                std::cout << "    (" << String ((Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s)" << std::endl;
                measure ("despues", id);
                if (auto mv = s.getEdit().getMasterVolumePlugin()) std::cout << "    master fader " << mv->getVolumeDb() << " dB" << std::endl;
                for (auto* p : s.getEdit().getMasterPluginList())
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
                        if (auto* pi = ext->getAudioPluginInstance())
                            for (auto* prm : pi->getParameters())
                                for (auto* n : { "Techo (dBTP)", "Salida", "Limitador: ganancia", "Entrada", "Limitador", "Destino" })
                                    if (prm->getName (64) == n) std::cout << "    " << p->getName() << " " << n << " = " << prm->getCurrentValueAsText() << " (" << prm->getValue() << ")" << std::endl;
                unsetenv ("RACOON_SIN_MEZCLA");
            }
        return 0;
    }

    // RACOON_PROGRAMAS=1: lists the presets of every Racoon instrument and the choices of its "Sonido"-like parameters
    if (SystemStats::getEnvironmentVariable ("RACOON_PROGRAMAS", {}).isNotEmpty())
    {
        Session s (engine, uiB);
        s.newProject (false);
        for (auto* product : { "Racoon - Hamster Drums", "Racoon - Badger Bass", "Racoon - Dormouse Pads", "Racoon - Chipmunk Chords",
                               "Racoon - Squirrel Sampler", "Racoon - Ferret Guitar", "Racoon - Porcupine Choir", "Racoon - Mole Rhythm" })
            if (auto* t = s.addRacoonInstrument (product))
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t)))
                    if (auto* pi = ext->getAudioPluginInstance())
                    {
                        std::cout << "== " << product << " (" << pi->getNumPrograms() << ")" << std::endl;
                        for (int i = 0; i < pi->getNumPrograms(); ++i) std::cout << "  P " << pi->getProgramName (i) << std::endl;
                        for (auto* prm : pi->getParameters())
                            if (prm->getNumSteps() > 1 && prm->getNumSteps() < 40)
                            {
                                std::cout << "  C " << prm->getName (64) << ":";
                                for (int i = 0; i < prm->getNumSteps(); ++i) std::cout << " | " << prm->getText ((float) i / (float) (prm->getNumSteps() - 1), 64);
                                std::cout << std::endl;
                            }
                    }
        return 0;
    }

    // RACOON_PROBE="Racoon - Ferret Guitar": one note per beat from E2 to E5, to check that it plays the notes it gets
    if (SystemStats::getEnvironmentVariable ("RACOON_PROBE", {}).isNotEmpty())
    {
        Session s (engine, uiB);
        s.newProject (false);
        s.setBpm (120.0);
        auto* t = s.addRacoonInstrument (SystemStats::getEnvironmentVariable ("RACOON_PROBE", {}));
        if (t == nullptr) { std::cout << "no plugin" << std::endl; return 1; }
        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t)))
            if (auto* pi = ext->getAudioPluginInstance())
                for (auto* prm : pi->getParameters())
                for (auto& set : StringArray::fromTokens (SystemStats::getEnvironmentVariable ("RACOON_PROBE_SET", {}), ";", ""))   // "Modo=Instrumento"
                {
                    if (set.upToFirstOccurrenceOf ("=", false, false) == prm->getName (64))
                        for (int i = 0; i < jmax (2, prm->getNumSteps()); ++i)
                            if (prm->getText ((float) i / (float) (jmax (2, prm->getNumSteps()) - 1), 64) == set.fromFirstOccurrenceOf ("=", false, false))
                                prm->setValueNotifyingHost ((float) i / (float) (jmax (2, prm->getNumSteps()) - 1));
                }
        if (auto* c = s.createMidiClip (*t, { 0_bp, te::BeatPosition::fromBeats (40.0) }, "probe"))
        if (auto chords = SystemStats::getEnvironmentVariable ("RACOON_PROBE_CHORDS", {}); chords.isNotEmpty())
        {
            // "41 53 57 60 65|46 53 58 62 65": one chord per two beats, strummed like the template does
            auto list = StringArray::fromTokens (chords, "|", "");
            const double step = jmax (0.1, SystemStats::getEnvironmentVariable ("RACOON_PROBE_STEP", "2").getDoubleValue());
            const double len = SystemStats::getEnvironmentVariable ("RACOON_PROBE_LEN", "1.8").getDoubleValue();
            const int per = jmax (1, SystemStats::getEnvironmentVariable ("RACOON_PROBE_REPEAT", "1").getIntValue());
            for (int k = 0; k < (int) (38.0 / step); ++k)
            {
                auto notes = StringArray::fromTokens (list[(k / per) % list.size()], " ", "");
                for (int i = 0; i < notes.size(); ++i)
                    c->getSequence().addNote (notes[i].getIntValue(), te::BeatPosition::fromBeats (k * step + 0.012 * i), te::BeatDuration::fromBeats (len), 88, 0, nullptr);
            }
        }
        else
        {
            const auto only = SystemStats::getEnvironmentVariable ("RACOON_PROBE_NOTE", {}).getIntValue();
            for (int k = 0; k < 37; ++k)
                c->getSequence().addNote (only > 0 ? only : 40 + k, te::BeatPosition::fromBeats (k), te::BeatDuration::fromBeats (0.9), 100, 0, nullptr);
        }
        MessageManager::getInstance()->runDispatchLoopUntil (2500);
        ExportOptions o;
        o.sampleRate = 44100.0;
        o.destination = out.getChildFile ("probe.wav");
        auto r = s.exportMix (o);
        std::cout << "probe " << r.getErrorMessage() << std::endl;
        return 0;
    }


    // RACOON_FX_HOST=1: every native effect on an audio track inside Racoon, rendered and compared with the track dry
    if (SystemStats::getEnvironmentVariable ("RACOON_FX_HOST", {}).isNotEmpty())
    {
        const double sr = 48000.0;
        const int n = (int) (sr * 4.0);
        AudioBuffer<float> mus (2, n);
        Random rng (7);
        const int beat = (int) (sr * 0.5);
        for (int i = 0; i < n; ++i)
        {
            const double t = (i % beat) / sr, th = (i % (beat / 2)) / sr;
            const int bn = i / beat;
            float v = (float) (std::sin (MathConstants<double>::twoPi * (50.0 + 120.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 7.0)) * 0.6f;
            if (bn % 2 == 1) v += (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * 18.0) * 0.3f;
            float hat = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-th * 90.0) * 0.12f;
            float ch = 0.0f;
            for (double f : { 220.0, 261.63, 329.63, 392.0 }) ch += (float) std::sin (MathConstants<double>::twoPi * f * i / sr + f) * 0.04f;
            mus.setSample (0, i, v + hat * 0.7f + ch); mus.setSample (1, i, v + hat + ch * 0.8f);
        }
        auto wav = out.getChildFile ("fxhost_in.wav");
        wav.deleteFile();
        {
            std::unique_ptr<AudioFormatWriter> w (WavAudioFormat().createWriterFor (wav.createOutputStream().release(), sr, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (mus, 0, n);
        }
        auto read = [&engine] (const File& f)
        {
            AudioBuffer<float> b;
            std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
            if (r != nullptr) { b.setSize ((int) r->numChannels, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true); }
            return b;
        };
        auto diffDb = [] (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
        {
            double d = 0, r = 0;
            const int len = jmin (a.getNumSamples(), b.getNumSamples());
            for (int c = 0; c < jmin (a.getNumChannels(), b.getNumChannels()); ++c)
                for (int i = 4800; i < len; ++i) { const double x = a.getSample (c, i) - b.getSample (c, i); d += x * x; r += (double) b.getSample (c, i) * b.getSample (c, i); }
            return 10.0 * std::log10 ((d + 1e-20) / (r + 1e-20));
        };
        Session s (engine, uiB);
        s.newProject (false);
        s.setBpm (120.0);
        auto* t = s.addAudioTrack ("prueba");
        s.addAudioFile (*t, wav, te::TimePosition());
        MessageManager::getInstance()->runDispatchLoopUntil (500);
        ExportOptions o;
        o.sampleRate = sr;
        o.tailSeconds = 0.5;
        o.destination = out.getChildFile ("fxhost_dry.wav");
        s.exportMix (o);
        const auto dry = read (o.destination);
        std::cout << "seco: " << dry.getNumSamples() << " muestras" << std::endl;
        const auto only = StringArray::fromTokens (SystemStats::getEnvironmentVariable ("RACOON_FX_HOST", {}), ",", "");
        for (auto& it : Catalog::nativeEffects ({}))
        {
            const auto* d = fx::find (it.builtInType);
            if (d == nullptr || d->kind != fx::Kind::effect) continue;
            if (only.size() > 0 && only[0] != "1" && ! only.contains (d->id)) continue;
            auto p = s.insertPlugin (*t, it);
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            o.destination = out.getChildFile ("fxhost_" + d->id + ".wav");
            s.exportMix (o);
            const auto wet = read (o.destination);
            std::cout << "  " << d->id.paddedRight (' ', 22) << " en Racoon: resto " << String (diffDb (wet, dry), 1) << " dB" << std::endl;
            // the knobs of its card, moved like the channel view moves them (Tracktion's parameter), one at a time to the top
            if (p != nullptr)
                for (auto& key : d->keys)
                    for (auto* ap : p->getAutomatableParameters())
                        if (ap->paramID == key || ap->paramID.endsWith (key) || ap->getParameterName() == key)
                        {
                            const float before = ap->getCurrentValue();
                            const auto range = ap->getValueRange();
                            ap->setParameter (range.getEnd(), sendNotification);
                            MessageManager::getInstance()->runDispatchLoopUntil (60);
                            o.destination = out.getChildFile ("fxhost_" + d->id + "_" + key + ".wav");
                            s.exportMix (o);
                            const auto moved = read (o.destination);
                            std::cout << "      " << key.paddedRight (' ', 12) << " (" << ap->getParameterName() << ") " << String (before, 3) << " -> "
                                      << String (range.getEnd(), 3) << ": cambio " << String (diffDb (moved, wet), 1) << " dB" << std::endl;
                            ap->setParameter (before, sendNotification);
                            break;
                        }
            if (p != nullptr) p->deleteFromParent();
            MessageManager::getInstance()->runDispatchLoopUntil (100);
        }
        return 0;
    }
    // RACOON_STEMS="trap,house": renders the stems of those genres (to check by ear and with scripts/afinacion.py)
    if (SystemStats::getEnvironmentVariable ("RACOON_STEMS", {}).isNotEmpty())
    {
        Session s (engine, uiB);
        auto ids = StringArray::fromTokens (SystemStats::getEnvironmentVariable ("RACOON_STEMS", {}), ",", "");
        if (ids[0] == "all") { ids.clear(); for (auto& g : genres()) ids.add (g.id); }
        for (auto& id : ids)
        {
            auto* g = findGenre (id);
            if (g == nullptr) continue;
            s.newProject (true, false, g);
            auto& ge = s.getEdit();
            auto ready = [&ge]
            {
                for (auto* t : te::getAudioTracks (ge))
                    for (auto* c : t->getClips())
                        if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                            if (! w->getPlaybackFile().isValid()) return false;
                return true;
            };
            for (int i = 0; i < 1200 && ! ready(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
            MessageManager::getInstance()->runDispatchLoopUntil (SystemStats::getEnvironmentVariable ("RACOON_STEMS_PUMP", "1500").getIntValue());
            if (SystemStats::getEnvironmentVariable ("RACOON_STEMS_DEBUG", {}).isNotEmpty())
                for (auto* t : te::getAudioTracks (ge))
                    if (t->getName().contains (SystemStats::getEnvironmentVariable ("RACOON_STEMS_DEBUG", {})))
                    {
                        int nn = 0;
                        for (auto* c : t->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) nn += mc->getSequence().getNumNotes();
                        std::cout << "  " << t->getName() << " notas " << nn << std::endl;
                        int pcs[12] {};
                        for (auto* c : t->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) for (auto* x : mc->getSequence().getNotes()) ++pcs[x->getNoteNumber() % 12];
                        std::cout << "    clases:"; for (int k = 0; k < 12; ++k) std::cout << " " << k << "=" << pcs[k]; std::cout << std::endl;
                        for (auto* plug : t->pluginList)
                        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (plug))
                            if (auto* pi = ext->getAudioPluginInstance())
                                for (auto* prm : pi->getParameters())
                                    if (prm->getParameterIndex() < 40 || prm->getName (64).containsIgnoreCase ("afinar") || prm->getName (64).containsIgnoreCase ("escala")
                                        || prm->getName (64).endsWith ("Tune") || prm->getName (64).endsWith ("Role") || prm->getName (64).endsWith (" Note") || prm->getName (64).contains ("Notas") || prm->getName (64).contains ("Biblioteca") || prm->getName (64).startsWith ("Granos") || prm->getName (64).startsWith ("Captura")) std::cout << "    " << prm->getName (64) << " = " << prm->getCurrentValueAsText() << std::endl;
                    }
            ExportOptions o;
            o.sampleRate = 44100.0;
            o.stems = true;
            o.destination = out.getChildFile ("stems_" + id);
            o.destination.deleteRecursively();
            Array<File> stems;
            auto r = s.exportStems (o, &stems);
            std::cout << id << " " << stems.size() << " stems " << r.getErrorMessage() << std::endl;
            MessageManager::getInstance()->runDispatchLoopUntil (300);
        }
        return 0;
    }

    if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_UI", {}).isEmpty())
    {
        // edit and analysis tools (quantize, velocities, silence, transients, audio to MIDI, key, loudness)
        extern int runToolsTests (Session&, const File&);
        extern int runTarareoTests (Session&, const File&);
        extern int runDjTests (Session&, const File&);
        Session s (engine, uiB);
        if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_DJ", {}).isNotEmpty())
        {
            failures += runDjTests (s, out);
            std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
            return failures == 0 ? 0 : 1;
        }
        if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_TARAREO", {}).isNotEmpty())
        {
            failures += runTarareoTests (s, out);
            std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
            return failures == 0 ? 0 : 1;
        }
        failures += runToolsTests (s, out);
        failures += runTarareoTests (s, out);
        failures += runDjTests (s, out);
        if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_TOOLS", {}).isNotEmpty())
        {
            std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
            return failures == 0 ? 0 : 1;
        }
    }

    if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_UI", {}).isNotEmpty())
    {
       #if RACOON_WITH_UI_TESTS
        extern int runUiTests (Session&, const File&);
        Session s (engine, uiB);
        failures += runUiTests (s, out);
       #endif
        std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
    }

    {
        // ------------------------------------------------ the block Racoon opens with: 16 bars, every channel playing
        std::cout << "bloque de inicio" << std::endl;
        Session s (engine, uiB);
        s.newProject (true);
        auto& e = s.getEdit();
        check (std::abs (s.getBpm() - 122.0) < 0.01, "bloque a 122 BPM");
        int channels = 0, full = 0;
        String shortOnes;
        for (auto* t : te::getAudioTracks (e))
        {
            if (t->getName() == "Reverb" || t->getName() == "Delay") continue;
            ++channels;
            double end = 0.0;
            for (auto* c : t->getClips())
                end = jmax (end, e.tempoSequence.toBeats (c->getPosition().getEnd()).inBeats());
            if (end >= 63.99) ++full; else shortOnes << t->getName() << " (" << String (end, 1) << ") ";
        }
        check (channels == 11, "11 canales de instrumentos y sonidos (Kick y Groove aparte): " + String (channels));
        check (full == channels, "todos los canales cubren el bloque de 64 tiempos " + shortOnes);
        check (te::getAudioTracks (e).size() == 13, "con los 2 retornos");
        check (countNotes (e) > 400, "notas del bloque: " + String (countNotes (e)));
        if (havePlugins)
        {
            int hosted = 0;
            for (auto* t : te::getAudioTracks (e))
                if (auto* p = Session::instrumentOf (*t)) hosted += dynamic_cast<te::ExternalPlugin*> (p) != nullptr ? 1 : 0;
            check (hosted == 9, "9 instrumentos Racoon alojados (Hamster dos veces: Kick y Groove): " + String (hosted));
            bool hedgehog = false;
            for (auto* p : e.getMasterPluginList()) hedgehog |= p->getName().contains ("Hedgehog");
            check (hedgehog, "Hedgehog en el Master");
        }
        std::cout << "  largo del bloque " << e.getLength().inSeconds() << " s" << std::endl;
        check (e.getLength().inSeconds() < 40.0, "el bloque dura lo que sus 16 compases");
        // the library loops follow the tempo (warp): their stretched copies are made in the background,
        // which in the app happens while the progress window runs; here the message loop is pumped by hand
        auto proxiesReady = [&e]
        {
            for (auto* t : te::getAudioTracks (e))
                for (auto* c : t->getClips())
                    if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                        if (! w->getPlaybackFile().isValid()) return false;
            return true;
        };
        for (int i = 0; i < 1200 && ! proxiesReady(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (proxiesReady(), "loops de la biblioteca listos para sonar a tempo");
        ExportOptions o;
        o.destination = out.getChildFile ("bloque.wav");
        o.sampleRate = 48000.0;
        auto r = s.exportMix (o);
        auto st = analyse (engine, o.destination);
        check (r.wasOk() && st.seconds > 30.0, "bloque exportado, " + String (st.seconds, 1) + " s");
        check (st.rms > 0.02 && st.peak <= 1.0001, "bloque con señal sin clip, rms " + String (st.rms, 4) + " pico " + String (st.peak, 3));
        o.stems = true;
        o.destination = out.getChildFile ("bloque stems");
        Array<File> stems;
        r = s.exportStems (o, &stems);
        check (r.wasOk() && stems.size() >= 11, "stems del bloque: " + String (stems.size()));
        for (auto& f : stems)
        {
            auto ss = analyse (engine, f);
            check (ss.rms > 0.002, "  suena " + f.getFileName() + " rms " + String (ss.rms, 4));
        }

        // ------------------------------------------------ every genre builds its block; a few of them are rendered
        // the 44 blocks run in their own pass (RACOON_GENRES=1): one process holding every plugin of 44 songs
        // in a row is more than the test machine allows, and the app only ever builds one
        const bool allGenres = SystemStats::getEnvironmentVariable ("RACOON_GENRES", {}).isNotEmpty();
        std::cout << "generos" << std::endl;
        if (allGenres)
        {
        int built = 0, withVoice = 0, notesChecked = 0, sampleKits = 0, tallerKits = 0;
        StringArray keysUsed;
        String wrong;
        StringArray voices, melodies;
        for (auto& g : genres())
        {
            s.newProject (true, false, &g);
            auto& ge = s.getEdit();
            int channels = 0, full = 0;
            for (auto* t : te::getAudioTracks (ge))
            {
                if (t->getName() == "Reverb" || t->getName() == "Delay") continue;
                ++channels;
                double end = 0.0;
                for (auto* c : t->getClips()) end = jmax (end, ge.tempoSequence.toBeats (c->getPosition().getEnd()).inBeats());
                if (end >= 63.99 || t->getName().startsWith ("Chinchilla")) ++full;
            }
            // every channel has its part written: notes in each MIDI clip, a sound in each audio channel
            int silentChannels = 0;
            for (auto* t : te::getAudioTracks (ge))
            {
                if (t->getName() == "Reverb" || t->getName() == "Delay") continue;
                int stuff = 0;
                for (auto* c : t->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c)) stuff += mc->getSequence().getNumNotes();
                    else if (dynamic_cast<te::WaveAudioClip*> (c) != nullptr) ++stuff;
                if (stuff == 0) { ++silentChannels; wrong << g.id << ":" << t->getName() << " vacío; "; }
            }
            int drumNotes = 0;     // the kick and the groove, each on its own Hamster channel
            for (auto* d : te::getAudioTracks (ge))
                if (d->getName().contains ("Hamster"))
                    for (auto* c : d->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) drumNotes += mc->getSequence().getNumNotes();
            bool kitOk = true;
            if (havePlugins)
                if (auto* d = findTrack (ge, "Hamster"))
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*d)))
                        if (auto* pi = ext->getAudioPluginInstance(); pi != nullptr && pi->getNumPrograms() > 10)
                        {
                            // a kit of the genre's pool (Muestras Racoon kits and synthesized ones), chosen by the seed
                            const auto prog = pi->getProgramName (pi->getCurrentProgram());
                            kitOk = Session::kitPoolFor (g).contains (prog);
                            if (prog.startsWith ("Muestras / ") || prog.startsWith ("Kits Racoon / ")) ++sampleKits;
                            if (prog.startsWith ("Kits Racoon / ")) ++tallerKits;
                        }
            const int wantChannels = std::strlen (g.kick) > 0 ? 11 : 10;    // Kick and Groove apart, when there is a kick
            bool ok = std::abs (s.getBpm() - g.bpm) < 0.01 && channels == wantChannels && full == wantChannels && countNotes (ge) > 150
                            && (drumNotes > 60 || String (g.id) == "ambient") && kitOk && silentChannels == 0;
            // what makes it its own song: the voice file, the melody and the key
            // the voice and the texture are MIDI that Chinchilla sings and Skunk plays: no audio files in the block
            int waves = 0;
            for (auto* t : te::getAudioTracks (ge))
                for (auto* c : t->getClips()) waves += dynamic_cast<te::WaveAudioClip*> (c) != nullptr ? 1 : 0;
            if (waves > 0) { ok = false; wrong << g.id << ": " << waves << " clips de audio; "; }
            if (auto* v = findTrack (ge, "Chinchilla"))
                for (auto* c : v->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                    {
                        String sig;
                        for (auto* n : mc->getSequence().getNotes()) sig << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << ";";
                        if (mc->getSequence().getNumNotes() > 0) voices.addIfNotAlreadyThere (String::toHexString (sig.hashCode64()));
                    }
            if (auto* m = findTrack (ge, "Squirrel"))
                for (auto* c : m->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                    {
                        String sig;
                        for (auto* n : mc->getSequence().getNotes()) sig << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << ";";
                        melodies.addIfNotAlreadyThere (String::toHexString (sig.hashCode64()));
                    }
            // in tune: every note of the pitched channels belongs to the song's key (in "harmonic" minor genres the
            // leading tone is allowed too: it is the V chord's third)
            {
                const int minorSt[] { 0, 2, 3, 5, 7, 8, 10 }, majorSt[] { 0, 2, 4, 5, 7, 9, 11 };
                bool inKey[12] {};
                for (int k = 0; k < 7; ++k) inKey[(g.key + (g.major ? majorSt[k] : minorSt[k])) % 12] = true;
                if (g.harmonic && ! g.major) inKey[(g.key + 11) % 12] = true;
                int total = 0, outside = 0;
                for (auto* t : te::getAudioTracks (ge))
                {
                    const auto n = t->getName();
                    if (! (n.contains ("Badger") || n.contains ("Dormouse") || n.contains ("Chipmunk") || n.contains ("Squirrel")
                           || n.contains ("Ferret") || n.contains ("Porcupine") || n.contains ("Chinchilla") || n.contains ("Skunk"))) continue;
                    for (auto* c : t->getClips())
                        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                            for (auto* nt : mc->getSequence().getNotes())
                            {
                                ++total;
                                if (! inKey[nt->getNoteNumber() % 12]) { ++outside; if (! wrong.contains (n)) wrong << "[" << n << " " << nt->getNoteNumber() << " @" << nt->getStartBeat().inBeats() << "] "; }
                            }
                }
                if (outside > 0) { wrong << g.id << ": " << outside << " notas fuera de " << keyName (g.key, g.major) << "; "; ok = false; }
                keysUsed.addIfNotAlreadyThere (camelot (g.key, g.major));
                notesChecked += total;
            }
            if (g.voice) ++withVoice;
            // what the app does between blocks: Tracktion lets the old plugins go one every 100 ms (some 45 with the mix)
            MessageManager::getInstance()->runDispatchLoopUntil (5000);
            if (ok) ++built; else wrong << g.id << " (" << channels << " canales, " << full << " llenos, " << drumNotes << " golpes, kit " << (int) kitOk << ") ";
        }
        check (voices.size() == withVoice && withVoice == (int) genres().size() && melodies.size() == (int) genres().size(),
               String (voices.size()) + " voces distintas y " + String (melodies.size()) + " melodías distintas: cada género es otra rola");
        check (built == (int) genres().size(), String (built) + " de " + String ((int) genres().size()) + " géneros arman su bloque de 16 compases " + wrong);
        if (havePlugins && biblio::Catalogo::get().disponible())
            check (sampleKits >= (int) genres().size() / 2, "Hamster toca con un kit de Muestras Racoon en " + String (sampleKits) + " de " + String ((int) genres().size()) + " géneros (los demás, uno sintetizado de su estilo)");
        if (havePlugins && biblio::Catalogo::get().disponible())
            check (tallerKits >= (int) genres().size() * 6 / 10, "en " + String (tallerKits) + " de " + String ((int) genres().size()) + " géneros el kit es un Kit Racoon (golpes del taller)");
        check (keysUsed.size() >= 13, String (keysUsed.size()) + " tonalidades distintas de la rueda de Camelot entre los 44 géneros (las 12 menores y dos mayores)");
        check (wrong.isEmpty() && notesChecked > 44 * 300, String (notesChecked) + " notas revisadas: todas dentro de la tonalidad de su género");
        std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
        return failures == 0 ? 0 : 1;
        }
        StringArray mixResults;
        bool mixOk = true;
        double mixSeconds = 0.0;
        for (auto id : { "trap", "dnb", "reggaeton", "techno", "lofi", "ambient" })
        {
            auto* g = findGenre (id);
            s.newProject (true, false, g);
            auto& ge = s.getEdit();
            auto ready = [&ge]
            {
                for (auto* t : te::getAudioTracks (ge))
                    for (auto* c : t->getClips())
                        if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                            if (! w->getPlaybackFile().isValid()) return false;
                return true;
            };
            for (int i = 0; i < 1200 && ! ready(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
            // the automatic mix: render, measure and correct, then the numbers of the final mix
            const auto t0 = Time::getMillisecondCounterHiRes();
            const auto passes = s.mixAndMaster (3);
            if (SystemStats::getEnvironmentVariable ("RACOON_MEZCLA_STEMS", {}) == id)
            {
                ExportOptions so; so.destination = out.getChildFile ("stems " + String (id)); so.sampleRate = 48000.0; so.stems = true;
                Array<File> written;
                s.exportStems (so, &written);
                for (auto& f : written)
                {
                    AudioBuffer<float> sb; double ssr = 48000.0;
                    if (audio::readFile (engine, f, sb, ssr))
                        std::cout << "      " << f.getFileNameWithoutExtension() << ": " << audio::measureLoudness (sb, ssr).summary().upToFirstOccurrenceOf (",", false, false)
                                  << "; " << audio::measureBalance (sb, ssr).summary() << std::endl;
                }
            }
            const double mixSecs = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            for (auto& line : passes) std::cout << "    " << line << std::endl;
            ExportOptions go;
            go.destination = out.getChildFile (String ("genero ") + id + ".wav");
            go.sampleRate = 48000.0;
            auto gr = s.exportMix (go);
            auto gs = analyse (engine, go.destination);
            // the clean-track certificate of the first song: what sounds, from where, under which license
            if (String (id) == "trap" && biblio::Catalogo::get().disponible())
            {
                File cf;
                const auto wr = s.writeCleanTrackCertificate (go.destination, &cf);
                const auto c = s.cleanTrackCertificate();
                const auto texto = cf.loadFileAsString();
                std::cout << texto.substring (0, 1600) << std::endl;
                const auto& cat = biblio::Catalogo::get();
                int enCatalogo = 0, conLicencia = 0, deKitsRacoon = 0, kickAjeno = 0;
                for (auto& l : c.lineas)
                {
                    const auto p = StringArray::fromTokens (l, "|", "");
                    const auto mid = p[3].trim();
                    if (cat.muestra (mid) != nullptr || cat.instrumento (mid.fromFirstOccurrenceOf (":", false, false)) != nullptr || mid.startsWith ("onda:")) ++enCatalogo;
                    if (auto it = cat.fuentes.find (p[4].trim()); it != cat.fuentes.end() && it->second.licencia.isNotEmpty()) ++conLicencia;
                    if (p[1].contains ("Kit Racoon")) ++deKitsRacoon;
                    if (p[0].trim().contains ("Kick") && ! p[1].contains ("Bombo")) ++kickAjeno;
                }
                // the SHA-256 written is the one of the exported file (checked against the standard "abc" vector too)
                auto abc = out.getChildFile ("abc.txt");
                abc.replaceWithText ("abc");
                const bool shaOk = Session::sha256Of (abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
                                   && texto.contains (Session::sha256Of (go.destination));
                check (wr.wasOk() && cf.existsAsFile() && c.muestras >= 8 && enCatalogo == c.lineas.size() && conLicencia == c.lineas.size() && shaOk,
                       "certificado de rola limpia: " + String (c.muestras) + " muestras de " + String (c.fuentes.size()) + " bibliotecas, todas en el catálogo con su licencia ("
                       + c.licencias.joinIntoString (", ") + "), con la huella SHA-256 del WAV"_u);
                check (deKitsRacoon > 0 && kickAjeno == 0,
                       String (deKitsRacoon) + " muestras salen de los golpes del Kit Racoon, cada una con sus capas, y el canal del bombo solo certifica el bombo (los pads que no toca no cuentan)");
                if (! cat.salas.empty())
                {
                    const auto salaId = s.getEdit().state.getProperty ("racoonSala").toString();
                    const auto* sala = cat.sala (salaId);
                    check (sala != nullptr && c.salas.size() == 1 && c.salas[0].contains (sala->nombre) && c.salas[0].contains (sala->licencia)
                           && texto.contains (sala->nombre),
                           "el retorno de reverb del trap suena en una sala real (" + (sala != nullptr ? sala->nombre + ", " + sala->licencia : String ("ninguna"))
                           + ") y el certificado la lista con su licencia");
                }
                check (c.revisar.isEmpty() && (c.creditos.isEmpty() || texto.contains (c.creditos[0])),
                       "nada queda por revisar y los créditos CC BY están listos para pegar: " + c.creditos.joinIntoString ("; ").substring (0, 160));
                // a muted channel does not sound: it leaves the certificate
                te::AudioTrack* muted = nullptr;
                // a channel that brings samples to the certificate (Squirrel when it plays a library instrument; its own
                // sounds bring none, and the block draws one or the other)
                for (auto* t : te::getAudioTracks (s.getEdit()))
                    for (auto& l : c.lineas)
                        if (l.startsWith (t->getName()) && (muted == nullptr || t->getName().contains ("Squirrel"))) muted = t;
                if (muted != nullptr)
                {
                    muted->setMute (true);
                    const auto c2 = s.cleanTrackCertificate();
                    muted->setMute (false);
                    bool fuera = true;
                    for (auto& l : c2.lineas) if (l.startsWith (muted->getName())) fuera = false;
                    check (fuera && c2.muestras < c.muestras, "un canal en silencio sale del certificado (" + String (c.muestras) + " muestras, " + String (c2.muestras) + " sin " + muted->getName() + ")");
                }
            }
            {
                AudioBuffer<float> mb; double msr = 48000.0;
                if (audio::readFile (engine, go.destination, mb, msr))
                {
                    const auto L = audio::measureLoudness (mb, msr);
                    const auto B = audio::measureBalance (mb, msr);
                    std::cout << "    MEZCLA " << id << " (" << String (mixSecs, 1) << " s): " << L.summary() << "; " << B.summary() << std::endl;
                    mixResults.add (String (id) + ": " + String (L.integratedLufs, 1) + " LUFS, " + String (L.truePeakDb, 1) + " dBTP, presencia "
                                    + String (B.bands[3], 1) + "%, correlación "_u + String (B.correlation, 2) + ", cresta " + String (B.crestDb, 1) + " dB");
                    const float target = (float) (double) s.getEdit().state.getProperty ("racoonMasterTarget", -14.0);
                    mixOk = mixOk && std::abs (L.integratedLufs - target) <= 1.5f && L.truePeakDb <= -0.5f
                            && B.bands[3] >= (String (id) == "ambient" || String (id) == "lofi" || String (id) == "trap" ? 2.5f : 4.0f)
                            && B.correlation <= 0.92f && B.correlation >= 0.5f;
                    mixSeconds = jmax (mixSeconds, mixSecs);
                }
            }
            const double want = 64.0 * 60.0 / g->bpm;
            check (gr.wasOk() && gs.rms > 0.02 && gs.peak <= 1.0001 && gs.seconds > want - 0.5,
                   String::fromUTF8 (g->name) + " a " + String (roundToInt (g->bpm)) + " BPM suena: " + String (gs.seconds, 1) + " s, rms " + String (gs.rms, 3));
            MessageManager::getInstance()->runDispatchLoopUntil (5000);   // the old block's plugins go (one every 100 ms)
        }

        check (mixOk, "mezcla automática: volumen en su destino (±1.5 LU), pico real bajo -0.5 dBTP, presencia y estéreo abierto en 6 géneros "_u
                      "(antes: trap 0.2 % de presencia, correlación 0.92): " + mixResults.joinIntoString (" / ") + " (la más lenta tardó " + String (mixSeconds, 1) + " s)");

        // ------------------------------------------------ ALEATORIA's generators: many patterns, not the same one moved
        {
            using namespace aleatorio;
            auto barSig = [] (const std::vector<Nota>& v, double from, double to, int onlyPitch = -1)
            {
                String sig;
                for (auto& n : v) if (n.start >= from && n.start < to && (onlyPitch < 0 || n.pitch == onlyPitch)) sig << n.pitch << "@" << String (n.start - from, 3) << " ";
                return sig;
            };
            String report;
            bool ok = true;
            for (auto e : { Estilo::cuatro, Estilo::afro, Estilo::trap, Estilo::dnb, Estilo::latino, Estilo::boombap })
            {
                std::set<String> drums, kicks, bass, gtr, chords;
                for (int seed = 1; seed <= 20; ++seed)
                {
                    Opciones o;
                    o.estilo = e; o.bars = 4; o.seed = seed * 7717; o.root = 9; o.minor = true;
                    o.rol = Rol::bateria;   auto d = generar (o);   drums.insert (barSig (d, 0.0, 16.0)); kicks.insert (barSig (d, 0.0, 4.0, 36));
                    o.rol = Rol::bajo;      bass.insert (barSig (generar (o), 0.0, 4.0));
                    o.rol = Rol::guitarra;  gtr.insert (barSig (generar (o), 0.0, 4.0));
                    o.rol = Rol::acordes;   chords.insert (barSig (generar (o), 0.0, 4.0));
                }
                report << nombreEstilo (e) << ": bateria " << (int) drums.size() << ", bombos " << (int) kicks.size() << ", bajos " << (int) bass.size()
                       << ", guitarras " << (int) gtr.size() << ", acordes " << (int) chords.size() << "; ";
                // the dembow's kick is four on the floor by nature: two kicks there are enough
                ok = ok && drums.size() >= 19 && kicks.size() >= (e == Estilo::latino ? 2u : 3u) && bass.size() >= 9 && gtr.size() >= 12 && chords.size() >= 10;
            }
            check (ok, "20 semillas por estilo dan 20 rolas distintas: " + report);
        }

        // ------------------------------------------------ VARIAR SONIDOS: each seed, other sounds in every instrument
        if (havePlugins)
        {
            s.newProject (true, false, findGenre ("house"));
            auto& ge = s.getEdit();
            auto instOf = [] (te::AudioTrack* t) -> juce::AudioPluginInstance*
            {
                if (t != nullptr)
                    for (auto* p : t->pluginList)
                        if (Catalog::isRacoonPlugin (*p))
                            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p)) return ext->getAudioPluginInstance();
                return nullptr;
            };
            auto textOf = [&] (te::AudioTrack* t, const String& name) -> String
            {
                if (auto* pi = instOf (t))
                    for (auto* prm : pi->getParameters()) if (prm->getName (64) == name) return prm->getCurrentValueAsText();
                return {};
            };
            auto stateOf = [&] (te::AudioTrack* t) -> String
            {
                if (auto* pi = instOf (t)) return Session::pluginStateXml (*pi);
                return {};
            };
            const auto firstSounds = StringArray::fromLines (ge.state.getProperty ("racoonSounds").toString());
            check (firstSounds.size() >= 9, "el bloque dice qué sonido tomó cada instrumento: " + firstSounds.joinIntoString (" / "));
            std::map<String, StringArray> seen;
            StringArray first;
            bool libraryMelody = false, moleKit = false, inKey = true;
            for (juce::uint32 seed = 1; seed <= 10; ++seed)
            {
                const auto ch = s.varySounds (seed);
                if (seed == 1) first = ch;
                for (auto& line : ch)
                {
                    const auto animal = line.upToFirstOccurrenceOf (":", false, false), what = line.fromFirstOccurrenceOf (": ", false, false);
                    seen[animal].addIfNotAlreadyThere (what);
                    if (animal == "Squirrel" && stateOf (findTrack (ge, "Squirrel")).contains ("instrumentos/")) libraryMelody = true;
                    if (animal == "Mole" && ! what.contains ("sintetizado") && stateOf (findTrack (ge, "Mole")).contains ("kit=\"") && ! stateOf (findTrack (ge, "Mole")).contains ("kit=\"\"")) moleKit = true;
                }
                // whatever preset came, the song keeps its key and the modes Racoon set
                inKey = inKey && textOf (findTrack (ge, "Ferret"), "Modo") == "Instrumento" && textOf (findTrack (ge, "Badger"), "Seq Mode") == "Off"
                              && textOf (findTrack (ge, "Porcupine"), "Tonalidad") == "Fa" && textOf (findTrack (ge, "Chipmunk"), "Acordes") == "Apagado";
                MessageManager::getInstance()->runDispatchLoopUntil (30);
            }
            String report;
            int varied = 0;
            for (auto& [a, l] : seen) { report << a << " " << l.size() << ", "; if (l.size() >= 3) ++varied; }
            check (seen.size() >= 9 && varied >= 9, "diez semillas, diez juegos de sonidos por instrumento: " + report);
            check (seen["Hamster"].size() >= 5 && seen["Badger"].size() >= 6 && seen["Ferret"].size() >= 6 && seen["Chipmunk"].size() >= 4,
                   "la batería, el bajo, la guitarra y Chipmunk cambian de verdad: " + seen["Hamster"].joinIntoString (" | "));
            check (s.varySounds (1) == first, "la misma semilla da los mismos sonidos");
            check (inKey, "con cada juego de sonidos el bloque sigue en Fa menor, Ferret toca las notas del clip y Badger no prende su secuenciador");
            if (biblio::Catalogo::get().disponible())
                check (libraryMelody && moleKit, "la melodía a veces toca un instrumento de la biblioteca (" + String ((int) libraryMelody) + ") y Mole cambia de kit de muestras (" + String ((int) moleKit) + ")");
            // the channels in skip keep their sound
            {
                auto* bassT = findTrack (ge, "Badger");
                const auto before = stateOf (bassT);
                s.varySounds (77, { bassT });
                check (stateOf (bassT) == before, "un canal fijado se queda con su sonido");
            }
        }

        // ------------------------------------------------ Cmd+D on a range that crosses channels with many clips
        {
            s.newProject (true);
            auto& ge = s.getEdit();
            Array<te::AudioTrack*> tracks;
            for (auto* t : te::getAudioTracks (ge)) if (t->getName() != "Reverb" && t->getName() != "Delay") tracks.add (t);
            const int clipsBefore = [&] { int n = 0; for (auto* t : tracks) n += t->getClips().size(); return n; }();
            s.copyRange (tracks, 6.0, 22.0);
            ge.getUndoManager().beginNewTransaction();
            auto pasted = s.pasteAt (*tracks.getFirst(), 64.0);
            const int clipsAfter = [&] { int n = 0; for (auto* t : tracks) n += t->getClips().size(); return n; }();
            check (pasted.size() >= 10 && clipsAfter == clipsBefore + pasted.size(),
                   "duplicar 4 compases de los 10 canales (con los 8 loops de Skunk): " + String (pasted.size()) + " pedazos, sin trabarse");
        }
    }

    {
        Session s (engine, uiB);

        // ------------------------------------------------ ranges: copy half a clip, paste it, delete a piece, move clips together
        {
            std::cout << "rangos y seleccion" << std::endl;
            s.newProject (false);
            auto& ed = s.getEdit();
            auto* t1 = s.addAudioTrack ("Uno");
            auto* t2 = s.addAudioTrack ("Dos");
            auto beatsOf = [&ed] (te::Clip* c) { return std::make_pair (ed.tempoSequence.toBeats (c->getPosition().getStart()).inBeats(),
                                                                        ed.tempoSequence.toBeats (c->getPosition().getEnd()).inBeats()); };
            auto visibleNotes = [] (te::MidiClip* c)
            {
                int n = 0;
                const double off = c->getOffsetInBeats().inBeats(), len = c->getLengthInBeats().inBeats();
                for (auto* nt : c->getSequence().getNotes())
                    if (nt->getStartBeat().inBeats() >= off - 1.0e-6 && nt->getStartBeat().inBeats() < off + len - 1.0e-6) ++n;
                return n;
            };
            for (auto* t : { t1, t2 })
                if (auto* c = s.createMidiClip (*t, { 0_bp, te::BeatPosition::fromBeats (8.0) }, "Ocho"))
                    for (int b = 0; b < 8; ++b) c->getSequence().addNote (60 + b, te::BeatPosition::fromBeats (b), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
            const int undoBefore = ed.getUndoManager().getNumActionsInCurrentTransaction();
            s.copyRange ({ t1, t2 }, 2.0, 4.0);
            check (s.hasClipboard() && std::abs (s.clipboardBeats() - 2.0) < 1.0e-9 && t1->getClips().size() == 1 && t2->getClips().size() == 1
                       && ed.getUndoManager().getNumActionsInCurrentTransaction() <= undoBefore + 1,
                   "copiar 2 tiempos de dos canales no cambia la canción");
            ed.getUndoManager().beginNewTransaction();
            auto pasted = s.pasteAt (*t1, 16.0);
            bool okPaste = pasted.size() == 2;
            for (auto* c : pasted)
            {
                auto [a, b] = beatsOf (c);
                okPaste = okPaste && std::abs (a - 16.0) < 1.0e-6 && std::abs (b - 18.0) < 1.0e-6;
                if (auto* mc = dynamic_cast<te::MidiClip*> (c)) okPaste = okPaste && visibleNotes (mc) == 2;
            }
            check (okPaste && t1->getClips().size() == 2 && t2->getClips().size() == 2, "pegar el pedazo: 2 clips de 2 tiempos con sus 2 notas, en el compás 5");
            ed.getUndoManager().beginNewTransaction();
            s.deleteRange ({ t1 }, 0.0, 2.0);
            double firstStart = 99.0;
            for (auto* c : t1->getClips()) firstStart = jmin (firstStart, beatsOf (c).first);
            check (std::abs (firstStart - 2.0) < 1.0e-6 && t1->getClips().size() == 2, "borrar los primeros 2 tiempos de un canal deja el resto en su lugar");
            ed.getUndoManager().undo();
            check (t1->getClips().size() == 2 && std::abs (beatsOf (t1->getClips().getFirst()).first) < 1.0e-6, "y se deshace");
            // move two clips together: 4 beats later and one channel down
            auto* t3 = s.addAudioTrack ("Tres");
            Array<te::Clip*> two { t1->getClips()[0], t2->getClips()[0] };
            ed.getUndoManager().beginNewTransaction();
            s.moveClips (two, 4.0, 1);
            check (std::abs (beatsOf (two[0]).first - 4.0) < 1.0e-6 && std::abs (beatsOf (two[1]).first - 4.0) < 1.0e-6
                       && two[0]->getTrack() == t2 && two[1]->getTrack() == t3,
                   "mover dos clips juntos: 4 tiempos y un canal abajo, los dos");
        }

        // ------------------------------------------------ a pattern exported from a plugin (notes + CC) dropped into Racoon
        {
            std::cout << "soltar un .mid exportado" << std::endl;
            s.newProject (false);
            MidiMessageSequence seq;
            for (int b = 0; b < 8; ++b)
            {
                seq.addEvent (MidiMessage::noteOn (1, 60 + (b % 4), (uint8) 100), b * 960.0);
                seq.addEvent (MidiMessage::noteOff (1, 60 + (b % 4)), b * 960.0 + 900.0);
            }
            for (int k = 0; k < 32; ++k) seq.addEvent (MidiMessage::controllerEvent (1, 20, k * 4), k * 240.0);
            seq.updateMatchedPairs();
            MidiFile mf;
            mf.setTicksPerQuarterNote (960);
            mf.addTrack (seq);
            auto f = out.getChildFile ("Racoon Dormouse - prueba.mid");
            { FileOutputStream os (f); mf.writeTo (os); }
            auto* t = s.trackForMidiFile (f);
            auto* c = t != nullptr ? s.addMidiFile (*t, f, 0_tp) : nullptr;
            check (c != nullptr && c->getSequence().getNumNotes() == 8 && c->getSequence().getNumControllerEvents() == 32,
                   "el .mid entra con sus notas y su CC: " + String (c != nullptr ? c->getSequence().getNumNotes() : -1) + " notas, "
                   + String (c != nullptr ? c->getSequence().getNumControllerEvents() : -1) + " CC");
            if (havePlugins && t != nullptr)
                check (Session::instrumentOf (*t) != nullptr && Session::instrumentOf (*t)->getName().contains ("Dormouse"),
                       "un .mid de Dormouse abre un canal con Dormouse");

            // "A la maqueta": the plugin leaves a request, Racoon puts the clip where "Crear secuencia" asked
            std::cout << "a la maqueta" << std::endl;
            const auto inboxDir = out.getChildFile ("maqueta");
            setenv ("RACOON_MAQUETA_DIR", inboxDir.getFullPathName().toRawUTF8(), 1);
            if (t != nullptr)
            {
                auto writeRequest = [&] (const String& animal, const String& track, const String& kind, const String& stamp)
                {
                    auto dir = Session::maquetaInbox();
                    f.copyFileTo (dir.getChildFile ("Racoon " + animal + " " + stamp + ".mid"));
                    dir.getChildFile (stamp + ".json").replaceWithText ("{\"animal\": \"" + animal + "\", \"track\": \"" + track
                                                                        + "\", \"kind\": \"" + kind + "\", \"midi\": \"Racoon " + animal + " " + stamp + ".mid\"}");
                };
                const int before = t->getClips().size();
                s.expectSequence (*t, 8.0);
                writeRequest ("Dormouse", t->getName(), "notes", "1");
                const auto hint = s.pollMaqueta();
                te::Clip* landed = nullptr;
                for (auto* c : t->getClips()) if (c->getName().startsWith ("Secuencia")) landed = c;
                check (landed != nullptr && t->getClips().size() == before + 1
                           && std::abs (s.getEdit().tempoSequence.toBeats (landed->getPosition().getStart()).inBeats() - 8.0) < 1.0e-6,
                       "la secuencia cae al final de su canal (" + hint + ")");
                check (Session::maquetaInbox().findChildFiles (File::findFiles, false, "*").isEmpty(), "la carpeta de pedidos queda limpia");
                // a second one, without asking: after the last clip, never on top of it
                writeRequest ("Dormouse", t->getName(), "notes", "2");
                s.pollMaqueta();
                double lastStart = -1.0;
                for (auto* c : t->getClips()) lastStart = jmax (lastStart, s.getEdit().tempoSequence.toBeats (c->getPosition().getStart()).inBeats());
                check (t->getClips().size() == before + 2 && lastStart >= 16.0 - 1.0e-6, "la siguiente secuencia va al final del canal, sin encimarse (compás "
                       + String ((int) (lastStart / 4.0) + 1) + ")");
                // double click on a clip: its notes go to the channel's instrument, and come back to the same clip
                if (havePlugins && c != nullptr)
                {
                    std::cout << "doble clic: el clip se abre en su instrumento" << std::endl;
                    auto* p = s.editClipInInstrument (*c);
                    auto reqs = Session::editInbox().findChildFiles (File::findFiles, false, "*.json");
                    const var v = reqs.size() == 1 ? JSON::parse (reqs[0].loadFileAsString()) : var();
                    check (p != nullptr && reqs.size() == 1 && v["clip"].toString() == c->itemID.toString() && v["track"].toString() == t->getName()
                               && std::abs ((double) v["beats"] - 8.0) < 1.0e-6 && v["animal"].toString() == "Dormouse",
                           "el clip le llega a Dormouse con su id, su canal y sus 8 tiempos");
                    {
                        FileInputStream is (Session::editInbox().getChildFile (v["midi"].toString()));
                        MidiFile sent;
                        int n = 0;
                        if (sent.readFrom (is)) for (auto* e : *sent.getTrack (0)) n += e->message.isNoteOn() ? 1 : 0;
                        check (n == 8, "con sus 8 notas");
                    }
                    // the plugin's window takes the request (its "A la maqueta" button watches for it)
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
                        if (auto* inst = ext->getAudioPluginInstance())
                        {
                            std::unique_ptr<AudioProcessorEditor> pe (inst->createEditorIfNeeded());
                            for (int i = 0; i < 30 && ! Session::editInbox().findChildFiles (File::findFiles, false, "*.json").isEmpty(); ++i)
                                MessageManager::getInstance()->runDispatchLoopUntil (100);
                            check (Session::editInbox().findChildFiles (File::findFiles, false, "*").isEmpty(), "la ventana de Dormouse toma el clip");
                            pe.reset();
                        }
                    // "Devolver al clip": the first bar comes back changed; the second stays as it was; no new clip
                    MidiMessageSequence back;
                    for (int k = 0; k < 3; ++k)
                    {
                        back.addEvent (MidiMessage::noteOn (1, 70 + k, (uint8) 90), k * 960.0);
                        back.addEvent (MidiMessage::noteOff (1, 70 + k), k * 960.0 + 400.0);
                    }
                    back.updateMatchedPairs();
                    MidiFile bf;
                    bf.setTicksPerQuarterNote (960);
                    bf.addTrack (back);
                    auto dir = Session::maquetaInbox();
                    { FileOutputStream os (dir.getChildFile ("Racoon Dormouse 9.mid")); bf.writeTo (os); }
                    dir.getChildFile ("9.json").replaceWithText ("{\"animal\": \"Dormouse\", \"track\": \"" + t->getName()
                                                                 + "\", \"kind\": \"notes\", \"clip\": \"" + c->itemID.toString() + "\", \"midi\": \"Racoon Dormouse 9.mid\"}");
                    const int clipsBefore = t->getClips().size();
                    const auto h = s.pollMaqueta();
                    int high = 0, low = 0;
                    for (auto* n : c->getSequence().getNotes())
                        (n->getNoteNumber() >= 70 && n->getStartBeat().inBeats() < 4.0 ? high : n->getStartBeat().inBeats() >= 4.0 ? low : low) += 1;
                    check (t->getClips().size() == clipsBefore && high == 3 && low == 4 && c->getSequence().getNumNotes() == 7,
                           "Devolver al clip cambia ese mismo clip: 3 notas nuevas en el primer compás, el segundo igual (" + h + ")");
                    s.getEdit().getUndoManager().undo();
                    check (c->getSequence().getNumNotes() == 8, "y se deshace");
                }
            }
            if (havePlugins)
                for (auto& mole : Catalog::racoonItems (engine))
                    if (mole.available && Catalog::animalFor (mole.name) == "Mole")
                    if (auto* mt = s.addChannelFor (mole))
                    {
                        const int tracksBefore = te::getAudioTracks (s.getEdit()).size();
                        auto dir = Session::maquetaInbox();
                        f.copyFileTo (dir.getChildFile ("Racoon Mole 3.mid"));
                        dir.getChildFile ("3.json").replaceWithText ("{\"animal\": \"Mole\", \"track\": \"\", \"kind\": \"generator\", \"midi\": \"Racoon Mole 3.mid\"}");
                        const int clipsBefore = mt->getClips().size();
                        const auto h = s.pollMaqueta();
                        check (te::getAudioTracks (s.getEdit()).size() == tracksBefore && mt->getClips().size() == clipsBefore + 1 && ! s.isTrackMuted (*mt),
                               "el ritmo de Mole queda como clip al final de su propio canal (" + h + ")");
                    }
            unsetenv ("RACOON_MAQUETA_DIR");
        }

        // ------------------------------------------------ the whole song from a loop (Crear la rola completa)
        std::cout << "rola completa" << std::endl;
        {
            using namespace racoon::arranger;
            // the plan: the course's shape at 224 bars, every section in whole phrases
            auto p224 = plan (224);
            bool allPhrases = true;
            int sum = 0;
            for (auto& sec : p224) { allPhrases = allPhrases && sec.bars % 8 == 0 && sec.startBar % 8 == 0; sum += sec.bars; }
            check (p224.size() == 7 && sum == 224 && p224[0].bars == 32 && p224[3].name == "Breakdown" && p224[6].name == "Outro",
                   "224 compases: intro, build-up, drop, breakdown, build-up 2, drop 2 y outro de 32 (como el curso)");
            String shape;
            for (int bars : { 96, 136, 256, 320 })
            {
                auto pl = plan (bars);
                int t = 0;
                for (auto& sec : pl) { t += sec.bars; allPhrases = allPhrases && sec.bars % 8 == 0; }
                shape << bars << "=" << t << " ";
                allPhrases = allPhrases && t == bars;
            }
            check (allPhrases, "cualquier duración cae en frases de 8 compases (" + shape.trim() + ")");
            check (barsForMinutes (7.0, 124.0) == 216 && barsForMinutes (1.0, 124.0) == minBars() && barsForMinutes (20.0, 124.0) == maxBars(),
                   "7 minutos a 124 BPM son 216 compases; muy corta o muy larga se ajusta a 96 o 320");
            check (! activeIn (Role::kick, Part::breakdown, 0, 4) && ! activeIn (Role::bass, Part::breakdown, 3, 4)
                     && ! activeIn (Role::bass, Part::intro, 3, 4) && ! activeIn (Role::lead, Part::intro, 3, 4)
                     && activeIn (Role::kick, Part::intro, 0, 4) && activeIn (Role::bass, Part::build, 0, 4) && activeIn (Role::lead, Part::drop, 0, 4)
                     && ! activeIn (Role::bass, Part::outro, 3, 4) && activeIn (Role::kick, Part::outro, 3, 4),
                   "la tabla: breakdown sin bombo ni bajo, intro y outro para DJ (bombo sí, bajo y melodía no)");

            s.newProject (true, false, &defaultGenre());
            auto& ae = s.getEdit();
            Array<te::AudioTrack*> loopTracks;
            for (auto* t : te::getAudioTracks (ae)) if (kindOf (*t) != TrackKind::returnBus) loopTracks.add (t);
            const int notesBefore = countNotes (ae);
            const int tracksBefore = te::getAudioTracks (ae).size();
            Options ao;
            ao.totalBars = 96;
            auto res = build (s, loopTracks, 0.0, 64.0, ao);
            MessageManager::getInstance()->runDispatchLoopUntil (300);
            check (res.ok, "rola de 96 compases desde el bloque de 16: " + res.message);
            check (res.loopBars == 16 && res.harmonicBars == 4, "loop de 16 compases, la armonía se repite cada " + String (res.harmonicBars));
            std::cout << "        " << res.found.joinIntoString (" | ") << std::endl;
            auto& ts = ae.tempoSequence;
            auto beatOf = [&ts] (te::TimePosition t) { return ts.toBeats (t).inBeats(); };
            auto secBeat = [&res] (Part part, bool end = false)
            {
                for (auto& sec : res.sections) if (sec.part == part) return (end ? sec.startBar + sec.bars : sec.startBar) * 4.0;
                return 0.0;
            };
            // notes of one role on one channel inside a range of beats
            auto count = [&] (te::AudioTrack* t, std::function<bool (int)> pitch, double a, double b)
            {
                int n = 0;
                if (t == nullptr) return -1;
                for (auto* c : t->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                        for (auto* nt : mc->getSequence().getNotes())
                        {
                            const double at = nt->getStartBeat().inBeats() + mc->getContentStartBeat().inBeats();
                            if (at >= a - 1.0e-6 && at < b - 1.0e-6 && pitch (nt->getNoteNumber())) ++n;
                        }
                return n;
            };
            auto* kitT = findTrack (ae, "Groove");
            auto* kickT = findTrack (ae, "Kick");
            auto* bassT = findTrack (ae, "Badger");
            auto* leadT = findTrack (ae, "Squirrel");
            auto any = [] (int) { return true; };
            auto kick = [] (int p) { return p == 36 || p == 35; };
            const double bd0 = secBeat (Part::breakdown), bd1 = secBeat (Part::breakdown, true), dr0 = secBeat (Part::drop), dr1 = secBeat (Part::drop, true);
            const double b10 = secBeat (Part::build), b11 = secBeat (Part::build, true);
            check (count (kickT, kick, bd0, bd1) == 0 && count (kickT, kick, dr0, dr1) > 30 && count (kickT, kick, 0.0, 16.0) > 0,
                   "bombo (en su canal Kick): suena desde el compás 1 y en el drop (" + String (count (kickT, kick, dr0, dr1)) + "), nada en el breakdown");
            check (count (bassT, any, 0.0, secBeat (Part::build)) == 0 && count (bassT, any, bd0, bd1) == 0 && count (bassT, any, b10, b11) > 0 && count (bassT, any, dr0, dr1) > 0,
                   "bajo: entra en el build-up, fuera en intro y breakdown");
            check (count (leadT, any, 0.0, dr0) == 0 && count (leadT, any, dr0, dr1) > 0, "la melodía se guarda para el drop");
            check (count (kitT, [] (int p) { return p == 49; }, dr0, dr0 + 0.01) == 1, "platillo en el uno del drop");
            // each kind of percussion has its own moment
            auto* moleT = findTrack (ae, "Mole");
            auto shaker = [] (int p) { return p == 70; };
            auto conga = [] (int p) { return p >= 60 && p <= 64; };
            const double in1 = secBeat (Part::intro, true);
            check (count (moleT, shaker, 0.0, in1) == 0 && count (moleT, shaker, b10, b11) > 0,
                   "shaker: fuera del intro corto, entra en el build-up");
            check (count (moleT, conga, dr0, dr0 + 32.0) == 0 && count (moleT, conga, dr0 + 32.0, dr1) > 0,
                   "congas en el drop 1: callan la primera frase y contestan en la segunda");
            const double d20 = secBeat (Part::drop2), d21 = secBeat (Part::drop2, true);
            check (count (moleT, conga, d20, d20 + 32.0) > 0 && count (moleT, conga, d20 + 32.0, d21) > 0, "en el drop 2 las congas van completas");
            check (count (moleT, shaker, bd0, bd0 + 32.0) == 0 && count (moleT, shaker, bd0 + 32.0, bd1) > 0,
                   "breakdown: el shaker vuelve en su segunda mitad para mantener el pulso");
            auto snareish = [] (int p) { return p == 38 || p == 39 || p == 40; };
            const int midBar = count (kitT, snareish, dr0 + 8.0, dr0 + 12.0), fillBar = count (kitT, snareish, dr1 - 4.0, dr1);
            check (fillBar > midBar + 2, "fill de la batería al final del drop (" + String (fillBar) + " golpes de caja contra "
                                             + String (midBar) + " en un compás normal)");
            int fillsMid = 0;
            for (double b = dr0; b < dr1 - 4.0; b += 4.0)
                if ((int) ((b - dr0) / 4.0) % 8 != 7 && count (kitT, snareish, b, b + 4.0) > midBar + 2) ++fillsMid;
            check (fillsMid == 0, "el fill del loop ya no cae a media frase");
            const int rollFirst = count (kitT, [] (int p) { return p == 38 || p == 39; }, b11 - 32.0, b11 - 28.0);
            const int rollLast = count (kitT, [] (int p) { return p == 38 || p == 39; }, b11 - 4.0, b11);
            check (rollLast > rollFirst * 2, "redoble que se acelera al final del build-up (" + String (rollFirst) + " golpes en el primer compás, "
                                                  + String (rollLast) + " en el último)");
            int inGap = 0;
            for (auto* t : loopTracks) inGap += jmax (0, count (t, any, b11 - 1.0, b11));
            check (inGap == 0, "un tiempo de silencio antes del drop (" + String (inGap) + " notas)");
            int offPhrase = 0, clipsMade = 0;
            for (auto* t : loopTracks)
                for (auto* c : t->getClips())
                {
                    ++clipsMade;
                    const double b = beatOf (c->getPosition().getStart());
                    if (std::abs (b / 32.0 - std::round (b / 32.0)) > 1.0e-6) ++offPhrase;
                }
            check (clipsMade > 10 && offPhrase == 0, "cada clip empieza en el uno de una frase de 8 compases (" + String (clipsMade) + " clips)");
            auto& mm = ae.getMarkerManager();
            StringArray names;
            for (auto& mk : mm.getMarkers()) names.add (mk->getName());
            check (names.joinIntoString (",") == "Intro,Build-up,Drop,Breakdown,Build-up 2,Drop 2,Outro", "marcadores de sección en la regla: " + names.joinIntoString (", "));
            int risers = 0, impacts = 0, downs = 0, reversed = 0;
            if (res.fxTrack != nullptr)
                for (auto* c : res.fxTrack->getClips())
                {
                    if (c->getName() == "Riser")
                    {
                        ++risers;
                        const double endB = beatOf (c->getPosition().getEnd()) + 1.0;
                        check (std::abs (endB / 32.0 - std::round (endB / 32.0)) < 1.0e-3, "  el riser termina un tiempo antes del drop");
                    }
                    if (c->getName() == "Impacto") ++impacts;
                    if (c->getName() == "Bajada") ++downs;
                    if (c->getName() == String::fromUTF8 ("Platillo al revés"))
                    {
                        ++reversed;
                        const double endB = beatOf (c->getPosition().getEnd());
                        check (std::abs (endB / 32.0 - std::round (endB / 32.0)) < 1.0e-3, "  el platillo al revés cae justo en el uno de la sección");
                    }
                }
            check (risers == 2 && impacts == 2 && downs == 2 && reversed == 3, "canal Transiciones: 2 risers, 2 impactos, 2 bajadas, 3 platillos al revés");
            int points = 0;
            if (res.sweepPlugin != nullptr)
                for (auto* prm : res.sweepPlugin->getAutomatableParameters()) if (prm->paramID == "cutoff") points = prm->getCurve().getNumPoints();
            check (res.sweepPlugin != nullptr && ae.getMasterPluginList().indexOf (res.sweepPlugin) == 0 && points > 30,
                   "filtro de graves al inicio del master, con su curva (" + String (points) + " puntos)");

            // what you hear: the lows in the breakdown against the drop
            if (havePlugins)
            {
                ExportOptions so;
                so.destination = out.getChildFile ("rola_completa.wav");
                so.sampleRate = 44100.0;
                so.tailSeconds = 0.5;
                auto rr = s.exportMix (so);
                AudioBuffer<float> mix;
                double sr = 0.0;
                if (rr.wasOk() && audio::readFile (s.engine, so.destination, mix, sr))
                {
                    IIRFilter lp[4];
                    for (auto& f : lp) f.setCoefficients (IIRCoefficients::makeLowPass (sr, 100.0));
                    std::vector<float> low ((size_t) mix.getNumSamples());
                    for (int i = 0; i < mix.getNumSamples(); ++i)
                    {
                        float x = 0.5f * (mix.getSample (0, i) + mix.getSample (jmin (1, mix.getNumChannels() - 1), i));
                        for (auto& f : lp) x = f.processSingleSampleRaw (x);
                        low[(size_t) i] = x;
                    }
                    auto lowDb = [&] (double a, double b)
                    {
                        const int i0 = (int) (ts.toTime (te::BeatPosition::fromBeats (a + 2.0)).inSeconds() * sr);
                        const int i1 = jmin ((int) low.size(), (int) (ts.toTime (te::BeatPosition::fromBeats (b - 1.0)).inSeconds() * sr));
                        double e = 0.0;
                        for (int i = i0; i < i1; ++i) e += (double) low[(size_t) i] * low[(size_t) i];
                        return 10.0 * std::log10 (e / jmax (1, i1 - i0) + 1.0e-20);
                    };
                    const double dDrop = lowDb (dr0, dr1), dBreak = lowDb (bd0, bd1), dIntro = lowDb (0.0, secBeat (Part::intro, true));
                    check (dDrop - dBreak > 24.0, "graves bajo 100 Hz: drop " + String (dDrop, 1) + " dB, breakdown " + String (dBreak, 1)
                                                      + " dB (intro " + String (dIntro, 1) + " dB)");
                    check (std::abs (mix.getNumSamples() / sr - 96 * 4 * 60.0 / s.getBpm()) < 1.5, "dura lo que dice el plan: " + String (mix.getNumSamples() / sr, 1) + " s");
                }
                else check (false, "render de la rola completa " + rr.getErrorMessage());
            }

            // one step back: the 16-bar block again
            for (int k = 0; k < 3 && countNotes (ae) != notesBefore; ++k) { ae.undo(); MessageManager::getInstance()->runDispatchLoopUntil (100); }
            check (countNotes (ae) == notesBefore && mm.getMarkers().isEmpty() && te::getAudioTracks (ae).size() == tracksBefore,
                   "deshacer devuelve el bloque de 16 compases (" + String (countNotes (ae)) + " notas de " + String (notesBefore) + ")");
        }

        // ------------------------------------------------ template
        std::cout << "plantilla Housito" << std::endl;
        s.newProject (true, true);
        auto& e = s.getEdit();
        check (std::abs (s.getBpm() - 124.0) < 0.01, "tempo 124 BPM");
        check (te::getAudioTracks (e).size() == 4, "4 pistas (Hamster, Badger, 2 retornos): " + String (te::getAudioTracks (e).size()));
        auto* drums = findTrack (e, "Hamster");
        auto* bass = findTrack (e, "Badger");
        check (drums != nullptr && bass != nullptr, "pistas de Hamster y Badger");
        if (havePlugins && drums != nullptr && bass != nullptr)
        {
            check (Catalog::isRacoonPlugin (*Session::instrumentOf (*drums), "Racoon Hamster"), "Hamster alojado como VST3");
            check (Catalog::isRacoonPlugin (*Session::instrumentOf (*bass), "Racoon Badger"), "Badger alojado como VST3");
        }
        check (countNotes (e) > 100, "notas del patrón: " + String (countNotes (e)));
        check (s.getSend (*drums, 0, false) != nullptr, "envío de Hamster al retorno de reverb");

        // ------------------------------------------------ export mix
        std::cout << "exportar" << std::endl;
        ExportOptions o;
        o.destination = out.getChildFile ("mezcla.wav");
        o.sampleRate = 48000.0;
        auto r = s.exportMix (o);
        check (r.wasOk(), "render WAV " + r.getErrorMessage());
        auto st = analyse (engine, o.destination);
        check (st.seconds > 15.0, "duración " + String (st.seconds, 2) + " s");
        check (st.rms > 0.01, "mezcla con señal, rms " + String (st.rms, 4) + " pico " + String (st.peak, 3));
        check (st.peak <= 1.0001, "sin clip");

        o.destination = out.getChildFile ("mezcla.flac");
        o.format = 1;
        r = s.exportMix (o);
        auto stf = analyse (engine, o.destination);
        check (r.wasOk() && stf.rms > 0.01, "render FLAC, rms " + String (stf.rms, 4));

        // an effect's knob moved the way the channel view moves it (Tracktion's parameter) changes the export
        if (bass != nullptr)
        {
            CatalogItem dl;
            for (auto& it : Catalog::nativeEffects ({})) if (it.builtInType.contains ("delay") && fx::find (it.builtInType) != nullptr && fx::find (it.builtInType)->id == "delay") dl = it;
            if (auto pl = s.insertPlugin (*bass, dl))
            {
                auto readWav = [&engine] (const File& f)
                {
                    AudioBuffer<float> b;
                    std::unique_ptr<AudioFormatReader> rd (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                    if (rd != nullptr) { b.setSize ((int) rd->numChannels, (int) rd->lengthInSamples); rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true); }
                    return b;
                };
                ExportOptions eo;
                eo.sampleRate = 48000.0;
                eo.destination = out.getChildFile ("perilla_antes.wav");
                s.exportMix (eo);
                const auto a = readWav (eo.destination);
                for (auto* ap : pl->getAutomatableParameters())
                    if (ap->paramID == "mix") ap->setParameter (ap->getValueRange().getEnd(), sendNotification);
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                eo.destination = out.getChildFile ("perilla_despues.wav");
                s.exportMix (eo);
                const auto b = readWav (eo.destination);
                double d = 0, e = 0;
                for (int c = 0; c < jmin (a.getNumChannels(), b.getNumChannels()); ++c)
                    for (int i = 0; i < jmin (a.getNumSamples(), b.getNumSamples()); ++i) { const double x = a.getSample (c, i) - b.getSample (c, i); d += x * x; e += (double) a.getSample (c, i) * a.getSample (c, i); }
                const double db = 10.0 * std::log10 ((d + 1e-20) / (e + 1e-20));
                check (db > -30.0, "mover la mezcla del delay desde la pista cambia lo exportado (" + String (db, 1) + " dB)");
                pl->deleteFromParent();
            }
            else check (false, "insertar el delay de Racoon");
        }

        o.format = 0;
        o.stems = true;
        o.destination = out.getChildFile ("stems");
        Array<File> stems;
        r = s.exportStems (o, &stems);
        check (r.wasOk() && stems.size() >= 2, "stems: " + String (stems.size()) + " " + r.getErrorMessage());
        for (auto& f : stems)
        {
            auto ss = analyse (engine, f);
            check (ss.rms > 0.0005, "  stem " + f.getFileName() + " rms " + String (ss.rms, 4));
        }

        // ------------------------------------------------ family: all 11 in the catalog, Chipmunk in front of the instrument
        {
            std::cout << "familia" << std::endl;
            int found = 0;
            for (auto& it : Catalog::racoonItems (engine)) found += it.available ? 1 : 0;
            std::cout << "  " << found << " de 11 instalados" << std::endl;
            CatalogItem dorm, chip, hedge;
            for (auto& it : Catalog::racoonItems (engine))
            {
                if (it.name.contains ("Dormouse")) dorm = it;
                if (it.name.contains ("Chipmunk")) chip = it;
                if (it.name.contains ("Hedgehog")) hedge = it;
            }
            if (dorm.available && chip.available && hedge.available)
            {
                auto* t = s.addInstrumentTrack (dorm);
                check (t != nullptr && dorm.isInstrument && chip.isInstrument && ! hedge.isInstrument, "Dormouse y Chipmunk son instrumentos; Hedgehog va como efecto");
                if (t != nullptr)
                {
                    auto ph = s.insertPlugin (*t, hedge);
                    auto* inst = Session::instrumentOf (*t);
                    const int ii = t->pluginList.indexOf (inst), ih = t->pluginList.indexOf (ph.get());
                    check (inst != nullptr && ih > ii, "orden en la pista: Hedgehog despues de Dormouse (" + String (ii) + " / " + String (ih) + ")");
                }
                auto* c = s.addInstrumentTrack (chip);
                check (c != nullptr && Session::instrumentOf (*c) != nullptr && Catalog::isRacoonPlugin (*Session::instrumentOf (*c), "Racoon - Chipmunk Chords"),
                       "Chipmunk crea su propio canal");
                if (c != nullptr)
                {
                    if (auto* mc = s.createMidiClip (*c, { 0_bp, te::BeatPosition::fromBeats (8.0) }, "Raices"))
                    {
                        mc->getSequence().addNote (57, 0_bp, te::BeatDuration::fromBeats (4.0), 100, 0, nullptr);
                        mc->getSequence().addNote (53, te::BeatPosition::fromBeats (4.0), te::BeatDuration::fromBeats (4.0), 100, 0, nullptr);
                    }
                    s.setTrackSolo (*c, true);
                    o.destination = out.getChildFile ("chipmunk.wav");
                    check (s.exportMix (o).wasOk() && analyse (engine, o.destination).rms > 0.01, "Chipmunk suena con su propio sonido en Racoon");
                    s.setTrackSolo (*c, false);
                    // mute and solo act on the song, not on the channel: the instrument still plays live
                    {
                        auto tracks = te::getAudioTracks (s.getEdit());
                        te::AudioTrack* other = nullptr;
                        for (auto* t : tracks) if (t != c && Session::mutesByClips (*t) && ! t->getClips().isEmpty()) { other = t; break; }
                        s.setTrackMute (*c, true);
                        bool clipsQuiet = ! c->getClips().isEmpty();
                        for (auto* cl : c->getClips()) clipsQuiet = clipsQuiet && cl->isMuted() && Session::clipMutedByTrack (*cl);
                        check (clipsQuiet && ! c->isMuted (false), "mute en el canal: se callan sus clips, el canal sigue abierto para tocar en vivo");
                        s.setTrackMute (*c, false);
                        bool back = true;
                        for (auto* cl : c->getClips()) back = back && ! cl->isMuted();
                        check (back, "quitar el mute devuelve los clips");
                        if (other != nullptr)
                        {
                            other->getClips().getFirst()->setMuted (true);   // a clip the user silenced
                            s.setTrackSolo (*c, true);
                            bool othersQuiet = true;
                            for (auto* cl : other->getClips()) othersQuiet = othersQuiet && cl->isMuted();
                            check (othersQuiet && ! other->isMuted (false) && ! c->getClips().getFirst()->isMuted(), "solo: se callan los clips de los otros canales, no sus canales");
                            s.setTrackSolo (*c, false);
                            check (other->getClips().getFirst()->isMuted() && (other->getClips().size() < 2 || ! other->getClips()[1]->isMuted()),
                                   "al quitar el solo, el clip que el usuario calló sigue callado y los demás vuelven");
                            other->getClips().getFirst()->setMuted (false);
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ undo
        std::cout << "deshacer" << std::endl;
        const int before = te::getAudioTracks (e).size();
        e.getUndoManager().beginNewTransaction();
        s.addAudioTrack ("Voz");
        check (te::getAudioTracks (e).size() == before + 1, "pista añadida");
        e.undo();
        check (te::getAudioTracks (e).size() == before, "deshacer quita la pista");
        e.redo();
        check (te::getAudioTracks (e).size() == before + 1, "rehacer la devuelve");

        // ------------------------------------------------ clips
        std::cout << "clips" << std::endl;
        auto* bclip = bass->getClips().getFirst();
        const auto len = bclip->getPosition().getLength();
        auto* right = s.splitClip (*bclip, bclip->getPosition().getStart() + len * 0.5);
        check (right != nullptr && bass->getClips().size() == 2, "dividir clip");
        auto* dup = s.duplicateClip (*bass->getClips().getLast());
        check (dup != nullptr && bass->getClips().size() == 3, "duplicar clip");
        if (auto* mc = dynamic_cast<te::MidiClip*> (dup))
        {
            mc->getSequence().getNotes()[0]->setStartAndLength (te::BeatPosition::fromBeats (0.6), 0.25_bd, &e.getUndoManager());
            s.quantise (*mc, 0.25);
            const double b = mc->getSequence().getNotes()[0]->getStartBeat().inBeats();
            check (std::abs (b * 4.0 - std::round (b * 4.0)) < 1e-6, "cuantizar a 1/16: " + String (b, 3));
        }

        // ------------------------------------------------ Arpegiador (efecto MIDI): antes del instrumento, y lo que toca pasa a clip
        if (bass != nullptr)
        {
            std::cout << "arpegiador" << std::endl;
            CatalogItem arpItem;
            for (auto& it : Catalog::nativeEffects ({})) if (it.builtInType.endsWith ("arpegiador")) arpItem = it;
            check (arpItem.name.isNotEmpty(), "el Arpegiador aparece en los efectos de Racoon");
            if (auto arp = s.insertPlugin (*bass, arpItem))
            {
                MessageManager::getInstance()->runDispatchLoopUntil (200);
                const auto chain = Session::chainOf (*bass);
                int arpAt = chain.indexOf (arp.get()), instAt = -1;
                for (int i = 0; i < chain.size(); ++i) if (Session::isInstrument (*chain[i])) { instAt = i; break; }
                check (Session::isMidiEffect (*arp) && arpAt >= 0 && instAt >= 0 && arpAt < instAt,
                       "el Arpegiador se pone antes del instrumento (" + String (arpAt) + " antes de " + String (instAt) + ")");
                int before = 0;
                for (auto* c : bass->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) before += mc->getSequence().getNotes().size();
                auto range = capture::defaultRange (s, *bass);
                auto res = capture::midiToClip (s, *bass, *arp, range, false);
                // what it played: notes on its 1/16 grid (the bass line under it is the held notes)
                int onGrid = 0;
                if (res.clip != nullptr)
                    for (auto* nt : res.clip->getSequence().getNotes())
                    {
                        const double b = nt->getStartBeat().inBeats() * 4.0;
                        onGrid += std::abs (b - std::round (b)) < 0.02 ? 1 : 0;
                    }
                check (res.error.isEmpty() && res.clip != nullptr && res.notes > 16 && onGrid == res.notes,
                       "Pasar a clip lo que toca el Arpegiador: " + String (res.notes) + " notas, " + String (onGrid) + " en la rejilla de 1/16 (el clip tenía "
                       + String (before) + ") " + res.error);
                if (res.clip != nullptr) res.clip->removeFromParent();
                arp->deleteFromParent();
                MessageManager::getInstance()->runDispatchLoopUntil (100);
            }
        }

        // ------------------------------------------------ sidechain
        if (auto comp = s.insertPlugin (*bass, [] { for (auto& it : Catalog::builtInItems()) if (it.builtInType == te::CompressorPlugin::xmlTypeName) return it; return CatalogItem(); }()))
        {
            check (Session::insertsOf (*bass).contains (comp.get()), "compresor insertado");
            check (s.setSidechain (*comp, drums), "sidechain desde la batería");
            check (comp->getSidechainSourceID() == drums->itemID, "fuente de sidechain guardada");
        }

        // ------------------------------------------------ save / load
        std::cout << "guardar y abrir" << std::endl;
        const int notes = countNotes (e);
        const int tracks = te::getAudioTracks (e).size();
        auto proj = out.getChildFile ("Housito prueba.racoon");
        check (s.saveAs (proj), "guardar como");
        check (! s.isUntitled() && ! s.hasUnsavedChanges(), "proyecto con nombre y sin cambios pendientes");
        s.newProject (false);
        check (te::getAudioTracks (s.getEdit()).size() == 1, "proyecto vacío");
        check (s.openProject (proj), "abrir proyecto");
        check (te::getAudioTracks (s.getEdit()).size() == tracks, "mismas pistas tras abrir");
        check (countNotes (s.getEdit()) == notes, "mismas notas tras abrir: " + String (countNotes (s.getEdit())));
        drums = findTrack (s.getEdit(), "Hamster");
        bass = findTrack (s.getEdit(), "Badger");
        if (havePlugins)
            check (drums != nullptr && Catalog::isRacoonPlugin (*Session::instrumentOf (*drums), "Racoon Hamster"), "Hamster restaurado");

        // ------------------------------------------------ freeze / bounce
        std::cout << "congelar y bounce" << std::endl;
        check (s.setFrozen (*bass, true), "congelar Badger");
        o.stems = false;
        o.destination = out.getChildFile ("congelado.wav");
        check (s.exportMix (o).wasOk() && analyse (engine, o.destination).rms > 0.01, "render con pista congelada");
        s.setFrozen (*bass, false);
        auto* bounced = s.bounceToAudio (*bass);
        check (bounced != nullptr && bounced->getClips().size() == 1, "bounce a pista de audio");
        check (s.isTrackMuted (*bass), "original silenciada tras bounce");

        // ------------------------------------------------ Hamster outputs
        if (havePlugins && drums != nullptr)
        {
            std::cout << "salidas separadas de Hamster" << std::endl;
            const int n = s.splitHamsterOutputs (*drums, 4);
            check (n == 4, "4 salidas creadas");
            o.stems = true;
            o.destination = out.getChildFile ("stems_hamster");
            Array<File> hs;
            r = s.exportStems (o, &hs);
            check (r.wasOk(), "stems con salidas " + r.getErrorMessage());
            double outsRms = 0;
            for (auto& f : hs)
            {
                auto ss = analyse (engine, f);
                std::cout << "      " << f.getFileName() << " rms " << String (ss.rms, 4) << std::endl;
                if (f.getFileName().contains ("Hamster ")) outsRms += ss.rms;
            }
            check (outsRms > 0.001, "alguna salida separada lleva señal");
            o.stems = false;
            o.destination = out.getChildFile ("mezcla_salidas.wav");
            check (s.exportMix (o).wasOk() && analyse (engine, o.destination).rms > 0.01, "mezcla con salidas separadas");
        }

        // ------------------------------------------------ groups
        std::cout << "grupos y orden" << std::endl;
        auto audioTracks = te::getAudioTracks (s.getEdit());
        auto* g = s.groupTracks ({ audioTracks[0], audioTracks[1] }, "Ritmo");
        check (g != nullptr && audioTracks[0]->getParentFolderTrack() == g, "agrupar pistas en un bus");
        o.destination = out.getChildFile ("grupo.wav");
        check (s.exportMix (o).wasOk() && analyse (engine, o.destination).rms > 0.01, "mezcla con grupo");

        // ------------------------------------------------ family installer (what Racoon.app does on first launch)
        if (havePlugins)
        {
            std::cout << "instalador de la familia" << std::endl;
            auto fakeApp = out.getChildFile ("Racoon.app");
            auto plugDir = fakeApp.getChildFile ("Contents/Resources/Plugins");
            plugDir.createDirectory();
            for (auto& name : Catalog::racoonProducts())
                if (auto d = Catalog::findRacoon (engine, name))
                    File (d->fileOrIdentifier).copyDirectoryTo (plugDir.getChildFile (name + ".vst3"));
            auto lib = out.getChildFile ("Plug-Ins");
            lib.getChildFile ("VST3/Racoon Badger.vst3/Contents").createDirectory();        // an install with the first, short name
            lib.getChildFile ("VST3/Racoon Hamster Drums.vst3/Contents").createDirectory(); // and one with the name before "Racoon - "
            auto r1 = installBundledFamilyPlugins (fakeApp, lib);
            check (r1.ok && r1.installed.joinIntoString ("|").contains ("Racoon - Badger Bass") && r1.installed.joinIntoString ("|").contains ("Racoon - Hamster Drums"), "instala los plugins de la familia (" + String (r1.installed.size()) + "): " + r1.installed.joinIntoString (", ") + " " + r1.error);
            check (lib.getChildFile ("VST3/Racoon - Badger Bass.vst3").isDirectory() && lib.getChildFile ("VST3/Racoon - Hamster Drums.vst3").isDirectory(), "bundles en VST3 con sus nombres nuevos");
            check (! lib.getChildFile ("VST3/Racoon Badger.vst3").exists(), "el Badger con el nombre viejo se borra (no sale dos veces)");
            check (! lib.getChildFile ("VST3/Racoon Hamster Drums.vst3").exists(), "el Hamster con el nombre anterior se borra");
            auto r2 = installBundledFamilyPlugins (fakeApp, lib);
            check (! r2.attempted, "segunda vez no reinstala lo que ya está al día");
        }

        // ------------------------------------------------ sound library (written on first launch)
        {
            std::cout << "biblioteca de sonidos" << std::endl;
            auto dest = out.getChildFile ("Biblioteca Racoon");
            const auto t0 = Time::getMillisecondCounterHiRes();
            auto res = library::build (dest);
            const auto secs = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const auto wavs = dest.findChildFiles (File::findFiles, true, "*.wav");
            int64 bytes = 0;
            for (auto& f : wavs) bytes += f.getSize();
            check (res.error.isEmpty() && res.files == library::expectedFileCount() && wavs.size() == res.files && res.files >= 300,
                   "biblioteca: " + String (res.files) + " sonidos en " + String (secs, 1) + " s, " + String ((double) bytes / 1048576.0, 1) + " MB " + res.error);
            check (library::isUpToDate (dest), "biblioteca marcada con su version");

            AudioFormatManager fm;
            fm.registerBasicFormats();
            auto loopFile = dest.getChildFile (String::fromUTF8 ("11 Loops de batería/Deep House 122 BPM - Completo A.wav"));
            if (std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (loopFile)); rd != nullptr)
            {
                const double expected = 8.0 * 60.0 / 122.0 * rd->sampleRate;
                check (std::abs ((double) rd->lengthInSamples - expected) <= 1.0 && rd->numChannels == 2,
                       "loop de dos compases exactos a 122 BPM (" + String ((int) rd->lengthInSamples) + " muestras)");
                AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
                rd->read (&b, 0, b.getNumSamples(), 0, true, true);
                const float peak = b.getMagnitude (0, b.getNumSamples());
                check (peak > 0.8f && peak < 0.95f && b.getRMSLevel (0, 0, b.getNumSamples()) > 0.05f, "el loop suena y no satura (pico " + String (peak, 2) + ")");
            }
            else check (false, "se puede leer un loop de la biblioteca");
            int silent = 0;
            for (auto& f : wavs)
                if (std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (f)); rd == nullptr || rd->lengthInSamples < 200) ++silent;
                else
                {
                    AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
                    rd->read (&b, 0, b.getNumSamples(), 0, true, true);
                    const float p = b.getMagnitude (0, b.getNumSamples());
                    if (p < 0.5f || p > 0.99f) { ++silent; std::cout << "    raro: " << f.getFileName() << " pico " << p << std::endl; }
                }
            check (silent == 0, "todos los sonidos se leen, suenan y no saturan (" + String (silent) + " raros)");
        }

        // ------------------------------------------------ effects by genre (written after the library)
        {
            std::cout << "efectos por genero" << std::endl;
            auto dest = out.getChildFile (String::fromUTF8 ("Efectos por género"));
            dest.deleteRecursively();
            const auto t0 = Time::getMillisecondCounterHiRes();
            auto res = library::buildEffects (dest);
            const auto secs = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const auto all = dest.findChildFiles (File::findFiles, true, "*.flac");
            int64 bytes = 0;
            for (auto& f : all) bytes += f.getSize();
            check (res.error.isEmpty() && res.files == library::expectedEffectCount() && all.size() == res.files && res.files >= 400,
                   "efectos: " + String (res.files) + " en " + String (secs, 1) + " s, " + String ((double) bytes / 1048576.0, 1) + " MB " + res.error);
            check (library::effectsUpToDate (dest), "efectos marcados con su version");
            const auto genreDirs = dest.findChildFiles (File::findDirectories, false);
            check (genreDirs.size() == (int) genres().size(), "una carpeta por genero (" + String (genreDirs.size()) + " de " + String ((int) genres().size()) + ")");
            check (dest.getChildFile ("House/Rollers").isDirectory() && dest.getChildFile ("House/Risers").isDirectory()
                   && dest.getChildFile ("Techno/Texturas").isDirectory() && dest.getChildFile ("Amapiano/Fills").isDirectory(),
                   "House trae Rollers y Risers, Techno Texturas, Amapiano Fills");
            AudioFormatManager fm;
            fm.registerBasicFormats();
            int bad = 0;
            for (auto& f : all)
            {
                std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (f));
                if (rd == nullptr || rd->lengthInSamples < 500) { ++bad; std::cout << "    no se lee: " << f.getFileName() << std::endl; continue; }
                AudioBuffer<float> b ((int) rd->numChannels, (int) rd->lengthInSamples);
                rd->read (&b, 0, b.getNumSamples(), 0, true, true);
                const float p = b.getMagnitude (0, b.getNumSamples());
                const float rms = b.getRMSLevel (0, 0, b.getNumSamples());
                if (p < 0.5f || p > 0.99f || rms < 0.005f) { ++bad; std::cout << "    raro: " << f.getRelativePathFrom (dest) << " pico " << p << " rms " << rms << std::endl; }
            }
            check (bad == 0, "todos los efectos se leen, suenan y no saturan (" + String (bad) + " raros)");
            // the rollers last exactly their bars at the genre's tempo (plus the room tail)
            if (std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (dest.getChildFile (String::fromUTF8 ("House/Rollers/Roller de clap 1 compás (124 BPM).flac")))); rd != nullptr)
            {
                const double bar = 240.0 / 124.0;
                const double len = (double) rd->lengthInSamples / rd->sampleRate;
                check (len > bar && len < bar + 1.0, "roller de clap de House: un compas a 124 BPM y su cola (" + String (len, 2) + " s)");
            }
            else check (false, "existe House/Rollers/Roller de clap 1 compas (124 BPM)");
        }

        // ------------------------------------------------ templates
        check (s.saveAsTemplate ("Prueba"), "guardar plantilla");
        check (s.userTemplates().contains ("Prueba"), "plantilla listada");
        Session::templatesFolder().getChildFile ("Prueba.racoon").deleteFile();

        // ------------------------------------------------ the audio goes with the project, and BUSCAR finds what moved
        {
            std::cout << "audios del proyecto" << std::endl;
            s.newProject (false);
            auto writeTone = [] (const File& f, double secs)
            {
                f.getParentDirectory().createDirectory();
                f.deleteFile();
                AudioBuffer<float> b (2, (int) (48000 * secs));
                for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.2f * (float) std::sin (i * 0.05); b.setSample (0, i, v); b.setSample (1, i, v); }
                std::unique_ptr<AudioFormatWriter> w (WavAudioFormat().createWriterFor (f.createOutputStream().release(), 48000.0, 2, 24, {}, 0));
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            };
            // a riser made while the project has no name (as Crear la rola completa does), and a sound from elsewhere
            const auto riser = s.audioFolder().getChildFile ("Transiciones").getChildFile ("Riser prueba.wav");
            writeTone (riser, 1.0);
            const auto other = out.getChildFile ("otros sonidos").getChildFile ("Tom prueba.wav");
            writeTone (other, 0.5);
            auto* at = te::getAudioTracks (s.getEdit()).getFirst();
            s.addAudioFile (*at, riser, 0_tp);
            s.addAudioFile (*at, other, te::TimePosition::fromSeconds (2.0));
            auto dir = out.getChildFile ("Proyecto guardado");
            dir.deleteRecursively();
            dir.createDirectory();
            const auto proj2 = dir.getChildFile ("Mi rola.racoon");
            check (s.saveAs (proj2), "guardar una rola nueva con un riser hecho antes de ponerle nombre");
            s.newProject (false);
            check (s.openProject (proj2) && s.countMissingAudio() == 0, "al abrirla no falta ningún audio (el riser y el sonido de otra carpeta)"_u);
            const auto kept = dir.getChildFile ("Mi rola Audio").getChildFile ("Transiciones").getChildFile ("Riser prueba.wav");
            check (kept.existsAsFile(), "el riser quedó junto al proyecto, en «Mi rola Audio/Transiciones»"_u);
            // the sound moves to another folder: BUSCAR finds it by its name
            const auto movedTo = out.getChildFile ("se movio").getChildFile ("Tom prueba.wav");
            movedTo.getParentDirectory().createDirectory();
            other.moveFileTo (movedTo);
            s.newProject (false);
            s.openProject (proj2);
            const auto wanted = s.missingAudio();
            const auto found = Session::searchFiles (wanted, { out }, {});
            const int fixed = s.relinkFound (found);
            check (wanted.size() == 1 && fixed == 1 && s.countMissingAudio() == 0,
                   "BUSCAR encuentra solo el audio que se movió y lo vuelve a poner en su clip ("_u + String ((int) wanted.size()) + " faltaba)");
            check (s.audioSearchRoots().size() >= 2, "BUSCAR revisa la carpeta del proyecto y las de Racoon ("_u + String (s.audioSearchRoots().size()) + " lugares)");
        }

       #if RACOON_WITH_UI_TESTS
        // RACOON_SIN_UI: the window's tests run in their own process (RACOON_ONLY_UI); after every song of the engine
        // tests, one process holding them all is more than the test machine's memory allows
        if (SystemStats::getEnvironmentVariable ("RACOON_SIN_UI", {}).isEmpty())
        {
            extern int runUiTests (Session&, const File&);
            failures += runUiTests (s, out);
        }
       #endif
    }

    std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
   #if JUCE_LINUX
    // Linux only: JUCE-built VST3s leave run-loop handlers behind when the engine unloads them,
    // which crashes the final message pump. macOS hosts do not use the VST3 IRunLoop.
    std::cout.flush();
    std::_Exit (failures == 0 ? 0 : 1);
   #endif
    return failures == 0 ? 0 : 1;
}

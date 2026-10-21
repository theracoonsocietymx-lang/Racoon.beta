// Badger and Hamster have their own code: this checks their automation and export the same way FamilyTest does
// for the other nine. Built twice (ENGINE_BADGER / ENGINE_HAMSTER), one program per plugin.
#include <JuceHeader.h>
#include "Biblioteca.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Pages.h"
#include "dsp/FxChain.h"
#if ! ENGINE_BADGER
 #include "dsp/Mixer.h"
#endif
#include <iostream>

using namespace juce;

#if ENGINE_BADGER
using Proc = BassEngineProcessor;
using Editor = BassEngineEditor;
static const char* kName = "Badger";
static const char* kParam = "sub_level";
#else
using Proc = DrumEngineProcessor;
using Editor = DrumEngineEditor;
static const char* kName = "Hamster";
static const char* kParam = "g_morph";
#endif

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLA ") << what << std::endl;
    if (! ok) ++failures;
}

// a DAW transport that plays from beat 0 at 120 BPM
struct FakePlayHead : public AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (120.0);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

int main()
{
    ScopedJuceInitialiser_GUI gui;
    std::cout << "automatizacion " << kName << std::endl;
    auto out = File::getCurrentWorkingDirectory().getChildFile ("engine_test_out");
    out.createDirectory();

    Proc p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
#if ! ENGINE_BADGER
    // HAMSTER_KIT="House / Dust Machine": the pitch of every drum of that kit (to see what sings out of key)
    if (auto kit = SystemStats::getEnvironmentVariable ("HAMSTER_KIT", {}); kit.isNotEmpty())
    {
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == kit) p.setCurrentProgram (i);
        for (int t = 0; t < be::kNumTracks; ++t)
            std::cout << t + 1 << " " << p.getTrackName (t) << " rol " << juce::roundToInt (p.trackValue (t, be::T_ROLE))
                      << " nota " << (int) p.trackValue (t, be::T_NOTE) << " tono " << p.measureTrackPitch (t, 40.0, 1500.0, 0.02)
                      << " / bajo " << p.measureTrackPitch (t, 30.0, 200.0, 0.06) << std::endl;
        for (auto& [m, w] : p.measureTrackPeaks (0, 30.0, 400.0, 0.06)) std::cout << "  pico del bombo " << m << " " << w << std::endl;
        {
            // the whole kit playing its kick, as the song hears it
            p.apvts.getParameter ("g_kitkey")->setValueNotifyingHost (4.0f / 12.0f);   // Mib
            auto mf = File::getCurrentWorkingDirectory().getChildFile ("kick_probe.wav");
        }
        return 0;
    }
#endif
    auto& a = p.automation;
    check (a.automatableIds().size() > 50, "parametros automatizables: " + String (a.automatableIds().size()));
    auto* prm = a.parameter (kParam);
    check (prm != nullptr, String ("parametro de prueba ") + kParam);
    if (prm == nullptr) return 1;

    a.setBars (1);
    p.undo.beginNewTransaction();
    a.setPoints (kParam, { { 0.0, 0.1f }, { 3.75, 0.9f } });

    // with the DAW playing
    FakePlayHead ph;
    p.setPlayHead (&ph);
    AudioBuffer<float> blk (p.getTotalNumOutputChannels(), 512);
    float lo = 1.0f, hi = 0.0f, maxErr = 0.0f;
    for (int k = 0; k < 150; ++k)
    {
        blk.clear();
        MidiBuffer m;
        p.processBlock (blk, m);
        const float want = racoon::Automation::valueAt (a.points (kParam), std::fmod (ph.ppq, 4.0), 4.0, false);
        lo = jmin (lo, prm->getValue()); hi = jmax (hi, prm->getValue());
        maxErr = jmax (maxErr, std::abs (want - prm->getValue()));
        ph.ppq += 512.0 / 48000.0 * 2.0;
    }
    check (hi - lo > 0.6f && maxErr < 0.03f, "la linea mueve " + a.nameOf (kParam) + " con el DAW (" + String (lo, 2) + " a " + String (hi, 2) + ", error " + String (maxErr, 3) + ")");

    // with its own Play (no DAW)
    ph.playing = false;
#if ENGINE_BADGER
    p.seqPreview = true;
#else
    if (auto* play = p.apvts.getParameter ("g_play")) play->setValueNotifyingHost (1.0f);
#endif
    lo = 1.0f; hi = 0.0f;
    for (int k = 0; k < 150; ++k)
    {
        blk.clear();
        MidiBuffer m;
        p.processBlock (blk, m);
        lo = jmin (lo, prm->getValue()); hi = jmax (hi, prm->getValue());
    }
    check (hi - lo > 0.6f, "la linea suena con su propio PLAY (" + String (lo, 2) + " a " + String (hi, 2) + ")");
#if ENGINE_BADGER
    p.seqPreview = false;
#else
    if (auto* play = p.apvts.getParameter ("g_play")) play->setValueNotifyingHost (0.0f);
#endif
    p.setPlayHead (nullptr);

    // CC
    const int cc = a.ccFor (kParam);
    {
        blk.clear();
        MidiBuffer m;
        m.addEvent (MidiMessage::controllerEvent (1, cc, 64), 0);
        p.processBlock (blk, m);
    }
    check (cc >= 0 && std::abs (prm->getValue() - 64.0f / 127.0f) < 0.01f, "sigue su MIDI CC " + String (cc));

    // state
    MemoryBlock st;
    p.getStateInformation (st);
    Proc q;
    q.setStateInformation (st.getData(), (int) st.getSize());
    bool samePts = q.automation.points (kParam).size() == a.points (kParam).size();
    for (size_t i = 0; samePts && i < a.points (kParam).size(); ++i)
        samePts = std::abs (q.automation.points (kParam)[i].value - a.points (kParam)[i].value) < 1.0e-5f;
    check (samePts && q.automation.bars() == 1, "la automatizacion se guarda con el proyecto");

    // export
#if ENGINE_BADGER
    auto f = p.exportPatternToMidi (0);
#else
    auto f = p.exportPatternToMidi (0, 1);
#endif
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
    check (notes > 0 && ccs > 4, "el .mid exportado trae " + String (notes) + " notas y " + String (ccs) + " movimientos de CC " + String (cc));

    // undo
    p.undo.beginNewTransaction();
    a.removeLane (kParam);
    const bool gone = ! a.hasLane (kParam);
    p.undo.undo();
    check (gone && a.hasPoints (kParam), "borrar la linea se puede deshacer");

    // "A la maqueta": the pattern with its automation, ready for the song
    {
        auto mm = p.maquetaMidi();
        int n3 = 0, c3 = 0;
        for (int t = 0; t < mm.getNumTracks(); ++t)
            for (auto* e : *mm.getTrack (t)) { n3 += e->message.isNoteOn() ? 1 : 0; c3 += e->message.isController() ? 1 : 0; }
        check (n3 > 0 && c3 > 4, "Pegar en maqueta lleva el patron (" + String (n3) + " notas) y su automatizacion (" + String (c3) + " CC)");
        p.updateTrackProperties ({ String ("Canal prueba"), std::nullopt });
        check (p.trackName() == "Canal prueba", "sabe el nombre de su canal");
    }
#if ! ENGINE_BADGER
    // once its pattern is in the song, Hamster stops playing it under the DAW (the clip plays it)
    {
        auto rmsWith = [&p] (float follow)
        {
            p.apvts.getParameter ("g_follow")->setValueNotifyingHost (follow);
            FakePlayHead h2;
            p.setPlayHead (&h2);
            AudioBuffer<float> b (p.getTotalNumOutputChannels(), 512);
            double sum = 0.0;
            for (int k = 0; k < 600; ++k)        // the first 4 s let any tail from before die out
            {
                b.clear();
                MidiBuffer m;
                p.processBlock (b, m);
                if (k >= 400) for (int i = 0; i < 512; ++i) sum += (double) b.getSample (0, i) * b.getSample (0, i);
                h2.ppq += 512.0 / 48000.0 * 2.0;
            }
            p.setPlayHead (nullptr);
            return std::sqrt (sum / (200.0 * 512.0));
        };
        const double off = rmsWith (0.0f), on = rmsWith (1.0f);
        check (on > 0.005 && off < on * 0.05, "con el patron en la maqueta, Hamster no lo toca doble (" + String (on, 4) + " / " + String (off, 4) + ")");
        p.apvts.getParameter ("g_follow")->setValueNotifyingHost (1.0f);
    }
#endif

#if ! ENGINE_BADGER
    // "Afinar el kit a": the kick lands on the root or the fifth of the key, the toms and hand drums on its scale
    {
        const float before = p.measureTrackPitch (0, 30.0, 400.0, 0.25);
        p.tuneKitToKey (2, false);                               // D minor
        const float after = p.measureTrackPitch (0, 30.0, 400.0, 0.25);
        const int pc = ((int) std::lround (after) % 12 + 12) % 12;
        const float cents = (after - (float) std::lround (after)) * 100.0f;
        check (before > 0.0f && (pc == 2 || pc == 9) && std::abs (cents) < 15.0f,
               "el bombo se afina a Re menor: de " + String (before, 2) + " a " + String (after, 2) + " (nota " + String (pc) + ")");
        int toms = 0, tuned = 0;
        static const int dMinor[] { 2, 4, 5, 7, 9, 10, 0 };
        for (int t = 1; t < be::kNumTracks; ++t)
        {
            const auto role = (be::Role) juce::roundToInt (p.trackValue (t, be::T_ROLE));
            if (role != be::Role::TomLow && role != be::Role::TomMid && role != be::Role::TomHigh && role != be::Role::Conga && role != be::Role::Bongo) continue;
            const float m = p.measureTrackPitch (t, 50.0, 900.0, 0.02);
            if (m < 0.0f) continue;
            ++toms;
            const int tpc = ((int) std::lround (m) % 12 + 12) % 12;
            bool in = false;
            for (int k : dMinor) in |= tpc == k;
            if (in && std::abs (m - (float) std::lround (m)) < 0.2f) ++tuned;
        }
        check (tuned == toms, "toms y congas en la escala: " + String (tuned) + " de " + String (toms));
    }
#endif

#if ENGINE_BADGER
    // Muestras Racoon en Badger: una onda de un ciclo (AKWF), una tabla de 64 cuadros (WaveEdit) y un bajo 808 de verdad
    {
        const auto& cat = racoon::biblio::Catalogo::get();
        if (! cat.disponible()) std::cout << "  (sin Muestras Racoon)" << std::endl;
        else
        {
            const auto& ondas = cat.ondas();
            const racoon::biblio::Onda* uno = nullptr; const racoon::biblio::Onda* tabla = nullptr;
            for (auto& o : ondas) { if (o.cuadros == 1 && uno == nullptr) uno = &o; if (o.cuadros > 1 && tabla == nullptr) tabla = &o; }
            check (ondas.size() > 4000 && uno != nullptr && tabla != nullptr, "ondas en la biblioteca: " + String ((int) ondas.size()));
            if (uno != nullptr)
            {
                p.loadUserWavetable (0, cat.archivo (uno->archivo), uno->ciclo);
                check (p.hasUserTable (0), "Badger toma la onda " + uno->nombre);
            }
            if (tabla != nullptr)
            {
                p.loadUserWavetable (1, cat.archivo (tabla->archivo), 256);
                check (p.hasUserTable (1), "Badger toma la tabla " + tabla->nombre + " de 64 cuadros");
            }
            const racoon::biblio::Muestra* bajo = nullptr;
            for (auto& m : cat.muestras) if (m.etiquetas.contains ("bajo 808")) { bajo = &m; break; }
            if (bajo != nullptr)
            {
                p.loadUserSample (2, cat.archivo (bajo->archivo));
                check (p.hasUserSample (2), "Badger toma el bajo " + bajo->nombre);
            }
            p.clearUserTable (0); p.clearUserTable (1);
        }
    }
#endif

#if ! ENGINE_BADGER
    // Muestras Racoon: un sonido con capas de velocidad en una pista, un kit completo y el proyecto que se guarda con ids
    {
        const auto& cat = racoon::biblio::Catalogo::get();
        if (! cat.disponible()) std::cout << "  (sin Muestras Racoon: se saltan sus pruebas) " << cat.error << std::endl;
        else
        {
            std::cout << "  biblioteca en " << cat.raiz.getFullPathName() << ": " << (int) cat.muestras.size() << " muestras, "
                      << (int) cat.grupos.size() << " grupos, " << (int) cat.kits.size() << " kits" << std::endl;
            const racoon::biblio::Grupo* snare = nullptr;
            for (auto& g : cat.grupos) if (g.cat == "caja" && g.capas >= 3 && g.pack == "Frankensnare") { snare = &g; break; }
            check (snare != nullptr, "hay cajas de Frankensnare con 3 capas de velocidad");
            const int t = 5;
            auto peak = [&p, t] (float vel)
            {
                p.reset();
                AudioBuffer<float> b (p.getTotalNumOutputChannels(), 512);
                for (int k = 0; k < 10; ++k) { b.clear(); MidiBuffer m; p.processBlock (b, m); }
                p.padDown (t, vel);
                float pk = 0.0f;
                for (int k = 0; k < 40; ++k) { b.clear(); MidiBuffer m; p.processBlock (b, m); pk = jmax (pk, b.getMagnitude (0, 512)); }
                return pk;
            };
            if (snare != nullptr)
            {
                const bool ok = p.loadLibraryGroup (t, snare->id);
                check (ok && p.numLoadedSlots (t) == jmin (be::kNumSlots, snare->capas)
                          && juce::roundToInt (p.trackValue (t, be::T_C_MULTI)) == (int) be::MultiMode::Velocity
                          && p.trackValue (t, be::T_A_ON) < 0.5f && p.trackValue (t, be::T_C_ON) > 0.5f,
                       "la pista toma " + snare->nombre + " con sus " + String (p.numLoadedSlots (t)) + " capas en modo velocidad");
                const float soft = peak (0.15f), loud = peak (1.0f);
                check (soft > 0.0005f && loud > soft * 1.6f, "golpe suave y fuerte suenan distinto (" + String (soft, 4) + " / " + String (loud, 4) + ")");
                // el proyecto guarda el id, no el audio, y vuelve con la misma muestra
                MemoryBlock mb;
                p.getStateInformation (mb);
                Proc q;
                q.setRateAndBufferSizeDetails (48000.0, 512);
                q.prepareToPlay (48000.0, 512);
                q.setStateInformation (mb.getData(), (int) mb.getSize());
                const auto* a = p.trackSample (t, 0);
                const auto* b2 = q.trackSample (t, 0);
                check (a != nullptr && b2 != nullptr && a->size() == b2->size() && q.trackLibraryGroup (t) == snare->id,
                       "el proyecto vuelve con la muestra de la biblioteca (" + String (mb.getSize() / 1024) + " KB de estado)");
            }
            // un kit entero de la biblioteca: las 16 pistas suenan con muestras
            const auto* kit = cat.kit ("trap-808");
            check (kit != nullptr && p.loadLibraryKit (kit->id), "carga el kit Trap 808");
            int conMuestra = 0, suenan = 0;
            for (int k = 0; k < be::kNumTracks; ++k)
            {
                if (p.numLoadedSlots (k) > 0) ++conMuestra;
                AudioBuffer<float> b (p.getTotalNumOutputChannels(), 512);
                p.reset();
                p.padDown (k, 0.9f);
                float pk = 0.0f;
                for (int i = 0; i < 30; ++i) { b.clear(); MidiBuffer m; p.processBlock (b, m); pk = jmax (pk, b.getMagnitude (0, 512)); }
                if (pk > 0.001f) ++suenan;
            }
            check (conMuestra == be::kNumTracks && suenan >= be::kNumTracks - 1, "las 16 pistas del kit tienen muestra (" + String (conMuestra) + ") y suenan (" + String (suenan) + ")");
            int fabrica = 0;
            for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i).startsWith ("Muestras / ")) ++fabrica;
            check (fabrica == (int) cat.kits.size(), "los kits de la biblioteca aparecen entre los de fábrica (" + String (fabrica) + ")");
            for (int i = 0; i < p.getNumPrograms(); ++i)
                if (p.getProgramName (i) == "Muestras / Cumbia")
                {
                    p.setCurrentProgram (i);
                    int n = 0;
                    for (int k = 0; k < be::kNumTracks; ++k) n += p.numLoadedSlots (k) > 0 ? 1 : 0;
                    check (n == be::kNumTracks, "el kit de fábrica Muestras / Cumbia carga sus 16 muestras (" + String (n) + ")");
                }
        }
    }
#endif

    // double click on a clip in Racoon: the clip comes into the pattern, "Devolver al clip" sends it back to that clip
    {
        setenv ("RACOON_HOST_IS_RACOON", "1", 1);
        const auto dir = File::getCurrentWorkingDirectory().getChildFile ("engine_test_maqueta");
        dir.deleteRecursively();
        setenv ("RACOON_MAQUETA_DIR", dir.getFullPathName().toRawUTF8(), 1);
        MidiMessageSequence cs;
        Array<int> sent;
       #if ENGINE_BADGER
        // a bass line with one chord: 16 notes over 4 bars
        for (int k = 0; k < 15; ++k)
        {
            const int pitch = 36 + (k * 7) % 14;
            cs.addEvent (MidiMessage::noteOn (1, pitch, (uint8) 100), k * 1.0 * 960.0);
            cs.addEvent (MidiMessage::noteOff (1, pitch), k * 960.0 + 400.0);
            sent.add (pitch);
        }
        cs.addEvent (MidiMessage::noteOn (1, 55, (uint8) 100), 0.0);
        cs.addEvent (MidiMessage::noteOff (1, 55), 400.0);
        sent.add (55);
       #else
        // kick, snare and hats of this kit
        const int kick = (int) p.trackValue (0, be::T_NOTE), snare = (int) p.trackValue (1, be::T_NOTE), hat = (int) p.trackValue (3, be::T_NOTE);
        for (int k = 0; k < 16; ++k)
        {
            const int pitch = k % 4 == 0 ? kick : k % 4 == 2 ? snare : hat;
            cs.addEvent (MidiMessage::noteOn (10, pitch, (uint8) 100), k * 0.5 * 960.0);
            cs.addEvent (MidiMessage::noteOff (10, pitch), k * 0.5 * 960.0 + 100.0);
            sent.add (pitch);
        }
       #endif
        cs.updateMatchedPairs();
        MidiFile cf;
        cf.setTicksPerQuarterNote (960);
        cf.addTrack (cs);
        racoon::maqueta::requestEdit (cf, kName, p.trackName(), "clip-5", "Clip de prueba", 16.0, 98.0);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditorIfNeeded());
        for (int i = 0; i < 30 && ! racoon::maqueta::editInbox().findChildFiles (File::findFiles, false, "*.json").isEmpty(); ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (100);
        std::function<racoon::maqueta::Button* (Component&)> findButton = [&] (Component& c) -> racoon::maqueta::Button*
        {
            if (auto* b = dynamic_cast<racoon::maqueta::Button*> (&c)) return b;
            for (auto* ch : c.getChildren()) if (auto* b = findButton (*ch)) return b;
            return nullptr;
        };
        auto* button = findButton (*ed);
        if (button == nullptr) { check (false, "boton Pegar en maqueta"); return 1; }
        check (p.songClip.clip == "clip-5" && button->getButtonText().containsIgnoreCase ("devolver al clip"),
               "doble clic en un clip: entra al patron y el boton dice " + button->getButtonText());
        auto pitchesOf = [] (const MidiFile& mf, double beats)
        {
            Array<int> a;
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (auto* e : *mf.getTrack (t))
                    if (e->message.isNoteOn() && e->message.getTimeStamp() < beats * mf.getTimeFormat() - 1.0) a.add (e->message.getNoteNumber());
            a.sort();
            return a;
        };
        sent.sort();
        const auto got = pitchesOf (p.maquetaMidi(), 16.0);
        String gs; for (auto v : got) gs << v << " ";
        check (got == sent, "el patron toca las mismas notas del clip (" + String (got.size()) + " de " + String (sent.size()) + ": " + gs + ")");
       #if ENGINE_BADGER
        // while the clip is being edited, the song's notes play the synth (the pattern waits for PLAY)
        check (p.clipEditing.load(), "mientras editas el clip, el patron solo suena con PLAY");
       #endif
        for (auto& j : racoon::maqueta::inbox().findChildFiles (File::findFiles, false, "*")) j.deleteFile();
        button->triggerClick();
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        bool backToClip = false;
        for (auto& j : racoon::maqueta::inbox().findChildFiles (File::findFiles, false, "*.json"))
            backToClip |= JSON::parse (j.loadFileAsString())["clip"].toString() == "clip-5";
        check (backToClip && ! p.songClip.active() && ! button->getButtonText().containsIgnoreCase ("clip"),
               "Devolver al clip lo manda a ese clip y el boton vuelve a " + button->getButtonText());
       #if ENGINE_BADGER
        check (! p.clipEditing.load() && p.apvts.getParameter ("seq_mode")->getValue() < 0.01f, "y Badger apaga su secuenciador: suena el clip");
       #endif
        p.editorBeingDeleted (ed.get());
        ed.reset();
        unsetenv ("RACOON_HOST_IS_RACOON");
    }

    // editor: the strip opens and the window keeps everything visible
    {
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditorIfNeeded());
        ed->setSize (1600, 1000);
        auto* e = dynamic_cast<Editor*> (ed.get());
        e->openAutomation (kParam, false);
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        racoon::AutomationPanel* panel = nullptr;
        for (auto* c : e->getChildren()) if (auto* ap = dynamic_cast<racoon::AutomationPanel*> (c)) panel = ap;
        check (panel != nullptr && panel->isVisible() && panel->getBottom() == e->getHeight(), "Ver automatizacion abre la linea abajo");
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        auto shot = out.getChildFile (String (kName).toLowerCase() + "_automatizacion.png");
        shot.deleteFile();
        FileOutputStream os (shot);
        PNGImageFormat().writeImageToStream (img, os);
    }

    // a picture of every page (to review the look)
    {
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<Editor*> (ed.get());
        for (int pg = 0; pg < Editor::numPages; ++pg)
        {
            e->showPage (pg);
            MessageManager::getInstance()->runDispatchLoopUntil (120);
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            auto shot = out.getChildFile (String (kName).toLowerCase() + "_pagina_" + String (pg) + ".png");
            shot.deleteFile();
            FileOutputStream os (shot);
            PNGImageFormat().writeImageToStream (img, os);
        }
        p.editorBeingDeleted (ed.get());
    }

#if ! ENGINE_BADGER
    // Hamster's grids (INICIO and PATRÓN): a click selects (it does not paint), double click writes, dragging moves,
    // Cmd+Z (and DESHACER) undo it, Cmd+D duplicates
    {
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<Editor*> (ed.get());
        e->showPage (0);
        MessageManager::getInstance()->runDispatchLoopUntil (150);
        be::ui::BeatGrid* grid = nullptr;
        std::function<void (Component*)> find = [&] (Component* c)
        { if (auto* gr = dynamic_cast<be::ui::BeatGrid*> (c)) grid = gr; for (auto* ch : c->getChildren()) if (grid == nullptr) find (ch); };
        find (e);
        check (grid != nullptr, "Hamster: la cuadrícula de INICIO"_u);
        if (grid != nullptr)
        {
            const int pi = juce::jlimit (0, be::kNumPatterns - 1, (int) p.apvts.getRawParameterValue ("g_pattern")->load() - 1);
            auto d = p.getSeqData();
            for (auto& st : d.pats[pi].tracks[3].steps) st = be::Step();
            d.pats[pi].tracks[3].length = 16;
            p.setSeqData (d, false);
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto on = [&] (int t, int s) { return p.getSeqData().pats[pi].tracks[t].steps[s].on; };
            const float cw = ((float) grid->getWidth() - (float) be::ui::BeatGrid::kLabelW) / 16.0f, rh = (float) grid->getHeight() / (float) be::kNumTracks;
            auto at = [&] (int t, int s) { return Point<float> ((float) be::ui::BeatGrid::kLabelW + ((float) s + 0.5f) * cw, ((float) t + 0.5f) * rh); };
            auto src = Desktop::getInstance().getMainMouseSource();
            auto ev = [&] (Point<float> pt, Point<float> down, bool dragged, int clicks = 1)
            { return MouseEvent (src, pt, ModifierKeys (ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, Time::getCurrentTime(), down, Time::getCurrentTime(), clicks, dragged); };
            grid->mouseDown (ev (at (3, 2), at (3, 2), false));
            for (int k = 1; k <= 6; ++k) grid->mouseDrag (ev (at (3, 2) + (at (3, 6) - at (3, 2)) * (k / 6.0f), at (3, 2), true));
            grid->mouseUp (ev (at (3, 6), at (3, 2), true));
            const bool noPaint = ! on (3, 2) && ! on (3, 4) && ! on (3, 6);
            grid->mouseDoubleClick (ev (at (3, 2), at (3, 2), false, 2));
            const bool wrote = on (3, 2);
            grid->mouseDown (ev (at (3, 2), at (3, 2), false));
            for (int k = 1; k <= 6; ++k) grid->mouseDrag (ev (at (3, 2) + (at (3, 5) - at (3, 2)) * (k / 6.0f), at (3, 2), true));
            grid->mouseUp (ev (at (3, 5), at (3, 2), true));
            const bool movedOk = ! on (3, 2) && on (3, 5);
            e->keyPressed (KeyPress ('z', ModifierKeys::commandModifier, 0));
            const bool undone = on (3, 2) && ! on (3, 5);
            e->keyPressed (KeyPress ('z', ModifierKeys (ModifierKeys::commandModifier | ModifierKeys::shiftModifier), 0));
            const bool redone = ! on (3, 2) && on (3, 5);
            grid->mouseDown (ev (at (3, 5), at (3, 5), false)); grid->mouseUp (ev (at (3, 5), at (3, 5), false));
            e->keyPressed (KeyPress ('d', ModifierKeys::commandModifier, 0));
            const bool dup = on (3, 5) && on (3, 6);
            check (noPaint && wrote && movedOk && undone && redone && dup,
                   "Hamster INICIO: clic y arrastrar no pinta ("_u + String ((int) noPaint) + "), doble clic escribe (" + String ((int) wrote) + "), arrastrar mueve ("
                   + String ((int) movedOk) + "), Cmd+Z deshace (" + String ((int) undone) + "), Cmd+Mayús+Z rehace (" + String ((int) redone) + "), Cmd+D duplica (" + String ((int) dup) + ")");
            {
                auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
                auto shot = out.getChildFile ("hamster_seleccion.png");
                shot.deleteFile();
                FileOutputStream os (shot);
                PNGImageFormat().writeImageToStream (img, os);
            }
        }
        p.editorBeingDeleted (ed.get());
    }
#endif

#if ENGINE_BADGER
    // Badger's piano roll works like Racoon's: a click selects (it does not write), a box selects several, dragging
    // moves the selected notes in time and pitch, arrows transpose, Cmd+Shift+arrow copies an octave away, Cmd+D
    // duplicates, double click writes. The pencil (B) keeps the old way: click writes, click on a note erases
    {
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<Editor*> (ed.get());
        e->showPage (4);
        MessageManager::getInstance()->runDispatchLoopUntil (150);
        be::ui::StepGrid* grid = nullptr;
        std::function<void (Component*)> find = [&] (Component* c)
        { if (auto* gr = dynamic_cast<be::ui::StepGrid*> (c)) grid = gr; for (auto* ch : c->getChildren()) if (grid == nullptr) find (ch); };
        find (e);
        check (grid != nullptr && grid->isVisible(), "el piano roll de Badger está en la página de secuencia"_u);
        MemoryBlock saved;
        p.getStateInformation (saved);
        if (grid != nullptr)
        {
            const int pi = (int) p.apvts.getRawParameterValue ("seq_pattern")->load() - 1;
            auto d = p.getSeqData();
            for (auto& st : d.patterns[pi].steps) { st = be::SeqStep(); st.on = false; }
            auto put = [&] (int i, int note) { auto& st = d.patterns[pi].steps[i]; st.on = true; st.note = note; st.octave = 0; st.gate = 0.5f; };
            put (0, 0); put (2, 3); put (4, 5);
            p.setSeqData (d); grid->reload(); grid->centreOnPitch (4);
            auto src = Desktop::getInstance().getMainMouseSource();
            auto ev = [&] (Point<float> pt, Point<float> down, bool dragged, int clicks = 1, ModifierKeys mods = ModifierKeys (ModifierKeys::leftButtonModifier))
            { return MouseEvent (src, pt, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, Time::getCurrentTime(), down, Time::getCurrentTime(), clicks, dragged); };
            auto click = [&] (Point<float> a) { grid->mouseDown (ev (a, a, false)); grid->mouseUp (ev (a, a, false)); };
            auto dragTo = [&] (Point<float> a, Point<float> b)
            { grid->mouseDown (ev (a, a, false)); for (int k = 1; k <= 8; ++k) grid->mouseDrag (ev (a + (b - a) * (k / 8.0f), a, true)); grid->mouseUp (ev (b, a, true)); };
            auto steps = [&] { return p.getSeqData().patterns[pi].steps; };
            auto count = [&]
            {
                auto dd = p.getSeqData(); int n = 0;
                for (int i = 0; i < 16; ++i) { auto& st = dd.patterns[pi].steps[i]; if (st.on && ! st.tie) ++n; if (st.on) n += st.numChord; }
                return n;
            };
            auto pitchAt = [&] (int i) { auto dd = p.getSeqData(); auto& st = dd.patterns[pi].steps[i]; return st.on ? st.note + 12 * st.octave : -999; };
            const int before = count();
            click (grid->posFor (8.3, 2));
            check (count() == before, "Badger: un clic en un hueco ya no escribe una nota (selecciona, como el piano de Racoon)"_u);
            click (grid->posFor (0.3, 0));
            check (count() == before && grid->numSelected() == 1, "Badger: un clic en una nota la selecciona y no la borra"_u);
            dragTo (grid->posFor (0.05, 4), grid->posFor (3.9, -1));
            check (grid->numSelected() == 2, "Badger: arrastrar en un hueco dibuja un recuadro que selecciona 2 notas"_u);
            dragTo (grid->posFor (0.3, 0), grid->posFor (1.3, 2));
            check (pitchAt (0) == -999 && pitchAt (1) == 2 && pitchAt (3) == 5 && pitchAt (4) == 5 && pitchAt (2) == -999,
                   "Badger: arrastrar mueve las 2 seleccionadas un paso después y 2 semitonos arriba; la otra se queda"_u);
            {
                auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
                auto shot = out.getChildFile ("badger_seleccion.png");
                shot.deleteFile();
                FileOutputStream os (shot);
                PNGImageFormat().writeImageToStream (img, os);
            }
            grid->keyPressed (KeyPress (KeyPress::upKey, ModifierKeys::shiftModifier, 0));
            check (pitchAt (1) == 14 && pitchAt (3) == 17 && pitchAt (4) == 5, "Badger: Mayús+flecha arriba sube la selección una octava"_u);
            grid->keyPressed (KeyPress (KeyPress::downKey, ModifierKeys (ModifierKeys::shiftModifier | ModifierKeys::commandModifier), 0));
            {
                auto st = p.getSeqData().patterns[pi].steps;
                check (st[1].numChord == 1 && st[1].chord[0].pitch == 2 && st[3].numChord == 1 && st[3].chord[0].pitch == 5 && grid->numSelected() == 2,
                       "Badger: Cmd+Mayús+flecha abajo pone una copia una octava abajo, encima de las originales"_u);
            }
            grid->keyPressed (KeyPress (KeyPress::upKey, ModifierKeys(), 0));
            {
                auto st = p.getSeqData().patterns[pi].steps;
                check (st[1].numChord == 1 && st[1].chord[0].pitch == 3 && pitchAt (1) == 14, "Badger: flecha arriba sube medio tono solo lo seleccionado"_u);
            }
            grid->keyPressed (KeyPress ('a', ModifierKeys::commandModifier, 0));
            grid->keyPressed (KeyPress (KeyPress::deleteKey));
            check (count() == 0, "Badger: Cmd+A y Supr borran todo el patrón"_u);
            grid->mouseDoubleClick (ev (grid->posFor (6.3, 7), grid->posFor (6.3, 7), false, 2));
            check (pitchAt (6) == 7 && grid->numSelected() == 1, "Badger: doble clic en un hueco escribe una nota"_u);
            grid->keyPressed (KeyPress ('d', ModifierKeys::commandModifier, 0));
            check (pitchAt (7) == 7 && pitchAt (6) == 7, "Badger: Cmd+D duplica la nota justo después"_u);
            grid->mouseDown (ev (grid->posFor (7.3, 7), grid->posFor (7.3, 7), false, 1, ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::altModifier)));
            for (int k = 1; k <= 8; ++k)
                grid->mouseDrag (ev (grid->posFor (7.3 + 2.0 * k / 8.0, 7), grid->posFor (7.3, 7), true, 1, ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::altModifier)));
            grid->mouseUp (ev (grid->posFor (9.3, 7), grid->posFor (7.3, 7), true));
            check (pitchAt (7) == 7 && pitchAt (9) == 7, "Badger: Alt+arrastrar copia la nota"_u);
            grid->keyPressed (KeyPress ('b', ModifierKeys(), 0));
            check (grid->isDrawMode(), "Badger: la tecla B prende el lápiz"_u);
            const int n0 = count();
            click (grid->posFor (12.3, 0));
            check (count() == n0 + 1, "Badger con lápiz: un clic escribe"_u);
            click (grid->posFor (12.3, 0));
            check (count() == n0, "Badger con lápiz: un clic en la nota la borra"_u);
            grid->setDrawMode (false);
            ignoreUnused (steps);
        }
        p.editorBeingDeleted (ed.get());
        ed.reset();
        p.keyboardState.allNotesOff (0);
        p.setStateInformation (saved.getData(), (int) saved.getSize());   // what the next checks expect
    }
#endif

    // Its own Play and its keys keep sounding while the host sends "all notes off" (CC 123): Tracktion sends it by itself
    // with the transport stopped (each cursor move, each mute). Only "all sound off" (CC 120), which Racoon sends when you
    // close the window and answer "Sí, detenerlo", stops the Play.
    {
        auto setPlay = [&p] (bool on)
        {
           #if ENGINE_BADGER
            p.seqPreview = on;
            if (auto* mode = p.apvts.getParameter ("seq_mode")) mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));   // Sequencer
           #else
            p.apvts.getParameter ("g_play")->setValueNotifyingHost (on ? 1.0f : 0.0f);
           #endif
        };
        auto playing = [&p]
        {
           #if ENGINE_BADGER
            return p.seqPreview.load();
           #else
            return p.apvts.getParameter ("g_play")->getValue() > 0.5f;
           #endif
        };
        // blocks of 512 at 48 kHz with what Tracktion adds while stopped: sustain off, hold off, all notes off on channel 1
        auto run = [&p] (double seconds, bool hostNoise, std::function<void (int, MidiBuffer&)> extra = {})
        {
            AudioBuffer<float> buf (2, 512);
            float peak = 0.0f;
            for (int b = 0; b < (int) (seconds * 48000.0 / 512.0); ++b)
            {
                MidiBuffer m;
                if (hostNoise)
                {
                    m.addEvent (MidiMessage::controllerEvent (1, 66, 0), 0);
                    m.addEvent (MidiMessage::controllerEvent (1, 64, 0), 0);
                    m.addEvent (MidiMessage::allNotesOff (1), 0);
                }
                if (extra) extra (b, m);
                buf.clear();
                p.processBlock (buf, m);
                peak = jmax (peak, buf.getMagnitude (0, 512));
                if (b % 8 == 0) MessageManager::getInstance()->runDispatchLoopUntil (1);
            }
            return peak;
        };
        setPlay (true);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        const float quiet = run (2.0, false);
        const float withNoise = run (2.0, true);
        check (playing() && quiet > 0.01f && withNoise > quiet * 0.3f,
               String ("con el transporte parado (CC 123 en cada bloque) su Play sigue sonando: pico ") + String (withNoise, 3) + " (sin ruido " + String (quiet, 3) + ")");
        setPlay (false);
        run (0.5, false);
        // a key pressed right after an "all notes off" sounds
       #if ENGINE_BADGER
        const int note = 48;
       #else
        const int note = roundToInt (p.trackValue (0, be::T_NOTE));       // the kick's note
       #endif
        const float key = run (1.0, true, [note] (int b, MidiBuffer& m)
        {
            if (b == 4) m.addEvent (MidiMessage::noteOn (1, note, 0.9f), 10);
            if (b == 60) m.addEvent (MidiMessage::noteOff (1, note), 10);
        });
        check (key > 0.01f, "una tecla suena aunque el host mande CC 123 (pico " + String (key, 3) + ")");
        // CC 120: the Play stops
        setPlay (true);
        run (0.3, false, [] (int b, MidiBuffer& m) { if (b == 10) m.addEvent (MidiMessage::allSoundOff (1), 0); });
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (! playing(), String ("Racoon pide silencio (CC 120): ") + kName + " apaga su Play");
    }

#if ! ENGINE_BADGER
    // Racoon > Trocear en golpes a Hamster: three slices on tracks 1-3 (notes 36-38), the rest quiet
    {
        std::cout << "golpes troceados en Racoon" << std::endl;
        const double sr = 48000.0;
        StringArray files;
        for (int k = 0; k < 3; ++k)
        {
            AudioBuffer<float> hit (2, (int) (sr * 0.2));
            for (int i = 0; i < hit.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                    hit.setSample (c, i, 0.7f * std::exp (-i / (float) (sr * 0.03)) * std::sin (2.0f * MathConstants<float>::pi * (80.0f + 400.0f * k) * i / (float) sr));
            auto f = out.getChildFile ("golpe " + String (k + 1) + ".wav");
            f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), sr, 2, 24, {}, 0));
            if (w != nullptr) w->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
            w.reset();
            files.add (f.getFullPathName());
        }
        p.loadSliceKit (files, { "Golpe 1", "Golpe 2", "Golpe 3" }, { "kick", "snare", "hat" }, 36);
        bool loaded = true;
        for (int t = 0; t < 3; ++t)
            loaded = loaded && p.trackSample (t, 0) != nullptr && roundToInt (p.trackValue (t, be::T_NOTE)) == 36 + t
                     && p.trackValue (t, be::T_C_ON) > 0.5f && p.trackValue (t, be::T_A_ON) < 0.5f;
        check (loaded && p.trackValue (3, be::T_C_ON) < 0.5f && p.trackSample (3, 0) == nullptr, "3 golpes en las pistas 1 a 3 (notas 36 a 38) y las demás calladas");
        // note 37 plays the second slice, and only it
        AudioBuffer<float> buf (2, 512);
        float peak = 0.0f;
        const int before = p.engine.telemetry.trackHits[1].load(), otherBefore = p.engine.telemetry.trackHits[5].load();
        for (int b = 0; b < 30; ++b)
        {
            MidiBuffer m;
            if (b == 1) m.addEvent (MidiMessage::noteOn (10, 37, 0.8f), 0);
            buf.clear();
            p.processBlock (buf, m);
            peak = jmax (peak, buf.getMagnitude (0, 0, 512));
        }
        check (p.engine.telemetry.trackHits[1].load() == before + 1 && p.engine.telemetry.trackHits[5].load() == otherBefore && peak > 0.02f,
               "la nota 37 toca el golpe 2 y suena (pico " + String (peak, 3) + ")");
        // the request Racoon leaves in Maqueta/Kits: the timer takes it
        auto inbox = out.getChildFile ("maqueta kits");
        inbox.deleteRecursively();
        setenv ("RACOON_MAQUETA_DIR", inbox.getFullPathName().toRawUTF8(), 1);
        auto kits = racoon::maqueta::inbox().getChildFile ("Kits");
        kits.createDirectory();
        DynamicObject::Ptr o (new DynamicObject());
        o->setProperty ("animal", "Hamster");
        o->setProperty ("track", p.trackName());
        Array<var> fl;
        fl.add (files[2]);
        o->setProperty ("files", fl);
        o->setProperty ("firstNote", 40);
        kits.getChildFile ("1.json").replaceWithText (JSON::toString (var (o.get())));
        // the timer looks twice a second; give it up to ten seconds (a busy machine delays the timer thread)
        for (int i = 0; i < 100 && kits.getChildFile ("1.json").existsAsFile(); ++i) MessageManager::getInstance()->runDispatchLoopUntil (100);
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (roundToInt (p.trackValue (0, be::T_NOTE)) == 40 && p.trackSample (0, 0) != nullptr && p.trackSample (1, 0) == nullptr
               && ! kits.getChildFile ("1.json").existsAsFile(), "Hamster toma solo el kit que Racoon le dejó en Maqueta/Kits (nota "_u
               + String (p.trackValue (0, be::T_NOTE)) + ", muestra " + String ((int) (p.trackSample (0, 0) != nullptr)) + "/" + String ((int) (p.trackSample (1, 0) != nullptr))
               + ", json " + String ((int) kits.getChildFile ("1.json").existsAsFile()) + ", canal '" + p.trackName() + "')");
        unsetenv ("RACOON_MAQUETA_DIR");
    }
#endif



    // ---------------------------------------------------------------- tanda 2
    {
        // spectrum helpers: the power that is not the tone or its harmonics, relative to all of it
        auto aliasDb = [] (const std::vector<float>& x, double f0, double sr)
        {
            const int order = 14, n = 1 << order;
            dsp::FFT fft (order);
            std::vector<float> d ((size_t) n * 2, 0.0f);
            const int start = (int) x.size() - n;
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
        };
        const double sr = 48000.0, f0 = 4987.0;
        auto tone = [&] (int n) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = 0.5f * (float) std::sin (MathConstants<double>::twoPi * f0 * i / sr); return v; };
        // the rack's clipper runs at the base rate: ADAA is what keeps it clean
        {
            be::FxRack rack;
            rack.prepare (sr, 512);
            be::FxSettings fs;
            fs.on[(int) be::FxId::Clipper] = true; fs.clipDrive = 18.0f; fs.clipCeiling = -1.0f; fs.clipKnee = 0.0f;
            auto L = tone (48000), R = L;
            for (int pos = 0; pos < 48000; pos += 512)
                rack.process (fs, L.data() + pos, R.data() + pos, std::min (512, 48000 - pos), 120.0, 0.0, false, 0.0f, nullptr);
            auto naive = tone (48000);
            const float ceil = std::pow (10.0f, -1.0f / 20.0f), drv = std::pow (10.0f, 18.0f / 20.0f);
            for (auto& v : naive) v = std::clamp (v * drv / ceil, -1.0f, 1.0f) * ceil;
            float pk = 0.0f;
            for (int i = 1000; i < 48000; ++i) pk = std::max (pk, std::abs (L[(size_t) i]));
            const double a = aliasDb (L, f0, sr), an = aliasDb (naive, f0, sr);
            check (a < an - 4.0 && pk <= ceil + 1.0e-4f, "clipper del rack con ADAA: aliasing " + String (a, 1) + " dB (sin ADAA " + String (an, 1) + "), el techo se respeta");
        }
        // the distortion stage: soft clip at 1x, with ADAA, against the same curve without it
        {
            be::DistortionStage dist;
            dist.prepare (sr, 512);
            be::DistSettings ds; ds.on = true; ds.type = 0; ds.drive = 0.8f; ds.mix = 1.0f;
            auto L = tone (48000), R = L;
            for (int pos = 0; pos < 48000; pos += 512) dist.process (ds, L.data() + pos, R.data() + pos, std::min (512, 48000 - pos), 0);
            auto naive = tone (48000);
            const float g = std::pow (10.0f, 0.8f * 36.0f / 20.0f);
            for (auto& v : naive) v = std::tanh (v * g);
            const double a = aliasDb (L, f0, sr), an = aliasDb (naive, f0, sr);
            check (a < an - 6.0, "distorsión suave sin sobremuestreo, con ADAA: aliasing " + String (a, 1) + " dB (sin ADAA " + String (an, 1) + ")");
        }
        // the new reverb (8-line FDN) against the Freeverb it replaces: about as loud with the same knobs, a decay that
        // grows with the size, and freeze holds the tail
        {
            Random rng (11);
            double prevT60 = 0.0;
            bool levelsOk = true, decayOk = true, finite = true;
            String detail;
            for (float room : { 0.3f, 0.6f, 0.9f })
            {
                juce::Reverb jr; jr.setSampleRate (sr);
                juce::Reverb::Parameters jp; jp.roomSize = room; jp.damping = 0.5f; jp.wetLevel = 0.33f; jp.dryLevel = 0.0f; jp.width = 1.0f;
                jr.setParameters (jp);
                racoon::sdsp::Verb rv; rv.setSampleRate (sr);
                racoon::sdsp::Verb::Parameters rp; rp.roomSize = room; rp.damping = 0.5f; rp.wetLevel = 0.33f; rp.dryLevel = 0.0f; rp.width = 1.0f;
                rv.setParameters (rp);
                const int n = (int) sr * 8;
                std::vector<float> a ((size_t) n), b ((size_t) n), c ((size_t) n), d ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const bool on = i < (int) sr * 2;
                    a[(size_t) i] = c[(size_t) i] = on ? rng.nextFloat() * 0.6f - 0.3f : 0.0f;
                    b[(size_t) i] = d[(size_t) i] = on ? rng.nextFloat() * 0.6f - 0.3f : 0.0f;
                }
                jr.processStereo (a.data(), b.data(), n);
                rv.processStereo (c.data(), d.data(), n);
                auto rmsDb = [] (const std::vector<float>& x, int s0, int s1) { double q = 0; for (int i = s0; i < s1; ++i) q += (double) x[(size_t) i] * x[(size_t) i]; return 10.0 * std::log10 (q / (s1 - s0) + 1e-30); };
                const double lj = rmsDb (a, (int) (sr * 1.5), (int) (sr * 2)), lr = rmsDb (c, (int) (sr * 1.5), (int) (sr * 2));
                // decay: time until 60 dB below the steady level, in 10 ms steps
                double t60 = 99.0;
                for (int k = 0; k < 590; ++k) { const int s0 = (int) (sr * (2.0 + k * 0.01)); if (rmsDb (c, s0, s0 + 480) < lr - 60.0) { t60 = k * 0.01; break; } }
                for (auto v : c) if (! std::isfinite (v)) finite = false;
                levelsOk = levelsOk && std::abs (lj - lr) < 2.5;
                decayOk = decayOk && t60 > prevT60;
                prevT60 = t60;
                detail << "tamaño " << String (room, 1) << ": " << String (lr - lj, 1) << " dB, " << String (t60, 2) << " s; ";
            }
            check (finite && levelsOk && decayOk, "reverb nueva contra Freeverb con las mismas perillas: " + detail);
            // freeze: after 3 s the tail is still there
            racoon::sdsp::Verb rv; rv.setSampleRate (sr);
            racoon::sdsp::Verb::Parameters rp; rp.wetLevel = 0.33f; rp.dryLevel = 0.0f;
            rv.setParameters (rp);
            std::vector<float> a ((size_t) sr), b ((size_t) sr);
            for (size_t i = 0; i < a.size(); ++i) { a[i] = rng.nextFloat() - 0.5f; b[i] = rng.nextFloat() - 0.5f; }
            rv.processStereo (a.data(), b.data(), (int) a.size());
            double first = 0, after = 0;
            for (auto v : a) first += (double) v * v;
            rp.freezeMode = 1.0f; rv.setParameters (rp);
            for (int s2 = 0; s2 < 3; ++s2)
            {
                std::fill (a.begin(), a.end(), 0.0f); std::fill (b.begin(), b.end(), 0.0f);
                rv.processStereo (a.data(), b.data(), (int) a.size());
            }
            for (auto v : a) after += (double) v * v;
            check (10.0 * std::log10 (after / first) > -6.0, "congelar sostiene la cola: " + String (10.0 * std::log10 (after / first), 1) + " dB después de 3 s");
        }
        // the voice filter glides between control steps: a resonant low pass swept fast does not move in 16-sample stairs.
        // The same sweep with the glide off (the old way) is the reference: the stairs show up as clicks up high.
        {
            auto run = [&] (int ramp)
            {
                be::VoiceFilter vf;
                vf.prepare (sr, 4096);
                std::vector<float> out;
                double ph = 0.0;
                for (int step = 0; step < 900; ++step)
                {
                    const float cut = 150.0f * std::pow (2.0f, 3.0f * (0.5f + 0.5f * (float) std::sin (step * 0.05)));   // 150 Hz to 1.2 kHz
                    vf.set ((int) be::FilterType::LP12, cut, 0.85f, 0.0f, 0.5f, 0.0f, false, ramp);
                    for (int i = 0; i < 16; ++i)
                    {
                        float l = 0.2f * (float) std::sin (ph), r = l;
                        ph += MathConstants<double>::twoPi * 700.0 / sr;
                        vf.process (l, r);
                        out.push_back (l);
                    }
                }
                // the stairs make sidebands at 700 Hz plus 3 kHz (48 kHz / 16) and up: the power above 2.5 kHz shows them
                const int order = 13, n = 1 << order;
                dsp::FFT fft (order);
                std::vector<float> d ((size_t) n * 2, 0.0f);
                const size_t start = out.size() - (size_t) n;
                for (int i = 0; i < n; ++i) d[(size_t) i] = out[start + (size_t) i] * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (n - 1)));
                fft.performFrequencyOnlyForwardTransform (d.data());
                double hi = 0.0, all = 0.0;
                for (int k = 1; k < n / 2; ++k)
                {
                    const double hz = k * sr / n, pw = (double) d[(size_t) k] * d[(size_t) k];
                    all += pw;
                    const double nearest = std::round (hz / 700.0) * 700.0;   // the input stage's soft saturation makes 700 Hz harmonics: not stairs
                    if (hz > 2500.0 && std::abs (hz - nearest) > 60.0) hi += pw;
                }
                return 10.0 * std::log10 (hi / all + 1.0e-30);
            };
            const double glide = run (16), stairs = run (1);
            check (glide < stairs - 10.0, "filtro de voz barrido con resonancia: la rampa quita los escalones (ruido arriba de 2.5 kHz "
                   + String (glide, 1) + " dB contra " + String (stairs, 1) + " dB con escalones)");
        }
    }
    // the delay of the effects rack (the same code in Badger and Hamster): a new time glides, it does not jump and click
    {
        be::FxRack rack;
        rack.prepare (48000.0, 512);
        be::FxSettings fs;
        fs.on[(int) be::FxId::Delay] = true;
        fs.dlSync = false; fs.dlTime = 200.0f; fs.dlMix = 1.0f; fs.dlFb = 0.0f; fs.dlPing = false; fs.dlHighCut = 20000.0f; fs.dlLowCut = 20.0f;
        std::vector<float> L (512), R (512);
        double ph = 0.0;
        float worstBefore = 0.0f, worstAfter = 0.0f, last = 0.0f;
        for (int blk = 0; blk < 200; ++blk)
        {
            if (blk == 100) fs.dlTime = 330.0f;
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = R[(size_t) i] = 0.4f * (float) std::sin (ph); ph += MathConstants<double>::twoPi * 220.0 / 48000.0; }
            rack.process (fs, L.data(), R.data(), 512, 120.0, 0.0, false, 0.0f, nullptr);
            for (int i = 0; i < 512; ++i)
            {
                const float j = std::abs (L[(size_t) i] - last);
                last = L[(size_t) i];
                if (blk >= 20 && blk < 100) worstBefore = jmax (worstBefore, j);
                if (blk >= 100) worstAfter = jmax (worstAfter, j);
            }
        }
        check (worstAfter < worstBefore * 1.6f, "delay del rack: al cambiar de 200 a 330 ms no hay salto (paso máximo "_u
               + String (worstAfter, 3) + ", antes " + String (worstBefore, 3) + ")");
    }
#if ! ENGINE_BADGER
    // Hamster's track fader moves smoothly: no step when it drops from 0 to -30 dB in the middle of a wave
    {
        be::TrackStrip strip;
        strip.prepare (48000.0, 512);
        be::StripSettings ss;
        std::vector<float> L (512), R (512);
        double ph = 0.0;
        float worst = 0.0f, last = 0.0f;
        for (int blk = 0; blk < 40; ++blk)
        {
            const int n = blk == 10 ? 240 : 512;   // the change lands right on the top of the 50 Hz wave
            if (blk == 11) ss.faderDb = -30.0f;
            for (int i = 0; i < n; ++i) { L[(size_t) i] = R[(size_t) i] = 0.5f * (float) std::sin (ph); ph += MathConstants<double>::twoPi * 50.0 / 48000.0; }
            strip.process (ss, L.data(), R.data(), n, 0);
            for (int i = 0; i < n; ++i) { if (blk > 2) worst = jmax (worst, std::abs (L[(size_t) i] - last)); last = L[(size_t) i]; }
        }
        check (worst < 0.02f, "fader de pista de Hamster: de 0 a -30 dB sin escalón (paso máximo " + String (worst, 4) + ")");
    }
#endif
    std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}

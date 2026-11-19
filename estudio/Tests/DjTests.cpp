// Modo DJ: synthetic songs (a kick, hats, a bass and chords at a known tempo and key) measured, a set ordered, and the
// track built to mix with one of them.
#include "engine/Session.h"
#include "engine/DJ.h"
#include "engine/Genres.h"
#include "engine/AudioTools.h"
#include "engine/Gusto.h"
#include "engine/Biblioteca.h"

using namespace racoon;
using namespace juce;

namespace
{
int djFailures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++djFailures;
}

// a song: "estilo" 0 four to the floor, 1 hip hop (kick 1 and the "and" of 2, snare 2 and 4), 2 breaks (drum & bass);
// the chords of the key's i-VI-VII-i (or I-V-vi-IV), a bass on the roots, a silent first beat if "offset"
AudioBuffer<float> cancion (double bpm, int root, bool major, int estilo, double segundos, double sr, uint32 seed, double offset = 0.0,
                            float nivel = 0.5f)
{
    Random rnd ((int64) seed);
    AudioBuffer<float> out (2, (int) (segundos * sr));
    out.clear();
    const double spb = 60.0 / bpm;
    auto add = [&] (int at, float v) { if (at >= 0 && at < out.getNumSamples()) { out.addSample (0, at, v); out.addSample (1, at, v); } };
    auto kick = [&] (double t, float g)
    {
        const int a = (int) (t * sr);
        double ph = 0.0;
        for (int i = 0; i < (int) (0.25 * sr); ++i)
        {
            const double tt = i / sr;
            ph += (45.0 + 110.0 * std::exp (-tt / 0.03)) / sr;
            add (a + i, (float) (g * std::sin (MathConstants<double>::twoPi * ph) * std::exp (-tt / 0.12)));
        }
    };
    auto noise = [&] (double t, float g, double decay, bool high)
    {
        const int a = (int) (t * sr);
        double prev = 0.0, lp = 0.0;
        for (int i = 0; i < (int) (decay * 6.0 * sr); ++i)
        {
            const double n = rnd.nextDouble() * 2.0 - 1.0;
            double s;
            if (high) { s = n - prev; prev = n; }
            else { lp += (n - lp) * 0.3; s = lp; }
            add (a + i, (float) (g * s * std::exp (-(i / sr) / decay)));
        }
    };
    static const int minS[] { 0, 2, 3, 5, 7, 8, 10 }, majS[] { 0, 2, 4, 5, 7, 9, 11 };
    const int* sc = major ? majS : minS;
    const int progMin[] { 0, 5, 6, 0 }, progMaj[] { 0, 4, 5, 3 };
    const int* prog = major ? progMaj : progMin;
    const int beats = (int) ((segundos - offset) / spb);
    for (int b = 0; b < beats; ++b)
    {
        const double t = offset + b * spb;
        const int inBar = b % 4;
        if (estilo == 0) { kick (t, 0.9f); noise (t + spb * 0.5, 0.25f, 0.03, true); if (inBar % 2 == 1) noise (t, 0.35f, 0.06, false); }
        else if (estilo == 1)
        {
            if (inBar == 0) kick (t, 0.9f);
            if (inBar == 1) kick (t + spb * 0.5, 0.8f);
            if (inBar % 2 == 1) noise (t, 0.5f, 0.07, false);
            noise (t, 0.18f, 0.02, true); noise (t + spb * 0.5, 0.14f, 0.02, true);
        }
        else
        {
            if (inBar == 0) kick (t, 0.9f);
            if (inBar == 2) kick (t + spb * 0.5, 0.8f);
            if (inBar % 2 == 1) noise (t, 0.55f, 0.06, false);
            for (int k = 0; k < 2; ++k) noise (t + k * spb * 0.5, 0.15f, 0.015, true);
        }
    }
    // harmony: a chord a bar (three saws, soft) and the bass on its root
    const int bars = beats / 4;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int deg = prog[bar % 4];
        const double t0 = offset + bar * 4 * spb;
        const int a = (int) (t0 * sr), len = (int) (4 * spb * sr);
        int notes[3];
        for (int k = 0; k < 3; ++k) notes[k] = 60 + root + sc[(deg + 2 * k) % 7] + ((deg + 2 * k) >= 7 ? 12 : 0);
        const int bassN = 36 + root + sc[deg];
        double ph[4] {};
        for (int i = 0; i < len; ++i)
        {
            const double env = jmin (1.0, i / (0.02 * sr)) * jmin (1.0, (len - i) / (0.05 * sr));
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                ph[k] += 440.0 * std::pow (2.0, (notes[k] - 69) / 12.0) / sr;
                ph[k] -= std::floor (ph[k]);
                s += (float) (0.06 * (2.0 * ph[k] - 1.0));
            }
            ph[3] += 440.0 * std::pow (2.0, (bassN - 69) / 12.0) / sr;
            s += (float) (0.25 * std::sin (MathConstants<double>::twoPi * ph[3]));
            add (a + i, (float) (s * env));
        }
    }
    out.applyGain (nivel / jmax (1.0e-6f, out.getMagnitude (0, out.getNumSamples())));
    return out;
}

dj::Analisis falsa (const char* t, double bpm, int key, bool major, double e)
{
    dj::Analisis a;
    a.titulo = t; a.bpm = bpm; a.key = key; a.major = major; a.energia = e; a.ok = true;
    return a;
}
}

// for the window's test: a minute of house at a tempo and key
AudioBuffer<float> cancionDePrueba (double bpm, int root, bool major, double sr) { return cancion (bpm, root, major, 0, 40.0, sr, 99); }

int runDjTests (Session& s, const File& out)
{
    std::cout << "modo DJ" << std::endl;
    // RACOON_VARIEDAD=house: four blocks of a genre with four seeds, and what each instrument really ended up with
    if (auto id = SystemStats::getEnvironmentVariable ("RACOON_VARIEDAD", {}); id.isNotEmpty())
    {
        for (int k = 1; k <= 4; ++k)
        {
            setenv ("RACOON_SEMILLA_SONIDOS", String (k * 7919).toRawUTF8(), 1);
            s.newProject (true, false, findGenre (id));
            MessageManager::getInstance()->runDispatchLoopUntil (1500);
            std::cout << "== semilla " << k << std::endl << s.getEdit().state.getProperty ("racoonSounds").toString() << std::endl;
            for (auto* t : te::getAudioTracks (s.getEdit()))
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t) != nullptr ? Session::instrumentOf (*t) : (t->pluginList.size() > 0 ? t->pluginList[0] : nullptr)))
                    if (auto* pi = ext->getAudioPluginInstance())
                    {
                        String line = t->getName() + ": programa " + pi->getProgramName (pi->getCurrentProgram());
                        for (auto* prm : pi->getParameters())
                            for (auto* nm : { "Sonido", "Osc 1 forma", "Osc 1 tabla", "Guitarra", "Cantar: voz", "Biblioteca", "Wave", "Osc A Wave" })
                                if (prm->getName (64) == String::fromUTF8 (nm)) line << " | " << nm << "=" << prm->getCurrentValueAsText();
                        std::cout << "   " << line << std::endl;
                    }
            MessageManager::getInstance()->runDispatchLoopUntil (3000);
        }
        return 0;
    }
    // RACOON_DJ_GENEROS="house,techno": the block of each genre written as "bloque <id> <key>.wav" (to tune the key finder)
    if (auto ids = SystemStats::getEnvironmentVariable ("RACOON_DJ_GENEROS", {}); ids.isNotEmpty())
    {
        for (auto& id : StringArray::fromTokens (ids, ",", ""))
            if (auto* g = findGenre (id))
            {
                s.newProject (true, false, g);
                ExportOptions o;
                o.destination = out.getChildFile ("bloque " + id + " " + String (g->key) + (g->major ? "M" : "m") + ".wav");
                o.sampleRate = 44100.0; o.loopOnly = true; o.tailSeconds = 0.0;
                s.exportMix (o);
                std::cout << "  " << o.destination.getFileName() << std::endl;
            }
        return 0;
    }
    const double sr = 44100.0;
    struct Caso { const char* nombre; double bpm; int root; bool major; int estilo; double offset; };
    const Caso casos[] {
        { "house a 126 BPM en Fa menor", 126.0, 5, false, 0, 0.0 },
        { "techno a 132 BPM en La menor, entra a los 0.3 s", 132.0, 9, false, 0, 0.3 },
        { "hip hop a 92 BPM en Do menor", 92.0, 0, false, 1, 0.0 },
        { "drum & bass a 174 BPM en Re menor", 174.0, 2, false, 2, 0.0 },
        { "nu disco a 118.5 BPM en Sol mayor", 118.5, 7, true, 0, 0.12 },
    };
    std::vector<dj::Analisis> medidas;
    uint32 seed = 70;
    for (auto& c : casos)
    {
        const auto audio = cancion (c.bpm, c.root, c.major, c.estilo, 60.0, sr, ++seed, c.offset);
        if (SystemStats::getEnvironmentVariable ("RACOON_DJ_DEBUG", {}).isNotEmpty())
            audio::writeWav (out.getChildFile (String ("cancion ") + String (c.root) + (c.major ? "M" : "m") + " " + String (c.bpm) + ".wav"), audio, sr, 16);
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto a = dj::analizarAudio (audio, sr);
        a.titulo = c.nombre;
        const double ms = Time::getMillisecondCounterHiRes() - t0;
        std::cout << "    " << c.nombre << ": " << a.resumen() << ", confianza " << String (a.confianza, 2) << ", primer tiempo "
                  << String (a.primerTiempo, 3) << " s (" << String (ms / 1000.0, 1) << " s de análisis)" << std::endl;
        check (std::abs (a.bpm - c.bpm) <= 0.05, String (c.nombre) + ": tempo " + String (a.bpm, 2) + " BPM");
        check (a.key == c.root && a.major == c.major, String (c.nombre) + ": tonalidad " + keyName (a.key, a.major) + " (" + a.camelot() + ")");
        const double beat = 60.0 / c.bpm;
        double ph = std::fmod (a.primerTiempo - c.offset + beat * 4, beat);
        if (ph > beat * 0.5) ph -= beat;
        check (std::abs (ph) <= 0.02, String (c.nombre) + ": el primer tiempo cae a " + String (ph * 1000.0, 1) + " ms de la rejilla");
        medidas.push_back (a);
    }
    {
        // the same song quieter and with only kicks is calmer
        const auto fuerte = dj::analizarAudio (cancion (124.0, 9, false, 0, 30.0, sr, 90, 0.0, 0.9f), sr);
        const auto suave = dj::analizarAudio (cancion (124.0, 9, false, 1, 30.0, sr, 91, 0.0, 0.15f), sr);
        check (fuerte.energia > suave.energia + 0.15, "la energía de una rola fuerte y llena (" + String (fuerte.energia, 2) + ") pasa a la de una suave ("
               + String (suave.energia, 2) + ")");
    }

    // the wheel
    check (dj::numeroCamelot (9, false) == 8 && dj::numeroCamelot (0, true) == 8 && dj::numeroCamelot (5, false) == 4 && dj::numeroCamelot (7, true) == 9,
           "rueda de Camelot: La menor 8A, Do mayor 8B, Fa menor 4A, Sol mayor 9B");
    check (dj::distanciaCamelot (9, false, 4, false) == 1 && dj::distanciaCamelot (9, false, 0, true) == 1 && dj::distanciaCamelot (9, false, 11, false) == 2,
           "vecinas: 8A con 9A y con 8B; 8A con 10A son dos pasos");
    check (dj::distanciaTempo (87.0, 174.0) < 0.01 && std::abs (dj::distanciaTempo (124.0, 128.0) - 3.125) < 0.01, "87 y 174 BPM mezclan; 124 y 128 están a 3 %");

    // a set: eight songs in a mess come out in a walk around the wheel, the energy climbing
    {
        std::vector<dj::Analisis> set {
            // shuffled; there is a walk 8A 9A 9B 10B 10A 11A 11B 12B with the energy going up
            falsa ("A", 124, 2, true, 0.55), falsa ("B", 122, 9, false, 0.30), falsa ("C", 126, 6, false, 0.70), falsa ("D", 128, 4, true, 0.85),
            falsa ("E", 123, 4, false, 0.40), falsa ("F", 125, 11, false, 0.60), falsa ("G", 127, 9, true, 0.80), falsa ("H", 124, 7, true, 0.50) };
        const auto order = dj::ordenarSet (set);
        String walk;
        int clashes = 0, falls = 0;
        double maxJump = 0.0;
        for (size_t i = 0; i < order.size(); ++i)
        {
            const auto& a = set[(size_t) order[i]];
            walk << a.titulo << " " << a.camelot() << " " << String (roundToInt (a.bpm)) << (i + 1 < order.size() ? ", " : "");
            if (i + 1 < order.size())
            {
                const auto& b = set[(size_t) order[i + 1]];
                clashes += dj::distanciaCamelot (a.key, a.major, b.key, b.major) > 1 ? 1 : 0;
                falls += b.energia < a.energia - 0.15 ? 1 : 0;
                maxJump = jmax (maxJump, dj::distanciaTempo (a.bpm, b.bpm));
            }
        }
        std::cout << "    set: " << walk << std::endl;
        check (order.size() == set.size() && set[(size_t) order[0]].titulo == "B", "el set empieza con la más tranquila (" + set[(size_t) order[0]].titulo + ")");
        check (clashes == 0, "en el set, " + String ((int) order.size() - 1 - clashes) + " de " + String ((int) order.size() - 1) + " cambios son a la misma tonalidad o a una vecina");
        check (falls == 0 && maxJump <= 4.2, "la energía no cae de golpe y el tempo no salta más de 4 % (" + String (maxJump, 1) + " %)");
        if (order.size() > 1) std::cout << "    " << dj::transicion (set[(size_t) order[0]], set[(size_t) order[1]]) << std::endl;
    }

    // Aleatoria que aprende tu gusto
    {
        gusto::olvidar();
        const StringArray bajos { "Uno", "Dos", "Tres", "Cuatro" };
        auto share = [&] (const String& which)
        {
            Random r (5);
            int k = 0;
            for (int i = 0; i < 4000; ++i) k += gusto::elegir (r, "bajo", bajos) == which ? 1 : 0;
            return k / 4000.0;
        };
        const double before = share ("Dos");
        for (int i = 0; i < 3; ++i) gusto::calificar ({ "bajo=Dos" }, true);
        for (int i = 0; i < 4; ++i) gusto::calificar ({ "bajo=Cuatro" }, false);
        const double liked = share ("Dos"), disliked = share ("Cuatro");
        check (std::abs (before - 0.25) < 0.03 && liked > 0.7 && disliked > 0.0 && disliked < 0.03,
               "un bajo con 3 \"Me gusta\" sale " + String (roundToInt (liked * 100)) + " % de las veces (antes " + String (roundToInt (before * 100))
               + " %); uno con 4 \"No me gusta\" sale " + String (disliked * 100, 1) + " %, pero sigue saliendo");
        check (gusto::archivo().existsAsFile() && gusto::archivo().loadFileAsString().contains ("Dos"), "los puntos se guardan en " + gusto::archivo().getFileName());
        gusto::olvidar();
        if (auto* g = findGenre ("house"))
        {
            s.newProject (true, false, g);
            const auto chosen = StringArray::fromTokens (s.getEdit().state.getProperty ("racoonElecciones").toString(), "|", "");
            const auto t = s.rateSounds (true);
            bool all = ! chosen.isEmpty();
            for (auto& c : chosen) all = all && gusto::puntaje (c.upToFirstOccurrenceOf ("=", false, false), c.fromFirstOccurrenceOf ("=", false, false)) == 1.0;
            check (all && chosen.size() >= 6 && (bool) s.getEdit().state.getProperty ("racoonCalificado"),
                   "al armar un bloque Racoon anota lo que eligió (" + String (chosen.size()) + ": " + chosen.joinIntoString (", ").substring (0, 120) + "...) y \"Me gusta\" le suma un punto a cada uno");
            std::cout << "    " << t << std::endl;
        }
        gusto::olvidar();
    }

    // the track to mix with the house song
    {
        auto a = medidas[0];
        a.titulo = "Prueba house";
        s.newProject (false);
        const auto r = s.buildDjTool (a, 128);
        std::cout << "    " << r.texto << std::endl;
        int root = -1; bool maj = true;
        s.songKey (root, maj);
        check (r.ok && std::abs (s.getBpm() - a.bpm) < 0.001 && root == a.key && maj == a.major,
               "la herramienta DJ queda a " + String (s.getBpm(), 2) + " BPM en " + keyName (root, maj) + ", como la rola (" + r.genero + ")");
        check (r.compases >= 128 && r.intro >= 16 && r.outro >= 16, String (r.compases) + " compases, intro de " + String (r.intro) + " y outro de " + String (r.outro));
        // in the intro and the outro only the drums play
        int musicInIntro = 0, musicInOutro = 0, drumsInIntro = 0, musicInDrop = 0;
        const double introEnd = r.intro * 4.0, outroStart = (r.compases - r.outro) * 4.0;
        for (auto* t : te::getAudioTracks (s.getEdit()))
        {
            const bool drums = t->getName().startsWith ("Hamster") || t->getName().startsWith ("Mole");
            for (auto* c : t->getClips())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                    for (auto* n : mc->getSequence().getNotes())
                    {
                        const double b = mc->getStartBeat().inBeats() + n->getStartBeat().inBeats() - mc->getOffsetInBeats().inBeats();
                        if (drums) { drumsInIntro += b < introEnd ? 1 : 0; continue; }
                        if (b < introEnd || b >= outroStart) std::cout << "      " << t->getName() << " " << n->getNoteNumber() << " @" << b << std::endl;
                        musicInIntro += b < introEnd ? 1 : 0;
                        musicInOutro += b >= outroStart ? 1 : 0;
                        musicInDrop += b >= introEnd && b < outroStart ? 1 : 0;
                    }
        }
        check (drumsInIntro > 0 && musicInIntro == 0 && musicInOutro == 0 && musicInDrop > 0,
               "entra y sale solo con batería (" + String (drumsInIntro) + " golpes en la intro; notas de música en intro y outro: "
               + String (musicInIntro) + " y " + String (musicInOutro) + ")");
        if (SystemStats::getEnvironmentVariable ("RACOON_DJ_DEBUG", {}).isNotEmpty())
        {
            ExportOptions so; so.destination = out.getChildFile ("stems dj"); so.sampleRate = 44100.0; so.stems = true;
            Array<File> written;
            s.exportStems (so, &written);
            for (auto& f : written)
            {
                AudioBuffer<float> sb; double ssr = 44100.0;
                if (audio::readFile (s.engine, f, sb, ssr))
                {
                    const auto k = audio::keyFromChroma (audio::chromaOfAudio (sb, ssr));
                    std::cout << "      stem " << f.getFileNameWithoutExtension() << ": " << k.name() << " " << k.confidence << std::endl;
                }
            }
        }
        ExportOptions o;
        o.destination = out.getChildFile ("herramienta dj.wav");
        o.sampleRate = 44100.0; o.tailSeconds = 0.0;
        s.exportMix (o);
        AudioBuffer<float> b; double bsr = 44100.0;
        if (audio::readFile (s.engine, o.destination, b, bsr))
        {
            const auto again = dj::analizarAudio (b, bsr);
            check (std::abs (again.bpm - a.bpm) <= 0.05 && dj::distanciaCamelot (again.key, again.major, a.key, a.major) <= 1,
                   "medida otra vez, la herramienta DJ da " + again.resumen());
        }
        else check (false, "la herramienta DJ se exporta");
    }
    // changing a channel's sound (a preset of the plugin) keeps it playing the song: Chinchilla still sings the MIDI,
    // Skunk's grains still follow the notes, Mole still plays with the song, Badger plays the clip and not its own
    // sequencer. Every preset of the four, one after the other, and the channels rendered at the end
    if (auto* g = findGenre ("deephouse"))
    {
        s.newProject (true, false, g);
        MessageManager::getInstance()->runDispatchLoopUntil (1000);
        StringArray lost;
        int presets = 0;
        for (auto* t : te::getAudioTracks (s.getEdit()))
            for (auto* p : t->pluginList)
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
                    if (auto* pi = ext->getAudioPluginInstance())
                    {
                        const auto name = t->getName();
                        if (! (name.startsWith ("Chinchilla") || name.startsWith ("Skunk") || name.startsWith ("Mole") || name.startsWith ("Badger"))) continue;
                        auto valueOf = [pi] (const String& id) -> float
                        {
                            // the host sees the plugin's parameters by name
                            static const std::map<String, String> names { { "sing_on", "Cantar las notas MIDI" }, { "gmidi", "Granos con el teclado" },
                                                                          { "play", "Reproducir" }, { "seq_mode", "Seq Mode" }, { "lib", "Biblioteca" } };
                            for (auto* prm : pi->getParameters())
                                if (prm->getName (64) == String::fromUTF8 (names.at (id).toRawUTF8())) return prm->getValue();
                            return -1.0f;
                        };
                        const float sing = valueOf ("sing_on"), grains = valueOf ("gmidi"), play = valueOf ("play"), seq = valueOf ("seq_mode");
                        std::cout << "    " << name << ": cantar " << sing << ", granos " << grains << ", reproducir " << play << ", secuenciador " << seq << std::endl;
                        for (int k = 0; k < pi->getNumPrograms(); ++k)
                        {
                            pi->setCurrentProgram (k);
                            ++presets;
                            if (std::abs (valueOf ("sing_on") - sing) > 1e-6f || std::abs (valueOf ("gmidi") - grains) > 1e-6f
                                || std::abs (valueOf ("play") - play) > 1e-6f || std::abs (valueOf ("seq_mode") - seq) > 1e-6f)
                                lost.addIfNotAlreadyThere (name + " / " + pi->getProgramName (k));
                        }
                        // and it ends on a sound in the middle of the list
                        pi->setCurrentProgram (pi->getNumPrograms() / 2);
                        if (std::abs (valueOf ("lib")) < 1e-6f && name.startsWith ("Skunk")) lost.addIfNotAlreadyThere (name + String::fromUTF8 (": sin material de biblioteca"));
                    }
        // RACOON_PRESETS_DEBUG: every Skunk sound rendered alone, to find the silent ones
        if (SystemStats::getEnvironmentVariable ("RACOON_PRESETS_DEBUG", {}).isNotEmpty())
            for (auto* t : te::getAudioTracks (s.getEdit()))
                if (t->getName().startsWith ("Skunk"))
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t) != nullptr ? Session::instrumentOf (*t) : t->pluginList[0]))
                        if (auto* pi = ext->getAudioPluginInstance())
                        {
                            t->setSolo (true);
                            for (int k = 0; k < pi->getNumPrograms(); ++k)
                            {
                                pi->setCurrentProgram (k);
                                MessageManager::getInstance()->runDispatchLoopUntil (100);
                                ExportOptions o; o.destination = out.getChildFile ("skunk " + String (k) + ".wav"); o.sampleRate = 44100.0; o.loopOnly = true; o.tailSeconds = 0.0;
                                s.exportMix (o);
                                AudioBuffer<float> b; double bsr = 44100.0;
                                audio::readFile (s.engine, o.destination, b, bsr);
                                String ps;
                                for (auto* prm : pi->getParameters())
                                    for (auto* nm : { "Biblioteca", "Captura: modo", "Granos: mezcla", "Granos con el teclado", "Espectro: encendido", "Espectro: congelar" })
                                        if (prm->getName (64) == String::fromUTF8 (nm)) ps << nm << "=" << prm->getCurrentValueAsText() << " ";
                                std::cout << "      skunk " << k << " " << pi->getProgramName (k) << ": " << String (Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples())), 1) << " dB | " << ps << std::endl;
                            }
                            t->setSolo (false);
                        }
        check (presets > 40 && lost.isEmpty(), "cambiar el sonido de Chinchilla, Skunk, Mole y Badger (" + String (presets)
               + " presets) conserva cómo tocan la rola " + lost.joinIntoString ("; ").substring (0, 300));
        ExportOptions so; so.destination = out.getChildFile ("stems presets"); so.sampleRate = 44100.0; so.stems = true; so.loopOnly = true;
        Array<File> written;
        s.exportStems (so, &written);
        StringArray silent, heard;
        for (auto& f : written)
        {
            const auto n = f.getFileNameWithoutExtension();
            if (! (n.contains ("Chinchilla") || n.contains ("Skunk") || n.contains ("Mole") || n.contains ("Badger"))) continue;
            AudioBuffer<float> b; double bsr = 44100.0;
            const bool ok = audio::readFile (s.engine, f, b, bsr);
            const float rms = ok ? b.getRMSLevel (0, 0, b.getNumSamples()) : 0.0f;
            (rms > 0.001f ? heard : silent).add (n + " " + String (Decibels::gainToDecibels (rms), 1) + " dB");
        }
        check (silent.isEmpty() && heard.size() == 4, "y después de cambiarles el sonido siguen sonando en la maqueta: " + heard.joinIntoString (", ")
               + (silent.isEmpty() ? String() : String::fromUTF8 (" | callados: ") + silent.joinIntoString (", ")));
    }

    {
        // two blocks of the same genre: another motif, other sounds, the header names the sound, and the lines sit low
        std::cout << "-- bloques que no se repiten" << std::endl;
        StringArray melodies, sounds, headers;
        double sum = 0.0, bassSum = 0.0; int count = 0, bassCount = 0;
        for (int k = 1; k <= 2; ++k)
        {
            setenv ("RACOON_SEMILLA_SONIDOS", String (k * 104729).toRawUTF8(), 1);
            s.newProject (true, false, findGenre ("deephouse"));
            MessageManager::getInstance()->runDispatchLoopUntil (1500);
            sounds.add (s.getEdit().state.getProperty ("racoonSounds").toString());
            String mel;
            for (auto* t : te::getAudioTracks (s.getEdit()))
            {
                for (auto* c : t->getClips())
                    if (auto* mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc->getName().startsWith (String::fromUTF8 ("Melodía")))
                        for (auto* n : mc->getSequence().getNotes()) { mel << n->getNoteNumber() << " "; sum += n->getNoteNumber(); ++count; }
                if (t->getName().startsWith ("Badger"))
                    for (auto* c : t->getClips())
                        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                            for (auto* n : mc->getSequence().getNotes()) { bassSum += n->getNoteNumber(); ++bassCount; }
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t)))
                    if (auto* pi = ext->getAudioPluginInstance(); pi != nullptr && t->getName().containsIgnoreCase ("Acordes"))
                    {
                        if (auto xml = parseXML (Session::pluginStateXml (*pi)))
                            headers.add (xml->getStringAttribute ("preset"));
                    }
            }
            melodies.add (mel);
        }
        unsetenv ("RACOON_SEMILLA_SONIDOS");
        check (melodies.size() == 2 && melodies[0].isNotEmpty() && melodies[0] != melodies[1], "dos bloques del mismo género traen otro motivo");
        check (sounds[0] != sounds[1], "y otros sonidos: " + sounds.joinIntoString (" / ").replace ("\n", "; "));
        check (headers.size() == 2 && ! headers.contains ("Inicial") && ! headers.contains ({}), "el encabezado de Chipmunk nombra su sonido: " + headers.joinIntoString (", "));
        const double avg = count > 0 ? sum / count : 99.0;
        check (avg < 62.0, "la melodía va grave: nota media " + String (avg, 1));
        const double bassAvg = bassCount > 0 ? bassSum / bassCount : 99.0;
        check (bassAvg < 38.0, "el bajo va profundo: nota media " + String (bassAvg, 1));
        bool shrill = false;
        for (auto& x : sounds) shrill = shrill || x.contains ("Voz aguda") || x.contains ("Campanas") || x.containsIgnoreCase ("cristal");
        check (! shrill, "nada de voces agudas, campanas ni cristal en los sonidos de los bloques");
    }

    s.newProject (false);
    MessageManager::getInstance()->runDispatchLoopUntil (6000);
    return djFailures;
}

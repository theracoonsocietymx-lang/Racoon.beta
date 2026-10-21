// Squirrel checks (included by Tests/FamilyTest.cpp)
#include "SquirrelProcessor.h"
#include "../../Shared/Biblioteca.h"

static void testSquirrel (const juce::File& out)
{
    using namespace juce;
    using racoon::squirrel::Processor;
    using racoon::squirrel::Zone;
    commonChecks ("Squirrel", [] { return new Processor(); }, out);
    std::cout << "sampler" << std::endl;
    const double sr = 48000.0;

    auto render = [&] (Processor& p, MidiBuffer& m, int len, int block = 256)
    {
        AudioBuffer<float> in (2, len); in.clear();
        return tu::render (p, in, block, &m);
    };
    auto noise = [] (int len, int chans, int seed)
    {
        AudioBuffer<float> b (chans, len);
        tu::fillNoise (b, 0.5f, seed);
        return b;
    };
    auto maxDiff = [] (const AudioBuffer<float>& a, int ach, int aStart, const AudioBuffer<float>& b, int bch, int bStart, int len, bool reversed = false)
    {
        float d = 0.0f;
        for (int i = 0; i < len; ++i)
            d = jmax (d, std::abs (a.getSample (ach, aStart + i) - b.getSample (bch, reversed ? bStart - i : bStart + i)));
        return d;
    };

    // bit-perfect playback at rate 1 with no processing
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        auto smp = noise ((int) sr, 2, 3);
        p.addBuffer (smp, sr, "ruido");
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 100);
        auto o = render (p, m, (int) sr + 2000, 128);
        const float d = jmax (maxDiff (o, 0, 100, smp, 0, 0, (int) sr), maxDiff (o, 1, 100, smp, 1, 0, (int) sr));
        check (d == 0.0f && tu::peak (o, 100 + (int) sr, 1500) == 0.0f, "reproducción bit-perfect a velocidad 1 (diferencia "_u + String (d) + ")");
    }

    // reverse is bit-perfect too
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        auto smp = noise (24000, 1, 4);
        p.addBuffer (smp, sr, "ruido", [] (Zone& z) { z.reverse = true; });
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 0);
        auto o = render (p, m, 25000);
        check (maxDiff (o, 0, 0, smp, 0, 23999, 24000, true) == 0.0f, "reverso exacto");
    }

    // a long sample plays to the end without a single dropped or changed sample, with tiny blocks
    {
        Processor p;
        p.prepareToPlay (sr, 64);
        auto smp = noise ((int) sr * 120, 2, 5);
        p.addBuffer (smp, sr, "largo");
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 0);
        auto o = render (p, m, (int) sr * 120, 61);
        const float d = jmax (maxDiff (o, 0, 0, smp, 0, 0, (int) sr * 120), maxDiff (o, 1, 0, smp, 1, 0, (int) sr * 120));
        check (d == 0.0f, "archivo de 2 minutos en bloques de 61: sin cortes (diferencia " + String (d) + ")");
    }

    // pitch: a 441 Hz sample recorded at 44.1 kHz plays at 441 Hz in a 48 kHz session; 2x rate is an octave up; +7 semitones
    {
        auto play = [&] (std::function<void (Processor&)> setup, int note)
        {
            Processor p;
            p.prepareToPlay (sr, 256);
            AudioBuffer<float> smp (1, 44100 * 2);
            tu::fillSine (smp, 441.0, 44100.0, 0.5f, 0.0);
            p.addBuffer (smp, 44100.0, "seno");
            setup (p);
            MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, note, (uint8) 127), 0);
            auto o = render (p, m, (int) sr);
            auto s = tu::spectrum (o, 0, 4096, 15);
            size_t best = 1;
            for (size_t i = 1; i < s.size(); ++i) if (s[i] > s[best]) best = i;
            // refine with a parabola around the peak
            const double a = std::log (s[best - 1] + 1e-20), b = std::log (s[best] + 1e-20), c = std::log (s[best + 1] + 1e-20);
            return ((double) best + 0.5 * (a - c) / (a - 2 * b + c)) * sr / 32768.0;
        };
        const double f1 = play ([] (Processor&) {}, 60), f2 = play ([] (Processor& p) { p.setRealValue ("rate", 2.0f); }, 60), f3 = play ([] (Processor&) {}, 67);
        check (std::abs (f1 - 441.0) < 0.5 && std::abs (f2 - 882.0) < 1.0 && std::abs (f3 - 441.0 * std::pow (2.0, 7.0 / 12.0)) < 0.6,
               "afinación: 44.1 kHz en sesión de 48 kHz "_u + String (f1, 2) + " Hz, velocidad 2x " + String (f2, 1) + " Hz, quinta " + String (f3, 1) + " Hz");
    }

    // loops: no jump at the loop point (forward with crossfade, and ping-pong)
    for (int mode : { 1, 2 })
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> smp (1, (int) sr);
        tu::fillSine (smp, 330.7, sr, 0.5f, 0.0);   // the loop points do not fall on whole cycles
        p.addBuffer (smp, sr, "seno", [mode] (Zone& z) { z.loopMode = mode; z.loopStart = 10000; z.loopEnd = 20111; });
        p.setRealValue ("loopxf", 30.0f);
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 0);
        auto o = render (p, m, (int) sr * 3);
        const float slope = 0.5f * (float) (MathConstants<double>::twoPi * 330.7 / sr);
        AudioBuffer<float> part (1, (int) sr * 2);
        part.copyFrom (0, 0, o, 0, (int) sr / 2, (int) sr * 2);
        const float j = tu::maxJump (part);
        check (j < slope * 1.25f && tu::rms (o, (int) sr * 2, (int) sr / 2) > 0.2f,
               String (mode == 1 ? "loop con crossfade" : "loop ping-pong") + ": sin saltos durante 2 s (salto " + String (j, 4) + ", el seno solo llega a " + String (slope, 4) + ")");
    }

    // one-shot ignores the note-off; normal zones release
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> smp (1, (int) sr);
        tu::fillSine (smp, 200.0, sr, 0.5f, 0.0);
        p.addBuffer (smp, sr, "uno", [] (Zone& z) { z.oneShot = true; });
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 0);
        m.addEvent (MidiMessage::noteOff (1, 60), 4800);
        auto o = render (p, m, (int) sr);
        check (tu::rms (o, (int) (sr * 0.6), 4800) > 0.3f, "one-shot: sigue sonando después de soltar la tecla"_u);
    }

    // choke group: the closed hat cuts the open one within a few milliseconds
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> open (1, (int) sr), closed (1, 2400);
        tu::fillSine (open, 500.0, sr, 0.5f, 0.0);
        tu::fillSine (closed, 3000.0, sr, 0.2f, 0.0);
        p.addBuffer (open, sr, "abierto", [] (Zone& z) { z.lowKey = z.highKey = z.root = 46; z.choke = 1; z.oneShot = true; });
        p.addBuffer (closed, sr, "cerrado", [] (Zone& z) { z.lowKey = z.highKey = z.root = 42; z.choke = 1; z.oneShot = true; }, false);
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 46, (uint8) 127), 0);
        m.addEvent (MidiMessage::noteOn (1, 42, (uint8) 127), 9600);
        auto o = render (p, m, (int) sr);
        check (tu::tone (o, 0, 9600 + 480, 2400, 500.0, sr) < 0.01f && tu::tone (o, 0, 4000, 2400, 500.0, sr) > 0.3f,
               "choke: el hi-hat cerrado corta al abierto en menos de 10 ms");
    }

    // round robin: three zones on the same key take turns; velocity layers pick the right zone
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        const double freqs[3] { 300.0, 450.0, 600.0 };
        for (int k = 0; k < 3; ++k)
        {
            AudioBuffer<float> b (1, 9600);
            tu::fillSine (b, freqs[k], sr, 0.5f, 0.0);
            p.addBuffer (b, sr, "rr" + String (k), [] (Zone& z) { z.lowKey = z.highKey = z.root = 38; z.roundRobin = 1; z.oneShot = true; }, k == 0);
        }
        AudioBuffer<float> soft (1, 9600), loud (1, 9600);
        tu::fillSine (soft, 1000.0, sr, 0.5f, 0.0);
        tu::fillSine (loud, 1500.0, sr, 0.5f, 0.0);
        p.addBuffer (soft, sr, "suave", [] (Zone& z) { z.lowKey = z.highKey = z.root = 40; z.highVel = 63; z.oneShot = true; }, false);
        p.addBuffer (loud, sr, "fuerte", [] (Zone& z) { z.lowKey = z.highKey = z.root = 40; z.lowVel = 64; z.oneShot = true; }, false);
        MidiBuffer m;
        for (int k = 0; k < 4; ++k) m.addEvent (MidiMessage::noteOn (1, 38, (uint8) 100), k * 12000);
        m.addEvent (MidiMessage::noteOn (1, 40, (uint8) 30), 48000);
        m.addEvent (MidiMessage::noteOn (1, 40, (uint8) 120), 60000);
        auto o = render (p, m, 72000);
        auto loudest = [&] (int at, std::initializer_list<double> fs)
        {
            double best = 0; float lv = 0;
            for (double f : fs) { const float t = tu::tone (o, 0, at + 1000, 4096, f, sr); if (t > lv) { lv = t; best = f; } }
            return best;
        };
        const double a = loudest (0, { 300, 450, 600 }), b = loudest (12000, { 300, 450, 600 }), c = loudest (24000, { 300, 450, 600 }), d = loudest (36000, { 300, 450, 600 });
        const double vs = loudest (48000, { 1000, 1500 }), vl = loudest (60000, { 1000, 1500 });
        check (a == 300 && b == 450 && c == 600 && d == 300, "round robin: 300, 450, 600, 300 Hz → "_u + String (a) + ", " + String (b) + ", " + String (c) + ", " + String (d));
        check (vs == 1000 && vl == 1500, "capas de velocidad: suave y fuerte suenan su zona");
    }

    // slicing: bursts at known places are found; the grid snaps them
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        AudioBuffer<float> b (1, (int) sr * 2);
        b.clear();
        const int hits[] { 0, 12000, 30000, 48000, 72000 };
        Random r (8);
        for (int h : hits)
            for (int i = 0; i < 6000; ++i) b.setSample (0, h + i, i == 0 ? 0.6f : (r.nextFloat() * 2 - 1) * 0.6f * std::exp (-i / 900.0f));
        p.addBuffer (b, sr, "golpes");
        p.detectSlices();
        const auto cuts = p.program().slices;
        bool ok = cuts.size() == 5;
        int worst = 0;
        for (size_t k = 0; ok && k < 5; ++k) { worst = jmax (worst, std::abs (cuts[k] - hits[k])); ok = worst <= 64; }
        check (ok, "cortes por transitorios: " + String ((int) cuts.size()) + " de 5, error máximo "_u + String (worst) + " muestras");
        p.setRealValue ("slgrid", 3); p.setRealValue ("slbpm", 120);   // 1/16 at 120 = 6000 samples
        p.detectSlices();
        bool grid = ! p.program().slices.empty();
        for (int c : p.program().slices) grid = grid && c % 6000 == 0;
        check (grid, "cortes cuantizados a 1/16: todos sobre la rejilla");
        // slice 2 on D1 plays exactly that slice
        p.setRealValue ("mode", 1);
        p.setRealValue ("slgrid", 0);
        p.detectSlices();
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 38, (uint8) 127), 0);
        auto o = render (p, m, 20000);
        check (maxDiff (o, 0, 0, b, 0, 30000, 17000) == 0.0f, "modo Cortes: la tecla D1 toca el tercer corte, exacto");
    }

    // project portability: file references, missing files and relinking, embedded samples
    {
        auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("squirrel_test");
        dir.deleteRecursively();
        dir.createDirectory();
        auto f = dir.getChildFile ("golpe.wav");
        auto smp = noise (12000, 2, 9);
        {
            std::unique_ptr<OutputStream> os = std::make_unique<FileOutputStream> (f);
            auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (32)
                                                              .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
            w->writeFromAudioSampleBuffer (smp, 0, smp.getNumSamples());
        }
        Processor p;
        p.prepareToPlay (sr, 256);
        const bool loaded = p.loadFileNow (f, false);
        MemoryBlock state;
        p.getStateInformation (state);

        // file still there: the project opens with it
        Processor q;
        q.prepareToPlay (sr, 256);
        q.setStateInformation (state.getData(), (int) state.getSize());
        q.waitUntilIdle();
        const bool back = q.program().zones.size() == 1 && q.program().zones[0].sample != nullptr;

        // file moved: the zone is marked missing, then found again in its new folder
        auto moved = dir.getChildFile ("nueva").getChildFile ("golpe.wav");
        moved.getParentDirectory().createDirectory();
        f.moveFileTo (moved);
        Processor m2;
        m2.prepareToPlay (sr, 256);
        m2.setStateInformation (state.getData(), (int) state.getSize());
        m2.waitUntilIdle();
        const bool flagged = m2.program().zones.size() == 1 && m2.program().zones[0].missing;
        const int found = m2.relocateMissing (dir);
        m2.waitUntilIdle();
        const bool relinked = found == 1 && m2.program().zones[0].sample != nullptr && ! m2.program().zones[0].missing;
        check (loaded && back && flagged && relinked, "proyecto portable: abre la muestra, marca la que falta y la vuelve a encontrar en otra carpeta");

        // embedded: the project carries the audio, bit for bit, even with the file gone
        m2.edit ([] (racoon::squirrel::Program& prog) { prog.embed = true; });
        MemoryBlock packed;
        m2.getStateInformation (packed);
        moved.deleteFile();
        Processor e;
        e.prepareToPlay (sr, 256);
        e.setStateInformation (packed.getData(), (int) packed.getSize());
        e.waitUntilIdle();
        MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 127), 0);
        auto o = render (e, m, 13000);
        const float d = jmax (maxDiff (o, 0, 0, smp, 0, 0, 12000), maxDiff (o, 1, 0, smp, 1, 0, 12000));
        check (e.program().zones.size() == 1 && d == 0.0f, "muestra incrustada en el proyecto: vuelve exacta sin el archivo ("_u + String (packed.getSize() / 1024) + " KB de estado)");
        dir.deleteRecursively();
    }

    // 128 voices and no hanging notes
    {
        Processor p;
        p.prepareToPlay (sr, 256);
        p.setRealValue ("voices", 128);
        p.setRealValue ("amp_r", 0.05f);
        MidiBuffer m;
        for (int k = 0; k < 128; ++k) m.addEvent (MidiMessage::noteOn (1, k, (uint8) 100), k * 8);
        AudioBuffer<float> in (2, 4800); in.clear();
        auto o = tu::render (p, in, 256, &m);
        const int peakVoices = p.activeVoices.load();
        MidiBuffer off;
        for (int k = 0; k < 128; ++k) off.addEvent (MidiMessage::noteOff (1, k), 0);
        AudioBuffer<float> in2 (2, (int) sr); in2.clear();
        auto o2 = tu::render (p, in2, 256, &off);
        check (peakVoices == 128 && p.activeVoices.load() == 0 && tu::allFinite (o) && tu::peak (o2, (int) sr / 2, (int) sr / 2) == 0.0f,
               "128 voces a la vez, y al soltar no queda ninguna colgada");
    }
    // SFZ: regiones con teclas por nombre, velocidades, bucle y nombres de archivo con espacios
    {
        auto dir = out.getChildFile ("sfz_prueba");
        dir.deleteRecursively();
        dir.getChildFile ("muestras").createDirectory();
        auto writeTone = [sr] (const File& f, float hz)
        {
            AudioBuffer<float> b (1, (int) sr);
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * std::sin (MathConstants<float>::twoPi * hz * (float) i / (float) sr));
            f.deleteFile();
            std::unique_ptr<OutputStream> os (f.createOutputStream().release());
            auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (24));
            if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        };
        writeTone (dir.getChildFile ("muestras/la grave.wav"), 220.0f);
        writeTone (dir.getChildFile ("muestras/la agudo.wav"), 440.0f);
        dir.getChildFile ("prueba.sfz").replaceWithText (
            "// prueba\n<control> default_path=muestras/\n<global> ampeg_release=0.7\n"
            "<group> lovel=1 hivel=127\n"
            "<region> sample=la grave.wav lokey=c2 hikey=f#3 pitch_keycenter=a2\n"
            "<region> sample=la agudo.wav lokey=g3 hikey=127 pitch_keycenter=69 loop_mode=loop_continuous loop_start=1000 loop_end=40000 volume=-6\n"
            "<region> sample=la agudo.wav key=100 trigger=release\n");
        Processor p;
        p.prepareToPlay (sr, 256);
        const int n = p.loadSfz (dir.getChildFile ("prueba.sfz"));
        p.waitUntilIdle();
        const auto& z = p.program().zones;
        const bool ok = n == 2 && z.size() == 2 && z[0].lowKey == 36 && z[0].highKey == 54 && z[0].root == 45 && z[1].lowKey == 55
                        && z[1].root == 69 && z[1].loopMode == racoon::squirrel::loopForward && z[1].loopStart == 1000 && z[1].loopEnd == 40000
                        && std::abs (z[1].gainDb + 6.0f) < 0.01f && z[0].sample != nullptr && z[1].sample != nullptr;
        check (ok && std::abs (p.value ("amp_r") - 0.7f) < 0.02f, "SFZ: 2 regiones (la de release se ignora), teclas por nombre, bucle, volumen y relajación de 0.7 s");
        dir.deleteRecursively();
    }

    // Muestras Racoon: un instrumento cromático, un sonido suelto con capas y un kit de batería en el teclado
    {
        const auto& cat = racoon::biblio::Catalogo::get();
        if (! cat.disponible()) std::cout << "  (sin Muestras Racoon: se saltan sus pruebas)" << std::endl;
        else
        {
            const racoon::biblio::Instrumento* piano = nullptr;
            for (auto& i : cat.instrumentos) if (i.cat == "piano" && i.notas >= 20) { piano = &i; break; }
            check (piano != nullptr && cat.instrumentos.size() >= 60, "Muestras Racoon trae " + String ((int) cat.instrumentos.size()) + " instrumentos cromáticos");
            if (piano != nullptr)
            {
                Processor p;
                p.prepareToPlay (sr, 256);
                const bool ok = p.loadLibraryInstrument (piano->id);
                p.waitUntilIdle (30000);
                int loaded = 0, lib = 0;
                for (auto& z : p.program().zones) { loaded += z.sample != nullptr ? 1 : 0; lib += z.path.startsWith ("biblioteca:") ? 1 : 0; }
                MidiBuffer m; m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
                auto o = render (p, m, (int) sr);
                check (ok && loaded == (int) p.program().zones.size() && lib == loaded && tu::peak (o, 0, o.getNumSamples()) > 0.05f,
                       piano->nombre + ": " + String (loaded) + " zonas cargadas desde la biblioteca, y suena (pico " + String (tu::peak (o, 0, o.getNumSamples()), 2) + ")");
                // el proyecto guarda rutas relativas a la biblioteca y vuelve completo
                MemoryBlock mb;
                p.getStateInformation (mb);
                Processor q;
                q.prepareToPlay (sr, 256);
                q.setStateInformation (mb.getData(), (int) mb.getSize());
                q.waitUntilIdle (30000);
                int back = 0;
                for (auto& z : q.program().zones) back += z.sample != nullptr ? 1 : 0;
                check (back == loaded, "el proyecto vuelve con las " + String (back) + " zonas del piano (" + String ((int) (mb.getSize() / 1024)) + " KB de estado)");
            }
            const auto* kit = cat.kit ("cumbia");
            if (kit != nullptr)
            {
                Processor p;
                p.prepareToPlay (sr, 256);
                const bool ok = p.loadLibraryKit (kit->id);
                p.waitUntilIdle (30000);
                MidiBuffer m;
                m.addEvent (MidiMessage::noteOn (1, 36, (uint8) 30), 0);
                auto soft = render (p, m, 12000);
                MidiBuffer m2;
                m2.addEvent (MidiMessage::noteOn (1, 36, (uint8) 127), 0);
                auto loud = render (p, m2, 12000);
                check (ok && p.program().zones.size() >= 16 && tu::peak (loud, 0, loud.getNumSamples()) > tu::peak (soft, 0, soft.getNumSamples()) * 1.3f && tu::peak (soft, 0, soft.getNumSamples()) > 0.001f,
                       "kit Cumbia en el teclado: " + String ((int) p.program().zones.size()) + " zonas, el bombo suave y fuerte cambian de capa");
            }
        }
    }
}

static RegisterTest regSquirrel ("Squirrel", testSquirrel);

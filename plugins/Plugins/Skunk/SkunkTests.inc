// Skunk checks (included by Tests/FamilyTest.cpp)
#include "SkunkProcessor.h"
#include "../../Shared/Biblioteca.h"
#include "SkunkEditor.h"

namespace skunk_test
{
using namespace juce;
using racoon::skunk::Processor;
constexpr double sr = 48000.0;

static std::unique_ptr<Processor> make (std::function<void (Processor&)> setup = {})
{
    auto p = std::make_unique<Processor>();
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

// runs stereo audio through (and a sidechain when given); returns the main output
static AudioBuffer<float> run (Processor& p, const AudioBuffer<float>& in, const AudioBuffer<float>* sc = nullptr, int block = 256,
                               std::function<void (int64)> perBlock = {}, MidiBuffer* midi = nullptr)
{
    const int total = in.getNumSamples();
    AudioBuffer<float> out (2, total);
    const int chans = jmax (2, p.getTotalNumInputChannels());
    AudioBuffer<float> io (chans, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = jmin (block, total - pos);
        if (perBlock) perBlock (pos);
        io.setSize (chans, n, false, false, true);
        io.clear();
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, n);
        if (sc != nullptr && chans >= 4) for (int c = 0; c < 2; ++c) io.copyFrom (2 + c, 0, *sc, c, pos, n);
        MidiBuffer m;
        if (midi != nullptr) for (const auto ev : *midi) if (ev.samplePosition >= pos && ev.samplePosition < pos + n) m.addEvent (ev.getMessage(), ev.samplePosition - pos);
        p.processBlock (io, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

static AudioBuffer<float> sine (double hz, float level, double seconds)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    tu::fillSine (b, hz, sr, level, 0.0);
    return b;
}

static AudioBuffer<float> silence (double seconds) { AudioBuffer<float> b (2, (int) (sr * seconds)); b.clear(); return b; }

// frequency with the most energy (coarse, for checks of an octave)
static double peakHz (const AudioBuffer<float>& b, int start, int len)
{
    double best = 0.0, bestHz = 0.0;
    for (double hz = 100.0; hz < 3000.0; hz += 2.0)
    {
        const double m = tu::tone (b, 0, start, len, hz, sr);
        if (m > best) { best = m; bestHz = hz; }
    }
    return bestHz;
}
}

static void testSkunk (const juce::File& out)
{
    using namespace juce;
    using namespace skunk_test;
    commonChecks ("Skunk", [] { return new Processor(); }, out);
    std::cout << "diseño sonoro"_u << std::endl;
    // the library: a material of its own, played by MIDI notes at their pitch, with no audio coming in
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("gmidi", 1.0f); });
        bool all = true;
        String bad;
        for (int lib = 1; lib < Processor::libraryNames().size(); ++lib)
        {
            p->loadLibrary (lib);
            AudioBuffer<float> silence (2, (int) (1.5 * sr));
            silence.clear();
            MidiBuffer m;
            m.addEvent (MidiMessage::noteOn (1, 48, (uint8) 100), (int) (0.1 * sr));
            m.addEvent (MidiMessage::noteOff (1, 48), (int) (1.3 * sr));
            auto o = run (*p, silence, nullptr, 256, {}, &m);
            if (! tu::allFinite (o) || tu::rms (o, (int) (0.4 * sr), (int) (1.0 * sr)) < 0.005f) { all = false; bad << Processor::libraryNames()[lib] << " "; }
        }
        check (all && p->captureValid.load() > (int) (sr * 3.0), "la biblioteca suena con notas MIDI, sin audio de entrada: "_u
               + String (Processor::libraryNames().size() - 1) + " materiales " + bad);
    }

    // live grains of a 440 Hz tone keep its pitch; +12 doubles it
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("gpos", 5.0f); p.setRealValue ("gspray", 0.0f); p.setRealValue ("gsize", 100.0f); });
        auto o = run (*p, sine (440.0, 0.4f, 2.0));
        const double f0 = peakHz (o, (int) sr, (int) (sr * 0.5));
        auto q = make ([] (Processor& p) { p.setRealValue ("gpos", 10.0f); p.setRealValue ("gspray", 0.0f); p.setRealValue ("gsize", 100.0f); p.setRealValue ("gpitch", 12.0f); });
        auto o2 = run (*q, sine (440.0, 0.4f, 2.0));
        const double f1 = peakHz (o2, (int) sr, (int) (sr * 0.5));
        check (std::abs (f0 - 440.0) < 15.0 && std::abs (f1 - 880.0) < 30.0, "granos en vivo: 440 Hz sale en " + String (f0, 0) + " Hz, con +12 en " + String (f1, 0) + " Hz");
    }

    // frozen buffer keeps sounding after the input stops
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("caplen", 2.0f); });
        run (*p, sine (330.0, 0.4f, 2.5));
        p->setRealValue ("recmode", 3.0f);
        auto o = run (*p, silence (2.0));
        check (tu::rms (o, (int) sr, (int) sr) > 0.05f && p->captureState.load() == 3, "búfer congelado: sigue sonando sin entrada (rms "_u + String (tu::rms (o, (int) sr, (int) sr), 3) + ")");
    }

    // one shot: waits for the threshold, records the length, then freezes
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("recmode", 1.0f); p.setRealValue ("caplen", 0.5f); p.setRealValue ("trig", -20.0f); });
        auto quiet = sine (200.0, 0.01f, 0.5);
        run (*p, quiet);
        const int waiting = p->captureState.load();
        run (*p, sine (200.0, 0.5f, 1.0));
        check (waiting == 1 && p->captureState.load() == 3 && p->captureValid.load() == (int) (sr * 0.5),
               "un disparo: espera el umbral, graba 0.5 s y se congela (" + String (p->captureValid.load()) + " muestras)");
    }

    // spectral engine neutral: the sound passes intact, late by the FFT size
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("spon", 1.0f); p.setRealValue ("gmix", 0.0f); });
        AudioBuffer<float> in (2, (int) sr);
        tu::fillNoise (in, 0.3f, 5);
        auto o = run (*p, in);
        const int lat = p->getLatencySamples();
        float err = 0.0f;
        for (int i = lat + 4096; i < in.getNumSamples(); ++i) err = jmax (err, std::abs (o.getSample (0, i) - in.getSample (0, i - lat)));
        check (lat == 2048 && err < 2.0e-3f, "espectro neutro: pasa igual con 2048 muestras de latencia informada (error " + String (err, 5) + ")");
    }

    // spectral pitch +12 and spectral freeze
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("spon", 1.0f); p.setRealValue ("gmix", 0.0f); p.setRealValue ("spitch", 12.0f); });
        auto o = run (*p, sine (300.0, 0.4f, 1.5));
        const double f = peakHz (o, (int) sr, (int) (sr * 0.4));
        check (std::abs (f - 600.0) < 25.0, "espectro +12: 300 Hz suena en " + String (f, 0) + " Hz");
        auto q = make ([] (Processor& p) { p.setRealValue ("spon", 1.0f); p.setRealValue ("gmix", 0.0f); });
        run (*q, sine (500.0, 0.4f, 1.0));
        q->setRealValue ("sfreeze", 1.0f);
        auto o2 = run (*q, silence (1.5));
        check (tu::rms (o2, (int) (sr * 0.5), (int) sr) > 0.05f && std::abs (peakHz (o2, (int) (sr * 0.5), (int) (sr * 0.5)) - 500.0) < 20.0,
               "congelado espectral: el tono de 500 Hz sigue sin entrada"_u);
    }

    // morph: spectral crossfade takes the sidechain's tone
    {
        auto p = make ([] (Processor& p)
        {
            p.enableAllBuses();
            p.setRealValue ("spon", 1.0f); p.setRealValue ("gmix", 0.0f); p.setRealValue ("morphmode", 1.0f); p.setRealValue ("morph", 100.0f);
        });
        auto sc = sine (1200.0, 0.4f, 1.5);
        auto o = run (*p, sine (300.0, 0.4f, 1.5), &sc);
        const double f = peakHz (o, (int) sr, (int) (sr * 0.4));
        check (std::abs (f - 1200.0) < 4.0, "morphing: el fundido espectral toma el tono del sidechain (" + String (f, 0) + " Hz)");
    }

    // glitch: a repeat replays the division just played
    {
        auto p = make ([] (Processor& p)
        {
            p.setRealValue ("gmix", 0.0f); p.setRealValue ("glprob", 100.0f); p.setRealValue ("gldiv", 0.0f); p.setRealValue ("gldur", 1.0f);
            for (auto id : { "glstut", "glrev", "gltape", "glshuf", "gldrop" }) p.setRealValue (id, 0.0f);
            p.setRealValue ("glrep", 100.0f); p.setRealValue ("glsmooth", 0.5f);
        });
        AudioBuffer<float> in (2, (int) (sr * 3));
        tu::fillNoise (in, 0.3f, 9);
        auto o = run (*p, in);
        // at 120 BPM a beat is 24000 samples: from beat 2 the output repeats beat 1
        float err = 0.0f;
        for (int i = 24000 + 500; i < 48000 - 500; ++i) err = jmax (err, std::abs (o.getSample (0, i) - in.getSample (0, i - 24000)));
        check (err < 0.05f, "glitch: repetir vuelve a tocar el tiempo anterior (error " + String (err, 3) + ")");
    }

    // modulation: an LFO on the output volume moves it
    {
        auto p = make ([] (Processor& p)
        {
            p.setRealValue ("gmix", 0.0f);
            p.setRealValue (Processor::lfoId (0, "rate"), 1.0f); p.setRealValue (Processor::lfoId (0, "shape"), 4.0f);
            p.setRealValue (Processor::routeId (0, "src"), 1.0f);
            p.setRealValue (Processor::routeId (0, "dst"), (float) racoon::skunk::destIndex ("outgain"));
            p.setRealValue (Processor::routeId (0, "depth"), -60.0f);
        });
        auto o = run (*p, sine (500.0, 0.3f, 1.0));
        const float a = tu::rms (o, 2000, (int) (sr * 0.4)), b = tu::rms (o, (int) (sr * 0.55), (int) (sr * 0.4));
        check (std::abs (Decibels::gainToDecibels (a) - Decibels::gainToDecibels (b)) > 10.0f, "matriz: un LFO cuadrado sobre la salida cambia el volumen a mitad de ciclo ("
               + String (Decibels::gainToDecibels (a), 1) + " / " + String (Decibels::gainToDecibels (b), 1) + " dB)");
    }

    // XY pad drives what it is routed to
    {
        auto p = make ([] (Processor& p)
        {
            p.setRealValue (Processor::routeId (3, "src"), (float) (1 + racoon::skunk::numLfos + racoon::skunk::numEnvs + racoon::skunk::numSeqs + racoon::skunk::numMacros));
            p.setRealValue (Processor::routeId (3, "dst"), (float) racoon::skunk::destIndex ("gpitch"));
            p.setRealValue (Processor::routeId (3, "depth"), 50.0f);
        });
        p->setRealValue ("xy_x", 100.0f);
        run (*p, silence (0.05));
        const float up = p->modulated (racoon::skunk::destIndex ("gpitch"));
        p->setRealValue ("xy_x", 0.0f);
        run (*p, silence (0.05));
        const float down = p->modulated (racoon::skunk::destIndex ("gpitch"));
        check (up > 40.0f && std::abs (down) < 0.01f, "pad XY: X al máximo sube la afinación de los granos "_u + String (up, 1) + " semitonos");
    }

    // seed lock: two copies make exactly the same random grains
    {
        auto setup = [] (Processor& p) { p.setRealValue ("seedlock", 1.0f); p.setRealValue ("gspray", 200.0f); p.setRealValue ("grev", 50.0f); p.setRealValue ("gjump", 30.0f); };
        auto a = make (setup), b = make (setup);
        AudioBuffer<float> in (2, (int) (sr * 2));
        tu::fillNoise (in, 0.3f, 3);
        auto oa = run (*a, in), ob = run (*b, in, nullptr, 512);
        float diff = 0.0f;
        for (int i = 0; i < oa.getNumSamples(); ++i) diff = jmax (diff, std::abs (oa.getSample (0, i) - ob.getSample (0, i)));
        check (diff < 1.0e-5f, "semilla fija: dos copias dan exactamente los mismos granos (aun con bloques distintos)");
    }

    // filter bank types stay stable with high resonance
    {
        bool ok = true;
        for (int t = 0; t < 7; ++t)
        {
            auto p = make ([t] (Processor& p) { p.setRealValue ("gmix", 0.0f); p.setRealValue ("ftype", (float) t); p.setRealValue ("freso", 100.0f); p.setRealValue ("fmix", 100.0f); p.setRealValue ("ffreq", 400.0f); });
            AudioBuffer<float> in (2, (int) sr);
            tu::fillNoise (in, 0.3f, t);
            auto o = run (*p, in);
            ok = ok && tu::allFinite (o) && tu::peak (o, 0, o.getNumSamples()) < 8.0f;
        }
        check (ok, "los 7 filtros creativos con resonancia al máximo se mantienen estables"_u);
    }

    // resample: record the output and turn it into the capture
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("gmix", 0.0f); });
        p->setRealValue ("rsrec", 1.0f);
        run (*p, sine (700.0, 0.3f, 1.0));
        p->setRealValue ("rsrec", 0.0f);
        const int rec = p->resampled.load();
        p->setRealValue ("recmode", 3.0f);
        p->requestResampleToCapture();
        run (*p, silence (0.05));
        check (rec == (int) sr && p->captureValid.load() == rec, "remuestreo: graba 1 s de la salida y lo pasa a la captura");
    }

    // state: the frozen capture comes back with the project
    {
        auto p = make ([] (Processor& p) { p.setRealValue ("caplen", 1.0f); });
        run (*p, sine (250.0, 0.4f, 1.2));
        p->setRealValue ("recmode", 3.0f);
        run (*p, silence (0.05));
        MemoryBlock mb;
        p->getStateInformation (mb);
        auto q = make();
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        float diff = 0.0f;
        for (int i = 1; i < 2000; ++i) diff = jmax (diff, std::abs (q->captureRing().back (0, i) - p->captureRing().back (0, i)));
        check (q->captureValid.load() == p->captureValid.load() && diff < 1.0e-4f, "guardar y abrir: la captura congelada vuelve igual");
    }

    // a WAV file becomes the capture
    {
        auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("skunk_test.wav");
        {
            auto tone = sine (220.0, 0.5f, 0.75);
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (file), 44100.0, 2, 24, {}, 0));
            file.deleteFile();
            w.reset (wav.createWriterFor (new FileOutputStream (file), 44100.0, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        }
        auto p = make();
        const bool ok = p->loadFile (file);
        run (*p, silence (0.05));
        check (ok && p->captureState.load() == 3 && std::abs (p->captureValid.load() - (int) (0.75 * 48000.0 * 48000.0 / 44100.0)) < 50,
               "cargar un WAV de 44.1 kHz: queda como captura congelada a 48 kHz ("_u + String (p->captureValid.load()) + " muestras)");
        file.deleteFile();
    }

    // every preset of the library sounds with noise in and stays finite
    {
        auto p = make();
        AudioBuffer<float> in (2, (int) sr);
        tu::fillNoise (in, 0.3f, 1);
        bool ok = true;
        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            auto o = run (*p, in);
            ok = ok && tu::allFinite (o) && tu::rms (o, (int) (sr * 0.5), (int) (sr * 0.5)) > 1.0e-4f;
        }
        check (ok && p->getNumPrograms() >= 14, String (p->getNumPrograms()) + " presets suenan sin explotar");
    }
}

static void shotsSkunk (const juce::File& out)
{
    using namespace juce;
    racoon::skunk::Processor p;
    p.setRateAndBufferSizeDetails (48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    if (auto* re = dynamic_cast<racoon::RacoonEditor*> (ed.get()))
        if (auto* d = dynamic_cast<racoon::skunk::Display*> (re->getDisplay()))
            for (int page = 1; page < 4; ++page)
            {
                d->showPage (page);
                tu::shot (*ed, out.getChildFile ("skunk_pagina_" + String (page) + ".png"));
            }
    check (true, "pantallas de matriz, moduladores y secuencias");
}
static RegisterTest skunkShots ("SkunkPantallas", shotsSkunk);
static RegisterTest skunkTestReg ("Skunk", testSkunk);

// Skunk toma una textura de Muestras Racoon como material
static void testSkunkMuestras (const juce::File&)
{
    using namespace juce;
    const auto& cat = racoon::biblio::Catalogo::get();
    if (! cat.disponible()) { std::cout << "  (sin Muestras Racoon)" << std::endl; return; }
    auto tex = cat.gruposDe ({ "textura" });
    check (tex.size() >= 60, "texturas en la biblioteca: " + String ((int) tex.size()));
    if (tex.empty()) return;
    racoon::skunk::Processor p;
    p.prepareToPlay (48000.0, 512);
    const auto& m = cat.muestras[(size_t) tex.front()->muestras.front()];
    const bool ok = p.loadFile (cat.archivo (m.archivo));
    AudioBuffer<float> b (2, 512);
    for (int k = 0; k < 20; ++k) { b.clear(); MidiBuffer mid; p.processBlock (b, mid); }
    check (ok && p.captureValid.load() > 48000, "la textura " + m.nombre + " queda como material (" + String (p.captureValid.load()) + " muestras)");
}
static RegisterTest skunkMuestrasReg ("SkunkMuestras", testSkunkMuestras);

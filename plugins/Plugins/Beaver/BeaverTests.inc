// Beaver checks (included by Tests/FamilyTest.cpp)
#include "BeaverProcessor.h"

namespace beaver_test
{
using namespace juce;
inline double hz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
// power spectrum with a 4-term Blackman-Harris window (side lobes at -92 dB), 2^order points
inline std::vector<double> bhSpectrum (const AudioBuffer<float>& b, int start, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> d ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = MathConstants<double>::twoPi * i / (n - 1);
        d[(size_t) i] = b.getSample (0, start + i) * (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
    }
    fft.performFrequencyOnlyForwardTransform (d.data());
    std::vector<double> p ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) p[(size_t) i] = (double) d[(size_t) i] * d[(size_t) i];
    return p;
}
inline double lvDb (const AudioBuffer<float>& o, int start, int len, double f, double sr) { return 20.0 * std::log10 (tu::tone (o, 0, start, len, f, sr) + 1.0e-12); }
}

static void testBeaver (const juce::File& out)
{
    using namespace juce;
    using namespace beaver_test;
    using racoon::beaver::Processor;
    namespace bv = racoon::beaver;
    commonChecks ("Beaver", [] { return new Processor(); }, out);
    std::cout << "aditivo" << std::endl;
    const double sr = 48000.0;

    // no colour: A Sierra, B silent, no tilt, open filter, one oscillator, no room
    auto plain = [] (Processor& p)
    {
        p.setRealValue ("a_fuente", 0); p.setRealValue ("a_forma", 0); p.setRealValue ("a_pos", 0); p.setRealValue ("b_nivel", 0); p.setRealValue ("morph", 0);
        p.setRealValue ("inclinacion", 0); p.setRealValue ("f1_tipo", 0); p.setRealValue ("f1_corte", 20000); p.setRealValue ("f1_res", 0); p.setRealValue ("f1_teclado", 0);
        p.setRealValue ("f1_env", 0); p.setRealValue ("f2_on", 0); p.setRealValue ("armonizador", 0); p.setRealValue ("pares", 0); p.setRealValue ("barrido", 0);
        p.setRealValue ("blurt", 0); p.setRealValue ("blurh", 0); p.setRealValue ("pulsar", 0); p.setRealValue ("unison", 1); p.setRealValue ("estereo", 0);
        p.setRealValue ("espacio", 0); p.setRealValue ("velamp", 0); p.setRealValue ("velcorte", 0); p.setRealValue ("calidad", 2); p.setRealValue ("armonicos", 511);
        for (int k = 0; k < bv::numSlots; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0);
        p.setRealValue ("adsr_a", 1); p.setRealValue ("adsr_d", 100); p.setRealValue ("adsr_s", 100); p.setRealValue ("adsr_r", 500);
    };
    auto run = [&] (std::function<void (Processor&)> setup, MidiBuffer& midi, int len, Processor* keep = nullptr)
    {
        std::unique_ptr<Processor> own;
        Processor* p = keep;
        if (p == nullptr) { own = std::make_unique<Processor>(); p = own.get(); }
        p->setRateAndBufferSizeDetails (sr, 256);
        p->prepareToPlay (sr, 256);
        setup (*p);
        p->syncMaterials (true);
        AudioBuffer<float> in (2, len); in.clear();
        return tu::render (*p, in, 256, &midi);
    };
    auto note = [] (int n, int vel = 100, int off = -1)
    {
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, n, (uint8) vel), 0);
        if (off > 0) m.addEvent (MidiMessage::noteOff (1, n), off);
        return m;
    };

    // an exact saw: harmonic k sits at -20 log10 k
    {
        auto m = note (45);
        auto o = run (plain, m, (int) sr);
        const double f = hz (45), ref = lvDb (o, 4800, 24000, f, sr);
        double worst = 0.0; int at = 0;
        for (int k = 1; k <= 40; ++k)
        {
            const double e = std::abs (lvDb (o, 4800, 24000, f * k, sr) - ref + 20.0 * std::log10 ((double) k));
            if (e > worst) { worst = e; at = k; }
        }
        check (worst < 0.5, "Sierra exacta: los 40 primeros armónicos a -20 log k (peor " + String (worst, 2) + " dB en el " + String (at) + ")");
    }

    // no aliasing on C7, and none while gliding up four octaves
    auto outside = [&] (const AudioBuffer<float>& o, int start, double f0)
    {
        auto sp = bhSpectrum (o, start, 15);
        double inH = 0.0, outH = 0.0;
        for (size_t i = 1; i < sp.size(); ++i)
        {
            const double fr = (double) i * sr / 32768.0;
            const double k = fr / f0, dist = std::abs (k - std::round (k)) * f0;
            if (dist < 12.0) inH += sp[i]; else outH += sp[i];
        }
        return 10.0 * std::log10 (outH / jmax (1.0e-30, inH) + 1.0e-30);
    };
    {
        auto m = note (96);
        auto o = run (plain, m, (int) (sr * 1.0));
        const double st = outside (o, 8000, hz (96));
        MidiBuffer g;
        g.addEvent (MidiMessage::noteOn (1, 36, (uint8) 100), 0);
        g.addEvent (MidiMessage::noteOn (1, 96, (uint8) 100), 4800);
        auto o2 = run ([&] (Processor& p) { plain (p); p.setRealValue ("voices", 1); p.setRealValue ("glide", 2000); }, g, (int) (sr * 3.0));
        // during the glide the pitch is not fixed: compare the energy above 0.45 sr with the whole
        auto sp = tu::spectrum (o2, 0, (int) (sr * 1.5), 14);
        double hi = 0.0, all = 0.0;
        for (size_t i = 1; i < sp.size(); ++i) { all += sp[i]; if ((double) i * sr / 16384.0 > sr * 0.46) hi += sp[i]; }
        const double gl = 10.0 * std::log10 (hi / all + 1.0e-30);
        check (st < -80.0 && gl < -60.0, "sin aliasing: Do 7 deja " + String (st, 1) + " dB fuera de sus armónicos; en el glide, " + String (gl, 1) + " dB arriba del límite");
    }

    // the filter acts on each partial exactly
    {
        auto m = note (33);
        auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("f1_corte", 1000); p.setRealValue ("f1_pend", 24); }, m, (int) sr);
        bv::FilterCurve fc; fc.type = 0; fc.fc = 1000.0f; fc.slope = 24.0f;
        const double f = hz (33), ref = lvDb (o, 4800, 32000, f, sr);
        double worst = 0.0;
        for (int k = 1; k <= 60; ++k)
        {
            const double want = -20.0 * std::log10 ((double) k) + fc.db ((float) (f * k)) - fc.db ((float) f);
            if (want < -70.0) continue;
            worst = jmax (worst, std::abs (lvDb (o, 4800, 32000, f * k, sr) - ref - want));
        }
        auto o2 = run ([&] (Processor& p) { plain (p); p.setRealValue ("f1_tipo", 6); p.setRealValue ("f1_corte", 1000); }, m, (int) sr);
        const double r2 = lvDb (o2, 4800, 32000, f, sr);
        double above = -200.0;
        for (int k = 20; k <= 60; ++k) above = jmax (above, lvDb (o2, 4800, 32000, f * k, sr) - r2);
        check (worst < 0.5 && above < -90.0, "filtro por armónico: paso bajo a 1 kHz igual a su curva (peor " + String (worst, 2) + " dB); Escalón deja " + String (above, 0) + " dB arriba");
    }

    // a fast cutoff sweep does not click
    {
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        p.setRealValue ("f1_corte", 2000);
        AudioBuffer<float> io (2, 256), all (2, (int) sr);
        for (int b = 0; b < (int) sr / 256; ++b)
        {
            MidiBuffer m;
            if (b == 0) m.addEvent (MidiMessage::noteOn (1, 45, (uint8) 100), 0);
            const double t = b * 256.0 / sr;
            if (t > 0.5 && t < 0.6) p.setRealValue ("f1_corte", (float) (t < 0.55 ? 200.0 * std::pow (40.0, (t - 0.5) / 0.05) : 8000.0 * std::pow (1.0 / 40.0, (t - 0.55) / 0.05)));
            io.clear();
            p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, b * 256, io, c, 0, 256);
        }
        // against the note held at the sweep's brightest point
        auto m8 = note (45);
        auto bright = run ([&] (Processor& q) { plain (q); q.setRealValue ("f1_corte", 8000); }, m8, (int) sr);
        AudioBuffer<float> still (2, 4800), sweep (2, 4800);
        for (int c = 0; c < 2; ++c) { still.copyFrom (c, 0, bright, c, (int) (sr * 0.3), 4800); sweep.copyFrom (c, 0, all, c, (int) (sr * 0.5), 4800); }
        check (tu::maxJump (sweep) <= tu::maxJump (still) * 1.1f, "barrer el corte de 200 a 8000 Hz en 50 ms no truena (salto "_u + String (tu::maxJump (sweep), 4) + " contra " + String (tu::maxJump (still), 4) + ")");
    }

    // even and odd partials
    {
        auto m = note (45);
        const double f = hz (45);
        auto ev = run ([&] (Processor& p) { plain (p); p.setRealValue ("pares", -100); }, m, (int) sr);
        auto od = run ([&] (Processor& p) { plain (p); p.setRealValue ("pares", 100); }, m, (int) sr);
        const double r1 = lvDb (ev, 4800, 24000, f, sr), r2 = lvDb (od, 4800, 24000, f, sr);
        double evMax = -200.0, odMax = -200.0;
        for (int k = 2; k <= 20; ++k)
        {
            if (k % 2 == 0) evMax = jmax (evMax, lvDb (ev, 4800, 24000, f * k, sr) - r1);
            else odMax = jmax (odMax, lvDb (od, 4800, 24000, f * k, sr) - r2);
        }
        check (evMax < -90.0 && odMax < -90.0, "Pares / impares: -100 quita los pares (" + String (evMax, 0) + " dB), +100 los impares (" + String (odMax, 0) + " dB)");
    }

    // spectral morph: the geometric mean of the two
    {
        auto m = note (45);
        const double f = hz (45);
        auto ab = run ([&] (Processor& p) { plain (p); p.setRealValue ("b_nivel", 100); p.setRealValue ("b_fuente", 0); p.setRealValue ("b_forma", 2); p.setRealValue ("b_pos", 0);
                                            p.setRealValue ("morph", 50); p.setRealValue ("morphmodo", 1); }, m, (int) sr);
        const double r = lvDb (ab, 4800, 24000, f, sr);
        double worst = 0.0;
        for (int k : { 3, 5, 7, 9 })
        {
            const double a = -20.0 * std::log10 ((double) k), b = -40.0 * std::log10 ((double) k);   // saw and triangle (odd)
            worst = jmax (worst, std::abs (lvDb (ab, 4800, 24000, f * k, sr) - r - 0.5 * (a + b)));
        }
        check (worst < 0.5, "Mezcla espectral al 50 %: cada armónico en la media de A y B en dB (peor "_u + String (worst, 2) + ")");
    }

    // unison keeps the level
    {
        auto m = note (45);
        auto one = run ([&] (Processor& p) { plain (p); }, m, (int) sr);
        auto eight = run ([&] (Processor& p) { plain (p); p.setRealValue ("unison", 8); p.setRealValue ("detune", 30); }, m, (int) sr);
        const double d = Decibels::gainToDecibels (tu::rms (eight, 4800, 40000) / tu::rms (one, 4800, 40000));
        check (std::abs (d) < 2.0, "unison 8 suena al mismo nivel que 1 (" + String (d, 2) + " dB)");
    }

    // resynthesis: 20 known harmonics with vibrato come back
    {
        const int n = (int) (sr * 3.0);
        AudioBuffer<float> src (1, n);
        src.clear();
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double f = 110.0 * std::pow (2.0, 20.0 * std::sin (MathConstants<double>::twoPi * 5.0 * i / sr) / 1200.0);
            ph += f / sr;
            double s = 0.0;
            for (int k = 1; k <= 20; ++k) if (k != 5) s += std::pow ((double) k, -0.7) * std::sin (MathConstants<double>::twoPi * k * ph);
            src.setSample (0, i, (float) (0.12 * s));
        }
        auto fs = bv::analyse (src, {}, "prueba");
        double pitchErr = 0.0; int voiced = 0;
        for (int fr = 20; fr < fs->frames - 20; ++fr)
        {
            const double t = fr * fs->frameSeconds;
            const double truth = 110.0 * std::pow (2.0, 20.0 * std::sin (MathConstants<double>::twoPi * 5.0 * t) / 1200.0);
            if (fs->f0[(size_t) fr] > 0.0f) { ++voiced; pitchErr = jmax (pitchErr, std::abs (1200.0 * std::log2 (fs->f0[(size_t) fr] / truth))); }
        }
        double lvl = 0.0;
        const int mid = fs->frames / 2;
        const double ref = fs->levelDb (mid, 0);
        for (int k = 1; k <= 20; ++k)
            if (k != 5) lvl = jmax (lvl, std::abs (fs->levelDb (mid, k - 1) - ref + 0.7 * 20.0 * std::log10 ((double) k)));
        const double notch = fs->levelDb (mid, 4) - ref;
        check (voiced > fs->frames / 2 && pitchErr < 3.0 && std::abs (fs->refHz - 110.0f) < 0.5f && lvl < 1.0 && notch < -30.0,
               "resíntesis: afinación seguida a "_u + String (pitchErr, 2) + " cents, referencia " + String (fs->refHz, 1) + " Hz, armónicos a " + String (lvl, 2)
               + " dB, el hueco del 5 a " + String (notch, 0) + " dB");
        // and through a file, played back on La 2 with its own pitch movement
        const File tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("beaver_prueba.wav");
        tmp.deleteFile();
        {
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> os (tmp.createOutputStream());
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 1, 24, {}, 0));
            if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (src, 0, n); }
        }
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        const bool loaded = p.loadOwn (0, tmp, true);
        p.setRealValue ("a_seguirf0", 0); p.setRealValue ("a_ruido", 0);
        auto m = note (45);
        auto o = run ([] (Processor&) {}, m, (int) (sr * 1.5), &p);
        const double f = 110.0;
        const double r1 = lvDb (o, 24000, 24000, f, sr);
        double pl = 0.0;
        for (int k : { 2, 3, 4, 6, 8 }) pl = jmax (pl, std::abs (lvDb (o, 24000, 24000, f * k, sr) - r1 + 0.7 * 20.0 * std::log10 ((double) k)));
        tmp.deleteFile();
        check (loaded && pl < 2.0, "el mismo audio cargado como archivo y tocado en La 2: sus armónicos regresan a "_u + String (pl, 2) + " dB");
    }

    // a picture: a bright row at partial 10
    {
        Image img (Image::RGB, 64, bv::numPartials, true);
        for (int x = 0; x < 64; ++x) img.setPixelAt (x, bv::numPartials - 10, Colours::white);
        const File tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("beaver_prueba.png");
        tmp.deleteFile();
        { FileOutputStream os (tmp); PNGImageFormat().writeImageToStream (img, os); }
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        p.setRealValue ("imgeje", 0);
        p.loadOwn (0, tmp, true);
        auto m = note (33);
        auto o = run ([] (Processor&) {}, m, (int) sr, &p);
        tmp.deleteFile();
        const double f = hz (33), r10 = lvDb (o, 4800, 24000, f * 10, sr);
        double other = -200.0;
        for (int k = 1; k <= 30; ++k) if (k != 10) other = jmax (other, lvDb (o, 4800, 24000, f * k, sr));
        check (r10 - other >= 30.0, "imagen: la fila brillante suena en el armónico 10, " + String (r10 - other, 1) + " dB arriba de los demás");
    }

    // pluck: the high partials die first
    {
        auto m = note (40);
        auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("pulsar", 80); p.setRealValue ("pulsarcaida", 2); }, m, (int) (sr * 2.0));
        const double f = hz (40);
        auto drop = [&] (int k) { return lvDb (o, 2400, 4800, f * k, sr) - lvDb (o, (int) (sr * 0.5), 4800, f * k, sr); };
        const double d1 = drop (1), d16 = drop (16);
        check (d16 > d1 * 3.0, "Pulsado: en medio segundo el armónico 16 cae "_u + String (d16, 1) + " dB y el grave " + String (d1, 1));
    }

    // time blur: with the tail only, the fall is slow and the rise immediate
    {
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        p.setRealValue ("blurt", 50); p.setRealValue ("blurcola", 1);
        AudioBuffer<float> io (2, 256), all (2, (int) (sr * 2.0));
        for (int b = 0; b < (int) (sr * 2.0) / 256; ++b)
        {
            MidiBuffer m;
            if (b == 0) m.addEvent (MidiMessage::noteOn (1, 45, (uint8) 100), 0);
            if (b == (int) (sr * 0.5) / 256) p.setRealValue ("a_nivel", 0);
            io.clear();
            p.processBlock (io, m);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, b * 256, io, c, 0, 256);
        }
        const double f = hz (45);
        const double before = tu::tone (all, 0, (int) (sr * 0.3), 4800, f, sr), after = tu::tone (all, 0, (int) (sr * 1.45), 4800, f, sr);
        const double rise = tu::tone (all, 0, 600, 1200, f, sr);
        const double ratio = after / before;
        check (std::abs (ratio - 0.368) < 0.06 && rise > before * 0.8, "Difuminar solo la cola: a 1 s queda "_u + String (ratio * 100.0, 0) + " % (37 % esperado), y entra de inmediato");
    }

    // state: an own material comes back byte for byte
    {
        auto fs = bv::builtInMaterial (1);
        auto mb = fs->save();
        auto back = bv::FrameSet::load (mb);
        check (back != nullptr && back->frames == fs->frames && back->level == fs->level && back->noise == fs->noise, "material guardado y abierto igual ("
               + String ((int) mb.getSize() / 1024) + " KB comprimido)");
    }

    // materials build and sound
    {
        const auto t0 = Time::getMillisecondCounterHiRes();
        String quiet;
        for (int i = 1; i < bv::numMaterials; ++i) bv::builtInMaterial (i);
        const double secs = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
        for (int i = 1; i < bv::numMaterials; ++i)
        {
            auto m = note (45);
            auto o = run ([&] (Processor& p) { plain (p); p.setRealValue ("a_fuente", i >= 7 ? 3.0f : 2.0f); p.setRealValue ("a_material", (float) i);
                                                   p.setRealValue ("a_conservar", i <= 2 ? 1.0f : 0.0f); }, m, (int) sr);
            if (tu::rms (o, 4800, 40000) < 0.01f) quiet << Processor::materialNames()[i] << " (" << String (tu::rms (o, 4800, 40000), 4) << ") ";
        }
        check (secs < 10.0 && quiet.isEmpty(), "8 materiales de fábrica hechos en " + String (secs, 1) + " s" + (quiet.isEmpty() ? String (", todos suenan") : ", callados: " + quiet));
    }

    // cost
    {
        auto cost = [&] (int quality, int voices)
        {
            Processor p;
            p.setRateAndBufferSizeDetails (sr, 256);
            p.prepareToPlay (sr, 256);
            p.setRealValue ("calidad", (float) quality);
            p.setRealValue ("espacio", 0);
            MidiBuffer m;
            for (int k = 0; k < voices; ++k) m.addEvent (MidiMessage::noteOn (1, 36 + k * 3, (uint8) 100), k * 16);
            AudioBuffer<float> in (2, (int) (sr * 4.0)); in.clear();
            const auto t0 = Time::getMillisecondCounterHiRes();
            tu::render (p, in, 256, &m);
            return (Time::getMillisecondCounterHiRes() - t0) * 0.001 / 4.0 * 100.0;
        };
        const double normal = cost (1, 12), eco = cost (0, 12);
        check (normal < 30.0 && eco < 15.0, "12 voces con unison 3: NORMAL " + String (roundToInt (normal)) + " % de un núcleo, ECO "_u + String (roundToInt (eco)) + " %");
    }

    // every preset is dark
    {
        Processor probe;
        const auto presets = probe.factoryPresets();
        String bright, bad; double worst = 0.0;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            Processor p;
            p.setRateAndBufferSizeDetails (sr, 256);
            p.prepareToPlay (sr, 256);
            p.applyFactoryPreset (i);
            p.syncMaterials (true);
            auto m = note (45, 100, (int) (sr * 1.5));
            AudioBuffer<float> in (2, (int) (sr * 1.5)); in.clear();
            auto o = tu::render (p, in, 256, &m);
            auto sp = tu::spectrum (o, 0, 12000, 15);
            double num = 0.0, den = 0.0;
            for (size_t k = 1; k < sp.size(); ++k) { num += k * sr / 32768.0 * sp[k]; den += sp[k]; }
            const double c = den > 0.0 ? num / den : 0.0;
            if (c > worst) { worst = c; bright = presets[(size_t) i].name + " " + String (c, 0) + " Hz"; }
            if (! tu::allFinite (o) || tu::rms (o, 0, o.getNumSamples()) < 1.0e-3f || tu::peak (o, 0, o.getNumSamples()) > 1.5f || p.value ("armonicos") > 255.0f)
                bad << presets[(size_t) i].name << " (rms " << String (tu::rms (o, 0, o.getNumSamples()), 4) << ", pico " << String (tu::peak (o, 0, o.getNumSamples()), 2) << ") ";
        }
        check (worst < 1200.0 && bad.isEmpty(), String ((int) presets.size()) + " presets oscuros (el más brillante: " + bright + ")" + (bad.isEmpty() ? String() : ", fallan: " + bad));
    }

    // silence is exact after the release
    {
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        p.setRealValue ("espacio", 30);
        auto m = note (45, 100, 4800);
        AudioBuffer<float> in (2, (int) (sr * 12.0)); in.clear();
        auto o = tu::render (p, in, 256, &m);
        check (tu::peak (o, (int) (sr * 11.0), (int) sr) == 0.0f && p.activeVoices.load() == 0, "después de la relajación y la sala el silencio es cero exacto");
    }

    // Aleatoria: a pad, or a bass with one voice
    {
        Processor p;
        const auto a = p.aleatoriaRol();
        p.setRealValue ("voices", 1);
        check (a == "pad" && p.aleatoriaRol() == "bajo" && racoon::aleatorio::rolDe ("Beaver") == racoon::aleatorio::Rol::pad, "Aleatoria: Beaver escribe pad, y bajo con una voz");
    }
}

static RegisterTest regBeaver ("Beaver", testBeaver);



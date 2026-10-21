// Hedgehog checks (included by Tests/FamilyTest.cpp)
#include "HedgehogProcessor.h"
#include "HedgehogEditor.h"

namespace hedgehog_test
{
using namespace juce;
using namespace racoon::hedgehog;
constexpr double sr = 48000.0;

static std::unique_ptr<Processor> make (std::function<void (Processor&)> setup = {})
{
    auto p = std::make_unique<Processor>();
    p->setRateAndBufferSizeDetails (sr, 512);
    p->prepareToPlay (sr, 512);
    p->setRealValue ("pa_on", 0.0f);         // the lab checks measure each tool alone; the Partícula has its own checks
    if (setup) setup (*p);
    p->prepareToPlay (sr, 512);
    return p;
}

static AudioBuffer<float> run (Processor& p, const AudioBuffer<float>& in, int block = 512)
{
    AudioBuffer<float> out (2, in.getNumSamples()), io (2, block);
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        io.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, n);
        MidiBuffer m;
        p.processBlock (io, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

static AudioBuffer<float> sine (double hz, float amp, double seconds, double phase = 0.0)
{
    AudioBuffer<float> b (2, (int) (seconds * sr));
    tu::fillSine (b, hz, sr, amp, phase);
    return b;
}

// pink-ish noise (three one-pole filtered whites) at a level
static AudioBuffer<float> pink (float amp, double seconds, int seed = 3)
{
    AudioBuffer<float> b (2, (int) (seconds * sr));
    Random r (seed);
    for (int c = 0; c < 2; ++c)
    {
        float s1 = 0, s2 = 0, s3 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float w = r.nextFloat() * 2.0f - 1.0f;
            s1 = 0.997f * s1 + 0.029f * w; s2 = 0.985f * s2 + 0.032f * w; s3 = 0.95f * s3 + 0.048f * w;
            b.setSample (c, i, (s1 + s2 + s3 + w * 0.02f) * amp * 4.0f);
        }
    }
    return b;
}

static float tone (const AudioBuffer<float>& b, int ch, double from, double hz) { return tu::tone (b, ch, (int) (from * sr), 16384, hz, sr); }
}

static void testHedgehog (const juce::File& out)
{
    using namespace juce;
    using namespace hedgehog_test;
    commonChecks ("Hedgehog", [] { return new Processor(); }, out);
    std::cout << "mastering" << std::endl;

    // loudness: a 1 kHz sine at -20 dBFS on both channels reads -20 LUFS; a full-scale sine in one channel reads -3.01
    {
        Loudness a, b;
        a.prepare (sr); b.prepare (sr);
        auto s = sine (1000.0, 0.1f, 6.0);
        a.process (s.getReadPointer (0), s.getReadPointer (1), s.getNumSamples());
        auto full = sine (1000.0, 1.0f, 6.0);
        AudioBuffer<float> silent (1, full.getNumSamples()); silent.clear();
        b.process (full.getReadPointer (0), silent.getReadPointer (0), full.getNumSamples());
        check (std::abs (a.integrated() + 20.0f) < 0.15f && std::abs (b.integrated() + 3.01f) < 0.15f,
               "LUFS: -20 dBFS da " + String (a.integrated(), 2) + " y 0 dBFS en un canal da " + String (b.integrated(), 2));
    }

    // LRA of two levels 10 dB apart
    {
        Loudness m;
        m.prepare (sr);
        for (int k = 0; k < 6; ++k)
        {
            auto seg = pink (k % 2 == 0 ? 0.1f : 0.0316f, 10.0, k);
            m.process (seg.getReadPointer (0), seg.getReadPointer (1), seg.getNumSamples());
        }
        check (std::abs (m.range() - 10.0f) < 1.5f, "LRA de dos niveles a 10 dB: " + String (m.range(), 1) + " LU");
    }

    // inter-sample peaks: a sine at fs/4 shifted 45 degrees peaks at 0.707 in samples and 1.0 between them
    {
        TruePeak t;
        auto s = sine (sr / 4.0, 1.0f, 0.1, MathConstants<double>::pi / 4.0);
        float tp = 0.0f, smp = 0.0f;
        for (int i = 0; i < s.getNumSamples(); ++i) { tp = jmax (tp, t.process (s.getSample (0, i))); smp = jmax (smp, std::abs (s.getSample (0, i))); }
        check (std::abs (racoon::dsp::gainToDb (tp)) < 0.3f && smp < 0.72f, "pico real: ve el pico entre muestras (" + String (racoon::dsp::gainToDb (tp), 2) + " dBTP contra "
               + String (racoon::dsp::gainToDb (smp), 2) + " dBFS)");
    }

    // latency is reported exactly and nothing else changes with every module off
    {
        auto p = make();
        AudioBuffer<float> imp (2, 8192);
        imp.clear();
        imp.setSample (0, 100, 0.5f); imp.setSample (1, 100, 0.5f);
        auto o = run (*p, imp);
        int at = 0; float best = 0.0f;
        for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > best) { best = std::abs (o.getSample (0, i)); at = i; }
        check (at - 100 == p->getLatencySamples() && std::abs (best - 0.5f) < 0.02f, "latencia informada " + String (p->getLatencySamples()) + " = medida " + String (at - 100));
    }

    // the limiter: loud material never passes the ceiling (true peak)
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("lim_gain", 14.0f); q.setRealValue ("ceiling", -1.0f); });
        auto in = pink (0.03f, 6.0);
        auto o = run (*p, in);
        TruePeak a, b;
        float tp = 0.0f;
        for (int i = (int) sr; i < o.getNumSamples(); ++i) tp = jmax (tp, a.process (o.getSample (0, i)), b.process (o.getSample (1, i)));
        check (racoon::dsp::gainToDb (tp) <= -0.8f && p->truePeakMax.load() <= -0.85f && tu::rms (o, (int) sr, (int) sr) > tu::rms (in, (int) sr, (int) sr) * 2.0f,
               "limitador: +14 dB y el pico real queda en " + String (racoon::dsp::gainToDb (tp), 2) + " dBTP (techo -1, medidor " + String (p->truePeakMax.load(), 2)
               + ", " + String (tu::rms (o, (int) sr, (int) sr) / tu::rms (in, (int) sr, (int) sr), 2) + "x)");
    }

    // dynamic EQ: it only cuts when the band is loud
    {
        auto level = [] (float amp, bool dynamic)
        {
            auto p = make ([dynamic] (Processor& q)
            {
                q.setRealValue (Processor::eqId (5, "on"), 1.0f); q.setRealValue (Processor::eqId (5, "freq"), 1000.0f); q.setRealValue (Processor::eqId (5, "gain"), 0.0f);
                q.setRealValue (Processor::eqId (5, "dyn"), dynamic ? 1.0f : 0.0f); q.setRealValue (Processor::eqId (5, "thr"), -30.0f); q.setRealValue (Processor::eqId (5, "range"), 12.0f);
                q.setRealValue ("lim_on", 0.0f);
            });
            auto o = run (*p, sine (1000.0, amp, 1.5));
            return tone (o, 0, 1.0, 1000.0) / amp;
        };
        const float loud = level (0.5f, true), quiet = level (0.005f, true), fixed = level (0.5f, false);
        check (racoon::dsp::gainToDb (loud) < -6.0f && std::abs (racoon::dsp::gainToDb (quiet)) < 1.0f && std::abs (racoon::dsp::gainToDb (fixed)) < 0.3f,
               "EQ dinámico: baja la banda fuerte ("_u + String (racoon::dsp::gainToDb (loud), 1) + " dB) y deja la suave (" + String (racoon::dsp::gainToDb (quiet), 1) + " dB)");
    }

    // multiband: with ratio 1 the bands add back flat; compressing the low band lowers only the lows
    {
        auto levels = [] (float lowRatio)
        {
            auto p = make ([lowRatio] (Processor& q)
            {
                q.setRealValue ("mb_on", 1.0f); q.setRealValue ("lim_on", 0.0f);
                for (int b = 0; b < numMbBands; ++b) { q.setRealValue (Processor::mbId (b, "ratio"), 1.0f); q.setRealValue (Processor::mbId (b, "thr"), -40.0f); }
                q.setRealValue (Processor::mbId (0, "ratio"), lowRatio);
            });
            AudioBuffer<float> in (2, (int) (sr * 1.5));
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                const float v = 0.2f * (float) (std::sin (MathConstants<double>::twoPi * 50.0 * i / sr) + std::sin (MathConstants<double>::twoPi * 1000.0 * i / sr)
                                                 + std::sin (MathConstants<double>::twoPi * 8000.0 * i / sr));
                in.setSample (0, i, v); in.setSample (1, i, v);
            }
            auto o = run (*p, in);
            return std::array<float, 3> { racoon::dsp::gainToDb (tone (o, 0, 1.0, 50.0) / 0.2f), racoon::dsp::gainToDb (tone (o, 0, 1.0, 1000.0) / 0.2f), racoon::dsp::gainToDb (tone (o, 0, 1.0, 8000.0) / 0.2f) };
        };
        const auto flat = levels (1.0f), comp = levels (8.0f);
        check (std::abs (flat[0]) < 0.5f && std::abs (flat[1]) < 0.5f && std::abs (flat[2]) < 0.5f && comp[0] < -6.0f && std::abs (comp[2]) < 0.5f,
               "multibanda: plana con ratio 1 (" + String (flat[0], 1) + ", " + String (flat[1], 1) + ", " + String (flat[2], 1) + " dB) y comprime solo los graves ("
               + String (comp[0], 1) + " dB)");
    }

    // resonance suppressor: a whistle in the noise goes down, the noise around it stays
    {
        auto measure = [] (bool on)
        {
            auto p = make ([on] (Processor& q) { q.setRealValue ("res_on", on ? 1.0f : 0.0f); q.setRealValue ("res_depth", 100.0f); q.setRealValue ("lim_on", 0.0f); });
            auto in = pink (0.05f, 3.0);
            for (int i = 0; i < in.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) in.setSample (c, i, in.getSample (c, i) + 0.1f * (float) std::sin (MathConstants<double>::twoPi * 3000.0 * i / sr));
            auto o = run (*p, in);
            return std::make_pair (tone (o, 0, 2.0, 3000.0), tone (o, 0, 2.0, 1500.0));
        };
        const auto off = measure (false), on = measure (true);
        const float cut = racoon::dsp::gainToDb (on.first / off.first), around = racoon::dsp::gainToDb (on.second / off.second);
        check (cut < -4.0f && around > -2.0f, "resonancias: el silbido baja " + String (cut, 1) + " dB, alrededor " + String (around, 1) + " dB");
    }

    // low end in mono: the side under 150 Hz disappears, the side at 2 kHz stays
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("lo_on", 1.0f); q.setRealValue ("lo_mono", 150.0f); q.setRealValue ("lim_on", 0.0f); });
        AudioBuffer<float> in (2, (int) (sr * 1.5));
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float lo = 0.3f * (float) std::sin (MathConstants<double>::twoPi * 50.0 * i / sr), hi = 0.3f * (float) std::sin (MathConstants<double>::twoPi * 2000.0 * i / sr);
            in.setSample (0, i, lo + hi); in.setSample (1, i, -lo - hi);
        }
        auto o = run (*p, in);
        check (tone (o, 0, 1.0, 50.0) < 0.3f * 0.1f && tone (o, 0, 1.0, 2000.0) > 0.3f * 0.85f, "graves en mono hasta 150 Hz; los lados de arriba se quedan");
    }

    // the assistant brings a quiet song to the platform's loudness
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("target", 0.0f); });
        auto in = pink (0.025f, 12.0);
        run (*p, in);
        const float before = p->integrated.load();
        const auto log = p->runAssistant();
        p->resetMeters();
        run (*p, in);
        const float after = p->integrated.load();
        check (std::abs (after + 14.0f) < 1.5f && p->value ("lim_gain") > 3.0f, "asistente: de " + String (before, 1) + " a " + String (after, 1) + " LUFS (destino -14). "
               + log[0]);
        bool tpOk = false;
        for (auto& c : p->qualityChecks()) if (c.text.startsWith ("Pico real")) tpOk = c.ok;
        check (tpOk, "control de calidad: pico real dentro del techo");
        auto rep = p->writeReport();
        check (rep.existsAsFile() && rep.loadFileAsString().contains ("LUFS"), "reporte escrito");
        rep.deleteFile();
    }

    // match EQ learns a curve; at zero it changes nothing
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("lim_on", 0.0f); });
        run (*p, pink (0.05f, 4.0));
        p->learnMatch();
        float maxAbs = 0.0f;
        for (int b = 0; b < numThirds; ++b) maxAbs = jmax (maxAbs, std::abs (p->matchDiff[b].load()));
        p->setRealValue ("match_on", 1.0f); p->setRealValue ("match_amt", 0.0f);
        auto in = sine (440.0, 0.2f, 1.0);
        auto o = run (*p, in);
        check (maxAbs > 0.5f && std::abs (racoon::dsp::gainToDb (tone (o, 0, 0.6, 440.0) / 0.2f)) < 0.2f, "match EQ aprende una curva (hasta " + String (maxAbs, 1) + " dB) y en 0 % no cambia nada");
    }

    // references: loudness measured, and played back at the master's loudness
    {
        auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("hedgehog_ref.wav");
        {
            f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), sr, 2, 24, {}, 0));
            auto s = sine (1000.0, 0.3f, 5.0);
            w->writeFromAudioSampleBuffer (s, 0, s.getNumSamples());
        }
        auto p = make ([] (Processor& q) { q.setRealValue ("lim_on", 0.0f); });
        const bool loaded = p->loadReference (0, f);
        const float refL = p->referenceLufs (0);
        run (*p, sine (1000.0, 0.1f, 4.0));
        p->setRealValue ("refplay", 1.0f);
        auto o = run (*p, sine (1000.0, 0.1f, 1.0));
        const float level = tone (o, 0, 0.5, 1000.0);
        check (loaded && std::abs (refL - (-10.46f)) < 0.3f && std::abs (racoon::dsp::gainToDb (level / 0.1f)) < 0.5f,
               "referencia: " + String (refL, 1) + " LUFS y suena al volumen del master (" + String (racoon::dsp::gainToDb (level / 0.1f), 2) + " dB)");
        f.deleteFile();
    }

    // versions A / B / C
    {
        auto p = make();
        p->setRealValue ("lim_gain", 3.0f);
        p->saveVersion (0);
        p->setRealValue ("lim_gain", 9.0f);
        p->saveVersion (1);
        p->recallVersion (0);
        const bool a = std::abs (p->value ("lim_gain") - 3.0f) < 0.01f;
        MemoryBlock st;
        p->getStateInformation (st);
        Processor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        q.recallVersion (1);
        check (a && q.hasVersion (1) && std::abs (q.value ("lim_gain") - 9.0f) < 0.01f, "versiones A, B y C: se guardan, vuelven y viajan con el proyecto");
    }

    // batch export: the file comes out the same length and in time (latency removed)
    {
        auto in = File::getSpecialLocation (File::tempDirectory).getChildFile ("hedgehog_in.wav");
        auto outF = File::getSpecialLocation (File::tempDirectory).getChildFile ("hedgehog_out.wav");
        const int len = (int) (sr * 2.0);
        {
            in.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (in), sr, 2, 24, {}, 0));
            AudioBuffer<float> b (2, len);
            b.clear();
            b.setSample (0, 1000, 0.5f); b.setSample (1, 1000, 0.5f);
            w->writeFromAudioSampleBuffer (b, 0, len);
        }
        auto p = make ([] (Processor& q) { q.setRealValue ("lim_on", 0.0f); });
        const bool ok = p->renderFile (in, outF);
        AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (outF));
        int at = -1;
        if (r != nullptr)
        {
            AudioBuffer<float> b (2, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            float best = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > best) { best = std::abs (b.getSample (0, i)); at = i; }
        }
        check (ok && r != nullptr && r->lengthInSamples == len && at == 1000, "exportar: mismo largo y en tiempo (golpe en " + String (at) + ")");
        in.deleteFile(); outF.deleteFile();
    }
}
static RegisterTest hedgehogTestReg ("Hedgehog", testHedgehog);

static void testParticle (const juce::File&)
{
    using namespace juce;
    using namespace hedgehog_test;
    std::cout << "particula" << std::endl;
    auto particle = [] (float amount, std::function<void (Processor&)> more = {})
    {
        return make ([amount, more] (Processor& q)
        {
            q.setRealValue ("pa_on", 1.0f); q.setRealValue ("pa_amount", amount); q.setRealValue ("lim_on", 0.0f);
            if (more) more (q);
        });
    };
    {
        // 0 %: the three bands add back to the same signal
        bool flat = true;
        String worst;
        for (double hz : { 60.0, 200.0, 1000.0, 3000.0, 9000.0 })
        {
            auto p = particle (0.0f);
            auto o = run (*p, sine (hz, 0.25f, 1.2));
            const float db = racoon::dsp::gainToDb (tone (o, 0, 0.6, hz) / 0.25f);
            if (std::abs (db) > 0.3f) { flat = false; worst += String (hz, 0) + " Hz " + String (db, 2) + " dB  "; }
        }
        check (flat, "Partícula en 0 %: las tres bandas suman plano " + worst);
    }
    {
        // 100 %: the bands compress a loud mix and the peaks come closer to the body
        auto in = pink (0.18f, 4.0, 11);
        auto crest = [] (const AudioBuffer<float>& b, int from)
        {
            float pk = 0.0f; double sum = 0.0;
            for (int i = from; i < b.getNumSamples(); ++i) { const float v = b.getSample (0, i); pk = jmax (pk, std::abs (v)); sum += (double) v * v; }
            return racoon::dsp::gainToDb (pk) - racoon::dsp::gainToDb ((float) std::sqrt (sum / (b.getNumSamples() - from)));
        };
        auto p100 = particle (100.0f);
        auto o100 = run (*p100, in);
        float gr100 = 0.0f; for (int b = 0; b < 3; ++b) gr100 += p100->particleGr[b].load();
        auto p200 = particle (200.0f);
        auto o200 = run (*p200, in);
        float gr200 = 0.0f; for (int b = 0; b < 3; ++b) gr200 += p200->particleGr[b].load();
        const int from = (int) (1.0 * sr);
        check (gr100 > 1.0f && gr200 > gr100 && crest (o100, from) < crest (in, from) - 0.5f,
               "Partícula: comprime las tres bandas (" + String (gr100, 1) + " dB en 100 %, " + String (gr200, 1) + " dB en 200 %), cresta de "
               + String (crest (in, from), 1) + " a " + String (crest (o100, from), 1) + " dB");
        float pk = 0.0f;
        for (int i = 0; i < o200.getNumSamples(); ++i) pk = jmax (pk, std::abs (o200.getSample (0, i)), std::abs (o200.getSample (1, i)));
        bool finite = true;
        for (int i = 0; i < o200.getNumSamples(); ++i) finite = finite && std::isfinite (o200.getSample (0, i));
        check (finite && pk < 4.0f, "Partícula en 200 %: sin valores raros (pico " + String (pk, 2) + ")");
    }
    {
        // pre EQ: +6 dB of highs shows up at 12 kHz and leaves 200 Hz alone
        auto p = particle (0.0f, [] (Processor& q) { q.setRealValue ("pa_high", 6.0f); });
        auto oh = run (*p, sine (14000.0, 0.1f, 1.2));
        auto p2 = particle (0.0f, [] (Processor& q) { q.setRealValue ("pa_high", 6.0f); });
        auto ol = run (*p2, sine (200.0, 0.1f, 1.2));
        const float hi = racoon::dsp::gainToDb (tone (oh, 0, 0.6, 14000.0) / 0.1f), lo = racoon::dsp::gainToDb (tone (ol, 0, 0.6, 200.0) / 0.1f);
        check (hi > 4.5f && std::abs (lo) < 0.5f, "Partícula: EQ previo, agudos +" + String (hi, 1) + " dB a 14 kHz, " + String (lo, 2) + " dB a 200 Hz");
    }
    {
        // external sidechain: a silent key leaves a loud mix alone, a loud key pulls it down
        auto runSc = [] (float keyAmp)
        {
            auto p = std::make_unique<Processor>();
            p->getBus (true, 1)->enable (true);
            p->setRateAndBufferSizeDetails (sr, 512);
            p->prepareToPlay (sr, 512);
            p->setRealValue ("pa_on", 1.0f); p->setRealValue ("pa_amount", 150.0f); p->setRealValue ("pa_sc", 1.0f); p->setRealValue ("lim_on", 0.0f);
            p->prepareToPlay (sr, 512);
            auto mix = pink (0.18f, 3.0, 5), key = pink (keyAmp, 3.0, 9);
            AudioBuffer<float> io (4, 512);
            float gr = 0.0f;
            for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
            {
                for (int c = 0; c < 2; ++c) { io.copyFrom (c, 0, mix, c, pos, 512); io.copyFrom (c + 2, 0, key, c, pos, 512); }
                MidiBuffer m;
                p->processBlock (io, m);
            }
            for (int b = 0; b < 3; ++b) gr += p->particleGr[b].load();
            return gr;
        };
        const float quiet = runSc (0.0f), loud = runSc (0.3f);
        check (quiet < 0.2f && loud > 1.0f, "Partícula con sidechain externo: llave muda " + String (quiet, 1) + " dB, llave fuerte " + String (loud, 1) + " dB");
    }
    {
        // the default Hedgehog is the Partícula at 100 %
        Processor q;
        check (q.value ("pa_on") > 0.5f && std::abs (q.value ("pa_amount") - 100.0f) < 0.01f, "Hedgehog arranca con la Partícula en 100 %");
    }
}
static RegisterTest particleTestReg ("HedgehogParticula", testParticle);

static void shotsHedgehog (const juce::File& out)
{
    using namespace juce;
    using namespace hedgehog_test;
    auto p = make ([] (Processor& q) { q.setRealValue (Processor::eqId (2, "on"), 1.0f); q.setRealValue (Processor::eqId (2, "gain"), 4.0f); q.setRealValue ("mb_on", 1.0f); });
    run (*p, pink (0.05f, 4.0));
    p->learnMatch();
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditorIfNeeded());
    auto* re = dynamic_cast<RacoonEditor*> (ed.get());
    auto* d = re != nullptr ? dynamic_cast<racoon::hedgehog::Display*> (re->getDisplay()) : nullptr;
    if (d == nullptr) { check (false, "pantallas de Hedgehog"); return; }
    for (int page = 0; page < 6; ++page)
    {
        d->showPage (page);
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        tu::shot (*re, out.getChildFile ("hedgehog_pagina" + String (page) + ".png"));
    }
    {
        auto q = make ([] (Processor& x) { x.setRealValue ("pa_on", 1.0f); x.setRealValue ("lim_gain", 4.0f); });
        run (*q, pink (0.15f, 3.0));
        std::unique_ptr<AudioProcessorEditor> e2 (q->createEditorIfNeeded());
        e2->setSize (e2->getWidth(), e2->getHeight());
        MessageManager::getInstance()->runDispatchLoopUntil (200);
        tu::shot (*e2, out.getChildFile ("hedgehog_particula.png"));
    }
    check (true, "pantallas de la Partícula, medidores, balance, EQ, multibanda y asistente"_u);
}
static RegisterTest hedgehogShots ("HedgehogPantallas", shotsHedgehog);

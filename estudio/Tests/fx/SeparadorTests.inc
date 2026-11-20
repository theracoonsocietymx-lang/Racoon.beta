// Checks for Separador de golpe and Pulir (beyond the common contract)
namespace racoon::fx { float separatorMask (RacoonProcessor&); }

static RegisterTest sgSeparador ("separador-golpe", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("separador-golpe", [] { return racoon::fx::create ("separador-golpe").release(); }, out);
    std::cout << "separador-golpe" << std::endl;
    const auto in = hits();
    const int beat = (int) (sr / 4);

    // hit + body = the input
    {
        auto o = run (*make ("separador-golpe"), in);
        float err = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < in.getNumSamples(); ++i) err = jmax (err, std::abs (o.getSample (c, i) - in.getSample (c, i)));
        auto p = make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("look", 1); });
        const int lat = p->getLatencySamples();
        auto o2 = run (*p, in);
        float err2 = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = lat; i < in.getNumSamples(); ++i) err2 = jmax (err2, std::abs (o2.getSample (c, i) - in.getSample (c, i - lat)));
        check (err < 1.0e-5f && err2 < 1.0e-5f && lat == (int) std::round (0.005 * sr), "golpe + cuerpo = la entrada (error " + String (err, 6) + ", con anticipación " + String (err2, 6) + ", " + String (lat) + " muestras)"_u);
    }

    // what is hit and what is body
    {
        auto onlyHit = run (*make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("listen", 1); }), in);
        auto onlyBody = run (*make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("listen", 2); }), in);
        double hitHead = 0, inHead = 0, hitTail = 0, inTail = 0;
        for (int k = 1; k < 7; ++k)
        {
            const int s = k * beat;
            for (int i = s; i < s + (int) (0.01 * sr); ++i) { hitHead += onlyHit.getSample (0, i) * onlyHit.getSample (0, i); inHead += in.getSample (0, i) * in.getSample (0, i); }
            for (int i = s + (int) (0.1 * sr); i < s + (int) (0.24 * sr); ++i) { hitTail += onlyHit.getSample (0, i) * onlyHit.getSample (0, i); inTail += in.getSample (0, i) * in.getSample (0, i); }
        }
        double bodyHead = 0;
        for (int k = 1; k < 7; ++k) for (int i = k * beat; i < k * beat + (int) (0.01 * sr); ++i) bodyHead += onlyBody.getSample (0, i) * onlyBody.getSample (0, i);
        check (hitHead > 0.8 * inHead && hitTail < 0.1 * inTail && bodyHead < 0.4 * inHead,
               "Solo golpe: " + String (100.0 * hitHead / inHead, 0) + " % de los primeros 10 ms y " + String (100.0 * hitTail / inTail, 1) + " % de la cola");
    }

    // sensitivity: a tone that steps up 3 dB every 500 ms
    {
        AudioBuffer<float> step (2, (int) (sr * 2.0));
        for (int i = 0; i < step.getNumSamples(); ++i)
        {
            const float g = 0.1f * std::pow (1.4125f, (float) (i / (int) (sr * 0.5)));
            const float v = g * (float) std::sin (MathConstants<double>::twoPi * 220.0 * i / sr);
            step.setSample (0, i, v); step.setSample (1, i, v);
        }
        auto peakMask = [&] (float sens)
        {
            auto p = make ("separador-golpe", [sens] (RacoonProcessor& q) { q.setRealValue ("sens", sens); });
            AudioBuffer<float> io (2, 256);
            float m = 0.0f;
            for (int pos = 0; pos + 256 <= step.getNumSamples(); pos += 256)
            {
                for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, step, c, pos, 256);
                MidiBuffer mb;
                p->processBlock (io, mb);
                if (pos > (int) (sr * 0.4)) m = jmax (m, racoon::fx::separatorMask (*p));     // after the start of the tone
            }
            return m;
        };
        const float lo = peakMask (10), hi = peakMask (90);
        check (lo < 0.1f && hi > 0.6f, "Sensibilidad: un escalón de 3 dB es golpe con 90 ("_u + String (hi, 2) + ") y no con 10 (" + String (lo, 2) + ")");
    }

    // levels: the hit off leaves the body, the body off leaves the hit
    {
        // with the look-ahead the whole start of each hit is on the hit side
        auto pa = make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("t_level", -60); q.setRealValue ("look", 1); });
        const int lat = pa->getLatencySamples();
        auto noHitRaw = run (*pa, in);
        AudioBuffer<float> noHit (2, in.getNumSamples()); noHit.clear();
        for (int c = 0; c < 2; ++c) noHit.copyFrom (c, 0, noHitRaw, c, lat, in.getNumSamples() - lat);
        auto noBody = run (*make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("s_level", -60); }), in);
        float headIn = 0, headOut = 0;
        double tailIn = 0, tailOut = 0;
        for (int k = 1; k < 7; ++k)
        {
            headIn = jmax (headIn, in.getMagnitude (0, k * beat, (int) (0.005 * sr)));
            headOut = jmax (headOut, noHit.getMagnitude (0, k * beat, (int) (0.005 * sr)));
            for (int i = k * beat + (int) (0.1 * sr); i < k * beat + (int) (0.24 * sr); ++i) { tailIn += in.getSample (0, i) * in.getSample (0, i); tailOut += noBody.getSample (0, i) * noBody.getSample (0, i); }
        }
        const float dh = Decibels::gainToDecibels (headOut / headIn), dt = (float) (10.0 * std::log10 (tailOut / tailIn + 1e-12));
        check (dh < -12.0f && dt < -20.0f, "Golpe en -60: el principio baja " + String (dh, 1) + " dB; Cuerpo en -60: la cola baja " + String (dt, 1) + " dB");
    }

    // the envelope ducks the body's tail
    {
        auto plain = run (*make ("separador-golpe"), in);
        auto duck = run (*make ("separador-golpe", [] (RacoonProcessor& q) { q.setRealValue ("d1_dest", 1); q.setRealValue ("d1_amt", -100); }), in);
        double a = 0, b2 = 0;
        for (int k = 1; k < 7; ++k) for (int i = k * beat + (int) (0.02 * sr); i < k * beat + (int) (0.06 * sr); ++i) { a += plain.getSample (0, i) * plain.getSample (0, i); b2 += duck.getSample (0, i) * duck.getSample (0, i); }
        const double d = 10.0 * std::log10 (b2 / a + 1e-12);
        check (d < -10.0, "la envolvente con Cuerpo: nivel en -100 baja la cola de 20 a 60 ms " + String (d, 1) + " dB");
    }
});

static RegisterTest sgPulir ("pulir", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("pulir", [] { return racoon::fx::create ("pulir").release(); }, out);
    std::cout << "pulir" << std::endl;
    // at 0 it is flat
    {
        float worst = 0.0f;
        for (int mode = 0; mode < 4; ++mode)
            for (double hz : { 40.0, 120.0, 400.0, 1500.0, 5000.0, 12000.0 })
            {
                auto in = sine (hz, 0.2f, 1.0);
                auto o = run (*make ("pulir", [mode] (RacoonProcessor& q) { q.setRealValue ("amount", 0); q.setRealValue ("mode", (float) mode); q.setRealValue ("match", 0); }), in);
                worst = jmax (worst, std::abs (rmsDb (o, 0, 24000, 24000) - rmsDb (in, 0, 24000, 24000)));
            }
        check (worst < 0.5f, "Pulir en 0: las tres bandas suman plano en los cuatro modos (peor " + String (worst, 2) + " dB)");
    }
    // the knob compresses: the crest of noise with peaks drops
    {
        AudioBuffer<float> n (2, (int) (sr * 3.0));
        Random r (3);
        for (int i = 0; i < n.getNumSamples(); ++i)
        {
            const float burst = (i % 24000) < 4800 ? 3.0f : 1.0f;
            const float v = (r.nextFloat() * 2.0f - 1.0f) * 0.12f * burst;
            n.setSample (0, i, v); n.setSample (1, i, v);
        }
        // the loud bursts against the quiet stretches between them: compression brings them closer
        auto crest = [&] (const AudioBuffer<float>& b)
        {
            double loud = 0.0, quiet = 0.0;
            for (int k = 2; k < 6; ++k) { loud += tu::rms (b, k * 24000 + 1200, 2400); quiet += tu::rms (b, k * 24000 + 12000, 6000); }
            return Decibels::gainToDecibels ((float) (loud / quiet));
        };
        String msg; bool ok = true;
        for (int mode = 0; mode < 4; ++mode)
        {
            auto o0 = run (*make ("pulir", [mode] (RacoonProcessor& q) { q.setRealValue ("amount", 0); q.setRealValue ("mode", (float) mode); }), n);
            auto o1 = run (*make ("pulir", [mode] (RacoonProcessor& q) { q.setRealValue ("amount", 100); q.setRealValue ("mode", (float) mode); }), n);
            const float d = crest (o0) - crest (o1);
            ok = ok && d >= 1.0f;
            msg << String (d, 1) << " ";
        }
        check (ok, "Pulir en 100 acerca lo fuerte a lo suave en los cuatro modos (dB: " + msg + ")");
    }
    // volume match
    {
        AudioBuffer<float> n (2, (int) (sr * 6.0));
        Random r (4);
        for (int i = 0; i < n.getNumSamples(); ++i) { const float v = (r.nextFloat() * 2.0f - 1.0f) * 0.15f; n.setSample (0, i, v); n.setSample (1, i, v); }
        float worst = 0.0f;
        for (int mode = 0; mode < 4; ++mode)
        {
            auto o0 = run (*make ("pulir", [mode] (RacoonProcessor& q) { q.setRealValue ("amount", 0); q.setRealValue ("mode", (float) mode); }), n);
            auto o1 = run (*make ("pulir", [mode] (RacoonProcessor& q) { q.setRealValue ("amount", 100); q.setRealValue ("mode", (float) mode); }), n);
            worst = jmax (worst, std::abs (rmsDb (o1, 0, (int) (sr * 4), (int) (sr * 2)) - rmsDb (o0, 0, (int) (sr * 4), (int) (sr * 2))));
        }
        check (worst < 1.0f, "Igualar volumen: de 0 a 100 el nivel cambia " + String (worst, 2) + " dB como máximo"_u);
    }
    // switching modes while it plays does not click
    {
        auto in = sine (220.0, 0.4f, 1.0);
        auto p = make ("pulir", [] (RacoonProcessor& q) { q.setRealValue ("mode", 0); q.setRealValue ("amount", 80); });
        AudioBuffer<float> io (2, 256), o (2, in.getNumSamples());
        for (int pos = 0; pos + 256 <= in.getNumSamples(); pos += 256)
        {
            if (pos == 24064) p->setRealValue ("mode", 1);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, 256);
            MidiBuffer mb;
            p->processBlock (io, mb);
            for (int c = 0; c < 2; ++c) o.copyFrom (c, pos, io, c, 0, 256);
        }
        AudioBuffer<float> around (2, 4000);
        for (int c = 0; c < 2; ++c) around.copyFrom (c, 0, o, c, 22000, 4000);
        const float step = 0.4f * (float) (MathConstants<double>::twoPi * 220.0 / sr);
        check (tu::maxJump (around) < step * 3.0f, "cambiar de A a B mientras suena no truena (salto "_u + String (tu::maxJump (around), 4) + ")");
    }
});

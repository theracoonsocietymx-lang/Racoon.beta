namespace racoon::fx { float meterReading (RacoonProcessor&, int); }

// Checks for the extra effects, beyond the common contract
namespace xt
{
using namespace juce;
using namespace nt;

// level of one frequency in a channel, in dB (Goertzel)
inline float toneDb (const AudioBuffer<float>& b, int ch, double hz, int start, int len)
{
    const double w = MathConstants<double>::twoPi * hz / sr, cw = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = start; i < start + len && i < b.getNumSamples(); ++i)
    {
        const double s0 = b.getSample (ch, i) + cw * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
    return (float) (10.0 * std::log10 (p / ((double) len * len / 4.0) + 1e-20));
}

inline float rmsDb (const AudioBuffer<float>& b, int ch, int start, int len) { return db (tu::rms (b, start, len) + 1.0e-9f) + 0.0f * (float) ch; }

// frequency from upward zero crossings with linear interpolation
inline double freqOf (const AudioBuffer<float>& b, int start, int len)
{
    double first = -1, last = -1; int count = 0;
    for (int i = start + 1; i < start + len && i < b.getNumSamples(); ++i)
    {
        const float a = b.getSample (0, i - 1), c = b.getSample (0, i);
        if (a < 0.0f && c >= 0.0f)
        {
            const double t = (i - 1) + a / (a - c);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    }
    return count > 0 ? count * sr / (last - first) : 0.0;
}

// drum-like hits: a 60 Hz thump with a click, four per second
inline AudioBuffer<float> hits (double seconds = 2.0)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    Random r (5);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = (i % (int) (sr / 4)) / sr;
        const float v = (float) (std::sin (MathConstants<double>::twoPi * 60.0 * t) * std::exp (-t * 6.0) * 0.5 + (r.nextFloat() * 2 - 1) * std::exp (-t * 300.0) * 0.4);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}
}

static RegisterTest xtTransients ("transitorios", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("transitorios", [] { return racoon::fx::create ("transitorios").release(); }, out);
    std::cout << "transitorios" << std::endl;
    auto in = hits();
    auto more = run (*make ("transitorios", [] (RacoonProcessor& p) { p.setRealValue ("attack", 100); p.setRealValue ("clip", 0); }), in);
    auto less = run (*make ("transitorios", [] (RacoonProcessor& p) { p.setRealValue ("attack", -100); p.setRealValue ("clip", 0); }), in);
    const float pin = tu::peak (in, 0, in.getNumSamples()), pm = tu::peak (more, 0, more.getNumSamples()), pl = tu::peak (less, 0, less.getNumSamples());
    check (pm > pin * 1.3f && pl < pin * 0.8f, "el ataque sube y baja el golpe (" + String (db (pl / pin), 1) + " / " + String (db (pm / pin), 1) + " dB)");
    auto tail = run (*make ("transitorios", [] (RacoonProcessor& p) { p.setRealValue ("attack", 0); p.setRealValue ("sustain", 100); p.setRealValue ("clip", 0); }), in);
    // energy late in each hit (100 to 240 ms after it)
    auto late = [] (const AudioBuffer<float>& b) { double e = 0; for (int h = 1; h < 7; ++h) { const int s = (int) (h * sr / 4 + 0.1 * sr); e += tu::rms (b, s, (int) (0.14 * sr)); } return (float) e; };
    check (late (tail) > late (in) * 1.5f, "el cuerpo alarga la cola (" + String (db (late (tail) / late (in)), 1) + " dB)");
});

static RegisterTest xtDeEsser ("de-esser", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("de-esser", [] { return racoon::fx::create ("de-esser").release(); }, out);
    std::cout << "de-esser" << std::endl;
    const float ess = toneGainDb ("de-esser", [] (RacoonProcessor& p) { p.setRealValue ("detect", 1); p.setRealValue ("thresh", -30); }, 7000.0, 0.4f);
    const float low = toneGainDb ("de-esser", [] (RacoonProcessor& p) { p.setRealValue ("detect", 1); p.setRealValue ("thresh", -30); }, 300.0, 0.4f);
    check (ess < -5.0f, "una ese fuerte en 7 kHz baja " + String (ess, 1) + " dB");
    check (std::abs (low) < 0.5f, "la voz en 300 Hz queda igual (" + String (low, 2) + " dB)");
});

static RegisterTest xtClipper ("clipper", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("clipper", [] { return racoon::fx::create ("clipper").release(); }, out);
    std::cout << "clipper" << std::endl;
    for (int shape = 0; shape < 4; ++shape)
    {
        auto p = make ("clipper", [shape] (RacoonProcessor& q) { q.setRealValue ("drive", 18); q.setRealValue ("ceiling", -3); q.setRealValue ("shape", (float) shape); q.setRealValue ("os", 0); });
        AudioBuffer<float> in (2, (int) sr);
        tu::fillNoise (in, 0.7f, 9);
        auto o = run (*p, in);
        const float pk = tu::peak (o, 0, o.getNumSamples());
        check (pk <= racoon::dsp::dbToGain (-3.0f) * 1.001f, "forma " + String (shape) + ": con +18 dB nada pasa de -3 dB (pico " + String (db (pk), 2) + " dB)");
    }
    auto q = make ("clipper", [] (RacoonProcessor& p) { p.setRealValue ("drive", 0); p.setRealValue ("delta", 1); p.setRealValue ("shape", 0); });
    auto quiet = run (*q, sine (500.0, 0.2f));
    check (tu::peak (quiet, 4000, 20000) < 1.0e-3f, "en delta, lo que no toca el techo no suena");
});

static RegisterTest xtPump ("bombeo", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("bombeo", [] { return racoon::fx::create ("bombeo").release(); }, out);
    std::cout << "bombeo" << std::endl;
    auto p = make ("bombeo", [] (RacoonProcessor& q) { q.setRealValue ("shape", 5); q.setRealValue ("depth", 100); });
    auto in = sine (200.0, 0.5f, 2.0);
    auto o = run (*p, in);
    int silent = 0, loud = 0;
    for (int s = 0; s + 480 < o.getNumSamples(); s += 480) (tu::rms (o, s, 480) < 0.01f ? silent : loud)++;
    check (silent > 70 && loud > 70, "la forma Corte calla medio ciclo: " + String (silent) + " de " + String (silent + loud) + " pedazos callados");
});

static RegisterTest xtSub ("sub-grave", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("sub-grave", [] { return racoon::fx::create ("sub-grave").release(); }, out);
    std::cout << "sub grave" << std::endl;
    auto in = sine (110.0, 0.4f, 1.0);
    auto o = run (*make ("sub-grave", [] (RacoonProcessor& p) { p.setRealValue ("sub", 100); }), in);
    const float subIn = toneDb (in, 0, 55.0, 24000, 19200), subOut = toneDb (o, 0, 55.0, 24000, 19200), main = toneDb (o, 0, 110.0, 24000, 19200);
    check (subOut > subIn + 30.0f && subOut > main - 15.0f, "aparece la octava de abajo, 55 Hz: " + String (subOut - main, 1) + " dB contra 110 Hz");
    auto o2 = run (*make ("sub-grave", [] (RacoonProcessor& p) { p.setRealValue ("sub", 100); p.setRealValue ("octave", 1); }), in);
    check (toneDb (o2, 0, 27.5, 24000, 19200) > toneDb (in, 0, 27.5, 24000, 19200) + 25.0f, "y con -2 octavas, 27.5 Hz");
    AudioBuffer<float> sil (2, (int) sr);
    sil.clear();
    auto o3 = run (*make ("sub-grave"), sil);
    check (tu::peak (o3, 0, o3.getNumSamples()) < 1.0e-6f, "en silencio no inventa grave");
});

static RegisterTest xtExciter ("excitador", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("excitador", [] { return racoon::fx::create ("excitador").release(); }, out);
    std::cout << "excitador" << std::endl;
    auto in = sine (4000.0, 0.3f, 1.0);
    auto o = run (*make ("excitador", [] (RacoonProcessor& p) { p.setRealValue ("amount", 100); p.setRealValue ("color", -100); }), in);
    auto o2 = run (*make ("excitador", [] (RacoonProcessor& p) { p.setRealValue ("amount", 100); p.setRealValue ("color", 100); }), in);
    check (toneDb (o, 0, 8000.0, 24000, 19200) > toneDb (in, 0, 8000.0, 24000, 19200) + 30.0f, "pares: aparece la segunda armónica, 8 kHz"_u);
    check (toneDb (o2, 0, 12000.0, 24000, 19200) > toneDb (in, 0, 12000.0, 24000, 19200) + 30.0f, "impares: aparece la tercera, 12 kHz"_u);
});

static RegisterTest xtImager ("imagen-estereo", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("imagen-estereo", [] { return racoon::fx::create ("imagen-estereo").release(); }, out);
    std::cout << "imagen estéreo"_u << std::endl;
    auto side = [] (double hz) { AudioBuffer<float> b (2, (int) sr); tu::fillSine (b, hz, sr, 0.3f, 0.0); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (1, i, -b.getSample (0, i)); return b; };
    auto o = run (*make ("imagen-estereo"), side (60.0));
    check (tu::rms (o, 24000, 20000) < 0.003f, "los graves quedan en mono: un lado puro en 60 Hz se va");
    auto in8 = side (8000.0);
    auto o8 = run (*make ("imagen-estereo"), in8);
    check (tu::rms (o8, 24000, 20000) > tu::rms (in8, 24000, 20000) * 1.3f, "los agudos se abren al 140 %");
    auto mono = sine (500.0, 0.3f);
    auto oMono = run (*make ("imagen-estereo"), mono);
    float diff = 0.0f;
    for (int i = 24000; i < 44000; ++i) diff = jmax (diff, std::abs (oMono.getSample (0, i) - oMono.getSample (1, i)));
    check (diff < 1.0e-3f, "un sonido mono sigue mono sin Haas"_u);
});

static RegisterTest xtMeter ("medidor", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("medidor", [] { return racoon::fx::create ("medidor").release(); }, out);
    std::cout << "medidor" << std::endl;
    auto p = make ("medidor");
    auto in = sine (1000.0, 0.1f, 4.0);
    auto o = run (*p, in);
    float diff = 0.0f;
    for (int i = 0; i < o.getNumSamples(); ++i) diff = jmax (diff, std::abs (o.getSample (0, i) - in.getSample (0, i)));
    check (diff == 0.0f, "no toca el audio");
    // a 1 kHz sine at -20 dBFS on both sides reads -20 LUFS (ITU-R BS.1770) and -20 dBTP
    const float lufs = racoon::fx::meterReading (*p, 2), tp = racoon::fx::meterReading (*p, 4), corr = racoon::fx::meterReading (*p, 5);
    check (std::abs (lufs + 20.0f) < 0.3f, "1 kHz a -20 dBFS en los dos lados: " + String (lufs, 2) + " LUFS integrado");
    check (std::abs (tp + 20.0f) < 0.3f, "pico real " + String (tp, 2) + " dBTP");
    check (corr > 0.99f, "correlación de un sonido mono: "_u + String (corr, 3));
});

static RegisterTest xtStepGate ("compuerta-ritmica", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("compuerta-ritmica", [] { return racoon::fx::create ("compuerta-ritmica").release(); }, out);
    std::cout << "compuerta rítmica"_u << std::endl;
    auto o = run (*make ("compuerta-ritmica"), sine (300.0, 0.5f, 2.0));
    int silent = 0, total = 0;
    for (int s = 0; s + 240 < o.getNumSamples(); s += 240, ++total) if (tu::rms (o, s, 240) < 0.01f) ++silent;
    check (silent > total / 5 && silent < total * 4 / 5, "corta el sonido al ritmo: " + String (silent) + " de " + String (total) + " pedazos callados");
});

static RegisterTest xtDj ("efectos-dj", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("efectos-dj", [] { return racoon::fx::create ("efectos-dj").release(); }, out);
    std::cout << "efectos DJ" << std::endl;
    auto p = make ("efectos-dj", [] (RacoonProcessor& q) { q.setRealValue ("time", 1); });   // a 1/8 brake at 120 BPM: 250 ms
    run (*p, sine (300.0, 0.5f, 0.5));
    auto pass = run (*p, sine (300.0, 0.5f, 0.2));
    check (tu::rms (pass, 0, pass.getNumSamples()) > 0.3f, "apagado deja pasar todo");
    p->setRealValue ("active", 1);
    auto o = run (*p, sine (300.0, 0.5f, 1.0));
    check (tu::rms (o, 0, 2400) > 0.2f && tu::rms (o, 24000, 20000) < 0.005f, "el freno de cinta empieza sonando y en medio segundo ya se paró");
    auto q = make ("efectos-dj", [] (RacoonProcessor& r) { r.setRealValue ("effect", 3); });
    run (*q, sine (300.0, 0.5f, 0.5));
    q->setRealValue ("active", 1);
    AudioBuffer<float> sil (2, (int) sr);
    sil.clear();
    auto st = run (*q, sil);
    check (tu::rms (st, 0, st.getNumSamples()) > 0.2f, "el tartamudeo repite lo último aunque la entrada se calle");
});

static RegisterTest xtRotary ("rotativo", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("rotativo", [] { return racoon::fx::create ("rotativo").release(); }, out);
    std::cout << "rotativo" << std::endl;
    auto o = run (*make ("rotativo", [] (RacoonProcessor& p) { p.setRealValue ("speed", 1); p.setRealValue ("accel", 0.2f); }), sine (1500.0, 0.4f, 2.0));
    float lo = 10.0f, hi = 0.0f;
    for (int s = 48000; s + 480 < o.getNumSamples(); s += 480) { const float r = tu::rms (o, s, 480); lo = jmin (lo, r); hi = jmax (hi, r); }
    check (hi > lo * 1.5f, "rápido: el volumen sube y baja al girar (" + String (db (hi / lo), 1) + " dB)");
});

static RegisterTest xtWah ("wah", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("wah", [] { return racoon::fx::create ("wah").release(); }, out);
    std::cout << "wah" << std::endl;
    const float heel = toneGainDb ("wah", [] (RacoonProcessor& p) { p.setRealValue ("mode", 2); p.setRealValue ("pedal", 0); }, 2500.0);
    const float toe = toneGainDb ("wah", [] (RacoonProcessor& p) { p.setRealValue ("mode", 2); p.setRealValue ("pedal", 100); }, 2500.0);
    check (toe - heel > 10.0f, "el pedal abajo deja pasar 2.5 kHz " + String (toe - heel, 1) + " dB más que arriba");
});

static RegisterTest xtTune ("afinacion-automatica", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("afinacion-automatica", [] { return racoon::fx::create ("afinacion-automatica").release(); }, out);
    std::cout << "afinación automática"_u << std::endl;
    for (double hz : { 452.0, 225.0, 160.0 })
    {
        auto p = make ("afinacion-automatica", [] (RacoonProcessor& q) { q.setRealValue ("speed", 0); });
        auto o = run (*p, sine (hz, 0.4f, 1.5));
        const double f = freqOf (o, 36000, 30000);
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0), want = 440.0 * std::pow (2.0, (std::round (midi) - 69.0) / 12.0);
        check (std::abs (f - want) < want * 0.006, String (hz, 0) + " Hz se corrige a " + String (want, 1) + " Hz: sale " + String (f, 1) + " Hz");
    }
    {
        // with nothing to tune (noise) it passes the sound clean, only late by the latency it reports
        auto p0 = make ("afinacion-automatica");
        AudioBuffer<float> nz (2, (int) sr);
        tu::fillNoise (nz, 0.3f, 4);
        auto o0 = run (*p0, nz);
        const int lat = p0->getLatencySamples();
        float err = 0.0f;
        for (int i = 24000; i < 46000; ++i) err = jmax (err, std::abs (o0.getSample (0, i) - nz.getSample (0, i - lat)));
        check (lat > 0 && err < 0.02f, "sin nota que corregir deja pasar el sonido limpio, con "_u + String (lat) + " muestras de latencia (error " + String (err, 4) + ")");
    }
    auto p = make ("afinacion-automatica", [] (RacoonProcessor& q) { q.setRealValue ("speed", 0); q.setRealValue ("scale", 1); });   // Do mayor
    auto o = run (*p, sine (270.0, 0.4f, 1.5));   // between Do and Do#, and Do# is not in the scale
    const double f = freqOf (o, 36000, 30000);
    check (std::abs (f - 261.63) < 1.6, "en Do mayor, 270 Hz va a Do y no a Do#: sale " + String (f, 1) + " Hz");
});

static RegisterTest xtShimmer ("shimmer", [] (const juce::File& out)
{
    using namespace juce; using namespace nt; using namespace xt;
    commonChecks ("shimmer", [] { return racoon::fx::create ("shimmer").release(); }, out);
    std::cout << "shimmer" << std::endl;
    auto burst = [] { AudioBuffer<float> b (2, (int) (sr * 3.0)); b.clear(); for (int i = 0; i < (int) (sr * 0.3); ++i) { const float v = 0.4f * (float) std::sin (MathConstants<double>::twoPi * 400.0 * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); } return b; };
    auto off = run (*make ("shimmer", [] (RacoonProcessor& p) { p.setRealValue ("shimmer", 0); p.setRealValue ("mix", 100); }), burst());
    auto on = run (*make ("shimmer", [] (RacoonProcessor& p) { p.setRealValue ("shimmer", 100); p.setRealValue ("mix", 100); }), burst());
    const float octOff = toneDb (off, 0, 800.0, (int) (sr * 1.0), (int) (sr * 1.0)), octOn = toneDb (on, 0, 800.0, (int) (sr * 1.0), (int) (sr * 1.0));
    check (octOn > octOff + 10.0f, "la cola sube una octava: 800 Hz " + String (octOn - octOff, 1) + " dB más con shimmer");
    check (tu::rms (on, (int) (sr * 2.5), (int) (sr * 0.4)) < 0.5f, "y no se desboca");
});

static RegisterTest xtGateRatio ("puerta", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    ignoreUnused (out);
    std::cout << "puerta: ratio"_u << std::endl;
    const float gate = toneGainDb ("puerta", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -40); }, 500.0, racoon::dsp::dbToGain (-50.0f));
    const float exp2 = toneGainDb ("puerta", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -40); p.setRealValue ("ratio", 2.0f); }, 500.0, racoon::dsp::dbToGain (-50.0f));
    check (gate < -60.0f, "como puerta calla lo que está 10 dB bajo el umbral (" + String (gate, 1) + " dB)");
    check (exp2 < -5.0f && exp2 > -16.0f, "como expansor 2:1 solo lo baja unos 10 dB (" + String (exp2, 1) + " dB)");
});

static RegisterTest xtAutoMakeup ("compresor", [] (const juce::File&)
{
    using namespace juce; using namespace nt;
    std::cout << "compresor: ganancia automática"_u << std::endl;
    const float plain = toneGainDb ("compresor", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30); p.setRealValue ("ratio", 4); }, 500.0, 0.3f);
    const float autoMk = toneGainDb ("compresor", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30); p.setRealValue ("ratio", 4); p.setRealValue ("automakeup", 1); }, 500.0, 0.3f);
    check (autoMk > plain + 6.0f, "la ganancia automática devuelve volumen: " + String (autoMk - plain, 1) + " dB");
});

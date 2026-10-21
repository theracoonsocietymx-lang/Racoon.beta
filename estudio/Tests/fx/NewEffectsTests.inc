// Checks for the native effects written for Racoon (beyond the common contract)
namespace racoon::fx { float tunerDetect (const float*, int, double); }

namespace nt
{
using namespace juce;
constexpr double sr = 48000.0;

inline std::unique_ptr<RacoonProcessor> make (const String& id, std::function<void (RacoonProcessor&)> setup = {})
{
    auto p = racoon::fx::create (id);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

inline AudioBuffer<float> run (RacoonProcessor& p, const AudioBuffer<float>& in, int block = 256) { return tu::render (p, in, block, nullptr); }

inline AudioBuffer<float> sine (double hz, float level, double seconds = 1.0)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    tu::fillSine (b, hz, sr, level, 0.0);
    return b;
}

inline float db (float g) { return racoon::dsp::gainToDb (g); }

// level of a tone in the last half of the output, in dB relative to the input level
inline float toneGainDb (const String& id, std::function<void (RacoonProcessor&)> setup, double hz, float level = 0.25f)
{
    auto p = make (id, setup);
    auto in = sine (hz, level);
    auto o = run (*p, in);
    const int half = o.getNumSamples() / 2;
    return db (tu::rms (o, half, half - 1) / tu::rms (in, half, half - 1));
}

#define NT_REGISTER(ID) static RegisterTest reg_##ID
}

static RegisterTest ntLimitador ("limitador", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("limitador", [] { return racoon::fx::create ("limitador").release(); }, out);
    std::cout << "limitador" << std::endl;
    auto p = make ("limitador", [] (RacoonProcessor& p) { p.setRealValue ("gain", 18.0f); p.setRealValue ("ceiling", -1.0f); });
    AudioBuffer<float> in (2, (int) sr);
    tu::fillNoise (in, 0.5f, 3);
    auto o = run (*p, in);
    const float pk = tu::peak (o, 0, o.getNumSamples());
    check (pk <= racoon::dsp::dbToGain (-1.0f) + 1.0e-4f, "con +18 dB de ganancia nunca pasa el techo de -1 dB (pico " + String (db (pk), 2) + " dB)");
    check (p->getLatencySamples() == (int) std::round (0.003 * sr), "informa su anticipación como latencia: "_u + String (p->getLatencySamples()) + " muestras");
    // quiet material passes untouched (after the latency)
    auto q = make ("limitador");
    auto s = sine (1000.0, 0.1f);
    auto o2 = run (*q, s);
    const int lat = q->getLatencySamples();
    float err = 0.0f;
    for (int i = lat + 1000; i < s.getNumSamples(); ++i) err = jmax (err, std::abs (o2.getSample (0, i) - s.getSample (0, i - lat)));
    check (err < 1.0e-4f, "bajo el techo no toca la señal (error "_u + String (err, 6) + ")");
});

static RegisterTest ntPuerta ("puerta", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("puerta", [] { return racoon::fx::create ("puerta").release(); }, out);
    std::cout << "puerta" << std::endl;
    const float quiet = toneGainDb ("puerta", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30.0f); }, 300.0, 0.003f);
    check (quiet < -60.0f, "un tono bajo el umbral queda callado: " + String (quiet, 1) + " dB");
    const float loud = toneGainDb ("puerta", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30.0f); }, 300.0, 0.3f);
    check (std::abs (loud) < 0.1f, "sobre el umbral pasa entero: " + String (loud, 2) + " dB");
    const float ducked = toneGainDb ("puerta", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30.0f); p.setRealValue ("flip", 1.0f); p.setRealValue ("floor", -20.0f); }, 300.0, 0.3f);
    check (std::abs (ducked + 20.0f) < 0.5f, "invertida baja 20 dB cuando la señal pasa el umbral: "_u + String (ducked, 1) + " dB");
});

static RegisterTest ntGlue ("compresor-bus", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("compresor-bus", [] { return racoon::fx::create ("compresor-bus").release(); }, out);
    std::cout << "compresor de bus" << std::endl;
    const float flat = toneGainDb ("compresor-bus", [] (RacoonProcessor& p) { p.setRealValue ("thresh", 0.0f); }, 500.0, 0.05f);
    check (std::abs (flat) < 0.1f, "bajo el umbral no cambia el nivel: " + String (flat, 2) + " dB");
    const float c = toneGainDb ("compresor-bus", [] (RacoonProcessor& p) { p.setRealValue ("thresh", -30.0f); p.setRealValue ("ratio", 1.0f); }, 500.0, 0.5f);
    check (c < -10.0f && c > -24.0f, "a 4:1 con 20 dB sobre el umbral baja unos 15 dB: " + String (c, 1) + " dB");
});

static RegisterTest ntMulti ("multibanda", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("multibanda", [] { return racoon::fx::create ("multibanda").release(); }, out);
    std::cout << "multibanda" << std::endl;
    // with every ratio at 1:1 nothing moves (its defaults do compress, that is checked elsewhere)
    auto neutral = [] (RacoonProcessor& p) { for (auto b : { "b1", "b2", "b3" }) { p.setRealValue (String (b) + "_ra", 1.0f); p.setRealValue (String (b) + "_rb", 1.0f); } };
    for (double hz : { 60.0, 1000.0, 8000.0 })
    {
        const float g = toneGainDb ("multibanda", neutral, hz, 0.1f);
        check (std::abs (g) < 0.3f, "con ratios en 1:1 es transparente a " + String (hz, 0) + " Hz: " + String (g, 2) + " dB");
    }
    const float lowSquash = toneGainDb ("multibanda", [neutral] (RacoonProcessor& p) { neutral (p); p.setRealValue ("b1_ta", -40.0f); p.setRealValue ("b1_ra", 10.0f); }, 60.0, 0.5f);
    const float highSame = toneGainDb ("multibanda", [neutral] (RacoonProcessor& p) { neutral (p); p.setRealValue ("b1_ta", -40.0f); p.setRealValue ("b1_ra", 10.0f); }, 8000.0, 0.5f);
    check (lowSquash < -15.0f && std::abs (highSame) < 0.5f, "comprimir los graves no toca los agudos (" + String (lowSquash, 1) + " / " + String (highSame, 1) + " dB)");
});

static RegisterTest ntDrum ("bus-bateria", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("bus-bateria", [] { return racoon::fx::create ("bus-bateria").release(); }, out);
    std::cout << "bus de batería"_u << std::endl;
    const float boom = toneGainDb ("bus-bateria", [] (RacoonProcessor& p) { p.setRealValue ("boom", 100.0f); p.setRealValue ("boomfreq", 60.0f); p.setRealValue ("drive", 0.0f); }, 60.0, 0.05f);
    const float noBoom = toneGainDb ("bus-bateria", [] (RacoonProcessor& p) { p.setRealValue ("boom", 0.0f); p.setRealValue ("boomfreq", 60.0f); p.setRealValue ("drive", 0.0f); }, 60.0, 0.05f);
    check (boom > noBoom + 2.0f, "boom refuerza el grave (" + String (noBoom, 1) + " -> " + String (boom, 1) + " dB)");
});

static RegisterTest ntEqTres ("eq-tres", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("eq-tres", [] { return racoon::fx::create ("eq-tres").release(); }, out);
    std::cout << "eq de tres bandas" << std::endl;
    for (double hz : { 60.0, 250.0, 1000.0, 2500.0, 10000.0 })
        for (int steep = 0; steep < 2; ++steep)
        {
            const float g = toneGainDb ("eq-tres", [steep] (RacoonProcessor& p) { p.setRealValue ("slope", (float) steep); }, hz);
            check (std::abs (g) < 0.1f, String ("plano en 0 dB a ") + String (hz, 0) + " Hz con " + (steep ? "48" : "24") + " dB/oct: " + String (g, 2) + " dB");
        }
    const float kill = toneGainDb ("eq-tres", [] (RacoonProcessor& p) { p.setRealValue ("onlow", 0.0f); }, 50.0);
    const float keep = toneGainDb ("eq-tres", [] (RacoonProcessor& p) { p.setRealValue ("onlow", 0.0f); }, 5000.0);
    check (kill < -30.0f && std::abs (keep) < 0.2f, "apagar los graves quita 50 Hz (" + String (kill, 1) + " dB) y deja 5 kHz (" + String (keep, 2) + " dB)");
});

static RegisterTest ntEqCanal ("eq-canal", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("eq-canal", [] { return racoon::fx::create ("eq-canal").release(); }, out);
    std::cout << "eq de canal" << std::endl;
    const float flat = toneGainDb ("eq-canal", {}, 1000.0);
    check (std::abs (flat) < 0.05f, "plano con todo en cero: " + String (flat, 3) + " dB");
    const float hp = toneGainDb ("eq-canal", [] (RacoonProcessor& p) { p.setRealValue ("hp", 1.0f); }, 25.0);
    check (hp < -15.0f, "el corte de 80 Hz baja 25 Hz: " + String (hp, 1) + " dB");
    const float mid = toneGainDb ("eq-canal", [] (RacoonProcessor& p) { p.setRealValue ("mid", 9.0f); p.setRealValue ("midfreq", 2000.0f); }, 2000.0);
    check (std::abs (mid - 9.0f) < 0.3f, "medios +9 dB en su frecuencia: " + String (mid, 2) + " dB");
});

static RegisterTest ntUtil ("utilidad", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("utilidad", [] { return racoon::fx::create ("utilidad").release(); }, out);
    std::cout << "utilidad" << std::endl;
    const float g = toneGainDb ("utilidad", [] (RacoonProcessor& p) { p.setRealValue ("gain", -6.0f); }, 1000.0);
    check (std::abs (g + 6.0f) < 0.05f, "ganancia -6 dB: " + String (g, 2));
    auto p = make ("utilidad", [] (RacoonProcessor& p) { p.setRealValue ("width", 0.0f); });
    AudioBuffer<float> in (2, (int) sr);
    tu::fillNoise (in, 0.3f, 9);
    auto o = run (*p, in);
    float diff = 0.0f;
    for (int i = 4800; i < o.getNumSamples(); ++i) diff = jmax (diff, std::abs (o.getSample (0, i) - o.getSample (1, i)));
    check (diff < 1.0e-5f, "ancho 0: izquierda y derecha iguales");
    auto q = make ("utilidad", [] (RacoonProcessor& p) { p.setRealValue ("invl", 1.0f); });
    auto s = sine (500.0, 0.3f);
    auto o2 = run (*q, s);
    check (std::abs (o2.getSample (0, 30000) + s.getSample (0, 30000)) < 1.0e-4f && std::abs (o2.getSample (1, 30000) - s.getSample (1, 30000)) < 1.0e-4f,
           "invertir fase izquierda solo voltea ese lado");
});

static RegisterTest ntTuner ("afinador", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("afinador", [] { return racoon::fx::create ("afinador").release(); }, out);
    std::cout << "afinador" << std::endl;
    for (double hz : { 82.41, 110.0, 440.0, 1046.5 })
    {
        AudioBuffer<float> b (1, 4096);
        // a tone with harmonics, like a string
        for (int i = 0; i < 4096; ++i)
        {
            double v = 0; for (int h = 1; h <= 5; ++h) v += std::sin (MathConstants<double>::twoPi * hz * h * i / sr) / h;
            b.setSample (0, i, (float) (0.2 * v));
        }
        const float f = racoon::fx::tunerDetect (b.getReadPointer (0), 4096, sr);
        const float cents = 1200.0f * std::log2 (f / (float) hz);
        check (std::abs (cents) < 2.0f, "detecta " + String (hz, 2) + " Hz: " + String (f, 2) + " Hz (" + String (cents, 2) + " cents)");
    }
    std::vector<float> silence (4096, 0.0f);
    check (racoon::fx::tunerDetect (silence.data(), 4096, sr) == 0.0f, "en silencio no inventa una nota");
});

static RegisterTest ntLooper ("looper", [] (const juce::File& out)
{
    using namespace juce; using namespace nt;
    commonChecks ("looper", [] { return racoon::fx::create ("looper").release(); }, out);
    std::cout << "looper" << std::endl;
    auto p = make ("looper", [] (RacoonProcessor& p) { p.setRealValue ("state", 1.0f); p.setRealValue ("monitor", 1.0f); });
    auto in = sine (330.0, 0.4f, 1.0);
    run (*p, in);
    p->setRealValue ("state", 2.0f);
    AudioBuffer<float> silence (2, (int) sr);
    silence.clear();
    auto played = run (*p, silence);
    check (tu::rms (played, 0, played.getNumSamples()) > 0.2f, "graba un segundo y lo repite"_u);
    MemoryBlock st;
    p->getStateInformation (st);
    auto q = racoon::fx::create ("looper");
    q->setRateAndBufferSizeDetails (sr, 256);
    q->prepareToPlay (sr, 256);
    q->setStateInformation (st.getData(), (int) st.getSize());
    auto again = run (*q, silence);
    float diff = 0.0f;
    for (int i = 0; i < 2000; ++i) diff = jmax (diff, std::abs (again.getSample (0, i) - in.getSample (0, i)));
    check (diff < 1.0e-3f, "la grabación se guarda con el proyecto y vuelve igual (diferencia "_u + String (diff, 5) + ")");
});

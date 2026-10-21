// Sidechain: the kick connected to it lowers this channel; the key filters, listening, anticipation and the reverse mode
namespace sct
{
using namespace juce;
constexpr double sr = 48000.0;

// a sidechain processor with its key bus on: channels 0-1 this channel, 2-3 the key
inline std::unique_ptr<RacoonProcessor> make (std::function<void (RacoonProcessor&)> setup)
{
    auto p = racoon::fx::create ("sidechain");
    AudioProcessor::BusesLayout l;
    l.inputBuses.add (AudioChannelSet::stereo());
    l.inputBuses.add (AudioChannelSet::stereo());
    l.outputBuses.add (AudioChannelSet::stereo());
    p->setBusesLayout (l);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    if (setup) setup (*p);
    return p;
}

// two seconds: a steady 220 Hz "bass" on this channel, a kick on the key every half second (60 Hz thump + a click)
inline AudioBuffer<float> material (float bassAmp = 0.3f, bool hats = false)
{
    AudioBuffer<float> in (4, (int) (sr * 2.0));
    Random r (3);
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const float bass = bassAmp * (float) std::sin (MathConstants<double>::twoPi * 220.0 * i / sr);
        const double t = std::fmod ((double) i / sr, 0.5);
        float key = t < 0.15 ? 0.9f * (float) std::sin (MathConstants<double>::twoPi * 60.0 * t) * (float) std::exp (-t * 25.0) : 0.0f;
        // hi-hats between the kicks (only when asked): bright noise at 0.25 s
        const double th = std::fmod ((double) i / sr + 0.25, 0.5);
        if (hats && th < 0.03) key += 0.5f * (r.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-th * 120.0);
        in.setSample (0, i, bass); in.setSample (1, i, bass);
        in.setSample (2, i, key); in.setSample (3, i, key);
    }
    return in;
}

inline AudioBuffer<float> run (RacoonProcessor& p, const AudioBuffer<float>& in)
{
    auto o4 = tu::render (p, in, 256, nullptr);
    AudioBuffer<float> o (2, o4.getNumSamples());
    for (int c = 0; c < 2; ++c) o.copyFrom (c, 0, o4, c, 0, o4.getNumSamples());
    return o;
}

inline float levelDb (const AudioBuffer<float>& o, double at, double len) { return racoon::dsp::gainToDb (tu::rms (o, (int) (sr * at), (int) (sr * len))); }
}

static RegisterTest scTest ("sidechain", [] (const juce::File& out)
{
    using namespace juce; using namespace sct;
    commonChecks ("sidechain", [] { return racoon::fx::create ("sidechain").release(); }, out);
    std::cout << "sidechain" << std::endl;

    // ducking: on each kick the bass goes down by the depth, between kicks it is back
    {
        auto p = make ([] (RacoonProcessor& q) { q.setRealValue ("depth", -18.0f); q.setRealValue ("release", 150.0f); q.setRealValue ("hold", 20.0f); });
        auto o = run (*p, material());
        const float during = levelDb (o, 1.02, 0.03), between = levelDb (o, 1.40, 0.05), dry = racoon::dsp::gainToDb (0.3f / std::sqrt (2.0f));
        check (during < between - 14.0f && std::abs (between - dry) < 1.0f,
               "ducking: con el bombo el bajo baja " + String (between - during, 1) + " dB y entre golpes vuelve (" + String (between - dry, 2) + " dB)");
        check (p->getLatencySamples() == 0, "sin anticipar no agrega latencia");
    }
    // mix: at 0 % nothing moves, at 50 % about half the drop in dB stays (the dry signal fills in)
    for (float mix : { 0.0f, 50.0f })
    {
        auto p = make ([mix] (RacoonProcessor& q) { q.setRealValue ("depth", -24.0f); q.setRealValue ("mix", mix); });
        auto o = run (*p, material());
        const float drop = levelDb (o, 1.40, 0.05) - levelDb (o, 1.02, 0.03);
        if (mix == 0.0f) check (std::abs (drop) < 0.5f, "Mezcla 0 %: el bajo no se mueve");
        else check (drop > 3.0f && drop < 8.0f, "Mezcla 50 %: el golpe baja el bajo " + String (drop, 1) + " dB, no 24");
    }
    // compressor mode: a ratio, limited by the depth
    {
        auto p = make ([] (RacoonProcessor& q) { q.setRealValue ("mode", 1.0f); q.setRealValue ("thresh", -30.0f); q.setRealValue ("ratio", 4.0f); q.setRealValue ("depth", -48.0f); q.setRealValue ("knee", 0.0f); });
        auto o = run (*p, material());
        const float drop = levelDb (o, 1.40, 0.05) - levelDb (o, 1.02, 0.03);
        check (drop > 6.0f && drop < 30.0f, "modo compresor: baja según el golpe y el ratio (" + String (drop, 1) + " dB)");
    }
    // the key's low-pass: hi-hats between kicks do not trigger
    {
        auto withHats = [] (float lp)
        {
            auto p = make ([lp] (RacoonProcessor& q) { q.setRealValue ("keylp", lp); q.setRealValue ("thresh", -24.0f); q.setRealValue ("release", 80.0f); q.setRealValue ("hold", 0.0f); });
            auto o = run (*p, material (0.3f, true));
            return levelDb (o, 1.40, 0.05) - levelDb (o, 1.252, 0.02);   // between kicks vs on a hi-hat
        };
        const float open = withHats (20000.0f), filtered = withHats (150.0f);
        check (open > 6.0f && filtered < 2.0f, "Clave: quitar agudos en 150 Hz: el charles ya no la dispara (" + String (open, 1) + " dB sin filtro, " + String (filtered, 1) + " con filtro)");
    }
    // listening to the key: the output is the key
    {
        auto p = make ([] (RacoonProcessor& q) { q.setRealValue ("listen", 1.0f); });
        auto in = material();
        auto o = run (*p, in);
        float diff = 0.0f;
        for (int i = 0; i < o.getNumSamples(); ++i) diff = jmax (diff, std::abs (o.getSample (0, i) - in.getSample (2, i)));
        check (diff < 1.0e-4f, "Escuchar la clave saca la clave tal cual (diferencia " + String (diff, 6) + ")");
    }
    // anticipate: 5 ms of latency, reported to the host, and the reduction is in place when the kick lands
    {
        auto p = make ([] (RacoonProcessor& q) { q.setRealValue ("look", 1.0f); q.setRealValue ("attack", 2.0f); });
        auto o = run (*p, material());
        const int lat = p->getLatencySamples();
        // the kick at 1.0 s comes out at 1.0 s + latency: the first millisecond of the bass there is already down
        const float early = racoon::dsp::gainToDb (tu::rms (o, (int) (sr * 1.0) + lat, (int) (sr * 0.002)));
        check (lat == (int) std::ceil (sr * 0.005) && early < racoon::dsp::gainToDb (0.3f / std::sqrt (2.0f)) - 8.0f,
               "Anticipar: " + String (lat) + " muestras de latencia y el bajo ya está abajo cuando llega el golpe (" + String (early, 1) + " dB)");
    }
    // reverse: the channel only sounds on the hits
    {
        auto p = make ([] (RacoonProcessor& q) { q.setRealValue ("invert", 1.0f); q.setRealValue ("depth", -40.0f); });
        auto o = run (*p, material());
        check (levelDb (o, 1.02, 0.03) > levelDb (o, 1.40, 0.05) + 25.0f, "Al revés: el canal suena con la clave y se calla entre golpes");
    }
    // nothing connected and "otro canal": the channel passes untouched; "este mismo canal": a loud bass lowers itself
    for (int own = 0; own < 2; ++own)
    {
        auto p = racoon::fx::create ("sidechain");
        p->setRateAndBufferSizeDetails (sr, 256);
        p->prepareToPlay (sr, 256);
        p->setRealValue ("source", (float) own);
        AudioBuffer<float> in (2, (int) sr);
        for (int i = 0; i < in.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, 0.5f * (float) std::sin (MathConstants<double>::twoPi * 220.0 * i / sr));
        auto o = tu::render (*p, in, 256, nullptr);
        const float change = racoon::dsp::gainToDb (tu::rms (o, (int) (sr * 0.5), (int) (sr * 0.2))) - racoon::dsp::gainToDb (0.5f / std::sqrt (2.0f));
        if (own == 0) check (std::abs (change) < 0.1f, "sin canal conectado no toca el sonido (" + String (change, 2) + " dB)");
        else check (change < -10.0f, "con «Este mismo canal» se baja a sí mismo (" + String (change, 1) + " dB)");
    }
});

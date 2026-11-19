// The ADSR of every instrument, measured on the sound: a long Ataque softens the first 50 ms, Sostenido at 0 with a
// short Caída empties the held note, a long Relajación keeps it sounding after the note-off. The same four knobs, the
// same card, on Chipmunk, Mole, Ferret, Porcupine, Dormouse and Squirrel (Badger and Hamster have them on their pages).
#include "PreviewSound.h"
namespace adsr_test
{
using namespace juce;
constexpr double sr = 48000.0;

struct Ids { String a, d, s, r; float timeScale; };   // timeScale: 1 when the knob is in ms, 0.001 in seconds

inline double rmsOf (const AudioBuffer<float>& b, double from, double to)
{
    const int s0 = (int) (from * sr), s1 = jmin (b.getNumSamples(), (int) (to * sr));
    double acc = 0.0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = s0; i < s1; ++i) { acc += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
    return std::sqrt (acc / jmax (1, n));
}

// a note held for `hold` seconds (or a chord for Chipmunk), 2.5 s rendered
inline AudioBuffer<float> play (std::function<RacoonProcessor*()> make, std::function<void (RacoonProcessor&)> setup, int note, double hold)
{
    std::unique_ptr<RacoonProcessor> p (make());
    if (setup) setup (*p);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    AudioBuffer<float> in (2, (int) (sr * 2.5));
    in.clear();
    MidiBuffer m;
    m.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), 0);
    m.addEvent (MidiMessage::noteOff (1, note), (int) (hold * sr));
    return tu::render (*p, in, 256, &m);
}

inline void instrument (const String& name, std::function<RacoonProcessor*()> make, const Ids& ids, int note, std::function<void (RacoonProcessor&)> base = {})
{
    auto with = [&] (std::function<void (RacoonProcessor&)> more) { return [base, more] (RacoonProcessor& p) { if (base) base (p); if (more) more (p); }; };
    std::unique_ptr<RacoonProcessor> probe (make());
    bool have = true;
    for (auto& id : { ids.a, ids.d, ids.s, ids.r }) have = have && probe->apvts.getParameter (id) != nullptr;
    check (have, name + ": tiene Ataque, Caída, Sostenido y Relajación"_u);
    if (! have) return;
    const auto t = ids.timeScale;
    const auto dflt = play (make, with ({}), note, 1.2);
    const double body = rmsOf (dflt, 0.05, 0.4);
    check (body > 1.0e-4, name + ": suena (rms " + String (body, 5) + ")");
    if (body <= 1.0e-4) return;
    // Ataque: 600 ms against the shortest
    auto quick = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.a, 1.0f * t); }), note, 1.2);
    auto slow = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.a, 600.0f * t); }), note, 1.2);
    const double q50 = rmsOf (quick, 0.0, 0.05), s50 = rmsOf (slow, 0.0, 0.05);
    check (s50 < q50 * 0.35, name + ": Ataque largo entra suave (primeros 50 ms: " + String (s50 / jmax (1.0e-9, q50), 2) + " del corto)");
    // Sostenido 0 with a 150 ms Caída: the held note is gone by 0.8 s
    auto pluck = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.s, 0.0f); p.setRealValue (ids.d, 150.0f * t); p.setRealValue (ids.a, 1.0f * t); }), note, 1.2);
    auto held = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.s, 100.0f); p.setRealValue (ids.a, 1.0f * t); }), note, 1.2);
    const double hp = rmsOf (pluck, 0.7, 1.0), hh = rmsOf (held, 0.7, 1.0);
    check (hp < hh * 0.1, name + ": Sostenido en 0 con Caída corta apaga la nota mantenida (" + String (hp / jmax (1.0e-9, hh), 3) + " de la sostenida)");
    // Relajación: long against short, after letting go at 0.5 s
    auto shortR = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.r, 20.0f * t); p.setRealValue (ids.s, 100.0f); }), note, 0.5);
    auto longR = play (make, with ([&] (RacoonProcessor& p) { p.setRealValue (ids.r, 1500.0f * t); p.setRealValue (ids.s, 100.0f); }), note, 0.5);
    if (juce::SystemStats::getEnvironmentVariable ("ADSR_DEBUG", {}).isNotEmpty())
    {
        std::cout << "   " << name << " corto/largo:";
        for (double w = 0.40; w < 1.2; w += 0.05) std::cout << " " << String (w, 2) << "=" << String (rmsOf (shortR, w, w + 0.05), 4) << "/" << String (rmsOf (longR, w, w + 0.05), 4);
        std::cout << std::endl;
    }
    // right after letting go (a guitar string dies on its own a moment later, and reverbs leave a tail on both)
    const double rs = rmsOf (shortR, 0.56, 0.75), rl = rmsOf (longR, 0.56, 0.75);
    check (rl > rs * 2.5 && rl > 1.0e-4, name + ": Relajación larga sigue sonando al soltar (" + String (rl / jmax (1.0e-9, rs), 1) + " veces la corta)");
}

// the drums: the same ADSR shapes each hit of the synthesized kit
inline void drums()
{
    auto hit = [] (bool env, float a, float d, float s, float r, double off)
    {
        racoon::PreviewSound ps;
        ps.prepare (sr);
        ps.setKind (racoon::PreviewSound::Kind::drums);
        if (env) ps.setEnvelope (a, d, s, r);
        AudioBuffer<float> b (2, (int) (sr * 1.5));
        b.clear();
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (10, 49, (uint8) 110), 0);     // crash: a long hit
        m.addEvent (MidiMessage::noteOff (10, 49), (int) (off * sr));
        ps.render (b, m, 0, b.getNumSamples());
        return b;
    };
    const auto natural = hit (false, 0, 0, 0, 0, 0.1);
    const auto whole = hit (true, 0.0005f, 1.0f, 1.0f, 8.0f, 0.1);
    const double n1 = rmsOf (natural, 0.2, 0.8), w1 = rmsOf (whole, 0.2, 0.8);
    check (std::abs (w1 / jmax (1.0e-9, n1) - 1.0) < 0.35, "Mole: con el ADSR de inicio el golpe suena casi igual (" + String (w1 / jmax (1.0e-9, n1), 2) + ")");
    const auto cut = hit (true, 0.0005f, 1.0f, 1.0f, 0.03f, 0.1);
    check (rmsOf (cut, 0.3, 0.8) < n1 * 0.05, "Mole: Relajación corta corta el platillo al soltar la nota");
    const auto swell = hit (true, 0.4f, 1.0f, 1.0f, 8.0f, 1.0);
    check (rmsOf (swell, 0.0, 0.05) < rmsOf (natural, 0.0, 0.05) * 0.3, "Mole: Ataque largo hace el golpe entrar suave");
}
}

static RegisterTest adsrTest ("adsr", [] (const juce::File&)
{
    using namespace adsr_test;
    instrument ("Chipmunk", [] { return (RacoonProcessor*) new racoon::chipmunk::Processor(); }, { "adsr_a", "adsr_d", "adsr_s", "adsr_r", 1.0f }, 60);
    instrument ("Porcupine", [] { return (RacoonProcessor*) new racoon::porcupine::Processor(); }, { "attack", "decay", "sustain", "release", 1.0f }, 60,
                [] (RacoonProcessor& p) { p.setRealValue ("attack", 5.0f); p.setRealValue ("hall_mix", 0.0f); });   // the hall would hide the release
    instrument ("Ferret", [] { return (RacoonProcessor*) new racoon::ferret::Processor(); }, { "adsr_a", "adsr_d", "adsr_s", "release", 1.0f }, 52,
                [] (RacoonProcessor& p) { p.setRealValue ("mode", 2.0f); p.setRealValue ("rv_on", 0.0f); p.setRealValue ("dl_on", 0.0f); });
    instrument ("Dormouse", [] { return (RacoonProcessor*) new racoon::dormouse::Processor(); }, { "env1_a", "env1_d", "env1_s", "env1_r", 0.001f }, 60);
    {
        std::unique_ptr<RacoonProcessor> sq (new racoon::squirrel::Processor());
        bool have = true;
        for (auto id : { "amp_a", "amp_d", "amp_s", "amp_r" }) have = have && sq->apvts.getParameter (id) != nullptr;
        check (have, "Squirrel: tiene Ataque, Caída, Sostenido y Relajación"_u);
    }
    {
        std::unique_ptr<RacoonProcessor> mo (new racoon::mole::Processor());
        bool have = true;
        for (auto id : { "adsr_a", "adsr_d", "adsr_s", "adsr_r" }) have = have && mo->apvts.getParameter (id) != nullptr;
        check (have, "Mole: tiene Ataque, Caída, Sostenido y Relajación"_u);
    }
    drums();
    // Chipmunk: choosing Pad brings the pad's envelope to the knobs
    {
        racoon::chipmunk::Processor c;
        c.setAdsrFromPatch (1);
        check (c.value ("adsr_a") > 200.0f && c.value ("adsr_s") > 70.0f, "Chipmunk: al elegir Pad, el ADSR toma la envolvente del pad ("
               + juce::String (c.value ("adsr_a"), 0) + " ms, " + juce::String (c.value ("adsr_s"), 0) + " %)");
    }
});

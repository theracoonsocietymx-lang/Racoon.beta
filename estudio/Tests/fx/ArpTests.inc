// Arpegiador (MIDI): the held chord comes out note by note, on the grid of its speed, with its gate, in the chosen
// order and octaves; Latch keeps it going; no note is left hanging; bypassed, the notes pass as they came.
namespace arpt
{
using namespace juce;
constexpr double sr = 48000.0;
constexpr int block = 512;

struct Ev { int64 at; bool on; int note; int vel; };

// runs the arpeggiator over `blocks` blocks; `feed` adds input MIDI for block b; returns every note event it sent
inline std::vector<Ev> run (std::function<void (RacoonProcessor&)> setup, int blocks, std::function<void (int, MidiBuffer&)> feed)
{
    auto p = racoon::fx::create ("arpegiador");
    if (setup) setup (*p);
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    AudioBuffer<float> buf (2, block);
    std::vector<Ev> out;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        MidiBuffer m;
        if (feed) feed (b, m);
        p->processBlock (buf, m);
        for (const auto ev : m)
        {
            const auto msg = ev.getMessage();
            if (msg.isNoteOn()) out.push_back ({ (int64) b * block + ev.samplePosition, true, msg.getNoteNumber(), msg.getVelocity() });
            else if (msg.isNoteOff()) out.push_back ({ (int64) b * block + ev.samplePosition, false, msg.getNoteNumber(), 0 });
        }
    }
    return out;
}

inline std::vector<Ev> ons (const std::vector<Ev>& v) { std::vector<Ev> o; for (auto& e : v) if (e.on) o.push_back (e); return o; }

inline String notesOf (const std::vector<Ev>& v, int count)
{
    String s;
    for (int i = 0; i < count && i < (int) v.size(); ++i) s << (i ? " " : "") << v[(size_t) i].note;
    return s;
}

// C, E and G held from the start (and let go at block `release`, -1 never)
inline std::function<void (int, MidiBuffer&)> chord (int release = -1)
{
    return [release] (int b, MidiBuffer& m)
    {
        if (b == 0) for (int n : { 60, 64, 67 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
        if (b == release) for (int n : { 60, 64, 67 }) m.addEvent (MidiMessage::noteOff (1, n), 0);
    };
}
}

static RegisterTest arpTest ("arpegiador", [] (const juce::File&)
{
    using namespace juce;
    using namespace arpt;
    {
        auto p = racoon::fx::create ("arpegiador");
        check (p != nullptr && p->isMidiEffect() && p->producesMidi() && p->acceptsMidi(), "el arpegiador es un efecto MIDI (va antes del instrumento)");
    }
    // 120 BPM, 1/16: one step every 6000 samples, gate 75 % = 4500
    const auto all = run ({}, 100, chord());
    const auto on = ons (all);
    bool spaced = on.size() >= 8;
    for (size_t i = 1; i < on.size() && i < 8; ++i) spaced = spaced && std::abs ((double) (on[i].at - on[i - 1].at) - 6000.0) <= 1.0;
    check (spaced && on[0].at == 0, "toca un paso cada 1/16 a 120 BPM (" + String (on.size() > 1 ? (int) (on[1].at - on[0].at) : -1) + " muestras)");
    check (notesOf (on, 7) == "60 64 67 60 64 67 60", "Arriba: " + notesOf (on, 7));
    int64 firstOff = -1;
    for (auto& e : all) if (! e.on && e.note == 60) { firstOff = e.at; break; }
    check (std::abs ((double) firstOff - 4500.0) <= 1.0, "Gate 75 %: la nota dura 3/4 del paso (" + String ((int) firstOff) + ")");
    check (on[0].vel == 100, "usa la fuerza con que tocaste (" + String (on[0].vel) + ")");

    auto down = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("mode", 1); }, 60, chord()));
    check (notesOf (down, 6) == "67 64 60 67 64 60", "Abajo: " + notesOf (down, 6));
    auto updown = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("mode", 2); }, 60, chord()));
    check (notesOf (updown, 6) == "60 64 67 64 60 64", "Arriba y abajo: " + notesOf (updown, 6));
    auto oct = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("octaves", 2); }, 80, chord()));
    check (notesOf (oct, 7) == "60 64 67 72 76 79 60", "Dos octavas: " + notesOf (oct, 7));
    auto chordMode = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("mode", 6); }, 20, chord()));
    check (chordMode.size() >= 6 && chordMode[0].at == chordMode[2].at && chordMode[3].at - chordMode[0].at == 6000, "Acorde: las tres notas juntas en cada paso");
    auto eighth = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("rate", 7); }, 60, chord()));
    check (eighth.size() >= 2 && eighth[1].at - eighth[0].at == 12000, "a 1/8 un paso cada 12000 muestras");
    auto fixedVel = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("velocity", 64); }, 20, chord()));
    check (! fixedVel.empty() && fixedVel[0].vel == 64, "Velocidad fija 64");
    auto rep = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("repeat", 2); }, 60, chord()));
    check (notesOf (rep, 6) == "60 60 64 64 67 67", "Repetir 2: " + notesOf (rep, 6));

    // let go at block 30 (0.32 s): it stops and every note gets its note-off
    const auto stop = run ({}, 80, chord (30));
    int lastOn = 0, balance = 0;
    for (auto& e : stop) { if (e.on) { lastOn = (int) e.at; ++balance; } else --balance; }
    check (lastOn < 30 * block && balance == 0, "al soltar se calla y no deja notas colgadas (" + String (balance) + ")");
    // Latch: it keeps playing after letting go
    const auto latched = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("latch", 1); }, 80, chord (30)));
    check (! latched.empty() && latched.back().at > 60 * block, "Latch sigue tocando al soltar las teclas");
    // swing: the second step comes later than the grid
    const auto sw = ons (run ([] (RacoonProcessor& q) { q.setRealValue ("swing", 50); }, 40, chord()));
    check (sw.size() >= 3 && sw[1].at > 6000 + 1000 && sw[2].at == 12000, "Swing retrasa los pasos pares (" + String (sw.size() > 1 ? (int) sw[1].at : -1) + ")");
    // bypassed: the chord passes untouched
    const auto by = run ([] (RacoonProcessor& q) { q.setRealValue ("bypass", 1); }, 10, chord());
    check (ons (by).size() == 3, "apagado deja pasar el acorde tal cual (" + String ((int) ons (by).size()) + " notas)");
});

// Mole checks (included by Tests/FamilyTest.cpp)
#include "MoleProcessor.h"
#include "MoleEditor.h"
#include "../../Shared/Biblioteca.h"
#include "LibrarySampler.h"

namespace mole_test
{
using namespace juce;
using racoon::mole::Processor;
using racoon::mole::Engine;

struct Head : AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Ev { int64 at; int type; int note; int vel; };
constexpr double sr = 48000.0;

static std::vector<Ev> run (Processor& p, Head& head, int64 total, int block, const std::vector<Ev>& input = {},
                            AudioBuffer<float>* audioIn = nullptr, AudioBuffer<float>* audioOut = nullptr,
                            std::function<void (int64, Head&)> perBlock = {})
{
    std::vector<Ev> out;
    AudioBuffer<float> io (2, block);
    size_t next = 0;
    for (int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) jmin ((int64) block, total - pos);
        if (perBlock) perBlock (pos, head);
        MidiBuffer m;
        while (next < input.size() && input[next].at < pos + n)
        {
            const auto& e = input[next++];
            const int at = (int) jmax ((int64) 0, e.at - pos);
            if (e.type == 1) m.addEvent (MidiMessage::noteOn (1, e.note, (uint8) e.vel), at);
            else m.addEvent (MidiMessage::noteOff (1, e.note), at);
        }
        io.setSize (2, n, false, false, true);
        io.clear();
        if (audioIn != nullptr) for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, *audioIn, c, (int) pos, n);
        p.processBlock (io, m);
        if (audioOut != nullptr) for (int c = 0; c < 2; ++c) audioOut->copyFrom (c, (int) pos, io, c, 0, n);
        for (const auto ev : m)
        {
            const auto msg = ev.getMessage();
            if (msg.isNoteOn()) out.push_back ({ pos + ev.samplePosition, 1, msg.getNoteNumber(), msg.getVelocity() });
            else if (msg.isNoteOff()) out.push_back ({ pos + ev.samplePosition, 0, msg.getNoteNumber(), 0 });
        }
        if (head.playing) head.ppq += n * head.bpm / 60.0 / sr;
    }
    return out;
}

static std::vector<Ev> ons (const std::vector<Ev>& v, int note = -1)
{
    std::vector<Ev> r;
    for (auto& e : v) if (e.type == 1 && (note < 0 || e.note == note)) r.push_back (e);
    return r;
}

static bool paired (const std::vector<Ev>& ev, String& why)
{
    int on[128] {};
    for (auto& e : ev)
    {
        if (e.type == 1) { if (on[e.note] > 0) { why = "doble note-on en " + String (e.note) + " a las " + String (e.at); return false; } on[e.note] = 1; }
        else on[e.note] = 0;
    }
    for (int i = 0; i < 128; ++i) if (on[i] > 0) { why = "nota colgada: " + String (i); return false; }
    return true;
}

// a fresh Mole with only the lanes the test turns on
static std::unique_ptr<Processor> make (Head& h, std::function<void (Processor&)> setup = {})
{
    auto p = std::make_unique<Processor>();
    p->setPlayHead (&h);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    for (int i = 0; i < racoon::mole::numLanes; ++i) p->setRealValue (Processor::laneId (i, "on"), 0.0f);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

static void lane (Processor& p, int i, int steps, int pulses, int rot = 0, int rate = 3)
{
    p.setRealValue (Processor::laneId (i, "on"), 1.0f);
    p.setRealValue (Processor::laneId (i, "steps"), (float) steps);
    p.setRealValue (Processor::laneId (i, "pulses"), (float) pulses);
    p.setRealValue (Processor::laneId (i, "rot"), (float) rot);
    p.setRealValue (Processor::laneId (i, "rate"), (float) rate);
}

constexpr int64 beat = 24000;   // samples per beat at 120 BPM and 48 kHz
}

static void testMole (const juce::File& out)
{
    using namespace juce;
    using namespace mole_test;
    commonChecks ("Mole", [] { return new Processor(); }, out);
    std::cout << "ritmo" << std::endl;

    // euclidean rhythms
    {
        String e58, e38, e1316;
        for (int i = 0; i < 8; ++i) e58 << (Engine::euclid (8, 5, 0, i) ? "x" : ".");
        for (int i = 0; i < 8; ++i) e38 << (Engine::euclid (8, 3, 0, i) ? "x" : ".");
        int count = 0;
        for (int i = 0; i < 16; ++i) count += Engine::euclid (16, 13, 0, i) ? 1 : 0;
        bool evenGaps = true;
        // 5 in 8: the gaps between hits are only of two sizes, 1 and 2
        int last = -1;
        for (int i = 0; i < 16; ++i)
            if (Engine::euclid (8, 5, 0, i % 8)) { if (last >= 0 && (i - last < 1 || i - last > 2)) evenGaps = false; last = i; }
        check (e38 == "x..x..x." && count == 13 && evenGaps, "euclidianos: 3 en 8 = " + e38 + ", 5 en 8 = " + e58 + ", 13 en 16 con 13 golpes");
        String rot;
        for (int i = 0; i < 8; ++i) rot << (Engine::euclid (8, 3, 2, i) ? "x" : ".");
        check (rot == "x.x..x..", "rotación de 2: "_u + rot);
    }

    // four on the floor, sample-accurate, at any block size
    {
        std::vector<int64> ref;
        bool same = true, exact = true;
        for (int block : { 1, 64, 256, 1000 })
        {
            Head h;
            auto p = make (h, [] (Processor& p) { lane (p, 0, 16, 4); });
            auto ev = ons (run (*p, h, beat * 8, block), 36);
            std::vector<int64> at;
            for (auto& e : ev) at.push_back (e.at);
            if (ref.empty()) ref = at; else same = same && at == ref;
            for (size_t i = 0; i < at.size(); ++i) exact = exact && at[i] == (int64) i * beat;
        }
        check (ref.size() == 8 && exact && same, "4 en 16: un bombo en cada tiempo, exacto a la muestra, igual con bloques de 1 a 1000 ("_u + String ((int) ref.size()) + " golpes)");
    }

    // polyrhythm: 3 steps in 2 beats against 4 in 1 bar
    {
        Head h;
        auto p = make (h, [] (Processor& p)
        {
            lane (p, 12, 3, 3); p.setRealValue (Processor::laneId (12, "span"), 2.0f);
            lane (p, 13, 4, 4); p.setRealValue (Processor::laneId (13, "span"), 3.0f);
        });
        auto ev = run (*p, h, beat * 6, 128);
        auto three = ons (ev, 63), four = ons (ev, 60);
        bool ok = three.size() == 9 && four.size() == 8;
        if (ok) ok = std::abs ((double) (three[1].at - three[0].at) - beat * 2.0 / 3.0) <= 1.0 && std::abs ((double) (four[1].at - four[0].at) - beat * 0.75) <= 1.0;
        check (ok, "polirritmo: 3 en 2 tiempos y 4 en 3 tiempos ("_u + String ((int) three.size()) + " y " + String ((int) four.size()) + " golpes en 6 tiempos)");
    }

    // probability: about half, and the same bar always the same
    {
        Head h1, h2;
        auto setup = [] (Processor& p) { lane (p, 3, 16, 16); p.setRealValue (Processor::laneId (3, "prob"), 50.0f); };
        auto a = make (h1, setup), b = make (h2, setup);
        auto ea = ons (run (*a, h1, beat * 64, 256), 42), eb = ons (run (*b, h2, beat * 64, 512), 42);
        bool same = ea.size() == eb.size();
        for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].at == eb[i].at;
        check (ea.size() > 100 && ea.size() < 156 && same, "probabilidad 50 %: "_u + String ((int) ea.size()) + " de 256 golpes, y se repite igual (semilla)");
        Head h3;
        auto c = make (h3, [&setup] (Processor& p) { setup (p); p.setRealValue ("seed", 77.0f); });
        auto ec = ons (run (*c, h3, beat * 64, 256), 42);
        bool differs = ec.size() != ea.size();
        for (size_t i = 0; ! differs && i < ec.size(); ++i) differs = ec[i].at != ea[i].at;
        check (differs, "otra semilla, otra variación"_u);
    }

    // ratchet on a drawn step, with acceleration
    {
        Head h;
        auto p = make (h, [] (Processor& p)
        {
            lane (p, 1, 16, 0);
            p.setRealValue (Processor::laneId (1, "mode"), 1.0f);
            racoon::mole::Step s; s.on = true; s.ratchet = 4;
            p.engine.setStep (1, 4, s);
        });
        auto ev = ons (run (*p, h, beat * 4, 256), 38);
        bool ok = ev.size() == 4 && ev[0].at == beat;
        for (size_t i = 1; ok && i < ev.size(); ++i) ok = std::abs ((double) (ev[i].at - ev[i - 1].at) - beat / 16.0) <= 1.0;
        check (ok, "ratchet de 4 en el paso 5: 4 golpes en 1/16 empezando en el tiempo 2 ("_u + String ((int) ev.size()) + ")");
        String why;
        check (paired (run (*p, h, beat * 4, 256), why), "con ratchets cada nota se apaga " + why);
    }

    // swing 66 % on 16ths moves the second 16th by a third
    {
        Head h;
        auto p = make (h, [] (Processor& p) { lane (p, 3, 16, 16); p.setRealValue ("swing", 66.6667f); });
        auto ev = ons (run (*p, h, beat * 2, 64), 42);
        const double second = ev.size() > 1 ? (double) ev[1].at : 0.0;
        check (std::abs (second - (beat / 4.0 + beat / 12.0)) <= 1.0, "swing 66 %: el segundo 1/16 cae en el tresillo ("_u + String (second, 0) + " muestras)");
    }

    // microtiming: a lane 10 ms early
    {
        Head h;
        auto p = make (h, [] (Processor& p) { lane (p, 0, 16, 4); p.setRealValue (Processor::laneId (0, "micro"), -10.0f); });
        auto ev = ons (run (*p, h, beat * 3, 256), 36);
        check (ev.size() >= 2 && std::abs ((double) ev[0].at - (beat - 480.0)) <= 1.0, "microtiempo -10 ms: 480 muestras antes del tiempo ("_u + String (ev.empty() ? -1 : (int) ev[0].at) + ")");
    }

    // conditions: 1 of 2 plays every other cycle
    {
        Head h;
        auto p = make (h, [] (Processor& p)
        {
            lane (p, 2, 16, 0);
            p.setRealValue (Processor::laneId (2, "mode"), 1.0f);
            racoon::mole::Step s; s.on = true; s.cond = racoon::mole::c1of2;
            p.engine.setStep (2, 4, s);
        });
        auto ev = ons (run (*p, h, beat * 16, 256), 39);
        String at; for (auto& e : ev) at << (int) (e.at / beat) << " ";
        check (ev.size() == 2 && ev[0].at == beat && ev[1].at == beat * 9, "condición 1 de 2: suena en la vuelta 1 y 3, no en la 2 y 4 (tiempos "_u + at + ")");
    }

    // fills every 4 bars only add notes in bar 4; a silence break every 8 bars empties bar 8
    {
        Head h;
        auto p = make (h, [] (Processor& p)
        {
            lane (p, 0, 16, 4); lane (p, 1, 16, 2, 4);
            p.setRealValue ("fillevery", 2.0f); p.setRealValue ("filllen", 2.0f); p.setRealValue ("filldens", 100.0f);
            p.setRealValue ("breakevery", 2.0f); p.setRealValue ("breaklen", 2.0f);
        });
        auto ev = ons (run (*p, h, beat * 32, 256));
        int perBar[8] {};
        for (auto& e : ev) ++perBar[jlimit (0, 7, (int) (e.at / (beat * 4)))];
        check (perBar[0] == 6 && perBar[1] == 6 && perBar[2] == 6 && perBar[3] > 6 && perBar[7] == 0,
               "fill en el compás 4 ("_u + String (perBar[0]) + "," + String (perBar[1]) + "," + String (perBar[2]) + " / " + String (perBar[3]) + " golpes contra 6) y corte en silencio en el 8 (" + String (perBar[7]) + ")");
    }

    // complement lanes never land on the kick
    {
        Head h;
        auto p = make (h, [] (Processor& p) { lane (p, 0, 16, 4); });
        p->complementHamster();
        p->setRealValue (Processor::laneId (11, "dens"), 100.0f);
        auto ev = ons (run (*p, h, beat * 16, 256));
        std::set<int64> kicks;
        for (auto& e : ev) if (e.note == 36) kicks.insert (e.at);
        int shaker = 0, clash = 0;
        for (auto& e : ev) if (e.note == 70) { ++shaker; if (kicks.count (e.at)) ++clash; }
        check (shaker > 10 && clash == 0, "complemento: el shaker toca "_u + String (shaker) + " veces y nunca encima del bombo");
    }

    // mutation evolves the hats but keeps the kick
    {
        Head h1, h2;
        auto plain = make (h1, [] (Processor& p) { lane (p, 0, 16, 4); lane (p, 3, 16, 5, 1); });
        auto mut = make (h2, [] (Processor& p) { lane (p, 0, 16, 4); lane (p, 3, 16, 5, 1); p.setRealValue ("mutation", 80.0f); p.setRealValue ("mutevery", 1.0f); });
        auto a = run (*plain, h1, beat * 16, 256), b = run (*mut, h2, beat * 16, 256);
        auto ka = ons (a, 36), kb = ons (b, 36), ha = ons (a, 42), hb = ons (b, 42);
        bool kickSame = ka.size() == kb.size();
        for (size_t i = 0; kickSame && i < ka.size(); ++i) kickSame = ka[i].at == kb[i].at;
        bool hatsDiffer = ha.size() != hb.size();
        for (size_t i = 0; ! hatsDiffer && i < ha.size(); ++i) hatsDiffer = ha[i].at != hb[i].at;
        check (kickSame && hatsDiffer, "mutación: el charles cambia, el bombo protegido no"_u);
    }

    // groove learned from MIDI played late on the offbeats, then applied
    {
        Head h;
        auto p = make (h, [] (Processor& p) { lane (p, 3, 16, 8, 0); });
        p->setRealValue ("learn", 1.0f);
        std::vector<Ev> played;
        for (int i = 0; i < 16; ++i)
        {
            const int64 at = (int64) i * beat / 2 + (i % 2 == 1 ? 600 : 0);   // offbeat 8ths 12.5 ms late
            played.push_back ({ at, 1, 50, i % 2 ? 70 : 110 });
            played.push_back ({ at + 1000, 0, 50, 0 });
        }
        run (*p, h, beat * 8, 256, played);
        p->setRealValue ("learn", 0.0f);
        p->setRealValue ("groove", (float) (racoon::mole::grooveNames().size() - 1));
        h.ppq = 0.0;
        run (*p, h, 256, 256);
        const float learnedOff = p->engine.learned.offset[2];
        Head h2;
        p->setPlayHead (&h2);
        p->prepareToPlay (sr, 256);
        p->setRealValue ("learn", 0.0f);
        auto ev = ons (run (*p, h2, beat * 2, 256), 42);
        const double lateness = ev.size() > 1 ? (double) (ev[1].at - beat / 2) : 0.0;
        check (std::abs (learnedOff - 0.1f) < 0.02f && std::abs (lateness - 600.0) <= 2.0,
               "groove aprendido: el contratiempo llega " + String (lateness, 0) + " muestras tarde, como se tocó (600)"_u);
    }

    // note mode: plays while a key is held, transposed
    {
        Head h;
        h.playing = false;
        auto p = make (h, [] (Processor& p) { lane (p, 0, 16, 4); p.setRealValue ("play", 3.0f); });
        std::vector<Ev> key { { 1000, 1, 62, 100 }, { 1000 + beat * 2, 0, 62, 0 } };
        auto ev = run (*p, h, beat * 4, 256, key);
        auto o = ons (ev);
        String why;
        check (o.size() == 2 && o[0].note == 38 && o[0].at == 1000 && paired (ev, why),
               "modo nota: arranca con la tecla, transpone (+2 = nota 38) y para al soltarla"_u + why);
    }

    // stopping the transport closes every note
    {
        Head h;
        auto p = make (h, [] (Processor& p) { lane (p, 0, 16, 4); p.setRealValue (Processor::laneId (0, "gate"), 100.0f); p.setRealValue (Processor::laneId (0, "rate"), 0.0f); });
        auto ev = run (*p, h, beat * 3, 256, {}, nullptr, nullptr, [] (int64 pos, Head& hh) { if (pos >= beat * 2 + 5000) hh.playing = false; });
        String why;
        check (paired (ev, why), "al parar la canción no queda ninguna nota colgada "_u + why);
    }

    // audio slicer: captures one bar of clicks and, with nothing random, gives them back in the next bar
    {
        Head h;
        auto p = make (h, [] (Processor& p) { p.setRealValue ("mode", 1.0f); p.setRealValue ("slcut", 2.0f); p.setRealValue ("slfade", 0.5f); });
        AudioBuffer<float> in (2, (int) beat * 12), outA (2, (int) beat * 12);
        in.clear();
        for (int64 t = 0; t < beat * 12; t += beat / 4) for (int c = 0; c < 2; ++c) in.setSample (c, (int) t + 100, ((t / (beat / 4)) % 4 == 0) ? 0.9f : 0.3f);
        p->setRealValue ("slcapture", 1.0f);
        run (*p, h, beat * 12, 256, {}, &in, &outA, [&p] (int64 pos, Head&) { if (pos > beat * 4 + 1000) p->setRealValue ("slcapture", 0.0f); });
        // after capture (bar 2 recorded from beat 4) bar 3 replays the slices: the clicks come back at the same places
        float err = 0.0f;
        for (int64 t = beat * 8; t < beat * 12; ++t) err = jmax (err, std::abs (outA.getSample (0, (int) t) - in.getSample (0, (int) (t - beat * 4))));
        check (p->sliceState.load() == 3 && p->numSlices.load() == 16 && err < 0.35f, "rebanador: captura un compás en 16 pedazos y lo vuelve a tocar en su lugar ("_u + String (p->numSlices.load()) + " pedazos)");
        Head h2;
        p->setPlayHead (&h2);
        p->setRealValue ("slrev", 100.0f);
        AudioBuffer<float> outB (2, (int) beat * 4);
        AudioBuffer<float> silence (2, (int) beat * 4);
        silence.clear();
        run (*p, h2, beat * 4, 256, {}, &silence, &outB);
        // reversed: the click of each 1/16 slice is now near its end
        int lateClicks = 0;
        for (int s = 0; s < 16; ++s)
        {
            const int start = (int) (s * beat / 4);
            float early = 0.0f, late = 0.0f;
            for (int i = 0; i < (int) beat / 8; ++i) early = jmax (early, std::abs (outB.getSample (0, start + i)));
            for (int i = (int) beat / 8; i < (int) beat / 4; ++i) late = jmax (late, std::abs (outB.getSample (0, start + i)));
            if (late > early) ++lateClicks;
        }
        check (lateClicks >= 14, "rebanador al revés: el golpe de cada pedazo queda al final ("_u + String (lateClicks) + " de 16)");
    }

    // MIDI export of 4 bars
    {
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        for (int i = 0; i < racoon::mole::numLanes; ++i) p.setRealValue (Processor::laneId (i, "on"), 0.0f);
        lane (p, 0, 16, 4);
        lane (p, 3, 16, 4, 2);
        auto mf = p.exportMidi (4);
        int notes = 0;
        if (mf.getNumTracks() > 1)
            for (auto* e : *mf.getTrack (1)) if (e->message.isNoteOn()) ++notes;
        check (notes == 32, "exportar MIDI de 4 compases: 32 notas (" + String (notes) + ")");
    }

    // state: drawn steps and the learned groove come back
    {
        Head h;
        auto p = make (h);
        racoon::mole::Step s; s.on = true; s.vel = 77; s.prob = 33; s.ratchet = 3; s.cond = racoon::mole::ifPrev; s.micro = -20;
        p->engine.setStep (5, 17, s, 3);
        p->engine.learned.offset[5] = 0.2f;
        MemoryBlock mb;
        p->getStateInformation (mb);
        Processor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto t = q.engine.getStep (5, 17, 3);
        check (t.on && t.vel == 77 && t.prob == 33 && t.ratchet == 3 && t.cond == racoon::mole::ifPrev && t.micro == -20
                 && std::abs (q.engine.learned.offset[5] - 0.2f) < 1.0e-3f, "guardar y abrir: pasos dibujados del patrón D y el groove aprendido");
    }

    // presets of the library load and play
    {
        Head h;
        Processor p;
        p.setPlayHead (&h);
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        bool all = true;
        String names;
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            h.ppq = 0.0;
            auto ev = ons (run (p, h, beat * 8, 256));
            all = all && ev.size() > 8;
            names << p.getProgramName (i) << ", ";
        }
        check (all && p.getNumPrograms() >= 12, "biblioteca: " + String (p.getNumPrograms()) + " grooves suenan");
    }

    // "Lo que dice la maqueta": with the song playing Mole only plays the clip's notes, with its own drums
    {
        Head h;
        Processor p;
        p.setPlayHead (&h);
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        p.setRealValue ("play", 4.0f);
        const int64 total = (int64) (sr * 2.0);
        AudioBuffer<float> outA (2, (int) total);
        auto silent = ons (run (p, h, total, 256, {}, nullptr, &outA));
        h.ppq = 0.0;
        std::vector<Ev> in { { 1000, 1, 70, 100 }, { 3000, 0, 70, 0 }, { 24000, 1, 63, 100 }, { 26000, 0, 63, 0 } };
        AudioBuffer<float> outB (2, (int) total);
        auto played = ons (run (p, h, total, 256, in, nullptr, &outB));
        check (silent.empty() && outA.getMagnitude (0, 0, (int) total) < 1.0e-4f && played.size() == 2 && outB.getMagnitude (0, 0, (int) total) > 0.01f,
               "modo maqueta: sin clip no genera nada; con el clip toca sus " + String ((int) played.size()) + " golpes con su sonido");
    }
}

static RegisterTest moleTestReg ("Mole", testMole);

// Mole con un kit de Muestras Racoon como sonido propio
static void testMoleKit (const juce::File&)
{
    using namespace juce;
    using namespace mole_test;
    const auto& cat = racoon::biblio::Catalogo::get();
    if (! cat.disponible()) { std::cout << "  (sin Muestras Racoon)" << std::endl; return; }
    racoon::LibrarySampler s;
    s.prepare (48000.0);
    const bool ok = s.loadKit ("techno");
    check (ok && s.notesMapped() >= 16, "el kit Techno cubre " + String (s.notesMapped()) + " notas General MIDI");
    auto play = [&s] (std::initializer_list<int> notes, int len)
    {
        AudioBuffer<float> b (2, len);
        b.clear();
        MidiBuffer m;
        int at = 0;
        for (int n : notes) { m.addEvent (MidiMessage::noteOn (10, n, (uint8) 110), at); at += 2400; }
        s.render (b, m, 0, len);
        return b;
    };
    int sounding = 0;
    for (int n : { 36, 38, 39, 42, 46, 49, 51 })
    {
        s.reset();
        auto b = play ({ n }, 9600);
        if (b.getMagnitude (0, 0, 9600) > 0.01f) ++sounding;
    }
    check (sounding == 7, "bombo, caja, palmas, hi-hats, crash y ride suenan con muestras (" + String (sounding) + " de 7)");
    // el hi-hat cerrado corta al abierto
    s.reset();
    auto open = play ({ 46 }, 48000);
    s.reset();
    auto cut = play ({ 46, 42 }, 48000);
    const float tailOpen = open.getMagnitude (0, 24000, 4800), tailCut = cut.getMagnitude (0, 24000, 4800);
    check (tailCut < tailOpen * 0.5f || tailOpen < 0.001f, "el hi-hat cerrado corta al abierto (" + String (tailOpen, 4) + " a " + String (tailCut, 4) + ")");
    // y el proyecto de Mole recuerda el kit
    Head h;
    auto p = make (h);
    check (p->setOwnKit ("cumbia"), "Mole toma el kit Cumbia");
    MemoryBlock mb;
    p->getStateInformation (mb);
    Head h2;
    auto q = make (h2);
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    check (q->ownKitId() == "cumbia", "el proyecto de Mole vuelve con su kit (" + q->ownKitId() + ")");
}
static RegisterTest moleKitReg ("MoleKit", testMoleKit);

// the lanes work like the piano roll: a click selects (it does not draw), dragging moves, double click writes,
// Deshacer and Cmd+Z take it back
static void testMoleGrid (const juce::File&)
{
    using namespace juce;
    using namespace mole_test;
    Head h;
    auto p = make (h);
    racoon::mole::Display disp (*p);
    disp.setSize (1500, 520);
    auto src = Desktop::getInstance().getMainMouseSource();
    auto ev = [&] (Point<float> a, Point<float> down, bool dragged, int clicks = 1, ModifierKeys m = ModifierKeys (ModifierKeys::leftButtonModifier))
    { return MouseEvent (src, a, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &disp, &disp, Time::getCurrentTime(), down, Time::getCurrentTime(), clicks, dragged); };
    auto at = [&] (int lane, int step) { return disp.cellBounds (lane, step).getCentre().toFloat(); };
    auto on = [&] (int lane, int step) { return disp.grid.get (lane, step).on; };
    // a quiet cell with two quiet cells after it
    int L = -1, S = -1;
    for (int l = racoon::mole::numLanes - 1; l >= 0 && L < 0; --l)
        for (int s = 0; s + 2 < 16 && L < 0; ++s)
            if (! on (l, s) && ! on (l, s + 1) && ! on (l, s + 2)) { L = l; S = s; }
    if (L < 0) { check (false, "Mole: no hay un hueco libre para probar"); return; }
    disp.mouseDown (ev (at (L, S), at (L, S), false)); disp.mouseUp (ev (at (L, S), at (L, S), false));
    const bool noDraw = ! on (L, S);
    disp.mouseDoubleClick (ev (at (L, S), at (L, S), false, 2));
    const bool wrote = on (L, S);
    disp.mouseDown (ev (at (L, S), at (L, S), false));
    for (int k = 1; k <= 6; ++k) disp.mouseDrag (ev (at (L, S) + (at (L, S + 2) - at (L, S)) * (k / 6.0f), at (L, S), true));
    disp.mouseUp (ev (at (L, S + 2), at (L, S), true));
    const bool movedOk = ! on (L, S) && on (L, S + 2) && disp.grid.selection.size() == 1;
    p->undoManager.undo();
    const bool undone = on (L, S) && ! on (L, S + 2);
    disp.grid.selection = { { L, S } };
    disp.keyPressed (KeyPress ('d', ModifierKeys::commandModifier, 0));
    const bool dup = on (L, S) && on (L, S + 1);
    check (noDraw && wrote && movedOk && undone && dup,
           "Mole: clic no dibuja (" + String ((int) noDraw) + "), doble clic escribe (" + String ((int) wrote) + "), arrastrar mueve ("
           + String ((int) movedOk) + "), Deshacer lo regresa (" + String ((int) undone) + "), Cmd+D duplica (" + String ((int) dup) + ")");
}
static RegisterTest moleGridReg ("MoleGrid", testMoleGrid);

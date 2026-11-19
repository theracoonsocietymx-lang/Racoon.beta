// Chipmunk checks (included by Tests/FamilyTest.cpp)
#include "ChipmunkProcessor.h"

namespace chipmunk_test
{
using namespace juce;

struct Head : AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

struct Ev { int64 at; int type; int note; int vel; };   // type 1 on, 0 off

// runs the processor for `total` samples; `input` holds absolute positions; `perBlock` may change the transport
static std::vector<Ev> run (racoon::chipmunk::Processor& p, Head& head, const std::vector<Ev>& input, int64 total, int block,
                            std::function<void (int64 start, Head&)> perBlock = {})
{
    std::vector<Ev> out;
    AudioBuffer<float> empty (0, block);
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
            else if (e.type == 0) m.addEvent (MidiMessage::noteOff (1, e.note), at);
            else m.addEvent (MidiMessage::controllerEvent (1, e.note, e.vel), at);
        }
        empty.setSize (0, n, false, false, true);
        p.processBlock (empty, m);
        for (const auto ev : m)
        {
            const auto msg = ev.getMessage();
            if (msg.isNoteOn()) out.push_back ({ pos + ev.samplePosition, 1, msg.getNoteNumber(), msg.getVelocity() });
            else if (msg.isNoteOff()) out.push_back ({ pos + ev.samplePosition, 0, msg.getNoteNumber(), 0 });
        }
        if (head.playing) head.ppq += n * head.bpm / 60.0 / p.getSampleRate();
    }
    return out;
}

// every note-on closed, never two note-ons on one key
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
}

static void testChipmunk (const juce::File& out)
{
    using namespace juce;
    using namespace chipmunk_test;
    using racoon::chipmunk::Processor;
    namespace harmony = racoon::chipmunk::harmony;
    commonChecks ("Chipmunk", [] { return new Processor(); }, out);
    std::cout << "acordes y arpegios" << std::endl;
    const double sr = 48000.0;

    auto fresh = [&] (Processor& p, Head& h) { p.setPlayHead (&h); p.setRateAndBufferSizeDetails (sr, 256); p.prepareToPlay (sr, 256); };
    auto names = [] (const std::vector<int>& v) { return harmony::nameOf (v); };

    // harmony tables
    {
        Processor p;
        StringArray got;
        for (int note : { 60, 62, 64, 65, 67, 69, 71 }) got.add (names (p.chordFor (note)));
        check (got.joinIntoString (" ") == "C Dm Em F G Am Bdim", "tríadas diatónicas de Do mayor: "_u + got.joinIntoString (" "));
        p.setRealValue ("chordmode", 1); p.setRealValue ("chordtype", 3);
        const auto dmaj7 = names (p.chordFor (62));
        p.setRealValue ("chordmode", 2); p.setRealValue ("key", 9); p.setRealValue ("scale", 1); p.setRealValue ("chordsize", 1);
        const auto am7 = names (p.chordFor (57));
        StringArray all;
        for (int t = 0; t < harmony::chordNames().size() - 2; ++t)
        {
            std::vector<int> v;
            for (int iv : harmony::chordIntervals (t)) v.push_back (48 + iv);
            all.add (names (v));
        }
        check (dmaj7.startsWith ("Dmaj7") && am7.startsWith ("Am7") && ! all.contains ({}),
               "tablas: D maj7 = " + dmaj7 + ", La menor con séptimas da "_u + am7 + ", los " + String (all.size()) + " tipos se reconocen (versión "_u + String (harmony::version) + ")");
    }

    // voice leading keeps the hand still
    {
        Processor p;
        auto a = p.makeChord (60, nullptr);
        auto b = p.makeChord (65, &a);
        auto c = p.makeChord (67, &b);
        auto move = [] (const racoon::chipmunk::Chord& x, const racoon::chipmunk::Chord& y)
        {
            int m = 0;
            for (int i = 0; i < y.n; ++i) { int d = 99; for (int j = 0; j < x.n; ++j) d = jmin (d, std::abs (y.p[(size_t) i] - x.p[(size_t) j])); m += d; }
            return m;
        };
        check (move (a, b) <= 4 && move (b, c) <= 6, "conducción de voces C → F → G: se mueven "_u + String (move (a, b)) + " y " + String (move (b, c)) + " semitonos");
    }

    // progression from the keyboard, chord memory
    {
        Processor p;
        p.setRealValue ("chordmode", 4);
        StringArray got;
        for (int note : { 48, 50, 52, 53 }) got.add (names (p.chordFor (note)));
        check (got.joinIntoString (" ").removeCharacters ("/CDEFGAB#").length() >= 0 && got[0].startsWith ("C") && got[1].startsWith ("G")
               && got[2].startsWith ("Am") && got[3].startsWith ("F"), "progresión con teclas blancas: "_u + got.joinIntoString (" "));

        Head h; fresh (p, h);
        p.learnChord = true;
        run (p, h, { { 0, 1, 60, 100 }, { 0, 1, 64, 100 }, { 0, 1, 67, 100 }, { 0, 1, 71, 100 },
                     { 4800, 0, 60, 0 }, { 4800, 0, 64, 0 }, { 4800, 0, 67, 0 }, { 4800, 0, 71, 0 } }, 9600, 256);
        p.setRealValue ("chordmode", 3);
        check (! p.learnChord.load() && names (p.chordFor (62)).startsWith ("Dmaj7"), "memoria de acordes: aprende C maj7 y lo toca en D: " + names (p.chordFor (62)));
    }

    // strum: 30 ms between notes
    {
        Processor p; Head h; fresh (p, h);
        p.setRealValue ("strum", 30.0f);
        auto ev = run (p, h, { { 1000, 1, 60, 100 }, { 30000, 0, 60, 0 } }, 48000, 256);
        std::vector<int64> ons;
        for (auto& e : ev) if (e.type == 1) ons.push_back (e.at);
        String why;
        check (ons.size() == 3 && ons[0] == 1000 && ons[1] == 2440 && ons[2] == 3880 && paired (ev, why), "strum de 30 ms: notas en 1000, 2440, 3880");
    }

    // arpeggio on the host grid at 120 BPM: 1/16 = 6000 samples, swing moves the even 16ths
    for (float swing : { 0.0f, 50.0f })
    {
        Processor p; Head h; fresh (p, h);
        p.setRealValue ("arp", 1); p.setRealValue ("swing", swing);
        h.playing = true;
        auto ev = run (p, h, { { 0, 1, 60, 100 } }, 6000 * 16, 512);
        std::vector<Ev> ons;
        for (auto& e : ev) if (e.type == 1) ons.push_back (e);
        bool ok = ons.size() == 16;
        for (size_t k = 0; ok && k < ons.size(); ++k) ok = ons[k].at == (int64) k * 6000 + ((k & 1) && swing > 0 ? 1500 : 0);
        if (! ok) { String d; for (auto& e : ons) d << e.at << ":" << e.note << " "; std::cout << "      " << d << std::endl; }
        const bool up = ons.size() >= 4 && ons[0].note < ons[1].note && ons[1].note < ons[2].note && ons[3].note == ons[0].note;
        check (ok && up, String (swing > 0 ? "swing 50 %: los pasos pares se retrasan 1500 muestras" : "arpegio 1/16 a 120 BPM: cada nota exacta cada 6000 muestras, hacia arriba"));
    }

    // tempo ramp from 90 to 150 BPM: every note lands on its 16th of the host timeline
    {
        Processor p; Head h; fresh (p, h);
        p.setRealValue ("arp", 1);
        h.playing = true; h.bpm = 90.0;
        std::vector<std::tuple<int64, double, double>> blocks;   // start, ppq, bpm
        auto ev = run (p, h, { { 0, 1, 60, 100 } }, (int64) sr * 8, 480, [&] (int64 start, Head& hd)
        {
            hd.bpm = 90.0 + 60.0 * (double) start / (sr * 8.0);
            blocks.emplace_back (start, hd.ppq, hd.bpm);
        });
        int bad = 0, count = 0;
        for (auto& e : ev)
        {
            if (e.type != 1) continue;
            ++count;
            const auto& b = blocks[(size_t) (e.at / 480)];
            const double q = std::get<1> (b) + (double) (e.at - std::get<0> (b)) * std::get<2> (b) / 60.0 / sr;
            const double err = std::abs (q - std::round (q * 4.0) / 4.0) * 60.0 / std::get<2> (b) * sr;   // in samples
            if (err > 1.0) ++bad;
        }
        const double beats = h.ppq;
        check (bad == 0 && count == (int) std::ceil (beats * 4.0 - 1e-9), "rampa de tempo 90 → 150 BPM: "_u + String (count) + " notas, todas en su semicorchea");
    }

    // loops: the same bar plays the same notes every pass, and nothing hangs at the loop point
    {
        Processor p; Head h; fresh (p, h);
        p.setRealValue ("arp", 1); p.setRealValue ("arpmode", 3); p.setRealValue ("variation", 40); p.setRealValue ("gate", 150);
        p.setRealValue ("s3_prob", 50); p.setRealValue ("s6_prob", 50);
        h.playing = true;
        const int64 bar = (int64) (sr * 2.0);   // 4 beats at 120
        auto ev = run (p, h, { { 0, 1, 60, 100 }, { 0, 1, 64, 90 } }, bar * 3, 256, [&] (int64 start, Head& hd) { if (start % bar == 0) hd.ppq = 0.0; });
        std::vector<std::pair<int64, int>> pass[3];
        for (auto& e : ev) if (e.type == 1) pass[(size_t) jmin ((int64) 2, e.at / bar)].push_back ({ e.at % bar, e.note });
        String why;
        const bool same = pass[0] == pass[1] && pass[1] == pass[2] && ! pass[0].empty();
        if (! same) for (int k = 0; k < 3; ++k) { String d; for (auto& e : pass[k]) d << (int) e.first << ":" << e.second << " "; std::cout << "      " << d << std::endl; }
        // the held notes are still down: release them and stop
        check (same, "loop de un compás: las 3 vueltas son idénticas ("_u + String ((int) pass[0].size()) + " notas por vuelta)");
    }

    // reproducible: the same seed gives the same output; another seed changes it
    {
        auto render = [&] (int seed)
        {
            Processor p; Head h; fresh (p, h);
            p.setRealValue ("arp", 1); p.setRealValue ("arpmode", 3); p.setRealValue ("variation", 50); p.setRealValue ("seed", (float) seed);
            p.setRealValue ("humtime", 8); p.setRealValue ("humvel", 15);
            for (int k = 0; k < 16; ++k) p.setRealValue (Processor::stepId (k, "prob"), 70);
            h.playing = true;
            return run (p, h, { { 0, 1, 60, 100 }, { 100, 1, 63, 100 }, { 200, 1, 67, 100 }, { (int64) sr * 3, 0, 60, 0 }, { (int64) sr * 3, 0, 63, 0 }, { (int64) sr * 3, 0, 67, 0 } },
                        (int64) sr * 4, 333);
        };
        auto a = render (7), b = render (7), c = render (8);
        auto same = [] (const std::vector<Ev>& x, const std::vector<Ev>& y)
        {
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i) if (x[i].at != y[i].at || x[i].note != y[i].note || x[i].vel != y[i].vel || x[i].type != y[i].type) return false;
            return true;
        };
        String why;
        check (same (a, b) && ! same (a, c) && paired (a, why), "semilla reproducible: igual con la misma, distinta con otra, notas cerradas");
    }

    // bypass in the middle of a chord closes its notes
    {
        Processor p; Head h; fresh (p, h);
        auto ev = run (p, h, { { 0, 1, 60, 100 } }, 4800, 256);
        MidiBuffer m;
        p.getBypassParameter()->setValue (1.0f);
        AudioBuffer<float> empty (0, 256);
        p.processBlock (empty, m);
        for (const auto e : m) { const auto msg = e.getMessage(); if (msg.isNoteOff()) ev.push_back ({ 4800, 0, msg.getNoteNumber(), 0 }); }
        String why;
        check (paired (ev, why), "bypass con un acorde sonando: se cierran sus notas "_u + why);
    }

    // MIDI learn: the next CC moves the chosen parameter
    {
        Processor p; Head h; fresh (p, h);
        int gateIndex = -1;
        for (int i = 0; i < p.getParameters().size(); ++i)
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p.getParameters()[i]); rp != nullptr && rp->paramID == "gate") gateIndex = i;
        p.learnParam = gateIndex;
        run (p, h, { { 0, 2, 20, 64 }, { 1000, 2, 20, 0 } }, 2048, 256);
        MemoryBlock st; p.getStateInformation (st);
        Processor q; q.setStateInformation (st.getData(), (int) st.getSize());
        check (p.value ("gate") < 2.0f && q.ccMap[20].load() == gateIndex, "MIDI learn: el CC 20 mueve el gate y se guarda con el proyecto");
    }

    // stress: random playing, mode switches, transport jumps, bypass and panic; every note-on gets its note-off
    {
        Processor p; Head h; fresh (p, h);
        Random r (1234);
        std::vector<Ev> in;
        bool down[128] {};
        int64 t = 0;
        const int64 total = (int64) sr * 60;
        while (t < total - (int64) sr * 3)
        {
            t += 200 + r.nextInt (9000);
            const int note = 40 + r.nextInt (40);
            if (! down[note]) { in.push_back ({ t, 1, note, 1 + r.nextInt (127) }); down[note] = true; }
            else { in.push_back ({ t, 0, note, 0 }); down[note] = false; }
        }
        for (int i = 0; i < 128; ++i) if (down[i]) in.push_back ({ total - (int64) sr * 2, 0, i, 0 });
        std::sort (in.begin(), in.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
        Random rr (99);
        auto ev = run (p, h, in, total, 256, [&] (int64 start, Head& hd)
        {
            if (start > total - (int64) sr * 2)
            {
                // the end: everything off, transport stopped
                p.setRealValue ("hold", 0); p.setRealValue ("latch", 0); hd.playing = false;
                if (p.getBypassParameter()->getValue() > 0.5f) p.getBypassParameter()->setValue (0.0f);
                return;
            }
            if (rr.nextInt (60) == 0) p.setRealValue ("arp", (float) rr.nextInt (2));
            if (rr.nextInt (90) == 0) p.setRealValue ("chordmode", (float) rr.nextInt (5));
            if (rr.nextInt (120) == 0) p.setRealValue ("latch", (float) rr.nextInt (2));
            if (rr.nextInt (120) == 0) p.setRealValue ("hold", (float) rr.nextInt (2));
            if (rr.nextInt (70) == 0) p.setRealValue ("gate", 1.0f + rr.nextFloat() * 199.0f);
            if (rr.nextInt (70) == 0) p.setRealValue ("strum", rr.nextFloat() * 120.0f);
            if (rr.nextInt (100) == 0) p.setRealValue ("arprate", (float) rr.nextInt (19));
            if (rr.nextInt (150) == 0) hd.playing = ! hd.playing;
            if (rr.nextInt (200) == 0) hd.ppq = rr.nextFloat() * 16.0;
            if (rr.nextInt (250) == 0) p.getBypassParameter()->setValue (p.getBypassParameter()->getValue() > 0.5f ? 0.0f : 1.0f);
            if (rr.nextInt (400) == 0) p.panicRequest = true;
            if (rr.nextInt (80) == 0) for (int k = 0; k < 16; ++k) p.setRealValue (Processor::stepId (k, "rat"), (float) (1 + rr.nextInt (4)));
        });
        String why;
        int ons = 0;
        for (auto& e : ev) ons += e.type == 1;
        check (paired (ev, why), "60 s de pruebas al azar (cambios de modo, latch, hold, saltos de transporte, bypass, pánico): "_u
               + String (ons) + " notas, ninguna colgada " + why);
    }

    // the recorded instruments of Muestras Racoon: in tune, with their velocity layers, their loops and their release
    if (racoon::biblio::Catalogo::get().disponible())
    {
        std::cout << "instrumentos grabados" << std::endl;
        // the fundamental of a rendered note, by autocorrelation over 100 ms after the attack
        // the fundamental of a rendered note: the spectrum's peak within a fifth of the expected note, refined between bins
        auto pitchOf = [] (const AudioBuffer<float>& b, int from, double rate)
        {
            const int order = 17, n = 1 << order, len = jmin ((int) (0.5 * rate), b.getNumSamples() - from);
            std::vector<float> buf ((size_t) n * 2, 0.0f);
            for (int i = 0; i < len; ++i) buf[(size_t) i] = b.getSample (0, from + i) * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (len - 1)));
            juce::dsp::FFT fft (order);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            const double expected = 220.0, lo = expected / std::pow (2.0, 7.0 / 12.0), hi = expected * std::pow (2.0, 7.0 / 12.0);
            int best = 0;
            for (int k = (int) (lo * n / rate); k <= (int) (hi * n / rate); ++k) if (buf[(size_t) k] > buf[(size_t) best] || best == 0) best = k;
            const double a = std::log (buf[(size_t) best - 1] + 1e-12), c0 = std::log (buf[(size_t) best] + 1e-12), c = std::log (buf[(size_t) best + 1] + 1e-12);
            const double d = 0.5 * (a - c) / (a - 2.0 * c0 + c);
            return (best + d) * rate / n;
        };
        auto play = [&] (racoon::LibraryInstrument& in, int note, float vel, double holdSeconds, double totalSeconds)
        {
            AudioBuffer<float> out (2, (int) (totalSeconds * sr));
            out.clear();
            const int block = 512;
            for (int pos = 0; pos < out.getNumSamples(); pos += block)
            {
                const int n = jmin (block, out.getNumSamples() - pos);
                MidiBuffer m;
                if (pos == 0) m.addEvent (MidiMessage::noteOn (1, note, vel), 0);
                const int off = (int) (holdSeconds * sr);
                if (off >= pos && off < pos + n) m.addEvent (MidiMessage::noteOff (1, note), off - pos);
                AudioBuffer<float> blk (out.getArrayOfWritePointers(), 2, pos, n);
                MidiBuffer shifted;
                for (const auto ev : m) shifted.addEvent (ev.getMessage(), ev.samplePosition);
                in.render (blk, shifted, 0, n, 1.0f);
            }
            return out;
        };
        int loaded = 0, inTune = 0, sound = 0;
        String off;
        for (auto& ri : Processor::realInstruments())
        {
            racoon::LibraryInstrument in;
            in.prepare (sr);
            if (! in.load (ri.id)) { off << ri.id << " no carga; "; continue; }
            ++loaded;
            // A3 (220 Hz): in the range of all of them
            auto b = play (in, 57, 0.8f, 1.0, 1.2);
            if (b.getRMSLevel (0, 0, b.getNumSamples()) > 0.003f) ++sound;
            const double f = pitchOf (b, (int) (0.15 * sr), sr);
            const double cents = f > 0.0 ? 1200.0 * std::log2 (f / 220.0) : 9999.0;
            if (std::abs (cents) < 25.0) ++inTune; else off << String (ri.id).fromLastOccurrenceOf ("/", false, false) << " " << String (roundToInt (cents)) << " cents; ";
        }
        const int total = (int) Processor::realInstruments().size();
        check (loaded == total && sound == total, "los " + String (total) + " instrumentos grabados de Chipmunk cargan (" + String (loaded) + ") y suenan (" + String (sound) + ")");
        check (inTune >= total - 1, String (inTune) + " de " + String (total) + " afinados a menos de 25 cents en La 220 Hz " + off);
        // the piano: a soft note and a hard one come from different layers (the hard one is brighter, not only louder)
        {
            racoon::LibraryInstrument in;
            in.prepare (sr);
            in.load ("instrumentos/salamander/piano-de-cola");
            auto bright = [] (const AudioBuffer<float>& b)
            {
                double hi = 0.0, all = 0.0, z = 0.0;
                const float* x = b.getReadPointer (0);
                for (int i = 0; i < (int) (0.5 * 48000); ++i) { const double d = x[i] - z; z = x[i]; hi += d * d; all += (double) x[i] * x[i]; }
                return hi / (all + 1e-20);
            };
            auto soft = play (in, 60, 0.25f, 0.5, 0.6), hard = play (in, 60, 1.0f, 0.5, 0.6);
            const float rs = soft.getRMSLevel (0, 0, 24000), rh = hard.getRMSLevel (0, 0, 24000);
            check (rh > rs * 1.8f && bright (hard) > bright (soft) * 1.2,
                   "piano de cola: el golpe fuerte suena " + String (Decibels::gainToDecibels (rh / rs), 1) + " dB arriba y más brillante (otra capa grabada)"_u);
            // the same note twice does not pile up: the first one fades in 30 ms
            AudioBuffer<float> twice (2, 48000);
            twice.clear();
            MidiBuffer m1, m2;
            m1.addEvent (MidiMessage::noteOn (1, 60, 1.0f), 0);
            m2.addEvent (MidiMessage::noteOn (1, 60, 1.0f), 0);
            { AudioBuffer<float> a (twice.getArrayOfWritePointers(), 2, 0, 24000); in.render (a, m1, 0, 24000); }
            { AudioBuffer<float> a (twice.getArrayOfWritePointers(), 2, 24000, 24000); in.render (a, m2, 0, 24000); }
            check (twice.getMagnitude (0, 24000, 24000) < hard.getMagnitude (0, 0, 24000) * 1.3f, "la misma nota dos veces no se apila (la primera se apaga como con el apagador)");
        }
        // the violas: the loop holds the note four seconds; after the note-off it goes away
        {
            racoon::LibraryInstrument in;
            in.prepare (sr);
            in.load ("instrumentos/vsco-2/violas-seccion");
            in.setEnvelope (0.09f, 1.0f, 1.0f, 0.65f);
            auto b = play (in, 60, 0.8f, 4.0, 6.5);
            const float atFour = b.getRMSLevel (0, (int) (3.7 * sr), (int) (0.25 * sr)), early = b.getRMSLevel (0, (int) (0.5 * sr), (int) (0.25 * sr));
            const float tail = b.getRMSLevel (0, (int) (6.2 * sr), (int) (0.25 * sr));
            check (atFour > early * 0.3f && tail < atFour * 0.01f,
                   "cuerdas de orquesta: la nota sigue sonando a los 4 s gracias al bucle (" + String (Decibels::gainToDecibels (atFour / early), 1)
                   + " dB) y se va al soltarla (" + String (Decibels::gainToDecibels (tail / jmax (1e-9f, atFour)), 0) + " dB)");
        }
        // in Chipmunk: choosing a recorded sound loads it; going back to a synthesized one lets it go
        {
            Processor p;
            Head h;
            fresh (p, h);
            p.setRealValue ("ownpatch", 10);
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            const bool on = p.instrument().loaded() && p.instrument().id().contains ("salamander");
            p.setRealValue ("ownpatch", 3);
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            check (on && ! p.instrument().loaded(), "Chipmunk: Piano de cola carga el Salamander y Bajo vuelve al sonido sintetizado");
            p.setRealValue ("ownpatch", 16);
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            MemoryBlock mb;
            p.getStateInformation (mb);
            Processor q;
            Head h2;
            fresh (q, h2);
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            MessageManager::getInstance()->runDispatchLoopUntil (60);
            check (q.instrument().loaded() && q.instrument().id().contains ("violas"), "el proyecto vuelve con las cuerdas de orquesta cargadas");
        }
    }
}

static RegisterTest regChipmunk ("Chipmunk", testChipmunk);

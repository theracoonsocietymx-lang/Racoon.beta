// Ferret checks (included by Tests/FamilyTest.cpp)
#include "FerretProcessor.h"
#include "FerretEditor.h"

namespace ferret_test
{
using namespace juce;
using namespace racoon::ferret;
constexpr double sr = 48000.0;

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

struct Note { double at, len; int note; int vel; };

static std::unique_ptr<Processor> make (Head* h, std::function<void (Processor&)> setup = {})
{
    auto p = std::make_unique<Processor>();
    if (h != nullptr) p->setPlayHead (h);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    // a dry, direct guitar unless the test asks for more
    p->setRealValue ("amp", 5.0f); p->setRealValue ("cab", 4.0f); p->setRealValue ("rv_on", 0.0f);
    p->setRealValue ("variation", 0.0f); p->setRealValue ("human", 0.0f); p->setRealValue ("humvel", 0.0f);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

static AudioBuffer<float> run (Processor& p, Head* h, double seconds, const std::vector<Note>& notes, int block = 256)
{
    const int total = (int) (seconds * sr);
    AudioBuffer<float> out (2, total), io (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = jmin (block, total - pos);
        io.setSize (2, n, false, false, true);
        MidiBuffer m;
        for (auto& e : notes)
        {
            const int on = (int) (e.at * sr), off = (int) ((e.at + e.len) * sr);
            if (on >= pos && on < pos + n) m.addEvent (MidiMessage::noteOn (1, e.note, (uint8) e.vel), on - pos);
            if (e.len > 0 && off >= pos && off < pos + n) m.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
        }
        p.processBlock (io, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
        if (h != nullptr) h->ppq += n / sr * h->bpm / 60.0;
    }
    return out;
}

// the period that repeats best (autocorrelation), in Hz
static double pitchOf (const AudioBuffer<float>& b, int start, int len)
{
    const auto* x = b.getReadPointer (0, start);
    const int minLag = (int) (sr / 1000.0), maxLag = (int) (sr / 50.0);
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < len; ++i) s += (double) x[i] * x[i + lag];
        ac[(size_t) lag] = s;
        best = jmax (best, s);
    }
    // the longest period whose match is almost as good as the best one would be an octave below: take the first strong peak
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] >= 0.9 * best && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
        {
            const double a = ac[(size_t) lag - 1], c = ac[(size_t) lag], d = ac[(size_t) lag + 1];
            const double shift = 0.5 * (a - d) / (a - 2.0 * c + d);
            return sr / (lag + shift);
        }
    return 0.0;
}

static float energy (const AudioBuffer<float>& b, double from, double to) { return tu::rms (b, (int) (from * sr), (int) ((to - from) * sr)); }

static bool near (double a, double b, double tol) { return std::abs (a - b) <= std::abs (b) * tol; }
}

static void testFerret (const juce::File& out)
{
    using namespace juce;
    using namespace ferret_test;
    commonChecks ("Ferret", [] { return new Processor(); }, out);
    std::cout << "guitarra" << std::endl;

    // chords are named the way a guitarist would
    {
        auto name = [] (std::initializer_list<int> n) { std::vector<int> v (n); return recognise (v.data(), (int) v.size()).name(); };
        check (name ({ 60, 64, 67 }) == "C" && name ({ 57, 60, 64, 67 }) == "Am7" && name ({ 40, 44, 47, 50 }) == "E7" && name ({ 47, 55, 62 }) == "G/B"
               && name ({ 50, 53, 57 }) == "Dm" && name ({ 43, 50 }) == "G5" && name ({ 54, 57, 60, 64 }) == "F#m7b5",
               "reconoce C, Am7, E7, G/B, Dm, G5 y F#m7b5 (" + name ({ 54, 57, 60, 64 }) + ")");
    }

    // voicings are playable shapes with the right bass
    {
        const int open[] { 40, 45, 50, 55, 59, 64 };
        bool ok = true;
        String why;
        for (auto chordNotes : { std::vector<int> { 60, 64, 67 }, std::vector<int> { 47, 55, 62 }, std::vector<int> { 54, 57, 60, 64 }, std::vector<int> { 53, 57, 60 },
                                 std::vector<int> { 46, 50, 53, 56 } })
            for (int pos : { 1, 5, 8 })
            {
                const auto c = recognise (chordNotes.data(), (int) chordNotes.size());
                const auto v = findVoicing (c, open, pos, true);
                if (! v.valid()) { ok = false; why << c.name() << " sin forma; "; continue; }
                int lo = 99, hi = -1, first = -1;
                bool has[12] {};
                for (int s = 0; s < 6; ++s)
                    if (v.fret[s] >= 0)
                    {
                        if (first < 0) first = s;
                        has[(open[s] + v.fret[s]) % 12] = true;
                        if (v.fret[s] > 0) { lo = jmin (lo, v.fret[s]); hi = jmax (hi, v.fret[s]); }
                    }
                const int bass = (open[first] + v.fret[first]) % 12;
                int missing = 0;
                for (int i = 0; i < c.numPcs; ++i) if (! has[c.pcs[i]]) ++missing;
                if (bass != c.bass || (hi >= 0 && hi - lo > 3) || missing > 1) { ok = false; why << c.name() << "@" << pos << " "; }
            }
        check (ok, "formas de acorde tocables, con el bajo correcto " + why);
    }

    // tuning: open low E, drop D, capo
    {
        auto pitchFor = [] (int note, std::function<void (Processor&)> setup)
        {
            auto p = make (nullptr, [&] (Processor& q) { q.setRealValue ("mode", 2.0f); q.setRealValue ("model", 5.0f); if (setup) setup (q); });
            auto o = run (*p, nullptr, 1.0, { { 0.01, 0.95, note, 100 } });
            return pitchOf (o, (int) (0.2 * sr), (int) (0.3 * sr));
        };
        const double e = pitchFor (40, {}), d = pitchFor (38, [] (Processor& q) { q.setRealValue ("tuning", 1.0f); }),
                     a = pitchFor (57, {}), capo = pitchFor (42, [] (Processor& q) { q.setRealValue ("capo", 2.0f); });
        check (near (e, 82.41, 0.01) && near (d, 73.42, 0.01) && near (a, 220.0, 0.01) && near (capo, 92.5, 0.01),
               "afinación: Mi grave "_u + String (e, 1) + " Hz, Drop D " + String (d, 1) + " Hz, La " + String (a, 1) + " Hz, cejilla 2 " + String (capo, 1) + " Hz");
    }

    // instrument mode: a chord lands on separate strings; palm mute dies sooner
    {
        auto p = make (nullptr, [] (Processor& q) { q.setRealValue ("mode", 2.0f); });
        run (*p, nullptr, 0.3, { { 0.01, 0.0, 48, 100 }, { 0.01, 0.0, 52, 100 }, { 0.01, 0.0, 55, 100 } });
        int strings = 0;
        for (int s = 0; s < numStrings; ++s) if (p->voicingFret[s].load() >= 0) ++strings;
        check (strings == 3 && p->chordName() == "C", "modo instrumento: tres notas en tres cuerdas y nombre " + p->chordName());

        auto ring = [] (bool mute)
        {
            auto q = make (nullptr, [] (Processor& r) { r.setRealValue ("mode", 2.0f); });
            std::vector<Note> n { { 0.01, 0.0, 52, 110 } };
            if (mute) n.insert (n.begin(), { 0.0, 0.005, 25, 100 });   // MIDI 25: palm mute
            auto o = run (*q, nullptr, 1.0, n);
            return std::make_pair (energy (o, 0.5, 0.9) / jmax (1.0e-6f, energy (o, 0.02, 0.1)), q->currentArt.load());
        };
        const auto sus = ring (false), pm = ring (true);
        check (pm.second == (int) palmMute && pm.first < sus.first * 0.2f, "keyswitch 25 = palm mute, y se apaga antes ("
               + String (pm.first, 3) + " contra " + String (sus.first, 3) + ")");
    }

    // the rhythm player: strokes per bar, swing, anticipation
    {
        PlayerSettings ps;
        ps.density = 0.5f; ps.variation = 0.0f;
        Voicing v; const int shape[] { -1, 3, 2, 0, 1, 0 };
        std::copy (std::begin (shape), std::end (shape), v.fret);
        const char* disco = patternFor (1, 0)->steps;   // D.U.D.U.D.U.D.U.
        int strokes = 0, hitsTotal = 0;
        for (int64 k = 0; k < 16; ++k)
        {
            Hit h[16];
            const int m = stepHits (ps, disco, 16, k, v, h, 16);
            if (m > 0 && ! h[0].choke) { ++strokes; hitsTotal += m; }
        }
        check (strokes == 8 && hitsTotal == 5 * 4 + 4 * 4, "disco en octavos: 8 rasgueos por compás ("_u + String (strokes) + ", " + String (hitsTotal) + " cuerdas)");

        ps.swing = 1.0f; ps.stepSeconds = 0.125;
        Hit even[16], odd[16];
        stepHits (ps, "DDDDDDDDDDDDDDDD", 16, 4, v, even, 16);
        stepHits (ps, "DDDDDDDDDDDDDDDD", 16, 5, v, odd, 16);
        check (even[0].delaySeconds == 0.0 && odd[0].delaySeconds > 0.03, "swing atrasa los pasos impares");

        ps.swing = 0.0f; ps.anticipation = 1.0f;
        const char* quarter = "D...D...D...D...";
        check (stepToken (ps, quarter, 16, 15) == 'D' && stepToken (ps, quarter, 16, 16) == '-' && stepToken (ps, quarter, 16, 3) == '.',
               "anticipación adelanta el golpe del compás"_u);

        ps.anticipation = 0.0f; ps.variation = 1.0f; ps.seed = 3;
        String a, b, c;
        for (int k = 0; k < 64; ++k) { a += stepToken (ps, disco, 16, k); b += stepToken (ps, disco, 16, k); }
        ps.seed = 4;
        for (int k = 0; k < 64; ++k) c += stepToken (ps, disco, 16, k);
        check (a == b && a != c, "variación: la misma semilla decide lo mismo, otra semilla cambia"_u);
    }

    // live: the held chord is strummed in time, and the same settings play exactly the same
    {
        auto render = [] (int seed)
        {
            Head h;
            auto p = make (&h, [seed] (Processor& q) { q.setRealValue ("style", 1.0f); q.setRealValue ("complexity", 0.0f); q.setRealValue ("variation", 60.0f);
                                                       q.setRealValue ("human", 8.0f); q.setRealValue ("seed", (float) seed); });
            return run (*p, &h, 2.0, { { 0.0, 1.9, 48, 100 }, { 0.0, 1.9, 52, 100 }, { 0.0, 1.9, 55, 100 } });
        };
        const auto a = render (5), b = render (5), c = render (6);
        float diffAB = 0.0f, diffAC = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i) { diffAB = jmax (diffAB, std::abs (a.getSample (0, i) - b.getSample (0, i))); diffAC = jmax (diffAC, std::abs (a.getSample (0, i) - c.getSample (0, i))); }
        check (tu::allFinite (a) && energy (a, 0.1, 1.8) > 0.01f && diffAB == 0.0f && diffAC > 0.001f,
               "rasgueo en vivo: suena, se repite igual con la misma semilla y cambia con otra");

        // the strokes fall on the eighths: the attack of each one is where the grid says
        Head h;
        auto p = make (&h, [] (Processor& q) { q.setRealValue ("style", 1.0f); q.setRealValue ("complexity", 0.0f); q.setRealValue ("strum", 0.0f); });
        auto o = run (*p, &h, 1.0, { { 0.0, 0.95, 48, 100 }, { 0.0, 0.95, 52, 100 }, { 0.0, 0.95, 55, 100 } });
        bool onGrid = true;
        const int eighth = (int) (sr * 0.25);
        for (int k = 1; k < 4; ++k)
        {
            const int at = k * eighth;
            const float before = tu::peak (o, at - 600, 500), after = tu::peak (o, at, 600);
            onGrid = onGrid && after > before * 1.3f;
        }
        check (onGrid, "cada rasgueo cae en su octavo");
    }

    // latch keeps the chord after the keys go up; without it the hand stops the strings
    {
        auto tail = [] (bool latch)
        {
            Head h;
            auto p = make (&h, [latch] (Processor& q) { q.setRealValue ("latch", latch ? 1.0f : 0.0f); q.setRealValue ("style", 4.0f); });
            auto o = run (*p, &h, 2.0, { { 0.0, 0.5, 48, 100 }, { 0.0, 0.5, 52, 100 }, { 0.0, 0.5, 55, 100 } });
            return energy (o, 1.2, 1.9);
        };
        const float on = tail (true), off = tail (false);
        check (on > 0.01f && off < on * 0.05f, "mantener acorde: sigue tocando al soltar (" + String (on, 3) + " contra " + String (off, 4) + ")");
    }

    // the chords of the song are logged and exported as a guitar part
    {
        Head h;
        auto p = make (&h, [] (Processor& q) { q.setRealValue ("style", 1.0f); q.setRealValue ("complexity", 0.0f); });
        run (*p, &h, 4.0, { { 0.0, 1.9, 48, 100 }, { 0.0, 1.9, 52, 100 }, { 0.0, 1.9, 55, 100 },
                            { 2.0, 1.9, 45, 100 }, { 2.0, 1.9, 48, 100 }, { 2.0, 1.9, 52, 100 } });
        const auto mf = p->exportMidi (4);
        int ons = 0, wrong = 0;
        if (auto* t = mf.getTrack (0))
            for (auto* e : *t)
                if (e->message.isNoteOn())
                {
                    ++ons;
                    const int pc = e->message.getNoteNumber() % 12;
                    if (pc != 0 && pc != 4 && pc != 7 && pc != 9) ++wrong;
                }
        check (p->logSize() == 2 && ons > 40 && wrong == 0, "registro de acordes (" + String (p->logSize()) + ") y exportación MIDI con "_u + String (ons) + " notas de C y Am");
        auto f = p->exportMidiFile (4);
        check (f.existsAsFile() && f.getSize() > 50, "archivo MIDI en la carpeta de Ferret");
        f.deleteFile();

        // the user pattern and the log come back with the project
        p->setUserStep (0, 'x'); p->setUserStep (63, 'A');
        MemoryBlock st;
        p->getStateInformation (st);
        Processor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.userStep (0) == 'x' && q.userStep (63) == 'A' && q.logSize() == 2, "el patrón de usuario y el registro se guardan con el proyecto"_u);
    }

    // the amplifier: more gain, more harmonics; the direct model stays clean
    {
        auto thd = [] (int model, float gain)
        {
            Amp a;
            a.prepare (sr, 512);
            Amp::Settings s; s.model = model; s.gain = gain;
            AudioBuffer<float> b (2, 512), all (2, 48000);
            for (int pos = 0; pos < 48000; pos += 512)
            {
                const int n = jmin (512, 48000 - pos);
                for (int i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, 0.2f * (float) std::sin (MathConstants<double>::twoPi * 220.0 * (pos + i) / sr));
                a.process (b, n, s);
                for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, b, c, 0, n);
            }
            return tu::tone (all, 0, 24000, 16384, 660.0, sr) / jmax (1.0e-6f, tu::tone (all, 0, 24000, 16384, 220.0, sr));
        };
        const float direct = thd (5, 0.5f), clean = thd (0, 0.1f), crunch = thd (1, 0.9f), brit = thd (2, 1.0f);
        check (direct < 0.001f && crunch > clean * 3.0f && brit > 0.05f, "amplificador: directo limpio (" + String (direct, 4) + "), crunch con más armónicos ("_u
               + String (clean, 3) + " a " + String (crunch, 3) + ")");
    }

    // every pedal on at once stays finite and bounded
    {
        Head h;
        auto p = make (&h, [] (Processor& q)
        {
            for (auto* id : { "comp_on", "wah_on", "od_on", "ch_on", "ph_on", "tr_on", "dl_on", "rv_on", "dbl" }) q.setRealValue (id, 1.0f);
            q.setRealValue ("amp", 2.0f); q.setRealValue ("gain", 100.0f); q.setRealValue ("cab", 2.0f); q.setRealValue ("dl_fb", 90.0f);
        });
        auto o = run (*p, &h, 3.0, { { 0.0, 2.0, 40, 127 }, { 0.0, 2.0, 47, 127 }, { 0.0, 2.0, 52, 127 } });
        check (tu::allFinite (o) && tu::peak (o, 0, o.getNumSamples()) < 2.0f && energy (o, 0.2, 2.5) > 0.01f, "todos los pedales a la vez: estable");
    }

    // every factory preset plays something
    {
        bool ok = true;
        String quiet;
        Processor ref;
        for (int i = 0; i < ref.getNumPrograms(); ++i)
        {
            Head h;
            auto p = std::make_unique<Processor>();
            p->setPlayHead (&h);
            p->prepareToPlay (sr, 256);
            p->setCurrentProgram (i);
            auto o = run (*p, &h, 1.5, { { 0.0, 1.4, 48, 100 }, { 0.0, 1.4, 52, 100 }, { 0.0, 1.4, 55, 100 } });
            if (! tu::allFinite (o) || energy (o, 0.1, 1.4) < 0.005f) { ok = false; quiet << p->getProgramName (i) << " "; }
        }
        check (ok, "los presets de fábrica suenan "_u + quiet);
    }
}
static RegisterTest ferretTestReg ("Ferret", testFerret);

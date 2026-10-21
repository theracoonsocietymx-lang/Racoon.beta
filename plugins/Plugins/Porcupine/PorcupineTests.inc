// Porcupine checks (included by Tests/FamilyTest.cpp)
#include "PorcupineProcessor.h"
#include "PorcupineEditor.h"

namespace porcupine_test
{
using namespace juce;
using namespace racoon::porcupine;
constexpr double sr = 48000.0;

struct Note { double at, len; int note; int vel; };

static std::unique_ptr<Processor> make (std::function<void (Processor&)> setup = {}, bool allBuses = false)
{
    auto p = std::make_unique<Processor>();
    if (allBuses) p->enableAllBuses();
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    p->setRealValue ("hall_mix", 0.0f); p->setRealValue ("depth", 0.0f); p->setRealValue ("scatter", 0.0f);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

// renders every output bus; returns all channels (bus 0 first)
static AudioBuffer<float> run (Processor& p, double seconds, const std::vector<Note>& notes, std::vector<MidiMessage> extra = {}, int block = 256)
{
    const int total = (int) (seconds * sr);
    const int chans = p.getTotalNumOutputChannels();
    AudioBuffer<float> out (chans, total), io (chans, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = jmin (block, total - pos);
        io.setSize (chans, n, false, false, true);
        io.clear();
        MidiBuffer m;
        if (pos == 0) for (auto& x : extra) m.addEvent (x, 0);
        for (auto& e : notes)
        {
            const int on = (int) (e.at * sr), off = (int) ((e.at + e.len) * sr);
            if (on >= pos && on < pos + n) m.addEvent (MidiMessage::noteOn (1, e.note, (uint8) e.vel), on - pos);
            if (e.len > 0 && off >= pos && off < pos + n) m.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
        }
        p.processBlock (io, m);
        for (int c = 0; c < chans; ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

static double pitchOf (const AudioBuffer<float>& b, int start, int len)
{
    const auto* x = b.getReadPointer (0, start);
    const int minLag = (int) (sr / 1000.0), maxLag = (int) (sr / 60.0);
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < len; ++i) s += (double) x[i] * x[i + lag];
        ac[(size_t) lag] = s; best = jmax (best, s);
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (best > 0.0 && ac[(size_t) lag] >= 0.9 * best && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
        {
            const double a = ac[(size_t) lag - 1], c = ac[(size_t) lag], d = ac[(size_t) lag + 1];
            return sr / (lag + 0.5 * (a - d) / (a - 2.0 * c + d));
        }
    return 0.0;
}

// energy in a band (sum of Goertzel magnitudes every 25 Hz)
static float band (const AudioBuffer<float>& b, int start, int len, double lo, double hi)
{
    float s = 0.0f;
    for (double f = lo; f <= hi; f += 25.0) s += tu::tone (b, 0, start, len, f, sr);
    return s;
}

static AudioBuffer<float> bus (const AudioBuffer<float>& all, int index)
{
    AudioBuffer<float> b (2, all.getNumSamples());
    for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, all, index * 2 + c, 0, all.getNumSamples());
    return b;
}
}

static void testPorcupine (const juce::File& out)
{
    using namespace juce;
    using namespace porcupine_test;
    commonChecks ("Porcupine", [] { return new Processor(); }, out);
    std::cout << "coro" << std::endl;

    // voice leading: a C chord in four lines, then F moves as little as possible
    {
        const int c[] { 0, 4, 7 }, f[] { 5, 9, 0 };
        const auto a = voiceLead (c, 3, 0, {}, false, 0.5f);
        const auto b = voiceLead (f, 3, 5, a, false, 0.5f);
        bool ok = a.valid() && b.valid();
        bool has[12] {};
        for (int v = 0; v < 4 && ok; ++v)
        {
            const auto r = rangeOf (v);
            ok = ok && a.note[v] >= r.lo && a.note[v] <= r.hi && (v == 0 || a.note[v] < a.note[v - 1]);
            has[a.note[v] % 12] = true;
        }
        ok = ok && has[0] && has[4] && has[7] && a.note[3] % 12 == 0 && b.note[3] % 12 == 5;
        int moved = 0, common = 0;
        for (int v = 0; v < 3; ++v) { moved += std::abs (b.note[v] - a.note[v]); if (b.note[v] == a.note[v]) ++common; }
        check (ok && moved <= 5 && common >= 1, "SATB: Do en cuatro voces y a Fa moviéndose "_u + String (moved) + " semitonos con "
               + String (common) + " nota común"_u);
    }

    // harmonizing a melody in C major
    {
        int pcs[3], bassPc = 0;
        const int dE = harmonize (64, 0, false, pcs, bassPc, -1);
        const bool eOk = std::find (pcs, pcs + 3, 4) != pcs + 3;
        const int dB = harmonize (71, 0, false, pcs, bassPc, -1);
        const bool bOk = dB == 4 && bassPc == 7;
        const int dF = harmonize (65, 0, false, pcs, bassPc, -1);
        const int dMinor = harmonize (68, 9, true, pcs, bassPc, -1);   // G# in A minor: E major (V)
        check (eOk && dE == 0 && bOk && dF == 3 && dMinor == 4 && pcs[1] == 8, "armonizar: Mi sobre Do, Si sobre Sol, Fa sobre Fa, Sol# en La menor sobre Mi mayor");
        auto p = make ([] (Processor& q) { q.setRealValue ("mode", 1.0f); });
        run (*p, 0.5, { { 0.01, 0.4, 76, 100 } });
        check (p->satbShown[0].load() == 76 && p->satbShown[3].load() % 12 == 0, "armonizar en vivo: la soprano canta la melodía y el bajo Do"_u);
    }

    // pitch of one singer
    {
        auto p = make ([] (Processor& q)
        {
            q.setRealValue ("mode", 2.0f); q.setRealValue ("size", 1.0f); q.setRealValue ("drift", 0.0f); q.setRealValue ("vibdepth", 0.0f);
            q.setRealValue ("breath", 0.0f); q.setRealValue ("sop_on", 0.0f); q.setRealValue ("alt_on", 0.0f); q.setRealValue ("ten_on", 0.0f);
        });
        auto o = run (*p, 1.0, { { 0.01, 0.9, 48, 100 } });
        const double f = pitchOf (o, (int) (0.4 * sr), (int) (0.3 * sr));
        check (std::abs (f - 130.81) < 1.5, "un bajo en Do3 canta a " + String (f, 2) + " Hz");
    }

    // vowels: A has its first formant high, I has the second one high
    {
        auto sing = [] (float vowel)
        {
            auto p = make ([vowel] (Processor& q)
            {
                q.setRealValue ("mode", 2.0f); q.setRealValue ("vowel", vowel); q.setRealValue ("size", 1.0f); q.setRealValue ("vibdepth", 0.0f);
                q.setRealValue ("sop_on", 0.0f); q.setRealValue ("alt_on", 0.0f); q.setRealValue ("bas_on", 0.0f);
            });
            return run (*p, 1.0, { { 0.01, 0.9, 50, 100 } });   // tenor on D3
        };
        const auto a = sing (0.0f), i = sing (2.0f);
        const int st = (int) (0.4 * sr), len = 8192;
        const float aLow = band (a, st, len, 550, 750), iLow = band (i, st, len, 550, 750);
        const float aHigh = band (a, st, len, 1750, 2000), iHigh = band (i, st, len, 1750, 2000);
        check (aLow > iLow * 2.0f && iHigh > aHigh * 2.0f, "vocales: la A abre el primer formante, la I sube el segundo ("_u
               + String (aLow / jmax (1.0e-6f, iLow), 1) + "x, " + String (iHigh / jmax (1.0e-6f, aHigh), 1) + "x)");
    }

    // ensemble: more singers make a wider sound
    {
        auto corr = [] (int size)
        {
            auto p = make ([size] (Processor& q) { q.setRealValue ("mode", 2.0f); q.setRealValue ("size", (float) size); q.setRealValue ("width", 100.0f);
                                                   q.setRealValue ("alt_on", 0.0f); q.setRealValue ("ten_on", 0.0f); q.setRealValue ("bas_on", 0.0f);
                                                   q.setRealValue ("sop_pan", 0.0f); });
            auto o = run (*p, 1.0, { { 0.01, 0.9, 67, 100 } });
            double lr = 0, ll = 0, rr = 0;
            for (int k = (int) (0.3 * sr); k < (int) (0.9 * sr); ++k) { const double l = o.getSample (0, k), r = o.getSample (1, k); lr += l * r; ll += l * l; rr += r * r; }
            return lr / std::sqrt (ll * rr + 1.0e-12);
        };
        const double one = corr (1), many = corr (12);
        check (one > 0.99 && many < 0.9 && many > 0.2, "ensamble: un cantante al centro, doce abren el estéreo (correlación "_u + String (many, 2) + ")");
    }

    // a consonant: S starts with hiss above 4 kHz, A does not
    {
        auto onset = [] (int syl)
        {
            auto p = make ([syl] (Processor& q) { q.setRealValue ("mode", 2.0f); q.setRealValue ("sylon", 1.0f); q.setRealValue ("syllen", 1.0f);
                                                  q.setRealValue ("syl1", (float) syl); q.setRealValue ("breath", 0.0f); q.setRealValue ("size", 1.0f); });
            auto o = run (*p, 0.5, { { 0.0, 0.45, 60, 100 } });
            return band (o, 1000, 2048, 5000, 8000) / jmax (1.0e-6f, band (o, 12000, 2048, 5000, 8000) + 1.0e-4f);
        };
        const float s = onset (16), a = onset (0);   // "Sa" and "A"
        check (s > a * 3.0f, "sílaba Sa: la S suena antes de la vocal ("_u + String (s, 1) + " contra " + String (a, 1) + ")");
    }

    // articulations: staccato stops by itself, legato glides
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("mode", 2.0f); q.setRealValue ("art", 1.0f); });
        auto o = run (*p, 1.0, { { 0.01, 0.95, 60, 100 } });
        check (tu::rms (o, (int) (0.05 * sr), 2400) > 0.01f && tu::rms (o, (int) (0.6 * sr), 4800) < 1.0e-4f, "staccato se apaga solo con la tecla abajo");

        auto q = make ([] (Processor& r) { r.setRealValue ("mode", 0.0f); r.setRealValue ("art", 3.0f); r.setRealValue ("glide", 200.0f); });
        auto g = run (*q, 1.0, { { 0.0, 0.5, 60, 100 }, { 0.0, 0.5, 64, 100 }, { 0.0, 0.5, 67, 100 },
                                 { 0.5, 0.45, 62, 100 }, { 0.5, 0.45, 65, 100 }, { 0.5, 0.45, 69, 100 } });
        float minRms = 1.0f;
        for (int k = (int) (0.45 * sr); k < (int) (0.6 * sr); k += 480) minRms = jmin (minRms, tu::rms (g, k, 480));
        check (minRms > 0.005f, "legato: el cambio de acorde no corta las voces");
    }

    // keyswitch
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("mode", 2.0f); });
        run (*p, 0.2, { { 0.0, 0.05, 26, 100 } });
        check (p->artShown.load() == (int) marcato, "keyswitch 26 = marcato");
    }

    // multi-out: with separate outputs each section goes to its own bus, the hall to the last one
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("multiout", 1.0f); q.setRealValue ("hall_mix", 50.0f); q.setRealValue ("str_on", 1.0f); }, true);
        const int chans = p->getTotalNumOutputChannels();
        auto o = run (*p, 1.0, { { 0.0, 0.9, 48, 100 }, { 0.0, 0.9, 55, 100 }, { 0.0, 0.9, 64, 100 } });
        bool ok = chans == 16;
        String levels;
        for (int b = 0; b < numOutBuses && ok; ++b)
        {
            const float r = tu::rms (bus (o, b), (int) (0.3 * sr), (int) (0.5 * sr));
            levels << String (r, 3) << " ";
            const bool shouldSound = b >= 1 && b <= 5 || b == 7;
            ok = ok && (shouldSound ? r > 0.002f : r < 1.0e-5f);
        }
        check (ok, "8 salidas: secciones, cuerdas y sala separadas; la mezcla vacía ("_u + levels.trim() + ")");
    }

    // every preset sings
    {
        bool ok = true;
        String quiet;
        Processor ref;
        for (int i = 0; i < ref.getNumPrograms(); ++i)
        {
            auto p = std::make_unique<Processor>();
            p->prepareToPlay (sr, 256);
            p->setCurrentProgram (i);
            auto o = run (*p, 1.5, { { 0.0, 1.4, 48, 100 }, { 0.0, 1.4, 55, 100 }, { 0.0, 1.4, 64, 100 } });
            if (! tu::allFinite (o) || tu::rms (o, (int) (0.3 * sr), (int) sr) < 0.003f || tu::peak (o, 0, o.getNumSamples()) > 1.6f) { ok = false; quiet << p->getProgramName (i) << " "; }
        }
        check (ok, "los presets de fábrica suenan sin pasarse "_u + quiet);
    }
}
static RegisterTest porcupineTestReg ("Porcupine", testPorcupine);

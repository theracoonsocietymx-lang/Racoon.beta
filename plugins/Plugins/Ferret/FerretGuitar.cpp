#include "FerretGuitar.h"

namespace racoon::ferret
{
using namespace juce;

// ===================================================================== instruments
const std::vector<Model>& models()
{
    static const std::vector<Model> m {
        //  name                       decay bright body  bodyB pickHz  Q    pickPos tr  noise
        { "Strat (bobina simple)",      4.0f, 0.78f, 0.0f, 0.0f, 4200.0f, 2.2f, 0.12f,  0, 1.0f },
        { "Humbucker",                  4.5f, 0.66f, 0.0f, 0.0f, 2600.0f, 1.4f, 0.10f,  0, 1.0f },
        { "Funk limpia",                3.2f, 0.85f, 0.0f, 0.0f, 4800.0f, 2.8f, 0.07f,  0, 1.1f },
        { "Apagada disco",              0.9f, 0.70f, 0.0f, 0.0f, 3800.0f, 2.0f, 0.10f,  0, 1.0f },
        { "Acústica de acero",        5.5f, 0.82f, 0.9f, 0.9f, 0.0f,    1.0f, 0.0f,   0, 1.0f },
        { "Acústica de nylon",        4.0f, 0.55f, 0.9f, 0.4f, 0.0f,    1.0f, 0.0f,   0, 0.6f },
        { "Barítono eléctrica",       5.0f, 0.62f, 0.0f, 0.0f, 2400.0f, 1.6f, 0.11f, -5, 1.0f },
    };
    return m;
}

StringArray modelNames() { StringArray s; for (auto& m : models()) s.add (String::fromUTF8 (m.name)); return s; }

StringArray articulationNames()
{
    return { "Sostenido", "Palm mute", "Medio mute", "Nota muerta", "Slide", "Hammer-on / pull-off", "Vibrato", "Bend", "Armónico"_u, "Raspado de púa"_u };
}

// ===================================================================== string
void StringVoice::prepare (double sampleRate)
{
    sr = sampleRate;
    int n = 1;
    while (n < (int) (sr / 20.0) + 8) n <<= 1;
    line.assign ((size_t) n, 0.0f);
    mask = n - 1;
    exc.assign ((size_t) n, 0.0f);
    reset();
}

void StringVoice::reset()
{
    std::fill (line.begin(), line.end(), 0.0f);
    write = 0; lp = 0.0f; energy = 0.0f; chokeGain = 1.0f; chokeStep = 0.0f; excLeft = 0; fret = -1; scrape = false;
    bendNow = bendTarget = 0.0; glideSamples = 0.0;
    rng.setSeed (seedValue);   // the same notes always sound the same
}

float StringVoice::readFrac (double delay) const
{
    const double d = jlimit (2.0, (double) mask - 3.0, delay);
    const int di = (int) d;
    const float f = (float) (d - di);
    auto at = [this] (int k) { return line[(size_t) ((write - k) & mask)]; };
    const float xm1 = at (di - 1), x0 = at (di), x1 = at (di + 1), x2 = at (di + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void StringVoice::pluck (double hz, float vel, Art art, const Model& m, float pickPos, float hardness, bool legato, double slideFromHz, float bendSemis)
{
    scrape = art == pickScrape;
    chokeGain = 1.0f; chokeStep = 0.0f;
    if (scrape) { excLeft = (int) (0.35 * sr); excPos = 0; scrapeFreq = 5000.0f; return; }

    if (art == harmonic) hz *= 2.0;
    const double newPeriod = sr / jlimit (20.0, sr * 0.25, hz);
    if ((art == slide || legato) && slideFromHz > 0.0 && active())
    {
        // the finger moves along the string: the pitch glides, the string keeps ringing
        period = sr / slideFromHz;
        targetPeriod = newPeriod;
        glideSamples = art == slide ? 0.08 * sr : 0.004 * sr;
    }
    else { period = targetPeriod = newPeriod; glideSamples = 0.0; }

    // how long it rings
    float t60 = m.decay * std::pow (82.0f / (float) hz, 0.3f);
    switch (art)
    {
        case palmMute: t60 = 0.12f; break;
        case halfMute: t60 = 0.45f; break;
        case deadNote: t60 = 0.02f; break;
        case harmonic: t60 *= 0.8f; break;
        default: break;
    }
    loss = (float) std::pow (10.0, -3.0 / (jmax (0.01f, t60) * hz));
    bright = art == palmMute ? m.bright * 0.35f : art == halfMute ? m.bright * 0.6f : art == harmonic ? 0.95f : m.bright;
    bendTarget = art == bend ? bendSemis : 0.0;
    bendNow = 0.0;
    bendRate = bendTarget / (0.12 * sr);
    vibPhase = 0.0;

    if (legato && art != slide && active()) return;   // hammer-on / pull-off: no new pick
    // excitation: a noise burst one period long, softened by the pick hardness and combed by where the pick hits
    const int len = jlimit (8, mask - 4, (int) period);
    float s = 0.0f;
    const float soft = 1.0f - jlimit (0.05f, 1.0f, hardness * m.pluckNoise);
    for (int i = 0; i < len; ++i)
    {
        const float w = rng.nextFloat() * 2.0f - 1.0f;
        s = w + (s - w) * soft * 0.9f;
        exc[(size_t) i] = s;
    }
    if (art != harmonic)
    {
        const int pp = jlimit (1, len - 1, (int) (pickPos * len));
        for (int i = len - 1; i >= pp; --i) exc[(size_t) i] -= exc[(size_t) (i - pp)];
    }
    float peak = 1.0e-6f;
    for (int i = 0; i < len; ++i) peak = jmax (peak, std::abs (exc[(size_t) i]));
    const float amp = vel * 0.5f / peak * (art == deadNote ? 0.6f : 1.0f);
    // a re-pick damps what was ringing
    for (auto& x : line) x *= 0.25f;
    for (int i = 0; i < len; ++i) line[(size_t) ((write - len + i) & mask)] += exc[(size_t) i] * amp;
    energy = jmax (energy, vel * 0.3f);
}

void StringVoice::choke (float ms)
{
    chokeStep = 1.0f / jmax (1.0f, (float) (ms * 0.001 * sr));
}

void StringVoice::render (float* out, int n, const Model& m, float vibDepth, float pickupPos)
{
    if (scrape)
    {
        // the pick slides down the wound strings: noise through a falling band
        for (int i = 0; i < n && excLeft > 0; ++i, --excLeft, ++excPos)
        {
            const float env = std::sin (MathConstants<float>::pi * (float) excPos / (float) (0.35 * sr));
            scrapeFreq = jmax (600.0f, scrapeFreq * 0.99993f);
            const float g = std::tan (MathConstants<float>::pi * scrapeFreq / (float) sr), k = 0.4f;
            const float x = (rng.nextFloat() * 2.0f - 1.0f) * env * 0.4f;
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - scrapeState[1], v1 = a1 * scrapeState[0] + a2 * v3, v2 = scrapeState[1] + a2 * scrapeState[0] + a3 * v3;
            scrapeState[0] = 2.0f * v1 - scrapeState[0]; scrapeState[1] = 2.0f * v2 - scrapeState[1];
            out[i] += v1;
        }
        if (excLeft <= 0) scrape = false;
        return;
    }
    if (! active()) return;
    const float c = (1.0f - bright) * 0.75f;
    const double vibInc = 5.5 / sr;
    for (int i = 0; i < n; ++i)
    {
        if (glideSamples > 0.0) { period += (targetPeriod - period) / glideSamples; glideSamples -= 1.0; if (glideSamples < 1.0) { period = targetPeriod; glideSamples = 0.0; } }
        if (bendNow < bendTarget) bendNow = jmin (bendTarget, bendNow + bendRate);
        double p = period / std::pow (2.0, (bendNow + offsetSemis) / 12.0);
        if (vibDepth > 0.0f)
        {
            vibPhase += vibInc;
            p /= std::pow (2.0, vibDepth * std::sin (MathConstants<double>::twoPi * vibPhase) / 12.0);
        }
        float y = readFrac (p);
        lp = y + (lp - y) * c;
        y = lp * loss;
        if (chokeStep > 0.0f) { chokeGain = jmax (0.0f, chokeGain - chokeStep); y *= chokeGain; }
        line[(size_t) write] = y;
        write = (write + 1) & mask;
        // electric pickups hear the string at one point: a comb on the harmonics
        float o = y;
        if (m.pickupHz > 0.0f) o = y - 0.6f * readFrac (jmax (2.0, p * pickupPos));
        out[i] += o;
        energy = std::abs (y) + (energy - std::abs (y)) * 0.9995f;
    }
    if (chokeStep > 0.0f && chokeGain <= 0.0f) { energy = 0.0f; std::fill (line.begin(), line.end(), 0.0f); chokeStep = 0.0f; fret = -1; }
    if (energy < 1.0e-5f) fret = -1;
}

// ===================================================================== body and pickup
void Body::prepare (double sampleRate) { sr = sampleRate; reset(); }
void Body::reset() { for (auto& r : modes) r.z1 = r.z2 = 0.0f; pk1 = pk2 = tone = 0.0f; lastModel = nullptr; }

void Body::design (const Model& m)
{
    static const float hz[8] { 98, 190, 245, 380, 520, 850, 1250, 2600 };
    static const float gain[8] { 1.0f, 0.8f, 0.9f, 0.6f, 0.5f, 0.35f, 0.3f, 0.2f };
    for (int i = 0; i < 8; ++i)
    {
        const double w = MathConstants<double>::twoPi * hz[i] / sr, q = 12.0 + i * 2.0;
        const double alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
        modes[i].b0 = (float) (alpha / a0);
        modes[i].a1 = (float) (-2.0 * std::cos (w) / a0);
        modes[i].a2 = (float) ((1.0 - alpha) / a0);
        modes[i].g = gain[i] * (i >= 5 ? m.bodyBright : 1.0f) * 2.5f;
    }
    lastModel = &m;
}

float Body::process (float x, const Model& m, float pickupTone)
{
    if (lastModel != &m) design (m);
    float y = x;
    if (m.body > 0.0f)
    {
        float sum = 0.0f;
        for (auto& r : modes)
        {
            const float o = r.b0 * x + r.z1;
            r.z1 = -r.a1 * o + r.z2;
            r.z2 = -r.b0 * x - r.a2 * o;
            sum += o * r.g;
        }
        y = x * (1.0f - m.body * 0.5f) + sum * m.body;
    }
    if (m.pickupHz > 0.0f)
    {
        // the pickup's resonance (a coil and the cable) and the tone knob
        const float g = std::tan (MathConstants<float>::pi * m.pickupHz / (float) sr), k = 1.0f / m.pickupQ;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = y - pk2, v1 = a1 * pk1 + a2 * v3, v2 = pk2 + a2 * pk1 + a3 * v3;
        pk1 = 2.0f * v1 - pk1; pk2 = 2.0f * v2 - pk2;
        y = v2 + v1 * k * 0.6f;   // low-pass with a resonant bump
    }
    const float cut = 1500.0f * std::pow (10.0f, pickupTone);   // tone 0..1 -> 1.5 .. 15 kHz
    const float c = std::exp (-MathConstants<float>::twoPi * cut / (float) sr);
    tone = y + (tone - y) * c;
    return tone;
}

// ===================================================================== fretboard
const std::vector<Tuning>& tunings()
{
    static const std::vector<Tuning> t {
        { "Estándar (E A D G B E)", { 40, 45, 50, 55, 59, 64 } }, { "Drop D", { 38, 45, 50, 55, 59, 64 } },
        { "DADGAD", { 38, 45, 50, 55, 57, 62 } }, { "Open G", { 38, 43, 50, 55, 59, 62 } }, { "Open E", { 40, 47, 52, 56, 59, 64 } },
        { "Medio tono abajo", { 39, 44, 49, 54, 58, 63 } }, { "Barítono (B)", { 35, 40, 45, 50, 54, 59 } } };
    return t;
}

bool Voicing::valid() const { for (int f : fret) if (f >= 0) return true; return false; }
int Voicing::lowest (const int* open) const { for (int s = 0; s < numStrings; ++s) if (fret[s] >= 0) return open[s] + fret[s]; return -1; }

StringArray chordTypeNames()
{
    return { "", "m", "dim", "aug", "sus2", "sus4", "7", "maj7", "m7", "m7b5", "dim7", "6", "m6", "9", "maj9", "m9", "add9", "7sus4", "11", "13", "5" };
}

static const std::vector<std::vector<int>>& templates()
{
    static const std::vector<std::vector<int>> t {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 }, { 0, 2, 7 }, { 0, 5, 7 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 },
        { 0, 3, 6, 10 }, { 0, 3, 6, 9 }, { 0, 4, 7, 9 }, { 0, 3, 7, 9 }, { 0, 4, 7, 10, 2 }, { 0, 4, 7, 11, 2 }, { 0, 3, 7, 10, 2 },
        { 0, 4, 7, 2 }, { 0, 5, 7, 10 }, { 0, 7, 10, 2, 5 }, { 0, 4, 7, 10, 2, 9 }, { 0, 7 } };
    return t;
}

String ChordInfo::name() const
{
    if (root < 0) return {};
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    String s = String (names[root]) + chordTypeNames()[type];
    if (bass >= 0 && bass != root) s << "/" << names[bass];
    return s;
}

ChordInfo recognise (const int* notes, int count)
{
    ChordInfo c;
    if (count <= 0) return c;
    bool present[12] {};
    int low = 128;
    for (int i = 0; i < count; ++i) { present[((notes[i] % 12) + 12) % 12] = true; low = jmin (low, notes[i]); }
    c.bass = ((low % 12) + 12) % 12;
    int best = -1000;
    for (int r = 0; r < 12; ++r)
        for (size_t t = 0; t < templates().size(); ++t)
        {
            bool in[12] {};
            int matched = 0, missing = 0;
            for (int iv : templates()[t])
            {
                const int pc = (r + iv) % 12;
                in[pc] = true;
                if (present[pc]) ++matched;
                else missing += iv == 7 ? 1 : 3;   // a missing fifth hardly matters
            }
            int extra = 0;
            for (int pc = 0; pc < 12; ++pc) if (present[pc] && ! in[pc]) ++extra;
            if (! present[r]) continue;
            int score = matched * 2 - missing - extra * 3 + (r == c.bass ? 1 : 0) - (int) templates()[t].size() / 3;
            if (count == 1 && t != 0) score -= 10;
            if (score > best) { best = score; c.root = r; c.type = (int) t; }
        }
    for (int pc = 0; pc < 12; ++pc) if (c.root >= 0 && present[pc]) c.pcs[c.numPcs++] = pc;
    // the chord's own tones (including a missing fifth, so voicings can use it)
    if (c.root >= 0)
    {
        c.numPcs = 0;
        for (int iv : templates()[(size_t) c.type]) c.pcs[c.numPcs++] = (c.root + iv) % 12;
    }
    return c;
}

std::pair<int, int> placeNote (int note, const int* open, Strategy st, int hand, const bool* busy)
{
    int bestS = -1, bestF = -1;
    double best = 1.0e9;
    for (int s = 0; s < numStrings; ++s)
    {
        const int f = note - open[s];
        if (f < 0 || f > numFrets || (busy != nullptr && busy[s])) continue;
        double score = 0.0;
        switch (st)
        {
            case Strategy::open: score = f + (f == 0 ? -2.0 : 0.0); break;
            case Strategy::nearHand: score = f == 0 ? 0.5 : std::abs (f - hand); break;
            case Strategy::leastMovement: score = (f == 0 ? 0.5 : std::abs (f - hand)) + 0.3 * f; break;
            case Strategy::lowStrings: score = s * 10.0 + (f > 15 ? 30.0 : 0.0) + f * 0.1; break;
            case Strategy::highStrings: score = (numStrings - 1 - s) * 10.0 + (f > 15 ? 30.0 : 0.0) + f * 0.1; break;
        }
        if (score < best) { best = score; bestS = s; bestF = f; }
    }
    return { bestS, bestF };
}

namespace
{
struct VoicingSearch
{
    const ChordInfo& c;
    const int* open;
    int position, wantBass;
    int cand[numStrings][8] {}, numCand[numStrings] {};
    int f[numStrings] {};
    double bestScore = 1.0e9;
    Voicing best;

    void walk (int s, int minF, int maxF)
    {
        if (maxF >= 0 && maxF - minF > 3) return;   // the hand cannot stretch further
        if (s == numStrings) { finish(); return; }
        for (int k = 0; k < numCand[s]; ++k)
        {
            const int x = cand[s][k];
            f[s] = x;
            if (x > 0) walk (s + 1, jmin (minF, x), jmax (maxF, x));
            else walk (s + 1, minF, maxF);
        }
    }

    void finish()
    {
        int sounding = 0, interior = 0, first = -1, last = -1;
        bool has[12] {};
        for (int i = 0; i < numStrings; ++i)
            if (f[i] >= 0)
            {
                ++sounding;
                if (first < 0) first = i;
                last = i;
                has[(open[i] + f[i]) % 12] = true;
            }
        if (first < 0 || sounding < jmin (3, c.numPcs)) return;
        for (int i = first; i <= last; ++i) if (f[i] < 0) ++interior;
        // the lowest note must be the bass of the chord
        if (((open[first] + f[first]) % 12) != wantBass) return;
        int missing = 0;
        for (int i = 0; i < c.numPcs; ++i) if (! has[c.pcs[i]]) ++missing;
        double avg = 0.0; int fretted = 0;
        for (int i = 0; i < numStrings; ++i) if (f[i] > 0) { avg += f[i]; ++fretted; }
        avg = fretted > 0 ? avg / fretted : position;
        const double score = missing * 10.0 + (numStrings - sounding) * 1.5 + interior * 6.0 + std::abs (avg - position) * 0.5;
        if (score < bestScore) { bestScore = score; for (int i = 0; i < numStrings; ++i) best.fret[i] = f[i]; }
    }
};
}

// no allocation: called on the audio thread when the chord changes
Voicing findVoicing (const ChordInfo& c, const int* open, int position, bool allowOpen)
{
    if (c.root < 0) return {};
    auto inChord = [&c] (int note) { const int pc = ((note % 12) + 12) % 12; for (int i = 0; i < c.numPcs; ++i) if (c.pcs[i] == pc) return true; return false; };
    VoicingSearch v { c, open, position, c.bass >= 0 ? c.bass : c.root };
    // per string: muted, open, and the frets of chord tones around the position
    const int lo = jmax (1, position - 1), hi = jmin (numFrets, position + 3);
    for (int s = 0; s < numStrings; ++s)
    {
        auto& n = v.numCand[s];
        v.cand[s][n++] = -1;
        if (allowOpen && inChord (open[s])) v.cand[s][n++] = 0;
        for (int fr = lo; fr <= hi && n < 8; ++fr) if (inChord (open[s] + fr)) v.cand[s][n++] = fr;
    }
    v.walk (0, 99, -1);
    if (! v.best.valid() && c.bass >= 0 && c.bass != c.root)
    {
        ChordInfo rootPos = c;   // an inversion that does not fit: play it with the root in the bass
        rootPos.bass = -1;
        return findVoicing (rootPos, open, position, allowOpen);
    }
    return v.best;
}

// ===================================================================== patterns
const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> p {
        { "Funk", "Rasgueo funk", "D.x.D.xUD.x.DUxU" }, { "Funk", "Funk con muertas", "x.UxD.xU.UxUD.xu" }, { "Funk", "Chicken scratch", "xUxUDuxUxUDuxuDU" },
        { "Disco", "Octavos disco", "D.U.D.U.D.U.D.U." }, { "Disco", "Disco con golpes", "D.UxD.UxD.UxD.Ux" }, { "Disco", "Disco de dieciseisavos", "xUDUxUDuxUDUxUdU" },
        { "Indie Dance", "Negras", "D-.-D-.-D-.-D-.-" }, { "Indie Dance", "Palm mute en octavos", "m.m.m.m.m.m.m.m." }, { "Indie Dance", "Galope", "mmmUmmmUmmmUmmDU" },
        { "Nu-Disco", "Contratiempos", "..U...U...U...U." }, { "Nu-Disco", "Contratiempos con golpe", "x.U.xUU.x.U.xUUx" }, { "Nu-Disco", "Nu-disco de 16", "xUdUxUdUxUdUxUDU" },
        { "Rock", "Octavos", "D-D-D-D-D-D-D-D-" }, { "Rock", "Abajo-arriba", "D-DUD-DUD-DUD-DU" }, { "Rock", "Galope con acento", "mmmmAmmmmmmmAmUm" },
        { "Post-Punk", "Octavos hacia abajo", "D.D.D.D.D.D.D.D." }, { "Post-Punk", "Dieciseisavos hacia abajo", "DDDDDDDDDDDDDDDD" }, { "Post-Punk", "Arpegio frío", "o.o.o.o.oooo.o.o" },
        { "Dark Disco", "Mute oscuro", "m.m.m.m.m.m.m.m." }, { "Dark Disco", "Golpes y contratiempos", ".x.U.x.U.x.U.x.U" }, { "Dark Disco", "Motor", "m.mUm.mUm.mUmUmU" },
    };
    return p;
}

StringArray styleNames() { return { "Funk", "Disco", "Indie Dance", "Nu-Disco", "Rock", "Post-Punk", "Dark Disco", "Usuario" }; }

const Pattern* patternFor (int style, int complexity)
{
    const auto names = styleNames();
    if (style < 0 || style >= names.size() - 1) return nullptr;
    int k = 0;
    for (auto& p : patterns())
        if (names[style] == p.style)
        {
            if (k == jlimit (0, 2, complexity)) return &p;
            ++k;
        }
    return nullptr;
}
}

#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <algorithm>

// "Aleatoria": a random sequence that follows the rules of the instrument it is for, not random notes.
// Every part lives in 4-bar phrases (bar 4 answers or prepares the next phrase), one chord per bar from one
// progression shared by the whole song, and each instrument has its own craft:
//   bajo       the root on the one of every chord, a one-bar cell repeated over the harmony, the fifth and the
//              octave as colour, an approach note into the next chord, never two notes at once; rhythm by style
//   pad        whole-bar chords, voice-led, common tones held across the bar line (a pad does not re-attack)
//   coro       four voices (soprano, alto, tenor, bass) in their ranges, no crossing, no parallel fifths or octaves,
//              held notes and a breath at the end of each phrase
//   acordes    stabs: a one-bar rhythm of the style repeated, close voicing, an anticipation at the end of a phrase
//   guitarra   strums (down low to high, up high to low) or fingerpicking, open-position voicings
//   arpegio    the chord's notes in a fixed order, restarted on every chord
//   melodia    a motif and its answer (A A' A B), chord tones on the beats, steps more than leaps, a rest to breathe,
//              a long note at the end of the phrase
//   voz        a melody a person can sing: A3 to D5, syllables of an eighth or longer, rests between lines
//   textura    a pedal: the tonic and the fifth held across the phrase, an upper note that comes in late
//   bateria    the style's groove (four on the floor, half time, two-step, dembow, boom bap...), fills only at
//              the end of a phrase
//   percusion  shaker, congas, bongos and cowbell only (no kick, no snare): the layer on top of the drums
// The same file lives in Racoon (Source/ui/Aleatorio.h) and in the family (Shared/Aleatorio.h): keep them equal.
namespace racoon::aleatorio
{
enum class Rol { bateria, bajo, acordes, melodia, arpegio, textura, pad, coro, guitarra, percusion, voz };

// the groove family of the song
enum class Estilo { cuatro, afro, trap, dnb, latino, boombap, ambient };

inline juce::String nombreRol (Rol r)
{
    switch (r)
    {
        case Rol::bateria:   return juce::String::fromUTF8 ("batería");
        case Rol::bajo:      return "bajo";
        case Rol::acordes:   return "acordes";
        case Rol::melodia:   return juce::String::fromUTF8 ("melodía");
        case Rol::arpegio:   return "arpegio";
        case Rol::textura:   return "textura";
        case Rol::pad:       return "pad";
        case Rol::coro:      return "coro";
        case Rol::guitarra:  return "guitarra";
        case Rol::percusion: return juce::String::fromUTF8 ("percusión");
        case Rol::voz:       return "voz";
    }
    return {};
}

inline juce::String nombreEstilo (Estilo e)
{
    switch (e)
    {
        case Estilo::cuatro:  return "four on the floor";
        case Estilo::afro:    return "afro";
        case Estilo::trap:    return "trap (medio tiempo)";
        case Estilo::dnb:     return "drum & bass";
        case Estilo::latino:  return "dembow";
        case Estilo::boombap: return "boom bap";
        case Estilo::ambient: return "ambient";
    }
    return {};
}

// the style of a Racoon genre (its id), or of a tempo when there is no genre
inline Estilo estiloDe (const juce::String& genreId, double bpm)
{
    const auto g = genreId.toLowerCase();
    for (auto* s : { "afrohouse", "organichouse", "amapiano", "afrobeats" }) if (g == s) return Estilo::afro;
    for (auto* s : { "trap", "drill", "phonk", "dubstep", "futurebass", "jerseyclub" }) if (g == s) return Estilo::trap;
    for (auto* s : { "dnb", "liquid", "jungle", "breakbeat" }) if (g == s) return Estilo::dnb;
    for (auto* s : { "reggaeton", "dembow", "moombahton", "cumbia", "dancehall" }) if (g == s) return Estilo::latino;
    for (auto* s : { "hiphop", "lofi", "rnb", "downtempo" }) if (g == s) return Estilo::boombap;
    if (g == "ambient") return Estilo::ambient;
    if (g.isNotEmpty()) return Estilo::cuatro;
    if (bpm >= 160.0) return Estilo::dnb;
    if (bpm > 0.0 && bpm < 92.0) return Estilo::boombap;
    return Estilo::cuatro;
}

struct Nota { double start = 0.0, length = 0.25; int pitch = 60, velocity = 100; };

// the notes that play each drum (General MIDI unless the kit says otherwise)
struct Bateria
{
    int kick = 36, snare = 38, clap = 39, closedHat = 42, openHat = 46, rim = 37, tom = 45, perc = 56;
    int shaker = 70, congaHi = 63, congaLo = 64, bongoHi = 60, bongoLo = 61, cowbell = 56;
};

struct Opciones
{
    Rol rol = Rol::melodia;
    int bars = 4;
    int beatsPerBar = 4;
    int root = 9;              // 0 = Do ... 11 = Si (La)
    bool minor = true;
    juce::int64 seed = 1;
    Bateria drums;
    Estilo estilo = Estilo::cuatro;
    std::vector<int> progresion;   // scale degrees, one per bar, repeated (empty: one of the usual ones)
    // what the clip already has: when it is not empty, Aleatoria varies it instead of starting over. The bars that
    // were silent stay silent, the rhythm, the register, the size of the chords and the harmony stay; what changes
    // is what the rules of the instrument allow (another voicing, another bass colour, another melodic contour...)
    std::vector<Nota> molde;
};

namespace detail
{
inline const std::array<int, 7>& scaleOf (bool minor)
{
    static const std::array<int, 7> maj { 0, 2, 4, 5, 7, 9, 11 }, min { 0, 2, 3, 5, 7, 8, 10 };
    return minor ? min : maj;
}
// the pitch of a scale degree (0 = the tonic, 7 = the tonic an octave up) above `base` (a Do)
inline int degreePitch (int base, int root, bool minor, int degree)
{
    const int oct = (int) std::floor (degree / 7.0);
    const int d = degree - oct * 7;
    return base + root + scaleOf (minor)[(size_t) d] + 12 * oct;
}
inline int clampPitch (int p) { return juce::jlimit (0, 127, p); }
inline int pc (int p) { return ((p % 12) + 12) % 12; }

// the usual progressions of dance music, four chords
inline std::vector<int> progression (juce::Random& rng, bool minor)
{
    static const int mins[][4] { { 0, 0, 3, 4 }, { 0, 3, 4, 0 }, { 0, 3, 0, 4 }, { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 0, 0, 5, 1 } };   // dark: i, iv, v, VI (and ii° once)
    static const int majs[][4] { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 0, 3, 4, 3 }, { 0, 3, 0, 4 }, { 5, 3, 0, 4 }, { 0, 2, 3, 4 } };
    const auto* p = minor ? mins[rng.nextInt (6)] : majs[rng.nextInt (6)];
    return { p[0], p[1], p[2], p[3] };
}

// the chord of each bar: one per bar, never faster (a 1-bar clip is the tonic, a 2-bar clip the first two)
struct Bar { double start, length; int degree, next; bool phraseEnd, last; int inPhrase; };
inline std::vector<Bar> barsOf (int bars, int bpb, const std::vector<int>& prog)
{
    std::vector<Bar> out;
    const int phrase = juce::jmin (4, bars);
    for (int b = 0; b < bars; ++b)
    {
        const int d = prog[(size_t) (b % (int) prog.size())];
        const int n = prog[(size_t) ((b + 1) % (int) prog.size())];
        out.push_back ({ b * (double) bpb, (double) bpb, d, n, (b + 1) % phrase == 0 || b == bars - 1, b == bars - 1, b % phrase });
    }
    return out;
}

inline int humanVel (juce::Random& rng, int v, int spread = 8) { return juce::jlimit (1, 127, v + rng.nextInt ({ -spread, spread + 1 })); }

// the pitch classes of the chord on a degree (triad, or with its seventh)
inline std::vector<int> chordPcs (const Opciones& o, int degree, bool seventh)
{
    std::vector<int> pcs;
    for (int k : { 0, 2, 4 }) pcs.push_back (pc (degreePitch (0, o.root, o.minor, degree + k)));
    if (seventh) pcs.push_back (pc (degreePitch (0, o.root, o.minor, degree + 6)));
    return pcs;
}

// close voicing of a chord between lo and hi, as near as possible to the previous one
inline std::vector<int> voiceLead (const std::vector<int>& pcs, int lo, int hi, const std::vector<int>& prev)
{
    std::vector<int> best;
    int bestCost = 1 << 30;
    const int n = (int) pcs.size();
    for (int inv = 0; inv < n; ++inv)
        for (int base = lo; base < lo + 12; ++base)
        {
            if (pc (base) != pcs[(size_t) inv]) continue;
            std::vector<int> v { base };
            for (int k = 1; k < n; ++k)
            {
                int x = v.back() + 1;
                while (pc (x) != pcs[(size_t) ((inv + k) % n)]) ++x;
                v.push_back (x);
            }
            if (v.back() > hi) continue;
            int cost = 0;
            if (prev.empty()) { const int mid = (lo + hi) / 2; for (int x : v) cost += std::abs (x - mid); }
            else for (int x : v) { int d = 99; for (int y : prev) d = juce::jmin (d, std::abs (x - y)); cost += d; }
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    if (best.empty()) for (int k = 0; k < n; ++k) { int x = lo; while (pc (x) != pcs[(size_t) k]) ++x; best.push_back (x); }
    return best;
}

// a one-bar rhythm (sixteenth steps 0..15) out of a list
inline const std::vector<int>& pick (juce::Random& rng, const std::vector<std::vector<int>>& list)
{
    return list[(size_t) rng.nextInt ((int) list.size())];
}
inline std::vector<int> fit (const std::vector<int>& cell, int steps)
{
    std::vector<int> v;
    for (int s : cell) if (s < steps) v.push_back (s);
    if (v.empty()) v.push_back (0);
    return v;
}
}

// ============================================================================================================ bass
namespace detail
{
inline void bajo (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    const double step = 0.25;
    std::vector<std::vector<int>> cells;
    bool legato = false;
    switch (o.estilo)
    {
        case Estilo::cuatro:  cells = { { 2, 6, 10, 14 }, { 2, 3, 6, 7, 10, 11, 14, 15 }, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, { 0, 3, 6, 10, 14 }, { 3, 6, 10, 14 } }; break;
        case Estilo::afro:    cells = { { 0, 3, 6, 10, 12 }, { 0, 6, 10 }, { 3, 6, 11, 14 }, { 0, 3, 8, 11, 14 } }; break;
        case Estilo::trap:    cells = { { 0, 7, 10 }, { 0, 3, 10, 11 }, { 0, 8, 11 }, { 0, 6, 10, 14 } }; legato = true; break;
        case Estilo::dnb:     cells = { { 0, 10 }, { 0, 6, 10 }, { 0, 3, 10, 14 } }; legato = true; break;
        case Estilo::latino:  cells = { { 0, 3, 8, 11 }, { 0, 6, 8, 14 }, { 0, 3, 6, 8, 11, 14 } }; break;
        case Estilo::boombap: cells = { { 0, 6, 8, 11 }, { 0, 4, 8, 12 }, { 0, 7, 10 } }; break;
        case Estilo::ambient: cells = { { 0 }, { 0, 8 } }; legato = true; break;
    }
    const auto cell = fit (pick (rng, cells), steps);
    // what each note of the cell plays over the chord, chosen once (so every bar sounds like the same line):
    // 0 the root, 7 the octave, 4 the fifth. The first note of the bar is always the root
    std::vector<int> colour;
    for (size_t k = 0; k < cell.size(); ++k)
    {
        const int r = rng.nextInt (100);
        colour.push_back (k == 0 ? 0 : (r < 60 ? 0 : (r < 82 ? 7 : 4)));
    }
    const int base = o.root >= 5 ? 24 : 36;      // the tonic between Fa1 and Mi2
    const int tonic = base + o.root;
    auto place = [&] (int deg)
    {
        int p = degreePitch (base, o.root, o.minor, deg);
        while (p > tonic + 12) p -= 12;
        while (p < tonic - 5) p += 12;
        return p;
    };
    for (auto& bar : bars)
    {
        const int total = (int) bar.length * 4;
        for (size_t k = 0; k < cell.size(); ++k)
        {
            const double t = bar.start + cell[k] * step;
            const double next = k + 1 < cell.size() ? bar.start + cell[k + 1] * step : bar.start + total * step;
            int p = place (bar.degree + colour[k]);
            if (colour[k] == 7)                                          // the octave of this chord's root, not of the scale's
            {
                p = place (bar.degree) + 12;
                if (p > 52) p -= 24;                                     // too high for a bass (over Mi3): the octave below instead
                if (p < tonic - 12) p += 12;
            }
            // the end of a phrase walks into the next chord: the last note becomes the step above or below its root
            if (bar.phraseEnd && ! bar.last && k + 1 == cell.size() && k > 0 && bar.next != bar.degree)
            {
                const int target = place (bar.next);
                const int above = place (bar.next + 1), below = place (bar.next - 1);
                p = std::abs (above - target) <= std::abs (below - target) && rng.nextBool() ? above : below;
            }
            const double len = legato ? (next - t) * 0.98 : juce::jmax (step, (next - t) * 0.8);
            out.push_back ({ t, juce::jmin (len, next - t), clampPitch (p), humanVel (rng, cell[k] % 4 == 0 ? 112 : 100, 6) });
        }
    }
}

// ============================================================================================================ pad
inline void pad (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, std::vector<Nota>& out)
{
    const bool seventh = rng.nextInt (100) < 60;
    std::vector<int> prev;
    std::vector<size_t> held;                        // index in `out` of each sounding note
    int prevPhrase = -1;
    for (size_t b = 0; b < bars.size(); ++b)
    {
        auto& bar = bars[b];
        auto pcs = chordPcs (o, bar.degree, seventh);
        auto v = voiceLead (pcs, 52, 76, prev);
        const int phrase = (int) b / juce::jmax (1, juce::jmin (4, (int) bars.size()));
        std::vector<size_t> now;
        for (int p : v)
        {
            // a tone the last chord had keeps ringing (inside a phrase): the pad only moves the voices that change
            bool tied = false;
            if (phrase == prevPhrase)
                for (size_t h : held)
                    if (out[h].pitch == p) { out[h].length += bar.length; now.push_back (h); tied = true; break; }
            if (tied) continue;
            const int vel = 62 + bar.inPhrase * 4;
            out.push_back ({ bar.start, bar.length * 0.99, clampPitch (p), humanVel (rng, vel, 4) });
            now.push_back (out.size() - 1);
        }
        held = now;
        prev = v;
        prevPhrase = phrase;
    }
}

// ============================================================================================================ choir
inline void coro (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, std::vector<Nota>& out)
{
    // ranges: soprano Do4-Sol5, alto Sol3-Do5, tenor Do3-Sol4, bass Mi2-Do4
    const int lo[4] { 40, 48, 55, 60 }, hi[4] { 60, 67, 72, 79 };
    std::array<int, 4> prev { 0, 0, 0, 0 };
    bool first = true;
    std::array<size_t, 4> idx {};
    int prevPhrase = -1;
    for (size_t b = 0; b < bars.size(); ++b)
    {
        auto& bar = bars[b];
        const auto pcs = chordPcs (o, bar.degree, false);         // root, third, fifth
        std::array<std::vector<int>, 4> opts;
        for (int v = 0; v < 4; ++v)
            for (int p = lo[v]; p <= hi[v]; ++p)
            {
                const int c = pc (p);
                if (v == 0 ? c == pcs[0] : (c == pcs[0] || c == pcs[1] || c == pcs[2])) opts[(size_t) v].push_back (p);
            }
        std::array<int, 4> best { 0, 0, 0, 0 };
        int bestCost = 1 << 30;
        for (int B : opts[0]) for (int T : opts[1]) for (int A : opts[2]) for (int S : opts[3])
        {
            if (! (B < T && T < A && A < S)) continue;                       // no crossing, no unison
            if (S - A > 12 || A - T > 12 || T - B > 19) continue;            // spacing
            const std::array<int, 4> v { B, T, A, S };
            bool third = false, fifth = false;
            for (int x : v) { third = third || pc (x) == pcs[1]; fifth = fifth || pc (x) == pcs[2]; }
            if (! third) continue;                                            // a chord without its third is not a chord
            int cost = fifth ? 0 : 6;
            if (first) cost += std::abs (S - 72) + std::abs (A - 64) + std::abs (T - 57) + std::abs (B - 48);
            else
            {
                for (int k = 0; k < 4; ++k) cost += std::abs (v[(size_t) k] - prev[(size_t) k]) * (k == 0 ? 1 : 3);
                // parallel fifths and octaves between any two voices
                bool parallel = false;
                for (int i = 0; i < 4 && ! parallel; ++i)
                    for (int j = i + 1; j < 4; ++j)
                    {
                        const int before = (prev[(size_t) j] - prev[(size_t) i]) % 12, after = (v[(size_t) j] - v[(size_t) i]) % 12;
                        const bool moved = v[(size_t) i] != prev[(size_t) i] || v[(size_t) j] != prev[(size_t) j];
                        if (moved && before == after && (after == 7 || after == 0)) { parallel = true; break; }
                    }
                if (parallel) cost += 200;
                if (std::abs (S - prev[3]) > 5) cost += 20;                  // the top line sings, it does not jump
            }
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
        if (bestCost == (1 << 30))
        {
            // no four-part answer in range (a very low or high key): the triad, close, over its root
            const auto v = voiceLead (pcs, 55, 74, {});
            best = { juce::jlimit (40, 60, v[0] - 12), v[0], v[1], v[2] };
        }
        const int phrase = (int) b / juce::jmax (1, juce::jmin (4, (int) bars.size()));
        // held notes: a voice that keeps its note inside a phrase is tied, not sung again; the phrase's last bar
        // ends an eighth early so the choir breathes
        for (int k = 0; k < 4; ++k)
        {
            const double len = bar.phraseEnd ? bar.length - 0.5 : bar.length;
            if (! first && phrase == prevPhrase && best[(size_t) k] == prev[(size_t) k])
            {
                out[idx[(size_t) k]].length = bar.start + len - out[idx[(size_t) k]].start;
                continue;
            }
            const int vel = 66 + bar.inPhrase * 5 + (k == 3 ? 6 : 0);
            out.push_back ({ bar.start, len, clampPitch (best[(size_t) k]), humanVel (rng, vel, 3) });
            idx[(size_t) k] = out.size() - 1;
        }
        prev = best;
        first = false;
        prevPhrase = phrase;
    }
}

// ============================================================================================================ stabs
inline void acordes (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    std::vector<std::vector<int>> cells;
    double gate = 0.3;
    switch (o.estilo)
    {
        case Estilo::cuatro:  cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 }, { 3, 6, 10 }, { 2, 10 } }; break;
        case Estilo::afro:    cells = { { 0, 3, 7, 10, 12 }, { 3, 6, 11, 14 }, { 0, 6, 10 } }; break;
        case Estilo::trap:    cells = { { 0, 8 }, { 0 } }; gate = 1.9; break;
        case Estilo::dnb:     cells = { { 0, 10 }, { 0 } }; gate = 1.4; break;
        case Estilo::latino:  cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 } }; break;
        case Estilo::boombap: cells = { { 0, 7, 10 }, { 0, 6, 12 } }; gate = 0.8; break;
        case Estilo::ambient: cells = { { 0 } }; gate = 3.9; break;
    }
    const auto cell = fit (pick (rng, cells), steps);
    const bool seventh = rng.nextBool();
    std::vector<int> prev, nextV;
    for (auto& bar : bars)
    {
        const auto v = voiceLead (chordPcs (o, bar.degree, seventh), 55, 77, prev);
        prev = v;
        const auto nv = voiceLead (chordPcs (o, bar.next, seventh), 55, 77, v);
        for (size_t k = 0; k < cell.size(); ++k)
        {
            const double t = bar.start + cell[k] * 0.25;
            const double next = k + 1 < cell.size() ? bar.start + cell[k + 1] * 0.25 : bar.start + bar.length;
            // the end of a phrase anticipates the next chord: its last hit, if it is late in the bar, plays it
            const bool push = bar.phraseEnd && ! bar.last && k + 1 == cell.size() && cell[k] >= 10 && bar.next != bar.degree;
            for (int p : push ? nv : v)
                out.push_back ({ t, juce::jmin (gate, (next - t) * 0.95), clampPitch (p), humanVel (rng, cell[k] % 4 == 0 ? 92 : 98, 5) });
        }
    }
}

// ============================================================================================================ guitar
inline void guitarra (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    const bool picking = o.estilo == Estilo::ambient || o.estilo == Estilo::boombap ? rng.nextInt (100) < 70 : rng.nextInt (100) < 30;
    std::vector<std::vector<int>> cells;
    switch (o.estilo)
    {
        case Estilo::afro: case Estilo::latino: cells = { { 0, 3, 6, 8, 11, 14 }, { 2, 6, 10, 14 } }; break;
        case Estilo::boombap: case Estilo::trap: cells = { { 0, 6, 8, 14 }, { 0, 8 } }; break;
        default: cells = { { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 3, 6, 8, 10, 12, 14 }, { 2, 6, 10, 14 } }; break;
    }
    const auto cell = fit (pick (rng, cells), steps);
    for (auto& bar : bars)
    {
        // an open-position shape: the root low, then fifth, root, third, fifth, root going up (six strings)
        const auto pcs = chordPcs (o, bar.degree, false);
        std::vector<int> shape;
        int x = 40;
        while (pc (x) != pcs[0]) ++x;
        shape.push_back (x);
        for (int want : { pcs[2], pcs[0], pcs[1], pcs[2], pcs[0] })
        {
            int y = shape.back() + 1;
            while (pc (y) != want) ++y;
            shape.push_back (y);
        }
        if (picking)
        {
            // thumb on the root, fingers on the three top strings, an eighth each
            const int order[] { 0, 3, 4, 5, 2, 4, 3, 5 };
            for (int k = 0; k < 8 && k * 2 < steps; ++k)
                out.push_back ({ bar.start + k * 0.5, k == 0 ? bar.length * 0.9 : 0.9, clampPitch (shape[(size_t) order[k]]), humanVel (rng, k == 0 ? 92 : 78, 5) });
            continue;
        }
        for (size_t k = 0; k < cell.size(); ++k)
        {
            const double t = bar.start + cell[k] * 0.25;
            const double next = k + 1 < cell.size() ? bar.start + cell[k + 1] * 0.25 : bar.start + bar.length;
            const bool down = cell[k] % 4 == 0 || cell[k] % 4 == 3;           // on the beat down, off the beat up
            const int strings = down ? 6 : 4;
            for (int s = 0; s < strings; ++s)
            {
                const int string = down ? 6 - strings + s : 5 - s;           // down: low to high; up: the top four, high to low
                const double offset = s * 0.018;                              // a strum takes a moment to cross the strings
                out.push_back ({ t + offset, juce::jmax (0.1, (next - t) * 0.9 - offset), clampPitch (shape[(size_t) string]),
                                 humanVel (rng, (down ? 96 : 76) - s * 2, 4) });
            }
        }
    }
}

// ============================================================================================================ arpeggio
inline void arpegio (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, std::vector<Nota>& out)
{
    const int shape = rng.nextInt (3);          // up, down, up and down
    const double rate = o.estilo == Estilo::cuatro || o.estilo == Estilo::dnb ? (rng.nextInt (100) < 70 ? 0.25 : 0.5)
                      : o.estilo == Estilo::ambient ? 1.0 : 0.5;
    for (auto& bar : bars)
    {
        std::vector<int> tones;
        for (int oct = 0; oct < 2; ++oct)
            for (int d : { 0, 2, 4 }) tones.push_back (degreePitch (60, o.root, o.minor, bar.degree + d + oct * 7));
        while (tones.back() > 88) for (auto& t : tones) t -= 12;
        std::vector<int> order;
        if (shape == 0) order = tones;
        else if (shape == 1) order.assign (tones.rbegin(), tones.rend());
        else { order = tones; for (int i = (int) tones.size() - 2; i > 0; --i) order.push_back (tones[(size_t) i]); }
        int k = 0;
        for (double t = 0.0; t < bar.length - 1.0e-6; t += rate, ++k)
            out.push_back ({ bar.start + t, rate * 0.85, clampPitch (order[(size_t) (k % (int) order.size())]), humanVel (rng, k % 4 == 0 ? 100 : 82, 4) });
    }
}

// ============================================================================================================ melody
inline void melodia (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, bool sung, std::vector<Nota>& out)
{
    // the rhythm of the motif: on eighths (sung: never sixteenths), 3 to 6 notes, at least one rest in the bar
    std::vector<int> rhythm;
    const int maxNotes = sung ? 5 : 6, grid = sung || o.estilo == Estilo::ambient || o.estilo == Estilo::boombap ? 2 : 1;
    for (int i = 0; i < steps; i += grid)
    {
        int chance = i % 4 == 0 ? 62 : (i % 2 == 0 ? 44 : 16);
        if (o.estilo == Estilo::afro || o.estilo == Estilo::latino) chance = i % 4 == 3 || i % 4 == 2 ? 52 : chance;   // off the beat
        if (o.estilo == Estilo::ambient) chance = i % 8 == 0 ? 80 : 6;
        if (rng.nextInt (100) < chance) rhythm.push_back (i);
    }
    while ((int) rhythm.size() < 3)
    {
        const int i = rng.nextInt (steps / 2) * 2;
        if (std::find (rhythm.begin(), rhythm.end(), i) == rhythm.end()) rhythm.push_back (i);
    }
    std::sort (rhythm.begin(), rhythm.end());
    while ((int) rhythm.size() > maxNotes) rhythm.erase (rhythm.begin() + 1 + rng.nextInt ((int) rhythm.size() - 1));
    if (rhythm.back() >= steps - 2 && rhythm.size() > 3) rhythm.pop_back();          // room to breathe at the end of the bar

    // the motif in scale degrees: a chord tone to start, then mostly steps; a leap (3 or 4 degrees at most) turns back
    std::vector<int> motif;
    int deg = rng.nextBool() ? 0 : (rng.nextBool() ? 2 : 4);
    int lastMove = 0;
    for (size_t k = 0; k < rhythm.size(); ++k)
    {
        motif.push_back (deg);
        int move;
        if (std::abs (lastMove) >= 3) move = lastMove > 0 ? -1 : 1;                   // after a leap, a step the other way
        else
        {
            const int r = rng.nextInt (100);
            move = r < 38 ? 1 : (r < 76 ? -1 : (r < 84 ? (sung ? 2 : 3) : (r < 92 ? -2 : 0)));
        }
        deg = juce::jlimit (-2, sung ? 7 : 9, deg + move);
        lastMove = move;
    }
    const int base = sung ? (o.root >= 9 ? 48 : 60) : (o.root >= 6 ? 48 : 60);
    const int top = sung ? 74 : 84, bottom = sung ? 57 : 50;
    auto chordTone = [&] (int d, int chordDeg)
    {
        int bestD = d, bestDist = 99;
        for (int c : { chordDeg, chordDeg + 2, chordDeg + 4, chordDeg - 7, chordDeg - 5, chordDeg - 3, chordDeg + 7 })
            if (std::abs (c - d) < bestDist) { bestDist = std::abs (c - d); bestD = c; }
        return bestD;
    };
    for (auto& bar : bars)
    {
        // A A' A B: the second bar changes its ending, the fourth answers and lands
        const int part = bar.inPhrase;
        std::vector<int> rh = rhythm;
        if (part == 3 || bar.last)
        {
            // the answer: fewer notes, and the last one on beat 3 at the latest so it can ring
            while (rh.size() > 3) rh.erase (rh.begin() + 1 + rng.nextInt ((int) rh.size() - 1));
            while (! rh.empty() && rh.back() > 8) rh.pop_back();
            if (rh.empty() || rh.back() < 8) rh.push_back (8);
        }
        for (size_t k = 0; k < rh.size(); ++k)
        {
            const double t = bar.start + rh[k] * 0.25;
            const double next = k + 1 < rh.size() ? bar.start + rh[k + 1] * 0.25 : bar.start + bar.length;
            int d = motif[juce::jmin (k, motif.size() - 1)];
            if (part == 1 && k + 1 == rh.size()) d += rng.nextBool() ? 1 : -1;
            if (rh[k] % 4 == 0) d = chordTone (d, bar.degree);                    // on a beat: a tone of the chord
            double len = juce::jmax (sung ? 0.5 : 0.25, (next - t) * 0.9);
            const bool end = (part == 3 || bar.last) && k + 1 == rh.size();
            if (end)
            {
                // the phrase lands: on the tonic at the very end, on the chord's third or fifth in between
                d = bar.last ? 0 : chordTone (bar.degree + (rng.nextBool() ? 2 : 4), bar.degree);
                len = juce::jmax (len, bar.start + bar.length - 0.5 - t);
            }
            if (sung) len = juce::jmin (len, next - t - 0.125);                    // a singer stops between syllables
            int p = degreePitch (base, o.root, o.minor, d);
            while (p > top) p -= 12;
            while (p < bottom) p += 12;
            out.push_back ({ t, juce::jmax (0.125, len), clampPitch (p), humanVel (rng, rh[k] % 4 == 0 ? 100 : 86, 6) });
        }
    }
}

// ============================================================================================================ texture
inline void textura (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, std::vector<Nota>& out)
{
    // a pedal on the key, not on each chord: the tonic low and its fifth held across each phrase; the octave
    // (or the ninth) enters in the second half of the phrase and rises with it
    const int low = degreePitch (36, o.root, o.minor, 0) + (o.root >= 7 ? -12 : 0);
    const int fifth = degreePitch (36, o.root, o.minor, 4) + (o.root >= 7 ? -12 : 0);
    const bool ninth = rng.nextBool();
    const int high = degreePitch (60, o.root, o.minor, ninth ? 1 : 0);
    const int phrase = juce::jmin (4, (int) bars.size());
    for (size_t b = 0; b < bars.size(); b += (size_t) phrase)
    {
        const double s = bars[b].start;
        const int n = juce::jmin (phrase, (int) (bars.size() - b));
        const double len = n * bars[b].length;
        out.push_back ({ s, len * 0.995, clampPitch (low), humanVel (rng, 70, 3) });
        out.push_back ({ s, len * 0.995, clampPitch (fifth + 12), humanVel (rng, 58, 3) });
        if (n >= 2) out.push_back ({ s + len / 2.0, len / 2.0 * 0.99, clampPitch (high), humanVel (rng, 52, 3) });
    }
}

// ============================================================================================================ drums
inline void bateria (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    const double step = 0.25;
    const auto& d = o.drums;
    std::vector<int> kick, snare, hats, open, ghost, perc;
    bool clapBack = false, swing = false, sixteenths = rng.nextInt (100) < 55;
    switch (o.estilo)
    {
        case Estilo::cuatro:
            kick = { 0, 4, 8, 12 }; snare = { 4, 12 }; clapBack = true;
            if (rng.nextBool()) open = { 2, 6, 10, 14 };
            perc = { 3, 7, 11, 13 };
            break;
        case Estilo::afro:
            kick = { 0, 4, 8, 12 }; snare = { 4, 12 }; clapBack = rng.nextBool();
            perc = pick (rng, { { 3, 6, 11, 14 }, { 2, 7, 10, 15 }, { 3, 7, 10, 14 } });
            sixteenths = true;
            break;
        case Estilo::trap:
            kick = pick (rng, { { 0, 7, 10 }, { 0, 3, 10, 11 }, { 0, 8, 11 } }); snare = { 8 }; clapBack = true;
            sixteenths = true;
            break;
        case Estilo::dnb:
            kick = pick (rng, { { 0, 10 }, { 0, 7, 10 } }); snare = { 4, 12 }; ghost = { 7, 15 };
            sixteenths = false;
            break;
        case Estilo::latino:
            kick = { 0, 4, 8, 12 }; snare = { 3, 6, 11, 14 };
            sixteenths = false;
            break;
        case Estilo::boombap:
            kick = pick (rng, { { 0, 7, 10 }, { 0, 3, 8, 10 }, { 0, 10, 11 } }); snare = { 4, 12 }; swing = true;
            sixteenths = false;
            break;
        case Estilo::ambient:
            kick = { 0, 10 }; snare = { 12 }; sixteenths = false;
            break;
    }
    if (o.estilo == Estilo::cuatro && rng.nextInt (100) < 40) kick = { 0, 4, 8, 12 };
    const std::vector<int> percSpots = fit (perc.empty() ? std::vector<int> { 3, 11 } : perc, steps);
    std::vector<bool> percOn;
    for (size_t k = 0; k < percSpots.size(); ++k) percOn.push_back (rng.nextInt (100) < 45);
    auto has = [] (const std::vector<int>& v, int i) { return std::find (v.begin(), v.end(), i) != v.end(); };
    for (auto& bar : bars)
    {
        // a fill only in the last bar of a phrase (and not always): the last beat, snare and tom rising
        const bool fill = bar.phraseEnd && rng.nextInt (100) < 60;
        for (int i = 0; i < steps; ++i)
        {
            double t = bar.start + i * step;
            if (swing && i % 2 == 1) t += step * 0.33;
            const bool inFill = fill && i >= steps - 4;
            if (has (kick, i) && ! (inFill && i > steps - 4)) out.push_back ({ t, step, d.kick, humanVel (rng, i % 4 == 0 ? 112 : 98) });
            if (has (snare, i) && ! inFill)
                out.push_back ({ t, step, clapBack ? d.clap : d.snare, humanVel (rng, o.estilo == Estilo::latino ? 98 : 108) });
            if (has (ghost, i)) out.push_back ({ t, step, d.snare, humanVel (rng, 42, 5) });
            if (! open.empty() && has (open, i)) out.push_back ({ t, step * 2, d.openHat, humanVel (rng, 90) });
            else if (sixteenths || i % 2 == 0)
            {
                out.push_back ({ t, step, d.closedHat, humanVel (rng, i % 4 == 0 ? 90 : (i % 2 == 0 ? 76 : 58)) });
                // trap: a roll of thirty-seconds on the last sixteenth of some beats
                if (o.estilo == Estilo::trap && i % 4 == 3 && rng.nextInt (100) < 25)
                    out.push_back ({ t + step * 0.5, step * 0.5, d.closedHat, humanVel (rng, 70) });
            }
            for (size_t k = 0; k < percSpots.size(); ++k)
                if (percSpots[k] == i && percOn[k]) out.push_back ({ t, step, o.estilo == Estilo::afro ? d.rim : (k % 2 == 0 ? d.rim : d.perc), humanVel (rng, 80) });
            if (inFill) out.push_back ({ t, step, (i % 2 == 0) ? d.snare : d.tom, juce::jlimit (1, 127, 80 + (i - (steps - 4)) * 12) });
        }
    }
}

// ============================================================================================================ percussion
inline void percusion (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    // the layer over the drums: shaker always, hand drums in a pattern of the style, bar 4 with a conga roll
    const auto& d = o.drums;
    const bool shakerSixteenths = o.estilo != Estilo::boombap && o.estilo != Estilo::ambient && rng.nextInt (100) < 70;
    std::vector<int> hi, lo, bongo, bell;
    switch (o.estilo)
    {
        case Estilo::afro:   hi = { 3, 10, 14 }; lo = { 6, 7 }; bongo = { 0, 2, 4, 6, 8, 10, 12, 14 }; bell = { 0, 3, 6, 10, 12 }; break;
        case Estilo::latino: hi = { 2, 10 }; lo = { 6, 7, 14, 15 }; bongo = { 0, 4, 8, 12 }; bell = { 0, 4, 8, 12 }; break;
        case Estilo::trap: case Estilo::boombap: hi = { 7 }; lo = { 15 }; break;
        default:             hi = pick (rng, { { 3, 11 }, { 7, 15 }, { 3, 10 } }); lo = { 14 }; bongo = rng.nextBool() ? std::vector<int> { 7, 15 } : std::vector<int> {}; break;
    }
    const bool useBell = ! bell.empty() && rng.nextInt (100) < 40;
    auto has = [] (const std::vector<int>& v, int i) { return std::find (v.begin(), v.end(), i) != v.end(); };
    for (auto& bar : bars)
    {
        const bool roll = bar.phraseEnd;
        for (int i = 0; i < steps; ++i)
        {
            const double t = bar.start + i * 0.25;
            if (shakerSixteenths || i % 2 == 0)
                out.push_back ({ t, 0.2, d.shaker, humanVel (rng, i % 4 == 2 ? 100 : (i % 2 == 0 ? 72 : 60), 4) });
            if (roll && i >= steps - 4) { out.push_back ({ t, 0.22, i % 2 == 0 ? d.congaHi : d.congaLo, juce::jlimit (1, 127, 78 + (i - (steps - 4)) * 10) }); continue; }
            if (has (hi, i)) out.push_back ({ t, 0.25, d.congaHi, humanVel (rng, 96) });
            if (has (lo, i)) out.push_back ({ t, 0.25, d.congaLo, humanVel (rng, 88) });
            if (has (bongo, i)) out.push_back ({ t, 0.2, i % 4 == 0 ? d.bongoLo : d.bongoHi, humanVel (rng, i % 4 == 0 ? 84 : 70) });
            if (useBell && has (bell, i)) out.push_back ({ t, 0.2, d.cowbell, humanVel (rng, 76) });
        }
    }
}
}


// ============================================================================================================ variation
namespace detail
{
// the notes that start together (a chord, a strum): one hit
struct Golpe { double start = 0.0, length = 0.0; int velocity = 100; std::vector<int> pitches; };

inline std::vector<std::vector<Golpe>> golpes (const std::vector<Nota>& molde, int bars, int bpb)
{
    std::vector<std::vector<Golpe>> out ((size_t) bars);
    std::vector<Nota> sorted = molde;
    std::sort (sorted.begin(), sorted.end(), [] (const Nota& a, const Nota& b) { return a.start < b.start; });
    for (auto& n : sorted)
    {
        const int b = (int) std::floor (n.start / bpb + 1.0e-9);
        if (b < 0 || b >= bars) continue;
        auto& v = out[(size_t) b];
        if (! v.empty() && n.start - v.back().start < 0.1)       // a strum spreads its strings over a few hundredths
        {
            v.back().pitches.push_back (n.pitch);
            v.back().length = juce::jmax (v.back().length, n.length);
            continue;
        }
        Golpe g;
        g.start = n.start; g.length = n.length; g.velocity = n.velocity; g.pitches = { n.pitch };
        v.push_back (g);
    }
    for (auto& v : out) for (auto& g : v) std::sort (g.pitches.begin(), g.pitches.end());
    return out;
}

inline int intoRange (int p, int lo, int hi)
{
    while (p > hi) p -= 12;
    while (p < lo) p += 12;
    if (p > hi + 4) p -= 12;
    return p;
}

// another voicing of the same pitch classes, n notes between lo and hi, led from the previous one; it avoids
// repeating the voicing the clip had (that is the point of asking for a variation)
inline std::vector<int> revoice (const std::vector<int>& pcsIn, int n, int lo, int hi, const std::vector<int>& prev,
                                 const std::vector<int>& old, juce::Random& rng, int centre)
{
    std::vector<int> pcs = pcsIn;
    if (pcs.empty()) return old;
    std::vector<std::vector<int>> cands;
    // every way of stacking the pitch classes upwards from each inversion, around the clip's range (an inversion
    // may reach a few semitones past it: that is how a voicing changes)
    for (int inv = 0; inv < (int) pcs.size(); ++inv)
        for (int base = lo - 3; base <= hi; ++base)
        {
            if (pc (base) != pcs[(size_t) inv]) continue;
            std::vector<int> v { base };
            int k = 1;
            while ((int) v.size() < n)
            {
                int x = v.back() + 1;
                while (pc (x) != pcs[(size_t) ((inv + k) % (int) pcs.size())]) ++x;
                v.push_back (x);
                ++k;
            }
            if (v.back() > hi + 4) continue;
            cands.push_back (v);
        }
    // open voicings too: the middle note an octave up (a "drop" voicing turned around)
    const size_t closed = cands.size();
    for (size_t i = 0; i < closed; ++i)
        if (cands[i].size() >= 3)
        {
            auto v = cands[i];
            v[1] += 12;
            std::sort (v.begin(), v.end());
            if (v.back() <= hi + 4) cands.push_back (v);
        }
    std::vector<int> best;
    double bestCost = 1.0e9;
    for (auto& v : cands)
    {
        if (v == old) continue;                     // a variation is not the same voicing
        double cost = 0.0;
        for (int x : v) cost += 1.5 * (juce::jmax (0, lo - 3 - x) + juce::jmax (0, x - hi - 3));
        if (prev.empty()) { for (int x : v) cost += 0.3 * std::abs (x - centre); }
        else for (size_t i = 0; i < v.size(); ++i) { int d = 99; for (int y : prev) d = juce::jmin (d, std::abs (v[i] - y)); cost += d; }
        cost += rng.nextFloat() * 3.0;              // among near-equal voicings, any
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best.empty() ? old : best;
}

// the octave of p that fits lo..hi best (and, between equals, the one nearest the centre)
inline int fitNear (int p, int lo, int hi, int centre)
{
    int best = p, bestCost = 1 << 20;
    for (int q = pc (p); q < 128; q += 12)
    {
        const int cost = 100 * (juce::jmax (0, lo - q) + juce::jmax (0, q - hi)) + std::abs (q - centre);
        if (cost < bestCost) { bestCost = cost; best = q; }
    }
    return best;
}

inline std::vector<int> pcsOf (const std::vector<int>& pitches)
{
    std::vector<int> out;
    for (int p : pitches) if (std::find (out.begin(), out.end(), pc (p)) == out.end()) out.push_back (pc (p));
    // root position order: the lowest note's pitch class first
    return out;
}

inline int scaleDegreeOf (int pitchClass, int root, bool minor)
{
    for (int d = 0; d < 7; ++d) if (pc (root + scaleOf (minor)[(size_t) d]) == pitchClass) return d;
    return -1;
}

inline std::vector<Nota> variar (const Opciones& o, juce::Random& rng, const std::vector<Bar>& plan, int bpb)
{
    std::vector<Nota> out;
    const int bars = (int) plan.size();
    const auto hits = golpes (o.molde, bars, bpb);
    int lo = 127, hi = 0;
    double sum = 0.0;
    for (auto& n : o.molde) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); sum += n.pitch; }
    const int centre = (int) std::round (sum / juce::jmax ((size_t) 1, o.molde.size()));
    auto active = [&] (int b) { return b >= 0 && b < bars && ! hits[(size_t) b].empty(); };
    auto phraseEnd = [&] (int b) { return (b + 1) % 4 == 0 || ! active (b + 1); };
    auto gap = [&] (int b, size_t k)        // room until the next hit (or the end of the bar)
    {
        const auto& v = hits[(size_t) b];
        return (k + 1 < v.size() ? v[k + 1].start : plan[(size_t) b].start + plan[(size_t) b].length) - v[k].start;
    };

    switch (o.rol)
    {
        case Rol::bajo:
        {
            // the same rhythm and register, the root of each bar where it was; new colour (fifth, octave) and a
            // new way into the next bar
            // a bass line keeps its root most of the time: a few hits of the bar (one in four, at most three)
            // take the octave or the fifth, the same hits in every bar so it is still one riff
            int typical = 0;
            for (auto& v : hits) typical = juce::jmax (typical, (int) v.size());
            std::vector<int> colour ((size_t) juce::jmax (1, typical), 0);
            const int changes = juce::jlimit (1, 3, typical / 4);
            for (int m = 0; m < changes && typical > 1; ++m)
                colour[(size_t) (1 + rng.nextInt (typical - 1))] = rng.nextInt (100) < 55 ? 12 : 7;
            const int shiftAt = 1 + rng.nextInt (3);                 // one hit of the cell moves a sixteenth (or not)
            const bool shift = rng.nextInt (100) < 45;
            for (int b = 0; b < bars; ++b)
            {
                if (! active (b)) continue;
                const auto& v = hits[(size_t) b];
                const int rootPitch = v.front().pitches.front();
                const int rootPc = pc (rootPitch);
                const int deg = scaleDegreeOf (rootPc, o.root, o.minor);
                const int fifthPc = deg >= 0 ? pc (o.root + scaleOf (o.minor)[(size_t) ((deg + 4) % 7)]) : pc (rootPc + 7);
                int nextRoot = -1;
                if (active (b + 1)) nextRoot = hits[(size_t) b + 1].front().pitches.front();
                for (size_t k = 0; k < v.size(); ++k)
                {
                    double t = v[k].start, len = juce::jmin (v[k].length, gap (b, k));
                    if (shift && (int) k == shiftAt && k + 1 < v.size() && v[k + 1].start - t > 0.5 && k > 0)
                        t += 0.25, len = juce::jmin (len, v[k + 1].start - t);
                    int p = rootPitch;
                    const int c = colour[k % colour.size()];
                    if (c == 12) p = rootPitch + 12 <= hi ? rootPitch + 12 : (rootPitch - 12 >= lo - 2 ? rootPitch - 12 : rootPitch);
                    if (c == 7) { p = rootPitch + ((fifthPc - rootPc + 12) % 12); if (p > hi) p -= 12; }
                    // into the next chord: the last hit of a phrase steps to the note above or below its root
                    if (phraseEnd (b) && k + 1 == v.size() && k > 0 && nextRoot >= 0 && pc (nextRoot) != rootPc)
                    {
                        const int nd = scaleDegreeOf (pc (nextRoot), o.root, o.minor);
                        if (nd >= 0)
                        {
                            const int up = nextRoot + ((pc (o.root + scaleOf (o.minor)[(size_t) ((nd + 1) % 7)]) - pc (nextRoot) + 12) % 12);
                            const int down = nextRoot - ((pc (nextRoot) - pc (o.root + scaleOf (o.minor)[(size_t) ((nd + 6) % 7)]) + 12) % 12);
                            p = intoRange (rng.nextBool() ? up : down, lo - 2, hi);
                        }
                    }
                    out.push_back ({ t, juce::jmax (0.0625, len), clampPitch (p), humanVel (rng, v[k].velocity, 5) });
                }
            }
            break;
        }

        case Rol::pad: case Rol::coro: case Rol::textura:
        {
            // the same chords at the same moments, held as long; a new voicing of each (another inversion, other
            // doublings), led smoothly. A single line (a texture's slow melody) moves to other notes of the harmony
            std::vector<int> prev;
            const bool line = std::all_of (hits.begin(), hits.end(), [] (const std::vector<Golpe>& v)
                                          { return std::all_of (v.begin(), v.end(), [] (const Golpe& g) { return g.pitches.size() == 1; }); });
            int last = centre;
            int dir = rng.nextBool() ? 1 : -1;
            for (int b = 0; b < bars; ++b)
                for (size_t k = 0; k < hits[(size_t) b].size(); ++k)
                {
                    const auto& g = hits[(size_t) b][k];
                    if (line)
                    {
                        // the harmony of the bar (its chord), the nearest of its notes in the direction the line goes
                        const auto pcs = chordPcs (o, plan[(size_t) b].degree, false);
                        int best = g.pitches.front(), bestCost = 999;
                        for (int p = lo - 3; p <= hi + 3; ++p)
                        {
                            if (std::find (pcs.begin(), pcs.end(), pc (p)) == pcs.end()) continue;
                            const int step = p - last;
                            int cost = std::abs (step) + (step * dir < 0 ? 3 : 0) + (p == last ? 4 : 0) + rng.nextInt (3);
                            if (cost < bestCost) { bestCost = cost; best = p; }
                        }
                        if (best >= hi + 2 || best <= lo - 2) dir = -dir;
                        last = best;
                        out.push_back ({ g.start, g.length, clampPitch (best), humanVel (rng, g.velocity, 4) });
                        continue;
                    }
                    auto pcs = pcsOf (g.pitches);
                    const auto v = revoice (pcs, (int) g.pitches.size(), lo, hi, prev, g.pitches, rng, centre);
                    prev = v;
                    for (int p : v) out.push_back ({ g.start, g.length, clampPitch (p), humanVel (rng, g.velocity, 4) });
                }
            break;
        }

        case Rol::acordes:
        {
            // the bars and the chords stay; the rhythm either moves a little (one hit a sixteenth later or earlier)
            // or becomes another stab pattern of the style with as many hits; each chord gets a new voicing
            int totalHits = 0, activeBars = 0;
            for (int b = 0; b < bars; ++b) if (active (b)) { totalHits += (int) hits[(size_t) b].size(); ++activeBars; }
            const int perBar = juce::jmax (1, totalHits / juce::jmax (1, activeBars));
            std::vector<std::vector<int>> cells;
            switch (o.estilo)
            {
                case Estilo::afro:   cells = { { 0, 3, 7, 10, 12 }, { 3, 6, 11, 14 }, { 0, 6, 10 }, { 2, 6, 10, 14 } }; break;
                case Estilo::latino: cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 }, { 0, 6, 10 } }; break;
                default:             cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 }, { 3, 6, 10 }, { 2, 10 }, { 0, 6, 12 }, { 0, 8 }, { 0 } }; break;
            }
            std::vector<int> cell;
            for (auto& c : cells) if (cell.empty() || std::abs ((int) c.size() - perBar) < std::abs ((int) cell.size() - perBar)) cell = c;
            const bool newRhythm = rng.nextBool();
            std::vector<double> lens;
            for (auto& v : hits) for (auto& g : v) lens.push_back (g.length);
            std::sort (lens.begin(), lens.end());
            const double typical = lens.empty() ? 0.3 : lens[lens.size() / 2];
            std::vector<int> prev;
            for (int b = 0; b < bars; ++b)
            {
                if (! active (b)) continue;
                const auto& v = hits[(size_t) b];
                const auto* main = &v.front();
                for (auto& g : v) if (g.pitches.size() > main->pitches.size()) main = &g;
                const auto chord = revoice (pcsOf (main->pitches), (int) main->pitches.size(), lo, hi, prev, main->pitches, rng, centre);
                prev = chord;
                if (newRhythm)
                {
                    for (size_t k = 0; k < cell.size(); ++k)
                    {
                        const double t = plan[(size_t) b].start + cell[k] * 0.25;
                        const double next = k + 1 < cell.size() ? plan[(size_t) b].start + cell[k + 1] * 0.25 : plan[(size_t) b].start + plan[(size_t) b].length;
                        for (int p : chord) out.push_back ({ t, juce::jmin (typical, (next - t) * 0.95), clampPitch (p), humanVel (rng, main->velocity, 5) });
                    }
                    continue;
                }
                const size_t moved = (size_t) rng.nextInt ((int) v.size());
                for (size_t k = 0; k < v.size(); ++k)
                {
                    double t = v[k].start;
                    if (k == moved && k > 0 && gap (b, k) > 0.5 && rng.nextBool()) t += 0.25;
                    // a hit that had another chord keeps its own harmony, revoiced
                    const auto here = v[k].pitches.size() >= 2 && pcsOf (v[k].pitches) != pcsOf (main->pitches)
                                          ? revoice (pcsOf (v[k].pitches), (int) v[k].pitches.size(), lo, hi, chord, v[k].pitches, rng, centre) : chord;
                    for (int p : here) out.push_back ({ t, juce::jmin (v[k].length, gap (b, k)), clampPitch (p), humanVel (rng, v[k].velocity, 5) });
                }
            }
            break;
        }

        case Rol::guitarra:
        {
            // the same strums (or picked notes) at the same moments; new shapes: strums revoiced over the guitar's
            // range, picked notes walk the chord in another order
            std::vector<int> prev;
            for (int b = 0; b < bars; ++b)
            {
                if (! active (b)) continue;
                std::vector<int> barPitches;
                for (auto& g : hits[(size_t) b]) for (int p : g.pitches) barPitches.push_back (p);
                const auto barPcs = pcsOf (barPitches);
                int lastPick = -1;
                for (size_t k = 0; k < hits[(size_t) b].size(); ++k)
                {
                    const auto& g = hits[(size_t) b][k];
                    if (g.pitches.size() >= 3)
                    {
                        auto v = revoice (pcsOf (g.pitches), (int) g.pitches.size(), lo, hi, prev, g.pitches, rng, centre);
                        prev = v;
                        const bool down = std::fmod (g.start, 1.0) < 0.3 || std::fmod (g.start, 1.0) > 0.7;
                        if (! down) std::reverse (v.begin(), v.end());
                        for (size_t s = 0; s < v.size(); ++s)
                            out.push_back ({ g.start + s * 0.018, juce::jmax (0.1, g.length - s * 0.018), clampPitch (v[s]), humanVel (rng, g.velocity - (int) s * 2, 4) });
                        continue;
                    }
                    for (int q : g.pitches)
                    {
                        // fingerpicking: another tone of the bar's chord, never the same one twice in a row
                        int best = q, bestCost = 999;
                        for (int p = lo; p <= hi; ++p)
                        {
                            if (std::find (barPcs.begin(), barPcs.end(), pc (p)) == barPcs.end() || p == lastPick) continue;
                            const int cost = std::abs (p - q) + rng.nextInt (6);
                            if (cost < bestCost) { bestCost = cost; best = p; }
                        }
                        lastPick = best;
                        out.push_back ({ g.start, g.length, clampPitch (best), humanVel (rng, g.velocity, 5) });
                    }
                }
            }
            break;
        }

        case Rol::arpegio:
        {
            // the same rate and range, the chord of each bar in another order (up, down, up and down)
            const int shape = rng.nextInt (3);
            for (int b = 0; b < bars; ++b)
            {
                if (! active (b)) continue;
                std::vector<int> barPitches;
                for (auto& g : hits[(size_t) b]) for (int p : g.pitches) barPitches.push_back (p);
                const auto pcs = pcsOf (barPitches);
                std::vector<int> tones;
                for (int p = lo; p <= hi; ++p) if (std::find (pcs.begin(), pcs.end(), pc (p)) != pcs.end()) tones.push_back (p);
                if (tones.empty()) continue;
                std::vector<int> order = tones;
                if (shape == 1) std::reverse (order.begin(), order.end());
                if (shape == 2) for (int i = (int) tones.size() - 2; i > 0; --i) order.push_back (tones[(size_t) i]);
                int k = 0;
                for (auto& g : hits[(size_t) b])
                    out.push_back ({ g.start, g.length, clampPitch (order[(size_t) (k++ % (int) order.size())]), humanVel (rng, g.velocity, 4) });
            }
            break;
        }

        case Rol::melodia: case Rol::voz:
        {
            // the same rhythm bar by bar (so the phrase keeps its shape: bars that were alike stay alike) and the same
            // register; a new contour by the rules: a chord tone on the beat, steps more than leaps, a leap turns back,
            // the phrase lands on a long note, the last one on the tonic
            std::map<juce::String, std::vector<int>> contours;
            const int base = (int) std::floor ((centre - 5 - o.root) / 12.0) * 12;       // a Do under the middle of the line
            for (int b = 0; b < bars; ++b)
            {
                if (! active (b)) continue;
                const auto& v = hits[(size_t) b];
                juce::String sig;
                for (auto& g : v) sig << juce::String (g.start - plan[(size_t) b].start, 2) << ",";
                auto it = contours.find (sig);
                if (it == contours.end())
                {
                    std::vector<int> c;
                    int d = rng.nextBool() ? 0 : (rng.nextBool() ? 2 : 4), lastMove = 0;
                    for (size_t k = 0; k < v.size(); ++k)
                    {
                        c.push_back (d);
                        int move;
                        if (std::abs (lastMove) >= 3) move = lastMove > 0 ? -1 : 1;
                        else { const int r = rng.nextInt (100); move = r < 38 ? 1 : (r < 76 ? -1 : (r < 84 ? 3 : (r < 92 ? -2 : 0))); }
                        d = juce::jlimit (-3, 9, d + move);
                        lastMove = move;
                    }
                    it = contours.emplace (sig, c).first;
                }
                const auto& c = it->second;
                for (size_t k = 0; k < v.size(); ++k)
                {
                    int d = c[k];
                    const double inBar = v[k].start - plan[(size_t) b].start;
                    if (std::abs (inBar - std::round (inBar)) < 0.01)           // on a beat: the nearest chord tone
                    {
                        const int cd = plan[(size_t) b].degree;
                        int bestD = d, bestDist = 99;
                        for (int x : { cd, cd + 2, cd + 4, cd - 7, cd - 5, cd - 3, cd + 7 })
                            if (std::abs (x - d) < bestDist) { bestDist = std::abs (x - d); bestD = x; }
                        d = bestD;
                    }
                    if (b == bars - 1 && k + 1 == v.size()) d = 0;                 // home at the very end
                    else if (phraseEnd (b) && k + 1 == v.size()) d = plan[(size_t) b].degree + (rng.nextBool() ? 2 : 4);
                    int p = degreePitch (base, o.root, o.minor, d);
                    p = fitNear (p, lo - 2, hi + 3, centre);
                    out.push_back ({ v[k].start, juce::jmin (v[k].length, gap (b, k)), clampPitch (p), humanVel (rng, v[k].velocity, 5) });
                }
            }
            break;
        }

        case Rol::bateria: case Rol::percusion:
        {
            // the backbone (kick, snare, claps) stays exactly where it was; every other sound keeps its bars and its
            // density but its hits move: one a sixteenth over, one left out, a ghost note added; fills stay
            const auto& dr = o.drums;
            auto backbone = [&] (int p) { return o.rol == Rol::bateria && (p == dr.kick || p == dr.snare || p == dr.clap || p == 35 || p == 36); };
            std::map<int, std::vector<std::vector<Nota>>> rows;
            for (auto& n : o.molde)
            {
                const int b = (int) std::floor (n.start / bpb + 1.0e-9);
                if (b < 0 || b >= bars) continue;
                auto& r = rows[n.pitch];
                if (r.empty()) r.resize ((size_t) bars);
                r[(size_t) b].push_back (n);
            }
            for (auto& [pitch, rb] : rows)
                for (int b = 0; b < bars; ++b)
                {
                    auto& v = rb[(size_t) b];
                    if (v.empty()) continue;
                    if (backbone (pitch) || phraseEnd (b) && (b + 1) % 4 == 0 && rng.nextInt (100) < 70)
                    {
                        for (auto& n : v) out.push_back ({ n.start, n.length, n.pitch, humanVel (rng, n.velocity, 4) });
                        continue;
                    }
                    std::set<int> taken;
                    for (auto& n : v) taken.insert ((int) std::round ((n.start - plan[(size_t) b].start) / 0.25));
                    std::vector<Nota> now;
                    for (auto& n : v)
                    {
                        auto m = n;
                        const int r = rng.nextInt (100);
                        const int s = (int) std::round ((n.start - plan[(size_t) b].start) / 0.25);
                        if (r < 14 && v.size() > 2) continue;                      // left out
                        if (r < 30)
                        {
                            const int to = s + (rng.nextBool() ? 1 : -1);
                            if (to >= 0 && to < bpb * 4 && ! taken.count (to)) { taken.erase (s); taken.insert (to); m.start = plan[(size_t) b].start + to * 0.25; }
                        }
                        m.velocity = humanVel (rng, n.velocity, 8);
                        now.push_back (m);
                    }
                    if (rng.nextInt (100) < 25)                                   // a ghost on a free off-beat sixteenth
                    {
                        const int s = rng.nextInt (bpb * 2) * 2 + 1;
                        if (! taken.count (s)) now.push_back ({ plan[(size_t) b].start + s * 0.25, 0.2, pitch, juce::jmax (1, v.front().velocity - 35) });
                    }
                    for (auto& m : now) out.push_back (m);
                }
            break;
        }
    }
    return out;
}
}

inline std::vector<Nota> generar (const Opciones& o)
{
    using namespace detail;
    juce::Random rng (o.seed);
    std::vector<Nota> out;
    const int bars = juce::jlimit (1, 16, o.bars), bpb = juce::jmax (1, o.beatsPerBar);
    const int steps = bpb * 4;                                  // sixteenths per bar
    auto prog = o.progresion.empty() ? progression (rng, o.minor) : o.progresion;
    if (o.progresion.empty() == false) rng.nextInt (6);         // the same random path either way
    const auto plan = barsOf (bars, bpb, prog);
    auto shape = o;
    shape.bars = bars;
    shape.beatsPerBar = bpb;

    if (! o.molde.empty())
        out = variar (shape, rng, plan, bpb);
    else switch (o.rol)
    {
        case Rol::bateria:   bateria (shape, rng, plan, steps, out); break;
        case Rol::percusion: percusion (shape, rng, plan, steps, out); break;
        case Rol::bajo:      bajo (shape, rng, plan, steps, out); break;
        case Rol::acordes:   acordes (shape, rng, plan, steps, out); break;
        case Rol::pad:       pad (shape, rng, plan, out); break;
        case Rol::coro:      coro (shape, rng, plan, out); break;
        case Rol::guitarra:  guitarra (shape, rng, plan, steps, out); break;
        case Rol::arpegio:   arpegio (shape, rng, plan, out); break;
        case Rol::melodia:   melodia (shape, rng, plan, steps, false, out); break;
        case Rol::voz:       melodia (shape, rng, plan, steps, true, out); break;
        case Rol::textura:   textura (shape, rng, plan, out); break;
    }
    // nothing outside the clip, nothing shorter than a thirty-second
    const double end = bars * (double) bpb;
    std::vector<Nota> kept;
    for (auto& n : out)
        if (n.start >= 0.0 && n.start < end - 1.0e-9)
        {
            auto m = n;
            m.length = juce::jlimit (0.03125, end - m.start, m.length);
            kept.push_back (m);
        }
    std::sort (kept.begin(), kept.end(), [] (const Nota& a, const Nota& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
    return kept;
}

// the usual role of each animal of the family
inline Rol rolDe (const juce::String& animal)
{
    const auto a = animal.toLowerCase();
    if (a.contains ("hamster")) return Rol::bateria;
    if (a.contains ("mole")) return Rol::percusion;
    if (a.contains ("badger")) return Rol::bajo;
    if (a.contains ("dormouse")) return Rol::pad;
    if (a.contains ("porcupine")) return Rol::coro;
    if (a.contains ("chipmunk") || a.contains ("hedgehog")) return Rol::acordes;
    if (a.contains ("ferret")) return Rol::guitarra;
    if (a.contains ("skunk")) return Rol::textura;
    if (a.contains ("chinchilla")) return Rol::voz;
    return Rol::melodia;     // Squirrel, anything else
}

// a key that the notes already in use suggest (Krumhansl-Kessler); returns false when there are too few
inline bool tonalidadDe (const std::vector<Nota>& notes, int& root, bool& minor)
{
    std::array<double, 12> c {};
    double total = 0.0;
    for (auto& n : notes) { c[(size_t) (n.pitch % 12)] += n.length; total += n.length; }
    if (total <= 0.0 || notes.size() < 3) return false;
    static const double ma[] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mi[] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double best = -2.0;
    for (int m = 0; m < 2; ++m)
        for (int r = 0; r < 12; ++r)
        {
            const double* prof = m == 0 ? ma : mi;
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += c[(size_t) i]; my += prof[(i - r + 12) % 12]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i) { const double x = c[(size_t) i] - mx, y = prof[(i - r + 12) % 12] - my; sxy += x * y; sxx += x * x; syy += y * y; }
            const double v = sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
            if (v > best) { best = v; root = r; minor = m == 1; }
        }
    return best > 0.3;
}

inline juce::String nombreTonalidad (int root, bool minor)
{
    static const char* n[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return juce::String (n[((root % 12) + 12) % 12]) + (minor ? " menor" : " mayor");
}
}

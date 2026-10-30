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
    for (auto* s : { "trap", "drill", "phonk", "dubstep", "futurebass", "jerseyclub", "hightempo" }) if (g == s) return Estilo::trap;
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
    int tomHi = 48, tomLo = 41, crash = 49, ride = 51;
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
// Toussaint's Euclidean rhythms: k hits spread as evenly as they can be over n steps, turned by rot. E(3,8) is the
// tresillo, E(5,16) the bossa's bell, E(7,16) a samba's, E(4,16) four on the floor
inline std::vector<int> euclid (int k, int n, int rot = 0)
{
    std::vector<int> v;
    if (k <= 0 || n <= 0) return v;
    for (int i = 0; i < n; ++i)
        if (((i + rot) * k) % n < k) v.push_back (i);
    return v;
}
// a cell made for 8 steps, repeated over the bar (tresillo, clave halves)
inline std::vector<int> twice (const std::vector<int>& eight)
{
    std::vector<int> v = eight;
    for (int s : eight) v.push_back (s + 8);
    return v;
}
inline bool hasStep (const std::vector<int>& v, int i) { return std::find (v.begin(), v.end(), i) != v.end(); }
// one option out of several, each with its weight
inline int weighted (juce::Random& rng, std::initializer_list<int> weights)
{
    int total = 0;
    for (int w : weights) total += w;
    int r = rng.nextInt (juce::jmax (1, total)), i = 0;
    for (int w : weights) { if (r < w) return i; r -= w; ++i; }
    return 0;
}
}

// ============================================================================================================ bass
namespace detail
{
inline void bajo (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    // A bass line is a one-bar cell played over the chords. The cell comes out of the style's ways of playing:
    //   off the beat (house: between the kicks), rolling sixteenths that leave each beat's first sixteenth to the
    //   kick (techno), disco octaves, syncopated funk, a 303 line with octave jumps and slides, the tresillo, the 808
    //   that holds and drops an octave, a walking line with approach notes...
    // Each note of the cell has a colour over the chord (root, octave, fifth, the minor seventh, a passing note),
    // chosen once so every bar sounds like the same line. Bar 2 of the phrase may answer bar 1 (call and response)
    // and the last bar of each phrase changes its ending: a pickup or a walk into the next chord.
    const double step = 0.25;
    struct Way { std::vector<int> cell; std::vector<int> colours; bool legato; double gate; };
    std::vector<Way> ways;
    // colours: 0 root, 7 octave up, 4 fifth, 6 minor seventh (a scale degree above the root), -7 octave down, 1 passing note
    switch (o.estilo)
    {
        case Estilo::cuatro:
            ways = { { { 2, 6, 10, 14 }, { 0 }, false, 0.6 },                                            // the offbeat bass
                     { { 2, 6, 10, 14 }, { 0, 0, 0, 7 }, false, 0.6 },
                     { { 0, 2, 6, 10, 14 }, { 7, 0, 0, 0, 0 }, false, 0.6 },                             // the first hit an octave up
                     { { 2, 3, 6, 7, 10, 11, 14, 15 }, { 0, 7, 0, 7, 0, 7, 0, 4 }, false, 0.8 },
                     { { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, { 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 6, 0 }, false, 0.85 },   // rolling techno
                     { { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 7, 0, 7, 0, 7, 0, 7 }, false, 0.7 },          // disco octaves
                     { { 0, 3, 6, 10, 12, 14 }, { 0, 0, 4, 6, 0, 7 }, false, 0.7 },
                     { { 0, 3, 7, 10, 13 }, { 0, 7, 0, 4, 6 }, true, 1.0 },                              // 303: slides and jumps
                     { { 0, 2, 3, 5, 8, 10, 11, 14 }, { 0, 0, 7, 0, 4, 0, 7, 6 }, true, 1.0 },
                     { { 0, 8 }, { 0, 4 }, true, 1.0 },                                                  // held call, then the response
                     { { 3, 6, 10, 14 }, { 0, 0, 4, 0 }, false, 0.6 } };
            break;
        case Estilo::afro:
            ways = { { twice ({ 0, 3, 6 }), { 0, 0, 4, 0, 0, 7 }, false, 0.7 },                           // the tresillo
                     { { 0, 6, 10 }, { 0, 4, 0 }, false, 0.8 },
                     { { 3, 6, 11, 14 }, { 0, 0, 4, 0 }, false, 0.7 },
                     { { 0, 3, 8, 11, 14 }, { 0, 7, 0, 4, 6 }, false, 0.7 },
                     { { 0, 3, 6, 10, 12 }, { 0, 0, 4, 0, 7 }, true, 1.0 },                              // log-drum like, sliding
                     { euclid (5, 16, 0), { 0, 4, 0, 7, 0 }, false, 0.7 } };
            break;
        case Estilo::trap:
            ways = { { { 0, 7, 10 }, { 0, 0, -7 }, true, 1.0 }, { { 0, 3, 10, 11 }, { 0, 0, 4, 0 }, true, 1.0 },
                     { { 0, 8, 11 }, { 0, 6, 0 }, true, 1.0 }, { { 0, 6, 10, 14 }, { 0, 0, -7, 0 }, true, 1.0 },
                     { { 0, 10 }, { 0, 4 }, true, 1.0 }, { { 0, 11, 14 }, { 0, 0, 7 }, true, 1.0 }, { { 0 }, { 0 }, true, 1.0 } };
            break;
        case Estilo::dnb:
            ways = { { { 0 }, { 0 }, true, 1.0 }, { { 0, 10 }, { 0, 4 }, true, 1.0 }, { { 0, 6, 10 }, { 0, 0, 6 }, true, 1.0 },
                     { { 0, 3, 10, 14 }, { 0, 7, 0, 4 }, true, 1.0 }, { { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 0, 7, 0, 0, 4, 0, 6 }, false, 0.8 } };
            break;
        case Estilo::latino:
            ways = { { { 0, 3, 8, 11 }, { 0, 4, 0, 4 }, false, 0.8 }, { { 0, 6, 8, 14 }, { 0, 4, 0, 4 }, false, 0.8 },
                     { { 0, 4, 8, 12 }, { 0, 4, 0, 4 }, false, 0.8 },                                   // cumbia: root and fifth
                     { { 0, 6, 12 }, { 0, 4, 0 }, false, 0.9 },                                         // the tumbao's anticipations
                     { { 0, 3, 6, 8, 11, 14 }, { 0, 0, 4, 0, 0, 7 }, false, 0.7 } };
            break;
        case Estilo::boombap:
            ways = { { { 0, 6, 8, 11 }, { 0, 0, 4, 1 }, false, 0.8 }, { { 0, 4, 8, 12 }, { 0, 4, 6, 1 }, false, 0.9 },   // walking, an approach at the end
                     { { 0, 7, 10 }, { 0, 0, 6 }, false, 0.9 }, { { 0, 3, 8, 10, 14 }, { 0, 7, 0, 4, 1 }, false, 0.7 },
                     { { 0, 10, 11 }, { 0, 4, 0 }, false, 0.8 } };
            break;
        case Estilo::ambient:
            ways = { { { 0 }, { 0 }, true, 1.0 }, { { 0, 8 }, { 0, 4 }, true, 1.0 } };
            break;
    }
    const auto& way = ways[(size_t) rng.nextInt ((int) ways.size())];
    std::vector<int> cell, colour;
    for (size_t k = 0; k < way.cell.size(); ++k)
        if (way.cell[k] < steps) { cell.push_back (way.cell[k]); colour.push_back (way.colours[k % way.colours.size()]); }
    if (cell.empty()) { cell = { 0 }; colour = { 0 }; }
    // one note in four may take another colour (so two lines of the same way are not the same)
    for (size_t k = 1; k < colour.size(); ++k)
        if (rng.nextInt (100) < 25) colour[k] = pick (rng, { { 0 }, { 7 }, { 4 }, { 6 } })[0];
    // call and response: bar 2 of the phrase plays another cell of the same way (shifted or thinned)
    const bool response = cell.size() > 2 && rng.nextInt (100) < 45;
    std::vector<int> cell2 = cell, colour2 = colour;
    if (response)
    {
        if (rng.nextBool()) { cell2.pop_back(); colour2.pop_back(); }
        else for (auto& c : colour2) if (c == 0 && rng.nextBool()) c = 7;
    }
    const int base = o.root >= 3 ? 24 : 36;      // the tonic between Re#1 and Re2: deep, under the old line
    const int tonic = base + o.root;
    auto place = [&] (int deg)
    {
        int p = degreePitch (base, o.root, o.minor, deg);
        while (p > tonic + 12) p -= 12;
        while (p < juce::jmax (24, tonic - 5)) p += 12;           // never under Do1 (it would be felt, not heard)
        return p;
    };
    for (auto& bar : bars)
    {
        const bool second = response && bar.inPhrase == 1;
        const auto& c = second ? cell2 : cell;
        const auto& col = second ? colour2 : colour;
        const int total = (int) bar.length * 4;
        // the end of the phrase: one time in two, a pickup of two sixteenths before the next chord
        std::vector<int> cc = c, cl = col;
        if (bar.phraseEnd && ! bar.last && total >= 16 && rng.nextBool() && ! hasStep (cc, 14) && ! hasStep (cc, 15))
        { cc.push_back (14); cl.push_back (1); cc.push_back (15); cl.push_back (1); }
        for (size_t k = 0; k < cc.size(); ++k)
        {
            const double t = bar.start + cc[k] * step;
            const double next = k + 1 < cc.size() ? bar.start + cc[k + 1] * step : bar.start + total * step;
            const int root = place (bar.degree);
            int p = root;
            switch (cl[k])
            {
                case 7:  p = root + 12; if (p > 52) p -= 24; if (p < tonic - 12) p += 12; break;
                case -7: p = root - 12; if (p < 24) p += 12; break;
                case 4:  p = place (bar.degree + 4); break;
                case 6:  p = place (bar.degree + 6); if (p > root + 12) p -= 12; break;
                case 1:
                {
                    // a passing note: the step that leads into the next chord's root (or the chord's own)
                    const int target = place (bar.next);
                    p = rng.nextBool() ? place (bar.next + 1) : place (bar.next - 1);
                    if (std::abs (p - target) > 3) p = target + (rng.nextBool() ? 1 : -1);
                    break;
                }
                default: break;
            }
            const double len = way.legato ? (next - t) * 0.98 : juce::jmax (step * 0.9, (next - t) * way.gate);
            out.push_back ({ t, juce::jmin (len, next - t), clampPitch (p), humanVel (rng, cc[k] % 4 == 0 ? 112 : 100, 6) });
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
        auto v = voiceLead (pcs, 43, 67, prev);
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
    const int lo[4] { 33, 40, 46, 50 }, hi[4] { 52, 58, 64, 70 };
    std::array<int, 4> prev { 0, 0, 0, 0 };
    bool first = true;
    std::array<size_t, 4> idx {};
    int prevPhrase = -1;
    // each time another choir: where the sopranos sit (low and warm or high and open), how close the voices go, and
    // how they sing (held chords, two "ah" a bar, an "ooh" on 2 and 4, the 3+3+2 of the dance floor, or a swell that
    // breathes in the middle of the bar)
    const int topTarget = 58 + rng.nextInt (9);
    const int spread = rng.nextInt (3);                          // 0 close, 1 middle, 2 open
    const int rhythm = rng.nextInt (5);
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
            cost += std::abs ((S - B) - (22 + spread * 6)) / 2;
            if (first) cost += std::abs (S - topTarget) * 2 + std::abs (A - (topTarget - 8)) + std::abs (T - (topTarget - 15)) + std::abs (B - 41);
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
            const auto v = voiceLead (pcs, 46, 65, {});
            best = { juce::jlimit (40, 60, v[0] - 12), v[0], v[1], v[2] };
        }
        const int phrase = (int) b / juce::jmax (1, juce::jmin (4, (int) bars.size()));
        // held notes: a voice that keeps its note inside a phrase is tied, not sung again; the phrase's last bar
        // ends an eighth early so the choir breathes
        if (rhythm == 0)
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
        else
        {
            // the attacks of the bar, in quarters: when each "ah" starts and how long it lasts
            std::vector<std::pair<double, double>> hits;
            const double L = bar.length;
            if (rhythm == 1) hits = { { 0.0, L * 0.5 - 0.1 }, { L * 0.5, L * 0.5 - (bar.phraseEnd ? 0.5 : 0.1) } };
            else if (rhythm == 2) for (double q = 1.0; q < L; q += 2.0) hits.push_back ({ q, 0.9 });
            else if (rhythm == 3) hits = { { 0.0, 1.4 }, { 1.5, 1.4 }, { 3.0, juce::jmax (0.5, L - 3.1) } };
            else hits = { { 0.0, L * 0.75 - 0.05 }, { L * 0.75, L * 0.25 - 0.05 } };
            for (auto& [at, len] : hits)
            {
                if (at >= L) continue;
                for (int k = 0; k < 4; ++k)
                {
                    const int vel = 62 + bar.inPhrase * 5 + (k == 3 ? 6 : 0) + (at == 0.0 ? 6 : 0);
                    out.push_back ({ bar.start + at, juce::jmax (0.25, len), clampPitch (best[(size_t) k]), humanVel (rng, vel, 4) });
                }
            }
        }
        prev = best;
        first = false;
        prevPhrase = phrase;
    }
}

// ============================================================================================================ stabs
inline void acordes (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    // Stabs: a one-bar rhythm of the style, and a chord colour (triad, seventh, ninth, sus2, add9) chosen once, voiced
    // close and led from the last one. Bar 2 of the phrase may answer with another rhythm; the last bar anticipates
    // the next chord on its late hit.
    std::vector<std::vector<int>> cells;
    double gate = 0.3;
    switch (o.estilo)
    {
        case Estilo::cuatro:  cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 }, { 3, 6, 10 }, { 2, 10 }, { 0, 3, 6, 10, 13 }, { 3, 7, 10, 14 },
                                        { 0, 2, 4, 6, 8, 10, 12, 14 }, { 2, 5, 8, 11, 14 }, { 0, 6, 12 }, { 3, 6, 11, 14 } }; gate = rng.nextBool() ? 0.3 : 0.18; break;
        case Estilo::afro:    cells = { { 0, 3, 7, 10, 12 }, { 3, 6, 11, 14 }, { 0, 6, 10 }, twice ({ 0, 3, 6 }), euclid (5, 16, 2), { 2, 6, 9, 14 } }; break;
        case Estilo::trap:    cells = { { 0, 8 }, { 0 }, { 0, 6, 12 }, { 0, 10 } }; gate = 1.9; break;
        case Estilo::dnb:     cells = { { 0, 10 }, { 0 }, { 0, 6, 10 } }; gate = 1.4; break;
        case Estilo::latino:  cells = { { 2, 6, 10, 14 }, { 0, 3, 6, 8, 11, 14 }, { 3, 6, 11, 14 }, { 0, 3, 7, 10, 12 } }; break;
        case Estilo::boombap: cells = { { 0, 7, 10 }, { 0, 6, 12 }, { 0, 3, 10 }, { 0, 8, 11 } }; gate = 0.8; break;
        case Estilo::ambient: cells = { { 0 }, { 0, 8 } }; gate = 3.9; break;
    }
    const auto cell = fit (pick (rng, cells), steps);
    const auto cell2 = rng.nextInt (100) < 40 ? fit (pick (rng, cells), steps) : cell;
    const int colour = weighted (rng, { 25, 30, 20, 12, 13 });       // triad, seventh, ninth, sus2, add9
    auto pcsFor = [&] (int degree)
    {
        auto pcs = chordPcs (o, degree, colour == 1 || colour == 2);
        const int ninth = pc (degreePitch (0, o.root, o.minor, degree + 1));
        if (colour == 2) pcs[2] = ninth;                                // the ninth chord: root, third, seventh, ninth (the fifth goes)
        if (colour == 3) pcs[1] = ninth;                                // sus2: the ninth instead of the third
        if (colour == 4) pcs.push_back (ninth);
        return pcs;
    };
    const int lo = 45 + rng.nextInt (5), hi = lo + 22;
    std::vector<int> prev;
    for (auto& bar : bars)
    {
        const auto v = voiceLead (pcsFor (bar.degree), lo, hi, prev);
        prev = v;
        const auto nv = voiceLead (pcsFor (bar.next), lo, hi, v);
        const auto& c = bar.inPhrase == 1 ? cell2 : cell;
        for (size_t k = 0; k < c.size(); ++k)
        {
            const double t = bar.start + c[k] * 0.25;
            const double next = k + 1 < c.size() ? bar.start + c[k + 1] * 0.25 : bar.start + bar.length;
            const bool push = bar.phraseEnd && ! bar.last && k + 1 == c.size() && c[k] >= 10 && bar.next != bar.degree;
            for (int p : push ? nv : v)
                out.push_back ({ t, juce::jmin (gate, (next - t) * 0.95), clampPitch (p), humanVel (rng, c[k] % 4 == 0 ? 92 : 98, 5) });
        }
    }
}

// ============================================================================================================ guitar
inline void guitarra (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    // Eight ways a rhythm guitar plays, picked by the style:
    //   funk     the hand never stops (sixteenths): most strums are dead (muted, short, soft), the chord speaks on a
    //            few accents; ninth and seventh chords on the top strings (the chicken scratch)
    //   disco    Nile Rodgers' chucks: sixteenths, light palm mute, accents off the beat
    //   skank    a short chord on every offbeat, top three strings
    //   strum    a strumming pattern (down on the beat, up off it), open chords
    //   picking  fingerpicking: the thumb on the bass, fingers on the top strings (Travis, rolling, arpeggio of 3)
    //   power    palm-muted eighths of a power chord (root, fifth, octave) on the low strings
    //   riff     a one-bar single-note riff on the chord (root, octave, fifth, seventh)
    //   tresillo a nylon guitar picking the 3-3-2
    // And five shapes: open position, a barre chord, a top triad (inversion), a seventh/ninth without its root, a
    // power chord. Bar 4 of the phrase changes the rhythm (it pushes into the next chord or leaves room).
    enum Way { funk, disco, skank, strum, picking, power, riff, tresillo };
    Way way = strum;
    switch (o.estilo)
    {
        case Estilo::cuatro:  way = (Way) pick (rng, { { funk }, { funk }, { disco }, { disco }, { skank }, { skank }, { strum }, { picking }, { power }, { riff } })[0]; break;
        case Estilo::afro:    way = (Way) pick (rng, { { skank }, { picking }, { picking }, { riff }, { funk }, { tresillo } })[0]; break;
        case Estilo::trap:    way = (Way) pick (rng, { { picking }, { picking }, { riff }, { strum }, { tresillo } })[0]; break;
        case Estilo::dnb:     way = (Way) pick (rng, { { power }, { picking }, { skank }, { strum }, { riff } })[0]; break;
        case Estilo::latino:  way = (Way) pick (rng, { { tresillo }, { tresillo }, { skank }, { picking }, { strum } })[0]; break;
        case Estilo::boombap: way = (Way) pick (rng, { { picking }, { picking }, { funk }, { riff }, { strum } })[0]; break;
        case Estilo::ambient: way = (Way) pick (rng, { { picking }, { picking }, { strum } })[0]; break;
    }
    // the shape of the chord on every bar
    enum Shape { openPos, barre, topTriad, extended, powerShape };
    Shape shapeKind = openPos;
    switch (way)
    {
        case funk: case disco: shapeKind = rng.nextInt (100) < 65 ? extended : topTriad; break;
        case skank:            shapeKind = rng.nextBool() ? topTriad : extended; break;
        case power:            shapeKind = powerShape; break;
        case strum:            shapeKind = rng.nextBool() ? openPos : barre; break;
        default:               shapeKind = (Shape) pick (rng, { { openPos }, { barre }, { topTriad } })[0]; break;
    }
    const int position = 52 + rng.nextInt (9);           // where on the neck the top shapes sit (Mi3..Do4)
    auto shapeFor = [&] (int degree) -> std::vector<int>
    {
        const auto pcs = chordPcs (o, degree, false);
        std::vector<int> v;
        auto above = [] (int from, int pcWanted) { int y = from + 1; while (pc (y) != pcWanted) ++y; return y; };
        switch (shapeKind)
        {
            case openPos: case barre:
            {
                int x = shapeKind == openPos ? 40 : 43;
                while (pc (x) != pcs[0]) ++x;
                v.push_back (x);
                for (int want : { pcs[2], pcs[0], pcs[1], pcs[2], pcs[0] }) v.push_back (above (v.back(), want));
                break;
            }
            case topTriad:
            {
                const int inv = rng.nextInt (3);
                int x = position;
                while (pc (x) != pcs[(size_t) inv]) ++x;
                v.push_back (x);
                v.push_back (above (x, pcs[(size_t) ((inv + 1) % 3)]));
                v.push_back (above (v.back(), pcs[(size_t) ((inv + 2) % 3)]));
                break;
            }
            case extended:
            {
                // third, seventh and ninth (or fifth, seventh, ninth): the funk and disco chord, no root
                const int third = pcs[1], seventh = pc (degreePitch (0, o.root, o.minor, degree + 6)), ninth = pc (degreePitch (0, o.root, o.minor, degree + 8));
                int x = position;
                while (pc (x) != third) ++x;
                v.push_back (x);
                v.push_back (above (x, seventh));
                v.push_back (above (v.back(), ninth));
                if (rng.nextBool()) v.push_back (above (v.back(), pcs[2]));
                break;
            }
            case powerShape:
            {
                int x = 40;
                while (pc (x) != pcs[0]) ++x;
                v = { x, x + 7, x + 12 };
                break;
            }
        }
        return v;
    };
    std::vector<int> accents;              // where the chord speaks (funk, disco) or is struck (strum)
    std::vector<int> pattern;
    switch (way)
    {
        case funk:  accents = pick (rng, { { 0, 3, 6, 10, 13 }, { 2, 6, 7, 10, 14 }, { 0, 6, 8, 11, 14 }, { 3, 6, 9, 12, 15 }, { 0, 2, 7, 10, 12, 15 } }); break;
        case disco: accents = pick (rng, { { 2, 6, 10, 14 }, { 2, 6, 7, 10, 14, 15 }, { 1, 2, 5, 6, 9, 10, 13, 14 }, { 2, 3, 6, 10, 11, 14 } }); break;
        case skank: pattern = pick (rng, { { 2, 6, 10, 14 }, { 2, 6, 10, 14 }, { 2, 3, 6, 10, 11, 14 }, { 4, 12 } }); break;
        case strum: pattern = pick (rng, { { 0, 4, 6, 10, 12, 14 }, { 0, 2, 4, 6, 8, 10, 12, 14 }, { 0, 3, 6, 8, 10, 12, 14 }, { 0, 6, 8, 14 }, { 0, 4, 8, 12 },
                                           { 0, 3, 6, 10, 12 }, { 0, 2, 6, 8, 10, 14 } }); break;
        case power: pattern = pick (rng, { { 0, 2, 4, 6, 8, 10, 12, 14 }, euclid (16, 16), { 0, 2, 3, 6, 8, 10, 11, 14 }, { 0, 3, 6, 8, 11, 14 } }); break;
        case tresillo: pattern = twice ({ 0, 3, 6 }); break;
        default: break;
    }
    // the riff: a one-bar line on the chord, chosen once
    std::vector<int> riffSteps = pick (rng, { { 0, 3, 6, 8, 10, 12, 14 }, { 0, 2, 3, 6, 8, 11, 14 }, { 0, 4, 6, 10, 12 }, euclid (7, 16, 0), { 0, 3, 7, 10, 12, 14 } });
    std::vector<int> riffColour;
    for (size_t k = 0; k < riffSteps.size(); ++k) riffColour.push_back (k == 0 ? 0 : pick (rng, { { 0 }, { 7 }, { 4 }, { 2 }, { 6 }, { 0 } })[0]);
    const int pickStyle = rng.nextInt (3);      // Travis, rolling sixteenths, arpeggio of three
    const int picksPerBeat = o.estilo == Estilo::ambient ? 1 : 2;

    for (auto& bar : bars)
    {
        const auto shape = shapeFor (bar.degree);
        const int total = (int) bar.length * 4;
        const bool lastOfPhrase = bar.phraseEnd && ! bar.last;
        auto strike = [&] (double t, double len, bool down, int vel, int strings, double spread)
        {
            const int n = juce::jmin (strings, (int) shape.size());
            for (int sIdx = 0; sIdx < n; ++sIdx)
            {
                const int string = down ? (int) shape.size() - n + sIdx : (int) shape.size() - 1 - sIdx;
                const double off = sIdx * spread;
                out.push_back ({ t + off, juce::jmax (0.04, len - off), clampPitch (shape[(size_t) string]), humanVel (rng, vel - sIdx * 2, 4) });
            }
        };
        switch (way)
        {
            case funk: case disco:
            {
                for (int i = 0; i < juce::jmin (steps, total); ++i)
                {
                    const double t = bar.start + i * 0.25;
                    const bool down = i % 2 == 0;
                    std::vector<int> acc = accents;
                    if (lastOfPhrase && rng.nextBool()) acc = { 0, 3, 6, 9, 12 };
                    if (hasStep (acc, i)) strike (t, way == disco ? 0.16 : 0.12, down, way == disco ? 96 : 100, 4, 0.008);
                    else if (rng.nextInt (100) < (way == funk ? 80 : 55)) strike (t, 0.035, down, 34, 3, 0.005);   // a dead strum
                }
                break;
            }
            case skank: case strum: case power: case tresillo:
            {
                std::vector<int> pat = pattern;
                if (lastOfPhrase && way != power) { pat.erase (std::remove_if (pat.begin(), pat.end(), [] (int i) { return i >= 12; }), pat.end()); pat.push_back (14); }
                for (size_t k = 0; k < pat.size(); ++k)
                {
                    if (pat[k] >= juce::jmin (steps, total)) continue;
                    const double t = bar.start + pat[k] * 0.25;
                    const double next = k + 1 < pat.size() ? bar.start + pat[k + 1] * 0.25 : bar.start + bar.length;
                    if (way == skank) strike (t, 0.14, false, 92, 3, 0.006);
                    else if (way == power) strike (t, 0.12, true, pat[k] % 4 == 0 ? 100 : 86, 3, 0.004);
                    else if (way == tresillo)
                    {
                        // the nylon picks the 3-3-2: the bass on the first, two top strings on the others
                        const int sIdx = k % 3 == 0 ? 0 : (int) shape.size() - 1 - (int) (k % 2);
                        out.push_back ({ t, (next - t) * 0.9, clampPitch (shape[(size_t) juce::jlimit (0, (int) shape.size() - 1, sIdx)]), humanVel (rng, k % 3 == 0 ? 92 : 80, 4) });
                    }
                    else
                    {
                        const bool down = pat[k] % 4 == 0 || pat[k] % 4 == 1;
                        strike (t, juce::jmax (0.1, (next - t) * 0.92), down, down ? 96 : 78, down ? 6 : 4, 0.018);
                    }
                }
                break;
            }
            case picking:
            {
                const double rate = 1.0 / (picksPerBeat * (pickStyle == 1 ? 2 : 1));
                int k = 0;
                for (double t = 0.0; t < bar.length - 1.0e-6; t += rate, ++k)
                {
                    int sIdx;
                    if (pickStyle == 0) sIdx = k % 2 == 0 ? (k % 4 == 0 ? 0 : 1) : (int) shape.size() - 1 - (k / 2) % 3;     // Travis: alternating bass
                    else if (pickStyle == 1) { static const int roll[] { 0, 2, 3, 4, 5, 4, 3, 2 }; sIdx = roll[k % 8]; }
                    else { static const int three[] { 0, 3, 4, 5, 4, 3 }; sIdx = three[k % 6]; }
                    sIdx = juce::jlimit (0, (int) shape.size() - 1, sIdx);
                    out.push_back ({ bar.start + t, rate * (sIdx == 0 ? 3.5 : 1.6), clampPitch (shape[(size_t) sIdx]), humanVel (rng, sIdx == 0 ? 90 : 76, 5) });
                }
                break;
            }
            case riff:
            {
                int root = 40;                                             // the riff on the low strings
                while (pc (root) != chordPcs (o, bar.degree, false)[0]) ++root;
                for (size_t k = 0; k < riffSteps.size(); ++k)
                {
                    if (riffSteps[k] >= juce::jmin (steps, total)) continue;
                    const double t = bar.start + riffSteps[k] * 0.25;
                    const double next = k + 1 < riffSteps.size() ? bar.start + riffSteps[k + 1] * 0.25 : bar.start + bar.length;
                    int p = root;
                    switch (riffColour[k]) { case 7: p = root + 12; break; case 4: p = degreePitch (0, o.root, o.minor, bar.degree + 4); break;
                                             case 2: p = degreePitch (0, o.root, o.minor, bar.degree + 2); break; case 6: p = degreePitch (0, o.root, o.minor, bar.degree + 6); break; default: break; }
                    while (p < root) p += 12;
                    while (p > root + 12) p -= 12;
                    out.push_back ({ t, juce::jmax (0.1, (next - t) * 0.8), clampPitch (p), humanVel (rng, riffSteps[k] % 4 == 0 ? 98 : 86, 5) });
                }
                break;
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
            for (int d : { 0, 2, 4 }) tones.push_back (degreePitch (48, o.root, o.minor, bar.degree + d + oct * 7));
        while (tones.back() > 77) for (auto& t : tones) t -= 12;
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
    // four times in ten, a rhythm of the book instead: syncopated motifs that dance music melodies use
    if (! sung && o.estilo != Estilo::ambient && rng.nextInt (100) < 40)
        rhythm = fit (pick (rng, { { 0, 3, 6, 10 }, { 0, 2, 3, 6, 8, 10, 11 }, { 2, 4, 7, 10, 12 }, { 0, 6, 8, 14 }, { 0, 3, 6, 8, 11 },
                                   { 2, 6, 7, 10 }, { 0, 4, 6, 7, 10 }, { 0, 3, 4, 7, 10, 12 } }), steps);
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
    const int base = sung ? 48 : (o.root >= 6 ? 36 : 48);   // low and dark, an octave under the old line
    const int top = sung ? 67 : 74, bottom = sung ? 48 : 40;
    auto chordTone = [&] (int d, int chordDeg)
    {
        int bestD = d, bestDist = 99;
        for (int c : { chordDeg, chordDeg + 2, chordDeg + 4, chordDeg - 7, chordDeg - 5, chordDeg - 3, chordDeg + 7 })
            if (std::abs (c - d) < bestDist) { bestDist = std::abs (c - d); bestD = c; }
        return bestD;
    };
    // how the motif develops inside each phrase (the songwriting books' tools): bar 2 repeats it with another
    // ending, sequences it (the same shape moved with the chord) or displaces it (it starts an eighth or a quarter
    // later); bar 3 repeats it, truncates it (its first half twice) or displaces it; bar 4 answers and lands
    const int techA = rng.nextInt (3), techB = rng.nextInt (3);
    const int shift = rng.nextBool() ? 2 : 4;
    int phraseFirstDegree = bars.empty() ? 0 : bars[0].degree;
    for (auto& bar : bars)
    {
        // A A' A B: the second bar changes its ending, the fourth answers and lands
        const int part = bar.inPhrase;
        if (part == 0) phraseFirstDegree = bar.degree;
        std::vector<int> rh = rhythm;
        int transpose = 0;
        bool truncated = false;
        if ((part == 1 && techA == 2) || (part == 2 && techB == 2))
        {
            for (auto& x : rh) x = (x + shift) % steps;
            std::sort (rh.begin(), rh.end());
            rh.erase (std::unique (rh.begin(), rh.end()), rh.end());
        }
        if (part == 1 && techA == 1) transpose = bar.degree - phraseFirstDegree;
        if (part == 2 && techB == 1 && rh.size() >= 4)
        {
            std::vector<int> half;
            for (int x : rh) if (x < steps / 2) half.push_back (x);
            if (half.size() >= 2) { rh = half; for (int x : half) rh.push_back (x + steps / 2); truncated = true; }
        }
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
            const size_t half = truncated ? rh.size() / 2 : rh.size();
            int d = motif[juce::jmin (truncated ? k % juce::jmax ((size_t) 1, half) : k, motif.size() - 1)] + transpose;
            if (part == 1 && techA == 0 && k + 1 == rh.size()) d += rng.nextBool() ? 1 : -1;
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
    // each time another texture over the key: a pedal held through each phrase (the tonic low, its fifth, the octave
    // or the ninth entering halfway), a slow line on the chord tones over the low tonic, a pulse that breathes with
    // gaps, or a bloom (the chord entering one note per beat and held to the end of the bar pair)
    const int lowOct = rng.nextBool() ? 0 : 12;
    const int low = degreePitch (36, o.root, o.minor, 0) + (o.root >= 7 ? -12 : 0) + lowOct;
    const int fifth = degreePitch (36, o.root, o.minor, 4) + (o.root >= 7 ? -12 : 0) + lowOct;
    const bool ninth = rng.nextBool();
    const int high = degreePitch (48, o.root, o.minor, ninth ? 1 : 0);
    const int phrase = juce::jmin (4, (int) bars.size());
    const int kind = rng.nextInt (4);
    if (kind == 0)
    {
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
    else if (kind == 1)
    {
        // the slow line: a chord tone each bar, moving by the nearest step, over the tonic held through the phrase
        int last = 60 + rng.nextInt (8);
        for (size_t b = 0; b < bars.size(); ++b)
        {
            const auto pcs = chordPcs (o, bars[b].degree, false);
            int best = last, bestD = 99;
            for (int c : pcs)
                for (int p = 55; p <= 76; ++p)
                    if (pc (p) == pc (c))
                    {
                        const int d = std::abs (p - last) + (p == last ? 3 : 0) + rng.nextInt (3);
                        if (d < bestD) { bestD = d; best = p; }
                    }
            last = best;
            out.push_back ({ bars[b].start, bars[b].length * 0.98, clampPitch (best), humanVel (rng, 56, 5) });
            if (b % (size_t) phrase == 0)
                out.push_back ({ bars[b].start, juce::jmin (phrase, (int) (bars.size() - b)) * bars[b].length * 0.99, clampPitch (low), humanVel (rng, 64, 3) });
        }
    }
    else if (kind == 2)
    {
        // the pulse: the tonic and its fifth in quarters or eighths, some beats left out, louder towards the phrase end
        const double step = rng.nextBool() ? 1.0 : 0.5;
        const int gaps = 15 + rng.nextInt (25);
        for (size_t b = 0; b < bars.size(); ++b)
            for (double t = 0.0; t < bars[b].length - 1.0e-6; t += step)
            {
                if (t > 0.0 && rng.nextInt (100) < gaps) continue;
                const int vel = 46 + bars[b].inPhrase * 6 + (t == 0.0 ? 8 : 0);
                out.push_back ({ bars[b].start + t, step * 0.8, clampPitch (low), humanVel (rng, vel, 4) });
                if (rng.nextInt (3) > 0) out.push_back ({ bars[b].start + t, step * 0.8, clampPitch (fifth + 12), humanVel (rng, vel - 8, 4) });
            }
    }
    else
    {
        // the bloom: every two bars the chord comes in from the bottom, one note per beat, all held to the end
        for (size_t b = 0; b < bars.size(); b += 2)
        {
            const int n = juce::jmin (2, (int) (bars.size() - b));
            const double end = bars[b].start + n * bars[b].length - 0.05;
            const auto pcs = chordPcs (o, bars[b].degree, false);
            std::vector<int> notes { low, fifth + 12 };
            for (int c : pcs) { int p = 60; while (pc (p) != pc (c)) ++p; notes.push_back (p); }
            notes.push_back (high + 12 <= 84 ? high : high - 12);
            std::sort (notes.begin(), notes.end());
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const double at = bars[b].start + (double) k * (rng.nextBool() ? 1.0 : 0.5);
                if (at >= end - 0.25) break;
                out.push_back ({ at, end - at, clampPitch (notes[k]), humanVel (rng, 50 + (int) k * 3, 4) });
            }
        }
    }
}

// ============================================================================================================ drums
// How a drum part is built (from the drum-machine pattern books, Grids' skeletons and the usual programming advice):
//   a skeleton of the style (kick and backbeat), chosen out of many; the hats in one of several shapes with their
//   own accents; layers on top (rim, shaker, cowbell, ride, congas) made of Euclidean rhythms; ghost snares at
//   ~40 velocity between kick and snare. Bars 1-3 of each phrase play the groove (bar 2 or 3 with a small change: a
//   pickup kick, one hat less, a ghost more), bar 4 ends with a fill of the style (a snare roll, toms from high to
//   low, a clap stutter, a drop that leaves the last beat empty, an open hat into a crash). Later phrases may add a
//   layer, so a 16-bar part grows.
inline void bateria (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    const double step = 0.25;
    const auto& d = o.drums;
    std::vector<int> kick, back, closed, open, ghostSpots, crashAt;
    int backVoice = 0;                   // 0 clap, 1 snare, 2 both, 3 rim
    int hatAccent = 0;                   // 0 on the beat, 1 off the beat, 2 flat, 3 the "and" of each beat
    double swing = 0.0;
    bool trapRolls = false, ride = false, triplets = false;
    int ghostChance = 0;
    struct Layer { int note; std::vector<int> steps; int vel; double len; };
    std::vector<Layer> layers;
    auto layer = [&] (int note, std::vector<int> st, int vel, double len = 0.2)
    {
        st.erase (std::remove_if (st.begin(), st.end(), [steps] (int i) { return i >= steps; }), st.end());
        if (! st.empty()) layers.push_back ({ note, st, vel, len });
    };
    const int r16 = rng.nextInt (16);
    switch (o.estilo)
    {
        case Estilo::cuatro:
        {
            static const std::vector<std::vector<int>> kicks { { 0, 4, 8, 12 }, { 0, 4, 8, 12 }, { 0, 4, 8, 12, 14 }, { 0, 4, 8, 11, 12 }, { 0, 4, 7, 8, 12 },
                                                               { 0, 3, 4, 8, 12 }, { 0, 4, 8, 10, 12 }, { 0, 4, 8, 12, 15 }, { 0, 4, 6, 8, 12 }, { 0, 4, 8, 9, 12 } };
            kick = pick (rng, kicks);
            back = { 4, 12 };
            backVoice = weighted (rng, { 40, 20, 25, 15 });
            switch (weighted (rng, { 22, 20, 18, 15, 15, 10 }))
            {
                case 0: closed = {}; open = { 2, 6, 10, 14 }; break;                                         // the classic: open hat off the beat
                case 1: closed = euclid (16, 16); open = rng.nextBool() ? std::vector<int> { 2, 6, 10, 14 } : std::vector<int> { 14 }; break;
                case 2: closed = { 0, 2, 4, 6, 8, 10, 12, 14 }; open = rng.nextBool() ? std::vector<int> { 14 } : std::vector<int> {}; break;
                case 3: closed = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }; open = {}; break;            // tech: every sixteenth but the kick's
                case 4: closed = { 2, 3, 6, 7, 10, 11, 14, 15 }; open = {}; break;                         // the gallop
                default: closed = { 0, 3, 4, 7, 8, 11, 12, 15 }; open = { 2, 6, 10, 14 }; break;
            }
            hatAccent = rng.nextInt (4);
            if (rng.nextInt (100) < 35) layer (d.rim, euclid (rng.nextBool() ? 3 : 5, 16, r16), 74);
            if (rng.nextInt (100) < 30) layer (d.shaker, rng.nextBool() ? euclid (16, 16) : std::vector<int> { 2, 6, 10, 14 }, 70, 0.15);
            if (rng.nextInt (100) < 15) { ride = true; layer (d.ride, { 2, 6, 10, 14 }, 80, 0.4); }
            if (rng.nextInt (100) < 12) layer (d.cowbell, euclid (3, 8, rng.nextInt (8)), 72);
            ghostChance = rng.nextInt (100) < 25 ? 18 : 0;
            swing = rng.nextInt (100) < 30 ? 0.12 : 0.0;
            break;
        }
        case Estilo::afro:
        {
            static const std::vector<std::vector<int>> kicks { { 0, 4, 8, 12 }, { 0, 4, 8, 12 }, { 0, 4, 8, 12, 14 }, { 0, 4, 7, 8, 12 }, { 0, 3, 4, 8, 12 }, { 0, 4, 8, 11, 12 }, { 0, 6, 8, 12 } };
            kick = pick (rng, kicks);
            back = weighted (rng, { 55, 30, 15 }) == 0 ? std::vector<int> { 4, 12 } : (rng.nextBool() ? std::vector<int> { 12 } : std::vector<int> {});
            backVoice = weighted (rng, { 45, 15, 15, 25 });
            closed = rng.nextBool() ? euclid (16, 16) : std::vector<int> { 2, 6, 10, 14 };
            open = rng.nextInt (100) < 40 ? std::vector<int> { 14 } : std::vector<int> {};
            hatAccent = rng.nextBool() ? 1 : 3;
            // two or three hand-drum layers over the kit: tresillo, bossa bell, samba lines
            const std::vector<std::vector<int>> hand { twice ({ 0, 3, 6 }), euclid (5, 16, r16), euclid (7, 16, rng.nextInt (16)), { 3, 6, 10, 14 }, { 2, 7, 10, 15 }, euclid (3, 16, rng.nextInt (16)) };
            layer (d.congaHi, pick (rng, hand), 84);
            if (rng.nextBool()) layer (d.congaLo, pick (rng, hand), 78);
            if (rng.nextInt (100) < 60) layer (d.shaker, euclid (16, 16), 66, 0.15);
            if (rng.nextInt (100) < 35) layer (d.cowbell, euclid (5, 16, rng.nextInt (16)), 70);
            if (rng.nextInt (100) < 45) layer (d.rim, twice ({ 0, 3, 6 }), 76);
            swing = rng.nextInt (100) < 40 ? 0.15 : 0.0;
            break;
        }
        case Estilo::trap:
        {
            static const std::vector<std::vector<int>> kicks { { 0, 7, 10 }, { 0, 3, 10, 11 }, { 0, 8, 11 }, { 0, 6, 10, 14 }, { 0, 2, 7, 10 }, { 0, 11, 14 },
                                                               { 0, 6, 7, 13 }, { 0, 3, 6, 10 }, { 0, 10 }, { 0, 7, 9, 14 } };
            kick = pick (rng, kicks);
            kick.erase (std::remove (kick.begin(), kick.end(), 8), kick.end());
            back = { 8 };
            backVoice = weighted (rng, { 40, 25, 35, 0 });
            switch (weighted (rng, { 35, 35, 30 }))
            {
                case 0: closed = { 0, 2, 4, 6, 8, 10, 12, 14 }; break;
                case 1: closed = euclid (16, 16); break;
                default: closed = {}; triplets = true; break;                                     // hats in triplets (1/8T)
            }
            trapRolls = true;
            open = rng.nextInt (100) < 35 ? std::vector<int> { rng.nextBool() ? 14 : 6 } : std::vector<int> {};
            hatAccent = 0;
            if (rng.nextInt (100) < 35) layer (d.rim, { 6, 14 }, 70);
            if (rng.nextInt (100) < 20) layer (d.perc, euclid (3, 16, rng.nextInt (16)), 68);
            break;
        }
        case Estilo::dnb:
        {
            switch (weighted (rng, { 30, 20, 20, 15, 15 }))
            {
                case 0: kick = { 0, 10 }; back = { 4, 12 }; break;
                case 1: kick = { 0, 6, 10 }; back = { 4, 12 }; break;
                case 2: kick = { 0, 2, 10, 11 }; back = { 4, 7, 9, 12, 15 }; break;               // amen-like
                case 3: kick = { 0, 10, 13 }; back = { 4, 12 }; break;
                default: kick = { 0, 7, 10 }; back = { 4, 12 }; break;
            }
            backVoice = 1;
            ghostChance = 30;
            if (rng.nextBool()) closed = { 0, 2, 4, 6, 8, 10, 12, 14 };
            else if (rng.nextBool()) closed = euclid (16, 16);
            else { ride = true; layer (d.ride, { 0, 2, 4, 6, 8, 10, 12, 14 }, 78, 0.4); }
            open = rng.nextInt (100) < 30 ? std::vector<int> { 14 } : std::vector<int> {};
            hatAccent = 1;
            if (rng.nextInt (100) < 30) layer (d.shaker, euclid (16, 16), 60, 0.12);
            break;
        }
        case Estilo::latino:
        {
            // the dembow's 3-3-2 stays (steps 3, 6, 11, 14); the variants add a hit at the end of the bar
            static const std::vector<std::vector<int>> backs { { 3, 6, 11, 14 }, { 3, 6, 11, 14 }, { 3, 6, 11, 14, 15 }, { 3, 6, 11, 13, 14 }, { 3, 6, 11, 14, 15 } };
            kick = rng.nextInt (100) < 80 ? std::vector<int> { 0, 4, 8, 12 } : std::vector<int> { 0, 3, 4, 8, 11, 12 };
            back = pick (rng, backs);
            backVoice = weighted (rng, { 25, 45, 30, 0 });
            closed = rng.nextBool() ? std::vector<int> { 0, 2, 4, 6, 8, 10, 12, 14 } : euclid (16, 16);
            hatAccent = rng.nextInt (4);
            if (rng.nextInt (100) < 50) layer (d.shaker, euclid (16, 16), 72, 0.12);                     // the güira
            if (rng.nextInt (100) < 45) layer (d.congaLo, { 6, 7, 14, 15 }, 80);                         // tumbao
            if (rng.nextInt (100) < 35) layer (d.congaHi, euclid (3, 8, rng.nextInt (8)), 76);
            if (rng.nextInt (100) < 30) layer (d.cowbell, { 0, 4, 8, 12 }, 70);
            if (rng.nextInt (100) < 30) layer (d.rim, rng.nextBool() ? std::vector<int> { 0, 3, 6, 10, 12 } : std::vector<int> { 0, 3, 7, 10, 12 }, 78);  // son / rumba clave
            break;
        }
        case Estilo::boombap:
        {
            static const std::vector<std::vector<int>> kicks { { 0, 7, 10 }, { 0, 3, 8, 10 }, { 0, 10, 11 }, { 0, 6, 9 }, { 0, 2, 7, 10 }, { 0, 7, 9, 10 },
                                                               { 0, 3, 10 }, { 0, 8, 10, 15 } };
            kick = pick (rng, kicks);
            back = { 4, 12 };
            backVoice = weighted (rng, { 15, 70, 15, 0 });
            closed = rng.nextInt (100) < 65 ? std::vector<int> { 0, 2, 4, 6, 8, 10, 12, 14 } : euclid (16, 16);
            open = rng.nextInt (100) < 30 ? std::vector<int> { 14 } : std::vector<int> {};
            hatAccent = rng.nextBool() ? 0 : 2;
            ghostChance = 25 + rng.nextInt (20);
            swing = 0.25 + rng.nextFloat() * 0.25;
            if (rng.nextInt (100) < 25) layer (d.shaker, { 2, 6, 10, 14 }, 64, 0.15);
            if (rng.nextInt (100) < 20) layer (d.rim, euclid (3, 16, rng.nextInt (16)), 70);
            break;
        }
        case Estilo::ambient:
            kick = pick (rng, { { 0 }, { 0, 10 }, { 0, 7 }, { 0, 11 } });
            back = rng.nextBool() ? std::vector<int> { 12 } : std::vector<int> {};
            backVoice = rng.nextBool() ? 3 : 0;
            closed = euclid (3 + rng.nextInt (4), 16, rng.nextInt (16));
            hatAccent = 2;
            if (rng.nextBool()) layer (d.shaker, euclid (5, 16, rng.nextInt (16)), 56, 0.2);
            break;
    }
    // inside the bar (a 3/4 bar has 12 steps); an empty line stays empty
    auto within = [steps] (std::vector<int>& v) { v.erase (std::remove_if (v.begin(), v.end(), [steps] (int i) { return i >= steps; }), v.end()); };
    within (kick); within (back); within (open); within (closed);
    if (kick.empty()) kick = { 0 };
    closed.erase (std::remove_if (closed.begin(), closed.end(), [&] (int i) { return hasStep (open, i); }), closed.end());
    // the ghost snares: between the kick and the backbeat, where the hand that does not play the hat is free
    for (int i = 1; i < steps; i += 2)
        if (ghostChance > 0 && ! hasStep (back, i) && ! hasStep (kick, i) && rng.nextInt (100) < ghostChance) ghostSpots.push_back (i);
    // the small change of bar 2 or 3: a pickup kick before the next bar, a ghost or a hat less
    const int varBar = rng.nextBool() ? 1 : 2;
    const int varKind = rng.nextInt (3);
    const int varStep = pick (rng, { { 14 }, { 15 }, { 11 }, { 6 } })[0];
    const int fillKind = o.estilo == Estilo::trap ? weighted (rng, { 10, 10, 30, 30, 20 }) : weighted (rng, { 30, 25, 20, 15, 10 });
    auto hatVel = [&] (int i)
    {
        switch (hatAccent)
        {
            case 0:  return i % 4 == 0 ? 92 : (i % 2 == 0 ? 74 : 56);
            case 1:  return i % 4 == 2 ? 94 : (i % 2 == 0 ? 66 : 58);
            case 3:  return i % 4 == 3 ? 88 : (i % 4 == 2 ? 74 : 60);
            default: return 76;
        }
    };
    const int phrase = juce::jmin (4, (int) bars.size());
    for (size_t b = 0; b < bars.size(); ++b)
    {
        auto& bar = bars[b];
        const int ph = (int) b / juce::jmax (1, phrase);
        const bool fill = bar.phraseEnd && bars.size() > 1 && rng.nextInt (100) < 80;
        const int fillFrom = steps - (fillKind == 0 && o.estilo != Estilo::latino && rng.nextBool() ? 8 : 4);
        // a crash opens every phrase after the first, one time in two
        if (bar.inPhrase == 0 && ph > 0 && rng.nextBool()) out.push_back ({ bar.start, 1.0, d.crash, humanVel (rng, 96) });
        for (int i = 0; i < steps; ++i)
        {
            double t = bar.start + i * step;
            if (i % 2 == 1) t += step * swing;
            const bool inFill = fill && i >= fillFrom;
            const bool dropped = inFill && fillKind == 3;             // the drop: the last beat is silent
            if (dropped) continue;
            bool k = hasStep (kick, i);
            if (bar.inPhrase == varBar && varKind == 0 && i == varStep) k = true;
            if (k && ! (inFill && (fillKind == 1 || fillKind == 0) && i > fillFrom))
                out.push_back ({ t, step, d.kick, humanVel (rng, i % 4 == 0 ? 114 : 98, 5) });
            if (hasStep (back, i) && ! (inFill && fillKind <= 2))
            {
                const int v = humanVel (rng, 104, 5);
                if (backVoice == 0 || backVoice == 2) out.push_back ({ t, step, d.clap, v });
                if (backVoice == 1 || backVoice == 2) out.push_back ({ t, step, d.snare, backVoice == 2 ? v - 10 : v });
                if (backVoice == 3) { out.push_back ({ t, step, d.clap, v - 4 }); out.push_back ({ t, step, d.rim, v - 10 }); }
            }
            if (hasStep (ghostSpots, i) || (bar.inPhrase == varBar && varKind == 1 && i == varStep))
                out.push_back ({ t, step * 0.5, d.snare, humanVel (rng, 40, 6) });
            if (hasStep (open, i)) out.push_back ({ t, step * 1.6, d.openHat, humanVel (rng, 88, 5) });
            else if (hasStep (closed, i) && ! (bar.inPhrase == varBar && varKind == 2 && i == varStep))
            {
                out.push_back ({ t, step * 0.5, d.closedHat, humanVel (rng, hatVel (i), 5) });
                // trap: a roll of thirty-seconds or a sixteenth triplet on some beats
                if (trapRolls && i % 4 == 2 && rng.nextInt (100) < 22)
                {
                    const int n = rng.nextBool() ? 2 : 3;
                    for (int r = 1; r <= n; ++r) out.push_back ({ t + r * step / n, step / n, d.closedHat, humanVel (rng, 60 + r * 6, 4) });
                }
            }
            for (auto& l : layers)
                if (hasStep (l.steps, i) && ! inFill && (ph > 0 || &l == &layers.front() || rng.nextInt (100) < 85))
                    out.push_back ({ t, l.len, l.note, humanVel (rng, l.vel + (i % 4 == 0 ? 6 : 0), 6) });
            // the fill
            if (inFill)
            {
                const int k2 = i - fillFrom, n = steps - fillFrom;
                switch (fillKind)
                {
                    case 0: out.push_back ({ t, step * 0.5, d.snare, juce::jlimit (1, 127, 62 + k2 * (56 / juce::jmax (1, n))) }); break;   // snare roll, rising
                    case 1: { const int toms[] { d.tomHi, d.tomHi, d.tom, d.tom, d.tomLo, d.tomLo, d.tomLo, d.tomLo };
                              out.push_back ({ t, step, toms[juce::jlimit (0, 7, k2 * 8 / juce::jmax (1, n))], juce::jlimit (1, 127, 88 + k2 * 4) }); break; }
                    case 2: if (i % 2 == 0 || k2 >= n - 2) out.push_back ({ t, step * 0.5, o.estilo == Estilo::trap ? d.snare : d.clap, juce::jlimit (1, 127, 76 + k2 * 8) }); break;  // stutter
                    case 4: if (k2 == n - 2) out.push_back ({ t, step * 2.0, d.openHat, 100 }); break;                                              // open hat into the next bar
                    default: break;
                }
            }
        }
        // triplet hats: three per beat
        if (triplets)
            for (int beat = 0; beat < steps / 4; ++beat)
                for (int k3 = 0; k3 < 3; ++k3)
                {
                    const double t = bar.start + beat + k3 / 3.0;
                    if (fill && t >= bar.start + fillFrom * step && fillKind == 3) continue;
                    out.push_back ({ t, 0.15, d.closedHat, humanVel (rng, k3 == 0 ? 88 : 64, 5) });
                }
        // the next phrases may bring one more layer: a ride or a shaker from bar 5 on
        if (bar.inPhrase == 3 && ph == 0 && layers.size() < 4 && rng.nextInt (100) < 45)
            layer (rng.nextBool() ? d.shaker : d.ride, rng.nextBool() ? std::vector<int> { 2, 6, 10, 14 } : euclid (16, 16), 62, 0.15);
    }
    juce::ignoreUnused (ride, crashAt);
}

// ============================================================================================================ percussion
// The layer over the drums (no kick, no snare): the shaker in one of its shapes, and two or three hand drums out of
// the style's figures (tumbao, tresillo, son and rumba clave, bossa and samba lines as Euclidean rhythms), each one
// turned so they answer each other. The last bar of a phrase rolls on the congas.
inline void percusion (const Opciones& o, juce::Random& rng, const std::vector<Bar>& bars, int steps, std::vector<Nota>& out)
{
    const auto& d = o.drums;
    struct Layer { int note; std::vector<int> steps; int vel; double len; };
    std::vector<Layer> layers;
    auto add = [&] (int note, std::vector<int> st, int vel, double len = 0.22)
    {
        st.erase (std::remove_if (st.begin(), st.end(), [steps] (int i) { return i >= steps; }), st.end());
        if (! st.empty()) layers.push_back ({ note, st, vel, len });
    };
    // the shaker: sixteenths with the accent on the "and", eighths, off the beat, or "e-and-a"
    if (o.estilo != Estilo::ambient || rng.nextBool())
        switch (weighted (rng, { 40, 20, 20, 20 }))
        {
            case 0: add (d.shaker, euclid (16, 16), 70, 0.15); break;
            case 1: add (d.shaker, { 0, 2, 4, 6, 8, 10, 12, 14 }, 74, 0.18); break;
            case 2: add (d.shaker, { 2, 6, 10, 14 }, 80, 0.18); break;
            default: add (d.shaker, { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 }, 66, 0.12); break;
        }
    std::vector<std::vector<int>> figures;
    switch (o.estilo)
    {
        case Estilo::afro:   figures = { twice ({ 0, 3, 6 }), euclid (5, 16, 0), euclid (7, 16, 2), { 3, 6, 10, 14 }, { 2, 7, 10, 15 }, { 0, 3, 7, 10, 12 } }; break;
        case Estilo::latino: figures = { { 6, 7, 14, 15 }, { 0, 3, 6, 10, 12 }, { 0, 3, 7, 10, 12 }, twice ({ 0, 3, 6 }), { 0, 4, 8, 12 }, euclid (5, 16, 3) }; break;
        case Estilo::trap: case Estilo::boombap: figures = { { 7 }, { 15 }, { 6, 14 }, euclid (3, 16, 5) }; break;
        case Estilo::ambient: figures = { euclid (3, 16, 1), euclid (5, 16, 7) }; break;
        default: figures = { { 3, 11 }, { 7, 15 }, { 3, 10 }, euclid (3, 8, 2), euclid (5, 16, 1), { 2, 7, 10, 15 }, twice ({ 0, 3, 6 }) }; break;
    }
    const int voices[] { d.congaHi, d.congaLo, d.bongoHi, d.bongoLo, d.cowbell, d.rim };
    const int n = o.estilo == Estilo::trap || o.estilo == Estilo::boombap || o.estilo == Estilo::ambient ? 1 + rng.nextInt (2) : 2 + rng.nextInt (2);
    std::vector<int> usedVoices;
    for (int k = 0; k < n; ++k)
    {
        int v = voices[rng.nextInt (o.estilo == Estilo::latino || o.estilo == Estilo::afro ? 6 : 4)];
        if (std::find (usedVoices.begin(), usedVoices.end(), v) != usedVoices.end()) continue;
        usedVoices.push_back (v);
        auto f = pick (rng, figures);
        if (rng.nextInt (100) < 30) { const int rot = rng.nextInt (4); for (auto& s : f) s = (s + rot) % 16; std::sort (f.begin(), f.end()); }
        add (v, f, v == d.cowbell ? 70 : 84);
    }
    for (auto& bar : bars)
    {
        const bool roll = bar.phraseEnd && bars.size() > 1 && rng.nextInt (100) < 70;
        for (int i = 0; i < steps; ++i)
        {
            const double t = bar.start + i * 0.25;
            if (roll && i >= steps - 4)
            {
                out.push_back ({ t, 0.22, i % 2 == 0 ? d.congaHi : d.congaLo, juce::jlimit (1, 127, 76 + (i - (steps - 4)) * 10) });
                continue;
            }
            for (auto& l : layers)
                if (hasStep (l.steps, i)) out.push_back ({ t, l.len, l.note, humanVel (rng, l.vel + (i % 4 == 2 && l.note == d.shaker ? 14 : 0), 6) });
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

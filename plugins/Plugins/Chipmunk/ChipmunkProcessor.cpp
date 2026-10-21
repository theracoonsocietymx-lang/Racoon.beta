#include "ChipmunkProcessor.h"
#include "ChipmunkEditor.h"

namespace racoon::chipmunk
{
using namespace juce;

// ===================================================================== harmony tables (version 1)
namespace harmony
{
const StringArray& chordNames()
{
    static StringArray a { "Mayor", "Menor", "7", "Maj7", "m7", "6", "m6", "9", "Maj9", "m9", "Add9", "Sus2", "Sus4", "7sus4",
                           "Disminuido", "Dim7", "m7b5", "Aumentado", "11", "13", "Quinta", "Octavas" };
    return a;
}

const std::vector<int>& chordIntervals (int t)
{
    static const std::vector<int> table[] {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 9 }, { 0, 3, 7, 9 },
        { 0, 4, 7, 10, 14 }, { 0, 4, 7, 11, 14 }, { 0, 3, 7, 10, 14 }, { 0, 4, 7, 14 }, { 0, 2, 7 }, { 0, 5, 7 }, { 0, 5, 7, 10 },
        { 0, 3, 6 }, { 0, 3, 6, 9 }, { 0, 3, 6, 10 }, { 0, 4, 8 }, { 0, 4, 7, 10, 14, 17 }, { 0, 4, 7, 10, 14, 21 }, { 0, 7 }, { 0, 12 } };
    return table[jlimit (0, (int) std::size (table) - 1, t)];
}

const StringArray& scaleNames()
{
    static StringArray a { "Mayor", "Menor", "Dórico"_u, "Frigio", "Lidio", "Mixolidio", "Locrio", "Menor armónica"_u, "Menor melódica"_u,
                           "Pentatónica mayor"_u, "Pentatónica menor"_u, "Blues", "Cromática"_u };
    return a;
}

const std::vector<int>& scale (int i)
{
    static const std::vector<int> table[] {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 },
        { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 4, 7, 9 },
        { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return table[jlimit (0, (int) std::size (table) - 1, i)];
}

String noteName (int p)
{
    static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((p % 12) + 12) % 12];
}

String nameOf (const std::vector<int>& pitches)
{
    if (pitches.empty()) return {};
    std::set<int> pcs;
    for (int p : pitches) pcs.insert (((p % 12) + 12) % 12);
    static const char* suffix[] { "", "m", "7", "maj7", "m7", "6", "m6", "9", "maj9", "m9", "add9", "sus2", "sus4", "7sus4",
                                  "dim", "dim7", "m7b5", "aug", "11", "13", "5", "" };
    const int lowest = *std::min_element (pitches.begin(), pitches.end());
    // prefer the reading whose root is the bass note
    for (int pass = 0; pass < 2; ++pass)
        for (int root : pcs)
        {
            if (pass == 0 && root != ((lowest % 12) + 12) % 12) continue;
            for (int t = 0; t < 21; ++t)
            {
                std::set<int> want;
                for (int iv : chordIntervals (t)) want.insert ((root + iv) % 12);
                if (want == pcs)
                {
                    String s = noteName (root) + suffix[t];
                    if (root != ((lowest % 12) + 12) % 12) s << "/" << noteName (lowest);
                    return s;
                }
            }
        }
    if (pcs.size() == 1) return noteName (lowest);
    return {};
}
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "chordmode", "Acordes", { "Apagado", "Fijo", "Diatónico"_u, "Memoria", "Progresión"_u }, 2);
    P::choice (l, "chordtype", "Tipo de acorde", harmony::chordNames(), 0);
    P::choice (l, "key", "Tonalidad", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    P::choice (l, "scale", "Escala", harmony::scaleNames(), 0);
    P::choice (l, "chordsize", "Notas del acorde", { "Tríada"_u, "Séptima"_u, "Novena" }, 0);
    P::toggle (l, "quantize", "Ajustar a la escala", false);
    P::integer (l, "transpose", "Transponer", -24, 24, 0);
    P::integer (l, "inversion", "Inversión"_u, 0, 3, 0);
    P::choice (l, "voicing", "Voicing", { "Cerrado", "Abierto", "Amplio" }, 0);
    P::integer (l, "rangelo", "Registro bajo", 24, 96, 48);
    P::integer (l, "rangehi", "Registro alto", 36, 108, 84);
    P::toggle (l, "voicelead", "Conducción de voces"_u, true);
    P::toggle (l, "bass", "Bajo", false);
    P::toggle (l, "ownsound", "Sonido propio", true);
    P::choice (l, "ownpatch", "Sonido", { "Teclas", "Pad", "Pluck", "Bajo" }, 0);
    static const int defProg[8] { 0, 4, 5, 3, 5, 3, 0, 4 };
    for (int k = 0; k < 8; ++k) P::choice (l, progId (k), "Progresión "_u + String (k + 1), { "I", "II", "III", "IV", "V", "VI", "VII" }, defProg[k]);
    P::integer (l, "progbase", "Teclas de progresión"_u, 24, 96, 48);
    P::toggle (l, "latch", "Latch", false);
    P::toggle (l, "hold", "Hold", false);

    P::toggle (l, "arp", "Arpegiador", false);
    P::choice (l, "arpmode", "Modo del arpegio", { "Arriba", "Abajo", "Péndulo"_u, "Al azar", "Como se tocó"_u, "Acorde" }, 0);
    P::choice (l, "arprate", "Velocidad del arpegio", RacoonProcessor::divisionNames(), 10);
    P::pct (l, "gate", "Gate", 1.0f, 200.0f, 80.0f);
    P::integer (l, "octaves", "Octavas", 1, 4, 1);
    P::integer (l, "steps", "Pasos", 1, 64, 16);
    P::pct (l, "swing", "Swing", 0.0f, 75.0f, 0.0f);
    P::integer (l, "accent", "Acento", 0, 60, 20);
    P::integer (l, "arpvel", "Velocidad fija", 0, 127, 0);
    P::pct (l, "variation", "Variación"_u, 0.0f, 100.0f, 0.0f);
    P::integer (l, "seed", "Semilla", 0, 9999, 1);
    for (int k = 0; k < tableSteps; ++k)
    {
        const String n = "Paso " + String (k + 1) + " ";
        P::toggle (l, stepId (k, "on"), n + "activo", true);
        P::toggle (l, stepId (k, "acc"), n + "acento", k % 4 == 0);
        P::pct (l, stepId (k, "prob"), n + "probabilidad", 0.0f, 100.0f, 100.0f);
        P::integer (l, stepId (k, "rat"), n + "ratchet", 1, 4, 1);
    }
    P::ms (l, "strum", "Strum", 0.0f, 200.0f, 0.0f, 40.0f);
    P::choice (l, "strumdir", "Dirección del strum"_u, { "Arriba", "Abajo", "Alternado" }, 0);
    P::ms (l, "humtime", "Humanizar tiempo", 0.0f, 30.0f, 0.0f, 10.0f);
    P::integer (l, "humvel", "Humanizar velocidad", 0, 30, 0);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Chipmunk", "Acordes y arpegios", Colour (0xffe0789a), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    for (int k = 0; k < tableSteps; ++k)
    {
        stepOn[k] = param (stepId (k, "on")); stepAcc[k] = param (stepId (k, "acc"));
        stepProb[k] = param (stepId (k, "prob")); stepRat[k] = param (stepId (k, "rat"));
    }
    for (int k = 0; k < 8; ++k) prog[k] = param (progId (k));
    for (auto& c : ccMap) c.store (-1);
    for (auto& c : lastChordPitches) c.store (0);
}

void Processor::prepareDsp (double, int) { resetDsp(); }

void Processor::resetDsp()
{
    numOn = numOff = 0;
    numHeld = 0; physicalCount = 0;
    std::fill (std::begin (sounding), std::end (sounding), false);
    std::fill (std::begin (chordRef), std::end (chordRef), 0);
    for (auto& m : mapping) m.clear();
    lastChord.clear();
    internalRunning = false;
    lastStep = std::numeric_limits<int64>::min();
    lastPpqEnd = -1.0;
    currentStep = -1;
}

double Processor::hash01 (uint32 seed, int64 k, int salt)
{
    uint64 z = (uint64) seed * 0x9E3779B97F4A7C15ull ^ (uint64) (k + 0x632BE59BD9B4E019ll) * 0xBF58476D1CE4E5B9ull ^ (uint64) salt * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double) (z >> 11) * (1.0 / 9007199254740992.0);
}

// ===================================================================== chords
Chord Processor::makeChord (int note, const Chord* previous) const
{
    const int mode = (int) value ("chordmode");
    const int key = (int) value ("key");
    const auto& sc = harmony::scale ((int) value ("scale"));
    const bool chromatic = sc.size() == 12;
    const int size = 3 + (int) value ("chordsize");

    auto snap = [&] (int p)
    {
        if (chromatic) return p;
        for (int d = 0; d < 12; ++d)
            for (int s : { -d, d })
            {
                const int pc = (((p + s - key) % 12) + 12) % 12;
                if (std::find (sc.begin(), sc.end(), pc) != sc.end()) return p + s;
            }
        return p;
    };
    if (value ("quantize") > 0.5f) note = snap (note);

    auto stack = [&] (Chord& c, int rootPitch, int degree)
    {
        const int len = (int) sc.size();
        for (int i = 0; i < size; ++i)
        {
            const int deg = degree + 2 * i;
            c.add (rootPitch - sc[(size_t) degree] + sc[(size_t) (deg % len)] + 12 * (deg / len));
        }
    };

    Chord c;
    int mainRoot = note;
    switch (mode)
    {
        case 0: c.add (note); break;
        case 2:
            if (! chromatic)
            {
                const int s = snap (note);
                const int pc = (((s - key) % 12) + 12) % 12;
                const int degree = (int) (std::find (sc.begin(), sc.end(), pc) - sc.begin());
                stack (c, s, degree);
                mainRoot = s;
                break;
            }
            [[fallthrough]];
        case 1:
            for (int iv : harmony::chordIntervals ((int) value ("chordtype"))) c.add (note + iv);
            break;
        case 3:
            for (auto& m : memory) { const int iv = m.load(); if (iv >= 0) c.add (note + iv); }
            break;
        default:
        {
            // progression: the white keys from the trigger note choose the chord (C D E F G A B C = slots 1..8)
            static const int white[12] { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
            const int rel = note - (int) value ("progbase");
            if (rel < 0) return c;
            const int w = white[rel % 12];
            if (w < 0) return c;
            const int slot = w + 7 * (rel / 12);
            if (slot > 7) return c;
            const int degree = jmin ((int) sc.size() - 1, (int) prog[slot]->load());
            const int lo = (int) value ("rangelo");
            const int root = lo + ((((key + sc[(size_t) degree]) - lo) % 12) + 12) % 12;
            if (chromatic) for (int iv : harmony::chordIntervals (0)) c.add (root + iv);
            else stack (c, root, degree);
            mainRoot = root;
            break;
        }
    }
    if (mode == 0) { if (const int t = (int) value ("transpose"); t != 0) c.p[0] += t; c.p[0] = jlimit (0, 127, c.p[0]); return c; }

    c.sort();
    // voicing: drop 2 or spread
    const int voicing = (int) value ("voicing");
    if (voicing == 1 && c.n >= 3) { c.p[(size_t) c.n - 2] -= 12; c.sort(); }
    else if (voicing == 2) { for (int i = 1; i < c.n; i += 2) c.p[(size_t) i] += 12; c.sort(); }

    const int lo = (int) value ("rangelo"), hi = jmax (lo + 12, (int) value ("rangehi"));
    const int transpose = (int) value ("transpose");
    auto finish = [&] (Chord x, int inv, int shift)
    {
        x.sort();
        for (int i = 0; i < inv && x.n > 1; ++i) { x.p[0] += 12; x.sort(); }
        for (int i = 0; i < x.n; ++i)
        {
            int p = x.p[(size_t) i] + shift + transpose;
            while (p < lo) p += 12;
            while (p > hi) p -= 12;
            x.p[(size_t) i] = jlimit (0, 127, p);
        }
        x.sort();
        Chord u;   // no duplicates
        for (int i = 0; i < x.n; ++i) if (! u.contains (x.p[(size_t) i])) u.add (x.p[(size_t) i]);
        return u;
    };

    Chord best = finish (c, (int) value ("inversion"), 0);
    if (value ("voicelead") > 0.5f && previous != nullptr && previous->n > 0)
    {
        int bestCost = std::numeric_limits<int>::max();
        for (int inv = 0; inv < c.n; ++inv)
            for (int shift : { -12, 0, 12 })
            {
                auto cand = finish (c, inv, shift);
                int cost = 0;
                for (int i = 0; i < cand.n; ++i)
                {
                    int d = 999;
                    for (int j = 0; j < previous->n; ++j) d = jmin (d, std::abs (cand.p[(size_t) i] - previous->p[(size_t) j]));
                    cost += d;
                }
                if (cost < bestCost) { bestCost = cost; best = cand; }
            }
    }
    if (value ("bass") > 0.5f)
    {
        int b = mainRoot + transpose;
        while (b >= best.p[0] && b - 12 >= 0) b -= 12;
        if (! best.contains (b)) { best.add (b); best.sort(); }
    }
    return best;
}

std::vector<int> Processor::chordFor (int note) const
{
    auto c = makeChord (note, nullptr);
    return std::vector<int> (c.p.begin(), c.p.begin() + c.n);
}

// ===================================================================== scheduling
void Processor::scheduleOn (int pitch, int vel, double at, int src, int64 id)
{
    if (numOn >= maxPending) return;
    pendingOn[numOn++] = { pitch, channel, vel, at, src, id };
}

void Processor::scheduleOff (int pitch, double at, int64 id)
{
    // id = -1 switches off whatever is sounding on that pitch; otherwise only the note-on with the same id
    if (numOff >= maxPending) return;
    pendingOff[numOff++] = { pitch, channel, 0, at, -1, id };
}

void Processor::emitOn (MidiBuffer& out, int pitch, int vel, int offset, int64 id)
{
    if (sounding[pitch]) out.addEvent (MidiMessage::noteOff (channel, pitch), offset);   // retrigger: close the previous one first
    out.addEvent (MidiMessage::noteOn (channel, pitch, (uint8) jlimit (1, 127, vel)), offset);
    sounding[pitch] = true;
    soundingId[pitch] = id;
}

void Processor::emitOff (MidiBuffer& out, int pitch, int offset)
{
    if (! sounding[pitch]) return;
    out.addEvent (MidiMessage::noteOff (channel, pitch), offset);
    sounding[pitch] = false;
}

void Processor::flushAll (MidiBuffer& out, int offset)
{
    for (int p = 0; p < 128; ++p) emitOff (out, p, offset);
    numOn = numOff = 0;
    std::fill (std::begin (chordRef), std::end (chordRef), 0);
    for (auto& m : mapping) m.clear();
}

// ===================================================================== input notes
void Processor::releaseDirect (MidiBuffer&, int inputNote, int offset)
{
    auto& m = mapping[inputNote];
    for (int i = 0; i < m.n; ++i)
    {
        const int p = m.p[(size_t) i];
        bool cancelled = false;
        for (int k = 0; k < numOn; ++k)
            if (pendingOn[k].src == inputNote && pendingOn[k].pitch == p) { pendingOn[k] = pendingOn[--numOn]; cancelled = true; break; }
        chordRef[p] = jmax (0, chordRef[p] - 1);
        if (! cancelled && chordRef[p] == 0) scheduleOff (p, offset);
    }
    m.clear();
}

void Processor::inputOn (MidiBuffer& out, int note, int vel, int offset)
{
    if (learnChord.load() && ! learning.contains (note)) learning.add (note);
    const bool arp = value ("arp") > 0.5f;

    // latch: a new chord after letting go of every key replaces the latched one
    if (value ("latch") > 0.5f && physicalCount == 0 && numHeld > 0)
    {
        for (int i = 0; i < numHeld; ++i) if (! arp) releaseDirect (out, held[i].note, offset);
        numHeld = 0;
    }
    bool exists = false;
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note) { exists = true; held[i].vel = vel; held[i].physical = true; held[i].order = ++orderCounter; }
    if (! exists && numHeld < 128) held[numHeld++] = { note, vel, ++orderCounter, true };
    ++physicalCount;

    if (! arp)
    {
        if (mapping[note].n > 0) releaseDirect (out, note, offset);
        Chord c = makeChord (note, &lastChord);
        if (c.n == 0) return;
        lastChord = c;
        mapping[note] = c;
        for (int i = 0; i < c.n; ++i) lastChordPitches[i % 8].store (c.p[(size_t) i]);
        lastChordSize.store (jmin (8, c.n));
        const double strum = value ("strum") * 0.001 * sr;
        const int dir = (int) value ("strumdir");
        const bool down = dir == 1 || (dir == 2 && strumFlip);
        if (dir == 2) strumFlip = ! strumFlip;
        const double hum = value ("humtime") * 0.001 * sr;
        const int humVel = (int) value ("humvel");
        const auto seed = (uint32) value ("seed");
        for (int i = 0; i < c.n; ++i)
        {
            const int p = c.p[(size_t) i];
            const int idx = down ? c.n - 1 - i : i;
            const double jitter = hum > 0.0 ? hash01 (seed, orderCounter * 16 + i, 7) * hum : 0.0;
            const int v = jlimit (1, 127, vel + (humVel > 0 ? roundToInt ((hash01 (seed, orderCounter * 16 + i, 8) * 2.0 - 1.0) * humVel) : 0));
            if (chordRef[p]++ == 0) scheduleOn (p, v, offset + strum * idx + jitter, note);
        }
    }
    else if (! internalRunning && ! isPlaying())
    {
        // free running: the pattern starts right on this note (step 0 lands on this sample)
        internalRunning = true;
        blockPpq0 = -(double) offset * bpm() / (60.0 * sr);
        lastStep = std::numeric_limits<int64>::min();
    }
}

void Processor::inputOff (MidiBuffer& out, int note, int offset)
{
    physicalCount = jmax (0, physicalCount - 1);
    if (learnChord.load() && physicalCount == 0 && learning.n > 0)
    {
        learning.sort();
        for (int i = 0; i < 8; ++i) memory[i].store (i < learning.n ? learning.p[(size_t) i] - learning.p[0] : -1);
        learning.clear();
        learnChord = false;
    }
    const bool keep = value ("hold") > 0.5f || value ("latch") > 0.5f;
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note)
        {
            if (keep) { held[i].physical = false; return; }
            held[i] = held[--numHeld];
            break;
        }
    if (value ("arp") < 0.5f) releaseDirect (out, note, offset);
}

// ===================================================================== arpeggiator
void Processor::buildPool (Pool& sorted, Pool& played) const
{
    sorted.clear(); played.clear();
    // held notes in the order they were played
    int order[128], cnt = 0;
    for (int i = 0; i < numHeld; ++i) order[cnt++] = i;
    std::sort (order, order + cnt, [this] (int a, int b) { return held[a].order < held[b].order; });
    Notes<128> base;
    for (int k = 0; k < cnt; ++k)
    {
        const auto& h = held[order[k]];
        const auto c = makeChord (h.note, nullptr);
        for (int i = 0; i < c.n; ++i) if (! base.contains (c.p[(size_t) i])) base.add (c.p[(size_t) i], h.vel);
    }
    const int octaves = (int) value ("octaves");
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < base.n; ++i)
        {
            const int p = base.p[(size_t) i] + 12 * o;
            if (p <= 127 && ! played.contains (p)) played.add (p, base.vel[(size_t) i]);
        }
    sorted = played;
    // sort by pitch keeping the velocities with their notes
    int idx[128];
    for (int i = 0; i < sorted.n; ++i) idx[i] = i;
    std::sort (idx, idx + sorted.n, [&] (int a, int b) { return played.p[(size_t) a] < played.p[(size_t) b]; });
    for (int i = 0; i < sorted.n; ++i) { sorted.p[(size_t) i] = played.p[(size_t) idx[i]]; sorted.vel[(size_t) i] = played.vel[(size_t) idx[i]]; }
}

void Processor::arpStep (int64 k, double at, double stepSamples, int)
{
    Pool sorted, played;
    buildPool (sorted, played);
    const int steps = (int) value ("steps");
    const int patternStep = (int) (((k % steps) + steps) % steps);
    const int t = patternStep % tableSteps;
    currentStep.store (patternStep);
    if (sorted.n == 0 || stepOn[t]->load() < 0.5f) return;
    const auto seed = (uint32) value ("seed");
    if (hash01 (seed, k, 1) * 100.0 >= stepProb[t]->load()) return;

    const int N = sorted.n;
    const int mode = (int) value ("arpmode");
    Notes<128> notes;
    const int64 c = k;   // position from the musical time, so the same bar always plays the same notes
    auto at_ = [&] (const Pool& pl, int64 i) { const int j = (int) (((i % pl.n) + pl.n) % pl.n); notes.add (pl.p[(size_t) j], pl.vel[(size_t) j]); };
    switch (mode)
    {
        case 0: at_ (sorted, c); break;
        case 1: at_ (sorted, N - 1 - (int) (((c % N) + N) % N)); break;
        case 2:
        {
            if (N == 1) { at_ (sorted, 0); break; }
            const int period = 2 * N - 2;
            const int i = (int) (((c % period) + period) % period);
            at_ (sorted, i < N ? i : period - i);
            break;
        }
        case 3: at_ (sorted, (int64) (hash01 (seed, k, 3) * N)); break;
        case 4: at_ (played, c); break;
        default: for (int i = 0; i < N; ++i) notes.add (sorted.p[(size_t) i], sorted.vel[(size_t) i]); break;
    }
    // variation: sometimes an octave up or another note of the chord
    const double var = value ("variation") * 0.01;
    if (var > 0.0 && hash01 (seed, k, 2) < var && notes.n == 1)
    {
        if (hash01 (seed, k, 4) < 0.5 && notes.p[0] + 12 <= 127) notes.p[0] += 12;
        else { const int j = (int) (hash01 (seed, k, 5) * N); notes.p[0] = sorted.p[(size_t) jmin (N - 1, j)]; }
    }
    const int fixedVel = (int) value ("arpvel");
    const int accent = stepAcc[t]->load() > 0.5f ? (int) value ("accent") : 0;
    const int rat = jlimit (1, 4, (int) stepRat[t]->load());
    const double gate = value ("gate") * 0.01;
    const double len = jmax (1.0, stepSamples * gate / rat);
    const double hum = value ("humtime") * 0.001 * sr;
    const int humVel = (int) value ("humvel");
    for (int r = 0; r < rat; ++r)
    {
        const double when = at + r * stepSamples / rat + (hum > 0.0 ? hash01 (seed, k * 4 + r, 6) * hum : 0.0);
        for (int i = 0; i < notes.n; ++i)
        {
            int v = (fixedVel > 0 ? fixedVel : notes.vel[(size_t) i]) + accent;
            if (humVel > 0) v += roundToInt ((hash01 (seed, k * 64 + r * 16 + i, 9) * 2.0 - 1.0) * humVel);
            const int64 id = ++idCounter;
            scheduleOn (notes.p[(size_t) i], jlimit (1, 127, v), when, -2, id);
            scheduleOff (notes.p[(size_t) i], when + len, id);
        }
    }
}

// ===================================================================== block
void Processor::bypassStateChanged (bool bypassed, MidiBuffer& midi)
{
    if (bypassed)
    {
        flushAll (midi, 0);
        numHeld = 0; physicalCount = 0;
        internalRunning = false;
        std::fill (std::begin (rawOn), std::end (rawOn), false);
    }
}

void Processor::bypassedMidi (MidiBuffer& midi)
{
    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        if (m.isNoteOn()) rawOn[m.getNoteNumber()] = true;
        else if (m.isNoteOff()) rawOn[m.getNoteNumber()] = false;
    }
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    MidiBuffer out;
    // its own sound plays the chords it writes (the base renders it after this)
    { const PreviewSound::Patch patches[] { PreviewSound::Patch::keys, PreviewSound::Patch::pad, PreviewSound::Patch::pluck, PreviewSound::Patch::bass };
      ownVoice.setPatch (patches[jlimit (0, 3, (int) value ("ownpatch"))]); }

    // notes that went through untouched while bypassed get their note-off now (their own will be ours)
    for (int p = 0; p < 128; ++p) if (rawOn[p]) { out.addEvent (MidiMessage::noteOff (channel, p), 0); rawOn[p] = false; }
    if (panicRequest.exchange (false)) { flushAll (out, 0); numHeld = 0; physicalCount = 0; }

    // switching modes on the fly never leaves notes hanging
    const bool arp = value ("arp") > 0.5f, keep = value ("hold") > 0.5f || value ("latch") > 0.5f;
    if (arp != lastArp)
    {
        if (arp) { for (int i = 0; i < 128; ++i) if (mapping[i].n > 0) releaseDirect (out, i, 0); }
        else
        {
            for (int i = 0; i < numHeld; ++i)
            {
                const int note = held[i].note, vel = held[i].vel;
                Chord c = makeChord (note, &lastChord);
                lastChord = c; mapping[note] = c;
                for (int j = 0; j < c.n; ++j) if (chordRef[c.p[(size_t) j]]++ == 0) scheduleOn (c.p[(size_t) j], vel, 0, note);
            }
        }
        lastArp = arp;
    }
    if (! keep && lastKeep)
    {
        // hold / latch switched off: let go of the notes no key is holding
        for (int i = 0; i < numHeld; )
            if (! held[i].physical) { if (! arp) releaseDirect (out, held[i].note, 0); held[i] = held[--numHeld]; }
            else ++i;
    }
    lastKeep = keep;

    // clock
    const bool playing = isPlaying();
    const double tempo = bpm();
    const double spb = 60.0 / tempo * sr;   // samples per beat
    if (! playing && wasPlaying) { flushAll (out, 0); internalRunning = false; }
    double& ppq0 = blockPpq0;
    if (playing)
    {
        ppq0 = ppq();
        if (! wasPlaying || lastPpqEnd < 0.0 || std::abs (ppq0 - lastPpqEnd) > 1.0e-6) lastStep = std::numeric_limits<int64>::min();   // start, loop or jump
    }
    else ppq0 = internalPpq;
    wasPlaying = playing;
    static const double beats[] { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0,
                                  0.25, 0.375, 1.0 / 6.0, 0.125, 0.1875, 1.0 / 12.0, 0.0625, 1.0 / 24.0, 0.03125 };
    const double L = beats[jlimit (0, 18, (int) value ("arprate"))];
    const double swingOff = value ("swing") * 0.01 * L * 0.5;

    auto runSteps = [&] (int s0, int s1)
    {
        if (! arp || s1 <= s0) return;
        if (! playing && ! internalRunning) return;
        const double a = ppq0 + s0 / spb, b = ppq0 + s1 / spb;
        for (int64 k = (int64) std::floor ((a - L) / L); ; ++k)
        {
            const double t = (double) k * L + ((k & 1) != 0 ? swingOff : 0.0);
            if (t >= b) break;
            if (t < a - 1.0e-9 || k <= lastStep) continue;
            const double at = jmax ((double) s0, (t - ppq0) * spb);
            if (std::floor (at + 1.0e-6) >= s1) break;   // rounds onto the next block: it belongs there (or to the loop start)
            lastStep = k;
            arpStep (k, at, L * spb, n);
        }
    };

    int segStart = 0;
    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        const int pos = jlimit (0, jmax (0, n - 1), ev.samplePosition);
        runSteps (segStart, pos);
        segStart = jmax (segStart, pos);
        if (m.isNoteOn()) { channel = m.getChannel(); inputOn (out, m.getNoteNumber(), m.getVelocity(), pos); }
        else if (m.isNoteOff()) inputOff (out, m.getNoteNumber(), pos);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            flushAll (out, pos); numHeld = 0; physicalCount = 0; internalRunning = false;
            out.addEvent (m, pos);
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (const int lp = learnParam.load(); lp >= 0) { ccMap[cc].store (lp); learnParam.store (-1); }
            else if (const int idx = ccMap[cc].load(); idx >= 0 && idx < getParameters().size())
                getParameters()[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
            else out.addEvent (m, pos);
        }
        else out.addEvent (m, pos);
    }
    runSteps (segStart, n);
    if (! playing && internalRunning)
    {
        internalPpq = ppq0 + n / spb;
        if (numHeld == 0) internalRunning = false;
    }
    lastPpqEnd = ppq0 + n / spb;
    if (arp && numHeld == 0 && ! playing) currentStep = -1;

    // emit everything due in this block, in time order (offs before ons at the same instant)
    // times come from beat arithmetic: 5999.9999 means sample 6000
    auto slot = [] (double at) { return (int) std::floor (at + 1.0e-6); };
    for (;;)
    {
        int bestOn = -1, bestOff = -1;
        for (int k = 0; k < numOn; ++k) if (slot (pendingOn[k].at) < n && (bestOn < 0 || pendingOn[k].at < pendingOn[bestOn].at)) bestOn = k;
        for (int k = 0; k < numOff; ++k) if (slot (pendingOff[k].at) < n && (bestOff < 0 || pendingOff[k].at < pendingOff[bestOff].at)) bestOff = k;
        if (bestOn < 0 && bestOff < 0) break;
        if (bestOff >= 0 && (bestOn < 0 || slot (pendingOff[bestOff].at) <= slot (pendingOn[bestOn].at)))
        {
            const auto e = pendingOff[bestOff];
            pendingOff[bestOff] = pendingOff[--numOff];
            // a note-off of an arp note that was already retriggered is stale: the newer note keeps sounding
            if (e.id < 0 || soundingId[e.pitch] == e.id) emitOff (out, e.pitch, jlimit (0, n - 1, slot (e.at)));
        }
        else
        {
            const auto e = pendingOn[bestOn];
            pendingOn[bestOn] = pendingOn[--numOn];
            emitOn (out, e.pitch, e.vel, jlimit (0, n - 1, slot (e.at)), e.id);
        }
    }
    for (int k = 0; k < numOn; ++k) pendingOn[k].at -= n;
    for (int k = 0; k < numOff; ++k) pendingOff[k].at -= n;

    uint64 sm[2] {}, hm[2] {};
    for (int p = 0; p < 128; ++p) if (sounding[p]) sm[p >> 6] |= (uint64) 1 << (p & 63);
    for (int i = 0; i < numHeld; ++i) hm[held[i].note >> 6] |= (uint64) 1 << (held[i].note & 63);
    for (int i = 0; i < 2; ++i) { soundingMask[i].store (sm[i]); heldMask[i].store (hm[i]); }
    midi.swapWith (out);
}

// ===================================================================== state: chord memory and MIDI learn
void Processor::saveExtraState (ValueTree& v) const
{
    v.setProperty ("harmonyVersion", harmony::version, nullptr);
    StringArray mem;
    for (auto& m : memory) mem.add (String (m.load()));
    v.setProperty ("memory", mem.joinIntoString (","), nullptr);
    // CC mapping stored by parameter ID, so it survives new parameters in later versions
    StringArray ids;
    for (int i = 0; i < 128; ++i)
        if (const int idx = ccMap[i].load(); idx >= 0)
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (getParameters()[idx])) ids.add (String (i) + "=" + rp->paramID);
    v.setProperty ("midilearn", ids.joinIntoString (","), nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    auto mem = StringArray::fromTokens (v.getProperty ("memory").toString(), ",", {});
    if (mem.size() == 8) for (int i = 0; i < 8; ++i) memory[i].store (mem[i].getIntValue());
    for (auto& c : ccMap) c.store (-1);
    for (auto& item : StringArray::fromTokens (v.getProperty ("midilearn").toString(), ",", {}))
    {
        const int cc = item.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto id = item.fromFirstOccurrenceOf ("=", false, false);
        for (int i = 0; i < getParameters().size(); ++i)
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (getParameters()[i]); rp != nullptr && rp->paramID == id && cc >= 0 && cc < 128)
                ccMap[cc].store (i);
    }
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    std::vector<FactoryPreset> p {
        { "Inicial", "Acordes", {} },
        { "Séptimas abiertas"_u, "Acordes", { { "chordsize", 1 }, { "voicing", 1 } } },
        { "Novenas de house", "Acordes", { { "chordmode", 1 }, { "chordtype", 9 }, { "voicing", 2 }, { "rangelo", 52 }, { "rangehi", 82 } } },
        { "Stab menor 7", "Acordes", { { "chordmode", 1 }, { "chordtype", 4 }, { "rangelo", 55 }, { "rangehi", 79 } } },
        { "Strum de guitarra", "Acordes", { { "strum", 28 }, { "strumdir", 2 }, { "humvel", 10 } } },
        { "Arpegio 1/16", "Arpegios", { { "arp", 1 }, { "chordmode", 0 } } },
        { "Péndulo dos octavas"_u, "Arpegios", { { "arp", 1 }, { "arpmode", 2 }, { "octaves", 2 }, { "gate", 60 } } },
        { "Al azar con semilla", "Arpegios", { { "arp", 1 }, { "arpmode", 3 }, { "variation", 30 }, { "s3_prob", 60 }, { "s7_prob", 50 }, { "s11_prob", 60 }, { "s15_prob", 40 } } },
        { "Ratchets", "Arpegios", { { "arp", 1 }, { "s4_rat", 2 }, { "s8_rat", 3 }, { "s12_rat", 2 }, { "s16_rat", 4 }, { "gate", 50 } } },
        { "Acordes a corcheas con swing", "Arpegios", { { "arp", 1 }, { "arpmode", 5 }, { "arprate", 7 }, { "swing", 50 }, { "gate", 40 } } },
        { "Progresión I-V-vi-IV"_u, "Progresiones"_u, { { "chordmode", 4 }, { "chordsize", 0 } } },
        { "Progresión menor con séptimas"_u, "Progresiones"_u, { { "chordmode", 4 }, { "scale", 1 }, { "key", 9 }, { "chordsize", 1 },
                                                                 { "prog1", 0 }, { "prog2", 5 }, { "prog3", 2 }, { "prog4", 6 }, { "prog5", 3 }, { "prog6", 0 }, { "prog7", 4 }, { "prog8", 4 } } },
    };
    return p;
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 3.9, 57, 100, 1 },
             { 4.0, 3.9, 53, 100, 1 },
             { 8.0, 3.9, 48, 100, 1 },
             { 12.0, 3.9, 55, 100, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

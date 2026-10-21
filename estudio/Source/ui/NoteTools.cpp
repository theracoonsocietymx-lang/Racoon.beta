#include "NoteTools.h"
#include <algorithm>

namespace racoon::ui::notes
{
using namespace juce;

static const char* names[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
const char* noteName (int pc) { return names[((pc % 12) + 12) % 12]; }
String pitchName (int p) { return String (noteName (p)) + String (p / 12 - 2); }

Array<te::MidiNote*> orAll (te::MidiClip& c, const Array<te::MidiNote*>& n)
{
    if (! n.isEmpty()) return n;
    Array<te::MidiNote*> all;
    for (auto* x : c.getSequence().getNotes()) all.add (x);
    return all;
}

Range<double> playingRange (te::MidiClip& c)
{
    auto pos = c.getPosition();
    return { c.getContentBeatAtTime (pos.getStart()).inBeats(), c.getContentBeatAtTime (pos.getEnd()).inBeats() };
}

void transpose (te::MidiClip& c, const Array<te::MidiNote*>& sel, int st, UndoManager* um)
{
    for (auto* n : orAll (c, sel)) n->setNoteNumber (jlimit (0, 127, n->getNoteNumber() + st), um);
}

void reverse (te::MidiClip& c, const Array<te::MidiNote*>& sel, UndoManager* um)
{
    auto list = orAll (c, sel);
    if (list.isEmpty()) return;
    double s = 1.0e9, e = -1.0e9;
    for (auto* n : list) { s = jmin (s, n->getStartBeat().inBeats()); e = jmax (e, n->getEndBeat().inBeats()); }
    if (sel.isEmpty())
    {
        // the whole clip: mirror inside its playing part, so a hit on beat 1 lands at the end of the loop
        const auto r = playingRange (c);
        s = jmin (s, r.getStart()); e = jmax (e, r.getEnd());
    }
    for (auto* n : list)
    {
        const double len = n->getLengthBeats().inBeats();
        const double ns = s + e - (n->getStartBeat().inBeats() + len);
        n->setStartAndLength (te::BeatPosition::fromBeats (jmax (0.0, ns)), te::BeatDuration::fromBeats (len), um);
    }
}

void invert (te::MidiClip& c, const Array<te::MidiNote*>& sel, UndoManager* um)
{
    auto list = orAll (c, sel);
    if (list.isEmpty()) return;
    int lo = 127, hi = 0;
    for (auto* n : list) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
    for (auto* n : list) n->setNoteNumber (jlimit (0, 127, lo + hi - n->getNoteNumber()), um);
}

void legato (te::MidiClip& c, const Array<te::MidiNote*>& sel, UndoManager* um)
{
    auto list = orAll (c, sel);
    std::vector<te::MidiNote*> v (list.begin(), list.end());
    std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->getStartBeat() < b->getStartBeat(); });
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double start = v[i]->getStartBeat().inBeats();
        double next = -1.0;
        for (size_t j = i + 1; j < v.size(); ++j)
            if (v[j]->getStartBeat().inBeats() > start + 1.0e-6) { next = v[j]->getStartBeat().inBeats(); break; }
        if (next > start) v[i]->setStartAndLength (v[i]->getStartBeat(), te::BeatDuration::fromBeats (next - start), um);
    }
}

void toggleMute (te::MidiClip& c, const Array<te::MidiNote*>& sel, UndoManager* um)
{
    auto list = orAll (c, sel);
    bool anyOn = false;
    for (auto* n : list) anyOn = anyOn || ! n->isMute();
    for (auto* n : list) n->setMute (anyOn, um);
}

// ------------------------------------------------------------------ quantize
double swungLine (double t, double grid, double swing)
{
    if (grid <= 0.0) return t;
    const double k0 = std::floor (t / grid);
    double best = t, bestD = 1.0e18;
    for (double k = k0 - 1.0; k <= k0 + 2.0; k += 1.0)
    {
        if (k < 0.0) continue;
        const bool odd = std::fmod (k, 2.0) != 0.0;
        const double line = k * grid + (odd ? swing * grid / 3.0 : 0.0);
        const double d = std::abs (line - t);
        if (d < bestD) { bestD = d; best = line; }
    }
    return best;
}

void quantize (te::MidiClip& c, const Array<te::MidiNote*>& sel, const QuantizeOptions& o, UndoManager* um)
{
    if (o.grid <= 0.0) return;
    const double half = o.grid * 0.5;
    const double strength = jlimit (0.0, 1.0, o.strength), swing = jlimit (0.0, 1.0, o.swing);
    Random rng (o.seed);
    for (auto* n : orAll (c, sel))
    {
        const double start = n->getStartBeat().inBeats(), len = n->getLengthBeats().inBeats(), end = start + len;
        const double target = swungLine (start + o.offset, o.grid, swing) - o.offset;
        const double dist = std::abs (target - start);
        double ns = start;
        const bool inside = dist <= jlimit (0.0, 1.0, o.includeWithin) * half + 1.0e-9;
        const bool tooClose = o.excludeWithin > 0.0 && dist < jlimit (0.0, 1.0, o.excludeWithin) * half;
        if (inside && ! tooClose) ns = start + (target - start) * strength;
        if (o.random > 0.0) ns += (rng.nextDouble() * 2.0 - 1.0) * jlimit (0.0, 1.0, o.random) * half;
        ns = jmax (0.0, ns);
        double ne;
        if (o.ends)
        {
            const double te = swungLine (end + o.offset, o.grid, swing) - o.offset;
            ne = end + (te - end) * strength;
            if (ne <= ns + 1.0e-6) ne = ns + o.grid * jmax (0.25, strength);   // never a note of zero length
        }
        else ne = o.keepLength ? ns + len : jmax (ns + 1.0 / 64.0, end);
        n->setStartAndLength (te::BeatPosition::fromBeats (ns), te::BeatDuration::fromBeats (ne - ns), um);
    }
}

// ------------------------------------------------------------------ velocity
static int vel (double v) { return jlimit (1, 127, roundToInt (v)); }

void velocityRamp (te::MidiClip& c, const Array<te::MidiNote*>& sel, int from, int to, UndoManager* um)
{
    auto list = orAll (c, sel);
    if (list.isEmpty()) return;
    double s = 1.0e9, e = -1.0e9;
    for (auto* n : list) { s = jmin (s, n->getStartBeat().inBeats()); e = jmax (e, n->getStartBeat().inBeats()); }
    for (auto* n : list)
    {
        const double t = e > s ? (n->getStartBeat().inBeats() - s) / (e - s) : 1.0;
        n->setVelocity (vel (from + (to - from) * t), um);
    }
}

void velocityScale (te::MidiClip& c, const Array<te::MidiNote*>& sel, double f, UndoManager* um)
{
    for (auto* n : orAll (c, sel)) n->setVelocity (vel (n->getVelocity() * f), um);
}

void velocityLimit (te::MidiClip& c, const Array<te::MidiNote*>& sel, int lo, int hi, UndoManager* um)
{
    if (lo > hi) std::swap (lo, hi);
    for (auto* n : orAll (c, sel)) n->setVelocity (jlimit (jmax (1, lo), jmin (127, hi), n->getVelocity()), um);
}

void velocitySet (te::MidiClip& c, const Array<te::MidiNote*>& sel, int v, UndoManager* um)
{
    for (auto* n : orAll (c, sel)) n->setVelocity (vel (v), um);
}

void velocityHumanize (te::MidiClip& c, const Array<te::MidiNote*>& sel, int amount, int64 seed, UndoManager* um)
{
    Random rng (seed);
    for (auto* n : orAll (c, sel)) n->setVelocity (vel (n->getVelocity() + rng.nextInt ({ -amount, amount + 1 })), um);
}

void velocityCompress (te::MidiClip& c, const Array<te::MidiNote*>& sel, double amount, UndoManager* um)
{
    auto list = orAll (c, sel);
    if (list.isEmpty()) return;
    double avg = 0.0;
    for (auto* n : list) avg += n->getVelocity();
    avg /= list.size();
    const double a = jlimit (0.0, 1.0, amount);
    for (auto* n : list) n->setVelocity (vel (n->getVelocity() + (avg - n->getVelocity()) * a), um);
}

// ------------------------------------------------------------------ overlaps
int removeOverlaps (te::MidiClip& c, const Array<te::MidiNote*>& sel, UndoManager* um)
{
    auto list = orAll (c, sel);
    std::vector<te::MidiNote*> v (list.begin(), list.end());
    std::sort (v.begin(), v.end(), [] (auto* a, auto* b)
    {
        if (a->getNoteNumber() != b->getNoteNumber()) return a->getNoteNumber() < b->getNoteNumber();
        return a->getStartBeat() < b->getStartBeat();
    });
    int cut = 0;
    for (size_t i = 0; i + 1 < v.size(); ++i)
    {
        auto* a = v[i];
        // the next note of the same pitch that starts later
        for (size_t j = i + 1; j < v.size() && v[j]->getNoteNumber() == a->getNoteNumber(); ++j)
        {
            const double bs = v[j]->getStartBeat().inBeats(), as = a->getStartBeat().inBeats();
            if (bs <= as + 1.0e-6) continue;
            if (a->getEndBeat().inBeats() > bs + 1.0e-6)
            {
                a->setStartAndLength (a->getStartBeat(), te::BeatDuration::fromBeats (bs - as), um);
                ++cut;
            }
            break;
        }
    }
    return cut;
}
}


#include "MoleEngine.h"
#include "Utf8.h"

namespace racoon::mole
{
using namespace juce;

// ===================================================================== steps
uint64 Step::pack() const
{
    uint64 v = 0;
    v |= (uint64) (on ? 1 : 0);
    v |= (uint64) jlimit (1, 127, vel) << 1;
    v |= (uint64) jlimit (0, 100, prob) << 8;
    v |= (uint64) (jlimit (1, 16, ratchet) - 1) << 15;
    v |= (uint64) (jlimit (-50, 50, micro) + 50) << 19;
    v |= (uint64) jlimit (0, (int) numConds - 1, cond) << 26;
    v |= (uint64) jlimit (10, 400, len) << 30;
    v |= (uint64) 1 << 40;   // "written" marker, so an all-default step still unpacks with its defaults
    return v;
}

Step Step::unpack (uint64 v)
{
    Step s;
    if (((v >> 40) & 1) == 0) return s;
    s.on = (v & 1) != 0;
    s.vel = (int) ((v >> 1) & 127);
    s.prob = (int) ((v >> 8) & 127);
    s.ratchet = (int) ((v >> 15) & 15) + 1;
    s.micro = (int) ((v >> 19) & 127) - 50;
    s.cond = (int) ((v >> 26) & 15);
    s.len = (int) ((v >> 30) & 511);
    return s;
}

StringArray conditionNames()
{
    return { "Siempre", "1 de 2", "2 de 2", "1 de 4", "2 de 4", "3 de 4", "4 de 4", "Primera vuelta", "No en la primera vuelta",
             "Si sonó el carril de arriba"_u, "Si no sonó el carril de arriba"_u, "Solo en fills", "Nunca en fills" };
}

// ===================================================================== grooves
const StringArray& grooveNames()
{
    static StringArray a { "Ninguno", "Swing 54 %", "Swing 58 %", "Swing 62 %", "Tresillo (shuffle)", "Funk empujado", "House perezoso", "Hip hop suelto", "Aprendido" };
    return a;
}

Groove builtInGroove (int index)
{
    Groove gr;
    auto oddSixteenths = [&gr] (float amount) { for (int i = 1; i < 16; i += 2) gr.offset[(size_t) i] = amount; };
    switch (index)
    {
        case 1: oddSixteenths (0.08f); break;
        case 2: oddSixteenths (0.16f); break;
        case 3: oddSixteenths (0.24f); break;
        case 4: oddSixteenths (0.3333f); break;
        case 5:
            // the backbeat pushed a little early, ghost 16ths soft, downbeats strong
            oddSixteenths (0.05f);
            gr.offset[4] = gr.offset[12] = -0.06f;
            for (int i = 0; i < 16; ++i) gr.velocity[(size_t) i] = i % 4 == 0 ? 1.15f : (i % 2 == 1 ? 0.8f : 1.0f);
            break;
        case 6:
            oddSixteenths (0.1f);
            for (int i : { 2, 6, 10, 14 }) gr.offset[(size_t) i] = 0.12f;
            for (int i : { 2, 6, 10, 14 }) gr.velocity[(size_t) i] = 1.1f;
            break;
        case 7:
            oddSixteenths (0.3f);
            gr.offset[4] = gr.offset[12] = 0.08f;
            for (int i = 0; i < 16; ++i) gr.velocity[(size_t) i] = (i % 4 == 0) ? 1.1f : (i % 2 ? 0.75f : 0.95f);
            break;
        default: break;
    }
    return gr;
}

// ===================================================================== engine
Engine::Engine()
{
    for (auto& s : steps) for (auto& l : s) for (auto& st : l) st.store (Step().pack());
    static const int notes[] { 36, 38, 39, 42, 46, 51, 49, 37, 41, 45, 48, 70, 63, 60, 56, 75 };
    for (int i = 0; i < numLanes; ++i) lanes[(size_t) i].note = notes[i];
}

Step Engine::getStep (int lane, int step, int s) const
{
    const int sl = s < 0 ? slot.load() : s;
    return Step::unpack (steps[jlimit (0, numSlots - 1, sl)][jlimit (0, numLanes - 1, lane)][jlimit (0, maxSteps - 1, step)].load (std::memory_order_relaxed));
}

void Engine::setStep (int lane, int step, const Step& st, int s)
{
    const int sl = s < 0 ? slot.load() : s;
    steps[jlimit (0, numSlots - 1, sl)][jlimit (0, numLanes - 1, lane)][jlimit (0, maxSteps - 1, step)].store (st.pack(), std::memory_order_relaxed);
}

void Engine::clearLane (int lane, int s)
{
    for (int i = 0; i < maxSteps; ++i) setStep (lane, i, Step(), s);
}

double Engine::hash01 (uint32 seed, int64 k, int salt)
{
    uint64 z = (uint64) seed * 0x9E3779B97F4A7C15ull ^ (uint64) (k + 0x632BE59BD9B4E019ll) * 0xBF58476D1CE4E5B9ull ^ (uint64) (int64) salt * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double) (z >> 11) * (1.0 / 9007199254740992.0);
}

bool Engine::euclid (int n, int k, int rotation, int position)
{
    // Bjorklund distribution in its bucket form: a step sounds when the running sum of k/n overflows
    if (n <= 0 || k <= 0) return false;
    if (k >= n) return true;
    const int p = ((position - rotation) % n + n) % n;
    return ((p * k) % n) < k;
}

bool Engine::anySolo() const
{
    for (auto& l : lanes) if (l.solo && l.on) return true;
    return false;
}

Groove Engine::currentGroove() const
{
    if (g.groove == grooveNames().size() - 1) return learned;
    return builtInGroove (g.groove);
}

bool Engine::inFill (double ppq) const
{
    if (g.fillEvery <= 0 || inBreak (ppq)) return false;
    const double bpb = jmax (1.0, g.beatsPerBar);
    const int64 bar = (int64) std::floor (ppq / bpb + 1.0e-9);
    if (((bar + 1) % g.fillEvery + g.fillEvery) % g.fillEvery != 0) return false;
    return ppq - (double) bar * bpb >= bpb - g.fillBeats - 1.0e-9;
}

bool Engine::inBreak (double ppq) const
{
    if (g.breakEvery <= 0) return false;
    const double bpb = jmax (1.0, g.beatsPerBar);
    const int64 bar = (int64) std::floor (ppq / bpb + 1.0e-9);
    if (((bar + 1) % g.breakEvery + g.breakEvery) % g.breakEvery != 0) return false;
    return ppq >= (double) (bar + 1) * bpb - g.breakBeats - 1.0e-9;
}

static double syncopation (double ppq)
{
    // offbeat 16ths are where answering percussion sounds best
    const double inBeat = ppq - std::floor (ppq);
    if (inBeat < 1.0e-6) return 0.25;
    if (std::abs (inBeat - 0.5) < 1.0e-6) return 1.0;
    return 0.8;
}

bool Engine::baseActive (int lane, int64 k) const
{
    const auto& L = lanes[(size_t) lane];
    const int pos = posOf (lane, k);
    switch (L.mode)
    {
        case LaneMode::euclid: return euclid (L.steps, L.pulses, L.rotation, pos);
        case LaneMode::manual: return getStep (lane, pos).on;
        case LaneMode::complement:
        {
            const int ref = jlimit (0, numLanes - 1, L.refLane);
            const double ppq = (double) k * L.stepBeats;
            if (ref != lane && lanes[(size_t) ref].mode != LaneMode::complement && lanes[(size_t) ref].on)
            {
                const double rs = lanes[(size_t) ref].stepBeats;
                const int64 kr = (int64) std::floor (ppq / rs + 0.5);
                if (std::abs ((double) kr * rs - ppq) < 1.0e-6 && baseActive (ref, kr)) return false;
            }
            return hash01 (g.seed, k, 40 + lane) < L.density * syncopation (ppq);
        }
    }
    return false;
}

bool Engine::mutatedActive (int lane, int64 k, int pos, int64 cycle) const
{
    const bool preserved = (lane == 0 && g.preserveKick) || (lane == 1 && g.preserveSnare);
    if (g.mutation <= 0.0 || g.mutateEvery <= 0 || preserved) return baseActive (lane, k);
    const int64 gen = floorDiv (cycle, g.mutateEvery);
    if (gen == 0) return baseActive (lane, k);
    const auto& L = lanes[(size_t) lane];
    // rotate the whole pattern by a few steps, then add or drop single hits
    const int rot = (int) std::round ((hash01 (g.seed, gen * 131 + lane, 11) * 2.0 - 1.0) * g.mutation * L.steps * 0.25);
    bool a = baseActive (lane, k - rot);
    if (hash01 (g.seed, gen * maxSteps + pos, 12 + lane) < g.mutation * 0.25)
        a = a ? false : hash01 (g.seed, gen * maxSteps + pos, 13 + lane) < syncopation ((double) k * L.stepBeats);
    return a;
}

bool Engine::hitsAt (int lane, int64 k) const
{
    const auto& L = lanes[(size_t) lane];
    if (! L.on || L.mute || (anySolo() && ! L.solo)) return false;
    const double ppq = (double) k * L.stepBeats;
    const int pos = posOf (lane, k);
    const int64 cycle = cycleOf (lane, k);

    if (inBreak (ppq))
    {
        switch (g.breakType)
        {
            case BreakType::silence: return false;
            case BreakType::thinOut:
                if (lane != 0 && (ppq - std::floor (ppq) > 1.0e-6 || hash01 (g.seed, k, 50 + lane) > 0.6)) return false;
                break;
            case BreakType::roll: return lane == 1;
            case BreakType::accentShift: break;
        }
    }

    bool active = mutatedActive (lane, k, pos, cycle);
    const bool fill = inFill (ppq);
    if (! active && fill && L.fill)
    {
        const double bpb = jmax (1.0, g.beatsPerBar);
        const double progress = jlimit (0.0, 1.0, (ppq - (std::floor (ppq / bpb + 1.0e-9) + 1.0) * bpb + g.fillBeats) / jmax (0.01, g.fillBeats));
        active = hash01 (g.seed, k, 20 + lane) < g.fillDensity * (0.25 + 0.75 * progress) * syncopation (ppq) * 1.25;
    }
    if (! active) return false;

    const auto s = getStep (lane, pos);
    auto laneHitsAt = [this] (int l, double at)
    {
        if (l < 0) return false;
        const double sb = lanes[(size_t) l].stepBeats;
        const int64 kk = (int64) std::floor (at / sb + 0.5);
        return std::abs ((double) kk * sb - at) < 1.0e-6 && hitsAt (l, kk);
    };
    switch (s.cond)
    {
        case c1of2: if (((cycle % 2) + 2) % 2 != 0) return false; break;
        case c2of2: if (((cycle % 2) + 2) % 2 != 1) return false; break;
        case c1of4: case c2of4: case c3of4: case c4of4:
            if (((cycle % 4) + 4) % 4 != s.cond - c1of4) return false;
            break;
        case firstTime:    if (ppq >= g.startPpq + L.steps * L.stepBeats - 1.0e-9) return false; break;
        case notFirstTime: if (ppq < g.startPpq + L.steps * L.stepBeats - 1.0e-9) return false; break;
        case ifPrev:       if (! laneHitsAt (lane - 1, ppq)) return false; break;
        case ifNotPrev:    if (laneHitsAt (lane - 1, ppq)) return false; break;
        case onlyFill:     if (! fill) return false; break;
        case notFill:      if (fill) return false; break;
        default: break;
    }
    double p = (L.mode == LaneMode::manual || L.mode == LaneMode::euclid ? s.prob / 100.0 : 1.0) * L.prob * g.probScale;
    // probability mutation: some steps of a mutated generation become less certain
    const bool preserved = (lane == 0 && g.preserveKick) || (lane == 1 && g.preserveSnare);
    if (! preserved && g.mutation > 0.0 && g.mutateEvery > 0)
        if (const int64 gen = floorDiv (cycle, g.mutateEvery); gen > 0)
            p *= 1.0 - g.mutation * 0.5 * hash01 (g.seed, gen * maxSteps + pos, 14 + lane);
    if (p < 1.0 && hash01 (g.seed, k, 30 + lane) >= p) return false;
    return true;
}

double Engine::offsetBeats (int lane, int64 k, double nominal, int stepMicro, int sub) const
{
    const auto& L = lanes[(size_t) lane];
    const double beatsPerMs = g.bpm / 60000.0;
    // swing: every second position of the swing grid moves later
    double swingOff = 0.0;
    {
        const double grid = g.swingGridBeats;
        const int64 idx = (int64) std::floor (nominal / grid + 0.5);
        if (std::abs ((double) idx * grid - nominal) < 1.0e-6 && (idx & 1) != 0)
            swingOff = (g.swing - 0.5) * 2.0 * grid * L.swing;
    }
    double free = 0.0;
    // groove template (per 16th of the bar)
    if (g.groove > 0 && g.grooveAmount > 0.0 && g.grooveMode != GrooveMode::accents)
    {
        const int64 i16 = (int64) std::floor (nominal / 0.25 + 0.5);
        if (std::abs ((double) i16 * 0.25 - nominal) < 1.0e-6)
        {
            const int bar16 = jmax (1, (int) std::round (g.beatsPerBar * 4.0));
            const int at = (int) (((i16 % bar16) + bar16) % bar16) % 16;
            free += g.grooveAmount * currentGroove().offset[(size_t) at] * 0.25;
        }
    }
    free += stepMicro / 100.0 * L.stepBeats;
    free += (L.microMs + g.pushPullMs) * beatsPerMs;
    // humanize: reproducible, from the position
    if (g.humanAmount > 0.0 && g.profile != Humanize::mechanical)
    {
        static const double sigma[] { 2.0, 6.0, 10.0, 0.0 };
        const double ms = sigma[(int) g.profile] * g.humanAmount * (g.humanTimeMs / 6.0);
        double h = (hash01 (g.seed, k * 17 + sub, 60 + lane) * 2.0 - 1.0) * ms;
        if (g.profile == Humanize::looseFunk)
        {
            const double inBar = nominal - std::floor (nominal / g.beatsPerBar) * g.beatsPerBar;
            if (std::abs (inBar - 1.0) < 1.0e-6 || std::abs (inBar - 3.0) < 1.0e-6) h += 8.0 * g.humanAmount;   // lazy backbeat
        }
        free += h * beatsPerMs;
    }
    free *= (1.0 - g.quantize);
    const double range = g.timingRangeMs * beatsPerMs;
    return swingOff + jlimit (-range, range, free);
}

void Engine::emitStep (int lane, int64 k, double from, double to, const std::function<void (const Event&)>& cb) const
{
    if (! hitsAt (lane, k)) return;
    const auto& L = lanes[(size_t) lane];
    const int pos = posOf (lane, k);
    const auto s = getStep (lane, pos);
    const double nominal = (double) k * L.stepBeats;
    const bool fill = inFill (nominal), brk = inBreak (nominal);
    const double bpb = jmax (1.0, g.beatsPerBar);
    const double barEnd = (std::floor (nominal / bpb + 1.0e-9) + 1.0) * bpb;

    double vel = L.velocity * s.vel / 100.0;
    // accent every n-th hit of the pattern
    if (L.accentEvery > 0 && L.mode == LaneMode::euclid)
    {
        int idx = 0;
        for (int i = 0; i < pos; ++i) if (euclid (L.steps, L.pulses, L.rotation, i)) ++idx;
        if (idx % L.accentEvery == 0) vel *= 1.0 + L.accent;
        else vel *= 1.0 - L.accent * 0.5;
    }
    if (fill) vel *= 0.7 + 0.3 * jlimit (0.0, 1.0, (nominal - barEnd + g.fillBeats) / jmax (0.01, g.fillBeats));
    if (brk && g.breakType == BreakType::accentShift) vel *= (nominal - std::floor (nominal) < 1.0e-6) ? 0.6 : 1.3;
    if (g.humanAmount > 0.0 && g.profile != Humanize::mechanical)
    {
        static const double spread[] { 0.05, 0.12, 0.18, 0.0 };
        vel *= 1.0 + (hash01 (g.seed, k, 70 + lane) * 2.0 - 1.0) * spread[(int) g.profile] * g.humanAmount * (g.humanVel / 0.12);
    }
    if (g.groove > 0 && g.grooveAmount > 0.0 && g.grooveMode != GrooveMode::timing)
    {
        const int64 i16 = (int64) std::floor (nominal / 0.25 + 0.5);
        const int bar16 = jmax (1, (int) std::round (bpb * 4.0));
        const int at = (int) (((i16 % bar16) + bar16) % bar16) % 16;
        vel *= 1.0 + (currentGroove().velocity[(size_t) at] - 1.0) * g.grooveAmount;
    }
    vel *= g.velScale;

    // ratchets: the step's own, the lane's random ones, fills and rolls
    int r = s.ratchet;
    if (L.ratchet > 1 && L.ratchetProb > 0.0 && hash01 (g.seed, k, 80 + lane) < L.ratchetProb) r = jmax (r, L.ratchet);
    if (fill && g.fillComplexity > 0.0)
    {
        const double progress = jlimit (0.0, 1.0, (nominal - barEnd + g.fillBeats) / jmax (0.01, g.fillBeats));
        r = jmax (r, 1 + (int) std::floor (g.fillComplexity * 3.0 * progress * hash01 (g.seed, k, 90 + lane) + 0.5));
    }
    if (brk && g.breakType == BreakType::roll && lane == 1)
    {
        const double progress = jlimit (0.0, 1.0, (nominal - barEnd + g.breakBeats) / jmax (0.01, g.breakBeats));
        r = jmax (r, 1 + (int) std::floor (progress * 3.0));
        vel *= 0.55 + 0.45 * progress;
    }
    r = jlimit (1, 16, r);

    const double gateLen = L.gate * s.len / 100.0 * L.stepBeats;
    for (int j = 0; j < r; ++j)
    {
        double t = (double) j / r;
        if (L.ratchetAccel > 0.0) t = 1.0 - std::pow (1.0 - t, 1.0 + 2.0 * L.ratchetAccel);
        else if (L.ratchetAccel < 0.0) t = std::pow (t, 1.0 - 2.0 * L.ratchetAccel);
        const double sub = nominal + t * L.stepBeats;
        const double fin = sub + offsetBeats (lane, k, j == 0 ? nominal : sub, s.micro, j);
        if (fin < from || fin >= to) continue;
        double v = vel;
        if (r > 1) v *= 1.0 + L.ratchetCurve * ((double) j / (r - 1) - 0.5);
        Event e;
        e.ppq = fin;
        e.lane = lane;
        int note = L.note;
        // note swap: a mutated generation sometimes plays another lane's sound
        const bool preserved = (lane == 0 && g.preserveKick) || (lane == 1 && g.preserveSnare);
        if (! preserved && g.mutation > 0.0 && g.mutateEvery > 0)
            if (const int64 gen = floorDiv (cycleOf (lane, k), g.mutateEvery); gen > 0 && hash01 (g.seed, gen * maxSteps + pos, 15 + lane) < g.mutation * 0.15)
                note = lanes[(size_t) ((lane + 1 + (int) (hash01 (g.seed, gen, 16 + lane) * (numLanes - 1))) % numLanes)].note;
        e.note = jlimit (0, 127, note + g.transpose);
        e.channel = jlimit (1, 16, L.channel);
        e.vel = jlimit (1, 127, (int) std::round (v));
        e.lengthBeats = r > 1 ? jmin (gateLen, L.stepBeats / r * 0.9) : gateLen;
        e.lengthBeats = jmax (1.0 / 128.0, e.lengthBeats);
        cb (e);
    }
}

void Engine::collect (double from, double to, const std::function<void (const Event&)>& cb) const
{
    if (to <= from) return;
    const double rangeBeats = g.timingRangeMs * g.bpm / 60000.0;
    for (int lane = 0; lane < numLanes; ++lane)
    {
        const auto& L = lanes[(size_t) lane];
        if (! L.on || L.stepBeats <= 0.0) continue;
        const double sb = L.stepBeats;
        // a step can sound before its place (negative offsets) or up to one step later (ratchets, swing)
        const double margin = rangeBeats + sb * 1.5 + 0.5;
        const int64 k0 = (int64) std::floor ((from - margin) / sb), k1 = (int64) std::floor ((to + margin) / sb);
        for (int64 k = k0; k <= k1; ++k) emitStep (lane, k, from, to, cb);
    }
}
}

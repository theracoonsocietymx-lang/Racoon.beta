#include "Pattern.h"

namespace be
{
using namespace juce;

// ============================================================ step helpers
void Step::setLock (int p, float v)
{
    const uint16_t q = (uint16_t) jlimit (0, 65535, roundToInt (clamp01 (v) * 65535.0f));
    for (int i = 0; i < numLocks; ++i) if (locks[i].param == p) { locks[i].value = q; return; }
    if (numLocks < kMaxLocks) { locks[numLocks].param = (uint8_t) p; locks[numLocks].value = q; ++numLocks; }
}

void Step::clearLock (int p)
{
    for (int i = 0; i < numLocks; ++i)
        if (locks[i].param == p)
        {
            for (int j = i; j < numLocks - 1; ++j) locks[j] = locks[j + 1];
            --numLocks;
            locks[numLocks] = Lock();
            return;
        }
}

bool Step::isDefault() const
{
    return ! on && ! accent && vel == 100 && cond == 0 && prob == 100 && micro == 0 && retrig == 1 && retrigMode == 0 && len == 4 && pitch == 0 && numLocks == 0;
}

bool Pattern::isEmpty() const
{
    for (auto& t : tracks) for (int s = 0; s < t.length; ++s) if (t.steps[s].on) return false;
    return true;
}

// ============================================================ serialisation
// track:  "len,res|idx:on,acc,vel,cond,a,b,prob,micro,rt,rtm,len,pitch[,p=v...];idx:..."
String SeqData::patternToString (const Pattern& p)
{
    String out;
    out << "M" << p.masterLength << "\n";
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = p.tracks[t];
        out << tp.length << "," << tp.res << "|";
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& st = tp.steps[s];
            if (st.isDefault()) continue;
            out << s << ":" << (int) st.on << "," << (int) st.accent << "," << (int) st.vel << "," << (int) st.cond << "," << (int) st.condA << ","
                << (int) st.condB << "," << (int) st.prob << "," << (int) st.micro << "," << (int) st.retrig << "," << (int) st.retrigMode << ","
                << (int) st.len << "," << (int) st.pitch;
            for (int l = 0; l < st.numLocks; ++l) out << "," << trackParam (st.locks[l].param).id << "=" << (int) st.locks[l].value;
            out << ";";
        }
        out << "\n";
    }
    return out;
}

void SeqData::patternFromString (Pattern& p, const String& s)
{
    p = Pattern();
    auto lines = StringArray::fromLines (s);
    int t = 0;
    for (auto& line : lines)
    {
        if (line.startsWithChar ('M')) { p.masterLength = jlimit (1, kMaxSteps, line.substring (1).getIntValue()); continue; }
        if (line.trim().isEmpty() || t >= kNumTracks) continue;
        auto& tp = p.tracks[t++];
        const auto head = line.upToFirstOccurrenceOf ("|", false, false);
        tp.length = jlimit (1, kMaxSteps, head.upToFirstOccurrenceOf (",", false, false).getIntValue());
        tp.res = jlimit (0, 7, head.fromFirstOccurrenceOf (",", false, false).getIntValue());
        auto steps = StringArray::fromTokens (line.fromFirstOccurrenceOf ("|", false, false), ";", "");
        for (auto& tok : steps)
        {
            if (! tok.containsChar (':')) continue;
            const int idx = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
            if (idx < 0 || idx >= kMaxSteps) continue;
            auto f = StringArray::fromTokens (tok.fromFirstOccurrenceOf (":", false, false), ",", "");
            if (f.size() < 12) continue;
            auto& st = tp.steps[idx];
            st.on = f[0].getIntValue() != 0; st.accent = f[1].getIntValue() != 0;
            st.vel = (uint8_t) jlimit (1, 127, f[2].getIntValue()); st.cond = (uint8_t) jlimit (0, condNames().size() - 1, f[3].getIntValue());
            st.condA = (uint8_t) jlimit (1, 8, f[4].getIntValue()); st.condB = (uint8_t) jlimit (1, 8, f[5].getIntValue());
            st.prob = (uint8_t) jlimit (0, 100, f[6].getIntValue()); st.micro = (int8_t) jlimit (-50, 50, f[7].getIntValue());
            st.retrig = (uint8_t) jlimit (1, 8, f[8].getIntValue()); st.retrigMode = (uint8_t) jlimit (0, 3, f[9].getIntValue());
            st.len = (uint8_t) jlimit (1, 64, f[10].getIntValue()); st.pitch = (int8_t) jlimit (-24, 24, f[11].getIntValue());
            for (int k = 12; k < f.size(); ++k)
            {
                const auto pid = f[k].upToFirstOccurrenceOf ("=", false, false);
                for (int pi = 0; pi < T_COUNT; ++pi)
                    if (pid == trackParam (pi).id) { st.setLock (pi, (float) f[k].fromFirstOccurrenceOf ("=", false, false).getIntValue() / 65535.0f); break; }
            }
        }
    }
}

ValueTree SeqData::toValueTree() const
{
    ValueTree v ("SEQ");
    for (int p = 0; p < kNumPatterns; ++p)
    {
        ValueTree pt ("PAT");
        pt.setProperty ("i", p, nullptr);
        pt.setProperty ("name", pats[p].name, nullptr);
        pt.setProperty ("data", patternToString (pats[p]), nullptr);
        v.appendChild (pt, nullptr);
    }
    String songStr;
    for (int i = 0; i < songLength; ++i) songStr << song[i].pattern << "x" << song[i].repeats << " ";
    v.setProperty ("song", songStr.trim(), nullptr);
    ValueTree g ("GROOVE");
    g.setProperty ("active", groove.active, nullptr);
    g.setProperty ("name", groove.name, nullptr);
    String tm, vl;
    for (int i = 0; i < 16; ++i) { tm << String (groove.timing[i], 4) << " "; vl << String (groove.velocity[i], 4) << " "; }
    g.setProperty ("timing", tm.trim(), nullptr);
    g.setProperty ("velocity", vl.trim(), nullptr);
    v.appendChild (g, nullptr);
    return v;
}

void SeqData::fromValueTree (const ValueTree& v)
{
    for (auto& p : pats) p = Pattern();
    for (auto c : v)
    {
        if (c.hasType ("PAT"))
        {
            const int i = (int) c.getProperty ("i", -1);
            if (i < 0 || i >= kNumPatterns) continue;
            patternFromString (pats[i], c.getProperty ("data").toString());
            pats[i].name = c.getProperty ("name").toString();
        }
        else if (c.hasType ("GROOVE"))
        {
            groove.active = (bool) c.getProperty ("active", false);
            groove.name = c.getProperty ("name").toString();
            auto tm = StringArray::fromTokens (c.getProperty ("timing").toString(), " ", "");
            auto vl = StringArray::fromTokens (c.getProperty ("velocity").toString(), " ", "");
            for (int i = 0; i < 16; ++i)
            {
                groove.timing[i] = i < tm.size() ? jlimit (-0.5f, 0.5f, tm[i].getFloatValue()) : 0.0f;
                groove.velocity[i] = i < vl.size() ? jlimit (-1.0f, 1.0f, vl[i].getFloatValue()) : 0.0f;
            }
        }
    }
    songLength = 0;
    for (auto& tok : StringArray::fromTokens (v.getProperty ("song").toString(), " ", ""))
    {
        if (songLength >= kMaxSong || ! tok.containsChar ('x')) continue;
        song[songLength].pattern = jlimit (0, kNumPatterns - 1, tok.upToFirstOccurrenceOf ("x", false, false).getIntValue());
        song[songLength].repeats = jlimit (1, 64, tok.fromFirstOccurrenceOf ("x", false, false).getIntValue());
        ++songLength;
    }
}

// ============================================================ tools
namespace PatternTools
{
void euclid (TrackPattern& t, int hits, int steps, int rotate, int velocity)
{
    steps = jlimit (1, kMaxSteps, steps);
    hits = jlimit (0, steps, hits);
    t.length = steps;
    for (int s = 0; s < kMaxSteps; ++s) { t.steps[s].on = false; t.steps[s].accent = false; }
    if (hits == 0) return;
    // Bresenham form of the Bjorklund distribution
    for (int s = 0; s < steps; ++s)
    {
        const int idx = ((s + rotate) % steps + steps) % steps;
        const bool on = ((s * hits) % steps) < hits;
        if (on) { t.steps[idx].on = true; t.steps[idx].vel = (uint8_t) velocity; }
    }
}

namespace
{
// probability of a hit per 1/16 position for typical electronic roles (16 positions)
const float* roleProfile (int role)
{
    static const float kick[16]  { 1, .05f, .1f, .15f, .9f, .05f, .1f, .2f, 1, .05f, .15f, .2f, .9f, .1f, .2f, .3f };
    static const float snare[16] { .02f, .05f, .05f, .1f, .95f, .05f, .1f, .15f, .02f, .1f, .05f, .2f, .95f, .1f, .2f, .3f };
    static const float hat[16]   { .3f, .5f, .9f, .5f, .3f, .5f, .9f, .5f, .3f, .5f, .9f, .5f, .3f, .5f, .9f, .6f };
    static const float open[16]  { 0, 0, .8f, 0, 0, 0, .8f, 0, 0, 0, .8f, 0, 0, 0, .8f, .1f };
    static const float perc[16]  { .1f, .3f, .2f, .4f, .1f, .4f, .3f, .3f, .1f, .3f, .2f, .5f, .1f, .4f, .3f, .4f };
    static const float cym[16]   { .6f, 0, 0, 0, 0, 0, 0, 0, .1f, 0, 0, 0, 0, 0, 0, 0 };
    switch ((Role) role)
    {
        case Role::Kick: return kick;
        case Role::Snare: case Role::Clap: return snare;
        case Role::ClosedHat: case Role::Shaker: return hat;
        case Role::OpenHat: case Role::Ride: return open;
        case Role::Crash: return cym;
        default: return perc;
    }
}
}

void randomize (TrackPattern& t, int role, float density, float amount, Random& rng)
{
    const float* prof = roleProfile (role);
    for (int s = 0; s < t.length; ++s)
    {
        if (rng.nextFloat() > amount) continue;
        const float p = clamp01 (prof[s % 16] * (0.4f + density * 1.2f));
        auto& st = t.steps[s];
        st.on = rng.nextFloat() < p;
        st.vel = (uint8_t) jlimit (20, 127, (int) (70 + prof[s % 16] * 50 + rng.nextInt (20) - 10));
        st.accent = st.on && prof[s % 16] > 0.85f && rng.nextFloat() < 0.4f;
        st.retrig = 1;
        if (st.on && (role == (int) Role::ClosedHat || role == (int) Role::Shaker) && rng.nextFloat() < 0.08f * amount) st.retrig = (uint8_t) (2 + rng.nextInt (2));
        st.prob = st.on && prof[s % 16] < 0.4f && rng.nextFloat() < 0.3f ? (uint8_t) (50 + rng.nextInt (40)) : 100;
    }
}

void humanize (TrackPattern& t, float amount, Random& rng)
{
    for (int s = 0; s < t.length; ++s)
    {
        auto& st = t.steps[s];
        if (! st.on) continue;
        st.micro = (int8_t) jlimit (-50, 50, (int) st.micro + roundToInt ((rng.nextFloat() * 2.0f - 1.0f) * 12.0f * amount));
        st.vel = (uint8_t) jlimit (1, 127, (int) st.vel + roundToInt ((rng.nextFloat() * 2.0f - 1.0f) * 22.0f * amount));
    }
}

void shift (TrackPattern& t, int by)
{
    const int n = t.length;
    if (n <= 1) return;
    std::vector<Step> tmp (t.steps, t.steps + n);
    for (int s = 0; s < n; ++s) t.steps[((s + by) % n + n) % n] = tmp[(size_t) s];
}

void reverse (TrackPattern& t) { std::reverse (t.steps, t.steps + t.length); }
void clear (TrackPattern& t) { for (auto& s : t.steps) s = Step(); }

void variation (const Pattern& src, Pattern& dst, const int* roles, float intensity, int flavour, Random& rng)
{
    dst = src;
    const float I = clamp01 (intensity);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tp = dst.tracks[t];
        const int role = roles[t];
        bool any = false;
        for (int s = 0; s < tp.length; ++s) any |= tp.steps[s].on;
        if (! any && flavour != 2) continue;
        const bool hat = role == (int) Role::ClosedHat || role == (int) Role::Shaker || role == (int) Role::Ride;
        const bool kick = role == (int) Role::Kick;
        const bool snare = role == (int) Role::Snare || role == (int) Role::Clap;
        for (int s = 0; s < tp.length; ++s)
        {
            auto& st = tp.steps[s];
            const int pos = s % 16;
            switch (flavour)
            {
                case 0: // subtle: velocity shading, rare drops / adds on weak positions
                    if (st.on) st.vel = (uint8_t) jlimit (1, 127, st.vel + rng.nextInt (Range<int> (-12, 13)) * (int) (I * 1.5f + 0.5f));
                    if (! kick && (pos & 1) && rng.nextFloat() < 0.12f * I) st.on = ! st.on;
                    break;
                case 1: // ghost groove: soft notes around the backbeat, swingy hats
                    if (snare && ! st.on && (pos == 3 || pos == 7 || pos == 10 || pos == 14 || pos == 15) && rng.nextFloat() < 0.5f * I)
                    { st.on = true; st.vel = (uint8_t) (25 + rng.nextInt (20)); st.prob = 85; }
                    if (hat && st.on && (pos & 1)) { st.vel = (uint8_t) jlimit (1, 127, (int) st.vel - (int) (30 * I)); st.micro = (int8_t) jlimit (-50, 50, (int) (12 * I)); }
                    break;
                case 2: // busy: more hits, ratchets on hats, syncopated kicks
                    if (hat && ! st.on && rng.nextFloat() < 0.55f * I) { st.on = true; st.vel = (uint8_t) (55 + rng.nextInt (30)); }
                    if (hat && st.on && rng.nextFloat() < 0.15f * I) { st.retrig = (uint8_t) (2 + rng.nextInt (3)); st.retrigMode = (uint8_t) (rng.nextBool() ? 0 : 3); }
                    if (kick && ! st.on && (pos == 7 || pos == 10 || pos == 14) && rng.nextFloat() < 0.35f * I) { st.on = true; st.vel = 90; }
                    if (! kick && ! hat && ! snare && ! st.on && rng.nextFloat() < 0.25f * I) { st.on = true; st.vel = (uint8_t) (50 + rng.nextInt (50)); }
                    break;
                case 3: // sparse: remove weak hits, keep the skeleton
                    if (st.on && ! (kick && pos % 4 == 0) && ! (snare && (pos == 4 || pos == 12)) && rng.nextFloat() < 0.6f * I) st.on = false;
                    break;
                case 4: // shuffle: micro timing and accents shift
                    if (st.on && (pos & 1)) st.micro = (int8_t) jlimit (-50, 50, (int) (8 + 25 * I));
                    if (st.on && rng.nextFloat() < 0.2f * I) st.accent = ! st.accent;
                    break;
                default: // conditional: introduce trig conditions so the pattern evolves over loops
                    if (st.on && ! (kick && pos % 4 == 0) && rng.nextFloat() < 0.35f * I)
                    {
                        st.cond = 11;                     // A:B
                        st.condB = (uint8_t) (rng.nextBool() ? 2 : 4);
                        st.condA = (uint8_t) (1 + rng.nextInt (st.condB));
                    }
                    else if (! st.on && hat && rng.nextFloat() < 0.25f * I) { st.on = true; st.prob = 40; st.vel = 60; }
                    break;
            }
        }
    }
    dst.name = src.name.isEmpty() ? String ("Variation") : src.name + "*";
}

void makeFill (Pattern& p, const int* roles, float intensity, Random& rng)
{
    const float I = clamp01 (intensity);
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tp = p.tracks[t];
        const int role = roles[t];
        const int startFill = std::max (0, tp.length - (tp.length >= 16 ? 4 : 2));
        for (int s = startFill; s < tp.length; ++s)
        {
            auto& st = tp.steps[s];
            if (role == (int) Role::Snare || role == (int) Role::Clap)
            {
                st.on = true; st.cond = 1;  // Fill
                st.vel = (uint8_t) jlimit (30, 127, 60 + (s - startFill) * 18);
                st.retrig = (uint8_t) (s == tp.length - 1 ? 4 : (rng.nextFloat() < I ? 2 : 1));
                st.retrigMode = 2;
            }
            else if (role >= (int) Role::TomLow && role <= (int) Role::TomHigh && rng.nextFloat() < 0.6f * I)
            {
                st.on = true; st.cond = 1; st.vel = (uint8_t) (80 + rng.nextInt (40));
            }
            else if (role == (int) Role::Kick && s >= tp.length - 2 && st.on)
            {
                st.cond = 2;   // Not Fill: drop the kick at the very end of a fill
            }
        }
    }
}

MidiFile toMidiFile (const Pattern& p, const int* notes, double bpm, int bars, float swing)
{
    MidiFile mf;
    const int ppq = 960;
    mf.setTicksPerQuarterNote (ppq);
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::tempoMetaEvent ((int) (60000000.0 / std::max (bpm, 1.0))), 0);
    seq.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const double totalBeats = (double) bars * 4.0;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = p.tracks[t];
        const double sb = seqResBeats (tp.res);
        const int count = (int) std::floor (totalBeats / sb + 1.0e-6);
        for (int k = 0; k < count; ++k)
        {
            const auto& st = tp.steps[k % tp.length];
            if (! st.on || st.prob == 0) continue;
            if (st.cond == 1) continue;   // fill-only hits are not part of the plain loop
            double beat = k * sb + (double) st.micro / 100.0 * sb;
            const double sixteenth = beat / 0.25;
            if (swing > 0.0f && std::abs (sixteenth - std::round (sixteenth)) < 1.0e-6 && ((int) std::round (sixteenth) & 1)) beat += swing * 0.1667;
            const int hits = std::max (1, (int) st.retrig);
            for (int h = 0; h < hits; ++h)
            {
                double hb = beat + (st.retrigMode == (uint8_t) RetrigMode::Flam ? (h == 0 ? 0.0 : 0.06) : sb * h / hits);
                int vel = st.vel;
                if (st.accent) vel = std::min (127, vel + 20);
                if (st.retrigMode == (uint8_t) RetrigMode::RollUp) vel = jmax (1, vel * (h + 1) / hits);
                if (st.retrigMode == (uint8_t) RetrigMode::RollDown) vel = jmax (1, vel * (hits - h) / hits);
                if (st.retrigMode == (uint8_t) RetrigMode::Flam && h == 0 && hits > 1) vel = jmax (1, vel / 2);
                const double len = std::max (0.02, std::min (sb / hits, sb * st.len / 4.0) * 0.95);
                const auto on = (double) roundToInt (hb * ppq), off = (double) roundToInt ((hb + len) * ppq);
                seq.addEvent (MidiMessage::noteOn (10, notes[t], (uint8) jlimit (1, 127, vel)), on);
                seq.addEvent (MidiMessage::noteOff (10, notes[t]), off);
            }
        }
    }
    seq.updateMatchedPairs();
    seq.sort();
    mf.addTrack (seq);
    return mf;
}
} // namespace PatternTools
} // namespace be

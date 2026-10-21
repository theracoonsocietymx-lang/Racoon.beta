#include "Arranger.h"
#include "PluginCatalog.h"
#include "AudioTools.h"

using namespace juce;

namespace racoon::arranger
{
const Identifier fxTrackProp { "racoonSongFx" }, sweepProp { "racoonSongSweep" };

namespace
{
constexpr double bpb = 4.0;     // electronic music: always four beats

struct Range8 { Part part; int lo, hi; };
// the course's ranges (module 04), in bars; drops and the breakdown can grow past them in long songs
const Range8 ranges[] = {
    { Part::intro, 8, 32 }, { Part::build, 16, 32 }, { Part::drop, 16, 32 }, { Part::breakdown, 16, 32 },
    { Part::build2, 16, 32 }, { Part::drop2, 16, 32 }, { Part::outro, 8, 32 } };
// the long-song extension: the breakdown and both drops up to 64
int longCap (Part p) { return p == Part::breakdown || p == Part::drop || p == Part::drop2 ? 64 : 32; }
// the order in which sections get another phrase when there is time for it
const Part growOrder[] = { Part::drop, Part::drop2, Part::intro, Part::outro, Part::breakdown, Part::build, Part::build2 };
}

int minBars() { int t = 0; for (auto& r : ranges) t += r.lo; return t; }
int maxBars() { int t = 0; for (auto& r : ranges) t += longCap (r.part); return t; }

int barsForMinutes (double minutes, double bpm)
{
    const double bars = minutes * bpm / bpb;
    const int whole = (int) std::round (bars / phraseBars) * phraseBars;
    return jlimit (minBars(), maxBars(), whole);
}

double minutesForBars (int bars, double bpm) { return bars * bpb / jmax (1.0, bpm); }

String clock (double seconds)
{
    const int s = (int) std::round (seconds);
    return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
}

String partName (Part p)
{
    switch (p)
    {
        case Part::intro:     return "Intro";
        case Part::build:     return "Build-up";
        case Part::drop:      return "Drop";
        case Part::breakdown: return "Breakdown";
        case Part::build2:    return "Build-up 2";
        case Part::drop2:     return "Drop 2";
        case Part::outro:     return "Outro";
    }
    return {};
}

std::vector<Section> plan (int totalBars)
{
    totalBars = jlimit (minBars(), maxBars(), (totalBars / phraseBars) * phraseBars);
    std::map<Part, int> len;
    int sum = 0;
    for (auto& r : ranges) { len[r.part] = r.lo; sum += r.lo; }
    // first every section up to the course's top, round after round; then the long extension
    for (int stage = 0; stage < 2 && sum < totalBars; ++stage)
    {
        bool grew = true;
        while (sum < totalBars && grew)
        {
            grew = false;
            for (auto p : growOrder)
            {
                if (sum >= totalBars) break;
                int cap = 32;
                for (auto& r : ranges) if (r.part == p) cap = stage == 0 ? r.hi : longCap (p);
                if (len[p] + phraseBars <= cap) { len[p] += phraseBars; sum += phraseBars; grew = true; }
            }
        }
    }
    std::vector<Section> out;
    int bar = 0;
    for (auto& r : ranges)
    {
        out.push_back ({ r.part, partName (r.part), bar, len[r.part] });
        bar += len[r.part];
    }
    return out;
}

String roleName (Role r)
{
    switch (r)
    {
        case Role::kick:    return "bombo";
        case Role::snare:   return "caja";
        case Role::clap:    return "palmas";
        case Role::hats:    return "hats";
        case Role::openHat: return "hat abierto";
        case Role::ride:    return "ride";
        case Role::rim:     return "aro";
        case Role::shaker:  return "shaker";
        case Role::conga:   return "congas y bongos";
        case Role::toms:    return "toms";
        case Role::bell:    return "cencerro";
        case Role::perc:    return String::fromUTF8 ("percusión");
        case Role::incidental: return "golpes sueltos";
        case Role::crash:   return "platillo";
        case Role::bass:    return "bajo";
        case Role::pad:     return "pad";
        case Role::chords:  return "acordes";
        case Role::lead:    return String::fromUTF8 ("melodía");
        case Role::choir:   return "coro";
        case Role::texture: return "textura";
        case Role::voice:   return "voz";
        case Role::other:   return "loop";
    }
    return {};
}

bool activeIn (Role r, Part part, int p, int n)
{
    const bool last = p == n - 1, odd = p % 2 == 1;
    switch (part)
    {
        case Part::intro:
            // DJ intro: the kick alone, then the hats, then the shaker and the rim; claps and the open hat
            // in the last phrase, and the pad to announce the harmony. No bass, no melody, no hand drums
            switch (r)
            {
                case Role::kick: case Role::texture: return true;
                case Role::hats:    return p >= 1 || n == 1;
                case Role::shaker:  return n >= 2 && p >= n / 2;
                case Role::rim:     return n >= 3 && p >= n - 2;
                case Role::perc:    return n >= 3 ? p >= 2 : (n == 2 && p == 1);
                case Role::snare: case Role::clap: case Role::openHat: return n >= 3 && last;
                case Role::pad:     return n >= 2 && last;
                default: return false;
            }
        case Part::build:
            // the bass comes in, then the harmony; the hand drums join halfway; the melody waits for the drop
            switch (r)
            {
                case Role::kick: case Role::hats: case Role::snare: case Role::clap: case Role::shaker: case Role::rim:
                case Role::bass: case Role::pad: case Role::texture: return true;
                case Role::openHat: case Role::perc: return n == 1 || p >= 1;
                case Role::conga: case Role::toms: return p >= n / 2;
                case Role::chords: case Role::other: return n == 1 || p >= 1;
                default: return false;
            }
        case Part::drop:
            // everything, but the hand drums and the bell answer every other phrase and the ride is for the second half
            switch (r)
            {
                case Role::choir: case Role::ride: return p >= n / 2;
                case Role::conga: case Role::toms: case Role::bell: return odd || n == 1;
                default: return true;
            }
        case Part::breakdown:
            // no kick, no bass: pads, chords, the melody and the voices; the shaker keeps the pulse in the second
            // half and the hats come back in the last phrase
            switch (r)
            {
                case Role::pad: case Role::chords: case Role::choir: case Role::voice: case Role::texture: return true;
                case Role::lead:   return n == 1 || p >= 1;
                case Role::shaker: return n >= 2 && p >= n / 2;
                case Role::hats:   return n >= 2 && last;
                default: return false;
            }
        case Part::build2:
            switch (r)
            {
                case Role::kick: case Role::hats: case Role::snare: case Role::clap: case Role::shaker: case Role::rim:
                case Role::bass: case Role::chords: case Role::pad: case Role::texture: case Role::other: return true;
                case Role::openHat: case Role::perc: case Role::conga: case Role::toms: return n == 1 || p >= 1;
                case Role::lead: return last;
                default: return false;
            }
        case Part::drop2:
            // the second drop goes further than the first: the ride and the hand drums all the way
            return true;
        case Part::outro:
            // the intro backwards: the music leaves first, kick and hats stay for the next DJ
            switch (r)
            {
                case Role::kick: case Role::hats: case Role::texture: return true;
                case Role::shaker: case Role::perc: return n == 1 || ! last;
                case Role::rim:    return n >= 2 && ! last;
                case Role::snare: case Role::clap: return n >= 2 && ! last;
                case Role::openHat: return p < n / 2;
                case Role::conga: case Role::toms: case Role::bell: case Role::ride: return n >= 3 && p == 0;
                case Role::bass:   return p < n / 2;
                case Role::pad:    return n >= 2 && p == 0;
                case Role::chords: return n >= 3 && p == 0;
                default: return false;
            }
    }
    return false;
}

namespace
{
struct N { double beat, len; int pitch, vel; };

Role drumRole (int pitch)
{
    switch (pitch)
    {
        case 35: case 36: return Role::kick;
        case 38: case 40: return Role::snare;
        case 39: return Role::clap;
        case 42: case 44: return Role::hats;
        case 46: return Role::openHat;
        case 51: case 53: case 59: return Role::ride;
        case 37: return Role::rim;
        case 49: case 52: case 55: case 57: return Role::crash;
        case 54: case 69: case 70: case 82: return Role::shaker;          // tambourine, cabasa, maracas, shaker
        case 60: case 61: case 62: case 63: case 64: case 65: case 66: return Role::conga;  // bongos, congas, timbales
        case 41: case 43: case 45: case 47: case 48: case 50: return Role::toms;
        case 56: case 67: case 68: case 76: case 77: return Role::bell;  // cowbell, agogo, woodblocks
        default: return Role::perc;
    }
}

bool nameHas (const String& lower, std::initializer_list<const char*> words)
{
    for (auto* w : words) if (lower.contains (String::fromUTF8 (w))) return true;
    return false;
}

// "drums": the channel holds a whole kit (General MIDI notes), split into kick, snare, hats and percussion
enum class Kind { drums, single };
struct TrackKindGuess { Kind kind = Kind::single; Role role = Role::other; bool sure = false; };

TrackKindGuess guessFromName (te::AudioTrack& t)
{
    String animal;
    for (auto* p : t.pluginList)
        if (Catalog::isRacoonPlugin (*p)) { animal = Catalog::animalFor (p->getName()); if (animal.isNotEmpty()) break; }
    const auto name = t.getName().toLowerCase();
    auto has = [&] (const char* a) { return animal == a || name.contains (String (a).toLowerCase()); };
    if (has ("Hamster")) return { Kind::drums, Role::perc, true };
    if (has ("Mole"))      return { Kind::drums, Role::perc, true };
    if (has ("Badger"))    return { Kind::single, Role::bass, true };
    if (has ("Dormouse"))  return { Kind::single, Role::pad, true };
    if (has ("Chipmunk") || has ("Ferret")) return { Kind::single, Role::chords, true };
    if (has ("Squirrel"))  return { Kind::single, Role::lead, true };
    if (has ("Porcupine")) return { Kind::single, Role::choir, true };
    if (has ("Skunk"))     return { Kind::single, Role::texture, true };
    if (has ("Chinchilla")) return { Kind::single, Role::voice, true };
    if (nameHas (name, { "kick", "bombo" }))                 return { Kind::single, Role::kick, true };
    if (nameHas (name, { "snare", "caja" }))                 return { Kind::single, Role::snare, true };
    if (nameHas (name, { "clap", "palma" }))                 return { Kind::single, Role::clap, true };
    if (nameHas (name, { "open hat", "abierto" }))           return { Kind::single, Role::openHat, true };
    if (nameHas (name, { "ride" }))                          return { Kind::single, Role::ride, true };
    if (nameHas (name, { "hat", "charles" }))                return { Kind::single, Role::hats, true };
    if (nameHas (name, { "shaker", "maraca", "pandero", "tambourine" })) return { Kind::single, Role::shaker, true };
    if (nameHas (name, { "conga", "bongo", "djembe", "timbal" }))         return { Kind::single, Role::conga, true };
    if (nameHas (name, { "tom" }))                           return { Kind::single, Role::toms, true };
    if (nameHas (name, { "cowbell", "cencerro", "bell" }))   return { Kind::single, Role::bell, true };
    if (nameHas (name, { "perc" }))                          return { Kind::single, Role::perc, true };
    if (nameHas (name, { "drum", "bater", "kit", "beat", "groove" }))  return { Kind::drums, Role::perc, true };
    if (nameHas (name, { "bass", "bajo", "sub", "808" }))    return { Kind::single, Role::bass, true };
    if (nameHas (name, { "pad", "atmós", "atmos" }))         return { Kind::single, Role::pad, true };
    if (nameHas (name, { "chord", "acorde", "keys", "piano", "stab", "guitar", "guitarra" })) return { Kind::single, Role::chords, true };
    if (nameHas (name, { "lead", "melod", "hook", "arp", "synth", "sinte" })) return { Kind::single, Role::lead, true };
    if (nameHas (name, { "coro", "choir" }))                 return { Kind::single, Role::choir, true };
    if (nameHas (name, { "vox", "voz", "vocal", "voice" }))  return { Kind::single, Role::voice, true };
    if (nameHas (name, { "fx", "textura", "texture", "ambient", "riser", "ruido" })) return { Kind::single, Role::texture, true };
    return {};
}

// a channel with notes and no name to go by: low notes are the bass, piled notes the harmony, the rest a melody
Role guessFromNotes (const std::vector<N>& notes)
{
    if (notes.empty()) return Role::other;
    double sum = 0.0, lenSum = 0.0;
    std::map<int, int> perOnset;
    for (auto& n : notes) { sum += n.pitch; lenSum += n.len; ++perOnset[(int) std::round (n.beat * 48.0)]; }
    const double avg = sum / (double) notes.size(), len = lenSum / (double) notes.size();
    const double poly = (double) notes.size() / (double) perOnset.size();
    if (avg < 48.0) return Role::bass;
    if (poly >= 2.5) return len >= 2.0 ? Role::pad : Role::chords;
    return Role::lead;
}

struct Plan
{
    te::AudioTrack* track = nullptr;
    bool isAudio = false;
    Kind kind = Kind::single;
    Role role = Role::other;                                    // single channels and audio
    std::map<Role, std::vector<std::vector<N>>> loop;          // per role: the notes of each bar of the loop
    std::map<Role, std::vector<int>> source;                   // per role: which loop bar plays at each loop bar
    Session::Clipboard audio;
    int maxLead = 0;
    std::vector<int> fillBars;                                  // loop bars that are the loop's own fill (a drum kit)
    std::vector<N> fill;                                        // the fill's snare, clap and toms (one bar)
    std::vector<std::vector<N>> incidentals;                    // one-off hits, one bar each, used at phrase ends
    std::vector<N> out;                                         // the song, beats from the song's start
    std::vector<bool> on;                                       // per bar of the song
};

// the notes of the selection on one channel, beats from the selection's start
std::vector<N> notesIn (te::AudioTrack& t, double s, double e)
{
    std::vector<N> out;
    for (auto* c : t.getClips())
        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
        {
            const double content = mc->getContentStartBeat().inBeats();
            const double cs = mc->getStartBeat().inBeats(), ce = mc->getEndBeat().inBeats();
            if (ce <= s || cs >= e) continue;
            if (mc->isMuted()) continue;
            auto& seq = mc->isLooping() ? mc->getSequenceLooped() : mc->getSequence();
            for (auto* n : seq.getNotes())
            {
                const double b = n->getStartBeat().inBeats() + content;
                if (b < s - 1.0e-6 || b >= e - 1.0e-6 || b < cs - 1.0e-6 || b >= ce - 1.0e-6) continue;
                if (n->isMute()) continue;
                out.push_back ({ b - s, n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
            }
        }
    return out;
}

bool hasAudioIn (te::AudioTrack& t, double s, double e, te::TempoSequence& ts)
{
    for (auto* c : t.getClips())
        if (dynamic_cast<te::WaveAudioClip*> (c) != nullptr)
        {
            const double cs = ts.toBeats (c->getPosition().getStart()).inBeats(), ce = ts.toBeats (c->getPosition().getEnd()).inBeats();
            if (ce > s && cs < e) return true;
        }
    return false;
}

// the harmony's period inside the loop: the smallest number of bars after which the bass and the chords repeat
int harmonicPeriod (const std::vector<Plan>& plans, int L)
{
    std::vector<std::vector<String>> sigs;
    for (auto& p : plans)
        for (auto& [role, bars] : p.loop)
        {
            if (! (role == Role::bass || role == Role::pad || role == Role::chords)) continue;
            std::vector<String> sig;
            for (auto& bar : bars)
            {
                if (bar.empty()) { sig.push_back ({}); continue; }
                std::set<int> pcs;
                if (role == Role::bass)
                {
                    auto lowest = std::min_element (bar.begin(), bar.end(), [] (const N& a, const N& b) { return a.beat < b.beat; });
                    pcs.insert (lowest->pitch % 12);
                }
                else for (auto& n : bar) pcs.insert (n.pitch % 12);
                String s;
                for (int x : pcs) s << x << ",";
                sig.push_back (s);
            }
            sigs.push_back (sig);
        }
    if (sigs.empty()) return 1;
    for (int p = 1; p < L; p *= 2)
    {
        if (L % p != 0) continue;
        bool ok = true;
        for (auto& sig : sigs)
            for (int i = 0; i < L && ok; ++i)
                for (int j = i + p; j < L && ok; j += p)
                    if (sig[(size_t) i].isNotEmpty() && sig[(size_t) j].isNotEmpty() && sig[(size_t) i] != sig[(size_t) j]) ok = false;
        if (ok) return p;
    }
    return L;
}

bool isDrum (Role r)
{
    switch (r)
    {
        case Role::kick: case Role::snare: case Role::clap: case Role::hats: case Role::openHat: case Role::ride:
        case Role::rim: case Role::shaker: case Role::conga: case Role::toms: case Role::bell: case Role::perc:
        case Role::incidental: case Role::crash: return true;
        default: return false;
    }
}

// a role that sounds in most of the loop but rests in a few bars (the loop's own little arrangement: no kick in
// its third phrase, the bass waiting two bars) plays a full bar there, one that fits the same chord
void fillGaps (Plan& p, int L, int H)
{
    for (auto& [role, bars] : p.loop)
    {
        auto& src = p.source[role];
        src.assign ((size_t) L, -1);
        int full = 0;
        // the loop's fill bar does not repeat in the middle of a phrase: the song puts its fills at phrase ends
        const bool fillRole = role == Role::snare || role == Role::clap || role == Role::toms;
        auto isFill = [&] (int i) { return fillRole && std::find (p.fillBars.begin(), p.fillBars.end(), i) != p.fillBars.end(); };
        for (int i = 0; i < L; ++i) if (! bars[(size_t) i].empty() && ! isFill (i)) { src[(size_t) i] = i; ++full; }
        if (! p.fillBars.empty() && fillRole && full > 0 && full < L)
        {
            // the fill bar takes the plain bar at the same place of the pattern
            for (int i : p.fillBars)
                for (int j = 0; j < L; ++j)
                    if (src[(size_t) j] >= 0 && j % jmin (L, 4) == i % jmin (L, 4)) { src[(size_t) i] = j; break; }
            continue;
        }
        if (full == 0 || full == L || L < 2 || full * 2 < L) continue;
        const bool rhythm = isDrum (role);
        for (int i = 0; i < L; ++i)
        {
            if (src[(size_t) i] >= 0) continue;
            int best = -1;
            size_t most = 0;
            auto consider = [&] (int mod)
            {
                for (int j = 0; j < L; ++j)
                    if (! bars[(size_t) j].empty() && j % mod == i % mod && bars[(size_t) j].size() > most) { best = j; most = bars[(size_t) j].size(); }
            };
            consider (H);
            if (best < 0 && rhythm) consider (jmin (L, 4));
            if (best < 0 && rhythm) consider (1);
            src[(size_t) i] = best;
        }
    }
}

// ------------------------------------------------------------------- transitions: made here, at the song's tempo
void writeWav (const File& f, const AudioBuffer<float>& b, double sr)
{
    f.getParentDirectory().createDirectory();
    audio::writeWav (f, b, sr, 24);
}

struct Svf
{
    double ic1 = 0, ic2 = 0;
    // returns { low, band, high }
    std::array<double, 3> run (double x, double f, double q, double sr)
    {
        const double g = std::tan (MathConstants<double>::pi * jlimit (10.0, sr * 0.45, f) / sr), k = 1.0 / q;
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
};

void normalise (AudioBuffer<float>& b, float peakDb)
{
    const float m = b.getMagnitude (0, b.getNumSamples());
    if (m > 0.0f) b.applyGain (Decibels::decibelsToGain (peakDb) / m);
}

// white noise through a band-pass that climbs, a sine that climbs with it, louder and faster towards the end
File riser (const File& dir, double seconds)
{
    const double sr = 44100.0;
    auto f = dir.getChildFile ("Riser " + String (seconds, 2) + " s.wav");
    if (f.existsAsFile()) return f;
    const int n = (int) (seconds * sr);
    AudioBuffer<float> b (2, n);
    Random rnd (8191);
    Svf fl[2];
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / n, curve = t * t;
        const double fc = 350.0 * std::pow (9000.0 / 350.0, curve);
        const double amp = 0.04 + 0.96 * std::pow (t, 2.2);
        const double sineF = 220.0 * std::pow (8.0, curve);
        ph += sineF / sr;
        const double sine = std::sin (MathConstants<double>::twoPi * ph) * 0.12;
        for (int c = 0; c < 2; ++c)
        {
            const double x = rnd.nextFloat() * 2.0f - 1.0f;
            const auto o = fl[c].run (x, fc, 2.2, sr);
            b.setSample (c, i, (float) ((o[1] * 1.6 + sine) * amp));
        }
    }
    b.applyGainRamp (n - 64, 64, 1.0f, 0.0f);
    normalise (b, -6.0f);
    writeWav (f, b, sr);
    return f;
}

// the hit at the start of a drop: a low boom that falls and a burst of noise that closes
File impact (const File& dir)
{
    const double sr = 44100.0;
    auto f = dir.getChildFile ("Impacto.wav");
    if (f.existsAsFile()) return f;
    const int n = (int) (3.0 * sr);
    AudioBuffer<float> b (2, n);
    Random rnd (4099);
    Svf fl[2];
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        ph += (38.0 + 30.0 * std::exp (-t / 0.12)) / sr;
        const double boom = std::sin (MathConstants<double>::twoPi * ph) * std::exp (-t / 0.55) * jmin (1.0, t * 400.0);
        const double fc = 300.0 + 7000.0 * std::exp (-t / 0.35);
        for (int c = 0; c < 2; ++c)
        {
            const auto o = fl[c].run (rnd.nextFloat() * 2.0f - 1.0f, fc, 0.8, sr);
            b.setSample (c, i, (float) (boom * 0.9 + o[0] * 0.5 * std::exp (-t / 0.8)));
        }
    }
    b.applyGainRamp (n - 2000, 2000, 1.0f, 0.0f);
    normalise (b, -4.0f);
    writeWav (f, b, sr);
    return f;
}

// the way down into a breakdown: noise that falls and fades, no lows
File downlifter (const File& dir, double seconds)
{
    const double sr = 44100.0;
    auto f = dir.getChildFile ("Bajada " + String (seconds, 2) + " s.wav");
    if (f.existsAsFile()) return f;
    const int n = (int) (seconds * sr);
    AudioBuffer<float> b (2, n);
    Random rnd (2731);
    Svf fl[2], hp[2];
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / n;
        const double fc = 7000.0 * std::pow (300.0 / 7000.0, std::sqrt (t));
        const double amp = (1.0 - t) * (1.0 - t) * jmin (1.0, i / 200.0);
        for (int c = 0; c < 2; ++c)
        {
            const auto o = fl[c].run (rnd.nextFloat() * 2.0f - 1.0f, fc, 1.8, sr);
            const auto h = hp[c].run (o[1], 180.0, 0.7, sr);
            b.setSample (c, i, (float) (h[2] * amp));
        }
    }
    normalise (b, -8.0f);
    writeWav (f, b, sr);
    return f;
}

// a cymbal played backwards: bright noise that swells for one bar and stops dead on the one
File reverseCymbal (const File& dir, double seconds)
{
    const double sr = 44100.0;
    auto f = dir.getChildFile ("Platillo al reves " + String (seconds, 2) + " s.wav");
    if (f.existsAsFile()) return f;
    const int n = (int) (seconds * sr);
    AudioBuffer<float> b (2, n);
    Random rnd (6007);
    Svf hp[2], bp[2];
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / n;
        const double amp = std::pow (t, 3.0);
        for (int c = 0; c < 2; ++c)
        {
            const double x = rnd.nextFloat() * 2.0f - 1.0f;
            const auto h = hp[c].run (x, 4500.0, 0.7, sr);
            const auto m = bp[c].run (x, 7800.0, 3.0, sr);
            b.setSample (c, i, (float) ((h[2] + m[1] * 0.8) * amp));
        }
    }
    b.applyGainRamp (n - 32, 32, 1.0f, 0.0f);
    normalise (b, -9.0f);
    writeWav (f, b, sr);
    return f;
}

te::AutomatableParameter* paramOf (te::Plugin& p, const String& id)
{
    for (auto* prm : p.getAutomatableParameters()) if (prm->paramID == id) return prm;
    return nullptr;
}

RangedAudioParameter* juceParamOf (te::Plugin& p, const String& id)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&p))
        if (auto* pi = ext->getAudioPluginInstance())
            for (auto* x : pi->getParameters())
                if (auto* r = dynamic_cast<RangedAudioParameter*> (x); r != nullptr && r->getParameterID() == id) return r;
    return nullptr;
}

void setPlain (te::Plugin& p, const String& id, float v)
{
    if (auto* r = juceParamOf (p, id)) r->setValueNotifyingHost (r->convertTo0to1 (v));
}
}

// ===================================================================================================== describe
namespace
{
void analyse (Session& s, std::vector<Plan>& plans, const Array<te::AudioTrack*>& tracks, double start, double end, int L)
{
    auto& ts = s.getEdit().tempoSequence;
    for (auto* t : tracks)
    {
        if (t == nullptr || kindOf (*t) == TrackKind::returnBus || (bool) t->state.getProperty (fxTrackProp)) continue;
        Plan p;
        p.track = t;
        const auto guess = guessFromName (*t);
        auto notes = notesIn (*t, start, end);
        if (notes.empty())
        {
            if (! hasAudioIn (*t, start, end, ts)) continue;
            p.isAudio = true;
            p.role = guess.sure ? (guess.kind == Kind::drums ? Role::perc : guess.role) : Role::other;
            plans.push_back (std::move (p));
            continue;
        }
        // a kit: by its notes, even with no name (most notes in the drum area and a kick or a snare)
        int drumish = 0;
        for (auto& n : notes) if (n.pitch == 35 || n.pitch == 36 || n.pitch == 38 || n.pitch == 39 || n.pitch == 42) ++drumish;
        p.kind = guess.sure ? guess.kind : (drumish * 3 >= (int) notes.size() ? Kind::drums : Kind::single);
        p.role = guess.sure ? guess.role : guessFromNotes (notes);
        // one-off hits: a sound of the kit (not the backbone) that appears in less than a quarter of the bars.
        // They are not looped: the song plays them at the end of phrases, where an incidental belongs
        std::set<int> rare;
        if (p.kind == Kind::drums && L >= 4)
        {
            std::map<int, std::set<int>> barsOf;
            for (auto& n : notes) barsOf[n.pitch].insert (jlimit (0, L - 1, (int) std::floor (n.beat / bpb + 1.0e-9)));
            for (auto& [pitch, bs] : barsOf)
            {
                const Role r = drumRole (pitch);
                const bool backbone = r == Role::kick || r == Role::snare || r == Role::clap || r == Role::hats || r == Role::crash;
                if (! backbone && (int) bs.size() * 4 < L) rare.insert (pitch);
            }
        }
        for (auto& n : notes)
        {
            const Role r = p.kind == Kind::drums ? (rare.count (n.pitch) ? Role::incidental : drumRole (n.pitch)) : p.role;
            auto& bars = p.loop[r];
            if (bars.empty()) bars.resize ((size_t) L);
            const int bar = jlimit (0, L - 1, (int) std::floor (n.beat / bpb + 1.0e-9));
            bars[(size_t) bar].push_back ({ n.beat - bar * bpb, n.len, n.pitch, n.vel });
            if (r == Role::lead) p.maxLead = jmax (p.maxLead, n.pitch);
        }
        if (p.kind == Kind::drums)
        {
            // the incidentals, bar by bar
            if (p.loop.count (Role::incidental))
                for (auto& bar : p.loop[Role::incidental]) if (! bar.empty()) p.incidentals.push_back (bar);
            // the loop's fill: a bar with clearly more snare, clap and tom hits than the others
            std::vector<int> busy ((size_t) L, 0);
            for (auto r : { Role::snare, Role::clap, Role::toms })
                if (p.loop.count (r))
                    for (int i = 0; i < L; ++i) busy[(size_t) i] += (int) p.loop[r][(size_t) i].size();
            auto sorted = busy;
            std::sort (sorted.begin(), sorted.end());
            const int median = sorted[(size_t) L / 2];
            int top = -1, most = 0;
            for (int i = 0; i < L; ++i)
                if (busy[(size_t) i] >= jmax (median + 3, (median * 8) / 5) && busy[(size_t) i] > most) { top = i; most = busy[(size_t) i]; }
            if (top >= 0 && L >= 4)
            {
                for (int i = 0; i < L; ++i)
                    if (busy[(size_t) i] >= jmax (median + 3, (median * 8) / 5)) p.fillBars.push_back (i);
                if ((int) p.fillBars.size() * 4 > L) p.fillBars.clear();
                else
                    for (auto r : { Role::snare, Role::clap, Role::toms })
                        if (p.loop.count (r)) for (auto& n : p.loop[r][(size_t) top]) p.fill.push_back (n);
            }
        }
        plans.push_back (std::move (p));
    }
}

String describePlan (const Plan& p)
{
    StringArray roles;
    if (p.isAudio) roles.add (roleName (p.role) + " (audio)");
    else for (auto& [r, bars] : p.loop) if (r != Role::crash) roles.add (roleName (r));
    if (! p.fill.empty()) roles.add ("su fill");
    return p.track->getName() + ": " + roles.joinIntoString (", ");
}
}

StringArray describe (Session& s, const Array<te::AudioTrack*>& tracks, double a, double b)
{
    StringArray out;
    a = std::floor (a / bpb + 1.0e-6) * bpb;
    b = jmax (a + bpb, std::ceil (b / bpb - 1.0e-6) * bpb);
    std::vector<Plan> plans;
    analyse (s, plans, tracks, a, b, jmax (1, (int) std::round ((b - a) / bpb)));
    for (auto& p : plans) out.add (describePlan (p));
    return out;
}

// ======================================================================================================== build
Result build (Session& s, const Array<te::AudioTrack*>& selected, double startBeat, double endBeat, const Options& o)
{
    Result res;
    auto& edit = s.getEdit();
    auto& ts = edit.tempoSequence;
    startBeat = std::floor (startBeat / bpb + 1.0e-6) * bpb;
    endBeat = jmax (startBeat + bpb, std::ceil (endBeat / bpb - 1.0e-6) * bpb);
    const int L = jmax (1, (int) std::round ((endBeat - startBeat) / bpb));
    res.loopBars = L;
    res.startBeat = startBeat;

    std::vector<Plan> plans;
    analyse (s, plans, selected, startBeat, endBeat, L);
    if (plans.empty())
    {
        res.message = String::fromUTF8 ("La selección no tiene notas ni audio. Selecciona unos compases con clips (arrastra sobre la maqueta) y vuelve a intentar.");
        return res;
    }
    const int H = harmonicPeriod (plans, L);
    res.harmonicBars = H;
    for (auto& p : plans) if (! p.isAudio) fillGaps (p, L, H);
    for (auto& p : plans) res.found.add (describePlan (p));

    // audio: a copy of the selection, taken before anything changes (the user's clipboard is put back after)
    const auto userClipboard = s.getClipboard();
    for (auto& p : plans)
        if (p.isAudio)
        {
            s.copyRange ({ p.track }, startBeat, endBeat);
            p.audio = s.getClipboard();
        }
    s.setClipboard (userClipboard);

    const auto sections = plan (o.totalBars);
    res.sections = sections;
    const int total = sections.back().startBar + sections.back().bars;
    const double songEnd = startBeat + total * bpb;
    auto* um = &edit.getUndoManager();
    um->beginNewTransaction();

    // ---- the notes of the song, channel by channel
    // the kit that plays the roll and the crashes: the one with a snare (or a kick)
    Plan* kit = nullptr;
    for (auto& p : plans) if (p.kind == Kind::drums && (p.loop.count (Role::snare) || p.loop.count (Role::clap))) { kit = &p; break; }
    if (kit == nullptr) for (auto& p : plans) if (p.kind == Kind::drums && p.loop.count (Role::kick)) { kit = &p; break; }
    int rollPitch = 38;
    if (kit != nullptr)
    {
        std::map<int, int> count;
        for (auto r : { Role::snare, Role::clap })
            if (kit->loop.count (r)) for (auto& bar : kit->loop[r]) for (auto& n : bar) count[n.pitch] += r == Role::snare ? 2 : 1;
        int most = 0;
        for (auto& [pitch, c] : count) if (c > most) { most = c; rollPitch = pitch; }
    }
    // where the drums answer the structure: the last bar of each phrase gets a fill (a big one every 16 bars and at
    // the end of a drop), except under the build-up's roll
    auto rollBarsOf = [] (const Section& sec) { return jmin (8, sec.bars); };
    auto fillAt = [&] (const Section& sec, int b) -> int    // 0 none, 1 small, 2 big
    {
        const bool energetic = sec.part == Part::drop || sec.part == Part::drop2 || sec.part == Part::build || sec.part == Part::build2;
        if (! energetic || (b + 1) % phraseBars != 0) return 0;
        if ((sec.part == Part::build || sec.part == Part::build2) && b >= sec.bars - rollBarsOf (sec)) return 0;
        return (b + 1) % 16 == 0 || b == sec.bars - 1 ? 2 : 1;
    };

    for (auto& p : plans)
    {
        p.on.assign ((size_t) total, false);
        if (p.isAudio) continue;
        size_t nextIncidental = 0;
        for (auto& sec : sections)
        {
            const int phrases = sec.bars / phraseBars;
            const bool build = sec.part == Part::build || sec.part == Part::build2;
            const bool drop = sec.part == Part::drop || sec.part == Part::drop2;
            for (int b = 0; b < sec.bars; ++b)
            {
                const int song = sec.startBar + b, phrase = b / phraseBars, i = song % L;
                const double bar0 = song * bpb;
                const int fill = p.kind == Kind::drums ? fillAt (sec, b) : 0;
                for (auto& [role, bars] : p.loop)
                {
                    if (role == Role::crash || role == Role::incidental) continue;
                    bool on = activeIn (role, sec.part, phrase, phrases);
                    // the end of a build-up: no kick in its last bar, no bass in its last two
                    if (build && role == Role::kick && b == sec.bars - 1) on = false;
                    if (build && role == Role::bass && b >= sec.bars - 2) on = false;
                    if (! on) continue;
                    const int src = p.source[role][(size_t) i];
                    const int shift = sec.part == Part::drop2 && role == Role::lead && phrase % 2 == 1 && p.maxLead + 12 <= 84 ? 12 : 0;
                    // a big fill: the loop's own fill bar for the snare, the clap and the toms (a snare that only
                    // plays in the fill plays only there)
                    std::vector<N> fillNotes;
                    if (fill == 2 && (role == Role::snare || role == Role::clap || role == Role::toms))
                        for (auto& n : p.fill) if (drumRole (n.pitch) == role) fillNotes.push_back (n);
                    const bool useFill = ! fillNotes.empty();
                    if (src < 0 && ! useFill) continue;
                    const auto& notes = useFill ? fillNotes : bars[(size_t) src];
                    // the kick takes a breath before the second half of a long drop (beats 3 and 4 of bar 16)
                    const bool kickBreath = drop && role == Role::kick && phrases >= 4 && b == 2 * phraseBars - 1;
                    for (auto& n : notes)
                    {
                        // one beat of silence before every drop
                        if (build && b == sec.bars - 1 && n.beat >= 3.0 - 1.0e-6) continue;
                        if (kickBreath && n.beat >= 2.0 - 1.0e-6) continue;
                        p.out.push_back ({ bar0 + n.beat, n.len, n.pitch + shift, n.vel });
                    }
                    p.on[(size_t) song] = true;
                }
                // one-off hits from the loop, one at a time, at the end of a phrase where the drums are playing
                if (fill > 0 && ! p.incidentals.empty() && p.on[(size_t) song])
                {
                    for (auto& n : p.incidentals[nextIncidental % p.incidentals.size()])
                        p.out.push_back ({ bar0 + n.beat, n.len, n.pitch, n.vel });
                    ++nextIncidental;
                }
            }
        }
    }

    // ---- the kit: crashes on the one of the big moments, a snare roll at the end of each build-up
    if (kit != nullptr)
    {
        auto add = [&] (double beat, double len, int pitch, int vel)
        {
            kit->out.push_back ({ beat, len, pitch, jlimit (1, 127, vel) });
            kit->on[(size_t) jlimit (0, total - 1, (int) (beat / bpb))] = true;
        };
        for (auto& sec : sections)
        {
            const double s0 = sec.startBar * bpb;
            switch (sec.part)
            {
                case Part::drop: case Part::drop2:
                    for (int b = 0; b < sec.bars; b += phraseBars) add (s0 + b * bpb, 2.0, 49, b == 0 ? 118 : 96);
                    break;
                case Part::breakdown: case Part::build2: case Part::outro:
                    add (s0, 2.0, 49, 104);
                    break;
                case Part::build:
                {
                    if (sec.startBar > 0) add (s0, 2.0, 49, 92);
                    break;
                }
                default: break;
            }
            if (sec.part == Part::build || sec.part == Part::build2)
            {
                // quarters, eighths, sixteenths and thirty-seconds: the classic roll over the last 8 bars, louder and louder
                const int rollBars = jmin (8, sec.bars);
                const double r0 = (sec.startBar + sec.bars - rollBars) * bpb, r1 = (sec.startBar + sec.bars) * bpb - 1.0;
                for (int k = 0; k < rollBars; ++k)
                {
                    const int fromEnd = rollBars - 1 - k;
                    const double step = fromEnd >= 4 ? 1.0 : fromEnd >= 2 ? 0.5 : fromEnd == 1 ? 0.25 : 0.125;
                    const double b0 = r0 + k * bpb;
                    for (double x = 0.0; x < bpb - 1.0e-6; x += step)
                    {
                        const double at = b0 + x;
                        if (at >= r1 - 1.0e-6) break;
                        const double prog = (at - r0) / jmax (1.0, r1 - r0);
                        add (at, jmin (step, 0.25) * 0.9, rollPitch, (int) (54 + 66 * prog));
                    }
                }
            }
            // fills where the loop has none of its own: two sixteenths at the end of a phrase, a whole beat of
            // sixteenths (down the toms if the kit has toms) every 16 bars
            if (kit->fill.empty())
            {
                std::vector<int> toms;
                if (kit->loop.count (Role::toms))
                {
                    std::set<int> ts2;
                    for (auto& bar : kit->loop[Role::toms]) for (auto& n : bar) ts2.insert (n.pitch);
                    toms.assign (ts2.rbegin(), ts2.rend());
                }
                for (int b = 0; b < sec.bars; ++b)
                {
                    const int f = fillAt (sec, b);
                    if (f == 0 || ! kit->on[(size_t) (sec.startBar + b)]) continue;
                    const double b0 = (sec.startBar + b) * bpb;
                    if (f == 1) { add (b0 + 3.5, 0.2, rollPitch, 74); add (b0 + 3.75, 0.2, rollPitch, 88); continue; }
                    for (int k = 0; k < 4; ++k)
                        add (b0 + 3.0 + k * 0.25, 0.2, toms.empty() ? rollPitch : toms[(size_t) jmin ((int) toms.size() - 1, k * (int) toms.size() / 4)], 70 + k * 12);
                }
            }
        }
    }

    // ---- out with what was there, in with the song
    Array<te::AudioTrack*> touched;
    for (auto& p : plans) touched.add (p.track);
    s.deleteRange (touched, startBeat, songEnd);

    for (auto& p : plans)
    {
        if (p.isAudio) continue;
        std::sort (p.out.begin(), p.out.end(), [] (const N& a, const N& b) { return a.beat < b.beat; });
        for (auto& sec : sections)
        {
            int b = 0;
            while (b < sec.bars)
            {
                if (! p.on[(size_t) (sec.startBar + b)]) { ++b; continue; }
                int e = b;
                while (e < sec.bars && p.on[(size_t) (sec.startBar + e)]) ++e;
                const double c0 = (sec.startBar + b) * bpb, c1 = (sec.startBar + e) * bpb;
                if (auto* clip = s.createMidiClip (*p.track, { te::BeatPosition::fromBeats (startBeat + c0), te::BeatPosition::fromBeats (startBeat + c1) }, sec.name))
                {
                    auto& seq = clip->getSequence();
                    for (auto& n : p.out)
                    {
                        if (n.beat < c0 - 1.0e-6 || n.beat >= c1 - 1.0e-6) continue;
                        const double len = jmax (0.05, jmin (n.len, c1 - n.beat));
                        seq.addNote (n.pitch, te::BeatPosition::fromBeats (n.beat - c0), te::BeatDuration::fromBeats (len), n.vel, 0, um);
                    }
                }
                b = e;
            }
        }
    }

    // ---- audio channels: the selection's audio, again and again where the table says, cut at the edges
    for (auto& p : plans)
    {
        if (! p.isAudio) continue;
        std::vector<bool> on ((size_t) total, false);
        for (auto& sec : sections)
            for (int b = 0; b < sec.bars; ++b)
                on[(size_t) (sec.startBar + b)] = activeIn (p.role, sec.part, b / phraseBars, sec.bars / phraseBars);
        int b = 0;
        while (b < total)
        {
            if (! on[(size_t) b]) { ++b; continue; }
            int e = b;
            while (e < total && on[(size_t) e]) ++e;
            double r0 = b * bpb, r1 = e * bpb;
            // a build-up that ends in this run: its last beat stays silent
            for (auto& sec : sections)
                if ((sec.part == Part::build || sec.part == Part::build2) && sec.startBar + sec.bars == e) r1 -= 1.0;
            for (int k = b / L; k * L < e; ++k)
            {
                const double w0 = k * L * bpb;
                for (auto& piece : p.audio.pieces)
                {
                    auto n = piece.state.createCopy();
                    te::EditItemID::remapIDs (n, nullptr, edit);
                    const auto id = te::EditItemID::fromID (n);
                    p.track->state.appendChild (n, um);
                    auto c = p.track->findClipForID (id);
                    if (c == nullptr) continue;
                    const double a = w0 + piece.startBeat;
                    c->setStart (ts.toTime (te::BeatPosition::fromBeats (startBeat + a)), false, true);
                    const double ce = ts.toBeats (c->getPosition().getEnd()).inBeats() - startBeat;
                    if (ce <= r0 + 1.0e-6 || a >= r1 - 1.0e-6) { c->removeFromParent(); continue; }
                    if (a < r0) c->setStart (ts.toTime (te::BeatPosition::fromBeats (startBeat + r0)), true, false);
                    if (ce > r1) c->setEnd (ts.toTime (te::BeatPosition::fromBeats (startBeat + r1)), true);
                    c->setName (p.track->getName());
                }
            }
            b = e;
        }
    }

    // ---- transitions on their own channel: risers into the drops, impacts on them, a way down into the breakdown
    if (o.transitions)
    {
        te::AudioTrack* fx = nullptr;
        for (auto* t : te::getAudioTracks (edit)) if ((bool) t->state.getProperty (fxTrackProp)) { fx = t; break; }
        if (fx == nullptr)
        {
            fx = s.addAudioTrack (String::fromUTF8 ("Transiciones"));
            if (fx != nullptr)
            {
                fx->state.setProperty (fxTrackProp, true, um);
                fx->setColour (Colour (0xff8a8f98));
                if (auto* v = fx->getVolumePlugin()) v->setVolumeDb (-7.0f);
            }
        }
        if (fx != nullptr)
        {
            s.deleteRange ({ fx }, startBeat, songEnd);
            const auto dir = s.audioFolder().getChildFile ("Transiciones");
            const double secPerBeat = 60.0 / jmax (1.0, s.getBpm());
            auto place = [&] (const File& f, double beat, double lenBeats, const String& name)
            {
                if (! f.existsAsFile()) return;
                if (auto* c = s.addAudioFile (*fx, f, ts.toTime (te::BeatPosition::fromBeats (startBeat + beat))))
                {
                    c->setName (name);
                    if (lenBeats > 0.0) c->setEnd (ts.toTime (te::BeatPosition::fromBeats (startBeat + beat + lenBeats)), true);
                }
            };
            for (auto& sec : sections)
            {
                const double s0 = sec.startBar * bpb, s1 = (sec.startBar + sec.bars) * bpb;
                if (sec.part == Part::build || sec.part == Part::build2)
                {
                    const double len = jmin (8, sec.bars) * bpb - 1.0;
                    place (riser (dir, len * secPerBeat), s1 - 1.0 - len, len, "Riser");
                }
                if (sec.part == Part::drop || sec.part == Part::drop2)
                    place (impact (dir), s0, jmin (2.0 * bpb, 3.0 / secPerBeat), "Impacto");
                if (sec.part == Part::breakdown || sec.part == Part::outro)
                    place (downlifter (dir, 2.0 * bpb * secPerBeat), s0, 2.0 * bpb, "Bajada");
                // into the build-up, the breakdown and the outro: one bar of reversed cymbal that lands on the one
                if ((sec.part == Part::build || sec.part == Part::breakdown || sec.part == Part::outro) && sec.startBar > 0)
                    place (reverseCymbal (dir, bpb * secPerBeat), s0 - bpb, bpb, String::fromUTF8 ("Platillo al revés"));
            }
            res.fxTrack = fx;
        }
    }

    // ---- the high-pass on the master: closed (10 Hz) except in the breakdown (180 Hz) and the build-up sweeps
    if (o.sweep)
    {
        auto& list = edit.getMasterPluginList();
        te::Plugin* hp = nullptr;
        for (auto* p : list) if ((bool) p->state.getProperty (sweepProp)) { hp = p; break; }
        if (hp == nullptr)
        {
            CatalogItem item;
            for (auto& it : Catalog::nativeEffects()) if (it.builtInType == "filtro-automatico") item = it;
            if (item.builtInType.isNotEmpty())
                if (auto np = Catalog::create (edit, item))
                {
                    list.insertPlugin (np, 0, nullptr);
                    np->state.setProperty (sweepProp, true, nullptr);
                    hp = np.get();
                }
        }
        if (hp != nullptr)
        {
            setPlain (*hp, "type", 1.0f);       // paso alto
            setPlain (*hp, "slope", 1.0f);      // 24 dB/oct
            setPlain (*hp, "reso", 8.0f);
            setPlain (*hp, "lfodepth", 0.0f);
            setPlain (*hp, "envamt", 0.0f);
            setPlain (*hp, "stepamt", 0.0f);
            setPlain (*hp, "mix", 100.0f);
            setPlain (*hp, "cutoff", 10.0f);
            auto* cut = paramOf (*hp, "cutoff");
            auto* jp = juceParamOf (*hp, "cutoff");
            if (cut != nullptr && jp != nullptr)
            {
                auto& curve = cut->getCurve();
                const auto vr = cut->getValueRange();
                auto value = [&] (float hz)
                {
                    const float norm = jp->convertTo0to1 (hz);
                    return vr.getStart() == 0.0f && vr.getEnd() == 1.0f ? norm : jp->convertFrom0to1 (norm);
                };
                auto t = [&] (double beat) { return ts.toTime (te::BeatPosition::fromBeats (startBeat + beat)); };
                curve.removePointsInRegion ({ t (-0.01), t (total * bpb + 0.01) });
                auto pt = [&] (double beat, float hz) { curve.addPoint (t (beat), value (hz), 0.0f); };
                pt (0.0, 10.0f);
                for (auto& sec : sections)
                {
                    const double s0 = sec.startBar * bpb, s1 = (sec.startBar + sec.bars) * bpb;
                    if (sec.part == Part::breakdown)
                    {
                        pt (s0 - 0.02, 10.0f); pt (s0 + 0.5, 180.0f);
                        pt (s1 - 0.02, 180.0f); pt (s1, 10.0f);
                    }
                    if (sec.part == Part::build || sec.part == Part::build2)
                    {
                        // eight bars that open more and more quickly, up to 400 Hz, then shut on the one of the drop
                        const double w0 = s1 - jmin (8, sec.bars) * bpb;
                        for (int k = 0; k <= 16; ++k)
                        {
                            const double x = k / 16.0;
                            pt (w0 + x * (s1 - 0.02 - w0), (float) (10.0 * std::pow (40.0, x * x)));
                        }
                        pt (s1, 10.0f);
                    }
                }
                pt (total * bpb, 10.0f);
            }
            res.sweepPlugin = hp;
        }
    }

    // a marker on the ruler at the start of every section
    {
        auto& mm = edit.getMarkerManager();
        const auto m0 = ts.toTime (te::BeatPosition::fromBeats (startBeat)), m1 = ts.toTime (te::BeatPosition::fromBeats (songEnd));
        for (auto& mk : mm.getMarkers())
            if (mk->getPosition().getStart() >= m0 - te::TimeDuration::fromSeconds (0.001) && mk->getPosition().getStart() < m1)
                mk->removeFromParent();
        int number = mm.getMarkers().size();
        for (auto& sec : sections)
        {
            const auto t0 = ts.toTime (te::BeatPosition::fromBeats (startBeat + sec.startBar * bpb));
            const auto t1 = ts.toTime (te::BeatPosition::fromBeats (startBeat + (sec.startBar + sec.bars) * bpb));
            if (auto mk = mm.createMarker (++number, t0, t1 - t0, nullptr))
                mk->setName (sec.name);
        }
    }

    s.setLooping (false);
    s.getTransport().setPosition (ts.toTime (te::BeatPosition::fromBeats (startBeat)));
    res.ok = true;
    res.message = String::fromUTF8 ("Rola de ") + String (total) + String::fromUTF8 (" compases (") + clock (minutesForBars (total, s.getBpm()) * 60.0)
                + String::fromUTF8 (") hecha con un loop de ") + String (L) + (L == 1 ? String::fromUTF8 (" compás.") : String::fromUTF8 (" compases."));
    return res;
}
}

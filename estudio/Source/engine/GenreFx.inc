// Efectos por género: risers, rollers, impactos, incidentales y texturas para cada uno de los géneros de Racoon,
// a su tempo y en su tonalidad. Todo se sintetiza aquí (osciladores, ruido filtrado, envolventes y los golpes de la
// biblioteca), con una semilla fija por sonido: la misma versión de Racoon escribe siempre los mismos archivos.
// Este archivo vive dentro del espacio de nombres anónimo de SoundLibrary.cpp y usa sus herramientas.

namespace gfx
{
constexpr int kFxVersion = 1;

inline double barSecs (double bpm) { return 240.0 / std::max (40.0, bpm); }
inline float midiHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }
inline float sawOf (float ph) { return 2.0f * (ph - std::floor (ph)) - 1.0f; }
inline float triOf (float ph) { const float p = ph - std::floor (ph); return 4.0f * std::abs (p - 0.5f) - 1.0f; }

void addAt (Mono& dst, const Mono& src, size_t at, float g)
{
    if (dst.size() < at + src.size()) dst.resize (at + src.size(), 0.0f);
    for (size_t i = 0; i < src.size(); ++i) dst[at + i] += src[i] * g;
}

void addAt (Stereo& dst, const Mono& src, size_t at, float g, float pan)
{
    const size_t n = at + src.size();
    if (dst.l.size() < n) { dst.l.resize (n, 0.0f); dst.r.resize (n, 0.0f); }
    const float gl = g * std::sqrt (0.5f * (1.0f - pan)), gr = g * std::sqrt (0.5f * (1.0f + pan));
    for (size_t i = 0; i < src.size(); ++i) { dst.l[at + i] += src[i] * gl; dst.r[at + i] += src[i] * gr; }
}

// a few milliseconds of delay on one side: width without a reverb
Stereo widen (const Mono& m, float ms = 11.0f)
{
    Stereo s;
    s.l = m;
    const size_t d = (size_t) (ms * 0.001f * SR);
    s.r.assign (m.size() + d, 0.0f);
    s.l.resize (m.size() + d, 0.0f);
    for (size_t i = 0; i < m.size(); ++i) s.r[i + d] = m[i];
    return s;
}

Stereo mixStereo (Stereo a, const Stereo& b, float gb = 1.0f)
{
    const size_t n = std::max (a.l.size(), b.l.size());
    a.l.resize (n, 0.0f); a.r.resize (n, 0.0f);
    for (size_t i = 0; i < b.l.size(); ++i) { a.l[i] += b.l[i] * gb; a.r[i] += b.r[i] * gb; }
    return a;
}

// feedback echo on a stereo sound (ping-pong): dub sirens, lasers, sonidero voices
Stereo echo (Stereo s, double delaySecs, float feedback, int repeats)
{
    const size_t d = (size_t) (delaySecs * SR);
    const size_t n = s.l.size() + d * (size_t) repeats;
    Stereo out { Mono (n, 0.0f), Mono (n, 0.0f) };
    float g = 1.0f;
    for (int k = 0; k <= repeats; ++k)
    {
        const bool swap = (k % 2) == 1;
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            out.l[d * (size_t) k + i] += (swap ? s.r[i] : s.l[i]) * g;
            out.r[d * (size_t) k + i] += (swap ? s.l[i] : s.r[i]) * g;
        }
        g *= feedback;
    }
    return out;
}

void cutTo (Stereo& s, double secs)
{
    const size_t n = (size_t) (secs * SR);
    s.l.resize (n, 0.0f); s.r.resize (n, 0.0f);
    fadeTail (s.l, 0.01); fadeTail (s.r, 0.01);
}

// the last secs of a sound (a reversed tail that ends on the beat)
void keepEnd (Stereo& s, double secs)
{
    const size_t n = (size_t) (secs * SR);
    if (s.l.size() > n)
    {
        s.l.erase (s.l.begin(), s.l.end() - (std::ptrdiff_t) n);
        s.r.erase (s.r.begin(), s.r.end() - (std::ptrdiff_t) n);
    }
    const size_t fade = std::min (s.l.size(), (size_t) (0.02 * SR));
    for (size_t i = 0; i < fade; ++i) { const float g = (float) i / (float) fade; s.l[i] *= g; s.r[i] *= g; }
}

std::vector<int> scaleOf (bool major) { return major ? std::vector<int> { 0, 2, 4, 5, 7, 9, 11 } : std::vector<int> { 0, 2, 3, 5, 7, 8, 10 }; }

// a soft chord of saws (root position triad) for swells, tape stops and wobbles
Mono chordPad (int rootMidi, bool major, double secs, float cutoff, Rng& r, float attack = 0.02f)
{
    auto m = make (secs);
    const int third = major ? 4 : 3;
    const float notes[] { (float) rootMidi, (float) (rootMidi + third), (float) (rootMidi + 7), (float) (rootMidi + 12) };
    float ph[8];
    for (auto& p : ph) p = r.uni();
    Svf lp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float s = 0.0f;
        for (int k = 0; k < 4; ++k)
            for (int d = 0; d < 2; ++d)
            {
                ph[k * 2 + d] += midiHz (notes[k] + (d == 0 ? -0.08f : 0.08f)) / (float) SR;
                s += sawOf (ph[k * 2 + d]);
            }
        const float env = std::min (1.0f, t / attack) * std::min (1.0f, (float) (secs - t) / 0.05f);
        m[i] = lp.run (s * 0.12f, cutoff, 0.8f, Svf::lp) * env;
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------- risers
Stereo uplifter (double secs, int rootMidi, bool major, Rng& r)
{
    auto m = make (secs);
    const int third = major ? 4 : 3;
    const float notes[] { (float) rootMidi, (float) (rootMidi + 7), (float) (rootMidi + 12), (float) (rootMidi + 12 + third) };
    float ph[8];
    for (auto& p : ph) p = r.uni();
    Svf lp, nbp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size();
        const float up = std::pow (2.0f, x * x);          // one octave up, slow at first
        float s = 0.0f;
        for (int k = 0; k < 4; ++k)
            for (int d = 0; d < 2; ++d)
            {
                ph[k * 2 + d] += midiHz (notes[k] + (d == 0 ? -0.1f : 0.1f)) * up / (float) SR;
                s += sawOf (ph[k * 2 + d]);
            }
        const float fc = 250.0f * std::pow (40.0f, x);
        const float nz = nbp.run (r.bi(), 400.0f * std::pow (20.0f, x), 2.0f, Svf::bp);
        m[i] = (lp.run (s * 0.1f, fc, 1.2f, Svf::lp) + nz * 0.5f * x) * std::pow (x, 1.4f);
    }
    fadeTail (m, 0.01);
    return room (m, 1.3f, 0.4f, 0.35f, 0.8);
}

// ---------------------------------------------------------------------------------------------------- rollers
enum class RollKind { clap, snare, hat, kick, shaker, breaks, hardKick };

Stereo roller (RollKind kind, int bars, double bpm, Rng& r)
{
    const double bar = barSecs (bpm), total = bar * bars;
    // a few takes of the hit, so the roll does not sound like a machine gun
    std::vector<Mono> hits, ghosts;
    for (int k = 0; k < 3; ++k)
    {
        Rng hr (r.next());
        switch (kind)
        {
            case RollKind::clap:   { ClapP p; p.bursts = 3 + k % 2; p.freq = 1150.0f + 80.0f * (float) k; p.tail = 0.18f; hits.push_back (clap (p, hr)); break; }
            case RollKind::snare:  { SnareP p; p.f = 200.0f + 6.0f * (float) k; p.noiseDec = 0.16f; p.snap = 0.4f; hits.push_back (snare (p, hr)); break; }
            case RollKind::hat:    { HatP p; p.dec = 0.035f + 0.005f * (float) k; p.len = 0.15f; hits.push_back (hat (p, hr)); break; }
            case RollKind::kick:   { KickP p; p.f0 = 240; p.f1 = 52; p.dec = 0.22f; p.drive = 0.5f; p.len = 0.4f; hits.push_back (kick (p, hr)); break; }
            case RollKind::hardKick: { KickP p; p.f0 = 320; p.f1 = 55; p.pitchT = 0.03f; p.dec = 0.25f; p.drive = 1.0f; p.click = 0.7f; p.len = 0.45f; hits.push_back (kick (p, hr)); break; }
            case RollKind::shaker: hits.push_back (shaker (5200.0f + 300.0f * (float) k, 0.008f, 0.05f, 3, 0.006f, hr)); break;
            case RollKind::breaks:
            {
                SnareP p; p.f = 185.0f; p.noiseDec = 0.18f; p.snap = 0.5f; p.drive = 0.5f;
                hits.push_back (snare (p, hr));
                KickP kp; kp.f0 = 200; kp.f1 = 55; kp.dec = 0.25f; kp.len = 0.4f;
                ghosts.push_back (kick (kp, hr));
                break;
            }
        }
    }
    Stereo out { make (total + 0.4), make (total + 0.4) };
    // eighths, then sixteenths, then thirty-seconds: the roll speeds up into the drop
    double t = 0.0;
    int n = 0;
    while (t < total - 1.0e-6)
    {
        const double x = t / total;
        const double step = x < 0.5 ? bar / 8.0 : x < 0.75 ? bar / 16.0 : bar / 32.0;
        const float vel = 0.3f + 0.7f * (float) std::pow (x, 1.3);
        const float pan = kind == RollKind::hat || kind == RollKind::shaker ? (n % 2 == 0 ? -0.25f : 0.25f) : 0.0f;
        addAt (out, hits[(size_t) (n % (int) hits.size())], (size_t) (t * SR), vel, pan);
        if (kind == RollKind::breaks && ! ghosts.empty() && n % 4 == 0) addAt (out, ghosts[0], (size_t) (t * SR), vel * 0.8f, 0.0f);
        t += step;
        ++n;
    }
    // the low end opens up as it builds (the classic high-pass sweep on a snare roll)
    if (kind == RollKind::clap || kind == RollKind::snare || kind == RollKind::breaks)
    {
        Svf hl, hr2;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            const float x = std::min (1.0f, (float) i / (float) (total * SR));
            const float fc = 80.0f + 900.0f * x * x;
            out.l[i] = hl.run (out.l[i], fc, 0.7f, Svf::hp);
            out.r[i] = hr2.run (out.r[i], fc, 0.7f, Svf::hp);
        }
    }
    auto wet = room (toMono (out), 1.0f, 0.4f, 0.22f, 0.5);
    return mixStereo (out, wet, 0.6f);
}

// ---------------------------------------------------------------------------------------------------- reverse
Stereo reverseCymbal (double secs, int v, Rng& r)
{
    // a long crash (it is still loud where the reversed sound starts), so the swell is heard from the first beat
    auto c = crash (1.0f + 0.12f * (float) v, (float) secs * 2.2f, r);
    auto rev = reversed (c);
    keepEnd (rev, secs);
    for (size_t i = 0; i < rev.l.size(); ++i)
    {
        const float g = std::pow ((float) i / (float) rev.l.size(), 0.8f);
        rev.l[i] *= g; rev.r[i] *= g;
    }
    return rev;
}

// a deeper, longer boom with a falling wash of noise: the hit at the start of a drop in cinematic and bass music
Stereo bigImpact (Rng& r)
{
    KickP k; k.f0 = 120; k.f1 = 30; k.pitchT = 0.14f; k.dec = 2.4f; k.hold = 0.08f; k.click = 0.8f; k.drive = 0.9f; k.len = 3.0f;
    auto boom = kick (k, r);
    auto nz = make (1.6);
    Svf bp;
    for (size_t i = 0; i < nz.size(); ++i)
    {
        const float x = (float) i / (float) nz.size();
        nz[i] = bp.run (r.bi(), 6000.0f * std::pow (0.05f, x), 1.5f, Svf::bp) * (1.0f - x) * 0.9f;
    }
    addAt (boom, nz, 0, 1.0f);
    return room (boom, 2.2f, 0.3f, 0.6f, 3.0);
}

Stereo reverseSwell (int rootMidi, bool major, double secs, Rng& r)
{
    auto stab = chordPad (rootMidi, major, 0.35, 2600.0f, r, 0.005f);
    fadeTail (stab, 0.1);
    auto big = room (stab, 2.2f, 0.25f, 0.95f, secs + 0.5);
    auto rev = reversed (big);
    keepEnd (rev, secs);
    return rev;
}

// ---------------------------------------------------------------------------------------------------- sweeps
Stereo filterSweep (double secs, int mode, Rng& r)   // 0 up, 1 down, 2 up and down
{
    Stereo s { make (secs), make (secs) };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rc (r.next());
        Svf bp;
        auto& m = ch == 0 ? s.l : s.r;
        for (size_t i = 0; i < m.size(); ++i)
        {
            float x = (float) i / (float) m.size();
            if (mode == 1) x = 1.0f - x;
            if (mode == 2) x = std::sin (x * kPi);
            const float fc = 180.0f * std::pow (50.0f, x);
            const float amp = mode == 2 ? 0.3f + 0.7f * x : (mode == 1 ? std::pow (x, 0.6f) : std::pow (x, 1.2f));
            m[i] = bp.run (rc.bi(), fc, 3.0f, Svf::bp) * amp * 1.8f;
        }
        fadeTail (m, 0.02);
    }
    return mixStereo (s, room (toMono (s), 1.4f, 0.4f, 0.3f, 0.6), 0.5f);
}

Stereo noiseWash (double secs, Rng& r)
{
    Stereo s { make (secs), make (secs) };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rc (r.next());
        FixedSvf lp (7000.0f, 0.7f, Svf::lp), hp (300.0f, 0.7f, Svf::hp);
        auto& m = ch == 0 ? s.l : s.r;
        for (size_t i = 0; i < m.size(); ++i)
        {
            const float x = (float) i / (float) m.size();
            const float env = x < 0.35f ? std::pow (x / 0.35f, 2.0f) : std::exp (-(x - 0.35f) * 5.0f);
            m[i] = hp.run (lp.run (rc.bi())) * env * 0.8f;
        }
    }
    return mixStereo (s, room (toMono (s), 2.0f, 0.3f, 0.6f, 1.5), 0.7f);
}

// ---------------------------------------------------------------------------------------------------- impacts
Stereo metalHit (int v, Rng& r)
{
    static const float ratios[] { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 17.2f };
    const float f = 170.0f + 45.0f * (float) v + r.in (-10.0f, 10.0f);
    auto m = make (2.6);
    float ph[6];
    for (auto& p : ph) p = r.uni();
    FixedSvf nhp (2500.0f, 0.7f, Svf::hp);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float s = 0.0f;
        for (int k = 0; k < 6; ++k)
        {
            ph[k] += ratios[k] * f / (float) SR;
            s += std::sin (kTwoPi * ph[k]) * decay (t, 2.2f / (1.0f + 0.6f * (float) k)) / (1.0f + 0.4f * (float) k);
        }
        const float nz = nhp.run (r.bi()) * std::exp (-t / 0.02f) * 0.8f;
        m[i] = sat ((s * 0.5f + nz) * std::min (1.0f, t / 0.0006f), 0.4f);
    }
    trimSilence (m);
    return room (m, 1.5f, 0.35f, 0.4f, 1.5);
}

Stereo subDrop (int rootMidi, double secs, Rng& r)
{
    auto m = make (secs);
    float ph = r.uni();
    const float f0 = midiHz ((float) rootMidi + 12.0f), f1 = midiHz ((float) rootMidi - 12.0f);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size(), t = (float) i / (float) SR;
        const float f = f0 * std::pow (f1 / f0, std::pow (x, 0.7f));
        ph += f / (float) SR;
        m[i] = sat (std::sin (kTwoPi * ph) * std::min (1.0f, t / 0.004f) * (1.0f - std::pow (x, 2.5f)), 0.35f);
    }
    fadeTail (m, 0.02);
    return Stereo { m, m };
}

Stereo slide808 (int rootMidi, int fromSemis, Rng& r)
{
    const double secs = 1.6;
    auto m = make (secs);
    float ph = 0.0f;
    FixedSvf click (2500.0f, 0.8f, Svf::bp);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        // holds the high note, then glides down to the root
        const float g = t < 0.28f ? 0.0f : std::min (1.0f, (t - 0.28f) / 0.14f);
        const float note = (float) rootMidi + (float) fromSemis * (1.0f - g * g * (3.0f - 2.0f * g));
        const float bend = 1.0f + 1.4f * std::exp (-t / 0.015f);
        ph += midiHz (note) * bend / (float) SR;
        const float env = std::min (1.0f, t / 0.002f) * decay (std::max (0.0f, t - 0.05f), 1.4f);
        m[i] = sat (std::sin (kTwoPi * ph) * env + click.run (r.bi()) * std::exp (-t / 0.003f) * 0.6f, 0.7f);
    }
    fadeTail (m, 0.03);
    return Stereo { m, m };
}

// ---------------------------------------------------------------------------------------------------- incidentals
Stereo laser (int kind, int v, double bpm, Rng& r)   // 0 zap, 1 disco, 2 retro
{
    const double len = kind == 0 ? 0.22 + 0.08 * v : kind == 1 ? 0.55 : 0.45;
    auto m = make (len);
    float ph = r.uni(), mph = 0.0f;
    const float from = kind == 1 ? 600.0f + 200.0f * (float) v : 2600.0f + 900.0f * (float) v, to = kind == 1 ? 2400.0f : 90.0f + 40.0f * (float) v;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size(), t = (float) i / (float) SR;
        const float f = from * std::pow (to / from, kind == 1 ? x : std::pow (x, 0.45f));
        mph += f * 1.5f / (float) SR;
        const float index = (kind == 0 ? 2.5f : 0.8f) * (1.0f - x);
        ph += (f + std::sin (kTwoPi * mph) * f * index) / (float) SR;
        float s = std::sin (kTwoPi * ph);
        if (kind == 2) s = std::tanh (s * 4.0f);
        m[i] = s * std::min (1.0f, t / 0.002f) * (1.0f - std::pow (x, 1.5f)) * 0.8f;
    }
    fadeTail (m, 0.01);
    auto s = kind == 0 ? Stereo { m, m } : widen (m, 7.0f);
    if (kind != 0) s = echo (s, barSecs (bpm) * 3.0 / 16.0, 0.45f, 4);
    return s;
}

Stereo siren (bool dub, double bpm, int v, Rng& r)
{
    const double bar = barSecs (bpm), secs = bar * 2.0;
    auto m = make (dub ? bar : secs);
    float ph = r.uni();
    FixedSvf lp (dub ? 3500.0f : 2200.0f, 0.8f, Svf::lp);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float f;
        if (dub)
        {
            // the dub siren: a fast square LFO between two notes, then a whoop up
            const float rate = (float) (bpm / 60.0 * (v == 0 ? 4.0 : 2.0));
            const float sq = std::fmod (t * rate, 1.0f) < 0.5f ? 0.0f : 1.0f;
            const float whoop = t > (float) bar * 0.6f ? std::pow ((t - (float) bar * 0.6f) / ((float) bar * 0.4f), 2.0f) : 0.0f;
            f = 520.0f * std::pow (1.5f, sq) * std::pow (2.0f, whoop);
        }
        else
        {
            // an air-raid style siren: up and down, twice
            f = 380.0f + 520.0f * (0.5f + 0.5f * triOf (t / (float) bar + 0.5f + 0.1f * (float) v));
        }
        ph += f / (float) SR;
        const float s = dub ? (std::fmod (ph, 1.0f) < 0.5f ? 0.6f : -0.6f) : sawOf (ph) * 0.5f + std::sin (kTwoPi * ph) * 0.5f;
        const float env = std::min (1.0f, t / 0.02f) * std::min (1.0f, ((float) m.size() / (float) SR - t) / 0.05f);
        m[i] = lp.run (s) * env * 0.7f;
    }
    auto s = widen (m, 9.0f);
    if (dub) s = echo (s, bar * 3.0 / 16.0, 0.55f, 6);
    return mixStereo (s, room (m, 1.2f, 0.4f, 0.25f, 0.6), 0.5f);
}

Stereo airHorn (double bpm, int v, Rng& r)
{
    // three detuned horns in a major-ish cluster through two formants, blown in the classic short-short-long
    const double six = barSecs (bpm) / 16.0;
    struct Blast { double at, len; };
    std::vector<Blast> pattern = v == 0 ? std::vector<Blast> { { 0.0, six * 1.2 }, { six * 2.0, six * 1.2 }, { six * 4.0, six * 7.0 } }
                                        : std::vector<Blast> { { 0.0, six * 1.0 }, { six * 1.5, six * 1.0 }, { six * 3.0, six * 1.0 }, { six * 4.5, six * 9.0 } };
    const double total = pattern.back().at + pattern.back().len + 0.2;
    auto m = make (total);
    const float base = 400.0f + 40.0f * (float) v;
    const float ratios[] { 1.0f, 1.26f, 1.5f };
    for (auto& b : pattern)
    {
        float ph[6];
        for (auto& p : ph) p = r.uni();
        FixedSvf f1 (950.0f, 2.0f, Svf::bp), f2 (2400.0f, 2.5f, Svf::bp), top (5000.0f, 0.7f, Svf::lp);
        const size_t start = (size_t) (b.at * SR), n = (size_t) ((b.len + 0.12) * SR);
        for (size_t i = 0; i < n && start + i < m.size(); ++i)
        {
            const float t = (float) i / (float) SR;
            const float fall = t > (float) b.len - 0.05f ? std::pow (2.0f, -0.6f * (t - ((float) b.len - 0.05f)) / 0.12f) : 1.0f;
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
                for (int d = 0; d < 2; ++d)
                {
                    ph[k * 2 + d] += base * ratios[k] * (d == 0 ? 0.996f : 1.004f) * fall / (float) SR;
                    s += sawOf (ph[k * 2 + d]);
                }
            const float env = std::min (1.0f, t / 0.006f) * (t > (float) b.len ? std::exp (-(t - (float) b.len) / 0.03f) : 1.0f);
            m[start + i] += top.run (std::tanh ((f1.run (s) * 1.4f + f2.run (s) * 0.9f + s * 0.08f) * 1.3f)) * env * 0.7f;
        }
    }
    return mixStereo (widen (m, 6.0f), room (m, 1.5f, 0.3f, 0.35f, 0.8), 0.6f);
}

Stereo rewind (int v, Rng& r)
{
    const double secs = 1.3 + 0.2 * v;
    auto m = make (secs);
    float ph = r.uni(), lph = 0.0f;
    Svf bp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size(), t = (float) i / (float) SR;
        // the record pulled back faster and faster: a scratchy "wika" whose rate climbs
        const float rate = 4.0f + 26.0f * x * x;
        lph += rate / (float) SR;
        const float wob = 0.5f + 0.5f * std::sin (kTwoPi * lph);
        const float f = (220.0f + 1800.0f * x) * (0.6f + 0.8f * wob);
        ph += f / (float) SR;
        const float tone = sawOf (ph) * 0.5f + bp.run (r.bi(), f * 2.0f, 2.0f, Svf::bp) * 0.8f;
        m[i] = tone * (0.4f + 0.6f * wob) * std::min (1.0f, t / 0.01f) * (x > 0.92f ? (1.0f - x) / 0.08f : 1.0f);
    }
    crush (m, 0.2f);
    return widen (m, 8.0f);
}

Stereo tapeStop (int rootMidi, bool major, double bpm, Rng& r)
{
    const double secs = barSecs (bpm);
    auto m = make (secs);
    const int third = major ? 4 : 3;
    const float notes[] { (float) rootMidi, (float) (rootMidi + third), (float) (rootMidi + 7), (float) (rootMidi - 12) };
    float ph[4];
    for (auto& p : ph) p = r.uni();
    float speedPos = 0.0f;
    Svf lp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size();
        // full speed for a quarter of the bar, then the tape slows to a stop
        const float speed = x < 0.25f ? 1.0f : std::pow (1.0f - (x - 0.25f) / 0.75f, 2.0f);
        speedPos += speed / (float) SR;
        float s = 0.0f;
        for (int k = 0; k < 4; ++k) { ph[k] += midiHz (notes[k]) * speed / (float) SR; s += sawOf (ph[k]) * (k == 3 ? 1.2f : 0.6f); }
        // a kick on every beat of the tape, so the slowdown is heard in the rhythm too
        const float beatPos = speedPos * (float) (bpm / 60.0);
        const float inBeat = (beatPos - std::floor (beatPos)) / (float) (bpm / 60.0);
        const float kickTone = std::sin (kTwoPi * 55.0f * speed * inBeat * 1.2f) * std::exp (-inBeat / 0.12f);
        m[i] = (lp.run (s * 0.25f, 300.0f + 3500.0f * speed, 0.9f, Svf::lp) + kickTone * 0.7f) * std::pow (speed, 0.3f);
    }
    fadeTail (m, 0.02);
    return Stereo { m, m };
}

Stereo glitch (int rootMidi, bool major, double bpm, Rng& r)
{
    const double bar = barSecs (bpm), slot = bar / 32.0;
    Stereo out { make (bar), make (bar) };
    const auto sc = scaleOf (major);
    Mono last;
    for (int k = 0; k < 32; ++k)
    {
        if (! r.chance (0.62f)) continue;
        Mono g;
        const double len = slot * r.in (0.4f, 1.0f);
        if (! last.empty() && r.chance (0.35f)) g = last;                  // stutter: the same grain again
        else if (r.chance (0.55f))
        {
            g = make (len);
            const float f = midiHz ((float) (rootMidi + 24 + sc[(size_t) (r.next() % sc.size())] + 12 * (int) (r.next() % 2)));
            const float ring = r.in (30.0f, 140.0f);
            float ph = 0.0f, rp = 0.0f;
            for (size_t i = 0; i < g.size(); ++i)
            {
                ph += f / (float) SR; rp += ring / (float) SR;
                g[i] = std::sin (kTwoPi * ph) * std::sin (kTwoPi * rp) * 0.8f;
            }
        }
        else
        {
            g = make (len);
            FixedSvf bp (r.in (800.0f, 6000.0f), 2.0f, Svf::bp);
            for (auto& s : g) s = bp.run (r.bi()) * 1.5f;
        }
        crush (g, r.in (0.3f, 0.9f));
        fadeTail (g, 0.002);
        last = g;
        addAt (out, g, (size_t) (k * slot * SR), r.in (0.6f, 1.0f), r.in (-0.7f, 0.7f));
    }
    cutTo (out, bar);
    return out;
}

Stereo bleeps (int rootMidi, bool major, double bpm, bool arcade, Rng& r)
{
    const double bar = barSecs (bpm), six = bar / 16.0;
    Stereo out { make (bar + 0.3), make (bar + 0.3) };
    const auto sc = scaleOf (major);
    for (int k = 0; k < 16; ++k)
    {
        if (! r.chance (0.7f)) continue;
        const float f = midiHz ((float) (rootMidi + 36 + sc[(size_t) (r.next() % sc.size())]));
        auto g = make (arcade ? six * 0.9 : 0.07);
        float ph = 0.0f;
        for (size_t i = 0; i < g.size(); ++i)
        {
            const float t = (float) i / (float) SR;
            const float glide = arcade ? 1.0f + 0.5f * t / (float) six : 1.0f;
            ph += f * glide / (float) SR;
            const float s = arcade ? (std::fmod (ph, 1.0f) < 0.5f ? 0.5f : -0.5f) : std::sin (kTwoPi * ph);
            g[i] = s * std::exp (-t / (arcade ? 0.08f : 0.025f));
        }
        fadeTail (g, 0.003);
        addAt (out, g, (size_t) (k * six * SR), 0.7f, k % 2 == 0 ? -0.4f : 0.4f);
    }
    return echo (out, six * 3.0, 0.35f, 3);
}

Stereo screech (int rootMidi, int v, Rng& r)
{
    const double secs = 1.4;
    auto m = make (secs);
    float ph = r.uni(), ph2 = r.uni();
    Svf bp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size(), t = (float) i / (float) SR;
        const float bend = (float) (v == 0 ? 7 : 12) * std::sin (kPi * std::min (1.0f, x * 1.4f));
        const float f = midiHz ((float) (rootMidi + 24) + bend);
        ph += f / (float) SR; ph2 += f * 1.01f / (float) SR;
        const float raw = std::tanh ((sawOf (ph) + sawOf (ph2)) * 3.0f);
        const float fc = 900.0f + 2600.0f * std::sin (kPi * x) + 400.0f * std::sin (kTwoPi * 6.0f * t);
        m[i] = std::tanh (bp.run (raw, fc, 6.0f, Svf::bp) * 3.0f) * std::min (1.0f, t / 0.01f) * (1.0f - std::pow (x, 3.0f)) * 0.7f;
    }
    return mixStereo (widen (m, 9.0f), room (m, 1.3f, 0.4f, 0.3f, 0.7), 0.5f);
}

Stereo alienVoice (int rootMidi, double bpm, bool robot, Rng& r)
{
    const double secs = robot ? barSecs (bpm) : 1.8;
    auto m = make (secs);
    float ph = r.uni(), rp = 0.0f;
    Svf f1, f2;
    const auto sc = scaleOf (false);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size(), t = (float) i / (float) SR;
        float note = (float) (rootMidi + 12) + 0.3f * std::sin (kTwoPi * 7.0f * t);
        if (robot)
        {
            const int step = (int) (t / (float) (barSecs (bpm) / 8.0));
            note = (float) (rootMidi + 12 + sc[(size_t) ((step * 3) % 7)]);
        }
        ph += midiHz (note) / (float) SR;
        const float src = sawOf (ph);
        // the vowel moves (aa, oo, ee): two formants that wander
        const float lfo = 0.5f + 0.5f * std::sin (kTwoPi * (robot ? 4.0f : 2.2f) * t);
        const float v1 = 300.0f + 500.0f * lfo, v2 = 900.0f + 1400.0f * (1.0f - lfo);
        float s = f1.run (src, v1, 6.0f, Svf::bp) + f2.run (src, v2, 8.0f, Svf::bp) * 0.7f;
        if (robot) { rp += 60.0f / (float) SR; s *= 0.6f + 0.4f * std::sin (kTwoPi * rp); }
        m[i] = std::tanh (s * 2.5f) * std::min (1.0f, t / 0.02f) * std::min (1.0f, (1.0f - x) * 20.0f) * 0.8f;
    }
    return mixStereo (widen (m, 8.0f), room (m, 1.4f, 0.4f, 0.3f, 0.8), 0.6f);
}

Stereo cowbellLine (int rootMidi, bool major, double bpm, int v, Rng& r)
{
    const double bar = barSecs (bpm), six = bar / 16.0;
    static const char* patterns[] { "x-xx-x-xx-x-xx-x", "x--x--x-x--x-x-x" };
    const char* p = patterns[v % 2];
    const auto sc = scaleOf (major);
    static const int line[] { 0, 0, 4, 3, 0, 2, 4, 0, 0, 4, 5, 4, 0, 2, 3, 0 };
    Stereo out { make (bar + 0.5), make (bar + 0.5) };
    for (int k = 0; k < 16; ++k)
    {
        if (p[k] != 'x') continue;
        const float f = midiHz ((float) (rootMidi + 48 + sc[(size_t) line[k] % sc.size()])) * 1.0f;
        Rng hr (r.next());
        auto hit = cowbell (f, hr);
        addAt (out, hit, (size_t) (k * six * SR), k % 4 == 0 ? 0.9f : 0.65f, 0.0f);
    }
    return mixStereo (out, room (toMono (out), 1.0f, 0.4f, 0.2f, 0.5), 0.5f);
}

Stereo logDrumFill (int rootMidi, bool major, double bpm, int v, Rng& r)
{
    const double bar = barSecs (bpm), six = bar / 16.0;
    const auto sc = scaleOf (major);
    static const int steps[2][8] { { 0, 3, 6, 8, 10, 11, 13, 14 }, { 2, 4, 6, 7, 10, 12, 14, 15 } };
    static const int degrees[2][8] { { 0, 0, 4, 3, 2, 0, 6, 4 }, { 4, 3, 0, 0, 2, 4, 3, 0 } };
    Stereo out { make (bar + 0.6), make (bar + 0.6) };
    for (int k = 0; k < 8; ++k)
    {
        const int deg = degrees[v % 2][k];
        const float f = midiHz ((float) (rootMidi + 24 + sc[(size_t) deg % sc.size()] - (deg >= 5 ? 12 : 0)));
        auto m = make (0.6);
        float ph = r.uni();
        Svf lp;
        for (size_t i = 0; i < m.size(); ++i)
        {
            const float t = (float) i / (float) SR;
            ph += f * (1.0f + 1.0f * std::exp (-t / 0.012f)) / (float) SR;   // the log drum "bwop": a fast pitch drop
            const float s = std::sin (kTwoPi * ph) + 0.25f * std::sin (2.0f * kTwoPi * ph) * std::exp (-t / 0.05f);
            m[i] = lp.run (sat (s, 0.6f), 1800.0f, 0.8f, Svf::lp) * std::min (1.0f, t / 0.002f) * decay (t, 0.42f);
        }
        trimSilence (m);
        addAt (out, m, (size_t) (steps[v % 2][k] * six * SR), 0.6f + 0.05f * (float) k, 0.0f);
    }
    return out;
}

Stereo percFill (double bpm, int v, Rng& r)
{
    const double bar = barSecs (bpm), six = bar / 16.0;
    Stereo out { make (bar + 0.5), make (bar + 0.5) };
    static const float tones[] { 190.0f, 280.0f, 410.0f };
    for (int k = 8; k < 16; ++k)              // the second half of the bar, getting busier
    {
        const int subs = k < 12 ? 1 : 2;
        for (int s = 0; s < subs; ++s)
        {
            DrumP p;
            const int which = (k + s + v) % 3;
            p.f = tones[which] * (1.0f + 0.02f * (float) v); p.dec = 0.18f + 0.04f * (float) (2 - which); p.slap = which == 2 ? 0.6f : 0.25f;
            p.drop = 0.12f; p.overtone = 1.6f; p.otAmt = 0.3f; p.len = 0.4f;
            Rng hr (r.next());
            auto hit = skinDrum (p, hr);
            const float vel = 0.4f + 0.6f * (float) (k - 8) / 8.0f;
            addAt (out, hit, (size_t) ((k + 0.5 * s) * six * SR), vel, which == 0 ? -0.3f : which == 1 ? 0.0f : 0.3f);
        }
    }
    return mixStereo (out, room (toMono (out), 1.0f, 0.4f, 0.2f, 0.5), 0.5f);
}

Stereo microClicks (double bpm, Rng& r)
{
    const double bar = barSecs (bpm), slot = bar / 32.0;
    Stereo out { make (bar + 0.1), make (bar + 0.1) };
    for (int k = 0; k < 32; ++k)
    {
        if (! r.chance (0.38f)) continue;
        auto g = make (0.02);
        FixedSvf bp (r.in (1800.0f, 9000.0f), 3.0f, Svf::bp);
        for (size_t i = 0; i < g.size(); ++i)
            g[i] = bp.run (i < 3 ? 1.0f : 0.0f) * 6.0f * std::exp (-(float) i / (float) SR / 0.004f);
        addAt (out, g, (size_t) (k * slot * SR), r.in (0.3f, 1.0f), r.in (-0.8f, 0.8f));
    }
    cutTo (out, bar);
    return out;
}

// ---------------------------------------------------------------------------------------------------- textures
Stereo vinylCrackle (double secs, Rng& r)
{
    Stereo s { make (secs), make (secs) };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rc (r.next());
        auto& m = ch == 0 ? s.l : s.r;
        FixedSvf hiss (5000.0f, 0.7f, Svf::lp), clickHp (1800.0f, 0.7f, Svf::hp), popLp (600.0f, 0.7f, Svf::lp), rumble (40.0f, 0.7f, Svf::lp);
        float click = 0.0f, pop = 0.0f;
        for (size_t i = 0; i < m.size(); ++i)
        {
            if (rc.chance (28.0f / (float) SR)) click = rc.in (0.2f, 1.0f) * (rc.chance (0.5f) ? 1.0f : -1.0f);
            if (rc.chance (1.5f / (float) SR)) pop = rc.in (0.5f, 1.0f);
            const float c = clickHp.run (click);
            const float p = popLp.run (pop);
            click *= 0.3f; pop *= 0.9f;
            m[i] = std::tanh ((c * 0.9f + p * 0.5f + hiss.run (rc.bi()) * 0.03f + rumble.run (rc.bi()) * 0.15f) * 4.0f);   // dense crackle, tamed pops
        }
    }
    return s;
}

Stereo tapeHiss (double secs, bool vhs, Rng& r)
{
    Stereo s { make (secs), make (secs) };
    for (int ch = 0; ch < 2; ++ch)
    {
        Rng rc (r.next());
        auto& m = ch == 0 ? s.l : s.r;
        FixedSvf hp (1800.0f, 0.7f, Svf::hp), lp (vhs ? 6500.0f : 9000.0f, 0.7f, Svf::lp);
        float hum = 0.0f, drop = 1.0f, dropTarget = 1.0f;
        for (size_t i = 0; i < m.size(); ++i)
        {
            const float t = (float) i / (float) SR;
            hum += 60.0f / (float) SR;
            const float wob = 0.85f + 0.15f * std::sin (kTwoPi * 0.4f * t + (float) ch);
            if (vhs && rc.chance (0.8f / (float) SR)) dropTarget = rc.in (0.1f, 0.5f);
            if (dropTarget < 1.0f && rc.chance (12.0f / (float) SR)) dropTarget = 1.0f;
            drop += (dropTarget - drop) * 0.002f;
            const float h = vhs ? (std::sin (kTwoPi * hum) * 0.05f + std::sin (3.0f * kTwoPi * hum) * 0.02f) : 0.0f;
            m[i] = (lp.run (hp.run (rc.bi())) * 0.35f * wob + h) * drop;
        }
    }
    return s;
}

Stereo tapeWobble (int rootMidi, bool major, double bpm, Rng& r)
{
    const double secs = barSecs (bpm) * 2.0;
    auto m = make (secs);
    const int third = major ? 4 : 3;
    const float notes[] { (float) rootMidi, (float) (rootMidi + third), (float) (rootMidi + 7), (float) (rootMidi + 10 + (major ? 1 : 0)) };
    float ph[4];
    for (auto& p : ph) p = r.uni();
    Svf lp;
    FixedSvf hiss (6000.0f, 0.7f, Svf::lp);
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        // wow (slow) and flutter (fast): the warble of a worn cassette
        const float cents = 14.0f * std::sin (kTwoPi * 0.55f * t) + 4.0f * std::sin (kTwoPi * 7.0f * t);
        const float k = std::pow (2.0f, cents / 1200.0f);
        float s = 0.0f;
        for (int n = 0; n < 4; ++n) { ph[n] += midiHz (notes[n]) * k / (float) SR; s += triOf (ph[n]); }
        const float env = std::min (1.0f, t / 0.05f) * std::min (1.0f, ((float) secs - t) / 0.2f);
        m[i] = (lp.run (s * 0.2f, 1400.0f, 0.7f, Svf::lp) + hiss.run (r.bi()) * 0.02f) * env;
    }
    return room (m, 1.2f, 0.5f, 0.25f, 0.6);
}

Stereo drone (int rootMidi, double secs, Rng& r)
{
    auto m = make (secs);
    const float notes[] { (float) (rootMidi - 12), (float) rootMidi, (float) (rootMidi + 7) };
    float ph[6];
    for (auto& p : ph) p = r.uni();
    Svf lp;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float t = (float) i / (float) SR;
        float s = 0.0f;
        for (int k = 0; k < 3; ++k)
            for (int d = 0; d < 2; ++d) { ph[k * 2 + d] += midiHz (notes[k] + (d == 0 ? -0.12f : 0.12f)) / (float) SR; s += sawOf (ph[k * 2 + d]); }
        const float fc = 300.0f + 900.0f * (0.5f + 0.5f * std::sin (kTwoPi * 0.08f * t));
        const float env = std::min (1.0f, t / 1.5f) * std::min (1.0f, ((float) secs - t) / 1.5f);
        m[i] = lp.run (s * 0.12f, fc, 1.3f, Svf::lp) * env;
    }
    return room (m, 2.2f, 0.3f, 0.6f, 1.0);
}

Stereo radioStatic (double secs, Rng& r)
{
    auto m = make (secs);
    FixedSvf bp (1800.0f, 0.7f, Svf::bp);
    float ph = 0.0f, crackle = 1.0f;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float x = (float) i / (float) m.size();
        if (r.chance (40.0f / (float) SR)) crackle = r.in (0.2f, 1.6f);
        crackle += (1.0f - crackle) * 0.0008f;
        // the dial being turned: a whistle that sweeps between stations
        const float f = 900.0f + 1500.0f * (0.5f + 0.5f * std::sin (kTwoPi * (0.3f * x * (float) secs)));
        ph += f / (float) SR;
        const float whistle = std::sin (kTwoPi * ph) * 0.12f * std::pow (std::abs (std::sin (kPi * x * 3.0f)), 4.0f);
        m[i] = bp.run (r.bi()) * 0.6f * crackle + whistle;
    }
    return widen (m, 4.0f);
}

Stereo granular (int rootMidi, bool major, double secs, Rng& r)
{
    Stereo out { make (secs), make (secs) };
    const auto sc = scaleOf (major);
    const int grains = (int) (secs * 18.0);
    for (int g = 0; g < grains; ++g)
    {
        const double len = r.in (0.06f, 0.22f);
        auto m = make (len);
        const float f = midiHz ((float) (rootMidi + 24 + sc[(size_t) (r.next() % sc.size())] + 12 * (int) (r.next() % 2)));
        float ph = r.uni();
        for (size_t i = 0; i < m.size(); ++i)
        {
            ph += f / (float) SR;
            const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) m.size());
            m[i] = std::sin (kTwoPi * ph) * w;
        }
        addAt (out, m, (size_t) (r.uni() * (secs - len) * SR), r.in (0.1f, 0.35f), r.in (-0.9f, 0.9f));
    }
    cutTo (out, secs);
    return mixStereo (out, room (toMono (out), 2.2f, 0.3f, 0.8f, 1.5), 0.8f);
}

Stereo rumble (double bpm, Rng& r)
{
    const double bar = barSecs (bpm);
    Mono kicks = make (bar);
    KickP p; p.f0 = 220; p.f1 = 48; p.dec = 0.3f; p.drive = 0.6f; p.len = 0.5f;
    for (int b = 0; b < 4; ++b) { Rng hr (r.next()); addAt (kicks, kick (p, hr), (size_t) (b * bar / 4.0 * SR), 1.0f); }
    auto wet = room (kicks, 2.4f, 0.2f, 1.0f, bar);
    Svf l1, l2;
    for (size_t i = 0; i < wet.l.size(); ++i)
    {
        wet.l[i] = sat (l1.run (wet.l[i], 170.0f, 0.9f, Svf::lp) * 2.0f, 0.6f);
        wet.r[i] = sat (l2.run (wet.r[i], 170.0f, 0.9f, Svf::lp) * 2.0f, 0.6f);
    }
    for (size_t i = 0; i < kicks.size() && i < wet.l.size(); ++i) { wet.l[i] += kicks[i] * 0.5f; wet.r[i] += kicks[i] * 0.5f; }
    return wet;
}

// ---------------------------------------------------------------------------------------------------- the catalogue
enum Code { RISER, UPLIFT, DOWN, ROLL_CLAP, ROLL_SNARE, ROLL_HAT, ROLL_KICK, ROLL_HARDKICK, ROLL_SHAKER, ROLL_BREAK, REVCYM, REVSWELL,
            SWEEP, WASH, IMPACT, METAL, SUBDROP, SLIDE808, LASER, LASERDISCO, LASERRETRO, SIREN, DUBSIREN, AIRHORN, REWIND, TAPESTOP,
            GLITCH, BLEEPS, ARCADE, SCREECH, ALIEN, ROBOT, COWBELL, LOGDRUM, PERCFILL, CLICKS, VINYL, HISS, VHS, WOBBLE, DRONE, STATIC,
            GRAIN, RUMBLE };

struct Recipe { const char* genre; std::vector<Code> codes; };

const std::vector<Recipe>& recipes()
{
    static const std::vector<Recipe> r {
        { "house",         { ROLL_CLAP, RISER, REVCYM, SWEEP, IMPACT, DOWN } },
        { "deephouse",     { SWEEP, REVCYM, WASH, VINYL, REVSWELL, DOWN } },
        { "techhouse",     { ROLL_CLAP, ROLL_HAT, RISER, DOWN, SUBDROP, GLITCH, TAPESTOP } },
        { "afrohouse",     { ROLL_SHAKER, PERCFILL, RISER, REVCYM, SWEEP, DOWN } },
        { "organichouse",  { ROLL_SHAKER, PERCFILL, REVSWELL, GRAIN, WASH, DRONE } },
        { "progressive",   { RISER, UPLIFT, ROLL_SNARE, REVCYM, IMPACT, DOWN, SUBDROP } },
        { "nudisco",       { ROLL_CLAP, SWEEP, LASERDISCO, REVCYM, RISER, VINYL } },
        { "indiedance",    { TAPESTOP, SWEEP, RISER, GLITCH, REVCYM, DOWN } },
        { "melodictechno", { RISER, DOWN, IMPACT, SUBDROP, REVSWELL, DRONE, STATIC } },
        { "techno",        { SWEEP, RISER, REVCYM, GLITCH, METAL, RUMBLE, STATIC } },
        { "hardtechno",    { SIREN, RISER, ROLL_HARDKICK, IMPACT, REVCYM, METAL } },
        { "minimal",       { CLICKS, GLITCH, SWEEP, REVSWELL, STATIC, VINYL } },
        { "acid",          { ROLL_CLAP, SWEEP, SIREN, LASER, RISER, DOWN } },
        { "trance",        { RISER, UPLIFT, ROLL_SNARE, DOWN, IMPACT, REVCYM, SUBDROP } },
        { "psytrance",     { LASER, BLEEPS, ALIEN, RISER, DOWN, GLITCH, SUBDROP } },
        { "dnb",           { RISER, SUBDROP, IMPACT, REWIND, DUBSIREN, REVCYM, LASER } },
        { "liquid",        { REVCYM, WASH, RISER, REVSWELL, DOWN, VINYL } },
        { "jungle",        { REWIND, DUBSIREN, ROLL_BREAK, AIRHORN, IMPACT, VINYL } },
        { "dubstep",       { RISER, SUBDROP, IMPACT, LASER, SIREN, GLITCH, DOWN } },
        { "futurebass",    { RISER, UPLIFT, ROLL_SNARE, REVCYM, GLITCH, IMPACT } },
        { "trap",          { ROLL_HAT, ROLL_SNARE, RISER, SLIDE808, TAPESTOP, METAL, IMPACT } },
        { "drill",         { ROLL_HAT, SLIDE808, REVSWELL, METAL, RISER, DOWN } },
        { "phonk",         { COWBELL, SLIDE808, TAPESTOP, SUBDROP, RISER, VINYL } },
        { "reggaeton",     { SIREN, AIRHORN, ROLL_SNARE, REWIND, SWEEP, IMPACT, RISER } },
        { "dembow",        { SIREN, AIRHORN, ROLL_SNARE, LASER, IMPACT, METAL } },
        { "moombahton",    { SIREN, AIRHORN, LASER, ROLL_SNARE, RISER, DOWN } },
        { "cumbia",        { LASER, SIREN, PERCFILL, SWEEP, REVCYM, IMPACT } },
        { "afrobeats",     { PERCFILL, ROLL_SHAKER, SWEEP, REVCYM, RISER, DOWN } },
        { "amapiano",      { LOGDRUM, ROLL_SHAKER, PERCFILL, SWEEP, REVCYM, DOWN } },
        { "dancehall",     { AIRHORN, REWIND, DUBSIREN, LASER, METAL, IMPACT } },
        { "hiphop",        { REWIND, TAPESTOP, ROLL_BREAK, VINYL, IMPACT, REVCYM } },
        { "lofi",          { WOBBLE, TAPESTOP, HISS, VINYL, REVSWELL, STATIC } },
        { "rnb",           { REVCYM, REVSWELL, RISER, SUBDROP, DOWN, VINYL } },
        { "jerseyclub",    { ROLL_KICK, ROLL_SNARE, SIREN, METAL, LASER, RISER } },
        { "ukgarage",      { REWIND, DUBSIREN, REVCYM, SUBDROP, SWEEP, RISER } },
        { "breakbeat",     { ROLL_BREAK, REWIND, SIREN, RISER, DOWN, IMPACT } },
        { "hardstyle",     { SCREECH, ROLL_HARDKICK, RISER, IMPACT, REVCYM, DOWN } },
        { "electro",       { LASER, ROBOT, BLEEPS, SWEEP, GLITCH, RISER } },
        { "synthwave",     { LASERRETRO, ARCADE, RISER, IMPACT, REVCYM, WOBBLE, VHS } },
        { "disco",         { ROLL_CLAP, SWEEP, LASERDISCO, SIREN, REVCYM, RISER } },
        { "funk",          { ROLL_SNARE, REVCYM, SWEEP, TAPESTOP, IMPACT, VINYL } },
        { "pop",           { RISER, REVCYM, ROLL_SNARE, IMPACT, SUBDROP, TAPESTOP, DOWN } },
        { "downtempo",     { WOBBLE, REVSWELL, WASH, DRONE, STATIC, VINYL } },
        { "ambient",       { DRONE, GRAIN, REVSWELL, WASH, HISS, STATIC } },
    };
    return r;
}

const char* folderOf (Code c)
{
    switch (c)
    {
        case RISER: case UPLIFT: return "Risers";
        case DOWN: return "Downlifters";
        case ROLL_CLAP: case ROLL_SNARE: case ROLL_HAT: case ROLL_KICK: case ROLL_HARDKICK: case ROLL_SHAKER: case ROLL_BREAK: return "Rollers";
        case REVCYM: case REVSWELL: return "Reverse";
        case SWEEP: case WASH: return "Sweeps";
        case IMPACT: case METAL: return "Impactos";
        case SUBDROP: case SLIDE808: return "Sub drops";
        case PERCFILL: case LOGDRUM: return "Fills";
        case VINYL: case HISS: case VHS: case WOBBLE: case DRONE: case STATIC: case GRAIN: case RUMBLE: return "Texturas";
        default: return "Incidentales";
    }
}

int variantsOf (Code c)
{
    switch (c)
    {
        case VINYL: case HISS: case VHS: case DRONE: case STATIC: case GRAIN: case RUMBLE: case WOBBLE: return 1;
        default: return 2;
    }
}

String bpmText (double bpm) { return String (roundToInt (bpm)) + " BPM"; }
String barsText (int n) { return n == 1 ? String::fromUTF8 (" compás") : String (" compases"); }

struct FxSound { String name; std::function<Stereo (Rng&)> make; };

// the name and the sound of one effect of a genre (v = 0 or 1, the two takes)
FxSound effectOf (Code c, int v, const Genre& g)
{
    const double bpm = g.bpm, bar = barSecs (bpm);
    const bool maj = g.major;
    // the effect's root: low (octave 1-2) for subs, middle (octave 3) for chords and tones
    const int low = 24 + g.key + (g.key > 5 ? 0 : 12), mid = 48 + g.key;
    const String key = String::fromUTF8 (noteName (g.key));
    const String t = bpmText (bpm);
    const int bars = v == 0 ? 4 : 8;
    switch (c)
    {
        case RISER:       return { "Riser de ruido " + String (bars) + " compases (" + t + ")", [=] (Rng& r) { return riser ((float) (bar * bars), 250.0f + 60.0f * (float) v, 9000.0f, false, r); } };
        case UPLIFT:      return { "Uplifter en " + key + " " + String (bars) + " compases (" + t + ")", [=] (Rng& r) { return uplifter (bar * bars, mid, maj, r); } };
        case DOWN:        return { "Downlifter " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return riser ((float) (bar * (v + 1)), 200.0f, 8000.0f, true, r); } };
        case ROLL_CLAP:   return { "Roller de clap " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::clap, v + 1, bpm, r); } };
        case ROLL_SNARE:  return { "Roller de snare " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::snare, v + 1, bpm, r); } };
        case ROLL_HAT:    return { "Roller de hi-hat " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::hat, v + 1, bpm, r); } };
        case ROLL_KICK:   return { "Roller de kick " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::kick, v + 1, bpm, r); } };
        case ROLL_HARDKICK: return { "Roller de kick duro " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::hardKick, v + 1, bpm, r); } };
        case ROLL_SHAKER: return { "Roller de shaker " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::shaker, v + 1, bpm, r); } };
        case ROLL_BREAK:  return { "Roller de break " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return roller (RollKind::breaks, v + 1, bpm, r); } };
        case REVCYM:      return { String::fromUTF8 ("Platillo al revés ") + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return reverseCymbal (bar * (v + 1), v, r); } };
        case REVSWELL:    return { String::fromUTF8 ("Swell al revés en ") + key + " " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return reverseSwell (mid, maj, bar * (v + 1), r); } };
        case SWEEP:       return { String (v == 0 ? "Filter sweep de subida " : "Filter sweep sube y baja ") + "2 compases (" + t + ")", [=] (Rng& r) { return filterSweep (bar * 2.0, v == 0 ? 0 : 2, r); } };
        case WASH:        return { "Ola de ruido blanco " + String (v + 1) + barsText (v + 1) + " (" + t + ")", [=] (Rng& r) { return noiseWash (bar * (v + 1), r); } };
        case IMPACT:      return { v == 0 ? String ("Impacto") : String::fromUTF8 ("Impacto cinemático"), [=] (Rng& r) { return v == 0 ? impact (r) : bigImpact (r); } };
        case METAL:       return { String::fromUTF8 ("Golpe metálico ") + String (v + 1), [=] (Rng& r) { return metalHit (v, r); } };
        case SUBDROP:     return { "Sub drop en " + key + " " + String (v + 1), [=] (Rng& r) { return subDrop (low, v == 0 ? 1.5 : 2.5, r); } };
        case SLIDE808:    return { "Slide de 808 en " + key + (v == 0 ? " (de la octava)" : " (de la quinta)"), [=] (Rng& r) { return slide808 (low, v == 0 ? 12 : 7, r); } };
        case LASER:       return { String::fromUTF8 ("Laser zap ") + String (v + 1), [=] (Rng& r) { return laser (0, v, bpm, r); } };
        case LASERDISCO:  return { String::fromUTF8 ("Láser disco ") + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return laser (1, v, bpm, r); } };
        case LASERRETRO:  return { String::fromUTF8 ("Láser retro ") + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return laser (2, v, bpm, r); } };
        case SIREN:       return { "Sirena " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return siren (false, bpm, v, r); } };
        case DUBSIREN:    return { "Sirena dub " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return siren (true, bpm, v, r); } };
        case AIRHORN:     return { "Air horn sintetizado " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return airHorn (bpm, v, r); } };
        case REWIND:      return { "Rewind " + String (v + 1), [=] (Rng& r) { return rewind (v, r); } };
        case TAPESTOP:    return { "Tape stop en " + key + " " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return tapeStop (mid - (v == 1 ? 12 : 0), maj, bpm, r); } };
        case GLITCH:      return { "Glitch " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return glitch (mid, maj, bpm, r); } };
        case BLEEPS:      return { "Bleeps " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return bleeps (mid, maj, bpm, false, r); } };
        case ARCADE:      return { "Bleeps arcade " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return bleeps (mid, maj, bpm, true, r); } };
        case SCREECH:     return { "Screech en " + key + " " + String (v + 1), [=] (Rng& r) { return screech (mid - 12, v, r); } };
        case ALIEN:       return { "Voz alien " + String (v + 1), [=] (Rng& r) { return alienVoice (mid - 12 + 7 * v, bpm, false, r); } };
        case ROBOT:       return { "Voz robot " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return alienVoice (mid - 12 + 5 * v, bpm, true, r); } };
        case COWBELL:     return { "Cowbell en " + key + " " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return cowbellLine (g.key, maj, bpm, v, r); } };
        case LOGDRUM:     return { "Log drum fill en " + key + " " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return logDrumFill (12 + g.key, maj, bpm, v, r); } };
        case PERCFILL:    return { String::fromUTF8 ("Fill de percusión ") + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return percFill (bpm, v, r); } };
        case CLICKS:      return { "Micro clicks " + String (v + 1) + " (" + t + ")", [=] (Rng& r) { return microClicks (bpm, r); } };
        case VINYL:       return { "Vinilo (crujido)", [=] (Rng& r) { return vinylCrackle (8.0, r); } };
        case HISS:        return { "Hiss de cassette", [=] (Rng& r) { return tapeHiss (8.0, false, r); } };
        case VHS:         return { "Ruido VHS", [=] (Rng& r) { return tapeHiss (8.0, true, r); } };
        case WOBBLE:      return { "Tape wobble en " + key + " (" + t + ")", [=] (Rng& r) { return tapeWobble (mid, maj, bpm, r); } };
        case DRONE:       return { "Drone en " + key, [=] (Rng& r) { return drone (mid - 12, 10.0, r); } };
        case STATIC:      return { String::fromUTF8 ("Estática de radio"), [=] (Rng& r) { return radioStatic (5.0, r); } };
        case GRAIN:       return { "Textura granular en " + key, [=] (Rng& r) { return granular (mid, maj, 8.0, r); } };
        case RUMBLE:      return { "Rumble (" + t + ")", [=] (Rng& r) { return rumble (bpm, r); } };
    }
    return {};
}

bool writeFlac (const File& f, const Stereo& s)
{
    f.getParentDirectory().createDirectory();
    TemporaryFile tmp (f);
    {
        FlacAudioFormat flac;
        auto* os = tmp.getFile().createOutputStream().release();
        if (os == nullptr) return false;
        std::unique_ptr<AudioFormatWriter> w (flac.createWriterFor (os, SR, 2u, 16, {}, 5));
        if (w == nullptr) { delete os; return false; }
        const float* chans[2] { s.l.data(), s.r.data() };
        if (! w->writeFromFloatArrays (chans, 2, (int) std::min (s.l.size(), s.r.size()))) return false;
    }
    return tmp.overwriteTargetFileWithTemporary();
}

std::vector<Job> effectJobs()
{
    std::vector<Job> list;
    for (auto& rec : recipes())
    {
        const auto* g = findGenre (rec.genre);
        if (g == nullptr) continue;
        const String folder = File::createLegalFileName (String::fromUTF8 (g->name));
        uint32_t seed = 0x6e5e0000u;
        for (auto ch : String (rec.genre)) seed = seed * 31u + (uint32_t) ch;
        for (auto c : rec.codes)
            for (int v = 0; v < variantsOf (c); ++v)
            {
                auto fx = effectOf (c, v, *g);
                const String path = folder + "/" + folderOf (c) + "/" + File::createLegalFileName (fx.name) + ".flac";
                const uint32_t s = seed + (uint32_t) c * 977u + (uint32_t) v * 13u;
                list.push_back ({ path, [fx, s] (const File& f)
                {
                    Rng r (s);
                    auto st = fx.make (r);
                    // a few sounds can come out longer on one side (the widening delay): same length, then level
                    const size_t n = std::max (st.l.size(), st.r.size());
                    st.l.resize (n, 0.0f); st.r.resize (n, 0.0f);
                    // no offset (distorted horns and voices lean to one side): a gentle high-pass at 20 Hz
                    for (auto* ch : { &st.l, &st.r })
                    {
                        float x1 = 0.0f, y1 = 0.0f;
                        const float a = 1.0f - kTwoPi * 20.0f / (float) SR;
                        for (auto& v : *ch) { const float y = v - x1 + a * y1; x1 = v; y1 = y; v = y; }
                    }
                    normalise (st, 0.89f);
                    writeFlac (f, st);
                } });
            }
    }
    return list;
}
} // namespace gfx

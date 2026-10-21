#include "PreviewSound.h"

namespace racoon
{
using namespace juce;

namespace
{
constexpr float twoPi = MathConstants<float>::twoPi;
inline float onePole (float& state, float x, float coef) { state += coef * (x - state); return state; }
inline float coefFor (float hz, double sr) { return 1.0f - std::exp (-twoPi * jlimit (10.0f, (float) (sr * 0.45), hz) / (float) sr); }

// band-limited oscillators (PolyBLEP): the naive ramps folded their upper harmonics back as a whistle on high notes
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float blepSaw (float ph, float dt) { return 2.0f * ph - 1.0f - polyBlep (ph, dt); }
inline float blepSquare (float ph, float dt)
{
    float s = ph < 0.5f ? 1.0f : -1.0f;
    s += polyBlep (ph, dt);
    float t2 = ph + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
    return s - polyBlep (t2, dt);
}
inline void advance (float& ph, float inc) { ph += inc; if (ph >= 1.0f) ph -= 1.0f; }

// state-variable filter in the trapezoidal (TPT) form: stable when the cutoff moves every sample, resonance k = 2 - 2 res
struct SvfOut { float lp, bp, hp; };
inline SvfOut svfTick (float* st, float x, float g, float k)
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - st[1];
    const float v1 = a1 * st[0] + a2 * v3;
    const float v2 = st[1] + a2 * st[0] + a3 * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    return { v2, v1, x - k * v1 - v2 };
}
inline float gFor (float hz, double sr) { return std::tan (MathConstants<float>::pi * jlimit (20.0f, (float) (sr * 0.45), hz) / (float) sr); }
float midiHz (int note) { return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f); }

struct Env { float a, d, s, r; };
Env envFor (PreviewSound::Patch p)
{
    switch (p)
    {
        case PreviewSound::Patch::pad:   return { 0.25f, 1.2f, 0.75f, 0.9f };
        case PreviewSound::Patch::keys:  return { 0.004f, 0.7f, 0.35f, 0.35f };
        case PreviewSound::Patch::pluck: return { 0.002f, 0.28f, 0.0f, 0.18f };
        case PreviewSound::Patch::bass:  return { 0.003f, 0.35f, 0.65f, 0.08f };
    }
    return { 0.01f, 0.5f, 0.5f, 0.3f };
}

// formants (Hz) of five sung vowels: a, e, i, o, u
const float formants[5][3] { { 800, 1150, 2900 }, { 450, 1900, 2600 }, { 320, 2300, 3000 }, { 500, 850, 2800 }, { 340, 720, 2500 } };
}

void PreviewSound::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    reset();
}

void PreviewSound::reset()
{
    for (auto& v : voices) v = Voice();
    beatOn = false;
}

bool PreviewSound::isSounding() const
{
    for (auto& v : voices) if (v.active) return true;
    return beatOn;
}

void PreviewSound::allOff()
{
    for (auto& v : voices) if (v.active && ! v.drum) v.released = true;
}

void PreviewSound::noteOn (int channel, int note, float velocity)
{
    if (kind == Kind::drums || (kind == Kind::mix && channel == 10)) { startDrum (note, velocity); return; }
    // a free voice, or the oldest one
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& x : voices) if (x.age > v->age) v = &x;
    }
    *v = Voice();
    v->active = true;
    v->note = note;
    v->channel = channel;
    v->vel = velocity;
    v->freq = midiHz (note);
    v->rng = 0x9e3779b9u ^ (juce::uint32) (note * 2654435761u);
    for (int k = 0; k < 3; ++k) v->phase[k] = (float) k * 0.31f;   // the unison copies start apart
    // in the mix, the low notes are the bass and the rest are keys
    v->drumType = kind == Kind::mix ? (note < 48 ? (int) Patch::bass : (int) Patch::keys) : (int) patch;
}

void PreviewSound::noteOff (int channel, int note)
{
    for (auto& v : voices)
        if (v.active && ! v.drum && v.note == note && (v.channel == channel || kind != Kind::mix)) v.released = true;
}

void PreviewSound::startDrum (int gm, float velocity)
{
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.age > v->age) v = &x; }
    *v = Voice();
    v->active = true;
    v->drum = true;
    v->note = gm;
    v->vel = velocity;
    v->rng = 0x51ed27u ^ (juce::uint32) (gm * 7919u);
    // which kind of drum this GM note is
    int t = 12;
    switch (gm)
    {
        case 35: case 36: t = 0; break;                  // kick
        case 38: case 40: t = 1; break;                  // snare
        case 39: t = 2; break;                           // clap
        case 37: t = 3; break;                           // rim
        case 42: case 44: t = 4; break;                  // closed hat
        case 46: t = 5; break;                           // open hat
        case 51: case 53: case 59: t = 6; break;         // ride
        case 49: case 52: case 55: case 57: t = 7; break;// crash
        case 41: case 43: case 45: case 47: case 48: case 50: t = 8; break;   // toms
        case 69: case 70: case 82: t = 9; break;         // shaker
        case 60: case 61: case 62: case 63: case 64: t = 10; break;           // bongos and congas
        case 56: t = 11; break;                          // cowbell
        case 54: t = 13; break;                          // tambourine
        default: t = 12; break;                          // claves, woodblock, metal
    }
    v->drumType = t;
    // a small stereo picture, like a kit: hats and shaker a little right, ride and crash a little left, toms across
    switch (t)
    {
        case 4: case 5: v->pan = 0.22f; break;
        case 6: v->pan = -0.3f; break;
        case 7: v->pan = -0.15f; break;
        case 9: v->pan = 0.35f; break;
        case 13: v->pan = 0.3f; break;
        case 8: v->pan = jlimit (-0.35f, 0.35f, 0.35f - (float) (gm - 41) * 0.08f); break;
        case 10: v->pan = (gm % 2 == 0) ? -0.2f : 0.2f; break;
        default: v->pan = 0.0f; break;
    }
    if (t == 4 || t == 5 || t == 6 || t == 7 || t == 13)   // the metal oscillators start out of step, so no two hits are the same
        for (int k = 0; k < 6; ++k) v->phase[k] = (float) ((v->rng >> (k * 4)) & 15u) / 16.0f;
    if (t == 0) v->freq = tunedHz (48.0f, true);
    if (t == 8) v->freq = tunedHz (70.0f + (float) (gm - 41) * 14.0f, false);
    if (t == 10) v->freq = tunedHz (180.0f + (float) (gm - 60) * 55.0f, false);
}

float PreviewSound::tunedHz (float hz, bool rootOrFifth) const
{
    if (drumKey < 0) return hz;
    static const int minor[] { 0, 2, 3, 5, 7, 8, 10 }, major[] { 0, 2, 4, 5, 7, 9, 11 };
    const float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    float best = m, bestD = 99.0f;
    for (int n = (int) m - 7; n <= (int) m + 7; ++n)
    {
        const int pc = ((n - drumKey) % 12 + 12) % 12;
        bool ok = false;
        if (rootOrFifth) ok = pc == 0 || pc == 7;
        else for (int k = 0; k < 7; ++k) ok |= pc == (drumMajor ? major[k] : minor[k]);
        if (ok && std::abs ((float) n - m) < bestD) { bestD = std::abs ((float) n - m); best = (float) n; }
    }
    return 440.0f * std::pow (2.0f, (best - 69.0f) / 12.0f);
}

void PreviewSound::setBeat (bool on, double bpm, double beatPosition)
{
    if (on && ! beatOn) beatPos = beatPosition;
    beatOn = on;
    beatBpm = jlimit (40.0, 240.0, bpm);
    if (on && std::abs (beatPosition - beatPos) > 0.05) beatPos = beatPosition;
}

float PreviewSound::renderDrum (Voice& v)
{
    const float t = v.age;
    float out = 0.0f;
    const float n = noise (v.rng);
    switch (v.drumType)
    {
        case 0:   // kick: a sine that falls from about three times its pitch, a short band-passed click, a soft clip
        {
            if (t == 0.0f) v.phase[0] = 0.0f;   // always starts at the zero crossing
            const float f = v.freq * (1.0f + 2.4f * std::exp (-t / 0.028f));
            advance (v.phase[0], f / (float) sr);
            const float body = std::sin (twoPi * v.phase[0]) * std::exp (-t / 0.22f);
            const float click = svfTick (v.svf[0], n, gFor (3500.0f, sr), 1.0f).bp * std::exp (-t / 0.0025f) * 1.4f;
            out = std::tanh ((body + click) * 1.35f) * 0.95f;
            if (t > 0.9f) v.active = false;
            break;
        }
        case 1:   // snare: two drum-head modes and a filtered rattle
        {
            const float bend = 1.0f + 0.35f * std::exp (-t / 0.008f);
            advance (v.phase[0], 185.0f * bend / (float) sr);
            advance (v.phase[1], 332.0f * bend / (float) sr);
            const float body = (0.62f * std::sin (twoPi * v.phase[0]) + 0.34f * std::sin (twoPi * v.phase[1])) * std::exp (-t / 0.07f);
            float rattle = svfTick (v.svf[0], n, gFor (1900.0f, sr), 1.2f).hp;
            rattle = onePole (v.lp1, rattle, coefFor (9500.0f, sr));
            out = body * 0.75f + rattle * std::exp (-t / 0.12f) * 0.95f;
            if (t > 0.6f) v.active = false;
            break;
        }
        case 2:   // clap: three bursts and a tail
        {
            float e = 0.0f;
            for (int k = 0; k < 3; ++k) { const float tk = t - 0.011f * (float) k; if (tk >= 0.0f) e = std::max (e, std::exp (-tk / 0.004f)); }
            e = std::max (e, t > 0.022f ? 0.7f * std::exp (-(t - 0.022f) / 0.09f) : 0.0f);
            const float lo = onePole (v.lp1, n, coefFor (2200.0f, sr));
            out = (lo - onePole (v.lp2, lo, coefFor (700.0f, sr))) * e * 2.2f;
            if (t > 0.5f) v.active = false;
            break;
        }
        case 3:   // rim
        {
            v.phase[0] += 480.0f / (float) sr;
            out = (std::sin (twoPi * v.phase[0]) + n * 0.4f) * std::exp (-t / 0.012f);
            if (t > 0.1f) v.active = false;
            break;
        }
        case 4: case 5: case 6: case 7: case 13:   // hats, ride, crash, tambourine: six detuned squares like an 808, then filtered
        {
            static const float ratios[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
            const float scale = v.drumType == 6 ? 1.18f : v.drumType == 7 ? 0.92f : 1.0f;
            float metal = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                const float dt = ratios[k] * scale / (float) sr;
                metal += blepSquare (v.phase[k], dt);
                advance (v.phase[k], dt);
            }
            metal *= 1.0f / 6.0f;
            const float noiseMix = v.drumType == 13 ? 0.7f : v.drumType == 7 ? 0.45f : 0.2f;
            const float src = metal * (1.0f - noiseMix) + n * noiseMix;
            const float bp = svfTick (v.svf[0], src, gFor (v.drumType == 6 ? 8500.0f : 10500.0f, sr), 1.3f).bp;
            const float hp = svfTick (v.svf[1], bp, gFor (v.drumType == 6 ? 4500.0f : 7000.0f, sr), 1.4f).hp;
            const float dec = v.drumType == 4 ? 0.04f : v.drumType == 5 ? 0.2f : v.drumType == 6 ? 0.55f : v.drumType == 7 ? 0.9f : 0.09f;
            // a closed hat closes fast; the others ring
            const float e = std::exp (-t / dec) * (v.drumType == 4 ? 1.0f : std::min (1.0f, t / 0.0008f));
            out = hp * e * (v.drumType == 6 ? 1.6f : 2.2f);
            if (t > dec * 7.0f) v.active = false;
            break;
        }
        case 8: case 10:   // toms, congas, bongos
        {
            const float f = v.freq * (1.0f + 0.25f * std::exp (-t / 0.02f));
            v.phase[0] += f / (float) sr;
            const float dec = v.drumType == 8 ? 0.22f : 0.12f;
            out = std::sin (twoPi * v.phase[0]) * std::exp (-t / dec) + n * 0.08f * std::exp (-t / 0.006f);
            if (t > dec * 7.0f) v.active = false;
            break;
        }
        case 9:   // shaker
        {
            const float bp = onePole (v.lp1, n, coefFor (9000.0f, sr));
            const float hp = bp - onePole (v.lp2, bp, coefFor (5000.0f, sr));
            const float e = t < 0.012f ? t / 0.012f : std::exp (-(t - 0.012f) / 0.04f);
            out = hp * e * 1.4f;
            if (t > 0.3f) v.active = false;
            break;
        }
        case 11:  // cowbell
        {
            v.phase[0] += 560.0f / (float) sr; v.phase[1] += 845.0f / (float) sr;
            v.phase[0] -= std::floor (v.phase[0]); v.phase[1] -= std::floor (v.phase[1]);
            const float sq = (v.phase[0] < 0.5f ? 1.0f : -1.0f) + (v.phase[1] < 0.5f ? 1.0f : -1.0f);
            out = onePole (v.lp1, sq, coefFor (2500.0f, sr)) * 0.35f * (0.6f * std::exp (-t / 0.01f) + 0.4f * std::exp (-t / 0.12f));
            if (t > 0.6f) v.active = false;
            break;
        }
        default:  // claves and metal
        {
            v.phase[0] += 2500.0f / (float) sr;
            out = std::sin (twoPi * v.phase[0]) * std::exp (-t / 0.02f) * 0.6f;
            if (t > 0.15f) v.active = false;
            break;
        }
    }
    return out * v.vel;
}

void PreviewSound::renderVoice (Voice& v, float& l, float& r)
{
    const float t = v.age;
    if (kind == Kind::voice)
    {
        // a sung vowel: a soft pulse through three formant resonators, with vibrato after a moment
        const float vib = 1.0f + 0.005f * std::sin (vibPhase) * jlimit (0.0f, 1.0f, (t - 0.25f) * 3.0f);
        v.phase[0] += v.freq * vib / (float) sr;
        if (v.phase[0] >= 1.0f) v.phase[0] -= 1.0f;
        const float pulse = onePole (v.lp1, 2.0f * v.phase[0] - 1.0f, coefFor (2400.0f, sr)) + noise (v.rng) * 0.02f;
        const auto* f = formants[(v.note % 5 + 5) % 5];
        const float gains[3] { 1.0f, 0.55f, 0.25f };
        float y = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float w = twoPi * f[k] / (float) sr;
            const float r = std::exp (-MathConstants<float>::pi * (60.0f + f[k] * 0.06f) / (float) sr);
            const float out = (1.0f - r) * pulse + 2.0f * r * std::cos (w) * v.bp[k][0] - r * r * v.bp[k][1];
            v.bp[k][1] = v.bp[k][0];
            v.bp[k][0] = out;
            y += out * gains[k];
        }
        const float a = std::min (1.0f, t / 0.08f);
        if (! v.released) v.env = a;
        else { v.env *= std::exp (-1.0f / (0.25f * (float) sr)); if (v.env < 1.0e-4f) v.active = false; }
        l = r = y * 0.055f * v.env * v.vel;
        return;
    }

    const auto p = (Patch) v.drumType;
    const auto e = envFor (p);
    if (! v.released) v.env = t < e.a ? t / e.a : e.s + (1.0f - e.s) * std::exp (-(t - e.a) / std::max (0.01f, e.d * 0.3f));
    else { v.env *= std::exp (-1.0f / (e.r * 0.3f * (float) sr)); if (v.env < 1.0e-4f) v.active = false; }
    if (! v.released && e.s <= 0.0f && t > e.a && v.env < 1.0e-4f) v.active = false;   // after the attack, not on its first sample

    // oscillators: band-limited, a few detuned copies spread left and right
    const float fs = (float) sr;
    const float vib = p == Patch::pad ? 1.0f + 0.0025f * std::sin (vibPhase * 0.17f + (float) v.note) : 1.0f;
    const float f = v.freq * vib;
    float a = 0.0f, b = 0.0f;
    switch (p)
    {
        case Patch::pad:
        {
            const float d0 = f * 1.0071f / fs, d1 = f / fs, d2 = f * 0.9931f / fs;
            const float s0 = blepSaw (v.phase[0], d0), s1 = blepSaw (v.phase[1], d1), s2 = blepSaw (v.phase[2], d2);
            advance (v.phase[0], d0); advance (v.phase[1], d1); advance (v.phase[2], d2);
            a = s0 + 0.6f * s1; b = s2 + 0.6f * s1;
            break;
        }
        case Patch::keys:
        {
            // a saw and a square, and a bell at twice the pitch that dies fast (the tine of an electric piano)
            const float d0 = f * 1.0025f / fs, d1 = f * 0.9975f / fs, d2 = 2.0f * f / fs;
            const float bell = std::sin (twoPi * v.phase[2]) * std::exp (-t / 0.22f) * 0.9f;
            a = blepSaw (v.phase[0], d0) + bell;
            b = 0.7f * blepSquare (v.phase[1], d1) + bell;
            advance (v.phase[0], d0); advance (v.phase[1], d1); advance (v.phase[2], d2);
            break;
        }
        case Patch::pluck:
        {
            const float d0 = f * 1.003f / fs, d1 = f * 0.997f / fs;
            const float pick = noise (v.rng) * std::exp (-t / 0.004f) * 0.8f;
            a = blepSaw (v.phase[0], d0) + pick;
            b = blepSaw (v.phase[1], d1) + pick;
            advance (v.phase[0], d0); advance (v.phase[1], d1);
            break;
        }
        case Patch::bass:
        {
            // one saw and a sine an octave down, the same on both sides
            const float d0 = f / fs, d1 = 0.5f * f / fs;
            a = b = 0.7f * blepSaw (v.phase[0], d0) + 1.1f * std::sin (twoPi * v.phase[1]);
            advance (v.phase[0], d0); advance (v.phase[1], d1);
            break;
        }
    }

    // filter: a 12 dB state-variable low-pass that follows the key and the velocity
    float cut = 0.0f, res = 0.0f;
    switch (p)
    {
        case Patch::pad:   cut = 900.0f + 1600.0f * v.env;                       res = 0.25f; break;
        case Patch::keys:  cut = 800.0f + 3800.0f * std::exp (-t / 0.25f);       res = 0.12f; break;
        case Patch::pluck: cut = 500.0f + 5200.0f * std::exp (-t / 0.08f);       res = 0.38f; break;
        case Patch::bass:  cut = 220.0f + 1500.0f * std::exp (-t / 0.12f);       res = 0.32f; break;
    }
    cut *= (0.65f + 0.55f * v.vel) * std::pow (v.freq / 261.6f, 0.35f);
    const float g = gFor (cut, sr), k = 2.0f - 2.0f * res;
    float yl = svfTick (v.svf[0], a, g, k).lp;
    float yr = p == Patch::bass ? yl : svfTick (v.svf[1], b, g, k).lp;
    if (p == Patch::bass) { yl = std::tanh (yl * 1.4f) / 1.4f; yr = yl; }   // a little weight; the bass is low, so it does not alias
    const float level = (p == Patch::bass ? 0.30f : p == Patch::pad ? 0.085f : 0.12f) * v.env * v.vel;
    l = yl * level;
    r = yr * level;
}

void PreviewSound::render (AudioBuffer<float>& buffer, const MidiBuffer& midi, int start, int n, float gain)
{
    const int chans = buffer.getNumChannels();
    if (chans == 0 || n <= 0) return;
    auto it = midi.begin();
    const double beatsPerSample = beatBpm / 60.0 / sr;
    for (int i = 0; i < n; ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= start + i)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) noteOn (m.getChannel(), m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) noteOff (m.getChannel(), m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
            ++it;
        }
        if (beatOn && kind == Kind::mix)
        {
            const double before = beatPos;
            beatPos += beatsPerSample;
            const double fb = std::floor (before), fa = std::floor (beatPos);
            if (fa > fb || before == 0.0)
            {
                startDrum (36, 0.95f);
                if (((int) fa) % 2 == 1) startDrum (39, 0.7f);
            }
            if (std::floor (before + 0.5) < std::floor (beatPos + 0.5)) startDrum (42, 0.45f);
        }
        vibPhase += twoPi * 5.4f / (float) sr;
        if (vibPhase > twoPi) vibPhase -= twoPi;

        float l = 0.0f, r = 0.0f;
        int idx = 0;
        for (auto& v : voices)
        {
            ++idx;
            if (! v.active) continue;
            if (v.drum)
            {
                const float s = renderDrum (v);
                l += s * (1.0f - v.pan);
                r += s * (1.0f + v.pan);
            }
            else
            {
                float vl, vr;
                renderVoice (v, vl, vr);
                // the sung voices alternate sides a little; the synth patches are already stereo
                const float pan = kind == Kind::voice ? ((idx % 2) == 0 ? 0.25f : -0.25f) : 0.0f;
                l += vl * (1.0f - pan);
                r += vr * (1.0f + pan);
            }
            v.age += 1.0f / (float) sr;
        }
        buffer.addSample (0, start + i, l * gain);
        if (chans > 1) buffer.addSample (1, start + i, r * gain);
    }
}
}

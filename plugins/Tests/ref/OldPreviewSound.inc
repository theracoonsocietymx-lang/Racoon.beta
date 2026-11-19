// the preview synth as it was before tanda 4, kept only so the test can measure the difference

// The sound every Racoon plugin can make on its own, so pressing Play always plays something:
//  - synth: a small polyphonic synth (pad, keys, pluck or bass) for the plugins that write notes (Chipmunk)
//  - drums: a synthesized kit on the General MIDI drum notes (Mole, the demo beat)
//  - voice: a sung "aah / ooh" made with formants, for Chinchilla
//  - mix:   drums + bass + keys, a small song for the effects (Skunk, Hedgehog) to work on
// It runs on the audio thread and never allocates while playing.
namespace racoon_old
{
class PreviewSound
{
public:
    enum class Kind { synth, drums, voice, mix };
    enum class Patch { pad, keys, pluck, bass };

    void prepare (double sampleRate);
    void reset();
    void setKind (Kind k) { kind = k; }
    void setPatch (Patch p) { patch = p; }
    Kind getKind() const { return kind; }

    // adds what the MIDI plays into the buffer (on top of what is already there)
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int startSample, int numSamples, float gain = 1.0f);
    // the "mix" kind plays its own four-on-the-floor under the notes while this is on
    void setBeat (bool on, double bpm, double beatPosition);
    bool isSounding() const;
    // the drums with a pitch (kick, toms, congas, bongos) land on notes of this key; keyPc -1 leaves them as they are
    void setDrumKey (int keyPc, bool major) { drumKey = keyPc; drumMajor = major; }

private:
    struct Voice
    {
        int note = -1, channel = 1;
        bool active = false, released = false, drum = false;
        float vel = 0.0f, phase[3] {}, freq = 440.0f, env = 0.0f, fenv = 0.0f, age = 0.0f, pitchEnv = 0.0f;
        float lp1 = 0.0f, lp2 = 0.0f, bp[3][2] {};
        juce::uint32 rng = 1;
        int drumType = 0;
    };
    static constexpr int numVoices = 24;
    Voice voices[numVoices];
    double sr = 48000.0;
    int drumKey = -1;
    bool drumMajor = false;
    float tunedHz (float hz, bool rootOrFifth) const;
    Kind kind = Kind::synth;
    Patch patch = Patch::pad;
    // beat for the mix
    bool beatOn = false;
    double beatBpm = 120.0, beatPos = 0.0;
    float vibPhase = 0.0f;

    void noteOn (int channel, int note, float velocity);
    void noteOff (int channel, int note);
    void allOff();
    void startDrum (int gmNote, float velocity);
    float renderVoice (Voice&);
    float renderDrum (Voice&);
    static float noise (juce::uint32& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 8388608.0f - 1.0f; }
};
}

namespace racoon_old
{
using namespace juce;

namespace
{
constexpr float twoPi = MathConstants<float>::twoPi;
inline float onePole (float& state, float x, float coef) { state += coef * (x - state); return state; }
inline float coefFor (float hz, double sr) { return 1.0f - std::exp (-twoPi * jlimit (10.0f, (float) (sr * 0.45), hz) / (float) sr); }
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
    for (int k = 0; k < 3; ++k) v->phase[k] = (float) k * 0.31f;
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
        case 0:   // kick: a falling sine and a click
        {
            const float f = v.freq + 110.0f * std::exp (-t / 0.035f);
            v.phase[0] += f / (float) sr; if (v.phase[0] > 1.0f) v.phase[0] -= 1.0f;
            out = std::sin (twoPi * v.phase[0]) * std::exp (-t / 0.16f) + n * 0.25f * std::exp (-t / 0.003f);
            out = std::tanh (out * 1.4f);
            if (t > 0.6f) v.active = false;
            break;
        }
        case 1:   // snare
        {
            v.phase[0] += 190.0f * (1.0f + 0.3f * std::exp (-t / 0.01f)) / (float) sr;
            const float body = std::sin (twoPi * v.phase[0]) * std::exp (-t / 0.05f);
            const float hp = n - onePole (v.lp1, n, coefFor (1800.0f, sr));
            out = body * 0.6f + hp * std::exp (-t / 0.08f) * 0.8f;
            if (t > 0.4f) v.active = false;
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
        case 4: case 5: case 6: case 7: case 13:   // hats, ride, crash, tambourine: bright noise
        {
            const float dec = v.drumType == 4 ? 0.035f : v.drumType == 5 ? 0.18f : v.drumType == 6 ? 0.5f : v.drumType == 7 ? 0.8f : 0.08f;
            const float cut = v.drumType == 6 ? 5000.0f : 7500.0f;
            const float hp = n - onePole (v.lp1, n, coefFor (cut, sr));
            out = hp * std::exp (-t / dec) * (v.drumType == 6 ? 0.45f : 0.6f);
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

float PreviewSound::renderVoice (Voice& v)
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
        return y * 0.055f * v.env * v.vel;
    }

    const auto p = (Patch) v.drumType;
    const auto e = envFor (p);
    if (! v.released) v.env = t < e.a ? t / e.a : e.s + (1.0f - e.s) * std::exp (-(t - e.a) / std::max (0.01f, e.d * 0.3f));
    else { v.env *= std::exp (-1.0f / (e.r * 0.3f * (float) sr)); if (v.env < 1.0e-4f) v.active = false; }
    if (! v.released && e.s <= 0.0f && t > e.a && v.env < 1.0e-4f) v.active = false;   // after the attack, not on its first sample

    const float det = p == Patch::pad ? 0.006f : 0.003f;
    v.phase[0] += v.freq * (1.0f + det) / (float) sr;
    v.phase[1] += v.freq * (1.0f - det) / (float) sr;
    v.phase[2] += v.freq * 0.5f / (float) sr;
    for (auto& ph : v.phase) if (ph >= 1.0f) ph -= 1.0f;
    float x = (2.0f * v.phase[0] - 1.0f) + (2.0f * v.phase[1] - 1.0f);
    if (p == Patch::bass) x = x * 0.6f + std::sin (twoPi * v.phase[2]) * 1.2f;
    float cut = 0.0f;
    switch (p)
    {
        case Patch::pad:   cut = 900.0f + 1400.0f * v.env; break;
        case Patch::keys:  cut = 700.0f + 3500.0f * std::exp (-t / 0.25f); break;
        case Patch::pluck: cut = 500.0f + 5000.0f * std::exp (-t / 0.08f); break;
        case Patch::bass:  cut = 250.0f + 1400.0f * std::exp (-t / 0.12f); break;
    }
    const float c = coefFor (cut, sr);
    const float y = onePole (v.lp2, onePole (v.lp1, x, c), c);
    const float level = p == Patch::bass ? 0.32f : p == Patch::pad ? 0.11f : 0.16f;
    return y * v.env * v.vel * level;
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
            const float s = v.drum ? renderDrum (v) : renderVoice (v);
            v.age += 1.0f / (float) sr;
            // pads and keys spread a little across the stereo field
            const float pan = v.drum ? 0.0f : ((idx % 2) == 0 ? 0.25f : -0.25f);
            l += s * (1.0f - pan);
            r += s * (1.0f + pan);
        }
        buffer.addSample (0, start + i, l * gain);
        if (chans > 1) buffer.addSample (1, start + i, r * gain);
    }
}
}

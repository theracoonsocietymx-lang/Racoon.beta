#include "DormouseProcessor.h"
#include "DormouseEditor.h"

namespace racoon::dormouse
{
using namespace juce;

// ===================================================================== wavetables
const StringArray& WaveBank::names()
{
    static StringArray a { "Analógica"_u, "Cálida"_u, "Vocal", "Cristal", "Órgano"_u, "Hueca", "VA Sierra", "VA Pulso" };
    return a;
}

int WaveBank::levelFor (float freq, double sampleRate)
{
    const double r = 1024.0 * jmax (1.0f, freq) / (0.45 * sampleRate);
    if (r <= 1.0) return 0;
    return jlimit (0, levels - 1, (int) std::ceil (std::log2 (r)));
}

const WaveBank& WaveBank::get()
{
    static const WaveBank bank;   // thread-safe initialisation, read only afterwards
    return bank;
}

WaveBank::WaveBank()
{
    data.assign ((size_t) (tableBanks * frames * levels * (size + 1)), 0.0f);
    juce::dsp::FFT fft (11);
    std::vector<float> a (1024), b (1024), buf (size * 2);
    const float pi = MathConstants<float>::pi;

    auto harmonics = [&] (int bankIdx, float t)
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        switch (bankIdx)
        {
            case 0:   // sine -> triangle -> saw -> square
            {
                auto shape = [&] (int s, int n) -> float
                {
                    const bool odd = (n & 1) == 1;
                    switch (s)
                    {
                        case 0: return n == 1 ? 1.0f : 0.0f;
                        case 1: return odd ? (8.0f / (pi * pi)) * (((n - 1) / 2) % 2 == 0 ? 1.0f : -1.0f) / (float) (n * n) : 0.0f;
                        case 2: return (2.0f / pi) * ((n % 2) == 1 ? 1.0f : -1.0f) / (float) n;
                        default: return odd ? 4.0f / (pi * (float) n) : 0.0f;
                    }
                };
                const float x = t * 3.0f;
                const int seg = jmin (2, (int) x);
                const float u = x - (float) seg;
                for (int n = 1; n < 1024; ++n) b[(size_t) n] = shape (seg, n) * (1.0f - u) + shape (seg + 1, n) * u;
                break;
            }
            case 1:   // warm: saw through a closing low pass
                for (int n = 1; n < 1024; ++n) b[(size_t) n] = (1.0f / (float) n) * std::exp (-(float) (n - 1) / (1.5f + t * t * 60.0f));
                break;
            case 2:   // vocal: formants of five vowels
            {
                static const float F[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 330, 700, 2500 } };
                static const float G[3] { 1.0f, 0.6f, 0.25f };
                const float x = t * 4.0f;
                const int seg = jmin (3, (int) x);
                const float u = x - (float) seg;
                for (int n = 1; n < 1024; ++n)
                {
                    float v = 0.04f / (float) n;
                    for (int j = 0; j < 3; ++j)
                    {
                        const float f = (F[seg][j] * (1.0f - u) + F[seg + 1][j] * u) / 160.0f;
                        const float d = ((float) n - f) / 1.3f;
                        v += G[j] * std::exp (-d * d);
                    }
                    b[(size_t) n] = v / std::pow ((float) n, 0.35f);
                }
                break;
            }
            case 3:   // crystal: sparse partials, the bright one moves up
            {
                static const int H[8] { 1, 2, 3, 5, 8, 13, 21, 34 };
                for (int k = 0; k < 8; ++k)
                {
                    const float d = (float) k - t * 7.0f;
                    b[(size_t) H[k]] += std::exp (-d * d * 0.5f) / (1.0f + 0.25f * (float) k) + (k == 0 ? 0.5f : 0.0f);
                }
                break;
            }
            case 4:   // organ: drawbars pulled out one after another
            {
                static const int H[7] { 1, 2, 3, 4, 5, 6, 8 };
                static const float r0[7] { 1.0f, 0.5f, 0, 0, 0, 0, 0 }, r1[7] { 1.0f, 1.0f, 0.9f, 0.85f, 0.6f, 0.7f, 0.8f };
                for (int j = 0; j < 7; ++j)
                    b[(size_t) H[j]] = r0[j] + (r1[j] - r0[j]) * jlimit (0.0f, 1.0f, t * 1.4f - (float) j * 0.12f);
                break;
            }
            default:  // hollow: pulse width from 50 % to 5 %
            {
                const float w = 0.5f - 0.45f * t;
                for (int n = 1; n < 1024; ++n) a[(size_t) n] = (2.0f / ((float) n * pi)) * std::sin ((float) n * pi * w);
                break;
            }
        }
    };

    for (int bk = 0; bk < tableBanks; ++bk)
        for (int fr = 0; fr < frames; ++fr)
        {
            harmonics (bk, (float) fr / (float) (frames - 1));
            float scale = 1.0f;
            for (int lv = 0; lv < levels; ++lv)
            {
                const int maxH = jmin (1023, 1024 >> lv);
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int n = 1; n <= maxH; ++n) { buf[(size_t) (2 * n)] = a[(size_t) n]; buf[(size_t) (2 * n + 1)] = -b[(size_t) n]; }
                fft.performRealOnlyInverseTransform (buf.data());
                // remove any DC and normalise every level with the scale of the richest one (no jumps between levels)
                float mean = 0.0f;
                for (int i = 0; i < size; ++i) mean += buf[(size_t) i];
                mean /= (float) size;
                if (lv == 0)
                {
                    float pk = 0.0f;
                    for (int i = 0; i < size; ++i) pk = jmax (pk, std::abs (buf[(size_t) i] - mean));
                    scale = pk > 0.0f ? 1.0f / pk : 1.0f;
                }
                auto* t = const_cast<float*> (table (bk, fr, lv));
                for (int i = 0; i < size; ++i) t[i] = (buf[(size_t) i] - mean) * scale;
                t[size] = t[0];
            }
        }
}

// ===================================================================== parameters
const StringArray& Processor::sourceNames()
{
    static StringArray a { "Nada", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Envolvente 2", "Envolvente 3", "Envolvente 4",
                           "Velocidad", "Aftertouch", "Rueda", "Tecla", "Azar por nota" };
    return a;
}

const StringArray& Processor::destNames()
{
    static StringArray a { "Nada", "Afinación"_u, "Afinación osc 2"_u, "Posición osc 1"_u, "Posición osc 2"_u, "Corte", "Resonancia",
                           "Volumen", "Paneo", "Ruido", "Textura", "Detune", "Nivel osc 1", "Nivel osc 2" };
    return a;
}

static const StringArray& lfoShapes()
{
    static StringArray a { "Seno", "Triángulo"_u, "Sierra", "Cuadrada", "S&H", "Azar suave" };
    return a;
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    for (int o = 1; o <= 2; ++o)
    {
        const String id = "osc" + String (o) + "_";
        P::choice (l, id + "bank", "Osc " + String (o) + " tabla", WaveBank::names(), o == 1 ? 1 : 0);
        P::pct (l, id + "pos", "Osc " + String (o) + " posición"_u, 0.0f, 100.0f, o == 1 ? 35.0f : 60.0f);
        P::integer (l, id + "oct", "Osc " + String (o) + " octava", -3, 3, 0);
        P::integer (l, id + "semi", "Osc " + String (o) + " semitonos", -12, 12, o == 1 ? 0 : 7);
        P::flt (l, id + "fine", "Osc " + String (o) + " fino", P::range (-100.0f, 100.0f), o == 1 ? 0.0f : 4.0f, "ct", 0);
        P::pct (l, id + "level", "Osc " + String (o) + " nivel", 0.0f, 100.0f, o == 1 ? 80.0f : 45.0f);
    }
    P::pct (l, "noise", "Ruido", 0.0f, 100.0f, 0.0f);
    P::flt (l, "noisecolor", "Color del ruido", P::range (-100.0f, 100.0f), -30.0f, "", 0);
    P::pct (l, "texture", "Textura", 0.0f, 100.0f, 0.0f);
    P::flt (l, "texdensity", "Densidad de textura", P::range (1.0f, 40.0f, 8.0f), 10.0f, "/s", 1);

    P::integer (l, "voices", "Voces", 1, maxVoices, 16);
    P::integer (l, "unison", "Unison", 1, maxUnison, 3);
    P::flt (l, "detune", "Detune", P::range (0.0f, 100.0f), 14.0f, "ct", 0);
    P::pct (l, "spread", "Apertura", 0.0f, 200.0f, 100.0f);
    P::ms (l, "glide", "Glide", 0.0f, 2000.0f, 0.0f, 200.0f);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);

    P::choice (l, "ftype", "Tipo de filtro", { "LP 12", "LP 24", "BP", "HP 12", "HP 24" }, 1);
    P::hz (l, "cutoff", "Corte", 20.0f, 20000.0f, 2400.0f);
    P::pct (l, "reso", "Resonancia", 0.0f, 95.0f, 15.0f);
    P::db (l, "drive", "Drive", 0.0f, 18.0f, 0.0f);
    P::pct (l, "keytrack", "Seguir teclado", 0.0f, 100.0f, 40.0f);
    P::flt (l, "fenv", "Envolvente al filtro", P::range (-100.0f, 100.0f), 20.0f, "%", 0);
    P::pct (l, "velflt", "Velocidad al filtro", 0.0f, 100.0f, 20.0f);
    P::pct (l, "velamp", "Velocidad al volumen", 0.0f, 100.0f, 50.0f);

    static const float defA[numEnv] { 0.6f, 1.2f, 2.0f, 0.01f }, defD[numEnv] { 1.5f, 3.0f, 2.0f, 1.0f },
                       defS[numEnv] { 85.0f, 40.0f, 60.0f, 0.0f }, defR[numEnv] { 2.5f, 3.0f, 3.0f, 1.0f };
    for (int k = 0; k < numEnv; ++k)
    {
        const String n = "Env " + String (k + 1) + " ";
        P::flt (l, envId (k, "a"), n + "ataque", P::range (0.001f, 30.0f, 1.0f), defA[k], "s", 3);
        P::flt (l, envId (k, "d"), n + "caída"_u, P::range (0.001f, 30.0f, 1.0f), defD[k], "s", 3);
        P::pct (l, envId (k, "s"), n + "sostenido", 0.0f, 100.0f, defS[k]);
        P::flt (l, envId (k, "r"), n + "relajación"_u, P::range (0.005f, 60.0f, 2.0f), defR[k], "s", 3);
    }
    static const float defRate[numLfo] { 0.12f, 0.05f, 0.3f, 0.01f };
    for (int k = 0; k < numLfo; ++k)
    {
        const String n = "LFO " + String (k + 1) + " ";
        P::flt (l, lfoId (k, "rate"), n + "velocidad", P::range (0.001f, 20.0f, 0.5f), defRate[k], "Hz", 3);
        P::toggle (l, lfoId (k, "sync"), n + "a tempo", false);
        P::choice (l, lfoId (k, "div"), n + "división"_u, RacoonProcessor::divisionNames(), 0);
        P::choice (l, lfoId (k, "shape"), n + "forma", lfoShapes(), k == 3 ? 5 : 0);
        P::flt (l, lfoId (k, "phase"), n + "fase", P::range (0.0f, 360.0f), 0.0f, "°"_u, 0);
        P::toggle (l, lfoId (k, "retrig"), n + "reinicia con cada nota", false);
    }
    static const int defSrc[numSlots] { srcLfo1, srcWheel, srcAt, srcLfo2, srcNone, srcNone, srcNone, srcNone };
    static const int defDst[numSlots] { dstPos1, dstCutoff, dstCutoff, dstPan, dstNone, dstNone, dstNone, dstNone };
    static const float defAmt[numSlots] { 20.0f, 30.0f, 25.0f, 15.0f, 0, 0, 0, 0 };
    for (int k = 0; k < numSlots; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        P::choice (l, slotId (k, "src"), n + "fuente", sourceNames(), defSrc[k]);
        P::choice (l, slotId (k, "dst"), n + "destino", destNames(), defDst[k]);
        P::flt (l, slotId (k, "amt"), n + "cantidad", P::range (-100.0f, 100.0f), defAmt[k], "%", 0);
    }
    P::pct (l, "chorus", "Chorus", 0.0f, 100.0f, 30.0f);
    P::flt (l, "chorusrate", "Chorus velocidad", P::range (0.05f, 3.0f, 0.5f), 0.35f, "Hz", 2);
    P::pct (l, "chorusdepth", "Chorus profundidad", 0.0f, 100.0f, 50.0f);
    P::choice (l, "quality", "Calidad", { "ECO", "NORMAL", "HQ" }, 1);
    P::db (l, "volume", "Volumen", -36.0f, 6.0f, -6.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Dormouse", "Pads", Colour (0xff7fbfa8), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    WaveBank::get();   // build the tables now, never on the audio thread
    static const char* oscNames[6] { "bank", "pos", "oct", "semi", "fine", "level" };
    static const char* envNames[4] { "a", "d", "s", "r" };
    static const char* lfoNames[6] { "rate", "sync", "div", "shape", "phase", "retrig" };
    static const char* slotNames[3] { "src", "dst", "amt" };
    for (int o = 0; o < 2; ++o) for (int j = 0; j < 6; ++j) pOsc[o][j] = param ("osc" + String (o + 1) + "_" + oscNames[j]);
    for (int k = 0; k < numEnv; ++k) for (int j = 0; j < 4; ++j) pEnv[k][j] = param (envId (k, envNames[j]));
    for (int k = 0; k < numLfo; ++k) for (int j = 0; j < 6; ++j) pLfo[k][j] = param (lfoId (k, lfoNames[j]));
    for (int k = 0; k < numSlots; ++k) for (int j = 0; j < 3; ++j) pSlot[k][j] = param (slotId (k, slotNames[j]));
}

void Processor::prepareDsp (double rate, int block)
{
    for (auto& c : chorusLine) c.prepare ((int) (rate * 0.05) + 8);
    mix.setSize (2, jmax (block, 1024), false, false, true);
    volSmooth.prepare (rate, 30.0);
    volSmooth.reset (dsp::dbToGain (value ("volume")));
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& v : voices) v = Voice();
    for (auto& c : chorusLine) c.clear();
    activeVoices = 0;
    pedal = false;
    keyboard.reset();
}

// ===================================================================== envelopes and LFOs
float Env::advance (int n, float a, float d, float s, float r, double sr)
{
    switch (stage)
    {
        case idle: level = 0.0f; break;
        case attack:
            level += (float) n / (float) (jmax (0.001f, a) * sr);
            if (level >= 1.0f) { level = 1.0f; stage = decay; }
            break;
        case decay:
            level = s + (level - s) * (float) std::exp (-6.9 * n / (jmax (0.001f, d) * sr));
            if (std::abs (level - s) < 1.0e-4f) { level = s; stage = sustain; }
            break;
        case sustain:
            level += (s - level) * (float) (1.0 - std::exp (-(double) n / (0.01 * sr)));
            break;
        case release:
            level *= (float) std::exp (-6.9 * n / (jmax (0.005f, r) * sr));
            if (level < 1.0e-4f) { level = 0.0f; stage = idle; }
            break;
    }
    return level;
}

float Processor::lfoValue (Voice& v, int k, float ph, bool newCycle)
{
    if (newCycle)
    {
        v.lfoRand[k][0] = v.lfoRand[k][1];
        v.lfoRand[k][1] = v.rng.nextBipolar();
    }
    switch ((int) rd (pLfo[k][3]))
    {
        case 0: return std::sin (MathConstants<float>::twoPi * ph);
        case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case 2: return 2.0f * ph - 1.0f;
        case 3: return ph < 0.5f ? 1.0f : -1.0f;
        case 4: return v.lfoRand[k][1];
        default:
        {
            const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::pi * ph);
            return v.lfoRand[k][0] + (v.lfoRand[k][1] - v.lfoRand[k][0]) * w;
        }
    }
}

// ===================================================================== MIDI
void Processor::noteOn (int note, float vel)
{
    const int limit = (int) value ("voices");
    const float glideMs = value ("glide");

    // mono legato: glide the held voice to the new note without restarting it
    if (limit == 1)
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.held)
            {
                v.glideFrom = v.pitch;
                v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
                v.note = note; v.velocity = vel;
                lastPitch = (float) note;
                return;
            }

    // the same note again: restart that voice from where it is (no click, no extra voice)
    for (auto& v : voices)
        if (v.active && ! v.stealing && v.note == note)
        {
            v.held = true; v.sustained = false; v.velocity = vel;
            for (auto& e : v.env) e.start();
            v.order = ++orderCounter;
            return;
        }

    int playing = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++playing;
    while (playing >= limit)
    {
        // steal: the oldest released voice, else the oldest held one; it fades out in 5 ms
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.stealing && ! v.held && ! v.sustained && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr)
            for (auto& v : voices)
                if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) break;
        victim->stealing = true;
        --playing;
    }

    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)   // every slot busy fading: reuse the quietest one
        for (auto& v : voices) if (slot == nullptr || v.stealGain < slot->stealGain) slot = &v;

    auto& v = *slot;
    v = Voice();
    v.active = true; v.held = true; v.note = note; v.velocity = vel;
    v.order = ++orderCounter;
    v.rng.seed ((uint32) (0x9e3779b9u * (uint32) (++noteCounter) + (uint32) note * 7919u));
    v.random = v.rng.nextBipolar();
    v.pitch = (float) note;
    v.glideFrom = glideMs > 0.5f ? lastPitch : (float) note;
    v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
    for (int i = 0; i < maxUnison; ++i)
    {
        v.phase[i][0] = v.rng.next01();
        v.phase[i][1] = v.rng.next01();
        v.uniJit[i] = v.rng.nextBipolar() * 2.0f;   // cents
    }
    for (int k = 0; k < numLfo; ++k)
    {
        v.lfoRand[k][0] = v.rng.nextBipolar(); v.lfoRand[k][1] = v.rng.nextBipolar();
        v.lfoPhase[k] = rd (pLfo[k][4]) / 360.0f;
    }
    for (auto& e : v.env) e.start();
    v.grainClock = v.rng.next01();
    lastPitch = (float) note;
}

void Processor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            v.held = false;
            if (pedal) v.sustained = true;
            else for (auto& e : v.env) e.noteOff();
        }
}

void Processor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isChannelPressure()) channelPressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
    {
        for (auto& v : voices) if (v.active && v.note == m.getNoteNumber()) v.pressure = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1) wheel = (float) val / 127.0f;
        else if (cc == 64)
        {
            pedal = val >= 64;
            if (! pedal)
                for (auto& v : voices)
                    if (v.active && v.sustained) { v.sustained = false; for (auto& e : v.env) e.noteOff(); }
        }
        else if (cc == 120) { for (auto& v : voices) if (v.active) { v.stealing = true; } }
        else if (cc == 123) { for (auto& v : voices) if (v.active) { v.held = v.sustained = false; for (auto& e : v.env) e.noteOff(); } }
    }
    else if (m.isAllNotesOff()) { for (auto& v : voices) if (v.active) { v.held = v.sustained = false; for (auto& e : v.env) e.noteOff(); } }
    else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; }
}

// ===================================================================== voice rendering
static inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

static inline float blepSaw (float ph, float inc)
{
    return 2.0f * ph - 1.0f - polyBlep (ph, inc);
}

static inline float wrap01 (float x) { return x - std::floor (x); }

void Processor::renderVoice (Voice& v, int start, int total, int cr)
{
    const auto& bank = WaveBank::get();
    const double srate = sr;
    const int quality = (int) value ("quality");
    const int uni = quality == 0 ? jmin (4, (int) value ("unison")) : (int) value ("unison");
    const float spread = value ("spread") * 0.01f;
    const float bendRange = value ("bendrange");
    const float glideMs = value ("glide");
    const int ftype = (int) value ("ftype");
    const float driveG = dsp::dbToGain (value ("drive"));
    const bool useDrive = driveG > 1.01f;
    const float driveComp = 1.0f / std::sqrt (driveG);
    const float noiseColor = value ("noisecolor") * 0.01f;
    const float texDensity = value ("texdensity");
    const float uniNorm = 1.0f / std::sqrt ((float) uni);
    const bool hq = quality == 2;
    int bankIdx[2], levelBase[2];
    float oscOffset[2], posBase[2], levelP[2];
    for (int o = 0; o < 2; ++o)
    {
        bankIdx[o] = (int) rd (pOsc[o][0]);
        oscOffset[o] = rd (pOsc[o][2]) * 12.0f + rd (pOsc[o][3]) + rd (pOsc[o][4]) * 0.01f;
        posBase[o] = rd (pOsc[o][1]) * 0.01f;
        levelP[o] = rd (pOsc[o][5]) * 0.01f;
        levelBase[o] = 0;
    }
    float envA[numEnv], envD[numEnv], envS[numEnv], envR[numEnv];
    for (int k = 0; k < numEnv; ++k)
    {
        envA[k] = rd (pEnv[k][0]); envD[k] = rd (pEnv[k][1]);
        envS[k] = rd (pEnv[k][2]) * 0.01f; envR[k] = rd (pEnv[k][3]);
    }
    float lfoRate[numLfo]; bool lfoRetrig[numLfo]; float lfoPh0[numLfo];
    for (int k = 0; k < numLfo; ++k)
    {
        lfoRate[k] = rd (pLfo[k][1]) > 0.5f ? (float) (1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm())) : rd (pLfo[k][0]);
        lfoRetrig[k] = rd (pLfo[k][5]) > 0.5f;
        lfoPh0[k] = rd (pLfo[k][4]) / 360.0f;
    }
    int slotSrc[numSlots], slotDst[numSlots]; float slotAmt[numSlots];
    for (int k = 0; k < numSlots; ++k)
    {
        slotSrc[k] = (int) rd (pSlot[k][0]); slotDst[k] = (int) rd (pSlot[k][1]);
        slotAmt[k] = rd (pSlot[k][2]) * 0.01f;
    }

    auto* outL = mix.getWritePointer (0) + start;
    auto* outR = mix.getWritePointer (1) + start;
    const float stealStep = (float) (1.0 / (0.005 * srate));

    for (int done = 0; done < total && v.active; )
    {
        const int n = jmin (cr, total - done);

        // ---- control rate: envelopes, LFOs and the modulation matrix
        float env[numEnv];
        for (int k = 0; k < numEnv; ++k) env[k] = v.env[k].advance (n, envA[k], envD[k], envS[k], envR[k], srate);
        float lfo[numLfo];
        for (int k = 0; k < numLfo; ++k)
        {
            float ph;
            bool wrapped;
            if (lfoRetrig[k])
            {
                v.lfoPhase[k] += lfoRate[k] * n / srate;
                wrapped = v.lfoPhase[k] >= 1.0;
                v.lfoPhase[k] -= std::floor (v.lfoPhase[k]);
                ph = (float) v.lfoPhase[k];
            }
            else
            {
                const double g = globalLfo[k] + lfoRate[k] * (start + done) / srate + lfoPh0[k];
                ph = (float) (g - std::floor (g));
                wrapped = ph < (float) v.lfoPhase[k];
                v.lfoPhase[k] = ph;
            }
            lfo[k] = lfoValue (v, k, ph, wrapped);
        }
        float src[numSources] {};
        for (int k = 0; k < numLfo; ++k) src[srcLfo1 + k] = lfo[k];
        src[srcEnv2] = env[1]; src[srcEnv3] = env[2]; src[srcEnv4] = env[3];
        src[srcVel] = v.velocity;
        src[srcAt] = jmax (v.pressure, channelPressure);
        src[srcWheel] = wheel;
        src[srcKey] = ((float) v.note - 60.0f) / 48.0f;
        src[srcRandom] = v.random;
        float mod[numDests] {};
        for (int k = 0; k < numSlots; ++k)
            if (slotSrc[k] != srcNone && slotDst[k] != dstNone) mod[slotDst[k]] += src[slotSrc[k]] * slotAmt[k];

        // glide
        if (v.glidePos < 1.0f)
            v.glidePos = jmin (1.0f, v.glidePos + (float) (n / (jmax (1.0f, glideMs) * 0.001f * srate)));
        v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glidePos;
        const float basePitch = v.pitch + bend * bendRange + mod[dstPitch] * 24.0f;

        // filter cutoff (keytrack, envelope 2, velocity, modulation)
        const float oct = (value ("keytrack") * 0.01f) * ((float) v.note - 60.0f) / 12.0f
                        + value ("fenv") * 0.01f * env[1] * 6.0f
                        + value ("velflt") * 0.01f * (v.velocity - 0.7f) * 3.0f
                        + mod[dstCutoff] * 6.0f;
        const float fc = jlimit (20.0f, (float) (srate * (hq ? 0.9 : 0.45)), value ("cutoff") * std::pow (2.0f, oct));
        const float gTarget = dsp::SVF::gFor (fc, hq ? srate * 2.0 : srate);
        const float reso = jlimit (0.0f, 0.95f, value ("reso") * 0.01f + mod[dstReso]);
        const float k1 = 2.0f - 2.0f * reso * 0.97f;

        // amplitude: envelope 1, velocity, level modulation, voice steal fade
        const float velAmp = 1.0f - value ("velamp") * 0.01f * (1.0f - v.velocity);
        const float ampTarget = env[0] * velAmp * jlimit (0.0f, 2.0f, 1.0f + mod[dstLevel]) * 0.25f;
        const float pan = jlimit (-1.0f, 1.0f, mod[dstPan]);
        const float panLT = std::cos ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;
        const float panRT = std::sin ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;

        const float detune = jlimit (0.0f, 200.0f, value ("detune") + mod[dstDetune] * 100.0f);
        float inc[2][maxUnison], uL[maxUnison], uR[maxUnison];
        int lvl[2]; float pos[2], oscLvl[2];
        for (int o = 0; o < 2; ++o)
        {
            const float p = basePitch + oscOffset[o] + (o == 1 ? mod[dstPitch2] * 24.0f : 0.0f);
            const float f0 = 440.0f * std::pow (2.0f, (p - 69.0f) / 12.0f);
            lvl[o] = WaveBank::levelFor (f0 * std::pow (2.0f, detune / 2400.0f), srate) + levelBase[o];
            for (int u = 0; u < uni; ++u)
            {
                const float cents = (uni == 1 ? 0.0f : detune * ((float) u / (float) (uni - 1) - 0.5f)) + (uni == 1 ? 0.0f : v.uniJit[u]);
                inc[o][u] = jmin (0.49f, (float) (f0 * std::pow (2.0f, cents / 1200.0f) / srate));
            }
            pos[o] = jlimit (0.0f, 1.0f, posBase[o] + mod[o == 0 ? dstPos1 : dstPos2]);
            oscLvl[o] = jlimit (0.0f, 1.0f, levelP[o] + mod[o == 0 ? dstOsc1 : dstOsc2]) * uniNorm;
        }
        for (int u = 0; u < uni; ++u)
        {
            const float p = uni == 1 ? 0.0f : jlimit (-1.0f, 1.0f, spread * ((float) u / (float) (uni - 1) * 2.0f - 1.0f));
            uL[u] = std::cos ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
            uR[u] = std::sin ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
        }
        const float noiseLvl = jlimit (0.0f, 1.0f, value ("noise") * 0.01f + mod[dstNoise]) * 0.35f;
        const float texLvl = jlimit (0.0f, 1.0f, value ("texture") * 0.01f + mod[dstTexture]) * 0.5f;
        const float f0tex = 440.0f * std::pow (2.0f, (basePitch - 69.0f) / 12.0f);

        // ---- audio rate, with values ramped across the block
        const float ampStart = v.ampNow, gStart = v.gNow > 0.0f ? v.gNow : gTarget;
        const float pos0Start = v.pos1, pos1Start = v.pos2;
        const float plStart = v.panL, prStart = v.panR;
        const float invN = 1.0f / (float) n;
        const float* sine = bank.table (0, 0, WaveBank::levels - 1);

        for (int i = 0; i < n; ++i)
        {
            const float w = (float) (i + 1) * invN;
            const float amp = ampStart + (ampTarget - ampStart) * w;
            const float g = gStart + (gTarget - gStart) * w;
            float sl = 0.0f, sr2 = 0.0f;
            for (int o = 0; o < 2; ++o)
            {
                if (oscLvl[o] <= 1.0e-4f) continue;
                const float ps = (o == 0 ? pos0Start : pos1Start);
                const float pp = ps + (pos[o] - ps) * w;
                const int bk = bankIdx[o];
                if (bk < WaveBank::tableBanks)
                {
                    const float fpos = pp * (float) (WaveBank::frames - 1);
                    const int f = jmin (WaveBank::frames - 2, (int) fpos);
                    const float ff = fpos - (float) f;
                    const float* t0 = bank.table (bk, f, lvl[o]);
                    const float* t1 = bank.table (bk, f + 1, lvl[o]);
                    for (int u = 0; u < uni; ++u)
                    {
                        float& ph = v.phase[u][o];
                        const float idx = ph * (float) WaveBank::size;
                        const int i0 = (int) idx;
                        const float fr = idx - (float) i0;
                        const float a = t0[i0] + fr * (t0[i0 + 1] - t0[i0]);
                        const float b = t1[i0] + fr * (t1[i0 + 1] - t1[i0]);
                        const float s = (a + (b - a) * ff) * oscLvl[o];
                        sl += s * uL[u]; sr2 += s * uR[u];
                        ph += inc[o][u];
                        if (ph >= 1.0f) ph -= 1.0f;
                    }
                }
                else
                {
                    const bool pulse = bk == WaveBank::tableBanks + 1;
                    const float shift = pulse ? 0.5f - 0.45f * pp : 0.5f;
                    const float amount = pulse ? 1.0f : pp;
                    for (int u = 0; u < uni; ++u)
                    {
                        float& ph = v.phase[u][o];
                        const float dt = inc[o][u];
                        float s = blepSaw (ph, dt) - amount * blepSaw (wrap01 (ph + shift), dt);
                        if (pulse) s += 1.0f - 2.0f * shift;         // remove the pulse DC
                        s *= (pulse ? 0.7f : 0.8f) * oscLvl[o];
                        sl += s * uL[u]; sr2 += s * uR[u];
                        ph += dt;
                        if (ph >= 1.0f) ph -= 1.0f;
                    }
                }
            }
            // noise
            if (noiseLvl > 0.0f)
            {
                const float wn = v.rng.nextBipolar();
                v.noiseLp += 0.08f * (wn - v.noiseLp);
                const float colored = noiseColor < 0.0f ? wn + (v.noiseLp * 2.5f - wn) * (-noiseColor) : wn + ((wn - v.noiseLp) - wn) * noiseColor;
                sl += colored * noiseLvl * 0.7071f; sr2 += colored * noiseLvl * 0.7071f;
            }
            // granular texture
            if (texLvl > 0.0f)
            {
                v.grainClock += texDensity / (float) srate;
                if (v.grainClock >= 1.0f)
                {
                    v.grainClock -= 1.0f;
                    for (auto& gr : v.grains)
                        if (! gr.on)
                        {
                            const float r = v.rng.next01();
                            const int h = 1 + (int) (r * r * 8.0f);
                            const float freq = f0tex * (float) h * std::pow (2.0f, v.rng.nextBipolar() * 10.0f / 1200.0f);
                            if (freq > srate * 0.45) break;
                            gr.on = true; gr.phase = v.rng.next01(); gr.inc = (float) (freq / srate);
                            gr.pos = 0.0f; gr.len = (float) (srate * (0.04 + 0.16 * v.rng.next01()));
                            const float p = v.rng.nextBipolar();
                            gr.gl = std::cos ((p + 1.0f) * MathConstants<float>::pi * 0.25f) / std::sqrt ((float) h);
                            gr.gr = std::sin ((p + 1.0f) * MathConstants<float>::pi * 0.25f) / std::sqrt ((float) h);
                            break;
                        }
                }
                for (auto& gr : v.grains)
                    if (gr.on)
                    {
                        const float x = gr.pos / gr.len;
                        const float win = 0.5f - 0.5f * sine[(int) (wrap01 (x + 0.25f) * WaveBank::size)];
                        const float s = sine[(int) (gr.phase * WaveBank::size)] * win * texLvl;
                        sl += s * gr.gl; sr2 += s * gr.gr;
                        gr.phase += gr.inc; if (gr.phase >= 1.0f) gr.phase -= 1.0f;
                        if ((gr.pos += 1.0f) >= gr.len) gr.on = false;
                    }
            }

            // drive and filter (stereo)
            float in[2] { sl, sr2 }, out[2];
            for (int c = 0; c < 2; ++c)
            {
                float x = in[c];
                if (useDrive) x = racoon::sdsp::adaaAverage ([] (float u) { return std::tanh (u); }, x * driveG, v.driveX[c]) * driveComp;   // ADAA
                auto run = [&] (float xin)
                {
                    v.f1[c].process (xin, g, k1);
                    float y;
                    switch (ftype)
                    {
                        case 0: y = v.f1[c].lp; break;
                        case 1: v.f2[c].process (v.f1[c].lp, g, 1.414f); y = v.f2[c].lp; break;
                        case 2: y = v.f1[c].bp * k1; break;
                        case 3: y = v.f1[c].hp; break;
                        default: v.f2[c].process (v.f1[c].hp, g, 1.414f); y = v.f2[c].hp; break;
                    }
                    return y;
                };
                if (hq)
                {
                    const float y0 = run ((x + v.lastIn[c]) * 0.5f);
                    const float y1 = run (x);
                    out[c] = (y0 + y1) * 0.5f;
                    v.lastIn[c] = x;
                }
                else out[c] = run (x);
            }

            float gain = amp;
            if (v.stealing)
            {
                v.stealGain -= stealStep;
                if (v.stealGain <= 0.0f) { v.stealGain = 0.0f; v.active = false; }
                gain *= v.stealGain;
            }
            const float pl = plStart + (panLT - plStart) * w, pr = prStart + (panRT - prStart) * w;
            outL[done + i] += out[0] * gain * pl;
            outR[done + i] += out[1] * gain * pr;
            if (! v.active) break;
        }
        v.ampNow = ampTarget; v.gNow = gTarget; v.pos1 = pos[0]; v.pos2 = pos[1]; v.panL = panLT; v.panR = panRT;
        if (v.env[0].stage == Env::idle) v.active = false;
        done += n;
    }
}

// ===================================================================== block
void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    if (mix.getNumSamples() < n) mix.setSize (2, n, false, false, true);   // rare: host larger than promised
    mix.clear (0, n);
    const int quality = (int) value ("quality");
    const int cr = quality == 0 ? 32 : quality == 1 ? 16 : 8;

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices) if (v.active) renderVoice (v, pos, end - pos, cr);
        pos = end;
    };
    for (const auto ev : midi)
    {
        renderTo (jlimit (0, n, ev.samplePosition));
        handleMidi (ev.getMessage());
    }
    renderTo (n);

    for (int k = 0; k < numLfo; ++k)
    {
        const double rate = rd (pLfo[k][1]) > 0.5f ? 1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm()) : rd (pLfo[k][0]);
        globalLfo[k] += rate * n / sr;
        globalLfo[k] -= std::floor (globalLfo[k]);
        lfoView[k].store ((float) std::sin (MathConstants<double>::twoPi * globalLfo[k]));
    }

    // chorus: two modulated short delays, one per side, in quadrature
    const float chMix = value ("chorus") * 0.01f;
    const float depth = value ("chorusdepth") * 0.01f * 0.004f * (float) sr;
    const double chInc = value ("chorusrate") / sr;
    volSmooth.target = dsp::dbToGain (value ("volume"));
    auto* L = mix.getWritePointer (0);
    auto* R = mix.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        chorusLine[0].push (l); chorusLine[1].push (r);
        if (chMix > 0.001f)
        {
            chorusPhase += chInc;
            if (chorusPhase >= 1.0) chorusPhase -= 1.0;
            const float s0 = (float) std::sin (MathConstants<double>::twoPi * chorusPhase);
            const float s1 = (float) std::cos (MathConstants<double>::twoPi * chorusPhase);
            const float base = 0.008f * (float) sr;
            const float wl = chorusLine[0].tap (base + depth * (1.0f + s0));
            const float wr = chorusLine[1].tap (base + depth * (1.0f + s1));
            const float norm = 1.0f / (1.0f + 0.5f * chMix);
            l = (l + chMix * (0.7f * wl - 0.3f * wr)) * norm;
            r = (r + chMix * (0.7f * wr - 0.3f * wl)) * norm;
        }
        const float vol = volSmooth.next();
        L[i] = l * vol; R[i] = r * vol;
    }
    if (ch == 1) { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.addFrom (0, 0, mix, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f); }
    else { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.copyFrom (1, 0, mix, 1, 0, n); }

    int count = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++count;
    activeVoices.store (count);
}

// ===================================================================== presets (original sounds)
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    auto e = [] (int k, float a, float d, float s, float r)
    {
        return std::vector<std::pair<String, float>> { { envId (k, "a"), a }, { envId (k, "d"), d }, { envId (k, "s"), s }, { envId (k, "r"), r } };
    };
    auto join = [] (std::vector<std::pair<String, float>> a, const std::vector<std::pair<String, float>>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    std::vector<FactoryPreset> p;
    p.push_back ({ "Colchón tibio"_u, "Warm Pad", {} });
    p.push_back ({ "Lana", "Warm Pad", join ({ { "osc1_bank", 0 }, { "osc1_pos", 70 }, { "osc2_bank", 1 }, { "osc2_pos", 30 }, { "osc2_semi", 12 },
                                               { "unison", 5 }, { "detune", 22 }, { "cutoff", 1500 }, { "reso", 10 }, { "chorus", 45 } }, e (0, 1.2f, 2, 90, 3.5f)) });
    p.push_back ({ "Domingo", "Warm Pad", join ({ { "osc1_bank", 4 }, { "osc1_pos", 45 }, { "osc2_bank", 1 }, { "osc2_semi", 0 }, { "osc2_fine", -6 },
                                                  { "cutoff", 3200 }, { "chorus", 55 }, { "m1_amt", 10 } }, e (0, 0.4f, 1, 95, 2)) });
    p.push_back ({ "Cuerdas de bola", "Dark Disco Strings", join ({ { "osc1_bank", 6 }, { "osc1_pos", 0 }, { "osc2_bank", 6 }, { "osc2_pos", 20 }, { "osc2_semi", 0 },
                                                                     { "osc2_fine", 9 }, { "unison", 4 }, { "detune", 18 }, { "ftype", 0 }, { "cutoff", 3800 },
                                                                     { "fenv", 10 }, { "chorus", 60 }, { "chorusdepth", 70 }, { "m1_amt", 0 } }, e (0, 0.12f, 1, 80, 0.8f)) });
    p.push_back ({ "Medianoche", "Dark Disco Strings", join ({ { "osc1_bank", 7 }, { "osc1_pos", 40 }, { "osc2_bank", 6 }, { "osc2_semi", -12 }, { "osc2_level", 35 },
                                                                { "unison", 3 }, { "detune", 12 }, { "cutoff", 1100 }, { "reso", 25 }, { "fenv", 30 },
                                                                { "chorus", 50 }, { "m1_src", srcLfo1 }, { "m1_dst", dstPos1 }, { "m1_amt", 35 } },
                                                              join (e (0, 0.08f, 1.5f, 70, 0.6f), e (1, 0.3f, 1.2f, 20, 0.8f))) });
    p.push_back ({ "Bruma", "Analog Wash", join ({ { "osc1_bank", 6 }, { "osc2_bank", 7 }, { "osc2_pos", 60 }, { "osc2_semi", 0 }, { "unison", 6 }, { "detune", 35 },
                                                   { "spread", 160 }, { "noise", 18 }, { "noisecolor", -60 }, { "cutoff", 900 }, { "fenv", 45 },
                                                   { "chorus", 40 } }, join (e (0, 2.5f, 3, 85, 5), e (1, 4, 6, 30, 5))) });
    p.push_back ({ "Acuarela", "Analog Wash", join ({ { "osc1_bank", 0 }, { "osc1_pos", 60 }, { "osc2_bank", 2 }, { "osc2_pos", 40 }, { "unison", 4 }, { "detune", 25 },
                                                      { "cutoff", 1800 }, { "m1_dst", dstPos2 }, { "m1_amt", 40 }, { "lfo1_rate", 0.07f }, { "chorus", 50 } },
                                                    e (0, 3, 2, 90, 6)) });
    p.push_back ({ "Pozo", "Drone", join ({ { "osc1_bank", 1 }, { "osc1_pos", 15 }, { "osc1_oct", -1 }, { "osc2_bank", 5 }, { "osc2_pos", 50 }, { "osc2_semi", 7 },
                                            { "osc2_fine", -3 }, { "unison", 2 }, { "detune", 8 }, { "cutoff", 600 }, { "reso", 35 }, { "noise", 10 },
                                            { "m1_src", srcLfo4 }, { "m1_dst", dstCutoff }, { "m1_amt", 30 }, { "lfo4_rate", 0.02f }, { "chorus", 25 } },
                                          e (0, 6, 4, 100, 12)) });
    p.push_back ({ "Antena", "Drone", join ({ { "osc1_bank", 3 }, { "osc1_pos", 30 }, { "osc2_bank", 3 }, { "osc2_pos", 80 }, { "osc2_semi", 12 }, { "osc2_fine", 3 },
                                              { "texture", 35 }, { "texdensity", 6 }, { "cutoff", 5000 }, { "m1_dst", dstPos2 }, { "m1_amt", 45 },
                                              { "lfo1_rate", 0.01f }, { "chorus", 35 } }, e (0, 8, 4, 100, 15)) });
    p.push_back ({ "Respiración"_u, "Evolving Texture", join ({ { "osc1_bank", 2 }, { "osc1_pos", 0 }, { "osc2_bank", 1 }, { "osc2_pos", 50 }, { "unison", 3 },
                                                               { "texture", 40 }, { "texdensity", 14 }, { "cutoff", 2200 }, { "m1_src", srcEnv3 }, { "m1_dst", dstPos1 },
                                                               { "m1_amt", 100 }, { "m4_src", srcLfo2 }, { "m4_dst", dstTexture }, { "m4_amt", 30 }, { "chorus", 40 } },
                                                             join (e (0, 2, 3, 90, 6), e (2, 12, 10, 100, 8))) });
    p.push_back ({ "Polen", "Evolving Texture", join ({ { "osc1_bank", 3 }, { "osc1_pos", 50 }, { "osc2_bank", 4 }, { "osc2_pos", 20 }, { "osc2_semi", 19 }, { "osc2_level", 25 },
                                                        { "texture", 60 }, { "texdensity", 22 }, { "noise", 6 }, { "noisecolor", 40 }, { "cutoff", 6000 },
                                                        { "m1_src", srcLfo3 }, { "m1_dst", dstPos1 }, { "m1_amt", 40 }, { "lfo3_shape", 5 }, { "lfo3_rate", 0.4f },
                                                        { "chorus", 55 } }, e (0, 1.5f, 3, 75, 7)) });
    p.push_back ({ "Marea", "Evolving Texture", join ({ { "osc1_bank", 5 }, { "osc1_pos", 20 }, { "osc2_bank", 2 }, { "osc2_pos", 75 }, { "unison", 4 }, { "detune", 16 },
                                                        { "ftype", 2 }, { "cutoff", 1200 }, { "reso", 45 }, { "m1_src", srcLfo1 }, { "m1_dst", dstCutoff }, { "m1_amt", 45 },
                                                        { "lfo1_rate", 0.05f }, { "m4_src", srcRandom }, { "m4_dst", dstPan }, { "m4_amt", 50 }, { "chorus", 35 } },
                                                      e (0, 4, 3, 90, 8)) });
    return p;
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 3.9, 57, 96, 1 },
             { 0.0, 3.9, 60, 96, 1 },
             { 0.0, 3.9, 64, 96, 1 },
             { 0.0, 3.9, 67, 96, 1 },
             { 4.0, 3.9, 53, 96, 1 },
             { 4.0, 3.9, 57, 96, 1 },
             { 4.0, 3.9, 60, 96, 1 },
             { 4.0, 3.9, 64, 96, 1 },
             { 8.0, 3.9, 48, 96, 1 },
             { 8.0, 3.9, 55, 96, 1 },
             { 8.0, 3.9, 60, 96, 1 },
             { 8.0, 3.9, 64, 96, 1 },
             { 12.0, 3.9, 55, 96, 1 },
             { 12.0, 3.9, 59, 96, 1 },
             { 12.0, 3.9, 62, 96, 1 },
             { 12.0, 3.9, 67, 96, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

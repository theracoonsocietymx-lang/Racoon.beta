#include "OpossumProcessor.h"
#include "OpossumEditor.h"
#include "RacoonDsp.h"

namespace racoon::opossum
{
using namespace juce;

// ===================================================================== wirings
const Algo& algo (int i)
{
    // operators are 0..5 here (1..6 on screen); from -> to; the ones in `carriers` are heard
    static const Algo a[numAlgos] = {
        { "Pila y sub",    4, { 4, 3, 2, 1 },       { 3, 2, 1, 0 },       0b100001 },
        { "Pila",          5, { 5, 4, 3, 2, 1 },    { 4, 3, 2, 1, 0 },    0b000001 },
        { "Dos pilas",     4, { 2, 1, 5, 4 },       { 1, 0, 4, 3 },       0b001001 },
        { "Tres pares",    3, { 1, 3, 5 },          { 0, 2, 4 },          0b010101 },
        { "Abanico",       5, { 1, 2, 3, 4, 5 },    { 0, 0, 0, 0, 0 },    0b000001 },
        { "Tridente",      5, { 5, 4, 3, 3, 3 },    { 4, 3, 0, 1, 2 },    0b000111 },
        { "Horquilla",     4, { 2, 1, 4, 3 },       { 1, 0, 3, 0 },       0b100001 },
        { "Campana",       4, { 1, 2, 4, 5 },       { 0, 0, 3, 3 },       0b001001 },
        { "Cascada doble", 5, { 3, 2, 1, 5, 4 },    { 2, 1, 0, 4, 0 },    0b000001 },
        { "Órgano",        0, {},                   {},                   0b111111 },
        { "Anillo",        6, { 5, 4, 3, 2, 1, 0 }, { 4, 3, 2, 1, 0, 5 }, 0b000001 },
        { "Gruñido",       5, { 5, 4, 3, 2, 1 },    { 3, 3, 1, 1, 0 },    0b000001 },
    };
    return a[jlimit (0, numAlgos - 1, i)];
}

// ===================================================================== tables
namespace
{
struct Sine
{
    static constexpr int size = 4096;
    float t[size + 1];
    Sine() { for (int i = 0; i <= size; ++i) t[i] = (float) std::sin (MathConstants<double>::twoPi * i / size); }
    static const Sine& get() { static const Sine s; return s; }
    inline float at (float ph) const   // ph in cycles, any value
    {
        ph -= std::floor (ph);
        const float idx = ph * (float) size;
        const int i0 = jmin (size - 1, (int) idx);
        const float fr = idx - (float) i0;
        return t[i0] + fr * (t[i0 + 1] - t[i0]);
    }
};

// the four shapes of an operator, all made from the sine (DC removed so they do not shift the carrier)
inline float wave (const Sine& S, int w, float ph)
{
    const float s = S.at (ph);
    switch (w)
    {
        case 1: return jmax (0.0f, s) * 2.0f - 0.6366f;                          // half sine
        case 2: return std::abs (s) * 2.0f - 1.2732f;                            // absolute sine (an octave up)
        case 3:                                                                   // quarter sine
        {
            const float p = ph - std::floor (ph);
            const float q = p - std::floor (p * 2.0f) * 0.5f;
            return (q < 0.25f ? std::abs (s) : 0.0f) * 2.0f - 0.6366f;
        }
        default: return s;
    }
}

constexpr float metalOffset[numOps] { 0.0f, 0.4142f, 0.7321f, 0.2361f, 0.6180f, 0.1623f };
// vowel formants U O A E I (Hz) and gains: the dark vowels first
constexpr float formants[5][3] { { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 } };
constexpr float formantGain[3] { 1.0f, 0.5f, 0.25f };
}

struct Processor::Impl
{
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    AudioBuffer<float> zeros;
    sdsp::Verb verb;
    AudioBuffer<float> wet;
    racoon::dsp::DelayLine echo[2];
    float echoLp[2] {};
    racoon::dsp::Smooth echoTime, volSmooth;
    float distX[2] {};
    float monoLp[2] {};
    racoon::dsp::SVF sideHp[2];
    racoon::dsp::DCBlock dc[2];
};

Processor::~Processor() = default;

// ===================================================================== parameters
const StringArray& Processor::algoNames()
{
    static StringArray a;
    if (a.isEmpty()) for (int i = 0; i < numAlgos; ++i) a.add (String::fromUTF8 (algo (i).name));
    return a;
}

const StringArray& Processor::waveNames()
{
    static StringArray a { "Seno", "Medio seno", "Seno absoluto", "Cuarto de seno" };
    return a;
}

const StringArray& Processor::ratioNames()
{
    static StringArray a { "0.5", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "14", "16" };
    return a;
}

float Processor::ratioValue (int i)
{
    static const float r[] { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16 };
    return r[jlimit (0, 14, i)];
}

const StringArray& Processor::sourceNames()
{
    static StringArray a { "Nada", "LFO 1", "LFO 2", "Envolvente del filtro", "Velocidad", "Aftertouch", "Rueda", "Tecla", "Azar por nota" };
    return a;
}

const StringArray& Processor::destNames()
{
    static StringArray a { "Nada", "Afinación"_u, "Mordida", "Retro", "Metal", "Corte", "Resonancia", "Vocal", "Drive", "Volumen", "Paneo" };
    return a;
}

static const StringArray& lfoShapes()
{
    static StringArray a { "Seno", "Triángulo"_u, "Sierra baja", "Sierra sube", "Cuadrada", "S&H" };
    return a;
}

static const StringArray& echoDivs()
{
    static StringArray a { "1/16", "1/8 T", "1/8", "1/8 .", "1/4", "1/4 .", "1/2" };
    return a;
}

static double echoBeats (int i)
{
    static const double b[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0 };
    return b[jlimit (0, 6, i)];
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "algo", "Algoritmo", algoNames(), 0);
    P::pct (l, "mordida", "Mordida", 0.0f, 200.0f, 100.0f);
    P::pct (l, "retro", "Retro", 0.0f, 200.0f, 100.0f);
    P::pct (l, "metal", "Metal", 0.0f, 100.0f, 0.0f);
    P::integer (l, "octave", "Octava", -3, 2, 0);
    // operator defaults: a stack 5 -> 4 -> 3 -> 2 -> 1 and a clean sine an octave down on 6
    static const int defRatio[numOps] { 1, 1, 2, 3, 1, 0 };
    static const float defLevel[numOps] { 90, 62, 45, 30, 0, 55 };
    static const float defFb[numOps] { 0, 0, 0, 25, 0, 0 };
    static const float defD[numOps] { 600, 450, 300, 250, 300, 600 }, defS[numOps] { 100, 55, 35, 20, 30, 100 };
    for (int o = 0; o < numOps; ++o)
    {
        const String n = "Op " + String (o + 1) + " ";
        P::choice (l, opId (o, "ratio"), n + "relación"_u, ratioNames(), defRatio[o]);
        P::flt (l, opId (o, "fine"), n + "fino", P::range (-1.0f, 1.0f), 0.0f, "", 3);
        P::pct (l, opId (o, "level"), n + "nivel", 0.0f, 100.0f, defLevel[o]);
        P::pct (l, opId (o, "fb"), n + "retro", 0.0f, 100.0f, defFb[o]);
        P::choice (l, opId (o, "wave"), n + "forma", waveNames(), 0);
        P::ms (l, opId (o, "a"), n + "ataque", 0.5f, 10000.0f, 1.0f, 60.0f);
        P::ms (l, opId (o, "d"), n + "caída"_u, 5.0f, 10000.0f, defD[o], 400.0f);
        P::pct (l, opId (o, "s"), n + "sostenido", 0.0f, 100.0f, defS[o]);
        P::ms (l, opId (o, "r"), n + "relajación"_u, 5.0f, 10000.0f, 300.0f, 400.0f);
    }
    P::flt (l, "penv_amt", "Caída de afinación"_u, P::range (-48.0f, 48.0f), 0.0f, "st", 1);
    P::ms (l, "penv_time", "Caída de afinación: tiempo"_u, 1.0f, 2000.0f, 80.0f, 120.0f);
    P::pct (l, "wobble", "Wobble", 0.0f, 100.0f, 0.0f);

    P::integer (l, "voices", "Voces", 1, maxVoices, 1);
    P::toggle (l, "legato", "Legato", true);
    P::ms (l, "glide", "Glide", 0.0f, 2000.0f, 0.0f, 200.0f);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);
    P::integer (l, "unison", "Unison", 1, maxUnison, 1);
    P::flt (l, "detune", "Detune", P::range (0.0f, 100.0f), 12.0f, "ct", 0);
    P::pct (l, "spread", "Apertura", 0.0f, 100.0f, 60.0f);

    P::choice (l, "ftype", "Tipo de filtro", { "Apagado", "LP 12", "LP 24", "BP", "HP 12", "Formante" }, 2);
    P::hz (l, "cutoff", "Corte", 20.0f, 20000.0f, 900.0f);
    P::pct (l, "reso", "Resonancia", 0.0f, 95.0f, 15.0f);
    P::flt (l, "fenv", "Envolvente al filtro", P::range (-100.0f, 100.0f), 25.0f, "%", 0);
    P::pct (l, "keytrack", "Seguir teclado", 0.0f, 100.0f, 30.0f);
    P::pct (l, "vowel", "Vocal", 0.0f, 100.0f, 0.0f);
    P::ms (l, "fenv_a", "Env filtro ataque", 0.5f, 10000.0f, 2.0f, 60.0f);
    P::ms (l, "fenv_d", "Env filtro caída"_u, 5.0f, 10000.0f, 400.0f, 400.0f);
    P::pct (l, "fenv_s", "Env filtro sostenido", 0.0f, 100.0f, 30.0f);
    P::ms (l, "fenv_r", "Env filtro relajación"_u, 5.0f, 10000.0f, 300.0f, 400.0f);
    P::db (l, "drive", "Drive", 0.0f, 36.0f, 0.0f);
    P::adsr (l, 2.0f, 300.0f, 100.0f, 150.0f);

    P::choice (l, "dist_type", "Destrucción"_u, { "Apagado", "Tubo", "Plegado", "Rectificar" }, 0);
    P::pct (l, "dist_amt", "Destrucción: cantidad"_u, 0.0f, 100.0f, 30.0f);
    P::pct (l, "dist_mix", "Destrucción: mezcla"_u, 0.0f, 100.0f, 100.0f);
    P::hz (l, "mono_hz", "Graves en mono", 20.0f, 300.0f, 120.0f);
    P::pct (l, "eco_mix", "Eco", 0.0f, 100.0f, 0.0f);
    P::choice (l, "eco_div", "Eco tiempo", echoDivs(), 3);
    P::pct (l, "eco_fb", "Eco repeticiones", 0.0f, 90.0f, 35.0f);
    P::pct (l, "sala_mix", "Sala", 0.0f, 100.0f, 8.0f);
    P::pct (l, "sala_size", "Sala tamaño"_u, 0.0f, 100.0f, 40.0f);

    static const bool defSync[numLfo] { true, false };
    static const float defRate[numLfo] { 2.0f, 0.3f };
    for (int k = 0; k < numLfo; ++k)
    {
        const String n = "LFO " + String (k + 1) + " ";
        P::flt (l, lfoId (k, "rate"), n + "velocidad", P::range (0.01f, 30.0f, 2.0f), defRate[k], "Hz", 2);
        P::toggle (l, lfoId (k, "sync"), n + "a tempo", defSync[k]);
        P::choice (l, lfoId (k, "div"), n + "división"_u, RacoonProcessor::divisionNames(), 7);
        P::choice (l, lfoId (k, "shape"), n + "forma", lfoShapes(), 0);
        P::toggle (l, lfoId (k, "retrig"), n + "reinicia con cada nota", k == 0);
    }
    static const int defSrc[numSlots] { srcWheel, srcAt, srcVel, srcNone, srcNone, srcNone };
    static const int defDst[numSlots] { dstIndex, dstCutoff, dstIndex, dstNone, dstNone, dstNone };
    static const float defAmt[numSlots] { 50.0f, 30.0f, 25.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < numSlots; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        P::choice (l, slotId (k, "src"), n + "fuente", sourceNames(), defSrc[k]);
        P::choice (l, slotId (k, "dst"), n + "destino", destNames(), defDst[k]);
        P::flt (l, slotId (k, "amt"), n + "cantidad", P::range (-100.0f, 100.0f), defAmt[k], "%", 0);
    }
    P::choice (l, "quality", "Calidad", { "ECO", "NORMAL", "HQ" }, 1);
    P::db (l, "volume", "Volumen", -36.0f, 6.0f, -8.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Opossum", "FM", Colour (0xffa9a39f), PluginKind::instrument, 1, 2.0, false, false },
                       layout())
{
    Sine::get();
    impl = std::make_unique<Impl>();
    static const char* opNames[9] { "ratio", "fine", "level", "fb", "wave", "a", "d", "s", "r" };
    static const char* lfoNames[5] { "rate", "sync", "div", "shape", "retrig" };
    static const char* slotNames[3] { "src", "dst", "amt" };
    for (int o = 0; o < numOps; ++o) for (int j = 0; j < 9; ++j) pOp[o][j] = param (opId (o, opNames[j]));
    for (int k = 0; k < numLfo; ++k) for (int j = 0; j < 5; ++j) pLfo[k][j] = param (lfoId (k, lfoNames[j]));
    for (int k = 0; k < numSlots; ++k) for (int j = 0; j < 3; ++j) pSlot[k][j] = param (slotId (k, slotNames[j]));
}

void Processor::prepareDsp (double rate, int block)
{
    const int maxBlock = jmax (block, 1024);
    impl->os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    impl->os->initProcessing ((size_t) maxBlock);
    setLatencySamples (roundToInt (impl->os->getLatencyInSamples()));
    impl->zeros.setSize (2, maxBlock, false, true, true);
    mix.setSize (2, maxBlock, false, false, true);
    impl->wet.setSize (2, maxBlock, false, false, true);
    impl->verb.setSampleRate (rate);
    for (auto& e : impl->echo) e.prepare ((int) (rate * 4.2) + 8);
    impl->echoTime.prepare (rate, 120.0);
    impl->echoTime.reset ((float) (echoBeats ((int) value ("eco_div")) * 60.0 / jmax (20.0, bpm()) * rate));
    impl->volSmooth.prepare (rate, 30.0);
    impl->volSmooth.reset (dsp::dbToGain (value ("volume")));
    for (auto& d : impl->dc) d.prepare (rate * 2.0);
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& v : voices) v = Voice();
    if (impl->os != nullptr) impl->os->reset();
    impl->verb.reset();
    for (auto& e : impl->echo) e.clear();
    impl->echoLp[0] = impl->echoLp[1] = 0.0f;
    impl->distX[0] = impl->distX[1] = 0.0f;
    impl->monoLp[0] = impl->monoLp[1] = 0.0f;
    for (auto& f : impl->sideHp) f.reset();
    for (auto& d : impl->dc) d.reset();
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
            level += (float) n / (float) (jmax (0.0005f, a) * sr);
            if (level >= 1.0f) { level = 1.0f; stage = decay; }
            break;
        case decay:
            level = s + (level - s) * (float) std::exp (-6.9 * n / (jmax (0.001f, d) * sr));
            if (std::abs (level - s) < 1.0e-4f) { level = s; stage = sustain; }
            break;
        case sustain:
            level += (s - level) * (float) (1.0 - std::exp (-(double) n / (0.01 * sr)));
            if (s <= 0.0f && level < 1.0e-4f) { level = 0.0f; stage = idle; }
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
    if (newCycle) { v.lfoRand[k][0] = v.lfoRand[k][1]; v.lfoRand[k][1] = v.rng.nextBipolar(); }
    switch ((int) rd (pLfo[k][3]))
    {
        case 0: return std::sin (MathConstants<float>::twoPi * ph);
        case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case 2: return 1.0f - 2.0f * ph;
        case 3: return 2.0f * ph - 1.0f;
        case 4: return ph < 0.5f ? 1.0f : -1.0f;
        default: return v.lfoRand[k][1];
    }
}

// ===================================================================== voices
void Processor::noteOn (int note, float vel)
{
    const int limit = (int) value ("voices");
    const float glideMs = value ("glide");
    if (limit == 1 && value ("legato") > 0.5f)
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.held)
            {
                v.glideFrom = v.pitch;
                v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
                v.note = note; v.velocity = vel;
                lastPitch = (float) note;
                return;
            }

    int playing = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++playing;
    while (playing >= limit)
    {
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.stealing && ! v.held && ! v.sustained && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr)
            for (auto& v : voices) if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) break;
        victim->stealing = true;
        --playing;
    }
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr) for (auto& v : voices) if (slot == nullptr || v.stealGain < slot->stealGain) slot = &v;

    auto& v = *slot;
    const float gf = glideMs > 0.5f ? lastPitch : (float) note;
    v = Voice();
    v.active = true; v.held = true; v.note = note; v.velocity = vel;
    v.order = ++orderCounter;
    v.rng.seed ((uint32) (0x9e3779b9u * (uint32) (++noteCounter) + (uint32) note * 7919u));
    v.random = v.rng.nextBipolar();
    v.pitch = (float) note; v.glideFrom = gf; v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
    for (int u = 0; u < maxUnison; ++u) v.uniJit[u] = v.rng.nextBipolar() * 2.0f;   // phases start at 0: every hit the same
    for (int k = 0; k < numLfo; ++k) { v.lfoRand[k][0] = v.rng.nextBipolar(); v.lfoRand[k][1] = v.rng.nextBipolar(); }
    v.amp.start(); v.fenv.start();
    for (auto& e : v.op) e.start();
    lastPitch = (float) note;
}

void Processor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            v.held = false;
            if (pedal) v.sustained = true;
            else { v.amp.noteOff(); v.fenv.noteOff(); for (auto& e : v.op) e.noteOff(); }
        }
}

void Processor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isChannelPressure()) channelPressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch()) { for (auto& v : voices) if (v.active && v.note == m.getNoteNumber()) v.pressure = (float) m.getAfterTouchValue() / 127.0f; }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1) wheel = (float) val / 127.0f;
        else if (cc == 64)
        {
            pedal = val >= 64;
            if (! pedal) for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; v.amp.noteOff(); v.fenv.noteOff(); for (auto& e : v.op) e.noteOff(); }
        }
        else if (cc == 120) { for (auto& v : voices) if (v.active) v.stealing = true; }
        else if (cc == 123) { for (auto& v : voices) if (v.active) { v.held = v.sustained = false; v.amp.noteOff(); v.fenv.noteOff(); for (auto& e : v.op) e.noteOff(); } }
    }
    else if (m.isAllNotesOff()) { for (auto& v : voices) if (v.active) { v.held = v.sustained = false; v.amp.noteOff(); v.fenv.noteOff(); for (auto& e : v.op) e.noteOff(); } }
    else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; }
}

// renders one voice into the oversampled bus (L, R), `n` base-rate samples starting at base sample `start`
void Processor::renderVoice (Voice& v, float* L, float* R, int start, int total, int cr, double osr, int factor)
{
    const auto& S = Sine::get();
    const double srate = sr;
    const auto& A = algo ((int) value ("algo"));
    const int quality = (int) value ("quality");
    const int uni = quality == 0 ? jmin (4, (int) value ("unison")) : (int) value ("unison");
    const float spread = value ("spread") * 0.01f;
    const float detune = value ("detune");
    const float uniNorm = 1.0f / std::sqrt ((float) uni);
    const float bendRange = value ("bendrange");
    const float glideMs = value ("glide");
    const int ftype = (int) value ("ftype");
    const float octave = value ("octave") * 12.0f;
    const float penvAmt = value ("penv_amt"), penvTime = jmax (0.001f, value ("penv_time") * 0.001f);
    const float wob = value ("wobble") * 0.01f;

    // the wiring: for each operator, who modulates it; which operators modulate anything (Metal acts on those)
    int modsOf[numOps][numOps]; int numMods[numOps] {};
    bool isMod[numOps] {};
    for (int e = 0; e < A.numEdges; ++e) { modsOf[A.to[e]][numMods[A.to[e]]++] = A.from[e]; isMod[A.from[e]] = true; }
    int numCarriers = 0;
    for (int o = 0; o < numOps; ++o) if (A.carriers & (1 << o)) ++numCarriers;
    const float carrierNorm = 1.0f / std::sqrt ((float) jmax (1, numCarriers));

    float ratio[numOps], level[numOps], fbAmt[numOps], opA[numOps], opD[numOps], opS[numOps], opR[numOps]; int wv[numOps];
    for (int o = 0; o < numOps; ++o)
    {
        ratio[o] = ratioValue ((int) rd (pOp[o][0])) + rd (pOp[o][1]);
        level[o] = rd (pOp[o][2]) * 0.01f; level[o] *= level[o];
        fbAmt[o] = rd (pOp[o][3]) * 0.01f;
        wv[o] = (int) rd (pOp[o][4]);
        opA[o] = rd (pOp[o][5]) * 0.001f; opD[o] = rd (pOp[o][6]) * 0.001f; opS[o] = rd (pOp[o][7]) * 0.01f; opR[o] = rd (pOp[o][8]) * 0.001f;
    }
    const float aA = value ("adsr_a") * 0.001f, aD = value ("adsr_d") * 0.001f, aS = value ("adsr_s") * 0.01f, aR = value ("adsr_r") * 0.001f;
    // the heard operators never let go before the volume envelope does (the family's Relajación stays in charge)
    for (int o = 0; o < numOps; ++o) if (A.carriers & (1 << o)) opR[o] = jmax (opR[o], aR);
    const float fA = value ("fenv_a") * 0.001f, fD = value ("fenv_d") * 0.001f, fS = value ("fenv_s") * 0.01f, fR = value ("fenv_r") * 0.001f;
    float lfoRate[numLfo]; bool lfoRetrig[numLfo];
    for (int k = 0; k < numLfo; ++k)
    {
        lfoRate[k] = rd (pLfo[k][1]) > 0.5f ? (float) (1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm())) : rd (pLfo[k][0]);
        lfoRetrig[k] = rd (pLfo[k][4]) > 0.5f;
    }
    int slotSrc[numSlots], slotDst[numSlots]; float slotAmt[numSlots];
    for (int k = 0; k < numSlots; ++k) { slotSrc[k] = (int) rd (pSlot[k][0]); slotDst[k] = (int) rd (pSlot[k][1]); slotAmt[k] = rd (pSlot[k][2]) * 0.01f; }

    const float stealStep = (float) (1.0 / (0.005 * osr));
    for (int done = 0; done < total && v.active; )
    {
        const int n = jmin (cr, total - done);
        const int no = n * factor;

        // ---- control rate
        const float env = v.amp.advance (n, aA, aD, aS, aR, srate);
        const float fenv = v.fenv.advance (n, fA, fD, fS, fR, srate);
        float opEnv[numOps];
        for (int o = 0; o < numOps; ++o) opEnv[o] = v.op[o].advance (n, opA[o], opD[o], opS[o], opR[o], srate);
        v.penvAge += (float) (n / srate);
        float lfo[numLfo];
        for (int k = 0; k < numLfo; ++k)
        {
            float ph; bool wrapped;
            if (lfoRetrig[k])
            {
                v.lfoPhase[k] += lfoRate[k] * n / srate;
                wrapped = v.lfoPhase[k] >= 1.0;
                v.lfoPhase[k] -= std::floor (v.lfoPhase[k]);
                ph = (float) v.lfoPhase[k];
            }
            else
            {
                const double g = globalLfo[k] + lfoRate[k] * (start + done) / srate;
                ph = (float) (g - std::floor (g));
                wrapped = ph < (float) v.lfoPhase[k];
                v.lfoPhase[k] = ph;
            }
            lfo[k] = lfoValue (v, k, ph, wrapped);
        }
        float src[numSources] {};
        src[srcLfo1] = lfo[0]; src[srcLfo2] = lfo[1]; src[srcFenv] = fenv; src[srcVel] = v.velocity;
        src[srcAt] = jmax (v.pressure, channelPressure); src[srcWheel] = wheel;
        src[srcKey] = ((float) v.note - 36.0f) / 48.0f; src[srcRandom] = v.random;
        float mod[numDests] {};
        for (int k = 0; k < numSlots; ++k) if (slotSrc[k] != srcNone && slotDst[k] != dstNone) mod[slotDst[k]] += src[slotSrc[k]] * slotAmt[k];

        // the wobble: LFO 1 closes the index and the filter together
        const float l01 = (lfo[0] + 1.0f) * 0.5f;
        const float wobIdx = 1.0f - 0.9f * wob * (1.0f - l01);
        const float wobCut = std::pow (2.0f, 4.0f * wob * (l01 - 0.5f));

        if (v.glidePos < 1.0f) v.glidePos = jmin (1.0f, v.glidePos + (float) (n / (jmax (1.0f, glideMs) * 0.001f * srate)));
        v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glidePos;
        const float penv = penvAmt * std::exp (-v.penvAge / penvTime);
        const float pitch = v.pitch + octave + bend * bendRange + penv + mod[dstPitch] * 24.0f;
        const float fNote = 440.0f * std::pow (2.0f, (pitch - 69.0f) / 12.0f);

        const float index = jmax (0.0f, value ("mordida") * 0.01f + mod[dstIndex] * 2.0f) * wobIdx;
        const float retro = jmax (0.0f, value ("retro") * 0.01f + mod[dstFeedback] * 2.0f);
        const float metal = jlimit (0.0f, 1.0f, value ("metal") * 0.01f + mod[dstMetal]);
        const float velScale = 0.4f + 0.6f * v.velocity;
        float opAmp[numOps], inc[maxUnison][numOps], fbBeta[numOps];
        for (int o = 0; o < numOps; ++o)
        {
            opAmp[o] = level[o] * opEnv[o] * (isMod[o] ? velScale : 1.0f);
            fbBeta[o] = 1.8f * fbAmt[o] * retro;
            const float r = ratio[o] + (isMod[o] ? metal * metalOffset[o] * ratio[o] : 0.0f);
            for (int u = 0; u < uni; ++u)
            {
                const float cents = (uni == 1 ? 0.0f : detune * ((float) u / (float) (uni - 1) - 0.5f) + v.uniJit[u]);
                inc[u][o] = jmin (0.45f, (float) (fNote * r * std::pow (2.0f, cents / 1200.0f) / osr));
            }
        }
        float uL[maxUnison], uR[maxUnison];
        for (int u = 0; u < uni; ++u)
        {
            const float p = uni == 1 ? 0.0f : jlimit (-1.0f, 1.0f, spread * ((float) u / (float) (uni - 1) * 2.0f - 1.0f));
            uL[u] = std::cos ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
            uR[u] = std::sin ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
        }

        // filter (at the oversampled rate)
        const float oct = value ("keytrack") * 0.01f * ((float) v.note - 36.0f) / 12.0f + value ("fenv") * 0.01f * fenv * 6.0f + mod[dstCutoff] * 6.0f;
        const float fc = jlimit (20.0f, (float) (osr * 0.45), value ("cutoff") * wobCut * std::pow (2.0f, oct));
        const float gT = dsp::SVF::gFor (fc, osr);
        const float reso = jlimit (0.0f, 0.95f, value ("reso") * 0.01f + mod[dstReso]);
        const float k1 = 2.0f - 2.0f * reso;
        float fg[3] {}, fk = 2.0f - 2.0f * jlimit (0.0f, 0.97f, 0.82f + reso * 0.16f);   // narrow bands: the vowel reads
        if (ftype == 5)
        {
            const float vw = jlimit (0.0f, 1.0f, value ("vowel") * 0.01f + mod[dstVowel]) * 4.0f;
            const int a0 = jmin (3, (int) vw);
            const float t = vw - (float) a0;
            const float shift = jlimit (-1.5f, 1.5f, std::log2 (jmax (20.0f, value ("cutoff") * wobCut) / 1000.0f));
            for (int b = 0; b < 3; ++b)
                fg[b] = dsp::SVF::gFor ((formants[a0][b] * (1.0f - t) + formants[a0 + 1][b] * t) * std::pow (2.0f, shift), osr);
        }
        const float driveG = dsp::dbToGain (jlimit (0.0f, 48.0f, value ("drive") + mod[dstDrive] * 24.0f));
        const bool useDrive = driveG > 1.01f;
        const float driveComp = 1.0f / std::sqrt (driveG);

        const float ampTarget = env * jlimit (0.0f, 2.0f, 1.0f + mod[dstLevel]) * 0.35f * (0.3f + 0.7f * v.velocity);
        const float pan = jlimit (-1.0f, 1.0f, mod[dstPan]);
        const float panLT = std::cos ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;
        const float panRT = std::sin ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;

        float viewNow[numOps] {};
        const float ampStart = v.ampNow, gStart = v.gNow > 0.0f ? v.gNow : gT;
        const float plStart = v.panL, prStart = v.panR;
        float opStart[numOps];
        for (int o = 0; o < numOps; ++o) opStart[o] = v.opAmpNow[o];
        const float invN = 1.0f / (float) no;
        const float beta = 8.0f * index;
        for (int i = 0; i < no; ++i)
        {
            const float w = (float) (i + 1) * invN;
            float oa[numOps];
            for (int o = 0; o < numOps; ++o) oa[o] = opStart[o] + (opAmp[o] - opStart[o]) * w;
            float sl = 0.0f, sr2 = 0.0f;
            for (int u = 0; u < uni; ++u)
            {
                float* out = v.out[u];
                float carrier = 0.0f;
                // 6 down to 1: a link downwards uses this sample, upwards (the Anillo) the previous one
                for (int o = numOps - 1; o >= 0; --o)
                {
                    if (oa[o] < 1.0e-6f && ! (A.carriers & (1 << o))) { out[o] = 0.0f; continue; }
                    float pm = 0.0f;
                    for (int m = 0; m < numMods[o]; ++m) pm += beta * out[modsOf[o][m]];
                    if (fbBeta[o] > 0.0f) pm += fbBeta[o] * (v.fb1[u][o] + v.fb2[u][o]) * 0.5f;
                    const float y = wave (S, wv[o], v.phase[u][o] + pm * 0.15915494f);
                    v.fb2[u][o] = v.fb1[u][o]; v.fb1[u][o] = y;
                    out[o] = y * oa[o];
                    v.phase[u][o] += inc[u][o];
                    if (v.phase[u][o] >= 1.0f) v.phase[u][o] -= 1.0f;
                    if (A.carriers & (1 << o)) carrier += out[o];
                }
                carrier *= carrierNorm * uniNorm;
                sl += carrier * uL[u]; sr2 += carrier * uR[u];
            }
            float in2[2] { sl, sr2 }, res[2];
            const float g = gStart + (gT - gStart) * w;
            for (int c = 0; c < 2; ++c)
            {
                float x = in2[c];
                if (useDrive) x = racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q); }, x * driveG, v.driveX[c]) * driveComp;
                switch (ftype)
                {
                    case 0: break;
                    case 1: v.f1[c].process (x, g, k1); x = v.f1[c].lp; break;
                    case 2: v.f1[c].process (x, g, k1); v.f2[c].process (v.f1[c].lp, g, 1.414f); x = v.f2[c].lp; break;
                    case 3: v.f1[c].process (x, g, k1); x = v.f1[c].bp * k1; break;
                    case 4: v.f1[c].process (x, g, k1); x = v.f1[c].hp; break;
                    default:
                    {
                        float y = 0.0f;
                        for (int b = 0; b < 3; ++b) { v.fm[b][c].process (x, fg[b], fk); y += v.fm[b][c].bp * fk * formantGain[b]; }
                        x = y * 1.6f;
                        break;
                    }
                }
                res[c] = x;
            }
            float gain = ampStart + (ampTarget - ampStart) * w;
            if (v.stealing)
            {
                v.stealGain -= stealStep;
                if (v.stealGain <= 0.0f) { v.stealGain = 0.0f; v.active = false; }
                gain *= v.stealGain;
            }
            const float pl = plStart + (panLT - plStart) * w, pr = prStart + (panRT - prStart) * w;
            const int at = (start + done) * factor + i;
            L[at] += res[0] * gain * pl;
            R[at] += res[1] * gain * pr;
            if (! v.active) break;
        }
        for (int o = 0; o < numOps; ++o) { v.opAmpNow[o] = opAmp[o]; viewNow[o] = opAmp[o]; opView[o].store (jmax (opView[o].load(), viewNow[o])); }
        v.ampNow = ampTarget; v.gNow = gT; v.panL = panLT; v.panR = panRT;
        if (v.amp.stage == Env::idle) v.active = false;
        done += n;
    }
}

// ===================================================================== block
void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    if (mix.getNumSamples() < n) mix.setSize (2, n, false, false, true);
    if (impl->zeros.getNumSamples() < n) impl->zeros.setSize (2, n, false, true, true);
    for (auto& o : opView) o.store (o.load() * 0.8f);

    const int factor = 2;
    const double osr = sr * factor;
    const int quality = (int) value ("quality");
    const int cr = quality == 0 ? 32 : quality == 1 ? 16 : 8;

    // the oversampled bus: the up path of silence gives the block to write into, then it comes down through the filter
    impl->zeros.clear (0, n);
    juce::dsp::AudioBlock<float> zb (impl->zeros.getArrayOfWritePointers(), 2, 0, (size_t) n);
    auto up = impl->os->processSamplesUp (zb);
    float* L = up.getChannelPointer (0);
    float* R = up.getChannelPointer (1);
    std::fill (L, L + n * factor, 0.0f);
    std::fill (R, R + n * factor, 0.0f);

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices) if (v.active) renderVoice (v, L, R, pos, end - pos, cr, osr, factor);
        pos = end;
    };
    for (const auto ev : midi) { renderTo (jlimit (0, n, ev.samplePosition)); handleMidi (ev.getMessage()); }
    renderTo (n);

    // destruction on the summed bus, still oversampled
    const int dist = (int) value ("dist_type");
    if (dist > 0)
    {
        const float amt = value ("dist_amt") * 0.01f, mx = value ("dist_mix") * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            float* x = c == 0 ? L : R;
            for (int i = 0; i < n * factor; ++i)
            {
                const float in = x[i];
                float y;
                if (dist == 1)
                {
                    const float g = 1.0f + amt * 9.0f;
                    y = (racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q + 0.2f) - 0.19738f; }, in * g, impl->distX[c])) / std::sqrt (g);
                }
                else if (dist == 2)
                {
                    const float k = 1.0f + amt * 11.0f;
                    y = std::sin (in * k) / (0.6f + 0.4f * std::sqrt (k));
                }
                else y = in + amt * (std::abs (in) - in);
                x[i] = impl->dc[c].process (in + mx * (y - in));
            }
        }
    }
    juce::dsp::AudioBlock<float> mb (mix.getArrayOfWritePointers(), 2, 0, (size_t) n);
    impl->os->processSamplesDown (mb);

    for (int k = 0; k < numLfo; ++k)
    {
        const double rate = rd (pLfo[k][1]) > 0.5f ? 1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm()) : rd (pLfo[k][0]);
        globalLfo[k] += rate * n / sr;
        globalLfo[k] -= std::floor (globalLfo[k]);
    }

    auto* ML = mix.getWritePointer (0);
    auto* MR = mix.getWritePointer (1);
    // the lows in mono (where the sub lives)
    const float monoHz = value ("mono_hz");
    if (monoHz > 21.0f)
    {
        // the side (left minus right) loses everything under the frequency, 24 dB per octave; the middle stays whole
        const float g = dsp::SVF::gFor (monoHz, sr);
        for (int i = 0; i < n; ++i)
        {
            const float mid = (ML[i] + MR[i]) * 0.5f;
            float side = (ML[i] - MR[i]) * 0.5f;
            impl->sideHp[0].process (side, g, 1.414f); side = impl->sideHp[0].hp;
            impl->sideHp[1].process (side, g, 1.414f); side = impl->sideHp[1].hp;
            ML[i] = mid + side;
            MR[i] = mid - side;
        }
    }
    // echo (dark repeats) and room
    const float eMix = value ("eco_mix") * 0.01f;
    {
        impl->echoTime.target = (float) jmin (echoBeats ((int) value ("eco_div")) * 60.0 / jmax (20.0, bpm()) * sr, sr * 4.0);
        const float fb = value ("eco_fb") * 0.01f;
        const float lpC = 1.0f - std::exp (-MathConstants<float>::twoPi * 3000.0f / (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float d = impl->echoTime.next();
            if (eMix <= 0.001f) { impl->echo[0].push (0.0f); impl->echo[1].push (0.0f); continue; }
            for (int c = 0; c < 2; ++c)
            {
                float* x = c == 0 ? ML : MR;
                const float t = impl->echo[c].tap (d);
                impl->echoLp[c] += lpC * (t - impl->echoLp[c]);
                impl->echo[c].push (x[i] + impl->echoLp[c] * fb);
                x[i] += impl->echoLp[c] * eMix;
            }
        }
    }
    const float sMix = value ("sala_mix") * 0.01f;
    if (sMix > 0.001f)
    {
        sdsp::Verb::Parameters vp;
        vp.roomSize = value ("sala_size") * 0.01f; vp.damping = 0.6f; vp.wetLevel = 0.33f; vp.dryLevel = 0.0f; vp.width = 1.0f;
        impl->verb.setParameters (vp);
        if (impl->wet.getNumSamples() < n) impl->wet.setSize (2, n, false, false, true);
        impl->wet.copyFrom (0, 0, mix, 0, 0, n);
        impl->wet.copyFrom (1, 0, mix, 1, 0, n);
        impl->verb.processStereo (impl->wet.getWritePointer (0), impl->wet.getWritePointer (1), n);
        mix.addFrom (0, 0, impl->wet, 0, 0, n, sMix);
        mix.addFrom (1, 0, impl->wet, 1, 0, n, sMix);
    }
    impl->volSmooth.target = dsp::dbToGain (value ("volume"));
    for (int i = 0; i < n; ++i) { const float vol = impl->volSmooth.next(); ML[i] *= vol; MR[i] *= vol; }

    if (ch == 1) { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.addFrom (0, 0, mix, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f); }
    else { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.copyFrom (1, 0, mix, 1, 0, n); }
    int count = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++count;
    activeVoices.store (count);
}

// ===================================================================== presets (all of them low: bells drop an octave or two)
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    auto op = [] (int o, int ratio, float level, float fb = 0.0f, float d = 400.0f, float s = 50.0f)
    {
        return V { { opId (o, "ratio"), (float) ratio }, { opId (o, "level"), level }, { opId (o, "fb"), fb }, { opId (o, "d"), d }, { opId (o, "s"), s } };
    };
    auto join = [] (std::initializer_list<V> parts) { V out; for (auto& p : parts) out.insert (out.end(), p.begin(), p.end()); return out; };
    const int r05 = 0, r1 = 1, r2 = 2, r3 = 3, r4 = 4, r5 = 5, r7 = 7, r9 = 9;
    std::vector<FactoryPreset> p;
    p.push_back ({ "Inicial", "Bajo metálico"_u, {} });
    // metallic basses
    p.push_back ({ "Yunque", "Bajo metálico"_u, join ({ { { "algo", 0 }, { "mordida", 120 }, { "cutoff", 700 }, { "fenv", 35 } },
                                                        op (1, r1, 70, 0, 250, 30), op (2, r3, 45, 0, 200, 20), op (3, r1, 35, 40, 300, 30), op (5, r05, 60, 0, 600, 100) }) });
    p.push_back ({ "Tubería"_u, "Bajo metálico"_u, join ({ { { "algo", 2 }, { "mordida", 90 }, { "metal", 25 }, { "cutoff", 600 } },
                                                          op (1, r2, 60, 0, 300, 40), op (2, r1, 40, 30), op (4, r3, 45, 0, 250, 25), op (5, r1, 30, 50) }) });
    p.push_back ({ "Cadena", "Bajo metálico"_u, join ({ { { "algo", 1 }, { "mordida", 110 }, { "cutoff", 800 }, { "dist_type", 1 }, { "dist_amt", 25 } },
                                                        op (1, r1, 60, 0, 220, 25), op (2, r2, 45), op (3, r1, 35), op (4, r3, 25, 30), op (5, r1, 20) }) });
    p.push_back ({ "Remache", "Bajo metálico"_u, join ({ { { "algo", 0 }, { "mordida", 140 }, { "penv_amt", 12 }, { "penv_time", 40 }, { "cutoff", 900 } },
                                                         op (1, r1, 75, 0, 150, 10), op (2, r4, 40, 0, 120, 0), op (5, r05, 65, 0, 500, 100) }) });
    // growls
    p.push_back ({ "Gruñido de alcantarilla"_u, "Gruñido"_u, join ({ { { "algo", 11 }, { "mordida", 130 }, { "retro", 150 }, { "wobble", 60 }, { "ftype", 5 }, { "vowel", 20 },
                                                                     { "cutoff", 900 }, { "reso", 40 }, { "dist_type", 1 }, { "dist_amt", 40 } },
                                                                   op (1, r1, 65, 0, 600, 80), op (2, r1, 50, 0, 600, 70), op (3, r2, 40, 20), op (4, r1, 45, 60), op (5, r3, 30) }) });
    p.push_back ({ "Mandíbula"_u, "Gruñido"_u, join ({ { { "algo", 1 }, { "mordida", 150 }, { "retro", 170 }, { "wobble", 80 }, { "lfo1_div", 10 }, { "cutoff", 700 },
                                                         { "reso", 30 }, { "dist_type", 2 }, { "dist_amt", 35 } },
                                                       op (1, r1, 60, 0, 600, 90), op (2, r1, 55, 0, 600, 80), op (3, r2, 45, 0, 600, 70), op (4, r1, 35, 70), op (5, r05, 25) }) });
    p.push_back ({ "Hiena dormida", "Gruñido"_u, join ({ { { "algo", 11 }, { "mordida", 110 }, { "retro", 130 }, { "wobble", 40 }, { "lfo1_div", 4 }, { "ftype", 5 },
                                                         { "vowel", 0 }, { "cutoff", 600 }, { "reso", 45 } },
                                                       op (1, r1, 65, 0, 600, 90), op (2, r2, 45), op (3, r1, 40, 40), op (4, r1, 40, 40), op (5, r3, 30) }) });
    p.push_back ({ "Motor frío"_u, "Gruñido"_u, join ({ { { "algo", 10 }, { "mordida", 80 }, { "retro", 120 }, { "cutoff", 650 }, { "dist_type", 3 }, { "dist_amt", 40 } },
                                                       op (1, r1, 60, 0, 600, 90), op (2, r1, 40), op (3, r2, 35), op (4, r1, 30, 50), op (5, r1, 25) }) });
    // dark bells (an octave or two down)
    p.push_back ({ "Campana hundida", "Campana oscura", join ({ { { "algo", 7 }, { "octave", -2 }, { "voices", 8 }, { "legato", 0 }, { "mordida", 70 }, { "metal", 55 },
                                                                { "ftype", 1 }, { "cutoff", 2200 }, { "sala_mix", 35 }, { "sala_size", 70 }, { "adsr_d", 3000 }, { "adsr_s", 0 }, { "adsr_r", 2500 } },
                                                              op (0, r1, 85, 0, 3000, 0), op (1, r3, 45, 0, 1500, 0), op (2, r7, 25, 0, 800, 0), op (3, r1, 70, 0, 2500, 0),
                                                              op (4, r5, 35, 0, 1200, 0), op (5, r9, 20, 0, 600, 0) }) });
    p.push_back ({ "Iglesia vacía"_u, "Campana oscura", join ({ { { "algo", 4 }, { "octave", -1 }, { "voices", 8 }, { "legato", 0 }, { "mordida", 60 }, { "metal", 70 },
                                                               { "ftype", 1 }, { "cutoff", 1800 }, { "sala_mix", 45 }, { "sala_size", 85 }, { "adsr_d", 4000 }, { "adsr_s", 0 }, { "adsr_r", 3000 } },
                                                             op (0, r1, 85, 0, 4000, 0), op (1, r2, 35, 0, 2000, 0), op (2, r3, 30, 0, 1500, 0), op (3, r5, 20, 0, 900, 0), op (4, r7, 15, 0, 700, 0) }) });
    p.push_back ({ "Plomo", "Campana oscura", join ({ { { "algo", 3 }, { "octave", -1 }, { "voices", 8 }, { "legato", 0 }, { "mordida", 55 }, { "metal", 40 }, { "ftype", 1 },
                                                      { "cutoff", 1500 }, { "sala_mix", 25 }, { "adsr_d", 2000 }, { "adsr_s", 0 }, { "adsr_r", 1500 } },
                                                    op (0, r1, 80, 0, 2000, 0), op (1, r4, 35, 0, 900, 0), op (2, r2, 60, 0, 1800, 0), op (3, r7, 25, 0, 600, 0), op (4, r1, 50, 0, 1500, 0), op (5, r3, 30, 0, 800, 0) }) });
    p.push_back ({ "Faro en la niebla", "Campana oscura", join ({ { { "algo", 7 }, { "octave", -1 }, { "voices", 6 }, { "legato", 0 }, { "mordida", 45 }, { "metal", 30 },
                                                                  { "ftype", 1 }, { "cutoff", 1400 }, { "eco_mix", 25 }, { "sala_mix", 35 }, { "adsr_a", 15 }, { "adsr_d", 3500 }, { "adsr_s", 10 }, { "adsr_r", 2500 } },
                                                                op (0, r1, 80, 0, 3500, 10), op (1, r2, 30), op (2, r3, 20), op (3, r1, 60, 0, 3000, 10), op (4, r4, 25), op (5, r1, 15) }) });
    // aggressive leads (low)
    p.push_back ({ "Sierra de hierro", "Lead agresivo", join ({ { { "algo", 2 }, { "mordida", 120 }, { "retro", 120 }, { "unison", 3 }, { "detune", 18 }, { "cutoff", 1600 },
                                                                { "dist_type", 1 }, { "dist_amt", 30 }, { "eco_mix", 15 }, { "glide", 60 } },
                                                              op (1, r1, 55, 40, 800, 70), op (2, r2, 40), op (4, r1, 55, 40, 800, 70), op (5, r3, 30) }) });
    p.push_back ({ "Alambre de púas"_u, "Lead agresivo", join ({ { { "algo", 5 }, { "mordida", 100 }, { "metal", 20 }, { "cutoff", 1400 }, { "reso", 25 }, { "dist_type", 2 }, { "dist_amt", 25 } },
                                                                 op (3, r2, 50, 40), op (4, r1, 30), op (5, r3, 25), op (1, r1, 70, 0, 600, 80), op (2, r1, 60, 0, 600, 80) }) });
    p.push_back ({ "Escape", "Lead agresivo", join ({ { { "algo", 0 }, { "mordida", 90 }, { "retro", 100 }, { "glide", 90 }, { "cutoff", 1200 }, { "fenv", 40 }, { "eco_mix", 20 } },
                                                    op (1, r1, 60, 0, 500, 60), op (2, r2, 40), op (3, r1, 30, 60), op (5, r05, 40, 0, 600, 100) }) });
    p.push_back ({ "Neón roto"_u, "Lead agresivo", join ({ { { "algo", 3 }, { "mordida", 85 }, { "unison", 4 }, { "detune", 22 }, { "cutoff", 1300 }, { "dist_type", 3 }, { "dist_amt", 30 } },
                                                          op (1, r1, 50, 30), op (3, r2, 45, 20), op (5, r1, 40, 30) }) });
    // sub and reese
    p.push_back ({ "Sub de concreto", "Sub y reese", join ({ { { "algo", 9 }, { "mordida", 0 }, { "cutoff", 300 }, { "ftype", 1 }, { "sala_mix", 0 } },
                                                             op (0, r1, 85, 0, 600, 100), op (1, r2, 18, 0, 600, 100), op (2, r1, 0), op (3, r1, 0), op (4, r1, 0), op (5, r05, 0) }) });
    p.push_back ({ "Reese de túnel"_u, "Sub y reese", join ({ { { "algo", 2 }, { "mordida", 60 }, { "unison", 4 }, { "detune", 30 }, { "spread", 80 }, { "cutoff", 500 }, { "reso", 20 },
                                                               { "lfo2_rate", 0.2f }, { "m4_src", srcLfo2 }, { "m4_dst", dstCutoff }, { "m4_amt", 20 }, { "sala_mix", 0 } },
                                                             op (1, r1, 30, 30, 600, 100), op (4, r1, 30, 30, 600, 100) }) });
    p.push_back ({ "Pulso subterráneo"_u, "Sub y reese", join ({ { { "algo", 0 }, { "mordida", 50 }, { "penv_amt", 24 }, { "penv_time", 25 }, { "cutoff", 400 }, { "sala_mix", 0 } },
                                                                op (1, r1, 40, 0, 120, 0), op (5, r05, 80, 0, 600, 100) }) });
    // drones
    p.push_back ({ "Respiro de hierro", "Drone", join ({ { { "algo", 6 }, { "octave", -1 }, { "voices", 6 }, { "legato", 0 }, { "mordida", 60 }, { "metal", 35 }, { "ftype", 1 },
                                                         { "cutoff", 1100 }, { "lfo2_rate", 0.05f }, { "m4_src", srcLfo2 }, { "m4_dst", dstCutoff }, { "m4_amt", 15 },
                                                         { "sala_mix", 40 }, { "sala_size", 80 }, { "adsr_a", 600 }, { "adsr_r", 3000 }, { "volume", -4 } },
                                                       op (1, r1, 40), op (2, r2, 30), op (3, r1, 35), op (4, r3, 25), op (0, r1, 90, 0, 600, 100) }) });
    p.push_back ({ "Mina abandonada", "Drone", join ({ { { "algo", 10 }, { "octave", -2 }, { "voices", 4 }, { "legato", 0 }, { "mordida", 50 }, { "retro", 80 }, { "cutoff", 900 },
                                                       { "eco_mix", 30 }, { "eco_fb", 55 }, { "sala_mix", 45 }, { "sala_size", 85 }, { "adsr_a", 2500 }, { "adsr_r", 4000 } },
                                                     op (1, r1, 35), op (2, r2, 30), op (3, r1, 25, 30), op (4, r3, 20), op (5, r1, 20) }) });
    p.push_back ({ "Ballena", "Drone", join ({ { { "algo", 9 }, { "octave", -2 }, { "voices", 4 }, { "legato", 0 }, { "ftype", 1 }, { "cutoff", 700 }, { "sala_mix", 50 },
                                               { "sala_size", 90 }, { "adsr_a", 2000 }, { "adsr_r", 4000 }, { "lfo2_rate", 0.08f }, { "m4_src", srcLfo2 }, { "m4_dst", dstPitch }, { "m4_amt", 1 } },
                                             op (0, r1, 70, 0, 600, 100), op (1, r2, 30, 0, 600, 100), op (2, r3, 18, 0, 600, 100), op (3, r5, 10, 0, 600, 100), op (4, r1, 0), op (5, r05, 40, 0, 600, 100) }) });
    return p;
}

// ===================================================================== example for Play: a half-time growl line in F minor
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 0.75, 29, 110, 1 }, { 1.0, 0.25, 29, 90, 1 }, { 1.5, 0.5, 32, 100, 1 }, { 2.0, 1.5, 29, 110, 1 }, { 3.5, 0.5, 27, 95, 1 },
             { 4.0, 0.75, 29, 110, 1 }, { 5.0, 0.25, 29, 90, 1 }, { 5.5, 0.5, 36, 100, 1 }, { 6.0, 1.0, 34, 110, 1 }, { 7.0, 1.0, 32, 100, 1 },
             { 8.0, 0.75, 25, 110, 1 }, { 9.0, 0.25, 25, 90, 1 }, { 9.5, 0.5, 29, 100, 1 }, { 10.0, 1.5, 25, 110, 1 }, { 11.5, 0.5, 27, 95, 1 },
             { 12.0, 0.75, 27, 110, 1 }, { 13.0, 0.25, 27, 90, 1 }, { 13.5, 0.5, 31, 100, 1 }, { 14.0, 2.0, 24, 110, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

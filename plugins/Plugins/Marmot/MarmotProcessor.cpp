#include "MarmotProcessor.h"
#include "MarmotEditor.h"
#include "RacoonDsp.h"

namespace racoon::marmot
{
using namespace juce;

// ===================================================================== tables
int Tables::levelFor (float freq, double sampleRate)
{
    const double r = 1024.0 * jmax (1.0f, freq) / (0.45 * sampleRate);
    if (r <= 1.0) return 0;
    return jlimit (0, levels - 1, (int) std::ceil (std::log2 (r)));
}

const Tables& Tables::get()
{
    static const Tables t;
    return t;
}

Tables::Tables() : sawData ((size_t) levels * (size + 1)), triData ((size_t) levels * (size + 1))
{
    // additive synthesis, one table per octave: level l keeps only the harmonics under 1024 >> l
    std::vector<double> sn ((size_t) size);
    for (int l = 0; l < levels; ++l)
    {
        const int harmonics = jmax (1, 1024 >> l);
        float* s = sawData.data() + (size_t) l * (size + 1);
        float* t = triData.data() + (size_t) l * (size + 1);
        for (int i = 0; i < size; ++i)
        {
            const double x = MathConstants<double>::twoPi * i / size;
            double saw = 0.0, tri = 0.0;
            for (int k = 1; k <= harmonics; ++k)
            {
                const double sk = std::sin (k * x);
                saw += sk / k;
                if (k & 1) tri += ((((k - 1) / 2) & 1) ? -1.0 : 1.0) * sk / ((double) k * k);
            }
            s[i] = (float) (-2.0 / MathConstants<double>::pi * saw);                  // 2·ph − 1
            t[i] = (float) (8.0 / (MathConstants<double>::pi * MathConstants<double>::pi) * tri);
        }
        s[size] = s[0];
        t[size] = t[0];
    }
}

static inline float wrap01 (float x) { return x - std::floor (x); }

// one oscillator sample. Pulse = saw minus a shifted saw (zero mean, any width), so the width moves without clicks.
static inline float oscValue (const Tables& T, int wave, float ph, float pw, int lvl)
{
    switch (wave)
    {
        case waveSaw: return T.saw (ph, lvl);
        case waveTri: return T.tri (ph, lvl);
        case waveSquare: pw = 0.5f; [[fallthrough]];
        default:
        {
            const float s = T.saw (ph, lvl) - T.saw (wrap01 (ph + pw), lvl);
            return s * 0.8f;
        }
    }
}

// ===================================================================== biquad
void Biquad::shelf (bool high, double sr, double hz, double db)
{
    const double A = std::pow (10.0, db / 40.0);
    const double w0 = MathConstants<double>::twoPi * jlimit (10.0, sr * 0.45, hz) / sr;
    const double cs = std::cos (w0), sn = std::sin (w0);
    const double alpha = sn / 2.0 * std::sqrt (2.0);     // S = 1
    const double sA = 2.0 * std::sqrt (A) * alpha;
    double B0, B1, B2, A0, A1, A2;
    if (high)
    {
        B0 = A * ((A + 1) + (A - 1) * cs + sA);
        B1 = -2 * A * ((A - 1) + (A + 1) * cs);
        B2 = A * ((A + 1) + (A - 1) * cs - sA);
        A0 = (A + 1) - (A - 1) * cs + sA;
        A1 = 2 * ((A - 1) - (A + 1) * cs);
        A2 = (A + 1) - (A - 1) * cs - sA;
    }
    else
    {
        B0 = A * ((A + 1) - (A - 1) * cs + sA);
        B1 = 2 * A * ((A - 1) - (A + 1) * cs);
        B2 = A * ((A + 1) - (A - 1) * cs - sA);
        A0 = (A + 1) + (A - 1) * cs + sA;
        A1 = -2 * ((A - 1) + (A + 1) * cs);
        A2 = (A + 1) + (A - 1) * cs - sA;
    }
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
    a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

struct Processor::VerbImpl { sdsp::Verb v; AudioBuffer<float> wet; };
Processor::~Processor() = default;

// ===================================================================== parameters
const StringArray& Processor::waveNames()
{
    static StringArray a { "Sierra", "Pulso", "Cuadrada", "Triángulo"_u };
    return a;
}

const StringArray& Processor::sourceNames()
{
    static StringArray a { "Nada", "LFO 1", "LFO 2", "Envolvente 2", "Velocidad", "Aftertouch", "Rueda", "Tecla", "Azar por nota" };
    return a;
}

const StringArray& Processor::destNames()
{
    static StringArray a { "Nada", "Afinación"_u, "Afinación osc 2"_u, "Ancho de pulso", "Corte", "Resonancia", "Volumen", "Paneo",
                           "Modulación cruzada"_u, "Ruido", "Nivel osc 2" };
    return a;
}

const StringArray& Processor::arpModeNames()
{
    static StringArray a { "Arriba", "Abajo", "Arriba y abajo", "Al azar", "Como se tocó"_u, "Acorde" };
    return a;
}

const StringArray& Processor::arpDivNames()
{
    static StringArray a { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };
    return a;
}

static double arpDivBeats (int i)
{
    static const double b[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[jlimit (0, 5, i)];
}

const StringArray& Processor::chorusNames()
{
    static StringArray a { "Apagado", "I", "II", "I + II" };
    return a;
}

static const StringArray& lfoShapes()
{
    static StringArray a { "Seno", "Triángulo"_u, "Sierra", "Cuadrada", "S&H", "Azar suave" };
    return a;
}

static const StringArray& delayDivs()
{
    static StringArray a { "1/16", "1/8 T", "1/8", "1/8 .", "1/4 T", "1/4", "1/4 .", "1/2" };
    return a;
}

static double delayDivBeats (int i)
{
    static const double b[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
    return b[jlimit (0, 7, i)];
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "osc1_wave", "Osc 1 forma", waveNames(), waveSaw);
    P::pct (l, "osc1_pw", "Osc 1 ancho", 5.0f, 95.0f, 50.0f);
    P::integer (l, "osc1_oct", "Osc 1 octava", -2, 2, 0);
    P::pct (l, "osc1_level", "Osc 1 nivel", 0.0f, 100.0f, 80.0f);
    P::choice (l, "osc2_wave", "Osc 2 forma", waveNames(), wavePulse);
    P::pct (l, "osc2_pw", "Osc 2 ancho", 5.0f, 95.0f, 35.0f);
    P::integer (l, "osc2_oct", "Osc 2 octava", -2, 2, 0);
    P::integer (l, "osc2_semi", "Osc 2 semitonos", -12, 12, 0);
    P::flt (l, "osc2_fine", "Osc 2 fino", P::range (-100.0f, 100.0f), 7.0f, "ct", 0);
    P::pct (l, "osc2_level", "Osc 2 nivel", 0.0f, 100.0f, 45.0f);
    P::toggle (l, "sync", "Sync", false);
    P::pct (l, "xmod", "Modulación cruzada"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "sub", "Sub", 0.0f, 100.0f, 20.0f);
    P::pct (l, "noise", "Ruido", 0.0f, 100.0f, 0.0f);

    P::integer (l, "voices", "Voces", 1, maxVoices, 8);
    P::integer (l, "unison", "Unison", 1, maxUnison, 1);
    P::flt (l, "detune", "Detune", P::range (0.0f, 100.0f), 12.0f, "ct", 0);
    P::pct (l, "spread", "Apertura", 0.0f, 100.0f, 70.0f);
    P::pct (l, "drift", "Deriva analógica"_u, 0.0f, 100.0f, 20.0f);
    P::ms (l, "glide", "Glide", 0.0f, 2000.0f, 0.0f, 200.0f);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);

    P::hz (l, "hpf", "Paso altos", 20.0f, 2000.0f, 20.0f);
    P::choice (l, "ftype", "Tipo de filtro", { "LP 12", "LP 24", "BP" }, 1);
    P::hz (l, "cutoff", "Corte", 20.0f, 20000.0f, 1800.0f);
    P::pct (l, "reso", "Resonancia", 0.0f, 98.0f, 20.0f);
    P::db (l, "drive", "Drive", 0.0f, 24.0f, 2.0f);
    P::pct (l, "keytrack", "Seguir teclado", 0.0f, 100.0f, 50.0f);
    P::flt (l, "fenv", "Envolvente al filtro", P::range (-100.0f, 100.0f), 35.0f, "%", 0);
    P::pct (l, "velflt", "Velocidad al filtro", 0.0f, 100.0f, 25.0f);
    P::pct (l, "velamp", "Velocidad al volumen", 0.0f, 100.0f, 40.0f);

    P::adsr (l, 5.0f, 450.0f, 70.0f, 300.0f);       // envelope 1: volume (the family's card)
    P::ms (l, "fenv_a", "Env 2 ataque", 0.5f, 10000.0f, 3.0f, 60.0f);
    P::ms (l, "fenv_d", "Env 2 caída"_u, 5.0f, 10000.0f, 500.0f, 400.0f);
    P::pct (l, "fenv_s", "Env 2 sostenido", 0.0f, 100.0f, 25.0f);
    P::ms (l, "fenv_r", "Env 2 relajación"_u, 5.0f, 10000.0f, 400.0f, 400.0f);

    static const float defRate[numLfo] { 5.2f, 0.3f }, defDelay[numLfo] { 0.4f, 0.0f };
    for (int k = 0; k < numLfo; ++k)
    {
        const String n = "LFO " + String (k + 1) + " ";
        P::flt (l, lfoId (k, "rate"), n + "velocidad", P::range (0.01f, 30.0f, 2.0f), defRate[k], "Hz", 2);
        P::toggle (l, lfoId (k, "sync"), n + "a tempo", false);
        P::choice (l, lfoId (k, "div"), n + "división"_u, RacoonProcessor::divisionNames(), 7);
        P::choice (l, lfoId (k, "shape"), n + "forma", lfoShapes(), k == 0 ? 1 : 0);
        P::flt (l, lfoId (k, "delay"), n + "entrada lenta", P::range (0.0f, 5.0f, 0.8f), defDelay[k], "s", 2);
        P::toggle (l, lfoId (k, "retrig"), n + "reinicia con cada nota", k == 0);
    }
    static const int defSrc[numSlots] { srcLfo1, srcWheel, srcAt, srcLfo2, srcVel, srcNone };
    static const int defDst[numSlots] { dstPitch, dstCutoff, dstCutoff, dstPw, dstCutoff, dstNone };
    static const float defAmt[numSlots] { 0.0f, 30.0f, 20.0f, 15.0f, 0.0f, 0.0f };
    for (int k = 0; k < numSlots; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        P::choice (l, slotId (k, "src"), n + "fuente", sourceNames(), defSrc[k]);
        P::choice (l, slotId (k, "dst"), n + "destino", destNames(), defDst[k]);
        P::flt (l, slotId (k, "amt"), n + "cantidad", P::range (-100.0f, 100.0f), defAmt[k], "%", 0);
    }

    P::toggle (l, "arp_on", "Arpegiador", false);
    P::choice (l, "arp_mode", "Arpegio modo", arpModeNames(), arpUp);
    P::choice (l, "arp_div", "Arpegio división"_u, arpDivNames(), 3);
    P::integer (l, "arp_oct", "Arpegio octavas", 1, 4, 1);
    P::pct (l, "arp_gate", "Arpegio duración"_u, 10.0f, 100.0f, 60.0f);
    P::toggle (l, "arp_hold", "Arpegio sostener", false);
    P::pct (l, "arp_swing", "Arpegio swing", 0.0f, 75.0f, 0.0f);

    P::pct (l, "sat", "Saturación"_u, 0.0f, 100.0f, 10.0f);
    P::choice (l, "chorus", "Chorus", chorusNames(), 1);
    P::db (l, "eq_low", "Graves", -12.0f, 12.0f, 0.0f);
    P::db (l, "eq_high", "Agudos", -12.0f, 12.0f, -2.0f);
    P::pct (l, "dly_mix", "Delay", 0.0f, 100.0f, 0.0f);
    P::choice (l, "dly_div", "Delay tiempo", delayDivs(), 3);
    P::pct (l, "dly_fb", "Delay repeticiones", 0.0f, 90.0f, 35.0f);
    P::hz (l, "dly_tone", "Delay tono", 500.0f, 12000.0f, 3000.0f);
    P::pct (l, "rev_mix", "Reverb", 0.0f, 100.0f, 15.0f);
    P::pct (l, "rev_size", "Reverb tamaño"_u, 0.0f, 100.0f, 45.0f);
    P::pct (l, "rev_damp", "Reverb amortiguación"_u, 0.0f, 100.0f, 55.0f);
    P::choice (l, "quality", "Calidad", { "ECO", "NORMAL", "HQ" }, 1);
    P::db (l, "volume", "Volumen", -36.0f, 6.0f, -8.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Marmot", "Synth", Colour (0xffc78c54), PluginKind::instrument, 1, 3.0, false, false },
                       layout())
{
    Tables::get();
    verb = std::make_unique<VerbImpl>();
    static const char* lfoNames[6] { "rate", "sync", "div", "shape", "delay", "retrig" };
    static const char* slotNames[3] { "src", "dst", "amt" };
    for (int k = 0; k < numLfo; ++k) for (int j = 0; j < 6; ++j) pLfo[k][j] = param (lfoId (k, lfoNames[j]));
    for (int k = 0; k < numSlots; ++k) for (int j = 0; j < 3; ++j) pSlot[k][j] = param (slotId (k, slotNames[j]));
}

void Processor::prepareDsp (double rate, int block)
{
    for (auto& c : chorusLine) c.prepare ((int) (rate * 0.02) + 8);
    for (auto& d : delayLine) d.prepare ((int) (rate * 4.2) + 8);       // 1/2 note at 30 BPM
    mix.setSize (2, jmax (block, 1024), false, false, true);
    verb->wet.setSize (2, jmax (block, 1024), false, false, true);
    verb->v.setSampleRate (rate);
    volSmooth.prepare (rate, 30.0);
    volSmooth.reset (dsp::dbToGain (value ("volume")));
    delayTime.prepare (rate, 120.0);
    delayTime.reset ((float) (delayDivBeats ((int) value ("dly_div")) * 60.0 / jmax (20.0, bpm()) * rate));
    eqLowDb = eqHighDb = 1000.0f;
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& v : voices) v = Voice();
    for (auto& c : chorusLine) c.clear();
    for (auto& d : delayLine) d.clear();
    for (auto& e : eqLow) e.reset();
    for (auto& e : eqHigh) e.reset();
    delayLp[0] = delayLp[1] = 0.0f;
    satX[0] = satX[1] = 0.0f;
    verb->v.reset();
    activeVoices = 0;
    pedal = false;
    numHeld = numLatched = numArpPlaying = 0;
    latchArmed = false;
    arpLastStep = -1; arpIndex = 0; arpDir = 1; arpOffAt = -1.0; arpBeat = 0.0;
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

// ===================================================================== voices
void Processor::noteOn (int note, float vel)
{
    const int limit = (int) value ("voices");
    const float glideMs = value ("glide");

    if (limit == 1)   // mono legato: the held voice glides to the new note without restarting
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.held)
            {
                v.glideFrom = v.pitch;
                v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
                v.note = note; v.velocity = vel;
                lastPitch = (float) note;
                return;
            }

    for (auto& v : voices)
        if (v.active && ! v.stealing && v.note == note)
        {
            v.held = true; v.sustained = false; v.velocity = vel;
            for (auto& e : v.env) e.start();
            v.order = ++orderCounter;
            v.lfoAge = 0.0f;
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
            for (auto& v : voices)
                if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) break;
        victim->stealing = true;
        --playing;
    }

    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
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
        // a classic synth's oscillators run free: each note starts them at a different point
        v.ph1[i] = v.rng.next01(); v.ph2[i] = v.rng.next01(); v.phSub[i] = v.ph1[i] * 0.5f;
        v.uniJit[i] = v.rng.nextBipolar() * 2.0f;
    }
    for (int k = 0; k < numLfo; ++k)
    {
        v.lfoRand[k][0] = v.rng.nextBipolar(); v.lfoRand[k][1] = v.rng.nextBipolar();
        v.lfoPhase[k] = 0.0;
    }
    for (auto& e : v.env) e.start();
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

void Processor::allVoicesOff()
{
    for (auto& v : voices) if (v.active) { v.held = v.sustained = false; for (auto& e : v.env) e.noteOff(); }
}

// ===================================================================== arpeggiator
void Processor::arpKeyDown (int note, float vel)
{
    const bool hold = value ("arp_hold") > 0.5f;
    const bool wasEmpty = numHeld == 0 && (! hold || numLatched == 0 || latchArmed);
    for (int i = 0; i < numHeld; ++i) if (held[i].note == note) return;
    if (numHeld < maxArpNotes) held[numHeld++] = { note, vel };
    if (hold)
    {
        if (latchArmed) { numLatched = 0; latchArmed = false; }
        bool there = false;
        for (int i = 0; i < numLatched; ++i) there = there || latched[i].note == note;
        if (! there && numLatched < maxArpNotes) latched[numLatched++] = { note, vel };
    }
    if (wasEmpty) { arpLastStep = std::numeric_limits<long long>::min(); arpIndex = 0; arpDir = 1; }   // the first note sounds at once
}

void Processor::arpKeyUp (int note)
{
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note)
        {
            for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1];
            --numHeld;
            break;
        }
    if (value ("arp_hold") > 0.5f && numHeld == 0) latchArmed = true;
}

void Processor::arpReleaseAll()
{
    for (int i = 0; i < numArpPlaying; ++i) noteOff (arpPlaying[i]);
    numArpPlaying = 0;
    arpOffAt = -1.0;
}

void Processor::runArp (double beat)
{
    const bool hold = value ("arp_hold") > 0.5f;
    const Held* list = (hold && numLatched > 0) ? latched : held;
    const int count = (hold && numLatched > 0) ? numLatched : numHeld;

    if (arpOffAt >= 0.0 && beat >= arpOffAt) arpReleaseAll();
    if (count == 0)
    {
        if (numArpPlaying > 0) arpReleaseAll();
        arpLastStep = -1;
        arpStepView.store (-1);
        return;
    }

    const double L = arpDivBeats ((int) value ("arp_div"));
    const double swing = value ("arp_swing") * 0.01;
    long long s = (long long) std::floor (beat / L);
    if ((s & 1) != 0 && beat - (double) s * L < swing * 0.5 * L) --s;       // the off-beat steps wait for the swing
    if (s == arpLastStep) return;
    const bool immediate = arpLastStep == std::numeric_limits<long long>::min();
    arpLastStep = s;
    arpReleaseAll();

    // the notes in play this step: sorted (or as played), across the octaves
    int base[maxArpNotes]; float vel[maxArpNotes];
    for (int i = 0; i < count; ++i) { base[i] = list[i].note; vel[i] = list[i].vel; }
    const int mode = (int) value ("arp_mode");
    if (mode != arpPlayed)
        for (int i = 1; i < count; ++i)
            for (int j = i; j > 0 && base[j - 1] > base[j]; --j) { std::swap (base[j - 1], base[j]); std::swap (vel[j - 1], vel[j]); }
    const int octs = (int) value ("arp_oct");
    const int total = jmin (maxArpNotes * 4, count * octs);
    auto noteAt = [&] (int i) { return base[i % count] + 12 * (i / count); };
    auto velAt = [&] (int i) { return vel[i % count]; };

    const double stepStart = immediate ? beat : (double) s * L + ((s & 1) != 0 ? swing * 0.5 * L : 0.0);
    const double stepLen = L * ((s & 1) != 0 ? 1.0 - swing * 0.5 : 1.0 + swing * 0.5);
    arpOffAt = stepStart + jmax (0.02, stepLen * value ("arp_gate") * 0.01);

    if (mode == arpChord)
    {
        const int o = arpIndex % octs;
        for (int i = 0; i < count && numArpPlaying < maxArpNotes; ++i)
        {
            const int n = jlimit (0, 127, base[i] + 12 * o);
            noteOn (n, vel[i]);
            arpPlaying[numArpPlaying++] = n;
        }
        arpStepView.store (o);
        ++arpIndex;
        return;
    }
    int idx = 0;
    switch (mode)
    {
        case arpDown: idx = total - 1 - (arpIndex % total); break;
        case arpUpDown:
        {
            const int period = jmax (1, 2 * total - 2);
            const int k = arpIndex % period;
            idx = total == 1 ? 0 : (k < total ? k : period - k);
            break;
        }
        case arpRandom:
        {
            dsp::Rng r; r.seed ((uint32) (arpIndex * 2654435761u + 12345u));
            idx = (int) (r.next01() * (float) total) % total;
            break;
        }
        default: idx = arpIndex % total; break;      // up, as played
    }
    ++arpIndex;
    const int n = jlimit (0, 127, noteAt (idx));
    noteOn (n, velAt (idx));
    arpPlaying[numArpPlaying++] = n;
    arpStepView.store (idx);
}

// ===================================================================== MIDI
void Processor::handleMidi (const MidiMessage& m)
{
    const bool arp = value ("arp_on") > 0.5f;
    if (m.isNoteOn())
    {
        if (arp) arpKeyDown (m.getNoteNumber(), m.getFloatVelocity());
        else noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        if (arp) arpKeyUp (m.getNoteNumber());
        else noteOff (m.getNoteNumber());
    }
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
        else if (cc == 120) { for (auto& v : voices) if (v.active) v.stealing = true; numHeld = numLatched = numArpPlaying = 0; }
        else if (cc == 123) { allVoicesOff(); numHeld = numLatched = numArpPlaying = 0; arpOffAt = -1.0; }
    }
    else if (m.isAllNotesOff()) { allVoicesOff(); numHeld = numLatched = numArpPlaying = 0; arpOffAt = -1.0; }
    else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; numHeld = numLatched = numArpPlaying = 0; }
}

// ===================================================================== voice rendering
void Processor::renderVoice (Voice& v, int start, int total, int cr)
{
    const auto& T = Tables::get();
    const double srate = sr;
    const int quality = (int) value ("quality");
    const int uni = (int) value ("unison");
    const float spread = value ("spread") * 0.01f;
    const float bendRange = value ("bendrange");
    const float glideMs = value ("glide");
    const int ftype = (int) value ("ftype");
    const float driveG = dsp::dbToGain (value ("drive"));
    const bool useDrive = driveG > 1.01f;
    const float driveComp = 1.0f / std::sqrt (driveG);
    const float uniNorm = 1.0f / std::sqrt ((float) uni);
    const bool hq = quality == 2;
    const int w1 = (int) value ("osc1_wave"), w2 = (int) value ("osc2_wave");
    const float pw1 = value ("osc1_pw") * 0.01f, pw2 = value ("osc2_pw") * 0.01f;
    const float off1 = value ("osc1_oct") * 12.0f;
    const float off2 = value ("osc2_oct") * 12.0f + value ("osc2_semi") + value ("osc2_fine") * 0.01f;
    const float l1 = value ("osc1_level") * 0.01f, l2Base = value ("osc2_level") * 0.01f;
    const float subLvl = value ("sub") * 0.01f * 0.8f;
    const bool sync = value ("sync") > 0.5f;
    const float xmodBase = value ("xmod") * 0.01f;
    const float hpHz = value ("hpf");
    const bool useHp = hpHz > 21.0f;
    const float hpG = dsp::SVF::gFor (hpHz, srate);
    const float envA[2] { value ("adsr_a") * 0.001f, value ("fenv_a") * 0.001f },
                envD[2] { value ("adsr_d") * 0.001f, value ("fenv_d") * 0.001f },
                envS[2] { value ("adsr_s") * 0.01f, value ("fenv_s") * 0.01f },
                envR[2] { value ("adsr_r") * 0.001f, value ("fenv_r") * 0.001f };
    float lfoRate[numLfo], lfoDelay[numLfo]; bool lfoRetrig[numLfo];
    for (int k = 0; k < numLfo; ++k)
    {
        lfoRate[k] = rd (pLfo[k][1]) > 0.5f ? (float) (1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm())) : rd (pLfo[k][0]);
        lfoDelay[k] = rd (pLfo[k][4]);
        lfoRetrig[k] = rd (pLfo[k][5]) > 0.5f;
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

        // ---- control rate
        float env[2];
        for (int k = 0; k < 2; ++k) env[k] = v.env[k].advance (n, envA[k], envD[k], envS[k], envR[k], srate);
        v.lfoAge += (float) (n / srate);
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
            const float fade = lfoDelay[k] > 0.001f ? jmin (1.0f, v.lfoAge / lfoDelay[k]) : 1.0f;   // vibrato that comes in late
            lfo[k] = lfoValue (v, k, ph, wrapped) * fade;
        }
        float src[numSources] {};
        src[srcLfo1] = lfo[0]; src[srcLfo2] = lfo[1];
        src[srcEnv2] = env[1];
        src[srcVel] = v.velocity;
        src[srcAt] = jmax (v.pressure, channelPressure);
        src[srcWheel] = wheel;
        src[srcKey] = ((float) v.note - 60.0f) / 48.0f;
        src[srcRandom] = v.random;
        float mod[numDests] {};
        for (int k = 0; k < numSlots; ++k)
            if (slotSrc[k] != srcNone && slotDst[k] != dstNone) mod[slotDst[k]] += src[slotSrc[k]] * slotAmt[k];

        if (v.glidePos < 1.0f)
            v.glidePos = jmin (1.0f, v.glidePos + (float) (n / (jmax (1.0f, glideMs) * 0.001f * srate)));
        v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glidePos;
        const float basePitch = v.pitch + bend * bendRange + mod[dstPitch] * 24.0f;

        const float driftAmt = value ("drift") * 0.01f;
        {
            const float chance = (float) n / (float) (0.5 * srate), k = jmin (1.0f, (float) n / (float) (0.35 * srate));
            for (int u = 0; u < uni; ++u)
            {
                if (v.rng.next01() < chance) v.driftTarget[u] = v.rng.nextBipolar() * 5.0f * driftAmt;
                v.drift[u] += (v.driftTarget[u] - v.drift[u]) * k;
            }
        }
        const float oct = (value ("keytrack") * 0.01f) * ((float) v.note - 60.0f) / 12.0f
                        + value ("fenv") * 0.01f * env[1] * 7.0f
                        + value ("velflt") * 0.01f * (v.velocity - 0.7f) * 3.0f
                        + mod[dstCutoff] * 6.0f;
        const float fc = jlimit (20.0f, (float) (srate * (hq ? 0.9 : 0.45)), value ("cutoff") * std::pow (2.0f, oct));
        const float gTarget = dsp::SVF::gFor (fc, hq ? srate * 2.0 : srate);
        const float reso = jlimit (0.0f, 0.98f, value ("reso") * 0.01f + mod[dstReso]);
        const float k1 = 2.0f - 2.0f * reso;

        const float velAmp = 1.0f - value ("velamp") * 0.01f * (1.0f - v.velocity);
        const float ampTarget = env[0] * velAmp * jlimit (0.0f, 2.0f, 1.0f + mod[dstLevel]) * 0.3f;
        const float pan = jlimit (-1.0f, 1.0f, mod[dstPan]);
        const float panLT = std::cos ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;
        const float panRT = std::sin ((pan + 1.0f) * MathConstants<float>::pi * 0.25f) * 1.41421356f;

        const float detune = value ("detune");
        const float p1 = basePitch + off1, p2 = basePitch + off2 + mod[dstPitch2] * 24.0f;
        const float f1 = 440.0f * std::pow (2.0f, (p1 - 69.0f) / 12.0f), f2 = 440.0f * std::pow (2.0f, (p2 - 69.0f) / 12.0f);
        const float detMul = std::pow (2.0f, detune / 2400.0f);
        const int lv1 = Tables::levelFor (f1 * detMul * (xmodBase > 0.0f ? 2.0f : 1.0f), srate);
        const int lv2 = Tables::levelFor (f2 * detMul * (sync ? jmax (1.0f, f2 / jmax (1.0f, f1)) : 1.0f), srate);
        const int lvSub = Tables::levelFor (f1 * 0.5f * detMul, srate);
        float inc1[maxUnison], inc2[maxUnison], uL[maxUnison], uR[maxUnison];
        for (int u = 0; u < uni; ++u)
        {
            const float cents = (uni == 1 ? 0.0f : detune * ((float) u / (float) (uni - 1) - 0.5f) + v.uniJit[u]) + v.drift[u];
            const float m = std::pow (2.0f, cents / 1200.0f);
            inc1[u] = jmin (0.45f, (float) (f1 * m / srate));
            inc2[u] = jmin (0.45f, (float) (f2 * m / srate));
            const float p = uni == 1 ? 0.0f : jlimit (-1.0f, 1.0f, spread * ((float) u / (float) (uni - 1) * 2.0f - 1.0f));
            uL[u] = std::cos ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
            uR[u] = std::sin ((p + 1.0f) * MathConstants<float>::pi * 0.25f);
        }
        const float pwm = mod[dstPw] * 0.45f;
        const float pwA = jlimit (0.05f, 0.95f, pw1 + pwm), pwB = jlimit (0.05f, 0.95f, pw2 + pwm);
        const float xmod = jlimit (0.0f, 1.0f, xmodBase + mod[dstXmod]);
        const float l2 = jlimit (0.0f, 1.0f, l2Base + mod[dstOsc2]);
        const float noiseLvl = jlimit (0.0f, 1.0f, value ("noise") * 0.01f + mod[dstNoise]) * 0.3f;
        const float a1 = l1 * uniNorm, a2 = l2 * uniNorm, aSub = subLvl * uniNorm;

        // ---- audio rate
        const float ampStart = v.ampNow, gStart = v.gNow > 0.0f ? v.gNow : gTarget;
        const float plStart = v.panL, prStart = v.panR;
        const float invN = 1.0f / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float w = (float) (i + 1) * invN;
            const float amp = ampStart + (ampTarget - ampStart) * w;
            const float g = gStart + (gTarget - gStart) * w;
            float sl = 0.0f, sr2 = 0.0f;
            for (int u = 0; u < uni; ++u)
            {
                // osc 1, frequency modulated by the last sample of osc 2 (the classic cross mod growl)
                const float i1 = jlimit (0.0f, 0.45f, inc1[u] * (1.0f + xmod * 3.0f * v.out2[u]));
                const float s1 = a1 > 0.0f ? oscValue (T, w1, v.ph1[u], pwA, lv1) : 0.0f;
                v.ph1[u] += i1;
                bool reset = false; float t = 0.0f;
                if (v.ph1[u] >= 1.0f) { v.ph1[u] -= 1.0f; reset = sync; t = v.ph1[u] / jmax (1.0e-6f, i1); }
                // sub: a square one octave down, locked to osc 1
                float sSub = 0.0f;
                if (aSub > 0.0f) sSub = (T.saw (v.phSub[u], lvSub) - T.saw (wrap01 (v.phSub[u] + 0.5f), lvSub));
                v.phSub[u] += i1 * 0.5f;
                if (v.phSub[u] >= 1.0f) v.phSub[u] -= 1.0f;

                // osc 2, hard synced to osc 1: the jump of the reset is smoothed on the samples on both sides (polyBLEP)
                float cur;
                if (reset)
                {
                    const float oldNext = wrap01 (v.ph2[u] + inc2[u]);
                    const float newPh = jmin (0.999f, t * inc2[u]);
                    const float h = oscValue (T, w2, newPh, pwB, lv2) - oscValue (T, w2, oldNext, pwB, lv2);
                    v.ph2[u] = newPh;
                    cur = oscValue (T, w2, v.ph2[u], pwB, lv2);
                    v.syncPrev[u] += h * t * t * 0.5f;
                    cur -= h * (1.0f - t) * (1.0f - t) * 0.5f;
                }
                else
                {
                    v.ph2[u] += inc2[u];
                    if (v.ph2[u] >= 1.0f) v.ph2[u] -= 1.0f;
                    cur = oscValue (T, w2, v.ph2[u], pwB, lv2);
                }
                const float s2 = v.syncPrev[u];
                v.syncPrev[u] = cur;
                v.out2[u] = s2;
                const float s = s1 * a1 + s2 * a2 + sSub * aSub;
                sl += s * uL[u]; sr2 += s * uR[u];
            }
            if (noiseLvl > 0.0f)
            {
                const float wn = v.rng.nextBipolar() * noiseLvl * 0.7071f;
                sl += wn; sr2 += wn;
            }

            float in[2] { sl, sr2 }, out[2];
            for (int c = 0; c < 2; ++c)
            {
                float x = in[c];
                if (useDrive) x = racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q); }, x * driveG, v.driveX[c]) * driveComp;
                if (useHp) { v.hp[c].process (x, hpG, 1.414f); x = v.hp[c].hp; }
                auto run = [&] (float xin)
                {
                    v.f1[c].process (xin, g, k1);
                    switch (ftype)
                    {
                        case 0: return v.f1[c].lp;
                        case 1: v.f2[c].process (v.f1[c].lp, g, 1.414f); return v.f2[c].lp;
                        default: return v.f1[c].bp * k1;
                    }
                };
                out[c] = run (x);
                if (hq) out[c] = (out[c] + run (x)) * 0.5f;   // filter run twice per sample: steadier at the top
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
        v.ampNow = ampTarget; v.gNow = gTarget; v.panL = panLT; v.panR = panRT;
        if (v.env[0].stage == Env::idle) v.active = false;
        done += n;
    }
}

// ===================================================================== output chain
void Processor::postChain (int n)
{
    auto* L = mix.getWritePointer (0);
    auto* R = mix.getWritePointer (1);
    const double srate = sr;

    // saturation
    const float sat = value ("sat") * 0.01f;
    if (sat > 0.001f)
    {
        const float g = 1.0f + sat * 7.0f, comp = 1.0f / std::pow (g, 0.55f);
        for (int i = 0; i < n; ++i)
        {
            L[i] = racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q); }, L[i] * g, satX[0]) * comp;
            R[i] = racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q); }, R[i] * g, satX[1]) * comp;
        }
    }

    // ensemble chorus: one short delay per side, moved by a triangle in opposite directions
    const int chMode = (int) value ("chorus");
    {
        static const float rate[4] { 0.0f, 0.513f, 0.863f, 9.75f }, depthMs[4] { 0.0f, 1.7f, 2.0f, 0.35f };
        const double inc = rate[chMode] / srate;
        const float base = 0.0035f * (float) srate, depth = depthMs[chMode] * 0.001f * (float) srate;
        for (int i = 0; i < n; ++i)
        {
            chorusLine[0].push (L[i]); chorusLine[1].push (R[i]);
            if (chMode == 0) continue;
            chorusPhase += inc;
            if (chorusPhase >= 1.0) chorusPhase -= 1.0;
            const float tri = (float) (chorusPhase < 0.5 ? 4.0 * chorusPhase - 1.0 : 3.0 - 4.0 * chorusPhase);
            const float wl = chorusLine[0].tap (base + depth * (1.0f + tri));
            const float wr = chorusLine[1].tap (base + depth * (1.0f - tri));
            L[i] = (L[i] + wl) * 0.75f;
            R[i] = (R[i] + wr) * 0.75f;
        }
    }

    // two-band EQ
    const float lowDb = value ("eq_low"), highDb = value ("eq_high");
    if (lowDb != eqLowDb) { for (auto& e : eqLow) e.shelf (false, srate, 220.0, lowDb); eqLowDb = lowDb; }
    if (highDb != eqHighDb) { for (auto& e : eqHigh) e.shelf (true, srate, 3500.0, highDb); eqHighDb = highDb; }
    if (std::abs (lowDb) > 0.01f || std::abs (highDb) > 0.01f)
        for (int i = 0; i < n; ++i)
        {
            L[i] = eqHigh[0].process (eqLow[0].process (L[i]));
            R[i] = eqHigh[1].process (eqLow[1].process (R[i]));
        }

    // tempo delay, bouncing left and right, darker on every repeat
    const float dMix = value ("dly_mix") * 0.01f;
    {
        delayTime.target = (float) jmin (delayDivBeats ((int) value ("dly_div")) * 60.0 / jmax (20.0, bpm()) * srate, srate * 4.0);
        const float fb = value ("dly_fb") * 0.01f;
        const float lpC = 1.0f - std::exp (-MathConstants<float>::twoPi * value ("dly_tone") / (float) srate);
        for (int i = 0; i < n; ++i)
        {
            const float d = delayTime.next();
            if (dMix <= 0.001f) { delayLine[0].push (0.0f); delayLine[1].push (0.0f); continue; }
            const float tl = delayLine[0].tap (d), tr = delayLine[1].tap (d);
            delayLp[0] += lpC * (tl - delayLp[0]);
            delayLp[1] += lpC * (tr - delayLp[1]);
            delayLine[0].push ((L[i] + R[i]) * 0.5f + delayLp[1] * fb);
            delayLine[1].push (delayLp[0] * fb);
            L[i] += delayLp[0] * dMix;
            R[i] += delayLp[1] * dMix;
        }
    }

    // reverb (wet only, added on top)
    const float rMix = value ("rev_mix") * 0.01f;
    if (rMix > 0.001f)
    {
        sdsp::Verb::Parameters vp;
        vp.roomSize = value ("rev_size") * 0.01f;
        vp.damping = value ("rev_damp") * 0.01f;
        vp.wetLevel = 0.33f; vp.dryLevel = 0.0f; vp.width = 1.0f;
        verb->v.setParameters (vp);
        if (verb->wet.getNumSamples() < n) verb->wet.setSize (2, n, false, false, true);
        verb->wet.copyFrom (0, 0, mix, 0, 0, n);
        verb->wet.copyFrom (1, 0, mix, 1, 0, n);
        verb->v.processStereo (verb->wet.getWritePointer (0), verb->wet.getWritePointer (1), n);
        mix.addFrom (0, 0, verb->wet, 0, 0, n, rMix);
        mix.addFrom (1, 0, verb->wet, 1, 0, n, rMix);
    }

    volSmooth.target = dsp::dbToGain (value ("volume"));
    for (int i = 0; i < n; ++i) { const float vol = volSmooth.next(); L[i] *= vol; R[i] *= vol; }
}

// ===================================================================== block
void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    if (mix.getNumSamples() < n) mix.setSize (2, n, false, false, true);
    mix.clear (0, n);
    const int quality = (int) value ("quality");
    const int cr = quality == 0 ? 32 : quality == 1 ? 16 : 8;

    const bool arp = value ("arp_on") > 0.5f;
    if (arp != arpWasOn)
    {
        // switching the arpeggiator: whatever was sounding the other way stops softly
        allVoicesOff();
        numHeld = numLatched = numArpPlaying = 0;
        arpOffAt = -1.0; arpLastStep = -1; latchArmed = false;
        arpWasOn = arp;
    }
    const double beatsPerSample = bpm() / 60.0 / sr;
    const double beat0 = isPlaying() ? ppq() : arpBeat;
    if (! isPlaying()) arpBeat += beatsPerSample * n;

    // small chunks: the arpeggiator is checked at the start of each (32 samples, under 1 ms)
    int pos = 0;
    auto it = midi.cbegin();
    while (pos < n)
    {
        int end = jmin (n, pos + 32);
        while (it != midi.cend() && (*it).samplePosition <= pos)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        if (it != midi.cend()) end = jmin (end, jmax (pos + 1, (*it).samplePosition));
        if (arp) runArp (beat0 + beatsPerSample * pos);
        for (auto& v : voices) if (v.active) renderVoice (v, pos, end - pos, cr);
        pos = end;
    }
    while (it != midi.cend()) { handleMidi ((*it).getMessage()); ++it; }

    for (int k = 0; k < numLfo; ++k)
    {
        const double rate = rd (pLfo[k][1]) > 0.5f ? 1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm()) : rd (pLfo[k][0]);
        globalLfo[k] += rate * n / sr;
        globalLfo[k] -= std::floor (globalLfo[k]);
        lfoView[k].store ((float) std::sin (MathConstants<double>::twoPi * globalLfo[k]));
    }

    postChain (n);
    if (ch == 1) { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.addFrom (0, 0, mix, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f); }
    else { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.copyFrom (1, 0, mix, 1, 0, n); }

    int count = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++count;
    activeVoices.store (count);
}

// ===================================================================== presets (original sounds, all of them low and dark)
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    auto amp = [] (float a, float d, float s, float r) { return V { { "adsr_a", a }, { "adsr_d", d }, { "adsr_s", s }, { "adsr_r", r } }; };
    auto fe = [] (float a, float d, float s, float r) { return V { { "fenv_a", a }, { "fenv_d", d }, { "fenv_s", s }, { "fenv_r", r } }; };
    auto join = [] (V a, const V& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto j3 = [&] (V a, const V& b, const V& c) { return join (join (a, b), c); };
    std::vector<FactoryPreset> p;
    p.push_back ({ "Inicial", "Lead", {} });
    // leads
    p.push_back ({ "Linterna", "Lead", j3 ({ { "voices", 1 }, { "glide", 70 }, { "osc2_wave", waveSaw }, { "osc2_fine", 9 }, { "sub", 35 },
                                             { "cutoff", 1400 }, { "reso", 25 }, { "fenv", 30 }, { "m1_amt", 2 }, { "lfo1_delay", 0.5f },
                                             { "dly_mix", 18 }, { "rev_mix", 18 } }, amp (4, 600, 80, 220), fe (2, 700, 30, 300)) });
    p.push_back ({ "Sótano"_u, "Lead", j3 ({ { "voices", 1 }, { "glide", 40 }, { "osc1_wave", waveSquare }, { "osc1_oct", -1 }, { "osc2_wave", waveSquare },
                                              { "osc2_oct", -1 }, { "osc2_fine", -6 }, { "sub", 50 }, { "cutoff", 700 }, { "reso", 35 }, { "fenv", 45 },
                                              { "drive", 8 }, { "chorus", 0 }, { "rev_mix", 10 } }, amp (3, 400, 85, 150), fe (1, 350, 15, 200)) });
    p.push_back ({ "Hilo negro", "Lead", j3 ({ { "voices", 1 }, { "sync", 1 }, { "osc1_level", 30 }, { "osc2_wave", waveSaw }, { "osc2_semi", 7 },
                                               { "osc2_level", 85 }, { "m4_src", srcEnv2 }, { "m4_dst", dstPitch2 }, { "m4_amt", 25 }, { "cutoff", 2200 },
                                               { "reso", 15 }, { "sat", 30 }, { "dly_mix", 20 }, { "dly_div", 2 } }, amp (2, 500, 75, 200), fe (1, 450, 0, 200)) });
    p.push_back ({ "Faro apagado", "Lead", j3 ({ { "osc1_wave", wavePulse }, { "osc1_pw", 30 }, { "osc2_wave", wavePulse }, { "osc2_pw", 60 }, { "osc2_oct", -1 },
                                                 { "m4_amt", 35 }, { "lfo2_rate", 0.18f }, { "unison", 2 }, { "detune", 10 }, { "cutoff", 1600 }, { "reso", 18 },
                                                 { "chorus", 2 }, { "rev_mix", 25 }, { "rev_size", 60 } }, amp (12, 900, 70, 500), fe (10, 1200, 40, 600)) });
    p.push_back ({ "Navaja", "Lead", j3 ({ { "voices", 1 }, { "sync", 1 }, { "xmod", 35 }, { "osc2_wave", waveSquare }, { "osc2_semi", 12 }, { "osc2_level", 70 },
                                           { "cutoff", 1900 }, { "reso", 30 }, { "drive", 12 }, { "sat", 45 }, { "eq_high", -5 }, { "chorus", 0 } },
                                         amp (1, 300, 70, 120), fe (1, 260, 10, 150)) });
    // plucks
    p.push_back ({ "Gota fría"_u, "Pluck", j3 ({ { "osc2_wave", waveTri }, { "osc2_oct", 1 }, { "osc2_level", 30 }, { "sub", 10 }, { "cutoff", 500 }, { "reso", 30 },
                                                 { "fenv", 70 }, { "dly_mix", 25 }, { "dly_fb", 45 }, { "rev_mix", 22 } }, amp (1, 420, 0, 260), fe (1, 220, 0, 200)) });
    p.push_back ({ "Pluma de cuervo", "Pluck", j3 ({ { "osc1_wave", wavePulse }, { "osc1_pw", 22 }, { "osc2_level", 0 }, { "sub", 25 }, { "cutoff", 380 }, { "reso", 45 },
                                                     { "fenv", 75 }, { "ftype", 0 }, { "chorus", 1 }, { "rev_mix", 18 } }, amp (1, 300, 0, 180), fe (1, 160, 0, 120)) });
    p.push_back ({ "Grillo", "Pluck", j3 ({ { "osc1_wave", waveSquare }, { "osc2_wave", waveSquare }, { "osc2_oct", 1 }, { "osc2_level", 25 }, { "sub", 0 },
                                            { "cutoff", 900 }, { "reso", 20 }, { "fenv", 50 }, { "hpf", 180 }, { "dly_mix", 30 }, { "dly_div", 1 } },
                                          amp (1, 160, 0, 90), fe (1, 120, 0, 90)) });
    p.push_back ({ "Ceniza", "Pluck", j3 ({ { "osc2_wave", waveSaw }, { "osc2_fine", 14 }, { "noise", 12 }, { "cutoff", 450 }, { "reso", 25 }, { "fenv", 65 },
                                            { "sat", 25 }, { "eq_high", -6 }, { "rev_mix", 30 }, { "rev_size", 70 } }, amp (1, 700, 0, 400), fe (1, 380, 0, 300)) });
    // stabs
    p.push_back ({ "Puerta de hierro", "Stab", j3 ({ { "osc2_wave", waveSaw }, { "osc2_fine", 12 }, { "unison", 3 }, { "detune", 16 }, { "cutoff", 1100 }, { "reso", 15 },
                                                     { "fenv", 45 }, { "chorus", 2 }, { "rev_mix", 20 } }, amp (1, 260, 0, 160), fe (1, 200, 0, 140)) });
    p.push_back ({ "Bodega", "Stab", j3 ({ { "osc1_wave", wavePulse }, { "osc1_pw", 40 }, { "osc2_wave", waveSaw }, { "osc2_semi", 12 }, { "osc2_level", 25 },
                                           { "cutoff", 750 }, { "reso", 30 }, { "fenv", 55 }, { "ftype", 0 }, { "chorus", 1 }, { "dly_mix", 15 }, { "dly_div", 3 } },
                                         amp (2, 350, 10, 220), fe (1, 260, 0, 200)) });
    p.push_back ({ "Concreto", "Stab", j3 ({ { "osc1_wave", waveSquare }, { "osc2_wave", waveSaw }, { "osc2_semi", -12 }, { "xmod", 15 }, { "cutoff", 900 }, { "reso", 40 },
                                             { "fenv", 50 }, { "drive", 10 }, { "sat", 35 }, { "chorus", 0 }, { "rev_mix", 12 } }, amp (1, 200, 0, 120), fe (1, 150, 0, 100)) });
    // arpeggios
    p.push_back ({ "Escalera", "Arp", j3 ({ { "arp_on", 1 }, { "arp_mode", arpUp }, { "arp_div", 3 }, { "arp_oct", 2 }, { "arp_gate", 50 },
                                            { "cutoff", 650 }, { "reso", 35 }, { "fenv", 55 }, { "dly_mix", 20 }, { "dly_div", 3 }, { "rev_mix", 20 } },
                                          amp (1, 220, 0, 150), fe (1, 180, 0, 120)) });
    p.push_back ({ "Túnel"_u, "Arp", j3 ({ { "arp_on", 1 }, { "arp_mode", arpUpDown }, { "arp_div", 3 }, { "arp_oct", 2 }, { "arp_gate", 70 },
                                            { "osc1_wave", wavePulse }, { "osc1_pw", 25 }, { "osc2_level", 20 }, { "cutoff", 800 }, { "reso", 30 }, { "fenv", 40 },
                                            { "m2_src", srcLfo2 }, { "m2_dst", dstCutoff }, { "m2_amt", 25 }, { "lfo2_sync", 1 }, { "lfo2_div", 0 },
                                            { "dly_mix", 30 }, { "dly_fb", 50 }, { "rev_mix", 25 } }, amp (1, 300, 20, 180), fe (1, 220, 10, 150)) });
    p.push_back ({ "Reloj", "Arp", j3 ({ { "arp_on", 1 }, { "arp_mode", arpRandom }, { "arp_div", 2 }, { "arp_oct", 2 }, { "arp_gate", 40 },
                                         { "osc1_wave", waveTri }, { "osc2_wave", waveSquare }, { "osc2_oct", 1 }, { "osc2_level", 18 }, { "cutoff", 1200 },
                                         { "fenv", 30 }, { "dly_mix", 28 }, { "dly_div", 1 }, { "rev_mix", 22 } }, amp (1, 180, 0, 120), fe (1, 150, 0, 100)) });
    p.push_back ({ "Satélite"_u, "Arp", j3 ({ { "arp_on", 1 }, { "arp_mode", arpPlayed }, { "arp_div", 3 }, { "arp_oct", 3 }, { "arp_gate", 35 },
                                               { "arp_swing", 20 }, { "sync", 1 }, { "osc2_semi", 5 }, { "osc2_level", 60 }, { "osc1_level", 40 },
                                               { "cutoff", 1500 }, { "reso", 20 }, { "fenv", 35 }, { "dly_mix", 25 }, { "rev_mix", 30 }, { "rev_size", 65 } },
                                             amp (1, 150, 0, 110), fe (1, 140, 0, 100)) });
    // polyphonic
    p.push_back ({ "Humo", "Poly", j3 ({ { "osc2_wave", waveSaw }, { "osc2_fine", 8 }, { "unison", 2 }, { "detune", 9 }, { "cutoff", 900 }, { "reso", 10 },
                                         { "fenv", 25 }, { "chorus", 1 }, { "rev_mix", 30 }, { "rev_size", 65 } }, amp (90, 1200, 75, 900), fe (150, 1500, 40, 900)) });
    p.push_back ({ "Neblina", "Poly", j3 ({ { "osc1_wave", wavePulse }, { "osc1_pw", 45 }, { "osc2_wave", wavePulse }, { "osc2_pw", 55 }, { "osc2_fine", -7 },
                                            { "m4_amt", 25 }, { "lfo2_rate", 0.12f }, { "cutoff", 1000 }, { "fenv", 15 }, { "chorus", 2 }, { "rev_mix", 35 },
                                            { "rev_size", 70 } }, amp (350, 1500, 85, 1500), fe (400, 2000, 60, 1500)) });
    p.push_back ({ "Andén"_u, "Poly", j3 ({ { "osc2_wave", waveSaw }, { "osc2_oct", -1 }, { "osc2_level", 35 }, { "unison", 3 }, { "detune", 14 },
                                            { "cutoff", 1300 }, { "reso", 12 }, { "fenv", 10 }, { "chorus", 3 }, { "eq_high", -4 }, { "rev_mix", 32 } },
                                          amp (500, 2000, 90, 2000), fe (800, 2500, 70, 2000)) });
    // bass and effects
    p.push_back ({ "Ruido de fondo", "Bass", j3 ({ { "voices", 1 }, { "osc1_oct", -1 }, { "osc2_wave", waveSquare }, { "osc2_oct", -2 }, { "osc2_level", 55 },
                                                   { "sub", 60 }, { "cutoff", 420 }, { "reso", 20 }, { "fenv", 35 }, { "chorus", 0 }, { "rev_mix", 0 },
                                                   { "sat", 20 } }, amp (2, 500, 80, 150), fe (1, 300, 10, 150)) });
    p.push_back ({ "Raíz"_u, "Bass", j3 ({ { "voices", 1 }, { "osc1_oct", -1 }, { "osc2_oct", -1 }, { "osc2_fine", -10 }, { "sub", 40 }, { "cutoff", 300 },
                                            { "reso", 40 }, { "fenv", 60 }, { "drive", 6 }, { "chorus", 0 }, { "rev_mix", 0 } }, amp (1, 260, 30, 120), fe (1, 200, 0, 100)) });
    p.push_back ({ "Sirena lejana", "FX", j3 ({ { "voices", 1 }, { "osc1_wave", waveTri }, { "osc2_level", 0 }, { "sub", 0 }, { "m1_amt", 8 }, { "lfo1_rate", 0.4f },
                                                { "lfo1_shape", 0 }, { "lfo1_delay", 0 }, { "cutoff", 2000 }, { "dly_mix", 35 }, { "dly_fb", 60 }, { "rev_mix", 45 },
                                                { "rev_size", 85 } }, amp (300, 1000, 90, 1500), fe (1, 500, 50, 500)) });
    p.push_back ({ "Óxido"_u, "FX", j3 ({ { "voices", 1 }, { "osc2_wave", waveSquare }, { "xmod", 55 }, { "noise", 25 }, { "cutoff", 1200 }, { "reso", 55 },
                                           { "drive", 18 }, { "sat", 60 }, { "eq_high", -7 }, { "chorus", 0 }, { "rev_mix", 15 } }, amp (5, 800, 70, 300), fe (3, 600, 20, 300)) });
    return p;
}

// ===================================================================== example for Play: a low line in A minor
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 0.5, 57, 100, 1 }, { 0.5, 0.5, 57, 80, 1 }, { 1.0, 0.5, 60, 96, 1 }, { 1.5, 0.5, 57, 80, 1 },
             { 2.0, 0.75, 64, 104, 1 }, { 3.0, 0.5, 62, 90, 1 }, { 3.5, 0.5, 60, 84, 1 },
             { 4.0, 0.5, 53, 100, 1 }, { 4.5, 0.5, 53, 80, 1 }, { 5.0, 0.5, 57, 96, 1 }, { 5.5, 0.5, 53, 80, 1 },
             { 6.0, 0.75, 60, 104, 1 }, { 7.0, 1.0, 59, 90, 1 },
             { 8.0, 0.5, 55, 100, 1 }, { 8.5, 0.5, 55, 80, 1 }, { 9.0, 0.5, 59, 96, 1 }, { 9.5, 0.5, 55, 80, 1 },
             { 10.0, 0.75, 62, 104, 1 }, { 11.0, 0.5, 60, 90, 1 }, { 11.5, 0.5, 59, 84, 1 },
             { 12.0, 0.5, 57, 100, 1 }, { 12.5, 0.5, 57, 80, 1 }, { 13.0, 0.5, 60, 96, 1 }, { 13.5, 0.5, 64, 90, 1 },
             { 14.0, 1.5, 57, 100, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

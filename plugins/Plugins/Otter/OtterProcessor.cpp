#include "OtterProcessor.h"
#include "OtterEditor.h"
#include "RacoonDsp.h"

namespace racoon::otter
{
using namespace juce;

struct Processor::Impl
{
    Bq bodyF[2][numBodies];
    int bodyType = -1; float bodySize = -1.0f;
    Bq toneLp[2], toneShelf[2];
    float toneDark = -1.0f;
    KS symp[numSymp];
    int sympRoot = -1, sympMode = -1;
    float sympGain[numSymp] {};
    sdsp::Verb verb;
    AudioBuffer<float> wet;
    racoon::dsp::Smooth volSmooth;
    double quietFor = 0.0;
    bool quietCleared = true;
};

Processor::~Processor() = default;

// ===================================================================== names
const StringArray& Processor::modelNames() { static StringArray a { "Cuerda", "Barra", "Cuerda + barra" }; return a; }

const StringArray& Processor::tableNames()
{
    static StringArray a;
    if (a.isEmpty()) for (int i = 0; i < numTables; ++i) a.add (String::fromUTF8 (modeTable (i).name));
    return a;
}

const StringArray& Processor::exciterNames() { static StringArray a { "Pulsar", "Golpear", "Frotar", "Raspar", "Imán"_u }; return a; }

const StringArray& Processor::bodyNames()
{
    static StringArray a;
    if (a.isEmpty()) for (int i = 0; i < numBodies; ++i) a.add (String::fromUTF8 (body (i).name));
    return a;
}

const StringArray& Processor::sourceNames()
{
    static StringArray a { "Nada", "LFO 1", "LFO 2", "Envolvente 2", "Velocidad", "Aftertouch", "Rueda", "Tecla", "Azar por nota" };
    return a;
}

const StringArray& Processor::destNames()
{
    static StringArray a { "Nada", "Afinación"_u, "Vibrato", "Presión del arco"_u, "Velocidad del arco", "Posición"_u, "Dureza", "Caída"_u, "Oscuridad",
                           "Cuerpo: cantidad", "Volumen", "Paneo" };
    return a;
}

const StringArray& Processor::keyNames()
{
    static StringArray a { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return a;
}

static const StringArray& lfoShapes() { static StringArray a { "Seno", "Triángulo"_u, "Sierra", "Cuadrada", "S&H", "Azar suave" }; return a; }

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "modelo", "Resonador", modelNames(), 0);
    P::choice (l, "modos", "Barra: modos", tableNames(), 1);
    P::choice (l, "exc", "Excitador", exciterNames(), 0);
    P::pct (l, "dureza", "Dureza", 0.0f, 100.0f, 18.0f);
    P::pct (l, "posicion", "Posición"_u, 2.0f, 50.0f, 14.0f);
    P::flt (l, "caida", "Caída"_u, P::range (0.1f, 30.0f, 3.0f), 5.0f, "s", 2);
    P::pct (l, "oscuro", "Oscuridad", 0.0f, 100.0f, 45.0f);
    P::choice (l, "cuerpo", "Cuerpo", bodyNames(), 3);
    P::pct (l, "presion", "Presión del arco"_u, 0.0f, 100.0f, 50.0f);
    P::pct (l, "arco", "Velocidad del arco", 0.0f, 100.0f, 50.0f);
    P::pct (l, "espacio", "Espacio", 0.0f, 100.0f, 25.0f);
    P::db (l, "volume", "Volumen", -36.0f, 6.0f, -6.0f);
    P::adsr (l, 2.0f, 800.0f, 100.0f, 400.0f);
    P::pct (l, "agudos", "Caída de agudos"_u, 0.0f, 100.0f, 55.0f);
    P::pct (l, "escucha", "Punto de escucha", 2.0f, 50.0f, 30.0f);
    P::pct (l, "rigidez", "Rigidez", 0.0f, 100.0f, 5.0f);
    P::flt (l, "afinmodos", "Barra: estirar modos", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
    P::pct (l, "tension", "Tensión"_u, 0.0f, 100.0f, 10.0f);
    P::pct (l, "velbrillo", "Velocidad al brillo", 0.0f, 100.0f, 60.0f);
    P::pct (l, "velamp", "Velocidad al volumen", 0.0f, 100.0f, 60.0f);
    P::pct (l, "doble", "Segunda cuerda", 0.0f, 100.0f, 0.0f);
    P::choice (l, "dobleint", "Segunda cuerda: intervalo", { "Unísono"_u, "Octava abajo", "Quinta abajo" }, 0);
    P::flt (l, "dobleaf", "Segunda cuerda: afinación"_u, P::range (-50.0f, 50.0f), 4.0f, "ct", 0);
    P::pct (l, "cuerpomix", "Cuerpo: cantidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "cuerpotam", "Cuerpo: tamaño"_u, 50.0f, 200.0f, 100.0f);
    P::pct (l, "simp", "Cuerdas simpáticas"_u, 0.0f, 100.0f, 20.0f);
    P::choice (l, "simpraiz", "Simpáticas: tónica"_u, keyNames(), 9);
    P::choice (l, "simpmodo", "Simpáticas: afinación"_u, { "Tónica y quinta"_u, "Menor", "Mayor" }, 1);
    P::flt (l, "vibrato", "Vibrato", P::range (0.0f, 50.0f), 0.0f, "ct", 0);
    P::flt (l, "vibvel", "Vibrato: velocidad", P::range (3.0f, 8.0f), 5.0f, "Hz", 1);
    P::ms (l, "vibret", "Vibrato: retardo", 0.0f, 1500.0f, 400.0f, 400.0f);
    P::integer (l, "voices", "Voces", 1, maxVoices, 12);
    P::ms (l, "glide", "Glide", 0.0f, 2000.0f, 0.0f, 200.0f);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);
    P::pct (l, "ancho", "Ancho", 0.0f, 200.0f, 100.0f);
    P::pct (l, "espaciotam", "Espacio: tamaño"_u, 0.0f, 100.0f, 60.0f);
    P::flt (l, "env2_a", "Env 2 ataque", P::range (0.001f, 30.0f, 1.0f), 0.3f, "s", 3);
    P::flt (l, "env2_d", "Env 2 caída"_u, P::range (0.001f, 30.0f, 1.0f), 1.0f, "s", 3);
    P::pct (l, "env2_s", "Env 2 sostenido", 0.0f, 100.0f, 60.0f);
    P::flt (l, "env2_r", "Env 2 relajación"_u, P::range (0.005f, 60.0f, 1.5f), 1.0f, "s", 3);
    static const float defRate[numLfo] { 0.2f, 4.5f };
    static const int defShape[numLfo] { 5, 0 };
    for (int k = 0; k < numLfo; ++k)
    {
        const String n = "LFO " + String (k + 1) + " ";
        P::flt (l, lfoId (k, "rate"), n + "velocidad", P::range (0.01f, 20.0f, 2.0f), defRate[k], "Hz", 2);
        P::toggle (l, lfoId (k, "sync"), n + "a tempo", false);
        P::choice (l, lfoId (k, "div"), n + "división"_u, RacoonProcessor::divisionNames(), 7);
        P::choice (l, lfoId (k, "shape"), n + "forma", lfoShapes(), defShape[k]);
    }
    static const int defSrc[numSlots] { srcAt, srcWheel, srcEnv2, srcLfo1, srcNone, srcNone, srcNone, srcNone };
    static const int defDst[numSlots] { dstPressure, dstVibrato, dstPressure, dstPosition, dstNone, dstNone, dstNone, dstNone };
    static const float defAmt[numSlots] { 40.0f, 60.0f, 0.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < numSlots; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        P::choice (l, slotId (k, "src"), n + "fuente", sourceNames(), defSrc[k]);
        P::choice (l, slotId (k, "dst"), n + "destino", destNames(), defDst[k]);
        P::flt (l, slotId (k, "amt"), n + "cantidad", P::range (-100.0f, 100.0f), defAmt[k], "%", 0);
    }
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Otter", "Cuerdas y barras", Colour (0xff7fbfa8), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    impl = std::make_unique<Impl>();
    static const char* lfoNames[4] { "rate", "sync", "div", "shape" };
    static const char* slotNames[3] { "src", "dst", "amt" };
    for (int k = 0; k < numLfo; ++k) for (int j = 0; j < 4; ++j) pLfo[k][j] = param (lfoId (k, lfoNames[j]));
    for (int k = 0; k < numSlots; ++k) for (int j = 0; j < 3; ++j) pSlot[k][j] = param (slotId (k, slotNames[j]));
}

String Processor::aleatoriaRol() const
{
    switch ((int) value ("exc"))
    {
        case excBow: case excMagnet: return "pad";
        case excScrape: return "textura";
        default: return "arpegio";
    }
}

void Processor::prepareDsp (double rate, int block)
{
    maxDelay = (int) (rate / 20.0) + 16;
    for (auto& v : voices)
    {
        v.str.prepare (maxDelay);
        v.second.prepare (maxDelay * 2);
        v.outHist.prepare (maxDelay);
    }
    for (auto& s : impl->symp) s.prepare ((int) (rate / 25.0) + 16);
    mix.setSize (2, jmax (block, 1024), false, false, true);
    impl->wet.setSize (2, jmax (block, 1024), false, false, true);
    impl->verb.setSampleRate (rate);
    impl->volSmooth.prepare (rate, 30.0);
    impl->volSmooth.reset (dsp::dbToGain (value ("volume")));
    impl->bodyType = -1; impl->toneDark = -1.0f; impl->sympRoot = -1;
    resetDsp();
}

static void clearVoice (Voice& v)
{
    v.active = v.held = v.sustained = v.stealing = v.released = false;
    v.str.clear(); v.second.clear(); v.modes.clear(); v.outHist.clear();
    for (auto& t : v.tube) t.reset();
    v.amp = Env(); v.env2 = Env();
    v.exT = 1.0e9f; v.exLp = v.exLp2 = v.noiseBp1 = v.noiseBp2 = v.scrapeImp = 0.0f;
    v.magHp = v.magHpX = v.magRms = v.magX = 0.0f;
    v.energy = v.outLevel = 0.0f; v.gate = 1.0f; v.stealGain = 1.0f;
}

void Processor::resetDsp()
{
    for (auto& v : voices) clearVoice (v);
    for (auto& ch : impl->bodyF) for (auto& b : ch) b.reset();
    for (int c = 0; c < 2; ++c) { impl->toneLp[c].reset(); impl->toneShelf[c].reset(); }
    for (auto& s : impl->symp) s.clear();
    impl->verb.reset();
    impl->quietFor = 0.0; impl->quietCleared = true;
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
        case 3: return ph < 0.5f ? 1.0f : -1.0f;
        case 4: return v.lfoRand[k][1];
        default:
        {
            const float t = ph * ph * (3.0f - 2.0f * ph);
            return v.lfoRand[k][0] + (v.lfoRand[k][1] - v.lfoRand[k][0]) * t;
        }
    }
}

// ===================================================================== voices
static bool continuous (int exc) { return exc == excBow || exc == excScrape || exc == excMagnet; }

// a new pluck or strike: the pulse's length and colour from Dureza and the velocity
void Processor::excite (Voice& v, bool fresh)
{
    const int exc = (int) value ("exc");
    const float hard = value ("dureza") * 0.01f;
    const float vb = value ("velbrillo") * 0.01f;
    v.exT = 0.0f;
    if (exc == excStrike)
    {
        const float soft = 1.0f - hard;
        v.exLen = (0.0003f + soft * soft * 0.007f) * jmax (0.2f, 1.0f - 1.2f * v.velocity * vb);
        v.exAmp = std::pow (v.velocity, 1.5f) * 0.6f + 0.05f;
    }
    else
    {
        v.exLen = (0.0004f + (1.0f - hard) * 0.005f) * jmax (0.2f, 1.0f - 1.2f * v.velocity * vb);
        v.exAmp = exc == excPluck ? 0.25f + 0.75f * v.velocity : v.velocity * 0.2f;    // a magnet starts from a small pluck
    }
    const float fc = 300.0f * std::pow (2.0f, 7.0f * (hard * 0.5f + v.velocity * vb * 0.7f));
    v.exLpC = 1.0f - std::exp (-MathConstants<float>::twoPi * jmin (fc, (float) sr * 0.45f) / (float) sr);
    if (fresh) v.exLp = v.exLp2 = 0.0f;
}

void Processor::noteOn (int note, float vel)
{
    const int limit = (int) value ("voices");
    const int exc = (int) value ("exc");
    const float glideMs = value ("glide");
    lastNote = note;

    // mono: legato notes glide the string; a bow, a scraper or a magnet keeps going, a pluck or a strike plays again
    if (limit == 1)
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.held)
            {
                v.glideFrom = v.pitch;
                v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
                v.note = note; v.velocity = vel;
                if (! continuous (exc)) { excite (v, false); v.amp.start(); }
                lastPitch = (float) note;
                return;
            }

    // the same note while it still rings: the string is played again, not cut
    for (auto& v : voices)
        if (v.active && ! v.stealing && v.note == note)
        {
            v.held = true; v.sustained = false; v.released = false; v.velocity = vel;
            v.sinceRelease = 0.0f; v.gate = 1.0f;
            excite (v, false);
            v.amp.start(); v.env2.start();
            lastPitch = (float) note;
            return;
        }

    int playing = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++playing;
    while (playing >= limit)
    {
        // the quietest released voice first, then the oldest
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.stealing && ! v.held && ! v.sustained && (victim == nullptr || v.outLevel < victim->outLevel)) victim = &v;
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
    clearVoice (v);
    v.active = true; v.held = true; v.note = note; v.velocity = vel;
    v.order = ++orderCounter;
    v.rng.seed ((uint32) (0x9e3779b9u * (uint32) (++noteCounter) + (uint32) note * 7919u));
    v.random = v.rng.nextBipolar();
    v.pitch = (float) note;
    v.glideFrom = glideMs > 0.5f && limit == 1 ? lastPitch : (float) note;
    v.glidePos = v.glideFrom != (float) note ? 0.0f : 1.0f;
    v.age = 0.0f; v.sinceRelease = 0.0f; v.vibPhase = 0.0; v.bendNow = bend;
    for (int k = 0; k < numLfo; ++k) { v.lfoRand[k][0] = v.rng.nextBipolar(); v.lfoRand[k][1] = v.rng.nextBipolar(); v.lfoPhase[k] = 0.0; }
    v.P = (float) (sr / (440.0 * std::pow (2.0, (note - 69) / 12.0)));
    v.amp.start(); v.env2.start();
    excite (v, true);
    lastPitch = (float) note;
}

void Processor::releaseVoice (Voice& v)
{
    v.released = true; v.sinceRelease = 0.0f;
    v.amp.noteOff(); v.env2.noteOff();
}

void Processor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            v.held = false;
            if (pedal) v.sustained = true;
            else releaseVoice (v);
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
            if (! pedal) for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; releaseVoice (v); }
        }
        else if (cc == 120) { for (auto& v : voices) if (v.active) v.stealing = true; }
        else if (cc == 123) { for (auto& v : voices) if (v.active && ! v.released) { v.held = v.sustained = false; releaseVoice (v); } }
    }
    else if (m.isAllNotesOff()) { for (auto& v : voices) if (v.active && ! v.released) { v.held = v.sustained = false; releaseVoice (v); } }
    else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; }
}

// ===================================================================== one voice
void Processor::renderVoice (Voice& v, float* L, float* R, int start, int total)
{
    const double srate = sr;
    const int cr = 32;
    const int model = (int) value ("modelo");
    const int exc = (int) value ("exc");
    const int tableIndex = (int) value ("modos");
    const auto& table = modeTable (tableIndex);
    const bool useString = model != modelBar, useBar = model != modelString;
    const float aA = value ("adsr_a") * 0.001f, aD = value ("adsr_d") * 0.001f, aS = value ("adsr_s") * 0.01f, aR = value ("adsr_r") * 0.001f;
    const float e2A = value ("env2_a"), e2D = value ("env2_d"), e2S = value ("env2_s") * 0.01f, e2R = value ("env2_r");
    const float bendRange = value ("bendrange");
    const float glideMs = value ("glide");
    const float caida0 = value ("caida"), agudos = value ("agudos") * 0.01f;
    const float escucha = value ("escucha") * 0.01f;
    const float rig = value ("rigidez") * 0.01f;
    const float stretch = std::pow (2.0f, value ("afinmodos") * 0.01f);
    const float tension = value ("tension") * 0.01f;
    const float velAmp = value ("velamp") * 0.01f;
    const float doble = value ("doble") * 0.01f;
    const int dobleInt = (int) value ("dobleint");
    const float dobleCents = value ("dobleaf");
    const float ancho = value ("ancho") * 0.01f;
    const bool tube = (int) value ("cuerpo") == numBodies - 1;
    const float bodyMix = value ("cuerpomix") * 0.01f;
    const float vibDepth0 = value ("vibrato"), vibRate = value ("vibvel"), vibDelay = value ("vibret") * 0.001f;
    float lfoRate[numLfo];
    for (int k = 0; k < numLfo; ++k)
        lfoRate[k] = rd (pLfo[k][1]) > 0.5f ? (float) (1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm())) : rd (pLfo[k][0]);
    int slotSrc[numSlots], slotDst[numSlots]; float slotAmt[numSlots];
    for (int k = 0; k < numSlots; ++k) { slotSrc[k] = (int) rd (pSlot[k][0]); slotDst[k] = (int) rd (pSlot[k][1]); slotAmt[k] = rd (pSlot[k][2]) * 0.01f; }
    const float stealStep = (float) (1.0 / (0.004 * srate));
    float bendSm = v.bendNow;
    const float velGain = (1.0f - velAmp) + velAmp * v.velocity;
    const float twoPi = MathConstants<float>::twoPi;

    for (int done = 0; done < total && v.active; )
    {
        const int n = jmin (cr, total - done);
        const float dt = (float) (n / srate);

        // ---- control rate
        const float env = v.amp.advance (n, aA, aD, aS, aR, srate);
        const float env2 = v.env2.advance (n, e2A, e2D, e2S, e2R, srate);
        v.age += dt;
        if (v.released) v.sinceRelease += dt;
        float lfo[numLfo];
        for (int k = 0; k < numLfo; ++k)
        {
            const double g = globalLfo[k] + lfoRate[k] * (start + done) / srate;
            const float ph = (float) (g - std::floor (g));
            const bool wrapped = ph < (float) v.lfoPhase[k];
            v.lfoPhase[k] = ph;
            lfo[k] = lfoValue (v, k, ph, wrapped);
        }
        float src[numSources] {};
        src[srcLfo1] = lfo[0]; src[srcLfo2] = lfo[1]; src[srcEnv2] = env2; src[srcVel] = v.velocity;
        src[srcAt] = jmax (v.pressure, channelPressure); src[srcWheel] = wheel;
        src[srcKey] = ((float) v.note - 48.0f) / 36.0f; src[srcRandom] = v.random;
        float mod[numDests] {};
        for (int k = 0; k < numSlots; ++k) if (slotSrc[k] != srcNone && slotDst[k] != dstNone) mod[slotDst[k]] += src[slotSrc[k]] * slotAmt[k];

        const float hard = jlimit (0.0f, 1.0f, value ("dureza") * 0.01f + mod[dstHardness]);
        const float beta = jlimit (0.02f, 0.5f, value ("posicion") * 0.01f + mod[dstPosition] * 0.5f);
        const float presion = jlimit (0.0f, 1.0f, value ("presion") * 0.01f + mod[dstPressure]);
        const float arco = jlimit (0.0f, 1.0f, value ("arco") * 0.01f + mod[dstBowSpeed]);
        float caida = caida0 * std::pow (2.0f, mod[dstDecay] * 3.0f);

        bendSm += (bend - bendSm) * (1.0f - std::exp (-dt / 0.008f));      // the wheel glides over a few ms, the string never jumps
        v.bendNow = bendSm;
        if (v.glidePos < 1.0f) v.glidePos = jmin (1.0f, v.glidePos + dt / (jmax (1.0f, glideMs) * 0.001f));
        v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glidePos;
        v.vibPhase += vibRate * dt;
        const float vibOn = jlimit (0.0f, 1.0f, (v.age - vibDelay) / 0.3f);
        const float vibCents = (vibDepth0 + jmax (0.0f, mod[dstVibrato]) * 50.0f) * vibOn * std::sin (twoPi * (float) (v.vibPhase - std::floor (v.vibPhase)));
        const float semis = v.pitch + bendSm * bendRange + mod[dstPitch] * 12.0f + vibCents * 0.01f;
        const double f0 = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);

        // damping on release: a hand on the string, the loop loses faster
        const bool damp = v.released;
        if (damp && aR < caida) caida = jmax (0.02f, aR);
        // the first chunk of a note starts on its values; after that they glide (a damped string, a bend)
        const float kSm = v.age <= dt * 1.01f ? 1.0f : 1.0f - std::exp (-(float) n / (float) (0.007 * srate));

        // ---- the string
        double stringF0 = f0;
        if (model == modelBoth && dobleInt == 1) stringF0 = f0 * 0.5;
        if (useString)
        {
            const double w0 = MathConstants<double>::twoPi * stringF0 / srate;
            const double w1 = MathConstants<double>::twoPi * jmin (8.0 * stringF0, 0.3 * srate) / srate;
            float g, a;
            Loss::design (stringF0, w0, jmax (w1, w0 * 1.5), caida, caida * (1.0f - 0.95f * agudos), g, a);
            v.str.loss.g += kSm * (g - v.str.loss.g);
            v.str.loss.a += kSm * (a - v.str.loss.a);
            for (auto& ap : v.str.disp) ap.c = -rig * 0.9f;
            // a hard pluck shortens the loop for a moment (the slack string's "boing")
            const double P = srate / stringF0 * (1.0 - tension * 0.03 * jmin (1.0f, v.energy * 4.0f));
            v.P = (float) P;
            v.str.tune (P, beta, w0);
            if (v.age <= dt * 1.01f) v.str.Ln = v.str.LnTarget;      // the first chunk starts in tune
            v.listenD = jlimit (2.0f, (float) maxDelay - 6.0f, escucha * (float) P);
            v.listenNorm = 1.0f / jmax (0.25f, 2.0f * std::sin (MathConstants<float>::pi * escucha));
            if (doble > 0.0f)
            {
                static const double ratio[3] { 1.0, 2.0, 1.5 };
                const double P2 = P * ratio[jlimit (0, 2, dobleInt)] / std::pow (2.0, dobleCents / 1200.0);
                v.second.loss.a = v.str.loss.a;
                v.second.loss.g = std::pow (v.str.loss.g, (float) (P2 / P));
                v.second.P = (float) jlimit (2.5, (double) maxDelay * 2.0 - 8.0, P2 - Loss::phaseDelay (v.second.loss.a, w0 / ratio[jlimit (0, 2, dobleInt)]));
            }
        }

        // ---- the bar
        float barF1 = (float) f0;
        if (useBar)
        {
            const float rhoK = 1.0f + (table.ratios[numModes - 1] - 1.0f) * stretch;
            const bool bell = tableIndex == 6;
            const float ref = bell ? 1.0f + (table.ratios[2] - 1.0f) * stretch : 1.0f;
            barF1 = (float) f0 / ref;
            const float nyq = (float) srate * 0.45f;
            float caidaBar = caida;
            for (int k = 0; k < numModes; ++k)
            {
                const float rho = 1.0f + (table.ratios[k] - 1.0f) * stretch;
                const float fk = (float) f0 * rho / ref;
                v.modeOn[k] = jlimit (0.0f, 1.0f, (nyq - fk) / (nyq * 0.2063f));
                const float t60 = caidaBar / (1.0f + 6.0f * agudos * (rho - 1.0f) / rhoK);
                const float Rk = std::pow (10.0f, -3.0f / (jmax (0.005f, t60) * (float) srate));
                const float w = twoPi * jmin (fk, nyq) / (float) srate;
                v.modeR[k] += kSm * (Rk - v.modeR[k]);
                v.modeCos[k] = v.modeR[k] * std::cos (w);
                v.modeSin[k] = v.modeR[k] * std::sin (w);
                if (table.weights == 2)
                {
                    v.modeW[k] = 1.0f / std::sqrt ((float) (k / (tableIndex == 7 ? 2 : 1) + 1));
                    v.modeOutW[k] = v.modeW[k];
                }
                else
                {
                    v.modeW[k] = modeShape (table.weights, k, beta);
                    v.modeOutW[k] = modeShape (table.weights, k, escucha);
                }
            }
            if (tube)
            {
                v.tube[0].bandpass (srate, barF1, 40.0);
                v.tube[1].bandpass (srate, barF1 * 3.0, 25.0);
            }
        }
        else if (tube)
        {
            v.tube[0].bandpass (srate, stringF0, 40.0);
            v.tube[1].bandpass (srate, stringF0 * 3.0, 25.0);
        }

        // pan: low notes a bit left, high a bit right (a harp seen from the front)
        {
            const float pan = jlimit (-1.0f, 1.0f, jlimit (-1.0f, 1.0f, ((float) v.note - 57.0f) / 30.0f) * 0.5f * ancho + mod[dstPan]);
            const float th = (pan + 1.0f) * MathConstants<float>::pi * 0.25f;
            v.panL = std::cos (th) * 1.41421f; v.panR = std::sin (th) * 1.41421f;
        }
        const float level = velGain * jmax (0.0f, 1.0f + mod[dstLevel]) * (continuous (exc) ? 1.0f : env);
        if (v.released && continuous (exc)) v.gate *= std::exp (-6.9f * dt / jmax (0.05f, aR * 1.5f));
        const float bowEnv = continuous (exc) ? env : 0.0f;
        const float slope = 5.0f - 4.0f * presion;
        const float vbMax = (0.03f + 0.2f * arco) * bowEnv;
        const float scrapeAmp = arco * bowEnv;
        const float scrapeRate = (40.0f + 400.0f * arco) / (float) srate;
        const float scrapeC = twoPi * 400.0f * std::pow (2.0f, 5.0f * hard) / (float) srate;
        const float magGain = (0.2f + 0.8f * bowEnv * arco);
        const float magDrive = 1.0f + hard * 6.0f;
        const float magHpC = std::exp (-twoPi * 40.0f / (float) srate);
        const float rmsC = 1.0f - std::exp (-1.0f / (0.005f * (float) srate));
        const float barAdm = 0.08f;
        const float exDt = (float) (1.0 / srate);
        const float g2 = doble * 0.8f;

        float lnStep = 0.0f;
        if (useString) v.str.glide (n, lnStep);

        // ---- per sample
        for (int i = 0; i < n; ++i)
        {
            v.str.Ln += lnStep;
            // the exciter's force
            float e = 0.0f;
            if (v.exT < v.exLen)
            {
                const float t = v.exT / v.exLen;
                float pulse;
                if (exc == excStrike) pulse = std::sin (MathConstants<float>::pi * t);
                else pulse = 0.5f - 0.5f * std::cos (twoPi * t);
                pulse = pulse * v.exAmp + v.exAmp * 0.03f * v.rng.nextBipolar();
                v.exLp += v.exLpC * (pulse - v.exLp);
                v.exLp2 += v.exLpC * (v.exLp - v.exLp2);          // two poles: a soft touch really is dark
                e = v.exLp2;
                v.exT += exDt;
            }
            else if (v.exLp2 != 0.0f || v.exLp != 0.0f)
            {
                v.exLp *= 1.0f - v.exLpC;
                v.exLp2 += v.exLpC * (v.exLp - v.exLp2);
                e = v.exLp2;
                if (std::abs (v.exLp2) < 1.0e-9f && std::abs (v.exLp) < 1.0e-9f) v.exLp = v.exLp2 = 0.0f;
            }
            if (exc == excScrape && scrapeAmp > 0.0f)
            {
                // noise through a band pass, plus the scraper's teeth
                const float x = v.rng.nextBipolar();
                v.noiseBp1 += scrapeC * (x - v.noiseBp1 - 0.66f * v.noiseBp2);
                v.noiseBp2 += scrapeC * v.noiseBp1;
                v.scrapeImp += scrapeRate;
                float tooth = 0.0f;
                if (v.scrapeImp >= 1.0f) { v.scrapeImp -= 1.0f; tooth = 0.5f * (0.5f + 0.25f * (v.rng.nextBipolar() + 1.0f)); }
                e += scrapeAmp * (v.noiseBp1 * 0.15f + tooth * 0.2f);
            }
            if (exc == excMagnet)
            {
                // the output, high-passed and saturated, back into the string: it sustains, and the level follower keeps it steady
                const float agc = jmax (0.0f, 1.0f - v.magRms / 0.12f);
                e += 0.02f * magGain * agc * racoon::sdsp::adaaAverage ([] (float q) { return std::tanh (q); }, v.magHp * magDrive, v.magX) / magDrive * 8.0f;
            }

            float out = 0.0f;
            if (useString)
            {
                const float vs = v.str.pre();
                float force = e;
                if (exc == excBow && vbMax > 0.0f)
                {
                    const float vb = vbMax * (1.0f + hard * 0.04f * v.rng.nextBipolar());
                    const float dv = vb - vs;
                    const float r = jlimit (0.01f, 0.98f, std::pow (std::abs (dv * slope + 0.001f) + 0.75f, -4.0f));
                    force += dv * r;
                }
                v.str.post (force);
                const float y = v.str.bridgeOut;
                v.outHist.push (y);
                const float heard = (y - v.outHist.frac (jmax (2.0f, v.listenD))) * v.listenNorm;
                out += heard * (useBar ? 0.5f : 1.0f);
                if (doble > 0.0f)
                {
                    const float y2 = v.second.tick (force * g2);
                    out += y2 * 0.5f;
                }
            }
            if (useBar)
            {
                float x = model == modelBoth ? v.str.bridgeOut * 0.3f : e;
                if (exc == excBow && vbMax > 0.0f && model == modelBar)
                {
                    float vsb = 0.0f;
                    for (int k = 0; k < numModes; ++k) vsb += v.modeW[k] * v.modes.re[k];
                    vsb *= barAdm;
                    const float dv = vbMax - vsb;
                    const float r = jlimit (0.01f, 0.98f, std::pow (std::abs (dv * slope + 0.001f) + 0.75f, -4.0f));
                    x += dv * r * 0.05f;
                }
                float yb = 0.0f;
                for (int k = 0; k < numModes; ++k)
                {
                    if (v.modeOn[k] <= 0.0f) { v.modes.re[k] = v.modes.im[k] = 0.0f; continue; }
                    const float re = v.modes.re[k], im = v.modes.im[k];
                    const float nr = v.modeCos[k] * re - v.modeSin[k] * im + x * v.modeW[k];
                    const float ni = v.modeSin[k] * re + v.modeCos[k] * im;
                    v.modes.re[k] = jlimit (-60.0f, 60.0f, nr); v.modes.im[k] = jlimit (-60.0f, 60.0f, ni);
                    yb += v.modes.im[k] * v.modeOutW[k] * v.modeOn[k];
                }
                out += yb * 0.12f * (useString ? 0.5f : 1.0f);
            }
            if (tube) out += bodyMix * (v.tube[0].process (out) + 0.35f * v.tube[1].process (out)) * 0.5f;

            // magnet's sense of the output, the string's energy
            const float hpOut = out - v.magHpX + magHpC * v.magHp;
            v.magHpX = out; v.magHp = hpOut;
            v.magRms += rmsC * (std::abs (out) - v.magRms);
            v.energy = v.magRms;

            float s = out * level * v.gate;
            if (v.stealing) { v.stealGain -= stealStep; if (v.stealGain <= 0.0f) { v.stealGain = 0.0f; v.active = false; } s *= v.stealGain; }
            L[start + done + i] += s * v.panL;
            R[start + done + i] += s * v.panR;
            v.outLevel = jmax (std::abs (s), v.outLevel * 0.9995f);
        }

        // the voice is free when its sound is gone
        const bool exciterDone = ! continuous (exc) || v.released;
        if (v.active && exciterDone && v.exT >= v.exLen && v.age > 0.05f && v.outLevel < 1.0e-6f
            && (! continuous (exc) || v.sinceRelease > 0.02f))
        {
            const bool envDone = continuous (exc) ? true : (v.amp.stage == Env::idle || v.outLevel < 1.0e-6f);
            if (envDone) clearVoice (v);
        }
        if (v.active && ! continuous (exc) && v.released && v.amp.stage == Env::idle) clearVoice (v);
        if (v.active && continuous (exc) && v.released && v.gate < 1.0e-4f) clearVoice (v);
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
    mix.clear (0, n);
    float* ML = mix.getWritePointer (0);
    float* MR = mix.getWritePointer (1);

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices) if (v.active) renderVoice (v, ML, MR, pos, end - pos);
        pos = end;
    };
    for (const auto ev : midi) { renderTo (jlimit (0, n, ev.samplePosition)); handleMidi (ev.getMessage()); }
    renderTo (n);

    for (int k = 0; k < numLfo; ++k)
    {
        const double rate = rd (pLfo[k][1]) > 0.5f ? 1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm()) : rd (pLfo[k][0]);
        globalLfo[k] += rate * n / sr;
        globalLfo[k] -= std::floor (globalLfo[k]);
    }

    int count = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++count;
    activeVoices.store (count);
    float voicePeak = 0.0f;
    for (int i = 0; i < n; ++i) voicePeak = jmax (voicePeak, std::abs (ML[i]), std::abs (MR[i]));

    // once every tail has died (no voices, the output under -120 dB for half a second) the shared parts are cleared and
    // skipped: the output is exactly zero and nothing runs on denormals
    if (count == 0 && voicePeak == 0.0f && impl->quietCleared)
    {
        buffer.clear (0, n);
        for (auto& s : shapeView) s.store (s.load() * 0.8f);
        return;
    }
    impl->quietCleared = false;

    // ---- sympathetic strings: they hear the voices at -24 dB and never feed them back
    const float simp = value ("simp") * 0.01f;
    {
        const int root = (int) value ("simpraiz"), mode = (int) value ("simpmodo");
        if (root != impl->sympRoot || mode != impl->sympMode)
        {
            impl->sympRoot = root; impl->sympMode = mode;
            static const int iv[3][numSymp] { { 0, 7, 12, 19, 24, 31 }, { 0, 7, 12, 15, 19, 24 }, { 0, 7, 12, 16, 19, 24 } };
            for (int s = 0; s < numSymp; ++s)
            {
                const double f = 440.0 * std::pow (2.0, (24 + root + iv[jlimit (0, 2, mode)][s] - 69) / 12.0);
                auto& k = impl->symp[s];
                k.loss.a = 0.2f;
                k.loss.g = (float) (std::pow (10.0, -3.0 / (6.0 * f)) / Loss::mag (0.2, MathConstants<double>::twoPi * f / sr));
                k.P = (float) (sr / f - Loss::phaseDelay (0.2, MathConstants<double>::twoPi * f / sr));
            }
        }
        if (simp > 0.0f)
            for (int i = 0; i < n; ++i)
            {
                const float in = (ML[i] + MR[i]) * 0.5f * 0.063f;
                float l = 0.0f, r = 0.0f;
                for (int s = 0; s < numSymp; ++s) { const float y = impl->symp[s].tick (in); if (s & 1) r += y; else l += y; }
                ML[i] += (l * 0.75f + r * 0.25f) * simp * 0.5f;
                MR[i] += (r * 0.75f + l * 0.25f) * simp * 0.5f;
            }
        else for (auto& s : impl->symp) s.clear();
    }

    // ---- body (shared), unless it is the tube (per voice) or none
    const int bodyType = (int) value ("cuerpo");
    const float bodyMix = value ("cuerpomix") * 0.01f;
    const float size = value ("cuerpotam") * 0.01f;
    const auto& B = body (bodyType);
    if (bodyType != impl->bodyType || std::abs (size - impl->bodySize) > 0.002f)
    {
        if (bodyType != impl->bodyType) for (auto& c : impl->bodyF) for (auto& b : c) b.reset();
        impl->bodyType = bodyType; impl->bodySize = size;
        for (int k = 0; k < B.count; ++k)
            for (int c = 0; c < 2; ++c) impl->bodyF[c][k].bandpass (sr, B.hz[k] / jmax (0.5f, size), B.q[k]);
    }
    if (B.count > 0 && bodyMix > 0.0f)
    {
        float gains[numBodies];
        for (int k = 0; k < B.count; ++k) gains[k] = Decibels::decibelsToGain (B.db[k]) * 0.35f;
        for (int c = 0; c < 2; ++c)
        {
            float* x = c == 0 ? ML : MR;
            for (int i = 0; i < n; ++i)
            {
                float res = 0.0f;
                for (int k = 0; k < B.count; ++k) res += gains[k] * impl->bodyF[c][k].process (x[i]);
                x[i] = x[i] * (1.0f - 0.5f * bodyMix) + res * bodyMix;
            }
        }
    }

    // ---- Oscuridad: a low pass and a tilt
    const float dark = value ("oscuro") * 0.01f;
    if (std::abs (dark - impl->toneDark) > 0.001f)
    {
        impl->toneDark = dark;
        const float fc = 18000.0f * (1.0f - 0.92f * dark) * (1.0f - 0.92f * dark) + 600.0f;
        for (int c = 0; c < 2; ++c) { impl->toneLp[c].lowpass (sr, fc, 0.707); impl->toneShelf[c].highShelf (sr, 700.0, -10.0 * dark); }
    }
    for (int c = 0; c < 2; ++c)
    {
        float* x = c == 0 ? ML : MR;
        for (int i = 0; i < n; ++i) x[i] = impl->toneLp[c].process (impl->toneShelf[c].process (x[i]));
    }

    // ---- room
    const float esp = value ("espacio") * 0.01f;
    if (esp > 0.001f)
    {
        sdsp::Verb::Parameters vp;
        vp.roomSize = 0.3f + value ("espaciotam") * 0.0065f; vp.damping = 0.6f; vp.wetLevel = 0.33f; vp.dryLevel = 0.0f; vp.width = 1.0f;
        impl->verb.setParameters (vp);
        if (impl->wet.getNumSamples() < n) impl->wet.setSize (2, n, false, false, true);
        impl->wet.copyFrom (0, 0, mix, 0, 0, n);
        impl->wet.copyFrom (1, 0, mix, 1, 0, n);
        impl->verb.processStereo (impl->wet.getWritePointer (0), impl->wet.getWritePointer (1), n);
        mix.addFrom (0, 0, impl->wet, 0, 0, n, esp);
        mix.addFrom (1, 0, impl->wet, 1, 0, n, esp);
    }
    impl->volSmooth.target = dsp::dbToGain (value ("volume"));
    for (int i = 0; i < n; ++i) { const float vol = impl->volSmooth.next(); ML[i] *= vol; MR[i] *= vol; }

    if (ch == 1) { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.addFrom (0, 0, mix, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f); }
    else { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.copyFrom (1, 0, mix, 1, 0, n); }
    {
        const float outPeak = jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n));
        if (count == 0 && outPeak < 1.0e-6f)
        {
            impl->quietFor += n / sr;
            if (impl->quietFor > 0.5)
            {
                for (auto& c : impl->bodyF) for (auto& b : c) b.reset();
                for (int c = 0; c < 2; ++c) { impl->toneLp[c].reset(); impl->toneShelf[c].reset(); }
                for (auto& s : impl->symp) s.clear();
                impl->verb.reset();
                impl->quietCleared = true;
                impl->quietFor = 0.0;
            }
        }
        else impl->quietFor = 0.0;
    }

    // ---- the screen: the newest voice's string shape and mode energies
    const Voice* newest = nullptr;
    for (auto& v : voices) if (v.active && (newest == nullptr || v.order > newest->order)) newest = &v;
    if (newest != nullptr)
    {
        const float len = newest->str.Ln + newest->str.Lb;
        for (int k = 0; k < 64; ++k)
        {
            const float d = 2.0f + len * 0.5f * (float) k / 63.0f;
            shapeView[k].store (newest->str.neck.frac (jmin (d, newest->str.Ln)) + newest->str.bridge.at (jlimit (1, (int) newest->str.Lb, (int) (d * newest->str.Lb / jmax (1.0f, len)))));
        }
        for (int k = 0; k < numModes; ++k)
            modeView[k].store (std::sqrt (newest->modes.re[k] * newest->modes.re[k] + newest->modes.im[k] * newest->modes.im[k]));
    }
    else for (auto& s : shapeView) s.store (s.load() * 0.8f);
}

// ===================================================================== presets: dark and low
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    // indexes: modelo 0 Cuerda 1 Barra 2 Cuerda + barra; exc 0 Pulsar 1 Golpear 2 Frotar 3 Raspar 4 Imán
    // modos 0 libre 1 marimba 2 vibráfono 3 xilófono 4 lengüeta 5 placa 6 campana 7 cuenco
    // cuerpo 0 sin 1 chelo 2 contrabajo 3 arpa 4 calabaza 5 madera 6 tanque 7 tubo
    std::vector<FactoryPreset> p;
    p.push_back ({ "Arpa de carbón"_u, "Pulsadas", {} });
    p.push_back ({ "Chelo de sótano"_u, "Frotadas", V { { "exc", 2 }, { "presion", 55 }, { "arco", 45 }, { "posicion", 12 }, { "caida", 6 }, { "cuerpo", 1 }, { "oscuro", 50 },
                                                         { "adsr_a", 180 }, { "adsr_r", 700 }, { "vibrato", 12 }, { "vibret", 500 } } });
    p.push_back ({ "Contrabajo de niebla", "Frotadas", V { { "exc", 2 }, { "presion", 62 }, { "arco", 38 }, { "caida", 8 }, { "cuerpo", 2 }, { "cuerpotam", 120 },
                                                           { "oscuro", 65 }, { "adsr_a", 260 }, { "vibrato", 8 } } });
    p.push_back ({ "Chelo sin arco", "Pulsadas", V { { "dureza", 25 }, { "posicion", 22 }, { "caida", 2.5f }, { "cuerpo", 1 }, { "oscuro", 45 }, { "tension", 15 } } });
    p.push_back ({ "Cuerdas de humo", "Frotadas", V { { "exc", 2 }, { "doble", 60 }, { "dobleint", 1 }, { "presion", 45 }, { "cuerpo", 2 }, { "simp", 40 }, { "oscuro", 55 },
                                                      { "espacio", 40 }, { "adsr_a", 600 } } });
    p.push_back ({ "Koto en penumbra", "Pulsadas", V { { "dureza", 40 }, { "posicion", 9 }, { "tension", 30 }, { "caida", 3 }, { "cuerpo", 5 }, { "oscuro", 40 } } });
    p.push_back ({ "Kora del río"_u, "Pulsadas", V { { "dureza", 22 }, { "posicion", 16 }, { "cuerpo", 4 }, { "simp", 30 }, { "simpmodo", 1 }, { "oscuro", 40 } } });
    p.push_back ({ "Pizzicato grave", "Pulsadas", V { { "dureza", 12 }, { "posicion", 25 }, { "caida", 1.2f }, { "cuerpo", 2 }, { "oscuro", 55 }, { "adsr_r", 200 } } });
    p.push_back ({ "Arpa de clavos", "Inventadas", V { { "rigidez", 60 }, { "dureza", 45 }, { "caida", 7 }, { "cuerpo", 6 }, { "oscuro", 50 } } });
    p.push_back ({ "Puente colgante", "Inventadas", V { { "exc", 1 }, { "rigidez", 80 }, { "tension", 50 }, { "dureza", 30 }, { "caida", 9 }, { "cuerpo", 6 }, { "cuerpotam", 160 },
                                                        { "oscuro", 55 }, { "espacio", 45 } } });
    p.push_back ({ "Marimba de barro", "Golpeadas", V { { "modelo", 1 }, { "modos", 1 }, { "exc", 1 }, { "dureza", 20 }, { "posicion", 22 }, { "caida", 1.6f }, { "cuerpo", 7 },
                                                        { "oscuro", 40 } } });
    p.push_back ({ "Balafón nocturno"_u, "Golpeadas", V { { "modelo", 1 }, { "modos", 3 }, { "exc", 1 }, { "dureza", 28 }, { "caida", 1.1f }, { "cuerpo", 4 }, { "oscuro", 45 },
                                                          { "afinmodos", -20 } } });
    p.push_back ({ "Lengüeta de hierro"_u, "Golpeadas", V { { "modelo", 1 }, { "modos", 4 }, { "exc", 1 }, { "dureza", 30 }, { "caida", 4 }, { "cuerpo", 6 }, { "cuerpotam", 140 },
                                                            { "oscuro", 50 } } });
    p.push_back ({ "Campana de pozo", "Golpeadas", V { { "modelo", 1 }, { "modos", 6 }, { "exc", 1 }, { "dureza", 30 }, { "caida", 12 }, { "agudos", 75 }, { "cuerpo", 0 },
                                                       { "oscuro", 60 }, { "espacio", 50 } } });
    p.push_back ({ "Cuenco frotado", "Frotadas", V { { "modelo", 1 }, { "modos", 7 }, { "exc", 2 }, { "presion", 40 }, { "arco", 30 }, { "caida", 15 }, { "oscuro", 50 },
                                                     { "espacio", 45 }, { "adsr_a", 900 } } });
    p.push_back ({ "Arco sobre hierro", "Inventadas", V { { "modelo", 1 }, { "modos", 2 }, { "exc", 2 }, { "presion", 50 }, { "arco", 40 }, { "cuerpo", 6 }, { "oscuro", 55 },
                                                          { "adsr_a", 400 } } });
    p.push_back ({ "Dulcémele apagado"_u, "Golpeadas", V { { "exc", 1 }, { "dureza", 22 }, { "doble", 80 }, { "dobleint", 0 }, { "dobleaf", 5 }, { "caida", 2 }, { "cuerpo", 5 },
                                                           { "oscuro", 45 } } });
    p.push_back ({ "Piano de cuerda floja", "Inventadas", V { { "exc", 1 }, { "dureza", 35 }, { "tension", 40 }, { "rigidez", 30 }, { "caida", 6 }, { "cuerpo", 5 },
                                                              { "cuerpotam", 150 }, { "oscuro", 50 } } });
    p.push_back ({ "Imán grave"_u, "Sostenidas", V { { "exc", 4 }, { "arco", 55 }, { "dureza", 35 }, { "posicion", 20 }, { "cuerpo", 2 }, { "oscuro", 60 }, { "adsr_a", 400 },
                                                     { "adsr_r", 900 } } });
    p.push_back ({ "Drone de simpáticas"_u, "Sostenidas", V { { "exc", 3 }, { "arco", 25 }, { "dureza", 15 }, { "simp", 80 }, { "simpmodo", 0 }, { "cuerpo", 2 }, { "oscuro", 65 },
                                                              { "espacio", 50 }, { "adsr_a", 1500 } } });
    p.push_back ({ "Raspado de óxido"_u, "Sostenidas", V { { "modelo", 1 }, { "modos", 5 }, { "exc", 3 }, { "arco", 40 }, { "dureza", 60 }, { "cuerpo", 6 }, { "oscuro", 55 } } });
    p.push_back ({ "Arpa marimba", "Inventadas", V { { "modelo", 2 }, { "exc", 0 }, { "dureza", 20 }, { "modos", 1 }, { "caida", 3 }, { "cuerpo", 4 }, { "oscuro", 45 } } });
    return p;
}

// ===================================================================== example for Play: a slow low arpeggio in A minor
std::vector<SeqNote> Processor::demoPattern() const
{
    static const int chords[4][4] { { 45, 52, 57, 60 }, { 41, 48, 53, 57 }, { 43, 50, 55, 59 }, { 40, 47, 52, 56 } };
    static const int vels[4] { 92, 74, 82, 70 };
    std::vector<SeqNote> out;
    for (int b = 0; b < 4; ++b)
    {
        const double at = b * 4.0;
        out.push_back ({ at + 0.0, 0.5, chords[b][0], 96, 1 });
        out.push_back ({ at + 0.5, 0.5, chords[b][1], vels[b], 1 });
        out.push_back ({ at + 1.0, 1.0, chords[b][2], vels[(b + 1) & 3], 1 });
        out.push_back ({ at + 2.0, 2.0, chords[b][3], vels[(b + 2) & 3], 1 });
    }
    return out;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

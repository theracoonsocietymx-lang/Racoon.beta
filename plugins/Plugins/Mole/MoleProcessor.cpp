#include <map>
#include "MoleProcessor.h"
#include "MoleEditor.h"

namespace racoon::mole
{
using namespace juce;

namespace
{
enum LP { pOn = 0, pMute, pSolo, pFill, pMode, pSteps, pPulses, pRot, pRate, pSpan, pNote, pChan, pVel, pGate, pProb, pRat, pRatProb,
          pRatCurve, pRatAcc, pMicro, pSwing, pRef, pDens, pAccEvery, pAccent, pCount };
const char* laneKeys[] { "on", "mute", "solo", "fill", "mode", "steps", "pulses", "rot", "rate", "span", "note", "chan", "vel", "gate", "prob",
                         "rat", "ratprob", "ratcurve", "ratacc", "micro", "swing", "ref", "dens", "accevery", "accent" };
static_assert (std::size (laneKeys) == pCount);
}

const StringArray& laneNames()
{
    static StringArray a { "Bombo", "Caja", "Palmas", "Charles cerrado", "Charles abierto", "Ride", "Crash", "Aro", "Tom grave", "Tom medio",
                           "Tom agudo", "Shaker", "Conga", "Bongo", "Cencerro", "Metal" };
    return a;
}
const StringArray& rateNames() { static StringArray a { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1/32 T", "1/64" }; return a; }
double rateBeats (int i) { static const double b[] { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 }; return b[jlimit (0, 7, i)]; }
const StringArray& spanNames()
{
    static StringArray a { "Por paso (rejilla)", "Todo en 1 tiempo", "Todo en 2 tiempos", "Todo en 3 tiempos", "Todo en 1 compás"_u, "Todo en 2 compases",
                           "Todo en 4 compases" };
    return a;
}
double spanBeats (int i) { static const double b[] { 0.0, 1.0, 2.0, 3.0, 4.0, 8.0, 16.0 }; return b[jlimit (0, 6, i)]; }

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", { "Generador MIDI", "Rebanador de audio", "Los dos" }, 0);
    P::toggle (l, "ownsound", "Sonido propio", true);
    P::choice (l, "play", "Reproducir", { "Con la canción"_u, "Siempre", "Mientras tocas una nota", "Nota que transpone",
                                          "Lo que dice la maqueta" }, 0);
    P::toggle (l, "thru", "Dejar pasar las notas que entran", true);
    P::choice (l, "slot", "Patrón"_u, { "A", "B", "C", "D", "E", "F", "G", "H" }, 0);
    P::integer (l, "transpose", "Transponer", -24, 24, 0);
    // its own drums with a pitch (kick, toms, congas, bongos) on notes of the song's key
    P::choice (l, "tunekey", "Afinar la percusión a"_u, { "Sin afinar", "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }, 0);
    P::choice (l, "tunescale", "Escala de la percusión"_u, { "Menor", "Mayor" }, 0);
    P::pct (l, "swing", "Swing", 50.0f, 75.0f, 50.0f);
    P::choice (l, "swinggrid", "Rejilla del swing", { "1/16", "1/8" }, 0);
    P::choice (l, "groove", "Plantilla de groove", grooveNames(), 0);
    P::pct (l, "grooveamt", "Cantidad de groove", 0.0f, 100.0f, 100.0f);
    P::choice (l, "groovemode", "Qué toma del groove"_u, { "Tiempo y acentos", "Solo tiempo", "Solo acentos" }, 0);
    P::toggle (l, "learn", "Aprender groove", false);
    P::choice (l, "human", "Perfil de humanización"_u, { "Electrónico preciso"_u, "Funk suelto", "Percusión humana"_u, "Mecánico"_u }, 0);
    P::pct (l, "humamt", "Humanizar", 0.0f, 100.0f, 0.0f);
    P::ms (l, "humtime", "Humanizar: tiempo"_u, 0.0f, 30.0f, 6.0f, 0.0f);
    P::pct (l, "humvel", "Humanizar: velocidad"_u, 0.0f, 50.0f, 12.0f);
    P::integer (l, "seed", "Semilla", 1, 9999, 1);
    P::pct (l, "mutation", "Mutación"_u, 0.0f, 100.0f, 0.0f);
    P::choice (l, "mutevery", "Aleatoria cada", { "Apagado", "Cada vuelta", "Cada 2 vueltas", "Cada 4 vueltas", "Cada 8 vueltas" }, 0);
    P::toggle (l, "keepkick", "Mutación: respetar el bombo"_u, true);
    P::toggle (l, "keepsnare", "Mutación: respetar la caja"_u, true);
    P::choice (l, "fillevery", "Fill cada", { "Apagado", "2 compases", "4 compases", "8 compases", "16 compases" }, 0);
    P::choice (l, "filllen", "Largo del fill", { "1 tiempo", "2 tiempos", "1 compás"_u }, 0);
    P::pct (l, "filldens", "Fill: densidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "fillcomp", "Fill: complejidad", 0.0f, 100.0f, 40.0f);
    P::choice (l, "breakevery", "Corte cada", { "Apagado", "4 compases", "8 compases", "16 compases", "32 compases" }, 0);
    P::choice (l, "breaklen", "Largo del corte", { "1 tiempo", "2 tiempos", "1 compás"_u, "2 compases" }, 2);
    P::choice (l, "breaktype", "Tipo de corte", { "Silencio", "Menos densidad", "Redoble", "Cambio de acentos" }, 0);
    P::pct (l, "prob", "Probabilidad general", 0.0f, 100.0f, 100.0f);
    P::pct (l, "velscale", "Velocidad general"_u, 0.0f, 150.0f, 100.0f);
    P::flt (l, "pushpull", "Empujar o retrasar todo", P::range (-30.0f, 30.0f), 0.0f, "ms", 1);
    P::ms (l, "trange", "Desplazamiento máximo"_u, 0.0f, 100.0f, 60.0f, 0.0f);
    P::pct (l, "quant", "Cuantizar", 0.0f, 100.0f, 0.0f);
    // audio slicer
    P::choice (l, "slbars", "Rebanador: largo", { "1 compás"_u, "2 compases", "4 compases" }, 0);
    P::toggle (l, "slcapture", "Rebanador: capturar", false);
    P::choice (l, "slcut", "Rebanador: cortes", { "Por transitorios", "Cada 1/8", "Cada 1/16" }, 0);
    P::pct (l, "slsens", "Rebanador: sensibilidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "slrev", "Rebanador: al revés"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "slskip", "Rebanador: saltar", 0.0f, 100.0f, 0.0f);
    P::pct (l, "slrep", "Rebanador: repetir", 0.0f, 100.0f, 0.0f);
    P::pct (l, "slshuf", "Rebanador: desordenar", 0.0f, 100.0f, 0.0f);
    P::ms (l, "slfade", "Rebanador: fundido", 0.5f, 20.0f, 3.0f, 0.0f);
    P::pct (l, "slmix", "Rebanador: mezcla", 0.0f, 100.0f, 100.0f);

    struct Def { bool on; int steps, pulses, rot, rate; };
    const Def defs[] { { true, 16, 4, 0, 3 }, { true, 16, 2, 4, 3 }, { false, 16, 2, 4, 3 }, { true, 16, 4, 2, 3 },
                       { false, 16, 2, 6, 3 }, { false, 16, 4, 0, 3 }, { false, 16, 1, 0, 3 }, { false, 16, 3, 3, 3 },
                       { false, 16, 3, 0, 3 }, { false, 16, 3, 2, 3 }, { false, 16, 3, 4, 3 }, { false, 16, 8, 1, 3 },
                       { false, 16, 5, 0, 3 }, { false, 16, 5, 3, 3 }, { false, 16, 3, 1, 3 }, { false, 16, 2, 7, 3 } };
    static const int notes[] { 36, 38, 39, 42, 46, 51, 49, 37, 41, 45, 48, 70, 63, 60, 56, 75 };
    for (int i = 0; i < numLanes; ++i)
    {
        const auto n = laneNames()[i] + ": ";
        const auto& d = defs[i];
        auto id = [i] (const char* w) { return laneId (i, w); };
        P::toggle (l, id ("on"), n + "encendido", d.on);
        P::toggle (l, id ("mute"), n + "silencio", false);
        P::toggle (l, id ("solo"), n + "solo", false);
        P::toggle (l, id ("fill"), n + "toca en fills", i < 11);
        P::choice (l, id ("mode"), n + "modo", { "Euclidiano", "Dibujado", "Complemento" }, i >= 11 ? 0 : 0);
        P::integer (l, id ("steps"), n + "pasos", 1, maxSteps, d.steps);
        P::integer (l, id ("pulses"), n + "golpes", 0, maxSteps, d.pulses);
        P::integer (l, id ("rot"), n + "rotación"_u, 0, maxSteps - 1, d.rot);
        P::choice (l, id ("rate"), n + "rejilla", rateNames(), d.rate);
        P::choice (l, id ("span"), n + "polirritmo", spanNames(), 0);
        P::integer (l, id ("note"), n + "nota", 0, 127, notes[i]);
        P::integer (l, id ("chan"), n + "canal MIDI", 1, 16, 10);
        P::integer (l, id ("vel"), n + "velocidad"_u, 1, 127, i == 3 ? 85 : 100);
        P::pct (l, id ("gate"), n + "duración"_u, 5.0f, 100.0f, 50.0f);
        P::pct (l, id ("prob"), n + "probabilidad", 0.0f, 100.0f, 100.0f);
        P::integer (l, id ("rat"), n + "ratchet", 1, 16, 2);
        P::pct (l, id ("ratprob"), n + "ratchet al azar", 0.0f, 100.0f, 0.0f);
        P::flt (l, id ("ratcurve"), n + "ratchet: curva de velocidad"_u, P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::flt (l, id ("ratacc"), n + "ratchet: aceleración"_u, P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::flt (l, id ("micro"), n + "microtiempo", P::range (-50.0f, 50.0f), 0.0f, "ms", 1);
        P::pct (l, id ("swing"), n + "cuánto swing toma"_u, 0.0f, 100.0f, 100.0f);
        P::integer (l, id ("ref"), n + "complementa al carril", 1, numLanes, i >= 11 ? 1 : 1);
        P::pct (l, id ("dens"), n + "densidad del complemento", 0.0f, 100.0f, 50.0f);
        P::integer (l, id ("accevery"), n + "acento cada", 0, 16, 0);
        P::pct (l, id ("accent"), n + "fuerza del acento", 0.0f, 100.0f, 25.0f);
    }
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Mole", "Groove y ritmo", Colour (0xff9a7b5c), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    wantsMidiAsEffect = true;
    for (int i = 0; i < numLanes; ++i)
        for (int k = 0; k < pCount; ++k) laneParam[i][k] = param (laneId (i, laneKeys[k]));
    syncEngineFromParams();
}

void Processor::syncEngineFromParams()
{
    auto& g = engine.g;
    static const int every[] { 0, 1, 2, 4, 8 };
    static const int fillBars[] { 0, 2, 4, 8, 16 };
    static const double fillLen[] { 1.0, 2.0, 4.0 };
    static const int breakBars[] { 0, 4, 8, 16, 32 };
    static const double breakLen[] { 1.0, 2.0, 4.0, 8.0 };
    g.swing = value ("swing") * 0.01;
    g.swingGridBeats = value ("swinggrid") > 0.5f ? 0.5 : 0.25;
    g.groove = (int) value ("groove");
    g.grooveAmount = value ("grooveamt") * 0.01;
    g.grooveMode = (int) value ("groovemode") == 1 ? GrooveMode::timing : (int) value ("groovemode") == 2 ? GrooveMode::accents : GrooveMode::both;
    g.profile = (Humanize) jlimit (0, 3, (int) value ("human"));
    g.humanAmount = value ("humamt") * 0.01;
    g.humanTimeMs = value ("humtime");
    g.humanVel = value ("humvel") * 0.01;
    g.seed = (uint32) value ("seed");
    g.mutation = value ("mutation") * 0.01;
    g.mutateEvery = every[jlimit (0, 4, (int) value ("mutevery"))];
    g.preserveKick = value ("keepkick") > 0.5f;
    g.preserveSnare = value ("keepsnare") > 0.5f;
    g.fillEvery = fillBars[jlimit (0, 4, (int) value ("fillevery"))];
    g.fillBeats = fillLen[jlimit (0, 2, (int) value ("filllen"))] * g.beatsPerBar / 4.0;
    g.fillDensity = value ("filldens") * 0.01;
    g.fillComplexity = value ("fillcomp") * 0.01;
    g.breakEvery = breakBars[jlimit (0, 4, (int) value ("breakevery"))];
    g.breakBeats = breakLen[jlimit (0, 3, (int) value ("breaklen"))] * g.beatsPerBar / 4.0;
    g.breakType = (BreakType) jlimit (0, 3, (int) value ("breaktype"));
    g.probScale = value ("prob") * 0.01;
    g.velScale = value ("velscale") * 0.01;
    g.pushPullMs = value ("pushpull");
    g.timingRangeMs = value ("trange");
    g.quantize = value ("quant") * 0.01;
    engine.slot.store (jlimit (0, numSlots - 1, (int) value ("slot")));
    for (int i = 0; i < numLanes; ++i)
    {
        auto& L = engine.lanes[(size_t) i];
        auto v = [this, i] (int k) { return laneParam[i][k]->load (std::memory_order_relaxed); };
        L.on = v (pOn) > 0.5f;
        L.mute = v (pMute) > 0.5f;
        L.solo = v (pSolo) > 0.5f;
        L.fill = v (pFill) > 0.5f;
        L.mode = (LaneMode) jlimit (0, 2, (int) v (pMode));
        L.steps = jlimit (1, maxSteps, (int) v (pSteps));
        L.pulses = jlimit (0, maxSteps, (int) v (pPulses));
        L.rotation = (int) v (pRot);
        const double span = spanBeats ((int) v (pSpan));
        L.stepBeats = span > 0.0 ? span / L.steps : rateBeats ((int) v (pRate));
        L.note = (int) v (pNote);
        L.channel = (int) v (pChan);
        L.velocity = (int) v (pVel);
        L.gate = v (pGate) * 0.01;
        L.prob = v (pProb) * 0.01;
        L.ratchet = (int) v (pRat);
        L.ratchetProb = v (pRatProb) * 0.01;
        L.ratchetCurve = v (pRatCurve) * 0.01;
        L.ratchetAccel = v (pRatAcc) * 0.01;
        L.microMs = v (pMicro);
        L.swing = v (pSwing) * 0.01;
        L.refLane = (int) v (pRef) - 1;
        L.density = v (pDens) * 0.01;
        L.accentEvery = (int) v (pAccEvery);
        L.accent = v (pAccent) * 0.01;
    }
}

void Processor::prepareDsp (double rate, int)
{
    capture.setSize (2, (int) (rate * 24.0));
    capture.clear();
    resetDsp();
}

void Processor::resetDsp()
{
    numOffs = 0;
    for (auto& c : soundingNote) for (auto& n : c) n = false;
    internalPpq = 0.0; lastEnd = -1.0; wasRunning = false; heldKeys = 0; keyStarted = false;
    envFast = envSlow = 0.0f; refractory = 0;
}

void Processor::bypassStateChanged (bool bypassed, MidiBuffer& midi)
{
    if (bypassed) flushOffs (midi, 0);
}

// ===================================================================== notes
void Processor::scheduleOff (int note, int channel, int64 at)
{
    if (numOffs < maxOffs) offs[numOffs++] = { note, channel, at };
}

void Processor::flushOffs (MidiBuffer& midi, int offset)
{
    for (int i = 0; i < numOffs; ++i)
    {
        midi.addEvent (MidiMessage::noteOff (offs[i].channel, offs[i].note), offset);
        soundingNote[offs[i].channel - 1][offs[i].note] = false;
    }
    numOffs = 0;
}

// ===================================================================== groove learning
void Processor::learnOnset (double ppq, float vel)
{
    if (numOnsets < (int) onsets.size()) onsets[(size_t) numOnsets++] = { ppq, vel };
    learnedCount.store (numOnsets);
}

void Processor::finishLearning()
{
    // average lateness and loudness of every 16th of the bar
    double off[16] {}, vel[16] {};
    int cnt[16] {};
    double velSum = 0.0;
    const int bar16 = jmax (1, (int) std::round (engine.g.beatsPerBar * 4.0));
    for (int i = 0; i < numOnsets; ++i)
    {
        const auto& o = onsets[(size_t) i];
        const double s16 = o.ppq / 0.25;
        const int64 nearest = (int64) std::floor (s16 + 0.5);
        const int at = (int) (((nearest % bar16) + bar16) % bar16) % 16;
        off[at] += s16 - (double) nearest;
        vel[at] += o.vel;
        velSum += o.vel;
        ++cnt[at];
    }
    if (numOnsets > 0)
    {
        const double mean = velSum / numOnsets;
        Groove gr;
        for (int i = 0; i < 16; ++i)
            if (cnt[i] > 0)
            {
                gr.offset[(size_t) i] = (float) jlimit (-0.5, 0.5, off[i] / cnt[i]);
                gr.velocity[(size_t) i] = (float) jlimit (0.3, 1.7, (vel[i] / cnt[i]) / jmax (0.01, mean));
            }
        engine.learned = gr;
    }
    numOnsets = 0;
}

// ===================================================================== audio slicer
void Processor::detectSlices()
{
    const int cut = (int) value ("slcut");
    int n = 0;
    if (cut > 0 || capLength <= 0)
    {
        const int pieces = jmax (1, (int) std::round (capBeats * (cut == 1 ? 2.0 : 4.0)));
        for (int i = 0; i < pieces && n < 64; ++i) sliceStart[(size_t) n++] = (int) ((int64) capLength * i / pieces);
    }
    else
    {
        // onsets: a fast envelope jumping over a slow one; at least a 1/32 apart
        const float sens = value ("slsens") * 0.01f;
        const float ratio = 3.0f - 2.0f * sens;
        const int minGap = (int) (capLength / (capBeats * 8.0));
        float fast = 0.0f, slow = 0.0f;
        const float af = std::exp (-1.0f / (0.002f * (float) sr)), as = std::exp (-1.0f / (0.05f * (float) sr));
        int last = -minGap;
        sliceStart[(size_t) n++] = 0;
        for (int i = 0; i < capLength && n < 64; ++i)
        {
            const float x = 0.5f * (std::abs (capture.getSample (0, i)) + std::abs (capture.getSample (1, i)));
            fast = x + (fast - x) * af;
            slow = x + (slow - x) * as;
            if (fast > slow * ratio && fast > 0.01f && i - last >= minGap)
            {
                if (i > minGap / 2) sliceStart[(size_t) n++] = i;
                last = i;
            }
        }
    }
    sliceStart[(size_t) n] = capLength;
    numSlices.store (n);
}

void Processor::processSlicer (AudioBuffer<float>& b, double ppq0, double beatsPerSample, bool running)
{
    const int n = b.getNumSamples();
    const bool cap = value ("slcapture") > 0.5f;
    if (cap && ! capturePrev) { captureArmedEdge = true; sliceState.store (1); }
    if (! cap && capturePrev && sliceState.load() == 1) sliceState.store (capLength > 0 ? 3 : 0);
    capturePrev = cap;
    static const double bars[] { 1.0, 2.0, 4.0 };
    const double bpb = engine.g.beatsPerBar;
    for (int i = 0; i < n; ++i)
    {
        const double ppq = ppq0 + i * beatsPerSample;
        // recording starts on the next bar line
        if (running && captureArmedEdge && sliceState.load() == 1)
        {
            const double prevPpq = ppq - beatsPerSample;
            if (std::floor (ppq / bpb + 1.0e-9) != std::floor (prevPpq / bpb + 1.0e-9) || std::abs (std::fmod (ppq, bpb)) < 1.0e-9)
            {
                capBeats = bars[jlimit (0, 2, (int) value ("slbars"))] * bpb;
                capBpm = engine.g.bpm;
                capStartPpq = std::floor (ppq / bpb + 0.5) * bpb;
                capLength = jmin (capture.getNumSamples(), (int) std::round (capBeats * 60.0 / capBpm * sr));
                capWrite = 0;
                captureArmedEdge = false;
                sliceState.store (2);
            }
        }
        if (sliceState.load() == 2)
        {
            for (int c = 0; c < 2; ++c) capture.setSample (c, capWrite, b.getSample (jmin (c, b.getNumChannels() - 1), i));
            if (++capWrite >= capLength) { detectSlices(); sliceState.store (3); }
        }
    }
    if (sliceState.load() != 3 || ! running || capLength <= 0) return;

    const int ns = jmax (1, numSlices.load());
    const float rev = value ("slrev") * 0.01f, skip = value ("slskip") * 0.01f, rep = value ("slrep") * 0.01f, shuf = value ("slshuf") * 0.01f;
    const float mix = value ("slmix") * 0.01f;
    const int fade = jmax (1, (int) (value ("slfade") * 0.001 * sr));
    const uint32 seed = engine.g.seed;
    for (int i = 0; i < n; ++i)
    {
        const double ppq = ppq0 + i * beatsPerSample;
        double rel = ppq - capStartPpq;
        const int64 cycle = (int64) std::floor (rel / capBeats);
        rel -= (double) cycle * capBeats;
        const int pos = jlimit (0, capLength - 1, (int) std::floor (rel * 60.0 / capBpm * sr + 0.5));
        int j = 0;
        while (j + 1 < ns && sliceStart[(size_t) j + 1] <= pos) ++j;
        // which piece plays in place of slice j in this cycle
        auto pick = [&] (int slice)
        {
            int src = slice;
            if (Engine::hash01 (seed, cycle * 64 + slice, 101) < shuf) src = (int) (Engine::hash01 (seed, cycle * 64 + slice, 102) * ns) % ns;
            return src;
        };
        int src = pick (j);
        if (j > 0 && Engine::hash01 (seed, cycle * 64 + j, 103) < rep) src = pick (j - 1);
        const bool silent = Engine::hash01 (seed, cycle * 64 + j, 104) < skip;
        const bool reversed = Engine::hash01 (seed, cycle * 64 + j, 105) < rev;
        const int d = pos - sliceStart[(size_t) j], outLen = sliceStart[(size_t) j + 1] - sliceStart[(size_t) j];
        const int s0 = sliceStart[(size_t) src], srcLen = sliceStart[(size_t) src + 1] - s0;
        float y[2] { 0.0f, 0.0f };
        if (! silent && d < srcLen)
        {
            const int at = reversed ? s0 + srcLen - 1 - d : s0 + d;
            const float g = jmin (1.0f, (float) (d + 1) / fade, (float) (jmin (outLen, srcLen) - d) / fade);
            for (int c = 0; c < 2; ++c) y[c] = capture.getSample (c, jlimit (0, capLength - 1, at)) * jmax (0.0f, g);
        }
        for (int c = 0; c < b.getNumChannels() && c < 2; ++c) b.setSample (c, i, b.getSample (c, i) * (1.0f - mix) + y[c] * mix);
    }
}

// ===================================================================== process
void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    // transport and meter
    double hostPpqNow = ppq();
    bool playing = isPlaying();
    double bpb = 4.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto ts = pos->getTimeSignature())
                bpb = jmax (1.0, 4.0 * ts->numerator / jmax (1, ts->denominator));
    engine.g.beatsPerBar = bpb;
    engine.g.bpm = bpm();
    syncEngineFromParams();

    const int mode = (int) value ("mode");
    const int playMode = (int) value ("play");
    // "Lo que dice la maqueta": the rhythm is a clip in the song; Mole only plays its notes with its own drums
    const bool fromSong = playMode == 4;
    const bool thru = (value ("thru") > 0.5f && playMode < 2) || fromSong;
    const double beatsPerSample = bpm() / (60.0 * sr);

    // incoming notes: keys for the note modes, onsets for groove learning; filter what passes
    MidiBuffer kept;
    const bool learnOn = value ("learn") > 0.5f;
    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        if (m.isNoteOn())
        {
            if (heldKeys++ == 0 && playMode >= 2 && ! fromSong) { keyStarted = true; internalPpq = -(double) ev.samplePosition * beatsPerSample; }
            lastKey = m.getNoteNumber();
            if (learnOn && playing) learnOnset (hostPpqNow + ev.samplePosition * beatsPerSample, m.getFloatVelocity());
        }
        else if (m.isNoteOff()) heldKeys = jmax (0, heldKeys - 1);
        if (! m.isNoteOnOrOff() || thru) kept.addEvent (m, ev.samplePosition);
    }
    midi.swapWith (kept);

    // audio onsets also teach the groove
    if (learnOn && playing)
    {
        const float af = std::exp (-1.0f / (0.002f * (float) sr)), as = std::exp (-1.0f / (0.08f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < jmin (2, buffer.getNumChannels()); ++c) x = jmax (x, std::abs (buffer.getSample (c, i)));
            envFast = x + (envFast - x) * af;
            envSlow = x + (envSlow - x) * as;
            if (refractory > 0) --refractory;
            else if (envFast > 0.02f && envFast > envSlow * 2.5f)
            {
                learnOnset (hostPpqNow + i * beatsPerSample, jmin (1.0f, envFast));
                refractory = (int) (0.06 * sr);
            }
        }
    }
    if (learning && ! learnOn) finishLearning();
    learning = learnOn;

    // which clock drives the pattern
    bool running = false;
    double from = 0.0;
    engine.g.transpose = (int) value ("transpose");
    ownVoice.setDrumKey ((int) value ("tunekey") - 1, (int) value ("tunescale") == 1);
    switch (playMode)
    {
        case 0: running = playing; from = hostPpqNow; break;
        case 1: running = true; from = playing ? hostPpqNow : internalPpq; break;
        // the song plays the clip; the plugin's own Play still plays its lanes (to hear what you are editing)
        case 4: running = isSeqPlaying() && ! hostDrivesTempo(); from = hostPpqNow; break;
        default:
            running = heldKeys > 0 && keyStarted;
            from = internalPpq;
            if (playMode == 3) engine.g.transpose += lastKey - 60;
            break;
    }
    const double to = from + n * beatsPerSample;

    if (running && (! wasRunning || std::abs (from - lastEnd) > 1.0e-3))
    {
        // start or jump: what was sounding stops, "first time" counts from here
        flushOffs (midi, 0);
        engine.g.startPpq = from;
    }
    if (! running && wasRunning) flushOffs (midi, 0);
    wasRunning = running;

    // the pattern
    if (running && mode != 1)
    {
        const double samplesPerBeat = 1.0 / beatsPerSample;
        struct Hit { int at; Event e; };
        std::array<Hit, 256> hits;
        int numHits = 0;
        // each event goes to the sample nearest to it: the window is shifted by half a sample so that
        // rounding never pushes an event out of the block it was collected in
        const double half = 0.5 * beatsPerSample;
        engine.collect (from - half, to - half, [&] (const Event& e)
        {
            if (numHits < (int) hits.size())
                hits[(size_t) numHits++] = { jlimit (0, n - 1, (int) std::floor ((e.ppq - from) * samplesPerBeat + 0.5)), e };
        });
        std::sort (hits.begin(), hits.begin() + numHits, [] (const Hit& a, const Hit& b) { return a.at < b.at; });
        // note-offs due in this block first (and those of retriggered notes)
        int hi = 0;
        for (int i = 0; i < n || hi < numHits; )
        {
            const int nextHit = hi < numHits ? hits[(size_t) hi].at : n;
            // offs before the next hit
            for (int k = 0; k < numOffs; )
            {
                if (offs[k].at - clock <= nextHit && offs[k].at - clock < n)
                {
                    midi.addEvent (MidiMessage::noteOff (offs[k].channel, offs[k].note), (int) jmax ((int64) 0, offs[k].at - clock));
                    soundingNote[offs[k].channel - 1][offs[k].note] = false;
                    offs[k] = offs[--numOffs];
                }
                else ++k;
            }
            if (hi >= numHits) break;
            const auto& h = hits[(size_t) hi++];
            const int ch = h.e.channel - 1;
            if (soundingNote[ch][h.e.note])
            {
                for (int k = 0; k < numOffs; ++k)
                    if (offs[k].note == h.e.note && offs[k].channel == h.e.channel) { offs[k] = offs[--numOffs]; break; }
                midi.addEvent (MidiMessage::noteOff (h.e.channel, h.e.note), h.at);
            }
            midi.addEvent (MidiMessage::noteOn (h.e.channel, h.e.note, (uint8) h.e.vel), h.at);
            soundingNote[ch][h.e.note] = true;
            scheduleOff (h.e.note, h.e.channel, clock + h.at + jmax (1, (int) (h.e.lengthBeats * samplesPerBeat)));
            laneFlash[h.e.lane].fetch_add (1);
            i = h.at;
        }
    }
    else
    {
        for (int k = 0; k < numOffs; )
        {
            if (offs[k].at - clock < n)
            {
                midi.addEvent (MidiMessage::noteOff (offs[k].channel, offs[k].note), (int) jmax ((int64) 0, offs[k].at - clock));
                soundingNote[offs[k].channel - 1][offs[k].note] = false;
                offs[k] = offs[--numOffs];
            }
            else ++k;
        }
    }

    if (mode != 0) processSlicer (buffer, from, beatsPerSample, running && (playMode == 0 ? playing : true));

    if (running) lastEnd = to;
    if ((playMode == 1 && ! playing) || playMode >= 2) internalPpq = to;
    if (playMode >= 2 && ! running) { internalPpq = 0.0; keyStarted = heldKeys > 0 && keyStarted; }
    clock += n;
    uiPpq.store (from);
    uiRunning.store (running);
}

// ===================================================================== message thread tools
void Processor::complementHamster()
{
    // shaker, conga, bongo, cowbell and metal answer the kick (or the snare) where it leaves room
    static const int ref[] { 1, 1, 2, 1, 2 };
    static const float dens[] { 70.0f, 45.0f, 40.0f, 20.0f, 25.0f };
    for (int k = 0; k < 5; ++k)
    {
        const int lane = 11 + k;
        setRealValue (laneId (lane, "on"), 1.0f);
        setRealValue (laneId (lane, "mode"), 2.0f);
        setRealValue (laneId (lane, "ref"), (float) ref[k]);
        setRealValue (laneId (lane, "dens"), dens[k]);
        setRealValue (laneId (lane, "rate"), 3.0f);
        setRealValue (laneId (lane, "span"), 0.0f);
        setRealValue (laneId (lane, "vel"), 80.0f);
    }
}

void Processor::loadFromSong (const racoon::maqueta::EditRequest& r)
{
    setSeqPlaying (false);
    const int numSteps = jlimit (1, maxSteps, (int) std::ceil (jmin (r.beats, maxSteps * 0.25) / 0.25 - 1.0e-6));
    setPatternBars ((numSteps + 15) / 16);
    setPatternBpm (r.bpm);
    setRealValue ("transpose", 0.0f);               // the clip comes in as it sounds
    const auto notes = r.notes();
    // which notes, the most played first
    std::map<int, int> count;
    for (auto& n : notes) if (n.start < numSteps * 0.25 - 1.0e-6) ++count[n.pitch];
    std::vector<std::pair<int, int>> order (count.begin(), count.end());
    std::stable_sort (order.begin(), order.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    // a lane that already plays the note keeps it; the others go to the lanes left
    int laneFor[128];
    std::fill (std::begin (laneFor), std::end (laneFor), -1);
    bool taken[numLanes] {};
    for (auto& [pitch, c] : order)
        for (int l = 0; l < numLanes; ++l)
            if (! taken[l] && (int) value (laneId (l, "note")) == pitch) { laneFor[pitch] = l; taken[l] = true; break; }
    for (auto& [pitch, c] : order)
        if (laneFor[pitch] < 0)
            for (int l = 0; l < numLanes; ++l)
                if (! taken[l]) { laneFor[pitch] = l; taken[l] = true; setRealValue (laneId (l, "note"), (float) pitch); break; }
    for (int l = 0; l < numLanes; ++l)
    {
        setRealValue (laneId (l, "on"), taken[l] ? 1.0f : 0.0f);
        if (! taken[l]) continue;
        setRealValue (laneId (l, "mute"), 0.0f);
        setRealValue (laneId (l, "mode"), 1.0f);                 // drawn
        setRealValue (laneId (l, "steps"), (float) numSteps);
        setRealValue (laneId (l, "rate"), 3.0f);                 // 1/16
        setRealValue (laneId (l, "span"), 0.0f);
        setRealValue (laneId (l, "rot"), 0.0f);
        setRealValue (laneId (l, "prob"), 100.0f);
        for (int k = 0; k < maxSteps; ++k) engine.setStep (l, k, Step());
    }
    for (auto& n : notes)
    {
        const int l = laneFor[jlimit (0, 127, n.pitch)];
        const int k = (int) std::round (n.start / 0.25);
        if (l < 0 || k >= numSteps) continue;
        Step st;
        st.on = true;
        st.vel = n.velocity;
        st.micro = jlimit (-50, 50, roundToInt ((n.start - k * 0.25) / 0.25 * 100.0));
        st.len = jlimit (10, 400, roundToInt (n.length / 0.25 * 100.0));
        engine.setStep (l, jmin (k, numSteps - 1), st);
    }
}

void Processor::bakeMutation()
{
    syncEngineFromParams();
    const uint32 salt = (uint32) Time::getMillisecondCounter();
    for (int lane = 0; lane < numLanes; ++lane)
    {
        if ((lane == 0 && engine.g.preserveKick) || (lane == 1 && engine.g.preserveSnare)) continue;
        const auto& L = engine.lanes[(size_t) lane];
        if (! L.on) continue;
        const double amount = jmax (0.25, engine.g.mutation);
        const int rot = (int) std::round ((Engine::hash01 (salt, lane, 1) * 2.0 - 1.0) * amount * L.steps * 0.25);
        bool pattern[maxSteps] {};
        for (int p = 0; p < L.steps; ++p)
        {
            bool a = engine.baseActive (lane, p - rot + (int64) L.steps * 64);
            if (Engine::hash01 (salt, p, 2 + lane) < amount * 0.25) a = ! a;
            pattern[p] = a;
        }
        for (int p = 0; p < L.steps; ++p)
        {
            auto s = engine.getStep (lane, p);
            s.on = pattern[p];
            // accents inverted on some hits
            if (s.on && Engine::hash01 (salt, p, 30 + lane) < amount * 0.3) s.vel = jlimit (1, 127, 140 - s.vel);
            engine.setStep (lane, p, s);
        }
        setRealValue (laneId (lane, "mode"), 1.0f);
    }
}

void Processor::copySlot (int from, int to)
{
    for (int l = 0; l < numLanes; ++l)
        for (int s = 0; s < maxSteps; ++s)
            engine.steps[jlimit (0, numSlots - 1, to)][l][s].store (engine.steps[jlimit (0, numSlots - 1, from)][l][s].load());
}

MidiFile Processor::exportMidi (int bars) const
{
    const_cast<Processor*> (this)->syncEngineFromParams();
    MidiMessageSequence seq;
    const double ticks = 960.0;
    const double beats = bars * engine.g.beatsPerBar;
    auto& g = const_cast<Engine&> (engine).g;
    const double oldStart = g.startPpq;
    g.startPpq = 0.0;
    engine.collect (0.0, beats, [&] (const Event& e)
    {
        const double t = jmax (0.0, e.ppq) * ticks;
        seq.addEvent (MidiMessage::noteOn (e.channel, e.note, (uint8) e.vel).withTimeStamp (t));
        seq.addEvent (MidiMessage::noteOff (e.channel, e.note).withTimeStamp (jmin (beats * ticks, t + e.lengthBeats * ticks)));
    });
    g.startPpq = oldStart;
    seq.sort();
    seq.updateMatchedPairs();
    MidiMessageSequence tempo;
    tempo.addEvent (MidiMessage::tempoMetaEvent ((int) (60000000.0 / jmax (20.0, engine.g.bpm))));
    MidiFile f;
    f.setTicksPerQuarterNote ((int) ticks);
    f.addTrack (tempo);
    f.addTrack (seq);
    return f;
}

File Processor::exportMidiFile (int bars) const
{
    auto dir = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Mole");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Mole " + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".mid", false);
    FileOutputStream out (file);
    if (out.openedOk())
    {
        auto mf = exportMidi (bars);
        mf.writeTo (out);
    }
    return file;
}

// ===================================================================== state
void Processor::saveExtraState (ValueTree& v) const
{
    // the drawn steps of the 8 patterns, packed; only the lanes that have something
    for (int s = 0; s < numSlots; ++s)
        for (int l = 0; l < numLanes; ++l)
        {
            MemoryBlock mb;
            bool any = false;
            for (int k = 0; k < maxSteps; ++k)
            {
                const uint64 x = engine.steps[s][l][k].load();
                mb.append (&x, sizeof (x));
                any = any || x != Step().pack();
            }
            if (any) v.setProperty ("steps_" + String (s) + "_" + String (l), mb.toBase64Encoding(), nullptr);
        }
    // the learned groove
    String gr;
    for (int i = 0; i < 16; ++i) gr << String (engine.learned.offset[(size_t) i], 4) << " " << String (engine.learned.velocity[(size_t) i], 4) << " ";
    v.setProperty ("learned", gr.trim(), nullptr);
    v.setProperty ("kit", ownKit.kitId(), nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    for (int s = 0; s < numSlots; ++s)
        for (int l = 0; l < numLanes; ++l)
        {
            const auto key = "steps_" + String (s) + "_" + String (l);
            MemoryBlock mb;
            if (v.hasProperty (key) && mb.fromBase64Encoding (v.getProperty (key).toString()) && mb.getSize() == maxSteps * sizeof (uint64))
            {
                auto* x = static_cast<const uint64*> (mb.getData());
                for (int k = 0; k < maxSteps; ++k) engine.steps[s][l][k].store (x[k]);
            }
            else for (int k = 0; k < maxSteps; ++k) engine.steps[s][l][k].store (Step().pack());
        }
    auto t = StringArray::fromTokens (v.getProperty ("learned").toString(), " ", {});
    Groove gr;
    if (t.size() == 32)
        for (int i = 0; i < 16; ++i) { gr.offset[(size_t) i] = t[i * 2].getFloatValue(); gr.velocity[(size_t) i] = t[i * 2 + 1].getFloatValue(); }
    engine.learned = gr;
    ownKit.loadKit (v.getProperty ("kit").toString());
}

// ===================================================================== presets (the groove library)
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    // one lane: on, steps, pulses, rotation (and optionally rate, velocity)
    auto lane = [] (V& v, int i, int steps, int pulses, int rot, int rate = 3, int vel = -1)
    {
        v.push_back ({ laneId (i, "on"), 1.0f });
        v.push_back ({ laneId (i, "steps"), (float) steps });
        v.push_back ({ laneId (i, "pulses"), (float) pulses });
        v.push_back ({ laneId (i, "rot"), (float) rot });
        v.push_back ({ laneId (i, "rate"), (float) rate });
        if (vel > 0) v.push_back ({ laneId (i, "vel"), (float) vel });
    };
    auto off = [] (V& v, int i) { v.push_back ({ laneId (i, "on"), 0.0f }); };
    auto base = [&] () { V v; for (int i = 0; i < numLanes; ++i) off (v, i); return v; };
    std::vector<FactoryPreset> p;
    auto add = [&p] (const String& name, const String& cat, V v) { p.push_back ({ name, cat, std::move (v) }); };

    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 4, 2, 3, 85); add ("Inicial", "Inicio", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 2, 16, 2, 4); lane (v, 3, 16, 4, 2, 3, 80); lane (v, 4, 16, 4, 2, 3, 70);
      lane (v, 11, 16, 16, 0, 3, 60); v.push_back ({ "swing", 56.0f }); v.push_back ({ "humamt", 30.0f });
      v.push_back ({ laneId (11, "accevery"), 4.0f }); add ("House clásico"_u, "House", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 2, 16, 2, 4); lane (v, 3, 16, 8, 1, 3, 70); lane (v, 12, 16, 5, 3, 3, 75);
      v.push_back ({ "groove", 6.0f }); v.push_back ({ "grooveamt", 70.0f }); v.push_back ({ "fillevery", 3.0f }); add ("Deep House perezoso", "House", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 3, 16, 4, 2, 3, 90); lane (v, 5, 16, 16, 0, 3, 55); lane (v, 7, 16, 3, 3, 3, 70);
      v.push_back ({ laneId (5, "accevery"), 4.0f }); v.push_back ({ "fillevery", 3.0f }); v.push_back ({ "breakevery", 2.0f });
      v.push_back ({ "breaktype", 2.0f }); add ("Techno de bodega", "Techno", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 3, 16, 7, 2, 3, 80); lane (v, 7, 16, 5, 1, 3, 75); lane (v, 15, 12, 5, 0, 4, 70);
      v.push_back ({ laneId (3, "ratprob"), 15.0f }); v.push_back ({ laneId (3, "rat"), 2.0f }); v.push_back ({ "mutation", 30.0f });
      v.push_back ({ "mutevery", 3.0f }); add ("Techno con polirritmo", "Techno", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 16, 0, 3, 70); lane (v, 4, 16, 2, 6, 3, 80);
      v.push_back ({ laneId (3, "accevery"), 2.0f }); v.push_back ({ laneId (3, "accent"), 40.0f }); v.push_back ({ "swing", 54.0f });
      add ("Acid de 303", "Acid", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 8, 0, 3, 75); lane (v, 4, 16, 2, 2, 3, 80);
      lane (v, 12, 16, 3, 2, 3, 80); lane (v, 14, 16, 2, 6, 3, 70); v.push_back ({ "groove", 5.0f }); v.push_back ({ "grooveamt", 80.0f });
      v.push_back ({ "humamt", 40.0f }); v.push_back ({ "human", 1.0f }); add ("Indie Dance con cencerro", "Indie Dance", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 4, 2, 3, 90); lane (v, 9, 16, 3, 1, 3, 75);
      v.push_back ({ "fillevery", 2.0f }); v.push_back ({ "fillcomp", 70.0f }); add ("Dark Disco motorik", "Dark Disco", v); }
    { V v = base(); lane (v, 0, 16, 3, 0); lane (v, 1, 16, 2, 4); lane (v, 2, 16, 1, 12); lane (v, 3, 16, 8, 0, 3, 75);
      v.push_back ({ laneId (0, "rot"), 0.0f }); add ("Electro roto", "Electro", v); }
    { V v = base(); lane (v, 0, 16, 5, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 12, 0, 3, 70); lane (v, 8, 16, 2, 11, 3, 80);
      v.push_back ({ "swing", 58.0f }); v.push_back ({ "humamt", 50.0f }); v.push_back ({ "human", 2.0f }); add ("Breakbeat", "Breakbeat", v); }
    { V v = base(); lane (v, 0, 16, 3, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 16, 0, 3, 70); lane (v, 7, 16, 4, 3, 3, 60);
      v.push_back ({ "groove", 5.0f }); v.push_back ({ "grooveamt", 100.0f }); v.push_back ({ "human", 1.0f }); v.push_back ({ "humamt", 60.0f });
      add ("Funk de 16", "Funk", v); }
    { V v = base(); lane (v, 0, 16, 5, 0); lane (v, 3, 12, 7, 0, 4, 70); lane (v, 12, 7, 3, 0, 3, 75); lane (v, 13, 5, 2, 0, 3, 75);
      lane (v, 15, 9, 4, 1, 5, 60); v.push_back ({ "mutation", 50.0f }); v.push_back ({ "mutevery", 2.0f });
      add ("Polirritmos 3-5-7", "Experimental", v); }
    { V v = base(); lane (v, 0, 16, 4, 0); lane (v, 1, 16, 2, 4); lane (v, 3, 16, 8, 1, 3, 70); lane (v, 12, 3, 3, 0, 3, 75);
      v.push_back ({ laneId (12, "span"), 2.0f }); lane (v, 13, 4, 4, 0, 3, 75); v.push_back ({ laneId (13, "span"), 3.0f });
      add ("Tresillo contra cuatro (3:2, 4:3)", "Experimental", v); }
    return p;
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return {  };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

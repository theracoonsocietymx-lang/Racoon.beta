#include "FerretProcessor.h"
#include "FerretEditor.h"

namespace racoon::ferret
{
using namespace juce;

StringArray modeNames() { return { "Guitarrista", "Arpegio con púa"_u, "Instrumento" }; }
StringArray pickPatternNames() { return { "Subir", "Bajar", "Subir y bajar", "Travis (bajo alterno)", "Arpegio de 3", "Rodar", "Al azar" }; }
StringArray rateNames() { return { "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }; }
double rateBeats (int i)
{
    static const double b[] { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[jlimit (0, 4, i)];
}

static StringArray complexityNames() { return { "Sencillo", "Medio", "Complejo" }; }
static StringArray strategyNames() { return { "Cuerdas al aire", "Cerca de la mano", "Menos movimiento", "Cuerdas graves", "Cuerdas agudas" }; }
static StringArray userLenNames() { return { "16 pasos", "32 pasos", "48 pasos", "64 pasos" }; }
static StringArray wahModeNames() { return { "Pedal", "Auto (LFO)", "Envolvente" }; }

static bool isHit (char t) { return t == 'D' || t == 'U' || t == 'd' || t == 'u' || t == 'A' || t == 'x' || t == 'm' || t == 'o'; }
static bool validToken (char t) { return isHit (t) || t == '.' || t == '-'; }

// deterministic randomness: the same seed and step always decide the same
static float hash01 (uint32 seed, int64 k, uint32 salt)
{
    uint64 x = (uint64) k * 0x9E3779B97F4A7C15ull ^ ((uint64) seed << 32) ^ ((uint64) salt * 0xBF58476D1CE4E5B9ull);
    x ^= x >> 31; x *= 0x94D049BB133111EBull; x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 32;
    return (float) ((x >> 40) & 0xffffff) / 16777216.0f;
}

// ===================================================================== the player's decisions
char stepToken (const PlayerSettings& ps, const char* pattern, int len, int64 step)
{
    if (len <= 0) return '.';
    auto base = [&] (int64 s) -> char
    {
        if (ps.mode == pickingMode) return 'o';
        const char c = pattern[(size_t) (((s % len) + len) % len)];
        return validToken (c) ? c : '.';
    };
    auto anticipated = [&] (int64 s)
    {
        // the hit on the downbeat comes a sixteenth early (a push)
        return ps.anticipation > 0.0f && ((s + 1) % 16) == 0 && isHit (base (s + 1)) && hash01 (ps.seed, s, 11) < ps.anticipation;
    };
    char t = base (step);
    if (anticipated (step)) t = base (step + 1);
    else if (anticipated (step - 1)) t = '-';

    // variation: the guitarist changes a stroke now and then
    if (ps.variation > 0.0f && hash01 (ps.seed, step, 23) < ps.variation * 0.5f)
    {
        const float r = hash01 (ps.seed, step, 29);
        switch (t)
        {
            case 'D': t = r < 0.5f ? 'd' : 'A'; break;
            case 'U': t = r < 0.5f ? 'u' : '.'; break;
            case '.': t = r < 0.5f ? 'x' : (step % 2 ? 'u' : 'd'); break;
            case 'x': t = r < 0.5f ? '.' : 'D'; break;
            case 'm': t = r < 0.3f ? 'x' : 'm'; break;
            case '-': t = r < 0.3f ? 'u' : '-'; break;
            default: break;
        }
    }
    // density: fewer strokes off the beat, or extra ghost strokes in the gaps
    if (ps.density < 0.5f)
    {
        if (isHit (t) && t != 'A' && (step % 4) != 0 && hash01 (ps.seed, step, 37) < (0.5f - ps.density) * 2.0f) t = ps.restMute ? '.' : '-';
    }
    else if ((t == '.' || t == '-') && ps.mode == playerMode && hash01 (ps.seed, step, 41) < (ps.density - 0.5f) * 0.6f)
        t = (step % 2) ? 'u' : 'd';
    return t;
}

static int pickIndex (int pattern, int64 step, int n, uint32 seed)
{
    if (n <= 1) return 0;
    const int s = (int) (step % 4096);
    switch (pattern)
    {
        case 0: return s % n;
        case 1: return n - 1 - s % n;
        case 2: { const int p = 2 * n - 2, k = s % p; return k < n ? k : p - k; }
        case 3: return (s % 2 == 0) ? ((s / 2) % 2 == 0 ? 0 : jmin (1, n - 1)) : ((s / 2) % 2 == 0 ? n - 1 : jmax (0, n - 2));
        case 4: { const int k = s % 3; return k == 0 ? 0 : (k == 1 ? jmax (0, n - 2) : n - 1); }
        case 5: return jmax (0, n - 3) + s % jmin (3, n);
        default: return (int) (hash01 (seed, step, 53) * (float) n) % n;
    }
}

int stepHits (const PlayerSettings& ps, const char* pattern, int len, int64 step, const Voicing& v, Hit* out, int maxHits)
{
    const char t = stepToken (ps, pattern, len, step);
    int snd[numStrings], n = 0;
    for (int s = 0; s < numStrings; ++s) if (v.fret[s] >= 0) snd[n++] = s;
    int count = 0;
    if (t == '.')
    {
        if (ps.restMute && maxHits > 0) { out[0] = Hit {}; out[0].choke = true; count = 1; }
        return count;
    }
    if (t == '-' || n == 0) return 0;

    const float base = 0.45f + 0.5f * ps.intensity;
    const double swingDelay = (step % 2 == 1) ? ps.swing * ps.stepSeconds * 0.33 : 0.0;
    const double human = hash01 (ps.seed, step, 61) * ps.humanMs * 0.001;
    const double startDelay = swingDelay + human;
    const float velJitter = 1.0f - ps.humanVel * hash01 (ps.seed, step, 67);
    Art art = (ps.art == slide || ps.art == hammerPull || ps.art == pickScrape) ? sustain : ps.art;

    auto add = [&] (int s, float vel, double delay, Art a)
    {
        if (count >= maxHits) return;
        Hit h;
        h.string = s; h.fret = v.fret[s];
        h.velocity = jlimit (0.05f, 1.0f, vel * velJitter * (1.0f - ps.humanVel * 0.3f * hash01 (ps.seed, step * 8 + s, 71)));
        h.delaySeconds = startDelay + delay + hash01 (ps.seed, step * 8 + s, 73) * ps.humanMs * 0.0002;
        h.art = a;
        out[count++] = h;
    };
    const double spread = ps.strumMs * 0.001 * (1.15 - 0.3 * base);

    switch (t)
    {
        case 'D': case 'A': case 'd':
        {
            int first = 0;
            if (t == 'd') first = jmax (0, n - 3);
            else if (ps.intensity < 0.35f) first = jmax (0, n - 4);
            const float vel = t == 'A' ? jmin (1.0f, base + 0.25f) : t == 'd' ? base * 0.4f : base;
            for (int i = first; i < n; ++i) add (snd[i], vel, (i - first) * spread, art);
            break;
        }
        case 'U': case 'u':
        {
            const int first = jmax (0, n - (t == 'u' ? 3 : 4));
            const float vel = t == 'u' ? base * 0.35f : base * 0.8f;
            for (int i = n - 1, k = 0; i >= first; --i, ++k) add (snd[i], vel, k * spread, art);
            break;
        }
        case 'm':
            for (int i = 0; i < jmin (3, n); ++i) add (snd[i], base * 0.85f, i * spread * 0.7, palmMute);
            break;
        case 'x':
            for (int i = jmax (0, n - 4), k = 0; i < n; ++i, ++k) add (snd[i], base * 0.7f, k * spread * 0.5, deadNote);
            break;
        case 'o':
        {
            const int idx = pickIndex (ps.mode == pickingMode ? ps.pickPattern : 0, step, n, ps.seed);
            add (snd[idx], base * 0.85f + ((step % 4) == 0 ? 0.1f : 0.0f), 0.0, ps.art == pickScrape ? sustain : ps.art);
            break;
        }
        default: break;
    }
    return count;
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", modeNames(), 0);
    P::choice (l, "style", "Estilo", styleNames(), 1);
    P::choice (l, "complexity", "Complejidad", complexityNames(), 1);
    P::choice (l, "rate", "Rejilla", rateNames(), 2);
    P::pct (l, "intensity", "Intensidad", 0.0f, 100.0f, 65.0f);
    P::pct (l, "density", "Densidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "variation", "Variación"_u, 0.0f, 100.0f, 10.0f);
    P::pct (l, "swing", "Swing", 0.0f, 100.0f, 0.0f);
    P::pct (l, "anticip", "Anticipación"_u, 0.0f, 100.0f, 0.0f);
    P::ms (l, "strum", "Rasgueo", 0.0f, 60.0f, 12.0f);
    P::ms (l, "human", "Tiempo humano", 0.0f, 30.0f, 4.0f);
    P::pct (l, "humvel", "Fuerza humana", 0.0f, 100.0f, 20.0f);
    P::integer (l, "seed", "Semilla", 1, 999, 7);
    P::toggle (l, "restmute", "Apagar en silencios", true);
    P::choice (l, "ulen", "Largo del patrón"_u, userLenNames(), 0);
    P::choice (l, "pickpat", "Arpegio", pickPatternNames(), 2);

    P::choice (l, "model", "Guitarra", modelNames(), 0);
    P::choice (l, "tuning", "Afinación"_u, [] { StringArray s; for (auto& t : tunings()) s.add (String::fromUTF8 (t.name)); return s; }(), 0);
    P::integer (l, "capo", "Cejilla", 0, 12, 0);
    for (int s = 0; s < numStrings; ++s) P::integer (l, "off" + String (s + 1), "Cuerda " + String (6 - s) + " (semitonos)", -12, 12, 0);
    P::choice (l, "strategy", "Diapasón"_u, strategyNames(), 0);
    P::integer (l, "position", "Traste de la mano", 0, 12, 3);
    P::toggle (l, "openstr", "Cuerdas al aire", true);
    P::toggle (l, "latch", "Mantener acorde", false);
    P::choice (l, "art", "Articulación"_u, articulationNames(), 0);
    P::toggle (l, "ks", "Keyswitches (MIDI 24 a 33)", true);
    P::flt (l, "bendamt", "Bend", P::range (0.5f, 3.0f), 2.0f, "st", 1);
    P::flt (l, "vib", "Vibrato", P::range (0.0f, 1.0f), 0.3f, "st", 2);
    P::pct (l, "pickpos", "Lugar de la púa"_u, 5.0f, 50.0f, 18.0f);
    P::pct (l, "hardness", "Púa dura"_u, 0.0f, 100.0f, 60.0f);
    P::pct (l, "tone", "Tono", 0.0f, 100.0f, 70.0f);
    P::ms (l, "release", "Soltar", 5.0f, 400.0f, 40.0f, 60.0f);
    P::toggle (l, "dbl", "Doble toma", false);
    P::pct (l, "width", "Ancho", 0.0f, 100.0f, 70.0f);

    P::choice (l, "amp", "Amplificador", ampNames(), 0);
    P::pct (l, "gain", "Ganancia", 0.0f, 100.0f, 30.0f);
    P::pct (l, "bass", "Graves", 0.0f, 100.0f, 50.0f);
    P::pct (l, "mid", "Medios", 0.0f, 100.0f, 50.0f);
    P::pct (l, "treble", "Agudos", 0.0f, 100.0f, 55.0f);
    P::pct (l, "presence", "Presencia", 0.0f, 100.0f, 50.0f);
    P::pct (l, "sag", "Sag", 0.0f, 100.0f, 30.0f);
    P::db (l, "master", "Master", -24.0f, 12.0f, 0.0f);
    P::pct (l, "cabblend", "Gabinete / directo", 0.0f, 100.0f, 100.0f);
    P::choice (l, "cab", "Gabinete", cabNames(), 1);
    P::pct (l, "micpos", "Micro: centro a borde", 0.0f, 100.0f, 30.0f);
    P::pct (l, "micdist", "Micro: distancia", 0.0f, 100.0f, 20.0f);
    P::pct (l, "ribbon", "Micro de cinta", 0.0f, 100.0f, 0.0f);
    P::pct (l, "room", "Cuarto", 0.0f, 100.0f, 10.0f);

    P::toggle (l, "comp_on", "Compresor", false);
    P::pct (l, "comp_amt", "Compresión"_u, 0.0f, 100.0f, 50.0f);
    P::toggle (l, "wah_on", "Wah", false);
    P::choice (l, "wah_mode", "Wah: modo", wahModeNames(), 2);
    P::pct (l, "wah_pos", "Wah: pedal / sensibilidad", 0.0f, 100.0f, 50.0f);
    P::hz (l, "wah_rate", "Wah: velocidad", 0.1f, 8.0f, 1.0f);
    P::pct (l, "wah_q", "Wah: resonancia", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "od_on", "Overdrive", false);
    P::pct (l, "od_drive", "Drive", 0.0f, 100.0f, 40.0f);
    P::pct (l, "od_tone", "Drive: tono", 0.0f, 100.0f, 50.0f);
    P::db (l, "od_level", "Drive: volumen", -12.0f, 12.0f, 0.0f);
    P::toggle (l, "ch_on", "Chorus", false);
    P::hz (l, "ch_rate", "Chorus: velocidad", 0.05f, 5.0f, 0.8f);
    P::pct (l, "ch_depth", "Chorus: profundidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "ch_mix", "Chorus: mezcla", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "ph_on", "Phaser", false);
    P::hz (l, "ph_rate", "Phaser: velocidad", 0.05f, 5.0f, 0.4f);
    P::pct (l, "ph_depth", "Phaser: profundidad", 0.0f, 100.0f, 70.0f);
    P::pct (l, "ph_mix", "Phaser: mezcla", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "tr_on", "Tremolo", false);
    P::hz (l, "tr_rate", "Tremolo: velocidad", 0.5f, 15.0f, 5.0f);
    P::pct (l, "tr_depth", "Tremolo: profundidad", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "dl_on", "Delay", false);
    P::choice (l, "dl_div", "Delay: tiempo", divisionNames(), 8);
    P::pct (l, "dl_fb", "Delay: repeticiones", 0.0f, 90.0f, 35.0f);
    P::pct (l, "dl_mix", "Delay: mezcla", 0.0f, 100.0f, 25.0f);
    P::pct (l, "dl_tone", "Delay: tono", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "rv_on", "Reverb", true);
    P::pct (l, "rv_size", "Reverb: tamaño"_u, 0.0f, 100.0f, 40.0f);
    P::pct (l, "rv_damp", "Reverb: apagado", 0.0f, 100.0f, 50.0f);
    P::pct (l, "rv_mix", "Reverb: mezcla", 0.0f, 100.0f, 15.0f);
    P::db (l, "outgain", "Volumen", -24.0f, 12.0f, 0.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Ferret", "Laboratorio de guitarra", Colour (0xffd98a4e), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    std::fill (std::begin (noteString), std::end (noteString), -1);
    std::fill (std::begin (keyOnString), std::end (keyOnString), -1);
    const char* start = patterns()[0].steps;
    for (int i = 0; i < userSteps; ++i) userPattern[(size_t) i].store (start[i % 16]);
    for (int s = 0; s < numStrings; ++s) { shownFret[s].store (-1); voicingFret[s].store (-1); shownLevel[s].store (0.0f); }
    { int notes[] { 60, 64, 67 }; recognise (notes, 3); }   // builds the chord tables before the audio thread needs them
}

void Processor::openNotes (int* out) const
{
    const auto& t = tunings()[(size_t) jlimit (0, (int) tunings().size() - 1, (int) value ("tuning"))];
    const auto& m = models()[(size_t) jlimit (0, (int) models().size() - 1, (int) value ("model"))];
    static const char* offIds[] { "off1", "off2", "off3", "off4", "off5", "off6" };
    const int capo = (int) value ("capo");
    for (int s = 0; s < numStrings; ++s) out[s] = t.notes[s] + capo + (int) value (offIds[s]) + m.transpose;
}

PlayerSettings Processor::playerSettings (double bpmNow) const
{
    PlayerSettings ps;
    ps.mode = (int) value ("mode");
    ps.pickPattern = (int) value ("pickpat");
    ps.intensity = value ("intensity") * 0.01f;
    ps.density = value ("density") * 0.01f;
    ps.variation = value ("variation") * 0.01f;
    ps.swing = value ("swing") * 0.01f;
    ps.anticipation = value ("anticip") * 0.01f;
    ps.humanMs = value ("human");
    ps.humanVel = value ("humvel") * 0.01f;
    ps.strumMs = value ("strum");
    ps.seed = (uint32) value ("seed");
    ps.restMute = value ("restmute") > 0.5f;
    ps.art = (Art) jlimit (0, (int) numArts - 1, currentArt.load());
    ps.stepSeconds = rateBeats ((int) value ("rate")) * 60.0 / jlimit (20.0, 400.0, bpmNow);
    return ps;
}

// ===================================================================== patterns
void Processor::refreshPattern()
{
    const int style = (int) value ("style");
    if (style >= styleNames().size() - 1)
    {
        livePatternLen = 16 * (1 + (int) value ("ulen"));
        for (int i = 0; i < livePatternLen; ++i) livePattern[i] = userPattern[(size_t) i].load();
    }
    else if (auto* p = patternFor (style, (int) value ("complexity")))
    {
        livePatternLen = 16;
        for (int i = 0; i < 16; ++i) livePattern[i] = p->steps[i];
    }
}

String Processor::patternNow() const
{
    const int style = (int) value ("style");
    String s;
    if (style >= styleNames().size() - 1)
    {
        const int len = 16 * (1 + (int) value ("ulen"));
        for (int i = 0; i < len; ++i) s += String::charToString ((juce_wchar) userStep (i));
    }
    else if (auto* p = patternFor (style, (int) value ("complexity"))) s = p->steps;
    return s;
}

void Processor::copyStyleToUser()
{
    const auto now = patternNow();
    if (now.isEmpty()) return;
    for (int i = 0; i < userSteps; ++i) setUserStep (i, (char) now[i % now.length()]);
    setRealValue ("style", (float) (styleNames().size() - 1));
}

String Processor::chordName() const
{
    ChordInfo c;
    c.root = chordRoot.load(); c.type = chordType.load(); c.bass = chordBass.load();
    return c.root >= 0 && c.type >= 0 ? c.name() : String();
}

// ===================================================================== audio
void Processor::prepareDsp (double sampleRate, int maxBlock)
{
    for (int set = 0; set < numSets; ++set)
        for (int s = 0; s < numStrings; ++s) { strings[set][s].setSeed (1 + s + set * 17); strings[set][s].prepare (sampleRate); }
    for (auto& b : body) b.prepare (sampleRate);
    amp.prepare (sampleRate, 64);
    cab.prepare (sampleRate);
    pedals.prepare (sampleRate, maxBlock);
    work.setSize (2, 64);
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& set : strings) for (auto& s : set) s.reset();
    for (auto& b : body) b.reset();
    amp.reset(); cab.reset(); pedals.reset();
    numEvents = 0;
    heldCount = 0;
    std::fill (std::begin (keyVel), std::end (keyVel), 0.0f);
    std::fill (std::begin (noteString), std::end (noteString), -1);
    std::fill (std::begin (keyOnString), std::end (keyOnString), -1);
    for (auto& p : pendingRelease) p = false;
    sustainPedal = false; wheelSemis = 0.0f; modWheel = 0.0f;
    chordActive = false; internalRunning = false; internalPpq = 0.0;
    currentArt.store ((int) value ("art"));
}

void Processor::schedule (const Ev& e)
{
    if (numEvents < maxEvents) events[numEvents++] = e;
}

void Processor::fire (const Ev& e)
{
    const auto& m = models()[(size_t) jlimit (0, (int) models().size() - 1, (int) value ("model"))];
    auto fireOne = [&] (StringVoice& sv)
    {
        if (e.choke) { sv.choke (value ("release")); return; }
        int open[numStrings];
        openNotes (open);
        const double hz = 440.0 * std::pow (2.0, (open[e.string] + e.fret - 69) / 12.0);
        const float pick = value ("pickpos") * 0.01f * (e.set == 1 ? 1.15f : 1.0f);
        sv.pluck (hz, e.velocity, e.art, m, pick, value ("hardness") * 0.01f, e.legato, e.slideFromHz, value ("bendamt"));
        sv.fret = e.fret;
    };
    if (e.string < 0) { for (auto& sv : strings[e.set]) if (e.choke) sv.choke (value ("release")); return; }
    fireOne (strings[e.set][e.string]);
}

void Processor::chordChanged (int sample)
{
    int notes[128], n = 0;
    for (int i = 0; i < heldCount; ++i) notes[n++] = heldOrder[i];
    const auto c = recognise (notes, n);
    if (c.root < 0) return;
    int open[numStrings];
    openNotes (open);
    const auto strategy = (Strategy) jlimit (0, 4, (int) value ("strategy"));
    const bool allowOpen = value ("openstr") > 0.5f || strategy == Strategy::open;
    int pos = (int) value ("position");
    if (strategy == Strategy::open) pos = 1;
    else if (strategy == Strategy::leastMovement) pos = handFret;
    auto v = findVoicing (c, open, pos, allowOpen);
    for (int k = 0; k <= 12 && ! v.valid(); ++k) v = findVoicing (c, open, k, true);
    if (! v.valid()) return;
    if (strategy == Strategy::highStrings)
    {
        int high = 0;
        for (int s = 2; s < numStrings; ++s) if (v.fret[s] >= 0) ++high;
        if (high >= 3) v.fret[0] = v.fret[1] = -1;
    }
    else if (strategy == Strategy::lowStrings)
    {
        int low = 0;
        for (int s = 0; s < 4; ++s) if (v.fret[s] >= 0) ++low;
        if (low >= 3) v.fret[4] = v.fret[5] = -1;
    }
    double avg = 0.0; int k = 0;
    for (int f : v.fret) if (f > 0) { avg += f; ++k; }
    if (k > 0) handFret = roundToInt (avg / k);
    chord = c; voicing = v; chordActive = true;
    chordRoot.store (c.root); chordType.store (c.type); chordBass.store (c.bass);
    for (int s = 0; s < numStrings; ++s) voicingFret[s].store (v.fret[s]);

    if (isPlaying())
    {
        const int count = logCount.load();
        const bool same = count > 0 && std::equal (std::begin (v.fret), std::end (v.fret), std::begin (chordLog[count - 1].voicing.fret));
        const double now = ppq() + sample * bpm() / (60.0 * sr);
        if (! same)
        {
            // a chord that lasted less than a sixteenth was the hand still landing: it is replaced
            if (count > 0 && now - chordLog[count - 1].ppq < 0.25) chordLog[count - 1] = LogEntry { chordLog[count - 1].ppq, v, c.root, c.type, c.bass };
            else if (count < maxLog)
            {
                chordLog[count] = LogEntry { now, v, c.root, c.type, c.bass };
                logCount.store (count + 1, std::memory_order_release);
            }
        }
    }
}

void Processor::noteOn (int note, float vel, int sample)
{
    const int mode = (int) value ("mode");
    for (int i = 0; i < heldCount; ++i) if (heldOrder[i] == note) { std::memmove (heldOrder + i, heldOrder + i + 1, sizeof (int) * (size_t) (heldCount - i - 1)); --heldCount; break; }

    if (mode != instrumentMode)
    {
        heldOrder[heldCount++] = note;
        keyVel[note] = vel;
        chordDirty = true;   // resolved once every note of this instant has arrived
        return;
    }

    // instrument: each key is a note on a string
    int open[numStrings];
    openNotes (open);
    const Art art = (Art) currentArt.load();
    const int64 at = clock + sample;
    if (sample == lastNoteSample) ++sameTimeIndex; else sameTimeIndex = 0;
    lastNoteSample = sample;

    int s = -1, f = -1;
    bool legato = false;
    double slideFrom = 0.0;
    if ((art == hammerPull || art == slide) && heldCount > 0)
    {
        const int prev = heldOrder[heldCount - 1];
        const int ps = noteString[prev];
        if (ps >= 0 && note - open[ps] >= 0 && note - open[ps] <= numFrets)
        {
            s = ps; f = note - open[ps]; legato = true;
            slideFrom = strings[0][ps].currentHz();
            noteString[prev] = -1;
        }
    }
    if (s < 0)
    {
        bool busy[numStrings];
        for (int k = 0; k < numStrings; ++k) busy[k] = keyOnString[k] >= 0;
        const auto strategy = (Strategy) jlimit (0, 4, (int) value ("strategy"));
        int n = note;
        auto place = placeNote (n, open, strategy, handFret, busy);
        if (place.first < 0) place = placeNote (n, open, strategy, handFret, nullptr);
        if (place.first < 0)
        {
            while (n < open[0]) n += 12;
            while (n > open[numStrings - 1] + numFrets) n -= 12;
            place = placeNote (n, open, strategy, handFret, nullptr);
        }
        s = place.first; f = place.second;
    }
    if (s < 0) return;
    if (keyOnString[s] >= 0 && keyOnString[s] != note) noteString[keyOnString[s]] = -1;
    keyOnString[s] = note;
    noteString[note] = s;
    pendingRelease[s] = false;
    if (f > 0) handFret = f;
    heldOrder[heldCount++] = note;
    keyVel[note] = vel;

    const int64 when = at + (int64) (sameTimeIndex * value ("strum") * 0.001 * sr);
    schedule ({ when, 0, s, f, vel, art, false, legato, slideFrom });
    if (value ("dbl") > 0.5f)
        schedule ({ when + (int64) ((0.009 + 0.006 * hash01 (7, note, 3)) * sr), 1, s, f, vel * 0.92f, art, false, legato, slideFrom });
    // the screen names what the hand is playing
    const auto named = recognise (heldOrder, heldCount);
    chordRoot.store (heldCount >= 2 ? named.root : -1); chordType.store (named.type); chordBass.store (named.bass);
    for (int k = 0; k < numStrings; ++k) voicingFret[k].store (keyOnString[k] >= 0 ? keyOnString[k] - open[k] : -1);
}

void Processor::noteOff (int note, int sample)
{
    const int mode = (int) value ("mode");
    int idx = -1;
    for (int i = 0; i < heldCount; ++i) if (heldOrder[i] == note) { idx = i; break; }
    if (idx >= 0) { std::memmove (heldOrder + idx, heldOrder + idx + 1, sizeof (int) * (size_t) (heldCount - idx - 1)); --heldCount; }

    if (mode != instrumentMode)
    {
        // the chord changes when keys go down, not while they are let go one by one
        if (heldCount > 0) return;
        if (value ("latch") < 0.5f)
        {
            chordActive = false;
            internalRunning = false;
            schedule ({ clock + sample, 0, -1, 0, 0.0f, sustain, true, false, 0.0 });
            schedule ({ clock + sample, 1, -1, 0, 0.0f, sustain, true, false, 0.0 });
        }
        return;
    }

    const int s = noteString[note];
    noteString[note] = -1;
    if (s < 0 || keyOnString[s] != note) return;
    keyOnString[s] = -1;
    // pull-off: the finger lifts and the note held before sounds again on the same string
    if ((Art) currentArt.load() == hammerPull)
        for (int i = heldCount - 1; i >= 0; --i)
        {
            const int prev = heldOrder[i];
            int open[numStrings];
            openNotes (open);
            const int f = prev - open[s];
            if (noteString[prev] < 0 && f >= 0 && f <= numFrets)
            {
                keyOnString[s] = prev; noteString[prev] = s;
                schedule ({ clock + sample, 0, s, f, keyVel[prev] * 0.8f, hammerPull, false, true, strings[0][s].currentHz() });
                if (value ("dbl") > 0.5f) schedule ({ clock + sample, 1, s, f, keyVel[prev] * 0.75f, hammerPull, false, true, strings[1][s].currentHz() });
                return;
            }
        }
    if (sustainPedal) { pendingRelease[s] = true; return; }
    schedule ({ clock + sample, 0, s, 0, 0.0f, sustain, true, false, 0.0 });
    schedule ({ clock + sample, 1, s, 0, 0.0f, sustain, true, false, 0.0 });
}

void Processor::runSteps (double p0, double p1, int64 chunkClock, double spp)
{
    const double stepLen = rateBeats ((int) value ("rate"));
    const double half = 0.5 / spp;
    const auto ps = playerSettings (bpm());
    const bool dbl = value ("dbl") > 0.5f;
    for (int64 k = (int64) std::ceil ((p0 - half) / stepLen - 1.0e-9); (double) k * stepLen < p1 - half; ++k)
    {
        if (k < 0) continue;
        const int64 at = chunkClock + (int64) std::floor (((double) k * stepLen - p0) * spp + 0.5);
        Hit hits[16];
        const int m = stepHits (ps, livePattern, livePatternLen, k, voicing, hits, 16);
        currentStep.store ((int) (k % jmax (1, livePatternLen)));
        for (int i = 0; i < m; ++i)
        {
            const auto& h = hits[i];
            const int64 when = at + (int64) std::floor (h.delaySeconds * sr + 0.5);
            schedule ({ when, 0, h.string, h.fret, h.velocity, h.art, h.choke, false, 0.0 });
            if (dbl)
                schedule ({ when + (int64) ((0.009 + 0.008 * hash01 (ps.seed, k * 8 + i, 97)) * sr), 1, h.string, h.fret,
                            h.velocity * (0.85f + 0.1f * hash01 (ps.seed, k * 8 + i, 89)), h.art, h.choke, false, 0.0 });
        }
    }
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (clearLogRequest.exchange (false)) logCount.store (0);
    refreshPattern();
    {
        const int artParam = (int) value ("art");
        if (artParam != lastArtParam) { currentArt.store (artParam); lastArtParam = artParam; }
    }
    const double spp = sr * 60.0 / jlimit (20.0, 400.0, bpm());
    const int mode = (int) value ("mode");
    if (mode != lastMode)
    {
        // a different way of playing starts clean
        if (lastMode >= 0)
        {
            heldCount = 0; chordActive = false; internalRunning = false;
            std::fill (std::begin (noteString), std::end (noteString), -1);
            std::fill (std::begin (keyOnString), std::end (keyOnString), -1);
            schedule ({ clock, 0, -1, 0, 0.0f, sustain, true, false, 0.0 });
            schedule ({ clock, 1, -1, 0, 0.0f, sustain, true, false, 0.0 });
        }
        lastMode = mode;
    }
    const bool ks = value ("ks") > 0.5f;
    const auto& model = models()[(size_t) jlimit (0, (int) models().size() - 1, (int) value ("model"))];
    const bool dbl = value ("dbl") > 0.5f;
    const float width = value ("width") * 0.01f;
    const float tone = value ("tone") * 0.01f;
    const float outGain = dsp::dbToGain (value ("outgain"));

    Amp::Settings as;
    as.model = (int) value ("amp"); as.gain = value ("gain") * 0.01f; as.bass = value ("bass") * 0.01f; as.mid = value ("mid") * 0.01f;
    as.treble = value ("treble") * 0.01f; as.presence = value ("presence") * 0.01f; as.sag = value ("sag") * 0.01f; as.masterDb = value ("master");
    Cabinet::Settings cs;
    cs.type = (int) value ("cab"); cs.micPos = value ("micpos") * 0.01f; cs.distance = value ("micdist") * 0.01f; cs.ribbon = value ("ribbon") * 0.01f;
    cs.room = value ("room") * 0.01f; cs.blend = value ("cabblend") * 0.01f;
    Pedals::Settings fx;
    fx.comp = value ("comp_on") > 0.5f; fx.compAmount = value ("comp_amt") * 0.01f;
    fx.wah = value ("wah_on") > 0.5f; fx.wahMode = (int) value ("wah_mode"); fx.wahPos = value ("wah_pos") * 0.01f; fx.wahRate = value ("wah_rate"); fx.wahQ = value ("wah_q") * 0.01f;
    fx.od = value ("od_on") > 0.5f; fx.odDrive = value ("od_drive") * 0.01f; fx.odTone = value ("od_tone") * 0.01f; fx.odLevel = value ("od_level");
    fx.chorus = value ("ch_on") > 0.5f; fx.chRate = value ("ch_rate"); fx.chDepth = value ("ch_depth") * 0.01f; fx.chMix = value ("ch_mix") * 0.01f;
    fx.phaser = value ("ph_on") > 0.5f; fx.phRate = value ("ph_rate"); fx.phDepth = value ("ph_depth") * 0.01f; fx.phMix = value ("ph_mix") * 0.01f;
    fx.trem = value ("tr_on") > 0.5f; fx.trRate = value ("tr_rate"); fx.trDepth = value ("tr_depth") * 0.01f;
    fx.delay = value ("dl_on") > 0.5f; fx.dlSeconds = (float) syncSeconds ((int) value ("dl_div"), bpm()); fx.dlFeedback = value ("dl_fb") * 0.01f;
    fx.dlMix = value ("dl_mix") * 0.01f; fx.dlTone = value ("dl_tone") * 0.01f;
    fx.reverb = value ("rv_on") > 0.5f; fx.rvSize = value ("rv_size") * 0.01f; fx.rvDamp = value ("rv_damp") * 0.01f; fx.rvMix = value ("rv_mix") * 0.01f;

    const float vib = modWheel * 0.6f + ((Art) currentArt.load() == vibrato ? value ("vib") : 0.0f);

    auto processChunk = [&] (int pos, int len)
    {
        if (mode != instrumentMode && chordActive && voicing.valid())
        {
            if (isPlaying()) runSteps (ppq() + pos / spp, ppq() + (pos + len) / spp, clock + pos, spp);
            else if (internalRunning) { runSteps (internalPpq, internalPpq + len / spp, clock + pos, spp); internalPpq += len / spp; }
        }
        work.clear();
        float* L = work.getWritePointer (0);
        float* R = work.getWritePointer (1);
        for (int set = 0; set < numSets; ++set)
            for (auto& sv : strings[set]) sv.offsetSemis = wheelSemis + (set == 1 ? 0.03 : 0.0);
        const int64 t0 = clock + pos;
        int cur = 0;
        while (true)
        {
            int64 earliest = std::numeric_limits<int64>::max();
            for (int e = 0; e < numEvents; ++e) earliest = jmin (earliest, events[e].at);
            const int next = earliest < t0 + len ? jmax (cur, (int) (earliest - t0)) : len;
            if (next > cur)
            {
                for (auto& sv : strings[0]) sv.render (L + cur, next - cur, model, vib, model.pickupPos);
                if (dbl) for (auto& sv : strings[1]) sv.render (R + cur, next - cur, model, vib, model.pickupPos * 1.2f);
            }
            if (next >= len) break;
            const int64 now = t0 + next;
            for (int e = 0; e < numEvents;)
                if (events[e].at <= now) { fire (events[e]); events[e] = events[--numEvents]; }
                else ++e;
            cur = next;
        }
        for (int i = 0; i < len; ++i)
        {
            const float a = body[0].process (L[i], model, tone);
            const float b = dbl ? body[1].process (R[i], model, tone) : a;
            L[i] = a * (1.0f + width) * 0.5f + b * (1.0f - width) * 0.5f;
            R[i] = b * (1.0f + width) * 0.5f + a * (1.0f - width) * 0.5f;
        }
        AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, 0, len);
        pedals.processPre (view, len, fx);
        amp.process (view, len, as);
        cab.process (view, len, cs);
        pedals.processPost (view, len, fx);
        for (int c = 0; c < jmin (2, buffer.getNumChannels()); ++c)
        {
            auto* o = buffer.getWritePointer (c, pos);
            const auto* w = work.getReadPointer (c);
            for (int i = 0; i < len; ++i)
            {
                const float x = w[i] * outGain;
                o[i] = std::abs (x) < 1.0f ? x : std::copysign (1.0f + std::tanh (std::abs (x) - 1.0f) * 0.5f, x);
            }
        }
    };

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int len = jmin (64, end - pos);
            processChunk (pos, len);
            pos += len;
        }
    };

    for (auto it = midi.begin(); it != midi.end();)
    {
        const auto ev = *it;
        ++it;
        const auto msg = ev.getMessage();
        const int sp = jlimit (0, n, ev.samplePosition);
        renderTo (sp);
        const bool lastOfInstant = it == midi.end() || (*it).samplePosition != ev.samplePosition;
        struct ResolveChord
        {
            Processor& p; bool last; int sp;
            ~ResolveChord()
            {
                if (! last || ! p.chordDirty) return;
                p.chordDirty = false;
                p.chordChanged (sp);
                if (! p.isPlaying() && ! p.internalRunning && p.chordActive) { p.internalRunning = true; p.internalPpq = 0.0; }
            }
        } resolve { *this, lastOfInstant, sp };
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (ks && note >= keyswitchLow && note < keyswitchLow + (int) numArts) currentArt.store (note - keyswitchLow);
            else noteOn (note, msg.getFloatVelocity(), sp);
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            if (! (ks && note >= keyswitchLow && note < keyswitchLow + (int) numArts)) noteOff (note, sp);
        }
        else if (msg.isSustainPedalOn()) sustainPedal = true;
        else if (msg.isSustainPedalOff())
        {
            sustainPedal = false;
            for (int s = 0; s < numStrings; ++s)
                if (pendingRelease[s])
                {
                    pendingRelease[s] = false;
                    schedule ({ clock + sp, 0, s, 0, 0.0f, sustain, true, false, 0.0 });
                    schedule ({ clock + sp, 1, s, 0, 0.0f, sustain, true, false, 0.0 });
                }
        }
        else if (msg.isController() && msg.getControllerNumber() == 1) modWheel = msg.getControllerValue() / 127.0f;
        else if (msg.isPitchWheel()) wheelSemis = (msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            heldCount = 0; chordActive = false; internalRunning = false;
            std::fill (std::begin (noteString), std::end (noteString), -1);
            std::fill (std::begin (keyOnString), std::end (keyOnString), -1);
            numEvents = 0;
            schedule ({ clock + sp, 0, -1, 0, 0.0f, sustain, true, false, 0.0 });
            schedule ({ clock + sp, 1, -1, 0, 0.0f, sustain, true, false, 0.0 });
        }
    }
    renderTo (n);
    midi.clear();
    clock += n;

    for (int s = 0; s < numStrings; ++s)
    {
        shownFret[s].store (strings[0][s].active() ? strings[0][s].fret : -1);
        shownLevel[s].store (strings[0][s].level());
    }
}

// ===================================================================== MIDI export
MidiFile Processor::exportMidi (int barsWhenNoLog) const
{
    constexpr int tpq = 960;
    MidiMessageSequence seq;
    const double tempo = jlimit (20.0, 400.0, bpm());
    seq.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / tempo)), 0.0);
    seq.addEvent (MidiMessage::textMetaEvent (3, "Ferret"), 0.0);

    int open[numStrings];
    openNotes (open);
    auto ps = playerSettings (tempo);
    if (ps.mode == instrumentMode) ps.mode = playerMode;
    const auto pat = patternNow();
    const int len = jmax (1, pat.length());
    std::vector<char> tokens ((size_t) len);
    for (int i = 0; i < len; ++i) tokens[(size_t) i] = (char) pat[i];
    const double stepLen = rateBeats ((int) value ("rate"));

    const int count = logCount.load (std::memory_order_acquire);
    double start = 0.0, end = barsWhenNoLog * 4.0;
    Voicing fallback;
    for (int s = 0; s < numStrings; ++s) fallback.fret[s] = voicingFret[s].load();
    if (count > 0)
    {
        start = std::floor (chordLog[0].ppq / 4.0) * 4.0;
        end = std::floor (chordLog[count - 1].ppq / 4.0) * 4.0 + 4.0;
    }
    else if (! fallback.valid())
    {
        MidiFile mf; mf.setTicksPerQuarterNote (tpq); seq.updateMatchedPairs(); mf.addTrack (seq); return mf;
    }

    double noteEnd[numStrings];
    int noteNum[numStrings];
    for (int s = 0; s < numStrings; ++s) { noteEnd[s] = -1.0; noteNum[s] = -1; }
    auto stop = [&] (int s, double tick)
    {
        if (noteNum[s] < 0) return;
        seq.addEvent (MidiMessage::noteOff (1, noteNum[s]), jmin (tick, noteEnd[s]));
        noteNum[s] = -1;
    };
    const double ticksPerSecond = tpq * tempo / 60.0;
    for (int64 k = (int64) std::ceil (start / stepLen - 1.0e-9); (double) k * stepLen < end - 1.0e-9; ++k)
    {
        const double p = (double) k * stepLen;
        const Voicing* v = &fallback;
        for (int i = 0; i < count; ++i) if (chordLog[i].ppq <= p + 1.0e-6) v = &chordLog[i].voicing;
        Hit hits[16];
        const int m = stepHits (ps, tokens.data(), len, k, *v, hits, 16);
        for (int i = 0; i < m; ++i)
        {
            const auto& h = hits[i];
            const double tick = (p - start) * tpq + h.delaySeconds * ticksPerSecond;
            if (h.choke) { for (int s = 0; s < numStrings; ++s) stop (s, tick); continue; }
            stop (h.string, tick);
            const int note = jlimit (0, 127, open[h.string] + h.fret);
            seq.addEvent (MidiMessage::noteOn (1, note, (uint8) jlimit (1, 127, roundToInt (h.velocity * 127.0f))), tick);
            noteNum[h.string] = note;
            const double maxLen = h.art == deadNote ? tpq / 16.0 : h.art == palmMute ? tpq / 5.0 : tpq * 4.0;
            noteEnd[h.string] = tick + maxLen;
        }
    }
    for (int s = 0; s < numStrings; ++s) stop (s, (end - start) * tpq);
    seq.sort();
    seq.updateMatchedPairs();
    MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    return mf;
}

File Processor::exportMidiFile (int bars) const
{
    auto dir = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Ferret");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Ferret " + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".mid", false);
    FileOutputStream out (file);
    if (out.openedOk()) exportMidi (bars).writeTo (out);
    return file;
}

// ===================================================================== state
void Processor::saveExtraState (ValueTree& v) const
{
    String up;
    for (int i = 0; i < userSteps; ++i) up += String::charToString ((juce_wchar) userStep (i));
    v.setProperty ("userPattern", up, nullptr);
    String log;
    const int count = logCount.load();
    for (int i = 0; i < count; ++i)
    {
        const auto& e = chordLog[i];
        log << String (e.ppq, 4) << ":" << e.root << ":" << e.type << ":" << e.bass << ":";
        for (int s = 0; s < numStrings; ++s) log << e.voicing.fret[s] << (s < numStrings - 1 ? "," : ";");
    }
    v.setProperty ("chordLog", log, nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    const auto up = v.getProperty ("userPattern").toString();
    for (int i = 0; i < userSteps && i < up.length(); ++i)
        setUserStep (i, validToken ((char) up[i]) ? (char) up[i] : '.');
    const auto entries = StringArray::fromTokens (v.getProperty ("chordLog").toString(), ";", "");
    int count = 0;
    for (auto& t : entries)
    {
        const auto f = StringArray::fromTokens (t, ":", "");
        if (f.size() < 5 || count >= maxLog) continue;
        LogEntry e;
        e.ppq = f[0].getDoubleValue(); e.root = f[1].getIntValue(); e.type = f[2].getIntValue(); e.bass = f[3].getIntValue();
        const auto frets = StringArray::fromTokens (f[4], ",", "");
        for (int s = 0; s < numStrings && s < frets.size(); ++s) e.voicing.fret[s] = jlimit (-1, numFrets, frets[s].getIntValue());
        chordLog[count++] = e;
    }
    logCount.store (count);
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Chic Disco Rhythm", "Disco", { { "mode", 0 }, { "style", 1 }, { "complexity", 2 }, { "model", 2 }, { "amp", 0 }, { "gain", 20 }, { "treble", 65 },
                                          { "comp_on", 1 }, { "comp_amt", 60 }, { "intensity", 70 }, { "swing", 8 }, { "restmute", 1 }, { "cab", 1 },
                                          { "rv_on", 1 }, { "rv_mix", 10 }, { "pickpos", 10 }, { "strum", 8 } } },
        { "Dark Post-Punk", "Post-Punk", { { "mode", 0 }, { "style", 5 }, { "complexity", 0 }, { "model", 1 }, { "amp", 1 }, { "gain", 35 }, { "ch_on", 1 },
                                           { "ch_mix", 60 }, { "dl_on", 1 }, { "dl_div", 8 }, { "dl_mix", 30 }, { "rv_on", 1 }, { "rv_size", 70 }, { "rv_mix", 30 },
                                           { "tuning", 0 }, { "restmute", 0 } } },
        { "Muted Funk", "Funk", { { "mode", 0 }, { "style", 0 }, { "complexity", 2 }, { "model", 0 }, { "amp", 3 }, { "gain", 25 }, { "wah_on", 1 },
                                  { "wah_mode", 2 }, { "wah_pos", 60 }, { "comp_on", 1 }, { "comp_amt", 70 }, { "variation", 20 }, { "swing", 15 }, { "rv_mix", 8 } } },
        { "Indie Arpeggio", "Indie", { { "mode", 1 }, { "pickpat", 2 }, { "rate", 2 }, { "model", 0 }, { "amp", 0 }, { "ch_on", 1 }, { "ch_mix", 40 },
                                       { "dl_on", 1 }, { "dl_div", 8 }, { "dl_fb", 45 }, { "dl_mix", 30 }, { "rv_on", 1 }, { "rv_mix", 25 }, { "restmute", 0 } } },
        { "Vintage Clean", "Limpio", { { "mode", 2 }, { "model", 1 }, { "amp", 0 }, { "gain", 15 }, { "cab", 3 }, { "tr_on", 1 }, { "tr_rate", 4.5f },
                                       { "tr_depth", 40 }, { "rv_on", 1 }, { "rv_size", 60 }, { "rv_mix", 25 }, { "tone", 55 } } },
        { "Acid Guitar", "Funk", { { "mode", 0 }, { "style", 0 }, { "complexity", 1 }, { "model", 1 }, { "amp", 2 }, { "gain", 45 }, { "wah_on", 1 },
                                   { "wah_mode", 1 }, { "wah_rate", 2.0f }, { "wah_q", 80 }, { "od_on", 1 }, { "od_drive", 55 }, { "dl_on", 1 }, { "dl_div", 12 }, { "dl_mix", 20 } } },
        { "Nu-Disco contratiempos", "Nu-Disco", { { "mode", 0 }, { "style", 3 }, { "complexity", 1 }, { "model", 2 }, { "amp", 0 }, { "comp_on", 1 },
                                                  { "ph_on", 1 }, { "ph_mix", 35 }, { "dbl", 1 }, { "width", 80 }, { "rv_mix", 18 } } },
        { "Rock crunch", "Rock", { { "mode", 0 }, { "style", 4 }, { "complexity", 1 }, { "model", 1 }, { "amp", 2 }, { "gain", 60 }, { "cab", 2 },
                                   { "dbl", 1 }, { "width", 90 }, { "restmute", 0 }, { "rv_mix", 12 } } },
        { "Indie Dance galope", "Indie", { { "mode", 0 }, { "style", 2 }, { "complexity", 2 }, { "model", 0 }, { "amp", 1 }, { "gain", 30 }, { "dl_on", 1 },
                                           { "dl_div", 10 }, { "dl_mix", 18 } } },
        { "Acústica rasgueada"_u, "Acústica"_u, { { "mode", 0 }, { "style", 4 }, { "complexity", 1 }, { "model", 4 }, { "amp", 5 }, { "cab", 4 },
                                                  { "restmute", 0 }, { "rv_on", 1 }, { "rv_mix", 20 }, { "strum", 18 } } },
        { "Nylon arpegiada", "Acústica"_u, { { "mode", 1 }, { "pickpat", 3 }, { "rate", 0 }, { "model", 5 }, { "amp", 5 }, { "cab", 4 }, { "restmute", 0 },
                                             { "hardness", 25 }, { "rv_on", 1 }, { "rv_mix", 22 } } },
        { "Barítono Dark Disco"_u, "Dark Disco", { { "mode", 0 }, { "style", 6 }, { "complexity", 2 }, { "model", 6 }, { "amp", 1 }, { "gain", 40 },
                                                   { "ch_on", 1 }, { "dl_on", 1 }, { "dl_div", 11 }, { "dl_mix", 22 }, { "rv_on", 1 }, { "rv_size", 75 } } },
        { "Instrumento directo", "Limpio", { { "mode", 2 }, { "model", 0 }, { "amp", 5 }, { "cab", 4 }, { "rv_on", 0 } } },
    };
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 3.9, 52, 96, 1 },
             { 0.0, 3.9, 55, 96, 1 },
             { 0.0, 3.9, 59, 96, 1 },
             { 0.0, 3.9, 64, 96, 1 },
             { 4.0, 3.9, 48, 96, 1 },
             { 4.0, 3.9, 52, 96, 1 },
             { 4.0, 3.9, 55, 96, 1 },
             { 4.0, 3.9, 60, 96, 1 },
             { 8.0, 3.9, 43, 96, 1 },
             { 8.0, 3.9, 47, 96, 1 },
             { 8.0, 3.9, 50, 96, 1 },
             { 8.0, 3.9, 55, 96, 1 },
             { 8.0, 3.9, 59, 96, 1 },
             { 12.0, 3.9, 50, 96, 1 },
             { 12.0, 3.9, 54, 96, 1 },
             { 12.0, 3.9, 57, 96, 1 },
             { 12.0, 3.9, 62, 96, 1 } };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

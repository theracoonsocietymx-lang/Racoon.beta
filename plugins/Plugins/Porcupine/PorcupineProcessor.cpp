#include "PorcupineProcessor.h"
#include "PorcupineEditor.h"

namespace racoon::porcupine
{
using namespace juce;

StringArray modeNames() { return { "Coro SATB (tus acordes)", "Armonizar melodía"_u, "Unísono (tus notas)"_u }; }
StringArray busNames() { return { "Mezcla", "Soprano", "Contralto", "Tenor", "Bajo", "Cuerdas", "Metales", "Sala" }; }

String Processor::prefix (int s)
{
    static const char* p[] { "sop", "alt", "ten", "bas", "str", "br" };
    return p[jlimit (0, numSections - 1, s)];
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", modeNames(), 0);
    P::choice (l, "key", "Tonalidad", keyNames(), 0);
    P::choice (l, "scale", "Escala", { "Mayor", "Menor" }, 0);
    P::choice (l, "spacing", "Voces", { "Cerradas", "Abiertas" }, 0);
    P::pct (l, "smooth", "Movimiento suave", 0.0f, 100.0f, 50.0f);

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vowel", 1 }, "Vocal", NormalisableRange<float> (0.0f, 4.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                  {
                                                      const auto n = vowelNames();
                                                      const int a = jlimit (0, 4, roundToInt (v));
                                                      return std::abs (v - (float) a) < 0.15f ? n[a] : n[(int) v] + "-" + n[jmin (4, (int) v + 1)];
                                                  })));
    P::pct (l, "mouth", "Boca cerrada", 0.0f, 100.0f, 0.0f);
    P::pct (l, "vmove", "Vocal en movimiento", 0.0f, 100.0f, 0.0f);
    P::hz (l, "vrate", "Velocidad de la vocal", 0.05f, 2.0f, 0.2f);
    P::choice (l, "sylon", "Sílabas"_u, { "Apagadas", "Una por acorde", "Al compás"_u }, 0);
    P::choice (l, "sylrate", "Sílabas: rejilla"_u, divisionNames(), 4);
    P::integer (l, "syllen", "Sílabas: pasos"_u, 1, sylSteps, 4);
    const int defaults[sylSteps] { 6, 9, 8, 10, 6, 9, 7, 5, 0, 1, 2, 3, 4, 6, 9, 0 };
    for (int i = 0; i < sylSteps; ++i) P::choice (l, sylId (i), "Sílaba "_u + String (i + 1), syllableNames(), defaults[i]);

    P::choice (l, "art", "Articulación"_u, articulationNames(), 0);
    P::toggle (l, "ks", "Keyswitches (MIDI 24 a 28)", true);
    P::choice (l, "dynsrc", "Dinámica con"_u, { "Velocidad", "Rueda (CC1)", "Expresión (CC11)"_u }, 0);
    P::pct (l, "dyn", "Dinámica"_u, 0.0f, 100.0f, 70.0f);
    P::ms (l, "attack", "Ataque", 5.0f, 2000.0f, 150.0f, 200.0f);
    P::ms (l, "release", "Soltar", 20.0f, 4000.0f, 400.0f, 500.0f);
    P::flt (l, "vibdepth", "Vibrato", P::range (0.0f, 100.0f), 18.0f, "ct", 0);
    P::hz (l, "vibrate", "Vibrato: velocidad", 3.0f, 8.0f, 5.2f);
    P::ms (l, "vibdelay", "Vibrato: retraso", 0.0f, 1500.0f, 300.0f);
    P::ms (l, "glide", "Legato: tiempo", 10.0f, 500.0f, 80.0f);

    P::flt (l, "drift", "Afinación humana"_u, P::range (0.0f, 30.0f), 5.0f, "ct", 1);
    P::ms (l, "scatter", "Entradas humanas", 0.0f, 60.0f, 12.0f);
    P::pct (l, "breath", "Aire", 0.0f, 100.0f, 20.0f);
    P::integer (l, "size", "Cantantes por sección"_u, 1, 16, 6);
    P::flt (l, "detune", "Desafinación del grupo"_u, P::range (0.0f, 40.0f), 12.0f, "ct", 1);
    P::pct (l, "width", "Ancho del grupo", 0.0f, 100.0f, 70.0f);

    const float pans[numChoir] { -50.0f, -20.0f, 20.0f, 50.0f };
    for (int s = 0; s < numChoir; ++s)
    {
        P::toggle (l, prefix (s) + "_on", sectionNames()[s], true);
        P::db (l, prefix (s) + "_lvl", sectionNames()[s] + ": volumen", -24.0f, 6.0f, 0.0f);
        P::pct (l, prefix (s) + "_pan", sectionNames()[s] + ": lugar", -100.0f, 100.0f, pans[s]);
    }
    P::toggle (l, "str_on", "Cuerdas", false);
    P::db (l, "str_lvl", "Cuerdas: volumen", -24.0f, 6.0f, -3.0f);
    P::pct (l, "str_pan", "Cuerdas: lugar", -100.0f, 100.0f, 0.0f);
    P::integer (l, "str_oct", "Cuerdas: octava", -1, 1, 0);
    P::integer (l, "str_size", "Cuerdas: músicos"_u, 1, 12, 6);
    P::ms (l, "str_att", "Cuerdas: ataque", 10.0f, 3000.0f, 300.0f, 400.0f);
    P::ms (l, "str_rel", "Cuerdas: soltar", 20.0f, 4000.0f, 600.0f, 600.0f);
    P::pct (l, "str_bright", "Cuerdas: brillo", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "br_on", "Metales", false);
    P::db (l, "br_lvl", "Metales: volumen", -24.0f, 6.0f, -4.0f);
    P::pct (l, "br_pan", "Metales: lugar", -100.0f, 100.0f, 30.0f);
    P::integer (l, "br_oct", "Metales: octava", -1, 1, 0);
    P::integer (l, "br_size", "Metales: músicos"_u, 1, 8, 4);
    P::ms (l, "br_att", "Metales: ataque", 10.0f, 1000.0f, 60.0f, 150.0f);
    P::pct (l, "br_bright", "Metales: brillo", 0.0f, 100.0f, 50.0f);

    P::pct (l, "depth", "Distancia", 0.0f, 100.0f, 30.0f);
    P::pct (l, "stagewidth", "Ancho del escenario", 0.0f, 100.0f, 80.0f);
    P::pct (l, "hall_size", "Sala: tamaño"_u, 0.0f, 100.0f, 65.0f);
    P::pct (l, "hall_damp", "Sala: apagado", 0.0f, 100.0f, 40.0f);
    P::pct (l, "hall_mix", "Sala", 0.0f, 100.0f, 25.0f);
    P::toggle (l, "multiout", "Secciones a salidas separadas", false);
    P::db (l, "outgain", "Volumen", -24.0f, 12.0f, 0.0f);
    return l;
}

AudioProcessor::BusesProperties Processor::buses()
{
    BusesProperties b;
    const auto names = busNames();
    for (int i = 0; i < numOutBuses; ++i) b = b.withOutput (names[i], AudioChannelSet::stereo(), i == 0);
    return b;
}

Processor::Processor()
    : RacoonProcessor (buses(), PluginInfo { "Porcupine", "Coro y ensamble", Colour (0xff7fbfa8), PluginKind::instrument, 1, 0.0, false, false }, layout())
{
    for (auto& s : satbShown) s.store (-1);
    for (auto& s : sectionLevel) s.store (0.0f);
    for (int s = 0; s < numSections; ++s)
    {
        secOn[s] = param (prefix (s) + "_on");
        secLvl[s] = param (prefix (s) + "_lvl");
        secPan[s] = param (prefix (s) + "_pan");
        secOct[s] = s >= numChoir ? param (prefix (s) + "_oct") : nullptr;
    }
    for (int i = 0; i < sylSteps; ++i) sylParam[i] = param (sylId (i));
}

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (! l.inputBuses.isEmpty() && ! l.getMainInputChannelSet().isDisabled()) return false;
    const auto main = l.getMainOutputChannelSet();
    if (main != AudioChannelSet::stereo() && main != AudioChannelSet::mono()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != AudioChannelSet::stereo()) return false;
    return true;
}

bool Processor::sectionOn (int s) const { return secOn[s]->load() > 0.5f; }

VoiceSettings Processor::settingsFor (int s) const
{
    VoiceSettings v;
    const float stageW = value ("stagewidth") * 0.01f;
    v.vowel = value ("vowel");
    v.hum = value ("mouth") * 0.01f;
    const int src = (int) value ("dynsrc");
    v.dynamics = jlimit (0.0f, 1.0f, value ("dyn") * 0.01f * (src == 1 ? cc1 : src == 2 ? cc11 : 1.0f));
    v.detuneCents = value ("detune");
    v.driftCents = value ("drift");
    v.width = value ("width") * 0.01f * stageW;
    v.pan = secPan[s]->load() * 0.01f * stageW;
    v.vibDepth = value ("vibdepth");
    v.vibRate = value ("vibrate");
    v.vibDelay = value ("vibdelay") * 0.001f;
    v.breath = value ("breath") * 0.01f;
    v.attack = value ("attack") * 0.001f;
    v.release = value ("release") * 0.001f;
    v.glideSeconds = value ("glide") * 0.001f;
    v.scatterMs = value ("scatter");
    v.size = (int) value ("size");
    if (s == strings)
    {
        v.size = (int) value ("str_size"); v.attack = value ("str_att") * 0.001f; v.release = value ("str_rel") * 0.001f;
        v.brightness = value ("str_bright") * 0.01f; v.vibDepth *= 0.6f; v.breath = 0.0f;
    }
    else if (s == brass)
    {
        v.size = (int) value ("br_size"); v.attack = value ("br_att") * 0.001f; v.brightness = value ("br_bright") * 0.01f;
        v.vibDepth *= 0.4f; v.detuneCents *= 0.6f; v.breath = 0.0f;
    }
    return v;
}

// ===================================================================== voices
Voice* Processor::findVoice (int section, int key)
{
    for (auto& v : voices) if (v.active && v.section == section && v.key == key) return &v;
    return nullptr;
}

Voice* Processor::allocVoice()
{
    for (auto& v : voices) if (! v.active) return &v;
    Voice* pick = nullptr;
    for (auto& v : voices) if (! v.gate && (pick == nullptr || v.level() < pick->level())) pick = &v;
    if (pick == nullptr) for (auto& v : voices) if (pick == nullptr || v.age < pick->age) pick = &v;
    return pick;
}

void Processor::currentSyllable (Consonant& c, float& vowel, float& hum) const
{
    if ((int) value ("sylon") == 0) { c = cNone; vowel = value ("vowel"); hum = value ("mouth") * 0.01f; return; }
    const int idx = jlimit (0, (int) syllables().size() - 1, (int) sylParam[jlimit (0, sylSteps - 1, sylStep)]->load());
    const auto& sy = syllables()[(size_t) idx];
    c = sy.consonant; vowel = sy.vowel; hum = jmax (sy.hum, value ("mouth") * 0.01f);
}

void Processor::startVoice (int section, int key, int note, float vel)
{
    Consonant c; float vowel, hum;
    currentSyllable (c, vowel, hum);
    auto s = settingsFor (section);
    s.vowel = vowel; s.hum = hum;
    const auto art = (Articulation) currentArt.load();
    if (auto* v = findVoice (section, key))
    {
        if (v->gate && art == legato)
        {
            v->start (section, key, note, vel, art, c, s, true);
            v->age = ++counter;
            return;
        }
        v->release();
        v->key = -1;
    }
    auto* v = allocVoice();
    v->prepare (sr);
    v->start (section, key, note, vel, art, c, s, false);
    v->age = ++counter;
}

void Processor::releaseLines()
{
    for (auto& v : voices) if (v.active && v.key >= 1000) v.release();
    satb = {};   // the screen keeps showing the last chord
}

void Processor::applyChord()
{
    const int mode = (int) value ("mode");
    if (heldCount == 0) { if (! sustainPedal) releaseLines(); return; }
    const bool open = (int) value ("spacing") == 1;
    const float smooth = value ("smooth") * 0.01f;
    int pcs[12], n = 0, bassPc = 0;
    Satb next;
    if (mode == harmonizeMode)
    {
        int melody = heldOrder[0];
        for (int i = 0; i < heldCount; ++i) melody = jmax (melody, heldOrder[i]);
        previousDegree = harmonize (melody, (int) value ("key"), (int) value ("scale") == 1, pcs, bassPc, previousDegree);
        degreeShown.store (previousDegree);
        next = voiceLead (pcs, 3, bassPc, satb, open, smooth, melody);
    }
    else
    {
        int sorted[128];
        std::copy (heldOrder, heldOrder + heldCount, sorted);
        std::sort (sorted, sorted + heldCount);
        for (int i = 0; i < heldCount; ++i)
        {
            const int pc = sorted[i] % 12;
            if (std::find (pcs, pcs + n, pc) == pcs + n && n < 12) pcs[n++] = pc;
        }
        bassPc = sorted[0] % 12;
        next = voiceLead (pcs, n, bassPc, satb, open, smooth);
    }
    if (! next.valid()) return;
    satb = next;
    for (int v = 0; v < numChoir; ++v) satbShown[v].store (satb.note[v]);

    const bool perChord = (int) value ("sylon") == 1;
    Consonant c; float vowel, hum;
    currentSyllable (c, vowel, hum);
    for (int s = 0; s < numSections; ++s)
    {
        if (! sectionOn (s)) continue;
        const int lines = s < numChoir ? 1 : (s == strings ? 4 : 3);
        for (int k = 0; k < lines; ++k)
        {
            const int line = s < numChoir ? s : k;
            int note = satb.note[line];
            if (note < 0) continue;
            if (s >= numChoir) note += 12 * (int) secOct[s]->load();
            const int key = 1000 + line;
            auto* v = findVoice (s, key);
            if (v != nullptr && v->gate && v->note == note)
            {
                if (perChord && s < numChoir) v->glideTo (note, c, vowel, hum);   // the same note sings the next syllable
                continue;
            }
            startVoice (s, key, note, lastVelocity);
        }
    }
    if (perChord) sylStep = (sylStep + 1) % jmax (1, (int) value ("syllen"));
}

void Processor::noteOn (int note, float vel)
{
    for (int i = 0; i < heldCount; ++i) if (heldOrder[i] == note) { std::memmove (heldOrder + i, heldOrder + i + 1, sizeof (int) * (size_t) (heldCount - i - 1)); --heldCount; break; }
    heldOrder[heldCount++] = note;
    keyVel[note] = vel;
    sustained[note] = false;
    lastVelocity = vel;
    if ((int) value ("mode") != unisonMode) { chordDirty = true; return; }

    // unison: every section sings the note in its own octave
    for (int s = 0; s < numSections; ++s)
    {
        if (! sectionOn (s)) continue;
        const auto r = rangeOf (s);
        int n = note + (s >= numChoir ? 12 * (int) secOct[s]->load() : 0);
        while (n < r.lo && n + 12 <= 127) n += 12;
        while (n > r.hi && n - 12 >= 0) n -= 12;
        startVoice (s, note, n, vel);
        if (s < numChoir) satbShown[s].store (n);
    }
    if ((int) value ("sylon") == 1) sylStep = (sylStep + 1) % jmax (1, (int) value ("syllen"));
}

void Processor::noteOff (int note)
{
    for (int i = 0; i < heldCount; ++i) if (heldOrder[i] == note) { std::memmove (heldOrder + i, heldOrder + i + 1, sizeof (int) * (size_t) (heldCount - i - 1)); --heldCount; break; }
    if ((int) value ("mode") != unisonMode)
    {
        if (heldCount == 0 && ! sustainPedal) releaseLines();
        return;
    }
    if (sustainPedal) { sustained[note] = true; return; }
    for (auto& v : voices) if (v.active && v.key == note) v.release();
}

// ===================================================================== audio
void Processor::prepareDsp (double sampleRate, int)
{
    for (auto& v : voices) v.prepare (sampleRate);
    sec.setSize (numSections * 2, 32);
    hallIn.setSize (2, 32);
    for (auto& s : preDelay) for (auto& d : s) d.prepare ((int) (0.03 * sampleRate) + 8);
    hall.setSampleRate (sampleRate);
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& v : voices) v.kill();
    for (auto& s : preDelay) for (auto& d : s) d.clear();
    for (auto& s : stageLp) s[0] = s[1] = 0.0f;
    hall.reset();
    heldCount = 0; sustainPedal = false; chordDirty = false;
    std::fill (std::begin (sustained), std::end (sustained), false);
    satb = {}; previousDegree = -1;
    cc1 = 1.0f; cc11 = 1.0f; volume = 1.0f;
    sylStep = 0; lastSylIndex = -1; internalRunning = false; internalPpq = 0.0;
    currentArt.store ((int) value ("art"));
    lastArtParam = (int) value ("art");
    for (auto& s : satbShown) s.store (-1);
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    buffer.clear();
    const int mode = (int) value ("mode");
    if (mode != lastMode)
    {
        if (lastMode >= 0) { for (auto& v : voices) v.release(); heldCount = 0; satb = {}; }
        lastMode = mode;
    }
    const int artParam = (int) value ("art");
    if (artParam != lastArtParam) { currentArt.store (artParam); lastArtParam = artParam; }
    const bool ks = value ("ks") > 0.5f;
    const int sylon = (int) value ("sylon");

    // where each section goes
    const bool multi = value ("multiout") > 0.5f;
    AudioBuffer<float> outs[numOutBuses];
    for (int b = 0; b < numOutBuses; ++b) outs[b] = getBusBuffer (buffer, false, b);
    auto dest = [&] (int bus) -> AudioBuffer<float>& { return (multi && bus < numOutBuses && outs[bus].getNumChannels() >= 2) ? outs[bus] : outs[0]; };

    VoiceSettings settings[numSections];
    float lvl[numSections];
    for (int s = 0; s < numSections; ++s) { settings[s] = settingsFor (s); lvl[s] = sectionOn (s) ? dsp::dbToGain (secLvl[s]->load()) : 0.0f; }

    const float depth = value ("depth") * 0.01f;
    const float preSamples = depth * 0.015f * (float) sr;
    const float lpCoef = std::exp (-MathConstants<float>::twoPi * (18000.0f - 13000.0f * depth) / (float) sr);
    const float depthGain = 1.0f - 0.4f * depth;
    const float send = value ("hall_mix") * 0.01f * (0.5f + depth);
    {
        racoon::sdsp::Verb::Parameters rp;   // the family's 8-line FDN hall (was juce Freeverb), same knobs
        rp.roomSize = 0.3f + 0.69f * value ("hall_size") * 0.01f;
        rp.damping = value ("hall_damp") * 0.01f;
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        hall.setParameters (rp);
    }
    const float outGain = dsp::dbToGain (value ("outgain"));
    const int dynsrc = (int) value ("dynsrc");
    const double stepBeats = syncSeconds ((int) value ("sylrate"), 60.0);   // seconds at 60 bpm = beats
    const int sylLen = jmax (1, (int) value ("syllen"));
    const double spp = sr * 60.0 / jlimit (20.0, 400.0, bpm());

    auto renderChunk = [&] (int pos, int len)
    {
        // syllables on the grid: each step sings the next one without a new attack
        if (sylon == 2 && (heldCount > 0 || isPlaying()))
        {
            double p;
            if (isPlaying()) p = ppq() + (pos + len) / spp;
            else { if (! internalRunning) { internalRunning = true; internalPpq = 0.0; lastSylIndex = -1; } internalPpq += len / spp; p = internalPpq; }
            const int64 idx = (int64) std::floor (p / stepBeats);
            if (idx != lastSylIndex)
            {
                lastSylIndex = idx;
                sylStep = (int) (((idx % sylLen) + sylLen) % sylLen);
                Consonant c; float vowel, hum;
                currentSyllable (c, vowel, hum);
                for (auto& v : voices) if (v.active && v.gate && v.section < numChoir) v.glideTo (v.note, c, vowel, hum);
            }
        }
        else if (heldCount == 0) internalRunning = false;
        sylStepShown.store (sylStep);

        // vowels follow the knob (and its slow movement) when no syllables are running
        if (sylon == 0)
        {
            const float vmove = value ("vmove") * 0.01f;
            vowelPhase += value ("vrate") * len / sr;
            if (vowelPhase >= 1.0) vowelPhase -= 1.0;
            const float vw = jlimit (0.0f, 4.0f, value ("vowel") + vmove * 1.5f * (float) std::sin (MathConstants<double>::twoPi * vowelPhase));
            for (auto& v : voices) if (v.active && v.section < numChoir) v.setVowel (vw, value ("mouth") * 0.01f);
        }

        sec.clear();
        for (auto& v : voices)
            if (v.active)
                v.render (sec.getWritePointer (v.section * 2), sec.getWritePointer (v.section * 2 + 1), len, settings[v.section]);

        hallIn.clear();
        for (int s = 0; s < numSections; ++s)
        {
            auto& d = dest (1 + s);
            float* L = sec.getWritePointer (s * 2);
            float* R = sec.getWritePointer (s * 2 + 1);
            float pk = 0.0f;
            for (int i = 0; i < len; ++i)
            {
                // farther away: later, darker and softer
                float x[2] { L[i] * lvl[s], R[i] * lvl[s] };
                for (int c = 0; c < 2; ++c)
                {
                    preDelay[s][c].push (x[c]);
                    float y = preSamples > 1.0f ? preDelay[s][c].tap (preSamples) : x[c];
                    stageLp[s][c] = y + (stageLp[s][c] - y) * lpCoef;
                    x[c] = stageLp[s][c] * depthGain;
                }
                d.getWritePointer (0)[pos + i] += x[0];
                d.getWritePointer (1)[pos + i] += x[1];
                hallIn.getWritePointer (0)[i] += x[0] * send;
                hallIn.getWritePointer (1)[i] += x[1] * send;
                pk = jmax (pk, std::abs (x[0]), std::abs (x[1]));
            }
            sectionLevel[s].store (jmax (pk, sectionLevel[s].load() * 0.97f));
        }
        hall.processStereo (hallIn.getWritePointer (0), hallIn.getWritePointer (1), len);
        auto& hd = dest (7);
        for (int c = 0; c < 2; ++c) hd.addFrom (c, pos, hallIn, c, 0, len);
    };

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end) { const int len = jmin (32, end - pos); renderChunk (pos, len); pos += len; }
    };

    for (auto it = midi.begin(); it != midi.end();)
    {
        const auto ev = *it;
        ++it;
        const auto msg = ev.getMessage();
        const int sp = jlimit (0, n, ev.samplePosition);
        renderTo (sp);
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (ks && note >= keyswitchLow && note < keyswitchLow + (int) numArticulations) currentArt.store (note - keyswitchLow);
            else noteOn (note, msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            if (! (ks && note >= keyswitchLow && note < keyswitchLow + (int) numArticulations)) noteOff (note);
        }
        else if (msg.isSustainPedalOn()) sustainPedal = true;
        else if (msg.isSustainPedalOff())
        {
            sustainPedal = false;
            if (mode == unisonMode)
            {
                for (int k = 0; k < 128; ++k)
                    if (sustained[k]) { sustained[k] = false; for (auto& v : voices) if (v.active && v.key == k) v.release(); }
            }
            else if (heldCount == 0) releaseLines();
        }
        else if (msg.isController() && msg.getControllerNumber() == 1) cc1 = msg.getControllerValue() / 127.0f;
        else if (msg.isController() && msg.getControllerNumber() == 11) cc11 = msg.getControllerValue() / 127.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            heldCount = 0;
            for (auto& v : voices) v.release();
            satb = {};
        }
        const bool lastOfInstant = it == midi.end() || (*it).samplePosition != ev.samplePosition;
        if (lastOfInstant && chordDirty) { chordDirty = false; applyChord(); }
    }
    renderTo (n);
    midi.clear();

    // volume: CC11 unless it drives the dynamics; then a gentle ceiling
    const float targetVol = dynsrc == 2 ? 1.0f : cc11;
    for (int b = 0; b < numOutBuses; ++b)
    {
        auto& o = outs[b];
        for (int c = 0; c < o.getNumChannels(); ++c)
        {
            auto* x = o.getWritePointer (c);
            float vol = volume;
            for (int i = 0; i < n; ++i)
            {
                vol += (targetVol - vol) * 0.001f;
                const float y = x[i] * outGain * vol;
                x[i] = std::abs (y) < 1.0f ? y : std::copysign (1.0f + std::tanh (std::abs (y) - 1.0f) * 0.5f, y);
            }
        }
    }
    {
        float vol = volume;
        for (int i = 0; i < n; ++i) vol += (targetVol - vol) * 0.001f;
        volume = vol;
    }
    int act = 0;
    for (auto& v : voices) if (v.active) ++act;
    activeVoices.store (act);
    artShown.store (currentArt.load());
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Catedral (Ah)", "Coro", { { "mode", 0 }, { "vowel", 0 }, { "size", 10 }, { "hall_size", 95 }, { "hall_mix", 45 }, { "depth", 55 }, { "attack", 400 }, { "release", 1500 } } },
        { "Coro pop (Ooh)", "Coro", { { "mode", 0 }, { "vowel", 4 }, { "size", 6 }, { "mouth", 15 }, { "hall_mix", 22 }, { "vibdepth", 12 } } },
        { "Gospel cálido"_u, "Coro", { { "mode", 0 }, { "vowel", 0.4f }, { "spacing", 1 }, { "size", 8 }, { "vibdepth", 30 }, { "breath", 30 }, { "dyn", 85 }, { "hall_mix", 20 } } },
        { "Coro de cámara"_u, "Coro", { { "mode", 0 }, { "vowel", 3 }, { "size", 2 }, { "detune", 6 }, { "depth", 15 }, { "hall_size", 40 }, { "hall_mix", 18 } } },
        { "Armonía automática (Do mayor)"_u, "Armonizar", { { "mode", 1 }, { "key", 0 }, { "scale", 0 }, { "vowel", 0 }, { "art", 3 }, { "size", 6 } } },
        { "Armonía menor (La menor)"_u, "Armonizar", { { "mode", 1 }, { "key", 9 }, { "scale", 1 }, { "vowel", 3 }, { "art", 3 }, { "size", 6 } } },
        { "Sílabas rítmicas (La Da Lu Do)"_u, "Sílabas"_u, { { "mode", 0 }, { "sylon", 2 }, { "sylrate", 7 }, { "syllen", 4 }, { "attack", 20 }, { "release", 150 }, { "size", 5 } } },
        { "Staccato Ta Ka"_u, "Sílabas"_u, { { "mode", 0 }, { "sylon", 1 }, { "syllen", 2 }, { "syl1", 18 }, { "syl2", 19 }, { "art", 1 }, { "size", 8 } } },
        { "Mm susurrado", "Coro", { { "mode", 0 }, { "mouth", 100 }, { "breath", 55 }, { "size", 8 }, { "dyn", 40 }, { "hall_mix", 30 } } },
        { "Coro con cuerdas", "Ensamble", { { "mode", 0 }, { "vowel", 0 }, { "str_on", 1 }, { "str_oct", 0 }, { "hall_mix", 30 } } },
        { "Metales épicos"_u, "Ensamble", { { "mode", 0 }, { "sop_on", 0 }, { "alt_on", 0 }, { "ten_on", 0 }, { "bas_on", 0 }, { "br_on", 1 }, { "br_oct", -1 },
                                            { "br_bright", 70 }, { "str_on", 1 }, { "hall_mix", 35 }, { "art", 2 } } },
        { "Ensamble de cine", "Ensamble", { { "mode", 0 }, { "vowel", 3 }, { "size", 12 }, { "str_on", 1 }, { "br_on", 1 }, { "br_lvl", -8 }, { "art", 4 }, { "hall_size", 85 }, { "hall_mix", 40 } } },
        { "Voces lejanas", "Coro", { { "mode", 0 }, { "vowel", 4 }, { "depth", 90 }, { "hall_size", 90 }, { "hall_mix", 60 }, { "vmove", 40 } } },
        { "Unísono directo"_u, "Coro", { { "mode", 2 }, { "vowel", 0 }, { "sop_on", 1 }, { "alt_on", 0 }, { "ten_on", 0 }, { "bas_on", 0 }, { "size", 4 } } },
    };
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 3.9, 60, 96, 1 },
             { 0.0, 3.9, 64, 96, 1 },
             { 0.0, 3.9, 67, 96, 1 },
             { 4.0, 3.9, 57, 96, 1 },
             { 4.0, 3.9, 60, 96, 1 },
             { 4.0, 3.9, 64, 96, 1 },
             { 8.0, 3.9, 53, 96, 1 },
             { 8.0, 3.9, 57, 96, 1 },
             { 8.0, 3.9, 60, 96, 1 },
             { 12.0, 3.9, 55, 96, 1 },
             { 12.0, 3.9, 59, 96, 1 },
             { 12.0, 3.9, 62, 96, 1 } };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

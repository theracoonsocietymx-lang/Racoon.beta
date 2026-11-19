#include "LibraryInstrument.h"
#include "../Shared/Biblioteca.h"
#include "../Shared/RacoonDsp.h"

namespace racoon
{
using namespace juce;

namespace
{
int noteNumber (const String& s)
{
    if (s.containsOnly ("-0123456789")) return s.getIntValue();
    static const char* names[] { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    const auto t = s.toLowerCase().replace ("s", "#");
    int pc = -1, len = 0;
    for (int i = 0; i < 12; ++i)
        if (t.startsWith (names[i]) && (int) std::strlen (names[i]) > len) { pc = i; len = (int) std::strlen (names[i]); }
    if (pc < 0) return -1;
    return (t.substring (len).getIntValue() + 1) * 12 + pc;
}
}

std::shared_ptr<LibraryInstrument::Instrument> loadSfzInstrument (const File& sfz)
{
    if (! sfz.existsAsFile()) return {};
    auto inst = std::make_shared<LibraryInstrument::Instrument>();
    String defaultPath;
    std::map<String, String> global;
    std::map<String, std::shared_ptr<AudioBuffer<float>>> files;
    std::map<String, double> rates;
    String section;
    auto addRegion = [&] (const std::map<String, String>& op)
    {
        auto get = [&op, &global] (const char* k, const String& def) -> String
        {
            if (auto it = op.find (k); it != op.end()) return it->second;
            if (auto it = global.find (k); it != global.end()) return it->second;
            return def;
        };
        const auto sample = get ("sample", {});
        if (sample.isEmpty()) return;
        const auto rel = (defaultPath + sample).replaceCharacter ('\\', '/');
        if (files.count (rel) == 0)
        {
            auto buf = std::make_shared<AudioBuffer<float>>();
            double rate = 48000.0;
            if (! biblio::Catalogo::leer (sfz.getParentDirectory().getChildFile (rel), *buf, rate, 30.0)) { files[rel] = nullptr; return; }
            files[rel] = buf;
            rates[rel] = rate;
        }
        if (files[rel] == nullptr) return;
        LibraryInstrument::Region r;
        r.audio = files[rel];
        r.rate = rates[rel];
        const int key = noteNumber (get ("key", "-1"));
        r.loKey = key >= 0 ? key : noteNumber (get ("lokey", "0"));
        r.hiKey = key >= 0 ? key : noteNumber (get ("hikey", "127"));
        r.center = key >= 0 ? key : noteNumber (get ("pitch_keycenter", String (r.loKey)));
        r.loVel = get ("lovel", "1").getIntValue();
        r.hiVel = get ("hivel", "127").getIntValue();
        r.tuneCents = get ("tune", "0").getFloatValue();
        r.gainDb = get ("volume", "0").getFloatValue();
        const auto mode = get ("loop_mode", "no_loop");
        if (mode == "loop_continuous" || mode == "loop_sustain")
        {
            r.loopStart = get ("loop_start", "-1").getIntValue();
            r.loopEnd = get ("loop_end", "-1").getIntValue();
            if (r.loopEnd <= r.loopStart + 64 || r.loopEnd >= r.audio->getNumSamples()) r.loopStart = r.loopEnd = -1;
            else inst->sustained = true;
        }
        inst->regions.push_back (r);
    };
    std::map<String, String> region;
    bool inRegion = false;
    for (auto line : StringArray::fromLines (sfz.loadFileAsString()))
    {
        line = line.upToFirstOccurrenceOf ("//", false, false).trim();
        if (line.isEmpty()) continue;
        // a line can hold a header and its opcodes: "<region> sample=A0-v1.flac pitch_keycenter=21 ..."
        while (line.startsWithChar ('<'))
        {
            const auto header = line.upToFirstOccurrenceOf (">", true, false);
            if (inRegion) { addRegion (region); region.clear(); }
            section = header;
            inRegion = header == "<region>";
            line = line.fromFirstOccurrenceOf (">", false, false).trim();
        }
        for (auto& tok : StringArray::fromTokens (line, " \t", ""))
        {
            if (! tok.containsChar ('=')) continue;
            const auto k = tok.upToFirstOccurrenceOf ("=", false, false), v = tok.fromFirstOccurrenceOf ("=", false, false);
            if (section == "<control>" && k == "default_path") defaultPath = v;
            else if (section == "<global>" || section == "<group>") global[k] = v;
            else if (inRegion) region[k] = v;
        }
    }
    if (inRegion) addRegion (region);
    if (inst->regions.empty()) return {};
    if (auto it = global.find ("ampeg_release"); it != global.end()) inst->release = jlimit (0.02f, 10.0f, it->second.getFloatValue());
    if (auto it = global.find ("amp_veltrack"); it != global.end()) inst->velTrack = jlimit (0.0f, 1.0f, it->second.getFloatValue() / 100.0f);
    return inst;
}

LibraryInstrument::LibraryInstrument() = default;
LibraryInstrument::~LibraryInstrument() { live.store (nullptr); }

void LibraryInstrument::prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); racoon::sdsp::SincTable::get(); }
void LibraryInstrument::reset() { for (auto& v : voices) v = Voice(); }
void LibraryInstrument::allNotesOff() { panic.store (true); }

int LibraryInstrument::numRegions() const
{
    const auto* i = live.load();
    return i != nullptr ? (int) i->regions.size() : 0;
}

void LibraryInstrument::collect (uint32 now)
{
    // an old instrument goes two seconds after the change and never while the audio thread still reads it
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [this, now] (auto& r) { return now - r.second > 2000 && playing.load() != r.first.get(); }), retired.end());
}

bool LibraryInstrument::load (const String& instrumentId)
{
    const auto now = Time::getMillisecondCounter();
    collect (now);
    if (instrumentId == currentId && (instrumentId.isEmpty() || loaded())) return true;
    std::shared_ptr<Instrument> inst;
    if (instrumentId.isNotEmpty())
    {
        const auto& cat = biblio::Catalogo::get();
        const auto* in = cat.instrumento (instrumentId);
        if (in == nullptr) return false;
        inst = loadSfzInstrument (cat.archivo (in->archivo));
        if (inst == nullptr) return false;
    }
    if (owner) retired.push_back ({ owner, now });
    owner = inst;
    live.store (owner.get());
    currentId = inst != nullptr ? instrumentId : String();
    return inst != nullptr || instrumentId.isEmpty();
}

void LibraryInstrument::start (const Instrument& inst, int note, float velocity)
{
    note = jlimit (0, 127, note + transpose);
    const int vel = jlimit (1, 127, roundToInt (velocity * 127.0f));
    const Region* best = nullptr;
    int bestDist = 1000;
    for (auto& r : inst.regions)
    {
        const bool velOk = vel >= r.loVel && vel <= r.hiVel;
        if (! velOk) continue;
        // inside its key range; if no region covers the note, the nearest one (a little out of range is better than silence)
        const int dist = note < r.loKey ? r.loKey - note : note > r.hiKey ? note - r.hiKey : 0;
        if (dist < bestDist) { best = &r; bestDist = dist; }
        if (dist == 0) break;
    }
    if (best == nullptr || bestDist > 12) return;
    // the same note again: the old voice fades fast (a piano's damper), never two copies stacked
    for (auto& v : voices)
        if (v.region != nullptr && v.note == note && ! v.stealing) v.stealing = true;
    Voice* free = nullptr;
    for (auto& v : voices) if (v.region == nullptr) { free = &v; break; }
    if (free == nullptr)
    {
        free = &voices[0];
        for (auto& v : voices) if (v.released && v.rel < free->rel) free = &v;
    }
    free->region = best;
    free->pos = 0.0;
    free->inc = best->rate / sr * std::pow (2.0, (note - best->center + best->tuneCents / 100.0) / 12.0);
    // velocity: the layer chosen already brings its dynamics; the curve shapes the rest (amp_veltrack)
    const float vt = inst.velTrack;
    free->gain = Decibels::decibelsToGain (best->gainDb) * ((1.0f - vt) + vt * velocity * velocity) * 1.6f;
    free->env = 0.0f;
    free->age = 0.0f;
    free->rel = 1.0f;
    free->steal = 1.0f;
    free->note = note;
    free->released = false;
    free->stealing = false;
}

void LibraryInstrument::render (AudioBuffer<float>& buffer, const MidiBuffer& midi, int startSample, int numSamples, float gain)
{
    const auto* inst = live.load();
    playing.store (inst);
    if (panic.exchange (false)) for (auto& v : voices) if (v.region != nullptr) v.released = true;
    if (inst == nullptr) { for (auto& v : voices) v.region = nullptr; return; }
    // a voice that still points into an instrument that is no longer live stops (the new one starts clean)
    for (auto& v : voices)
        if (v.region != nullptr && (v.region < inst->regions.data() || v.region >= inst->regions.data() + inst->regions.size())) v.region = nullptr;
    const int nch = buffer.getNumChannels();
    auto* L = buffer.getWritePointer (0);
    auto* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;
    auto it = midi.findNextSamplePosition (startSample);
    const auto& sinc = racoon::sdsp::SincTable::get();
    const float dt = 1.0f / (float) sr;
    const float release = envR >= 0.0f ? jmax (0.01f, envR) : inst->release;
    const float relK = std::exp (-6.9f / (release * (float) sr));
    const float stealK = std::exp (-6.9f / (0.03f * (float) sr));
    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) start (*inst, m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff())
            {
                const int n = jlimit (0, 127, m.getNoteNumber() + transpose);
                for (auto& v : voices) if (v.region != nullptr && v.note == n) v.released = true;
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& v : voices) if (v.region != nullptr) v.released = true;
        }
        float l = 0.0f, r = 0.0f;
        for (auto& v : voices)
        {
            if (v.region == nullptr) continue;
            const auto& a = *v.region->audio;
            const int len = a.getNumSamples();
            const bool loops = v.region->loopStart >= 0;
            if (loops && v.pos >= v.region->loopEnd) v.pos -= (v.region->loopEnd - v.region->loopStart);
            if ((int) v.pos + 2 >= len) { v.region = nullptr; continue; }
            // attack, fall to the sustain level, release after the note-off
            const float target = v.age < envA ? v.age / jmax (1.0e-4f, envA)
                                               : envS + (1.0f - envS) * std::exp (-(v.age - envA) * 6.9f / jmax (0.005f, envD));
            v.env = target;
            v.age += dt;
            if (v.released) { v.rel *= relK; if (v.rel < 1.0e-4f) { v.region = nullptr; continue; } }
            if (v.stealing) { v.steal *= stealK; if (v.steal < 1.0e-4f) { v.region = nullptr; continue; } }
            const float g = v.gain * v.env * v.rel * v.steal;
            float sl, sr2;
            sinc.read2 (a.getReadPointer (0), a.getNumChannels() > 1 ? a.getReadPointer (1) : nullptr, len, v.pos,
                        racoon::sdsp::SincTable::bandFor (v.inc), sl, sr2);
            l += sl * g;
            r += sr2 * g;
            v.pos += v.inc;
        }
        L[i] += l * gain;
        if (R != nullptr) R[i] += r * gain;
    }
}
} // namespace racoon

// MIDI: Arpegiador. Takes the notes you hold (or a clip's chords) and plays them one by one, at the song's tempo,
// before the instrument of the channel. It goes in front of any instrument (Racoon's or a plugin); "Pasar a clip"
// turns what it plays into notes.
#include "AllEffects.h"
#include "engine/I18n.h"
#include "FxBase.h"

namespace racoon::fx
{
using namespace juce;

namespace
{
class Arpeggiator : public FxBase
{
public:
    Arpeggiator() : FxBase ("Arpegiador", "MIDI", layout(), 0.0, false, false, false, true) {}

    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Arriba", "Abajo", "Arriba y abajo", "Abajo y arriba", "Como tocas", "Al azar", "Acorde" }, 0);
        P::choice (l, "rate", "Velocidad", RacoonProcessor::divisionNames(), 10);       // 1/16
        P::integer (l, "octaves", "Octavas", 1, 4, 1);
        P::pct (l, "gate", "Gate", 5.0f, 200.0f, 75.0f);
        P::pct (l, "swing", "Swing", 0.0f, 75.0f, 0.0f);
        P::toggle (l, "latch", "Latch", false);
        P::integer (l, "repeat", "Repetir", 1, 4, 1);
        P::integer (l, "velocity", "Velocidad fija", 0, 127, 0);
        P::integer (l, "transpose", "Transponer", -24, 24, 0);
        return l;
    }

    void prepare (double rate, int) override { sr = rate > 0.0 ? rate : 48000.0; reset(); }

    void reset() override
    {
        numHeld = 0; physical = 0; numPending = 0; stepCount = 0; clock = 0; freeBeat = 0.0; running = false;
        for (auto& s : sounding) s = false;
    }

    void process (AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        MidiBuffer out;
        const double tempo = jlimit (20.0, 400.0, bpm() > 0.0 ? bpm() : 120.0);
        const double samplesPerBeat = sr * 60.0 / tempo;
        const int rateIdx = (int) value ("rate");
        const double stepBeats = RacoonProcessor::syncSeconds (rateIdx, tempo) * tempo / 60.0;
        const double swingBeats = value ("swing") * 0.01 * stepBeats * 0.5;
        const bool synced = isPlaying();
        const double startBeat = synced ? ppq() : freeBeat;

        // the notes coming in: they change what is held; anything else (CC, pitch bend) goes through
        std::vector<std::pair<int, MidiMessage>> events;
        for (const auto ev : midi) events.push_back ({ ev.samplePosition, ev.getMessage() });
        size_t e = 0;
        int pos = 0;
        while (pos < n)
        {
            const int next = e < events.size() ? jlimit (pos, n, events[e].first) : n;
            if (next > pos)
            {
                if (numHeld > 0) runSteps (out, pos, next, startBeat, samplesPerBeat, stepBeats, swingBeats);
                flushOffs (out, pos, next);
            }
            pos = next;
            for (; e < events.size() && events[e].first <= pos; ++e)
            {
                const auto& m = events[e].second;
                if (m.isNoteOn()) noteOn (out, m.getNoteNumber(), m.getVelocity(), pos, startBeat, samplesPerBeat, stepBeats, synced);
                else if (m.isNoteOff()) noteOff (out, m.getNoteNumber(), pos);
                else if (m.isAllNotesOff() || m.isAllSoundOff()) { allOff (out, pos); out.addEvent (m, pos); }
                else out.addEvent (m, pos);
            }
            if (e >= events.size() && pos >= n) break;
        }
        clock += n;
        if (! synced && running) freeBeat += (double) n / samplesPerBeat;
        midi.swapWith (out);
    }

    void bypassStateChanged (bool bypassed, MidiBuffer& midi) override { if (bypassed) allOff (midi, 0); }

    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Arpegio", { { "mode", "Modo", "Arriba, abajo, arriba y abajo, abajo y arriba, como tocas, al azar o el acorde entero en cada paso."_u },
                           { "rate", "Velocidad", "Cuánto dura cada paso, a tempo: 1/8, 1/16, 1/16 T (tresillo)..."_u },
                           { "octaves", "Octavas", "Cuántas octavas recorre, de 1 a 4."_u },
                           { "gate", "Gate", "Cuánto suena cada nota respecto al paso: corto es staccato, 100 % liga."_u } } },
            { "Ritmo", { { "swing", "Swing", "Retrasa los pasos pares: de recto a shuffle."_u },
                         { "latch", "Latch", "El arpegio sigue al soltar las teclas, hasta que tocas otro acorde."_u },
                         { "repeat", "Repetir", "Cuántas veces suena cada nota antes de pasar a la siguiente."_u } } },
            { "Notas", { { "velocity", "Velocidad fija", "La fuerza de todas las notas. En 0 usa la que tocaste."_u },
                         { "transpose", "Transponer", "Sube o baja el arpegio en semitonos."_u } }, true },
        };
        return s;
    }

    String displayText() const override
    {
        const int held = shownHeld.load();
        if (held == 0) return "Toca un acorde: el arpegio sale al instrumento de este canal."_u;
        static const char* modes[] { "ARRIBA", "ABAJO", "ARRIBA Y ABAJO", "ABAJO Y ARRIBA", "COMO TOCAS", "AL AZAR", "ACORDE" };
        return String (modes[jlimit (0, 6, (int) value ("mode"))]) + "  ·  " + RacoonProcessor::divisionNames()[(int) value ("rate")]
             + "  ·  " + String (held) + (held == 1 ? " nota" : " notas");
    }

    // for the tests
    int heldCount() const { return numHeld; }

private:
    struct Held { int note, vel, order; };
    struct Off { int note; int64 at; };
    static constexpr int maxHeld = 32, maxPending = 128;
    Held held[maxHeld];
    int numHeld = 0, physical = 0, orderCounter = 0;
    Off pending[maxPending];
    int numPending = 0;
    bool sounding[128] {};
    double sr = 48000.0, freeBeat = 0.0;
    int64 clock = 0, stepCount = 0;
    bool running = false;
    Random rng { 0x41a9 };
    std::atomic<int> shownHeld { 0 };

    void noteOn (MidiBuffer& out, int note, int vel, int pos, double startBeat, double spb, double stepBeats, bool synced)
    {
        const bool latch = value ("latch") > 0.5f;
        // with Latch, the first key after letting go of everything starts a new chord
        if (latch && physical == 0) { numHeld = 0; }
        ++physical;
        for (int i = 0; i < numHeld; ++i) if (held[i].note == note) return;
        if (numHeld >= maxHeld) return;
        const bool wasEmpty = numHeld == 0;
        held[numHeld++] = { note, vel, orderCounter++ };
        shownHeld.store (numHeld);
        if (wasEmpty)
        {
            stepCount = 0;
            if (! synced)
            {
                // stopped: the arpeggio starts right on this note, on its own clock
                freeBeat = -(double) pos / spb;
                running = true;
            }
            juce::ignoreUnused (out, startBeat, stepBeats);
        }
    }

    void noteOff (MidiBuffer& out, int note, int pos)
    {
        physical = jmax (0, physical - 1);
        if (value ("latch") > 0.5f) return;
        for (int i = 0; i < numHeld; ++i)
            if (held[i].note == note) { held[i] = held[--numHeld]; break; }
        shownHeld.store (numHeld);
        if (numHeld == 0) { allOff (out, pos); running = false; }
    }

    void allOff (MidiBuffer& out, int pos)
    {
        for (int p = 0; p < 128; ++p) if (sounding[p]) { out.addEvent (MidiMessage::noteOff (1, p), pos); sounding[p] = false; }
        numPending = 0;
    }

    void flushOffs (MidiBuffer& out, int from, int to)
    {
        for (int i = 0; i < numPending;)
        {
            if (pending[i].at < clock + to)
            {
                const int at = jlimit (from, to - 1, (int) (pending[i].at - clock));
                if (sounding[pending[i].note]) { out.addEvent (MidiMessage::noteOff (1, pending[i].note), at); sounding[pending[i].note] = false; }
                pending[i] = pending[--numPending];
            }
            else ++i;
        }
    }

    // the sequence of one cycle: the held notes in the chosen order, over the octaves
    int buildSequence (int* seq, int mode, int octaves) const
    {
        Held sorted[maxHeld];
        std::copy (held, held + numHeld, sorted);
        if (mode == 4) std::sort (sorted, sorted + numHeld, [] (const Held& a, const Held& b) { return a.order < b.order; });
        else std::sort (sorted, sorted + numHeld, [] (const Held& a, const Held& b) { return a.note < b.note; });
        int up[maxHeld * 4];
        int count = 0;
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < numHeld; ++i) up[count++] = sorted[i].note + 12 * o;
        int len = 0;
        switch (mode)
        {
            case 1: for (int i = count; --i >= 0;) seq[len++] = up[i]; break;                                          // down
            case 2: for (int i = 0; i < count; ++i) seq[len++] = up[i]; for (int i = count - 2; i > 0; --i) seq[len++] = up[i]; break;   // up and down
            case 3: for (int i = count; --i >= 0;) seq[len++] = up[i]; for (int i = 1; i < count - 1; ++i) seq[len++] = up[i]; break;    // down and up
            default: for (int i = 0; i < count; ++i) seq[len++] = up[i]; break;                                        // up, as played, random
        }
        return len;
    }

    int velocityFor (int note) const
    {
        const int fixed = (int) value ("velocity");
        if (fixed > 0) return fixed;
        for (int i = 0; i < numHeld; ++i) if (held[i].note % 12 == note % 12) return jlimit (1, 127, held[i].vel);
        return 100;
    }

    void play (MidiBuffer& out, int note, int vel, int at, double stepSamples)
    {
        note = jlimit (0, 127, note + (int) value ("transpose"));
        // the same note again: its old note-off goes, the new one gets its own
        for (int i = 0; i < numPending;) { if (pending[i].note == note) pending[i] = pending[--numPending]; else ++i; }
        if (sounding[note]) out.addEvent (MidiMessage::noteOff (1, note), at);
        out.addEvent (MidiMessage::noteOn (1, note, (uint8) jlimit (1, 127, vel)), at);
        sounding[note] = true;
        if (numPending < maxPending)
            pending[numPending++] = { note, clock + at + jmax ((int64) 1, (int64) (stepSamples * value ("gate") * 0.01)) };
    }

    void runSteps (MidiBuffer& out, int from, int to, double startBeat, double spb, double stepBeats, double swingBeats)
    {
        const double b0 = startBeat + (double) from / spb, b1 = startBeat + (double) to / spb;
        const int64 kFirst = (int64) std::floor (b0 / stepBeats) - 1, kLast = (int64) std::floor (b1 / stepBeats) + 1;
        const int mode = (int) value ("mode"), octaves = jlimit (1, 4, (int) value ("octaves")), repeat = jlimit (1, 4, (int) value ("repeat"));
        for (int64 k = kFirst; k <= kLast; ++k)
        {
            const double at = (double) k * stepBeats + ((k & 1) != 0 ? swingBeats : 0.0);
            if (at < b0 || at >= b1 || k < 0) continue;
            const int sample = jlimit (from, to - 1, (int) std::llround ((at - startBeat) * spb));
            const double stepSamples = stepBeats * spb;
            int seq[maxHeld * 8];
            const int len = buildSequence (seq, mode, octaves);
            if (len == 0) return;
            const int64 idx = stepCount / repeat;
            if (mode == 6)
            {
                // the whole chord on every step, climbing the octaves
                const int o = (int) (idx % octaves);
                for (int i = 0; i < numHeld; ++i) play (out, held[i].note + 12 * o, velocityFor (held[i].note), sample, stepSamples);
            }
            else if (mode == 5)
            {
                const int note = seq[rng.nextInt (len)];
                play (out, note, velocityFor (note), sample, stepSamples);
            }
            else
            {
                const int note = seq[(int) (idx % len)];
                play (out, note, velocityFor (note), sample, stepSamples);
            }
            ++stepCount;
        }
    }
};
}

void registerMidiEffects (std::vector<FxDef>& v)
{
    v.push_back ({ "arpegiador", "Arpegiador", "MIDI",
                   "Toca tus acordes nota por nota a tempo: arriba, abajo, al azar, en varias octavas. Va antes del instrumento."_u,
                   Kind::effect, [] { return std::unique_ptr<RacoonProcessor> (new Arpeggiator()); },
                   { "mode", "rate", "octaves", "gate", "swing", "latch" } });
}
}

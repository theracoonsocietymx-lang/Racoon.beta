// The edit and analysis tools ported from SoundCraft (MIT/Apache-2.0) and the ones around them:
// quantize with strength and swing, velocities, overlaps.
#include "engine/Session.h"
#include "ui/NoteTools.h"
#include "ui/Aleatorio.h"
#include <set>
#include <map>
#include "engine/AudioTools.h"
#include "engine/CrashLog.h"

using namespace racoon;
using namespace juce;

namespace
{
int toolFailures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++toolFailures;
}
bool near (double a, double b, double tol = 1.0e-6) { return std::abs (a - b) <= tol; }

te::MidiClip* freshClip (Session& s, double beats = 8.0)
{
    s.newProject (false);
    auto* t = s.addAudioTrack ("Notas");
    return t != nullptr ? s.createMidiClip (*t, { 0_bp, te::BeatPosition::fromBeats (beats) }, "prueba") : nullptr;
}
te::MidiNote* add (te::MidiClip& c, double start, double len, int pitch = 60, int vel = 100)
{
    return c.getSequence().addNote (pitch, te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (len), vel, 0, nullptr);
}

void noteTools (Session& s)
{
    std::cout << "herramientas MIDI" << std::endl;
    auto* c = freshClip (s);
    if (c == nullptr) { check (false, "clip de prueba"); return; }
    using namespace ui::notes;

    // strength: halfway
    auto* a = add (*c, 0.1, 0.5);
    QuantizeOptions o; o.grid = 0.25; o.strength = 0.5;
    quantize (*c, { a }, o, nullptr);
    check (near (a->getStartBeat().inBeats(), 0.05) && near (a->getLengthBeats().inBeats(), 0.5), "cuantizar al 50 %: la nota va a medio camino y conserva su duración");
    // swing: the second 1/8 line is late by swing * grid / 3
    auto* b = add (*c, 0.52, 0.2);
    o = {}; o.grid = 0.5; o.swing = 0.6;
    quantize (*c, { b }, o, nullptr);
    check (near (b->getStartBeat().inBeats(), 0.5 + 0.6 * 0.5 / 3.0), "swing 60 %: la corchea de contratiempo llega tarde (" + String (b->getStartBeat().inBeats(), 3) + ")");
    check (near (swungLine (0.98, 0.5, 1.0), 1.0) && near (swungLine (0.7, 0.5, 1.0), 0.5 + 0.5 / 3.0), "swing al 100 % es sentir de tresillo");
    // the bars of the song: a clip that starts half a beat in still lands on the song's beats
    auto* d = add (*c, 0.4, 0.2);
    o = {}; o.grid = 1.0; o.offset = 0.5;
    quantize (*c, { d }, o, nullptr);
    check (near (d->getStartBeat().inBeats(), 0.5), "la rejilla se cuenta desde el compás de la canción, no desde el clip");
    // exclude: notes already close stay
    auto* e = add (*c, 2.02, 0.2);
    auto* f = add (*c, 3.2, 0.2);
    o = {}; o.grid = 0.5; o.excludeWithin = 0.2;
    quantize (*c, { e, f }, o, nullptr);
    check (near (e->getStartBeat().inBeats(), 2.02) && near (f->getStartBeat().inBeats(), 3.0), "las notas que ya están casi encima de la rejilla no se tocan");
    // ends
    auto* g = add (*c, 4.1, 0.6);
    o = {}; o.grid = 0.25; o.ends = true;
    quantize (*c, { g }, o, nullptr);
    check (near (g->getStartBeat().inBeats(), 4.0) && near (g->getEndBeat().inBeats(), 4.75), "cuantizar finales: empieza en 4 y termina en 4.75");
    // keep the end in place
    auto* h = add (*c, 5.1, 0.5);
    o = {}; o.grid = 0.5; o.keepLength = false;
    quantize (*c, { h }, o, nullptr);
    check (near (h->getStartBeat().inBeats(), 5.0) && near (h->getEndBeat().inBeats(), 5.6), "sin conservar la duración, el final se queda donde estaba");
    // random never pushes more than half a step times the amount
    {
        Array<te::MidiNote*> list;
        for (int i = 0; i < 40; ++i) list.add (add (*c, i * 0.125, 0.1));
        o = {}; o.grid = 0.125; o.random = 0.4; o.seed = 7;
        quantize (*c, list, o, nullptr);
        double worst = 0.0; bool moved = false;
        for (int i = 0; i < list.size(); ++i)
        {
            const double dv = std::abs (list[i]->getStartBeat().inBeats() - i * 0.125);
            worst = jmax (worst, dv);
            moved = moved || dv > 1.0e-4;
            if (i > 0) list[i]->setNoteNumber (60, nullptr);
        }
        check (moved && worst <= 0.4 * 0.0625 + 1.0e-9, "el azar mueve las notas, como mucho " + String (worst, 4) + " pulsos");
    }

    // velocities
    c = freshClip (s);
    Array<te::MidiNote*> v;
    for (int i = 0; i < 5; ++i) v.add (add (*c, i, 0.5, 60, 90));
    velocityRamp (*c, v, 40, 120, nullptr);
    check (v[0]->getVelocity() == 40 && v[2]->getVelocity() == 80 && v[4]->getVelocity() == 120, "rampa de velocidad 40 a 120 a lo largo del tiempo");
    velocityScale (*c, v, 1.5, nullptr);
    check (v[4]->getVelocity() == 127 && v[0]->getVelocity() == 60, "escalar 150 %: no pasa de 127");
    velocityLimit (*c, v, 70, 100, nullptr);
    check (v[0]->getVelocity() == 70 && v[4]->getVelocity() == 100, "limitar entre 70 y 100");
    velocitySet (*c, v, 90, nullptr);
    velocityHumanize (*c, v, 10, 3, nullptr);
    bool inRange = true, differ = false;
    for (auto* n : v) { inRange = inRange && std::abs (n->getVelocity() - 90) <= 10; differ = differ || n->getVelocity() != 90; }
    check (inRange && differ, "humanizar +-10 cambia las velocidades sin salirse");
    velocityRamp (*c, v, 20, 120, nullptr);
    velocityCompress (*c, v, 0.5, nullptr);
    check (v[0]->getVelocity() == 45 && v[4]->getVelocity() == 95, "emparejar al 50 %: 20 y 120 quedan en 45 y 95");

    // overlaps
    c = freshClip (s);
    auto* o1 = add (*c, 0.0, 2.0, 60);
    auto* o2 = add (*c, 1.0, 1.0, 60);
    auto* o3 = add (*c, 1.5, 1.0, 64);     // another pitch: not an overlap
    const int cut = removeOverlaps (*c, {}, nullptr);
    check (cut == 1 && near (o1->getLengthBeats().inBeats(), 1.0) && near (o2->getLengthBeats().inBeats(), 1.0) && near (o3->getLengthBeats().inBeats(), 1.0),
           "quitar solapes recorta la nota que pisa a la siguiente de la misma altura");
}

AudioBuffer<float> tone (double sr, double secs, double hz, float amp, int ch = 2)
{
    AudioBuffer<float> b (ch, (int) (sr * secs));
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, amp * (float) std::sin (2.0 * MathConstants<double>::pi * hz * i / sr));
    return b;
}
AudioBuffer<float> join (std::initializer_list<const AudioBuffer<float>*> parts)
{
    int n = 0, ch = 1;
    for (auto* p : parts) { n += p->getNumSamples(); ch = jmax (ch, p->getNumChannels()); }
    AudioBuffer<float> out (ch, n);
    out.clear();
    int at = 0;
    for (auto* p : parts)
    {
        for (int c = 0; c < p->getNumChannels(); ++c) out.copyFrom (c, at, *p, c, 0, p->getNumSamples());
        at += p->getNumSamples();
    }
    return out;
}
// a drum-like hit: a noise burst with a quick decay and a low thump
void addHit (AudioBuffer<float>& b, double sr, double at, float amp, Random& rng)
{
    const int s0 = (int) (at * sr), len = (int) (sr * 0.12);
    for (int i = 0; i < len && s0 + i < b.getNumSamples(); ++i)
    {
        const float env = amp * std::exp (-(float) i / (float) (sr * 0.025));
        const float v = env * (0.6f * (rng.nextFloat() * 2.0f - 1.0f) + 0.6f * (float) std::sin (2.0 * MathConstants<double>::pi * 70.0 * i / sr));
        for (int c = 0; c < b.getNumChannels(); ++c) b.addSample (c, s0 + i, v);
    }
}

void audioTools()
{
    std::cout << "herramientas de audio" << std::endl;
    using namespace racoon::audio;
    const double sr = 48000.0;

    // processes
    {
        auto b = tone (sr, 0.5, 220.0, 0.25f);
        for (int i = 0; i < b.getNumSamples(); ++i) b.addSample (0, i, 0.1f);     // DC on the left
        removeDC (b);
        double mean = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i) mean += b.getSample (0, i);
        check (std::abs (mean / b.getNumSamples()) < 1.0e-4, "quitar DC deja la onda centrada en cero");
        const float g = normalizeGain (b, -0.3f);
        b.applyGain (g);
        check (std::abs (20.0f * std::log10 (peakOf (b)) + 0.3f) < 0.01f, "normalizar lleva el pico a -0.3 dB");
        const float before = b.getSample (1, 100);
        invertPhase (b);
        check (b.getSample (1, 100) == -before, "invertir la fase cambia el signo de cada muestra");
    }
    // strip silence
    {
        auto a = tone (sr, 1.0, 330.0, 0.5f), q = tone (sr, 0.5, 330.0, 0.0f), z = tone (sr, 0.1, 330.0, 0.0f);
        auto b = join ({ &a, &q, &a, &z, &a });
        const auto r = nonSilentRanges (b, -40.0f, (int64) (sr * 0.25), 0, 0);
        check (r.size() == 2 && std::abs ((double) r[0].getEnd() / sr - 1.0) < 0.01 && std::abs ((double) r[1].getStart() / sr - 1.5) < 0.01,
               "quitar silencios: 2 regiones; el hueco de 0.1 s no corta (" + String ((int) r.size()) + ")");
        const auto padded = nonSilentRanges (b, -40.0f, (int64) (sr * 0.25), (int64) (sr * 0.05), (int64) (sr * 0.1));
        check (padded.size() == 2 && std::abs ((double) padded[0].getEnd() / sr - 1.1) < 0.01 && std::abs ((double) padded[1].getStart() / sr - 1.45) < 0.01,
               "con margen antes y después las regiones crecen 50 y 100 ms");
    }
    // transients
    {
        AudioBuffer<float> b (2, (int) (sr * 2.2));
        b.clear();
        Random rng (5);
        std::vector<double> at { 0.1, 0.35, 0.6, 0.85, 1.1, 1.225, 1.35, 1.6, 1.85 };
        for (size_t i = 0; i < at.size(); ++i) addHit (b, sr, at[i], i % 2 == 0 ? 0.8f : 0.35f, rng);
        const auto found = detectTransients (b, sr, 0.5f);
        int matched = 0;
        double worst = 0.0;
        for (auto t : at)
            for (auto f : found)
                if (std::abs ((double) f / sr - t) < 0.01) { ++matched; worst = jmax (worst, std::abs ((double) f / sr - t)); break; }
        check (matched == (int) at.size() && found.size() == at.size(), "golpes: " + String (matched) + " de " + String ((int) at.size())
               + " encontrados (" + String ((int) found.size()) + " en total), el peor a " + String (worst * 1000.0, 1) + " ms");
        auto pad = tone (sr, 2.0, 220.0, 0.4f);
        const auto onPad = detectTransients (pad, sr, 0.5f);
        check (onPad.size() <= 1, "un tono sostenido no da golpes falsos (" + String ((int) onPad.size()) + ")");
    }
    // pitch and audio to notes
    {
        auto a = tone (sr, 0.1, 440.0, 0.5f, 1);
        auto hz = yin (a.getReadPointer (0), 2048, sr);
        check (hz.has_value() && std::abs (*hz - 440.0f) < 2.0f, "YIN encuentra La 440 (" + String (hz ? *hz : 0.0f, 1) + " Hz)");
        auto c4 = tone (sr, 0.4, 261.63, 0.5f, 1), g4 = tone (sr, 0.4, 392.0, 0.5f, 1), gap = tone (sr, 0.1, 1.0, 0.0f, 1), e2 = tone (sr, 0.5, 82.41, 0.4f, 1);
        auto mel = join ({ &c4, &gap, &g4, &gap, &g4, &gap, &e2 });
        const auto notes = audioToNotes (mel.getReadPointer (0), mel.getNumSamples(), sr);
        StringArray p;
        for (auto& n : notes) p.add (String (n.pitch));
        check (p.joinIntoString (" ") == "60 67 67 40", "audio a MIDI: Do4, Sol4 dos veces y un Mi grave salen como 60 67 67 40 (" + p.joinIntoString (" ") + ")");
        if (notes.size() >= 2)
            check (std::abs ((double) notes[1].start / sr - 0.5) < 0.04 && std::abs ((double) notes[1].end / sr - 0.9) < 0.05, "y cada nota empieza y acaba donde suena");
        AudioBuffer<float> silence (1, (int) sr);
        silence.clear();
        check (audioToNotes (silence.getReadPointer (0), silence.getNumSamples(), sr).empty(), "el silencio no da notas");
    }
    // key
    {
        Chroma cMajor {};
        for (int pc : { 0, 4, 7, 0, 5, 9, 7, 11, 2, 0, 4, 7 }) cMajor[(size_t) pc] += 1.0;
        auto k = keyFromChroma (cMajor);
        check (k.root == 0 && ! k.minor, "tonalidad de Do-Fa-Sol-Do: " + k.name());
        Chroma aMinor {};
        for (int pc : { 9, 0, 4, 9, 2, 5, 9, 4, 8, 11, 9, 0, 4 }) aMinor[(size_t) pc] += 1.0;
        k = keyFromChroma (aMinor);
        check (k.root == 9 && k.minor, "tonalidad de La-Re-Mi-La menor: " + k.name());
        // chords made of sines
        AudioBuffer<float> chords (1, (int) (sr * 4.0));
        chords.clear();
        const double triads[4][3] { { 261.63, 329.63, 392.0 }, { 349.23, 440.0, 523.25 }, { 392.0, 493.88, 587.33 }, { 261.63, 329.63, 392.0 } };
        for (int c = 0; c < 4; ++c)
            for (auto f : triads[c])
                for (int i = 0; i < (int) sr; ++i) chords.addSample (0, c * (int) sr + i, 0.2f * (float) std::sin (2.0 * MathConstants<double>::pi * f * i / sr));
        k = keyFromChroma (chromaOfAudio (chords, sr));
        check (k.root == 0 && ! k.minor, "tonalidad de un audio con acordes de Do, Fa, Sol: " + k.name());
        check (keyFromChroma (Chroma {}).root < 0, "sin notas no inventa una tonalidad");
    }
    // loudness
    {
        auto s1 = tone (sr, 10.0, 997.0, std::pow (10.0f, -20.0f / 20.0f));
        auto L = measureLoudness (s1, sr);
        check (std::abs (L.integratedLufs + 20.0f) < 0.3f, "un seno de 1 kHz a -20 dBFS en estéreo mide -20 LUFS ("_u + String (L.integratedLufs, 2) + ")");
        AudioBuffer<float> tp (2, (int) sr);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < tp.getNumSamples(); ++i) tp.setSample (c, i, (float) std::sin (MathConstants<double>::pi * i / 2.0 + MathConstants<double>::pi / 4.0));
        L = measureLoudness (tp, sr);
        check (std::abs (L.peakDb + 3.01f) < 0.05f && std::abs (L.truePeakDb) < 0.35f,
               "pico real: las muestras marcan -3 dB y entre ellas llega a 0 dBTP (" + String (L.peakDb, 2) + " / " + String (L.truePeakDb, 2) + ")");
        AudioBuffer<float> quiet (2, (int) (sr * 5.0));
        quiet.clear();
        check (measureLoudness (quiet, sr).integratedLufs <= -69.9f, "el silencio queda bajo la compuerta (-70 LUFS)");
    }
}

void clipTools (Session& s, const File& out)
{
    std::cout << "herramientas de audio en clips" << std::endl;
    using namespace racoon::audio;
    const double sr = 48000.0;
    s.newProject (false);
    s.setBpm (120.0);
    auto dir = out.getChildFile ("clips");
    dir.createDirectory();
    auto* t = s.addAudioTrack ("Voz");
    if (t == nullptr) { check (false, "canal de audio"); return; }

    // tone - silence - tone: strip silence leaves two clips
    auto a = tone (sr, 1.0, 330.0, 0.25f), q = tone (sr, 1.0, 1.0, 0.0f);
    auto withGaps = join ({ &a, &q, &a });
    auto f1 = dir.getChildFile ("con huecos.wav");
    writeWav (f1, withGaps, sr);
    auto* c1 = s.addAudioFile (*t, f1, 0_tp);
    if (c1 == nullptr) { check (false, "clip de audio"); return; }
    int pieces = 0;
    auto r = stripSilence (s, *c1, {}, &pieces);
    int clips = 0;
    double firstEnd = 0.0, secondStart = 0.0;
    for (auto* c : t->getClips())
    {
        ++clips;
        if (clips == 1) firstEnd = c->getPosition().getEnd().inSeconds();
        if (clips == 2) secondStart = c->getPosition().getStart().inSeconds();
    }
    check (r.wasOk() && pieces == 2 && clips == 2 && std::abs (firstEnd - 1.08) < 0.03 && std::abs (secondStart - 1.99) < 0.03,
           "quitar silencios en un clip: quedan 2 clips (" + String (clips) + "), el primero acaba en " + String (firstEnd, 2) + " s y el segundo empieza en " + String (secondStart, 2) + " s " + r.getErrorMessage());
    s.getEdit().undo();
    int afterUndo = 0;
    for (auto* c : t->getClips()) { ignoreUnused (c); ++afterUndo; }
    check (afterUndo == 1, "deshacer devuelve el clip entero");

    // normalize: a new file with its peak at -0.3 dB, the clip points to it
    if (auto* c = dynamic_cast<te::WaveAudioClip*> (t->getClips().getFirst()))
    {
        const auto before = c->getOriginalFile();
        r = processClip (s, *c, racoon::audio::Process::normalize);
        AudioBuffer<float> b;
        double fsr = 0.0;
        bool read = readFile (s.engine, c->getOriginalFile(), b, fsr);
        check (r.wasOk() && c->getOriginalFile() != before && read && std::abs (20.0f * std::log10 (peakOf (b)) + 0.3f) < 0.05f && before.existsAsFile(),
               "normalizar un clip escribe un archivo nuevo a -0.3 dB y deja el original (" + c->getOriginalFile().getFileName() + ") " + r.getErrorMessage());
        s.getEdit().undo();
        check (c->getOriginalFile() == before, "deshacer vuelve al audio original");
        r = processClip (s, *c, racoon::audio::Process::invertPhase);
        read = readFile (s.engine, c->getOriginalFile(), b, fsr);
        check (r.wasOk() && read && std::abs (b.getSample (0, 1000) + withGaps.getSample (0, 1000)) < 1.0e-5f && std::abs (withGaps.getSample (0, 1000)) > 0.05f, "invertir la fase del clip");
    }

    // a sung line to MIDI
    {
        auto c4 = tone (sr, 0.5, 261.63, 0.4f, 1), e4 = tone (sr, 0.5, 329.63, 0.4f, 1), g4 = tone (sr, 1.0, 392.0, 0.4f, 1), gap = tone (sr, 0.05, 1.0, 0.0f, 1);
        auto mel = join ({ &c4, &gap, &e4, &gap, &g4 });
        auto f2 = dir.getChildFile ("melodia.wav");
        writeWav (f2, mel, sr);
        auto* v = s.addAudioTrack ("Melodia");
        auto* c2 = v != nullptr ? s.addAudioFile (*v, f2, te::TimePosition::fromSeconds (2.0)) : nullptr;
        String msg;
        auto* mc = c2 != nullptr ? clipToMidi (s, *c2, &msg) : nullptr;
        StringArray p;
        double firstBeat = -1.0;
        if (mc != nullptr)
            for (auto* n : mc->getSequence().getNotes())
            {
                p.add (String (n->getNoteNumber()));
                if (firstBeat < 0.0) firstBeat = mc->getStartBeat().inBeats() + n->getStartBeat().inBeats();
            }
        check (mc != nullptr && p.joinIntoString (" ") == "60 64 67" && std::abs (firstBeat - 4.0) < 0.1 && mc->getTrack() != v,
               "audio a MIDI en un clip: canal nuevo con Do Mi Sol desde el pulso 4 (" + p.joinIntoString (" ") + ", pulso " + String (firstBeat, 2) + ") " + msg);
        if (mc != nullptr)
        {
            auto k = keyOfClip (s, *mc);
            check (k.root == 0 && ! k.minor, "la tonalidad de esas notas es Do mayor: " + k.name());
        }
    }

    // a drum loop to slices
    {
        AudioBuffer<float> loop (2, (int) (sr * 2.0));
        loop.clear();
        Random rng (9);
        for (int i = 0; i < 8; ++i) addHit (loop, sr, i * 0.25, i % 2 == 0 ? 0.9f : 0.4f, rng);
        auto f3 = dir.getChildFile ("loop.wav");
        writeWav (f3, loop, sr);
        auto* d = s.addAudioTrack ("Loop");
        auto* c3 = d != nullptr ? s.addAudioFile (*d, f3, 0_tp) : nullptr;
        String msg;
        auto slices = c3 != nullptr ? sliceAtTransients (s, *c3, 0.5f, 16, &msg) : std::vector<Slice>();
        bool onBeats = slices.size() == 8;
        for (size_t i = 0; i < slices.size() && onBeats; ++i) onBeats = std::abs (slices[i].startBeat - i * 0.5) < 0.03 && slices[i].file.existsAsFile();
        check (onBeats, "trocear un loop de 8 golpes: 8 archivos, cada uno en su corchea (" + String ((int) slices.size()) + ") " + msg);
        // and to Hamster: a channel with Hamster, a clip that plays them, and Hamster takes the kit
        // (only in RACOON_ONLY_TOOLS: on Linux a VST3 taken out of an Edit leaves run-loop handlers that break the
        // message pumping of the tests that come after; macOS does not use the VST3 IRunLoop)
        if (SystemStats::getEnvironmentVariable ("RACOON_ONLY_TOOLS", {}).isNotEmpty()
            && Catalog::findRacoon (s.engine, "Racoon Hamster").has_value() && c3 != nullptr && ! slices.empty())
        {
            for (auto& f : kitInbox().findChildFiles (File::findFiles, false)) f.deleteFile();
            auto* h = slicesToHamster (s, *c3, slices, &msg);
            te::MidiClip* hc = nullptr;
            if (h != nullptr) for (auto* c : h->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c)) hc = m;
            bool notesOk = hc != nullptr && hc->getSequence().getNumNotes() == 8;
            if (notesOk)
                for (int i = 0; i < 8; ++i)
                {
                    auto* n = hc->getSequence().getNote (i);
                    notesOk = notesOk && n->getNoteNumber() == 36 + i && std::abs (hc->getStartBeat().inBeats() + n->getStartBeat().inBeats() - i * 0.5) < 0.03;
                }
            check (h != nullptr && notesOk && c3->isMuted(), "a Hamster: canal nuevo, un clip con las notas 36 a 43 en su lugar y el loop silenciado. " + msg);
            bool taken = false;
            for (int i = 0; i < 40 && ! taken; ++i)
            {
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                taken = kitInbox().findChildFiles (File::findFiles, false, "*.json").isEmpty();
            }
            check (taken, "Hamster toma el kit que Racoon le dejó");
            if (taken && h != nullptr)
            {
                // it plays: render the new channel and look for sound where the hits are
                AudioBuffer<float> b;
                double rsr = 0.0;
                c3->setMuted (true);
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                ExportOptions o;
                o.destination = out.getChildFile ("hamster golpes.wav");
                o.tailSeconds = 0.0;
                auto all = te::getAllTracks (s.getEdit());
                BigInteger bits;
                bits.setBit (all.indexOf (h));
                te::Renderer::Parameters p (s.getEdit());
                p.destFile = o.destination;
                p.audioFormat = s.engine.getAudioFileFormatManager().getWavFormat();
                p.time = { 0_tp, te::TimePosition::fromSeconds (2.0) };
                p.tracksToDo = bits;
                p.usePlugins = true;
                p.checkNodesForAudio = false;
                s.getTransport().stop (false, true);
                s.getTransport().freePlaybackContext();
                const auto rendered = te::Renderer::renderToFile ("prueba", p);
                const bool read = rendered.existsAsFile() && readFile (s.engine, rendered, b, rsr);
                check (read && peakOf (b) > 0.05f, "y suena: el canal de Hamster tiene pico " + String (read ? peakOf (b) : 0.0f, 2));
            }
        }
    }
}
}

namespace
{
void reportsAndHealth (Session& s, const File& out)
{
    std::cout << "informes, fallos y revisión del proyecto" << std::endl;
    using namespace racoon::audio;
    const double sr = 48000.0;

    // the export report: a -20 dBFS tone, exported, measures about -20 LUFS (minus the pan law) with no peak over 0
    {
        s.newProject (false);
        auto* t = s.addAudioTrack ("Tono");
        auto f = out.getChildFile ("clips").getChildFile ("tono -20.wav");
        f.getParentDirectory().createDirectory();
        writeWav (f, tone (sr, 6.0, 997.0, std::pow (10.0f, -1.0f)), sr);
        if (t != nullptr) s.addAudioFile (*t, f, 0_tp);
        ExportOptions o;
        o.destination = out.getChildFile ("informe.wav");
        o.tailSeconds = 0.0;
        const auto r = s.exportMix (o);
        AudioBuffer<float> b;
        double fsr = 0.0;
        const bool ok = r.wasOk() && readFile (s.engine, o.destination, b, fsr);
        const auto L = measureLoudness (b, fsr);
        check (ok && L.integratedLufs < -19.0f && L.integratedLufs > -25.0f && L.truePeakDb < -10.0f,
               "el informe de la mezcla exportada: " + L.summary());
    }

    // crash reports: the last things that happened go in, only the newest 5 stay, each is told once
    {
        for (auto& f : crashlog::reports()) f.deleteFile();
        crashlog::folder().getChildFile ("ultimo-avisado.txt").deleteFile();
        crashlog::note ("Abrir Mi tema.racoon");
        crashlog::note ("Comando: Exportar");
        auto f = crashlog::writeReport ("prueba", "pila de prueba");
        const auto text = f.loadFileAsString();
        check (text.contains ("Abrir Mi tema.racoon") && text.contains ("Comando: Exportar") && text.contains ("pila de prueba"),
               "el informe de fallo guarda lo último que pasó y la pila");
        for (int i = 0; i < 7; ++i) crashlog::writeReport ("prueba " + String (i), {});
        check (crashlog::reports().size() == 5, "solo se guardan los 5 informes más nuevos (" + String (crashlog::reports().size()) + ")");
        const auto first = crashlog::takeNewReport();
        check (first.existsAsFile() && crashlog::takeNewReport() == File(), "el aviso de un informe nuevo sale una sola vez");
    }

    // health: missing audio, then found again in another folder
    {
        s.newProject (false);
        auto* t = s.addAudioTrack ("Voz");
        auto dir = out.getChildFile ("se movio");
        dir.createDirectory();
        auto moved = out.getChildFile ("otra carpeta").getChildFile ("sub");
        moved.createDirectory();
        auto f = dir.getChildFile ("toma 3.wav");
        writeWav (f, tone (sr, 1.0, 220.0, 0.3f), sr);
        if (t != nullptr) s.addAudioFile (*t, f, 0_tp);
        check (s.healthReport().isEmpty(), "un proyecto sano no da avisos");
        f.moveFileTo (moved.getChildFile ("toma 3.wav"));
        const auto issues = s.healthReport();
        check (issues.size() == 1 && issues[0].contains ("toma 3.wav") && s.countMissingAudio() == 1, "falta un audio: lo dice con su nombre: " + issues.joinIntoString (" | "));
        const int fixed = s.relinkMissingAudio (out.getChildFile ("otra carpeta"));
        check (fixed == 1 && s.countMissingAudio() == 0 && s.healthReport().isEmpty(), "buscar los archivos lo encuentra en una subcarpeta y lo vuelve a enlazar");
        // a clip asking for more than its file has
        if (t != nullptr)
            if (auto* c = dynamic_cast<te::WaveAudioClip*> (t->getClips().getFirst()))
            {
                c->setLength (te::TimeDuration::fromSeconds (3.0), true);
                c->setLoopRange ({});
                const auto again = s.healthReport();
                check (c->isLooping() || (again.size() == 1 && again[0].contains ("dura más")), "un clip más largo que su audio también se avisa"_u + (again.isEmpty() ? String() : ": " + again[0]));
            }
    }
}
}

namespace
{
// level of one frequency (Goertzel), in dB
float toneDb (const AudioBuffer<float>& b, double hz, double sr, double at, double len)
{
    const int start = (int) (at * sr), n = (int) (len * sr);
    const double w = MathConstants<double>::twoPi * hz / sr, cw = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = start; i < start + n && i < b.getNumSamples(); ++i) { const double s0 = b.getSample (0, i) + cw * s1 - s2; s2 = s1; s1 = s0; }
    return (float) (10.0 * std::log10 ((s1 * s1 + s2 * s2 - cw * s1 * s2) / ((double) n * n / 4.0) + 1e-20));
}

void sidechainInTheSong (Session& s, const File& out)
{
    std::cout << "sidechain en la canción"_u << std::endl;
    using namespace racoon::audio;
    const double sr = 48000.0;
    s.newProject (false);
    s.setBpm (120.0);
    auto dir = out.getChildFile ("clips");
    dir.createDirectory();
    // a kick every half second on "Bombo", a steady 2 kHz tone on "Bajo"
    AudioBuffer<float> kick (2, (int) (sr * 3.0)), bass (2, (int) (sr * 3.0));
    for (int i = 0; i < kick.getNumSamples(); ++i)
    {
        const double t = std::fmod ((double) i / sr, 0.5);
        const float k = t < 0.15 ? 0.9f * (float) std::sin (MathConstants<double>::twoPi * 60.0 * t) * (float) std::exp (-t * 25.0) : 0.0f;
        const float b = 0.3f * (float) std::sin (MathConstants<double>::twoPi * 2000.0 * i / sr);
        for (int c = 0; c < 2; ++c) { kick.setSample (c, i, k); bass.setSample (c, i, b); }
    }
    writeWav (dir.getChildFile ("bombo.wav"), kick, sr);
    writeWav (dir.getChildFile ("bajo 2k.wav"), bass, sr);
    auto* kt = s.addAudioTrack ("Bombo");
    auto* bt = s.addAudioTrack ("Bajo");
    if (kt == nullptr || bt == nullptr) { check (false, "dos canales"); return; }
    s.addAudioFile (*kt, dir.getChildFile ("bombo.wav"), 0_tp);
    s.addAudioFile (*bt, dir.getChildFile ("bajo 2k.wav"), 0_tp);
    CatalogItem item;
    for (auto& it : Catalog::nativeEffects()) if (it.name == "Sidechain") item = it;
    check (item.name == "Sidechain", "Sidechain está entre los efectos (Dinámica)");
    auto p = s.insertPlugin (*bt, item);
    check (p != nullptr && p->canSidechain() && p->getSidechainSourceID() == kt->itemID,
           "al ponerlo en el Bajo ya escucha al Bombo solo");
    if (p == nullptr) return;
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    ExportOptions o;
    o.destination = out.getChildFile ("sidechain mezcla.wav");
    o.tailSeconds = 0.0;
    o.sampleRate = sr;
    auto r = s.exportMix (o);
    AudioBuffer<float> mix;
    double msr = 0.0;
    const bool ok = r.wasOk() && readFile (s.engine, o.destination, mix, msr);
    const float onKick = ok ? toneDb (mix, 2000.0, msr, 1.02, 0.03) : 0.0f, between = ok ? toneDb (mix, 2000.0, msr, 1.40, 0.05) : 0.0f;
    check (ok && between - onKick > 12.0f, "en la mezcla exportada el Bajo baja " + String (between - onKick, 1) + " dB con cada golpe del Bombo");
    // nobody to listen to: the bass stays put
    s.setSidechain (*p, nullptr);
    MessageManager::getInstance()->runDispatchLoopUntil (200);
    r = s.exportMix (o);
    const bool ok2 = r.wasOk() && readFile (s.engine, o.destination, mix, msr);
    const float a = ok2 ? toneDb (mix, 2000.0, msr, 1.02, 0.03) : 0.0f, b2 = ok2 ? toneDb (mix, 2000.0, msr, 1.40, 0.05) : 99.0f;
    check (ok2 && std::abs (b2 - a) < 1.5f, "sin conectar, el Bajo no se mueve con el Bombo (" + String (b2 - a, 1) + " dB)");
}
}

namespace
{
void randomSequences()
{
    std::cout << "secuencias aleatorias" << std::endl;
    using namespace racoon::aleatorio;
    auto inScale = [] (int pitch, int root, bool minor)
    {
        static const int maj[] { 0, 2, 4, 5, 7, 9, 11 }, mi[] { 0, 2, 3, 5, 7, 8, 10 };
        const int rel = ((pitch - root) % 12 + 12) % 12;
        for (int i = 0; i < 7; ++i) if ((minor ? mi : maj)[i] == rel) return true;
        return false;
    };
    for (auto rol : { Rol::bajo, Rol::acordes, Rol::melodia, Rol::arpegio, Rol::textura, Rol::pad, Rol::coro, Rol::guitarra, Rol::voz })
    {
        int outOfKey = 0, outOfClip = 0, total = 0, lowest = 127, highest = 0;
        std::set<String> different;
        for (int seed = 1; seed <= 40; ++seed)
        {
            Opciones o; o.rol = rol; o.bars = 4; o.root = seed % 12; o.minor = seed % 3 != 0; o.seed = seed;
            auto notes = generar (o);
            String sig;
            for (auto& n : notes)
            {
                ++total;
                if (! inScale (n.pitch, o.root, o.minor)) ++outOfKey;
                if (n.start < 0.0 || n.start >= 16.0 || n.length <= 0.0) ++outOfClip;
                lowest = jmin (lowest, n.pitch); highest = jmax (highest, n.pitch);
                sig << n.pitch << "@" << n.start << " ";
            }
            different.insert (sig);
        }
        // a key repeats every 12 seeds here, so a few may come out alike; most must differ
        const int minDifferent = rol == Rol::textura || rol == Rol::pad || rol == Rol::coro ? 8 : 25;
        check (total > (rol == Rol::textura ? 40 * 2 : 40 * 4) && outOfKey == 0 && outOfClip == 0 && (int) different.size() >= minDifferent,
               "aleatoria de " + nombreRol (rol) + ": " + String ((int) different.size()) + " distintas de 40, " + String (total)
               + " notas, todas en la tonalidad y dentro de los 4 compases (" + String (outOfKey) + " fuera de tono)");
        if (rol == Rol::bajo) check (highest <= 52 && lowest >= 24, "el bajo vive abajo (de " + String (lowest) + " a " + String (highest) + ")");
        if (rol == Rol::melodia) check (lowest >= 40 && highest <= 74, "la melodía va grave (de "_u + String (lowest) + " a " + String (highest) + ")");
    }
    // chords: three or more notes at once
    {
        Opciones o; o.rol = Rol::acordes; o.seed = 9;
        auto notes = generar (o);
        std::map<double, int> at;
        for (auto& n : notes) ++at[n.start];
        int chords = 0;
        for (auto& [t, k] : at) if (k >= 3) ++chords;
        check (chords >= 4, "los acordes suenan de a 3 o 4 notas (" + String (chords) + " acordes)");
    }
    // drums: only the kit's notes, the backbeat on 2 and 4 of every bar, the kick on the one
    {
        Bateria kit; kit.kick = 40; kit.snare = 41; kit.clap = 41; kit.closedHat = 50; kit.openHat = 51; kit.rim = 52; kit.tom = 53; kit.perc = 54;
        kit.shaker = 42; kit.congaHi = 43; kit.congaLo = 44; kit.bongoHi = 45; kit.bongoLo = 46; kit.cowbell = 47; kit.tomHi = 48; kit.tomLo = 49; kit.crash = 50; kit.ride = 51;
        bool onlyKit = true, backbeat = true, kickOnOne = true;
        for (int seed = 1; seed <= 30; ++seed)
        {
            Opciones o; o.rol = Rol::bateria; o.bars = 4; o.seed = seed; o.drums = kit;
            auto notes = generar (o);
            for (auto& n : notes) onlyKit = onlyKit && n.pitch >= 40 && n.pitch <= 54;
            for (int b = 0; b < 3; ++b)
            {
                bool two = false, four = false, one = false;
                for (auto& n : notes)
                {
                    if (n.pitch == 41 && std::abs (n.start - (b * 4 + 1)) < 1e-6) two = true;
                    if (n.pitch == 41 && std::abs (n.start - (b * 4 + 3)) < 1e-6) four = true;
                    if (n.pitch == 40 && std::abs (n.start - b * 4) < 1e-6) one = true;
                }
                backbeat = backbeat && two && four;
                kickOnOne = kickOnOne && one;
            }
        }
        check (onlyKit && backbeat && kickOnOne, "la batería usa solo las notas del kit, con caja en 2 y 4 y bombo en el 1");
    }
    // the same seed gives the same sequence (so a test, or an undo and redo, sees the same notes)
    {
        Opciones o; o.rol = Rol::melodia; o.seed = 1234;
        auto a = generar (o), b = generar (o);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) same = a[i].pitch == b[i].pitch && a[i].start == b[i].start;
        check (same, "la misma semilla da la misma secuencia");
    }
    // the key of notes already there
    {
        std::vector<Nota> am;
        for (int p : { 57, 60, 64, 57, 62, 65, 64, 59, 57 }) am.push_back ({ 0.0, 1.0, p, 100 });
        int root = 0; bool minor = false;
        check (tonalidadDe (am, root, minor) && root == 9 && minor, "reconoce La menor en notas sueltas: " + nombreTonalidad (root, minor));
        check (rolDe ("Badger") == Rol::bajo && rolDe ("Hamster") == Rol::bateria && rolDe ("Dormouse") == Rol::pad && rolDe ("Squirrel") == Rol::melodia
                 && rolDe ("Porcupine") == Rol::coro && rolDe ("Mole") == Rol::percusion && rolDe ("Ferret") == Rol::guitarra
                 && rolDe ("Chinchilla") == Rol::voz && rolDe ("Skunk") == Rol::textura && rolDe ("Chipmunk") == Rol::acordes,
               "cada animal tiene su papel: Badger bajo, Hamster batería, Mole percusión, Dormouse pad, Porcupine coro, Chipmunk acordes, Ferret guitarra, Chinchilla voz, Skunk textura"_u);
    }

    // ---------------------------------------------------------------- the rules of each instrument
    const std::vector<int> prog { 0, 5, 3, 4 };      // Am F D E... in La menor: i VI iv v
    auto chordPcsOf = [] (int root, bool minor, int degree)
    {
        static const int maj[] { 0, 2, 4, 5, 7, 9, 11 }, mi[] { 0, 2, 3, 5, 7, 8, 10 };
        std::set<int> s;
        for (int k : { 0, 2, 4, 6 })
        {
            const int d = degree + k, oct = d / 7;
            s.insert ((root + (minor ? mi : maj)[d - oct * 7]) % 12);
        }
        return s;
    };
    auto make = [&] (Rol r, juce::int64 seed, Estilo e = Estilo::cuatro, int bars = 8)
    {
        Opciones o; o.rol = r; o.bars = bars; o.root = 9; o.minor = true; o.seed = seed; o.estilo = e; o.progresion = prog;
        return generar (o);
    };
    {
        // bass: one note at a time, the root of the bar's chord on its first note, the chord changes only on the bar line
        int overlaps = 0, wrongRoot = 0, outside = 0;
        for (int seed = 1; seed <= 40; ++seed)
            for (auto e : { Estilo::cuatro, Estilo::afro, Estilo::trap, Estilo::dnb, Estilo::latino, Estilo::boombap })
            {
                auto notes = make (Rol::bajo, seed, e);
                for (size_t i = 1; i < notes.size(); ++i) if (notes[i].start < notes[i - 1].start + notes[i - 1].length - 1.0e-6) ++overlaps;
                for (int b = 0; b < 8; ++b)
                {
                    const Nota* first = nullptr;
                    for (auto& n : notes) if (n.start >= b * 4.0 - 1.0e-6 && n.start < b * 4.0 + 4.0) { first = &n; break; }
                    const int rootPc = (9 + std::array<int, 7> { 0, 2, 3, 5, 7, 8, 10 }[(size_t) prog[(size_t) (b % 4)]]) % 12;
                    if (first != nullptr && first->pitch % 12 != rootPc) ++wrongRoot;
                    const Nota* lastInBar = nullptr;
                    for (auto& n : notes) if (n.start >= b * 4.0 && n.start < b * 4.0 + 4.0) lastInBar = &n;
                    for (auto& n : notes)
                        if (n.start >= b * 4.0 && n.start < b * 4.0 + 4.0)
                        {
                            // the last note of a phrase may step towards the next chord
                            if (b % 4 == 3 && &n == lastInBar) continue;
                            if (! chordPcsOf (9, true, prog[(size_t) (b % 4)]).count (n.pitch % 12)) ++outside;
                        }
                }
            }
        check (overlaps == 0 && wrongRoot == 0, "bajo: una nota a la vez y la raíz del acorde en la primera nota de cada compás, en 6 estilos ("_u
                                                   + String (overlaps) + " encimadas, " + String (wrongRoot) + " sin raíz)");
        check (outside == 0, "bajo: solo notas del acorde (raíz, quinta, octava) salvo la de paso al siguiente acorde (" + String (outside) + " fuera)");
    }
    {
        // pad and choir: every note belongs to the chord of its bar; the choir in four voices, in their ranges,
        // never crossed, and without parallel fifths or octaves
        int padOut = 0, padAttacks = 0, choirBad = 0, crossings = 0, parallels = 0, ranges = 0;
        for (int seed = 1; seed <= 30; ++seed)
        {
            auto pd = make (Rol::pad, seed);
            for (auto& n : pd)
            {
                const int b = (int) (n.start / 4.0 + 1.0e-6);
                if (! chordPcsOf (9, true, prog[(size_t) (b % 4)]).count (n.pitch % 12)) ++padOut;
                ++padAttacks;
            }
            auto ch = make (Rol::coro, seed);
            std::vector<std::array<int, 4>> voices;
            // the choir sings in several rhythms (held chords, two "ah", "ooh" on 2 and 4...): every attack has its four voices
            std::set<double> attacks;
            for (auto& n : ch) attacks.insert (n.start);
            for (double at : attacks)
            {
                const int b = (int) (at / 4.0 + 1.0e-6);
                std::vector<int> sounding;
                for (auto& n : ch) if (n.start <= at + 1.0e-6 && n.start + n.length > at + 0.05) sounding.push_back (n.pitch);
                std::sort (sounding.begin(), sounding.end());
                if (sounding.size() != 4) { ++choirBad; continue; }
                for (int x : sounding) if (! chordPcsOf (9, true, prog[(size_t) (b % 4)]).count (x % 12)) ++choirBad;
                if (sounding[0] < 33 || sounding[0] > 53 || sounding[3] < 50 || sounding[3] > 70) ++ranges;
                for (size_t k = 1; k < 4; ++k) if (sounding[k] <= sounding[k - 1]) ++crossings;
                voices.push_back ({ sounding[0], sounding[1], sounding[2], sounding[3] });
            }
            for (size_t v = 1; v < voices.size(); ++v)
                for (int i = 0; i < 4; ++i)
                    for (int j = i + 1; j < 4; ++j)
                    {
                        const int a = (voices[v - 1][(size_t) j] - voices[v - 1][(size_t) i]) % 12, b2 = (voices[v][(size_t) j] - voices[v][(size_t) i]) % 12;
                        const bool moved = voices[v][(size_t) i] != voices[v - 1][(size_t) i] || voices[v][(size_t) j] != voices[v - 1][(size_t) j];
                        if (moved && a == b2 && (a == 7 || a == 0)) ++parallels;
                    }
        }
        check (padOut == 0 && padAttacks < 30 * 8 * 4, "pad: cada nota es del acorde de su compás y las notas comunes se quedan sonando ("
                                                         + String (padAttacks) + " ataques en 30 pads de 8 compases)");
        check (choirBad == 0 && crossings == 0 && ranges == 0, "coro: cuatro voces en su registro, del acorde, sin cruzarse");
        check (parallels == 0, "coro: sin quintas ni octavas paralelas (" + String (parallels) + ")");
    }
    {
        // chords: stabs change chord only on the bar line (or anticipate the next one at the end of a phrase)
        int wrong = 0;
        for (int seed = 1; seed <= 30; ++seed)
            for (auto e : { Estilo::cuatro, Estilo::afro, Estilo::trap })
                for (auto& n : make (Rol::acordes, seed, e))
                {
                    const int b = (int) (n.start / 4.0 + 1.0e-6);
                    // the chord's tones, its seventh and its ninth (stabs may be sevenths, ninths, sus2 or add9)
                    auto withNinth = [&] (int degree) { auto c = chordPcsOf (9, true, degree); c.insert ((9 + std::array<int, 7> { 0, 2, 3, 5, 7, 8, 10 }[(size_t) ((degree + 1) % 7)]) % 12); return c; };
                    const bool here = withNinth (prog[(size_t) (b % 4)]).count (n.pitch % 12) > 0;
                    const bool next = (b % 4 == 3) && n.start - b * 4.0 >= 2.5 && withNinth (prog[(size_t) ((b + 1) % 4)]).count (n.pitch % 12) > 0;
                    if (! here && ! next) ++wrong;
                }
        check (wrong == 0, "acordes: el acorde (con su séptima o novena) cambia en la barra de compás o se anticipa al final de la frase (" + String (wrong) + " fuera)");
    }
    {
        // texture: over the key (a pedal, a slow line, a pulse or a bloom), different each time, never out of the key
        int outside = 0;
        std::set<String> kinds;
        const std::array<int, 7> aMinor { 9, 11, 0, 2, 4, 5, 7 };
        for (int seed = 1; seed <= 20; ++seed)
        {
            String sig;
            for (auto& n : make (Rol::textura, seed))
            {
                if (std::find (aMinor.begin(), aMinor.end(), n.pitch % 12) == aMinor.end()) ++outside;
                sig << n.pitch << "@" << n.start << ";";
            }
            kinds.insert (sig);
        }
        check (outside == 0 && kinds.size() >= 15, "textura: en la tonalidad y distinta cada vez (" + String ((int) kinds.size()) + " distintas en 20 semillas)");
    }
    {
        // percussion: shaker, congas, bongos and cowbell; never a kick or a snare
        Bateria kit;
        bool onlyHand = true, shakerEveryBar = true;
        for (int seed = 1; seed <= 30; ++seed)
            for (auto e : { Estilo::cuatro, Estilo::afro, Estilo::latino })
            {
                auto notes = make (Rol::percusion, seed, e);
                for (auto& n : notes)
                    onlyHand = onlyHand && (n.pitch == kit.shaker || n.pitch == kit.congaHi || n.pitch == kit.congaLo || n.pitch == kit.bongoHi
                                            || n.pitch == kit.bongoLo || n.pitch == kit.cowbell || n.pitch == kit.rim);
                for (int b = 0; b < 8; ++b)
                {
                    bool sh = false;
                    for (auto& n : notes) if (n.pitch == kit.shaker && n.start >= b * 4.0 && n.start < b * 4.0 + 4.0) sh = true;
                    shakerEveryBar = shakerEveryBar && sh;
                }
            }
        check (onlyHand && shakerEveryBar, "percusión: shaker en cada compás y congas, bongos, cencerro o clave; sin bombo ni caja"_u);
    }
    {
        // the voice: a range a person sings, no sixteenth runs, a rest in every bar, the last note on the tonic
        int outOfRange = 0, sixteenths = 0, noRest = 0, endsHome = 0;
        for (int seed = 1; seed <= 30; ++seed)
        {
            auto notes = make (Rol::voz, seed);
            for (auto& n : notes)
            {
                if (n.pitch < 48 || n.pitch > 67) ++outOfRange;
                if (std::abs (n.start * 2.0 - std::round (n.start * 2.0)) > 1.0e-6) ++sixteenths;
            }
            for (int b = 0; b < 8; ++b)
            {
                double sung = 0.0;
                for (auto& n : notes) if (n.start >= b * 4.0 && n.start < b * 4.0 + 4.0) sung += n.length;
                if (sung > 3.9) ++noRest;
            }
            if (! notes.empty() && notes.back().pitch % 12 == 9) ++endsHome;
        }
        check (outOfRange == 0 && sixteenths == 0 && noRest == 0 && endsHome == 30,
               "voz: de La3 a Re5, sílabas de corchea o más, un respiro en cada compás y termina en la tónica"_u);
    }
    {
        // the melody lands: the last note of each phrase is long, and the very last is the tonic
        int shortEnds = 0, home = 0;
        for (int seed = 1; seed <= 30; ++seed)
        {
            auto notes = make (Rol::melodia, seed);
            for (int ph = 0; ph < 2; ++ph)
            {
                const Nota* lastN = nullptr;
                for (auto& n : notes) if (n.start >= ph * 16.0 && n.start < ph * 16.0 + 16.0) lastN = &n;
                if (lastN != nullptr && lastN->length < 1.0) ++shortEnds;
            }
            if (! notes.empty() && notes.back().pitch % 12 == 9) ++home;
        }
        check (shortEnds == 0 && home == 30, "melodía: cada frase de 4 compases termina en una nota larga y la última es la tónica"_u);
    }
    {
        // drums by style, and fills only at the end of a phrase
        Bateria kit;
        bool trapHalf = true, dembow = true, fillsAtEnd = true;
        for (int seed = 1; seed <= 20; ++seed)
        {
            for (auto& n : make (Rol::bateria, seed, Estilo::trap))
                if (n.pitch == kit.clap && std::abs (std::fmod (n.start, 4.0) - 2.0) > 1.0e-6) trapHalf = false;
            for (auto& n : make (Rol::bateria, seed, Estilo::latino))
                if (n.pitch == kit.snare)
                {
                    const double s = std::fmod (n.start, 4.0) / 0.25;
                    const int st = (int) std::round (s);
                    if (std::abs (s - st) < 1.0e-6 && st != 3 && st != 6 && st != 11 && st != 14 && st < 12) dembow = false;
                }
            for (auto& n : make (Rol::bateria, seed, Estilo::cuatro))
                if (n.pitch == kit.tom && ((int) (n.start / 4.0) % 4) != 3) fillsAtEnd = false;
        }
        check (trapHalf, "trap: palmas solo en el tiempo 3 (medio tiempo)");
        check (dembow, "dembow: la caja en el patrón 3-3-2 (pasos 4, 7, 12 y 15)");
        check (fillsAtEnd, "fills de batería solo en el último compás de cada frase");
    }
    {
        // VARIATION: a clip that already has notes keeps its shape. A block like Racoon's: the bass waits two bars,
        // the chords rest in the third phrase, the melody comes in at bar 5
        std::map<Rol, std::vector<Nota>> moldes;
        {
            auto& b = moldes[Rol::bajo];
            for (int bar = 2; bar < 16; ++bar)
            {
                const int root = 45 + std::array<int, 4> { 0, 8, 5, 7 }[(size_t) (bar % 4)];   // A F D E (low)
                for (int beat = 0; beat < 4; ++beat)
                    for (int k = 0; k < 3; ++k) b.push_back ({ bar * 4.0 + beat + 0.25 + k * 0.25, 0.2, root - 12 + (root - 12 < 33 ? 12 : 0), 100 });
            }
            auto& ch = moldes[Rol::acordes];
            const int triads[4][3] { { 57, 60, 64 }, { 57, 60, 65 }, { 57, 62, 65 }, { 56, 59, 64 } };
            for (int bar = 4; bar < 16; ++bar)
            {
                if (bar >= 8 && bar < 12) continue;
                for (double t : { 0.5, 1.5, 2.5, 3.5 }) for (int p2 : triads[bar % 4]) ch.push_back ({ bar * 4.0 + t, 0.3, p2, 94 });
            }
            auto& pd = moldes[Rol::pad];
            for (int bar = 0; bar < 16; ++bar) for (int p2 : triads[bar % 4]) pd.push_back ({ bar * 4.0, 3.9, p2, 78 });
            moldes[Rol::coro] = pd;
            auto& me = moldes[Rol::melodia];
            for (int bar = 4; bar < 16; ++bar)
                for (double t : { 0.0, 0.75, 1.5, 2.5 }) me.push_back ({ bar * 4.0 + t, 0.5, 69 + (int) t * 2, 96 });
            auto& tx = moldes[Rol::textura];
            for (int bar = 0; bar < 16; ++bar) tx.push_back ({ bar * 4.0, 3.9, 72 - bar, 70 });
            auto& dr = moldes[Rol::bateria];
            for (int bar = 0; bar < 16; ++bar)
                for (int s = 0; s < 16; ++s)
                {
                    if (s % 4 == 0 && (bar < 8 || bar >= 12)) dr.push_back ({ bar * 4.0 + s * 0.25, 0.25, 36, 112 });
                    if (s == 4 || s == 12) dr.push_back ({ bar * 4.0 + s * 0.25, 0.25, 39, 104 });
                    if (s % 2 == 0) dr.push_back ({ bar * 4.0 + s * 0.25, 0.25, 42, 80 });
                }
        }
        auto barsWith = [] (const std::vector<Nota>& v)
        {
            std::set<int> s;
            for (auto& n : v) s.insert ((int) std::floor (n.start / 4.0 + 1.0e-9));
            return s;
        };
        for (auto& [rol, molde] : moldes)
        {
            int sameBars = 0, inRange = 0, changed = 0, sameHarmony = 0, keptRhythm = 0;
            int lo = 127, hi = 0;
            for (auto& n : molde) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
            for (int seed = 1; seed <= 20; ++seed)
            {
                Opciones o; o.rol = rol; o.bars = 16; o.root = 9; o.minor = true; o.seed = seed; o.molde = molde;
                o.progresion = { 0, 5, 3, 4 };
                auto v = generar (o);
                if (barsWith (v) == barsWith (molde)) ++sameBars;
                bool range = true;
                for (auto& n : v) range = range && n.pitch >= lo - 3 && n.pitch <= hi + 4;
                if (range) ++inRange;
                String a, b;
                for (auto& n : v) a << n.pitch << "@" << String (n.start, 2) << " ";
                for (auto& n : molde) b << n.pitch << "@" << String (n.start, 2) << " ";
                if (a != b) ++changed;
                // the harmony of each bar: the variation's pitch classes are the clip's (chord roles) or its root (bass)
                bool harmony = true;
                for (int bar : barsWith (molde))
                {
                    std::set<int> was, now;
                    int wasRoot = -1, nowRoot = -1;
                    for (auto& n : molde) if ((int) (n.start / 4.0) == bar) { was.insert (n.pitch % 12); if (wasRoot < 0) wasRoot = n.pitch % 12; }
                    for (auto& n : v) if ((int) (n.start / 4.0) == bar) { now.insert (n.pitch % 12); if (nowRoot < 0) nowRoot = n.pitch % 12; }
                    if (rol == Rol::bajo) harmony = harmony && wasRoot == nowRoot;
                    if (rol == Rol::pad || rol == Rol::coro) harmony = harmony && was == now;
                    if (rol == Rol::acordes) for (int x : now) harmony = harmony && (was.count (x) > 0 || bar % 4 == 3);
                }
                if (harmony) ++sameHarmony;
                // the rhythm: as many hits per bar (give or take one) as the clip had
                bool rhythm = true;
                for (int bar : barsWith (molde))
                {
                    std::set<int> was, now;
                    for (auto& n : molde) if ((int) (n.start / 4.0) == bar) was.insert ((int) std::round (n.start * 4.0));
                    for (auto& n : v) if ((int) (n.start / 4.0) == bar) now.insert ((int) std::round (n.start * 4.0));
                    if (std::abs ((int) was.size() - (int) now.size()) > (rol == Rol::bateria ? 3 : 1)) rhythm = false;
                }
                if (rhythm) ++keptRhythm;
            }
            check (sameBars == 20 && inRange == 20 && changed >= 18 && sameHarmony == 20 && keptRhythm == 20,
                   "variar " + nombreRol (rol) + ": suena en los mismos compases (" + String (sameBars) + "/20), en su registro (" + String (inRange)
                   + "/20), con su armonía (" + String (sameHarmony) + "/20) y su ritmo (" + String (keptRhythm) + "/20); distinta del original en "
                   + String (changed) + "/20"_u);
        }
        // the drums keep their backbone: the kick and the claps exactly where they were
        {
            Opciones o; o.rol = Rol::bateria; o.bars = 16; o.seed = 5; o.molde = moldes[Rol::bateria];
            auto v = generar (o);
            int kicks = 0, claps = 0, kicksWas = 0, clapsWas = 0;
            std::set<String> pos;
            for (auto& n : moldes[Rol::bateria]) if (n.pitch == 36 || n.pitch == 39) { pos.insert (String (n.pitch) + "@" + String (n.start, 2)); (n.pitch == 36 ? kicksWas : clapsWas)++; }
            for (auto& n : v) if ((n.pitch == 36 || n.pitch == 39) && pos.count (String (n.pitch) + "@" + String (n.start, 2))) (n.pitch == 36 ? kicks : claps)++;
            check (kicks == kicksWas && claps == clapsWas, "variar batería: bombo y palmas no se mueven (" + String (kicks) + " de " + String (kicksWas) + " bombos)"_u);
        }
    }
    {
        // a one-bar clip does not change chord inside the bar
        Opciones o; o.rol = Rol::acordes; o.bars = 1; o.seed = 3; o.root = 0; o.minor = false;
        std::set<int> pcs;
        for (auto& n : generar (o)) pcs.insert (n.pitch % 12);
        check (pcs.size() <= 4, "un clip de un compás tiene un solo acorde (" + String ((int) pcs.size()) + " notas distintas)");
    }
}

// the arrangement edits: a range duplicates into the next equal span, and whatever a clip lands on is cut away
struct ClipSpan { double start, end; int notes; };
std::vector<ClipSpan> spansOf (Session& s, te::AudioTrack& t)
{
    std::vector<ClipSpan> v;
    auto& ts = s.getEdit().tempoSequence;
    for (auto* c : t.getClips())
    {
        int n = 0;
        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            for (auto* note : mc->getSequence().getNotes())
            {
                // only the notes that sound inside the clip
                const auto at = mc->getTimeOfContentBeat (note->getStartBeat());
                if (at >= c->getPosition().getStart() && at < c->getPosition().getEnd()) ++n;
            }
        v.push_back ({ ts.toBeats (c->getPosition().getStart()).inBeats(), ts.toBeats (c->getPosition().getEnd()).inBeats(), n });
    }
    std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    return v;
}
String describe (const std::vector<ClipSpan>& v)
{
    StringArray a;
    for (auto& c : v) a.add (String (c.start, 2) + "-" + String (c.end, 2) + "(" + String (c.notes) + ")");
    return a.joinIntoString (" ");
}

void arrangementEdits (Session& s)
{
    std::cout << "maqueta: duplicar, pegar encima y mover encima" << std::endl;
    s.newProject (false);
    s.setBpm (120.0);
    auto* t = s.addAudioTrack ("Notas");
    if (t == nullptr) { check (false, "canal"); return; }
    // 16 beats, one note per beat
    auto* c = s.createMidiClip (*t, { 0_bp, 16_bp }, "dieciséis"_u);
    for (int i = 0; i < 16; ++i) add (*c, i, 0.5, 60 + i % 12);

    // a range of 4 beats (4-8) duplicated: the copy sits at 8-12, with the notes of 4-8, and 8-12 of the original goes
    s.getEdit().getUndoManager().beginNewTransaction();
    s.copyRange ({ t }, 4.0, 8.0);
    s.getEdit().getUndoManager().beginNewTransaction();
    auto pasted = s.pasteAt (*t, 8.0);
    auto v = spansOf (s, *t);
    int notes = 0; for (auto& x : v) notes += x.notes;
    bool piece = false;
    for (auto& x : v) if (near (x.start, 8.0, 1e-4) && near (x.end, 12.0, 1e-4) && x.notes == 4) piece = true;
    bool overlap = false;
    for (size_t i = 1; i < v.size(); ++i) if (v[i].start < v[i - 1].end - 1e-4) overlap = true;
    check (pasted.size() == 1 && piece && ! overlap && notes == 16 && near (v.back().end, 16.0, 1e-4),
           "duplicar 4 tiempos (4-8): la copia queda en 8-12 con sus 4 notas y lo de abajo se borra: " + describe (v));
    // and its notes are the ones of 4-8
    if (pasted.size() == 1)
        if (auto* mc = dynamic_cast<te::MidiClip*> (pasted[0]))
        {
            Array<int> pitches;
            for (auto* n : mc->getSequence().getNotes())
            {
                const double b = s.getEdit().tempoSequence.toBeats (mc->getTimeOfContentBeat (n->getStartBeat())).inBeats();
                if (b >= 8.0 - 1e-4 && b < 12.0 - 1e-4) pitches.add (n->getNoteNumber());
            }
            check (pitches == Array<int> { 64, 65, 66, 67 }, "las notas de la copia son las de 4-8 (64 65 66 67): "
                   + String (pitches.size() > 0 ? pitches[0] : -1) + "..");
        }
    s.getEdit().getUndoManager().undo();
    v = spansOf (s, *t);
    check (v.size() == 1 && near (v[0].end, 16.0, 1e-4) && v[0].notes == 16, "deshacer deja el clip como estaba: " + describe (v));

    // a range with silence: the copy still takes its whole length (what was there in the silent part goes too)
    s.newProject (false);
    t = s.addAudioTrack ("Notas");
    auto* a = s.createMidiClip (*t, { 0_bp, 2_bp }, "corto");
    add (*a, 0.0, 0.5);
    auto* b = s.createMidiClip (*t, { 4_bp, 8_bp }, "debajo");
    add (*b, 0.0, 0.5); add (*b, 2.0, 0.5);   // at 4 and 6 of the song (notes count from the clip's start)
    s.getEdit().getUndoManager().beginNewTransaction();
    s.copyRange ({ t }, 0.0, 4.0);
    s.getEdit().getUndoManager().beginNewTransaction();
    s.pasteAt (*t, 4.0);
    v = spansOf (s, *t);
    check (v.size() == 2 && near (v[1].start, 4.0, 1e-4) && near (v[1].end, 6.0, 1e-4) && v[1].notes == 1,
           "pegar un pedazo de 4 tiempos encima borra esos 4 tiempos aunque la copia tenga silencio: " + describe (v));

    // a whole clip duplicated: right after itself, and the clip that was there is trimmed
    s.newProject (false);
    t = s.addAudioTrack ("Notas");
    auto* one = s.createMidiClip (*t, { 0_bp, 4_bp }, "uno");
    add (*one, 0.0, 1.0);
    auto* next = s.createMidiClip (*t, { 2_bp, 10_bp }, "siguiente");
    add (*next, 1.0, 0.5); add (*next, 7.0, 0.5);   // at 3 and 9
    s.getSelection().selectOnly (one);
    s.getEdit().getUndoManager().beginNewTransaction();
    // "siguiente" (2-10) loses 4-8 under the copy: its head 2-4 overlaps "uno" and stays (it was there already)
    auto* dup = s.duplicateClip (*one);
    v = spansOf (s, *t);
    bool dupOk = dup != nullptr && near (s.getEdit().tempoSequence.toBeats (dup->getPosition().getStart()).inBeats(), 4.0, 1e-4);
    bool tail = false, gone = true;
    for (auto& x : v)
    {
        if (near (x.start, 8.0, 1e-4) && near (x.end, 10.0, 1e-4) && x.notes == 1) tail = true;
        if (x.start < 8.0 - 1e-4 && x.end > 4.0 + 1e-4 && ! (near (x.start, 4.0, 1e-4) && near (x.end, 8.0, 1e-4))) gone = false;
    }
    check (dupOk && tail && gone, "duplicar un clip: la copia va justo después y corta lo que estaba ahí: " + describe (v));

    // moving a clip on top of another one: the one underneath is cut where the moved one lands
    s.newProject (false);
    t = s.addAudioTrack ("Notas");
    auto* m1 = s.createMidiClip (*t, { 0_bp, 4_bp }, "mueve");
    add (*m1, 0.0, 1.0);
    auto* m2 = s.createMidiClip (*t, { 8_bp, 16_bp }, "abajo");
    for (int i = 8; i < 16; ++i) add (*m2, i - 8, 0.5);
    s.getEdit().getUndoManager().beginNewTransaction();
    s.moveClips ({ m1 }, 10.0, 0);   // to 10-14, in the middle of 8-16
    v = spansOf (s, *t);
    check (v.size() == 3 && near (v[0].start, 8.0, 1e-4) && near (v[0].end, 10.0, 1e-4) && v[0].notes == 2
           && near (v[1].start, 10.0, 1e-4) && v[1].notes == 1 && near (v[2].start, 14.0, 1e-4) && near (v[2].end, 16.0, 1e-4) && v[2].notes == 2,
           "mover un clip encima de otro: el de abajo conserva solo lo que queda fuera (8-10 y 14-16): " + describe (v));
    s.getEdit().getUndoManager().undo();
    v = spansOf (s, *t);
    check (v.size() == 2 && v[1].notes == 8, "y deshacer devuelve el de abajo entero: " + describe (v));

    // pasting copied clips on another clip
    s.newProject (false);
    t = s.addAudioTrack ("Notas");
    auto* p1 = s.createMidiClip (*t, { 0_bp, 2_bp }, "copia");
    add (*p1, 0.0, 0.5);
    auto* p2 = s.createMidiClip (*t, { 4_bp, 12_bp }, "fondo");
    for (int i = 4; i < 12; ++i) add (*p2, i - 4, 0.5);
    s.copyClips ({ p1 });
    s.getEdit().getUndoManager().beginNewTransaction();
    s.pasteAt (*t, 4.0);
    v = spansOf (s, *t);
    check (v.size() == 3 && near (v[1].start, 4.0, 1e-4) && near (v[1].end, 6.0, 1e-4) && near (v[2].start, 6.0, 1e-4) && v[2].notes == 6,
           "pegar un clip encima de otro: lo de abajo se borra solo donde cae (4-6): " + describe (v));
}
}

int runToolsTests (Session& s, const File& out)
{
    toolFailures = 0;
    arrangementEdits (s);
    randomSequences();
    sidechainInTheSong (s, out);
    noteTools (s);
    audioTools();
    clipTools (s, out);
    reportsAndHealth (s, out);
    return toolFailures;
}

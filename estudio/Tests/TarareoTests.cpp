// Tarareo a rola: synthetic hummed melodies (a voice with formants, vibrato, out-of-tune notes, slides and late
// attacks) and synthetic beatboxes, and the block built around them.
#include "engine/Session.h"
#include "engine/Tarareo.h"
#include "engine/Genres.h"
#include "engine/AudioTools.h"

using namespace racoon;
using namespace juce;

namespace
{
int humFailures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++humFailures;
}

struct Escrita { double beat, len; int pitch; };

// a voice: a glottal pulse train through three formant resonators ("mmm" / "aaa"), with vibrato, a constant detune,
// a drift per note, a slide into each note, attacks up to `jitter` seconds late, and breath noise
AudioBuffer<float> cantar (const std::vector<Escrita>& notes, double bpm, double sr, uint32 seed, bool silbido = false,
                           double desafinado = 0.18, double deriva = 0.15, double jitter = 0.03)
{
    Random rnd ((int64) seed);
    double end = 0.0;
    for (auto& n : notes) end = jmax (end, n.beat + n.len);
    const double spb = 60.0 / bpm;
    AudioBuffer<float> out (1, (int) ((end * spb + 0.6) * sr));
    out.clear();
    auto* y = out.getWritePointer (0);
    double phase = 0.0, curHz = 0.0;
    // formant resonators (two-pole)
    struct Res { double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0; void set (double f, double bw, double sr)
        { const double r = std::exp (-MathConstants<double>::pi * bw / sr); a1 = 2 * r * std::cos (MathConstants<double>::twoPi * f / sr); a2 = -r * r; g = 1 - r; }
        double run (double x) { const double v = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = v; return v; } };
    Res f1, f2, f3;
    const bool aa = (seed % 2) == 0;
    f1.set (aa ? 750 : 300, 90, sr); f2.set (aa ? 1200 : 1000, 110, sr); f3.set (2600, 160, sr);
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        const double t0 = n.beat * spb + rnd.nextDouble() * jitter;
        const bool legato = k + 1 < notes.size() && std::abs (notes[k + 1].beat - (n.beat + n.len)) < 1e-6 && rnd.nextInt (3) > 0;
        const double t1 = (n.beat + n.len) * spb - (legato ? 0.0 : 0.05 + rnd.nextDouble() * 0.04);
        const double cents = desafinado * 100.0 + (rnd.nextDouble() * 2.0 - 1.0) * deriva * 100.0;
        const double target = 440.0 * std::pow (2.0, (n.pitch - 69 + cents / 100.0) / 12.0);
        if (curHz <= 0.0) curHz = target * 0.94;
        const int a = (int) (t0 * sr), b = jmin (out.getNumSamples(), (int) (t1 * sr));
        const double vibRate = 5.0 + rnd.nextDouble(), vibDepth = 0.003 + rnd.nextDouble() * 0.004;
        for (int i = a; i < b; ++i)
        {
            const double t = (i - a) / sr, left = (b - i) / sr;
            curHz += (target - curHz) * (1.0 - std::exp (-1.0 / (0.035 * sr)));          // the slide: about 35 ms
            const double vib = 1.0 + vibDepth * std::sin (MathConstants<double>::twoPi * vibRate * t) * jmin (1.0, t / 0.25);
            phase += curHz * vib / sr;
            phase -= std::floor (phase);
            const double env = jmin (1.0, t / 0.03) * jmin (1.0, left / 0.04) * (0.8 + 0.2 * std::sin (t * 3.0));
            double s;
            if (silbido) s = std::sin (MathConstants<double>::twoPi * phase) + 0.05 * (rnd.nextDouble() - 0.5);
            else
            {
                const double pulse = phase < 0.4 ? std::sin (MathConstants<double>::pi * phase / 0.4) : 0.0;   // the glottal opening
                s = 4.0 * (f1.run (pulse) + 0.6 * f2.run (pulse) + 0.25 * f3.run (pulse)) + 0.01 * (rnd.nextDouble() - 0.5);
            }
            y[i] += (float) (0.3 * env * s);
        }
    }
    // the room and the microphone: a little noise everywhere
    for (int i = 0; i < out.getNumSamples(); ++i) y[i] += (float) (0.002 * (rnd.nextDouble() - 0.5));
    return out;
}

struct Escrito { double beat; int tipo; };

AudioBuffer<float> beatboxear (const std::vector<Escrito>& hits, double bpm, double sr, uint32 seed, double bars)
{
    Random rnd ((int64) seed);
    const double spb = 60.0 / bpm;
    AudioBuffer<float> out (1, (int) ((bars * 4.0 * spb + 0.5) * sr));
    out.clear();
    auto* y = out.getWritePointer (0);
    for (auto& h : hits)
    {
        const int a = (int) ((h.beat * spb + rnd.nextDouble() * 0.02) * sr);
        double lp = 0, bp1 = 0, bp2 = 0, hp = 0, prevN = 0, ph = 0;
        const int len = (int) ((h.tipo == 0 ? 0.14 : h.tipo == 1 ? 0.16 : 0.05) * sr);
        for (int i = 0; i < len && a + i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double n = rnd.nextDouble() * 2.0 - 1.0;
            double s = 0.0;
            if (h.tipo == 0)            // "b": a lip burst, a low thump that falls from 140 to 50 Hz
            {
                const double f = 50.0 + 90.0 * std::exp (-t / 0.025);
                ph += f / sr;
                lp += (n - lp) * 0.05;
                s = std::sin (MathConstants<double>::twoPi * ph) * std::exp (-t / 0.06) + 0.3 * lp * std::exp (-t / 0.005);
            }
            else if (h.tipo == 1)       // "pf" / "k": a noisy burst in the middle, some throat tone
            {
                bp1 += (n - bp1) * 0.35;  bp2 += (bp1 - bp2) * 0.35;
                const double band = bp1 - bp2 * 0.8;
                ph += 190.0 / sr;
                s = (1.6 * band * std::exp (-t / 0.05) + 0.3 * std::sin (MathConstants<double>::twoPi * ph) * std::exp (-t / 0.03));
            }
            else                        // "ts": hiss
            {
                hp = n - prevN; prevN = n;
                s = 0.6 * hp * std::exp (-t / 0.015);
            }
            y[a + i] += (float) (0.5 * s * jmin (1.0, i / (0.002 * sr)));
        }
    }
    for (int i = 0; i < out.getNumSamples(); ++i) y[i] += (float) (0.002 * (rnd.nextDouble() - 0.5));
    return out;
}

// how many written notes were found: same sixteenth (or one off) and the same pitch class after fitting to the key
double acierto (const std::vector<Escrita>& escritas, const std::vector<tarareo::Nota>& halladas, bool exactPitch)
{
    int ok = 0;
    for (auto& e : escritas)
        for (auto& h : halladas)
            if (std::abs (h.beat - e.beat) <= 0.25 + 1e-6 && (exactPitch ? (h.pitch % 12 == e.pitch % 12) : true)) { ++ok; break; }
    return (double) ok / std::max<size_t> (1, escritas.size());
}

std::vector<Escrita> frase (std::initializer_list<std::tuple<double, double, int>> l)
{
    std::vector<Escrita> v;
    for (auto& [b, len, p] : l) v.push_back ({ b, len, p });
    return v;
}
}

// for the window's test: the A minor tune hummed at a tempo, and a boots-and-cats beatbox
AudioBuffer<float> tarareoDePrueba (double bpm, double sr, bool beatbox)
{
    if (beatbox)
    {
        std::vector<Escrito> bb;
        for (int b = 0; b < 8; ++b) { bb.push_back ({ (double) b, b % 2 == 0 ? 0 : 1 }); bb.push_back ({ b + 0.5, 2 }); }
        return beatboxear (bb, bpm, sr, 51, 2.0);
    }
    return cantar (frase ({ {0,1,69},{1,0.5,72},{1.5,0.5,71},{2,1,69},{3,1,64}, {4,0.5,69},{4.5,0.5,71},{5,1,72},{6,1.5,74},{7.5,0.5,72},
                            {8,1,76},{9,1,74},{10,1,72},{11,1,71}, {12,0.75,69},{12.75,0.75,71},{13.5,0.5,72},{14,2,69} }), bpm, sr, 52);
}

int runTarareoTests (Session& s, const File& out)
{
    std::cout << "tarareo a rola" << std::endl;
    const double sr = 48000.0;

    // YIN alone: a sine at 220 Hz and a voice at 146.8 Hz (D3)
    {
        AudioBuffer<float> b (1, 24000);
        for (int i = 0; i < 24000; ++i) b.setSample (0, i, 0.3f * std::sin (MathConstants<float>::twoPi * 220.0f * i / 48000.0f));
        const auto f0 = tarareo::seguirTono (b.getReadPointer (0), 24000, sr, 480);
        double err = 0.0; int n = 0;
        for (auto f : f0) if (f > 0) { err = jmax (err, std::abs (f - 220.0)); ++n; }
        check (n > 10 && err < 0.5, "YIN: una senoidal de 220 Hz se lee a " + String (220.0 + err, 2) + " Hz como mucho");
    }

    struct Caso { const char* nombre; double bpm; std::vector<Escrita> notas; int root; bool major; bool silbido; uint32 seed;
                  double desafinado = 0.18, deriva = 0.15, jitter = 0.03; };
    std::vector<Caso> casos {
        // La menor, voz media, 100 BPM, 4 compases
        { "La menor a 100 BPM (\"mmm\")", 100.0, frase ({ {0,1,69},{1,0.5,72},{1.5,0.5,71},{2,1,69},{3,1,64},
                                                     {4,0.5,69},{4.5,0.5,71},{5,1,72},{6,1.5,74},{7.5,0.5,72},
                                                     {8,1,76},{9,1,74},{10,1,72},{11,1,71},
                                                     {12,0.75,69},{12.75,0.75,71},{13.5,0.5,72},{14,2,69} }), 9, false, false, 11 },
        // Re mayor, voz grave (un hombre), 124 BPM, 2 compases
        { "Re mayor a 124 BPM, voz grave (\"aaa\")", 124.0, frase ({ {0,0.5,50},{0.5,0.5,54},{1,1,57},{2,0.5,54},{2.5,0.5,55},{3,1,57},
                                                                {4,0.5,59},{4.5,0.5,57},{5,1,55},{6,0.5,54},{6.5,0.5,52},{7,1,50} }), 2, true, false, 12 },
        // Sol menor silbado, agudo, 90 BPM
        { "Sol menor silbado a 90 BPM", 90.0, frase ({ {0,1,79},{1,1,82},{2,1,86},{3,0.5,84},{3.5,0.5,82},
                                                      {4,1,81},{5,1,79},{6,1,77},{7,1,79},
                                                      {8,0.5,74},{8.5,0.5,77},{9,1,79},{10,1,82},{11,1,81},
                                                      {12,1,79},{13,1,77},{14,2,79} }), 7, false, true, 13 },
        // Mi menor, notas repetidas (sílabas sobre la misma nota) a 140 BPM
        { "Mi menor con notas repetidas a 140 BPM", 140.0, frase ({ {0,0.5,64},{0.5,0.5,64},{1,0.5,64},{1.5,0.5,67},{2,1,66},{3,1,64},
                                                                    {4,0.5,71},{4.5,0.5,71},{5,0.5,71},{5.5,0.5,69},{6,1,67},{7,1,66} }), 4, false, false, 14 },
        // the same A minor tune by someone 45 cents flat, wobbling 20 cents per note and up to 60 ms late
        { "La menor desafinada y atrasada a 100 BPM", 100.0, frase ({ {0,1,69},{1,0.5,72},{1.5,0.5,71},{2,1,69},{3,1,64},
                                                     {4,0.5,69},{4.5,0.5,71},{5,1,72},{6,1.5,74},{7.5,0.5,72},
                                                     {8,1,76},{9,1,74},{10,1,72},{11,1,71},
                                                     {12,0.75,69},{12.75,0.75,71},{13.5,0.5,72},{14,2,69} }), 9, false, false, 15, -0.45, 0.2, 0.06 },
    };
    for (auto& c : casos)
    {
        const auto audio = cantar (c.notas, c.bpm, sr, c.seed, c.silbido, c.desafinado, c.deriva, c.jitter);
        auto notas = tarareo::transcribir (audio, sr, c.bpm);
        const auto tono = tarareo::detectarTono (notas);
        const double rit = acierto (c.notas, notas, false);
        auto ajust = notas;
        tarareo::ajustarATono (ajust, tono, 67);
        const double alt = acierto (c.notas, notas, true);
        String found;
        for (size_t i = 0; i < std::min<size_t> (notas.size(), 8); ++i) found << tarareo::nombreNota (notas[i].pitch) << "@" << String (notas[i].beat, 2) << " ";
        std::cout << "    " << c.nombre << ": " << (int) notas.size() << " notas de " << (int) c.notas.size() << ": " << found << std::endl;
        check (std::abs ((int) notas.size() - (int) c.notas.size()) <= jmax (1, (int) c.notas.size() / 8),
               String (c.nombre) + ": " + String ((int) notas.size()) + " notas halladas de " + String ((int) c.notas.size()));
        check (rit >= 0.85, String (c.nombre) + ": " + String (roundToInt (rit * 100)) + " % de las notas a tiempo (a un dieciseisavo)");
        check (alt >= 0.85, String (c.nombre) + ": " + String (roundToInt (alt * 100)) + " % con la nota correcta");
        check (tono.root == c.root && tono.major == c.major, String (c.nombre) + ": tono hallado " + keyName (tono.root, tono.major));
        // fitted, every note is in the key (the leading tone of minor allowed)
        static const int majS[] { 0, 2, 4, 5, 7, 9, 11 }, minS[] { 0, 2, 3, 5, 7, 8, 10 };
        int fuera = 0;
        for (auto& n : ajust)
        {
            const int rel = ((n.pitch - tono.root) % 12 + 12) % 12;
            bool in = ! tono.major && rel == 11;
            for (int k = 0; k < 7; ++k) in = in || rel == (tono.major ? majS : minS)[k];
            fuera += in ? 0 : 1;
        }
        check (fuera == 0, String (c.nombre) + ": ajustadas, todas las notas en la tonalidad");
        const auto prog = tarareo::progresion (ajust, tono, true);
        // the chord of each bar holds at least half of what is sung in it (by length)
        int sostiene = 0, compases = 0;
        static const int* esc[2] { minS, majS };
        double lastB = 0; for (auto& n : ajust) lastB = jmax (lastB, n.beat + n.len);
        const int bars = jmax (1, (int) std::ceil (lastB / 4.0 - 1e-6));
        for (int bar = 0; bar < jmin (4, bars); ++bar)
        {
            const int* sc = esc[tono.major ? 1 : 0];
            const int d = prog[(size_t) bar];
            double dentro = 0, todo = 0;
            for (auto& n : ajust)
            {
                if (n.beat < bar * 4 || n.beat >= bar * 4 + 4) continue;
                const int rel = ((n.pitch - tono.root) % 12 + 12) % 12;
                bool in = false;
                for (int k : { 0, 2, 4 }) in = in || rel == sc[(d + k) % 7] || (! tono.major && d == 4 && k == 2 && rel == 11);
                todo += n.len; dentro += in ? n.len : 0.0;
            }
            if (todo <= 0) continue;
            ++compases;
            sostiene += dentro >= todo * 0.5 - 1e-9 ? 1 : 0;
        }
        check (sostiene == compases, String (c.nombre) + ": la progresión " + String (prog[0]) + "-" + String (prog[1]) + "-" + String (prog[2]) + "-"
               + String (prog[3]) + " lleva la mitad o más de lo cantado en " + String (sostiene) + " de " + String (compases) + " compases");
    }

    // the beatbox: boots and cats, and a trap one with hats in sixteenths
    {
        std::vector<Escrito> boots;
        for (int bar = 0; bar < 2; ++bar)
            for (int b = 0; b < 4; ++b)
            {
                boots.push_back ({ bar * 4.0 + b, b % 2 == 0 ? 0 : 1 });
                boots.push_back ({ bar * 4.0 + b + 0.5, 2 });
            }
        boots.push_back ({ 6.75, 0 });
        std::vector<Escrito> trap;
        // one mouth makes one sound at a time: the hats are where the kick and the snare are not
        for (int i = 0; i < 16; ++i) if ((i % 2 == 0 || i == 13 || i == 15) && i != 0 && i != 6 && i != 8 && i != 11) trap.push_back ({ i * 0.25, 2 });
        for (double k : { 0.0, 1.5, 2.75 }) trap.push_back ({ k, 0 });
        trap.push_back ({ 2.0, 1 });
        for (auto& [nombre, hits, bpm, bars] : std::vector<std::tuple<const char*, std::vector<Escrito>, double, double>> {
                 { "boots and cats a 120 BPM", boots, 120.0, 2.0 }, { "trap a 140 BPM con charles en dieciseisavos", trap, 140.0, 1.0 } })
        {
            const auto audio = beatboxear (hits, bpm, sr, 21, bars);
            const auto found = tarareo::beatbox (audio, sr, bpm);
            int lugar = 0, tipo = 0;
            for (auto& h : hits)
                for (auto& f : found)
                    if (std::abs (f.beat - h.beat) < 1e-6) { ++lugar; tipo += f.tipo == h.tipo ? 1 : 0; break; }
            String desc;
            for (auto& f : found) desc << String (f.beat, 2) << (f.tipo == 0 ? "B " : f.tipo == 1 ? "K " : "t ");
            std::cout << "    " << nombre << ": " << desc << std::endl;
            check (lugar >= (int) hits.size() - 1 && (int) found.size() <= (int) hits.size() + 1,
                   String (nombre) + ": " + String (lugar) + " de " + String ((int) hits.size()) + " golpes en su dieciseisavo (" + String ((int) found.size()) + " hallados)");
            check (tipo >= (int) hits.size() - 1, String (nombre) + ": " + String (tipo) + " de " + String ((int) hits.size()) + " reconocidos (bombo, caja, charles)");
        }
    }

    // the block: a trap song from the A minor hum and the boots-and-cats beatbox
    if (auto* g = findGenre ("trap"))
    {
        auto melodia = cantar (casos[0].notas, g->bpm, sr, 31);
        std::vector<Escrito> bb;
        for (int b = 0; b < 4; ++b) { bb.push_back ({ (double) b, b % 2 == 0 ? 0 : 1 }); bb.push_back ({ b + 0.5, 2 }); }
        auto beat = beatboxear (bb, g->bpm, sr, 32, 1.0);
        s.newProject (false);
        const auto r = s.buildFromHum (*g, &melodia, &beat, sr);
        std::cout << "    " << r.texto << std::endl;
        check (r.ok && r.key == 9 && ! r.major, "el bloque de trap queda en La menor, la tonalidad del tarareo: " + keyName (r.key, r.major));
        auto& e = s.getEdit();
        check (e.state["racoonKeyRoot"].toString() == "9", "el proyecto guarda la tonalidad del tarareo");
        te::MidiClip* hook = nullptr; te::MidiClip* groove = nullptr; te::MidiClip* kick = nullptr; te::MidiClip* voz = nullptr;
        for (auto* t : te::getAudioTracks (e))
            for (auto* c : t->getClips())
                if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                {
                    if (t->getName().startsWith (String::fromUTF8 ("Squirrel"))) hook = mc;
                    if (t->getName().startsWith (String::fromUTF8 ("Hamster · Groove"))) groove = mc;
                    if (t->getName().startsWith (String::fromUTF8 ("Hamster · Kick"))) kick = mc;
                    if (t->getName().startsWith (String::fromUTF8 ("Chinchilla"))) voz = mc;
                }
        // Squirrel plays the hum from bar 5: the first notes of bar 5 are the first notes hummed
        int igual = 0, total = 0;
        if (hook != nullptr)
            for (auto& n : casos[0].notas)
            {
                ++total;
                for (auto* m : hook->getSequence().getNotes())
                    if (std::abs (m->getStartBeat().inBeats() - (16.0 + n.beat)) <= 0.25 && m->getNoteNumber() % 12 == n.pitch % 12) { ++igual; break; }
            }
        check (hook != nullptr && igual >= total * 85 / 100, "Squirrel toca la melodía tarareada desde el compás 5 (" + String (igual) + " de " + String (total) + " notas)");
        check (voz == nullptr || voz->getSequence().getNumNotes() > 0, "Chinchilla canta la melodía en la última frase");
        int kicks = 0, hats = 0, snares = 0;
        if (kick != nullptr) kicks = kick->getSequence().getNumNotes();
        if (groove != nullptr)
            for (auto* m : groove->getSequence().getNotes())
            {
                hats += m->getNoteNumber() == 42 ? 1 : 0;
                snares += (m->getNoteNumber() == 38 || m->getNoteNumber() == 39) && m->getStartBeat().inBeats() < 60.0 ? 1 : 0;
            }
        // 16 bars: kicks in 12 of them (the break drops them), 2 a bar; hats 4 a bar; snares 2 a bar from bar 3
        check (kicks == 24, "Hamster toca los bombos del beatbox (" + String (kicks) + ", 24 esperados)");
        check (hats == 64, "y sus charles (" + String (hats) + ", 64 esperados)");
        check (snares == 26, "y sus cajas desde el compás 3 (" + String (snares) + ", 26 esperadas)");
        // and the block sounds
        ExportOptions o;
        o.destination = out.getChildFile ("tarareo trap.wav");
        o.sampleRate = 48000.0; o.loopOnly = true; o.tailSeconds = 0.5;
        s.exportMix (o);
        AudioBuffer<float> b; double bsr = 48000.0;
        const bool okRead = audio::readFile (s.engine, o.destination, b, bsr);
        check (okRead && b.getMagnitude (0, b.getNumSamples()) > 0.1f, "la rola del tarareo suena (" + String (b.getMagnitude (0, 0, jmax (1, b.getNumSamples())), 2) + " de pico)");
    }
    // only a beatbox: the genre keeps its key and its melody
    if (auto* g = findGenre ("techno"))
    {
        std::vector<Escrito> bb { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 1, 1 }, { 3, 1 }, { 0.5, 2 }, { 1.5, 2 }, { 2.5, 2 }, { 3.5, 2 } };
        auto beat = beatboxear (bb, g->bpm, sr, 41, 1.0);
        s.newProject (false);
        const auto r = s.buildFromHum (*g, nullptr, &beat, sr);
        check (r.ok && r.notas == 0 && r.key == g->key && r.major == g->major, "solo con beatbox, el bloque queda en la tonalidad del género");
    }
    s.newProject (false);
    MessageManager::getInstance()->runDispatchLoopUntil (6000);
    return humFailures;
}

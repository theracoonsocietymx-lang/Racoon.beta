// Otter checks (included by Tests/FamilyTest.cpp)
#include "OtterProcessor.h"

namespace otter_test
{
using namespace juce;

// the pitch near `approx` Hz: the peak of a Hann-windowed single-frequency probe, found on a fine grid of cents
inline double pitchNear (const AudioBuffer<float>& b, int start, int len, double approx, double sr)
{
    auto at = [&] (double cents) { return (double) tu::tone (b, 0, start, len, approx * std::pow (2.0, cents / 1200.0), sr); };
    double best = 0.0, bestV = -1.0;
    for (double c = -100.0; c <= 100.0; c += 4.0) { const double v = at (c); if (v > bestV) { bestV = v; best = c; } }
    double centre = best; bestV = -1.0;
    for (double c = centre - 5.0; c <= centre + 5.0; c += 0.5) { const double v = at (c); if (v > bestV) { bestV = v; best = c; } }
    const double ym = at (best - 0.5), y0 = bestV, yp = at (best + 0.5);
    const double den = ym - 2.0 * y0 + yp;
    const double off = std::abs (den) > 1.0e-30 ? 0.5 * (ym - yp) / den * 0.5 : 0.0;
    return approx * std::pow (2.0, (best + off) / 1200.0);
}

inline double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
inline double hz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }

inline double centroid (const AudioBuffer<float>& o, int start, double sr, int order = 15)
{
    auto sp = tu::spectrum (o, 0, start, order);
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < sp.size(); ++i) { const double f = (double) i * sr / (double) (1 << order); num += f * sp[i]; den += sp[i]; }
    return den > 0.0 ? num / den : 0.0;
}
}

static void testOtter (const juce::File& out)
{
    using namespace juce;
    using namespace otter_test;
    using racoon::otter::Processor;
    namespace ot = racoon::otter;
    commonChecks ("Otter", [] { return new Processor(); }, out);
    std::cout << "cuerdas y barras" << std::endl;

    // no colour: no body, no darkening, no room, no sympathetic or second string, no tension or stiffness
    auto plain = [] (Processor& p)
    {
        p.setRealValue ("cuerpo", 0); p.setRealValue ("oscuro", 0); p.setRealValue ("espacio", 0); p.setRealValue ("simp", 0); p.setRealValue ("doble", 0);
        p.setRealValue ("tension", 0); p.setRealValue ("rigidez", 0); p.setRealValue ("vibrato", 0); p.setRealValue ("velamp", 0);
        for (int k = 0; k < ot::numSlots; ++k) p.setRealValue (Processor::slotId (k, "amt"), 0);
        p.setRealValue ("adsr_a", 0.5f); p.setRealValue ("adsr_d", 100); p.setRealValue ("adsr_s", 100); p.setRealValue ("adsr_r", 500);
    };
    auto run = [] (double sr, std::function<void (Processor&)> setup, MidiBuffer& midi, int len, Processor* keep = nullptr)
    {
        std::unique_ptr<Processor> own;
        Processor* p = keep;
        if (p == nullptr) { own = std::make_unique<Processor>(); p = own.get(); }
        p->setRateAndBufferSizeDetails (sr, 256);
        p->prepareToPlay (sr, 256);
        setup (*p);
        AudioBuffer<float> in (2, len); in.clear();
        return tu::render (*p, in, 256, &midi);
    };
    auto note = [] (int n, int vel = 100, int at = 0, int off = -1)
    {
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, n, (uint8) vel), at);
        if (off > 0) m.addEvent (MidiMessage::noteOff (1, n), off);
        return m;
    };

    // tuning within 2 cents: plucked and bowed string, struck bar, at 44.1, 48 and 96 kHz
    {
        double worst = 0.0; String where;
        struct Case { const char* name; int model, exc; double from, len; };
        const Case cases[] { { "cuerda pulsada", 0, 0, 0.3, 0.5 }, { "cuerda frotada", 0, 2, 0.6, 0.8 }, { "marimba", 1, 1, 0.05, 0.35 } };
        for (auto& c : cases)
            for (double sr : { 44100.0, 48000.0, 96000.0 })
                for (int nt : { 28, 36, 48, 60, 72 })
                {
                    if (c.exc == 2 && nt > 60) continue;
                    auto m = note (nt, 100);
                    auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("modelo", (float) c.model); p.setRealValue ("exc", (float) c.exc);
                                                           p.setRealValue ("modos", 1); p.setRealValue ("posicion", 14); p.setRealValue ("escucha", c.model == 1 ? 4.0f : 23.0f); p.setRealValue ("caida", 8); },
                                  m, (int) (sr * (c.from + c.len + 0.1)));
                    const double f = pitchNear (o, (int) (sr * c.from), (int) (sr * c.len), hz (nt), sr);
                    const double e = std::abs (cents (f, hz (nt)));
                    if (e > worst) { worst = e; where = String (c.name) + " nota " + String (nt) + " a " + String (sr / 1000.0, 1) + " kHz"; }
                }
        check (worst < 2.0, "afinación: peor caso "_u + String (worst, 2) + " cents (" + where + ")");
    }

    // the bar's first overtones sit on the table's ratios
    {
        String bad; double worst = 0.0;
        for (int table : { 0, 1 })
        {
            auto m = note (48, 110);
            const double sr = 48000.0;
            auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("modelo", 1); p.setRealValue ("exc", 1); p.setRealValue ("modos", (float) table);
                                                   p.setRealValue ("posicion", 3); p.setRealValue ("escucha", 2); p.setRealValue ("dureza", 90); p.setRealValue ("caida", 6);
                                                   p.setRealValue ("agudos", 0); }, m, (int) (sr * 1.2));
            const auto& T = ot::modeTable (table);
            for (int k = 0; k < 4; ++k)
            {
                const double want = hz (48) * T.ratios[k];
                const double f = pitchNear (o, 2400, 48000, want, sr);
                const double err = std::abs (f / want - 1.0) * 100.0;
                if (err > worst) { worst = err; bad = String::fromUTF8 (T.name) + " modo " + String (k + 1); }
            }
        }
        check (worst < 0.5, "modos de la barra en su lugar (peor " + String (worst, 3) + " %, " + bad + ")");
    }

    // decay: Caída 4 s gives a T60 of 4 s on the fundamental, the same at three rates
    auto t60 = [&] (const AudioBuffer<float>& o, double f, double sr, double from, double to)
    {
        std::vector<double> xs, ys;
        for (double t = from; t < to; t += 0.1)
        {
            const float a = tu::tone (o, 0, (int) (t * sr), (int) (0.1 * sr), f, sr);
            xs.push_back (t); ys.push_back (20.0 * std::log10 (a + 1.0e-12));
        }
        double mx = 0, my = 0; for (size_t i = 0; i < xs.size(); ++i) { mx += xs[i]; my += ys[i]; }
        mx /= (double) xs.size(); my /= (double) ys.size();
        double sxy = 0, sxx = 0; for (size_t i = 0; i < xs.size(); ++i) { sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) * (xs[i] - mx); }
        const double slope = sxy / sxx;
        return slope < 0.0 ? -60.0 / slope : 1000.0;
    };
    {
        double t[3]; int i = 0;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            auto m = note (40, 100);
            auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("caida", 4); }, m, (int) (sr * 3.2));
            t[i++] = t60 (o, hz (40), sr, 0.5, 3.0);
        }
        const double spread = (jmax (t[0], t[1], t[2]) - jmin (t[0], t[1], t[2])) / t[1];
        check (std::abs (t[1] - 4.0) < 0.6 && spread < 0.05, "Caída 4 s: el grave se apaga en " + String (t[0], 2) + " / " + String (t[1], 2) + " / " + String (t[2], 2) + " s (44.1 / 48 / 96 kHz)");
    }

    // highs die faster with Caída de agudos
    {
        auto ratioFor = [&] (float ag)
        {
            const double sr = 48000.0;
            auto m = note (40, 110);
            auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("caida", 4); p.setRealValue ("agudos", ag); p.setRealValue ("posicion", 3); p.setRealValue ("dureza", 80); },
                          m, (int) (sr * 2.2));
            return t60 (o, hz (40), sr, 0.2, 2.0) / t60 (o, hz (40) * 8.0, sr, 0.2, 1.0);
        };
        const double r80 = ratioFor (80), r0 = ratioFor (0);
        check (r80 >= 3.0 && r0 < 1.5, "Caída de agudos al 80 %: el armónico 8 dura "_u + String (r80, 1) + " veces menos; en 0, " + String (r0, 2));
    }

    // plucked in the middle: no even harmonics
    {
        const double sr = 48000.0;
        auto m = note (45, 100);
        auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("posicion", 50); p.setRealValue ("escucha", 30); p.setRealValue ("dureza", 70); }, m, (int) (sr * 0.4));
        const double f = hz (45);
        auto lv = [&] (int h) { return 20.0 * std::log10 (tu::tone (o, 0, 0, (int) (sr * 0.3), f * h, sr) + 1.0e-12); };
        const double h1 = lv (1), h2 = lv (2), h3 = lv (3), h4 = lv (4);
        const double gap = jmin (h1, h3) - jmax (h2, h4);
        check (gap >= 20.0, "Posición al centro: los armónicos pares quedan "_u + String (gap, 1) + " dB abajo");
    }

    // playing harder opens the sound, plucked and struck
    {
        String msg; bool ok = true;
        for (int exc : { 0, 1 })
        {
            const double sr = 48000.0;
            auto c = [&] (int vel)
            {
                auto m = note (45, vel);
                auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("exc", (float) exc); p.setRealValue ("velbrillo", 60); }, m, 40000);
                return centroid (o, 0, sr, 13);      // the attack: the first 170 ms
            };
            const double lo = c (40), hi = c (127);
            ok = ok && hi >= lo * 1.3;
            msg << (exc == 0 ? "pulsar " : ", golpear ") << String (lo, 0) << " a " << String (hi, 0) << " Hz";
        }
        check (ok, "Velocidad al brillo: centroide " + msg);
    }

    // a bowed note holds steady and stops when let go
    {
        const double sr = 48000.0;
        Processor p;
        auto m = note (36, 100, 0, (int) (sr * 6.0));
        auto o = run (sr, [&] (Processor& q) { plain (q); q.setRealValue ("exc", 2); q.setRealValue ("adsr_r", 300); q.setRealValue ("adsr_a", 100); }, m, (int) (sr * 6.6), &p);
        const float l1 = tu::rms (o, (int) sr, (int) (sr * 0.5)), l6 = tu::rms (o, (int) (sr * 5.4), (int) (sr * 0.5));
        float lo = 1.0e9f, hi = 0.0f;
        for (double t = 1.0; t < 5.9; t += 0.5) { const float r = tu::rms (o, (int) (t * sr), (int) (sr * 0.1)); lo = jmin (lo, r); hi = jmax (hi, r); }
        const double pA = pitchNear (o, (int) sr, (int) sr, hz (36), sr), pB = pitchNear (o, (int) (sr * 4.8), (int) sr, hz (36), sr);
        const float tail = tu::rms (o, (int) (sr * 6.5), (int) (sr * 0.05));
        const double spanDb = Decibels::gainToDecibels (hi / jmax (1.0e-9f, lo));
        check (spanDb < 3.0 && std::abs (cents (pB, pA)) < 5.0 && tail < l1 * 0.001f && p.activeVoices.load() == 0 && l6 > 0.0f,
               "arco sostenido: el nivel varía "_u + String (spanDb, 1) + " dB en 5 s, la nota se mueve " + String (cents (pB, pA), 1)
               + " cents, al soltar baja " + String (Decibels::gainToDecibels (tail / jmax (1.0e-9f, l1) + 1.0e-12f), 0) + " dB en 0.5 s");
    }

    // a magnet sustains forever without running away
    {
        const double sr = 48000.0;
        auto m = note (36, 100);
        auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("exc", 4); p.setRealValue ("arco", 100); p.setRealValue ("dureza", 100); }, m, (int) (sr * 20.0));
        const float a4 = tu::rms (o, (int) (sr * 4.0), (int) (sr * 0.5)), a19 = tu::rms (o, (int) (sr * 19.0), (int) (sr * 0.5));
        const double d = Decibels::gainToDecibels (a19 / jmax (1.0e-9f, a4));
        const float pk = tu::peak (o, 0, o.getNumSamples());
        check (tu::allFinite (o) && pk < 2.0f && std::abs (d) < 1.0 && a4 > 1.0e-3f,
               "Imán al máximo, 20 s: pico "_u + String (pk, 2) + ", de 4 a 19 s cambia " + String (d, 2) + " dB");
    }

    // the bow at its extremes stays finite, and at the middle it catches the note
    {
        const double sr = 48000.0;
        bool ok = true; float pk = 0.0f;
        for (float pr : { 0.0f, 100.0f }) for (float ar : { 0.0f, 100.0f })
        {
            auto m = note (40, 100);
            auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("exc", 2); p.setRealValue ("presion", pr); p.setRealValue ("arco", ar); }, m, (int) (sr * 3.0));
            ok = ok && tu::allFinite (o);
            pk = jmax (pk, tu::peak (o, 0, o.getNumSamples()));
        }
        auto m = note (40, 100);
        auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("exc", 2); }, m, (int) (sr * 2.0));
        const float f1 = tu::tone (o, 0, (int) sr, (int) (sr * 0.8), hz (40), sr);
        float other = 0.0f;
        for (double f = hz (40) * 0.55; f < hz (40) * 0.95; f += 3.0) other = jmax (other, tu::tone (o, 0, (int) sr, (int) (sr * 0.8), f, sr));
        check (ok && pk < 4.0f && f1 > other * 10.0f, "arco con presión y velocidad extremas: estable (pico "_u + String (pk, 2) + "); al centro agarra la nota");
    }

    // voices: stealing without clicks, and every voice free after the release
    {
        const double sr = 48000.0;
        auto chord = [&] (int voices, float& jump, int& left)
        {
            Processor p;
            MidiBuffer m;
            for (int k = 0; k < 16; ++k) { m.addEvent (MidiMessage::noteOn (1, 36 + k * 2, (uint8) 90), k * 2400); m.addEvent (MidiMessage::noteOff (1, 36 + k * 2), (int) (sr * 1.0)); }
            auto o = run (sr, [&] (Processor& q) { plain (q); q.setRealValue ("adsr_r", 3000); q.setRealValue ("voices", (float) voices); q.setRealValue ("caida", 2); }, m, (int) (sr * 9.0), &p);
            AudioBuffer<float> head (2, (int) sr);
            for (int c = 0; c < 2; ++c) head.copyFrom (c, 0, o, c, 0, (int) sr);
            jump = tu::maxJump (head);
            left = p.activeVoices.load();
        };
        float j15, j16; int l15, l16;
        chord (15, j15, l15); chord (16, j16, l16);
        check (j15 <= j16 * 1.05f && l15 == 0 && l16 == 0, "16 notas en 15 voces: el robo no hace clic (salto "_u + String (j15, 4) + " contra " + String (j16, 4) + "), y todas se liberan");
    }

    // the same note again goes into the ringing string, it does not cut it
    {
        const double sr = 48000.0;
        MidiBuffer m;
        for (int k = 0; k < 3; ++k) { m.addEvent (MidiMessage::noteOn (1, 45, (uint8) 100), (int) (k * 0.2 * sr)); m.addEvent (MidiMessage::noteOff (1, 45), (int) (k * 0.2 * sr) + 4000); }
        auto o = run (sr, [&] (Processor& p) { plain (p); }, m, (int) (sr * 1.0));
        AudioBuffer<float> first (2, (int) (0.2 * sr));
        for (int c = 0; c < 2; ++c) first.copyFrom (c, 0, o, c, 0, (int) (0.2 * sr));
        check (tu::maxJump (o) <= tu::maxJump (first) * 1.5f, "la misma nota otra vez se vuelve a tocar sin cortar (salto "_u + String (tu::maxJump (o), 4) + " contra " + String (tu::maxJump (first), 4) + ")");
    }

    // sympathetic strings keep ringing after the note
    {
        const double sr = 48000.0;
        auto ring = [&] (float simp)
        {
            auto m = note (45, 110, 0, (int) (sr * 0.3));
            auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("adsr_r", 50); p.setRealValue ("simp", simp); p.setRealValue ("simpraiz", 9); p.setRealValue ("simpmodo", 1); },
                          m, (int) (sr * 1.6));
            const float ref = tu::tone (o, 0, 0, (int) (sr * 0.25), 110.0, sr);
            const float a = tu::tone (o, 0, (int) (sr * 1.3), (int) (sr * 0.25), 110.0, sr), b = tu::tone (o, 0, (int) (sr * 1.3), (int) (sr * 0.25), 220.0, sr);
            return std::pair<double, double> (Decibels::gainToDecibels (a / ref + 1.0e-12f), Decibels::gainToDecibels (b / ref + 1.0e-12f));
        };
        const auto on = ring (100), off = ring (0);
        check (on.first > -40.0 && on.second > -40.0 && off.first < -80.0, "cuerdas simpáticas: 1 s después siguen a "_u + String (on.first, 1) + " / " + String (on.second, 1) + " dB (sin ellas "
               + String (off.first, 1) + ")");
    }

    // silence costs nothing and is exactly zero
    {
        const double sr = 48000.0;
        Processor p;
        p.setRateAndBufferSizeDetails (sr, 256);
        p.prepareToPlay (sr, 256);
        plain (p);
        p.setRealValue ("caida", 1.0f);
        AudioBuffer<float> io (2, 256);
        float last = 0.0f;
        for (int b = 0; b < (int) (sr * 32.0 / 256.0); ++b)
        {
            io.clear();
            MidiBuffer m;
            if (b % 40 == 0 && b < (int) (sr * 2.0 / 256.0)) { m.addEvent (MidiMessage::noteOn (1, 40 + b % 20, (uint8) 100), 0); m.addEvent (MidiMessage::noteOff (1, 40 + b % 20), 100); }
            p.processBlock (io, m);
            if (b > (int) (sr * 22.0 / 256.0)) last = jmax (last, io.getMagnitude (0, 256), io.getMagnitude (1, 256));
        }
        check (last == 0.0f && p.activeVoices.load() == 0, "después de las notas el silencio es cero exacto y no quedan voces");
    }

    // every preset is dark: on Do 3 the centroid stays under 1500 Hz (2000 with the metal tank), Oscuridad at 30 % or more
    {
        Processor probe;
        const auto presets = probe.factoryPresets();
        String bright, bad; double worst = 0.0;
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            Processor p;
            p.setRateAndBufferSizeDetails (48000.0, 256);
            p.prepareToPlay (48000.0, 256);
            p.applyFactoryPreset (i);
            auto m = note (48, 100, 0, 48000);
            AudioBuffer<float> in (2, 48000 + 32768); in.clear();
            auto o = tu::render (p, in, 256, &m);
            const double c = centroid (o, 2000, 48000.0);
            const bool metal = (int) p.value ("cuerpo") == 6;
            if (c / (metal ? 2000.0 : 1500.0) > worst) { worst = c / (metal ? 2000.0 : 1500.0); bright = presets[(size_t) i].name + " " + String (c, 0) + " Hz"; }
            if (! tu::allFinite (o) || tu::peak (o, 0, o.getNumSamples()) > 1.5f || tu::rms (o, 0, 48000) < 1.0e-4f || p.value ("oscuro") < 30.0f) bad << presets[(size_t) i].name << " ";
        }
        check (worst < 1.0 && bad.isEmpty(), String ((int) presets.size()) + " presets oscuros y estables (el más brillante: " + bright + ")" + (bad.isEmpty() ? String() : " fallan: " + bad));
    }

    // cost
    {
        const double sr = 48000.0;
        auto cost = [&] (int preset)
        {
            Processor p;
            p.setRateAndBufferSizeDetails (sr, 256);
            p.prepareToPlay (sr, 256);
            p.applyFactoryPreset (preset);
            p.setRealValue ("voices", 16);
            MidiBuffer m;
            for (int k = 0; k < 16; ++k) m.addEvent (MidiMessage::noteOn (1, 33 + k * 2, (uint8) 100), k * 32);
            AudioBuffer<float> in (2, (int) (sr * 5.0)); in.clear();
            const auto t0 = Time::getMillisecondCounterHiRes();
            tu::render (p, in, 256, &m);
            return (Time::getMillisecondCounterHiRes() - t0) * 0.001 / 5.0 * 100.0;
        };
        const double bowed = cost (1), struck = cost (10);
        check (bowed < 25.0 && struck < 15.0, "16 voces: arco con cuerpo y sala " + String (roundToInt (bowed)) + " % de un núcleo, marimba "_u + String (roundToInt (struck)) + " %");
    }

    // pitch bend moves a bowed note by two semitones without a jump
    {
        const double sr = 48000.0;
        MidiBuffer m;
        m.addEvent (MidiMessage::noteOn (1, 40, (uint8) 100), 0);
        m.addEvent (MidiMessage::pitchWheel (1, 16383), (int) (sr * 1.5));
        auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("exc", 2); }, m, (int) (sr * 2.6));
        const double before = pitchNear (o, (int) (sr * 0.6), (int) (sr * 0.8), hz (40), sr), after = pitchNear (o, (int) (sr * 1.7), (int) (sr * 0.8), hz (42), sr);
        AudioBuffer<float> steady (2, (int) sr), around (2, (int) (sr * 0.2));
        for (int c = 0; c < 2; ++c) { steady.copyFrom (c, 0, o, c, (int) (sr * 0.4), (int) sr); around.copyFrom (c, 0, o, c, (int) (sr * 1.45), (int) (sr * 0.2)); }
        const double moved = cents (after, before);
        check (std::abs (moved - 200.0) < 3.0 && tu::maxJump (around) <= tu::maxJump (steady) * 1.3f,
               "pitch bend +2 en la nota frotada: sube " + String (moved, 1) + " cents sin saltos");
    }

    // string + bar: both ring
    {
        const double sr = 48000.0;
        auto m = note (45, 110);
        auto o = run (sr, [&] (Processor& p) { plain (p); p.setRealValue ("modelo", 2); p.setRealValue ("modos", 1); p.setRealValue ("dureza", 60); }, m, (int) (sr * 1.0));
        const double f = hz (45);
        const float bar2 = tu::tone (o, 0, 2000, (int) (sr * 0.5), f * 3.99, sr), str2 = tu::tone (o, 0, 2000, (int) (sr * 0.5), f * 2.0, sr);
        const float gap = tu::tone (o, 0, 2000, (int) (sr * 0.5), f * 3.5, sr);
        check (bar2 > gap * 3.0f && str2 > gap * 3.0f, "Cuerda + barra: suenan la barra (modo 2 en 3.99) y la cuerda (armónico 2)");
    }

    // Aleatoria follows how it is played: plucked writes arpeggios, bowed writes pads
    {
        Processor p;
        p.setRealValue ("exc", 0);
        const auto a = p.aleatoriaRol();
        p.setRealValue ("exc", 2);
        const auto b = p.aleatoriaRol();
        p.setRealValue ("exc", 3);
        const auto c = p.aleatoriaRol();
        check (a == "arpegio" && b == "pad" && c == "textura", "Aleatoria: pulsada escribe " + a + ", frotada " + b + ", raspada " + c);
    }
}

static RegisterTest regOtter ("Otter", testOtter);


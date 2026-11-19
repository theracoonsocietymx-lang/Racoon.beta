#include "Tarareo.h"
#include "I18n.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace racoon::tarareo
{
using namespace juce;

juce::String nombreNota (int p)
{
    static const char* es[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    static const char* en[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const auto* n = i18n::current() == i18n::Lang::en ? en : es;
    return String (n[((p % 12) + 12) % 12]) + String (p / 12 - 1);
}

std::vector<float> seguirTono (const float* x, int n, double sr, int hop, double minHz, double maxHz)
{
    // YIN (de Cheveigné and Kawahara, 2002): the difference function, its cumulative mean normalization, the first dip
    // under the threshold, and a parabola through it for the period between samples
    const int maxLag = (int) (sr / minHz), minLag = jmax (2, (int) (sr / maxHz));
    const int win = maxLag;
    std::vector<float> out;
    std::vector<double> d ((size_t) maxLag + 2), dn ((size_t) maxLag + 2);
    for (int start = 0; start + win + maxLag < n; start += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < win; ++i) energy += (double) x[start + i] * x[start + i];
        if (energy / win < 1.0e-7) { out.push_back (0.0f); continue; }     // silence (-70 dB)
        d[0] = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double s = 0.0;
            const float* a = x + start;
            const float* b = x + start + tau;
            for (int i = 0; i < win; ++i) { const double v = (double) a[i] - b[i]; s += v * v; }
            d[(size_t) tau] = s;
        }
        dn[0] = 1.0;
        double run = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau) { run += d[(size_t) tau]; dn[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0; }
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
            if (dn[(size_t) tau] < 0.15)
            {
                while (tau + 1 < maxLag && dn[(size_t) tau + 1] < dn[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            // no dip under the threshold: the global minimum, only when it is clearly periodic
            double m = 1e9;
            for (int tau = minLag; tau < maxLag; ++tau) if (dn[(size_t) tau] < m) { m = dn[(size_t) tau]; best = tau; }
            if (m > 0.3) { out.push_back (0.0f); continue; }
        }
        double period = best;
        if (best > minLag && best < maxLag - 1)
        {
            const double a = dn[(size_t) best - 1], b = dn[(size_t) best], c = dn[(size_t) best + 1];
            const double den = a - 2.0 * b + c;
            if (std::abs (den) > 1e-12) period = best + 0.5 * (a - c) / den;
        }
        out.push_back ((float) (sr / period));
    }
    return out;
}

namespace
{
std::vector<float> mono (const AudioBuffer<float>& a)
{
    std::vector<float> m ((size_t) a.getNumSamples(), 0.0f);
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i) m[(size_t) i] += a.getSample (c, i) / (float) a.getNumChannels();
    return m;
}

double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}
}

std::vector<Nota> transcribir (const AudioBuffer<float>& audio, double sr, double bpm)
{
    auto x = mono (audio);
    const int hop = (int) (sr * 0.01);
    const auto f0 = seguirTono (x.data(), (int) x.size(), sr, hop);
    const int frames = (int) f0.size();
    // the loudness of each frame, to find new attacks on the same pitch
    std::vector<double> rms ((size_t) frames, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        const int a = f * hop, b = jmin ((int) x.size(), a + hop * 2);
        for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
        rms[(size_t) f] = std::sqrt (s / jmax (1, b - a));
    }
    double loud = 0.0;
    for (auto r : rms) loud = jmax (loud, r);
    // pitch in semitones, smoothed with a median of five frames (one wrong frame does not break a note)
    std::vector<double> semi ((size_t) frames, -1.0);
    for (int f = 0; f < frames; ++f)
        if (f0[(size_t) f] > 0.0f && rms[(size_t) f] > loud * 0.06) semi[(size_t) f] = 69.0 + 12.0 * std::log2 (f0[(size_t) f] / 440.0);
    // nobody hums on 440: the whole take's tuning is measured (the mean of each frame's distance to its nearest
    // semitone, taken on a circle) and taken out, so a singer 40 cents flat lands on the notes meant
    {
        double cs = 0.0, sn = 0.0;
        for (auto v : semi) if (v >= 0.0) { const double a = MathConstants<double>::twoPi * (v - std::round (v)); cs += std::cos (a); sn += std::sin (a); }
        if (cs != 0.0 || sn != 0.0)
        {
            const double offset = std::atan2 (sn, cs) / MathConstants<double>::twoPi;    // -0.5 .. 0.5 semitones
            for (auto& v : semi) if (v >= 0.0) v -= offset;
        }
    }
    std::vector<double> sm = semi;
    for (int f = 2; f + 2 < frames; ++f)
    {
        std::vector<double> w;
        for (int k = -2; k <= 2; ++k) if (semi[(size_t) (f + k)] >= 0.0) w.push_back (semi[(size_t) (f + k)]);
        sm[(size_t) f] = semi[(size_t) f] >= 0.0 && w.size() >= 3 ? median (w) : semi[(size_t) f];
    }
    // notes: a run of voiced frames; it is cut when the pitch moves away (more than 0.7 semitones for 3 frames) or the
    // voice dips and comes back (a new syllable on the same note)
    std::vector<Nota> notes;
    const double beatsPerSecond = bpm / 60.0;
    int f = 0, lastEnd = 0;
    while (f < frames)
    {
        if (sm[(size_t) f] < 0.0) { ++f; continue; }
        const int start = f;
        std::vector<double> run;
        double ref = sm[(size_t) f];
        int away = 0;
        double peak = rms[(size_t) f];
        while (f < frames && sm[(size_t) f] >= 0.0)
        {
            const double p = sm[(size_t) f];
            if (std::abs (p - ref) > 0.7) { if (++away >= 3) { f -= 2; break; } }
            else away = 0;
            // a dip of 8 dB and back up: a new attack
            if (f > start + 6 && rms[(size_t) f] > rms[(size_t) f - 2] * 2.0 && rms[(size_t) f - 2] < peak * 0.4) break;
            peak = jmax (peak, rms[(size_t) f]);
            run.push_back (p);
            if (run.size() >= 4) ref = median (run);
            ++f;
        }
        if (f < frames && sm[(size_t) f] < 0.0) {} // the run ended in silence
        const int len = f - start;
        if (len >= 6)                                          // 60 ms: shorter is a slide or a breath
        {
            Nota n;
            // the pitch where the note settles: the slide into it (the first third) does not count
            const std::vector<double> settled (run.begin() + (long) (run.size() / 3), run.end());
            n.pitch = (int) std::lround (median (settled.size() >= 3 ? settled : run));
            // the attack starts before the pitch is clear: back from the first voiced frame while the sound keeps
            // growing (up to 80 ms, never into the note before)
            int onset = start;
            while (onset > jmax (lastEnd, start - 8) && rms[(size_t) onset - 1] > loud * 0.015 && rms[(size_t) onset - 1] < rms[(size_t) onset] * 1.02) --onset;
            const double t0 = onset * 0.01, t1 = (start + len) * 0.01;
            n.beat = jmax (0.0, std::round (t0 * beatsPerSecond * 4.0) / 4.0);                 // sixteenths
            n.len = jmax (0.25, std::round ((t1 - t0) * beatsPerSecond * 4.0) / 4.0);
            n.vel = (float) jlimit (0.45, 1.0, 0.45 + 0.55 * peak / jmax (1e-9, loud));
            if (! notes.empty() && notes.back().beat >= n.beat - 1e-6)
            {
                // two notes on the same sixteenth: the longer stays
                if (n.len > notes.back().len) notes.back() = n;
            }
            else notes.push_back (n);
            lastEnd = start + len;
        }
        if (f == start) ++f;
    }
    // a note never overlaps the next one
    for (size_t i = 0; i + 1 < notes.size(); ++i)
        notes[i].len = jmax (0.25, jmin (notes[i].len, notes[i + 1].beat - notes[i].beat));
    return notes;
}

Tono detectarTono (const std::vector<Nota>& notes)
{
    static const double maj[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double w[12] {};
    for (auto& n : notes) w[((n.pitch % 12) + 12) % 12] += n.len * (0.5 + n.vel);
    auto corr = [&] (const double* prof, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += w[i]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = w[(i + root) % 12] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    Tono best;
    best.score = -2.0;
    for (int r = 0; r < 12; ++r)
    {
        // a small preference for minor: Racoon's songs (and Juglar's) live there
        const double cm = corr (mnr, r) + 0.02, cM = corr (maj, r);
        if (cm > best.score) { best = { r, false, cm }; }
        if (cM > best.score) { best = { r, true, cM }; }
    }
    return best;
}

void ajustarATono (std::vector<Nota>& notes, const Tono& t, int centro)
{
    static const int majS[7] { 0, 2, 4, 5, 7, 9, 11 }, minS[7] { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = t.major ? majS : minS;
    for (auto& n : notes)
    {
        const int rel = ((n.pitch - t.root) % 12 + 12) % 12;
        if (! t.major && rel == 11) continue;                  // the leading tone of harmonic minor
        int best = 0, bestD = 99;
        for (int k = 0; k < 7; ++k)
            for (int o : { -12, 0, 12 })
            {
                const int d = std::abs (sc[k] + o - rel);
                if (d < bestD) { bestD = d; best = sc[k] + o; }
            }
        n.pitch += best - rel;
    }
    if (notes.empty()) return;
    std::vector<double> ps;
    for (auto& n : notes) ps.push_back (n.pitch);
    const int mid = (int) median (ps);
    const int shift = (int) std::lround ((centro - mid) / 12.0) * 12;
    for (auto& n : notes) n.pitch += shift;
}

std::vector<Golpe> beatbox (const AudioBuffer<float>& audio, double sr, double bpm)
{
    auto x = mono (audio);
    const int order = 10, size = 1 << order, hop = 256;
    // two windows of silence in front: a hit right at the start is still an attack
    const int pad = size * 2;
    x.insert (x.begin(), (size_t) pad, 0.0f);
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) size * 2), prev ((size_t) size / 2 + 1, 0.0f);
    std::vector<double> flux;
    std::vector<std::array<double, 3>> bands;               // low, mid, high energy of each frame
    for (int start = 0; start + size < (int) x.size(); start += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i) buf[(size_t) i] = x[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double fl = 0.0;
        std::array<double, 3> b { 0, 0, 0 };
        for (int k = 1; k <= size / 2; ++k)
        {
            const double mag = buf[(size_t) k], hz = k * sr / size;
            fl += jmax (0.0, mag - (double) prev[(size_t) k]);
            prev[(size_t) k] = (float) mag;
            (hz < 250.0 ? b[0] : hz < 5000.0 ? b[1] : b[2]) += mag * mag;
        }
        flux.push_back (fl);
        bands.push_back (b);
    }
    // peaks of the flux above a moving threshold (the median around it plus a margin), at least 70 ms apart
    std::vector<Golpe> hits;
    double maxFlux = 0.0;
    for (auto v : flux) maxFlux = jmax (maxFlux, v);
    const int minGap = (int) (0.07 * sr / hop);
    int last = -minGap;
    for (int f = 1; f + 2 < (int) flux.size(); ++f)
    {
        std::vector<double> around;
        for (int k = jmax (0, f - 8); k <= jmin ((int) flux.size() - 1, f + 8); ++k) around.push_back (flux[(size_t) k]);
        const double thr = median (around) * 1.5 + maxFlux * 0.08;
        if (flux[(size_t) f] > thr && flux[(size_t) f] >= flux[(size_t) f - 1] && flux[(size_t) f] >= flux[(size_t) f + 1] && f - last >= minGap)
        {
            last = f;
            // what the hit is: the energy of the 60 ms after it, by band
            std::array<double, 3> e { 0, 0, 0 };
            for (int k = f; k < jmin ((int) bands.size(), f + (int) (0.06 * sr / hop)); ++k)
                for (int j = 0; j < 3; ++j) e[(size_t) j] += bands[(size_t) k][(size_t) j];
            const double tot = e[0] + e[1] + e[2] + 1e-12;
            Golpe g;
            // a hat is almost only hiss; a kick almost only low end; what is in between (a "pf", a "k") is the snare
            g.tipo = e[0] / tot > 0.5 ? 0 : e[2] / tot > 0.75 ? 2 : 1;
            if (SystemStats::getEnvironmentVariable ("RACOON_BEATBOX_DEBUG", {}).isNotEmpty())
                std::cerr << "golpe " << f << " " << e[0] / tot << " " << e[1] / tot << " " << e[2] / tot << std::endl;
            const double t = (f * hop + size * 0.25 - pad) / sr;      // the attack is near the front of the window
            g.beat = jmax (0.0, std::round (t * bpm / 60.0 * 4.0) / 4.0);
            g.vel = (float) jlimit (0.4, 1.0, 0.4 + 0.6 * flux[(size_t) f] / jmax (1e-9, maxFlux));
            bool dup = false;
            for (auto& h : hits) if (std::abs (h.beat - g.beat) < 1e-6 && h.tipo == g.tipo) dup = true;
            if (! dup) hits.push_back (g);
        }
    }
    return hits;
}

std::array<int, 4> progresion (const std::vector<Nota>& notes, const Tono& t, bool harmonic)
{
    static const int majS[7] { 0, 2, 4, 5, 7, 9, 11 }, minS[7] { 0, 2, 3, 5, 7, 8, 10 };
    const int* sc = t.major ? majS : minS;
    std::array<int, 4> out { 0, 5, 3, 4 };
    double barLen = 4.0;
    double lastBeat = 0.0;
    for (auto& n : notes) lastBeat = jmax (lastBeat, n.beat + n.len);
    const int bars = jmax (1, (int) std::ceil (lastBeat / barLen - 1e-6));
    int prev = -1;
    for (int bar = 0; bar < 4; ++bar)
    {
        const int src = bar % bars;                            // a melody of two bars fills four
        double w[12] {};
        for (auto& n : notes)
        {
            const double a = jmax (n.beat, src * barLen), b = jmin (n.beat + n.len, (src + 1) * barLen);
            if (b <= a) continue;
            const double accent = std::fmod (n.beat, 1.0) < 1e-6 ? 1.5 : 1.0;   // notes on the beat weigh more
            w[((n.pitch - t.root) % 12 + 12) % 12] += (b - a) * accent;
        }
        double best = -1e9;
        int bestDeg = 0;
        for (int deg = 0; deg < 7; ++deg)
        {
            int tones[3] { sc[deg], sc[(deg + 2) % 7], sc[(deg + 4) % 7] };
            if (! t.major && harmonic && deg == 4) tones[1] = 11;      // V major in harmonic minor
            double s = 0.0, all = 0.0;
            for (int i = 0; i < 12; ++i) all += w[i];
            for (int k = 0; k < 3; ++k) s += w[tones[k] % 12] * (k == 0 ? 1.2 : 1.0);
            s -= (all - (w[tones[0] % 12] + w[tones[1] % 12] + w[tones[2] % 12])) * 0.3;
            if (bar == 0 && deg == 0) s += 0.5;                // the song opens on its tonic when it can
            if (deg == prev) s -= 0.4;                         // and moves
            if (deg == 6) s -= 0.3;                            // the diminished chord, only if nothing else fits
            if (s > best) { best = s; bestDeg = deg; }
        }
        out[(size_t) bar] = bestDeg;
        prev = bestDeg;
    }
    return out;
}
} // namespace racoon::tarareo

#include "DJ.h"
#include "AudioTools.h"
#include "I18n.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace racoon::dj
{
using namespace juce;

namespace
{
std::vector<float> monoDe (const AudioBuffer<float>& b)
{
    std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] += x[i] / (float) b.getNumChannels();
    }
    return m;
}

// the onset strength of each frame: the growth of the log spectrum in 24 bands, summed (low bands weigh more: the kick)
std::vector<double> onsets (const std::vector<float>& x, double sr, int hop, double& frameRate, bool soloGraves = false)
{
    const int order = 11, n = 1 << order;
    frameRate = sr / hop;
    std::vector<double> out;
    if ((int) x.size() < n * 2) return out;
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2), win ((size_t) n);
    for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (n - 1));
    const int bands = 24;
    std::vector<int> bandOf ((size_t) n / 2 + 1, -1);
    for (int k = 1; k <= n / 2; ++k)
    {
        const double hz = k * sr / n;
        if (hz < 30.0 || hz > 16000.0) continue;
        bandOf[(size_t) k] = jlimit (0, bands - 1, (int) (bands * std::log (hz / 30.0) / std::log (16000.0 / 30.0)));
    }
    std::vector<double> prev ((size_t) bands, 0.0), cur ((size_t) bands);
    for (size_t pos = 0; pos + (size_t) n <= x.size(); pos += (size_t) hop)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = x[pos + (size_t) i] * win[(size_t) i];
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        std::fill (cur.begin(), cur.end(), 0.0);
        for (int k = 1; k <= n / 2; ++k) if (bandOf[(size_t) k] >= 0) cur[(size_t) bandOf[(size_t) k]] += (double) buf[(size_t) k] * buf[(size_t) k];
        double f = 0.0;
        for (int b = 0; b < (soloGraves ? 6 : bands); ++b)          // the low bands alone: 30 to 145 Hz, the kick
        {
            const double v = std::log1p (1000.0 * cur[(size_t) b]);
            f += jmax (0.0, v - prev[(size_t) b]) * (b < 6 ? 1.5 : 1.0);
            prev[(size_t) b] = v;
        }
        out.push_back (f);
    }
    // the slow level taken out (a moving mean of half a second), what is left is the attacks
    const int w = jmax (1, (int) (frameRate * 0.25));
    std::vector<double> d (out.size());
    double run = 0.0;
    std::vector<double> pre (out.size() + 1, 0.0);
    for (size_t i = 0; i < out.size(); ++i) pre[i + 1] = pre[i] + out[i];
    for (size_t i = 0; i < out.size(); ++i)
    {
        const int a = jmax (0, (int) i - w), b = jmin ((int) out.size(), (int) i + w + 1);
        run = (pre[(size_t) b] - pre[(size_t) a]) / (b - a);
        d[i] = jmax (0.0, out[i] - run);
    }
    return d;
}

// the pitch classes by harmonic salience. Per frame, the spectrum on a semitone grid, log-compressed (against the loudest
// moment of the song); each note from C2 to B6 scores itself and its 2nd to 5th harmonics (each 0.8 of the one
// before), what stands above the frame's median counts, and every frame weighs the same. Tuned on 23 pieces of known
// key (5 synthetic songs and 18 Racoon blocks): 19 exact, 20 on the key or a neighbour of the wheel
audio::Chroma chromaSaliencia (const std::vector<float>& x, double sr)
{
    audio::Chroma c {};
    const int order = sr >= 44100.0 ? 13 : 12, n = 1 << order, hop = n / 2;
    if ((int) x.size() < n) return c;
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    constexpr int lo = 36, hi = 95, top = hi + 28, width = top - lo + 1;
    std::vector<int> binLo ((size_t) width), binHi ((size_t) width);
    for (int p = lo; p <= top; ++p)
    {
        const double k = 440.0 * std::pow (2.0, (p - 69) / 12.0) * n / sr;
        binLo[(size_t) (p - lo)] = jmax (1, (int) std::floor (k * std::pow (2.0, -0.5 / 12.0)));
        binHi[(size_t) (p - lo)] = jmin (n / 2 - 1, jmax (binLo[(size_t) (p - lo)], (int) std::ceil (k * std::pow (2.0, 0.5 / 12.0))));
    }
    std::vector<float> semis;
    float loudest = 0.0f;
    for (size_t pos = 0; pos + (size_t) n <= x.size(); pos += (size_t) hop)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = x[pos + (size_t) i] * (float) (0.5 - 0.5 * std::cos (MathConstants<double>::twoPi * i / n));
        std::fill (buf.begin() + n, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int j = 0; j < width; ++j)
        {
            float m = 0.0f;
            for (int k = binLo[(size_t) j]; k <= binHi[(size_t) j]; ++k) m = jmax (m, buf[(size_t) k]);
            semis.push_back (m);
            loudest = jmax (loudest, m);
        }
    }
    if (loudest <= 0.0f) return c;
    for (auto& v : semis) v = std::log1p (v * 1000.0f / loudest);
    const int frames = (int) semis.size() / width;
    std::vector<double> sal ((size_t) (hi - lo + 1)), sorted;
    for (int f = 0; f < frames; ++f)
    {
        const float* sp = semis.data() + (size_t) f * (size_t) width;
        for (int p = 0; p <= hi - lo; ++p)
        {
            double v = 0.0, w = 1.0;
            for (int h = 1; h <= 5; ++h, w *= 0.8) v += w * sp[p + (int) std::lround (12.0 * std::log2 ((double) h))];
            sal[(size_t) p] = v;
        }
        sorted = sal;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        const double med = sorted[sorted.size() / 2];
        audio::Chroma frame {};
        double sum = 0.0;
        for (int p = 0; p <= hi - lo; ++p)
        {
            const double v = jmax (0.0, sal[(size_t) p] - med);
            frame[(size_t) ((p + lo) % 12)] += v;
            sum += v;
        }
        if (sum <= 1.0e-9) continue;
        for (int i = 0; i < 12; ++i) c[(size_t) i] += frame[(size_t) i] / sum;
    }
    return c;
}

// the key from the chroma: the profiles measured on electronic dance music (Faraldo et al., "edma", 2016), which suit
// it better than the classical ones; the confidence is how far the winner is from the runner-up
struct Llave { int root = 0; bool major = false; double confidence = 0.0; };
Llave llaveDe (const audio::Chroma& c)
{
    static const double M[12] { 0.16519551, 0.04749026, 0.08293076, 0.06687112, 0.09994645, 0.09274123, 0.05294487, 0.13159476, 0.05218986, 0.07855834, 0.05165141, 0.07803867 };
    static const double m[12] { 0.17235348, 0.04, 0.0761009, 0.12100454, 0.05341021, 0.09208589, 0.04527928, 0.13180044, 0.07806982, 0.04712092, 0.0697727, 0.06900887 };
    auto corr = [&] (const double* prof, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += c[(size_t) ((i + root) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = c[(size_t) ((i + root) % 12)] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };
    Llave best;
    double first = -2.0, second = -2.0;
    for (int r = 0; r < 12; ++r)
        for (bool maj : { false, true })
        {
            const double v = corr (maj ? M : m, r);
            if (v > first) { second = first; first = v; best.root = r; best.major = maj; }
            else if (v > second) second = v;
        }
    best.confidence = jlimit (0.0, 1.0, (first - second) * 5.0);
    return best;
}

// the onset strength at a fractional frame (linear)
double at (const std::vector<double>& o, double f)
{
    if (f < 0.0) return 0.0;
    const int i = (int) f;
    if (i + 1 >= (int) o.size()) return 0.0;
    const double t = f - i;
    return o[(size_t) i] * (1.0 - t) + o[(size_t) i + 1] * t;
}

// how well a grid of this period lines up with the attacks, at its best phase (and which phase)
double gridScore (const std::vector<double>& o, double period, double& bestPhase)
{
    double best = -1.0;
    const int steps = jmax (8, (int) std::ceil (period));
    for (int s = 0; s < steps; ++s)
    {
        const double ph = period * s / steps;
        double sum = 0.0;
        int k = 0;
        for (double f = ph; f < (double) o.size() - 1; f += period, ++k)
            sum += jmax (at (o, f - 0.5), jmax (at (o, f), at (o, f + 0.5)));
        const double v = k > 0 ? sum / k : 0.0;
        if (v > best) { best = v; bestPhase = ph; }
    }
    return best;
}
}

double estimarTempo (const AudioBuffer<float>& audio, double sr, double* confianza, double* fase)
{
    const auto x = monoDe (audio);
    const int hop = jmax (64, (int) std::lround (sr / 200.0));         // 200 frames a second
    double fr = 200.0;
    const auto o = onsets (x, sr, hop, fr);
    if (o.size() < (size_t) (fr * 4.0)) { if (confianza) *confianza = 0.0; return 0.0; }

    // the autocorrelation of the attacks, between 60 and 200 BPM, with a preference for tempos people dance to
    // (a log-Gaussian around 120 BPM, one octave wide) and the echo at two and four times the period added in
    const int minLag = (int) std::floor (fr * 60.0 / 200.0), maxLag = (int) std::ceil (fr * 60.0 / 60.0);
    const size_t N = std::min<size_t> (o.size(), (size_t) (fr * 120.0));         // two minutes are enough for this step
    std::vector<double> ac ((size_t) maxLag * 4 + 2, 0.0);
    double mean = 0.0;
    for (size_t i = 0; i < N; ++i) mean += o[i];
    mean /= (double) N;
    for (int lag = 1; lag < (int) ac.size(); ++lag)
    {
        double s = 0.0;
        for (size_t i = 0; i + (size_t) lag < N; ++i) s += (o[i] - mean) * (o[i + (size_t) lag] - mean);
        ac[(size_t) lag] = s / (double) (N - (size_t) lag);
    }
    // the candidates: the autocorrelation's peaks between 60 and 200 BPM (and their double and half), each laid over
    // the whole song as a grid; the score of a grid is the attack its beats carry on average. Of the grids that score
    // close to the best (85 %), the fastest wins: at half the tempo the strong beats still line up, but half of them go missing
    std::vector<std::pair<double, int>> peaks;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (ac[(size_t) lag] > 0.0 && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
            peaks.push_back ({ ac[(size_t) lag] + 0.5 * ac[(size_t) lag * 2], lag });
    std::sort (peaks.begin(), peaks.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    if (peaks.size() > 8) peaks.resize (8);
    std::vector<double> cands;
    for (auto& [v, lag] : peaks)
    {
        // a parabola through the peak for a tempo between frames
        const double y0 = ac[(size_t) lag - 1], y1 = ac[(size_t) lag], y2 = ac[(size_t) lag + 1];
        const double den = y0 - 2.0 * y1 + y2;
        const double l = lag + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
        for (double m : { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0, 0.75, 4.0 / 3.0 }) { const double c = 60.0 * fr / l * m; if (c >= 60.0 && c <= 200.0) cands.push_back (c); }
    }
    if (cands.empty()) { if (confianza) *confianza = 0.0; return 0.0; }
    // the kick's own attacks count too (half as much as everything): a four-to-the-floor kick under triplet toms decides it
    double frLow = fr;
    const auto oLow = onsets (x, sr, hop, frLow, true);
    double meanAll = 1e-9, meanLow = 1e-9;
    for (auto v : o) meanAll += v / (double) o.size();
    for (auto v : oLow) meanLow += v / (double) std::max<size_t> (1, oLow.size());
    std::vector<std::pair<double, double>> scored;          // tempo, grid score
    double top = 0.0;
    for (auto c : cands)
    {
        double bestS = -1.0, bestC = c;
        for (double f = 0.985; f <= 1.0151; f += 0.0025)
        {
            double ph = 0.0, ph2 = 0.0;
            const double sc = gridScore (o, 60.0 * fr / (c * f), ph) / meanAll + 0.5 * gridScore (oLow, 60.0 * fr / (c * f), ph2) / meanLow;
            if (sc > bestS) { bestS = sc; bestC = c * f; }
        }
        scored.push_back ({ bestC, bestS });
        top = jmax (top, bestS);
    }
    double coarse = 0.0;
    for (auto& [c, sc] : scored) if (sc >= top * 0.85 && c > coarse) coarse = c;

    // the fine tempo: every grid from -3 % to +3 % in hundredths of a BPM, against the whole song
    double bestBpm = coarse, bestScore = -1.0, bestPh = 0.0;
    auto search = [&] (double from, double to, double step)
    {
        for (double b = from; b <= to; b += step)
        {
            double ph = 0.0;
            const double sc = gridScore (o, 60.0 * fr / b, ph);
            if (sc > bestScore) { bestScore = sc; bestBpm = b; bestPh = ph; }
        }
    };
    search (coarse * 0.995, coarse * 1.005, 0.02);
    const double c2 = bestBpm;
    search (c2 - 0.06, c2 + 0.06, 0.005);
    // records are made on whole tempos: one within 0.05 BPM is taken
    const double whole = std::round (bestBpm);
    if (std::abs (whole - bestBpm) <= 0.05) { double ph = 0.0; gridScore (o, 60.0 * fr / whole, ph); bestBpm = whole; bestPh = ph; }

    if (confianza != nullptr)
    {
        // the grid against the song's average attack: 1 when every beat is clearly marked
        double avg = 0.0;
        for (auto v : o) avg += v;
        avg /= (double) o.size();
        *confianza = jlimit (0.0, 1.0, (bestScore / jmax (1e-9, avg) - 1.0) / 8.0);
    }
    // a frame is dated by its start; the attack it catches sits in the middle of its window
    if (fase != nullptr) *fase = (bestPh * hop + 1024) / sr + 0.015;      // measured on test songs: the rest of the lag is the attack's rise
    return bestBpm;
}

Analisis analizarAudio (const AudioBuffer<float>& b, double sr)
{
    Analisis a;
    a.segundos = b.getNumSamples() / sr;
    if (a.segundos < 8.0) { a.error = i18n::tr ("Es muy corta: hacen falta al menos 8 segundos."); return a; }
    a.bpm = estimarTempo (b, sr, &a.confianza, &a.primerTiempo);
    // the key: Krumhansl over the notes heard (by harmonic salience)
    const auto chroma = chromaSaliencia (monoDe (b), sr);
    const auto k = llaveDe (chroma);
    if (SystemStats::getEnvironmentVariable ("RACOON_DJ_DEBUG", {}).isNotEmpty())
    {
        String d ("croma:");
        for (int i = 0; i < 12; ++i) d << " " << String (chroma[(size_t) i], 3);
        std::cerr << d << std::endl;
    }
    a.key = k.root;
    a.major = k.major;
    a.claridadTono = k.confidence;
    const auto loud = audio::measureLoudness (b, sr);
    a.lufs = loud.integratedLufs;
    // energy: loudness (-20 LUFS calm, -6 hot), how many attacks a second, and the share of highs
    const auto bal = audio::measureBalance (b, sr);
    double fr = 0.0;
    const auto o = onsets (monoDe (b), sr, jmax (64, (int) std::lround (sr / 200.0)), fr);
    double mean = 0.0;
    for (auto v : o) mean += v;
    mean /= std::max<size_t> (1, o.size());
    int hits = 0;
    for (size_t i = 1; i + 1 < o.size(); ++i) if (o[i] > mean * 3.0 && o[i] >= o[i - 1] && o[i] >= o[i + 1]) ++hits;
    const double rate = hits / jmax (1.0, a.segundos);
    const double loudPart = jlimit (0.0, 1.0, (a.lufs + 20.0) / 14.0);
    const double busyPart = jlimit (0.0, 1.0, rate / 6.0);
    const double brightPart = jlimit (0.0, 1.0, (bal.bands[3] + bal.bands[4]) / 30.0);
    a.energia = jlimit (0.0, 1.0, 0.45 * loudPart + 0.35 * busyPart + 0.2 * brightPart);
    a.ok = a.bpm > 0.0;
    if (! a.ok) a.error = i18n::tr ("No se encontró un pulso claro.");
    return a;
}

Analisis analizar (te::Engine& e, const File& f)
{
    AudioBuffer<float> b;
    double sr = 44100.0;
    Analisis a;
    if (! audio::readFile (e, f, b, sr)) { a.file = f; a.titulo = f.getFileNameWithoutExtension(); a.error = i18n::tr ("No se pudo leer el archivo."); return a; }
    a = analizarAudio (b, sr);
    a.file = f;
    a.titulo = f.getFileNameWithoutExtension();
    return a;
}

int numeroCamelot (int key, bool major)
{
    // 8A is A minor, 8B is C major; each number up is a fifth up
    const int base = major ? 0 : 9;
    for (int n = 1; n <= 12; ++n) if (((base + (n - 8) * 7) % 12 + 12) % 12 == ((key % 12) + 12) % 12) return n;
    return 8;
}

String Analisis::camelot() const { return String (numeroCamelot (key, major)) + (major ? "B" : "A"); }

String Analisis::resumen() const
{
    if (! ok) return error;
    return String (bpm, 2) + " BPM, " + camelot() + " (" + i18n::tr (keyName (key, major)) + "), " + i18n::tr ("energía") + " "
           + String (jlimit (1, 10, (int) std::lround (energia * 9.0) + 1)) + "/10";
}

int distanciaCamelot (int ka, bool ma, int kb, bool mb)
{
    const int na = numeroCamelot (ka, ma), nb = numeroCamelot (kb, mb);
    int d = std::abs (na - nb);
    d = jmin (d, 12 - d);
    return d + (ma != mb ? 1 : 0);
}

double distanciaTempo (double a, double b)
{
    if (a <= 0.0 || b <= 0.0) return 100.0;
    double best = 1e9;
    for (double m : { 0.5, 1.0, 2.0 }) best = jmin (best, std::abs (a * m - b) / b * 100.0);
    return best;
}

namespace
{
double costo (const Analisis& a, const Analisis& b, double posicion)
{
    // the wheel first (a clash is heard), then the tempo (beyond 6 % the pitch moves when synced), then the energy:
    // it should climb in the first two thirds of the set and may come down at the end
    const int dk = distanciaCamelot (a.key, a.major, b.key, b.major);
    double c = dk <= 1 ? dk * 1.0 : dk == 2 ? 3.0 : 4.0 + dk;
    const double dt = distanciaTempo (a.bpm, b.bpm);
    c += dt / 2.0 + (dt > 6.0 ? 4.0 : 0.0);
    const double de = b.energia - a.energia;
    if (posicion < 0.7) c += de < 0.0 ? -de * 8.0 : de > 0.3 ? (de - 0.3) * 4.0 : 0.0;
    else c += de > 0.1 ? de * 3.0 : 0.0;
    return c;
}

double costoTotal (const std::vector<Analisis>& s, const std::vector<int>& order)
{
    double c = 0.0;
    for (size_t i = 0; i + 1 < order.size(); ++i)
        c += costo (s[(size_t) order[i]], s[(size_t) order[i + 1]], (double) i / std::max<size_t> (1, order.size() - 1));
    return c;
}
}

std::vector<int> ordenarSet (const std::vector<Analisis>& s)
{
    std::vector<int> best;
    const int n = (int) s.size();
    if (n == 0) return best;
    double bestCost = 1e300;
    // a greedy walk from each of the calmest songs, then improved by swapping stretches (2-opt)
    std::vector<int> byEnergy ((size_t) n);
    std::iota (byEnergy.begin(), byEnergy.end(), 0);
    std::sort (byEnergy.begin(), byEnergy.end(), [&] (int a, int b) { return s[(size_t) a].energia < s[(size_t) b].energia; });
    for (int st = 0; st < jmin (n, 3); ++st)
    {
        std::vector<int> order { byEnergy[(size_t) st] };
        std::vector<bool> used ((size_t) n, false);
        used[(size_t) order[0]] = true;
        while ((int) order.size() < n)
        {
            int pick = -1;
            double pc = 1e300;
            for (int j = 0; j < n; ++j)
                if (! used[(size_t) j])
                {
                    const double c = costo (s[(size_t) order.back()], s[(size_t) j], (double) order.size() / jmax (1, n - 1));
                    if (c < pc) { pc = c; pick = j; }
                }
            used[(size_t) pick] = true;
            order.push_back (pick);
        }
        bool better = true;
        double cur = costoTotal (s, order);
        for (int pass = 0; better && pass < 50; ++pass)
        {
            better = false;
            for (int i = 1; i < n - 1; ++i)
                for (int j = i + 1; j < n; ++j)
                {
                    auto t = order;
                    std::reverse (t.begin() + i, t.begin() + j + 1);
                    const double c = costoTotal (s, t);
                    if (c < cur - 1e-9) { cur = c; order = t; better = true; }
                }
        }
        if (cur < bestCost) { bestCost = cur; best = order; }
    }
    return best;
}

String transicion (const Analisis& a, const Analisis& b)
{
    const int dk = distanciaCamelot (a.key, a.major, b.key, b.major);
    String t = a.camelot() + String::fromUTF8 (" → ") + b.camelot();
    double diff = b.bpm - a.bpm;
    if (std::abs (b.bpm - a.bpm * 2.0) < std::abs (diff)) diff = b.bpm - a.bpm * 2.0;
    if (std::abs (b.bpm * 2.0 - a.bpm) < std::abs (diff)) diff = b.bpm * 2.0 - a.bpm;
    t << ", " << (diff >= 0 ? "+" : "") << String (diff, 1) << " BPM: ";
    if (dk == 0) t << i18n::tr ("la misma tonalidad");
    else if (dk == 1) t << i18n::tr ("vecinas en la rueda");
    else if (dk == 2) t << i18n::tr ("dos pasos: sube la energía, mézclala corto");
    else t << i18n::tr ("chocan: mezcla solo la batería o corta en seco");
    return t;
}

const Genre& generoPara (const Analisis& a)
{
    // the club genres (the ones a DJ mixes): the nearest tempo; the energy breaks ties
    static const StringArray club { "house", "deephouse", "techhouse", "afrohouse", "organichouse", "progressive", "nudisco", "indiedance",
                                    "melodictechno", "techno", "hardtechno", "minimal", "acid", "trance", "psytrance", "dnb", "liquid",
                                    "dubstep", "ukgarage", "breakbeat", "electro", "disco", "reggaeton", "dembow", "moombahton", "amapiano",
                                    "afrobeats", "jerseyclub", "hardstyle", "trap" };
    const Genre* best = nullptr;
    double bestD = 1e9;
    for (auto& g : genres())
    {
        // the track plays at the song's tempo, so the genre has to live near it (not at half or double)
        const double d = std::abs (g.bpm - a.bpm) / a.bpm * 100.0 + (club.contains (g.id) ? 0.0 : 3.0)
                         + std::abs ((g.bpm >= 130.0 ? 0.8 : 0.5) - a.energia) * 2.0;
        if (d < bestD) { bestD = d; best = &g; }
    }
    return best != nullptr ? *best : defaultGenre();
}
} // namespace racoon::dj

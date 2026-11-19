#include "ChinchillaDsp.h"

namespace racoon::chinchilla
{
using namespace juce;

void Biquad::set (Type t, double sr, double hz, double q, double gainDb)
{
    const double w = MathConstants<double>::twoPi * jlimit (5.0, sr * 0.49, hz) / sr;
    const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * jmax (0.05, q));
    const double A = std::pow (10.0, gainDb / 40.0);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (t)
    {
        case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
        case highShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

// ===================================================================== scales
StringArray scaleNames() { return { "Cromática"_u, "Mayor", "Menor", "Menor armónica"_u, "Pentatónica mayor"_u, "Pentatónica menor"_u, "Blues", "Dórica"_u, "Mixolidia" }; }
StringArray keyNames() { return { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }; }

void scaleMask (int scale, int key, bool* out)
{
    static const std::vector<std::vector<int>> s {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 } };
    for (int i = 0; i < 12; ++i) out[i] = false;
    for (int iv : s[(size_t) jlimit (0, (int) s.size() - 1, scale)]) out[(key + iv) % 12] = true;
}

float nearestInScale (float midi, const bool* mask)
{
    const int base = (int) std::floor (midi);
    float best = midi, bestD = 1.0e9f;
    for (int n = base - 6; n <= base + 7; ++n)
        if (mask[((n % 12) + 12) % 12])
        {
            const float d = std::abs ((float) n - midi);
            if (d < bestD) { bestD = d; best = (float) n; }
        }
    return best;
}

String noteName (int midi)
{
    static const char* n[] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return midi < 0 ? String ("--") : String (n[midi % 12]) + String (midi / 12 - 1);
}

// ===================================================================== YIN
void Yin::prepare (double sampleRate)
{
    sr = sampleRate;
    dec = jmax (1, roundToInt (sr / 12000.0));
    for (auto& b : aa) b.set (Biquad::lowpass, sr, jmin (4500.0, sr / dec * 0.4), 0.7071);
    reset();
}

void Yin::reset()
{
    for (auto& b : aa) b.reset();
    std::fill (std::begin (buf), std::end (buf), 0.0f);
    filled = 0; decCount = 0; hopCount = 0; f0 = 0.0f; clar = 0.0f;
}

bool Yin::push (float x)
{
    const float y = aa[1].process (aa[0].process (x));
    if (++decCount < dec) return false;
    decCount = 0;
    constexpr int size = win + maxLag;
    if (filled < size) { buf[filled++] = y; if (filled < size) return false; }
    else
    {
        std::memmove (buf, buf + 1, sizeof (float) * (size - 1));
        buf[size - 1] = y;
    }
    if (++hopCount < hop) return false;
    hopCount = 0;
    analyse();
    return true;
}

void Yin::analyse()
{
    const double rate = sr / dec;
    double energy = 0.0;
    for (int j = 0; j < win; ++j) energy += (double) buf[j] * buf[j];
    if (energy / win < 1.0e-7) { f0 = 0.0f; clar = 0.0f; return; }
    d[0] = 0.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        float s = 0.0f;
        for (int j = 0; j < win; ++j) { const float e = buf[j] - buf[j + tau]; s += e * e; }
        d[tau] = s;
    }
    // cumulative mean normalised difference
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += d[tau];
        d[tau] = running > 0.0f ? d[tau] * tau / running : 1.0f;
    }
    const int minLag = jmax (2, (int) (rate / 1000.0));
    int pick = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
        if (d[tau] < 0.15f)
        {
            while (tau + 1 < maxLag && d[tau + 1] < d[tau]) ++tau;
            pick = tau;
            break;
        }
    if (pick < 0)
    {
        float best = 1.0e9f;
        for (int tau = minLag; tau < maxLag; ++tau) if (d[tau] < best) { best = d[tau]; pick = tau; }
        if (best > 0.35f) { f0 = 0.0f; clar = jmax (0.0f, 1.0f - best); return; }
    }
    float t = (float) pick;
    if (pick > 1 && pick < maxLag)
    {
        const float a = d[pick - 1], b = d[pick], c = d[pick + 1];
        const float den = a - 2.0f * b + c;
        if (std::abs (den) > 1.0e-9f) t += 0.5f * (a - c) / den;
    }
    f0 = (float) (rate / t);
    clar = jlimit (0.0f, 1.0f, 1.0f - d[pick]);
}

// ===================================================================== PSOLA
void PsolaVoice::prepare (double sampleRate, int lat)
{
    sr = sampleRate;
    latency = lat;
    int n = 1;
    while (n < lat * 4) n <<= 1;
    acc.assign ((size_t) n, 0.0f);
    wsum.assign ((size_t) n, 0.0f);
    mask = n - 1;
    reset();
}

void PsolaVoice::reset()
{
    std::fill (acc.begin(), acc.end(), 0.0f);
    std::fill (wsum.begin(), wsum.end(), 0.0f);
    outTime = 0;
    nextMark = 0.0;
    anaMark = -1.0e9;
}

float PsolaVoice::process (const History& h, double period, float ratio, float formant)
{
    const bool voiced = period > 0.0;
    const double T = voiced ? period : 0.01 * sr;
    const double r = voiced ? jlimit (0.25, 4.0, (double) ratio) : 1.0;
    const double f = jlimit (0.7, 1.42, (double) formant);
    const double hOut = T / f;
    const double t = (double) outTime;
    // add every grain whose start has come
    int guard = 0;
    while (nextMark - hOut <= t && guard++ < 8)
    {
        const double m = nextMark;
        const double cIn = m - latency;
        double a = cIn;
        if (voiced)
        {
            if (anaMark < cIn - 2.0 * T || anaMark > cIn + T) anaMark = cIn;
            while (anaMark + T * 0.5 < cIn) anaMark += T;
            a = anaMark;
        }
        else anaMark = -1.0e9;
        const int half = (int) hOut;
        const auto centre = (int64) std::llround (m);
        for (int j = -half; j <= half; ++j)
        {
            const int64 idx = centre + j;
            if (idx < outTime) continue;
            const float w = 0.5f + 0.5f * std::cos (MathConstants<float>::pi * (float) j / (float) (half + 1));
            acc[(size_t) (idx & mask)] += h.at (a + j * f) * w;
            wsum[(size_t) (idx & mask)] += w;
        }
        nextMark += T / r;
    }
    if (nextMark < t) nextMark = t;   // after a silence the marks catch up
    const size_t i = (size_t) (outTime & mask);
    const float y = acc[i] / jmax (wsum[i], 0.3f);
    acc[i] = 0.0f; wsum[i] = 0.0f;
    ++outTime;
    return y;
}

// ===================================================================== carrier synth
void CarrierSynth::prepare (double sampleRate) { sr = sampleRate; reset(); }
void CarrierSynth::reset() { for (auto& v : voices) v = V {}; noise = 0.0f; }

void CarrierSynth::noteOn (int note, float vel)
{
    V* pick = nullptr;
    for (auto& v : voices) if (v.note == note) pick = &v;
    if (pick == nullptr) for (auto& v : voices) if (! v.gate && v.env < 1.0e-3f) { pick = &v; break; }
    if (pick == nullptr) { pick = &voices[0]; for (auto& v : voices) if (v.env < pick->env) pick = &v; }
    const bool fresh = pick->env < 1.0e-3f;
    pick->note = note; pick->vel = vel; pick->gate = true;
    const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    if (fresh || pick->hz <= 0.0) pick->hz = hz;
}

void CarrierSynth::noteOff (int note) { for (auto& v : voices) if (v.note == note) v.gate = false; }
void CarrierSynth::allOff() { for (auto& v : voices) v.gate = false; }
bool CarrierSynth::anyOn() const { for (auto& v : voices) if (v.gate) return true; return false; }

int CarrierSynth::heldCount() const { int k = 0; for (auto& v : voices) if (v.gate) ++k; return k; }
int CarrierSynth::held (int index) const
{
    int notes[8], k = 0;
    for (auto& v : voices) if (v.gate) notes[k++] = v.note;
    std::sort (notes, notes + k);
    return index < k ? notes[index] : -1;
}
int CarrierSynth::lowestHeld() const { return held (0); }

float CarrierSynth::blep (double t, double dt) const
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

float CarrierSynth::process (int wave, float detuneCents, float glideMs)
{
    const float att = 1.0f / (0.005f * (float) sr), rel = std::exp (-1.0f / (0.08f * (float) sr));
    const double glide = 1.0 - std::exp (-1.0 / (jmax (0.5f, glideMs) * 0.001 * sr));
    float out = 0.0f;
    for (auto& v : voices)
    {
        if (v.gate) v.env = jmin (1.0f, v.env + att); else v.env *= rel;
        if (v.env < 1.0e-5f) continue;
        const double target = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
        v.hz += (target - v.hz) * glide;
        float s = 0.0f;
        for (int o = 0; o < 2; ++o)
        {
            const double dt = v.hz * std::exp2 ((o == 0 ? -0.5 : 0.5) * detuneCents / 1200.0) / sr;
            v.phase[o] += dt;
            if (v.phase[o] >= 1.0) v.phase[o] -= 1.0;
            const float saw = (float) (2.0 * v.phase[o] - 1.0) - blep (v.phase[o], dt);
            double p2 = v.phase[o] + 0.5; if (p2 >= 1.0) p2 -= 1.0;
            const float saw2 = (float) (2.0 * p2 - 1.0) - blep (p2, dt);
            const float sq = (saw - saw2) * 0.5f;
            s += wave == 0 ? saw : wave == 1 ? sq : 0.5f * (saw + sq);
        }
        out += s * 0.5f * v.env * (0.5f + 0.5f * v.vel);
    }
    return out * 0.35f;
}

// ===================================================================== vocoder
void Vocoder::prepare (double sampleRate)
{
    sr = sampleRate;
    att = std::exp (-1.0f / (0.003f * (float) sr));
    rel = std::exp (-1.0f / (0.035f * (float) sr));
    sib.set (Biquad::highpass, sr, 5000.0, 0.7071);
    lastQ = -1.0f;
    reset();
}

void Vocoder::reset()
{
    for (auto& b : an) { b[0].reset(); b[1].reset(); }
    for (auto& b : sy) { b[0].reset(); b[1].reset(); }
    sib.reset();
    std::fill (std::begin (env), std::end (env), 0.0f);
}

void Vocoder::design (float q, float shift)
{
    for (int b = 0; b < bands; ++b)
    {
        const double hz = 120.0 * std::pow (7500.0 / 120.0, b / (double) (bands - 1));
        const double sh = jlimit (40.0, sr * 0.45, hz * std::pow (2.0, shift / 12.0));
        for (int k = 0; k < 2; ++k)
        {
            const float z[4] { an[b][k].z1, an[b][k].z2, sy[b][k].z1, sy[b][k].z2 };
            an[b][k].set (Biquad::bandpass, sr, hz, q);
            sy[b][k].set (Biquad::bandpass, sr, sh, q);
            an[b][k].z1 = z[0]; an[b][k].z2 = z[1]; sy[b][k].z1 = z[2]; sy[b][k].z2 = z[3];
        }
    }
    lastQ = q; lastShift = shift;
}

float Vocoder::process (float voice, float carrier, float q, float sibilance, float shift)
{
    if (std::abs (q - lastQ) > 0.01f || std::abs (shift - lastShift) > 0.01f) design (q, shift);
    float out = 0.0f;
    for (int b = 0; b < bands; ++b)
    {
        const float a = std::abs (an[b][1].process (an[b][0].process (voice)));
        env[b] = a + (env[b] - a) * (a > env[b] ? att : rel);
        out += sy[b][1].process (sy[b][0].process (carrier)) * env[b];
    }
    return out * q * 6.0f + sib.process (voice) * sibilance;
}

// ===================================================================== talkbox
void Talkbox::prepare (double sampleRate) { sr = sampleRate; reset(); }

void Talkbox::reset()
{
    std::fill (std::begin (buf), std::end (buf), 0.0f);
    std::fill (std::begin (hist), std::end (hist), 0.0f);
    std::fill (std::begin (coef), std::end (coef), 0.0);
    coef[0] = 1.0;
    gain = 0.0f; pos = 0; preState = 0.0f;
}

void Talkbox::analyse (float brightness)
{
    // autocorrelation of the windowed frame, then Levinson-Durbin
    double r[order + 1] {};
    float w[frame];
    for (int i = 0; i < frame; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (frame - 1));
        w[i] = buf[(pos + i) % frame] * win;
    }
    for (int k = 0; k <= order; ++k)
    {
        double s = 0.0;
        for (int i = k; i < frame; ++i) s += (double) w[i] * w[i - k];
        r[k] = s;
    }
    if (r[0] < 1.0e-9) { gain = 0.0f; return; }
    r[0] *= 1.0001;   // a touch of white noise keeps it stable
    double a[order + 1] {}, tmp[order + 1] {};
    a[0] = 1.0;
    double err = r[0];
    for (int i = 1; i <= order; ++i)
    {
        double acc = r[i];
        for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
        const double k = -acc / err;
        for (int j = 0; j <= i; ++j) tmp[j] = a[j];
        for (int j = 1; j < i; ++j) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        err *= (1.0 - k * k);
        if (err <= 0.0) break;
    }
    for (int i = 0; i <= order; ++i) coef[i] = a[i];
    gain = (float) std::sqrt (jmax (0.0, err) / frame) * (1.0f + brightness);
}

float Talkbox::process (float voice, float carrier, float brightness)
{
    // brighter: more pre-emphasis before the analysis
    const float pre = voice - (0.5f + 0.45f * brightness) * preState;
    preState = voice;
    buf[pos] = pre;
    pos = (pos + 1) % frame;
    if (pos % (frame / 2) == 0) analyse (brightness);
    double y = gain * carrier * 4.0;
    for (int k = 1; k <= order; ++k) y -= coef[k] * hist[k - 1];
    for (int k = order - 1; k > 0; --k) hist[k] = hist[k - 1];
    hist[0] = (float) jlimit (-8.0, 8.0, y);
    return hist[0];
}
}

// ===================================================================== singer
namespace racoon::chinchilla
{
void Singer::prepare (double sampleRate) { sr = sampleRate; reset(); }

void Singer::reset()
{
    note = -1; gate = false; env = 0.0f; heldCount = 0; cons = 0.0f; lp = 0.0f;
    for (auto& b : bp) b[0] = b[1] = 0.0f;
    consBp[0] = consBp[1] = 0.0f;
}

float Singer::noise()
{
    rnd = rnd * 1664525u + 1013904223u;
    return (float) (rnd >> 8) / 8388608.0f - 1.0f;
}

void Singer::noteOn (int n, float v)
{
    for (int i = 0; i < heldCount; ++i) if (held[i] == n) { for (int k = i; k < heldCount - 1; ++k) held[k] = held[k + 1]; --heldCount; break; }
    if (heldCount < 16) held[heldCount++] = n;
    const bool legato = gate && env > 0.3f;
    note = n; vel = v; gate = true;
    targetHz = 440.0f * std::pow (2.0f, (float) (n - 69) / 12.0f);
    if (! legato) { hz = targetHz; age = 0.0f; cons = 1.0f; }
    // a new syllable: a new vowel (in an order that sounds like words, not like a scale)
    static const int order[] { 0, 3, 1, 0, 2, 3, 4, 1, 0, 3, 2, 1 };
    currentVowel = order[syllable++ % 12];
}

void Singer::noteOff (int n)
{
    for (int i = 0; i < heldCount; ++i) if (held[i] == n) { for (int k = i; k < heldCount - 1; ++k) held[k] = held[k + 1]; --heldCount; break; }
    if (n != note) return;
    if (heldCount > 0)
    {
        note = held[heldCount - 1];
        targetHz = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
    }
    else gate = false;
}

float Singer::process (int vowel, float size, float breath)
{
    if (! active()) return 0.0f;
    static const float formants[5][3] { { 800, 1150, 2900 }, { 450, 1900, 2600 }, { 320, 2300, 3000 }, { 500, 850, 2800 }, { 340, 720, 2500 } };
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float dt = 1.0f / (float) sr;
    age += dt;
    hz += (targetHz - hz) * (1.0f - std::exp (-dt / 0.035f));       // a short glide between notes
    vib += twoPi * 5.2f * dt;
    if (vib > twoPi) vib -= twoPi;
    const float vibDepth = 0.007f * juce::jlimit (0.0f, 1.0f, (age - 0.25f) * 2.5f);
    phase += hz * (1.0f + vibDepth * std::sin (vib)) * dt;
    if (phase >= 1.0f) phase -= 1.0f;
    const float envTarget = gate ? vel : 0.0f;
    env += (envTarget - env) * (1.0f - std::exp (-dt / (gate ? 0.04f : 0.12f)));
    // the vowel glides to the next one
    const int v = vowel >= 0 ? juce::jlimit (0, 4, vowel) : currentVowel;
    for (int k = 0; k < 3; ++k) f[k] += (formants[v][k] * size - f[k]) * (1.0f - std::exp (-dt / 0.03f));
    // glottal pulse (a softened saw) and breath
    lp += 0.3f * ((2.0f * phase - 1.0f) - lp);
    const float src = lp + noise() * (0.02f + 0.08f * breath);
    float y = 0.0f;
    const float gains[3] { 1.0f, 0.5f, 0.22f };
    for (int k = 0; k < 3; ++k)
    {
        const float w = twoPi * f[k] / (float) sr;
        const float rr = std::exp (-juce::MathConstants<float>::pi * (55.0f + f[k] * 0.05f) / (float) sr);
        const float o = (1.0f - rr) * src + 2.0f * rr * std::cos (w) * bp[k][0] - rr * rr * bp[k][1];
        bp[k][1] = bp[k][0]; bp[k][0] = o;
        y += o * gains[k];
    }
    // the consonant at the start of a syllable: a short breathy burst high up
    if (cons > 1.0e-3f)
    {
        const float w = twoPi * 3200.0f / (float) sr, rr = 0.97f;
        const float o = (1.0f - rr) * noise() + 2.0f * rr * std::cos (w) * consBp[0] - rr * rr * consBp[1];
        consBp[1] = consBp[0]; consBp[0] = o;
        y += o * cons * 3.0f;
        cons *= std::exp (-dt / 0.025f);
    }
    return y * env * 1.6f;
}
}

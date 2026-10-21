#include "PorcupineVoice.h"

namespace racoon::porcupine
{
using namespace juce;

Range rangeOf (int s)
{
    static const Range r[] { { 60, 81 }, { 53, 74 }, { 48, 69 }, { 40, 62 }, { 43, 88 }, { 46, 77 } };
    return r[jlimit (0, numSections - 1, s)];
}

StringArray sectionNames() { return { "Soprano", "Contralto", "Tenor", "Bajo", "Cuerdas", "Metales" }; }
StringArray vowelNames() { return { "A", "E", "I", "O", "U" }; }
StringArray keyNames() { return { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" }; }
StringArray articulationNames() { return { "Sostenido", "Staccato", "Marcato", "Legato", "Crescendo" }; }

// ===================================================================== vowels
// formant frequencies, bandwidths and levels (dB) of the five vowels for each voice type
namespace
{
struct VowelRow { float f[5], db[5], bw[5]; };
const VowelRow table[4][5] {
    { // soprano
      { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 }, { 80, 90, 120, 130, 140 } },
      { { 350, 2000, 2800, 3600, 4950 }, { 0, -20, -15, -40, -56 }, { 60, 100, 120, 150, 200 } },
      { { 270, 2140, 2950, 3900, 4950 }, { 0, -12, -26, -26, -44 }, { 60, 90, 100, 120, 120 } },
      { { 450, 800, 2830, 3800, 4950 }, { 0, -11, -22, -22, -50 }, { 70, 80, 100, 130, 135 } },
      { { 325, 700, 2700, 3800, 4950 }, { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } } },
    { // alto
      { { 800, 1150, 2800, 3500, 4950 }, { 0, -4, -20, -36, -60 }, { 80, 90, 120, 130, 140 } },
      { { 400, 1600, 2700, 3300, 4950 }, { 0, -24, -30, -35, -60 }, { 60, 80, 120, 150, 200 } },
      { { 350, 1700, 2700, 3700, 4950 }, { 0, -20, -30, -36, -60 }, { 50, 100, 120, 150, 200 } },
      { { 450, 800, 2830, 3500, 4950 }, { 0, -9, -16, -28, -55 }, { 70, 80, 100, 130, 135 } },
      { { 325, 700, 2530, 3500, 4950 }, { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } } },
    { // tenor
      { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 } },
      { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },
      { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },
      { { 400, 800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
      { { 350, 600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } } },
    { // bass
      { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 }, { 60, 70, 110, 120, 130 } },
      { { 400, 1620, 2400, 2800, 3100 }, { 0, -12, -9, -12, -18 }, { 40, 80, 100, 120, 120 } },
      { { 250, 1750, 2600, 3050, 3340 }, { 0, -30, -16, -22, -28 }, { 60, 90, 100, 120, 120 } },
      { { 400, 750, 2400, 2600, 2900 }, { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
      { { 350, 600, 2400, 2675, 2950 }, { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } } },
};
}

Formants formantsFor (int sec, float vowel, float hum, float brightness)
{
    sec = jlimit (0, numChoir - 1, sec);
    vowel = jlimit (0.0f, 4.0f, vowel);
    const int a = jmin (3, (int) vowel);
    const float t = vowel - (float) a;
    const auto& A = table[sec][a];
    const auto& B = table[sec][a + 1];
    Formants f;
    for (int i = 0; i < 5; ++i)
    {
        f.f[i] = A.f[i] + (B.f[i] - A.f[i]) * t;
        f.bw[i] = A.bw[i] + (B.bw[i] - A.bw[i]) * t;
        float db = A.db[i] + (B.db[i] - A.db[i]) * t;
        if (i >= 2) db += (brightness - 0.5f) * 14.0f;          // louder singing has a brighter "ring"
        // a closed mouth: one low nasal resonance, the rest muffled
        if (i == 0) f.f[0] += (260.0f - f.f[0]) * hum;
        else db -= hum * (20.0f + 8.0f * i);
        f.gain[i] = dsp::dbToGain (db);
    }
    return f;
}

// ===================================================================== syllables
const std::vector<Syllable>& syllables()
{
    static const std::vector<Syllable> s {
        { "A", cNone, 0, 0 }, { "E", cNone, 1, 0 }, { "I", cNone, 2, 0 }, { "O", cNone, 3, 0 }, { "U", cNone, 4, 0 }, { "Mm", cM, 4, 1 },
        { "La", cL, 0, 0 }, { "Lo", cL, 3, 0 }, { "Lu", cL, 4, 0 }, { "Da", cD, 0, 0 }, { "Do", cD, 3, 0 }, { "Du", cD, 4, 0 },
        { "Na", cN, 0, 0 }, { "No", cN, 3, 0 }, { "Ma", cM, 0, 0 }, { "Mo", cM, 3, 0 }, { "Sa", cS, 0, 0 }, { "So", cS, 3, 0 },
        { "Ta", cT, 0, 0 }, { "Ka", cK, 0, 0 }, { "Ki", cK, 2, 0 }, { "Ha", cH, 0, 0 }, { "Ho", cH, 3, 0 }, { "Ah-Oh", cNone, 1.5f, 0 } };
    return s;
}

StringArray syllableNames() { StringArray s; for (auto& x : syllables()) s.add (x.text); return s; }

// ===================================================================== harmony
Satb voiceLead (const int* pcs, int numPcs, int bassPc, const Satb& prev, bool open, float smooth, int sopranoNote)
{
    Satb best;
    if (numPcs <= 0) return best;
    auto inChord = [&] (int n) { const int pc = ((n % 12) + 12) % 12; for (int i = 0; i < numPcs; ++i) if (pcs[i] == pc) return true; return false; };
    const bool hasPrev = prev.valid();

    // the bass: its note, as close as possible to where it was
    const auto rb = rangeOf (bass);
    int bassNote = -1;
    double bestB = 1.0e9;
    for (int n = rb.lo; n <= rb.hi; ++n)
        if (((n % 12) + 12) % 12 == bassPc)
        {
            const double d = hasPrev && prev.note[3] >= 0 ? std::abs (n - prev.note[3]) : std::abs (n - 48);
            if (d < bestB) { bestB = d; bassNote = n; }
        }
    if (bassNote < 0) return best;

    int cand[3][24], nc[3] {};
    for (int v = 0; v < 3; ++v)
    {
        const auto r = rangeOf (v);
        for (int n = r.lo; n <= r.hi && nc[v] < 24; ++n) if (inChord (n)) cand[v][nc[v]++] = n;
    }
    if (sopranoNote >= 0) { cand[0][0] = sopranoNote; nc[0] = 1; }
    double bestScore = 1.0e9;
    for (int i = 0; i < nc[0]; ++i)
        for (int j = 0; j < nc[1]; ++j)
            for (int k = 0; k < nc[2]; ++k)
            {
                const int s = cand[0][i], a = cand[1][j], t = cand[2][k], b = bassNote;
                if (! (s > a && a > t && t > b)) continue;
                if ((s - a > 12 && sopranoNote < 0) || a - t > 12 || t - b > 19) continue;
                const int now[4] { s, a, t, b };
                double score = 0.0;
                // every chord tone sung (the first four when the chord has more)
                bool has[12] {};
                for (int n : now) has[n % 12] = true;
                for (int p = 0; p < jmin (4, numPcs); ++p) if (! has[pcs[p]]) score += 10.0;
                // doubling: the root (or the bass) is the one to double
                int count[12] {};
                for (int n : now) ++count[n % 12];
                for (int pc = 0; pc < 12; ++pc) if (count[pc] > 1 && pc != pcs[0] && pc != bassPc) score += 2.0;
                // spacing
                score += open ? (s - t <= 12 ? 4.0 : 0.0) : (s - t > 12 ? 4.0 : 0.0);
                if (hasPrev)
                {
                    for (int v = 0; v < 3; ++v) score += std::abs (now[v] - prev.note[v]) * (0.3 + smooth);
                    if (std::abs (s - prev.note[0]) > 7 && sopranoNote < 0) score += 2.0;
                    if (sopranoNote >= 0) score += (s - a > 12 ? (s - a - 12) * 1.5 : 0.0);
                    // no parallel fifths or octaves
                    for (int x = 0; x < 4; ++x)
                        for (int y = x + 1; y < 4; ++y)
                        {
                            const int before = ((prev.note[x] - prev.note[y]) % 12 + 12) % 12, after = ((now[x] - now[y]) % 12 + 12) % 12;
                            if (before == after && (after == 7 || after == 0) && now[x] != prev.note[x]) score += 5.0;
                        }
                }
                else
                    for (int v = 0; v < 3; ++v) { const auto r = rangeOf (v); score += std::abs (now[v] - (r.lo + r.hi) / 2) * 0.3; }
                if (score < bestScore) { bestScore = score; best.note[0] = s; best.note[1] = a; best.note[2] = t; best.note[3] = b; }
            }
    if (! best.valid())
    {
        // too few notes for four different lines: each section sings the nearest chord tone to the middle of its range
        for (int v = 0; v < 3; ++v)
        {
            const auto r = rangeOf (v);
            int pick = -1;
            for (int n = r.lo; n <= r.hi; ++n) if (inChord (n) && (pick < 0 || std::abs (n - (r.lo + r.hi) / 2) < std::abs (pick - (r.lo + r.hi) / 2))) pick = n;
            best.note[v] = pick;
        }
        if (sopranoNote >= 0) best.note[0] = sopranoNote;
        best.note[3] = bassNote;
    }
    return best;
}

int harmonize (int melody, int key, bool minor, int* pcs, int& bassPc, int previousDegree)
{
    static const int major[7] { 0, 2, 4, 5, 7, 9, 11 }, minorScale[7] { 0, 2, 3, 5, 7, 8, 10 };
    static const float wMajor[7] { 0.0f, 2.5f, 3.5f, 1.5f, 1.0f, 2.0f, 4.0f }, wMinor[7] { 0.0f, 4.0f, 3.0f, 1.5f, 1.0f, 2.0f, 3.0f };
    const int* sc = minor ? minorScale : major;
    const int m = ((melody % 12) + 12) % 12;
    int bestDeg = -1;
    float best = 1.0e9f;
    int chosen[3] {};
    for (int d = 0; d < 7; ++d)
    {
        int tri[3];
        for (int k = 0; k < 3; ++k) tri[k] = (key + sc[(d + 2 * k) % 7]) % 12;
        if (minor && d == 4) tri[1] = (key + 11) % 12;   // the dominant is major in a minor key
        int where = -1;
        for (int k = 0; k < 3; ++k) if (tri[k] == m) where = k;
        if (where < 0) continue;
        const float score = (minor ? wMinor[d] : wMajor[d]) + (d == previousDegree ? 0.7f : 0.0f) + (where == 2 ? 0.3f : 0.0f);
        if (score < best) { best = score; bestDeg = d; std::copy (tri, tri + 3, chosen); }
    }
    if (bestDeg < 0) { chosen[0] = m; chosen[1] = (m + 4) % 12; chosen[2] = (m + 7) % 12; }   // outside the key: a major chord on the note
    std::copy (chosen, chosen + 3, pcs);
    bassPc = chosen[0];
    return bestDeg;
}

// ===================================================================== voice
namespace
{
inline float hash01 (uint32 seed, uint32 k, uint32 salt)
{
    uint64 x = (uint64) k * 0x9E3779B97F4A7C15ull ^ ((uint64) seed << 32) ^ ((uint64) salt * 0xBF58476D1CE4E5B9ull);
    x ^= x >> 31; x *= 0x94D049BB133111EBull; x ^= x >> 29;
    return (float) ((x >> 40) & 0xffffff) / 16777216.0f;
}
inline float polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}
}

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    active = false;
    apDelay[0] = jlimit (1, 2047, (int) (0.0011 * sr));
    apDelay[1] = jlimit (1, 2047, (int) (0.0017 * sr));
}

void Voice::start (int sec, int k, int n, float vel, Articulation a, Consonant c, const VoiceSettings& s, bool glideFrom)
{
    const bool wasSounding = active && env > 0.01f;
    section = sec; key = k; velocity = vel; art = a;
    targetHz = 440.0 * std::pow (2.0, (n - 69) / 12.0);
    if (! (glideFrom && wasSounding)) hz = targetHz;
    note = n;
    glideCoef = 1.0 - std::exp (-1.0 / (jmax (0.005f, s.glideSeconds) * sr * 0.3));
    active = true; gate = true;
    if (! (glideFrom && wasSounding))
    {
        env = 0.0f; age01 = 0.0;
        for (auto& r : res) for (auto& x : r) x.z1 = x.z2 = 0.0f;
        lp[0].reset(); lp[1].reset();
        for (auto& b : apBuf) std::fill (std::begin (b), std::end (b), 0.0f);
        numPlayers = jlimit (1, maxSingers, s.size);
        const float centre = 0.5f + 0.5f * s.pan;
        for (int i = 0; i < numPlayers; ++i)
        {
            auto& p = players[i];
            const uint32 h = (uint32) (n * 131 + i * 7 + sec * 1031);
            p.phase = hash01 (s.seed, h, 1);
            p.detune = numPlayers == 1 ? 0.0f : (hash01 (s.seed, h, 2) * 2.0f - 1.0f) * s.detuneCents;
            p.pan = jlimit (0.0f, 1.0f, centre + (numPlayers == 1 ? 0.0f : ((float) i / (float) (numPlayers - 1) - 0.5f) * s.width * 0.95f));
            p.vibRate = s.vibRate * (0.9f + 0.2f * hash01 (s.seed, h, 3));
            p.vibPhase = hash01 (s.seed, h, 4);
            p.driftRate = 0.05f + 0.2f * hash01 (s.seed, h, 5);
            p.driftPhase = hash01 (s.seed, h, 6);
            p.delay = i == 0 ? 0 : (int) (hash01 (s.seed, h, 7) * s.scatterMs * 0.001 * sr);
            p.tilt = 0.0f;
        }
        rng.seed ((uint32) (n * 977 + sec * 31 + 1));
        vowelNow = vowelTarget = s.vowel;
        humVoice = s.hum;
    }
    else { vowelTarget = s.vowel; humVoice = s.hum; }
    float attack = s.attack;
    accent = 0.0f;
    switch (a)
    {
        case staccato: attack = 0.012f; autoReleaseIn = (int) (0.12 * sr); break;
        case marcato:  attack = 0.015f; accent = 0.6f; autoReleaseIn = -1; break;
        case crescendo: attack = 1.8f; autoReleaseIn = -1; break;
        default: autoReleaseIn = -1; break;
    }
    if (sec == brass) accent = jmax (accent, 0.5f);
    accentDecay = std::exp (-1.0f / (0.25f * (float) sr));
    attackStep = 1.0f / jmax (1.0f, attack * (float) sr);
    envTarget = 1.0f;
    releaseCoef = std::exp (-4.6f / jmax (1.0f, (a == staccato ? 0.08f : s.release) * (float) sr));
    consonant = (glideFrom && wasSounding && a == legato) ? cNone : c;
    consTotal = consLeft = consonant == cNone ? 0 : (int) ((consonant == cS ? 0.09 : consonant == cH ? 0.06 : 0.05) * sr);
    coefCountdown = 0;
}

void Voice::glideTo (int n, Consonant c, float vowel, float hum)
{
    targetHz = 440.0 * std::pow (2.0, (n - 69) / 12.0);
    note = n;
    vowelTarget = vowel;
    humVoice = hum;
    consonant = c;
    consTotal = consLeft = c == cNone ? 0 : (int) ((c == cS ? 0.09 : 0.05) * sr);
}

void Voice::release() { gate = false; }

float Voice::source (Player& p, double baseHz, float vib)
{
    if (p.delay > 0) { --p.delay; return 0.0f; }
    p.vibPhase += p.vibRate / (float) sr;
    if (p.vibPhase >= 1.0f) p.vibPhase -= 1.0f;
    p.driftPhase += p.driftRate / (float) sr;
    if (p.driftPhase >= 1.0f) p.driftPhase -= 1.0f;
    const float cents = p.detune + vib * std::sin (MathConstants<float>::twoPi * p.vibPhase) + drift * std::sin (MathConstants<float>::twoPi * p.driftPhase);
    const double inc = baseHz * std::exp2 (cents / 1200.0f) / sr;
    p.phase += inc;
    if (p.phase >= 1.0) p.phase -= 1.0;
    return (float) (2.0 * p.phase - 1.0) - polyBlep (p.phase, inc);
}

void Voice::updateCoefficients (const VoiceSettings& s)
{
    if (section >= numChoir) return;
    float hum = humVoice;
    const float consPos = consTotal > 0 ? (float) consLeft / (float) consTotal : 0.0f;
    if (consLeft > 0 && (consonant == cM || consonant == cN)) hum = jmax (hum, 0.9f * consPos + 0.1f);
    if (consLeft > 0 && consonant == cL) hum = jmax (hum, 0.5f * consPos);
    const auto f = formantsFor (section, vowelNow, hum, s.brightness * (0.5f + s.dynamics));
    for (int i = 0; i < 5; ++i)
    {
        const double fr = jmin ((double) f.f[i], sr * 0.45);
        const double r = std::exp (-MathConstants<double>::pi * f.bw[i] / sr);
        const double w = MathConstants<double>::twoPi * fr / sr;
        const float a1 = (float) (-2.0 * r * std::cos (w)), a2 = (float) (r * r);
        const float b0 = (float) ((1.0 - r) * std::sqrt (1.0 - 2.0 * r * std::cos (2.0 * w) + r * r));
        for (int c = 0; c < 2; ++c) { res[c][i].a1 = a1; res[c][i].a2 = a2; res[c][i].b0 = b0; }
        resGain[i] = f.gain[i];
    }
}

void Voice::render (float* L, float* R, int n, const VoiceSettings& s)
{
    if (! active) return;
    const bool choir = section < numChoir;
    const float invN = 1.0f / std::sqrt ((float) numPlayers);
    const float dynGain = 0.25f + 0.75f * s.dynamics;
    drift = s.driftCents;
    for (int i = 0; i < n; ++i)
    {
        if (--coefCountdown <= 0)
        {
            vowelNow += (vowelTarget - vowelNow) * 0.15f;
            updateCoefficients (s);
            coefCountdown = 32;
        }
        age01 += 1.0 / sr;
        hz += (targetHz - hz) * glideCoef;
        // envelope
        if (gate)
        {
            if (env < envTarget) env = jmin (envTarget, env + attackStep);
            if (autoReleaseIn > 0 && --autoReleaseIn == 0) gate = false;
        }
        else env *= releaseCoef;
        accent *= accentDecay;
        if (! gate && env < 1.0e-4f) { active = false; return; }

        const float vibAmt = s.vibDepth * (float) jlimit (0.0, 1.0, (age01 - s.vibDelay) / 0.5);
        double base = hz;
        if (section == brass) base *= std::exp2 (-0.4 * std::exp (-age01 / 0.03) / 12.0);   // the lip scoops up into the note

        // consonant: how much of the voice and of the noise sounds right now
        float voiced = 1.0f, noise = 0.0f;
        float noiseHz = 4000.0f;
        if (consLeft > 0)
        {
            const float pos = 1.0f - (float) consLeft / (float) consTotal;   // 0 .. 1 through the consonant
            switch (consonant)
            {
                case cS: voiced = pos > 0.8f ? (pos - 0.8f) * 5.0f : 0.0f; noise = 0.35f * std::sin (MathConstants<float>::pi * jmin (1.0f, pos * 1.25f)); noiseHz = 6500.0f; break;
                case cH: voiced = pos * pos; noise = 0.4f * (1.0f - pos); noiseHz = 1500.0f; break;
                case cT: case cK: case cD:
                {
                    voiced = pos < 0.6f ? 0.0f : (pos - 0.6f) * 2.5f;
                    const bool burst = pos >= 0.6f && pos < 0.75f;
                    noise = burst ? (consonant == cD ? 0.25f : 0.5f) : 0.0f;
                    noiseHz = consonant == cT ? 4500.0f : consonant == cK ? 1800.0f : 700.0f;
                    if (consonant == cD) voiced = jmax (voiced, 0.15f);
                    break;
                }
                case cL: voiced = 0.6f + 0.4f * pos; break;
                case cM: case cN: voiced = 0.7f + 0.3f * pos; break;
                default: break;
            }
            --consLeft;
        }

        float sl = 0.0f, sr2 = 0.0f;
        for (int p = 0; p < numPlayers; ++p)
        {
            auto& pl = players[p];
            float x = source (pl, base, vibAmt);
            if (choir)
            {
                // the glottis: a softer edge when singing softly
                const float c = 0.35f - 0.25f * s.dynamics;
                pl.tilt = x + (pl.tilt - x) * c;
                x = pl.tilt;
            }
            sl += x * std::sqrt (1.0f - pl.pan);
            sr2 += x * std::sqrt (pl.pan);
        }
        sl *= invN * voiced; sr2 *= invN * voiced;

        float outL = 0.0f, outR = 0.0f;
        if (choir)
        {
            // breath: noise that follows the opening of the glottis
            const float br = s.breath * 0.25f * (players[0].phase < 0.5 ? 1.0f : 0.4f);
            const float nl = rng.nextBipolar() * br, nr = rng.nextBipolar() * br;
            const float inL = sl + nl, inR = sr2 + nr;
            for (int k = 0; k < 5; ++k)
            {
                auto& a = res[0][k]; auto& b = res[1][k];
                const float yl = a.b0 * inL - a.a1 * a.z1 - a.a2 * a.z2; a.z2 = a.z1; a.z1 = yl;
                const float yr = b.b0 * inR - b.a1 * b.z1 - b.a2 * b.z2; b.z2 = b.z1; b.z1 = yr;
                outL += yl * resGain[k]; outR += yr * resGain[k];
            }
            outL *= 3.0f; outR *= 3.0f;
        }
        else
        {
            const float bright = s.brightness * (0.5f + s.dynamics);
            float cutoff = section == strings ? (500.0f + bright * 6000.0f) * (0.4f + 0.6f * env)
                                              : 350.0f + bright * 4500.0f * (0.3f + 0.7f * env + accent * 1.5f);
            const float g = dsp::SVF::gFor (cutoff, sr);
            lp[0].process (sl, g, 1.2f); lp[1].process (sr2, g, 1.2f);
            outL = lp[0].lp * 0.5f; outR = lp[1].lp * 0.5f;
        }
        if (noise > 0.0f)
        {
            // the consonant's hiss or burst, through a band around its colour
            const float g = dsp::SVF::gFor (noiseHz, sr);
            const float x = rng.nextBipolar() * noise;
            consState[0] += g * (x - consState[0] - consState[1] * 0.7f);
            consState[1] += g * consState[0];
            outL += consState[0] * 0.8f; outR += consState[0] * 0.8f;
        }
        if (numPlayers > 1 && s.width > 0.0f)
        {
            const float dec = jmin (1.0f, s.width) * 0.3f, g = 0.5f;
            float* io[2] { &outL, &outR };
            for (int c = 0; c < 2; ++c)
            {
                const float d = apBuf[c][(apPos - apDelay[c]) & 2047];
                const float x = *io[c];
                const float y = -g * x + d;
                apBuf[c][apPos] = x + g * y;
                *io[c] = x + (y - x) * dec;
            }
            apPos = (apPos + 1) & 2047;
        }
        const float a = env * (1.0f + accent) * velocity * dynGain;
        L[i] += outL * a;
        R[i] += outR * a;
    }
}
}

#include "Features.h"
#include "../PluginProcessor.h"

namespace be
{
using namespace juce;

namespace
{
std::vector<float> monoOf (const SampleData& s)
{
    std::vector<float> m ((size_t) s.size());
    for (int i = 0; i < s.size(); ++i) m[(size_t) i] = s.stereo() ? 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]) : s.l[(size_t) i];
    return m;
}

float inverseExp (float v, float lo, float hi) { return clamp01 (std::log (std::max (v, 1.0e-6f) / lo) / std::log (hi / lo)); }
}

// ===================================================================== Drum DNA
DrumDnaResult DrumDNA::analyse (const File& f) { return analyse (loadSampleFile (f, 8.0)); }

DrumDnaResult DrumDNA::analyse (SamplePtr sp)
{
    DrumDnaResult r;
    if (sp == nullptr || sp->size() < 256) { r.summary = "Could not read that file"; return r; }
    r.sample = sp;
    const auto x = monoOf (*sp);
    const double sr = sp->sampleRate;
    const int n = (int) x.size();
    float pk = 0.0f;
    for (auto v : x) pk = std::max (pk, std::abs (v));
    if (pk < 1.0e-4f) { r.summary = "The file is silent"; return r; }
    int onset = 0;
    while (onset < n && std::abs (x[(size_t) onset]) < pk * 0.08f) ++onset;
    // envelope in 2 ms frames
    const int hop = std::max (16, (int) (sr * 0.002));
    std::vector<float> env;
    for (int i = onset; i + hop < n; i += hop)
    {
        double e = 0.0;
        for (int j = 0; j < hop; ++j) e += (double) x[(size_t) (i + j)] * x[(size_t) (i + j)];
        env.push_back ((float) std::sqrt (e / hop));
    }
    if (env.empty()) { r.summary = "Too short"; return r; }
    int peakF = 0;
    for (int i = 0; i < (int) env.size() && i < 50; ++i) if (env[(size_t) i] > env[(size_t) peakF]) peakF = i;
    r.attackMs = (float) (peakF * hop / sr * 1000.0);
    int endF = (int) env.size() - 1;
    for (int i = peakF; i < (int) env.size(); ++i) if (env[(size_t) i] < env[(size_t) peakF] * 0.01f) { endF = i; break; }
    r.decaySec = (float) ((endF - peakF) * hop / sr);
    // bursts (clap): count re-attacks in the first 50 ms
    r.bursts = 1;
    for (int i = 2; i < std::min ((int) env.size() - 1, (int) (0.05 * sr / hop)); ++i)
        if (env[(size_t) i] > env[(size_t) i - 1] * 1.35f && env[(size_t) i] > env[(size_t) peakF] * 0.3f && env[(size_t) i - 1] < env[(size_t) i - 2]) ++r.bursts;

    // spectrum of the first 85 ms
    const int order = 12, N = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N && onset + i < n; ++i) buf[(size_t) i] = x[(size_t) (onset + i)] * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (N - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double tot = 0, wsum = 0, low = 0, hi = 0, hi2 = 0, logSum = 0, linSum = 0; int flatN = 0;
    const double bin = sr / N;
    for (int k = 1; k < N / 2; ++k)
    {
        const double m = buf[(size_t) k], hz = k * bin, e = m * m;
        tot += e; wsum += e * hz;
        if (hz < 150) low += e;
        if (hz > 5000) hi += e;
        if (hz > 2000) hi2 += e;
        if (hz > 2000 && hz < 12000) { logSum += std::log (m + 1.0e-9); linSum += m; ++flatN; }
    }
    r.centroidHz = tot > 0 ? (float) (wsum / tot) : 0.0f;
    r.lowRatio = tot > 0 ? (float) (low / tot) : 0.0f;
    r.clickRatio = tot > 0 ? (float) (hi / tot) : 0.0f;
    const float hiRatio = tot > 0 ? (float) (hi2 / tot) : 0.0f;
    r.flatness = flatN > 0 && linSum > 0 ? (float) (std::exp (logSum / flatN) / (linSum / flatN)) : 0.0f;
    // pitch of the body and of the very start (for kick sweeps)
    const int bodyStart = std::min (n - 1, onset + (int) (0.012 * sr));
    r.pitchHz = detectPitchHz (x.data() + bodyStart, std::min (n - bodyStart, (int) (0.25 * sr)), sr, 25.0f, 1500.0f);
    r.startPitchHz = detectPitchHz (x.data() + onset, std::min (n - onset, (int) (0.03 * sr)), sr, 30.0f, 2000.0f);

    // role classification
    const float f0 = r.pitchHz, dec = r.decaySec, cen = r.centroidHz, flat = r.flatness;
    Role role = Role::Custom;
    const float sweep = (r.startPitchHz > 0.0f && f0 > 0.0f) ? r.startPitchHz / f0 : 1.0f;
    if (r.lowRatio > 0.45f && (f0 == 0.0f || f0 < 70.0f || (f0 < 130.0f && sweep > 1.4f))) role = Role::Kick;
    else if (r.bursts >= 3 && cen > 700.0f && cen < 4000.0f) role = Role::Clap;
    else if (cen > 5500.0f)
    {
        if (dec < 0.12f) role = Role::ClosedHat;
        else if (dec < 0.9f) role = (r.attackMs > 15.0f && dec < 0.35f) ? Role::Shaker : Role::OpenHat;
        else role = flat > 0.35f ? Role::Crash : Role::Ride;
    }
    else if (flat < 0.3f && cen > 700.0f && f0 > 250.0f && dec > 0.1f && hiRatio < 0.25f) role = Role::Cowbell;
    else if (dec < 0.07f && cen > 1200.0f) role = Role::Rim;
    else if (hiRatio > 0.015f && (f0 == 0.0f || (f0 > 100.0f && f0 < 400.0f)) && dec < 0.7f) role = Role::Snare;
    else if (flat > 0.25f && cen > 900.0f && dec < 0.6f) role = Role::Snare;
    else if (f0 > 0.0f && f0 < 450.0f && dec > 0.06f)
    {
        if (f0 < 100.0f) role = Role::TomLow;
        else if (f0 < 160.0f) role = Role::TomMid;
        else if (f0 < 240.0f) role = Role::TomHigh;
        else role = f0 < 360.0f ? Role::Conga : Role::Bongo;
    }
    else if (cen > 3000.0f) role = Role::Metal;
    r.role = (int) role;
    r.ok = true;
    r.summary = roleNames()[r.role] + " / " + (f0 > 0 ? String (f0, 1) + " Hz" : String ("unpitched")) + " / decay " + String (r.decaySec, 2)
                + " s / attack " + String (r.attackMs, 1) + " ms / centroid " + String (r.centroidHz / 1000.0f, 1) + " kHz / highs " + String (hiRatio * 100.0f, 1) + " % / noise " + String (roundToInt (r.flatness * 100)) + " %";
    return r;
}

void DrumDNA::apply (DrumEngineProcessor& p, int t, const DrumDnaResult& r, int mode)
{
    if (! r.ok) return;
    p.undo.beginNewTransaction ("Drum DNA");
    p.applyRoleDefaults (t, r.role, 4);
    auto set = [&] (int local, float v) { p.setTrackValue (t, local, v); };
    const float f0 = r.pitchHz, dec = std::max (0.02f, r.decaySec);
    switch ((Role) r.role)
    {
        case Role::Kick:
        {
            const float body = f0 > 0.0f ? f0 : 50.0f;
            set (T_A_P1, inverseExp (body, 34.0f, 92.0f));
            const float start = r.startPitchHz > body ? r.startPitchHz : body * 3.0f;
            set (T_A_P2, clamp01 (std::log2 (start / body) / 3.6f));
            set (T_A_P4, clamp01 (r.clickRatio * 8.0f));
            set (T_A_P6, inverseExp (dec / 3.0f, 0.05f, 0.9f));
            set (T_A_P8, inverseExp (dec / 3.0f, 0.08f, 1.6f));
            break;
        }
        case Role::Snare:
            if (f0 > 0) set (T_A_P1, inverseExp (f0, 140.0f, 330.0f));
            set (T_A_P2, clamp01 (r.flatness * 1.6f));
            set (T_A_P3, inverseExp (r.centroidHz, 900.0f, 7000.0f));
            set (T_A_P4, inverseExp (dec / 2.5f, 0.04f, 0.45f));
            break;
        case Role::Clap:
            set (T_A_P1, clamp01 ((r.bursts - 2) / 4.0f));
            set (T_A_P3, inverseExp (dec / 2.5f, 0.05f, 0.7f));
            set (T_A_P4, inverseExp (r.centroidHz, 600.0f, 2600.0f));
            break;
        case Role::ClosedHat: case Role::OpenHat: case Role::Ride: case Role::Crash:
        {
            float lo = 0.008f, hi = 0.14f;
            if (r.role == (int) Role::OpenHat) { lo = 0.09f; hi = 1.3f; }
            if (r.role == (int) Role::Ride) { lo = 0.35f; hi = 3.2f; }
            if (r.role == (int) Role::Crash) { lo = 0.5f; hi = 4.2f; }
            set (T_A_P4, inverseExp (dec / 3.0f, lo, hi));
            set (T_A_P1, inverseExp (r.centroidHz * 0.8f, 2500.0f, 11000.0f));
            set (T_A_P2, inverseExp (r.centroidHz, 3500.0f, 14000.0f));
            set (T_A_P3, clamp01 (1.0f - r.flatness * 1.4f));
            break;
        }
        case Role::TomLow: case Role::TomMid: case Role::TomHigh: case Role::Conga: case Role::Bongo:
        {
            float lo = 55.0f, hi = 140.0f;
            if (r.role == (int) Role::TomMid) { lo = 85.0f; hi = 210.0f; }
            if (r.role == (int) Role::TomHigh) { lo = 120.0f; hi = 310.0f; }
            if (r.role == (int) Role::Conga) { lo = 230.0f; hi = 460.0f; }
            if (r.role == (int) Role::Bongo) { lo = 340.0f; hi = 720.0f; }
            if (f0 > 0) set (T_A_P1, inverseExp (f0, lo, hi));
            set (T_A_P3, inverseExp (dec / 3.0f, 0.05f, 1.0f));
            break;
        }
        case Role::Cowbell:
            if (f0 > 0) set (T_A_P1, inverseExp (f0, 420.0f, 760.0f));
            set (T_A_P4, inverseExp (dec / 3.0f, 0.05f, 0.7f));
            break;
        case Role::Shaker:
            set (T_A_P1, inverseExp (r.centroidHz, 2800.0f, 11000.0f));
            set (T_A_P3, inverseExp (r.attackMs * 0.001f, 0.002f, 0.05f));
            set (T_A_P8, inverseExp (dec / 3.0f, 0.025f, 0.28f));
            break;
        default:
            if (f0 > 0) set (T_A_P1, inverseExp (f0, 40.0f, 2000.0f));
            set (T_DECAY, clamp01 (0.5f + std::log2 (dec / 0.9f) / 4.0f));
            break;
    }
    if (mode >= 1 && r.sample != nullptr)
    {
        p.setTrackSample (t, 0, r.sample);
        set (T_C_ON, 1.0f);
        set (T_C_LEVEL, mode == 2 ? 0.9f : 0.6f);
        set (T_A_LEVEL, mode == 2 ? 0.0f : 0.55f);
        if (mode == 2) set (T_A_ON, 0.0f);
    }
    p.setTrackName (t, r.sample != nullptr ? r.sample->name.substring (0, 14) : String());
}

// ===================================================================== Groove DNA
GrooveResult GrooveDNA::analyse (const File& f, double bpmHint)
{
    GrooveResult out;
    auto s = loadSampleFile (f, 40.0);
    if (s == nullptr) { out.summary = "Could not read that file"; return out; }
    const auto x = monoOf (*s);
    const double sr = s->sampleRate;
    const double len = (double) x.size() / sr;
    // onsets with strength
    auto on = detectTransients (x.data(), (int) x.size(), sr, 1024);
    if (on.size() < 4) { out.summary = "Not enough hits found in that loop"; return out; }
    std::vector<float> strength;
    for (auto o : on)
    {
        float pk = 0.0f;
        for (int i = o; i < std::min ((int) x.size(), o + (int) (0.03 * sr)); ++i) pk = std::max (pk, std::abs (x[(size_t) i]));
        strength.push_back (pk);
    }
    // tempo: whole bars between 80 and 165 BPM, otherwise the median inter-onset interval
    double bpm = bpmHint;
    if (bpm <= 0.0)
    {
        double best = 0.0, bestErr = 1.0e9;
        for (int bars : { 1, 2, 4, 8, 16 })
        {
            const double b = bars * 4.0 * 60.0 / len;
            if (b < 80.0 || b > 165.0) continue;
            const double err = std::abs (b - 124.0);
            if (err < bestErr) { bestErr = err; best = b; }
        }
        if (best <= 0.0)
        {
            std::vector<double> ioi;
            for (size_t i = 1; i < on.size(); ++i) ioi.push_back ((on[i] - on[i - 1]) / sr);
            std::sort (ioi.begin(), ioi.end());
            double beat = ioi[ioi.size() / 2];
            while (beat < 0.3) beat *= 2.0;
            while (beat > 0.75) beat *= 0.5;
            best = 60.0 / beat;
        }
        bpm = best;
    }
    const double sixteenth = 60.0 / bpm / 4.0;
    // the first onset defines the grid origin
    const double origin = on.front() / sr;
    double tSum[16] {}, vSum[16] {}; int cnt[16] {};
    float meanStrength = 0.0f;
    for (auto v : strength) meanStrength += v;
    meanStrength /= (float) strength.size();
    for (size_t i = 0; i < on.size(); ++i)
    {
        const double t = on[i] / sr - origin;
        const double g = t / sixteenth;
        const long long k = std::llround (g);
        const double dev = g - (double) k;
        if (std::abs (dev) > 0.45) continue;
        const int pos = (int) (((k % 16) + 16) % 16);
        tSum[pos] += dev; vSum[pos] += strength[i] / std::max (1.0e-6f, meanStrength) - 1.0f; cnt[pos]++;
    }
    out.groove.active = true;
    out.groove.name = f.getFileNameWithoutExtension();
    for (int i = 0; i < 16; ++i)
    {
        out.groove.timing[i] = cnt[i] > 0 ? (float) jlimit (-0.5, 0.5, tSum[i] / cnt[i]) : 0.0f;
        out.groove.velocity[i] = cnt[i] > 0 ? (float) jlimit (-1.0, 1.0, vSum[i] / cnt[i]) : 0.0f;
    }
    // remove the global offset so the grid stays anchored
    float mean = 0.0f; int used = 0;
    for (int i = 0; i < 16; i += 4) if (cnt[i] > 0) { mean += out.groove.timing[i]; ++used; }
    if (used > 0) for (auto& v : out.groove.timing) v -= mean / used;
    out.bpm = bpm;
    out.onsets = (int) on.size();
    out.bars = (int) std::round (len / (4.0 * 60.0 / bpm));
    out.ok = true;
    float swing = 0.0f; int sc = 0;
    for (int i = 1; i < 16; i += 2) if (cnt[i] > 0) { swing += out.groove.timing[i]; ++sc; }
    out.summary = String (out.onsets) + " hits / " + String (bpm, 1) + " BPM / " + String (out.bars) + " bars / offbeat push "
                  + String (sc > 0 ? roundToInt (swing / sc * 100.0f) : 0) + " % of a 16th";
    return out;
}

// ===================================================================== kick / bass
String KickBass::noteName (float hz)
{
    if (hz <= 0.0f) return "-";
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float note = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int n = roundToInt (note);
    const int cents = roundToInt ((note - (float) n) * 100.0f);
    return String (names[((n % 12) + 12) % 12]) + String (n / 12 - 1) + (cents != 0 ? (cents > 0 ? " +" : " ") + String (cents) + "c" : String());
}

float KickBass::tuneOffsetTo (float kickHz, float bassHz)
{
    if (kickHz <= 0.0f || bassHz <= 0.0f) return 0.0f;
    float semis = 12.0f * std::log2 (bassHz / kickHz);
    while (semis > 6.0f) semis -= 12.0f;
    while (semis < -6.0f) semis += 12.0f;
    return semis;
}

String KickBass::advice (float kickHz, float bassHz, float collision)
{
    String s;
    if (bassHz > 0.0f)
    {
        const float off = tuneOffsetTo (kickHz, bassHz);
        if (std::abs (off) < 0.3f) s << "Kick and bass share the root. ";
        else s << "Kick sits " << String (std::abs (off), 1) << " st " << (off > 0 ? "below" : "above") << " the bass root. ";
    }
    else s << "No bass detected (route a bass to the sidechain or open Bass Engine). ";
    if (collision > 0.45f) s << "Heavy low-end overlap: shorten the kick tail or duck the bass.";
    else if (collision > 0.2f) s << "Some overlap in the lows, ducking will clean it up.";
    else s << "Low end is clear.";
    return s;
}
} // namespace be

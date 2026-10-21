#include "DrumEngine.h"

namespace be
{
using namespace juce;

namespace
{
constexpr int kSub = 32;

inline uint32_t hash3 (uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline float h01 (uint32_t a, uint32_t b, uint32_t c) { return (float) (hash3 (a, b, c) >> 8) * (1.0f / 16777216.0f); }

inline float shapeCurve (float v, int curve)
{
    switch (curve)
    {
        case 1: return v * std::abs (v);
        case 2: return (v < 0.0f ? -1.0f : 1.0f) * std::sqrt (std::abs (v));
        case 3: { const float a = std::abs (v); const float s = a * a * (3.0f - 2.0f * a); return v < 0.0f ? -s : s; }
        default: return v;
    }
}

constexpr int kStripParams[] { T_DRV_TYPE, T_DRV_AMT, T_CRUSH, T_EQ_LOW, T_EQ_HIGH, T_COMP, T_GATE, T_TRANSIENT, T_FADER,
                               T_SEND1, T_SEND2, T_SEND3, T_SEND4, T_INVERT, T_PHASE };

double masterBeats (const Pattern& p) { return std::max (1, p.masterLength) * 0.25; }
}

DrumEngine::DrumEngine()
{
    for (auto& q : padQueue) q = 0.0f;
}

void DrumEngine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = std::max (block, 64);
    for (auto& tv : voices) for (auto& v : tv) v.prepare (sr);
    for (auto& st : strips) st.prepare (sr, kSub);
    for (auto& a : aux) a.prepare (sr, maxBlock);
    for (auto& g : groups) g.prepare (sr);
    master.prepare (sr, maxBlock);
    fxRack.prepare (sr, maxBlock);
    ducker.prepare (sr);
    scatter.prepare (sr);
    breakFilter.prepare (sr);
    loudness.prepare (sr);
    trackBuf.setSize (kNumTracks * 2, maxBlock);
    sendBuf.setSize (kNumAux * 2, maxBlock);
    groupBuf.setSize (kNumGroups * 2, maxBlock);
    mainBuf.setSize (2, maxBlock);
    duckGain.setSize (2, maxBlock);
    trackBuf.clear(); sendBuf.clear(); groupBuf.clear(); mainBuf.clear(); duckGain.clear();
    outDelay.setSize (kNumTracks * 2, 512);
    outDelay.clear();
    outDelayPos = 0;
    scratchL.assign ((size_t) maxBlock, 0.0f); scratchR.assign ((size_t) maxBlock, 0.0f);
    const float cr = (float) (sr / kSub);
    for (auto& l : glfo) { l.setRate (cr); }
    for (int t = 0; t < kNumTracks; ++t) for (int k = 0; k < kNumVLfos; ++k) { vlfo[t][k].setRate (cr); vlfo[t][k].seed ((uint32_t) (t * 31 + k * 7 + 1)); }
    for (int k = 0; k < kNumGLfos; ++k) glfo[k].seed ((uint32_t) (1000 + k));
    kickLp.set (150.0f, 0.7071f, (float) sr);
    for (auto& b : bassLp) b.set (150.0f, 0.7071f, (float) sr);
    reset();
}

void DrumEngine::reset()
{
    for (auto& tv : voices) for (auto& v : tv) v.choke (0.5f);
    for (auto& st : strips) st.reset();
    for (auto& a : aux) a.reset();
    master.reset();
    fxRack.reset();
    ducker.reset();
    numPending = 0;
    expectedBeat = -1.0;
    wasPlaying = false;
    std::memset (stripLocked, 0, sizeof (stripLocked));
}

void DrumEngine::triggerPad (int track, float velocity)
{
    if (track >= 0 && track < kNumTracks) padQueue[track] = jlimit (0.01f, 1.0f, velocity);
}

void DrumEngine::releasePad (int track)
{
    if (track >= 0 && track < kNumTracks) padQueue[track] = -1.0f;
}

void DrumEngine::addPending (const Pending& p)
{
    if (numPending < kMaxPending) pending[numPending++] = p;
}

// ===================================================================== modulation
float DrumEngine::routeValue (int src, int t) const
{
    if (src >= ModSrc::VLfo1 && src < ModSrc::VLfo1 + kNumVLfos) return vlfo[t][src - ModSrc::VLfo1].value();
    switch (src)
    {
        case ModSrc::Velocity: return tsrc[t].vel;
        case ModSrc::Accent: return tsrc[t].accent;
        case ModSrc::Random: return tsrc[t].random;
        case ModSrc::StepPitch: return tsrc[t].pitch;
        default: return srcGlobal[src];
    }
}

void DrumEngine::computeModulation (const EngineSnap& s, double beats, double bpm, bool playing, int)
{
    // global LFOs (rate / depth may be modulated by the previous control step)
    for (int k = 0; k < kNumGLfos; ++k)
    {
        float rateMul = 1.0f, depthAdd = 0.0f;
        const int rd = ModDst::GLfoRate1 + k, dd = ModDst::GLfoDepth1 + k;
        if (goffs[rd] != 0.0f && s.globalParam[rd] != nullptr)
        {
            const float base = s.glfo[k].rate;
            rateMul = s.globalParam[rd]->convertFrom0to1 (clamp01 (s.globalNorm[rd] + goffs[rd])) / std::max (0.001f, base);
        }
        if (goffs[dd] != 0.0f) depthAdd = goffs[dd];
        srcGlobal[ModSrc::GLfo1 + k] = glfo[k].tick (s.glfo[k], bpm, rateMul, depthAdd, playing, beats);
    }
    for (int m = 0; m < kNumMacros; ++m) srcGlobal[ModSrc::Macro1 + m] = s.macros[m];
    srcGlobal[ModSrc::Aftertouch] = aftertouch;
    srcGlobal[ModSrc::Pressure] = pressure;
    srcGlobal[ModSrc::ModWheel] = modWheel;
    srcGlobal[ModSrc::Fill] = telemetry.fillActive.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    srcGlobal[ModSrc::BarRamp] = (float) (std::fmod (std::max (0.0, beats), 4.0) / 4.0);
    srcGlobal[ModSrc::PatternRamp] = patternRamp;
    srcGlobal[ModSrc::XYX] = s.xyX;
    srcGlobal[ModSrc::XYY] = s.xyY;
    srcGlobal[ModSrc::SceneFade] = s.sceneFade;
    srcGlobal[ModSrc::Constant] = 1.0f;

    // voice LFOs run per track
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < kNumVLfos; ++k)
        {
            LfoSettings ls;
            ls.wave = (int) s.base[t][k == 0 ? T_VL1_WAVE : T_VL2_WAVE];
            ls.rate = s.base[t][k == 0 ? T_VL1_RATE : T_VL2_RATE];
            ls.trigger = 1;
            vlfo[t][k].tick (ls, bpm, 1.0f, 0.0f, false, beats);
        }

    std::memset (offs, 0, sizeof (offs));
    std::fill (std::begin (goffs), std::end (goffs), 0.0f);
    for (int r = 0; r < s.numRoutes; ++r)
    {
        const auto& R = s.routes[r];
        if (R.dst >= ModDst::TrackBase && R.dst < ModDst::GlobalStart)
        {
            const int p = R.dst - ModDst::TrackBase;
            if (R.trk == 0) for (int t = 0; t < kNumTracks; ++t) offs[t][p] += R.amt * shapeCurve (routeValue (R.src, t), R.curve);
            else offs[R.trk - 1][p] += R.amt * shapeCurve (routeValue (R.src, R.trk - 1), R.curve);
        }
        else if (R.dst >= ModDst::GlobalStart && R.dst < ModDst::Count)
            goffs[R.dst] += R.amt * shapeCurve (routeValue (R.src, R.trk > 0 ? R.trk - 1 : 0), R.curve);
    }
}

void DrumEngine::effective (const EngineSnap& s, int t, float* out) const
{
    for (int i = 0; i < T_COUNT; ++i)
    {
        const float o = offs[t][i];
        out[i] = (o != 0.0f && s.trackRange[i] != nullptr) ? s.trackRange[i]->convertFrom0to1 (clamp01 (s.norm[t][i] + o)) : s.base[t][i];
    }
}

// ===================================================================== sequencer
void DrumEngine::schedule (const EngineSnap& s, const SeqData* seq, double b0, double b1, double bpm, int n)
{
    if (seq == nullptr) return;
    const double spb = sr * 60.0 / bpm;
    bool anySolo = false;
    for (int t = 0; t < kNumTracks; ++t) anySolo |= s.base[t][T_SOLO] > 0.5f;

    // song arrangement lookup
    double songTotal = 0.0;
    if (s.songMode && seq->songLength > 0)
        for (int e = 0; e < seq->songLength; ++e) songTotal += seq->song[e].repeats * masterBeats (seq->pats[seq->song[e].pattern]);

    struct PatAt { int pattern; double since; double end; bool lastLoopKnown; };
    auto patternAt = [&] (double g) -> PatAt
    {
        if (songTotal > 0.0)
        {
            const double pos = std::fmod (std::max (0.0, g), songTotal);
            double acc = 0.0;
            for (int e = 0; e < seq->songLength; ++e)
            {
                const double len = seq->song[e].repeats * masterBeats (seq->pats[seq->song[e].pattern]);
                if (pos < acc + len || e == seq->songLength - 1)
                {
                    telemetry.songIndex.store (e, std::memory_order_relaxed);
                    return { seq->song[e].pattern, g - (pos - acc), g - (pos - acc) + len, true };
                }
                acc += len;
            }
        }
        if (queuedPattern >= 0 && g >= switchBeat) return { queuedPattern, switchBeat, 1.0e12, false };
        return { activePattern, patternActiveSince, queuedPattern >= 0 ? switchBeat : 1.0e12, queuedPattern >= 0 };
    };
    auto fillAt = [&] (double g)
    {
        if (s.fill) return true;
        if (s.fillEvery <= 0) return false;
        static const int every[] { 0, 2, 4, 8, 16 };
        const int N = every[jlimit (0, 4, s.fillEvery)];
        const int bar = (int) std::floor (g / 4.0 + 1.0e-9);
        return (bar % N) == N - 1;
    };
    telemetry.fillActive.store (fillAt (b0), std::memory_order_relaxed);
    const auto& groove = seq->groove;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const PatAt pa0 = patternAt (b0);
        const auto& tp0 = seq->pats[pa0.pattern].tracks[t];
        const double sb = seqResBeats (tp0.res);
        const long long k0 = (long long) std::floor (b0 / sb) - 2, k1 = (long long) std::floor (b1 / sb) + 2;
        for (long long k = std::max (0LL, k0); k <= k1; ++k)
        {
            const double g = (double) k * sb;
            const PatAt pa = patternAt (g);
            const auto& pat = seq->pats[pa.pattern];
            const auto& tp = pat.tracks[t];
            const double tsb = seqResBeats (tp.res);
            if (std::abs (tsb - sb) > 1.0e-9) continue;     // resolution changed with the pattern
            const int len = std::max (1, tp.length);
            const long long rel = (long long) std::floor ((g - pa.since) / sb + 1.0e-6);
            const int idx = (int) (((rel % len) + len) % len);
            Step st = tp.steps[idx];
            if (s.morph > 0.0f)
            {
                const int mt = jlimit (0, kNumPatterns - 1, s.morphTarget - 1);
                const auto& tb = seq->pats[mt].tracks[t];
                if (h01 (0xA11CEu, (uint32_t) t, (uint32_t) idx) < s.morph) st = tb.steps[idx % std::max (1, tb.length)];
            }
            if (! st.on) continue;

            // timing
            double tb = g + (double) st.micro / 100.0 * sb;
            const double six = g / 0.25;
            const bool on16 = std::abs (six - std::round (six)) < 1.0e-6;
            const int pos16 = on16 ? (int) (((long long) std::llround (six)) % 16) : -1;
            if (on16 && (pos16 & 1)) tb += s.swing * 0.1667;
            float velScale = 1.0f;
            if (groove.active && pos16 >= 0 && s.groove > 0.0f)
            {
                tb += groove.timing[pos16] * 0.25 * s.groove;
                velScale += groove.velocity[pos16] * 0.6f * s.groove;
            }
            if (s.humanize > 0.0f) tb += (h01 (salt, (uint32_t) t, (uint32_t) k) - 0.5f) * 2.0f * s.humanize * 0.025;
            if (tb < b0 || tb >= b1) continue;

            // conditions
            const double loopBeats = len * sb;
            const double countFrom = std::max (pa.since, playStartBeat);
            const int loopIdx = std::max (0, (int) std::floor ((g - countFrom) / loopBeats + 1.0e-9));
            const bool isLast = pa.end < 1.0e11 && (pa.since + (loopIdx + 1) * loopBeats >= pa.end - 1.0e-6);
            const bool fill = fillAt (g);
            bool ok = true;
            switch (st.cond)
            {
                case 1: ok = fill; break;
                case 2: ok = ! fill; break;
                case 3: ok = lastCond[t]; break;
                case 4: ok = ! lastCond[t]; break;
                case 5: ok = t > 0 && lastCond[t - 1]; break;
                case 6: ok = ! (t > 0 && lastCond[t - 1]); break;
                case 7: ok = loopIdx == 0; break;
                case 8: ok = loopIdx != 0; break;
                case 9: ok = isLast; break;
                case 10: ok = ! isLast; break;
                case 11: ok = (loopIdx % std::max (1, (int) st.condB)) == st.condA - 1; break;
                case 12: ok = (loopIdx % std::max (1, (int) st.condB)) != st.condA - 1; break;
                default: break;
            }
            if (ok && st.prob < 100) ok = h01 (salt ^ 0x5EEDu, (uint32_t) t, (uint32_t) k) * 100.0f < (float) st.prob;
            if (st.cond != 3 && st.cond != 4) lastCond[t] = ok;
            if (! ok) continue;
            if (s.base[t][T_MUTE] > 0.5f || (anySolo && s.base[t][T_SOLO] < 0.5f)) continue;
            if (s.brk && (int) s.base[t][T_ROLE] == (int) Role::Kick) continue;

            float vel = (float) st.vel / 127.0f * velScale;
            if (s.humanize > 0.0f) vel += (h01 (salt ^ 0x77u, (uint32_t) t, (uint32_t) k) - 0.5f) * 0.25f * s.humanize;
            vel = clamp01 (vel);
            const int hits = std::max (1, (int) st.retrig);
            const double gateBeats = std::min (sb * st.len / 4.0, 64.0);
            for (int h = 0; h < hits; ++h)
            {
                double hb = tb;
                float hv = vel;
                switch ((RetrigMode) st.retrigMode)
                {
                    case RetrigMode::Flam:
                        if (h > 0) hb = tb + (0.008 + 0.004 * hits) * bpm / 60.0;
                        if (h == 0 && hits > 1) hv = vel * 0.55f;
                        if (h > 1) continue;
                        break;
                    case RetrigMode::RollUp: hb = tb + sb * h / hits; hv = vel * (0.3f + 0.7f * (float) (h + 1) / (float) hits); break;
                    case RetrigMode::RollDown: hb = tb + sb * h / hits; hv = vel * (1.0f - 0.7f * (float) h / (float) hits); break;
                    default: hb = tb + sb * h / hits; break;
                }
                Pending p;
                p.time = clock + (juce::int64) std::llround ((hb - b0) * spb);
                p.track = t;
                p.vel = hv;
                p.accent = st.accent ? s.accent : 0.0f;
                p.pitch = (float) st.pitch;
                p.gate = (int) (gateBeats / hits * spb);
                p.fromSeq = true;
                p.step = st;
                p.hasStep = true;
                addPending (p);
            }
        }
    }
    juce::ignoreUnused (n);
    // pattern switch completes inside this block
    if (songTotal <= 0.0 && queuedPattern >= 0 && b1 > switchBeat)
    {
        activePattern = queuedPattern;
        patternActiveSince = switchBeat;
        queuedPattern = -1;
    }
    const PatAt cur = patternAt (b0);
    const double mb = masterBeats (seq->pats[cur.pattern]);
    patternRamp = (float) (std::fmod (std::max (0.0, b0 - cur.since), mb) / mb);
    telemetry.activePattern.store (cur.pattern, std::memory_order_relaxed);
    telemetry.patternSince.store (cur.since, std::memory_order_relaxed);
    telemetry.queuedPattern.store (queuedPattern, std::memory_order_relaxed);
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = seq->pats[cur.pattern].tracks[t];
        const double sb = seqResBeats (tp.res);
        const long long rel = (long long) std::floor ((b0 - cur.since) / sb + 1.0e-6);
        const int len = std::max (1, tp.length);
        telemetry.trackStep[t].store ((int) (((rel % len) + len) % len), std::memory_order_relaxed);
    }
}

// ===================================================================== trigger
void DrumEngine::trigger (const EngineSnap& s, int t, const Pending& p, int offset)
{
    // hit-dependent modulation sources
    tsrc[t].vel = p.vel;
    tsrc[t].accent = p.accent > 0.0f ? 1.0f : 0.0f;
    tsrc[t].random = h01 (salt, (uint32_t) t, (uint32_t) (clock + offset)) * 2.0f - 1.0f;
    tsrc[t].pitch = p.pitch / 24.0f;
    for (auto& l : vlfo[t]) { LfoSettings ls; ls.trigger = 1; l.trigger (ls, false); }
    // refresh this track's offsets so velocity / random routes see the new hit
    bool hitRoutes = false;
    for (int r = 0; r < s.numRoutes; ++r)
    {
        const int sc = s.routes[r].src;
        if (sc == ModSrc::Velocity || sc == ModSrc::Accent || sc == ModSrc::Random || sc == ModSrc::StepPitch) { hitRoutes = true; break; }
    }
    if (hitRoutes)
    {
        std::fill (std::begin (offs[t]), std::end (offs[t]), 0.0f);
        for (int r = 0; r < s.numRoutes; ++r)
        {
            const auto& R = s.routes[r];
            if (R.dst < ModDst::TrackBase || R.dst >= ModDst::GlobalStart) continue;
            if (R.trk != 0 && R.trk - 1 != t) continue;
            offs[t][R.dst - ModDst::TrackBase] += R.amt * shapeCurve (routeValue (R.src, t), R.curve);
        }
    }

    float P[T_COUNT];
    effective (s, t, P);
    bool locked[T_COUNT] {};
    float lockNorm[T_COUNT] {};
    if (p.hasStep)
        for (int i = 0; i < p.step.numLocks; ++i)
        {
            const int pi = p.step.locks[i].param;
            if (pi >= T_COUNT || s.trackRange[pi] == nullptr) continue;
            locked[pi] = true;
            lockNorm[pi] = p.step.locks[i].norm();
            P[pi] = s.trackRange[pi]->convertFrom0to1 (clamp01 (lockNorm[pi] + offs[t][pi]));
        }

    // choke groups
    const int choke = (int) P[T_CHOKE];
    if (choke > 0)
        for (int u = 0; u < kNumTracks; ++u)
            if (u != t && (int) s.base[u][T_CHOKE] == choke)
                for (auto& v : voices[u]) v.choke (4.0f);

    // voice allocation
    static const int polyMax[] { 1, 2, 4 };
    const int maxV = polyMax[jlimit (0, 2, (int) P[T_POLY])];
    auto& tv = voices[t];
    int active = 0;
    for (auto& v : tv) active += v.isActive() ? 1 : 0;
    while (active >= maxV)
    {
        DrumVoice* oldest = nullptr;
        for (auto& v : tv) if (v.isActive() && v.startedAt >= 0 && (oldest == nullptr || v.startedAt < oldest->startedAt)) oldest = &v;
        if (oldest == nullptr) break;
        oldest->choke (2.5f);
        oldest->startedAt = std::numeric_limits<juce::int64>::max() - 1;     // mark as fading
        --active;
    }
    DrumVoice* voice = nullptr;
    for (auto& v : tv) if (! v.isActive()) { voice = &v; break; }
    if (voice == nullptr)
    {
        for (auto& v : tv) if (voice == nullptr || v.startedAt > voice->startedAt) voice = &v;   // the fading one with the largest mark
    }

    // velocity curve
    const float vc = std::exp2 (-s.velCurve * 1.5f);
    HitInfo h;
    h.velocity = std::pow (clamp01 (p.vel), vc);
    h.accent = p.accent;
    h.pitch = p.pitch;
    h.gateSamples = p.gate;
    h.seed = hash3 (salt, (uint32_t) t, (uint32_t) (clock + offset));
    h.rrIndex = rrCounter[t]++;

    float w[4] {};
    const float c = clampf (P[T_CIRCUIT], 0.0f, 3.0f);
    const int a = std::min (2, (int) c);
    const float f = c - (float) a;
    w[a] = 1.0f - f; w[a + 1] = f;

    voice->start (t, P, h, s.assets[t], offset, w);
    voice->startedAt = clock + offset;
    std::copy (std::begin (locked), std::end (locked), voice->locked);
    std::copy (std::begin (lockNorm), std::end (lockNorm), voice->lockNorm);

    // track-level locks latch until the next hit
    for (int sp : kStripParams) { stripLocked[t][sp] = locked[sp]; stripLock[t][sp] = lockNorm[sp]; }

    telemetry.trackHits[t].fetch_add (1, std::memory_order_relaxed);
    if (! p.fromSeq && s.bridgeOn && t == s.bridgeTrack) { liveKickVel = p.vel; liveKickCount.fetch_add (1); }
}

// ===================================================================== block
void DrumEngine::process (const EngineSnap& s, const SeqData* seq, const HostInfo& host, const MidiBuffer& midi, EngineOutputs& out, int n)
{
    ScopedNoDenormals nd;
    jassert (n <= maxBlock);
    n = std::min (n, maxBlock);

    // ------------------------------------------------ transport
    const double bpm = jlimit (20.0, 400.0, host.valid ? host.bpm : (double) s.bpm);
    bool playing = false;
    double b0 = 0.0;
    // with "follow" off the pattern stays quiet under the DAW (a clip in the song plays Hamster instead)
    if (host.valid && host.playing && s.followHost) { playing = true; b0 = host.beats; }
    else if (s.play) { playing = true; b0 = internalBeats; }
    const double spb = sr * 60.0 / bpm;
    const double b1 = b0 + n / spb;
    if (playing && (! wasPlaying || std::abs (b0 - expectedBeat) > 0.02))
    {
        // start or relocation: drop scheduled sequencer hits
        int k = 0;
        for (int i = 0; i < numPending; ++i) if (! pending[i].fromSeq) pending[k++] = pending[i];
        numPending = k;
        salt = hash3 (salt, (uint32_t) clock, 0xBEEFu);
        if (! wasPlaying) { playStartBeat = std::floor (b0 / 4.0 + 1.0e-9) * 4.0; for (auto& c : lastCond) c = false; }
    }
    if (! playing)
    {
        int k = 0;
        for (int i = 0; i < numPending; ++i) if (! pending[i].fromSeq) pending[k++] = pending[i];
        numPending = k;
        internalBeats = 0.0;
    }
    else if (! (host.valid && host.playing)) internalBeats = b1;
    expectedBeat = b1;
    wasPlaying = playing;
    beatPos = b0;
    telemetry.playing.store (playing, std::memory_order_relaxed);
    telemetry.beat.store (b0, std::memory_order_relaxed);

    // ------------------------------------------------ pattern selection
    if (seq != nullptr && ! s.songMode)
    {
        const int wanted = jlimit (0, kNumPatterns - 1, s.pattern);
        if (wanted != activePattern)
        {
            if (! playing) { activePattern = wanted; queuedPattern = -1; patternActiveSince = 0.0; }
            else if (queuedPattern != wanted)
            {
                queuedPattern = wanted;
                const double mb = masterBeats (seq->pats[activePattern]);
                switchBeat = std::ceil ((b0 - patternActiveSince) / mb - 1.0e-9) * mb + patternActiveSince;
                if (switchBeat < b0) switchBeat += mb;
            }
        }
        else queuedPattern = -1;
    }

    // ------------------------------------------------ pads
    for (int t = 0; t < kNumTracks; ++t)
    {
        const float v = padQueue[t].exchange (0.0f);
        if (v > 0.0f)
        {
            Pending p; p.time = clock; p.track = t; p.vel = v; p.fromSeq = false; p.gate = -1;
            addPending (p);
            heldVel[t] = v; padDown[t] = true;
            if (s.record && playing && seq != nullptr && recordFifo.getFreeSpace() > 0)
            {
                const auto& tp = seq->pats[activePattern].tracks[t];
                const double sb = seqResBeats (tp.res);
                const long long k = (long long) std::llround ((b0 - patternActiveSince) / sb);
                auto sc = recordFifo.write (1);
                if (sc.blockSize1 > 0) recordBuf[sc.startIndex1] = { t, (int) (((k % tp.length) + tp.length) % tp.length), roundToInt (v * 127.0f), activePattern };
            }
        }
        else if (v < 0.0f)
        {
            heldVel[t] = 0.0f; padDown[t] = false;
            for (auto& vv : voices[t]) vv.noteOff();
        }
    }

    // ------------------------------------------------ MIDI input
    int duckMidi[64]; int numDuckMidi = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = jlimit (0, n - 1, meta.samplePosition);
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            const float v = m.getFloatVelocity();
            if (s.duck.source == (int) DuckSource::MidiNote && note == s.duck.note
                && (s.duck.channel == 0 || s.duck.channel == 16 || m.getChannel() == s.duck.channel) && numDuckMidi < 64)
                duckMidi[numDuckMidi++] = pos;
            for (int t = 0; t < kNumTracks; ++t)
            {
                if ((int) s.base[t][T_NOTE] != note) continue;
                Pending p; p.time = clock + pos; p.track = t; p.vel = v; p.fromSeq = false; p.gate = -1;
                addPending (p);
                heldVel[t] = v;
                if (s.record && playing && seq != nullptr && recordFifo.getFreeSpace() > 0)
                {
                    const auto& tp = seq->pats[activePattern].tracks[t];
                    const double sb = seqResBeats (tp.res);
                    const double tb = b0 + pos / spb;
                    const long long k = (long long) std::llround ((tb - patternActiveSince) / sb);
                    auto sc = recordFifo.write (1);
                    if (sc.blockSize1 > 0) recordBuf[sc.startIndex1] = { t, (int) (((k % tp.length) + tp.length) % tp.length), roundToInt (v * 127.0f), activePattern };
                }
            }
        }
        else if (m.isNoteOff())
        {
            for (int t = 0; t < kNumTracks; ++t)
                if ((int) s.base[t][T_NOTE] == m.getNoteNumber()) { heldVel[t] = 0.0f; for (auto& vv : voices[t]) vv.noteOff(); }
        }
        else if (m.isController() && m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure()) pressure = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch()) aftertouch = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& tv : voices) for (auto& v : tv) v.choke (3.0f); for (auto& h : heldVel) h = 0.0f; }
    }

    // ------------------------------------------------ note repeat
    if (s.noteRepeat > 0)
    {
        static const double div[] { 0.0, 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        const double d = div[jlimit (0, 6, s.noteRepeat)];
        const double c0 = playing ? b0 : freeBeats, c1 = c0 + n / spb;
        for (double g = std::ceil (c0 / d - 1.0e-9) * d; g < c1; g += d)
            for (int t = 0; t < kNumTracks; ++t)
                if (heldVel[t] > 0.0f && g > c0 + 1.0e-6)
                {
                    Pending p; p.time = clock + (juce::int64) ((g - c0) * spb); p.track = t; p.vel = heldVel[t]; p.fromSeq = false; p.gate = (int) (d * spb * 0.5);
                    addPending (p);
                }
    }
    freeBeats += n / spb;

    // ------------------------------------------------ sequencer
    if (playing) schedule (s, seq, b0, b1, bpm, n);
    else telemetry.fillActive.store (s.fill, std::memory_order_relaxed);

    // ------------------------------------------------ ducking gain curve
    const bool duckMasterLow = s.duck.band == 1 && s.duckTarget == 0;
    {
        if (s.duck.source == (int) DuckSource::Track)
            for (int i = 0; i < numPending; ++i)
                if (pending[i].track == s.duckTrack && pending[i].time < clock + n) ducker.trigger ((int) std::max<juce::int64> (0, pending[i].time - clock));
        for (int i = 0; i < numDuckMidi; ++i) ducker.trigger (duckMidi[i]);
    }
    auto* dg = duckGain.getWritePointer (0);
    auto* dg2 = duckGain.getWritePointer (1);
    const bool ducking = s.duck.source != (int) DuckSource::Off;
    if (ducking && ! duckMasterLow)
    {
        std::fill (dg, dg + n, 1.0f); std::fill (dg2, dg2 + n, 1.0f);
        DuckSettings ds = s.duck; ds.band = 0;
        ducker.process (ds, dg, dg2, n, bpm, b0, playing, out.scL, out.scR, goffs[0] * 0.0f);
    }
    telemetry.duckGr.store (ducking ? ducker.gainReductionDb() : 0.0f, std::memory_order_relaxed);

    // ------------------------------------------------ render tracks in control blocks
    for (int c = 0; c < kNumTracks * 2; ++c) trackBuf.clear (c, 0, n);
    for (int c = 0; c < kNumAux * 2; ++c) sendBuf.clear (c, 0, n);
    bool anySolo = false;
    for (int t = 0; t < kNumTracks; ++t) anySolo |= s.base[t][T_SOLO] > 0.5f;
    bool audible[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) audible[t] = ! (s.base[t][T_MUTE] > 0.5f || (anySolo && s.base[t][T_SOLO] < 0.5f));
    for (int s0 = 0; s0 < n; s0 += kSub)
    {
        const int len = std::min (kSub, n - s0);
        computeModulation (s, b0 + s0 / spb, bpm, playing, len);
        // fire events due in this control block (in time order)
        const juce::int64 end = clock + s0 + len;
        for (;;)
        {
            int best = -1;
            for (int i = 0; i < numPending; ++i)
                if (pending[i].time < end && (best < 0 || pending[i].time < pending[best].time)) best = i;
            if (best < 0) break;
            const Pending p = pending[best];
            pending[best] = pending[--numPending];
            const int off = (int) jlimit<juce::int64> (0, len - 1, p.time - (clock + s0));
            trigger (s, p.track, p, off);
        }
        for (int t = 0; t < kNumTracks; ++t)
        {
            float* L = trackBuf.getWritePointer (t * 2) + s0;
            float* R = trackBuf.getWritePointer (t * 2 + 1) + s0;
            for (auto& v : voices[t])
            {
                if (! v.isActive()) continue;
                float cont[kNumCont];
                for (int k = 0; k < kNumCont; ++k)
                {
                    const int pi = kContParams[k];
                    const float o = offs[t][pi];
                    if (! v.locked[pi] && o == 0.0f) cont[k] = s.base[t][pi];
                    else cont[k] = s.trackRange[pi]->convertFrom0to1 (clamp01 ((v.locked[pi] ? v.lockNorm[pi] : s.norm[t][pi]) + o));
                }
                v.setContinuous (cont);
                v.render (L, R, len);
            }
            // insert chain with latched locks and modulation
            auto val = [&] (int pi)
            {
                const float o = offs[t][pi];
                if (! stripLocked[t][pi] && o == 0.0f) return s.base[t][pi];
                return s.trackRange[pi]->convertFrom0to1 (clamp01 ((stripLocked[t][pi] ? stripLock[t][pi] : s.norm[t][pi]) + o));
            };
            StripSettings st;
            st.drvType = (int) val (T_DRV_TYPE); st.drive = val (T_DRV_AMT); st.crush = val (T_CRUSH);
            st.eqLow = val (T_EQ_LOW); st.eqHigh = val (T_EQ_HIGH); st.comp = val (T_COMP); st.gate = val (T_GATE);
            st.transient = val (T_TRANSIENT); st.faderDb = val (T_FADER); st.invert = val (T_INVERT) > 0.5f; st.phaseMs = val (T_PHASE);
            strips[t].process (st, L, R, len, s.quality);
            for (int a = 0; a < kNumAux; ++a)
            {
                const float sl = val (T_SEND1 + a);
                if (sl <= 1.0e-4f) continue;
                float* aL = sendBuf.getWritePointer (a * 2) + s0;
                float* aR = sendBuf.getWritePointer (a * 2 + 1) + s0;
                if (! audible[t]) continue;
                for (int i = 0; i < len; ++i) { aL[i] += L[i] * sl; aR[i] += R[i] * sl; }
            }
        }
    }
    clock += n;

    // ------------------------------------------------ mix
    float* mL = mainBuf.getWritePointer (0);
    float* mR = mainBuf.getWritePointer (1);
    std::fill (mL, mL + n, 0.0f); std::fill (mR, mR + n, 0.0f);
    for (int g = 0; g < kNumGroups * 2; ++g) groupBuf.clear (g, 0, n);
    const int lat = master.latency();
    for (int t = 0; t < kNumTracks; ++t)
    {
        float* L = trackBuf.getWritePointer (t * 2);
        float* R = trackBuf.getWritePointer (t * 2 + 1);
        const bool silent = ! audible[t];
        float pk = 0.0f;
        if (silent) { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); }
        else
        {
            if (ducking && ! duckMasterLow && s.duckTarget == 1 && t != s.duckTrack)
                for (int i = 0; i < n; ++i) { L[i] *= dg[i]; R[i] *= dg[i]; }
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        lastPeak[t] = std::max (pk, lastPeak[t] * 0.85f);
        telemetry.trackPeak[t].store (lastPeak[t], std::memory_order_relaxed);
        telemetry.trackGr[t].store (strips[t].gainReduction, std::memory_order_relaxed);

        const int o = (int) s.base[t][T_OUTPUT];
        if (o > 0 && out.outL[o - 1] != nullptr)
        {
            // direct output, delayed to stay aligned with the limiter look-ahead on the main bus
            float* dL = outDelay.getWritePointer (t * 2);
            float* dR = outDelay.getWritePointer (t * 2 + 1);
            const int dsz = outDelay.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                const int w = (outDelayPos + i) % dsz;
                const int r = (w - lat + dsz) % dsz;
                dL[w] = L[i]; dR[w] = R[i];
                out.outL[o - 1][i] += dL[r];
                out.outR[o - 1][i] += dR[r];
            }
            continue;
        }
        const int g = (int) s.base[t][T_GROUP];
        float* tL = g > 0 ? groupBuf.getWritePointer ((g - 1) * 2) : mL;
        float* tR = g > 0 ? groupBuf.getWritePointer ((g - 1) * 2 + 1) : mR;
        for (int i = 0; i < n; ++i) { tL[i] += L[i]; tR[i] += R[i]; }
    }
    outDelayPos = (outDelayPos + n) % outDelay.getNumSamples();

    // groups
    for (int g = 0; g < kNumGroups; ++g)
    {
        float* L = groupBuf.getWritePointer (g * 2);
        float* R = groupBuf.getWritePointer (g * 2 + 1);
        groups[g].process (s.grp[g], L, R, n);
        telemetry.groupGr[g].store (groups[g].gainReduction, std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { mL[i] += L[i]; mR[i] += R[i]; }
    }
    // aux returns
    for (int a = 0; a < kNumAux; ++a)
    {
        float* L = sendBuf.getWritePointer (a * 2);
        float* R = sendBuf.getWritePointer (a * 2 + 1);
        AuxSettings as = s.aux[a];
        const int d = ModDst::Aux1Return + a;
        if (goffs[d] != 0.0f && s.globalParam[d] != nullptr) as.ret = s.globalParam[d]->convertFrom0to1 (clamp01 (s.globalNorm[d] + goffs[d]));
        aux[a].process (as, L, R, n, bpm);
        if (ducking && ! duckMasterLow && s.duckTarget == 2) for (int i = 0; i < n; ++i) { L[i] *= dg[i]; R[i] *= dg[i]; }
        telemetry.auxLevel[a].store (aux[a].level, std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { mL[i] += L[i]; mR[i] += R[i]; }
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
    }

    // ------------------------------------------------ kick / bass collision analysis
    {
        const int kt = jlimit (0, kNumTracks - 1, s.bridgeTrack);
        const float* kL = trackBuf.getReadPointer (kt * 2);
        const float rel = std::exp (-1.0f / (0.03f * (float) sr));
        float kAcc = 0.0f, bAcc = 0.0f, oAcc = 0.0f;
        const float bassExt = bassEnvExternal.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            kickLp.tick (kL[i]);
            const float kx = std::abs (kickLp.lp);
            kickEnv = std::max (kx, kickEnv * rel);
            float bx = 0.0f;
            if (out.scL != nullptr) { bassLp[0].tick (0.5f * (out.scL[i] + (out.scR != nullptr ? out.scR[i] : out.scL[i]))); bx = std::abs (bassLp[0].lp); }
            bassEnv = std::max (bx, bassEnv * rel);
            const float be = std::max (bassEnv, bassExt);
            kAcc += kickEnv; bAcc += be; oAcc += std::min (kickEnv, be);
        }
        const float decay = std::exp (-(float) n / (float) (sr * 1.5));
        collisionK = collisionK * decay + kAcc;
        collisionB = collisionB * decay + oAcc;
        telemetry.collision.store (collisionK > 1.0e-6f ? clamp01 (collisionB / collisionK) : 0.0f, std::memory_order_relaxed);
        telemetry.kickLow.store (kickEnv, std::memory_order_relaxed);
        telemetry.bassLow.store (std::max (bassEnv, bassExt), std::memory_order_relaxed);
        const float kv = kickEnv, bv = std::max (bassEnv, bassExt);
        telemetry.kickLowFifo.push (&kv, 1);
        telemetry.bassLowFifo.push (&bv, 1);
        juce::ignoreUnused (bAcc);
    }

    // ------------------------------------------------ master chain
    breakFilter.process (s.brk, mL, mR, n);
    if (ducking && duckMasterLow) ducker.process (s.duck, mL, mR, n, bpm, b0, playing, out.scL, out.scR, 0.0f);
    else if (ducking && s.duckTarget == 0) for (int i = 0; i < n; ++i) { mL[i] *= dg[i]; mR[i] *= dg[i]; }
    {
        FxSettings fx = s.fx;
        auto modFx = [&] (int d, float& v)
        {
            if (goffs[d] != 0.0f && s.globalParam[d] != nullptr) v = s.globalParam[d]->convertFrom0to1 (clamp01 (s.globalNorm[d] + goffs[d]));
        };
        modFx (ModDst::ReverbMix, fx.rvMix);
        modFx (ModDst::DelayMix, fx.dlMix);
        modFx (ModDst::AutoFilterCutoff, fx.afCutoff);
        fxRack.process (fx, mL, mR, n, bpm, b0, playing, s.keyHz, nullptr);
    }
    scatter.process (s.scatter, mL, mR, n, bpm, b0, playing);
    {
        MasterSettings ms = s.master;
        auto modM = [&] (int d, float& v)
        {
            if (goffs[d] != 0.0f && s.globalParam[d] != nullptr) v = s.globalParam[d]->convertFrom0to1 (clamp01 (s.globalNorm[d] + goffs[d]));
        };
        modM (ModDst::MasterVolume, ms.volume);
        modM (ModDst::GlueMix, ms.glueMix);
        master.process (ms, mL, mR, n);
    }
    telemetry.glueGr.store (master.glueGr, std::memory_order_relaxed);
    telemetry.limGr.store (master.limGr, std::memory_order_relaxed);
    telemetry.truePeak.store (std::max (master.truePeak, telemetry.truePeak.load() * 0.9f), std::memory_order_relaxed);
    loudness.process (mL, mR, n);
    loudness.truePeakMax.store (std::max (loudness.truePeakMax.load(), master.truePeak), std::memory_order_relaxed);
    {
        float pl = 0, pr = 0;
        for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (mL[i])); pr = std::max (pr, std::abs (mR[i])); scratchL[(size_t) i] = 0.5f * (mL[i] + mR[i]); }
        telemetry.masterPeakL.store (std::max (pl, telemetry.masterPeakL.load() * 0.85f), std::memory_order_relaxed);
        telemetry.masterPeakR.store (std::max (pr, telemetry.masterPeakR.load() * 0.85f), std::memory_order_relaxed);
        telemetry.scope.push (scratchL.data(), n);
        telemetry.spectrumL.push (mL, n);
        telemetry.spectrumR.push (mR, n);
    }
    for (int i = 0; i < n; ++i) { out.mainL[i] += mL[i]; out.mainR[i] += mR[i]; }

    // modulation telemetry
    for (int t = 0; t < kNumTracks; ++t) for (int p = 0; p < T_COUNT; ++p) telemetry.mod[t][p].store (offs[t][p], std::memory_order_relaxed);
    for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d) telemetry.gmod[d].store (goffs[d], std::memory_order_relaxed);
    const int sel = jlimit (0, kNumTracks - 1, telemetry.selectedTrack.load (std::memory_order_relaxed));
    for (int k = 1; k < ModSrc::Count; ++k) telemetry.src[k].store (routeValue (k, sel), std::memory_order_relaxed);
}
} // namespace be

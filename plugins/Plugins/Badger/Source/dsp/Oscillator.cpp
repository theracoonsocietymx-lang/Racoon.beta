#include "Oscillator.h"
#include "RacoonDsp.h"

namespace be
{
const EngineParamInfo& engineParamInfo (int engine)
{
    static const EngineParamInfo info[] {
        { "PULSE W", "MORPH", "SOFTEN", 0.5f, 0.0f, 0.0f },     // VA
        { "BEND", "MIRROR", "STEPS", 0.5f, 0.0f, 0.0f },        // Wavetable
        { "INDEX", "RATIO", "FEEDBACK", 0.3f, 0.0f, 0.0f },     // FM
        { "AMOUNT", "SHAPE", "RESO", 0.5f, 0.0f, 0.0f },        // Phase distortion
        { "TILT", "EVEN CUT", "ODD CUT", 0.5f, 0.0f, 0.0f },    // Additive
        { "SOURCE", "INTENSITY", "BLUR", 0.0f, 0.5f, 0.0f },    // Spectral
        { "SIZE", "DENSITY", "SPREAD", 0.5f, 0.5f, 0.2f },      // Granular
        { "LOOP ST", "LOOP LEN", "XFADE", 0.3f, 0.0f, 0.3f },   // Sample
        { "DAMPING", "EXCITER", "BODY", 0.5f, 0.0f, 0.3f },     // Physical
        { "SQUARE", "BIAS", "DRIVE", 0.0f, 0.5f, 0.3f },        // Acid circuit
    };
    return info[juce::jlimit (0, 9, engine)];
}

void Oscillator::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxDelay = (int) (sampleRate / 18.0) + 8;
    for (int i = 0; i < 2; ++i) uni[i].ks.prepare (maxDelay);
    racoon::sdsp::SincTable::get();   // sample engine kernels, built here and not on the audio thread
    exScratch.assign ((size_t) maxDelay + 8, 0.0f);   // the pluck is shaped here, so a note never allocates on the audio thread
    for (int i = 0; i < kMaxUnison; ++i) uni[i].rng.seed (0x1234567u + (uint32_t) i * 7919u);
}

void Oscillator::exciteString (const OscSettings& s, Uni& u, float noteHz)
{
    u.ks.clear();
    const int N = jlimit (2, (int) u.ks.buf.size() - 8, (int) (sr / std::max (noteHz, 18.0f)));
    const float exciter = pb;              // 0 pluck .. 1 bow
    const float pickPos = 0.05f + pos * 0.45f;
    const int comb = std::max (1, (int) (pickPos * (float) N));
    if ((int) exScratch.size() < N) return;   // only if prepare was never called
    auto& ex = exScratch;
    std::fill (ex.begin(), ex.begin() + N, 0.0f);
    float lp = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float noise = u.rng.bi();
        const float t = (float) i / (float) N;
        float e;
        if (exciter < 0.33f)        // pluck: filtered noise
        {
            lp += (0.25f + (0.33f - exciter) * 2.0f) * (noise - lp);
            e = lp;
        }
        else if (exciter < 0.66f)   // thumb / mallet: smooth pulse
            e = std::sin (kPi * clamp01 (t * 4.0f)) * (t < 0.25f ? 1.0f : 0.0f);
        else e = noise * 0.3f;      // bow: low initial, continuous injection
        ex[(size_t) i] = e;
    }
    // pick-position comb
    for (int i = N - 1; i >= comb; --i) ex[(size_t) i] -= ex[(size_t) (i - comb)];
    for (int i = 0; i < N; ++i) u.ks.push (ex[(size_t) i]);
    u.ksLp = 0.0f; u.ksPrev = 0.0f;
    u.exciteLeft = exciter >= 0.66f ? 1 << 30 : 0;
}

void Oscillator::noteOn (const OscSettings& s, float noteHz, uint32_t seed, bool legato)
{
    grng.seed (seed * 31u + 7u);
    voices = (s.engine == (int) Engine::Physical || s.engine == (int) Engine::Granular) ? std::min (s.unison, 2) : s.unison;
    for (int v = 0; v < kMaxUnison; ++v)
    {
        auto& u = uni[v];
        u.rng.seed (seed * 2654435761u + (uint32_t) v * 97u + 1u);
        u.slopCents = u.rng.bi() * s.slop * 30.0f;
        if (! legato)
        {
            if (s.reset)
            {
                u.phase = s.startPhase + s.randPhase * u.rng.uni() + (voices > 1 && v > 0 ? s.randPhase * 0.0f + u.rng.uni() * 0.999f * (s.randPhase > 0.0f ? 1.0f : 0.0f) : 0.0f);
                if (voices > 1 && s.randPhase <= 0.0f) u.phase = s.startPhase + (float) v / (float) voices * 0.37f;   // decorrelate unison
                u.phase -= std::floor (u.phase);
                u.master = u.phase;
                u.fmPhase = 0.0f;
                u.fmMod = 0.0f;
            }
            u.play = 0.0;
            u.dir = 1.0f;
            u.lp = 0.0f;
        }
        if (! legato && s.engine == (int) Engine::Physical && v < 2) exciteString (s, u, noteHz);
    }
    if (! legato && s.engine == (int) Engine::Sample && s.sample != nullptr)
        for (int v = 0; v < kMaxUnison; ++v) uni[v].play = (double) pos * (double) s.sample->data.size();
    if (! legato)
        for (auto& g : grains) g.on = false;
}

void Oscillator::setControl (const OscSettings& s, const OscMod& m, double a4, float impDrift)
{
    targetLevel = m.level;
    pan = m.pan; pos = clamp01 (m.pos); pa = clamp01 (m.pa); pb = clamp01 (m.pb);
    fold = clamp01 (m.fold); shapeAmt = clamp01 (m.shape); xmodAmt = clamp01 (m.xmod); detune = clamp01 (m.detune);

    const double hz = a4 * std::pow (2.0, (m.pitchSemis - 69.0) / 12.0);
    baseInc = (float) (hz / sr);
    voices = (s.engine == (int) Engine::Physical || s.engine == (int) Engine::Granular) ? std::min (s.unison, 2) : s.unison;
    level0 = WaveTable::levelFor (baseInc * (s.syncMode ? s.syncRatio : 1.0f));

    const float maxCents = 100.0f;
    for (int v = 0; v < voices; ++v)
    {
        auto& u = uni[v];
        // slow drift random walk (per control step)
        const float driftAmt = s.drift + impDrift;
        if (u.rng.uni() < 0.01f) u.driftTarget = u.rng.bi();
        u.driftVal += 0.002f * (u.driftTarget - u.driftVal);
        float cents = u.driftVal * driftAmt * 14.0f + u.slopCents;
        float panPos = 0.0f;
        if (voices > 1)
        {
            const float x = 2.0f * (float) v / (float) (voices - 1) - 1.0f;     // -1..1
            const float curved = x * (0.6f + 0.4f * std::abs (x));
            cents += curved * detune * detune * maxCents;
            panPos = x * s.spread;
        }
        u.inc = baseInc * std::exp2 (cents / 1200.0f);
        // gain: blend centre vs sides
        float g = 1.0f;
        if (voices > 2)
        {
            const bool centre = std::abs (2.0f * (float) v / (float) (voices - 1) - 1.0f) < 0.34f;
            g = centre ? 1.0f - s.blend * 0.5f : 0.3f + s.blend * 0.7f;
        }
        const float p = clampf (pan + panPos, -1.0f, 1.0f);
        u.gainL = g * std::sqrt (0.5f * (1.0f - p)) * 1.41421f;
        u.gainR = g * std::sqrt (0.5f * (1.0f + p)) * 1.41421f;
    }
}

void Oscillator::spawnGrain (const OscSettings& s)
{
    if (s.sample == nullptr || s.sample->data.empty()) return;
    for (auto& g : grains)
    {
        if (g.on) continue;
        const int n = (int) s.sample->data.size();
        const float spread = s.pc;
        const float p = clamp01 (pos + grng.bi() * spread * 0.25f);
        const float sizeSec = 0.005f + pa * pa * 0.25f;
        g.on = true;
        g.t = 0;
        g.len = std::max (16, (int) (sizeSec * (float) sr));
        g.pos = (double) p * (double) (n - 1);
        // pitch: note relative to sample root
        const double rootHz = 440.0 * std::pow (2.0, (s.sample->rootNote - 69.0) / 12.0);
        g.rate = ((double) baseInc * sr / rootHz) * (s.sample->sampleRate / sr) * std::exp2 (grng.bi() * spread * 0.05);
        const float pn = clampf (pan + grng.bi() * spread, -1.0f, 1.0f);
        g.panL = std::sqrt (0.5f * (1.0f - pn)) * 1.41f;
        g.panR = std::sqrt (0.5f * (1.0f + pn)) * 1.41f;
        return;
    }
}

float Oscillator::renderUnison (const OscSettings& s, Uni& u, float pmIn)
{
    const auto engine = (Engine) s.engine;
    float dt = u.inc;
    float y = 0.0f;

    // --- master/slave sync
    float ph = u.phase;
    if (s.syncMode != 0 && engine != Engine::Sample && engine != Engine::Physical)
    {
        u.master += dt;
        bool wrapped = false;
        if (u.master >= 1.0f) { u.master -= 1.0f; wrapped = true; }
        const float sdt = dt * s.syncRatio;
        if (s.syncMode == 1)        // hard
        {
            u.phase += sdt;
            if (wrapped) u.phase = u.master * s.syncRatio;
        }
        else                        // soft (reverse)
        {
            if (wrapped) u.dir = -u.dir;
            u.phase += sdt * u.dir;
        }
        u.phase -= std::floor (u.phase);
        ph = u.phase;
        dt = sdt;
    }
    else
    {
        ph = u.phase;
        u.phase += dt;
        if (u.phase >= 1.0f) u.phase -= 1.0f;
    }
    // phase modulation from x-mod
    float p = ph + pmIn;
    p -= std::floor (p);

    switch (engine)
    {
        case Engine::VA:
        {
            auto wave = [&] (int w) -> float
            {
                switch ((VAWave) w)
                {
                    case VAWave::Sine: return std::sin (kTwoPi * p);
                    case VAWave::Triangle:
                    {
                        float t = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
                        t += 4.0f * dt * (polyBlamp (p, dt) - polyBlamp (std::fmod (p + 0.5f, 1.0f), dt));
                        return t;
                    }
                    case VAWave::Saw: return 2.0f * p - 1.0f - polyBlep (p, dt);
                    case VAWave::Square: case VAWave::Pulse:
                    {
                        const float pw = w == (int) VAWave::Square ? 0.5f : 0.03f + pa * 0.94f;
                        float q = p < pw ? 1.0f : -1.0f;
                        q += polyBlep (p, dt);
                        float p2 = p - pw; if (p2 < 0.0f) p2 += 1.0f;
                        q -= polyBlep (p2, dt);
                        return q + (2.0f * pw - 1.0f);   // remove DC
                    }
                    case VAWave::Custom:
                        return s.table != nullptr ? s.table->sample (p, 0.0f, level0) : std::sin (kTwoPi * p);
                }
                return 0.0f;
            };
            y = wave (s.vaWave);
            if (pb > 0.001f) y = lerpf (y, wave ((s.vaWave + 1) % 6), pb);
            if (s.pc > 0.001f) { u.lp += (1.0f - s.pc * 0.95f) * (y - u.lp); y = u.lp * (1.0f + s.pc * 0.4f); }
            break;
        }
        case Engine::Wavetable: case Engine::Additive: case Engine::Spectral:
        {
            float q = p;
            if (engine == Engine::Wavetable)
            {
                const float b = (pa - 0.5f) * 2.0f;
                if (std::abs (b) > 0.01f) q = b > 0.0f ? std::pow (q, 1.0f + b * 3.0f) : 1.0f - std::pow (1.0f - q, 1.0f - b * 3.0f);
            }
            const WaveTable* wt = s.table != nullptr ? s.table : FactoryBank::get().sineTable();
            y = wt->sample (q, pos, level0);
            if (engine == Engine::Wavetable && pb > 0.001f)
            {
                const float m = q < 0.5f ? q * 2.0f : 2.0f - q * 2.0f;
                y = lerpf (y, wt->sample (m * 0.5f, pos, level0), pb);
            }
            if (engine == Engine::Wavetable && s.pc > 0.001f)
            {
                const float lv = std::exp2 (8.0f - s.pc * 7.0f);
                y = std::round (y * lv) / lv;
            }
            if (engine == Engine::Spectral && s.pc > 0.001f)
            {
                u.lp += (1.0f - s.pc * 0.9f) * (y - u.lp);
                y = u.lp;
            }
            break;
        }
        case Engine::FM:
        {
            const float ratio = 0.5f + std::round (pb * 30.0f) * 0.5f;
            const float index = pa * pa * 12.0f;
            u.fmPhase += dt * ratio;
            u.fmPhase -= std::floor (u.fmPhase);
            const float m = std::sin (kTwoPi * u.fmPhase + s.pc * 1.6f * u.fmMod);
            u.fmMod = m;
            y = std::sin (kTwoPi * p + index * m);
            break;
        }
        case Engine::PhaseDist:
        {
            const int shape = std::min (7, (int) (pb * 8.0f));
            const float d = 0.5f - pa * 0.48f;
            auto pdSaw = [d] (float x) { return x < d ? 0.5f * x / d : 0.5f + 0.5f * (x - d) / (1.0f - d); };
            switch (shape)
            {
                case 0: y = -std::cos (kTwoPi * pdSaw (p)); break;
                case 1: { const float x = p < 0.5f ? pdSaw (p * 2.0f) * 0.5f : 0.5f + pdSaw ((p - 0.5f) * 2.0f) * 0.5f; y = -std::cos (kTwoPi * x); break; }
                case 2: { const float w = d * 0.9f + 0.02f; const float x = p < w ? p / w * 0.5f : 0.5f + 0.5f * (p - w) / (1.0f - w); y = -std::cos (kTwoPi * x) * (p < w ? 1.0f : 0.6f); break; }
                case 3: y = -std::cos (kTwoPi * pdSaw (std::fmod (p * 2.0f, 1.0f))); break;
                case 4: { const float x = pdSaw (p); y = -std::cos (kTwoPi * x) * (p < 0.5f ? 1.0f : -0.5f); break; }
                default:
                {
                    const float resRatio = 1.0f + (s.pc + pa) * 12.0f;
                    float win;
                    if (shape == 5) win = 1.0f - p;
                    else if (shape == 6) win = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
                    else win = clamp01 ((1.0f - p) * 3.0f);
                    y = (1.0f - std::cos (kTwoPi * p * resRatio)) * 0.5f * win * 2.0f - 0.5f;
                    break;
                }
            }
            break;
        }
        case Engine::Sample:
        {
            if (s.sample == nullptr || s.sample->data.empty()) { y = 0.0f; break; }
            const auto& d = s.sample->data;
            const int n = (int) d.size();
            const double rootHz = 440.0 * std::pow (2.0, (s.sample->rootNote - 69.0) / 12.0);
            const double rate = ((double) u.inc * sr / rootHz) * (s.sample->sampleRate / sr);
            const bool loop = pb > 0.001f;
            const double loopStart = (double) pa * (double) (n - 1);
            const double loopLen = std::max (64.0, (double) pb * (double) (n - 1 - loopStart));
            const double loopEnd = std::min ((double) (n - 2), loopStart + loopLen);
            // windowed sinc (was linear); pitched up, the kernel narrows so it does not fold back
            const auto& sinc = racoon::sdsp::SincTable::get();
            const int band = racoon::sdsp::SincTable::bandFor (rate);
            auto read = [&] (double x) { return (x < 0.0 || x >= (double) (n - 1)) ? 0.0f : sinc.read (d.data(), n, x, band); };
            if (u.play >= (double) (n - 2) && ! loop) { y = 0.0f; break; }
            y = read (u.play);
            if (loop)
            {
                const double xf = std::max (8.0, (double) s.pc * loopLen * 0.5);
                if (u.play > loopEnd - xf)
                {
                    float go, gi;
                    racoon::sdsp::equalPower ((float) ((u.play - (loopEnd - xf)) / xf), go, gi);
                    y = y * go + read (u.play - loopLen) * gi;
                }
            }
            u.play += rate;
            if (loop && u.play >= loopEnd) u.play -= (loopEnd - loopStart);
            break;
        }
        case Engine::Physical:
        {
            const float delay = std::max (2.0f, 1.0f / std::max (u.inc, 1.0e-5f) - 0.5f);
            float x = u.ks.read (std::min (delay, (float) u.ks.buf.size() - 8.0f));
            const float damp = 0.15f + (1.0f - pa) * 0.8f;          // loop lowpass coefficient
            u.ksLp += damp * (x - u.ksLp);
            const float decay = 0.9990f - pa * pa * 0.03f;
            float fb = u.ksLp * decay;
            if (u.exciteLeft > 0) { u.bowNoise += 0.1f * (u.rng.bi() - u.bowNoise); fb += u.bowNoise * 0.02f * (pb - 0.5f); }
            fb += pmIn * 0.1f;
            u.ks.push (fastTanh (fb));
            y = x;
            if (s.pc > 0.001f)
            {
                u.body1.set (110.0f, 2.0f, (float) sr); u.body2.set (240.0f, 3.0f, (float) sr);
                u.body1.tick (x); u.body2.tick (x);
                y = x + s.pc * (u.body1.bp * 0.6f + u.body2.bp * 0.4f);
            }
            y *= 1.5f;
            break;
        }
        case Engine::AcidCircuit:
        {
            const float saw = 2.0f * p - 1.0f - polyBlep (p, dt);
            const float bias = (pb - 0.5f) * 0.6f;
            const float gain = 1.0f + s.pc * 10.0f;
            // the 303 square is the saw pushed through a single-transistor shaper
            const float sq = fastTanh ((saw + 0.12f + bias) * (3.0f + gain * 2.0f));
            y = lerpf (fastTanh (saw * gain * 0.5f) / fastTanh (gain * 0.5f), sq, pa);
            u.lp += 0.6f * (y - u.lp);   // slight HF loss of the analog path
            y = u.lp;
            break;
        }
        default: break;
    }
    return y;
}
} // namespace be

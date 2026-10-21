#include "Voice.h"

namespace be
{
juce::uint64 Voice::ageCounter = 0;

void Voice::prepare (double maxRate, int factor, int voiceIndex)
{
    // allocate for the highest rate once; setOversampling never allocates
    index = voiceIndex;
    rng.seed (0xBEEF0000u + (uint32_t) voiceIndex * 7717u);
    for (auto& o : osc) o.prepare (maxRate);
    const int maxComb = (int) (maxRate / 15.0) + 16;
    for (auto& f : filt) f.prepare (maxRate, maxComb);
    acidFilt.prepare (maxRate, 64);
    setOversampling (maxRate, factor);
}

void Voice::setOversampling (double rate, int factor)
{
    osRate = rate;
    osFactor = factor;
    cr = (float) (rate / (double) factor / 16.0);
    for (auto& e : env) e.setRate (cr);
    for (int i = 0; i < kNumLfos; ++i) { lfo[i].setRate (cr); lfo[i].seed (0x51u + (uint32_t) (index * 131 + i * 17)); }
    for (auto& o : osc) o.setSampleRate (rate);
    for (auto& f : filt) f.setSampleRate (rate);
    acidFilt.setSampleRate (rate);
    for (auto& n : noiseTilt) n.reset();
    subTone.reset();
    subCeil.reset();
}

void Voice::start (const PatchSnapshot& p, int n, float vel, int ch, bool legato, bool acc, bool slide,
                   float seqModValue, int stack, int stackCount, uint32_t seed, float startNote)
{
    const bool wasActive = active;
    active = true; gate = true;
    note = n; channel = ch; accent = acc; slideFlag = slide;
    rawVelocity = vel;
    // velocity curve
    const float vc = p.voice.velCurve;
    velocity = std::pow (clamp01 (vel), std::exp2 (-vc * 2.0f));
    stackIndex = stack; stackSize = std::max (1, stackCount);
    age = ++ageCounter;
    seqModVal = seqModValue;
    rng.seed (seed ^ (0x9E3779B9u * (uint32_t) (index + 1)));
    randomVal = rng.bi();
    const float h = p.voice.humanize;
    humanPitch = rng.bi() * h * 0.08f;
    humanCut = rng.bi() * h * 0.25f;
    velocity = clamp01 (velocity + rng.bi() * h * 0.12f);
    mpeBend = 0.0f; pressure = 0.0f; mpeSlide = 0.0f;

    // unison stack layout
    if (stackSize > 1)
    {
        const float x = 2.0f * (float) stackIndex / (float) (stackSize - 1) - 1.0f;
        stackDetune = x * p.voice.uniDetune * 0.5f;   // semitones
        stackPan = x * 0.8f;
    }
    else { stackDetune = 0.0f; stackPan = 0.0f; }

    // imperfection (per note)
    const float imp = p.imp.amount;
    impFilter = rng.bi() * imp * p.imp.filter * 0.35f;
    impAttack = 1.0f + rng.bi() * imp * p.imp.attack * 0.6f;
    impSat = rng.uni() * imp * p.imp.sat * 0.3f;
    acidCalibCut = rng.bi() * p.acid.calib * 0.25f;
    acidCalibRes = rng.bi() * p.acid.calib * 0.06f;

    // layer key / velocity ranges
    const int vel127 = (int) std::round (vel * 127.0f);
    for (int l = 0; l < kNumLayers; ++l)
        layerActive[l] = n >= p.layer[l].keyLo && n <= p.layer[l].keyHi && vel127 >= p.layer[l].velLo && vel127 <= p.layer[l].velHi;

    // pitch / glide
    targetNote = (float) n;
    if (! wasActive || ! legato)
    {
        const bool glideAlways = p.voice.glideMode == 0 && p.voice.glide > 0.0005f;
        curNote = glideAlways && startNote >= 0.0f ? startNote : (float) n;
        glideStart = curNote;
        glidePos = curNote == targetNote ? 1.0f : 0.0f;
        glideTime = p.voice.glide;
    }
    else
    {
        glideStart = curNote;
        glidePos = curNote == targetNote ? 1.0f : 0.0f;
        glideTime = p.voice.glide;
    }

    // modulators
    const bool legatoRetrig = legato && wasActive && ! p.voice.retrig;
    for (int e = 0; e < kNumEnvs; ++e)
    {
        if (! wasActive) env[e].kill();
        env[e].noteOn (p.env[e], velocity, n, legatoRetrig, e == p.voice.ampEnv ? impAttack : 1.0f);
    }
    for (int l = 0; l < kNumLfos; ++l)
        lfo[l].trigger (p.lfo[l], legatoRetrig);

    // generators: set control first so engine params (pos, exciter) are valid for note-on
    computeModulation (p, GlobalModState {});
    for (int o = 0; o < kNumOsc; ++o)
    {
        const auto& os = p.osc[o];
        OscMod m = oscMod[o];
        m.pitchSemis = pitchFor (p, curNote) + (float) (os.oct * 12 + os.semi) + os.fine * 0.01f;
        osc[o].setControl (os.s, m, p.voice.a4, 0.0f);
        const float hz = (float) (p.voice.a4 * std::pow (2.0, (m.pitchSemis - 69.0) / 12.0));
        osc[o].noteOn (os.s, hz, seed + (uint32_t) o * 101u, legatoRetrig);
    }
    if (! legatoRetrig)
    {
        if (p.sub.align) subPhase = p.sub.phase;
        noiseBurst = 1.0f;
        if (! wasActive)
        {
            for (auto& f : filt) f.reset();
            acidFilt.reset();
            ampPrev = 0.0f; subEnvPrev = 0.0f;
            acidAmp = 0.0f;
        }
        acidEnv = 1.0f;
        if (accent) acidAccentCap += (1.0f - acidAccentCap) * 0.55f;
    }
    else if (accent) acidAccentCap += (1.0f - acidAccentCap) * 0.55f;
}

void Voice::glideTo (const PatchSnapshot& p, int n, float vel, bool acc, bool slide, float seqModValue)
{
    // legato transition: keep envelopes, move pitch
    note = n; gate = true; accent = acc; slideFlag = slide;
    targetNote = (float) n;
    glideStart = curNote;
    glidePos = 0.0f;
    const float m = clamp01 (p.destBase[ModDst::AcidMorph]);
    glideTime = slide ? p.acid.slide : lerpf (p.voice.glide, p.acid.slide, m);
    if (glideTime < 0.0005f) { curNote = targetNote; glidePos = 1.0f; }
    seqModVal = seqModValue;
    age = ++ageCounter;
    if (p.voice.retrig)
    {
        velocity = std::pow (clamp01 (vel), std::exp2 (-p.voice.velCurve * 2.0f));
        for (int e = 0; e < kNumEnvs; ++e) env[e].noteOn (p.env[e], velocity, n, false, 1.0f);
        for (int l = 0; l < kNumLfos; ++l) lfo[l].trigger (p.lfo[l], false);
    }
    if (acc) acidAccentCap += (1.0f - acidAccentCap) * 0.55f;
    // 303: a slid note does not retrigger the filter envelope; a non-slid legato note does
    if (! slide) acidEnv = 1.0f;
}

void Voice::release()
{
    if (! gate) return;
    gate = false;
    for (auto& e : env) e.noteOff();
    for (auto& l : lfo) l.release();
}

void Voice::kill()
{
    active = false; gate = false;
    for (auto& e : env) e.kill();
    ampPrev = ampTarget = 0.0f;
    subEnvPrev = subEnvTarget = 0.0f;
    acidAmp = 0.0f;
}

float Voice::pitchFor (const PatchSnapshot& p, float noteF) const
{
    float n = noteF + (float) p.voice.transpose;
    if (p.tuningActive)
    {
        const int i0 = juce::jlimit (0, 127, (int) std::floor (noteF));
        const int i1 = juce::jlimit (0, 127, i0 + 1);
        const float t = noteF - std::floor (noteF);
        n += (float) (lerpf ((float) p.tuningCents[i0], (float) p.tuningCents[i1], t) * 0.01);
    }
    return n + mpeBend + humanPitch + stackDetune;
}

void Voice::computeModulation (const PatchSnapshot& p, const GlobalModState& g)
{
    // ------------------------------------------------ sources
    for (int e = 0; e < kNumEnvs; ++e)
    {
        const bool needed = p.envUsed[e] || e == p.voice.ampEnv;
        srcVal[ModSrc::Env1 + e] = needed ? env[e].tick (p.env[e], g.bpm) : 0.0f;
    }
    for (int l = 0; l < kNumLfos; ++l)
    {
        if (! p.lfoUsed[l]) { srcVal[ModSrc::Lfo1 + l] = 0.0f; continue; }
        if (p.lfo[l].global) { srcVal[ModSrc::Lfo1 + l] = g.lfo[l]; continue; }
        const float baseRate = std::max (0.01f, p.lfo[l].rate);
        const float modRate = destValue (p, ModDst::LfoRate1 + l, modSum[ModDst::LfoRate1 + l]);
        const float rateMul = p.lfo[l].sync ? std::exp2 (modSum[ModDst::LfoRate1 + l] * 4.0f) : modRate / baseRate;
        srcVal[ModSrc::Lfo1 + l] = lfo[l].tick (p.lfo[l], g.bpm, rateMul, modSum[ModDst::LfoDepth1 + l], g.hostPlaying, g.hostBeats);
    }
    for (int m = 0; m < kNumMacros; ++m) srcVal[ModSrc::Macro1 + m] = p.macros[m];
    srcVal[ModSrc::Velocity] = velocity;
    srcVal[ModSrc::Note] = clampf (((float) note - 60.0f) / 60.0f, -1.0f, 1.0f);
    srcVal[ModSrc::Aftertouch] = g.aftertouch;
    srcVal[ModSrc::PolyPressure] = pressure;
    srcVal[ModSrc::ModWheel] = g.modWheel;
    srcVal[ModSrc::PitchBend] = g.pitchBend;
    srcVal[ModSrc::MpeSlide] = mpeSlide;
    srcVal[ModSrc::Random] = randomVal;
    srcVal[ModSrc::SeqMod] = seqModVal;
    srcVal[ModSrc::SeqAccent] = accent ? 1.0f : 0.0f;
    srcVal[ModSrc::Evolution] = g.evolution;
    srcVal[ModSrc::Gate] = gate ? 1.0f : 0.0f;
    srcVal[ModSrc::XYX] = p.xyX;
    srcVal[ModSrc::XYY] = p.xyY;
    srcVal[ModSrc::Expression] = g.expression;
    srcVal[ModSrc::Constant] = 1.0f;

    // ------------------------------------------------ routes
    std::fill (std::begin (modSum), std::end (modSum), 0.0f);
    std::fill (std::begin (modMul), std::end (modMul), 1.0f);
    for (int i = 0; i < p.routes.count; ++i)
    {
        const auto& r = p.routes.r[i];
        if (r.dst <= 0 || ModDst::isGlobal (r.dst)) continue;
        float s = srcVal[r.src];
        const float as = std::abs (s), sg = s < 0.0f ? -1.0f : 1.0f;
        switch ((ModCurve) r.curve)
        {
            case ModCurve::Exp: s = sg * as * as * as; break;
            case ModCurve::Log: s = sg * std::sqrt (std::sqrt (as)) * (0.9f + 0.1f * as); break;
            case ModCurve::SCurve: s = sg * as * as * (3.0f - 2.0f * as); break;
            default: break;
        }
        if (r.quant > 0) s = std::round (s * (float) r.quant) / (float) r.quant;
        if (r.via > 0) s *= srcVal[r.via];
        if (r.smooth > 0.0f)
        {
            const float a = std::exp (-1.0f / (cr * (0.002f + r.smooth * r.smooth * 0.5f)));
            routeSmooth[r.index] = s + a * (routeSmooth[r.index] - s);
            s = routeSmooth[r.index];
        }
        else routeSmooth[r.index] = s;

        switch ((ModOp) r.op)
        {
            case ModOp::Add: modSum[r.dst] += r.amount * s; break;
            case ModOp::Multiply: modMul[r.dst] *= 1.0f - std::abs (r.amount) + std::abs (r.amount) * (ModSrc::isBipolar (r.src) ? 0.5f * (s + 1.0f) : s); break;
            case ModOp::Invert: modSum[r.dst] += r.amount * (ModSrc::isBipolar (r.src) ? -s : 1.0f - s); break;
        }
    }
    // fold multiply into an additive offset in normalised space
    for (int d = 1; d < ModDst::GlobalStart; ++d)
        if (modMul[d] != 1.0f) modSum[d] += p.destBase[d] * (modMul[d] - 1.0f);

    for (int o = 0; o < kNumOsc; ++o)
    {
        const int b = ModDst::Osc1 + o * ModDst::kOscBlock;
        auto& m = oscMod[o];
        m.level = destValue (p, b + ModDst::OLevel, modSum[b + ModDst::OLevel]);
        m.pan = destValue (p, b + ModDst::OPan, modSum[b + ModDst::OPan]);
        m.pos = destValue (p, b + ModDst::OPos, modSum[b + ModDst::OPos]);
        m.pa = destValue (p, b + ModDst::OParamA, modSum[b + ModDst::OParamA]);
        m.pb = destValue (p, b + ModDst::OParamB, modSum[b + ModDst::OParamB]);
        m.detune = destValue (p, b + ModDst::ODetune, modSum[b + ModDst::ODetune]);
        m.fold = destValue (p, b + ModDst::OFold, modSum[b + ModDst::OFold]);
        m.xmod = destValue (p, b + ModDst::OXMod, modSum[b + ModDst::OXMod]);
        m.shape = destValue (p, b + ModDst::OShape, modSum[b + ModDst::OShape]);
    }
}

void Voice::controlStep (const PatchSnapshot& p, const GlobalModState& g)
{
    computeModulation (p, g);
    const float dt = 1.0f / cr;

    // ------------------------------------------------ glide
    if (glidePos < 1.0f)
    {
        const float span = targetNote - glideStart;
        if (p.voice.glideCurve == 1)   // constant rate: glide time per octave
        {
            const float rate = 12.0f / std::max (glideTime, 0.001f);
            const float step = rate * dt;
            if (std::abs (targetNote - curNote) <= step) { curNote = targetNote; glidePos = 1.0f; }
            else curNote += (targetNote > curNote ? step : -step);
        }
        else
        {
            glidePos = std::min (1.0f, glidePos + dt / std::max (glideTime, 0.001f));
            const float shaped = 1.0f - std::pow (1.0f - glidePos, 2.2f);   // exponential-like approach
            curNote = glideStart + span * shaped;
        }
    }

    const float voicePitchMod = modSum[ModDst::VoicePitch] * 48.0f;
    const float basePitch = pitchFor (p, curNote) + g.pitchBendSemis;

    // ------------------------------------------------ oscillators
    const float impDrift = p.imp.amount * p.imp.drift * 0.6f;
    for (int o = 0; o < kNumOsc; ++o)
    {
        const auto& os = p.osc[o];
        if (! os.s.on) continue;
        auto& m = oscMod[o];
        const int b = ModDst::Osc1 + o * ModDst::kOscBlock;
        const float coarse = destValue (p, b + ModDst::OPitch, modSum[b + ModDst::OPitch]);
        const float kt = os.s.keyTrack;
        const float tracked = 60.0f + (basePitch - 60.0f) * kt;
        m.pitchSemis = tracked + (float) (os.oct * 12 + os.semi) + os.fine * 0.01f + coarse
                     + p.layer[os.layer].tune + voicePitchMod;
        osc[o].setControl (os.s, m, p.voice.a4, impDrift);
    }

    // ------------------------------------------------ sub
    {
        const bool lock = p.sub.lock;
        float sp = lock ? pitchFor (p, curNote) - stackDetune + g.pitchBendSemis : basePitch + voicePitchMod;
        sp += p.layer[0].tune - 12.0f * (float) p.sub.octave;
        const double hz = p.voice.a4 * std::pow (2.0, (sp - 69.0) / 12.0);
        subInc = (float) (hz / osRate);
        subLevel = p.sub.on && (! lock || stackIndex == 0) ? destValue (p, ModDst::SubLevel, modSum[ModDst::SubLevel]) : 0.0f;
        subDrive = destValue (p, ModDst::SubDrive, modSum[ModDst::SubDrive]);
        const float cut = std::min (destValue (p, ModDst::SubCutoff, modSum[ModDst::SubCutoff]), p.sub.limitHz * 4.0f);
        subTone.setCutoff (cut, (float) osRate);
        subCeil.set (p.sub.limitHz * 2.5f, 0.6f, (float) osRate);
    }

    // ------------------------------------------------ noise
    noiseLevel = p.noise.on ? destValue (p, ModDst::NoiseLevel, modSum[ModDst::NoiseLevel]) : 0.0f;
    noiseColor = destValue (p, ModDst::NoiseColor, modSum[ModDst::NoiseColor]);
    {
        const float c = noiseColor;
        const float fc = c < 0.0f ? 18000.0f * std::exp2 (c * 8.0f) : 30.0f * std::exp2 (c * 9.0f);
        for (auto& t : noiseTilt) t.setCutoff (fc, (float) osRate);
    }
    if (p.noise.attack > 0.0f) noiseBurst *= std::exp (-dt / 0.035f);

    // ------------------------------------------------ layers
    xfade = destValue (p, ModDst::LayerXFade, modSum[ModDst::LayerXFade]);
    bool anySolo = false;
    for (int l = 0; l < kNumLayers; ++l) anySolo |= p.layer[l].solo;
    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto& L = p.layer[l];
        float lv = destValue (p, ModDst::LayerLevel1 + l, modSum[ModDst::LayerLevel1 + l]);
        if (l == 1) lv *= 1.0f - std::max (0.0f, xfade);
        if (l == 2) lv *= 1.0f + std::min (0.0f, xfade);
        if (L.mute || (anySolo && ! L.solo) || ! layerActive[l]) lv = 0.0f;
        if (L.invert) lv = -lv;
        layLevel[l] = lv;
        layDrive[l] = clamp01 (destValue (p, ModDst::LayerDrive1 + l, modSum[ModDst::LayerDrive1 + l]) + impSat);
        const float pan = clampf (L.pan, -1.0f, 1.0f);
        layGainL[l] = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f;
        layGainR[l] = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
        for (int c = 0; c < 2; ++c)
        {
            layLow[l][c].setCutoff (L.lowCut, (float) osRate);
            layHigh[l][c].setCutoff (L.highCut, (float) osRate);
        }
    }

    // ------------------------------------------------ filters
    const float noteOct = ((float) curNote - 60.0f) / 12.0f;
    for (int f = 0; f < kNumFilters; ++f)
    {
        const auto& F = p.filter[f];
        filtOn[f] = F.on;
        if (! F.on) continue;
        const int b = ModDst::Filter1 + f * 5;
        float cut = destValue (p, b + 0, modSum[b + 0]);
        const float envV = srcVal[ModSrc::Env1 + F.envSel];
        float oct = noteOct * F.keyTrack + F.envAmt * envV * 9.0f + F.velSens * (velocity - 1.0f) * 4.0f + humanCut + impFilter;
        cut *= std::exp2 (oct);
        const float res = destValue (p, b + 1, modSum[b + 1]);
        const float drv = clamp01 (destValue (p, b + 2, modSum[b + 2]) + impSat);
        filtMix[f] = destValue (p, b + 3, modSum[b + 3]);
        const float morph = destValue (p, b + 4, modSum[b + 4]);
        filt[f].set (F.type, cut, res, drv, morph, F.feedback, F.comp, 16 * osFactor);   // glides over the control step
    }

    // ------------------------------------------------ acid path
    acidMorph = clamp01 (destValue (p, ModDst::AcidMorph, modSum[ModDst::AcidMorph]));
    acidEnvMod = destValue (p, ModDst::AcidEnvMod, modSum[ModDst::AcidEnvMod]);
    acidDecayTime = destValue (p, ModDst::AcidDecay, modSum[ModDst::AcidDecay]);
    {
        const float decay = accent ? 0.2f : acidDecayTime;   // 303: accent forces the shortest decay
        acidEnv *= std::exp (-dt / std::max (0.01f, decay * 0.35f));
        acidAccentCap *= std::exp (-dt / 0.6f);
        const float target = gate ? (1.0f + (accent ? p.acid.accent * 0.7f : 0.0f)) : 0.0f;
        const float tc = gate ? 0.003f : 0.012f;
        acidAmp += (target - acidAmp) * (1.0f - std::exp (-dt / tc));
        if (acidMorph > 0.0f)
        {
            const float accentSweep = accent ? acidAccentCap * p.acid.accent * 2.2f : 0.0f;
            float cut = p.acid.cutoff * std::exp2 (acidEnv * acidEnvMod * 5.0f + accentSweep + acidCalibCut + noteOct * 0.25f);
            const float res = clamp01 (p.acid.res + acidCalibRes + (accent ? p.acid.accent * 0.12f : 0.0f));
            acidFilt.set ((int) FilterType::Acid, cut, res, clamp01 (p.acid.drive * 0.6f + p.acid.nonlin * 0.4f), 0.5f, 0.0f, true, 16 * osFactor);
        }
    }

    // ------------------------------------------------ amp
    ampPrev = ampTarget;
    subEnvPrev = subEnvTarget;
    ampVolume = destValue (p, ModDst::AmpVolume, modSum[ModDst::AmpVolume]);
    const float pan = clampf (destValue (p, ModDst::AmpPan, modSum[ModDst::AmpPan]) + stackPan, -1.0f, 1.0f);
    ampPanL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f;
    ampPanR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
    const float ampEnvV = srcVal[ModSrc::Env1 + p.voice.ampEnv];
    ampTarget = ampEnvV * ampVolume / std::sqrt ((float) stackSize);
    const int subEnvIdx = p.sub.env == 0 ? p.voice.ampEnv : p.sub.env;
    subEnvTarget = (p.sub.env == 0 ? ampEnvV : srcVal[ModSrc::Env1 + subEnvIdx]) * ampVolume;
    lastAmp = ampTarget;

    // voice end
    const bool ampDone = ! env[p.voice.ampEnv].active();
    const bool subDone = p.sub.env == 0 || ! env[subEnvIdx].active();
    const bool acidDone = acidMorph <= 0.0f || acidAmp < 1.0e-4f;
    if (ampDone && subDone && acidDone && ampPrev < 1.0e-5f) active = false;
}

void Voice::render (const PatchSnapshot& p, const GlobalModState& g, LayerBuses& out, int startOs, int numBase)
{
    if (! active) return;
    controlStep (p, g);
    const int n = numBase * osFactor;
    const bool serial = p.layerMode == 1;
    const auto routing = (FilterRouting) p.filterRouting;
    const float aM = acidMorph;
    const bool subMono = p.sub.mono;

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const float amp = ampPrev + (ampTarget - ampPrev) * t;
        const float subEnv = subEnvPrev + (subEnvTarget - subEnvPrev) * t;

        // ---- generators
        float L[kNumLayers] {}, R[kNumLayers] {};
        // sub
        float subS = 0.0f;
        if (subLevel > 0.0f)
        {
            subPhase += subInc;
            if (subPhase >= 1.0f) subPhase -= 1.0f;
            const float ph = subPhase;
            switch ((SubWave) p.sub.wave)
            {
                case SubWave::Sine: subS = std::sin (kTwoPi * ph); break;
                case SubWave::Triangle: subS = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
                case SubWave::Square: subS = (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, subInc) - polyBlep (std::fmod (ph + 0.5f, 1.0f), subInc); break;
                case SubWave::SoftTri: { const float tr = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; subS = std::sin (tr * kPi * 0.5f); break; }
                case SubWave::SoftSquare: subS = fastTanh (std::sin (kTwoPi * ph) * 3.0f) / fastTanh (3.0f); break;
                case SubWave::RoundPulse: subS = fastTanh (std::sin (kTwoPi * ph) * 2.0f + 0.6f) * 0.9f; break;
            }
            subS = subTone.process (subS);
            subCeil.tick (subS);
            subS = subCeil.lp;
            if (subDrive > 0.0f) subS = fastTanh (subS * (1.0f + subDrive * 6.0f)) / fastTanh (1.0f + subDrive * 6.0f);
            subS *= subLevel;
        }

        // noise
        float noiseS = 0.0f;
        if (noiseLevel > 0.0f)
        {
            const float w = rng.bi();
            switch ((NoiseType) p.noise.type)
            {
                case NoiseType::White: noiseS = w; break;
                case NoiseType::Pink:
                {
                    auto& b = noisePink;
                    b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f;
                    b[2] = 0.96900f * b[2] + w * 0.1538520f; b[3] = 0.86650f * b[3] + w * 0.3104856f;
                    b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
                    noiseS = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f;
                    b[6] = w * 0.115926f;
                    break;
                }
                case NoiseType::Brown: noiseBrown = clampf (noiseBrown + w * 0.02f, -1.0f, 1.0f); noiseS = noiseBrown * 2.5f; break;
                case NoiseType::Crackle: noiseS = rng.uni() > 0.9985f ? rng.bi() * 3.0f : w * 0.02f; break;
                case NoiseType::Digital:
                    if (--noiseHoldCount <= 0) { noiseHold = rng.uni() > 0.5f ? 1.0f : -1.0f; noiseHoldCount = 1 + (int) (rng.uni() * 24.0f); }
                    noiseS = noiseHold * 0.5f;
                    break;
            }
            const float burst = lerpf (1.0f, noiseBurst, p.noise.attack);
            noiseS *= noiseLevel * burst;
        }

        // oscillators (x-mod sources: previous outputs)
        float oL[kNumOsc], oR[kNumOsc];
        for (int o = 0; o < kNumOsc; ++o)
        {
            const auto& os = p.osc[o].s;
            float xin = 0.0f;
            if (os.xmodType != 0)
            {
                switch (os.xmodSrc)
                {
                    case 0: case 1: case 2: xin = osc[os.xmodSrc].lastMono(); break;
                    case 3: xin = subS * 2.0f; break;
                    case 4: xin = noiseS * 2.0f; break;
                    default: break;
                }
            }
            osc[o].tick (os, xin, oL[o], oR[o]);
            const int ly = p.osc[o].layer;
            L[ly] += oL[o]; R[ly] += oR[o];
        }
        if (subMono) { L[0] += subS; R[0] += subS; }
        else { L[0] += subS * ampPanL; R[0] += subS * ampPanR; }
        if (noiseLevel > 0.0f)
        {
            const float np = clampf (p.noise.pan, -1.0f, 1.0f);
            const float ns = noiseColor < 0.0f ? noiseTilt[0].lp (noiseS) : noiseTilt[0].hp (noiseS);
            L[p.noise.layer] += ns * std::sqrt (0.5f * (1.0f - np)) * 1.41f;
            R[p.noise.layer] += ns * std::sqrt (0.5f * (1.0f + np)) * 1.41f;
        }

        // ---- layer processing
        for (int l = 0; l < kNumLayers; ++l)
        {
            if (serial && l >= 2) { L[l] += L[l - 1]; R[l] += R[l - 1]; L[l - 1] = R[l - 1] = 0.0f; }
            float a = L[l], b = R[l];
            if (a == 0.0f && b == 0.0f && layDrive[l] <= 0.0f) continue;
            const auto& LS = p.layer[l];
            if (LS.lowCut > 10.5f) { a = layLow[l][0].hp (a); b = layLow[l][1].hp (b); }
            if (LS.highCut < 21000.0f) { a = layHigh[l][0].lp (a); b = layHigh[l][1].lp (b); }
            if (layDrive[l] > 0.0f) { a = shapeDrive (LS.driveType, a, layDrive[l]); b = shapeDrive (LS.driveType, b, layDrive[l]); }
            if (LS.width != 1.0f) { const float m = 0.5f * (a + b), s = 0.5f * (a - b) * LS.width; a = m + s; b = m - s; }
            L[l] = a * layLevel[l] * layGainL[l];
            R[l] = b * layLevel[l] * layGainR[l];
        }

        // ---- filter network
        float bus[kNumLayers][2] {};
        float inL = 0.0f, inR = 0.0f;
        for (int l = 0; l < kNumLayers; ++l)
        {
            if (p.layer[l].bypassFilters) { bus[l][0] += L[l]; bus[l][1] += R[l]; }
            else { inL += L[l]; inR += R[l]; }
        }
        float fL = 0.0f, fR = 0.0f;
        auto runFilter = [this] (int f, float& a, float& b)
        {
            const float da = a, db = b;
            filt[f].process (a, b);
            a = lerpf (da, a, filtMix[f]); b = lerpf (db, b, filtMix[f]);
        };
        switch (routing)
        {
            case FilterRouting::Serial:
            {
                fL = inL; fR = inR;
                for (int f = 0; f < kNumFilters; ++f) if (filtOn[f]) runFilter (f, fL, fR);
                bus[1][0] += fL; bus[1][1] += fR;
                break;
            }
            case FilterRouting::Parallel:
            {
                int count = 0;
                for (int f = 0; f < kNumFilters; ++f)
                    if (filtOn[f]) { float a = inL, b = inR; runFilter (f, a, b); fL += a; fR += b; ++count; }
                if (count == 0) { fL = inL; fR = inR; }
                else { const float g2 = 1.0f / std::sqrt ((float) count); fL *= g2; fR *= g2; }
                bus[1][0] += fL; bus[1][1] += fR;
                break;
            }
            case FilterRouting::PerLayer:
            {
                for (int l = 0; l < kNumLayers; ++l)
                {
                    if (p.layer[l].bypassFilters) continue;
                    const int f = l == 0 ? 0 : l - 1;
                    float a = L[l], b = R[l];
                    if (filtOn[f]) runFilter (f, a, b);
                    const int target = l == 0 ? 1 : l;
                    bus[target][0] += a; bus[target][1] += b;
                    fL += a; fR += b;
                }
                break;
            }
            case FilterRouting::Split:
            {
                float a1 = inL, b1 = inR, a2 = inL, b2 = inR;
                int c = 0;
                float sL = 0.0f, sR = 0.0f;
                if (filtOn[0]) { runFilter (0, a1, b1); sL += a1; sR += b1; ++c; }
                if (filtOn[1]) { runFilter (1, a2, b2); sL += a2; sR += b2; ++c; }
                if (c == 0) { sL = inL; sR = inR; } else { sL /= std::sqrt ((float) c); sR /= std::sqrt ((float) c); }
                if (filtOn[2]) runFilter (2, sL, sR);
                fL = sL; fR = sR;
                bus[1][0] += fL; bus[1][1] += fR;
                break;
            }
        }

        // ---- acid morph: crossfade filtered path with the 303 path
        float acL = 0.0f, acR = 0.0f;
        if (aM > 0.0f)
        {
            acL = inL * (1.0f + p.acid.drive * 2.0f); acR = inR * (1.0f + p.acid.drive * 2.0f);
            acidFilt.process (acL, acR);
            if (p.acid.overdrive > 0.0f)
            {
                const float od = 1.0f + p.acid.overdrive * 10.0f;
                acL = fastTanh (acL * od) / fastTanh (od) * 0.9f;
                acR = fastTanh (acR * od) / fastTanh (od) * 0.9f;
            }
            for (int l = 1; l < kNumLayers; ++l) { bus[l][0] *= (1.0f - aM); bus[l][1] *= (1.0f - aM); }
            if (! p.layer[0].bypassFilters) { bus[0][0] *= (1.0f - aM); bus[0][1] *= (1.0f - aM); }
        }

        // ---- VCA and output
        const float subGain = p.layer[0].bypassFilters ? subEnv : amp;
        out.l[0][startOs + i] += bus[0][0] * subGain * (subMono ? 1.0f : 1.0f);
        out.r[0][startOs + i] += bus[0][1] * subGain;
        for (int l = 1; l < kNumLayers; ++l)
        {
            out.l[l][startOs + i] += bus[l][0] * amp * ampPanL;
            out.r[l][startOs + i] += bus[l][1] * amp * ampPanR;
        }
        if (aM > 0.0f)
        {
            const float ag = acidAmp * aM * ampVolume / std::sqrt ((float) stackSize);
            out.l[1][startOs + i] += acL * ag * ampPanL;
            out.r[1][startOs + i] += acR * ag * ampPanR;
        }
    }
}
} // namespace be

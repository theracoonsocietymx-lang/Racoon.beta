#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/PresetManager.h"

using namespace be;

// ===================================================================== parameter cache
struct BassEngineProcessor::Cache
{
    using A = std::atomic<float>*;
    juce::AudioProcessorValueTreeState& s;
    explicit Cache (juce::AudioProcessorValueTreeState& st) : s (st) {}
    A get (const juce::String& id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    }
    struct Osc { A on, engine, wave, table, sample, spmode, oct, semi, fine, phase, reset, randphase, unison, spread, blend, drift, slop, syncmode, sync, xmodtype, xmodsrc, pc, keytrack, layer; } osc[kNumOsc];
    A subOn, subWave, subOct, subPhase, subAlign, subEnv, subMono, subLock, subLimit;
    A noiseOn, noiseType, noisePan, noiseLayer, noiseAttack;
    struct Lay { A pan, width, tune, drivetype, lowcut, highcut, keylo, keyhi, vello, velhi, mute, solo, path, invert, delay; } lay[kNumLayers];
    A layerMode, filterRouting;
    struct Flt { A on, type, keytrack, envamt, envsel, velsens, feedback, comp; } flt[kNumFilters];
    struct Env { A delay, attack, hold, decay, sustain, release, acurve, dcurve, rcurve, mode, loop, retrig, sync, velsens, keyscale, drawlen; } env[kNumEnvs];
    struct Lfo { A wave, rate, sync, div, trig, oneshot, phase, offset, fadein, fadeout, scope, polarity, smooth, depth; } lfo[kNumLfos];
    struct Rt { A on, src, via, dst, amt, curve, op, quant, smooth; } rt[kNumRoutes];
    A macro[kNumMacros];
    A vMode, vVoices, vPriority, vGlide, vGlideMode, vGlideCurve, vBendUp, vBendDown, vMpe, vMpeRange, vVelCurve, vTranspose, vTune, vKeyLo, vKeyHi, vHumanize, vUnison, vUniDetune, vRetrig, vQuality, ampEnv;
    A acidCutoff, acidRes, acidAccent, acidAccThresh, acidSlide, acidDrive, acidCalib, acidOverdrive, acidNonlin;
    A impAmount, impDrift, impFilter, impAttack, impSat;
    A seqMode, seqPattern, seqLength, seqRate, seqSwing, seqGate, seqTranspose, seqRoot, seqScale, seqQuantize, seqArpMode, seqArpOct, seqLatch, seqHostSync, seqBpm, seqTrigger;
    struct Dst { A on, type, tone, out, shape, xlow, xhigh, lowdrive, highdrive; } dist[kNumDist];
    A distRouting, distPosition;
    A fxOn[kNumFx];
    std::map<juce::String, A> fx;
    A duckSrc, duckDiv, duckDepth, duckAttack, duckHold, duckRelease, duckCurve, duckBand, duckFreq, duckNote, duckChannel, duckThresh;
    A evoLen, evoShape, evoAmount, xyX, xyY;
    const juce::RangedAudioParameter* destParam[ModDst::Count] {};

    void build()
    {
        for (int o = 0; o < kNumOsc; ++o)
        {
            auto& c = osc[o];
            auto g = [&] (const char* n) { return get (oscId (o, n)); };
            c = { g ("on"), g ("engine"), g ("wave"), g ("table"), g ("sample"), g ("spmode"), g ("oct"), g ("semi"), g ("fine"), g ("phase"), g ("reset"),
                  g ("randphase"), g ("unison"), g ("spread"), g ("blend"), g ("drift"), g ("slop"), g ("syncmode"), g ("sync"), g ("xmodtype"),
                  g ("xmodsrc"), g ("pc"), g ("keytrack"), g ("layer") };
        }
        subOn = get ("sub_on"); subWave = get ("sub_wave"); subOct = get ("sub_oct"); subPhase = get ("sub_phase"); subAlign = get ("sub_align");
        subEnv = get ("sub_env"); subMono = get ("sub_mono"); subLock = get ("sub_lock"); subLimit = get ("sub_limit");
        noiseOn = get ("noise_on"); noiseType = get ("noise_type"); noisePan = get ("noise_pan"); noiseLayer = get ("noise_layer"); noiseAttack = get ("noise_attack");
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto g = [&] (const char* n) { return get (layId (l, n)); };
            lay[l] = { g ("pan"), g ("width"), g ("tune"), g ("drivetype"), g ("lowcut"), g ("highcut"), g ("keylo"), g ("keyhi"), g ("vello"), g ("velhi"),
                       g ("mute"), g ("solo"), g ("path"), g ("invert"), g ("delay") };
        }
        layerMode = get ("layer_mode"); filterRouting = get ("filter_routing");
        for (int f = 0; f < kNumFilters; ++f)
        {
            auto g = [&] (const char* n) { return get (filtId (f, n)); };
            flt[f] = { g ("on"), g ("type"), g ("keytrack"), g ("envamt"), g ("envsel"), g ("velsens"), g ("feedback"), g ("comp") };
        }
        for (int e = 0; e < kNumEnvs; ++e)
        {
            auto g = [&] (const char* n) { return get (envId (e, n)); };
            env[e] = { g ("delay"), g ("attack"), g ("hold"), g ("decay"), g ("sustain"), g ("release"), g ("acurve"), g ("dcurve"), g ("rcurve"),
                       g ("mode"), g ("loop"), g ("retrig"), g ("sync"), g ("velsens"), g ("keyscale"), g ("drawlen") };
        }
        for (int l = 0; l < kNumLfos; ++l)
        {
            auto g = [&] (const char* n) { return get (lfoId (l, n)); };
            lfo[l] = { g ("wave"), g ("rate"), g ("sync"), g ("div"), g ("trig"), g ("oneshot"), g ("phase"), g ("offset"), g ("fadein"), g ("fadeout"),
                       g ("scope"), g ("polarity"), g ("smooth"), g ("depth") };
        }
        for (int r = 0; r < kNumRoutes; ++r)
        {
            auto g = [&] (const char* n) { return get (routeId (r, n)); };
            rt[r] = { g ("on"), g ("src"), g ("via"), g ("dst"), g ("amt"), g ("curve"), g ("op"), g ("quant"), g ("smooth") };
        }
        for (int m = 0; m < kNumMacros; ++m) macro[m] = get (macroId (m));
        vMode = get ("v_mode"); vVoices = get ("v_voices"); vPriority = get ("v_priority"); vGlide = get ("v_glide"); vGlideMode = get ("v_glidemode");
        vGlideCurve = get ("v_glidecurve"); vBendUp = get ("v_bendup"); vBendDown = get ("v_benddown"); vMpe = get ("v_mpe"); vMpeRange = get ("v_mperange");
        vVelCurve = get ("v_velcurve"); vTranspose = get ("v_transpose"); vTune = get ("v_tune"); vKeyLo = get ("v_keylo"); vKeyHi = get ("v_keyhi");
        vHumanize = get ("v_humanize"); vUnison = get ("v_unison"); vUniDetune = get ("v_unidetune"); vRetrig = get ("v_retrig"); vQuality = get ("v_quality");
        ampEnv = get ("amp_env");
        acidCutoff = get ("acid_cutoff"); acidRes = get ("acid_res"); acidAccent = get ("acid_accent"); acidAccThresh = get ("acid_accthresh");
        acidSlide = get ("acid_slide"); acidDrive = get ("acid_drive"); acidCalib = get ("acid_calib"); acidOverdrive = get ("acid_overdrive"); acidNonlin = get ("acid_nonlin");
        impAmount = get ("imp_amount"); impDrift = get ("imp_drift"); impFilter = get ("imp_filter"); impAttack = get ("imp_attack"); impSat = get ("imp_sat");
        seqMode = get ("seq_mode"); seqPattern = get ("seq_pattern"); seqLength = get ("seq_length"); seqRate = get ("seq_rate"); seqSwing = get ("seq_swing");
        seqGate = get ("seq_gate"); seqTranspose = get ("seq_transpose"); seqRoot = get ("seq_root"); seqScale = get ("seq_scale"); seqQuantize = get ("seq_quantize");
        seqArpMode = get ("seq_arpmode"); seqArpOct = get ("seq_arpoct"); seqLatch = get ("seq_latch"); seqHostSync = get ("seq_hostsync"); seqBpm = get ("seq_bpm");
        seqTrigger = get ("seq_trigger");
        for (int d = 0; d < kNumDist; ++d)
        {
            auto g = [&] (const char* n) { return get (distId (d, n)); };
            dist[d] = { g ("on"), g ("type"), g ("tone"), g ("out"), g ("shape"), g ("xlow"), g ("xhigh"), g ("lowdrive"), g ("highdrive") };
        }
        distRouting = get ("dist_routing"); distPosition = get ("dist_position");
        const char* fxKeys[kNumFx] { "eq", "comp", "mbcomp", "limiter", "clipper", "chorus", "phaser", "flanger", "delay", "reverb", "freqshift", "ringmod", "transient", "imager", "autofilter", "resonator" };
        for (int k = 0; k < kNumFx; ++k) fxOn[k] = get (fxId (fxKeys[k], "on"));
        for (auto* p : s.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID.startsWith ("fx_")) fx[rp->paramID] = get (rp->paramID);
        duckSrc = get ("duck_src"); duckDiv = get ("duck_div"); duckDepth = get ("duck_depth"); duckAttack = get ("duck_attack"); duckHold = get ("duck_hold");
        duckRelease = get ("duck_release"); duckCurve = get ("duck_curve"); duckBand = get ("duck_band"); duckFreq = get ("duck_freq"); duckNote = get ("duck_note");
        duckChannel = get ("duck_channel"); duckThresh = get ("duck_thresh");
        evoLen = get ("evo_len"); evoShape = get ("evo_shape"); evoAmount = get ("evo_amount"); xyX = get ("xy_x"); xyY = get ("xy_y");
        for (int d = 1; d < ModDst::Count; ++d)
        {
            const auto id = ModDst::paramIdFor (d);
            destParam[d] = id.isNotEmpty() ? s.getParameter (id) : nullptr;
        }
    }
    float f (const juce::String& id) const { return fx.at (id)->load(); }
};

namespace
{
inline int I (std::atomic<float>* a) { return (int) std::round (a->load()); }
inline bool B (std::atomic<float>* a) { return a->load() > 0.5f; }
inline float F (std::atomic<float>* a) { return a->load(); }

}
juce::AudioProcessor::BusesProperties BassEngineProcessor::makeBuses()
{
    auto b = BusesProperties()
                 .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                 .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
    for (int l = 0; l < kNumLayers; ++l)
        b = b.withOutput (layerNames()[l], juce::AudioChannelSet::stereo(), false);
    return b;
}
namespace
{
juce::String floatsToBase64 (const float* d, size_t n)
{
    juce::MemoryBlock mb (d, n * sizeof (float));
    return mb.toBase64Encoding();
}
std::vector<float> base64ToFloats (const juce::String& s)
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (s)) return {};
    std::vector<float> v (mb.getSize() / sizeof (float));
    std::memcpy (v.data(), mb.getData(), v.size() * sizeof (float));
    return v;
}
}

// ===================================================================== construction
BassEngineProcessor::BassEngineProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, &undo, "PARAMS", createParameterLayout())
{
    FactoryBank::get();
    cache = std::make_unique<Cache> (apvts);
    cache->build();
    paramList = getParameters();

    for (auto& c : ccMap) c = -1;
    { const int def[kNumFx] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3 }; for (int k = 0; k < kNumFx; ++k) fxOrder[k] = def[k]; }

    // default shapes
    for (int i = 0; i < kNumShapes; ++i)
        setShape (i, i < kNumEnvs ? defaultEnvShape() : (i < kNumEnvs + kNumLfos ? defaultLfoShape()
                                                         : std::vector<ShapePoint> { { 0, 0.5f, 0 }, { 0.25f, 1.0f, -0.5f }, { 0.5f, 0.5f, 0.5f }, { 0.75f, 0.0f, -0.5f }, { 1.0f, 0.5f, 0.5f } }));
    msgSeq.initDefault();
    audioSeq = msgSeq;
    extra.getOrCreateChildWithName ("SEQ", nullptr).copyPropertiesAndChildrenFrom (msgSeq.toValueTree(), nullptr);
    for (int o = 0; o < kNumOsc; ++o)
    {
        std::vector<float> h (64, 0.0f);
        for (int i = 0; i < 64; ++i) h[(size_t) i] = 1.0f / (float) (i + 1);
        setAdditiveHarmonics (o, h);
    }
    presetManager = std::make_unique<PresetManager> (*this);
    rebuildDerivedTables (true);
    rebuildTuning();
    startTimerHz (20);
}

BassEngineProcessor::~BassEngineProcessor() { stopTimer(); }

bool BassEngineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses[i];
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    return true;
}

void BassEngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    automation.prepare (sampleRate);
    sampleRateCache = sampleRate;
    engine.prepare (sampleRate, std::max (samplesPerBlock, 512));
    seqDirty = true;
}

// ===================================================================== snapshot
void BassEngineProcessor::fillSnapshot()
{
    auto& c = *cache;
    auto& p = snap;
    const auto& fb = FactoryBank::get();

    for (int o = 0; o < kNumOsc; ++o)
    {
        auto& os = p.osc[o];
        auto& cc = c.osc[o];
        auto& s = os.s;
        s.on = B (cc.on);
        s.engine = I (cc.engine);
        s.vaWave = I (cc.wave);
        const bool user = userTable[o].get() != nullptr;
        switch ((Engine) s.engine)
        {
            case Engine::VA: s.table = s.vaWave == (int) VAWave::Custom ? vaCustomTable.get() : nullptr; break;
            case Engine::Wavetable: s.table = user ? userTable[o].get() : fb.table (I (cc.table)); break;
            case Engine::Additive: case Engine::Spectral: s.table = derivedTable[o].get(); break;
            default: s.table = nullptr; break;
        }
        s.sample = userSample[o].get() != nullptr ? userSample[o].get() : fb.sample (I (cc.sample));
        s.unison = juce::jlimit (1, kMaxUnison, I (cc.unison));
        s.spread = F (cc.spread); s.blend = F (cc.blend); s.drift = F (cc.drift); s.slop = F (cc.slop);
        s.syncMode = I (cc.syncmode); s.syncRatio = F (cc.sync);
        s.xmodType = I (cc.xmodtype); s.xmodSrc = I (cc.xmodsrc);
        s.randPhase = F (cc.randphase); s.startPhase = F (cc.phase); s.reset = B (cc.reset);
        s.keyTrack = F (cc.keytrack); s.pc = F (cc.pc);
        os.oct = I (cc.oct); os.semi = I (cc.semi); os.fine = F (cc.fine);
        os.layer = juce::jlimit (0, kNumLayers - 1, I (cc.layer));
    }
    p.sub.on = B (c.subOn); p.sub.wave = I (c.subWave); p.sub.octave = I (c.subOct); p.sub.phase = F (c.subPhase);
    p.sub.align = B (c.subAlign); p.sub.env = I (c.subEnv); p.sub.mono = B (c.subMono); p.sub.lock = B (c.subLock); p.sub.limitHz = F (c.subLimit);
    p.noise.on = B (c.noiseOn); p.noise.type = I (c.noiseType); p.noise.pan = F (c.noisePan); p.noise.layer = I (c.noiseLayer); p.noise.attack = F (c.noiseAttack);
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto& L = p.layer[l]; auto& cl = c.lay[l];
        L.pan = F (cl.pan); L.width = F (cl.width); L.tune = F (cl.tune); L.driveType = I (cl.drivetype);
        L.lowCut = F (cl.lowcut); L.highCut = F (cl.highcut); L.keyLo = I (cl.keylo); L.keyHi = I (cl.keyhi);
        L.velLo = I (cl.vello); L.velHi = I (cl.velhi); L.mute = B (cl.mute); L.solo = B (cl.solo);
        L.bypassFilters = I (cl.path) == 1; L.invert = B (cl.invert); L.delayMs = F (cl.delay);
        gset.layerDelayMs[l] = L.delayMs;
    }
    p.layerMode = I (c.layerMode);
    p.filterRouting = I (c.filterRouting);
    for (int f = 0; f < kNumFilters; ++f)
    {
        auto& F_ = p.filter[f]; auto& cf = c.flt[f];
        F_.on = B (cf.on); F_.type = I (cf.type); F_.keyTrack = F (cf.keytrack); F_.envAmt = F (cf.envamt);
        F_.envSel = I (cf.envsel); F_.velSens = F (cf.velsens); F_.feedback = F (cf.feedback); F_.comp = B (cf.comp);
    }
    for (int e = 0; e < kNumEnvs; ++e)
    {
        auto& E = p.env[e]; auto& ce = c.env[e];
        E.delay = F (ce.delay); E.attack = F (ce.attack); E.hold = F (ce.hold); E.decay = F (ce.decay); E.sustain = F (ce.sustain);
        E.release = F (ce.release); E.aCurve = F (ce.acurve); E.dCurve = F (ce.dcurve); E.rCurve = F (ce.rcurve);
        E.drawn = I (ce.mode) == 1; E.loop = I (ce.loop) == 1; E.retrig = B (ce.retrig); E.sync = B (ce.sync);
        E.velSens = F (ce.velsens); E.keyScale = F (ce.keyscale); E.drawLen = F (ce.drawlen);
        E.lut = shapeSlots[(size_t) e].get();
    }
    for (int l = 0; l < kNumLfos; ++l)
    {
        auto& L = p.lfo[l]; auto& cl = c.lfo[l];
        L.wave = I (cl.wave); L.rate = F (cl.rate); L.sync = B (cl.sync); L.divBeats = syncDivisionBeats (I (cl.div));
        L.trigger = I (cl.trig); L.oneShot = B (cl.oneshot); L.phase = F (cl.phase); L.offset = F (cl.offset);
        L.fadeIn = F (cl.fadein); L.fadeOut = F (cl.fadeout); L.global = I (cl.scope) == 1; L.unipolar = I (cl.polarity) == 1;
        L.smooth = F (cl.smooth); L.depth = F (cl.depth);
        L.lut = shapeSlots[(size_t) (kNumEnvs + l)].get();
    }
    // routes & usage
    std::fill (std::begin (p.envUsed), std::end (p.envUsed), false);
    std::fill (std::begin (p.lfoUsed), std::end (p.lfoUsed), false);
    p.routes.count = 0;
    auto markSrc = [&p] (int src)
    {
        if (src >= ModSrc::Env1 && src < ModSrc::Env1 + kNumEnvs) p.envUsed[src - ModSrc::Env1] = true;
        if (src >= ModSrc::Lfo1 && src < ModSrc::Lfo1 + kNumLfos) p.lfoUsed[src - ModSrc::Lfo1] = true;
    };
    for (int r = 0; r < kNumRoutes; ++r)
    {
        auto& cr = c.rt[r];
        if (! B (cr.on)) continue;
        const int src = I (cr.src), dst = I (cr.dst);
        const float amt = F (cr.amt);
        if (src <= 0 || dst <= 0 || std::abs (amt) < 1.0e-5f) continue;
        auto& R = p.routes.r[p.routes.count++];
        R.src = src; R.via = I (cr.via); R.dst = dst; R.amount = amt; R.curve = I (cr.curve); R.op = I (cr.op);
        R.quant = I (cr.quant); R.smooth = F (cr.smooth); R.index = r;
        markSrc (src); markSrc (R.via);
    }
    for (int f = 0; f < kNumFilters; ++f) if (p.filter[f].on && std::abs (p.filter[f].envAmt) > 1.0e-4f) p.envUsed[p.filter[f].envSel] = true;
    for (int m = 0; m < kNumMacros; ++m) p.macros[m] = F (c.macro[m]);
    p.xyX = F (c.xyX); p.xyY = F (c.xyY);

    auto& v = p.voice;
    v.mode = I (c.vMode); v.voices = I (c.vVoices); v.priority = I (c.vPriority); v.glide = F (c.vGlide); v.glideMode = I (c.vGlideMode);
    v.glideCurve = I (c.vGlideCurve); v.bendUp = I (c.vBendUp); v.bendDown = I (c.vBendDown); v.mpe = B (c.vMpe); v.mpeRange = I (c.vMpeRange);
    v.velCurve = F (c.vVelCurve); v.transpose = I (c.vTranspose); v.a4 = F (c.vTune); v.keyLo = I (c.vKeyLo); v.keyHi = I (c.vKeyHi);
    v.humanize = F (c.vHumanize); v.unison = I (c.vUnison); v.uniDetune = F (c.vUniDetune); v.retrig = B (c.vRetrig); v.quality = I (c.vQuality);
    v.ampEnv = juce::jlimit (0, kNumEnvs - 1, I (c.ampEnv));
    p.envUsed[v.ampEnv] = true;
    if (p.sub.env > 0) p.envUsed[p.sub.env] = true;

    auto& a = p.acid;
    a.cutoff = F (c.acidCutoff); a.res = F (c.acidRes); a.accent = F (c.acidAccent); a.accentThresh = I (c.acidAccThresh);
    a.slide = F (c.acidSlide); a.drive = F (c.acidDrive); a.calib = F (c.acidCalib); a.overdrive = F (c.acidOverdrive); a.nonlin = F (c.acidNonlin);
    p.imp = { F (c.impAmount), F (c.impDrift), F (c.impFilter), F (c.impAttack), F (c.impSat) };

    for (int d = 1; d < ModDst::Count; ++d)
    {
        p.destParam[d] = c.destParam[d];
        p.destBase[d] = c.destParam[d] != nullptr ? c.destParam[d]->getValue() : 0.0f;
    }
    if (auto* t = tuning.get()) { std::copy (std::begin (t->cents), std::end (t->cents), std::begin (p.tuningCents)); p.tuningActive = t->active; }
    else p.tuningActive = false;

    // ------------------------------------------------ global settings
    auto& sq = gset.seq;
    sq.mode = I (c.seqMode); sq.pattern = I (c.seqPattern) - 1; sq.length = I (c.seqLength); sq.rate = I (c.seqRate); sq.swing = F (c.seqSwing);
    sq.gateScale = F (c.seqGate); sq.keyTranspose = B (c.seqTranspose); sq.root = I (c.seqRoot); sq.scale = I (c.seqScale); sq.quantize = B (c.seqQuantize);
    sq.arpMode = I (c.seqArpMode); sq.arpOct = I (c.seqArpOct); sq.latch = B (c.seqLatch); sq.hostSync = B (c.seqHostSync); sq.bpm = F (c.seqBpm);
    sq.trigger = I (c.seqTrigger);
    sq.preview = seqPreview.load() && sq.mode == (int) SeqMode::Sequencer;
    // a clip of the song is being edited: the song plays the clip, the pattern only sounds with PLAY
    if (clipEditing.load() && ! sq.preview) sq.mode = (int) SeqMode::Off;
    for (int d = 0; d < kNumDist; ++d)
    {
        auto& D = gset.dist[d]; auto& cd = c.dist[d];
        D.on = B (cd.on); D.type = I (cd.type); D.tone = F (cd.tone); D.outDb = F (cd.out); D.shape = F (cd.shape);
        D.xLow = F (cd.xlow); D.xHigh = F (cd.xhigh); D.lowDrive = F (cd.lowdrive); D.highDrive = F (cd.highdrive);
    }
    gset.distRouting = I (c.distRouting); gset.distPosition = I (c.distPosition);
    auto& x = gset.fx;
    for (int k = 0; k < kNumFx; ++k) { x.on[k] = B (c.fxOn[k]); x.order[k] = fxOrder[k].load(); }
    auto fv = [&c] (const char* fx, const char* n) { return c.f (fxId (fx, n)); };
    x.eqHpf = fv ("eq", "hpf"); x.eqLsF = fv ("eq", "lsfreq"); x.eqLsG = fv ("eq", "lsgain"); x.eqP1F = fv ("eq", "p1freq"); x.eqP1G = fv ("eq", "p1gain");
    x.eqP1Q = fv ("eq", "p1q"); x.eqP2F = fv ("eq", "p2freq"); x.eqP2G = fv ("eq", "p2gain"); x.eqP2Q = fv ("eq", "p2q"); x.eqHsF = fv ("eq", "hsfreq"); x.eqHsG = fv ("eq", "hsgain");
    x.cThresh = fv ("comp", "thresh"); x.cRatio = fv ("comp", "ratio"); x.cAttack = fv ("comp", "attack"); x.cRelease = fv ("comp", "release");
    x.cMakeup = fv ("comp", "makeup"); x.cMix = fv ("comp", "mix");
    x.mbXLow = fv ("mbcomp", "xlow"); x.mbXHigh = fv ("mbcomp", "xhigh"); x.mbLow = fv ("mbcomp", "low"); x.mbMid = fv ("mbcomp", "mid");
    x.mbHigh = fv ("mbcomp", "high"); x.mbRatio = fv ("mbcomp", "ratio"); x.mbMix = fv ("mbcomp", "mix");
    x.limCeiling = fv ("limiter", "ceiling"); x.limRelease = fv ("limiter", "release");
    x.clipDrive = fv ("clipper", "drive"); x.clipCeiling = fv ("clipper", "ceiling"); x.clipKnee = fv ("clipper", "knee");
    x.chRate = fv ("chorus", "rate"); x.chDepth = fv ("chorus", "depth"); x.chDelay = fv ("chorus", "delay"); x.chFb = fv ("chorus", "feedback"); x.chLowCut = fv ("chorus", "lowcut");
    x.phRate = fv ("phaser", "rate"); x.phDepth = fv ("phaser", "depth"); x.phCentre = fv ("phaser", "centre"); x.phFb = fv ("phaser", "feedback"); x.phLowCut = fv ("phaser", "lowcut");
    x.flRate = fv ("flanger", "rate"); x.flDepth = fv ("flanger", "depth"); x.flDelay = fv ("flanger", "delay"); x.flFb = fv ("flanger", "feedback"); x.flLowCut = fv ("flanger", "lowcut");
    x.dlTime = fv ("delay", "time"); x.dlSync = fv ("delay", "sync") > 0.5f; x.dlDiv = (int) fv ("delay", "div"); x.dlPing = fv ("delay", "pingpong") > 0.5f;
    x.dlHighCut = fv ("delay", "highcut"); x.dlLowCut = fv ("delay", "lowcut");
    x.rvSize = fv ("reverb", "size"); x.rvDamp = fv ("reverb", "damp"); x.rvWidth = fv ("reverb", "width"); x.rvPre = fv ("reverb", "predelay"); x.rvLowCut = fv ("reverb", "lowcut");
    x.fsMix = fv ("freqshift", "mix");
    x.rmFreq = fv ("ringmod", "freq"); x.rmMode = (int) fv ("ringmod", "mode"); x.rmRatio = fv ("ringmod", "ratio"); x.rmMix = fv ("ringmod", "mix");
    x.trAttack = fv ("transient", "attack"); x.trSustain = fv ("transient", "sustain"); x.trSpeed = fv ("transient", "speed");
    x.imBassMono = fv ("imager", "bassmono") > 0.5f; x.imMonoFreq = fv ("imager", "monofreq");
    x.afType = (int) fv ("autofilter", "type"); x.afRes = fv ("autofilter", "res"); x.afRate = fv ("autofilter", "rate"); x.afSync = fv ("autofilter", "sync") > 0.5f;
    x.afDiv = (int) fv ("autofilter", "div"); x.afDepth = fv ("autofilter", "depth"); x.afMix = fv ("autofilter", "mix");
    x.rsTune = fv ("resonator", "tune"); x.rsDecay = fv ("resonator", "decay"); x.rsBright = fv ("resonator", "bright");
    auto& dk = gset.duck;
    dk.source = I (c.duckSrc); dk.division = I (c.duckDiv); dk.depth = F (c.duckDepth); dk.attackMs = F (c.duckAttack); dk.holdMs = F (c.duckHold);
    dk.releaseMs = F (c.duckRelease); dk.curve = F (c.duckCurve); dk.band = I (c.duckBand); dk.freq = F (c.duckFreq); dk.note = I (c.duckNote);
    dk.channel = I (c.duckChannel); dk.threshDb = F (c.duckThresh);
    gset.evoLen = I (c.evoLen); gset.evoShape = I (c.evoShape); gset.evoAmount = F (c.evoAmount);
}

// ===================================================================== audio
void BassEngineProcessor::handleMidiLearn (juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber();
        if (cc == 120 || cc == 123) continue;
        {
            const juce::SpinLock::ScopedTryLockType l (learnLock);
            if (l.isLocked() && learnTarget.isNotEmpty())
            {
                for (int i = 0; i < paramList.size(); ++i)
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == learnTarget)
                    {
                        for (auto& c : ccMap) if (c.load() == i) c = -1;
                        ccMap[cc] = i;
                    }
                learnTarget = {};
            }
        }
        const int pi = ccMap[cc].load();
        if (pi >= 0 && pi < paramList.size())
            paramList[pi]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
    }
}

void BassEngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();

    // "All sound off" (CC 120) from the host stops the PLAY of the piano roll too: Racoon sends it when you close Badger
    // while it plays on its own and answer "Sí, detenerlo". Only CC 120: the host sends "all notes off" (CC 123) by itself
    // all the time with the transport stopped (each cursor move), and that one must never stop Badger's own Play.
    for (const auto meta : midi)
        if (meta.getMessage().isAllSoundOff()) { seqPreview = false; break; }
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    handleMidiLearn (midi);
    fillSnapshot();
    engine.setQuality (isNonRealtime() ? 2 : snap.voice.quality);

    if (seqDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType l (seqLock);
        if (l.isLocked()) { audioSeq = msgSeq; seqDirty = false; }
    }

    be::HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.valid = true;
            host.playing = pos->getIsPlaying();
            host.bpm = pos->getBpm().orFallback (120.0);
            host.beats = pos->getPpqPosition().orFallback (0.0);
        }

    // automation: with the DAW when it plays, with the PLAY of the piano roll otherwise
    automation.process (midi, automation.clock (host.valid && host.playing, host.beats, seqPreview.load(),
                                                host.valid ? host.bpm : (double) gset.seq.bpm, n), n);

    // buses
    auto mainOut = getBusBuffer (buffer, false, 0);
    float* outL = mainOut.getWritePointer (0);
    float* outR = mainOut.getWritePointer (1);
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 0);
        if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getReadPointer (juce::jmin (1, sc.getNumChannels() - 1)); }
    }
    // copy sidechain before outputs overwrite shared channels
    juce::HeapBlock<float> scCopy;
    if (scL != nullptr)
    {
        scCopy.allocate ((size_t) n * 2, false);
        std::copy (scL, scL + n, scCopy.get());
        std::copy (scR, scR + n, scCopy.get() + n);
        scL = scCopy.get(); scR = scCopy.get() + n;
    }
    float* layL[kNumLayers] {}; float* layR[kNumLayers] {};
    bool anyLayerOut = false;
    for (int l = 0; l < kNumLayers; ++l)
    {
        auto* bus = getBus (false, l + 1);
        if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
        {
            auto b = getBusBuffer (buffer, false, l + 1);
            layL[l] = b.getWritePointer (0); layR[l] = b.getWritePointer (1);
            anyLayerOut = true;
        }
    }
    if (gset.duck.source == (int) be::DuckSource::DrumBridge)
    {
        kickSchedValid = bridge.readKick (kickSched);
        drumBridgeSeen = kickSchedValid;
    }
    else { kickSchedValid = false; liveKickPrimed = false; }
    // process in chunks no larger than the prepared size
    int done = 0;
    juce::MidiBuffer chunkMidi;
    while (done < n)
    {
        const int len = std::min (512, n - done);
        chunkMidi.clear();
        chunkMidi.addEvents (midi, done, len, -done);
        float* cl[kNumLayers] {}; float* cr[kNumLayers] {};
        for (int l = 0; l < kNumLayers; ++l) if (layL[l] != nullptr) { cl[l] = layL[l] + done; cr[l] = layR[l] + done; }
        be::HostInfo h = host;
        h.beats = host.beats + (double) done * host.bpm / 60.0 / getSampleRate();
        if (gset.duck.source == (int) be::DuckSource::DrumBridge && kickSchedValid)
        {
            // kicks published by Drum Engine: exact positions on the host timeline, even ahead of the drum plugin's own block
            const auto& ks = kickSched;
            if (h.valid && h.playing && ks.numHits > 0 && ks.loopBeats > 0.0)
            {
                const double spb = getSampleRate() * 60.0 / std::max (1.0, h.bpm);
                const double b0 = h.beats, b1 = b0 + len / spb;
                for (int k = 0; k < ks.numHits; ++k)
                {
                    const double off = ks.anchorBeat + (double) ks.hitBeat[k];
                    double t = off + std::ceil ((b0 - off) / ks.loopBeats - 1.0e-9) * ks.loopBeats;
                    for (; t < b1; t += ks.loopBeats)
                        if (t >= b0) engine.addDuckTrigger ((int) ((t - b0) * spb));
                }
            }
            if (liveKickPrimed && ks.liveCount != lastLiveKick) engine.addDuckTrigger (0);
            lastLiveKick = ks.liveCount;
            liveKickPrimed = true;
        }
        engine.process (snap, gset, audioSeq, h, chunkMidi, outL + done, outR + done, len,
                        anyLayerOut ? cl : nullptr, anyLayerOut ? cr : nullptr,
                        scL != nullptr ? scL + done : nullptr, scR != nullptr ? scR + done : nullptr);
        done += len;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
    {
        bool used = false;
        for (int l = 0; l < kNumLayers; ++l) used |= layL[l] == buffer.getWritePointer (ch) || layR[l] == buffer.getWritePointer (ch);
        if (! used) buffer.clear (ch, 0, n);
    }
    midi.clear();

    // publish the bass pitch and low-end level for Drum Engine's kick / bass view
    if (++bridgePublishCounter >= 4)
    {
        bridgePublishCounter = 0;
        be::BridgeBassState bs;
        const float note = engine.telemetry.leadNote.load();
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (outL[i]));
        bs.active = note >= 0.0f && pk > 1.0e-3f ? 1 : 0;
        bs.bassHz = bs.active ? (float) be::noteToHz (note) : 0.0f;
        bs.bassEnv = pk;
        bridge.publishBass (bs);
    }

    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double avail = (double) n / std::max (1.0, getSampleRate());
    cpuLoad = 0.9f * cpuLoad.load() + 0.1f * (float) (elapsed / avail);
}

// ===================================================================== shapes / seq / assets
std::vector<ShapePoint> BassEngineProcessor::getShape (int index) const
{
    return shapeFromString (extra.getChildWithName ("SHAPES").getProperty ("s" + juce::String (index)).toString());
}

void BassEngineProcessor::setShape (int index, const std::vector<ShapePoint>& pts)
{
    extra.getOrCreateChildWithName ("SHAPES", nullptr).setProperty ("s" + juce::String (index), shapeToString (pts), nullptr);
    shapeSlots[(size_t) index].set (ShapeLut::fromPoints (pts));
    if (index == kNumShapes - 1)
    {
        auto lut = ShapeLut::fromPoints (pts);
        vaCustomTable.set (TableBuilders::fromCycleFunction ([lut] (float x) { return lut->at (x) * 2.0f - 1.0f; }));
    }
}

SeqData BassEngineProcessor::getSeqData() const { const juce::SpinLock::ScopedLockType l (seqLock); return msgSeq; }

void BassEngineProcessor::setSeqData (const SeqData& d)
{
    {
        const juce::SpinLock::ScopedLockType l (seqLock);
        msgSeq = d;
    }
    seqDirty = true;
    ++seqVersion;
    auto seqTree = extra.getOrCreateChildWithName ("SEQ", nullptr);
    seqTree.removeAllChildren (nullptr);
    seqTree.copyPropertiesAndChildrenFrom (d.toValueTree(), nullptr);
}

namespace
{
// a change to the patterns in the same history as the knobs (Deshacer, Rehacer, Cmd+Z); what comes after is read when
// it is undone, so a drag of several steps undoes as one
struct BassSeqAction : public juce::UndoableAction
{
    BassSeqAction (BassEngineProcessor& p, SeqData b, SeqData a) : proc (p), before (std::move (b)), after (std::move (a)) {}
    bool perform() override { if (done) proc.setSeqData (after); done = true; return true; }
    bool undo() override { after = proc.getSeqData(); proc.setSeqData (before); return true; }
    int getSizeInUnits() override { return 60; }
    BassEngineProcessor& proc;
    SeqData before, after;
    bool done = false;
};
}

void BassEngineProcessor::setSeqDataUndoable (const SeqData& d)
{
    auto* a = new BassSeqAction (*this, getSeqData(), d);
    setSeqData (d);
    undo.beginNewTransaction();
    undo.perform (a);
}

juce::MidiFile BassEngineProcessor::maquetaMidi()
{
    fillSnapshot();
    const int pat = juce::jmax (0, juce::roundToInt (apvts.getRawParameterValue ("seq_pattern")->load()) - 1);
    auto mf = Sequencer::toMidiFile (getSeqData(), gset.seq, pat, gset.seq.bpm);
    automation.addToMidiFile (mf, juce::jlimit (1, kSeqMaxSteps, gset.seq.length) * seqRateBeats (gset.seq.rate));
    return mf;
}

void BassEngineProcessor::loadFromSong (const racoon::maqueta::EditRequest& r)
{
    seqPreview = false;
    clipEditing = true;
    extra.setProperty ("clipEditing", true, nullptr);
    auto setReal = [this] (const char* id, float v)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id))) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    const int numSteps = juce::jlimit (1, kSeqMaxSteps, (int) std::ceil (juce::jmin (r.beats, kSeqMaxSteps * 0.25) / 0.25 - 1.0e-6));
    setReal ("seq_mode", 1.0f);           // Sequencer
    setReal ("seq_length", (float) numSteps);
    setReal ("seq_rate", 4.0f);           // 1/16
    setReal ("seq_root", 0.0f);
    setReal ("seq_quantize", 0.0f);       // the notes as they are, not pulled into a scale
    setReal ("seq_swing", 0.0f);
    setReal ("seq_gate", 1.0f);
    setReal ("seq_bpm", (float) juce::jlimit (40.0, 220.0, r.bpm));
    const int pat = juce::jlimit (0, kSeqPatterns - 1, juce::roundToInt (apvts.getRawParameterValue ("seq_pattern")->load()) - 1);
    auto d = getSeqData();
    auto& P = d.patterns[pat];
    for (auto& st : P.steps) { st = SeqStep(); st.on = false; }
    // notes by step; the lowest one leads the step, the rest are its chord
    std::vector<std::vector<racoon::maqueta::EditRequest::Note>> at ((size_t) numSteps);
    for (auto& n : r.notes())
    {
        const int k = (int) std::round (n.start / 0.25);
        if (k < numSteps) at[(size_t) k].push_back (n);
    }
    auto place = [] (int pitch, int& note, int& octave)
    {
        const int rel = juce::jlimit (-24, 48, pitch - 36);
        octave = juce::jlimit (-2, 2, (int) std::floor (rel / 12.0));
        note = juce::jlimit (0, 24, rel - 12 * octave);
    };
    for (int k = 0; k < numSteps; ++k)
    {
        auto& here = at[(size_t) k];
        if (here.empty()) continue;
        std::sort (here.begin(), here.end(), [] (auto& a, auto& b) { return a.pitch < b.pitch; });
        const auto& lead = here.front();
        auto& st = P.steps[k];
        st.on = true;
        place (lead.pitch, st.note, st.octave);
        st.vel = juce::jlimit (0.05f, 1.0f, (float) lead.velocity / 110.0f);
        st.micro = (float) juce::jlimit (-0.5, 0.5, (lead.start - k * 0.25) / 0.25);
        const double steps = lead.length / 0.25;
        st.gate = (float) juce::jlimit (0.05, 1.0, steps);
        for (size_t c = 1; c < here.size(); ++c)
            st.addChord (juce::jlimit (-24, 48, here[c].pitch - 36), (float) juce::jmax (0.1, here[c].length / 0.25));
        // a long note holds over the next empty steps
        for (int j = k + 1; j < numSteps && steps > (j - k) + 0.05; ++j)
        {
            if (! at[(size_t) j].empty()) break;
            auto& tie = P.steps[j];
            tie = st;
            tie.numChord = 0;
            tie.tie = true;
            tie.micro = 0.0f;
            tie.gate = (float) juce::jlimit (0.05, 1.0, steps - (j - k));
        }
    }
    setSeqData (d);
}

juce::File BassEngineProcessor::exportPatternToMidi (int patternIndex)
{
    fillSnapshot();
    auto mf = Sequencer::toMidiFile (getSeqData(), gset.seq, patternIndex, gset.seq.bpm);
    automation.addToMidiFile (mf, juce::jlimit (1, kSeqMaxSteps, gset.seq.length) * seqRateBeats (gset.seq.rate));
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("BassEngine_Pattern_" + juce::String::charToString ((juce::juce_wchar) ('A' + patternIndex)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { mf.writeTo (os); os.flush(); }
    return f;
}

void BassEngineProcessor::setUserTable (int o, WaveTablePtr t, const juce::String& name)
{
    if (t == nullptr) return;
    userTable[o].set (t);
    auto ot = extra.getOrCreateChildWithName ("OSC" + juce::String (o), nullptr);
    // persist base-level cycles
    std::vector<float> data;
    data.reserve ((size_t) t->numFrames() * WaveTable::kBaseSize);
    for (int f = 0; f < t->numFrames(); ++f)
    {
        const float* fr = t->frameData (f, 0);
        data.insert (data.end(), fr, fr + WaveTable::kBaseSize);
    }
    ot.setProperty ("wt", floatsToBase64 (data.data(), data.size()), nullptr);
    ot.setProperty ("wtName", name, nullptr);
}

void BassEngineProcessor::clearUserTable (int o)
{
    userTable[o].set (nullptr);
    auto ot = extra.getOrCreateChildWithName ("OSC" + juce::String (o), nullptr);
    ot.removeProperty ("wt", nullptr);
    ot.removeProperty ("wtName", nullptr);
}

bool BassEngineProcessor::hasUserTable (int o) const { return userTable[o].get() != nullptr; }
bool BassEngineProcessor::hasUserSample (int o) const { return userSample[o].get() != nullptr; }
juce::String BassEngineProcessor::userTableName (int o) const { return extra.getChildWithName ("OSC" + juce::String (o)).getProperty ("wtName").toString(); }
juce::String BassEngineProcessor::userSampleName (int o) const { return extra.getChildWithName ("OSC" + juce::String (o)).getProperty ("smpName").toString(); }

void BassEngineProcessor::loadUserWavetable (int o, const juce::File& file, int cycleLength)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));
    juce::AudioBuffer<float> buf ((int) r->numChannels, len);
    r->read (&buf, 0, len, 0, true, true);
    // common wavetable files use 2048-sample frames; otherwise detect the pitch period
    const int cycle = cycleLength > 0 ? cycleLength : ((len % 2048 == 0 && len >= 2048) ? 2048 : 0);
    setUserTable (o, TableBuilders::fromAudio (buf, r->sampleRate, cycle), file.getFileNameWithoutExtension());
}

void BassEngineProcessor::loadUserSample (int o, const juce::File& file)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 12.0));
    juce::AudioBuffer<float> buf ((int) r->numChannels, len);
    r->read (&buf, 0, len, 0, true, true);
    auto sd = std::make_shared<SampleData>();
    sd->data.resize ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c) s += buf.getSample (c, i);
        sd->data[(size_t) i] = s / (float) std::max (1, buf.getNumChannels());
    }
    sd->sampleRate = r->sampleRate;
    const float hz = detectPitchHz (sd->data.data(), std::min (len, (int) (r->sampleRate * 0.5)), r->sampleRate);
    sd->rootNote = hz > 0.0f ? (float) (69.0 + 12.0 * std::log2 (hz / 440.0)) : 36.0f;
    sd->name = file.getFileNameWithoutExtension();
    userSample[o].set (sd);
    auto ot = extra.getOrCreateChildWithName ("OSC" + juce::String (o), nullptr);
    ot.setProperty ("smp", floatsToBase64 (sd->data.data(), sd->data.size()), nullptr);
    ot.setProperty ("smpName", sd->name, nullptr);
    ot.setProperty ("smpRate", sd->sampleRate, nullptr);
    ot.setProperty ("smpRoot", sd->rootNote, nullptr);
}

std::vector<float> BassEngineProcessor::getAdditiveHarmonics (int o) const
{
    auto s = extra.getChildWithName ("OSC" + juce::String (o)).getProperty ("add").toString();
    std::vector<float> h;
    for (auto& t : juce::StringArray::fromTokens (s, ",", "")) h.push_back (t.getFloatValue());
    h.resize (64, 0.0f);
    return h;
}

void BassEngineProcessor::setAdditiveHarmonics (int o, const std::vector<float>& h)
{
    juce::StringArray parts;
    for (auto v : h) parts.add (juce::String (v, 4));
    extra.getOrCreateChildWithName ("OSC" + juce::String (o), nullptr).setProperty ("add", parts.joinIntoString (","), nullptr);
    derivedKeys[o].hash = 0;   // force rebuild
}

const WaveTable* BassEngineProcessor::displayTable (int o) const
{
    const int engineType = (int) apvts.getRawParameterValue (oscId (o, "engine"))->load();
    const int wave = (int) apvts.getRawParameterValue (oscId (o, "wave"))->load();
    switch ((Engine) engineType)
    {
        case Engine::VA: return wave == (int) VAWave::Custom ? vaCustomTable.get() : nullptr;
        case Engine::Wavetable: return userTable[o].get() != nullptr ? userTable[o].get() : FactoryBank::get().table ((int) apvts.getRawParameterValue (oscId (o, "table"))->load());
        case Engine::Additive: case Engine::Spectral: return derivedTable[o].get();
        default: return nullptr;
    }
}

const SampleData* BassEngineProcessor::displaySample (int o) const
{
    return userSample[o].get() != nullptr ? userSample[o].get() : FactoryBank::get().sample ((int) apvts.getRawParameterValue (oscId (o, "sample"))->load());
}

void BassEngineProcessor::rebuildDerivedTables (bool force)
{
    for (int o = 0; o < kNumOsc; ++o)
    {
        const int eng = (int) apvts.getRawParameterValue (oscId (o, "engine"))->load();
        if (eng != (int) Engine::Additive && eng != (int) Engine::Spectral && ! force) continue;
        DerivedKey k;
        k.engine = eng;
        k.table = (int) apvts.getRawParameterValue (oscId (o, "table"))->load();
        k.mode = (int) apvts.getRawParameterValue (oscId (o, "spmode"))->load();
        k.a = std::round (apvts.getRawParameterValue (oscId (o, "pa"))->load() * 50.0f) / 50.0f;
        k.b = std::round (apvts.getRawParameterValue (oscId (o, "pb"))->load() * 50.0f) / 50.0f;
        k.c = std::round (apvts.getRawParameterValue (oscId (o, "pc"))->load() * 50.0f) / 50.0f;
        const auto addStr = extra.getChildWithName ("OSC" + juce::String (o)).getProperty ("add").toString();
        k.hash = (juce::int64) addStr.hashCode64() + (userTable[o].get() != nullptr ? 17 : 0);
        auto& old = derivedKeys[o];
        const bool same = old.engine == k.engine && old.table == k.table && old.mode == k.mode && old.a == k.a && old.b == k.b && old.hash == k.hash && old.hash != 0;
        if (same && ! force) continue;
        old = k;
        if (eng == (int) Engine::Spectral)
        {
            const WaveTable* src = userTable[o].get() != nullptr ? userTable[o].get() : FactoryBank::get().table (k.table);
            const int frame = (int) std::round (k.a * (float) (src->numFrames() - 1));
            derivedTable[o].set (TableBuilders::spectral (src->spectrum (frame), k.mode, k.b));
        }
        else
        {
            // additive: Param A = tilt (centre 0.5), B = even cut, C = odd cut
            auto h = getAdditiveHarmonics (o);
            derivedTable[o].set (TableBuilders::additive (h, (k.a - 0.5f) * 2.0f, k.c - k.b));
        }
    }
}

void BassEngineProcessor::rebuildTuning()
{
    const int t = (int) apvts.getRawParameterValue ("v_tuning")->load();
    const int root = (int) apvts.getRawParameterValue ("v_tuningroot")->load();
    lastTuning = t; lastTuningRoot = root;
    auto table = std::make_shared<TuningTable>();
    if (t == 0) { tuning.set (table); return; }
    table->active = true;
    std::vector<double> scale;   // cents per degree (12-step or custom)
    switch (t)
    {
        case 1: scale = { 0, 111.73, 203.91, 315.64, 386.31, 498.04, 582.51, 701.96, 813.69, 884.36, 1017.6, 1088.27 }; break;
        case 2: scale = { 0, 113.69, 203.91, 294.13, 407.82, 498.04, 611.73, 701.96, 815.64, 905.87, 996.09, 1109.78 }; break;
        case 3: scale = { 0, 76.05, 193.16, 310.26, 386.31, 503.42, 579.47, 696.58, 772.63, 889.74, 1006.84, 1082.89 }; break;
        case 4: scale = { 0, 90.22, 192.18, 294.13, 390.22, 498.04, 588.27, 696.09, 792.18, 888.27, 996.09, 1092.18 }; break;
        default: break;
    }
    for (int n = 0; n < 128; ++n)
    {
        const int rel = n - root;
        const int oct = (int) std::floor ((double) rel / 12.0);
        const int deg = rel - oct * 12;
        double cents = 0.0;
        if (t >= 1 && t <= 4) cents = scale[(size_t) deg] - deg * 100.0;
        else if (t == 5) cents = rel * (1200.0 / 19.0) - rel * 100.0;    // 19-EDO mapped per key
        else if (t == 6) cents = rel * 50.0 - rel * 100.0;               // 24-EDO: quarter tones per key
        else if (t == 7 && ! sclCents.empty())
        {
            const int steps = (int) sclCents.size();
            const double period = sclCents.back();
            const int o2 = (int) std::floor ((double) rel / steps);
            const int d2 = rel - o2 * steps;
            const double c = (d2 == 0 ? 0.0 : sclCents[(size_t) d2 - 1]) + o2 * period;
            cents = c - rel * 100.0;
        }
        table->cents[n] = cents;
    }
    tuning.set (table);
}

bool BassEngineProcessor::loadScl (const juce::File& f)
{
    auto lines = juce::StringArray::fromLines (f.loadFileAsString());
    std::vector<double> cents;
    int count = -1;
    for (auto& raw : lines)
    {
        auto l = raw.trim();
        if (l.startsWith ("!") || l.isEmpty()) continue;
        if (count < 0) { if (! juce::CharacterFunctions::isDigit (l[0])) continue; count = l.getIntValue(); continue; }
        auto tok = l.upToFirstOccurrenceOf (" ", false, false);
        if (tok.contains ("."))
            cents.push_back (tok.getDoubleValue());
        else if (tok.contains ("/"))
        {
            const double a = tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double b = tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            if (a > 0 && b > 0) cents.push_back (1200.0 * std::log2 (a / b));
        }
        else if (tok.getIntValue() > 0) cents.push_back (1200.0 * std::log2 ((double) tok.getIntValue()));
        if ((int) cents.size() >= count) break;
    }
    if (cents.empty()) return false;
    sclCents = cents;
    extra.setProperty ("sclName", f.getFileNameWithoutExtension(), nullptr);
    extra.setProperty ("sclData", f.loadFileAsString(), nullptr);
    if (auto* p = apvts.getParameter ("v_tuning")) p->setValueNotifyingHost (p->convertTo0to1 (7.0f));
    rebuildTuning();
    return true;
}

std::array<int, kNumFx> BassEngineProcessor::getFxOrder() const
{
    std::array<int, kNumFx> o {};
    for (int k = 0; k < kNumFx; ++k) o[(size_t) k] = fxOrder[k].load();
    return o;
}

void BassEngineProcessor::setFxOrder (const std::array<int, kNumFx>& order)
{
    juce::StringArray s;
    for (int k = 0; k < kNumFx; ++k) { fxOrder[k] = order[(size_t) k]; s.add (juce::String (order[(size_t) k])); }
    extra.setProperty ("fxOrder", s.joinIntoString (","), nullptr);
}

juce::String BassEngineProcessor::getMacroName (int i) const
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("macroNames").toString(), "|", "");
    return i < names.size() && names[i].isNotEmpty() ? names[i] : "MACRO " + juce::String (i + 1);
}

void BassEngineProcessor::setMacroName (int i, const juce::String& n)
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("macroNames").toString(), "|", "");
    while (names.size() < kNumMacros) names.add ({});
    names.set (i, n.replace ("|", "/"));
    extra.setProperty ("macroNames", names.joinIntoString ("|"), nullptr);
}

void BassEngineProcessor::clearMidiLearn (const juce::String& paramId)
{
    for (int i = 0; i < paramList.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == paramId)
            for (auto& c : ccMap) if (c.load() == i) c = -1;
}

juce::String BassEngineProcessor::midiLearnFor (const juce::String& paramId) const
{
    for (int cc = 0; cc < 128; ++cc)
    {
        const int i = ccMap[cc].load();
        if (i >= 0 && i < paramList.size())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == paramId)
                return "CC " + juce::String (cc);
    }
    return {};
}

// ===================================================================== A/B & morph
void BassEngineProcessor::storeAB (int slot) { abState[slot] = createPatchState(); }

void BassEngineProcessor::recallAB (int slot)
{
    storeAB (abSlot);
    abSlot = slot;
    if (abState[slot].isValid()) loadPatchState (abState[slot], true);
}

void BassEngineProcessor::copyAtoB() { abState[0] = createPatchState(); abState[1] = abState[0].createCopy(); }

void BassEngineProcessor::captureMorphCorner (int corner)
{
    auto morph = extra.getOrCreateChildWithName ("MORPH", nullptr);
    juce::ValueTree c ("C" + juce::String (corner));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto& id = rp->paramID;
            if (id.startsWith ("xy_") || id.startsWith ("m") || id == "v_quality" || id.startsWith ("seq_")) continue;
            c.setProperty (id, rp->getValue(), nullptr);
        }
    if (auto old = morph.getChildWithName (c.getType()); old.isValid()) morph.removeChild (old, nullptr);
    morph.appendChild (c, nullptr);
}

bool BassEngineProcessor::morphCornerSet (int corner) const
{
    return extra.getChildWithName ("MORPH").getChildWithName ("C" + juce::String (corner)).isValid();
}

void BassEngineProcessor::applyMorph (float x, float y)
{
    auto morph = extra.getChildWithName ("MORPH");
    juce::ValueTree c[4];
    for (int i = 0; i < 4; ++i) c[i] = morph.getChildWithName ("C" + juce::String (i));
    // corners: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right (y = 0 top)
    const float w[4] { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };
    float wsum = 0.0f;
    for (int i = 0; i < 4; ++i) if (c[i].isValid()) wsum += w[i];
    if (wsum <= 0.0f) return;
    int best = 0;
    for (int i = 1; i < 4; ++i) if (c[i].isValid() && (w[i] > w[best] || ! c[best].isValid())) best = i;
    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        const juce::Identifier id (rp->paramID);
        if (! c[best].hasProperty (id)) continue;
        const bool discrete = dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr || dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr
                              || dynamic_cast<juce::AudioParameterInt*> (rp) != nullptr;
        float v;
        if (discrete) v = (float) c[best].getProperty (id);
        else
        {
            v = 0.0f;
            for (int i = 0; i < 4; ++i) if (c[i].isValid()) v += w[i] / wsum * (float) c[i].getProperty (id, rp->getValue());
        }
        if (std::abs (rp->getValue() - v) > 1.0e-4f) rp->setValueNotifyingHost (v);
    }
}

// ===================================================================== state
juce::ValueTree BassEngineProcessor::createPatchState() const
{
    juce::ValueTree root ("BASSENGINE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("presetName", currentPresetName, nullptr);
    root.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    root.appendChild (extra.createCopy(), nullptr);
    return root;
}

void BassEngineProcessor::applyExtraToAssets()
{
    // shapes
    auto shapes = extra.getChildWithName ("SHAPES");
    for (int i = 0; i < kNumShapes; ++i)
    {
        auto s = shapes.getProperty ("s" + juce::String (i)).toString();
        auto pts = s.isNotEmpty() ? shapeFromString (s) : (i < kNumEnvs ? defaultEnvShape() : defaultLfoShape());
        setShape (i, pts);
    }
    // sequencer
    SeqData d;
    d.initDefault();
    if (auto st = extra.getChildWithName ("SEQ"); st.isValid() && st.getNumChildren() > 0) d.fromValueTree (st);
    setSeqData (d);
    // per-osc assets
    for (int o = 0; o < kNumOsc; ++o)
    {
        auto ot = extra.getChildWithName ("OSC" + juce::String (o));
        auto wt = base64ToFloats (ot.getProperty ("wt").toString());
        if (wt.size() >= (size_t) WaveTable::kBaseSize)
        {
            const int frames = (int) (wt.size() / WaveTable::kBaseSize);
            std::vector<std::vector<float>> cycles ((size_t) frames);
            for (int f = 0; f < frames; ++f) cycles[(size_t) f].assign (wt.begin() + f * WaveTable::kBaseSize, wt.begin() + (f + 1) * WaveTable::kBaseSize);
            userTable[o].set (WaveTable::fromCycles (cycles));
        }
        else userTable[o].set (nullptr);
        auto smp = base64ToFloats (ot.getProperty ("smp").toString());
        if (! smp.empty())
        {
            auto sd = std::make_shared<SampleData>();
            sd->data = std::move (smp);
            sd->sampleRate = (double) ot.getProperty ("smpRate", 48000.0);
            sd->rootNote = (float) ot.getProperty ("smpRoot", 36.0f);
            sd->name = ot.getProperty ("smpName").toString();
            userSample[o].set (sd);
        }
        else userSample[o].set (nullptr);
        if (! ot.hasProperty ("add"))
        {
            std::vector<float> h (64);
            for (int i = 0; i < 64; ++i) h[(size_t) i] = 1.0f / (float) (i + 1);
            setAdditiveHarmonics (o, h);
        }
        derivedKeys[o].hash = 0;
    }
    // fx order
    auto ord = juce::StringArray::fromTokens (extra.getProperty ("fxOrder").toString(), ",", "");
    std::array<int, kNumFx> order {};
    bool valid = ord.size() == kNumFx;
    const int def[kNumFx] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3 };
    for (int k = 0; k < kNumFx; ++k) order[(size_t) k] = valid ? ord[k].getIntValue() : def[k];
    setFxOrder (order);
    // midi learn
    for (auto& c : ccMap) c = -1;
    for (auto& tok : juce::StringArray::fromTokens (extra.getProperty ("midiLearn").toString(), ";", ""))
    {
        const int cc = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const auto id = tok.fromFirstOccurrenceOf (":", false, false);
        for (int i = 0; i < paramList.size(); ++i)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == id && cc >= 0 && cc < 128) ccMap[cc] = i;
    }
    // custom tuning
    if (extra.hasProperty ("sclData"))
    {
        auto tmp = juce::File::createTempFile (".scl");
        tmp.replaceWithText (extra.getProperty ("sclData").toString());
        auto keepName = extra.getProperty ("sclName").toString();
        sclCents.clear();
        auto lines = juce::StringArray::fromLines (tmp.loadFileAsString());
        tmp.deleteFile();
        int count = -1;
        for (auto& raw : lines)
        {
            auto l = raw.trim();
            if (l.startsWith ("!") || l.isEmpty()) continue;
            if (count < 0) { count = l.getIntValue(); continue; }
            auto tok = l.upToFirstOccurrenceOf (" ", false, false);
            if (tok.contains (".")) sclCents.push_back (tok.getDoubleValue());
            else if (tok.contains ("/")) sclCents.push_back (1200.0 * std::log2 (tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue() / std::max (1.0, tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue())));
            if ((int) sclCents.size() >= count) break;
        }
    }
    rebuildDerivedTables (true);
    rebuildTuning();
}

void BassEngineProcessor::loadPatchState (const juce::ValueTree& v, bool keepSequencer)
{
    auto params = v.getChildWithName (apvts.state.getType());
    if (params.isValid())
    {
        // set every parameter explicitly so the host is notified
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                if (keepSequencer && rp->paramID.startsWith ("seq_")) continue;
                if (rp->paramID == "v_quality") continue;
                auto child = params.getChildWithProperty ("id", rp->paramID);
                const float def = rp->getDefaultValue();
                const float val = child.isValid() ? rp->convertTo0to1 ((float) child.getProperty ("value")) : def;
                rp->setValueNotifyingHost (val);
            }
    }
    auto ex = v.getChildWithName ("EXTRA");
    if (ex.isValid())
    {
        auto seqKeep = extra.getChildWithName ("SEQ").createCopy();
        extra.copyPropertiesAndChildrenFrom (ex, nullptr);
        if (keepSequencer && seqKeep.isValid())
        {
            if (auto s = extra.getChildWithName ("SEQ"); s.isValid()) extra.removeChild (s, nullptr);
            extra.appendChild (seqKeep, nullptr);
        }
    }
    currentPresetName = v.getProperty ("presetName", "Init").toString();
    applyExtraToAssets();
    engine.allNotesOff();
    if (onPresetLoaded) onPresetLoaded();
}

void BassEngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // midi learn map into extra
    juce::StringArray ml;
    for (int cc = 0; cc < 128; ++cc)
    {
        const int i = ccMap[cc].load();
        if (i >= 0 && i < paramList.size())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i])) ml.add (juce::String (cc) + ":" + rp->paramID);
    }
    extra.setProperty ("midiLearn", ml.joinIntoString (";"), nullptr);
    auto state = createPatchState();
    state.setProperty ("program", currentProgram, nullptr);
    state.appendChild (automation.toTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void BassEngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto v = juce::ValueTree::fromXml (*xml);
        if (! v.hasType ("BASSENGINE")) return;
        auto params = v.getChildWithName (apvts.state.getType());
        if (params.isValid())
        {
            apvts.replaceState (params);
            // push every value explicitly so wrappers that cache normalised values (VST3) stay in sync
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = params.getChildWithProperty ("id", rp->paramID);
                    const float v01 = child.isValid() ? rp->convertTo0to1 ((float) child.getProperty ("value")) : rp->getDefaultValue();
                    rp->setValueNotifyingHost (v01);
                }
        }
        auto ex = v.getChildWithName ("EXTRA");
        if (ex.isValid()) extra.copyPropertiesAndChildrenFrom (ex, nullptr);
        clipEditing = (bool) extra.getProperty ("clipEditing", false);
        currentPresetName = v.getProperty ("presetName", "Init").toString();
        currentProgram = (int) v.getProperty ("program", 0);
        applyExtraToAssets();
        automation.fromTree (v.getChildWithName (racoon::Automation::treeType));
        if (onPresetLoaded) juce::MessageManager::callAsync ([this] { if (onPresetLoaded) onPresetLoaded(); });
    }
}

// ===================================================================== timer (message thread)
void BassEngineProcessor::timerCallback()
{
    rebuildDerivedTables (false);
    const int t = (int) apvts.getRawParameterValue ("v_tuning")->load();
    const int r = (int) apvts.getRawParameterValue ("v_tuningroot")->load();
    if (t != lastTuning || r != lastTuningRoot) rebuildTuning();
    if (apvts.getRawParameterValue ("xy_on")->load() > 0.5f)
    {
        const float x = apvts.getRawParameterValue ("xy_x")->load(), y = apvts.getRawParameterValue ("xy_y")->load();
        if (std::abs (x - lastXY[0]) > 1.0e-4f || std::abs (y - lastXY[1]) > 1.0e-4f)
        {
            lastXY[0] = x; lastXY[1] = y;
            applyMorph (x, y);
        }
    }
    for (int o = 0; o < kNumOsc; ++o) { userTable[o].collect(); derivedTable[o].collect(); userSample[o].collect(); }
}

// ===================================================================== programs
int BassEngineProcessor::getNumPrograms() { return std::max (1, presetManager->numFactory()); }
int BassEngineProcessor::getCurrentProgram() { return currentProgram; }
void BassEngineProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetManager->numFactory()) return;
    currentProgram = index;
    presetManager->loadFactory (index);
}
const juce::String BassEngineProcessor::getProgramName (int index) { return presetManager->factoryName (index); }

juce::AudioProcessorEditor* BassEngineProcessor::createEditor() { return new BassEngineEditor (*this); }

#if ! BE_TEST_HARNESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BassEngineProcessor(); }
#endif

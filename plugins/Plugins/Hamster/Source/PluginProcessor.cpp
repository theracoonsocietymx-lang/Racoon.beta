#include <set>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/PresetManager.h"
#include "presets/LibraryKits.h"
#include "Biblioteca.h"

using namespace be;

// ===================================================================== parameter cache (globals)
struct DrumEngineProcessor::Cache
{
    using A = std::atomic<float>*;
    juce::AudioProcessorValueTreeState& s;
    explicit Cache (juce::AudioProcessorValueTreeState& st) : s (st) {}
    A get (const juce::String& id) { auto* p = s.getRawParameterValue (id); jassert (p != nullptr); return p; }

    A play, follow, bpm, swing, pattern, songmode, fill, fillevery, brk, scatter, morph, morphtarget, groove, humanize, quality, noterepeat, velcurve, record,
      accent, master, xyx, xyy, scenea, sceneb, scenefade, sceneon;
    struct Gl { A wave, rate, sync, div, depth, phase, polarity; } gl[kNumGLfos];
    struct Rt { A on, src, trk, dst, amt, curve; } rt[kNumRoutes];
    A macro[kNumMacros];
    struct Ax { A type, p1, p2, p3, p4, ret, lowcut, hicut; } ax[kNumAux];
    struct Gr { A level, comp, drive, width; } gr[kNumGroups];
    A glueOn, glueThresh, glueRatio, glueAttack, glueRelease, glueMakeup, glueMix, softclip, tpOn, tpCeiling;
    A fxOn[kNumFx];
    std::map<juce::String, A> fx;
    struct FxPtrs { A eq_hpf, eq_lsfreq, eq_lsgain, eq_p1freq, eq_p1gain, eq_p1q, eq_p2freq, eq_p2gain, eq_p2q, eq_hsfreq, eq_hsgain, comp_thresh, comp_ratio, comp_attack, comp_release, comp_makeup, comp_mix, mbcomp_xlow, mbcomp_xhigh, mbcomp_low, mbcomp_mid, mbcomp_high, mbcomp_ratio, mbcomp_mix, limiter_ceiling, limiter_release, clipper_drive, clipper_ceiling, clipper_knee, chorus_rate, chorus_depth, chorus_delay, chorus_feedback, chorus_mix, chorus_lowcut, phaser_rate, phaser_depth, phaser_centre, phaser_feedback, phaser_mix, phaser_lowcut, flanger_rate, flanger_depth, flanger_delay, flanger_feedback, flanger_mix, flanger_lowcut, delay_time, delay_sync, delay_div, delay_feedback, delay_pingpong, delay_highcut, delay_mix, delay_lowcut, reverb_size, reverb_damp, reverb_width, reverb_predelay, reverb_mix, reverb_lowcut, freqshift_shift, freqshift_mix, ringmod_freq, ringmod_ratio, ringmod_mix, transient_attack, transient_sustain, transient_speed, imager_width, imager_bassmono, imager_monofreq, autofilter_type, autofilter_cutoff, autofilter_res, autofilter_rate, autofilter_sync, autofilter_div, autofilter_depth, autofilter_mix, resonator_tune, resonator_decay, resonator_bright, resonator_mix; } fxp;
    A duckSrc, duckTrack, duckTarget, duckDiv, duckDepth, duckAttack, duckHold, duckRelease, duckCurve, duckBand, duckFreq, duckNote, duckChannel, duckThresh;
    A bridgeOn, bridgeTrack;

    void build()
    {
        play = get ("g_play"); follow = get ("g_follow"); bpm = get ("g_bpm"); swing = get ("g_swing"); pattern = get ("g_pattern"); songmode = get ("g_songmode");
        fill = get ("g_fill"); fillevery = get ("g_fillevery"); brk = get ("g_break"); scatter = get ("g_scatter"); morph = get ("g_morph");
        morphtarget = get ("g_morphtarget"); groove = get ("g_groove"); humanize = get ("g_humanize"); quality = get ("g_quality");
        noterepeat = get ("g_noterepeat"); velcurve = get ("g_velcurve"); record = get ("g_record"); accent = get ("g_accent"); master = get ("g_master");
        xyx = get ("g_xyx"); xyy = get ("g_xyy"); scenea = get ("g_scenea"); sceneb = get ("g_sceneb"); scenefade = get ("g_scenefade"); sceneon = get ("g_sceneon");
        for (int l = 0; l < kNumGLfos; ++l)
        {
            auto g = [&] (const char* n) { return get (glfoId (l, n)); };
            gl[l] = { g ("wave"), g ("rate"), g ("sync"), g ("div"), g ("depth"), g ("phase"), g ("polarity") };
        }
        for (int r = 0; r < kNumRoutes; ++r)
        {
            auto g = [&] (const char* n) { return get (routeId (r, n)); };
            rt[r] = { g ("on"), g ("src"), g ("trk"), g ("dst"), g ("amt"), g ("curve") };
        }
        for (int m = 0; m < kNumMacros; ++m) macro[m] = get (macroId (m));
        for (int a = 0; a < kNumAux; ++a)
        {
            auto g = [&] (const char* n) { return get (auxId (a, n)); };
            ax[a] = { g ("type"), g ("p1"), g ("p2"), g ("p3"), g ("p4"), g ("return"), g ("lowcut"), g ("hicut") };
        }
        for (int k = 0; k < kNumGroups; ++k)
        {
            auto g = [&] (const char* n) { return get (groupId (k, n)); };
            gr[k] = { g ("level"), g ("comp"), g ("drive"), g ("width") };
        }
        glueOn = get ("glue_on"); glueThresh = get ("glue_thresh"); glueRatio = get ("glue_ratio"); glueAttack = get ("glue_attack");
        glueRelease = get ("glue_release"); glueMakeup = get ("glue_makeup"); glueMix = get ("glue_mix"); softclip = get ("softclip");
        tpOn = get ("tp_on"); tpCeiling = get ("tp_ceiling");
        const char* fxKeys[kNumFx] { "eq", "comp", "mbcomp", "limiter", "clipper", "chorus", "phaser", "flanger", "delay", "reverb", "freqshift", "ringmod", "transient", "imager", "autofilter", "resonator" };
        for (int k = 0; k < kNumFx; ++k) fxOn[k] = get (fxId (fxKeys[k], "on"));
        for (auto* p : s.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID.startsWith ("fx_")) fx[rp->paramID] = get (rp->paramID);
        duckSrc = get ("duck_src"); duckTrack = get ("duck_track"); duckTarget = get ("duck_target"); duckDiv = get ("duck_div"); duckDepth = get ("duck_depth");
        duckAttack = get ("duck_attack"); duckHold = get ("duck_hold"); duckRelease = get ("duck_release"); duckCurve = get ("duck_curve");
        duckBand = get ("duck_band"); duckFreq = get ("duck_freq"); duckNote = get ("duck_note"); duckChannel = get ("duck_channel"); duckThresh = get ("duck_thresh");
        bridgeOn = get ("bridge_on"); bridgeTrack = get ("bridge_track");
        fxp.eq_hpf = get (fxId ("eq", "hpf"));
        fxp.eq_lsfreq = get (fxId ("eq", "lsfreq"));
        fxp.eq_lsgain = get (fxId ("eq", "lsgain"));
        fxp.eq_p1freq = get (fxId ("eq", "p1freq"));
        fxp.eq_p1gain = get (fxId ("eq", "p1gain"));
        fxp.eq_p1q = get (fxId ("eq", "p1q"));
        fxp.eq_p2freq = get (fxId ("eq", "p2freq"));
        fxp.eq_p2gain = get (fxId ("eq", "p2gain"));
        fxp.eq_p2q = get (fxId ("eq", "p2q"));
        fxp.eq_hsfreq = get (fxId ("eq", "hsfreq"));
        fxp.eq_hsgain = get (fxId ("eq", "hsgain"));
        fxp.comp_thresh = get (fxId ("comp", "thresh"));
        fxp.comp_ratio = get (fxId ("comp", "ratio"));
        fxp.comp_attack = get (fxId ("comp", "attack"));
        fxp.comp_release = get (fxId ("comp", "release"));
        fxp.comp_makeup = get (fxId ("comp", "makeup"));
        fxp.comp_mix = get (fxId ("comp", "mix"));
        fxp.mbcomp_xlow = get (fxId ("mbcomp", "xlow"));
        fxp.mbcomp_xhigh = get (fxId ("mbcomp", "xhigh"));
        fxp.mbcomp_low = get (fxId ("mbcomp", "low"));
        fxp.mbcomp_mid = get (fxId ("mbcomp", "mid"));
        fxp.mbcomp_high = get (fxId ("mbcomp", "high"));
        fxp.mbcomp_ratio = get (fxId ("mbcomp", "ratio"));
        fxp.mbcomp_mix = get (fxId ("mbcomp", "mix"));
        fxp.limiter_ceiling = get (fxId ("limiter", "ceiling"));
        fxp.limiter_release = get (fxId ("limiter", "release"));
        fxp.clipper_drive = get (fxId ("clipper", "drive"));
        fxp.clipper_ceiling = get (fxId ("clipper", "ceiling"));
        fxp.clipper_knee = get (fxId ("clipper", "knee"));
        fxp.chorus_rate = get (fxId ("chorus", "rate"));
        fxp.chorus_depth = get (fxId ("chorus", "depth"));
        fxp.chorus_delay = get (fxId ("chorus", "delay"));
        fxp.chorus_feedback = get (fxId ("chorus", "feedback"));
        fxp.chorus_mix = get (fxId ("chorus", "mix"));
        fxp.chorus_lowcut = get (fxId ("chorus", "lowcut"));
        fxp.phaser_rate = get (fxId ("phaser", "rate"));
        fxp.phaser_depth = get (fxId ("phaser", "depth"));
        fxp.phaser_centre = get (fxId ("phaser", "centre"));
        fxp.phaser_feedback = get (fxId ("phaser", "feedback"));
        fxp.phaser_mix = get (fxId ("phaser", "mix"));
        fxp.phaser_lowcut = get (fxId ("phaser", "lowcut"));
        fxp.flanger_rate = get (fxId ("flanger", "rate"));
        fxp.flanger_depth = get (fxId ("flanger", "depth"));
        fxp.flanger_delay = get (fxId ("flanger", "delay"));
        fxp.flanger_feedback = get (fxId ("flanger", "feedback"));
        fxp.flanger_mix = get (fxId ("flanger", "mix"));
        fxp.flanger_lowcut = get (fxId ("flanger", "lowcut"));
        fxp.delay_time = get (fxId ("delay", "time"));
        fxp.delay_sync = get (fxId ("delay", "sync"));
        fxp.delay_div = get (fxId ("delay", "div"));
        fxp.delay_feedback = get (fxId ("delay", "feedback"));
        fxp.delay_pingpong = get (fxId ("delay", "pingpong"));
        fxp.delay_highcut = get (fxId ("delay", "highcut"));
        fxp.delay_mix = get (fxId ("delay", "mix"));
        fxp.delay_lowcut = get (fxId ("delay", "lowcut"));
        fxp.reverb_size = get (fxId ("reverb", "size"));
        fxp.reverb_damp = get (fxId ("reverb", "damp"));
        fxp.reverb_width = get (fxId ("reverb", "width"));
        fxp.reverb_predelay = get (fxId ("reverb", "predelay"));
        fxp.reverb_mix = get (fxId ("reverb", "mix"));
        fxp.reverb_lowcut = get (fxId ("reverb", "lowcut"));
        fxp.freqshift_shift = get (fxId ("freqshift", "shift"));
        fxp.freqshift_mix = get (fxId ("freqshift", "mix"));
        fxp.ringmod_freq = get (fxId ("ringmod", "freq"));
        fxp.ringmod_ratio = get (fxId ("ringmod", "ratio"));
        fxp.ringmod_mix = get (fxId ("ringmod", "mix"));
        fxp.transient_attack = get (fxId ("transient", "attack"));
        fxp.transient_sustain = get (fxId ("transient", "sustain"));
        fxp.transient_speed = get (fxId ("transient", "speed"));
        fxp.imager_width = get (fxId ("imager", "width"));
        fxp.imager_bassmono = get (fxId ("imager", "bassmono"));
        fxp.imager_monofreq = get (fxId ("imager", "monofreq"));
        fxp.autofilter_type = get (fxId ("autofilter", "type"));
        fxp.autofilter_cutoff = get (fxId ("autofilter", "cutoff"));
        fxp.autofilter_res = get (fxId ("autofilter", "res"));
        fxp.autofilter_rate = get (fxId ("autofilter", "rate"));
        fxp.autofilter_sync = get (fxId ("autofilter", "sync"));
        fxp.autofilter_div = get (fxId ("autofilter", "div"));
        fxp.autofilter_depth = get (fxId ("autofilter", "depth"));
        fxp.autofilter_mix = get (fxId ("autofilter", "mix"));
        fxp.resonator_tune = get (fxId ("resonator", "tune"));
        fxp.resonator_decay = get (fxId ("resonator", "decay"));
        fxp.resonator_bright = get (fxId ("resonator", "bright"));
        fxp.resonator_mix = get (fxId ("resonator", "mix"));
    }
    float f (const juce::String& id) const { return fx.at (id)->load(); }
};

namespace
{
inline int I (std::atomic<float>* a) { return (int) std::round (a->load()); }
inline bool B (std::atomic<float>* a) { return a->load() > 0.5f; }
inline float F (std::atomic<float>* a) { return a->load(); }

juce::String samplesToBase64 (const SampleData& s)
{
    juce::MemoryOutputStream mo;
    const int n = s.size();
    const int ch = s.stereo() ? 2 : 1;
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const float v = c == 0 ? s.l[(size_t) i] : s.r[(size_t) i];
            mo.writeShort ((short) juce::jlimit (-32767, 32767, juce::roundToInt (v * 32767.0f)));
        }
    return juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}

SamplePtr samplesFromBase64 (const juce::String& b64, int ch, double rate, const juce::String& name)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64)) return {};
    juce::MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
    const int frames = (int) (mo.getDataSize() / 2 / (size_t) std::max (1, ch));
    std::vector<float> l ((size_t) frames), r;
    if (ch > 1) r.resize ((size_t) frames);
    for (int i = 0; i < frames; ++i)
    {
        l[(size_t) i] = (float) mi.readShort() / 32767.0f;
        if (ch > 1) r[(size_t) i] = (float) mi.readShort() / 32767.0f;
    }
    return makeSample (std::move (l), std::move (r), rate, name);
}

}

juce::String DrumEngineProcessor::roleName (int role) { return roleNames()[role]; }

juce::AudioProcessor::BusesProperties DrumEngineProcessor::makeBuses()
{
    auto b = BusesProperties()
                 .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                 .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
    for (int t = 0; t < kNumTracks; ++t)
        b = b.withOutput ("Out " + juce::String (t + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

// ===================================================================== construction
DrumEngineProcessor::DrumEngineProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, &undo, "PARAMS", createParameterLayout())
{
    FactoryPcm::get();
    cache = std::make_unique<Cache> (apvts);
    for (auto& tr : slots) for (auto& sl : tr) sl.inUse = [this] (const be::SampleData* p) { return engine.sampleInUse (p); };
    cache->build();
    paramList = getParameters();
    for (int t = 0; t < kNumTracks; ++t)
        for (int p = 0; p < T_COUNT; ++p)
        {
            const auto id = trackParamId (t, p);
            trackAtoms[t][p] = apvts.getRawParameterValue (id);
            trackParams[t][p] = apvts.getParameter (id);
            jassert (trackAtoms[t][p] != nullptr && trackParams[t][p] != nullptr);
        }
    for (auto& c : ccMap) c = -1;
    { const int def[kNumFx] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3 }; for (int k = 0; k < kNumFx; ++k) fxOrder[k] = def[k]; }
    for (int i = 0; i < kNumShapes; ++i) setShape (i, defaultLfoShape());

    presetManager = std::make_unique<PresetManager> (*this);
    // start from the default kit so the first open sounds like a drum machine
    presetManager->loadInitKit();
    for (int t = 0; t < kNumTracks; ++t) for (int p = 0; p < T_COUNT; ++p) lastTrackNorm[t][p] = trackParams[t][p]->getValue();
    startTimerHz (30);
}

DrumEngineProcessor::~DrumEngineProcessor() { stopTimer(); }

bool DrumEngineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void DrumEngineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    automation.prepare (sampleRate);
    sampleRateCache = sampleRate;
    engine.prepare (sampleRate, std::max (samplesPerBlock, 512));
    setLatencySamples (engine.latencySamples());
    updateAuxImpulses();
}

// ===================================================================== snapshot
void DrumEngineProcessor::fillSnapshot (EngineSnap& p)
{
    {
        // with "Afinar el kit a" on, the resonator and the ring modulator sing the key's root (between E1 and D#2)
        const int key = juce::roundToInt (getParamReal ("g_kitkey"));
        p.keyHz = key > 0 ? 55.0f * std::pow (2.0f, (float) (((key - 1 - 9 + 12 + 5) % 12) - 5) / 12.0f) : 55.0f;
    }
    auto& c = *cache;
    const auto& pcm = FactoryPcm::get();
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int i = 0; i < T_COUNT; ++i)
        {
            p.base[t][i] = trackAtoms[t][i]->load (std::memory_order_relaxed);
            p.norm[t][i] = trackParams[t][i]->getValue();
        }
        auto& a = p.assets[t];
        a.factory = pcm.sample ((int) p.base[t][T_C_SAMPLE]);
        a.roleDefault = pcm.sample (FactoryPcm::defaultForRole ((int) p.base[t][T_ROLE]));
        for (int k = 0; k < kNumSlots; ++k) a.slots[k] = slots[t][k].get();
    }
    p.play = B (c.play); p.followHost = B (c.follow); p.bpm = F (c.bpm); p.swing = F (c.swing); p.pattern = I (c.pattern) - 1; p.songMode = B (c.songmode);
    p.fill = B (c.fill); p.fillEvery = I (c.fillevery); p.brk = B (c.brk); p.scatter = I (c.scatter); p.morph = F (c.morph);
    p.morphTarget = I (c.morphtarget); p.groove = F (c.groove); p.humanize = F (c.humanize); p.quality = isNonRealtime() ? 2 : I (c.quality);
    p.noteRepeat = I (c.noterepeat); p.velCurve = F (c.velcurve); p.record = B (c.record); p.accent = F (c.accent);
    p.xyX = F (c.xyx); p.xyY = F (c.xyy); p.sceneFade = F (c.scenefade);
    for (int l = 0; l < kNumGLfos; ++l)
    {
        auto& L = p.glfo[l]; auto& g = c.gl[l];
        L.wave = I (g.wave); L.rate = F (g.rate); L.sync = B (g.sync); L.divBeats = syncDivisionBeats (I (g.div)); L.trigger = 0;
        L.depth = F (g.depth); L.phase = F (g.phase); L.unipolar = I (g.polarity) == 1; L.lut = shapeSlots[(size_t) l].get();
    }
    p.numRoutes = 0;
    for (int r = 0; r < kNumRoutes; ++r)
    {
        auto& R = c.rt[r];
        if (! B (R.on)) continue;
        const int src = I (R.src), dst = I (R.dst);
        const float amt = F (R.amt);
        if (src <= 0 || dst <= 0 || std::abs (amt) < 1.0e-5f) continue;
        p.routes[p.numRoutes++] = { src, I (R.trk), dst, I (R.curve), amt };
    }
    for (int m = 0; m < kNumMacros; ++m) p.macros[m] = F (c.macro[m]);
    for (int a = 0; a < kNumAux; ++a)
    {
        auto& A_ = c.ax[a];
        p.aux[a] = { I (A_.type), F (A_.p1), F (A_.p2), F (A_.p3), F (A_.p4), F (A_.ret), F (A_.lowcut), F (A_.hicut) };
    }
    for (int g = 0; g < kNumGroups; ++g) p.grp[g] = { F (c.gr[g].level), F (c.gr[g].comp), F (c.gr[g].drive), F (c.gr[g].width) };
    auto& m = p.master;
    m.glueOn = B (c.glueOn); m.glueThresh = F (c.glueThresh); m.glueRatio = F (c.glueRatio); m.glueAttack = F (c.glueAttack);
    m.glueRelease = F (c.glueRelease); m.glueMakeup = F (c.glueMakeup); m.glueMix = F (c.glueMix); m.softclip = F (c.softclip);
    m.tpOn = B (c.tpOn); m.tpCeiling = F (c.tpCeiling); m.volume = F (c.master);

    auto& x = p.fx;
    for (int k = 0; k < kNumFx; ++k) { x.on[k] = B (c.fxOn[k]); x.order[k] = fxOrder[k].load(); }
    x.eqHpf = F (c.fxp.eq_hpf); x.eqLsF = F (c.fxp.eq_lsfreq); x.eqLsG = F (c.fxp.eq_lsgain); x.eqP1F = F (c.fxp.eq_p1freq); x.eqP1G = F (c.fxp.eq_p1gain);
    x.eqP1Q = F (c.fxp.eq_p1q); x.eqP2F = F (c.fxp.eq_p2freq); x.eqP2G = F (c.fxp.eq_p2gain); x.eqP2Q = F (c.fxp.eq_p2q); x.eqHsF = F (c.fxp.eq_hsfreq); x.eqHsG = F (c.fxp.eq_hsgain);
    x.cThresh = F (c.fxp.comp_thresh); x.cRatio = F (c.fxp.comp_ratio); x.cAttack = F (c.fxp.comp_attack); x.cRelease = F (c.fxp.comp_release);
    x.cMakeup = F (c.fxp.comp_makeup); x.cMix = F (c.fxp.comp_mix);
    x.mbXLow = F (c.fxp.mbcomp_xlow); x.mbXHigh = F (c.fxp.mbcomp_xhigh); x.mbLow = F (c.fxp.mbcomp_low); x.mbMid = F (c.fxp.mbcomp_mid);
    x.mbHigh = F (c.fxp.mbcomp_high); x.mbRatio = F (c.fxp.mbcomp_ratio); x.mbMix = F (c.fxp.mbcomp_mix);
    x.limCeiling = F (c.fxp.limiter_ceiling); x.limRelease = F (c.fxp.limiter_release);
    x.clipDrive = F (c.fxp.clipper_drive); x.clipCeiling = F (c.fxp.clipper_ceiling); x.clipKnee = F (c.fxp.clipper_knee);
    x.chRate = F (c.fxp.chorus_rate); x.chDepth = F (c.fxp.chorus_depth); x.chDelay = F (c.fxp.chorus_delay); x.chFb = F (c.fxp.chorus_feedback); x.chMix = F (c.fxp.chorus_mix); x.chLowCut = F (c.fxp.chorus_lowcut);
    x.phRate = F (c.fxp.phaser_rate); x.phDepth = F (c.fxp.phaser_depth); x.phCentre = F (c.fxp.phaser_centre); x.phFb = F (c.fxp.phaser_feedback); x.phMix = F (c.fxp.phaser_mix); x.phLowCut = F (c.fxp.phaser_lowcut);
    x.flRate = F (c.fxp.flanger_rate); x.flDepth = F (c.fxp.flanger_depth); x.flDelay = F (c.fxp.flanger_delay); x.flFb = F (c.fxp.flanger_feedback); x.flMix = F (c.fxp.flanger_mix); x.flLowCut = F (c.fxp.flanger_lowcut);
    x.dlTime = F (c.fxp.delay_time); x.dlSync = F (c.fxp.delay_sync) > 0.5f; x.dlDiv = (int) F (c.fxp.delay_div); x.dlFb = F (c.fxp.delay_feedback);
    x.dlPing = F (c.fxp.delay_pingpong) > 0.5f; x.dlHighCut = F (c.fxp.delay_highcut); x.dlMix = F (c.fxp.delay_mix); x.dlLowCut = F (c.fxp.delay_lowcut);
    x.rvSize = F (c.fxp.reverb_size); x.rvDamp = F (c.fxp.reverb_damp); x.rvWidth = F (c.fxp.reverb_width); x.rvPre = F (c.fxp.reverb_predelay); x.rvMix = F (c.fxp.reverb_mix); x.rvLowCut = F (c.fxp.reverb_lowcut);
    x.fsShift = F (c.fxp.freqshift_shift); x.fsMix = F (c.fxp.freqshift_mix);
    x.rmFreq = F (c.fxp.ringmod_freq); x.rmMode = 0; x.rmRatio = F (c.fxp.ringmod_ratio); x.rmMix = F (c.fxp.ringmod_mix);
    x.trAttack = F (c.fxp.transient_attack); x.trSustain = F (c.fxp.transient_sustain); x.trSpeed = F (c.fxp.transient_speed);
    x.imWidth = F (c.fxp.imager_width); x.imBassMono = F (c.fxp.imager_bassmono) > 0.5f; x.imMonoFreq = F (c.fxp.imager_monofreq);
    x.afType = (int) F (c.fxp.autofilter_type); x.afCutoff = F (c.fxp.autofilter_cutoff); x.afRes = F (c.fxp.autofilter_res); x.afRate = F (c.fxp.autofilter_rate);
    x.afSync = F (c.fxp.autofilter_sync) > 0.5f; x.afDiv = (int) F (c.fxp.autofilter_div); x.afDepth = F (c.fxp.autofilter_depth); x.afMix = F (c.fxp.autofilter_mix);
    x.rsTune = F (c.fxp.resonator_tune); x.rsDecay = F (c.fxp.resonator_decay); x.rsBright = F (c.fxp.resonator_bright); x.rsMix = F (c.fxp.resonator_mix);

    auto& dk = p.duck;
    dk.source = I (c.duckSrc); dk.division = I (c.duckDiv); dk.depth = F (c.duckDepth); dk.attackMs = F (c.duckAttack); dk.holdMs = F (c.duckHold);
    dk.releaseMs = F (c.duckRelease); dk.curve = F (c.duckCurve); dk.band = I (c.duckBand); dk.freq = F (c.duckFreq); dk.note = I (c.duckNote);
    dk.channel = I (c.duckChannel); dk.threshDb = F (c.duckThresh);
    p.duckTrack = I (c.duckTrack) - 1; p.duckTarget = I (c.duckTarget);
    p.bridgeOn = B (c.bridgeOn); p.bridgeTrack = I (c.bridgeTrack) - 1;

    if (p.trackRange[0] == nullptr)
    {
        for (int i = 0; i < T_COUNT; ++i) p.trackRange[i] = trackParams[0][i];
        for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d)
        {
            const auto id = ModDst::globalParamId (d);
            p.globalParam[d] = id.isNotEmpty() ? apvts.getParameter (id) : nullptr;
        }
    }
    for (int d = ModDst::GlobalStart; d < ModDst::Count; ++d) p.globalNorm[d] = p.globalParam[d] != nullptr ? p.globalParam[d]->getValue() : 0.0f;
}

// ===================================================================== audio
void DrumEngineProcessor::handleMidiLearn (juce::MidiBuffer& midi)
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

void DrumEngineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();

    // "All sound off" (CC 120) from the host stops Hamster's own Play: Racoon sends it when you close Hamster while it
    // plays on its own and answer "Sí, detenerlo". The parameter is put down by the timer. Only CC 120: the host sends
    // "all notes off" (CC 123) by itself with the transport stopped, and that one must never stop Hamster.
    for (const auto meta : midi)
        if (meta.getMessage().isAllSoundOff()) { stopPlayRequest = true; break; }
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    handleMidiLearn (midi);
    fillSnapshot (snap);
    if (stopPlayRequest.load()) snap.play = false;

    HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.valid = true;
            host.playing = pos->getIsPlaying();
            host.bpm = pos->getBpm().orFallback (120.0);
            host.beats = pos->getPpqPosition().orFallback (0.0);
        }

    // automation: with the DAW when it plays, with PLAY otherwise
    automation.process (midi, automation.clock (host.valid && host.playing, host.beats, snap.play,
                                                host.valid ? host.bpm : (double) snap.bpm, n), n);

    // sidechain copy before outputs overwrite shared channels
    juce::HeapBlock<float> scCopy;
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 0);
        if (sc.getNumChannels() > 0)
        {
            scCopy.allocate ((size_t) n * 2, false);
            std::copy (sc.getReadPointer (0), sc.getReadPointer (0) + n, scCopy.get());
            const float* r = sc.getReadPointer (juce::jmin (1, sc.getNumChannels() - 1));
            std::copy (r, r + n, scCopy.get() + n);
            scL = scCopy.get(); scR = scCopy.get() + n;
        }
    }
    buffer.clear();
    auto mainOut = getBusBuffer (buffer, false, 0);
    float* outL = mainOut.getWritePointer (0);
    float* outR = mainOut.getWritePointer (1);
    float* dL[kNumTracks] {}; float* dR[kNumTracks] {};
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto* bus = getBus (false, t + 1);
        if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
        {
            auto b = getBusBuffer (buffer, false, t + 1);
            dL[t] = b.getWritePointer (0); dR[t] = b.getWritePointer (1);
        }
    }

    const auto* seq = seqSlot.get();
    int done = 0;
    juce::MidiBuffer chunkMidi;
    while (done < n)
    {
        const int len = std::min (512, n - done);
        chunkMidi.clear();
        chunkMidi.addEvents (midi, done, len, -done);
        EngineOutputs o;
        o.mainL = outL + done; o.mainR = outR + done;
        for (int t = 0; t < kNumTracks; ++t) if (dL[t] != nullptr) { o.outL[t] = dL[t] + done; o.outR[t] = dR[t] + done; }
        o.scL = scL != nullptr ? scL + done : nullptr;
        o.scR = scR != nullptr ? scR + done : nullptr;
        HostInfo h = host;
        h.beats = host.beats + (double) done * host.bpm / 60.0 / getSampleRate();
        engine.process (snap, seq, h, chunkMidi, o, len);
        done += len;
    }
    midi.clear();

    // live kick hits for the bridge
    const int lk = engine.liveKickCount.load();
    if (lk != lastLiveKick) { lastLiveKick = lk; if (snap.bridgeOn) bridge.publishLiveKick (engine.liveKickVel); }

    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double avail = (double) n / std::max (1.0, getSampleRate());
    cpuLoad = 0.9f * cpuLoad.load() + 0.1f * (float) (elapsed / avail);
}

// ===================================================================== parameters helpers
void DrumEngineProcessor::setTrackValue (int t, int local, float real)
{
    if (auto* p = trackParams[t][local]) p->setValueNotifyingHost (p->convertTo0to1 (real));
}

float DrumEngineProcessor::getParamReal (const juce::String& id) const
{
    auto* a = apvts.getRawParameterValue (id);
    return a != nullptr ? a->load() : 0.0f;
}

void DrumEngineProcessor::setParamReal (const juce::String& id, float real)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (real));
}

// ===================================================================== patterns
SeqData DrumEngineProcessor::getSeqData() const { const juce::SpinLock::ScopedLockType l (seqLock); return msgSeq; }

namespace
{
// a change to the patterns, in the same undo history as the knobs: Deshacer, Rehacer and Cmd+Z undo whatever came last.
// The state after is read when it is undone, so a stroke (several edits after one undoable one) undoes as one
struct SeqAction : public juce::UndoableAction
{
    // only the pattern that changed is kept (a whole set of patterns is almost a megabyte); anything else keeps it all
    SeqAction (DrumEngineProcessor& p, const SeqData& b, const SeqData& a) : proc (p)
    {
        int diff = -1, count = 0;
        for (int i = 0; i < be::kNumPatterns; ++i)
            if (SeqData::patternToString (b.pats[i]) != SeqData::patternToString (a.pats[i])) { diff = i; ++count; }
        bool rest = b.songLength == a.songLength && b.groove.active == a.groove.active && b.groove.name == a.groove.name;
        for (int i = 0; rest && i < SeqData::kMaxSong; ++i) rest = b.song[i].pattern == a.song[i].pattern && b.song[i].repeats == a.song[i].repeats;
        for (int i = 0; rest && i < 16; ++i) rest = b.groove.timing[i] == a.groove.timing[i] && b.groove.velocity[i] == a.groove.velocity[i];
        if (count <= 1 && rest) { pat = diff < 0 ? 0 : diff; patBefore = b.pats[pat]; }
        else fullBefore = std::make_unique<SeqData> (b);
    }
    bool perform() override
    {
        if (done)
        {
            if (fullAfter != nullptr) proc.setSeqData (*fullAfter, false);
            else { auto d = proc.getSeqData(); d.pats[pat] = patAfter; proc.setSeqData (d, false); }
        }
        done = true;
        return true;
    }
    bool undo() override
    {
        auto d = proc.getSeqData();
        if (fullBefore != nullptr) { fullAfter = std::make_unique<SeqData> (d); proc.setSeqData (*fullBefore, false); }
        else { patAfter = d.pats[pat]; d.pats[pat] = patBefore; proc.setSeqData (d, false); }
        return true;
    }
    int getSizeInUnits() override { return fullBefore != nullptr ? 800 : 100; }
    DrumEngineProcessor& proc;
    int pat = 0;
    be::Pattern patBefore, patAfter;
    std::unique_ptr<SeqData> fullBefore, fullAfter;
    bool done = false;
};
}

void DrumEngineProcessor::setSeqData (const SeqData& d, bool undoable)
{
    if (undoable && juce::MessageManager::existsAndIsCurrentThread())
    {
        auto* a = new SeqAction (*this, getSeqData(), d);
        setSeqData (d, false);
        undo.beginNewTransaction();
        undo.perform (a);
        return;
    }
    {
        const juce::SpinLock::ScopedLockType l (seqLock);
        msgSeq = d;
    }
    seqSlot.set (std::make_shared<SeqData> (d));
    ++seqVersion;
    auto seqTree = extra.getOrCreateChildWithName ("SEQ", nullptr);
    seqTree.removeAllChildren (nullptr);
    seqTree.removeAllProperties (nullptr);
    seqTree.copyPropertiesAndChildrenFrom (d.toValueTree(), nullptr);
}

void DrumEngineProcessor::editSeq (const std::function<void (SeqData&)>& fn, bool undoable)
{
    auto d = getSeqData();
    fn (d);
    setSeqData (d, undoable);
}

bool DrumEngineProcessor::undoSeq() { return undo.undo(); }
bool DrumEngineProcessor::redoSeq() { return undo.redo(); }

juce::MidiFile DrumEngineProcessor::maquetaMidi()
{
    const int pat = juce::jlimit (0, kNumPatterns - 1, juce::roundToInt (getParamReal ("g_pattern")) - 1);
    auto d = getSeqData();
    const int bars = juce::jmax (1, (d.pats[pat].masterLength + 15) / 16);
    int notes[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) notes[t] = (int) trackValue (t, T_NOTE);
    auto mf = PatternTools::toMidiFile (d.pats[pat], notes, getParamReal ("g_bpm"), bars, getParamReal ("g_swing"));
    automation.addToMidiFile (mf, bars * 4.0);
    return mf;
}

void DrumEngineProcessor::loadFromSong (const racoon::maqueta::EditRequest& r)
{
    if (auto* pl = apvts.getParameter ("g_play")) pl->setValueNotifyingHost (0.0f);
    const int pat = juce::jlimit (0, kNumPatterns - 1, juce::roundToInt (getParamReal ("g_pattern")) - 1);
    const int numSteps = juce::jlimit (1, kMaxSteps, (int) std::ceil (juce::jmin (r.beats, kMaxSteps * 0.25) / 0.25 - 1.0e-6));
    int notes[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) notes[t] = (int) trackValue (t, T_NOTE);
    const auto hits = r.notes();
    setParamReal ("g_bpm", (float) r.bpm);
    editSeq ([&] (SeqData& d)
    {
        auto& P = d.pats[pat];
        P.masterLength = numSteps;
        for (auto& tp : P.tracks)
        {
            for (auto& st : tp.steps) st = Step();
            tp.length = numSteps;
            tp.res = 3;                                      // 1/16
        }
        for (auto& n : hits)
        {
            int t = -1;
            for (int k = 0; k < kNumTracks && t < 0; ++k) if (notes[k] == n.pitch) t = k;
            const int k = (int) std::round (n.start / 0.25);
            if (t < 0 || k >= numSteps) continue;
            auto& st = P.tracks[t].steps[k];
            st.on = true;
            st.vel = (uint8_t) juce::jlimit (1, 127, n.velocity);
            st.micro = (int8_t) juce::jlimit (-50, 50, juce::roundToInt ((n.start - k * 0.25) / 0.25 * 100.0));
            st.len = (uint8_t) juce::jlimit (1, 64, juce::roundToInt (n.length / 0.25 * 4.0));
        }
    }, true);
}

juce::File DrumEngineProcessor::exportPatternToMidi (int pattern, int bars)
{
    auto d = getSeqData();
    int notes[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) notes[t] = (int) trackValue (t, T_NOTE);
    auto mf = PatternTools::toMidiFile (d.pats[pattern], notes, getParamReal ("g_bpm"), bars, getParamReal ("g_swing"));
    automation.addToMidiFile (mf, bars * 4.0);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("DrumEngine_Pattern_" + juce::String::charToString ((juce::juce_wchar) ('A' + pattern)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { mf.writeTo (os); os.flush(); }
    return f;
}

juce::File DrumEngineProcessor::exportPatternToAudio (int pattern, int bars, int soloTrack)
{
    const double rate = 48000.0;
    auto eng = std::make_unique<DrumEngine>();
    eng->prepare (rate, 512);
    for (int a = 0; a < kNumAux; ++a)
        eng->aux[a].updateImpulse ((int) getParamReal (auxId (a, "type")), getParamReal (auxId (a, "p1")), getParamReal (auxId (a, "p2")));
    juce::Thread::sleep (250);    // let convolution loads settle
    auto sp = std::make_unique<EngineSnap>();
    fillSnapshot (*sp);     // private snapshot: never touch the audio thread's copy
    sp->pattern = pattern;
    sp->songMode = false;
    sp->play = false;
    sp->record = false;
    sp->scatter = 0;
    sp->brk = false;
    if (soloTrack >= 0)
        for (int t = 0; t < kNumTracks; ++t) { sp->base[t][T_SOLO] = t == soloTrack ? 1.0f : 0.0f; sp->base[t][T_MUTE] = t == soloTrack ? 0.0f : sp->base[t][T_MUTE]; }
    auto seq = std::make_shared<SeqData> (getSeqData());
    const double bpm = (double) getParamReal ("g_bpm");
    const double beats = bars * 4.0;
    const int total = (int) (beats * 60.0 / bpm * rate);
    const int tail = (int) (rate * 1.5);
    juce::AudioBuffer<float> out (2, total + tail);
    out.clear();
    juce::MidiBuffer none;
    // warm-up so convolution and smoothing settle (not recorded)
    {
        juce::AudioBuffer<float> tmp (2, 512);
        HostInfo h; h.valid = true; h.playing = false; h.bpm = bpm;
        for (int i = 0; i < 8; ++i)
        {
            tmp.clear();
            EngineOutputs o; o.mainL = tmp.getWritePointer (0); o.mainR = tmp.getWritePointer (1);
            eng->process (*sp, seq.get(), h, none, o, 512);
        }
    }
    for (int pos = 0; pos < total + tail; pos += 512)
    {
        const int len = std::min (512, total + tail - pos);
        HostInfo h;
        h.valid = true;
        h.playing = pos < total;
        h.bpm = bpm;
        h.beats = (double) pos / rate * bpm / 60.0;
        EngineOutputs o; o.mainL = out.getWritePointer (0) + pos; o.mainR = out.getWritePointer (1) + pos;
        eng->process (*sp, seq.get(), h, none, o, len);
    }
    auto name = "DrumEngine_" + juce::String::charToString ((juce::juce_wchar) ('A' + pattern))
                + (soloTrack >= 0 ? "_" + getTrackName (soloTrack).replaceCharacters (" /\\:", "____") : juce::String()) + ".wav";
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        }
    return f;
}

// ===================================================================== samples
bool DrumEngineProcessor::loadTrackSample (int t, int slot, const juce::File& file)
{
    auto s = loadSampleFile (file);
    if (s == nullptr) return false;
    setTrackSample (t, slot, s);
    return true;
}

void DrumEngineProcessor::setTrackSample (int t, int slot, SamplePtr s, const juce::String& libraryId)
{
    slots[t][slot].set (s);
    auto smp = extra.getOrCreateChildWithName ("SAMPLES", nullptr);
    const juce::String key = "t" + juce::String (t) + "s" + juce::String (slot);
    if (auto old = smp.getChildWithName (key); old.isValid()) smp.removeChild (old, nullptr);
    if (s == nullptr) return;
    juce::ValueTree v (key);
    v.setProperty ("name", s->name, nullptr);
    v.setProperty ("rate", s->sampleRate, nullptr);
    v.setProperty ("ch", s->stereo() ? 2 : 1, nullptr);
    // de la biblioteca solo se guarda el id: el proyecto no carga megas de audio que ya están en disco
    if (libraryId.isNotEmpty()) v.setProperty ("lib", libraryId, nullptr);
    else if (s->size() <= (int) (s->sampleRate * 20.0)) v.setProperty ("data", samplesToBase64 (*s), nullptr);
    smp.appendChild (v, nullptr);
    // the sample layer switches to user slots automatically
    setTrackValue (t, T_C_SAMPLE, (float) (factorySampleNames().size() - 1));
    setTrackValue (t, T_C_ON, 1.0f);
}

void DrumEngineProcessor::loadSliceKit (const juce::StringArray& files, const juce::StringArray& names, const juce::StringArray& roles, int firstNote)
{
    undo.beginNewTransaction ("Slices");
    for (int t = 0; t < kNumTracks; ++t)
    {
        SamplePtr smp = t < files.size() ? loadSampleFile (juce::File (files[t]), 4.0) : nullptr;
        for (int k = 0; k < kNumSlots; ++k) clearTrackSample (t, k);
        setTrackValue (t, T_CIRCUIT_ON, 0.0f);
        setTrackValue (t, T_A_ON, 0.0f);
        setTrackValue (t, T_B_ON, 0.0f);
        setTrackValue (t, T_D_ON, 0.0f);
        setTrackValue (t, T_NOTE, (float) juce::jlimit (0, 127, firstNote + t));
        if (smp == nullptr)
        {
            setTrackValue (t, T_C_ON, 0.0f);
            setTrackName (t, {});
            continue;
        }
        const auto role = roles[t] == "kick" ? Role::Kick : roles[t] == "hat" ? Role::ClosedHat : roles[t] == "snare" ? Role::Snare : Role::Custom;
        const float dur = juce::jlimit (0.03f, 4.0f, (float) smp->size() / (float) smp->sampleRate);
        setTrackSample (t, 0, smp);
        // like a sound of the library: the sample alone, played whole (the same voice libraryVoice builds)
        const std::pair<int, float> p[] {
            { T_ROLE, (float) role }, { T_C_ON, 1.0f }, { T_C_LEVEL, 1.0f }, { T_C_START, 0.0f }, { T_C_END, 1.0f }, { T_C_PITCH, 0.0f },
            { T_C_REVERSE, 0.0f }, { T_C_SLICEMODE, 0.0f }, { T_C_LOOP, 0.0f }, { T_C_FADEIN, 0.0f }, { T_C_MULTI, (float) MultiMode::Single },
            { T_ENV_MODE, (float) EnvMode::AHD }, { T_ENV_ATTACK, 0.0f }, { T_ENV_HOLD, juce::jmin (1.0f, dur * 0.7f) },
            { T_ENV_DECAY, juce::jlimit (0.12f, 8.0f, dur * 1.3f) }, { T_ENV_CURVE, 0.5f }, { T_PENV_AMT, 0.0f }, { T_FENV_AMT, 0.0f },
            { T_VELSENS, 0.6f }, { T_TUNE, 0.0f }, { T_FINE, 0.0f } };
        for (auto& [local, value] : p) setTrackValue (t, local, value);
        setTrackName (t, names[t].isNotEmpty() ? names[t] : "Golpe " + juce::String (t + 1));
    }
}

bool DrumEngineProcessor::takeSliceKit()
{
    const auto dir = racoon::maqueta::inbox().getChildFile ("Kits");
    if (! dir.isDirectory()) return false;
    auto notes = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    notes.sort();
    const auto me = trackName();
    for (auto& j : notes)
    {
        // nobody took it in two minutes: the channel is gone
        if (j.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::minutes (2)) { j.deleteFile(); continue; }
        const auto v = juce::JSON::parse (j.loadFileAsString());
        if (v["animal"].toString() != "Hamster") continue;
        // a kit addressed to a channel goes only to the Hamster on that channel; one without a channel name goes to the
        // first Hamster without a name that sees it fresh (a Hamster with no name used to take any fresh kit, also
        // the one Racoon left for another channel)
        const auto target = v["track"].toString();
        const bool fresh = j.getLastModificationTime() > juce::Time::getCurrentTime() - juce::RelativeTime::seconds (20);
        const bool mine = target.isNotEmpty() ? target == me : (me.isEmpty() && fresh);
        if (! mine) continue;
        juce::StringArray files, names, roles;
        if (auto* a = v["files"].getArray()) for (auto& x : *a) files.add (x.toString());
        if (auto* a = v["names"].getArray()) for (auto& x : *a) names.add (x.toString());
        if (auto* a = v["roles"].getArray()) for (auto& x : *a) roles.add (x.toString());
        j.deleteFile();
        loadSliceKit (files, names, roles, (int) v.getProperty ("firstNote", 36));
        return true;
    }
    return false;
}

void DrumEngineProcessor::clearTrackSample (int t, int slot)
{
    setTrackSample (t, slot, nullptr);
    if (slot == 0) extra.getOrCreateChildWithName ("SAMPLES", nullptr).removeProperty ("g" + juce::String (t), nullptr);
}

bool DrumEngineProcessor::loadLibraryGroup (int t, const juce::String& grupo)
{
    if (! juce::isPositiveAndBelow (t, kNumTracks)) return false;
    auto v = libraryVoice (grupo);
    if (! v.ok) return false;
    std::vector<SamplePtr> loaded;
    for (auto& id : v.slotIds) loaded.push_back (loadLibrarySample (id));
    if (loaded.empty() || loaded[0] == nullptr) return false;
    for (int k = 0; k < kNumSlots; ++k)
    {
        if (k < (int) loaded.size() && loaded[(size_t) k] != nullptr) setTrackSample (t, k, loaded[(size_t) k], v.slotIds[k]);
        else setTrackSample (t, k, nullptr);
    }
    for (auto& [local, value] : v.params) setTrackValue (t, local, value);
    setTrackName (t, v.name);
    extra.getOrCreateChildWithName ("SAMPLES", nullptr).setProperty ("g" + juce::String (t), grupo, nullptr);
    return true;
}

juce::String DrumEngineProcessor::trackLibraryGroup (int t) const
{
    return extra.getChildWithName ("SAMPLES").getProperty ("g" + juce::String (t)).toString();
}

bool DrumEngineProcessor::loadLibraryKit (const juce::String& id, bool keepPatterns)
{
    const auto* k = racoon::biblio::Catalogo::get().kit (id);
    if (k == nullptr) return false;
    bool any = false;
    for (auto& p : k->pistas) any = loadLibraryGroup (p.pista, p.grupo) || any;
    juce::ignoreUnused (keepPatterns);
    return any;
}

int DrumEngineProcessor::numLoadedSlots (int t) const
{
    int n = 0;
    for (auto& s : slots[t]) n += s.get() != nullptr ? 1 : 0;
    return n;
}

const SampleData* DrumEngineProcessor::displaySample (int t) const
{
    const int idx = (int) trackValue (t, T_C_SAMPLE);
    if (idx < factorySampleNames().size() - 1) return FactoryPcm::get().sample (idx);
    for (auto& s : slots[t]) if (s.get() != nullptr) return s.get();
    return nullptr;
}

// ===================================================================== shapes / fx order / names
std::vector<ShapePoint> DrumEngineProcessor::getShape (int index) const
{
    auto s = extra.getChildWithName ("SHAPES").getProperty ("s" + juce::String (index)).toString();
    return s.isNotEmpty() ? shapeFromString (s) : defaultLfoShape();
}

void DrumEngineProcessor::setShape (int index, const std::vector<ShapePoint>& pts)
{
    extra.getOrCreateChildWithName ("SHAPES", nullptr).setProperty ("s" + juce::String (index), shapeToString (pts), nullptr);
    shapeSlots[(size_t) index].set (ShapeLut::fromPoints (pts));
}

std::array<int, kNumFx> DrumEngineProcessor::getFxOrder() const
{
    std::array<int, kNumFx> o {};
    for (int k = 0; k < kNumFx; ++k) o[(size_t) k] = fxOrder[k].load();
    return o;
}

void DrumEngineProcessor::setFxOrder (const std::array<int, kNumFx>& order)
{
    juce::StringArray s;
    for (int k = 0; k < kNumFx; ++k) { fxOrder[k] = order[(size_t) k]; s.add (juce::String (order[(size_t) k])); }
    extra.setProperty ("fxOrder", s.joinIntoString (","), nullptr);
}

juce::String DrumEngineProcessor::getMacroName (int i) const
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("macroNames").toString(), "|", "");
    return i < names.size() && names[i].isNotEmpty() ? names[i] : "MACRO " + juce::String (i + 1);
}

void DrumEngineProcessor::setMacroName (int i, const juce::String& n)
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("macroNames").toString(), "|", "");
    while (names.size() < kNumMacros) names.add ({});
    names.set (i, n.replace ("|", "/"));
    extra.setProperty ("macroNames", names.joinIntoString ("|"), nullptr);
}

juce::String DrumEngineProcessor::getTrackName (int t) const
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("trackNames").toString(), "|", "");
    if (t < names.size() && names[t].isNotEmpty()) return names[t];
    return roleNames()[(int) trackValue (t, T_ROLE)];
}

void DrumEngineProcessor::setTrackName (int t, const juce::String& n)
{
    auto names = juce::StringArray::fromTokens (extra.getProperty ("trackNames").toString(), "|", "");
    while (names.size() < kNumTracks) names.add ({});
    names.set (t, n.replace ("|", "/"));
    extra.setProperty ("trackNames", names.joinIntoString ("|"), nullptr);
}

// ===================================================================== scenes
namespace
{
bool sceneParam (const juce::RangedAudioParameter* rp)
{
    const auto& id = rp->paramID;
    if (dynamic_cast<const juce::AudioParameterFloat*> (rp) == nullptr) return false;
    if (id.startsWith ("t"))
    {
        int local = -1;
        if (trackOfParamId (id, &local) >= 0) return trackParam (local).kind == 0;
        return false;
    }
    return id.startsWith ("aux") || id.startsWith ("grp") || id.startsWith ("mac") || id.startsWith ("glue_") || id == "g_master"
           || id == "g_swing" || id == "g_humanize" || id == "softclip" || id.endsWith ("_mix") || id == fxId ("autofilter", "cutoff");
}
}

void DrumEngineProcessor::captureScene (int scene)
{
    auto sc = extra.getOrCreateChildWithName ("SCENES", nullptr);
    juce::ValueTree v ("S" + juce::String (scene));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && sceneParam (rp))
            v.setProperty (rp->paramID, rp->getValue(), nullptr);
    if (auto old = sc.getChildWithName (v.getType()); old.isValid()) sc.removeChild (old, nullptr);
    sc.appendChild (v, nullptr);
    sceneApplyFade = -1.0f;
}

bool DrumEngineProcessor::sceneSet (int scene) const
{
    return extra.getChildWithName ("SCENES").getChildWithName ("S" + juce::String (scene)).isValid();
}

void DrumEngineProcessor::clearScene (int scene)
{
    auto sc = extra.getChildWithName ("SCENES");
    if (auto old = sc.getChildWithName ("S" + juce::String (scene)); old.isValid()) sc.removeChild (old, nullptr);
}

void DrumEngineProcessor::recallScene (int scene)
{
    auto v = extra.getChildWithName ("SCENES").getChildWithName ("S" + juce::String (scene));
    if (! v.isValid()) return;
    suppressMotion = true;
    for (int i = 0; i < v.getNumProperties(); ++i)
    {
        const auto id = v.getPropertyName (i);
        if (auto* p = apvts.getParameter (id.toString())) p->setValueNotifyingHost ((float) v.getProperty (id));
    }
    syncMotionBaseline();
    suppressMotion = false;
}

void DrumEngineProcessor::syncMotionBaseline()
{
    for (int t = 0; t < kNumTracks; ++t) for (int p = 0; p < T_COUNT; ++p) lastTrackNorm[t][p] = trackParams[t][p]->getValue();
}

void DrumEngineProcessor::applySceneFade()
{
    if (getParamReal ("g_sceneon") < 0.5f) { sceneApplyFade = -1.0f; return; }
    const int a = (int) getParamReal ("g_scenea") - 1, b = (int) getParamReal ("g_sceneb") - 1;
    float fade = getParamReal ("g_scenefade") + engine.telemetry.gmod[ModDst::SceneFade].load();
    fade = juce::jlimit (0.0f, 1.0f, fade);
    if (a == sceneApplyA && b == sceneApplyB && std::abs (fade - sceneApplyFade) < 0.002f) return;
    sceneApplyA = a; sceneApplyB = b; sceneApplyFade = fade;
    auto sc = extra.getChildWithName ("SCENES");
    auto A_ = sc.getChildWithName ("S" + juce::String (a)), B_ = sc.getChildWithName ("S" + juce::String (b));
    if (! A_.isValid() && ! B_.isValid()) return;
    suppressMotion = true;
    auto apply = [&] (const juce::ValueTree& src)
    {
        for (int i = 0; i < src.getNumProperties(); ++i)
        {
            const auto id = src.getPropertyName (i);
            auto* p = apvts.getParameter (id.toString());
            if (p == nullptr) continue;
            const float va = A_.isValid() ? (float) A_.getProperty (id, p->getValue()) : p->getValue();
            const float vb = B_.isValid() ? (float) B_.getProperty (id, p->getValue()) : p->getValue();
            if (std::abs (va - vb) < 1.0e-5f && &src == &B_) continue;
            const float v = va + (vb - va) * fade;
            if (std::abs (p->getValue() - v) > 1.0e-4f) p->setValueNotifyingHost (v);
        }
    };
    apply (A_.isValid() ? A_ : B_);
    if (A_.isValid() && B_.isValid()) apply (B_);
    syncMotionBaseline();
    suppressMotion = false;
}

// ===================================================================== tracks
void DrumEngineProcessor::applyRoleDefaults (int t, int role, int model)
{
    for (int i = 0; i < T_COUNT; ++i)
    {
        if (i == T_NOTE || i == T_OUTPUT) continue;
        auto* p = trackParams[t][i];
        p->setValueNotifyingHost (p->getDefaultValue());
    }
    roleDefaults (role, model, [this, t] (int local, float v) { setTrackValue (t, local, v); });
}

void DrumEngineProcessor::mapNotes (bool chromatic)
{
    undo.beginNewTransaction ("Map notes");
    std::set<int> used;
    static const int spare[] { 35, 40, 44, 47, 50, 52, 53, 54, 55, 57, 58, 59, 61, 62, 64, 65, 66, 67, 68, 69, 71, 72, 73, 74, 77, 78, 79, 80, 81 };
    for (int t = 0; t < kNumTracks; ++t)
    {
        int note = chromatic ? 36 + t : defaultNoteForRole ((int) trackValue (t, T_ROLE));
        if (! chromatic && used.count (note) > 0)
            for (int s : spare) if (used.count (s) == 0) { note = s; break; }
        used.insert (note);
        setTrackValue (t, T_NOTE, (float) note);
    }
}

void DrumEngineProcessor::resetTrack (int t, int role)
{
    applyRoleDefaults (t, role, 0);
    for (int k = 0; k < kNumSlots; ++k) clearTrackSample (t, k);
    setTrackName (t, {});
}

void DrumEngineProcessor::copyTrack (int from, int to)
{
    if (from == to) return;
    for (int i = 0; i < T_COUNT; ++i)
    {
        if (i == T_NOTE || i == T_OUTPUT) continue;
        trackParams[to][i]->setValueNotifyingHost (trackParams[from][i]->getValue());
    }
    auto smp = extra.getChildWithName ("SAMPLES");
    for (int k = 0; k < kNumSlots; ++k)
        setTrackSample (to, k, slots[from][k].getShared(), smp.getChildWithName ("t" + juce::String (from) + "s" + juce::String (k)).getProperty ("lib").toString());
    setTrackName (to, getTrackName (from));
}

float DrumEngineProcessor::kickFundamental (int t) const
{
    const int role = (int) trackValue (t, T_ROLE);
    const float tune = trackValue (t, T_TUNE) + trackValue (t, T_FINE) * 0.01f;
    float f = roleBaseHz (role);
    if (role == (int) Role::Kick) f = 34.0f * std::pow (92.0f / 34.0f, trackValue (t, T_A_P1));
    return f * std::exp2 (tune / 12.0f);
}

float DrumEngineProcessor::kickDecaySeconds (int t) const
{
    const float ds = std::exp2 ((trackValue (t, T_DECAY) - 0.5f) * 4.0f);
    static const float md[] { 1.3f, 0.85f, 0.6f, 0.55f, 1.0f };
    const float tau = 0.05f * std::pow (18.0f, trackValue (t, T_A_P6)) * md[juce::jlimit (0, 4, (int) trackValue (t, T_MODEL))] * ds;
    return std::min (tau * 3.0f, trackValue (t, T_ENV_DECAY));
}

// ===================================================================== MIDI learn
void DrumEngineProcessor::clearMidiLearn (const juce::String& paramId)
{
    for (int i = 0; i < paramList.size(); ++i)
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == paramId)
            for (auto& c : ccMap) if (c.load() == i) c = -1;
}

juce::String DrumEngineProcessor::midiLearnFor (const juce::String& paramId) const
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

// ===================================================================== A/B
void DrumEngineProcessor::storeAB (int slot) { abState[slot] = createPatchState(); }

void DrumEngineProcessor::recallAB (int slot)
{
    storeAB (abSlot);
    abSlot = slot;
    if (abState[slot].isValid()) loadPatchState (abState[slot]);
}

void DrumEngineProcessor::copyAtoB() { abState[0] = createPatchState(); abState[1] = abState[0].createCopy(); }

// ===================================================================== timer (message thread)
void DrumEngineProcessor::drainRecordFifo()
{
    const int ready = engine.recordFifo.getNumReady();
    if (ready <= 0) return;
    std::vector<RecordEvent> ev;
    auto rd = engine.recordFifo.read (ready);
    for (int i = 0; i < rd.blockSize1; ++i) ev.push_back (engine.recordBuf[rd.startIndex1 + i]);
    for (int i = 0; i < rd.blockSize2; ++i) ev.push_back (engine.recordBuf[rd.startIndex2 + i]);
    editSeq ([&ev] (SeqData& d)
    {
        for (auto& e : ev)
        {
            auto& st = d.pats[juce::jlimit (0, kNumPatterns - 1, e.pattern)].tracks[e.track].steps[juce::jlimit (0, kMaxSteps - 1, e.step)];
            st.on = true;
            st.vel = (uint8_t) juce::jlimit (1, 127, e.vel);
        }
    }, true);
}

void DrumEngineProcessor::motionRecord()
{
    const bool rec = getParamReal ("g_record") > 0.5f && engine.telemetry.playing.load();
    std::vector<std::tuple<int, int, float>> changes;
    for (int t = 0; t < kNumTracks; ++t)
        for (int p = 0; p < T_COUNT; ++p)
        {
            const float v = trackParams[t][p]->getValue();
            if (v != lastTrackNorm[t][p])
            {
                if (rec && ! suppressMotion && trackParam (p).lockable) changes.emplace_back (t, p, v);
                lastTrackNorm[t][p] = v;
            }
        }
    if (changes.empty()) return;
    const int pat = engine.telemetry.activePattern.load();
    editSeq ([&] (SeqData& d)
    {
        for (auto& [t, p, v] : changes)
        {
            auto& tp = d.pats[pat].tracks[t];
            const int step = engine.telemetry.trackStep[t].load();
            if (step < 0 || step >= tp.length) continue;
            auto& st = tp.steps[step];
            if (st.on) st.setLock (p, v);
        }
    }, false);
}

void DrumEngineProcessor::updateAuxImpulses()
{
    for (int a = 0; a < kNumAux; ++a)
        engine.aux[a].updateImpulse ((int) getParamReal (auxId (a, "type")), getParamReal (auxId (a, "p1")), getParamReal (auxId (a, "p2")));
}

void DrumEngineProcessor::publishBridge()
{
    const bool on = getParamReal ("bridge_on") > 0.5f;
    BridgeBassState bs;
    if (bridge.readBass (bs))
    {
        bridgeBassHz = bs.active ? bs.bassHz : 0.0f;
        bridgeBassSeen = true;
        engine.bassEnvExternal.store (bs.bassEnv);
    }
    else { bridgeBassSeen = false; bridgeBassHz = 0.0f; engine.bassEnvExternal.store (0.0f); }
    if (! on) return;
    const int bt = juce::jlimit (0, kNumTracks - 1, (int) getParamReal ("bridge_track") - 1);
    BridgeKickSchedule s;
    const auto* cur = seqSlot.get();
    if (cur == nullptr) return;
    const auto& tp = cur->pats[juce::jlimit (0, kNumPatterns - 1, engine.telemetry.activePattern.load())].tracks[bt];
    const double sb = seqResBeats (tp.res);
    const float swing = getParamReal ("g_swing");
    s.loopBeats = sb * std::max (1, tp.length);
    s.anchorBeat = engine.telemetry.patternSince.load();
    for (int k = 0; k < tp.length && s.numHits < 128; ++k)
    {
        const auto& st = tp.steps[k];
        if (! st.on || st.prob < 50 || (st.cond != 0 && st.cond != 2 && st.cond != 10)) continue;
        double b = k * sb + st.micro / 100.0 * sb;
        const double six = k * sb / 0.25;
        if (std::abs (six - std::round (six)) < 1.0e-6 && ((int) std::llround (six) & 1)) b += swing * 0.1667;
        s.hitBeat[s.numHits] = (float) b;
        s.hitVel[s.numHits] = st.vel / 127.0f;
        ++s.numHits;
    }
    s.kickDecay = kickDecaySeconds (bt);
    s.kickHz = kickFundamental (bt);
    s.bpm = getParamReal ("g_bpm");
    s.playing = engine.telemetry.playing.load() ? 1 : 0;
    if (trackValue (bt, T_MUTE) > 0.5f) s.numHits = 0;
    bridge.publishKick (s);
}

juce::int64 DrumEngineProcessor::kitSignature() const
{
    // what decides each drum's sound, except its tuning
    juce::int64 h = 0;
    for (int t = 0; t < kNumTracks; ++t)
        for (int k : { (int) T_ROLE, (int) T_MODEL, (int) T_C_SAMPLE, (int) T_A_P1, (int) T_A_P2, (int) T_B_ALGO, (int) T_D_FREQ, (int) T_PENV_AMT })
            h = h * 1000003 + (juce::int64) std::llround (trackValue (t, k) * 1000.0);
    return h;
}

std::vector<std::pair<float, float>> DrumEngineProcessor::measureTrackPeaks (int track, double loHz, double hiHz, double fromSeconds)
{
    std::vector<std::pair<float, float>> peaks;
    constexpr double rate = 48000.0;
    constexpr int block = 512, len = 32768;
    auto eng = std::make_unique<DrumEngine>();
    eng->prepare (rate, block);
    auto sp = std::make_unique<EngineSnap>();
    fillSnapshot (*sp);
    sp->play = false; sp->record = false; sp->songMode = false; sp->scatter = 0; sp->brk = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        sp->base[t][T_SOLO] = t == track ? 1.0f : 0.0f;
        sp->base[t][T_MUTE] = t == track ? 0.0f : 1.0f;
        sp->base[t][T_SEND1] = sp->base[t][T_SEND2] = sp->base[t][T_SEND3] = sp->base[t][T_SEND4] = 0.0f;
    }
    auto seq = std::make_shared<SeqData>();
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    juce::MidiBuffer none;
    HostInfo h; h.valid = true; h.playing = false; h.bpm = 120.0;
    eng->triggerPad (track, 0.9f);
    for (int pos = 0; pos < len; pos += block)
    {
        EngineOutputs o; o.mainL = out.getWritePointer (0) + pos; o.mainR = out.getWritePointer (1) + pos;
        eng->process (*sp, seq.get(), h, none, o, juce::jmin (block, len - pos));
    }
    eng->releasePad (track);
    // the body of the hit, just after the attack (a kick keeps falling for a while; from 60 to 230 ms is what the
    // ear takes as its note): its peaks that stand out of the noise, with how loud each one is
    constexpr int order = 13, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2, 0.0f);
    const int start = (int) (fromSeconds * rate);
    if (start + n > len) return peaks;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
        buf[(size_t) i] = 0.5f * (out.getSample (0, start + i) + out.getSample (1, start + i)) * w;
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double binHz = rate / n;
    const int lo = juce::jmax (2, (int) (loHz / binHz)), hi = (int) (hiHz / binHz);
    float best = 0.0f, sum = 0.0f;
    for (int k = lo; k <= hi; ++k) { sum += buf[(size_t) k]; best = juce::jmax (best, buf[(size_t) k]); }
    const float mean = sum / (float) (hi - lo + 1);
    // noise has no peak standing out (a narrow band, like a kick's, is judged less strictly: it is mostly peak)
    if (best < mean * (hi - lo < 64 ? 2.5f : 12.0f) || best < 1.0e-4f) return peaks;
    for (int k = lo + 1; k < hi; ++k)
    {
        const float y = buf[(size_t) k];
        if (y < best * 0.2f || y <= buf[(size_t) k - 1] || y < buf[(size_t) k + 1]) continue;
        const float a = std::log (buf[(size_t) k - 1] + 1.0e-9f), b = std::log (y + 1.0e-9f), c = std::log (buf[(size_t) k + 1] + 1.0e-9f);
        const float d = 0.5f * (a - c) / (a - 2.0f * b + c - 1.0e-9f);
        const double hz = ((double) k + juce::jlimit (-0.5f, 0.5f, d)) * binHz;
        peaks.push_back ({ (float) (69.0 + 12.0 * std::log2 (hz / 440.0)), y * y });
    }
    std::sort (peaks.begin(), peaks.end(), [] (auto& x, auto& y) { return x.second > y.second; });
    if (peaks.size() > 8) peaks.resize (8);
    return peaks;
}

float DrumEngineProcessor::measureTrackPitch (int track, double loHz, double hiHz, double fromSeconds)
{
    const auto p = measureTrackPeaks (track, loHz, hiHz, fromSeconds);
    return p.empty() ? -1.0f : p.front().first;
}

void DrumEngineProcessor::tuneKitToKey (int keyPc, bool major)
{
    static const int minorSt[] { 0, 2, 3, 5, 7, 8, 10 }, majorSt[] { 0, 2, 4, 5, 7, 9, 11 };
    auto inScale = [&] (int pc) { for (int k = 0; k < 7; ++k) if (pc == (major ? majorSt[k] : minorSt[k])) return true; return false; };
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto role = (Role) juce::roundToInt (trackValue (t, T_ROLE));
        const bool kick = role == Role::Kick;
        // where each drum sings: the kick low, the snare's body, the rim's knock, toms and hand drums in between
        double lo = 0.0, hi = 0.0;
        switch (role)
        {
            case Role::Kick:    lo = 30.0;  hi = 400.0;  break;
            case Role::Snare:   lo = 120.0; hi = 600.0;  break;
            case Role::Rim:     lo = 250.0; hi = 1400.0; break;
            case Role::TomLow: case Role::TomMid: case Role::TomHigh: lo = 50.0; hi = 700.0; break;
            case Role::Conga: case Role::Bongo: lo = 100.0; hi = 1000.0; break;
            case Role::Cowbell: lo = 300.0; hi = 1400.0; break;
            default: break;
        }
        if (hi <= 0.0) continue;
        // a kick falls in pitch and settles on its note: that tail is measured (later windows first; a short kick
        // that is gone by then is measured earlier)
        auto peaks = measureTrackPeaks (t, lo, hi, kick ? 0.25 : 0.02);
        if (kick && peaks.empty()) peaks = measureTrackPeaks (t, lo, hi, 0.12);
        if (kick && peaks.empty()) peaks = measureTrackPeaks (t, lo, hi, 0.06);
        if (peaks.empty()) continue;
        // the move (at most 6 semitones) that puts the most of the drum's sound on notes of the key; the main
        // peak always lands exactly on a note (a kick's on the root or the fifth)
        const float main = peaks.front().first;
        float bestShift = 0.0f, bestScore = -1.0f;
        for (int nn = (int) std::floor (main) - 6; nn <= (int) std::ceil (main) + 6; ++nn)
        {
            const int pc = ((nn - keyPc) % 12 + 12) % 12;
            if (kick ? (pc != 0 && pc != 7) : ! inScale (pc)) continue;
            const float shift = (float) nn - main;
            if (std::abs (shift) > 6.0f) continue;
            float score = 0.0f, total = 0.0f;
            for (auto& [m, w] : peaks)
            {
                const float moved = m + shift;
                const int r = (int) std::lround (moved);
                total += w;
                if (std::abs (moved - (float) r) < 0.3f && inScale (((r - keyPc) % 12 + 12) % 12)) score += w;
            }
            score = score / juce::jmax (1.0e-12f, total) - 0.02f * std::abs (shift);   // smaller moves win a tie
            if (score > bestScore) { bestScore = score; bestShift = shift; }
        }
        if (bestScore < 0.0f) continue;
        setTrackValue (t, T_TUNE, juce::jlimit (-24.0f, 24.0f, trackValue (t, T_TUNE) + bestShift));
    }
}

void DrumEngineProcessor::timerCallback()
{
    ++timerTick;
    // "Afinar el kit a": when the key, the scale or the kit change, the pitched drums are measured and tuned again
    if (timerTick % 5 == 0)
    {
        const int key = juce::roundToInt (getParamReal ("g_kitkey")), scale = juce::roundToInt (getParamReal ("g_kitscale"));
        const auto sig = kitSignature();
        // "Notas del kit: General MIDI": a kit that comes mapped 36, 37, 38... is moved to GM, so a clip's hi-hat
        // (42) plays the hi-hat and not whatever drum sits on 42
        const int noteMap = juce::roundToInt (getParamReal ("g_notemap"));
        if (noteMap == 1 && (noteMap != lastNoteMap || sig != lastMapSignature)) mapNotes (false);
        lastNoteMap = noteMap;
        lastMapSignature = sig;
        if (key > 0 && (key != lastKitKey || scale != lastKitScale || sig != lastKitSignature))
            tuneKitToKey (key - 1, scale == 1);
        lastKitKey = key; lastKitScale = scale; lastKitSignature = sig;
    }
    if (timerTick % 15 == 7) takeSliceKit();
    if (stopPlayRequest.load())
    {
        if (auto* pl = apvts.getParameter ("g_play")) pl->setValueNotifyingHost (0.0f);
        stopPlayRequest = false;
    }
    drainRecordFifo();
    motionRecord();
    applySceneFade();
    if (timerTick % 6 == 0) updateAuxImpulses();
    if (timerTick % 2 == 0) publishBridge();
    if (timerTick % 60 == 0)
    {
        seqSlot.collect();
        for (auto& tr : slots) for (auto& s : tr) s.collect();
    }
}

// ===================================================================== state
juce::ValueTree DrumEngineProcessor::createPatchState() const
{
    juce::ValueTree root ("DRUMENGINE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("presetName", currentPresetName, nullptr);
    root.appendChild (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    root.appendChild (extra.createCopy(), nullptr);
    return root;
}

void DrumEngineProcessor::applyExtraToAssets()
{
    auto shapes = extra.getChildWithName ("SHAPES");
    for (int i = 0; i < kNumShapes; ++i)
    {
        auto s = shapes.getProperty ("s" + juce::String (i)).toString();
        shapeSlots[(size_t) i].set (ShapeLut::fromPoints (s.isNotEmpty() ? shapeFromString (s) : defaultLfoShape()));
    }
    SeqData d;
    if (auto st = extra.getChildWithName ("SEQ"); st.isValid()) d.fromValueTree (st);
    setSeqData (d, false);
    // samples
    auto smp = extra.getChildWithName ("SAMPLES");
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < kNumSlots; ++k)
        {
            auto v = smp.getChildWithName ("t" + juce::String (t) + "s" + juce::String (k));
            SamplePtr s;
            if (v.isValid() && v.hasProperty ("lib"))
                s = loadLibrarySample (v.getProperty ("lib").toString());      // de la biblioteca, a calidad completa
            if (s == nullptr && v.isValid() && v.hasProperty ("data"))
                s = samplesFromBase64 (v.getProperty ("data").toString(), (int) v.getProperty ("ch", 1), (double) v.getProperty ("rate", 48000.0), v.getProperty ("name").toString());
            slots[t][k].set (s);
        }
    auto ord = juce::StringArray::fromTokens (extra.getProperty ("fxOrder").toString(), ",", "");
    std::array<int, kNumFx> order {};
    const bool valid = ord.size() == kNumFx;
    const int def[kNumFx] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 3 };
    for (int k = 0; k < kNumFx; ++k) order[(size_t) k] = valid ? ord[k].getIntValue() : def[k];
    setFxOrder (order);
    for (auto& c : ccMap) c = -1;
    for (auto& tok : juce::StringArray::fromTokens (extra.getProperty ("midiLearn").toString(), ";", ""))
    {
        const int cc = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const auto id = tok.fromFirstOccurrenceOf (":", false, false);
        for (int i = 0; i < paramList.size(); ++i)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (paramList[i]); rp != nullptr && rp->paramID == id && cc >= 0 && cc < 128) ccMap[cc] = i;
    }
    updateAuxImpulses();
}

void DrumEngineProcessor::loadPatchState (const juce::ValueTree& v, bool keepPatterns, bool keepKit)
{
    suppressMotion = true;
    auto params = v.getChildWithName (apvts.state.getType());
    if (params.isValid())
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto& id = rp->paramID;
                if (id == "g_quality" || id == "g_play" || id == "g_record") continue;
                if (id == "g_follow" && ! params.getChildWithProperty ("id", id).isValid()) continue;   // kits don't carry it
                const bool seqParam = id == "g_swing" || id == "g_pattern" || id == "g_songmode" || id == "g_fillevery" || id == "g_groove" || id == "g_humanize";
                if (keepKit && ! seqParam) continue;
                if (keepPatterns && seqParam) continue;
                auto child = params.getChildWithProperty ("id", id);
                const float val = child.isValid() ? rp->convertTo0to1 ((float) child.getProperty ("value")) : rp->getDefaultValue();
                rp->setValueNotifyingHost (val);
            }
    }
    auto ex = v.getChildWithName ("EXTRA");
    if (ex.isValid())
    {
        auto seqKeep = extra.getChildWithName ("SEQ").createCopy();
        auto keepCopy = extra.createCopy();
        if (keepKit)
        {
            if (auto s = extra.getChildWithName ("SEQ"); s.isValid()) extra.removeChild (s, nullptr);
            if (auto s = ex.getChildWithName ("SEQ"); s.isValid()) extra.appendChild (s.createCopy(), nullptr);
        }
        else
        {
            extra.copyPropertiesAndChildrenFrom (ex, nullptr);
            if (keepPatterns && seqKeep.isValid())
            {
                if (auto s = extra.getChildWithName ("SEQ"); s.isValid()) extra.removeChild (s, nullptr);
                extra.appendChild (seqKeep, nullptr);
            }
        }
    }
    if (! keepKit) currentPresetName = v.getProperty ("presetName", "Init Kit").toString();
    applyExtraToAssets();
    syncMotionBaseline();
    sceneApplyFade = -1.0f;
    suppressMotion = false;
    if (onPresetLoaded) onPresetLoaded();
}

void DrumEngineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
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

void DrumEngineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto v = juce::ValueTree::fromXml (*xml);
        if (v.hasType ("DRUMENGINE"))
        {
            currentProgram = (int) v.getProperty ("program", 0);
            loadPatchState (v);
            automation.fromTree (v.getChildWithName (racoon::Automation::treeType));
        }
    }
}

int DrumEngineProcessor::getNumPrograms() { return std::max (1, presetManager->numFactory()); }
int DrumEngineProcessor::getCurrentProgram() { return currentProgram; }
void DrumEngineProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetManager->numFactory() || index == currentProgram) return;
    currentProgram = index;
    presetManager->loadFactory (index);
}
const juce::String DrumEngineProcessor::getProgramName (int index) { return presetManager->factoryName (index); }

juce::AudioProcessorEditor* DrumEngineProcessor::createEditor() { return new DrumEngineEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumEngineProcessor(); }

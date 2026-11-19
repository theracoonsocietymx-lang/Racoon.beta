#include "Features.h"
#include "../PluginProcessor.h"
#include "../Params.h"
#include "../dsp/Wavetables.h"

namespace be
{
// ====================================================================== Bass DNA
namespace BassDNA
{
DnaResult analyse (const juce::AudioBuffer<float>& audio, double sr)
{
    DnaResult r;
    const int total = audio.getNumSamples();
    if (total < (int) (sr * 0.05)) { r.summary = "Sample too short"; return r; }
    std::vector<float> x ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c) s += audio.getSample (c, i);
        x[(size_t) i] = s / (float) std::max (1, audio.getNumChannels());
    }
    // trim leading silence
    float peakAll = 1.0e-9f;
    for (auto v : x) peakAll = std::max (peakAll, std::abs (v));
    int start = 0;
    while (start < total - 1 && std::abs (x[(size_t) start]) < peakAll * 0.003f) ++start;
    const int n = std::min (total - start, (int) (sr * 3.0));
    const float* s = x.data() + start;
    r.duration = (float) n / (float) sr;

    // amplitude envelope (5 ms RMS)
    const int win = std::max (16, (int) (sr * 0.005));
    std::vector<float> env;
    for (int i = 0; i + win <= n; i += win)
    {
        double acc = 0.0;
        for (int k = 0; k < win; ++k) acc += (double) s[i + k] * s[i + k];
        env.push_back ((float) std::sqrt (acc / win));
    }
    if (env.size() < 4) { r.summary = "Sample too short"; return r; }
    const auto peakIt = std::max_element (env.begin(), env.end());
    const float pk = *peakIt;
    const int pkIdx = (int) (peakIt - env.begin());
    int i10 = 0, i90 = pkIdx;
    for (int i = 0; i <= pkIdx; ++i) if (env[(size_t) i] >= pk * 0.1f) { i10 = i; break; }
    for (int i = 0; i <= pkIdx; ++i) if (env[(size_t) i] >= pk * 0.9f) { i90 = i; break; }
    r.attack = std::max (0.001f, (float) (i90 - i10) * (float) win / (float) sr);
    // sustain: median of the 40..80% region
    const size_t a = (size_t) (env.size() * 0.4), b = std::max (a + 1, (size_t) (env.size() * 0.8));
    std::vector<float> mid (env.begin() + (long) a, env.begin() + (long) b);
    std::nth_element (mid.begin(), mid.begin() + (long) mid.size() / 2, mid.end());
    r.sustain = juce::jlimit (0.0f, 1.0f, mid[mid.size() / 2] / pk);
    // decay: time from peak until within 10% of the sustain level
    int dIdx = pkIdx;
    const float target = pk * (r.sustain + (1.0f - r.sustain) * 0.1f);
    while (dIdx < (int) env.size() - 1 && env[(size_t) dIdx] > target) ++dIdx;
    r.decay = std::max (0.02f, (float) (dIdx - pkIdx) * (float) win / (float) sr);
    // release: tail fall time at the end (from last 20% to -40 dB)
    int tailStart = (int) env.size() - 1;
    while (tailStart > pkIdx && env[(size_t) tailStart] < pk * 0.01f) --tailStart;
    int relIdx = tailStart;
    const float relRef = env[(size_t) tailStart];
    while (relIdx > pkIdx && env[(size_t) relIdx] < relRef * 3.0f) --relIdx;
    r.release = juce::jlimit (0.02f, 2.0f, (float) (tailStart - relIdx) * (float) win / (float) sr);
    if (r.sustain < 0.05f) r.release = std::min (r.release, r.decay * 0.5f + 0.05f);

    // fundamental
    const int pitchStart = std::min (n - 1, (int) (sr * 0.03) + i90 * win);
    const int pitchLen = std::min (n - pitchStart, (int) (sr * 0.35));
    r.f0 = detectPitchHz (s + pitchStart, pitchLen, sr, 25.0f, 1000.0f);
    if (r.f0 <= 0.0f) { r.summary = "No stable pitch found"; return r; }
    r.midiNote = (float) (69.0 + 12.0 * std::log2 (r.f0 / 440.0));

    // harmonic frames (Goertzel at h*f0)
    const int frames = 8, H = 64;
    const int analysisLen = std::max ((int) (sr / r.f0 * 6.0), 2048);
    const int usable = std::max (1, n - analysisLen);
    float centroidFirst = 0, centroidLast = 0, energyH1 = 0, energyAll = 0;
    r.frames.assign (frames, std::vector<float> (H, 0.0f));
    for (int f = 0; f < frames; ++f)
    {
        const int off = (int) ((float) usable * (float) f / (float) (frames - 1) * 0.85f);
        float mx = 1.0e-9f, cen = 0.0f, tot = 0.0f;
        for (int h = 1; h <= H; ++h)
        {
            const double fh = r.f0 * h;
            if (fh >= sr * 0.45) break;
            const double w = 2.0 * juce::MathConstants<double>::pi * fh / sr;
            double s1 = 0, s2 = 0;
            const double coeff = 2.0 * std::cos (w);
            for (int k = 0; k < analysisLen && off + k < n; ++k)
            {
                const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * k / (analysisLen - 1));
                const double s0 = s[off + k] * hann + coeff * s1 - s2;
                s2 = s1; s1 = s0;
            }
            const float mag = (float) std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
            r.frames[(size_t) f][(size_t) h - 1] = mag;
            mx = std::max (mx, mag);
            cen += mag * (float) h; tot += mag;
            if (f == 1) { energyAll += mag * mag; if (h == 1) energyH1 += mag * mag; }
        }
        for (auto& v : r.frames[(size_t) f]) v /= mx;
        const float c = tot > 0 ? cen / tot : 1.0f;
        if (f == 0) centroidFirst = c;
        if (f == frames - 1) centroidLast = c;
        if (f == 1) r.brightness = c;
    }
    r.brightnessDrop = juce::jlimit (0.0f, 1.0f, (centroidFirst - centroidLast) / std::max (1.0f, centroidFirst));
    r.fundamentalRatio = energyAll > 0 ? energyH1 / energyAll : 0.0f;

    // noisiness: energy between harmonics relative to total (one frame)
    {
        const int off = (int) (usable * 0.15f);
        const int N = 8192;
        juce::dsp::FFT fft (13);
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int k = 0; k < N && off + k < n; ++k) buf[(size_t) k] = s[off + k] * (0.5f - 0.5f * std::cos (kTwoPi * (float) k / (float) (N - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const float binHz = (float) sr / (float) N;
        double harm = 0, all = 0;
        for (int k = 2; k < N / 2; ++k)
        {
            const float hz = (float) k * binHz;
            if (hz > 8000.0f) break;
            const double e = (double) buf[(size_t) k] * buf[(size_t) k];
            all += e;
            const float rel = hz / r.f0;
            const float dist = std::abs (hz - std::round (rel) * r.f0);
            if (std::round (rel) >= 1.0f && dist < std::max (0.08f * r.f0, 3.0f * binHz)) harm += e;
        }
        r.noisiness = all > 0 ? juce::jlimit (0.0f, 1.0f, (float) (1.0 - harm / all)) : 0.0f;
    }
    r.ok = true;
    const auto noteName = juce::MidiMessage::getMidiNoteName ((int) std::round (r.midiNote), true, true, 3);
    r.summary = "Root " + noteName + " (" + juce::String (r.f0, 1) + " Hz)  /  attack " + juce::String (r.attack * 1000.0f, 0) + " ms  /  decay "
              + juce::String (r.decay * 1000.0f, 0) + " ms  /  sustain " + juce::String (juce::roundToInt (r.sustain * 100.0f)) + "%\n"
              + "Brightness " + juce::String (r.brightness, 1) + " harmonics  /  fundamental share " + juce::String (juce::roundToInt (r.fundamentalRatio * 100.0f))
              + "%  /  noise " + juce::String (juce::roundToInt (r.noisiness * 100.0f)) + "%";
    return r;
}

DnaResult analyseFile (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) { DnaResult r; r.summary = "Unsupported file"; return r; }
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 4.0));
    juce::AudioBuffer<float> buf ((int) rd->numChannels, len);
    rd->read (&buf, 0, len, 0, true, true);
    return analyse (buf, rd->sampleRate);
}

void apply (BassEngineProcessor& proc, const DnaResult& r, const juce::String& name)
{
    if (! r.ok) return;
    proc.undo.beginNewTransaction ("Bass DNA");
    auto set = [&proc] (const juce::String& id, float real)
    {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (real));
    };
    // oscillator 1: evolving wavetable built from the measured harmonics
    proc.setUserTable (0, WaveTable::fromSpectra (r.frames), "DNA " + name);
    set ("o1_on", 1); set ("o1_engine", (float) Engine::Wavetable); set ("o1_layer", 1); set ("o1_pos", 0.0f);
    set ("o1_pa", 0.5f); set ("o1_pb", 0.0f); set ("o1_pc", 0.0f); set ("o1_unison", 1); set ("o1_level", 0.8f);
    set ("o1_oct", 0); set ("o1_semi", 0); set ("o1_fine", 0); set ("o1_coarse", 0);
    // amp envelope
    set ("amp_env", 0);
    set ("e1_delay", 0); set ("e1_attack", r.attack); set ("e1_hold", 0); set ("e1_decay", r.decay); set ("e1_sustain", r.sustain); set ("e1_release", r.release);
    // timbre scan over the sound's length with env 8
    const float scanTime = juce::jlimit (0.05f, 4.0f, r.duration * 0.85f);
    set ("e8_attack", 0.001f); set ("e8_decay", scanTime); set ("e8_sustain", 0.0f); set ("e8_release", r.release); set ("e8_dcurve", 0.0f);
    // routing: Env 8 -> Osc 1 Position (inverted so the start of the note reads frame 0)
    int slot = -1;
    for (int i = 0; i < kNumRoutes; ++i)
    {
        auto* src = proc.apvts.getRawParameterValue (routeId (i, "src"));
        auto* dst = proc.apvts.getRawParameterValue (routeId (i, "dst"));
        if ((int) src->load() == ModSrc::Env1 + 7 && (int) dst->load() == ModDst::Osc1 + ModDst::OPos) { slot = i; break; }
        if (slot < 0 && (int) src->load() == 0) slot = i;
    }
    if (slot >= 0)
    {
        set (routeId (slot, "on"), 1); set (routeId (slot, "src"), (float) (ModSrc::Env1 + 7)); set (routeId (slot, "via"), 0);
        set (routeId (slot, "dst"), (float) (ModDst::Osc1 + ModDst::OPos)); set (routeId (slot, "amt"), 1.0f);
        set (routeId (slot, "op"), (float) ModOp::Invert); set (routeId (slot, "curve"), 0);
    }
    // filter: open enough for the measured brightness, envelope from the brightness drop
    set ("f1_on", 1); set ("f1_type", (float) FilterType::LP24);
    set ("f1_cutoff", juce::jlimit (80.0f, 18000.0f, r.f0 * std::max (2.0f, r.brightness * 3.0f)));
    set ("f1_res", 0.1f); set ("f1_envamt", r.brightnessDrop * 0.4f); set ("f1_keytrack", 1.0f);
    // sub: support thin fundamentals
    set ("sub_on", r.fundamentalRatio < 0.5f ? 1.0f : 0.0f);
    set ("sub_level", juce::jlimit (0.2f, 0.7f, 0.7f - r.fundamentalRatio * 0.6f));
    // noise layer for breathy / noisy sources
    set ("noise_on", r.noisiness > 0.25f ? 1.0f : 0.0f);
    set ("noise_level", juce::jlimit (0.0f, 0.5f, r.noisiness * 0.5f));
    set ("noise_attack", r.attack < 0.01f ? 0.6f : 0.0f);
    proc.extra.setProperty ("dnaSummary", r.summary, nullptr);
    proc.currentPresetName = "DNA " + name;
}
} // namespace BassDNA

// ====================================================================== Randomizer
namespace Randomizer
{
static int groupOf (const juce::String& id)
{
    // 0 osc, 1 sub, 2 filter, 3 env, 4 lfo, 5 fx, 6 matrix, -1 never
    if (id.startsWith ("mac") || id.startsWith ("v_") || id.startsWith ("seq_") || id.startsWith ("xy_") || id == "master_volume"
        || id.startsWith ("evo_") || id.startsWith ("imp_") || id == "amp_env") return -1;
    if (id.startsWith ("o") && juce::CharacterFunctions::isDigit (id[1])) return 0;
    if (id.startsWith ("ly") || id.startsWith ("layer_")) return 0;
    if (id.startsWith ("sub_") || id.startsWith ("noise_")) return 1;
    if ((id.startsWith ("f") && juce::CharacterFunctions::isDigit (id[1])) || id == "filter_routing" || id.startsWith ("acid_")) return 2;
    if (id.startsWith ("e") && juce::CharacterFunctions::isDigit (id[1])) return 3;
    if (id.startsWith ("amp_")) return 3;
    if (id.startsWith ("l") && juce::CharacterFunctions::isDigit (id[1])) return 4;
    if ((id.startsWith ("d") && juce::CharacterFunctions::isDigit (id[1])) || id.startsWith ("fx_") || id.startsWith ("dist_") || id.startsWith ("duck_")) return 5;
    if (id.startsWith ("m") && juce::CharacterFunctions::isDigit (id[1])) return 6;
    return -1;
}

void randomize (BassEngineProcessor& proc, float amount, const RandomLocks& L, juce::uint32 seed)
{
    proc.undo.beginNewTransaction ("Randomize");
    juce::Random rnd ((juce::int64) seed);
    const bool locked[7] { L.osc, L.sub, L.filter, L.env, L.lfo, L.fx, L.matrix };
    auto isFund = [] (const juce::String& id)
    {
        return id.contains ("_oct") || id.contains ("_semi") || id.contains ("_fine") || id.contains ("coarse") || id.contains ("keytrack")
            || id.contains ("_tune") || id.endsWith ("_layer") || id.contains ("sync") || id == "sub_lock" || id == "sub_mono";
    };
    auto isVol = [] (const juce::String& id)
    {
        return id.contains ("level") || id.contains ("volume") || id.endsWith ("_out") || id.contains ("makeup") || id.contains ("ceiling");
    };
    auto isChar = [] (const juce::String& id)
    {
        return id.endsWith ("_engine") || id.endsWith ("_type") || id.endsWith ("_wave") || id.endsWith ("_table") || id.endsWith ("_sample")
            || id.endsWith ("drivetype") || id.endsWith ("spmode") || id.endsWith ("xmodtype") || id == "filter_routing";
    };
    for (auto* p : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        const auto& id = rp->paramID;
        const int g = groupOf (id);
        if (g < 0 || locked[g] || g == 6) continue;
        if (L.fundamental && isFund (id)) continue;
        if (L.volume && isVol (id)) continue;
        if (L.character && isChar (id)) continue;
        if (id == "o1_on" || id.endsWith ("_path") || id.endsWith ("_mute") || id.endsWith ("_solo") || id.contains ("key") || id.contains ("vel")
            || id == "fx_limiter_on" || id == "duck_note" || id == "duck_channel" || id.endsWith ("_invert") || id.endsWith ("_delay")) continue;
        const bool discrete = dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr || dynamic_cast<juce::AudioParameterInt*> (rp) != nullptr;
        const bool isBool = dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr;
        if (isBool)
        {
            if (rnd.nextFloat() < amount * 0.25f) rp->setValueNotifyingHost (rp->getValue() > 0.5f ? 0.0f : 1.0f);
        }
        else if (discrete)
        {
            if (rnd.nextFloat() < amount * 0.4f) rp->setValueNotifyingHost (rnd.nextFloat());
        }
        else
        {
            float v = juce::jmap (amount, rp->getValue(), rnd.nextFloat());
            // keep envelopes and levels musical
            if (id.endsWith ("_attack") && id.startsWith ("e1")) v = std::min (v, rp->convertTo0to1 (0.03f));
            if (id.endsWith ("_release") && id.startsWith ("e1")) v = std::min (v, rp->convertTo0to1 (0.4f));
            if (id.endsWith ("_delay") && id.startsWith ("e")) v = std::min (v, rp->convertTo0to1 (0.05f));
            if (id.contains ("fx_") && id.endsWith ("_mix")) v = std::min (v, 0.45f);
            rp->setValueNotifyingHost (v);
        }
    }
    // matrix: add a few musical routes in free slots
    if (! L.matrix)
    {
        const int extras = 1 + (int) (amount * 4.0f);
        const int srcPool[] { ModSrc::Env1 + 1, ModSrc::Env1 + 2, ModSrc::Lfo1, ModSrc::Lfo1 + 1, ModSrc::Velocity, ModSrc::Random, ModSrc::ModWheel };
        const int dstPool[] { ModDst::Osc1 + ModDst::OPos, ModDst::Osc1 + ModDst::OParamA, ModDst::Filter1, ModDst::Filter1 + 1, ModDst::Osc1 + ModDst::OFold,
                              ModDst::Filter1 + 2, ModDst::LayerDrive1 + 2, ModDst::Osc1 + ModDst::OShape };
        int added = 0;
        for (int i = 8; i < kNumRoutes && added < extras; ++i)
        {
            if ((int) proc.apvts.getRawParameterValue (routeId (i, "src"))->load() != 0) continue;
            auto set = [&proc] (const juce::String& id, float real) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (real)); };
            set (routeId (i, "on"), 1);
            set (routeId (i, "src"), (float) srcPool[rnd.nextInt ((int) std::size (srcPool))]);
            set (routeId (i, "dst"), (float) dstPool[rnd.nextInt ((int) std::size (dstPool))]);
            set (routeId (i, "amt"), (rnd.nextFloat() * 2.0f - 1.0f) * 0.4f * amount);
            ++added;
        }
    }
    // ensure at least one sound source
    if (proc.apvts.getRawParameterValue ("o1_on")->load() < 0.5f && proc.apvts.getRawParameterValue ("sub_on")->load() < 0.5f)
        if (auto* p = proc.apvts.getParameter ("o1_on")) p->setValueNotifyingHost (1.0f);
    proc.currentPresetName = "Random " + juce::String::toHexString ((int) seed).substring (0, 4).toUpperCase();
}
} // namespace Randomizer

// ====================================================================== Bass Focus
namespace BassFocus
{
FocusReport diagnose (const std::vector<float>& spec, float binHz, float lowCorr, float stereoCorr, float subStability, float peak, float rms, float leadNote)
{
    FocusReport rep;
    auto band = [&] (float lo, float hi)
    {
        double e = 0.0;
        for (size_t k = 1; k < spec.size(); ++k)
        {
            const float hz = (float) k * binHz;
            if (hz >= lo && hz < hi) e += (double) spec[k] * spec[k];
        }
        return e;
    };
    const double sub = band (20, 60), low = band (60, 120), mud = band (150, 400), def = band (700, 3000), total = band (20, 16000) + 1.0e-12;
    if (total < 1.0e-8 || peak < 1.0e-4f)
    {
        rep.items.push_back ({ 0, "Play a note to analyse the low end." });
        return rep;
    }
    const double subShare = sub / total, mudShare = mud / total, defShare = def / total, lowShare = (sub + low) / total;
    if (subShare > 0.55) rep.items.push_back ({ 2, "Too much energy below 60 Hz (" + juce::String (juce::roundToInt (subShare * 100)) + "%). Lower SUB level or raise the sub ceiling filter." });
    else if (lowShare < 0.15 && leadNote > 0 && leadNote < 52) rep.items.push_back ({ 1, "Thin low end for this register. Add SUB or lower the BODY low cut." });
    else rep.items.push_back ({ 0, "Sub energy in range (" + juce::String (juce::roundToInt (subShare * 100)) + "% below 60 Hz)." });
    if (mudShare > 0.35) rep.items.push_back ({ 2, "Low-mid build-up 150-400 Hz (" + juce::String (juce::roundToInt (mudShare * 100)) + "%). Try a cut with the EQ peak or less filter resonance." });
    if (defShare < 0.008) rep.items.push_back ({ 1, "Little content 700 Hz - 3 kHz: the bass may disappear on small speakers. Add BITE or drive." });
    if (lowCorr < -0.2f) rep.items.push_back ({ 2, "Phase cancellation between SUB and the other layers (" + juce::String (lowCorr, 2) + "). Flip a layer phase or enable Sub Phase Align." });
    else if (lowCorr < 0.3f) rep.items.push_back ({ 1, "SUB and upper layers are weakly correlated in the low band (" + juce::String (lowCorr, 2) + ")." });
    if (stereoCorr < 0.2f) rep.items.push_back ({ 2, "Wide stereo image (" + juce::String (stereoCorr, 2) + "): low end may collapse in mono. Enable Bass Mono in the Imager." });
    if (subStability < 0.7f) rep.items.push_back ({ 1, "Unstable sub level between notes. Check Sub Lock and the sub envelope." });
    const float crest = rms > 0 ? peak / rms : 0.0f;
    if (crest > 0 && crest < 1.6f) rep.items.push_back ({ 1, "Very low crest factor (" + juce::String (crest, 1) + "): heavy limiting or saturation, transients flattened." });
    if (rep.items.size() == 1 && rep.items[0].severity == 0) rep.items.push_back ({ 0, "No problems detected." });
    return rep;
}
} // namespace BassFocus
} // namespace be

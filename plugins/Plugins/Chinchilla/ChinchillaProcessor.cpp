#include "ChinchillaProcessor.h"
#include "ChinchillaEditor.h"

namespace racoon::chinchilla
{
using namespace juce;

const StringArray& modSources()
{
    static StringArray s { "Nada", "LFO 1", "LFO 2", "Volumen de la voz", "Afinación"_u, "Velocidad MIDI", "Rueda (CC1)" };
    return s;
}
const StringArray& modDestinations()
{
    static StringArray s { "Nada", "Formantes", "Armonías"_u, "Vocoder", "Chopper", "Delay", "Reverb", "Filtro", "Afinación (cents)"_u, "Robot", "Ancho del doble" };
    return s;
}
enum Dest { dNone = 0, dFormant, dHarm, dVocoder, dChop, dDelay, dReverb, dFilter, dTune, dRobot, dWidth };

static const char* hKeys[] { "on", "mode", "int", "lvl", "pan", "form", "delay", "det" };

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    // tuning
    P::toggle (l, "tune_on", "Afinar", true);
    P::choice (l, "key", "Tonalidad", keyNames(), 0);
    P::choice (l, "scale", "Escala", scaleNames(), 0);
    P::choice (l, "target", "Afinar hacia", { "La escala", "Las notas MIDI", "El editor de notas" }, 0);
    P::ms (l, "retune", "Velocidad de afinación"_u, 0.0f, 400.0f, 40.0f, 60.0f);
    P::pct (l, "humanize", "Humanizar", 0.0f, 100.0f, 30.0f);
    P::pct (l, "amount", "Cantidad", 0.0f, 100.0f, 100.0f);
    P::integer (l, "transpose", "Transponer", -12, 12, 0);
    P::flt (l, "formant", "Formantes", P::range (-6.0f, 6.0f), 0.0f, "st", 1);
    // cleanup
    P::hz (l, "cl_hp", "Corte de graves", 20.0f, 300.0f, 80.0f);
    P::db (l, "cl_gate", "Puerta", -90.0f, -20.0f, -70.0f);
    P::pct (l, "cl_deess", "De-esser", 0.0f, 100.0f, 0.0f);
    P::pct (l, "cl_comp", "Compresión"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "cl_breath", "Bajar respiraciones", 0.0f, 100.0f, 0.0f);
    // harmonies
    P::toggle (l, "h_on", "Armonías"_u, false);
    P::pct (l, "h_mix", "Armonías: volumen"_u, 0.0f, 100.0f, 60.0f);
    P::pct (l, "tm_tight", "Armonías: juntas"_u, 0.0f, 100.0f, 70.0f);
    const int ints[numHarmonies] { 2, -2, 4, 7, -3, 12, -12, 5 };
    const float pans[numHarmonies] { -40, 40, -70, 70, -20, 20, -90, 90 };
    for (int v = 0; v < numHarmonies; ++v)
    {
        const String n = "Voz " + String (v + 1) + ": ";
        P::toggle (l, hId (v, "on"), n + "encendida", v < 2);
        P::choice (l, hId (v, "mode"), n + "modo", { "Intervalo fijo", "Grados de la escala", "Nota MIDI" }, 1);
        P::integer (l, hId (v, "int"), n + "intervalo", -24, 24, ints[v]);
        P::db (l, hId (v, "lvl"), n + "volumen", -30.0f, 6.0f, -3.0f);
        P::pct (l, hId (v, "pan"), n + "lugar", -100.0f, 100.0f, pans[v]);
        P::flt (l, hId (v, "form"), n + "formantes", P::range (-6.0f, 6.0f), 0.0f, "st", 1);
        P::ms (l, hId (v, "delay"), n + "retraso", 0.0f, 60.0f, 8.0f + 4.0f * v);
        P::flt (l, hId (v, "det"), n + "desafinar", P::range (-30.0f, 30.0f), (v % 2 ? -6.0f : 6.0f), "ct", 0);
    }
    // doubles
    P::toggle (l, "dbl_on", "Doble", false);
    P::integer (l, "dbl_voices", "Doble: voces", 1, numDoubles, 2);
    P::pct (l, "dbl_width", "Doble: ancho", 0.0f, 100.0f, 80.0f);
    P::ms (l, "dbl_spread", "Doble: separación"_u, 5.0f, 40.0f, 18.0f);
    P::flt (l, "dbl_detune", "Doble: desafinar", P::range (0.0f, 30.0f), 10.0f, "ct", 0);
    P::pct (l, "dbl_mix", "Doble: volumen", 0.0f, 100.0f, 60.0f);
    // vocoder, synth, talkbox, robot
    // the singer: a voice of its own that sings the notes of a MIDI clip (no recording needed)
    P::toggle (l, "sing_on", "Cantar las notas MIDI", false);
    P::choice (l, "sing_vowel", "Cantar: vocal", { "Sílabas automáticas"_u, "A", "E", "I", "O", "U" }, 0);
    P::choice (l, "sing_voice", "Cantar: voz", { "Grave", "Media", "Aguda" }, 1);
    P::db (l, "sing_level", "Cantar: volumen", -24.0f, 6.0f, 0.0f);
    P::pct (l, "sing_breath", "Cantar: aire", 0.0f, 100.0f, 25.0f);
    P::toggle (l, "voc_on", "Vocoder", false);
    P::pct (l, "voc_mix", "Vocoder: mezcla", 0.0f, 100.0f, 80.0f);
    P::choice (l, "voc_carrier", "Vocoder: portadora", { "Sinte con MIDI", "Sidechain" }, 0);
    P::flt (l, "voc_q", "Vocoder: definición"_u, P::range (2.0f, 20.0f), 6.0f, "", 1);
    P::pct (l, "voc_sib", "Vocoder: eses", 0.0f, 100.0f, 30.0f);
    P::flt (l, "voc_shift", "Vocoder: formantes", P::range (-12.0f, 12.0f), 0.0f, "st", 1);
    P::choice (l, "syn_wave", "Sinte: onda", { "Sierra", "Cuadrada", "Mezcla" }, 0);
    P::flt (l, "syn_detune", "Sinte: desafinar", P::range (0.0f, 40.0f), 8.0f, "ct", 0);
    P::ms (l, "syn_glide", "Sinte: portamento", 0.0f, 500.0f, 20.0f);
    P::toggle (l, "tb_on", "Talkbox", false);
    P::pct (l, "tb_mix", "Talkbox: mezcla", 0.0f, 100.0f, 80.0f);
    P::pct (l, "tb_bright", "Talkbox: brillo", 0.0f, 100.0f, 50.0f);
    P::toggle (l, "rb_on", "Robot", false);
    P::choice (l, "rb_mode", "Robot: nota", { "Nota fija", "Sigue el MIDI" }, 0);
    P::integer (l, "rb_note", "Robot: nota fija", 36, 84, 57);
    P::pct (l, "rb_amount", "Robot: cantidad", 0.0f, 100.0f, 100.0f);
    P::pct (l, "rb_metal", "Robot: metal", 0.0f, 100.0f, 0.0f);
    P::hz (l, "rb_metalhz", "Robot: frecuencia del metal", 20.0f, 2000.0f, 120.0f);
    // chopper
    P::toggle (l, "ch_on", "Chopper", false);
    P::choice (l, "ch_div", "Chopper: rejilla", divisionNames(), 10);
    P::integer (l, "ch_steps", "Chopper: pasos", 1, chopSteps, 8);
    for (int s = 0; s < chopSteps; ++s) P::toggle (l, chId (s), "Chopper: paso " + String (s + 1), s % 4 != 3);
    P::pct (l, "ch_depth", "Chopper: profundidad", 0.0f, 100.0f, 100.0f);
    P::ms (l, "ch_smooth", "Chopper: suavizado", 1.0f, 30.0f, 4.0f);
    P::pct (l, "ch_stutter", "Chopper: repeticiones", 0.0f, 100.0f, 0.0f);
    // effects
    P::pct (l, "sat", "Saturación"_u, 0.0f, 100.0f, 0.0f);
    P::choice (l, "flt", "Filtro", { "Apagado", "Teléfono"_u, "Radio", "Megáfono"_u }, 0);
    P::toggle (l, "dl_on", "Delay", false);
    P::choice (l, "dl_div", "Delay: tiempo", divisionNames(), 7);
    P::pct (l, "dl_fb", "Delay: repeticiones", 0.0f, 90.0f, 30.0f);
    P::pct (l, "dl_mix", "Delay: mezcla", 0.0f, 100.0f, 20.0f);
    P::toggle (l, "rv_on", "Reverb", false);
    P::pct (l, "rv_size", "Reverb: tamaño"_u, 0.0f, 100.0f, 50.0f);
    P::pct (l, "rv_mix", "Reverb: mezcla", 0.0f, 100.0f, 20.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
    P::db (l, "outgain", "Volumen", -24.0f, 12.0f, 0.0f);
    // modulation
    for (int k = 0; k < 2; ++k)
    {
        P::hz (l, "lfo" + String (k + 1) + "_rate", "LFO " + String (k + 1) + ": velocidad", 0.05f, 12.0f, k == 0 ? 0.5f : 2.0f);
        P::choice (l, "lfo" + String (k + 1) + "_shape", "LFO " + String (k + 1) + ": forma", { "Seno", "Triángulo"_u, "Cuadrada", "Al azar" }, 0);
    }
    for (int r = 0; r < numRoutes; ++r)
    {
        P::choice (l, mId (r, "src"), "Ruta " + String (r + 1) + ": fuente", modSources(), 0);
        P::choice (l, mId (r, "dst"), "Ruta " + String (r + 1) + ": destino", modDestinations(), 0);
        P::pct (l, mId (r, "amt"), "Ruta " + String (r + 1) + ": cantidad", -100.0f, 100.0f, 50.0f);
    }
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true)
                                        .withInput ("Sidechain", AudioChannelSet::stereo(), false),
                       PluginInfo { "Chinchilla", "Laboratorio vocal", Colour (0xffb9a3d9), PluginKind::effect, 1, 2.0, true, false },
                       layout())
{
    wantsMidiAsEffect = true;
    for (int v = 0; v < numHarmonies; ++v) for (int k = 0; k < 8; ++k) hp_[v][k] = param (hId (v, hKeys[k]));
    static const char* mKeys[] { "src", "dst", "amt" };
    for (int r = 0; r < numRoutes; ++r) for (int k = 0; k < 3; ++k) mp_[r][k] = param (mId (r, mKeys[k]));
    for (int s = 0; s < chopSteps; ++s) chp_[s] = param (chId (s));
    frames.resize (120000);
}

// ===================================================================== audio
void Processor::prepareDsp (double sampleRate, int maxBlock)
{
    lat = (int) std::round (0.045 * sampleRate);
    hist.prepare (lat * 4 + 8);
    yin.prepare (sampleRate);
    lead.prepare (sampleRate, lat);
    for (auto& h : harm) h.prepare (sampleRate, lat);
    synth.prepare (sampleRate);
    singer.prepare (sampleRate);
    vocoder.prepare (sampleRate);
    talkbox.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    for (auto& d : dry) d.prepare (lat + jmax (4096, maxBlock) + 8);
    for (auto& d : harmDelay) d.prepare ((int) (0.07 * sampleRate));
    dblLine.prepare ((int) (0.08 * sampleRate));
    for (auto& d : fxDelay) d.prepare ((int) (2.1 * sampleRate));
    stutter.assign ((size_t) (2.0 * sampleRate), 0.0f);
    fft = std::make_unique<juce::dsp::FFT> (10);
    fftIn.assign (2048, 0.0f);
    setLatencySamples (lat);
    resetDsp();
}

void Processor::resetDsp()
{
    yin.reset(); lead.reset(); for (auto& h : harm) h.reset();
    synth.reset(); singer.reset(); vocoder.reset(); talkbox.reset(); reverb.reset();
    for (auto& d : dry) d.clear();
    for (auto& d : harmDelay) d.clear();
    dblLine.clear();
    for (auto& d : fxDelay) d.clear();
    hp.reset(); deessBand.reset(); phone[0].reset(); phone[1].reset();
    gateEnv = 0.0f; gateGain = 1.0f; deessEnv = 0.0f; compEnv = 0.0f; compGain = 1.0f;
    midiIn = -1.0f; centre = -1.0f; correction = 0.0f; heldTarget = -1.0f; period = 0.0;
    chopGain = 1.0f; lastChopStep = -1; stuttering = false;
    rng.seed (12345);
    hist.prepare ((int) hist.buf.size());
    fftPos = 0; dlNow = -1.0f; satX[0] = satX[1] = phoneX = 0.0f;
    std::fill (std::begin (scopeRing), std::end (scopeRing), 0.0f);
    scopeWrite.store (0);
    voiceEnv = 0.0f;
    std::fill (std::begin (hGate), std::end (hGate), 0.0f);
    std::fill (std::begin (hHas), std::end (hHas), false);
    hVoiced = 0.0f; desiredCorrection = 0.0f;
}

void Processor::updateModulation (int n)
{
    // two LFOs, then the routes
    float lfo[2];
    for (int k = 0; k < 2; ++k)
    {
        const float rate = value (k == 0 ? "lfo1_rate" : "lfo2_rate");
        const int shape = (int) value (k == 0 ? "lfo1_shape" : "lfo2_shape");
        lfoPhase[k] += rate * n / sr;
        if (lfoPhase[k] >= 1.0) { lfoPhase[k] -= std::floor (lfoPhase[k]); lfoHold[k] = rng.nextBipolar(); }
        const float p = (float) lfoPhase[k];
        lfo[k] = shape == 0 ? std::sin (MathConstants<float>::twoPi * p) : shape == 1 ? 1.0f - 4.0f * std::abs (p - 0.5f) : shape == 2 ? (p < 0.5f ? 1.0f : -1.0f) : lfoHold[k];
    }
    const float src[7] { 0.0f, lfo[0], lfo[1], jlimit (0.0f, 1.0f, voiceEnv * 4.0f), midiIn > 0.0f ? jlimit (-1.0f, 1.0f, (midiIn - 60.0f) / 24.0f) : 0.0f,
                         lastVelocity, modWheel };
    for (auto& m : modNow) m = 0.0f;
    for (int r = 0; r < numRoutes; ++r)
    {
        const int s = (int) mp_[r][0]->load(), d = (int) mp_[r][1]->load();
        const float v = s > 0 && s < 7 ? src[s] * mp_[r][2]->load() * 0.01f : 0.0f;
        modValue[r].store (v);
        if (d > 0 && d < 16) modNow[d] += v;
    }
}

float Processor::modulation (int d) const { return modNow[d]; }

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, false, 0);
    float* L = main.getWritePointer (0);
    float* R = main.getNumChannels() > 1 ? main.getWritePointer (1) : main.getWritePointer (0);
    const float* scL = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) scL = sc.getReadPointer (0);
    }

    // this block's settings
    bool mask[12];
    scaleMask ((int) value ("scale"), (int) value ("key"), mask);
    const bool tuneOn = value ("tune_on") > 0.5f;
    const int targetMode = (int) value ("target");
    const float retuneMs = value ("retune"), amount = value ("amount") * 0.01f, humanize = value ("humanize") * 0.01f;
    const float transpose = value ("transpose");
    const float hpHz = value ("cl_hp");
    hp.set (Biquad::highpass, sr, hpHz, 0.7071);
    deessBand.set (Biquad::bandpass, sr, 6500.0, 1.2);
    const float gateThr = dsp::dbToGain (value ("cl_gate")), deess = value ("cl_deess") * 0.01f, comp = value ("cl_comp") * 0.01f, breath = value ("cl_breath") * 0.01f;
    const bool hOn = value ("h_on") > 0.5f;
    const float hMix = value ("h_mix") * 0.01f, tight = value ("tm_tight") * 0.01f;
    const bool dblOn = value ("dbl_on") > 0.5f;
    const int dblVoices = (int) value ("dbl_voices");
    const bool vocOn = value ("voc_on") > 0.5f, tbOn = value ("tb_on") > 0.5f, rbOn = value ("rb_on") > 0.5f;
    const int carrierMode = (int) value ("voc_carrier");
    const bool chOn = value ("ch_on") > 0.5f;
    const int chSteps = (int) value ("ch_steps");
    const double chStepSec = syncSeconds ((int) value ("ch_div"), bpm());
    const int flt = (int) value ("flt");
    const bool dlOn = value ("dl_on") > 0.5f, rvOn = value ("rv_on") > 0.5f;
    const float mix = value ("mix") * 0.01f, outGain = dsp::dbToGain (value ("outgain"));
    const float satAmt = value ("sat") * 0.01f;
    const double spp = sr * 60.0 / jlimit (20.0, 400.0, bpm());
    const float retuneCoef = retuneMs <= 0.5f ? 1.0f : 1.0f - std::exp (-1.0f / (retuneMs * 0.001f * (float) sr));
    const float gateA = std::exp (-1.0f / (0.001f * (float) sr)), gateR = std::exp (-1.0f / (0.08f * (float) sr));
    const float att5 = std::exp (-1.0f / (0.005f * (float) sr)), rel100 = std::exp (-1.0f / (0.1f * (float) sr));

    updateModulation (n);
    const float formSt = jlimit (-6.0f, 6.0f, value ("formant") + modulation (dFormant) * 6.0f);
    const float hGainMod = jlimit (0.0f, 2.0f, 1.0f + modulation (dHarm));
    const float vocMix = jlimit (0.0f, 1.0f, value ("voc_mix") * 0.01f + modulation (dVocoder));
    const float tbMix = value ("tb_mix") * 0.01f;
    const float chDepth = jlimit (0.0f, 1.0f, value ("ch_depth") * 0.01f + modulation (dChop));
    const float dlMix = jlimit (0.0f, 1.0f, value ("dl_mix") * 0.01f + modulation (dDelay) * 0.5f);
    const float rvMix = jlimit (0.0f, 1.0f, value ("rv_mix") * 0.01f + modulation (dReverb) * 0.5f);
    const float tuneCents = modulation (dTune) * 100.0f;
    const float robotAmt = jlimit (0.0f, 1.0f, value ("rb_amount") * 0.01f + modulation (dRobot));
    const float dblWidth = jlimit (0.0f, 1.0f, value ("dbl_width") * 0.01f + modulation (dWidth));
    if (flt > 0)
    {
        const float shift = std::exp2 (modulation (dFilter));
        const float lo = flt == 1 ? 350.0f : flt == 2 ? 500.0f : 900.0f, hi = flt == 1 ? 3200.0f : flt == 2 ? 5000.0f : 1800.0f;
        phone[0].set (Biquad::highpass, sr, lo * shift, 0.9);
        phone[1].set (Biquad::lowpass, sr, hi * shift, 0.9);
    }
    {
        // the new 8-line FDN reverb. Its levels follow the old one (dry x2), so 0.5 keeps the voice at its level:
        // before, switching the reverb on also made the dry voice 6 dB louder
        racoon::sdsp::Verb::Parameters rp;
        rp.roomSize = 0.3f + 0.68f * value ("rv_size") * 0.01f; rp.damping = 0.4f; rp.wetLevel = rvOn ? rvMix : 0.0f; rp.dryLevel = 0.5f; rp.width = 1.0f;
        reverb.setParameters (rp);
    }
    const float dlSamples = (float) jlimit (10.0, 2.0 * sr, syncSeconds ((int) value ("dl_div"), bpm()) * sr);
    // a new division or tempo is reached in about 60 ms (before, the time jumped and the echo clicked)
    if (dlNow < 0.0f) dlNow = dlSamples;
    const float dlGlide = 1.0f - (float) std::exp (-1.0 / (0.060 * sr));
    const float dlFb = value ("dl_fb") * 0.01f;

    // harmony voice settings
    struct HV { bool on; int mode, interval; float gain, l, r, formant, delay, det; };
    HV hv[numHarmonies];
    for (int v = 0; v < numHarmonies; ++v)
    {
        auto& h = hv[v];
        h.on = hOn && hp_[v][0]->load() > 0.5f;
        h.mode = (int) hp_[v][1]->load(); h.interval = (int) hp_[v][2]->load();
        h.gain = dsp::dbToGain (hp_[v][3]->load()) * hMix * hGainMod;
        const float pan = hp_[v][4]->load() * 0.01f;
        h.l = std::sqrt (0.5f * (1.0f - pan)); h.r = std::sqrt (0.5f * (1.0f + pan));
        h.formant = std::exp2 ((formSt + hp_[v][5]->load()) / 12.0f);
        h.delay = (float) (hp_[v][6]->load() * 0.001 * sr) * (1.0f - 0.8f * tight) + 1.0f;
        h.det = hp_[v][7]->load() * (1.0f - 0.5f * tight);
    }

    const bool singOn = value ("sing_on") > 0.5f;
    const int singVowel = (int) value ("sing_vowel") - 1;
    const float singSize = (int) value ("sing_voice") == 0 ? 0.86f : (int) value ("sing_voice") == 2 ? 1.14f : 1.0f;
    const float singGain = dsp::dbToGain (value ("sing_level")) * 0.09f, singBreath = value ("sing_breath") * 0.01f;
    auto it = midi.begin();
    for (int i = 0; i < n; ++i)
    {
        for (; it != midi.end() && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) { synth.noteOn (m.getNoteNumber(), m.getFloatVelocity()); singer.noteOn (m.getNoteNumber(), m.getFloatVelocity()); lastVelocity = m.getFloatVelocity(); }
            else if (m.isNoteOff()) { synth.noteOff (m.getNoteNumber()); singer.noteOff (m.getNoteNumber()); }
            else if (m.isController() && m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { synth.allOff(); singer.reset(); }
        }
        float inL = L[i], inR = R[i];
        if (singOn)
        {
            const float v = singer.process (singVowel, singSize, singBreath) * singGain;
            inL += v; inR += v;
        }
        dry[0].push (inL); dry[1].push (inR);
        float x = 0.5f * (inL + inR);

        // ---------------------------------------------------------------- cleanup
        x = hp.process (x);
        const float ax = std::abs (x);
        gateEnv = ax + (gateEnv - ax) * (ax > gateEnv ? gateA : gateR);
        const float gTarget = gateEnv > gateThr ? 1.0f : 0.0f;
        gateGain = gTarget + (gateGain - gTarget) * (gTarget > gateGain ? 0.99f : 0.9995f);
        x *= gateGain;
        if (deess > 0.0f)
        {
            const float s = deessBand.process (x);
            const float as = std::abs (s);
            deessEnv = as + (deessEnv - as) * (as > deessEnv ? att5 : rel100);
            const float over = jlimit (0.0f, 1.0f, (dsp::gainToDb (deessEnv) + 40.0f - 20.0f * (1.0f - deess)) / 12.0f);
            x -= s * over * deess;
        }
        if (comp > 0.0f)
        {
            const float a = std::abs (x);
            compEnv = a + (compEnv - a) * (a > compEnv ? att5 : rel100);
            const float thr = -10.0f - 25.0f * comp, db = dsp::gainToDb (compEnv);
            const float g = db > thr ? dsp::dbToGain ((thr - db) * (1.0f - 1.0f / 3.0f)) : 1.0f;
            compGain = g + (compGain - g) * 0.999f;
            x *= compGain * dsp::dbToGain (comp * 10.0f);
        }
        if (breath > 0.0f && yin.clarity() < 0.4f && gateEnv < 0.02f) x *= dsp::dbToGain (-18.0f * breath);
        voiceEnv = std::abs (x) + (voiceEnv - std::abs (x)) * 0.999f;
        hist.push (x);

        // ---------------------------------------------------------------- pitch
        if (yin.push (x))
        {
            const float f0 = yin.hz();
            clarityNow.store (yin.clarity());
            const bool voiced = f0 > 60.0f && f0 < 1100.0f && yin.clarity() > 0.5f;
            if (voiced)
            {
                midiIn = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
                period = sr / f0;
                const float hopSec = 64.0f * (float) yin.decimation() / (float) sr;
                const float tau = 0.005f + humanize * 0.35f;
                centre = centre < 0.0f ? midiIn : centre + (midiIn - centre) * (1.0f - std::exp (-hopSec / tau));
            }
            else { period = 0.0; centre = -1.0f; heldTarget = -1.0f; }
            if (capture.load() && isPlaying())
            {
                const int k = frameCount.load();
                if (k < (int) frames.size()) { frames[(size_t) k] = { ppq() + i / spp, voiced ? midiIn : -1.0f }; frameCount.store (k + 1); }
                capturedFrames.store (frameCount.load());
            }
            // where it should go
            float target = -1.0f;
            float amt = amount;
            if (voiced)
            {
                if (targetMode == 2)
                {
                    SpinLock::ScopedTryLockType lock (segLock);
                    if (lock.isLocked())
                    {
                        const double at = ppq() + (i - lat) / spp;
                        for (auto& s : segs) if (at >= s.start && at < s.end) { target = (float) (s.note + s.offset); amt = 1.0f; break; }
                    }
                }
                if (target < 0.0f && targetMode == 1 && synth.heldCount() > 0)
                {
                    float best = 1.0e9f;
                    for (int k = 0; k < synth.heldCount(); ++k)
                        for (int o = -24; o <= 24; o += 12)
                        {
                            const float cand = (float) (synth.held (k) + o);
                            if (std::abs (cand - centre) < std::abs (best - centre)) best = cand;
                        }
                    target = best;
                }
                if (target < 0.0f)
                {
                    // the scale, with a little hysteresis so it does not flutter between two notes
                    const float q = nearestInScale (centre, mask);
                    if (heldTarget < 0.0f || std::abs (centre - heldTarget) > 0.6f) heldTarget = q;
                    target = heldTarget;
                }
            }
            float desired = transpose + tuneCents * 0.01f;
            if (voiced && tuneOn) desired += amt * (target - centre);
            if (voiced && rbOn)
            {
                const int note = (int) value ("rb_mode") == 1 && synth.heldCount() > 0 ? synth.lowestHeld() : (int) value ("rb_note");
                desired += (((float) note - midiIn) - desired) * robotAmt;
            }
            targetMidi.store (voiced ? midiIn + desired : -1.0f);
            detectedMidi.store (voiced ? midiIn : -1.0f);
            // harmonies follow the corrected lead
            const float leadC = midiIn + desired;
            for (int v = 0; v < numHarmonies; ++v)
            {
                hHas[v] = false;
                if (! hv[v].on || ! voiced) continue;
                float t = leadC + (float) hv[v].interval;
                if (hv[v].mode == 1)
                {
                    const float q = nearestInScale (leadC, mask);
                    int note = (int) q;
                    const int dir = hv[v].interval >= 0 ? 1 : -1;
                    for (int s = 0; s < std::abs (hv[v].interval); ++s) { do note += dir; while (! mask[((note % 12) + 12) % 12]); }
                    t = (float) note + (leadC - q);
                }
                else if (hv[v].mode == 2)
                {
                    const int held = synth.held (v);
                    if (held < 0) continue;
                    t = (float) held;
                }
                hTarget[v] = t;
                hHas[v] = true;
            }
            // history for the screen
            if (++historyDecim >= 2)
            {
                historyDecim = 0;
                const int p = historyPos.load();
                detectedHistory[p].store (voiced ? midiIn : -1.0f);
                correctedHistory[p].store (voiced ? midiIn + desired : -1.0f);
                historyPos.store ((p + 1) % pitchHistory);
            }
            correctedMidi.store (voiced ? midiIn + correction : -1.0f);
            hVoiced = voiced ? 1.0f : 0.0f;
            desiredCorrection = desired;
        }
        correction += (desiredCorrection - correction) * retuneCoef;
        const float leadRatio = std::exp2 (correction / 12.0f);
        const float formantRatio = std::exp2 (formSt / 12.0f);
        float leadOut = lead.process (hist, period, leadRatio, formantRatio);
        // nothing to change: the voice passes untouched (only delayed)
        if (! tuneOn && ! rbOn && std::abs (correction) < 0.01f && std::abs (formSt) < 0.01f) leadOut = hist.at ((double) hist.written - 1.0 - lat);
        if (rbOn && value ("rb_metal") > 0.0f)
        {
            metalPhase += value ("rb_metalhz") / (float) sr;
            if (metalPhase >= 1.0f) metalPhase -= 1.0f;
            const float mAmt = value ("rb_metal") * 0.01f;
            leadOut = leadOut * (1.0f - mAmt) + leadOut * std::sin (MathConstants<float>::twoPi * metalPhase) * mAmt;
        }

        // ---------------------------------------------------------------- harmonies
        float outL = 0.0f, outR = 0.0f;
        for (int v = 0; v < numHarmonies; ++v)
        {
            const auto& h = hv[v];
            const float gTarget2 = h.on && hHas[v] && hVoiced > 0.0f ? 1.0f : 0.0f;
            hGate[v] = gTarget2 + (hGate[v] - gTarget2) * 0.999f;
            if (! h.on && hGate[v] < 1.0e-4f) continue;
            const float ratio = hHas[v] && midiIn > 0.0f ? std::exp2 ((hTarget[v] - midiIn + h.det * 0.01f) / 12.0f) : 1.0f;
            const float y = harm[v].process (hist, hHas[v] ? period : 0.0, ratio, h.formant) * hGate[v];
            harmDelay[v].push (y);
            const float d = harmDelay[v].tap (h.delay);
            outL += d * h.gain * h.l;
            outR += d * h.gain * h.r;
        }

        // ---------------------------------------------------------------- doubles
        dblLine.push (leadOut);
        if (dblOn)
        {
            const float mixD = value ("dbl_mix") * 0.01f;
            const float spread = value ("dbl_spread") * 0.001f * (float) sr * (1.0f - 0.6f * tight);
            const float det = value ("dbl_detune");
            for (int d = 0; d < dblVoices; ++d)
            {
                const double rate = 0.17 + 0.09 * d;
                dblPhase[d] += rate / sr;
                if (dblPhase[d] >= 1.0) dblPhase[d] -= 1.0;
                const double depth = det / (1731.0 * MathConstants<double>::twoPi * rate) * sr;
                const float delay = spread * (0.45f + 0.55f * (float) (d + 1) / (float) dblVoices) + (float) (depth * (1.0 + std::sin (MathConstants<double>::twoPi * dblPhase[d]))) + 2.0f;
                const float y = dblLine.tap (delay) * mixD / std::sqrt ((float) dblVoices);
                const float pan = (d % 2 == 0 ? -1.0f : 1.0f) * dblWidth;
                outL += y * std::sqrt (0.5f * (1.0f - pan));
                outR += y * std::sqrt (0.5f * (1.0f + pan));
            }
        }

        // ---------------------------------------------------------------- vocoder and talkbox
        float leadGain = 1.0f;
        if (vocOn || tbOn)
        {
            const float carrier = carrierMode == 1 && scL != nullptr ? scL[i] : synth.process ((int) value ("syn_wave"), value ("syn_detune"), value ("syn_glide"));
            const float voice = hist.at ((double) hist.written - 1.0 - lat);
            float fx = 0.0f;
            if (vocOn) { fx += vocoder.process (voice, carrier, value ("voc_q"), value ("voc_sib") * 0.01f, value ("voc_shift")) * vocMix; leadGain -= 0.8f * vocMix; }
            if (tbOn) { fx += talkbox.process (voice, carrier, value ("tb_bright") * 0.01f) * tbMix; leadGain -= 0.8f * tbMix; }
            outL += fx; outR += fx;
        }
        leadGain = jmax (0.0f, leadGain);
        outL += leadOut * leadGain;
        outR += leadOut * leadGain;

        // ---------------------------------------------------------------- chopper
        if (chOn)
        {
            const double secs = isPlaying() ? ppq() * 60.0 / bpm() + i / sr : (double) hist.written / sr;
            const int64 step = (int64) std::floor (secs / chStepSec);
            const int k = (int) (((step % chSteps) + chSteps) % chSteps);
            const int stepLen = (int) (chStepSec * sr);
            if (step != lastChopStep)
            {
                lastChopStep = step;
                chopStepNow.store (k);
                const float r = (float) ((uint32) (step * 2654435761u) % 1000u) / 1000.0f;
                stuttering = r < value ("ch_stutter") * 0.01f && stepLen > 64;
                stutterLen = jlimit (16, (int) stutter.size() / 2, stepLen / 4);
                stutterPos = 0;
            }
            if (stuttering)
            {
                if (stutterPos < stutterLen) { stutter[(size_t) (2 * stutterPos)] = outL; stutter[(size_t) (2 * stutterPos + 1)] = outR; }
                else { const int p = (stutterPos % stutterLen); outL = stutter[(size_t) (2 * p)]; outR = stutter[(size_t) (2 * p + 1)]; }
                ++stutterPos;
            }
            const float target = chp_[k]->load() > 0.5f ? 1.0f : 1.0f - chDepth;
            const float c = std::exp (-1.0f / (value ("ch_smooth") * 0.001f * (float) sr));
            chopGain = target + (chopGain - target) * c;
            outL *= chopGain; outR *= chopGain;
        }

        // ---------------------------------------------------------------- effects
        if (satAmt > 0.0f)
        {
            const float k = 1.0f + satAmt * 8.0f;
            // ADAA: the saturation averaged between samples (no fizz folded back on a bright voice)
            auto th = [] (float v) { return std::tanh (v); };
            outL = racoon::sdsp::adaaAverage (th, outL * k, satX[0]) / std::sqrt (k);
            outR = racoon::sdsp::adaaAverage (th, outR * k, satX[1]) / std::sqrt (k);
        }
        if (flt > 0)
        {
            float m = phone[1].process (phone[0].process (0.5f * (outL + outR)));
            if (flt >= 2) m = racoon::sdsp::adaaAverage ([] (float v) { return std::tanh (v); }, m * (flt == 3 ? 4.0f : 2.0f), phoneX) * 0.6f;
            outL = outR = m;
        }
        if (dlOn)
        {
            dlNow += (dlSamples - dlNow) * dlGlide;
            const float a = fxDelay[0].tap (dlNow), b = fxDelay[1].tap (dlNow);
            fxDelay[0].push (outL + b * dlFb);
            fxDelay[1].push (outR + a * dlFb);
            outL += a * dlMix; outR += b * dlMix;
        }
        L[i] = outL; R[i] = outR;

        // analyzer: just remember the sample; the FFT is done by the screen (updateSpectrum), never here
        scopeRing[fftPos & 2047] = 0.5f * (outL + outR);
        fftPos = (fftPos + 1) & 0x3fffffff;
    }
    scopeWrite.store (fftPos & 0x3fffffff, std::memory_order_release);
    if (rvOn) reverb.processStereo (L, R, n);
    for (int b = 0; b < Vocoder::bands; ++b) vocoderBands[b].store (vocoder.bandLevel (b));

    // dry / wet (the dry is delayed like the wet)
    if (mix < 0.999f)
    {
        // the dry ring holds the last lat + 8 samples; read it aligned
        for (int i = 0; i < n; ++i)
        {
            const float dl = dry[0].tapInt (lat + (n - i)), dr = dry[1].tapInt (lat + (n - i));
            L[i] = L[i] * mix + dl * (1.0f - mix);
            R[i] = R[i] * mix + dr * (1.0f - mix);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        L[i] = jlimit (-2.0f, 2.0f, L[i] * outGain);
        R[i] = jlimit (-2.0f, 2.0f, R[i] * outGain);
    }
}

// ===================================================================== note editor
void Processor::startCapture() { frameCount.store (0); capturedFrames.store (0); capture.store (true); }

void Processor::stopCapture()
{
    capture.store (false);
    const int count = frameCount.load();
    std::vector<Frame> f (frames.begin(), frames.begin() + count);
    std::sort (f.begin(), f.end(), [] (const Frame& a, const Frame& b) { return a.ppq < b.ppq; });
    std::vector<Segment> out;
    const double minLen = 0.08 * bpm() / 60.0;
    size_t i = 0;
    while (i < f.size())
    {
        if (f[i].midi < 0.0f) { ++i; continue; }
        size_t j = i;
        std::vector<float> vals;
        float ref = f[i].midi;
        while (j < f.size() && f[j].midi >= 0.0f && std::abs (f[j].midi - ref) < 0.7f)
        {
            vals.push_back (f[j].midi);
            std::vector<float> sorted (vals);
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            ref = sorted[sorted.size() / 2];
            ++j;
        }
        const double end = j < f.size() ? f[j].ppq : f[j - 1].ppq + 0.02;
        if (end - f[i].ppq >= minLen) out.push_back ({ f[i].ppq, end, (int) std::round (ref), 0 });
        i = jmax (j, i + 1);
    }
    {
        SpinLock::ScopedLockType lock (segLock);
        segs = out;
    }
    if (! out.empty()) setRealValue ("target", 2.0f);   // from now on it tunes to the notes of the editor
}

std::vector<Processor::Segment> Processor::segments() const { SpinLock::ScopedLockType lock (segLock); return segs; }

void Processor::setSegmentOffset (int index, int semitones)
{
    SpinLock::ScopedLockType lock (segLock);
    if (index >= 0 && index < (int) segs.size()) segs[(size_t) index].offset = jlimit (-24, 24, semitones);
}

void Processor::clearSegments() { SpinLock::ScopedLockType lock (segLock); segs.clear(); }

MidiFile Processor::exportMidi() const
{
    MidiMessageSequence seq;
    const double tempo = jlimit (20.0, 400.0, bpm());
    seq.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / tempo)), 0.0);
    seq.addEvent (MidiMessage::textMetaEvent (3, "Chinchilla"), 0.0);
    const auto s = segments();
    const double origin = s.empty() ? 0.0 : std::floor (s.front().start / 4.0) * 4.0;
    for (auto& g : s)
    {
        const int note = jlimit (0, 127, g.note + g.offset);
        seq.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), (g.start - origin) * 960.0);
        seq.addEvent (MidiMessage::noteOff (1, note), (g.end - origin) * 960.0);
    }
    seq.sort();
    seq.updateMatchedPairs();
    MidiFile mf;
    mf.setTicksPerQuarterNote (960);
    mf.addTrack (seq);
    return mf;
}

File Processor::exportMidiFile() const
{
    auto dir = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Chinchilla");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Chinchilla " + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".mid", false);
    FileOutputStream out (file);
    if (out.openedOk()) exportMidi().writeTo (out);
    return file;
}

void Processor::saveExtraState (ValueTree& v) const
{
    String s;
    for (auto& g : segments()) s << String (g.start, 4) << ":" << String (g.end, 4) << ":" << g.note << ":" << g.offset << ";";
    v.setProperty ("segments", s, nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    std::vector<Segment> out;
    for (auto& t : StringArray::fromTokens (v.getProperty ("segments").toString(), ";", ""))
    {
        const auto f = StringArray::fromTokens (t, ":", "");
        if (f.size() == 4) out.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), f[3].getIntValue() });
    }
    SpinLock::ScopedLockType lock (segLock);
    segs = out;
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    auto harmony = [] (int v, int mode, int interval, float pan, float lvl = -4.0f)
    {
        return std::vector<std::pair<String, float>> { { hId (v, "on"), 1 }, { hId (v, "mode"), (float) mode }, { hId (v, "int"), (float) interval },
                                                       { hId (v, "pan"), pan }, { hId (v, "lvl"), lvl } };
    };
    auto join = [] (std::initializer_list<std::vector<std::pair<String, float>>> parts)
    {
        std::vector<std::pair<String, float>> all;
        for (auto& p : parts) all.insert (all.end(), p.begin(), p.end());
        return all;
    };
    std::vector<std::pair<String, float>> offAll;
    for (int v = 0; v < numHarmonies; ++v) offAll.push_back ({ hId (v, "on"), 0 });
    return {
        { "Afinación natural"_u, "Afinar", join ({ offAll, { { "retune", 60 }, { "humanize", 50 }, { "amount", 80 } } }) },
        { "Hard tune", "Afinar", join ({ offAll, { { "retune", 0 }, { "humanize", 0 }, { "amount", 100 }, { "scale", 1 } } }) },
        { "Terceras arriba y abajo", "Armonías"_u, join ({ offAll, { { "h_on", 1 } }, harmony (0, 1, 2, -40), harmony (1, 1, -2, 40) }) },
        { "Coro de armonías"_u, "Armonías"_u, join ({ offAll, { { "h_on", 1 }, { "h_mix", 70 }, { "rv_on", 1 }, { "rv_mix", 30 } }, harmony (0, 1, 2, -50), harmony (1, 1, 4, 50),
                                                       harmony (2, 1, -3, -80), harmony (3, 1, 7, 80), harmony (4, 0, -12, 0, -8) }) },
        { "Armonías con MIDI"_u, "Armonías"_u, join ({ offAll, { { "h_on", 1 } }, harmony (0, 2, 0, -60), harmony (1, 2, 0, 0), harmony (2, 2, 0, 60) }) },
        { "Vocoder robot", "Robot", join ({ offAll, { { "voc_on", 1 }, { "voc_mix", 100 }, { "syn_wave", 0 }, { "tune_on", 0 } } }) },
        { "Talkbox funk", "Robot", join ({ offAll, { { "tb_on", 1 }, { "tb_mix", 100 }, { "syn_wave", 2 }, { "syn_glide", 60 }, { "tune_on", 0 } } }) },
        { "Robot monótono"_u, "Robot", join ({ offAll, { { "rb_on", 1 }, { "rb_amount", 100 }, { "rb_metal", 30 } } }) },
        { "Ardilla (formantes arriba)", "Formantes", join ({ offAll, { { "formant", 5 }, { "transpose", 7 } } }) },
        { "Monstruo (formantes abajo)", "Formantes", join ({ offAll, { { "formant", -5 }, { "transpose", -7 } } }) },
        { "Voz de radio", "Efectos", join ({ offAll, { { "flt", 2 }, { "sat", 30 }, { "cl_comp", 60 } } }) },
        { "Doble ancho", "Doble", join ({ offAll, { { "dbl_on", 1 }, { "dbl_voices", 2 }, { "dbl_width", 100 }, { "retune", 80 } } }) },
        { "Voz picada", "Chopper", join ({ offAll, { { "ch_on", 1 }, { "ch_div", 10 }, { "ch_stutter", 25 }, { "dl_on", 1 }, { "dl_mix", 25 } } }) },
        { "Voz limpia", "Limpieza", join ({ offAll, { { "tune_on", 0 }, { "cl_hp", 100 }, { "cl_deess", 50 }, { "cl_comp", 40 }, { "cl_breath", 50 }, { "cl_gate", -55 } } }) },
    };
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 1.0, 64, 100, 1 },
             { 1.0, 1.0, 67, 100, 1 },
             { 2.0, 1.5, 69, 100, 1 },
             { 3.5, 0.5, 67, 90, 1 },
             { 4.0, 1.0, 64, 100, 1 },
             { 5.0, 1.0, 62, 100, 1 },
             { 6.0, 1.9, 60, 100, 1 } };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

namespace racoon::chinchilla
{
void Processor::updateSpectrum()
{
    if (fft == nullptr || fftIn.size() < 2048) return;
    const int w = scopeWrite.load (std::memory_order_acquire);
    for (int k = 0; k < 1024; ++k)
    {
        const float x = scopeRing[(w - 1024 + k) & 2047];
        fftIn[(size_t) k] = x * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * k / 1023.0f));
    }
    std::fill (fftIn.begin() + 1024, fftIn.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftIn.data());
    for (int b = 0; b < 64; ++b)
    {
        const int lo = juce::jmax (1, (int) std::pow (2.0, b / 64.0 * 9.0)), hi = juce::jmax (lo + 1, (int) std::pow (2.0, (b + 1) / 64.0 * 9.0));
        float s = 0.0f;
        for (int k = lo; k < hi && k < 512; ++k) s = juce::jmax (s, fftIn[(size_t) k]);
        spectrum[b].store (juce::Decibels::gainToDecibels (s / 256.0f, -100.0f));
    }
}
}

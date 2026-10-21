#include "MoleProcessor.h"
#include "MoleEditor.h"
#include <complex>

namespace racoon::mole
{
using namespace juce;

enum Type { lp, hp, bp, notch, allpass, combPos, combNeg, ladderT };

const StringArray& Processor::typeNames()
{
    static StringArray a { "Paso bajo", "Paso alto", "Paso banda", "Notch", "Allpass", "Peine +", "Peine -", "Ladder" };
    return a;
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "type", "Tipo", typeNames(), 0);
    P::choice (l, "slope", "Pendiente", { "12 dB/oct", "24 dB/oct" }, 1);
    P::hz (l, "cutoff", "Corte", 10.0f, 20000.0f, 1200.0f);
    P::pct (l, "reso", "Resonancia", 0.0f, 98.0f, 30.0f);
    P::db (l, "drive", "Drive", 0.0f, 30.0f, 0.0f);
    P::choice (l, "drivepos", "Drive en", { "Antes", "Después"_u }, 0);
    P::flt (l, "lforate", "LFO velocidad", P::range (0.01f, 30.0f, 1.0f), 0.5f, "Hz", 2);
    P::toggle (l, "lfosync", "LFO a tempo", false);
    P::choice (l, "lfodiv", "LFO división"_u, RacoonProcessor::divisionNames(), 4);
    P::choice (l, "lfoshape", "LFO forma", { "Seno", "Triángulo"_u, "Sierra", "Cuadrada", "S&H" }, 0);
    P::pct (l, "lfodepth", "LFO cantidad", -100.0f, 100.0f, 35.0f);
    P::flt (l, "lfophase", "Desfase estéreo"_u, P::range (0.0f, 180.0f), 0.0f, "°"_u, 0);
    P::ms (l, "envatk", "Env ataque", 0.1f, 500.0f, 5.0f, 20.0f);
    P::ms (l, "envrel", "Env relajación"_u, 1.0f, 5000.0f, 200.0f, 200.0f);
    P::pct (l, "envamt", "Env cantidad", -100.0f, 100.0f, 0.0f);
    P::choice (l, "envsrc", "Env fuente", { "Entrada", "Sidechain" }, 0);
    P::choice (l, "stepdiv", "Pasos división"_u, RacoonProcessor::divisionNames(), 10);
    P::pct (l, "stepamt", "Pasos cantidad", -100.0f, 100.0f, 0.0f);
    P::pct (l, "stepglide", "Pasos glide", 0.0f, 100.0f, 10.0f);
    for (int i = 1; i <= numSteps; ++i)
        P::pct (l, "step" + String (i), "Paso " + String (i), -100.0f, 100.0f, (i % 2 == 1) ? 100.0f : -100.0f);
    P::pct (l, "keytrack", "Seguir teclado", 0.0f, 100.0f, 0.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
    P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties()
                           .withInput ("Input", AudioChannelSet::stereo(), true)
                           .withOutput ("Output", AudioChannelSet::stereo(), true)
                           .withInput ("Sidechain", AudioChannelSet::stereo(), false),
                       PluginInfo { "Filtro automático"_u, "Filtros"_u, Colour (0xffe0789a), PluginKind::effect, 1, 0.0, false, false },
                       layout())
{
    wantsMidiAsEffect = true;   // key tracking follows the notes of the track
    for (int i = 0; i < numSteps; ++i) stepParams[i] = param ("step" + String (i + 1));
}

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (l.getMainInputChannelSet() != out) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.inputBuses[1];
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo()) return false;
    }
    return true;
}

void Processor::prepareDsp (double rate, int block)
{
    juce::dsp::ProcessSpec spec { rate, (uint32) block, 2 };
    ladder.prepare (spec);
    ladderBuf.setSize (2, block);
    ladderAux.setSize (2, block);
    for (auto& c : comb) c.prepare ((int) (rate / 8.0) + 8);
    for (auto& s : cutSmooth) s.prepare (rate, 8.0);
    resoSmooth.prepare (rate, 20.0);
    mixSmooth.prepare (rate, 20.0);
    outSmooth.prepare (rate, 20.0);
    resetDsp();
}

void Processor::resetDsp()
{
    for (int c = 0; c < 2; ++c) { svfA[c].reset(); svfB[c].reset(); comb[c].clear(); cutSmooth[c].reset (std::log2 (value ("cutoff"))); preSat[c].reset(); postSat[c].reset(); }
    ladder.reset();
    follower.env = 0.0f;
    resoSmooth.reset (value ("reso") * 0.01f);
    mixSmooth.reset (value ("mix") * 0.01f);
    outSmooth.reset (dsp::dbToGain (value ("output")));
    lfoPhase = stepPhase = 0.0;
    rng.seed (1234);
    heldNote = -1;
}

float Processor::lfoValue (double ph, int shape)
{
    ph -= std::floor (ph);
    switch (shape)
    {
        case 0: return (float) std::sin (MathConstants<double>::twoPi * ph);
        case 1: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
        case 2: return (float) (2.0 * ph - 1.0);
        case 3: return ph < 0.5 ? 1.0f : -1.0f;
        default: return sh;
    }
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    auto main = getBusBuffer (buffer, false, 0);
    if (n > ladderBuf.getNumSamples()) { ladderBuf.setSize (2, n, false, false, true); ladderAux.setSize (2, n, false, false, true); }

    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        if (m.isNoteOn()) heldNote = m.getNoteNumber();
        else if (m.isNoteOff() && m.getNoteNumber() == heldNote) heldNote = -1;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldNote = -1;
    }

    const int type = (int) value ("type");
    const bool steep = (int) value ("slope") == 1;
    const float baseOct = std::log2 (value ("cutoff"));
    resoSmooth.target = value ("reso") * 0.01f;
    mixSmooth.target = value ("mix") * 0.01f;
    outSmooth.target = dsp::dbToGain (value ("output"));
    const float driveDb = value ("drive");
    const float driveG = dsp::dbToGain (driveDb);
    const bool drivePost = (int) value ("drivepos") == 1;
    const float lfoDepth = value ("lfodepth") * 0.04f;     // +-4 octaves
    const float envAmt = value ("envamt") * 0.04f;
    const float stepAmt = value ("stepamt") * 0.04f;
    const float keyOct = heldNote >= 0 ? (float) (heldNote - 60) / 12.0f * value ("keytrack") * 0.01f : 0.0f;
    const int shape = (int) value ("lfoshape");
    const double phaseOffset = value ("lfophase") / 360.0;
    follower.set (sr, value ("envatk"), value ("envrel"));
    const bool scEnv = (int) value ("envsrc") == 1 && getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
    AudioBuffer<float> sc;
    if (scEnv) sc = getBusBuffer (buffer, true, 1);

    // tempo-synced phases follow the host position while it plays
    const bool lfoSync = value ("lfosync") > 0.5f;
    const double lfoBeats = syncSeconds ((int) value ("lfodiv"), bpm()) * bpm() / 60.0;
    const double stepBeats = syncSeconds ((int) value ("stepdiv"), bpm()) * bpm() / 60.0;
    const double beatsPerSample = bpm() / 60.0 / sr;
    double ppqNow = ppq();
    const double lfoInc = lfoSync ? beatsPerSample / lfoBeats : value ("lforate") / sr;
    const float glide = value ("stepglide") * 0.01f;
    const float glideCoef = glide <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (sr * stepBeats * 60.0 / bpm() * glide * 0.5));
    float steps[numSteps];
    for (int i = 0; i < numSteps; ++i) steps[i] = stepParams[i]->load() * 0.01f;
    const float maxOct = std::log2 ((float) sr * 0.45f);

    if (isPlaying())
    {
        if (lfoSync) lfoPhase = std::fmod (ppqNow / lfoBeats, 1.0);
        stepPhase = ppqNow / stepBeats;
    }

    const bool useLadder = type == ladderT;
    if (useLadder)
    {
        ladder.setMode (steep ? juce::dsp::LadderFilterMode::LPF24 : juce::dsp::LadderFilterMode::LPF12);
        ladder.setDrive (jmax (1.0f, driveG));
    }

    for (int i = 0; i < n; ++i)
    {
        // modulators
        const double prevPhase = lfoPhase;
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) { lfoPhase -= 1.0; shLast = sh; sh = rng.nextBipolar(); }
        ignoreUnused (prevPhase);
        stepPhase += beatsPerSample / stepBeats;
        const int step = ((int) std::floor (stepPhase)) % numSteps;
        const float stepTarget = steps[jlimit (0, numSteps - 1, step)];
        stepValue = glideCoef <= 0.0f ? stepTarget : stepTarget + (stepValue - stepTarget) * glideCoef;

        float lvl = 0.0f;
        if (scEnv) for (int c = 0; c < sc.getNumChannels(); ++c) lvl = jmax (lvl, std::abs (sc.getSample (c, i)));
        else for (int c = 0; c < ch; ++c) lvl = jmax (lvl, std::abs (main.getSample (c, i)));
        const float env = jmin (1.0f, follower.process (lvl) * 2.0f);

        const float reso = resoSmooth.next();
        const float m = mixSmooth.next(), outG = outSmooth.next();
        const float common = baseOct + env * envAmt + stepValue * stepAmt + keyOct;

        float x[2] { main.getSample (0, i), ch > 1 ? main.getSample (1, i) : main.getSample (0, i) };
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = lfoValue (lfoPhase + (c == 1 ? phaseOffset : 0.0), shape);
            const float oct = jlimit (std::log2 (10.0f), maxOct, common + lfo * lfoDepth);
            cutSmooth[c].target = oct;
            const float cutHz = std::exp2 (cutSmooth[c].next());
            if (c == 0) { modCutoff.store (cutHz); }

            float in = x[c];
            if (driveDb > 0.05f && ! drivePost && ! useLadder) in = preSat[c].process (in * driveG) / std::sqrt (driveG);   // ADAA
            float y = in;
            const float g = dsp::SVF::gFor (cutHz, sr);
            const float k = 2.0f * (1.0f - reso * 0.98f);
            switch (type)
            {
                case lp: case hp: case bp: case notch: case allpass:
                {
                    float v = in;
                    if (steep && type != allpass)
                    {
                        svfA[c].process (v, g, 1.85f);
                        v = type == lp ? svfA[c].lp : type == hp ? svfA[c].hp : type == bp ? svfA[c].bp * 1.85f : v - 1.85f * svfA[c].bp;
                    }
                    svfB[c].process (v, g, k);
                    y = type == lp ? svfB[c].lp : type == hp ? svfB[c].hp : type == bp ? svfB[c].bp * k
                      : type == notch ? v - k * svfB[c].bp : v - 2.0f * k * svfB[c].bp;
                    break;
                }
                case combPos: case combNeg:
                {
                    const float fb = reso * 0.98f * (type == combPos ? 1.0f : -1.0f);
                    const float d = jlimit (2.0f, (float) (sr / 8.0), (float) sr / cutHz);
                    const float delayed = comb[c].tap (d);
                    const float v = in + fb * delayed;
                    comb[c].push (v);
                    y = v * (1.0f - std::abs (fb) * 0.5f);
                    break;
                }
                default: break;
            }
            if (! useLadder)
            {
                if (driveDb > 0.05f && drivePost) y = postSat[c].process (y * driveG) / std::sqrt (driveG);   // ADAA
                x[c] = x[c] * (1.0f - m) + y * m;
            }
            else ladderBuf.setSample (c, i, in);
        }
        if (! useLadder)
        {
            main.setSample (0, i, x[0] * outG);
            if (ch > 1) main.setSample (1, i, x[1] * outG);
        }
        else
        {
            // ladder runs per sub-block below; keep the mix and output gain for it
            ladderAux.setSample (0, i, m);
            ladderAux.setSample (1, i, outG);
        }
        if (i == n - 1) currentStep.store (step);
    }

    if (useLadder)
    {
        // ladder: sub-blocks of 32 samples with the modulated cutoff
        for (int s = 0; s < n; s += 32)
        {
            const int len = jmin (32, n - s);
            ladder.setCutoffFrequencyHz (jlimit (20.0f, (float) sr * 0.45f, modCutoff.load()));
            ladder.setResonance (jlimit (0.0f, 0.97f, resoSmooth.current));
            float* ptrs[2] { ladderBuf.getWritePointer (0, s), ladderBuf.getWritePointer (1, s) };
            juce::dsp::AudioBlock<float> blk (ptrs, 2, (size_t) len);
            ladder.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c)
            {
                const float m = ladderAux.getSample (0, i);
                main.setSample (c, i, (main.getSample (c, i) * (1.0f - m) + ladderBuf.getSample (c, i) * m) * ladderAux.getSample (1, i));
            }
    }
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    auto pattern = [] (std::initializer_list<float> v)
    {
        std::vector<std::pair<String, float>> out;
        int i = 1;
        for (float x : v) out.push_back ({ "step" + String (i++), x });
        return out;
    };
    auto join = [] (std::vector<std::pair<String, float>> a, std::vector<std::pair<String, float>> b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Barrido lento", "Movimiento", { { "cutoff", 600 }, { "reso", 45 }, { "lfodepth", 60 }, { "lfosync", 1 }, { "lfodiv", 0 } } },
        { "Respira", "Movimiento", { { "cutoff", 900 }, { "reso", 20 }, { "lfodepth", 35 }, { "lforate", 0.12f } } },
        { "Wah por envolvente", "Envolvente", { { "type", 2 }, { "cutoff", 400 }, { "reso", 70 }, { "envamt", 80 }, { "envatk", 3 }, { "envrel", 180 } } },
        { "Abre con el bombo", "Envolvente", { { "cutoff", 300 }, { "reso", 30 }, { "envamt", 70 }, { "envsrc", 1 } } },
        { "Gate rítmico 1/16"_u, "Pasos", join ({ { "cutoff", 250 }, { "reso", 25 }, { "stepamt", 60 }, { "stepdiv", 10 }, { "stepglide", 5 } },
                                                  pattern ({ 100, -100, 60, -100, 100, -60, 30, -100, 100, -100, 80, -20, 100, -100, 40, -100 })) },
        { "Escalera", "Pasos", join ({ { "cutoff", 500 }, { "reso", 55 }, { "stepamt", 50 }, { "stepdiv", 7 }, { "stepglide", 40 } },
                                     pattern ({ -100, -75, -50, -25, 0, 25, 50, 75, 100, 75, 50, 25, 0, -25, -50, -75 })) },
        { "Peine metálico"_u, "Color", { { "type", 5 }, { "cutoff", 440 }, { "reso", 85 }, { "mix", 60 } } },
        { "Filtro de teclado", "Color", { { "cutoff", 800 }, { "reso", 50 }, { "keytrack", 100 } } },
        { "Teléfono"_u, "Color", { { "type", 2 }, { "cutoff", 1600 }, { "reso", 35 }, { "drive", 12 } } },
        { "Dub", "Movimiento", { { "type", 7 }, { "cutoff", 700 }, { "reso", 75 }, { "drive", 8 }, { "lfodepth", 40 }, { "lfosync", 1 }, { "lfodiv", 5 } } },
        { "Quita graves", "Básicos"_u, { { "type", 1 }, { "cutoff", 150 }, { "reso", 10 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }

float Processor::responseDb (int type, int slope, float fc, float reso, float f) const
{
    using cd = std::complex<double>;
    const double w = f / fc;
    const cd s (0.0, w);
    const double k = 2.0 * (1.0 - reso * 0.98);
    auto den = [&] (double kk) { return s * s + kk * s + 1.0; };
    cd h (1.0, 0.0);
    switch (type)
    {
        case lp:      h = 1.0 / den (k); if (slope) h *= 1.0 / den (1.85); break;
        case hp:      h = (s * s) / den (k); if (slope) h *= (s * s) / den (1.85); break;
        case bp:      h = (k * s) / den (k); if (slope) h *= (1.85 * s) / den (1.85); break;
        case notch:   h = (s * s + 1.0) / den (k); if (slope) h *= (s * s + 1.0) / den (1.85); break;
        case allpass: h = 1.0; break;
        case combPos: case combNeg:
        {
            const double fb = reso * 0.98 * (type == combPos ? 1.0 : -1.0);
            const cd e = std::exp (cd (0.0, -MathConstants<double>::twoPi * w));
            h = (1.0 - std::abs (fb) * 0.5) / (1.0 - fb * e);
            break;
        }
        case ladderT:
        {
            const double r = 4.0 * jlimit (0.0, 0.97, (double) reso);
            h = slope ? 1.0 / (std::pow (1.0 + s, 4.0) + r) * (1.0 + r) : 1.0 / (std::pow (1.0 + s, 2.0) + r * 0.5) * (1.0 + r * 0.5);
            break;
        }
        default: break;
    }
    return (float) (20.0 * std::log10 (jmax (1.0e-6, std::abs (h))));
}
}

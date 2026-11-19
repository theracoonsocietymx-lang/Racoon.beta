#include "ChinchillaProcessor.h"
#include "ChinchillaEditor.h"

namespace racoon::chinchilla
{
using namespace juce;

const StringArray& Processor::irNames()
{
    static StringArray a { "Sala", "Cámara"_u, "Placa", "Habitación"_u, "Catedral", "Archivo propio" };
    return a;
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "algo", "Tipo", { "Algorítmica"_u, "Convolución"_u }, 0);
    P::choice (l, "ir", "Espacio (IR)", irNames(), 0);
    P::ms (l, "predelay", "Predelay", 0.0f, 250.0f, 15.0f, 40.0f);
    P::toggle (l, "presync", "Predelay a tempo", false);
    P::choice (l, "prediv", "Predelay división"_u, RacoonProcessor::divisionNames(), 13);
    P::flt (l, "rt60", "Duración"_u, P::range (0.1f, 60.0f, 2.5f), 2.2f, "s", 2);
    P::pct (l, "size", "Tamaño"_u, 0.0f, 200.0f, 100.0f);
    P::pct (l, "diffusion", "Difusión"_u, 0.0f, 100.0f, 75.0f);
    P::pct (l, "early", "Reflexiones", 0.0f, 100.0f, 40.0f);
    P::pct (l, "damping", "Amortiguación"_u, 0.0f, 100.0f, 45.0f);
    P::flt (l, "bassmult", "Graves", P::range (0.5f, 2.0f, 1.0f), 1.0f, "x", 2);
    P::hz (l, "hp", "Corte de graves", 20.0f, 1000.0f, 80.0f);
    P::hz (l, "lp", "Corte de agudos", 1000.0f, 20000.0f, 12000.0f);
    P::pct (l, "moddepth", "Modulación"_u, 0.0f, 100.0f, 25.0f);
    P::flt (l, "modrate", "Mod velocidad", P::range (0.05f, 5.0f, 0.8f), 0.6f, "Hz", 2);
    P::pct (l, "duck", "Ducking", 0.0f, 100.0f, 0.0f);
    P::pct (l, "width", "Anchura", 0.0f, 200.0f, 100.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 30.0f);
    P::toggle (l, "wetonly", "Solo efecto", false);
    P::toggle (l, "freeze", "Congelar", false);
    P::choice (l, "quality", "Calidad", { "ECO", "HQ" }, 1);
    return l;
}

// ===================================================================== IR builder (background)
class Processor::IRBuilder : public Thread
{
public:
    explicit IRBuilder (Processor& p) : Thread ("Chinchilla IR"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (80);
            if ((int) proc.value ("algo") != 1 || proc.sr <= 0.0) continue;
            const int kind = (int) proc.value ("ir");
            if (kind == 5)
            {
                const auto f = proc.getUserIR();
                if (f != lastFile && f.existsAsFile())
                {
                    lastFile = f;
                    lastHash = 0;
                    proc.conv.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                                                   juce::dsp::Convolution::Normalise::yes);
                }
                continue;
            }
            const int64 h = kind * 1000003 + roundToInt (proc.value ("rt60") * 100) * 7919 + roundToInt (proc.value ("size")) * 131
                          + roundToInt (proc.value ("damping")) * 17 + (int) proc.value ("quality") * 3 + (int64) proc.sr;
            if (h == lastHash) continue;
            // wait until the knob settles before building
            wait (150);
            const int64 h2 = kind * 1000003 + roundToInt (proc.value ("rt60") * 100) * 7919 + roundToInt (proc.value ("size")) * 131
                           + roundToInt (proc.value ("damping")) * 17 + (int) proc.value ("quality") * 3 + (int64) proc.sr;
            if (h2 != h) continue;
            lastHash = h;
            lastFile = File();
            auto ir = makeIR (kind, proc.sr, proc.value ("rt60"), proc.value ("size"), proc.value ("damping") * 0.01f, (int) proc.value ("quality") == 0);
            proc.conv.loadImpulseResponse (std::move (ir), proc.sr, juce::dsp::Convolution::Stereo::yes,
                                           juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
        }
    }
    Processor& proc;
    int64 lastHash = 0;
    File lastFile;
};

AudioBuffer<float> Processor::makeIR (int kind, double rate, float rt60, float sizePct, float damping, bool eco)
{
    // original impulse responses: decorrelated noise in three bands with their own decay, plus early reflections
    struct Char { float early, onsetMs, bright, density; };
    const Char chars[] { { 0.6f, 6.0f, 0.75f, 1.0f },    // sala
                         { 0.3f, 2.0f, 0.95f, 1.0f },    // cámara
                         { 0.0f, 0.6f, 1.0f, 1.0f },     // placa
                         { 0.9f, 1.5f, 0.7f, 0.8f },     // habitación
                         { 0.5f, 18.0f, 0.55f, 1.0f } }; // catedral
    const auto& c = chars[jlimit (0, 4, kind)];
    const float rt = jlimit (0.1f, eco ? 3.0f : 12.0f, rt60);
    const int len = (int) (rate * jmin (rt * 1.25f + 0.08f, eco ? 3.2f : 12.5f));
    AudioBuffer<float> ir (2, jmax (64, len));
    const float rtLow = rt * 1.15f, rtHigh = rt * jlimit (0.15f, 1.0f, c.bright * (1.0f - 0.75f * damping));
    const float sizeF = 0.3f + sizePct * 0.01f * 0.85f;
    Random rnd (kind * 7919 + 13);
    for (int ch = 0; ch < 2; ++ch)
    {
        float lp1 = 0.0f, lp2 = 0.0f;
        const float aLow = 1.0f - std::exp (-MathConstants<float>::twoPi * 300.0f / (float) rate);
        const float aHigh = 1.0f - std::exp (-MathConstants<float>::twoPi * 4000.0f / (float) rate);
        for (int i = 0; i < ir.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) rate;
            float n = rnd.nextFloat() * 2.0f - 1.0f;
            if (c.density < 1.0f && rnd.nextFloat() > c.density) n *= 0.2f;
            lp1 += aLow * (n - lp1);
            lp2 += aHigh * (n - lp2);
            const float low = lp1, mid = lp2 - lp1, high = n - lp2;
            const float onset = jmin (1.0f, t * 1000.0f / jmax (0.1f, c.onsetMs * sizeF));
            const float v = low * std::exp (-6.9078f * t / rtLow) * 1.6f + mid * std::exp (-6.9078f * t / rt) + high * std::exp (-6.9078f * t / rtHigh) * 0.8f;
            ir.setSample (ch, i, v * onset);
        }
        // early reflections
        const float times[] { 7, 11, 17, 23, 31, 41, 53, 67 };
        float g = c.early;
        for (int k = 0; k < 8; ++k)
        {
            const int at = (int) (times[k] * sizeF * 0.001f * (float) rate) + (ch == 1 ? 37 : 0);
            if (at < ir.getNumSamples()) ir.addSample (ch, at, g * ((k + ch) % 2 ? -1.0f : 1.0f));
            g *= 0.8f;
        }
    }
    return ir;
}

// ===================================================================== processor
Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Reverb"_u, "Reverb"_u, Colour (0xff5cf2e8), PluginKind::effect, 1, 20.0, true, true },
                       layout())
{
}

Processor::~Processor()
{
    if (builder) builder->stopThread (2000);
}

void Processor::prepareDsp (double rate, int block)
{
    // delay lengths in ms for 16 lines (the first 8 are used in ECO); mutually prime in samples at 48 kHz
    const float ms[maxLines] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f,
                               31.3f, 47.9f, 61.3f, 71.9f, 79.7f, 83.9f, 89.3f, 97.1f };
    for (int i = 0; i < maxLines; ++i)
    {
        baseLen[i] = ms[i] * 0.001f * (float) rate;
        lines[i].prepare ((int) (baseLen[i] * 2.1f) + 64);
    }
    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].prepare ((int) (rate * 0.26) + 8);
        early[c].prepare ((int) (rate * 0.2) + 8);
        const int lens[4] { 142, 107, 379, 277 };
        for (int k = 0; k < 4; ++k)
        {
            inDiff[c][k].len = (int) ((lens[k] + c * 23) * rate / 48000.0);
            inDiff[c][k].d.prepare (inDiff[c][k].len + 4);
        }
    }
    sizeSmooth.prepare (rate, 200.0);
    mixSmooth.prepare (rate, 30.0);
    juce::dsp::ProcessSpec spec { rate, (uint32) block, 2 };
    conv.prepare (spec);
    convBuf.setSize (2, block);
    if (builder == nullptr) builder = std::make_unique<IRBuilder> (*this);
    builder->lastHash = 0;
    builder->lastFile = File();
    if (! builder->isThreadRunning()) builder->startThread();
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& l : lines) l.clear();
    for (auto& s : lowState) s = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        preDelay[c].clear(); early[c].clear();
        for (auto& a : inDiff[c]) a.d.clear();
        hp[c].reset(); lp[c].reset();
    }
    duck.env = 0.0f;
    sizeSmooth.reset (value ("size"));
    mixSmooth.reset (value ("mix") * 0.01f);
    conv.reset();
}

static inline void hadamard (float* v, int n)
{
    for (int h = 1; h < n; h <<= 1)
        for (int i = 0; i < n; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = v[j], b = v[j + h];
                v[j] = a + b; v[j + h] = a - b;
            }
    const float s = 1.0f / std::sqrt ((float) n);
    for (int i = 0; i < n; ++i) v[i] *= s;
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    if (clearRequest.exchange (false)) resetDsp();
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    const bool convMode = (int) value ("algo") == 1;
    const bool freeze = value ("freeze") > 0.5f && ! convMode;
    const int N = (int) value ("quality") == 0 ? 8 : 16;
    const float rt = value ("rt60");
    const float damping = value ("damping") * 0.01f;
    const float bass = value ("bassmult");
    const float diffG = 0.75f * value ("diffusion") * 0.01f;
    for (auto& cd : inDiff) for (auto& a : cd) a.g = diffG;
    const float earlyLvl = value ("early") * 0.01f;
    const float modDepth = value ("moddepth") * 0.01f * 12.0f * (float) (sr / 48000.0);
    const double modInc = value ("modrate") / sr;
    float pre = value ("predelay") * 0.001f;
    if (value ("presync") > 0.5f) pre = (float) jmin (0.25, syncSeconds ((int) value ("prediv"), bpm()));
    const float preSamples = jlimit (2.0f, (float) (sr * 0.25), pre * (float) sr);
    const float gHp = dsp::SVF::gFor (value ("hp"), sr), gLp = dsp::SVF::gFor (value ("lp"), sr);
    const bool useHp = value ("hp") > 21.0f, useLp = value ("lp") < 19900.0f;
    const float duckAmt = value ("duck") * 0.01f;
    duck.set (sr, 5.0f, 300.0f);
    const float width = value ("width") * 0.01f;
    const bool wetOnly = value ("wetonly") > 0.5f;
    mixSmooth.target = value ("mix") * 0.01f;
    sizeSmooth.target = value ("size");

    // per-block line gains (frequency dependent decay)
    const float sizeF = 0.25f + sizeSmooth.current * 0.01f * 0.875f;
    float gLow[maxLines], gHigh[maxLines], len[maxLines];
    for (int i = 0; i < N; ++i)
    {
        len[i] = baseLen[i] * sizeF;
        const float secs = len[i] / (float) sr;
        gLow[i] = freeze ? 1.0f : std::pow (10.0f, -3.0f * secs / (rt * bass));
        gHigh[i] = freeze ? 1.0f : std::pow (10.0f, -3.0f * secs / (rt / (1.0f + 3.0f * damping)));
    }
    const float aCross = 1.0f - std::exp (-MathConstants<float>::twoPi * 500.0f / (float) sr);
    const float inScale = 1.0f / std::sqrt ((float) N * 0.5f);
    static const float earlyMs[12] { 4.3f, 7.9f, 11.3f, 15.7f, 19.1f, 23.9f, 29.3f, 34.1f, 41.3f, 47.9f, 55.1f, 63.7f };

    // convolution path first (block based)
    if (convMode)
    {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                preDelay[c].push (buffer.getSample (jmin (c, ch - 1), i));
                convBuf.setSample (c, i, preDelay[c].tap (preSamples));
            }
        juce::dsp::AudioBlock<float> blk (convBuf.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }

    float v[maxLines];
    float wetPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float inL = buffer.getSample (0, i), inR = ch > 1 ? buffer.getSample (1, i) : inL;
        float wet[2] {};
        if (convMode)
        {
            wet[0] = convBuf.getSample (0, i) * 0.5f;
            wet[1] = convBuf.getSample (1, i) * 0.5f;
        }
        else
        {
            preDelay[0].push (freeze ? 0.0f : inL);
            preDelay[1].push (freeze ? 0.0f : inR);
            float x[2] { preDelay[0].tap (preSamples), preDelay[1].tap (preSamples) };

            // early reflections from the predelayed input
            float er[2] {};
            for (int c = 0; c < 2; ++c) early[c].push (x[c]);
            if (earlyLvl > 0.001f)
            {
                float g = 0.7f;
                for (int k = 0; k < 12; ++k)
                {
                    const float d = earlyMs[k] * sizeF * 0.001f * (float) sr;
                    const int side = k % 2;
                    er[side] += early[k % 2 == 0 ? 0 : 1].tap (d) * g;
                    g *= 0.86f;
                }
            }

            // input diffusion
            for (int c = 0; c < 2; ++c)
                for (auto& a : inDiff[c]) x[c] = a.process (x[c]);

            // read the network
            modPhase += modInc;
            if (modPhase >= 1.0) modPhase -= 1.0;
            for (int k = 0; k < N; ++k)
            {
                float d = len[k];
                if (k < 4 && modDepth > 0.0f)
                    d += modDepth * (float) std::sin (MathConstants<double>::twoPi * (modPhase + k * 0.25));
                float s = lines[k].tap (d);
                lowState[k] += aCross * (s - lowState[k]);
                const float hi = s - lowState[k];
                v[k] = lowState[k] * gLow[k] + hi * gHigh[k];
            }
            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < N; k += 2) { outL += v[k] * ((k & 2) ? -1.0f : 1.0f); outR += v[k + 1] * ((k & 4) ? -1.0f : 1.0f); }
            hadamard (v, N);
            for (int k = 0; k < N; ++k)
            {
                float w = v[k] + x[k & 1] * inScale;
                if (freeze && std::abs (w) > 4.0f) w = std::tanh (w * 0.25f) * 4.0f;   // freeze safety
                lines[k].push (w);
            }
            wet[0] = outL * inScale * 1.25f + er[0] * earlyLvl * 0.6f;
            wet[1] = outR * inScale * 1.25f + er[1] * earlyLvl * 0.6f;
        }

        for (int c = 0; c < 2; ++c)
        {
            if (useHp) { hp[c].process (wet[c], gHp, 1.414f); wet[c] = hp[c].hp; }
            if (useLp) { lp[c].process (wet[c], gLp, 1.414f); wet[c] = lp[c].lp; }
        }
        const float env = duck.process (jmax (std::abs (inL), std::abs (inR)));
        const float dg = 1.0f - duckAmt * jmin (1.0f, env * 4.0f);
        const float mid = (wet[0] + wet[1]) * 0.5f, side = (wet[0] - wet[1]) * 0.5f * width;
        const float wl = (mid + side) * dg, wr = (mid - side) * dg;
        const float m = mixSmooth.next();
        const float dryG = wetOnly ? 0.0f : jmin (1.0f, 2.0f * (1.0f - m));
        const float wetG = wetOnly ? 1.0f : jmin (1.0f, 2.0f * m);
        buffer.setSample (0, i, inL * dryG + wl * wetG);
        if (ch > 1) buffer.setSample (1, i, inR * dryG + wr * wetG);
        wetPeak = jmax (wetPeak, std::abs (wl), std::abs (wr));
        sizeSmooth.next();
    }
    wetLevel.store (jmax (wetPeak, wetLevel.load() * 0.92f));
}

void Processor::saveExtraState (ValueTree& v) const
{
    v.setProperty ("ir", getUserIR().getFullPathName(), nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    const String path = v.getProperty ("ir", {}).toString();
    const ScopedLock sl (irLock);
    userIR = path.isNotEmpty() ? File (path) : File();
}

File Processor::getUserIR() const
{
    const ScopedLock sl (irLock);
    return userIR;
}

void Processor::loadUserIR (const File& f)
{
    {
        const ScopedLock sl (irLock);
        userIR = f;
    }
    setRealValue ("algo", 1.0f);
    setRealValue ("ir", 5.0f);
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Sala mediana", "Salas", { { "rt60", 1.8f }, { "size", 100 }, { "predelay", 20 }, { "damping", 45 } } },
        { "Sala grande", "Salas", { { "rt60", 3.2f }, { "size", 150 }, { "predelay", 35 }, { "damping", 50 }, { "early", 50 } } },
        { "Habitación"_u, "Salas", { { "rt60", 0.6f }, { "size", 40 }, { "predelay", 5 }, { "early", 70 }, { "damping", 60 } } },
        { "Placa brillante", "Placas", { { "rt60", 2.0f }, { "size", 80 }, { "early", 0 }, { "diffusion", 95 }, { "damping", 15 }, { "lp", 16000 } } },
        { "Placa en voces", "Placas", { { "rt60", 1.5f }, { "size", 70 }, { "early", 0 }, { "predelay", 60 }, { "duck", 40 }, { "hp", 200 } } },
        { "Catedral", "Grandes", { { "rt60", 7.5f }, { "size", 200 }, { "predelay", 50 }, { "damping", 60 }, { "bassmult", 1.3f } } },
        { "Cola infinita", "Grandes", { { "rt60", 25.0f }, { "size", 190 }, { "damping", 35 }, { "moddepth", 60 }, { "mix", 40 } } },
        { "Retorno 100 %", "Retornos", { { "wetonly", 1 }, { "rt60", 2.4f }, { "predelay", 25 }, { "hp", 150 } } },
        { "Cámara de convolución"_u, "Convolución"_u, { { "algo", 1 }, { "ir", 1 }, { "rt60", 1.4f } } },
        { "Catedral de convolución"_u, "Convolución"_u, { { "algo", 1 }, { "ir", 4 }, { "rt60", 6.0f } } },
        { "Reverb gate corta", "Creativos", { { "rt60", 0.35f }, { "size", 60 }, { "diffusion", 100 }, { "early", 80 }, { "mix", 35 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

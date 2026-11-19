#include "SkunkProcessor.h"
#include "SkunkEditor.h"

namespace racoon::skunk
{
using namespace juce;

const StringArray& Processor::modeNames()
{
    static StringArray a { "Soft clip", "Hard clip", "Waveshaper", "Diodo", "Foldback", "Cinta", "Válvula"_u, "Rectificador", "Plegado seno" };
    return a;
}

float Processor::shape (int mode, float x)
{
    switch (mode)
    {
        case 0: return std::tanh (x);
        case 1: return jlimit (-1.0f, 1.0f, x);
        case 2: return 0.63662f * std::atan (x * 1.5708f);
        case 3: return x >= 0.0f ? 1.0f - std::exp (-x) : -0.6f * (1.0f - std::exp (1.6f * x));
        case 4: { const float t = 0.25f * x + 0.25f; return 4.0f * std::abs (t - std::floor (t + 0.5f)) - 1.0f; }
        case 5: return x / std::sqrt (1.0f + x * x);
        case 6: { const float t = std::tanh (x); return 0.8f * t + 0.3f * t * t; }   // tube: strong second harmonic
        case 7: return x >= 0.0f ? std::tanh (x) : 0.15f * std::tanh (x);          // rectifier: the lower half almost gone
        case 8: return std::sin (x);                                                 // sine fold: folds back over and over
        default: return x;
    }
}

float Processor::shapeBiased (int mode, float x, float bias)
{
    return shape (mode, x + bias) - shape (mode, bias);
}

// the antiderivative of each curve (for ADAA); foldback has none here and goes without
double Processor::shapeIntegral (int mode, double x)
{
    using racoon::sdsp::curve::logCosh;
    switch (mode)
    {
        case 0: return logCosh (x);
        case 1: { const double a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
        case 2: { const double u = x * 1.5708; return 0.63662 / 1.5708 * (u * std::atan (u) - 0.5 * std::log1p (u * u)); }
        case 3: return x >= 0.0 ? x + std::exp (-x) - 1.0 : -0.6 * x + 0.375 * (std::exp (1.6 * x) - 1.0);
        case 5: return std::sqrt (1.0 + x * x);
        case 6: return 0.8 * logCosh (x) + 0.3 * (x - std::tanh (x));
        case 7: return x >= 0.0 ? logCosh (x) : 0.15 * logCosh (x);
        case 8: return -std::cos (x);
        default: return 0.0;
    }
}

// first-order ADAA of shapeBiased: the average of the curve between this sample and the last one
float Processor::shapeAdaa (int mode, float x, float bias, int c)
{
    const double u = (double) x + bias;
    if (mode != adaaMode[c]) { adaaMode[c] = mode; adaaValid[c] = false; }   // another curve: its antiderivative starts fresh
    if (mode == 4) { adaaX[c] = u; adaaF[c] = 0.0; adaaValid[c] = false; return shapeBiased (mode, x, bias); }
    const double Fu = shapeIntegral (mode, u);
    double y;
    const double d = u - adaaX[c];
    if (adaaValid[c] && std::abs (d) > 1.0e-5) y = (Fu - adaaF[c]) / d;
    else y = shape (mode, (float) (adaaValid[c] ? 0.5 * (u + adaaX[c]) : u));
    adaaX[c] = u; adaaF[c] = Fu; adaaValid[c] = true;
    return (float) y - shape (mode, bias);
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Motor", modeNames(), 0);
    P::flt (l, "drive", "Drive", P::range (0.0f, 48.0f, 12.0f), 12.0f, "dB");
    P::pct (l, "bias", "Bias", -100.0f, 100.0f, 0.0f);
    P::hz (l, "tone", "Tono", 1000.0f, 20000.0f, 20000.0f);
    P::hz (l, "lowcut", "Corte de graves", 20.0f, 1000.0f, 20.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
    P::db (l, "intrim", "Entrada", -24.0f, 24.0f, 0.0f);
    P::db (l, "outtrim", "Salida", -24.0f, 24.0f, 0.0f);
    P::toggle (l, "autogain", "Nivel automático"_u, true);
    P::choice (l, "os", "Oversampling", { "OFF", "2x", "4x", "8x" }, 1);
    P::toggle (l, "hq", "HQ al exportar", true);
    P::choice (l, "stereo", "Estéreo"_u, { "L / R", "Mid / Side" }, 0);
    P::pct (l, "sidedrive", "Drive de lados", 0.0f, 200.0f, 100.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Saturador"_u, "Distorsión"_u, Colour (0xffe0789a), PluginKind::effect, 1, 0.0, false, true },
                       layout())
{
}

int Processor::osIndexWanted() const
{
    int k = (int) value ("os");
    if (isNonRealtime() && value ("hq") > 0.5f) k = 3;
    return jlimit (0, 3, k);
}

void Processor::prepareDsp (double rate, int block)
{
    for (int k = 0; k < 4; ++k)
    {
        os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) k, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os[k]->initProcessing ((size_t) block);
    }
    work.setSize (2, block);
    for (auto& d : dryLine) d.prepare ((int) (rate * 0.05) + block + 16);
    for (auto* s : { &drive, &mix, &inTrim, &outTrim }) s->prepare (rate, 15.0);
    for (auto& d : dc) d.prepare (rate);
    activeOs = pendingOs = osIndexWanted();
    fadingOut = false; fade = 0;
    fadeLen = (int) (rate * 0.006);
    setLatencySamples ((int) std::round (os[activeOs]->getLatencyInSamples()));
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& o : os) if (o) o->reset();
    for (int c = 0; c < 2; ++c) { pre[c].reset(); post[c].reset(); dc[c].reset(); adaaX[c] = adaaF[c] = 0.0; adaaValid[c] = false; }
    drive.reset (dsp::dbToGain (value ("drive")));
    mix.reset (value ("mix") * 0.01f);
    inTrim.reset (dsp::dbToGain (value ("intrim")));
    outTrim.reset (dsp::dbToGain (value ("outtrim")));
    autoGain = 1.0f;
    for (auto& d : dryLine) d.clear();
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    if (n > work.getNumSamples()) return;   // never happens: blocks are prepared to the maximum

    // oversampling change: fade out, switch at a block boundary, wait for the new filters to fill, fade back in
    const int want = osIndexWanted();
    if (renderModeChanged.exchange (false) && want != activeOs)
    {
        activeOs = pendingOs = want;
        os[activeOs]->reset();
        setLatencySamples ((int) std::round (os[activeOs]->getLatencyInSamples()));
        fadingOut = switchNow = false; fade = 0; fadeInDelay = 0;
    }
    if (switchNow)
    {
        switchNow = false;
        activeOs = pendingOs;
        os[activeOs]->reset();
        setLatencySamples ((int) std::round (os[activeOs]->getLatencyInSamples()));
        fadeInDelay = (int) std::round (os[activeOs]->getLatencyInSamples()) + 2;
        fade = -fadeLen;
    }
    if (want != activeOs && ! fadingOut && ! switchNow && fade >= 0) { fadingOut = true; fade = fadeLen; pendingOs = want; }

    const int mode = (int) value ("mode");
    const float bias = value ("bias") * 0.01f * 0.8f;
    const bool ms = (int) value ("stereo") == 1 && ch == 2;
    const float sideScale = value ("sidedrive") * 0.01f;
    drive.target = dsp::dbToGain (value ("drive"));
    mix.target = value ("mix") * 0.01f;
    inTrim.target = dsp::dbToGain (value ("intrim"));
    outTrim.target = dsp::dbToGain (value ("outtrim"));
    const float lowcut = value ("lowcut"), tone = value ("tone");
    const float gPre = dsp::SVF::gFor (lowcut, sr);
    const float toneHz = mode == 5 ? jmin (tone, 14000.0f) : tone;     // tape rolls off the top
    const float gPost = dsp::SVF::gFor (toneHz, sr);
    const bool autoLevel = value ("autogain") > 0.5f;
    const int latency = (int) std::round (os[activeOs]->getLatencyInSamples());

    // input stage at the base rate: trim, M/S, low cut; keep a latency-aligned dry copy for the mix
    float inSq = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float t = inTrim.next();
        float x[2] { buffer.getSample (0, i) * t, (ch > 1 ? buffer.getSample (1, i) : buffer.getSample (0, i)) * t };
        for (int c = 0; c < 2; ++c) { dryLine[c].push (buffer.getSample (jmin (c, ch - 1), i)); }
        if (ms) { const float m = (x[0] + x[1]) * 0.7071f, s = (x[0] - x[1]) * 0.7071f; x[0] = m; x[1] = s; }
        for (int c = 0; c < 2; ++c)
        {
            if (lowcut > 21.0f) { pre[c].process (x[c], gPre, 1.414f); x[c] = pre[c].hp; }
            work.setSample (c, i, x[c]);
            inSq += x[c] * x[c];
        }
    }

    // nonlinear stage, oversampled
    juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, (size_t) n);
    auto up = os[activeOs]->processSamplesUp (block);
    const int un = (int) up.getNumSamples();
    for (int i = 0; i < un; ++i)
    {
        const float d = drive.current + (drive.target - drive.current) * ((float) i / (float) un);
        for (int c = 0; c < 2; ++c)
        {
            auto* s = up.getChannelPointer ((size_t) c);
            const float dd = (ms && c == 1) ? 1.0f + (d - 1.0f) * sideScale : d;
            s[i] = shapeAdaa (mode, s[i] * dd, bias, c);   // ADAA on top of the oversampling
        }
    }
    drive.reset (drive.target);
    os[activeOs]->processSamplesDown (block);

    // output stage at the base rate
    float outSq = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            float v = work.getSample (c, i);
            if (toneHz < 19900.0f) { post[c].process (v, gPost, 1.0f); v = post[c].lp; }
            y[c] = dc[c].process (v);
        }
        if (ms) { const float l = (y[0] + y[1]) * 0.7071f, r = (y[0] - y[1]) * 0.7071f; y[0] = l; y[1] = r; }
        outSq += y[0] * y[0] + y[1] * y[1];
        work.setSample (0, i, y[0]);
        work.setSample (1, i, y[1]);
    }

    // automatic level: match the loudness of what went in (slow, so it never pumps)
    if (autoLevel && outSq > 1.0e-9f && inSq > 1.0e-9f)
    {
        const float target = jlimit (dsp::dbToGain (-24.0f), dsp::dbToGain (6.0f), std::sqrt (inSq / outSq));
        const float k = 1.0f - std::exp (-(float) n / (float) (sr * 0.4));
        autoGain += (target - autoGain) * k;
    }
    else if (! autoLevel) autoGain = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float m = mix.next(), o = outTrim.next();
        float fadeGain = 1.0f;
        if (fadingOut)
        {
            fadeGain = (float) jmax (0, fade) / (float) fadeLen;
            if (--fade <= 0) { fade = 0; fadeGain = 0.0f; if (i == n - 1) { fadingOut = false; switchNow = true; } }
        }
        else if (switchNow) fadeGain = 0.0f;
        else if (fadeInDelay > 0) { fadeGain = 0.0f; --fadeInDelay; }
        else if (fade < 0) { fadeGain = 1.0f - (float) (-fade) / (float) fadeLen; ++fade; }

        for (int c = 0; c < ch; ++c)
        {
            const float dryV = dryLine[c].tapInt (latency + 1 + (n - 1 - i));
            const float wet = work.getSample (c, i) * autoGain;
            buffer.setSample (c, i, (dryV * (1.0f - m) + wet * m) * o * fadeGain);
        }
        if (++scopeDecim >= 2)
        {
            scopeDecim = 0;
            const int p = scopePos.load();
            scopeIn[(size_t) p].store (dryLine[0].tapInt (latency + 1 + (n - 1 - i)));
            scopeOut[(size_t) p].store (buffer.getSample (0, i));
            scopePos.store ((p + 1) % scopeSize);
        }
    }
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Calidez de cinta", "Suaves", { { "mode", 5 }, { "drive", 8 }, { "tone", 14000 }, { "mix", 100 } } },
        { "Válvula suave"_u, "Suaves", { { "mode", 6 }, { "drive", 10 }, { "bias", 15 } } },
        { "Brillo de bus", "Suaves", { { "mode", 0 }, { "drive", 6 }, { "mix", 50 }, { "lowcut", 120 } } },
        { "Bajo gordo", "Graves", { { "mode", 6 }, { "drive", 18 }, { "tone", 6000 }, { "mix", 70 } } },
        { "Bajo en M/S", "Graves", { { "mode", 0 }, { "drive", 14 }, { "stereo", 1 }, { "sidedrive", 40 } } },
        { "Batería rota"_u, "Batería"_u, { { "mode", 1 }, { "drive", 24 }, { "mix", 45 }, { "os", 2 } } },
        { "Diodo sucio", "Fuertes", { { "mode", 3 }, { "drive", 26 }, { "bias", 30 }, { "tone", 9000 } } },
        { "Lo-fi doblado", "Fuertes", { { "mode", 4 }, { "drive", 20 }, { "tone", 7000 }, { "mix", 60 } } },
        { "Destrucción"_u, "Fuertes", { { "mode", 4 }, { "drive", 40 }, { "bias", 40 }, { "os", 3 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

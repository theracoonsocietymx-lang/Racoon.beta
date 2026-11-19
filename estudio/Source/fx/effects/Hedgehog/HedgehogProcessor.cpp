#include "HedgehogProcessor.h"
#include "HedgehogEditor.h"

namespace racoon::hedgehog
{
using namespace juce;

enum Mode { vca, opto, fet, glue, upward, parallel };

const StringArray& Processor::modeNames()
{
    static StringArray a { "VCA limpio", "Opto suave", "FET rápido"_u, "Bus glue", "Upward", "Paralelo" };
    return a;
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", modeNames(), 0);
    P::flt (l, "thresh", "Umbral", P::range (-80.0f, 6.0f), -18.0f, "dB");
    P::flt (l, "ratio", "Ratio", P::range (1.0f, 30.0f, 4.0f), 4.0f, ": 1");
    P::flt (l, "knee", "Rodilla", P::range (0.0f, 24.0f), 6.0f, "dB");
    P::ms (l, "attack", "Ataque", 0.01f, 200.0f, 10.0f, 10.0f);
    P::ms (l, "release", "Relajación"_u, 5.0f, 5000.0f, 150.0f, 200.0f);
    P::ms (l, "hold", "Hold", 0.0f, 500.0f, 0.0f, 50.0f);
    P::ms (l, "look", "Lookahead", 0.0f, 20.0f, 0.0f, 5.0f);
    P::db (l, "makeup", "Ganancia", -24.0f, 24.0f, 0.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
    P::choice (l, "detector", "Detector", { "Pico", "RMS" }, 0);
    P::ms (l, "rms", "Ventana RMS", 1.0f, 100.0f, 20.0f, 20.0f);
    P::choice (l, "topology", "Topología"_u, { "Feedforward", "Feedback" }, 0);
    P::toggle (l, "autorel", "Relajación auto"_u, false);
    P::toggle (l, "scext", "Sidechain externo", false);
    P::hz (l, "schp", "HP del detector", 20.0f, 500.0f, 20.0f);
    P::pct (l, "link", "Enlace estéreo"_u, 0.0f, 100.0f, 100.0f);
    P::choice (l, "stereo", "Estéreo"_u, { "L / R", "Mid / Side" }, 0);
    P::toggle (l, "automakeup", "Ganancia automática"_u, false);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties()
                           .withInput ("Input", AudioChannelSet::stereo(), true)
                           .withOutput ("Output", AudioChannelSet::stereo(), true)
                           .withInput ("Sidechain", AudioChannelSet::stereo(), false),
                       PluginInfo { "Compresor"_u, "Dinámica"_u, Colour (0xff7fbfa8), PluginKind::effect, 1, 0.0, false, true },
                       layout())
{
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

float Processor::gainComputer (float level, float thr, float ratio, float knee, bool up)
{
    const float slope = 1.0f / jmax (1.0f, ratio) - 1.0f;     // <= 0
    const float over = level - thr;
    if (up)
    {
        // upward: quiet material (below the threshold) is raised, never more than 24 dB, never the noise floor
        if (level < -90.0f || over >= 0.0f) return 0.0f;
        const float boost = -slope * (-over);
        return jmin (24.0f, boost);
    }
    if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
        return slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
    if (over <= 0.0f) return 0.0f;
    return slope * over;
}

void Processor::prepareDsp (double rate, int block)
{
    for (auto& d : look) d.prepare ((int) (rate * 0.021) + 8);
    scBuf.setSize (2, block);
    resetDsp();
}

void Processor::resetDsp()
{
    for (int c = 0; c < 2; ++c)
    {
        fetSat[c].reset();
        look[c].clear();
        scFilter[c].reset();
        rms[c] = gainDb[c] = prevOut[c] = 0.0f;
        holdCount[c] = 0;
    }
    slowEnv = 0.0f;
    lookSamples = (int) std::round (value ("look") * 0.001 * sr);
    setLatencySamples (lookSamples);
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    auto main = getBusBuffer (buffer, false, 0);

    const int wantLook = (int) std::round (value ("look") * 0.001 * sr);
    if (wantLook != lookSamples) { lookSamples = wantLook; setLatencySamples (lookSamples); }

    int mode = (int) value ("mode");
    const float thr = value ("thresh"), knee = value ("knee");
    float ratio = value ("ratio");
    float attMs = value ("attack"), relMs = value ("release");
    const float holdMs = value ("hold");
    float makeup = value ("makeup");
    const float mix = value ("mix") * 0.01f;
    bool useRms = (int) value ("detector") == 1;
    const bool feedback = (int) value ("topology") == 1;
    bool autoRel = value ("autorel") > 0.5f;
    const float link = value ("link") * 0.01f;
    const bool midSide = (int) value ("stereo") == 1 && ch == 2;
    const float scHp = value ("schp");

    if (mode == glue) { useRms = true; autoRel = true; attMs = jmax (attMs, 0.3f); }
    if (mode == fet)  { attMs *= 0.35f; relMs *= 0.6f; }
    if (mode == opto) { attMs = jmax (attMs, 10.0f); }
    // parallel (New York): a copy crushed hard and added under the untouched signal
    const bool par = mode == parallel;
    if (par) { ratio = jmax (ratio * 2.0f, 10.0f); attMs = jmin (attMs, 2.0f); }
    const bool up = mode == upward;
    // automatic makeup: give back about half of what the curve takes at 0 dB, the usual estimate
    if (value ("automakeup") > 0.5f && ! up) makeup += jmax (0.0f, -gainComputer (0.0f, thr, ratio, knee, false) * 0.5f);

    const float aCoef = (float) std::exp (-1.0 / (sr * jmax (0.01f, attMs) * 0.001));
    const float rmsCoef = (float) std::exp (-1.0 / (sr * value ("rms") * 0.001));
    const float slowCoef = (float) std::exp (-1.0 / (sr * 0.8));
    const int holdSamples = (int) (holdMs * 0.001 * sr);
    const float g = dsp::SVF::gFor (scHp, sr);

    // external sidechain, if the host connected one and the switch is on
    const bool ext = value ("scext") > 0.5f && getBusCount (true) > 1 && getChannelCountOfBus (true, 1) > 0;
    AudioBuffer<float> sc;
    if (ext) sc = getBusBuffer (buffer, true, 1);

    float minGr = 0.0f, maxLevel = -100.0f;
    for (int i = 0; i < n; ++i)
    {
        float x[2] { main.getSample (0, i), ch > 1 ? main.getSample (1, i) : main.getSample (0, i) };
        if (midSide) { const float m = (x[0] + x[1]) * 0.7071f, s = (x[0] - x[1]) * 0.7071f; x[0] = m; x[1] = s; }

        float det[2];
        for (int c = 0; c < 2; ++c)
        {
            float d = x[c];
            if (ext) d = sc.getSample (jmin (c, sc.getNumChannels() - 1), i);
            if (feedback) d = prevOut[c];
            if (scHp > 21.0f) { scFilter[c].process (d, g, 1.414f); d = scFilter[c].hp; }
            if (useRms) { rms[c] = rms[c] * rmsCoef + d * d * (1.0f - rmsCoef); det[c] = std::sqrt (rms[c]) * 1.4142f; }
            else det[c] = std::abs (d);
        }
        if (! midSide)
        {
            const float linked = jmax (det[0], det[1]);
            for (auto& d : det) d += (linked - d) * link;
        }

        for (int c = 0; c < 2; ++c)
        {
            const float lvl = dsp::gainToDb (det[c]);
            maxLevel = jmax (maxLevel, lvl);
            const float target = gainComputer (lvl, thr, ratio, knee, up);
            const bool moreChange = std::abs (target) > std::abs (gainDb[c]);
            if (moreChange)
            {
                gainDb[c] = target + (gainDb[c] - target) * aCoef;
                holdCount[c] = holdSamples;
            }
            else if (holdCount[c] > 0) --holdCount[c];
            else
            {
                float r = relMs;
                if (mode == opto) r *= 0.5f + std::abs (gainDb[c]) / 12.0f;          // deeper reduction, slower return
                if (autoRel) r *= 0.3f + 0.7f * jmin (1.0f, std::abs (slowEnv) / 6.0f); // transients recover fast
                const float rCoef = (float) std::exp (-1.0 / (sr * jmax (1.0f, r) * 0.001));
                gainDb[c] = target + (gainDb[c] - target) * rCoef;
            }
        }
        const float grNow = up ? jmax (gainDb[0], gainDb[1]) : jmin (gainDb[0], gainDb[1]);
        slowEnv = grNow + (slowEnv - grNow) * slowCoef;
        if (std::abs (grNow) > std::abs (minGr)) minGr = grNow;

        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            look[c].push (x[c]);
            const float delayed = lookSamples > 0 ? look[c].tapInt (lookSamples + 1) : x[c];
            float wet = delayed * dsp::dbToGain (jmin (30.0f, gainDb[c] + makeup));   // upward plus makeup never past +30 dB
            if (mode == fet) wet = fetSat[c].process (wet * 1.1f) / 1.1f;   // ADAA
            prevOut[c] = wet;
            y[c] = par ? delayed + wet * mix * 0.8f : delayed * (1.0f - mix) + wet * mix;
        }
        if (midSide) { const float l = (y[0] + y[1]) * 0.7071f, r = (y[0] - y[1]) * 0.7071f; y[0] = l; y[1] = r; }
        main.setSample (0, i, y[0]);
        if (ch > 1) main.setSample (1, i, y[1]);

        // display history, about 60 points per second
        histGr = jmin (histGr, up ? -grNow : grNow);
        histIn = jmax (histIn, jmax (dsp::gainToDb (std::abs (x[0])), dsp::gainToDb (std::abs (x[1]))));
        if (++historyCounter >= (int) (sr / 60.0))
        {
            historyCounter = 0;
            const int p = historyPos.load();
            grHistory[(size_t) p].store (histGr);
            inHistory[(size_t) p].store (histIn);
            historyPos.store ((p + 1) % historySize);
            histGr = 0.0f; histIn = -100.0f;
        }
    }
    grDb.store (minGr);
    levelDb.store (maxLevel);
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Voz suave", "Voces", { { "mode", 1 }, { "thresh", -22 }, { "ratio", 3 }, { "knee", 10 }, { "attack", 12 }, { "release", 220 }, { "makeup", 4 } } },
        { "Voz al frente", "Voces", { { "mode", 0 }, { "thresh", -26 }, { "ratio", 6 }, { "knee", 4 }, { "attack", 3 }, { "release", 90 }, { "makeup", 7 } } },
        { "Bombo pegado", "Batería"_u, { { "mode", 2 }, { "thresh", -16 }, { "ratio", 5 }, { "knee", 2 }, { "attack", 18 }, { "release", 70 }, { "makeup", 3 } } },
        { "Bus de batería"_u, "Batería"_u, { { "mode", 3 }, { "thresh", -14 }, { "ratio", 2 }, { "knee", 8 }, { "attack", 10 }, { "release", 120 }, { "makeup", 2 }, { "schp", 90 } } },
        { "Paralelo NY", "Batería"_u, { { "mode", 5 }, { "thresh", -30 }, { "ratio", 10 }, { "knee", 0 }, { "attack", 1 }, { "release", 60 }, { "makeup", 10 }, { "mix", 40 } } },
        { "Sidechain al bombo", "Sidechain", { { "mode", 0 }, { "thresh", -30 }, { "ratio", 8 }, { "knee", 0 }, { "attack", 0.5f }, { "release", 160 }, { "scext", 1 }, { "schp", 30 } } },
        { "Bajo controlado", "Bajo", { { "mode", 0 }, { "thresh", -20 }, { "ratio", 4 }, { "knee", 6 }, { "attack", 25 }, { "release", 120 }, { "makeup", 3 }, { "detector", 1 } } },
        { "Master suave", "Master", { { "mode", 3 }, { "thresh", -10 }, { "ratio", 1.5f }, { "knee", 12 }, { "attack", 30 }, { "release", 300 }, { "autorel", 1 }, { "makeup", 1 }, { "schp", 60 } } },
        { "Upward detalle", "Creativo", { { "mode", 4 }, { "thresh", -40 }, { "ratio", 2 }, { "attack", 5 }, { "release", 200 } } },
        { "Limitador de picos", "Master", { { "mode", 0 }, { "thresh", -3 }, { "ratio", 30 }, { "knee", 0 }, { "attack", 0.05f }, { "release", 80 }, { "look", 5 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

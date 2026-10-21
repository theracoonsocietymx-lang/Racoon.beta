#include "FerretProcessor.h"
#include "FerretEditor.h"

namespace racoon::ferret
{
using namespace juce;

enum Mode { stereo, pingpong, dual, tape, multitap };

const StringArray& Processor::modeNames()
{
    static StringArray a { "Estéreo"_u, "Ping-pong", "Doble", "Cinta", "Multitap" };
    return a;
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", modeNames(), 0);
    P::toggle (l, "sync", "A tempo", true);
    P::choice (l, "divl", "División izquierda"_u, RacoonProcessor::divisionNames(), 8);
    P::choice (l, "divr", "División derecha"_u, RacoonProcessor::divisionNames(), 4);
    P::toggle (l, "link", "Enlazar L / R", false);
    P::ms (l, "timel", "Tiempo izquierda", 1.0f, 4000.0f, 375.0f, 400.0f);
    P::ms (l, "timer", "Tiempo derecha", 1.0f, 4000.0f, 500.0f, 400.0f);
    P::pct (l, "feedback", "Repeticiones", 0.0f, 98.0f, 40.0f);
    P::pct (l, "cross", "Cruce", 0.0f, 100.0f, 0.0f);
    P::hz (l, "hp", "Corte de graves", 20.0f, 1000.0f, 120.0f);
    P::hz (l, "lp", "Corte de agudos", 1000.0f, 20000.0f, 9000.0f);
    P::flt (l, "modrate", "Mod velocidad", P::range (0.01f, 15.0f, 1.0f), 0.6f, "Hz", 2);
    P::ms (l, "moddepth", "Mod profundidad", 0.0f, 50.0f, 0.0f, 5.0f);
    P::pct (l, "drive", "Saturación"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "diffusion", "Difusión"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "duck", "Ducking", 0.0f, 100.0f, 0.0f);
    P::toggle (l, "reverse", "Reverse", false);
    P::toggle (l, "freeze", "Congelar", false);
    P::pct (l, "jitter", "Jitter", 0.0f, 100.0f, 0.0f);
    P::integer (l, "taps", "Taps", 2, 8, 4);
    P::pct (l, "tapdecay", "Caída de taps"_u, 0.0f, 100.0f, 70.0f);
    P::pct (l, "width", "Anchura", 0.0f, 200.0f, 100.0f);
    P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 30.0f);
    P::db (l, "output", "Salida", -24.0f, 12.0f, 0.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Delay"_u, "Delay"_u, Colour (0xff7fbfa8), PluginKind::effect, 1, 12.0, true, true },
                       layout())
{
}

double Processor::delaySecondsFor (int c) const
{
    const bool link = value ("link") > 0.5f || (int) value ("mode") == pingpong;   // ping-pong bounces one time
    const int ch = link ? 0 : c;
    if (value ("sync") > 0.5f)
        return syncSeconds ((int) value (ch == 0 ? "divl" : "divr"), bpm());
    return value (ch == 0 ? "timel" : "timer") * 0.001;
}

void Processor::prepareDsp (double rate, int)
{
    const int maxLen = (int) (rate * 4.2) + 64;
    for (auto& l : line) l.prepare (maxLen);
    const int lens[2][2] { { 113, 337 }, { 127, 353 } };
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 2; ++k)
        {
            diff[c][k].len = (int) (lens[c][k] * rate / 48000.0);
            diff[c][k].d.prepare (diff[c][k].len + 4);
        }
    for (auto* s : { &mixSmooth, &fbSmooth, &outSmooth, &widthSmooth }) s->prepare (rate, 30.0);
    resetDsp();
}

void Processor::resetDsp()
{
    for (int c = 0; c < 2; ++c)
    {
        line[c].clear();
        hp[c].reset(); lp[c].reset();
        for (auto& d : diff[c]) d.d.clear();
        const float t = (float) jlimit (2.0, sr * 4.0, delaySecondsFor (c) * sr);
        headA[c] = headB[c] = tapeTime[c] = t;
        fading[c] = false; xfade[c] = 0.0f;
        jitterNow[c] = jitterTarget[c] = 0.0f;
        revPhase[c] = c * 0.5;
        lastOut[c] = 0.0f;
    }
    duckFollower.env = 0.0f;
    lfoPhase = 0.0;
    rng.seed (77);
    mixSmooth.reset (value ("mix") * 0.01f);
    fbSmooth.reset (value ("feedback") * 0.01f);
    outSmooth.reset (dsp::dbToGain (value ("output")));
    widthSmooth.reset (value ("width") * 0.01f);
}

float Processor::readHeads (int c, float target, bool isTape)
{
    if (isTape)
    {
        // tape: the head glides to the new time and the pitch bends, like a real machine
        tapeTime[c] += (target - tapeTime[c]) * (float) (1.0 - std::exp (-1.0 / (sr * 0.25)));
        headA[c] = headB[c] = tapeTime[c];
        fading[c] = false;
        return 0.0f;
    }
    tapeTime[c] = target;
    if (! fading[c] && std::abs (target - headA[c]) > 0.5f) { headB[c] = target; fading[c] = true; xfade[c] = 0.0f; }
    return 0.0f;
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    const int mode = (int) value ("mode");
    const bool freeze = value ("freeze") > 0.5f;
    const bool reverse = value ("reverse") > 0.5f && mode != multitap;
    const float maxD = (float) (sr * 4.0);
    float target[2];
    for (int c = 0; c < 2; ++c) target[c] = (float) jlimit (2.0, (double) maxD, delaySecondsFor (c) * sr);

    fbSmooth.target = freeze ? 1.0f : value ("feedback") * 0.01f;
    mixSmooth.target = value ("mix") * 0.01f;
    outSmooth.target = dsp::dbToGain (value ("output"));
    widthSmooth.target = value ("width") * 0.01f;
    const float cross = mode == pingpong ? 1.0f : value ("cross") * 0.01f;
    const float gHp = dsp::SVF::gFor (value ("hp"), sr);
    const float gLp = dsp::SVF::gFor (mode == tape ? jmin (value ("lp"), 7000.0f) : value ("lp"), sr);
    const bool useHp = value ("hp") > 21.0f, useLp = value ("lp") < 19900.0f || mode == tape;
    float drive = value ("drive") * 0.01f;
    if (mode == tape) drive = jmax (drive, 0.25f);
    const float driveG = 1.0f + drive * 5.0f;
    const float diffG = value ("diffusion") * 0.01f * 0.6f;
    for (auto& cd : diff) for (auto& d : cd) d.g = diffG;
    const float duck = value ("duck") * 0.01f;
    duckFollower.set (sr, 5.0f, 250.0f);
    float modDepth = value ("moddepth") * 0.001f * (float) sr;
    double modRate = value ("modrate");
    if (mode == tape) modDepth = jmax (modDepth, 0.0004f * (float) sr);   // a little wow always
    const float jitter = value ("jitter") * 0.01f * 0.005f * (float) sr;
    const int taps = (int) value ("taps");
    const float tapDecay = value ("tapdecay") * 0.01f;
    const float xfadeInc = (float) (1.0 / (0.04 * sr));
    const bool isTape = mode == tape;

    float wetPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float inL = buffer.getSample (0, i), inR = ch > 1 ? buffer.getSample (1, i) : inL;
        const float mono = (inL + inR) * 0.5f;

        lfoPhase += modRate / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        if (--jitterCount <= 0)
        {
            jitterCount = (int) (sr * 0.05);
            for (auto& t : jitterTarget) t = rng.nextBipolar() * jitter;
        }

        float y[2] {};
        for (int c = 0; c < 2; ++c)
        {
            jitterNow[c] += (jitterTarget[c] - jitterNow[c]) * 0.0005f;
            float mod = freeze ? 0.0f : modDepth * (0.5f + 0.5f * (float) std::sin (MathConstants<double>::twoPi * (lfoPhase + c * 0.25)));
            if (isTape) mod += 0.00008f * (float) sr * (float) std::sin (MathConstants<double>::twoPi * lfoPhase * 11.0);   // flutter
            const float offset = freeze ? 0.0f : mod + jitterNow[c];
            readHeads (c, target[c], isTape);

            if (mode == multitap && c == 1) continue;   // both sides were made by the taps above

            if (reverse)
            {
                // two overlapping reversed grains with sin^2 windows that sum to one
                const float D = jmin (headA[c], maxD / 3.0f - 4.0f);
                revPhase[c] += 1.0 / jmax (64.0, (double) D);
                if (revPhase[c] >= 1.0) revPhase[c] -= 1.0;
                float acc = 0.0f;
                for (int k = 0; k < 2; ++k)
                {
                    double ph = revPhase[c] + k * 0.5;
                    ph -= std::floor (ph);
                    const float w = (float) std::pow (std::sin (MathConstants<double>::pi * ph), 2.0);
                    acc += w * line[c].tap (jlimit (2.0f, maxD, D + (float) (2.0 * D * ph) + offset));
                }
                y[c] = acc;
            }
            else if (mode == multitap)
            {
                float sumL = 0.0f, sumR = 0.0f, g = 1.0f;
                for (int k = 1; k <= taps; ++k)
                {
                    const float v = line[0].tap (jlimit (2.0f, maxD, headA[0] * (float) k / (float) taps + offset)) * g;
                    const bool left = (k % 2) == 1;
                    sumL += v * (left ? 0.85f : 0.15f);
                    sumR += v * (left ? 0.15f : 0.85f);
                    g *= tapDecay;
                }
                y[0] = sumL; y[1] = sumR;
                continue;
            }
            else
            {
                float v = line[c].tap (jlimit (2.0f, maxD, headA[c] + offset));
                if (fading[c])
                {
                    xfade[c] += xfadeInc;
                    const float w = jmin (1.0f, xfade[c]);
                    const float vb = line[c].tap (jlimit (2.0f, maxD, headB[c] + offset));
                    v = v * std::cos (w * MathConstants<float>::halfPi) + vb * std::sin (w * MathConstants<float>::halfPi);
                    if (xfade[c] >= 1.0f) { headA[c] = headB[c]; fading[c] = false; }
                }
                y[c] = v;
            }
        }

        // feedback path
        const float fb = fbSmooth.next();
        float f[2];
        if (mode == multitap)
        {
            const float last = line[0].tap (jlimit (2.0f, maxD, headA[0]));
            f[0] = last; f[1] = 0.0f;
        }
        else
        {
            f[0] = y[0] * (1.0f - cross) + y[1] * cross;
            f[1] = y[1] * (1.0f - cross) + y[0] * cross;
        }
        for (int c = 0; c < 2; ++c)
        {
            float v = f[c] * fb;
            if (std::abs (v) > 1.5f) v = std::tanh (v / 1.5f) * 1.5f;   // the loop can never run away
            f[c] = v;
        }

        // write: what enters each line (new input plus the repeats) goes through the filters, the drive and the
        // diffusion, so the first echo is already filtered and every repeat a little more, as in a hardware delay
        const float feedIn = freeze ? 0.0f : 1.0f;
        float w[2];
        switch (mode)
        {
            case pingpong: w[0] = mono * feedIn + f[0]; w[1] = f[1]; break;
            case dual:     w[0] = mono * feedIn + f[0]; w[1] = mono * feedIn + f[1]; break;
            case multitap: w[0] = mono * feedIn + f[0]; w[1] = 0.0f; break;
            default:       w[0] = inL * feedIn + f[0]; w[1] = inR * feedIn + f[1]; break;
        }
        for (int c = 0; c < 2; ++c)
        {
            float v = w[c];
            if (! freeze)
            {
                if (useHp) { hp[c].process (v, gHp, 1.414f); v = hp[c].hp; }
                if (useLp) { lp[c].process (v, gLp, 1.414f); v = lp[c].lp; }
                if (drive > 0.001f) v = std::tanh (v * driveG) / driveG;
                if (diffG > 0.001f) v = diff[c][1].process (diff[c][0].process (v));
            }
            line[c].push (v);
        }

        // output
        const float env = duckFollower.process (jmax (std::abs (inL), std::abs (inR)));
        const float duckGain = 1.0f - duck * jmin (1.0f, env * 4.0f);
        const float wdt = widthSmooth.next();
        const float mid = (y[0] + y[1]) * 0.5f, side = (y[0] - y[1]) * 0.5f * wdt;
        const float wl = (mid + side) * duckGain, wr = (mid - side) * duckGain;
        const float m = mixSmooth.next(), og = outSmooth.next();
        const float dryG = jmin (1.0f, 2.0f * (1.0f - m)), wetG = jmin (1.0f, 2.0f * m);
        buffer.setSample (0, i, (inL * dryG + wl * wetG) * og);
        if (ch > 1) buffer.setSample (1, i, (inR * dryG + wr * wetG) * og);
        wetPeak = jmax (wetPeak, std::abs (wl), std::abs (wr));
    }
    wetLevel.store (jmax (wetPeak, wetLevel.load() * 0.9f));
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Corchea con puntillo", "Básicos"_u, { { "divl", 8 }, { "divr", 8 }, { "feedback", 35 }, { "mix", 25 } } },
        { "Ping-pong 1/8", "Rítmicos"_u, { { "mode", 1 }, { "divl", 7 }, { "divr", 7 }, { "feedback", 45 }, { "mix", 30 } } },
        { "Doble 1/8 y 1/4", "Rítmicos"_u, { { "mode", 2 }, { "divl", 7 }, { "divr", 4 }, { "feedback", 30 }, { "mix", 30 } } },
        { "Slapback", "Cortos", { { "sync", 0 }, { "timel", 95 }, { "timer", 110 }, { "feedback", 5 }, { "mix", 35 } } },
        { "Eco de cinta", "Cinta", { { "mode", 3 }, { "divl", 5 }, { "divr", 5 }, { "feedback", 55 }, { "drive", 35 }, { "lp", 5000 }, { "mix", 30 } } },
        { "Dub", "Cinta", { { "mode", 3 }, { "divl", 4 }, { "divr", 5 }, { "feedback", 75 }, { "hp", 300 }, { "lp", 3500 }, { "drive", 40 }, { "mix", 35 } } },
        { "Multitap disperso", "Creativos", { { "mode", 4 }, { "divl", 1 }, { "taps", 6 }, { "tapdecay", 75 }, { "feedback", 20 }, { "mix", 30 } } },
        { "Reverso", "Creativos", { { "reverse", 1 }, { "divl", 4 }, { "divr", 4 }, { "feedback", 30 }, { "mix", 40 } } },
        { "Nube difusa", "Creativos", { { "diffusion", 80 }, { "feedback", 60 }, { "moddepth", 4 }, { "modrate", 0.3f }, { "mix", 35 } } },
        { "Voz con ducking", "Voces", { { "divl", 7 }, { "divr", 8 }, { "feedback", 40 }, { "duck", 80 }, { "mix", 35 } } },
        { "Chorus de delay", "Cortos", { { "sync", 0 }, { "timel", 12 }, { "timer", 15 }, { "feedback", 0 }, { "moddepth", 3 }, { "modrate", 0.8f }, { "mix", 50 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

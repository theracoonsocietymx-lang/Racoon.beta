#include "FerretFx.h"

namespace racoon::ferret
{
using namespace juce;

void Biquad::set (Type t, double sr, double hz, double q, double gainDb)
{
    const double w = MathConstants<double>::twoPi * jlimit (5.0, sr * 0.49, hz) / sr;
    const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * jmax (0.05, q));
    const double A = std::pow (10.0, gainDb / 40.0);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (t)
    {
        case lowpass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case highpass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case bandpass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
        case lowShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
            A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case highShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }
    b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
}

// ===================================================================== amplifier
const std::vector<AmpModel>& ampModels()
{
    static const std::vector<AmpModel> m {
        //  name              minDb maxDb bias  hard  tight  bass  mid    treble midQ postLp  sag   clean
        { "Limpio",            0.0f, 22.0f, 0.04f, 0.0f,  60.0f, 110.0f, 650.0f, 3200.0f, 0.7f, 9000.0f, 0.6f, false },
        { "Crunch",            6.0f, 36.0f, 0.12f, 0.1f,  80.0f, 120.0f, 750.0f, 3000.0f, 0.8f, 7500.0f, 1.0f, false },
        { "Británico",      10.0f, 46.0f, 0.10f, 0.2f, 120.0f, 130.0f, 900.0f, 2800.0f, 1.0f, 6500.0f, 1.2f, false },
        { "Americano",         0.0f, 30.0f, 0.06f, 0.1f,  70.0f, 100.0f, 500.0f, 3600.0f, 0.6f, 8500.0f, 0.8f, false },
        { "Vintage de transistores", 4.0f, 40.0f, 0.02f, 0.9f, 90.0f, 110.0f, 800.0f, 2600.0f, 0.9f, 6000.0f, 0.0f, false },
        { "Directo",           0.0f, 12.0f, 0.0f,  0.0f,  30.0f, 100.0f, 800.0f, 3500.0f, 0.7f, 18000.0f, 0.0f, true },
    };
    return m;
}

StringArray ampNames() { StringArray s; for (auto& m : ampModels()) s.add (String::fromUTF8 (m.name)); return s; }

void Amp::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    os->initProcessing ((size_t) jmax (64, maxBlock));
    for (auto& f : sagEnv) f.set (sr, 15.0f, 200.0f);
    designed = false;
    reset();
}

void Amp::reset()
{
    if (os) os->reset();
    for (int c = 0; c < 2; ++c)
    {
        for (auto* b : { &tight[c], &bass[c], &mid[c], &treble[c], &presence[c], &post[c], &dcb[c] }) b->reset();
        sagEnv[c].env = 0.0f;
    }
}

void Amp::design (const Settings& s)
{
    const auto& m = ampModels()[(size_t) jlimit (0, (int) ampModels().size() - 1, s.model)];
    for (int c = 0; c < 2; ++c)
    {
        tight[c].set (Biquad::highpass, sr, m.tightHz, 0.7);
        bass[c].set (Biquad::lowShelf, sr, m.bassHz, 0.7, (s.bass - 0.5f) * 24.0f);
        mid[c].set (Biquad::peak, sr, m.midHz, m.midQ, (s.mid - 0.5f) * 20.0f);
        treble[c].set (Biquad::highShelf, sr, m.trebleHz, 0.7, (s.treble - 0.5f) * 24.0f);
        presence[c].set (Biquad::highShelf, sr, 4500.0, 0.7, (s.presence - 0.5f) * 16.0f);
        post[c].set (Biquad::lowpass, sr, m.postLp, 0.7);
        dcb[c].set (Biquad::highpass, sr, 12.0, 0.7);
    }
    last = s;
    designed = true;
}

void Amp::process (AudioBuffer<float>& b, int n, const Settings& s)
{
    if (! designed || s.model != last.model || s.bass != last.bass || s.mid != last.mid || s.treble != last.treble || s.presence != last.presence)
        design (s);
    last = s;
    const auto& m = ampModels()[(size_t) jlimit (0, (int) ampModels().size() - 1, s.model)];
    const float pre = dsp::dbToGain (m.minDb + s.gain * (m.maxDb - m.minDb));
    const int chans = jmin (2, b.getNumChannels());

    for (int c = 0; c < chans; ++c)
    {
        auto* x = b.getWritePointer (c);
        const float sagGain = 1.0f / (1.0f + s.sag * m.sagScale * sagEnv[c].env * 3.0f);
        for (int i = 0; i < n; ++i) x[i] = tight[c].process (x[i]) * pre * sagGain;
    }

    if (! m.clean)
    {
        // the preamp clips at twice the rate (less aliasing)
        juce::dsp::AudioBlock<float> block (b.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        auto up = os->processSamplesUp (block);
        const float bias = m.bias, softB = std::tanh (bias), hardB = jlimit (-1.0f, 1.0f, bias);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* x = up.getChannelPointer (c);
            // the preamp curve with ADAA on top of the 2x: averaged between samples, the clipping folds much less back
            auto curve = [&] (float in)
            {
                const float v = in + bias;
                const float soft = std::tanh (v) - softB;
                const float hard = jlimit (-1.0f, 1.0f, v * 1.5f) - hardB;
                return soft + (hard - soft) * m.hardness;
            };
            for (size_t i = 0; i < up.getNumSamples(); ++i) x[i] = racoon::sdsp::adaaAverage (curve, x[i], preX[c & 1]);
        }
        os->processSamplesDown (block);
    }

    const float master = dsp::dbToGain (s.masterDb) * (m.clean ? 1.0f : 0.5f);
    for (int c = 0; c < chans; ++c)
    {
        auto* x = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float y = treble[c].process (mid[c].process (bass[c].process (x[i])));
            if (! m.clean) y = powerSat[c].process (y * 1.3f) / 1.3f * 1.4f;   // the power stage (ADAA)
            y = presence[c].process (post[c].process (y));
            y = dcb[c].process (y);
            sagEnv[c].process (std::abs (y));
            x[i] = y * master;
        }
    }
}

// ===================================================================== cabinet
StringArray cabNames() { return { "1x12 abierto", "2x12 americano", "4x12 británico"_u, "Combo vintage", "Sin gabinete" }; }

namespace
{
struct CabShape { float hp, resHz, resDb, notchHz, notchDb, presHz, lp; };
const CabShape cabShapes[] {
    {  90.0f, 110.0f, 3.0f, 500.0f, -3.0f, 2500.0f, 5500.0f },
    {  80.0f, 105.0f, 4.0f, 400.0f, -4.0f, 3000.0f, 6000.0f },
    {  75.0f,  95.0f, 5.0f, 600.0f, -3.0f, 2200.0f, 4800.0f },
    { 110.0f, 130.0f, 2.0f, 800.0f, -2.0f, 2000.0f, 4200.0f },
};
}

void Cabinet::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& d : room) d.prepare ((int) (0.03 * sr) + 8);
    designed = false;
    reset();
}

void Cabinet::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        for (auto* b : { &hp[c], &res[c], &notch[c], &pres[c], &lp[c], &prox[c], &ribbonLp[c], &ribbonLow[c] }) b->reset();
        room[c].clear();
    }
}

void Cabinet::design (const Settings& s)
{
    const auto& k = cabShapes[jlimit (0, 3, s.type)];
    const float lpHz = k.lp * (1.25f - 0.6f * s.micPos) * (1.0f - 0.3f * s.distance);
    for (int c = 0; c < 2; ++c)
    {
        hp[c].set (Biquad::highpass, sr, k.hp, 0.8);
        res[c].set (Biquad::peak, sr, k.resHz, 2.0, k.resDb);
        notch[c].set (Biquad::peak, sr, k.notchHz, 1.2, k.notchDb);
        pres[c].set (Biquad::peak, sr, k.presHz, 1.5, 4.0 - 6.0 * s.micPos);
        lp[c].set (Biquad::lowpass, sr, lpHz, 0.9);
        prox[c].set (Biquad::lowShelf, sr, 200.0, 0.7, 5.0 * (1.0f - s.distance) - 1.0f);
        ribbonLp[c].set (Biquad::lowpass, sr, 3200.0, 0.6);
        ribbonLow[c].set (Biquad::lowShelf, sr, 250.0, 0.7, 2.0);
    }
    last = s;
    designed = true;
}

void Cabinet::process (AudioBuffer<float>& b, int n, const Settings& s)
{
    if (s.type >= 4) return;   // straight from the amp
    if (! designed || s.type != last.type || s.micPos != last.micPos || s.distance != last.distance) design (s);
    last = s;
    static const float tapMs[5] { 3.1f, 7.3f, 11.9f, 17.2f, 23.5f }, tapG[5] { 0.5f, 0.4f, 0.3f, 0.25f, 0.2f };
    const float roomMix = jlimit (0.0f, 1.0f, s.room + s.distance * 0.3f) * 0.6f;
    for (int c = 0; c < jmin (2, b.getNumChannels()); ++c)
    {
        auto* x = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i];
            float y = lp[c].process (pres[c].process (notch[c].process (res[c].process (hp[c].process (in)))));
            y = prox[c].process (y);
            const float rib = ribbonLow[c].process (ribbonLp[c].process (y));
            y += (rib - y) * s.ribbon;
            room[c].push (y);
            float r = 0.0f;
            for (int t = 0; t < 5; ++t) r += room[c].tapInt ((int) (tapMs[t] * (c == 0 ? 1.0f : 1.13f) * 0.001f * (float) sr)) * tapG[t];
            y += r * roomMix;
            x[i] = in + (y - in) * s.blend;
        }
    }
}

// ===================================================================== pedals
void Pedals::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& d : chLine) d.prepare ((int) (0.05 * sr));
    for (auto& d : dl) d.prepare ((int) (2.1 * sr));
    dlTime.prepare (sr, 120.0);
    verb.setSampleRate (sr);
    for (int c = 0; c < 2; ++c)
    {
        odHp[c].set (Biquad::highpass, sr, 720.0, 0.7);
        odMid[c].set (Biquad::peak, sr, 720.0, 0.8, 4.0);
    }
    reset();
}

void Pedals::reset()
{
    compEnv = 0.0f; compGain = 1.0f; wahEnv = 0.0f; wahPhase = chPhase = phPhase = trPhase = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        wahF[c].reset(); odHp[c].reset(); odMid[c].reset(); odTone[c].reset(); chLine[c].clear(); dl[c].clear();
        dlLp[c] = 0.0f; phFb[c] = 0.0f;
        for (auto& a : apState[c]) a = 0.0f;
    }
    dlTime.reset (0.375f * (float) sr);
    verb.reset();
}

void Pedals::processPre (AudioBuffer<float>& b, int n, const Settings& s)
{
    const int chans = jmin (2, b.getNumChannels());
    float* ch[2] { b.getWritePointer (0), b.getWritePointer (chans > 1 ? 1 : 0) };

    if (s.comp)
    {
        const float thr = -12.0f - 28.0f * s.compAmount, ratio = 4.0f, makeup = dsp::dbToGain (s.compAmount * 14.0f);
        const float att = std::exp (-1.0f / (0.004f * (float) sr)), rel = std::exp (-1.0f / (0.12f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const float lv = jmax (std::abs (ch[0][i]), std::abs (ch[1][i]));
            compEnv = lv + (compEnv - lv) * (lv > compEnv ? att : rel);
            const float db = dsp::gainToDb (compEnv);
            const float target = db > thr ? dsp::dbToGain ((thr - db) * (1.0f - 1.0f / ratio)) : 1.0f;
            compGain = target + (compGain - target) * 0.995f;
            for (int c = 0; c < chans; ++c) ch[c][i] *= compGain * makeup;
        }
    }

    if (s.wah)
    {
        const float k = 1.0f / (2.0f + s.wahQ * 8.0f);
        const float envA = std::exp (-1.0f / (0.005f * (float) sr)), envR = std::exp (-1.0f / (0.15f * (float) sr));
        const float smooth = std::exp (-1.0f / (0.01f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            float pos = s.wahPos;
            if (s.wahMode == 1)
            {
                wahPhase += s.wahRate / sr;
                if (wahPhase >= 1.0) wahPhase -= 1.0;
                pos = 0.5f - 0.5f * (float) std::cos (MathConstants<double>::twoPi * wahPhase);
            }
            else if (s.wahMode == 2)
            {
                const float lv = jmax (std::abs (ch[0][i]), std::abs (ch[1][i]));
                wahEnv = lv + (wahEnv - lv) * (lv > wahEnv ? envA : envR);
                pos = jlimit (0.0f, 1.0f, wahEnv * (1.0f + s.wahPos * 12.0f));
            }
            wahSmooth = pos + (wahSmooth - pos) * smooth;
            const float hz = 350.0f * std::pow (2200.0f / 350.0f, wahSmooth);
            const float g = dsp::SVF::gFor (hz, sr);
            for (int c = 0; c < chans; ++c)
            {
                wahF[c].process (ch[c][i], g, k);
                ch[c][i] = wahF[c].bp * k * 2.2f + wahF[c].lp * 0.15f;
            }
        }
        wahShown = wahSmooth;
    }

    if (s.od)
    {
        const float gain = 1.0f + s.odDrive * 60.0f, level = dsp::dbToGain (s.odLevel);
        for (int c = 0; c < chans; ++c)
        {
            odTone[c].set (Biquad::lowpass, sr, 900.0 * std::pow (7.0, s.odTone), 0.7);
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float d = odSat[c].process (odMid[c].process (odHp[c].process (x)) * gain);   // ADAA: gain up to 61x
                ch[c][i] = odTone[c].process (0.5f * x + d * 0.6f) * level;
            }
        }
    }
}

void Pedals::processPost (AudioBuffer<float>& b, int n, const Settings& s)
{
    const int chans = jmin (2, b.getNumChannels());
    float* ch[2] { b.getWritePointer (0), b.getWritePointer (chans > 1 ? 1 : 0) };
    const double twoPi = MathConstants<double>::twoPi;

    if (s.chorus)
    {
        for (int i = 0; i < n; ++i)
        {
            chPhase += s.chRate / sr;
            if (chPhase >= 1.0) chPhase -= 1.0;
            for (int c = 0; c < chans; ++c)
            {
                const double ph = chPhase + (c == 0 ? 0.0 : 0.25);
                const float d = (float) ((0.012 + 0.004 * s.chDepth * std::sin (twoPi * ph)) * sr);
                chLine[c].push (ch[c][i]);
                const float wet = chLine[c].tap (d);
                ch[c][i] = ch[c][i] * (1.0f - 0.5f * s.chMix) + wet * s.chMix;
            }
        }
    }

    if (s.phaser)
    {
        for (int i = 0; i < n; ++i)
        {
            phPhase += s.phRate / sr;
            if (phPhase >= 1.0) phPhase -= 1.0;
            for (int c = 0; c < chans; ++c)
            {
                const double ph = phPhase + (c == 0 ? 0.0 : 0.15);
                const float hz = 200.0f * std::pow (8.0f, (float) (0.5 + 0.5 * std::sin (twoPi * ph)));
                const float t = std::tan (MathConstants<float>::pi * hz / (float) sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                float x = ch[c][i] + phFb[c] * 0.5f * s.phDepth;
                for (auto& st : apState[c])
                {
                    const float y = a * x + st;
                    st = x - a * y;
                    x = y;
                }
                phFb[c] = x;
                ch[c][i] = ch[c][i] * (1.0f - 0.5f * s.phMix) + x * s.phMix;
            }
        }
    }

    if (s.trem)
    {
        for (int i = 0; i < n; ++i)
        {
            trPhase += s.trRate / sr;
            if (trPhase >= 1.0) trPhase -= 1.0;
            const float g = 1.0f - s.trDepth * (0.5f + 0.5f * (float) std::sin (twoPi * trPhase));
            for (int c = 0; c < chans; ++c) ch[c][i] *= g;
        }
    }

    if (s.delay)
    {
        dlTime.target = jlimit (10.0f, 2.0f * (float) sr, s.dlSeconds * (float) sr);
        const float toneC = std::exp (-MathConstants<float>::twoPi * (1500.0f * std::pow (8.0f, s.dlTone)) / (float) sr);
        for (int i = 0; i < n; ++i)
        {
            const float d = dlTime.next();
            for (int c = 0; c < chans; ++c)
            {
                const float wet = dl[c].tap (d * (c == 0 ? 1.0f : 1.02f));
                dlLp[c] = wet + (dlLp[c] - wet) * toneC;
                dl[c].push (ch[c][i] + dlLp[c] * jlimit (0.0f, 0.95f, s.dlFeedback));
                ch[c][i] += wet * s.dlMix;
            }
        }
    }

    if (s.reverb)
    {
        racoon::sdsp::Verb::Parameters p;   // the new 8-line FDN reverb, same knobs and levels as the old one
        p.roomSize = s.rvSize;
        p.damping = s.rvDamp;
        p.wetLevel = s.rvMix * 0.8f;
        p.dryLevel = 1.0f - s.rvMix * 0.4f;
        p.width = 1.0f;
        verb.setParameters (p);
        if (chans > 1) verb.processStereo (ch[0], ch[1], n);
        else verb.processMono (ch[0], n);
    }
}
}

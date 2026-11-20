#include "JerboaProcessor.h"
#include "JerboaEditor.h"

namespace racoon::jerboa
{
using namespace juce;

const StringArray& Processor::bankNames()
{
    static StringArray a { "Inicial", "Build y drop", "Medio tiempo", "Compuertas", "Cinta", "Rascados" };
    return a;
}

const StringArray& Processor::quantNames()
{
    static StringArray a { "Al instante", "En 1/16", "En 1/8", "En 1/4", "En el compás"_u };
    return a;
}

static double quantBeats (int i)
{
    static const double q[] { 0.0, 0.25, 0.5, 1.0, 4.0 };
    return q[jlimit (0, 4, i)];
}

static double holdBeats (int i)
{
    static const double q[] { 0.25, 0.5, 1.0, 2.0, 4.0 };
    return q[jlimit (0, 4, i)];
}

AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::pct (l, "tmix", "Tiempo: mezcla"_u, 0.0f, 100.0f, 100.0f);
    P::integer (l, "patron", "Patrón"_u, 1, patternsPerBank, 1);
    P::pct (l, "vmix", "Volumen: cantidad"_u, 0.0f, 100.0f, 100.0f);
    P::ms (l, "suave", "Suavizado de saltos", 0.5f, 40.0f, 6.0f, 6.0f);
    P::pct (l, "tono", "Tono de cinta", 0.0f, 100.0f, 40.0f);
    P::db (l, "salida", "Salida", -24.0f, 12.0f, 0.0f);
    P::choice (l, "banco", "Banco de patrones", bankNames(), 0);
    P::choice (l, "cambio", "Cambiar de patrón"_u, quantNames(), 0);
    P::choice (l, "arranque", "Arranque del patrón"_u, { "Sigue el compás"_u, "Desde la nota" }, 0);
    P::choice (l, "soltar", "Al soltar la nota", { "Se queda", "Vuelve al patrón base"_u }, 0);
    P::pct (l, "velvol", "Velocidad al volumen", 0.0f, 100.0f, 0.0f);
    P::ms (l, "vatk", "Volumen: ataque", 0.0f, 50.0f, 1.0f, 5.0f);
    P::ms (l, "vrel", "Volumen: relajación"_u, 0.0f, 200.0f, 3.0f, 20.0f);
    P::toggle (l, "rascaron", "Rascar a mano", false);
    P::pct (l, "rascar", "Rascar", 0.0f, 100.0f, 0.0f);
    P::ms (l, "inercia", "Inercia del plato", 5.0f, 400.0f, 80.0f, 80.0f);
    P::toggle (l, "mantener", "Mantener", false);
    P::choice (l, "mantlargo", "Mantener: largo", { "1/16", "1/8", "1/4", "1/2", "1 compás"_u }, 2);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Jerboa", "Tiempo y volumen", Colour (0xffdebb84), PluginKind::effect, 1, 8.0, false, true },
                       layout())
{
    wantsMidiAsEffect = true;
}

void Processor::prepareDsp (double rate, int)
{
    // 12 seconds of history (a bar at 20 BPM), a power of two so the ring wraps with a mask
    int size = 1;
    while (size < (int) (rate * 12.0) + 64) size <<= 1;
    for (auto& h : hist) h.assign ((size_t) size, 0.0f);
    histMask = size - 1;
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
    written = 0;
    timePat = volPat = jlimit (0, patternsPerBank - 1, (int) value ("patron") - 1);
    lastPatronParam = timePat;
    timeAnchor = volAnchor = 0.0;
    pending = {};
    heldSwitches = 0; switchVel = 1.0f;
    dPrev = 0.0; speed = 1.0; havePrev = false;
    xfLeft = 0; dOld = 0.0; sOld = 1.0;
    tapeLp[0] = tapeLp[1] = 0.0f; tapeC = 1.0f;
    stopGain = 1.0f; stoppedFor = 0; volNow = 1.0f;
    platter = platterVel = 0.0; holdOn = false;
}

// one sample from `d` samples behind the newest one: exact on whole samples (a flat delay is bit for bit),
// linear right behind the write head (only past samples exist there), cubic further back
float Processor::readAt (int ch, double d) const
{
    const auto& h = hist[ch];
    const double r = (double) (written - 1) - d;
    const double fl = std::floor (r);
    const int64 i = (int64) fl;
    const float t = (float) (r - fl);
    auto at = [&] (int64 k) { return h[(size_t) (k & histMask)]; };
    if (t < 1.0e-6f) return at (i);
    if (d < 2.0) return at (i) + t * (at (i + 1) - at (i));
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void Processor::requestSwitch (int tp, int vp, float vel, double ppqNow)
{
    const double q = quantBeats ((int) value ("cambio"));
    pending.on = true;
    pending.at = q <= 0.0 ? ppqNow : std::ceil (ppqNow / q - 1.0e-9) * q;
    pending.timePat = tp; pending.volPat = vp; pending.vel = vel;
}

void Processor::handleMidi (const MidiMessage& m, double ppqNow)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        if (n >= firstNote && n < firstNote + patternsPerBank) { requestSwitch (n - firstNote, n - firstNote, m.getFloatVelocity(), ppqNow); ++heldSwitches; }
        else if (n >= firstVolNote && n < firstVolNote + patternsPerBank) { requestSwitch (-1, n - firstVolNote, m.getFloatVelocity(), ppqNow); ++heldSwitches; }
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        if ((n >= firstNote && n < firstNote + patternsPerBank) || (n >= firstVolNote && n < firstVolNote + patternsPerBank))
        {
            heldSwitches = jmax (0, heldSwitches - 1);
            if (heldSwitches == 0 && (int) value ("soltar") == 1)
            {
                const int base = jlimit (0, patternsPerBank - 1, (int) value ("patron") - 1);
                requestSwitch (base, base, 1.0f, ppqNow);
            }
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) heldSwitches = 0;
}

static inline double posMod (double x, double m) { const double r = std::fmod (x, m); return r < 0.0 ? r + m : r; }

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, buffer.getNumChannels(), getMainBusNumOutputChannels());
    const bool playing = isPlaying();
    const double tempo = jmax (20.0, bpm());
    const double spb = 60.0 / tempo * sr;                  // samples per beat
    const double ppq0 = playing ? ppq() : freeBeat;
    if (! playing) freeBeat += n / spb;

    const int bank = (int) value ("banco");
    const int patron = jlimit (0, patternsPerBank - 1, (int) value ("patron") - 1);
    if (patron != lastPatronParam) { requestSwitch (patron, patron, 1.0f, ppq0); lastPatronParam = patron; }

    const float tmix = value ("tmix") * 0.01f, vmix = value ("vmix") * 0.01f, tono = value ("tono") * 0.01f;
    const float dryG = std::cos (tmix * MathConstants<float>::halfPi), wetG = std::sin (tmix * MathConstants<float>::halfPi);
    const int xfSamples = jmax (1, (int) (value ("suave") * 0.001 * sr));
    const bool followBar = (int) value ("arranque") == 0;
    const float velvol = value ("velvol") * 0.01f;
    const float atkC = 1.0f - std::exp (-1.0f / (float) (jmax (0.0003, value ("vatk") * 0.001) * sr));
    const float relC = 1.0f - std::exp (-1.0f / (float) (jmax (0.0003, value ("vrel") * 0.001) * sr));
    const float outG = dsp::dbToGain (value ("salida"));
    const bool scratch = value ("rascaron") > 0.5f;
    const double platterTarget = value ("rascar") * 0.01;
    const double w = 1.0 / jmax (0.005, value ("inercia") * 0.001);
    const double dt = 1.0 / sr;
    const bool hold = value ("mantener") > 0.5f;
    const double holdLen = holdBeats ((int) value ("mantlargo")) * spb;
    const double maxD = (double) histMask - 8.0;

    auto it = midi.cbegin();
    float* io[2] { buffer.getWritePointer (0), buffer.getWritePointer (ch > 1 ? 1 : 0) };
    double uT = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double ppqNow = ppq0 + i / spb;
        while (it != midi.cend() && (*it).samplePosition <= i) { handleMidi ((*it).getMessage(), ppqNow); ++it; }
        if (pending.on && (! playing || ppqNow >= pending.at - 1.0e-9))
        {
            const double anchor = std::floor (pending.at * 4.0 + 1.0e-6) / 4.0;     // "from the note" starts on the sixteenth
            if (pending.timePat >= 0) { timePat = pending.timePat; timeAnchor = anchor; }
            if (pending.volPat >= 0) { volPat = pending.volPat; volAnchor = anchor; }
            switchVel = pending.vel;
            pending.on = false;
        }

        float in[2] { io[0][i], io[1][i] };
        for (int c = 0; c < 2; ++c) hist[c][(size_t) (written & histMask)] = in[c];
        ++written;

        if (! playing)
        {
            // the song is stopped: what comes in goes out, and a pattern that starts later fades in from here
            dPrev = 0.0; speed = 1.0; havePrev = true; xfLeft = 0; volNow = 1.0f; holdOn = false;
            for (int c = 0; c < ch; ++c) io[c][i] = in[c] * outG;
            continue;
        }

        const auto& pat = racoon::jerboa::pattern (bank, timePat);
        const auto& vpat = racoon::jerboa::pattern (bank, volPat);
        uT = followBar ? posMod (ppqNow, barBeats) : posMod (ppqNow - timeAnchor, barBeats);
        const double uV = followBar ? posMod (ppqNow, barBeats) : posMod (ppqNow - volAnchor, barBeats);

        // how far back to read: hold > hand scratch > the pattern's time shape
        double d;
        if (hold)
        {
            if (! holdOn) { holdOn = true; holdCapture = (int64) std::llround ((double) (written - 1) - dPrev); holdStart = written; }
            const double r = (double) holdCapture - holdLen + std::fmod ((double) (written - holdStart), holdLen);
            d = (double) (written - 1) - r;
        }
        else
        {
            holdOn = false;
            if (scratch)
            {
                // a platter with inertia follows the hand (critically damped), so a jerky line becomes a vinyl move
                const double acc = w * w * (platterTarget - platter) - 2.0 * w * platterVel;
                platterVel += acc * dt;
                platter += platterVel * dt;
                d = jmax (0.0, platter) * spb;
            }
            else d = timeAt (pat.time, uT) * spb;
        }
        d = jlimit (0.0, maxD, d);

        // a jump (a step in the shape, a wrap, a switch): the old head keeps going at its speed and fades out
        if (havePrev && std::abs (d - dPrev) > 8.0)
        {
            dOld = dPrev; sOld = speed;
            xfLen = xfSamples; xfLeft = xfSamples;
        }
        else if (havePrev) speed = 1.0 - (d - dPrev);
        dPrev = d; havePrev = true;

        // tape tone: slower is darker; transparent at normal speed
        const float as = (float) jmin (1.0, std::abs (speed));
        const float fc = 20000.0f * std::pow (jmax (0.02f, as), 1.5f);
        tapeC += ((1.0f - std::exp (-MathConstants<float>::twoPi * fc / (float) sr)) - tapeC) * 0.002f;
        const float toneAmt = tono * jlimit (0.0f, 1.0f, (1.0f - as) * 20.0f);

        // a stopped tape is silent (no hum): after 20 ms below 2 % speed it fades out in 10 ms
        if (as < 0.02f) ++stoppedFor; else stoppedFor = 0;
        const float stopTarget = stoppedFor > (int) (0.02 * sr) ? 0.0f : 1.0f;
        stopGain += jlimit (-1.0f / (float) (0.01 * sr), 1.0f / (float) (0.01 * sr), stopTarget - stopGain);

        // volume lane, smoothed with its own attack and release (never under 0.3 ms: a square gate does not click)
        float g = 1.0f - vmix * (1.0f - (float) volAt (vpat.vol, uV));
        g *= 1.0f - velvol * (1.0f - switchVel);
        volNow += (g - volNow) * (g > volNow ? atkC : relC);

        float xfOut = 1.0f, xfIn = 1.0f;
        const bool fading = xfLeft > 0;
        if (fading)
        {
            dOld = jlimit (0.0, maxD, dOld + 1.0 - sOld);
            const float t = 1.0f - (float) xfLeft / (float) xfLen;
            xfOut = std::cos (t * MathConstants<float>::halfPi);
            xfIn = std::sin (t * MathConstants<float>::halfPi);
            --xfLeft;
        }
        for (int c = 0; c < ch; ++c)
        {
            float x = readAt (c, d);
            if (fading) x = readAt (c, dOld) * xfOut + x * xfIn;
            tapeLp[c] += tapeC * (x - tapeLp[c]);
            x += toneAmt * (tapeLp[c] - x);
            x *= stopGain;
            const float y = tmix >= 0.999f ? x : in[c] * dryG + x * wetG;
            io[c][i] = y * volNow * outG;
        }
    }
    while (it != midi.cend()) { handleMidi ((*it).getMessage(), ppq0 + n / spb); ++it; }

    uiTimePattern.store (timePat);
    uiVolPattern.store (volPat);
    uiPhase.store ((float) uT);
    uiSpeed.store ((float) speed);
    uiOffset.store ((float) (dPrev / spb));
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Inicio", {} },
        { "Cinta cansada", "Cinta", { { "banco", 4 }, { "patron", 2 }, { "tono", 70 } } },
        { "Freno de medianoche", "Cinta", { { "banco", 4 }, { "patron", 20 }, { "tono", 80 }, { "suave", 10 } } },
        { "Media velocidad oscura", "Medio tiempo", { { "banco", 2 }, { "patron", 2 }, { "tono", 85 } } },
        { "Medio tiempo pesado", "Medio tiempo", { { "banco", 2 }, { "patron", 3 }, { "tono", 60 }, { "vrel", 20 } } },
        { "Cámara lenta"_u, "Medio tiempo", { { "banco", 2 }, { "patron", 4 }, { "tono", 90 }, { "suave", 14 } } },
        { "Compuerta de sótano"_u, "Compuertas", { { "banco", 3 }, { "patron", 2 }, { "vatk", 4 }, { "vrel", 30 } } },
        { "Bombeo grave", "Compuertas", { { "banco", 3 }, { "patron", 5 }, { "vrel", 60 }, { "vmix", 80 } } },
        { "Puerta 3+3+2", "Compuertas", { { "banco", 3 }, { "patron", 4 }, { "vatk", 2 }, { "vrel", 12 } } },
        { "Build que se atora", "Build y drop", { { "banco", 1 }, { "patron", 4 }, { "cambio", 3 }, { "suave", 5 } } },
        { "Caída al drop"_u, "Build y drop", { { "banco", 1 }, { "patron", 19 }, { "cambio", 4 }, { "tono", 60 } } },
        { "Tartamudeo grave", "Glitch", { { "banco", 0 }, { "patron", 7 }, { "tono", 50 }, { "suave", 4 } } },
        { "Reversa en sombra", "Glitch", { { "banco", 5 }, { "patron", 5 }, { "tono", 55 }, { "suave", 8 } } },
        { "Rebobinado lento", "Cinta", { { "banco", 4 }, { "patron", 18 }, { "tono", 75 }, { "suave", 12 } } },
        { "Rascado sordo", "Rascados", { { "banco", 5 }, { "patron", 2 }, { "tono", 65 }, { "inercia", 150 } } },
        { "Plato a mano", "Rascados", { { "banco", 5 }, { "rascaron", 1 }, { "inercia", 120 }, { "tono", 60 } } },
        { "Mantener el compás"_u, "Manos", { { "mantlargo", 4 } } },
        { "Eco de cinta vieja", "Cinta", { { "banco", 4 }, { "patron", 3 }, { "tmix", 50 }, { "tono", 85 } } },
        { "Desvanecer en la oscuridad", "Cinta", { { "banco", 4 }, { "patron", 5 }, { "tono", 95 }, { "vrel", 80 } } },
        { "Respiración de bombo"_u, "Compuertas", { { "banco", 3 }, { "patron", 6 }, { "vrel", 70 } } },
    };
}

// a beat runs through Jerboa; the notes switch: Directo, Reversa en el 4, Compuerta 1/8 for a bar, Freno de cinta
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 0.25, 36, 100, 1 }, { 4.0, 0.25, 36, 100, 1 }, { 7.0, 0.25, 46, 100, 1 },
             { 8.0, 0.25, 39, 100, 1 }, { 12.0, 0.25, 36, 100, 1 }, { 14.0, 0.25, 55, 100, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

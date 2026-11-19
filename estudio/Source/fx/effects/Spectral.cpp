// Creativos y espectrales: Resonador espectral, Tiempo espectral, Transpositor, Vocoder
#include "AllEffects.h"
#include "FxBase.h"
#include <complex>

namespace racoon::fx
{
using namespace juce;
using dsp2::Biquad;
float tunerDetect (const float*, int, double);

namespace
{
inline float toGain (float db) { return db <= -69.5f ? 0.0f : std::pow (10.0f, db * 0.05f); }
constexpr double twoPi = MathConstants<double>::twoPi;
using cpx = std::complex<float>;

// ===================================================================== short-time Fourier framework
// 2048-point frames, hop 512, Hann in and out. Output is late by exactly `latency()` samples.
class Stft
{
public:
    static constexpr int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;
    static constexpr int latency() { return N; }
    std::function<void (cpx* spectrum, int channel)> onFrame;

    void prepare()
    {
        for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * (float) std::cos (twoPi * i / N);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { std::fill (std::begin (inBuf[c]), std::end (inBuf[c]), 0.0f); std::fill (std::begin (outBuf[c]), std::end (outBuf[c]), 0.0f); }
        pos = 0; count = 0;
    }
    // one sample in, one sample out, per channel (call with every channel for the same sample index, then advance())
    float push (int c, float x)
    {
        inBuf[c][pos] = x;
        return outBuf[c][pos];
    }
    void advance (int channels)
    {
        for (int c = 0; c < channels; ++c) outBuf[c][pos] = 0.0f;   // this slot has been read; it will be accumulated again
        pos = (pos + 1) % N;
        if (++count == hop)
        {
            count = 0;
            for (int c = 0; c < channels; ++c) frame (c);
        }
    }
private:
    juce::dsp::FFT fft { order };
    float win[N] {};
    float inBuf[2][N] {}, outBuf[2][N] {};
    float work[2 * N] {};
    cpx spec[bins];
    int pos = 0, count = 0;

    void frame (int c)
    {
        // the latest N samples, oldest first
        for (int i = 0; i < N; ++i) work[i] = inBuf[c][(pos + i) % N] * win[i];
        std::fill (work + N, work + 2 * N, 0.0f);
        fft.performRealOnlyForwardTransform (work, true);
        for (int k = 0; k < bins; ++k) spec[k] = { work[2 * k], work[2 * k + 1] };
        if (onFrame) onFrame (spec, c);
        for (int k = 0; k < bins; ++k) { work[2 * k] = spec[k].real(); work[2 * k + 1] = spec[k].imag(); }
        // mirror for the inverse of a real signal
        for (int k = bins; k < N; ++k) { work[2 * k] = spec[N - k].real(); work[2 * k + 1] = -spec[N - k].imag(); }
        fft.performRealOnlyInverseTransform (work);
        // overlap-add; Hann in and out at 4x overlap sums to 1.5
        const float g = 1.0f / 1.5f;
        for (int i = 0; i < N; ++i) outBuf[c][(pos + i) % N] += work[i] * win[i] * g;
    }
};

// ===================================================================== Tiempo espectral (congelar y delay por frecuencia)
class SpectralTime : public FxBase
{
public:
    SpectralTime() : FxBase ("Tiempo espectral", "Creativos y espectrales", layout(), 6.0, false, true, false)
    {
        stft.onFrame = [this] (cpx* s, int c) { frame (s, c); };
    }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::toggle (l, "freeze", "Congelar", false);
        P::pct (l, "fadein", "Entrada del congelado", 0.0f, 100.0f, 20.0f);
        P::toggle (l, "sync", "Delay al tempo", false);
        P::choice (l, "div", "División"_u, RacoonProcessor::divisionNames(), 7);
        P::ms (l, "time", "Tiempo del delay", 20.0f, 2000.0f, 300.0f, 300.0f);
        P::pct (l, "feedback", "Repeticiones", 0.0f, 95.0f, 50.0f);
        P::flt (l, "tilt", "Inclinación (agudos antes o después)"_u, P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::pct (l, "spray", "Dispersión"_u, 0.0f, 100.0f, 0.0f);
        P::flt (l, "shift", "Corrimiento por repetición"_u, P::range (-24.0f, 24.0f), 0.0f, " st", 1);
        P::hz (l, "maskhz", "Solo arriba de", 20.0f, 10000.0f, 20.0f);
        P::pct (l, "dmix", "Volumen del delay", 0.0f, 100.0f, 70.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 50.0f);
        return l;
    }
    int maxFrames = 400;   // about 4.3 s of frames, at any sample rate
    void prepare (double rate, int) override
    {
        maxFrames = jmax (400, (int) std::ceil (4.27 * rate / Stft::hop));
        stft.prepare();
        for (auto& h : hist) h.assign ((size_t) maxFrames * Stft::bins, cpx {});
        setLatencySamples (Stft::latency());
        reset();
    }
    void reset() override
    {
        stft.reset();
        for (auto& h : hist) std::fill (h.begin(), h.end(), cpx {});
        frameIdx[0] = frameIdx[1] = 0;
        for (auto& f : frozenMag) std::fill (std::begin (f), std::end (f), 0.0f);
        for (auto& p : frozenPh) std::fill (std::begin (p), std::end (p), 0.0f);
        freezeGain = 0.0f;
    }
    void frame (cpx* s, int c)
    {
        const int bins = Stft::bins;
        auto& h = hist[c];
        const int fi = frameIdx[c];
        const int maskBin = (int) (maskHz / sr * Stft::N);
        // delay per bin (in frames) with tilt and spray
        for (int k = 0; k < bins; ++k)
        {
            if (k < maskBin) continue;   // below the mask the sound passes untouched
            const float t = (float) k / bins;
            float d = baseFrames * (1.0f + tilt * (t - 0.5f) * 1.6f);
            if (spray > 0.0f) d += spray * baseFrames * 0.5f * sprayOff[k];
            const int di = jlimit (1, maxFrames - 1, (int) std::round (d));
            int src = k;
            if (shiftRatio != 1.0f) src = (int) std::round (k / shiftRatio);
            const cpx delayed = (src >= 0 && src < bins) ? h[(size_t) (((fi - di + maxFrames) % maxFrames) * bins + src)] : cpx {};
            h[(size_t) (fi * bins + k)] = s[k] + delayed * fb;
            cpx wet = delayed * dmix;
            // freeze: hold the magnitudes and keep the phases turning at each bin's own speed
            if (freezing)
            {
                if (! wasFrozenFrame[c]) { frozenMag[c][k] = std::abs (s[k]); frozenPh[c][k] = std::arg (s[k]); }
                frozenPh[c][k] += (float) (twoPi * k * Stft::hop / Stft::N);
                wet += std::polar (frozenMag[c][k] * freezeGain, frozenPh[c][k]);
            }
            s[k] = s[k] * (1.0f - mixWet) + wet * mixWet;
        }
        wasFrozenFrame[c] = freezing;
        frameIdx[c] = (fi + 1) % maxFrames;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        const double sec = value ("sync") > 0.5f ? syncSeconds ((int) value ("div"), bpm()) : value ("time") * 0.001;
        baseFrames = (float) (sec * sr / Stft::hop);
        fb = value ("feedback") * 0.01f;
        tilt = value ("tilt") * 0.01f;
        spray = value ("spray") * 0.01f;
        shiftRatio = (float) std::pow (2.0, value ("shift") / 12.0);
        maskHz = value ("maskhz");
        dmix = value ("dmix") * 0.01f;
        mixWet = value ("mix") * 0.01f;
        freezing = value ("freeze") > 0.5f;
        const float fadeStep = (float) Stft::hop / (float) (sr * (0.02 + value ("fadein") * 0.01 * 2.0));
        if (sprayOff[1] == 0.0f) for (int k = 0; k < Stft::bins; ++k) sprayOff[k] = rng.nextBipolar();
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < ch; ++c) b.setSample (c, i, stft.push (c, b.getSample (c, i)));
            stft.advance (ch);
            if (i % Stft::hop == 0) freezeGain = freezing ? jmin (1.0f, freezeGain + fadeStep) : 0.0f;
        }
    }
    String displayText() const override { return freezing ? "CONGELADO" : ""; }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Congelar", { { "freeze", "Congelar", "Detiene el sonido en el aire y lo sostiene."_u }, { "fadein", "Entrada", "Qué tan suave aparece lo congelado."_u },
                            { "mix", "Mezcla", "Cuánto efecto se oye."_u } } },
            { "Delay espectral", { { "time", "Tiempo", "Eco de cada frecuencia."_u }, { "feedback", "Repeticiones", ""_u },
                                   { "tilt", "Inclinación"_u, "Los agudos llegan antes o después que los graves: barridos."_u },
                                   { "spray", "Dispersión"_u, "Cada frecuencia con un tiempo distinto: nube."_u },
                                   { "shift", "Corrimiento", "Cada repetición sube o baja de afinación."_u }, { "dmix", "Volumen", ""_u } } },
            { "Más"_u, { { "sync", "Al tempo", ""_u }, { "div", "División"_u, ""_u }, { "maskhz", "Solo arriba de", "Deja los graves sin efecto."_u } }, true },
        };
        return s;
    }
private:
    Stft stft;
    std::vector<cpx> hist[2];
    int frameIdx[2] {};
    float frozenMag[2][Stft::bins] {}, frozenPh[2][Stft::bins] {}, sprayOff[Stft::bins] {};
    bool wasFrozenFrame[2] {}, freezing = false;
    float baseFrames = 20.0f, fb = 0.5f, tilt = 0.0f, spray = 0.0f, shiftRatio = 1.0f, maskHz = 20.0f, dmix = 0.7f, mixWet = 0.5f, freezeGain = 0.0f;
    racoon::dsp::Rng rng;
};

// ===================================================================== Resonador espectral
class SpectralResonator : public FxBase
{
public:
    SpectralResonator() : FxBase ("Resonador espectral", "Creativos y espectrales", layout(), 8.0, false, true, false)
    {
        wantsMidiAsEffect = true;
        notes.ensureStorageAllocated (8);   // no allocation on the audio thread
        stft.onFrame = [this] (cpx* s, int c) { frame (s, c); };
    }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::hz (l, "freq", "Frecuencia", 30.0f, 2000.0f, 110.0f);
        P::toggle (l, "midi", "Seguir notas MIDI", false);
        P::flt (l, "decay", "Caída"_u, P::range (0.05f, 20.0f, 2.0f), 2.0f, " s", 2);
        P::flt (l, "stretch", "Estirar armónicos"_u, P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::flt (l, "shift", "Corrimiento", P::range (-24.0f, 24.0f), 0.0f, " st", 1);
        P::pct (l, "width", "Ancho de los armónicos"_u, 1.0f, 100.0f, 25.0f);
        P::integer (l, "partials", "Armónicos"_u, 1, 64, 24);
        P::pct (l, "damp", "Amortiguar agudos", 0.0f, 100.0f, 30.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 60.0f);
        return l;
    }
    void prepare (double, int) override { stft.prepare(); setLatencySamples (Stft::latency()); reset(); }
    void reset() override { stft.reset(); for (auto& r : res) std::fill (std::begin (r), std::end (r), 0.0f); for (auto& p : ph) std::fill (std::begin (p), std::end (p), 0.0f); notes.clear(); }
    void buildMask()
    {
        std::fill (std::begin (mask), std::end (mask), 0.0f);
        float roots[8];
        int numRoots = 0;
        if (value ("midi") > 0.5f && ! notes.isEmpty()) for (int nn : notes) { if (numRoots < 8) roots[numRoots++] = (float) MidiMessage::getMidiNoteInHertz (nn); }
        else roots[numRoots++] = value ("freq");
        const float stretch = value ("stretch") * 0.01f * 0.3f, shiftR = (float) std::pow (2.0, value ("shift") / 12.0);
        const int parts = (int) value ("partials");
        const float w = 0.3f + value ("width") * 0.01f * 4.0f;     // half width in bins
        const float damp = value ("damp") * 0.01f;
        for (int r = 0; r < numRoots; ++r)
            for (int h = 1; h <= parts; ++h)
            {
                const float f = roots[r] * shiftR * std::pow ((float) h, 1.0f + stretch);
                const float bin = f / (float) sr * Stft::N;
                if (bin >= Stft::bins - 2) break;
                const float amp = 1.0f / (1.0f + damp * (h - 1) * 0.3f);
                for (int k = jmax (1, (int) (bin - w * 3)); k <= jmin (Stft::bins - 1, (int) (bin + w * 3) + 1); ++k)
                {
                    const float d = (k - bin) / w;
                    mask[k] = jmax (mask[k], amp * std::exp (-0.5f * d * d));
                }
            }
    }
    void frame (cpx* s, int c)
    {
        for (int k = 0; k < Stft::bins; ++k)
        {
            const float m = std::abs (s[k]) * mask[k];
            // the resonance follows the input up at once and dies with the decay time
            res[c][k] = jmax (m, res[c][k] * decayPerFrame);
            ph[c][k] += (float) (twoPi * k * Stft::hop / Stft::N);
            if (ph[c][k] > 1000.0f) ph[c][k] = std::fmod (ph[c][k], (float) twoPi);
            const cpx wet = m > res[c][k] * 0.5f && m > 1.0e-6f ? s[k] / std::abs (s[k]) * res[c][k] : std::polar (res[c][k], ph[c][k]);
            if (m > 1.0e-6f) ph[c][k] = std::arg (s[k]);
            s[k] = s[k] * (1.0f - mixWet) + wet * mixWet * 1.5f;
        }
    }
    void process (AudioBuffer<float>& b, MidiBuffer& midi) override
    {
        for (const auto ev : midi)
        {
            const auto m = ev.getMessage();
            if (m.isNoteOn()) { if (notes.size() < 6) notes.addIfNotAlreadyThere (m.getNoteNumber()); }
            else if (m.isNoteOff()) notes.removeFirstMatchingValue (m.getNoteNumber());
        }
        buildMask();
        decayPerFrame = (float) std::pow (10.0, -3.0 * Stft::hop / (sr * value ("decay")));
        mixWet = value ("mix") * 0.01f;
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < ch; ++c) b.setSample (c, i, stft.push (c, b.getSample (c, i)));
            stft.advance (ch);
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Resonador", { { "freq", "Frecuencia", "La nota en la que resuena todo lo que entra."_u }, { "decay", "Caída"_u, "Cuánto se sostiene."_u },
                             { "partials", "Armónicos"_u, "Cuántos armónicos de esa nota."_u }, { "width", "Ancho", "Finos suenan a cristal, anchos a coro."_u },
                             { "midi", "MIDI", "Toca las notas con un teclado: hasta seis a la vez."_u }, { "mix", "Mezcla", ""_u } } },
            { "Color", { { "stretch", "Estirar", "Separa o junta los armónicos: campanas y metales."_u }, { "shift", "Corrimiento", "Mueve todo hacia arriba o abajo."_u },
                         { "damp", "Amortiguar", "Los armónicos altos más suaves."_u } }, true },
        };
        return s;
    }
private:
    Stft stft;
    float mask[Stft::bins] {}, res[2][Stft::bins] {}, ph[2][Stft::bins] {};
    float decayPerFrame = 0.9f, mixWet = 0.5f;
    Array<int> notes;
};

// ===================================================================== Transpositor (pitch, frecuencia, anillo)
struct Hilbert
{
    // two chains of allpasses 90 degrees apart (Niemitalo's coefficients)
    float a[4] { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    float bq[4] { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    float xa[4][2] {}, ya[4][2] {}, xb[4][2] {}, yb[4][2] {}, prevB = 0.0f;
    static float stage (float c, float x, float* xs, float* ys)
    {
        const float y = c * c * (x + ys[1]) - xs[1];
        xs[1] = xs[0]; xs[0] = x; ys[1] = ys[0]; ys[0] = y;
        return y;
    }
    void process (float x, float& re, float& im)
    {
        float u = x, v = x;
        for (int k = 0; k < 4; ++k) { u = stage (a[k], u, xa[k], ya[k]); v = stage (bq[k], v, xb[k], yb[k]); }
        re = u; im = prevB; prevB = v;
    }
    void reset() { *this = Hilbert(); }
};

class Shifter : public FxBase
{
public:
    Shifter() : FxBase ("Transpositor", "Creativos y espectrales", layout(), 2.0, false, true) {}
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "mode", "Modo", { "Afinación"_u, "Frecuencia", "Anillo" }, 0);
        P::flt (l, "pitch", "Semitonos", P::range (-48.0f, 48.0f, 0.0f, 1.0f), 0.0f, " st", 0);
        P::flt (l, "fine", "Ajuste fino", P::range (-50.0f, 50.0f), 0.0f, " cents", 0);
        P::flt (l, "freq", "Frecuencia (modos frecuencia y anillo)", P::range (-5000.0f, 5000.0f, 0.0f), 100.0f, " Hz", 1);
        P::ms (l, "window", "Ventana", 10.0f, 200.0f, 60.0f, 60.0f);
        P::flt (l, "spread", "Diferencia entre lados", P::range (-100.0f, 100.0f), 0.0f, " cents", 0);
        P::ms (l, "delay", "Delay", 0.0f, 2000.0f, 0.0f, 300.0f);
        P::pct (l, "feedback", "Realimentación"_u, 0.0f, 95.0f, 0.0f);
        P::flt (l, "lforate", "LFO: velocidad", P::range (0.01f, 20.0f, 1.0f), 1.0f, " Hz", 2);
        P::flt (l, "lfoamt", "LFO: cantidad", P::range (0.0f, 12.0f), 0.0f, " st", 2);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    void prepare (double rate, int) override
    {
        for (auto& d : grain) d.prepare ((int) (rate * 0.5));
        for (auto& d : fbLine) d.prepare ((int) (rate * 2.1));
        reset();
    }
    void reset() override
    {
        for (auto& d : grain) d.clear();
        for (auto& d : fbLine) d.clear();
        for (auto& h : hil) h.reset();
        gph = 0.0; osc = 0.0; lfo = 0.0; fbv[0] = fbv[1] = 0.0f;
    }
    void process (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int mode = (int) value ("mode");
        const float baseSt = value ("pitch") + value ("fine") * 0.01f;
        const float spread = value ("spread") * 0.01f;
        const double freq = value ("freq");
        const double win = value ("window") * 0.001 * sr;
        const float dly = value ("delay") * 0.001f * (float) sr, fbk = value ("feedback") * 0.01f, mix = value ("mix") * 0.01f;
        const double lfoInc = value ("lforate") / sr;
        const float lfoAmt = value ("lfoamt");
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
            const float lfoV = (float) std::sin (twoPi * lfo) * lfoAmt;
            osc += freq / sr; osc -= std::floor (osc);
            for (int c = 0; c < ch; ++c)
            {
                const float x = b.getSample (c, i);
                float in = x + fbv[c] * fbk;
                float y = 0.0f;
                if (mode == 0)
                {
                    // two read heads half a window apart, each with a sine window: a smooth, simple pitch shifter
                    const float st = baseSt + lfoV + (c == 0 ? -spread * 0.5f : spread * 0.5f);
                    const double ratio = std::pow (2.0, st / 12.0);
                    grain[c].push (in);
                    const double rate = 1.0 - ratio;     // how fast the read point moves away from the write head
                    double& p = c == 0 ? gph : gph2;
                    p += rate / win;
                    p -= std::floor (p);
                    for (int head = 0; head < 2; ++head)
                    {
                        double ph = p + head * 0.5; ph -= std::floor (ph);
                        const float w = (float) std::sin (MathConstants<double>::pi * ph);
                        y += grain[c].tap ((float) (2.0 + ph * win)) * w * w;
                    }
                }
                else
                {
                    float re, im;
                    hil[c].process (in, re, im);
                    const double phs = twoPi * (osc + (c == 1 ? spread * 0.25 : 0.0));
                    if (mode == 1) y = re * (float) std::cos (phs) - im * (float) std::sin (phs);   // single sideband: every frequency moves by the same Hz
                    else y = in * (float) std::cos (phs);                                             // ring: both sidebands
                }
                if (dly > 1.0f || fbk > 0.0f)
                {
                    fbLine[c].push (y);
                    const float d = fbLine[c].tap (jmax (2.0f, dly));
                    fbv[c] = d;
                    if (dly > 1.0f) y = d;
                }
                else fbv[c] = 0.0f;
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Transpositor", { { "mode", "Modo", "Afinación cambia la nota; frecuencia y anillo hacen sonidos metálicos y robóticos."_u },
                                { "pitch", "Semitonos", "Cuánto sube o baja."_u }, { "fine", "Fino", ""_u }, { "freq", "Hz", "Cuánto se corre en los modos frecuencia y anillo."_u },
                                { "mix", "Mezcla", ""_u } } },
            { "Más"_u, { { "window", "Ventana", "Corta para voces y percusión, larga para sonidos sostenidos."_u },
                       { "spread", "Estéreo"_u, "Desafina un lado contra el otro: ensancha."_u },
                       { "delay", "Delay", "Retrasa el resultado."_u }, { "feedback", "Realimentación"_u, "Con delay crea escaleras de notas."_u },
                       { "lforate", "LFO vel", ""_u }, { "lfoamt", "LFO cant", "Vibrato."_u } }, true },
        };
        return s;
    }
private:
    racoon::dsp::DelayLine grain[2], fbLine[2];
    Hilbert hil[2];
    double gph = 0.0, gph2 = 0.0, osc = 0.0, lfo = 0.0;
    float fbv[2] {};
};

// ===================================================================== Vocoder
class Vocoder : public FxBase
{
public:
    Vocoder() : FxBase ("Vocoder", "Creativos y espectrales", layout(), 0.5, true, false, false) { wantsMidiAsEffect = true; }
    static AudioProcessorValueTreeState::ParameterLayout layout()
    {
        P::Layout l;
        P::choice (l, "carrier", "Portadora", { "Ruido", "Sierra (nota)", "Sidechain", "Modulador", "Sigue el tono" }, 1);
        P::integer (l, "note", "Nota de la sierra", 24, 84, 45);
        P::toggle (l, "midi", "La sierra sigue notas MIDI", true);
        P::integer (l, "bands", "Bandas", 4, 40, 20);
        P::hz (l, "lo", "Banda más grave"_u, 40.0f, 1000.0f, 80.0f);
        P::hz (l, "hi", "Banda más aguda"_u, 2000.0f, 18000.0f, 12000.0f);
        P::pct (l, "bw", "Ancho de banda", 10.0f, 200.0f, 100.0f);
        P::flt (l, "formant", "Formantes", P::range (-12.0f, 12.0f), 0.0f, " st", 1);
        P::ms (l, "attack", "Ataque", 0.1f, 100.0f, 3.0f, 5.0f);
        P::ms (l, "release", "Relajación"_u, 1.0f, 2000.0f, 60.0f, 100.0f);
        P::pct (l, "unvoiced", "Consonantes", 0.0f, 100.0f, 20.0f);
        P::toggle (l, "enhance", "Igualar portadora", true);
        P::db (l, "gain", "Ganancia", -24.0f, 24.0f, 6.0f);
        P::pct (l, "mix", "Mezcla", 0.0f, 100.0f, 100.0f);
        return l;
    }
    static constexpr int maxBands = 40;
    void reset() override
    {
        for (auto& b : mod) for (auto& f : b) f.reset();
        for (auto& b : car) for (auto& f : b) f.reset();
        for (auto& e : env) e = 0.0f;
        for (auto& e : carEnv) e = 0.0f;
        saw = 0.0; hpU.reset(); midiNote = -1; pitchHz = 110.0f; detectCount = 0; histPos = 0;
        std::fill (std::begin (hist), std::end (hist), 0.0f);
    }
    void design()
    {
        const int nb = (int) value ("bands");
        const double lo = value ("lo"), hi = jmax ((double) value ("hi"), lo * 2.0);
        const double q = jmax (0.5, nb / std::log2 (hi / lo) * 1.4 / (value ("bw") * 0.01));
        const double fr = std::pow (2.0, value ("formant") / 12.0);
        for (int k = 0; k < nb; ++k)
        {
            const double f = lo * std::pow (hi / lo, (k + 0.5) / nb);
            for (int s = 0; s < 2; ++s)
            {
                mod[k][s].set (Biquad::bandpass, sr, f, q);
                car[k][s].set (Biquad::bandpass, sr, jlimit (20.0, sr * 0.45, f * fr), q);
            }
        }
        numBands = nb;
        hpU.set (Biquad::highpass, sr, 5000.0, 0.707);
    }
    void process (AudioBuffer<float>& b, MidiBuffer& midi) override
    {
        for (const auto ev : midi)
        {
            const auto m = ev.getMessage();
            if (m.isNoteOn()) midiNote = m.getNoteNumber();
            else if (m.isNoteOff() && m.getNoteNumber() == midiNote) midiNote = -1;
        }
        design();
        const int carrier = (int) value ("carrier");
        const float a = dsp2::coeffMs (sr, value ("attack")), r = dsp2::coeffMs (sr, value ("release"));
        const float cUp = dsp2::coeffMs (sr, 2.27), cDown = dsp2::coeffMs (sr, 45.35);   // carrier flattening, in time (was 0.99 / 0.9995 per sample)
        const float unv = value ("unvoiced") * 0.01f, gain = toGain (value ("gain")) * 3.0f, mix = value ("mix") * 0.01f;
        const bool enhance = value ("enhance") > 0.5f;
        const bool useMidi = value ("midi") > 0.5f && midiNote >= 0;
        double hz = 440.0 * std::pow (2.0, ((useMidi ? midiNote : (int) value ("note")) - 69) / 12.0);
        const int n = b.getNumSamples(), ch = jmin (2, b.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float xl = b.getSample (0, i), xr = ch > 1 ? b.getSample (1, i) : xl;
            const float m = 0.5f * (xl + xr);
            // pitch tracking: look at the last 2048 samples every 1024
            hist[histPos] = m; histPos = (histPos + 1) % histSize;
            if (carrier == 4 && ++detectCount >= 1024)
            {
                detectCount = 0;
                float tmp[histSize];
                for (int k = 0; k < histSize; ++k) tmp[k] = hist[(histPos + k) % histSize];
                const float f = tunerDetect (tmp, histSize, sr);
                if (f > 40.0f && f < 1500.0f) pitchHz = f;
            }
            if (carrier == 4) hz = pitchHz;
            // carrier signal per side
            float cs[2];
            switch (carrier)
            {
                case 0: cs[0] = rng.nextBipolar(); cs[1] = rng.nextBipolar(); break;
                case 2:
                {
                    const float* sl = sidechain (0); const float* sr2 = sidechain (1);
                    cs[0] = sl ? sl[i] : 0.0f; cs[1] = sr2 ? sr2[i] : cs[0];
                    break;
                }
                case 3: cs[0] = xl; cs[1] = xr; break;
                default:
                {
                    const double dt = hz / sr;
                    saw += dt; if (saw >= 1.0) saw -= 1.0;
                    // PolyBLEP (Välimäki & Huovilainen 2007): the jump of the saw is rounded over two samples, so the
                    // high notes stop folding back as metallic tones
                    double v = 2.0 * saw - 1.0;
                    if (saw < dt) { const double t = saw / dt; v -= t + t - t * t - 1.0; }
                    else if (saw > 1.0 - dt) { const double t = (saw - 1.0) / dt; v -= t * t + t + t + 1.0; }
                    cs[0] = cs[1] = (float) v * 0.7f;
                    break;
                }
            }
            float out[2] {};
            for (int k = 0; k < numBands; ++k)
            {
                const float mb = mod[k][0].process (mod[k][1].process (m, 0), 0);
                const float e = std::abs (mb);
                env[k] = e > env[k] ? e + (env[k] - e) * a : e + (env[k] - e) * r;
                for (int c = 0; c < ch; ++c)
                {
                    float cb = car[k][0].process (car[k][1].process (cs[c], c), c);
                    if (enhance)
                    {
                        // flatten the carrier: every band carries the same level, only the voice shapes it
                        const float ce = std::abs (cb);
                        carEnv[k * 2 + c] = ce > carEnv[k * 2 + c] ? ce + (carEnv[k * 2 + c] - ce) * cUp : ce + (carEnv[k * 2 + c] - ce) * cDown;
                        cb /= carEnv[k * 2 + c] + 0.02f;
                        cb *= 0.1f;
                    }
                    out[c] += cb * env[k];
                }
            }
            // consonants (s, t, f) come straight from the voice's highs, so the words stay clear
            const float u = hpU.process (m, 0) * unv;
            for (int c = 0; c < ch; ++c)
            {
                const float y = std::tanh ((out[c] * gain * 4.0f / std::sqrt ((float) numBands)) + u);
                const float x = c == 0 ? xl : xr;
                b.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
        }
    }
    EditorSpec editorSpec() const override
    {
        EditorSpec s;
        s.sections = {
            { "Vocoder", { { "carrier", "Portadora", "El sonido que va a hablar: ruido, una sierra, otra pista por sidechain, la misma señal o una sierra que sigue tu voz."_u },
                           { "note", "Nota", "La nota de la sierra si no llega MIDI."_u }, { "midi", "MIDI", "Toca la sierra con el teclado."_u },
                           { "bands", "Bandas", "Más bandas, más inteligible."_u }, { "formant", "Formantes", "Voz más grande o más chica sin cambiar la nota."_u },
                           { "mix", "Mezcla", ""_u } } },
            { "Detalles", { { "lo", "Grave", ""_u }, { "hi", "Agudo", ""_u }, { "bw", "Ancho", "Bandas anchas suenan más suave, estrechas más robot."_u },
                            { "attack", "Ataque", ""_u }, { "release", "Relajación"_u, ""_u }, { "unvoiced", "Consonantes", "Deja pasar las eses y tes de la voz."_u },
                            { "enhance", "Igualar", "Más claro con portadoras opacas."_u }, { "gain", "Ganancia", ""_u } }, true },
        };
        return s;
    }
private:
    Biquad mod[maxBands][2], car[maxBands][2], hpU;
    float env[maxBands] {}, carEnv[maxBands * 2] {};
    int numBands = 0, midiNote = -1, detectCount = 0, histPos = 0;
    static constexpr int histSize = 2048;
    float hist[histSize] {};
    float pitchHz = 110.0f;
    double saw = 0.0;
    racoon::dsp::Rng rng;
};
}

void registerSpectral (std::vector<FxDef>& v)
{
    auto add = [&v] (const char* id, const String& name, const String& detail, std::function<std::unique_ptr<RacoonProcessor>()> make, StringArray keys)
    { v.push_back ({ id, name, "Creativos y espectrales", detail, Kind::effect, std::move (make), keys }); };
    add ("resonador-espectral", "Resonador espectral", "Hace que cualquier sonido cante en una nota y sus armónicos."_u, [] { return std::make_unique<SpectralResonator>(); },
         { "freq", "decay", "partials", "stretch", "width", "mix" });
    add ("tiempo-espectral", "Tiempo espectral", "Congela el sonido o retrasa cada frecuencia por separado."_u, [] { return std::make_unique<SpectralTime>(); },
         { "freeze", "time", "feedback", "tilt", "spray", "mix" });
    add ("transpositor", "Transpositor", "Sube o baja la afinación, o corre las frecuencias para sonidos metálicos."_u, [] { return std::make_unique<Shifter>(); },
         { "mode", "pitch", "fine", "freq", "window", "mix" });
    add ("vocoder", "Vocoder", "Hace hablar a un sintetizador con tu voz."_u, [] { return std::make_unique<Vocoder>(); },
         { "carrier", "note", "bands", "formant", "unvoiced", "mix" });
}
}

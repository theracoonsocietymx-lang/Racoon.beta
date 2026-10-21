#include "PorcupineProcessor.h"
#include "PorcupineEditor.h"

namespace racoon::porcupine
{
using namespace juce;

enum Type { peak, lowShelf, highShelf, highPass, lowPass, notchT, bandPass };

const StringArray& Processor::typeNames()
{
    static StringArray a { "Campana", "Shelf grave", "Shelf agudo", "Paso alto", "Paso bajo", "Notch", "Paso banda" };
    return a;
}

std::complex<double> Biquad::response (double w) const
{
    const std::complex<double> z1c = std::exp (std::complex<double> (0.0, -w)), z2c = z1c * z1c;
    return (b0 + b1 * z1c + b2 * z2c) / (1.0 + a1 * z1c + a2 * z2c);
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    const float freqs[numBands] { 30, 100, 250, 800, 2500, 6000, 10000, 18000 };
    const int types[numBands] { highPass, lowShelf, peak, peak, peak, peak, highShelf, lowPass };
    for (int b = 0; b < numBands; ++b)
    {
        const String n = "B" + String (b + 1) + " ";
        P::toggle (l, bandId (b, "on"), n + "Activa", b != 0 && b != numBands - 1);
        P::choice (l, bandId (b, "type"), n + "Tipo", typeNames(), types[b]);
        P::hz (l, bandId (b, "freq"), n + "Frecuencia", 10.0f, 24000.0f, freqs[b]);
        P::db (l, bandId (b, "gain"), n + "Ganancia", -24.0f, 24.0f, 0.0f);
        P::flt (l, bandId (b, "q"), n + "Q", P::range (0.1f, 36.0f, 1.0f), 0.71f, {}, 2);
        P::choice (l, bandId (b, "slope"), n + "Pendiente", { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
        P::toggle (l, bandId (b, "dyn"), n + "Dinámica"_u, false);
        P::flt (l, bandId (b, "thr"), n + "Umbral", P::range (-60.0f, 0.0f), -24.0f, "dB");
        P::flt (l, bandId (b, "range"), n + "Rango", P::range (0.0f, 24.0f), 6.0f, "dB");
    }
    P::ms (l, "dynatk", "Ataque dinámico"_u, 0.1f, 200.0f, 5.0f, 10.0f);
    P::ms (l, "dynrel", "Relajación dinámica"_u, 10.0f, 2000.0f, 150.0f, 200.0f);
    P::choice (l, "phase", "Fase", { "Mínima"_u, "Lineal" }, 0);
    P::choice (l, "analyzer", "Analizador", { "Apagado", "Antes", "Después"_u, "Antes y después"_u }, 3);
    P::choice (l, "fftsize", "Resolución"_u, { "1024", "2048", "4096", "8192", "16384" }, 2);
    P::integer (l, "solo", "Solo de banda", 0, numBands, 0);
    P::db (l, "output", "Salida", -24.0f, 24.0f, 0.0f);
    return l;
}

// ===================================================================== linear phase designer
class Processor::Designer : public Thread
{
public:
    explicit Designer (Processor& p) : Thread ("Porcupine FIR"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (60);
            if (proc.sr <= 0.0) continue;
            int64 hash = 0;
            for (int b = 0; b < numBands; ++b)
            {
                auto s = proc.band (b);
                hash = hash * 31 + (s.on ? 1 : 0) + s.type * 3 + roundToInt (s.freq * 10) * 7 + roundToInt (s.gain * 100) * 11 + roundToInt (s.q * 1000) * 13 + s.slope * 17;
            }
            hash += (int64) proc.sr;
            if (hash == lastHash) continue;
            lastHash = hash;

            constexpr int order = 13, N = 1 << order, taps = 4096;
            juce::dsp::FFT fft (order);
            std::vector<float> spec ((size_t) N * 2, 0.0f);
            for (int k = 0; k <= N / 2; ++k)
            {
                const double f = (double) k * proc.sr / N;
                spec[(size_t) k * 2] = (float) proc.magnitudeAt (jmax (1.0, f), false);   // zero phase
                spec[(size_t) k * 2 + 1] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (spec.data());
            AudioBuffer<float> ir (1, taps);
            for (int i = 0; i < taps; ++i)
            {
                const int src = (i - taps / 2 + N) % N;
                const double w = 0.35875 - 0.48829 * std::cos (MathConstants<double>::twoPi * i / (taps - 1))
                               + 0.14128 * std::cos (2.0 * MathConstants<double>::twoPi * i / (taps - 1))
                               - 0.01168 * std::cos (3.0 * MathConstants<double>::twoPi * i / (taps - 1));
                ir.setSample (0, i, spec[(size_t) src] * (float) w);
            }
            proc.linear.loadImpulseResponse (std::move (ir), proc.sr, juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            ready = true;
        }
    }
    Processor& proc;
    int64 lastHash = 0;
    std::atomic<bool> ready { false };
};

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "EQ de ocho bandas"_u, "EQ"_u, Colour (0xffd9c452), PluginKind::effect, 1, 0.0, false, true },
                       layout())
{
    const char* names[9] { "on", "type", "freq", "gain", "q", "slope", "dyn", "thr", "range" };
    for (int b = 0; b < numBands; ++b)
        for (int k = 0; k < 9; ++k) ptr[b][k] = param (bandId (b, names[k]));
    fifoPre.assign (fifoSize, 0.0f);
    fifoPost.assign (fifoSize, 0.0f);
}

Processor::~Processor()
{
    if (designer) designer->stopThread (2000);
}

BandSettings Processor::band (int b) const
{
    return { ptr[b][0]->load() > 0.5f, (int) ptr[b][1]->load(), ptr[b][2]->load(), ptr[b][3]->load(), ptr[b][4]->load(), (int) ptr[b][5]->load() };
}

// ===================================================================== design
static void rbj (Biquad& q, double a0, double a1, double a2, double b0, double b1, double b2)
{
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}

int Processor::designBand (const BandSettings& s, double rate, Biquad* out)
{
    if (! s.on) return 0;
    const double f = jlimit (10.0, rate * 0.45, (double) s.freq);
    const double w0 = MathConstants<double>::twoPi * f / rate;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double Q = jlimit (0.1, 36.0, (double) s.q);
    const double A = std::pow (10.0, s.gain / 40.0);
    auto alphaFor = [sw] (double q) { return sw / (2.0 * q); };
    const double alpha = alphaFor (Q);

    switch (s.type)
    {
        case peak:
            rbj (out[0], 1 + alpha / A, -2 * cw, 1 - alpha / A, 1 + alpha * A, -2 * cw, 1 - alpha * A);
            return 1;
        case lowShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            rbj (out[0], (A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa,
                 A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa));
            return 1;
        }
        case highShelf:
        {
            const double sa = 2.0 * std::sqrt (A) * alpha;
            rbj (out[0], (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa,
                 A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa));
            return 1;
        }
        case notchT:
            rbj (out[0], 1 + alpha, -2 * cw, 1 - alpha, 1, -2 * cw, 1);
            return 1;
        case bandPass:
            rbj (out[0], 1 + alpha, -2 * cw, 1 - alpha, alpha, 0, -alpha);
            return 1;
        case highPass:
        case lowPass:
        {
            const bool hp = s.type == highPass;
            if (s.slope == 0)
            {
                const double K = std::tan (w0 * 0.5);
                auto& b = out[0];
                if (hp) { b.b0 = 1.0 / (1.0 + K); b.b1 = -b.b0; }
                else    { b.b0 = K / (1.0 + K); b.b1 = b.b0; }
                b.b2 = 0.0; b.a1 = (K - 1.0) / (K + 1.0); b.a2 = 0.0;
                return 1;
            }
            static const double q4[] { 0.5412, 1.3066 };
            static const double q8[] { 0.5098, 0.6013, 0.9000, 2.5629 };
            const int nst = s.slope == 1 ? 1 : (s.slope == 2 ? 2 : 4);
            for (int k = 0; k < nst; ++k)
            {
                double qq = nst == 1 ? Q : (nst == 2 ? q4[k] : q8[k]);
                if (nst > 1 && k == nst - 1) qq *= Q / 0.7071;    // the user Q shapes the last stage (resonant edge)
                const double a = alphaFor (jlimit (0.1, 36.0, qq));
                if (hp) rbj (out[k], 1 + a, -2 * cw, 1 - a, (1 + cw) / 2, -(1 + cw), (1 + cw) / 2);
                else    rbj (out[k], 1 + a, -2 * cw, 1 - a, (1 - cw) / 2, 1 - cw, (1 - cw) / 2);
            }
            return nst;
        }
        default: return 0;
    }
}

float Processor::bandMagnitudeDb (int b, double freq) const
{
    Biquad tmp[4];
    const int n = designBand (band (b), sr, tmp);
    std::complex<double> h (1.0, 0.0);
    const double w = MathConstants<double>::twoPi * freq / sr;
    for (int k = 0; k < n; ++k) h *= tmp[k].response (w);
    return (float) (20.0 * std::log10 (jmax (1.0e-9, std::abs (h))));
}

double Processor::magnitudeAt (double freq, bool includeDynamic) const
{
    std::complex<double> h (1.0, 0.0);
    const double w = MathConstants<double>::twoPi * jmin (freq, sr * 0.5) / sr;
    for (int b = 0; b < numBands; ++b)
    {
        auto s = band (b);
        if (includeDynamic && ptr[b][6]->load() > 0.5f) s.gain -= std::abs (dynGainDb[b].load());
        Biquad tmp[4];
        const int n = designBand (s, sr, tmp);
        for (int k = 0; k < n; ++k) h *= tmp[k].response (w);
    }
    return std::abs (h);
}

// ===================================================================== processing
void Processor::prepareDsp (double rate, int block)
{
    juce::dsp::ProcessSpec spec { rate, (uint32) block, 2 };
    linear.prepare (spec);
    soloBuf.setSize (2, block);
    for (auto& d : latencyDry) d.prepare (4096 + block + 8);
    outGain.prepare (rate, 20.0);
    if (designer == nullptr) designer = std::make_unique<Designer> (*this);
    designer->lastHash = 0;
    if (! designer->isThreadRunning()) designer->startThread();
    resetDsp();
}

void Processor::resetDsp()
{
    for (int b = 0; b < numBands; ++b)
    {
        for (auto& s : stages[b]) s.reset();
        detector[b].reset();
        followers[b].env = 0.0f;
        dynDb[b] = 0.0f;
        current[b] = band (b);
    }
    linear.reset();
    outGain.reset (dsp::dbToGain (value ("output")));
    linearMode = (int) value ("phase") == 1;
    setLatencySamples (linearMode ? 2048 : 0);
    counter = 0;
    updateCoefficients (true);
}

void Processor::updateCoefficients (bool snap)
{
    for (int b = 0; b < numBands; ++b)
    {
        auto target = band (b);
        auto& c = current[b];
        if (snap || target.on != c.on || target.type != c.type || target.slope != c.slope)
        {
            if (target.type != c.type || target.slope != c.slope || target.on != c.on)
                for (auto& s : stages[b]) s.reset();
            c = target;
        }
        else
        {
            // glide towards the new settings so automation never clicks
            c.freq *= std::pow (target.freq / c.freq, 0.3f);
            c.gain += (target.gain - c.gain) * 0.3f;
            c.q += (target.q - c.q) * 0.3f;
        }
        auto eff = c;
        if (ptr[b][6]->load() > 0.5f) eff.gain -= dynDb[b];
        Biquad fresh[4];
        numStages[b] = designBand (eff, sr, fresh);
        for (int k = 0; k < numStages[b]; ++k)
        {
            auto& s = stages[b][k];
            s.b0 = fresh[k].b0; s.b1 = fresh[k].b1; s.b2 = fresh[k].b2; s.a1 = fresh[k].a1; s.a2 = fresh[k].a2;
        }
    }
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());

    const bool wantLinear = (int) value ("phase") == 1;
    if (wantLinear != linearMode)
    {
        linearMode = wantLinear;
        setLatencySamples (linearMode ? 2048 : 0);
        linear.reset();
    }
    outGain.target = dsp::dbToGain (value ("output"));
    const int solo = (int) value ("solo");

    // analyzer input (pre), mono
    int w = fifoWrite.load();
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < ch; ++c) m += buffer.getSample (c, i);
        fifoPre[(size_t) ((w + i) & (fifoSize - 1))] = m / (float) ch;
    }

    // dynamic bands: detector on the band region
    bool anyDyn = false;
    if (! linearMode)
    {
        followersSet (value ("dynatk"), value ("dynrel"));
        for (int b = 0; b < numBands; ++b)
        {
            if (ptr[b][6]->load() < 0.5f || ptr[b][0]->load() < 0.5f) { dynDb[b] = 0.0f; dynGainDb[b].store (0.0f); continue; }
            anyDyn = true;
            const float g = dsp::SVF::gFor (ptr[b][2]->load(), sr);
            const float k = 1.0f / jlimit (0.3f, 10.0f, ptr[b][4]->load());
            float env = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                for (int c = 0; c < ch; ++c) x += buffer.getSample (c, i);
                detector[b].process (x / (float) ch, g, k);
                env = followers[b].process (std::abs (detector[b].bp * k));
            }
            const float lvl = dsp::gainToDb (env);
            const float over = jlimit (0.0f, 1.0f, (lvl - ptr[b][7]->load()) / 6.0f);
            dynDb[b] = over * ptr[b][8]->load();
            dynGainDb[b].store (dynDb[b]);
        }
    }
    ignoreUnused (anyDyn);

    if (solo > 0)
    {
        // band solo: listen only to the region of that band
        const auto s = band (solo - 1);
        const float g = dsp::SVF::gFor (s.freq, sr), k = 1.0f / jlimit (0.3f, 10.0f, s.q);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
            {
                detector[0].process (buffer.getSample (c, i), g, k);
                buffer.setSample (c, i, detector[0].bp * k);
            }
    }
    else if (linearMode)
    {
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        linear.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
    else
    {
        for (int start = 0; start < n; start += 32)
        {
            updateCoefficients (false);
            const int len = jmin (32, n - start);
            for (int b = 0; b < numBands; ++b)
                for (int k = 0; k < numStages[b]; ++k)
                    for (int c = 0; c < ch; ++c)
                    {
                        auto* d = buffer.getWritePointer (c, start);
                        auto& st = stages[b][k];
                        for (int i = 0; i < len; ++i) d[i] = st.process (d[i], c);
                    }
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.next();
        float m = 0.0f;
        for (int c = 0; c < ch; ++c) { buffer.setSample (c, i, buffer.getSample (c, i) * g); m += buffer.getSample (c, i); }
        fifoPost[(size_t) ((w + i) & (fifoSize - 1))] = m / (float) ch;
    }
    fifoWrite.store ((w + n) & (fifoSize - 1));
}

void Processor::followersSet (float atk, float rel)
{
    for (auto& f : followers) f.set (sr, atk, rel);
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    auto B = [] (int b, const char* what) { return Processor::bandId (b - 1, what); };
    return {
        { "Plano", "Básicos"_u, {} },
        { "Limpiar graves", "Básicos"_u, { { B (1, "on"), 1 }, { B (1, "freq"), 80 }, { B (1, "slope"), 2 } } },
        { "Voz clara", "Voces", { { B (1, "on"), 1 }, { B (1, "freq"), 90 }, { B (3, "freq"), 300 }, { B (3, "gain"), -3 }, { B (3, "q"), 1.2f },
                                  { B (5, "freq"), 3000 }, { B (5, "gain"), 2.5f }, { B (7, "freq"), 11000 }, { B (7, "gain"), 3 } } },
        { "De-esser", "Voces", { { B (6, "freq"), 7000 }, { B (6, "q"), 2.5f }, { B (6, "dyn"), 1 }, { B (6, "thr"), -30 }, { B (6, "range"), 8 } } },
        { "Bombo con punch", "Batería"_u, { { B (2, "freq"), 60 }, { B (2, "gain"), 3 }, { B (3, "freq"), 350 }, { B (3, "gain"), -4 }, { B (3, "q"), 1.5f },
                                             { B (5, "freq"), 3500 }, { B (5, "gain"), 3 } } },
        { "Bajo definido", "Bajo", { { B (1, "on"), 1 }, { B (1, "freq"), 35 }, { B (3, "freq"), 220 }, { B (3, "gain"), -2.5f }, { B (4, "freq"), 900 }, { B (4, "gain"), 2 } } },
        { "Aire en el master", "Master", { { B (7, "freq"), 12000 }, { B (7, "gain"), 1.5f }, { "phase", 1 } } },
        { "Master lineal plano", "Master", { { "phase", 1 } } },
        { "Teléfono"_u, "Creativo", { { B (1, "on"), 1 }, { B (1, "freq"), 400 }, { B (1, "slope"), 3 }, { B (8, "on"), 1 }, { B (8, "freq"), 3000 }, { B (8, "slope"), 3 } } },
        { "Quitar zumbido 60 Hz", "Reparar", { { B (2, "type"), 5 }, { B (2, "freq"), 60 }, { B (2, "q"), 12 } } },
    };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

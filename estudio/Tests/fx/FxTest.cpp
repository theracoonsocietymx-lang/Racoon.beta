// Offline checks for Racoon's own effects: every processor is driven without a host or a window.
#include "engine/Common.h"
#include "fx/core/RacoonProcessor.h"
#include "fx/core/RacoonEditor.h"
#include "fx/NativeFx.h"

using namespace juce;
using namespace racoon;

int failures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++failures;
}

// ---------------------------------------------------------------- helpers shared by the plugin tests
namespace tu
{
MidiBuffer midiOut;

void fillNoise (AudioBuffer<float>& b, float level, int seed)
{
    Random r (seed);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * level);
}

void fillSine (AudioBuffer<float>& b, double freq, double sr, float level, double phase0)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = level * (float) std::sin (phase0 + MathConstants<double>::twoPi * freq * i / sr);
        for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, v);
    }
}

// runs a long signal through the processor in blocks; returns the output
AudioBuffer<float> render (AudioProcessor& p, const AudioBuffer<float>& in, int block, MidiBuffer* midi)
{
    AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
    out.clear();
    const int total = jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    AudioBuffer<float> io (total, block);
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        io.setSize (total, n, false, false, true);
        io.clear();
        for (int c = 0; c < jmin (total, in.getNumChannels()); ++c) io.copyFrom (c, 0, in, c, pos, n);
        MidiBuffer m;
        if (midi != nullptr)
            for (const auto ev : *midi)
                if (ev.samplePosition >= pos && ev.samplePosition < pos + n) m.addEvent (ev.getMessage(), ev.samplePosition - pos);
        p.processBlock (io, m);
        if (midi != nullptr && p.producesMidi())
            for (const auto ev : m) midiOut.addEvent (ev.getMessage(), ev.samplePosition + pos);
        for (int c = 0; c < jmin (in.getNumChannels(), io.getNumChannels()); ++c) out.copyFrom (c, pos, io, c, 0, n);
    }
    return out;
}

bool allFinite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

float peak (const AudioBuffer<float>& b, int start, int len)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) p = jmax (p, b.getMagnitude (c, start, jmin (len, b.getNumSamples() - start)));
    return p;
}

float rms (const AudioBuffer<float>& b, int start, int len)
{
    double s = 0; int k = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < jmin (b.getNumSamples(), start + len); ++i) { s += (double) b.getSample (c, i) * b.getSample (c, i); ++k; }
    return (float) std::sqrt (s / jmax (1, k));
}

float maxJump (const AudioBuffer<float>& b)
{
    float j = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 1; i < b.getNumSamples(); ++i) j = jmax (j, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return j;
}

// amplitude of one frequency (Goertzel), Hann window
float tone (const AudioBuffer<float>& b, int ch, int start, int len, double freq, double sr)
{
    const double w = MathConstants<double>::twoPi * freq / sr, cw = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0, wsum = 0;
    for (int i = 0; i < len; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (MathConstants<double>::twoPi * i / (len - 1));
        wsum += win;
        s0 = b.getSample (ch, start + i) * win + cw * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return (float) (2.0 * std::sqrt (re * re + im * im) / wsum);
}

// power spectrum (Hann) of one channel, 2^order bins
std::vector<float> spectrum (const AudioBuffer<float>& b, int ch, int start, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i)
        data[(size_t) i] = b.getSample (ch, start + i) * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (n - 1)));
    fft.performFrequencyOnlyForwardTransform (data.data());
    std::vector<float> mag ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) mag[(size_t) i] = data[(size_t) i] * data[(size_t) i];
    return mag;
}

void setParam (RacoonProcessor& p, const String& id, float real) { p.setRealValue (id, real); }

void shot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}
}

// ---------------------------------------------------------------- contract checks for every plugin
static void commonChecks (const String& name, std::function<RacoonProcessor*()> make, const File& shots)
{
    std::cout << "contrato " << name << std::endl;
    std::unique_ptr<RacoonProcessor> p (make());
    const bool instrument = p->info.kind == PluginKind::instrument;
    const bool midiFx = p->info.kind == PluginKind::midiEffect;

    // block sizes and sample rates from the spec
    bool finite = true, ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 16, 64, 256, 1024 })
        {
            p->setRateAndBufferSizeDetails (sr, block);
            p->prepareToPlay (sr, block);
            AudioBuffer<float> in (2, (int) (sr * 0.25));
            tu::fillNoise (in, 0.3f, block);
            MidiBuffer midi;
            if (instrument || midiFx)
            {
                midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 10);
                midi.addEvent (MidiMessage::noteOn (1, 64, (uint8) 90), 20);
                midi.addEvent (MidiMessage::noteOff (1, 60), (int) (sr * 0.15));
                midi.addEvent (MidiMessage::noteOff (1, 64), (int) (sr * 0.15));
                if (instrument) in.clear();
            }
            auto out = tu::render (*p, in, block, &midi);
            finite = finite && tu::allFinite (out);
            ok = ok && tu::peak (out, 0, out.getNumSamples()) < 8.0f;
        }
    check (finite, "sin NaN ni infinitos con bloques de 1 a 1024 y 44.1 / 48 / 96 kHz");
    check (ok, "sin explosiones de nivel");

    // parameter fuzz: random values every block
    p->prepareToPlay (48000.0, 256);
    Random r (7);
    AudioBuffer<float> fz (2, 256);
    bool fuzzOk = true;
    for (int k = 0; k < 400; ++k)
    {
        for (auto* prm : p->getParameters())
            if (prm != p->getBypassParameter() && r.nextFloat() < 0.3f) prm->setValue (r.nextFloat());
        tu::fillNoise (fz, 0.5f, k);
        MidiBuffer m;
        if (k % 7 == 0) m.addEvent (MidiMessage::noteOn (1, 40 + (k % 40), (uint8) 100), 0);
        if (k % 7 == 3) m.addEvent (MidiMessage::allNotesOff (1), 0);
        p->processBlock (fz, m);
        const bool fin = tu::allFinite (fz);
        const float mag = fin ? fz.getMagnitude (0, 256) : 0.0f;
        if (fuzzOk && (! fin || mag >= 40.0f)) std::cout << "      bloque " << k << (fin ? " pico " + String (mag, 1) : String (" NaN")) << std::endl;
        fuzzOk = fuzzOk && fin && mag < 40.0f;
    }
    check (fuzzOk, "parámetros al azar en 400 bloques: estable"_u);

    // state round trip
    for (auto* prm : p->getParameters()) if (prm != p->getBypassParameter()) prm->setValueNotifyingHost (r.nextFloat());
    MemoryBlock state;
    p->getStateInformation (state);
    std::unique_ptr<RacoonProcessor> q (make());
    q->setStateInformation (state.getData(), (int) state.getSize());
    bool same = true;
    for (int i = 0; i < p->getParameters().size(); ++i)
        if (p->getParameters()[i]->getCurrentValueAsText() != q->getParameters()[i]->getCurrentValueAsText())
        {
            same = false;
            std::cout << "      distinto: " << p->getParameters()[i]->getName (40) << " " << p->getParameters()[i]->getValue()
                      << " vs " << q->getParameters()[i]->getValue() << std::endl;
        }
    check (same, "estado: guardar y abrir devuelve los mismos parámetros"_u);

    // presets
    const int np = p->getNumPrograms();
    bool presetsOk = np >= 1;
    for (int i = 0; i < np; ++i)
    {
        p->setCurrentProgram (i);
        presetsOk = presetsOk && p->getProgramName (i).isNotEmpty();
    }
    check (presetsOk, String (np) + " presets de fábrica cargan"_u);
    check (p->saveUserPreset ("Prueba test") && p->loadUserPreset ("Prueba test") && p->deleteUserPreset ("Prueba test"), "preset de usuario: guardar, abrir y borrar");

    // bypass without clicks (effects)
    if (! instrument && ! midiFx)
    {
        p->setCurrentProgram (0);
        p->prepareToPlay (48000.0, 128);
        AudioBuffer<float> sine (2, 48000);
        tu::fillSine (sine, 220.0, 48000.0, 0.4f, 0.0);
        AudioBuffer<float> io (2, 128);
        AudioBuffer<float> out (2, 48000);
        for (int pos = 0; pos < 48000; pos += 128)
        {
            if (pos == 24000) p->getBypassParameter()->setValue (1.0f);
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, sine, c, pos, 128);
            MidiBuffer m;
            p->processBlock (io, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, 128);
        }
        p->getBypassParameter()->setValue (0.0f);
        const float sineStep = 0.4f * (float) (MathConstants<double>::twoPi * 220.0 / 48000.0);
        AudioBuffer<float> around (2, 4000);
        for (int c = 0; c < 2; ++c) around.copyFrom (c, 0, out, c, 22000, 4000);
        // a click is a jump bigger than the ones the processed signal already has (distortion makes steep edges)
        AudioBuffer<float> before (2, 3800);
        for (int c = 0; c < 2; ++c) before.copyFrom (c, 0, out, c, 20000, 3800);
        const float own = tu::maxJump (before);
        check (tu::maxJump (around) < jmax (0.08f, sineStep * 6.0f, own * 1.5f), "bypass sin clic (salto máx "_u + String (tu::maxJump (around), 3) + ", la señal ya tenía "_u + String (own, 3) + ")");
    }

    // editor builds and draws, in both layers
    {
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditorIfNeeded());
        if (auto* re = dynamic_cast<RacoonEditor*> (ed.get()))
        {
            re->setEasy (true);
            MessageManager::getInstance()->runDispatchLoopUntil (150);
            tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_facil.png"));
            re->setEasy (false);
            MessageManager::getInstance()->runDispatchLoopUntil (150);
            tu::shot (*re, shots.getChildFile (name.toLowerCase() + "_completo.png"));
            check (true, "editor en PRINCIPIANTE y AVANZADO"_u);
        }
        else check (false, "editor");
    }
}

// each plugin's own checks live in its folder (PluginTests.inc files are pulled in here)
#include "AllFxTests.inc"

// ---------------------------------------------------------------- FX_MEASURE=1: how much each effect and each knob changes the sound
namespace measure
{
// a short piece of music: kick, snare, hats, a bass line and a chord, 120 BPM, stereo
static AudioBuffer<float> music (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    AudioBuffer<float> b (2, n);
    b.clear();
    Random rng (7);
    const int beat = (int) (sr * 0.5);
    for (int i = 0; i < n; ++i)
    {
        const int inBeat = i % beat, inHalf = i % (beat / 2);
        const double t = inBeat / sr, th = inHalf / sr;
        const int beatNo = i / beat;
        float kick = (float) (std::sin (MathConstants<double>::twoPi * (50.0 + 120.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 7.0)) * 0.8f;
        float snare = (beatNo % 2 == 1) ? (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * 18.0) * 0.4f : 0.0f;
        float hat = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-th * 90.0) * 0.15f;
        static const double bassNotes[] { 55.0, 55.0, 65.41, 49.0 };
        const double bf = bassNotes[(beatNo / 2) % 4];
        const double ph = std::fmod (bf * i / sr, 1.0);
        float bass = (float) (2.0 * ph - 1.0) * 0.18f * (float) std::exp (-t * 2.0);
        float chord = 0.0f;
        for (double f : { 220.0, 261.63, 329.63, 392.0 })
            chord += (float) std::sin (MathConstants<double>::twoPi * f * i / sr + f) * 0.05f;
        const float mono = kick + snare + bass;
        b.setSample (0, i, mono + hat * 0.7f + chord * 1.1f);
        b.setSample (1, i, mono + hat * 1.0f + chord * 0.8f);
    }
    return b;
}

static double rmsDiffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& ref, int start)
{
    double d = 0, r = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = start; i < jmin (a.getNumSamples(), ref.getNumSamples()); ++i)
        {
            const double x = a.getSample (c, i) - ref.getSample (c, i);
            d += x * x; r += (double) ref.getSample (c, i) * ref.getSample (c, i);
        }
    return 10.0 * std::log10 ((d + 1e-20) / (r + 1e-20));
}

// average absolute difference of the third-octave band levels, in dB (sees EQ, filters, saturation, reverb)
static double bandDiffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& b, int start, double sr)
{
    double sum = 0; int k = 0;
    for (int c = 0; c < 2; ++c)
    {
        std::vector<double> ea (32, 0.0), eb (32, 0.0);
        for (int pos = start; pos + 4096 <= jmin (a.getNumSamples(), b.getNumSamples()); pos += 4096)
        {
            auto sa = tu::spectrum (a, c, pos, 12), sb = tu::spectrum (b, c, pos, 12);
            for (size_t bin = 1; bin < sa.size(); ++bin)
            {
                const double hz = bin * sr / 4096.0;
                if (hz < 30.0 || hz > 18000.0) continue;
                const int band = jlimit (0, 31, (int) std::floor (3.0 * std::log2 (hz / 30.0)));
                ea[(size_t) band] += sa[bin]; eb[(size_t) band] += sb[bin];
            }
        }
        for (int i = 0; i < 28; ++i)
            if (ea[(size_t) i] > 1e-9 || eb[(size_t) i] > 1e-9)
            { sum += std::abs (10.0 * std::log10 ((ea[(size_t) i] + 1e-12) / (eb[(size_t) i] + 1e-12))); ++k; }
    }
    return k > 0 ? sum / k : 0.0;
}

static AudioBuffer<float> run (const String& id, std::function<void (RacoonProcessor&)> setup, const AudioBuffer<float>& in, double sr)
{
    auto p = racoon::fx::create (id);
    p->setRateAndBufferSizeDetails (sr, 512);
    p->prepareToPlay (sr, 512);
    for (auto& kv : StringArray::fromTokens (SystemStats::getEnvironmentVariable ("FX_SET", {}), ",", ""))
        p->setRealValue (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    if (setup) setup (*p);
    p->prepareToPlay (sr, 512);
    return tu::render (*p, in, 512, nullptr);
}

static void all (const StringArray& only)
{
    const double sr = 48000.0;
    auto in = music (sr, 3.0);
    const int skip = (int) (0.25 * sr);
    for (auto& def : racoon::fx::registry())
    {
        if (def.kind != racoon::fx::Kind::effect || ! def.make) continue;
        if (! only.isEmpty() && ! only.contains (def.id)) continue;
        auto dflt = run (def.id, {}, in, sr);
        std::cout << "EFECTO " << def.id << "  defecto: resto " << String (rmsDiffDb (dflt, in, skip), 1) << " dB, bandas "
                  << String (bandDiffDb (dflt, in, skip, sr), 2) << " dB, nivel " << String (20.0 * std::log10 (tu::rms (dflt, skip, 1 << 30) / tu::rms (in, skip, 1 << 30) + 1e-9), 1) << " dB" << std::endl;
        auto probe = racoon::fx::create (def.id);
        if (SystemStats::getEnvironmentVariable ("FX_MEASURE", {}) == "keys")
        {
            // the knobs of its card, from the defaults: each one at both ends must sound different from the default
            StringArray keys = def.keys;
            if (keys.isEmpty())
                for (auto* prm : probe->getParameters())
                    if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm); rp != nullptr && rp->paramID != "bypass" && keys.size() < 6) keys.add (rp->paramID);
            for (auto& pid : keys)
            {
                auto* rp = dynamic_cast<RangedAudioParameter*> (probe->apvts.getParameter (pid));
                if (rp == nullptr) { std::cout << "   FALTA  " << pid << std::endl; continue; }
                const auto range = rp->getNormalisableRange();
                auto lo = run (def.id, [pid, range] (RacoonProcessor& p) { p.setRealValue (pid, range.start); }, in, sr);
                auto hi = run (def.id, [pid, range] (RacoonProcessor& p) { p.setRealValue (pid, range.end); }, in, sr);
                const double rl = rmsDiffDb (lo, dflt, skip), rh = rmsDiffDb (hi, dflt, skip);
                const double bl = bandDiffDb (lo, dflt, skip, sr), bh = bandDiffDb (hi, dflt, skip, sr);
                const double r = jmax (rl, rh), bd = jmax (bl, bh);
                std::cout << "   " << (r < -30.0 && bd < 0.3 ? "MUERTO " : (r < -18.0 && bd < 0.6 ? "FLOJO  " : "       ")) << pid.paddedRight (' ', 14) << " " << rp->getName (24).paddedRight (' ', 24)
                          << " min " << String (rl, 1) << "/" << String (bl, 2) << "  max " << String (rh, 1) << "/" << String (bh, 2) << std::endl;
            }
            continue;
        }
        for (auto* prm : probe->getParameters())
        {
            auto* rp = dynamic_cast<RangedAudioParameter*> (prm);
            if (rp == nullptr || rp->paramID == "bypass") continue;
            const auto pid = rp->paramID;
            const auto range = rp->getNormalisableRange();
            auto lo = run (def.id, [pid, range] (RacoonProcessor& p) { p.setRealValue (pid, range.start); }, in, sr);
            auto hi = run (def.id, [pid, range] (RacoonProcessor& p) { p.setRealValue (pid, range.end); }, in, sr);
            const double r = rmsDiffDb (hi, lo, skip), bd = bandDiffDb (hi, lo, skip, sr);
            std::cout << "   " << (r < -40.0 && bd < 0.2 ? "MUERTO " : "       ") << pid.paddedRight (' ', 14) << " " << rp->getName (24).paddedRight (' ', 24)
                      << " min-max: resto " << String (r, 1) << " dB, bandas " << String (bd, 2) << " dB" << std::endl;
        }
    }
}
}

// ---------------------------------------------------------------- what each effect does, heard from outside
namespace behaviour
{
static double rmsDb (const AudioBuffer<float>& b, int ch, int from, int to)
{
    double e = 0; int k = 0;
    for (int i = jmax (0, from); i < jmin (to, b.getNumSamples()); ++i, ++k) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10.0 * std::log10 (e / jmax (1, k) + 1e-20);
}

static void run()
{
    const double sr = 48000.0;
    std::cout << "COMPORTAMIENTO" << std::endl;

    // a host like Tracktion calls setValue() on the parameter and nothing else: the effect must hear it
    {
        auto p = racoon::fx::create ("delay");
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        AudioProcessorParameter* mixP = nullptr;
        for (auto* jp : p->getParameters())
            if (auto* hp = dynamic_cast<AudioProcessorParameterWithID*> (jp); hp != nullptr && hp->paramID == "mix") mixP = jp;
        mixP->setValue (1.0f);
        AudioBuffer<float> io (2, 512);
        io.clear();
        MidiBuffer m;
        p->processBlock (io, m);
        check (std::abs (p->value ("mix") - 100.0f) < 0.01f, "una perilla movida por el anfitrión (setValue) llega al efecto");
        MemoryBlock st;
        p->getStateInformation (st);
        auto q = racoon::fx::create ("delay");
        q->setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (q->value ("mix") - 100.0f) < 0.01f, "y se guarda con el proyecto");
    }

    // ping-pong: one click in the middle bounces left, right, left
    {
        auto p = racoon::fx::create ("delay");
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        for (auto [k, v] : { std::pair<const char*, float> { "mode", 1 }, { "sync", 0 }, { "timel", 200 }, { "feedback", 60 }, { "mix", 100 }, { "hp", 20 }, { "lp", 20000 } })
            p->setRealValue (k, v);
        p->prepareToPlay (sr, 512);
        AudioBuffer<float> in (2, (int) sr);
        in.clear();
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 48; ++i) in.setSample (c, i, 0.8f);
        auto out = tu::render (*p, in, 512, nullptr);
        auto lr = [&] (double ms) { const int a = (int) (ms * 0.001 * sr) - 200, z = a + 600; return rmsDb (out, 0, a, z) - rmsDb (out, 1, a, z); };
        const double e1 = lr (200), e2 = lr (400), e3 = lr (600);
        check (e1 > 20.0 && e2 < -20.0 && e3 > 20.0, "ping-pong: izquierda, derecha, izquierda (" + String (e1, 0) + ", " + String (e2, 0) + ", " + String (e3, 0) + " dB)");
    }

    // looper: what it recorded keeps playing when the input stops
    {
        auto p = racoon::fx::create ("looper");
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        AudioBuffer<float> in (2, (int) sr), sil (2, (int) sr);
        tu::fillNoise (in, 0.3f, 3);
        sil.clear();
        p->setRealValue ("state", 1);
        tu::render (*p, in, 512, nullptr);
        p->setRealValue ("state", 2);
        p->setRealValue ("monitor", 1);
        auto out = tu::render (*p, sil, 512, nullptr);
        check (rmsDb (out, 0, 0, out.getNumSamples()) > -20.0, "looper: graba y repite (" + String (rmsDb (out, 0, 0, out.getNumSamples()), 1) + " dB)");
    }

    // vinyl: crackle and hiss are heard on silence with its defaults
    {
        auto p = racoon::fx::create ("vinilo");
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        AudioBuffer<float> sil (2, (int) sr * 2);
        sil.clear();
        auto out = tu::render (*p, sil, 512, nullptr);
        const double lvl = rmsDb (out, 0, 0, out.getNumSamples());
        check (lvl > -50.0 && lvl < -18.0, "vinilo: crujidos y soplido audibles en silencio (" + String (lvl, 1) + " dB)");
    }

    // every knob on every card changes the sound from the defaults (a few only matter with another switch)
    {
        const StringArray waits { "paneo-automatico:rate", "eco:divr", "alinear:mode", "alinear:msr", "alinear:link", "eq-canal:midfreq",
                                  "transpositor:freq", "reverb-hibrida:space", "espectro:size", "espectro:avg", "espectro:range", "afinador:ref",
                                  "looper:state", "looper:length", "looper:speed", "looper:reverse", "looper:clear", "puerta:attack", "puerta:hold",
                                  "puerta:release", "puerta:floor",
                                  // these need a switch, a voice or a meter to show anything
                                  "medidor:target", "medidor:reset", "efectos-dj:effect", "efectos-dj:time", "efectos-dj:repeat", "efectos-dj:filter",
                                  "efectos-dj:mix", "afinacion-automatica:key", "afinacion-automatica:scale", "afinacion-automatica:speed",
                                  "afinacion-automatica:tol", "afinacion-automatica:human", "afinacion-automatica:mix",
                                  "de-esser:freq", "de-esser:range", "de-esser:band", "imagen-estereo:wlow", "imagen-estereo:whigh", "rotativo:accel",
                                  // the Sidechain does nothing until a channel is connected; SidechainTests.inc checks every knob with a key
                                  "sidechain:thresh", "sidechain:depth", "sidechain:attack", "sidechain:release", "sidechain:keylp", "sidechain:mix" };
        auto in = measure::music (sr, 1.5);
        const int skip = (int) (0.25 * sr);
        int dead = 0, total = 0;
        for (auto& def : racoon::fx::registry())
        {
            if (def.kind != racoon::fx::Kind::effect || ! def.make) continue;
            auto dflt = measure::run (def.id, {}, in, sr);
            auto probe = racoon::fx::create (def.id);
            for (auto& pid : def.keys)
            {
                if (waits.contains (def.id + ":" + pid)) continue;
                auto* rp = dynamic_cast<RangedAudioParameter*> (probe->apvts.getParameter (pid));
                if (rp == nullptr) { check (false, def.id + ": la perilla " + pid + " no existe"); continue; }
                const auto range = rp->getNormalisableRange();
                ++total;
                double worst = -300.0, band = 0.0;
                for (float v : { range.start, range.end })
                {
                    auto o = measure::run (def.id, [pid, v] (RacoonProcessor& q) { q.setRealValue (pid, v); }, in, sr);
                    worst = jmax (worst, measure::rmsDiffDb (o, dflt, skip));
                    band = jmax (band, measure::bandDiffDb (o, dflt, skip, sr));
                }
                if (worst < -30.0 && band < 0.3) { ++dead; check (false, def.id + ": la perilla " + pid + " no cambia el sonido"); }
            }
        }
        check (dead == 0, "perillas de las tarjetas que cambian el sonido: " + String (total - dead) + " de " + String (total));
    }
}
}


// ---------------------------------------------------------------- the faults found in the October 2026 review, measured
namespace arreglos
{
static std::unique_ptr<RacoonProcessor> make (const String& id, double sr, std::function<void (RacoonProcessor&)> setup)
{
    auto p = racoon::fx::create (id);
    p->setRateAndBufferSizeDetails (sr, 512);
    p->prepareToPlay (sr, 512);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 512);
    return p;
}
static AudioBuffer<float> sine (double sr, double hz, float amp, double seconds, int ch = 2)
{
    AudioBuffer<float> b (ch, (int) (sr * seconds));
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, amp * (float) std::sin (MathConstants<double>::twoPi * hz * i / sr));
    return b;
}
static float dbOf (float x) { return 20.0f * std::log10 (x + 1.0e-12f); }

static void run()
{
    std::cout << "ARREGLOS (revisión de octubre)" << std::endl;

    // 1. the DC blocker of Amplificador, Válvula and Pedal: 10 Hz at any sample rate. Before it was a 70 Hz high pass at 44.1 kHz
    //    and 150 Hz at 96 kHz, so the lows of a bass changed with the sample rate.
    for (auto id : { "amplificador", "valvula", "pedal" })
    {
        float lvl[2];
        int k = 0;
        for (double sr : { 44100.0, 96000.0 })
        {
            const String sid (id);
            auto p = make (id, sr, [sid] (RacoonProcessor& q)
            {
                if (sid == "amplificador") { q.setRealValue ("model", 0); q.setRealValue ("gain", 0); }
                if (sid == "valvula") q.setRealValue ("drive", 0);
                if (sid == "pedal") { q.setRealValue ("type", 0); q.setRealValue ("gain", 0); }
            });
            auto in = sine (sr, 40.0, 0.05f, 1.0);
            auto out = tu::render (*p, in, 512, nullptr);
            const int n = (int) (sr * 0.5);
            lvl[k++] = dbOf (tu::tone (out, 0, n / 2, n, 40.0, sr)) - dbOf (0.05f);
        }
        check (std::abs (lvl[0] - lvl[1]) < 1.0f, String (id) + ": 40 Hz suena igual a 44.1 y a 96 kHz (" + String (lvl[0], 1) + " / " + String (lvl[1], 1) + " dB)");
    }

    // 2. Válvula dinámica: the same signal on both sides comes out the same on both sides (the right side used a stale bias)
    {
        const double sr = 48000.0;
        auto p = make ("valvula", sr, [] (RacoonProcessor& q) { q.setRealValue ("env", 300); q.setRealValue ("drive", 18); q.setRealValue ("attack", 0.5f); q.setRealValue ("release", 20); });
        AudioBuffer<float> in (2, (int) sr);
        Random r (7);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float hit = std::exp (-(float) (i % 6000) / 600.0f);   // a hit every 125 ms
            const float v = hit * 0.6f * (float) std::sin (MathConstants<double>::twoPi * 110.0 * i / sr);
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        auto out = tu::render (*p, in, 512, nullptr);
        float d = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) d = jmax (d, std::abs (out.getSample (0, i) - out.getSample (1, i)));
        check (d < 1.0e-5f, "válvula con envolvente: izquierda y derecha iguales con la misma entrada (diferencia " + String (d, 7) + ")");
    }

    // 3. Puerta: the detector's high cut works on the audio. A 5 kHz tone with the detector cut at 200 Hz must not open it
    {
        const double sr = 48000.0;
        auto p = make ("puerta", sr, [] (RacoonProcessor& q) { q.setRealValue ("thresh", -30); q.setRealValue ("sclp", 200); q.setRealValue ("hyst", 3); });
        auto in = sine (sr, 5000.0, 0.3f, 1.0);
        auto out = tu::render (*p, in, 512, nullptr);
        const float o = tu::rms (out, (int) (sr * 0.5), (int) (sr * 0.4)), i = tu::rms (in, (int) (sr * 0.5), (int) (sr * 0.4));
        check (dbOf (o / i) < -40.0f, "puerta: con el detector cortado a 200 Hz, un tono de 5 kHz no la abre (" + String (dbOf (o / i), 1) + " dB)");
        auto p2 = make ("puerta", sr, [] (RacoonProcessor& q) { q.setRealValue ("thresh", -30); q.setRealValue ("sclp", 20000); });
        auto out2 = tu::render (*p2, in, 512, nullptr);
        const float o2 = tu::rms (out2, (int) (sr * 0.5), (int) (sr * 0.4));
        check (dbOf (o2 / i) > -1.0f, "y sin ese corte el mismo tono sí la abre (" + String (dbOf (o2 / i), 1) + " dB)");
    }

    // 4. Limitador: "Estéreo enlazado" does something. Unlinked, a quiet right side keeps its level while the left one is limited
    {
        const double sr = 48000.0;
        AudioBuffer<float> in (2, (int) sr);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            in.setSample (0, i, 0.9f * (float) std::sin (MathConstants<double>::twoPi * 200.0 * i / sr));
            in.setSample (1, i, 0.1f * (float) std::sin (MathConstants<double>::twoPi * 300.0 * i / sr));
        }
        float right[2];
        for (int linked = 0; linked < 2; ++linked)
        {
            auto p = make ("limitador", sr, [linked] (RacoonProcessor& q) { q.setRealValue ("gain", 12); q.setRealValue ("ceiling", -1); q.setRealValue ("link", (float) linked); });
            auto out = tu::render (*p, in, 512, nullptr);
            AudioBuffer<float> r1 (1, out.getNumSamples()), r0 (1, in.getNumSamples());
            r1.copyFrom (0, 0, out, 1, 0, out.getNumSamples()); r0.copyFrom (0, 0, in, 1, 0, in.getNumSamples());
            right[linked] = dbOf (tu::rms (r1, (int) (sr * 0.5), (int) (sr * 0.4)) / tu::rms (r0, (int) (sr * 0.5), (int) (sr * 0.4)));
        }
        check (std::abs (right[0] - 12.0f) < 0.5f && right[1] < 6.0f,
               "limitador: sin enlazar el lado bajito conserva sus +12 dB (" + String (right[0], 1) + "), enlazado baja con el otro (" + String (right[1], 1) + ")");
    }

    // 5. Reverb híbrida: the space arrives with the predelay, also below 11 ms (it used to come one 512-sample block late)
    for (float pre : { 0.0f, 5.0f, 40.0f })
    {
        const double sr = 44100.0;
        auto p = make ("reverb-hibrida", sr, [pre] (RacoonProcessor& q)
        {
            q.setRealValue ("routing", 2); q.setRealValue ("predelay", pre); q.setRealValue ("mix", 100); q.setRealValue ("lowcut", 20); q.setRealValue ("highcut", 20000);
        });
        AudioBuffer<float> in (2, (int) sr);
        in.clear();
        in.setSample (0, 100, 1.0f); in.setSample (1, 100, 1.0f);
        auto out = tu::render (*p, in, 512, nullptr);
        // the first sample where the space reaches a fifth of its loudest moment
        float pk = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) pk = jmax (pk, std::abs (out.getSample (0, i)));
        int first = -1;
        for (int i = 0; i < out.getNumSamples() && first < 0; ++i) if (std::abs (out.getSample (0, i)) > pk * 0.2f) first = i;
        const double ms = (first - 100) * 1000.0 / sr;
        check (first >= 0 && ms >= pre - 0.2 && ms < pre + 3.0, "reverb híbrida con predelay de " + String (pre, 0) + " ms: el espacio llega a los " + String (ms, 2) + " ms");
    }

    // 6. Puerta: the level it watches falls in milliseconds, so it closes after the same time at 44.1 and 96 kHz
    {
        double closeMs[2];
        int k = 0;
        for (double sr : { 44100.0, 96000.0 })
        {
            auto p = make ("puerta", sr, [] (RacoonProcessor& q) { q.setRealValue ("thresh", -30); q.setRealValue ("hold", 0); q.setRealValue ("release", 1); q.setRealValue ("hyst", 0); });
            // loud for half a second, then the same tone 40 dB down (under the threshold): the gate has to notice and close
            auto in = sine (sr, 1000.0, 0.5f, 1.0);
            const int stop = (int) (sr * 0.5);
            for (int c = 0; c < 2; ++c) in.applyGain (c, stop, in.getNumSamples() - stop, 0.01f);
            auto out = tu::render (*p, in, 512, nullptr);
            int closed = -1;
            for (int i = stop; i < out.getNumSamples() && closed < 0; ++i) if (std::abs (out.getSample (0, i)) < 1.0e-4f && std::abs (in.getSample (0, i)) > 0.004f) closed = i;
            closeMs[k++] = closed < 0 ? 1.0e9 : (closed - stop) * 1000.0 / sr;
        }
        check (closeMs[0] < 1000.0 && closeMs[1] < 1000.0 && std::abs (closeMs[0] - closeMs[1]) < 0.15 * closeMs[0] + 1.0,
               "puerta: cierra al mismo tiempo a 44.1 y a 96 kHz (" + String (closeMs[0], 1) + " / " + String (closeMs[1], 1) + " ms)");
    }
}
}


// ---------------------------------------------------------------- tanda 2: saturation without aliasing, filters that glide
namespace tanda2
{
using arreglos::make;
// power that is not the tone or its harmonics (folded back from above Nyquist), relative to the whole output, in dB
static double aliasDb (const AudioBuffer<float>& b, double f0, double sr)
{
    const int order = 15, n = 1 << order;
    const int start = b.getNumSamples() - n;
    auto mag = tu::spectrum (b, 0, start, order);
    const double binHz = sr / n;
    double total = 0.0, alias = 0.0;
    for (int k = 2; k < (int) mag.size(); ++k)
    {
        const double hz = k * binHz;
        if (hz < 30.0) continue;
        total += mag[(size_t) k];
        bool harmonic = false;
        for (int h = 1; h * f0 < sr * 0.5; ++h) if (std::abs (hz - h * f0) < 6.0 * binHz) harmonic = true;
        if (! harmonic) alias += mag[(size_t) k];
    }
    return 10.0 * std::log10 (alias / jmax (1.0e-30, total) + 1.0e-30);
}
// the same tone through a plain tanh at 1x: what aliasing sounds like without any care (for comparison)
static double naiveAliasDb (double f0, double sr, float drive)
{
    auto in = arreglos::sine (sr, f0, 0.5f, 1.0, 1);
    for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (0, i, std::tanh (in.getSample (0, i) * drive));
    return aliasDb (in, f0, sr);
}

static void run()
{
    std::cout << "TANDA 2 (aliasing, filtros, reverb)" << std::endl;
    const double sr = 48000.0, f0 = 4987.0;
    const double naive = naiveAliasDb (f0, sr, 20.0f);
    struct Case { const char* id; String what; std::function<void (RacoonProcessor&)> set; double maxDb; };
    const std::vector<Case> cases {
        { "amplificador", "Amplificador en Pesado al máximo"_u, [] (RacoonProcessor& q) { q.setRealValue ("model", 5); q.setRealValue ("gain", 100); }, -35.0 },
        { "pedal", "Pedal en fuzz", [] (RacoonProcessor& q) { q.setRealValue ("type", 2); q.setRealValue ("gain", 100); }, -28.0 },
        { "bus-bateria", "Bus de batería duro al máximo"_u, [] (RacoonProcessor& q) { q.setRealValue ("type", 2); q.setRealValue ("drive", 100); q.setRealValue ("boom", 0); }, -27.0 },
        { "clipper", "Clipper duro sin sobremuestreo", [] (RacoonProcessor& q) { q.setRealValue ("os", 0); q.setRealValue ("shape", 0); q.setRealValue ("drive", 18); }, -18.0 },
        { "overdrive", "Overdrive al máximo"_u, [] (RacoonProcessor& q) { q.setRealValue ("drive", 100); }, -45.0 },
        { "valvula", "Válvula C con 36 dB"_u, [] (RacoonProcessor& q) { q.setRealValue ("tube", 2); q.setRealValue ("drive", 36); }, -33.0 },
    };
    for (auto& c : cases)
    {
        auto p = make (c.id, sr, c.set);
        auto out = tu::render (*p, arreglos::sine (sr, f0, 0.5f, 1.0), 512, nullptr);
        const double a = aliasDb (out, f0, sr);
        check (a < c.maxDb && a < naive - 5.0, c.what + ": aliasing " + String (a, 1) + " dB (un tanh sin cuidado: " + String (naive, 1) + " dB)");
    }

    // a filter moved by automation glides inside the block: no step at the block edges. A 60 Hz tone through the
    // low shelf of the channel EQ, the shelf changing a little every block (a jump there is a click on a low note)
    {
        auto p = make ("eq-canal", sr, {});
        AudioBuffer<float> io (2, 512);
        double ph = 0.0;
        float worst = 0.0f, last = 0.0f, natural = 0.0f;
        for (int blk = 0; blk < 120; ++blk)
        {
            p->setRealValue ("low", (float) (std::sin (blk * 0.2) * 12.0));
            for (int i = 0; i < 512; ++i) { const float v = 0.4f * (float) std::sin (ph); io.setSample (0, i, v); io.setSample (1, i, v); ph += MathConstants<double>::twoPi * 60.0 / sr; }
            MidiBuffer m;
            p->processBlock (io, m);
            for (int i = 0; i < 512; ++i)
            {
                const float j = std::abs (io.getSample (0, i) - last);
                last = io.getSample (0, i);
                if (blk > 4) { if (i == 0) worst = jmax (worst, j); else if (i > 80) natural = jmax (natural, j); }
            }
        }
        check (worst < natural * 1.5f, "EQ de canal automatizado: sin escalón en el borde del bloque (paso " + String (worst, 4) + ", dentro del bloque " + String (natural, 4) + ")");
    }
}
}

namespace tanda5
{
using arreglos::make;
// average power spectrum of one channel over many Hann frames
static std::vector<double> avgSpectrum (const AudioBuffer<float>& b, int ch, int order, int skip)
{
    const int n = 1 << order;
    std::vector<double> acc ((size_t) n / 2 + 1, 0.0);
    int frames = 0;
    for (int pos = skip; pos + n <= b.getNumSamples(); pos += n / 2, ++frames)
    {
        auto s = tu::spectrum (b, ch, pos, order);
        for (size_t k = 0; k < acc.size() && k < s.size(); ++k) acc[k] += s[k];
    }
    for (auto& a : acc) a /= jmax (1, frames);
    return acc;
}
// worst deviation in dB of out/in over third-octave bands from lo to hi Hz
static double worstDev (const AudioBuffer<float>& in, const AudioBuffer<float>& out, double sr, double lo, double hi)
{
    const int order = 12;
    double worst = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        auto a = avgSpectrum (in, c, order, 8192), b = avgSpectrum (out, c, order, 8192);
        const double bin = sr / (1 << order);
        for (double f = lo; f <= hi; f *= std::pow (2.0, 1.0 / 3.0))
        {
            double pa = 0, pb = 0;
            for (size_t k = (size_t) (f / std::pow (2.0, 1.0 / 6.0) / bin); k <= (size_t) (f * std::pow (2.0, 1.0 / 6.0) / bin) && k < a.size(); ++k) { pa += a[k]; pb += b[k]; }
            if (pa > 0) worst = jmax (worst, std::abs (10.0 * std::log10 (pb / pa)));
        }
    }
    return worst;
}
static void run()
{
    std::cout << "TANDA 5 (Prisma)" << std::endl;
    const double sr = 48000.0;
    AudioBuffer<float> noise (2, (int) (sr * 6));
    Random rnd (11);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (c, i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.1f);
    // untouched, the four bands add back up flat (an allpass), wet and half mixed
    for (float mix : { 100.0f, 50.0f })
    {
        auto p = make ("prisma", sr, [mix] (RacoonProcessor& q) { q.setRealValue ("s_width", 100); q.setRealValue ("mix", mix); });
        auto out = tu::render (*p, noise, 512, nullptr);
        const double d = worstDev (noise, out, sr, 30.0, 16000.0);
        check (d < 0.6, "Prisma sin tocar (mezcla " + String ((int) mix) + " %) suma plano de 30 Hz a 16 kHz: " + String (d, 2) + " dB");
    }
    // driven hard on every band, a high tone stays clean of fold-back
    {
        const double f0 = 4987.0;
        auto p = make ("prisma", sr, [] (RacoonProcessor& q) { for (auto* b : { "s", "b", "c", "a" }) q.setRealValue (String (b) + "_drive", 100); });
        auto out = tu::render (*p, arreglos::sine (sr, f0, 0.5f, 1.0), 512, nullptr);
        const double a = tanda2::aliasDb (out, f0, sr), naive = tanda2::naiveAliasDb (f0, sr, 20.0f);
        check (a < -33.0 && a < naive - 15.0, "Prisma con saturación al máximo en las cuatro bandas: aliasing " + String (a, 1) + " dB (tanh sin cuidado " + String (naive, 1) + ")"_u);
    }
    // refraction moves the highs apart and leaves the sub in the centre
    {
        AudioBuffer<float> mono (2, (int) sr * 2);
        for (int i = 0; i < mono.getNumSamples(); ++i) { const float v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.1f; mono.setSample (0, i, v); mono.setSample (1, i, v); }
        auto p = make ("prisma", sr, [] (RacoonProcessor& q) { q.setRealValue ("refract", 100); });
        auto out = tu::render (*p, mono, 512, nullptr);
        double side = 0, mid = 0;
        for (int i = 4096; i < out.getNumSamples(); ++i) { const double l = out.getSample (0, i), r = out.getSample (1, i); side += (l - r) * (l - r); mid += (l + r) * (l + r); }
        const double ratio = 10.0 * std::log10 (side / mid);
        check (ratio > -20.0 && ratio < 0.0, "la refracción abre una señal mono: lados a " + String (ratio, 1) + " dB del centro"_u);
    }
}
}


// ---------------------------------------------------------------- showcase: the windows with music running through them
namespace showcase
{
static void run (const StringArray& ids, const File& out)
{
    const double sr = 48000.0;
    auto music = measure::music (sr, 4.0);
    for (auto& id : ids)
    {
        auto p = racoon::fx::create (id);
        if (p == nullptr) continue;
        p->setRateAndBufferSizeDetails (sr, 512);
        p->prepareToPlay (sr, 512);
        for (auto& kv : StringArray::fromTokens (SystemStats::getEnvironmentVariable ("FX_SET", {}), ",", ""))
            if (kv.startsWith (id + ":"))
            {
                auto rest = kv.fromFirstOccurrenceOf (":", false, false);
                p->setRealValue (rest.upToFirstOccurrenceOf ("=", false, false), rest.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            }
        std::unique_ptr<AudioProcessorEditor> ed (p->createEditorIfNeeded());
        auto* re = dynamic_cast<RacoonEditor*> (ed.get());
        if (re == nullptr) continue;
        re->setEasy (SystemStats::getEnvironmentVariable ("FX_EASY", "1") == "1");
        AudioBuffer<float> io (2, 512);
        for (int pos = 0; pos + 512 <= music.getNumSamples(); pos += 512)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, music, c, pos, 512);
            MidiBuffer m;
            p->processBlock (io, m);
            MessageManager::getInstance()->runDispatchLoopUntil (11);
        }
        tu::shot (*re, out.getChildFile ("vitrina_" + id + ".png"));
        std::cout << "vitrina " << id << std::endl;
    }
}
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    auto out = File::getCurrentWorkingDirectory().getChildFile ("fx_test_out");
    out.createDirectory();
    StringArray only;
    for (int i = 1; i < argc; ++i) only.add (argv[i]);
    if (SystemStats::getEnvironmentVariable ("FX_MEASURE", {}).isNotEmpty()) { measure::all (only); return 0; }
    if (SystemStats::getEnvironmentVariable ("FX_SHOWCASE", {}).isNotEmpty()) { showcase::run (only, out); return 0; }
    runAllPluginTests (out, only);
    if (only.isEmpty() || only.contains ("comportamiento")) behaviour::run();
    if (only.isEmpty() || only.contains ("arreglos")) arreglos::run();
    if (only.isEmpty() || only.contains ("tanda2")) tanda2::run();
    if (only.isEmpty() || only.contains ("tanda5")) tanda5::run();
    std::cout << (failures == 0 ? "TODO BIEN" : "FALLOS: " + String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}

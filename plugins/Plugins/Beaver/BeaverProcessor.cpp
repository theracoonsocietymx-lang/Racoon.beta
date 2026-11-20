#include "BeaverProcessor.h"
#include "BeaverEditor.h"
#include "RacoonDsp.h"

namespace racoon::beaver
{
using namespace juce;

struct Processor::Impl
{
    // scratch for one frame (voices are rendered one after another, so one set is enough)
    float A[numPartials], B[numPartials], M[numPartials], db1[numPartials], db2[numPartials];
    float noiseCalib = 1.0f;
    sdsp::Verb verb;
    AudioBuffer<float> wet;
    racoon::dsp::Smooth volSmooth;
};

Processor::~Processor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

// ===================================================================== names
const StringArray& Processor::sourceNames() { static StringArray a { "Forma", "Dibujo", "Resíntesis"_u, "Imagen" }; return a; }
const StringArray& Processor::shapeNames()
{
    static StringArray a { "Sierra", "Cuadrada", "Triángulo"_u, "Órgano grave"_u, "Vocal oscura", "Hueca", "Respiro", "Sub" };
    return a;
}
const StringArray& Processor::materialNames()
{
    static StringArray a { "Propio", "Voz de garganta", "Coro de ceniza", "Campana hundida", "Arco de piedra", "Viento de túnel"_u, "Órgano de basalto"_u,
                           "Imagen: horizonte", "Imagen: escalera" };
    return a;
}
const StringArray& Processor::filterNames()
{
    static StringArray a { "Paso bajo", "Paso banda", "Paso alto", "Rechazo", "Peine", "Vocal", "Escalón"_u };
    return a;
}
const StringArray& Processor::modSourceNames()
{
    static StringArray a { "Nada", "LFO 1", "LFO 2", "Envolvente 2", "Envolvente 3", "Velocidad", "Aftertouch", "Rueda", "Tecla", "Azar por nota" };
    return a;
}
const StringArray& Processor::modDestNames()
{
    static StringArray a { "Nada", "Mezcla A ↔ B"_u, "A: posición"_u, "B: posición"_u, "Corte 1", "Corte 2", "Resonancia 1", "Inclinación"_u, "Pares / impares",
                           "Difuminar en el tiempo", "Difuminar armónicos"_u, "Barrido", "A: formantes", "Afinación"_u, "Detune", "Volumen", "Paneo" };
    return a;
}
static const StringArray& lfoShapes() { static StringArray a { "Seno", "Triángulo"_u, "Sierra", "Cuadrada", "S&H", "Azar suave" }; return a; }

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    for (int L = 0; L < 2; ++L)
    {
        const String n = L == 0 ? "A: " : "B: ";
        P::choice (l, layerId (L, "fuente"), n + "fuente", sourceNames(), 0);
        P::choice (l, layerId (L, "forma"), n + "forma", shapeNames(), L == 0 ? 0 : 4);
        P::choice (l, layerId (L, "material"), n + "material", materialNames(), 0);
        P::pct (l, layerId (L, "pos"), n + "posición"_u, 0.0f, 100.0f, L == 0 ? 0.0f : 30.0f);
    }
    P::pct (l, "morph", "Mezcla A ↔ B"_u, 0.0f, 100.0f, 0.0f);
    P::flt (l, "inclinacion", "Inclinación"_u, P::range (-12.0f, 6.0f), -3.0f, "dB/oct", 1);
    P::choice (l, "f1_tipo", "Filtro 1: tipo", filterNames(), 0);
    P::hz (l, "f1_corte", "Filtro 1: corte", 20.0f, 20000.0f, 1400.0f);
    P::pct (l, "f1_res", "Filtro 1: resonancia", 0.0f, 100.0f, 15.0f);
    P::pct (l, "pulsar", "Pulsado", 0.0f, 100.0f, 0.0f);
    P::pct (l, "blurt", "Difuminar en el tiempo", 0.0f, 100.0f, 0.0f);
    P::integer (l, "unison", "Unison", 1, maxUnison, 3);
    P::flt (l, "detune", "Detune", P::range (0.0f, 100.0f), 12.0f, "ct", 0);
    P::pct (l, "espacio", "Espacio", 0.0f, 100.0f, 25.0f);
    P::db (l, "volume", "Volumen", -36.0f, 6.0f, -8.0f);
    P::adsr (l, 20.0f, 900.0f, 80.0f, 900.0f);
    for (int L = 0; L < 2; ++L)
    {
        const String n = L == 0 ? "A: " : "B: ";
        P::pct (l, layerId (L, "nivel"), n + "nivel", 0.0f, 100.0f, 100.0f);
        P::flt (l, layerId (L, "tono"), n + "afinación"_u, P::range (-24.0f, 24.0f), 0.0f, "st", 1);
        P::flt (l, layerId (L, "lectura"), n + "velocidad de lectura", P::range (-2.0f, 2.0f), 1.0f, "x", 2);
        P::toggle (l, layerId (L, "bucle"), n + "bucle", true);
        P::toggle (l, layerId (L, "conservar"), n + "conservar formantes", true);
        P::flt (l, layerId (L, "formante"), n + "formantes", P::range (-24.0f, 24.0f), 0.0f, "st", 1);
        P::pct (l, layerId (L, "seguirf0"), n + "seguir la afinación del audio"_u, 0.0f, 100.0f, 0.0f);
        P::pct (l, layerId (L, "ruido"), n + "ruido", 0.0f, 100.0f, 50.0f);
    }
    P::choice (l, "analisis", "Análisis"_u, { "Con su nota", "Como espectro" }, 0);
    P::integer (l, "notaref", "Nota de referencia", 24, 72, 33);
    P::choice (l, "imgeje", "Imagen: eje", { "Lineal", "Logarítmico"_u }, 1);
    P::flt (l, "imgrango", "Imagen: contraste", P::range (24.0f, 96.0f), 60.0f, "dB", 0);
    P::flt (l, "imgdur", "Imagen: duración"_u, P::range (1.0f, 30.0f, 8.0f), 8.0f, "s", 1);
    P::choice (l, "morphmodo", "Modo de mezcla", { "Fundido", "Espectral", "Por armónico"_u }, 1);
    P::integer (l, "armonicos", "Armónicos"_u, 1, numPartials, 255);
    P::flt (l, "pares", "Pares / impares", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
    P::choice (l, "armonizador", "Armonizador", { "Apagado", "Octavas", "Octavas y quintas", "Impares", "Serie de órgano"_u, "Primos" }, 0);
    P::pct (l, "armcant", "Armonizador: cantidad", 0.0f, 100.0f, 100.0f);
    for (int f = 1; f <= 2; ++f)
    {
        const String id = "f" + String (f) + "_", n = "Filtro " + String (f) + ": ";
        if (f == 2)
        {
            P::choice (l, id + "tipo", n + "tipo", filterNames(), 4);
            P::hz (l, id + "corte", n + "corte", 20.0f, 20000.0f, 220.0f);
            P::pct (l, id + "res", n + "resonancia", 0.0f, 100.0f, 0.0f);
        }
        P::flt (l, id + "pend", n + "pendiente", P::range (6.0f, 96.0f), f == 1 ? 24.0f : 12.0f, "dB/oct", 0);
        P::flt (l, id + "ancho", n + "ancho", P::range (0.1f, 3.0f), 1.0f, "oct", 2);
        P::flt (l, id + "vocal", n + "vocal", P::range (0.0f, 4.0f), 1.0f, "", 2);
        P::pct (l, id + "teclado", n + "seguir teclado", 0.0f, 100.0f, 30.0f);
        P::flt (l, id + "env", n + "envolvente", P::range (-100.0f, 100.0f), f == 1 ? 20.0f : 0.0f, "%", 0);
    }
    P::toggle (l, "f2_on", "Filtro 2: encendido", false);
    P::choice (l, "filtros", "Filtros", { "En serie", "En paralelo", "Solo el 1" }, 0);
    P::pct (l, "barrido", "Barrido", 0.0f, 100.0f, 0.0f);
    P::flt (l, "barvel", "Barrido: velocidad", P::range (-4.0f, 4.0f), 0.1f, "Hz", 2);
    P::toggle (l, "barsync", "Barrido: a tempo", false);
    P::choice (l, "bardiv", "Barrido: división"_u, RacoonProcessor::divisionNames(), 7);
    P::flt (l, "barsep", "Barrido: separación"_u, P::range (0.25f, 2.0f), 1.0f, "oct", 2);
    P::pct (l, "blurh", "Difuminar armónicos"_u, 0.0f, 100.0f, 0.0f);
    P::toggle (l, "blurcola", "Difuminar: solo la cola", false);
    P::flt (l, "pulsarcaida", "Pulsado: caída"_u, P::range (0.05f, 10.0f, 1.5f), 1.5f, "s", 2);
    P::choice (l, "fase", "Fase", { "Suave", "Pico", "Al azar" }, 0);
    P::pct (l, "estereo", "Armónicos en estéreo"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "apertura", "Apertura", 0.0f, 200.0f, 100.0f);
    P::integer (l, "voices", "Voces", 1, maxVoices, 12);
    P::ms (l, "glide", "Glide", 0.0f, 2000.0f, 0.0f, 200.0f);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);
    P::pct (l, "velamp", "Velocidad al volumen", 0.0f, 100.0f, 50.0f);
    P::pct (l, "velcorte", "Velocidad al filtro", 0.0f, 100.0f, 30.0f);
    P::pct (l, "espaciotam", "Espacio: tamaño"_u, 0.0f, 100.0f, 60.0f);
    P::choice (l, "calidad", "Calidad", { "ECO", "NORMAL", "HQ" }, 1);
    struct E { const char* id; float a, d, s, r; };
    for (const E e : { E { "env2", 0.01f, 1.2f, 30.0f, 1.0f }, E { "env3", 1.0f, 2.0f, 50.0f, 2.0f } })
    {
        const String id (e.id), n = id == "env2" ? "Env 2 " : "Env 3 ";
        P::flt (l, id + "_a", n + "ataque", P::range (0.001f, 30.0f, 1.0f), e.a, "s", 3);
        P::flt (l, id + "_d", n + "caída"_u, P::range (0.001f, 30.0f, 1.0f), e.d, "s", 3);
        P::pct (l, id + "_s", n + "sostenido", 0.0f, 100.0f, e.s);
        P::flt (l, id + "_r", n + "relajación"_u, P::range (0.005f, 60.0f, 1.5f), e.r, "s", 3);
    }
    static const float defRate[numLfo] { 0.08f, 0.25f };
    static const int defShape[numLfo] { 5, 0 };
    for (int k = 0; k < numLfo; ++k)
    {
        const String n = "LFO " + String (k + 1) + " ";
        P::flt (l, lfoId (k, "rate"), n + "velocidad", P::range (0.01f, 20.0f, 2.0f), defRate[k], "Hz", 2);
        P::toggle (l, lfoId (k, "sync"), n + "a tempo", false);
        P::choice (l, lfoId (k, "div"), n + "división"_u, RacoonProcessor::divisionNames(), 7);
        P::choice (l, lfoId (k, "shape"), n + "forma", lfoShapes(), defShape[k]);
    }
    static const int defSrc[numSlots] { msWheel, msLfo1, msAt, msNone, msNone, msNone, msNone, msNone };
    static const int defDst[numSlots] { mdMorph, mdPosA, mdCut1, mdNone, mdNone, mdNone, mdNone, mdNone };
    static const float defAmt[numSlots] { 100.0f, 15.0f, 40.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < numSlots; ++k)
    {
        const String n = "Mod " + String (k + 1) + " ";
        P::choice (l, slotId (k, "src"), n + "fuente", modSourceNames(), defSrc[k]);
        P::choice (l, slotId (k, "dst"), n + "destino", modDestNames(), defDst[k]);
        P::flt (l, slotId (k, "amt"), n + "cantidad", P::range (-100.0f, 100.0f), defAmt[k], "%", 0);
    }
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Beaver", "Aditivo", Colour (0xffe0789a), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    impl = std::make_unique<Impl>();
    static const char* lfoNames[4] { "rate", "sync", "div", "shape" };
    static const char* slotNames[3] { "src", "dst", "amt" };
    for (int k = 0; k < numLfo; ++k) for (int j = 0; j < 4; ++j) pLfo[k][j] = param (lfoId (k, lfoNames[j]));
    for (int k = 0; k < numSlots; ++k) for (int j = 0; j < 3; ++j) pSlot[k][j] = param (slotId (k, slotNames[j]));
    for (int L = 0; L < 2; ++L)
        for (int b = 0; b < numBars; ++b)
            bars[L][b].store (jmax (-60.0f, -20.0f * std::log10 ((float) (b + 1)) - (float) b * 0.4f));     // a soft saw to start drawing from
    startTimerHz (10);
}

// ===================================================================== materials
void Processor::setActive (int layer, std::shared_ptr<const FrameSet> fs)
{
    const SpinLock::ScopedLockType sl (setLock);
    if (active[layer] == fs) return;
    if (active[layer] != nullptr) graveyard.push_back (active[layer]);
    active[layer] = std::move (fs);
    ++materialVersion;
}

std::shared_ptr<const FrameSet> Processor::activeSet (int layer) const
{
    const SpinLock::ScopedLockType sl (setLock);
    return active[jlimit (0, 1, layer)];
}

void Processor::cleanGraveyard()
{
    const SpinLock::ScopedLockType sl (setLock);
    // what no voice holds any more can go (only the message thread frees materials)
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [] (const std::shared_ptr<const FrameSet>& f) { return f.use_count() == 1; }),
                     graveyard.end());
}

void Processor::syncMaterials (bool wait)
{
    for (int L = 0; L < 2; ++L)
    {
        const int src = (int) value (layerId (L, "fuente"));
        const int mat = (int) value (layerId (L, "material"));
        if (src != srcResintesis && src != srcImagen) { wanted[L] = -1; continue; }
        if (mat == 0)
        {
            std::shared_ptr<const FrameSet> o;
            { const SpinLock::ScopedLockType sl (setLock); o = own[L]; }
            setActive (L, o);
            wanted[L] = 0;
            continue;
        }
        if (wanted[L] == mat && activeSet (L) != nullptr && activeSet (L)->builtIn) continue;
        wanted[L] = mat;
        if (wait) setActive (L, builtInMaterial (mat));
        else
        {
            const int layer = L;
            pool.addJob ([this, layer, mat]
            {
                auto fs = builtInMaterial (mat);
                if ((int) value (layerId (layer, "material")) == mat) setActive (layer, fs);
            });
        }
    }
}

bool Processor::loadOwn (int layer, const File& file, bool wait)
{
    layer = jlimit (0, 1, layer);
    auto work = [this, layer, file]
    {
        analysing = 1;
        std::shared_ptr<FrameSet> fs;
        String err;
        const String ext = file.getFileExtension().toLowerCase();
        const bool picture = ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif";
        if (picture)
        {
            auto img = ImageFileFormat::loadFrom (file);
            fs = fromImage (img, value ("imgeje") > 0.5f, value ("imgrango"), value ("imgdur"), file.getFileNameWithoutExtension());
            if (fs == nullptr) err = "No se pudo abrir la imagen."_u;
        }
        else
        {
            AnalysisOptions opt;
            opt.asSpectrum = value ("analisis") > 0.5f;
            opt.refNote = (int) value ("notaref");
            fs = analyseFile (file, opt, err);
        }
        analysing = 0;
        if (fs == nullptr) { lastError = err; return false; }
        { const SpinLock::ScopedLockType sl (setLock); own[layer] = fs; }
        lastError.clear();
        auto apply = [this, layer, picture]
        {
            setRealValue (layerId (layer, "fuente"), (float) (picture ? srcImagen : srcResintesis));
            setRealValue (layerId (layer, "material"), 0.0f);
            wanted[layer] = -2;
            syncMaterials (true);
        };
        if (MessageManager::getInstanceWithoutCreating() != nullptr && ! MessageManager::getInstance()->isThisTheMessageThread())
            MessageManager::callAsync (apply);
        else apply();
        return true;
    };
    if (wait) return work();
    pool.addJob ([work] { work(); });
    return true;
}

void Processor::saveExtraState (ValueTree& t) const
{
    for (int L = 0; L < 2; ++L)
    {
        StringArray b;
        for (int k = 0; k < numBars; ++k) b.add (String (bars[L][k].load(), 2));
        t.setProperty (L == 0 ? "barsA" : "barsB", b.joinIntoString (" "), nullptr);
        std::shared_ptr<const FrameSet> o;
        { const SpinLock::ScopedLockType sl (setLock); o = own[L]; }
        if (o != nullptr)
        {
            auto mb = o->save();
            t.setProperty (L == 0 ? "ownA" : "ownB", mb.toBase64Encoding(), nullptr);
        }
    }
}

void Processor::loadExtraState (const ValueTree& t)
{
    for (int L = 0; L < 2; ++L)
    {
        StringArray b;
        b.addTokens (t.getProperty (L == 0 ? "barsA" : "barsB").toString(), " ", "");
        if (b.size() == numBars) for (int k = 0; k < numBars; ++k) bars[L][k].store (jlimit (-60.0f, 0.0f, b[k].getFloatValue()));
        const String s = t.getProperty (L == 0 ? "ownA" : "ownB").toString();
        std::shared_ptr<const FrameSet> o;
        if (s.isNotEmpty())
        {
            MemoryBlock mb;
            if (mb.fromBase64Encoding (s)) o = FrameSet::load (mb);
        }
        { const SpinLock::ScopedLockType sl (setLock); own[L] = o; }
        wanted[L] = -2;
    }
    syncMaterials (false);
}

// ===================================================================== prepare
void Processor::prepareDsp (double rate, int block)
{
    // the IFFT's scale: a bin of 1 makes a cosine of this amplitude
    {
        juce::dsp::FFT f (11);
        std::vector<float> b ((size_t) tableSize * 2, 0.0f);
        b[2] = 1.0f;
        f.performRealOnlyInverseTransform (b.data());
        float pk = 0.0f; for (int i = 0; i < tableSize; ++i) pk = jmax (pk, std::abs (b[(size_t) i]));
        calib = pk > 0.0f ? 1.0f / pk : 1.0f;
        juce::dsp::FFT g (9);
        std::vector<float> c (1024, 0.0f);
        c[2] = 1.0f;
        g.performRealOnlyInverseTransform (c.data());
        pk = 0.0f; for (int i = 0; i < 512; ++i) pk = jmax (pk, std::abs (c[(size_t) i]));
        impl->noiseCalib = pk > 0.0f ? 1.0f / pk : 1.0f;
    }
    for (auto& v : voices)
    {
        for (auto& t : v.tables) t.assign ((size_t) tableSize, 0.0f);
        if (v.fft == nullptr) v.fft = std::make_unique<juce::dsp::FFT> (11);
        if (v.noiseFft == nullptr) v.noiseFft = std::make_unique<juce::dsp::FFT> (9);
        v.fftBuf.assign ((size_t) tableSize * 2, 0.0f);
        v.noiseBuf.assign (1024, 0.0f);
        v.noiseOla.assign (512, 0.0f);
    }
    mix.setSize (2, jmax (block, 1024), false, false, true);
    impl->wet.setSize (2, jmax (block, 1024), false, false, true);
    impl->verb.setSampleRate (rate);
    impl->volSmooth.prepare (rate, 30.0);
    impl->volSmooth.reset (dsp::dbToGain (value ("volume")));
    resetDsp();
    syncMaterials (false);
}

void Processor::resetDsp()
{
    for (auto& v : voices)
    {
        v.active = v.held = v.sustained = v.stealing = false;
        v.set[0].reset(); v.set[1].reset();
        for (auto& t : v.tables) std::fill (t.begin(), t.end(), 0.0f);
        std::fill (v.noiseOla.begin(), v.noiseOla.end(), 0.0f);
    }
    impl->verb.reset();
    activeVoices = 0;
    pedal = false;
    keyboard.reset();
}

// ===================================================================== envelopes and LFOs
float Env::advance (int n, float a, float d, float s, float r, double sr)
{
    switch (stage)
    {
        case idle: level = 0.0f; break;
        case attack:
            level += (float) n / (float) (jmax (0.0005f, a) * sr);
            if (level >= 1.0f) { level = 1.0f; stage = decay; }
            break;
        case decay:
            level = s + (level - s) * (float) std::exp (-6.9 * n / (jmax (0.001f, d) * sr));
            if (std::abs (level - s) < 1.0e-4f) { level = s; stage = sustain; }
            break;
        case sustain: level += (s - level) * (float) (1.0 - std::exp (-(double) n / (0.01 * sr))); break;
        case release:
            level *= (float) std::exp (-6.9 * n / (jmax (0.005f, r) * sr));
            if (level < 1.0e-5f) { level = 0.0f; stage = idle; }
            break;
    }
    return level;
}

float Processor::lfoValue (Voice& v, int k, float ph, bool newCycle)
{
    if (newCycle) { v.lfoRand[k][0] = v.lfoRand[k][1]; v.lfoRand[k][1] = v.rng.nextBipolar(); }
    switch ((int) rd (pLfo[k][3]))
    {
        case 0: return std::sin (MathConstants<float>::twoPi * ph);
        case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        case 2: return 1.0f - 2.0f * ph;
        case 3: return ph < 0.5f ? 1.0f : -1.0f;
        case 4: return v.lfoRand[k][1];
        default: { const float t = ph * ph * (3.0f - 2.0f * ph); return v.lfoRand[k][0] + (v.lfoRand[k][1] - v.lfoRand[k][0]) * t; }
    }
}

// ===================================================================== voices
void Processor::noteOn (int note, float vel)
{
    const int limit = (int) value ("voices");
    const float glideMs = value ("glide");
    if (limit == 1)
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.held)
            {
                v.glideFrom = v.pitch; v.glidePos = glideMs > 0.5f ? 0.0f : 1.0f;
                v.note = note; v.velocity = vel;
                lastPitch = (float) note;
                return;
            }
    int playing = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++playing;
    while (playing >= limit)
    {
        Voice* victim = nullptr;
        for (auto& v : voices) if (v.active && ! v.stealing && ! v.held && ! v.sustained && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) for (auto& v : voices) if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) break;
        victim->stealing = true;
        --playing;
    }
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr) for (auto& v : voices) if (slot == nullptr || v.stealGain < slot->stealGain) slot = &v;
    auto& v = *slot;
    v.active = true; v.held = true; v.sustained = false; v.stealing = false; v.stealGain = 1.0f;
    v.note = note; v.velocity = vel; v.order = ++orderCounter; v.pressure = 0.0f;
    v.rng.seed ((uint32) (0x9e3779b9u * (uint32) (++noteCounter) + (uint32) note * 7919u));
    v.random = v.rng.nextBipolar();
    v.pitch = (float) note;
    v.glideFrom = glideMs > 0.5f && limit == 1 ? lastPitch : (float) note;
    v.glidePos = v.glideFrom != (float) note ? 0.0f : 1.0f;
    v.amp = Env(); v.env2 = Env(); v.env3 = Env();
    v.amp.start(); v.env2.start(); v.env3.start();
    v.age = 0.0f; v.f0Track = 1.0f; v.blurStarted = false; v.levelNow = 0.0f;
    for (int k = 0; k < numLfo; ++k) { v.lfoRand[k][0] = v.rng.nextBipolar(); v.lfoRand[k][1] = v.rng.nextBipolar(); v.lfoPhase[k] = 0.0; }
    for (int u = 0; u < maxUnison; ++u) { v.uniPhase[u] = v.rng.next01(); v.uniJit[u] = v.rng.nextBipolar() * 2.0f; }
    const int fase = (int) value ("fase");
    for (int i = 0; i < numPartials; ++i)
    {
        const float k = (float) (i + 1);
        v.phases[i] = fase == 0 ? MathConstants<float>::pi * k * k / (float) numPartials
                    : fase == 1 ? -MathConstants<float>::halfPi : v.rng.next01() * MathConstants<float>::twoPi;
    }
    for (auto& t : v.tables) std::fill (t.begin(), t.end(), 0.0f);
    std::fill (v.noiseOla.begin(), v.noiseOla.end(), 0.0f);
    v.noisePos = 0; v.noiseOn = false; v.hasTable = false; v.tablesEqual = false; v.stereoNow = v.stereoPrev = false;
    {
        const SpinLock::ScopedTryLockType tl (setLock);
        if (tl.isLocked()) { v.set[0] = active[0]; v.set[1] = active[1]; }
    }
    for (int L = 0; L < 2; ++L)
        v.readPos[L] = v.set[L] != nullptr ? value (layerId (L, "pos")) * 0.01 * (v.set[L]->frames - 1) : 0.0;
    v.frameLeft = 0;          // the first frame is built before the first sample
    v.cur = 0;
    v.freqNow = 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
    lastPitch = (float) note;
}

void Processor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && v.held)
        {
            v.held = false;
            if (pedal) v.sustained = true;
            else { v.amp.noteOff(); v.env2.noteOff(); v.env3.noteOff(); }
        }
}

void Processor::handleMidi (const MidiMessage& m)
{
    auto releaseAll = [this] { for (auto& v : voices) if (v.active) { v.held = v.sustained = false; v.amp.noteOff(); v.env2.noteOff(); v.env3.noteOff(); } };
    if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isChannelPressure()) channelPressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch()) { for (auto& v : voices) if (v.active && v.note == m.getNoteNumber()) v.pressure = (float) m.getAfterTouchValue() / 127.0f; }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1) wheel = (float) val / 127.0f;
        else if (cc == 64)
        {
            pedal = val >= 64;
            if (! pedal) for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; v.amp.noteOff(); v.env2.noteOff(); v.env3.noteOff(); }
        }
        else if (cc == 120) { for (auto& v : voices) if (v.active) v.stealing = true; }
        else if (cc == 123) releaseAll();
    }
    else if (m.isAllNotesOff()) releaseAll();
    else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; }
}

// log2 of each partial number
static const float* log2k()
{
    static float t[numPartials];
    static bool made = false;
    if (! made) { for (int i = 0; i < numPartials; ++i) t[i] = std::log2 ((float) (i + 1)); made = true; }
    return t;
}

// ===================================================================== one frame of partials
void Processor::buildFrame (Voice& v, float frameSeconds, const float* mod, float env2, float env3)
{
    ignoreUnused (env3);
    auto& I = *impl;
    const float f0 = v.freqNow;
    const int quality = (int) value ("calidad");
    const int maxK = jmin ((int) value ("armonicos"), quality == 0 ? 127 : numPartials);
    v.noiseOn = false;
    float noiseDb[numNoiseBands]; for (auto& n : noiseDb) n = -200.0f;

    // ---- the two layers
    for (int L = 0; L < 2; ++L)
    {
        float* out = L == 0 ? I.A : I.B;
        const float nivel = value (layerId (L, "nivel")) * 0.01f;
        if (nivel <= 0.0f) { std::fill (out, out + numPartials, 0.0f); continue; }
        const int src = (int) value (layerId (L, "fuente"));
        const float tono = value (layerId (L, "tono"));
        const float pos = value (layerId (L, "pos")) * 0.01f + (L == 0 ? mod[mdPosA] : mod[mdPosB]);
        const auto& set = v.set[L];
        if ((src == srcResintesis || src == srcImagen) && set != nullptr && set->frames > 0)
        {
            const bool picture = set->noise.empty();
            const bool keep = ! picture && value (layerId (L, "conservar")) > 0.5f;
            const float formant = value (layerId (L, "formante")) + (L == 0 ? mod[mdFormantA] * 24.0f : 0.0f);
            const float ratio = keep ? f0 / (set->refHz * std::pow (2.0f, formant / 12.0f)) : std::pow (2.0f, -tono / 12.0f);
            material (*set, (float) v.readPos[L], ratio, out);
            // the pitch movement of the audio (vibrato, slides)
            const float follow = value (layerId (L, "seguirf0")) * 0.01f;
            if (follow > 0.0f && ! set->f0.empty())
            {
                const float tr = set->f0[(size_t) jlimit (0, set->frames - 1, (int) v.readPos[L])];
                if (tr > 0.0f) v.f0Track = std::pow (tr / set->refHz, follow);
            }
            // the noise the analysis set apart
            const float ruido = value (layerId (L, "ruido")) * 0.01f;
            if (! picture && ruido > 0.0f)
            {
                const int fr = jlimit (0, set->frames - 1, (int) v.readPos[L]);
                const float w = L == 0 ? 1.0f - jlimit (0.0f, 1.0f, value ("morph") * 0.01f + mod[mdMorph]) : jlimit (0.0f, 1.0f, value ("morph") * 0.01f + mod[mdMorph]);
                const float g = linToDb (ruido * nivel * w + 1.0e-9f);
                const float shift = keep ? 1.0f : f0 / set->refHz;
                for (int b = 0; b < numNoiseBands; ++b)
                {
                    // with formants off the bands move with the note
                    const float srcBand = shift == 1.0f ? (float) b : (float) b - std::log2 (shift) / std::log2 (320.0f) * (float) numNoiseBands;
                    const int b0 = jlimit (0, numNoiseBands - 1, (int) std::floor (srcBand));
                    const float d = (srcBand < -0.5f || srcBand > numNoiseBands - 0.5f) ? -200.0f : set->noiseDb (fr, b0);
                    if (d <= -95.0f) continue;          // the floor of the scale: nothing there
                    const float total = d + g;
                    noiseDb[b] = noiseDb[b] < -150.0f ? total : 10.0f * std::log10 (std::pow (10.0f, noiseDb[b] * 0.1f) + std::pow (10.0f, total * 0.1f));
                }
                v.noiseOn = true;
            }
            // read on
            const double frameSecs = picture ? value ("imgdur") / jmax (1, set->frames) : set->frameSeconds;
            const double step = value (layerId (L, "lectura")) * frameSeconds / jmax (1.0e-4, frameSecs);
            const double startF = jlimit (0.0, (double) set->frames - 1.0, (double) value (layerId (L, "pos")) * 0.01 * (set->frames - 1));
            v.readPos[L] += step;
            if (value (layerId (L, "bucle")) > 0.5f)
            {
                const double len = jmax (1.0, (double) set->frames - 1.0 - startF);
                if (v.readPos[L] > set->frames - 1) v.readPos[L] = startF + std::fmod (v.readPos[L] - startF, len);
                if (v.readPos[L] < startF) v.readPos[L] = set->frames - 1 - std::fmod (startF - v.readPos[L], len);
            }
            else v.readPos[L] = jlimit (0.0, (double) set->frames - 1.0, v.readPos[L]);
        }
        else if (src == srcDibujo)
        {
            float b[numBars];
            for (int k = 0; k < numBars; ++k) b[k] = bars[L][k].load (std::memory_order_relaxed);
            drawn (b, out);
        }
        else shape ((int) value (layerId (L, "forma")), pos, f0, out);
        // a layer's own tuning inside the note: the grid resampled (materials did it already)
        if (std::abs (tono) > 0.01f && ! ((src == srcResintesis || src == srcImagen) && set != nullptr))
        {
            const float r = std::pow (2.0f, -tono / 12.0f);
            for (int i = 0; i < numPartials; ++i) I.db1[i] = out[i];
            for (int i = 0; i < numPartials; ++i)
            {
                const float s = (float) (i + 1) * r - 1.0f;
                if (s < -0.5f || s > numPartials - 1.0f) { out[i] = 0.0f; continue; }
                const int s0 = jlimit (0, numPartials - 1, (int) std::floor (s)), s1 = jmin (numPartials - 1, s0 + 1);
                const float u = jlimit (0.0f, 1.0f, s - (float) s0);
                out[i] = s < 0.0f ? I.db1[0] * (s + 1.0f) : I.db1[s0] + (I.db1[s1] - I.db1[s0]) * u;
            }
        }
        for (int i = 0; i < numPartials; ++i) out[i] *= nivel;
    }

    // ---- morph
    const float m = jlimit (0.0f, 1.0f, value ("morph") * 0.01f + mod[mdMorph]);
    const int mode = (int) value ("morphmodo");
    for (int i = 0; i < numPartials; ++i)
    {
        const float a = I.A[i], b = I.B[i];
        float r;
        if (m <= 0.0f) r = a;
        else if (m >= 1.0f) r = b;
        else if (mode == 0) r = a + (b - a) * m;
        else if (mode == 1) r = dbToLin (linToDb (a) * (1.0f - m) + linToDb (b) * m);
        else
        {
            const float kc = std::pow (2.0f, 9.0f * m), o = std::log2 ((float) (i + 1) / kc);
            const float t = jlimit (0.0f, 1.0f, o + 0.5f);
            const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::pi * t);
            r = a * (1.0f - w) + b * w;
        }
        I.M[i] = r;
    }
    float* M = I.M;

    // ---- harmonizer, even and odd, tilt
    const int harm = (int) value ("armonizador");
    const float harmAmt = value ("armcant") * 0.01f;
    const float pares = jlimit (-1.0f, 1.0f, value ("pares") * 0.01f + mod[mdEvenOdd]);
    const float tilt = value ("inclinacion") + mod[mdTilt] * 12.0f;
    for (int i = 0; i < numPartials; ++i)
    {
        const int k = i + 1;
        if (harm > 0 && ! harmonizerKeeps (harm, k)) M[i] *= 1.0f - harmAmt;
        if (pares < 0.0f && (k & 1) == 0) M[i] *= 1.0f + pares;
        if (pares > 0.0f && (k & 1) == 1 && k > 1) M[i] *= 1.0f - pares;
        if (tilt != 0.0f) M[i] *= std::exp (tilt * log2k()[i] * 0.1151293f);
    }

    // ---- filters, sweep
    FilterCurve f1, f2;
    const float key = ((float) v.note - 60.0f) / 12.0f;
    const float velOpen = value ("velcorte") * 0.01f * (v.velocity - 0.787f) * 3.0f;
    f1.type = (int) value ("f1_tipo"); f1.slope = value ("f1_pend"); f1.res = jlimit (0.0f, 1.0f, value ("f1_res") * 0.01f + mod[mdRes1]);
    f1.width = value ("f1_ancho"); f1.vowel = value ("f1_vocal");
    f1.fc = jlimit (20.0f, 20000.0f, value ("f1_corte") * std::pow (2.0f, value ("f1_teclado") * 0.01f * key + value ("f1_env") * 0.01f * env2 * 5.0f + mod[mdCut1] * 5.0f + velOpen));
    const bool use2 = value ("f2_on") > 0.5f && (int) value ("filtros") != 2;
    if (use2)
    {
        f2.type = (int) value ("f2_tipo"); f2.slope = value ("f2_pend"); f2.res = value ("f2_res") * 0.01f; f2.width = value ("f2_ancho"); f2.vowel = value ("f2_vocal");
        f2.fc = jlimit (20.0f, 20000.0f, value ("f2_corte") * std::pow (2.0f, value ("f2_teclado") * 0.01f * key + value ("f2_env") * 0.01f * env2 * 5.0f + mod[mdCut2] * 5.0f));
    }
    const bool parallel = (int) value ("filtros") == 1;
    const float sweep = jlimit (0.0f, 1.0f, value ("barrido") * 0.01f + mod[mdSweep]);
    const float sep = value ("barsep");
    const float sweepPh = (float) sweepPhase;
    const float nyq = (float) sr * 0.45f;
    const float maxRatio = std::pow (2.0f, (value ("detune") * 0.5f + 6.0f + jmax (0.0f, mod[mdDetune]) * 50.0f) / 1200.0f);
    for (int i = 0; i < numPartials; ++i)
    {
        const int k = i + 1;
        const float f = (float) k * f0;
        if (i >= maxK || f * maxRatio >= nyq) { M[i] = 0.0f; continue; }
        float g = f1.db (f);
        if (use2) g = parallel ? linToDb (0.5f * (dbToLin (g) + dbToLin (f2.db (f)))) : g + f2.db (f);
        if (sweep > 0.0f)
        {
            float x = std::log2 (jmax (1.0f, f)) / sep - sweepPh;
            x -= std::floor (x);
            const float d = std::abs (x - 0.075f);
            if (d < 0.075f) g -= 24.0f * sweep * (0.5f + 0.5f * std::cos (MathConstants<float>::pi * d / 0.075f));
        }
        M[i] *= g <= -119.0f ? 0.0f : std::exp (g * 0.1151293f);
        // band limit: the top third of an octave fades, nothing above the limit
        const float top = f * maxRatio;
        if (top >= nyq) M[i] = 0.0f;
        else if (top > nyq * 0.7937f) { const float t = (top - nyq * 0.7937f) / (nyq * 0.2063f); M[i] *= 0.5f + 0.5f * std::cos (MathConstants<float>::pi * t); }
    }

    // ---- blur across partials and in time, pluck
    const float blurH = jlimit (0.0f, 1.0f, value ("blurh") * 0.01f + mod[mdBlurH]);
    if (blurH > 0.0f) blurAcross (M, 12.0f * blurH, I.db1, I.db2);
    const float blurT = jlimit (0.0f, 1.0f, value ("blurt") * 0.01f + mod[mdBlurT]);
    if (blurT > 0.0f && v.blurStarted)
    {
        const float tau = 4.0f * blurT * blurT;
        const float c = std::exp (-frameSeconds / jmax (0.001f, tau));
        const bool tailOnly = value ("blurcola") > 0.5f;
        for (int i = 0; i < numPartials; ++i)
        {
            float& s = v.blurState[i];
            s = (tailOnly && M[i] > s) ? M[i] : c * s + (1.0f - c) * M[i];
            M[i] = s;
        }
    }
    else for (int i = 0; i < numPartials; ++i) v.blurState[i] = M[i];
    v.blurStarted = true;
    const float pul = value ("pulsar") * 0.01f;
    if (pul > 0.0f)
    {
        const float pc = value ("pulsarcaida");
        for (int i = 0; i < numPartials; ++i) M[i] *= std::exp (-v.age * (1.0f + 30.0f * pul * (float) i / 64.0f) / pc);
    }
    int heard = 0;
    for (int i = 0; i < numPartials; ++i)
    {
        if (! std::isfinite (M[i]) || M[i] < 1.0e-7f) M[i] = 0.0f;
        else ++heard;
        v.lastLevels[i] = M[i];
    }
    partialsNow.store (heard);

    // ---- the table(s): one inverse FFT each. When the partials have not moved since the current table was built
    // (a held pad), the table stays and no FFT runs
    const float st = value ("estereo") * 0.01f;
    {
        float peak = 0.0f, diff = 0.0f;
        for (int i = 0; i < numPartials; ++i) { peak = jmax (peak, M[i]); diff = jmax (diff, std::abs (M[i] - v.builtLevels[i])); }
        if (v.hasTable && diff <= peak * 0.002f && (st > 0.0f) == v.stereoNow)
        {
            if (! v.tablesEqual)
            {
                std::copy (v.tables[v.cur].begin(), v.tables[v.cur].end(), v.tables[1 - v.cur].begin());
                std::copy (v.tables[2 + v.cur].begin(), v.tables[2 + v.cur].end(), v.tables[3 - v.cur].begin());
                v.tablesEqual = true;
            }
            v.stereoPrev = v.stereoNow;
            if (v.noiseOn) for (int b = 0; b < numNoiseBands; ++b) v.noiseBands[b] = noiseDb[b];
            return;
        }
        std::copy (M, M + numPartials, v.builtLevels);
        v.hasTable = true;
        v.tablesEqual = false;
    }
    v.cur = 1 - v.cur;
    auto build = [&] (std::vector<float>& table, float side)
    {
        auto& buf = v.fftBuf;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < numPartials; ++i)
        {
            if (M[i] == 0.0f) continue;
            float a = M[i] * calib;
            if (side != 0.0f)
            {
                const float p = ((i + 1) & 1) ? -st : st;                  // odd left, even right
                const float th = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
                a *= (side < 0.0f ? std::cos (th) : std::sin (th)) * 1.41421f;
            }
            buf[(size_t) 2 * (i + 1)] = a * std::cos (v.phases[i]);
            buf[(size_t) 2 * (i + 1) + 1] = a * std::sin (v.phases[i]);
        }
        v.fft->performRealOnlyInverseTransform (buf.data());
        std::copy (buf.begin(), buf.begin() + tableSize, table.begin());
    };
    v.stereoPrev = v.stereoNow;
    v.stereoNow = st > 0.0f;
    if (st > 0.0f) { build (v.tables[v.cur], -1.0f); build (v.tables[2 + v.cur], 1.0f); }
    else { build (v.tables[v.cur], 0.0f); std::copy (v.tables[v.cur].begin(), v.tables[v.cur].end(), v.tables[2 + v.cur].begin()); }

    // ---- noise bands for this frame
    if (v.noiseOn) for (int b = 0; b < numNoiseBands; ++b) v.noiseBands[b] = noiseDb[b];
}

// ===================================================================== one voice
static inline float hermite (const float* t, double phase)
{
    const double x = phase * tableSize;
    const int i = (int) x;
    const float f = (float) (x - i);
    const float xm1 = t[(i - 1) & (tableSize - 1)], x0 = t[i & (tableSize - 1)], x1 = t[(i + 1) & (tableSize - 1)], x2 = t[(i + 2) & (tableSize - 1)];
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void Processor::renderVoice (Voice& v, float* L, float* R, int start, int total)
{
    const double srate = sr;
    const int quality = (int) value ("calidad");
    const int H = jmax (16, (int) std::round ((quality == 0 ? 256 : quality == 1 ? 128 : 64) * srate / 48000.0));
    const float aA = value ("adsr_a") * 0.001f, aD = value ("adsr_d") * 0.001f, aS = value ("adsr_s") * 0.01f, aR = value ("adsr_r") * 0.001f;
    const float e2[4] { value ("env2_a"), value ("env2_d"), value ("env2_s") * 0.01f, value ("env2_r") };
    const float e3[4] { value ("env3_a"), value ("env3_d"), value ("env3_s") * 0.01f, value ("env3_r") };
    const int uni = (int) value ("unison");
    const float detune = value ("detune"), apert = value ("apertura") * 0.01f;
    const float bendRange = value ("bendrange"), glideMs = value ("glide");
    const float velAmp = value ("velamp") * 0.01f;
    const float uniNorm = 1.0f / std::sqrt ((float) uni);
    const float stealStep = (float) (1.0 / (0.005 * srate));
    float lfoRate[numLfo];
    for (int k = 0; k < numLfo; ++k) lfoRate[k] = rd (pLfo[k][1]) > 0.5f ? (float) (1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm())) : rd (pLfo[k][0]);
    int slotSrc[numSlots], slotDst[numSlots]; float slotAmt[numSlots];
    for (int k = 0; k < numSlots; ++k) { slotSrc[k] = (int) rd (pSlot[k][0]); slotDst[k] = (int) rd (pSlot[k][1]); slotAmt[k] = rd (pSlot[k][2]) * 0.01f; }
    const float noiseNorm = impl->noiseCalib / std::sqrt (1.5f);
    const float binHz = (float) (srate / 512.0);
    static float hann[512];
    static bool hannMade = false;
    if (! hannMade) { for (int i = 0; i < 512; ++i) hann[i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / 512.0f); hannMade = true; }

    float mod[numModDests] {};
    float uniInc[maxUnison], uniL[maxUnison], uniR[maxUnison];
    for (int done = 0; done < total && v.active; )
    {
        // ---- a new frame: the control values, then the partials
        if (v.frameLeft <= 0)
        {
            v.frameLen = H; v.frameLeft = H;
            const float fs = (float) (H / srate);
            const float env2 = v.env2.advance (H, e2[0], e2[1], e2[2], e2[3], srate);
            const float env3 = v.env3.advance (H, e3[0], e3[1], e3[2], e3[3], srate);
            float lfo[numLfo];
            for (int k = 0; k < numLfo; ++k)
            {
                const double g = globalLfo[k] + lfoRate[k] * (start + done) / srate;
                const float ph = (float) (g - std::floor (g));
                const bool wrapped = ph < (float) v.lfoPhase[k];
                v.lfoPhase[k] = ph;
                lfo[k] = lfoValue (v, k, ph, wrapped);
            }
            float src[numModSources] {};
            src[msLfo1] = lfo[0]; src[msLfo2] = lfo[1]; src[msEnv2] = env2; src[msEnv3] = env3; src[msVel] = v.velocity;
            src[msAt] = jmax (v.pressure, channelPressure); src[msWheel] = wheel; src[msKey] = ((float) v.note - 48.0f) / 36.0f; src[msRandom] = v.random;
            std::fill (mod, mod + numModDests, 0.0f);
            for (int k = 0; k < numSlots; ++k) if (slotSrc[k] != msNone && slotDst[k] != mdNone) mod[slotDst[k]] += src[slotSrc[k]] * slotAmt[k];
            if (v.glidePos < 1.0f) v.glidePos = jmin (1.0f, v.glidePos + fs / (jmax (1.0f, glideMs) * 0.001f));
            v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glidePos;
            const float semis = v.pitch + bend * bendRange + mod[mdPitch] * 12.0f;
            v.freqNow = 440.0f * std::pow (2.0f, (semis - 69.0f) / 12.0f) * v.f0Track;
            buildFrame (v, fs, mod, env2, env3);
            v.age += fs;
            const float pan = jlimit (-1.0f, 1.0f, mod[mdPan]);
            const float th = (pan + 1.0f) * MathConstants<float>::pi * 0.25f;
            v.panL = std::cos (th) * 1.41421f; v.panR = std::sin (th) * 1.41421f;
            const float det = detune + jmax (0.0f, mod[mdDetune]) * 50.0f;
            for (int u = 0; u < uni; ++u)
            {
                const float spread = uni == 1 ? 0.0f : (float) u / (float) (uni - 1) * 2.0f - 1.0f;
                const float cents = uni == 1 ? 0.0f : det * spread * 0.5f + v.uniJit[u];
                uniInc[u] = (float) (v.freqNow * std::pow (2.0f, cents / 1200.0f) / srate);
                const float p = jlimit (-1.0f, 1.0f, spread * apert * 0.5f);
                const float t2 = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
                uniL[u] = std::cos (t2) * 1.41421f; uniR[u] = std::sin (t2) * 1.41421f;
            }
        }
        else
            for (int u = 0; u < uni; ++u)
            {
                const float spread = uni == 1 ? 0.0f : (float) u / (float) (uni - 1) * 2.0f - 1.0f;
                const float cents = uni == 1 ? 0.0f : (detune + jmax (0.0f, mod[mdDetune]) * 50.0f) * spread * 0.5f + v.uniJit[u];
                uniInc[u] = (float) (v.freqNow * std::pow (2.0f, cents / 1200.0f) / srate);
                const float p = jlimit (-1.0f, 1.0f, spread * apert * 0.5f);
                const float t2 = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
                uniL[u] = std::cos (t2) * 1.41421f; uniR[u] = std::sin (t2) * 1.41421f;
            }

        const int n = jmin (16, v.frameLeft, total - done);
        const float envFrom = v.amp.level;
        const float envTo = v.amp.advance (n, aA, aD, aS, aR, srate);
        const float velGain = (1.0f - velAmp) + velAmp * v.velocity;
        const float lvl = velGain * jmax (0.0f, 1.0f + mod[mdLevel]) * 0.25f;
        const float* tpL = v.tables[1 - v.cur].data(); const float* tcL = v.tables[v.cur].data();
        const float* tpR = v.tables[3 - v.cur].data(); const float* tcR = v.tables[2 + v.cur].data();     // right (stereo per partial)
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) (v.frameLen - v.frameLeft + i) / (float) v.frameLen;
            float sl = 0.0f, sr2 = 0.0f;
            for (int u = 0; u < uni; ++u)
            {
                const double ph = v.uniPhase[u];
                const float lp = hermite (tpL, ph);
                const float l = v.tablesEqual ? lp : lp + (hermite (tcL, ph) - lp) * x;
                float r = l;
                if (v.stereoNow || v.stereoPrev) { const float rp = hermite (tpR, ph); r = v.tablesEqual ? rp : rp + (hermite (tcR, ph) - rp) * x; }
                sl += l * uniL[u]; sr2 += r * uniR[u];
                double np = ph + uniInc[u];
                if (np >= 1.0) np -= 1.0;
                v.uniPhase[u] = np;
            }
            // the noise the material carried (overlap-add, a new grain every 128 samples)
            float nz = 0.0f;
            if (v.noiseOn)
            {
                if ((v.noisePos & 127) == 0)
                {
                    auto& nb = v.noiseBuf;
                    std::fill (nb.begin(), nb.end(), 0.0f);
                    for (int j = 1; j < 256; ++j)
                    {
                        const float f = (float) j * binHz;
                        const float bp = std::log2 (f / 50.0f) / std::log2 (320.0f) * (float) numNoiseBands - 0.5f;
                        if (bp < -0.5f || bp > numNoiseBands - 0.5f) continue;
                        const int b0 = jlimit (0, numNoiseBands - 1, (int) std::floor (bp)), b1 = jmin (numNoiseBands - 1, b0 + 1);
                        const float t = jlimit (0.0f, 1.0f, bp - (float) b0);
                        const float d = v.noiseBands[b0] + (v.noiseBands[b1] - v.noiseBands[b0]) * t;
                        if (d < -150.0f) continue;
                        const float a = std::sqrt (std::pow (10.0f, d * 0.1f) * binHz) * noiseNorm;
                        const float phs = v.rng.next01() * MathConstants<float>::twoPi;
                        nb[(size_t) 2 * j] = a * std::cos (phs); nb[(size_t) 2 * j + 1] = a * std::sin (phs);
                    }
                    v.noiseFft->performRealOnlyInverseTransform (nb.data());
                    const int base = v.noisePos & 511;
                    for (int j = 0; j < 512; ++j) v.noiseOla[(size_t) ((base + j) & 511)] += nb[(size_t) j] * hann[j];
                }
                const int at = v.noisePos & 511;
                nz = v.noiseOla[(size_t) at];
                v.noiseOla[(size_t) at] = 0.0f;
                ++v.noisePos;
            }
            const float env = envFrom + (envTo - envFrom) * (float) (i + 1) / (float) n;
            float g = env * lvl;
            if (v.stealing) { v.stealGain -= stealStep; if (v.stealGain <= 0.0f) { v.stealGain = 0.0f; v.active = false; } g *= v.stealGain; }
            L[start + done + i] += (sl * uniNorm + nz * 4.0f) * g * v.panL;
            R[start + done + i] += (sr2 * uniNorm + nz * 4.0f) * g * v.panR;
        }
        v.frameLeft -= n;
        done += n;
        if (v.active && ! v.held && ! v.sustained && v.amp.stage == Env::idle) v.active = false;
    }
}

// ===================================================================== block
void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const int ch = jmin (2, getMainBusNumOutputChannels());
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    if (mix.getNumSamples() < n) mix.setSize (2, n, false, false, true);
    mix.clear (0, n);
    float* ML = mix.getWritePointer (0);
    float* MR = mix.getWritePointer (1);
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        for (auto& v : voices) if (v.active) renderVoice (v, ML, MR, pos, end - pos);
        pos = end;
    };
    for (const auto ev : midi) { renderTo (jlimit (0, n, ev.samplePosition)); handleMidi (ev.getMessage()); }
    renderTo (n);
    for (int k = 0; k < numLfo; ++k)
    {
        const double rate = rd (pLfo[k][1]) > 0.5f ? 1.0 / syncSeconds ((int) rd (pLfo[k][2]), bpm()) : rd (pLfo[k][0]);
        globalLfo[k] += rate * n / sr;
        globalLfo[k] -= std::floor (globalLfo[k]);
    }
    {
        const double rate = value ("barsync") > 0.5f ? 1.0 / syncSeconds ((int) value ("bardiv"), bpm()) : value ("barvel");
        sweepPhase += rate * n / sr;
        sweepPhase -= std::floor (sweepPhase);
    }
    int count = 0;
    const Voice* newest = nullptr;
    for (auto& v : voices) if (v.active && ! v.stealing) { ++count; if (newest == nullptr || v.order > newest->order) newest = &v; }
    activeVoices.store (count);
    if (newest != nullptr) for (int i = 0; i < numPartials; ++i) levelView[i].store (newest->lastLevels[i], std::memory_order_relaxed);

    const float esp = value ("espacio") * 0.01f;
    if (esp > 0.001f)
    {
        sdsp::Verb::Parameters vp;
        vp.roomSize = 0.3f + value ("espaciotam") * 0.0065f; vp.damping = 0.6f; vp.wetLevel = 0.33f; vp.dryLevel = 0.0f; vp.width = 1.0f;
        impl->verb.setParameters (vp);
        if (impl->wet.getNumSamples() < n) impl->wet.setSize (2, n, false, false, true);
        impl->wet.copyFrom (0, 0, mix, 0, 0, n);
        impl->wet.copyFrom (1, 0, mix, 1, 0, n);
        impl->verb.processStereo (impl->wet.getWritePointer (0), impl->wet.getWritePointer (1), n);
        mix.addFrom (0, 0, impl->wet, 0, 0, n, esp);
        mix.addFrom (1, 0, impl->wet, 1, 0, n, esp);
    }
    else impl->verb.reset();
    impl->volSmooth.target = dsp::dbToGain (value ("volume"));
    for (int i = 0; i < n; ++i) { const float vol = impl->volSmooth.next(); ML[i] *= vol; MR[i] *= vol; }
    // a dying room tail is cut at -140 dB so the silence is exact
    if (count == 0 && jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n)) < 1.0e-7f) { mix.clear (0, n); impl->verb.reset(); }
    if (ch == 1) { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.addFrom (0, 0, mix, 1, 0, n); buffer.applyGain (0, 0, n, 0.5f); }
    else { buffer.copyFrom (0, 0, mix, 0, 0, n); buffer.copyFrom (1, 0, mix, 1, 0, n); }
}

// ===================================================================== presets: dark and low
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    // fuente 0 Forma 1 Dibujo 2 Resíntesis 3 Imagen; forma 0 sierra 1 cuadrada 2 triángulo 3 órgano 4 vocal 5 hueca 6 respiro 7 sub
    // material 1 voz 2 coro 3 campana 4 arco 5 viento 6 órgano 7 horizonte 8 escalera; filtro 0 bajo 1 banda 2 alto 3 rechazo 4 peine 5 vocal 6 escalón
    std::vector<FactoryPreset> p;
    p.push_back ({ "Sierra tibia", "Pads", {} });
    p.push_back ({ "Órgano de basalto"_u, "Pads", V { { "a_fuente", 2 }, { "a_material", 6 }, { "a_conservar", 0 }, { "a_lectura", 0.5f }, { "blurt", 25 }, { "armonizador", 4 }, { "armcant", 60 },
                                                        { "f1_corte", 2200 } } });
    p.push_back ({ "Coro de ceniza", "Resíntesis"_u, V { { "a_fuente", 2 }, { "a_material", 2 }, { "a_formante", -3 }, { "blurh", 20 }, { "blurt", 30 }, { "espacio", 45 },
                                                          { "adsr_a", 600 } } });
    p.push_back ({ "Voz de garganta", "Resíntesis"_u, V { { "a_fuente", 2 }, { "a_material", 1 }, { "a_lectura", 0.25f }, { "a_seguirf0", 60 }, { "a_ruido", 40 },
                                                           { "f1_tipo", 5 }, { "f1_vocal", 1 }, { "f1_corte", 900 } } });
    p.push_back ({ "Bajo de humo", "Bajos", V { { "a_forma", 1 }, { "a_pos", 20 }, { "pares", -80 }, { "f1_corte", 420 }, { "f1_pend", 36 }, { "unison", 1 }, { "voices", 1 },
                                                { "glide", 60 }, { "adsr_a", 2 }, { "adsr_d", 400 }, { "adsr_s", 70 }, { "adsr_r", 120 }, { "inclinacion", -6 } } });
    p.push_back ({ "Sub de piedra", "Bajos", V { { "a_forma", 7 }, { "a_pos", 30 }, { "armonicos", 8 }, { "unison", 1 }, { "voices", 1 }, { "adsr_a", 1 }, { "adsr_d", 600 },
                                                 { "adsr_s", 85 }, { "adsr_r", 150 } } });
    p.push_back ({ "Respiro de ballena", "Texturas", V { { "a_fuente", 2 }, { "a_material", 5 }, { "a_ruido", 90 }, { "a_formante", -12 }, { "blurt", 70 }, { "blurcola", 1 },
                                                         { "espacio", 55 } } });
    p.push_back ({ "Campana hundida", "Pulsados", V { { "a_fuente", 2 }, { "a_material", 3 }, { "a_conservar", 0 }, { "pulsar", 60 }, { "pulsarcaida", 5 }, { "f1_corte", 900 }, { "espacio", 50 } } });
    p.push_back ({ "Pad de carbón"_u, "Pads", V { { "a_pos", 30 }, { "b_pos", 50 }, { "m1_src", msLfo1 }, { "m1_dst", mdMorph }, { "m1_amt", 70 }, { "lfo1_rate", 0.05f },
                                                   { "unison", 5 }, { "detune", 18 } } });
    p.push_back ({ "Formante de cueva", "Pads", V { { "a_forma", 6 }, { "f1_tipo", 5 }, { "f1_corte", 700 }, { "m2_src", msLfo2 }, { "m2_dst", mdCut1 }, { "m2_amt", 25 },
                                                    { "lfo2_rate", 0.12f }, { "blurh", 15 } } });
    p.push_back ({ "Cuerda espectral", "Pulsados", V { { "pulsar", 80 }, { "pulsarcaida", 2.5f }, { "f1_corte", 1800 }, { "f1_res", 30 }, { "unison", 2 } } });
    p.push_back ({ "Neblina armónica"_u, "Texturas", V { { "a_forma", 4 }, { "blurh", 80 }, { "blurt", 60 }, { "unison", 6 }, { "detune", 22 }, { "estereo", 40 }, { "espacio", 50 } } });
    p.push_back ({ "Túnel de viento"_u, "Texturas", V { { "a_fuente", 2 }, { "a_material", 5 }, { "a_lectura", 0.3f }, { "a_ruido", 100 }, { "f1_tipo", 1 }, { "f1_corte", 400 },
                                                         { "f1_res", 40 } } });
    p.push_back ({ "Sótano en fase"_u, "Pads", V { { "a_pos", 40 }, { "barrido", 70 }, { "barvel", 0.07f }, { "barsep", 0.75f }, { "f1_corte", 1600 } } });
    p.push_back ({ "Horizonte", "Imagen", V { { "a_fuente", 3 }, { "a_material", 7 }, { "imgdur", 12 }, { "a_bucle", 1 }, { "blurt", 30 }, { "espacio", 40 } } });
    p.push_back ({ "Escalera de humo", "Imagen", V { { "a_fuente", 3 }, { "a_material", 8 }, { "imgdur", 4 }, { "a_bucle", 1 }, { "f1_corte", 1500 }, { "blurt", 10 } } });
    p.push_back ({ "Peine oscuro", "Pads", V { { "f2_on", 1 }, { "f2_tipo", 4 }, { "f2_corte", 220 }, { "filtros", 0 }, { "f1_corte", 1200 }, { "m3_src", msAt }, { "m3_dst", mdCut2 },
                                              { "m3_amt", 30 } } });
    p.push_back ({ "Drone de alquitrán"_u, "Texturas", V { { "a_forma", 3 }, { "a_pos", 60 }, { "b_fuente", 2 }, { "b_material", 4 }, { "b_conservar", 0 }, { "morph", 50 }, { "morphmodo", 1 },
                                                            { "m1_src", msLfo1 }, { "m1_dst", mdMorph }, { "m1_amt", 50 }, { "lfo1_rate", 0.03f }, { "adsr_a", 2000 },
                                                            { "adsr_r", 4000 } } });
    p.push_back ({ "Metal frío grave"_u, "Pulsados", V { { "a_forma", 5 }, { "a_pos", 70 }, { "pares", 60 }, { "inclinacion", -2 }, { "f1_corte", 2600 }, { "pulsar", 40 } } });
    p.push_back ({ "Marea espectral", "Resíntesis"_u, V { { "a_fuente", 2 }, { "a_material", 2 }, { "a_lectura", 0.1f }, { "b_forma", 4 }, { "morph", 30 }, { "blurt", 45 },
                                                           { "espacio", 50 } } });
    return p;
}

// ===================================================================== example for Play: whole-bar minor chords, low
std::vector<SeqNote> Processor::demoPattern() const
{
    static const int chords[4][4] { { 45, 52, 57, 60 }, { 41, 48, 53, 57 }, { 43, 48, 52, 55 }, { 40, 47, 52, 55 } };
    std::vector<SeqNote> out;
    for (int b = 0; b < 4; ++b) for (int k = 0; k < 4; ++k) out.push_back ({ b * 4.0, 3.9, chords[b][k], 90, 1 });
    return out;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

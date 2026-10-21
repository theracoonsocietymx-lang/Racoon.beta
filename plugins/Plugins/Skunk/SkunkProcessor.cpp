#include "SkunkProcessor.h"
#include "SkunkEditor.h"

namespace racoon::skunk
{
using namespace juce;

// ===================================================================== matrix tables
const StringArray& sourceNames()
{
    static StringArray a = []
    {
        StringArray s { "Nada" };
        for (int i = 0; i < numLfos; ++i) s.add ("LFO " + String (i + 1));
        for (int i = 0; i < numEnvs; ++i) s.add ("Envolvente " + String (i + 1));
        for (int i = 0; i < numSeqs; ++i) s.add ("Secuencia " + String (i + 1));
        for (int i = 0; i < numMacros; ++i) s.add ("Macro " + String (i + 1));
        s.addArray (StringArray { "Pad X", "Pad Y", "Velocidad"_u, "Nota", "Volumen de entrada", "Azar por 1/16" });
        return s;
    }();
    return a;
}

const std::vector<Dest>& destinations()
{
    static const std::vector<Dest> d {
        { "gsize", "Granos: tamaño" }, { "gdens", "Granos: densidad" }, { "gpos", "Granos: posición" }, { "gspray", "Granos: dispersión" },
        { "gpitch", "Granos: afinación" }, { "grev", "Granos: al revés" }, { "gjitter", "Granos: jitter" }, { "gspread", "Granos: estéreo" },
        { "gscan", "Granos: velocidad de lectura" }, { "gmix", "Granos: mezcla" },
        { "sfreezemix", "Espectro: congelado" }, { "sblur", "Espectro: difuminar" }, { "sshift", "Espectro: correr" }, { "sstretch", "Espectro: estirar" },
        { "sphase", "Espectro: evolución de fase" }, { "stexture", "Espectro: textura" }, { "spitch", "Espectro: afinación" }, { "morph", "Morphing" },
        { "smix", "Espectro: mezcla" }, { "glprob", "Glitch: probabilidad" },
        { "ffreq", "Filtro: frecuencia" }, { "freso", "Filtro: resonancia" }, { "fvowel", "Filtro: vocal" }, { "fmix", "Filtro: mezcla" },
        { "width", "Espacio: ancho" }, { "mdepth", "Espacio: movimiento" },
        { "dist_drive", "Distorsión: drive" }, { "dist_mix", "Distorsión: mezcla" }, { "ch_mix", "Chorus: mezcla" }, { "ph_rate", "Phaser: velocidad" },
        { "ph_mix", "Phaser: mezcla" }, { "dl_fb", "Delay: repeticiones" }, { "dl_mix", "Delay: mezcla" }, { "rv_size", "Reverb: tamaño" },
        { "rv_mix", "Reverb: mezcla" }, { "nlevel", "Ruido: volumen" }, { "drywet", "Mezcla general" }, { "outgain", "Salida" },
        { "lfo1_rate", "LFO 1: velocidad" }, { "lfo2_rate", "LFO 2: velocidad" } };
    return d;
}

int destIndex (const char* id)
{
    const auto& d = destinations();
    for (size_t i = 0; i < d.size(); ++i) if (std::strcmp (d[i].id, id) == 0) return (int) i + 1;
    return 0;
}

const StringArray& destinationNames()
{
    static StringArray a = [] { StringArray s { "Nada" }; for (auto& d : destinations()) s.add (String::fromUTF8 (d.name)); return s; }();
    return a;
}

namespace
{
const StringArray& syncNames() { static StringArray a { "Libre", "1/16", "1/8", "1/4", "1/2", "1 compás"_u, "2 compases", "4 compases" }; return a; }
double syncBeats (int i) { static const double b[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return b[jlimit (0, 7, i)]; }
const StringArray& divNames() { static StringArray a { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/2", "1 compás"_u }; return a; }
double divBeats (int i) { static const double b[] { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 2.0, 4.0 }; return b[jlimit (0, 7, i)]; }
const StringArray& fxNames() { static StringArray a { "Nada", "Distorsión"_u, "Chorus", "Phaser", "Delay", "Reverb" }; return a; }
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    // capture
    P::flt (l, "caplen", "Captura: largo", P::range (0.1f, 120.0f, 8.0f), 8.0f, "s", 1);
    P::db (l, "ingain", "Captura: entrada", -24.0f, 24.0f, 0.0f);
    P::db (l, "trig", "Captura: umbral de disparo", -60.0f, 0.0f, -30.0f);
    P::choice (l, "recmode", "Captura: modo", { "En vivo", "Un disparo", "Captura continua", "Búfer congelado"_u }, 0);
    P::toggle (l, "freeze", "Congelar el búfer"_u, false);
    P::toggle (l, "loop", "Lectura en bucle", true);
    // granular
    P::ms (l, "gsize", "Granos: tamaño"_u, 1.0f, 1000.0f, 80.0f, 80.0f);
    P::flt (l, "gdens", "Granos: densidad"_u, P::range (1.0f, 200.0f, 20.0f), 20.0f, "/s", 1);
    P::pct (l, "gpos", "Granos: posición"_u, 0.0f, 100.0f, 20.0f);
    P::ms (l, "gspray", "Granos: dispersión"_u, 0.0f, 500.0f, 20.0f, 50.0f);
    P::flt (l, "gpitch", "Granos: afinación"_u, P::range (-48.0f, 48.0f), 0.0f, "st", 1);
    P::flt (l, "gfine", "Granos: ajuste fino"_u, P::range (-100.0f, 100.0f), 0.0f, "cents", 0);
    P::flt (l, "grandp", "Granos: afinación al azar"_u, P::range (0.0f, 24.0f), 0.0f, "st", 1);
    P::pct (l, "grandprob", "Granos: probabilidad de afinación al azar"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "grev", "Granos: al revés"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "gjitter", "Granos: jitter", 0.0f, 100.0f, 10.0f);
    P::pct (l, "gspread", "Granos: estéreo"_u, 0.0f, 100.0f, 50.0f);
    P::choice (l, "gwin", "Granos: ventana", { "Hann", "Gauss", "Tukey", "Recta suave" }, 0);
    P::integer (l, "gmax", "Granos: máximo"_u, 16, 256, 128);
    P::pct (l, "gmix", "Granos: mezcla", 0.0f, 100.0f, 100.0f);
    P::flt (l, "gscan", "Granos: velocidad de lectura"_u, P::range (-2.0f, 2.0f), 0.0f, "x", 2);
    P::pct (l, "gskip", "Granos: silencios", 0.0f, 100.0f, 0.0f);
    P::pct (l, "gjump", "Granos: saltos de posición"_u, 0.0f, 100.0f, 0.0f);
    P::choice (l, "profile", "Perfil", { "Percusivo", "Melódico"_u, "Experimental" }, 1);
    P::toggle (l, "gmidi", "Granos con el teclado", false);
    P::choice (l, "lib", "Biblioteca", libraryNames(), 0);
    // spectral
    P::toggle (l, "spon", "Espectro: encendido", false);
    P::choice (l, "fftsize", "Espectro: tamaño de FFT"_u, { "512", "1024", "2048", "4096", "8192" }, 2);
    P::toggle (l, "sfreeze", "Espectro: congelar", false);
    P::pct (l, "sfreezemix", "Espectro: congelado", 0.0f, 100.0f, 100.0f);
    P::pct (l, "sdecay", "Espectro: caída del congelado"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "sblur", "Espectro: difuminar", 0.0f, 99.0f, 0.0f);
    P::pct (l, "shold", "Espectro: retener armónicos"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "sphase", "Espectro: evolución de fase"_u, 0.0f, 200.0f, 100.0f);
    P::pct (l, "stexture", "Espectro: textura", 0.0f, 100.0f, 0.0f);
    P::flt (l, "sshift", "Espectro: correr", P::range (-2000.0f, 2000.0f), 0.0f, "Hz", 0);
    P::flt (l, "sstretch", "Espectro: estirar", P::range (0.25f, 4.0f, 1.0f), 1.0f, "x", 2);
    P::pct (l, "sdisp", "Espectro: dispersión de fase"_u, 0.0f, 100.0f, 0.0f);
    P::hz (l, "slow", "Espectro: desde", 20.0f, 20000.0f, 20.0f);
    P::hz (l, "shigh", "Espectro: hasta", 20.0f, 20000.0f, 20000.0f);
    P::flt (l, "spitch", "Espectro: afinación"_u, P::range (-48.0f, 48.0f), 0.0f, "st", 1);
    P::toggle (l, "sformant", "Espectro: conservar formantes", false);
    P::choice (l, "morphmode", "Morphing: modo", { "Apagado", "Fundido espectral", "Transferir envolvente", "Mezcla armónica"_u, "Transferir ruido" }, 0);
    P::pct (l, "morph", "Morphing: cantidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "smix", "Espectro: mezcla", 0.0f, 100.0f, 100.0f);
    // glitch
    P::pct (l, "glprob", "Glitch: probabilidad", 0.0f, 100.0f, 0.0f);
    P::choice (l, "gldiv", "Glitch: rejilla", divNames(), 2);
    P::integer (l, "gldur", "Glitch: duración (divisiones)"_u, 1, 16, 2);
    P::pct (l, "glrep", "Glitch: repetir", 0.0f, 100.0f, 100.0f);
    P::pct (l, "glstut", "Glitch: stutter", 0.0f, 100.0f, 50.0f);
    P::pct (l, "glrev", "Glitch: al revés"_u, 0.0f, 100.0f, 30.0f);
    P::pct (l, "gltape", "Glitch: tape stop", 0.0f, 100.0f, 20.0f);
    P::pct (l, "glshuf", "Glitch: barajar", 0.0f, 100.0f, 30.0f);
    P::pct (l, "gldrop", "Glitch: cortes digitales", 0.0f, 100.0f, 10.0f);
    P::ms (l, "gltapems", "Glitch: tiempo del tape stop", 50.0f, 2000.0f, 400.0f, 400.0f);
    P::ms (l, "glsmooth", "Glitch: suavizado", 0.5f, 30.0f, 4.0f, 0.0f);
    // transients
    P::choice (l, "trmode", "Transitorios: modo", { "Apagado", "Conservar", "Quitar", "Resaltar", "Disparar granos" }, 0);
    P::pct (l, "trsens", "Transitorios: sensibilidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "tramount", "Transitorios: cantidad", 0.0f, 100.0f, 70.0f);
    P::ms (l, "trhold", "Transitorios: duración"_u, 2.0f, 200.0f, 30.0f, 30.0f);
    // noise
    P::choice (l, "ntype", "Ruido: tipo", { "Blanco", "Rosa", "Café"_u, "Impulsos", "Modulado" }, 1);
    P::db (l, "nlevel", "Ruido: volumen"_u, -70.0f, 0.0f, -70.0f);
    P::pct (l, "ncolor", "Ruido: color", 0.0f, 100.0f, 70.0f);
    P::choice (l, "ndest", "Ruido: a dónde va"_u, { "Al búfer"_u, "A la salida" }, 0);
    // filter bank
    P::choice (l, "ftype", "Filtro: tipo", { "Paso bajo", "Paso banda", "Paso alto", "Rechazo", "Formantes", "Peine", "Resonadores" }, 0);
    P::hz (l, "ffreq", "Filtro: frecuencia", 20.0f, 18000.0f, 18000.0f);
    P::pct (l, "freso", "Filtro: resonancia", 0.0f, 100.0f, 20.0f);
    P::flt (l, "fvowel", "Filtro: vocal (A E I O U)", P::range (0.0f, 4.0f), 0.0f, "", 2);
    P::toggle (l, "fkey", "Filtro: sigue al teclado", false);
    P::pct (l, "fmix", "Filtro: mezcla", 0.0f, 100.0f, 0.0f);
    // space
    P::pct (l, "width", "Espacio: ancho", 0.0f, 200.0f, 100.0f);
    P::flt (l, "move", "Espacio: velocidad de movimiento", P::range (0.01f, 5.0f, 0.5f), 0.2f, "Hz", 2);
    P::pct (l, "mdepth", "Espacio: movimiento", 0.0f, 100.0f, 0.0f);
    P::ms (l, "disp", "Espacio: dispersión"_u, 0.0f, 20.0f, 0.0f, 0.0f);
    P::pct (l, "decor", "Espacio: decorrelación"_u, 0.0f, 100.0f, 0.0f);
    // fx rack
    for (int i = 0; i < 5; ++i) P::choice (l, "fx" + String (i + 1), "Efectos: lugar " + String (i + 1), fxNames(), i + 1);
    P::pct (l, "dist_drive", "Distorsión: drive"_u, 0.0f, 100.0f, 30.0f);
    P::pct (l, "dist_tone", "Distorsión: tono"_u, 0.0f, 100.0f, 60.0f);
    P::pct (l, "dist_mix", "Distorsión: mezcla"_u, 0.0f, 100.0f, 0.0f);
    P::pct (l, "ch_rate", "Chorus: velocidad"_u, 0.0f, 100.0f, 20.0f);
    P::pct (l, "ch_depth", "Chorus: profundidad", 0.0f, 100.0f, 40.0f);
    P::pct (l, "ch_mix", "Chorus: mezcla", 0.0f, 100.0f, 0.0f);
    P::pct (l, "ph_rate", "Phaser: velocidad"_u, 0.0f, 100.0f, 15.0f);
    P::pct (l, "ph_fb", "Phaser: realimentación"_u, 0.0f, 100.0f, 50.0f);
    P::pct (l, "ph_mix", "Phaser: mezcla", 0.0f, 100.0f, 0.0f);
    P::choice (l, "dl_div", "Delay: tiempo", divNames(), 4);
    P::pct (l, "dl_fb", "Delay: repeticiones", 0.0f, 100.0f, 40.0f);
    P::pct (l, "dl_mix", "Delay: mezcla", 0.0f, 100.0f, 0.0f);
    P::pct (l, "rv_size", "Reverb: tamaño"_u, 0.0f, 100.0f, 60.0f);
    P::pct (l, "rv_damp", "Reverb: amortiguar", 0.0f, 100.0f, 40.0f);
    P::pct (l, "rv_mix", "Reverb: mezcla", 0.0f, 100.0f, 0.0f);
    // probability, XY, macros
    P::toggle (l, "seedlock", "Fijar la semilla", false);
    P::integer (l, "seed", "Semilla", 1, 9999, 1);
    P::pct (l, "xy_x", "Pad X", 0.0f, 100.0f, 50.0f);
    P::pct (l, "xy_y", "Pad Y", 0.0f, 100.0f, 50.0f);
    for (int i = 0; i < numMacros; ++i) P::pct (l, "macro" + String (i + 1), "Macro " + String (i + 1), 0.0f, 100.0f, 0.0f);
    // modulators
    for (int i = 0; i < numLfos; ++i)
    {
        const String n = "LFO " + String (i + 1) + ": ";
        P::flt (l, lfoId (i, "rate"), n + "velocidad"_u, P::range (0.01f, 20.0f, 1.0f), 0.2f + 0.1f * i, "Hz", 2);
        P::choice (l, lfoId (i, "shape"), n + "forma", lfoShapeNames(), i % 6);
        P::choice (l, lfoId (i, "sync"), n + "al tempo", syncNames(), 0);
    }
    for (int i = 0; i < numEnvs; ++i)
    {
        const String n = "Envolvente " + String (i + 1) + ": ";
        P::ms (l, envId (i, "att"), n + "ataque", 0.5f, 5000.0f, 10.0f, 100.0f);
        P::ms (l, envId (i, "dec"), n + "caída"_u, 1.0f, 5000.0f, 300.0f, 300.0f);
        P::pct (l, envId (i, "sus"), n + "sostenido", 0.0f, 100.0f, 50.0f);
        P::ms (l, envId (i, "rel"), n + "relajación"_u, 1.0f, 10000.0f, 500.0f, 500.0f);
        P::choice (l, envId (i, "trig"), n + "disparo", { "Teclado", "Transitorio", "Los dos" }, 0);
    }
    for (int i = 0; i < numSeqs; ++i)
    {
        const String n = "Secuencia " + String (i + 1) + ": ";
        for (int s = 0; s < seqSteps; ++s)
            P::flt (l, seqStepId (i, s), n + "paso " + String (s + 1), P::range (-100.0f, 100.0f), (float) std::round (std::sin ((s + i * 3) * 0.9) * 60.0), "%", 0);
        P::choice (l, seqId (i, "div"), n + "rejilla", divNames(), 2);
        P::integer (l, seqId (i, "len"), n + "pasos", 1, seqSteps, 16);
        P::pct (l, seqId (i, "glide"), n + "deslizar", 0.0f, 100.0f, 0.0f);
    }
    for (int r = 0; r < numRoutes; ++r)
    {
        const String n = "Ruta " + String (r + 1) + ": ";
        P::choice (l, routeId (r, "src"), n + "fuente", sourceNames(), 0);
        P::choice (l, routeId (r, "dst"), n + "destino", destinationNames(), 0);
        P::flt (l, routeId (r, "depth"), n + "cantidad", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::flt (l, routeId (r, "curve"), n + "curva", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
        P::ms (l, routeId (r, "smooth"), n + "suavizado", 0.0f, 500.0f, 0.0f, 50.0f);
    }
    // resample and output
    P::toggle (l, "rsrec", "Grabar la salida", false);
    P::pct (l, "drywet", "Mezcla general", 0.0f, 100.0f, 100.0f);
    P::db (l, "outgain", "Salida", -36.0f, 12.0f, 0.0f);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", AudioChannelSet::stereo(), true)
                                        .withInput ("Sidechain", AudioChannelSet::stereo(), false),
                       PluginInfo { "Skunk", "Laboratorio de diseño sonoro"_u, Colour (0xffe0789a), PluginKind::effect, 1, 4.0, true, false },
                       layout())
{
    wantsMidiAsEffect = true;
    startTimer (250);
    apvts.addParameterListener ("lib", this);
    destinationNames();
    for (size_t d = 0; d < destinations().size(); ++d)
        destParam[d + 1] = dynamic_cast<RangedAudioParameter*> (apvts.getParameter (destinations()[d].id));
    for (int r = 0; r < numRoutes; ++r)
    {
        routeSrc[r] = param (routeId (r, "src")); routeDst[r] = param (routeId (r, "dst")); routeDepth[r] = param (routeId (r, "depth"));
        routeCurve[r] = param (routeId (r, "curve")); routeSmooth[r] = param (routeId (r, "smooth"));
    }
}

void Processor::prepareDsp (double rate, int)
{
    // a frozen material (a file, the library, a capture you kept) survives a new sample rate: it is read back and
    // resampled into the new buffer instead of being lost
    AudioBuffer<float> kept;
    const int valid = captureValid.load();
    if (valid > 0 && captureState.load() == 3 && capture.size > 16 && preparedRate > 0.0)
    {
        kept.setSize (2, valid);
        for (int i = 0; i < valid; ++i)
            for (int c = 0; c < 2; ++c) kept.setSample (c, i, capture.back (c, (double) (valid - i)));
    }
    const double oldRate = preparedRate;
    preparedRate = rate;
    capture.prepare ((int) (rate * 120.0) + 16);
    if (kept.getNumSamples() > 0)
    {
        const double ratio = oldRate / rate;
        const int len = jmin (capture.size - 1, (int) (kept.getNumSamples() / ratio));
        for (int i = 0; i < len; ++i)
        {
            const double p = i * ratio;
            const int k = jmin ((int) p, kept.getNumSamples() - 2);
            const float f = (float) (p - k);
            capture.push (kept.getSample (0, k) + (kept.getSample (0, k + 1) - kept.getSample (0, k)) * f,
                          kept.getSample (1, k) + (kept.getSample (1, k + 1) - kept.getSample (1, k)) * f);
        }
        captureValid.store (len);
        captureState.store (3);
    }
    dryDelay.prepare ((1 << Spectral::maxOrder) + 64);
    finalDry.prepare ((1 << Spectral::maxOrder) + 64);
    resample.setSize (2, (int) (rate * 60.0));
    resample.clear();
    spaceDelay.prepare ((int) (rate * 0.03) + 8);
    granular.prepare (rate);
    lastFftOrder = 9 + (int) value ("fftsize");
    spectral.prepare (rate, lastFftOrder);
    glitch.prepare (rate);
    noise.prepare (rate);
    filter.prepare (rate);
    for (auto& f : fx) f.prepare (rate);
    setLatencySamples (value ("spon") > 0.5f ? spectral.latency() : 0);
    resetDsp();
}

void Processor::resetDsp()
{
    dryDelay.clear(); finalDry.clear(); spaceDelay.clear();
    // a frozen material stays (stopping, a new sample rate or the bypass don't throw away a file or the library)
    if (! (captureState.load() == 3 && captureValid.load() > 0))
    {
        capture.clear();
        captureValid.store (0);
        captureState.store (0);
    }
    granular.reset(); spectral.reset(); glitch.reset(); filter.reset();
    for (auto& f : fx) f.reset();
    for (auto& e : env) e = Env();
    for (auto& s : routeState) s = 0.0f;
    for (auto& m : modNorm) m = 0.0f;
    held = 0; trFast = trSlow = trEnv = follower = 0.0f; trRefractory = 0;
    oneShotLeft = 0; scanPos = 0.0; decor[0] = decor[1] = 0.0f;
    resampled.store (0);
    rnd.seed ((uint32) value ("seed"));
}

float Processor::modulated (int d) const
{
    auto* p = destParam[jlimit (0, 63, d)];
    if (p == nullptr) return 0.0f;
    return p->convertFrom0to1 (jlimit (0.0f, 1.0f, p->getValue() + modNorm[d]));
}

// ===================================================================== modulation (every 32 samples)
void Processor::updateModulation (int n, double ppq, bool playing)
{
    const double dt = n / sr;
    const double bpmNow = bpm();
    float src[64] {};
    int s = 1;
    // LFOs
    for (int i = 0; i < numLfos; ++i, ++s)
    {
        const int sync = (int) value (lfoId (i, "sync"));
        const double beats = syncBeats (sync);
        double ph;
        if (sync > 0 && playing) { const double c = ppq / beats; ph = c - std::floor (c); }
        else
        {
            // LFO 1 and 2 can have their speed modulated
            double hz = sync > 0 ? bpmNow / 60.0 / beats : (double) value (lfoId (i, "rate"));
            if (i < 2 && sync == 0) hz = modulated (destIndex (i == 0 ? "lfo1_rate" : "lfo2_rate"));
            ph = lfoPhase[i] + hz * dt;
            ph -= std::floor (ph);
        }
        if (ph < lfoPhase[i]) lfoHold[i] = rnd.bi();   // a new cycle: new random value
        lfoPhase[i] = ph;
        src[s] = lfoWave ((int) value (lfoId (i, "shape")), ph, lfoHold[i]);
    }
    // envelopes
    for (int i = 0; i < numEnvs; ++i, ++s)
    {
        auto& e = env[i];
        const int trig = (int) value (envId (i, "trig"));
        const bool keyGate = (trig == 0 || trig == 2) && held > 0;
        if ((trig == 1 || trig == 2) && trOnsetThisChunk) { e.stage = 1; e.autoRelease = (float) ((value (envId (i, "att")) + value (envId (i, "dec"))) * 0.001); }
        if (keyGate && ! e.gate) e.stage = 1;
        e.gate = keyGate;
        const float att = value (envId (i, "att")) * 0.001f, dec = value (envId (i, "dec")) * 0.001f, sus = value (envId (i, "sus")) * 0.01f, rel = value (envId (i, "rel")) * 0.001f;
        if (e.autoRelease >= 0.0f) { e.autoRelease -= (float) dt; if (e.autoRelease < 0.0f && ! e.gate) e.stage = 3; }
        else if (! e.gate && e.stage > 0 && e.stage < 3) e.stage = 3;
        switch (e.stage)
        {
            case 1: e.level += (float) dt / jmax (0.0005f, att); if (e.level >= 1.0f) { e.level = 1.0f; e.stage = 2; } break;
            case 2: e.level = sus + (e.level - sus) * std::exp (-(float) dt / jmax (0.001f, dec * 0.3f)); break;
            case 3: e.level *= std::exp (-(float) dt / jmax (0.001f, rel * 0.3f)); if (e.level < 1.0e-4f) { e.level = 0.0f; e.stage = 0; } break;
            default: break;
        }
        src[s] = e.level;
    }
    // step sequencers
    for (int i = 0; i < numSeqs; ++i, ++s)
    {
        const double b = divBeats ((int) value (seqId (i, "div")));
        const int len = jlimit (1, seqSteps, (int) value (seqId (i, "len")));
        const int64 k = (int64) std::floor (ppq / b);
        const int step = (int) (((k % len) + len) % len);
        const float target = value (seqStepId (i, step)) * 0.01f;
        const float glide = value (seqId (i, "glide")) * 0.01f;
        seqValue[i] = glide > 0.0f ? target + (seqValue[i] - target) * std::pow (glide, 0.5f) : target;
        src[s] = seqValue[i];
    }
    for (int i = 0; i < numMacros; ++i, ++s) src[s] = value ("macro" + String (i + 1)) * 0.01f;
    src[s++] = value ("xy_x") * 0.01f;
    src[s++] = value ("xy_y") * 0.01f;
    src[s++] = lastVel;
    src[s++] = jlimit (-1.0f, 1.0f, (lastNote - 60) / 48.0f);
    src[s++] = jlimit (0.0f, 1.0f, follower * 2.0f);
    {
        // a new random value on every 1/16
        const uint32 k = (uint32) (int64) std::floor (ppq / 0.25);
        uint32 h = k * 2654435761u ^ ((uint32) value ("seed") * 40503u);
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        src[s++] = (float) (h & 0xffffff) / 8388608.0f - 1.0f;
    }
    for (int i = 0; i < 64; ++i) sourceValue[i].store (src[i]);

    // routes
    for (auto& m : modNorm) m = 0.0f;
    const float chunkSec = (float) dt;
    for (int r = 0; r < numRoutes; ++r)
    {
        const int si = (int) routeSrc[r]->load (std::memory_order_relaxed), di = (int) routeDst[r]->load (std::memory_order_relaxed);
        if (si <= 0 || di <= 0) { modOut[r].store (0.0f); continue; }
        float v = src[jlimit (0, 63, si)];
        const float curve = routeCurve[r]->load() * 0.01f;
        if (curve != 0.0f) v = std::copysign (std::pow (std::abs (v), std::pow (4.0f, curve)), v);
        v *= routeDepth[r]->load() * 0.01f;
        const float sm = routeSmooth[r]->load() * 0.001f;
        routeState[r] = sm > 0.0f ? v + (routeState[r] - v) * std::exp (-chunkSec / sm) : v;
        modOut[r].store (routeState[r]);
        modNorm[jlimit (0, 63, di)] += routeState[r];
    }
}

// ===================================================================== audio
void Processor::processChunk (float* L, float* R, const float* scL, const float* scR, int n, double ppq, double bpb, bool playing)
{
    auto D = [] (const char* id) { return destIndex (id); };
    static const int dGsize = D ("gsize"), dGdens = D ("gdens"), dGpos = D ("gpos"), dGspray = D ("gspray"), dGpitch = D ("gpitch"), dGrev = D ("grev"),
                     dGjit = D ("gjitter"), dGspread = D ("gspread"), dGscan = D ("gscan"), dGmix = D ("gmix"), dSfm = D ("sfreezemix"), dSblur = D ("sblur"),
                     dSshift = D ("sshift"), dSstretch = D ("sstretch"), dSphase = D ("sphase"), dStex = D ("stexture"), dSpitch = D ("spitch"),
                     dMorph = D ("morph"), dSmix = D ("smix"), dGlprob = D ("glprob"), dFfreq = D ("ffreq"), dFreso = D ("freso"), dFvowel = D ("fvowel"),
                     dFmix = D ("fmix"), dWidth = D ("width"), dMdepth = D ("mdepth"), dDrive = D ("dist_drive"), dDmix = D ("dist_mix"), dChmix = D ("ch_mix"),
                     dPhrate = D ("ph_rate"), dPhmix = D ("ph_mix"), dDlfb = D ("dl_fb"), dDlmix = D ("dl_mix"), dRvsize = D ("rv_size"), dRvmix = D ("rv_mix"),
                     dNlevel = D ("nlevel"), dDrywet = D ("drywet"), dOut = D ("outgain");

    const int profile = (int) value ("profile");
    const float inGain = Decibels::decibelsToGain (value ("ingain"));
    const int recMode = (int) value ("recmode");
    const bool manualFreeze = value ("freeze") > 0.5f;
    const int capN = jlimit (16, capture.size - 1, (int) (value ("caplen") * sr));
    const float trigLevel = Decibels::decibelsToGain (value ("trig"));
    const int trMode = (int) value ("trmode");
    const float trSens = value ("trsens") * 0.01f, trAmount = value ("tramount") * 0.01f;
    const float trHold = std::exp (-1.0f / (value ("trhold") * 0.001f * (float) sr));
    const int nType = (int) value ("ntype");
    const float nLevel = Decibels::decibelsToGain (modulated (dNlevel), -69.5f), nColor = value ("ncolor") * 0.01f;
    const bool noiseToBuffer = value ("ndest") < 0.5f;
    const float gmix = modulated (dGmix) * 0.01f;
    const bool gmidi = value ("gmidi") > 0.5f;

    // the dry signal (for the final mix and transient keeping), delayed like the spectral path
    const bool spOn = value ("spon") > 0.5f;
    const int lat = spOn ? spectral.latency() : 0;
    const float dryWet = modulated (dDrywet) * 0.01f;
    const float outG = Decibels::decibelsToGain (modulated (dOut));

    float grainL[32] {}, grainR[32] {}, dryL[32], dryR[32], alignedL[32], alignedR[32];
    bool onset = false;
    for (int i = 0; i < n; ++i)
    {
        float l = L[i] * inGain, r = R[i] * inGain;
        dryL[i] = l; dryR[i] = r;
        // transients and the input follower
        const float a = 0.5f * (std::abs (l) + std::abs (r));
        trFast = a + (trFast - a) * 0.95f;
        trSlow = a + (trSlow - a) * 0.9995f;
        follower = a + (follower - a) * 0.999f;
        if (trRefractory > 0) --trRefractory;
        else if (trFast > 0.01f && trFast > trSlow * (3.5f - 2.5f * trSens)) { onset = true; trEnv = 1.0f; trRefractory = (int) (0.05 * sr); }
        trEnv *= trHold;
        if (nLevel > 0.0f && noiseToBuffer) { const float x = noise.next (nType, nColor, rnd) * nLevel; l += x; r += x; }
        // capture
        int st = captureState.load (std::memory_order_relaxed);
        bool write = ! manualFreeze;
        if (recMode == 3) write = false;
        if (recMode == 1)
        {
            if (st == 0 || st == 1)
            {
                st = 1;
                if (a > trigLevel || noteTrigger) { st = 2; oneShotLeft = capN; captureValid.store (0); noteTrigger = false; }
            }
            write = st == 2;
            if (st == 2 && --oneShotLeft <= 0) st = 3;
            captureState.store (st, std::memory_order_relaxed);
        }
        else captureState.store (write ? 0 : 3, std::memory_order_relaxed);
        if (write)
        {
            capture.push (l, r);
            const int v = captureValid.load (std::memory_order_relaxed);
            if (v < capN) captureValid.store (v + 1, std::memory_order_relaxed);
        }
        L[i] = l; R[i] = r;
        // the dry input, as late as the spectral path makes the wet one
        finalDry.push (dryL[i], dryR[i]);
        alignedL[i] = finalDry.back (0, lat + 1);
        alignedR[i] = finalDry.back (1, lat + 1);
    }
    trOnsetThisChunk = onset;
    transientLevel.store (trEnv);

    // granular
    GrainParams gp;
    gp.sizeMs = modulated (dGsize) * (profile == 0 ? 0.5 : profile == 1 ? 1.5 : 1.0);
    gp.density = modulated (dGdens);
    gp.position = modulated (dGpos) * 0.01;
    gp.sprayMs = modulated (dGspray);
    gp.pitch = modulated (dGpitch) + value ("gfine") * 0.01 + (gmidi && held > 0 ? lastNote - 60 : 0);
    gp.randPitch = value ("grandp");
    gp.randPitchProb = value ("grandprob") * 0.01;
    gp.reverseProb = modulated (dGrev) * 0.01;
    gp.jitter = modulated (dGjit) * 0.01 * (profile == 1 ? 0.5 : 1.0);
    gp.spread = modulated (dGspread) * 0.01;
    gp.skipProb = value ("gskip") * 0.01;
    gp.jumpProb = value ("gjump") * 0.01;
    gp.win = profile == 0 ? Window::tukey : (Window) jlimit (0, 3, (int) value ("gwin"));
    gp.maxGrains = (int) value ("gmax");
    const int state = captureState.load();
    gp.live = recMode == 0 && state == 0;
    const double scanRate = modulated (dGscan);
    if (! gp.live)
    {
        scanPos += scanRate * n / jmax (1, captureValid.load());
        if (value ("loop") > 0.5f) scanPos -= std::floor (scanPos);
        else scanPos = jlimit (0.0, 1.0, scanPos);
    }
    gp.scan = scanPos;
    const int valid = captureValid.load();
    const bool grainsOn = gmix > 0.0f && (! gmidi || held > 0);
    if (grainsOn)
    {
        granular.render (capture, valid, gp, grainL, grainR, n, rnd);
        if (onset && (trMode == 4 || profile == 0)) granular.burst (capture, valid, gp, trMode == 4 ? 1 + (int) (trAmount * 8.0f) : 2, rnd);
    }
    for (int i = 0; i < n; ++i)
    {
        L[i] = L[i] * (1.0f - gmix) + grainL[i] * gmix;
        R[i] = R[i] * (1.0f - gmix) + grainR[i] * gmix;
    }

    // spectral
    if (spOn)
    {
        SpectralParams sp;
        sp.freeze = value ("sfreeze") > 0.5f;
        sp.freezeMix = modulated (dSfm) * 0.01f;
        sp.decay = value ("sdecay") * 0.01f;
        sp.blur = modulated (dSblur) * 0.01f;
        sp.harmonicHold = value ("shold") * 0.01f;
        sp.phaseEvolution = modulated (dSphase) * 0.01f;
        sp.texture = modulated (dStex) * 0.01f;
        sp.shiftHz = modulated (dSshift);
        sp.stretch = modulated (dSstretch);
        sp.dispersion = value ("sdisp") * 0.01f;
        sp.lowHz = value ("slow");
        sp.highHz = value ("shigh");
        sp.pitch = modulated (dSpitch);
        sp.keepFormants = value ("sformant") > 0.5f;
        sp.morphMode = (int) value ("morphmode");
        sp.morph = modulated (dMorph) * 0.01f;
        const float smix = modulated (dSmix) * 0.01f;
        for (int i = 0; i < n; ++i)
        {
            float ol, orr;
            spectral.process (L[i], R[i], scL != nullptr ? scL[i] : 0.0f, scR != nullptr ? scR[i] : 0.0f, ol, orr, sp, rnd);
            // the unprocessed part waits the same latency
            dryDelay.push (L[i], R[i]);
            const float al = dryDelay.back (0, lat + 1), ar = dryDelay.back (1, lat + 1);
            L[i] = al * (1.0f - smix) + ol * smix;
            R[i] = ar * (1.0f - smix) + orr * smix;
        }
    }

    // filter bank
    const float fmix = modulated (dFmix) * 0.01f;
    if (fmix > 0.0f)
    {
        float freq = modulated (dFfreq);
        if (value ("fkey") > 0.5f && held > 0) freq *= std::pow (2.0f, (lastNote - 60) / 12.0f);
        const int ft = (int) value ("ftype");
        const float reso = modulated (dFreso) * 0.01f, vowel = modulated (dFvowel);
        for (int i = 0; i < n; ++i) filter.process (L[i], R[i], ft, freq, reso, vowel, fmix);
    }

    // glitch
    GlitchParams glp;
    glp.probability = modulated (dGlprob) * 0.01f;
    glp.divisionBeats = divBeats ((int) value ("gldiv"));
    glp.bpm = bpm();
    glp.durationDivs = (int) value ("gldur");
    glp.wRepeat = value ("glrep"); glp.wStutter = value ("glstut"); glp.wReverse = value ("glrev");
    glp.wTapeStop = value ("gltape"); glp.wShuffle = value ("glshuf"); glp.wDropout = value ("gldrop");
    glp.tapeStopMs = value ("gltapems");
    glp.smoothMs = value ("glsmooth");
    if (glp.probability > 0.0f || glitch.uiType.load() >= 0)
    {
        const double bps = bpm() / (60.0 * sr);
        for (int i = 0; i < n; ++i) glitch.process (L[i], R[i], ppq + i * bps, true, glp, rnd);
    }

    // space
    {
        const float width = modulated (dWidth) * 0.01f, mdepth = modulated (dMdepth) * 0.01f, decorAmt = value ("decor") * 0.01f;
        const double moveInc = value ("move") / sr;
        const float dispS = value ("disp") * 0.001f * (float) sr;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * width;
            l = m + sd; r = m - sd;
            if (mdepth > 0.0f)
            {
                movePhase += moveInc; if (movePhase >= 1.0) movePhase -= 1.0;
                const float pan = mdepth * (float) std::sin (MathConstants<double>::twoPi * movePhase);
                l *= jmin (1.0f, 1.0f - pan); r *= jmin (1.0f, 1.0f + pan);
            }
            if (dispS > 0.5f || decorAmt > 0.0f)
            {
                spaceDelay.push (l, r);
                // left a little late, right later: the image smears
                l = spaceDelay.back (0, 1.0 + dispS * 0.37);
                r = spaceDelay.back (1, 1.0 + dispS);
                if (decorAmt > 0.0f)
                {
                    const float c = 0.6f * decorAmt;
                    const float y = c * r + decor[0] - c * decor[1];
                    decor[0] = r; decor[1] = y;
                    r = r * (1.0f - decorAmt * 0.5f) + y * decorAmt * 0.5f;
                }
            }
            L[i] = l; R[i] = r;
        }
    }

    // fx rack, in the order of its five places
    {
        const float drive = modulated (dDrive) * 0.01f, dmix = modulated (dDmix) * 0.01f, chmix = modulated (dChmix) * 0.01f;
        const float phrate = modulated (dPhrate) * 0.01f, phmix = modulated (dPhmix) * 0.01f, dlfb = modulated (dDlfb) * 0.01f;
        const float dlmix = modulated (dDlmix) * 0.01f, rvsize = modulated (dRvsize) * 0.01f, rvmix = modulated (dRvmix) * 0.01f;
        for (int slot = 0; slot < 5; ++slot)
        {
            const int kind = (int) value ("fx" + String (slot + 1));
            switch (kind)
            {
                case 1: fx[slot].process (1, L, R, n, drive, value ("dist_tone") * 0.01f, dmix, bpm(), 0.0); break;
                case 2: fx[slot].process (2, L, R, n, value ("ch_rate") * 0.01f, value ("ch_depth") * 0.01f, chmix, bpm(), 0.0); break;
                case 3: fx[slot].process (3, L, R, n, phrate, value ("ph_fb") * 0.01f, phmix, bpm(), 0.0); break;
                case 4: fx[slot].process (4, L, R, n, dlfb, 0.0f, dlmix, bpm(), divBeats ((int) value ("dl_div"))); break;
                case 5: fx[slot].process (5, L, R, n, rvsize, value ("rv_damp") * 0.01f, rvmix, bpm(), 0.0); break;
                default: break;
            }
        }
    }

    // transients: keep, remove or emphasise the attacks
    if (trMode >= 1 && trMode <= 3)
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = trEnv * trAmount;
            if (trMode == 1) { L[i] = L[i] * (1.0f - t) + alignedL[i] * t; R[i] = R[i] * (1.0f - t) + alignedR[i] * t; }
            else if (trMode == 2) { L[i] *= 1.0f - t; R[i] *= 1.0f - t; }
            else { L[i] *= 1.0f + t * 2.0f; R[i] *= 1.0f + t * 2.0f; }
        }
    }

    // noise to the output, the final mix, resampling
    const bool rec = value ("rsrec") > 0.5f;
    for (int i = 0; i < n; ++i)
    {
        if (nLevel > 0.0f && ! noiseToBuffer) { const float x = noise.next (nType, nColor, rnd) * nLevel; L[i] += x; R[i] += x; }
        const float dl = alignedL[i], dr = alignedR[i];
        L[i] = (dl * (1.0f - dryWet) + L[i] * dryWet) * outG;
        R[i] = (dr * (1.0f - dryWet) + R[i] * dryWet) * outG;
        if (rec)
        {
            const int at = resampled.load (std::memory_order_relaxed);
            if (at < resample.getNumSamples()) { resample.setSample (0, at, L[i]); resample.setSample (1, at, R[i]); resampled.store (at + 1, std::memory_order_relaxed); }
        }
    }
    juce::ignoreUnused (bpb, playing);
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    // FFT size changes reset the spectral engine (and the latency)
    const int ord = 9 + (int) value ("fftsize");
    if (ord != lastFftOrder) { lastFftOrder = ord; spectral.setOrder (ord); }
    const int wantLat = value ("spon") > 0.5f ? spectral.latency() : 0;
    if (wantLat != getLatencySamples()) setLatencySamples (wantLat);

    // actions from the window
    if (toCapture.exchange (false))
    {
        const int len = jmin (resampled.load(), capture.size - 1);
        capture.clear();
        for (int i = 0; i < len; ++i) capture.push (resample.getSample (0, i), resample.getSample (1, i));
        captureValid.store (len);
    }
    if (fileReady.load())
    {
        const SpinLock::ScopedTryLockType sl (loadedLock);
        if (sl.isLocked())
        {
            fileReady.store (false);
            const int len = jmin (loaded.getNumSamples(), capture.size - 1);
            capture.clear();
            for (int i = 0; i < len; ++i) capture.push (loaded.getSample (0, i), loaded.getSample (jmin (1, loaded.getNumChannels() - 1), i));
            captureValid.store (len);
            captureState.store (3);
        }
    }
    if (recapture.exchange (false)) { captureValid.store (0); captureState.store (0); oneShotLeft = 0; capture.clear(); }
    if (value ("rsrec") > 0.5f && ! wasRecording) resampled.store (0);
    wasRecording = value ("rsrec") > 0.5f;

    // MIDI
    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        if (m.isNoteOn()) { ++held; lastNote = m.getNoteNumber(); lastVel = m.getFloatVelocity(); noteTrigger = true; }
        else if (m.isNoteOff()) held = jmax (0, held - 1);
    }

    // musical time
    double ppqNow = ppq();
    const bool playing = isPlaying();
    if (! playing) { ppqNow = runPpq; }
    double bpb = 4.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto ts = pos->getTimeSignature()) bpb = 4.0 * ts->numerator / jmax (1, ts->denominator);

    // seed lock: every bar starts from the same random numbers
    if (value ("seedlock") > 0.5f)
    {
        const int64 bar = (int64) std::floor (ppqNow / bpb);
        if (bar != lastBar) { lastBar = bar; rnd.seed ((uint32) value ("seed") * 7919u + (uint32) bar); }
    }

    auto main = getBusBuffer (buffer, false, 0);
    float* L = main.getWritePointer (0);
    float* R = main.getNumChannels() > 1 ? main.getWritePointer (1) : main.getWritePointer (0);
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getReadPointer (jmin (1, sc.getNumChannels() - 1)); }
    }
    const double bps = bpm() / (60.0 * sr);
    for (int start = 0; start < n; start += 32)
    {
        const int len = jmin (32, n - start);
        const double at = ppqNow + start * bps;
        updateModulation (len, at, playing);
        processChunk (L + start, R + start, scL != nullptr ? scL + start : nullptr, scR != nullptr ? scR + start : nullptr, len, at, bpb, playing);
    }
    runPpq = ppqNow + n * bps;
}

// ===================================================================== a file becomes the capture
bool Processor::loadFile (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const double ratio = r->sampleRate / sr;
    const int srcLen = (int) jmin ((int64) (120.0 * r->sampleRate), r->lengthInSamples);
    AudioBuffer<float> src ((int) jmax (1u, r->numChannels), srcLen);
    r->read (&src, 0, srcLen, 0, true, true);
    // to the session's sample rate (linear, good enough for raw material)
    const int outLen = jmax (1, (int) (srcLen / ratio));
    AudioBuffer<float> out (2, outLen);
    for (int c = 0; c < 2; ++c)
    {
        const float* x = src.getReadPointer (jmin (c, src.getNumChannels() - 1));
        for (int i = 0; i < outLen; ++i)
        {
            const double p = i * ratio;
            const int k = (int) p;
            const float fr = (float) (p - k);
            out.setSample (c, i, x[jmin (k, srcLen - 1)] + (x[jmin (k + 1, srcLen - 1)] - x[jmin (k, srcLen - 1)]) * fr);
        }
    }
    {
        const SpinLock::ScopedLockType sl (loadedLock);
        loaded = std::move (out);
    }
    fileReady.store (true);
    setRealValue ("recmode", 3.0f);   // the material stays put
    return true;
}

// ===================================================================== the library
const StringArray& Processor::libraryNames()
{
    static StringArray a { "Mi captura o mi archivo", "Pad de cristal", "Coro oscuro", "Cuerdas lejanas", "Viento afinado",
                           "Campanas suaves", "Órgano grave"_u, "Polvo de cinta", "Agua profunda" };
    return a;
}

void Processor::parameterChanged (const String&, float)
{
    if (MessageManager::getInstanceWithoutCreating() != nullptr && MessageManager::getInstance()->isThisTheMessageThread())
        timerCallback();
}

void Processor::timerCallback()
{
    const int lib = (int) value ("lib");
    if (lib != lastLibrary)
    {
        lastLibrary = lib;
        if (lib > 0) loadLibrary (lib);
    }
}

bool Processor::loadLibrary (int index)
{
    if (index <= 0 || index >= libraryNames().size()) return false;
    const double rate = sr > 0.0 ? sr : 48000.0;
    const int len = (int) (rate * 6.0);
    AudioBuffer<float> out (2, len);
    out.clear();
    const float twoPi = MathConstants<float>::twoPi;
    const double c4 = 261.6256;
    uint32 seed = 0x5c0ffee ^ (uint32) index;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; };
    // only C and G (and their octaves) sound in every material, so a note played by MIDI stays in its key
    for (int ch = 0; ch < 2; ++ch)
    {
        float* y = out.getWritePointer (ch);
        double ph[8] {};
        float lp1 = 0.0f, lp2 = 0.0f, bp[4][2] {};
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) (i / rate);
            const float fadeIn = jmin (1.0f, t / 0.4f), fadeOut = jmin (1.0f, (6.0f - t) / 0.4f);
            float x = 0.0f;
            switch (index)
            {
                case 1:   // glass pad: detuned saws an octave apart, softened
                {
                    const double det[4] { 1.0, 1.004 + 0.001 * ch, 0.996, 2.0 };
                    for (int k = 0; k < 4; ++k) { ph[k] += c4 * det[k] / rate; ph[k] -= std::floor (ph[k]); x += (float) (2.0 * ph[k] - 1.0) * (k == 3 ? 0.4f : 0.6f); }
                    const float cut = 0.06f + 0.03f * std::sin (twoPi * 0.2f * t);
                    lp1 += cut * (x - lp1); lp2 += cut * (lp1 - lp2); x = lp2 * 0.7f;
                    break;
                }
                case 2:   // dark choir: a sung "o", with vibrato
                {
                    const float vib = 1.0f + 0.006f * std::sin (twoPi * 5.0f * t + (float) ch);
                    ph[0] += c4 * 0.5 * vib / rate; ph[0] -= std::floor (ph[0]);
                    const float src = (float) (2.0 * ph[0] - 1.0) + rnd() * 0.03f;
                    const float fo[3] { 450.0f, 800.0f, 2600.0f }, g[3] { 1.0f, 0.5f, 0.15f };
                    for (int k = 0; k < 3; ++k)
                    {
                        const float w = twoPi * fo[k] / (float) rate, rr = std::exp (-MathConstants<float>::pi * 90.0f / (float) rate);
                        const float o = (1.0f - rr) * src + 2.0f * rr * std::cos (w) * bp[k][0] - rr * rr * bp[k][1];
                        bp[k][1] = bp[k][0]; bp[k][0] = o; x += o * g[k];
                    }
                    x *= 2.0f;
                    break;
                }
                case 3:   // distant strings: saws of C3 and G3 and C4, slow bow
                {
                    const double f[3] { c4 * 0.5, c4 * 0.75, c4 };
                    for (int k = 0; k < 3; ++k) { ph[k] += f[k] * (1.0 + 0.003 * std::sin (twoPi * (0.3f + 0.1f * k) * t)) / rate; ph[k] -= std::floor (ph[k]); x += (float) (2.0 * ph[k] - 1.0) * 0.4f; }
                    lp1 += 0.05f * (x - lp1); lp2 += 0.05f * (lp1 - lp2); x = lp2 * (0.6f + 0.4f * std::sin (twoPi * 0.15f * t));
                    break;
                }
                case 4:   // tuned wind: noise through narrow resonances on C and G
                {
                    const float n = rnd();
                    const float fr[3] { (float) c4, (float) (c4 * 1.5), (float) (c4 * 2.0) };
                    for (int k = 0; k < 3; ++k)
                    {
                        const float w = twoPi * fr[k] / (float) rate, rr = 0.9992f;
                        const float o = (1.0f - rr) * n + 2.0f * rr * std::cos (w) * bp[k][0] - rr * rr * bp[k][1];
                        bp[k][1] = bp[k][0]; bp[k][0] = o; x += o * (k == 0 ? 6.0f : 3.5f);
                    }
                    x *= 0.7f + 0.3f * std::sin (twoPi * 0.4f * t + (float) ch);
                    break;
                }
                case 5:   // soft bells: harmonic partials struck every 1.5 s
                {
                    const float tt = std::fmod (t, 1.5f);
                    const float ratios[4] { 1.0f, 2.0f, 3.0f, 4.0f }, amps[4] { 1.0f, 0.5f, 0.35f, 0.2f }, decs[4] { 1.4f, 0.9f, 0.6f, 0.4f };
                    for (int k = 0; k < 4; ++k) x += std::sin (twoPi * (float) c4 * ratios[k] * t) * amps[k] * std::exp (-tt / decs[k]);
                    x *= 0.4f;
                    break;
                }
                case 6:   // low organ: C2, C3, G3, C4 drawbars
                {
                    const float f[4] { (float) (c4 * 0.25), (float) (c4 * 0.5), (float) (c4 * 0.75), (float) c4 }, a[4] { 0.6f, 0.5f, 0.3f, 0.25f };
                    for (int k = 0; k < 4; ++k) x += std::sin (twoPi * f[k] * t * (1.0f + 0.0008f * ch)) * a[k];
                    x *= 0.5f * (1.0f + 0.08f * std::sin (twoPi * 6.0f * t));
                    break;
                }
                case 7:   // tape dust: a wobbly C under crackle and hiss
                {
                    const float wob = 1.0f + 0.004f * std::sin (twoPi * 0.7f * t);
                    x = std::sin (twoPi * (float) c4 * wob * t) * 0.35f + std::sin (twoPi * (float) c4 * 0.5f * wob * t) * 0.3f;
                    const float n = rnd();
                    lp1 += 0.2f * (n - lp1);
                    x += (n - lp1) * 0.05f + (std::abs (rnd()) > 0.9994f ? rnd() * 0.6f : 0.0f);
                    break;
                }
                default:  // deep water: slow sines on C2 and G2 that swell, with bubbles of C4
                {
                    x = std::sin (twoPi * (float) (c4 * 0.25) * t) * 0.5f * (0.6f + 0.4f * std::sin (twoPi * 0.25f * t))
                      + std::sin (twoPi * (float) (c4 * 0.375) * t) * 0.3f * (0.6f + 0.4f * std::sin (twoPi * 0.31f * t + 1.0f));
                    const float tt = std::fmod (t + 0.37f * ch, 0.73f);
                    x += std::sin (twoPi * (float) c4 * (1.0f + 0.4f * tt) * t) * 0.25f * std::exp (-tt / 0.08f);
                    break;
                }
            }
            y[i] = x * fadeIn * fadeOut;
        }
    }
    const float mag = out.getMagnitude (0, len);
    if (mag > 0.0f) out.applyGain (0.7f / mag);
    {
        const SpinLock::ScopedLockType sl (loadedLock);
        loaded = std::move (out);
    }
    fileReady.store (true);
    setRealValue ("recmode", 3.0f);   // the material stays put
    return true;
}

// ===================================================================== resample to a file
File Processor::saveResampleWav() const
{
    const int len = resampled.load();
    if (len <= 0) return {};
    auto dir = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Skunk");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile ("Skunk " + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav", false);
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (file), sr, 2, 24, {}, 0));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (resample, 0, len);
    return file;
}

// ===================================================================== state: the frozen capture travels with the project
void Processor::saveExtraState (ValueTree& v) const
{
    const int valid = captureValid.load();
    const bool frozen = captureState.load() == 3 || (int) value ("recmode") == 3 || value ("freeze") > 0.5f;
    if (! frozen || valid <= 0 || valid > (int) (sr * 30.0)) return;
    AudioBuffer<float> buf (2, valid);
    for (int i = 0; i < valid; ++i)
        for (int c = 0; c < 2; ++c) buf.setSample (c, i, capture.back (c, valid - i));
    MemoryBlock mb;
    {
        FlacAudioFormat flac;
        std::unique_ptr<AudioFormatWriter> w (flac.createWriterFor (new MemoryOutputStream (mb, false), sr, 2, 24, {}, 0));
        if (w == nullptr) return;
        w->writeFromAudioSampleBuffer (buf, 0, valid);
    }
    v.setProperty ("capture", mb.toBase64Encoding(), nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    restoreCapture (v);
    // a library material is made again from its name when the project did not keep the capture (a copy of the
    // plugin for a render, or a project saved before the material was made)
    const int lib = (int) value ("lib");
    lastLibrary = lib;
    if (lib > 0 && captureValid.load() == 0) loadLibrary (lib);
}

void Processor::restoreCapture (const ValueTree& v)
{
    MemoryBlock mb;
    if (! mb.fromBase64Encoding (v.getProperty ("capture").toString()) || mb.getSize() == 0) return;
    FlacAudioFormat flac;
    std::unique_ptr<AudioFormatReader> r (flac.createReaderFor (new MemoryInputStream (mb, false), true));
    if (r == nullptr || capture.size < 32) return;
    const int len = (int) jmin ((int64) capture.size - 1, r->lengthInSamples);
    AudioBuffer<float> buf (2, len);
    r->read (&buf, 0, len, 0, true, true);
    capture.clear();
    for (int i = 0; i < len; ++i) capture.push (buf.getSample (0, i), buf.getSample (1, i));
    captureValid.store (len);
    captureState.store (3);
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    using V = std::vector<std::pair<String, float>>;
    std::vector<FactoryPreset> p;
    auto route = [] (V& v, int r, int src, const char* dst, float depth, float smooth = 0.0f)
    {
        int d = 0;
        for (size_t i = 0; i < destinations().size(); ++i) if (String (destinations()[i].id) == dst) d = (int) i + 1;
        v.push_back ({ routeId (r, "src"), (float) src }); v.push_back ({ routeId (r, "dst"), (float) d });
        v.push_back ({ routeId (r, "depth"), depth }); v.push_back ({ routeId (r, "smooth"), smooth });
    };
    const int lfo1 = 1, lfo2 = 2, lfo3 = 3, env1 = 1 + numLfos, seq1 = 1 + numLfos + numEnvs, macro1 = 1 + numLfos + numEnvs + numSeqs;
    const int padX = macro1 + numMacros, padY = padX + 1, follow = padX + 4;
    p.push_back ({ "Inicial", "Inicio", {} });
    { V v { { "gsize", 220 }, { "gdens", 30 }, { "gspray", 120 }, { "gspread", 80 }, { "rv_mix", 40 }, { "rv_size", 85 }, { "fx5", 5 } };
      route (v, 0, lfo1, "gpos", 20, 50); route (v, 1, lfo2, "gpitch", 3); p.push_back ({ "Pad de nube", "Granular Pads", v }); }
    { V v { { "gsize", 400 }, { "gdens", 18 }, { "gpitch", 12 }, { "grandp", 7 }, { "grandprob", 30 }, { "gwin", 1 }, { "rv_mix", 50 } };
      route (v, 0, lfo1, "gspray", 40, 100); p.push_back ({ "Coro de cristal", "Granular Pads", v }); }
    { V v { { "dist_mix", 60 }, { "dist_drive", 70 }, { "ftype", 5 }, { "ffreq", 110 }, { "freso", 80 }, { "fmix", 60 }, { "nlevel", -24 }, { "ntype", 4 } };
      route (v, 0, seq1, "ffreq", 30); p.push_back ({ "Fábrica de metal"_u, "Industrial Textures", v }); }
    { V v { { "glprob", 50 }, { "gldiv", 2 }, { "glrep", 60 }, { "glstut", 60 }, { "dist_mix", 30 }, { "gmix", 0 } };
      p.push_back ({ "Remachadora", "Industrial Textures", v }); }
    { V v { { "spon", 1 }, { "spitch", -12 }, { "sformant", 0 }, { "ftype", 4 }, { "fmix", 70 }, { "gmix", 0 }, { "dist_mix", 40 }, { "dist_drive", 50 } };
      route (v, 0, lfo1, "fvowel", 100, 30); p.push_back ({ "Voz demolida", "Vocal Destruction", v }); }
    { V v { { "gsize", 30 }, { "gdens", 80 }, { "grev", 50 }, { "gjump", 20 }, { "glprob", 40 }, { "glrev", 80 } };
      p.push_back ({ "Sílabas rotas"_u, "Vocal Destruction", v }); }
    { V v { { "spon", 1 }, { "sfreeze", 1 }, { "stexture", 40 }, { "sblur", 60 }, { "gmix", 0 }, { "rv_mix", 60 }, { "rv_size", 95 }, { "ftype", 0 }, { "ffreq", 2500 }, { "fmix", 100 } };
      route (v, 0, lfo1, "ffreq", 25, 200); p.push_back ({ "Sótano congelado"_u, "Dark Atmospheres", v }); }
    { V v { { "gpitch", -12 }, { "gsize", 600 }, { "gdens", 12 }, { "rv_mix", 70 }, { "dl_mix", 30 } };
      p.push_back ({ "Respiración oscura"_u, "Dark Atmospheres", v }); }
    { V v { { "glprob", 70 }, { "gldiv", 3 }, { "gldur", 1 }, { "glstut", 100 }, { "gltape", 30 }, { "gmix", 0 }, { "seedlock", 1 } };
      p.push_back ({ "Tartamudeo a tempo", "Rhythmic Glitch", v }); }
    { V v { { "gsize", 60 }, { "gdens", 40 }, { "gskip", 30 }, { "gmix", 100 }, { "ph_mix", 40 } };
      route (v, 0, seq1, "gpitch", 24); route (v, 1, seq1 + 1, "gpos", 40); p.push_back ({ "Secuencia de granos", "Rhythmic Glitch", v }); }
    { V v { { "spon", 1 }, { "sfreeze", 1 }, { "sphase", 30 }, { "stexture", 20 }, { "gmix", 0 }, { "rv_mix", 80 }, { "rv_size", 100 } };
      route (v, 0, padX, "sstretch", 60, 100); route (v, 1, padY, "sshift", 30, 100); p.push_back ({ "Drone de catedral", "Cinematic Drones", v }); }
    { V v { { "gsize", 900 }, { "gdens", 8 }, { "gscan", 0.05f }, { "recmode", 3 }, { "rv_mix", 50 } };
      route (v, 0, lfo3, "gspread", 50, 200); p.push_back ({ "Marea lenta", "Cinematic Drones", v }); }
    { V v { { "profile", 0 }, { "trmode", 4 }, { "tramount", 80 }, { "gsize", 25 }, { "gpitch", 7 }, { "dist_mix", 30 } };
      route (v, 0, env1, "gpitch", 20); v.push_back ({ envId (0, "trig"), 1 }); p.push_back ({ "Golpe mutante", "Percussive Mutation", v }); }
    { V v { { "trmode", 1 }, { "tramount", 100 }, { "gsize", 120 }, { "gpitch", -5 }, { "ftype", 6 }, { "ffreq", 220 }, { "fmix", 50 } };
      route (v, 0, follow, "fmix", 50); p.push_back ({ "Bombo con cola", "Percussive Mutation", v }); }
    return p;
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.5, 0.4, 33, 100, 1 },
             { 1.5, 0.4, 33, 100, 1 },
             { 2.5, 0.4, 33, 100, 1 },
             { 3.5, 0.4, 33, 100, 1 },
             { 4.5, 0.4, 29, 100, 1 },
             { 5.5, 0.4, 29, 100, 1 },
             { 6.5, 0.4, 29, 100, 1 },
             { 7.5, 0.4, 29, 100, 1 },
             { 0.0, 1.5, 57, 84, 1 },
             { 0.0, 1.5, 60, 84, 1 },
             { 0.0, 1.5, 64, 84, 1 },
             { 4.0, 1.5, 53, 84, 1 },
             { 4.0, 1.5, 57, 84, 1 },
             { 4.0, 1.5, 60, 84, 1 },
             { 2.0, 1.5, 57, 84, 1 },
             { 2.0, 1.5, 60, 84, 1 },
             { 2.0, 1.5, 64, 84, 1 },
             { 6.0, 1.5, 53, 84, 1 },
             { 6.0, 1.5, 57, 84, 1 },
             { 6.0, 1.5, 60, 84, 1 } };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

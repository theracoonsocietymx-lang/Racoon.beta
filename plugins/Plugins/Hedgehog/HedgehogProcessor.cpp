#include "HedgehogProcessor.h"
#include "HedgehogEditor.h"

namespace racoon::hedgehog
{
using namespace juce;

StringArray platformNames() { return { "Spotify / YouTube (-14)", "Apple Music (-16)", "Club / DJ (-8)", "CD fuerte (-10)", "Podcast (-16)", "Cine (-24)" }; }
float platformLufs (int i) { static const float v[] { -14.0f, -16.0f, -8.0f, -10.0f, -16.0f, -24.0f }; return v[jlimit (0, 5, i)]; }
float platformCeiling (int i) { static const float v[] { -1.0f, -1.0f, -0.3f, -0.3f, -1.0f, -2.0f }; return v[jlimit (0, 5, i)]; }

static const char* eqKeys[] { "on", "type", "freq", "gain", "q", "dyn", "thr", "range", "ms" };
static const char* mbKeys[] { "on", "thr", "ratio", "att", "rel", "gain" };

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::db (l, "ingain", "Entrada", -24.0f, 24.0f, 0.0f);
    P::toggle (l, "pa_on", "Partícula"_u, true);
    P::pct (l, "pa_amount", "Cantidad", 0.0f, 200.0f, 100.0f);
    P::db (l, "pa_high", "Agudos", -6.0f, 6.0f, 0.0f);
    P::db (l, "pa_mid", "Medios", -6.0f, 6.0f, 0.0f);
    P::db (l, "pa_low", "Graves", -6.0f, 6.0f, 0.0f);
    P::toggle (l, "pa_dyn", "Compresión por bandas"_u, true);
    P::choice (l, "pa_sc", "Sidechain", { "Interno", "Externo" }, 0);
    P::choice (l, "target", "Destino", platformNames(), 0);
    P::choice (l, "genre", "Género"_u, genreNames(), 0);

    const float freqs[numEqBands] { 40, 80, 150, 300, 500, 800, 1200, 2000, 3500, 6000, 10000, 15000 };
    for (int b = 0; b < numEqBands; ++b)
    {
        const String n = "EQ " + String (b + 1) + ": ";
        P::toggle (l, eqId (b, "on"), n + "encendida", false);
        P::choice (l, eqId (b, "type"), n + "tipo", { "Campana", "Graves (shelf)", "Agudos (shelf)", "Corte de graves", "Corte de agudos" }, b == 0 ? 1 : b == numEqBands - 1 ? 2 : 0);
        P::hz (l, eqId (b, "freq"), n + "frecuencia", 20.0f, 20000.0f, freqs[b]);
        P::db (l, eqId (b, "gain"), n + "ganancia", -18.0f, 18.0f, 0.0f);
        P::flt (l, eqId (b, "q"), n + "Q", P::range (0.1f, 12.0f, 1.0f), 1.0f, "", 2);
        P::choice (l, eqId (b, "dyn"), n + "dinámica"_u, { "Fija", "Baja si se pasa", "Sube si falta" }, 0);
        P::db (l, eqId (b, "thr"), n + "umbral", -60.0f, 0.0f, -24.0f);
        P::db (l, eqId (b, "range"), n + "rango", 0.0f, 18.0f, 6.0f);
        P::choice (l, eqId (b, "ms"), n + "canal", { "Estéreo"_u, "Centro (mid)", "Lados (side)" }, 0);
    }
    P::ms (l, "eq_att", "EQ dinámico: ataque"_u, 0.5f, 200.0f, 10.0f);
    P::ms (l, "eq_rel", "EQ dinámico: soltar"_u, 10.0f, 1000.0f, 120.0f);

    P::toggle (l, "res_on", "Resonancias", false);
    P::pct (l, "res_depth", "Resonancias: cantidad", 0.0f, 100.0f, 50.0f);
    P::pct (l, "res_sharp", "Resonancias: finura", 0.0f, 100.0f, 50.0f);
    P::hz (l, "res_lo", "Resonancias: desde", 20.0f, 20000.0f, 200.0f);
    P::hz (l, "res_hi", "Resonancias: hasta", 20.0f, 20000.0f, 12000.0f);
    P::pct (l, "res_speed", "Resonancias: velocidad", 0.0f, 100.0f, 50.0f);

    P::toggle (l, "match_on", "Match EQ", false);
    P::pct (l, "match_amt", "Match EQ: cantidad", 0.0f, 100.0f, 50.0f);
    P::choice (l, "match_src", "Match EQ: igualar a", { "Curva del género"_u, "Referencia 1", "Referencia 2" }, 0);

    P::toggle (l, "mb_on", "Multibanda", false);
    const float xo[numMbBands - 1] { 80, 250, 1000, 4000, 10000 };
    for (int k = 0; k < numMbBands - 1; ++k) P::hz (l, "mb_x" + String (k + 1), "Multibanda: corte " + String (k + 1), 30.0f, 16000.0f, xo[k]);
    for (int b = 0; b < numMbBands; ++b)
    {
        const String n = "Banda " + String (b + 1) + ": ";
        P::toggle (l, mbId (b, "on"), n + "comprime", true);
        P::db (l, mbId (b, "thr"), n + "umbral", -60.0f, 0.0f, -18.0f);
        P::flt (l, mbId (b, "ratio"), n + "ratio", P::range (1.0f, 10.0f, 2.5f), 2.0f, ":1", 1);
        P::ms (l, mbId (b, "att"), n + "ataque", 0.1f, 200.0f, b < 2 ? 30.0f : 10.0f);
        P::ms (l, mbId (b, "rel"), n + "soltar", 10.0f, 1000.0f, b < 2 ? 200.0f : 120.0f);
        P::db (l, mbId (b, "gain"), n + "ganancia", -12.0f, 12.0f, 0.0f);
    }
    P::choice (l, "mb_solo", "Multibanda: escuchar sola", { "Todas", "Banda 1", "Banda 2", "Banda 3", "Banda 4", "Banda 5", "Banda 6" }, 0);

    P::toggle (l, "lo_on", "Graves", false);
    P::hz (l, "lo_freq", "Graves: hasta", 40.0f, 250.0f, 110.0f);
    P::pct (l, "lo_punch", "Graves: golpe", 0.0f, 100.0f, 0.0f);
    P::pct (l, "lo_tight", "Graves: firmeza", 0.0f, 100.0f, 0.0f);
    P::hz (l, "lo_mono", "Graves en mono hasta", 0.0f, 300.0f, 0.0f);
    P::toggle (l, "tr_on", "Transitorios", false);
    P::pct (l, "tr_att", "Transitorios: ataque", -100.0f, 100.0f, 0.0f);
    P::pct (l, "tr_sus", "Transitorios: cuerpo", -100.0f, 100.0f, 0.0f);
    P::toggle (l, "ex_on", "Excitador", false);
    P::hz (l, "ex_freq", "Excitador: desde", 1000.0f, 12000.0f, 4000.0f);
    P::pct (l, "ex_drive", "Excitador: drive", 0.0f, 100.0f, 30.0f);
    P::pct (l, "ex_mix", "Excitador: mezcla", 0.0f, 100.0f, 20.0f);
    P::choice (l, "ex_mode", "Excitador: color", { "Cinta (impares)", "Válvula (pares)"_u }, 0);
    P::toggle (l, "im_on", "Imagen estéreo"_u, false);
    P::pct (l, "im_low", "Ancho graves", 0.0f, 200.0f, 100.0f);
    P::pct (l, "im_mid", "Ancho medios", 0.0f, 200.0f, 100.0f);
    P::pct (l, "im_high", "Ancho agudos", 0.0f, 200.0f, 100.0f);

    P::toggle (l, "lim_on", "Limitador", true);
    P::db (l, "lim_gain", "Limitador: ganancia", 0.0f, 24.0f, 0.0f);
    P::flt (l, "ceiling", "Techo (dBTP)", P::range (-3.0f, 0.0f), -1.0f, "dBTP", 1);
    P::ms (l, "lim_rel", "Limitador: soltar", 10.0f, 1000.0f, 120.0f, 150.0f);
    P::db (l, "outgain", "Salida", -24.0f, 0.0f, 0.0f);
    P::choice (l, "listen", "Escuchar", { "Estéreo"_u, "Mono", "Centro (mid)", "Lados (side)" }, 0);
    P::choice (l, "refplay", "Comparar con", { "Tu master", "Referencia 1", "Referencia 2" }, 0);
    P::toggle (l, "refmatch", "Igualar volumen de referencia", true);
    return l;
}

Processor::Processor()
    : RacoonProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true)
                                        .withInput ("Sidechain", AudioChannelSet::stereo(), false),
                       PluginInfo { "Hedgehog", "Mastering", Colour (0xffd9c452), PluginKind::effect, 1, 0.5, true, false },
                       layout())
{
    for (int b = 0; b < numEqBands; ++b) for (int k = 0; k < 9; ++k) eqP[b][k] = param (eqId (b, eqKeys[k]));
    for (int b = 0; b < numMbBands; ++b) for (int k = 0; k < 6; ++k) mbP[b][k] = param (mbId (b, mbKeys[k]));
    for (auto& v : matchDiff) v.store (0.0f);
}

Processor::~Processor()
{
    if (exportJob != nullptr) exportJob->stopThread (10000);
}

// ===================================================================== audio
void Processor::prepareDsp (double sampleRate, int maxBlock)
{
    for (auto& b : eq) b.prepare (sampleRate);
    resonance.prepare (sampleRate);
    match.prepare (sampleRate);
    multiband.prepare (sampleRate);
    lowEnd.prepare (sampleRate);
    transient.prepare (sampleRate);
    exciter.prepare (sampleRate);
    imager.prepare (sampleRate);
    limiter.prepare (sampleRate);
    particle.prepare (sampleRate);
    rmsCoef = std::exp (-1.0 / (sampleRate * 0.3));
    meterOut.prepare (sampleRate);
    meterIn.prepare (sampleRate);
    analyzerIn.prepare (sampleRate);
    analyzerOut.prepare (sampleRate);
    mono.setSize (2, jmax (64, maxBlock));
    lastMatchVersion = -1;
    setLatencySamples (latencyTotal());
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& b : eq) b.reset();
    resonance.reset(); match.reset(); multiband.reset(); lowEnd.reset(); transient.reset(); exciter.reset(); imager.reset(); limiter.reset();
    particle.reset();
    rmsIn = rmsOut = 0.0;
    tpMeter[0].reset(); tpMeter[1].reset();
    refPos[0] = refPos[1] = 0;
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer&)
{
    if (resetRequest.exchange (false))
    {
        meterOut.reset(); meterIn.reset(); analyzerIn.reset(); analyzerOut.reset();
        truePeakMax.store (-100.0f); samplePeakMax.store (-100.0f); correlationMin.store (1.0f); clippedIn.store (0);
        dcSum[0] = dcSum[1] = 0.0; dcCount = 0;
    }
    const int total = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    const int chunk = mono.getNumSamples();

    // settings of this block
    EqBandSettings es[numEqBands];
    for (int b = 0; b < numEqBands; ++b)
    {
        auto& s = es[b];
        s.on = eqP[b][0]->load() > 0.5f; s.type = (int) eqP[b][1]->load(); s.hz = eqP[b][2]->load(); s.gainDb = eqP[b][3]->load(); s.q = eqP[b][4]->load();
        s.dynMode = (int) eqP[b][5]->load(); s.thresholdDb = eqP[b][6]->load(); s.rangeDb = eqP[b][7]->load(); s.where = (int) eqP[b][8]->load();
    }
    MbBandSettings ms[numMbBands];
    for (int b = 0; b < numMbBands; ++b)
    {
        ms[b].on = mbP[b][0]->load() > 0.5f; ms[b].thresholdDb = mbP[b][1]->load(); ms[b].ratio = mbP[b][2]->load();
        ms[b].attackMs = mbP[b][3]->load(); ms[b].releaseMs = mbP[b][4]->load(); ms[b].gainDb = mbP[b][5]->load();
    }
    const float xo[numMbBands - 1] { value ("mb_x1"), value ("mb_x2"), value ("mb_x3"), value ("mb_x4"), value ("mb_x5") };
    multiband.setCrossovers (xo);
    {
        const int v = matchVersion.load();
        const float amt = value ("match_on") > 0.5f ? value ("match_amt") * 0.01f : 0.0f;
        if (v != lastMatchVersion || amt != lastMatchAmount)
        {
            float g[numThirds];
            for (int b = 0; b < numThirds; ++b) g[b] = matchDiff[b].load() * amt;
            match.setGains (g);
            lastMatchVersion = v; lastMatchAmount = amt;
        }
    }
    const float inGain = dsp::dbToGain (value ("ingain")), outGain = dsp::dbToGain (value ("outgain"));
    if (peakReset.exchange (false)) { inPeakHold.store (-100.0f); outPeakHold.store (-100.0f); }
    const bool paOn = value ("pa_on") > 0.5f;
    ParticleSettings ps;
    ps.amount = value ("pa_amount") * 0.01f;
    ps.lowDb = value ("pa_low"); ps.midDb = value ("pa_mid"); ps.highDb = value ("pa_high");
    ps.dynamics = value ("pa_dyn") > 0.5f;
    ps.external = (int) value ("pa_sc") == 1;
    const float* scBase[2] { nullptr, nullptr };
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { scBase[0] = sc.getReadPointer (0); scBase[1] = sc.getReadPointer (jmin (1, sc.getNumChannels() - 1)); }
    }
    float inPk = -100.0f, outPk = -100.0f;
    const bool limOn = value ("lim_on") > 0.5f;
    const float ceilDb = value ("ceiling");
    const int listen = (int) value ("listen");
    const int solo = (int) value ("mb_solo") - 1;
    float corr = 1.0f;

    for (int pos = 0; pos < total; pos += chunk)
    {
        const int n = jmin (chunk, total - pos);
        float* L = buffer.getWritePointer (0, pos);
        float* R = chans > 1 ? buffer.getWritePointer (1, pos) : mono.getWritePointer (1);
        if (chans == 1) FloatVectorOperations::copy (R, L, n);

        int clipped = 0;
        for (int i = 0; i < n; ++i)
        {
            if (std::abs (L[i]) >= 0.999f || std::abs (R[i]) >= 0.999f) ++clipped;
            L[i] *= inGain; R[i] *= inGain;
        }
        if (clipped > 0) clippedIn.fetch_add (clipped);
        for (int i = 0; i < n; ++i)
        {
            const float a = jmax (std::abs (L[i]), std::abs (R[i]));
            inPk = jmax (inPk, dsp::gainToDb (a));
            rmsIn = rmsIn * rmsCoef + (1.0 - rmsCoef) * 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        }
        meterIn.process (L, R, n);
        float* m = mono.getWritePointer (0);
        for (int i = 0; i < n; ++i) m[i] = (L[i] + R[i]) * 0.5f;
        analyzerIn.push (m, n);

        // dynamic EQ on mid / side
        for (int i = 0; i < n; ++i) { const float a = L[i], b = R[i]; L[i] = (a + b) * 0.5f; R[i] = (a - b) * 0.5f; }
        for (int b = 0; b < numEqBands; ++b) eq[b].process (L, R, n, es[b], value ("eq_att"), value ("eq_rel"));
        for (int i = 0; i < n; ++i) { const float mid = L[i], side = R[i]; L[i] = mid + side; R[i] = mid - side; }

        resonance.process (L, R, n, value ("res_on") > 0.5f, value ("res_depth") * 0.01f, value ("res_sharp") * 0.01f, value ("res_lo"), value ("res_hi"),
                           value ("res_speed") * 0.01f);
        match.process (L, R, n);
        if (value ("mb_on") > 0.5f) multiband.process (L, R, n, ms, solo);
        const bool lo = value ("lo_on") > 0.5f;
        if (lo) lowEnd.process (L, R, n, value ("lo_punch") * 0.01f, value ("lo_tight") * 0.01f, value ("lo_freq"));
        if (value ("tr_on") > 0.5f) transient.process (L, R, n, value ("tr_att") * 0.01f, value ("tr_sus") * 0.01f);
        if (value ("ex_on") > 0.5f) exciter.process (L, R, n, value ("ex_freq"), value ("ex_drive") * 0.01f, value ("ex_mix") * 0.01f, (int) value ("ex_mode"));
        if (paOn) particle.process (L, R, scBase[0] != nullptr ? scBase[0] + pos : nullptr, scBase[1] != nullptr ? scBase[1] + pos : nullptr, n, ps);
        const bool im = value ("im_on") > 0.5f;
        imager.process (L, R, n, im ? value ("im_low") * 0.01f : 1.0f, im ? value ("im_mid") * 0.01f : 1.0f, im ? value ("im_high") * 0.01f : 1.0f,
                        lo ? value ("lo_mono") : 0.0f, corr);
        // with the limiter off it still delays the same, so the latency never changes
        limiter.process (L, R, n, limOn ? value ("lim_gain") : 0.0f, limOn ? ceilDb : 24.0f, value ("lim_rel"));
        if (outGain != 1.0f) for (int i = 0; i < n; ++i) { L[i] *= outGain; R[i] *= outGain; }
        for (int i = 0; i < n; ++i)
        {
            outPk = jmax (outPk, dsp::gainToDb (jmax (std::abs (L[i]), std::abs (R[i]))));
            rmsOut = rmsOut * rmsCoef + (1.0 - rmsCoef) * 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        }

        // meters of what leaves
        float tpm = truePeakMax.load(), spm = samplePeakMax.load(), tpNow = -100.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = jmax (tpMeter[0].process (L[i]), tpMeter[1].process (R[i]));
            tpNow = jmax (tpNow, dsp::gainToDb (t));
            spm = jmax (spm, dsp::gainToDb (jmax (std::abs (L[i]), std::abs (R[i]))));
            dcSum[0] += L[i]; dcSum[1] += R[i];
        }
        dcCount += n;
        truePeakMax.store (jmax (tpm, tpNow)); truePeakNow.store (tpNow); samplePeakMax.store (spm);
        meterOut.process (L, R, n);
        for (int i = 0; i < n; ++i) m[i] = (L[i] + R[i]) * 0.5f;
        analyzerOut.push (m, n);

        // monitoring
        if (listen != 0)
            for (int i = 0; i < n; ++i)
            {
                const float mid = (L[i] + R[i]) * 0.5f, side = (L[i] - R[i]) * 0.5f;
                const float v = listen == 1 || listen == 2 ? mid : side;
                L[i] = R[i] = v;
            }

        // the references, at the same loudness as the master
        const int ref = (int) value ("refplay") - 1;
        if (ref >= 0)
        {
            SpinLock::ScopedTryLockType lock (refLock);
            if (lock.isLocked() && refs[ref] != nullptr && refs[ref]->audio.getNumSamples() > 0)
            {
                const auto& r = *refs[ref];
                float g = 1.0f;
                if (value ("refmatch") > 0.5f)
                {
                    const float master = meterOut.integrated() > -69.0f ? meterOut.integrated() : meterOut.shortTerm();
                    if (master > -69.0f && r.lufs > -69.0f) g = dsp::dbToGain (jlimit (-24.0f, 24.0f, master - r.lufs));
                }
                const int len = r.audio.getNumSamples();
                for (int i = 0; i < n; ++i)
                {
                    const int p = (int) (refPos[ref] % len);
                    L[i] = r.audio.getSample (0, p) * g;
                    R[i] = r.audio.getSample (jmin (1, r.audio.getNumChannels() - 1), p) * g;
                    ++refPos[ref];
                }
            }
        }
    }

    momentary.store (meterOut.momentary()); shortTerm.store (meterOut.shortTerm()); integrated.store (meterOut.integrated());
    lra.store (meterOut.range()); inIntegrated.store (meterIn.integrated()); secondsMeasured.store (meterOut.seconds());
    correlation.store (corr);
    if (meterOut.momentary() > -50.0f) correlationMin.store (jmin (correlationMin.load(), corr));
    limiterGr.store (limOn ? limiter.reductionDb() : 0.0f);
    inPeakNow.store (inPk); outPeakNow.store (outPk);
    inPeakHold.store (jmax (inPeakHold.load(), inPk)); outPeakHold.store (jmax (outPeakHold.load(), outPk));
    inRms.store (dsp::gainToDb ((float) std::sqrt (rmsIn))); outRms.store (dsp::gainToDb ((float) std::sqrt (rmsOut)));
    for (int b = 0; b < 3; ++b) particleGr[b].store (paOn && ps.dynamics ? particle.reductionDb (b) : 0.0f);
    for (int b = 0; b < numMbBands; ++b) mbGr[b].store (value ("mb_on") > 0.5f ? multiband.reductionDb (b) : 0.0f);
    for (int b = 0; b < numEqBands; ++b) eqDyn[b].store (eq[b].dynamicDb());
    for (int b = 0; b < numThirds; ++b) { specIn[b].store (analyzerIn.bandDb (b)); specOut[b].store (analyzerOut.bandDb (b)); specOutNow[b].store (analyzerOut.nowDb (b)); }
    for (int k = 0; k < 64; ++k) resCut[k].store (resonance.cutDb (jmin (Resonance::size / 2, (int) (std::pow (2.0, k / 64.0 * 9.0) * 1.0))));
    if (dcCount > 0) dcOffsetDb.store (dsp::gainToDb ((float) (jmax (std::abs (dcSum[0]), std::abs (dcSum[1])) / (double) dcCount)));
}

// ===================================================================== references
bool Processor::loadReference (int slot, const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;
    const int len = (int) std::min<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * 600.0));
    AudioBuffer<float> raw (2, len);
    reader->read (&raw, 0, len, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, len);
    auto r = std::make_unique<Reference>();
    const double target = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    if (std::abs (reader->sampleRate - target) > 1.0)
    {
        const double ratio = reader->sampleRate / target;
        const int outLen = (int) (len / ratio);
        r->audio.setSize (2, outLen);
        for (int c = 0; c < 2; ++c)
        {
            LagrangeInterpolator li;
            li.process (ratio, raw.getReadPointer (c), r->audio.getWritePointer (c), outLen);
        }
    }
    else r->audio = std::move (raw);
    r->sampleRate = target;
    r->name = f.getFileNameWithoutExtension();
    // its loudness and its tonal balance
    Loudness lm; lm.prepare (target);
    ThirdOctave an; an.prepare (target);
    TruePeak tp[2];
    std::vector<float> m ((size_t) r->audio.getNumSamples());
    for (int i = 0; i < r->audio.getNumSamples(); ++i)
    {
        m[(size_t) i] = (r->audio.getSample (0, i) + r->audio.getSample (1, i)) * 0.5f;
        r->truePeak = jmax (r->truePeak, dsp::gainToDb (jmax (tp[0].process (r->audio.getSample (0, i)), tp[1].process (r->audio.getSample (1, i)))));
    }
    lm.process (r->audio.getReadPointer (0), r->audio.getReadPointer (1), r->audio.getNumSamples());
    an.push (m.data(), (int) m.size());
    r->lufs = lm.integrated();
    for (int b = 0; b < numThirds; ++b) r->spectrum[b] = an.bandDb (b);
    {
        SpinLock::ScopedLockType lock (refLock);
        refs[slot] = std::move (r);
        refPos[slot] = 0;
    }
    return true;
}

String Processor::referenceName (int slot) const { SpinLock::ScopedLockType l (refLock); return refs[slot] != nullptr ? refs[slot]->name : String(); }
float Processor::referenceLufs (int slot) const { SpinLock::ScopedLockType l (refLock); return refs[slot] != nullptr ? refs[slot]->lufs : -70.0f; }
void Processor::referenceSpectrum (int slot, float* out) const
{
    SpinLock::ScopedLockType l (refLock);
    for (int b = 0; b < numThirds; ++b) out[b] = refs[slot] != nullptr ? refs[slot]->spectrum[b] : -100.0f;
}

// ===================================================================== match, assistant, checks
static float meanBetween (const float* db, float lo, float hi)
{
    double s = 0.0; int k = 0;
    for (int b = 0; b < numThirds; ++b) { const float c = ThirdOctave::centre (b); if (c >= lo && c <= hi && db[b] > -99.0f) { s += db[b]; ++k; } }
    return k > 0 ? (float) (s / k) : 0.0f;
}

void Processor::learnMatch()
{
    float cur[numThirds], src[numThirds];
    for (int b = 0; b < numThirds; ++b) cur[b] = specIn[b].load();
    const int which = (int) value ("match_src");
    if (which == 0) for (int b = 0; b < numThirds; ++b) src[b] = targetDb ((int) value ("genre"), b);
    else referenceSpectrum (which - 1, src);
    const float mc = meanBetween (cur, 200.0f, 5000.0f), ms = meanBetween (src, 200.0f, 5000.0f);
    float diff[numThirds];
    for (int b = 0; b < numThirds; ++b)
    {
        const bool valid = cur[b] > -99.0f && src[b] > -99.0f && ThirdOctave::centre (b) >= 25.0f && ThirdOctave::centre (b) <= 18000.0f;
        diff[b] = valid ? (src[b] - ms) - (cur[b] - mc) : 0.0f;
    }
    // smooth across neighbours and keep it gentle
    for (int b = 0; b < numThirds; ++b)
    {
        const float a = diff[jmax (0, b - 1)], c = diff[jmin (numThirds - 1, b + 1)];
        matchDiff[b].store (jlimit (-9.0f, 9.0f, 0.25f * a + 0.5f * diff[b] + 0.25f * c));
    }
    matchVersion.fetch_add (1);
}

StringArray Processor::runAssistant()
{
    StringArray done;
    const double secs = secondsMeasured.load();
    if (secs < 5.0) { done.add ("Primero deja sonar la canción (la parte más fuerte) al menos 10 segundos."_u); return done; }
    const int platform = (int) value ("target");
    const float target = platformLufs (platform), now = integrated.load();

    // loudness: the limiter brings it to the target
    if (now > -69.0f)
    {
        const float g = value ("lim_gain");
        float wanted = g + (target - now);
        if (wanted < 0.0f)
        {
            setRealValue ("ingain", jlimit (-24.0f, 24.0f, value ("ingain") + wanted));
            done.add ("Bajé la entrada "_u + String (-wanted, 1) + " dB: estaba más fuerte que "_u + String (target, 0) + " LUFS."_u);
            wanted = 0.0f;
        }
        setRealValue ("lim_on", 1.0f);
        setRealValue ("lim_gain", jlimit (0.0f, 24.0f, wanted));
        setRealValue ("ceiling", platformCeiling (platform));
        done.add ("Limitador a " + String (jlimit (0.0f, 24.0f, wanted), 1) + " dB y techo de " + String (platformCeiling (platform), 1)
                  + " dBTP para " + platformNames()[platform] + ".");
    }

    // tonal balance against the genre
    float cur[numThirds], tgt[numThirds];
    for (int b = 0; b < numThirds; ++b) { cur[b] = specIn[b].load(); tgt[b] = targetDb ((int) value ("genre"), b); }
    const float mc = meanBetween (cur, 200.0f, 5000.0f), mt = meanBetween (tgt, 200.0f, 5000.0f);
    struct Region { float lo, hi; int band; int type; float hz; const char* name; };
    const Region regions[] { { 40, 120, 0, 1, 100.0f, "graves" }, { 200, 500, 3, 0, 300.0f, "medios graves" },
                             { 2000, 5000, 8, 0, 3500.0f, "presencia" }, { 8000, 16000, 11, 2, 10000.0f, "aire" } };
    for (auto& r : regions)
    {
        const float d = (meanBetween (cur, r.lo, r.hi) - mc) - (meanBetween (tgt, r.lo, r.hi) - mt);
        if (std::abs (d) < 1.5f) continue;
        const float g = jlimit (-4.0f, 4.0f, -d * 0.6f);
        setRealValue (eqId (r.band, "on"), 1.0f);
        setRealValue (eqId (r.band, "type"), (float) r.type);
        setRealValue (eqId (r.band, "freq"), r.hz);
        setRealValue (eqId (r.band, "gain"), g);
        setRealValue (eqId (r.band, "q"), r.type == 0 ? 0.8f : 0.7f);
        done.add (String (g > 0 ? "Subí "_u : "Bajé "_u) + String (std::abs (g), 1) + " dB de " + r.name + " (" + (d > 0 ? "sobraban " : "faltaban ")
                  + String (std::abs (d), 1) + " dB contra " + genreNames()[(int) value ("genre")] + ").");
        if (r.band == 8 && d > 3.0f)
        {
            setRealValue ("res_on", 1.0f); setRealValue ("res_depth", 40.0f);
            done.add ("Encendí el control de resonancias: hay estridencias en la presencia."_u);
        }
    }

    // stereo
    const float c = correlationMin.load();
    if (c < 0.0f)
    {
        setRealValue ("lo_on", 1.0f); setRealValue ("lo_mono", 120.0f);
        setRealValue ("im_on", 1.0f); setRealValue ("im_low", 60.0f);
        done.add ("La fase se abre demasiado (" + String (c, 2) + "): puse los graves en mono hasta 120 Hz.");
    }
    if (done.isEmpty()) done.add ("Todo está en su lugar para "_u + platformNames()[platform] + ".");
    return done;
}

std::vector<Processor::Check> Processor::qualityChecks() const
{
    std::vector<Check> v;
    const float tp = truePeakMax.load(), ceil = value ("ceiling"), lufs = integrated.load(), target = platformLufs ((int) value ("target"));
    const double secs = secondsMeasured.load();
    v.push_back ({ tp <= ceil + 0.1f, "Pico real máximo "_u + String (tp, 1) + " dBTP (techo " + String (ceil, 1) + ")" });
    if (secs >= 10.0) v.push_back ({ std::abs (lufs - target) <= 1.0f, "Volumen integrado " + String (lufs, 1) + " LUFS (destino " + String (target, 0) + ")" });
    else v.push_back ({ false, "Faltan segundos para medir el volumen integrado (" + String (roundToInt (secs)) + " de 10)" });
    const float range = lra.load();
    v.push_back ({ range >= 3.0f || secs < 10.0, "Rango de volumen (LRA) " + String (range, 1) + " LU" + (range < 3.0f && secs >= 10.0 ? ": muy aplastado" : "") });
    const float plr = tp - lufs;
    v.push_back ({ plr >= 6.0f || lufs <= -69.0f, "Pico contra volumen (PLR) " + String (plr, 1) + " dB" });
    const float c = correlationMin.load();
    v.push_back ({ c >= -0.2f, "Fase: correlación mínima "_u + String (c, 2) });
    const float dc = dcOffsetDb.load();
    v.push_back ({ dc < -45.0f, "Corriente continua " + String (roundToInt (dc)) + " dB" });
    const int clips = clippedIn.load();
    v.push_back ({ clips == 0, clips == 0 ? String ("La mezcla llega sin saturar") : "La mezcla llega saturada en " + String (clips) + " muestras" });
    float cur[numThirds], tgt[numThirds];
    for (int b = 0; b < numThirds; ++b) { cur[b] = specOut[b].load(); tgt[b] = targetDb ((int) value ("genre"), b); }
    const float mc = meanBetween (cur, 200.0f, 5000.0f), mt = meanBetween (tgt, 200.0f, 5000.0f);
    const float sub = (meanBetween (cur, 30, 80) - mc) - (meanBetween (tgt, 30, 80) - mt);
    const float harsh = (meanBetween (cur, 2500, 5000) - mc) - (meanBetween (tgt, 2500, 5000) - mt);
    v.push_back ({ sub < 4.0f, "Subgraves " + String (sub >= 0 ? "+" : "") + String (sub, 1) + " dB contra el género"_u });
    v.push_back ({ harsh < 3.0f, "Estridencia (2.5 a 5 kHz) " + String (harsh >= 0 ? "+" : "") + String (harsh, 1) + " dB"_u });
    return v;
}

File Processor::exportFolder() const
{
    auto d = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Hedgehog");
    d.createDirectory();
    return d;
}

File Processor::writeReport() const
{
    auto dir = exportFolder().getChildFile ("Reportes");
    dir.createDirectory();
    auto f = dir.getNonexistentChildFile ("Reporte " + Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".txt", false);
    String t;
    t << "RACOON HEDGEHOG · REPORTE DE MASTER\n"_u << Time::getCurrentTime().toString (true, true) << "\n\n";
    t << "Destino: " << platformNames()[(int) value ("target")] << "   Género: "_u << genreNames()[(int) value ("genre")] << "\n\n";
    t << "MEDIDAS (" << String (secondsMeasured.load(), 0) << " s medidos)\n";
    t << "  Integrado: " << String (integrated.load(), 1) << " LUFS\n";
    t << "  Corto plazo: " << String (shortTerm.load(), 1) << " LUFS\n";
    t << "  Rango (LRA): " << String (lra.load(), 1) << " LU\n";
    t << "  Pico real máximo: "_u << String (truePeakMax.load(), 1) << " dBTP\n";
    t << "  Pico de muestra máximo: "_u << String (samplePeakMax.load(), 1) << " dBFS\n";
    t << "  Correlación mínima: "_u << String (correlationMin.load(), 2) << "\n";
    t << "  Entrada (antes de la cadena): " << String (inIntegrated.load(), 1) << " LUFS\n\n";
    t << "CONTROL DE CALIDAD\n";
    for (auto& c : qualityChecks()) t << (c.ok ? "  [bien]   " : "  [revisar] ") << c.text << "\n";
    t << "\nCADENA\n";
    auto onOff = [this] (const char* id) { return value (id) > 0.5f ? String ("encendido") : String ("apagado"); };
    int eqOn = 0;
    for (int b = 0; b < numEqBands; ++b)
        if (value (eqId (b, "on")) > 0.5f)
        {
            ++eqOn;
            t << "  EQ " << (b + 1) << ": " << String (value (eqId (b, "freq")), 0) << " Hz " << String (value (eqId (b, "gain")), 1) << " dB"
              << (value (eqId (b, "dyn")) > 0.5f ? " (dinámica)"_u : "") << "\n";
        }
    if (eqOn == 0) t << "  EQ dinámico: sin bandas\n"_u;
    t << "  Resonancias: " << onOff ("res_on") << "\n  Match EQ: " << onOff ("match_on") << "\n  Multibanda: " << onOff ("mb_on")
      << "\n  Graves: " << onOff ("lo_on") << "\n  Transitorios: " << onOff ("tr_on") << "\n  Excitador: " << onOff ("ex_on")
      << "\n  Imagen: " << onOff ("im_on") << "\n  Limitador: " << onOff ("lim_on") << ", +" << String (value ("lim_gain"), 1) << " dB, techo "
      << String (value ("ceiling"), 1) << " dBTP\n";
    t << "\nREFERENCIAS\n";
    for (int r = 0; r < 2; ++r)
        t << "  " << (r + 1) << ": " << (referenceName (r).isEmpty() ? String ("(ninguna)") : referenceName (r) + "  " + String (referenceLufs (r), 1) + " LUFS") << "\n";
    t << "\nVERSIONES\n";
    for (int v = 0; v < 3; ++v)
        t << "  " << String::charToString ((juce_wchar) ('A' + v)) << ": " << (versions[v].isValid() ? String (versionLoudness[v], 1) + " LUFS al guardarla" : String ("vacía"_u)) << "\n";
    f.replaceWithText (t);
    return f;
}

// ===================================================================== versions
void Processor::saveVersion (int slot)
{
    versions[slot] = apvts.copyState().createCopy();
    versionLoudness[slot] = integrated.load() > -69.0f ? integrated.load() : shortTerm.load();
    activeVersion = slot;
}

void Processor::recallVersion (int slot)
{
    if (! versions[slot].isValid()) return;
    apvts.replaceState (versions[slot].createCopy());
    activeVersion = slot;
}

void Processor::saveExtraState (ValueTree& v) const
{
    ValueTree vs ("Versions");
    for (int i = 0; i < 3; ++i)
        if (versions[i].isValid())
        {
            ValueTree one ("Version");
            one.setProperty ("slot", i, nullptr);
            one.setProperty ("lufs", versionLoudness[i], nullptr);
            one.appendChild (versions[i].createCopy(), nullptr);
            vs.appendChild (one, nullptr);
        }
    v.appendChild (vs, nullptr);
    String m;
    for (int b = 0; b < numThirds; ++b) m << String (matchDiff[b].load(), 2) << (b < numThirds - 1 ? "," : "");
    v.setProperty ("matchDiff", m, nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    for (auto& x : versions) x = {};
    const auto vs = v.getChildWithName ("Versions");
    for (int i = 0; i < vs.getNumChildren(); ++i)
    {
        const auto one = vs.getChild (i);
        const int slot = jlimit (0, 2, (int) one.getProperty ("slot"));
        if (one.getNumChildren() > 0) { versions[slot] = one.getChild (0).createCopy(); versionLoudness[slot] = (float) one.getProperty ("lufs"); }
    }
    const auto parts = StringArray::fromTokens (v.getProperty ("matchDiff").toString(), ",", "");
    for (int b = 0; b < numThirds; ++b) matchDiff[b].store (b < parts.size() ? parts[b].getFloatValue() : 0.0f);
    matchVersion.fetch_add (1);
}

// ===================================================================== batch export
bool Processor::renderFile (const File& in, const File& out) const
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr) return false;
    Processor p;
    MemoryBlock state;
    const_cast<Processor*> (this)->getStateInformation (state);
    p.setStateInformation (state.getData(), (int) state.getSize());
    p.setRealValue ("refplay", 0.0f);
    p.setRealValue ("listen", 0.0f);
    const double rate = reader->sampleRate;
    constexpr int block = 1024;
    p.setRateAndBufferSizeDetails (rate, block);
    p.prepareToPlay (rate, block);
    const int lat = p.getLatencySamples();
    const int64 len = reader->lengthInSamples;
    out.deleteFile();
    std::unique_ptr<FileOutputStream> os (out.createOutputStream());
    if (os == nullptr) return false;
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0));
    if (writer == nullptr) return false;
    os.release();
    AudioBuffer<float> buf (2, block);
    int64 written = 0, skip = lat;
    for (int64 pos = 0; written < len; pos += block)
    {
        const int n = block;
        buf.clear();
        if (pos < len)
        {
            const int r = (int) std::min<int64> (n, len - pos);
            reader->read (&buf, 0, r, pos, true, reader->numChannels > 1);
            if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, r);
        }
        MidiBuffer m;
        p.processBlock (buf, m);
        int from = 0;
        if (skip > 0) { from = (int) std::min<int64> (skip, n); skip -= from; }
        const int count = (int) std::min<int64> (n - from, len - written);
        if (count > 0)
        {
            const float* ptrs[2] { buf.getReadPointer (0, from), buf.getReadPointer (1, from) };
            writer->writeFromFloatArrays (ptrs, 2, count);
            written += count;
        }
        if (pos > len + lat + block * 2) break;
    }
    return true;
}

class Processor::ExportJob : public Thread
{
public:
    ExportJob (Processor& p, Array<File> f) : Thread ("Hedgehog export"), proc (p), files (std::move (f)) {}
    void run() override
    {
        auto dir = proc.exportFolder().getChildFile ("Exportados");
        dir.createDirectory();
        int ok = 0;
        for (int i = 0; i < files.size() && ! threadShouldExit(); ++i)
        {
            proc.exportProgress.store ((float) i / (float) files.size());
            const auto out = dir.getNonexistentChildFile (files[i].getFileNameWithoutExtension() + " (master)", ".wav", false);
            if (proc.renderFile (files[i], out)) ++ok;
        }
        proc.lastExportMessage = String (ok) + " de " + String (files.size()) + " archivos en " + dir.getFullPathName();
        proc.exportProgress.store (-1.0f);
    }
private:
    Processor& proc;
    Array<File> files;
};

void Processor::startExport (const Array<File>& files)
{
    if (exportJob != nullptr && exportJob->isThreadRunning()) return;
    exportJob = std::make_unique<ExportJob> (*this, files);
    exportProgress.store (0.0f);
    exportJob->startThread();
}

// ===================================================================== presets
std::vector<FactoryPreset> Processor::factoryPresets() const
{
    auto eqBand = [] (int b, int type, float hz, float gain, float q, int dyn = 0, float thr = -24.0f, float range = 6.0f)
    {
        return std::vector<std::pair<String, float>> { { eqId (b, "on"), 1 }, { eqId (b, "type"), (float) type }, { eqId (b, "freq"), hz }, { eqId (b, "gain"), gain },
                                                       { eqId (b, "q"), q }, { eqId (b, "dyn"), (float) dyn }, { eqId (b, "thr"), thr }, { eqId (b, "range"), range } };
    };
    auto join = [] (std::initializer_list<std::vector<std::pair<String, float>>> parts)
    {
        std::vector<std::pair<String, float>> all;
        for (auto& p : parts) all.insert (all.end(), p.begin(), p.end());
        return all;
    };
    // the lab presets were made without the Partícula: they switch it off
    auto lab = [] (std::vector<std::pair<String, float>> v) { v.push_back ({ "pa_on", 0.0f }); return v; };
    std::vector<FactoryPreset> list {
        { "Inicial", "Base", {} },
        { "Partícula suave (50 %)"_u, "Partícula"_u, { { "pa_amount", 50 }, { "lim_gain", 3 } } },
        { "Partícula de fábrica (100 %)"_u, "Partícula"_u, { { "pa_amount", 100 }, { "lim_gain", 4 } } },
        { "Partícula con brillo"_u, "Partícula"_u, { { "pa_amount", 100 }, { "pa_high", 2.0f }, { "pa_low", 1.0f }, { "lim_gain", 4 } } },
        { "Partícula fuerte (150 %)"_u, "Partícula"_u, { { "pa_amount", 150 }, { "lim_gain", 6 } } },
        { "Partícula club (200 %)"_u, "Partícula"_u, { { "pa_amount", 200 }, { "pa_low", 1.5f }, { "lim_gain", 8 }, { "ceiling", -0.3f }, { "target", 2 } } },
        { "Streaming limpio (-14)", "Destino", join ({ { { "target", 0 }, { "lim_gain", 4 }, { "ceiling", -1 } } }) },
        { "Club fuerte (-8)", "Destino", join ({ { { "target", 2 }, { "lim_gain", 9 }, { "ceiling", -0.3f }, { "mb_on", 1 }, { "lo_on", 1 }, { "lo_mono", 120 }, { "lo_tight", 30 } } }) },
        { "Pop brillante", "Género"_u, join ({ { { "genre", 0 }, { "lim_gain", 5 }, { "ex_on", 1 }, { "ex_freq", 5000 }, { "ex_mix", 15 } }, eqBand (11, 2, 11000, 2.0f, 0.7f),
                                               eqBand (8, 0, 3500, -2.0f, 1.2f, 1, -30.0f, 4.0f) }) },
        { "Hip-hop con peso", "Género"_u, join ({ { { "genre", 2 }, { "lim_gain", 7 }, { "lo_on", 1 }, { "lo_punch", 40 }, { "lo_mono", 100 } }, eqBand (0, 1, 70, 2.0f, 0.7f) }) },
        { "Rock con cuerpo", "Género"_u, join ({ { { "genre", 3 }, { "lim_gain", 6 }, { "mb_on", 1 }, { "tr_on", 1 }, { "tr_att", 25 } }, eqBand (3, 0, 300, -1.5f, 0.9f) }) },
        { "Acústico natural"_u, "Género"_u, join ({ { { "genre", 4 }, { "lim_gain", 3 }, { "lim_rel", 300 } }, eqBand (0, 3, 30, 0.0f, 0.7f) }) },
        { "Electrónica ancha"_u, "Género"_u, join ({ { { "genre", 1 }, { "lim_gain", 8 }, { "im_on", 1 }, { "im_mid", 120 }, { "im_high", 130 }, { "lo_on", 1 }, { "lo_mono", 140 } } }) },
        { "Podcast claro (-16)", "Destino", join ({ { { "target", 4 }, { "lim_gain", 6 }, { "mb_on", 1 } }, eqBand (0, 3, 80, 0.0f, 0.7f), eqBand (8, 0, 3000, 2.0f, 0.8f),
                                                    eqBand (10, 0, 7000, -3.0f, 2.0f, 1, -36.0f, 6.0f) }) },
        { "Cine (-24)", "Destino", join ({ { { "target", 5 }, { "genre", 5 }, { "ceiling", -2 }, { "lim_rel", 400 } } }) },
        { "Domar estridencias", "Corregir", join ({ { { "res_on", 1 }, { "res_depth", 60 }, { "res_lo", 1500 }, { "res_hi", 9000 } }, eqBand (8, 0, 3500, 0.0f, 1.5f, 1, -30.0f, 6.0f) }) },
        { "Graves en mono", "Corregir", join ({ { { "lo_on", 1 }, { "lo_mono", 150 }, { "im_on", 1 }, { "im_low", 50 } } }) },
    };
    for (size_t i = 6; i < list.size(); ++i) list[i].values = lab (list[i].values);
    return list;
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
             { 8.5, 0.4, 36, 100, 1 },
             { 9.5, 0.4, 36, 100, 1 },
             { 10.5, 0.4, 36, 100, 1 },
             { 11.5, 0.4, 36, 100, 1 },
             { 12.5, 0.4, 31, 100, 1 },
             { 13.5, 0.4, 31, 100, 1 },
             { 14.5, 0.4, 31, 100, 1 },
             { 15.5, 0.4, 31, 100, 1 },
             { 0.0, 3.9, 57, 78, 1 },
             { 0.0, 3.9, 60, 78, 1 },
             { 0.0, 3.9, 64, 78, 1 },
             { 4.0, 3.9, 53, 78, 1 },
             { 4.0, 3.9, 57, 78, 1 },
             { 4.0, 3.9, 60, 78, 1 },
             { 8.0, 3.9, 52, 78, 1 },
             { 8.0, 3.9, 55, 78, 1 },
             { 8.0, 3.9, 60, 78, 1 },
             { 12.0, 3.9, 55, 78, 1 },
             { 12.0, 3.9, 59, 78, 1 },
             { 12.0, 3.9, 62, 78, 1 } };
}

AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

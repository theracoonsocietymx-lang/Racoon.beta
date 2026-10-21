#include "SquirrelProcessor.h"
#include "SquirrelEditor.h"
#include "../../Shared/Biblioteca.h"

namespace racoon::squirrel
{
using namespace juce;

void Sample::buildOverview()
{
    const int n = length(), bins = (n + 255) / 256;
    overview.assign ((size_t) jmax (1, bins), 0.0f);
    peak = 0.0f;
    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (d[i]);
            auto& o = overview[(size_t) (i >> 8)];
            if (a > o) o = a;
            if (a > peak) peak = a;
        }
    }
    if (peak <= 0.0f) peak = 1.0f;
}

// ===================================================================== envelope
float SEnv::next (float a, float d, float s, float r, double sr)
{
    switch (stage)
    {
        case idle: level = 0.0f; break;
        case attack:
            if (a <= 0.000101f) level = 1.0f;          // minimum attack: instant (bit-perfect start)
            else level += (float) (1.0 / (a * sr));
            if (level >= 1.0f) { level = 1.0f; stage = decay; }
            break;
        case decay:
            level = s + (level - s) * d;                 // d, r: per-sample coefficients
            if (std::abs (level - s) < 1.0e-5f) { level = s; stage = sustain; }
            break;
        case sustain:
            if (level != s) level += (s - level) * 0.002f;
            break;
        case release:
            level *= r;
            if (level < 1.0e-4f) { level = 0.0f; stage = idle; }
            break;
    }
    return level;
}

// ===================================================================== parameters
AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    P::Layout l;
    P::choice (l, "mode", "Modo", { "Cromático"_u, "Cortes" }, 0);
    P::flt (l, "rate", "Velocidad", P::range (0.25f, 4.0f, 1.0f), 1.0f, "x", 3);
    P::integer (l, "pitch", "Afinación"_u, -48, 48, 0);
    P::flt (l, "fine", "Fino", P::range (-100.0f, 100.0f), 0.0f, "ct", 0);
    P::pct (l, "startoffset", "Inicio", 0.0f, 100.0f, 0.0f);
    P::pct (l, "velstart", "Velocidad al inicio", 0.0f, 100.0f, 0.0f);
    P::toggle (l, "reverse", "Reverso", false);
    P::ms (l, "loopxf", "Crossfade del loop", 0.0f, 500.0f, 20.0f, 60.0f);
    P::integer (l, "voices", "Voces", 1, maxVoices, 32);
    P::integer (l, "bendrange", "Rango de pitch bend", 0, 24, 2);
    P::flt (l, "amp_a", "Ataque", P::range (0.0001f, 30.0f, 0.3f), 0.0001f, "s", 4);
    P::flt (l, "amp_d", "Caída"_u, P::range (0.001f, 30.0f, 1.0f), 1.0f, "s", 3);
    P::pct (l, "amp_s", "Sostenido", 0.0f, 100.0f, 100.0f);
    P::flt (l, "amp_r", "Relajación"_u, P::range (0.0001f, 30.0f, 1.0f), 0.25f, "s", 3);
    P::pct (l, "velamp", "Velocidad al volumen", 0.0f, 100.0f, 60.0f);
    P::choice (l, "ftype", "Filtro", { "Apagado", "LP", "BP", "HP" }, 0);
    P::hz (l, "cutoff", "Corte", 20.0f, 20000.0f, 6000.0f);
    P::pct (l, "reso", "Resonancia", 0.0f, 95.0f, 10.0f);
    P::flt (l, "fenv", "Envolvente al filtro", P::range (-100.0f, 100.0f), 0.0f, "%", 0);
    P::flt (l, "flt_a", "Filtro ataque", P::range (0.0001f, 30.0f, 0.3f), 0.001f, "s", 4);
    P::flt (l, "flt_d", "Filtro caída"_u, P::range (0.001f, 30.0f, 1.0f), 0.5f, "s", 3);
    P::pct (l, "flt_s", "Filtro sostenido", 0.0f, 100.0f, 0.0f);
    P::flt (l, "flt_r", "Filtro relajación"_u, P::range (0.0001f, 30.0f, 1.0f), 0.5f, "s", 3);
    P::pct (l, "velflt", "Velocidad al filtro", 0.0f, 100.0f, 0.0f);
    P::flt (l, "lforate", "LFO velocidad", P::range (0.01f, 20.0f, 2.0f), 4.0f, "Hz", 2);
    P::choice (l, "lfoshape", "LFO forma", { "Seno", "Triángulo"_u, "Cuadrada", "S&H" }, 0);
    P::flt (l, "lfopitch", "LFO a la afinación"_u, P::range (0.0f, 100.0f), 0.0f, "ct", 0);
    P::pct (l, "lfocutoff", "LFO al filtro", 0.0f, 100.0f, 0.0f);
    P::pct (l, "lfoamp", "LFO al volumen", 0.0f, 100.0f, 0.0f);
    P::db (l, "slthresh", "Umbral de cortes", -60.0f, 0.0f, -36.0f);
    P::pct (l, "slsens", "Sensibilidad de cortes", 0.0f, 100.0f, 60.0f);
    P::choice (l, "slgrid", "Rejilla de cortes", { "Libre", "1/4", "1/8", "1/16", "1/32" }, 0);
    P::flt (l, "slbpm", "Tempo de la muestra", P::range (40.0f, 240.0f), 120.0f, "BPM", 1);
    P::toggle (l, "slgate", "Cortes con gate", false);
    P::db (l, "volume", "Volumen", -36.0f, 12.0f, 0.0f);
    P::flt (l, "pan", "Paneo", P::range (-100.0f, 100.0f), 0.0f, "", 0);
    return l;
}

// ===================================================================== background loader
class Processor::Loader : public Thread
{
public:
    explicit Loader (Processor& p) : Thread ("Squirrel loader"), proc (p) { startThread(); }
    ~Loader() override { signalThreadShouldExit(); notify(); stopThread (4000); }

    struct Job
    {
        Array<File> files;          // new files
        bool addLayers = false;
        File file;                  // or: fill existing zones from a file...
        MemoryBlock data;           // ...or from embedded audio
        String name;
        std::vector<uint32> zoneIds;
    };
    void add (Job j)
    {
        { const ScopedLock sl (lock); jobs.push_back (std::move (j)); }
        notify();
    }
    bool busy() const { const ScopedLock sl (lock); return working || ! jobs.empty(); }

    void run() override
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const ScopedLock sl (lock);
                if (jobs.empty()) { working = false; }
                else { job = std::move (jobs.front()); jobs.erase (jobs.begin()); working = true; }
            }
            if (! working) { wait (200); continue; }

            if (! job.files.isEmpty())
            {
                std::vector<std::shared_ptr<Sample>> loaded;
                for (auto& f : job.files) if (auto s = proc.readFile (f)) loaded.push_back (s);
                if (! loaded.empty())
                    proc.edit ([&] (Program& p)
                    {
                        if (! job.addLayers) { p.zones.clear(); p.slices.clear(); }
                        const bool kit = ! job.addLayers && loaded.size() > 1;
                        for (size_t i = 0; i < loaded.size(); ++i)
                        {
                            Zone z;
                            z.id = newZoneId();
                            z.sample = loaded[i];
                            z.path = loaded[i]->file.getFullPathName();
                            z.name = loaded[i]->name;
                            if (kit) { z.lowKey = z.highKey = z.root = jmin (127, 36 + (int) i); z.oneShot = true; }
                            p.zones.push_back (z);
                        }
                    });
            }
            else
            {
                std::shared_ptr<Sample> s;
                if (job.data.getSize() > 0)
                {
                    WavAudioFormat wav;
                    std::unique_ptr<AudioFormatReader> r (wav.createReaderFor (new MemoryInputStream (job.data, false), true));
                    if (r != nullptr && r->lengthInSamples > 0)
                    {
                        s = std::make_shared<Sample>();
                        s->audio.setSize ((int) jmin (2u, r->numChannels), (int) r->lengthInSamples);
                        r->read (&s->audio, 0, (int) r->lengthInSamples, 0, true, s->audio.getNumChannels() > 1);
                        s->rate = r->sampleRate;
                        s->name = job.name;
                        s->file = job.file;
                        s->buildOverview();
                    }
                }
                else s = proc.readFile (job.file);
                proc.edit ([&] (Program& p)
                {
                    for (auto& z : p.zones)
                        if (std::find (job.zoneIds.begin(), job.zoneIds.end(), z.id) != job.zoneIds.end())
                        {
                            z.sample = s;
                            z.missing = s == nullptr;
                            if (s != nullptr) z.path = job.file.getFullPathName().isNotEmpty() ? Processor::libraryPath (job.file) : z.path;
                        }
                });
            }
        }
    }

private:
    Processor& proc;
    CriticalSection lock;
    std::vector<Job> jobs;
    bool working = false;
};

// frees replaced programs once the audio thread can no longer be using them
class Processor::Janitor : public Timer
{
public:
    explicit Janitor (Processor& p) : proc (p) { startTimer (300); }
    void timerCallback() override { proc.collectGarbage(); }
private:
    Processor& proc;
};

// ===================================================================== construction
Processor::Processor()
    : RacoonProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true),
                       PluginInfo { "Squirrel", "Sampler", Colour (0xffd9c452), PluginKind::instrument, 1, 0.0, false, false },
                       layout())
{
    formats.registerBasicFormats();
    current = std::make_unique<Program>();
    {
        Zone z;
        z.id = newZoneId();
        z.sample = makeBuiltin (0);
        z.name = z.sample->name;
        z.loopMode = loopForward;
        z.loopStart = (int) (z.sample->rate * 0.8);
        z.loopEnd = (int) (z.sample->rate * 2.2);
        current->zones.push_back (z);
    }
    live.store (current.get());
    loader = std::make_unique<Loader> (*this);
    if (MessageManager::getInstanceWithoutCreating() != nullptr) janitor = std::make_unique<Janitor> (*this);
}

Processor::~Processor()
{
    janitor.reset();
    loader.reset();
    for (auto& v : voices) if (v.active && v.sample) v.sample->voiceRefs--;
}

uint32 Processor::newZoneId()
{
    static std::atomic<uint32> counter { 1 };
    return counter++;
}

void Processor::edit (std::function<void (Program&)> fn)
{
    const ScopedLock sl (editLock);
    auto next = std::make_unique<Program> (*current);
    fn (*next);
    next->serial = current->serial + 1;
    live.store (next.get());
    retired.emplace_back (std::move (current), blocksDone.load());
    current = std::move (next);
    ++programVersion;
    if (MessageManager::getInstanceWithoutCreating() != nullptr && MessageManager::getInstance()->isThisTheMessageThread())
        collectGarbage();
}

void Processor::collectGarbage()
{
    const ScopedLock sl (editLock);
    for (size_t i = 0; i < retired.size(); )
    {
        auto& [prog, stamp] = retired[i];
        bool safe = ! audioBusy.load() || blocksDone.load() >= stamp + 2;
        // a sample only dies with this program if nobody else holds it; then no voice may be playing it
        for (auto& z : prog->zones)
            if (z.sample != nullptr && z.sample.use_count() == 1 && z.sample->voiceRefs.load() > 0) safe = false;
        if (safe) retired.erase (retired.begin() + (long) i);
        else ++i;
    }
}

bool Processor::isLoading() const { return loader != nullptr && loader->busy(); }

void Processor::waitUntilIdle (int timeoutMs) const
{
    const auto until = Time::getMillisecondCounter() + (uint32) timeoutMs;
    while (isLoading() && Time::getMillisecondCounter() < until) Thread::sleep (5);
}

std::shared_ptr<Sample> Processor::readFile (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return nullptr;
    const int64 maxFrames = (int64) (r->sampleRate * 60.0 * 20.0);   // 20 minutes in memory at most
    auto s = std::make_shared<Sample>();
    const int len = (int) jmin (r->lengthInSamples, maxFrames);
    s->audio.setSize ((int) jlimit (1u, 2u, r->numChannels), len);
    r->read (&s->audio, 0, len, 0, true, s->audio.getNumChannels() > 1);
    s->rate = r->sampleRate;
    s->file = f;
    s->name = f.getFileNameWithoutExtension();
    s->buildOverview();
    return s;
}

void Processor::loadFiles (const Array<File>& files, bool addLayers)
{
    Loader::Job j;
    j.files = files;
    j.addLayers = addLayers;
    loader->add (std::move (j));
}

bool Processor::loadFileNow (const File& f, bool addLayer)
{
    auto s = readFile (f);
    if (s == nullptr) return false;
    edit ([&] (Program& p)
    {
        if (! addLayer) { p.zones.clear(); p.slices.clear(); }
        Zone z;
        z.id = newZoneId();
        z.sample = s; z.path = f.getFullPathName(); z.name = s->name;
        p.zones.push_back (z);
    });
    return true;
}

void Processor::addBuffer (const AudioBuffer<float>& b, double rate, const String& name, std::function<void (Zone&)> setup, bool replace)
{
    auto s = std::make_shared<Sample>();
    s->audio.makeCopyOf (b);
    s->rate = rate;
    s->name = name;
    s->buildOverview();
    edit ([&] (Program& p)
    {
        if (replace) { p.zones.clear(); p.slices.clear(); }
        Zone z;
        z.id = newZoneId();
        z.sample = s; z.name = name;
        if (setup) setup (z);
        p.zones.push_back (z);
    });
}

int Processor::relocateMissing (const File& folder)
{
    int found = 0;
    for (auto& z : program().zones)
    {
        if (! z.missing) continue;
        const auto fileName = resolvePath (z.path).getFileName();
        auto hits = folder.findChildFiles (File::findFiles, true, fileName);
        if (hits.isEmpty()) continue;
        Loader::Job j;
        j.file = hits.getFirst();
        j.zoneIds = { z.id };
        loader->add (std::move (j));
        ++found;
    }
    return found;
}

// ===================================================================== factory sounds (made here, nothing borrowed)
StringArray Processor::builtinNames() { return { "Tono tibio", "Campana", "Ritmo 120", "Aire" }; }

std::shared_ptr<Sample> Processor::makeBuiltin (int kind)
{
    auto s = std::make_shared<Sample>();
    const double rate = 48000.0;
    s->rate = rate;
    s->builtin = kind;
    s->name = builtinNames()[kind];
    const double f0 = 261.6256;
    dsp::Rng rng; rng.seed (77u + (uint32) kind);
    const double tp = MathConstants<double>::twoPi;
    switch (kind)
    {
        case 0:   // warm sustained tone, loopable
        {
            const int n = (int) (rate * 2.5);
            s->audio.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / rate;
                double v = 0.0;
                for (int h = 1; h <= 14; ++h) v += std::sin (tp * f0 * h * t + h * 0.7) / std::pow ((double) h, 1.4);
                const double env = jmin (1.0, t / 0.012) * (0.85 + 0.15 * std::exp (-t * 2.0));
                const double w = std::sin (tp * f0 * 1.003 * t) * 0.12;
                s->audio.setSample (0, i, (float) ((v + w) * env * 0.42));
                s->audio.setSample (1, i, (float) ((v - w) * env * 0.42));
            }
            break;
        }
        case 1:   // bell / electric piano-like pluck
        {
            const int n = (int) (rate * 2.2);
            s->audio.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / rate;
                const double mod = std::sin (tp * f0 * 3.5 * t) * 2.2 * std::exp (-t * 6.0);
                const double v = std::sin (tp * f0 * t + mod) * std::exp (-t * 2.4) + 0.3 * std::sin (tp * f0 * 2.0 * t) * std::exp (-t * 4.0);
                const double a = jmin (1.0, t / 0.002);
                s->audio.setSample (0, i, (float) (v * a * 0.6));
                s->audio.setSample (1, i, (float) (v * a * 0.6));
            }
            break;
        }
        case 2:   // two bars of drums at 120 BPM: kick, snare, hats
        {
            const int step = (int) (rate * 0.125), n = step * 32;
            s->audio.setSize (2, n);
            s->audio.clear();
            auto addHit = [&] (int at, int type)
            {
                for (int i = 0; i < step * 3 && at + i < n; ++i)
                {
                    const double t = i / rate;
                    double v = 0.0;
                    if (type == 0) v = std::sin (tp * (50.0 * t + 90.0 * (1.0 - std::exp (-t * 30.0)) / 30.0 * 30.0 * 0.033)) * std::exp (-t * 9.0);
                    else if (type == 1) v = (rng.nextBipolar() * 0.7 + 0.4 * std::sin (tp * 190.0 * t)) * std::exp (-t * 18.0);
                    else { const double x = rng.nextBipolar(); v = x * std::exp (-t * 60.0) * 0.45; }
                    const double a = jmin (1.0, t / 0.0005);
                    for (int c = 0; c < 2; ++c) s->audio.addSample (c, at + i, (float) (v * a * 0.8));
                }
            };
            for (int k = 0; k < 32; ++k)
            {
                if (k % 8 == 0 || k == 11 || k == 27) addHit (k * step, 0);
                if (k % 8 == 4) addHit (k * step, 1);
                if (k % 2 == 1) addHit (k * step, 2);
            }
            break;
        }
        default:   // breathy noise texture, loopable
        {
            const int n = (int) (rate * 3.0);
            s->audio.setSize (2, n);
            float lp[2] {}, bp[2] {};
            for (int i = 0; i < n; ++i)
            {
                const double t = i / rate;
                const double env = jmin (1.0, t / 0.4) * jmin (1.0, (3.0 - t) / 0.4);
                for (int c = 0; c < 2; ++c)
                {
                    const float x = rng.nextBipolar();
                    lp[c] += 0.05f * (x - lp[c]);
                    bp[c] += 0.2f * (lp[c] - bp[c]);
                    s->audio.setSample (c, i, (float) ((lp[c] - bp[c] * 0.5f) * 2.2f * env));
                }
            }
            break;
        }
    }
    s->buildOverview();
    return s;
}

void Processor::loadBuiltin (int kind)
{
    auto s = makeBuiltin (kind);
    edit ([&] (Program& p)
    {
        p.zones.clear(); p.slices.clear();
        Zone z;
        z.id = newZoneId();
        z.sample = s; z.name = s->name;
        if (kind == 0 || kind == 3)
        {
            z.loopMode = loopForward;
            z.loopStart = (int) (s->rate * (kind == 0 ? 0.8 : 0.6));
            z.loopEnd = (int) (s->rate * (kind == 0 ? 2.2 : 2.4));
        }
        if (kind == 2) z.oneShot = true;
        p.zones.push_back (z);
    });
    if (kind == 2) detectSlices();
}

// ===================================================================== slicing by transients
void Processor::detectSlices()
{
    const auto& prog = program();
    if (prog.zones.empty() || prog.zones[0].sample == nullptr) return;
    const auto& z = prog.zones[0];
    const auto& a = z.sample->audio;
    const int s0 = z.startF(), s1 = z.endF();
    const float threshDb = value ("slthresh");
    const float rise = jmap (value ("slsens") * 0.01f, 9.0f, 1.5f);
    const int hop = 256;
    std::vector<float> env;
    for (int p = s0; p + hop <= s1; p += hop)
    {
        double e = 0.0;
        for (int c = 0; c < a.getNumChannels(); ++c)
            for (int i = 0; i < hop; ++i) { const float x = a.getSample (c, p + i); e += x * x; }
        env.push_back ((float) (10.0 * std::log10 (e / (hop * a.getNumChannels()) + 1e-12)));
    }
    std::vector<int> cuts { s0 };
    const int minGap = (int) (z.sample->rate * 0.06);
    for (size_t k = 1; k < env.size(); ++k)
    {
        float before = env[k - 1];
        if (k >= 3) before = jmax (before, (env[k - 1] + env[k - 2] + env[k - 3]) / 3.0f);
        if (env[k] < threshDb || env[k] - before < rise) continue;
        // refine: the first sample in this hop (or the previous one) that reaches a tenth of the local peak
        const int from = jmax (s0, s0 + (int) (k - 1) * hop), to = jmin (s1, s0 + (int) (k + 2) * hop);
        float pk = 0.0f;
        for (int c = 0; c < a.getNumChannels(); ++c) pk = jmax (pk, a.getMagnitude (c, from, to - from));
        int at = s0 + (int) k * hop;
        for (int i = from; i < to; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < a.getNumChannels(); ++c) m = jmax (m, std::abs (a.getSample (c, i)));
            if (m >= pk * 0.1f) { at = i; break; }
        }
        if (at - cuts.back() >= minGap) cuts.push_back (at);
    }
    const int grid = (int) value ("slgrid");
    if (grid > 0)
    {
        static const int divIdx[] { 0, 4, 7, 10, 13 };   // 1/4, 1/8, 1/16, 1/32 in divisionNames()
        const double g = syncSeconds (divIdx[grid], value ("slbpm")) * z.sample->rate;
        std::vector<int> q;
        for (int c : cuts)
        {
            const int v = s0 + (int) std::round (std::round ((c - s0) / g) * g);
            if (v < s1 && (q.empty() || v > q.back())) q.push_back (v);
        }
        cuts = q;
    }
    edit ([&] (Program& p) { p.slices = cuts; });
}

// ===================================================================== state (zones, slices, optional embedded audio)
void Processor::saveExtraState (ValueTree& v) const
{
    const auto& prog = *live.load();
    ValueTree zones ("ZONES");
    zones.setProperty ("embed", prog.embed, nullptr);
    std::vector<Sample*> samples;
    for (auto& z : prog.zones)
    {
        ValueTree t ("ZONE");
        t.setProperty ("path", z.path, nullptr);
        t.setProperty ("name", z.name, nullptr);
        for (auto [k, val] : std::initializer_list<std::pair<const char*, int>> {
                 { "lowKey", z.lowKey }, { "highKey", z.highKey }, { "lowVel", z.lowVel }, { "highVel", z.highVel }, { "root", z.root },
                 { "start", z.start }, { "end", z.end }, { "loopStart", z.loopStart }, { "loopEnd", z.loopEnd }, { "loopMode", z.loopMode },
                 { "reverse", (int) z.reverse }, { "oneShot", (int) z.oneShot }, { "normalize", (int) z.normalize }, { "choke", z.choke }, { "rr", z.roundRobin } })
            t.setProperty (k, val, nullptr);
        t.setProperty ("tune", z.tune, nullptr); t.setProperty ("gain", z.gainDb, nullptr); t.setProperty ("pan", z.pan, nullptr);
        t.setProperty ("fadeIn", z.fadeInMs, nullptr); t.setProperty ("fadeOut", z.fadeOutMs, nullptr);
        if (z.sample != nullptr)
        {
            auto it = std::find (samples.begin(), samples.end(), z.sample.get());
            if (it == samples.end()) { samples.push_back (z.sample.get()); it = samples.end() - 1; }
            t.setProperty ("sample", (int) (it - samples.begin()), nullptr);
        }
        zones.appendChild (t, nullptr);
    }
    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto* s = samples[i];
        ValueTree t ("SAMPLE");
        t.setProperty ("index", (int) i, nullptr);
        t.setProperty ("name", s->name, nullptr);
        t.setProperty ("builtin", s->builtin, nullptr);
        if (s->builtin < 0 && prog.embed)
        {
            // packed as 32-bit float WAV so it comes back bit for bit
            MemoryBlock mb;
            {
                std::unique_ptr<OutputStream> os = std::make_unique<MemoryOutputStream> (mb, false);
                auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions().withSampleRate (s->rate)
                                                                   .withNumChannels (s->audio.getNumChannels()).withBitsPerSample (32)
                                                                   .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
                if (w != nullptr) w->writeFromAudioSampleBuffer (s->audio, 0, s->length());
            }
            t.setProperty ("wav", mb.toBase64Encoding(), nullptr);
        }
        zones.appendChild (t, nullptr);
    }
    if (! prog.slices.empty())
    {
        StringArray sl;
        for (int c : prog.slices) sl.add (String (c));
        zones.setProperty ("slices", sl.joinIntoString (","), nullptr);
    }
    v.appendChild (zones, nullptr);
}

void Processor::loadExtraState (const ValueTree& v)
{
    auto zones = v.getChildWithName ("ZONES");
    if (! zones.isValid()) return;
    std::map<int, ValueTree> sampleInfo;
    for (auto t : zones) if (t.hasType ("SAMPLE")) sampleInfo[(int) t.getProperty ("index")] = t;

    std::map<int, std::shared_ptr<Sample>> builtins;
    std::vector<Loader::Job> jobs;
    std::map<int, size_t> jobForSample;
    Program p;
    p.embed = (bool) zones.getProperty ("embed", false);
    for (auto t : zones)
    {
        if (! t.hasType ("ZONE")) continue;
        Zone z;
        z.id = newZoneId();
        z.path = t.getProperty ("path").toString();
        z.name = t.getProperty ("name").toString();
        z.lowKey = t.getProperty ("lowKey", 0); z.highKey = t.getProperty ("highKey", 127);
        z.lowVel = t.getProperty ("lowVel", 1); z.highVel = t.getProperty ("highVel", 127); z.root = t.getProperty ("root", 60);
        z.start = t.getProperty ("start", 0); z.end = t.getProperty ("end", -1);
        z.loopStart = t.getProperty ("loopStart", -1); z.loopEnd = t.getProperty ("loopEnd", -1); z.loopMode = t.getProperty ("loopMode", 0);
        z.reverse = (int) t.getProperty ("reverse", 0) != 0; z.oneShot = (int) t.getProperty ("oneShot", 0) != 0;
        z.normalize = (int) t.getProperty ("normalize", 0) != 0;
        z.choke = t.getProperty ("choke", 0); z.roundRobin = t.getProperty ("rr", 0);
        z.tune = t.getProperty ("tune", 0.0f); z.gainDb = t.getProperty ("gain", 0.0f); z.pan = t.getProperty ("pan", 0.0f);
        z.fadeInMs = t.getProperty ("fadeIn", 0.0f); z.fadeOutMs = t.getProperty ("fadeOut", 0.0f);
        const int si = t.getProperty ("sample", -1);
        if (si >= 0 && sampleInfo.count (si) > 0)
        {
            auto& info = sampleInfo[si];
            const int b = info.getProperty ("builtin", -1);
            if (b >= 0)
            {
                if (builtins.count (b) == 0) builtins[b] = makeBuiltin (b);
                z.sample = builtins[b];
            }
            else
            {
                const File f (resolvePath (z.path));
                const String wav = info.getProperty ("wav").toString();
                if (jobForSample.count (si) == 0)
                {
                    Loader::Job j;
                    j.file = f;
                    j.name = info.getProperty ("name").toString();
                    if (wav.isNotEmpty()) j.data.fromBase64Encoding (wav);
                    else if (! f.existsAsFile()) z.missing = true;
                    if (! z.missing) { jobForSample[si] = jobs.size(); jobs.push_back (std::move (j)); }
                }
                if (jobForSample.count (si) > 0) jobs[jobForSample[si]].zoneIds.push_back (z.id);
            }
        }
        else if (z.path.isNotEmpty()) z.missing = true;
        p.zones.push_back (z);
    }
    for (auto& s : StringArray::fromTokens (zones.getProperty ("slices").toString(), ",", {})) if (s.isNotEmpty()) p.slices.push_back (s.getIntValue());
    edit ([&] (Program& target) { target = p; });
    for (auto& j : jobs) loader->add (std::move (j));
}

// ===================================================================== audio
void Processor::prepareDsp (double, int)
{
    volSmooth.prepare (sr, 20.0);
    volSmooth.reset (dsp::dbToGain (value ("volume")));
    racoon::sdsp::SincTable::get();   // the kernels are built here, never on the audio thread
    resetDsp();
}

void Processor::resetDsp()
{
    for (auto& v : voices)
    {
        if (v.active && v.sample) v.sample->voiceRefs--;
        v = SVoice();
    }
    seenSerial = -1;
    activeVoices = 0;
    pedal = false;
    std::fill (std::begin (pedalHeld), std::end (pedalHeld), false);
    keyboard.reset();
}

void Processor::startVoice (const Zone& z, int note, float vel, int sliceStart, int sliceEnd)
{
    const int limit = (int) value ("voices");
    int playing = 0;
    for (auto& v : voices) if (v.active && ! v.stealing) ++playing;
    while (playing >= limit)
    {
        SVoice* victim = nullptr;
        for (auto& v : voices) if (v.active && ! v.stealing && v.released && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) for (auto& v : voices) if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order)) victim = &v;
        if (victim == nullptr) break;
        victim->stealing = true;
        --playing;
    }
    SVoice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr) return;   // every slot fading out; this note is dropped (rare: >144 notes in 5 ms)

    auto& v = *slot;
    v = SVoice();
    v.active = true;
    v.gPrev = -1.0f;
    v.note = note; v.velocity = vel; v.order = ++orderCounter;
    v.zoneId = z.id;
    v.sample = z.sample.get();
    v.sample->voiceRefs++;
    const bool slice = sliceStart >= 0;
    v.sliceVoice = slice;
    v.start = slice ? sliceStart : z.startF();
    v.end = slice ? sliceEnd : z.endF();
    v.loopStart = z.loopStartF(); v.loopEnd = z.loopEndF();
    v.loopMode = slice ? loopOff : z.loopMode;
    v.oneShot = slice ? value ("slgate") < 0.5f : z.oneShot;
    v.choke = z.choke;
    v.gain = dsp::dbToGain (z.gainDb) * (z.normalize ? 1.0f / z.sample->peak : 1.0f);
    v.panL = jmin (1.0f, 1.0f - z.pan); v.panR = jmin (1.0f, 1.0f + z.pan);
    const bool rev = z.reverse != (value ("reverse") > 0.5f);
    v.dir = rev ? -1 : 1;
    const float off = jlimit (0.0f, 0.99f, value ("startoffset") * 0.01f + value ("velstart") * 0.01f * vel);
    const double span = (double) (v.end - v.start);
    v.pos = rev ? (double) v.end - 1.0 - off * span : (double) v.start + off * span;
    const double ms = sr * 0.001;
    v.fadeIn = (float) (jmax ((double) z.fadeInMs, (! slice && (off > 0.0f || v.start > 0 || (rev && v.end < z.len()))) ? 1.0 : 0.0) * ms);   // slices start on their transient
    v.fadeOut = (float) (jmax ((double) z.fadeOutMs, (rev ? v.start > 0 : v.end < z.len()) ? 1.0 : 0.0) * ms);
    v.rootRatio = slice ? 1.0 : std::pow (2.0, ((double) note - z.root + z.tune) / 12.0);
    v.amp.stage = SEnv::attack; v.flt.stage = SEnv::attack;
}

void Processor::noteOn (const Program& prog, int note, float vel)
{
    const int vel127 = jlimit (1, 127, roundToInt (vel * 127.0f));
    if ((int) value ("mode") == 1)
    {
        if (prog.zones.empty() || prog.zones[0].sample == nullptr) return;
        const auto& z = prog.zones[0];
        const int idx = note - 36;
        const auto& cuts = prog.slices;
        if (idx < 0 || idx >= (int) jmax ((size_t) 1, cuts.size())) return;
        const int s0 = cuts.empty() ? z.startF() : cuts[(size_t) idx];
        const int s1 = (size_t) idx + 1 < cuts.size() ? cuts[(size_t) idx + 1] : z.endF();
        for (auto& v : voices) if (v.active && v.sliceVoice && v.note == note) v.stealing = true;   // retrigger the pad
        startVoice (z, note, vel, s0, s1);
        return;
    }

    // choke groups and round robin
    bool rrDone[9] {};
    for (size_t i = 0; i < prog.zones.size(); ++i)
    {
        const auto& z = prog.zones[i];
        if (z.sample == nullptr || note < z.lowKey || note > z.highKey || vel127 < z.lowVel || vel127 > z.highVel) continue;
        if (z.roundRobin > 0)
        {
            const int g = jlimit (1, 8, z.roundRobin);
            if (rrDone[g]) continue;
            rrDone[g] = true;
            int count = 0;
            for (auto& o : prog.zones)
                if (o.sample && o.roundRobin == z.roundRobin && note >= o.lowKey && note <= o.highKey && vel127 >= o.lowVel && vel127 <= o.highVel) ++count;
            const int pick = rrCounter[g]++ % jmax (1, count);
            int k = 0;
            for (auto& o : prog.zones)
                if (o.sample && o.roundRobin == z.roundRobin && note >= o.lowKey && note <= o.highKey && vel127 >= o.lowVel && vel127 <= o.highVel)
                    if (k++ == pick)
                    {
                        if (o.choke > 0) for (auto& v : voices) if (v.active && v.choke == o.choke) v.stealing = true;
                        startVoice (o, note, vel, -1, -1);
                    }
            continue;
        }
        if (z.choke > 0) for (auto& v : voices) if (v.active && v.choke == z.choke) v.stealing = true;
        startVoice (z, note, vel, -1, -1);
    }
}

void Processor::noteOff (int note)
{
    if (pedal) { pedalHeld[note] = true; return; }
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.released && ! v.oneShot)
        {
            v.released = true;
            v.amp.stage = SEnv::release; v.flt.stage = SEnv::release;
        }
}

void Processor::syncVoices (const Program& prog)
{
    for (auto& v : voices)
    {
        if (! v.active || v.stealing) continue;
        const Zone* z = nullptr;
        for (auto& o : prog.zones) if (o.id == v.zoneId) { z = &o; break; }
        if (z == nullptr || z->sample.get() != v.sample) { v.stealing = true; continue; }
        if (! v.sliceVoice)
        {
            v.end = z->endF(); v.start = z->startF();
            v.loopStart = z->loopStartF(); v.loopEnd = z->loopEndF(); v.loopMode = z->loopMode;
            v.gain = dsp::dbToGain (z->gainDb) * (z->normalize ? 1.0f / z->sample->peak : 1.0f);
            v.panL = jmin (1.0f, 1.0f - z->pan); v.panR = jmin (1.0f, 1.0f + z->pan);
        }
    }
}

void Processor::processDsp (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    audioBusy.store (true);
    const Program& prog = *live.load();
    if (prog.serial != seenSerial) { if (seenSerial >= 0) syncVoices (prog); seenSerial = prog.serial; }

    const int n = buffer.getNumSamples();
    const int outCh = jmin (2, buffer.getNumChannels());
    keyboard.processNextMidiBuffer (midi, 0, n, true);
    buffer.clear();

    // per-block parameters
    const float aA = value ("amp_a"), aS = value ("amp_s") * 0.01f;
    const float aD = (float) std::exp (-6.9 / (value ("amp_d") * sr)), aR = (float) std::exp (-6.9 / (jmax (0.0001f, value ("amp_r")) * sr));
    const float fA = value ("flt_a"), fS = value ("flt_s") * 0.01f;
    const float fD = (float) std::exp (-6.9 / (value ("flt_d") * sr)), fR = (float) std::exp (-6.9 / (jmax (0.0001f, value ("flt_r")) * sr));
    const float velAmp = value ("velamp") * 0.01f, velFlt = value ("velflt") * 0.01f;
    const int ftype = (int) value ("ftype");
    const float cutoff = value ("cutoff"), fenv = value ("fenv") * 0.01f * 6.0f;
    const float k = 2.0f - 2.0f * value ("reso") * 0.01f * 0.97f;
    // values that land a hair away from 1x / 0 st after the parameter round trip are snapped, so rate 1 stays bit-perfect
    double rate = value ("rate");
    if (std::abs (rate - 1.0) < 1.0e-4) rate = 1.0;
    double pitchSemis = std::round (value ("pitch")) + value ("fine") * 0.01;
    if (std::abs (pitchSemis) < 1.0e-4) pitchSemis = 0.0;
    const double lfoInc = value ("lforate") / sr;
    const int lfoShape = (int) value ("lfoshape");
    const float lfoPitch = value ("lfopitch") * 0.01f, lfoCut = value ("lfocutoff") * 0.01f * 3.0f, lfoAmp = value ("lfoamp") * 0.01f;
    const float bendRange = value ("bendrange");
    const double xfMs = value ("loopxf");
    volSmooth.target = dsp::dbToGain (value ("volume"));
    const float masterPan = value ("pan") * 0.01f;
    const float mpL = jmin (1.0f, 1.0f - masterPan), mpR = jmin (1.0f, 1.0f + masterPan);
    const float stealStep = (float) (1.0 / (0.005 * sr));
    auto* outL = buffer.getWritePointer (0);
    auto* outR = outCh > 1 ? buffer.getWritePointer (1) : nullptr;

    auto midiIt = midi.begin();
    for (int pos = 0; pos < n; )
    {
        // MIDI at this position
        while (midiIt != midi.end() && (*midiIt).samplePosition <= pos)
        {
            const auto m = (*midiIt).getMessage();
            if (m.isNoteOn()) noteOn (prog, m.getNoteNumber(), (float) m.getVelocity() / 127.0f);   // exact 1.0 at 127
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
            else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            else if (m.isSustainPedalOn()) pedal = true;
            else if (m.isSustainPedalOff())
            {
                pedal = false;
                for (int nn = 0; nn < 128; ++nn) if (pedalHeld[nn]) { pedalHeld[nn] = false; noteOff (nn); }
            }
            else if (m.isAllNotesOff()) { for (int nn = 0; nn < 128; ++nn) noteOff (nn); }
            else if (m.isAllSoundOff()) { for (auto& v : voices) if (v.active) v.stealing = true; }
            ++midiIt;
        }
        const int nextEvent = midiIt != midi.end() ? jlimit (pos + 1, n, (*midiIt).samplePosition) : n;
        const int len = jmin (32, nextEvent - pos);

        // LFO (control rate)
        lfoPhase += lfoInc * len;
        bool wrapped = false;
        if (lfoPhase >= 1.0) { lfoPhase -= std::floor (lfoPhase); wrapped = true; }
        float lfo;
        switch (lfoShape)
        {
            case 0: lfo = (float) std::sin (MathConstants<double>::twoPi * lfoPhase); break;
            case 1: lfo = lfoPhase < 0.5 ? (float) (4.0 * lfoPhase - 1.0) : (float) (3.0 - 4.0 * lfoPhase); break;
            case 2: lfo = lfoPhase < 0.5 ? 1.0f : -1.0f; break;
            default: if (wrapped) lfoHold = lfoRng.nextBipolar(); lfo = lfoHold; break;
        }
        const float trem = 1.0f - lfoAmp * (0.5f + 0.5f * lfo);

        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& smp = *v.sample;
            const int slen = smp.length();
            const float* dl = smp.audio.getReadPointer (0);
            const float* dr = smp.audio.getReadPointer (jmin (1, smp.audio.getNumChannels() - 1));
            double semis = pitchSemis + bend * bendRange + lfo * lfoPitch;
            if (std::abs (semis) < 1.0e-6) semis = 0.0;
            v.inc = (smp.rate / sr) * rate * v.rootRatio * (semis == 0.0 ? 1.0 : std::pow (2.0, semis / 12.0));
            const bool loop = v.loopMode != loopOff;
            const int ls = v.loopStart, le = v.loopEnd, loopLen = le - ls;
            // crossfade length in sample frames, and how far the loop has to be shortened when there is no room before it
            double xf = 0.0, shift = 0.0;
            if (v.loopMode == loopForward && xfMs > 0.0)
            {
                xf = jmin (xfMs * 0.001 * smp.rate, loopLen * 0.5);
                const double room = v.dir > 0 ? (double) (ls - v.start) : (double) (v.end - le);
                shift = jmax (0.0, xf - room);
            }
            const double jump = loopLen - shift;
            float g = 0.0f;
            const bool filt = ftype > 0;
            if (filt)
            {
                const float fe = v.flt.level;
                const float oct = fenv * fe + lfoCut * lfo + velFlt * (v.velocity - 0.7f) * 3.0f;
                g = dsp::SVF::gFor (jlimit (20.0f, (float) (sr * 0.45), cutoff * std::pow (2.0f, oct)), sr);
            }
            const float vg = (1.0f - velAmp * (1.0f - v.velocity)) * v.gain * trem;
            // the cutoff glides sample by sample from the last control step to this one (was a stair every 32 samples)
            const float gFrom = (filt && v.gPrev > 0.0f) ? v.gPrev : g, gInc = (g - gFrom) / (float) jmax (1, len);
            // windowed-sinc reads; pitched up, the kernel closes in so the octave-up copies do not fold back
            const auto& sinc = racoon::sdsp::SincTable::get();
            const int band = racoon::sdsp::SincTable::bandFor (std::abs (v.inc));
            float gNow = gFrom;
            if (filt) v.gPrev = g;
            for (int i = pos; i < pos + len; ++i)
            {
                gNow += gInc;
                const double p = v.pos;
                float sl, sr2;
                sinc.read2 (dl, dr, slen, p, band, sl, sr2);
                if (xf > 0.0)
                {
                    double w = -1.0, q = 0.0;
                    if (v.dir > 0 && p > le - xf) { w = (p - (le - xf)) / xf; q = p - jump; }
                    else if (v.dir < 0 && p < ls + xf) { w = ((ls + xf) - p) / xf; q = p + jump; }
                    if (w >= 0.0)
                    {
                        // equal power: the loop seam keeps its loudness on material that does not line up
                        float go, gi, ql, qr;
                        racoon::sdsp::equalPower ((float) jmin (1.0, w), go, gi);
                        sinc.read2 (dl, dr, slen, q, band, ql, qr);
                        sl = sl * go + ql * gi;
                        sr2 = sr2 * go + qr * gi;
                    }
                }
                const float a = v.amp.next (aA, aD, aS, aR, sr);
                v.flt.next (fA, fD, fS, fR, sr);
                float gain = a * vg;
                if (v.fadeIn > 0.0f && v.played < v.fadeIn) gain *= (float) (v.played / v.fadeIn);
                if (! loop && v.fadeOut > 0.0f)
                {
                    const double left = (v.dir > 0 ? v.end - p : p - v.start) / jmax (1.0e-6, v.inc);
                    if (left < v.fadeOut) gain *= (float) jmax (0.0, left / v.fadeOut);
                }
                if (v.stealing)
                {
                    v.stealGain -= stealStep;
                    if (v.stealGain <= 0.0f) v.stealGain = 0.0f;
                    gain *= v.stealGain;
                }
                if (filt)
                {
                    float y[2] { sl, sr2 };
                    for (int c = 0; c < 2; ++c)
                    {
                        v.f[c].process (y[c], gNow, k);
                        y[c] = ftype == 1 ? v.f[c].lp : ftype == 2 ? v.f[c].bp * k : v.f[c].hp;
                    }
                    sl = y[0]; sr2 = y[1];
                }
                if (outR != nullptr) { outL[i] += sl * gain * v.panL; outR[i] += sr2 * gain * v.panR; }
                else outL[i] += (sl * v.panL + sr2 * v.panR) * 0.5f * gain;

                // advance
                v.played += 1.0;
                v.pos += v.inc * v.dir;
                bool finished = false;
                if (loop)
                {
                    if (v.loopMode == loopForward)
                    {
                        if (v.dir > 0 && v.pos >= le) v.pos -= jump;
                        else if (v.dir < 0 && v.pos < ls) v.pos += jump;
                    }
                    else
                    {
                        if (v.dir > 0 && v.pos > le - 1) { v.pos = 2.0 * (le - 1) - v.pos; v.dir = -1; }
                        else if (v.dir < 0 && v.pos < ls) { v.pos = 2.0 * ls - v.pos; v.dir = 1; }
                    }
                }
                else if (v.pos >= v.end || v.pos < v.start) finished = true;
                if (finished || (v.stealing && v.stealGain <= 0.0f) || v.amp.stage == SEnv::idle)
                {
                    v.active = false;
                    v.sample->voiceRefs--;
                    break;
                }
            }
        }
        pos += len;
    }

    // master volume and pan
    for (int i = 0; i < n; ++i)
    {
        const float vol = volSmooth.next();
        outL[i] *= vol * mpL;
        if (outR != nullptr) outR[i] *= vol * mpR;
    }

    int count = 0, shown = 0;
    for (auto& v : voices)
        if (v.active && ! v.stealing)
        {
            ++count;
            if (shown < 4) playhead[shown++].store ((float) (v.pos / jmax (1, v.sample->length())));
        }
    for (int i = shown; i < 4; ++i) playhead[i].store (-1.0f);
    activeVoices.store (count);
    blocksDone++;
    audioBusy.store (false);
}

// ===================================================================== presets
// ===================================================================== SFZ y Muestras Racoon
namespace
{
void setReal (AudioProcessorValueTreeState& s, const String& id, float v)
{
    if (auto* p = dynamic_cast<RangedAudioParameter*> (s.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

int sfzKey (const String& v, int fallback)
{
    auto t = v.trim();
    if (t.isEmpty()) return fallback;
    if (t.containsOnly ("0123456789-")) return jlimit (0, 127, t.getIntValue());
    // nombre de nota: c4, c#4, db4 (C4 = 60)
    static const int base[] { 9, 11, 0, 2, 4, 5, 7 };     // a b c d e f g
    const auto c = (char) CharacterFunctions::toLowerCase (t[0]);
    if (c < 'a' || c > 'g') return fallback;
    int n = base[c - 'a'];
    int i = 1;
    if (t[i] == '#') { ++n; ++i; }
    else if (t[i] == 'b') { --n; ++i; }
    return jlimit (0, 127, (t.substring (i).getIntValue() + 1) * 12 + n);
}

// las opciones de una línea: "sample=a b.wav lokey=40" -> {sample: "a b.wav", lokey: "40"}
void opcodes (const String& line, NamedValueSet& out)
{
    struct P { int key, eq; };
    std::vector<P> ps;
    for (int i = 0; i < line.length(); ++i)
        if (line[i] == '=')
        {
            int k = i - 1;
            while (k >= 0 && (CharacterFunctions::isLetterOrDigit (line[k]) || line[k] == '_' || line[k] == '$')) --k;
            if (k + 1 < i) ps.push_back ({ k + 1, i });
        }
    for (size_t j = 0; j < ps.size(); ++j)
    {
        const auto name = line.substring (ps[j].key, ps[j].eq);
        const int end = j + 1 < ps.size() ? ps[j + 1].key : line.length();
        out.set (Identifier (name), line.substring (ps[j].eq + 1, end).trim());
    }
}
}

String Processor::libraryPath (const File& f)
{
    const auto root = racoon::biblio::Catalogo::get().raiz;
    if (root.exists() && f.isAChildOf (root)) return "biblioteca:" + f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    return f.getFullPathName();
}

File Processor::resolvePath (const String& p)
{
    if (p.startsWith ("biblioteca:")) return racoon::biblio::Catalogo::get().archivo (p.fromFirstOccurrenceOf ("biblioteca:", false, false));
    return File (p);
}

int Processor::loadSfz (const File& sfz, bool replace)
{
    const auto text = sfz.loadFileAsString();
    if (text.isEmpty()) return 0;
    NamedValueSet control, global, master, group, region;
    int level = 0;                         // 0 nada, 1 control, 2 global, 3 master, 4 group, 5 region
    std::vector<NamedValueSet> regions;
    auto flush = [&]
    {
        if (level != 5) return;
        NamedValueSet r;
        for (auto* set : { &global, &master, &group, &region })
            for (auto& nv : *set) r.set (nv.name, nv.value);
        if (r.contains ("sample")) regions.push_back (r);
    };
    for (auto line : StringArray::fromLines (text))
    {
        line = line.upToFirstOccurrenceOf ("//", false, false);
        // un renglón puede traer varios encabezados: "<region> sample=... <region> ..."
        while (line.isNotEmpty())
        {
            const int lt = line.indexOfChar ('<');
            const auto before = lt < 0 ? line : line.substring (0, lt);
            NamedValueSet* target = level == 1 ? &control : level == 2 ? &global : level == 3 ? &master : level == 4 ? &group : level == 5 ? &region : nullptr;
            if (target != nullptr && before.trim().isNotEmpty()) opcodes (before, *target);
            if (lt < 0) break;
            const int gt = line.indexOfChar (lt, '>');
            if (gt < 0) break;
            const auto header = line.substring (lt + 1, gt).trim().toLowerCase();
            flush();
            if (header == "control") { level = 1; control.clear(); }
            else if (header == "global") { level = 2; global.clear(); master.clear(); group.clear(); }
            else if (header == "master") { level = 3; master.clear(); group.clear(); }
            else if (header == "group") { level = 4; group.clear(); }
            else if (header == "region") { level = 5; region.clear(); }
            else level = 0;
            line = line.substring (gt + 1);
        }
    }
    flush();
    if (regions.empty()) return 0;
    const auto base = sfz.getParentDirectory().getChildFile (control.getWithDefault ("default_path", "").toString().replaceCharacter ('\\', '/'));
    Program p;
    if (! replace) p = program();
    else p.embed = program().embed;
    if (replace) { p.zones.clear(); p.slices.clear(); }
    std::map<String, std::vector<uint32>> zonesForFile;
    float release = -1.0f;
    for (auto& r : regions)
    {
        if (r.getWithDefault ("trigger", "attack").toString() == "release") continue;
        if ((int) r.getWithDefault ("seq_position", 1) > 1) continue;      // las alternancias extra no caben como zonas fijas
        const auto file = base.getChildFile (r["sample"].toString().replaceCharacter ('\\', '/'));
        Zone z;
        z.id = newZoneId();
        const int key = sfzKey (r.getWithDefault ("key", "").toString(), -1);
        z.lowKey = sfzKey (r.getWithDefault ("lokey", "").toString(), key >= 0 ? key : 0);
        z.highKey = sfzKey (r.getWithDefault ("hikey", "").toString(), key >= 0 ? key : 127);
        z.root = sfzKey (r.getWithDefault ("pitch_keycenter", "").toString(), key >= 0 ? key : 60);
        z.lowVel = jlimit (1, 127, (int) r.getWithDefault ("lovel", 1));
        z.highVel = jlimit (z.lowVel, 127, (int) r.getWithDefault ("hivel", 127));
        z.tune = (float) (double) r.getWithDefault ("tune", 0.0) + 100.0f * (float) (int) r.getWithDefault ("transpose", 0);
        z.gainDb = (float) (double) r.getWithDefault ("volume", 0.0);
        z.pan = jlimit (-1.0f, 1.0f, (float) (double) r.getWithDefault ("pan", 0.0) / 100.0f);
        z.start = (int) r.getWithDefault ("offset", 0);
        if (r.contains ("end")) z.end = (int) r["end"];
        const auto lm = r.getWithDefault ("loop_mode", r.getWithDefault ("loopmode", "")).toString();
        if (lm == "loop_continuous" || lm == "loop_sustain")
        {
            z.loopMode = loopForward;
            z.loopStart = (int) r.getWithDefault ("loop_start", r.getWithDefault ("loopstart", -1));
            z.loopEnd = (int) r.getWithDefault ("loop_end", r.getWithDefault ("loopend", -1));
        }
        z.oneShot = lm == "one_shot";
        z.path = libraryPath (file);
        z.name = file.getFileNameWithoutExtension();
        if (! file.existsAsFile()) z.missing = true;
        else zonesForFile[file.getFullPathName()].push_back (z.id);
        if (r.contains ("ampeg_release")) release = jmax (release, (float) (double) r["ampeg_release"]);
        p.zones.push_back (z);
    }
    edit ([&] (Program& target) { target = p; });
    for (auto& [path, ids] : zonesForFile)
    {
        Loader::Job j;
        j.file = File (path);
        j.zoneIds = ids;
        loader->add (std::move (j));
    }
    if (release > 0.0f) setReal (apvts, "amp_r", jlimit (0.01f, 10.0f, release));
    return (int) p.zones.size();
}

bool Processor::loadLibraryInstrument (const String& id)
{
    const auto& cat = racoon::biblio::Catalogo::get();
    const auto* in = cat.instrumento (id);
    if (in == nullptr) return false;
    if (loadSfz (cat.archivo (in->archivo)) <= 0) return false;
    setReal (apvts, "mode", 0.0f);
    setReal (apvts, "amp_a", 0.002f);
    setReal (apvts, "amp_d", 1.0f);
    setReal (apvts, "amp_s", 100.0f);
    setReal (apvts, "velamp", 75.0f);
    setReal (apvts, "loopxf", in->bucle ? 40.0f : 20.0f);
    return true;
}

bool Processor::loadLibraryGroup (const String& grupo)
{
    const auto& cat = racoon::biblio::Catalogo::get();
    const auto* g = cat.grupo (grupo);
    if (g == nullptr) return false;
    Program p;
    p.embed = program().embed;
    std::map<String, std::vector<uint32>> files;
    for (int idx : g->muestras)
    {
        const auto& m = cat.muestras[(size_t) idx];
        if (m.rr > 1) continue;
        Zone z;
        z.id = newZoneId();
        z.root = 60;
        z.lowVel = 1 + ((m.capa - 1) * 127) / jmax (1, g->capas);
        z.highVel = (m.capa * 127) / jmax (1, g->capas);
        z.oneShot = m.dur < 6.0f;
        const auto f = cat.archivo (m.archivo);
        z.path = libraryPath (f);
        z.name = m.nombre;
        files[f.getFullPathName()].push_back (z.id);
        p.zones.push_back (z);
    }
    if (p.zones.empty()) return false;
    edit ([&] (Program& target) { target = p; });
    for (auto& [path, ids] : files) { Loader::Job j; j.file = File (path); j.zoneIds = ids; loader->add (std::move (j)); }
    setReal (apvts, "mode", 0.0f);
    return true;
}

bool Processor::loadLibraryKit (const String& id)
{
    const auto& cat = racoon::biblio::Catalogo::get();
    const auto* k = cat.kit (id);
    if (k == nullptr) return false;
    Program p;
    p.embed = program().embed;
    std::map<String, std::vector<uint32>> files;
    for (auto& pista : k->pistas)
    {
        const auto* g = cat.grupo (pista.grupo);
        if (g == nullptr) continue;
        const int note = 36 + pista.pista;
        for (int idx : g->muestras)
        {
            const auto& m = cat.muestras[(size_t) idx];
            if (m.rr > 1) continue;
            Zone z;
            z.id = newZoneId();
            z.lowKey = z.highKey = z.root = note;
            z.lowVel = 1 + ((m.capa - 1) * 127) / jmax (1, g->capas);
            z.highVel = (m.capa * 127) / jmax (1, g->capas);
            z.oneShot = true;
            // los hi-hats se cortan entre sí, como en una batería
            if (g->cat.startsWith ("hihat")) z.choke = 1;
            const auto f = cat.archivo (m.archivo);
            z.path = libraryPath (f);
            z.name = m.nombre;
            files[f.getFullPathName()].push_back (z.id);
            p.zones.push_back (z);
        }
    }
    if (p.zones.empty()) return false;
    edit ([&] (Program& target) { target = p; });
    for (auto& [path, ids] : files) { Loader::Job j; j.file = File (path); j.zoneIds = ids; loader->add (std::move (j)); }
    setReal (apvts, "mode", 0.0f);
    setReal (apvts, "amp_r", 0.3f);
    return true;
}

std::vector<FactoryPreset> Processor::factoryPresets() const
{
    return {
        { "Inicial", "Básicos"_u, {} },
        { "Tecla suave", "Básicos"_u, { { "amp_a", 0.005f }, { "amp_r", 0.4f }, { "velamp", 70 } } },
        { "Octava arriba", "Básicos"_u, { { "pitch", 12 }, { "amp_r", 0.3f } } },
        { "Pad en loop", "Texturas", { { "amp_a", 0.8f }, { "amp_r", 2.5f }, { "ftype", 1 }, { "cutoff", 3000 }, { "lfoamp", 15 }, { "lforate", 0.5f }, { "loopxf", 120 } } },
        { "Filtro que respira", "Texturas", { { "ftype", 1 }, { "cutoff", 1200 }, { "reso", 35 }, { "lfocutoff", 50 }, { "lforate", 0.3f }, { "amp_a", 0.3f }, { "amp_r", 1.5f } } },
        { "Cinta lenta", "Texturas", { { "rate", 0.5f }, { "lfopitch", 12 }, { "lforate", 0.6f }, { "ftype", 1 }, { "cutoff", 5000 } } },
        { "Pluck", "Creativos", { { "amp_d", 0.35f }, { "amp_s", 0 }, { "amp_r", 0.2f }, { "ftype", 1 }, { "cutoff", 900 }, { "fenv", 45 }, { "flt_d", 0.25f } } },
        { "Reverso", "Creativos", { { "reverse", 1 }, { "amp_a", 0.2f }, { "amp_r", 0.6f } } },
        { "Vibrato", "Creativos", { { "lfopitch", 25 }, { "lforate", 5.5f } } },
        { "Cortes libres", "Cortes", { { "mode", 1 }, { "slgrid", 0 }, { "slgate", 0 } } },
        { "Cortes a la 16", "Cortes", { { "mode", 1 }, { "slgrid", 3 }, { "slgate", 0 } } },
        { "Cortes con gate", "Cortes", { { "mode", 1 }, { "slgate", 1 }, { "amp_r", 0.02f } } },
    };
}

// ===================================================================== example for Play
std::vector<SeqNote> Processor::demoPattern() const
{
    return { { 0.0, 0.9, 60, 100, 1 },
             { 1.0, 0.9, 64, 100, 1 },
             { 2.0, 0.9, 67, 100, 1 },
             { 3.0, 0.9, 72, 100, 1 },
             { 4.0, 0.9, 71, 100, 1 },
             { 5.0, 0.9, 67, 100, 1 },
             { 6.0, 0.9, 64, 100, 1 },
             { 7.0, 0.9, 62, 100, 1 } };
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

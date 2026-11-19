#include "Stems.h"
#include "StemsCore.h"

namespace racoon::stems
{
using namespace juce;

namespace
{
// the model's order is drums, bass, other, vocals; Racoon shows the voice first
struct StemInfo { int index; const char* name; const char* file; uint32 colour; };
const StemInfo order[] { { 3, "Voz", "voz", 0xffe0789a }, { 0, "Batería", "bateria", 0xffd9c452 },
                         { 1, "Bajo", "bajo", 0xff7fbfa8 }, { 2, "Otros", "otros", 0xff5cf2e8 } };

std::atomic<bool> busy { false }, cancelFlag { false }, fetching { false };

// read the whole source file as stereo float at its own rate
bool readFile (const File& f, AudioBuffer<float>& out, double& rate, String& err)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { err = "No se puede leer " + f.getFileName(); return false; }
    if (r->lengthInSamples > (int64) r->sampleRate * 60 * 20) { err = String::fromUTF8 ("El audio dura más de 20 minutos"); return false; }
    rate = r->sampleRate;
    out.setSize (2, (int) r->lengthInSamples);
    r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
    if (r->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, out.getNumSamples());
    return true;
}

// sample-rate conversion with JUCE's windowed sinc (only when the file is not at 44.1 kHz)
std::vector<float> resample (const float* in, int n, double from, double to)
{
    if (std::abs (from - to) < 0.5) return std::vector<float> (in, in + n);
    const double ratio = from / to;
    const int outN = (int) std::ceil (n / ratio);
    std::vector<float> padded (in, in + n);
    padded.resize ((size_t) n + 64, 0.0f);
    std::vector<float> out ((size_t) outN, 0.0f);
    WindowedSincInterpolator interp;
    interp.process (ratio, padded.data(), out.data(), outN);
    return out;
}

struct Output { bool ok = false; String message; Array<File> files; };

// the heavy part, any thread
Output separateFile (const File& source, const File& folder, std::function<void (float)> progress)
{
    Output o;
    if (! modelReady()) { o.message = String::fromUTF8 ("Falta el modelo de separación"); return o; }
    AudioBuffer<float> buf;
    double rate = 44100.0;
    if (! readFile (source, buf, rate, o.message)) return o;
    const int n = buf.getNumSamples();
    auto l = resample (buf.getReadPointer (0), n, rate, stems::sampleRate);
    auto r = resample (buf.getReadPointer (1), n, rate, stems::sampleRate);
    const int threads = jlimit (1, 4, SystemStats::getNumCpus());   // each piece keeps its own buffers: four at most, for 8 GB Macs
    auto res = stems::separate (modelFile().getFullPathName().toStdString(), l, r, threads, progress, &cancelFlag);
    if (! res.ok) { o.message = String::fromUTF8 (res.error.c_str()); return o; }

    folder.createDirectory();
    const auto base = source.getFileNameWithoutExtension();
    for (auto& s : order)
    {
        AudioBuffer<float> st (2, n);
        for (int c = 0; c < 2; ++c)
        {
            auto back = resample (res.out[s.index][c].data(), (int) res.out[s.index][c].size(), stems::sampleRate, rate);
            st.copyFrom (c, 0, back.data(), jmin (n, (int) back.size()));
        }
        auto f = folder.getChildFile (base + " - " + s.file + ".wav").getNonexistentSibling();
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), rate, 2, 24, {}, 0));
        if (w == nullptr || ! w->writeFromAudioSampleBuffer (st, 0, n)) { o.message = "No se pudo escribir " + f.getFileName(); return o; }
        o.files.add (f);
    }
    o.ok = true;
    return o;
}

// message thread: four channels with the stems, each clip where the original is and with its settings
Array<te::WaveAudioClip*> place (Session& s, te::WaveAudioClip& clip, const Array<File>& files)
{
    Array<te::WaveAudioClip*> made;
    auto& um = s.getEdit().getUndoManager();
    um.beginNewTransaction (String::fromUTF8 ("Separar en stems"));
    const auto name = clip.getName();
    for (int k = 0; k < 4 && k < files.size(); ++k)
    {
        auto* t = s.addAudioTrack (String::fromUTF8 (order[k].name) + String::fromUTF8 (" · ") + name);
        if (t == nullptr) continue;
        t->setColour (Colour (order[k].colour));
        auto c = t->insertWaveClip (name + " " + String::fromUTF8 (order[k].name), files[k], clip.getPosition(), false);
        if (c == nullptr) continue;
        // the same timing and sound settings as the original: tempo following, warp markers, gain, pitch, fades
        for (auto id : { te::IDs::LOOPINFO, te::IDs::WARPTIME })
        {
            auto from = clip.state.getChildWithName (id);
            if (! from.isValid()) continue;
            auto old = c->state.getChildWithName (id);
            if (old.isValid()) c->state.removeChild (old, nullptr);
            c->state.appendChild (from.createCopy(), nullptr);
        }
        c->setAutoTempo (clip.getAutoTempo());
        c->setWarpTime (clip.getWarpTime());
        c->setGainDB (clip.getGainDB());
        c->setPitchChange (clip.getPitchChange());
        c->setIsReversed (clip.getIsReversed());
        c->setFadeIn (clip.getFadeIn());
        c->setFadeOut (clip.getFadeOut());
        made.add (c.get());
    }
    // the original stays, silenced: the four stems play in its place
    if (made.size() == 4) clip.setMuted (true);
    return made;
}
}

File modelFile()
{
    const auto env = SystemStats::getEnvironmentVariable ("RACOON_STEMS_MODEL", {});
    if (env.isNotEmpty()) return File (env);
    return Session::projectsFolder().getChildFile ("Modelos").getChildFile ("ggml-model-htdemucs-4s-f16.bin");
}

bool modelReady() { return stems::looksLikeModel (modelFile().getFullPathName().toStdString()); }

URL modelUrl() { return URL ("https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-4s-f16.bin"); }

bool downloading() { return fetching.load(); }

void downloadModel (std::function<void (float)> progress, std::function<void (bool, const String&)> done)
{
    if (fetching.exchange (true)) return;
    std::thread ([progress, done]
    {
        auto dest = modelFile();
        dest.getParentDirectory().createDirectory();
        auto tmp = dest.getSiblingFile (dest.getFileName() + ".descargando");
        tmp.deleteFile();
        bool ok = false;
        String msg;
        int status = 0;
        if (auto in = modelUrl().createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                        .withConnectionTimeoutMs (20000).withStatusCode (&status).withNumRedirectsToFollow (8)))
        {
            const auto total = in->getTotalLength();
            FileOutputStream os (tmp);
            if (os.openedOk())
            {
                HeapBlock<char> chunk (1 << 16);
                int64 got = 0;
                double lastShown = 0.0;
                while (! in->isExhausted())
                {
                    const int r = in->read (chunk, 1 << 16);
                    if (r <= 0) break;
                    os.write (chunk, (size_t) r);
                    got += r;
                    const double now = Time::getMillisecondCounterHiRes();
                    if (total > 0 && now - lastShown > 250.0)
                    {
                        lastShown = now;
                        const float p = (float) got / (float) total;
                        MessageManager::callAsync ([progress, p] { if (progress) progress (p); });
                    }
                }
                os.flush();
            }
        }
        else msg = String::fromUTF8 ("Sin conexión con huggingface.co");
        if (stems::looksLikeModel (tmp.getFullPathName().toStdString()) && tmp.moveFileTo (dest)) ok = true;
        else
        {
            tmp.deleteFile();
            if (msg.isEmpty()) msg = String::fromUTF8 ("La descarga no se completó") + (status != 0 ? " (HTTP " + String (status) + ")" : String());
        }
        fetching = false;
        MessageManager::callAsync ([done, ok, msg] { if (done) done (ok, ok ? String::fromUTF8 ("Modelo de separación listo") : msg); });
    }).detach();
}

bool running() { return busy.load(); }
void cancel() { cancelFlag = true; }

bool start (Session& s, te::WaveAudioClip& clip, std::function<void (const String&)> status, std::function<void (bool, const String&)> done)
{
    if (busy.exchange (true)) return false;
    cancelFlag = false;
    const auto source = clip.getAudioFile().getFile();
    const auto folder = s.audioFolder().getChildFile ("Stems");
    te::WaveAudioClip::Ptr keep (&clip);
    Session* session = &s;
    std::thread ([=]
    {
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto out = separateFile (source, folder, [status, t0] (float p)
        {
            static double last = 0.0;
            const double now = Time::getMillisecondCounterHiRes();
            if (now - last < 400.0 && p < 1.0f) return;
            last = now;
            String line = String::fromUTF8 ("Separando en stems: ") + String (roundToInt (p * 100.0f)) + " %";
            if (p > 0.03f)
            {
                const double left = (now - t0) / 1000.0 * (1.0 - p) / p;
                line << String::fromUTF8 ("  ·  faltan ") << (left < 90.0 ? String (roundToInt (left)) + " s" : String (roundToInt (left / 60.0)) + " min");
            }
            MessageManager::callAsync ([status, line] { if (status) status (line); });
        });
        MessageManager::callAsync ([=]
        {
            busy = false;
            if (! out.ok) { if (done) done (false, out.message); return; }
            if (keep->state.getParent().isValid())
            {
                auto made = place (*session, *keep, out.files);
                if (done) done (made.size() == 4, made.size() == 4 ? String::fromUTF8 ("Listo: Voz, Batería, Bajo y Otros en cuatro canales nuevos. El clip original quedó apagado.")
                                                                      : String::fromUTF8 ("No se pudieron crear los canales"));
            }
            else if (done) done (false, String::fromUTF8 ("El clip ya no está en la canción"));
        });
    }).detach();
    return true;
}

Array<te::WaveAudioClip*> separateNow (Session& s, te::WaveAudioClip& clip, String& message)
{
    cancelFlag = false;
    auto out = separateFile (clip.getAudioFile().getFile(), s.audioFolder().getChildFile ("Stems"), {});
    if (! out.ok) { message = out.message; return {}; }
    auto made = place (s, clip, out.files);
    message = made.size() == 4 ? "ok" : "canales";
    return made;
}
}

// Chinchilla checks (included by Tests/FamilyTest.cpp)
#include "ChinchillaProcessor.h"
#include "ChinchillaEditor.h"

namespace chinchilla_test
{
using namespace juce;
using namespace racoon::chinchilla;
constexpr double sr = 48000.0;

struct Head : AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Note { double at, len; int note; };

// a voice-like test signal: a sawtooth with two vowel resonances, at a frequency that can change over time
static AudioBuffer<float> voice (std::function<double (double)> hzAt, double seconds, float level = 0.3f)
{
    AudioBuffer<float> b (2, (int) (seconds * sr));
    racoon::chinchilla::Biquad f1, f2;
    f1.set (racoon::chinchilla::Biquad::bandpass, sr, 700.0, 4.0);
    f2.set (racoon::chinchilla::Biquad::bandpass, sr, 1200.0, 5.0);
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        ph += hzAt (i / sr) / sr;
        if (ph >= 1.0) ph -= 1.0;
        const float saw = (float) (2.0 * ph - 1.0);
        const float v = (saw * 0.35f + f1.process (saw) * 1.2f + f2.process (saw) * 0.7f) * level;
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static std::unique_ptr<Processor> make (std::function<void (Processor&)> setup = {}, Head* head = nullptr)
{
    auto p = std::make_unique<Processor>();
    if (head != nullptr) p->setPlayHead (head);
    p->setRateAndBufferSizeDetails (sr, 256);
    p->prepareToPlay (sr, 256);
    p->setCurrentProgram (0);
    // only what the test asks for
    p->setRealValue ("h_on", 0.0f); p->setRealValue ("retune", 0.0f); p->setRealValue ("humanize", 0.0f); p->setRealValue ("amount", 100.0f);
    p->setRealValue ("cl_hp", 20.0f); p->setRealValue ("cl_gate", -90.0f);
    if (setup) setup (*p);
    p->prepareToPlay (sr, 256);
    return p;
}

static AudioBuffer<float> run (Processor& p, const AudioBuffer<float>& in, const std::vector<Note>& notes = {}, Head* head = nullptr, int block = 256)
{
    AudioBuffer<float> out (2, in.getNumSamples()), io (2, block);
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = jmin (block, in.getNumSamples() - pos);
        io.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, in, c, pos, n);
        MidiBuffer m;
        for (auto& e : notes)
        {
            const int on = (int) (e.at * sr), off = (int) ((e.at + e.len) * sr);
            if (on >= pos && on < pos + n) m.addEvent (MidiMessage::noteOn (1, e.note, (uint8) 100), on - pos);
            if (off >= pos && off < pos + n) m.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
        }
        p.processBlock (io, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, io, c, 0, n);
        if (head != nullptr) head->ppq += n / sr * head->bpm / 60.0;
    }
    return out;
}

static double pitchOf (const AudioBuffer<float>& b, double from, double len, int ch = 0)
{
    const int start = (int) (from * sr), count = (int) (len * sr);
    const auto* x = b.getReadPointer (ch, start);
    const int minLag = (int) (sr / 1000.0), maxLag = (int) (sr / 70.0);
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < count; ++i) s += (double) x[i] * x[i + lag];
        ac[(size_t) lag] = s; best = jmax (best, s);
    }
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (best > 0.0 && ac[(size_t) lag] >= 0.85 * best && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1])
        {
            const double a = ac[(size_t) lag - 1], c = ac[(size_t) lag], d = ac[(size_t) lag + 1];
            return sr / (lag + 0.5 * (a - d) / (a - 2.0 * c + d));
        }
    return 0.0;
}

static double cents (double a, double b) { return a > 0.0 && b > 0.0 ? 1200.0 * std::log2 (a / b) : 9999.0; }
static float tone (const AudioBuffer<float>& b, double from, double hz) { return tu::tone (b, 0, (int) (from * sr), 16384, hz, sr); }

static double centroid (const AudioBuffer<float>& b, double from)
{
    double num = 0.0, den = 0.0;
    for (double hz = 100.0; hz < 6000.0; hz += 50.0) { const double m = tu::tone (b, 0, (int) (from * sr), 8192, hz, sr); num += m * hz; den += m; }
    return den > 0.0 ? num / den : 0.0;
}
}

static void testChinchilla (const juce::File& out)
{
    using namespace juce;
    using namespace chinchilla_test;
    commonChecks ("Chinchilla", [] { return new Processor(); }, out);
    std::cout << "voz" << std::endl;

    // pitch detection
    {
        bool ok = true;
        String got;
        for (double hz : { 110.0, 220.0, 440.0 })
        {
            Yin y;
            y.prepare (sr);
            auto v = voice ([hz] (double) { return hz; }, 0.5);
            float last = 0.0f;
            for (int i = 0; i < v.getNumSamples(); ++i) if (y.push (v.getSample (0, i))) last = y.hz();
            got << String (last, 1) << " ";
            ok = ok && std::abs (cents (last, hz)) < 10.0;
        }
        check (ok, "detecta la afinación de una voz a 110, 220 y 440 Hz ("_u + got.trim() + ")");
    }

    // pitch correction: 225 Hz goes to A (220), chromatic, hard
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("scale", 0.0f); });
        auto o = run (*p, voice ([] (double) { return 225.0; }, 1.5));
        const double f = pitchOf (o, 0.8, 0.4);
        check (std::abs (cents (f, 220.0)) < 8.0, "afinación: 225 Hz sale en "_u + String (f, 1) + " Hz (La)"_u);
    }

    // the scale decides: A# + 36 cents in C major goes to B
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("scale", 1.0f); q.setRealValue ("key", 0.0f); });
        auto o = run (*p, voice ([] (double) { return 238.0; }, 1.5));
        const double f = pitchOf (o, 0.8, 0.4);
        check (std::abs (cents (f, 246.94)) < 10.0, "escala de Do mayor: 238 Hz va a Si (" + String (f, 1) + " Hz)");
    }

    // transpose an octave; formants move without moving the note
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("transpose", 12.0f); });
        auto o = run (*p, voice ([] (double) { return 165.0; }, 1.5));
        const double f = pitchOf (o, 0.8, 0.4);
        auto a = make ([] (Processor& q) { q.setRealValue ("tune_on", 0.0f); q.setRealValue ("formant", 0.0f); });
        auto b = make ([] (Processor& q) { q.setRealValue ("tune_on", 0.0f); q.setRealValue ("formant", 5.0f); });
        auto oa = run (*a, voice ([] (double) { return 165.0; }, 1.5)), ob = run (*b, voice ([] (double) { return 165.0; }, 1.5));
        const double ca = centroid (oa, 0.8), cb = centroid (ob, 0.8), fb = pitchOf (ob, 0.8, 0.4);
        check (std::abs (cents (f, 330.0)) < 10.0 && cb > ca * 1.07 && std::abs (cents (fb, 165.0)) < 15.0,
               "transponer +12 da " + String (f, 1) + " Hz; formantes +5 suben el color (" + String (ca, 0) + " a " + String (cb, 0) + " Hz) con la misma nota (" + String (fb, 1) + " Hz)");
    }

    // harmonies: a fifth above, and a voice on the MIDI note
    {
        auto p = make ([] (Processor& q)
        {
            q.setRealValue ("h_on", 1.0f); q.setRealValue ("h_mix", 100.0f);
            for (int v = 0; v < numHarmonies; ++v) q.setRealValue (Processor::hId (v, "on"), 0.0f);
            q.setRealValue (Processor::hId (0, "on"), 1.0f); q.setRealValue (Processor::hId (0, "mode"), 0.0f); q.setRealValue (Processor::hId (0, "int"), 7.0f);
            q.setRealValue (Processor::hId (0, "lvl"), 0.0f); q.setRealValue (Processor::hId (0, "det"), 0.0f);
            q.setRealValue (Processor::hId (1, "on"), 1.0f); q.setRealValue (Processor::hId (1, "mode"), 2.0f); q.setRealValue (Processor::hId (1, "lvl"), 0.0f);
            q.setRealValue (Processor::hId (1, "det"), 0.0f);
        });
        auto withMidi = run (*p, voice ([] (double) { return 220.0; }, 1.5), { { 0.0, 1.5, 64 } });
        auto q = make ([] (Processor& r) { r.setRealValue ("h_on", 0.0f); });
        auto plain = run (*q, voice ([] (double) { return 220.0; }, 1.5));
        const float fifth = tone (withMidi, 0.8, 329.63 * 1.0), plainFifth = tone (plain, 0.8, 329.63);
        // 329.6 Hz is both the fifth of A (E) and the MIDI note 64; check also the octave of the fifth's own fundamental
        const float e2 = tone (withMidi, 0.8, 659.26), plainE2 = tone (plain, 0.8, 659.26);
        check (fifth > plainFifth * 4.0f && e2 > plainE2 * 1.5f, "armonías: la quinta y la nota MIDI suenan ("_u + String (fifth / jmax (1.0e-6f, plainFifth), 1) + "x)");
    }

    // vocoder: the synth note sings with the voice's envelope
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("voc_on", 1.0f); q.setRealValue ("voc_mix", 100.0f); q.setRealValue ("tune_on", 0.0f); });
        auto o = run (*p, voice ([] (double) { return 150.0; }, 1.5), { { 0.0, 1.5, 69 } });
        auto silentVoice = AudioBuffer<float> (2, (int) (1.5 * sr));
        silentVoice.clear();
        auto p2 = make ([] (Processor& q) { q.setRealValue ("voc_on", 1.0f); q.setRealValue ("voc_mix", 100.0f); q.setRealValue ("tune_on", 0.0f); });
        auto o2 = run (*p2, silentVoice, { { 0.0, 1.5, 69 } });
        check (tone (o, 0.8, 440.0) > 0.005f && tu::rms (o2, (int) (0.5 * sr), (int) sr) < 1.0e-3f, "vocoder: la nota MIDI suena solo cuando hay voz");
    }

    // talkbox and robot
    {
        auto t = make ([] (Processor& q) { q.setRealValue ("tb_on", 1.0f); q.setRealValue ("tb_mix", 100.0f); q.setRealValue ("tune_on", 0.0f); });
        auto ot = run (*t, voice ([] (double) { return 150.0; }, 1.5), { { 0.0, 1.5, 57 } });
        auto r = make ([] (Processor& q) { q.setRealValue ("rb_on", 1.0f); q.setRealValue ("rb_note", 57.0f); q.setRealValue ("tune_on", 0.0f); });
        auto orb = run (*r, voice ([] (double t2) { return 260.0 + 30.0 * std::sin (t2 * 6.0); }, 1.5));
        const double fr = pitchOf (orb, 0.8, 0.4);
        check (tu::allFinite (ot) && tone (ot, 0.8, 220.0) > 0.003f && std::abs (cents (fr, 220.0)) < 15.0,
               "talkbox habla con la nota del sinte; robot plano en " + String (fr, 1) + " Hz");
    }

    // latency is exact and the dry path is identical when nothing changes the voice
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("tune_on", 0.0f); });
        AudioBuffer<float> imp (2, 8192);
        imp.clear();
        imp.setSample (0, 300, 0.5f); imp.setSample (1, 300, 0.5f);
        auto o = run (*p, imp);
        int at = 0; float best = 0.0f;
        for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > best) { best = std::abs (o.getSample (0, i)); at = i; }
        check (at - 300 == p->getLatencySamples(), "latencia " + String (p->getLatencySamples()) + " = medida " + String (at - 300));
    }

    // chopper and doubles
    {
        Head h;
        auto p = make ([] (Processor& q)
        {
            q.setRealValue ("tune_on", 0.0f); q.setRealValue ("ch_on", 1.0f); q.setRealValue ("ch_div", 10.0f); q.setRealValue ("ch_steps", 2.0f);
            q.setRealValue (Processor::chId (0), 1.0f); q.setRealValue (Processor::chId (1), 0.0f); q.setRealValue ("ch_depth", 100.0f);
        }, &h);
        auto o = run (*p, voice ([] (double) { return 200.0; }, 2.0), {}, &h);
        // 1/16 at 120 bpm = 125 ms: every other one is silent
        const float on = tu::rms (o, (int) (1.0 * sr) + 600, 4800), off = tu::rms (o, (int) (1.125 * sr) + 600, 4800);
        auto d = make ([] (Processor& q) { q.setRealValue ("tune_on", 0.0f); q.setRealValue ("dbl_on", 1.0f); q.setRealValue ("dbl_width", 100.0f); });
        auto od = run (*d, voice ([] (double) { return 200.0; }, 1.5));
        float diff = 0.0f;
        for (int i = (int) sr; i < (int) (1.2 * sr); ++i) diff = jmax (diff, std::abs (od.getSample (0, i) - od.getSample (1, i)));
        check (on > off * 20.0f && diff > 0.01f, "chopper corta cada otro dieciseisavo; el doble abre izquierda y derecha");
    }

    // note editor: capture, notes, move one, export
    {
        Head h;
        auto p = make ([] (Processor& q) { q.setRealValue ("scale", 0.0f); }, &h);
        auto take = voice ([] (double t) { return t < 1.0 ? 220.0 : 246.94; }, 2.0);
        p->startCapture();
        run (*p, take, {}, &h);
        p->stopCapture();
        const auto segs = p->segments();
        const bool two = segs.size() == 2 && segs[0].note == 57 && segs[1].note == 59;
        p->setSegmentOffset (0, 2);
        h.ppq = 0.0;
        auto o = run (*p, take, {}, &h);
        const double f = pitchOf (o, 0.6, 0.3);
        const auto mf = p->exportMidi();
        int ons = 0;
        if (auto* tr = mf.getTrack (0)) for (auto* e : *tr) if (e->message.isNoteOn()) ++ons;
        MemoryBlock st;
        p->getStateInformation (st);
        Processor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (two && p->value ("target") > 1.5f && std::abs (cents (f, 246.94)) < 15.0 && ons == 2 && q.segments().size() == 2 && q.segments()[0].offset == 2,
               "editor de notas: captura 2 notas, la primera movida +2 suena en " + String (f, 1) + " Hz, MIDI y proyecto");
    }

    // presets
    {
        bool ok = true;
        Processor ref;
        String bad;
        for (int i = 0; i < ref.getNumPrograms(); ++i)
        {
            auto p = std::make_unique<Processor>();
            p->prepareToPlay (sr, 256);
            p->setCurrentProgram (i);
            auto o = run (*p, voice ([] (double t) { return 200.0 + 20.0 * std::sin (t * 3.0); }, 1.0), { { 0.0, 1.0, 60 }, { 0.0, 1.0, 64 } });
            if (! tu::allFinite (o) || tu::rms (o, (int) (0.4 * sr), (int) (0.5 * sr)) < 1.0e-3f || tu::peak (o, 0, o.getNumSamples()) > 1.9f) { ok = false; bad << p->getProgramName (i) << " "; }
        }
        check (ok, "presets de voz: suenan y sin pasarse "_u + bad);
    }
    // the singer: with no audio coming in, a MIDI clip makes Chinchilla sing, in tune
    {
        auto p = make ([] (Processor& q) { q.setRealValue ("sing_on", 1.0f); q.setRealValue ("tune_on", 0.0f); });
        AudioBuffer<float> silence (2, (int) (2.0 * sr));
        silence.clear();
        auto o = run (*p, silence, { { 0.1, 1.6, 57 } });
        const double hz = pitchOf (o, 0.6, 0.5);
        check (tu::allFinite (o) && tu::rms (o, (int) (0.5 * sr), (int) (0.8 * sr)) > 0.01f && std::abs (cents (hz, 220.0)) < 25.0,
               "canta las notas MIDI sin audio de entrada: La3 a " + String (hz, 1) + " Hz, " + String (roundToInt (cents (hz, 220.0))) + " cents");
        auto q = make ([] (Processor&) {});
        auto o2 = run (*q, silence, { { 0.1, 1.6, 57 } });
        check (tu::rms (o2, (int) (0.5 * sr), (int) (0.8 * sr)) < 1.0e-4f, "apagado, las notas no suenan solas");
    }
}
static RegisterTest chinchillaTestReg ("Chinchilla", testChinchilla);

static void shotsChinchilla (const juce::File& out)
{
    using namespace juce;
    using namespace chinchilla_test;
    Head h;
    auto p = make ([] (Processor& q) { q.setRealValue ("scale", 1.0f); q.setRealValue ("h_on", 1.0f); q.setRealValue ("voc_on", 1.0f); q.setRealValue ("voc_mix", 40.0f); }, &h);
    p->startCapture();
    run (*p, voice ([] (double t) { return t < 1.0 ? 220.0 : (t < 2.0 ? 246.94 : 196.0); }, 3.0), { { 0.0, 3.0, 57 } }, &h);
    p->stopCapture();
    p->setSegmentOffset (1, 1);
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditorIfNeeded());
    auto* re = dynamic_cast<RacoonEditor*> (ed.get());
    auto* d = re != nullptr ? dynamic_cast<racoon::chinchilla::Display*> (re->getDisplay()) : nullptr;
    if (d == nullptr) { check (false, "pantallas de Chinchilla"); return; }
    for (int page = 0; page < 5; ++page)
    {
        d->showPage (page);
        MessageManager::getInstance()->runDispatchLoopUntil (120);
        tu::shot (*re, out.getChildFile ("chinchilla_pagina" + String (page) + ".png"));
    }
    check (true, "pantallas de afinación, editor, armonías, matriz y chopper"_u);
}
static RegisterTest chinchillaShots ("ChinchillaPantallas", shotsChinchilla);

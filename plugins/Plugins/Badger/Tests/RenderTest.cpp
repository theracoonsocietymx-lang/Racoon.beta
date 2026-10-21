#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/presets/PresetManager.h"
#include "../Source/PluginEditor.h"
#include "../Source/ui/Pages.h"
#include "../Source/installer/SelfInstaller.h"
#include "../Source/features/Features.h"

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 256;
    auto outDir = juce::File (argc > 1 ? argv[1] : "/tmp/be_render");
    outDir.createDirectory();
    if (argc > 2 && juce::String (argv[2]) == "osc")
    {
        be::Oscillator o; o.prepare (48000.0);
        be::OscSettings s; s.on = true; s.engine = juce::String (argv[3]).getIntValue(); s.vaWave = 2; s.unison = 1; s.reset = true; s.keyTrack = 1;
        s.table = be::FactoryBank::get().table (0); s.sample = be::FactoryBank::get().sample (0);
        be::OscMod m; m.level = 1; m.pitchSemis = (float) (69.0 + 12.0 * std::log2 (93.75 / 440.0)); m.pa = 0.5f;
        o.setSampleRate (48000.0);
        o.setControl (s, m, 440.0, 0.0f);
        o.noteOn (s, 93.75f, 1234, false);
        o.setControl (s, m, 440.0, 0.0f);
        for (int i = 0; i < 1024; ++i) { float l, r; o.tick (s, 0.0f, l, r); if (i % 32 == 0) printf ("%d %f %f\n", i, l, r); }
        return 0;
    }
    BassEngineProcessor proc;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    if (argc > 3 && juce::String (argv[2]) == "dna")
    {
        auto r = be::BassDNA::analyseFile (juce::File (argv[3]));
        printf ("DNA ok=%d f0=%.2f note=%.2f att=%.3f dec=%.3f sus=%.2f rel=%.3f bright=%.2f noise=%.2f\n%s\n", (int) r.ok, r.f0, r.midiNote,
                r.attack, r.decay, r.sustain, r.release, r.brightness, r.noisiness, r.summary.toRawUTF8());
        be::BassDNA::apply (proc, r, "test");
        juce::AudioBuffer<float> buf (2, block); float pk = 0;
        for (int b = 0; b < 300; ++b)
        {
            juce::MidiBuffer m;
            if (b == 2) m.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
            if (b == 200) m.addEvent (juce::MidiMessage::noteOff (1, 40), 0);
            if (b == 100) proc.apvts.getParameter ("v_quality")->setValueNotifyingHost (1.0f);   // High
            if (b == 150) proc.apvts.getParameter ("v_quality")->setValueNotifyingHost (0.0f);   // Eco
            buf.clear(); proc.processBlock (buf, m);
            for (int i = 0; i < block; ++i) { const float v = buf.getSample (0, i); if (! std::isfinite (v)) { printf ("NaN!\n"); return 1; } pk = std::max (pk, std::abs (v)); }
        }
        printf ("render after DNA peak %.3f\n", pk);
        return 0;
    }
    if (argc > 3 && juce::String (argv[2]) == "install")
    {
        const juce::File app (argv[3]);
        auto r1 = doceveinte::installBundledPlugins (app);
        printf ("install#1 attempted %d ok %d n %d err '%s'\n", (int) r1.attempted, (int) r1.ok, r1.installed.size(), r1.error.toRawUTF8());
        auto r2 = doceveinte::installBundledPlugins (app);
        printf ("install#2 attempted %d (expect 0)\n", (int) r2.attempted);
        return (r1.ok && r1.installed.size() == 2 && ! r2.attempted) ? 0 : 1;
    }
    if (argc > 2 && juce::String (argv[2]) == "tie")
    {
        be::SeqData d; d.initDefault();
        for (auto& st : d.patterns[0].steps) { st = be::SeqStep(); st.on = false; }
        auto put = [&] (int i, int note, bool tie, float gate) { auto& st = d.patterns[0].steps[i]; st.on = true; st.note = note; st.tie = tie; st.gate = gate; };
        put (0, 0, false, 0.5f); put (1, 0, true, 0.5f); put (2, 0, true, 0.5f);   // 2.5 steps long, gates < 1 on purpose
        put (4, 7, false, 0.5f); d.patterns[0].steps[4].addChord (10, 2.0f); d.patterns[0].steps[4].addChord (14, 0.5f);
        put (6, 3, false, 0.3f); put (7, 3, true, 0.4f);
        be::SeqParams sp; sp.mode = (int) be::SeqMode::Sequencer; sp.trigger = 0; sp.length = 8; sp.rate = 4; sp.gateScale = 0.8f; sp.swing = 0.3f;
        be::Sequencer sq; sq.prepare (48000.0); sq.reset();
        std::vector<be::SeqEvent> out;
        const double bpm = 120.0; const int n = 512;
        double beats = 0.0; long long sample = 0;
        struct On { long long s; int note; }; std::vector<On> ons; std::vector<std::pair<long long, int>> offs;
        for (int b = 0; b < 200; ++b)
        {
            out.clear();
            sq.process (d, sp, n, true, bpm, beats, out);
            for (auto& e : out) { if (e.noteOn) ons.push_back ({ sample + e.sampleOffset, e.note }); else offs.push_back ({ sample + e.sampleOffset, e.note }); }
            sample += n; beats += n / 48000.0 * bpm / 60.0;
        }
        const double stepS = 48000.0 * 60.0 / bpm * be::seqRateBeats (sp.rate);
        int fails = 0;
        // first loop: expect exactly 3 note-ons in the first 8 steps
        int c = 0; for (auto& o : ons) if (o.s < (long long) (stepS * 7.9)) ++c;
        for (auto& o : ons) if (o.s < (long long) (stepS * 7.9)) printf ("  on step %.2f note %d\n", o.s / stepS, o.note);
        printf ("tie: %d ons in first loop (expect 5)\n", c);
        if (c != 5) ++fails;
        // first note off should land after 2 steps
        long long off0 = -1; for (auto& o : offs) if (o.second == ons[0].note) { off0 = o.first; break; }
        printf ("tie: first note length %.2f steps (expect ~%.2f)\n", (off0 - ons[0].s) / stepS, 2.0 + 0.5 * 0.8);
        if ((off0 - ons[0].s) / stepS < 2.2) ++fails;
        {   // chord on step 4: three notes start together
            int at4 = 0; for (auto& o : ons) if (std::abs (o.s / stepS - 4.0) < 0.01) ++at4;
            printf ("chord: %d notes at step 4 (expect 1 before)\n", at4);
        }
        {   // PLAY button: pattern runs with trigger = Key Held, no key and no host transport
            be::Sequencer s2; s2.prepare (48000.0); s2.reset();
            auto sp2 = sp; sp2.trigger = 1; sp2.preview = true;
            int on2 = 0; std::vector<be::SeqEvent> o2;
            for (int b = 0; b < 100; ++b) { o2.clear(); s2.process (d, sp2, n, false, 0.0, 0.0, o2); for (auto& e : o2) if (e.noteOn) ++on2; }
            sp2.preview = false; int on3 = 0;
            for (int b = 0; b < 100; ++b) { o2.clear(); s2.process (d, sp2, n, false, 0.0, 0.0, o2); for (auto& e : o2) if (e.noteOn) ++on3; }
            printf ("preview: %d notes while playing, %d after stop\n", on2, on3);
            if (on2 == 0 || on3 != 0) ++fails;
        }
        printf ("TIE TESTS: %d failures\n", fails);
        return fails;
    }
    if (argc > 2 && juce::String (argv[2]) == "screens")
    {
        const int presetIndex = argc > 3 ? juce::String (argv[3]).getIntValue() : 1;
        proc.presets().loadFactory (presetIndex);
        std::unique_ptr<BassEngineEditor> ed (dynamic_cast<BassEngineEditor*> (proc.createEditor()));
        ed->setSize (BassEngineEditor::baseW, BassEngineEditor::baseH);
        be::ui::gForceAnimation = true;
        ed->setVisible (true);
        // play some audio so analyzers have content
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < 200; ++b)
        {
            juce::MidiBuffer midi;
            if (b % 25 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (b / 25 % 2) * 7, 0.9f), 0);
            if (b % 25 == 20) midi.addEvent (juce::MidiMessage::noteOff (1, 36 + (b / 25 % 2) * 7), 0);
            buf.clear();
            proc.processBlock (buf, midi);
        }
        juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (1, 36, 0.9f), 0);
        buf.clear(); proc.processBlock (buf, on);
        for (int b = 0; b < 30; ++b) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); }
        const char* names[] { "inicio", "synth", "mod", "acid", "seq", "fx", "mix", "analyze", "presets" };
        for (int pg = 0; pg < 9; ++pg)
        {
            ed->showPage (pg);
            for (int k = 0; k < 3; ++k) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); ed->tickForTest(); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = outDir.getChildFile (juce::String (pg) + "_" + names[pg] + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
        }
        {   // piano roll with ties, slides, accents, ratchets and octaves
            auto d = proc.getSeqData();
            auto& pt = d.patterns[(int) proc.apvts.getRawParameterValue ("seq_pattern")->load() - 1];
            for (auto& st : pt.steps) { st = be::SeqStep(); st.on = false; }
            auto put = [&] (int i, int note, int oct, bool acc, bool sl, bool tie, float gate, int rat, float vel)
            { auto& st = pt.steps[i]; st.on = true; st.note = note; st.octave = oct; st.accent = acc; st.slide = sl; st.tie = tie; st.gate = gate; st.ratchet = rat; st.vel = vel; };
            put (0, 0, 0, true, false, false, 1.0f, 1, 0.9f);
            put (1, 0, 0, false, false, true, 1.0f, 1, 0.9f);
            put (2, 0, 0, false, true, true, 0.6f, 1, 0.9f);
            put (3, 7, 0, false, false, false, 0.5f, 1, 0.6f);
            put (4, 12, 0, true, false, false, 0.4f, 1, 1.0f);
            put (6, 3, 0, false, true, false, 0.8f, 1, 0.7f);
            put (7, 5, 0, false, false, false, 0.5f, 3, 0.8f);
            put (8, 0, -1, true, false, false, 0.5f, 1, 1.0f);
            put (10, 10, 0, false, false, false, 1.0f, 1, 0.5f);
            put (11, 10, 0, false, false, true, 1.0f, 1, 0.5f);
            put (12, 10, 0, false, false, true, 0.3f, 1, 0.5f);
            put (14, 0, 1, true, true, false, 0.5f, 2, 1.0f);
            put (15, 3, 1, false, false, false, 0.5f, 1, 0.7f);
            proc.setSeqData (d);
            ed->showPage (4);
            for (int k = 0; k < 3; ++k) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); ed->tickForTest(); }
            ed->showPage (3); ed->showPage (4);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("3b_seq_roll.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {   // piano roll interaction test (synthetic mouse events)
            be::ui::StepGrid* grid = nullptr;
            std::function<void (juce::Component*)> find = [&] (juce::Component* c)
            { if (auto* gr = dynamic_cast<be::ui::StepGrid*> (c)) grid = gr; for (auto* ch : c->getChildren()) if (! grid) find (ch); };
            find (ed.get());
            if (grid == nullptr) { printf ("ROLL: grid not found\n"); return 1; }
            auto d = proc.getSeqData();
            const int pi = (int) proc.apvts.getRawParameterValue ("seq_pattern")->load() - 1;
            for (auto& st : d.patterns[pi].steps) { st = be::SeqStep(); st.on = false; }
            proc.setSeqData (d); grid->reload();
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            auto ev = [&] (juce::Point<float> p, juce::Point<float> down, bool dragged, int clicks = 1)
            { return juce::MouseEvent (src, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       grid, grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, dragged); };
            auto click = [&] (juce::Point<float> a) { grid->mouseDown (ev (a, a, false)); grid->mouseUp (ev (a, a, false)); };
            auto dragTo = [&] (juce::Point<float> a, juce::Point<float> b)
            { grid->mouseDown (ev (a, a, false)); for (int k = 1; k <= 8; ++k) grid->mouseDrag (ev (a + (b - a) * (k / 8.0f), a, true)); grid->mouseUp (ev (b, a, true)); };
            int fails = 0;
            auto chk = [&] (bool c, const char* what) { if (! c) { printf ("ROLL FAIL: %s\n", what); ++fails; } };
            grid->setDrawMode (true);       // the pencil: click writes, drag paints, click erases
            click (grid->posFor (0.3, 5));
            auto sd = proc.getSeqData(); auto* q = sd.patterns[pi].steps;
            chk (q[0].on && q[0].note == 5 && q[0].octave == 0 && ! q[0].tie, "click creates note");
            // resize: drag right edge of step 0 to step 3.5
            const float g0 = q[0].gate;
            dragTo (grid->posFor (g0 - 0.03, 5), grid->posFor (3.5, 5));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[1].on && q[1].tie && q[2].tie && q[3].tie && ! q[4].on, "resize builds tie chain");
            chk (std::abs (q[3].gate - 0.5f) < 0.08f, "resize sets last gate");
            // shrink back to 2 steps
            dragTo (grid->posFor (3.47, 5), grid->posFor (1.8, 5));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[1].tie && ! q[2].on && ! q[3].on, "shrink clears tail");
            // paint 5..7 at pitch 0
            dragTo (grid->posFor (5.2, 0), grid->posFor (7.6, 0));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[5].on && q[6].on && q[7].on && q[6].note == 0 && ! q[6].tie, "paint creates notes");
            // move step 5 up to pitch 3 (D#, in C minor)
            dragTo (grid->posFor (5.1, 0), grid->posFor (5.1, 3));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[5].note == 3, "vertical drag moves pitch");
            // click on note 6 removes it
            click (grid->posFor (6.1, 0));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (! q[6].on && q[7].on, "click on note erases");
            // octave range: pitch -12 and +30
            { juce::MouseWheelDetails w {}; w.deltaY = 1.0f; auto c = grid->posFor (9.2, 0); grid->mouseWheelMove (ev (c, c, false), w); grid->mouseWheelMove (ev (c, c, false), w); }
            click (grid->posFor (9.2, 25));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[9].on && q[9].note + 12 * q[9].octave == 25 && q[9].octave == 1, "high pitch maps to octave");
            // velocity lane
            dragTo (grid->lanePos (7.5, 0.25f), grid->lanePos (7.5, 0.25f));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (std::abs (q[7].vel - 0.25f) < 0.05f, "velocity lane");
            // chords: a second pitch in the same column adds a chord note and switches to Poly
            grid->centreOnPitch (9);
            click (grid->posFor (12.3, 7));
            click (grid->posFor (12.3, 12));
            click (grid->posFor (12.3, 10));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[12].on && q[12].numChord == 2, "chord notes in one column");
            chk ((int) proc.apvts.getRawParameterValue ("v_mode")->load() == 2, "poly mode switched on");
            // resize a chord note to 2 steps
            {
                auto r0 = grid->posFor (12.0 + q[12].chord[0].len - 0.04, q[12].chord[0].pitch);
                dragTo (r0, grid->posFor (14.0, q[12].chord[0].pitch));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (std::abs (q[12].chord[0].len - 2.0f) < 0.1f, "chord note resize");
            }
            // click on a chord note removes only that note
            click (grid->posFor (12.2, 10));
            sd = proc.getSeqData(); q = sd.patterns[pi].steps;
            chk (q[12].on && q[12].numChord == 1 && q[12].note == 7, "click removes chord note only");
            // state survives save / load
            {
                be::SeqData d2; d2.fromValueTree (proc.getSeqData().toValueTree());
                chk (d2.patterns[pi].steps[12].numChord == 1 && std::abs (d2.patterns[pi].steps[12].chord[0].len - 2.0f) < 0.1f, "chord saved in state");
            }
            // tie chain plays as one long note in the engine
            {
                be::SeqParams sp; sp.length = 16; sp.rate = proc.apvts.getRawParameterValue ("seq_rate") ? (int) proc.apvts.getRawParameterValue ("seq_rate")->load() : 0;
                auto mf = be::Sequencer::toMidiFile (proc.getSeqData(), sp, pi, 120.0);
                auto* tr = mf.getTrack (0);
                int ons = 0; double firstLen = -1;
                for (int i = 0; i < tr->getNumEvents(); ++i)
                {
                    auto& m = tr->getEventPointer (i)->message;
                    if (m.isNoteOn()) { ++ons; if (firstLen < 0) for (int j = i + 1; j < tr->getNumEvents(); ++j) if (tr->getEventPointer (j)->message.isNoteOff() && tr->getEventPointer (j)->message.getNoteNumber() == m.getNoteNumber()) { firstLen = tr->getEventPointer (j)->message.getTimeStamp() - m.getTimeStamp(); break; } }
                }
                chk (ons == 6, "export note count");
                const double stepTicks = 960.0 * be::seqRateBeats (sp.rate);
                chk (firstLen > stepTicks * 1.1, "tied note longer than one step");
                printf ("roll export: %d notes, first len %.2f steps\n", ons, firstLen / stepTicks);
            }
            // selection, as in Racoon's piano roll: box, move several in time and pitch, octave keys, duplicate
            {
                grid->setDrawMode (false);
                auto d3 = proc.getSeqData();
                for (auto& st : d3.patterns[pi].steps) { st = be::SeqStep(); st.on = false; }
                auto put = [&] (int i, int note) { auto& st = d3.patterns[pi].steps[i]; st.on = true; st.note = note; st.octave = 0; st.gate = 0.5f; };
                put (0, 0); put (2, 3); put (4, 5);
                proc.setSeqData (d3); grid->reload(); grid->centreOnPitch (4);
                auto count = [&]
                {
                    auto dd = proc.getSeqData(); int n = 0;
                    for (int i = 0; i < 16; ++i) { auto& st = dd.patterns[pi].steps[i]; if (st.on && ! st.tie) ++n; if (st.on) n += st.numChord; }
                    return n;
                };
                const int before = count();
                click (grid->posFor (8.3, 2));
                chk (count() == before, "select mode: a click on an empty spot writes nothing");
                dragTo (grid->posFor (0.05, 4), grid->posFor (3.9, -1));
                chk (grid->numSelected() == 2, "select mode: the box selects the two notes inside");
                dragTo (grid->posFor (0.3, 0), grid->posFor (1.3, 2));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (! q[0].on && q[1].on && q[1].note == 2 && q[3].on && q[3].note == 5 && q[4].on && q[4].note == 5 && ! q[2].on,
                     "select mode: dragging moves the selected notes one step later and two semitones up, the other stays");
                grid->keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (q[1].note + 12 * q[1].octave == 14 && q[3].note + 12 * q[3].octave == 17, "Shift+Up: the selection goes up an octave");
                grid->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier, 0));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (q[1].numChord == 1 && q[1].chord[0].pitch == 2 && q[3].numChord == 1 && q[3].chord[0].pitch == 5,
                     "Cmd+Shift+Down: a copy an octave below, on top of the originals");
                grid->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
                grid->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
                chk (count() == 0, "Cmd+A and Delete clear the pattern");
                grid->mouseDoubleClick (ev (grid->posFor (6.3, 7), grid->posFor (6.3, 7), false, 2));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (q[6].on && q[6].note == 7 && grid->numSelected() == 1, "select mode: double click writes a note");
                grid->keyPressed (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0));
                sd = proc.getSeqData(); q = sd.patterns[pi].steps;
                chk (q[7].on && q[7].note == 7, "Cmd+D duplicates the note right after");
            }
            printf ("ROLL TESTS: %d failures\n", fails);
        }
        printf ("screens written\n");
        return 0;
    }
    auto& pm = proc.presets();
    const int n = pm.numFactory();
    const int only = argc > 2 ? juce::String (argv[2]).getIntValue() : -1;
    bool allOk = true;
    for (int i = 0; i < n; ++i)
    {
        if (only >= 0 && i != only) continue;
        pm.loadFactory (i);
        if (argc > 3) if (auto* lp = proc.apvts.getParameter ("fx_limiter_on")) lp->setValueNotifyingHost (0.0f);
        const int total = (int) (sr * 4.0);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        // phrase: notes at 120 bpm 1/8ths, with overlap (legato) on some
        struct Ev { double t; int note; bool on; float vel; };
        std::vector<Ev> evs;
        const int notes[] { 36, 36, 48, 39, 36, 43, 34, 36 };
        for (int k = 0; k < 8; ++k)
        {
            const double t = k * 0.25;
            evs.push_back ({ t, notes[k], true, k % 4 == 0 ? 1.0f : 0.7f });
            const double len = (k == 2 || k == 5) ? 0.3 : 0.18;   // overlaps -> legato slides
            evs.push_back ({ t + len, notes[k], false, 0.0f });
        }
        evs.push_back ({ 2.2, 36, true, 0.9f }); evs.push_back ({ 3.4, 36, false, 0 });
        std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        juce::AudioBuffer<float> buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int len = std::min (block, total - pos);
            juce::MidiBuffer midi;
            for (auto& e : evs)
            {
                const int s = (int) (e.t * sr);
                if (s >= pos && s < pos + len)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos);
            }
            buf.setSize (2, len, false, false, true);
            buf.clear();
            proc.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, len);
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        float peak = 0, rms = 0; bool nan = false;
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < total; ++s) { const float v = out.getSample (c, s); if (! std::isfinite (v)) nan = true; peak = std::max (peak, std::abs (v)); rms += v * v; }
        rms = std::sqrt (rms / (2.0f * total));
        const bool ok = ! nan && peak > 0.01f && peak < 1.5f;
        allOk &= ok;
        printf ("%2d %-22s peak %6.3f  rms %6.3f  cpu %5.1f%%  %s\n", i, pm.factoryName (i).toRawUTF8(), peak, rms, 100.0 * ms / 4000.0, ok ? "OK" : "FAIL");
        writeWav (outDir.getChildFile (juce::String (i).paddedLeft ('0', 2) + "_" + pm.factoryName (i).replaceCharacter (' ', '_') + ".wav"), out, sr);
    }
    return allOk ? 0 : 1;
}

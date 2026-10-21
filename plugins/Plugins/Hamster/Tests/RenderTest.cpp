#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/presets/PresetManager.h"
#include "../Source/PluginEditor.h"
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
    auto outDir = juce::File (argc > 1 ? argv[1] : "/tmp/de_render");
    outDir.createDirectory();
    DrumEngineProcessor proc;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    const juce::String mode = argc > 2 ? juce::String (argv[2]) : juce::String ("presets");
    if (mode == "presets")
    {
        const int n = proc.presets().numFactory();
        int failures = 0;
        for (int i = 0; i < n; ++i)
        {
            proc.presets().loadFactory (i);
            proc.setParamReal ("g_play", 1.0f);
            const int total = (int) (sr * 8.0);
            juce::AudioBuffer<float> out (2, total), buf (2, block);
            out.clear();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            float pk = 0.0f; double rms = 0.0; bool bad = false;
            for (int pos = 0; pos < total; pos += block)
            {
                juce::MidiBuffer m;
                buf.clear();
                proc.processBlock (buf, m);
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < block; ++s)
                    {
                        const float v = buf.getSample (c, s);
                        if (! std::isfinite (v)) bad = true;
                        pk = std::max (pk, std::abs (v)); rms += (double) v * v;
                        if (pos + s < total) out.setSample (c, pos + s, v);
                    }
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            proc.setParamReal ("g_play", 0.0f);
            for (int k = 0; k < 40; ++k) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); }
            rms = std::sqrt (rms / (total * 2.0));
            const bool silent = pk < 0.05f;
            if (bad || silent || pk > 1.0f) ++failures;
            printf ("%2d %-34s peak %.3f rms %6.1f dB lufsI %6.1f cpu %5.1f%% %s\n", i, proc.presets().factoryName (i).toRawUTF8(), pk,
                    20.0 * std::log10 (std::max (rms, 1.0e-9)), proc.engine.loudness.integrated.load(), ms / 80.0, bad ? "NaN!" : (silent ? "SILENT" : ""));
            proc.engine.loudness.resetRequest = true;
            if (argc > 3) writeWav (outDir.getChildFile (juce::String (i) + ".wav"), out, sr);
        }
        printf ("failures %d\n", failures);
        return failures;
    }
    if (mode == "screens")
    {
        const int presetIndex = argc > 3 ? juce::String (argv[3]).getIntValue() : 9;
        proc.presets().loadFactory (presetIndex);
        std::unique_ptr<DrumEngineEditor> ed (dynamic_cast<DrumEngineEditor*> (proc.createEditor()));
        ed->setSize (DrumEngineEditor::baseW, DrumEngineEditor::baseH);
        be::ui::gForceAnimation = true;
        ed->setVisible (true);
        proc.setParamReal ("g_play", 1.0f);
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < 500; ++b) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); if (b % 8 == 0) ed->tickForTest(); }
        const char* names[] { "inicio", "kit", "synth", "sample", "seq", "mod", "fx", "mix", "analyze", "presets", "live" };
        for (int pg = 0; pg < 11; ++pg)
        {
            if (pg == 10) ed->setLive (true); else ed->showPage (pg);
            for (int k = 0; k < 6; ++k) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); ed->tickForTest(); }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = outDir.getChildFile (juce::String (pg) + "_" + names[pg] + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
        }
        printf ("screens written\n");
        // stress: preset changes, track selection and page flips with the editor open
        ed->setLive (false);
        for (int i = 0; i < 24; ++i)
        {
            proc.presets().loadFactory ((i * 7) % proc.presets().numFactory());
            ed->showPage (i % 10);
            proc.padDown (i % 16, 0.8f);
            for (int k = 0; k < 4; ++k) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); ed->tickForTest(); }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        printf ("stress ok\n");
        return 0;
    }
    if (mode == "func")
    {
        int fails = 0;
        auto check = [&] (bool ok, const char* what) { printf ("%-48s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; };
        juce::AudioBuffer<float> buf (2, block);
        auto run = [&] (double seconds, std::function<void (int, juce::MidiBuffer&)> midiFn = {})
        {
            const int blocks = (int) (seconds * sr / block);
            for (int b = 0; b < blocks; ++b) { juce::MidiBuffer m; if (midiFn) midiFn (b, m); buf.clear(); proc.processBlock (buf, m); }
        };
        auto hits = [&] (int t) { return proc.engine.telemetry.trackHits[t].load(); };
        auto fresh = [&] (std::function<void (be::SeqData&)> fn)
        {
            proc.setParamReal ("g_play", 0.0f);
            run (0.05);
            be::SeqData d;
            for (auto& p : d.pats) for (auto& t : p.tracks) t.length = 16;
            fn (d);
            proc.setSeqData (d, false);
            proc.setParamReal ("g_pattern", 1.0f);
            proc.setParamReal ("g_songmode", 0.0f);
            proc.setParamReal ("g_swing", 0.0f);
            proc.setParamReal ("g_humanize", 0.0f);
            proc.setParamReal ("g_bpm", 120.0f);
            run (0.05);
        };
        proc.presets().loadFactory (0);
        // 120 BPM: one 16-step bar = 2 s
        fresh ([] (be::SeqData& d) { d.pats[0].tracks[0].steps[0].on = true; d.pats[0].tracks[0].steps[8].on = true; });
        int h0 = hits (0); proc.setParamReal ("g_play", 1.0f); run (4.0 - 0.01); int h1 = hits (0);
        check (h1 - h0 == 4, "basic: 2 hits per bar over 2 bars");
        fresh ([] (be::SeqData& d) { auto& s = d.pats[0].tracks[0].steps[0]; s.on = true; s.retrig = 4; });
        h0 = hits (0); proc.setParamReal ("g_play", 1.0f); run (2.0 - 0.01); h1 = hits (0);
        check (h1 - h0 == 4, "ratchet x4");
        fresh ([] (be::SeqData& d) { auto& s = d.pats[0].tracks[0].steps[0]; s.on = true; s.cond = 11; s.condA = 1; s.condB = 2; });
        h0 = hits (0); proc.setParamReal ("g_play", 1.0f); run (8.0 - 0.01); h1 = hits (0);
        check (h1 - h0 == 2, "condition 1:2 over 4 loops");
        fresh ([] (be::SeqData& d) { auto& s = d.pats[0].tracks[0].steps[0]; s.on = true; s.prob = 50; for (auto& t : d.pats[0].tracks) t.length = 1; });
        h0 = hits (0); proc.setParamReal ("g_play", 1.0f); run (16.0); h1 = hits (0);
        printf ("   probability 50%%: %d of %d\n", h1 - h0, 128);
        check (h1 - h0 > 40 && h1 - h0 < 90, "probability 50 %");
        fresh ([] (be::SeqData& d) { auto& s = d.pats[0].tracks[0].steps[0]; s.on = true; s.cond = 1; });
        h0 = hits (0); proc.setParamReal ("g_play", 1.0f); run (1.9); h1 = hits (0);
        proc.setParamReal ("g_fill", 1.0f); run (2.0); int h2 = hits (0); proc.setParamReal ("g_fill", 0.0f);
        printf ("   fill: without %d with %d  fillActive %d param %.2f\n", h1 - h0, h2 - h1, (int) proc.engine.telemetry.fillActive.load(), proc.getParamReal ("g_fill"));
        check (h1 - h0 == 0 && h2 - h1 == 1, "fill condition");
        fresh ([] (be::SeqData& d) { d.pats[0].tracks[0].length = 3; d.pats[0].tracks[0].steps[0].on = true; d.pats[0].tracks[1].length = 4; d.pats[0].tracks[1].steps[0].on = true; });
        h0 = hits (0); int k0 = hits (1); proc.setParamReal ("g_play", 1.0f); run (6.0 - 0.01); h1 = hits (0); int k1 = hits (1);
        check (h1 - h0 == 16 && k1 - k0 == 12, "polymeter 3 against 4");
        // pattern switch is queued to the next bar
        fresh ([] (be::SeqData& d) { d.pats[0].tracks[0].steps[0].on = true; d.pats[1].tracks[1].steps[0].on = true; });
        proc.setParamReal ("g_play", 1.0f); run (1.0);
        h0 = hits (0); k0 = hits (1);
        proc.setParamReal ("g_pattern", 2.0f); run (0.9); 
        check (hits (1) == k0, "switch waits for the bar");
        run (2.0);
        check (hits (1) > k0 && proc.engine.telemetry.activePattern.load() == 1, "switch happens at the bar");
        // song mode A x1, B x2
        fresh ([] (be::SeqData& d) { d.pats[0].tracks[0].steps[0].on = true; d.pats[1].tracks[1].steps[0].on = true; d.song[0] = { 0, 1 }; d.song[1] = { 1, 2 }; d.songLength = 2; });
        proc.setParamReal ("g_songmode", 1.0f);
        h0 = hits (0); k0 = hits (1); proc.setParamReal ("g_play", 1.0f); run (12.0 - 0.01);
        check (hits (0) - h0 == 2 && hits (1) - k0 == 4, "song mode A x1 B x2 twice");
        proc.setParamReal ("g_songmode", 0.0f);
        // MIDI note input
        proc.setParamReal ("g_play", 0.0f); run (0.1);
        h0 = hits (2);
        run (0.2, [&] (int b, juce::MidiBuffer& m) { if (b == 1) m.addEvent (juce::MidiMessage::noteOn (10, (int) proc.trackValue (2, be::T_NOTE), 0.8f), 10); });
        check (hits (2) == h0 + 1, "MIDI note triggers its track");
        // note repeat
        proc.setParamReal ("g_noterepeat", 4.0f);   // 1/16
        h0 = hits (2);
        run (1.0, [&] (int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent (juce::MidiMessage::noteOn (10, (int) proc.trackValue (2, be::T_NOTE), 0.8f), 0);
                                                    if (b == (int) (0.99 * sr / block)) m.addEvent (juce::MidiMessage::noteOff (10, (int) proc.trackValue (2, be::T_NOTE)), 0); });
        printf ("   note repeat hits in 1 s at 120 BPM 1/16: %d\n", hits (2) - h0);
        check (hits (2) - h0 >= 7 && hits (2) - h0 <= 9, "note repeat 1/16");
        proc.setParamReal ("g_noterepeat", 0.0f);
        // state round trip
        fresh ([] (be::SeqData& d) { auto& s = d.pats[3].tracks[5].steps[7]; s.on = true; s.vel = 77; s.setLock (be::T_TUNE, 0.25f); s.cond = 11; s.condA = 3; s.condB = 4; d.pats[3].name = "Test"; });
        proc.setTrackValue (4, be::T_A_P3, 0.123f);
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        DrumEngineProcessor p2; p2.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto a = proc.getSeqData(), b2 = p2.getSeqData();
        check (be::SeqData::patternToString (a.pats[3]) == be::SeqData::patternToString (b2.pats[3]) && b2.pats[3].name == "Test", "state: patterns + locks restored");
        check (std::abs (p2.trackValue (4, be::T_A_P3) - 0.123f) < 1.0e-3f, "state: track params restored");
        // exports
        fresh ([] (be::SeqData& d) { d.pats[0].tracks[0].steps[0].on = true; d.pats[0].tracks[2].steps[4].on = true; });
        auto mf = proc.exportPatternToMidi (0, 2);
        check (mf.existsAsFile() && mf.getSize() > 40, "MIDI export");
        auto wf = proc.exportPatternToAudio (0, 2);
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wf));
            float lo[2] {}, hi[2] {};
            bool ok = r != nullptr && r->lengthInSamples > sr * 3.5;
            if (ok) r->readMaxLevels (0, r->lengthInSamples, lo[0], hi[0], lo[1], hi[1]);
            check (ok && hi[0] > 0.05f, "audio export renders");
        }
        // drum dna on synthesized roles
        int correct = 0, total = 0;
        for (int role : { 0, 1, 2, 3, 4, 6, 8, 12, 14 })
        {
            proc.applyRoleDefaults (0, role, 1);
            proc.setParamReal ("tp_on", 0.0f);
            juce::AudioBuffer<float> out (2, (int) sr);
            out.clear();
            for (int pos = 0; pos < (int) sr; pos += block)
            {
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (10, (int) proc.trackValue (0, be::T_NOTE), 0.9f), 0);
                buf.clear(); proc.processBlock (buf, m);
                for (int c = 0; c < 2; ++c) for (int s2 = 0; s2 < block && pos + s2 < (int) sr; ++s2) out.setSample (c, pos + s2, buf.getSample (c, s2));
            }
            auto f = outDir.getChildFile ("dna_" + juce::String (role) + ".wav");
            writeWav (f, out, sr);
            auto res = be::DrumDNA::analyse (f);
            const bool ok = res.ok && (res.role == role || (role == 4 && res.role == 3) || (role >= 8 && res.role >= 8 && res.role <= 13));
            printf ("   DNA %-10s -> %-10s  %s\n", be::roleNames()[role].toRawUTF8(), be::roleNames()[res.role].toRawUTF8(), res.summary.toRawUTF8());
            correct += ok ? 1 : 0; ++total;
            run (0.3);
        }
        check (correct >= total - 2, "Drum DNA role detection");
        // groove dna: render a swung hat loop and read the swing back
        proc.presets().loadFactory (0);
        fresh ([] (be::SeqData& d) { for (int s2 = 0; s2 < 16; ++s2) d.pats[0].tracks[5].steps[s2].on = true; });
        proc.setParamReal ("g_swing", 0.6f);
        auto gw = proc.exportPatternToAudio (0, 4, 5);
        auto gr = be::GrooveDNA::analyse (gw, 120.0);
        float odd = 0, even = 0;
        for (int i = 0; i < 16; ++i) (i % 2 ? odd : even) += gr.groove.timing[i] / 8.0f;
        printf ("   groove: %s  odd %.3f even %.3f\n", gr.summary.toRawUTF8(), odd, even);
        check (gr.ok && odd > 0.25f && std::abs (even) < 0.1f, "Groove DNA reads swing");
        // kick-bass bridge: publish the kick track and read it back like Bass Engine does
        fresh ([] (be::SeqData& d) { for (int k : { 0, 4, 8, 12 }) d.pats[0].tracks[0].steps[k].on = true; d.pats[0].tracks[0].steps[14].on = true; d.pats[0].tracks[0].steps[14].prob = 30; });
        proc.setParamReal ("bridge_on", 1.0f);
        proc.setParamReal ("bridge_track", 1.0f);
        proc.setParamReal ("g_play", 1.0f);
        run (0.2);
        proc.publishBridgeNow();
        {
            be::KickBassBridge reader;
            be::BridgeKickSchedule ks;
            const bool ok = reader.readKick (ks);
            printf ("   bridge: ok %d hits %d loop %.2f beats kick %.1f Hz decay %.2f s playing %d\n", (int) ok, ks.numHits, ks.loopBeats, ks.kickHz, ks.kickDecay, ks.playing);
            check (ok && ks.numHits == 4 && std::abs (ks.loopBeats - 4.0) < 1.0e-6 && ks.playing == 1 && std::abs (ks.hitBeat[1] - 1.0f) < 1.0e-4f, "bridge publishes the kick schedule");
            be::BridgeBassState bs; bs.active = 1; bs.bassHz = 55.0f; bs.bassEnv = 0.4f;
            reader.publishBass (bs);
            proc.publishBridgeNow();
            check (std::abs (proc.bridgeBassHz.load() - 55.0f) < 0.01f, "bridge reads the bass state");
        }
        printf ("func failures %d\n", fails);
        return fails;
    }
    if (mode == "probe")
    {
        // probe <role> <model> <local param> <value>: prints the -20/-40 dB decay times of one hit
        const int role = juce::String (argv[3]).getIntValue(), model = juce::String (argv[4]).getIntValue();
        proc.applyRoleDefaults (0, role, model);
        for (int a = 5; a + 1 < argc; a += 2) proc.setTrackValue (0, juce::String (argv[a]).getIntValue(), juce::String (argv[a + 1]).getFloatValue());
        proc.setParamReal ("tp_on", 0.0f); proc.setParamReal ("glue_on", 0.0f);
        juce::AudioBuffer<float> buf (2, block);
        std::vector<float> env;
        for (int pos = 0; pos < (int) (sr * 2); pos += block)
        {
            juce::MidiBuffer m;
            if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (10, (int) proc.trackValue (0, be::T_NOTE), 0.9f), 0);
            buf.clear(); proc.processBlock (buf, m);
            float pk = 0; for (int i = 0; i < block; ++i) pk = std::max (pk, std::abs (buf.getSample (0, i)));
            env.push_back (pk);
        }
        float mx = 0; int at = 0; for (int i = 0; i < (int) env.size(); ++i) if (env[i] > mx) { mx = env[i]; at = i; }
        int t20 = -1, t40 = -1;
        for (int i = at; i < (int) env.size(); ++i) { if (t20 < 0 && env[i] < mx * 0.1f) t20 = i; if (t40 < 0 && env[i] < mx * 0.01f) { t40 = i; break; } }
        printf ("peak %.3f  -20dB %.3f s  -40dB %.3f s\n", mx, t20 * block / sr, t40 * block / sr);
        return 0;
    }
    if (mode == "bench")
    {
        proc.presets().loadFactory (argc > 3 ? juce::String (argv[3]).getIntValue() : 9);
        proc.setParamReal ("g_play", 1.0f);
        juce::AudioBuffer<float> buf (2, block);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const int blocks = (int) (sr * 2.0 / block);
        for (int b = 0; b < blocks; ++b) { juce::MidiBuffer m; buf.clear(); proc.processBlock (buf, m); }
        printf ("bench %.1f%% realtime\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
        return 0;
    }
    if (mode == "roles")
    {
        const bool raw = argc > 4;
        if (raw) { proc.setParamReal ("tp_on", 0); proc.setParamReal ("glue_on", 0); proc.setParamReal (be::fxId ("limiter", "on"), 0); proc.setParamReal ("g_master", 0.8f); }
        // every role x model hit once, written to a wav for inspection
        juce::AudioBuffer<float> buf (2, block);
        for (int role = 0; role < (int) be::Role::Count; ++role)
            for (int model = 0; model < 5; ++model)
            {
                proc.applyRoleDefaults (0, role, model);
                juce::AudioBuffer<float> out (2, (int) sr);
                out.clear();
                float pk = 0.0f;
                for (int pos = 0; pos < (int) sr; pos += block)
                {
                    juce::MidiBuffer m;
                    if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (10, (int) proc.trackValue (0, be::T_NOTE), 0.9f), 0);
                    buf.clear();
                    proc.processBlock (buf, m);
                    for (int c = 0; c < 2; ++c) for (int s = 0; s < block && pos + s < (int) sr; ++s) { out.setSample (c, pos + s, buf.getSample (c, s)); pk = std::max (pk, std::abs (buf.getSample (c, s))); }
                }
                double e = 0; int cnt = 0; for (int i = 0; i < out.getNumSamples(); ++i) { const float v = out.getSample (0, i); e += v * v; if (std::abs (v) > pk * 0.01f) cnt = i; }
                printf ("role %-10s model %d peak %6.2f dB  energy %6.2f dB  len %.2fs\n", be::roleNames()[role].toRawUTF8(), model, 20 * std::log10 (pk + 1e-9), 10 * std::log10 (e + 1e-12), cnt / sr);
                if (argc > 3) writeWav (outDir.getChildFile ("role_" + juce::String (role) + "_" + juce::String (model) + ".wav"), out, sr);
            }
        return 0;
    }
    return 0;
}

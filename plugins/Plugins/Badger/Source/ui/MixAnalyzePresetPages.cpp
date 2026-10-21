#include "Pages.h"
#include "I18n.h"
#include "../presets/PresetManager.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== MIX: layer strip
LayerStrip::LayerStrip (Ctx& c, int layer)
    : Panel (layerNames()[layer], layer == 0), ctx (c), l (layer),
      level (c, layId (layer, "level"), "LEVEL"), pan (c, layId (layer, "pan"), "PAN"), width (c, layId (layer, "width"), "WIDTH"),
      tune (c, layId (layer, "tune"), "TUNE"), drive (c, layId (layer, "drive"), "DRIVE"), lowCut (c, layId (layer, "lowcut"), "LOW CUT"),
      highCut (c, layId (layer, "highcut"), "HIGH CUT"), delay (c, layId (layer, "delay"), "ALIGN"),
      driveType (c, layId (layer, "drivetype")), path (c, layId (layer, "path")),
      mute (c, layId (layer, "mute"), "MUTE"), solo (c, layId (layer, "solo"), "SOLO"), invert (c, layId (layer, "invert"), "PHASE"),
      keyLo (c, layId (layer, "keylo"), "KEY LO", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); }),
      keyHi (c, layId (layer, "keyhi"), "KEY HI", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); }),
      velLo (c, layId (layer, "vello"), "VEL LO"), velHi (c, layId (layer, "velhi"), "VEL HI")
{
    for (Component* comp : std::initializer_list<Component*> { &level, &pan, &width, &tune, &drive, &lowCut, &highCut, &delay, &driveType, &path, &mute, &solo, &invert,
                                                               &keyLo, &keyHi, &velLo, &velHi, &savePreset, &loadPreset })
        addAndMakeVisible (comp);
    savePreset.setTooltip ("Save this layer (its oscillators and layer settings) as a layer preset");
    loadPreset.setTooltip ("Load a layer preset into this layer");
    savePreset.onClick = [this]
    {
        auto* w = new AlertWindow ("Save layer preset", "Name for this " + layerNames()[l] + " layer:", MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", layerNames()[l] + " " + Time::getCurrentTime().formatted ("%H%M"));
        w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r) { if (r == 1) ctx.proc.presets().saveLayerPreset (l, w->getTextEditorContents ("name")); }), true);
    };
    loadPreset.onClick = [this]
    {
        PopupMenu m;
        auto names = ctx.proc.presets().layerPresetNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        if (names.isEmpty()) m.addItem (-1, "No layer presets saved yet", false);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&loadPreset), [this, names] (int r) { if (r > 0) ctx.proc.presets().loadLayerPreset (l, names[r - 1]); });
    };
    ctx.registerAnimated (this);
}

LayerStrip::~LayerStrip() { ctx.unregisterAnimated (this); }

void LayerStrip::animate()
{
    if (! animVisible (this)) return;
    StringArray parts;
    for (int o = 0; o < kNumOsc; ++o)
        if ((int) ctx.getParam (oscId (o, "layer")) == l && ctx.getParam (oscId (o, "on")) > 0.5f) parts.add ("OSC " + String (o + 1));
    if (l == 0 && ctx.getParam ("sub_on") > 0.5f) parts.insert (0, "SUB OSC");
    if (ctx.getParam ("noise_on") > 0.5f && (int) ctx.getParam ("noise_layer") == l) parts.add ("NOISE");
    auto s = parts.isEmpty() ? String ("empty") : parts.joinIntoString ("  /  ");
    if (s != contents) { contents = s; repaint(); }
}

void LayerStrip::paint (Graphics& g)
{
    Panel::paint (g);
    auto r = getLocalBounds().reduced (10, 6).withTrimmedTop (22).removeFromTop (16).toFloat();
    g.setFont (Fonts::mono (10.5f));
    g.setColour (contents == "empty" ? S::inkFaint : S::inkSoft);
    g.drawText (::racoon::i18n::tr (contents), r, Justification::centredLeft);
}

void LayerStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromTop (40);
    auto big = r.removeFromTop (110);
    level.setBounds (big.withSizeKeepingCentre (110, 110));
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (88), { &pan, &width, &tune });
    layoutRow (r.removeFromTop (88), { &drive, &lowCut, &highCut });
    r.removeFromTop (2);
    driveType.setBounds (r.removeFromTop (24).reduced (2, 0));
    r.removeFromTop (6);
    layoutRow (r.removeFromTop (40), { &keyLo, &keyHi }, 6);
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (40), { &velLo, &velHi }, 6);
    r.removeFromTop (6);
    path.setBounds (r.removeFromTop (24).reduced (2, 0));
    r.removeFromTop (6);
    layoutRow (r.removeFromTop (22), { &mute, &solo, &invert }, 2);
    r.removeFromTop (6);
    auto row = r.removeFromTop (88);
    delay.setBounds (row.removeFromLeft (row.getWidth() / 2));
    auto btns = row.reduced (4, 14);
    savePreset.setBounds (btns.removeFromTop (btns.getHeight() / 2).reduced (0, 2));
    loadPreset.setBounds (btns.reduced (0, 2));
}

// ===================================================================== MIX page
MixPage::MixPage (Ctx& c)
    : ctx (c), l0 (c, 0), l1 (c, 1), l2 (c, 2), l3 (c, 3),
      layerMode (c, "layer_mode", "LAYER MIX"), tuning (c, "v_tuning", "TUNING"), tuningRoot (c, "v_tuningroot", "TUNING ROOT"),
      evoLen (c, "evo_len", "LENGTH"), evoShape (c, "evo_shape", "SHAPE"),
      xfade (c, "layer_xfade", "BODY / BITE"), velCurve (c, "v_velcurve", "VEL CURVE"), tune (c, "v_tune", "A4"), humanize (c, "v_humanize", "HUMANIZE"),
      impAmount (c, "imp_amount", "AMOUNT"), impDrift (c, "imp_drift", "PITCH"), impFilter (c, "imp_filter", "FILTER"), impAttack (c, "imp_attack", "ATTACK"),
      impSat (c, "imp_sat", "SATURATION"), evoAmount (c, "evo_amount", "AMOUNT"),
      bendUp (c, "v_bendup", "BEND UP"), bendDown (c, "v_benddown", "BEND DN"), mpeRange (c, "v_mperange", "MPE RANGE"), transpose (c, "v_transpose", "TRANSPOSE"),
      keyLo (c, "v_keylo", "KEY LO", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); }),
      keyHi (c, "v_keyhi", "KEY HI", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3); }),
      mpe (c, "v_mpe", "MPE")
{
    for (Component* comp : std::initializer_list<Component*> { &l0, &l1, &l2, &l3, &busPanel, &midiPanel, &impPanel, &evoPanel, &layerMode, &tuning, &tuningRoot,
                                                               &evoLen, &evoShape, &xfade, &velCurve, &tune, &humanize, &impAmount, &impDrift, &impFilter,
                                                               &impAttack, &impSat, &evoAmount, &bendUp, &bendDown, &mpeRange, &transpose, &keyLo, &keyHi,
                                                               &mpe, &loadScl, &outInfo })
        addAndMakeVisible (comp);
    outInfo.setFont (Fonts::body (13.0f));
    outInfo.setColour (Label::textColourId, S::inkSoft);
    outInfo.setJustificationType (Justification::topLeft);
    outInfo.setText ("Separate outputs: enable the SUB / BODY / BITE / TEXTURE outputs in your DAW (I/O of the track, output channels). Filtered layers in Serial, Parallel and Split routing share the BODY output.",
                     dontSendNotification);
    loadScl.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Load Scala tuning", File(), "*.scl");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) ctx.proc.loadScl (fc.getResult());
        });
    };
    evoAmount.knob.setTooltip ("Bass Evolution: a slow, bar-synced source. Route EVOLUTION to anything, or use macro EVOLVE");
}

void MixPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto strips = r.removeFromLeft (r.getWidth() * 60 / 100);
    layoutRow (strips, { &l0, &l1, &l2, &l3 }, 8);
    r.removeFromLeft (8);
    auto bus = r.removeFromTop (150);
    busPanel.setBounds (bus);
    auto b = busPanel.content() + bus.getPosition();
    auto left = b.removeFromLeft (110);
    layerMode.setBounds (left.removeFromTop (40));
    xfade.setBounds (b.removeFromLeft (90));
    outInfo.setBounds (b.reduced (4, 0));
    r.removeFromTop (8);
    auto midi = r.removeFromTop (230);
    midiPanel.setBounds (midi);
    auto m = midiPanel.content() + midi.getPosition();
    layoutRow (m.removeFromTop (40), { &bendUp, &bendDown, &transpose, &keyLo, &keyHi }, 6);
    m.removeFromTop (6);
    auto row = m.removeFromTop (40);
    mpe.setBounds (row.removeFromLeft (70).withTrimmedTop (14));
    mpeRange.setBounds (row.removeFromLeft (90));
    row.removeFromLeft (8);
    tuning.setBounds (row.removeFromLeft (140));
    row.removeFromLeft (6);
    tuningRoot.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (6);
    loadScl.setBounds (row.withTrimmedTop (14));
    m.removeFromTop (4);
    layoutRow (m.removeFromTop (92), { &velCurve, &tune, &humanize, nullptr });
    r.removeFromTop (8);
    auto imp = r.removeFromTop ((r.getHeight() - 8) / 2);
    impPanel.setBounds (imp);
    layoutRow ((impPanel.content() + imp.getPosition()).removeFromTop (92), { &impAmount, &impDrift, &impFilter, &impAttack, &impSat });
    r.removeFromTop (8);
    evoPanel.setBounds (r);
    auto e = evoPanel.content() + r.getPosition();
    auto ec = e.removeFromLeft (260);
    layoutRow (ec.removeFromTop (40), { &evoLen, &evoShape }, 6);
    evoAmount.setBounds (e.removeFromLeft (90).withHeight (92));
}

// ===================================================================== ANALYZE
AnalyzePage::AnalyzePage (Ctx& c) : ctx (c), scope (c), spectrum (c), harmonics (spectrum), meter (c)
{
    for (Component* comp : std::initializer_list<Component*> { &scopePanel, &specPanel, &harmPanel, &fundPanel, &meterPanel, &focusPanel, &envPanel, &histPanel,
                                                               &scope, &spectrum, &harmonics, &meter, &prePost })
        addAndMakeVisible (comp);
    prePost.setClickingTogglesState (true);
    prePost.setTooltip ("Off: after the FX chain (what you hear). On: before distortion and effects");
    prePost.onClick = [this] { scope.showPre = prePost.getToggleState(); };
    scopeBuf.resize (8192);
    ctx.registerAnimated (this);
}

AnalyzePage::~AnalyzePage() { ctx.unregisterAnimated (this); }

void AnalyzePage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (290);
    auto sc = top.removeFromLeft (top.getWidth() * 45 / 100);
    scopePanel.setBounds (sc);
    auto scc = scopePanel.content() + sc.getPosition();
    prePost.setBounds (scc.removeFromTop (22).removeFromRight (150));
    scc.removeFromTop (4);
    scope.setBounds (scc);
    top.removeFromLeft (8);
    specPanel.setBounds (top);
    spectrum.setBounds (specPanel.content() + top.getPosition());
    r.removeFromTop (8);
    auto mid = r;
    auto c1 = mid.removeFromLeft (360);
    auto h = c1.removeFromTop (200);
    harmPanel.setBounds (h);
    harmonics.setBounds (harmPanel.content() + h.getPosition());
    c1.removeFromTop (8);
    fundPanel.setBounds (c1);
    fundArea = fundPanel.content() + c1.getPosition();
    mid.removeFromLeft (8);
    auto c2 = mid.removeFromLeft (250);
    meterPanel.setBounds (c2);
    auto mc = meterPanel.content() + c2.getPosition();
    meter.setBounds (mc.removeFromLeft (40));
    meterArea = mc.withTrimmedLeft (8);
    mid.removeFromLeft (8);
    auto c3 = mid.removeFromLeft (mid.getWidth() * 55 / 100);
    focusPanel.setBounds (c3);
    focusArea = focusPanel.content() + c3.getPosition();
    mid.removeFromLeft (8);
    auto e = mid.removeFromTop (mid.getHeight() / 2 - 4);
    envPanel.setBounds (e);
    envArea = envPanel.content() + e.getPosition();
    mid.removeFromTop (8);
    histPanel.setBounds (mid);
    histArea = histPanel.content() + mid.getPosition();
}

void AnalyzePage::animate()
{
    if (! animVisible (this)) return;
    auto& t = ctx.proc.engine.telemetry;
    const float note = t.leadNote.load();
    harmonics.f0 = note > 0 ? (float) noteToHz (note, ctx.getParam ("v_tune")) : 0.0f;
    harmonics.repaint();
    // fundamental detector on the output
    if (frame++ % 4 == 0)
    {
        t.scope.copyLatest (scopeBuf.data(), 4096);
        const double sr = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
        detectedHz = detectPitchHz (scopeBuf.data(), 4096, sr, 25.0f, 800.0f);
        // sub stability: spread of the sub level over recent frames
        std::vector<float> sub (2048);
        t.subTap.copyLatest (sub.data(), 2048);
        float rms = 0; for (auto v : sub) rms += v * v; rms = std::sqrt (rms / 2048.0f);
        if (rms > 1.0e-3f)
        {
            subMin = std::min (subMin * 1.002f + 0.0001f, rms); subMax = std::max (subMax * 0.998f, rms);
            subStability = subMax > 0 ? jlimit (0.0f, 1.0f, subMin / subMax) : 1.0f;
        }
        report = BassFocus::diagnose (spectrum.magnitudes(), spectrum.binHz(), t.lowCorrelation.load(), t.correlation.load(), subStability,
                                      std::max (t.peakL.load(), t.peakR.load()), std::max (t.rmsL.load(), t.rmsR.load()), note);
    }
    repaint (fundArea); repaint (meterArea); repaint (focusArea); repaint (envArea); repaint (histArea);
}

void AnalyzePage::paintOverChildren (Graphics& g)
{
    auto& t = ctx.proc.engine.telemetry;
    // fundamental
    {
        auto r = fundArea.toFloat();
        const float note = t.leadNote.load();
        String name = "--", hz = "-- Hz", cents;
        if (detectedHz > 0.0f && note > 0.0f)
        {
            const double midi = 69.0 + 12.0 * std::log2 (detectedHz / ctx.getParam ("v_tune"));
            const int n = (int) std::round (midi);
            name = MidiMessage::getMidiNoteName (n, true, true, 3);
            hz = String (detectedHz, 2) + " Hz";
            const int ct = roundToInt ((midi - n) * 100.0);
            cents = (ct >= 0 ? "+" : "") + String (ct) + " ct";
        }
        g.setFont (Fonts::title (40.0f)); g.setColour (S::ink);
        g.drawText (::racoon::i18n::tr (name), r.removeFromTop (54.0f), Justification::centred);
        g.setFont (Fonts::mono (16.0f)); g.setColour (S::ink);
        g.drawText (::racoon::i18n::tr (hz + "   " + cents), r.removeFromTop (22.0f), Justification::centred);
        g.setFont (Fonts::body (13.0f)); g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr ("Voices " + String (t.activeVoices.load()) + "   /   CPU " + String (roundToInt (ctx.proc.cpuLoad.load() * 100.0f)) + "%"), r.removeFromTop (20.0f), Justification::centred);
    }
    // meters, correlation, phase
    {
        auto r = meterArea.toFloat();
        auto dbs = [] (float v) { return v > 1.0e-5f ? String (20.0f * std::log10 (v), 1) : String ("-inf"); };
        g.setFont (Fonts::label (11.5f)); g.setColour (S::inkSoft);
        auto line = [&] (const String& l, const String& v, Colour c)
        {
            auto row = r.removeFromTop (18.0f);
            g.setFont (Fonts::label (11.5f)); g.setColour (S::inkSoft); g.drawText (::racoon::i18n::tr (l), row, Justification::centredLeft);
            g.setFont (Fonts::mono (12.0f)); g.setColour (c); g.drawText (::racoon::i18n::tr (v), row, Justification::centredRight);
        };
        line ("PEAK L / R", dbs (t.peakL.load()) + " / " + dbs (t.peakR.load()), S::ink);
        line ("RMS L / R", dbs (t.rmsL.load()) + " / " + dbs (t.rmsR.load()), S::ink);
        r.removeFromTop (8.0f);
        auto bar = [&] (const String& l, float v, bool warnNeg)
        {
            g.setFont (Fonts::label (11.5f)); g.setColour (S::inkSoft);
            g.drawText (::racoon::i18n::tr (l), r.removeFromTop (16.0f), Justification::centredLeft);
            auto b = r.removeFromTop (12.0f);
            g.setColour (C::engrave); g.fillRect (b);
            const float x = b.getCentreX() + b.getWidth() * 0.5f * jlimit (-1.0f, 1.0f, v);
            g.setColour (C::ridge); g.drawVerticalLine ((int) b.getCentreX(), b.getY(), b.getBottom());
            g.setColour (warnNeg && v < 0 ? C::ochre : C::jade);
            g.fillRect (Rectangle<float> (std::min (x, b.getCentreX()), b.getY() + 2.0f, std::abs (x - b.getCentreX()) + 2.0f, b.getHeight() - 4.0f));
            r.removeFromTop (6.0f);
        };
        bar ("STEREO CORRELATION", t.correlation.load(), true);
        bar ("SUB vs LAYERS PHASE (<120 Hz)", t.lowCorrelation.load(), true);
        bar ("SUB STABILITY", subStability * 2.0f - 1.0f, true);
        const bool cancel = t.lowCorrelation.load() < -0.2f;
        g.setFont (Fonts::label (12.0f));
        g.setColour (cancel ? Brand::rosa.darker (0.4f) : S::ink);
        g.drawText (::racoon::i18n::tr (cancel ? "CANCELLATION DETECTED" : "PHASE OK"), r.removeFromTop (18.0f), Justification::centredLeft);
        line ("DUCK GR", String (t.duckGr.load(), 1) + " dB", S::ink);
    }
    // bass focus
    {
        auto r = focusArea.toFloat();
        for (auto& it : report.items)
        {
            auto row = r.removeFromTop (40.0f);
            const Colour c = it.severity == 2 ? C::ochre : (it.severity == 1 ? C::amber : C::jade);
            Path d; d.addPolygon ({ row.getX() + 6.0f, row.getY() + 9.0f }, 4, 4.5f, MathConstants<float>::pi / 4.0f);
            g.setColour (c); g.fillPath (d);
            g.setFont (Fonts::body (14.0f)); g.setColour (S::ink);
            g.drawFittedText (::racoon::i18n::tr (it.text), row.withTrimmedLeft (16.0f).toNearestInt(), Justification::topLeft, 2, 0.9f);
        }
        g.setFont (Fonts::body (12.0f)); g.setColour (S::inkSoft);
        g.drawText (::racoon::i18n::tr ("Suggestions only. Nothing is changed automatically."), r.removeFromBottom (16.0f), Justification::centredLeft);
    }
    // modulators: live env values and first LFOs
    {
        auto r = envArea.toFloat();
        const int n = kNumEnvs + 8;
        const float w = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const int src = i < kNumEnvs ? ModSrc::Env1 + i : ModSrc::Lfo1 + (i - kNumEnvs);
            const float v = t.srcDisplay[src].load();
            auto b = Rectangle<float> (r.getX() + (float) i * w + 2.0f, r.getY(), w - 4.0f, r.getHeight() - 14.0f);
            g.setColour (C::engrave); g.fillRect (b);
            g.setColour (i < kNumEnvs ? C::amber : C::cyan);
            if (i < kNumEnvs) g.fillRect (b.withTop (b.getBottom() - b.getHeight() * jlimit (0.0f, 1.0f, v)));
            else { const float y = b.getCentreY() - b.getHeight() * 0.5f * jlimit (-1.0f, 1.0f, v); g.fillRect (b.getX(), std::min (y, b.getCentreY()), b.getWidth(), std::abs (y - b.getCentreY()) + 1.0f); }
            g.setFont (Fonts::mono (9.0f)); g.setColour (S::inkSoft);
            g.drawText (::racoon::i18n::tr (i < kNumEnvs ? "E" + String (i + 1) : "L" + String (i - kNumEnvs + 1)), Rectangle<float> (b.getX(), r.getBottom() - 12.0f, b.getWidth(), 12.0f), Justification::centred);
        }
    }
    // MIDI history
    {
        auto r = histArea.toFloat();
        const int pos = t.historyPos.load();
        const int rows = std::max (1, (int) (r.getHeight() / 15.0f));
        g.setFont (Fonts::mono (11.0f));
        for (int k = 0; k < std::min (rows * 2, 64) && k < pos; ++k)
        {
            const auto& e = t.history[(pos - 1 - k) & 63];
            const int col = k / rows, row = k % rows;
            if (col > 1) break;
            auto cell = Rectangle<float> (r.getX() + (float) col * r.getWidth() * 0.5f, r.getY() + (float) row * 15.0f, r.getWidth() * 0.5f, 15.0f);
            g.setColour (e.on ? S::ink : S::inkFaint);
            g.drawText (::racoon::i18n::tr ((e.on ? "ON  " : "OFF ") + MidiMessage::getMidiNoteName (e.note, true, true, 3).paddedRight (' ', 4) + (e.on ? " v" + String (e.vel) : String())),
                        cell, Justification::centredLeft);
        }
    }
}

// ===================================================================== PRESETS
PresetPage::PresetPage (Ctx& c) : ctx (c), xy (c), xyOn (c, "xy_on", "MORPH ACTIVE")
{
    for (Component* comp : std::initializer_list<Component*> { &filterPanel, &listPanel, &dnaPanel, &randPanel, &morphPanel, &search, &category, &genre, &character,
                                                               &synth, &bank, &favOnly, &minIntensity, &maxComplexity, &list, &loadBtn, &saveBtn, &deleteBtn,
                                                               &previewBtn, &favBtn, &dnaLoad, &dnaApply, &dnaSummary, &randAmount, &lockOsc, &lockSub, &lockFilter,
                                                               &lockEnv, &lockLfo, &lockFx, &lockMatrix, &keepFund, &keepVol, &keepChar, &randBtn, &xy, &xyOn })
        addAndMakeVisible (comp);
    search.setTextToShowWhenEmpty (::racoon::i18n::tr ("buscar"), S::inkFaint);
    search.setColour (TextEditor::textColourId, S::ink);
    search.onTextChange = [this] { applyFilter(); };
    for (auto* cb : { &category, &genre, &character, &synth, &bank }) cb->onChange = [this] { applyFilter(); };
    favOnly.onClick = [this] { applyFilter(); };
    for (auto* s : { &minIntensity, &maxComplexity })
    {
        s->setSliderStyle (Slider::LinearHorizontal);
        s->setRange (1, 5, 1);
        s->setTextBoxStyle (Slider::TextBoxRight, false, 24, 18);
        s->onValueChange = [this] { applyFilter(); };
    }
    minIntensity.setValue (1, dontSendNotification);
    maxComplexity.setValue (5, dontSendNotification);
    minIntensity.setTooltip ("Minimum intensity");
    maxComplexity.setTooltip ("Maximum complexity");
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    loadBtn.onClick = [this] { const int r = list.getSelectedRow(); if (r >= 0) ctx.proc.presets().load (shown[(size_t) r]); };
    saveBtn.onClick = [this] { saveDialog(); };
    deleteBtn.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r < 0 || shown[(size_t) r].factory) return;
        ctx.proc.presets().deleteUser (shown[(size_t) r]);
        refresh();
    };
    previewBtn.setTooltip ("Play a short bass phrase with the current patch");
    previewBtn.onClick = [this] { ctx.proc.triggerPreview(); };
    favBtn.onClick = [this]
    {
        const int r = list.getSelectedRow();
        if (r < 0) return;
        ctx.proc.presets().setFavourite (shown[(size_t) r], ! shown[(size_t) r].favourite);
        refresh();
    };
    // DNA
    dnaSummary.setFont (Fonts::body (13.5f));
    dnaSummary.setColour (Label::textColourId, S::inkSoft);
    dnaSummary.setJustificationType (Justification::topLeft);
    dnaSummary.setText ("Drop a bass sample here (wav, aiff, flac) or click ANALYSE. Bass DNA measures pitch, envelope, harmonics and noise, then proposes an editable patch. The result is an approximation, not a copy.", dontSendNotification);
    dnaApply.setEnabled (false);
    dnaLoad.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Bass DNA: choose a bass sample", File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        { if (fc.getResult().existsAsFile()) analyse (fc.getResult()); });
    };
    dnaApply.onClick = [this] { BassDNA::apply (ctx.proc, dna, dnaName); if (ctx.proc.onPresetLoaded) ctx.proc.onPresetLoaded(); };
    // randomizer
    randAmount.setSliderStyle (Slider::LinearHorizontal);
    randAmount.setRange (0.05, 1.0, 0.01);
    randAmount.setValue (0.35);
    randAmount.setTextBoxStyle (Slider::TextBoxRight, false, 44, 18);
    randAmount.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100)) + "%"; };
    randAmount.updateText();
    keepFund.setToggleState (true, dontSendNotification);
    keepVol.setToggleState (true, dontSendNotification);
    randBtn.onClick = [this]
    {
        RandomLocks L;
        L.osc = lockOsc.getToggleState(); L.sub = lockSub.getToggleState(); L.filter = lockFilter.getToggleState(); L.env = lockEnv.getToggleState();
        L.lfo = lockLfo.getToggleState(); L.fx = lockFx.getToggleState(); L.matrix = lockMatrix.getToggleState();
        L.fundamental = keepFund.getToggleState(); L.volume = keepVol.getToggleState(); L.character = keepChar.getToggleState();
        Randomizer::randomize (ctx.proc, (float) randAmount.getValue(), L, (uint32) Time::getMillisecondCounter());
        if (ctx.proc.onPresetLoaded) ctx.proc.onPresetLoaded();
    };
    // morph corners
    const char* corner[4] { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        auto* b = cornerBtns.add (new TextButton (String ("SET ") + corner[i]));
        b->setTooltip ("Store the current sound in corner " + String (corner[i]));
        b->onClick = [this, i] { ctx.proc.captureMorphCorner (i); xy.repaint(); };
        addAndMakeVisible (b);
    }
    category.addItem ("All categories", 1);
    for (int i = 0; i < presetCategoryNames().size(); ++i) category.addItem (presetCategoryNames()[i], i + 2);
    category.setSelectedId (1, dontSendNotification);
    refresh();
}

void PresetPage::refresh()
{
    all = ctx.proc.presets().list();
    auto fill = [this] (ComboBox& cb, const String& allName, std::function<String (const PresetInfo&)> get)
    {
        const auto cur = cb.getText();
        StringArray items;
        for (auto& p : all) if (get (p).isNotEmpty()) items.addIfNotAlreadyThere (get (p));
        items.sort (true);
        cb.clear (dontSendNotification);
        cb.addItem (allName, 1);
        for (int i = 0; i < items.size(); ++i) cb.addItem (items[i], i + 2);
        const int idx = items.indexOf (cur);
        cb.setSelectedId (idx >= 0 ? idx + 2 : 1, dontSendNotification);
    };
    fill (genre, "All genres", [] (const PresetInfo& p) { return p.genre; });
    fill (character, "All characters", [] (const PresetInfo& p) { return p.character; });
    fill (synth, "All synthesis", [] (const PresetInfo& p) { return p.synthType; });
    fill (bank, "All banks", [] (const PresetInfo& p) { return p.bank; });
    applyFilter();
}

void PresetPage::applyFilter()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (auto& p : all)
    {
        if (category.getSelectedId() > 1 && p.category != category.getText()) continue;
        if (genre.getSelectedId() > 1 && p.genre != genre.getText()) continue;
        if (character.getSelectedId() > 1 && p.character != character.getText()) continue;
        if (synth.getSelectedId() > 1 && p.synthType != synth.getText()) continue;
        if (bank.getSelectedId() > 1 && p.bank != bank.getText()) continue;
        if (favOnly.getToggleState() && ! p.favourite) continue;
        if (p.intensity < (int) minIntensity.getValue() || p.complexity > (int) maxComplexity.getValue()) continue;
        if (q.isNotEmpty() && ! (p.name.toLowerCase().contains (q) || p.category.toLowerCase().contains (q) || p.genre.toLowerCase().contains (q)
                                 || p.character.toLowerCase().contains (q) || p.synthType.toLowerCase().contains (q))) continue;
        shown.push_back (p);
    }
    list.updateContent();
    list.repaint();
}

void PresetPage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row];
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (selected ? S::hi : (row % 2 ? S::ink.withAlpha (0.05f) : Colours::transparentBlack));
    g.fillRect (r);
    const bool current = p.name == ctx.proc.currentPresetName;
    Path star; star.addStar ({ 12.0f, (float) h * 0.5f }, 5, 2.5f, 6.0f);
    g.setColour (p.favourite ? S::accent() : S::inkFaint);
    if (p.favourite) { g.fillPath (star); g.setColour (S::ink); g.strokePath (star, PathStrokeType (0.8f)); } else g.strokePath (star, PathStrokeType (0.8f));
    g.setFont (current ? Fonts::bodyBold (15.0f) : Fonts::body (15.0f));
    g.setColour (S::ink);
    g.drawText (::racoon::i18n::tr (p.name), r.withTrimmedLeft (26.0f).withWidth ((float) w * 0.40f), Justification::centredLeft);
    g.setFont (Fonts::label (11.5f));
    g.setColour (S::inkSoft);
    g.drawText (::racoon::i18n::tr (p.category.toUpperCase()), r.withTrimmedLeft ((float) w * 0.46f).withWidth ((float) w * 0.22f), Justification::centredLeft);
    g.drawText (::racoon::i18n::tr (p.synthType.toUpperCase()), r.withTrimmedLeft ((float) w * 0.68f).withWidth ((float) w * 0.18f), Justification::centredLeft);
    g.setColour (S::ink);
    for (int k = 0; k < p.intensity; ++k) g.fillRect ((float) w - 50.0f + (float) k * 8.0f, (float) h * 0.5f - 3.0f, 5.0f, 6.0f);
    if (! p.factory) { g.setColour (S::accent()); g.fillEllipse ((float) w - 60.0f, (float) h * 0.5f - 2.0f, 4.0f, 4.0f); }
}

void PresetPage::listBoxItemDoubleClicked (int row, const MouseEvent&)
{
    if (row >= 0 && row < (int) shown.size()) { ctx.proc.presets().load (shown[(size_t) row]); list.repaint(); }
}

void PresetPage::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (e.x < 24) { ctx.proc.presets().setFavourite (shown[(size_t) row], ! shown[(size_t) row].favourite); refresh(); }
}

void PresetPage::saveDialog()
{
    auto* w = new AlertWindow ("Save preset", "Name and tags for your patch", MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", ctx.proc.currentPresetName == "Init" ? String ("My Bass") : ctx.proc.currentPresetName, "Name");
    w->addComboBox ("category", presetCategoryNames(), "Category");
    w->addTextEditor ("genre", "House", "Genre");
    w->addTextEditor ("character", "Warm", "Character");
    w->addComboBox ("intensity", { "1", "2", "3", "4", "5" }, "Intensity");
    w->addComboBox ("complexity", { "1", "2", "3", "4", "5" }, "Complexity");
    w->addTextEditor ("bank", "User", "Bank");
    w->getComboBoxComponent ("intensity")->setSelectedItemIndex (2);
    w->getComboBoxComponent ("complexity")->setSelectedItemIndex (2);
    w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
    w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r != 1) return;
        ctx.proc.presets().saveUser (w->getTextEditorContents ("name"), w->getComboBoxComponent ("category")->getText(), w->getTextEditorContents ("genre"),
                                     w->getTextEditorContents ("character"), w->getComboBoxComponent ("intensity")->getSelectedItemIndex() + 1,
                                     w->getComboBoxComponent ("complexity")->getSelectedItemIndex() + 1, w->getTextEditorContents ("bank"));
        refresh();
        if (ctx.proc.onPresetLoaded) ctx.proc.onPresetLoaded();
    }), true);
}

void PresetPage::analyse (const File& f)
{
    dnaSummary.setText ("Analysing " + f.getFileName() + " ...", dontSendNotification);
    dna = BassDNA::analyseFile (f);
    dnaName = f.getFileNameWithoutExtension();
    dnaSummary.setText (f.getFileName() + "\n" + dna.summary, dontSendNotification);
    dnaApply.setEnabled (dna.ok);
    repaint();
}

void PresetPage::filesDropped (const StringArray& files, int, int)
{
    if (! files.isEmpty()) analyse (File (files[0]));
}

void PresetPage::paintOverChildren (Graphics& g)
{
    Draw::engraved (g, "MIN INTENSITY", minIntensity.getBounds().translated (0, -13).withHeight (13).toFloat(), Fonts::label (11.0f), Justification::centredLeft, C::boneDim);
    Draw::engraved (g, "MAX COMPLEXITY", maxComplexity.getBounds().translated (0, -13).withHeight (13).toFloat(), Fonts::label (11.0f), Justification::centredLeft, C::boneDim);
    Draw::engraved (g, "AMOUNT", randAmount.getBounds().translated (0, -13).withHeight (13).toFloat(), Fonts::label (11.0f), Justification::centredLeft, C::boneDim);
    Draw::engraved (g, "LOCK", lockOsc.getBounds().translated (0, -13).withHeight (13).toFloat(), Fonts::label (11.0f), Justification::centredLeft, C::boneDim);
    // DNA drop zone glyph (behind the summary text)
    auto r = dnaSummary.getBounds().toFloat().expanded (4.0f);
    g.setColour (S::inkSoft);
    const float dash[] { 4.0f, 4.0f };
    Path p; p.addRectangle (r);
    Path dashed; PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
    g.fillPath (dashed);
    // a DNA helix sketch
    if (dna.ok && ! dna.frames.empty())
    {
        auto h = Rectangle<float> (r.getX(), r.getBottom() + 6.0f, r.getWidth(), 34.0f);
        const auto& fr = dna.frames[1];
        const float bw = h.getWidth() / 32.0f;
        for (int k = 0; k < 32; ++k)
        {
            g.setColour (S::ink);
            const float v = fr[(size_t) k];
            g.fillRect (h.getX() + (float) k * bw + 1.0f, h.getBottom() - v * h.getHeight(), bw - 2.0f, v * h.getHeight());
        }
    }
}

void PresetPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (240);
    filterPanel.setBounds (left);
    auto f = filterPanel.content() + left.getPosition();
    search.setBounds (f.removeFromTop (26)); f.removeFromTop (8);
    for (auto* cb : { &category, &genre, &character, &synth, &bank }) { cb->setBounds (f.removeFromTop (24)); f.removeFromTop (6); }
    favOnly.setBounds (f.removeFromTop (24)); f.removeFromTop (20);
    minIntensity.setBounds (f.removeFromTop (24)); f.removeFromTop (20);
    maxComplexity.setBounds (f.removeFromTop (24));
    r.removeFromLeft (8);
    auto mid = r.removeFromLeft (r.getWidth() * 52 / 100);
    listPanel.setBounds (mid);
    auto l = listPanel.content() + mid.getPosition();
    auto btns = l.removeFromBottom (30);
    const int bw = btns.getWidth() / 5;
    for (auto* b : { &loadBtn, &saveBtn, &deleteBtn, &favBtn, &previewBtn }) b->setBounds (btns.removeFromLeft (bw).reduced (2, 0));
    l.removeFromBottom (6);
    list.setBounds (l);
    r.removeFromLeft (8);
    auto dnaR = r.removeFromTop (230);
    dnaPanel.setBounds (dnaR);
    auto d = dnaPanel.content() + dnaR.getPosition();
    auto db = d.removeFromBottom (28);
    dnaLoad.setBounds (db.removeFromLeft (db.getWidth() / 2).reduced (2, 0));
    dnaApply.setBounds (db.reduced (2, 0));
    d.removeFromBottom (44);
    dnaSummary.setBounds (d.reduced (4));
    r.removeFromTop (8);
    auto rand = r.removeFromTop (150);
    randPanel.setBounds (rand);
    auto rr = randPanel.content() + rand.getPosition();
    rr.removeFromTop (12);
    auto amtRow = rr.removeFromTop (24);
    randBtn.setBounds (amtRow.removeFromRight (120));
    randAmount.setBounds (amtRow.reduced (0, 2));
    rr.removeFromTop (16);
    auto locks = rr.removeFromTop (22);
    const int lw = locks.getWidth() / 7;
    for (auto* t : { &lockOsc, &lockSub, &lockFilter, &lockEnv, &lockLfo, &lockFx, &lockMatrix }) t->setBounds (locks.removeFromLeft (lw));
    rr.removeFromTop (4);
    auto keeps = rr.removeFromTop (22);
    const int kw = keeps.getWidth() / 3;
    for (auto* t : { &keepFund, &keepVol, &keepChar }) t->setBounds (keeps.removeFromLeft (kw));
    r.removeFromTop (8);
    morphPanel.setBounds (r);
    auto m = morphPanel.content() + r.getPosition();
    auto side = m.removeFromRight (120);
    xyOn.setBounds (side.removeFromTop (24));
    side.removeFromTop (8);
    for (auto* b : cornerBtns) { b->setBounds (side.removeFromTop (26)); side.removeFromTop (6); }
    xy.setBounds (m.withSizeKeepingCentre (std::min (m.getWidth(), m.getHeight() + 80), m.getHeight()));
}
} // namespace be::ui

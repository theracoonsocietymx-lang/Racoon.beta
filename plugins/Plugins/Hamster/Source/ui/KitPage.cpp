#include "Pages.h"
#include "I18n.h"
#include "Biblioteca.h"
#include "PanelMuestras.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== track page base
TrackPage::TrackPage (Ctx& c) : ctx (c), selector (c)
{
    addAndMakeVisible (selector);
    ctx.onTrackChanged ([this] { if (isShowing() || gForceAnimation) { rebuild(); resized(); } selector.repaint(); });
}

KnobPtr TrackPage::knob (int local, const String& label)
{
    auto k = std::make_unique<ParamKnob> (ctx, ctx.tid (local), label);
    addAndMakeVisible (*k);
    return k;
}

BoxPtr TrackPage::box (int local, const String& label)
{
    auto b = std::make_unique<ChoiceBox> (ctx, ctx.tid (local), label);
    addAndMakeVisible (*b);
    return b;
}

TogglePtr TrackPage::toggle (int local, const String& text)
{
    auto t = std::make_unique<Toggle> (ctx, ctx.tid (local), text);
    addAndMakeVisible (*t);
    return t;
}

StepperPtr TrackPage::stepper (int local, const String& label, std::function<String (int)> fmt)
{
    auto s = std::make_unique<Stepper> (ctx, ctx.tid (local), label, std::move (fmt));
    addAndMakeVisible (*s);
    return s;
}

// ===================================================================== pads
PadGrid::PadGrid (Ctx& c, bool b) : ctx (c), big (b) { ctx.registerAnimated (this); }
PadGrid::~PadGrid() { ctx.unregisterAnimated (this); }

Rectangle<float> PadGrid::padRect (int t) const
{
    const int row = 3 - t / 4, col = t % 4;     // pad 1 bottom-left like hardware
    const float w = (float) getWidth() / 4.0f, h = (float) getHeight() / 4.0f;
    return Rectangle<float> ((float) col * w, (float) row * h, w, h).reduced (big ? 6.0f : 4.0f);
}

int PadGrid::padAt (Point<float> p) const
{
    for (int t = 0; t < kNumTracks; ++t) if (padRect (t).contains (p)) return t;
    return -1;
}

void PadGrid::paint (Graphics& g)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto r = padRect (t);
        const bool sel = t == ctx.track();
        const bool muted = ctx.proc.trackValue (t, T_MUTE) > 0.5f;
        // flat brutalist pad: Ciruela face, Limón when selected, Menta flash on each hit
        Draw::box (g, r, sel ? S::hi : C::void_, 2.0f, 4.0f);
        auto face = r.withTrimmedRight (4.0f).withTrimmedBottom (4.0f);
        if (flash[t] > 0.01f)
        {
            g.setColour (S::accent().withAlpha (0.9f * flash[t]));
            g.fillRect (face.reduced (2.0f));
        }
        if (hoverPad == t) { g.setColour (Colours::white.withAlpha (0.12f)); g.fillRect (face); }
        // the eyes of the family: two little squares
        {
            const float e = face.getHeight() * 0.09f;
            const auto c = face.getCentre().translated (0.0f, -face.getHeight() * 0.06f);
            g.setColour ((sel ? S::ink : C::bone).withAlpha (muted ? 0.3f : 0.85f));
            g.fillRect (Rectangle<float> (e, e).withCentre (c.translated (-e * 1.4f, 0.0f)));
            g.fillRect (Rectangle<float> (e, e).withCentre (c.translated (e * 1.4f, 0.0f)));
        }
        r = face;
        g.setFont (Fonts::mono (big ? 12.0f : 10.0f));
        g.setColour (sel ? S::inkSoft : C::boneDim);
        g.drawText (::racoon::i18n::tr (String (t + 1)), r.reduced (7.0f, 5.0f), Justification::topLeft);
        const int model = (int) ctx.proc.trackValue (t, T_MODEL);
        const bool circ = ctx.proc.trackValue (t, T_CIRCUIT_ON) > 0.5f;
        g.drawText (::racoon::i18n::tr (circ ? String ("MORPH") : modelNames()[model]), r.reduced (7.0f, 5.0f), Justification::topRight);
        g.setFont (Fonts::label (big ? 16.0f : 13.0f));
        g.setColour (muted ? C::ash : (sel ? S::ink : C::bone));
        g.drawFittedText (::racoon::i18n::tr (ctx.proc.getTrackName (t).toUpperCase()), r.reduced (6.0f, 6.0f).toNearestInt(), Justification::centredBottom, 1, 0.6f);
        if (muted) { g.setFont (Fonts::mono (10.0f)); g.setColour (Brand::rosa); g.drawText (::racoon::i18n::tr ("MUTE"), r.reduced (7.0f, 5.0f), Justification::centredTop); }
    }
}

void PadGrid::mouseDown (const MouseEvent& e)
{
    const int t = padAt (e.position);
    if (t < 0) return;
    ctx.selectTrack (t);
    if (e.mods.isPopupMenu()) { if (onMenu) onMenu (t); repaint(); return; }
    const auto r = padRect (t);
    const float vel = jlimit (0.15f, 1.0f, 0.35f + 0.65f * (1.0f - (e.position.y - r.getY()) / r.getHeight()) * 1.2f);
    ctx.proc.padDown (t, vel);
    downPad = t;
    repaint();
}

void PadGrid::mouseUp (const MouseEvent&)
{
    if (downPad >= 0) ctx.proc.padUp (downPad);
    downPad = -1;
}

void PadGrid::animate()
{
    if (! animVisible (this)) return;
    bool any = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const int h = ctx.proc.engine.telemetry.trackHits[t].load (std::memory_order_relaxed);
        if (h != lastHits[t]) { lastHits[t] = h; flash[t] = 1.0f; any = true; }
        else if (flash[t] > 0.01f) { flash[t] *= 0.8f; any = true; }
    }
    if (any) repaint();
}

void PadGrid::filesDropped (const StringArray& files, int x, int y)
{
    hoverPad = -1;
    const int t = padAt ({ (float) x, (float) y });
    if (t < 0 || files.isEmpty()) { repaint(); return; }
    ctx.proc.undo.beginNewTransaction ("Load sample");
    if (ctx.proc.loadTrackSample (t, 0, File (files[0])))
    {
        // a dropped sample replaces the synth layers so the pad plays the file
        ctx.proc.setTrackValue (t, T_A_ON, 0.0f);
        ctx.proc.setTrackValue (t, T_B_ON, 0.0f);
        ctx.proc.setTrackValue (t, T_CIRCUIT_ON, 0.0f);
        ctx.proc.setTrackValue (t, T_C_LEVEL, 0.9f);
        ctx.proc.setTrackName (t, File (files[0]).getFileNameWithoutExtension().substring (0, 14));
    }
    ctx.selectTrack (t);
    if (ctx.refreshAll) ctx.refreshAll();
    repaint();
}

// ===================================================================== kit page
KitPage::KitPage (Ctx& c) : TrackPage (c), pads (c, true), circuitView (c)
{
    for (Component* comp : std::initializer_list<Component*> { &padPanel, &trackPanel, &layerPanel, &morphPanel, &toolPanel })
        addAndMakeVisible (comp), comp->toBack();
    addAndMakeVisible (pads);
    addAndMakeVisible (circuitView);
    addAndMakeVisible (nameEditor);
    nameEditor.setFont (Fonts::label (16.0f));
    nameEditor.setJustification (Justification::centredLeft);
    nameEditor.setTextToShowWhenEmpty (::racoon::i18n::tr ("nombre de la pista"), S::inkFaint);
    nameEditor.setColour (TextEditor::textColourId, S::ink);
    nameEditor.onReturnKey = [this] { ctx.proc.setTrackName (ctx.track(), nameEditor.getText()); repaint(); if (ctx.refreshAll) ctx.refreshAll(); };
    nameEditor.onFocusLost = nameEditor.onReturnKey;
    pads.onMenu = [this] (int t) { padMenu (t); };
    for (auto* b : { &copyBtn, &pasteBtn, &resetBtn, &dnaBtn, &saveSnd, &loadSnd, &clearSmp, &auditionBtn, &notesChrom, &notesGm, &libBtn, &libKitBtn }) addAndMakeVisible (b);
    notesChrom.setTooltip ("Pads answer MIDI notes C1 to D#2 (36-51), the usual layout of pad controllers");
    notesGm.setTooltip ("General MIDI drum notes per role (kick 36, snare 38, closed hat 42...)");
    notesChrom.onClick = [this] { ctx.proc.mapNotes (true); rebuild(); resized(); };
    notesGm.onClick = [this] { ctx.proc.mapNotes (false); rebuild(); resized(); };
    addAndMakeVisible (dnaInfo);
    dnaInfo.setFont (Fonts::body (13.0f));
    dnaInfo.setColour (Label::textColourId, S::inkSoft);
    dnaInfo.setText ("Drop a file on a pad to load it. DRUM DNA rebuilds a one-shot as a synth voice.", dontSendNotification);
    dnaInfo.setJustificationType (Justification::topLeft);

    copyBtn.onClick = [this] { clipboardTrack = ctx.track(); };
    pasteBtn.onClick = [this] { if (clipboardTrack >= 0) { ctx.proc.undo.beginNewTransaction ("Paste track"); ctx.proc.copyTrack (clipboardTrack, ctx.track()); rebuild(); resized(); if (ctx.refreshAll) ctx.refreshAll(); } };
    resetBtn.onClick = [this]
    {
        PopupMenu m;
        for (int r = 0; r < (int) Role::Count; ++r) m.addItem (r + 1, roleNames()[r]);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&resetBtn), [this] (int r)
        {
            if (r <= 0) return;
            ctx.proc.undo.beginNewTransaction ("Reset track");
            ctx.proc.resetTrack (ctx.track(), r - 1);
            rebuild(); resized();
            if (ctx.refreshAll) ctx.refreshAll();
        });
    };
    dnaBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Drum DNA: choose a one-shot", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) runDna (fc.getResult(), 1);
        });
    };
    saveSnd.onClick = [this]
    {
        auto* w = new AlertWindow ("Save sound", "Name for this track sound", AlertWindow::NoIcon);
        w->addTextEditor ("name", ctx.proc.getTrackName (ctx.track()));
        w->addButton ("SAVE", 1); w->addButton ("CANCEL", 0);
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1) ctx.proc.presets().saveSound (ctx.track(), w->getTextEditorContents ("name"));
            delete w;
        }), false);
    };
    loadSnd.onClick = [this]
    {
        PopupMenu m;
        auto names = ctx.proc.presets().soundNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        if (names.isEmpty()) m.addItem (-1, "No saved sounds yet", false);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&loadSnd), [this, names] (int r)
        {
            if (r <= 0) return;
            ctx.proc.undo.beginNewTransaction ("Load sound");
            ctx.proc.presets().loadSound (ctx.track(), names[r - 1]);
            rebuild(); resized();
        });
    };
    clearSmp.onClick = [this] { for (int k = 0; k < kNumSlots; ++k) ctx.proc.clearTrackSample (ctx.track(), k); };
    auditionBtn.onClick = [this] { ctx.proc.padDown (ctx.track(), 0.85f); };
    libBtn.setTooltip ("Muestras Racoon: sonidos reales para esta pista, con sus capas de velocidad");
    libKitBtn.setTooltip ("Un kit completo de la biblioteca en las 16 pistas; los patrones se quedan");
    libBtn.onClick = [this] { libraryMenu (ctx.track(), &libBtn); };
    libKitBtn.onClick = [this]
    {
        PopupMenu m;
        const auto& cat = racoon::biblio::Catalogo::get();
        if (cat.kits.empty()) m.addItem (racoon::biblio::avisoSinBiblioteca(), false, false, [] {});
        for (auto& kit : cat.kits)
        {
            const auto id = kit.id;
            m.addItem (kit.nombre + "   " + kit.generos.joinIntoString (", "), [this, id]
            {
                ctx.proc.undo.beginNewTransaction ("Kit de biblioteca");
                ctx.proc.loadLibraryKit (id);
                rebuild(); resized();
                if (ctx.refreshAll) ctx.refreshAll();
            });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&libKitBtn));
    };
    rebuild();
}

void KitPage::libraryMenu (int t, Component* target)
{
    // a panel to try one sound after another (it stays open, CERRAR or Esc puts it away)
    const int roleIdx = (int) ctx.proc.trackValue (t, T_ROLE);
    const auto cats = racoon::biblio::Catalogo::categoriasDeRol (roleNames()[roleIdx]);
    if (! racoon::biblio::Catalogo::get().disponible())
    {
        PopupMenu m;
        m.addItem (racoon::biblio::avisoSinBiblioteca(), false, false, [] {});
        m.showMenuAsync (target != nullptr ? PopupMenu::Options().withTargetComponent (target) : PopupMenu::Options());
        return;
    }
    racoon::biblio::PanelMuestras::mostrar (*this, "Muestras para " + ctx.proc.getTrackName (t), cats, [this, t] (const String& grupo)
    {
        ctx.proc.undo.beginNewTransaction ("Muestra de biblioteca");
        if (ctx.proc.loadLibraryGroup (t, grupo)) ctx.proc.padDown (t, 0.8f);
        rebuild(); resized();
        if (ctx.refreshAll) ctx.refreshAll();
    }, ctx.proc.trackLibraryGroup (t));
}

void KitPage::runDna (const File& f, int mode)
{
    auto r = DrumDNA::analyse (f);
    if (! r.ok) { dnaInfo.setText (r.summary, dontSendNotification); return; }
    DrumDNA::apply (ctx.proc, ctx.track(), r, mode);
    dnaInfo.setText ("DNA: " + r.summary, dontSendNotification);
    rebuild(); resized();
    if (ctx.refreshAll) ctx.refreshAll();
}

void KitPage::padMenu (int t)
{
    PopupMenu m;
    m.addSectionHeader ("Pad " + String (t + 1) + "  /  " + ctx.proc.getTrackName (t));
    m.addItem (1, ctx.proc.trackValue (t, T_MUTE) > 0.5f ? "Unmute" : "Mute");
    m.addItem (2, "Solo");
    m.addItem (3, "Copy track");
    m.addItem (4, "Paste track", clipboardTrack >= 0);
    m.addItem (5, "Load sample...");
    m.addItem (6, "Drum DNA from file...");
    m.addItem (7, "Clear samples");
    m.addItem (8, "Muestras Racoon...");
    m.showMenuAsync (PopupMenu::Options(), [this, t] (int r)
    {
        switch (r)
        {
            case 1: ctx.proc.setTrackValue (t, T_MUTE, ctx.proc.trackValue (t, T_MUTE) > 0.5f ? 0.0f : 1.0f); break;
            case 2: ctx.proc.setTrackValue (t, T_SOLO, ctx.proc.trackValue (t, T_SOLO) > 0.5f ? 0.0f : 1.0f); break;
            case 3: clipboardTrack = t; break;
            case 4: ctx.proc.copyTrack (clipboardTrack, t); rebuild(); resized(); break;
            case 5: case 6:
            {
                chooser = std::make_unique<FileChooser> (r == 5 ? "Load sample" : "Drum DNA: choose a one-shot", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, r, t] (const FileChooser& fc)
                {
                    if (! fc.getResult().existsAsFile()) return;
                    if (r == 5) { ctx.proc.loadTrackSample (t, 0, fc.getResult()); ctx.proc.setTrackValue (t, T_A_ON, 0.0f); ctx.proc.setTrackValue (t, T_B_ON, 0.0f); }
                    else runDna (fc.getResult(), 1);
                });
                break;
            }
            case 7: for (int k = 0; k < kNumSlots; ++k) ctx.proc.clearTrackSample (t, k); break;
            case 8: libraryMenu (t, nullptr); break;
            default: break;
        }
        pads.repaint();
    });
}

void KitPage::rebuild()
{
    builtFor = ctx.track();
    role = box (T_ROLE, "ROLE");
    model = box (T_MODEL, "MODEL");
    choke = box (T_CHOKE, "CHOKE");
    poly = box (T_POLY, "VOICES");
    note = stepper (T_NOTE, "MIDI NOTE", [] (int v) { return MidiMessage::getMidiNoteName (v, true, true, 3) + " " + String (v); });
    level = knob (T_LEVEL, "LEVEL"); pan = knob (T_PAN, "PAN"); tune = knob (T_TUNE, "TUNE"); fine = knob (T_FINE, "FINE");
    decay = knob (T_DECAY, "DECAY"); velsens = knob (T_VELSENS, "VEL SENS"); drift = knob (T_A_DRIFT, "DRIFT");
    aOn = toggle (T_A_ON, "A  ANALOG"); bOn = toggle (T_B_ON, "B  FM"); cOn = toggle (T_C_ON, "C  SAMPLE"); dOn = toggle (T_D_ON, "D  TEXTURE");
    aLvl = knob (T_A_LEVEL, "LEVEL A"); bLvl = knob (T_B_LEVEL, "LEVEL B"); cLvl = knob (T_C_LEVEL, "LEVEL C"); dLvl = knob (T_D_LEVEL, "LEVEL D");
    morphOn = toggle (T_CIRCUIT_ON, "MORPH ON");
    circuit = knob (T_CIRCUIT, "POSITION");
    role->box.onChange = [this]
    {
        // keep sensible defaults when the role changes from the box
        MessageManager::callAsync ([sp = Component::SafePointer<KitPage> (this)] { if (sp == nullptr) return; if (sp->ctx.refreshAll) sp->ctx.refreshAll(); sp->pads.repaint(); sp->selector.repaint(); });
    };
    nameEditor.setText (ctx.proc.getTrackName (ctx.track()), false);
    nameEditor.applyColourToAllText (S::ink);
    repaint();
}

void KitPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    selector.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    auto left = r.removeFromLeft (r.getHeight() + 10);
    padPanel.setBounds (left);
    pads.setBounds (padPanel.content() + left.getPosition());
    r.removeFromLeft (8);
    auto top = r.removeFromTop (196);
    trackPanel.setBounds (top);
    auto tc = trackPanel.content() + top.getPosition();
    auto row1 = tc.removeFromTop (40);
    nameEditor.setBounds (row1.removeFromLeft (200).withTrimmedTop (13));
    row1.removeFromLeft (8);
    layoutRow (row1, { role.get(), model.get(), choke.get(), poly.get(), note.get() }, 8);
    tc.removeFromTop (8);
    layoutRow (tc.removeFromTop (92), { level.get(), pan.get(), tune.get(), fine.get(), decay.get(), velsens.get(), drift.get() });
    r.removeFromTop (8);
    auto mid = r.removeFromTop (190);
    auto lp = mid.removeFromLeft (mid.getWidth() / 2 - 4);
    layerPanel.setBounds (lp);
    auto lc = layerPanel.content() + lp.getPosition();
    auto toggles = lc.removeFromTop (24);
    layoutRow (toggles, { aOn.get(), bOn.get(), cOn.get(), dOn.get() }, 6);
    lc.removeFromTop (6);
    layoutRow (lc.removeFromTop (100), { aLvl.get(), bLvl.get(), cLvl.get(), dLvl.get() });
    mid.removeFromLeft (8);
    morphPanel.setBounds (mid);
    auto mc = morphPanel.content() + mid.getPosition();
    auto mtop = mc.removeFromTop (24);
    morphOn->setBounds (mtop.removeFromLeft (110));
    circuitView.setBounds (mc.removeFromTop (70));
    mc.removeFromTop (4);
    circuit->setBounds (mc.withSizeKeepingCentre (110, std::min (mc.getHeight(), 80)));
    r.removeFromTop (8);
    toolPanel.setBounds (r);
    auto oc = toolPanel.content() + r.getPosition();
    auto b1 = oc.removeFromTop (28);
    layoutRow (b1, { &copyBtn, &pasteBtn, &resetBtn, &auditionBtn }, 6);
    oc.removeFromTop (6);
    layoutRow (oc.removeFromTop (28), { &dnaBtn, &saveSnd, &loadSnd, &clearSmp }, 6);
    oc.removeFromTop (6);
    layoutRow (oc.removeFromTop (28), { &notesChrom, &notesGm, &libBtn, &libKitBtn }, 6);
    oc.removeFromTop (8);
    dnaInfo.setBounds (oc);
}

void KitPage::paintOverChildren (Graphics& g)
{
    // role specific parameter legend
    juce::ignoreUnused (g);
}
} // namespace be::ui

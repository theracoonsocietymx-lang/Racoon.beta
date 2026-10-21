#include "Pages.h"
#include "Biblioteca.h"

namespace be::ui
{
using namespace juce;

// ===================================================================== additive harmonics editor (call-out)
namespace
{
class HarmonicEditor : public Component
{
public:
    HarmonicEditor (BassEngineProcessor& p, int o) : proc (p), osc (o) { h = proc.getAdditiveHarmonics (osc); setSize (520, 220); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f);
        Draw::engraved (g, "ADDITIVE  /  64 PARTIALS", r.removeFromTop (18.0f), Fonts::title (13.0f), Justification::centredLeft, C::amberHot);
        const float bw = r.getWidth() / 64.0f;
        for (int i = 0; i < 64; ++i)
        {
            auto bar = Rectangle<float> (r.getX() + (float) i * bw + 1.0f, r.getY(), bw - 2.0f, r.getHeight());
            g.setColour (C::engrave); g.fillRect (bar);
            const float v = jlimit (0.0f, 1.0f, h[(size_t) i]);
            g.setColour (i % 2 == 0 ? C::amber : C::amberDim.brighter (0.4f));
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * v));
        }
    }
    void mouseDown (const MouseEvent& e) override { edit (e); }
    void mouseDrag (const MouseEvent& e) override { edit (e); }
    void mouseUp (const MouseEvent&) override { proc.setAdditiveHarmonics (osc, h); }
private:
    BassEngineProcessor& proc;
    int osc;
    std::vector<float> h;
    void edit (const MouseEvent& e)
    {
        auto r = getLocalBounds().toFloat().reduced (8.0f).withTrimmedTop (18.0f);
        const int i = jlimit (0, 63, (int) ((e.position.x - r.getX()) / (r.getWidth() / 64.0f)));
        h[(size_t) i] = jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        repaint();
    }
};
}

// ===================================================================== oscillator panel
OscPanel::OscPanel (Ctx& c, int o)
    : Panel ("OSC " + String (o + 1)), ctx (c), osc (o),
      on (c, oscId (o, "on")),
      engine (c, oscId (o, "engine")), layer (c, oscId (o, "layer")), vaWave (c, oscId (o, "wave")), table (c, oscId (o, "table")),
      sample (c, oscId (o, "sample")), spMode (c, oscId (o, "spmode")),
      display (c, o),
      oct (c, oscId (o, "oct"), "OCT"), semi (c, oscId (o, "semi"), "SEMI"), unison (c, oscId (o, "unison"), "UNISON"),
      fine (c, oscId (o, "fine"), "FINE"), level (c, oscId (o, "level"), "LEVEL"), pan (c, oscId (o, "pan"), "PAN"), pos (c, oscId (o, "pos"), "POSITION"),
      pa (c, oscId (o, "pa"), "A"), pb (c, oscId (o, "pb"), "B"), pc (c, oscId (o, "pc"), "C"),
      detune (c, oscId (o, "detune"), "DETUNE"), spread (c, oscId (o, "spread"), "SPREAD"),
      phase (c, oscId (o, "phase"), "PHASE"), randPh (c, oscId (o, "randphase"), "RAND PH"), blend (c, oscId (o, "blend"), "BLEND"),
      drift (c, oscId (o, "drift"), "DRIFT"), slop (c, oscId (o, "slop"), "SLOP"), keyTrack (c, oscId (o, "keytrack"), "KEY TRK"),
      coarse (c, oscId (o, "coarse"), "COARSE"), fold (c, oscId (o, "fold"), "FOLD"), shape (c, oscId (o, "shape"), "SHAPE"),
      sync (c, oscId (o, "sync"), "SYNC"), xmodAmt (c, oscId (o, "xmodamt"), "X-MOD"),
      syncMode (c, oscId (o, "syncmode")), xmodType (c, oscId (o, "xmodtype")), xmodSrc (c, oscId (o, "xmodsrc")),
      reset (c, oscId (o, "reset"), "RESET")
{
    for (Component* comp : std::initializer_list<Component*> { &on, &engine, &layer, &vaWave, &table, &sample, &spMode, &advBtn, &loadBtn, &editBtn, &clearBtn, &display,
                                                               &oct, &semi, &unison, &fine, &level, &pan, &pos, &pa, &pb, &pc, &detune, &spread,
                                                               &phase, &randPh, &blend, &drift, &slop, &keyTrack, &coarse, &fold, &shape, &sync, &xmodAmt,
                                                               &syncMode, &xmodType, &xmodSrc, &reset })
        addAndMakeVisible (comp);
    advBtn.setClickingTogglesState (true);
    advBtn.onClick = [this] { adv = advBtn.getToggleState(); updateFace(); };
    advBtn.setTooltip ("Advanced: phase, drift, sync, cross-modulation, fold and shaping");
    loadBtn.onClick = [this]
    {
        const int e = (int) ctx.getParam (oscId (osc, "engine"));
        const bool smp = e == (int) Engine::Sample || e == (int) Engine::Granular;
        auto fromDisk = [this, smp]
        {
            chooser = std::make_unique<FileChooser> (smp ? "Load sample" : "Load wavetable", File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, smp] (const FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                if (smp) ctx.proc.loadUserSample (osc, f); else ctx.proc.loadUserWavetable (osc, f);
                updateFace();
            });
        };
        // Muestras Racoon: miles de ondas de un ciclo (AKWF), tablas de 64 cuadros (WaveEdit) y bajos 808 de verdad
        PopupMenu m;
        m.addItem ("Archivo...", fromDisk);
        const auto& cat = racoon::biblio::Catalogo::get();
        auto useWave = [this] (const racoon::biblio::Onda& o)
        {
            ctx.proc.loadUserWavetable (osc, racoon::biblio::Catalogo::get().archivo (o.archivo), o.cuadros > 1 ? 256 : o.ciclo);
            updateFace();
        };
        if (! smp)
        {
            PopupMenu one, tabs;
            racoon::biblio::menuOndas (one, false, useWave);
            racoon::biblio::menuOndas (tabs, true, useWave);
            m.addSubMenu ("Muestras: ondas de un ciclo (AKWF)", one, cat.disponible());
            m.addSubMenu ("Muestras: tablas de 64 cuadros (WaveEdit)", tabs, cat.disponible());
        }
        else
        {
            PopupMenu lib;
            racoon::biblio::menuBiblioteca (lib, { "bombo", "bajo_808", "textura", "voz" }, [this] (const String& grupo)
            {
                const auto& c = racoon::biblio::Catalogo::get();
                if (const auto* g = c.grupo (grupo); g != nullptr && ! g->muestras.empty())
                {
                    // la capa más fuerte, primera alternancia
                    int best = g->muestras.front();
                    for (int idx : g->muestras) if (c.muestras[(size_t) idx].rr == 1) best = idx;
                    ctx.proc.loadUserSample (osc, c.archivo (c.muestras[(size_t) best].archivo));
                    updateFace();
                }
            });
            m.addSubMenu ("Muestras Racoon", lib, cat.disponible());
        }
        if (! cat.disponible()) m.addItem (racoon::biblio::avisoSinBiblioteca(), false, false, [] {});
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&loadBtn));
    };
    clearBtn.setTooltip ("Return to the factory table");
    clearBtn.onClick = [this] { ctx.proc.clearUserTable (osc); updateFace(); };
    editBtn.onClick = [this] { openHarmonicsEditor(); };
    display.onClick = [this] { if ((int) ctx.getParam (oscId (osc, "engine")) == (int) Engine::Additive) openHarmonicsEditor(); };
    // when the engine changes from the UI, load that engine's A/B/C defaults
    engine.box.onChange = [this]
    {
        const int e = engine.box.getSelectedItemIndex();
        if (e >= 0 && e != lastEngine && lastEngine >= 0 && userTouchedEngine)
        {
            const auto& info = engineParamInfo (e);
            ctx.setParam (oscId (osc, "pa"), info.da);
            ctx.setParam (oscId (osc, "pb"), info.db);
            ctx.setParam (oscId (osc, "pc"), info.dc);
        }
        userTouchedEngine = false;
    };
    engine.box.addMouseListener (this, true);
    ctx.registerAnimated (this);
    updateFace();
}

OscPanel::~OscPanel() { ctx.unregisterAnimated (this); }

void OscPanel::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent == &engine.box || engine.box.isParentOf (e.eventComponent)) userTouchedEngine = true;
}

void OscPanel::openHarmonicsEditor()
{
    auto ed = std::make_unique<HarmonicEditor> (ctx.proc, osc);
    CallOutBox::launchAsynchronously (std::move (ed), display.getScreenBounds(), nullptr);
}

void OscPanel::animate()
{
    const int e = (int) ctx.getParam (oscId (osc, "engine"));
    if (e != lastEngine) { updateFace(); lastEngine = e; }
}

void OscPanel::updateFace()
{
    const int e = (int) ctx.getParam (oscId (osc, "engine"));
    const auto& info = engineParamInfo (e);
    pa.setLabel (info.a); pb.setLabel (info.b); pc.setLabel (info.c);
    const bool isVA = e == (int) Engine::VA, isWT = e == (int) Engine::Wavetable, isSpec = e == (int) Engine::Spectral,
               isAdd = e == (int) Engine::Additive, isSmp = e == (int) Engine::Sample || e == (int) Engine::Granular;
    vaWave.setVisible (isVA);
    table.setVisible (isWT || isSpec);
    spMode.setVisible (isSpec);
    sample.setVisible (isSmp);
    loadBtn.setVisible (isWT || isSmp);
    clearBtn.setVisible (isWT && ctx.proc.hasUserTable (osc));
    editBtn.setVisible (isAdd);
    pos.setVisible (! adv);
    pos.setLabel (isSmp ? (e == (int) Engine::Sample ? "START" : "POSITION") : (e == (int) Engine::Physical ? "PICK POS" : "POSITION"));
    for (Component* m : std::initializer_list<Component*> { &oct, &semi, &unison, &fine, &level, &pan, &pos, &pa, &pb, &pc, &detune, &spread })
        m->setVisible (! adv);
    for (Component* m : std::initializer_list<Component*> { &phase, &randPh, &blend, &drift, &slop, &keyTrack, &coarse, &fold, &shape, &sync, &xmodAmt, &syncMode, &xmodType, &xmodSrc, &reset })
        m->setVisible (adv);
    resized();
    repaint();
}

void OscPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (22);
    head.removeFromLeft (64);   // title
    on.setBounds (head.removeFromLeft (24));
    advBtn.setBounds (head.removeFromRight (40).reduced (0, 1));
    head.removeFromRight (4);
    layer.setBounds (head.removeFromRight (86));
    head.removeFromRight (4);
    engine.setBounds (head.removeFromRight (128));
    r.removeFromTop (4);
    display.setBounds (r.removeFromTop (92));
    r.removeFromTop (4);
    auto sel = r.removeFromTop (22);
    auto right = sel.removeFromRight (100);
    if (clearBtn.isVisible()) { clearBtn.setBounds (right.removeFromRight (22)); right.removeFromRight (2); }
    loadBtn.setBounds (right);
    editBtn.setBounds (sel.removeFromRight (100));
    if (spMode.isVisible()) { spMode.setBounds (sel.removeFromRight (110)); sel.removeFromRight (4); }
    vaWave.setBounds (sel); table.setBounds (sel); sample.setBounds (sel);
    r.removeFromTop (4);
    const int rowH = (r.getHeight()) / 2;
    auto row1 = r.removeFromTop (rowH), row2 = r;
    if (! adv)
    {
        layoutRow (row1, { &oct, &semi, &fine, &level, &pan, &pos });
        layoutRow (row2, { &pa, &pb, &pc, &unison, &detune, &spread });
        for (Stepper* s : { &oct, &semi, &unison })
            s->setBounds (s->getBounds().withSizeKeepingCentre (s->getWidth() - 4, 38));
    }
    else
    {
        layoutRow (row1, { &phase, &randPh, &blend, &drift, &slop, &keyTrack });
        auto combos = row2.removeFromRight (row2.getWidth() / 6 + 18);
        layoutRow (row2, { &coarse, &fold, &shape, &sync, &xmodAmt });
        const int h = combos.getHeight() / 4;
        syncMode.setBounds (combos.removeFromTop (h).reduced (0, 2));
        xmodType.setBounds (combos.removeFromTop (h).reduced (0, 2));
        xmodSrc.setBounds (combos.removeFromTop (h).reduced (0, 2));
        reset.setBounds (combos.reduced (0, 2));
    }
}

// ===================================================================== sub
SubPanel::SubPanel (Ctx& c)
    : Panel ("SUB"), on (c, "sub_on"), align (c, "sub_align", "ALIGN"), mono (c, "sub_mono", "MONO"), lock (c, "sub_lock", "SUB LOCK"),
      wave (c, "sub_wave", "WAVE"), octave (c, "sub_oct", "OCTAVE"), env (c, "sub_env", "ENVELOPE"),
      level (c, "sub_level", "LEVEL"), drive (c, "sub_drive", "DRIVE"), cutoff (c, "sub_cutoff", "TONE"), ceiling (c, "sub_limit", "CEILING"),
      phase (c, "sub_phase", "PHASE")
{
    for (Component* comp : std::initializer_list<Component*> { &on, &align, &mono, &lock, &wave, &octave, &env, &level, &drive, &cutoff, &ceiling, &phase })
        addAndMakeVisible (comp);
    lock.setTooltip ("Sub Lock: the sub keeps its pitch and stays mono while detune, stereo width and upper layers move");
}

void SubPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (22);
    head.removeFromLeft (44);
    on.setBounds (head.removeFromLeft (24));
    r.removeFromTop (4);
    auto combos = r.removeFromTop (38);
    layoutRow (combos, { &wave, &octave, &env }, 6);
    r.removeFromTop (6);
    auto row1 = r.removeFromTop (88);
    layoutRow (row1, { &level, &drive, &cutoff });
    auto row2 = r.removeFromTop (88);
    layoutRow (row2, { &ceiling, &phase, nullptr });
    auto t = r.removeFromTop (22);
    layoutRow (t, { &lock, &align, &mono }, 4);
}

// ===================================================================== noise
NoisePanel::NoisePanel (Ctx& c)
    : Panel ("NOISE"), on (c, "noise_on"), type (c, "noise_type", "TYPE"), layer (c, "noise_layer", "LAYER"),
      level (c, "noise_level", "LEVEL"), color (c, "noise_color", "COLOR"), pan (c, "noise_pan", "PAN"), transient (c, "noise_attack", "TRANSIENT")
{
    for (Component* comp : std::initializer_list<Component*> { &on, &type, &layer, &level, &color, &pan, &transient }) addAndMakeVisible (comp);
}

void NoisePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (22);
    head.removeFromLeft (60);
    on.setBounds (head.removeFromLeft (24));
    r.removeFromTop (4);
    layoutRow (r.removeFromTop (38), { &type, &layer }, 6);
    r.removeFromTop (6);
    layoutRow (r.removeFromTop (88), { &level, &color });
    layoutRow (r.removeFromTop (88), { &pan, &transient });
}

// ===================================================================== filters
FilterStrip::FilterStrip (Ctx& c, int filter)
    : f (filter), on (c, filtId (filter, "on")), comp (c, filtId (filter, "comp"), "COMP"),
      type (c, filtId (filter, "type")), envSel (c, filtId (filter, "envsel")), curve (c, filter),
      cutoff (c, filtId (filter, "cutoff"), "CUTOFF"), res (c, filtId (filter, "res"), "RESO"), drive (c, filtId (filter, "drive"), "DRIVE"),
      envAmt (c, filtId (filter, "envamt"), "ENV"), keyTrack (c, filtId (filter, "keytrack"), "KEY"), vel (c, filtId (filter, "velsens"), "VEL"),
      mix (c, filtId (filter, "mix"), "MIX"), morph (c, filtId (filter, "morph"), "SLOPE/MORPH"), feedback (c, filtId (filter, "feedback"), "FEEDBK")
{
    for (Component* comp2 : std::initializer_list<Component*> { &on, &comp, &type, &envSel, &curve, &cutoff, &res, &drive, &envAmt, &keyTrack, &vel, &mix, &morph, &feedback })
        addAndMakeVisible (comp2);
    comp.setTooltip ("Resonance volume / bass loss compensation");
}

void FilterStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (S::ink.withAlpha (0.06f));
    g.fillRect (r);
    g.setColour (S::ink.withAlpha (0.35f));
    g.drawRect (r, 1.0f);
    Draw::engraved (g, "F" + String (f + 1), r.removeFromTop (22.0f).withTrimmedLeft (28.0f), Fonts::title (14.0f), Justification::centredLeft, C::bone);
}

void FilterStrip::resized()
{
    auto r = getLocalBounds().reduced (4);
    auto head = r.removeFromTop (22);
    on.setBounds (head.removeFromLeft (24));
    head.removeFromLeft (26);
    type.setBounds (head.reduced (0, 0));
    r.removeFromTop (4);
    curve.setBounds (r.removeFromTop (56));
    r.removeFromTop (2);
    const int rh = (r.getHeight() - 24) / 2;
    layoutRow (r.removeFromTop (rh), { &cutoff, &res, &drive, &envAmt });
    layoutRow (r.removeFromTop (rh), { &keyTrack, &vel, &mix, &morph, &feedback });
    auto bottom = r;
    comp.setBounds (bottom.removeFromRight (70));
    envSel.setBounds (bottom.removeFromLeft (90).reduced (0, 1));
}

FilterSection::FilterSection (Ctx& c) : Panel ("FILTERS"), routing (c, "filter_routing"), f1 (c, 0), f2 (c, 1), f3 (c, 2)
{
    for (Component* comp : std::initializer_list<Component*> { &routing, &f1, &f2, &f3 }) addAndMakeVisible (comp);
    routing.box.setTooltip ("Serial, parallel, one filter per layer, or F1+F2 into F3");
}

void FilterSection::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto head = r.removeFromTop (22);
    routing.setBounds (head.removeFromRight (130));
    r.removeFromTop (4);
    layoutRow (r, { &f1, &f2, &f3 }, 8);
}

// ===================================================================== page
SynthPage::SynthPage (Ctx& c) : o1 (c, 0), o2 (c, 1), o3 (c, 2), sub (c), noise (c), filters (c)
{
    for (Component* comp : std::initializer_list<Component*> { &o1, &o2, &o3, &sub, &noise, &filters }) addAndMakeVisible (comp);
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (r.getHeight() * 52 / 100);
    layoutRow (top, { &o1, &o2, &o3 }, 8);
    r.removeFromTop (8);
    sub.setBounds (r.removeFromLeft (260));
    r.removeFromLeft (8);
    noise.setBounds (r.removeFromLeft (190));
    r.removeFromLeft (8);
    filters.setBounds (r);
}
} // namespace be::ui

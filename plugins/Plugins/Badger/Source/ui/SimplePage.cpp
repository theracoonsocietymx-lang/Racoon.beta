#include "Pages.h"
#include "I18n.h"

namespace be::ui
{
using namespace juce;

namespace
{
// default macro names of the factory presets, said in plain Spanish
String spanishMacro (const String& name, String& english)
{
    static const std::map<String, String> m {
        { "CUTOFF", "BRILLO" }, { "RESO", "RESONANCIA" }, { "DRIVE", "SATURACIÓN"_u }, { "ACID", "ÁCIDO"_u }, { "SUB", "SUB" },
        { "SPACE", "ESPACIO" }, { "EVOLVE", "MOVIMIENTO" }, { "BODY/BITE", "CUERPO · MORDIDA"_u }, { "VOWEL", "VOCAL" },
    };
    english = name;
    if (auto it = m.find (name.toUpperCase()); it != m.end()) return it->second;
    english = {};
    return name.toUpperCase();
}

String macroHelp (const String& english)
{
    if (english == "CUTOFF") return "Abre el filtro. A la izquierda el bajo queda redondo y oscuro, a la derecha brilla y se oye en bocinas chicas.";
    if (english == "RESO") return "Resonancia. Agrega el pico chillón en el filtro. Con BRILLO y RESONANCIA arriba tienes un bajo acid."_u;
    if (english == "DRIVE") return "Saturación. Calienta y ensucia el bajo para que empuje más en la mezcla."_u;
    if (english == "ACID") return "Convierte el bajo en un 303. Combínalo con el piano roll y los acentos."_u;
    if (english == "SUB") return "Sube la nota grave pura de abajo. Es lo que se siente en el pecho en un club. Con cuidado en bocinas chicas.";
    if (english == "SPACE") return "Reverb. Pone el bajo en un cuarto. Para bajos de house casi siempre va poco.";
    if (english == "EVOLVE") return "Movimiento lento del filtro con los compases, para que el bajo no suene igual todo el track.";
    if (english == "BODY/BITE") return "Cruza entre el cuerpo redondo y la capa que muerde.";
    return "Macro del preset. Mueve varios controles a la vez.";
}
}

// ===================================================================== mini roll
MiniRoll::MiniRoll (Ctx& c) : ctx (c)
{
    setTooltip ("El patrón que va a sonar. Click para abrir el piano roll y editarlo."_u);
    setMouseCursor (MouseCursor::PointingHandCursor);
    ctx.registerAnimated (this);
}

MiniRoll::~MiniRoll() { ctx.unregisterAnimated (this); }

void MiniRoll::animate()
{
    if (! animVisible (this)) return;
    bool dirty = false;
    if (ctx.proc.seqVersion.load() != seen) { seen = ctx.proc.seqVersion.load(); data = ctx.proc.getSeqData(); dirty = true; }
    const int s = ctx.proc.engine.telemetry.seqStep.load();
    if (s != playStep) { playStep = s; dirty = true; }
    if (dirty) repaint();
}

void MiniRoll::mouseDown (const MouseEvent&) { if (ctx.showPage) ctx.showPage (4); }

void MiniRoll::paint (Graphics& g)
{
    if (seen < 0) { seen = ctx.proc.seqVersion.load(); data = ctx.proc.getSeqData(); }
    auto r = getLocalBounds().toFloat();
    Draw::box (g, r, C::void_, 2.0f, 4.0f);
    r = r.withTrimmedRight (4.0f).withTrimmedBottom (4.0f).reduced (8.0f, 8.0f);
    const int pat = jlimit (0, kSeqPatterns - 1, (int) ctx.getParam ("seq_pattern") - 1);
    const int len = jlimit (1, kSeqMaxSteps, (int) ctx.getParam ("seq_length"));
    const auto& pt = data.patterns[pat];
    // pitch range used
    int lo = 0, hi = 12;
    bool any = false;
    for (int s = 0; s < len; ++s)
    {
        const auto& st = pt.steps[s];
        if (! st.on) continue;
        auto use = [&] (int p) { if (! any) { lo = hi = p; any = true; } lo = std::min (lo, p); hi = std::max (hi, p); };
        use (st.note + 12 * st.octave);
        for (int c = 0; c < st.numChord; ++c) use (st.chord[c].pitch);
    }
    lo -= 2; hi += 2;
    const float stepW = r.getWidth() / (float) len;
    const float rowH = r.getHeight() / (float) std::max (8, hi - lo + 1);
    auto yOf = [&] (int p) { return r.getBottom() - (float) (p - lo + 1) * rowH; };
    for (int s = 0; s <= len; ++s)
    {
        g.setColour (s % 16 == 0 ? C::ridge.brighter (0.4f) : s % 4 == 0 ? C::ridge : C::stoneHi);
        g.drawVerticalLine ((int) (r.getX() + (float) s * stepW), r.getY(), r.getBottom());
    }
    if (playStep >= 0 && playStep < len)
    {
        g.setColour (C::cyan.withAlpha (0.18f));
        g.fillRect (Rectangle<float> (r.getX() + (float) playStep * stepW, r.getY(), stepW, r.getHeight()));
    }
    for (int s = 0; s < len; ++s)
    {
        const auto& st = pt.steps[s];
        if (! st.on || (st.tie && s > 0 && pt.steps[s - 1].on)) continue;
        int e = s;
        while (e + 1 < len && pt.steps[e + 1].on && pt.steps[e + 1].tie) ++e;
        const float x0 = r.getX() + ((float) s + st.micro) * stepW;
        const float x1 = r.getX() + ((float) e + pt.steps[e].gate) * stepW;
        g.setColour (st.accent ? C::amberHot : C::amber);
        g.fillRect (Rectangle<float> (x0 + 1.0f, yOf (st.note + 12 * st.octave), std::max (3.0f, x1 - x0 - 2.0f), std::max (3.0f, rowH - 1.0f)));
        for (int c = 0; c < st.numChord; ++c)
        {
            const float xc = r.getX() + ((float) s + st.micro + st.chord[c].len) * stepW;
            g.fillRect (Rectangle<float> (x0 + 1.0f, yOf (st.chord[c].pitch), std::max (3.0f, xc - x0 - 2.0f), std::max (3.0f, rowH - 1.0f)));
        }
    }
    if (! any)
    {
        g.setColour (C::bone);
        g.setFont (Fonts::body (15.0f));
        g.drawText (::racoon::i18n::tr ("Patrón vacío. Click aquí para dibujar notas."_u), r, Justification::centred);
    }
}

// ===================================================================== simple page
SimplePage::SimplePage (Ctx& c)
    : ctx (c), scope (c, false),
      glide (c, "v_glide", "DESLIZ"), tempo (c, "seq_bpm", "TEMPO"), swing (c, "seq_swing", "SWING"),
      playMode (c, "v_mode", "TOCAR"), play (c, true), mini (c)
{
    for (Component* comp : std::initializer_list<Component*> { &soundPanel, &patternPanel, &presetsPanel, &helpPanel, &scope, &glide, &tempo, &swing,
                                                               &playMode, &play, &variation, &clear, &openRoll, &mini, &category, &list, &prevBtn, &nextBtn })
        addAndMakeVisible (comp);
    for (int i = 0; i < 8; ++i)
        addAndMakeVisible (macros.add (new ParamKnob (c, be::macroId (i), "MACRO")));
    for (int i = 0; i < kSeqPatterns; ++i)
    {
        auto* b = patternBtns.add (new TextButton (String::charToString ((juce_wchar) ('A' + i))));
        b->setTooltip ("Patrón "_u + String::charToString ((juce_wchar) ('A' + i)) + ". Cada preset trae 8 patrones distintos.");
        b->onClick = [this, i] { ctx.setParam ("seq_pattern", (float) (i + 1)); };
        addAndMakeVisible (b);
    }
    playMode.box.setTooltip ("Mono toca una nota a la vez. Legato liga las notas. Poly deja tocar acordes.");
    variation.setTooltip ("Inventa una variación del patrón actual sin salirse de la escala. Si no te gusta, Cmd+Z."_u);
    variation.onClick = [this]
    {
        ctx.proc.undo.beginNewTransaction ("Variation");
        auto d = ctx.proc.getSeqData();
        const int p = (int) ctx.getParam ("seq_pattern") - 1;
        Sequencer::makeVariation (d.patterns[p], (int) ctx.getParam ("seq_length"), (int) ctx.getParam ("seq_scale"), 0.6f, (uint32) Time::getMillisecondCounter());
        ctx.proc.setSeqDataUndoable (d);
    };
    clear.setTooltip ("Borra todas las notas del patrón actual."_u);
    clear.onClick = [this]
    {
        auto d = ctx.proc.getSeqData();
        const int p = (int) ctx.getParam ("seq_pattern") - 1;
        for (auto& s : d.patterns[p].steps) { s = SeqStep(); s.on = false; }
        ctx.proc.setSeqDataUndoable (d);
    };
    openRoll.setTooltip ("Abre el piano roll para dibujar notas, acordes, acentos y slides.");
    openRoll.onClick = [this] { if (ctx.showPage) ctx.showPage (4); };
    prevBtn.setTooltip ("Sonido anterior"); nextBtn.setTooltip ("Sonido siguiente");
    prevBtn.onClick = [this] { ctx.proc.presets().step (-1); };
    nextBtn.onClick = [this] { ctx.proc.presets().step (1); };

    category.addItem ("Todos los sonidos", 1);
    for (int i = 0; i < presetCategoryNames().size(); ++i) category.addItem (presetCategoryNames()[i], i + 2);
    category.setSelectedId (1, dontSendNotification);
    category.onChange = [this] { fillList(); };
    category.setTooltip ("Filtra los sonidos por tipo de bajo.");
    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (ListBox::backgroundColourId, S::card);
    list.setOutlineThickness (0);
    ctx.registerAnimated (this);
    refresh();
}

SimplePage::~SimplePage() { ctx.unregisterAnimated (this); list.setModel (nullptr); }

void SimplePage::updateMacroLabels()
{
    String all;
    for (int i = 0; i < 8; ++i) all << ctx.proc.getMacroName (i) << "|";
    if (all == lastMacroNames) return;
    lastMacroNames = all;
    for (int i = 0; i < 8; ++i)
    {
        String english;
        const auto name = ctx.proc.getMacroName (i);
        macros[i]->setLabel (spanishMacro (name, english));
        auto* p = ctx.apvts.getParameter (be::macroId (i));
        macros[i]->knob.setTooltip ((english.isNotEmpty() ? spanishMacro (name, english) + " (" + english + ")" : name) + "\n" + macroHelp (english)
                                    + "\nDoble click regresa a cero. Este control también está abajo, en la fila de macros."_u + (p != nullptr ? String() : String()));
    }
}

void SimplePage::refresh()
{
    updateMacroLabels();
    fillList();
    const int p = (int) ctx.getParam ("seq_pattern") - 1;
    for (int i = 0; i < patternBtns.size(); ++i) patternBtns[i]->setToggleState (i == p, dontSendNotification);
    repaint();
}

void SimplePage::animate()
{
    if (! animVisible (this)) return;
    updateMacroLabels();
    const int p = (int) ctx.getParam ("seq_pattern") - 1;
    for (int i = 0; i < patternBtns.size(); ++i)
        if (patternBtns[i]->getToggleState() != (i == p)) patternBtns[i]->setToggleState (i == p, dontSendNotification);
    static String lastPreset;
    if (lastPreset != ctx.proc.currentPresetName) { lastPreset = ctx.proc.currentPresetName; list.repaint(); }
}

void SimplePage::fillList()
{
    const int cat = category.getSelectedId() - 2;
    shown.clear();
    for (auto& p : ctx.proc.presets().list())
        if (cat < 0 || p.category == presetCategoryNames()[cat]) shown.push_back (p);
    list.updateContent();
    list.repaint();
}

void SimplePage::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& p = shown[(size_t) row];
    const bool current = p.name == ctx.proc.currentPresetName;
    auto r = Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f, 1.0f);
    if (current) { g.setColour (S::hi); g.fillRect (r); }
    else if (selected) { g.setColour (S::cardDim); g.fillRect (r); }
    g.setColour (S::ink.withAlpha (0.12f));
    g.drawHorizontalLine (h - 1, 4.0f, (float) w - 4.0f);
    g.setColour (S::ink);
    g.setFont (current ? Fonts::bodyBold (15.0f) : Fonts::body (15.0f));
    g.drawText (::racoon::i18n::tr (p.name), r.withTrimmedLeft (8.0f).withTrimmedRight (120.0f), Justification::centredLeft, true);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::label (11.0f));
    g.drawText (::racoon::i18n::tr (p.category.toUpperCase()), r.withTrimmedRight (8.0f), Justification::centredRight, true);
}

void SimplePage::listBoxItemClicked (int row, const MouseEvent&)
{
    if (row < 0 || row >= (int) shown.size()) return;
    ctx.proc.presets().load (shown[(size_t) row]);
    list.repaint();
}

void SimplePage::paintOverChildren (Graphics& g)
{
    // numbered first steps in the help card
    auto r = helpPanel.getBounds().toFloat().withTrimmedRight (4.0f).withTrimmedBottom (4.0f).reduced (16.0f, 12.0f).withTrimmedTop (24.0f);
    const String steps[][2] {
        { "1", "Elige un sonido de la lista. Se escucha con el teclado de abajo o con las teclas A S D F de tu compu." },
        { "2", "Dale TOCAR PATRÓN. El bajo toca el patrón del piano roll sin que tengas que darle play en tu DAW."_u },
        { "3", "Mueve BRILLO, RESONANCIA y ÁCIDO mientras suena. Así encuentras tu bajo en un minuto."_u },
        { "4", "Arrastra DRAG MIDI de la página PATRÓN a una pista de tu DAW y ya tienes el clip."_u },
    };
    const float rowH = r.getHeight() / 4.0f;
    for (auto& st : steps)
    {
        auto row = r.removeFromTop (rowH);
        auto num = row.removeFromLeft (30.0f).withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (S::accent()); g.fillEllipse (num);
        g.setColour (S::ink); g.drawEllipse (num, 1.5f);
        g.setFont (Fonts::title (14.0f)); g.drawText (::racoon::i18n::tr (st[0]), num, Justification::centred);
        g.setFont (Fonts::body (14.0f));
        g.drawFittedText (::racoon::i18n::tr (st[1]), row.withTrimmedLeft (10.0f).toNearestInt(), Justification::centredLeft, 3, 0.9f);
    }
}

void SimplePage::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (r.getHeight() * 56 / 100);
    r.removeFromTop (12);
    auto right = top.removeFromRight (420);
    top.removeFromRight (12);

    // ---- TU SONIDO: screen + 8 big macros + glide / play mode
    soundPanel.setBounds (top);
    auto c = soundPanel.content() + top.getPosition();
    c = c.withTrimmedRight (4).withTrimmedBottom (4);
    auto scr = c.removeFromTop (112);
    scope.setBounds (scr.reduced (0, 4));
    c.removeFromTop (6);
    auto extra = c.removeFromRight (150);
    const int kw = c.getWidth() / 4, kh = c.getHeight() / 2;
    for (int i = 0; i < 8; ++i)
        macros[i]->setBounds (c.getX() + (i % 4) * kw, c.getY() + (i / 4) * kh, kw, kh);
    extra.removeFromLeft (10);
    glide.setBounds (extra.removeFromTop (extra.getHeight() / 2).reduced (8, 0));
    extra.removeFromTop (8);
    playMode.setBounds (extra.removeFromTop (40));

    // ---- SONIDOS
    presetsPanel.setBounds (right);
    auto pc = presetsPanel.content() + right.getPosition();
    pc = pc.withTrimmedRight (4).withTrimmedBottom (4);
    auto prow = pc.removeFromTop (28);
    prevBtn.setBounds (prow.removeFromLeft (30));
    nextBtn.setBounds (prow.removeFromRight (30));
    category.setBounds (prow.reduced (6, 1));
    pc.removeFromTop (6);
    list.setBounds (pc);

    // ---- PATRÓN
    auto helpArea = r.removeFromRight (420);
    r.removeFromRight (12);
    patternPanel.setBounds (r);
    auto q = patternPanel.content() + r.getPosition();
    q = q.withTrimmedRight (4).withTrimmedBottom (4);
    auto ctrl = q.removeFromLeft (300);
    play.setBounds (ctrl.removeFromTop (58).withTrimmedRight (10));
    ctrl.removeFromTop (10);
    auto pats = ctrl.removeFromTop (30).withTrimmedRight (10);
    for (int i = 0; i < patternBtns.size(); ++i)
        patternBtns[i]->setBounds (pats.getX() + i * pats.getWidth() / 8, pats.getY(), pats.getWidth() / 8 - 3, pats.getHeight());
    ctrl.removeFromTop (8);
    auto knobs = ctrl.removeFromTop (std::min (96, ctrl.getHeight() - 36)).withTrimmedRight (10);
    tempo.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    swing.setBounds (knobs);
    ctrl.removeFromTop (6);
    auto bt = ctrl.removeFromTop (28).withTrimmedRight (10);
    variation.setBounds (bt.removeFromLeft (bt.getWidth() / 2 - 3));
    bt.removeFromLeft (6);
    clear.setBounds (bt);
    auto rollArea = q;
    openRoll.setBounds (rollArea.removeFromBottom (30).removeFromRight (190));
    rollArea.removeFromBottom (8);
    mini.setBounds (rollArea);

    helpPanel.setBounds (helpArea);
}
} // namespace be::ui

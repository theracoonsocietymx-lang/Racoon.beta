#include "MoleEditor.h"
#include "I18n.h"
#include "../../Shared/Biblioteca.h"

namespace racoon::mole
{
using namespace juce;
using namespace racoon::ui;

namespace
{
const char* inspectorIds[] { "mode", "steps", "pulses", "rot", "rate", "span", "note", "vel", "gate", "prob", "accevery", "accent",
                             "rat", "ratprob", "ratcurve", "ratacc", "micro", "swing", "ref", "dens", "chan", "fill" };
const char* inspectorLabels[] { "Modo", "Pasos", "Golpes", "Rotación", "Rejilla", "Polirritmo", "Nota", "Velocidad", "Duración", "Probab.",
                                "Acento cada", "Acento", "Ratchet", "Ratchet azar", "Curva", "Aceleración", "Microtiempo", "Swing",
                                "Responde a", "Densidad", "Canal", "En fills" };
const char* inspectorHelp[] {
    "Euclidiano reparte los golpes solos; Dibujado usa los pasos que marcas; Complemento toca donde otro carril deja hueco.",
    "Cuántos pasos tiene el ciclo de este carril (hasta 128). Carriles con distinto número de pasos se desfasan y evolucionan.",
    "Cuántos golpes reparte el modo euclidiano en los pasos.", "Mueve el patrón euclidiano hacia adelante.",
    "Cuánto dura cada paso.", "Mete todos los pasos en un tiempo fijo: 3 pasos en 2 tiempos es un polirritmo 3:2.",
    "La nota MIDI que manda. Las de fábrica son las de Hamster.", "Fuerza de cada golpe.", "Qué tanto dura cada nota respecto al paso.",
    "Probabilidad de que suene cada golpe de este carril.", "Acentúa uno de cada n golpes.", "Cuánto se nota el acento.",
    "Repeticiones rápidas que se añaden al azar (con la probabilidad de al lado).", "Probabilidad de que un golpe se vuelva ratchet.",
    "Las repeticiones suben o bajan de volumen.", "Las repeticiones se aceleran o se frenan.", "Adelanta o atrasa este carril en milisegundos.",
    "Cuánto del swing general toma este carril.", "En modo Complemento, a qué carril responde.", "Qué tan seguido toca el complemento.",
    "Canal MIDI de salida.", "Si este carril participa en los fills." };
static_assert (std::size (inspectorIds) == std::size (inspectorLabels) && std::size (inspectorIds) == std::size (inspectorHelp));
}

namespace
{
// the hits of every lane before and after a change: Deshacer puts them back (the lane modes are parameters, undone
// with them in the same step)
struct StepsAction : public UndoableAction
{
    StepsAction (Processor& p, std::vector<uint64> b, std::vector<uint64> a) : proc (p), before (std::move (b)), after (std::move (a)) {}
    void apply (const std::vector<uint64>& v)
    {
        for (int l = 0; l < numLanes; ++l)
            for (int s = 0; s < maxSteps; ++s) proc.engine.setStep (l, s, Step::unpack (v[(size_t) (l * maxSteps + s)]));
    }
    bool perform() override { if (done) apply (after); done = true; return true; }
    bool undo() override { apply (before); return true; }
    int getSizeInUnits() override { return 40; }
    Processor& proc;
    std::vector<uint64> before, after;
    bool done = false;
};
}

std::vector<uint64> Display::packAll() const
{
    std::vector<uint64> v ((size_t) (numLanes * maxSteps));
    for (int l = 0; l < numLanes; ++l)
        for (int s = 0; s < maxSteps; ++s) v[(size_t) (l * maxSteps + s)] = proc.engine.getStep (l, s).pack();
    return v;
}

void Display::beginEdit()
{
    proc.undoManager.beginNewTransaction();
    undoBefore = packAll();
}

void Display::endEdit()
{
    auto after = packAll();
    if (after != undoBefore) proc.undoManager.perform (new StepsAction (proc, std::move (undoBefore), std::move (after)));
    undoBefore.clear();
}

Display::Display (Processor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    grid.hit = [this] (Point<float> pt, int& lane, int& step)
    {
        auto [l, st] = hitCell (pt.toInt());
        if (l < 0) return false;
        lane = l; step = st;
        return true;
    };
    grid.cellRect = [this] (int lane, int step) { return cellBounds (lane, step).toFloat(); };
    grid.visibleSteps = [this] { return Range<int> (page * visibleSteps(), page * visibleSteps() + visibleSteps()); };
    grid.numRows = [] { return numLanes; };
    grid.length = [this] (int lane) { return proc.engine.lanes[(size_t) lane].steps; };
    grid.get = [this] (int lane, int step) { auto st = proc.engine.getStep (lane, step); st.on = cellActive (lane, step); return st; };
    grid.set = [this] (int lane, int step, const Step& st) { ensureManual (lane); proc.engine.setStep (lane, step, st); };
    grid.isOn = [] (const Step& st) { return st.on; };
    grid.newHit = [this] (int lane, int step) { auto st = proc.engine.getStep (lane, step); st.on = true; if (st.vel <= 0) st.vel = 100; return st; };
    grid.empty = [] { return Step(); };
    grid.begin = [this] { beginEdit(); };
    grid.end = [this] { endEdit(); };
    grid.onChoose = [this] (int lane, int) { if (lane != selected) selectLane (lane); };
    grid.repaint = [this] { repaint(); };
    for (auto* b : { &complement, &mutate, &clearLane, &copyPattern, &exportMidi, &prevPage, &nextPage, &kitButton }) addAndMakeVisible (b);
    kitButton.setTooltip ("Con qué batería suena Mole cuando toca sola: la sintetizada o un kit de Muestras Racoon."_u);
    kitButton.onClick = [this]
    {
        PopupMenu m;
        m.addItem ("Sintetizada (la de siempre)", true, proc.ownKitId().isEmpty(), [this] { proc.setOwnKit ({}); refreshKitButton(); });
        const auto& cat = racoon::biblio::Catalogo::get();
        if (cat.kits.empty()) m.addItem (racoon::biblio::avisoSinBiblioteca(), false, false, [] {});
        else m.addSectionHeader ("Muestras Racoon");
        for (auto& k : cat.kits)
        {
            const auto id = k.id;
            m.addItem (k.nombre + "   " + k.generos.joinIntoString (", "), true, proc.ownKitId() == id, [this, id]
            {
                proc.setOwnKit (id);
                if (auto* p = proc.apvts.getParameter ("ownsound")) p->setValueNotifyingHost (1.0f);
                refreshKitButton();
            });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&kitButton));
    };
    refreshKitButton();
    complement.setTooltip ("Shaker, conga, bongo, cencerro y metal tocan en los huecos del bombo y la caja.");
    mutate.setTooltip ("Escribe una variación de todos los carriles (menos bombo y caja si están protegidos). Cmd+Z en el DAW la deshace."_u);
    clearLane.setTooltip ("Borra los pasos dibujados del carril elegido.");
    copyPattern.setTooltip ("Copia el patrón actual (A a H) a otro."_u);
    exportMidi.setTooltip ("Clic: guarda 8 compases en ~/Música/Racoon/Mole. Arrástralo a una pista para soltar el archivo MIDI."_u);
    complement.onClick = [this] { beginEdit(); proc.complementHamster(); endEdit(); repaint(); };
    mutate.onClick = [this] { beginEdit(); proc.bakeMutation(); endEdit(); grid.selection.clear(); repaint(); };
    clearLane.onClick = [this] { beginEdit(); proc.engine.clearLane (selected); endEdit(); grid.selection.clear(); repaint(); };
    copyPattern.onClick = [this]
    {
        PopupMenu m;
        const int cur = (int) proc.value ("slot");
        for (int i = 0; i < numSlots; ++i)
            if (i != cur) m.addItem ("Copiar " + String::charToString ((juce_wchar) ('A' + cur)) + " a " + String::charToString ((juce_wchar) ('A' + i)),
                                     [this, cur, i] { proc.copySlot (cur, i); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (copyPattern));
    };
    exportMidi.onClick = [this]
    {
        lastExport = proc.exportMidiFile (8);
        lastExport.revealToUser();
    };
    exportMidi.addMouseListener (this, false);
    prevPage.onClick = [this] { page = jmax (0, page - 1); repaint(); };
    nextPage.onClick = [this] { page = jmin (maxSteps / 32 - 1, page + 1); repaint(); };
    selectLane (0);
    startTimerHz (30);
}

Display::~Display() { exportMidi.removeMouseListener (this); }

void Display::selectLane (int lane)
{
    selected = jlimit (0, numLanes - 1, lane);
    laneControls.clear();
    for (size_t i = 0; i < std::size (inspectorIds); ++i)
    {
        Ctl c { Processor::laneId (selected, inspectorIds[i]), String::fromUTF8 (inspectorLabels[i]), String::fromUTF8 (inspectorHelp[i]) };
        addAndMakeVisible (laneControls.add (new ParamControl (proc, c)));
    }
    resized();
    repaint();
}

void Display::refreshKitButton()
{
    const auto id = proc.ownKitId();
    const auto* k = racoon::biblio::Catalogo::get().kit (id);
    kitButton.setButtonText (k != nullptr ? "BATERÍA: "_u + k->nombre.toUpperCase() : "BATERÍA: SINTETIZADA"_u);
}

void Display::resized()
{
    auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4).reduced (12, 10);
    auto tools = r.removeFromBottom (28);
    toolsArea = tools;
    const int bw = (tools.getWidth() - 8 * 6 - 80) / 6;
    for (auto* b : { &kitButton, &complement, &mutate, &clearLane, &copyPattern, &exportMidi }) { b->setBounds (tools.removeFromLeft (bw)); tools.removeFromLeft (6); }
    nextPage.setBounds (tools.removeFromRight (36));
    tools.removeFromRight (4);
    prevPage.setBounds (tools.removeFromRight (36));
    r.removeFromBottom (8);
    inspectorArea = r.removeFromBottom (186);
    r.removeFromBottom (6);
    gridArea = r;
    auto ins = inspectorArea.reduced (8, 0).withTrimmedTop (26).withTrimmedBottom (6);
    const int perRow = (laneControls.size() + 1) / 2;
    const int w = ins.getWidth() / jmax (1, perRow), h = ins.getHeight() / 2;
    for (int i = 0; i < laneControls.size(); ++i)
        laneControls[i]->setBounds (ins.getX() + (i % perRow) * w, ins.getY() + (i / perRow) * h, w, h);
}

Rectangle<int> Display::cellBounds (int lane, int step) const
{
    const int col = step - page * visibleSteps();
    const int cellsW = gridArea.getWidth() - nameW;
    const int cw = cellsW / visibleSteps();
    return { gridArea.getX() + nameW + col * cw, gridArea.getY() + headH + lane * rowH(), cw - 2, rowH() - 3 };
}

std::pair<int, int> Display::hitCell (Point<int> p) const
{
    if (! gridArea.contains (p)) return { -1, -1 };
    const int lane = (p.y - gridArea.getY() - headH) / rowH();
    if (lane < 0 || lane >= numLanes) return { -1, -1 };
    if (p.x < gridArea.getX() + nameW) return { lane, -1 };
    const int cw = (gridArea.getWidth() - nameW) / visibleSteps();
    const int step = page * visibleSteps() + (p.x - gridArea.getX() - nameW) / jmax (1, cw);
    return { lane, step };
}

bool Display::cellActive (int lane, int step) const
{
    const auto& L = proc.engine.lanes[(size_t) lane];
    if (step >= L.steps) return false;
    if (L.mode == LaneMode::manual) return proc.engine.getStep (lane, step).on;
    return proc.engine.baseActive (lane, step);
}

void Display::ensureManual (int lane)
{
    const auto& L = proc.engine.lanes[(size_t) lane];
    if (L.mode == LaneMode::manual) return;
    // drawing on a generated lane keeps what it was playing and lets you edit it
    for (int s = 0; s < L.steps; ++s)
    {
        auto st = proc.engine.getStep (lane, s);
        st.on = proc.engine.baseActive (lane, s);
        proc.engine.setStep (lane, s, st);
    }
    proc.setRealValue (Processor::laneId (lane, "mode"), 1.0f);
    proc.engine.lanes[(size_t) lane].mode = LaneMode::manual;
}

void Display::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent == &exportMidi) return;
    auto [lane, step] = hitCell (e.getPosition());
    if (lane < 0) return;
    grabKeyboardFocus();
    if (step < 0)
    {
        if (e.mods.isPopupMenu()) laneMenu (lane);
        else
        {
            // click on the name: choose; the small M and S boxes mute and solo
            const auto row = Rectangle<int> (gridArea.getX(), gridArea.getY() + headH + lane * rowH(), nameW, rowH());
            if (e.x > row.getRight() - 22) proc.setRealValue (Processor::laneId (lane, "solo"), proc.value (Processor::laneId (lane, "solo")) > 0.5f ? 0.0f : 1.0f);
            else if (e.x > row.getRight() - 44) proc.setRealValue (Processor::laneId (lane, "mute"), proc.value (Processor::laneId (lane, "mute")) > 0.5f ? 0.0f : 1.0f);
            else if (e.x < row.getX() + 18) proc.setRealValue (Processor::laneId (lane, "on"), proc.value (Processor::laneId (lane, "on")) > 0.5f ? 0.0f : 1.0f);
            selectLane (lane);
        }
        return;
    }
    if (lane != selected) selectLane (lane);
    if (step >= proc.engine.lanes[(size_t) lane].steps) return;
    if (e.mods.isPopupMenu()) { stepMenu (lane, step); return; }
    if (e.mods.isCommandDown() && ! e.mods.isShiftDown() && cellActive (lane, step))
    {
        // Cmd + drag up or down on a hit: its velocity
        beginEdit();
        ensureManual (lane);
        dragLane = lane; dragStep = step; draggingVel = true;
        dragStartVel = proc.engine.getStep (lane, step).vel;
        return;
    }
    draggingVel = false;
    dragLane = -1;
    grid.mouseDown (e);
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (e.eventComponent == &exportMidi)
    {
        if (e.getDistanceFromDragStart() > 8)
        {
            lastExport = proc.exportMidiFile (8);
            DragAndDropContainer::performExternalDragDropOfFiles ({ lastExport.getFullPathName() }, false, this);
        }
        return;
    }
    if (draggingVel && dragLane >= 0)
    {
        auto st = proc.engine.getStep (dragLane, dragStep);
        st.on = true;
        st.vel = jlimit (1, 127, dragStartVel - e.getDistanceFromDragStartY());
        proc.engine.setStep (dragLane, dragStep, st);
        repaint();
        return;
    }
    grid.mouseDrag (e);
}

void Display::mouseUp (const MouseEvent& e)
{
    if (draggingVel) { draggingVel = false; dragLane = -1; endEdit(); return; }
    grid.mouseUp (e);
}

void Display::mouseDoubleClick (const MouseEvent& e) { if (e.eventComponent != &exportMidi) grid.mouseDoubleClick (e); }
bool Display::keyPressed (const KeyPress& k) { return grid.keyPressed (k); }

void Display::stepMenu (int lane, int step)
{
    auto st = proc.engine.getStep (lane, step);
    PopupMenu m, prob, rat, cond, micro, len;
    m.addSectionHeader (laneNames()[lane] + ", paso " + String (step + 1));
    auto set = [this, lane, step] (std::function<void (Step&)> f)
    {
        return [this, lane, step, f]
        {
            beginEdit();
            ensureManual (lane);
            auto s = proc.engine.getStep (lane, step);
            s.on = true;
            f (s);
            proc.engine.setStep (lane, step, s);
            endEdit();
            repaint();
        };
    };
    for (int v : { 100, 90, 75, 50, 33, 25, 10 }) prob.addItem (String (v) + " %", true, st.prob == v, set ([v] (Step& s) { s.prob = v; }));
    for (int v : { 1, 2, 3, 4, 6, 8, 12, 16 }) rat.addItem (v == 1 ? String ("Sin ratchet") : String (v) + " golpes", true, st.ratchet == v, set ([v] (Step& s) { s.ratchet = v; }));
    const auto names = conditionNames();
    for (int c = 0; c < names.size(); ++c) cond.addItem (names[c], true, st.cond == c, set ([c] (Step& s) { s.cond = c; }));
    for (int v : { -50, -25, -10, 0, 10, 25, 50 }) micro.addItem ((v > 0 ? "+" : "") + String (v) + " % del paso", true, st.micro == v, set ([v] (Step& s) { s.micro = v; }));
    for (int v : { 25, 50, 100, 200, 400 }) len.addItem (String (v) + " %", true, st.len == v, set ([v] (Step& s) { s.len = v; }));
    m.addSubMenu ("Probabilidad", prob);
    m.addSubMenu ("Ratchet", rat);
    m.addSubMenu ("Condición"_u, cond);
    m.addSubMenu ("Microtiempo", micro);
    m.addSubMenu ("Largo de la nota", len);
    m.addSeparator();
    m.addItem (st.vel >= 120 ? "Quitar acento" : "Acentuar", set ([] (Step& s) { s.vel = s.vel >= 120 ? 90 : 127; }));
    m.addItem ("Apagar este paso", [this, lane, step] { beginEdit(); ensureManual (lane); auto s = proc.engine.getStep (lane, step); s.on = false; proc.engine.setStep (lane, step, s); endEdit(); grid.selection.erase ({ lane, step }); repaint(); });
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void Display::laneMenu (int lane)
{
    PopupMenu m;
    m.addSectionHeader (laneNames()[lane]);
    m.addItem ("Pasar a pasos dibujados", [this, lane] { ensureManual (lane); });
    m.addItem ("Borrar pasos dibujados", [this, lane] { proc.engine.clearLane (lane); });
    PopupMenu notes;
    for (int i = 0; i < numLanes; ++i)
    {
        static const int hamster[] { 36, 38, 39, 42, 46, 51, 49, 37, 41, 45, 48, 70, 63, 60, 56, 75 };
        notes.addItem (laneNames()[i] + " de Hamster (" + String (hamster[i]) + ")", [this, lane, i] { proc.setRealValue (Processor::laneId (lane, "note"), (float) hamster[i]); });
    }
    m.addSubMenu ("Mandar la nota de", notes);
    m.showMenuAsync (PopupMenu::Options().withMousePosition());
}

void Display::timerCallback()
{
    for (int i = 0; i < numLanes; ++i)
    {
        const auto f = proc.laneFlash[i].load();
        if (f != lastFlash[i]) { lastFlash[i] = f; flash[i] = 1.0f; }
        else flash[i] *= 0.82f;
    }
    repaint();
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    const auto& eng = proc.engine;
    const double ppq = proc.uiPpq.load();
    const bool running = proc.uiRunning.load();

    // header: step numbers and status
    g.setFont (Fonts::label (10.0f));
    g.setColour (C::boneDim);
    for (int s = 0; s < visibleSteps(); s += 4)
    {
        auto cb = cellBounds (0, page * visibleSteps() + s);
        g.drawText (::racoon::i18n::tr (String (page * visibleSteps() + s + 1)), cb.getX(), gridArea.getY(), 30, headH - 2, Justification::left);
    }
    String status = "PATRÓN "_u + String::charToString ((juce_wchar) ('A' + (int) proc.value ("slot")));
    if (eng.inFill (ppq) && running) status << "  ·  FILL"_u;
    if (eng.inBreak (ppq) && running) status << "  ·  CORTE"_u;
    if (proc.value ("learn") > 0.5f) status << "  ·  APRENDIENDO GROOVE ("_u << proc.learnedCount.load() << " golpes)";
    static const char* sliceText[] { "", "  ·  REBANADOR: ESPERANDO EL COMPÁS", "  ·  REBANADOR: GRABANDO", "  ·  REBANADOR: " };
    const int ss = proc.sliceState.load();
    if ((int) proc.value ("mode") != 0 && ss > 0)
    {
        status << String::fromUTF8 (sliceText[ss]);
        if (ss == 3) status << proc.numSlices.load() << " PEDAZOS";
    }
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (status), gridArea.getX(), gridArea.getY(), nameW + 400, headH - 2, Justification::left);

    const bool solo = eng.anySolo();
    for (int lane = 0; lane < numLanes; ++lane)
    {
        const auto& L = eng.lanes[(size_t) lane];
        auto row = Rectangle<int> (gridArea.getX(), gridArea.getY() + headH + lane * rowH(), nameW, rowH() - 3);
        const bool audible = L.on && ! L.mute && (! solo || L.solo);
        if (lane == selected) { g.setColour (C::amber.withAlpha (0.18f)); g.fillRect (row.withWidth (gridArea.getWidth())); }
        // on light, name, M and S
        g.setColour (L.on ? C::amber.interpolatedWith (C::bone, flash[lane]) : C::stone);
        g.fillEllipse (row.getX() + 3.0f, row.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (audible ? C::bone : C::boneDim);
        g.setFont (Fonts::label (12.0f));
        static const char* modeMark[] { "E", "D", "C" };
        g.drawFittedText (::racoon::i18n::tr (laneNames()[lane] + " " + modeMark[(int) L.mode]), row.withTrimmedLeft (18).withTrimmedRight (46), Justification::centredLeft, 1, 0.7f);
        auto ms = row.removeFromRight (44);
        const int msX = ms.getX();
        g.setColour (L.mute ? C::ochre : C::stoneHi.brighter (0.4f)); g.fillRect (ms.removeFromLeft (20).reduced (1));
        g.setColour (L.solo ? C::cyan : C::stoneHi.brighter (0.4f)); g.fillRect (ms.removeFromLeft (20).reduced (1));
        g.setColour (L.mute || L.solo ? C::basalt : C::bone);
        g.setFont (Fonts::label (10.0f));
        g.drawText (::racoon::i18n::tr ("M"), msX, row.getY(), 20, row.getHeight(), Justification::centred);
        g.drawText (::racoon::i18n::tr ("S"), msX + 20, row.getY(), 20, row.getHeight(), Justification::centred);

        // playhead of this lane
        int playPos = -1;
        if (running && L.stepBeats > 0.0) playPos = eng.posOf (lane, (int64) std::floor (ppq / L.stepBeats + 1.0e-9));
        for (int c = 0; c < visibleSteps(); ++c)
        {
            const int s = page * visibleSteps() + c;
            auto cb = cellBounds (lane, s).toFloat();
            if (s >= L.steps) { g.setColour (C::basalt); g.fillRect (cb); continue; }
            const bool beat = (s % 4) == 0;
            g.setColour (beat ? C::stoneHi : C::stone);
            g.fillRect (cb);
            if (cellActive (lane, s))
            {
                const auto st = eng.getStep (lane, s);
                const float velH = L.mode == LaneMode::manual ? st.vel / 127.0f : 1.0f;
                auto fillR = cb.withTrimmedTop (cb.getHeight() * (1.0f - jmax (0.25f, velH)));
                g.setColour ((L.mode == LaneMode::complement ? C::cyan : C::amber).withAlpha (audible ? (0.35f + 0.65f * st.prob / 100.0f) : 0.3f));
                g.fillRect (fillR);
                if (st.ratchet > 1)
                {
                    g.setColour (C::basalt);
                    for (int r = 1; r < st.ratchet; ++r) g.drawVerticalLine ((int) (cb.getX() + cb.getWidth() * r / st.ratchet), cb.getY() + 2, cb.getBottom() - 2);
                }
                if (st.cond != always) { g.setColour (C::bone); g.fillEllipse (cb.getRight() - 5.0f, cb.getY() + 1.0f, 4.0f, 4.0f); }
                if (st.micro != 0) { g.setColour (C::ochre); g.fillRect (cb.getCentreX() + st.micro / 100.0f * cb.getWidth(), cb.getBottom() - 3.0f, 2.0f, 3.0f); }
            }
            if (s == playPos) { g.setColour (C::bone.withAlpha (0.55f)); g.drawRect (cb, 1.5f); }
        }
    }
    grid.paintOverlay (g, Brand::limon);
    if (grid.drawMode)
    {
        g.setColour (Brand::limon);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("LÁPIZ (B)")), gridArea.withHeight (headH).reduced (4, 0), Justification::centredRight);
    }

    // the controls of the chosen lane, on a card like the ones below
    Draw::panel (g, inspectorArea.toFloat(), "Carril: "_u + laneNames()[selected]);
    g.setColour (C::ash);
    g.setFont (Fonts::label (11.0f));
    g.drawText (::racoon::i18n::tr ("Doble clic: poner o quitar un golpe. Clic: seleccionar (Mayús suma); arrastrar: mover; en un hueco, recuadro. Cmd+arrastrar: fuerza. Clic derecho: probabilidad, ratchet, condición. B: lápiz."_u),
                inspectorArea.reduced (12, 6).removeFromTop (14), Justification::right);
    g.setColour (C::boneDim);
    g.setFont (Fonts::label (10.0f));
    g.drawText (::racoon::i18n::tr ("PÁGINA "_u + String (page + 1) + " / 4"), prevPage.getX() - 90, toolsArea.getY(), 84, toolsArea.getHeight(), Justification::centredRight);
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y suena la batería; cada fila de la pantalla es un sonido."_u, "Prende filas y cambia sus pulsos para armar tu ritmo."_u, "Swing y Humanizar le dan groove; Fills mete redobles cada tantos compases."_u, "Para que lo toque Hamster en tu DAW, apaga Sonido propio y pon Hamster después."_u };
    s.sections = {
        { "Modo", { { "ownsound", "Sonido propio", "Mole toca sus ritmos con su propia batería. Apágalo para mandarlos a Hamster u otro instrumento."_u },
                    { "mode", "Modo", "Generar MIDI, rebanar audio o las dos cosas." },
                    { "play", "Reproducir", "Con la canción, siempre, mientras tocas una nota o con la nota transponiendo."_u },
                    { "slot", "Patrón"_u, "Ocho patrones de pasos dibujados; cámbialo para la siguiente parte de la canción."_u },
                    { "thru", "Pasar notas", "Deja pasar al instrumento las notas que tocas." } } },
        { "Groove", { { "swing", "Swing", "50 es recto; 66 es tresillo." }, { "swinggrid", "Rejilla", "Swing a 1/16 o a 1/8." },
                      { "groove", "Plantilla", "Un groove guardado o el que aprendió Mole."_u }, { "grooveamt", "Cantidad", "Qué tanto se aplica la plantilla."_u },
                      { "learn", "Aprender", "Préndelo, toca o reproduce un loop dos compases y apágalo: queda en la plantilla Aprendido."_u } } },
        { "Humanizar", { { "human", "Perfil", "Cómo se equivoca: preciso, funk, percusión humana o nada."_u },
                         { "humamt", "Cantidad", "Cuánta imperfección."_u }, { "seed", "Semilla", "Otro número, otras variaciones (siempre las mismas para ese número)."_u } } },
        { "Fills y cortes", { { "fillevery", "Fill cada", "Cada cuántos compases hay un fill."_u }, { "filldens", "Densidad", "Cuántos golpes extra."_u },
                              { "breakevery", "Corte cada", "Cada cuántos compases hay un corte."_u }, { "breaktype", "Tipo", "Silencio, menos golpes, redoble o acentos cambiados." } } },
        { "Mutación"_u, { { "mutation", "Mutación"_u, "Cuánto cambia el patrón con el tiempo."_u }, { "mutevery", "Cada", "Cada cuántas vueltas muta."_u },
                         { "keepkick", "Bombo", "El bombo no muta." }, { "keepsnare", "Caja", "La caja no muta." } }, true },
        { "Tiempo", { { "humtime", "Humanizar tiempo", "" }, { "humvel", "Humanizar fuerza", "" }, { "pushpull", "Empujar", "Todo antes o después, en ms."_u },
                      { "trange", "Máximo"_u, "Límite de cuánto se puede mover un golpe."_u }, { "quant", "Cuantizar", "Regresa los golpes a la rejilla." },
                      { "groovemode", "Del groove", "Tiempo, acentos o los dos." }, { "transpose", "Transponer", "Sube o baja todas las notas." } }, true },
        { "Fills (detalle)", { { "filllen", "Largo", "" }, { "fillcomp", "Complejidad", "Más ratchets y síncopas al final."_u }, { "breaklen", "Largo del corte", "" },
                               { "prob", "Probabilidad", "Probabilidad general de todos los carriles." }, { "velscale", "Velocidad", "Fuerza general."_u } }, true },
        { "Rebanador", { { "slcapture", "Capturar", "Préndelo con la canción sonando: graba desde el siguiente compás y corta en pedazos."_u },
                         { "slbars", "Largo", "" }, { "slcut", "Cortes", "Por transitorios o en partes iguales." }, { "slsens", "Sensibilidad", "" },
                         { "slrev", "Al revés"_u, "" }, { "slskip", "Saltar", "" }, { "slrep", "Repetir", "" }, { "slshuf", "Desordenar", "" },
                         { "slfade", "Fundido", "Evita clics entre pedazos."_u }, { "slmix", "Mezcla", "" } }, true },
    };
    s.width = 1260;
    s.displayMin = 600;   // the 16 lanes and the inspector need it
    s.displayEasy = 640;
    s.displayFull = 640;
    s.easyHeight = 940;
    s.advancedHeight = 190;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

#include "ChipmunkEditor.h"
#include "I18n.h"

namespace racoon::chipmunk
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p)
{
    for (auto* b : { &learnChordButton, &midiLearnButton, &clearLearnButton, &panicButton }) addAndMakeVisible (b);
    learnChordButton.setTooltip ("Toca un acorde y suéltalo: queda guardado en la memoria (modo de acordes Memoria)."_u);
    midiLearnButton.setTooltip ("Mueve un control de esta ventana y luego una perilla de tu teclado MIDI: quedan enlazados."_u);
    clearLearnButton.setTooltip ("Quita todos los enlaces de MIDI learn.");
    panicButton.setTooltip ("Apaga todas las notas que estén sonando."_u);
    learnChordButton.onClick = [this] { proc.learnChord = ! proc.learnChord.load(); };
    midiLearnButton.onClick = [this]
    {
        if (armed || proc.learnParam.load() >= 0) { armed = false; proc.learnParam = -1; }
        else armed = true;
    };
    clearLearnButton.onClick = [this] { for (auto& c : proc.ccMap) c.store (-1); };
    panicButton.onClick = [this] { proc.panicRequest = true; };
    for (auto* prm : proc.getParameters()) prm->addListener (this);
    startTimerHz (25);
}

Display::~Display()
{
    for (auto* prm : proc.getParameters()) prm->removeListener (this);
}

void Display::parameterGestureChanged (int index, bool starting)
{
    // MIDI learn: the next control you touch waits for a CC
    if (starting && armed.exchange (false)) proc.learnParam = index;
}

void Display::resized()
{
    auto r = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    auto buttons = r.removeFromBottom (28.0f).toNearestInt();
    const int bw = (buttons.getWidth() - 18) / 4;
    for (auto* b : { &learnChordButton, &midiLearnButton, &clearLearnButton, &panicButton })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (6);
    }
    r.removeFromBottom (10.0f);
    auto left = r.removeFromLeft (r.getWidth() * 0.42f);
    infoArea = left.removeFromTop (left.getHeight() * 0.42f);
    keysArea = left.reduced (0, 6);
    gridArea = r.withTrimmedLeft (16.0f);
}

static bool bit (const std::atomic<uint64>* m, int p) { return (m[p >> 6].load() >> (p & 63)) & 1; }

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());

    // chord name and what it plays
    std::vector<int> playing;
    for (int p = 0; p < 128; ++p) if (bit (proc.soundingMask, p)) playing.push_back (p);
    const bool arp = proc.value ("arp") > 0.5f;
    String name = harmony::nameOf (playing);
    if (arp && proc.heldMask[0].load() + proc.heldMask[1].load() != 0)
    {
        std::vector<int> c;
        for (int p = 0; p < 128; ++p) if (bit (proc.heldMask, p)) for (int q : proc.chordFor (p)) c.push_back (q);
        name = harmony::nameOf (c);
    }
    g.setColour (C::amber);
    g.setFont (Fonts::title (40.0f));
    g.drawText (::racoon::i18n::tr (name.isNotEmpty() ? name : String ("-")), infoArea.withTrimmedBottom (22), Justification::centredLeft);
    g.setFont (Fonts::label (12.0f));
    g.setColour (C::boneDim);
    static const char* modes[] { "PASA LAS NOTAS", "ACORDE FIJO", "DIATÓNICO", "MEMORIA", "PROGRESIÓN" };
    const int mode = (int) proc.value ("chordmode");
    String sub = String::fromUTF8 (modes[mode]);
    if (mode == 2 || mode == 4) sub << "  ·  "_u << harmony::noteName ((int) proc.value ("key")) << " " << upper (harmony::scaleNames()[(int) proc.value ("scale")]);
    if (mode == 3)
    {
        std::vector<int> mem;
        for (auto& m : proc.memory) if (m.load() >= 0) mem.push_back (60 + m.load());
        sub << "  ·  "_u << (harmony::nameOf (mem).isNotEmpty() ? harmony::nameOf (mem) : String ((int) mem.size()) + " NOTAS");
    }
    if (proc.learnChord.load()) sub = "TOCA EL ACORDE Y SUÉLTALO"_u;
    if (proc.learnParam.load() >= 0) sub = "AHORA MUEVE UNA PERILLA DE TU TECLADO MIDI";
    else if (armed) sub = "TOCA UN CONTROL DE ESTA VENTANA";
    g.drawText (::racoon::i18n::tr (sub), infoArea.removeFromBottom (18), Justification::centredLeft);

    // keyboard, C2..C7: held keys in cyan, notes going out in amber
    {
        const int lo = 36, hi = 96;
        int whites = 0;
        for (int p = lo; p <= hi; ++p) if (! MidiMessage::isMidiNoteBlack (p)) ++whites;
        const float w = keysArea.getWidth() / (float) whites;
        int wi = 0;
        for (int p = lo; p <= hi; ++p)
        {
            if (MidiMessage::isMidiNoteBlack (p)) continue;
            auto k = Rectangle<float> (keysArea.getX() + w * (float) wi++, keysArea.getY(), w - 1.0f, keysArea.getHeight());
            g.setColour (bit (proc.soundingMask, p) ? C::amber : bit (proc.heldMask, p) ? C::cyan : C::bone);
            g.fillRect (k);
        }
        wi = 0;
        for (int p = lo; p <= hi; ++p)
        {
            if (! MidiMessage::isMidiNoteBlack (p)) { ++wi; continue; }
            auto k = Rectangle<float> (keysArea.getX() + w * (float) wi - w * 0.32f, keysArea.getY(), w * 0.64f, keysArea.getHeight() * 0.6f);
            g.setColour (bit (proc.soundingMask, p) ? C::amberHot : bit (proc.heldMask, p) ? C::cyan : C::void_);
            g.fillRect (k);
        }
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (10.0f));
        g.drawText (::racoon::i18n::tr ("C2"), keysArea.withHeight (12).translated (2, keysArea.getHeight() + 1), Justification::centredLeft);
        g.drawText (::racoon::i18n::tr ("C7"), keysArea.withHeight (12).translated (-2, keysArea.getHeight() + 1), Justification::centredRight);
    }

    // step table
    {
        g.setFont (Fonts::label (11.0f));
        g.setColour (arp ? C::boneDim : C::ash);
        g.drawText (::racoon::i18n::tr (arp ? "ARPEGIADOR  ·  "_u + RacoonProcessor::divisionNames()[(int) proc.value ("arprate")] + "  ·  "_u + String ((int) proc.value ("steps")) + " PASOS"
                        : String ("ARPEGIADOR APAGADO  ·  LA TABLA SE USA AL ENCENDERLO"_u)),
                    gridArea.withHeight (14), Justification::centredLeft);
        auto grid = gridArea.withTrimmedTop (20).withTrimmedBottom (16);
        const float cw = grid.getWidth() / (float) tableSteps;
        const int cur = proc.currentStep.load();
        for (int k = 0; k < tableSteps; ++k)
        {
            auto cell = Rectangle<float> (grid.getX() + cw * (float) k + 2.0f, grid.getY(), cw - 4.0f, grid.getHeight() - 16.0f);
            const bool on = proc.value (Processor::stepId (k, "on")) > 0.5f;
            const bool acc = proc.value (Processor::stepId (k, "acc")) > 0.5f;
            const float prob = proc.value (Processor::stepId (k, "prob")) * 0.01f;
            const int rat = (int) proc.value (Processor::stepId (k, "rat"));
            g.setColour (C::stone);
            g.fillRect (cell);
            if (on)
            {
                const float h = cell.getHeight() * prob;
                auto fill = cell.withTop (cell.getBottom() - h);
                for (int r = 0; r < rat; ++r)
                {
                    const float rh = fill.getHeight() / (float) rat;
                    g.setColour ((arp ? C::amber : C::amberDim).withAlpha (acc ? 1.0f : 0.7f));
                    g.fillRect (fill.getX(), fill.getY() + rh * (float) r + 1.0f, fill.getWidth(), rh - 2.0f);
                }
            }
            if (acc) { g.setColour (C::ochre); g.fillRect (cell.withHeight (4.0f)); }
            if (arp && cur >= 0 && cur % tableSteps == k) { g.setColour (C::cyan); g.drawRect (cell.expanded (1.5f), 2.0f); }
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (String (k + 1)), cell.withY (cell.getBottom() + 2).withHeight (12), Justification::centred);
        }
        g.setColour (C::ash);
        g.setFont (Fonts::label (10.0f));
        g.drawText (::racoon::i18n::tr ("CLIC: PASO  ·  CLIC DERECHO: ACENTO  ·  ARRASTRAR: PROBABILIDAD  ·  RUEDA: RATCHET"_u),
                    gridArea.withTop (gridArea.getBottom() - 12), Justification::centredLeft);
    }
}

int Display::stepAt (Point<float> p) const
{
    auto grid = gridArea.withTrimmedTop (20).withTrimmedBottom (16);
    if (! grid.contains (p)) return -1;
    return jlimit (0, tableSteps - 1, (int) ((p.x - grid.getX()) / (grid.getWidth() / (float) tableSteps)));
}

void Display::setStep (int k, const char* what, float v)
{
    if (auto* prm = proc.apvts.getParameter (Processor::stepId (k, what)))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    }
}

void Display::mouseDown (const MouseEvent& e)
{
    const int k = stepAt (e.position);
    dragStep = -1;
    if (k < 0) return;
    if (e.mods.isPopupMenu()) { setStep (k, "acc", proc.value (Processor::stepId (k, "acc")) > 0.5f ? 0.0f : 1.0f); return; }
    setStep (k, "on", proc.value (Processor::stepId (k, "on")) > 0.5f ? 0.0f : 1.0f);
    dragStep = k;
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (dragStep < 0 || e.getDistanceFromDragStartY() == 0) return;
    auto grid = gridArea.withTrimmedTop (20).withTrimmedBottom (16);
    const float prob = jlimit (0.0f, 100.0f, (grid.getBottom() - 16.0f - e.position.y) / (grid.getHeight() - 16.0f) * 100.0f);
    if (proc.value (Processor::stepId (dragStep, "on")) < 0.5f) setStep (dragStep, "on", 1.0f);
    setStep (dragStep, "prob", std::round (prob));
}

void Display::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int k = stepAt (e.position);
    if (k < 0) return;
    const int rat = jlimit (1, 4, (int) proc.value (Processor::stepId (k, "rat")) + (w.deltaY > 0 ? 1 : -1));
    setStep (k, "rat", (float) rat);
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1000;
    s.easyHeight = 640;
    s.advancedHeight = 220;
    s.displayEasy = 290;
    s.displayFull = 250;
    auto stepCards = [] (int from)
    {
        std::vector<Ctl> c;
        for (int k = from; k < from + 4; ++k)
        {
            c.push_back ({ Processor::stepId (k, "on"), "Paso " + String (k + 1), "Enciende o apaga el paso."_u });
            c.push_back ({ Processor::stepId (k, "acc"), "Acento", "Este paso suena más fuerte."_u });
            c.push_back ({ Processor::stepId (k, "prob"), "Prob", "Probabilidad de que suene. Con la misma semilla, sale igual cada vez."_u });
            c.push_back ({ Processor::stepId (k, "rat"), "Ratchet", "Repite la nota 2, 3 o 4 veces dentro del paso."_u });
        }
        return c;
    };
    s.steps = { "Dale Play y cada nota de la secuencia se vuelve un acorde."_u, "En Acordes eliges tonalidad y escala; en Arpegio prendes el arpegiador."_u, "Escribe una nota por compás en la secuencia y Chipmunk arma la progresión."_u, "Para que toque otro instrumento en tu DAW, apaga Sonido propio y ponlo antes de ese instrumento."_u };
    s.sections = {
        { "Acordes", {
            { "chordmode", "Modo", "Apagado deja pasar las notas. Fijo pone el mismo tipo de acorde en cada nota. Diatónico arma el acorde con la escala. Memoria usa el acorde que grabaste. Progresión: las teclas blancas desde la tecla base tocan los 8 acordes de la progresión."_u },
            { "chordtype", "Tipo", "En modo Fijo: mayor, menor, séptimas, novenas, sus, add9, disminuidos..."_u },
            { "key", "Tono", "Tónica de la escala."_u },
            { "scale", "Escala", "Escala o modo para los acordes diatónicos y el ajuste de notas."_u },
            { "chordsize", "Notas", "Tríada, séptima o novena."_u } } },
        { "Voicing", {
            { "inversion", "Inversión"_u, "Sube las notas más graves una octava."_u },
            { "voicing", "Voicing", "Cerrado, abierto (drop 2) o amplio."_u },
            { "voicelead", "Conducción"_u, "Cada acorde nuevo se acomoda para moverse lo menos posible desde el anterior."_u } } },
        { "Arpegio", {
            { "arp", "Arpegiador", "Toca las notas del acorde una por una."_u },
            { "arpmode", "Modo", "Arriba, abajo, péndulo, al azar, como se tocó o el acorde entero en cada paso."_u },
            { "arprate", "Velocidad", "Duración de cada paso, de 1/1 a 1/128."_u },
            { "gate", "Gate", "Duración de cada nota respecto al paso, de 1 % a 200 %."_u } } },
        { "Tocar", {
            { "ownsound", "Sonido propio", "Chipmunk toca sus acordes con su propio sonido. Apágalo para mandarlos a otro instrumento."_u },
            { "ownpatch", "Sonido", "Teclas, pad, pluck o bajo." },
            { "latch", "Latch", "Las notas siguen después de soltar, hasta que tocas otro acorde."_u },
            { "strum", "Strum", "Separa las notas del acorde como una guitarra, en milisegundos."_u } } },
        { "Escala +", {
            { "quantize", "Ajustar", "Lleva las notas que tocas a la escala más cercana."_u },
            { "transpose", "Transponer", "Sube o baja todo en semitonos."_u },
            { "rangelo", "Grave", "Nota más grave del registro: lo que quede abajo sube de octava."_u },
            { "rangehi", "Agudo", "Nota más aguda del registro: lo que quede arriba baja de octava."_u },
            { "bass", "Bajo", "Agrega la fundamental una octava abajo."_u },
            { "hold", "Hold", "Junta todas las notas que toques mientras esté encendido."_u },
            { "strumdir", "Dirección"_u, "El strum va hacia arriba, hacia abajo o alterna."_u } }, true },
        { "Progresión"_u, {
            { "progbase", "Tecla base", "Desde esta tecla, las blancas tocan los acordes 1 a 8."_u },
            { "prog1", "1", "Grado del acorde 1."_u }, { "prog2", "2", "Grado del acorde 2."_u }, { "prog3", "3", "Grado del acorde 3."_u },
            { "prog4", "4", "Grado del acorde 4."_u }, { "prog5", "5", "Grado del acorde 5."_u }, { "prog6", "6", "Grado del acorde 6."_u },
            { "prog7", "7", "Grado del acorde 7."_u }, { "prog8", "8", "Grado del acorde 8."_u } }, true },
        { "Arpegio +", {
            { "octaves", "Octavas", "Cuántas octavas recorre, de 1 a 4."_u },
            { "steps", "Pasos", "Largo del patrón, de 1 a 64 pasos (la tabla de 16 se repite)."_u },
            { "swing", "Swing", "Retrasa los pasos pares."_u },
            { "accent", "Acento", "Cuánto más fuerte suenan los pasos con acento."_u },
            { "arpvel", "Velocidad", "Velocidad fija de las notas. En 0 usa la que tocaste."_u },
            { "variation", "Variación"_u, "Cambia algunas notas por otra del acorde o una octava arriba."_u },
            { "seed", "Semilla", "Con la misma semilla, el azar sale igual cada vez."_u } }, true },
        { "Pasos 1-4", stepCards (0), true },
        { "Pasos 5-8", stepCards (4), true },
        { "Pasos 9-12", stepCards (8), true },
        { "Pasos 13-16", stepCards (12), true },
        { "Humanizar", {
            { "humtime", "Tiempo", "Retrasa un poco cada nota al azar, hasta 30 ms."_u },
            { "humvel", "Velocidad", "Cambia un poco la velocidad de cada nota."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

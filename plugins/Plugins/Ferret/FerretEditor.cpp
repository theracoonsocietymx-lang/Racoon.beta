#include "FerretEditor.h"
#include "I18n.h"

namespace racoon::ferret
{
using namespace juce;
using namespace racoon::ui;

const char* Display::tokenCycle() { return "DUAduxmo-."; }

String Display::tokenName (char t)
{
    switch (t)
    {
        case 'D': return "Abajo";
        case 'U': return "Arriba";
        case 'A': return "Abajo con acento";
        case 'd': return "Abajo fantasma (suave)";
        case 'u': return "Arriba fantasma (suave)";
        case 'x': return "Golpe muerto (chuck)";
        case 'm': return "Palm mute en graves";
        case 'o': return "Una cuerda con púa"_u;
        case '-': return "Dejar sonar";
        default:  return "Silencio (la mano apaga)";
    }
}

static void drawToken (Graphics& g, Rectangle<float> r, char t, Colour c)
{
    auto arrow = [&] (bool down, float scale, bool accent)
    {
        const float w = r.getWidth() * 0.45f * scale, h = r.getHeight() * 0.62f * scale;
        const auto cx = r.getCentreX(), cy = r.getCentreY();
        Path p;
        if (down)
        {
            p.startNewSubPath (cx, cy - h * 0.5f); p.lineTo (cx, cy + h * 0.5f);
            p.startNewSubPath (cx - w * 0.5f, cy + h * 0.1f); p.lineTo (cx, cy + h * 0.5f); p.lineTo (cx + w * 0.5f, cy + h * 0.1f);
        }
        else
        {
            p.startNewSubPath (cx, cy + h * 0.5f); p.lineTo (cx, cy - h * 0.5f);
            p.startNewSubPath (cx - w * 0.5f, cy - h * 0.1f); p.lineTo (cx, cy - h * 0.5f); p.lineTo (cx + w * 0.5f, cy - h * 0.1f);
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (accent ? 3.0f : 1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        if (accent) g.fillEllipse (cx - 2.5f, r.getY() + 2.0f, 5.0f, 5.0f);
    };
    switch (t)
    {
        case 'D': arrow (true, 1.0f, false); break;
        case 'A': arrow (true, 1.0f, true); break;
        case 'U': arrow (false, 1.0f, false); break;
        case 'd': g.setOpacity (0.6f); arrow (true, 0.6f, false); break;
        case 'u': arrow (false, 0.6f, false); break;
        case 'x':
        {
            const auto b = r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.3f);
            g.setColour (c);
            g.drawLine ({ b.getTopLeft(), b.getBottomRight() }, 2.0f);
            g.drawLine ({ b.getBottomLeft(), b.getTopRight() }, 2.0f);
            break;
        }
        case 'm': g.setColour (c); g.setFont (Fonts::label (r.getHeight() * 0.42f)); g.drawText (::racoon::i18n::tr ("PM"), r, Justification::centred); break;
        case 'o': g.setColour (c); g.fillEllipse (r.withSizeKeepingCentre (8.0f, 8.0f)); break;
        case '-': g.setColour (c.withAlpha (0.5f)); g.drawLine (r.getX() + 2.0f, r.getCentreY(), r.getRight() - 2.0f, r.getCentreY(), 2.0f); break;
        default: break;
    }
}

Display::Display (Processor& p) : proc (p)
{
    for (auto* b : { &toUser, &exportMidi, &clearLog }) addAndMakeVisible (b);
    toUser.setTooltip ("Copia el patrón que suena al estilo Usuario para cambiarlo paso por paso."_u);
    exportMidi.setTooltip ("Clic: guarda la guitarra de los acordes que tocaste (o 4 compases del acorde actual) en ~/Música/Racoon/Ferret. Arrástralo a una pista."_u);
    clearLog.setTooltip ("Olvida los acordes registrados mientras sonaba la canción."_u);
    toUser.onClick = [this] { proc.copyStyleToUser(); repaint(); };
    exportMidi.onClick = [this] { lastExport = proc.exportMidiFile (4); lastExport.revealToUser(); };
    exportMidi.addMouseListener (this, false);
    clearLog.onClick = [this] { proc.clearLog(); };
    startTimerHz (30);
}

Display::~Display() { exportMidi.removeMouseListener (this); }

void Display::resized()
{
    auto r = getLocalBounds().reduced (10);
    infoArea = r.removeFromBottom (28);
    auto top = r.removeFromTop ((int) (r.getHeight() * 0.52f));
    chordArea = top.removeFromLeft (230);
    top.removeFromLeft (10);
    fretArea = top;
    r.removeFromTop (8);
    patternArea = r;
    auto b = infoArea;
    exportMidi.setBounds (b.removeFromRight (210)); b.removeFromRight (6);
    clearLog.setBounds (b.removeFromRight (150)); b.removeFromRight (6);
    toUser.setBounds (b.removeFromRight (180));
}

Rectangle<float> Display::cellBounds (int step, int len) const
{
    const int rows = jmax (1, (len + 15) / 16);
    const float cw = (patternArea.getWidth() - 40.0f) / 16.0f, ch = jmin (56.0f, (patternArea.getHeight() - 16.0f) / (float) rows);
    return { patternArea.getX() + 40.0f + (step % 16) * cw, patternArea.getY() + 16.0f + (step / 16) * ch, cw - 3.0f, ch - 3.0f };
}

int Display::cellAt (Point<int> p) const
{
    const auto pat = proc.patternNow();
    for (int i = 0; i < pat.length(); ++i) if (cellBounds (i, pat.length()).contains (p.toFloat())) return i;
    return -1;
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    int open[numStrings];
    proc.openNotes (open);
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    // the chord
    {
        auto r = chordArea.toFloat();
        const int mode = (int) proc.value ("mode");
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (13.0f));
        g.drawText (::racoon::i18n::tr (upper (modeNames()[mode])), r.removeFromTop (18.0f), Justification::left);
        const auto name = proc.chordName();
        g.setColour (name.isEmpty() ? C::ash : C::amberHot);
        g.setFont (Fonts::title (name.length() > 6 ? 34.0f : 46.0f));
        g.drawText (::racoon::i18n::tr (name.isEmpty() ? String ("--") : name), r.removeFromTop (62.0f), Justification::left);
        g.setFont (Fonts::label (14.0f));
        g.setColour (C::cyan);
        g.drawText (::racoon::i18n::tr ("ARTICULACIÓN: "_u + upper (articulationNames()[proc.currentArt.load()])), r.removeFromTop (20.0f), Justification::left);
        g.setColour (C::bone);
        const int style = (int) proc.value ("style");
        String what = mode == instrumentMode ? String ("Tocas notas: cada una cae en una cuerda")
                    : mode == pickingMode ? "Arpegio: " + pickPatternNames()[(int) proc.value ("pickpat")]
                    : "Estilo: " + styleNames()[style] + (style < styleNames().size() - 1 ? "  ·  "_u + StringArray { "sencillo", "medio", "complejo" }[(int) proc.value ("complexity")] : String());
        g.setFont (Fonts::body (12.0f));
        g.drawText (::racoon::i18n::tr (what), r.removeFromTop (18.0f), Justification::left);
        g.setColour (C::boneDim);
        const int logged = proc.logSize();
        g.drawText (::racoon::i18n::tr (logged > 0 ? String (logged) + " cambios de acorde registrados" : String ("Toca un acorde para que la guitarra lo rasguee")), r.removeFromTop (18.0f), Justification::left);
    }

    // the fretboard: high string on top, like a tab
    {
        auto r = fretArea.toFloat();
        const float nameW = 20.0f, top = r.getY() + 8.0f, bottom = r.getBottom() - 16.0f;
        const int frets = 15;
        const float fx0 = r.getX() + nameW + 26.0f, fw = (r.getRight() - fx0 - 4.0f) / (float) frets;
        auto yOf = [&] (int s) { return top + (bottom - top) * (float) (numStrings - 1 - s) / (float) (numStrings - 1); };
        auto xOf = [&] (int f) { return f == 0 ? fx0 - 9.0f : fx0 + (f - 0.5f) * fw; };
        g.setColour (C::stone);
        g.fillRect (Rectangle<float> (fx0, top - 6.0f, fw * frets, bottom - top + 12.0f));
        for (int f : { 3, 5, 7, 9, 15 }) { g.setColour (C::ridge); g.fillEllipse (xOf (f) - 3.0f, (top + bottom) * 0.5f - 3.0f, 6.0f, 6.0f); }
        g.fillEllipse (xOf (12) - 3.0f, top + (bottom - top) * 0.3f - 3.0f, 6.0f, 6.0f);
        g.fillEllipse (xOf (12) - 3.0f, top + (bottom - top) * 0.7f - 3.0f, 6.0f, 6.0f);
        for (int f = 0; f <= frets; ++f)
        {
            g.setColour (f == 0 ? C::bone : C::ridge);
            g.fillRect (fx0 + f * fw - (f == 0 ? 2.0f : 0.5f), top - 6.0f, f == 0 ? 4.0f : 1.0f, bottom - top + 12.0f);
        }
        g.setFont (Fonts::mono (10.0f));
        g.setColour (C::boneDim);
        for (int f : { 3, 5, 7, 9, 12, 15 }) g.drawText (::racoon::i18n::tr (String (f)), Rectangle<float> (xOf (f) - 10.0f, bottom + 4.0f, 20.0f, 12.0f), Justification::centred);
        for (int s = 0; s < numStrings; ++s)
        {
            const float y = yOf (s);
            g.setColour (C::boneDim);
            g.setFont (Fonts::label (12.0f));
            g.drawText (::racoon::i18n::tr (names[((open[s] % 12) + 12) % 12]), Rectangle<float> (r.getX(), y - 8.0f, nameW, 16.0f), Justification::centredRight);
            const float lv = jlimit (0.0f, 1.0f, proc.shownLevel[s].load() * 6.0f);
            g.setColour (C::amberDim.interpolatedWith (C::amberHot, lv));
            g.fillRect (fx0, y - (s < 3 ? 1.0f : 0.6f), fw * frets, s < 3 ? 2.0f : 1.2f);
            const int vf = proc.voicingFret[s].load(), sf = proc.shownFret[s].load();
            if (vf < 0)
            {
                g.setColour (C::ochre);
                g.drawText (::racoon::i18n::tr ("x"), Rectangle<float> (fx0 - 14.0f, y - 8.0f, 12.0f, 16.0f), Justification::centred);
            }
            else if (vf <= frets)
            {
                const auto c = Rectangle<float> (12.0f, 12.0f).withCentre ({ xOf (vf), y });
                g.setColour (C::cyan);
                if (vf == 0) g.drawEllipse (c, 1.6f); else g.fillEllipse (c);
            }
            if (sf >= 0 && sf <= frets && lv > 0.02f)
            {
                g.setColour (C::amberHot.withAlpha (lv));
                g.fillEllipse (Rectangle<float> (18.0f, 18.0f).withCentre ({ xOf (sf), y }));
            }
        }
    }

    // the pattern
    {
        const auto pat = proc.patternNow();
        const bool user = (int) proc.value ("style") == styleNames().size() - 1;
        const int mode = (int) proc.value ("mode");
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (12.0f));
        String head = mode == instrumentMode ? String ("PATRÓN (SIN USO EN MODO INSTRUMENTO)"_u)
                    : mode == pickingMode ? String ("ARPEGIO: CADA PASO TOCA UNA CUERDA")
                    : user ? String ("TU PATRÓN: CLIC CAMBIA EL GOLPE, CLIC DERECHO ELIGE, ARRASTRA PARA PINTAR"_u)
                           : String ("PATRÓN DEL ESTILO (PULSA EDITAR PARA HACERLO TUYO)"_u);
        g.drawText (::racoon::i18n::tr (head), patternArea.getX(), patternArea.getY(), patternArea.getWidth(), 14, Justification::left);
        const int cur = proc.currentStep.load();
        const int len = pat.length();
        for (int row = 0; row < (len + 15) / 16; ++row)
        {
            auto c0 = cellBounds (row * 16, len);
            g.setColour (C::boneDim);
            g.drawText (::racoon::i18n::tr (String (row + 1)), Rectangle<float> ((float) patternArea.getX(), c0.getY(), 30.0f, c0.getHeight()), Justification::centredLeft);
        }
        for (int i = 0; i < len; ++i)
        {
            const auto c = cellBounds (i, len);
            const bool beat = i % 4 == 0;
            g.setColour (i == cur && mode != instrumentMode ? C::amber.withAlpha (0.35f) : (beat ? C::stoneHi : C::stone));
            g.fillRect (c);
            drawToken (g, c, (char) pat[i], mode == pickingMode ? C::boneDim : (user ? C::amberHot : C::bone));
        }
    }
}

void Display::mouseDown (const MouseEvent& e)
{
    if (e.eventComponent == &exportMidi) return;
    const int cell = cellAt (e.getPosition());
    lastDragCell = cell;
    dragToken = 0;
    if (cell < 0) return;
    const bool user = (int) proc.value ("style") == styleNames().size() - 1;
    if (! user) { proc.copyStyleToUser(); return; }
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        const String cycle (tokenCycle());
        for (int k = 0; k < cycle.length(); ++k)
        {
            const char t = (char) cycle[k];
            m.addItem (tokenName (t), [this, cell, t] { proc.setUserStep (cell, t); repaint(); });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    const String cycle (tokenCycle());
    const int at = cycle.indexOfChar ((juce_wchar) proc.userStep (cell));
    dragToken = (char) cycle[(at + 1) % cycle.length()];
    proc.setUserStep (cell, dragToken);
    repaint();
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (e.eventComponent == &exportMidi)
    {
        if (e.getDistanceFromDragStart() > 8)
        {
            lastExport = proc.exportMidiFile (4);
            DragAndDropContainer::performExternalDragDropOfFiles ({ lastExport.getFullPathName() }, false, this);
        }
        return;
    }
    if (dragToken == 0) return;
    const int cell = cellAt (e.getEventRelativeTo (this).getPosition());
    if (cell >= 0 && cell != lastDragCell) { proc.setUserStep (cell, dragToken); lastDragCell = cell; repaint(); }
}

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y el guitarrista rasguea los acordes del ejemplo."_u, "Modo cambia entre rasgueo, arpegio o tus notas tal cual."_u, "Elige guitarra, amplificador y pedales abajo."_u, "Escribe tus acordes en la secuencia, uno por compás."_u };
    s.sections = {
        { "Modo", { { "mode", "Modo", "Guitarrista rasguea el acorde que tocas; arpegio lo toca cuerda por cuerda; instrumento toca tus notas tal cual."_u },
                    { "style", "Estilo", "Funk, disco, indie dance, nu-disco, rock, post-punk, dark disco o tu propio patrón."_u },
                    { "complexity", "Complejidad", "Qué tan ocupado es el patrón."_u }, { "rate", "Rejilla", "Cuánto dura cada paso del patrón."_u } } },
        { "Mano derecha", { { "intensity", "Intensidad", "Fuerza del rasgueo: con poca, solo las cuerdas agudas."_u }, { "density", "Densidad", "Menos golpes o golpes fantasma de relleno." },
                            { "variation", "Variación"_u, "Cambia golpes de vez en cuando, distinto en cada compás."_u }, { "swing", "Swing", "" },
                            { "anticip", "Anticipación"_u, "Adelanta el golpe del compás un dieciseisavo."_u }, { "strum", "Rasgueo", "Tiempo entre una cuerda y la siguiente."_u } } },
        { "Guitarra", { { "model", "Guitarra", "Strat, humbucker, funk, disco apagada, acústica de acero, nylon o barítono."_u }, { "tuning", "Afinación"_u, "" },
                        { "art", "Articulación"_u, "También con las notas MIDI 24 a 33 (C1 a A1; en DAWs que llaman C3 al Do central, C0 a A0), debajo de la guitarra."_u }, { "tone", "Tono", "La perilla de tono de la guitarra." } } },
        { "Amplificador", { { "amp", "Modelo", "" }, { "gain", "Ganancia", "" }, { "bass", "Graves", "" }, { "mid", "Medios", "" }, { "treble", "Agudos", "" }, { "master", "Master", "" } } },
        { "Pedales", { { "comp_on", "Comp", "" }, { "wah_on", "Wah", "" }, { "od_on", "Drive", "" }, { "ch_on", "Chorus", "" }, { "dl_on", "Delay", "" }, { "rv_on", "Reverb", "" } } },
        { "Diapasón"_u, { { "strategy", "Dónde tocar"_u, "Cómo elige cuerda y traste: al aire, cerca de la mano, moviéndose poco, en graves o en agudas."_u },
                          { "position", "Traste", "Dónde está la mano."_u }, { "openstr", "Al aire", "Permite cuerdas al aire en los acordes." },
                          { "latch", "Mantener", "El acorde sigue sonando al soltar las teclas." }, { "capo", "Cejilla", "" },
                          { "off1", "6ª"_u, "Afinación propia de cada cuerda (semitonos)."_u }, { "off2", "5ª"_u, "" }, { "off3", "4ª"_u, "" }, { "off4", "3ª"_u, "" },
                          { "off5", "2ª"_u, "" }, { "off6", "1ª"_u, "" } }, true },
        { "Humano", { { "human", "Tiempo", "Pequeños retrasos como los de una persona."_u }, { "humvel", "Fuerza", "" }, { "seed", "Semilla", "Misma semilla, mismas decisiones." },
                      { "restmute", "Apagar", "En los silencios la mano apaga las cuerdas." }, { "ulen", "Largo", "Pasos del patrón Usuario."_u },
                      { "pickpat", "Arpegio", "Orden de las cuerdas en modo arpegio." } }, true },
        { "Cuerdas", { { "pickpos", "Púa"_u, "Cerca del puente suena más fino."_u }, { "hardness", "Dureza", "" }, { "release", "Soltar", "" }, { "bendamt", "Bend", "" },
                       { "vib", "Vibrato", "" }, { "ks", "Keyswitch", "" }, { "dbl", "Doble toma", "Una segunda guitarra a la derecha, como en el estudio." }, { "width", "Ancho", "" } }, true },
        { "Amp y gabinete", { { "presence", "Presencia", "" }, { "sag", "Sag", "" }, { "cabblend", "Gab./directo", "" }, { "cab", "Gabinete", "" },
                              { "micpos", "Micro pos.", "" }, { "micdist", "Distancia", "" }, { "ribbon", "Cinta", "" }, { "room", "Cuarto", "" } }, true },
        { "Comp, wah y drive", { { "comp_amt", "Compresión"_u, "" }, { "wah_mode", "Wah modo", "" }, { "wah_pos", "Wah pos.", "" }, { "wah_rate", "Wah vel.", "" },
                                 { "wah_q", "Wah Q", "" }, { "od_drive", "Drive", "" }, { "od_tone", "Tono", "" }, { "od_level", "Volumen", "" } }, true },
        { "Modulación"_u, { { "ch_rate", "Ch. vel.", "" }, { "ch_depth", "Ch. prof.", "" }, { "ch_mix", "Ch. mezcla", "" }, { "ph_on", "Phaser", "" }, { "ph_rate", "Ph. vel.", "" },
                            { "ph_depth", "Ph. prof.", "" }, { "ph_mix", "Ph. mezcla", "" }, { "tr_on", "Tremolo", "" }, { "tr_rate", "Tr. vel.", "" }, { "tr_depth", "Tr. prof.", "" } }, true },
        { "Delay y reverb", { { "dl_div", "Tiempo", "" }, { "dl_fb", "Repet.", "" }, { "dl_mix", "Delay", "" }, { "dl_tone", "Tono", "" }, { "rv_size", "Tamaño"_u, "" },
                              { "rv_damp", "Apagado", "" }, { "rv_mix", "Reverb", "" }, { "outgain", "Volumen", "" } }, true },
    };
    s.width = 1240;
    s.displayEasy = 330;
    s.displayFull = 330;
    s.easyHeight = 680;
    s.advancedHeight = 200;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

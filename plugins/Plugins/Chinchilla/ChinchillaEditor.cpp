#include "ChinchillaEditor.h"
#include "I18n.h"

namespace racoon::chinchilla
{
using namespace juce;
using namespace racoon::ui;

namespace
{
const char* hKeysUi[] { "on", "mode", "int", "lvl", "pan", "form", "delay", "det" };
const char* hLabels[] { "Encendida", "Modo", "Intervalo", "Volumen", "Lugar", "Formantes", "Retraso", "Desafinar" };
}

Display::Display (Processor& p) : proc (p)
{
    for (auto name : { "AFINACIÓN", "EDITOR DE NOTAS", "ARMONÍAS", "MATRIZ", "CHOPPER Y ANALIZADOR" })
    {
        auto* b = tabs.add (new TextButton (String::fromUTF8 (name)));
        const int idx = tabs.size() - 1;
        b->onClick = [this, idx] { showPage (idx); };
        addAndMakeVisible (b);
    }
    showPage (0);
    startTimerHz (30);
}

Display::~Display()
{
    for (auto* b : buttons) b->removeMouseListener (this);
}

void Display::showPage (int p)
{
    page = p;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == page, dontSendNotification);
    rebuild();
    resized();
    repaint();
}

void Display::rebuild()
{
    for (auto* b : buttons) b->removeMouseListener (this);
    controls.clear();
    buttons.clear();
    auto addCtl = [this] (const String& id, const String& label) { addAndMakeVisible (controls.add (new ParamControl (proc, Ctl { id, label, {} }))); };
    auto addButton = [this] (const String& text, std::function<void()> f)
    {
        auto* b = buttons.add (new TextButton (text));
        b->onClick = std::move (f);
        addAndMakeVisible (b);
        return b;
    };
    switch (page)
    {
        case 0:
            addCtl ("tune_on", "Afinar"); addCtl ("key", "Tonalidad"); addCtl ("scale", "Escala"); addCtl ("target", "Hacia");
            addCtl ("retune", "Velocidad"); addCtl ("humanize", "Humanizar"); addCtl ("formant", "Formantes");
            break;
        case 1:
        {
            addButton (proc.capturing() ? "PARAR Y VER NOTAS" : "CAPTURAR (DALE PLAY)", [this]
            {
                if (proc.capturing()) proc.stopCapture();
                else proc.startCapture();
                rebuild(); resized();
            });
            auto* ex = addButton ("EXPORTAR MIDI (ARRASTRA)", [this] { lastExport = proc.exportMidiFile(); lastExport.revealToUser(); });
            ex->addMouseListener (this, false);
            addButton ("BORRAR NOTAS", [this] { proc.clearSegments(); repaint(); });
            break;
        }
        case 2:
            for (int k = 0; k < 8; ++k) addCtl (Processor::hId (voice, hKeysUi[k]), String::fromUTF8 (hLabels[k]));
            addCtl ("h_on", "Armonías"_u); addCtl ("h_mix", "Volumen");
            break;
        case 3:
            for (int r = 0; r < numRoutes; ++r)
            {
                addCtl (Processor::mId (r, "src"), "Fuente " + String (r + 1));
                addCtl (Processor::mId (r, "dst"), "Destino");
                addCtl (Processor::mId (r, "amt"), "Cantidad");
            }
            break;
        default:
            addCtl ("ch_on", "Chopper"); addCtl ("ch_div", "Rejilla"); addCtl ("ch_steps", "Pasos"); addCtl ("ch_depth", "Profundidad");
            addCtl ("ch_smooth", "Suavizado"); addCtl ("ch_stutter", "Repeticiones");
            break;
    }
}

void Display::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto tabRow = r.removeFromTop (26);
    const int tw = tabRow.getWidth() / tabs.size();
    for (auto* t : tabs) t->setBounds (tabRow.removeFromLeft (tw).reduced (2, 0));
    r.removeFromTop (8);
    graphArea = r;
    controlsArea = {};
    stepsArea = {};
    auto layoutRow = [this] (Rectangle<int> c, int from, int to)
    {
        for (int i = from; i < to; ++i) controls[i]->setBounds (c.removeFromLeft (c.getWidth() / (to - i)));
    };
    switch (page)
    {
        case 1:
        {
            auto b = graphArea.removeFromBottom (30);
            for (int i = 0; i < buttons.size(); ++i) buttons[i]->setBounds (b.removeFromLeft (b.getWidth() / (buttons.size() - i)).reduced (3, 0));
            graphArea.removeFromBottom (8);
            break;
        }
        case 3:
        {
            controlsArea = graphArea;
            auto c = controlsArea.reduced (8, 6);
            const int rowH = c.getHeight() / 4;
            for (int row = 0; row < 4; ++row)
            {
                auto line = c.removeFromTop (rowH);
                auto left = line.removeFromLeft (line.getWidth() / 2);
                layoutRow (left.reduced (4, 2), row * 3, row * 3 + 3);
                layoutRow (line.reduced (4, 2), (row + 4) * 3, (row + 4) * 3 + 3);
            }
            graphArea = {};
            break;
        }
        default:
        {
            controlsArea = graphArea.removeFromBottom (100);
            graphArea.removeFromBottom (8);
            layoutRow (controlsArea.reduced (8, 6).withTrimmedTop (6), 0, controls.size());
            if (page == 4)
            {
                stepsArea = graphArea.removeFromBottom (48);
                graphArea.removeFromBottom (8);
            }
            break;
        }
    }
}

Rectangle<float> Display::stepCell (int s) const
{
    const float w = stepsArea.getWidth() / (float) chopSteps;
    return { stepsArea.getX() + s * w + 2.0f, (float) stepsArea.getY(), w - 4.0f, (float) stepsArea.getHeight() };
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    if (! controlsArea.isEmpty()) Draw::panel (g, controlsArea.toFloat());

    if (page == 0)
    {
        // pitch over time: the voice (white) and where it was taken (yellow)
        auto r = graphArea.toFloat();
        const float now = proc.detectedMidi.load();
        if (now > 0.0f) viewCentre += (now - viewCentre) * 0.05f;
        const float lo = std::floor (viewCentre) - 9.0f, hi = lo + 18.0f;
        bool mask[12];
        scaleMask ((int) proc.value ("scale"), (int) proc.value ("key"), mask);
        auto yOf = [&] (float m) { return r.getBottom() - (m - lo) / (hi - lo) * r.getHeight(); };
        for (int nn = (int) lo; nn <= (int) hi; ++nn)
        {
            const float y = yOf ((float) nn);
            g.setColour (mask[((nn % 12) + 12) % 12] ? C::stoneHi : C::basalt);
            g.fillRect (r.getX(), y - r.getHeight() / 36.0f, r.getWidth(), r.getHeight() / 18.0f - 1.0f);
            g.setColour (C::boneDim);
            g.setFont (Fonts::mono (10.0f));
            if (mask[((nn % 12) + 12) % 12]) g.drawText (::racoon::i18n::tr (noteName (nn)), Rectangle<float> (r.getX() + 4.0f, y - 6.0f, 40.0f, 12.0f), Justification::left);
        }
        auto curve = [&] (std::atomic<float>* hist, Colour c, float width)
        {
            Path p;
            bool started = false;
            const int head = proc.historyPos.load();
            for (int k = 0; k < pitchHistory; ++k)
            {
                const float m = hist[(head + k) % pitchHistory].load();
                const float x = r.getX() + 46.0f + (r.getWidth() - 46.0f) * k / (float) pitchHistory;
                if (m <= 0.0f || m < lo - 2.0f || m > hi + 2.0f) { started = false; continue; }
                if (! started) { p.startNewSubPath (x, yOf (m)); started = true; } else p.lineTo (x, yOf (m));
            }
            Draw::glowPath (g, p, c, width);
        };
        curve (proc.detectedHistory, C::boneDim, 1.2f);
        curve (proc.correctedHistory, C::amberHot, 2.0f);
        // the note and how far it is
        auto box = r.removeFromRight (170.0f).reduced (8.0f);
        g.setColour (C::void_.withAlpha (0.7f));
        g.fillRect (box.removeFromTop (110.0f));
        const float t = proc.targetMidi.load();
        g.setColour (C::amberHot);
        g.setFont (Fonts::title (34.0f));
        g.drawText (::racoon::i18n::tr (t > 0.0f ? noteName (roundToInt (t)) : String ("--")), box.withY (r.getY() + 8.0f).withHeight (44.0f), Justification::centred);
        g.setFont (Fonts::label (13.0f));
        g.setColour (C::bone);
        const float cents = now > 0.0f && t > 0.0f ? (t - now) * 100.0f : 0.0f;
        g.drawText (::racoon::i18n::tr (now > 0.0f ? String ("CORRIGE ") + (cents >= 0 ? "+" : "") + String (roundToInt (cents)) + " CENTS" : String ("SIN VOZ")),
                    box.withY (r.getY() + 56.0f).withHeight (18.0f), Justification::centred);
        g.drawText (::racoon::i18n::tr ("CLARIDAD " + String (roundToInt (proc.clarityNow.load() * 100.0f)) + " %"), box.withY (r.getY() + 78.0f).withHeight (18.0f), Justification::centred);
        return;
    }

    if (page == 1)
    {
        auto r = graphArea.toFloat();
        drawScreenGrid (g, r, 16, 6);
        const auto segs = proc.segments();
        g.setFont (Fonts::label (12.0f));
        if (segs.empty())
        {
            g.setColour (C::bone);
            g.drawFittedText (::racoon::i18n::tr (proc.capturing()
                                  ? "Capturando: dale play y canta o reproduce la pista de voz. " + String (proc.capturedFrames.load()) + " lecturas."
                                  : String ("Pulsa CAPTURAR, dale play a la canción con la voz y luego PARAR: aparecen las notas que cantaste. Arrastra una nota arriba o abajo para cambiarla."_u)),
                              r.reduced (20.0f).toNearestInt(), Justification::centred, 3);
            return;
        }
        const double start = segs.front().start, end = segs.back().end;
        int lo = 127, hi = 0;
        for (auto& s : segs) { lo = jmin (lo, s.note + s.offset); hi = jmax (hi, s.note + s.offset); }
        lo -= 3; hi += 3;
        auto xOf = [&] (double p) { return r.getX() + 40.0f + (float) ((p - start) / jmax (0.001, end - start)) * (r.getWidth() - 50.0f); };
        auto yOf = [&] (int nn) { return r.getBottom() - (float) (nn - lo + 0.5f) / (float) (hi - lo + 1) * r.getHeight(); };
        const float rowH = r.getHeight() / (float) (hi - lo + 1);
        for (int nn = lo; nn <= hi; ++nn)
            if (nn % 12 == 0 || nn == lo)
            {
                g.setColour (C::boneDim);
                g.drawText (::racoon::i18n::tr (noteName (nn)), Rectangle<float> (r.getX() + 2.0f, yOf (nn) - 6.0f, 36.0f, 12.0f), Justification::left);
            }
        for (size_t i = 0; i < segs.size(); ++i)
        {
            const auto& s = segs[i];
            const auto box = Rectangle<float> (xOf (s.start), yOf (s.note + s.offset) - rowH * 0.45f, jmax (3.0f, xOf (s.end) - xOf (s.start) - 1.0f), rowH * 0.9f);
            if (s.offset != 0)
            {
                g.setColour (C::ash);
                g.drawRect (Rectangle<float> (box.getX(), yOf (s.note) - rowH * 0.45f, box.getWidth(), rowH * 0.9f), 1.0f);
            }
            g.setColour ((int) i == dragSeg ? C::amberHot : (s.offset != 0 ? C::cyan : C::amber));
            g.fillRect (box);
        }
        return;
    }

    if (page == 2)
    {
        auto r = graphArea.toFloat();
        const float w = r.getWidth() / numHarmonies;
        bool mask[12];
        scaleMask ((int) proc.value ("scale"), (int) proc.value ("key"), mask);
        for (int v = 0; v < numHarmonies; ++v)
        {
            auto col = Rectangle<float> (r.getX() + v * w, r.getY(), w, r.getHeight()).reduced (4.0f);
            const bool on = proc.value ("h_on") > 0.5f && proc.value (Processor::hId (v, "on")) > 0.5f;
            g.setColour (v == voice ? C::stoneHi : C::stone);
            g.fillRect (col);
            g.setColour (on ? C::amberHot : C::ash);
            g.setFont (Fonts::label (13.0f));
            g.drawText (::racoon::i18n::tr ("VOZ " + String (v + 1)), col.removeFromTop (22.0f), Justification::centred);
            const int mode = (int) proc.value (Processor::hId (v, "mode"));
            const int iv = (int) proc.value (Processor::hId (v, "int"));
            String what = mode == 2 ? "Nota MIDI " + String (v + 1) : (iv > 0 ? "+" : "") + String (iv) + (mode == 1 ? " grados" : " st");
            g.setColour (on ? C::bone : C::boneDim);
            g.setFont (Fonts::title (16.0f));
            g.drawText (::racoon::i18n::tr (what), col.removeFromTop (30.0f), Justification::centred);
            // where it sits on the stage
            const float pan = proc.value (Processor::hId (v, "pan")) * 0.01f;
            auto bar = col.removeFromBottom (16.0f).reduced (8.0f, 4.0f);
            g.setColour (C::ridge);
            g.fillRect (bar);
            g.setColour (C::cyan);
            g.fillEllipse (bar.getX() + (pan * 0.5f + 0.5f) * bar.getWidth() - 4.0f, bar.getCentreY() - 4.0f, 8.0f, 8.0f);
            const float lvl = jlimit (0.0f, 1.0f, (proc.value (Processor::hId (v, "lvl")) + 30.0f) / 36.0f);
            auto meter = col.reduced (col.getWidth() * 0.35f, 8.0f);
            g.setColour (C::ridge);
            g.fillRect (meter);
            g.setColour (on ? C::amber : C::ash);
            g.fillRect (meter.withTrimmedTop (meter.getHeight() * (1.0f - lvl)));
        }
        return;
    }

    if (page == 4)
    {
        auto r = graphArea.toFloat();
        drawScreenGrid (g, r, 10, 4);
        Path p;
        for (int b = 0; b < 64; ++b)
        {
            const float db = jlimit (-90.0f, 0.0f, proc.spectrum[b].load());
            const float x = r.getX() + r.getWidth() * b / 63.0f, y = r.getY() + r.getHeight() * (-db / 90.0f);
            if (b == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        Draw::glowPath (g, p, C::cyan);
        // vocoder bands
        if (proc.value ("voc_on") > 0.5f)
        {
            const float bw = r.getWidth() / Vocoder::bands;
            for (int b = 0; b < Vocoder::bands; ++b)
            {
                const float v = jlimit (0.0f, 1.0f, (racoon::dsp::gainToDb (proc.vocoderBands[b].load()) + 60.0f) / 60.0f);
                g.setColour (C::amber.withAlpha (0.5f));
                g.fillRect (Rectangle<float> (r.getX() + b * bw + 2.0f, r.getBottom() - v * r.getHeight() * 0.5f, bw - 4.0f, v * r.getHeight() * 0.5f));
            }
        }
        g.setColour (C::boneDim);
        g.setFont (Fonts::label (11.0f));
        g.drawText (::racoon::i18n::tr ("ESPECTRO DE LA SALIDA" + String (proc.value ("voc_on") > 0.5f ? "  ·  BANDAS DEL VOCODER"_u : String())), r.reduced (6.0f).removeFromTop (14.0f), Justification::left);
        // chopper steps
        const int steps = (int) proc.value ("ch_steps");
        const int cur = proc.chopStepNow.load();
        for (int s = 0; s < chopSteps; ++s)
        {
            const auto c = stepCell (s);
            const bool on = proc.value (Processor::chId (s)) > 0.5f;
            g.setColour (s >= steps ? C::basalt : (on ? (s == cur && proc.value ("ch_on") > 0.5f ? C::amberHot : C::amber) : C::stone));
            g.fillRect (c);
            g.setColour (C::void_);
            g.setFont (Fonts::label (11.0f));
            g.drawText (::racoon::i18n::tr (String (s + 1)), c, Justification::centred);
        }
    }
}

void Display::mouseDown (const MouseEvent& e)
{
    if (page == 1 && buttons.size() > 1 && e.eventComponent == buttons[1]) return;
    const auto pos = e.getEventRelativeTo (this).position;
    if (page == 2 && graphArea.contains (pos.toInt()))
    {
        const int v = jlimit (0, numHarmonies - 1, (int) ((pos.x - graphArea.getX()) / (graphArea.getWidth() / (float) numHarmonies)));
        if (v != voice) { voice = v; rebuild(); resized(); repaint(); }
        return;
    }
    if (page == 4)
    {
        for (int s = 0; s < chopSteps; ++s)
            if (stepCell (s).contains (pos))
            {
                proc.setRealValue (Processor::chId (s), proc.value (Processor::chId (s)) > 0.5f ? 0.0f : 1.0f);
                repaint();
                return;
            }
    }
    if (page == 1 && graphArea.contains (pos.toInt()))
    {
        const auto segs = proc.segments();
        dragSeg = -1;
        if (segs.empty()) return;
        auto r = graphArea.toFloat();
        const double start = segs.front().start, end = segs.back().end;
        int lo = 127, hi = 0;
        for (auto& s : segs) { lo = jmin (lo, s.note + s.offset); hi = jmax (hi, s.note + s.offset); }
        lo -= 3; hi += 3;
        auto xOf = [&] (double p) { return r.getX() + 40.0f + (float) ((p - start) / jmax (0.001, end - start)) * (r.getWidth() - 50.0f); };
        for (size_t i = 0; i < segs.size(); ++i)
            if (pos.x >= xOf (segs[i].start) && pos.x <= jmax (xOf (segs[i].end), xOf (segs[i].start) + 3.0f))
            {
                dragSeg = (int) i;
                dragStartOffset = segs[i].offset;
                if (e.mods.isPopupMenu()) { proc.setSegmentOffset (dragSeg, 0); dragSeg = -1; }
                break;
            }
        repaint();
    }
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (page == 1 && buttons.size() > 1 && e.eventComponent == buttons[1])
    {
        if (e.getDistanceFromDragStart() > 8)
        {
            lastExport = proc.exportMidiFile();
            DragAndDropContainer::performExternalDragDropOfFiles ({ lastExport.getFullPathName() }, false, this);
        }
        return;
    }
    if (page != 1 || dragSeg < 0) return;
    const auto segs = proc.segments();
    int lo = 127, hi = 0;
    for (auto& s : segs) { lo = jmin (lo, s.note); hi = jmax (hi, s.note); }
    const float rowH = graphArea.getHeight() / (float) jmax (1, hi - lo + 7);
    const int delta = -roundToInt (e.getDistanceFromDragStartY() / rowH);
    proc.setSegmentOffset (dragSeg, dragStartOffset + delta);
    repaint();
}

void Display::mouseUp (const MouseEvent&) { dragSeg = -1; repaint(); }

// ===================================================================== editor
static EditorSpec makeSpec()
{
    EditorSpec s;
    s.steps = { "Dale Play y canta una voz de ejemplo."_u, "Prende Afinar y elige tonalidad y escala."_u, "Prende Armonías para sumar voces a la tuya."_u, "Apaga Sonido de ejemplo y canta con tu micrófono, o ponlo en tu pista de voz."_u };
    s.sections = {
        { "Afinación"_u, { { "tune_on", "Afinar", "" }, { "key", "Tonalidad", "" }, { "scale", "Escala", "" },
                           { "retune", "Velocidad", "0 ms es el efecto robótico; más tiempo suena natural."_u },
                           { "humanize", "Humanizar", "Deja el vibrato y las curvas largas; corrige el centro de la nota." } } },
        { "Voz", { { "target", "Hacia", "La escala, las notas que tocas o las notas del editor." }, { "formant", "Formantes", "El tamaño de la garganta sin cambiar la nota."_u },
                   { "transpose", "Transponer", "" }, { "amount", "Cantidad", "" } } },
        { "Armonías"_u, { { "h_on", "Encendidas", "Ocho voces: intervalo fijo, grados de la escala o las notas MIDI que tocas." }, { "h_mix", "Volumen", "" },
                          { "tm_tight", "Juntas", "Qué tan pegadas entran a la voz principal."_u } } },
        { "Efectos de voz", { { "dbl_on", "Doble", "" }, { "voc_on", "Vocoder", "Toca notas MIDI o usa un sidechain como portadora." },
                              { "tb_on", "Talkbox", "" }, { "rb_on", "Robot", "" }, { "ch_on", "Chopper", "" } } },
        { "Salida", { { "rv_on", "Reverb", "" }, { "rv_mix", "Mezcla rev.", "" }, { "mix", "Mezcla", "" }, { "outgain", "Volumen", "" } } },
        { "Limpieza", { { "cl_hp", "Corte graves", "" }, { "cl_gate", "Puerta", "" }, { "cl_deess", "De-esser", "" }, { "cl_comp", "Compresión"_u, "" },
                        { "cl_breath", "Respiraciones", "" } }, true },
        { "Doble", { { "dbl_voices", "Voces", "" }, { "dbl_width", "Ancho", "" }, { "dbl_spread", "Separación"_u, "" }, { "dbl_detune", "Desafinar", "" },
                     { "dbl_mix", "Volumen", "" } }, true },
        { "Vocoder y sinte", { { "voc_mix", "Mezcla", "" }, { "voc_carrier", "Portadora", "" }, { "voc_q", "Definición"_u, "" }, { "voc_sib", "Eses", "" },
                               { "voc_shift", "Formantes", "" }, { "syn_wave", "Onda", "" }, { "syn_detune", "Desafinar", "" }, { "syn_glide", "Portamento", "" } }, true },
        { "Talkbox y robot", { { "tb_mix", "Talkbox", "" }, { "tb_bright", "Brillo", "" }, { "rb_mode", "Robot nota", "" }, { "rb_note", "Nota fija", "" },
                               { "rb_amount", "Cantidad", "" }, { "rb_metal", "Metal", "" }, { "rb_metalhz", "Frec. metal", "" } }, true },
        { "Modulación"_u, { { "lfo1_rate", "LFO 1", "" }, { "lfo1_shape", "Forma 1", "" }, { "lfo2_rate", "LFO 2", "" }, { "lfo2_shape", "Forma 2", "" } }, true },
        { "Efectos", { { "sat", "Saturación"_u, "" }, { "flt", "Filtro", "" }, { "dl_on", "Delay", "" }, { "dl_div", "Tiempo", "" }, { "dl_fb", "Repet.", "" },
                       { "dl_mix", "Mezcla", "" }, { "rv_size", "Tamaño rev."_u, "" } }, true },
    };
    s.width = 1240;
    s.displayEasy = 340;
    s.displayFull = 340;
    s.easyHeight = 680;
    s.advancedHeight = 200;
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

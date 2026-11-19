#include "ChinchillaEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::chinchilla
{
using namespace juce;
using namespace racoon::ui;

Display::Display (Processor& p) : proc (p)
{
    clearButton.setTooltip ("Corta la cola que está sonando (útil con Congelar o con duraciones largas)"_u);
    clearButton.onClick = [this] { proc.clearRequest.store (true); };
    irButton.setTooltip (String::fromUTF8 ("Usa una respuesta al impulso tuya (WAV, AIFF o FLAC)"));
    irButton.onClick = [this]
    {
        chooser = std::make_shared<FileChooser> ("Elegir IR", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) proc.loadUserIR (fc.getResult());
        });
    };
    addAndMakeVisible (clearButton);
    addChildComponent (irButton);
    startTimerHz (20);
}

void Display::resized()
{
    auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4).reduced (14, 12);
    auto top = r.removeFromTop (26);
    clearButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (6);
    irButton.setBounds (top.removeFromRight (120));
}

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    auto all = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    auto head = all.removeFromTop (26.0f);
    all.removeFromTop (8.0f);
    auto r = all.withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
    const bool conv = (int) proc.value ("algo") == 1;
    const bool freeze = proc.value ("freeze") > 0.5f;
    const float rt = proc.value ("rt60");
    const float damp = proc.value ("damping") * 0.01f;
    const float bass = proc.value ("bassmult");
    const float pre = proc.value ("predelay") * 0.001f;
    const float span = jmax (1.0f, jmin (jmax (rt, rt * bass) * 1.15f + pre, 30.0f));
    auto tx = [&] (float t) { return r.getX() + r.getWidth() * t / span; };
    auto dy = [&] (float db) { return r.getY() + r.getHeight() * jlimit (0.0f, 1.0f, -db / 66.0f); };

    // grid with its numbers: dB on the left, seconds below
    g.setFont (NumFont::medium (9.5f));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        g.setColour (Pro::grid);
        g.fillRect (r.getX(), dy (db), r.getWidth(), 1.0f);
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr (String ((int) db)), Rectangle<float> (r.getX() - 30.0f, dy (db) - 6.0f, 26.0f, 12.0f), Justification::centredRight);
    }
    const float tick = span > 12.0f ? 4.0f : span > 6.0f ? 2.0f : span > 3.0f ? 1.0f : span > 1.5f ? 0.5f : 0.25f;
    for (float t = 0.0f; t <= span + 1.0e-4f; t += tick)
    {
        g.setColour (Pro::grid);
        g.fillRect (tx (t), r.getY(), 1.0f, r.getHeight());
        g.setColour (Pro::textFaint);
        g.drawText (::racoon::i18n::tr (String (t, tick < 1.0f ? 2 : 0) + " s"), Rectangle<float> (tx (t) - 22.0f, r.getBottom() + 3.0f, 44.0f, 12.0f), Justification::centred);
    }

    // the decay of each band: -60 dB after its own time; it stops where it falls off the bottom
    auto bandPath = [&] (float bandRt)
    {
        Path p;
        p.startNewSubPath (tx (pre), dy (-3.0f));
        for (int i = 1; i <= 160; ++i)
        {
            const float t = pre + (span - pre) * (float) i / 160.0f;
            const float db = freeze ? -3.0f : -60.0f * (t - pre) / jmax (0.05f, bandRt) - 3.0f;
            p.lineTo (tx (t), dy (db));
            if (db < -66.0f) break;
        }
        return p;
    };
    const auto mid = bandPath (rt);
    {
        Path fill (mid);
        fill.lineTo (fill.getCurrentPosition().x, r.getBottom());
        fill.lineTo (tx (pre), r.getBottom());
        fill.closeSubPath();
        g.setColour (Pro::value.withAlpha (0.10f));
        g.fillPath (fill);
    }
    g.setColour (Pro::second);
    g.strokePath (bandPath (rt * bass), PathStrokeType (1.4f));
    g.setColour (Pro::reduce);
    g.strokePath (bandPath (rt / (1.0f + 3.0f * damp)), PathStrokeType (1.4f));
    g.setColour (Pro::value);
    g.strokePath (mid, PathStrokeType (2.2f));
    // predelay
    if (pre > 0.0005f)
    {
        const float dash[] { 3.0f, 3.0f };
        g.setColour (Pro::textDim);
        g.drawDashedLine ({ tx (pre), r.getY(), tx (pre), r.getBottom() }, dash, 2, 1.0f);
    }
    // early reflections
    if (! conv)
    {
        const float er = proc.value ("early") * 0.01f;
        const float sizeF = 0.25f + proc.value ("size") * 0.01f * 0.875f;
        const float ms[] { 4.3f, 7.9f, 11.3f, 15.7f, 19.1f, 23.9f, 29.3f, 34.1f, 41.3f, 47.9f, 55.1f, 63.7f };
        float gg = 0.7f;
        g.setColour (Pro::input);
        for (float m : ms)
        {
            const float t = pre + m * sizeF * 0.001f;
            const float db = 20.0f * std::log10 (jmax (1.0e-4f, gg * er));
            g.fillRect (tx (t) - 1.0f, dy (db), 2.0f, r.getBottom() - dy (db));
            gg *= 0.86f;
        }
    }
    // legend, top right of the plot
    {
        auto leg = r.withHeight (16.0f).translated (0.0f, 4.0f);
        leg = leg.withLeft (leg.getRight() - 400.0f);
        auto item = [&] (const String& name, Colour col)
        {
            g.setColour (col);
            g.fillRect (leg.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 2.0f));
            leg.removeFromLeft (5.0f);
            ProDraw::caps (g, name, leg.removeFromLeft (name.length() > 7 ? 92.0f : 70.0f), Justification::centredLeft, Pro::textDim, 9.5f);
        };
        item ("Medios", Pro::value);
        item ("Graves", Pro::second);
        item ("Agudos", Pro::reduce);
        if (! conv) item ("Reflexiones", Pro::input);
    }

    // header: the kind and the time, then the live level of the tail
    {
        auto h = head;
        ProDraw::caps (g, conv ? String::fromUTF8 ("Convolución") : String::fromUTF8 ("Algorítmica"), h.removeFromLeft (110.0f), Justification::centredLeft, Pro::textDim, 11.0f);
        String what = conv ? Processor::irNames()[(int) proc.value ("ir")] : String (rt, rt < 10 ? 2 : 1) + " s";
        if (conv && (int) proc.value ("ir") == 5)
            what << "  " << (proc.getUserIR().existsAsFile() ? proc.getUserIR().getFileName() : String::fromUTF8 ("(elige un archivo)"));
        g.setColour (Pro::text);
        g.setFont (NumFont::semi (15.0f));
        g.drawText (::racoon::i18n::tr (what), h.removeFromLeft (260.0f), Justification::centredLeft);
        if (freeze) ProDraw::caps (g, "Congelada", h.removeFromLeft (100.0f), Justification::centredLeft, Pro::input, 11.0f);
        const float wl = proc.wetLevel.load();
        const float wdb = jmax (-60.0f, dsp::gainToDb (wl));
        auto m = head.withLeft (head.getRight() - 440.0f).withWidth (170.0f);
        ProDraw::caps (g, "Cola", m.removeFromLeft (40.0f), Justification::centredLeft, Pro::textDim, 9.5f);
        auto bar = m.removeFromLeft (80.0f).reduced (0.0f, 10.0f);
        g.setColour (Pro::raised);
        g.fillRect (bar);
        g.setColour (Pro::input);
        g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, (wdb + 60.0f) / 60.0f)));
        g.setColour (Pro::textDim);
        g.setFont (NumFont::medium (10.5f));
        g.drawText (::racoon::i18n::tr (wdb <= -59.5f ? String ("-inf") : String (wdb, 1)), m.withTrimmedLeft (6.0f), Justification::centredLeft);
    }
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 960;
    s.easyHeight = 600;
    s.advancedHeight = 190;
    s.sections = {
        { "Espacio", {
            { "algo", "Tipo", "Algorítmica se ajusta en todo; Convolución usa respuestas de espacios (originales de Racoon o tuyas)."_u },
            { "ir", "Espacio", "En Convolución: Sala, Cámara, Placa, Habitación, Catedral o un archivo tuyo."_u },
            { "rt60", "Duración"_u, "Cuánto tarda la cola en apagarse (RT60)."_u },
            { "size", "Tamaño"_u, "Tamaño del espacio: cambia la separación de las reflexiones."_u },
            { "predelay", "Predelay", "Silencio antes de la reverb. Separa la voz de su espacio."_u },
            { "damping", "Amortiguación"_u, "Cuánto más rápido se apagan los agudos: alto suena más oscuro y cálido."_u },
            { "mix", "Mezcla", "Cuánta reverb se escucha. En una pista de retorno usa «Solo efecto»."_u } } },
        { "Retorno", {
            { "wetonly", "Solo efecto", "Saca solo la reverb, sin la señal original: para pistas de retorno."_u },
            { "freeze", "Congelar", "Sostiene la cola sin fin y no deja entrar nada nuevo."_u } } },
        { "Textura", {
            { "diffusion", "Difusión"_u, "Qué tan rápido se vuelve densa la cola. Bajo deja oír ecos sueltos."_u },
            { "early", "Reflexiones", "Nivel de los primeros rebotes, los que dicen el tamaño del cuarto."_u },
            { "bassmult", "Graves", "La cola de graves dura más (más de 1x) o menos que la de medios."_u },
            { "moddepth", "Modulación"_u, "Movimiento leve en la cola para que no suene metálica."_u },
            { "modrate", "Velocidad", "Velocidad de esa modulación."_u } }, true },
        { "EQ y estéreo"_u, {
            { "hp", "Graves", "Quita graves a la reverb para que no embarre la mezcla."_u },
            { "lp", "Agudos", "Quita agudos a la reverb."_u },
            { "width", "Anchura", "Abre o cierra el estéreo de la cola."_u },
            { "duck", "Ducking", "Baja la reverb mientras suena la voz o el instrumento."_u } }, true },
        { "Ajustes", {
            { "presync", "Predelay a tempo", "El predelay sigue el tempo del proyecto."_u },
            { "prediv", "División"_u, "Predelay a tempo (máximo 250 ms)."_u },
            { "quality", "Calidad", "ECO gasta menos CPU (menos líneas y IR más corta); HQ suena más densa."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

#include "FerretEditor.h"
#include "engine/I18n.h"
#include "ui/ProLook.h"

namespace racoon::ferret
{
using namespace juce;
using namespace racoon::ui;

void Display::paint (Graphics& g)
{
    drawScreen (g, getLocalBounds().toFloat());
    auto all = getLocalBounds().toFloat().withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    const int mode = (int) proc.value ("mode");
    const float fb = proc.value ("freeze") > 0.5f ? 1.0f : proc.value ("feedback") * 0.01f;
    const double tl = proc.delaySecondsFor (0), tr = proc.delaySecondsFor (1);
    const double span = jmax (2.0, jmax (tl, tr) * 6.0);
    const double beat = 60.0 / jmax (40.0, proc.bpm());
    auto r = all.withTrimmedBottom (16.0f).withTrimmedLeft (34.0f);
    auto xFor = [&] (double t) { return r.getX() + r.getWidth() * (float) (t / span); };

    // beats as faint lines, every bar stronger, with the time below
    g.setFont (NumFont::medium (9.5f));
    int n = 0;
    for (double t = 0.0; t <= span + 1.0e-6; t += beat, ++n)
    {
        const bool bar = n % 4 == 0;
        g.setColour (bar ? Pro::line : Pro::grid);
        g.fillRect (xFor (t), r.getY(), 1.0f, r.getHeight());
        if (bar || span <= 4.5)
        {
            g.setColour (Pro::textFaint);
            g.drawText (::racoon::i18n::tr (t < 1.0 ? String (roundToInt (t * 1000.0)) + " ms" : String (t, 2) + " s"),
                        Rectangle<float> (xFor (t) - 26.0f, r.getBottom() + 3.0f, 52.0f, 12.0f), Justification::centred);
        }
    }
    g.setColour (Pro::line);
    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
    auto laneL = r.withHeight (r.getHeight() * 0.5f).withTrimmedTop (20.0f).withTrimmedBottom (4.0f);
    auto laneR = r.withTrimmedTop (r.getHeight() * 0.5f).withTrimmedTop (20.0f).withTrimmedBottom (4.0f);
    ProDraw::caps (g, "L", Rectangle<float> (all.getX(), laneL.getCentreY() - 7.0f, 28.0f, 14.0f), Justification::centredLeft, Pro::textDim, 11.0f);
    ProDraw::caps (g, "R", Rectangle<float> (all.getX(), laneR.getCentreY() - 7.0f, 28.0f, 14.0f), Justification::centredLeft, Pro::textDim, 11.0f);

    // the original hit
    g.setColour (Pro::text);
    g.fillRect (r.getX(), laneL.getY(), 2.0f, laneL.getHeight());
    g.fillRect (r.getX(), laneR.getY(), 2.0f, laneR.getHeight());

    auto echo = [&] (double t, float level, Rectangle<float> lane, Colour col)
    {
        if (t > span || level < 0.01f) return;
        const float x = xFor (t);
        const float h = lane.getHeight() * jlimit (0.0f, 1.0f, level);
        g.setColour (col.withAlpha (0.18f));
        g.fillRect (x - 6.0f, lane.getBottom() - h, 12.0f, h);
        g.setColour (col);
        g.fillRect (x - 1.5f, lane.getBottom() - h, 3.0f, h);
        g.fillRect (x - 5.0f, lane.getBottom() - h, 10.0f, 2.0f);
    };
    const float tone = jlimit (0.4f, 1.0f, 1.0f - 0.25f * (std::log10 (proc.value ("hp") / 20.0f) / 1.7f) - 0.25f * (1.0f - std::log10 (proc.value ("lp") / 1000.0f) / 1.3f));
    if (mode == 4)
    {
        const int taps = (int) proc.value ("taps");
        const float decay = proc.value ("tapdecay") * 0.01f;
        for (int rep = 0; rep < 6; ++rep)
        {
            float g0 = std::pow (fb * tone, (float) rep);
            for (int k = 1; k <= taps; ++k)
            {
                const double t = tl * rep + tl * k / taps;
                echo (t, g0 * std::pow (decay, (float) (k - 1)), (k % 2) ? laneL : laneR, Pro::value);
            }
        }
    }
    else if (mode == 1)
    {
        for (int k = 0; k < 24; ++k)
            echo (tl * (k + 1), std::pow (fb * tone, (float) k), (k % 2) == 0 ? laneL : laneR, (k % 2) == 0 ? Pro::value : Pro::input);
    }
    else
    {
        const float cross = proc.value ("cross") * 0.01f;
        for (int k = 0; k < 24; ++k)
        {
            echo (tl * (k + 1), std::pow (fb * tone, (float) k) * (k == 0 ? 1.0f : 1.0f - cross * 0.5f), laneL, Pro::value);
            echo (tr * (k + 1), std::pow (fb * tone, (float) k) * (k == 0 ? 1.0f : 1.0f - cross * 0.5f), laneR, Pro::input);
        }
    }
    // the times of each side, and states
    auto fmt = [] (double s) { return s < 1.0 ? String (roundToInt (s * 1000.0)) + " ms" : String (s, 2) + " s"; };
    const bool sync = proc.value ("sync") > 0.5f;
    const auto& names = RacoonProcessor::divisionNames();
    auto label = [&] (Rectangle<float> lane, const String& div, double secs, Colour col)
    {
        auto l = Rectangle<float> (lane.getX() + 8.0f, lane.getY() - 18.0f, 240.0f, 14.0f);
        if (sync) ProDraw::caps (g, div, l.removeFromLeft (52.0f), Justification::centredLeft, col, 10.5f);
        g.setColour (Pro::text);
        g.setFont (NumFont::medium (11.0f));
        g.drawText (::racoon::i18n::tr (fmt (secs)), l, Justification::centredLeft);
    };
    label (laneL, names[(int) proc.value ("divl")], tl, Pro::value);
    label (laneR, names[(int) proc.value (proc.value ("link") > 0.5f ? "divl" : "divr")], tr, Pro::input);
    auto states = r.withHeight (14.0f).translated (0.0f, 2.0f);
    if (proc.value ("freeze") > 0.5f) ProDraw::caps (g, "Congelado", states.removeFromRight (90.0f), Justification::centredRight, Pro::input, 10.5f);
    if (proc.value ("reverse") > 0.5f) ProDraw::caps (g, "Reverse", states.removeFromRight (80.0f), Justification::centredRight, Pro::reduce, 10.5f);
    ProDraw::caps (g, String (roundToInt (fb * 100.0f)) + " % repeticiones", states.removeFromRight (160.0f), Justification::centredRight, Pro::textDim, 10.5f);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 960;
    s.easyHeight = 590;
    s.advancedHeight = 190;
    s.sections = {
        { "Delay", {
            { "mode", "Modo", "Estéreo repite cada lado por separado; Ping-pong rebota de un lado a otro; Doble usa dos tiempos; Cinta se ensucia y dobla el tono al cambiar el tiempo; Multitap reparte varios ecos."_u },
            { "sync", "A tempo", "Los tiempos siguen el tempo del proyecto (corcheas, negras...)."_u },
            { "divl", "Izquierda", "Tiempo del lado izquierdo cuando va a tempo. El punto es puntillo, la T es tresillo."_u },
            { "divr", "Derecha", "Tiempo del lado derecho cuando va a tempo."_u },
            { "feedback", "Repeticiones", "Cuántas veces se repite. Arriba de 90 % casi no se apaga."_u },
            { "mix", "Mezcla", "Cuánto eco se escucha. En una pista de retorno ponlo en 100 %."_u } } },
        { "Color", {
            { "hp", "Graves", "Quita graves a cada repetición para que no se embarre."_u },
            { "lp", "Agudos", "Quita brillo a cada repetición, como un eco analógico."_u } } },
        { "Tiempo libre", {
            { "timel", "Izquierda", "Tiempo del lado izquierdo cuando no va a tempo."_u },
            { "timer", "Derecha", "Tiempo del lado derecho cuando no va a tempo."_u },
            { "link", "Enlazar", "El lado derecho copia el tiempo del izquierdo."_u },
            { "cross", "Cruce", "Cuánto de cada lado se repite en el otro."_u },
            { "width", "Anchura", "Abre o cierra el estéreo de los ecos."_u } }, true },
        { "Movimiento", {
            { "modrate", "Velocidad", "Velocidad de la modulación del tiempo."_u },
            { "moddepth", "Profundidad", "Cuánto se mueve el tiempo: de chorus sutil a desafinado."_u },
            { "jitter", "Jitter", "Pequeños cambios al azar en el tiempo, para una textura menos perfecta."_u },
            { "drive", "Saturación"_u, "Ensucia un poco cada repetición."_u },
            { "diffusion", "Difusión"_u, "Emborrona las repeticiones hasta parecer una reverb corta."_u } }, true },
        { "Creativo", {
            { "duck", "Ducking", "Baja el eco mientras tocas y lo deja salir en los huecos. Ideal en voces."_u },
            { "reverse", "Reverse", "Las repeticiones suenan al revés."_u },
            { "freeze", "Congelar", "Atrapa lo que está sonando y lo repite sin fin, sin dejar entrar nada nuevo."_u },
            { "taps", "Taps", "En Multitap, cuántos ecos reparte dentro del tiempo."_u },
            { "tapdecay", "Caída"_u, "En Multitap, cuánto baja cada eco respecto al anterior."_u },
            { "output", "Salida", "Volumen final."_u } }, true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

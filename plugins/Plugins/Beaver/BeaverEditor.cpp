#include "BeaverEditor.h"
#include "I18n.h"

namespace racoon::beaver
{
using namespace juce;
using namespace racoon::ui;

static String noteName (float hz)
{
    static const char* names[12] { "Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    const int n = roundToInt (69.0f + 12.0f * std::log2 (jmax (1.0f, hz) / 440.0f));
    return String (names[(n % 12 + 12) % 12]) + " " + String (n / 12 - 1);
}

Display::Display (Processor& p) : proc (p), keys (p.keyboard, MidiKeyboardComponent::horizontalKeyboard)
{
    keys.setAvailableRange (12, 96);
    keys.setWantsKeyboardFocus (false);
    addChildComponent (keys);
    loadAudio.setButtonText (::racoon::i18n::tr ("CARGAR AUDIO"));
    loadImage.setButtonText (::racoon::i18n::tr ("CARGAR IMAGEN"));
    layerA.setButtonText ("A"); layerB.setButtonText ("B");
    for (auto* b : { &layerA, &layerB }) { b->setClickingTogglesState (true); b->setRadioGroupId (771); addAndMakeVisible (b); }
    layerA.setToggleState (true, dontSendNotification);
    layerA.onClick = [this] { target = 0; };
    layerB.onClick = [this] { target = 1; };
    loadAudio.onClick = [this] { choose (false); };
    loadImage.onClick = [this] { choose (true); };
    addAndMakeVisible (loadAudio);
    addAndMakeVisible (loadImage);
    startTimerHz (25);
}

void Display::resized()
{
    auto r = getLocalBounds();
    keys.setBounds ({});
    r.removeFromBottom (6);
    screen = r.toFloat();
    auto a = screen.withTrimmedRight (4).withTrimmedBottom (4).reduced (14.0f, 12.0f);
    specArea = a.removeFromLeft (a.getWidth() * 0.66f).reduced (4, 0);
    matArea = a.reduced (8, 0);
    auto m = matArea.reduced (8, 22);
    auto buttons = m.removeFromBottom (26.0f);
    thumbArea = m.withTrimmedBottom (26.0f);
    auto b = buttons.toNearestInt();
    const int w = b.getWidth();
    loadAudio.setBounds (b.removeFromLeft ((int) (w * 0.36f)).reduced (2, 0));
    loadImage.setBounds (b.removeFromLeft ((int) (w * 0.40f)).reduced (2, 0));
    layerA.setBounds (b.removeFromLeft (b.getWidth() / 2).reduced (2, 0));
    layerB.setBounds (b.reduced (2, 0));
}

void Display::timerCallback()
{
    // the material's picture is made here, once per new material
    const int show = proc.value ("b_fuente") >= 2.0f && proc.value ("a_fuente") < 2.0f ? 1 : 0;
    if (proc.materialVersion.load() != thumbVersion || show != thumbLayer)
    {
        thumbVersion = proc.materialVersion.load(); thumbLayer = show;
        thumb = {};
        if (auto fs = proc.activeSet (show))
        {
            const int w = jmin (256, fs->frames), h = 96;
            thumb = Image (Image::RGB, jmax (1, w), h, true);
            for (int x = 0; x < w; ++x)
            {
                const int fr = fs->frames <= 1 ? 0 : x * (fs->frames - 1) / jmax (1, w - 1);
                for (int y = 0; y < h; ++y)
                {
                    const int k = jlimit (0, numPartials - 1, (int) std::pow (2.0, (double) (h - 1 - y) / (h - 1) * 7.0) - 1);
                    const float v = jlimit (0.0f, 1.0f, (fs->levelDb (fr, k) + 72.0f) / 72.0f);
                    thumb.setPixelAt (x, y, C::void_.interpolatedWith (C::amber, v * v));
                }
            }
        }
    }
    repaint (screen.toNearestInt());
}

int Display::drawLayer() const
{
    if ((int) proc.value ("a_fuente") == srcDibujo) return 0;
    if ((int) proc.value ("b_fuente") == srcDibujo) return 1;
    return -1;
}

void Display::drawAt (Point<float> p, bool reset)
{
    const int L = drawLayer();
    if (L < 0 || ! specArea.contains (p)) return;
    auto plot = specArea.reduced (10, 24);
    const int b = jlimit (0, numBars - 1, (int) ((p.x - plot.getX()) / plot.getWidth() * numBars));
    const float db = reset ? -60.0f : jlimit (-60.0f, 0.0f, jmap (p.y, plot.getY(), plot.getBottom(), 0.0f, -60.0f));
    proc.bars[L][b].store (db);
}

void Display::mouseDown (const MouseEvent& e) { drawAt (e.position, false); }
void Display::mouseDrag (const MouseEvent& e) { drawAt (e.position, false); }
void Display::mouseDoubleClick (const MouseEvent& e) { drawAt (e.position, true); }

bool Display::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;png;jpg;jpeg;gif")) return true;
    return false;
}

void Display::filesDropped (const StringArray& files, int x, int)
{
    if (files.isEmpty()) return;
    // dropped on the right half of the spectrum or the panel: the chosen layer
    ignoreUnused (x);
    proc.loadOwn (target, File (files[0]), false);
}

void Display::choose (bool picture)
{
    chooser = std::make_unique<FileChooser> (::racoon::i18n::tr (picture ? "Elige una imagen" : "Elige un audio"), File(),
                                             picture ? "*.png;*.jpg;*.jpeg;*.gif" : "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile()) proc.loadOwn (target, f, false);
    });
}

void Display::paint (Graphics& g)
{
    drawScreen (g, screen);
    g.setFont (Fonts::label (11.5f));

    // ---- spectrum
    {
        g.setColour (C::ridge);
        g.drawRect (specArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("ESPECTRO"), specArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        auto plot = specArea.reduced (10, 24);
        const int L = drawLayer();
        if (L >= 0)
        {
            // the 64 bars of the drawn layer, editable here
            const float w = plot.getWidth() / (float) numBars;
            for (int b = 0; b < numBars; ++b)
            {
                const float db = proc.bars[L][b].load();
                const float y = jmap (db, 0.0f, -60.0f, plot.getY(), plot.getBottom());
                g.setColour ((L == 0 ? C::amber : C::cyan).withAlpha (0.35f));
                g.fillRect (Rectangle<float> (plot.getX() + w * (float) b + 1.0f, y, w - 2.0f, plot.getBottom() - y));
            }
        }
        const float maxX = std::log2 ((float) numPartials);
        for (int i = 0; i < numPartials; ++i)
        {
            const float a = proc.levelView[i].load (std::memory_order_relaxed);
            if (a <= 1.0e-5f) continue;
            const float db = jlimit (-72.0f, 0.0f, linToDb (a));
            const float x = L >= 0 ? plot.getX() + plot.getWidth() * ((float) i + 0.5f) / (float) numBars
                                   : plot.getX() + plot.getWidth() * std::log2 ((float) (i + 1)) / maxX;
            if (x > plot.getRight()) break;
            const float y = jmap (db, 0.0f, -72.0f, plot.getY(), plot.getBottom());
            g.setColour (C::amber.withAlpha (0.55f + 0.45f * (db + 72.0f) / 72.0f));
            g.drawLine (x, plot.getBottom(), x, y, i < 16 ? 2.0f : 1.2f);
        }
        // filter 1 over the partials (at the last note's pitch)
        {
            FilterCurve f1;
            f1.type = (int) proc.value ("f1_tipo"); f1.fc = proc.value ("f1_corte"); f1.slope = proc.value ("f1_pend");
            f1.res = proc.value ("f1_res") * 0.01f; f1.width = proc.value ("f1_ancho"); f1.vowel = proc.value ("f1_vocal");
            Path pth;
            for (int x = 0; x <= 160; ++x)
            {
                const float f = 20.0f * std::pow (1000.0f, (float) x / 160.0f);
                const float y = jmap (jlimit (-72.0f, 12.0f, f1.db (f) - 6.0f), 12.0f, -72.0f, plot.getY(), plot.getBottom());
                const float xx = plot.getX() + plot.getWidth() * (float) x / 160.0f;
                if (x == 0) pth.startNewSubPath (xx, y); else pth.lineTo (xx, y);
            }
            g.setColour (C::bone.withAlpha (0.7f));
            g.strokePath (pth, PathStrokeType (1.2f));
        }
    }

    // ---- material
    {
        g.setColour (C::ridge);
        g.drawRect (matArea, 1.0f);
        g.setColour (C::boneDim);
        g.drawText (::racoon::i18n::tr ("MATERIAL"), matArea.withHeight (14).translated (6, 2), Justification::centredLeft);
        if (thumb.isValid()) g.drawImage (thumb, thumbArea, RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (C::boneDim);
            g.drawFittedText (::racoon::i18n::tr ("Arrastra aquí un audio o una imagen, o elige un material de Beaver."), thumbArea.toNearestInt().reduced (8), Justification::centred, 4);
        }
        String line;
        if (proc.analysing.load() > 0) line = ::racoon::i18n::tr ("Analizando...");
        else if (proc.lastError.isNotEmpty()) line = ::racoon::i18n::tr (proc.lastError);
        else if (auto fs = proc.activeSet (thumbLayer < 0 ? 0 : thumbLayer))
            line = fs->name + (fs->noise.empty() ? String() : "  ·  " + ::racoon::i18n::tr ("Analizado en " + noteName (fs->refHz)));
        g.setColour (C::bone);
        g.drawText (line, Rectangle<float> (thumbArea.getX(), thumbArea.getBottom() + 4.0f, thumbArea.getWidth(), 16.0f), Justification::centredLeft);
    }
    g.setColour (C::amber);
    g.drawText (::racoon::i18n::tr (String (proc.activeVoices.load()) + " / " + String ((int) proc.value ("voices")) + " VOCES  ·  " + String (proc.partialsNow.load()) + " ARMÓNICOS"_u),
                screen.reduced (16, 4).withHeight (14), Justification::topRight);
}

static EditorSpec makeSpec()
{
    EditorSpec s;
    s.width = 1080;
    s.easyHeight = 680;
    s.advancedHeight = 240;
    s.displayEasy = 330;
    s.displayFull = 260;
    auto join = [] (std::vector<Ctl> a, const std::vector<Ctl>& b) { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto layerCard = [] (int L)
    {
        return std::vector<Ctl> {
            { Processor::layerId (L, "fuente"), "Fuente", "De dónde salen los armónicos de esta capa."_u },
            { Processor::layerId (L, "forma"), "Forma", "La forma de fábrica, cuando la fuente es Forma."_u },
            { Processor::layerId (L, "material"), "Material", "Un material de Beaver para Resíntesis o Imagen. Propio es el archivo o la imagen que cargaste."_u },
            { Processor::layerId (L, "pos"), "Posición"_u, "Recorre la forma, o el punto del material donde empieza la nota."_u } };
    };
    auto layerMore = [] (int L)
    {
        return std::vector<Ctl> {
            { Processor::layerId (L, "nivel"), "Nivel", "Volumen de esta capa."_u },
            { Processor::layerId (L, "tono"), "Afinación"_u, "Sube o baja una capa dentro de la misma nota."_u },
            { Processor::layerId (L, "lectura"), "Lectura", "Qué tan rápido avanza el material. 0 lo congela; negativo lo lee al revés."_u },
            { Processor::layerId (L, "bucle"), "Bucle", "Al llegar al final vuelve a la posición."_u },
            { Processor::layerId (L, "conservar"), "Formantes", "La voz suena a la misma voz en todo el teclado."_u },
            { Processor::layerId (L, "formante"), "Mover", "Mueve las resonancias: negativo, más grande y oscuro."_u },
            { Processor::layerId (L, "seguirf0"), "Seguir", "Trae el vibrato y los deslices del audio original."_u },
            { Processor::layerId (L, "ruido"), "Ruido", "El aire y el soplo que el análisis separó de los armónicos."_u } };
    };
    auto filter = [] (int f)
    {
        const String id = "f" + String (f) + "_";
        return std::vector<Ctl> {
            { id + "tipo", "Tipo", "La forma del filtro sobre los armónicos."_u },
            { id + "corte", "Corte", "Dónde corta."_u },
            { id + "res", "Resonancia", "Resalta los armónicos cerca del corte."_u },
            { id + "pend", "Pendiente", "Qué tan de golpe corta."_u },
            { id + "ancho", "Ancho", "Para Rechazo."_u },
            { id + "vocal", "Vocal", "Para Vocal: u, o, a, e, i."_u },
            { id + "teclado", "Teclado", "Las notas agudas abren más el filtro."_u },
            { id + "env", "Envolvente", "Cuánto lo abre la envolvente 2."_u } };
    };
    auto slots = [] (int from, int count)
    {
        std::vector<Ctl> c;
        for (int k = from; k < from + count; ++k)
        {
            c.push_back ({ Processor::slotId (k, "src"), "Fuente " + String (k + 1), "Qué se mueve: un LFO, una envolvente, la velocidad, el aftertouch, la rueda, la tecla o un valor al azar por nota."_u });
            c.push_back ({ Processor::slotId (k, "dst"), "Destino " + String (k + 1), "Qué parámetro mueve."_u });
            c.push_back ({ Processor::slotId (k, "amt"), "Cantidad " + String (k + 1), "Cuánto y en qué sentido. Negativo invierte."_u });
        }
        return c;
    };
    auto env = [] (const char* id, const char* label)
    {
        const String i (id);
        return std::vector<Ctl> {
            { i + "_a", String (label) + " A", "Ataque."_u }, { i + "_d", String (label) + " D", "Caída."_u },
            { i + "_s", String (label) + " S", "Sostenido."_u }, { i + "_r", String (label) + " R", "Relajación."_u } };
    };
    auto lfo = [] (int k)
    {
        return std::vector<Ctl> {
            { Processor::lfoId (k, "rate"), "LFO " + String (k + 1), "Velocidad, de 0.01 a 20 Hz."_u },
            { Processor::lfoId (k, "sync"), "Tempo", "Sigue el tempo del proyecto."_u },
            { Processor::lfoId (k, "div"), "División"_u, "Duración de un ciclo cuando va a tempo."_u },
            { Processor::lfoId (k, "shape"), "Forma", "Seno, triángulo, sierra, cuadrada, S&H o azar suave."_u } };
    };
    s.steps = { "Dale Play: suena una sierra tibia hecha de 255 armónicos."_u, "Gira Mezcla A ↔ B: pasa poco a poco a la vocal oscura de la capa B."_u,
                "Arrastra un audio o una imagen a la pantalla: Beaver lo convierte en armónicos y lo toca con el teclado."_u,
                "Prueba Pulsado y Difuminar en el tiempo para que el mismo sonido pulse o se vuelva nube."_u };
    s.sections = {
        { "Capa A", layerCard (0) },
        { "Capa B", layerCard (1) },
        { "Mezcla", {
            { "morph", "A ↔ B"_u, "De la capa A a la capa B."_u },
            { "inclinacion", "Inclinación"_u, "Negativo oscurece todo de un jalón."_u } } },
        { "Filtro", {
            { "f1_tipo", "Tipo", "La forma del filtro sobre los armónicos."_u },
            { "f1_corte", "Corte", "Dónde corta."_u },
            { "f1_res", "Resonancia", "Resalta los armónicos cerca del corte."_u } } },
        { "Movimiento", {
            { "pulsar", "Pulsado", "Los agudos se apagan antes que el grave, como una cuerda."_u },
            { "blurt", "Difuminar", "Cada armónico llega y se va despacio."_u },
            { "unison", "Unison", "Copias de la nota, desafinadas entre sí."_u },
            { "detune", "Detune", "Cuánto se desafinan las copias."_u } } },
        adsrSection(),
        { "Salida", {
            { "espacio", "Espacio", "Sala alrededor del sonido."_u },
            { "volume", "Volumen", "Volumen final."_u } } },
        { "A +", layerMore (0), true },
        { "B +", layerMore (1), true },
        { "Análisis"_u, {
            { "analisis", "Análisis"_u, "Con su nota sigue la afinación del audio. Como espectro lee cualquier sonido (acordes, ritmos) como un color."_u },
            { "notaref", "Nota", "Para Como espectro: la nota en la que se lee el audio."_u },
            { "imgeje", "Eje", "Logarítmico da más espacio a los graves de la imagen."_u },
            { "imgrango", "Contraste", "Cuánta diferencia hay entre lo negro y lo blanco."_u },
            { "imgdur", "Duración"_u, "Cuánto dura leer la imagen de izquierda a derecha."_u } }, true },
        { "Armónicos"_u, {
            { "armonicos", "Armónicos"_u, "Cuántos armónicos como máximo. Menos, más oscuro y más barato."_u },
            { "pares", "Pares", "Negativo quita los pares (hueco); positivo quita los impares."_u },
            { "armonizador", "Armonizador", "Deja sonar solo algunos armónicos."_u },
            { "armcant", "Cantidad", "Cuánto actúa el armonizador."_u },
            { "fase", "Fase", "Suave reparte la energía; Pico golpea más."_u },
            { "estereo", "Estéreo"_u, "Impares a la izquierda, pares a la derecha."_u },
            { "morphmodo", "Mezcla", "Espectral pasa de un timbre a otro sin oírse como dos capas."_u } }, true },
        { "Filtro 1 +", filter (1), true },
        { "Filtro 2", join ({ { "f2_on", "Encendido", "Prende el segundo filtro."_u }, { "filtros", "Ruta", "En serie, en paralelo o solo el 1."_u } }, filter (2)), true },
        { "Barrido y difuminar", {
            { "barrido", "Barrido", "Huecos que suben por los armónicos sin parar."_u },
            { "barvel", "Velocidad", "Negativo baja."_u },
            { "barsync", "Tempo", "El barrido sigue el tempo."_u },
            { "bardiv", "División"_u, "Duración de una vuelta cuando va a tempo."_u },
            { "barsep", "Separación"_u, "Distancia entre huecos, en octavas."_u },
            { "blurh", "Armónicos"_u, "Mezcla cada armónico con sus vecinos: una nube sin vocal."_u },
            { "blurcola", "Cola", "Solo lo que cae se difumina: una cola hecha de la misma nota."_u },
            { "pulsarcaida", "Pulsado", "Cuánto dura el grave cuando el pulsado está puesto."_u } }, true },
        { "Voces", {
            { "voices", "Voces", "Notas a la vez. Con 1 es un bajo."_u },
            { "glide", "Glide", "Deslizamiento entre notas con una voz."_u },
            { "bendrange", "Bend", "Semitonos de la rueda de pitch bend."_u },
            { "velamp", "Vel. volumen", "Tocar fuerte suena más fuerte."_u },
            { "velcorte", "Vel. filtro", "Tocar fuerte abre el filtro 1."_u },
            { "apertura", "Apertura", "Qué tan abiertas quedan las copias de unison."_u },
            { "calidad", "Calidad", "ECO gasta la mitad (hasta 127 armónicos); HQ calcula el doble de cuadros por segundo."_u },
            { "espaciotam", "Sala", "Tamaño del espacio."_u } }, true },
        { "Env 2 · 3"_u, join (env ("env2", "Env 2"), env ("env3", "Env 3")), true },
        { "LFO 1 · 2"_u, join (lfo (0), lfo (1)), true },
        { "Mod 1-4", slots (0, 4), true },
        { "Mod 5-8", slots (4, 4), true },
    };
    return s;
}

Editor::Editor (Processor& p) : RacoonEditor (p, makeSpec(), std::make_unique<Display> (p)) {}
}

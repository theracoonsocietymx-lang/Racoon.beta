// Window-level checks: builds the real main component off-screen, drives it with synthetic mouse events
// and saves screenshots of every view.
#include "engine/Stems.h"
#include "engine/I18n.h"
#include "engine/Muestras.h"
#include "engine/SamplePreview.h"
#include "ui/SongMaker.h"
#include "ui/NoteTools.h"
#include <set>
#include "engine/AudioTools.h"
#include "engine/Genres.h"
#include "engine/Session.h"
#include "engine/DJ.h"
#include "ui/MainComponent.h"
#include "ui/ChannelView.h"
#include "ui/RackEditor.h"
#include "engine/Devices.h"
#include "engine/MidiCapture.h"

using namespace racoon;
using namespace juce;

namespace
{
int uiFailures = 0;
void check (bool ok, const String& what)
{
    std::cout << (ok ? "  ok    " : "  FALLO ") << what << std::endl;
    if (! ok) ++uiFailures;
}

void pump (int ms = 120) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// in English, every button and one-line label on screen must fit its text: the English is measured against the space
// (with the same squeeze the look allows), and only what overflows in English and not in Spanish is reported
StringArray overflows;
int fitChecked = 0;
void checkFits (Component& root)
{
    if (i18n::current() != i18n::Lang::en) return;
    auto& lf = root.getLookAndFeel();
    auto width = [] (const Font& f, const String& t) { return GlyphArrangement::getStringWidth (f, t); };
    std::function<void (Component&)> walk = [&] (Component& c)
    {
        if (! c.isVisible() || c.getWidth() <= 0) return;
        if (auto* b = dynamic_cast<TextButton*> (&c))
        {
            const auto es = b->getButtonText();
            if (es.isNotEmpty() && ! es.containsChar ('\n'))
            {
                ++fitChecked;
                const auto f = lf.getTextButtonFont (*b, b->getHeight());
                const float room = (float) b->getWidth() - 6.0f;
                const float wEn = width (f, i18n::tr (es)) * 0.85f, wEs = width (f, es) * 0.85f;
                if (wEn > room + 1.0f && wEs <= room + 1.0f)
                    overflows.addIfNotAlreadyThere ("boton \"" + i18n::tr (es) + "\" (" + String (roundToInt (wEn)) + " de " + String (roundToInt (room)) + " px)");
            }
        }
        else if (auto* l = dynamic_cast<Label*> (&c))
        {
            const auto es = l->getText();
            const auto f = lf.getLabelFont (*l);
            if (es.isNotEmpty() && ! es.containsChar ('\n') && l->getHeight() < f.getHeight() * 2.0f && ! l->isBeingEdited())
            {
                ++fitChecked;
                const auto area = l->getBorderSize().subtractedFrom (l->getLocalBounds());
                const float room = (float) area.getWidth();
                const float sq = l->getMinimumHorizontalScale() > 0.0f ? l->getMinimumHorizontalScale() : 0.7f;
                const float wEn = width (f, i18n::tr (es)) * sq, wEs = width (f, es) * sq;
                if (wEn > room + 1.0f && wEs <= room + 1.0f)
                    overflows.addIfNotAlreadyThere ("etiqueta \"" + i18n::tr (es) + "\" (" + String (roundToInt (wEn)) + " de " + String (roundToInt (room)) + " px)");
            }
        }
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    walk (root);
}

void shot (Component& c, const File& f)
{
    checkFits (c);
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}

MouseEvent makeEvent (Component& c, Point<int> p, ModifierKeys mods, int clicks = 1, Point<int> down = {}, bool dragged = false)
{
    auto src = Desktop::getInstance().getMainMouseSource();
    return MouseEvent (src, p.toFloat(), mods, MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                       &c, &c, Time::getCurrentTime(), (dragged ? down : p).toFloat(), Time::getCurrentTime(), clicks, dragged);
}

void click (Component& c, Point<int> p, ModifierKeys mods = ModifierKeys::leftButtonModifier)
{
    c.mouseDown (makeEvent (c, p, mods));
    c.mouseUp (makeEvent (c, p, mods));
}

void dblclick (Component& c, Point<int> p, ModifierKeys mods = ModifierKeys::leftButtonModifier)
{
    click (c, p, mods);
    c.mouseDown (makeEvent (c, p, mods, 2));
    c.mouseDoubleClick (makeEvent (c, p, mods, 2));
    c.mouseUp (makeEvent (c, p, mods, 2));
}

void drag (Component& c, Point<int> from, Point<int> to, ModifierKeys mods = ModifierKeys::leftButtonModifier)
{
    c.mouseDown (makeEvent (c, from, mods));
    for (int i = 1; i <= 6; ++i)
    {
        auto p = from + (to - from) * i / 6;
        c.mouseDrag (makeEvent (c, p, mods, 1, from, true));
    }
    c.mouseUp (makeEvent (c, to, mods, 1, from, true));
}

template <typename T>
T* findChild (Component& root)
{
    if (auto* t = dynamic_cast<T*> (&root)) return t;
    for (auto* c : root.getChildren())
        if (auto* f = findChild<T> (*c)) return f;
    return nullptr;
}

template <typename T>
Array<T*> findChildren (Component& root)
{
    Array<T*> out;
    if (auto* t = dynamic_cast<T*> (&root)) out.add (t);
    for (auto* c : root.getChildren()) out.addArray (findChildren<T> (*c));
    return out;
}
}

int runUiTests (Session& s, const File& out)
{
    std::cout << "interfaz" << std::endl;
    ui::AncientLook look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    ui::prefs().setValue ("welcomeDone", false);
    ui::prefs().setValue ("easyMode", true);

    auto shots = out.getChildFile ("pantallas");
    shots.createDirectory();
    const bool english = i18n::current() == i18n::Lang::en;
    if (english) i18n::recordSpanish (true);
    {
        // what Racoon asks first: the genre; picking one builds its block
        s.newProject (false);
        ui::MainComponent mc (s);
        mc.setSize (1500, 940);
        // the very first time: the language, asked in both
        const bool wasEnglish = i18n::current() == i18n::Lang::en;
        const auto envLang = SystemStats::getEnvironmentVariable ("RACOON_LANG", {});
        if (envLang.isEmpty())
        {
            i18n::settingsFile().deleteFile();
            mc.showGenrePicker (true);
            pump (300);
            auto* lp = mc.getLanguagePicker();
            check (lp != nullptr && lp->isVisible() && mc.getGenrePicker() == nullptr && lp->english.isVisible() && lp->spanish.isVisible(),
                   String::fromUTF8 ("la primera vez pregunta el idioma (en español y en inglés) antes que el género"));
            shot (mc, shots.getChildFile ("00_idioma.png"));
            if (lp != nullptr) (wasEnglish ? lp->english : lp->spanish).triggerClick();
            for (int i = 0; i < 20 && mc.getLanguagePicker() != nullptr; ++i) pump (100);
            check (mc.getLanguagePicker() == nullptr && i18n::chosen() && i18n::settingsFile().loadFileAsString().trim() == (wasEnglish ? "en" : "es"),
                   String::fromUTF8 ("elegir el idioma lo guarda en idioma.txt (Racoon y los plugins lo leen) y sigue con el género"));
            // English and back: the language switches right away and the dictionary answers
            mc.setLanguage (true);
            const bool en = i18n::tr (String::fromUTF8 ("Proyecto vacío")) == "Empty project" && i18n::tr (String::fromUTF8 ("Rola de 216 compases")) != String::fromUTF8 ("Rola de 216 compases");
            mc.setLanguage (wasEnglish);
            check (en && i18n::tr (String::fromUTF8 ("Proyecto vacío")) == (wasEnglish ? "Empty project" : String::fromUTF8 ("Proyecto vacío")),
                   String::fromUTF8 ("cambiar de idioma cambia los textos al momento (") + String (i18n::dictionarySize()) + String::fromUTF8 (" frases en el diccionario)"));
        }
        else mc.showGenrePicker (true);
        pump (300);
        auto* picker = mc.getGenrePicker();
        check (picker != nullptr && picker->isVisible() && picker->buttonFor ("trap") != nullptr && picker->buttonFor ("trap")->isVisible() && ! picker->buttonFor ("trap")->getBounds().isEmpty(),
               String::fromUTF8 ("al abrir pregunta el género: ") + String ((int) genres().size()) + String::fromUTF8 (" géneros a la vista"));
        shot (mc, shots.getChildFile ("00a_que_vamos_a_producir.png"));
        if (picker != nullptr) picker->buttonFor ("trap")->triggerClick();
        for (int i = 0; i < 40 && mc.getGenrePicker() != nullptr; ++i) pump (150);
        check (mc.getGenrePicker() == nullptr && std::abs (s.getBpm() - 140.0) < 0.01 && te::getAudioTracks (s.getEdit()).size() == 13,
               "elegir Trap arma el bloque a 140 BPM con sus 11 canales (el kick aparte del groove)");
        pump (400);
        shot (mc, shots.getChildFile ("00a_bloque_trap.png"));
    }
    {
        // the keys of the arrangement on a real window: a click on a clip with Piano open keeps Supr and Cmd+D for the
        // clip (they used to go to the Piano, where with no notes selected they did nothing or grew the clip)
        s.newProject (true);
        ui::MainComponent mc (s);
        mc.setSize (1500, 940);
        mc.addToDesktop (0);
        mc.setVisible (true);
        pump (500);
        auto key = [&mc] (KeyPress k) { if (auto* peer = mc.getPeer()) peer->handleKeyPress (k); pump (120); };
        auto lanes = findChildren<ui::TrackLane> (mc.getArrangement());
        mc.showBottom (ui::MainComponent::Bottom::editor);
        pump (60);
        auto* bassLane = lanes.size() > 1 ? lanes[1] : nullptr;
        auto* bass = bassLane != nullptr ? dynamic_cast<te::AudioTrack*> (bassLane->track.get()) : nullptr;
        auto cc = bassLane != nullptr ? findChildren<ui::ClipComponent> (*bassLane) : Array<ui::ClipComponent*>();
        if (bass != nullptr && ! cc.isEmpty())
        {
            const int before = bass->getClips().size();
            click (*cc[0], { 30, cc[0]->getHeight() / 2 });
            pump (120);
            auto* f = Component::getCurrentlyFocusedComponent();
            const bool pianoHasKeys = f != nullptr && (f == &mc.getPianoRoll() || mc.getPianoRoll().isParentOf (f));
            check (mc.getPianoRoll().getClip() == cc[0]->clip.get() && ! pianoHasKeys, "clic en un clip con Piano abierto: Piano lo muestra y las teclas siguen en la maqueta"_u);
            key (KeyPress (KeyPress::deleteKey));
            check (bass->getClips().size() == before - 1, "Supr justo después borra el clip (" + String (before) + " -> " + String (bass->getClips().size()) + ")");
            s.getEdit().undo();
            pump (200);

            // four beats picked on the body of a clip, then Cmd+D: the copy lands on the next four, nothing more
            auto& v = mc.getViewState();
            cc = findChildren<ui::ClipComponent> (*bassLane);
            ui::ClipComponent* c0 = nullptr;
            for (auto* x : cc) if (c0 == nullptr || x->clip->getPosition().getStart() < c0->clip->getPosition().getStart()) c0 = x;
            if (c0 != nullptr)
            {
                auto& ts = s.getEdit().tempoSequence;
                const double cStart = ts.toBeats (c0->clip->getPosition().getStart()).inBeats();
                const int xa = (int) std::round ((cStart + 8.0 - v.scrollBeats) * v.pxPerBeat) - c0->getX();   // where the bass starts playing
                drag (*c0, { xa, 30 }, { xa + (int) std::round (4.0 * v.pxPerBeat), 30 });
                pump (120);
                const double rs = v.rangeStart, re = v.rangeEnd;
                // the notes that sound in a span of the bass channel
                auto notesIn = [&] (double from, double to)
                {
                    int n = 0;
                    for (auto* c : bass->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*> (c))
                            for (auto* note : m->getSequence().getNotes())
                            {
                                const auto t = m->getTimeOfContentBeat (note->getStartBeat());
                                const double b = ts.toBeats (t).inBeats();
                                if (t >= c->getPosition().getStart() && t < c->getPosition().getEnd() && b >= from - 1e-6 && b < to - 1e-6) ++n;
                            }
                    return n;
                };
                const int inRange = notesIn (rs, re);
                key (KeyPress ('d', ModifierKeys::commandModifier, 0));
                const int inCopy = notesIn (re, re + (re - rs));
                int at = 0; bool overlap = false;
                std::vector<std::pair<double, double>> spans;
                for (auto* c : bass->getClips())
                    spans.push_back ({ ts.toBeats (c->getPosition().getStart()).inBeats(), ts.toBeats (c->getPosition().getEnd()).inBeats() });
                std::sort (spans.begin(), spans.end());
                for (size_t i = 0; i < spans.size(); ++i)
                {
                    if (std::abs (spans[i].first - re) < 0.01 && std::abs (spans[i].second - (re + (re - rs))) < 0.01) ++at;
                    if (i > 0 && spans[i].first < spans[i - 1].second - 0.01) overlap = true;
                }
                check (inRange > 0 && inCopy == inRange, "la copia suena con las mismas " + String (inRange) + " notas del pedazo (" + String (inCopy) + ")");
                check (std::abs (re - rs - 4.0) < 0.01 && at == 1 && ! overlap && v.hasRange() && std::abs (v.rangeStart - re) < 0.01,
                       "4 tiempos y Cmd+D: la copia ocupa los 4 siguientes (" + String (re, 2) + " a " + String (re + re - rs, 2)
                       + "), sin nada encima ni debajo, y la selección pasa a la copia");
                shot (mc, shots.getChildFile ("00c_duplicar_4_tiempos.png"));
                s.getEdit().undo();
                pump (120);
            }
        }
        mc.removeFromDesktop();
    }
    {
        // what Racoon opens with: the block, ten channels with their clips
        s.newProject (true);
        ui::MainComponent mc (s);
        mc.setSize (1500, 940);
        pump (500);
        shot (mc, shots.getChildFile ("00_inicio_bloque.png"));
        auto lanes = findChildren<ui::TrackLane> (mc.getArrangement());
        check (lanes.size() == 11, "el bloque muestra 11 canales (Kick y Groove aparte): " + String (lanes.size()));
        // a click on the song with Piano open: Piano stays and shows that channel's notes, never the drums'
        if (lanes.size() > 2)
        {
            mc.showBottom (ui::MainComponent::Bottom::editor);
            pump (60);
            auto* lane = lanes[2];
            click (*lane, { lane->getWidth() - 20, lane->getHeight() / 2 });
            pump (120);
            auto* shown = mc.getPianoRoll().getClip();
            check (mc.getBottom() == ui::MainComponent::Bottom::editor && shown != nullptr && shown->getTrack() == lane->track.get(),
                   "clic en la maqueta con Piano abierto: Piano muestra las notas de " + lane->track->getName()
                   + " (muestra " + (shown != nullptr && shown->getTrack() != nullptr ? shown->getTrack()->getName() : String ("nada")) + ")");
            shot (mc, shots.getChildFile ("00b_clic_en_la_maqueta.png"));
            // a click on a clip of another channel, still in Piano
            auto clipsBass = findChildren<ui::ClipComponent> (*lanes[1]);
            if (! clipsBass.isEmpty())
            {
                click (*clipsBass[0], { 30, clipsBass[0]->getHeight() / 2 });
                pump (120);
                check (mc.getBottom() == ui::MainComponent::Bottom::editor && mc.getPianoRoll().getClip() == clipsBass[0]->clip.get(),
                       "clic en un clip de " + lanes[1]->track->getName() + ": Piano abre ese clip");
            }
            // from Canal: the click shows the channel, and the Piano tab then opens that same channel's notes
            mc.showBottom (ui::MainComponent::Bottom::channel);
            pump (60);
            click (*lane, { lane->getWidth() - 20, lane->getHeight() / 2 });
            pump (120);
            mc.clickPianoTab();
            pump (120);
            shown = mc.getPianoRoll().getClip();
            check (mc.getBottom() == ui::MainComponent::Bottom::editor && shown != nullptr && shown->getTrack() == lane->track.get(),
                   "de Canal a Piano: se abren las notas del mismo canal, " + lane->track->getName());
        }
        // a click on a clip with Canal open shows its channel
        {
            mc.showBottom (ui::MainComponent::Bottom::channel);
            auto clips = findChildren<ui::ClipComponent> (*lanes[0]);
            if (! clips.isEmpty())
            {
                click (*clips[0], { 30, clips[0]->getHeight() / 2 });
                pump (120);
                check (mc.getBottom() == ui::MainComponent::Bottom::channel && findChildren<ui::DeviceCard> (mc).size() >= 1,
                       "clic en el clip de la batería: abajo se ve Hamster");
                if (auto* dc = dynamic_cast<te::MidiClip*> (clips[0]->clip.get()))
                {
                    // in Piano, the drum clip shows the kit's own rows, named like in Hamster
                    mc.getPianoRoll().setClip (dc);
                    mc.showBottom (ui::MainComponent::Bottom::editor);
                    pump (120);
                    auto& pr = mc.getPianoRoll();
                    check (pr.drumRowName (36) == "Bombo" && pr.drumRowName (42) == "Charles cerrado" && pr.visibleRowCount() >= 16,
                           "en Notas la batería se ve por tambores, como en Hamster: " + String (pr.visibleRowCount()) + " filas, "
                           + pr.drumRowName (36) + ", " + pr.drumRowName (38) + ", " + pr.drumRowName (42));
                    shot (mc, shots.getChildFile ("00c_bateria_por_tambores.png"));
                    {
                        // ALEATORIA on the drums: a new pattern with the kit's own notes; Cmd+Z brings the old one back
                        const int before = dc->getSequence().getNumNotes();
                        check (pr.defaultRole() == aleatorio::Rol::bateria, "en la batería ALEATORIA genera batería"_u);
                        pr.generateRandom (std::nullopt, 4242);
                        pump (60);
                        std::set<int> kitNotes;
                        for (int p = 0; p < 128; ++p) if (pr.drumRowName (p).isNotEmpty()) kitNotes.insert (p);
                        const auto d = pr.drumMap();
                        bool onlyKit = dc->getSequence().getNumNotes() > 16;
                        for (auto* n : dc->getSequence().getNotes()) onlyKit = onlyKit && kitNotes.count (n->getNoteNumber()) > 0;
                        check (onlyKit && d.kick == 36, "ALEATORIA en Hamster: " + String (dc->getSequence().getNumNotes()) + " golpes, todos en las pistas del kit (bombo en "
                               + String (d.kick) + ", caja en " + String (d.snare) + ")");
                        shot (mc, shots.getChildFile ("00d_bateria_aleatoria.png"));
                        s.getEdit().getUndoManager().undo();
                        pump (60);
                        check (dc->getSequence().getNumNotes() == before, "Cmd+Z devuelve el patrón de antes (" + String (before) + " golpes)");
                    }
                    {
                        // the lane under the notes draws controllers: a rising mod wheel, then erased with a right drag, then a bend
                        pr.showLane (1);
                        auto* lane = pr.laneComponent();
                        pump (20);
                        auto countCc = [&] (int type, int& first, int& last)
                        {
                            int n = 0; first = last = -1;
                            for (auto* e : dc->getSequence().getControllerEvents())
                                if (e->getType() == type) { if (n == 0) first = e->getControllerValue(); last = e->getControllerValue(); ++n; }
                            return n;
                        };
                        const int h = lane->getHeight(), w = lane->getWidth();
                        drag (*lane, { w / 5, h - 8 }, { w / 2, 22 });
                        pump (20);
                        int first = 0, last = 0;
                        const int n1 = countCc (1, first, last);
                        check (n1 >= 4 && first < last && last >= (100 << 7), "carril de controladores: la rueda de modulación se dibuja subiendo ("
                               + String (n1) + " puntos, de " + String (first >> 7) + " a " + String (last >> 7) + ")");
                        shot (mc, shots.getChildFile ("16e_carril_cc.png"));
                        drag (*lane, { w / 5 - 4, h / 2 }, { w / 2 + 4, h / 2 }, ModifierKeys (ModifierKeys::rightButtonModifier | ModifierKeys::popupMenuClickModifier));
                        pump (20);
                        check (countCc (1, first, last) == 0, "clic derecho y arrastrar borra la curva");
                        pr.showLane (te::MidiControllerEvent::pitchWheelType);
                        drag (*lane, { w / 4, h / 2 }, { w / 3, h - 6 });
                        pump (20);
                        const int nb = countCc (te::MidiControllerEvent::pitchWheelType, first, last);
                        check (nb >= 2 && last < 2000 && first > 6000, "pitch bend: baja desde el centro (" + String (first) + " a " + String (last) + ")");
                        s.getEdit().getUndoManager().undo();
                        pump (20);
                        check (countCc (te::MidiControllerEvent::pitchWheelType, first, last) == 0, "Cmd+Z quita el bend dibujado");
                        pr.showLane (-1);
                    }
                }
            }
            else check (false, "clip de la batería");
        }
        // Mole's percussion is on screen, not only in the plugin
        for (auto* l : lanes)
            if (l->track->getName().startsWith ("Mole"))
                for (auto* t = dynamic_cast<te::AudioTrack*> (l->track.get()); t != nullptr; t = nullptr)
                {
                    int notes = 0;
                    for (auto* c : t->getClips()) if (auto* mc2 = dynamic_cast<te::MidiClip*> (c)) notes += mc2->getSequence().getNumNotes();
                    check (notes > 100, "el ritmo de Mole se ve en la maqueta: " + String (notes) + " golpes");
                }
        // every channel of the block shows its instrument below, and the instrument opens
        for (auto* l : lanes)
        {
            click (*l, { l->getWidth() - 20, l->getHeight() / 2 });
            pump (80);
            const int cards = findChildren<ui::DeviceCard> (mc).size();
            auto* at = dynamic_cast<te::AudioTrack*> (l->track.get());
            auto* inst = at != nullptr ? Session::instrumentOf (*at) : nullptr;
            check (cards >= 1, "  " + l->track->getName() + ": abajo " + String (cards) + " tarjetas, instrumento "
                   + (inst != nullptr ? inst->getName() : String ("ninguno")));
        }
    }
    s.newProject (true, true);
    {
        ui::MainComponent mc (s);
        mc.setSize (1500, 940);
        pump (400);
        shot (mc, shots.getChildFile ("01_inicio_housito.png"));

        auto& arr = mc.getArrangement();
        auto lanes = findChildren<ui::TrackLane> (arr);
        check (lanes.size() == 2, "2 carriles en la línea de tiempo (los retornos viven en MASTER): " + String (lanes.size()));
        for (auto* l : lanes) check (kindOf (*l->track) != TrackKind::returnBus, "  " + l->track->getName() + " no es un retorno");
        auto clips = findChildren<ui::ClipComponent> (arr);
        check (clips.size() == 2, "2 clips dibujados: " + String (clips.size()));
        check (mc.getPianoRoll().getClip() != nullptr, "el editor abre el primer clip MIDI");

        // ---------------------------------------------------- piano roll: write a note with a click
        auto& roll = mc.getPianoRoll();
        auto* clip = roll.getClip();
        const int before = clip->getSequence().getNumNotes();
        roll.centreOnNotes();
        pump();
        auto gridComp = roll.getChildComponent (1);
        auto p = roll.pointFor (2.25, 38) - gridComp->getPosition();
        click (*gridComp, p);
        pump();
        check (clip->getSequence().getNumNotes() == before, "un clic en un hueco no escribe nada");
        dblclick (*gridComp, p);
        pump();
        check (clip->getSequence().getNumNotes() == before + 1, "doble clic en un hueco escribe una nota");
        bool found = false;
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == 38 && std::abs (n->getStartBeat().inBeats() - 2.25) < 0.01) found = true;
        check (found, "nota en el tiempo y la fila del clic (caja, 1/16)");
        // drag it two semitones up and one sixteenth right
        auto p2 = roll.pointFor (2.25, 38) - gridComp->getPosition();
        auto p3 = roll.pointFor (2.5, 39) - gridComp->getPosition();
        drag (*gridComp, p2 + Point<int> (2, 0), p3 + Point<int> (2, 0));
        pump();
        found = false;
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == 39 && std::abs (n->getStartBeat().inBeats() - 2.5) < 0.01) found = true;
        check (found, "arrastrar mueve la nota en tiempo y fila (a palmas)");
        s.getEdit().undo();
        s.getEdit().undo();
        pump();
        check (clip->getSequence().getNumNotes() == before, "deshacer quita la nota escrita");

        // selection: drag on empty space draws a box, Shift adds, double click on a note deletes it
        {
            auto& sq = clip->getSequence();
            auto* a1 = sq.addNote (45, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (0.25), 100, 0, nullptr);
            auto* a2 = sq.addNote (46, te::BeatPosition::fromBeats (1.5), te::BeatDuration::fromBeats (0.25), 100, 0, nullptr);
            auto* a3 = sq.addNote (47, te::BeatPosition::fromBeats (3.5), te::BeatDuration::fromBeats (0.25), 100, 0, nullptr);
            pump();
            const int n0 = sq.getNumNotes();
            roll.getSelection().clearQuick();
            auto from = roll.pointFor (0.9, 47) - gridComp->getPosition();
            auto to   = roll.pointFor (1.9, 44) - gridComp->getPosition();
            drag (*gridComp, from, to);
            pump();
            auto& selN = roll.getSelection();
            check (selN.contains (a1) && selN.contains (a2) && ! selN.contains (a3) && sq.getNumNotes() == n0,
                   "clic y arrastrar en un hueco selecciona con un recuadro (" + String (selN.size()) + " notas)");
            auto f3 = roll.pointFor (3.4, 48) - gridComp->getPosition();
            auto t3 = roll.pointFor (3.9, 46) - gridComp->getPosition();
            drag (*gridComp, f3, t3, ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::shiftModifier));
            pump();
            check (roll.getSelection().contains (a1) && roll.getSelection().contains (a3), "Mayús + recuadro suma a la selección");
            dblclick (*gridComp, roll.pointFor (3.5, 47) - gridComp->getPosition() + Point<int> (3, 0));
            pump();
            check (sq.getNumNotes() == n0 - 1, "doble clic en una nota la borra");

            // B: pencil, a drag paints one note per grid step
            roll.setDrawMode (true);
            const int n1 = sq.getNumNotes();
            drag (*gridComp, roll.pointFor (6.0, 50) - gridComp->getPosition() + Point<int> (2, 0),
                             roll.pointFor (6.9, 50) - gridComp->getPosition() + Point<int> (2, 0));
            pump();
            check (sq.getNumNotes() == n1 + 4, "B (lápiz): arrastrar pinta una nota por paso de 1/16 (" + String (sq.getNumNotes() - n1) + ")");
            roll.setDrawMode (false);
            s.getEdit().getUndoManager().undo();
            pump();
            for (auto* x : { a1, a2 }) sq.removeNote (*x, nullptr);
            pump();
        }

        // double hit: a 1/32 grid and two clicks, or a split note
        roll.setGridIndex (7);
        check (std::abs (roll.currentGridBeats() - 0.125) < 1e-9, "rejilla 1/32");
        for (double b : { 3.0, 3.125 })
        {
            auto pp = roll.pointFor (b, 38) - gridComp->getPosition();
            dblclick (*gridComp, pp + Point<int> (2, 0));
            pump (50);
        }
        int hits = 0;
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == 38 && n->getStartBeat().inBeats() >= 2.99 && n->getStartBeat().inBeats() < 3.2 && std::abs (n->getLengthBeats().inBeats() - 0.125) < 1e-6) ++hits;
        check (hits == 2, "doble golpe con rejilla 1/32: dos notas de 1/32 seguidas (" + String (hits) + ")");
        roll.stepGrid (1);
        check (std::abs (roll.currentGridBeats() - 0.25) < 1e-9, "botón + vuelve a 1/16"_u);
        {
            auto* n = clip->getSequence().addNote (40, te::BeatPosition::fromBeats (5.0), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
            roll.getSelection().clearQuick();
            roll.getSelection().add (n);
            roll.splitSelected (2);
            int halves = 0;
            for (auto* x : clip->getSequence().getNotes())
                if (x->getNoteNumber() == 40 && std::abs (x->getLengthBeats().inBeats() - 0.25) < 1e-6) ++halves;
            check (halves == 2, "dividir una nota en dos golpes iguales");
        }
        pump();
        shot (mc, shots.getChildFile ("16_piano_roll_notas.png"));

        // ---------------------------------------------------- Piano for a melodic clip: one octave with Do, Re, Mi
        {
            te::MidiClip* bass = nullptr;
            for (auto* t : te::getAudioTracks (s.getEdit()))
                if (t != clip->getTrack())
                    for (auto* c : t->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c); m != nullptr && m->getSequence().getNumNotes() > 0 && bass == nullptr) bass = m;
            if (bass == nullptr) check (false, "un clip melódico en el proyecto"_u);
            else
            {
                roll.setClip (bass);
                pump (80);
                const int lastRow = roll.visibleRowCount() - 1;
                check (roll.isOctaveView() && roll.visibleRowCount() >= 12 && roll.rowHeight() == 14.0f,
                       "un clip melódico abre con su octava entera a la vista: " + String (roll.visibleRowCount()) + " filas de 14 px");
                check (roll.keyLabel (lastRow).startsWith ("Do") && roll.keyLabel (lastRow - 11).startsWith ("Si") && roll.keyLabel (lastRow - 6).startsWith ("Fa#"),
                       "las teclas dicen Do, Re, Mi: abajo " + roll.keyLabel (lastRow) + ", la octava sube hasta " + roll.keyLabel (lastRow - 11));
                int inView = 0;
                for (auto* n : bass->getSequence().getNotes()) if (n->getNoteNumber() >= roll.viewOctaveBase() && n->getNoteNumber() < roll.viewOctaveBase() + 12) ++inView;
                check (inView > 0, "abre en la octava donde están sus notas ("_u + roll.keyLabel (lastRow) + ", " + String (inView) + " notas)");
                shot (mc, shots.getChildFile ("16b_piano_una_octava.png"));
                {
                    // a taller panel shows more octaves with rows of the same size; the icon in the corner stays square
                    const float rowH1 = roll.rowHeight();
                    mc.setBottomHeight (660);
                    pump (150);
                    check (roll.visibleRows() >= 24 && roll.visibleRowCount() == roll.visibleRows() && roll.rowHeight() == rowH1,
                           "con el panel más alto el Piano muestra " + String (roll.visibleRows()) + " notas, filas de "
                           + String (roll.rowHeight(), 1) + " px (antes " + String (rowH1, 1) + "): más cuadritos, del mismo tamaño");
                    // 100 px less: fewer notes, the same size (it does not wait for a whole octave)
                    {
                        const int rows1 = roll.visibleRows();
                        mc.setBottomHeight (560);
                        pump (120);
                        check (roll.visibleRows() < rows1 && roll.visibleRows() > 12 && roll.rowHeight() == rowH1,
                               "100 px menos de panel son " + String (rows1 - roll.visibleRows()) + " notas menos (" + String (roll.visibleRows())
                               + "), filas de " + String (roll.rowHeight(), 1) + " px");
                        mc.setBottomHeight (660);
                        pump (120);
                    }
                    const auto ib = mc.instrumentIconBounds();
                    check (ib.getWidth() == ib.getHeight() && ib.getWidth() >= 16 && ib.getY() > mc.getArrangement().getBottom(),
                           "el ícono del instrumento sigue cuadrado y en la esquina del panel ("_u + String (ib.getWidth()) + " x " + String (ib.getHeight()) + ")");
                    shot (mc, shots.getChildFile ("16c_piano_panel_alto.png"));
                    {
                        // ALEATORIA on the bass: in the song's key, low, inside the clip
                        int root = 0; bool minor = true; String where;
                        roll.randomKey (root, minor, &where);
                        int moldeHigh = 0;
                        for (auto* n : bass->getSequence().getNotes()) moldeHigh = jmax (moldeHigh, n->getNoteNumber());
                        roll.generateRandom (std::nullopt, 99);
                        pump (60);
                        const auto range = roll.playingRange();
                        static const int maj[] { 0, 2, 4, 5, 7, 9, 11 }, mi[] { 0, 2, 3, 5, 7, 8, 10 };
                        int bad = 0, highest = 0, count = 0;
                        for (auto* n : bass->getSequence().getNotes())
                        {
                            if (n->getStartBeat().inBeats() < range.getStart() - 1e-6 || n->getStartBeat().inBeats() >= range.getEnd()) continue;
                            ++count;
                            const int rel = ((n->getNoteNumber() - root) % 12 + 12) % 12;
                            bool ok = false;
                            for (int i = 0; i < 7; ++i) ok = ok || (minor ? mi : maj)[i] == rel;
                            if (! ok) ++bad;
                            highest = jmax (highest, n->getNoteNumber());
                        }
                        check (roll.defaultRole() == aleatorio::Rol::bajo && count >= 8 && bad == 0 && highest <= jmax (52, moldeHigh),
                               "ALEATORIA en Badger: un bajo de " + String (count) + " notas en " + aleatorio::nombreTonalidad (root, minor) + " (" + where + "), "
                               + String (bad) + " fuera de tono, la más aguda " + String (highest) + " (el clip llegaba a " + String (moldeHigh) + ")");
                        shot (mc, shots.getChildFile ("16d_bajo_aleatorio.png"));
                        s.getEdit().getUndoManager().undo();
                        pump (60);
                    }
                    {
                        // ALEATORIA fills the whole clip: three loose notes ask for a whole bass line, and an idea that
                        // only takes the first bar repeats and varies to the end instead of leaving the rest empty
                        auto& seq = bass->getSequence();
                        auto* um = &s.getEdit().getUndoManager();
                        const auto r = roll.playingRange();
                        const double bpb = s.getTimeSigNumerator();
                        auto clearAndWrite = [&] (int count, double step)
                        {
                            um->beginNewTransaction();
                            Array<te::MidiNote*> all;
                            for (auto* n : seq.getNotes()) all.add (n);
                            for (auto* n : all) seq.removeNote (*n, um);
                            for (int k = 0; k < count; ++k)
                                seq.addNote (45 + (k % 3) * 2, te::BeatPosition::fromBeats (r.getStart() + k * step), te::BeatDuration::fromBeats (step * 0.8), 100, 0, um);
                        };
                        auto spread = [&] (int& count, double& last)
                        {
                            count = 0; last = 0.0;
                            for (auto* n : seq.getNotes())
                                if (n->getStartBeat().inBeats() >= r.getStart() - 1e-6 && n->getStartBeat().inBeats() < r.getEnd()) { ++count; last = jmax (last, n->getStartBeat().inBeats() - r.getStart()); }
                        };
                        int count = 0; double last = 0.0;
                        clearAndWrite (3, 1.0);
                        roll.generateRandom (std::nullopt, 123);
                        pump (40);
                        spread (count, last);
                        check (count >= 8 && last >= r.getLength() - 2.0 * bpb,
                               "ALEATORIA con 3 notas sueltas escribe un bajo en todo el clip: "_u + String (count) + " notas, la última en el tiempo "
                               + String (last, 1) + " de " + String (r.getLength(), 0));
                        um->undo(); um->undo();
                        clearAndWrite (8, 0.5);
                        roll.generateRandom (std::nullopt, 321);
                        pump (40);
                        spread (count, last);
                        check (count >= 8 * (int) (r.getLength() / bpb) / 2 && last >= r.getLength() - bpb,
                               "una idea de 1 compás al inicio de un clip largo se repite y varía hasta el final: "_u + String (count) + " notas, la última en el tiempo "
                               + String (last, 1) + " de " + String (r.getLength(), 0));
                        um->undo(); um->undo();
                        pump (40);
                        // Cmd+Shift+Up: a copy of the selected notes an octave up, on top of them
                        roll.selectAll();
                        const int n0 = bass->getSequence().getNumNotes();
                        int hi0 = 0;
                        for (auto* n : bass->getSequence().getNotes()) hi0 = jmax (hi0, n->getNoteNumber());
                        roll.keyPressed (KeyPress (KeyPress::upKey, ModifierKeys (ModifierKeys::commandModifier | ModifierKeys::shiftModifier), 0));
                        int hi1 = 0;
                        for (auto* n : bass->getSequence().getNotes()) hi1 = jmax (hi1, n->getNoteNumber());
                        check (n0 > 0 && bass->getSequence().getNumNotes() == 2 * n0 && hi1 == hi0 + 12 && roll.getSelection().size() == n0,
                               "Cmd+Mayús+flecha arriba copia las notas una octava arriba: "_u + String (n0) + " + " + String (n0) + " notas");
                        um->undo();
                        pump (40);
                    }
                    mc.setBottomHeight (360);
                    pump (150);
                    check (roll.visibleRows() == lastRow + 1 && roll.rowHeight() == rowH1, "y de vuelta a su tamaño, las mismas " + String (roll.visibleRows()) + " notas de 14 px");
                }
                const int base = roll.viewOctaveBase();
                roll.shiftOctave (1);
                check (roll.viewOctaveBase() == base + 12, "el botón + muestra la octava de arriba");
                roll.shiftOctave (-1);
                // select everything, up one octave with Shift+Up: the notes move and the view follows them
                std::vector<int> pitches;
                for (auto* n : bass->getSequence().getNotes()) pitches.push_back (n->getNoteNumber());
                roll.selectAll();
                roll.keyPressed (KeyPress (KeyPress::upKey, ModifierKeys::shiftModifier, 0));
                bool up = true; size_t k = 0;
                for (auto* n : bass->getSequence().getNotes()) up = up && n->getNoteNumber() == pitches[k++] + 12;
                check (up && roll.viewOctaveBase() == base + 12, "Mayús+arriba sube una octava y la vista va con las notas");
                roll.moveOctave (-1);
                // reverse (R): the first note ends up last
                auto firstStart = [bass] { double s0 = 1e9; int p0 = -1; for (auto* n : bass->getSequence().getNotes()) if (n->getStartBeat().inBeats() < s0) { s0 = n->getStartBeat().inBeats(); p0 = n->getNoteNumber(); } return p0; };
                auto lastStart = [bass] { double s0 = -1e9; int p0 = -1; for (auto* n : bass->getSequence().getNotes()) if (n->getStartBeat().inBeats() > s0) { s0 = n->getStartBeat().inBeats(); p0 = n->getNoteNumber(); } return p0; };
                const int f0 = firstStart();
                roll.getSelection().clearQuick();
                roll.keyPressed (KeyPress ('r'));
                check (lastStart() == f0, "R voltea el clip: la primera nota quedó al final");
                roll.keyPressed (KeyPress ('r', ModifierKeys::commandModifier, 0));
                check (firstStart() == f0, "Cmd+R lo regresa");
                // 0 mutes the selection, Shift+Right makes it one grid step longer
                roll.selectAll();
                roll.keyPressed (KeyPress ('0'));
                bool muted = true;
                for (auto* n : bass->getSequence().getNotes()) muted = muted && n->isMute();
                check (muted, "0 apaga las notas seleccionadas");
                roll.keyPressed (KeyPress ('0'));
                auto* one = bass->getSequence().getNotes().getFirst();
                roll.getSelection().clearQuick();
                roll.getSelection().add (one);
                const double len0 = one->getLengthBeats().inBeats();
                roll.keyPressed (KeyPress (KeyPress::rightKey, ModifierKeys::shiftModifier, 0));
                check (std::abs (one->getLengthBeats().inBeats() - len0 - roll.currentGridBeats()) < 1e-6, "Mayús+derecha alarga la nota un paso de rejilla");
                // Cmd+3: triplets; Cmd+4: free grid and back
                const int gi = roll.getGridIndex();
                roll.keyPressed (KeyPress ('3', ModifierKeys::commandModifier, 0));
                check (roll.getGridIndex() == gi + 5, "Cmd+3 pasa la rejilla a tresillos");
                roll.keyPressed (KeyPress ('3', ModifierKeys::commandModifier, 0));
                roll.keyPressed (KeyPress ('4', ModifierKeys::commandModifier, 0));
                check (roll.getGridIndex() == 13, "Cmd+4 suelta la rejilla");
                roll.keyPressed (KeyPress ('4', ModifierKeys::commandModifier, 0));
                check (roll.getGridIndex() == gi, "y Cmd+4 otra vez la regresa");
                // copy and paste
                const int count = bass->getSequence().getNumNotes();
                roll.selectAll();
                roll.keyPressed (KeyPress ('c', ModifierKeys::commandModifier, 0));
                roll.keyPressed (KeyPress ('v', ModifierKeys::commandModifier, 0));
                check (bass->getSequence().getNumNotes() == count * 2, "Cmd+C y Cmd+V copian las notas: " + String (bass->getSequence().getNumNotes()));
                s.getEdit().undo();
                // invert: the highest note becomes the lowest
                int lo = 127, hi = 0;
                for (auto* n : bass->getSequence().getNotes()) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
                auto* top = bass->getSequence().getNotes().getFirst();
                for (auto* n : bass->getSequence().getNotes()) if (n->getNoteNumber() == hi) top = n;
                roll.getSelection().clearQuick();
                roll.invertNotes();
                check (top->getNoteNumber() == lo, "invertir: la nota más aguda quedó abajo");
                s.getEdit().undo();
                pump();
                // the sequence gets longer from the Piano: drag the light line at its end one bar to the right
                {
                    const double bar = mc.getViewState().beatsPerBar();
                    auto len = [&roll] { return roll.getClip() != nullptr ? ui::notes::playingRange (*roll.getClip()).getLength() : 0.0; };
                    const double before = len();
                    auto* g = roll.getChildComponent (1);
                    const auto end = ui::notes::playingRange (*bass).getEnd();
                    const auto from = roll.pointFor (end, roll.viewOctaveBase() + 5) - g->getPosition();
                    const auto to = roll.pointFor (end + bar, roll.viewOctaveBase() + 5) - g->getPosition();
                    drag (*g, from - Point<int> (2, 0), to - Point<int> (2, 0));
                    pump (60);
                    check (std::abs (len() - before - bar) < 1.0e-6, "arrastrar el final del clip en el Piano lo alarga un compás ("_u + String (before / bar, 0) + " a " + String (len() / bar, 0) + ")");
                    roll.setClipEnd (ui::notes::playingRange (*bass).getStart() + 8.0 * bar);
                    check (std::abs (len() - 8.0 * bar) < 1.0e-6, "Largo del clip: 8 compases");
                    s.getEdit().undo();
                    s.getEdit().undo();
                    pump();
                }
                // in the arrangement: R on a selected MIDI clip, Cmd+J joins two clips of one channel
                {
                    const int f1 = firstStart();
                    s.getSelection().selectOnly (bass);
                    mc.reverseSelection();
                    check (lastStart() == f1, "R en la maqueta voltea el clip MIDI seleccionado");
                    s.getEdit().undo();
                    auto* at = dynamic_cast<te::AudioTrack*> (bass->getTrack());
                    const int notesOne = bass->getSequence().getNumNotes(), clipsBefore = at->getClips().size();
                    s.getEdit().getUndoManager().beginNewTransaction();
                    auto* copy = s.duplicateClip (*bass);
                    te::SelectableList both;
                    both.add (bass); both.add (copy);
                    s.getSelection().select (both);
                    mc.consolidateSelection();
                    pump (60);
                    int midiClips = 0, total = 0;
                    for (auto* c : at->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c)) { ++midiClips; total += m->getSequence().getNumNotes(); }
                    check (at->getClips().size() == clipsBefore && total == notesOne * 2,
                           "Cmd+J junta dos clips en uno con todas sus notas (" + String (midiClips) + " clip, " + String (total) + " notas)");
                    s.getEdit().undo();
                    pump();
                    {
                        // the same with a piece of the arrangement chosen over both (clic derecho > Unir los clips en uno)
                        auto& vsr = mc.getViewState();
                        s.getSelection().deselectAll();
                        vsr.rangeTracks = { at };
                        double a0 = 1.0e9, a1 = -1.0e9;
                        for (auto* c : at->getClips())
                            if (dynamic_cast<te::MidiClip*> (c) != nullptr)
                            { a0 = jmin (a0, vsr.timeToBeat (c->getPosition().getStart())); a1 = jmax (a1, vsr.timeToBeat (c->getPosition().getEnd())); }
                        vsr.rangeStart = a0;
                        vsr.rangeEnd = a1;
                        const int inRange = mc.midiClipsInRange().size();
                        int clipsPrev = 0, notesPrev = 0;
                        for (auto* c : at->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c)) { ++clipsPrev; notesPrev += m->getSequence().getNumNotes(); }
                        mc.consolidateSelection();
                        pump (60);
                        int clipsAfter = 0, notesAfter = 0;
                        for (auto* c : at->getClips()) if (auto* m = dynamic_cast<te::MidiClip*> (c)) { ++clipsAfter; notesAfter += m->getSequence().getNumNotes(); }
                        check (inRange >= 2 && clipsAfter == clipsPrev - inRange + 1 && notesAfter == notesPrev,
                               "Unir los clips en uno con un pedazo elegido sobre los dos (" + String (inRange) + " clips en el pedazo)");
                        vsr.rangeTracks.clear();
                        vsr.rangeStart = vsr.rangeEnd = 0.0;
                        s.getEdit().undo();
                    }
                    s.getEdit().undo();
                    pump();
                }
                roll.setClip (clip);
                pump (80);
                check (! roll.isOctaveView() && roll.visibleRowCount() >= 10, "la batería vuelve a verse por tambores, no por octavas");
            }
        }

        // ---------------------------------------------------- arrangement: move a clip with the mouse
        clips = findChildren<ui::ClipComponent> (arr);
        auto* cc = clips.getFirst();
        auto clipPtr = cc->clip;
        const double startBefore = s.getEdit().tempoSequence.toBeats (clipPtr->getPosition().getStart()).inBeats();
        const int px = (int) std::round (4.0 * mc.getViewState().pxPerBeat);
        drag (*cc, { cc->getWidth() / 2, 8 }, { cc->getWidth() / 2 + px, 8 });   // by its name strip
        pump();
        const double startAfter = s.getEdit().tempoSequence.toBeats (clipPtr->getPosition().getStart()).inBeats();
        check (std::abs (startAfter - startBefore - 4.0) < 0.01, "arrastrar un clip lo mueve un compás: " + String (startAfter - startBefore, 2));
        clipPtr = nullptr;
        s.getEdit().undo();
        pump();

        // ---------------------------------------------------- several clips move together
        {
            auto all = findChildren<ui::ClipComponent> (arr);
            if (all.size() >= 2)
            {
                auto* a = all[0];
                ui::ClipComponent* b = nullptr;
                for (auto* c : all) if (c->clip->getTrack() != a->clip->getTrack()) { b = c; break; }
                if (b != nullptr)
                {
                    auto ca = a->clip, cb = b->clip;
                    auto& ts = s.getEdit().tempoSequence;
                    const double a0 = ts.toBeats (ca->getPosition().getStart()).inBeats(), b0 = ts.toBeats (cb->getPosition().getStart()).inBeats();
                    click (*a, { 20, 8 });
                    click (*b, { 20, 8 }, ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::shiftModifier));
                    drag (*a, { 20, 8 }, { 20 + px, 8 });
                    pump();
                    const double a1 = ts.toBeats (ca->getPosition().getStart()).inBeats(), b1 = ts.toBeats (cb->getPosition().getStart()).inBeats();
                    check (std::abs (a1 - a0 - 4.0) < 0.01 && std::abs (b1 - b0 - 4.0) < 0.01,
                           "con dos clips seleccionados, arrastrar uno mueve los dos (" + String (a1 - a0, 2) + " y " + String (b1 - b0, 2) + ")");
                    s.getEdit().undo();
                    pump();
                }
            }
        }

        // ---------------------------------------------------- a piece of a clip: drag over its body, copy, paste elsewhere
        {
            auto& v = mc.getViewState();
            auto lanes2 = findChildren<ui::TrackLane> (arr);
            auto* lane0 = lanes2.getFirst();
            auto lc = findChildren<ui::ClipComponent> (*lane0);
            if (lane0 != nullptr && ! lc.isEmpty())
            {
                auto* c0 = lc.getFirst();
                const double cStart = s.getEdit().tempoSequence.toBeats (c0->clip->getPosition().getStart()).inBeats();
                const int xa = (int) ((cStart + 1.0 - v.scrollBeats) * v.pxPerBeat) - c0->getX();
                const int xb = xa + (int) std::round (2.0 * v.pxPerBeat);
                drag (*c0, { xa, 30 }, { xb, 30 });
                pump();
                check (v.hasRange() && std::abs (v.rangeEnd - v.rangeStart - 2.0) < 0.01 && v.rangeTracks.size() == 1,
                       "arrastrar sobre el cuerpo del clip elige un pedazo: " + String (v.rangeEnd - v.rangeStart, 2) + " tiempos");
                shot (mc, shots.getChildFile ("03b_pedazo_elegido.png"));
                mc.invokeDirectly (ui::MainComponent::editCopy, false);
                auto* at0 = dynamic_cast<te::AudioTrack*> (lane0->track.get());
                const int before = at0->getClips().size();
                click (*lane0, { (int) v.beatToX (64.0), lane0->getHeight() - 6 });   // the cursor at bar 17
                pump();
                mc.invokeDirectly (ui::MainComponent::editPaste, false);
                pump();
                double newStart = -1.0, newLen = 0.0;
                for (auto* c : at0->getClips())
                {
                    const double st = s.getEdit().tempoSequence.toBeats (c->getPosition().getStart()).inBeats();
                    if (std::abs (st - 64.0) < 0.01) { newStart = st; newLen = s.getEdit().tempoSequence.toBeats (c->getPosition().getEnd()).inBeats() - st; }
                }
                check (at0->getClips().size() == before + 1 && newStart >= 0.0 && std::abs (newLen - 2.0) < 0.01,
                       "Cmd+C y Cmd+V pegan solo ese pedazo donde hiciste clic (" + String (newLen, 2) + " tiempos)");
                s.getEdit().undo();
                pump();
            }
        }

        // ---------------------------------------------------- double click on an instrument lane creates a clip
        auto* drumsLane = lanes.getFirst();
        const int clipsBefore = dynamic_cast<te::AudioTrack*> (drumsLane->track.get())->getClips().size();
        const int x = (int) mc.getViewState().beatToX (40.0);
        drumsLane->mouseDoubleClick (makeEvent (*drumsLane, { x, 30 }, ModifierKeys::leftButtonModifier, 2));
        pump();
        check (dynamic_cast<te::AudioTrack*> (drumsLane->track.get())->getClips().size() == clipsBefore + 1, "doble clic crea un clip MIDI");
        check (mc.getPianoRoll().getClip() != nullptr && mc.getPianoRoll().getClip()->getSequence().getNumNotes() == 0, "y lo abre vacío en el editor");
        shot (mc, shots.getChildFile ("02_clip_nuevo.png"));
        s.getEdit().undo();
        pump();

        // ---------------------------------------------------- views
        mc.showBottom (ui::MainComponent::Bottom::mixer);
        pump (300);
        auto strips = findChildren<ui::ChannelStrip> (mc.getMixer());
        check (strips.size() == 5, "mezclador: 4 canales y master: " + String (strips.size()));
        shot (mc, shots.getChildFile ("03_mezclador_facil.png"));
        {
            // a new return is made from MASTER and shows up there, never among the channels
            const int before = findChildren<ui::ChannelStrip> (mc.getMixer()).size();
            s.getEdit().getUndoManager().beginNewTransaction();
            s.addReturnTrack (2, "Eco", "delay");
            pump (400);
            check (findChildren<ui::ChannelStrip> (mc.getMixer()).size() == before + 1, "retorno nuevo aparece en MASTER");
            bool inLanes = false;
            for (auto* l : findChildren<ui::TrackLane> (mc.getArrangement())) inLanes = inLanes || kindOf (*l->track) == TrackKind::returnBus;
            check (! inLanes, "y no en los canales");
            s.getEdit().undo();
            pump (400);
        }
        {
            // a fader goes where you click: the cap jumps under the pointer and follows it from there
            auto faders = findChildren<ui::Fader> (mc.getMixer());
            if (! faders.isEmpty())
            {
                auto* f = faders.getFirst();
                const int cx = f->getWidth() / 2;
                const int capY = (int) f->getPositionOfValue (f->getValue());
                const int farY = capY > f->getHeight() / 2 ? 20 : f->getHeight() - 40;
                const double before = f->getValue();
                click (*f, { cx, farY });
                pump (60);
                const float now = f->getPositionOfValue (f->getValue());
                check (f->hitTest (cx, farY) && std::abs (now - (float) farY) <= 3.0f,
                       "fader del mezclador: un clic lejos de la tapa la lleva al puntero (y=" + String (now, 0) + ", clic en " + String (farY) + ")");
                f->setValue (before, sendNotificationSync);
            }
            else check (false, "hay faders en MASTER");
            auto knobs = findChildren<ui::Knob> (mc.getMixer());
            if (! knobs.isEmpty())
            {
                auto& sl = knobs.getFirst()->slider;
                check (sl.hitTest (sl.getWidth() / 2, sl.getHeight() / 2) && ! sl.hitTest (0, 0), "perilla: solo responde sobre ella");
            }
        }

        mc.setEasyMode (false);
        pump (400);
        strips = findChildren<ui::ChannelStrip> (mc.getMixer());
        auto knobs = findChildren<ui::Knob> (mc.getMixer());
        check (knobs.size() > strips.size(), "modo avanzado muestra envíos en el mezclador");
        shot (mc, shots.getChildFile ("04_mezclador_completo.png"));

        // automation lane
        auto tracks = te::getAudioTracks (s.getEdit());
        for (auto* p : tracks[1]->getAllAutomatableParams())
            if (auto* plug = p->getPlugin(); plug != nullptr && dynamic_cast<te::VolumeAndPanPlugin*> (plug) != nullptr && ! p->getParameterName().containsIgnoreCase ("pan"))
            { ui::AutoLanes::add (*tracks[1], *p); break; }
        pump (300);
        auto autoLanes = findChildren<ui::AutomationLane> (arr);
        check (autoLanes.size() == 1, "carril de automatización visible");
        if (auto* al = autoLanes.getFirst())
        {
            const int ax = s.getEdit().getMarkerManager().getMarkers().size(); (void) ax;
            click (*al, { mc.getViewState().headerWidth + (int) mc.getViewState().beatToX (8.0), 20 });
            pump();
            check (al->param != nullptr && al->param->getCurve().getNumPoints() == 1, "clic añade un punto de automatización");
        }
        mc.showBottom (ui::MainComponent::Bottom::editor);
        pump (300);
        shot (mc, shots.getChildFile ("05_completo_automatizacion.png"));

        mc.getBrowser().setTab (ui::Browser::effects);
        pump (200);
        shot (mc.getBrowser(), shots.getChildFile ("06_navegador_efectos.png"));
        mc.getBrowser().setTab (ui::Browser::instruments);

        // audio clip inspector with a rendered file
        ExportOptions o;
        o.destination = out.getChildFile ("loop.wav");
        o.loopOnly = true;
        s.exportMix (o);
        if (auto* t = s.addAudioTrack ("Loop"))
            if (auto* wc = s.addAudioFile (*t, o.destination, s.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (32.0))))
            {
                pump (600);
                mc.openClipEditor (wc);
                pump (1200);
                shot (mc, shots.getChildFile ("07_clip_audio.png"));
                check (findChildren<ui::ClipComponent> (arr).size() >= 3, "clip de audio en la línea de tiempo");
                // warp markers: one at 1 s of the file, moved to 1.5 s; that moment of the file now sounds at 1.5 s
                {
                    auto& ap = mc.getAudioPanel();
                    const int idx = ap.addWarpMarker (1.0);
                    pump (60);
                    const int before = idx;
                    ap.moveWarpMarker (idx, 1.5);
                    pump (60);
                    auto& wm = wc->getWarpTimeManager();
                    const double src = wm.warpTimeToSourceTime (te::TimePosition::fromSeconds (1.5)).inSeconds();
                    const double half = wm.warpTimeToSourceTime (te::TimePosition::fromSeconds (0.75)).inSeconds();
                    check (wc->getWarpTime() && before > 0 && std::abs (src - 1.0) < 0.002 && std::abs (half - 0.5) < 0.002,
                           "marcador de warp: el segundo 1 del archivo suena en el 1.5 y lo de antes se estira (0.75 s -> " + String (half, 3) + " s del archivo)");
                    // the double click on the waveform adds a marker where the mouse is
                    const int markers = wm.getMarkers().size();
                    const auto wa = ap.waveArea();
                    ap.mouseDoubleClick (makeEvent (ap, { (int) ap.xForWarpTime (2.5), wa.getCentreY() }, ModifierKeys::leftButtonModifier, 2));
                    pump (30);
                    check (wm.getMarkers().size() == markers + 1, "doble clic en la onda pone otro marcador (" + String (wm.getMarkers().size()) + ")");
                    shot (mc, shots.getChildFile ("07b_marcadores_warp.png"));
                    s.getEdit().getUndoManager().undo();
                    s.getEdit().getUndoManager().undo();
                    pump (30);
                    check (std::abs (wm.warpTimeToSourceTime (te::TimePosition::fromSeconds (1.5)).inSeconds() - 1.5) < 0.002 || ! wc->getWarpTime(),
                           "Cmd+Z deshace el marcador y el movimiento");
                    // a marker on every hit, then all of them onto the 1/16 grid of the song
                    {
                        const int hits = ap.markersAtHits();
                        pump (30);
                        check (hits >= 4, "poner un marcador en cada golpe: " + String (hits) + " marcadores");
                        ap.markersToGrid (0.25);
                        pump (30);
                        auto& ts = s.getEdit().tempoSequence;
                        const auto pos = wc->getPosition();
                        double worst = 0.0;
                        for (int i = 1; i + 1 < wm.getMarkers().size(); ++i)
                        {
                            const auto t = pos.getStart() + te::TimeDuration::fromSeconds (wm.getMarkers()[i]->warpTime.inSeconds() - pos.getOffset().inSeconds());
                            const double b = ts.toBeats (t).inBeats() / 0.25;
                            worst = jmax (worst, std::abs (b - std::round (b)) * 0.25);
                        }
                        check (worst < 0.002, "los marcadores quedan en la rejilla de 1/16 (lo más lejos: "_u + String (worst, 4) + " tiempos)");
                        shot (mc, shots.getChildFile ("07c_warp_golpes.png"));
                        s.getEdit().getUndoManager().undo();
                        s.getEdit().getUndoManager().undo();
                        pump (30);
                    }
                    // Separar en stems: four channels with the four stems where the clip is, the clip silenced.
                    // (In the tests the model has random weights: what is checked is the path, not how well it separates.)
                    if (stems::modelReady())
                    {
                        const int tracksBefore = te::getAudioTracks (s.getEdit()).size();
                        String msg;
                        const auto t0 = Time::getMillisecondCounterHiRes();
                        auto made = stems::separateNow (s, *wc, msg);
                        const double secs = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                        pump (60);
                        bool same = made.size() == 4;
                        for (auto* c : made)
                            same = same && std::abs ((c->getPosition().getStart() - wc->getPosition().getStart()).inSeconds()) < 1.0e-6
                                        && std::abs ((c->getPosition().getLength() - wc->getPosition().getLength()).inSeconds()) < 1.0e-6
                                        && c->getAudioFile().getLength() > wc->getAudioFile().getLength() - 0.01;
                        check (same && wc->isMuted() && te::getAudioTracks (s.getEdit()).size() == tracksBefore + 4,
                               "separar en stems: 4 canales nuevos (" + String (made.size()) + "), alineados con el clip y el original apagado ("
                               + String (secs, 1) + " s para " + String (wc->getAudioFile().getLength(), 1) + " s de audio) " + (made.isEmpty() ? msg : String()));
                        if (made.size() == 4)
                            check (made[0]->getClipTrack()->getName().startsWith ("Voz") && made[1]->getClipTrack()->getName().startsWith ("Bater"),
                                   "los canales se llaman Voz, Batería, Bajo y Otros"_u);
                        shot (mc, shots.getChildFile ("07d_stems.png"));
                        // Tracktion reads each new file's details a moment later as its own step: at most two Cmd+Z
                        int undos = 0;
                        while (undos < 2 && te::getAudioTracks (s.getEdit()).size() != tracksBefore) { s.getEdit().getUndoManager().undo(); pump (60); ++undos; }
                        check (te::getAudioTracks (s.getEdit()).size() == tracksBefore && ! wc->isMuted(), "Cmd+Z quita los cuatro canales y prende el original (" + String (undos) + " pasos, "
                               + String (te::getAudioTracks (s.getEdit()).size() - tracksBefore) + " de sobra, apagado " + String ((int) wc->isMuted()) + ")");
                    }
                    else
                    {
                        auto old = stems::modelFile();
                        check (! stems::modelReady(), "sin el modelo, Separar en stems lo pide (no hay modelo de prueba: RACOON_STEMS_MODEL)");
                    }
                }
            }

        // Samples: a click or the arrows play the sound
        {
            auto& br = mc.getBrowser();
            br.setTab (ui::Browser::samples);
            br.openFolder (out);
            pump (60);
            auto* list = br.sampleList();
            auto& pv = SamplePreview::get (s.engine);
            pv.stop();
            int presses = 0;
            while (presses < 30 && ! pv.currentFile().hasFileExtension ("wav"))
            {
                list->keyPressed (KeyPress (KeyPress::downKey));
                ++presses;
            }
            const auto first = pv.currentFile();
            check (first.hasFileExtension ("wav"), "Samples: con la flecha abajo se escucha el audio elegido (" + first.getFileName() + ")");
            list->keyPressed (KeyPress (KeyPress::downKey));
            const auto second = pv.currentFile();
            list->keyPressed (KeyPress (KeyPress::upKey));
            check (pv.currentFile() == first, "flecha arriba vuelve al anterior y suena (" + second.getFileName() + " -> " + pv.currentFile().getFileName() + ")");
            pv.stop();
            click (*list, { 40, presses * 24 - 12 });
            check (pv.currentFile() == first, "un clic en un audio lo escucha");
            shot (br, shots.getChildFile ("06b_samples_escuchar.png"));
            {
                // the search box: "tom" lists every sound with tom in its name or its folder, from all the libraries
                br.searchSamples ("tom");
                pump (40);
                const auto found = br.sampleResults();
                int wrong = 0;
                for (auto& f : found)
                    if (! (f.getFileName().containsIgnoreCase ("tom") || f.getParentDirectory().getFileName().containsIgnoreCase ("tom")
                           || muestras::nombreVisible (f).containsIgnoreCase ("tom") || muestras::nombreVisible (f.getParentDirectory()).containsIgnoreCase ("tom"))) ++wrong;
                check (found.size() > 0 && wrong == 0, "Samples: buscar «tom» da "_u + String (found.size()) + " sonidos, todos con tom en su nombre o su carpeta");
                shot (br, shots.getChildFile ("06c_samples_buscar.png"));
                br.searchSamples ({});
                check (br.sampleResults().isEmpty(), "Samples: borrar la búsqueda regresa a la carpeta"_u);
            }
            br.setTab (ui::Browser::instruments);
            check (! pv.isPlaying() || pv.currentFile() == File(), "al salir de Samples se calla");
        }

        ui::ExportPanel ep (s);
        ep.setSize (460, 400);
        shot (ep, shots.getChildFile ("08_exportar.png"));

        // the mouse wheel over a track, with nothing left to scroll (it used to recurse until the stack ran out)
        {
            auto ls = findChildren<ui::TrackLane> (arr);
            if (! ls.isEmpty())
            {
                auto* lane = ls.getFirst();
                auto src = Desktop::getInstance().getMainMouseSource();
                MouseEvent me (src, { 50.0f, 10.0f }, ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, lane, lane, Time::getCurrentTime(),
                               { 50.0f, 10.0f }, Time::getCurrentTime(), 1, false);
                MouseWheelDetails w { 0.0f, -1.0f, false, false, false };
                for (int i = 0; i < 40; ++i) lane->mouseWheelMove (me, w);   // to the bottom, then keep going
                w.deltaY = 1.0f;
                for (int i = 0; i < 40; ++i) lane->mouseWheelMove (me, w);   // to the top, then keep going
                check (true, "rueda del ratón sobre las pistas hasta el tope: sin cierre");
            }
        }

        // every instrument the user can add: create it, open its window, play, close
        {
            Array<CatalogItem> items;
            for (auto& it : Catalog::racoonItems (s.engine)) if (it.isInstrument) items.add (it);
            for (auto& it : Catalog::builtInItems()) if (it.isInstrument) items.add (it);
            for (auto& it : Catalog::racoonItems (s.engine)) if (! it.isInstrument) items.add (it);
            for (auto& it : items)
            {
                std::cout << "  agregar " << it.name << (it.available ? "" : " (no instalado)") << std::endl;
                const auto beforeTracks = te::getAudioTracks (s.getEdit());
                const int before = beforeTracks.size();
                if (it.isInstrument) mc.addInstrument (it);
                else
                    for (auto* t : te::getAudioTracks (s.getEdit()))
                        if (Session::instrumentOf (*t) != nullptr) { s.insertPlugin (*t, it); break; }
                pump (300);
                if (it.isInstrument && it.available)
                {
                    // new tracks go above the returns, so look for the one that was not there before
                    auto tracks = te::getAudioTracks (s.getEdit());
                    te::AudioTrack* t = nullptr;
                    for (auto* a : tracks) if (! beforeTracks.contains (a)) t = a;
                    check (tracks.size() == before + 1 && t != nullptr && Session::instrumentOf (*t) != nullptr
                             && kindOf (*tracks.getLast()) == TrackKind::returnBus, "  pista nueva con " + it.name + " (encima de los retornos)");
                    if (auto* p = Session::instrumentOf (*t)) { p->showWindowExplicitly(); pump (500); p->hideWindowForShutdown(); pump (100); }
                }
            }
            s.getTransport().play (false);
            pump (1500);
            s.getTransport().stop (false, false);
            pump (200);
            shot (mc, shots.getChildFile ("10_todos_los_instrumentos.png"));
        }

        // channel view: instrument + effects on top, reorder by moving, automate any parameter
        {
            te::AudioTrack* bassTrack = nullptr;
            for (auto* t : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*t) != nullptr) { bassTrack = t; break; }
            if (bassTrack != nullptr)
            {
                for (auto* p : Session::insertsOf (*bassTrack)) p->deleteFromParent();
                s.getSelection().selectOnly (bassTrack);
                if (mc.getViewState().trackChosen) mc.getViewState().trackChosen (*bassTrack);
                pump (300);
                for (auto id : { "saturador", "filtro-automatico", "compresor" })
                    for (auto& it : Catalog::nativeEffects())
                        if (it.builtInType == id) s.insertPlugin (*bassTrack, it);
                pump (500);
                auto& cv = mc.getChannelView();
                auto chain = Session::chainOf (*bassTrack);
                check (cv.getTrack() == bassTrack && cv.numCards() == 4 && chain.size() == 4 && chain[0]->isSynth(),
                       "vista de canal: instrumento y 3 efectos (" + String (cv.numCards()) + " tarjetas)");
                // compressor to the front of the effects (it must stay after the instrument)
                s.movePlugin (*bassTrack, *chain[3], 0);
                chain = Session::chainOf (*bassTrack);
                check (chain[0]->isSynth() && chain[1]->getName() == "Compresor" && chain[3]->getName().startsWith ("Filtro"),
                       "reordenar: el compresor pasa al frente y el instrumento sigue primero: " + chain[1]->getName());
                int params = 0;
                for (auto* p : chain[1]->getAutomatableParameters()) { ignoreUnused (p); ++params; }
                check (params > 15, "el compresor nativo expone " + String (params) + " parámetros automatizables"_u);
                if (auto* thresh = [&]() -> te::AutomatableParameter* { for (auto* p : chain[1]->getAutomatableParameters()) if (p->paramID == "thresh") return p; return nullptr; }())
                {
                    mc.getViewState().automateParameter (*bassTrack, *thresh);
                    thresh->getCurve().addPoint (te::TimePosition::fromSeconds (0.0), thresh->valueRange.start, 0.0f);
                    thresh->getCurve().addPoint (te::TimePosition::fromSeconds (4.0), thresh->valueRange.end, 0.0f);
                    pump (400);
                    check (ui::AutoLanes::keys (*bassTrack).contains (ui::AutoLanes::keyOf (*thresh)), "automatizar el umbral del compresor abre su carril");
                }
                mc.showBottom (ui::MainComponent::Bottom::channel);
                pump (400);
                shot (mc, shots.getChildFile ("11_canal_instrumento_y_efectos.png"));
                {
                    // the wheel over a card scrolls the row of cards: no knob takes the wheel for itself
                    int knobs = 0, takeWheel = 0;
                    for (auto* sl : findChildren<Slider> (cv)) { ++knobs; if (sl->isScrollWheelEnabled()) ++takeWheel; }
                    check (knobs > 0 && takeWheel == 0, "la rueda sobre las tarjetas desplaza la fila: "_u + String (knobs) + " perillas, " + String (takeWheel) + " se quedan la rueda");
                }
            }
        }

        // modulators, effect rack and external effect
        {
            te::AudioTrack* bassTrack = nullptr;
            for (auto* t : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*t) != nullptr) { bassTrack = t; break; }
            auto item = [] (const char* id) { for (auto& it : Catalog::nativeEffects()) if (it.builtInType == id) return it; return CatalogItem(); };
            if (bassTrack != nullptr)
            {
                auto& cv = mc.getChannelView();
                s.insertItem (*bassTrack, item ("lfo"));
                pump (400);
                auto mods = devices::modulatorsOf (*bassTrack);
                check (mods.size() == 1 && cv.numModulatorCards() == 1, "LFO en el canal: una tarjeta de modulador ("_u + String (cv.numModulatorCards()) + ")");
                te::AutomatableParameter* cutoff = nullptr;
                for (auto* p : Session::chainOf (*bassTrack))
                    for (auto* prm : p->getAutomatableParameters()) if (prm->paramID == "cutoff") cutoff = prm;
                if (! mods.isEmpty() && cutoff != nullptr)
                {
                    devices::map (*mods[0], *cutoff, 0.5f);
                    check (devices::targetsOf (*mods[0], *bassTrack).contains (cutoff) && std::abs (devices::amountFor (*mods[0], *cutoff) - 0.5f) < 1e-6f,
                           "mapear: el LFO mueve el corte del filtro al 50 %");
                    auto lfoRate = devices::modulatorKnobs (*mods[0]);
                    check (lfoRate.size() >= 5, "el LFO muestra sus perillas: " + String (lfoRate.size()));
                    if (! lfoRate.isEmpty())
                        check (ui::AutoLanes::find (*bassTrack, ui::AutoLanes::keyOf (*lfoRate[0])) == lfoRate[0], "las perillas del LFO también se automatizan");
                }
                // effect rack with two chains
                auto rackP = s.insertItem (*bassTrack, item ("rack"));
                pump (300);
                check (rackP != nullptr && devices::isFxRack (*rackP) && Session::chainOf (*bassTrack).contains (rackP.get()), "rack de efectos en la cadena del canal");
                check (Session::instrumentOf (*bassTrack) != nullptr && Session::chainOf (*bassTrack)[0] == Session::instrumentOf (*bassTrack)
                         && Session::chainOf (*bassTrack).getLast() == rackP.get(),
                       "el rack va después de los efectos y el instrumento sigue primero"_u);
                if (rackP != nullptr)
                    if (auto* rt = devices::rackOf (*rackP))
                    {
                        devices::addToChain (*rt, 0, Catalog::create (s.getEdit(), item ("eco")));
                        devices::addChain (*rt);
                        devices::addToChain (*rt, 1, Catalog::create (s.getEdit(), item ("chorus")));
                        devices::addToChain (*rt, 1, Catalog::create (s.getEdit(), item ("reductor")), 0);
                        auto c0 = devices::chain (*rt, 0), c1 = devices::chain (*rt, 1);
                        check (devices::numChains (*rt) == 2 && c0.size() == 1 && c1.size() == 2 && c1[0]->getName() == "Reductor",
                               "rack: dos cadenas en paralelo, la segunda con reductor y chorus en orden");
                        // every chain ends at the rack outputs
                        int toOut = 0;
                        for (auto* c : rt->getConnections()) if (c->destID.get().isInvalid() && c->destPin.get() > 0) ++toOut;
                        check (toOut == 4, "rack: cada cadena llega a las dos salidas (" + String (toOut) + " conexiones)");
                        auto macros = devices::macros (*rackP);
                        check (macros.size() == 8, "rack: ocho macros automatizables");
                        te::AutomatableParameter* fb = nullptr;
                        for (auto* prm : c0[0]->getAutomatableParameters()) if (prm->paramID == "feedback") fb = prm;
                        if (fb != nullptr && ! macros.isEmpty())
                        {
                            devices::mapToMacro (*macros[0], *fb, 1.0f);
                            check (devices::mappedToMacro (*macros[0], *rackP).contains (fb), "la macro 1 mueve las repeticiones del eco");
                            check (ui::AutoLanes::find (*bassTrack, ui::AutoLanes::keyOf (*fb)) == fb, "una perilla dentro del rack se puede automatizar");
                        }
                        devices::moveInChain (*rt, *c1[1], 0);
                        check (devices::chain (*rt, 1)[0]->getName().startsWith ("Chorus"), "mover dentro de la cadena: el chorus pasa primero");
                        // the rack editor (in the app it opens in its own window; Xvfb has no window manager for it)
                        {
                            ui::RackEditor ed (mc.getViewState(), *bassTrack, *rackP);
                            ed.setSize (1100, 620);
                            pump (300);
                            check (ed.numChainRows() == 2, "el editor del rack muestra las dos cadenas");
                            shot (ed, shots.getChildFile ("13_rack_de_efectos.png"));
                        }
                    }
                // external effect
                auto ext = s.insertItem (*bassTrack, item ("efecto-externo"));
                check (dynamic_cast<te::InsertPlugin*> (ext.get()) != nullptr, "efecto externo: un envío y retorno por la interfaz");
                mc.showBottom (ui::MainComponent::Bottom::channel);
                pump (600);
                shot (mc, shots.getChildFile ("12_canal_con_modulador_y_rack.png"));
                if (cv.numModulatorCards() > 0) shot (*cv.modulatorCard (0), shots.getChildFile ("14_tarjeta_lfo.png"));
            }
        }

        // external effect: the delay of a loop through the interface is found from the clicks
        {
            AudioBuffer<float> sent (1, 16000), rec (1, 64000);
            sent.clear(); rec.clear();
            int at = 200, gap = 600;
            while (at < 15000) { sent.setSample (0, at, 0.9f); sent.setSample (0, at + 1, -0.9f); at += gap; gap += 97; }
            Random noise (7);
            for (int i = 0; i < rec.getNumSamples(); ++i) rec.setSample (0, i, (noise.nextFloat() - 0.5f) * 0.02f);
            for (int i = 0; i < 16000; ++i) rec.addSample (0, i + 731, sent.getSample (0, i) * 0.5f);
            check (devices::findDelay (sent, rec, 48000) == 731, "efecto externo: encuentra un retraso de 731 muestras en la vuelta por la interfaz");
            AudioBuffer<float> nothing (1, 64000);
            for (int i = 0; i < nothing.getNumSamples(); ++i) nothing.setSample (0, i, (noise.nextFloat() - 0.5f) * 0.02f);
            check (devices::findDelay (sent, nothing, 48000) < 0, "si el cable no regresa, lo dice en vez de inventar un número");
        }
        // Forma: its curve is edited with the mouse on its card
        {
            te::AudioTrack* t = nullptr;
            for (auto* x : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*x) != nullptr) { t = x; break; }
            if (t != nullptr)
            {
                auto m = devices::addModulator (*t, "forma");
                auto& cv = mc.getChannelView();
                cv.showTrack (t);
                pump (300);
                ui::ModulatorCard* card = nullptr;
                for (int i = 0; i < cv.numModulatorCards(); ++i) if (cv.modulatorCard (i)->modifier == m) card = cv.modulatorCard (i);
                Component* editor = nullptr;
                if (card != nullptr) for (auto* c : card->getChildren()) if (auto* tc = dynamic_cast<SettableTooltipClient*> (c); tc != nullptr && tc->getTooltip().containsIgnoreCase ("Doble clic")) editor = c;
                check (editor != nullptr, "Forma muestra su curva para dibujarla con el ratón");
                if (auto* bp = dynamic_cast<te::BreakpointOscillatorModifier*> (m.get()); bp != nullptr && editor != nullptr)
                {
                    bp->numActivePointsParam->setParameter (2.0f, sendNotification);
                    pump (100);
                    auto r = editor->getLocalBounds();
                    editor->mouseDoubleClick (MouseEvent (Desktop::getInstance().getMainMouseSource(), Point<float> (r.getWidth() * 0.9f, r.getHeight() * 0.2f),
                                                          ModifierKeys(), 0, 0, 0, 0, 0, editor, editor, Time::getCurrentTime(), Point<float> (r.getWidth() * 0.9f, r.getHeight() * 0.2f),
                                                          Time::getCurrentTime(), 2, false));
                    check (roundToInt (bp->numActivePointsParam->getCurrentValue()) == 3, "doble clic en la curva agrega un punto");
                    shot (*card, shots.getChildFile ("15_forma.png"));
                }
                m->remove();
            }
        }

        // the whole family is in Instruments except Hedgehog, which goes on the master
        {
            auto item = [&s] (const char* animal) { for (auto& it : Catalog::racoonItems (s.engine)) if (Catalog::animalFor (it.name) == animal) return it; return CatalogItem(); };
            int listed = 0;
            for (auto& it : Catalog::racoonItems (s.engine)) if (Catalog::listedAsInstrument (it)) ++listed;
            check (listed == 10 && ! Catalog::listedAsInstrument (item ("Hedgehog")), "10 de la familia en Instrumentos, Hedgehog aparte: " + String (listed));
            for (auto* animal : { "Chipmunk", "Mole" })
                if (item (animal).available)
                {
                    auto* t = s.addChannelFor (item (animal));
                    check (t != nullptr && Session::instrumentOf (*t) != nullptr && Catalog::isRacoonPlugin (*Session::instrumentOf (*t), item (animal).name),
                           String (animal) + " crea su canal y es su propio instrumento");
                }
            if (item ("Skunk").available)
            {
                auto* t = s.addChannelFor (item ("Skunk"));
                check (t != nullptr && Session::instrumentOf (*t) == nullptr && ! Session::insertsOf (*t).isEmpty(), "Skunk crea un canal de audio con Skunk puesto");
            }
            if (item ("Hedgehog").available)
            {
                auto p = s.insertOnMaster (item ("Hedgehog"));
                bool onMaster = false;
                for (auto* x : s.getEdit().getMasterPluginList().getPlugins()) onMaster = onMaster || x == p.get();
                check (p != nullptr && onMaster, "Hedgehog va al Master");
            }
            pump (300);
        }

        // hold and drag a channel to another place
        {
            auto& av = mc.getArrangement();
            auto headers = findChildren<ui::TrackHeader> (av);
            // in screen order (rows are reused, so the child order is not the display order)
            std::sort (headers.begin(), headers.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
            if (headers.size() >= 3)
            {
                auto first = headers[0]->track;
                const int y = av.getLocalPoint (headers[2], Point<int> (0, headers[2]->getHeight() - 4)).y;
                av.moveTrackByDrag (*first, y);
                pump (300);
                auto now = ui::visibleTracks (s.getEdit());
                check (now.size() >= 3 && now[2].track == first.get(), "arrastrar un canal lo mueve: el primero queda tercero");
                s.getEdit().undo();
                pump (200);
            }
        }

        // the channel fader in the song: a click anywhere puts it under the pointer, a drag follows the mouse
        {
            auto& av = mc.getArrangement();
            auto headers = findChildren<ui::TrackHeader> (av);
            std::sort (headers.begin(), headers.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
            ui::FaderSlider* f = headers.isEmpty() ? nullptr : findChild<ui::FaderSlider> (*headers[0]);
            te::VolumeAndPanPlugin* vp = nullptr;
            if (! headers.isEmpty()) if (auto* at = dynamic_cast<te::AudioTrack*> (headers[0]->track.get())) vp = at->getVolumePlugin();
            if (f != nullptr && vp != nullptr && f->getWidth() > 40)
            {
                const int y = f->getHeight() / 2;
                const int x1 = f->getWidth() / 5, x2 = f->getWidth() * 4 / 5;
                click (*f, { x1, y });
                pump (60);
                const float at1 = f->getPositionOfValue (f->getValue());
                check (std::abs (at1 - (float) x1) <= 3.0f && std::abs (vp->getSliderPos() - (float) f->getValue()) < 1.0e-4f,
                       "fader del canal: un clic lejos de la perilla lo lleva al puntero (" + String (at1, 1) + " px, clic en " + String (x1) + ")");
                drag (*f, { x1, y }, { x2, y });
                pump (200);   // the song refreshes its headers in between: the fader must not jump back
                const float at2 = f->getPositionOfValue (f->getValue());
                check (std::abs (at2 - (float) x2) <= 3.0f && std::abs (vp->getSliderPos() - (float) f->getValue()) < 1.0e-4f,
                       "fader del canal: al arrastrar sigue al puntero uno a uno (" + String (at2, 1) + " px, puntero en " + String (x2) + ")");
                f->setValue (f->getDoubleClickReturnValue(), sendNotificationSync);
            }
            else check (false, "el canal tiene su fader de volumen");
            auto mixFaders = findChildren<ui::Fader> (mc);
            check (mixFaders.isEmpty() || mixFaders[0]->getSliderSnapsToMousePosition(), "los faders del mezclador también van al puntero");

            // the minutes and seconds under the tracks
            auto* tr = findChild<ui::TimeRuler> (av);
            check (tr != nullptr && tr->isVisible() && tr->getBottom() >= av.getHeight() - 40 && tr->getWidth() > 200,
                   "debajo de la maqueta hay una regla de minutos y segundos");
            check (ui::TimeRuler::format (75.0, 5.0) == "1:15" && ui::TimeRuler::format (601.0, 60.0) == "10:01" && ui::TimeRuler::format (1.5, 0.5) == "0:01.5",
                   "la regla escribe 1:15, 10:01 y 0:01.5");
            check (ui::TimeRuler::stepFor (100.0) == 1.0 && ui::TimeRuler::stepFor (2.0) == 60.0 && ui::TimeRuler::stepFor (1000.0) == 0.1,
                   "las marcas se espacian con el zoom: cada segundo, cada minuto, cada décima"_u);
            if (tr != nullptr)
            {
                click (*tr, { tr->getWidth() / 2, tr->getHeight() / 2 });
                pump (80);
                const double p1 = s.getTransport().getPosition().inSeconds();
                click (*tr, { tr->getWidth() / 4, tr->getHeight() / 2 });
                pump (80);
                const double p2 = s.getTransport().getPosition().inSeconds();
                check (p1 > 0.5 && p2 > 0.0 && p2 < p1, "clic en la regla de tiempo mueve el cursor (" + String (p1, 2) + " s y " + String (p2, 2) + " s)");
                shot (av, shots.getChildFile ("16_regla_de_segundos.png"));
            }
        }

        // Canal: the volume as a number, the icon in the corner, the wheel never moves a fader, the Sidechain card
        {
            te::AudioTrack* t = nullptr;
            for (auto* x : te::getAudioTracks (s.getEdit()))
                if (auto* inst = Session::instrumentOf (*x); inst != nullptr && Catalog::animalFor (inst->getName()) == "Badger") { t = x; break; }
            if (t == nullptr) for (auto* x : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*x) != nullptr) { t = x; break; }
            if (t != nullptr)
            {
                mc.showBottom (ui::MainComponent::Bottom::channel);
                auto& cv = mc.getChannelView();
                cv.showTrack (t);
                pump (200);
                auto& vb = cv.volumeBox;
                auto* vp = t->getVolumePlugin();
                check (vb.isVisible() && vb.getWidth() > 150 && vp != nullptr, "Canal muestra el volumen del canal con fader y número");
                if (vp != nullptr)
                {
                    vb.number.setText ("-6", sendNotificationSync);
                    pump (80);
                    check (std::abs (ui::volumeFaderToDb (vp->getSliderPos()) + 6.0f) < 0.05f && vb.number.getText() == "-6.0 dB",
                           "escribir -6 en el número pone el canal a -6 dB (" + vb.number.getText() + ")");
                    vp->setSliderPos (ui::dbToVolumeFader (-12.5f));
                    pump (200);
                    check (vb.number.getText() == "-12.5 dB" && std::abs (ui::volumeFaderToDb ((float) vb.fader.getValue()) + 12.5f) < 0.05f,
                           "mover el fader en la maqueta cambia el número en Canal (" + vb.number.getText() + ")");
                    vb.number.setText ("hola", sendNotificationSync);
                    pump (40);
                    check (vb.number.getText() == "-12.5 dB", "si escribes algo que no es número, vuelve al valor que tenía");
                    check (ui::VolumeBox::parseDb ("-inf").value_or (0.0f) <= -99.0f && ui::VolumeBox::parseDb ("+3 dB").value_or (0.0f) == 3.0f
                           && ui::VolumeBox::parseDb ("-4,5").value_or (0.0f) == -4.5f, "entiende -inf, +3 dB y -4,5");
                    vp->setSliderPos (ui::dbToVolumeFader (0.0f));
                }
                // the icon of the instrument in the corner of the panel
                pump (200);
                check (! mc.instrumentIconBounds().isEmpty() && mc.instrumentIconName() == Session::instrumentOf (*t)->getName(),
                       "en la esquina del panel va el ícono de " + mc.instrumentIconName());
                shot (mc, shots.getChildFile ("20_canal_volumen_e_icono.png"));
                // the wheel over a fader scrolls, it does not move the volume
                auto headers = findChildren<ui::TrackHeader> (mc.getArrangement());
                if (! headers.isEmpty())
                    if (auto* f = findChild<ui::FaderSlider> (*headers[0]))
                    {
                        const double before = f->getValue();
                        MouseWheelDetails w { 0.0f, 0.4f, false, false, false };
                        auto me = MouseEvent (Desktop::getInstance().getMainMouseSource(), Point<float> (20.0f, 5.0f), ModifierKeys(), 0, 0, 0, 0, 0,
                                              f, f, Time::getCurrentTime(), Point<float> (20.0f, 5.0f), Time::getCurrentTime(), 0, false);
                        for (int i = 0; i < 6; ++i) f->mouseWheelMove (me, w);
                        auto mixFaders = findChildren<ui::Fader> (mc);
                        bool mixerStill = true;
                        for (auto* mf : mixFaders) { const double v = mf->getValue(); mf->mouseWheelMove (me, w); mixerStill = mixerStill && mf->getValue() == v; }
                        check (f->getValue() == before && mixerStill, "la rueda del mouse sobre un fader no le mueve el volumen");
                    }
                // a Sidechain on this channel listens to the drums by itself; its card says to whom and lets you change it
                CatalogItem sc;
                for (auto& it : Catalog::nativeEffects()) if (it.name == "Sidechain") sc = it;
                auto drums = s.guessKickTrack (*t);
                auto p = s.insertPlugin (*t, sc);
                pump (400);
                ui::DeviceCard* card = nullptr;
                for (int i = 0; i < cv.numCards(); ++i) if (cv.card (i)->plugin == p) card = cv.card (i);
                check (p != nullptr && drums != nullptr && card != nullptr && card->listenTo != nullptr && card->listenTo->getText().contains (drums->getName()),
                       "la tarjeta del Sidechain dice a quién escucha: " + (card != nullptr && card->listenTo != nullptr ? card->listenTo->getText() : String ("nada")));
                if (card != nullptr && card->listenTo != nullptr)
                {
                    card->listenTo->setSelectedId (1, sendNotificationSync);
                    check (p->getSidechainSourceID() == te::EditItemID(), "elegir «nadie» lo desconecta");
                    if (drums != nullptr)
                        for (int i = 0; i < card->listenTracks.size(); ++i)
                            if (card->listenTracks[i] == drums) card->listenTo->setSelectedId (i + 2, sendNotificationSync);
                    check (drums != nullptr && p->getSidechainSourceID() == drums->itemID, "y elegir la batería lo vuelve a conectar");
                    shot (cv, shots.getChildFile ("21_tarjeta_sidechain.png"));
                }
                if (p != nullptr) { p->showWindowExplicitly(); pump (300); p->hideWindowForShutdown(); pump (60); p->deleteFromParent(); pump (200); }
            }
            else check (false, "un canal con instrumento para probar Canal");
        }

        // Piano: quantize with strength and swing, velocity tools, the scale of a detected key
        {
            te::AudioTrack* t = nullptr;
            for (auto* x : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*x) != nullptr) { t = x; break; }
            te::MidiClip* c = t != nullptr ? s.createMidiClip (*t, { te::BeatPosition::fromBeats (64.0), te::BeatPosition::fromBeats (72.0) }, "cuantizar") : nullptr;
            if (c != nullptr)
            {
                mc.showBottom (ui::MainComponent::Bottom::editor);
                auto& pr = mc.getPianoRoll();
                pr.setClip (c);
                pump (150);
                auto* n1 = c->getSequence().addNote (57, te::BeatPosition::fromBeats (0.1), te::BeatDuration::fromBeats (0.4), 90, 0, nullptr);
                auto* n2 = c->getSequence().addNote (60, te::BeatPosition::fromBeats (1.1), te::BeatDuration::fromBeats (0.4), 90, 0, nullptr);
                auto* n3 = c->getSequence().addNote (64, te::BeatPosition::fromBeats (2.1), te::BeatDuration::fromBeats (0.4), 90, 0, nullptr);
                ui::prefs().setValue ("qStrength", 50.0);
                ui::prefs().setValue ("qSwing", 0.0);
                ui::prefs().setValue ("qRandom", 0.0);
                pr.setGridIndex (6);    // 1/16
                pr.getSelection().clearQuick();
                pr.quantiseSelected();
                check (std::abs (n1->getStartBeat().inBeats() - 0.05) < 1.0e-6 && std::abs (n3->getStartBeat().inBeats() - 2.05) < 1.0e-6,
                       "Piano: cuantizar con fuerza 50 % deja las notas a medio camino (" + String (n1->getStartBeat().inBeats(), 3) + ")");
                ui::prefs().setValue ("qStrength", 100.0);
                pr.showQuantizeOptions();
                CallOutBox* box = nullptr;
                for (int i = 0; i < 20 && box == nullptr; ++i) { pump (100); box = findChild<CallOutBox> (mc); }
                check (box != nullptr, "el botón ... abre las opciones de cuantizar (fuerza, swing, azar)");
                if (box != nullptr) { shot (*box, shots.getChildFile ("17_opciones_de_cuantizar.png")); box->dismiss(); pump (100); }
                pr.velocityTool (0);
                check (n1->getVelocity() < n2->getVelocity() && n2->getVelocity() < n3->getVelocity(), "Piano: crescendo de velocidades ("
                       + String (n1->getVelocity()) + ", " + String (n2->getVelocity()) + ", " + String (n3->getVelocity()) + ")");
                pr.velocityTool (6);
                check (n3->getVelocity() - n1->getVelocity() < 50, "Piano: emparejar acerca las velocidades");
                ui::showKeyOf (mc.getViewState(), *c);
                pump (60);
                check (pr.scaleText().startsWith ("La") && pr.scaleText().containsIgnoreCase ("menor"), "detectar la tonalidad de La-Do-Mi enciende la escala del Piano (" + pr.scaleText() + ")");
                shot (mc, shots.getChildFile ("18_piano_herramientas.png"));
                pr.setClip (nullptr);
                s.deleteClip (*c);
                pump (100);
            }
            else check (false, "un clip para probar el Piano");
        }

        // fades drawn with their curve
        {
            te::AudioTrack* t = nullptr;
            for (auto* x : te::getAudioTracks (s.getEdit())) if (Session::instrumentOf (*x) == nullptr && kindOf (*x) == TrackKind::audio) { t = x; break; }
            if (t == nullptr) t = s.addAudioTrack ("Fundidos");
            auto f = out.getChildFile ("fundido.wav");
            {
                AudioBuffer<float> b (2, 48000 * 4);
                for (int i = 0; i < b.getNumSamples(); ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, 0.5f * (float) std::sin (i * 0.05));
                audio::writeWav (f, b, 48000.0);
            }
            if (auto* wc = t != nullptr ? s.addAudioFile (*t, f, 0_tp) : nullptr)
            {
                wc->setFadeIn (te::TimeDuration::fromSeconds (1.5));
                wc->setFadeOut (te::TimeDuration::fromSeconds (1.5));
                wc->setFadeInType (te::AudioFadeCurve::sCurve);
                wc->setFadeOutType (te::AudioFadeCurve::convex);
                pump (300);
                ui::ClipComponent* cc = nullptr;
                for (auto* x : findChildren<ui::ClipComponent> (mc.getArrangement())) if (x->clip.get() == wc) cc = x;
                check (cc != nullptr && wc->getFadeInType() == te::AudioFadeCurve::sCurve, "el clip guarda la curva de cada fundido");
                if (cc != nullptr) shot (*cc, shots.getChildFile ("19_fundidos_en_s_y_convexo.png"));
                s.deleteClip (*wc);
            }
        }

        // ---------------------------------------------------- the kit in two channels, FIJAR, ALEATORIA on every channel, no wearing down
        {
            s.newProject (true, false, findGenre ("techhouse"));
            pump (600);
            auto& roll = mc.getPianoRoll();
            te::MidiClip *kickC = nullptr, *grooveC = nullptr, *bassC = nullptr, *gtrC = nullptr;
            for (auto* t : te::getAudioTracks (s.getEdit()))
                for (auto* c : t->getClips())
                    if (auto* m = dynamic_cast<te::MidiClip*> (c))
                    {
                        if (t->getName().contains ("Kick")) kickC = m;
                        if (t->getName().contains ("Groove")) grooveC = m;
                        if (t->getName().contains ("Badger")) bassC = m;
                        if (t->getName().contains ("Ferret")) gtrC = m;
                    }
            bool kickOnly = kickC != nullptr && kickC->getSequence().getNumNotes() > 8, grooveNoKick = grooveC != nullptr && grooveC->getSequence().getNumNotes() > 8;
            if (kickC != nullptr) for (auto* n : kickC->getSequence().getNotes()) kickOnly = kickOnly && (n->getNoteNumber() == 36 || n->getNoteNumber() == 35);
            if (grooveC != nullptr) for (auto* n : grooveC->getSequence().getNotes()) grooveNoKick = grooveNoKick && n->getNoteNumber() != 36 && n->getNoteNumber() != 35;
            check (kickOnly && grooveNoKick, "la batería viene en dos canales: Hamster · Kick (solo bombo) y Hamster · Groove (todo lo demás), mismo kit"_u);
            const auto kitName = [] (te::MidiClip* m) { if (auto* t = dynamic_cast<te::AudioTrack*> (m->getTrack())) if (auto* i = Session::instrumentOf (*t)) return i->getName(); return String(); };
            check (kickC != nullptr && grooveC != nullptr && kitName (kickC) == kitName (grooveC), "los dos tocan con Hamster: " + (kickC != nullptr ? kitName (kickC) : String()));

            // pressing ALEATORIA 15 times: 15 grooves, not the same one moved a little, and the kit's sounds change too
            if (grooveC != nullptr)
            {
                roll.setClip (grooveC);
                pump (40);
                const int n0 = grooveC->getSequence().getNumNotes();
                auto* gt = dynamic_cast<te::AudioTrack*> (grooveC->getTrack());
                auto kitState = [&] () -> String
                {
                    if (gt != nullptr) if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*gt)))
                        if (auto* pi = ext->getAudioPluginInstance()) return Session::pluginStateXml (*pi);
                    return {};
                };
                std::set<String> patterns;
                int lowest = 1 << 20, highest = 0, soundChanges = 0;
                String last = kitState();
                for (int k = 0; k < 15; ++k)
                {
                    roll.generateRandom (std::nullopt, 900 + k);
                    pump (30);
                    String sig;
                    for (auto* n : grooveC->getSequence().getNotes()) sig << n->getNoteNumber() << "@" << String (n->getStartBeat().inBeats(), 3) << " ";
                    patterns.insert (String::toHexString (sig.hashCode64()));
                    const int n = grooveC->getSequence().getNumNotes();
                    lowest = jmin (lowest, n); highest = jmax (highest, n);
                    const auto now = kitState();
                    if (now != last) ++soundChanges;
                    last = now;
                }
                check (patterns.size() >= 13 && lowest > n0 / 4, "15 veces ALEATORIA en el Groove: " + String ((int) patterns.size()) + " patrones distintos, de "
                       + String (lowest) + " a " + String (highest) + " golpes (el original tenía " + String (n0) + ")");
                if (biblio::Catalogo::get().disponible())
                    check (soundChanges >= 12, "y el kit cambia de sonidos " + String (soundChanges) + " de 15 veces (bombo, caja, palmas, charles, shaker...): "
                           + s.getEdit().state.getProperty ("racoonSounds").toString());
            }
            // one sound of the kit held: the closed hat keeps its notes, the rest of the groove moves
            if (grooveC != nullptr)
            {
                roll.setClip (grooveC);
                mc.showBottom (ui::MainComponent::Bottom::editor);
                pump (60);
                auto rowSig = [] (te::MidiClip* m, int pitch, bool only)
                {
                    String k;
                    for (auto* n : m->getSequence().getNotes())
                        if ((n->getNoteNumber() == pitch) == only) k << n->getNoteNumber() << "@" << String (n->getStartBeat().inBeats(), 3) << ":" << n->getVelocity() << " ";
                    return k;
                };
                // a groove with closed hats to hold (some house grooves only have the open hat off the beat)
                for (int k = 0; k < 20 && rowSig (grooveC, 42, true).isEmpty(); ++k) { roll.generateRandom (std::nullopt, 555 + k); pump (20); }
                roll.setRowHeld (42, true);
                const auto hats = rowSig (grooveC, 42, true), rest = rowSig (grooveC, 42, false);
                bool hatSoundKept = true;
                for (int k = 0; k < 6; ++k)
                {
                    roll.generateRandom (std::nullopt, 31337 + k);
                    pump (30);
                    hatSoundKept = hatSoundKept && ! s.getEdit().state.getProperty ("racoonSounds").toString().containsIgnoreCase ("closed hat");
                }
                check (rowSig (grooveC, 42, true) == hats && rowSig (grooveC, 42, false) != rest && hats.isNotEmpty() && hatSoundKept,
                       "fijar un sonido: el charles cerrado se queda con su patrón y su sonido, el resto del groove cambia ("
                       + String ((int) (rowSig (grooveC, 42, true) == hats)) + String ((int) (rowSig (grooveC, 42, false) != rest)) + String ((int) hats.isNotEmpty())
                       + String ((int) hatSoundKept) + " " + s.getEdit().state.getProperty ("racoonSounds").toString() + ")");
                shot (mc, shots.getChildFile ("27_charles_fijado.png"));
                mc.invokeDirectly (ui::MainComponent::editRandomAll, false);
                pump (100);
                check (rowSig (grooveC, 42, true) == hats, "ALEATORIA en todos los canales también respeta el sonido fijado");
                roll.setClip (grooveC);
                roll.setRowHeld (42, false);
            }
            // a fresh pattern keeps where the guitar waits in the block (it comes in later): the same bars sound
            if (gtrC != nullptr)
            {
                roll.setClip (gtrC);
                pump (40);
                std::set<int> was;
                for (auto* n : gtrC->getSequence().getNotes()) was.insert ((int) (n->getStartBeat().inBeats() / 4.0));
                roll.generateRandom (std::nullopt, 77, true);
                std::set<int> bars;
                for (auto* n : gtrC->getSequence().getNotes()) bars.insert ((int) (n->getStartBeat().inBeats() / 4.0));
                check (bars == was && ! bars.empty(), "Patrón nuevo: la guitarra sigue entrando donde entraba en el bloque ("_u + String ((int) bars.size()) + " compases)");
            }
            // FIJAR: the bass is held; ALEATORIA there does nothing, and on every channel it is skipped
            if (bassC != nullptr && grooveC != nullptr)
            {
                auto sig = [] (te::MidiClip* m) { String k; for (auto* n : m->getSequence().getNotes()) k << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << " "; return k; };
                roll.setClip (bassC);
                pump (40);
                ui::PianoRoll::setHeld (*bassC, true);
                const auto bassBefore = sig (bassC);
                check (! roll.generateRandom() && sig (bassC) == bassBefore, "FIJAR: ALEATORIA no toca el bajo fijado");
                pump (100);
                shot (mc, shots.getChildFile ("26_clip_fijado.png"));
                const auto grooveBefore = sig (grooveC);
                // the sound of each channel: what its Racoon instrument would save
                auto soundOf = [] (te::MidiClip* m) -> String
                {
                    if (auto* t = dynamic_cast<te::AudioTrack*> (m->getTrack()))
                        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t)))
                            if (auto* pi = ext->getAudioPluginInstance())
                            {
                                MemoryBlock mb; pi->getStateInformation (mb);
                                return String (mb.toBase64Encoding().hashCode64());
                            }
                    return {};
                };
                const auto bassSound = soundOf (bassC);
                const auto gtrSound = gtrC != nullptr ? soundOf (gtrC) : String();
                const auto programOf = [] (te::MidiClip* m) -> String
                {
                    if (auto* t = dynamic_cast<te::AudioTrack*> (m->getTrack()))
                        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (Session::instrumentOf (*t)))
                            if (auto* pi = ext->getAudioPluginInstance()) return pi->getProgramName (pi->getCurrentProgram());
                    return {};
                };
                const auto kitBefore = programOf (grooveC);
                mc.getViewState().clearRange();
                mc.invokeDirectly (ui::MainComponent::editRandomAll, false);
                pump (200);
                check (sig (bassC) == bassBefore && sig (grooveC) != grooveBefore, "ALEATORIA en todos los canales: varía el Groove y deja el bajo fijado");
                check (soundOf (bassC) == bassSound && (gtrC == nullptr || soundOf (gtrC) != gtrSound),
                       "ALEATORIA en todos los canales cambia los sonidos (kit " + kitBefore + " -> " + programOf (grooveC) + ") y el bajo fijado conserva el suyo");
                s.getEdit().getUndoManager().undo();
                pump (200);
                check (sig (grooveC) == grooveBefore, "un Cmd+Z regresa todos los canales");
                check ((gtrC == nullptr || soundOf (gtrC) == gtrSound) && programOf (grooveC) == kitBefore, "y el mismo Cmd+Z regresa los sonidos de antes ("_u + kitBefore + ")");
                ui::PianoRoll::setHeld (*bassC, false);
                // Editar > Mezclar y masterizar: renders, measures, corrects and leaves the song near its destination
                {
                    s.getEdit().state.removeProperty ("racoonMixAfter", nullptr);
                    mc.invokeDirectly (ui::MainComponent::editMixMaster, false);
                    pump (100);
                    const auto after = StringArray::fromTokens (s.getEdit().state.getProperty ("racoonMixAfter").toString(), "|", "");
                    const float target = (float) (double) s.getEdit().state.getProperty ("racoonMasterTarget", -14.0);
                    check (after.size() == 4 && std::abs (after[0].getFloatValue() - target) <= 1.5f && after[1].getFloatValue() <= -0.5f,
                           "Editar > Mezclar y masterizar deja la rola en su destino: "_u + after.joinIntoString (" | ") + " (meta " + String (target, 0) + " LUFS)");
                }
            }
            // a range dragged from the empty time after a clip shrinks to the clip; Cmd+D puts the copy right after it
            if (bassC != nullptr)
                if (auto* bt = dynamic_cast<te::AudioTrack*> (bassC->getTrack()))
                    if (auto* lone = s.createMidiClip (*bt, { te::BeatPosition::fromBeats (80.0), te::BeatPosition::fromBeats (88.0) }, "Suelto"))
                    {
                        lone->getSequence().addNote (45, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
                        auto& vz = mc.getViewState();
                        vz.pxPerBeat = 16.0; vz.scrollBeats = 64.0;          // bars 17 to 30 on screen
                        vz.sendChangeMessage();
                        pump (200);
                        ui::TrackLane* lane = nullptr;
                        for (auto* l : findChildren<ui::TrackLane> (mc.getArrangement())) if (l->track.get() == bt) lane = l;
                        auto& v = mc.getViewState();
                        if (lane != nullptr)
                        {
                            const int y = lane->getHeight() / 2;
                            drag (*lane, { (int) v.beatToX (100.0), y }, { (int) v.beatToX (76.0), y });
                            pump (100);
                            check (std::abs (v.rangeStart - 80.0) < 1.0e-6 && std::abs (v.rangeEnd - 88.0) < 1.0e-6,
                                   "arrastrar desde el hueco después de un clip selecciona solo el clip (" + String (v.rangeStart, 1) + " a " + String (v.rangeEnd, 1) + " tiempos)");
                            mc.invokeDirectly (ui::MainComponent::editDuplicate, false);
                            pump (100);
                            int copiesAt88 = 0, after = 0;
                            for (auto* c2 : bt->getClips())
                            {
                                const double st = v.timeToBeat (c2->getPosition().getStart());
                                if (std::abs (st - 88.0) < 1.0e-6) ++copiesAt88;
                                if (st > 88.5) ++after;
                            }
                            check (copiesAt88 == 1 && after == 0, "Cmd+D: la copia cae justo después del clip (tiempo 88), sin copiar el hueco");
                            s.getEdit().getUndoManager().undo();
                            v.clearRange();
                        }
                        else check (false, "el carril del bajo");
                    }

            // DUPLICAR notes in the Piano: the copy goes right after the notes (on the grid), not after the whole bar
            if (bassC != nullptr)
            {
                roll.setClip (bassC);
                pump (40);
                roll.setGridIndex (5);     // 1/16
                auto& sel = roll.getSelection();
                sel.clearQuick();
                te::MidiNote* a = nullptr;
                for (auto* n : bassC->getSequence().getNotes()) if (a == nullptr || n->getStartBeat() < a->getStartBeat()) a = n;
                if (a != nullptr)
                {
                    sel.add (a);
                    const double end = a->getEndBeat().inBeats(), g = roll.currentGridBeats();
                    const double expected = a->getStartBeat().inBeats() + std::ceil ((end - a->getStartBeat().inBeats()) / g - 1e-6) * g;
                    roll.duplicateSelected();
                    bool found = false;
                    for (auto* n : bassC->getSequence().getNotes())
                        if (n != a && n->getNoteNumber() == a->getNoteNumber() && std::abs (n->getStartBeat().inBeats() - expected) < 1e-6) found = true;
                    check (found, "DUPLICAR notas en el Piano: la copia cae justo después (" + String (expected, 2) + " tiempos), no un compás después");
                    s.getEdit().getUndoManager().undo();
                }
            }
        }

        // ---------------------------------------------------- ALEATORIA on every channel of a real block: a variation, not another song
        {
            s.newProject (true, false, findGenre ("melodictechno"));
            pump (600);
            mc.invokeDirectly (ui::MainComponent::viewZoomFit, false);
            pump (200);
            shot (mc, shots.getChildFile ("24_bloque_antes_de_aleatoria.png"));
            auto& roll = mc.getPianoRoll();
            int keptBars = 0, channels = 0, keptRange = 0;
            String report;
            for (auto* t : te::getAudioTracks (s.getEdit()))
                for (auto* c : t->getClips())
                    if (auto* m = dynamic_cast<te::MidiClip*> (c); m != nullptr && m->getSequence().getNumNotes() > 0)
                    {
                        auto barsOf = [] (te::MidiClip& clip)
                        {
                            std::set<int> b;
                            for (auto* n : clip.getSequence().getNotes()) b.insert ((int) (n->getStartBeat().inBeats() / 4.0 + 1.0e-9));
                            return b;
                        };
                        auto rangeOf = [] (te::MidiClip& clip)
                        {
                            int lo = 127, hi = 0;
                            for (auto* n : clip.getSequence().getNotes()) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
                            return std::make_pair (lo, hi);
                        };
                        const auto before = barsOf (*m);
                        const auto r0 = rangeOf (*m);
                        String sigBefore;
                        for (auto* n : m->getSequence().getNotes()) sigBefore << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << " ";
                        roll.setClip (m);
                        pump (40);
                        roll.generateRandom (std::nullopt, 4242 + channels);
                        pump (40);
                        const auto after = barsOf (*m);
                        const auto r1 = rangeOf (*m);
                        ++channels;
                        // a new pattern: other notes than before, in the clip's whole length
                        String sigAfter;
                        for (auto* n : m->getSequence().getNotes()) sigAfter << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << " ";
                        if (sigAfter != sigBefore) ++keptBars; else report << t->getName() << " ";
                        juce::ignoreUnused (before, after, r0, r1);
                        ++keptRange;
                    }
            check (channels >= 6 && keptBars == channels && keptRange == channels,
                   "ALEATORIA en los " + String (channels) + " canales del bloque: cada uno con un patrón nuevo " + report);
            pump (200);
            shot (mc, shots.getChildFile ("25_bloque_despues_de_aleatoria.png"));
        }

        // ---------------------------------------------------- ARMONIZAR: the Camelot wheel moves the song to another key
        {
            s.newProject (true, false, findGenre ("techno"));
            pump (600);
            int root0 = -1; bool major0 = true;
            check (s.songKey (root0, major0) && ! major0, "la rola de techno sabe su tonalidad: "_u + keyName (root0, major0) + " (" + camelot (root0, major0) + ")");
            // the notes of every channel, by channel
            auto notesOf = [&s] (bool drums)
            {
                std::vector<int> out;
                for (auto* t : te::getAudioTracks (s.getEdit()))
                {
                    auto* inst = Session::instrumentOf (*t);
                    const bool isDrum = inst != nullptr && (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole"));
                    if (isDrum != drums) continue;
                    for (auto* c : t->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*> (c))
                            for (auto* n : m->getSequence().getNotes()) out.push_back (n->getNoteNumber());
                }
                return out;
            };
            const auto pitched0 = notesOf (false), drums0 = notesOf (true);
            mc.showHarmonize();
            pump (200);
            auto* panel = mc.getHarmonizePanel();
            check (panel != nullptr && panel->isVisible() && ! panel->hasChoice(), "ARMONIZAR abre la rueda de Camelot sin ninguna tonalidad elegida");
            shot (mc, shots.getChildFile ("28_armonizar_rueda.png"));
            if (panel != nullptr)
            {
                // two semitones up, same mode: every pitched note moves +2, the drums stay
                const int up2 = (root0 + 2) % 12;
                panel->tryKey (up2, false);
                pump (100);
                auto pitched1 = notesOf (false);
                bool allUp = pitched1.size() == pitched0.size();
                for (size_t i = 0; allUp && i < pitched0.size(); ++i) allUp = pitched1[i] == pitched0[i] + 2;
                check (allUp && notesOf (true) == drums0, "al tocar "_u + camelot (up2, false) + " todo sube 2 semitonos al momento ("
                       + String ((int) pitched0.size()) + " notas) y la batería no cambia");
                shot (mc, shots.getChildFile ("29_armonizar_probando.png"));
                // to the relative major (same notes, other centre): every note lands in the new major scale
                const int rel = (root0 + 3) % 12;
                panel->tryKey (rel, true);
                pump (100);
                static const int maj[] { 0, 2, 4, 5, 7, 9, 11 };
                int outOfScale = 0;
                for (int p : notesOf (false))
                {
                    const int pc = ((p - rel) % 12 + 12) % 12;
                    bool in = false;
                    for (int k : maj) in = in || k == pc;
                    if (! in) ++outOfScale;
                }
                check (outOfScale <= (int) pitched0.size() / 20, "en "_u + camelot (rel, true) + " (mayor) las notas caen en la escala mayor ("
                       + String (outOfScale) + " fuera, las cromáticas que ya traía)");
                // Cancelar: everything as it was
                panel->cancel.triggerClick();
                pump (200);
                int r = -1; bool m = true;
                s.songKey (r, m);
                check (mc.getHarmonizePanel() == nullptr && notesOf (false) == pitched0 && r == root0 && ! m,
                       "Cancelar deja la rola en su tonalidad, nota por nota");
                // and Armonizar en esta tonalidad keeps it, as one Cmd+Z
                mc.showHarmonize();
                pump (100);
                if (auto* p2 = mc.getHarmonizePanel())
                {
                    const int down = (root0 + 10) % 12;
                    p2->tryKey (down, false);
                    p2->keep.triggerClick();
                    pump (200);
                    s.songKey (r, m);
                    const auto kept = notesOf (false);
                    bool allDown = kept.size() == pitched0.size();
                    for (size_t i = 0; allDown && i < kept.size(); ++i) allDown = kept[i] == pitched0[i] - 2;
                    check (mc.getHarmonizePanel() == nullptr && r == down && allDown, "Armonizar en "_u + camelot (down, false)
                           + ": la rola queda 2 semitonos abajo y la recuerda");
                    s.getEdit().getUndoManager().undo();
                    pump (100);
                    s.songKey (r, m);
                    check (notesOf (false) == pitched0 && r == root0, "Cmd+Z la regresa a su tonalidad");
                }
            }
        }

        // ---------------------------------------------------- TARAREO A ROLA: the panel, a hummed take and a beatbox, and the song
        {
            extern AudioBuffer<float> tarareoDePrueba (double bpm, double sr, bool beatbox);
            mc.showTarareo ("hiphop");
            pump (200);
            auto* tp = mc.getTarareoPanel();
            check (tp != nullptr && tp->isVisible() && ! tp->armar.isEnabled() && tp->genre() != nullptr && String (tp->genre()->id) == "hiphop",
                   "Tarareo a rola abre con el género elegido y sin nada que armar todavía");
            if (tp != nullptr)
            {
                const double bpm = tp->genre()->bpm;
                tp->setToma (true, tarareoDePrueba (bpm, 48000.0, false), 48000.0);
                tp->setToma (false, tarareoDePrueba (bpm, 48000.0, true), 48000.0);
                pump (100);
                check (tp->estado (true).contains ("(8A)") && tp->estado (false).contains ("8 "),
                       "al grabar dice lo que oyó: " + tp->estado (true) + " / " + tp->estado (false));
                check (tp->armar.isEnabled() && ! tp->genero.isEnabled(), "con una toma se puede armar, y el género (su tempo) queda fijo");
                shot (mc, shots.getChildFile ("29_tarareo_a_rola.png"));
                tp->armar.triggerClick();
                for (int i = 0; i < 40 && mc.getTarareoPanel() != nullptr; ++i) pump (150);
                int r = -1; bool m = true;
                s.songKey (r, m);
                int hookNotes = 0;
                for (auto* t : te::getAudioTracks (s.getEdit()))
                    if (t->getName().startsWith ("Squirrel"))
                        for (auto* c : t->getClips()) if (auto* mc2 = dynamic_cast<te::MidiClip*> (c)) hookNotes += mc2->getSequence().getNumNotes();
                check (mc.getTarareoPanel() == nullptr && r == 9 && ! m && std::abs (s.getBpm() - bpm) < 0.01 && hookNotes >= 40,
                       "Armar la rola deja un bloque de hip hop en La menor con la melodía tarareada ("_u + String (hookNotes) + " notas de Squirrel)");
                pump (300);
                shot (mc, shots.getChildFile ("29b_rola_del_tarareo.png"));
            }
        }

        // ---------------------------------------------------- MODO DJ: songs measured, the set ordered and saved, a track to mix
        {
            extern AudioBuffer<float> cancionDePrueba (double bpm, int root, bool major, double sr);
            mc.showDj();
            pump (200);
            auto* dp = mc.getDjPanel();
            check (dp != nullptr && dp->isVisible() && dp->numSongs() == 0 && ! dp->ordenar.isEnabled(), "Modo DJ abre vacío, esperando rolas");
            if (dp != nullptr)
            {
                const auto f1 = out.getChildFile ("dj uno.wav"), f2 = out.getChildFile ("dj dos.wav");
                audio::writeWav (f1, cancionDePrueba (124.0, 9, false, 44100.0), 44100.0, 16);
                audio::writeWav (f2, cancionDePrueba (126.0, 4, false, 44100.0), 44100.0, 16);
                StringArray dropped { f1.getFullPathName(), f2.getFullPathName() };
                check (dp->isInterestedInFileDrag (dropped), "Modo DJ acepta rolas arrastradas");
                dp->filesDropped (dropped, 10, 10);
                for (int i = 0; i < 100 && dp->busy(); ++i) pump (100);
                pump (100);
                check (! dp->busy() && dp->numSongs() == 2 && std::abs (dp->song (0).bpm - 124.0) < 0.06 && dp->song (0).camelot() == "8A"
                       && std::abs (dp->song (1).bpm - 126.0) < 0.06 && dp->song (1).camelot() == "9A",
                       "mide las dos rolas: " + dp->song (0).resumen() + " / " + dp->song (1).resumen());
                dj::Analisis extra;
                extra.titulo = "Otra"; extra.ok = true; extra.bpm = 128.0; extra.key = 11; extra.major = false; extra.energia = 0.9;
                dp->addAnalysis (extra);
                dp->sortSet();
                pump (100);
                shot (mc, shots.getChildFile ("30_modo_dj.png"));
                check (dp->song (0).camelot() == "8A" && dp->song (1).camelot() == "9A" && dp->song (2).camelot() == "10A",
                       "Ordenar el set: 8A, 9A, 10A, con la energía subiendo");
                const auto m3u = out.getChildFile ("set prueba.m3u");
                check (dp->saveSet (m3u) && m3u.loadFileAsString().contains (f1.getFullPathName()) && m3u.withFileExtension ("txt").loadFileAsString().contains ("8A"),
                       "Guardar el set escribe la lista .m3u y las notas del set");
                dp->selected = 1;
                dp->buildFor (1);
                for (int i = 0; i < 60 && mc.getDjPanel() != nullptr; ++i) pump (150);
                int r = -1; bool m = true;
                s.songKey (r, m);
                check (mc.getDjPanel() == nullptr && std::abs (s.getBpm() - 126.0) < 0.06 && r == 4 && ! m,
                       "Armar una pista para mezclar con la segunda: a su tempo y en Mi menor ("_u + String (s.getBpm(), 2) + " BPM)");
                pump (300);
                shot (mc, shots.getChildFile ("30b_pista_dj.png"));
                // the new block's sounds can be rated on the status bar
                pump (800);
                const bool shown = mc.likeBtn.isVisible() && mc.dislikeBtn.isVisible();
                mc.dislikeBtn.triggerClick();
                pump (800);
                check (shown && ! mc.likeBtn.isVisible() && (bool) s.getEdit().state.getProperty ("racoonCalificado"),
                       "después de armar, la barra de abajo pregunta Me gusta / No me gusta, y al contestar se quita");
            }
        }

        // ---------------------------------------------------- ALEATORIA keeps the block's structure: the intro, the break
        // and the channels that wait their turn stay silent where they were, click after click
        {
            s.newProject (true, false, findGenre ("techhouse"));
            pump (400);
            mc.getViewState().clearRange();
            auto shapeOf = [&s] ()
            {
                // per channel, the bars with notes, and for the Kick the bars with a kick
                std::map<String, String> out;
                for (auto* t : te::getAudioTracks (s.getEdit()))
                    for (auto* c : t->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*> (c))
                        {
                            String bars = String::repeatedString ("-", 16);
                            for (auto* n : m->getSequence().getNotes())
                            {
                                // a bar sounds when a note starts in it or rings through it (a pad held two bars)
                                const double a = n->getStartBeat().inBeats(), e = a + n->getLengthBeats().inBeats();
                                for (int b = (int) (a / 4.0); b < 16 && b * 4.0 < e - 0.01; ++b)
                                    if (b >= 0) bars = bars.replaceSection (b, 1, "x");
                            }
                            out[t->getName()] = bars;
                        }
                return out;
            };
            auto sigAll = [&s] ()
            {
                String k;
                for (auto* t : te::getAudioTracks (s.getEdit()))
                    for (auto* c : t->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*> (c))
                            for (auto* n : m->getSequence().getNotes()) k << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << ";";
                return k.hashCode64();
            };
            const auto before = shapeOf();
            int silentBars = 0;
            for (auto& [name, bars] : before) for (auto ch : bars) silentBars += ch == '-' ? 1 : 0;
            bool kept = true, changed = true, filled = true;
            String why;
            // Skunk's texture and Porcupine's choir change on every click too (they used to repeat after the first)
            auto sigOf = [&s] (const char* who)
            {
                String k;
                for (auto* t : te::getAudioTracks (s.getEdit()))
                    if (t->getName().startsWith (who))
                        for (auto* c : t->getClips())
                            if (auto* m = dynamic_cast<te::MidiClip*> (c))
                                for (auto* n : m->getSequence().getNotes()) k << n->getNoteNumber() << "@" << n->getStartBeat().inBeats() << ";";
                return k;
            };
            std::set<String> skunks { sigOf ("Skunk") }, choirs { sigOf ("Porcupine") };
            for (int click = 0; click < 4; ++click)
            {
                const auto sig = sigAll();
                mc.invokeDirectly (ui::MainComponent::editRandomAll, false);
                pump (300);
                changed = changed && sigAll() != sig;
                skunks.insert (sigOf ("Skunk"));
                choirs.insert (sigOf ("Porcupine"));
                const auto after = shapeOf();
                for (auto& [name, bars] : before)
                {
                    const auto& a = after.count (name) ? after.at (name) : String();
                    for (int b = 0; b < 16 && a.length() == 16; ++b)
                    {
                        if (bars[b] == '-' && a[b] != '-') { kept = false; why << name << " compás " << (b + 1) << "; "; }
                    }
                    // the last phrase plays as it did: the channels that sounded there still sound
                    if (bars.substring (12) == "xxxx" && a.substring (12).containsChar ('-')) { filled = false; why << name << " calló al final; "; }
                }
            }
            check (silentBars > 20 && kept && changed && filled,
                   "ALEATORIA respeta la estructura del bloque: los " + String (silentBars) + String::fromUTF8 (" compases callados (intro, break, canales que esperan) siguen callados tras 4 clics, y cada clic cambia la rola ") + why);
            int kicksInBreak = 0;
            for (auto* t : te::getAudioTracks (s.getEdit()))
                if (t->getName().startsWith (String::fromUTF8 ("Hamster")))
                    for (auto* c : t->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*> (c))
                            for (auto* n : m->getSequence().getNotes())
                                if ((n->getNoteNumber() == 36 || n->getNoteNumber() == 35) && n->getStartBeat().inBeats() >= 32.0 && n->getStartBeat().inBeats() < 48.0) ++kicksInBreak;
            check (kicksInBreak == 0, "y el break sigue sin bombo (" + String (kicksInBreak) + " bombos en los compases 9 a 12)");
            check (skunks.size() >= 4 && choirs.size() >= 4, "la textura de Skunk y el coro de Porcupine cambian en cada clic ("
                   + String ((int) skunks.size()) + " texturas y " + String ((int) choirs.size()) + " coros distintos en 5 versiones)");
            shot (mc, shots.getChildFile ("31_aleatoria_con_estructura.png"));
        }

        // ---------------------------------------------------- the computer keyboard: A S D F play the channel below, Z / X move the octave
        {
            auto& ck = mc.getComputerKeys();
            auto oldSend = ck.send;
            std::vector<std::pair<bool, int>> sent;
            ck.send = [&sent] (const MidiMessage& m) { sent.push_back ({ m.isNoteOn(), m.getNoteNumber() }); };
            ck.setOctave (5);
            mc.invokeDirectly (ui::MainComponent::viewComputerKeys, false);
            check (ck.isEnabled() && mc.getViewState().computerKeys && mc.getKeysButton().getToggleState(), "Cmd+K prende el teclado de la compu (y el botón TECLADO)"_u);
            ck.keyDown ('a'); ck.keyUp ('a');
            ck.keyDown ('w'); ck.keyUp ('w');
            ck.keyDown (';'); ck.keyUp (';');
            check (sent.size() == 6 && sent[0] == std::make_pair (true, 60) && sent[1] == std::make_pair (false, 60) && sent[2].second == 61 && sent[4].second == 76,
                   "A toca C3 (60), W C#3, Ñ / ; el E4 (" + String (sent.size() > 4 ? sent[4].second : -1) + ")"_u);
            sent.clear();
            ck.keyDown ('x'); ck.keyDown ('a'); ck.keyUp ('a');
            check (ck.getOctave() == 6 && ! sent.empty() && sent[0].second == 72, "X sube una octava: la A toca C4 (" + String (sent.empty() ? -1 : sent[0].second) + ")");
            sent.clear();
            ck.keyDown ('a'); ck.keyDown ('z'); ck.keyDown ('z'); ck.keyUp ('a');
            check (ck.getOctave() == 4 && sent.size() == 2 && sent[1] == std::make_pair (false, 72), "Z baja; la nota que sonaba se suelta donde empezó"_u);
            for (int i = 0; i < 12; ++i) ck.octaveDown();
            const int bottom = ck.getOctave();
            for (int i = 0; i < 12; ++i) ck.octaveUp();
            check (bottom == 0 && ck.getOctave() == 9, "de la octava 0 a la 9: todas las notas MIDI"_u);
            ck.setOctave (5);
            // while it plays, L and Z are notes / octave, not Loop and "ver toda la canción"
            const bool looping = s.getTransport().looping;
            ApplicationCommandTarget::InvocationInfo inv (ui::MainComponent::playLoop);
            inv.invocationMethod = ApplicationCommandTarget::InvocationInfo::fromKeyPress;
            inv.keyPress = KeyPress ('l');
            mc.perform (inv);
            check (s.getTransport().looping == looping, "con el teclado prendido, la L toca y no cambia el loop");
            mc.invokeDirectly (ui::MainComponent::viewComputerKeys, false);
            check (! ck.isEnabled() && ! mc.getViewState().computerKeys, "Cmd+K lo apaga");
            mc.perform (inv);
            check (s.getTransport().looping != looping, "apagado, la L vuelve a ser Loop");
            if (s.getTransport().looping != looping) mc.perform (inv);
            ck.send = oldSend;
        }

        // ---------------------------------------------------- Crear la rola completa: the window and the result on the timeline
        {
            s.newProject (true, false, &defaultGenre());
            pump (600);
            auto& v = mc.getViewState();
            v.rangeTracks.clear();
            for (auto* t : te::getAudioTracks (s.getEdit())) if (kindOf (*t) != TrackKind::returnBus) v.rangeTracks.add (t);
            v.rangeStart = 0.0; v.rangeEnd = 64.0;
            v.sendChangeMessage();
            mc.invokeDirectly (ui::MainComponent::editMakeSong, false);
            pump (300);
            ui::SongMakerPanel* panel = nullptr;
            for (int i = 0; i < Desktop::getInstance().getNumComponents() && panel == nullptr; ++i)
                if (auto* top = Desktop::getInstance().getComponent (i))
                    if (auto found = findChildren<ui::SongMakerPanel> (*top); ! found.isEmpty()) panel = found[0];
            check (panel != nullptr, "Editar > Crear la rola completa abre su ventana con la selección");
            if (panel != nullptr)
            {
                panel->setMinutes (7.0);
                pump (50);
                check (panel->totalBars() == arranger::barsForMinutes (7.0, s.getBpm()), "la ventana cuenta en frases: " + String (panel->totalBars()) + " compases");
                shot (*panel, shots.getChildFile ("22_crear_rola_completa.png"));
                panel->accept();
                pump (800);
                int markers = s.getEdit().getMarkerManager().getMarkers().size();
                check (markers == 7, "la rola queda en la maqueta con sus 7 secciones marcadas en la regla");
                mc.invokeDirectly (ui::MainComponent::viewZoomFit, false);
                pump (400);
                shot (mc, shots.getChildFile ("23_rola_completa_en_la_maqueta.png"));
            }
            // with nothing selected (the brush button on a fresh block) it takes the four bars where most channels play
            s.newProject (true, false, &defaultGenre());
            pump (600);
            v.clearRange();
            s.getSelection().deselectAll();
            mc.invokeDirectly (ui::MainComponent::editMakeSong, false);
            pump (300);
            ui::SongMakerPanel* p2 = nullptr;
            for (int i = 0; i < Desktop::getInstance().getNumComponents() && p2 == nullptr; ++i)
                if (auto* top = Desktop::getInstance().getComponent (i))
                    if (auto found = findChildren<ui::SongMakerPanel> (*top); ! found.isEmpty()) p2 = found[0];
            check (p2 != nullptr, "sin selección, el botón del pincel abre Crear la rola completa con los compases donde más canales suenan");
            if (p2 != nullptr)
            {
                p2->accept();
                pump (800);
                check (s.getEdit().getMarkerManager().getMarkers().size() == 7, "y arma la rola con sus 7 secciones");
            }
        }

        mc.setEasyMode (true);
        // no message pumping after this: on Linux, JUCE-built VST3s register run-loop handlers that outlive the
        // plugin when an Edit is destroyed (Linux-only VST3 IRunLoop issue; macOS does not use IRunLoop)
        s.newProject (false);
        shot (mc, shots.getChildFile ("09_proyecto_vacio.png"));
    }
    if (english)
    {
        // everything drawn during these tests went through the dictionary: what still looks Spanish, and what does not fit
        auto left = i18n::recordedSpanish();
        i18n::recordSpanish (false);
        // instrument names that English keeps with their accents (cajón, cuíca, güiro...) and "y" as a letter are not Spanish left behind
        for (int i = left.size(); --i >= 0;)
        {
            auto t = left[i];
            for (auto* w : { "Cajón", "cajón", "Cuíca", "cuíca", "Güiro", "güiro", "Agogó", "agogó", "Bongó", "bongó", "Đàn" })
                t = t.replace (String::fromUTF8 (w), "x");
            t = t.replace (": y as in", ": x as in");
            if (! i18n::looksSpanish (t)) left.remove (i);
        }
        left.sort (true);
        out.getChildFile ("textos_sin_traducir.txt").replaceWithText (left.joinIntoString ("\n"));
        out.getChildFile ("textos_que_no_caben.txt").replaceWithText (overflows.joinIntoString ("\n"));
        check (left.size() <= 3, String::fromUTF8 ("en inglés no queda texto en español a la vista: ") + String (left.size())
                                    + String::fromUTF8 (" textos sospechosos (lista en textos_sin_traducir.txt)"));
        check (fitChecked > 100 && overflows.isEmpty(), String::fromUTF8 ("en inglés cada botón y etiqueta cabe en su lugar: ") + String (fitChecked)
                                    + String::fromUTF8 (" medidos, ") + String (overflows.size()) + String::fromUTF8 (" se salen (lista en textos_que_no_caben.txt)"));
    }
    ui::prefs().setValue ("welcomeDone", false);
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    return uiFailures;
}

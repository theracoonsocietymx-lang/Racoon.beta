#include "DjPanel.h"
#include "Widgets.h"
#include "engine/I18n.h"

namespace racoon::ui
{
using namespace juce;

namespace
{
String u (const char* s) { return ::racoon::i18n::tr (String::fromUTF8 (s)); }
const StringArray audioExts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".ogg" };
}

DjPanel::DjPanel (Session& s) : session (s)
{
    setWantsKeyboardFocus (true);
    agregar.setButtonText (u ("Agregar rolas..."));
    ordenar.setButtonText (u ("Ordenar el set"));
    guardar.setButtonText (u ("Guardar el set..."));
    armar.setButtonText (u ("Armar una pista para mezclar con esta"));
    agregar.onClick = [this]
    {
        auto chooser = std::make_shared<FileChooser> (u ("Rolas para el set"), File::getSpecialLocation (File::userMusicDirectory),
                                                      "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [sp = SafePointer<DjPanel> (this), chooser] (const FileChooser& fc)
                              {
                                  if (sp != nullptr) sp->addFiles (fc.getResults());
                              });
    };
    ordenar.onClick = [this] { sortSet(); };
    guardar.onClick = [this]
    {
        auto chooser = std::make_shared<FileChooser> (u ("Guardar el set"), File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Set Racoon.m3u"), "*.m3u");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                              [sp = SafePointer<DjPanel> (this), chooser] (const FileChooser& fc)
                              {
                                  if (sp != nullptr && fc.getResult() != File()) sp->saveSet (fc.getResult().withFileExtension ("m3u"));
                              });
    };
    armar.onClick = [this] { buildFor (selected); };
    cerrar.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &agregar, &ordenar, &guardar, &armar, &cerrar }) addAndMakeVisible (b);
    updateButtons();
}

DjPanel::~DjPanel() { pool.removeAllJobs (true, 20000); }

bool DjPanel::busy() const
{
    for (auto& s : songs) if (! s.done) return true;
    return false;
}

void DjPanel::updateButtons()
{
    int ready = 0;
    for (auto& s : songs) ready += s.done && s.a.ok ? 1 : 0;
    ordenar.setEnabled (ready >= 2 && ! busy());
    guardar.setEnabled (ready >= 1 && ! busy());
    armar.setEnabled (isPositiveAndBelow (selected, (int) songs.size()) && songs[(size_t) selected].done && songs[(size_t) selected].a.ok);
}

bool DjPanel::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (audioExts.contains (File (f).getFileExtension().toLowerCase())) return true;
    return false;
}

void DjPanel::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    Array<File> list;
    for (auto& f : files) list.add (File (f));
    addFiles (list);
}

void DjPanel::addFiles (const Array<File>& files)
{
    for (auto& f : files)
    {
        if (! f.existsAsFile() || ! audioExts.contains (f.getFileExtension().toLowerCase())) continue;
        bool dup = false;
        for (auto& s : songs) dup = dup || s.a.file == f;
        if (dup) continue;
        Song s;
        s.a.file = f;
        s.a.titulo = f.getFileNameWithoutExtension();
        songs.push_back (s);
        const size_t index = songs.size() - 1;
        // a song takes about a second to measure: in the background, two at a time
        pool.addJob ([sp = SafePointer<DjPanel> (this), f, index, &engine = session.engine]
        {
            auto a = dj::analizar (engine, f);
            MessageManager::callAsync ([sp, a, index]
            {
                if (sp == nullptr || index >= sp->songs.size() || sp->songs[index].a.file != a.file) return;
                sp->songs[index].a = a;
                sp->songs[index].done = true;
                if (sp->selected < 0 && a.ok) sp->selected = (int) index;
                sp->ordered = false;
                sp->updateButtons();
                sp->repaint();
            });
        });
    }
    ordered = false;
    startTimerHz (4);
    updateButtons();
    repaint();
}

void DjPanel::addAnalysis (const dj::Analisis& a)
{
    Song s;
    s.a = a;
    s.done = true;
    songs.push_back (s);
    if (selected < 0) selected = 0;
    ordered = false;
    updateButtons();
    repaint();
}

void DjPanel::transiciones()
{
    for (size_t i = 0; i < songs.size(); ++i)
        songs[i].transicion = i + 1 < songs.size() && songs[i].a.ok && songs[i + 1].a.ok ? dj::transicion (songs[i].a, songs[i + 1].a) : String();
}

void DjPanel::sortSet()
{
    std::vector<dj::Analisis> list;
    std::vector<Song> bad, good;
    for (auto& s : songs) (s.a.ok ? good : bad).push_back (s);
    for (auto& s : good) list.push_back (s.a);
    const auto order = dj::ordenarSet (list);
    const auto selFile = isPositiveAndBelow (selected, (int) songs.size()) ? songs[(size_t) selected].a.file : File();
    songs.clear();
    for (int i : order) songs.push_back (good[(size_t) i]);
    for (auto& s : bad) songs.push_back (s);          // the ones that could not be measured go last
    selected = -1;
    for (size_t i = 0; i < songs.size(); ++i) if (songs[i].a.file == selFile) selected = (int) i;
    ordered = true;
    transiciones();
    updateButtons();
    repaint();
}

bool DjPanel::saveSet (const File& m3u)
{
    // the playlist any player opens, and next to it the set written out: tempo, key and how to go from each one to the next
    String list ("#EXTM3U\n"), notes;
    notes << u ("Set armado por Racoon") << "\n\n";
    int n = 0;
    for (size_t i = 0; i < songs.size(); ++i)
    {
        const auto& a = songs[i].a;
        if (! a.ok) continue;
        list << "#EXTINF:" << roundToInt (a.segundos) << "," << a.titulo << "\n" << a.file.getFullPathName() << "\n";
        notes << String (++n) << ". " << a.titulo << " - " << a.resumen() << "\n";
        if (ordered && songs[i].transicion.isNotEmpty()) notes << "     " << songs[i].transicion << "\n";
    }
    const bool ok = m3u.replaceWithText (list) && m3u.withFileExtension ("txt").replaceWithText (notes);
    aviso = ok ? u ("Set guardado: ") + m3u.getFileName() + " (y " + m3u.withFileExtension ("txt").getFileName() + ")" : u ("No se pudo guardar el set.");
    repaint();
    return ok;
}

void DjPanel::buildFor (int index)
{
    if (! isPositiveAndBelow (index, (int) songs.size()) || ! songs[(size_t) index].a.ok) return;
    const auto r = session.buildDjTool (songs[(size_t) index].a, 128);
    if (! r.ok) { aviso = r.texto; repaint(); return; }
    if (onBuilt) onBuilt (r);
}

bool DjPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == KeyPress::upKey && selected > 0) { --selected; updateButtons(); repaint(); return true; }
    if (k == KeyPress::downKey && selected + 1 < (int) songs.size()) { ++selected; updateButtons(); repaint(); return true; }
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && isPositiveAndBelow (selected, (int) songs.size()) && songs[(size_t) selected].done)
    {
        songs.erase (songs.begin() + selected);
        selected = jmin (selected, (int) songs.size() - 1);
        ordered = false;
        updateButtons();
        repaint();
        return true;
    }
    return false;
}

void DjPanel::mouseDown (const MouseEvent& e)
{
    if (! listArea.contains (e.getPosition())) return;
    const int i = (e.y - listArea.getY()) / rowH;
    if (! isPositiveAndBelow (i, (int) songs.size())) return;
    selected = i;
    updateButtons();
    repaint();
    if (e.mods.isPopupMenu() && songs[(size_t) i].done && songs[(size_t) i].a.ok)
    {
        // tempo detectors can land on half or double the tempo a DJ counts: fixed here
        PopupMenu m;
        m.addItem (1, u ("Tempo al doble"));
        m.addItem (2, u ("Tempo a la mitad"));
        m.addItem (3, u ("Quitar del set"));
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [sp = SafePointer<DjPanel> (this), i] (int r)
        {
            if (sp == nullptr || ! isPositiveAndBelow (i, (int) sp->songs.size())) return;
            if (r == 1) sp->songs[(size_t) i].a.bpm *= 2.0;
            if (r == 2) sp->songs[(size_t) i].a.bpm *= 0.5;
            if (r == 3) { sp->songs.erase (sp->songs.begin() + i); sp->selected = jmin (sp->selected, (int) sp->songs.size() - 1); }
            sp->ordered = false;
            sp->transiciones();
            sp->updateButtons();
            sp->repaint();
        });
    }
}

void DjPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (jmin (getWidth() - 32, 900), jmin (getHeight() - 32, 640));
    cerrar.setBounds (card.getRight() - 44, card.getY() + 12, 30, 30);
    auto r = card.reduced (28, 22);
    r.removeFromTop (40 + 50);
    auto bottom = r.removeFromBottom (40);
    agregar.setBounds (bottom.removeFromLeft (160));
    bottom.removeFromLeft (10);
    ordenar.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (10);
    guardar.setBounds (bottom.removeFromLeft (150));
    armar.setBounds (bottom.removeFromRight (300));
    r.removeFromBottom (30);
    r.removeFromTop (22);
    listArea = r;
}

void DjPanel::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1f1a1c).withAlpha (0.82f));
    Draw::panel (g, card.toFloat(), {}, false);
    auto r = card.reduced (28, 22);
    g.setColour (S::ink);
    g.setFont (Fonts::title (30.0f));
    g.drawText (u ("Modo DJ"), r.removeFromTop (40), Justification::centredLeft);
    g.setColour (S::inkSoft);
    g.setFont (Fonts::body (14.0f));
    g.drawFittedText (u ("Arrastra aquí tus rolas. Racoon mide su tempo, su tonalidad en la rueda de Camelot y su energía, ordena el set "
                         "y arma una pista para mezclar con la que elijas: a su tempo, en su tonalidad, con intro y outro de batería."),
                      r.removeFromTop (50), Justification::topLeft, 3);
    // the header of the list
    auto head = r.removeFromTop (22);
    g.setFont (Fonts::body (12.0f));
    g.setColour (S::inkFaint);
    auto cols = [] (Rectangle<int> row)
    {
        struct Cols { Rectangle<int> num, title, bpm, key, energy; } c;
        c.num = row.removeFromLeft (34);
        c.energy = row.removeFromRight (120);
        c.key = row.removeFromRight (170);
        c.bpm = row.removeFromRight (100);
        c.title = row;
        return c;
    };
    {
        auto c = cols (head);
        g.drawText (u ("Rola"), c.title, Justification::centredLeft);
        g.drawText ("BPM", c.bpm, Justification::centredLeft);
        g.drawText (u ("Tonalidad"), c.key, Justification::centredLeft);
        g.drawText (u ("Energía"), c.energy, Justification::centredLeft);
    }
    g.saveState();
    g.reduceClipRegion (listArea);
    if (songs.empty())
    {
        g.setColour (dragging ? Brand::limon : S::inkFaint);
        g.drawRoundedRectangle (listArea.toFloat().reduced (2.0f), 8.0f, 2.0f);
        g.setFont (Fonts::body (16.0f));
        g.drawText (u ("Suelta aquí tus rolas (wav, aiff, flac, mp3)"), listArea, Justification::centred);
    }
    for (size_t i = 0; i < songs.size(); ++i)
    {
        auto row = listArea.withY (listArea.getY() + (int) i * rowH).withHeight (rowH);
        if (row.getY() > listArea.getBottom()) break;
        if ((int) i == selected) { g.setColour (S::cardDim.brighter (0.15f)); g.fillRoundedRectangle (row.toFloat(), 6.0f); }
        const auto& s = songs[i];
        auto c = cols (row.reduced (0, 2));
        auto top = [] (Rectangle<int> x) { return x.removeFromTop (x.getHeight() * 3 / 5); };
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (13.0f));
        g.drawText (String ((int) i + 1), c.num, Justification::centred);
        g.setColour (S::ink);
        g.setFont (Fonts::body (14.0f));
        auto t = c.title;
        g.drawText (s.a.titulo, top (t), Justification::centredLeft, true);
        g.setFont (Fonts::body (11.0f));
        g.setColour (S::inkFaint);
        if (! s.done) g.drawText (u ("midiendo..."), t.withTrimmedTop (t.getHeight() * 3 / 5), Justification::centredLeft);
        else if (! s.a.ok) g.drawText (s.a.error, t.withTrimmedTop (t.getHeight() * 3 / 5), Justification::centredLeft);
        else if (ordered && s.transicion.isNotEmpty())
        {
            g.setColour (s.transicion.contains (u ("chocan")) ? Brand::rosa : Brand::menta);
            g.drawText (String::fromUTF8 ("↓ ") + s.transicion, t.withTrimmedTop (t.getHeight() * 3 / 5), Justification::centredLeft, true);
        }
        if (! s.done || ! s.a.ok) continue;
        g.setColour (S::ink);
        g.setFont (Fonts::body (14.0f));
        g.drawText (String (s.a.bpm, 2), c.bpm, Justification::centredLeft);
        g.drawText (s.a.camelot() + "  " + ::racoon::i18n::tr (keyName (s.a.key, s.a.major)), c.key, Justification::centredLeft);
        auto bar = c.energy.withSizeKeepingCentre (c.energy.getWidth() - 10, 10).toFloat();
        g.setColour (S::shade);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (Brand::limon.interpolatedWith (Brand::rosa, (float) s.a.energia));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) jmax (0.05, s.a.energia)), 4.0f);
    }
    g.restoreState();
    if (aviso.isNotEmpty())
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (13.0f));
        g.drawText (aviso, card.reduced (28, 22).removeFromBottom (68).removeFromTop (24), Justification::centredLeft, true);
    }
}
}

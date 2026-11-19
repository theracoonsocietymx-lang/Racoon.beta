#include "Browser.h"
#include "engine/I18n.h"
#include "fx/core/Upper.h"
#include "fx/NativeFx.h"
#include "engine/SoundLibrary.h"
#include "engine/Muestras.h"
#include "engine/SamplePreview.h"
#include <BinaryData.h>

namespace racoon::ui
{
using namespace juce;

void drawAnimalBadge (Graphics& g, Rectangle<float> r, const String& name, Colour fill)
{
    // every animal of the family has its badge in BinaryData as "<animal>_png"
    Image img;
    auto animal = Catalog::animalFor (name).toLowerCase();
    if (animal.isEmpty() && name.containsIgnoreCase ("Racoon")) animal = "racoon";
    int size = 0;
    if (const char* data = BinaryData::getNamedResource ((animal.retainCharacters ("abcdefghijklmnopqrstuvwxyz") + "_png").toRawUTF8(), size))
        img = ImageCache::getFromMemory (data, size);
    if (! img.isValid() && name.containsIgnoreCase ("Racoon")) img = ImageCache::getFromMemory (BinaryData::racoon_png, BinaryData::racoon_pngSize);

    if (img.isValid())
    {
        g.drawImage (img, r, RectanglePlacement::centred);
        return;
    }
    Draw::box (g, r, fill, 1.5f, 2.0f);
    g.setColour (S::ink);
    g.setFont (Fonts::title (r.getHeight() * 0.5f));
    g.drawText (::racoon::i18n::tr (name.substring (0, 1).toUpperCase()), r.withTrimmedRight (2.0f).withTrimmedBottom (2.0f), Justification::centred);
}

// ===================================================================== catalog list
class Browser::ItemList : public Component
{
public:
    explicit ItemList (Browser& b) : owner (b) {}

    struct Row { CatalogItem item; bool header = false, note = false; String headerText; int height = 44; };
    Array<Row> rows;

    void setItems (const Array<Row>& r)
    {
        rows = r;
        int h = 0;
        for (auto& row : rows) h += row.height;
        setSize (getWidth(), jmax (h + 10, 50));
        repaint();
    }

    int rowAt (int y) const
    {
        int top = 0;
        for (int i = 0; i < rows.size(); ++i) { if (y < top + rows[i].height) return i; top += rows[i].height; }
        return -1;
    }
    Rectangle<int> rowBounds (int index) const
    {
        int top = 0;
        for (int i = 0; i < index; ++i) top += rows[i].height;
        return { 0, top, getWidth(), rows[index].height };
    }

    void paint (Graphics& g) override
    {
        for (int i = 0; i < rows.size(); ++i)
        {
            auto& row = rows.getReference (i);
            auto r = rowBounds (i).reduced (8, 3);
            if (row.header)
            {
                drawCaption (g, upper (row.headerText), r.withTrimmedTop (8), 12.5f, S::inkSoft);
                continue;
            }
            if (row.note)
            {
                g.setColour (S::inkSoft);
                g.setFont (Fonts::body (12.5f));
                g.drawFittedText (::racoon::i18n::tr (row.headerText), r.reduced (2, 0), Justification::centredLeft, 3, 0.9f);
                continue;
            }
            const bool racoon = row.item.source == CatalogItem::Source::racoon;
            const bool over = i == hover;
            Draw::box (g, r.toFloat(), ! row.item.available ? S::cardDim : (over ? C::stone : S::card), 1.5f, racoon ? 3.0f : 2.0f);
            auto body = r.withTrimmedRight (3).withTrimmedBottom (3).reduced (6, 4);

            if (racoon)
            {
                drawAnimalBadge (g, body.removeFromLeft (body.getHeight()).toFloat(), row.item.name, Catalog::colourFor (row.item.name));
                body.removeFromLeft (8);
            }
            auto plus = body.removeFromRight (26).withSizeKeepingCentre (24, 24);
            if (row.item.available)
            {
                Draw::box (g, plus.toFloat(), over ? Brand::limon : S::cardDim, 1.5f, 2.0f);
                drawGlyph (g, Glyph::plus, plus.toFloat().withTrimmedRight (2).withTrimmedBottom (2).reduced (4.0f), S::ink);
            }

            g.setColour (row.item.available ? S::ink : S::inkFaint);
            g.setFont (racoon ? Fonts::title (17.0f) : Fonts::bodyBold (14.0f));
            auto nameR = body.removeFromTop (racoon ? body.getHeight() / 2 + 2 : body.getHeight() / 2 + 3);
            g.drawFittedText (::racoon::i18n::tr (racoon ? Catalog::titleFor (row.item.name) : row.item.name), nameR, Justification::bottomLeft, 1, 0.8f);
            g.setColour (S::inkSoft);
            g.setFont (Fonts::body (12.0f));
            g.drawFittedText (::racoon::i18n::tr (row.item.available ? row.item.detail : String::fromUTF8 ("No instalado: abre su instalador .dmg")), body, Justification::topLeft, 2, 0.9f);
        }
    }

    void mouseMove (const MouseEvent& e) override { const int h = rowAt (e.y); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const MouseEvent&) override   { hover = -1; repaint(); }

    void mouseUp (const MouseEvent& e) override
    {
        if (dragging) { dragging = false; return; }
        const int i = rowAt (e.y);
        if (i < 0 || rows[i].header || rows[i].note || ! rows[i].item.available) return;
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            auto item = rows[i].item;
            if (Catalog::isMasterProduct (item))
                m.addItem ("Poner en el Master", [this, item] { if (owner.onAddItem) owner.onAddItem (item); });
            else if (Catalog::listedAsInstrument (item))
            {
                m.addItem ("Crear un canal con este", [this, item] { if (owner.onAddItem) owner.onAddItem (item); });
                if (! item.isInstrument)
                    m.addItem ("Ponerlo en el canal seleccionado", [this, item] { if (owner.onInsertItem) owner.onInsertItem (item); });
            }
            else
                m.addItem ("Poner en el canal seleccionado", [this, item] { if (owner.onAddItem) owner.onAddItem (item); });
            m.addItem ("Arrastrar a la pista que quieras (también se puede)"_u, false, false, [] {});
            m.showMenuAsync (PopupMenu::Options().withMousePosition());
            return;
        }
        auto plus = rowBounds (i).reduced (8, 3).withTrimmedRight (3).reduced (6, 4).removeFromRight (26);
        if (plus.contains (e.getPosition()) || e.getNumberOfClicks() > 1)
            if (owner.onAddItem) owner.onAddItem (rows[i].item);
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i >= 0 && ! rows[i].header && ! rows[i].note && rows[i].item.available && owner.onAddItem) owner.onAddItem (rows[i].item);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        const int i = rowAt (e.getMouseDownY());
        if (i < 0 || rows[i].header || rows[i].note) return;
        if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            SamplePreview::get (owner.vs.session.engine).stop();
            auto snap = createComponentSnapshot (rowBounds (i)).convertedToFormat (Image::ARGB);
            snap.multiplyAllAlphas (0.8f);
            c->startDragging (DragData::forItem (rows[i].item), this, ScaledImage (snap), true);
        }
    }

    Browser& owner;
    int hover = -1;
    bool dragging = false;
};

// ===================================================================== sample files
class Browser::FileList : public Component, public SettableTooltipClient
{
public:
    explicit FileList (Browser& b) : owner (b)
    {
        setTooltip (String::fromUTF8 ("Clic en un audio: se escucha. Flechas arriba y abajo: el anterior o el siguiente, y suena. "
                                      "Arrástralo a una pista para usarlo. Doble clic en una carpeta (o Enter, o flecha derecha) para entrar; izquierda para salir."));
        setWantsKeyboardFocus (true);
    }

    // ---- choosing and listening (a click or the arrows play the sound)
    void select (int i, bool listen)
    {
        if (! isPositiveAndBelow (i, entries.size())) return;
        selected = i;
        repaint();
        if (auto* vp = findParentComponentOfClass<Viewport>())
        {
            const int top = i * rowH, viewTop = vp->getViewPositionY(), viewH = vp->getViewHeight();
            if (top < viewTop) vp->setViewPosition (0, top);
            else if (top + rowH > viewTop + viewH) vp->setViewPosition (0, top + rowH - viewH);
        }
        if (listen && ! entries[i].isDirectory()) owner.preview (entries[i]);
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (entries.isEmpty()) return false;
        if (k == KeyPress::downKey) { select (selected < 0 ? 0 : jmin (entries.size() - 1, selected + 1), true); return true; }
        if (k == KeyPress::upKey)   { select (selected < 0 ? 0 : jmax (0, selected - 1), true); return true; }
        if (k == KeyPress::leftKey || k == KeyPress::backspaceKey)
        {
            if (results) owner.searchSamples ({});
            else owner.goToFolder (owner.sampleFolder.getParentDirectory());
            return true;
        }
        if (isPositiveAndBelow (selected, entries.size()))
        {
            const auto f = entries[selected];
            if ((k == KeyPress::rightKey || k == KeyPress::returnKey) && f.isDirectory()) { owner.goToFolder (f); return true; }
            if (k == KeyPress::returnKey && owner.onAddFile) { owner.onAddFile (f); return true; }
            if (k == KeyPress::spaceKey && ! f.isDirectory())
            {
                // space: stop, or play again
                auto& pv = SamplePreview::get (owner.vs.session.engine);
                if (pv.isPlaying() && pv.currentFile() == f) pv.stop(); else owner.preview (f);
                return true;
            }
        }
        return false;
    }

    void setResults (const Array<File>& found)
    {
        results = true;
        entries = found;
        selected = -1;
        setSize (getWidth(), jmax (60, entries.size() * rowH + 10));
        repaint();
    }

    void setFolder (const File& f)
    {
        results = false;
        folder = f;
        entries.clear();
        for (auto& d : folder.findChildFiles (File::findDirectories, false))
            if (! d.isHidden()) entries.add (d);
        entries.sort();
        Array<File> audio;
        for (auto& a : folder.findChildFiles (File::findFiles, false))
            if (DragData::isAudioFile (a)) audio.add (a);
        audio.sort();
        entries.addArray (audio);
        selected = -1;
        setSize (getWidth(), jmax (60, entries.size() * rowH + 10));
        repaint();
    }

    void paint (Graphics& g) override
    {
        if (entries.isEmpty())
        {
            g.setColour (S::inkSoft);
            g.setFont (Fonts::body (13.0f));
            g.drawFittedText (::racoon::i18n::tr (results ? String::fromUTF8 ("No encontré audios con ese nombre.")
                                                          : String::fromUTF8 ("No hay audios en esta carpeta. Elige otra con CARPETA...")),
                              getLocalBounds().reduced (10), Justification::topLeft, 3);
        }
        for (int i = 0; i < entries.size(); ++i)
        {
            auto r = Rectangle<int> (6, i * rowH, getWidth() - 12, rowH);
            const bool dir = entries[i].isDirectory();
            if (i == hover) { g.setColour (C::stone); g.fillRect (r); }
            if (i == selected)
            {
                // the chosen one: a lit row with a Limón edge (and the sound playing, if it is audio)
                g.setColour (C::stoneHi);
                g.fillRect (r);
                g.setColour (Brand::limon);
                g.fillRect (r.withWidth (3));
            }
            drawGlyph (g, dir ? Glyph::folder : Glyph::wave, r.removeFromLeft (rowH).toFloat().reduced (5.0f), dir ? S::inkSoft : S::ink);
            g.setColour (S::ink);
            g.setFont (dir ? Fonts::bodyBold (13.0f) : Fonts::body (13.0f));
            const auto pretty = muestras::nombreVisible (entries[i]);
            if (results)
            {
                // a search result also says where it lives
                const auto par = entries[i].getParentDirectory();
                const auto parPretty = muestras::nombreVisible (par);
                g.setColour (S::inkFaint);
                g.setFont (Fonts::body (11.0f));
                auto where = r.removeFromRight (jmin (110, r.getWidth() / 3));
                g.drawFittedText (::racoon::i18n::tr (parPretty.isNotEmpty() ? parPretty : par.getFileName()), where.reduced (2, 0), Justification::centredRight, 1, 0.7f);
                g.setColour (S::ink);
                g.setFont (Fonts::body (13.0f));
            }
            g.drawFittedText (::racoon::i18n::tr (pretty.isNotEmpty() ? pretty : entries[i].getFileName()), r.reduced (4, 0), Justification::centredLeft, 1, 0.8f);
        }
    }

    int rowAt (int y) const { const int i = y / rowH; return isPositiveAndBelow (i, entries.size()) ? i : -1; }
    void mouseMove (const MouseEvent& e) override { hover = rowAt (e.y); repaint(); }
    void mouseExit (const MouseEvent&) override   { hover = -1; repaint(); }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i < 0) return;
        if (entries[i].isDirectory()) owner.goToFolder (entries[i]);
        else if (owner.onAddFile) owner.onAddFile (entries[i]);
    }

    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (! e.mods.isPopupMenu()) { select (rowAt (e.y), true); return; }
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            const int i = rowAt (e.y);
            if (i >= 0)
            {
                auto f = entries[i];
                if (f.isDirectory()) m.addItem ("Abrir carpeta", [this, f] { owner.goToFolder (f); });
                else
                {
                    m.addItem (String::fromUTF8 ("Añadir en una pista nueva"), [this, f] { if (owner.onAddFile) owner.onAddFile (f); });
                    m.addItem ("Escuchar", [this, f] { owner.preview (f); });
                    m.addItem ("Parar", [this] { SamplePreview::get (owner.vs.session.engine).stop(); });
                }
                m.addItem ("Mostrar en el Finder", [f] { f.revealToUser(); });
                m.addSeparator();
            }
            m.addItem (String::fromUTF8 ("Atrás"), ! owner.backStack.isEmpty() || owner.sampleFolder.getParentDirectory() != owner.sampleFolder, false, [this] { owner.goBack(); });
            m.addItem ("Adelante", ! owner.forwardStack.isEmpty(), false, [this] { owner.goForward(); });
            m.addItem ("Elegir otra carpeta...", [this] { owner.folderButton.triggerClick(); });
            m.showMenuAsync (PopupMenu::Options().withMousePosition());
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        const int i = rowAt (e.getMouseDownY());
        if (i < 0 || entries[i].isDirectory()) return;
        if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            auto snap = createComponentSnapshot ({ 0, i * rowH, getWidth(), rowH }).convertedToFormat (Image::ARGB);
            snap.multiplyAllAlphas (0.8f);
            c->startDragging (DragData::forFile (entries[i]), this, ScaledImage (snap), true);
        }
    }
    void mouseUp (const MouseEvent&) override { dragging = false; }

    Browser& owner;
    File folder;
    Array<File> entries;
    bool results = false;
    int hover = -1, selected = -1;
    bool dragging = false;
    static constexpr int rowH = 24;
};

// ===================================================================== browser
Browser::Browser (ViewState& v) : vs (v)
{
    const char* names[] { "INSTRUMENTOS", "EFECTOS", "SAMPLES" };
    const char* help[] { "Instrumentos: Badger Bass, Hamster Drums, Dormouse Pads, Squirrel Sampler, Ferret Guitar, Porcupine Choir y los tuyos",
                         "Efectos: los 45 de Racoon, Hedgehog Mastering, Skunk Sound Design, Chinchilla Vocals, Chipmunk Chords y Mole Rhythm (MIDI) y los tuyos",
                         "Tus audios: arrástralos a la línea de tiempo" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = tabButtons.add (new TextButton (names[i]));
        b->setRadioGroupId (77);
        b->setClickingTogglesState (true);
        b->setTooltip (String::fromUTF8 (help[i]));
        b->onClick = [this, i] { setTab ((Tab) i); };
        addAndMakeVisible (b);
    }
    tabButtons[0]->setToggleState (true, dontSendNotification);

    search.setTextToShowWhenEmpty (::racoon::i18n::tr ("Buscar..."), S::inkFaint);
    search.setFont (Fonts::body (14.0f));
    search.onTextChange = [this] { refreshItems(); };
    addAndMakeVisible (search);

    sampleSearch.setTextToShowWhenEmpty (::racoon::i18n::tr (String::fromUTF8 ("Buscar un sonido (tom, riser, clap...)")), S::inkFaint);
    sampleSearch.setFont (Fonts::body (14.0f));
    sampleSearch.setTooltip (String::fromUTF8 ("Escribe parte del nombre: aparecen todos los audios que lo tienen, de la Biblioteca, Muestras Racoon, "
                                               "tu carpeta de samples y los audios de tus proyectos. Esc borra la búsqueda."));
    sampleSearch.onTextChange = [this] { searchSamples (sampleSearch.getText()); };
    sampleSearch.onEscapeKey = [this] { sampleSearch.clear(); searchSamples ({}); };
    sampleSearch.onReturnKey = [this] { if (files != nullptr) files->grabKeyboardFocus(); };
    addChildComponent (sampleSearch);

    scanButton.setTooltip (String::fromUTF8 ("Busca los VST3 y AU instalados en este ordenador (se hace una vez; el escáner corre aparte por si algún plugin falla)"));
    scanButton.onClick = [this] { showPluginScanner(); };
    addAndMakeVisible (scanButton);

    folderButton.setTooltip ("Elegir la carpeta de samples");
    folderButton.onClick = [this]
    {
        auto chooser = std::make_shared<FileChooser> ("Carpeta de samples", sampleFolder);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this, chooser] (const FileChooser& fc)
        {
            if (fc.getResult().isDirectory()) { prefs().setValue ("sampleFolderChosen", true); goToFolder (fc.getResult()); }
        });
    };
    backButton.setTooltip (String::fromUTF8 ("Atrás: la carpeta donde estabas"));
    backButton.onClick = [this] { goBack(); };
    forwardButton.setTooltip ("Adelante");
    forwardButton.onClick = [this] { goForward(); };
    libraryButton.setTooltip (String::fromUTF8 ("Biblioteca Racoon: bombos, cajas, charles, percusiones, loops por estilo, fills, bajos y transiciones"));
    libraryButton.onClick = [this] { prefs().setValue ("sampleFolderChosen", false); goToFolder (library::folder()); };
    addChildComponent (libraryButton);
    // Muestras Racoon: miles de golpes, instrumentos, texturas y loops reales con licencia libre (se copia a Música/Racoon)
    muestrasButton.setTooltip (String::fromUTF8 ("Muestras Racoon: 3060 golpes de batería y percusión reales, 76 instrumentos, texturas, loops y 5063 ondas, de 55 bibliotecas libres"));
    muestrasButton.onClick = [this]
    {
        const auto& c = biblio::Catalogo::get();
        if (c.disponible()) { prefs().setValue ("sampleFolderChosen", true); goToFolder (c.raiz); }
    };
    addChildComponent (muestrasButton);
    // Efectos por género: risers, rollers, impactos, incidentales y texturas de cada género (Racoon los escribe la
    // primera vez que abre; el instalador suma los grabados)
    efectosButton.setTooltip (String::fromUTF8 ("Efectos por género: entra a tu género y encuentras sus risers, rollers, impactos, sub drops, incidentales y texturas, a su tempo y en su tonalidad"));
    efectosButton.onClick = [this]
    {
        const auto f = library::effectsFolder();
        f.createDirectory();   // the first minute after installing, Racoon is still writing them: they show up as they come
        prefs().setValue ("sampleFolderChosen", true);
        goToFolder (f);
    };
    addChildComponent (efectosButton);
    muestras::instalarAsync();
    addChildComponent (folderButton);
    addChildComponent (backButton);
    addChildComponent (forwardButton);

    items = std::make_unique<ItemList> (*this);
    files = std::make_unique<FileList> (*this);
    itemViewport.setViewedComponent (items.get(), false);
    itemViewport.setScrollBarsShown (true, false);
    itemViewport.setScrollBarThickness (8);
    addAndMakeVisible (itemViewport);

    // the samples tab opens on Racoon's own library unless you chose another folder; the library is written
    // the first time Racoon opens (and again when a new version brings new sounds)
    sampleFolder = library::folder().isDirectory() ? library::folder() : File::getSpecialLocation (File::userMusicDirectory);
    auto saved = prefs().getValue ("sampleFolder");
    if (saved.isNotEmpty() && File (saved).isDirectory() && prefs().getBoolValue ("sampleFolderChosen", false)) sampleFolder = File (saved);
    files->setFolder (sampleFolder);
    library::ensureAsync ([safe = Component::SafePointer<Browser> (this)]
    {
        if (safe == nullptr) return;
        if (! prefs().getBoolValue ("sampleFolderChosen", false)) safe->goToFolder (library::folder(), false);
        else if (safe->sampleFolder.isAChildOf (library::folder()) || safe->sampleFolder == library::folder()) safe->goToFolder (safe->sampleFolder, false);
    });

    vs.session.engine.getPluginManager().knownPluginList.addChangeListener (this);
    refreshItems();
}

Browser::~Browser()
{
    vs.session.engine.getPluginManager().knownPluginList.removeChangeListener (this);
}

Component* Browser::sampleList() const { return files.get(); }

void Browser::preview (const File& f)
{
    SamplePreview::get (vs.session.engine).play (f);
}

void Browser::setTab (Tab t)
{
    tab = t;
    if (t != samples) SamplePreview::get (vs.session.engine).stop();
    tabButtons[(int) t]->setToggleState (true, dontSendNotification);
    const bool sam = t == samples;
    itemViewport.setViewedComponent (sam ? (Component*) files.get() : (Component*) items.get(), false);
    for (auto* b : tabButtons) b->setToggleState (b == tabButtons[(int) t], dontSendNotification);
    folderButton.setVisible (sam);
    libraryButton.setVisible (sam);
    muestrasButton.setVisible (sam);
    efectosButton.setVisible (sam);
    backButton.setVisible (sam);
    forwardButton.setVisible (sam);
    scanButton.setVisible (! sam);
    search.setVisible (! sam);
    sampleSearch.setVisible (sam);
    refreshItems();
    resized();
    repaint();
}

void Browser::refreshItems()
{
    const auto q = search.getText().trim();
    auto matches = [&q] (const CatalogItem& it) { return q.isEmpty() || it.name.containsIgnoreCase (q) || it.detail.containsIgnoreCase (q); };
    Array<ItemList::Row> rows;
    auto header = [&rows] (const String& t) { ItemList::Row r; r.header = true; r.headerText = t; r.height = 30; rows.add (r); };
    auto add = [&rows] (const CatalogItem& it, int h) { ItemList::Row r; r.item = it; r.height = h; rows.add (r); };

    if (tab == instruments)
    {
        // the whole family except Hedgehog Mastering, which lives on the master (EFECTOS)
        header ("Familia Racoon");
        for (auto& it : Catalog::racoonItems (vs.session.engine)) if (Catalog::listedAsInstrument (it) && matches (it)) add (it, 74);
        Array<CatalogItem> ext;
        for (auto& it : Catalog::externalItems (vs.session.engine)) if (it.isInstrument && matches (it)) ext.add (it);
        header ("Tus plugins (" + String (ext.size()) + ")");
        for (auto& it : ext) add (it, 50);
    }
    else if (tab == effects)
    {
        // Hedgehog first (it goes on the master), then Racoon's own effects by family, the MIDI tools and the user's plug-ins
        bool family = false;
        for (auto& it : Catalog::racoonItems (vs.session.engine))
            if (Catalog::isMasterProduct (it) && matches (it)) { if (! family) { header (String::fromUTF8 ("Familia Racoon · Master")); family = true; } add (it, 74); }
        for (auto& cat : fx::categories())
        {
            Array<CatalogItem> list;
            for (auto& it : Catalog::nativeEffects (cat)) if (matches (it)) list.add (it);
            if (list.isEmpty()) continue;
            header (cat);
            for (auto& it : list) add (it, 50);
        }
        header ("MIDI");
        for (auto& it : Catalog::builtInItems()) if (! it.isInstrument && matches (it)) add (it, 50);
        Array<CatalogItem> ext;
        for (auto& it : Catalog::externalItems (vs.session.engine)) if (! it.isInstrument && matches (it)) ext.add (it);
        header ("Tus plugins (" + String (ext.size()) + ")");
        for (auto& it : ext) add (it, 50);
    }
    items->setSize (itemViewport.getMaximumVisibleWidth(), items->getHeight());
    items->setItems (rows);
}

void Browser::showPluginScanner()
{
    auto& pm = vs.session.engine.getPluginManager();
    DialogWindow::LaunchOptions o;
    o.dialogTitle = ::racoon::i18n::tr ("Buscar plugins");
    o.dialogBackgroundColour = S::shade;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    auto* v = new PluginListComponent (pm.pluginFormatManager, pm.knownPluginList,
                                       vs.session.engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                       std::addressof (vs.session.engine.getPropertyStorage().getPropertiesFile()), true);
    v->setSize (760, 560);
    o.content.setOwned (v);
    o.launchAsync();
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto tabs = r.removeFromTop (28);
    const int w = tabs.getWidth() / 3;
    for (auto* b : tabButtons) b->setBounds (tabs.removeFromLeft (w).reduced (1, 0));
    r.removeFromTop (8);
    if (tab == samples)
    {
        auto row = r.removeFromTop (26);
        backButton.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (4);
        forwardButton.setBounds (row.removeFromLeft (30));
        row.removeFromLeft (6);
        folderButton.setBounds (row.reduced (1, 0));
        r.removeFromTop (4);
        // the three libraries of Racoon, side by side
        auto libs = r.removeFromTop (26);
        const int bw = (libs.getWidth() - 8) / 3;
        libraryButton.setBounds (libs.removeFromLeft (bw).reduced (1, 0));
        libs.removeFromLeft (4);
        muestrasButton.setBounds (libs.removeFromLeft (bw).reduced (1, 0));
        libs.removeFromLeft (4);
        efectosButton.setBounds (libs.reduced (1, 0));
        r.removeFromTop (22);
        sampleSearch.setBounds (r.removeFromTop (28));
    }
    else
    {
        search.setBounds (r.removeFromTop (28));
        scanButton.setBounds (r.removeFromBottom (28));
        r.removeFromBottom (6);
    }
    r.removeFromTop (4);
    itemViewport.setBounds (r);
    const int w2 = itemViewport.getMaximumVisibleWidth();
    items->setSize (w2, items->getHeight());
    files->setSize (w2, files->getHeight());
}

void Browser::paint (Graphics& g)
{
    g.fillAll (S::shade);
    Draw::box (g, getLocalBounds().toFloat().reduced (2.0f), S::cardDim, 0.0f, 0.0f);
    if (tab == samples)
    {
        const auto cap = resultCount >= 0 ? String::fromUTF8 ("Resultados: ") + String (resultCount)
                                          : (muestras::nombreVisible (sampleFolder).isNotEmpty() ? muestras::nombreVisible (sampleFolder) : sampleFolder.getFileName());
        // the folder's name goes under the three library buttons
        drawCaption (g, upper (cap), backButton.getBounds().withY (efectosButton.getBottom() + 2).withRight (folderButton.getRight()), 12.0f, S::inkSoft);
    }
    g.setColour (C::stoneHi);
    g.fillRect (getWidth() - 2, 0, 2, getHeight());
}
}

namespace racoon::ui
{
void Browser::goToFolder (const juce::File& f, bool remember)
{
    if (! f.isDirectory()) return;
    if (remember && f != sampleFolder) { backStack.add (sampleFolder); forwardStack.clear(); }
    sampleFolder = f;
    prefs().setValue ("sampleFolder", sampleFolder.getFullPathName());
    sampleSearch.setText ({}, dontSendNotification);
    resultCount = -1;
    files->setFolder (sampleFolder);
    itemViewport.setViewPosition (0, 0);
    backButton.setEnabled (! backStack.isEmpty() || sampleFolder.getParentDirectory() != sampleFolder);
    forwardButton.setEnabled (! forwardStack.isEmpty());
    resized();
    repaint();
}

void Browser::goBack()
{
    // back to where you were; with no history, back is the folder that contains this one
    juce::File to = backStack.isEmpty() ? sampleFolder.getParentDirectory() : backStack.removeAndReturn (backStack.size() - 1);
    if (to == sampleFolder) return;
    forwardStack.add (sampleFolder);
    goToFolder (to, false);
}

void Browser::goForward()
{
    if (forwardStack.isEmpty()) return;
    backStack.add (sampleFolder);
    goToFolder (forwardStack.removeAndReturn (forwardStack.size() - 1), false);
}
}

namespace racoon::ui
{
namespace
{
// lower case without accents: "Tóm" and "tom" are the same search
String plain (const String& s)
{
    auto r = s.toLowerCase();
    const auto from = String::fromUTF8 ("áéíóúüñÁÉÍÓÚÜÑ"), to = String ("aeiouunaeiouun");
    for (int i = 0; i < from.length(); ++i) r = r.replaceCharacter (from[i], to[i]);
    return r;
}
}

void Browser::buildSampleIndex()
{
    Array<File> roots;
    auto add = [&roots] (const File& f)
    {
        if (! f.isDirectory()) return;
        for (auto& r : roots) if (f == r || f.isAChildOf (r)) return;
        for (int i = roots.size(); --i >= 0;) if (roots[i].isAChildOf (f)) roots.remove (i);
        roots.add (f);
    };
    add (library::folder());
    add (library::effectsFolder());
    const auto& c = biblio::Catalogo::get();
    if (c.disponible()) add (c.raiz);
    if (prefs().getBoolValue ("sampleFolderChosen", false)) add (File (prefs().getValue ("sampleFolder")));
    add (vs.session.audioFolder());
    add (Session::appDataFolder().getChildFile ("Sin guardar"));
    if (! vs.session.isUntitled()) add (vs.session.getFile().getParentDirectory());
    if (roots == indexedRoots && ! sampleIndex.empty()) return;
    indexedRoots = roots;
    sampleIndex.clear();
    int seen = 0;
    for (auto& root : roots)
        for (const auto& entry : RangedDirectoryIterator (root, true, "*", File::findFiles, File::FollowSymlinks::no))
        {
            if (++seen > 60000) break;
            const auto f = entry.getFile();
            if (! DragData::isAudioFile (f)) continue;
            const auto par = f.getParentDirectory();
            // the folder above counts too: "house roller" finds the rollers of House (Efectos por género/House/Rollers)
            sampleIndex.push_back ({ f, plain (f.getFileName() + " " + muestras::nombreVisible (f) + " " + par.getFileName() + " " + muestras::nombreVisible (par)
                                               + " " + par.getParentDirectory().getFileName()) });
        }
}

void Browser::searchSamples (const String& query)
{
    const auto q = plain (query.trim());
    if (sampleSearch.getText() != query) sampleSearch.setText (query, dontSendNotification);
    if (q.length() < 2)
    {
        resultCount = -1;
        files->setFolder (sampleFolder);
        itemViewport.setViewPosition (0, 0);
        repaint();
        return;
    }
    buildSampleIndex();
    const auto words = StringArray::fromTokens (q, " ", "");
    Array<File> starts, others;
    for (auto& it : sampleIndex)
    {
        bool all = true;
        for (auto& w : words) if (w.isNotEmpty() && ! it.key.contains (w)) { all = false; break; }
        if (! all) continue;
        (plain (it.file.getFileName()).startsWith (words[0]) ? starts : others).add (it.file);
    }
    starts.sort(); others.sort();
    starts.addArray (others);
    resultCount = starts.size();
    if (starts.size() > 500) starts.removeRange (500, starts.size() - 500);
    files->setResults (starts);
    itemViewport.setViewPosition (0, 0);
    repaint();
}

Array<File> Browser::sampleResults() const
{
    Array<File> out;
    if (resultCount < 0) return out;
    return files->entries;
}
}

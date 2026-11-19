#include "PanelMuestras.h"
#include "I18n.h"
#include "Biblioteca.h"

namespace racoon::biblio
{
using namespace juce;

namespace
{
// its own colours, readable whatever look the plugin paints with
const Colour fondo (0xfff7f5f2), tinta (0xff1f1a1c), suave (0xff6b6266), marca (0xffe7c84a), linea (0xffd9d3cc);
Component::SafePointer<PanelMuestras>& abierto() { static Component::SafePointer<PanelMuestras> p; return p; }
}

void PanelMuestras::mostrar (Component& inside, const String& titulo, const StringArray& sugeridas,
                             std::function<void (const String&)> onElegir, const String& actual)
{
    cerrarTodo();
    auto* top = inside.getTopLevelComponent();
    if (top == nullptr) top = &inside;
    auto* p = new PanelMuestras (titulo, sugeridas, std::move (onElegir), actual);
    top->addAndMakeVisible (p);
    // on the right side, the full height: the plugin stays visible on the left so you hear and see what changes
    const auto area = top->getLocalBounds().reduced (10);
    p->setBounds (area.withLeft (jmax (area.getX(), area.getRight() - 380)));
    p->toFront (true);
    p->lista.grabKeyboardFocus();
    abierto() = p;
}

void PanelMuestras::cerrarTodo()
{
    if (auto* p = abierto().getComponent()) p->cerrar();
}

PanelMuestras::PanelMuestras (const String& t, const StringArray& sugeridas, std::function<void (const String&)> cb, const String& act)
    : titulo (t), onElegir (std::move (cb)), actual (act)
{
    const auto& c = Catalogo::get();
    for (auto& s : sugeridas) if (! cats.contains (s)) cats.add (s);
    sugeridasCount = cats.size();
    for (auto& g : c.grupos) if (! cats.contains (g.cat) && g.cat != "loop") cats.add (g.cat);
    if (sugeridasCount == 0) sugeridasCount = cats.size();

    cerrarBtn.setTooltip (String::fromUTF8 ("Guardar el panel (Esc). El sonido que elegiste se queda."));
    cerrarBtn.setColour (TextButton::buttonColourId, tinta);
    cerrarBtn.setColour (TextButton::textColourOffId, fondo);
    cerrarBtn.onClick = [this] { cerrar(); };
    addAndMakeVisible (cerrarBtn);

    // the category the current sound belongs to opens first
    int start = 0;
    if (auto* g = c.grupo (actual)) start = jmax (0, cats.indexOf (g->cat));
    for (int i = 0; i < cats.size(); ++i)
    {
        int n = 0;
        for (auto& g : c.grupos) if (g.cat == cats[i]) ++n;
        categoria.addItem (c.nombreCategoria (cats[i]) + "  (" + String (n) + ")", i + 1);
        if (i == sugeridasCount - 1 && sugeridasCount < cats.size()) categoria.addSeparator();
    }
    categoria.setSelectedItemIndex (start, dontSendNotification);
    categoria.onChange = [this] { llenarFuentes(); filtrar(); };
    fuente.onChange = [this] { filtrar(); };
    for (auto* cb2 : { &categoria, &fuente })
    {
        cb2->setColour (ComboBox::backgroundColourId, Colours::white);
        cb2->setColour (ComboBox::textColourId, tinta);
        cb2->setColour (ComboBox::outlineColourId, tinta);
        cb2->setColour (ComboBox::arrowColourId, tinta);
        addAndMakeVisible (cb2);
    }
    buscar.setTextToShowWhenEmpty (::racoon::i18n::tr (String::fromUTF8 ("Buscar en esta categoría...")), suave);
    buscar.setColour (TextEditor::backgroundColourId, Colours::white);
    buscar.setColour (TextEditor::textColourId, tinta);
    buscar.setColour (TextEditor::outlineColourId, tinta);
    buscar.onTextChange = [this] { filtrar(); };
    buscar.onEscapeKey = [this] { cerrar(); };
    addAndMakeVisible (buscar);

    lista.setModel (this);
    lista.setRowHeight (24);
    lista.setColour (ListBox::backgroundColourId, Colours::white);
    lista.setColour (ListBox::outlineColourId, tinta);
    lista.setOutlineThickness (1);
    addAndMakeVisible (lista);

    pie.setColour (Label::textColourId, suave);
    pie.setFont (FontOptions (12.0f));
    addAndMakeVisible (pie);

    setWantsKeyboardFocus (true);
    llenarFuentes();
    filtrar();
    // the sound in use, selected and in view
    for (int i = 0; i < (int) visibles.size(); ++i)
        if (c.grupos[(size_t) visibles[(size_t) i]].id == actual) { lista.selectRow (i, false, true); lista.scrollToEnsureRowIsOnscreen (i); break; }
}

void PanelMuestras::llenarFuentes()
{
    const auto& c = Catalogo::get();
    const auto cat = cats[categoria.getSelectedItemIndex()];
    StringArray packs;
    for (auto& g : c.grupos) if (g.cat == cat) packs.addIfNotAlreadyThere (g.pack);
    packs.sort (true);
    fuente.clear (dontSendNotification);
    fuente.addItem ("Todas las fuentes", 1);
    for (int i = 0; i < packs.size(); ++i) fuente.addItem (packs[i], i + 2);
    fuente.setSelectedId (1, dontSendNotification);
}

void PanelMuestras::filtrar()
{
    const auto& c = Catalogo::get();
    const auto cat = cats[categoria.getSelectedItemIndex()];
    const auto pack = fuente.getSelectedId() > 1 ? fuente.getText() : String();
    const auto words = StringArray::fromTokens (buscar.getText().toLowerCase(), " ", "");
    visibles.clear();
    for (int i = 0; i < (int) c.grupos.size(); ++i)
    {
        const auto& g = c.grupos[(size_t) i];
        if (g.cat != cat || (pack.isNotEmpty() && g.pack != pack)) continue;
        const auto hay = (g.nombre + " " + g.pack + " " + g.etiquetas.joinIntoString (" ")).toLowerCase();
        bool ok = true;
        for (auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) { ok = false; break; }
        if (ok) visibles.push_back (i);
    }
    lista.updateContent();
    lista.deselectAllRows();
    pie.setText (String ((int) visibles.size()) + String::fromUTF8 (" sonidos. Clic o flechas: se carga y suena. Esc: cerrar."), dontSendNotification);
    repaint();
}

int PanelMuestras::getNumRows() { return (int) visibles.size(); }

void PanelMuestras::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) visibles.size()) return;
    const auto& gr = Catalogo::get().grupos[(size_t) visibles[(size_t) row]];
    if (selected) { g.setColour (marca); g.fillRect (0, 0, w, h); }
    else if (gr.id == actual) { g.setColour (marca.withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
    g.setColour (tinta);
    g.setFont (FontOptions (14.0f));
    g.drawText (::racoon::i18n::tr (gr.nombre), 10, 0, w - 90, h, Justification::centredLeft, true);
    g.setColour (suave);
    g.setFont (FontOptions (11.5f));
    String extra = gr.capas > 1 ? String (gr.capas) + " capas" : String();
    if (gr.dur > 0.0f) extra << (extra.isEmpty() ? "" : "  ") << String (gr.dur, 1) << " s";
    g.drawText (::racoon::i18n::tr (extra), w - 86, 0, 80, h, Justification::centredRight);
    g.setColour (linea);
    g.fillRect (0, h - 1, w, 1);
}

void PanelMuestras::selectedRowsChanged (int row)
{
    if (row < 0 || row >= (int) visibles.size()) return;
    actual = Catalogo::get().grupos[(size_t) visibles[(size_t) row]].id;
    if (onElegir) onElegir (actual);
    lista.repaint();
}

bool PanelMuestras::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey) { cerrar(); return true; }
    return false;
}

void PanelMuestras::cerrar()
{
    setVisible (false);
    // deleted after this click is finished with it
    MessageManager::callAsync ([sp = SafePointer<PanelMuestras> (this)] { if (auto* p = sp.getComponent()) delete p; });
}

void PanelMuestras::paint (Graphics& g)
{
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRect (getLocalBounds().toFloat().translated (4.0f, 4.0f));
    auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4);
    g.setColour (fondo);
    g.fillRect (r);
    g.setColour (tinta);
    g.drawRect (r, 2);
    g.setFont (FontOptions (17.0f, Font::bold));
    g.drawText (::racoon::i18n::tr (titulo), r.reduced (12, 0).removeFromTop (40).withTrimmedRight (90), Justification::centredLeft, true);
}

void PanelMuestras::resized()
{
    auto r = getLocalBounds().withTrimmedRight (4).withTrimmedBottom (4).reduced (10);
    auto head = r.removeFromTop (30);
    cerrarBtn.setBounds (head.removeFromRight (84));
    r.removeFromTop (8);
    categoria.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    fuente.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    buscar.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    pie.setBounds (r.removeFromBottom (20));
    lista.setBounds (r);
}
}

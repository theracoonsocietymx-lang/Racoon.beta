#include "Menus.h"
#include "fx/NativeFx.h"

namespace racoon::ui::Menus
{
using namespace juce;

PopupMenu instrumentMenu (Session& s, ItemCallback cb)
{
    PopupMenu m;
    m.addSectionHeader ("Familia Racoon");
    for (auto& it : Catalog::racoonItems (s.engine))
        if (Catalog::listedAsInstrument (it))
            m.addItem (it.available ? it.name : it.name + String::fromUTF8 (" (no instalado)"), it.available, false, [cb, it] { cb (it); });
    PopupMenu ext;
    for (auto& it : Catalog::externalItems (s.engine))
        if (it.isInstrument) ext.addItem (it.name + "  ·  "_u + it.desc.manufacturerName, [cb, it] { cb (it); });
    if (ext.getNumItems() > 0) m.addSubMenu ("Tus plugins", ext);
    else m.addItem (String::fromUTF8 ("Tus plugins: usa «Buscar plugins» en el navegador"), false, false, [] {});
    return m;
}

PopupMenu effectMenu (Session& s, ItemCallback cb)
{
    PopupMenu m;
    m.addSectionHeader ("Efectos de Racoon");
    for (auto& cat : fx::categories())
    {
        PopupMenu sub;
        for (auto& it : Catalog::nativeEffects (cat))
            sub.addItem (it.name, it.available, false, [cb, it] { cb (it); });
        if (sub.getNumItems() > 0) m.addSubMenu (cat, sub);
    }
    bool family = false;
    for (auto& it : Catalog::racoonItems (s.engine))
        if (Catalog::isMasterProduct (it))
        {
            if (! family) { m.addSectionHeader (String::fromUTF8 ("Familia Racoon · Master")); family = true; }
            m.addItem (it.available ? it.name : it.name + String::fromUTF8 (" (no instalado)"), it.available, false, [cb, it] { cb (it); });
        }
    PopupMenu inc;
    for (auto& it : Catalog::builtInItems())
        if (! it.isInstrument) inc.addItem (it.name, [cb, it] { cb (it); });
    if (inc.getNumItems() > 0) m.addSubMenu ("MIDI", inc);
    PopupMenu ext;
    for (auto& it : Catalog::externalItems (s.engine))
        if (! it.isInstrument) ext.addItem (it.name + "  ·  "_u + it.desc.manufacturerName, [cb, it] { cb (it); });
    if (ext.getNumItems() > 0) m.addSubMenu ("Tus plugins", ext);
    return m;
}
}

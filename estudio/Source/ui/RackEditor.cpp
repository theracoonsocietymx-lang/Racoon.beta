#include "RackEditor.h"
#include "engine/I18n.h"
#include "ChannelView.h"
#include "Menus.h"
#include "Spanish.h"
#include "engine/Devices.h"

namespace racoon::ui
{
using namespace juce;

namespace
{
constexpr int macroH = 120, rowH = 196, headW = 150, cardW = 250, knobW = 58;

// macros: right click lists what each one moves
void macroMenu (ViewState& vs, te::Plugin& rack, te::AutomatableParameter& macro, PopupMenu& m)
{
    auto targets = devices::mappedToMacro (macro, rack);
    m.addSeparator();
    if (targets.isEmpty()) { m.addItem ("Esta macro todavía no mueve nada. Clic derecho en una perilla de adentro para asignarla."_u, false, false, [] {}); return; }
    PopupMenu sub;
    te::AutomatableParameter::Ptr mac (&macro);
    for (auto* t : targets)
    {
        te::AutomatableParameter::Ptr prm (t);
        String n = (t->getPlugin() != nullptr ? Catalog::displayName (*t->getPlugin()) + " · "_u : String()) + ParamKnob::labelFor (*t);
        sub.addItem (n, [&vs, mac, prm] { vs.edit().getUndoManager().beginNewTransaction(); devices::unmapFromMacro (*mac, *prm); });
    }
    m.addSubMenu ("Dejar de mover", sub);
}

// any knob inside the rack: assign it to one of the macros
void assignMenu (ViewState& vs, te::Plugin& rack, te::AutomatableParameter& param, PopupMenu& m)
{
    PopupMenu sub;
    te::AutomatableParameter::Ptr prm (&param);
    for (auto* mac : devices::macros (rack))
    {
        te::AutomatableParameter::Ptr macro (mac);
        PopupMenu amt;
        for (float a : { 1.0f, 0.5f, 0.25f, -0.5f, -1.0f })
            amt.addItem ((a > 0 ? "+" : "") + String (roundToInt (a * 100.0f)) + " % del rango", [&vs, macro, prm, a]
            { vs.edit().getUndoManager().beginNewTransaction(); devices::mapToMacro (*macro, *prm, a); });
        amt.addItem ("Quitar", [&vs, macro, prm] { vs.edit().getUndoManager().beginNewTransaction(); devices::unmapFromMacro (*macro, *prm); });
        sub.addSubMenu (mac->getParameterName(), amt);
    }
    m.addSeparator();
    m.addSubMenu ("Asignar a una macro", sub);
}

// an effect inside a chain
class InnerCard : public Component, private Timer
{
public:
    InnerCard (ViewState& v, te::Track& t, te::Plugin& rackInst, te::Plugin& p) : plugin (&p), rack (&rackInst), vs (v), track (t)
    {
        power.setButtonText ("ON");
        power.setClickingTogglesState (true);
        power.setToggleState (p.isEnabled(), dontSendNotification);
        power.onClick = [this] { vs.edit().getUndoManager().beginNewTransaction(); plugin->setEnabled (power.getToggleState()); };
        open.onClick = [this] { DeviceCard::openDevice (vs, track, *plugin); };
        left.setButtonText ("<"); right.setButtonText (">"); del.setButtonText ("X");
        left.setTooltip ("Mover antes en la cadena"); right.setTooltip ("Mover después en la cadena"_u); del.setTooltip ("Quitar del rack");
        auto move = [this] (int d)
        {
            auto* r = devices::rackOf (*rack);
            if (r == nullptr) return;
            const int k = (int) plugin->state.getProperty ("racoonChain", 0);
            const int at = devices::chain (*r, k).indexOf (plugin.get());
            vs.edit().getUndoManager().beginNewTransaction();
            devices::moveInChain (*r, *plugin, at + d);
        };
        left.onClick = [move] { move (-1); };
        right.onClick = [move] { move (1); };
        del.onClick = [this]
        {
            auto p = plugin; auto rk = rack;
            MessageManager::callAsync ([p, rk, &edit = vs.edit()]
            {
                if (auto* r = devices::rackOf (*rk)) { edit.getUndoManager().beginNewTransaction(); devices::removeFromRack (*r, *p); }
            });
        };
        for (auto* b : { &power, &open, &left, &right, &del }) addAndMakeVisible (b);
        auto params = DeviceCard::cardParameters (p);
        for (int i = 0; i < jmin (4, params.size()); ++i)
        {
            auto* k = knobs.add (new ParamKnob (vs, t, *params[i]));
            te::Plugin::Ptr rk = rack;
            te::AutomatableParameter::Ptr prm (params[i]);
            k->extraMenu = [this, rk, prm] (PopupMenu& m) { assignMenu (vs, *rk, *prm, m); };
            addAndMakeVisible (k);
        }
        startTimerHz (4);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 5);
        auto top = r.removeFromTop (22);
        power.setBounds (top.removeFromLeft (34));
        del.setBounds (top.removeFromRight (24));
        right.setBounds (top.removeFromRight (22));
        left.setBounds (top.removeFromRight (22));
        top.removeFromRight (4);
        open.setBounds (top.removeFromRight (56));
        r.removeFromTop (22);
        if (knobs.isEmpty()) return;
        const int w = r.getWidth() / knobs.size();
        for (auto* k : knobs) k->setBounds (r.removeFromLeft (w).withSizeKeepingCentre (jmin (w, knobW), jmin (r.getHeight(), 96)));
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        Draw::box (g, r, plugin->isEnabled() ? S::card : S::cardDim, 2.0f, 3.0f);
        g.setColour (S::ink);
        g.setFont (Fonts::bodyBold (13.0f));
        g.drawFittedText (::racoon::i18n::tr (Catalog::displayName (*plugin)), getLocalBounds().reduced (10, 0).withY (28).withHeight (20), Justification::centredLeft, 1, 0.8f);
        if (knobs.isEmpty())
        {
            g.setColour (S::inkSoft);
            g.setFont (Fonts::body (12.0f));
            g.drawFittedText (::racoon::i18n::tr ("Doble clic o ABRIR para ver sus controles."_u), getLocalBounds().reduced (10, 0).withTrimmedTop (52), Justification::centred, 2);
        }
    }
    void mouseDoubleClick (const MouseEvent&) override { DeviceCard::openDevice (vs, track, *plugin); }
    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        PopupMenu m;
        m.addItem ("Abrir (o doble clic)", [this] { DeviceCard::openDevice (vs, track, *plugin); });
        m.addItem (plugin->isEnabled() ? "Apagar (bypass)" : "Encender", [this] { power.triggerClick(); });
        m.addItem ("Mover antes", [this] { left.triggerClick(); });
        m.addItem (String::fromUTF8 ("Mover después"), [this] { right.triggerClick(); });
        m.addSeparator();
        m.addItem ("Quitar del rack", [this] { del.triggerClick(); });
        m.showMenuAsync (PopupMenu::Options().withMousePosition());
    }
    te::Plugin::Ptr plugin, rack;
private:
    ViewState& vs;
    te::Track& track;
    TextButton power, open { "ABRIR" }, left, right, del;
    OwnedArray<ParamKnob> knobs;
    void timerCallback() override { power.setToggleState (plugin->isEnabled(), dontSendNotification); repaint(); }
};

class RackWindow : public DocumentWindow
{
public:
    RackWindow (ViewState& vs, te::Track& t, te::Plugin& rack)
        : DocumentWindow ("Rack de efectos · "_u + t.getName(), S::shade, DocumentWindow::closeButton)
    {
       #if JUCE_MAC
        setUsingNativeTitleBar (true);
       #endif
        setContentOwned (new RackEditor (vs, t, rack), true);
        setResizable (true, false);
        centreWithSize (1000, 640);
        setVisible (true);
    }
    void closeButtonPressed() override;
};

OwnedArray<RackWindow>& windows() { static OwnedArray<RackWindow> w; return w; }
void RackWindow::closeButtonPressed() { MessageManager::callAsync ([this] { windows().removeObject (this); }); }
}

// ===================================================================== chain row
struct RackEditor::ChainRow : public Component
{
    ChainRow (RackEditor& o, int k) : owner (o), index (k)
    {
        auto* r = devices::rackOf (*o.rack);
        if (auto* vol = devices::chainVolume (*r, k))
            if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*> (vol))
            {
                volume = std::make_unique<ParamKnob> (o.vs, o.track, *vp->volParam);
                pan = std::make_unique<ParamKnob> (o.vs, o.track, *vp->panParam);
                te::Plugin::Ptr rk = o.rack;
                for (auto* kn : { volume.get(), pan.get() })
                {
                    te::AutomatableParameter::Ptr prm (kn->param);
                    kn->extraMenu = [this, rk, prm] (PopupMenu& m) { assignMenu (owner.vs, *rk, *prm, m); };
                    addAndMakeVisible (kn);
                }
            }
        add.onClick = [this]
        {
            te::Plugin::Ptr rk = owner.rack;
            const int k = index;
            auto& vs = owner.vs;
            Menus::effectMenu (vs.session, [rk, k, &vs] (const CatalogItem& it)
            {
                auto* r = devices::rackOf (*rk);
                if (r == nullptr) return;
                if (it.source == CatalogItem::Source::native && devices::isModulatorId (it.builtInType))
                {
                    if (vs.showHint) vs.showHint ("Los moduladores van en el canal, no dentro del rack."_u);
                    return;
                }
                vs.edit().getUndoManager().beginNewTransaction();
                auto p = Catalog::create (vs.edit(), it);
                if (p == nullptr || p->isSynth() || dynamic_cast<te::RackInstance*> (p.get()) != nullptr)
                {
                    if (vs.showHint) vs.showHint ("Ese dispositivo no puede ir dentro de un rack."_u);
                    return;
                }
                devices::addToChain (*r, k, p);
            }).showMenuAsync (PopupMenu::Options().withTargetComponent (add));
        };
        add.setTooltip ("Agregar un efecto al final de esta cadena");
        addAndMakeVisible (add);
        del.setTooltip ("Quitar esta cadena con sus efectos");
        del.onClick = [this]
        {
            auto rk = owner.rack; const int k = index;
            MessageManager::callAsync ([rk, k, &edit = owner.vs.edit()]
            {
                if (auto* r = devices::rackOf (*rk)) { edit.getUndoManager().beginNewTransaction(); devices::removeChain (*r, k); }
            });
        };
        del.setEnabled (devices::numChains (*r) > 1);
        addAndMakeVisible (del);
        for (auto* p : devices::chain (*r, k))
            addAndMakeVisible (cards.add (new InnerCard (o.vs, o.track, *o.rack, *p)));
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto head = r.removeFromLeft (headW);
        head.removeFromTop (26);
        auto buttons = head.removeFromBottom (26);
        del.setBounds (buttons.removeFromRight (30));
        buttons.removeFromRight (4);
        add.setBounds (buttons);
        if (volume != nullptr)
        {
            auto kn = head.reduced (0, 4);
            volume->setBounds (kn.removeFromLeft (kn.getWidth() / 2));
            pan->setBounds (kn);
        }
        r.removeFromLeft (12);
        for (auto* c : cards) { c->setBounds (r.removeFromLeft (cardW)); r.removeFromLeft (16); }
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (S::cardDim.withAlpha (0.5f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (S::ink);
        g.setFont (Fonts::label (13.0f));
        g.drawText (::racoon::i18n::tr ("CADENA " + String (index + 1)), getLocalBounds().reduced (12, 6).removeFromTop (20), Justification::topLeft);
        if (cards.isEmpty())
        {
            g.setColour (S::inkSoft);
            g.setFont (Fonts::body (13.0f));
            g.drawText (::racoon::i18n::tr ("Vacía: deja pasar el sonido tal cual. Pulsa + EFECTO."_u), getLocalBounds().withTrimmedLeft (headW + 20), Justification::centredLeft);
        }
        // arrows between effects
        g.setColour (S::ink);
        for (int i = 0; i + 1 < cards.size(); ++i)
        {
            const float y = (float) cards[i]->getBounds().getCentreY(), x0 = (float) cards[i]->getRight() + 2.0f, x1 = x0 + 12.0f;
            g.drawLine (x0, y, x1 - 3.0f, y, 2.0f);
            Path p; p.addTriangle (x1, y, x1 - 6.0f, y - 5.0f, x1 - 6.0f, y + 5.0f);
            g.fillPath (p);
        }
    }
    int preferredWidth() const { return headW + 30 + cards.size() * (cardW + 16); }

    RackEditor& owner;
    const int index;
    std::unique_ptr<ParamKnob> volume, pan;
    TextButton add { "+ EFECTO" }, del { "X" };
    OwnedArray<InnerCard> cards;
};

// ===================================================================== editor
RackEditor::RackEditor (ViewState& v, te::Track& t, te::Plugin& r) : rack (&r), vs (v), track (t)
{
    viewport.setViewedComponent (&content, false);
    addAndMakeVisible (viewport);
    addChain.setTooltip ("Una cadena nueva suena en paralelo con las demás"_u);
    addChain.onClick = [this]
    {
        if (auto* rt = devices::rackOf (*rack)) { vs.edit().getUndoManager().beginNewTransaction(); devices::addChain (*rt); }
    };
    content.addAndMakeVisible (addChain);
    for (auto* m : devices::macros (r))
    {
        auto* k = macroKnobs.add (new ParamKnob (vs, t, *m));
        te::AutomatableParameter::Ptr mac (m);
        k->extraMenu = [this, mac] (PopupMenu& menu) { macroMenu (vs, *rack, *mac, menu); };
        addAndMakeVisible (k);
    }
    rebuild();
    setSize (1000, 640);
    startTimerHz (4);
}

RackEditor::~RackEditor() = default;

String RackEditor::makeSignature() const
{
    auto* r = devices::rackOf (*rack);
    if (r == nullptr) return {};
    String s (devices::numChains (*r));
    for (int k = 0; k < devices::numChains (*r); ++k)
    {
        s << "|";
        for (auto* p : devices::chain (*r, k)) s << String::toHexString ((pointer_sized_int) p) << ",";
    }
    return s;
}

void RackEditor::rebuild()
{
    signature = makeSignature();
    chainRows.clear();
    if (auto* r = devices::rackOf (*rack))
        for (int k = 0; k < devices::numChains (*r); ++k)
            content.addAndMakeVisible (chainRows.add (new ChainRow (*this, k)));
    layoutContent();
    repaint();
}

void RackEditor::timerCallback()
{
    // the rack may have been deleted from the channel
    bool alive = false;
    for (auto* p : track.pluginList.getPlugins()) alive = alive || p == rack.get();
    if (! alive)
    {
        stopTimer();
        MessageManager::callAsync ([r = rack] { for (auto* w : windows()) if (auto* e = dynamic_cast<RackEditor*> (w->getContentComponent()); e != nullptr && e->rack == r) { windows().removeObject (w); return; } });
        return;
    }
    if (makeSignature() != signature) rebuild();
}

void RackEditor::layoutContent()
{
    int w = viewport.getWidth() - 12, y = 6;
    for (auto* c : chainRows) w = jmax (w, c->preferredWidth());
    for (auto* c : chainRows) { c->setBounds (6, y, w, rowH); y += rowH + 10; }
    addChain.setBounds (12, y, 150, 30);
    content.setSize (w + 12, y + 44);
}

void RackEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (macroH).reduced (16, 10);
    top.removeFromTop (22);
    const int w = macroKnobs.isEmpty() ? 0 : jmin (90, top.getWidth() / macroKnobs.size());
    for (auto* k : macroKnobs) k->setBounds (top.removeFromLeft (w));
    viewport.setBounds (r.reduced (6));
    layoutContent();
}

void RackEditor::paint (Graphics& g)
{
    g.fillAll (S::shade);
    auto head = getLocalBounds().removeFromTop (macroH).reduced (16, 8);
    g.setColour (S::ink);
    g.setFont (Fonts::title (15.0f));
    g.drawText (::racoon::i18n::tr ("MACROS"), head.removeFromTop (20), Justification::topLeft);
    g.setFont (Fonts::body (12.0f));
    g.setColour (S::inkSoft);
    g.drawText (::racoon::i18n::tr ("Clic derecho en cualquier perilla de una cadena para que una macro la mueva. Las cadenas suenan juntas, en paralelo."_u),
                getLocalBounds().removeFromTop (macroH).reduced (16, 8).withTrimmedLeft (110).removeFromTop (20), Justification::topLeft);
    g.setColour (S::ink.withAlpha (0.2f));
    g.fillRect (12, macroH - 2, getWidth() - 24, 2);
}

void RackEditor::open (ViewState& vs, te::Track& t, te::Plugin& rack)
{
    if (auto* e = findOpen (rack))
    {
        if (auto* w = e->findParentComponentOfClass<DocumentWindow>()) w->toFront (true);
        return;
    }
    windows().add (new RackWindow (vs, t, rack));
}

RackEditor* RackEditor::findOpen (te::Plugin& rack)
{
    for (auto* w : windows())
        if (auto* e = dynamic_cast<RackEditor*> (w->getContentComponent()); e != nullptr && e->rack.get() == &rack) return e;
    return nullptr;
}

void RackEditor::closeAll() { windows().clear(); }
}

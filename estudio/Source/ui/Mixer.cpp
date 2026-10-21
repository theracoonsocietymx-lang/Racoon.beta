#include "Mixer.h"
#include "engine/I18n.h"
#include "fx/core/Upper.h"
#include "Menus.h"

namespace racoon::ui
{
using namespace juce;

// ===================================================================== strip
ChannelStrip::ChannelStrip (ViewState& v, te::Track::Ptr t) : track (t), vs (v)
{
    instrumentSlot.setTooltip ("Instrumento de la pista: clic para abrir su ventana");
    instrumentSlot.onClick = [this]
    {
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            if (auto* p = Session::instrumentOf (*at)) p->showWindowExplicitly();
    };
    addChildComponent (instrumentSlot);

    addFx.setTooltip (String::fromUTF8 ("Añadir un efecto a esta pista (también puedes arrastrarlo desde el navegador)"));
    addFx.onClick = [this]
    {
        auto* list = pluginList();
        if (list == nullptr) return;
        Menus::effectMenu (vs.session, [this] (const CatalogItem& it)
        {
            vs.edit().getUndoManager().beginNewTransaction();
            if (track != nullptr) vs.session.insertItem (*track, it);
            else if (auto p = Catalog::create (vs.edit(), it)) vs.edit().getMasterPluginList().insertPlugin (p, vs.edit().getMasterPluginList().size(), nullptr);
        }).showMenuAsync (PopupMenu::Options().withTargetComponent (addFx));
    };
    addAndMakeVisible (addFx);

    pan.slider.setRange (-1.0, 1.0);
    pan.slider.setDoubleClickReturnValue (true, 0.0);
    pan.slider.getProperties().set ("bipolar", true);
    pan.slider.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
    pan.slider.onValueChange = [this] { if (auto* vp = volumePlugin()) vp->setPan ((float) pan.slider.getValue()); };
    addAndMakeVisible (pan);
    pan.slider.onPopupMenu = [this] { if (track != nullptr) if (auto* vp = volumePlugin()) showAutomationMenu (vs, *track, vp->panParam.get(), pan); };
    fader.onPopupMenu = [this] { if (track != nullptr) if (auto* vp = volumePlugin()) showAutomationMenu (vs, *track, vp->volParam.get(), fader); };

    fader.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
    fader.onValueChange = [this] { if (auto* vp = volumePlugin()) vp->setSliderPos ((float) fader.getValue()); };
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);
    addAndMakeVisible (peakBox);

    if (track != nullptr)
    {
        mute.setClickingTogglesState (false); solo.setClickingTogglesState (false);
        mute.setTooltip ("Silenciar"); solo.setTooltip ("Solo");
        mute.onClick = [this] { vs.edit().getUndoManager().beginNewTransaction(); vs.session.setTrackMute (*track, ! vs.session.isTrackMuted (*track)); refresh(); };
        solo.onClick = [this] { vs.edit().getUndoManager().beginNewTransaction(); vs.session.setTrackSolo (*track, ! vs.session.isTrackSoloed (*track)); refresh(); };
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
        if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
            if (auto* lm = at->getLevelMeterPlugin()) meter.attach (&lm->measurer, lm);
    }

    buildSlots();
    refresh();
    startTimerHz (10);
}

ChannelStrip::~ChannelStrip()
{
    // the master meter reads the playback context, which may have been freed (for example by a render)
    if (track == nullptr && vs.edit().getCurrentPlaybackContext() != masterContext) meter.forget();
    meter.attach (nullptr);
}

te::VolumeAndPanPlugin* ChannelStrip::volumePlugin() const
{
    if (track == nullptr) return vs.edit().getMasterVolumePlugin().get();
    if (auto* at = dynamic_cast<te::AudioTrack*> (track.get())) return at->getVolumePlugin();
    if (auto* f = dynamic_cast<te::FolderTrack*> (track.get())) return f->getVolumePlugin();
    return nullptr;
}

te::PluginList* ChannelStrip::pluginList() const
{
    if (track == nullptr) return &vs.edit().getMasterPluginList();
    return &track->pluginList;
}

void ChannelStrip::buildSlots()
{
    slots.clear();
    sends.clear();
    sendBuses.clear();

    Array<te::Plugin*> inserts;
    if (track != nullptr) inserts = Session::insertsOf (*track);
    else
        for (auto* p : vs.edit().getMasterPluginList().getPlugins())
            if (dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr) inserts.add (p);

    for (auto* p : inserts)
    {
        te::Plugin::Ptr pp (p);
        auto* b = slots.add (new TextButton (Catalog::displayName (*p)));
        b->setColour (TextButton::buttonColourId, p->isEnabled() ? S::card : S::cardDim);
        b->setTooltip (Catalog::displayName (*p) + String::fromUTF8 (": clic abre la ventana; clic derecho, más opciones"));
        b->onClick = [pp] { if (! ModifierKeys::currentModifiers.isPopupMenu()) pp->showWindowExplicitly(); };
        b->addMouseListener (this, false);
        b->getProperties().set ("pluginID", pp->itemID.toString());
        addAndMakeVisible (b);
    }

    if (auto* at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        const bool isReturn = kindOf (*at) == TrackKind::returnBus;
        if (! vs.easyMode && ! isReturn)
        {
            const auto names = vs.session.returnBusNames();
            Array<int> buses;
            for (auto* t : te::getAudioTracks (vs.edit()))
                for (auto* rp : t->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
                    buses.addIfNotAlreadyThere ((int) rp->busNumber);
            buses.sort();
            for (int bus : buses)
            {
                auto* k = sends.add (new Knob (upper (names[bus]).substring (0, 8),
                                               String::fromUTF8 ("Envío al retorno ") + names[bus] + String::fromUTF8 (". Todo a la izquierda: apagado.")));
                k->slider.setRange (-60.0, 6.0);
                k->slider.setDoubleClickReturnValue (true, -60.0);
                k->slider.onDragStart = [this] { vs.edit().getUndoManager().beginNewTransaction(); };
                k->slider.onPopupMenu = [this, k, bus, at]
                {
                    // the send needs to exist to have a line; it starts switched off
                    const bool existed = vs.session.getSend (*at, bus, false) != nullptr;
                    auto* snd = vs.session.getSend (*at, bus, true);
                    if (snd != nullptr && ! existed) snd->setGainDb (-100.0f);
                    if (snd != nullptr) showAutomationMenu (vs, *at, snd->gain.get(), *k);
                };
                k->slider.onValueChange = [this, k, bus, at]
                {
                    const double db = k->slider.getValue();
                    if (auto* s = vs.session.getSend (*at, bus, db > -59.5))
                        s->setGainDb (db <= -59.5 ? -100.0f : (float) db);
                };
                if (auto* s = vs.session.getSend (*at, bus, false))
                    k->slider.setValue (jmax (-60.0f, s->getGainDb()), dontSendNotification);
                else
                    k->slider.setValue (-60.0, dontSendNotification);
                addAndMakeVisible (k);
                sendBuses.add (bus);
            }
        }
        instrumentSlot.setVisible (Session::instrumentOf (*at) != nullptr);
        if (auto* p = Session::instrumentOf (*at))
            instrumentSlot.setButtonText ((Catalog::isRacoonPlugin (*p) ? Catalog::titleFor (p->getName()) : Catalog::displayName (*p)).toUpperCase());
    }
    resized();
}

void ChannelStrip::showSlotMenu (te::Plugin* plugin, Component* target)
{
    te::Plugin::Ptr p (plugin);
    auto* list = pluginList();
    PopupMenu m;
    m.addItem ("Abrir", [p] { p->showWindowExplicitly(); });
    m.addItem (p->isEnabled() ? "Desactivar (bypass)" : "Activar", [this, p] { p->setEnabled (! p->isEnabled()); buildSlots(); });
    const int idx = list->indexOf (p.get());
    m.addItem ("Subir", idx > 0, false, [this, p, list, idx]
    {
        vs.edit().getUndoManager().beginNewTransaction();
        p->removeFromParent();
        list->insertPlugin (p, jmax (0, idx - 1), nullptr);
    });
    m.addItem ("Bajar", [this, p, list, idx]
    {
        vs.edit().getUndoManager().beginNewTransaction();
        p->removeFromParent();
        list->insertPlugin (p, idx + 1, nullptr);
    });
    if (p->canSidechain() && track != nullptr)
    {
        PopupMenu sc;
        sc.addItem ("Ninguna", true, p->getSidechainSourceID() == te::EditItemID(), [this, p] { vs.session.setSidechain (*p, nullptr); });
        for (auto* t : te::getAudioTracks (vs.edit()))
        {
            if (t == track.get()) continue;
            te::Track::Ptr src (t);
            sc.addItem (t->getName(), true, p->getSidechainSourceID() == t->itemID, [this, p, src] { vs.session.setSidechain (*p, src.get()); });
        }
        m.addSubMenu (String::fromUTF8 ("Sidechain desde"), sc);
    }
    m.addSeparator();
    m.addItem ("Quitar", [this, p] { vs.edit().getUndoManager().beginNewTransaction(); p->deleteFromParent(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void ChannelStrip::mouseDown (const MouseEvent& e)
{
    if (auto* b = dynamic_cast<TextButton*> (e.eventComponent); b != nullptr && e.mods.isPopupMenu() && slots.contains (b))
    {
        const auto id = b->getProperties()["pluginID"].toString();
        if (auto p = te::findPluginForID (vs.edit(), te::EditItemID::fromVar (id)))
            showSlotMenu (p.get(), b);
        return;
    }
    if (track != nullptr && e.eventComponent == this)
        vs.session.getSelection().selectOnly (track.get());
}

void ChannelStrip::refresh()
{
    if (auto* vp = volumePlugin())
    {
        fader.setValue (vp->getSliderPos(), dontSendNotification);
        pan.slider.setValue (vp->getPan(), dontSendNotification);
    }
    if (track != nullptr)
    {
        mute.setToggleState (vs.session.isTrackMuted (*track), dontSendNotification);
        solo.setToggleState (vs.session.isTrackSoloed (*track), dontSendNotification);
    }
}

void ChannelStrip::timerCallback()
{
    if (track == nullptr)
    {
        auto* ctx = vs.edit().getCurrentPlaybackContext();
        if (ctx != masterContext)
        {
            meter.forget();
            masterContext = ctx;
            if (ctx != nullptr) meter.attach (&ctx->masterLevels);
        }
    }
    if (! fader.isMouseButtonDown() && ! pan.slider.isMouseButtonDown()) refresh();
    peakBox.poll();
}

bool ChannelStrip::isReturn() const
{
    return track != nullptr && kindOf (*track) == TrackKind::returnBus;
}

void PeakBox::paint (juce::Graphics& g)
{
    using namespace juce;
    auto r = getLocalBounds().toFloat();
    const bool over = shown > 0.0f;
    g.setColour (over ? Brand::rosa : C::void_);
    g.fillRect (r);
    g.setColour (over ? S::ink : (shown > -6.0f ? Brand::limon : C::bone));
    g.setFont (Fonts::mono (12.5f));
    g.drawText (::racoon::i18n::tr (shown <= -99.0f ? String ("-inf") : String (shown, 1)), r, Justification::centred);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromTop (28);
    if (instrumentSlot.isVisible()) { instrumentSlot.setBounds (r.removeFromTop (24)); r.removeFromTop (6); }
    for (auto* s : slots) { s->setBounds (r.removeFromTop (22)); r.removeFromTop (3); }
    addFx.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    if (! sends.isEmpty())
    {
        auto row = r.removeFromTop (46);
        const int w = row.getWidth() / jmax (2, sends.size());
        for (auto* k : sends) k->setBounds (row.removeFromLeft (w));
        r.removeFromTop (2);
    }
    pan.setBounds (r.removeFromTop (46).withSizeKeepingCentre (54, 46));
    r.removeFromTop (4);
    if (track != nullptr)
    {
        auto ms = r.removeFromBottom (24);
        mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
        solo.setBounds (ms.reduced (2, 0));
        r.removeFromBottom (4);
    }
    // the meter column: the loudest level on top, then the meter with its dB scale beside the fader
    const int meterW = track == nullptr ? 22 : 18;
    auto meterCol = r.removeFromRight (meterW);
    peakBox.setBounds (meterCol.getRight() - (meterW + 22), meterCol.getY(), meterW + 22, 20);
    meterCol.removeFromTop (20);
    meterCol.removeFromTop (4);
    meter.setBounds (meterCol.withTrimmedBottom (22));
    scaleArea = r.removeFromRight (22).withTop (meter.getY()).withBottom (meter.getBottom());
    r.removeFromTop (24);
    fader.setBounds (r);
}

void ChannelStrip::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool selected = track != nullptr && vs.session.getSelection().isSelected (track.get());
    const auto face = dropHighlight ? Brand::limon : selected ? C::stone : isReturn() ? S::cardDim : S::card;
    Draw::box (g, r, face, 1.5f, 3.0f);
    auto head = getLocalBounds().reduced (4, 4).removeFromTop (24);
    const auto col = track != nullptr ? colourForTrack (*track) : Brand::limon;
    g.setColour (col);
    g.fillRect (head.withTrimmedRight (2));
    g.setColour (S::shade);
    g.setFont (Fonts::bodyBold (13.0f));
    g.drawFittedText (::racoon::i18n::tr (track != nullptr ? track->getName() : String ("Master")), head.reduced (6, 0), Justification::centredLeft, 1, 0.8f);

    // dB marks next to the meter, so "how loud" reads at a glance
    if (! scaleArea.isEmpty())
    {
        g.setFont (Fonts::body (10.0f));
        for (int db : { 6, 0, -6, -12, -24, -48 })
        {
            const float y = LevelMeter::yFor ((float) db, meter.getBounds().toFloat());
            g.setColour (db == 0 ? S::ink : S::inkSoft);
            g.fillRect ((float) scaleArea.getRight() - 4.0f, y - 0.5f, 4.0f, 1.0f);
            g.drawText (::racoon::i18n::tr (db > 0 ? "+" + String (db) : String (db)), Rectangle<float> ((float) scaleArea.getX() - 2.0f, y - 6.0f, (float) scaleArea.getWidth() - 4.0f, 12.0f),
                        Justification::centredRight);
        }
    }
}

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& d)
{
    return DragData::isPlugin (d.description) && ! (bool) d.description.getProperty ("instrument", false);
}

void ChannelStrip::itemDropped (const SourceDetails& d)
{
    dropHighlight = false;
    auto it = DragData::item (d.description);
    vs.edit().getUndoManager().beginNewTransaction();
    if (track != nullptr) vs.session.insertItem (*track, it);
    else if (auto p = Catalog::create (vs.edit(), it)) vs.edit().getMasterPluginList().insertPlugin (p, vs.edit().getMasterPluginList().size(), nullptr);
    repaint();
}

// ===================================================================== mixer
MixerView::MixerView (ViewState& v) : vs (v)
{
    content.addReturn.setTooltip (String::fromUTF8 ("Nuevo retorno (reverb, delay o vacío). Las pistas le mandan sonido con sus envíos."));
    content.addReturn.onClick = [this] { showAddReturnMenu(); };
    content.addAndMakeVisible (content.addReturn);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    vs.addChangeListener (this);
    editChanged();
}

MixerView::~MixerView()
{
    watched.removeListener (this);
    vs.removeChangeListener (this);
}

void MixerView::detach()
{
    cancelPendingUpdate();
    watched.removeListener (this);
    watched = {};
    strips.clear();
    master.reset();
}

void MixerView::editChanged()
{
    watched.removeListener (this);
    watched = vs.edit().state;
    watched.addListener (this);
    rebuild();
}

void MixerView::valueTreePropertyChanged (ValueTree& v, const Identifier& id)
{
    // names, colours and plugin bypass change the strips' look; values are polled by the strips
    if (te::TrackList::isTrack (v) && (id == te::IDs::name || id == te::IDs::colour)) triggerAsyncUpdate();
    else if (v.hasType (te::IDs::PLUGIN) && id == te::IDs::enabled) triggerAsyncUpdate();
}

void MixerView::changeListenerCallback (ChangeBroadcaster*)
{
    if (lastEasy != vs.easyMode) { lastEasy = vs.easyMode; rebuild(); }
}

void MixerView::rebuild()
{
    strips.clear();
    // the tracks, then the returns (reverb, delay...), then the master on its own at the right
    Array<te::Track*> returns;
    for (auto& vt : visibleTracks (vs.edit()))
    {
        if (kindOf (*vt.track) == TrackKind::returnBus) { returns.add (vt.track); continue; }
        content.addAndMakeVisible (strips.add (new ChannelStrip (vs, vt.track)));
    }
    firstReturn = strips.size();
    for (auto* t : returns) content.addAndMakeVisible (strips.add (new ChannelStrip (vs, t)));
    master = std::make_unique<ChannelStrip> (vs, nullptr);
    addAndMakeVisible (*master);
    resized();
}

void MixerView::showAddReturnMenu()
{
    auto& s = vs.session;
    int freeBus = 0;
    for (auto* t : te::getAudioTracks (s.getEdit()))
        for (auto* rp : t->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
            freeBus = jmax (freeBus, (int) rp->busNumber + 1);
    PopupMenu m;
    m.addItem ("Retorno con reverb", freeBus < 4, false, [&s, freeBus] { s.getEdit().getUndoManager().beginNewTransaction(); s.addReturnTrack (freeBus, "Reverb", "reverb"); });
    m.addItem ("Retorno con delay", freeBus < 4, false, [&s, freeBus] { s.getEdit().getUndoManager().beginNewTransaction(); s.addReturnTrack (freeBus, "Delay", "delay"); });
    m.addItem (String::fromUTF8 ("Retorno vacío"), freeBus < 4, false, [&s, freeBus] { s.getEdit().getUndoManager().beginNewTransaction(); s.addReturnTrack (freeBus, "Retorno " + String (freeBus + 1), {}); });
    if (freeBus >= 4) m.addSectionHeader (String::fromUTF8 ("Ya hay cuatro retornos, el máximo"));
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (content.addReturn));
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    auto masterArea = r.removeFromRight (ChannelStrip::masterWidth + 4);
    if (master != nullptr) master->setBounds (masterArea.withTrimmedTop (captionH));
    r.removeFromRight (10);
    viewport.setBounds (r);
    const int h = r.getHeight() - (viewport.isHorizontalScrollBarShown() ? 10 : 0);
    const int gap = 16;
    content.setSize (jmax (r.getWidth(), strips.size() * ChannelStrip::stripWidth + gap + 110), h);
    int x = 0;
    content.returnsX = -1;
    content.tracksEnd = 0;
    for (int i = 0; i < strips.size(); ++i)
    {
        if (i == firstReturn) { content.tracksEnd = x; x += gap; content.returnsX = x; }
        strips[i]->setBounds (x, captionH, ChannelStrip::stripWidth, h - captionH);
        x += ChannelStrip::stripWidth;
    }
    if (content.returnsX < 0) { content.tracksEnd = x; content.returnsX = x + gap; }
    content.addReturn.setBounds (content.returnsX + 76, 0, 100, captionH);
}

void MixerView::Content::paint (Graphics& g)
{
    g.setColour (S::ink);
    g.setFont (Fonts::label (12.0f));
    if (tracksEnd > 0) g.drawText (::racoon::i18n::tr ("Pistas"), Rectangle<int> (4, 0, jmax (60, tracksEnd - 8), captionH), Justification::centredLeft);
    if (returnsX >= 0)
    {
        g.drawText (::racoon::i18n::tr ("Retornos"), Rectangle<int> (returnsX + 4, 0, 70, captionH), Justification::centredLeft);
        g.setColour (S::ink.withAlpha (0.35f));
        g.fillRect (returnsX - 9, captionH, 2, getHeight() - captionH);
    }
}

void MixerView::paint (Graphics& g)
{
    g.fillAll (S::shade);
    if (master != nullptr)
    {
        g.setColour (S::ink);
        g.setFont (Fonts::label (12.0f));
        g.drawText (::racoon::i18n::tr ("Salida"), master->getBounds().withY (6).withHeight (captionH).translated (4, 0), Justification::centredLeft);
    }
    if (strips.isEmpty())
    {
        g.setColour (S::inkSoft);
        g.setFont (Fonts::body (15.0f));
        g.drawText (::racoon::i18n::tr (String::fromUTF8 ("Sin pistas todavía")), viewport.getBounds(), Justification::centred);
    }
}
}

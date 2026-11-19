#pragma once
#include "Widgets.h"
#include "DragData.h"

namespace racoon::ui
{
// the loudest level a channel reached, above its fader: click to start counting again
class PeakBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PeakBox (LevelMeter& m) : meter (m) { setTooltip (String::fromUTF8 ("Lo más alto que ha sonado este canal. Clic para reiniciar.")); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { meter.resetMax(); shown = -100.0f; repaint(); }
    void poll() { const float m = meter.getMaxDb(); if (std::abs (m - shown) > 0.05f) { shown = m; repaint(); } }
private:
    LevelMeter& meter;
    float shown = -100.0f;
    using String = juce::String;
};

class ChannelStrip : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    ChannelStrip (ViewState&, te::Track::Ptr track);   // track == nullptr: master
    ~ChannelStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHighlight = false; repaint(); }
    static constexpr int stripWidth = 118, masterWidth = 150;
    te::Track::Ptr track;
    bool isReturn() const;

private:
    ViewState& vs;
    juce::TextButton instrumentSlot;
    juce::OwnedArray<juce::TextButton> slots;
    juce::TextButton addFx { "+ EFECTO" };
    juce::OwnedArray<Knob> sends;
    juce::Array<int> sendBuses;
    Knob pan { "PAN", "Panorama: izquierda / derecha. Doble clic: centro." };
    Fader fader;
    LevelMeter meter;
    PeakBox peakBox { meter };
    GlyphButton mute { Glyph::none, "M", Brand::limon }, solo { Glyph::none, "S", Brand::hielo };
    bool dropHighlight = false;
    te::EditPlaybackContext* masterContext = nullptr;
    juce::Rectangle<int> scaleArea;

    te::VolumeAndPanPlugin* volumePlugin() const;
    te::PluginList* pluginList() const;
    void buildSlots();
    void showSlotMenu (te::Plugin*, juce::Component*);
    void timerCallback() override;
    void refresh();
};

class MixerView : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::AsyncUpdater,
                  private juce::ChangeListener
{
public:
    explicit MixerView (ViewState&);
    ~MixerView() override;
    void editChanged();
    void detach();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ViewState& vs;
    juce::Viewport viewport;
    // tracks first, then the returns (reverb, delay...) after a gap, each group with its caption
    static constexpr int captionH = 24;
    struct Content : public juce::Component
    {
        int returnsX = -1, tracksEnd = 0;
        juce::TextButton addReturn { "+ Retorno" };
        void paint (juce::Graphics&) override;
    } content;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> master;
    int firstReturn = 0;
    juce::ValueTree watched;
    bool lastEasy = true;
    void rebuild();
    void showAddReturnMenu();
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override  { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override          { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override;
    void handleAsyncUpdate() override { rebuild(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};
}

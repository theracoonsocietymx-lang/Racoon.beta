#pragma once
#include "Widgets.h"
#include <optional>
#include "DragData.h"

namespace racoon::ui
{
// One knob of a device card, bound to an automatable parameter of the plugin.
class ParamKnob : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ParamKnob (ViewState&, te::Track&, te::AutomatableParameter&);
    ~ParamKnob() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    te::AutomatableParameter::Ptr param;
    std::function<void (juce::PopupMenu&)> extraMenu;   // more entries for the right-click menu (macros of a rack)
    static juce::String labelFor (te::AutomatableParameter&);
    static juce::String valueText (te::AutomatableParameter&);
private:
    ViewState& vs;
    te::Track& track;
    struct KnobSlider : public GrabSlider
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); } else Slider::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseUp (e); }
    } slider;
    bool dragging = false;
    void timerCallback() override;
    void showMenu();
};

// A device in the chain: instrument or effect. The title bar drags it to a new place.
class DeviceCard : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    DeviceCard (ViewState&, te::Track&, te::Plugin&, int indexInChain);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;   // opens it
    int preferredWidth() const;
    te::Plugin::Ptr plugin;
    const int index;
    static juce::Array<te::AutomatableParameter*> cardParameters (te::Plugin&);
    static void openDevice (ViewState&, te::Track&, te::Plugin&);   // its window, the rack editor or the external effect menu
private:
    ViewState& vs;
    te::Track& track;
    juce::TextButton power, open { "ABRIR" }, remove;
    juce::OwnedArray<ParamKnob> knobs;
    juce::String kindLabel;
    bool instrument = false;
    int rows() const { return knobs.size() > 6 ? 2 : 1; }
    void timerCallback() override;
    void fillListenTo();
    juce::String listenSignature;
public:
    // an effect that can listen to another channel (Sidechain, compressors, Bombeo): which one
    std::unique_ptr<juce::ComboBox> listenTo;
    juce::Array<te::Track*> listenTracks;      // item i + 2 is listenTracks[i]
};

// A modulator of the channel (LFO, envelope follower, shape): its knobs and the parameters it moves.
class ModulatorCard : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ModulatorCard (ViewState&, te::Track&, te::Modifier&);
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredWidth() const;
    te::Modifier::Ptr modifier;
    static juce::PopupMenu mapMenu (ViewState&, te::Track&, te::Modifier&);
private:
    ViewState& vs;
    te::Track& track;
    juce::TextButton power, map { "MAPEAR" }, remove;
    juce::OwnedArray<ParamKnob> knobs;
    std::vector<float> history;
    std::unique_ptr<juce::Component> shape;   // Forma: the curve, drawn with the mouse
    int rows() const { return knobs.size() > 6 ? 2 : 1; }
    void timerCallback() override;
};

// The selected channel: its instrument and the effects on top of it, left to right in signal order.
// The channel's volume where its devices are: a fader and the number in dB. Type a number (-6, -6 dB, -inf) or drag;
// the two follow each other and the faders of the song and the mixer.
class VolumeBox : public juce::Component, private juce::Timer
{
public:
    VolumeBox();
    void setTrack (te::Track*);
    void resized() override;
    void paint (juce::Graphics&) override;
    FaderSlider fader;
    juce::Label number;
    static juce::String dbText (float db);              // "-3.0 dB", "0.0 dB", "-inf dB"
    static std::optional<float> parseDb (const juce::String&);
private:
    te::VolumeAndPanPlugin::Ptr volume;
    te::VolumeAndPanPlugin* plugin() const { return volume.get(); }
    void timerCallback() override;
    void show (float pos);
};

class ChannelView : public juce::Component,
                    public juce::DragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ChannelView (ViewState&);
    ~ChannelView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTrack (te::Track*);
    te::Track* getTrack() const { return track.get(); }
    void detach();                                    // the edit is about to be replaced

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropIndex = -1; repaint(); }
    void itemDropped (const SourceDetails&) override;

    int numCards() const { return cards.size(); }
    DeviceCard* card (int i) const { return cards[i]; }
    int numModulatorCards() const { return modCards.size(); }
    ModulatorCard* modulatorCard (int i) const { return modCards[i]; }
    VolumeBox volumeBox;

private:
    ViewState& vs;
    te::Track::Ptr track;
    juce::Viewport viewport;
    struct Strip : public juce::Component { ChannelView* owner = nullptr; void paint (juce::Graphics&) override; } strip;
    juce::OwnedArray<DeviceCard> cards;
    juce::OwnedArray<ModulatorCard> modCards;
    juce::TextButton addInstrument { "+ INSTRUMENTO" }, addEffect { "+ EFECTO" }, newSequence { "CREAR SECUENCIA" };
    juce::String signature;
    int dropIndex = -1, modStartX = 0;

    void rebuild();
    int titleWidth() const;
    juce::String chainSignature() const;
    int indexForX (int xInView) const;
    float markerX() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    friend struct Strip;
};
}

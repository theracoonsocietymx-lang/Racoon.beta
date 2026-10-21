#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Automation.h"
#include "../PluginProcessor.h"

namespace be::ui
{
extern bool gForceAnimation;
inline bool animVisible (const juce::Component* c)
{
    if (! gForceAnimation) return c->isShowing();
    for (auto* p = c; p != nullptr; p = p->getParentComponent()) if (! p->isVisible()) return false;
    return true;
}

struct Animated
{
    virtual ~Animated() = default;
    virtual void animate() = 0;
};

void slab (juce::Graphics& g, juce::Rectangle<float> r);

// shared editor context
struct Ctx
{
    explicit Ctx (DrumEngineProcessor& p);
    DrumEngineProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;

    int track() const { return proc.selectedTrack; }
    void selectTrack (int t);
    std::vector<std::function<void()>> trackListeners;
    void onTrackChanged (std::function<void()> f) { trackListeners.push_back (std::move (f)); }

    // parameter -> modulation destination (+ track, -1 for globals)
    int destFor (const juce::String& paramId, int* trackOut = nullptr) const;
    int addRoute (int src, int trk, int dst, float amount);
    void removeRoutesTo (int dst, int trk);
    void routesTo (int dst, int trk, juce::Array<int>& out) const;
    float routeAmountTo (int dst, int trk) const;
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const { auto* a = apvts.getRawParameterValue (id); return a != nullptr ? a->load() : 0.0f; }
    juce::String tid (int local) const { return trackParamId (track(), local); }

    std::vector<Animated*> animated;
    void registerAnimated (Animated* a) { animated.push_back (a); }
    void unregisterAnimated (Animated* a) { animated.erase (std::remove (animated.begin(), animated.end(), a), animated.end()); }

    std::function<void (int page)> showPage;
    std::function<void (int route)> showRoute;
    std::function<void()> refreshMatrix;
    std::function<void()> refreshAll;

    // sequencer selection shared between pages
    int selStep = -1;
private:
    std::map<juce::String, int> destMap;
    struct RoutePtrs { std::atomic<float>* on; std::atomic<float>* src; std::atomic<float>* trk; std::atomic<float>* dst; std::atomic<float>* amt; };
    RoutePtrs rp[kNumRoutes];
};

// ------------------------------------------------------------------ knob
class Knob : public GrabSlider, public juce::DragAndDropTarget, public Animated
{
public:
    Knob (Ctx& c, const juce::String& paramId);
    ~Knob() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    void animate() override;
    juce::String getParamId() const { return pid; }
private:
    Ctx& ctx;
    juce::String pid;
    int dest = 0, trk = -1;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool dropHover = false;
    float lastMod = 0.0f, lastStatic = 0.0f;
    void showMenu();
};

class ParamKnob : public juce::Component, public Animated
{
public:
    ParamKnob (Ctx& c, const juce::String& paramId, const juce::String& label);
    ~ParamKnob() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void animate() override;
    Knob knob;
private:
    Ctx& ctx;
    juce::String label, lastText;
};

class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (Ctx& c, const juce::String& paramId, const juce::String& label = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    racoon::RightClickComboBox box;
private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

class Toggle : public juce::ToggleButton
{
public:
    Toggle (Ctx& c, const juce::String& paramId, const juce::String& text = {});
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::String pid;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// bool parameter shown as a lit stone button (toggle or momentary)
class ParamButton : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    ParamButton (Ctx& c, const juce::String& paramId, const juce::String& text, bool momentary = false, juce::Colour lit = C::amber);
    ~ParamButton() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void animate() override;
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
private:
    Ctx& ctx;
    juce::String pid, text;
    bool momentary;
    juce::Colour litColour;
    bool last = false;
};

class Stepper : public juce::Component, public juce::SettableTooltipClient, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Stepper (Ctx& c, const juce::String& paramId, const juce::String& label, std::function<juce::String (int)> fmt = {});
    ~Stepper() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    Ctx& ctx;
    juce::String pid, label;
    std::function<juce::String (int)> fmt;
    juce::RangedAudioParameter* param;
    int dragStartValue = 0;
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { repaint(); }
    void setInt (int v);
    int getInt() const;
};

// plain (non-parameter) value stepper used by editors of pattern data
class ValueStepper : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueStepper (const juce::String& label, int lo, int hi, std::function<juce::String (int)> fmt = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setValue (int v, bool notify = false);
    int getValue() const { return value; }
    std::function<void (int)> onChange;
    void setRange (int l, int h) { lo = l; hi = h; setValue (value); }
private:
    juce::String label;
    int lo, hi, value = 0, dragStart = 0;
    std::function<juce::String (int)> fmt;
};

class ModChip : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    ModChip (Ctx& c, int source, const juce::String& text = {});
    ~ModChip() override;
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void animate() override;
    void setSource (int s, const juce::String& t) { src = s; text = t; repaint(); }
private:
    Ctx& ctx;
    int src;
    juce::String text;
    float last = 0.0f;
};

class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& t = {}, bool lit = false) : title (t), lit (lit) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override { Draw::panel (g, getLocalBounds().toFloat(), title, lit); }
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (10, 6).withTrimmedTop (title.isNotEmpty() ? 20 : 0); }
    void setTitle (const juce::String& t) { title = t; repaint(); }
private:
    juce::String title;
    bool lit;
};

class TabStrip : public juce::Component
{
public:
    TabStrip (juce::StringArray names, std::function<void (int)> onChange);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int i) { sel = i; repaint(); }
    int getSelected() const { return sel; }
    void setNames (const juce::StringArray& n) { names = n; repaint(); }
    std::function<juce::Colour (int)> dotColour;    // optional status dot per tab
private:
    juce::StringArray names;
    std::function<void (int)> onChange;
    int sel = 0;
};

// 16 track selector buttons with activity light
class TrackSelector : public juce::Component, public Animated
{
public:
    explicit TrackSelector (Ctx& c);
    ~TrackSelector() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void animate() override;
private:
    Ctx& ctx;
    int lastHits[kNumTracks] {};
    float flash[kNumTracks] {};
};

void layoutRow (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int gap = 4);
void layoutGrid (juce::Rectangle<int> r, const juce::Array<juce::Component*>& comps, int columns, int rowHeight, int gap = 4);
juce::String fmtStepIndex (int v);
} // namespace be::ui

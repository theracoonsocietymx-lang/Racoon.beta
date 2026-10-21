#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Automation.h"
#include "../PluginProcessor.h"

namespace be::ui
{
// true when a component is on screen (or when a test harness forces animation)
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

// shared editor context
struct Ctx
{
    explicit Ctx (BassEngineProcessor& p);
    BassEngineProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;

    int destFor (const juce::String& paramId) const;
    int addRoute (int src, int dst, float amount);
    void removeRoutesTo (int dst);
    void routesTo (int dst, juce::Array<int>& out) const;
    float routeAmountTo (int dst) const;
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    std::vector<Animated*> animated;
    void registerAnimated (Animated* a) { animated.push_back (a); }
    void unregisterAnimated (Animated* a) { animated.erase (std::remove (animated.begin(), animated.end(), a), animated.end()); }

    std::function<void (int page)> showPage;
    std::function<void (int route)> showRoute;
    std::function<void()> refreshMatrix;
private:
    std::map<juce::String, int> destMap;
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
    int getDest() const { return dest; }
private:
    Ctx& ctx;
    juce::String pid;
    int dest = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool dropHover = false;
    float lastMod = 0.0f, lastStatic = 0.0f;
    void showMenu();
};

// knob + engraved label + value readout
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

// combo box bound to a choice parameter
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

// toggle bound to a bool parameter
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

// integer parameter as a stepper ( < value > )
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

// draggable modulation source
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

// stone panel container with title
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

// simple tab strip
class TabStrip : public juce::Component
{
public:
    TabStrip (juce::StringArray names, std::function<void (int)> onChange);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int i) { sel = i; repaint(); }
    int getSelected() const { return sel; }
    void setNames (const juce::StringArray& n) { names = n; repaint(); }
private:
    juce::StringArray names;
    std::function<void (int)> onChange;
    int sel = 0;
};

// PLAY / STOP for the pattern (runs the sequencer without holding a key or starting the host)
class PlayButton : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    PlayButton (Ctx& c, bool big = false);
    ~PlayButton() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void animate() override;
    static void toggle (Ctx& c);
private:
    Ctx& ctx;
    bool big, lastState = false;
};

// helper: lay out a row of components evenly
void layoutRow (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps, int gap = 4);
} // namespace be::ui

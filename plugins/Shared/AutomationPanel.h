#pragma once
#include "Automation.h"

// The automation strip of every Racoon plugin: pick a parameter, draw its line over the loop, record movements.
namespace racoon
{
struct AutomationStyle
{
    juce::Font (*label) (float) = nullptr;     // the plugin's label font
    juce::Font (*body) (float) = nullptr;
    juce::Font (*mono) (float) = nullptr;
    juce::Colour surface { 0xfff7f5f2 }, ink { 0xff1f1a1c }, inkSoft { 0xff5e5559 }, accent { 0xffe0789a };
};

// the line itself: click puts a point, dragging draws, a point can be dragged, double click (or right click) removes it
class AutomationLaneEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    AutomationLaneEditor (Automation&, const AutomationStyle&);
    void setParam (const juce::String& id) { paramId = id; repaint(); }
    const juce::String& getParam() const { return paramId; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }
    double grid = 0.25;
private:
    Automation& autom;
    const AutomationStyle& st;
    juce::String paramId;
    std::vector<Automation::Point> before;
    bool existedBefore = false, editing = false;
    enum class Mode { none, move, draw } mode = Mode::none;
    Automation::Point moving;
    double lastDrawBeat = -1.0;
    float lastDrawValue = 0.0f;
    juce::Point<float> hover;
    juce::Rectangle<float> area() const;
    float xOf (double beat) const;
    double beatOf (float x, bool snap) const;
    float yOf (float v) const;
    float valueOf (float y) const;
    int pointAt (juce::Point<float>) const;
    void begin();
    void finish();
};

class AutomationPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    AutomationPanel (Automation&, AutomationStyle);
    ~AutomationPanel() override;
    void showParam (const juce::String& id);
    void setRecording (bool);
    juce::String currentParam() const { return lane.getParam(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    static constexpr int preferredHeight = 206;
    AutomationLaneEditor lane;
private:
    Automation& autom;
    AutomationStyle style;
    juce::ComboBox which, bars;
    juce::TextButton record { "Grabar" }, clear { "Borrar" }, other { "+ Otro" }, close { "Cerrar" };
    juce::Rectangle<int> titleArea, ccArea;
    juce::StringArray listed;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshList();
    void showOtherMenu();
};
}

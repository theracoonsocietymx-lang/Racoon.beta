#pragma once
#include "RacoonEditor.h"
#include "SquirrelProcessor.h"

namespace racoon::squirrel
{
// small number field: drag up / down or double-click to type
class NumberBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    NumberBox (const juce::String& label, double lo, double hi, double step, std::function<juce::String (double)> fmt = {});
    void setValue (double v) { value = v; repaint(); }
    std::function<void (double)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    juce::String label;
    double lo, hi, step, value = 0.0, dragStart = 0.0;
    std::function<juce::String (double)> format;
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (Processor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    int selected = 0;
private:
    Processor& proc;
    int dragging = -1;          // 0 start, 1 end, 2 loop start, 3 loop end, 10+k slice k
    float xFor (int frame, int len) const;
    int frameFor (float x, int len) const;
    int hit (float x) const;
};

// screen: file and zone tools, the waveform with its markers, the zone settings and a keyboard
class Display : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Display (Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
private:
    Processor& proc;
    WaveView wave;
    juce::MidiKeyboardComponent keys;
    juce::TextButton loadButton { "CARGAR..." }, layerButton { "CAPA..." }, factoryButton { "DE FÁBRICA"_u }, libraryButton { "MUESTRAS" }, removeButton { "QUITAR" },
                     sliceButton { "DETECTAR CORTES" }, findButton { "BUSCAR FALTANTES..." }, embedButton { "INCRUSTAR" };
    juce::ComboBox zoneBox;
    NumberBox root, lowKey, highKey, lowVel, highVel, tune, gain, choke, rr, fadeIn, fadeOut;
    juce::ComboBox loopBox;
    juce::TextButton revButton { "REV" }, oneShotButton { "ONE-SHOT" }, normButton { "NORM" };
    std::shared_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    bool dragOver = false;
    juce::Rectangle<int> zoneRow;
    void timerCallback() override;
    void refresh();
    void editZone (std::function<void (Zone&)>);
    void choose (bool layers);
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

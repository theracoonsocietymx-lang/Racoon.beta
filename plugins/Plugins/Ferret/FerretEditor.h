#pragma once
#include "RacoonEditor.h"
#include "FerretProcessor.h"

namespace racoon::ferret
{
// the screen: the chord and its name, the fretboard with the hand on it, and the rhythm pattern (editable in Usuario)
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    ~Display() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    static juce::String tokenName (char);
    static const char* tokenCycle();
private:
    Processor& proc;
    juce::TextButton toUser { "EDITAR ESTE PATRÓN"_u }, exportMidi { "EXPORTAR MIDI (ARRASTRA)" }, clearLog { "BORRAR REGISTRO" };
    juce::Rectangle<int> chordArea, fretArea, patternArea, infoArea;
    juce::File lastExport;
    char dragToken = 0;
    int lastDragCell = -1;
    int cellAt (juce::Point<int>) const;
    juce::Rectangle<float> cellBounds (int step, int len) const;
    void timerCallback() override { repaint(); }
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

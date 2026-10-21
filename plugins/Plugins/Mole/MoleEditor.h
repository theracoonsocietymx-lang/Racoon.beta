#pragma once
#include "RacoonEditor.h"
#include "MoleProcessor.h"
#include "GridSelection.h"

namespace racoon::mole
{
// the screen: 16 lanes with their steps (click to draw, drag up and down for velocity, right click for probability,
// ratchets, conditions and microtiming), the controls of the chosen lane, and the tools (complement Hamster,
// mutate, copy patterns, export MIDI by dragging)
class Display : public juce::Component, private juce::Timer
{
public:
    explicit Display (Processor&);
    ~Display() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    int selectedLane() const { return selected; }
    // selecting, moving and copying hits, the pencil (B), and every change undoable
    racoon::GridSelection<Step> grid;
    void selectLane (int);
    juce::Rectangle<int> cellBounds (int lane, int step) const;   // tests click on cells
    int page = 0;                                                  // 32 steps per page

private:
    Processor& proc;
    int selected = 0;
    juce::Rectangle<int> gridArea, inspectorArea, toolsArea;
    juce::OwnedArray<ParamControl> laneControls;
    juce::TextButton complement { "COMPLEMENTAR A HAMSTER" }, mutate { "ALEATORIA" }, clearLane { "BORRAR CARRIL" },
                     copyPattern { "COPIAR PATRÓN"_u }, exportMidi { "EXPORTAR MIDI (ARRASTRA)" }, prevPage { "<" }, nextPage { ">" },
                     kitButton { "BATERÍA: SINTETIZADA"_u };
    void refreshKitButton();
    juce::uint32 lastFlash[numLanes] {};
    float flash[numLanes] {};
    int dragLane = -1, dragStep = -1, dragStartVel = 100;
    bool dragSetOn = true, draggingVel = false;
    juce::File lastExport;
    std::vector<juce::uint64> undoBefore;
    std::vector<juce::uint64> packAll() const;
    void beginEdit();
    void endEdit();

    static constexpr int nameW = 150, headH = 18;
    int rowH() const { return juce::jmax (12, (gridArea.getHeight() - headH) / numLanes); }
    int visibleSteps() const { return 32; }
    std::pair<int, int> hitCell (juce::Point<int>) const;
    bool cellActive (int lane, int step) const;
    void stepMenu (int lane, int step);
    void laneMenu (int lane);
    void ensureManual (int lane);
    void timerCallback() override;
};

class Editor : public RacoonEditor
{
public:
    explicit Editor (Processor&);
};
}

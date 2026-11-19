#pragma once
#include "Widgets.h"

namespace racoon::ui
{
class ParamKnob;

// The inside of an effect rack: eight macros on top and one row per chain. Chains play in parallel; the effects
// of a chain play left to right. Every knob can be automated and assigned to a macro with a right click.
class RackEditor : public juce::Component, private juce::Timer
{
public:
    RackEditor (ViewState&, te::Track&, te::Plugin& rackInstance);
    ~RackEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static void open (ViewState&, te::Track&, te::Plugin& rackInstance);   // in its own window
    static void closeAll();
    static RackEditor* findOpen (te::Plugin& rackInstance);

    te::Plugin::Ptr rack;
    int numChainRows() const { return chainRows.size(); }

private:
    ViewState& vs;
    te::Track& track;
    juce::OwnedArray<ParamKnob> macroKnobs;
    struct ChainRow;
    juce::OwnedArray<ChainRow> chainRows;
    juce::TextButton addChain { "+ CADENA" };
    juce::Viewport viewport;
    juce::Component content;
    juce::String signature;
    juce::String makeSignature() const;
    void rebuild();
    void layoutContent();
    void timerCallback() override;
};
}

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Pages.h"
#include "ui/ProLook.h"
#include "AutomationPanel.h"
#include "Maqueta.h"

class BassEngineEditor : public juce::AudioProcessorEditor,
                         public racoon::AutomationOpener,
                         public juce::DragAndDropContainer,
                         private juce::Timer
{
public:
    explicit BassEngineEditor (BassEngineProcessor&);
    ~BassEngineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    // automation (right click on a control > Ver automatización) and export
    racoon::Automation& automation() override { return proc.automation; }
    void openAutomation (const juce::String& paramId, bool record) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int baseW = 1360, baseH = 940;
    enum Page { pInicio, pSynth, pMod, pAcid, pSeq, pFx, pMix, pAnalyze, pPresets, numPages };
    void showPage (int p);
    void tickForTest() { timerCallback(); }

private:
    BassEngineProcessor& proc;
    be::ui::ProLook look;
    be::ui::Ctx ctx;
    juce::TooltipWindow tooltips { this, 600 };

    // root holds everything at base size; scaled with an affine transform
    struct Root : public juce::Component
    {
        explicit Root (BassEngineEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        BassEngineEditor& ed;
        juce::Image bg;
    } root { *this };

    // header
    struct Logo : public juce::Component { void paint (juce::Graphics&) override; } logo;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetName, saveBtn { "GUARDAR" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" }, undoBtn { "DESHACER" }, redoBtn { "REHACER" };
    juce::TextButton easyBtn { "PRINCIPIANTE"_u }, fullBtn { "AVANZADO" };
    be::ui::PlayButton headerPlay;
    be::ui::ChoiceBox quality;
    juce::Label cpu;
    be::ui::ScopeView miniScope;
    be::ui::Meter meter;
    be::ui::ParamKnob master;
    be::ui::TabStrip tabs;

    // pages
    be::ui::SimplePage simplePage;
    be::ui::SynthPage synthPage;
    be::ui::ModPage modPage;
    be::ui::AcidPage acidPage;
    be::ui::SeqPage seqPage;
    be::ui::FxPage fxPage;
    be::ui::MixPage mixPage;
    be::ui::AnalyzePage analyzePage;
    be::ui::PresetPage presetPage;
    std::array<juce::Component*, 9> pages;
    int currentPage = 0;
    bool easyMode = true;
    void setEasyMode (bool e);
    std::vector<int> visibleTabs() const;

    // footer macros
    struct MacroCell : public juce::Component
    {
        MacroCell (be::ui::Ctx& c, int i);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        be::ui::Ctx& ctx;
        int index;
        be::ui::Knob knob;
        be::ui::ModChip chip;
        juce::TextEditor editor;
    };
    juce::OwnedArray<MacroCell> macros;

    // playable keyboard (mouse + computer keys)
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton octDown { "OCT -" }, octUp { "OCT +" };
    juce::Label kbHint;
    int kbOctave = 3;
    bool focusGrabbed = false;
    void setKbOctave (int o);

    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<racoon::AutomationPanel> autoPanel;
    juce::TextButton exportBtn { "EXPORTAR" }, autoBtn { "AUTOMATIZAR" };
    racoon::maqueta::Button toSong;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override;
    void refreshAfterPresetChange();
};

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Pages.h"
#include "ui/ProLook.h"
#include "AutomationPanel.h"
#include "Maqueta.h"

class DrumEngineEditor : public juce::AudioProcessorEditor,
                         public racoon::AutomationOpener,
                         public juce::DragAndDropContainer,
                         private juce::Timer
{
public:
    explicit DrumEngineEditor (DrumEngineProcessor&);
    ~DrumEngineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    // automation (right click on a control > Ver automatización) and export
    racoon::Automation& automation() override { return proc.automation; }
    void openAutomation (const juce::String& paramId, bool record) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    static constexpr int baseW = 1440, baseH = 960, headerH = 88, footerH = 84, padsH = 62;
    enum Page { pInicio, pKit, pSynth, pSample, pSeq, pMod, pFx, pMix, pAnalyze, pPresets, numPages };
    void showPage (int p);
    void tickForTest() { timerCallback(); }
    void setLive (bool live);

private:
    DrumEngineProcessor& proc;
    be::ui::ProLook look;
    be::ui::Ctx ctx;
    juce::TooltipWindow tooltips { this, 600 };

    struct Root : public juce::Component
    {
        explicit Root (DrumEngineEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        DrumEngineEditor& ed;
        juce::Image bg;
    } root { *this };

    struct Logo : public juce::Component { void paint (juce::Graphics&) override; } logo;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetName, saveBtn { "GUARDAR" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" }, undoBtn { "DESHACER" }, redoBtn { "REHACER" }, liveBtn { "EN VIVO" };
    juce::TextButton easyBtn { "PRINCIPIANTE" }, fullBtn { "AVANZADO" };
    std::unique_ptr<be::ui::ParamButton> playBtn, recBtn;
    be::ui::ChoiceBox quality;
    juce::Label cpu, transport;
    be::ui::ScopeView miniScope;
    be::ui::MasterMeter meter;
    be::ui::ParamKnob master;
    be::ui::TabStrip tabs;

    be::ui::SimplePage simplePage;
    be::ui::KitPage kitPage;
    be::ui::SynthPage synthPage;
    be::ui::SamplePage samplePage;
    be::ui::SeqPage seqPage;
    be::ui::ModPage modPage;
    be::ui::FxPage fxPage;
    be::ui::MixPage mixPage;
    be::ui::AnalyzePage analyzePage;
    be::ui::PresetPage presetPage;
    be::ui::LivePage livePage;
    std::array<juce::Component*, 10> pages;
    int currentPage = 0;
    bool easyMode = true;
    void setEasyMode (bool e);
    std::vector<int> visibleTabs() const;
    bool live = false;

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

    // pad strip (mouse + computer keys)
    struct PadStrip : public juce::Component, public be::ui::Animated
    {
        explicit PadStrip (be::ui::Ctx& c);
        ~PadStrip() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void animate() override;
        be::ui::Ctx& ctx;
        int down = -1;
        int lastHits[be::kNumTracks] {};
        float flash[be::kNumTracks] {};
        bool keyDown[be::kNumTracks] {};
    } padStrip { ctx };

    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<racoon::AutomationPanel> autoPanel;
    juce::TextButton exportBtn { "EXPORTAR" }, autoBtn { "AUTOMATIZAR" };
    racoon::maqueta::Button toSong;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override;
    void refreshAfterPresetChange();
    void refreshAll();
    int lastTransportKey = -1;
    bool focusGrabbed = false;
};

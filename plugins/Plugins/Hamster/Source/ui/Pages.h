#pragma once
#include "Displays.h"
#include "DrumGridEditor.h"
#include "../features/Features.h"
#include "../presets/PresetManager.h"

namespace be::ui
{
using KnobPtr = std::unique_ptr<ParamKnob>;
using BoxPtr = std::unique_ptr<ChoiceBox>;
using TogglePtr = std::unique_ptr<Toggle>;
using StepperPtr = std::unique_ptr<Stepper>;

// base for pages whose controls follow the selected track
class TrackPage : public juce::Component
{
public:
    explicit TrackPage (Ctx& c);
    virtual void rebuild() = 0;
    void visibilityChanged() override { if (isVisible() && builtFor != ctx.track()) { rebuild(); resized(); } }
protected:
    Ctx& ctx;
    TrackSelector selector;
    int builtFor = -1;
    KnobPtr knob (int local, const juce::String& label);
    BoxPtr box (int local, const juce::String& label);
    TogglePtr toggle (int local, const juce::String& text);
    StepperPtr stepper (int local, const juce::String& label, std::function<juce::String (int)> fmt = {});
    void add (juce::Component* c) { addAndMakeVisible (c); }
};

// ------------------------------------------------------------------ KIT
class PadGrid : public juce::Component, public Animated, public juce::FileDragAndDropTarget
{
public:
    explicit PadGrid (Ctx& c, bool big = false);
    ~PadGrid() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void animate() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override { hoverPad = padAt ({ (float) x, (float) y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override { hoverPad = -1; repaint(); }
    std::function<void (int)> onMenu;
private:
    Ctx& ctx;
    bool big;
    int downPad = -1, hoverPad = -1;
    int lastHits[kNumTracks] {};
    float flash[kNumTracks] {};
    int padAt (juce::Point<float>) const;
    juce::Rectangle<float> padRect (int t) const;
};

class KitPage : public TrackPage
{
public:
    explicit KitPage (Ctx& c);
    void rebuild() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
private:
    Panel padPanel { "PADS", true }, trackPanel { "TRACK" }, layerPanel { "LAYERS" }, morphPanel { "CIRCUIT MORPH" }, toolPanel { "TOOLS" };
    PadGrid pads;
    juce::TextEditor nameEditor;
    BoxPtr role, model, choke, poly;
    StepperPtr note;
    KnobPtr level, pan, tune, fine, decay, velsens, drift;
    TogglePtr aOn, bOn, cOn, dOn, morphOn;
    KnobPtr aLvl, bLvl, cLvl, dLvl, circuit;
    CircuitView circuitView;
    juce::TextButton copyBtn { "COPY" }, pasteBtn { "PASTE" }, resetBtn { "RESET" }, dnaBtn { "DRUM DNA" }, saveSnd { "SAVE SOUND" }, loadSnd { "LOAD SOUND" },
                     clearSmp { "CLEAR SAMPLES" }, auditionBtn { "AUDITION" }, notesChrom { "NOTES 36-51" }, notesGm { "NOTES GM" },
                     libBtn { "MUESTRAS" }, libKitBtn { "KIT DE MUESTRAS" };
    void libraryMenu (int t, juce::Component* target);
    juce::Label dnaInfo;
    int clipboardTrack = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    void padMenu (int t);
    void runDna (const juce::File& f, int mode);
};

// ------------------------------------------------------------------ SYNTH (layers A, B, D, envelopes, filter)
class SynthPage : public TrackPage
{
public:
    explicit SynthPage (Ctx& c);
    void rebuild() override;
    void resized() override;
private:
    Panel aPanel { "A  /  ANALOG MODEL" }, bPanel { "B  /  FM  6-OP" }, dPanel { "D  /  TEXTURE" }, envPanel { "ENVELOPES" }, filtPanel { "FILTER" }, lfoPanel { "VOICE LFO" };
    TogglePtr aOn, bOn, dOn;
    BoxPtr model, algo, texType, envMode, fType, vl1w, vl2w;
    KnobPtr aP[8], aLvl, drift;
    KnobPtr ratio[6], lvl[6], dcy[6], fb, morph, bLvl;
    KnobPtr dLvl, dFreq, dQ, dDecay, dTone;
    KnobPtr att, hold, dec, sus, rel, curve, pAmt, pTime, fAmt, fDec;
    KnobPtr cutoff, res, drive, key, mix;
    KnobPtr vl1r, vl2r;
    std::unique_ptr<ModChip> vl1chip, vl2chip;
    FmAlgoView algoView;
    EnvSketch envSketch;
    TrackFilterCurve filterCurve;
};

// ------------------------------------------------------------------ SAMPLE (layer C)
class SamplePage : public TrackPage, public Animated
{
public:
    explicit SamplePage (Ctx& c);
    ~SamplePage() override;
    void rebuild() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void animate() override;
private:
    Panel wavePanel { "C  /  SAMPLE LAYER", true }, slotPanel { "MULTISAMPLE SLOTS" }, playPanel { "PLAYBACK" }, slicePanel { "SLICES" };
    WaveformView wave;
    TogglePtr cOn, reverse, loop;
    BoxPtr sample, stretchMode, sliceMode, multi;
    StepperPtr slice;
    KnobPtr cLvl, start, end, pitch, stretch, fadeIn, fadeOut, rr;
    juce::OwnedArray<juce::TextButton> slotBtns, slotLoad, slotClear;
    juce::TextButton dnaBtn { "REBUILD WITH DRUM DNA" };
    std::unique_ptr<juce::FileChooser> chooser;
    int lastSlotsKey = -1;
    void refreshSlots();
};

// ------------------------------------------------------------------ SEQ
class StepGrid : public juce::Component, public Animated
{
public:
    explicit StepGrid (Ctx& c);
    ~StepGrid() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void animate() override;
    void reload() { data = ctx.proc.getSeqData(); repaint(); }
    DrumGridEditor editor;  // selection, moving, copying, the pencil
    int page = 0;           // 16-step page
    std::function<void()> onSelect;
    SeqData data;
    int pattern() const;
private:
    Ctx& ctx;
    int playStep[kNumTracks] {};
    bool playing = false;
    int dragTrack = -1, dragStep = -1, lastDragStep = -1;
    bool dragSetTo = false, dragVel = false;
    int dragStartVel = 100;
    juce::Rectangle<float> cell (int track, int step) const;
    bool hit (juce::Point<float>, int& track, int& step) const;
    void commit (bool undoable = true);
};

class ExportHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    ExportHandle (Ctx& c, bool audio) : ctx (c), audio (audio)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (audio ? "Arrastra el patrón (WAV) a tu DAW"_u : "Arrastra el patrón como clip MIDI a tu DAW"_u);
    }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<int()> bars, solo;
private:
    Ctx& ctx;
    bool audio, dragging = false;
};

class StepEditor : public juce::Component
{
public:
    explicit StepEditor (Ctx& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void load();
    std::function<void()> onChanged;
private:
    Ctx& ctx;
    ValueStepper vel { "VELOCITY", 1, 127 }, prob { "PROB %", 0, 100 }, micro { "MICRO", -50, 50 }, retrig { "RETRIG", 1, 8 }, len { "LENGTH", 1, 64 },
                 pitch { "PITCH", -24, 24 }, condA { "A", 1, 8 }, condB { "B", 1, 8 };
    juce::ComboBox cond, mode;
    juce::ToggleButton on { "ON" }, accent { "ACCENT" };
    juce::TextButton addLock { "+ LOCK" }, clearLocks { "CLEAR LOCKS" };
    juce::OwnedArray<GrabSlider> lockSliders;
    juce::OwnedArray<juce::TextButton> lockRemove;
    juce::StringArray lockNames;
    bool loading = false;
    void write (std::function<void (Step&)> fn);
};

class SeqPage : public TrackPage, public Animated
{
public:
    explicit SeqPage (Ctx& c);
    ~SeqPage() override;
    void rebuild() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void animate() override;
    void visibilityChanged() override { TrackPage::visibilityChanged(); if (isVisible()) { grid.reload(); stepEditor.load(); refreshTrackSettings(); } }
private:
    Panel topPanel { "Patrones" }, gridPanel { "Pasos" }, stepPanel { "Paso elegido" }, trackPanel { "Pista" }, toolPanel { "Generar" }, songPanel { "Canción y exportar"_u };
    juce::OwnedArray<juce::TextButton> patBtns, pageBtns;
    juce::TextButton copyPat { "COPIAR" }, pastePat { "PEGAR" }, clearPat { "LIMPIAR" }, undoSeq { "DESHACER" }, redoSeq { "REHACER" };
    std::unique_ptr<ParamButton> songMode, fillBtn, breakBtn, recBtn, playBtn;
    std::unique_ptr<ChoiceBox> fillEvery, scatter, noteRepeat;
    std::unique_ptr<ParamKnob> swing, grooveAmt, humanize, morph, bpm;
    std::unique_ptr<Stepper> morphTarget;
    StepGrid grid;
    StepEditor stepEditor;
    // track settings
    ValueStepper trkLen { "PASOS", 1, kMaxSteps }, masterLen { "LARGO DEL PATRÓN"_u, 1, kMaxSteps };
    juce::ComboBox trkRes;
    juce::TextButton shiftL { "<<" }, shiftR { ">>" }, reverseBtn { "INVERTIR" }, clearTrk { "LIMPIAR" }, humanBtn { "HUMANIZAR" };
    // generators
    ValueStepper eHits { "GOLPES", 0, 32 }, eSteps { "PASOS", 1, 32 }, eRot { "ROTAR", 0, 31 };
    juce::TextButton euclidBtn { "EUCLID" }, randBtn { "ALEATORIA" }, varBtn { "ALEATORIA" }, fillGenBtn { "CREAR FILL" };
    GrabSlider density, intensity;
    juce::ComboBox flavour, varTarget;
    // song
    juce::TextButton songAdd { "+ AGREGAR" }, songDel { "- QUITAR" }, songClear { "LIMPIAR" };
    juce::Label songText;
    ExportHandle midiDrag, audioDrag;
    ValueStepper exportBars { "COMPASES", 1, 16 };
    juce::ToggleButton soloExport { "SOLO LA PISTA" };
    Pattern clipboard;
    bool hasClip = false;
    int lastPattern = -1, lastQueued = -2;
    juce::Rectangle<int> beatRow;
    void refreshTrackSettings();
    void refreshSong();
    void editTrack (std::function<void (TrackPattern&)> fn);
};

// ------------------------------------------------------------------ MOD
class MatrixRow : public juce::Component, public Animated
{
public:
    MatrixRow (Ctx& c, int r);
    ~MatrixRow() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void animate() override;
    void setHighlight (bool h) { highlight = h; repaint(); }
private:
    Ctx& ctx;
    int r;
    Toggle on;
    juce::ComboBox src, trk, dst;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcA, trkA, dstA;
    GrabSlider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtA;
    ChoiceBox curve;
    float live = 0.0f;
    bool highlight = false;
};

class ModPage : public juce::Component
{
public:
    explicit ModPage (Ctx& c);
    void resized() override;
    void showRoute (int r);
    void paintOverChildren (juce::Graphics&) override;
private:
    Ctx& ctx;
    juce::Rectangle<int> matHeader;
    Panel lfoPanel { "GLOBAL LFO" }, srcPanel { "SOURCES" }, matPanel { "MODULATION MATRIX  /  64 ROUTES" }, scenePanel { "SCENES  /  XY" };
    TabStrip lfoTabs;
    LfoView lfoView;
    ModChip lfoChip;
    juce::OwnedArray<juce::Component> lfoControls;
    juce::OwnedArray<ModChip> chips;
    juce::Viewport matView;
    juce::Component matContent;
    juce::OwnedArray<MatrixRow> rows;
    XYPad xy;
    std::unique_ptr<Stepper> sceneA, sceneB;
    std::unique_ptr<ParamKnob> sceneFade;
    std::unique_ptr<ParamButton> sceneOn;
    juce::OwnedArray<juce::TextButton> sceneBtns;
    int curLfo = 0;
    void buildLfoControls();
};

// ------------------------------------------------------------------ FX
class FxRackList : public juce::Component
{
public:
    FxRackList (Ctx& c, std::function<void (int)> onSelect);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int selected = 9;
private:
    Ctx& ctx;
    std::function<void (int)> onSelect;
    juce::OwnedArray<Toggle> toggles;
    int dragFrom = -1, dragTo = -1;
    int rowAt (int y) const;
};

class AuxStrip : public Panel
{
public:
    AuxStrip (Ctx& c, int a);
    ~AuxStrip() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    Ctx& ctx;
    int a;
    ChoiceBox type;
    ParamKnob p1, p2, p3, p4, ret, lowcut, hicut;
    struct Labels : juce::AudioProcessorValueTreeState::Listener, juce::AsyncUpdater
    {
        AuxStrip& s;
        explicit Labels (AuxStrip& o) : s (o) {}
        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override { s.updateLabels(); }
    } labels { *this };
    void updateLabels();
};

class FxPage : public TrackPage
{
public:
    explicit FxPage (Ctx& c);
    void rebuild() override;
    void resized() override;
private:
    Panel insertPanel { "TRACK INSERTS" }, rackPanel { "MASTER RACK" }, editPanel { "EFFECT" }, masterPanel { "MASTER DYNAMICS" }, duckPanel { "DUCKING  /  SIDECHAIN" };
    BoxPtr drvType;
    KnobPtr drv, crush, eqLow, eqHigh, comp, gate, trans, phase, s1, s2, s3, s4;
    TogglePtr invert;
    AuxStrip a1, a2, a3, a4;
    FxRackList rack;
    juce::OwnedArray<juce::Component> fxControls;
    std::unique_ptr<Toggle> glueOn, tpOn;
    std::unique_ptr<ParamKnob> glueThr, glueRatio, glueAtt, glueRel, glueMake, glueMix, softclip, ceiling;
    std::unique_ptr<ChoiceBox> duckSrc, duckTarget, duckDiv, duckBand;
    std::unique_ptr<Stepper> duckTrack, duckNote;
    std::unique_ptr<ParamKnob> duckDepth, duckAttack, duckHold, duckRelease, duckCurve, duckThresh;
    void showFx (int id);
};

// ------------------------------------------------------------------ MIX
class ChannelStrip : public juce::Component, public Animated
{
public:
    ChannelStrip (Ctx& c, int t);
    ~ChannelStrip() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void animate() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    Ctx& ctx;
    int t;
    GrabSlider fader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderA;
    Knob pan, s1, s2, s3, s4;
    ParamButton mute, solo;
    juce::ComboBox group, output;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> groupA, outA;
    float lastPk = 0.0f, hold = 0.0f;
    int lastHits = 0;
    float flash = 0.0f;
};

class BusStrip : public juce::Component, public Animated
{
public:
    BusStrip (Ctx& c, int index, bool group);
    ~BusStrip() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void animate() override { if (animVisible (this)) repaint(); }
private:
    Ctx& ctx;
    int index;
    bool isGroup;
    std::unique_ptr<ParamKnob> k1, k2, k3, k4;
};

class MixPage : public juce::Component
{
public:
    explicit MixPage (Ctx& c);
    void resized() override;
private:
    Ctx& ctx;
    Panel chanPanel { "CHANNELS" }, busPanel { "GROUPS" }, auxPanel { "AUX RETURNS" }, masterPanel { "MASTER" };
    juce::OwnedArray<ChannelStrip> strips;
    juce::OwnedArray<BusStrip> groupStrips, auxStrips;
    MasterMeter meter;
    ParamKnob master;
    juce::Label outInfo;
};

// ------------------------------------------------------------------ ANALYZE
class AnalyzePage : public juce::Component, public Animated
{
public:
    explicit AnalyzePage (Ctx& c);
    ~AnalyzePage() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    Panel specPanel { "SPECTRUM" }, scopePanel { "WAVEFORM" }, loudPanel { "LOUDNESS  /  BS.1770" }, trackPanel { "TRACK ACTIVITY" },
          kbPanel { "KICK  /  BASS", true }, groovePanel { "GROOVE" };
    SpectrumView spectrum;
    ScopeView scope;
    CollisionView collision;
    GrooveView grooveView;
    juce::TextButton resetLufs { "RESET" }, tuneKick { "TUNE KICK TO BASS" }, duckBass { "SET UP DUCKING" };
    std::unique_ptr<ParamButton> bridgeOn;
    std::unique_ptr<Stepper> bridgeTrack;
    juce::Rectangle<int> loudArea, trackArea, kbInfo;
    float trackHold[kNumTracks] {};
    int frame = 0;
};

// ------------------------------------------------------------------ PRESETS
class PresetPage : public juce::Component, private juce::ListBoxModel, public juce::FileDragAndDropTarget
{
public:
    explicit PresetPage (Ctx& c);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh();
    void visibilityChanged() override { if (isVisible()) refresh(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;
private:
    Ctx& ctx;
    Panel filterPanel { "BROWSE" }, listPanel { "LIBRARY" }, dnaPanel { "DRUM DNA", true }, groovePanel { "GROOVE DNA", true }, soundPanel { "SOUNDS" };
    juce::TextEditor search;
    juce::ComboBox genre, character, loadMode;
    juce::ToggleButton favOnly { "FAVOURITES" };
    juce::ListBox list;
    juce::TextButton loadBtn { "LOAD" }, saveBtn { "SAVE AS" }, deleteBtn { "DELETE" }, favBtn { "FAVOURITE" };
    std::vector<be::PresetInfo> all, shown;
    // drum dna
    juce::TextButton dnaLoad { "ANALYSE ONE-SHOT" };
    juce::ComboBox dnaMode;
    juce::Label dnaSummary;
    // groove dna
    juce::TextButton grooveLoad { "ANALYSE LOOP" }, grooveClear { "CLEAR GROOVE" };
    juce::Label grooveSummary;
    GrooveView grooveView;
    std::unique_ptr<ParamKnob> grooveAmt;
    // sounds
    juce::ComboBox soundList;
    juce::TextButton soundSave { "SAVE TRACK" }, soundLoad { "LOAD TO TRACK" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> dnaDrop, grooveDrop;

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void applyFilter();
    void saveDialog();
    void analyseDna (const juce::File& f);
    void analyseGroove (const juce::File& f);
};

// ------------------------------------------------------------------ LIVE
class LivePage : public juce::Component, public Animated
{
public:
    explicit LivePage (Ctx& c);
    ~LivePage() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    Panel padPanel { "PERFORM", true }, patPanel { "PATTERNS" }, fxPanel { "FX  /  SCENES" }, mutePanel { "MUTES" };
    PadGrid pads;
    juce::OwnedArray<juce::TextButton> patBtns;
    juce::OwnedArray<ParamButton> mutes;
    std::unique_ptr<ParamButton> fill, brk, rec, songMode, sceneOn, playBtn;
    juce::OwnedArray<juce::TextButton> scatterBtns;
    std::unique_ptr<ChoiceBox> noteRepeat;
    std::unique_ptr<ParamKnob> morph, sceneFade, swing, humanize;
    std::unique_ptr<Stepper> morphTarget, sceneA, sceneB;
    XYPad xy;
    int lastPattern = -1, lastQueued = -2, tick = 0;
};

// ------------------------------------------------------------------ INICIO (simple page)
// 16 tracks x 16 steps like a classic drum machine, the selected sound, PLAY and the kits.
class BeatGrid : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit BeatGrid (Ctx& c);
    ~BeatGrid() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void animate() override;
    DrumGridEditor editor;  // selection, moving, copying, the pencil
    static constexpr int kLabelW = 118;
private:
    Ctx& ctx;
    SeqData data;
    int seen = -1, pattern = -1, playStep[kNumTracks] {};
    int dragTrack = -1, lastStep = -1;
    bool dragSet = true;
    juce::Rectangle<float> cell (int t, int s) const;
    void setStep (int t, int s, bool on);
};

class BigPlay : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit BigPlay (Ctx& c);
    ~BigPlay() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void animate() override;
private:
    Ctx& ctx;
    bool last = false;
};

class SimplePage : public juce::Component, private juce::ListBoxModel, public Animated
{
public:
    explicit SimplePage (Ctx& c);
    ~SimplePage() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh();
    void animate() override;
    void visibilityChanged() override { if (isVisible()) refresh(); }
private:
    Ctx& ctx;
    Panel beatPanel { "Tu ritmo" }, soundPanel { "Sonido elegido" }, playPanel { "Toca" }, kitPanel { "Kits" }, helpPanel { "Empieza aquí"_u };
    BeatGrid grid;
    BigPlay play;
    std::unique_ptr<ParamKnob> level, tune, decay, tone;
    int builtFor = -1;
    ParamKnob tempo, swing;
    std::unique_ptr<ParamButton> fill;
    juce::OwnedArray<juce::TextButton> patternBtns;
    juce::TextButton variation { "ALEATORIA" }, clear { "LIMPIAR" }, prevBtn { "<" }, nextBtn { ">" }, editBtn { "EDITAR SONIDO" };
    juce::ComboBox genre;
    juce::ListBox list;
    std::vector<PresetInfo> shown;
    juce::String lastPreset;
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void fillList();
    void buildTrackKnobs();
};
} // namespace be::ui

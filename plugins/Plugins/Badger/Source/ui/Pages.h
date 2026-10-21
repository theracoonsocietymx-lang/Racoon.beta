#pragma once
#include <set>
#include "Displays.h"
#include "../features/Features.h"
#include "../dsp/Sequencer.h"
#include "../presets/PresetManager.h"

namespace be::ui
{
using KnobPtr = std::unique_ptr<ParamKnob>;
using BoxPtr = std::unique_ptr<ChoiceBox>;
using TogglePtr = std::unique_ptr<Toggle>;

// ------------------------------------------------------------------ SYNTH
class OscPanel : public Panel, public Animated
{
public:
    OscPanel (Ctx& c, int osc);
    ~OscPanel() override;
    void resized() override;
    void animate() override;
private:
    Ctx& ctx;
    int osc;
    Toggle on;
    ChoiceBox engine, layer, vaWave, table, sample, spMode;
    juce::TextButton advBtn { "ADV" }, loadBtn { "LOAD" }, editBtn { "HARMONICS" }, clearBtn { "X" };
    WaveDisplay display;
    Stepper oct, semi, unison;
    ParamKnob fine, level, pan, pos, pa, pb, pc, detune, spread;
    ParamKnob phase, randPh, blend, drift, slop, keyTrack, coarse, fold, shape, sync, xmodAmt;
    ChoiceBox syncMode, xmodType, xmodSrc;
    Toggle reset;
    int lastEngine = -1;
    bool adv = false, userTouchedEngine = false;
    void mouseDown (const juce::MouseEvent&) override;
    std::unique_ptr<juce::FileChooser> chooser;
    void updateFace();
    void openHarmonicsEditor();
};

class SubPanel : public Panel
{
public:
    explicit SubPanel (Ctx& c);
    void resized() override;
private:
    Toggle on, align, mono, lock;
    ChoiceBox wave, octave, env;
    ParamKnob level, drive, cutoff, ceiling, phase;
};

class NoisePanel : public Panel
{
public:
    explicit NoisePanel (Ctx& c);
    void resized() override;
private:
    Toggle on;
    ChoiceBox type, layer;
    ParamKnob level, color, pan, transient;
};

class FilterStrip : public juce::Component
{
public:
    FilterStrip (Ctx& c, int f);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    int f;
    Toggle on, comp;
    ChoiceBox type, envSel;
    FilterCurve curve;
    ParamKnob cutoff, res, drive, envAmt, keyTrack, vel, mix, morph, feedback;
};

class FilterSection : public Panel
{
public:
    explicit FilterSection (Ctx& c);
    void resized() override;
private:
    ChoiceBox routing;
    FilterStrip f1, f2, f3;
};

class SynthPage : public juce::Component
{
public:
    explicit SynthPage (Ctx& c);
    void resized() override;
private:
    OscPanel o1, o2, o3;
    SubPanel sub;
    NoisePanel noise;
    FilterSection filters;
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
    juce::ComboBox src, via, dst;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcA, viaA, dstA;
    GrabSlider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amtA;
    ChoiceBox curve, op;
    Stepper quant;
    GrabSlider smooth;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smA;
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
    Panel envPanel { "ENVELOPES" }, lfoPanel { "LFO" }, srcPanel { "SOURCES" }, matPanel { "MODULATION MATRIX" };
    TabStrip envTabs, lfoTabsA, lfoTabsB;
    EnvView envView;
    LfoView lfoView;
    ModChip envChip, lfoChip;
    juce::OwnedArray<juce::Component> envControls, lfoControls;
    juce::OwnedArray<ModChip> chips;
    juce::Viewport matView;
    juce::Component matContent;
    juce::OwnedArray<MatrixRow> rows;
    ChoiceBox ampEnv;
    int curEnv = 0, curLfo = 0;
    void buildEnvControls();
    void buildLfoControls();
    void layoutEnv (juce::Rectangle<int>);
    void layoutLfo (juce::Rectangle<int>);
};

// ------------------------------------------------------------------ ACID
class AcidPage : public juce::Component, public Animated
{
public:
    explicit AcidPage (Ctx& c);
    ~AcidPage() override;
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    Panel acidPanel { "ACID CIRCUIT", true }, playPanel { "PERFORMANCE" }, envPanel { "ACCENT  /  SLIDE" };
    ParamKnob morph, cutoff, res, envMod, decay, accent, slide, drive, calib, overdrive, nonlin;
    Stepper accVel;
    ChoiceBox mode, priority, glideMode, glideCurve;
    Stepper voices, unison;
    ParamKnob glide, uniDetune;
    Toggle retrig;
    juce::Rectangle<int> curveArea;
    float lastMorph = -1;
};

// ------------------------------------------------------------------ SEQ
// Piano roll: keyboard on the left, pitch rows, time running to the right,
// note lanes (velocity, gate, probability, ratchet, micro timing, mod) and accent / slide rows below.
class StepGrid : public juce::Component, public juce::SettableTooltipClient, public Animated, private juce::ScrollBar::Listener
{
public:
    StepGrid (Ctx& c);
    ~StepGrid() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void animate() override;
    void reload();
    be::SeqData data;

    // the same hands as Racoon's piano roll: a click on a note selects it (Shift adds), dragging moves every selected
    // note in time and pitch, its right edge changes their length, Alt+drag copies, dragging on an empty spot draws a
    // box, double click writes or erases. The pencil (B) keeps the quick way: click writes, drag paints, click erases
    void setDrawMode (bool on);
    bool isDrawMode() const { return drawMode; }
    std::function<void()> onDrawModeChanged;
    int numSelected() const { return (int) selection.size(); }
    struct Item { int start = 0; double len = 0.5; int pitch = 0; be::SeqStep attrs; bool slide = false; };
    using Key = std::pair<int, int>;                    // where a note starts (step) and its pitch
    void selectAllNotes();
    void clearSelection() { selection.clear(); repaint(); }
    void transposeSelected (int semis);                 // Up/Down: 1, Shift: 12
    void duplicateSelectedPitch (int semis);            // Cmd+Shift+Up/Down: a copy an octave up or down, on top
    void duplicateSelectedInTime();                     // Cmd+D: a copy right after
    void moveSelected (int steps);
    void deleteSelected();

    static constexpr int kMinPitch = -24, kMaxPitch = 48, kNumRows = kMaxPitch - kMinPitch + 1;
    // test helpers: component position of a step / pitch, and of a lane value
    juce::Point<float> posFor (double step, int pitch) const { return { xOfStep (step), yOfPitch (pitch) + rowH * 0.5f }; }
    void centreOnPitch (int p) { scrollY = (double) (kMaxPitch - p) - rollArea.getHeight() / (double) rowH * 0.5; updateScrollBars(); repaint(); }
    juce::Point<float> lanePos (double step, float value) const
    { auto r = laneArea.toFloat().withTrimmedTop (16.0f).withTrimmedBottom (5.0f); return { xOfStep (step), r.getBottom() - value * r.getHeight() }; }
private:
    Ctx& ctx;
    TabStrip laneTabs;
    juce::ScrollBar hbar { false }, vbar { true };
    int lane = 0;          // 0 vel, 1 gate, 2 prob, 3 ratchet, 4 micro, 5 mod
    int playStep = -1;
    int lastLen = -1, lastPattern = -1, seenVersion = -1;
    double scrollX = 0.0;  // first visible step
    double scrollY = 0.0;  // first visible row (row 0 = kMaxPitch)
    float stepW = 40.0f, rowH = 14.0f, rowsInView = 25.0f;
    juce::Rectangle<int> kbArea, rulerArea, rollArea, laneTabArea, laneArea, flagArea, laneLabelArea;
    juce::uint32 lastUserScroll = 0;

    enum class Drag { none, paint, move, resize, erase, lane, flag, ruler, key, rubber, moveSel, resizeSel };
    std::vector<Item> items() const;
    void rebuild (std::vector<Item>);
    std::set<Key> selection, selectionAtDown, dragSel;
    std::vector<Item> dragItems, clipboardItems;
    int lastDStep = 0, lastDPitch = 0;
    double lastDLen = 0.0;
    bool drawMode = false, dragCopy = false, downShift = false;
    Key downKey { -1, 0 };
    juce::Rectangle<float> rubberRect;
    juce::Point<float> downPos;
    static Key keyOf (const Item& it) { return { it.start, it.pitch }; }
    void applyDrag (int dStep, int dPitch, double dLen, bool copy);
    Drag drag = Drag::none;
    int dragHead = -1, dragPitch = 0, dragStartPitch = 0, lastPaintStep = -1, flagRow = -1, auditionNote = -999;
    bool flagSetTo = false, dragMoved = false;

    int pattern() const;
    int length() const;
    be::SeqPattern& pat() { return data.patterns[pattern()]; }
    bool isHead (int i) const;
    int headOf (int i) const;
    int chainEnd (int head) const;
    int pitchOf (int head) const;
    void setPitch (be::SeqStep& st, int p);
    float xOfStep (double step) const;
    double stepAtX (float x) const;
    float yOfPitch (int p) const;
    int pitchAtY (float y) const;
    juce::Rectangle<float> noteRect (int head) const;
    juce::Rectangle<float> chordRect (int step, int c) const;
    int chordPitch (int step, int c) const;     // as played (scale lock applied)
    int rawPitchAt (int step) const;            // main note pitch covering this step, unquantized
    int hitNote (juce::Point<float> pos, bool& onRightEdge, int& chordIdx) const;
    void eraseChain (int head);
    bool addNoteAt (int step, int pitch);       // main note if the step is empty, chord note otherwise
    void ensurePoly();
    int dragChord = -1;
    float dragStartLen = 0.0f;
    juce::String notice;
    juce::uint32 noticeTime = 0;
    void setLaneValue (int step, float y);
    void updateScrollBars();
    void ensureVisible();
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void commit();
    void paintKeyboard (juce::Graphics&);
    void paintRuler (juce::Graphics&);
    void paintRoll (juce::Graphics&);
    void paintLanes (juce::Graphics&);
};

class ExportHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ExportHandle (Ctx& c) : ctx (c) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Arrastra este botón a una pista MIDI de tu DAW y el patrón queda como clip."_u); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    Ctx& ctx;
    bool dragging = false;
};

class SeqPage : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    explicit SeqPage (Ctx& c);
    ~SeqPage() override;
    void resized() override;
    void visibilityChanged() override { if (isVisible()) grid.reload(); }
private:
    Ctx& ctx;
    Panel top { "Secuenciador  /  arpegiador" }, gridPanel { "Piano roll" };
    ChoiceBox mode, rate, root, scale, trigger, arpMode;
    Stepper length, arpOct;
    ParamKnob swing, gate, bpm;
    Toggle scaleLock, keyTranspose, hostSync, latch;
    juce::OwnedArray<juce::TextButton> patternBtns;
    juce::TextButton pencil, variation { "ALEATORIA" }, clear { "LIMPIAR" }, copy { "COPIAR" }, paste { "PEGAR" }, shiftL { "<<" }, shiftR { ">>" };
    ExportHandle exportHandle;
    PlayButton play;
    StepGrid grid;
    be::SeqPattern clipboard;
    bool hasClip = false;
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
};

// ------------------------------------------------------------------ FX
class DistPanel : public Panel
{
public:
    DistPanel (Ctx& c, int d);
    void resized() override;
private:
    Toggle on;
    ChoiceBox type;
    ParamKnob drive, tone, mix, out, shape, xlow, xhigh, lowDrive, highDrive;
};

class FxRackList : public juce::Component
{
public:
    FxRackList (Ctx& c, std::function<void (int)> onSelect);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int selected = 13;
private:
    Ctx& ctx;
    std::function<void (int)> onSelect;
    juce::OwnedArray<Toggle> toggles;
    int dragFrom = -1, dragTo = -1;
    int rowAt (int y) const;
};

class FxPage : public juce::Component
{
public:
    explicit FxPage (Ctx& c);
    void resized() override;
private:
    Ctx& ctx;
    DistPanel d1, d2;
    Panel distGlobal { "DRIVE ROUTING" }, rackPanel { "EFFECTS RACK" }, editPanel { "EFFECT" }, duckPanel { "DYNAMICS  /  SIDECHAIN" };
    ChoiceBox distRouting, distPosition;
    FxRackList rack;
    juce::OwnedArray<juce::Component> fxControls;
    // ducking
    ChoiceBox duckSrc, duckDiv, duckBand;
    ParamKnob duckDepth, duckAttack, duckHold, duckRelease, duckCurve, duckFreq, duckThresh;
    Stepper duckNote, duckChannel;
    struct GrMeter : public juce::Component, public Animated
    {
        GrMeter (Ctx& c) : ctx (c) { ctx.registerAnimated (this); }
        ~GrMeter() override { ctx.unregisterAnimated (this); }
        void paint (juce::Graphics&) override;
        void animate() override { if (animVisible (this)) repaint(); }
        Ctx& ctx;
    } gr;
    void showFx (int id);
};

// ------------------------------------------------------------------ MIX
class LayerStrip : public Panel, public Animated
{
public:
    LayerStrip (Ctx& c, int layer);
    ~LayerStrip() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void animate() override;
private:
    Ctx& ctx;
    int l;
    ParamKnob level, pan, width, tune, drive, lowCut, highCut, delay;
    ChoiceBox driveType, path;
    Toggle mute, solo, invert;
    Stepper keyLo, keyHi, velLo, velHi;
    juce::TextButton savePreset { "SAVE" }, loadPreset { "LOAD" };
    juce::String contents;
};

class MixPage : public juce::Component
{
public:
    explicit MixPage (Ctx& c);
    void resized() override;
private:
    Ctx& ctx;
    LayerStrip l0, l1, l2, l3;
    Panel busPanel { "LAYER BUS" }, midiPanel { "MIDI  /  TUNING" }, impPanel { "ANALOG IMPERFECTION" }, evoPanel { "BASS EVOLUTION" };
    ChoiceBox layerMode, tuning, tuningRoot, evoLen, evoShape;
    ParamKnob xfade, velCurve, tune, humanize, impAmount, impDrift, impFilter, impAttack, impSat, evoAmount;
    Stepper bendUp, bendDown, mpeRange, transpose, keyLo, keyHi;
    Toggle mpe;
    juce::TextButton loadScl { "LOAD .SCL" };
    juce::Label outInfo;
    std::unique_ptr<juce::FileChooser> chooser;
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
    Panel scopePanel { "OSCILLOSCOPE" }, specPanel { "SPECTRUM" }, harmPanel { "HARMONICS" }, fundPanel { "FUNDAMENTAL" },
          meterPanel { "LEVEL  /  PHASE" }, focusPanel { "BASS FOCUS", true }, envPanel { "MODULATORS" }, histPanel { "MIDI HISTORY" };
    ScopeView scope;
    SpectrumView spectrum;
    HarmonicsView harmonics;
    Meter meter;
    juce::TextButton prePost { "BEFORE / AFTER FX" };
    float detectedHz = 0.0f, subMin = 1.0f, subMax = 0.0f, subStability = 1.0f;
    int frame = 0;
    be::FocusReport report;
    std::vector<float> scopeBuf;
    juce::Rectangle<int> fundArea, meterArea, focusArea, envArea, histArea;
};

// ------------------------------------------------------------------ PRESETS
// ------------------------------------------------------------------ INICIO (simple page)
// The first screen: eight big macro knobs, the pattern with PLAY, and the sounds by category.
class MiniRoll : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit MiniRoll (Ctx& c);
    ~MiniRoll() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void animate() override;
private:
    Ctx& ctx;
    be::SeqData data;
    int seen = -1, playStep = -1;
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
    Panel soundPanel { "Tu sonido" }, patternPanel { "Patrón"_u }, presetsPanel { "Sonidos" }, helpPanel { "Empieza aquí"_u };
    ScopeView scope;
    juce::OwnedArray<ParamKnob> macros;
    ParamKnob glide, tempo, swing;
    ChoiceBox playMode;
    PlayButton play;
    juce::OwnedArray<juce::TextButton> patternBtns;
    juce::TextButton variation { "ALEATORIA" }, clear { "LIMPIAR" }, openRoll { "ABRIR PIANO ROLL" }, prevBtn { "<" }, nextBtn { ">" };
    MiniRoll mini;
    juce::ComboBox category;
    juce::ListBox list;
    std::vector<be::PresetInfo> shown;
    juce::String lastMacroNames;
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void fillList();
    void updateMacroLabels();
};

class PresetPage : public juce::Component, private juce::ListBoxModel, public juce::FileDragAndDropTarget
{
public:
    explicit PresetPage (Ctx& c);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void refresh();
    void visibilityChanged() override { if (isVisible()) refresh(); }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    Ctx& ctx;
    Panel filterPanel { "BROWSE" }, listPanel { "LIBRARY" }, dnaPanel { "BASS DNA", true }, randPanel { "INTELLIGENT RANDOMIZER" }, morphPanel { "BASS MORPH XY" };
    juce::TextEditor search;
    juce::ComboBox category, genre, character, synth, bank;
    juce::ToggleButton favOnly { "FAVOURITES" };
    GrabSlider minIntensity, maxComplexity;
    juce::ListBox list;
    juce::TextButton loadBtn { "LOAD" }, saveBtn { "SAVE AS" }, deleteBtn { "DELETE" }, previewBtn { "PREVIEW" }, favBtn { "FAVOURITE" };
    std::vector<be::PresetInfo> all, shown;
    // DNA
    juce::TextButton dnaLoad { "ANALYSE SAMPLE" }, dnaApply { "APPLY PATCH" };
    juce::Label dnaSummary;
    be::DnaResult dna;
    juce::String dnaName;
    bool dnaHover = false;
    // randomizer
    GrabSlider randAmount;
    juce::ToggleButton lockOsc { "OSC" }, lockSub { "SUB" }, lockFilter { "FILT" }, lockEnv { "ENV" }, lockLfo { "LFO" }, lockFx { "FX" }, lockMatrix { "MTX" };
    juce::ToggleButton keepFund { "KEEP ROOT" }, keepVol { "KEEP LEVELS" }, keepChar { "KEEP TYPES" };
    juce::TextButton randBtn { "ALEATORIA" };
    // morph
    XYPad xy;
    Toggle xyOn;
    juce::OwnedArray<juce::TextButton> cornerBtns;
    std::unique_ptr<juce::FileChooser> chooser;

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void applyFilter();
    void saveDialog();
    void analyse (const juce::File& f);
};
} // namespace be::ui

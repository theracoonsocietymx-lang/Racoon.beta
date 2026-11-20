#pragma once
#include "RacoonProcessor.h"
#include "JerboaPatterns.h"

namespace racoon::jerboa
{
// Jerboa: time and volume by patterns. It keeps a rolling recording of what comes in; the pattern that plays says,
// sample by sample, how far back to read (stutters, tape stops, reverses, half speed, scratches) and how loud
// (gates, pumping, silences). Notes Do 2 to Si 3 switch between the 24 patterns of the bank, on the beat.
class Processor : public RacoonProcessor
{
public:
    Processor();
    void prepareDsp (double, int) override;
    void processDsp (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void resetDsp() override;
    std::vector<FactoryPreset> factoryPresets() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    std::vector<SeqNote> demoPattern() const override;
    double demoBpm() const override { return 124.0; }
    int demoBars() const override { return 4; }
    bool demoPlaysNotes() const override { return false; }

    static const juce::StringArray& bankNames();
    static const juce::StringArray& quantNames();
    static constexpr int firstNote = 36, firstVolNote = 60;

    // for the screen
    std::atomic<int> uiTimePattern { 0 }, uiVolPattern { 0 };
    std::atomic<float> uiPhase { 0.0f }, uiSpeed { 1.0f }, uiOffset { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    // history: a ring of 2^k samples per channel, written always (even when the song is stopped)
    std::vector<float> hist[2];
    int histMask = 0;
    juce::int64 written = 0;              // samples written so far (absolute write index)

    // what plays and what is waiting for its moment
    int timePat = 0, volPat = 0;
    double timeAnchor = 0.0, volAnchor = 0.0;
    struct Pending { bool on = false; double at = 0.0; int timePat = -1, volPat = -1; float vel = 1.0f; };
    Pending pending;
    int heldSwitches = 0;
    float switchVel = 1.0f;
    int lastPatronParam = -1;

    // reading: the main head, and an old head that fades out after a jump
    double dPrev = 0.0, speed = 1.0;
    bool havePrev = false;
    double dOld = 0.0, sOld = 1.0;
    int xfLeft = 0, xfLen = 1;
    float tapeLp[2] {}, tapeC = 1.0f;
    float stopGain = 1.0f; int stoppedFor = 0;
    float volNow = 1.0f;

    // hands
    double platter = 0.0, platterVel = 0.0;
    bool holdOn = false; juce::int64 holdCapture = 0, holdStart = 0;

    double freeBeat = 0.0;
    void handleMidi (const juce::MidiMessage&, double ppqAtEvent);
    void requestSwitch (int tp, int vp, float vel, double ppqNow);
    float readAt (int ch, double d) const;
};
}

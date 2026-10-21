#pragma once
#include "Common.h"

// Listening to a sample in the browser before using it: one sound at a time, mixed on top of
// whatever the song is doing, through the same audio output. Clicking another sample replaces it.
namespace racoon
{
class SamplePreview
{
public:
    static SamplePreview& get (te::Engine&);
    ~SamplePreview();
    bool play (const juce::File&);          // false if it is not an audio file it can read
    void stop();
    bool isPlaying() const;
    juce::File currentFile() const { return current; }
    void setGainDb (float db) { player.setGain (juce::Decibels::decibelsToGain (db)); }

private:
    explicit SamplePreview (te::Engine&);
    te::Engine& engine;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread reader { "Racoon preview" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    juce::AudioSourcePlayer player;
    juce::File current;
    bool attached = false;
};
}

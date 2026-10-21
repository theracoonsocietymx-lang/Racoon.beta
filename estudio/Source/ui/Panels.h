#pragma once
#include "engine/AudioTools.h"
#include "Widgets.h"

namespace racoon::ui
{
// Export window: format, bit depth, sample rate, range, stems, normalise, dither.
class ExportPanel : public juce::Component
{
public:
    explicit ExportPanel (Session&);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    static void show (Session&);
private:
    Session& session;
    juce::ComboBox format, bits, rate, range;
    juce::ToggleButton stems { "Stems: un archivo por pista" }, normalise { "Normalizar a -0.3 dB" },
                       dither { "Dither al bajar a 16 bits" };
    juce::TextButton exportButton { "EXPORTAR" }, cancelButton { "CANCELAR" };
    juce::String status;
    void run();
public:
    // after a mix export: its loudness (also shown in the status line)
    std::optional<audio::Loudness> report;
    juce::String getStatus() const { return status; }
};

// Inspector for an audio clip: gain, pitch, warp, reverse and fades, with a big waveform.
class AudioClipPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit AudioClipPanel (ViewState&);
    void setClip (te::WaveAudioClip*);
    te::WaveAudioClip* getClip() const { return clip.get(); }
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    ViewState& vs;
    te::WaveAudioClip::Ptr clip;
    std::unique_ptr<te::SmartThumbnail> thumb;
    GrabSlider gain, pitch, fadeIn, fadeOut;
    juce::ToggleButton warp { "Seguir el tempo (warp)" }, reverse { "Invertir" };
    juce::Label title;
    void timerCallback() override { refresh(); repaint(); }
    void refresh();

    // warp markers on the waveform: double click adds one where a hit is, drag moves it in time (the audio between
    // markers stretches), right click removes it. The waveform is drawn warped, as it will sound.
public:
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    // for tests and menus: add a marker at a time of the source file, move marker i to a new (linear) time
    int addWarpMarker (double sourceSeconds);
    void moveWarpMarker (int index, double warpSeconds);
    int markersAtHits (float sensitivity = 0.5f);   // a marker on every hit the file has (returns how many)
    int markersToGrid (double gridBeats);            // every marker to the nearest step of the song's grid
    juce::Rectangle<int> waveArea() const;
    float xForWarpTime (double seconds) const;
private:
    int dragIndex = -1;
    double viewSpan() const;
    double warpTimeForX (float x) const;
    int markerAt (float x) const;
};

void showAudioSettings (te::Engine&);
void showShortcuts();
void showAbout();
}

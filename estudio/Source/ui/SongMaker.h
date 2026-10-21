#pragma once
#include "engine/Arranger.h"
#include "Widgets.h"

namespace racoon::ui
{
// "Crear la rola completa": how long the song lasts, the sections it will have (drawn to scale, with the minute
// where each one starts), what the selection holds, and the two extras (transitions, the high-pass on the master).
class SongMakerPanel : public juce::Component
{
public:
    SongMakerPanel (Session&, juce::Array<te::AudioTrack*> tracks, double startBeat, double endBeat);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void (const arranger::Result&)> onDone;

    // opens the window. Returns false (and says why in "why") when the selection cannot make a song
    static bool show (Session&, juce::Array<te::AudioTrack*> tracks, double startBeat, double endBeat,
                      std::function<void (const arranger::Result&)> done, juce::String& why);

    int totalBars() const;
    void setMinutes (double);
    arranger::Result create();          // builds the song
    void accept();                      // the button: builds, reports, closes

private:
    Session& session;
    juce::Array<te::AudioTrack*> tracks;
    double start, end;
    juce::StringArray found;
    juce::Slider minutes;
    juce::ToggleButton transitions { "Transiciones: riser antes de cada drop, impacto al entrar, bajada al breakdown" },
                       sweep { "Barrido de graves en el master: sin graves en el breakdown, filtro que abre en los build-ups" };
    juce::TextButton createButton { "CREAR LA ROLA" }, cancelButton { "CANCELAR" };
    juce::Rectangle<int> planArea, foundArea;
};
}

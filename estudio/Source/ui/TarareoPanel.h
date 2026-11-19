#pragma once
#include "Theme.h"
#include "engine/Session.h"
#include "engine/Genres.h"

// TARAREO A ROLA: the song from a hummed melody and a beatbox. Pick the genre (it sets the tempo), record the melody
// (4 bars after a bar of clicks) and the rhythm with your mouth (2 bars), or bring them from files, and Racoon makes the
// genre's block in the key you hummed: Squirrel plays your melody, Chinchilla sings it at the end, Hamster plays your
// rhythm with the kit, and the bass, chords and pads follow the chords that hold your notes.
namespace racoon::ui
{
class TarareoPanel : public juce::Component, private juce::Timer
{
public:
    TarareoPanel (Session&, const juce::String& genreId);
    ~TarareoPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}

    std::function<void (const Session::ResultadoTarareo&)> onBuilt;
    std::function<void()> onClose;

    // what was recorded or imported (tests set them directly)
    void setToma (bool melodia, const juce::AudioBuffer<float>& audio, double sampleRate);
    void importFile (bool melodia, const juce::File&);
    bool hasToma (bool melodia) const { return (melodia ? melodiaAudio : beatboxAudio).getNumSamples() > 0; }
    juce::String estado (bool melodia) const { return melodia ? estadoMelodia : estadoBeatbox; }
    const Genre* genre() const;
    void build();

    juce::ComboBox genero;
    juce::TextButton grabarMelodia, importarMelodia, grabarBeatbox, importarBeatbox, armar, cerrar { "X" };
    juce::ToggleButton clic;

private:
    Session& session;
    juce::Array<const Genre*> lista;
    juce::AudioBuffer<float> melodiaAudio, beatboxAudio;
    double tomaRate = 48000.0, tomaBpm = 0.0;
    juce::String estadoMelodia, estadoBeatbox, aviso;
    juce::Rectangle<int> card, filaMelodia, filaBeatbox;

    struct Grabadora;
    std::unique_ptr<Grabadora> grabadora;
    bool grabandoMelodia = false;
    void grabar (bool melodia);
    void terminarGrabacion();
    void timerCallback() override;
    void analizar (bool melodia);
    void actualizar();
};
}

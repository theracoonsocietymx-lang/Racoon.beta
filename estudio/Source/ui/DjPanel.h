#pragma once
#include "Theme.h"
#include "engine/Session.h"
#include "engine/DJ.h"

// MODO DJ: drag songs in (or add them). Racoon measures each one (tempo to a hundredth, key on the Camelot wheel,
// energy), orders them into a set that walks the wheel with the energy going up, saves the set as a playlist, and
// builds a track to mix with any of them: at its tempo, in its key, with DJ intro and outro.
namespace racoon::ui
{
class DjPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit DjPanel (Session&);
    ~DjPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

    void addFiles (const juce::Array<juce::File>&);
    void addAnalysis (const dj::Analisis&);                // tests: an already measured song
    bool busy() const;                                     // still measuring
    int numSongs() const { return (int) songs.size(); }
    const dj::Analisis& song (int i) const { return songs[(size_t) i].a; }
    void sortSet();
    bool saveSet (const juce::File& m3u);
    void buildFor (int index);
    int selected = -1;

    std::function<void (const Session::ResultadoDJ&)> onBuilt;
    std::function<void()> onClose;
    juce::TextButton agregar, ordenar, guardar, armar, cerrar { "X" };

private:
    Session& session;
    struct Song { dj::Analisis a; bool done = false; juce::String transicion; };
    std::vector<Song> songs;
    juce::ThreadPool pool { 2 };
    juce::Rectangle<int> card, listArea;
    bool dragging = false, ordered = false;
    juce::String aviso;
    int rowH = 40;
    void timerCallback() override { repaint(); if (! busy()) stopTimer(); updateButtons(); }
    void updateButtons();
    void transiciones();
};
}

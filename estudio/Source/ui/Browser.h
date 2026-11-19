#pragma once
#include "Widgets.h"
#include "DragData.h"

namespace racoon::ui
{
// Left panel: instruments (Racoon family first), effects and samples. Everything can be dragged to the timeline.
class Browser : public juce::Component, private juce::ChangeListener
{
public:
    explicit Browser (ViewState&);
    ~Browser() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshItems();
    void showPluginScanner();

    std::function<void (const CatalogItem&)> onAddItem;      // + or double click: a channel, or the master for Hedgehog
    std::function<void (const CatalogItem&)> onInsertItem;   // into the selected channel
    std::function<void (const juce::File&)> onAddFile;

    enum Tab { instruments, effects, samples };
    void setTab (Tab);
    void preview (const juce::File&);                     // plays a sample in the browser (one at a time)
    void openFolder (const juce::File& f) { goToFolder (f); }
    juce::Component* sampleList() const;                  // the list of the Samples tab (for tests)
    // the search box of the Samples tab: "tom" lists every audio with tom in its name (or in its folder's name)
    // in the Biblioteca, Muestras Racoon, your sample folder and the project's own audio
    void searchSamples (const juce::String& query);
    juce::Array<juce::File> sampleResults() const;        // what the search found (for tests)

private:
    ViewState& vs;
    Tab tab = instruments;
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextEditor search, sampleSearch;
    struct Indexed { juce::File file; juce::String key; };
    std::vector<Indexed> sampleIndex;
    juce::Array<juce::File> indexedRoots;
    void buildSampleIndex();
    int resultCount = -1;
    juce::TextButton scanButton { "BUSCAR PLUGINS" }, folderButton { "CARPETA..." }, libraryButton { "Biblioteca" }, muestrasButton { "Muestras" }, efectosButton { "Efectos" };
    GlyphButton backButton { Glyph::back }, forwardButton { Glyph::forward };
    juce::Array<juce::File> backStack, forwardStack;
    void goToFolder (const juce::File&, bool remember = true);
    void goBack();
    void goForward();

    class ItemList;
    class FileList;
    std::unique_ptr<ItemList> items;
    std::unique_ptr<FileList> files;
    juce::Viewport itemViewport;
    juce::File sampleFolder;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshItems(); }
};
}

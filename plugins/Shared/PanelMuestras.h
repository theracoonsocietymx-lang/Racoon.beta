#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace racoon::biblio
{
// A panel over the plugin to try the sounds of Muestras Racoon one after another:
// a click (or the arrow keys) loads the sound and the panel stays open; CERRAR or Esc puts it away.
class PanelMuestras : public juce::Component, private juce::ListBoxModel
{
public:
    // shows the panel over `inside` (the plugin window), replacing any other one that was open
    static void mostrar (juce::Component& inside, const juce::String& titulo, const juce::StringArray& sugeridas,
                         std::function<void (const juce::String&)> onElegir, const juce::String& actual = {});
    static void cerrarTodo();

    PanelMuestras (const juce::String& titulo, const juce::StringArray& sugeridas, std::function<void (const juce::String&)> onElegir, const juce::String& actual);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void cerrar();

private:
    juce::String titulo;
    juce::StringArray cats;                         // suggested first, then the rest
    int sugeridasCount = 0;
    std::function<void (const juce::String&)> onElegir;
    juce::String actual;
    juce::TextButton cerrarBtn { "CERRAR" };
    juce::ComboBox categoria, fuente;
    juce::TextEditor buscar;
    juce::ListBox lista;
    juce::Label pie;
    std::vector<int> visibles;                      // indices into Catalogo::grupos

    void llenarFuentes();
    void filtrar();
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
};
}

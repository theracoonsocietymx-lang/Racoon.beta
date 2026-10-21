#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

// Un kit de Muestras Racoon tocado por notas General MIDI: lo usa Mole como su sonido propio (y cualquier plugin que
// escriba ritmos). Carga en el hilo de mensajes; el hilo de audio solo lee una estructura que nunca cambia y la suelta sin
// liberar memoria (la anterior se borra después, desde el hilo de mensajes).
namespace racoon
{
class LibrarySampler
{
public:
    LibrarySampler();
    ~LibrarySampler();
    void prepare (double sampleRate);
    void reset();
    bool loadKit (const juce::String& kitIdOrName);     // hilo de mensajes; "" quita el kit
    juce::String kitId() const { return currentKit; }
    bool hasKit() const { return live.load() != nullptr; }
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int startSample, int numSamples, float gain = 1.0f);
    int notesMapped() const;
    static int gmNoteFor (const juce::String& hamsterRole, int nth);   // primera, segunda... nota GM de ese rol

    struct Layer { juce::AudioBuffer<float> audio; double rate = 48000.0; };
    struct Sound { std::vector<std::shared_ptr<Layer>> layers; int chokeGroup = 0; bool chokes = false; };
    struct Kit { std::shared_ptr<Sound> byNote[128]; };

private:
    struct Voice { const Layer* layer = nullptr; double pos = 0.0, inc = 1.0; float gain = 0.0f, fade = 1.0f; int note = -1, choke = 0; bool fading = false; };
    static constexpr int numVoices = 32;
    Voice voices[numVoices];
    std::atomic<const Layer*> busy[numVoices];         // lo que suena en cada voz, para que el hilo de mensajes lo sepa
    bool kitInUse (const Kit&) const;
    void collect (juce::uint32 now);
    double sr = 48000.0;
    std::atomic<Kit*> live { nullptr };
    std::shared_ptr<Kit> owner;
    std::vector<std::pair<std::shared_ptr<Kit>, juce::uint32>> retired;
    juce::String currentKit;
    juce::Random rng;
    void start (const Kit&, int note, float velocity);
};
} // namespace racoon

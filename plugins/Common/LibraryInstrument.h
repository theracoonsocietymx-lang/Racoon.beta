#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

// Un instrumento cromático de Muestras Racoon (piano, vibráfono, cuerdas, guitarras...) tocado por notas: lee su .sfz,
// reparte las muestras por tecla y por fuerza, repite los bucles de los instrumentos sostenidos y afina cada nota desde
// la muestra más cercana. Chipmunk y Ferret lo usan en lugar de su sonido sintetizado. Como LibrarySampler, carga en el
// hilo de mensajes y el hilo de audio solo lee una estructura que nunca cambia.
namespace racoon
{
class LibraryInstrument
{
public:
    LibraryInstrument();
    ~LibraryInstrument();
    void prepare (double sampleRate);
    void reset();
    bool load (const juce::String& instrumentId);       // hilo de mensajes; "" lo quita
    juce::String id() const { return currentId; }
    bool loaded() const { return live.load() != nullptr; }
    int numRegions() const;
    // envolvente (segundos, segundos, 0..1, segundos); release < 0 usa el del instrumento
    void setEnvelope (float a, float d, float s, float r) { envA = a; envD = d; envS = s; envR = r; }
    void setTranspose (int semitones) { transpose = semitones; }
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int startSample, int numSamples, float gain = 1.0f);
    void allNotesOff();

    struct Region
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        double rate = 48000.0;
        int loKey = 0, hiKey = 127, loVel = 1, hiVel = 127, center = 60;
        int loopStart = -1, loopEnd = -1;            // en muestras del archivo; -1 sin bucle
        float tuneCents = 0.0f, gainDb = 0.0f;
    };
    struct Instrument
    {
        std::vector<Region> regions;
        float release = 0.4f, velTrack = 0.8f;
        bool sustained = false;                      // algún bucle: el sonido dura mientras se tiene la tecla
    };

private:
    struct Voice
    {
        const Region* region = nullptr;
        double pos = 0.0, inc = 1.0;
        float gain = 0.0f, env = 0.0f, age = 0.0f, rel = 1.0f, steal = 1.0f;
        int note = -1;
        bool released = false, stealing = false;
    };
    static constexpr int numVoices = 48;
    Voice voices[numVoices];
    std::atomic<const Instrument*> live { nullptr };
    std::shared_ptr<Instrument> owner;
    std::vector<std::pair<std::shared_ptr<Instrument>, juce::uint32>> retired;
    std::atomic<const Instrument*> playing { nullptr };  // la que el audio leyó en su último bloque
    std::atomic<bool> panic { false };
    juce::String currentId;
    double sr = 48000.0;
    float envA = 0.002f, envD = 1.0f, envS = 1.0f, envR = -1.0f;
    int transpose = 0;
    void start (const Instrument&, int note, float velocity);
    void collect (juce::uint32 now);
};

std::shared_ptr<LibraryInstrument::Instrument> loadSfzInstrument (const juce::File& sfz);
} // namespace racoon

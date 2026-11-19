#pragma once
#include "Session.h"

// Separar en stems: a song (an audio clip) becomes four clips on four new channels, Voz, Bateria, Bajo and Otros,
// in the same place and with the same settings. The separation is Demucs v4 (Meta, MIT) run by demucs.cpp
// (MIT) inside Racoon, with no internet once the model is on disk. The model (81 MB) is downloaded once.
namespace racoon::stems
{
juce::File modelFile();                     // Music/Racoon/Modelos/ggml-model-htdemucs-4s-f16.bin (or RACOON_STEMS_MODEL)
bool modelReady();
juce::URL modelUrl();

// downloads the model in the background; progress 0..1 and the end come back on the message thread
void downloadModel (std::function<void (float)> progress, std::function<void (bool ok, const juce::String& message)> done);
bool downloading();

// starts separating the clip's file in the background. status: a line for the status bar while it works.
// done: on the message thread, after the four channels are in the song (ok) or with the reason it did not happen.
bool start (Session&, te::WaveAudioClip&, std::function<void (const juce::String&)> status,
            std::function<void (bool ok, const juce::String& message)> done);
bool running();
void cancel();

// the same, waiting for it (for tests and scripts). Returns the new clips (voz, bateria, bajo, otros) or empty.
juce::Array<te::WaveAudioClip*> separateNow (Session&, te::WaveAudioClip&, juce::String& message);
}

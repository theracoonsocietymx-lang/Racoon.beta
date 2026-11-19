#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

// The bridge between Racoon and demucs.cpp. Nothing of Eigen crosses this line: the library is compiled with its
// own flags, and Racoon only sees plain float vectors.
namespace racoon::stems
{
constexpr int sampleRate = 44100;                 // what the model was trained on
constexpr int numStems = 4;                       // drums, bass, other, vocals (the model's order)

struct Result
{
    bool ok = false;
    std::string error;
    std::vector<float> out[numStems][2];          // [stem][channel], same length as the input
};

// Separates a stereo song at 44.1 kHz. progress goes 0..1; when cancel turns true the work stops (ok = false).
// threads: how many pieces of the song run at once (each piece is a full model pass with overlap at the seams).
Result separate (const std::string& modelFile, const std::vector<float>& left, const std::vector<float>& right,
                 int threads, std::function<void (float)> progress, const std::atomic<bool>* cancel);

// true if the file starts like a Demucs 4-stem ggml model (the 4 magic bytes, and a sane size)
bool looksLikeModel (const std::string& modelFile);
}

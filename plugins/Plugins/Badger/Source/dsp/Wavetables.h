#pragma once
#include "Common.h"
#include <memory>
#include <vector>
#include <functional>

namespace be
{
// ---------------------------------------------------------------- band-limited, mip-mapped wavetable
class WaveTable
{
public:
    static constexpr int kBaseSize = 2048;
    static constexpr int kLevels = 11;          // level L: length = max(32, 2048 >> L), harmonics <= length/2
    static constexpr int kMaxHarm = kBaseSize / 2;

    // build from time-domain single cycles (each kBaseSize samples)
    static std::shared_ptr<WaveTable> fromCycles (const std::vector<std::vector<float>>& cycles, bool normalise = true);
    // build from harmonic spectra (amplitude per harmonic, index 0 = fundamental), optional phases
    static std::shared_ptr<WaveTable> fromSpectra (const std::vector<std::vector<float>>& amps,
                                                   const std::vector<std::vector<float>>* phases = nullptr,
                                                   bool normalise = true);

    int numFrames() const { return frames; }
    static int levelFor (float phaseInc)        // phaseInc = f / sr
    {
        const float allowed = 0.5f / std::max (phaseInc, 1.0e-7f);
        int L = 0;
        while (L < kLevels - 1 && (kMaxHarm >> L) > allowed) ++L;
        return L;
    }
    static int lengthOf (int level) { return std::max (32, kBaseSize >> level); }

    // phase in [0,1), pos in [0,1]
    inline float sample (float phase, float pos, int level) const
    {
        const float fp = pos * (float) (frames - 1);
        const int f0 = (int) fp;
        const int f1 = std::min (f0 + 1, frames - 1);
        const float ft = fp - (float) f0;
        const int len = lengthOf (level);
        const float idx = phase * (float) len;
        int i0 = (int) idx;
        const float t = idx - (float) i0;
        i0 &= (len - 1);
        const int i1 = (i0 + 1) & (len - 1);
        const float* a = data.data() + offsetOf (f0, level);
        const float* b = data.data() + offsetOf (f1, level);
        const float va = a[i0] + (a[i1] - a[i0]) * t;
        const float vb = b[i0] + (b[i1] - b[i0]) * t;
        return va + (vb - va) * ft;
    }

    // a frame's base-level cycle (for display)
    const float* frameData (int frame, int level = 0) const { return data.data() + offsetOf (juce::jlimit (0, frames - 1, frame), level); }
    // harmonic spectrum of a frame (computed at build time)
    const std::vector<float>& spectrum (int frame) const { return spectra[(size_t) juce::jlimit (0, frames - 1, frame)]; }

private:
    int frames = 0;
    int frameStride = 0;
    std::vector<float> data;
    std::vector<std::vector<float>> spectra;
    int offsetOf (int frame, int level) const
    {
        int off = frame * frameStride;
        for (int l = 0; l < level; ++l) off += lengthOf (l);
        return off;
    }
    void allocate (int numFrames);
    void buildFrameFromSpectrum (int frame, const float* re, const float* im);
};

using WaveTablePtr = std::shared_ptr<const WaveTable>;

// ---------------------------------------------------------------- sample data
struct SampleData
{
    std::vector<float> data;    // mono
    double sampleRate = 48000.0;
    float rootNote = 36.0f;     // MIDI note of the recorded pitch
    juce::String name;
};
using SamplePtr = std::shared_ptr<const SampleData>;

// ---------------------------------------------------------------- lock-free swap of shared assets
template <typename T>
class AssetSlot
{
public:
    const T* get() const noexcept { return current.load (std::memory_order_acquire); }
    std::shared_ptr<const T> getShared() const { const juce::SpinLock::ScopedLockType l (lock); return owner; }
    void set (std::shared_ptr<const T> p)   // message thread
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (owner) retired.push_back ({ owner, juce::Time::getMillisecondCounter() });
        owner = std::move (p);
        current.store (owner.get(), std::memory_order_release);
        collect();
    }
    void collect()
    {
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const auto& r) { return now - r.second > 2000; }), retired.end());
    }
private:
    std::atomic<const T*> current { nullptr };
    std::shared_ptr<const T> owner;
    std::vector<std::pair<std::shared_ptr<const T>, juce::uint32>> retired;
    juce::SpinLock lock;
};

// ---------------------------------------------------------------- factory content (built once per process)
class FactoryBank
{
public:
    static FactoryBank& get();
    const WaveTable* table (int i) const { return tables[(size_t) juce::jlimit (0, (int) tables.size() - 1, i)].get(); }
    WaveTablePtr tableShared (int i) const { return tables[(size_t) juce::jlimit (0, (int) tables.size() - 1, i)]; }
    const SampleData* sample (int i) const { return samples[(size_t) juce::jlimit (0, (int) samples.size() - 1, i)].get(); }
    SamplePtr sampleShared (int i) const { return samples[(size_t) juce::jlimit (0, (int) samples.size() - 1, i)]; }
    int numTables() const { return (int) tables.size(); }
    // shared basic shapes for fast VA Custom fallback
    const WaveTable* sineTable() const { return sine.get(); }
private:
    FactoryBank();
    std::vector<WaveTablePtr> tables;
    std::vector<SamplePtr> samples;
    WaveTablePtr sine;
};

// ---------------------------------------------------------------- table builders (message thread)
namespace TableBuilders
{
    // additive: 64 user harmonic levels, tilt (-1..1), odd/even balance (-1..1); frames sweep partial count
    WaveTablePtr additive (const std::vector<float>& harmonics, float tilt, float oddEven);
    // spectral transform of a source spectrum; frames sweep the transform amount
    WaveTablePtr spectral (const std::vector<float>& sourceSpectrum, int mode, float param);
    // single drawn cycle (e.g. VA Custom)
    WaveTablePtr fromCycleFunction (std::function<float (float)> fn, int numFrames = 1);
    // import an audio file as wavetable (cycle length 2048, or auto-detected by pitch)
    WaveTablePtr fromAudio (const juce::AudioBuffer<float>& audio, double sampleRate, int cycleLength);
}

// pitch detection (YIN) used by sample import and Bass DNA. Returns Hz or 0.
float detectPitchHz (const float* x, int n, double sampleRate, float minHz = 25.0f, float maxHz = 1200.0f);
} // namespace be

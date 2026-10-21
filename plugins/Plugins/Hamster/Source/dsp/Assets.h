#pragma once
#include "Common.h"

namespace be
{
// ---------------------------------------------------------------- audio sample (stereo, with analysis)
struct SampleData
{
    std::vector<float> l, r;          // r empty -> mono
    double sampleRate = 48000.0;
    juce::String name;
    float pitchHz = 0.0f;             // detected fundamental (0 = unpitched)
    std::vector<int> transients;      // onset positions (samples) for slicing
    int size() const { return (int) l.size(); }
    bool stereo() const { return ! r.empty(); }
    const float* right() const { return r.empty() ? l.data() : r.data(); }
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
        // an old asset goes 2 s after it was replaced, and never while a voice is still playing it
        // (a long or looping sample can sound far longer than that after the kit changed)
        const auto now = juce::Time::getMillisecondCounter();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [this, now] (const auto& r) { return now - r.second > 2000 && ! (inUse && inUse (r.first.get())); }), retired.end());
    }
    // message thread: tells whether the audio thread still reads an asset
    std::function<bool (const T*)> inUse;
private:
    std::atomic<const T*> current { nullptr };
    std::shared_ptr<const T> owner;
    std::vector<std::pair<std::shared_ptr<const T>, juce::uint32>> retired;
    juce::SpinLock lock;
};

float detectPitchHz (const float* x, int n, double sr, float minHz = 25.0f, float maxHz = 2000.0f);
std::vector<int> detectTransients (const float* x, int n, double sr, int maxCount = 64);
SamplePtr loadSampleFile (const juce::File& f, double maxSeconds = 30.0);
SamplePtr makeSample (std::vector<float> l, std::vector<float> r, double sr, const juce::String& name);

// ---------------------------------------------------------------- synthesized factory PCM set (no third-party audio)
class FactoryPcm
{
public:
    static const FactoryPcm& get();
    const SampleData* sample (int index) const;        // index into factorySampleNames(), nullptr for "User Slots"
    int count() const { return (int) samples.size(); }
    static int defaultForRole (int role);
private:
    FactoryPcm();
    std::vector<SamplePtr> samples;
};

// ---------------------------------------------------------------- impulse responses for the convolution aux types
juce::AudioBuffer<float> makeImpulseResponse (int type, float size, float tone, double sr);   // type 0 room, 1 plate, 2 hall
} // namespace be

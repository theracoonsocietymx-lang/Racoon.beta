#include "Bridge.h"

namespace be
{
struct KickBassBridge::Block
{
    juce::uint32 magic;
    juce::uint32 version;
    std::atomic<juce::uint32> kickSeq;
    std::atomic<juce::uint32> bassSeq;
    double kickWallMs;
    double bassWallMs;
    BridgeKickSchedule kick;
    BridgeBassState bass;
};

static_assert (std::atomic<juce::uint32>::is_always_lock_free, "bridge needs lock-free atomics");
static constexpr juce::uint32 kMagic = 0x4B42444Eu;   // "KBDN"
static constexpr juce::uint32 kVersion = 1;

juce::File KickBassBridge::sharedFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Studio DOCEVEiNTE");
    dir.createDirectory();
    return dir.getChildFile ("KickBassBridge.bin");
}

KickBassBridge::KickBassBridge()
{
    auto f = sharedFile();
    const auto need = (juce::int64) sizeof (Block);
    if (! f.existsAsFile() || f.getSize() < need)
    {
        juce::MemoryBlock zero ((size_t) need, true);
        f.replaceWithData (zero.getData(), zero.getSize());
    }
    mapped = std::make_unique<juce::MemoryMappedFile> (f, juce::Range<juce::int64> (0, need), juce::MemoryMappedFile::readWrite, false);
    if (mapped->getData() == nullptr || mapped->getSize() < (size_t) need) { mapped.reset(); return; }
    mem = static_cast<Block*> (mapped->getData());
    if (mem->magic != kMagic || mem->version != kVersion)
    {
        std::memset (static_cast<void*> (mem), 0, sizeof (Block));
        mem->magic = kMagic;
        mem->version = kVersion;
    }
}

KickBassBridge::~KickBassBridge() = default;

namespace
{
template <typename T>
void seqWrite (std::atomic<juce::uint32>& seq, T& dst, const T& src, double& wall)
{
    const auto s = seq.load (std::memory_order_relaxed);
    seq.store (s | 1u, std::memory_order_release);           // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    std::memcpy (static_cast<void*> (&dst), &src, sizeof (T));
    wall = juce::Time::getMillisecondCounterHiRes();
    std::atomic_thread_fence (std::memory_order_release);
    seq.store ((s | 1u) + 1u, std::memory_order_release);    // even: stable
}

template <typename T>
bool seqRead (const std::atomic<juce::uint32>& seq, const T& src, T& out, const double& wall, double maxAge)
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto a = seq.load (std::memory_order_acquire);
        if (a & 1u) continue;
        std::memcpy (static_cast<void*> (&out), &src, sizeof (T));
        const double w = wall;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_acquire) == a)
            return a != 0 && juce::Time::getMillisecondCounterHiRes() - w < maxAge;
    }
    return false;
}
}

void KickBassBridge::publishKick (const BridgeKickSchedule& s)
{
    if (mem == nullptr) return;
    BridgeKickSchedule copy = s;
    copy.liveCount = mem->kick.liveCount;
    copy.liveWallMs = mem->kick.liveWallMs;
    copy.liveVel = mem->kick.liveVel;
    seqWrite (mem->kickSeq, mem->kick, copy, mem->kickWallMs);
}

void KickBassBridge::publishLiveKick (float velocity)
{
    if (mem == nullptr) return;
    BridgeKickSchedule copy = mem->kick;
    copy.liveCount += 1;
    copy.liveWallMs = juce::Time::getMillisecondCounterHiRes();
    copy.liveVel = velocity;
    seqWrite (mem->kickSeq, mem->kick, copy, mem->kickWallMs);
}

bool KickBassBridge::readKick (BridgeKickSchedule& out, double maxAgeMs) const
{
    return mem != nullptr && seqRead (mem->kickSeq, mem->kick, out, mem->kickWallMs, maxAgeMs);
}

void KickBassBridge::publishBass (const BridgeBassState& s)
{
    if (mem == nullptr) return;
    seqWrite (mem->bassSeq, mem->bass, s, mem->bassWallMs);
}

bool KickBassBridge::readBass (BridgeBassState& out, double maxAgeMs) const
{
    return mem != nullptr && seqRead (mem->bassSeq, mem->bass, out, mem->bassWallMs, maxAgeMs);
}
} // namespace be

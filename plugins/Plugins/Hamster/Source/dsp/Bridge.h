#pragma once
#include <JuceHeader.h>
#include <atomic>

// Kick-Bass Bridge: a tiny shared memory block that Drum Engine and Bass Engine instances
// use to exchange the kick schedule (drum -> bass) and the bass pitch/energy (bass -> drum).
// Shared by every instance on the machine through a memory-mapped file, so it works across
// plugin binaries and host processes. All fields are guarded by seqlocks.
namespace be
{
struct BridgeKickSchedule
{
    double loopBeats = 4.0;          // kick track loop length in beats (absolute host timeline)
    double anchorBeat = 0.0;         // host beat where the loop is aligned
    int numHits = 0;
    float hitBeat[128] {};           // positions inside the loop (swing / micro timing applied)
    float hitVel[128] {};
    float kickDecay = 0.3f;          // seconds, approximate audible length
    float kickHz = 50.0f;            // kick fundamental
    float bpm = 120.0f;
    int playing = 0;
    juce::uint32 liveCount = 0;      // incremented for every live (non-sequenced) kick hit
    double liveWallMs = 0.0;
    float liveVel = 0.0f;
};

struct BridgeBassState
{
    float bassHz = 0.0f;             // current bass fundamental (0 = silent)
    float bassEnv = 0.0f;            // low band level 0..1
    int active = 0;
};

class KickBassBridge
{
public:
    KickBassBridge();
    ~KickBassBridge();
    bool isOpen() const { return mem != nullptr; }

    // drum side
    void publishKick (const BridgeKickSchedule& s);
    void publishLiveKick (float velocity);
    bool readBass (BridgeBassState& out, double maxAgeMs = 500.0) const;

    // bass side
    void publishBass (const BridgeBassState& s);
    bool readKick (BridgeKickSchedule& out, double maxAgeMs = 1500.0) const;

    static juce::File sharedFile();

private:
    struct Block;
    std::unique_ptr<juce::MemoryMappedFile> mapped;
    Block* mem = nullptr;
};
} // namespace be

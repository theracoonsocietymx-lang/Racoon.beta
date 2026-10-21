#pragma once
#include "Session.h"

// Crear la rola completa: a selection (a loop of a few bars on several channels) becomes a whole song.
// The shape is the one of "El arte de contar historias" (module 04): intro, build-up, drop, breakdown, second
// build-up, second drop and outro, every section a whole number of 8-bar phrases, every change on the one of a
// phrase. What plays in each part follows the usual practice of house and techno: DJ intros and outros with kick
// and percussion only, the bass enters in the build-up, the breakdown loses the kick, the bass and the lows,
// the build-ups end with a snare roll, a riser, a high-pass sweep and one beat of silence, the drops start with a
// crash and an impact.
namespace racoon::arranger
{
enum class Part { intro, build, drop, breakdown, build2, drop2, outro };
struct Section { Part part; juce::String name; int startBar = 0, bars = 0; };

constexpr int phraseBars = 8;
int minBars();                                      // 96: every section at its shortest
int maxBars();                                      // 320
int barsForMinutes (double minutes, double bpm);    // whole phrases, between minBars and maxBars
double minutesForBars (int bars, double bpm);
juce::String clock (double seconds);                // "3:06"
std::vector<Section> plan (int totalBars);
juce::String partName (Part);

// what each channel is in the song
// the kit is split by sound: each kind of percussion has its own moment (a shaker is not a conga, a ride is not a hat)
enum class Role { kick, snare, clap, hats, openHat, ride, rim, shaker, conga, toms, bell, perc, incidental, crash,
                  bass, pad, chords, lead, choir, texture, voice, other };
juce::String roleName (Role);
// the arrangement table: does this role sound in phrase "phrase" (of "phrases") of this part
bool activeIn (Role, Part, int phrase, int phrases);

struct Options
{
    int totalBars = 224;
    bool transitions = true;    // riser, impact and downlifter on their own channel
    bool sweep = true;          // the high-pass on the master: lows out in the breakdown, the sweep in the build-ups
};

struct Result
{
    bool ok = false;
    juce::String message;
    std::vector<Section> sections;
    double startBeat = 0.0;
    int loopBars = 0;
    int harmonicBars = 0;       // how often the harmony repeats inside the loop
    juce::StringArray found;    // "Hamster · Batería: bombo, caja, hats"
    te::AudioTrack* fxTrack = nullptr;
    te::Plugin* sweepPlugin = nullptr;
};

// what the song maker will use from these channels (for the dialog, before building)
juce::StringArray describe (Session&, const juce::Array<te::AudioTrack*>&, double startBeat, double endBeat);

// writes the song on the selected channels from the selection's first bar on, in one undo step
Result build (Session&, const juce::Array<te::AudioTrack*>& tracks, double startBeat, double endBeat, const Options&);

// the channel and the plugin the song maker adds (found again when it runs a second time)
extern const juce::Identifier fxTrackProp, sweepProp;
}

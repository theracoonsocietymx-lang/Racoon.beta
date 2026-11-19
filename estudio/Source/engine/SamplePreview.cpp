#include "SamplePreview.h"

namespace racoon
{
using namespace juce;

namespace { std::unique_ptr<SamplePreview>& theOne() { static std::unique_ptr<SamplePreview> one; return one; } }

SamplePreview& SamplePreview::get (te::Engine& e)
{
    auto& one = theOne();
    if (one == nullptr) one.reset (new SamplePreview (e));
    return *one;
}

// before the engine goes: the preview lets go of the audio device while there still is one
void SamplePreview::shutdown() { theOne().reset(); }

SamplePreview::SamplePreview (te::Engine& e) : engine (e)
{
    formats.registerBasicFormats();
    reader.startThread (Thread::Priority::normal);
    player.setSource (&transport);
    player.setGain (Decibels::decibelsToGain (-4.0f));
}

SamplePreview::~SamplePreview()
{
    if (attached) engine.getDeviceManager().deviceManager.removeAudioCallback (&player);
    transport.setSource (nullptr);
    player.setSource (nullptr);
    reader.stopThread (2000);
}

bool SamplePreview::play (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) return false;
    if (! attached)
    {
        engine.getDeviceManager().deviceManager.addAudioCallback (&player);
        attached = true;
    }
    const double rate = r->sampleRate;
    auto next = std::make_unique<AudioFormatReaderSource> (r.release(), true);
    transport.stop();
    transport.setSource (next.get(), 32768, &reader, rate, 2);   // swapped under the callback lock; the old one goes after
    source = std::move (next);
    transport.setPosition (0.0);
    transport.start();
    current = f;
    return true;
}

void SamplePreview::stop()
{
    transport.stop();
    current = File();
}

bool SamplePreview::isPlaying() const { return transport.isPlaying(); }
}

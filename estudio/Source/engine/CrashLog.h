#pragma once
#include <juce_core/juce_core.h>

// What Racoon leaves behind when it closes on its own: a report with the stack and the last things that happened.
// The last 5 reports are kept in Application Support/Racoon/Fallos (the idea comes from SoundCraft's logging.rs).
namespace racoon::crashlog
{
juce::File folder();
// installs the handler and marks this run as started; the next start knows if this one ended badly
void install();
// a clean quit: the mark goes away
void cleanExit();
// one line of what is going on (opening a project, loading a plugin, exporting); the last 200 go into a report.
// Message thread or any other; never call it from the audio thread.
void note (const juce::String&);
// a report newer than the last one the user was told about (empty if none); marks it as told
juce::File takeNewReport();
// writes a report now (the crash handler does the same); returns it
juce::File writeReport (const juce::String& reason, const juce::String& stack);
// the file names, newest first
juce::Array<juce::File> reports();
}

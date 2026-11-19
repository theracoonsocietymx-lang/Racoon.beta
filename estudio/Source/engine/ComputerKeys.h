#pragma once
#include "Common.h"

// El teclado de la compu como piano: A W S E D F T G Y H U J K O L P Ñ/; tocan una octava y media (Do, Do#, Re...),
// Z baja y X sube de octava. Mira el teclado unas cien veces por segundo (como el teclado en pantalla de JUCE), así
// las notas suenan aunque el foco esté en la maqueta o en el piano roll; quien lo usa decide a dónde van las notas.
namespace racoon
{
class ComputerKeys : private juce::Timer
{
public:
    static constexpr const char* keys = "awsedftgyhujkolp;'";     // semitones 0..17 from the octave's C

    std::function<void (const juce::MidiMessage&)> send;          // a note-on or note-off to play
    std::function<bool()> allowed;                                 // false while typing in a text field, or in another app
    std::function<void()> onChange;                                // on, off or another octave (for the buttons)

    ~ComputerKeys() override { stopTimer(); }

    void setEnabled (bool on)
    {
        if (on == enabled) return;
        enabled = on;
        if (on) startTimer (10); else { stopTimer(); releaseAll(); }
        if (onChange) onChange();
    }
    bool isEnabled() const { return enabled; }

    // octave of the A key: 5 is middle C (C3), as in the piano roll; 0..9 covers every MIDI note
    void setOctave (int o)
    {
        const int v = juce::jlimit (0, 9, o);
        if (v == octave) return;
        octave = v;
        if (onChange) onChange();
    }
    int getOctave() const { return octave; }
    void octaveDown() { setOctave (octave - 1); }
    void octaveUp()   { setOctave (octave + 1); }
    juce::String rootName() const { return juce::MidiMessage::getMidiNoteName (octave * 12, true, true, 3); }
    int velocity = 100;

    static int offsetFor (juce::juce_wchar c)
    {
        c = juce::CharacterFunctions::toLowerCase (c);
        if (c == 0xf1) c = ';';                                      // ñ, where the ; is on a Spanish keyboard
        for (int i = 0; keys[i] != 0; ++i) if ((juce::juce_wchar) keys[i] == c) return i;
        return -1;
    }
    // the plain keys it uses (notes, Z and X): the shortcuts that share them wait while it is on
    static bool uses (const juce::KeyPress& k)
    {
        const auto c = k.getTextCharacter();
        return uses (c != 0 ? c : (juce::juce_wchar) k.getKeyCode());
    }
    static bool uses (juce::juce_wchar c)
    {
        c = juce::CharacterFunctions::toLowerCase (c);
        return offsetFor (c) >= 0 || c == 'z' || c == 'x';
    }

    // a key pressed or let go (the timer calls these; the tests too)
    void keyDown (juce::juce_wchar c)
    {
        c = juce::CharacterFunctions::toLowerCase (c);
        if (c == 'z') { octaveDown(); return; }
        if (c == 'x') { octaveUp(); return; }
        const int off = offsetFor (c);
        if (off < 0 || held[off] >= 0) return;
        const int note = octave * 12 + off;
        if (note > 127) return;
        held[off] = note;
        if (send) send (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, velocity)));
    }
    void keyUp (juce::juce_wchar c)
    {
        const int off = offsetFor (c);
        if (off < 0 || held[off] < 0) return;
        // the note that sounded, even if the octave changed while it was held
        if (send) send (juce::MidiMessage::noteOff (1, held[off]));
        held[off] = -1;
    }
    void releaseAll()
    {
        for (int i = 0; i < numKeys; ++i)
            if (held[i] >= 0) { if (send) send (juce::MidiMessage::noteOff (1, held[i])); held[i] = -1; }
        zDown = xDown = false;
    }

private:
    static constexpr int numKeys = 18;
    bool enabled = false, zDown = false, xDown = false;
    int octave = 5;
    int held[numKeys] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

    void timerCallback() override
    {
        if (allowed && ! allowed()) { releaseAll(); return; }
        for (int i = 0; i < numKeys; ++i)
        {
            const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) keys[i])
                              || (keys[i] == ';' && juce::KeyPress::isKeyCurrentlyDown (0xf1));
            if (down && held[i] < 0) keyDown ((juce::juce_wchar) keys[i]);
            else if (! down && held[i] >= 0) keyUp ((juce::juce_wchar) keys[i]);
        }
        const bool z = juce::KeyPress::isKeyCurrentlyDown ('z'), x = juce::KeyPress::isKeyCurrentlyDown ('x');
        if (z && ! zDown) octaveDown();
        if (x && ! xDown) octaveUp();
        zDown = z; xDown = x;
    }
};
}

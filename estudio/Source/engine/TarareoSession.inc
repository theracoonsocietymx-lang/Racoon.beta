// Included by Session.cpp: Tarareo a rola, the block built around a hummed melody and a beatbox.

namespace
{
juce::String gradoRomano (int deg, bool major, bool harmonic)
{
    static const char* M[7] { "I", "ii", "iii", "IV", "V", "vi", "vii°" };
    static const char* m[7] { "i", "ii°", "III", "iv", "v", "VI", "VII" };
    if (! major && harmonic && deg == 4) return "V";
    return major ? M[((deg % 7) + 7) % 7] : m[((deg % 7) + 7) % 7];
}

// how many bars a phrase of this length fills: 1, 2 or 4
int compasesDe (double lastBeat)
{
    const double bars = lastBeat / 4.0;
    return bars <= 1.05 ? 1 : bars <= 2.1 ? 2 : 4;
}
}

Session::ResultadoTarareo Session::buildFromHum (const Genre& base, const AudioBuffer<float>* melodiaAudio,
                                                const AudioBuffer<float>* beatboxAudio, double sampleRate)
{
    ResultadoTarareo r;
    Genre g = base;
    std::vector<tarareo::Nota> mel;
    std::vector<tarareo::Golpe> hits;
    tarareo::Tono tono { g.key, g.major, 0.0 };

    if (melodiaAudio != nullptr && melodiaAudio->getNumSamples() > 0)
    {
        mel = tarareo::transcribir (*melodiaAudio, sampleRate, g.bpm);
        mel.erase (std::remove_if (mel.begin(), mel.end(), [] (const tarareo::Nota& n) { return n.beat >= 16.0; }), mel.end());
        if (mel.size() >= 3)
        {
            tono = tarareo::detectarTono (mel);
            // the genre's "harmonic" habit only makes sense in minor
            g.key = tono.root;
            g.major = tono.major;
            if (g.major) g.harmonic = false;
            tarareo::ajustarATono (mel, tono, 67);
            const auto prog = tarareo::progresion (mel, tono, g.harmonic);
            for (int i = 0; i < 4; ++i) g.prog[i] = prog[(size_t) i];
        }
        else mel.clear();
    }
    if (beatboxAudio != nullptr && beatboxAudio->getNumSamples() > 0)
    {
        hits = tarareo::beatbox (*beatboxAudio, sampleRate, g.bpm);
        hits.erase (std::remove_if (hits.begin(), hits.end(), [] (const tarareo::Golpe& h) { return h.beat >= 16.0; }), hits.end());
    }

    buildBlockTemplate (g);

    auto* um = &edit->getUndoManager();
    auto bp = [] (double b) { return te::BeatPosition::fromBeats (b); };
    auto bd = [] (double b) { return te::BeatDuration::fromBeats (b); };
    auto trackNamed = [this] (const String& prefix) -> te::AudioTrack*
    {
        for (auto* t : te::getAudioTracks (*edit)) if (t->getName().startsWith (prefix)) return t;
        return nullptr;
    };
    auto clipOf = [] (te::AudioTrack* t) -> te::MidiClip*
    {
        if (t != nullptr) for (auto* c : t->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) return mc;
        return nullptr;
    };
    auto clear = [um] (te::MidiClip* c, std::function<bool (int)> which)
    {
        if (c == nullptr) return;
        Array<te::MidiNote*> out;
        for (auto* n : c->getSequence().getNotes()) if (which (n->getNoteNumber())) out.add (n);
        for (auto* n : out) c->getSequence().removeNote (*n, um);
    };

    // ---- the melody: Squirrel plays it from bar 5 (the break breathes, as in every block), Chinchilla sings it in the last phrase
    if (! mel.empty())
    {
        double last = 0.0;
        for (auto& n : mel) last = jmax (last, n.beat + n.len);
        const int L = compasesDe (last);
        auto lay = [&] (te::MidiClip* c, int fromBar, int shift, int lo, int hi, bool breathe)
        {
            if (c == nullptr) return;
            for (int bar = fromBar; bar < 16; ++bar)
            {
                if (breathe && (bar == 8 || bar == 9)) continue;
                const int src = bar % L;
                for (auto& n : mel)
                {
                    if (n.beat < src * 4.0 || n.beat >= (src + 1) * 4.0) continue;
                    int p = n.pitch + shift;
                    while (p > hi) p -= 12;
                    while (p < lo) p += 12;
                    c->getSequence().addNote (p, bp (bar * 4.0 + n.beat - src * 4.0), bd (jmin (n.len, (src + 1) * 4.0 - n.beat) * 0.95),
                                              jlimit (1, 127, (int) (n.vel * 112.0f)), 0, um);
                }
            }
        };
        if (auto* hook = clipOf (trackNamed (String::fromUTF8 ("Squirrel · "))))
        {
            clear (hook, [] (int) { return true; });
            lay (hook, 4, 0, 50, 86, true);
        }
        if (auto* voice = clipOf (trackNamed (String::fromUTF8 ("Chinchilla · "))))
        {
            // in the voice's range (E3 to A4): the whole melody moved by octaves, so the contour stays
            std::vector<int> ps;
            for (auto& n : mel) ps.push_back (n.pitch);
            std::sort (ps.begin(), ps.end());
            const int mid = ps[ps.size() / 2];
            const int shift = (int) std::lround ((60 - mid) / 12.0) * 12;
            clear (voice, [] (int) { return true; });
            lay (voice, 12, shift, 48, 74, false);
        }
        r.notas = (int) mel.size();
    }

    // ---- the beatbox: Hamster plays it in place of the genre's kick, snare, clap and hats (the rest of the kit stays)
    if (! hits.empty())
    {
        double last = 0.0;
        for (auto& h : hits) last = jmax (last, h.beat + 0.25);
        const int L = compasesDe (last);
        auto* kickT = trackNamed (String::fromUTF8 ("Hamster · Kick"));
        auto* groove = trackNamed (String::fromUTF8 ("Hamster · Groove"));
        auto* gc = clipOf (groove);
        auto* kc = clipOf (kickT);
        if (gc == nullptr && groove != nullptr)
            gc = createMidiClip (*groove, { 0_bp, bp (64.0) }, "Groove");
        if (kc == nullptr && kickT != nullptr)
            kc = createMidiClip (*kickT, { 0_bp, bp (64.0) }, "Kick");
        clear (gc, [] (int n) { return n == 35 || n == 36 || n == 38 || n == 39 || n == 40 || n == 42 || n == 44; });
        clear (kc, [] (int) { return true; });
        const int snareNote = std::strlen (g.snare) > 0 || std::strlen (g.clap) == 0 ? 38 : 39;
        for (int bar = 0; bar < 16; ++bar)
        {
            const int ph = bar / 4;
            const int src = bar % L;
            for (auto& h : hits)
            {
                if (h.beat < src * 4.0 || h.beat >= (src + 1) * 4.0) continue;
                const double at = bar * 4.0 + h.beat - src * 4.0;
                const int v = jlimit (1, 127, (int) (h.vel * 120.0f));
                if (h.tipo == 0)
                {
                    if (ph == 2) continue;                                  // the break drops the kick
                    if (auto* c = kc != nullptr ? kc : gc) c->getSequence().addNote (36, bp (at), bd (0.25), v, 0, um);
                }
                else if (h.tipo == 1)
                {
                    if (bar < 2) continue;                                  // the intro is kick and hats
                    if (bar == 15 && h.beat - src * 4.0 >= 3.0) continue;   // room for the fill
                    if (gc != nullptr) gc->getSequence().addNote (snareNote, bp (at), bd (0.25), v, 0, um);
                }
                else if (gc != nullptr) gc->getSequence().addNote (42, bp (at), bd (0.125), jlimit (1, 127, v - 14), 0, um);
            }
        }
        if (gc != nullptr)                                                  // the fill into the next block
            for (int k = 0; k < 4; ++k) gc->getSequence().addNote (38, bp (63.0 + k * 0.25), bd (0.2), 70 + k * 12, 0, um);
        r.golpes = (int) hits.size();
    }

    r.key = g.key;
    r.major = g.major;
    for (int i = 0; i < 4; ++i) r.prog[i] = g.prog[i];
    r.ok = r.notas > 0 || r.golpes > 0;
    StringArray grados;
    for (int i = 0; i < 4; ++i) grados.add (gradoRomano (g.prog[i], g.major, g.harmonic));
    String t;
    if (r.notas > 0)
    {
        t << i18n::tr ("Tono") << ": " << keyName (g.key, g.major) << " (" << camelot (g.key, g.major) << "). "
          << i18n::tr ("Progresión") << ": " << grados.joinIntoString (String::fromUTF8 (" – ")) << ". "
          << String (r.notas) << " " << i18n::tr ("notas") << ".";
    }
    if (r.golpes > 0)
    {
        int k[3] {};
        for (auto& h : hits) ++k[jlimit (0, 2, h.tipo)];
        if (t.isNotEmpty()) t << " ";
        t << String (k[0]) << " " << i18n::tr ("bombos") << ", " << String (k[1]) << " " << i18n::tr ("cajas") << ", "
          << String (k[2]) << " " << i18n::tr ("charles") << ".";
    }
    r.texto = t;
    edit->state.setProperty ("racoonTarareo", t, nullptr);
    structureChanged();
    return r;
}

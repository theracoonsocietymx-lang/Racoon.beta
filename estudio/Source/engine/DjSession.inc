// Included by Session.cpp: Modo DJ, a whole track built to mix with a song that was measured.

Session::ResultadoDJ Session::buildDjTool (const dj::Analisis& a, int totalBars)
{
    ResultadoDJ r;
    if (! a.ok || a.bpm <= 0.0) { r.texto = a.error; return r; }
    const Genre& base = dj::generoPara (a);
    Genre g = base;
    g.bpm = a.bpm;
    g.key = a.key;
    g.major = a.major;
    if (g.major) g.harmonic = false;
    buildBlockTemplate (g);

    // the block's last phrase (everything playing) becomes the loop the song is made from: its 4 bars go to the start
    // and the rest of the block is cleared (the arranger writes the whole song from there)
    auto* um = &edit->getUndoManager();
    Array<te::AudioTrack*> tracks;
    for (auto* t : te::getAudioTracks (*edit))
    {
        bool any = false;
        for (auto* c : t->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            {
                any = true;
                auto& seq = mc->getSequence();
                const bool drums = t->getName().startsWith ("Hamster") || t->getName().startsWith ("Mole");
                struct N { double b, l; int p, v; };
                std::vector<N> keep;
                for (auto* n : seq.getNotes())
                {
                    const double b = n->getStartBeat().inBeats();
                    if (b < 48.0 - 1e-6) continue;
                    if (drums && b >= 63.0 - 1e-6) continue;           // the block's fill: the song makes its own
                    keep.push_back ({ b - 48.0, n->getLengthBeats().inBeats(), n->getNoteNumber(), n->getVelocity() });
                }
                seq.clear (um);
                for (auto& n : keep) seq.addNote (n.p, te::BeatPosition::fromBeats (n.b), te::BeatDuration::fromBeats (jmin (n.l, 16.0 - n.b)), n.v, 0, um);
                mc->setEnd (edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)), true);
            }
        if (any) tracks.add (t);
    }

    arranger::Options o;
    o.totalBars = jlimit (arranger::minBars(), arranger::maxBars(), totalBars);
    o.soloBateriaEnBordes = true;
    const auto res = arranger::build (*this, tracks, 0.0, 16.0, o);
    if (! res.ok) { r.texto = res.message; return r; }
    int bars = 0, intro = 0, outro = 0;
    for (auto& sec : res.sections)
    {
        bars = jmax (bars, sec.startBar + sec.bars);
        if (sec.part == arranger::Part::intro) intro = sec.bars;
        if (sec.part == arranger::Part::outro) outro = sec.bars;
    }
    r.intro = intro;
    r.outro = outro;
    r.compases = bars;
    r.genero = String::fromUTF8 (base.name);
    r.ok = true;
    getTransport().looping = false;
    String t;
    t << i18n::tr ("Para mezclar con") << " \"" << a.titulo << "\": " << String::fromUTF8 (base.name) << ", " << String (a.bpm, 2) << " BPM, "
      << a.camelot() << " (" << i18n::tr (keyName (a.key, a.major)) << "), " << String (bars) << " " << i18n::tr ("compases") << ". "
      << i18n::tr (String::fromUTF8 ("Entra con ") + String (intro) + String::fromUTF8 (" compases y sale con ") + String (outro)
                   + String::fromUTF8 (" de bombo y percusión, para mezclarla."));
    r.texto = t;
    edit->state.setProperty ("racoonDj", t, nullptr);
    structureChanged();
    return r;
}

// MEZCLA AUTOMÁTICA: what an engineer does on every channel of a song, done by Racoon. Included by Session.cpp.
//
// Every channel gets a chain for its part (kick, the rest of the drums, bass, pad, chords, melody, guitar, choir,
// percussion, texture, voice, the reverb and delay returns):
//   a cut of the lows that do not belong to it (only the kick and the bass keep the bottom), a dip in the mud
//   (250-450 Hz), a lift in the presence (2-6 kHz, where a snare cracks and a guitar speaks), a little saturation or
//   excitation where a part needs to be heard on a phone, the bass ducking under the kick (sidechain), the pads and
//   choirs open to the sides while everything under 120 Hz stays in the centre.
// The master gets Hedgehog set for the genre: its target (club or streaming), its tonal curve, the lows in mono,
// a light exciter and stereo image, a gentle multiband and the limiter with a -1 dBTP ceiling.
// Then mixAndMaster() renders the song, measures it (loudness, the five bands, the stereo, the crest) and corrects:
// the limiter to reach the loudness, more presence where it is missing, more width where it is too narrow.
// The chains are marked (racoonMix) so a second pass replaces them instead of piling them up.

namespace mezcla
{
enum class Papel { kick, drums, bass, pad, chords, melody, guitar, choir, perc, texture, voice, reverb, delay, none };

struct Ajuste { const char* fx; std::vector<std::pair<const char*, float>> params; };

// the chain of each part; `g` changes a few values by group (an 808 needs more grit, lo-fi less shine)
std::vector<Ajuste> cadena (Papel p, variedad::Grupo g)
{
    using G = variedad::Grupo;
    const bool chill = g == G::chill || g == G::hiphop;
    const bool sub808 = g == G::trap;
    switch (p)
    {
        case Papel::kick:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 28 }, { "b1freq", 320 }, { "b1gain", -3.0f }, { "b1q", 1.2f },
                                      { "b3freq", 3500 }, { "b3gain", 2.5f }, { "b3q", 1.0f } } },
                     { "transitorios", { { "attack", 25 }, { "sustain", 0 }, { "split", 20 }, { "mix", 100 } } } };
        case Papel::drums:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 110 }, { "b1freq", 420 }, { "b1gain", -2.0f }, { "b1q", 1.0f },
                                      { "b3freq", 4000 }, { "b3gain", chill ? 1.5f : 3.0f }, { "b3q", 0.8f },
                                      { "hsfreq", 10000 }, { "hsgain", chill ? 0.0f : 2.5f }, { "lpon", chill ? 1.0f : 0.0f }, { "lpfreq", 12000 } } },
                     { "bus-bateria", { { "drive", chill ? 30.0f : 22.0f }, { "type", 1 }, { "trans", 15 }, { "boom", 0 }, { "mix", 100 } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 105 }, { "whigh", 120 } } } };
        case Papel::bass:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", sub808 ? 25.0f : 32.0f }, { "b1freq", 250 }, { "b1gain", -2.5f }, { "b1q", 1.0f },
                                      { "b2freq", 900 }, { "b2gain", 2.0f }, { "b2q", 1.2f } } },
                     { "valvula", { { "tube", 1 }, { "drive", sub808 ? 9.0f : 6.0f }, { "mix", sub808 ? 45.0f : 35.0f }, { "output", -2.0f } } },
                     { "sidechain", { { "mode", 0 }, { "depth", sub808 ? -6.0f : -9.0f }, { "attack", 1 }, { "hold", 20 }, { "release", 140 }, { "mix", 100 } } },
                     { "utilidad", { { "mono", 1 } } } };
        case Papel::pad:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 220 }, { "b1freq", 450 }, { "b1gain", -2.5f }, { "b1q", 1.0f },
                                      { "hsfreq", 8000 }, { "hsgain", 1.5f } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 125 }, { "whigh", 140 } } },
                     { "sidechain", { { "mode", 0 }, { "depth", -4.0f }, { "attack", 2 }, { "release", 180 }, { "mix", 100 } } } };
        case Papel::chords:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 180 }, { "b1freq", 380 }, { "b1gain", -2.0f }, { "b1q", 1.0f },
                                      { "b3freq", 3000 }, { "b3gain", 2.5f }, { "b3q", 0.9f } } },
                     { "excitador", { { "freq", 3000 }, { "amount", chill ? 15.0f : 30.0f }, { "mix", 35 } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 115 }, { "whigh", 125 } } } };
        case Papel::melody:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 200 }, { "b1freq", 400 }, { "b1gain", -1.5f }, { "b1q", 1.0f },
                                      { "b3freq", 3500 }, { "b3gain", 2.0f }, { "b3q", 0.9f }, { "hsfreq", 10000 }, { "hsgain", 1.5f } } },
                     { "excitador", { { "freq", 3500 }, { "amount", 20 }, { "mix", 25 } } } };
        case Papel::guitar:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 150 }, { "b1freq", 350 }, { "b1gain", -2.5f }, { "b1q", 1.2f },
                                      { "b3freq", 3000 }, { "b3gain", 3.0f }, { "b3q", 0.9f }, { "hsfreq", 9000 }, { "hsgain", 1.0f } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 130 }, { "whigh", 140 } } } };
        case Papel::choir:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 220 }, { "b1freq", 400 }, { "b1gain", -2.0f }, { "b1q", 1.0f },
                                      { "hsfreq", 9000 }, { "hsgain", 2.5f } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 130 }, { "whigh", 145 } } } };
        case Papel::perc:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 300 }, { "b3freq", 5000 }, { "b3gain", 2.0f }, { "b3q", 0.8f },
                                      { "hsfreq", 10000 }, { "hsgain", 2.0f } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 115 }, { "whigh", 130 } } } };
        case Papel::texture:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 250 }, { "lpon", 1 }, { "lpfreq", 9000 } } },
                     { "imagen-estereo", { { "wlow", 0 }, { "wmid", 140 }, { "whigh", 150 } } } };
        case Papel::voice:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 120 }, { "b1freq", 300 }, { "b1gain", -2.0f }, { "b1q", 1.0f },
                                      { "b3freq", 3200 }, { "b3gain", 2.5f }, { "b3q", 0.9f }, { "hsfreq", 11000 }, { "hsgain", 2.0f } } },
                     // the voice is the loudest part and Chinchilla sings warm: without harmonics above 2.5 kHz it gets
                     // lost on a phone (measured: 0.1 % of its energy in 2-6 kHz). The exciter starts at 1 kHz: it makes
                    // its harmonics from what is above that, and above 2.5 kHz there was nothing to start from
                     { "excitador", { { "freq", 1000 }, { "amount", chill ? 20.0f : 35.0f }, { "mix", 35 } } } };
        case Papel::reverb:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 280 }, { "lpon", 1 }, { "lpfreq", 9000 } } } };
        case Papel::delay:
            return { { "eq-mezcla", { { "hpon", 1 }, { "hpfreq", 250 }, { "lpon", 1 }, { "lpfreq", 7000 } } } };
        case Papel::none: break;
    }
    return {};
}

// where each part sits left and right (the drums, the bass and the voice in the centre)
float paneo (Papel p)
{
    switch (p)
    {
        case Papel::guitar: return -0.25f;
        case Papel::chords: return 0.2f;
        case Papel::perc:   return 0.3f;
        case Papel::melody: return -0.08f;
        default: return 0.0f;
    }
}

// the master's settings for the group: its destination (Hedgehog's list), its tonal curve, how much exciter
// the starting limiter and input gains come from measuring the blocks of every group (the gain the measurement
// settled on), so a new block is near its loudness before anything is measured
struct Master { const char* destino; const char* genero; float exMix; float limGain; float inGain; };
Master master (variedad::Grupo g)
{
    using G = variedad::Grupo;
    switch (g)
    {
        case G::trap:   return { "CD fuerte (-10)", "Hip-hop", 16.0f, 2.0f, 0.0f };
        case G::latino: return { "CD fuerte (-10)", "Hip-hop", 16.0f, 4.0f, 0.0f };
        case G::hiphop: return { "Spotify / YouTube (-14)", "Hip-hop", 12.0f, 0.0f, -3.0f };
        case G::chill:  return { "Spotify / YouTube (-14)", "Acústico", 0.0f, 0.0f, -3.0f };
        case G::disco:  return { "CD fuerte (-10)", "Pop", 16.0f, 2.5f, 0.0f };
        case G::afro:   return { "CD fuerte (-10)", "Electrónica", 18.0f, 5.0f, 0.0f };
        case G::house:  return { "CD fuerte (-10)", "Electrónica", 18.0f, 7.0f, 0.0f };
        default:        return { "CD fuerte (-10)", "Electrónica", 18.0f, 2.0f, 0.0f };
    }
}
float destinoLufs (const String& destino) { return destino.contains ("-10") ? -10.0f : destino.contains ("-8") ? -8.0f : destino.contains ("-16") ? -16.0f : -14.0f; }
}

namespace
{
// the part a channel plays, from its Racoon instrument (and its name for the kick and the returns)
mezcla::Papel papelDe (Session& s, te::AudioTrack& t)
{
    using P = mezcla::Papel;
    const auto name = t.getName().toLowerCase();
    if (name == "reverb") return P::reverb;
    if (name == "delay") return P::delay;
    String animal;
    if (auto* p = s.familyPluginOf (t)) animal = Catalog::animalFor (p->getName());
    if (animal == "Hamster") return name.contains ("kick") || name.contains ("bombo") ? P::kick : P::drums;
    if (animal == "Badger") return P::bass;
    if (animal == "Dormouse") return P::pad;
    if (animal == "Chipmunk") return P::chords;
    if (animal == "Squirrel") return P::melody;
    if (animal == "Ferret") return P::guitar;
    if (animal == "Porcupine") return P::choir;
    if (animal == "Mole") return P::perc;
    if (animal == "Skunk") return P::texture;
    if (animal == "Chinchilla") return P::voice;
    return P::none;
}

RacoonProcessor* nativeOf (te::Plugin* p)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p)) return dynamic_cast<RacoonProcessor*> (ext->getAudioPluginInstance());
    return nullptr;
}

// a family plugin's parameter by its name: a value in the parameter's own units, or a switch on or off
void setByName (juce::AudioPluginInstance& pi, const String& name, float real)
{
    if (auto* prm = paramNamed (pi, name)) prm->setValueNotifyingHost (prm->getValueForText (String (real)));
}
void setSwitch (juce::AudioPluginInstance& pi, const String& name, bool on)
{
    if (auto* prm = paramNamed (pi, name)) prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
}
}

int Session::autoMix (bool masterToo)
{
    const auto genreId = edit->state.getProperty ("racoonGenre").toString();
    const auto grupo = variedad::grupoDe (genreId, getBpm());
    int channels = 0;
    for (auto* t : te::getAudioTracks (*edit))
    {
        const auto papel = papelDe (*this, *t);
        if (papel == mezcla::Papel::none) continue;
        // the chain of a former pass goes
        Array<te::Plugin*> old;
        for (auto* p : t->pluginList) if (p->state.getProperty ("racoonMix", false)) old.add (p);
        for (auto* p : old) p->deleteFromParent();
        // the reverb and delay returns get their cut before their effect, the parts after their instrument
        int index = -1;
        if (papel == mezcla::Papel::reverb || papel == mezcla::Papel::delay) index = 0;
        for (auto& a : mezcla::cadena (papel, grupo))
        {
            if (SystemStats::getEnvironmentVariable ("RACOON_MEZCLA_QUITAR", {}).contains (a.fx)) continue;   // to find out who does what
            CatalogItem item;
            for (auto& it : Catalog::nativeEffects()) if (it.builtInType == a.fx) { item = it; break; }
            if (item.builtInType.isEmpty()) continue;
            auto p = insertItem (*t, item, index);
            if (index >= 0) ++index;
            if (p == nullptr) continue;
            p->state.setProperty ("racoonMix", true, nullptr);
            if (auto* rp = nativeOf (p.get()))
                for (auto& [id, v] : a.params) rp->setRealValue (id, v);
            // the bass and the pad duck under the kick
            if (String (a.fx) == "sidechain")
                if (auto* kick = guessKickTrack (*t)) setSidechain (*p, kick);
        }
        // the width of the whole part, last in its chain: if a sound comes very wide (a doubled voice, a detuned
        // pad), the measurement can close it from here, whatever made it wide
        if (papel != mezcla::Papel::kick && papel != mezcla::Papel::bass && papel != mezcla::Papel::reverb && papel != mezcla::Papel::delay)
        {
            CatalogItem item;
            for (auto& it : Catalog::nativeEffects()) if (it.builtInType == "utilidad") { item = it; break; }
            if (item.builtInType.isNotEmpty())
                if (auto p = insertItem (*t, item, -1))
                {
                    p->state.setProperty ("racoonMix", true, nullptr);
                    p->state.setProperty ("racoonMixWidth", true, nullptr);
                    if (auto* rp = nativeOf (p.get())) rp->setRealValue ("width", 100.0f);
                }
        }
        if (auto* vp = t->getVolumePlugin()) if (mezcla::paneo (papel) != 0.0f) vp->setPan (mezcla::paneo (papel));
        ++channels;
    }
    // the master: a bus compressor that glues the parts (2:1, slow attack, its detector deaf to the sub), then Hedgehog
    if (masterToo)
    {
        auto& list = edit->getMasterPluginList();
        Array<te::Plugin*> old;
        for (int i = 0; i < list.size(); ++i) if (list[i]->state.getProperty ("racoonMix", false)) old.add (list[i]);
        for (auto* p : old) p->deleteFromParent();
        CatalogItem item;
        for (auto& it : Catalog::nativeEffects()) if (it.builtInType == "compresor-bus") { item = it; break; }
        if (item.builtInType.isNotEmpty())
            if (auto p = Catalog::create (*edit, item))
            {
                list.insertPlugin (p, 0, nullptr);
                p->state.setProperty ("racoonMix", true, nullptr);
                if (auto* rp = nativeOf (p.get()))
                {
                    rp->setRealValue ("thresh", -18.0f); rp->setRealValue ("ratio", 0); rp->setRealValue ("attack", 5);
                    rp->setRealValue ("release", 6); rp->setRealValue ("makeup", 2.0f); rp->setRealValue ("schp", 2); rp->setRealValue ("mix", 100.0f);
                }
            }
    }
    if (masterToo)
        for (auto* p : edit->getMasterPluginList())
            if (Catalog::isRacoonPlugin (*p) && Catalog::animalFor (p->getName()) == "Hedgehog")
                if (auto* pi = instanceOf (p))
                {
                    const auto m = mezcla::master (grupo);
                    setParamText (*pi, "Destino", String::fromUTF8 (m.destino));
                    setParamText (*pi, "Género"_u, String::fromUTF8 (m.genero));
                    setSwitch (*pi, "Graves", true); setByName (*pi, "Graves en mono hasta", 120.0f);
                    setSwitch (*pi, "Excitador", m.exMix > 0.0f);
                    setByName (*pi, "Excitador: desde", 4000.0f); setByName (*pi, "Excitador: drive", 30.0f); setByName (*pi, "Excitador: mezcla", m.exMix);
                    setSwitch (*pi, "Imagen estéreo"_u, true); setByName (*pi, "Ancho medios", 110.0f); setByName (*pi, "Ancho agudos", 125.0f);
                    setSwitch (*pi, "Limitador", true); setByName (*pi, "Limitador: ganancia", m.limGain); setByName (*pi, "Techo (dBTP)", -1.3f);   // the limiter reads samples: 0.3 dB of margin for the true peak
                    setByName (*pi, "Entrada", m.inGain);
                    edit->state.setProperty ("racoonMasterTarget", mezcla::destinoLufs (String::fromUTF8 (m.destino)), nullptr);
                    // the limiter sets the final level: the master fader stays at 0 dB after it (at -3 dB every song
                    // came out 3 dB under its ceiling, and the limiter was pushed harder to make up for it)
                    if (auto mv = edit->getMasterVolumePlugin()) mv->setVolumeDb (0.0f);
                }
    edit->state.setProperty ("racoonMixed", true, nullptr);
    structureChanged();
    return channels;
}

StringArray Session::mixAndMaster (int passes, std::function<void (const String&)> progress)
{
    StringArray report;
    if (! edit->state.getProperty ("racoonMixed", false)) report.add ("Cadenas de mezcla en " + String (autoMix (true)) + " canales.");
    const auto genreId = edit->state.getProperty ("racoonGenre").toString();
    const auto grupo = variedad::grupoDe (genreId, getBpm());
    const float target = (float) (double) edit->state.getProperty ("racoonMasterTarget", -14.0);
    juce::AudioPluginInstance* hedgehog = nullptr;
    for (auto* p : edit->getMasterPluginList())
        if (Catalog::isRacoonPlugin (*p) && Catalog::animalFor (p->getName()) == "Hedgehog") hedgehog = instanceOf (p);
    auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("racoon-mezcla-" + String (Random::getSystemRandom().nextInt()) + ".wav");
    // passes + 1 measurements: the last one only reads what the corrections left (what the person will hear)
    for (int pass = 0; pass <= passes; ++pass)
    {
        if (progress) progress ("Midiendo la mezcla (" + String (pass + 1) + " de " + String (passes + 1) + ")...");
        // the loop (the block) or the whole song: the loudness is what the whole song measures
        ExportOptions o;
        o.destination = file;
        o.sampleRate = 48000.0;
        o.tailSeconds = 0.5;
        o.loopOnly = true;
        const auto loopWas = getTransport().getLoopRange();
        {
            auto range = loopWas.getLength().inSeconds() > 1.0 ? loopWas : te::TimeRange (te::TimePosition(), edit->getLength());

            getTransport().setLoopRange (range);
        }
        const bool rendered = exportMix (o).wasOk();
        getTransport().setLoopRange (loopWas);
        if (! rendered) break;
        AudioBuffer<float> buf;
        double sr = 48000.0;
        if (! audio::readFile (engine, file, buf, sr)) break;
        const auto L = audio::measureLoudness (buf, sr);
        const auto B = audio::measureBalance (buf, sr);
        edit->state.setProperty ("racoonMixReport", L.summary() + "; " + B.summary(), nullptr);
        report.add ((pass == passes ? String ("Final: ") : "Pasada " + String (pass + 1) + ": ") + L.summary() + "; " + B.summary());
        {
            // the numbers the Editar menu shows (before = the first measurement, after = the last)
            const auto nums = String (L.integratedLufs, 1) + "|" + String (L.truePeakDb, 1) + "|" + String (B.bands[3], 1) + "|" + String (B.correlation, 2);
            if (pass == 0) edit->state.setProperty ("racoonMixBefore", nums, nullptr);
            edit->state.setProperty ("racoonMixAfter", nums, nullptr);
        }
        if (pass == passes) break;
        bool changed = false;
        // loudness: the limiter brings it to the destination (a dB at a time is enough: the limiter is the last stage)
        if (hedgehog != nullptr && std::abs (L.integratedLufs - target) > 0.8f)
            if (auto* prm = paramNamed (*hedgehog, "Limitador: ganancia"))
            {
                const float now = prm->getCurrentValueAsText().getFloatValue();
                const float wanted = now + jlimit (-6.0f, 6.0f, target - L.integratedLufs);
                const float want = jlimit (0.0f, 14.0f, wanted);
                if (std::abs (want - now) > 0.2f) { setByName (*hedgehog, "Limitador: ganancia", want); changed = true; }
                // still too loud with the limiter at 0: the input of the master comes down (as Hedgehog's assistant does)
                if (wanted < 0.0f)
                    if (auto* in = paramNamed (*hedgehog, "Entrada"))
                    {
                        setByName (*hedgehog, "Entrada", jlimit (-24.0f, 24.0f, in->getCurrentValueAsText().getFloatValue() + wanted));
                        changed = true;
                    }
            }
        // true peak too close to 0 (the limiter catches samples, not the peaks between them): the ceiling comes down
        if (hedgehog != nullptr && L.truePeakDb > -0.8f)
            if (auto* c = paramNamed (*hedgehog, "Techo (dBTP)"))
            {
                setByName (*hedgehog, "Techo (dBTP)", jmax (-3.0f, c->getCurrentValueAsText().getFloatValue() - (L.truePeakDb + 1.0f)));
                changed = true;
            }
        // presence (2-6 kHz): where it is missing, more lift and more excitation on the parts that carry it
        const float presenceWanted = grupo == variedad::Grupo::chill ? 4.0f : 6.0f;
        // the step grows with what is missing: at 0.4 % (a trap with an 808 on top of everything) one pass lifts 4.5 dB
        const float missing = jlimit (0.0f, 1.0f, (presenceWanted - B.bands[3]) / presenceWanted);
        if (B.bands[3] < presenceWanted)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                // the bass takes part too: an 808 that only has sub disappears on a phone, its harmonics are what is heard
                if (papel == mezcla::Papel::bass)
                {
                    for (auto* p : t->pluginList)
                        if (p->state.getProperty ("racoonMix", false))
                            if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("valvula"))
                            {
                                rp->setRealValue ("drive", jmin (18.0f, rp->value ("drive") + 4.0f * missing));
                                rp->setRealValue ("mix", jmin (70.0f, rp->value ("mix") + 15.0f * missing));
                                changed = true;
                            }
                    continue;
                }
                if (papel != mezcla::Papel::drums && papel != mezcla::Papel::chords && papel != mezcla::Papel::guitar && papel != mezcla::Papel::melody
                    && papel != mezcla::Papel::kick && papel != mezcla::Papel::perc && papel != mezcla::Papel::voice && papel != mezcla::Papel::choir) continue;
                bool excited = false;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p))
                        {
                            if (rp->effectId.endsWith ("eq-mezcla")) { rp->setRealValue ("b3gain", jmin (10.0f, rp->value ("b3gain") + 1.5f + 3.0f * missing)); changed = true; }
                            if (rp->effectId.endsWith ("excitador")) { rp->setRealValue ("mix", jmin (75.0f, rp->value ("mix") + 10.0f + 20.0f * missing)); changed = true; excited = true; }
                        }
                // very dark (less than half of what it needs) and the drums have no exciter: one goes in after the EQ
                if (! excited && missing > 0.5f && (papel == mezcla::Papel::drums || papel == mezcla::Papel::choir))
                    for (auto& item : Catalog::nativeEffects())
                        if (item.builtInType == "excitador")
                        {
                            int at = 0;
                            for (int k = 0; k < t->pluginList.size(); ++k)
                                if (auto* rp = nativeOf (t->pluginList[k]); rp != nullptr && rp->effectId.endsWith ("eq-mezcla")) at = k + 1;
                            if (auto np = insertItem (*t, item, at))
                            {
                                np->state.setProperty ("racoonMix", true, nullptr);
                                if (auto* rp = nativeOf (np.get())) { rp->setRealValue ("freq", 2500.0f); rp->setRealValue ("amount", 35.0f); rp->setRealValue ("mix", 40.0f); }
                                changed = true;
                            }
                            break;
                        }
            }
        // too much 60-250 Hz (over 40 %): mud; the low mids of every part except the kick come down and the parts
        // that are not bass cut a little higher
        if (B.bands[1] > 40.0f)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                if (papel == mezcla::Papel::kick || papel == mezcla::Papel::reverb || papel == mezcla::Papel::delay) continue;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("eq-mezcla"))
                        {
                            rp->setRealValue ("b1gain", jmax (-8.0f, rp->value ("b1gain") - 1.5f));
                            if (papel != mezcla::Papel::bass && rp->value ("hpon") > 0.5f)
                                rp->setRealValue ("hpfreq", jmin (400.0f, rp->value ("hpfreq") * 1.2f));
                            changed = true;
                        }
            }
        // very dark (a trap where the 808 takes everything): the master's exciter starts at 1.5 kHz and mixes more,
        // the harmonics of the whole song fill 2 to 6 kHz
        if (hedgehog != nullptr && missing > 0.3f)
            if (auto* mix = paramNamed (*hedgehog, "Excitador: mezcla"))
            {
                setSwitch (*hedgehog, "Excitador", true);
                setByName (*hedgehog, "Excitador: desde", 1500.0f);
                setByName (*hedgehog, "Excitador: mezcla", jmin (50.0f, mix->getCurrentValueAsText().getFloatValue() + 20.0f * missing));
                changed = true;
            }
        // too much presence (over 25 %): it hurts; the same parts come down
        if (B.bands[3] > 25.0f)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                if (papel != mezcla::Papel::drums && papel != mezcla::Papel::chords && papel != mezcla::Papel::guitar && papel != mezcla::Papel::melody) continue;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("eq-mezcla"))
                        { rp->setRealValue ("b3gain", jmax (-6.0f, rp->value ("b3gain") - 2.0f)); changed = true; }
            }
        // sub (20-60 Hz): a club song without it has no floor; the kick's low shelf (and the bass's) brings it up
        const bool club = grupo != variedad::Grupo::chill && grupo != variedad::Grupo::hiphop;
        if (club && B.bands[0] < 8.0f)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                if (papel != mezcla::Papel::kick && papel != mezcla::Papel::bass) continue;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("eq-mezcla") && rp->value ("lsgain") < 7.0f)
                        {
                            rp->setRealValue ("lsfreq", 60.0f);
                            rp->setRealValue ("lsgain", jmin (7.0f, rp->value ("lsgain") + 2.5f));
                            changed = true;
                        }
            }
        // too much sub outside the club (ambient, lo-fi, hip-hop with more than 35 % below 60 Hz): the bass and the kick
        // lose their lowest shelf, 2 dB a pass; there half the energy under 60 Hz is boom, not floor
        if (! club && B.bands[0] > 35.0f)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                if (papel != mezcla::Papel::kick && papel != mezcla::Papel::bass) continue;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("eq-mezcla") && rp->value ("lsgain") > -8.0f)
                        {
                            rp->setRealValue ("lsfreq", 80.0f);
                            rp->setRealValue ("lsgain", jmax (-8.0f, rp->value ("lsgain") - 2.0f));
                            changed = true;
                        }
            }
        // width: a mix almost in mono opens the parts that may be wide
        if (B.correlation > 0.88f)
            for (auto* t : te::getAudioTracks (*edit))
            {
                const auto papel = papelDe (*this, *t);
                // the drum bus too: the Kits Racoon hits come out in mono, and its hats and percussion are what can open
                if (papel != mezcla::Papel::pad && papel != mezcla::Papel::choir && papel != mezcla::Papel::chords && papel != mezcla::Papel::guitar
                    && papel != mezcla::Papel::texture && papel != mezcla::Papel::perc && papel != mezcla::Papel::drums) continue;
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMix", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->effectId.endsWith ("imagen-estereo"))
                        {
                            rp->setRealValue ("wmid", jmin (190.0f, rp->value ("wmid") + 20.0f));
                            rp->setRealValue ("whigh", jmin (200.0f, rp->value ("whigh") + 20.0f));
                            changed = true;
                        }
            }
        // too wide (less than 0.55): every part closes a quarter, so the mix holds in mono
        if (B.correlation < 0.55f)
            for (auto* t : te::getAudioTracks (*edit))
                for (auto* p : t->pluginList)
                    if (p->state.getProperty ("racoonMixWidth", false))
                        if (auto* rp = nativeOf (p); rp != nullptr && rp->value ("width") > 40.0f)
                        {
                            rp->setRealValue ("width", jmax (40.0f, rp->value ("width") - 25.0f));
                            changed = true;
                        }
        if (! changed) break;   // nothing to correct: this measurement is already the final one
    }
    file.deleteFile();
    return report;
}

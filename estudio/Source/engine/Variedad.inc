// VARIAR SONIDOS: each Racoon instrument of the song takes another sound that fits the genre. Included by Session.cpp.
//
// Every genre belongs to a group (house, afro, disco, techno, breaks, trap, latino, hip hop, chill) and every group has
// its pools: Hamster kits (Muestras Racoon and synthesized), Badger basses, Ferret guitars (preset, guitar, amp and
// cabinet), Dormouse pads, Chipmunk sounds, Squirrel melodies (an instrument of the library or one of its own),
// Porcupine choirs, Mole kits, Skunk materials and Chinchilla voices. A seed picks one of each, so the same seed
// gives the same sounds and a new seed another song.
// Before a preset is loaded the parameters Racoon already set (key, scale, mode, the kit's tuning...) are noted
// and put back after it, so the song stays in its key whatever the preset brings.

namespace variedad
{
enum class Grupo { house, afro, disco, techno, breaks, trap, latino, hiphop, chill };

Grupo grupoDe (const String& genreId, double bpm)
{
    static const std::map<String, Grupo> m {
        { "house", Grupo::house }, { "deephouse", Grupo::house }, { "techhouse", Grupo::house }, { "progressive", Grupo::house },
        { "ukgarage", Grupo::house },
        { "afrohouse", Grupo::afro }, { "organichouse", Grupo::afro }, { "amapiano", Grupo::afro }, { "afrobeats", Grupo::afro },
        { "hightempo", Grupo::afro },
        { "nudisco", Grupo::disco }, { "indiedance", Grupo::disco }, { "disco", Grupo::disco }, { "funk", Grupo::disco },
        { "synthwave", Grupo::disco }, { "pop", Grupo::disco },
        { "melodictechno", Grupo::techno }, { "techno", Grupo::techno }, { "hardtechno", Grupo::techno }, { "minimal", Grupo::techno },
        { "acid", Grupo::techno }, { "trance", Grupo::techno }, { "psytrance", Grupo::techno }, { "hardstyle", Grupo::techno },
        { "electro", Grupo::techno },
        { "dnb", Grupo::breaks }, { "liquid", Grupo::breaks }, { "jungle", Grupo::breaks }, { "breakbeat", Grupo::breaks },
        { "dubstep", Grupo::trap }, { "futurebass", Grupo::trap }, { "trap", Grupo::trap }, { "drill", Grupo::trap },
        { "phonk", Grupo::trap }, { "jerseyclub", Grupo::trap },
        { "reggaeton", Grupo::latino }, { "dembow", Grupo::latino }, { "moombahton", Grupo::latino }, { "cumbia", Grupo::latino },
        { "dancehall", Grupo::latino },
        { "hiphop", Grupo::hiphop }, { "lofi", Grupo::hiphop }, { "rnb", Grupo::hiphop },
        { "downtempo", Grupo::chill }, { "ambient", Grupo::chill } };
    if (auto it = m.find (genreId); it != m.end()) return it->second;
    if (bpm >= 160.0) return Grupo::breaks;
    if (bpm > 0.0 && bpm < 92.0) return Grupo::hiphop;
    return Grupo::house;
}

// the id of the group in kits-racoon.json
const char* idDe (Grupo g)
{
    static const char* ids[] { "house", "afro", "disco", "techno", "breaks", "trap", "latino", "hiphop", "chill" };
    return ids[(int) g];
}

StringArray u8 (std::initializer_list<const char*> l)
{
    StringArray a;
    for (auto* s : l) a.add (String::fromUTF8 (s));
    return a;
}

// Muestras Racoon kits (Hamster plays them as "Muestras / <name>", Mole by their name)
StringArray kitsReales (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return u8 ({ "Deep house", "House orgánico", "TR-707", "TR-505", "Techno", "Disco", "Afro" });
        case Grupo::afro:   return u8 ({ "Afro", "House orgánico", "Deep house", "Mundo: tablas y darbuka", "Cumbia", "Salsa" });
        case Grupo::disco:  return u8 ({ "Disco", "Synthpop 80", "LinnDrum", "CR-78", "CR-8000", "TR-707", "Virtuosity", "Gogodze 73", "Drumtraks" });
        case Grupo::techno: return u8 ({ "Techno", "MFB-512", "Sonic Pi Electrónico", "TR-808", "808 Emulado", "Drumtraks", "TR-707" });
        case Grupo::breaks: return u8 ({ "Gogodze 73", "Rhythm King", "Soul vintage", "Unruly", "Techno", "TR-808" });
        case Grupo::trap:   return u8 ({ "Trap 808", "Trap duro", "Bounce", "TR-808", "808 Emulado" });
        case Grupo::latino: return u8 ({ "Reggaetón y dembow", "Bounce", "Cumbia", "Salsa", "TR-808", "Afro" });
        case Grupo::hiphop: return u8 ({ "Boom bap", "Casio RZ-1", "Soul vintage", "Lo-fi", "Rhythm King", "TR-808", "Jazz con escobillas" });
        case Grupo::chill:  return u8 ({ "Lo-fi", "Soul vintage", "Mundo: tablas y darbuka", "Jazz con escobillas", "CR-78", "House orgánico", "Yamaha MR-10" });
    }
    return {};
}

// Hamster's synthesized kits: "<style> / <engine>"
StringArray kitsSinteticos (Grupo g, const String& genreKit)
{
    StringArray styles;
    switch (g)
    {
        case Grupo::house:  styles = { "House", "Tech House" }; break;
        case Grupo::afro:   styles = { "House", "Dark Disco" }; break;
        case Grupo::disco:  styles = { "Dark Disco", "Indie Dance" }; break;
        case Grupo::techno: styles = { "Techno", "Minimal", "Acid", "EBM", "Electro" }; break;
        case Grupo::breaks: styles = { "Electro", "Techno" }; break;
        case Grupo::trap:   styles = { "Electro" }; break;
        case Grupo::latino: styles = { "House" }; break;
        case Grupo::hiphop: styles = { "Indie Dance", "Dark Disco" }; break;
        case Grupo::chill:  styles = { "House", "Indie Dance" }; break;
    }
    StringArray out;
    if (genreKit.isNotEmpty()) out.add (genreKit);
    for (auto& s : styles)
        for (auto* e : { "909 Pressure", "808 Depth", "Basalt Engine", "Dust Machine" })
            out.addIfNotAlreadyThere (s + " / " + e);
    return out;
}

StringArray bajos (Grupo g)
{
    const StringArray rolling { "Rolling Stone", "Sixteenth River", "Offbeat Engine", "Tide Roller", "Basalt Ladder", "Temple Pulse",
                                "Copper Drift", "Neon Monolith", "Glass Pyramid", "Velvet Oracle", "Dance of Jaguars", "Midnight Codex", "Eclipse Pulse" };
    const StringArray sub { "Obsidian Round", "Pure Sub Glyph", "808 Monument", "Deep Earth", "Low Altar", "Underworld Rumble", "Stone Cathedral" };
    const StringArray acid { "Sun Glyph 303", "Square Acid Codex", "Half Moon Acid", "Rave Ritual" };
    const StringArray funk { "Black Sun Funk", "Funk Hieroglyph", "Slap Codex", "Quack Totem", "Wah Ritual", "Bone Thumb", "Picked Granite" };
    const StringArray fm { "Jade FM", "Bell Serpent", "Stone Pluck", "Short Glyph", "Electric Jaguar" };
    const StringArray reese { "Reese of Ra", "Twin Serpents", "Reese Monolith", "Hollow Reese", "Growl Stela", "Formant Mask", "Sync Obelisk", "Fold Temple" };
    StringArray out;
    auto add = [&out] (const StringArray& a) { out.addArray (a); };
    switch (g)
    {
        case Grupo::house:  add (rolling); add ({ "Obsidian Round", "Deep Earth", "Velvet Oracle", "Black Sun Funk", "Stone Pluck" }); break;
        case Grupo::afro:   add (rolling); add ({ "Obsidian Round", "Deep Earth", "Low Altar", "Bone Thumb", "Stone Pluck", "Jade FM" }); break;
        case Grupo::disco:  add (funk); add ({ "Rolling Stone", "Offbeat Engine", "Neon Monolith", "Jade FM", "Electric Jaguar", "Sixteenth River" }); break;
        case Grupo::techno: add (acid); add ({ "Rolling Stone", "Sixteenth River", "Tide Roller", "Neon Monolith", "Midnight Codex", "Pressure Chamber",
                                               "Underworld Rumble", "Sync Obelisk", "Cult Fifths", "Eclipse Pulse" }); break;
        case Grupo::breaks: add (reese); add ({ "Obsidian Round", "Pure Sub Glyph", "Underworld Rumble", "Deep Earth" }); break;
        case Grupo::trap:   add (sub); add ({ "Growl Stela", "Formant Mask", "Reese of Ra", "Hollow Reese" }); break;
        case Grupo::latino: add (sub); add ({ "Rolling Stone", "Offbeat Engine", "Bone Thumb", "Picked Granite", "Black Sun Funk", "Velvet Oracle" }); break;
        case Grupo::hiphop: add (funk); add ({ "Obsidian Round", "808 Monument", "Deep Earth", "Low Altar", "Velvet Oracle", "Jade FM" }); break;
        case Grupo::chill:  add ({ "Obsidian Round", "Pure Sub Glyph", "Deep Earth", "Low Altar", "Velvet Oracle", "Glass Pyramid", "Jade FM",
                                   "Bell Serpent", "Stone Pluck", "Bone Thumb", "Ritual Drone" }); break;
    }
    return out;
}

StringArray guitarras (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return u8 ({ "Chic Disco Rhythm", "Muted Funk", "Vintage Clean", "Nu-Disco contratiempos", "Indie Dance galope", "Instrumento directo", "Barítono Dark Disco" });
        case Grupo::afro:   return u8 ({ "Vintage Clean", "Muted Funk", "Nylon arpegiada", "Acústica rasgueada", "Instrumento directo", "Chic Disco Rhythm" });
        case Grupo::disco:  return u8 ({ "Chic Disco Rhythm", "Muted Funk", "Nu-Disco contratiempos", "Indie Dance galope", "Barítono Dark Disco", "Dark Post-Punk", "Vintage Clean", "Rock crunch" });
        case Grupo::techno: return u8 ({ "Dark Post-Punk", "Barítono Dark Disco", "Acid Guitar", "Vintage Clean", "Indie Arpeggio", "Instrumento directo" });
        case Grupo::breaks: return u8 ({ "Dark Post-Punk", "Vintage Clean", "Acid Guitar", "Rock crunch", "Indie Arpeggio" });
        case Grupo::trap:   return u8 ({ "Nylon arpegiada", "Vintage Clean", "Dark Post-Punk", "Acústica rasgueada", "Instrumento directo" });
        case Grupo::latino: return u8 ({ "Nylon arpegiada", "Acústica rasgueada", "Vintage Clean", "Muted Funk", "Chic Disco Rhythm" });
        case Grupo::hiphop: return u8 ({ "Vintage Clean", "Muted Funk", "Nylon arpegiada", "Instrumento directo", "Acústica rasgueada" });
        case Grupo::chill:  return u8 ({ "Nylon arpegiada", "Acústica rasgueada", "Vintage Clean", "Indie Arpeggio", "Instrumento directo" });
    }
    return {};
}

// Ferret's body: which guitar, which amp and which cabinet (on top of the preset, half of the time each)
StringArray modelosGuitarra (Grupo g)
{
    if (g == Grupo::latino || g == Grupo::chill || g == Grupo::trap)
        return u8 ({ "Acústica de acero", "Acústica de nylon", "Funk limpia", "Strat (bobina simple)" });
    if (g == Grupo::techno || g == Grupo::breaks)
        return u8 ({ "Barítono eléctrica", "Humbucker", "Strat (bobina simple)", "Apagada disco" });
    return u8 ({ "Strat (bobina simple)", "Humbucker", "Funk limpia", "Apagada disco", "Barítono eléctrica", "Acústica de acero" });
}
StringArray amplis (Grupo g)
{
    if (g == Grupo::disco || g == Grupo::breaks) return u8 ({ "Limpio", "Crunch", "Británico", "Americano", "Vintage de transistores" });
    return u8 ({ "Limpio", "Crunch", "Americano", "Vintage de transistores", "Directo" });
}
StringArray gabinetes() { return u8 ({ "1x12 abierto", "2x12 americano", "4x12 británico", "Combo vintage", "Sin gabinete" }); }

StringArray pads (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return u8 ({ "Colchón tibio", "Lana", "Domingo", "Cuerdas de bola", "Medianoche", "Bruma", "Acuarela", "Respiración", "Marea" });
        case Grupo::techno:
        case Grupo::breaks: return u8 ({ "Medianoche", "Pozo", "Antena", "Bruma", "Polen", "Marea", "Respiración" });
        case Grupo::trap:
        case Grupo::latino:
        case Grupo::hiphop: return u8 ({ "Medianoche", "Bruma", "Marea", "Pozo", "Colchón tibio", "Lana", "Acuarela" });
        case Grupo::disco:  return u8 ({ "Cuerdas de bola", "Medianoche", "Domingo", "Lana", "Acuarela", "Bruma" });
        case Grupo::afro:
        case Grupo::chill:  return u8 ({ "Colchón tibio", "Lana", "Domingo", "Acuarela", "Respiración", "Polen", "Marea", "Bruma" });
    }
    return {};
}
StringArray tablasPad() { return u8 ({ "Analógica", "Cálida", "Vocal", "Órgano", "Hueca", "VA Sierra", "VA Pulso" }); }   // no Cristal: too bright

StringArray sonidosChipmunk (Grupo g)
{
    // the synthesized ones and, since the library brings them, recorded instruments (they sound like the real thing)
    switch (g)
    {
        case Grupo::house:  return u8 ({ "Teclas", "Pluck", "Órgano", "Piano eléctrico", "Supersierra", "Pad", "Piano de cola", "Piano Splendid", "Piano FM", "Órgano de tubos" });
        case Grupo::afro:   return u8 ({ "Marimba", "Piano eléctrico", "Pluck", "Vibráfono", "Arpa", "Piano de cola", "Piano FM" });
        case Grupo::disco:  return u8 ({ "Teclas", "Órgano", "Piano eléctrico", "Supersierra", "Piano Splendid", "Piano FM", "Cuerdas de orquesta", "Clavecín" });
        case Grupo::techno: return u8 ({ "Pluck", "Supersierra", "Pad", "Cuerdas", "Órgano de tubos", "Cuerdas de orquesta" });
        case Grupo::breaks: return u8 ({ "Pad", "Cuerdas", "Piano eléctrico", "Supersierra", "Piano de cola", "Cuerdas de orquesta" });
        case Grupo::trap:   return u8 ({ "Piano eléctrico", "Pluck", "Pad", "Piano de cola", "Piano vertical", "Cuerdas de orquesta", "Arpa", "Clavecín" });
        case Grupo::latino: return u8 ({ "Piano eléctrico", "Marimba", "Pluck", "Órgano", "Piano de cola", "Piano de cantina", "Vibráfono" });
        case Grupo::hiphop: return u8 ({ "Piano eléctrico", "Teclas", "Órgano", "Piano vertical", "Piano de cantina", "Vibráfono", "Cuerdas de orquesta", "Piano FM" });
        case Grupo::chill:  return u8 ({ "Piano eléctrico", "Pad", "Piano de cola", "Piano vertical", "Vibráfono", "Arpa", "Cuerdas de orquesta" });
    }
    return {};
}

StringArray coros (Grupo g)
{
    auto a = u8 ({ "Catedral (Ah)", "Coro pop (Ooh)", "Gospel cálido", "Coro de cámara", "Mm susurrado", "Coro con cuerdas", "Ensamble de cine", "Voces lejanas" });
    if (g == Grupo::techno || g == Grupo::trap) a.add ("Metales épicos"_u);
    return a;
}

// the melody: Squirrel's own presets, or an instrument of the library of these families
StringArray presetsMelodia() { return u8 ({ "Inicial", "Tecla suave", "Pad en loop", "Filtro que respira", "Cinta lenta", "Pluck", "Vibrato" }); }
StringArray familiasMelodia (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return { "piano", "piano_electrico", "mallets", "mundo", "sintetizador", "maderas", "saxofon", "teclado" };
        case Grupo::afro:   return { "mallets", "mundo", "piano", "piano_electrico", "maderas", "cuerda_pulsada" };
        case Grupo::disco:  return { "piano_electrico", "teclado", "sintetizador", "cuerdas", "saxofon", "metales", "piano" };
        case Grupo::techno: return { "sintetizador", "piano_electrico", "mallets", "cuerdas" };
        case Grupo::breaks: return { "piano", "piano_electrico", "cuerdas", "sintetizador", "saxofon" };
        case Grupo::trap:   return { "piano", "mallets", "mundo", "cuerdas", "arpa", "maderas", "sintetizador" };
        case Grupo::latino: return { "mallets", "mundo", "piano", "metales", "maderas", "saxofon", "cuerda_pulsada" };
        case Grupo::hiphop: return { "piano", "piano_electrico", "saxofon", "mallets", "cuerdas", "maderas", "armonica" };
        case Grupo::chill:  return { "piano", "mallets", "mundo", "maderas", "arpa", "cuerda_pulsada", "piano_electrico", "cuerdas" };
    }
    return {};
}

StringArray materialesSkunk (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return u8 ({ "Cuerdas lejanas", "Coro oscuro", "Agua profunda", "Órgano grave", "Polvo de cinta" });
        case Grupo::afro:   return u8 ({ "Agua profunda", "Cuerdas lejanas", "Viento afinado", "Órgano grave", "Polvo de cinta" });
        case Grupo::disco:  return u8 ({ "Cuerdas lejanas", "Órgano grave", "Coro oscuro", "Polvo de cinta" });
        case Grupo::techno: return u8 ({ "Viento afinado", "Órgano grave", "Coro oscuro", "Agua profunda", "Polvo de cinta" });
        case Grupo::breaks: return u8 ({ "Agua profunda", "Viento afinado", "Órgano grave", "Polvo de cinta", "Coro oscuro" });
        case Grupo::trap:   return u8 ({ "Coro oscuro", "Agua profunda", "Viento afinado", "Órgano grave", "Polvo de cinta" });
        case Grupo::latino: return u8 ({ "Coro oscuro", "Agua profunda", "Cuerdas lejanas", "Órgano grave", "Polvo de cinta" });
        case Grupo::hiphop: return u8 ({ "Polvo de cinta", "Coro oscuro", "Cuerdas lejanas", "Órgano grave" });
        case Grupo::chill:  return u8 ({ "Agua profunda", "Polvo de cinta", "Viento afinado", "Órgano grave", "Coro oscuro" });
    }
    return {};
}

StringArray vocesChinchilla (Grupo g)
{
    juce::ignoreUnused (g);
    return { "Grave", "Grave", "Media" };         // dark: the low voice twice as often, never the high one
}
StringArray efectosChinchilla (Grupo g)
{
    auto a = u8 ({ "Afinación natural", "Doble ancho", "Coro de armonías", "Voz de radio" });
    if (g == Grupo::trap || g == Grupo::latino || g == Grupo::techno) a.add ("Hard tune");
    if (g == Grupo::disco) a.add ("Talkbox funk");
    return a;
}

// the genres of the library's tags that suit each group (to pick a kick, a snare... that belongs)
StringArray etiquetas (Grupo g)
{
    switch (g)
    {
        case Grupo::house:  return { "house", "techno", "disco", "electro", "synthpop", "edm", "funk" };
        case Grupo::afro:   return { "afro", "afrobeat", "world", "house", "latino", "reggae" };
        case Grupo::disco:  return { "disco", "funk", "synthpop", "pop", "italo", "synthwave", "new wave", "soul" };
        case Grupo::techno: return { "techno", "electro", "industrial", "ebm", "idm", "edm", "darkwave" };
        case Grupo::breaks: return { "funk", "soul", "breakbeat", "jungle", "hiphop", "electro", "rock" };
        case Grupo::trap:   return { "trap", "drill", "hiphop", "miami bass", "edm" };
        case Grupo::latino: return { "reggaeton", "dembow", "latino", "cumbia", "salsa", "reggae" };
        case Grupo::hiphop: return { "boombap", "hiphop", "lofi", "soul", "funk", "jazz", "rnb" };
        case Grupo::chill:  return { "lofi", "jazz", "world", "ambient", "downtempo", "trip-hop", "soul" };
    }
    return {};
}

// the drum roles that ALEATORIA can give another sound, the library's categories for each, and their GM notes
struct RolBateria { const char* rol; StringArray cats; std::vector<int> notas; };
const std::vector<RolBateria>& rolesBateria()
{
    static const std::vector<RolBateria> r {
        { "Kick", { "bombo" }, { 35, 36 } }, { "Snare", { "caja" }, { 38, 40 } }, { "Clap", { "palmas", "chasquido" }, { 39 } },
        { "Closed Hat", { "hihat_cerrado", "hihat_pedal" }, { 42, 44 } }, { "Open Hat", { "hihat_abierto" }, { 46 } },
        { "Shaker", { "shaker", "pandereta", "cabasa", "guiro" }, { 69, 70, 82 } }, { "Rim", { "aro", "clave", "bloque" }, { 37, 75, 76, 77 } },
        { "Conga", { "conga", "bongo" }, { 60, 61, 62, 63, 64 } }, { "Cowbell", { "cencerro", "agogo" }, { 56, 67, 68 } },
        { "Ride", { "ride" }, { 51, 53, 59 } } };
    return r;
}

// what a preset may change that the song needs as it was: noted before, put back after
const StringArray& seConservan()
{
    static const StringArray a = u8 ({ "Tonalidad", "Escala", "Modo", "Acordes", "Conducción de voces", "Registro bajo", "Registro alto",
                                       "Afinar el kit a", "Escala del kit", "Notas del kit", "Pattern follows the DAW", "Seq Mode",
                                       "Afinar la percusión a", "Escala de la percusión", "Reproducir", "Cantar las notas MIDI", "Cantar: voz",
                                       "Granos con el teclado", "Biblioteca" });
    return a;
}

int notaDe (const String& name)   // "C#2" -> 37 (C4 = 60), "E-1" -> 4
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int pc = -1, len = 0;
    for (int i = 0; i < 12; ++i)
        if (name.startsWith (names[i]) && (int) std::strlen (names[i]) > len) { pc = i; len = (int) std::strlen (names[i]); }
    if (pc < 0) return -1;
    return (name.substring (len).getIntValue() + 1) * 12 + pc;
}
}

namespace
{
juce::AudioPluginInstance* instanceOf (te::Plugin* p)
{
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p)) return ext->getAudioPluginInstance();
    return nullptr;
}

bool setChoiceByText (juce::AudioProcessorParameter* prm, const String& label)
{
    const int n = jmax (2, prm->getNumSteps());
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) i / (float) (n - 1);
        if (prm->getText (v, 64) == label) { prm->setValueNotifyingHost (v); return true; }
    }
    return false;
}

juce::AudioProcessorParameter* paramNamed (juce::AudioPluginInstance& pi, const String& name)
{
    for (auto* prm : pi.getParameters()) if (prm->getName (64) == name) return prm;
    return nullptr;
}

bool setParamText (juce::AudioPluginInstance& pi, const String& name, const String& text)
{
    if (auto* prm = paramNamed (pi, name)) return setChoiceByText (prm, text);
    return false;
}

// loads the program with that name, keeping the parameters the song needs
bool loadProgramKeeping (juce::AudioPluginInstance& pi, const String& program)
{
    int index = -1;
    for (int i = 0; i < pi.getNumPrograms() && index < 0; ++i) if (pi.getProgramName (i) == program) index = i;
    if (index < 0) return false;
    std::vector<std::pair<juce::AudioProcessorParameter*, float>> kept;
    for (auto* prm : pi.getParameters())
        if (variedad::seConservan().contains (prm->getName (64))) kept.push_back ({ prm, prm->getValue() });
    pi.setCurrentProgram (index);
    for (auto& [prm, v] : kept) if (std::abs (prm->getValue() - v) > 1.0e-6f) prm->setValueNotifyingHost (v);
    return true;
}

// the plugin's own state inside what the host hands over: as it is (a JUCE plugin hosted directly), inside the VST3
// wrapper (base64 in <IComponent>, followed by the wrapper's private data) or inside an AU's property list
// (<key>jucePluginState</key><data>...</data>). edit changes the plugin's XML; whatever is around it stays.
bool editPluginXml (juce::AudioPluginInstance& pi, const std::function<void (juce::XmlElement&)>& edit, bool write = true)
{
    auto editInner = [&edit] (juce::MemoryBlock& inner) -> bool
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        if (xml == nullptr || ! (xml->hasTagName ("RACOON") || xml->hasTagName ("DRUMENGINE"))) return false;
        // what comes after the XML block (the VST3 wrapper's private data) is kept as it was
        juce::MemoryBlock tail;
        if (inner.getSize() >= 8)
        {
            const auto len = (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (inner.getData()) + 4);
            if (8 + len < inner.getSize()) tail.append (static_cast<const char*> (inner.getData()) + 8 + len, inner.getSize() - 8 - len);
        }
        edit (*xml);
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        out.append (tail.getData(), tail.getSize());
        inner = out;
        return true;
    };
    juce::MemoryBlock mb;
    pi.getStateInformation (mb);
    bool done = false;
    if (auto outer = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
    {
        if (outer->hasTagName ("RACOON") || outer->hasTagName ("DRUMENGINE"))
        {
            done = editInner (mb);
        }
        else if (auto* comp = outer->getChildByName ("IComponent"))
        {
            juce::MemoryBlock inner;
            if (inner.fromBase64Encoding (comp->getAllSubText()) && editInner (inner))
            {
                comp->deleteAllTextElements();
                comp->addTextElement (inner.toBase64Encoding());
                mb.reset();
                juce::AudioProcessor::copyXmlToBinary (*outer, mb);
                done = true;
            }
        }
    }
    else if (auto plist = juce::XmlDocument::parse (mb.toString()))
    {
        // an AU: the dictionary holds the plugin's state as data after the key jucePluginState
        std::function<bool (juce::XmlElement&)> walk = [&] (juce::XmlElement& e) -> bool
        {
            for (auto* c = e.getFirstChildElement(); c != nullptr; c = c->getNextElement())
            {
                if (c->hasTagName ("key") && c->getAllSubText().trim() == "jucePluginState")
                    if (auto* data = c->getNextElement(); data != nullptr && data->hasTagName ("data"))
                    {
                        juce::MemoryBlock inner;
                        {
                            juce::MemoryOutputStream mo (inner, false);
                            if (! juce::Base64::convertFromBase64 (mo, data->getAllSubText().removeCharacters (" \t\r\n"))) return false;
                        }
                        if (! editInner (inner)) return false;
                        data->deleteAllTextElements();
                        data->addTextElement (juce::Base64::toBase64 (inner.getData(), inner.getSize()));
                        return true;
                    }
                if (walk (*c)) return true;
            }
            return false;
        };
        if (walk (*plist))
        {
            mb.reset();
            mb.append (plist->toString().toRawUTF8(), plist->toString().getNumBytesAsUTF8());
            done = true;
        }
    }
    if (done && write) pi.setStateInformation (mb.getData(), (int) mb.getSize());
    return done;
}

}

// the plugin's own XML, for the tests (and for whoever wants to read what a Racoon plugin keeps)
String Session::pluginStateXml (juce::AudioPluginInstance& pi)
{
    String text;
    editPluginXml (pi, [&] (juce::XmlElement& xml) { text = xml.toString(); }, false);
    return text;
}

namespace
{
String pickOf (juce::Random& r, const StringArray& a) { return a.isEmpty() ? String() : a[r.nextInt (a.size())]; }
}

StringArray Session::kitPoolFor (const Genre& g)
{
    StringArray out;
    const auto grupo = variedad::grupoDe (g.id, g.bpm);
    const auto& cat = biblio::Catalogo::get();
    if (cat.disponible() && SystemStats::getEnvironmentVariable ("RACOON_KIT_SINTETICO", {}).isEmpty())
    {
        // first the Kits Racoon of its group (the hits made in the workshop), then the kits of the library as they come
        for (auto* r : cat.recetasDeGrupo (variedad::idDe (grupo))) out.add ("Kits Racoon / " + r->nombre);
        auto reales = variedad::kitsReales (grupo);
        reales.insert (0, muestras::kitParaGenero (g.id));
        for (auto& k : reales) if (k.isNotEmpty() && cat.kit (k) != nullptr) out.addIfNotAlreadyThere ("Muestras / " + k);
    }
    out.addArray (variedad::kitsSinteticos (grupo, String (g.kit)));
    out.removeEmptyStrings();
    return out;
}

StringArray Session::varySounds (juce::uint32 seed, const juce::Array<te::AudioTrack*>& skip, const SoundVariation& opts)
{
    StringArray changed;
    gusto::empezar();
    const auto genreId = edit->state.getProperty ("racoonGenre").toString();
    const auto* genre = findGenre (genreId);
    const auto grupo = variedad::grupoDe (genreId, getBpm());
    juce::Random rng ((int64) seed * 2654435761LL + genreId.hashCode64());
    const auto& cat = biblio::Catalogo::get();
    const bool reales = cat.disponible() && SystemStats::getEnvironmentVariable ("RACOON_KIT_SINTETICO", {}).isEmpty();
    const bool hayLlave = edit->state.hasProperty ("racoonKeyRoot");
    const int keyRoot = edit->state.getProperty ("racoonKeyRoot", 0);
    const bool keyMajor = edit->state.getProperty ("racoonKeyMajor", false);

    // the Hamster kit (all the Hamster channels share it) and, for Mole, another one of the same pool
    String hamsterKit;
    {
        StringArray pool;
        if (genre != nullptr) pool = kitPoolFor (*genre);
        else
        {
            if (reales) for (auto& k : variedad::kitsReales (grupo)) if (cat.kit (k) != nullptr) pool.add ("Muestras / " + k);
            pool.addArray (variedad::kitsSinteticos (grupo, {}));
        }
        StringArray taller, real, synth;
        for (auto& k : pool) (k.startsWith ("Kits Racoon / ") ? taller : k.startsWith ("Muestras / ") ? real : synth).add (k);
        // a Kit Racoon four times of five; then a kit of the library as it comes; one of the synthesized ones once in a while
        const int dado = rng.nextInt (100);
        if (! taller.isEmpty() && dado < 80) hamsterKit = gusto::elegir (rng, "kit", taller);
        else if (! real.isEmpty() && (synth.isEmpty() || dado < 92)) hamsterKit = gusto::elegir (rng, "kit", real);
        else hamsterKit = gusto::elegir (rng, "kit", synth.isEmpty() ? taller : synth);
    }

    // the loose drum sounds, the same for every Hamster channel of the song (the Kick and the Groove stay one kit):
    // each role one time in two (more when the kit stays), never the ones the user held with their row's lock
    StringArray drumSwaps;            // "Kick=grupo~Name"
    if (reales)
    {
        const auto tags = variedad::etiquetas (grupo);
        for (auto& rb : variedad::rolesBateria())
        {
            bool held = false;
            for (int n : rb.notas) held = held || opts.keepDrumNotes.count (n) > 0;
            if (held || rng.nextInt (100) >= (opts.newKit ? 45 : 70)) continue;
            // the same pad of another Kit Racoon of the group: the new sound comes out of the workshop too
            {
                static const std::map<String, int> padDe { { "Kick", 0 }, { "Snare", 2 }, { "Clap", 3 }, { "Rim", 4 }, { "Closed Hat", 5 },
                                                           { "Open Hat", 6 }, { "Ride", 7 }, { "Shaker", 9 }, { "Conga", 10 }, { "Cowbell", 13 } };
                const auto recetas = cat.recetasDeGrupo (variedad::idDe (grupo));
                auto pd = padDe.find (String (rb.rol));
                if (! recetas.empty() && pd != padDe.end() && rng.nextInt (100) < 85)
                {
                    const auto* r = recetas[(size_t) rng.nextInt ((int) recetas.size())];
                    if (const auto* p = r->pad (pd->second))
                    {
                        drumSwaps.add (String (rb.rol) + "=receta:" + r->id + "/" + String (pd->second) + "~" + p->nombre + " (" + r->nombre + ")");
                        continue;
                    }
                }
            }
            auto cats = rb.cats;
            if (grupo == variedad::Grupo::trap && String (rb.rol) == "Kick") cats.add ("bajo_808");
            std::vector<const biblio::Grupo*> fits, any;
            for (auto* g : cat.gruposDe (cats))
            {
                any.push_back (g);
                for (auto& t : tags) if (g->generos.contains (t)) { fits.push_back (g); break; }
            }
            const auto& from = fits.size() >= 3 ? fits : any;
            if (from.empty()) continue;
            const auto* g = from[(size_t) rng.nextInt ((int) from.size())];
            drumSwaps.add (String (rb.rol) + "=" + g->id + "~" + g->nombre);
        }
    }

    // the melody's range, so the instrument chosen for it plays it in its own register
    int melLo = 128, melHi = -1;

    for (auto* t : te::getAudioTracks (*edit))
    {
        if (skip.contains (t)) continue;
        for (auto* p : t->pluginList)
        {
            if (! Catalog::isRacoonPlugin (*p)) continue;
            const auto animal = Catalog::animalFor (p->getName());
            auto* pi = instanceOf (p);
            if (pi == nullptr || animal.isEmpty()) continue;
            String what;
            if (animal == "Hamster")
            {
                if (opts.newKit && hamsterKit.isNotEmpty() && pi->getProgramName (pi->getCurrentProgram()) != hamsterKit && loadProgramKeeping (*pi, hamsterKit))
                    what = hamsterKit;
                // and loose sounds of the library on some pads: another kick, snare, clap, hats, shaker...
                if (! drumSwaps.isEmpty())
                {
                    StringArray pairs;
                    for (auto& sw : drumSwaps) pairs.add (sw.upToFirstOccurrenceOf ("=", false, false) + "=" + sw.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf ("~", false, false));
                    if (editPluginXml (*pi, [&] (juce::XmlElement& x) { x.setAttribute ("cambiarSonidos", pairs.joinIntoString ("|")); }))
                    {
                        if (what.isEmpty()) what = pi->getProgramName (pi->getCurrentProgram());
                        for (auto& sw : drumSwaps) what << ", " << sw.upToFirstOccurrenceOf ("=", false, false).toLowerCase() << " " << sw.fromFirstOccurrenceOf ("~", false, false);
                    }
                }
                // the kit is measured again and tuned to the song's key
                if (hayLlave)
                {
                    if (auto* prm = paramNamed (*pi, "Afinar el kit a")) prm->setValueNotifyingHost ((float) (keyRoot + 1) / (float) jmax (1, prm->getNumSteps() - 1));
                    if (auto* prm = paramNamed (*pi, "Escala del kit")) prm->setValueNotifyingHost (keyMajor ? 1.0f : 0.0f);
                }
            }
            else if (animal == "Badger")
            {
                const auto b = gusto::elegir (rng, "bajo", variedad::bajos (grupo));
                if (loadProgramKeeping (*pi, b)) what = b;
            }
            else if (animal == "Ferret")
            {
                const auto preset = gusto::elegir (rng, "guitarra", variedad::guitarras (grupo));
                if (loadProgramKeeping (*pi, preset)) what = preset;
                // the guitar itself changes every time, the amp and the cabinet most times, and how the hand plays
                { const auto m = gusto::elegir (rng, "modelo de guitarra", variedad::modelosGuitarra (grupo)); if (setParamText (*pi, "Guitarra", m)) what << ", " << m; }
                // one time in three a recorded guitar of the library instead of the modelled strings (the amp and cabinet stay)
                {
                    const bool acustica = grupo == variedad::Grupo::chill || grupo == variedad::Grupo::afro || grupo == variedad::Grupo::latino
                                          || grupo == variedad::Grupo::hiphop;
                    const auto grabadas = acustica ? variedad::u8 ({ "Acústica Emily", "Acústica Shiny" })
                                                   : variedad::u8 ({ "Eléctrica Black", "Eléctrica Green", "Eléctrica limpia Shiny" });
                    const bool grabada = cat.disponible() && rng.nextInt (100) < 35;
                    const auto g = grabada ? gusto::elegir (rng, "guitarra grabada", grabadas) : String ("Modelada (cuerdas)");
                    if (setParamText (*pi, "Sonido de la guitarra", g) && grabada) what << ", grabada " << g;
                }
                if (rng.nextInt (100) < 75) { const auto a = gusto::elegir (rng, "amplificador", variedad::amplis (grupo)); if (setParamText (*pi, "Amplificador", a)) what << ", ampli " << a; }
                if (rng.nextInt (100) < 65) { const auto c = gusto::elegir (rng, "gabinete", variedad::gabinetes()); if (setParamText (*pi, "Gabinete", c)) what << ", " << c; }
                {
                    const auto art = variedad::u8 ({ "Sostenido", "Sostenido", "Palm mute", "Medio mute", "Vibrato" })[rng.nextInt (5)];
                    if (setParamText (*pi, "Articulación"_u, art) && art != "Sostenido") what << ", " << art.toLowerCase();
                }
            }
            else if (animal == "Dormouse")
            {
                const auto preset = gusto::elegir (rng, "pad", variedad::pads (grupo));
                if (loadProgramKeeping (*pi, preset)) what = preset;
                if (rng.nextInt (100) < 40) { const auto w = gusto::elegir (rng, "onda del pad", variedad::tablasPad()); if (setParamText (*pi, "Osc 1 tabla", w)) what << ", onda " << w; }
            }
            else if (animal == "Chipmunk")
            {
                const auto s = gusto::elegir (rng, "sonido de acordes", variedad::sonidosChipmunk (grupo));
                if (setParamText (*pi, "Sonido", s)) what = s;
            }
            else if (animal == "Porcupine")
            {
                const auto c = gusto::elegir (rng, "coro", variedad::coros (grupo));
                if (loadProgramKeeping (*pi, c)) what = c;
            }
            else if (animal == "Squirrel")
            {
                if (melHi < 0)
                    for (auto* c : t->getClips())
                        if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                            for (auto* n : mc->getSequence().getNotes()) { melLo = jmin (melLo, n->getNoteNumber()); melHi = jmax (melHi, n->getNoteNumber()); }
                // six of ten times an instrument of the library (piano, marimba, sax, kalimba...), the rest one of its own
                std::vector<const biblio::Instrumento*> fit;
                if (cat.disponible())
                    for (auto& in : cat.instrumentos)
                    {
                        if (! variedad::familiasMelodia (grupo).contains (in.cat)) continue;
                        const int lo = variedad::notaDe (in.rangoBajo), hi = variedad::notaDe (in.rangoAlto);
                        if (melHi >= 0 && lo >= 0 && hi >= 0 && (melLo < lo - 5 || melHi > hi + 5)) continue;
                        fit.push_back (&in);
                    }
                if (! fit.empty() && rng.nextInt (100) < 60)
                {
                    StringArray ids;
                    for (auto* f : fit) ids.add (f->nombre);
                    const auto pick = gusto::elegir (rng, "instrumento de melodía"_u, ids);
                    const auto* in = fit[(size_t) jmax (0, ids.indexOf (pick))];
                    // one write: the instrument to load and the header's name (a second write while it loads would empty it)
                    if (editPluginXml (*pi, [&] (juce::XmlElement& xml)
                        {
                            auto* extra = xml.getChildByName ("EXTRA");
                            if (extra == nullptr) extra = xml.createNewChildElement ("EXTRA");
                            extra->setAttribute ("cargarInstrumento", in->id);
                            if (xml.hasTagName ("RACOON")) xml.setAttribute ("preset", in->nombre);
                        }))
                        what = in->nombre;
                }
                else
                {
                    const auto preset = gusto::elegir (rng, "melodía"_u, variedad::presetsMelodia());
                    if (loadProgramKeeping (*pi, preset)) what = preset;
                }
            }
            else if (animal == "Mole")
            {
                // Mole plays its percussion with its own kit: another kit of the genre than Hamster's, or its synthesized one
                StringArray pool;
                if (reales) for (auto& k : variedad::kitsReales (grupo)) if (cat.kit (k) != nullptr && "Muestras / " + k != hamsterKit) pool.add (k);
                const auto k = (! pool.isEmpty() && rng.nextInt (100) < 75) ? gusto::elegir (rng, "kit de percusión"_u, pool) : String();
                const auto name = k.isEmpty() ? String::fromUTF8 ("Kit sintetizado") : k;
                if (editPluginXml (*pi, [&] (juce::XmlElement& xml)        // the kit and the header's name in one write
                    {
                        auto* extra = xml.getChildByName ("EXTRA");
                        if (extra == nullptr) extra = xml.createNewChildElement ("EXTRA");
                        extra->setAttribute ("kit", k);
                        if (xml.hasTagName ("RACOON")) xml.setAttribute ("preset", name);
                    }))
                    what = k.isEmpty() ? String::fromUTF8 ("kit sintetizado") : k;
            }
            else if (animal == "Skunk")
            {
                const auto m = gusto::elegir (rng, "textura", variedad::materialesSkunk (grupo));
                if (setParamText (*pi, "Biblioteca", m)) { what = m; edit->state.setProperty ("racoonTexture", m, nullptr); }
            }
            else if (animal == "Chinchilla")
            {
                if (rng.nextInt (100) < 50)
                {
                    const auto e = gusto::elegir (rng, "efecto de voz", variedad::efectosChinchilla (grupo));
                    if (loadProgramKeeping (*pi, e)) what = e;
                }
                const auto v = gusto::elegir (rng, "voz", variedad::vocesChinchilla (grupo));
                if (setParamText (*pi, "Cantar: voz", v)) what << (what.isEmpty() ? "" : ", ") << "Voz " << v.toLowerCase();
            }
            if (what.isNotEmpty()) changed.add (animal + ": " + what);
            // the plugin's header names the sound it got (Chipmunk and Skunk change a parameter, not a program, and the
            // header kept saying "Inicial"; Mole's kit and Squirrel's library instrument name it in their own write above)
            if (what.isNotEmpty() && (animal == "Chipmunk" || animal == "Skunk"))
            {
                const auto shown = what.upToFirstOccurrenceOf (",", false, false).trim();
                editPluginXml (*pi, [&] (juce::XmlElement& x) { if (x.hasTagName ("RACOON")) x.setAttribute ("preset", shown); });
            }
            break;   // the channel's Racoon instrument (the effects after it keep theirs)
        }
    }
    edit->state.setProperty ("racoonSounds", changed.joinIntoString ("\n"), nullptr);
    // what was chosen, for "Me gusta" / "No me gusta"
    edit->state.setProperty ("racoonElecciones", gusto::elecciones().joinIntoString ("|"), nullptr);
    edit->state.setProperty ("racoonCalificado", false, nullptr);
    return changed;
}

// ALEATORIA's sounds as one Cmd+Z step: every Racoon instrument that may change keeps a copy of its state before and
// after; the action keeps each plugin's id (the plugin may go before the history does)
namespace
{
struct SoundsAction : public juce::UndoableAction
{
    explicit SoundsAction (te::Edit& e) : edit (e) {}
    struct One { te::EditItemID id; juce::MemoryBlock before, after; };
    te::Edit& edit;
    std::vector<One> all;
    bool done = true;
    void put (const One& o, const juce::MemoryBlock& m)
    {
        for (auto* p : te::getAllPlugins (edit, false))
            if (p->itemID == o.id)
                if (auto* pi = instanceOf (p)) pi->setStateInformation (m.getData(), (int) m.getSize());
    }
    bool perform() override { if (! done) for (auto& o : all) put (o, o.after); done = false; return true; }
    bool undo() override { for (auto& o : all) put (o, o.before); return true; }
};
}

StringArray Session::varySoundsUndoable (juce::uint32 seed, const juce::Array<te::AudioTrack*>& skip, const SoundVariation& opts)
{
    auto action = std::make_unique<SoundsAction> (*edit);
    for (auto* t : te::getAudioTracks (*edit))
        if (! skip.contains (t))
            if (auto* p = familyPluginOf (*t))
                if (auto* pi = instanceOf (p))
                {
                    SoundsAction::One o { p->itemID, {}, {} };
                    pi->getStateInformation (o.before);
                    action->all.push_back (std::move (o));
                }
    const auto changed = varySounds (seed, skip, opts);
    for (auto& o : action->all)
        for (auto* p : te::getAllPlugins (*edit, false))
            if (p->itemID == o.id)
                if (auto* pi = instanceOf (p)) pi->getStateInformation (o.after);
    if (! changed.isEmpty()) edit->getUndoManager().perform (action.release());
    return changed;
}

String Session::rateSounds (bool meGusta)
{
    const auto list = StringArray::fromTokens (edit->state.getProperty ("racoonElecciones").toString(), "|", "");
    if (list.isEmpty()) return {};
    gusto::calificar (list, meGusta);
    edit->state.setProperty ("racoonCalificado", true, nullptr);
    StringArray names;
    for (auto& l : list) names.add (l.fromFirstOccurrenceOf ("=", false, false));
    while (names.size() > 4) names.remove (names.size() - 1);
    return meGusta ? i18n::tr ("Anotado: de aquí en adelante saldrán más seguido ") + names.joinIntoString (", ") + "."
                   : i18n::tr ("Anotado: de aquí en adelante saldrán menos ") + names.joinIntoString (", ") + ".";
}

// Included by Session.cpp. The clean-track certificate: next to an exported song, a text file that lists every sample
// that really sounds in it (the pads Hamster's notes fire, the instrument Squirrel plays, the waves Badger loads, the
// audio clips of the library), each with its library, author, license and origin, and the credits the CC BY licenses
// ask for, ready to paste. What Racoon cannot vouch for (a third-party plugin, an audio file of your own) is listed apart.

namespace certificado
{
// SHA-256 (FIPS 180-4) of a file, to tie the certificate to the exported audio
String sha256 (const File& f)
{
    static const uint32 K[64] {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
    uint32 h[8] { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    auto rotr = [] (uint32 x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&] (const uint8* b)
    {
        uint32 w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32) b[i * 4] << 24 | (uint32) b[i * 4 + 1] << 16 | (uint32) b[i * 4 + 2] << 8 | b[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            const uint32 s0 = rotr (w[i - 15], 7) ^ rotr (w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr (w[i - 2], 17) ^ rotr (w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32 a = h[0], b2 = h[1], c = h[2], d = h[3], e = h[4], ff = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            const uint32 t1 = hh + (rotr (e, 6) ^ rotr (e, 11) ^ rotr (e, 25)) + ((e & ff) ^ (~e & g)) + K[i] + w[i];
            const uint32 t2 = (rotr (a, 2) ^ rotr (a, 13) ^ rotr (a, 22)) + ((a & b2) ^ (a & c) ^ (b2 & c));
            hh = g; g = ff; ff = e; e = d + t1; d = c; c = b2; b2 = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b2; h[2] += c; h[3] += d; h[4] += e; h[5] += ff; h[6] += g; h[7] += hh;
    };
    FileInputStream in (f);
    if (! in.openedOk()) return {};
    uint64 total = 0;
    uint8 buf[64];
    for (;;)
    {
        const int n = in.read (buf, 64);
        total += (uint64) juce::jmax (0, n);
        if (n == 64) { block (buf); continue; }
        uint8 last[128] {};
        std::memcpy (last, buf, (size_t) juce::jmax (0, n));
        last[juce::jmax (0, n)] = 0x80;
        const int len = n < 56 ? 64 : 128;
        const uint64 bits = total * 8;
        for (int i = 0; i < 8; ++i) last[len - 1 - i] = (uint8) (bits >> (8 * i));
        block (last);
        if (len == 128) block (last + 64);
        break;
    }
    String hex;
    for (auto v : h) hex << String::toHexString ((int) v).paddedLeft ('0', 8);
    return hex;
}

struct Uso { String canal, sonido, muestra; };

// the values of every attribute of a plugin's state, except under the elements to skip
void valores (const XmlElement& e, const StringArray& saltar, StringArray& out)
{
    if (saltar.contains (e.getTagName())) return;
    for (int i = 0; i < e.getNumAttributes(); ++i) out.add (e.getAttributeValue (i));
    for (auto* c : e.getChildIterator()) valores (*c, saltar, out);
}

// the library sample (or Kit Racoon pad) a word of a state names, if any
StringArray referencias (const String& valor)
{
    const auto& cat = biblio::Catalogo::get();
    StringArray out;
    if (valor.length() > 4000) return out;                          // embedded audio (base64), not a reference
    for (auto tok : StringArray::fromTokens (valor, "|;,=~\n", ""))
    {
        tok = tok.trim();
        if (tok.length() < 4) continue;
        // an absolute path inside the library: a sample, or a note of a library instrument (Squirrel's zones)
        const bool relativa = tok.startsWith ("biblioteca:");               // how Squirrel names the files of the library
        if (relativa || (File::isAbsolutePath (tok) && cat.disponible() && File (tok).isAChildOf (cat.raiz)))
        {
            const auto rel = relativa ? tok.fromFirstOccurrenceOf ("biblioteca:", false, false)
                                      : File (tok).getRelativePathFrom (cat.raiz).replaceCharacter ('\\', '/');
            const auto id = rel.upToLastOccurrenceOf (".", false, false);
            if (cat.muestra (id) != nullptr) { out.add (id); continue; }
            const String* hit = nullptr;
            for (auto& in : cat.instrumentos)
            {
                const auto base = in.archivo.upToLastOccurrenceOf (".", false, false);
                if (rel.startsWith (base + "/") || rel.startsWith (base + "_") || rel.startsWith (base + "-")) { hit = &in.id; break; }
            }
            if (hit == nullptr)
            {
                // same folder as exactly one instrument
                const auto dir = rel.upToLastOccurrenceOf ("/", false, false);
                int n = 0;
                for (auto& in : cat.instrumentos) if (in.archivo.upToLastOccurrenceOf ("/", false, false) == dir) { hit = &in.id; ++n; }
                if (n != 1) hit = nullptr;
            }
            if (hit != nullptr) out.add ("instrumento:" + *hit);
            continue;
        }
        if (tok.startsWith ("receta:")) { out.addArray (cat.origenes (tok)); continue; }
        if (cat.muestra (tok) != nullptr) { out.add (tok); continue; }
        if (auto* g = cat.grupo (tok)) { if (! g->muestras.empty()) out.add (cat.muestras[(size_t) g->muestras.front()].id); continue; }
        if (auto* in = cat.instrumento (tok)) { out.add ("instrumento:" + in->id); continue; }
        if (tok.startsWith ("kit:") || tok == tok.toLowerCase())                // Mole's "kit": a kit of the library, every lane
            if (auto* k = cat.kit (tok.fromFirstOccurrenceOf ("kit:", false, false).isNotEmpty() ? tok.fromFirstOccurrenceOf ("kit:", false, false) : tok))
            {
                for (auto& p : k->pistas)
                    if (auto* g = cat.grupo (p.grupo); g != nullptr && ! g->muestras.empty()) out.add (cat.muestras[(size_t) g->muestras.front()].id);
                continue;
            }
        const auto sinExt = tok.upToLastOccurrenceOf (".", false, false);
        if (tok.containsChar ('/') && cat.muestra (sinExt) != nullptr) { out.add (sinExt); continue; }
        if (tok.containsChar ('/') && cat.instrumento (sinExt) != nullptr) { out.add ("instrumento:" + sinExt); continue; }
        if (tok.containsChar ('/'))
            for (auto& o : cat.ondas())
                if (o.id == tok || o.archivo == tok || o.id == sinExt) { out.add ("onda:" + o.id); break; }
    }
    return out;
}

// Hamster: only the pads whose note the channel's clips play (with the kit's own note map)
void hamster (const XmlElement& x, const std::set<int>& notas, const String& canal, std::vector<Uso>& usos)
{
    std::map<String, float> prm;
    std::function<void (const XmlElement&)> leer = [&] (const XmlElement& e)
    {
        if (e.hasAttribute ("id") && e.hasAttribute ("value")) prm[e.getStringAttribute ("id")] = (float) e.getDoubleAttribute ("value");
        for (auto* c : e.getChildIterator()) leer (*c);
    };
    leer (x);
    // with "Notas del kit: General MIDI" Hamster rewrites each pad's note itself: the saved note is the one that plays
    const auto& cat = biblio::Catalogo::get();
    std::function<const XmlElement* (const XmlElement&)> samples = [&] (const XmlElement& e) -> const XmlElement*
    {
        if (e.hasTagName ("SAMPLES")) return &e;
        for (auto* c : e.getChildIterator()) if (auto* s = samples (*c)) return s;
        return nullptr;
    };
    const auto* smp = samples (x);
    if (smp == nullptr) return;
    for (int t = 0; t < 16; ++t)
    {
        const auto key = "t" + String (t + 1) + "_note";
        const int nota = prm.count (key) ? roundToInt (prm[key]) : 36 + t;
        if (notas.count (nota) == 0) continue;
        for (auto* s : smp->getChildIterator())
            if (s->getTagName().startsWith ("t" + String (t) + "s") && s->hasAttribute ("lib"))
            {
                const auto lib = s->getStringAttribute ("lib");
                String sonido = s->getStringAttribute ("name");
                if (lib.startsWith ("receta:"))
                    if (auto* r = cat.receta (lib.fromFirstOccurrenceOf ("receta:", false, false).upToLastOccurrenceOf ("/", false, false)))
                        sonido << " (Kit Racoon " << r->nombre << ")";
                for (auto& m : cat.origenes (lib)) usos.push_back ({ canal, sonido, m });
            }
    }
}
}

String Session::sha256Of (const File& f) { return certificado::sha256 (f); }

Session::Certificado Session::cleanTrackCertificate() const
{
    Certificado c;
    const auto& cat = biblio::Catalogo::get();
    std::vector<certificado::Uso> usos;
    const auto propio = racoon::library::folder();
    for (auto* t : te::getAudioTracks (*edit))
    {
        if (t->isMuted (true)) continue;
        std::set<int> notas;
        bool suena = false;
        for (auto* clip : t->getClips())
        {
            if (clip->disabled) continue;
            if (auto* mc = dynamic_cast<te::MidiClip*> (clip))
                for (auto* n : mc->getSequence().getNotes()) { notas.insert (n->getNoteNumber()); suena = true; }
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (clip))
            {
                suena = true;
                const auto f = w->getOriginalFile();
                if (cat.disponible() && f.isAChildOf (cat.raiz))
                {
                    const auto rel = f.getRelativePathFrom (cat.raiz).replaceCharacter ('\\', '/');
                    const auto id = rel.upToLastOccurrenceOf (".", false, false);
                    if (cat.muestra (id) != nullptr) { usos.push_back ({ t->getName(), w->getName(), id }); continue; }
                }
                if (f.isAChildOf (propio)) c.propios.addIfNotAlreadyThere (t->getName() + ": " + f.getFileName() + " (Biblioteca Racoon)");
                else c.revisar.addIfNotAlreadyThere (t->getName() + ": " + f.getFileName());
            }
        }
        // a return (the reverb, the delay) sounds when the song does: what the channels send goes through it
        if (kindOf (*t) == TrackKind::returnBus) suena = true;
        if (! suena) continue;
        for (auto* p : t->pluginList)
        {
            if (! p->isEnabled()) continue;
            if (auto* rp = nativeOf (p))
            {
                // Racoon's own effects carry no samples, except the real rooms of Sala real (a recording, with its license)
                if (rp->effectId.endsWith ("sala-real") && rp->apvts.getParameter ("room") != nullptr)
                {
                    const auto nombre = rp->apvts.getParameter ("room")->getCurrentValueAsText();
                    if (auto* sala = cat.sala (nombre))
                    {
                        c.salas.addIfNotAlreadyThere (t->getName() + " | " + sala->nombre + " | " + sala->licencia + " | " + sala->autor + " | " + sala->origen);
                        if (sala->licencia.containsIgnoreCase ("BY"))
                            c.creditos.addIfNotAlreadyThere (String::fromUTF8 ("Sala ") + sala->nombre + ", " + sala->autor + " (" + sala->licencia + ")");
                        c.licencias.addIfNotAlreadyThere (sala->licencia);
                    }
                    else c.propios.addIfNotAlreadyThere (t->getName() + String::fromUTF8 (": Sala real (espacio generado)"));
                }
                continue;
            }
            auto* pi = instanceOf (p);
            if (pi == nullptr) continue;
            if (! Catalog::isRacoonPlugin (*p))
            {
                c.revisar.addIfNotAlreadyThere (t->getName() + ": " + p->getName() + String::fromUTF8 (" (plugin de terceros)"));
                continue;
            }
            const auto animal = Catalog::animalFor (p->getName());
            const auto xml = parseXML (pluginStateXml (*pi));
            if (xml == nullptr) continue;
            const size_t antes = usos.size();
            // Hamster: only its pads that the notes fire (the rest of its state names sounds that do not play)
            if (animal == "Hamster") certificado::hamster (*xml, notas, t->getName(), usos);
            else
            {
                StringArray vals;
                certificado::valores (*xml, {}, vals);
                StringArray vistas;
                for (auto& v : vals)
                    for (auto& m : certificado::referencias (v))
                        if (! vistas.contains (m)) { vistas.add (m); usos.push_back ({ t->getName(), animal, m }); }
            }
            if (usos.size() == antes) c.propios.addIfNotAlreadyThere (t->getName() + ": " + animal + String::fromUTF8 (" (sonido sintetizado)"));
        }
    }
    // one line per sample, the libraries and their licenses
    std::map<String, std::set<String>> porFuente;
    StringArray vistos;
    for (auto& u : usos)
    {
        String fuente, nombre;
        if (u.muestra.startsWith ("instrumento:"))
        {
            if (auto* in = cat.instrumento (u.muestra.fromFirstOccurrenceOf (":", false, false))) { fuente = in->fuente; nombre = in->nombre; }
        }
        else if (u.muestra.startsWith ("onda:"))
        {
            for (auto& o : cat.ondas()) if (o.id == u.muestra.fromFirstOccurrenceOf (":", false, false)) { fuente = o.fuente; nombre = o.nombre; }
        }
        else if (auto* m = cat.muestra (u.muestra)) { fuente = m->fuente; nombre = m->nombre; }
        if (fuente.isEmpty()) continue;
        const auto linea = u.canal + " | " + u.sonido + " | " + nombre + " | " + u.muestra;
        if (vistos.contains (linea)) continue;
        vistos.add (linea);
        c.lineas.add (linea + " | " + fuente);
        porFuente[fuente].insert (u.muestra);
        ++c.muestras;
    }
    for (auto& [id, ms] : porFuente)
    {
        auto it = cat.fuentes.find (id);
        if (it == cat.fuentes.end()) { c.revisar.add (id + String::fromUTF8 (": biblioteca sin licencia registrada")); continue; }
        c.fuentes.add (it->second.nombre + " | " + it->second.autor + " | " + it->second.licencia + " | " + it->second.url + " | " + String ((int) ms.size()));
        if (it->second.licencia.containsIgnoreCase ("BY")) c.creditos.add (it->second.nombre + ", " + it->second.autor + " (" + it->second.licencia + ")");
        c.licencias.addIfNotAlreadyThere (it->second.licencia);
    }
    return c;
}

Result Session::writeCleanTrackCertificate (const File& audio, File* written) const
{
    const auto c = cleanTrackCertificate();
    auto tr = [] (const char* s) { return ::racoon::i18n::tr (String::fromUTF8 (s)); };
    String t;
    t << tr ("CERTIFICADO DE ROLA LIMPIA") << "\n\n";
    t << tr ("Rola") << ": " << getProjectName() << "\n";
    if (audio.existsAsFile()) t << tr ("Archivo") << ": " << audio.getFileName() << "\n";
    else t << tr ("Stems en la carpeta") << ": " << audio.getParentDirectory().getFileName() << "\n";
    t << tr ("Exportada el") << " " << Time::getCurrentTime().toString (true, true, false, true) << " " << tr ("con Racoon") << " " << RACOON_VERSION_STRING << "\n";
    {
        // the file's fingerprint: this certificate goes with this audio and no other
        const auto huella = certificado::sha256 (audio);
        if (huella.isNotEmpty()) t << tr ("Huella del archivo (SHA-256)") << ": " << huella << "\n";
        t << "\n";
    }
    if (c.revisar.isEmpty())
        t << tr ("Todo lo que suena en esta rola sale de muestras con licencia libre o de sonidos sintetizados. Puedes publicarla y cobrar regalías.") << "\n";
    else
        t << tr ("Casi todo está en regla. Revisa la lista del final: son sonidos que Racoon no puede verificar.") << "\n";
    t << c.muestras << " " << tr ("muestras de") << " " << c.fuentes.size() << " " << tr ("bibliotecas") << ". " << tr ("Licencias") << ": "
      << (c.licencias.isEmpty() ? String ("-") : c.licencias.joinIntoString (", ")) << "\n\n";
    t << tr ("CRÉDITOS PARA PEGAR") << "\n" << tr ("(en la descripción de Spotify, YouTube, Bandcamp o SoundCloud; las licencias CC BY lo piden)") << "\n";
    if (c.creditos.isEmpty()) t << tr ("Ninguna muestra de esta rola pide crédito. Puedes darlo por cortesía:") << " " << tr ("Muestras de Muestras Racoon.") << "\n";
    else t << tr ("Muestras") << ": " << c.creditos.joinIntoString ("; ") << ". " << tr ("Vía Muestras Racoon.") << "\n";
    t << "\n" << tr ("BIBLIOTECAS") << "\n";
    for (auto& f : c.fuentes)
    {
        const auto p = StringArray::fromTokens (f, "|", "");
        t << "- " << p[0].trim() << "\n  " << tr ("autor") << ": " << p[1].trim() << "\n  " << tr ("licencia") << ": " << p[2].trim()
          << "\n  " << tr ("origen") << ": " << p[3].trim() << "\n  " << tr ("muestras en esta rola") << ": " << p[4].trim() << "\n";
    }
    t << "\n" << tr ("CADA MUESTRA QUE SUENA") << "\n" << tr ("canal | sonido | muestra | id en Muestras Racoon | biblioteca") << "\n";
    for (auto& l : c.lineas) t << l << "\n";
    if (! c.salas.isEmpty())
    {
        t << "\n" << tr ("SALAS DE REVERB (grabadas por OpenAIR, Universidad de York)") << "\n";
        bool sa = false;
        for (auto& l : c.salas)
        {
            const auto p = StringArray::fromTokens (l, "|", "");
            t << "- " << p[1].trim() << " (" << tr ("en") << " " << p[0].trim() << "): " << p[2].trim() << ", " << p[4].trim() << "\n";
            sa = sa || p[2].contains ("SA");
        }
        if (sa)
            t << tr ("Las salas CC BY-SA piden crédito (ya está en los créditos). Sobre si una rola procesada con ellas hereda esa licencia no hay acuerdo claro; si te preocupa, usa una sala de dominio público.") << "\n";
    }
    if (! c.propios.isEmpty())
    {
        t << "\n" << tr ("SONIDOS SINTETIZADOS (no usan muestras: son tuyos)") << "\n";
        for (auto& l : c.propios) t << "- " << l << "\n";
    }
    if (! c.revisar.isEmpty())
    {
        t << "\n" << tr ("PARA REVISAR (Racoon no sabe de dónde vienen)") << "\n";
        for (auto& l : c.revisar) t << "- " << l << "\n";
    }
    t << "\n" << tr ("Un aviso honesto. Una muestra CC0 puede activar un reclamo de Content ID si alguien más subió una rola con ella a su nombre. Este certificado no lo evita, pero te da con qué disputarlo: la muestra, su licencia y su origen.")
      << "\n" << tr ("Este certificado no es asesoría legal.") << "\n";
    auto out = audio.getSiblingFile (audio.getFileNameWithoutExtension() + " - " + tr ("certificado de rola limpia") + ".txt");
    if (! out.replaceWithText (t, false, false, "\n")) return Result::fail (tr ("No se pudo escribir el certificado."));
    if (written != nullptr) *written = out;
    return Result::ok();
}

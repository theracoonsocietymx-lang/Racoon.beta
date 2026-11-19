#include "Maqueta.h"
#include "I18n.h"
#include <string_view>
#include <functional>

namespace racoon::maqueta
{
using namespace juce;

File inbox()
{
    const auto env = SystemStats::getEnvironmentVariable ("RACOON_MAQUETA_DIR", {});
    File f = env.isNotEmpty() ? File (env)
                              : File::getSpecialLocation (File::userApplicationDataDirectory)
                                   #if JUCE_MAC
                                    .getChildFile ("Application Support")
                                   #endif
                                    .getChildFile ("Racoon").getChildFile ("Maqueta");
    f.createDirectory();
    return f;
}

bool hostIsRacoon()
{
    if (SystemStats::getEnvironmentVariable ("RACOON_HOST_IS_RACOON", {}).isNotEmpty()) return true;
    return File (PluginHostType::getHostPath()).getFileNameWithoutExtension().containsIgnoreCase ("Racoon");
}

File send (const MidiFile& mf, const String& animal, const String& trackName, const String& kind, const String& clip)
{
    const auto dir = inbox();
    const auto stamp = String (Time::currentTimeMillis()) + "-" + String::toHexString (Random::getSystemRandom().nextInt());
    auto mid = dir.getChildFile ("Racoon " + animal + " " + stamp + ".mid");
    {
        FileOutputStream os (mid);
        if (! os.openedOk() || ! mf.writeTo (os)) return {};
    }
    // the note goes last and in one move: when Racoon sees it, the .mid is complete
    DynamicObject::Ptr o (new DynamicObject());
    o->setProperty ("animal", animal);
    o->setProperty ("track", trackName);
    o->setProperty ("kind", kind);
    o->setProperty ("midi", mid.getFileName());
    if (clip.isNotEmpty()) o->setProperty ("clip", clip);
    auto tmp = dir.getChildFile (stamp + ".tmp");
    if (! tmp.replaceWithText (JSON::toString (var (o.get())))) return {};
    if (! tmp.moveFileTo (dir.getChildFile (stamp + ".json"))) return {};
    return mid;
}

// ===================================================================== a clip of the song, opened in the plugin
File editInbox()
{
    auto f = inbox().getChildFile ("Editar");
    f.createDirectory();
    return f;
}

bool requestEdit (const MidiFile& mf, const String& animal, const String& trackName, const String& clip, const String& clipName,
                  double beats, double bpm)
{
    const auto dir = editInbox();
    const auto stamp = String (Time::currentTimeMillis()) + "-" + String::toHexString (Random::getSystemRandom().nextInt());
    auto mid = dir.getChildFile ("Clip " + animal + " " + stamp + ".mid");
    {
        FileOutputStream os (mid);
        if (! os.openedOk() || ! mf.writeTo (os)) return false;
    }
    DynamicObject::Ptr o (new DynamicObject());
    o->setProperty ("animal", animal);
    o->setProperty ("track", trackName);
    o->setProperty ("clip", clip);
    o->setProperty ("name", clipName);
    o->setProperty ("beats", beats);
    o->setProperty ("bpm", bpm);
    o->setProperty ("midi", mid.getFileName());
    auto tmp = dir.getChildFile (stamp + ".tmp");
    return tmp.replaceWithText (JSON::toString (var (o.get()))) && tmp.moveFileTo (dir.getChildFile (stamp + ".json"));
}

std::optional<EditRequest> takeEdit (const String& animal, const String& trackName, bool justOpened)
{
    const auto dir = editInbox();
    auto notes = dir.findChildFiles (File::findFiles, false, "*.json");
    notes.sort();
    std::optional<EditRequest> found;
    for (auto& j : notes)
    {
        const auto v = JSON::parse (j.loadFileAsString());
        const auto mid = dir.getChildFile (v["midi"].toString());
        // a request nobody took in two minutes (the plugin was not there) is thrown away
        if (j.getLastModificationTime() < Time::getCurrentTime() - RelativeTime::minutes (2)) { j.deleteFile(); mid.deleteFile(); continue; }
        if (v["animal"].toString() != animal) continue;
        const bool sameTrack = trackName.isNotEmpty() && v["track"].toString() == trackName;
        const bool fresh = justOpened && j.getLastModificationTime() > Time::getCurrentTime() - RelativeTime::seconds (15);
        if (! sameTrack && ! fresh) continue;
        EditRequest r;
        FileInputStream is (mid);
        if (is.openedOk()) r.midi.readFrom (is);
        r.clip = v["clip"].toString();
        r.clipName = v["name"].toString();
        r.beats = jmax (1.0, (double) v["beats"]);
        r.bpm = jlimit (20.0, 400.0, (double) v.getProperty ("bpm", 120.0));
        found = r;                                  // the newest one wins
        j.deleteFile();
        mid.deleteFile();
    }
    return found;
}

std::vector<EditRequest::Note> EditRequest::notes() const
{
    std::vector<Note> out;
    const double tpq = midi.getTimeFormat() > 0 ? (double) midi.getTimeFormat() : 960.0;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        MidiMessageSequence seq (*midi.getTrack (t));
        seq.updateMatchedPairs();
        for (auto* e : seq)
        {
            if (! e->message.isNoteOn()) continue;
            const double s = e->message.getTimeStamp() / tpq;
            if (s >= beats - 1.0e-6 || s < -1.0e-6) continue;
            const double end = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : s + 0.25;
            out.push_back ({ jmax (0.0, s), jmax (1.0 / 64.0, jmin (end, beats) - s), e->message.getNoteNumber(),
                             jlimit (1, 127, (int) e->message.getVelocity()), jlimit (1, 16, e->message.getChannel()) });
        }
    }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

// ===================================================================== button
Button::Button() : TextButton (String::fromUTF8 ("Pegar en maqueta"))
{
    startTimer (400);
    setTooltip (hostIsRacoon()
                  ? String::fromUTF8 ("Pasa esta secuencia (con su automatización) a la maqueta de la rola, en el canal de este plugin.")
                  : String::fromUTF8 ("Arrastra este botón a una pista de tu DAW para pasar la secuencia (con su automatización) a la maqueta."));
}

void Button::mouseDown (const MouseEvent& e)
{
    dragged = false;
    TextButton::mouseDown (e);
}

void Button::mouseDrag (const MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 8 || ! makeMidi) return;
    dragged = true;
    const auto name = animal ? animal() : String ("Racoon");
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Racoon MIDI");
    dir.createDirectory();
    auto f = dir.getChildFile ("Racoon " + name + ".mid");
    {
        f.deleteFile();
        FileOutputStream os (f);
        if (! os.openedOk() || ! makeMidi().writeTo (os)) return;
    }
    DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void Button::clicked()
{
    if (dragged) { dragged = false; return; }
    if (! makeMidi) return;
    // the request always goes out: if Racoon is open, it puts the clip at the end of the channel (or back in the clip
    // it came from)
    const bool back = binding != nullptr && binding->active();
    const auto f = send (makeMidi(), animal ? animal() : String(), trackName ? trackName() : String(), kind, back ? binding->clip : String());
    if (f == File()) { bubble (String::fromUTF8 ("No se pudo pasar la secuencia.")); return; }
    if (hostIsRacoon())
    {
        if (afterSend) afterSend();
        if (back)
        {
            const auto name = binding->clipName;
            binding->clear();
            refreshText();
            bubble (String::fromUTF8 ("Listo: el clip «") + name + String::fromUTF8 ("» quedó con tus cambios en la maqueta."));
        }
        else
            bubble (String::fromUTF8 ("Listo: la secuencia quedó al final de tu canal en la maqueta."));
    }
    else
        bubble (String::fromUTF8 ("En tu DAW, arrastra este botón a una pista: ahí suelta la secuencia como clip."));
}

void Button::refreshText()
{
    if (normalText.isEmpty()) normalText = getButtonText();
    const bool back = binding != nullptr && binding->active();
    const bool caps = normalText == normalText.toUpperCase();
    String t = back ? String::fromUTF8 ("Devolver al clip") : normalText;
    if (caps) t = t.toUpperCase();
    if (getButtonText() != t) setButtonText (t);
    setTooltip (back ? String::fromUTF8 ("Estás editando el clip «") + binding->clipName
                         + String::fromUTF8 ("» de la maqueta. Un click le devuelve la secuencia con tus cambios.")
                     : hostIsRacoon()
                         ? String::fromUTF8 ("Pasa esta secuencia (con su automatización) a la maqueta de la rola, en el canal de este plugin.")
                         : String::fromUTF8 ("Arrastra este botón a una pista de tu DAW para pasar la secuencia (con su automatización) a la maqueta."));
}

File statusInbox()
{
    auto f = inbox().getChildFile ("Estado");
    f.createDirectory();
    return f;
}

namespace
{
std::size_t hashOf (const MidiFile& mf)
{
    MemoryOutputStream os;
    mf.writeTo (os);
    return std::hash<std::string_view>() (std::string_view (static_cast<const char*> (os.getData()), os.getDataSize()));
}
}

void Button::watchChanges()
{
    const bool bound = binding != nullptr && binding->active() && makeMidi;
    const auto dir = statusInbox();
    if (flaggedClip.isNotEmpty() && (! bound || binding->clip != flaggedClip))
    {
        dir.getChildFile (flaggedClip + ".cambios").deleteFile();
        flaggedClip.clear();
    }
    if (! bound) { haveBaseline = false; return; }
    const auto clip = binding->clip;
    // Racoon's answer when the window closes
    if (auto req = dir.getChildFile (clip + ".devolver"); req.existsAsFile())
    {
        req.deleteFile();
        dir.getChildFile (clip + ".cambios").deleteFile();
        flaggedClip.clear();
        dragged = false;
        clicked();
        haveBaseline = false;
        return;
    }
    if (auto forget = dir.getChildFile (clip + ".olvidar"); forget.existsAsFile())
    {
        forget.deleteFile();
        dir.getChildFile (clip + ".cambios").deleteFile();
        flaggedClip.clear();
        binding->clear();
        refreshText();
        haveBaseline = false;
        return;
    }
    const auto h = hashOf (makeMidi());
    if (! haveBaseline) { baseline = h; haveBaseline = true; }
    const bool changed = h != baseline;
    auto flag = dir.getChildFile (clip + ".cambios");
    if (changed && ! flag.existsAsFile()) { flag.create(); flaggedClip = clip; }
    else if (! changed && flag.existsAsFile()) { flag.deleteFile(); flaggedClip.clear(); }
}

void Button::timerCallback()
{
    watchChanges();
    if (! onEdit || ! animal) return;
    const auto track = trackName ? trackName() : String();
    const bool justOpened = Time::getMillisecondCounter() - createdAt < 10000;
    if (track.isEmpty() && ! justOpened) return;
    if (auto r = takeEdit (animal(), track, justOpened))
    {
        if (binding != nullptr)
        {
            binding->clip = r->clip;
            binding->clipName = r->clipName;
            binding->beats = r->beats;
        }
        onEdit (*r);
        haveBaseline = false;                 // what the clip brought is the starting point
        refreshText();
        bubble (String::fromUTF8 ("Aquí está el clip «") + r->clipName + String::fromUTF8 ("». Edítalo y dale «Devolver al clip»."));
    }
    else if (binding != nullptr && (binding->active() != getButtonText().containsIgnoreCase ("clip")))
        refreshText();
}

Button::~Button()
{
    stopTimer();
    // the window went away: the plugin still holds the clip, but the flag is not watched any more
    if (flaggedClip.isNotEmpty()) statusInbox().getChildFile (flaggedClip + ".cambios").deleteFile();
    tip.reset();
}

void Button::bubble (const String& text)
{
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this) return;
    tip = std::make_unique<BubbleMessageComponent> (300);
    // a light bubble with dark text, whatever the plugin's own look paints bubbles with
    tip->setColour (BubbleComponent::backgroundColourId, Colour (0xfff7f5f2));
    tip->setColour (BubbleComponent::outlineColourId, Colour (0xff1f1a1c));
    top->addChildComponent (*tip);
    AttributedString s;
    s.append (::racoon::i18n::tr (text), FontOptions (15.0f), Colour (0xff1f1a1c));
    tip->showAt (this, s, 3500, true, false);
}
}

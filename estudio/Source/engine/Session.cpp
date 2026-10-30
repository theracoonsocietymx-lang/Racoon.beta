#include "Session.h"
#include "AudioTools.h"
#include <set>
#include "CrashLog.h"
#include <map>
#include "Muestras.h"
#include "Behaviour.h"
#include "fx/NativeFx.h"
#include "Devices.h"
#include "SoundLibrary.h"
#include "I18n.h"
#include "Genres.h"
#include "Tarareo.h"
#include "DJ.h"
#include "Gusto.h"
#include "Arranger.h"

namespace racoon
{
using namespace juce;

namespace
{
const Colour kRosa (0xffe0789a), kLimon (0xffd9c452), kMenta (0xff7fbfa8), kHielo (0xff5cf2e8),
             kLeche (0xfff7f5f2), kGris (0xffb9b1b4), kCiruela (0xff6b6166);

te::Track* lastUserTrack (te::Edit& e)
{
    te::Track* last = nullptr;
    for (auto* t : te::getTopLevelTracks (e))
        if (dynamic_cast<te::AudioTrack*> (t) != nullptr || dynamic_cast<te::FolderTrack*> (t) != nullptr)
            last = t;
    return last;
}

// new tracks go above the return tracks: returns always sit at the bottom, just above the master
te::Track* lastTrackBeforeReturns (te::Edit& e)
{
    te::Track* last = nullptr;
    for (auto* t : te::getTopLevelTracks (e))
    {
        if (dynamic_cast<te::AudioTrack*> (t) == nullptr && dynamic_cast<te::FolderTrack*> (t) == nullptr) continue;
        if (kindOf (*t) == TrackKind::returnBus) return last;
        last = t;
    }
    return last;
}

String stamp() { return Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"); }
}

// ===================================================================== helpers
Colour colourForTrack (te::Track& t)
{
    if (auto c = t.getColour(); c.isOpaque() && c != Colour()) return c;
    switch (kindOf (t))
    {
        case TrackKind::instrument: return kLimon;
        case TrackKind::returnBus:  return kGris;
        case TrackKind::group:      return kCiruela;
        case TrackKind::hamsterOut: return kMenta;
        default:                    return kHielo;
    }
}

String trackTitle (te::Track& t) { return t.getName(); }

String timeToBarsBeats (te::Edit& e, te::TimePosition t)
{
    auto bb = e.tempoSequence.toBarsAndBeats (t);
    const int beat = (int) std::floor (bb.beats.inBeats());
    const int sixteenth = (int) std::floor ((bb.beats.inBeats() - beat) * 4.0);
    return String (bb.bars + 1) + "." + String (beat + 1) + "." + String (sixteenth + 1);
}

// ===================================================================== lifecycle
Session::Session (te::Engine& e, RacoonUIBehaviour* behaviour)
    : engine (e), selection (e), ui (behaviour)
{
    Catalog::registerInstalledRacoonPlugins (engine);
    newProject (false);
    startTimer (60 * 1000);
    muteSoloTimer.startTimerHz (5);
}

Session::~Session()
{
    stopTimer();
    muteSoloTimer.stopTimer();
    if (edit != nullptr)
    {
        edit->getTransport().stop (false, true);
        if (ui != nullptr && ui->focusedEdit == edit.get()) { ui->focusedEdit = nullptr; ui->focusedSelection = nullptr; }
        selection.deselectAll();
        selection.edit = nullptr;
    }
    edit.reset();
}

File Session::appDataFolder()
{
    auto f = File::getSpecialLocation (File::userApplicationDataDirectory)
   #if JUCE_MAC
                 .getChildFile ("Application Support")
   #endif
                 .getChildFile ("Racoon");
    f.createDirectory();
    return f;
}

File Session::projectsFolder()
{
    auto f = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon");
    if (! File::getSpecialLocation (File::userMusicDirectory).isDirectory())
        f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Racoon");
    f.createDirectory();
    return f;
}

File Session::templatesFolder()
{
    auto f = appDataFolder().getChildFile ("Plantillas");
    f.createDirectory();
    return f;
}

File Session::autosaveFileFor (const File& f) const
{
    if (untitled) return f;
    auto dir = appDataFolder().getChildFile ("Autoguardado");
    dir.createDirectory();
    return dir.getChildFile (f.getFileNameWithoutExtension() + "-" + String::toHexString (f.getFullPathName().hashCode64()) + ".racoon");
}

namespace
{
// Tracktion writes a relative audio path from the project file as if it were a folder ("../Mi rola Audio/x.wav")
// but its default reader starts from the folder that holds the file, one level too high: every relative path broke
// after closing and opening. This reader starts where the writer did (and still accepts the other way)
File resolveAudioPath (const File& editFile, const String& path)
{
    if (path.isEmpty()) return {};
    if (File::isAbsolutePath (path)) return File (path);
    const auto a = editFile.getChildFile (path);
    if (a.existsAsFile()) return a;
    const auto b = editFile.getSiblingFile (path);
    return b.existsAsFile() ? b : a;
}

std::unique_ptr<te::Edit> loadEditWithPaths (te::Engine& engine, const File& file)
{
    auto state = te::loadEditFromFile (engine, file, te::ProjectItemID{});
    if (! state.isValid()) return {};
    auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);
    if (! id.isValid()) id = te::ProjectItemID::createNewID (0);
    te::Edit::Options o { engine, state, id };
    o.editFileRetriever = [file] { return file; };
    o.filePathResolver = [file] (const String& p) { return resolveAudioPath (file, p); };
    return te::Edit::createEdit (std::move (o));
}
}

void Session::adopt (std::unique_ptr<te::Edit> newEdit, const File& file, bool isUntitled)
{
    if (edit != nullptr)
    {
        edit->getTransport().stop (false, true);
        selection.deselectAll();
        listeners.call ([] (Listener& l) { l.editAboutToBeReplaced(); });
        selection.edit = nullptr;
    }

    // the old Edit has to go before the new one starts playing to free the audio device
    edit.reset();
    edit = std::move (newEdit);
    currentFile = file;
    untitled = isUntitled;
    edit->editFileRetriever = [this] { return currentFile; };
    edit->filePathResolver = [this] (const String& p) { return resolveAudioPath (currentFile, p); };
    edit->playInStopEnabled = true;
    selection.edit = edit.get();

    if (ui != nullptr)
    {
        ui->focusedEdit = edit.get();
        ui->focusedSelection = &selection;
    }

    sendSynchronousChangeMessage();
}

void Session::prepareNewEdit()
{
    edit->tempoSequence.getTempo (0)->setBpm (120.0);
    edit->getTransport().setLoopRange ({ 0_tp, edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)) });
    edit->clickTrackEmphasiseBars = true;
}

void Session::newProject (bool withTemplate, bool housito, const Genre* genre)
{
    crashlog::note (String ("Proyecto nuevo") + (genre != nullptr ? String (" (") + genre->id + ")" : String()));
    auto dir = appDataFolder().getChildFile ("Sin guardar");
    dir.createDirectory();
    auto file = dir.getChildFile ("Proyecto " + stamp() + ".racoon");

    adopt (te::createEmptyEdit (engine, file), file, true);
    prepareNewEdit();

    if (withTemplate && housito)
        buildHousitoTemplate();
    else if (withTemplate)
        buildBlockTemplate (genre != nullptr ? *genre : defaultGenre());
    else
    {
        for (auto* t : te::getAudioTracks (*edit))
            edit->deleteTrack (t);
        if (auto* t = addAudioTrack ("Audio 1"))
            selection.selectOnly (t);
        snapBeats = 1.0;
    }

    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    structureChanged();
}

bool Session::openProject (const File& file)
{
    crashlog::note ("Abrir " + file.getFullPathName());
    if (! file.existsAsFile())
        return false;

    Catalog::registerInstalledRacoonPlugins (engine);
    auto loaded = loadEditWithPaths (engine, file);
    if (loaded == nullptr)
        return false;

    adopt (std::move (loaded), file, file.isAChildOf (appDataFolder()));
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    structureChanged();
    return true;
}

String Session::getProjectName() const
{
    return untitled ? String::fromUTF8 ("Sin título") : currentFile.getFileNameWithoutExtension();
}

bool Session::hasUnsavedChanges() const { return edit != nullptr && edit->hasChangedSinceSaved(); }

bool Session::save()
{
    crashlog::note ("Guardar " + currentFile.getFileName());
    if (untitled)
        return false;

    edit->flushState();
    if (! te::EditFileOperations (*edit).writeToFile (currentFile, false))
        return false;

    autosaveFileFor (currentFile).deleteFile();
    edit->resetChangedStatus();
    return true;
}

bool Session::saveAs (const File& target)
{
    auto f = target.withFileExtension (".racoon");
    const auto oldFile = currentFile;
    const bool wasUntitled = untitled;

    // where every audio clip's file really is, read while the project still points to its old place: Tracktion keeps
    // the path relative to the project file, so after moving the project the old relative path would lead nowhere
    // (the risers and impacts of a song saved for the first time went missing that way)
    std::vector<std::pair<te::WaveAudioClip*, File>> sources;
    if (edit != nullptr)
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : t->getClips())
                if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c)) sources.push_back ({ ac, ac->getOriginalFile() });
    const auto oldAudio = (untitled ? appDataFolder().getChildFile ("Sin guardar") : currentFile.getParentDirectory()).getChildFile (getProjectName() + " Audio");

    currentFile = f;
    untitled = false;

    // the project's own audio (recordings, bounces, the transitions of Crear la rola) goes with it, next to the new file
    if (oldFile != f && oldAudio.isDirectory())
    {
        const auto newAudio = audioFolder();
        if (newAudio != oldAudio) oldAudio.copyDirectoryTo (newAudio);
        for (auto& [ac, src] : sources)
            if (src.isAChildOf (oldAudio))
                if (auto moved = newAudio.getChildFile (src.getRelativePathFrom (oldAudio)); moved.existsAsFile()) src = moved;
    }
    for (auto& [ac, src] : sources)
        if (src.existsAsFile())
            ac->getSourceFileReference().setToDirectFileReference (src, src.isAChildOf (f.getParentDirectory()));

    if (! save())
    {
        currentFile = oldFile;
        untitled = wasUntitled;
        return false;
    }

    if (wasUntitled && oldFile != f)
        oldFile.deleteFile();

    sendChangeMessage();
    return true;
}

void Session::autosaveNow()
{
    if (edit == nullptr || ! edit->hasChangedSinceSaved())
        return;

    edit->flushState();
    te::EditFileOperations (*edit).writeToFile (autosaveFileFor (currentFile), false);
}

File Session::findRecoveryFile() const
{
    if (untitled) return {};
    auto f = autosaveFileFor (currentFile);
    if (f.existsAsFile() && f.getLastModificationTime() > currentFile.getLastModificationTime())
        return f;
    return {};
}

// ===================================================================== mute and solo
namespace
{
const Identifier kMute { "racoonMute" }, kSolo { "racoonSolo" }, kByTrack { "racoonMutedByTrack" };
}

bool Session::mutesByClips (const te::Track& t)
{
    const int k = kindOf (t);
    return racoon::isAudioTrack (t) && (k == TrackKind::audio || k == TrackKind::instrument);
}

static bool soloByProperty (const te::Track& t) { return Session::mutesByClips (t) || kindOf (t) == TrackKind::group; }

bool Session::clipMutedByTrack (const te::Clip& c) { return (bool) c.state.getProperty (kByTrack, false); }

bool Session::isTrackMuted (const te::Track& t) const { return mutesByClips (t) ? (bool) t.state.getProperty (kMute, false) : t.isMuted (false); }
bool Session::isTrackSoloed (const te::Track& t) const { return soloByProperty (t) ? (bool) t.state.getProperty (kSolo, false) : t.isSolo (false); }

void Session::setTrackMute (te::Track& t, bool on)
{
    if (mutesByClips (t)) t.state.setProperty (kMute, on, &edit->getUndoManager());
    else t.setMute (on);
    applyMuteSolo();
}

void Session::setTrackSolo (te::Track& t, bool on)
{
    if (soloByProperty (t)) t.state.setProperty (kSolo, on, &edit->getUndoManager());
    else t.setSolo (on);
    applyMuteSolo();
}

void Session::applyMuteSolo()
{
    if (edit == nullptr) return;
    auto tracks = te::getAudioTracks (*edit);
    // projects from before: the channel mute and solo of Tracktion become Racoon's
    for (auto* t : te::getAllTracks (*edit))
    {
        if (mutesByClips (*t) && t->isMuted (false)) { t->state.setProperty (kMute, true, nullptr); t->setMute (false); }
        if (soloByProperty (*t) && t->isSolo (false)) { t->state.setProperty (kSolo, true, nullptr); t->setSolo (false); }
    }
    auto soloed = [] (const te::Track& t)
    {
        if ((bool) t.state.getProperty (kSolo, false)) return true;
        for (auto* f = t.getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
            if ((bool) f->state.getProperty (kSolo, false)) return true;
        return false;
    };
    bool anySolo = false;
    for (auto* t : tracks) if (mutesByClips (*t) && soloed (*t)) anySolo = true;
    for (auto* t : tracks)
    {
        if (! mutesByClips (*t)) continue;
        const bool silent = (bool) t->state.getProperty (kMute, false) || (anySolo && ! soloed (*t));
        for (auto* c : t->getClips())
        {
            const bool muted = c->isMuted(), byTrack = clipMutedByTrack (*c);
            // the clip's own mute (the user's) is left alone; only the one the channel put is taken back
            if (silent && ! muted) { c->state.setProperty (te::IDs::mute, true, nullptr); c->state.setProperty (kByTrack, true, nullptr); }
            else if (! silent && byTrack) { c->state.removeProperty (kByTrack, nullptr); c->state.setProperty (te::IDs::mute, false, nullptr); }
        }
    }
}

void Session::timerCallback() { autosaveNow(); }

bool Session::saveAsTemplate (const String& name)
{
    edit->flushState();
    return te::EditFileOperations (*edit).writeToFile (templatesFolder().getChildFile (File::createLegalFileName (name) + ".racoon"), false);
}

StringArray Session::userTemplates() const
{
    StringArray names;
    for (auto& f : templatesFolder().findChildFiles (File::findFiles, false, "*.racoon"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool Session::newFromUserTemplate (const String& name)
{
    auto src = templatesFolder().getChildFile (name + ".racoon");
    if (! src.existsAsFile()) return false;

    Catalog::registerInstalledRacoonPlugins (engine);
    auto dir = appDataFolder().getChildFile ("Sin guardar");
    dir.createDirectory();
    auto file = dir.getChildFile ("Proyecto " + stamp() + ".racoon");
    if (! src.copyFileTo (file)) return false;

    auto loaded = loadEditWithPaths (engine, file);
    if (loaded == nullptr) return false;
    adopt (std::move (loaded), file, true);
    edit->getUndoManager().clearUndoHistory();
    edit->resetChangedStatus();
    structureChanged();
    return true;
}

// ===================================================================== tracks
te::AudioTrack* Session::addAudioTrack (const String& name)
{
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, lastTrackBeforeReturns (*edit)), nullptr);
    if (t == nullptr) return nullptr;

    int n = 0;
    for (auto* a : te::getAudioTracks (*edit)) if (kindOf (*a) == TrackKind::audio) ++n;
    t->setName (name.isNotEmpty() ? name : "Audio " + String (n));
    t->state.setProperty (TrackKind::prop, TrackKind::audio, nullptr);
    t->setColour (kHielo);
    structureChanged();
    return t.get();
}

te::AudioTrack* Session::addInstrumentTrack (const CatalogItem& item)
{
    auto plugin = Catalog::create (*edit, item);
    if (plugin == nullptr)
        return nullptr;

    auto* t = addAudioTrack (item.source == CatalogItem::Source::racoon ? Catalog::titleFor (item.name) : item.name);
    if (t == nullptr) return nullptr;

    t->state.setProperty (TrackKind::prop, TrackKind::instrument, nullptr);
    t->setColour (item.source == CatalogItem::Source::racoon ? Catalog::colourFor (item.name) : kLimon);
    t->pluginList.insertPlugin (plugin, 0, nullptr);
    structureChanged();
    return t;
}

te::AudioTrack* Session::addRacoonInstrument (const String& productName)
{
    for (auto& it : Catalog::racoonItems (engine))
    {
        if (Catalog::animalFor (it.name) != Catalog::animalFor (productName)) continue;   // "Racoon Hamster" is "Racoon Hamster Drums"
        if (it.available)
            return addInstrumentTrack (it);

        // not installed on this computer: keep the song playable with the built-in synth
        for (auto& b : Catalog::builtInItems())
        {
            if (b.builtInType != te::FourOscPlugin::xmlTypeName) continue;
            if (auto* t = addInstrumentTrack (b))
            {
                t->setName (Catalog::animalFor (productName) + String::fromUTF8 (" (sin plugin)"));
                t->setColour (Catalog::colourFor (productName));
                return t;
            }
        }
    }
    return nullptr;
}

te::AudioTrack* Session::addChannelFor (const CatalogItem& it)
{
    if (it.isInstrument) return addInstrumentTrack (it);
    if (it.source != CatalogItem::Source::racoon || ! it.available) return nullptr;

    const auto animal = Catalog::animalFor (it.name);
    te::AudioTrack* t = nullptr;
    if (animal == "Chipmunk" || animal == "Mole")
    {
        // a MIDI tool needs something to play: chords on pads, rhythms on drums
        t = addRacoonInstrument (animal == "Chipmunk" ? "Racoon - Dormouse Pads" : "Racoon - Hamster Drums");
        if (t == nullptr) return nullptr;
    }
    else
    {
        t = addAudioTrack (Catalog::titleFor (it.name));
        if (t == nullptr) return nullptr;
    }
    insertPlugin (*t, it);
    t->setName (Catalog::titleFor (it.name));
    t->setColour (Catalog::colourFor (it.name));
    structureChanged();
    return t;
}

te::Plugin::Ptr Session::insertOnMaster (const CatalogItem& it)
{
    auto p = Catalog::create (*edit, it);
    if (p == nullptr) return {};
    auto& list = edit->getMasterPluginList();
    // before the master volume, so the meter shows what Hedgehog delivers
    int idx = list.size();
    for (int i = 0; i < list.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i]) != nullptr) { idx = i; break; }
    list.insertPlugin (p, idx, nullptr);
    return p;
}

te::AudioTrack* Session::addReturnTrack (int bus, const String& name, const String& fxType)
{
    auto* t = addAudioTrack (name);
    if (t == nullptr) return nullptr;

    t->state.setProperty (TrackKind::prop, TrackKind::returnBus, nullptr);
    t->setColour (kGris);
    if (auto* last = lastUserTrack (*edit); last != nullptr && last != t)
        edit->moveTrack (t, te::TrackInsertPoint (nullptr, last));   // returns go last
    edit->setAuxBusName (bus, name);

    if (auto ret = edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {}))
    {
        ret->state.setProperty (te::IDs::busNum, bus, nullptr);
        t->pluginList.insertPlugin (ret, 0, nullptr);
    }

    if (fxType.isNotEmpty())
    {
        // fxType is the id of one of Racoon's own effects ("reverb", "delay")
        if (auto* def = fx::find (fxType))
            if (auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, fx::describe (*def)))
            {
                t->pluginList.insertPlugin (p, 1, nullptr);
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p.get()))
                    if (auto* rp = dynamic_cast<RacoonProcessor*> (ext->getAudioPluginInstance()))
                    {
                        // a return only carries the effect: 100 % wet
                        if (rp->apvts.getParameter ("mix") != nullptr) rp->setRealValue ("mix", 100.0f);
                        if (rp->apvts.getParameter ("wetonly") != nullptr) rp->setRealValue ("wetonly", 1.0f);
                    }
            }
    }
    structureChanged();
    return t;
}

te::FolderTrack* Session::groupTracks (const Array<te::Track*>& tracks, const String& name)
{
    if (tracks.isEmpty()) return nullptr;

    auto* first = tracks.getFirst();
    auto all = te::getAllTracks (*edit);
    te::Track* preceding = nullptr;
    for (int i = all.indexOf (first) - 1; i >= 0; --i)
        if (all[i]->getParentFolderTrack() == first->getParentFolderTrack()
             && (dynamic_cast<te::AudioTrack*> (all[i]) != nullptr || dynamic_cast<te::FolderTrack*> (all[i]) != nullptr))
        { preceding = all[i]; break; }

    auto folder = edit->insertNewFolderTrack (te::TrackInsertPoint (first->getParentFolderTrack(), preceding), nullptr, true);
    if (folder == nullptr) return nullptr;

    folder->setName (name);
    folder->state.setProperty (TrackKind::prop, TrackKind::group, nullptr);
    folder->setColour (kCiruela);

    te::Track* prev = nullptr;
    for (auto* t : tracks)
    {
        edit->moveTrack (t, te::TrackInsertPoint (folder.get(), prev));
        prev = t;
    }
    structureChanged();
    return folder.get();
}

void Session::deleteTrack (te::Track& t)
{
    selection.deselectAll();
    edit->deleteTrack (&t);
    structureChanged();
}

te::Track* Session::duplicateTrack (te::Track& t)
{
    auto copy = edit->copyTrack (&t, te::TrackInsertPoint (t.getParentFolderTrack(), &t));
    if (copy != nullptr)
        copy->setName (t.getName() + " copia");
    structureChanged();
    return copy.get();
}

void Session::moveTrackTo (te::Track& t, int index)
{
    int idx = 0, i = 0;
    for (auto* x : te::getAllTracks (*edit))
        if (x->getParentFolderTrack() == t.getParentFolderTrack()
             && (dynamic_cast<te::AudioTrack*> (x) != nullptr || dynamic_cast<te::FolderTrack*> (x) != nullptr))
        {
            if (x == &t) idx = i;
            ++i;
        }
    if (index != idx) moveTrack (t, index - idx);
}

Array<te::Plugin*> Session::chainOf (te::Track& t)
{
    Array<te::Plugin*> out;
    for (auto* p : t.pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
             || dynamic_cast<te::AuxSendPlugin*> (p) != nullptr || dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr
             || (dynamic_cast<te::RackInstance*> (p) != nullptr && ! devices::isFxRack (*p)))
            continue;
        out.add (p);
    }
    return out;
}

void Session::movePlugin (te::Track& t, te::Plugin& plugin, int index)
{
    auto chain = chainOf (t);
    const int from = chain.indexOf (&plugin);
    if (from < 0) return;
    te::Plugin::Ptr keep (&plugin);
    chain.remove (from);
    index = jlimit (0, chain.size(), index);

    // keep the order the signal needs: MIDI effects, then the instrument, then audio effects
    int instIdx = -1;
    for (int i = 0; i < chain.size(); ++i) if (isInstrument (*chain[i])) { instIdx = i; break; }
    if (! isInstrument (plugin) && instIdx >= 0)
    {
        if (isMidiEffect (plugin)) index = jmin (index, instIdx);
        else index = jmax (index, instIdx + 1);
    }
    if (isInstrument (plugin))
    {
        int lastMidi = 0;
        for (int i = 0; i < chain.size(); ++i) if (isMidiEffect (*chain[i])) lastMidi = i + 1;
        index = lastMidi;
    }

    // position in the real plugin list: before the device that now sits at `index`, or after the last device
    auto& list = t.pluginList;
    int listIndex;
    if (index < chain.size()) listIndex = list.indexOf (chain[index]);
    else listIndex = chain.isEmpty() ? 0 : list.indexOf (chain.getLast()) + 1;
    if (list.indexOf (&plugin) < listIndex) --listIndex;
    plugin.removeFromParent();
    list.insertPlugin (keep, jmax (0, listIndex), nullptr);
    structureChanged();
}

void Session::moveTrack (te::Track& t, int delta)
{
    Array<te::Track*> siblings;
    for (auto* x : te::getAllTracks (*edit))
        if (x->getParentFolderTrack() == t.getParentFolderTrack()
             && (dynamic_cast<te::AudioTrack*> (x) != nullptr || dynamic_cast<te::FolderTrack*> (x) != nullptr))
            siblings.add (x);

    const int idx = siblings.indexOf (&t);
    if (idx < 0) return;
    const int target = jlimit (0, siblings.size() - 1, idx + delta);
    if (target == idx) return;

    siblings.remove (idx);
    te::Track* preceding = target > 0 ? siblings[target - 1] : nullptr;
    edit->moveTrack (&t, te::TrackInsertPoint (t.getParentFolderTrack(), preceding));
    structureChanged();
}

te::Plugin* Session::instrumentOf (te::AudioTrack& t)
{
    for (auto* p : t.pluginList.getPlugins())
        if (p->isSynth() && dynamic_cast<te::RackInstance*> (p) == nullptr)
            return p;
    for (auto* p : t.pluginList.getPlugins())
        if (dynamic_cast<te::RackInstance*> (p) != nullptr && ! devices::isFxRack (*p))
            return p;
    return nullptr;
}

Array<te::Plugin*> Session::insertsOf (te::Track& t)
{
    Array<te::Plugin*> out;
    for (auto* p : t.pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
             || dynamic_cast<te::AuxSendPlugin*> (p) != nullptr || dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr
             || (dynamic_cast<te::RackInstance*> (p) != nullptr && ! devices::isFxRack (*p)))
            continue;
        if (isInstrument (*p))
            continue;
        out.add (p);
    }
    return out;
}

bool Session::isInstrument (te::Plugin& p)
{
    return p.isSynth() && ! devices::isFxRack (p);
}

bool Session::isMidiEffect (te::Plugin& p)
{
    if (dynamic_cast<te::MidiModifierPlugin*> (&p) != nullptr) return true;
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&p))
    {
        // VST3 has no "MIDI effect" flag: a plugin with no audio at all that sends MIDI counts as one
        // (Chipmunk and Mole are instruments now: they play what they write with their own sound)
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->isMidiEffect() || (pi->producesMidi() && pi->getTotalNumInputChannels() == 0 && pi->getTotalNumOutputChannels() == 0);
    }
    return false;
}

te::Plugin::Ptr Session::insertPlugin (te::Track& track, te::Plugin::Ptr plugin, int index)
{
    if (plugin == nullptr) return {};
    auto* list = &track.pluginList;

    auto* audio = dynamic_cast<te::AudioTrack*> (&track);

    if (isInstrument (*plugin) && audio != nullptr)
    {
        // one instrument per track: a new one replaces the old one
        if (auto* old = instrumentOf (*audio))
            old->deleteFromParent();
        audio->state.setProperty (TrackKind::prop, TrackKind::instrument, nullptr);
        index = 0;
        for (int i = 0; i < list->size(); ++i)
            if (isMidiEffect (*(*list)[i])) index = i + 1;
    }
    else if (isMidiEffect (*plugin) && audio != nullptr)
    {
        // MIDI effects go in front of the instrument (the Chipmunk -> instrument route)
        index = 0;
        if (auto* inst = instrumentOf (*audio)) index = list->indexOf (inst);
    }
    else if (index < 0)
    {
        // effects: before the fader, i.e. pre-volume inserts
        index = list->size();
        for (int i = 0; i < list->size(); ++i)
        {
            auto* p = (*list)[i];
            if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
                 || dynamic_cast<te::AuxSendPlugin*> (p) != nullptr)
            { index = i; break; }
        }
    }

    list->insertPlugin (plugin, index, nullptr);
    structureChanged();
    return plugin;
}

te::Plugin::Ptr Session::insertPlugin (te::AudioTrack& t, const CatalogItem& item, int index)
{
    return insertItem (t, item, index);
}

te::Plugin::Ptr Session::insertItem (te::Track& t, const CatalogItem& item, int index)
{
    if (item.source == CatalogItem::Source::native && devices::isModulatorId (item.builtInType))
    {
        devices::addModulator (t, item.builtInType);
        structureChanged();
        return {};
    }
    auto p = insertPlugin (t, Catalog::create (*edit, item), index);
    // a Sidechain wants to listen to someone: the drums of the song, when there are drums
    if (p != nullptr && p->canSidechain() && p->getSidechainSourceID() == te::EditItemID())
        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p.get()))
            if (auto* rp = dynamic_cast<RacoonProcessor*> (ext->getAudioPluginInstance()); rp != nullptr && rp->effectId.endsWith ("sidechain"))
                if (auto* kick = guessKickTrack (t))
                    setSidechain (*p, kick);
    return p;
}

te::AudioTrack* Session::guessKickTrack (te::Track& except) const
{
    te::AudioTrack* byName = nullptr;
    for (auto* at : te::getAudioTracks (*edit))
    {
        if (at == &except || kindOf (*at) == TrackKind::returnBus) continue;
        const auto name = at->getName().toLowerCase();
        if (name.contains ("bombo") || name.contains ("kick")) return at;
        if (auto* inst = instrumentOf (*at))
            if (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole")) { if (byName == nullptr) byName = at; continue; }
        if (byName == nullptr && (name.contains ("bater") || name.contains ("drum") || name.contains ("groove"))) byName = at;
    }
    return byName;
}

te::AuxSendPlugin* Session::getSend (te::AudioTrack& t, int bus, bool create)
{
    for (auto* s : t.pluginList.getPluginsOfType<te::AuxSendPlugin>())
        if (s->getBusNumber() == bus)
            return s;

    if (! create) return nullptr;

    auto p = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    if (p == nullptr) return nullptr;
    p->state.setProperty (te::IDs::busNum, bus, nullptr);

    // post-fader: right after the volume plugin
    int index = t.pluginList.size();
    if (auto vol = t.getVolumePlugin())
        index = t.pluginList.indexOf (vol) + 1;
    t.pluginList.insertPlugin (p, index, nullptr);

    auto* send = dynamic_cast<te::AuxSendPlugin*> (p.get());
    if (send != nullptr) send->setGainDb (-100.0f);
    return send;
}

StringArray Session::returnBusNames() const
{
    StringArray s;
    for (int i = 0; i < 4; ++i)
        s.add (edit->getAuxBusName (i).isNotEmpty() ? edit->getAuxBusName (i) : String::charToString ((juce_wchar) ('A' + i)));
    return s;
}

bool Session::setSidechain (te::Plugin& p, te::Track* source)
{
    if (! p.canSidechain())
        return false;
    p.setSidechainSourceID (source != nullptr ? source->itemID : te::EditItemID());
    if (source != nullptr)
        p.guessSidechainRouting();
    edit->restartPlayback();
    return true;
}

// ===================================================================== clips
te::MidiClip* Session::createMidiClip (te::AudioTrack& t, te::BeatRange beats, const String& name)
{
    auto range = edit->tempoSequence.toTime (beats);
    auto c = t.insertMIDIClip (name.isNotEmpty() ? name : t.getName(), range, nullptr);
    return c.get();
}

te::MidiClip* Session::addMidiFile (te::AudioTrack& t, const File& file, te::TimePosition start)
{
    FileInputStream is (file);
    MidiFile mf;
    if (! is.openedOk() || ! mf.readFrom (is)) return nullptr;
    const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
    MidiMessageSequence all;
    for (int i = 0; i < mf.getNumTracks(); ++i) all.addSequence (*mf.getTrack (i), 0.0);
    all.updateMatchedPairs();
    double endBeat = 0.0;
    for (auto* e : all) endBeat = jmax (endBeat, e->message.getTimeStamp() / tpq);
    const double length = jmax (4.0, std::ceil (endBeat / 4.0 - 1.0e-6) * 4.0);
    const double startBeat = edit->tempoSequence.toBeats (start).inBeats();
    auto* c = createMidiClip (t, { te::BeatPosition::fromBeats (startBeat), te::BeatPosition::fromBeats (startBeat + length) },
                              file.getFileNameWithoutExtension());
    if (c == nullptr) return nullptr;
    auto& seq = c->getSequence();
    auto* um = &edit->getUndoManager();
    for (int i = 0; i < all.getNumEvents(); ++i)
    {
        const auto& m = all.getEventPointer (i)->message;
        const double b = m.getTimeStamp() / tpq;
        if (b >= length) continue;
        if (m.isNoteOn())
        {
            const double off = all.getTimeOfMatchingKeyUp (i) / tpq;
            const double len = off > b ? off - b : 0.25;
            seq.addNote (m.getNoteNumber(), te::BeatPosition::fromBeats (b), te::BeatDuration::fromBeats (jmin (len, length - b)),
                         m.getVelocity(), 0, um);
        }
        else if (m.isController())
            seq.addControllerEvent (te::BeatPosition::fromBeats (b), m.getControllerNumber(), m.getControllerValue() << 7, um);
    }
    return c;
}

te::AudioTrack* Session::trackForMidiFile (const File& file)
{
    const auto name = file.getFileNameWithoutExtension();
    if (name.startsWith ("Racoon"))
    {
        const auto animal = name.fromFirstOccurrenceOf ("Racoon", false, false).trimStart().trimCharactersAtStart ("- ")
                                .upToFirstOccurrenceOf (" ", false, false);
        for (auto& it : Catalog::racoonItems (engine))
            if (it.available && Catalog::animalFor (it.name) == animal && ! Catalog::isMasterProduct (it))
                if (auto* t = addChannelFor (it)) return t;
    }
    return addAudioTrack (name);
}

te::WaveAudioClip* Session::addAudioFile (te::AudioTrack& t, const File& file, te::TimePosition start)
{
    te::AudioFile af (engine, file);
    if (! af.isValid())
        return nullptr;

    auto c = t.insertWaveClip (file.getFileNameWithoutExtension(), file,
                               { { start, start + te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
    return c.get();
}

te::Clip* Session::duplicateClip (te::Clip& c)
{
    auto n = te::duplicateClip (c);
    if (n == nullptr) return nullptr;
    n->setStart (c.getPosition().getEnd(), false, true);
    // the copy takes the place right after the original: what was there goes
    if (auto* t = n->getClipTrack()) clearUnder (*t, n->getEditTimeRange(), { &c, n.get() });
    return n.get();
}

te::Clip* Session::splitClip (te::Clip& c, te::TimePosition t)
{
    if (! c.getEditTimeRange().contains (t)) return nullptr;
    if (auto* track = c.getClipTrack())
        return track->splitClip (c, t);
    return nullptr;
}

void Session::deleteClip (te::Clip& c)
{
    selection.deselect (&c);
    c.removeFromParent();
}

bool Session::moveClipToTrack (te::Clip& c, te::AudioTrack& t)
{
    if (c.getClipTrack() == &t) return true;
    return c.moveTo (t);
}

void Session::quantise (te::MidiClip& clip, double grid, float strength)
{
    if (grid <= 0.0) return;
    auto* um = &edit->getUndoManager();
    const double contentStart = edit->tempoSequence.toBeats (clip.getTimeOfContentBeat (te::BeatPosition())).inBeats();

    for (auto* n : clip.getSequence().getNotes())
    {
        const double editBeat = contentStart + n->getStartBeat().inBeats();
        const double target = std::round (editBeat / grid) * grid;
        const double moved = editBeat + (target - editBeat) * (double) strength;
        n->setStartAndLength (te::BeatPosition::fromBeats (moved - contentStart), n->getLengthBeats(), um);
    }
}

// ===================================================================== transport
void Session::togglePlay()
{
    auto& tr = getTransport();
    if (tr.isPlaying()) tr.stop (false, false);
    else tr.play (false);
}

void Session::stop()
{
    auto& tr = getTransport();
    if (! tr.isPlaying()) tr.setPosition (0_tp);
    tr.stop (false, false);
}

void Session::toggleRecord()
{
    auto& tr = getTransport();
    if (tr.isRecording()) tr.stop (false, false);
    else tr.record (false);
}

void Session::returnToStart()    { getTransport().setPosition (0_tp); }
void Session::setLooping (bool b) { getTransport().looping = b; }
void Session::setLoopRange (te::TimeRange r) { getTransport().setLoopRange (r); }

double Session::getBpm() const { return edit->tempoSequence.getTempo (0)->getBpm(); }
void Session::setBpm (double b) { edit->tempoSequence.getTempo (0)->setBpm (jlimit (40.0, 250.0, b)); }
int Session::getTimeSigNumerator() const { return edit->tempoSequence.getTimeSig (0)->numerator; }
void Session::setTimeSigNumerator (int n) { edit->tempoSequence.getTimeSig (0)->numerator = jlimit (1, 16, n); }
bool Session::isMetronomeOn() const { return edit->clickTrackEnabled; }
void Session::setMetronome (bool b) { edit->clickTrackEnabled = b; }

te::TimeDuration Session::barLength() const
{
    return edit->tempoSequence.toTime (te::BeatPosition::fromBeats ((double) getTimeSigNumerator())) - 0_tp;
}

// ===================================================================== recording
StringArray Session::audioInputNames()
{
    StringArray names;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i))
            names.add (w->getAlias());
    return names;
}

bool Session::isArmed (te::AudioTrack& t)
{
    for (auto* inst : edit->getAllInputDevices())
        if (te::isOnTargetTrack (*inst, t, 0) && inst->isRecordingEnabled (t.itemID))
            return true;
    return false;
}

String Session::inputOf (te::AudioTrack& t)
{
    for (auto* inst : edit->getAllInputDevices())
        if (te::isOnTargetTrack (*inst, t, 0))
            return inst->getInputDevice().getAlias();
    return {};
}

void Session::setArmed (te::AudioTrack& t, bool armed, const String& audioInput)
{
    auto& dm = engine.getDeviceManager();
    auto* um = &edit->getUndoManager();
    const bool midi = kindOf (t) == TrackKind::instrument;

    if (! armed)
    {
        for (auto* inst : edit->getAllInputDevices())
        {
            if (te::isOnTargetTrack (*inst, t, 0))
            {
                inst->setRecordingEnabled (t.itemID, false);
                [[maybe_unused]] auto r = inst->removeTarget (t.itemID, um);
            }
        }
        return;
    }

    if (midi)
        for (auto& m : dm.getMidiInDevices())
        {
            m->setEnabled (true);
            m->setMonitorMode (te::InputDevice::MonitorMode::automatic);
        }
    else
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* w = dm.getWaveInDevice (i))
            {
                w->setEnabled (true);
                w->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            }

    edit->getTransport().ensureContextAllocated();

    bool assigned = false;
    for (auto* inst : edit->getAllInputDevices())
    {
        auto& dev = inst->getInputDevice();
        const bool wanted = midi ? dev.getDeviceType() == te::InputDevice::physicalMidiDevice
                                 : (dev.getDeviceType() == te::InputDevice::waveDevice
                                    && (audioInput.isEmpty() ? ! assigned : dev.getAlias() == audioInput));
        if (! wanted) continue;

        [[maybe_unused]] auto res = inst->setTarget (t.itemID, ! midi, um, 0);
        inst->setRecordingEnabled (t.itemID, true);
        assigned = true;
    }
    edit->restartPlayback();
}

// ===================================================================== render
te::TimeRange Session::exportRange (const ExportOptions& o) const
{
    if (o.loopOnly)
        return getTransport().getLoopRange();
    return { 0_tp, te::TimePosition() + edit->getLength() + te::TimeDuration::fromSeconds (o.tailSeconds) };
}

Result Session::renderTracks (const BigInteger& tracks, const File& file, const ExportOptions& o, te::TimeRange range)
{
    if (range.isEmpty())
        return Result::fail (String::fromUTF8 ("No hay nada que exportar: el proyecto está vacío."));

    te::Renderer::Parameters p (*edit);
    p.destFile = file;
    auto& fm = engine.getAudioFileFormatManager();
    p.audioFormat = o.format == 1 ? fm.getFlacFormat() : fm.getWavFormat();
    p.bitDepth = o.format == 1 ? jmin (24, o.bitDepth) : o.bitDepth;
    p.sampleRateForAudio = o.sampleRate;
    p.blockSizeForAudio = 512;
    p.time = range;
    p.tracksToDo = tracks;
    p.usePlugins = true;
    p.useMasterPlugins = ! o.stems;
    p.shouldNormalise = o.normalise;
    p.normaliseToLevelDb = -0.3f;
    p.ditheringEnabled = o.dither && p.bitDepth == 16;
    p.canRenderInMono = false;
    p.checkNodesForAudio = false;

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    getTransport().stop (false, true);
    getTransport().freePlaybackContext();
    auto out = te::Renderer::renderToFile ("Exportando " + file.getFileName(), p);
    if (out.existsAsFile())
        return Result::ok();
    return Result::fail ("No se pudo escribir " + file.getFullPathName());
}

Result Session::exportMix (const ExportOptions& o)
{
    crashlog::note ("Exportar mezcla a " + o.destination.getFileName());
    return renderTracks (te::toBitSet (te::getAllTracks (*edit)), o.destination, o, exportRange (o));
}

Result Session::exportStems (const ExportOptions& o, Array<File>* written)
{
    auto dir = o.destination;
    dir.createDirectory();
    const auto ext = o.format == 1 ? String (".flac") : String (".wav");
    auto all = te::getAllTracks (*edit);
    auto range = exportRange (o);
    int n = 0;

    // reverb / delay returns are printed into the stem of every track that feeds them,
    // so the stems still add up to the mix and no return stem comes out empty
    auto addWithParents = [&] (BigInteger& bits, te::Track* t)
    {
        bits.setBit (all.indexOf (t));
        for (auto* f = t->getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
            bits.setBit (all.indexOf (f));
    };
    auto returnFor = [&] (int bus) -> te::Track*
    {
        for (auto* x : all)
            if (auto* xa = dynamic_cast<te::AudioTrack*> (x))
                for (auto* rp : xa->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
                    if (rp->busNumber == bus) return x;
        return nullptr;
    };

    for (auto* t : all)
    {
        auto* at = dynamic_cast<te::AudioTrack*> (t);
        if (at == nullptr || isTrackMuted (*at) || kindOf (*at) == TrackKind::returnBus)
            continue;
        if (at->getClips().isEmpty() && kindOf (*at) != TrackKind::hamsterOut)
            continue;

        BigInteger bits;
        addWithParents (bits, t);
        for (auto* send : at->pluginList.getPluginsOfType<te::AuxSendPlugin>())
            if (send->getGainDb() > -90.0f)
                if (auto* ret = returnFor (send->getBusNumber()))
                    addWithParents (bits, ret);

        // an output of Hamster needs the drum track that plays the rack, with its main output closed
        Array<te::RackInstance*> silenced;
        if (kindOf (*at) == TrackKind::hamsterOut)
            for (auto* mine : at->pluginList.getPluginsOfType<te::RackInstance>())
                for (auto* other : all)
                    if (auto* oa = dynamic_cast<te::AudioTrack*> (other); oa != nullptr && oa != at && kindOf (*oa) == TrackKind::instrument)
                        for (auto* ri : oa->pluginList.getPluginsOfType<te::RackInstance>())
                            if (ri->type == mine->type)
                            {
                                addWithParents (bits, other);
                                silenced.add (ri);
                            }

        Array<int> oldPins;
        for (auto* ri : silenced)
        {
            oldPins.add (ri->leftOutputComesFrom.get());
            oldPins.add (ri->rightOutputComesFrom.get());
            ri->leftOutputComesFrom = -1;
            ri->rightOutputComesFrom = -1;
        }

        auto name = String (++n).paddedLeft ('0', 2) + " " + File::createLegalFileName (at->getName());
        auto file = dir.getChildFile (name + ext);
        auto r = renderTracks (bits, file, o, range);
        for (int i = 0; i < silenced.size(); ++i)
        {
            silenced[i]->leftOutputComesFrom = oldPins[i * 2];
            silenced[i]->rightOutputComesFrom = oldPins[i * 2 + 1];
        }
        if (r.failed()) return r;
        if (written != nullptr) written->add (file);
    }

    if (n == 0)
        return Result::fail ("No hay pistas con contenido para exportar.");
    return Result::ok();
}

bool Session::setFrozen (te::AudioTrack& t, bool frozen)
{
    t.setFrozen (frozen, te::Track::individualFreeze);
    return t.isFrozen (te::Track::individualFreeze) == frozen;
}

File Session::audioFolder() const
{
    auto dir = (untitled ? appDataFolder().getChildFile ("Sin guardar") : currentFile.getParentDirectory())
                   .getChildFile (getProjectName() + " Audio");
    dir.createDirectory();
    return dir;
}

StringArray Session::healthReport() const
{
    StringArray out;
    if (edit == nullptr) return out;
    std::map<String, int> missing;
    for (auto* t : te::getAudioTracks (*edit))
    {
        for (auto* c : t->getClips())
        {
            auto* ac = dynamic_cast<te::WaveAudioClip*> (c);
            if (ac == nullptr) continue;
            const auto f = ac->getOriginalFile();
            if (! f.existsAsFile())
            {
                ++missing[f.getFileName().isNotEmpty() ? f.getFileName() : ac->getName()];
                continue;
            }
            const te::AudioFile af (engine, f);
            const double len = af.getLength();
            const auto pos = ac->getPosition();
            if (len > 0.0 && ! ac->isLooping() && ! ac->getAutoTempo() && std::abs (ac->getSpeedRatio() - 1.0) < 1.0e-6
                && pos.getOffset().inSeconds() + pos.getLength().inSeconds() > len + 0.05)
                out.add (String::fromUTF8 ("El clip «") + ac->getName() + String::fromUTF8 ("» en ") + t->getName() + String::fromUTF8 (" dura más que su audio: el final queda en silencio."));
        }
        for (auto* p : t->pluginList.getPlugins())
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && ext->getAudioPluginInstance() == nullptr)
                out.add (String::fromUTF8 ("No cargó el plugin «") + ext->getName() + String::fromUTF8 ("» en ") + t->getName()
                         + String::fromUTF8 (": no está instalado en esta computadora o falló al abrir."));
    }
    for (auto& [name, n] : missing)
        out.insert (0, String::fromUTF8 ("Falta el audio «") + name + "»"_u + (n > 1 ? " (" + String (n) + " clips)" : String())
                           + String::fromUTF8 (": se movió o se borró."));
    return out;
}

int Session::countMissingAudio() const
{
    int n = 0;
    if (edit != nullptr)
        for (auto* t : te::getAudioTracks (*edit))
            for (auto* c : t->getClips())
                if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! ac->getOriginalFile().existsAsFile()) ++n;
    return n;
}

int Session::relinkMissingAudio (const File& folder)
{
    if (edit == nullptr || ! folder.isDirectory()) return 0;
    std::map<String, File> found;
    for (const auto& entry : RangedDirectoryIterator (folder, true, "*", File::findFiles))
        found.emplace (entry.getFile().getFileName().toLowerCase(), entry.getFile());
    int fixed = 0;
    edit->getUndoManager().beginNewTransaction();
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! ac->getOriginalFile().existsAsFile())
                if (auto it = found.find (ac->getOriginalFile().getFileName().toLowerCase()); it != found.end())
                {
                    ac->getSourceFileReference().setToDirectFileReference (it->second, false);
                    ++fixed;
                }
    return fixed;
}

std::vector<Session::Wanted> Session::missingAudio() const
{
    std::vector<Wanted> out;
    std::set<String> seen;
    if (edit == nullptr) return out;
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! ac->getOriginalFile().existsAsFile())
            {
                const auto f = ac->getOriginalFile();
                if (f.getFileName().isEmpty() || ! seen.insert (f.getFileName().toLowerCase()).second) continue;
                out.push_back ({ f.getFileName(), f.getParentDirectory().getFileName() });
            }
    return out;
}

Array<File> Session::audioSearchRoots() const
{
    // nearest first: what is found in a closer place wins
    Array<File> roots;
    auto add = [&roots] (const File& f)
    {
        if (! f.isDirectory()) return;
        for (auto& r : roots) if (f == r || f.isAChildOf (r)) return;
        roots.add (f);
    };
    if (! untitled) add (currentFile.getParentDirectory());
    add (appDataFolder());
    const auto music = File::getSpecialLocation (File::userMusicDirectory);
    const auto docs = File::getSpecialLocation (File::userDocumentsDirectory);
    add (music.getChildFile ("Racoon"));
    add (docs.getChildFile ("Racoon"));
    add (File::getSpecialLocation (File::userDesktopDirectory));
    add (File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"));
    add (File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Descargas"));
    add (music);
    add (docs);
    return roots;
}

std::map<String, File> Session::searchFiles (const std::vector<Wanted>& wanted, const Array<File>& roots,
                                             std::function<bool()> shouldStop, int* filesLooked)
{
    std::map<String, String> parentOf;
    for (auto& w : wanted) parentOf[w.name.toLowerCase()] = w.parent.toLowerCase();
    std::map<String, File> found;
    int looked = 0;
    for (auto& root : roots)
    {
        for (const auto& entry : RangedDirectoryIterator (root, true, "*", File::findFiles, File::FollowSymlinks::no))
        {
            if ((++looked & 255) == 0 && shouldStop && shouldStop()) break;
            if (looked > 600000) break;                       // a whole disk is not the place to look
            const auto f = entry.getFile();
            const auto key = f.getFileName().toLowerCase();
            auto it = parentOf.find (key);
            if (it == parentOf.end()) continue;
            // two files with the same name: the one in a folder with the same name as before wins
            auto have = found.find (key);
            const bool sameParent = f.getParentDirectory().getFileName().toLowerCase() == it->second;
            if (have == found.end() || (sameParent && have->second.getParentDirectory().getFileName().toLowerCase() != it->second))
                found[key] = f;
        }
        if (found.size() == parentOf.size() || (shouldStop && shouldStop())) break;
    }
    if (filesLooked != nullptr) *filesLooked = looked;
    return found;
}

int Session::relinkFound (const std::map<String, File>& byName)
{
    if (edit == nullptr || byName.empty()) return 0;
    int fixed = 0;
    edit->getUndoManager().beginNewTransaction();
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! ac->getOriginalFile().existsAsFile())
                if (auto it = byName.find (ac->getOriginalFile().getFileName().toLowerCase()); it != byName.end() && it->second.existsAsFile())
                {
                    ac->getSourceFileReference().setToDirectFileReference (it->second, false);
                    ++fixed;
                }
    return fixed;
}

te::AudioTrack* Session::bounceToAudio (te::AudioTrack& t)
{
    auto all = te::getAllTracks (*edit);
    BigInteger bits;
    bits.setBit (all.indexOf (&t));

    auto dir = (untitled ? appDataFolder().getChildFile ("Sin guardar") : currentFile.getParentDirectory())
                   .getChildFile (getProjectName() + " Audio");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile (File::createLegalFileName (t.getName()) + " bounce", ".wav", false);

    ExportOptions o;
    o.sampleRate = engine.getDeviceManager().getSampleRate() > 0 ? engine.getDeviceManager().getSampleRate() : 48000.0;
    o.stems = true;
    o.tailSeconds = 1.0;
    if (renderTracks (bits, file, o, exportRange (o)).failed())
        return nullptr;

    auto* audio = addAudioTrack (t.getName() + " (audio)");
    if (audio == nullptr) return nullptr;
    edit->moveTrack (audio, te::TrackInsertPoint (t.getParentFolderTrack(), &t));
    addAudioFile (*audio, file, 0_tp);
    setTrackMute (t, true);
    structureChanged();
    return audio;
}

Result Session::consolidate (te::AudioTrack& t, te::TimeRange range)
{
    if (range.isEmpty()) return Result::fail ("Selecciona un rango.");
    auto all = te::getAllTracks (*edit);
    BigInteger bits;
    bits.setBit (all.indexOf (&t));

    auto dir = (untitled ? appDataFolder().getChildFile ("Sin guardar") : currentFile.getParentDirectory())
                   .getChildFile (getProjectName() + " Audio");
    auto file = dir.getNonexistentChildFile (File::createLegalFileName (t.getName()) + " consolidado", ".wav", false);

    // clip content only, without the track's plugins
    te::Renderer::Parameters p (*edit);
    p.destFile = file;
    p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24;
    p.sampleRateForAudio = engine.getDeviceManager().getSampleRate() > 0 ? engine.getDeviceManager().getSampleRate() : 48000.0;
    p.time = range;
    p.tracksToDo = bits;
    p.usePlugins = false;
    p.canRenderInMono = false;
    p.checkNodesForAudio = false;
    dir.createDirectory();
    getTransport().stop (false, true);
    getTransport().freePlaybackContext();
    if (! te::Renderer::renderToFile ("Consolidando", p).existsAsFile())
        return Result::fail ("No se pudo consolidar.");

    for (auto* c : t.getClips())
        if (c->getEditTimeRange().overlaps (range))
            te::deleteRegionOfClip (*c, range);

    t.insertWaveClip (t.getName(), file, { range, {} }, false);
    return Result::ok();
}

// ===================================================================== Racoon family
int Session::splitHamsterOutputs (te::AudioTrack& t, int numOutputs)
{
    auto* inst = instrumentOf (t);
    if (inst == nullptr || ! Catalog::isRacoonPlugin (*inst, "Racoon Hamster"))
        return 0;

    te::Plugin::Ptr hamster (inst);
    const int index = t.pluginList.indexOf (inst);
    hamster->removeFromParent();

    te::Plugin::Array wrapped;
    wrapped.add (hamster);
    auto rack = te::RackType::createTypeToWrapPlugins (wrapped, *edit);
    if (rack == nullptr) return 0;
    rack->rackName = "Hamster";

    auto outs = rack->getOutputNames();
    numOutputs = jlimit (0, (outs.size() - 2) / 2, numOutputs);

    // the drum track keeps the main stereo output
    if (auto mainInst = edit->getPluginCache().createNewPlugin (te::RackInstance::create (*rack)))
    {
        t.pluginList.insertPlugin (mainInst, jmax (0, index), nullptr);
        if (auto* ri = dynamic_cast<te::RackInstance*> (mainInst.get()))
        {
            ri->leftOutputComesFrom = 1;
            ri->rightOutputComesFrom = 2;
        }
    }

    // route kit tracks 1..n of Hamster to its outputs 1..n (Hamster parameter "Tn Output")
    // and read each kit track's role to name the new tracks
    StringArray roles;
    for (int k = 0; k < numOutputs; ++k) roles.add ({});
    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (hamster.get()))
        if (auto* pi = ext->getAudioPluginInstance())
            for (auto* prm : pi->getParameters())
                for (int k = 0; k < numOutputs; ++k)
                {
                    const auto n = prm->getName (64);
                    if (n == "T" + String (k + 1) + " Output")
                        prm->setValueNotifyingHost ((float) (k + 1) / 16.0f);
                    else if (n == "T" + String (k + 1) + " Role")
                        roles.set (k, prm->getCurrentValueAsText());
                }

    auto spanishRole = [] (const String& r) -> String
    {
        static const char* map[][2] { { "Kick", "Bombo" }, { "Snare", "Caja" }, { "Clap", "Palmas" }, { "Closed Hat", "Charles cerrado" },
                                      { "Open Hat", "Charles abierto" }, { "Ride", "Ride" }, { "Crash", "Crash" }, { "Rim", "Aro" },
                                      { "Tom Low", "Tom grave" }, { "Tom Mid", "Tom medio" }, { "Tom High", "Tom agudo" },
                                      { "Shaker", "Shaker" }, { "Conga", "Conga" }, { "Bongo", "Bongo" }, { "Cowbell", "Cencerro" },
                                      { "Metal Perc", "Metal" }, { "Custom", "Libre" } };
        for (auto& m : map) if (r == m[0]) return m[1];
        return r;
    };

    te::Track* prev = &t;
    for (int k = 0; k < numOutputs; ++k)
    {
        auto label = "Hamster " + String (k + 1);
        if (roles[k].isNotEmpty()) label << String::fromUTF8 (" · ") << spanishRole (roles[k]);
        auto* out = addAudioTrack (label);
        if (out == nullptr) break;
        out->state.setProperty (TrackKind::prop, TrackKind::hamsterOut, nullptr);
        out->setColour (kMenta.withMultipliedBrightness (0.95f - 0.03f * (float) k));
        edit->moveTrack (out, te::TrackInsertPoint (t.getParentFolderTrack(), prev));
        prev = out;

        if (auto p = edit->getPluginCache().createNewPlugin (te::RackInstance::create (*rack)))
        {
            out->pluginList.insertPlugin (p, 0, nullptr);
            if (auto* ri = dynamic_cast<te::RackInstance*> (p.get()))
            {
                ri->leftInputGoesTo = -1;
                ri->rightInputGoesTo = -1;
                ri->leftOutputComesFrom = 3 + 2 * k;
                ri->rightOutputComesFrom = 4 + 2 * k;
            }
        }
    }
    edit->restartPlayback();
    structureChanged();
    return numOutputs;
}

// ===================================================================== the example block
// What Racoon opens with: one block of a song (4 phrases of 4 bars, 16 bars, 64 beats) in the genre you chose.
// Every channel has its part: drums (Hamster), bass (Badger), pads (Dormouse), chords (Chipmunk), melody (Squirrel),
// guitar (Ferret), choir (Porcupine), percussion (Mole), a loop through Skunk and a voice through Chinchilla.
// Phrase 1 presents, phrase 2 adds chords and melody, phrase 3 is the break (no kick), phrase 4 has everything.
#include "Variedad.inc"
#include "Mezcla.inc"
#include "Certificado.inc"
#include "TarareoSession.inc"
#include "DjSession.inc"

void Session::buildBlockTemplate (const Genre& g)
{
    setBpm (g.bpm);
    snapBeats = 0.25;
    for (auto* t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);
    edit->state.setProperty ("racoonGenre", String (g.id), nullptr);
    edit->state.setProperty ("racoonKey", keyName (g.key, g.major) + " (" + camelot (g.key, g.major) + ")", nullptr);
    edit->state.setProperty ("racoonKeyRoot", g.key, nullptr);
    edit->state.setProperty ("racoonKeyMajor", g.major, nullptr);

    constexpr double bpb = 4.0, bars = 16.0, beats = bars * bpb;
    auto* um = &edit->getUndoManager();
    auto bp = [] (double b) { return te::BeatPosition::fromBeats (b); };
    auto bd = [] (double b) { return te::BeatDuration::fromBeats (b); };
    auto phraseOf = [] (int bar) { return bar / 4; };          // 0..3
    auto setVolume = [] (te::AudioTrack* t, float db) { if (t != nullptr) if (auto* v = t->getVolumePlugin()) v->setVolumeDb (db); };
    auto itemFor = [this] (const char* animal) -> CatalogItem
    {
        for (auto& it : Catalog::racoonItems (engine)) if (Catalog::animalFor (it.name) == animal) return it;
        return {};
    };
    auto midiTrack = [&] (const char* product, const String& name) -> te::AudioTrack*
    {
        auto* t = addRacoonInstrument (product);
        if (t != nullptr) t->setName (name);
        return t;
    };
    auto paramsOf = [] (te::AudioTrack* t) -> juce::Array<juce::AudioProcessorParameter*>
    {
        if (t != nullptr)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (instrumentOf (*t)))
                if (auto* pi = ext->getAudioPluginInstance()) return pi->getParameters();
        return {};
    };

    // ---- the key: A minor or C major (the same notes), triads built on the scale
    const int minorSteps[7] { 0, 2, 3, 5, 7, 8, 10 }, majorSteps[7] { 0, 2, 4, 5, 7, 9, 11 };
    const int* steps = g.major ? majorSteps : minorSteps;
    const int tonic = 48 + g.key + (g.key < 4 ? 12 : 0);        // between E3 and D#4: each genre in its own key
    // the block's own seed: its sounds, its motif and its voice change with it (another block, another song)
    const auto blockSeed = [&]
    {
        auto seedText = SystemStats::getEnvironmentVariable ("RACOON_SEMILLA_SONIDOS", {});
        return seedText.isNotEmpty() ? (juce::uint32) seedText.getLargeIntValue() : (juce::uint32) Random::getSystemRandom().nextInt();
    }();
    auto note = [&] (int degree, int base)                      // a note of the scale, any degree, from "base"
    {
        const int oct = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
        const int d = degree - oct * 7;
        return base + steps[d] + 12 * oct;
    };
    auto bassNote = [&] (int degree)                            // the root, between C1 and B1: deep
    {
        int n = note (degree, tonic - 24);
        while (n > 47) n -= 12;
        while (n < 36) n += 12;
        return n - 12 + 12 * 0;
    };
    auto chordOf = [&] (int bar) { return g.prog[bar % 4]; };
    auto mod7 = [] (int d) { return ((d % 7) + 7) % 7; };
    // a note of the scale while chord "chordDeg" sounds: in the "harmonic" minor genres the 7th goes up a half step
    // on the V chord (the leading tone), everywhere else the notes are the key's own
    auto noteOn = [&] (int degree, int base, int chordDeg)
    {
        int n = note (degree, base);
        if (g.harmonic && ! g.major && mod7 (chordDeg) == 4 && mod7 (degree) == 6) ++n;
        return n;
    };
    // the pitch classes (0..11 from the tonic) of the chord on "deg", with the genre's colour
    auto chordTones = [&] (int deg, int colour)
    {
        std::vector<int> st;
        switch (colour)
        {
            case 1:  st = { 0, 2, 4, 6 }; break;          // seventh
            case 2:  st = { 0, 2, 4, 6, 8 }; break;       // ninth
            case 3:  st = { 0, 1, 4 }; break;             // sus2
            case 4:  st = { 0, 4 }; break;                // fifth
            default: st = { 0, 2, 4 }; break;
        }
        std::vector<int> pcs;
        for (int k : st) pcs.push_back (((noteOn (deg + k, tonic, deg) % 12) + 12) % 12);
        return pcs;
    };
    // voice leading: the pitch classes placed between lo and hi, as close as possible to the last chord of that part
    auto voiceLead = [] (const std::vector<int>& pcs, int lo, int hi, const std::vector<int>& prev)
    {
        std::vector<int> best;
        double bestCost = 1.0e9;
        const int n = (int) pcs.size();
        for (int inv = 0; inv < n; ++inv)
            for (int base = lo; base < lo + 12; ++base)
            {
                if (((base % 12) + 12) % 12 != pcs[(size_t) inv]) continue;
                std::vector<int> v { base };
                for (int k = 1; k < n; ++k)
                {
                    int x = v.back() + 1;
                    while (((x % 12) + 12) % 12 != pcs[(size_t) ((inv + k) % n)]) ++x;
                    v.push_back (x);
                }
                if (v.back() > hi) continue;
                double cost = 0.0;
                if (prev.empty()) { const double mid = (lo + hi) * 0.5; for (int x : v) cost += std::abs (x - mid); }
                else
                {
                    for (int x : v) { int d = 99; for (int y : prev) d = jmin (d, std::abs (x - y)); cost += d; }
                    cost += 0.5 * std::abs (v.front() - prev.front());
                }
                if (cost < bestCost) { bestCost = cost; best = v; }
            }
        if (best.empty())       // nothing fits the range: each note at its first place above lo (still in the key)
            for (int k = 0; k < n; ++k) { int x = lo; while (((x % 12) + 12) % 12 != pcs[(size_t) k]) ++x; best.push_back (x); }
        return best;
    };

    // a one-bar step string: calls hit (step position in beats, step length in beats, character)
    auto eachStep = [] (const char* pattern, std::function<void (double, double, char)> hit)
    {
        const int n = (int) std::strlen (pattern);
        if (n == 0) return;
        const double len = bpb / n;
        for (int i = 0; i < n; ++i) if (pattern[i] != '-' && pattern[i] != '_') hit (i * len, len, pattern[i]);
    };
    // how long a note lasts: until the next event (or the end of the bar), counting "_" as held
    auto heldLength = [] (const char* pattern, int i)
    {
        const int n = (int) std::strlen (pattern);
        int k = i + 1;
        while (k < n && pattern[k] == '_') ++k;
        return (double) (k - i) * bpb / n;
    };
    const double swing16 = g.swing * 0.25;
    auto swung = [&] (double pos)                               // the off sixteenths come a little late
    {
        const double s16 = pos / 0.25;
        return std::abs (s16 - std::round (s16)) < 1.0e-6 && ((int) std::round (s16)) % 2 == 1 ? pos + swing16 : pos;
    };
    auto vel = [] (char c, int base) { return c == 'X' ? 127 : c == 'o' ? base - 40 : base; };

    // the kick on its own channel and the rest of the kit (the groove) on another, both played by Hamster with the
    // same kit: the master (and the mix) treat the kick apart
    auto* kickT = midiTrack ("Racoon - Hamster Drums", String::fromUTF8 ("Hamster · Kick"));
    auto* drums = midiTrack ("Racoon - Hamster Drums", String::fromUTF8 ("Hamster · Groove"));
    auto* bass  = midiTrack ("Racoon - Badger Bass", String::fromUTF8 ("Badger · Bajo"));
    auto* pad   = midiTrack ("Racoon - Dormouse Pads", String::fromUTF8 ("Dormouse · Pads"));
    auto* keys  = midiTrack ("Racoon - Chipmunk Chords", String::fromUTF8 ("Chipmunk · Acordes"));
    auto* hook  = midiTrack ("Racoon - Squirrel Sampler", String::fromUTF8 ("Squirrel · Melodía"));
    auto* gtr   = midiTrack ("Racoon - Ferret Guitar", String::fromUTF8 ("Ferret · Guitarra"));
    auto* choir = midiTrack ("Racoon - Porcupine Choir", String::fromUTF8 ("Porcupine · Coro"));
    auto* perc  = midiTrack ("Racoon - Mole Rhythm", String::fromUTF8 ("Mole · Percusión"));
    te::AudioTrack* texture = addAudioTrack (String::fromUTF8 ("Skunk · Textura"));
    te::AudioTrack* voice = addAudioTrack (String::fromUTF8 ("Chinchilla · Voz"));
    if (texture != nullptr)
    {
        texture->setColour (Catalog::colourFor ("Racoon - Skunk Sound Design"));
        if (auto item = itemFor ("Skunk"); item.available) insertPlugin (*texture, item);
    }
    if (voice != nullptr)
    {
        voice->setColour (Catalog::colourFor ("Racoon - Chinchilla Vocals"));
        if (auto item = itemFor ("Chinchilla"); item.available) insertPlugin (*voice, item);
    }
    // the reverb return: a real room that suits the genre, when the library brings them (the algorithmic reverb otherwise)
    {
        const auto& cat = biblio::Catalogo::get();
        auto* rev = addReturnTrack (0, "Reverb", cat.salas.empty() ? "reverb" : "sala-real");
        if (rev != nullptr && ! cat.salas.empty())
        {
            using G = variedad::Grupo;
            const auto grupo = variedad::grupoDe (g.id, g.bpm);
            static const std::map<G, StringArray> preferidas {
                { G::house, { "salon-spokane", "recamara-vacia", "oficina", "capilla-st-albans" } },
                { G::afro, { "iglesia-san-lorenzo", "salon-spokane", "cancha-falkland" } },
                { G::disco, { "salon-spokane", "gimnasio", "recamara-vacia" } },
                { G::techno, { "tunel-tren", "estacionamiento", "bodega", "reactor-r1" } },
                { G::breaks, { "escalera", "recamara-vacia", "tunel-tren" } },
                { G::trap, { "recamara-vacia", "estacionamiento", "sala-casa" } },
                { G::latino, { "recamara-vacia", "salon-spokane", "sala-casa" } },
                { G::hiphop, { "sotano", "sala-casa", "oficina", "recamara-vacia" } },
                { G::chill, { "iglesia-san-lorenzo", "catedral-york", "capilla-st-albans", "iglesia-st-patrick" } } };
            StringArray instaladas;
            if (auto it = preferidas.find (grupo); it != preferidas.end())
                for (auto& id : it->second) if (cat.sala (id) != nullptr) instaladas.add (id);
            if (instaladas.isEmpty()) instaladas.add (cat.salas.front().id);
            const auto semilla = SystemStats::getEnvironmentVariable ("RACOON_SEMILLA_SONIDOS", {});
            Random rng (semilla.isNotEmpty() ? semilla.getLargeIntValue() + String (g.id).hashCode64() : Random::getSystemRandom().nextInt64());
            const auto* sala = cat.sala (instaladas[rng.nextInt (instaladas.size())]);
            for (auto* p : rev->pluginList)
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p))
                    if (auto* rp = dynamic_cast<RacoonProcessor*> (ext->getAudioPluginInstance()); rp != nullptr && rp->apvts.getParameter ("room") != nullptr)
                    {
                        const auto names = rp->apvts.getParameter ("room")->getAllValueStrings();
                        if (sala != nullptr && names.contains (sala->nombre)) rp->setRealValue ("room", (float) names.indexOf (sala->nombre));
                        rp->setRealValue ("predelay", 20.0f);
                        // a long room is shortened in club genres: the tail would cover the next kick
                        rp->setRealValue ("length", grupo == G::chill ? 100.0f : 70.0f);
                        edit->state.setProperty ("racoonSala", sala != nullptr ? sala->id : String(), nullptr);
                    }
        }
    }
    addReturnTrack (1, "Delay", "delay");

    // ---- drums: the intro keeps the kick and hats, the break drops the kick, the last bar has a fill
    const bool hasDrums = std::strlen (g.kick) + std::strlen (g.snare) + std::strlen (g.clap) + std::strlen (g.hat) > 0;
    if (drums != nullptr)
        if (auto* c = createMidiClip (*drums, { 0_bp, bp (beats) }, "Groove"))
        {
            auto& s = c->getSequence();
            for (int bar = 0; bar < (int) bars; ++bar)
            {
                const int ph = phraseOf (bar);
                const double b0 = bar * bpb;
                auto lay = [&] (const char* pat, int noteNum, int base)
                {
                    eachStep (pat, [&] (double pos, double len, char ch)
                    {
                        s.addNote (noteNum, bp (b0 + swung (pos)), bd (jmin (0.25, len)), jlimit (1, 127, vel (ch, base)), 0, um);
                    });
                };
                if (ph != 2) lay (g.kick, 36, 116);
                if (ph >= 1 || bar >= 2) { lay (g.snare, 38, 106); lay (g.clap, 39, 104); }
                else if (ph == 2 && bar % 2 == 1) lay (g.clap, 39, 96);
                lay (g.hat, 42, 88);
                if (ph >= 1) { lay (g.openHat, 46, 84); lay (g.rim, 37, 90); }
                if (ph == 1 || ph == 3) lay (g.extra, g.extraNote > 0 ? g.extraNote : 56, 92);
                if (ph == 2 && ! hasDrums) lay (g.rim, 37, 80);
                if (bar == (int) bars - 1 && hasDrums)                 // the fill into the next block
                    for (int k = 0; k < 4; ++k) s.addNote (38, bp (b0 + 3.0 + k * 0.25), bd (0.2), 70 + k * 12, 0, um);
                if (bar % 4 == 0 && ph >= 1 && hasDrums) s.addNote (49, bp (b0), bd (0.5), 96, 0, um);   // crash on each phrase
            }
        }

    // the kick notes go to their own channel
    if (drums != nullptr && kickT != nullptr)
        for (auto* c : drums->getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
                if (auto* kc = createMidiClip (*kickT, { 0_bp, bp (beats) }, "Kick"))
                {
                    Array<te::MidiNote*> kicks;
                    for (auto* n : mc->getSequence().getNotes()) if (n->getNoteNumber() == 36 || n->getNoteNumber() == 35) kicks.add (n);
                    for (auto* n : kicks)
                    {
                        kc->getSequence().addNote (n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(), n->getVelocity(), 0, um);
                        mc->getSequence().removeNote (*n, um);
                    }
                    if (kc->getSequence().getNumNotes() == 0) kc->removeFromParent();
                    break;
                }
    if (kickT != nullptr && kickT->getClips().isEmpty()) { edit->deleteTrack (kickT); kickT = nullptr; }

    // ---- bass: silent in the first two bars and at the start of the break
    if (bass != nullptr)
        if (auto* c = createMidiClip (*bass, { 0_bp, bp (beats) }, "Bajo"))
        {
            auto& s = c->getSequence();
            for (int bar = 0; bar < (int) bars; ++bar)
            {
                if (bar < 2 || bar == 8 || bar == 9) continue;
                const int deg = chordOf (bar);
                const int n = (int) std::strlen (g.bass);
                for (int i = 0; i < n; ++i)
                {
                    const char ch = g.bass[i];
                    if (ch == '-' || ch == '_') continue;
                    int nn = bassNote (deg);
                    if (ch == 'o') nn += 12;
                    else if (ch == 'f') nn = bassNote (deg) + (noteOn (deg + 4, 0, deg) - note (deg, 0));
                    else if (ch == 't') nn = bassNote (deg) + (noteOn (deg + 2, 0, deg) - note (deg, 0));
                    const double pos = i * bpb / n;
                    const double len = jmax (0.12, heldLength (g.bass, i) * (std::strchr (g.bass, '_') != nullptr ? 0.98 : 0.8));
                    s.addNote (nn, bp (bar * bpb + swung (pos)), bd (len), 108, 0, um);
                }
            }
        }

    // the registers: each part has its own place, so the chords don't pile up on the same notes
    // (low and dark: the whole block sits under the middle of the piano, nothing shrill)
    const int padLo = tonic - 19, padHi = tonic + 1;          // pads: low and wide, dark
    const int stabLo = tonic - 10, stabHi = tonic + 5;        // chord stabs: just above the pads, close
    const int gtrLo = tonic - 16, gtrHi = tonic + 2;          // guitar: under the pads
    const int choirLo = tonic - 12, choirHi = tonic + 4;      // choir: low voices

    // ---- pads: the whole block, one chord per bar, the root underneath and the rest led from chord to chord
    if (pad != nullptr)
        if (auto* c = createMidiClip (*pad, { 0_bp, bp (beats) }, "Pads"))
        {
            std::vector<int> prev;
            for (int bar = 0; bar < (int) bars; ++bar)
            {
                const int deg = chordOf (bar);
                auto pcs = chordTones (deg, g.color == 4 ? 3 : g.color);
                std::vector<int> upper (pcs.begin() + 1, pcs.end());
                if (upper.size() < 2) upper.push_back (pcs[0]);
                const auto v = voiceLead (upper, padLo, padHi, prev);
                prev = v;
                for (int x : v) c->getSequence().addNote (x, bp (bar * bpb), bd (3.9), 78, 0, um);
                c->getSequence().addNote (note (deg, tonic - 12), bp (bar * bpb), bd (3.9), 70, 0, um);
            }
        }

    // ---- chords: the genre's stab rhythm from phrase 2, close voicings above the pads (Chipmunk plays them as written)
    if (keys != nullptr)
        if (auto* c = createMidiClip (*keys, { 0_bp, bp (beats) }, "Acordes"))
        {
            std::vector<int> prev;
            for (int bar = 4; bar < (int) bars; ++bar)
            {
                if (phraseOf (bar) == 2) continue;
                const int deg = chordOf (bar);
                auto pcs = chordTones (deg, g.color);
                // ninths: without the root (the bass and the pads have it), the way keys players do it
                if (g.color == 2 && pcs.size() > 3) pcs.erase (pcs.begin());
                const auto v = voiceLead (pcs, stabLo, stabHi, prev);
                prev = v;
                const int n = (int) std::strlen (g.chords);
                for (int i = 0; i < n; ++i)
                    if (g.chords[i] == 'x')
                        for (int x : v)
                            c->getSequence().addNote (x, bp (bar * bpb + swung (i * bpb / n)), bd (jmax (0.2, heldLength (g.chords, i) * 0.9)), 94, 0, um);
            }
        }

    // ---- the hook: the genre's melodic rhythm over the chord tones, a two-bar motif that answers itself
    if (hook != nullptr)
        if (auto* c = createMidiClip (*hook, { 0_bp, bp (beats) }, String::fromUTF8 ("Melodía")))
        {
            // each genre has its own motif: a walk over the scale that leans on the chord tones
            int walk[16];
            {
                juce::Random rnd ((juce::int64) String (g.id).hashCode64() ^ 0x5eed ^ ((juce::int64) blockSeed << 7));
                const int pool[] { 0, 2, 4, 7, 4, 2, 1, 3, 5, 6, 0, 4 };
                int cur = 4;
                for (int i = 0; i < 16; ++i)
                {
                    const int pick = pool[rnd.nextInt (12)];
                    cur = (i % 4 == 0) ? (rnd.nextBool() ? 0 : 4) : jlimit (-2, 9, (cur + pick) / 2 + (rnd.nextInt (3) - 1));
                    walk[i] = cur;
                }
            }
            for (int bar = 4; bar < (int) bars; ++bar)
            {
                if (phraseOf (bar) == 2 && bar < 10) continue;          // the break breathes
                const int deg = chordOf (bar);
                int k = (bar % 2) * 5;
                eachStep (g.melody, [&] (double pos, double len, char)
                {
                    const int step = walk[(k++) % 16];
                    int nn = noteOn (deg + step, tonic - 12, deg);      // an octave under the old line: darker
                    while (nn > 62) nn -= 12;                           // by octaves, so the note stays in the key
                    while (nn < 40) nn += 12;
                    c->getSequence().addNote (nn, bp (bar * bpb + swung (pos)), bd (jmax (0.2, len * 0.85)), 92, 0, um);
                });
            }
        }

    // ---- guitar: in guitar genres it strums the genre's rhythm (a guitar shape under the pads); in the others it
    // picks the chord note by note in the second half, so it never doubles the pads
    if (gtr != nullptr)
        if (auto* c = createMidiClip (*gtr, { 0_bp, bp (beats) }, "Guitarra"))
        {
            std::vector<int> prev;
            for (int bar = g.guitar ? 4 : 8; bar < (int) bars; ++bar)
            {
                const int deg = chordOf (bar);
                const auto tri = chordTones (deg, g.color == 4 ? 4 : 0);
                // a guitar chord: root and fifth low, then the chord tones on top
                std::vector<int> shape;
                {
                    int r = note (deg, tonic);
                    while (r >= gtrLo + 12) r -= 12;
                    while (r < gtrLo) r += 12;
                    shape.push_back (r);
                    const auto up = voiceLead (tri, r + 5, jmax (gtrHi + 7, r + 21), prev);
                    prev = up;
                    for (int x : up) shape.push_back (x);
                }
                if (g.guitar)
                {
                    const int n = (int) std::strlen (g.chords);
                    for (int i = 0; i < n; ++i)
                        if (g.chords[i] == 'x')
                            for (size_t k = 0; k < shape.size(); ++k)   // a strum: the strings come one after the other
                                c->getSequence().addNote (shape[k], bp (bar * bpb + swung (i * bpb / n) + 0.012 * (double) k),
                                                          bd (jmax (0.2, heldLength (g.chords, i) * 0.9)), 88 - 3 * (int) k, 0, um);
                }
                else
                {
                    static const int order[] { 0, 2, 1, 3, 2, 1, 3, 2 };   // root, then the chord tones up and down, in eighths
                    for (int e = 0; e < 8; ++e)
                    {
                        const int idx = jmin ((int) shape.size() - 1, order[e]);
                        c->getSequence().addNote (shape[(size_t) idx], bp (bar * bpb + e * 0.5), bd (0.9), e == 0 ? 90 : 76, 0, um);
                    }
                }
            }
        }

    // ---- choir: long chords in the break and the last phrase
    if (choir != nullptr)
        if (auto* c = createMidiClip (*choir, { 0_bp, bp (beats) }, "Coro"))
        {
            std::vector<int> prev;
            for (int bar = 8; bar < (int) bars; ++bar)
            {
                const int deg = chordOf (bar);
                const auto v = voiceLead (chordTones (deg, g.color == 4 ? 3 : 0), choirLo, choirHi, prev);
                prev = v;
                for (int x : v) c->getSequence().addNote (x, bp (bar * bpb), bd (3.9), 70, 0, um);
            }
        }

    // ---- Mole: its percussion is a clip you can see and edit; Mole plays it with its own drums
    if (perc != nullptr)
    {
        if (auto* c = createMidiClip (*perc, { 0_bp, bp (beats) }, String::fromUTF8 ("Percusión")))
        {
            auto& sq = c->getSequence();
            // genres without hand percussion (trap, hip hop, lo-fi...) still get Mole's part: a shaker and a few congas
            const bool noPerc = std::strlen (g.shaker) == 0 && std::strlen (g.conga) == 0;
            const char* shaker = noPerc ? (g.bpm >= 130.0 ? "--x---x---x-x-x-" : "--x---x---x---xx") : g.shaker;
            const char* conga  = noPerc ? "------x--x----x-" : g.conga;
            for (int bar = 0; bar < (int) bars; ++bar)
            {
                const int ph = phraseOf (bar);
                const double b0 = bar * bpb;
                if (ph > 0 || bar >= 2)
                    eachStep (shaker, [&] (double pos, double, char ch) { sq.addNote (70, bp (b0 + swung (pos)), bd (0.2), ((int) (pos * 4) % 2) ? 96 : vel (ch, 60), 0, um); });
                if (ph != 2)
                {
                    int k = 0;
                    eachStep (conga, [&] (double pos, double, char) { sq.addNote ((k++ % 3) == 1 ? 63 : 64, bp (b0 + swung (pos)), bd (0.25), 96, 0, um); });
                }
                if (ph >= 1 && std::strlen (conga) > 0) { sq.addNote (60, bp (b0 + 1.25), bd (0.2), 86, 0, um); sq.addNote (61, bp (b0 + 3.25), bd (0.2), 80, 0, um); }
            }
        }
        for (auto* prm : paramsOf (perc))
            if (prm->getName (64) == "Reproducir" && prm->getText (1.0f, 64).containsIgnoreCase ("maqueta"))
                prm->setValueNotifyingHost (1.0f);                                    // "Lo que dice la maqueta"
    }

    // ---- every instrument in the song's key: the plugins that think in keys get it, so whatever they add, tune
    // or snap lands on the same notes
    {
        auto setIndex = [] (juce::AudioProcessorParameter* prm, int index)
        {
            const int n = jmax (2, prm->getNumSteps());
            prm->setValueNotifyingHost ((float) jlimit (0, n - 1, index) / (float) (n - 1));
        };
        auto setChoice = [] (juce::AudioProcessorParameter* prm, const String& label)
        {
            const int n = jmax (2, prm->getNumSteps());
            for (int i = 0; i < n; ++i)
            {
                const float v = (float) i / (float) (n - 1);
                if (prm->getText (v, 64) == label) { prm->setValueNotifyingHost (v); return true; }
            }
            return false;
        };
        const String scaleText = g.major ? "Mayor" : "Menor";
        // Chipmunk: the chords are already written voiced in the clip; it plays them as they are, in the key
        for (auto* prm : paramsOf (keys))
        {
            const auto name = prm->getName (64);
            if (name == "Tonalidad") setIndex (prm, g.key);
            else if (name == "Escala") setChoice (prm, scaleText);
            else if (name == "Acordes") setChoice (prm, "Apagado");
            else if (name == String::fromUTF8 ("Conducción de voces")) prm->setValueNotifyingHost (0.0f);
            else if (name == "Registro bajo") prm->setValueNotifyingHost (0.0f);    // the lowest it allows
            else if (name == "Registro alto") prm->setValueNotifyingHost (1.0f);    // the highest
        }
        // Porcupine harmonizes in the key; Chinchilla tunes the voice to the key (chromatic when the V chord borrows
        // the leading tone, so that note is not pulled down)
        for (auto* prm : paramsOf (choir))
        {
            if (prm->getName (64) == "Tonalidad") setIndex (prm, g.key);
            if (prm->getName (64) == "Escala") setChoice (prm, scaleText);
        }
        if (voice != nullptr)
            for (auto* p : voice->pluginList)
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && Catalog::isRacoonPlugin (*p))
                    if (auto* pi = ext->getAudioPluginInstance())
                        for (auto* prm : pi->getParameters())
                        {
                            if (prm->getName (64) == "Tonalidad") setIndex (prm, g.key);
                            if (prm->getName (64) == "Escala") setChoice (prm, g.harmonic && ! g.major ? String::fromUTF8 ("Cromática") : scaleText);
                        }
        auto setByText = [] (juce::AudioProcessorParameter* prm, const String& text) { prm->setValueNotifyingHost (prm->getValueForText (text)); };
        // Skunk: a material of its own library, played as grains at the pitch of the texture clip's notes
        {
            const String fam = String::fromUTF8 (g.family);
            static const std::map<String, juce::StringArray> libs {
                { "House", { "Órgano grave"_u, "Cuerdas lejanas", "Coro oscuro" } },
                { "Techno y trance", { "Viento afinado", "Órgano grave"_u } },
                { "Bass music", { "Agua profunda", "Viento afinado", "Órgano grave"_u } },
                { "Urbano y latino", { "Coro oscuro", "Polvo de cinta", "Agua profunda" } },
                { "Hip hop y R&B", { "Polvo de cinta", "Coro oscuro" } },
                { "Retro, pop y funk", { "Cuerdas lejanas", "Órgano grave"_u } },
                { "Chill", { "Agua profunda", "Polvo de cinta" } } };
            const auto it = libs.find (fam);
            const auto list = it != libs.end() ? it->second : juce::StringArray { "Órgano grave"_u };
            const auto material = list[(int) ((juce::uint32) String (g.id).hashCode64() % (juce::uint32) list.size())];
            if (texture != nullptr)
                for (auto* p : texture->pluginList)
                    if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && Catalog::isRacoonPlugin (*p))
                        if (auto* pi = ext->getAudioPluginInstance())
                            for (auto* prm : pi->getParameters())
                            {
                                const auto name = prm->getName (64);
                                if (name == "Biblioteca") setChoice (prm, material);
                                else if (name == "Granos con el teclado") prm->setValueNotifyingHost (1.0f);
                                else if (name == String::fromUTF8 ("Granos: tamaño")) setByText (prm, "220");
                                else if (name == String::fromUTF8 ("Granos: densidad")) setByText (prm, "28");
                                else if (name == String::fromUTF8 ("Granos: dispersión")) setByText (prm, "150");
                                else if (name == String::fromUTF8 ("Granos: estéreo")) setByText (prm, "90");
                            }
            edit->state.setProperty ("racoonTexture", material, nullptr);
        }
        // Chinchilla sings the voice clip with its own voice (low and warm)
        if (voice != nullptr)
            for (auto* p : voice->pluginList)
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && Catalog::isRacoonPlugin (*p))
                    if (auto* pi = ext->getAudioPluginInstance())
                        for (auto* prm : pi->getParameters())
                        {
                            if (prm->getName (64) == "Cantar las notas MIDI") prm->setValueNotifyingHost (1.0f);
                            if (prm->getName (64) == "Cantar: voz") setChoice (prm, g.bpm >= 135.0 ? "Media" : "Grave");
                        }
        // Ferret plays the guitar shapes written in the clip, note by note (its own strummer would add notes)
        for (auto* prm : paramsOf (gtr))
            if (prm->getName (64) == "Modo") setChoice (prm, "Instrumento");
        // the percussion with a pitch (kicks, toms, congas) is tuned to the key
        for (auto* t : { kickT, drums, perc })
            for (auto* prm : paramsOf (t))
            {
                const auto name = prm->getName (64);
                if (name == "Afinar el kit a" || name == String::fromUTF8 ("Afinar la percusión a")) setIndex (prm, g.key + 1);
                if (name == "Escala del kit" || name == String::fromUTF8 ("Escala de la percusión")) setChoice (prm, scaleText);
            }
    }

    // ---- Hamster: its own pattern quiet under the song (the kit is chosen with the other sounds, at the end) (the Kick and Groove clips play it)
    for (auto* kitTrack : { kickT, drums })
    if (kitTrack != nullptr)
        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (instrumentOf (*kitTrack)))
            if (auto* pi = ext->getAudioPluginInstance())
            {
                // the kit itself is chosen at the end, with the other sounds (varySounds)
                for (auto* prm : pi->getParameters())
                {
                    const auto name = prm->getName (64);
                    if (name == "Pattern follows the DAW") prm->setValueNotifyingHost (0.0f);
                    // after the kit: its kick and toms are measured and tuned to the song's key
                    if (name == "Afinar el kit a") prm->setValueNotifyingHost ((float) (g.key + 1) / (float) jmax (1, prm->getNumSteps() - 1));
                    if (name == "Escala del kit") prm->setValueNotifyingHost (g.major ? 1.0f : 0.0f);
                    if (name == "Notas del kit") prm->setValueNotifyingHost (1.0f);     // General MIDI, like the Groove clip
                }
            }

    // ---- Skunk: a texture played by MIDI. Skunk takes a material of its library (made by Skunk itself, no files) and
    // plays it as grains at the pitch of each note: long notes that follow the chords, low and dark, the whole block
    if (texture != nullptr)
        if (auto* c = createMidiClip (*texture, { 0_bp, bp (beats) }, "Textura"))
        {
            // a note held while the chord keeps it, a new one when it changes (at least once a phrase, so it breathes)
            int held = -1;
            double from = 0.0;
            for (int bar = 0; bar <= (int) bars; ++bar)
            {
                int n = -1;
                if (bar < (int) bars)
                {
                    const int deg = chordOf (bar);
                    // the chord's fifth in the first half of the block, its root in the second (under the tonic)
                    n = noteOn (deg + (bar < 8 ? 4 : 0), tonic - 12, deg);
                    while (n > tonic - 5) n -= 12;
                    while (n < tonic - 17) n += 12;
                }
                if (held >= 0 && (n != held || bar % 4 == 0))
                {
                    c->getSequence().addNote (held, bp (from), bd (bar * bpb - from - 0.05), 90, 0, um);
                    held = -1;
                }
                if (held < 0 && n >= 0) { held = n; from = bar * bpb; }
            }
        }

    // ---- Chinchilla: a voice for this song, written as MIDI that Chinchilla sings (its key, its chords, its tempo);
    // it comes in at bar 5 or at the break and stays to the end
    if (voice != nullptr && g.voice)
        if (auto* vc = createMidiClip (*voice, { 0_bp, bp (beats) }, String::fromUTF8 ("Voz · ") + String::fromUTF8 (g.name)))
        {
            juce::Random rnd ((juce::int64) String (g.id).hashCode64() ^ 0x70c4 ^ ((juce::int64) blockSeed << 11));
            // club genres get a chopped voice (short repeated syllables with gaps); the rest get a sung line
            static const juce::StringArray chopped { "techno", "hardtechno", "minimal", "acid", "psytrance", "dubstep",
                                                      "drill", "phonk", "hardstyle", "electro", "jerseyclub", "melodictechno" };
            const bool chop = chopped.contains (g.id);
            const double sungDurs[] { 0.5, 1.0, 1.0, 1.5, 2.0, 0.75 }, chopDurs[] { 0.25, 0.5, 0.25, 0.75, 0.5, 0.25 };
            const double* durs = chop ? chopDurs : sungDurs;
            std::vector<std::pair<double, double>> rhythm;              // a two-bar rhythm, reused with other notes
            for (double t = rnd.nextBool() ? 0.0 : 0.5; t < 7.5;)
            {
                const double d = durs[rnd.nextInt (6)];
                rhythm.push_back ({ t, jmin (d * (chop ? 0.7 : 0.9), 8.0 - t) });
                t += d + (rnd.nextInt (chop ? 2 : 4) == 0 ? (chop ? 0.75 : 0.5) : 0.0);
            }
            auto singable = [&] (int degree, int chordDeg)              // between B2 and E4: a low, dark voice
            {
                int n = noteOn (degree, tonic, chordDeg);
                while (n < 47) n += 12;
                while (n > 64) n -= 12;
                return n;
            };
            const int startBar = rnd.nextInt (3) == 0 ? 4 : 8;
            for (int bar = startBar; bar < (int) bars; bar += 2)
            {
                const bool answer = (bar / 2) % 2 == 1;
                int k = 0;
                for (auto& [t, len] : rhythm)
                {
                    const int barIn = (int) (t / bpb);
                    const int deg = chordOf (bar + barIn);
                    const int tone = chop ? (k % 4 == 3 ? 4 : 0) : (answer ? (k % 2 == 0 ? 2 : 4) : (k % 3 == 0 ? 0 : k % 3 == 1 ? 2 : 4)) + (rnd.nextInt (5) == 0 ? 1 : 0);
                    vc->getSequence().addNote (singable (deg + tone, deg), bp (bar * bpb + t), bd (len), 100 - 8 * (k % 3), 0, um);
                    ++k;
                }
            }
        }

    // ---- levels and space
    setVolume (kickT, 0.0f); setVolume (drums, hasDrums ? -1.0f : -6.0f); setVolume (bass, -2.0f); setVolume (pad, hasDrums ? -9.0f : -4.0f); setVolume (keys, -9.0f);
    setVolume (hook, -10.0f);  setVolume (gtr, g.guitar ? -8.0f : -11.0f); setVolume (choir, -10.0f); setVolume (perc, -12.0f);
    setVolume (texture, -14.0f); setVolume (voice, -6.0f);
    auto send = [this] (te::AudioTrack* t, int bus, float db) { if (t != nullptr) if (auto* s = getSend (*t, bus, true)) s->setGainDb (db); };
    send (drums, 0, -20.0f); send (pad, 0, -12.0f); send (choir, 0, -10.0f); send (voice, 0, -12.0f); send (gtr, 0, -16.0f);
    send (hook, 1, -14.0f);  send (voice, 1, -18.0f); send (keys, 1, -20.0f);

    // ---- Hedgehog on the Master: the Partícula and a little limiter
    {
        auto item = itemFor ("Hedgehog");
        if (item.available)
            if (auto p = insertOnMaster (item))
                if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p.get()))
                    if (auto* pi = ext->getAudioPluginInstance())
                        for (auto* prm : pi->getParameters())
                            if (prm->getName (64) == "Limitador: ganancia")
                                prm->setValueNotifyingHost (dynamic_cast<RangedAudioParameter*> (prm) != nullptr
                                                                ? dynamic_cast<RangedAudioParameter*> (prm)->convertTo0to1 (3.0f) : 0.125f);
    }

    // ---- the sounds: each block its own kit, bass, guitar, pads, chords, melody, choir... (another seed, another song)
    {
        const auto seed = blockSeed;
        edit->state.setProperty ("racoonSoundSeed", (int64) seed, nullptr);
        varySounds (seed);
    }
    // ---- the mix: every part with its chain, the master set for the genre
    if (SystemStats::getEnvironmentVariable ("RACOON_SIN_MEZCLA", {}).isEmpty()) autoMix (true);

    getTransport().setLoopRange ({ 0_tp, edit->tempoSequence.toTime (bp (beats)) });
    getTransport().looping = true;
    if (drums != nullptr) selection.selectOnly (drums);
    structureChanged();
}

void Session::buildHousitoTemplate()
{
    // "Housito": house at 124 BPM, the two Racoon instruments, two returns, eight bars looping
    setBpm (124.0);
    snapBeats = 0.25;
    for (auto* t : te::getAudioTracks (*edit))
        edit->deleteTrack (t);

    auto* drums = addRacoonInstrument ("Racoon Hamster");
    auto* bass  = addRacoonInstrument ("Racoon Badger");
    addReturnTrack (0, "Reverb", "reverb");
    addReturnTrack (1, "Delay", "delay");

    const double bars = 8.0, bpb = 4.0;
    auto* um = &edit->getUndoManager();

    if (drums != nullptr)
    {
        drums->setName (drums->getName().replace ("Hamster", String::fromUTF8 ("Hamster · Batería")));
        if (auto* c = createMidiClip (*drums, { 0_bp, te::BeatPosition::fromBeats (bars * bpb) }, "Groove"))
        {
            auto& s = c->getSequence();
            for (int b = 0; b < (int) (bars * bpb); ++b)
            {
                s.addNote (36, te::BeatPosition::fromBeats (b), 0.25_bd, 118, 0, um);           // bombo en negras
                s.addNote (42, te::BeatPosition::fromBeats (b + 0.5), 0.125_bd, 92, 0, um);     // charles en el contratiempo
                if (b % 2 == 1) s.addNote (39, te::BeatPosition::fromBeats (b), 0.25_bd, 104, 0, um); // palmas en 2 y 4
                if (b % 4 == 3) s.addNote (42, te::BeatPosition::fromBeats (b + 0.75), 0.125_bd, 70, 0, um);
            }
        }
        if (auto* send = getSend (*drums, 0, true)) send->setGainDb (-18.0f);
    }

    if (bass != nullptr)
    {
        bass->setName (bass->getName().replace ("Badger", "Badger · Bajo"_u));
        if (auto* c = createMidiClip (*bass, { 0_bp, te::BeatPosition::fromBeats (bars * bpb) }, "Bajo"))
        {
            auto& s = c->getSequence();
            // A minor, offbeat bass with a turnaround every two bars
            const int roots[] { 45, 45, 48, 43 };
            for (int bar = 0; bar < (int) bars; ++bar)
            {
                const int root = roots[(bar / 2) % 4];
                for (int beat = 0; beat < 4; ++beat)
                {
                    const double at = bar * bpb + beat;
                    s.addNote (root, te::BeatPosition::fromBeats (at + 0.5), 0.375_bd, 108, 0, um);
                    if (beat == 3 && bar % 2 == 1)
                        s.addNote (root + 12, te::BeatPosition::fromBeats (at + 0.75), 0.25_bd, 96, 0, um);
                }
            }
        }
        if (auto* send = getSend (*bass, 1, true)) send->setGainDb (-24.0f);
    }

    getTransport().setLoopRange ({ 0_tp, edit->tempoSequence.toTime (te::BeatPosition::fromBeats (bars * bpb)) });
    getTransport().looping = true;
    if (drums != nullptr) selection.selectOnly (drums);
    structureChanged();
}
}

// ===================================================================== "A la maqueta"
namespace racoon
{
File Session::maquetaInbox()
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

te::Plugin* Session::familyPluginOf (te::AudioTrack& t) const
{
    for (auto* p : t.pluginList)
        if (Catalog::isRacoonPlugin (*p) && Catalog::animalFor (p->getName()).isNotEmpty()) return p;
    return nullptr;
}

void Session::expectSequence (te::AudioTrack& t, double beat)
{
    expectedSequences[t.itemID] = jmax (0.0, beat);
}

void Session::pushTrackNames()
{
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* p : t->pluginList)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && Catalog::isRacoonPlugin (*p))
                if (auto* inst = ext->getAudioPluginInstance())
                {
                    auto& last = pushedNames[p->itemID];
                    if (last == t->getName()) continue;
                    last = t->getName();
                    juce::AudioProcessor::TrackProperties props;
                    props.name = t->getName();
                    props.colour = t->getColour();
                    inst->updateTrackProperties (props);
                }
}

String Session::pollMaqueta()
{
    String hint;
    const auto dir = maquetaInbox();
    auto notes = dir.findChildFiles (File::findFiles, false, "*.json");
    notes.sort();
    for (auto& j : notes)
    {
        const auto v = JSON::parse (j.loadFileAsString());
        const auto mid = dir.getChildFile (v["midi"].toString());
        // what was left from another session (Racoon closed) is not dropped into this song
        const bool stale = j.getLastModificationTime() < Time::getCurrentTime() - RelativeTime::minutes (5);
        if (! stale && mid.existsAsFile())
            hint = receiveSequence (mid, v["animal"].toString(), v["track"].toString(), v["kind"].toString(), v["clip"].toString());
        j.deleteFile();
        mid.deleteFile();
    }
    return hint;
}

File Session::editInbox()
{
    auto f = maquetaInbox().getChildFile ("Editar");
    f.createDirectory();
    return f;
}

te::Plugin* Session::editClipInInstrument (te::MidiClip& clip)
{
    auto* t = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    if (t == nullptr) return nullptr;
    auto* p = familyPluginOf (*t);
    if (p == nullptr) return nullptr;
    const auto animal = Catalog::animalFor (p->getName());
    if (animal.isEmpty() || animal == "Hedgehog") return nullptr;
    // the plugin finds the request by its channel's name: make sure it knows it
    pushedNames.erase (p->itemID);
    pushTrackNames();

    // the notes as the clip shows them, from its first beat
    const double offset = clip.getOffsetInBeats().inBeats(), length = clip.getLengthInBeats().inBeats();
    constexpr int tpq = 960;
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / jmax (20.0, getBpm()))), 0.0);
    seq.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (auto* n : clip.getSequence().getNotes())
    {
        const double s = n->getStartBeat().inBeats() - offset;
        if (s < -1.0e-6 || s >= length - 1.0e-6) continue;
        const double e = jmin (length, s + n->getLengthBeats().inBeats());
        const int ch = jlimit (1, 16, clip.getMidiChannel().getChannelNumber());
        seq.addEvent (MidiMessage::noteOn (ch, n->getNoteNumber(), (uint8) jlimit (1, 127, n->getVelocity())), s * tpq);
        seq.addEvent (MidiMessage::noteOff (ch, n->getNoteNumber()), jmax (s + 1.0 / 64.0, e) * tpq - 1.0);
    }
    seq.sort();
    seq.updateMatchedPairs();
    MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);

    const auto dir = editInbox();
    // an older request nobody took is thrown away
    for (auto& f : dir.findChildFiles (File::findFiles, false))
        if (f.getLastModificationTime() < Time::getCurrentTime() - RelativeTime::minutes (2)) f.deleteFile();
    const auto stamp = String (Time::currentTimeMillis()) + "-" + String::toHexString (Random::getSystemRandom().nextInt());
    auto midFile = dir.getChildFile ("Clip " + animal + " " + stamp + ".mid");
    {
        FileOutputStream os (midFile);
        if (! os.openedOk() || ! mf.writeTo (os)) return nullptr;
    }
    DynamicObject::Ptr o (new DynamicObject());
    o->setProperty ("animal", animal);
    o->setProperty ("track", t->getName());
    o->setProperty ("clip", clip.itemID.toString());
    o->setProperty ("name", clip.getName());
    o->setProperty ("beats", length);
    o->setProperty ("bpm", getBpm());
    o->setProperty ("midi", midFile.getFileName());
    auto tmp = dir.getChildFile (stamp + ".tmp");
    if (! tmp.replaceWithText (JSON::toString (var (o.get()))) || ! tmp.moveFileTo (dir.getChildFile (stamp + ".json"))) return nullptr;
    // the window remembers which clip it holds: when it closes with changes, it asks whether they go to the clip
    p->state.setProperty ("racoonEditingClip", clip.itemID.toString(), nullptr);
    p->state.setProperty ("racoonEditingClipName", clip.getName(), nullptr);
    maquetaInbox().getChildFile ("Estado").getChildFile (clip.itemID.toString() + ".cambios").deleteFile();
    return p;
}

String Session::receiveSequence (const File& mid, const String& animal, const String& trackName, const String& kind, const String& clipId)
{
    // back to the clip it was opened from: the part the plugin holds is replaced, what comes after it stays
    if (clipId.isNotEmpty())
        if (auto* clip = dynamic_cast<te::MidiClip*> (te::findClipForID (*edit, te::EditItemID::fromString (clipId))))
        {
            FileInputStream is (mid);
            MidiFile mf;
            if (! is.openedOk() || ! mf.readFrom (is)) return String::fromUTF8 ("No se pudo leer la secuencia de ") + animal;
            const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
            MidiMessageSequence all;
            for (int i = 0; i < mf.getNumTracks(); ++i) all.addSequence (*mf.getTrack (i), 0.0);
            all.updateMatchedPairs();
            double end = 0.0;
            for (auto* e : all) end = jmax (end, e->message.getTimeStamp() / tpq);
            // the clip keeps its length: the plugin's loop may be rounded up (3 bars come back as 4) or shorter
            // (Hamster holds 8 bars, Badger 4); only the part it holds is replaced
            const double span = jmin (clip->getLengthInBeats().inBeats(), jmax (1.0, std::ceil (end / 4.0 - 1.0e-6) * 4.0));
            auto& um = edit->getUndoManager();
            um.beginNewTransaction();
            const double offset = clip->getOffsetInBeats().inBeats();
            auto& seq = clip->getSequence();
            Array<te::MidiNote*> gone;
            for (auto* n : seq.getNotes())
            {
                const double s = n->getStartBeat().inBeats() - offset;
                if (s > -1.0e-6 && s < span - 1.0e-6) gone.add (n);
            }
            for (auto* n : gone) seq.removeNote (*n, &um);
            Array<te::MidiControllerEvent*> goneCc;
            for (auto* c : seq.getControllerEvents())
            {
                const double s = c->getBeatPosition().inBeats() - offset;
                if (s > -1.0e-6 && s < span - 1.0e-6) goneCc.add (c);
            }
            for (auto* c : goneCc) seq.removeControllerEvent (*c, &um);
            int count = 0;
            for (int i = 0; i < all.getNumEvents(); ++i)
            {
                const auto& m = all.getEventPointer (i)->message;
                const double b = m.getTimeStamp() / tpq;
                if (b >= span) continue;
                if (m.isNoteOn())
                {
                    const double off = all.getTimeOfMatchingKeyUp (i) / tpq;
                    const double len = off > b ? off - b : 0.25;
                    seq.addNote (m.getNoteNumber(), te::BeatPosition::fromBeats (b + offset), te::BeatDuration::fromBeats (jmin (len, span - b)),
                                 m.getVelocity(), 0, &um);
                    ++count;
                }
                else if (m.isController())
                    seq.addControllerEvent (te::BeatPosition::fromBeats (b + offset), m.getControllerNumber(), m.getControllerValue() << 7, &um);
            }
            selection.selectOnly (clip);
            structureChanged();
            return String::fromUTF8 ("«") + clip->getName() + String::fromUTF8 ("» quedó con los cambios de ") + animal + " (" + String (count) + " notas).";
        }

    // the channels that have this plugin
    Array<te::AudioTrack*> cands;
    for (auto* t : te::getAudioTracks (*edit))
        if (auto* p = familyPluginOf (*t); p != nullptr && Catalog::animalFor (p->getName()) == animal)
            cands.add (t);
    te::AudioTrack* source = nullptr;
    double beat = -1.0;
    for (auto* t : cands)
        if (auto it = expectedSequences.find (t->itemID); it != expectedSequences.end()) { source = t; beat = it->second; }
    if (source == nullptr) for (auto* t : cands) if (t->getName() == trackName) { source = t; break; }
    if (source == nullptr)
        for (auto* t : selection.getItemsOfType<te::AudioTrack>()) if (cands.contains (t)) { source = t; break; }
    if (source == nullptr && ! cands.isEmpty()) source = cands.getFirst();

    edit->getUndoManager().beginNewTransaction();
    te::AudioTrack* target = source;
    // no channel with this plugin in the song: the request came from somewhere else (a plugin in another DAW)
    if (target == nullptr) return {};

    // where: always at the end of the channel, so nothing that is already there gets covered (loop after loop)
    FileInputStream is (mid);
    MidiFile mf;
    double lengthBeats = 4.0;
    if (mf.readFrom (is))
    {
        const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
        double end = 0.0;
        for (int i = 0; i < mf.getNumTracks(); ++i) for (auto* e : *mf.getTrack (i)) end = jmax (end, e->message.getTimeStamp() / tpq);
        lengthBeats = jmax (4.0, std::ceil (end / 4.0 - 1.0e-6) * 4.0);
    }
    const double bar = jmax (1, getTimeSigNumerator());
    double lastEnd = -1.0;
    for (auto* c : target->getClips())
        lastEnd = jmax (lastEnd, edit->tempoSequence.toBeats (c->getPosition().getEnd()).inBeats());
    if (lastEnd >= 0.0) beat = std::ceil (lastEnd / bar - 1.0e-6) * bar;
    else if (beat < 0.0) beat = 0.0;
    auto* clip = addMidiFile (*target, mid, edit->tempoSequence.toTime (te::BeatPosition::fromBeats (beat)));
    expectedSequences.erase (target->itemID);
    if (source != nullptr) expectedSequences.erase (source->itemID);
    if (clip == nullptr) return String::fromUTF8 ("No se pudo leer la secuencia de ") + animal;
    clip->setName (String::fromUTF8 ("Secuencia de ") + animal);
    selection.selectOnly (clip);
    // the loop grows to take the new part in, so Play reaches it
    auto& tr = getTransport();
    const auto clipEnd = clip->getPosition().getEnd();
    if (tr.looping.get() && tr.getLoopRange().getStart() <= te::TimePosition() + te::TimeDuration::fromSeconds (1.0e-6)
         && tr.getLoopRange().getEnd() < clipEnd)
        tr.setLoopRange ({ tr.getLoopRange().getStart(), clipEnd });
    structureChanged();
    return String::fromUTF8 ("Secuencia de ") + animal + String::fromUTF8 (" en «") + target->getName() + String::fromUTF8 ("», compás ")
           + String ((int) (beat / bar) + 1) + ".";
}
}

// ===================================================================== copy, paste and ranges
namespace racoon
{
void Session::copyRange (const Array<te::AudioTrack*>& tracks, double startBeat, double endBeat)
{
    if (tracks.isEmpty() || endBeat <= startBeat) return;
    auto all = te::getAudioTracks (*edit);
    int firstIndex = INT_MAX;
    for (auto* t : tracks) firstIndex = jmin (firstIndex, all.indexOf (t));
    auto& ts = edit->tempoSequence;
    const auto rs = ts.toTime (te::BeatPosition::fromBeats (startBeat)), re = ts.toTime (te::BeatPosition::fromBeats (endBeat));
    std::vector<Piece> pieces;
    // the cut copies are made and trimmed inside a transaction that is then taken back, so the song never changes
    auto& um = edit->getUndoManager();
    um.beginNewTransaction();
    for (auto* t : tracks)
    {
        // a copy of the list: the copies made below are added to this same channel
        Array<te::Clip::Ptr> original;
        for (auto* c : t->getClips()) original.add (c);
        for (auto& c : original)
        {
            const auto cr = c->getEditTimeRange();
            if (cr.getEnd() <= rs || cr.getStart() >= re) continue;
            auto dup = te::duplicateClip (*c);
            if (dup == nullptr) continue;
            if (cr.getStart() < rs) dup->setStart (rs, true, false);
            if (cr.getEnd() > re) dup->setEnd (re, true);
            Piece p;
            p.trackOffset = all.indexOf (t) - firstIndex;
            p.startBeat = ts.toBeats (dup->getPosition().getStart()).inBeats() - startBeat;
            p.state = dup->state.createCopy();
            pieces.push_back (p);
        }
    }
    um.undoCurrentTransactionOnly();
    clipboard = std::move (pieces);
    clipboardLength = endBeat - startBeat;
    int lastIndex = -1;
    for (auto* t : tracks) lastIndex = jmax (lastIndex, all.indexOf (t));
    clipboardTracks = lastIndex - firstIndex + 1;
}

void Session::copyClips (const Array<te::Clip*>& clips)
{
    if (clips.isEmpty()) return;
    auto all = te::getAudioTracks (*edit);
    auto& ts = edit->tempoSequence;
    int firstIndex = INT_MAX;
    double start = 1.0e12, end = 0.0;
    for (auto* c : clips)
    {
        if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack())) firstIndex = jmin (firstIndex, all.indexOf (t));
        start = jmin (start, ts.toBeats (c->getPosition().getStart()).inBeats());
        end = jmax (end, ts.toBeats (c->getPosition().getEnd()).inBeats());
    }
    clipboard.clear();
    for (auto* c : clips)
        if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
            clipboard.push_back ({ all.indexOf (t) - firstIndex, ts.toBeats (c->getPosition().getStart()).inBeats() - start, c->state.createCopy() });
    clipboardLength = end - start;
    clipboardTracks = 0;
}

Array<te::Clip*> Session::pasteAt (te::AudioTrack& first, double beat)
{
    Array<te::Clip*> out;
    auto all = te::getAudioTracks (*edit);
    const int base = all.indexOf (&first);
    if (base < 0) return out;
    beat = jmax (0.0, beat);
    auto& ts = edit->tempoSequence;
    // overwrite: a copied range takes its whole length of time on its channels (the silent parts too);
    // copied clips take the place each one lands on
    if (clipboardTracks > 0)
    {
        Array<te::AudioTrack*> span;
        for (int i = 0; i < clipboardTracks; ++i) if (auto* t = all[base + i]) span.add (t);
        deleteRange (span, beat, beat + clipboardLength);
    }
    auto* um = &edit->getUndoManager();
    for (auto& p : clipboard)
    {
        auto* t = all[base + p.trackOffset];
        if (t == nullptr) continue;
        auto n = p.state.createCopy();
        te::EditItemID::remapIDs (n, nullptr, *edit);
        const auto id = te::EditItemID::fromID (n);
        t->state.appendChild (n, um);
        if (auto c = t->findClipForID (id))
        {
            c->setStart (ts.toTime (te::BeatPosition::fromBeats (jmax (0.0, beat + p.startBeat))), false, true);
            out.add (c);
        }
    }
    if (clipboardTracks == 0) settleClips (out);
    return out;
}

void Session::clearUnder (te::ClipTrack& t, te::TimeRange r, const Array<te::Clip*>& keep)
{
    if (r.getLength() <= te::TimeDuration::fromSeconds (1.0e-6)) return;
    const auto eps = te::TimeDuration::fromSeconds (1.0e-6);
    Array<te::Clip::Ptr> list;
    for (auto* c : t.getClips()) if (! keep.contains (c)) list.add (c);
    for (auto& c : list)
    {
        const auto cr = c->getEditTimeRange();
        if (cr.getEnd() <= r.getStart() + eps || cr.getStart() >= r.getEnd() - eps) continue;
        const bool before = cr.getStart() < r.getStart() - eps, after = cr.getEnd() > r.getEnd() + eps;
        if (! before && ! after)
        {
            selection.deselect (c.get());
            c->removeFromParent();
        }
        else if (before && after)
        {
            // the new clip lands in the middle: the old one keeps its head and its tail
            if (auto* tail = t.splitClip (*c, r.getEnd()))
                ignoreUnused (tail);
            c->setEnd (r.getStart(), true);
        }
        else if (before) c->setEnd (r.getStart(), true);
        else             c->setStart (r.getEnd(), true, false);
    }
}

void Session::settleClips (const Array<te::Clip*>& clips)
{
    Array<te::Clip::Ptr> keepAlive;
    for (auto* c : clips) keepAlive.add (c);
    for (auto* c : clips)
        if (auto* t = c->getClipTrack())
            clearUnder (*t, c->getEditTimeRange(), clips);
}

void Session::deleteRange (const Array<te::AudioTrack*>& tracks, double startBeat, double endBeat)
{
    if (endBeat <= startBeat) return;
    auto& ts = edit->tempoSequence;
    const auto rs = ts.toTime (te::BeatPosition::fromBeats (startBeat)), re = ts.toTime (te::BeatPosition::fromBeats (endBeat));
    for (auto* t : tracks)
    {
        Array<te::Clip*> inside;
        for (auto* c : Array<te::Clip*> (t->getClips()))
        {
            auto cr = c->getEditTimeRange();
            if (cr.getEnd() <= rs || cr.getStart() >= re) continue;
            te::Clip* mid = c;
            if (cr.getStart() < rs) mid = t->splitClip (*mid, rs);
            if (mid != nullptr && mid->getEditTimeRange().getEnd() > re) t->splitClip (*mid, re);
            if (mid != nullptr) inside.add (mid);
        }
        for (auto* c : inside)
        {
            selection.deselect (c);
            c->removeFromParent();
        }
    }
}

void Session::moveClips (const Array<te::Clip*>& clips, double deltaBeats, int trackDelta)
{
    auto& ts = edit->tempoSequence;
    // never before the start of the song
    for (auto* c : clips) deltaBeats = jmax (deltaBeats, -ts.toBeats (c->getPosition().getStart()).inBeats());
    auto all = te::getAudioTracks (*edit);
    for (auto* c : clips)
    {
        const double b = ts.toBeats (c->getPosition().getStart()).inBeats() + deltaBeats;
        c->setStart (ts.toTime (te::BeatPosition::fromBeats (b)), false, true);
    }
    if (trackDelta != 0)
        for (auto* c : clips)
            if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                if (auto* target = all[all.indexOf (t) + trackDelta])
                    if (kindOf (*target) != TrackKind::returnBus)
                    {
                        te::Clip::Ptr keep (c);
                        keep->moveTo (*target);
                    }
    // where they land, what was under them goes
    settleClips (clips);
}
}

// ===================================================================== ARMONIZAR
namespace racoon
{
namespace
{
// a note of a song in one key, in another: the same distance from the tonic when only the tonic moves (the closest way,
// up to 6 semitones up or 5 down); when the mode changes too, the same degree of the new scale, so the line keeps its shape
int harmonizePitch (int p, int oldRoot, bool oldMajor, int newRoot, bool newMajor)
{
    static const int maj[7] { 0, 2, 4, 5, 7, 9, 11 }, mn[7] { 0, 2, 3, 5, 7, 8, 10 };
    const int* so = oldMajor ? maj : mn;
    const int* sn = newMajor ? maj : mn;
    int r = ((newRoot - oldRoot) % 12 + 12) % 12;
    if (r > 6) r -= 12;
    const int rel = p - oldRoot;
    const int oct = (int) std::floor (rel / 12.0);
    const int pc = rel - oct * 12;
    int d = 0;
    for (int i = 6; i >= 0; --i) if (so[i] <= pc) { d = i; break; }
    const int chrom = pc - so[d];
    return jlimit (0, 127, oldRoot + r + oct * 12 + sn[d] + chrom);
}

int rootShift (int oldRoot, int newRoot)
{
    int r = ((newRoot - oldRoot) % 12 + 12) % 12;
    return r > 6 ? r - 12 : r;
}

bool isDrumTrack (te::AudioTrack& t)
{
    if (auto* inst = Session::instrumentOf (t))
        if (Catalog::isRacoonPlugin (*inst, "Racoon Hamster") || Catalog::isRacoonPlugin (*inst, "Racoon Mole")) return true;
    const auto n = t.getName().toLowerCase();
    for (auto* w : { "kick", "groove", "drum", "bater", "percus", "transicion" }) if (n.contains (String::fromUTF8 (w))) return true;
    return false;
}

// the parameters of Racoon's plugins that follow the key: "Tonalidad", "Escala", and the tuning of the kits
bool isKeyParam (const String& name)
{
    return name == "Tonalidad" || name == "Escala" || name == "Afinar el kit a" || name == String::fromUTF8 ("Afinar la percusión a")
        || name == "Escala del kit" || name == String::fromUTF8 ("Escala de la percusión");
}
}

bool Session::songKey (int& root, bool& major) const
{
    if (edit == nullptr) return false;
    const auto& st = edit->state;
    if (st.hasProperty ("racoonKeyRoot")) { root = (int) st.getProperty ("racoonKeyRoot"); major = (bool) st.getProperty ("racoonKeyMajor"); return true; }
    if (const auto* g = findGenre (st.getProperty ("racoonGenre").toString())) { root = g->key; major = g->major; return true; }
    return false;
}

void Session::beginHarmonize (int oldRoot, bool oldMajor)
{
    harm = {};
    harm.active = true;
    harm.root = ((oldRoot % 12) + 12) % 12;
    harm.major = oldMajor;
    harm.keyText = edit->state.getProperty ("racoonKey").toString();
    harm.hadRoot = edit->state.hasProperty ("racoonKeyRoot");
    for (auto* t : te::getAudioTracks (*edit))
    {
        const bool drums = isDrumTrack (*t);
        for (auto* c : t->getClips())
        {
            if (auto* mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && ! drums)
            {
                HarmClip hc;
                hc.clip = mc;
                for (auto* n : mc->getSequence().getNotes()) hc.notes.push_back ({ n, n->getNoteNumber() });
                harm.clips.push_back (std::move (hc));
            }
            else if (auto* ac = dynamic_cast<te::WaveAudioClip*> (c); ac != nullptr && ! drums)
                harm.audio.push_back ({ ac, ac->getPitchChange() });
        }
        for (auto* p : t->pluginList)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && Catalog::isRacoonPlugin (*p))
                if (auto* pi = ext->getAudioPluginInstance())
                    for (auto* prm : pi->getParameters())
                        if (isKeyParam (prm->getName (64))) harm.params.push_back ({ prm, prm->getValue() });
    }
}

void Session::applyHarmonize (int root, bool major, juce::UndoManager* um)
{
    if (! harm.active) return;
    root = ((root % 12) + 12) % 12;
    for (auto& hc : harm.clips)
        for (auto& [n, p] : hc.notes)
            n->setNoteNumber (harmonizePitch (p, harm.root, harm.major, root, major), um);
    const int r = rootShift (harm.root, root);
    for (auto& [ac, pitch] : harm.audio) ac->setPitchChange (pitch + (float) r);
    for (auto& [prm, v] : harm.params)
    {
        const auto name = prm->getName (64);
        const int steps = jmax (2, prm->getNumSteps());
        auto setIndex = [&] (int i) { prm->setValueNotifyingHost ((float) jlimit (0, steps - 1, i) / (float) (steps - 1)); };
        auto setText = [&] (const String& text)
        {
            for (int i = 0; i < steps; ++i)
            {
                const float x = (float) i / (float) (steps - 1);
                if (prm->getText (x, 64) == text) { prm->setValueNotifyingHost (x); return; }
            }
        };
        if (name == "Tonalidad") setIndex (root);
        else if (name == "Afinar el kit a" || name == String::fromUTF8 ("Afinar la percusión a")) setIndex (root + 1);
        else
        {
            // the scale: major or minor (a chromatic one stays chromatic)
            const auto before = prm->getText (v, 64);
            if (before == "Mayor" || before == "Menor") setText (major ? "Mayor" : "Menor");
            else if (name == "Escala del kit" && steps == 2) prm->setValueNotifyingHost (major ? 1.0f : 0.0f);
        }
    }
    edit->state.setProperty ("racoonKeyRoot", root, um);
    edit->state.setProperty ("racoonKeyMajor", major, um);
    edit->state.setProperty ("racoonKey", keyName (root, major) + " (" + camelot (root, major) + ")", um);
}

void Session::previewHarmonize (int root, bool major) { applyHarmonize (root, major, nullptr); }

void Session::cancelHarmonize()
{
    if (! harm.active) return;
    for (auto& hc : harm.clips) for (auto& [n, p] : hc.notes) n->setNoteNumber (p, nullptr);
    for (auto& [ac, pitch] : harm.audio) ac->setPitchChange (pitch);
    for (auto& [prm, v] : harm.params) prm->setValueNotifyingHost (v);
    edit->state.setProperty ("racoonKey", harm.keyText, nullptr);
    if (harm.hadRoot) { edit->state.setProperty ("racoonKeyRoot", harm.root, nullptr); edit->state.setProperty ("racoonKeyMajor", harm.major, nullptr); }
    else { edit->state.removeProperty ("racoonKeyRoot", nullptr); edit->state.removeProperty ("racoonKeyMajor", nullptr); }
    harm = {};
}

void Session::commitHarmonize (int root, bool major)
{
    if (! harm.active) return;
    // back to the start without a trace, then the change for real as one Cmd+Z step
    auto keep = harm;
    cancelHarmonize();
    harm = keep;
    auto& um = edit->getUndoManager();
    um.beginNewTransaction ("Armonizar");
    applyHarmonize (root, major, &um);
    harm = {};
}
}

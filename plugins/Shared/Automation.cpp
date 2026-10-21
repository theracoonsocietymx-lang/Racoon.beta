#include "Automation.h"

namespace racoon
{
using namespace juce;

namespace
{
// CC numbers no synth uses for anything standard, in the order lanes take them
const int kFreeCCs[] { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                       102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
                       3, 9, 14, 15, 85, 86, 87, 88, 89, 90, 52, 53 };

String idOf (const AudioProcessorParameter* p)
{
    if (auto* w = dynamic_cast<const AudioProcessorParameterWithID*> (p)) return w->paramID;
    return {};
}

String pointsToString (const std::vector<Automation::Point>& pts)
{
    String s;
    for (auto& p : pts)
    {
        if (s.isNotEmpty()) s << ";";
        s << String (p.beat, 6) << ":" << String (p.value, 6);
    }
    return s;
}

std::vector<Automation::Point> pointsFromString (const String& s)
{
    std::vector<Automation::Point> out;
    for (auto& tok : StringArray::fromTokens (s, ";", {}))
    {
        if (! tok.containsChar (':')) continue;
        out.push_back ({ tok.upToFirstOccurrenceOf (":", false, false).getDoubleValue(),
                         jlimit (0.0f, 1.0f, tok.fromFirstOccurrenceOf (":", false, false).getFloatValue()) });
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    return out;
}
}

// ===================================================================== undo
class AutomationEdit : public UndoableAction
{
public:
    AutomationEdit (Automation& a, String i, std::vector<Automation::Point> b, bool eb, std::vector<Automation::Point> af, bool ea)
        : owner (a), id (std::move (i)), before (std::move (b)), after (std::move (af)), existedBefore (eb), existsAfter (ea) {}
    bool perform() override { apply (after, existsAfter); return true; }
    bool undo() override    { apply (before, existedBefore); return true; }
    int getSizeInUnits() override { return 16 + (int) (before.size() + after.size()); }
private:
    Automation& owner;
    String id;
    std::vector<Automation::Point> before, after;
    bool existedBefore, existsAfter;
    void apply (const std::vector<Automation::Point>& pts, bool exists)
    {
        if (exists) owner.setPoints (id, pts, false);
        else owner.removeLane (id, false);
    }
};

// ===================================================================== setup
Automation::Automation (AudioProcessor& p, UndoManager* um, int defaultBars) : proc (p), undo (um)
{
    loopBars.store (jlimit (1, 16, defaultBars));
    int maxIndex = 0;
    for (auto* ap : proc.getParameters())
    {
        maxIndex = jmax (maxIndex, ap->getParameterIndex() + 1);
        if (auto* r = dynamic_cast<RangedAudioParameter*> (ap))
            if (r->isAutomatable() && idOf (r).isNotEmpty() && idOf (r) != "bypass" && r != proc.getBypassParameter())
            {
                params.push_back (r);
                paramIds.add (idOf (r));
            }
        ap->addListener (this);
    }
    numTouched = jmax (1, maxIndex);
    touched.reset (new std::atomic<bool>[(size_t) numTouched]);
    for (int i = 0; i < jmax (1, maxIndex); ++i) touched[(size_t) i].store (false);
    publish();
}

Automation::~Automation()
{
    for (auto* ap : proc.getParameters()) ap->removeListener (this);
}

void Automation::prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 44100.0; }

void Automation::parameterGestureChanged (int index, bool starting)
{
    if (index >= 0 && index < numTouched) touched[(size_t) index].store (starting);
}

// ===================================================================== lanes
Automation::Lane* Automation::find (const String& id)
{
    for (auto& l : lanes) if (l.id == id) return &l;
    return nullptr;
}

const Automation::Lane* Automation::find (const String& id) const
{
    for (auto& l : lanes) if (l.id == id) return &l;
    return nullptr;
}

RangedAudioParameter* Automation::parameter (const String& id) const
{
    const int i = paramIds.indexOf (id);
    return i >= 0 ? params[(size_t) i] : nullptr;
}

StringArray Automation::automatableIds() const { return paramIds; }

String Automation::nameOf (const String& id) const
{
    if (auto* p = parameter (id)) return p->getName (60);
    return id;
}

bool Automation::hasLane (const String& id) const   { return find (id) != nullptr; }
bool Automation::hasPoints (const String& id) const { auto* l = find (id); return l != nullptr && ! l->pts.empty(); }
std::vector<Automation::Point> Automation::points (const String& id) const { auto* l = find (id); return l != nullptr ? l->pts : std::vector<Point>(); }
int Automation::ccFor (const String& id) const      { auto* l = find (id); return l != nullptr ? l->cc : -1; }

StringArray Automation::laneIds() const
{
    StringArray s;
    for (auto& l : lanes) s.add (l.id);
    return s;
}

int Automation::nextFreeCC() const
{
    for (int c : kFreeCCs)
    {
        bool used = false;
        for (auto& l : lanes) used |= l.cc == c;
        if (! used) return c;
    }
    return -1;
}

void Automation::setPoints (const String& id, std::vector<Point> pts, bool undoable)
{
    auto* p = parameter (id);
    if (p == nullptr) return;
    for (auto& pt : pts) { pt.value = jlimit (0.0f, 1.0f, pt.value); pt.beat = jlimit (0.0, 64.0, pt.beat); }
    std::stable_sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    if (undoable && undo != nullptr)
    {
        undo->perform (new AutomationEdit (*this, id, points (id), hasLane (id), std::move (pts), true));
        return;
    }
    auto* l = find (id);
    if (l == nullptr)
    {
        Lane n;
        n.id = id;
        n.cc = nextFreeCC();
        n.param = p;
        n.index = p->getParameterIndex();
        n.discrete = p->isDiscrete() || p->isBoolean();
        lanes.push_back (n);
        l = &lanes.back();
    }
    if (l->pts == pts && l->param != nullptr) return;
    l->pts = std::move (pts);
    publish();
    sendChangeMessage();
}

void Automation::removeLane (const String& id, bool undoable)
{
    if (! hasLane (id)) return;
    if (undoable && undo != nullptr)
    {
        undo->perform (new AutomationEdit (*this, id, points (id), true, {}, false));
        return;
    }
    lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&] (auto& l) { return l.id == id; }), lanes.end());
    publish();
    sendChangeMessage();
}

void Automation::clearAll()
{
    lanes.clear();
    publish();
    sendChangeMessage();
}

void Automation::setBars (int b)
{
    b = b <= 1 ? 1 : b <= 2 ? 2 : b <= 4 ? 4 : b <= 8 ? 8 : 16;
    if (b == loopBars.load()) return;
    loopBars.store (b);
    sendChangeMessage();
}

void Automation::commitEdit (const String& id, const std::vector<Point>& before, bool existedBefore)
{
    if (undo == nullptr) return;
    const bool exists = hasLane (id);
    auto after = points (id);
    if (exists == existedBefore && after == before) return;
    undo->beginNewTransaction();
    // the lane is already as it should be: the action only has to know both sides
    undo->perform (new AutomationEdit (*this, id, before, existedBefore, after, exists));
}

void Automation::publish()
{
    auto next = std::make_shared<const Lanes> (lanes);
    {
        const SpinLock::ScopedLockType l (liveLock);
        std::swap (live, next);
    }
    if (next != nullptr) retired.push_back (std::move (next));
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (auto& r) { return r.use_count() <= 1; }), retired.end());
}

// ===================================================================== state
ValueTree Automation::toTree() const
{
    ValueTree t (treeType);
    t.setProperty ("bars", loopBars.load(), nullptr);
    for (auto& l : lanes)
    {
        ValueTree v ("LANE");
        v.setProperty ("id", l.id, nullptr);
        v.setProperty ("cc", l.cc, nullptr);
        v.setProperty ("pts", pointsToString (l.pts), nullptr);
        t.appendChild (v, nullptr);
    }
    return t;
}

void Automation::fromTree (const ValueTree& t)
{
    lanes.clear();
    if (t.isValid() && t.hasType (treeType))
    {
        loopBars.store (jlimit (1, 16, (int) t.getProperty ("bars", loopBars.load())));
        for (auto v : t)
        {
            auto* p = parameter (v["id"].toString());
            if (p == nullptr || ! v.hasType ("LANE")) continue;
            Lane n;
            n.id = v["id"].toString();
            n.param = p;
            n.index = p->getParameterIndex();
            n.discrete = p->isDiscrete() || p->isBoolean();
            n.pts = pointsFromString (v["pts"].toString());
            n.cc = (int) v.getProperty ("cc", -1);
            for (auto& o : lanes) if (o.cc == n.cc) n.cc = -1;
            if (n.cc < 0) n.cc = nextFreeCC();
            lanes.push_back (std::move (n));
        }
    }
    publish();
    sendChangeMessage();
}

// ===================================================================== playback
float Automation::valueAt (const std::vector<Point>& pts, double pos, double len, bool discrete)
{
    if (pts.empty()) return 0.0f;
    if (pts.size() == 1) return pts.front().value;
    auto it = std::upper_bound (pts.begin(), pts.end(), pos, [] (double b, const Point& p) { return b < p.beat; });
    const Point *prev, *next;
    double pb, nb;
    if (it == pts.begin())     { prev = &pts.back(); pb = prev->beat - len; next = &pts.front(); nb = next->beat; }
    else if (it == pts.end())  { prev = &pts.back(); pb = prev->beat; next = &pts.front(); nb = next->beat + len; }
    else                       { prev = &*(it - 1); pb = prev->beat; next = &*it; nb = next->beat; }
    if (discrete || nb <= pb) return prev->value;
    const double t = jlimit (0.0, 1.0, (pos - pb) / (nb - pb));
    return (float) (prev->value + t * (next->value - prev->value));
}

double Automation::clock (bool hostPlaying, double hostBeat, bool ownPlaying, double ownBpm, int numSamples)
{
    if (hostPlaying) { ownWas = false; return hostBeat; }
    if (! ownPlaying) { ownWas = false; return -1.0; }
    if (! ownWas) { ownBeat = 0.0; ownWas = true; }
    const double b = ownBeat;
    ownBeat += (double) numSamples * jlimit (20.0, 400.0, ownBpm) / 60.0 / sr;
    if (ownBeat > 1.0e6) ownBeat = std::fmod (ownBeat, lengthBeats());
    return b;
}

void Automation::process (const MidiBuffer& incoming, double beat, int numSamples)
{
    std::shared_ptr<const Lanes> cur;
    {
        const SpinLock::ScopedTryLockType l (liveLock);
        if (! l.isLocked()) return;
        cur = live;
    }
    if (cur == nullptr) return;

    // CC from an exported file (or from a controller): moves the parameter of its lane and holds the drawn line off
    if (! incoming.isEmpty() && ! cur->empty())
        for (const auto meta : incoming)
        {
            const auto m = meta.getMessage();
            if (! m.isController()) continue;
            const int c = m.getControllerNumber();
            for (auto& l : *cur)
                if (l.cc == c && l.param != nullptr)
                {
                    l.param->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
                    ccHold[c] = (int) (sr * 0.5);
                }
        }
    for (auto& h : ccHold) if (h > 0) h -= numSamples;

    if (beat < 0.0) { uiBeat.store (-1.0); return; }
    const double len = lengthBeats();
    double pos = std::fmod (beat, len);
    if (pos < 0.0) pos += len;
    uiBeat.store (pos);
    for (auto& l : *cur)
    {
        if (l.pts.empty() || l.param == nullptr) continue;
        if (l.index >= 0 && l.index < numTouched && touched[(size_t) l.index].load()) continue;
        if (l.cc >= 0 && ccHold[l.cc] > 0) continue;
        const float v = valueAt (l.pts, pos, len, l.discrete);
        if (std::abs (l.param->getValue() - v) > 1.0e-4f)
            l.param->setValueNotifyingHost (v);
    }
}

// ===================================================================== recording
void Automation::recordTick()
{
    const double pos = uiBeat.load();
    if (! recording.load() || pos < 0.0)
    {
        // a gesture that ends after recording stops is still one undo step
        for (auto& [idx, before] : recBefore)
            for (auto* p : params)
                if (p->getParameterIndex() == idx) commitEdit (idOf (p), before.first, before.second);
        recBefore.clear();
        recLast.clear();
        return;
    }
    const double len = lengthBeats();
    for (size_t k = 0; k < params.size(); ++k)
    {
        auto* p = params[k];
        const int idx = p->getParameterIndex();
        const bool held = idx >= 0 && idx < numTouched && touched[(size_t) idx].load();
        if (! held && recBefore.count (idx) == 0) continue;
        const auto& id = paramIds[(int) k];
        if (held)
        {
            if (recBefore.count (idx) == 0) { recBefore[idx] = { points (id), hasLane (id) }; recLast[idx] = pos; }
            auto pts = points (id);
            const double from = recLast[idx];
            auto inSweep = [&] (double b)
            {
                if (pos >= from) return b > from && b <= pos;
                return b > from || b <= pos;                     // the loop wrapped
            };
            pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (const Point& q) { return inSweep (q.beat) || std::abs (q.beat - pos) < 1.0e-6; }), pts.end());
            pts.push_back ({ std::fmod (pos, len), p->getValue() });
            setPoints (id, std::move (pts), false);
            recLast[idx] = pos;
        }
        else if (recBefore.count (idx) != 0)
        {
            commitEdit (id, recBefore[idx].first, recBefore[idx].second);
            recBefore.erase (idx);
            recLast.erase (idx);
        }
    }
}

// ===================================================================== export
void Automation::appendToMidi (MidiMessageSequence& seq, double ticksPerBeat, double totalBeats, int channel) const
{
    const double len = lengthBeats();
    for (auto& l : lanes)
    {
        if (l.cc < 0 || l.pts.empty()) continue;
        int last = -1;
        for (double b = 0.0; b < totalBeats - 1.0e-9; b += 0.125)
        {
            const int v = jlimit (0, 127, roundToInt (valueAt (l.pts, std::fmod (b, len), len, l.discrete) * 127.0f));
            if (v != last) seq.addEvent (MidiMessage::controllerEvent (channel, l.cc, v), b * ticksPerBeat);
            last = v;
        }
    }
    seq.sort();
}

void Automation::addToMidiFile (MidiFile& mf, double patternBeats) const
{
    if (lanes.empty() || mf.getNumTracks() == 0) return;
    const short tf = mf.getTimeFormat();
    const double tpq = tf > 0 ? (double) tf : 960.0;
    // the pattern's length: its last event, rounded up to a bar
    double lastTick = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* e : *mf.getTrack (t)) lastTick = jmax (lastTick, e->message.getTimeStamp());
    const double patBeats = patternBeats > 0.0 ? patternBeats : jmax (4.0, std::ceil (lastTick / tpq / 4.0 - 1.0e-6) * 4.0);
    const double total = jmax (patBeats, lengthBeats());
    MidiFile out;
    out.setTicksPerQuarterNote ((int) tpq);
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        MidiMessageSequence seq;
        const auto& src = *mf.getTrack (t);
        for (double off = 0.0; off < total - 1.0e-9; off += patBeats)
            for (auto* e : src)
            {
                const auto& m = e->message;
                if (m.isEndOfTrackMetaEvent()) continue;
                if (off > 0.0 && m.isMetaEvent()) continue;
                auto c = m;
                c.setTimeStamp (m.getTimeStamp() + off * tpq);
                seq.addEvent (c);
            }
        if (t == 0) appendToMidi (seq, tpq, total);
        seq.addEvent (MidiMessage::endOfTrack(), total * tpq);
        seq.sort();
        seq.updateMatchedPairs();
        out.addTrack (seq);
    }
    mf = out;
}

File Automation::exportFolder()
{
    auto f = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Racoon").getChildFile ("Patrones");
    f.createDirectory();
    return f;
}

// ===================================================================== menus
void addAutomationItems (PopupMenu& m, Automation& a, const String& id)
{
    m.addItem (9001, String::fromUTF8 ("Ver automatización"));
    m.addItem (9002, String::fromUTF8 ("Grabar sus movimientos"));
    if (a.hasPoints (id)) m.addItem (9003, String::fromUTF8 ("Borrar su automatización"));
    if (a.ccFor (id) >= 0) m.addItem (9004, String::fromUTF8 ("Al exportar viaja como MIDI CC ") + String (a.ccFor (id)), false, false);
}

bool handleAutomationMenu (int result, Component& from, const String& id)
{
    if (result < 9001 || result > 9004) return false;
    auto* opener = dynamic_cast<AutomationOpener*> (&from);
    if (opener == nullptr) opener = from.findParentComponentOfClass<AutomationOpener>();
    if (opener == nullptr) return true;
    if (result == 9001) opener->openAutomation (id, false);
    else if (result == 9002) opener->openAutomation (id, true);
    else if (result == 9003) opener->automation().removeLane (id);
    return true;
}

void exportMidiWithChooser (std::unique_ptr<FileChooser>& holder, const String& defaultName, std::function<bool (const File&)> write)
{
    holder = std::make_unique<FileChooser> ("Exportar MIDI", Automation::exportFolder().getChildFile (defaultName), "*.mid");
    holder->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                         [write] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
        if (write (f)) f.revealToUser();
        else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Exportar", String::fromUTF8 ("No se pudo escribir ") + f.getFullPathName());
    });
}

void showParamMenu (Component& from, const String& id)
{
    auto* opener = from.findParentComponentOfClass<AutomationOpener>();
    if (opener == nullptr) return;
    auto& a = opener->automation();
    PopupMenu m;
    m.addSectionHeader (a.nameOf (id));
    addAutomationItems (m, a, id);
    m.addSeparator();
    m.addItem (9010, "Valor inicial");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&from),
                     [sp = Component::SafePointer<Component> (&from), id] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (handleAutomationMenu (r, *sp, id)) return;
        if (r == 9010)
            if (auto* opener = sp->findParentComponentOfClass<AutomationOpener>())
                if (auto* p = opener->automation().parameter (id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->getDefaultValue());
                    p->endChangeGesture();
                }
    });
}
}

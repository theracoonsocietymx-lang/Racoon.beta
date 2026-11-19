#include "Devices.h"

namespace racoon::devices
{
using namespace juce;

namespace
{
const Identifier chainProp ("racoonChain"), posProp ("racoonPos"), chainVolProp ("racoonChainVol"), fxRackProp ("racoonFxRack"),
                 chainsProp ("racoonChains");
}

// ===================================================================== modulators
bool isModulatorId (const String& id) { return id == "lfo" || id == "seguidor-envolvente" || id == "forma"; }

te::Modifier::Ptr addModulator (te::Track& t, const String& id)
{
    auto* list = t.getModifierList();
    if (list == nullptr) return {};
    Identifier type = id == "lfo" ? te::IDs::LFO : id == "seguidor-envolvente" ? te::IDs::ENVELOPEFOLLOWER : te::IDs::BREAKPOINTOSCILLATOR;
    auto m = list->insertModifier (ValueTree (type), -1, nullptr);
    if (m != nullptr && type == te::IDs::ENVELOPEFOLLOWER)
        if (auto* ef = dynamic_cast<te::EnvelopeFollowerModifier*> (m.get()))
        {
            // react to the track's own sound with a useful default
            ef->depthParam->setParameter (0.8f, sendNotification);
            ef->attackParam->setParameter (10.0f, sendNotification);
            ef->releaseParam->setParameter (200.0f, sendNotification);
        }
    return m;
}

String modulatorName (te::Modifier& m)
{
    if (dynamic_cast<te::LFOModifier*> (&m) != nullptr) return "LFO";
    if (dynamic_cast<te::EnvelopeFollowerModifier*> (&m) != nullptr) return "Seguidor de envolvente";
    if (dynamic_cast<te::BreakpointOscillatorModifier*> (&m) != nullptr) return "Forma";
    if (dynamic_cast<te::StepModifier*> (&m) != nullptr) return "Pasos";
    if (dynamic_cast<te::RandomModifier*> (&m) != nullptr) return "Azar";
    return m.getName();
}

Array<te::AutomatableParameter*> modulatorKnobs (te::Modifier& m)
{
    Array<te::AutomatableParameter*> out;
    auto add = [&] (te::AutomatableParameter::Ptr p) { if (p != nullptr) out.add (p.get()); };
    if (auto* l = dynamic_cast<te::LFOModifier*> (&m))
    {
        add (l->waveParam); add (l->rateTypeParam); add (l->rateParam); add (l->depthParam); add (l->phaseParam); add (l->offsetParam); add (l->bipolarParam);
    }
    else if (auto* e = dynamic_cast<te::EnvelopeFollowerModifier*> (&m))
    {
        add (e->gainDbParam); add (e->attackParam); add (e->holdParam); add (e->releaseParam); add (e->depthParam); add (e->offsetParam);
    }
    else if (auto* b = dynamic_cast<te::BreakpointOscillatorModifier*> (&m))
    {
        add (b->numActivePointsParam); add (b->rateTypeParam); add (b->rateParam); add (b->depthParam);
        add (b->stageOneValueParam); add (b->stageTwoValueParam); add (b->stageThreeValueParam); add (b->stageFourValueParam);
    }
    else
        for (auto* p : m.getAutomatableParameters()) out.add (p);
    return out;
}

Array<te::Modifier*> modulatorsOf (te::Track& t)
{
    Array<te::Modifier*> out;
    if (auto* list = t.getModifierList())
        for (auto* m : list->getModifiers()) out.add (m);
    return out;
}

static te::AutomatableParameter::ModifierAssignment* assignmentFor (te::AutomatableParameter::ModifierSource& src, te::AutomatableParameter& target)
{
    for (auto* a : target.getAssignments())
        if (a->isForModifierSource (src)) return a;
    return nullptr;
}

Array<te::AutomatableParameter*> targetsOf (te::Modifier& m, te::Track& t)
{
    Array<te::AutomatableParameter*> out;
    for (auto* p : allParams (t))
        if (assignmentFor (m, *p) != nullptr) out.add (p);
    return out;
}

float amountFor (te::Modifier& m, te::AutomatableParameter& target)
{
    if (auto* a = assignmentFor (m, target)) return a->value.get();
    return 0.0f;
}

void map (te::Modifier& m, te::AutomatableParameter& target, float amount)
{
    if (auto* a = assignmentFor (m, target)) { a->value = amount; return; }
    target.addModifier (m, amount);
}

void unmap (te::Modifier& m, te::AutomatableParameter& target)
{
    if (auto* a = assignmentFor (m, target)) target.removeModifier (*a);
}

// ===================================================================== effect rack
bool isFxRack (const te::Plugin& p)
{
    if (auto* ri = dynamic_cast<const te::RackInstance*> (&p))
        if (ri->type != nullptr) return (bool) ri->type->state.getProperty (fxRackProp, false);
    return false;
}

te::RackType* rackOf (te::Plugin& p)
{
    if (auto* ri = dynamic_cast<te::RackInstance*> (&p)) return ri->type.get();
    return nullptr;
}

int numChains (te::RackType& r) { return jmax (1, (int) r.state.getProperty (chainsProp, 1)); }

static bool isChainVolume (te::Plugin& p) { return (bool) p.state.getProperty (chainVolProp, false); }

Array<te::Plugin*> chain (te::RackType& r, int k)
{
    Array<te::Plugin*> out;
    for (auto* p : r.getPlugins())
        if ((int) p->state.getProperty (chainProp, 0) == k && ! isChainVolume (*p)) out.add (p);
    std::stable_sort (out.begin(), out.end(), [] (te::Plugin* a, te::Plugin* b)
                      { return (double) a->state.getProperty (posProp, 0.0) < (double) b->state.getProperty (posProp, 0.0); });
    return out;
}

te::Plugin* chainVolume (te::RackType& r, int k)
{
    for (auto* p : r.getPlugins())
        if (isChainVolume (*p) && (int) p->state.getProperty (chainProp, 0) == k) return p;
    return nullptr;
}

static void ensureChainVolume (te::RackType& r, int k)
{
    if (chainVolume (r, k) != nullptr) return;
    auto vol = r.edit.getPluginCache().createNewPlugin (te::VolumeAndPanPlugin::xmlTypeName, {});
    if (vol == nullptr) return;
    vol->state.setProperty (chainVolProp, true, nullptr);
    vol->state.setProperty (chainProp, k, nullptr);
    r.addPlugin (vol, { 0.9f, (k + 1.0f) / (numChains (r) + 1.0f) }, false);
}

static void renumber (te::RackType& r, int k)
{
    auto c = chain (r, k);
    for (int i = 0; i < c.size(); ++i) c[i]->state.setProperty (posProp, (double) i, nullptr);
}

void rebuildConnections (te::RackType& r)
{
    // forget every connection and draw them again from the chain properties
    struct C { te::EditItemID s, d; int sp, dp; };
    std::vector<C> old;
    for (auto* c : r.getConnections()) old.push_back ({ c->sourceID.get(), c->destID.get(), c->sourcePin.get(), c->destPin.get() });
    for (auto& c : old) r.removeConnection (c.s, c.sp, c.d, c.dp);

    auto pins = [] (te::Plugin& p, bool input)
    {
        StringArray ins, outs;
        p.getChannelNames (&ins, &outs);
        return jlimit (0, 2, (input ? ins : outs).size());
    };
    const te::EditItemID rackIO {};
    for (int k = 0; k < numChains (r); ++k)
    {
        auto seq = chain (r, k);
        if (auto* v = chainVolume (r, k)) seq.add (v);
        te::EditItemID prev = rackIO;
        int prevOuts = 2;
        bool first = true;
        for (auto* p : seq)
        {
            const int ins = pins (*p, true);
            for (int c = 1; c <= 2; ++c)
            {
                const int from = jmin (c, jmax (1, prevOuts)), to = jmin (c, jmax (1, ins));
                if (ins > 0) r.addConnection (prev, from, p->itemID, to);
            }
            // MIDI flows down the chain too (for effects that follow notes)
            r.addConnection (first ? rackIO : prev, 0, p->itemID, 0);
            first = false;
            prev = p->itemID;
            prevOuts = pins (*p, false);
        }
        for (int c = 1; c <= 2; ++c)
            r.addConnection (prev, prev == rackIO ? c : jmin (c, jmax (1, prevOuts)), rackIO, c);
    }
}

te::Plugin::Ptr createFxRack (te::Edit& edit)
{
    auto type = edit.getRackList().addNewRack();
    if (type == nullptr) return {};
    type->rackName = "Rack de efectos";
    type->state.setProperty (fxRackProp, true, nullptr);
    type->state.setProperty (chainsProp, 1, nullptr);
    ensureChainVolume (*type, 0);
    rebuildConnections (*type);
    auto inst = edit.getPluginCache().createNewPlugin (te::RackInstance::create (*type));
    if (inst == nullptr) return {};
    auto& ml = inst->getMacroParameterListForWriting();
    for (int i = 0; i < 8; ++i)
        if (auto* m = ml.createMacroParameter())
            m->macroName = "Macro " + String (i + 1);
    return inst;
}

void addChain (te::RackType& r)
{
    const int k = numChains (r);
    r.state.setProperty (chainsProp, k + 1, &r.edit.getUndoManager());
    ensureChainVolume (r, k);
    rebuildConnections (r);
}

void removeChain (te::RackType& r, int k)
{
    const int n = numChains (r);
    if (n <= 1 || k < 0 || k >= n) return;
    Array<te::Plugin::Ptr> doomed;
    for (auto* p : r.getPlugins()) if ((int) p->state.getProperty (chainProp, 0) == k) doomed.add (p);
    for (auto& p : doomed) p->deleteFromParent();
    for (auto* p : r.getPlugins())
    {
        const int c = (int) p->state.getProperty (chainProp, 0);
        if (c > k) p->state.setProperty (chainProp, c - 1, nullptr);
    }
    r.state.setProperty (chainsProp, n - 1, &r.edit.getUndoManager());
    rebuildConnections (r);
}

te::Plugin::Ptr addToChain (te::RackType& r, int k, te::Plugin::Ptr p, int position)
{
    if (p == nullptr || ! te::RackType::isPluginAllowed (p)) return {};
    k = jlimit (0, numChains (r) - 1, k);
    auto c = chain (r, k);
    if (position < 0 || position > c.size()) position = c.size();
    p->state.setProperty (chainProp, k, nullptr);
    p->state.setProperty (posProp, position - 0.5, nullptr);
    if (! r.addPlugin (p, { (position + 1.0f) / (c.size() + 3.0f), (k + 1.0f) / (numChains (r) + 1.0f) }, false)) return {};
    renumber (r, k);
    rebuildConnections (r);
    return p;
}

void moveInChain (te::RackType& r, te::Plugin& p, int newPosition)
{
    const int k = (int) p.state.getProperty (chainProp, 0);
    auto c = chain (r, k);
    const int from = c.indexOf (&p);
    if (from < 0) return;
    c.remove (from);
    c.insert (jlimit (0, c.size(), newPosition), &p);
    for (int i = 0; i < c.size(); ++i) c[i]->state.setProperty (posProp, (double) i, nullptr);
    rebuildConnections (r);
}

void removeFromRack (te::RackType& r, te::Plugin& p)
{
    const int k = (int) p.state.getProperty (chainProp, 0);
    te::Plugin::Ptr keep (&p);
    keep->deleteFromParent();
    renumber (r, k);
    rebuildConnections (r);
}

Array<te::AutomatableParameter*> macros (te::Plugin& inst)
{
    Array<te::AutomatableParameter*> out;
    for (auto* m : inst.getMacroParameters()) out.add (m);
    return out;
}

static te::MacroParameter* asMacro (te::AutomatableParameter& p) { return dynamic_cast<te::MacroParameter*> (&p); }

void mapToMacro (te::AutomatableParameter& macro, te::AutomatableParameter& target, float amount)
{
    if (auto* m = asMacro (macro))
    {
        if (auto* a = assignmentFor (*m, target)) { a->value = amount; return; }
        target.addModifier (*m, amount);
    }
}

void unmapFromMacro (te::AutomatableParameter& macro, te::AutomatableParameter& target)
{
    if (auto* m = asMacro (macro))
        if (auto* a = assignmentFor (*m, target)) target.removeModifier (*a);
}

Array<te::AutomatableParameter*> mappedToMacro (te::AutomatableParameter& macro, te::Plugin& inst)
{
    Array<te::AutomatableParameter*> out;
    auto* m = asMacro (macro);
    auto* r = rackOf (inst);
    if (m == nullptr || r == nullptr) return out;
    for (auto* p : r->getPlugins())
        for (auto* prm : p->getAutomatableParameters())
            if (assignmentFor (*m, *prm) != nullptr) out.add (prm);
    return out;
}

// ===================================================================== external effect latency
int findDelay (const AudioBuffer<float>& sent, const AudioBuffer<float>& rec, int maxDelay)
{
    const float* a = sent.getReadPointer (0);
    const float* b = rec.getReadPointer (0);
    const int n = sent.getNumSamples();
    double best = 0.0, energy = 0.0;
    int bestLag = -1;
    for (int i = 0; i < n; ++i) energy += a[i] * a[i];
    for (int lag = 0; lag <= maxDelay && lag + n <= rec.getNumSamples(); ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += a[i] * b[i + lag];
        if (sum > best) { best = sum; bestLag = lag; }
    }
    // the loop must bring back a clear copy of the clicks (at least a fifth of their energy, any polarity aside)
    return best > energy * 0.04 ? bestLag : -1;
}

namespace
{
class LatencyProbe : public AudioIODeviceCallback, private Timer
{
public:
    LatencyProbe (te::Engine& e, te::InsertPlugin& p, int outCh, int inCh, std::function<void (bool, double, String)> cb)
        : engine (e), pluginRef (&p), plugin (&p), outChannel (outCh), inChannel (inCh), done (std::move (cb))
    {
        // a train of clicks at uneven distances, so only one delay lines them all up
        sent.setSize (1, 16000);
        sent.clear();
        int at = 200, gap = 600;
        while (at < 15000) { sent.setSample (0, at, 0.9f); sent.setSample (0, at + 1, -0.9f); at += gap; gap += 97; }
        recorded.setSize (1, 16000 + 48000);
        recorded.clear();
        engine.getDeviceManager().deviceManager.addAudioCallback (this);
        startTimer (50);
    }
    ~LatencyProbe() override { engine.getDeviceManager().deviceManager.removeAudioCallback (this); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) FloatVectorOperations::clear (out[c], n);
        int p = pos.load();
        for (int i = 0; i < n && p < recorded.getNumSamples(); ++i, ++p)
        {
            if (outChannel < numOut && out[outChannel] != nullptr) out[outChannel][i] = p < sent.getNumSamples() ? sent.getSample (0, p) : 0.0f;
            if (inChannel < numIn && in[inChannel] != nullptr) recorded.setSample (0, p, in[inChannel][i]);
        }
        pos.store (p);
    }
    void audioDeviceAboutToStart (AudioIODevice* d) override { if (d != nullptr) rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

private:
    te::Engine& engine;
    te::Plugin::Ptr pluginRef;
    te::InsertPlugin* plugin = nullptr;
    int outChannel, inChannel;
    std::function<void (bool, double, String)> done;
    AudioBuffer<float> sent, recorded;
    std::atomic<int> pos { 0 };
    double rate = 48000.0;
    int waited = 0;

    void timerCallback() override
    {
        if (pos.load() < recorded.getNumSamples() && ++waited < 100) return;
        stopTimer();
        engine.getDeviceManager().deviceManager.removeAudioCallback (this);
        auto* dev = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        if (dev != nullptr) rate = dev->getCurrentSampleRate();
        const int lag = pos.load() >= recorded.getNumSamples() ? findDelay (sent, recorded, 48000) : -1;
        auto cb = done;
        if (lag < 0)
            cb (false, 0.0, "No regresó el clic: revisa que el cable salga por la salida elegida y vuelva por la entrada elegida."_u);
        else
        {
            // Tracktion already adds what the interface reports; the correction is the rest of the trip
            const int reported = dev != nullptr ? dev->getOutputLatencyInSamples() + dev->getInputLatencyInSamples() : 0;
            const double ms = (lag - reported) * 1000.0 / rate;
            plugin->manualAdjustMs = ms;
            cb (true, lag * 1000.0 / rate, "Retraso medido: "_u + String (lag * 1000.0 / rate, 2) + " ms. Corrección aplicada: "_u + String (ms, 2) + " ms.");
        }
        delete this;
    }
};
}

void measureInsertLatency (te::Engine& e, te::InsertPlugin& p, std::function<void (bool, double, String)> cb)
{
    auto& dm = e.getDeviceManager();
    auto* out = dynamic_cast<te::WaveOutputDevice*> (dm.findOutputDeviceWithName (p.outputDevice.get()));
    auto* in = dynamic_cast<te::WaveInputDevice*> (dm.findInputDeviceWithName (p.inputDevice.get()));
    if (out == nullptr || in == nullptr || out->getChannels().empty() || in->getChannels().empty())
    {
        cb (false, 0.0, "Primero elige por dónde sale y por dónde vuelve el sonido."_u);
        return;
    }
    new LatencyProbe (e, p, out->getChannels()[0].indexInDevice, in->getChannels()[0].indexInDevice, std::move (cb));
}

Array<te::AutomatableParameter*> allParams (te::Track& t)
{
    auto out = t.getAllAutomatableParams();
    for (auto* p : t.pluginList.getPlugins())
        if (auto* r = rackOf (*p); r != nullptr && isFxRack (*p))
            for (auto* inner : r->getPlugins())
                for (auto* prm : inner->getAutomatableParameters()) out.add (prm);
    return out;
}
}

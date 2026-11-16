#include "Device.h"
#include "../Processor.h"

namespace fal::daw
{
// the holder owns the mirror parameters so they have an index, gestures and listeners like any other parameter
struct Device::Holder : juce::AudioProcessor
{
    Holder() : juce::AudioProcessor (BusesProperties()) {}
    const juce::String getName() const override { return "studio"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

Device::~Device()
{
    if (proc) proc->removeListener (this);
    if (holder) holder->removeListener (this);
    if (proc) proc->releaseResources();
}

bool Device::isInstrument() const
{
    if (kind == Kind::Internal) return info (pid).kind != fal::Kind::Effect;
    if (kind == Kind::Hosted) return desc.isInstrument;
    return false;
}

void Device::buildMirrors (std::function<juce::String (int)> nameOf, std::function<juce::String (int, float)> textOf, std::function<float (int)> defOf, int n)
{
    holder = std::make_unique<Holder>();
    mirrors.clear();
    for (int i = 0; i < n; ++i)
    {
        auto attr = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([textOf, i] (float v, int) { return textOf (i, v); });
        auto* p = new juce::AudioParameterFloat (juce::ParameterID { "p" + juce::String (i), 1 }, nameOf (i), juce::NormalisableRange<float> (0.0f, 1.0f), defOf (i), attr);
        holder->addParameter (p);
        mirrors.push_back (p);
    }
    holder->addListener (this);
    finals = std::vector<std::atomic<float>> ((size_t) n);
    lockOn = std::vector<std::atomic<int>> ((size_t) n);
    gestureOn = std::vector<std::atomic<int>> ((size_t) n);
    for (int i = 0; i < n; ++i) { finals[(size_t) i].store (defOf (i)); lockOn[(size_t) i].store (0); gestureOn[(size_t) i].store (0); }
    lastWritten.assign ((size_t) n, -1.0f);
    lockVal.assign ((size_t) n, 0.0f); slideFrom.assign ((size_t) n, 0.0f); slideTo.assign ((size_t) n, 0.0f);
    slideA.assign ((size_t) n, 0.0); slideB.assign ((size_t) n, 0.0);
    modOffset.assign ((size_t) n, 0.0f);
    macro = std::vector<Macro> ((size_t) n);
}

std::unique_ptr<Device> Device::makeInternal (int uid, Pid pid)
{
    std::unique_ptr<Device> d (new Device());
    d->uid = uid; d->kind = Kind::Internal; d->pid = pid;
    d->typeId = "fal:" + juce::String (info (pid).code);
    d->name = juce::String (info (pid).code) + " " + info (pid).name;
    auto p = std::make_unique<fal::Processor> (pid);
    auto& params = p->getParameters();
    auto* raw = p.get();
    d->buildMirrors ([raw] (int i) { return raw->getParameters()[i]->getName (40); },
                     [raw] (int i, float v) { return raw->getParameters()[i]->getText (v, 24); },
                     [raw] (int i) { return raw->getParameters()[i]->getValue(); }, params.size());
    d->proc = std::move (p);
    d->proc->addListener (d.get());
    return d;
}

std::unique_ptr<Device> Device::makeHosted (int uid, std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& desc)
{
    std::unique_ptr<Device> d (new Device());
    d->uid = uid; d->kind = Kind::Hosted; d->desc = desc;
    d->typeId = desc.pluginFormatName.toLowerCase() + ":" + desc.createIdentifierString();
    d->name = desc.name;
    auto* raw = inst.get();
    // a plain stereo pair in and out; side-chains and extra outputs stay off
    auto layout = inst->getBusesLayout();
    for (int i = 0; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    for (int i = 0; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (! inst->checkBusesLayoutSupported (layout) || ! inst->setBusesLayout (layout))
    {
        auto l2 = inst->getBusesLayout();
        if (l2.outputBuses.size() > 0) l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        inst->setBusesLayout (l2);
    }
    d->buildMirrors ([raw] (int i) { return raw->getParameters()[i]->getName (40); },
                     [raw] (int i, float v) { return raw->getParameters()[i]->getText (v, 24); },
                     [raw] (int i) { return raw->getParameters()[i]->getValue(); }, inst->getParameters().size());
    d->proc = std::move (inst);
    d->proc->addListener (d.get());
    return d;
}

juce::String mixerParamName (int i)
{
    switch (i) { case MP_LEVEL: return "level"; case MP_PAN: return "pan"; case MP_MUTE: return "mute"; default: return "send " + juce::String (i - MP_SEND1 + 1); }
}
juce::String mixerParamText (int i, float v)
{
    switch (i)
    {
        case MP_LEVEL: return v <= 0.001f ? juce::String ("off") : (mixerLevelDb (v) > 0 ? "+" : "") + juce::String (mixerLevelDb (v), 1) + " db";
        case MP_PAN: { const int p = juce::roundToInt ((v * 2.0f - 1.0f) * 50.0f); return p == 0 ? juce::String ("c") : (p < 0 ? "l " + juce::String (-p) : "r " + juce::String (p)); }
        case MP_MUTE: return v > 0.5f ? "muted" : "on";
        default: return v <= 0.001f ? juce::String ("off") : juce::String (-48.0f + 48.0f * v, 1) + " db";
    }
}
juce::String globalParamName (int i)
{
    if (i < kMacros) return "macro " + juce::String (i + 1);
    switch (i) { case GP_TEMPO: return "tempo"; case GP_SWING: return "swing"; default: return "master"; }
}
juce::String globalParamText (int i, float v)
{
    if (i < kMacros) return juce::String (juce::roundToInt (v * 100)) + "%";
    switch (i)
    {
        case GP_TEMPO: return juce::String (tempoFromNorm (v), 1) + " bpm";
        case GP_SWING: return juce::String (juce::roundToInt (swingFromNorm (v) * 100)) + "%";
        default: return mixerParamText (MP_LEVEL, v);
    }
}

std::unique_ptr<Device> Device::makeMixer()
{
    std::unique_ptr<Device> d (new Device());
    d->uid = 0; d->kind = Kind::Mixer; d->typeId = "mixer"; d->name = "mixer";
    d->buildMirrors (mixerParamName, mixerParamText, [] (int i) { return i == MP_LEVEL ? 0.8f : i == MP_PAN ? 0.5f : 0.0f; }, MP_COUNT);
    return d;
}
std::unique_ptr<Device> Device::makeGlobal()
{
    std::unique_ptr<Device> d (new Device());
    d->uid = 0; d->kind = Kind::Global; d->typeId = "global"; d->name = "global";
    d->buildMirrors (globalParamName, globalParamText, [] (int i) { return i == GP_TEMPO ? tempoToNorm (124.0f) : i == GP_SWING ? 0.16f : i == GP_MASTER ? 0.8f : 0.0f; }, GP_COUNT);
    return d;
}

juce::String Device::paramName (int i) const { return i >= 0 && i < numParams() ? mirrors[(size_t) i]->getName (40) : juce::String(); }
juce::String Device::paramText (int i, float v) const { return i >= 0 && i < numParams() ? mirrors[(size_t) i]->getText (v, 24) : juce::String(); }
juce::String Device::paramId (int i) const
{
    if (proc && i >= 0 && i < proc->getParameters().size())
    {
        if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (proc->getParameters()[i])) return h->getParameterID();
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (proc->getParameters()[i])) return r->getParameterID();
    }
    return "p" + juce::String (i);
}
int Device::paramIndexForId (const juce::String& id) const
{
    for (int i = 0; i < numParams(); ++i) if (paramId (i) == id) return i;
    return -1;
}

void Device::setBase (int i, float v, bool notify)
{
    if (i < 0 || i >= numParams()) return;
    if (notify) { writing.store (true); mirrors[(size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v)); writing.store (false); }
    else mirrors[(size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

// ---- listeners -------------------------------------------------------------------------------------
void Device::audioProcessorParameterChanged (juce::AudioProcessor* p, int idx, float v)
{
    if (writing.load()) return;
    if (p == proc.get())
    {
        // the device's own window moved a control: that becomes the base value
        if (idx >= 0 && idx < numParams())
        {
            writing.store (true);
            mirrors[(size_t) idx]->setValueNotifyingHost (v);
            lastWritten[(size_t) idx] = v;
            writing.store (false);
            if (onEdited) onEdited (idx, v);
        }
    }
    else if (p == holder.get())
    {
        if (onEdited) onEdited (idx, v);
    }
}
void Device::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) {}
void Device::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int idx) { if (onGesture) onGesture (idx, true); }
void Device::audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int idx) { if (onGesture) onGesture (idx, false); }

// ---- audio thread ---------------------------------------------------------------------------------
void Device::prepare (double sr, int bs)
{
    if (proc)
    {
        proc->setRateAndBufferSizeDetails (sr, bs);
        proc->prepareToPlay (sr, bs);
        scratchChannels = std::max (2, std::max (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels()));
        scratch.setSize (scratchChannels, bs, false, false, true);
    }
    std::fill (lastWritten.begin(), lastWritten.end(), -1.0f);
    prepared = true;
}
void Device::release() { if (proc && prepared) proc->releaseResources(); prepared = false; }

void Device::lock (int param, float v, bool slide, float to, double a, double b)
{
    if (param < 0 || param >= numParams()) return;
    lockVal[(size_t) param] = v;
    slideFrom[(size_t) param] = v; slideTo[(size_t) param] = slide ? to : v;
    slideA[(size_t) param] = a; slideB[(size_t) param] = std::max (a + 1.0e-6, b);
    lockOn[(size_t) param].store (slide ? 2 : 1, std::memory_order_relaxed);
}
void Device::unlockAll() { for (auto& l : lockOn) l.store (0, std::memory_order_relaxed); }
bool Device::clearLocksExcept (const Lock* keep, int n)
{
    bool dropped = false;
    for (int i = 0; i < numParams(); ++i)
    {
        const int lk = lockOn[(size_t) i].load (std::memory_order_relaxed);
        if (lk == 0 || lk == 2) continue;   // a slide keeps going until the step it slides to
        bool kept = false;
        for (int k = 0; k < n; ++k) if (keep[k].param == i && keep[k].device == uid) { kept = true; break; }
        if (! kept) { lockOn[(size_t) i].store (0, std::memory_order_relaxed); dropped = true; }
    }
    return dropped;
}
int Device::paramIndexForName (const juce::String& name) const
{
    for (int i = 0; i < numParams(); ++i) if (paramName (i) == name) return i;
    return -1;
}

void Device::apply (const float* macros, double ppq)
{
    const int n = numParams();
    auto& params = proc ? proc->getParameters() : holder->getParameters();
    for (int i = 0; i < n; ++i)
    {
        float v = mirrors[(size_t) i]->getValue();
        const int m = macro[(size_t) i].index.load (std::memory_order_relaxed);
        if (m >= 0 && m < kMacros) v = macro[(size_t) i].lo.load() + (macro[(size_t) i].hi.load() - macro[(size_t) i].lo.load()) * macros[m];
        v += modOffset[(size_t) i];
        const int lk = lockOn[(size_t) i].load (std::memory_order_relaxed);
        if (lk == 1) v = lockVal[(size_t) i] + modOffset[(size_t) i];
        else if (lk == 2)
        {
            const double t = juce::jlimit (0.0, 1.0, (ppq - slideA[(size_t) i]) / (slideB[(size_t) i] - slideA[(size_t) i]));
            v = slideFrom[(size_t) i] + (slideTo[(size_t) i] - slideFrom[(size_t) i]) * (float) t + modOffset[(size_t) i];
        }
        v = juce::jlimit (0.0f, 1.0f, v);
        finals[(size_t) i].store (v, std::memory_order_relaxed);
        if (proc && std::abs (v - lastWritten[(size_t) i]) > 1.0e-6f)
        {
            writing.store (true, std::memory_order_relaxed);
            params[i]->setValueNotifyingHost (v);
            writing.store (false, std::memory_order_relaxed);
            lastWritten[(size_t) i] = v;
        }
    }
}

void Device::process (juce::AudioBuffer<float>& stereo, juce::MidiBuffer& midi)
{
    if (! proc || ! prepared) return;
    if (bypass.load (std::memory_order_relaxed)) { midi.clear(); return; }
    const int n = stereo.getNumSamples();
    if (kind == Kind::Internal)
    {
        proc->processBlock (stereo, midi);
        return;
    }
    if (scratch.getNumSamples() < n) scratch.setSize (scratchChannels, n, false, false, true);
    juce::AudioBuffer<float> b (scratch.getArrayOfWritePointers(), scratchChannels, n);
    b.clear();
    const int ins = proc->getTotalNumInputChannels();
    for (int c = 0; c < std::min (2, ins); ++c) b.copyFrom (c, 0, stereo, c, 0, n);
    proc->processBlock (b, midi);
    const int outs = proc->getTotalNumOutputChannels();
    for (int c = 0; c < 2; ++c)
    {
        if (c < outs) stereo.copyFrom (c, 0, b, c, 0, n);
        else if (outs == 1) stereo.copyFrom (c, 0, b, 0, 0, n);
        else stereo.clear (c, 0, n);
    }
}

// ---- state ----------------------------------------------------------------------------------------
void Device::save (juce::XmlElement& e) const
{
    e.setAttribute ("uid", uid);
    e.setAttribute ("type", typeId);
    e.setAttribute ("name", name);
    if (bypass.load()) e.setAttribute ("bypass", 1);
    if (kind == Kind::Hosted) e.addChildElement (desc.createXml().release());
    if (proc)
    {
        juce::MemoryBlock mb; proc->getStateInformation (mb);
        auto* s = e.createNewChildElement ("state");
        s->addTextElement (mb.toBase64Encoding());
    }
    for (int i = 0; i < numParams(); ++i)
    {
        const float v = baseValue (i);
        const int m = macro[(size_t) i].index.load();
        if (std::abs (v - defaultValue (i)) < 1.0e-6f && m < 0) continue;
        auto* p = e.createNewChildElement ("p");
        p->setAttribute ("id", paramId (i)); p->setAttribute ("v", v);
        if (m >= 0) { p->setAttribute ("m", m); p->setAttribute ("lo", macro[(size_t) i].lo.load()); p->setAttribute ("hi", macro[(size_t) i].hi.load()); }
    }
}

void Device::load (const juce::XmlElement& e)
{
    name = e.getStringAttribute ("name", name);
    bypass.store (e.getIntAttribute ("bypass", 0) != 0);
    if (proc)
        if (auto* s = e.getChildByName ("state"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (s->getAllSubText().trim()) && mb.getSize() > 0) proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
    pullBases();
    for (auto* p : e.getChildWithTagNameIterator ("p"))
    {
        const int i = paramIndexForId (p->getStringAttribute ("id"));
        if (i < 0) continue;
        setBase (i, (float) p->getDoubleAttribute ("v"));
        if (p->hasAttribute ("m"))
        {
            macro[(size_t) i].index.store (p->getIntAttribute ("m"));
            macro[(size_t) i].lo.store ((float) p->getDoubleAttribute ("lo", 0.0)); macro[(size_t) i].hi.store ((float) p->getDoubleAttribute ("hi", 1.0));
        }
    }
}

void Device::setProgram (int i)
{
    if (! proc || i < 0 || i >= proc->getNumPrograms()) return;
    proc->setCurrentProgram (i);
    pullBases();
}

void Device::pullBases()
{
    if (! proc) return;
    auto& params = proc->getParameters();
    writing.store (true);
    for (int i = 0; i < numParams() && i < params.size(); ++i)
    {
        const float v = params[i]->getValue();
        mirrors[(size_t) i]->setValueNotifyingHost (v);
        lastWritten[(size_t) i] = v;
    }
    writing.store (false);
}
} // namespace fal::daw

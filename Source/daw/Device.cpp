#include "Device.h"
#include "../Processor.h"
#include "devices/StudioDevices.h"

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
    if (kind == Kind::Studio) return proc && proc->acceptsMidi() && proc->getTotalNumInputChannels() == 0;
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
    modLo = std::vector<std::atomic<float>> ((size_t) n);
    modHi = std::vector<std::atomic<float>> ((size_t) n);
    lockOn = std::vector<std::atomic<int>> ((size_t) n);
    gestureOn = std::vector<std::atomic<int>> ((size_t) n);
    for (int i = 0; i < n; ++i) { finals[(size_t) i].store (defOf (i)); lockOn[(size_t) i].store (0); gestureOn[(size_t) i].store (0); modLo[(size_t) i].store (0); modHi[(size_t) i].store (0); }
    lastWritten.assign ((size_t) n, -1.0f);
    lockVal.assign ((size_t) n, 0.0f); slideFrom.assign ((size_t) n, 0.0f); slideTo.assign ((size_t) n, 0.0f);
    slideA.assign ((size_t) n, 0.0); slideB.assign ((size_t) n, 0.0);
    modOffset.assign ((size_t) n, 0.0f);
    macro = std::vector<Macro> ((size_t) n);
}

static void mirrorsFromProcessor (Device& d, juce::AudioProcessor* raw, std::unique_ptr<Device>& out,
                                  void (Device::*build) (std::function<juce::String (int)>, std::function<juce::String (int, float)>, std::function<float (int)>, int))
{
    (d.*build) ([raw] (int i) { return raw->getParameters()[i]->getName (40); },
                [raw] (int i, float v) { return raw->getParameters()[i]->getText (v, 24); },
                [raw] (int i) { return raw->getParameters()[i]->getValue(); }, raw->getParameters().size());
    juce::ignoreUnused (out);
}

std::unique_ptr<Device> Device::makeInternal (int uid, Pid pid)
{
    std::unique_ptr<Device> d (new Device());
    d->uid = uid; d->kind = Kind::Internal; d->pid = pid;
    d->typeId = "fal:" + juce::String (info (pid).code);
    d->name = juce::String (info (pid).code) + " " + info (pid).name;
    auto p = std::make_unique<fal::Processor> (pid);
    mirrorsFromProcessor (*d, p.get(), d, &Device::buildMirrors);
    d->proc = std::move (p);
    d->proc->addListener (d.get());
    d->inWidth = d->isInstrument() ? 0 : 2; d->outWidth = 2;
    return d;
}

std::unique_ptr<Device> Device::makeStudio (int uid, const juce::String& typeId)
{
    auto p = createStudioProcessor (typeId);
    if (! p) return nullptr;
    return makeFromProcessor (uid, std::move (p), typeId, studioDeviceName (typeId));
}
std::unique_ptr<Device> Device::makeFromProcessor (int uid, std::unique_ptr<juce::AudioProcessor> p, const juce::String& typeId, const juce::String& name)
{
    std::unique_ptr<Device> d (new Device());
    d->uid = uid; d->kind = Kind::Studio; d->typeId = typeId; d->name = name;
    mirrorsFromProcessor (*d, p.get(), d, &Device::buildMirrors);
    d->proc = std::move (p);
    d->proc->addListener (d.get());
    d->inWidth = d->proc->getTotalNumInputChannels(); d->outWidth = d->proc->getTotalNumOutputChannels();
    return d;
}

juce::AudioChannelSet channelSetFor (int width)
{
    switch (width)
    {
        case 1: return juce::AudioChannelSet::mono();
        case 2: return juce::AudioChannelSet::stereo();
        case 4: return juce::AudioChannelSet::ambisonic (1);
        case 9: return juce::AudioChannelSet::ambisonic (2);
        case 16: return juce::AudioChannelSet::ambisonic (3);
        case 25: return juce::AudioChannelSet::ambisonic (4);
        case 36: return juce::AudioChannelSet::ambisonic (5);
        case 49: return juce::AudioChannelSet::ambisonic (6);
        case 64: return juce::AudioChannelSet::ambisonic (7);
        default: return juce::AudioChannelSet::discreteChannels (width);
    }
}

std::unique_ptr<Device> Device::makeHosted (int uid, std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& desc)
{
    std::unique_ptr<Device> d (new Device());
    d->uid = uid; d->kind = Kind::Hosted; d->desc = desc;
    d->typeId = desc.pluginFormatName.toLowerCase() + ":" + desc.createIdentifierString();
    d->name = desc.name;
    auto* raw = inst.get();
    mirrorsFromProcessor (*d, raw, d, &Device::buildMirrors);
    d->proc = std::move (inst);
    d->proc->addListener (d.get());
    // what the plug-in is made for shows in the layout it arrives with: a binaural decoder comes with a stereo output,
    // an encoder with a stereo input, however permissive they are about other layouts
    d->defIn = d->proc->getTotalNumInputChannels(); d->defOut = d->proc->getTotalNumOutputChannels();
    d->defLayout = d->proc->getBusesLayout();
    d->configure (2, 2);
    return d;
}

int Device::configure (int incoming, int trackWidth)
{
    if (kind == Kind::Internal) { inWidth = isInstrument() ? 0 : 2; outWidth = 2; return outWidth; }
    if (kind == Kind::Studio)
    {
        if (proc)
        {
            // studio devices follow the chain width when they can (their buses accept any discrete layout); encoders
            // and decoders say themselves what they read and write
            auto l = proc->getBusesLayout();
            int wantIn = juce::jlimit (1, kMaxWidth, incoming > 0 ? incoming : trackWidth), wantOut = wantIn;
            if (auto* neg = dynamic_cast<WidthNegotiator*> (proc.get())) { const auto io = neg->widthsFor (incoming, trackWidth); wantIn = io.first; wantOut = io.second; }
            if (l.inputBuses.size() > 0 && proc->getTotalNumInputChannels() > 0) l.inputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (wantIn);
            if (l.outputBuses.size() > 0) l.outputBuses.getReference (0) = juce::AudioChannelSet::discreteChannels (wantOut);
            if (l != proc->getBusesLayout() && proc->checkBusesLayoutSupported (l))
            {
                if (prepared) proc->releaseResources();
                proc->setBusesLayout (l);
                if (prepared)
                {
                    const double rate = proc->getSampleRate() > 0 ? proc->getSampleRate() : 48000.0; const int block = std::max (16, proc->getBlockSize());
                    proc->setRateAndBufferSizeDetails (rate, block);
                    proc->prepareToPlay (rate, block);
                }
            }
            inWidth = proc->getTotalNumInputChannels(); outWidth = proc->getTotalNumOutputChannels();
            scratchChannels = std::max (2, std::max (inWidth, outWidth));
            if (scratch.getNumChannels() < scratchChannels) scratch.setSize (scratchChannels, std::max (16, scratch.getNumSamples()), false, false, true);
        }
        return outWidth;
    }
    if (kind != Kind::Hosted || ! proc) return outWidth;
    // hosted: the main input takes the incoming width, the main output the widest layout the plug-in offers up to the
    // track's width (an encoder: 2 in, 16 out; a decoder: 16 in, 2 out); other buses stay off
    if (prepared) proc->releaseResources();   // bus layouts change only while the plug-in is released
    auto layout = proc->getBusesLayout();
    for (int i = 1; i < layout.inputBuses.size(); ++i) layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();
    auto tryLayout = [&] (int in, int out)
    {
        auto l = layout;
        if (l.inputBuses.size() > 0) l.inputBuses.getReference (0) = in <= 0 ? juce::AudioChannelSet::disabled() : channelSetFor (in);
        if (l.outputBuses.size() > 0) l.outputBuses.getReference (0) = channelSetFor (out);
        // (some plug-ins say yes to a layout and then settle on another channel count: only what they really took counts)
        if (proc->checkBusesLayoutSupported (l) && proc->setBusesLayout (l))
            return proc->getTotalNumOutputChannels() == out && (in <= 0 || proc->getTotalNumInputChannels() == in);
        return false;
    };
    const bool hasIn = layout.inputBuses.size() > 0 && ! desc.isInstrument;
    // a plug-in that comes stereo on a side stays stereo there (an encoder's input, a binaural decoder's output, any
    // plain stereo effect); a side that comes wide takes what the track has
    const int inWant = hasIn ? (defIn <= 2 ? juce::jlimit (1, std::max (1, defIn), std::max (1, incoming)) : std::max (1, incoming)) : 0;
    const int outWant = defOut <= 2 ? juce::jlimit (1, std::max (1, defOut), trackWidth) : trackWidth;
    // the count the track has, or the next layouts a plug-in format can name that are at least as wide (vst3 has no
    // "24 discrete channels": a decoder for 24 speakers takes 25, the next ambisonic count, and the track uses 24)
    auto sizesFrom = [] (int want) { std::vector<int> v { want }; for (int a : { 4, 9, 16, 25, 36, 49, 64 }) if (a > want) v.push_back (a); return v; };
    bool ok = false;
    for (int out : sizesFrom (outWant))
    {
        if (ok || (defOut <= 2 && out > 2)) break;
        if (! hasIn) { ok = tryLayout (0, out); continue; }
        for (int in : sizesFrom (inWant)) { if (tryLayout (in, out)) { ok = true; break; } if (defIn <= 2) break; }
    }
    for (int out : { trackWidth, 64, 49, 36, 25, 16, 9, 8, 6, 4, 2, 1 })
    {
        if (ok) break;
        if (out > trackWidth || out <= 0 || (defOut <= 2 && out > 2)) continue;
        ok = tryLayout (inWant, out);
    }
    if (! ok && hasIn)
        for (int in : { 2, 1, 16, 4, 9, 25, 36, 49, 64 })
            for (int out : { outWant, 16, 2, 1 })
                if (! ok && out <= std::max (2, trackWidth) && tryLayout (in, out)) ok = true;
    if (! ok && ! tryLayout (hasIn ? 2 : 0, 2))
        if (proc->checkBusesLayoutSupported (defLayout)) proc->setBusesLayout (defLayout);   // as it arrived
    // what the track gives it and takes from it (the buses themselves may be wider)
    inWidth = hasIn ? std::min (proc->getTotalNumInputChannels(), std::max (1, incoming)) : 0;
    outWidth = std::max (1, std::min (proc->getTotalNumOutputChannels(), std::max (outWant, 1)));
    if (prepared)
    {
        const double rate = proc->getSampleRate() > 0 ? proc->getSampleRate() : 48000.0; const int block = std::max (16, proc->getBlockSize());
        proc->setRateAndBufferSizeDetails (rate, block);
        proc->prepareToPlay (rate, block);
    }
    scratchChannels = std::max (2, std::max (inWidth, outWidth));
    scratch.setSize (scratchChannels, std::max (16, scratch.getNumSamples()), false, false, true);
    return outWidth;
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
        default: return v <= 0.001f ? juce::String ("off") : (v > 0.889f ? "+" : "") + juce::String (-48.0f + 54.0f * v, 1) + " db";
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
int Device::paramIndexForName (const juce::String& name) const
{
    for (int i = 0; i < numParams(); ++i) if (paramName (i) == name) return i;
    return -1;
}

void Device::setBase (int i, float v, bool notify)
{
    if (i < 0 || i >= numParams()) return;
    v = juce::jlimit (0.0f, 1.0f, v);
    if (notify) { writing.store (true); mirrors[(size_t) i]->setValueNotifyingHost (v); writing.store (false); }
    else mirrors[(size_t) i]->setValueNotifyingHost (v);
    if (! proc) finals[(size_t) i].store (v, std::memory_order_relaxed);   // the mixer and the globals: readable at once (the next block refines it)
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
        // the processor's own smoothers settle on the parameter values they find at prepare, so the bases go in first:
        // a render right after loading then sounds the same as the next one
        auto& params = proc->getParameters();
        for (int i = 0; i < numParams() && i < params.size(); ++i)
        {
            const float v = mirrors[(size_t) i]->getValue();
            if (std::abs (params[i]->getValue() - v) > 1.0e-6f)
            {
                writing.store (true, std::memory_order_relaxed);
                params[i]->setValueNotifyingHost (v);
                writing.store (false, std::memory_order_relaxed);
            }
        }
        proc->setRateAndBufferSizeDetails (sr, bs);
        proc->prepareToPlay (sr, bs);
        scratchChannels = std::max (2, std::max (proc->getTotalNumInputChannels(), proc->getTotalNumOutputChannels()));
        scratch.setSize (scratchChannels, bs, false, false, true);
        if (kind == Kind::Studio) { inWidth = proc->getTotalNumInputChannels(); outWidth = proc->getTotalNumOutputChannels(); }
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

void Device::apply (const float* macros, double ppq)
{
    const int n = numParams();
    auto& params = proc ? proc->getParameters() : holder->getParameters();
    for (int i = 0; i < n; ++i)
    {
        float v = mirrors[(size_t) i]->getValue();
        const int m = macro[(size_t) i].index.load (std::memory_order_relaxed);
        if (macros && m >= 0 && m < kMacros) v = macro[(size_t) i].lo.load() + (macro[(size_t) i].hi.load() - macro[(size_t) i].lo.load()) * macros[m];
        const float off = modOffset[(size_t) i];
        v += off;
        const int lk = lockOn[(size_t) i].load (std::memory_order_relaxed);
        if (lk == 1) v = lockVal[(size_t) i] + off;
        else if (lk == 2)
        {
            const double t = juce::jlimit (0.0, 1.0, (ppq - slideA[(size_t) i]) / (slideB[(size_t) i] - slideA[(size_t) i]));
            v = slideFrom[(size_t) i] + (slideTo[(size_t) i] - slideFrom[(size_t) i]) * (float) t + off;
        }
        v = juce::jlimit (0.0f, 1.0f, v);
        finals[(size_t) i].store (v, std::memory_order_relaxed);
        modLo[(size_t) i].store (std::min (0.0f, off), std::memory_order_relaxed);
        modHi[(size_t) i].store (std::max (0.0f, off), std::memory_order_relaxed);
        if (proc && std::abs (v - lastWritten[(size_t) i]) > 1.0e-6f)
        {
            writing.store (true, std::memory_order_relaxed);
            params[i]->setValueNotifyingHost (v);
            writing.store (false, std::memory_order_relaxed);
            lastWritten[(size_t) i] = v;
        }
    }
}

void Device::process (juce::AudioBuffer<float>& track, juce::MidiBuffer& midi)
{
    if (! proc || ! prepared) return;
    const int n = track.getNumSamples(), width = track.getNumChannels();
    if (bypass.load (std::memory_order_relaxed)) { midi.clear(); return; }
    if (kind == Kind::Internal && width == 2)
    {
        proc->processBlock (track, midi);
        return;
    }
    const int ins = kind == Kind::Internal ? (isInstrument() ? 0 : 2) : proc->getTotalNumInputChannels();
    const int outs = kind == Kind::Internal ? 2 : proc->getTotalNumOutputChannels();
    const int ch = std::max (2, std::max (ins, outs));
    if (scratch.getNumChannels() < ch || scratch.getNumSamples() < n) { scratchChannels = ch; scratch.setSize (ch, std::max (n, scratch.getNumSamples()), false, false, true); }
    juce::AudioBuffer<float> b (scratch.getArrayOfWritePointers(), kind == Kind::Internal ? 2 : ch, n);
    b.clear();
    for (int c = 0; c < std::min (ins, width); ++c) b.copyFrom (c, 0, track, c, 0, n);
    if (kind == Kind::Hosted || kind == Kind::Studio)
        if (auto* kp = dynamic_cast<KeyedProcessor*> (proc.get())) kp->setKey (keyL, keyR);
    proc->processBlock (b, midi);
    // the device replaces the channels it read with the ones it wrote; channels beyond both pass through untouched
    const int top = std::max (ins, outs);
    for (int c = 0; c < width; ++c)
    {
        if (c < outs) track.copyFrom (c, 0, b, c, 0, n);
        else if (outs == 1 && ins <= 1 && c == 1) track.copyFrom (c, 0, b, 0, 0, n);
        else if (c < top) track.clear (c, 0, n);
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
        // (a parameter a modulator or a lock is moving right now is written even at its default: the state blob above
        // holds the moved value, and the base must win when the project comes back)
        const float live = proc && i < proc->getParameters().size() ? proc->getParameters()[i]->getValue() : v;
        if (std::abs (v - defaultValue (i)) < 1.0e-6f && m < 0 && std::abs (live - v) < 1.0e-6f) continue;
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

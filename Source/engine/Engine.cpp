#include "Engine.h"
#include "PluginHost.h"

namespace um
{
namespace
{
juce::String configKey (const IOConfig& c)
{
    juce::String k;
    for (auto& d : c.inputDevices) k << "i:" << d << ";";
    for (auto& d : c.outputDevices) k << "o:" << d << ";";
    for (auto& t : c.taps)
    {
        k << "t:" << (int) t.kind << "," << (t.muteSource ? 1 : 0) << "," << (t.mono ? 1 : 0) << "," << t.deviceUid << ",";
        for (auto p : t.pids) k << p << " ";
        for (auto& b : t.bundleIds) k << b << " ";
        k << ";";
    }
    k << "c:" << c.clockDevice << " r:" << c.sampleRate << " b:" << c.bufferSize;
    return k;
}
std::vector<pid_t> pidList (const juce::String& s) { std::vector<pid_t> v; for (auto& t : juce::StringArray::fromTokens (s, " ,", "")) if (t.getIntValue() > 0) v.push_back ((pid_t) t.getIntValue()); return v; }
std::vector<juce::String> strList (const juce::String& s) { std::vector<juce::String> v; for (auto& t : juce::StringArray::fromTokens (s, " ,", "")) if (t.isNotEmpty()) v.push_back (t); return v; }
}

Engine::Engine (Model& m, PluginHost* p) : model (m), plugins (p)
{
    formats.registerBasicFormats();
    writerThread.startThread();
    model.root.addListener (this);
    coreAudio.onDevicesChanged = [this] { dirty = true; };
    coreAudio.onProcessesChanged = [this] { dirty = true; };
    coreAudio.onStoppedUnexpectedly = [this] (juce::String why) { error = why; dirty = true; };
    startTimerHz (10);
}
Engine::~Engine()
{
    stopTimer();
    model.root.removeListener (this);
    coreAudio.stop();
    { const juce::SpinLock::ScopedLockType l (planLock); plan.reset(); }
    for (auto& [id, rt] : rts) { rt->writer.store (nullptr); rt->writerOwned.reset(); }
    rts.clear();
    writerThread.stopThread (2000);
}

BlockRt* Engine::runtime (int id) { auto it = rts.find (id); return it == rts.end() ? nullptr : it->second.get(); }
std::shared_ptr<BlockRt> Engine::runtimeShared (int id) { auto it = rts.find (id); return it == rts.end() ? nullptr : it->second; }

juce::String Engine::resolveOutput (const juce::ValueTree& b) const
{
    const auto d = b[ids::device].toString();
    return d.isEmpty() ? CoreAudioHost::defaultOutputUid() : d;   // empty: the system's output, following it when it changes
}

IOConfig Engine::wantedConfig()
{
    IOConfig c;
    tapOfBlock.clear(); inDevOfBlock.clear(); outDevOfBlock.clear();
    auto indexOf = [] (std::vector<juce::String>& v, const juce::String& s) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int) i; v.push_back (s); return (int) v.size() - 1; };
    for (auto s : model.root)
    {
        if (! s.hasType (ids::session) || ! (bool) s[ids::on]) continue;
        for (auto b : s)
        {
            if (! b.hasType (ids::block)) continue;
            const auto t = typeFromId (b[ids::type]); const int id = b[ids::id];
            if (t == BlockType::App || t == BlockType::System)
            {
                TapSpec ts;
                ts.kind = t == BlockType::System ? (b[ids::exclude].toString().isEmpty() ? TapSpec::Kind::System : TapSpec::Kind::AllButProcesses) : TapSpec::Kind::Processes;
                if (t == BlockType::App) { ts.pids = pidList (b[ids::pids]); ts.bundleIds = strList (b[ids::bundles]); }
                else ts.bundleIds = strList (b[ids::exclude]);
                ts.muteSource = (bool) b[ids::mute]; ts.mono = (bool) b[ids::mono]; ts.name = b[ids::name].toString();
                if (t == BlockType::App && ts.pids.empty() && ts.bundleIds.empty()) continue;   // no app chosen yet
                tapOfBlock[id] = (int) c.taps.size(); c.taps.push_back (ts);
            }
            else if (t == BlockType::Input && b[ids::device].toString().isNotEmpty()) inDevOfBlock[id] = indexOf (c.inputDevices, b[ids::device].toString());
            else if (t == BlockType::Output) { const auto u = resolveOutput (b); if (u.isNotEmpty()) outDevOfBlock[id] = indexOf (c.outputDevices, u); }
        }
    }
    const auto st = model.settings();
    c.clockDevice = st[ids::device].toString();
    c.sampleRate = (double) st.getProperty ("rate", 0.0);
    c.bufferSize = (int) st.getProperty ("buffer", 512);
    return c;
}

void Engine::rebuild()
{
    dirty = false;
    auto want = wantedConfig();
    const bool any = ! want.inputDevices.empty() || ! want.outputDevices.empty() || ! want.taps.empty();
    if (offline) { compile(); return; }
    if (! any) { if (coreAudio.running()) coreAudio.stop(); layout = {}; running_ = {}; compile(); return; }
    if (! coreAudio.running() || configKey (want) != configKey (running_))
    {
        juce::String err;
        running_ = want;
        if (! coreAudio.start (want, this, err)) { error = err; running_ = {}; layout = {}; compile(); return; }
        error.clear();
        return;   // prepare() compiles
    }
    compile();
}

void Engine::runOffline (const IOLayout& l) { offline = true; wantedConfig(); prepare (l); }

void Engine::prepare (const IOLayout& l)
{
    {
        const juce::SpinLock::ScopedLockType lock (planLock);
        plan.reset();
    }
    layout = l;
    // a new rate or new devices: everything starts fresh (plug-ins keep their state through the model)
    for (auto& [id, rt] : rts) if (rt->plugin) if (auto b = [&] { for (auto s : model.root) { auto x = Model::blockById (s, id); if (x.isValid()) return x; } return juce::ValueTree(); }(); b.isValid())
    {
        juce::MemoryBlock mb; rt->plugin->getStateInformation (mb); b.setProperty (ids::state, mb.toBase64Encoding(), nullptr);
    }
    for (auto& [id, rt] : rts) { rt->writer.store (nullptr); rt->writerOwned.reset(); }
    rts.clear();
    compile();
}
void Engine::stopped() {}

std::shared_ptr<BlockRt> Engine::makeRuntime (const juce::ValueTree& b, int sessionId)
{
    auto rt = std::make_shared<BlockRt>();
    rt->id = b[ids::id]; rt->sessionId = sessionId; rt->type = typeFromId (b[ids::type]); rt->name = b[ids::name].toString();
    return rt;
}

void Engine::prepareRuntime (BlockRt& rt, const juce::ValueTree& b)
{
    const double sr = layout.sampleRate > 0 ? layout.sampleRate : 48000.0;
    const int C = std::max (1, rt.channels);
    rt.buf.setSize (C, kMaxBlock); rt.buf.clear();
    rt.scratch.setSize (std::max (C, std::max (rt.inChannels, 2)), kMaxBlock); rt.scratch.clear();
    rt.filters.prepare (C, sr);
    rt.limiter.prepare (sr, C); rt.leveler.prepare (sr, C); rt.gate.prepare (sr); rt.delay.prepare (sr, C); rt.tone.prepare (sr);
    rt.loudness.prepare (sr, C);
    if (rt.type == BlockType::File)
    {
        const juce::File f (b[ids::file].toString());
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) })
        {
            const auto len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 20));
            rt.file.setSize ((int) r->numChannels, len); r->read (&rt.file, 0, len, 0, true, true);
            rt.fileRatio = r->sampleRate / sr; rt.filePos = 0;
            rt.status.clear();
        }
        else { rt.file.setSize (0, 0); rt.status = f.existsAsFile() ? "can't read " + f.getFileName() : "choose a file"; }
    }
    refreshParams (rt, b);
}

void Engine::refreshParams (BlockRt& rt, const juce::ValueTree& b)
{
    const double sr = layout.sampleRate > 0 ? layout.sampleRate : 48000.0;
    rt.bypass = (bool) b[ids::bypass];
    rt.name = b[ids::name].toString();
    switch (rt.type)
    {
        case BlockType::Volume: rt.gainDb = (float) b[ids::db]; rt.balance = (float) b[ids::balance]; rt.mute = (bool) b[ids::mute]; rt.invert = (bool) b[ids::invert]; break;
        case BlockType::Output: rt.gainDb = (float) b.getProperty (ids::level, 0.0f); rt.mute = (bool) b[ids::mute]; break;
        case BlockType::Tone: rt.shape = (int) b[ids::shape]; rt.a = (float) b[ids::freq]; rt.gainDb = (float) b[ids::level]; break;
        case BlockType::File: rt.gainDb = (float) b[ids::level]; rt.fileLoop = (bool) b[ids::loop]; break;
        case BlockType::Eq:
            rt.filters.set (dsp::graphicFilters (Model::graphicBands (b), graphicFreqs()));
            rt.gainDb = (float) b[ids::db]; break;
        case BlockType::Peq:
        {
            std::vector<std::array<double, 4>> v;
            for (auto& p : Model::peqBands (b)) if (p.on) v.push_back ({ (double) p.shape, (double) p.freq, (double) p.db, (double) p.q });
            rt.filters.set (v); rt.gainDb = (float) b[ids::db]; break;
        }
        case BlockType::Delay: rt.delaySamples = (int) std::lround ((double) (float) b[ids::ms] * 0.001 * sr); break;
        case BlockType::Leveler: rt.a = (float) b[ids::target]; rt.b = (float) b[ids::db]; break;
        case BlockType::Gate: rt.a = (float) b[ids::threshold]; rt.b = (float) b[ids::release]; break;
        case BlockType::Limiter: rt.a = (float) b[ids::ceiling]; rt.limiter.setRelease ((float) b[ids::release], sr); break;
        case BlockType::Recorder: rt.a = (float) b[ids::silenceDb]; rt.mute = (bool) b[ids::pauseSilence]; break;
        case BlockType::Matrix:
        {
            const int outs = std::max (1, rt.channels), ins = std::max (1, rt.inChannels);
            std::vector<float> m ((size_t) (outs * ins), 0.0f);
            auto cells = Model::matrixCells (b);
            if (cells.empty()) for (int o = 0; o < outs; ++o) m[(size_t) (o * ins + (o % ins))] = 1.0f;   // nothing set: straight through (wrapping)
            for (auto& c : cells) if (c.in >= 0 && c.in < ins && c.out >= 0 && c.out < outs) m[(size_t) (c.out * ins + c.in)] = c.gain;
            const juce::SpinLock::ScopedLockType l (rt.matrixLock); rt.matrixPending = m; rt.matrixIns = ins; rt.matrixDirty = true;
            break;
        }
        default: break;
    }
}

void Engine::compile()
{
    auto next = std::make_shared<Plan>();
    std::map<int, std::shared_ptr<BlockRt>> keep;
    for (auto s : model.root)
    {
        if (! s.hasType (ids::session) || ! (bool) s[ids::on]) continue;
        const int sid = s[ids::id];
        // the blocks in signal order (Kahn)
        std::map<int, juce::ValueTree> blocks; std::map<int, std::vector<std::pair<int, float>>> into; std::map<int, int> indeg;
        for (auto b : s) if (b.hasType (ids::block)) { blocks[(int) b[ids::id]] = b; indeg[(int) b[ids::id]] = 0; }
        for (auto w : s) if (w.hasType (ids::wire)) { const int f = w[ids::from], t = w[ids::to]; if (blocks.count (f) && blocks.count (t) && f != t) { into[t].push_back ({ f, (float) w.getProperty (ids::gain, 1.0f) }); ++indeg[t]; } }
        std::vector<int> order, ready; for (auto& [id, d] : indeg) if (d == 0) ready.push_back (id);
        while (! ready.empty())
        {
            const int id = ready.front(); ready.erase (ready.begin()); order.push_back (id);
            for (auto w : s) if (w.hasType (ids::wire) && (int) w[ids::from] == id && blocks.count ((int) w[ids::to])) if (--indeg[(int) w[ids::to]] == 0) ready.push_back ((int) w[ids::to]);
        }
        std::map<int, BlockRt*> made;
        for (int id : order)
        {
            const auto& b = blocks[id]; const auto t = typeFromId (b[ids::type]);
            // what it receives, and so what it puts out
            int inCh = 0; for (auto& [f, g] : into[id]) if (made.count (f)) inCh = std::max (inCh, made[f]->channels);
            Step st;
            int ch = std::max (1, inCh == 0 ? 2 : inCh);
            if (t == BlockType::App) ch = (bool) b[ids::mono] ? 1 : 2;
            else if (t == BlockType::System) ch = 2;
            else if (t == BlockType::Tone) ch = juce::jlimit (1, 64, (int) b[ids::channels]);
            else if (t == BlockType::Input) ch = juce::jlimit (1, 256, (int) b[ids::count]);
            else if (t == BlockType::Matrix) ch = juce::jlimit (1, 256, (int) b[ids::outs]);
            else if (t == BlockType::File) { const juce::File f (b[ids::file].toString()); if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) }) ch = (int) r->numChannels; }
            // reuse the running block if nothing about its shape changed
            std::shared_ptr<BlockRt> rt;
            auto it = rts.find (id);
            if (it != rts.end() && it->second->type == t && it->second->channels == ch && it->second->inChannels == std::max (1, inCh) && ! (t == BlockType::File && it->second->status.isNotEmpty() && juce::File (b[ids::file].toString()).existsAsFile()))
                rt = it->second;
            else
            {
                rt = makeRuntime (b, sid); rt->channels = ch; rt->inChannels = std::max (1, inCh);
                if (t == BlockType::Plugin && it != rts.end() && it->second->plugin)
                {
                    // the instance moves over (the old plan may be running it: take its lock)
                    const juce::SpinLock::ScopedLockType l (it->second->pluginLock);
                    rt->plugin = std::move (it->second->plugin);
                }
                prepareRuntime (*rt, b);
            }
            if (t == BlockType::Plugin)
            {
                const double sr = layout.sampleRate > 0 ? layout.sampleRate : 48000.0;
                if (! rt->plugin && plugins && b[ids::plugin].toString().isNotEmpty())
                {
                    juce::String err;
                    rt->plugin = plugins->create (b[ids::plugin].toString(), sr, kMaxBlock, err);
                    if (rt->plugin && b[ids::state].toString().isNotEmpty()) { juce::MemoryBlock mb; mb.fromBase64Encoding (b[ids::state].toString()); rt->plugin->setStateInformation (mb.getData(), (int) mb.getSize()); }
                    rt->status = rt->plugin ? juce::String() : (err.isNotEmpty() ? err : juce::String ("can't load the plug-in"));
                }
                else if (! plugins || b[ids::plugin].toString().isEmpty()) rt->status = "choose a plug-in";
                if (rt->plugin)
                {
                    // ask for as many channels in and out as it receives; a plug-in that can't do that keeps its own
                    auto& p = *rt->plugin;
                    p.releaseResources();
                    juce::AudioProcessor::BusesLayout want = p.getBusesLayout();
                    if (want.inputBuses.size() > 0) want.inputBuses.getReference (0) = juce::AudioChannelSet::canonicalChannelSet (rt->inChannels);
                    if (want.outputBuses.size() > 0) want.outputBuses.getReference (0) = juce::AudioChannelSet::canonicalChannelSet (rt->inChannels);
                    if (p.checkBusesLayoutSupported (want)) p.setBusesLayout (want);
                    p.setRateAndBufferSizeDetails (sr, kMaxBlock);
                    p.prepareToPlay (sr, kMaxBlock);
                    const int outs = std::max (1, p.getTotalNumOutputChannels());
                    if (outs != rt->channels)
                    {
                        // the block's shape follows the plug-in's (a new runtime: the buffers are the io thread's)
                        auto nrt = makeRuntime (b, sid); nrt->channels = outs; nrt->inChannels = rt->inChannels;
                        { const juce::SpinLock::ScopedLockType l (rt->pluginLock); nrt->plugin = std::move (rt->plugin); }
                        prepareRuntime (*nrt, b); rt = nrt;
                    }
                    rt->scratch.setSize (std::max (rt->inChannels, std::max (rt->plugin->getTotalNumInputChannels(), rt->plugin->getTotalNumOutputChannels())), kMaxBlock);
                    rt->status.clear();
                }
            }
            rt->sessionId = sid;
            refreshParams (*rt, b);
            st.rt = rt;
            for (auto& [f, g] : into[id]) if (made.count (f)) st.ins.push_back ({ made[f], g });
            // where it reads / writes in the callback
            rt->active = true;
            if (t == BlockType::App || t == BlockType::System)
            {
                if (! tapOfBlock.count (id)) { rt->active = false; rt->status = t == BlockType::App ? "choose an app" : "no tap"; }
                else
                {
                    rt->active = false;
                    for (int k = 0; k < (int) layout.inputs.size(); ++k) if (layout.inputs[(size_t) k].kind == IOLayout::Part::Kind::Tap && layout.inputs[(size_t) k].index == tapOfBlock[id])
                    { st.inPart = k; st.firstIn = layout.inputs[(size_t) k].firstChannel; st.countIn = layout.inputs[(size_t) k].numChannels; rt->active = true; }
                    rt->status = rt->active ? juce::String() : (error.isNotEmpty() ? error : juce::String ("not capturing"));
                }
            }
            else if (t == BlockType::Input)
            {
                rt->active = false;
                if (inDevOfBlock.count (id))
                    for (int k = 0; k < (int) layout.inputs.size(); ++k) if (layout.inputs[(size_t) k].kind == IOLayout::Part::Kind::InputDevice && layout.inputs[(size_t) k].uid == b[ids::device].toString())
                    {
                        const auto& p = layout.inputs[(size_t) k]; const int first = juce::jlimit (0, std::max (0, p.numChannels - 1), (int) b[ids::first]);
                        st.inPart = k; st.firstIn = p.firstChannel + first; st.countIn = std::min (ch, p.numChannels - first); rt->active = st.countIn > 0;
                    }
                rt->status = rt->active ? juce::String() : (b[ids::device].toString().isEmpty() ? juce::String ("choose a device") : juce::String ("the device is not there"));
            }
            else if (t == BlockType::Output)
            {
                rt->active = false;
                if (outDevOfBlock.count (id))
                    for (int k = 0; k < (int) layout.outputs.size(); ++k) if (layout.outputs[(size_t) k].uid == resolveOutput (b))   // (by uid: the device's place in the list may differ)
                    {
                        const auto& p = layout.outputs[(size_t) k]; const int first = juce::jlimit (0, std::max (0, p.numChannels - 1), (int) b[ids::first]);
                        st.outPart = k; st.firstOut = p.firstChannel + first; st.countOut = ch == 1 ? p.numChannels - first : std::min (ch, p.numChannels - first); rt->active = st.countOut > 0;   // (mono: to every channel of the device)
                    }
                rt->status = rt->active ? juce::String() : juce::String ("the device is not there");
            }
            made[id] = rt.get();
            keep[id] = rt;
            next->steps.push_back (std::move (st));
        }
    }
    {
        const juce::SpinLock::ScopedLockType l (planLock);
        std::swap (plan, next);
    }
    // blocks that are gone: their recorders close, their plug-ins' states go back into the model
    for (auto& [id, rt] : rts)
        if (! keep.count (id) || keep[id] != rt)
        {
            if (! keep.count (id)) { rt->writer.store (nullptr); rt->writerOwned.reset(); }
            else if (rt->writerOwned) { keep[id]->writerOwned = std::move (rt->writerOwned); keep[id]->writer.store (keep[id]->writerOwned.get()); keep[id]->currentFile = rt->currentFile; keep[id]->fileIndex = rt->fileIndex; keep[id]->fileStarted = rt->fileStarted; }
        }
    next.reset();   // the old plan (its last user, the io thread, has let go: the swap took its lock)
    rts = std::move (keep);
    if (onRebuilt) onRebuilt();
}

void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType l (planLock);
    if (! l.isLocked() || ! plan) return;
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (kMaxBlock, numSamples - done);
        // pointers into this chunk
        std::array<const float*, 512> ip {}; std::array<float*, 512> op {};
        for (int c = 0; c < std::min (numIn, 512); ++c) ip[(size_t) c] = in[c] ? in[c] + done : nullptr;
        for (int c = 0; c < std::min (numOut, 512); ++c) op[(size_t) c] = out[c] ? out[c] + done : nullptr;
        for (auto& st : plan->steps) runBlock (st, ip.data(), std::min (numIn, 512), op.data(), std::min (numOut, 512), n);
        done += n;
    }
}

void Engine::runBlock (Step& st, const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    auto& rt = *st.rt; auto& buf = rt.buf;
    const int C = buf.getNumChannels();
    const auto t = rt.type;
    if (! rt.active.load (std::memory_order_relaxed)) { for (int c = 0; c < C; ++c) juce::FloatVectorOperations::clear (buf.getWritePointer (c), n); rt.levels.write (buf, n); return; }
    // ---- what comes in ----
    if (t == BlockType::App || t == BlockType::System || t == BlockType::Input)
    {
        for (int c = 0; c < C; ++c)
        {
            const int k = st.firstIn + c;
            if (c < st.countIn && k < numIn && in[k]) juce::FloatVectorOperations::copy (buf.getWritePointer (c), in[k], n);
            else juce::FloatVectorOperations::clear (buf.getWritePointer (c), n);
        }
    }
    else if (t == BlockType::Tone) rt.tone.process (buf, n, rt.shape.load(), rt.a.load(), rt.gainDb.load());
    else if (t == BlockType::File)
    {
        const int fc = rt.file.getNumChannels(), fl = rt.file.getNumSamples();
        const float g = dsp::dbToGain (rt.gainDb.load());
        for (int c = 0; c < C; ++c) juce::FloatVectorOperations::clear (buf.getWritePointer (c), n);
        if (fc > 0 && fl > 1)
            for (int i = 0; i < n; ++i)
            {
                if (rt.filePos >= fl - 1) { if (rt.fileLoop.load()) rt.filePos = 0; else break; }
                const int k = (int) rt.filePos; const float fr = (float) (rt.filePos - k);
                for (int c = 0; c < C && c < fc; ++c) { const float* x = rt.file.getReadPointer (c); buf.setSample (c, i, g * (x[k] + fr * (x[k + 1] - x[k]))); }
                rt.filePos += rt.fileRatio;
            }
    }
    else
    {
        for (int c = 0; c < C; ++c) juce::FloatVectorOperations::clear (buf.getWritePointer (c), n);
        for (auto& [src, g] : st.ins)
        {
            const int sc = src->buf.getNumChannels();
            if (sc == 1) { for (int c = 0; c < C; ++c) juce::FloatVectorOperations::addWithMultiply (buf.getWritePointer (c), src->buf.getReadPointer (0), g, n); }   // mono: to every channel
            else for (int c = 0; c < std::min (C, sc); ++c) juce::FloatVectorOperations::addWithMultiply (buf.getWritePointer (c), src->buf.getReadPointer (c), g, n);
        }
    }
    // ---- what it does ----
    if (! rt.bypass.load (std::memory_order_relaxed))
        switch (t)
        {
            case BlockType::Volume:
            {
                const float g = rt.mute.load() ? 0.0f : dsp::dbToGain (rt.gainDb.load()) * (rt.invert.load() ? -1.0f : 1.0f);
                const float g0 = rt.gain.cur; rt.gain.cur = g;
                const float bal = rt.balance.load();
                for (int c = 0; c < C; ++c) { const float bg = dsp::balanceGain (c, C, bal); dsp::rampGain (buf.getWritePointer (c), n, g0 * bg, g * bg); }
                break;
            }
            case BlockType::Eq: case BlockType::Peq:
            {
                rt.filters.process (buf, n);
                const float g = dsp::dbToGain (rt.gainDb.load());
                if (g != 1.0f) buf.applyGain (0, n, g);
                break;
            }
            case BlockType::Plugin:
            {
                const juce::SpinLock::ScopedTryLockType pl (rt.pluginLock);
                if (pl.isLocked() && rt.plugin)
                {
                    auto& p = *rt.plugin;
                    const int pc = std::min (rt.scratch.getNumChannels(), std::max (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
                    for (int c = 0; c < pc; ++c) { if (c < C) juce::FloatVectorOperations::copy (rt.scratch.getWritePointer (c), buf.getReadPointer (c), n); else juce::FloatVectorOperations::clear (rt.scratch.getWritePointer (c), n); }
                    juce::AudioBuffer<float> view (rt.scratch.getArrayOfWritePointers(), pc, n);
                    rt.midi.clear();
                    p.processBlock (view, rt.midi);
                    for (int c = 0; c < C; ++c) { if (c < pc) juce::FloatVectorOperations::copy (buf.getWritePointer (c), rt.scratch.getReadPointer (c), n); }
                }
                break;
            }
            case BlockType::Matrix:
            {
                if (rt.matrixDirty.load()) { const juce::SpinLock::ScopedTryLockType ml (rt.matrixLock); if (ml.isLocked()) { std::swap (rt.matrix, rt.matrixPending); rt.matrixDirty = false; } }
                const int ins = rt.matrixIns;
                // the inputs (summed already into buf's first channels: copy them aside first)
                const int ic = std::min ({ ins, rt.inChannels, rt.scratch.getNumChannels() });
                for (int c = 0; c < ic; ++c) juce::FloatVectorOperations::copy (rt.scratch.getWritePointer (c), c < C ? buf.getReadPointer (c) : buf.getReadPointer (0), n);
                // (buf holds the sum of the inputs only up to its own channel count: inputs beyond it come straight from the sources)
                if (rt.inChannels > C)
                    for (int c = C; c < ic; ++c)
                    {
                        juce::FloatVectorOperations::clear (rt.scratch.getWritePointer (c), n);
                        for (auto& [src, g] : st.ins) if (c < src->buf.getNumChannels()) juce::FloatVectorOperations::addWithMultiply (rt.scratch.getWritePointer (c), src->buf.getReadPointer (c), g, n);
                    }
                for (int o = 0; o < C; ++o)
                {
                    float* y = buf.getWritePointer (o); juce::FloatVectorOperations::clear (y, n);
                    if ((int) rt.matrix.size() >= (o + 1) * ins)
                        for (int i = 0; i < ic; ++i) { const float g = rt.matrix[(size_t) (o * ins + i)]; if (g != 0.0f) juce::FloatVectorOperations::addWithMultiply (y, rt.scratch.getReadPointer (i), g, n); }
                }
                break;
            }
            case BlockType::Delay: rt.delay.process (buf, n, rt.delaySamples.load()); break;
            case BlockType::Leveler: rt.leveler.process (buf, n, rt.a.load(), rt.b.load()); break;
            case BlockType::Gate: rt.gate.process (buf, n, rt.a.load(), rt.b.load()); break;
            case BlockType::Limiter: rt.limiter.process (buf, n, dsp::dbToGain (rt.a.load())); break;
            case BlockType::Output:
            {
                const float g = rt.mute.load() ? 0.0f : dsp::dbToGain (rt.gainDb.load());
                for (int c = 0; c < st.countOut && (c < C || C == 1); ++c)
                {
                    const int k = st.firstOut + c;
                    if (k < numOut && out[k]) juce::FloatVectorOperations::addWithMultiply (out[k], buf.getReadPointer (C == 1 ? 0 : c), g, n);
                }
                break;
            }
            case BlockType::Recorder:
            {
                if (auto* w = rt.writer.load (std::memory_order_acquire))
                {
                    bool quiet = false;
                    if (rt.mute.load())   // pause on silence
                    { const float thr = dsp::dbToGain (rt.a.load()); quiet = true; for (int c = 0; c < C && quiet; ++c) if (buf.getMagnitude (c, 0, n) > thr) quiet = false; }
                    rt.silent = quiet;
                    if (! quiet && w->write (buf.getArrayOfReadPointers(), n)) { rt.recorded += n; rt.recordedTotal += n; }
                }
                break;
            }
            default: break;
        }
    // ---- meters ----
    rt.levels.write (buf, n);
    if (t == BlockType::Meter || rt.analysed.load (std::memory_order_relaxed)) { rt.scope.push (buf, n); rt.loudness.process (buf, n); }
}

// ---- the recorder ----------------------------------------------------------------------------------------------
juce::File Engine::recordingFolder (const juce::ValueTree& b)
{
    juce::File f (b[ids::folder].toString());
    if (! juce::File::isAbsolutePath (b[ids::folder].toString())) f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("umleitung");
    return f;
}
juce::String Engine::fileName (const juce::ValueTree& b, const juce::String& session, int index)
{
    const auto now = juce::Time::getCurrentTime();
    auto s = b[ids::pattern].toString(); if (s.trim().isEmpty()) s = "{session} {date} {time}";
    s = s.replace ("{session}", session).replace ("{block}", b[ids::name].toString()).replace ("{date}", now.formatted ("%Y-%m-%d")).replace ("{time}", now.formatted ("%H.%M.%S"))
         .replace ("{n}", juce::String (index)).replace ("{year}", now.formatted ("%Y")).replace ("{month}", now.formatted ("%m")).replace ("{day}", now.formatted ("%d"));
    return juce::File::createLegalFileName (s.trim());
}
void Engine::startRecording (int id)
{
    for (auto s : model.root) if (auto b = Model::blockById (s, id); b.isValid()) { b.setProperty (ids::armed, true, nullptr); if (! (bool) s[ids::on]) s.setProperty (ids::on, true, nullptr); }
}
void Engine::stopRecording (int id) { for (auto s : model.root) if (auto b = Model::blockById (s, id); b.isValid()) b.setProperty (ids::armed, false, nullptr); }
bool Engine::recording (int id) { auto* rt = runtime (id); return rt && rt->writer.load() != nullptr; }

void Engine::recorderTick (BlockRt& rt, const juce::ValueTree& b)
{
    const auto s = b.getParent();
    const bool want = (bool) s[ids::on] && (bool) b[ids::armed] && rt.active.load() && (running() || offline);
    const double sr = layout.sampleRate > 0 ? layout.sampleRate : 48000.0;
    const double secs = (double) rt.recorded.load() / sr;
    const int splitMin = b[ids::splitMin], maxMin = b[ids::maxMin];
    bool needNew = want && rt.writer.load() == nullptr;
    if (want && rt.writer.load() && splitMin > 0 && secs >= splitMin * 60.0) needNew = true;
    if (want && maxMin > 0 && (double) rt.recordedTotal.load() / sr >= maxMin * 60.0) { const_cast<juce::ValueTree&> (b).setProperty (ids::armed, false, nullptr); return; }
    if (! want && rt.writer.load())
    {
        { const juce::SpinLock::ScopedLockType l (planLock); rt.writer.store (nullptr); }
        rt.writerOwned.reset(); rt.recorded = 0; rt.recordedTotal = 0;
        return;
    }
    if (! needNew) return;
    // a new file (the first, or the next part)
    const auto folder = recordingFolder (b); folder.createDirectory();
    const auto fmt = b[ids::format].toString();
    std::unique_ptr<juce::AudioFormat> af;
    if (fmt == "aiff") af = std::make_unique<juce::AiffAudioFormat>(); else if (fmt == "flac") af = std::make_unique<juce::FlacAudioFormat>(); else af = std::make_unique<juce::WavAudioFormat>();
    int bits = juce::jlimit (16, 32, (int) b[ids::bits]); if (fmt == "flac") bits = std::min (bits, 24);
    const int index = rt.writer.load() ? rt.fileIndex + 1 : 1;
    auto file = folder.getChildFile (fileName (b, s[ids::name].toString(), index) + (splitMin > 0 ? " " + juce::String (index).paddedLeft ('0', 2) : juce::String()) + af->getFileExtensions()[0]).getNonexistentSibling();
    auto os = file.createOutputStream();
    if (! os) { rt.status = "can't write in " + folder.getFullPathName(); return; }
    std::unique_ptr<juce::OutputStream> stream (os.release());
    const auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (rt.channels).withBitsPerSample (bits)
                          .withSampleFormat (bits == 32 && fmt != "aiff" ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = af->createWriterFor (stream, opts);
    if (! writer) { rt.status = "can't write " + fmt + " with " + juce::String (rt.channels) + " channels"; file.deleteFile(); return; }
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 1 << 18);
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    { const juce::SpinLock::ScopedLockType l (planLock); rt.writer.store (tw.get()); }
    old = std::move (rt.writerOwned); rt.writerOwned = std::move (tw);
    old.reset();   // the finished part is flushed and closed
    rt.currentFile = file; rt.fileIndex = index; rt.fileStarted = juce::Time::getCurrentTime(); rt.recorded = 0; rt.status.clear();
}

void Engine::timerCallback()
{
    // sessions on a timer
    const auto now = juce::Time::getCurrentTime();
    for (auto s : model.root)
    {
        if (! s.hasType (ids::session)) continue;
        const auto at = s[ids::startAt].toString();
        if (at.isNotEmpty() && ! (bool) s[ids::on] && now.formatted ("%H:%M") == at && s.getProperty ("lastStart").toString() != now.formatted ("%Y-%m-%d %H:%M"))
        { s.setProperty ("lastStart", now.formatted ("%Y-%m-%d %H:%M"), nullptr); s.setProperty ("onSince", (double) now.toMilliseconds(), nullptr); s.setProperty (ids::on, true, nullptr); }
        const double stopAfter = (double) s.getProperty (ids::stopAfter, 0.0);
        if (stopAfter > 0 && (bool) s[ids::on] && (double) now.toMilliseconds() - (double) s.getProperty ("onSince", (double) now.toMilliseconds()) >= stopAfter * 60000.0) s.setProperty (ids::on, false, nullptr);
    }
    if (dirty) rebuild();
    for (auto& [id, rt] : rts)
        if (rt->type == BlockType::Recorder)
            for (auto s : model.root) if (auto b = Model::blockById (s, id); b.isValid()) recorderTick (*rt, b);
}

void Engine::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    if (t.hasType (ids::session) && p == ids::on)
    {
        if ((bool) t[ids::on]) t.setProperty ("onSince", (double) juce::Time::getCurrentTime().toMilliseconds(), nullptr);
        dirty = true; return;
    }
    if (t.hasType (ids::block))
    {
        // the shape of the graph or what it needs from the mac: rebuild; anything else: the running block takes it now
        static const juce::Identifier structural[] = { ids::type, ids::device, ids::pids, ids::bundles, ids::exclude, ids::mute, ids::mono, ids::first, ids::count, ids::outs, ids::channels, ids::file, ids::plugin };
        const auto ty = typeFromId (t[ids::type]);
        const bool sourceMute = p == ids::mute && (ty == BlockType::App || ty == BlockType::System);
        bool structuralChange = sourceMute;
        for (auto& s : structural) if (p == s && p != ids::mute) structuralChange = true;
        if (structuralChange) { dirty = true; return; }
        if (auto* rt = runtime ((int) t[ids::id])) refreshParams (*rt, t);
        if (p == ids::armed) timerCallback();
        return;
    }
    if (t.hasType (ids::wire) || t.hasType (ids::settings)) dirty = true;
}

juce::ValueTree Engine::systemEqSession()
{
    for (auto s : model.root) if (s.hasType (ids::session) && s[ids::kind].toString() == "system eq") return s;
    auto s = model.addSession ("system eq");
    s.setProperty (ids::kind, "system eq", nullptr);
    auto src = model.addBlock (s, BlockType::System, 40, 60); src.setProperty (ids::mute, true, nullptr); src.setProperty (ids::name, "the mac's sound", nullptr);
    auto vol = model.addBlock (s, BlockType::Volume, 240, 60);
    auto eq = model.addBlock (s, BlockType::Eq, 440, 60);
    auto peq = model.addBlock (s, BlockType::Peq, 640, 60); peq.setProperty (ids::bands, "", nullptr); peq.setProperty (ids::bypass, true, nullptr);
    auto lim = model.addBlock (s, BlockType::Limiter, 840, 60); lim.setProperty (ids::ceiling, -0.3f, nullptr);
    auto out = model.addBlock (s, BlockType::Output, 1040, 60); out.setProperty (ids::name, "the mac's output", nullptr);
    int prev = src[ids::id]; for (auto b : { vol, eq, peq, lim, out }) { model.connect (s, prev, b[ids::id]); prev = b[ids::id]; }
    return s;
}
} // namespace um

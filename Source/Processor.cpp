#include "Processor.h"
#include "Editor.h"

namespace fal
{
juce::AudioProcessor::BusesProperties Processor::busesFor (Pid p)
{
    const auto& i = info (p);
    const auto st = juce::AudioChannelSet::stereo();
    BusesProperties b;
    if (i.kind == Kind::Effect) return b.withInput ("in", st, true).withOutput ("out", st, true);
    b = b.withOutput ("main", st, true);
    if (p == Pid::kt1)
        for (int v = 0; v < kKitVoices; ++v) b = b.withOutput (kitVoice (v).id, st, false);
    return b;
}

// pattern text helpers ----------------------------------------------------------------------------
static void stepFromChar (Step& s, juce::juce_wchar c)
{
    s = Step();
    switch (c)
    {
        case 'x': s.on = true; s.vel = 0.8f; break;
        case 'X': s.on = true; s.vel = 1.0f; break;
        case 'o': s.on = true; s.vel = 0.5f; break;
        case 'r': s.on = true; s.vel = 0.8f; s.ratchet = 2; break;
        case '?': s.on = true; s.vel = 0.8f; s.prob = 0.5f; break;
        default: break;
    }
}
static void parseKit (const juce::String& text, Pattern<8>& pat)
{
    pat = Pattern<8>();
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        const auto id = line.upToFirstOccurrenceOf (" ", false, false);
        const auto steps = line.fromFirstOccurrenceOf (" ", false, false).trim();
        for (int v = 0; v < kKitVoices; ++v)
            if (id == kitVoice (v).id)
                for (int s = 0; s < kSteps && s < steps.length(); ++s)
                    stepFromChar (pat.t[(size_t) v][(size_t) s], steps[s]);
    }
    // a little built-in looseness: offbeat hats and percussion sit late, rims early
    for (int s = 0; s < kSteps; ++s)
    {
        if (s & 1) { pat.t[3][(size_t) s].micro = 0.25f; pat.t[6][(size_t) s].micro = 0.35f; }
        pat.t[2][(size_t) s].micro = (s % 4 == 3) ? -0.3f : pat.t[2][(size_t) s].micro;
    }
}
static void parseLoop (const juce::String& text, Pattern<1>& pat)
{
    pat = Pattern<1>();
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (! line.startsWith ("sl ")) continue;
        const auto steps = line.substring (3).trim();
        for (int s = 0; s < kSteps && s < steps.length(); ++s)
        {
            const auto c = steps[s];
            auto& st = pat.t[0][(size_t) s];
            st = Step();
            const int v = juce::CharacterFunctions::getHexDigitValue (c);
            if (v >= 0) { st.on = true; st.slice = v; st.vel = (s % 4 == 0) ? 1.0f : 0.8f; }
        }
    }
}

// -------------------------------------------------------------------------------------------------
Processor::Processor (Pid product)
    : AudioProcessor (busesFor (product)), pid (product), pinfo (info (product)),
      apvts (*this, nullptr, "faltung", makeLayout (product))
{
    rng.seed ((uint32_t) juce::Time::getMillisecondCounter() | 1u);
    switch (pid)
    {
        case Pid::dl4: dubFx = std::make_unique<DubDelay>(); break;
        case Pid::rv5: roomFx = std::make_unique<Room>(); break;
        case Pid::fl7: filterFx = std::make_unique<Isolator>(); break;
        case Pid::gl8: glueFx = std::make_unique<Glue>(); break;
        case Pid::mc3: microFx = std::make_unique<Micro>(); break;
        case Pid::sw6: swingFx = std::make_unique<Swing>(); break;
        case Pid::wb1: bassFx = std::make_unique<BassSynth>(); break;
        case Pid::ms0: masterFx = std::make_unique<MasterChain>(); designer = std::make_unique<Designer> (*this); designer->startThread(); break;
        default: break;
    }
    if (pid == Pid::kt1)
    {
        const auto& pr = presets (pid);
        for (int i = 0; i < 3 && i < (int) pr.size(); ++i) parseKit (pr[(size_t) i].pattern, kitBank.p[(size_t) i]);
    }
    if (pid == Pid::kt2)
    {
        const auto& pr = presets (pid);
        for (int i = 0; i < 4 && i < (int) pr.size(); ++i) parseLoop (pr[(size_t) i].pattern, loopBank.p[(size_t) i]);
        auto s = renderDefaultLoop();
        sample = s;
    }
}

Processor::~Processor() { if (designer) designer->stopThread (2000); }

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto st = juce::AudioChannelSet::stereo();
    if (l.getMainOutputChannelSet() != st) return false;
    if (pinfo.kind == Kind::Effect && l.getMainInputChannelSet() != st) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != st) return false;
    return true;
}

double Processor::getTailLengthSeconds() const
{
    switch (pid)
    {
        case Pid::dl4: return 8.0;
        case Pid::rv5: return 8.0;
        case Pid::ms0: return 2.0;
        case Pid::mc3: return 0.5;
        default: return pinfo.kind == Kind::Effect ? 0.0 : 2.0;
    }
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; blockSize = samplesPerBlock;
    uint32_t seed = 1234;
    for (auto& pair : kitVoices) for (auto& v : pair) v.prepare ((float) sr, seed += 7919);
    for (auto& v : modVoices) v.prepare ((float) sr, seed += 7919);
    kitSeq.prepare (sr); loopSeq.prepare (sr);
    for (auto& s : slicers) s.on = false;
    masterSm.prepare ((float) sr, 20.0f); masterSm.snap (1.0f);
    if (dubFx) dubFx->prepare ((float) sr);
    if (roomFx) roomFx->prepare ((float) sr);
    if (filterFx) filterFx->prepare ((float) sr);
    if (glueFx) glueFx->prepare ((float) sr);
    if (microFx) microFx->prepare ((float) sr);
    if (bassFx) bassFx->prepare ((float) sr);
    if (masterFx)
    {
        const juce::ScopedLock sl (designLock);
        masterFx->prepare (sr, samplesPerBlock);
        masterFx->design (readMaster(), true);
        masterReady = true;
        setLatencySamples (masterFx->latency());
    }
    if (swingFx) { swingFx->prepare ((float) sr); setLatencySamples (swingFx->latency()); }
}

Transport Processor::readTransport()
{
    Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tr.bpm = *b;
            tr.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { tr.ppq = *q; tr.hasPpq = true; }
        }
    return tr;
}

void Processor::uiTrigger (int index, float velocity)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) uiQueue[(size_t) s1] = { index, velocity };
    else if (n2 > 0) uiQueue[(size_t) s2] = { index, velocity };
    uiFifo.finishedWrite (n1 + n2);
}

void Processor::setParam (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

VoiceParams Processor::readVoice (const juce::String& pre, VType t) const
{
    VoiceParams vp;
    const auto& vi = vtypeInfo (t);
    vp.model = (int) param (pre + "model");
    for (int s = 0; s < kSlots; ++s) vp.s[(size_t) s] = param (pre + vi.slot[(size_t) s]);
    vp.level = param (pre + "level");
    vp.pan = param (pre + "pan");
    vp.vel = param (pre + "vel");
    return vp;
}

int Processor::currentPattern() const { return std::clamp ((int) param ("pattern"), 0, kPatterns - 1); }

void Processor::applyPatternText (const juce::String& text, int pi)
{
    const juce::SpinLock::ScopedLockType sl (bankLock);
    if (pid == Pid::kt1) parseKit (text, kitBank.p[(size_t) pi]);
    else parseLoop (text, loopBank.p[(size_t) pi]);
}

// -------------------------------------------------------------------------------------------------
void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const auto tr = readTransport();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());

    switch (pinfo.kind)
    {
        case Kind::Machine: if (pid == Pid::kt1) processKit (buffer, midi, tr); else processLoop (buffer, midi, tr); break;
        case Kind::Module:  processModule (buffer, midi); break;
        case Kind::Effect:  processEffect (buffer, tr); break;
        case Kind::Synth:   processSynth (buffer, midi, tr); break;
    }
    // safety: never let a non-finite sample reach the host
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < buffer.getNumSamples(); ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
    }
    outLevel.store (buffer.getMagnitude (0, buffer.getNumSamples()));
}

// ---- kt1 kit --------------------------------------------------------------------------------------
void Processor::triggerKit (int v, float vel)
{
    const auto& kv = kitVoice (v);
    const VoiceParams vp = readVoice (kitPrefix (v), kv.type);
    auto& pair = kitVoices[(size_t) v];
    const int cur = kitNext[(size_t) v], nxt = cur ^ 1;
    pair[(size_t) cur].release();
    pair[(size_t) nxt].trigger (kv.type, vp, vel, 0.0f);
    kitNext[(size_t) v] = nxt;
    if (v == 3) for (auto& o : kitVoices[4]) o.release();   // closed hat chokes the open hat
    hitCount[(size_t) v].fetch_add (1);
}

void Processor::processKit (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const Transport& tr)
{
    const int n = buffer.getNumSamples();
    struct Ev { int s; int v; float vel; };
    std::array<Ev, 512> evs; int ne = 0;
    auto push = [&] (int s, int v, float vel) { if (ne < (int) evs.size()) evs[(size_t) ne++] = { s, v, vel }; };

    int s1, n1, s2, n2;
    uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) push (0, uiQueue[(size_t) (s1 + i)].first, uiQueue[(size_t) (s1 + i)].second);
    for (int i = 0; i < n2; ++i) push (0, uiQueue[(size_t) (s2 + i)].first, uiQueue[(size_t) (s2 + i)].second);
    uiFifo.finishedRead (n1 + n2);

    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            for (int v = 0; v < kKitVoices; ++v)
                if (note == kitVoice (v).defaultNote) push (m.samplePosition, v, msg.getFloatVelocity());
        }
    }

    std::array<TrackCtl, 8> ctl;
    for (int v = 0; v < kKitVoices; ++v)
    {
        const auto pre = kitPrefix (v);
        ctl[(size_t) v] = { (int) std::round (param (pre + "len")), param (pre + "prob"), param (pre + "shift"), param (pre + "mute") > 0.5f };
    }
    Pattern<8> pat;
    { const juce::SpinLock::ScopedLockType sl (bankLock); pat = kitBank.p[(size_t) currentPattern()]; }
    const bool run = param ("run") > 0.5f;
    const double bpm = (tr.playing && tr.hasPpq) ? tr.bpm : (double) param ("bpm");
    kitSeq.run (pat, ctl, n, tr.playing, tr.hasPpq, tr.ppq, bpm, run, param ("swing"), param ("micro"), rng,
                [&] (const SeqEvent& e) { push (e.sample, e.track, e.vel); });
    playPpq.store (kitSeq.isRunning() ? (float) kitSeq.positionPpq() : -1.0f);
    std::sort (evs.begin(), evs.begin() + ne, [] (const Ev& a, const Ev& b) { return a.s < b.s; });

    // where does each voice go: its own output if that bus is on, otherwise the main pair
    std::array<float*, 8> outL {}, outR {};
    auto mainBus = getBusBuffer (buffer, false, 0);
    for (int v = 0; v < kKitVoices; ++v)
    {
        outL[(size_t) v] = mainBus.getWritePointer (0); outR[(size_t) v] = mainBus.getWritePointer (1);
        if (auto* bus = getBus (false, v + 1))
            if (bus->isEnabled())
            {
                auto b = getBusBuffer (buffer, false, v + 1);
                if (b.getNumChannels() >= 2) { outL[(size_t) v] = b.getWritePointer (0); outR[(size_t) v] = b.getWritePointer (1); }
            }
        const auto pre = kitPrefix (v);
        for (auto& vc : kitVoices[(size_t) v]) vc.setLive (param (pre + "level"), param (pre + "pan"));
    }
    auto renderTo = [&] (int a, int b)
    {
        if (b <= a) return;
        for (int v = 0; v < kKitVoices; ++v)
            for (auto& vc : kitVoices[(size_t) v]) vc.render (outL[(size_t) v] + a, outR[(size_t) v] + a, b - a);
    };
    int pos = 0;
    for (int i = 0; i < ne; ++i)
    {
        renderTo (pos, evs[(size_t) i].s); pos = std::max (pos, evs[(size_t) i].s);
        triggerKit (evs[(size_t) i].v, evs[(size_t) i].vel);
    }
    renderTo (pos, n);

    masterSm.target = dbToGain (param ("master"));
    auto* L = mainBus.getWritePointer (0); auto* R = mainBus.getWritePointer (1);
    for (int i = 0; i < n; ++i) { const float g = masterSm.tick(); L[i] *= g; R[i] *= g; }
    midi.clear();
}

// ---- modules --------------------------------------------------------------------------------------
void Processor::triggerModule (int note, float vel)
{
    const auto t = pinfo.vtype;
    VoiceParams vp = readVoice ("", t);
    const bool keytrack = param ("keytrack") > 0.5f;
    const bool choke = param ("choke") > 0.5f;
    const bool hat = t == VType::Hat;
    const bool open = hat && (note % 12 == 10);
    if (open) vp.s[1] = param ("open");
    if (choke) for (auto& v : modVoices) v.release();
    int idx = -1;
    for (int k = 0; k < (int) modVoices.size(); ++k)
    {
        const int i = (modNext + k) % (int) modVoices.size();
        if (! modVoices[(size_t) i].isActive()) { idx = i; break; }
    }
    if (idx < 0) { idx = modNext; }
    modNext = (idx + 1) % (int) modVoices.size();
    modVoices[(size_t) idx].trigger (t, vp, vel, keytrack ? (float) (note - 60) : 0.0f);
    hitCount[open ? 1 : 0].fetch_add (1);
}

void Processor::processModule (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    buffer.clear();
    auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
    for (auto& v : modVoices) v.setLive (param ("level"), param ("pan"));

    int s1, n1, s2, n2;
    uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1 + n2; ++i)
    {
        const auto& q = uiQueue[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
        triggerModule (q.first == 1 ? 70 : 60, q.second);
    }
    uiFifo.finishedRead (n1 + n2);

    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOn()) continue;
        const int at = std::clamp (m.samplePosition, 0, n);
        for (auto& v : modVoices) v.render (L + pos, R + pos, at - pos);
        pos = std::max (pos, at);
        triggerModule (msg.getNoteNumber(), msg.getFloatVelocity());
    }
    for (auto& v : modVoices) v.render (L + pos, R + pos, n - pos);
    midi.clear();
}

// ---- kt2 loop -------------------------------------------------------------------------------------
std::shared_ptr<const LoopSample> Processor::getSample() const
{
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    return sample;
}

void Processor::setSample (std::shared_ptr<const LoopSample> s)
{
    std::shared_ptr<const LoopSample> old;
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        old = sample; sample = std::move (s);
    }
    // `old` is released here, on the message thread
}

std::vector<std::pair<int, int>> Processor::currentSlices (const LoopSample& s) const
{
    static const int counts[4] = { 4, 8, 16, 32 };
    const int count = counts[std::clamp ((int) param ("slices"), 0, 3)];
    const int total = s.buf.getNumSamples();
    const int start = (int) (param ("start") * (float) total * 0.75f);
    std::vector<std::pair<int, int>> out;
    if (total < 64) return out;
    if ((int) param ("slicemode") == 1 && s.onsets.size() >= 2)
    {
        std::vector<int> on;
        for (int o : s.onsets) if (o >= start) on.push_back (o);
        for (size_t i = 0; i < on.size() && (int) out.size() < count; ++i)
            out.push_back ({ on[i], i + 1 < on.size() ? on[i + 1] : total });
        if (! out.empty()) return out;
    }
    const double len = (double) (total - start) / count;
    for (int k = 0; k < count; ++k)
        out.push_back ({ start + (int) (k * len), start + (int) ((k + 1) * len) });
    return out;
}

void Processor::triggerSlice (int slice, float vel, bool rev)
{
    const juce::SpinLock::ScopedTryLockType tl (sampleLock);
    if (! tl.isLocked() || ! sample) return;
    const auto sl = currentSlices (*sample);
    if (sl.empty()) return;
    const auto [a, b] = sl[(size_t) (((slice % (int) sl.size()) + (int) sl.size()) % (int) sl.size())];
    double rate = sample->rate / sr * semis (param ("pitch"));
    if (param ("fit") > 0.5f)
    {
        const double want = param ("loopbeats") * 60.0 / std::max (20.0, (double) lastBpm);
        const double have = sample->buf.getNumSamples() / sample->rate;
        if (want > 0.01) rate *= have / want;
    }
    rate = juce::jlimit (0.05, 8.0, rate);
    // monophonic slicing: the previous slice fades over the release time
    const float relMs = param ("release");
    for (auto& v : slicers) if (v.on && ! v.releasing) { v.releasing = true; v.relK = 1.0f / std::max (1.0f, relMs * 0.001f * (float) sr); }
    auto& v = slicers[(size_t) sliceNext]; sliceNext = (sliceNext + 1) % (int) slicers.size();
    v.on = true; v.releasing = false; v.age = 0; v.env = 0; v.rev = rev; v.vel = vel;
    v.start = a; v.end = b;
    v.rate = rev ? -rate : rate;
    v.pos = rev ? (double) b - 1.0 : (double) a;
    const float outLen = (float) ((b - a) / rate);
    const float relS = relMs * 0.001f * (float) sr;
    v.gateLen = (int) std::max (8.0f, std::min (outLen * param ("gate"), outLen - relS));
    v.relK = 1.0f / std::max (1.0f, std::min (relS, outLen * 0.5f));
    const float sp = param ("spread");
    panGains (rng.bi() * sp, v.gl, v.gr);
    const float fc = expMap (param ("tone"), 300.0f, 20000.0f);
    v.fl.set (fc, 0.707f, (float) sr); v.fr.set (fc, 0.707f, (float) sr); v.fl.reset(); v.fr.reset();
    hitCount[(size_t) (slice & 15)].fetch_add (1);
}

void Processor::renderSlices (float* L, float* R, int n)
{
    const juce::SpinLock::ScopedTryLockType tl (sampleLock);
    if (! tl.isLocked() || ! sample) return;
    const auto& s = *sample;
    const int total = s.buf.getNumSamples();
    const float* dl = s.buf.getReadPointer (0);
    const float* dr = s.buf.getReadPointer (s.buf.getNumChannels() > 1 ? 1 : 0);
    const float attS = std::max (1.0f, param ("attack") * 0.001f * (float) sr);
    const float vs = param ("vel");
    for (auto& v : slicers)
    {
        if (! v.on) continue;
        const float g = ((1.0f - vs) + vs * v.vel * v.vel);
        for (int i = 0; i < n; ++i)
        {
            if (! v.releasing) { v.env = std::min (1.0f, v.env + 1.0f / attS); if (v.age >= v.gateLen) v.releasing = true; }
            else { v.env -= v.relK; if (v.env <= 0.0f) { v.on = false; break; } }
            const bool inside = v.rev ? v.pos >= v.start : v.pos < v.end;
            if (! inside) { v.on = false; break; }
            const float xl = v.fl.tickLP (hermiteAt (dl, total, v.pos));
            const float xr = v.fr.tickLP (hermiteAt (dr, total, v.pos));
            L[i] += xl * v.env * g * v.gl;
            R[i] += xr * v.env * g * v.gr;
            v.pos += v.rate; ++v.age;
        }
    }
}

void Processor::processLoop (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const Transport& tr)
{
    const int n = buffer.getNumSamples();
    buffer.clear();
    auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
    const bool run = param ("run") > 0.5f;
    lastBpm = (float) ((tr.playing && tr.hasPpq) || ! run ? tr.bpm : param ("bpm"));

    struct Ev { int s; int slice; float vel; bool rev; };
    std::array<Ev, 256> evs; int ne = 0;
    auto push = [&] (Ev e) { if (ne < (int) evs.size()) evs[(size_t) ne++] = e; };

    int s1, n1, s2, n2;
    uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1 + n2; ++i)
    {
        const auto& q = uiQueue[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
        push ({ 0, q.first, q.second, false });
    }
    uiFifo.finishedRead (n1 + n2);
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn() && msg.getNoteNumber() >= 36) push ({ m.samplePosition, msg.getNoteNumber() - 36, msg.getFloatVelocity(), false });
    }
    std::array<TrackCtl, 1> ctl { TrackCtl { (int) std::round (param ("len")), param ("prob"), param ("shift"), param ("mute") > 0.5f } };
    Pattern<1> pat;
    { const juce::SpinLock::ScopedLockType sl (bankLock); pat = loopBank.p[(size_t) currentPattern()]; }
    const float revp = param ("revprob");
    loopSeq.run (pat, ctl, n, tr.playing, tr.hasPpq, tr.ppq, lastBpm, run, param ("swing"), param ("micro"), rng,
                 [&] (const SeqEvent& e)
                 {
                     const Step& st = pat.t[0][(size_t) e.step];
                     push ({ e.sample, st.slice, e.vel, st.rev || rng.uni() < revp });
                 });
    playPpq.store (loopSeq.isRunning() ? (float) loopSeq.positionPpq() : -1.0f);
    std::sort (evs.begin(), evs.begin() + ne, [] (const Ev& a, const Ev& b) { return a.s < b.s; });

    int pos = 0;
    for (int i = 0; i < ne; ++i)
    {
        const int at = std::clamp (evs[(size_t) i].s, 0, n);
        renderSlices (L + pos, R + pos, at - pos); pos = std::max (pos, at);
        triggerSlice (evs[(size_t) i].slice, evs[(size_t) i].vel, evs[(size_t) i].rev);
    }
    renderSlices (L + pos, R + pos, n - pos);
    const float lv = levelToGain (param ("level"));
    masterSm.target = lv * dbToGain (param ("master"));
    for (int i = 0; i < n; ++i) { const float g = masterSm.tick(); L[i] *= g; R[i] *= g; }
    midi.clear();
}

void Processor::detectOnsets (LoopSample& s)
{
    s.onsets.clear();
    const int n = s.buf.getNumSamples(), hop = 256;
    if (n < hop * 4) return;
    std::vector<float> e;
    for (int i = 0; i + hop <= n; i += hop)
    {
        float acc = 0;
        for (int c = 0; c < s.buf.getNumChannels(); ++c)
        {
            const float* d = s.buf.getReadPointer (c);
            for (int k = 0; k < hop; ++k) acc += d[i + k] * d[i + k];
        }
        e.push_back (std::log (1.0e-9f + acc));
    }
    std::vector<float> flux (e.size(), 0.0f);
    for (size_t i = 1; i < e.size(); ++i) flux[i] = std::max (0.0f, e[i] - e[i - 1]);
    float mean = 0; for (float f : flux) mean += f; mean /= (float) flux.size();
    float var = 0; for (float f : flux) var += (f - mean) * (f - mean); var /= (float) flux.size();
    const float thr = mean + 1.2f * std::sqrt (var);
    const int minGap = (int) (0.045 * s.rate / hop);
    int last = -minGap;
    for (size_t i = 1; i + 1 < flux.size(); ++i)
        if (flux[i] > thr && flux[i] >= flux[i - 1] && flux[i] >= flux[i + 1] && (int) i - last >= minGap)
        {
            s.onsets.push_back (std::max (0, (int) i * hop - hop - (int) (0.002 * s.rate)));
            last = (int) i;
        }
    if (s.onsets.empty() || s.onsets.front() > (int) (0.01 * s.rate)) s.onsets.insert (s.onsets.begin(), 0);
}

std::shared_ptr<LoopSample> Processor::renderDefaultLoop()
{
    // one bar of the kit's "a3 tok" pattern at 124 bpm, rendered with the kit's own voices
    auto s = std::make_shared<LoopSample>();
    s->rate = 48000.0; s->name = "faltung break (kt1 a3)";
    const double bpm = 124.0, spb = 60.0 / bpm * s->rate;
    const int len = (int) (4 * spb);
    s->buf.setSize (2, len); s->buf.clear();
    Pattern<8> pat; parseKit (presets (Pid::kt1)[0].pattern, pat);
    std::array<Voice, 8> vs;
    for (int v = 0; v < 8; ++v) vs[(size_t) v].prepare ((float) s->rate, 99u + (uint32_t) v * 31u);
    struct E { int at; int v; float vel; };
    std::vector<E> ev;
    for (int k = 0; k < 16; ++k)
        for (int v = 0; v < 8; ++v)
        {
            const auto& st = pat.t[(size_t) v][(size_t) k];
            if (! st.on) continue;
            const double q = k * 0.25 + ((k & 1) ? 0.03 : 0.0) + st.micro * 0.012 * bpm / 60.0;
            ev.push_back ({ std::clamp ((int) (q * spb), 0, len - 1), v, st.vel });
        }
    std::sort (ev.begin(), ev.end(), [] (const E& a, const E& b) { return a.at < b.at; });
    float* L = s->buf.getWritePointer (0); float* R = s->buf.getWritePointer (1);
    int pos = 0;
    for (auto& e : ev)
    {
        for (auto& v : vs) v.render (L + pos, R + pos, e.at - pos);
        pos = e.at;
        vs[(size_t) e.v].trigger (kitVoice (e.v).type, defaultVoice (kitVoice (e.v).type), e.vel, 0.0f);
    }
    for (auto& v : vs) v.render (L + pos, R + pos, len - pos);
    s->buf.applyGain (0.8f / std::max (0.1f, s->buf.getMagnitude (0, len)));
    detectOnsets (*s);
    return s;
}

bool Processor::loadSample (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->lengthInSamples < 64) return false;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    auto s = std::make_shared<LoopSample>();
    s->buf.setSize (2, len);
    r->read (&s->buf, 0, len, 0, true, true);
    if (r->numChannels == 1) s->buf.copyFrom (1, 0, s->buf, 0, 0, len);
    s->rate = r->sampleRate; s->name = f.getFileNameWithoutExtension();
    detectOnsets (*s);
    // guess the loop length in beats from the current tempo
    const double secs = len / s->rate, beat = 60.0 / std::max (20.0f, lastBpm);
    const double beats = juce::jlimit (1.0, 16.0, std::round (secs / beat));
    setSample (s);
    setParam ("loopbeats", (float) beats);
    samplePath = f.getFullPathName();
    return true;
}

juce::String Processor::sampleToBase64 (const LoopSample& s) const
{
    juce::MemoryBlock mb;
    {
        auto* os = new juce::MemoryOutputStream (mb, false);
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, s.rate, 2, 24, {}, 0));
        if (! w) { delete os; return {}; }
        w->writeFromAudioSampleBuffer (s.buf, 0, s.buf.getNumSamples());
    }
    return mb.toBase64Encoding();
}

std::shared_ptr<LoopSample> Processor::sampleFromBase64 (const juce::String& b64, const juce::String& name) const
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (b64)) return {};
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, true), true));
    if (! r) return {};
    auto s = std::make_shared<LoopSample>();
    s->buf.setSize (2, (int) r->lengthInSamples);
    r->read (&s->buf, 0, (int) r->lengthInSamples, 0, true, true);
    s->rate = r->sampleRate; s->name = name;
    detectOnsets (*s);
    return s;
}

// ---- effects --------------------------------------------------------------------------------------
void Processor::processEffect (juce::AudioBuffer<float>& buffer, const Transport& tr)
{
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto p = [this] (const char* id) { return param (id); };
    switch (pid)
    {
        case Pid::dl4:
        {
            DubParams d;
            d.sync = p ("sync") > 0.5f; d.division = (int) p ("division"); d.ms = p ("time");
            d.feedback = p ("feedback"); d.age = p ("age"); d.wow = p ("wow"); d.hp = p ("hp"); d.lp = p ("lp");
            d.spread = p ("spread"); d.mode = (int) p ("mode"); d.freeze = p ("freeze") > 0.5f; d.mix = p ("mix"); d.input = p ("input");
            dubFx->process (L, R, n, d, tr);
            break;
        }
        case Pid::rv5:
        {
            RoomParams r;
            r.model = (int) p ("model"); r.size = p ("size"); r.decay = p ("decay"); r.predelay = p ("predelay");
            r.damp = p ("damp"); r.lowcut = p ("lowcut"); r.diffusion = p ("diffusion"); r.mod = p ("mod");
            r.gate = p ("gate"); r.width = p ("width"); r.mix = p ("mix");
            roomFx->process (L, R, n, r);
            break;
        }
        case Pid::fl7:
        {
            FilterParams f;
            f.low = p ("low"); f.mid = p ("mid"); f.high = p ("high"); f.xlow = p ("xlow"); f.xhigh = p ("xhigh");
            f.sweep = p ("sweep"); f.res = p ("res"); f.drive = p ("drive");
            f.lfoDiv = (int) p ("lfodiv"); f.lfoDepth = p ("lfodepth"); f.lfoShape = (int) p ("lfoshape"); f.mix = p ("mix");
            filterFx->process (L, R, n, f, tr);
            break;
        }
        case Pid::gl8:
        {
            static const float ratios[4] = { 1.5f, 2.0f, 4.0f, 10.0f };
            GlueParams g;
            g.threshold = p ("threshold"); g.ratio = ratios[std::clamp ((int) p ("ratio"), 0, 3)];
            g.attack = p ("attack"); g.release = p ("release"); g.autoRelease = p ("auto") > 0.5f;
            g.scHp = p ("schp"); g.makeup = p ("makeup"); g.tape = p ("tape"); g.bump = p ("bump");
            g.hf = p ("hf"); g.bias = p ("bias"); g.mix = p ("mix"); g.out = p ("out");
            glueFx->process (L, R, n, g);
            break;
        }
        case Pid::mc3:
        {
            MicroParams m;
            m.mode = (int) p ("mode"); m.division = (int) p ("division"); m.density = p ("density"); m.sizeMs = p ("size");
            m.pitch = p ("pitch"); m.scatter = p ("scatter"); m.reverse = p ("reverse"); m.shape = p ("shape");
            m.mix = p ("mix"); m.hold = p ("hold") > 0.5f;
            microFx->process (L, R, n, m, tr);
            break;
        }
        case Pid::ms0:
        {
            const auto mp = readMaster();
            for (int off = 0; off < n; off += 4096)
                masterFx->process (L + off, R + off, std::min (4096, n - off), mp);
            break;
        }
        case Pid::sw6:
        {
            SwingParams s;
            s.templ = (int) p ("template"); s.swing = p ("swing"); s.amount = p ("amount"); s.humanize = p ("humanize");
            s.accent = p ("accent"); s.xfadeMs = p ("xfade");
            for (int i = 0; i < 16; ++i) s.step[(size_t) i] = param ("step" + juce::String (i + 1));
            swingFx->process (L, R, n, s, tr);
            playPpq.store (tr.playing ? (float) tr.ppq : -1.0f);
            break;
        }
        default: break;
    }
}

void Processor::swingLearn (bool start)
{
    if (! swingFx) return;
    if (start)
    {
        swingFx->learnSum.fill (0); swingFx->learnCnt.fill (0);
        swingFx->learning.store (true);
        return;
    }
    swingFx->learning.store (false);
    setParam ("template", 0.0f);
    setParam ("amount", 1.0f);
    for (int i = 0; i < 16; ++i)
    {
        const int c = swingFx->learnCnt[(size_t) i];
        const float avg = c > 0 ? swingFx->learnSum[(size_t) i] / (float) c : 0.0f;
        setParam ("step" + juce::String (i + 1), juce::jlimit (-0.5f, 0.5f, avg));
    }
}

// ---- programs + state -----------------------------------------------------------------------------
const juce::String Processor::getProgramName (int i)
{
    const auto& pr = presets (pid);
    return i >= 0 && i < (int) pr.size() ? pr[(size_t) i].name : juce::String ("init");
}

void Processor::setCurrentProgram (int i)
{
    const auto& pr = presets (pid);
    if (i < 0 || i >= (int) pr.size()) return;
    currentProgram = i;
    // a preset is complete: everything it does not mention goes back to its default
    static const juce::StringArray keep { "pattern", "run", "bpm", "master", "loopbeats", "listen", "bypass", "match", "trim" };
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! keep.contains (r->getParameterID()) && std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-6f)
            {
                r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
            }
    for (auto& [id, v] : pr[(size_t) i].values) setParam (id, v);
    if (pinfo.kind == Kind::Machine && pr[(size_t) i].pattern.isNotEmpty()) applyPatternText (pr[(size_t) i].pattern, currentPattern());
    else if (pid == Pid::kt1) applyPatternText ({}, currentPattern());
}

void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();
    xml->setAttribute ("program", currentProgram);
    xml->setAttribute ("product", pinfo.code);
    {
        const juce::SpinLock::ScopedLockType sl (bankLock);
        if (pid == Pid::kt1) xml->createNewChildElement ("bank")->addTextElement (kitBank.toString());
        if (pid == Pid::kt2) xml->createNewChildElement ("bank")->addTextElement (loopBank.toString());
    }
    if (pid == Pid::kt2)
        if (auto s = getSample())
        {
            auto* e = xml->createNewChildElement ("sample");
            e->setAttribute ("name", s->name);
            e->setAttribute ("path", samplePath);
            if (s->buf.getNumSamples() / s->rate <= 40.0) e->setAttribute ("flac", sampleToBase64 (*s));
        }
    copyXmlToBinary (*xml, dest);
}

void Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml) return;
    currentProgram = xml->getIntAttribute ("program", 0);
    if (auto* b = xml->getChildByName ("bank"))
    {
        const auto txt = b->getAllSubText().toStdString();
        const juce::SpinLock::ScopedLockType sl (bankLock);
        if (pid == Pid::kt1) kitBank.fromString (txt);
        if (pid == Pid::kt2) loopBank.fromString (txt);
    }
    if (auto* e = xml->getChildByName ("sample"))
    {
        std::shared_ptr<LoopSample> s;
        if (e->hasAttribute ("flac")) s = sampleFromBase64 (e->getStringAttribute ("flac"), e->getStringAttribute ("name"));
        samplePath = e->getStringAttribute ("path");
        if (s) setSample (s);
        else if (juce::File (samplePath).existsAsFile()) loadSample (juce::File (samplePath));
    }
    xml->deleteAllChildElementsWithTagName ("bank");
    xml->deleteAllChildElementsWithTagName ("sample");
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---- wb1 wub ---------------------------------------------------------------------------------------
BassParams Processor::readBass() const
{
    BassParams b;
    auto osc = [this] (const juce::String& x, BassParams::Osc& o)
    {
        o.shape = param (x + "shape"); o.pw = param (x + "pw"); o.oct = (int) param (x + "oct") - 3;
        o.semi = param (x + "semi"); o.fine = param (x + "fine"); o.level = param (x + "level");
        o.unison = (int) param (x + "uni") + 1; o.detune = param (x + "detune"); o.spread = param (x + "spread");
    };
    osc ("a_", b.a); osc ("b_", b.b);
    b.fm = param ("fm"); b.sub = param ("sub"); b.subWave = (int) param ("subwave"); b.subOct = (int) param ("suboct") - 2; b.noise = param ("noise");
    b.fType = (int) param ("ftype"); b.cutoff = param ("cutoff"); b.res = param ("res"); b.fDrive = param ("fdrive"); b.keytrack = param ("keytrack");
    b.vowel = param ("vowel"); b.fmtQ = param ("fmtq"); b.fmtShift = param ("fmtshift"); b.fmtMix = param ("fmtmix");
    b.combTune = param ("combtune"); b.combFb = param ("combfb"); b.combMix = param ("combmix");
    b.dMode = (int) param ("dmode"); b.dDrive = param ("ddrive"); b.dTone = param ("dtone"); b.dMix = param ("dmix"); b.order = (int) param ("order");
    b.wDiv = (int) param ("wdiv"); b.wShape = (int) param ("wshape"); b.wRetrig = param ("wretrig") > 0.5f; b.wPhase = param ("wphase");
    b.wCut = param ("wcut"); b.wVowel = param ("wvowel"); b.wFm = param ("wfm"); b.wDrive = param ("wdrive");
    b.wPitch = param ("wpitch"); b.wAmp = param ("wamp"); b.wComb = param ("wcomb");
    b.mA = param ("ma"); b.mD = param ("md"); b.mS = param ("ms"); b.mR = param ("mr");
    b.mCut = param ("mcut"); b.mPitch = param ("mpitch"); b.mFm = param ("mfm"); b.mVowel = param ("mvowel");
    b.aA = param ("aa"); b.aD = param ("ad"); b.aS = param ("as"); b.aR = param ("ar");
    b.ott = param ("ott"); b.ottTime = param ("otttime");
    b.mono = (int) param ("voices") == 0; b.legato = param ("legato") > 0.5f; b.glide = param ("glide"); b.bend = param ("bend");
    b.wheel = (int) param ("wheel"); b.vel = param ("vel"); b.width = param ("width"); b.level = param ("level");
    return b;
}

void Processor::processSynth (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const Transport& tr)
{
    const int n = buffer.getNumSamples();
    std::array<BassEvent, 512> ev; int ne = 0;
    auto push = [&] (BassEvent e) { if (ne < (int) ev.size()) ev[(size_t) ne++] = e; };
    int s1, n1, s2, n2;
    uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1 + n2; ++i)
    {
        const auto& q = uiQueue[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
        push ({ 0, q.second > 0 ? 0 : 1, q.first, q.second });
    }
    uiFifo.finishedRead (n1 + n2);
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = std::clamp (m.samplePosition, 0, n - 1);
        if (msg.isNoteOn()) { push ({ at, 0, msg.getNoteNumber(), msg.getFloatVelocity() }); hitCount[0].fetch_add (1); }
        else if (msg.isNoteOff()) push ({ at, 1, msg.getNoteNumber(), 0 });
        else if (msg.isPitchWheel()) push ({ at, 2, 0, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f });
        else if (msg.isController() && msg.getControllerNumber() == 1) push ({ at, 3, 0, msg.getControllerValue() / 127.0f });
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) push ({ at, 4, 0, 0 });
    }
    std::stable_sort (ev.begin(), ev.begin() + ne, [] (const BassEvent& a, const BassEvent& b) { return a.sample < b.sample; });
    auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
    bassFx->process (L, R, n, readBass(), tr, ev.data(), ne);
    midi.clear();
}


// ---- ms0 master ----------------------------------------------------------------------------------------
MasterParams Processor::readMaster() const
{
    MasterParams m;
    auto b = [this] (const juce::String& id) { return param (id) > 0.5f; };
    auto I = [this] (const juce::String& id) { return (int) param (id); };
    static const int orders[4] = { 2, 4, 8, 16 };
    m.trim = param ("trim"); m.listen = I ("listen"); m.bypass = b ("bypass"); m.match = b ("match");
    m.eqOn = b ("eqon");
    for (int k = 0; k < 8; ++k)
    {
        const juce::String x = "e" + juce::String (k + 1);
        auto& e = m.eq[(size_t) k];
        e.on = b (x + "on"); e.type = I (x + "type"); e.freq = param (x + "freq"); e.gain = param (x + "gain"); e.q = param (x + "q"); e.ch = I (x + "ch");
    }
    m.hpOn = b ("hpon"); m.hpF = param ("hpfreq"); m.hpOrd = orders[std::clamp (I ("hpslope"), 0, 3)]; m.hpCh = I ("hpch");
    m.lpOn = b ("lpon"); m.lpF = param ("lpfreq"); m.lpOrd = orders[std::clamp (I ("lpslope"), 0, 3)]; m.lpCh = I ("lpch");
    m.dynOn = b ("dynon");
    for (int k = 0; k < 2; ++k)
    {
        const juce::String x = "d" + juce::String (k + 1);
        auto& d = m.dyn[(size_t) k];
        d.on = b (x + "on"); d.ch = I (x + "ch"); d.freq = param (x + "freq"); d.q = param (x + "q"); d.thr = param (x + "thr");
        d.range = param (x + "range"); d.attack = param (x + "att"); d.release = param (x + "rel");
    }
    m.mbOn = b ("mbon"); m.x1 = param ("x1"); m.x2 = param ("x2"); m.x3 = param ("x3");
    for (int k = 0; k < 4; ++k)
    {
        const juce::String x = "b" + juce::String (k + 1);
        auto& d = m.mb[(size_t) k];
        d.thr = param (x + "thr"); d.ratio = param (x + "ratio"); d.attack = param (x + "att"); d.release = param (x + "rel");
        d.gain = param (x + "gain"); d.width = param (x + "width"); d.solo = b (x + "solo");
    }
    m.imgOn = b ("imgon"); m.air = param ("air");
    m.satOn = b ("saton"); m.satCurve = I ("satcurve"); m.satFocus = I ("satfocus"); m.satDrive = param ("satdrive"); m.satMix = param ("satmix");
    m.ambOn = b ("ambon"); m.ambLevel = param ("amblevel"); m.ambSize = param ("ambsize"); m.ambDecay = param ("ambdecay");
    m.ambHp = param ("ambhp"); m.ambLp = param ("amblp"); m.ambWidth = param ("ambwidth");
    m.glueOn = b ("glueon"); m.gThr = param ("gthr"); m.gRatio = param ("gratio"); m.gAtt = param ("gatt"); m.gRel = param ("grel");
    m.gAuto = b ("gauto"); m.gMakeup = param ("gmakeup"); m.gMix = param ("gmix");
    m.limOn = b ("limon"); m.limGain = param ("limgain"); m.ceiling = param ("ceiling"); m.limRel = param ("limrel");
    m.limAuto = b ("limauto"); m.lookahead = param ("lookahead");
    m.out = param ("out"); m.dither = I ("dither"); m.shaping = b ("shaping");
    return m;
}

} // namespace fal

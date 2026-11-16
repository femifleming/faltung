#include "Session.h"
#include "../dsp/Fx.h"

namespace fal::daw
{
// ---- model helpers --------------------------------------------------------------------------------
double divisionBeats (int i) { const auto& d = divisions(); return d[(size_t) juce::jlimit (0, (int) d.size() - 1, i)].beats; }
juce::StringArray divisionNames() { juce::StringArray a; for (auto& d : divisions()) a.add (d.name); return a; }

float AutoLane::valueAt (double beat) const
{
    if (pts.empty()) return 0.0f;
    if (beat <= pts.front().beat) return pts.front().value;
    if (beat >= pts.back().beat) return pts.back().value;
    size_t lo = 0, hi = pts.size() - 1;
    while (hi - lo > 1) { const size_t mid = (lo + hi) / 2; if (pts[mid].beat <= beat) lo = mid; else hi = mid; }
    const auto& a = pts[lo]; const auto& b = pts[hi];
    const double span = std::max (1.0e-9, b.beat - a.beat);
    float t = (float) juce::jlimit (0.0, 1.0, (beat - a.beat) / span);
    const float c = juce::jlimit (-1.0f, 1.0f, a.curve);
    if (c > 0.001f) t = std::pow (t, 1.0f + 4.0f * c);
    else if (c < -0.001f) t = 1.0f - std::pow (1.0f - t, 1.0f - 4.0f * c);
    return a.value + (b.value - a.value) * t;
}

// ---- Track ----------------------------------------------------------------------------------------
Track::Track()
{
    mixer = Device::makeMixer();
    mod = std::make_shared<const ModDef>();
    clips = std::make_shared<const std::vector<Clip>>();
    chain = std::make_shared<const std::vector<int>>();
    for (auto& p : sendPre) p.store (false);
    for (auto& s : showLfo) s.store (0.0f);
}
Device* Track::device (int devUid)
{
    if (devUid == 0) return mixer.get();
    if (instrument && instrument->uid == devUid) return instrument.get();
    for (auto& d : inserts) if (d->uid == devUid) return d.get();
    return nullptr;
}

// ---- Session --------------------------------------------------------------------------------------
Session::Session()
{
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    global = Device::makeGlobal();
    for (int i = 0; i < kMacros; ++i) macroNames[(size_t) i] = "macro " + juce::String (i + 1);
    master = std::make_unique<Track>(); master->uid = 0; master->name = "master"; master->colour = 0xfff6f6f0;
    for (int r = 0; r < kReturns; ++r)
    {
        auto t = std::make_unique<Track>(); t->uid = -1 - r; t->name = "return " + juce::String (r + 1); t->colour = trackColour (r + 4);
        returns[(size_t) r] = std::move (t);
    }
    std::vector<Scene> sc ((size_t) kScenes);
    for (int i = 0; i < kScenes; ++i) sc[(size_t) i].name = juce::String (i + 1);
    scenes = std::make_shared<const std::vector<Scene>> (std::move (sc));
    song = std::make_shared<const std::vector<SongEntry>>();
    markers = std::make_shared<const std::vector<Marker>>();
    lanes = std::make_shared<const std::vector<LanePtr>>();
    midiMaps = std::make_shared<const std::vector<MidiMap>>();
    for (auto& c : cc) c.store (0.0f);
    loadKnown();
}
Session::~Session()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    tracks.clear();
}

juce::File Session::dataDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (kDeep ? "faltung deep studio" : "faltung studio");
    d.createDirectory();
    return d;
}
juce::File Session::knownFile() const { return dataDir().getChildFile ("plugins.xml"); }
void Session::loadKnown()
{
    if (auto x = juce::XmlDocument::parse (knownFile())) known.recreateFromXml (*x);
}
void Session::saveKnown()
{
    if (auto x = known.createXml()) x->writeTo (knownFile());
}

juce::uint32 Session::trackColour (int i)
{
    static const juce::uint32 c[12] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0,
                                        0xffc9b53b, 0xff79e04a, 0xff8593d4, 0xffe3bf5f };
    return c[((i % 12) + 12) % 12];
}
juce::String Session::uniqueTrackName (const juce::String& base) const
{
    juce::String n = base; int k = 2;
    auto taken = [&] (const juce::String& s) { for (auto& t : tracks) if (t->name == s) return true; return false; };
    while (taken (n)) n = base + " " + juce::String (k++);
    return n;
}

Track* Session::addTrack (const juce::String& name, int at)
{
    if ((int) tracks.size() >= kMaxTracks) return nullptr;
    auto t = std::make_unique<Track>();
    t->uid = nextUid++;
    t->name = uniqueTrackName (name.isEmpty() ? "track" : name);
    t->colour = trackColour ((int) tracks.size());
    if (prepared) prepareTrack (*t);
    Track* raw = t.get();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (at < 0 || at > (int) tracks.size()) tracks.push_back (std::move (t));
        else tracks.insert (tracks.begin() + at, std::move (t));
    }
    if (at >= 0 && at < (int) tracks.size() - 1)
        editScenes ([at] (std::vector<Scene>& s) { for (auto& sc : s) { for (int i = kMaxTracks - 1; i > at; --i) sc.pattern[(size_t) i] = sc.pattern[(size_t) i - 1]; sc.pattern[(size_t) at] = -2; } });
    dirty = true;
    return raw;
}

void Session::removeTrack (int index)
{
    if (index < 0 || index >= (int) tracks.size()) return;
    std::unique_ptr<Track> gone;
    const int uid = tracks[(size_t) index]->uid;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        gone = std::move (tracks[(size_t) index]);
        tracks.erase (tracks.begin() + index);
    }
    editScenes ([index] (std::vector<Scene>& s) { for (auto& sc : s) { for (int i = index; i < kMaxTracks - 1; ++i) sc.pattern[(size_t) i] = sc.pattern[(size_t) i + 1]; sc.pattern[kMaxTracks - 1] = -2; } });
    // lanes and maps that pointed at it
    auto nl = std::make_shared<std::vector<LanePtr>>();
    for (auto& l : *lanes) if (l->target.track != uid) nl->push_back (l);
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = nl; }
    editMidiMaps ([uid] (std::vector<MidiMap>& m) { m.erase (std::remove_if (m.begin(), m.end(), [uid] (const MidiMap& x) { return x.target.track == uid; }), m.end()); });
    rebuildLatency();
    dirty = true;
}

void Session::moveTrack (int from, int to)
{
    if (from == to || from < 0 || from >= (int) tracks.size() || to < 0 || to >= (int) tracks.size()) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto t = std::move (tracks[(size_t) from]);
        tracks.erase (tracks.begin() + from);
        tracks.insert (tracks.begin() + to, std::move (t));
    }
    editScenes ([from, to] (std::vector<Scene>& s) { for (auto& sc : s) { const int v = sc.pattern[(size_t) from]; if (from < to) for (int i = from; i < to; ++i) sc.pattern[(size_t) i] = sc.pattern[(size_t) i + 1]; else for (int i = from; i > to; --i) sc.pattern[(size_t) i] = sc.pattern[(size_t) i - 1]; sc.pattern[(size_t) to] = v; } });
    dirty = true;
}

Track* Session::trackByUid (int uid)
{
    if (uid == 0) return master.get();
    if (uid < 0) return (-uid - 1) < kReturns ? returns[(size_t) (-uid - 1)].get() : nullptr;
    for (auto& t : tracks) if (t->uid == uid) return t.get();
    return nullptr;
}
Track* Session::trackAt (int sel)
{
    if (sel == -100) return master.get();
    if (sel < 0) return (-sel - 1) < kReturns ? returns[(size_t) (-sel - 1)].get() : nullptr;
    return sel < (int) tracks.size() ? tracks[(size_t) sel].get() : nullptr;
}
int Session::indexOf (const Track* t) const
{
    for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].get() == t) return i;
    if (t == master.get()) return -100;
    for (int r = 0; r < kReturns; ++r) if (returns[(size_t) r].get() == t) return -1 - r;
    return -1;
}

juce::StringArray Session::internalInstrumentTypes()
{
    juce::StringArray a;
    for (int i = 0; i < kNumProducts; ++i)
    {
        const auto& pi = info (i);
        if (pi.kind == fal::Kind::Module || pi.kind == fal::Kind::Synth) a.add ("fal:" + juce::String (pi.code));
    }
    return a;
}
juce::StringArray Session::internalEffectTypes()
{
    juce::StringArray a;
    for (int i = 0; i < kNumProducts; ++i)
    {
        const auto& pi = info (i);
        if (pi.kind == fal::Kind::Effect && ! (kDeep && pi.id == Pid::ms0)) a.add ("fal:" + juce::String (pi.code));
    }
    return a;
}
juce::String Session::typeDisplayName (const juce::String& typeId)
{
    if (typeId.startsWith ("fal:"))
    {
        const auto code = typeId.substring (4);
        for (int i = 0; i < kNumProducts; ++i) if (code == info (i).code) return code + " " + info (i).name;
    }
    return typeId.fromFirstOccurrenceOf (":", false, false);
}

std::unique_ptr<Device> Session::createDevice (const juce::String& typeId, juce::String* error)
{
    if (typeId.startsWith ("fal:"))
    {
        const auto code = typeId.substring (4);
        for (int i = 0; i < kNumProducts; ++i)
            if (code == info (i).code) return Device::makeInternal (nextUid++, (Pid) i);
        if (error) *error = "unknown device " + typeId;
        return nullptr;
    }
    const auto id = typeId.fromFirstOccurrenceOf (":", false, false);
    if (auto d = known.getTypeForIdentifierString (id)) return createHosted (*d, error);
    if (error) *error = "plug-in not found: " + id + " (scan your plug-ins in the browser)";
    return nullptr;
}

std::unique_ptr<Device> Session::createHosted (const juce::PluginDescription& desc, juce::String* error)
{
    juce::String err;
    auto inst = formats.createPluginInstance (desc, sr, bs, err);
    if (! inst) { if (error) *error = err; return nullptr; }
    return Device::makeHosted (nextUid++, std::move (inst), desc);
}

void Session::setInstrument (Track& t, std::unique_ptr<Device> d)
{
    if (d && prepared) { d->setPlayHead (this); d->prepare (sr, bs); }
    std::unique_ptr<Device> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (t.instrument);
        t.instrument = std::move (d);
    }
    if (old && t.name.startsWith ("track") && t.instrument) t.name = uniqueTrackName (t.instrument->name.upToFirstOccurrenceOf (" ", false, false));
    old.reset();
    rebuildLatency();
    dirty = true;
}

Device* Session::addInsert (Track& t, std::unique_ptr<Device> d, int at)
{
    if (! d || (int) t.inserts.size() >= kMaxInserts) return nullptr;
    if (prepared) { d->setPlayHead (this); d->prepare (sr, bs); }
    Device* raw = d.get();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (at < 0 || at > (int) t.inserts.size()) t.inserts.push_back (std::move (d));
        else t.inserts.insert (t.inserts.begin() + at, std::move (d));
    }
    rebuildLatency();
    dirty = true;
    return raw;
}

std::unique_ptr<Device> Session::removeDevice (Track& t, int devUid)
{
    std::unique_ptr<Device> gone;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (t.instrument && t.instrument->uid == devUid) gone = std::move (t.instrument);
        else
            for (size_t i = 0; i < t.inserts.size(); ++i)
                if (t.inserts[i]->uid == devUid) { gone = std::move (t.inserts[i]); t.inserts.erase (t.inserts.begin() + (long) i); break; }
    }
    if (gone)
    {
        auto nl = std::make_shared<std::vector<LanePtr>>();
        for (auto& l : *lanes) if (! (l->target.track == t.uid && l->target.device == devUid)) nl->push_back (l);
        { const juce::SpinLock::ScopedLockType sl (lock); lanes = nl; }
        editMod (t, [devUid] (ModDef& m) { for (auto& s : m.slots) if (s.device == devUid) { s.param = -1; s.device = 0; } });
        rebuildLatency();
        dirty = true;
    }
    return gone;
}

void Session::moveInsert (Track& t, int from, int to)
{
    if (from == to || from < 0 || from >= (int) t.inserts.size() || to < 0 || to >= (int) t.inserts.size()) return;
    const juce::SpinLock::ScopedLockType sl (lock);
    auto d = std::move (t.inserts[(size_t) from]);
    t.inserts.erase (t.inserts.begin() + from);
    t.inserts.insert (t.inserts.begin() + to, std::move (d));
    dirty = true;
}

void Session::rebuildLatency()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    auto chain = [] (Track& t)
    {
        int l = 0;
        if (t.instrument && ! t.instrument->bypass.load()) l += t.instrument->latency();
        for (auto& d : t.inserts) if (! d->bypass.load()) l += d->latency();
        return l;
    };
    latTrackMax = 0; latRetMax = 0;
    for (auto& t : tracks) { t->latency = chain (*t); latTrackMax = std::max (latTrackMax, t->latency); }
    for (auto& r : returns) { r->latency = chain (*r); latRetMax = std::max (latRetMax, r->latency); }
    master->latency = chain (*master); latMaster = master->latency;
    for (auto& t : tracks) t->alignDelay = latTrackMax - t->latency;
    for (auto& r : returns) r->alignDelay = latRetMax - r->latency;
    master->alignDelay = 0;
    latTotal = latTrackMax + latRetMax + latMaster;
}

// ---- copy-on-write edits ---------------------------------------------------------------------------
void Session::editPattern (Track& t, int i, const std::function<void (Pattern&)>& fn)
{
    if (i < 0 || i >= kPatterns) return;
    Pattern p = t.patterns[(size_t) i] ? *t.patterns[(size_t) i] : Pattern();
    fn (p);
    setPattern (t, i, std::make_shared<const Pattern> (p));
}
void Session::setPattern (Track& t, int i, PatternPtr p)
{
    if (i < 0 || i >= kPatterns) return;
    PatternPtr old;
    { const juce::SpinLock::ScopedLockType sl (lock); old = t.patterns[(size_t) i]; t.patterns[(size_t) i] = p; }
    dirty = true;
}
void Session::editClips (Track& t, const std::function<void (std::vector<Clip>&)>& fn)
{
    auto v = std::make_shared<std::vector<Clip>> (*t.clips);
    fn (*v);
    std::sort (v->begin(), v->end(), [] (const Clip& a, const Clip& b) { return a.startBar < b.startBar; });
    { const juce::SpinLock::ScopedLockType sl (lock); t.clips = v; }
    dirty = true;
}
void Session::setChain (Track& t, std::vector<int> c)
{
    auto v = std::make_shared<const std::vector<int>> (std::move (c));
    { const juce::SpinLock::ScopedLockType sl (lock); t.chain = v; }
    dirty = true;
}
void Session::editMod (Track& t, const std::function<void (ModDef&)>& fn)
{
    ModDef m = *t.mod; fn (m);
    auto v = std::make_shared<const ModDef> (m);
    { const juce::SpinLock::ScopedLockType sl (lock); t.mod = v; }
    dirty = true;
}
void Session::editScenes (const std::function<void (std::vector<Scene>&)>& fn)
{
    auto v = std::make_shared<std::vector<Scene>> (*scenes); fn (*v);
    { const juce::SpinLock::ScopedLockType sl (lock); scenes = v; }
    dirty = true;
}
void Session::editSong (const std::function<void (std::vector<SongEntry>&)>& fn)
{
    auto v = std::make_shared<std::vector<SongEntry>> (*song); fn (*v);
    { const juce::SpinLock::ScopedLockType sl (lock); song = v; }
    dirty = true;
}
void Session::editMarkers (const std::function<void (std::vector<Marker>&)>& fn)
{
    auto v = std::make_shared<std::vector<Marker>> (*markers); fn (*v);
    std::sort (v->begin(), v->end(), [] (const Marker& a, const Marker& b) { return a.bar < b.bar; });
    { const juce::SpinLock::ScopedLockType sl (lock); markers = v; }
    dirty = true;
}
void Session::editMidiMaps (const std::function<void (std::vector<MidiMap>&)>& fn)
{
    auto v = std::make_shared<std::vector<MidiMap>> (*midiMaps); fn (*v);
    { const juce::SpinLock::ScopedLockType sl (lock); midiMaps = v; }
    dirty = true;
}
LanePtr Session::lane (const Target& t) const
{
    for (auto& l : *lanes) if (l->target == t) return l;
    return nullptr;
}
void Session::editLane (const Target& t, const std::function<void (AutoLane&)>& fn)
{
    auto v = std::make_shared<std::vector<LanePtr>> (*lanes);
    AutoLane l; l.target = t;
    size_t at = v->size();
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i]->target == t) { l = *(*v)[i]; at = i; break; }
    fn (l);
    std::stable_sort (l.pts.begin(), l.pts.end(), [] (const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
    auto np = std::make_shared<const AutoLane> (l);
    if (at < v->size()) (*v)[at] = np; else v->push_back (np);
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = v; }
    dirty = true;
}
void Session::removeLane (const Target& t)
{
    auto v = std::make_shared<std::vector<LanePtr>>();
    for (auto& l : *lanes) if (l->target != t) v->push_back (l);
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = v; }
    dirty = true;
}
void Session::recordPoint (const Target& t, float v)
{
    const double now = playPpq.load();
    if (now < 0) return;
    double from = now - 1.0 / 64.0;
    {
        const juce::ScopedLock sl (gestureLock);
        for (auto& g : gestures) if (g == t) { from = std::max (from, lastRecorded); }
        lastRecorded = now;
    }
    editLane (t, [=] (AutoLane& l)
    {
        l.pts.erase (std::remove_if (l.pts.begin(), l.pts.end(), [=] (const AutoPoint& p) { return p.beat > from - 1.0e-6 && p.beat <= now + 1.0e-6; }), l.pts.end());
        l.pts.push_back ({ now, v, 0.0f });
    });
}
void Session::beginGesture (const Target& t)
{
    const juce::ScopedLock sl (gestureLock);
    gestures.push_back (t);
    lastRecorded = playPpq.load();
    if (auto* d = resolve (t)) d->setGesture (t.param, true);
}
void Session::endGesture (const Target& t)
{
    const juce::ScopedLock sl (gestureLock);
    gestures.erase (std::remove (gestures.begin(), gestures.end(), t), gestures.end());
    if (auto* d = resolve (t)) d->setGesture (t.param, false);
}
bool Session::inGesture (const Target& t) const
{
    const juce::ScopedLock sl (gestureLock);
    for (auto& g : gestures) if (g == t) return true;
    return false;
}

// ---- transport ------------------------------------------------------------------------------------
void Session::play() { running.store (true); }
void Session::stop() { running.store (false); panicFlag.store (true); }
void Session::togglePlay() { if (running.load()) stop(); else play(); }
void Session::locate (double bar) { internalPpq.store (std::max (0.0, bar) * 4.0); relocate.store (true); }
void Session::launch (Track& t, int p)
{
    const double now = playPpq.load();
    if (now < 0 || ! running.load())
    {
        t.pattern.store (p); t.pendingPattern.store (p); t.pendingAt.store (0.0);
        return;
    }
    const int q = launchQuant.load();
    const double at = q <= 0 ? now : std::ceil ((now + 1.0e-6) / (4.0 * q)) * 4.0 * q;
    t.pendingPattern.store (p); t.pendingAt.store (at); t.pattern.store (p);
}
void Session::stopTrack (Track& t)
{
    const double now = playPpq.load();
    const int q = launchQuant.load();
    const double at = (now < 0 || q <= 0) ? std::max (0.0, now) : std::ceil ((now + 1.0e-6) / (4.0 * q)) * 4.0 * q;
    t.pendingPattern.store (-2); t.pendingAt.store (at);
}
void Session::launchScene (int s)
{
    if (s < 0 || s >= (int) scenes->size()) return;
    const auto& sc = (*scenes)[(size_t) s];
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const int p = sc.pattern[(size_t) i];
        if (p == -2) continue;
        if (p == -1) stopTrack (*tracks[(size_t) i]); else launch (*tracks[(size_t) i], p);
    }
    currentScene.store (s);
}
double Session::songBars() const { double b = 0; for (auto& e : *song) b += e.bars; return b; }
double Session::arrangementBars() const
{
    double b = 0;
    for (auto& t : tracks) for (auto& c : *t->clips) b = std::max (b, c.startBar + c.bars);
    return b;
}

// ---- targets --------------------------------------------------------------------------------------
Device* Session::resolve (const Target& t)
{
    if (! t.valid()) return nullptr;
    if (t.track == 0 && t.device == 0) return global.get();
    auto* tr = trackByUid (t.track);
    return tr ? tr->device (t.device) : nullptr;
}
juce::String Session::targetName (const Target& t) const
{
    auto* self = const_cast<Session*> (this);
    auto* d = self->resolve (t);
    if (! d) return "-";
    juce::String n;
    if (t.track == 0 && t.device == 0) n = "global";
    else if (auto* tr = self->trackByUid (t.track)) n = tr->name + (t.device == 0 ? juce::String() : " / " + d->name);
    return n + " : " + d->paramName (t.param);
}
bool Session::midiLearnPoll()
{
    if (! learning.load()) return false;
    const int c = lastCc.load();
    if (c < 0) return false;
    learning.store (false);
    const Target t = learnTarget;
    editMidiMaps ([c, t] (std::vector<MidiMap>& m)
    {
        m.erase (std::remove_if (m.begin(), m.end(), [t] (const MidiMap& x) { return x.target == t; }), m.end());
        m.push_back ({ c, t, 0.0f, 1.0f });
    });
    return true;
}
} // namespace fal::daw

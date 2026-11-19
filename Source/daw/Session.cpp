#include "Session.h"
#include "../dsp/Fx.h"
#include "devices/StudioDevices.h"

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

std::vector<int> detectOnsets (const juce::AudioBuffer<float>& b, double rate)
{
    std::vector<int> on;
    const int n = b.getNumSamples(), hop = std::max (64, (int) (rate * 0.005));
    if (n < hop * 4) return on;
    std::vector<float> env;
    for (int i = 0; i + hop <= n; i += hop)
    {
        float e = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) { const float* d = b.getReadPointer (c) + i; for (int k = 0; k < hop; ++k) e += d[k] * d[k]; }
        env.push_back (std::sqrt (e / (float) (hop * b.getNumChannels())));
    }
    float peak = 0; for (float e : env) peak = std::max (peak, e);
    const float thr = peak * 0.08f;
    int last = -1000000; const int minGap = (int) (rate * 0.05);
    for (size_t i = 2; i < env.size(); ++i)
    {
        const float rise = env[i] - std::max (env[i - 1], env[i - 2]);
        if (env[i] > thr && rise > env[i] * 0.35f && (int) i * hop - last > minGap) { on.push_back ((int) i * hop); last = (int) i * hop; }
    }
    return on;
}

// ---- Track ----------------------------------------------------------------------------------------
Track::Track()
{
    mixer = Device::makeMixer();
    clips = std::make_shared<const std::vector<Clip>>();
    chain = std::make_shared<const std::vector<int>>();
    mods = std::make_shared<const std::vector<Modulator>>();
    routes = std::make_shared<const std::vector<Route>>();
    aclips = std::make_shared<const std::vector<AudioClip>>();
    for (auto& p : sendPre) p.store (false);
    for (auto& m : meter) m.store (0.0f);
}
Device* Track::device (int devUid)
{
    if (devUid == 0) return mixer.get();
    if (instrument && instrument->uid == devUid) return instrument.get();
    for (auto& d : inserts) if (d->uid == devUid) return d.get();
    return nullptr;
}
/** the width of the signal after a device: a device replaces the channels it reads with the ones it writes; channels
    beyond both pass through untouched (a stereo effect on a 16-channel track leaves 3 … 16 alone) */
static int widthAfter (int cur, const Device& d)
{
    return cur > std::max (d.inWidth, d.outWidth) ? cur : std::max (1, d.outWidth);
}
int Track::outWidth() const
{
    int w = sourceWidth;
    for (auto& d : inserts) if (! d->bypass.load()) w = widthAfter (w, *d);
    return std::min (w, width.load());
}
int Track::inWidth() const
{
    for (auto& d : inserts) if (! d->bypass.load()) return std::min (d->inWidth, width.load());
    return std::min (sourceWidth, width.load());
}

// ---- Session --------------------------------------------------------------------------------------
Session::Session()
{
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    audioFormats.registerBasicFormats();
    writerThread = std::make_unique<juce::TimeSliceThread> ("sndstudio recorder");
    writerThread->startThread();
    global = Device::makeGlobal();
    for (int i = 0; i < kMacros; ++i) macroNames[(size_t) i] = "macro " + juce::String (i + 1);
    master = std::make_unique<Track>(); master->uid = 0; master->name = "master"; master->colour = 0xfff6f6f0;
    for (int r = 0; r < 2; ++r) addReturn();
    std::vector<Scene> sc ((size_t) kScenes);
    for (int i = 0; i < kScenes; ++i) sc[(size_t) i].name = juce::String (i + 1);
    scenes = std::make_shared<const std::vector<Scene>> (std::move (sc));
    song = std::make_shared<const std::vector<SongEntry>>();
    markers = std::make_shared<const std::vector<Marker>>();
    lanes = std::make_shared<const std::vector<LanePtr>>();
    midiMaps = std::make_shared<const std::vector<MidiMap>>();
    keyMaps = std::make_shared<const std::vector<KeyMap>>();
    for (auto& c : cc) c.store (0.0f);
    loadKnown();
    rebuildLatency();
}
Session::~Session()
{
    stopRecording();
    recWriters.clear(); oldWriters.clear();
    writerThread->stopThread (2000);
    const juce::SpinLock::ScopedLockType sl (lock);
    tracks.clear(); returns.clear();
}

juce::File Session::dataDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (kDeep ? "faltung deep sndstudio" : "faltung sndstudio");
    d.createDirectory();
    return d;
}
juce::File Session::projectDir() const
{
    if (projectFile != juce::File()) return projectFile.getParentDirectory();
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (kDeep ? "faltung deep sndstudio" : "faltung sndstudio");
    d.createDirectory();
    return d;
}
juce::File Session::audioDir() const
{
    auto d = projectDir().getChildFile (projectName + " audio");
    d.createDirectory();
    return d;
}
juce::File Session::knownFile() const { return knownOverride != juce::File() ? knownOverride : dataDir().getChildFile ("plugins.xml"); }
void Session::loadKnown() { if (auto x = juce::XmlDocument::parse (knownFile())) known.recreateFromXml (*x); }
void Session::saveKnown() { if (auto x = known.createXml()) x->writeTo (knownFile()); }

namespace
{
/** opens each plug-in file in a child process: a crash or a hang there is a failed file, not a crashed studio */
struct ChildScanner : juce::KnownPluginList::CustomScanner
{
    explicit ChildScanner (int ms) : timeoutMs (ms) {}
    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result, const juce::String& fileOrId) override
    {
        const auto out = juce::File::createTempFile ("scan.xml"); out.deleteFile();
        juce::ChildProcess cp;
        if (! cp.start (juce::StringArray { Session::scannerExecutable().getFullPathName(), "--scan-plugin", format.getName(), fileOrId, out.getFullPathName() }, 0))
            return false;
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (cp.isRunning())
        {
            if (shouldExit() || juce::Time::getMillisecondCounter() > until) { cp.kill(); out.deleteFile(); return false; }
            juce::Thread::sleep (20);
        }
        // (a child killed by a signal can report 0: only a written list counts as a scan that worked)
        const bool ok = cp.getExitCode() == 0 && out.existsAsFile() && out.getSize() > 0;
        if (ok)
            if (auto x = juce::XmlDocument::parse (out))
                for (auto* e : x->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e)) result.add (d.release());
                }
        out.deleteFile();
        return ok;
    }
    int timeoutMs;
};
} // namespace

int Session::scanOneFile (const juce::String& formatName, const juce::String& fileOrId, const juce::File& out)
{
    // (tests: a scan that crashes on purpose, to check that the studio survives it)
    const auto crashOn = juce::SystemStats::getEnvironmentVariable ("SNDSTUDIO_SCAN_CRASH_ON", {});
    if (crashOn.isNotEmpty() && fileOrId.contains (crashOn)) std::abort();
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    for (auto* f : fm.getFormats())
    {
        if (f->getName() != formatName) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        f->findAllTypesForFile (found, fileOrId);
        juce::XmlElement x ("scan");
        for (auto* d : found) x.addChildElement (d->createXml().release());
        return x.writeTo (out) && ! found.isEmpty() ? 0 : 2;
    }
    return 3;
}

int Session::scanPlugins (const juce::FileSearchPath* only, const std::function<bool (const juce::String&)>& progress, int timeoutMs)
{
    if (scannerExecutable().existsAsFile()) known.setCustomScanner (std::make_unique<ChildScanner> (timeoutMs));
    const int failedBefore = known.getBlacklistedFiles().size();
    for (auto* f : formats.getFormats())
    {
        const juce::FileSearchPath paths = only ? *only : f->getDefaultLocationsToSearch();
        if (only && f->getName() != "VST3") continue;   // (a folder of files: AU components are found by the system, not in folders)
        juce::PluginDirectoryScanner sc (known, *f, paths, true, dataDir().getChildFile ("scan-deadmans.txt"), false);
        juce::String name;
        for (;;)
        {
            if (progress && ! progress (f->getName() + ": " + sc.getNextPluginFileThatWillBeScanned())) break;
            if (! sc.scanNextFile (true, name)) break;
        }
    }
    saveKnown();
    return known.getBlacklistedFiles().size() - failedBefore;
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
    auto taken = [&] (const juce::String& s) { for (auto& t : tracks) if (t->name == s) return true; for (auto& t : returns) if (t->name == s) return true; return false; };
    while (taken (n)) n = base + " " + juce::String (k++);
    return n;
}

Track* Session::addTrack (const juce::String& name, int at, TrackKind kind)
{
    auto t = std::make_unique<Track>();
    t->uid = nextUid++;
    t->kind = kind;
    t->name = uniqueTrackName (name.isEmpty() ? (kind == TrackKind::Audio ? "audio" : "track") : name);
    t->colour = trackColour ((int) tracks.size());
    if (prepared) prepareTrack (*t); else sizeBuffers (*t);
    Track* raw = t.get();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (at < 0 || at > (int) tracks.size()) { at = (int) tracks.size(); tracks.push_back (std::move (t)); }
        else tracks.insert (tracks.begin() + at, std::move (t));
    }
    editScenes ([at] (std::vector<Scene>& s) { for (auto& sc : s) { if ((int) sc.pattern.size() < at) sc.pattern.resize ((size_t) at, -2); sc.pattern.insert (sc.pattern.begin() + at, -2); } });
    rebuildLatency();
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
        // routes into it, from every other track
        for (auto& t : tracks)
            if (std::any_of (t->routes->begin(), t->routes->end(), [uid] (const Route& r) { return r.dst == uid; }))
            {
                auto v = std::make_shared<std::vector<Route>>();
                std::vector<std::unique_ptr<RouteRun>> runs;
                for (size_t i = 0; i < t->routes->size(); ++i) if ((*t->routes)[i].dst != uid) { v->push_back ((*t->routes)[i]); runs.push_back (std::move (t->routeRun[i])); }
                t->routes = v; t->routeRun = std::move (runs);
            }
    }
    editScenes ([index] (std::vector<Scene>& s) { for (auto& sc : s) if (index < (int) sc.pattern.size()) sc.pattern.erase (sc.pattern.begin() + index); });
    auto nl = std::make_shared<std::vector<LanePtr>>();
    for (auto& l : *lanes) if (l->target.track != uid) nl->push_back (l);
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = nl; }
    editMidiMaps ([uid] (std::vector<MidiMap>& m) { m.erase (std::remove_if (m.begin(), m.end(), [uid] (const MidiMap& x) { return x.target.track == uid; }), m.end()); });
    editKeyMaps ([uid] (std::vector<KeyMap>& m) { m.erase (std::remove_if (m.begin(), m.end(), [uid] (const KeyMap& x) { return x.target.track == uid; }), m.end()); });
    for (auto& r : *gone->routes) if (auto* d = trackByUid (r.dst)) negotiate (*d);   // they lost an input
    if (selectedTrack.load() >= (int) tracks.size()) selectedTrack.store (std::max (0, (int) tracks.size() - 1));
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
    editScenes ([from, to] (std::vector<Scene>& s) { for (auto& sc : s) if (from < (int) sc.pattern.size() && to < (int) sc.pattern.size()) { const int v = sc.pattern[(size_t) from]; sc.pattern.erase (sc.pattern.begin() + from); sc.pattern.insert (sc.pattern.begin() + to, v); } });
    rebuildLatency();
    dirty = true;
}

Track* Session::addReturn (const juce::String& name)
{
    if ((int) returns.size() >= kReturns) return nullptr;
    const int r = (int) returns.size();
    auto t = std::make_unique<Track>(); t->uid = -1 - r; t->name = name.isEmpty() ? "return " + juce::String (r + 1) : name; t->colour = trackColour (r + 4);
    if (prepared) prepareTrack (*t); else sizeBuffers (*t);
    Track* raw = t.get();
    { const juce::SpinLock::ScopedLockType sl (lock); returns.push_back (std::move (t)); }
    rebuildLatency();
    dirty = true;
    return raw;
}
void Session::removeReturn (int index)
{
    if (index < 0 || index >= (int) returns.size()) return;
    std::unique_ptr<Track> gone;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        gone = std::move (returns[(size_t) index]);
        returns.erase (returns.begin() + index);
        for (size_t r = 0; r < returns.size(); ++r) returns[r]->uid = -1 - (int) r;
    }
    rebuildLatency();
    dirty = true;
}

Track* Session::trackByUid (int uid)
{
    if (uid == 0) return master.get();
    if (uid < 0) return (-uid - 1) < (int) returns.size() ? returns[(size_t) (-uid - 1)].get() : nullptr;
    for (auto& t : tracks) if (t->uid == uid) return t.get();
    return nullptr;
}
Track* Session::trackAt (int sel)
{
    if (sel == -100) return master.get();
    if (sel < 0) return (-sel - 1) < (int) returns.size() ? returns[(size_t) (-sel - 1)].get() : nullptr;
    return sel < (int) tracks.size() ? tracks[(size_t) sel].get() : nullptr;
}
int Session::indexOf (const Track* t) const
{
    for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].get() == t) return i;
    if (t == master.get()) return -100;
    for (int r = 0; r < (int) returns.size(); ++r) if (returns[(size_t) r].get() == t) return -1 - r;
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
    // the two machines as well: the whole kit, or the loop slicer, on one track (their own sequencers follow the studio's clock,
    // and the track's steps play their voices and slices by note)
    for (int i = 0; i < kNumProducts; ++i) if (info (i).kind == fal::Kind::Machine) a.add ("fal:" + juce::String (info (i).code));
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
    if (typeId.startsWith ("st:")) return studioDeviceName (typeId);
    return typeId.fromFirstOccurrenceOf (":", false, false);
}
std::vector<Session::TypeEntry> Session::deviceTypes() const
{
    std::vector<TypeEntry> v;
    for (auto& t : internalInstrumentTypes()) v.push_back ({ t, typeDisplayName (t), kDeep ? "faltung deep" : "faltung", true });
    for (auto& t : internalEffectTypes()) v.push_back ({ t, typeDisplayName (t), kDeep ? "faltung deep" : "faltung", false });
    // the suite's own plug-ins that live next to it (hosted, but offered without a scan)
    for (auto& f : suitePlugins())
    {
        const auto n = f.getFileNameWithoutExtension();
        v.push_back ({ "suite:" + f.getFullPathName(), n, kDeep ? "faltung deep" : "faltung", n == "Partials" || n == "ObliqSound" || n == "ObliqSonic" || n == "openfield" || n == "openprocess" });
    }
    for (auto& t : studioDeviceTypes()) v.push_back ({ t, studioDeviceName (t), "studio", studioDeviceIsInstrument (t) });
    for (auto& d : known.getTypes()) v.push_back ({ d.pluginFormatName.toLowerCase() + ":" + d.createIdentifierString(), d.name + " (" + d.manufacturerName + ", " + d.pluginFormatName + ")", d.pluginFormatName, d.isInstrument });
    return v;
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
    if (typeId.startsWith ("st:"))
    {
        auto d = Device::makeStudio (nextUid, typeId);
        if (d) ++nextUid; else if (error) *error = "unknown device " + typeId;
        return d;
    }
    const auto id = typeId.fromFirstOccurrenceOf (":", false, false);
    if (typeId.startsWith ("suite:"))
    {
        // one of the suite's plug-ins by its file: found on the spot, remembered in the known list
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* fmt : formats.getFormats()) if (fmt->getName() == "VST3" && fmt->fileMightContainThisPluginType (id)) fmt->findAllTypesForFile (found, id);
        if (found.isEmpty()) { if (error) *error = "cannot open " + juce::File (id).getFileName(); return nullptr; }
        known.addType (*found[0]);
        return createHosted (*found[0], error);
    }
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
    negotiate (t);
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
    negotiate (t);
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
        editMods (t, [devUid] (std::vector<Modulator>& m) { m.erase (std::remove_if (m.begin(), m.end(), [devUid] (const Modulator& x) { return x.device == devUid; }), m.end()); });
        negotiate (t);
        rebuildLatency();
        dirty = true;
    }
    return gone;
}

void Session::moveInsert (Track& t, int from, int to)
{
    if (from == to || from < 0 || from >= (int) t.inserts.size() || to < 0 || to >= (int) t.inserts.size()) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto d = std::move (t.inserts[(size_t) from]);
        t.inserts.erase (t.inserts.begin() + from);
        t.inserts.insert (t.inserts.begin() + to, std::move (d));
    }
    negotiate (t);
    rebuildLatency();
    dirty = true;
}

void Session::sizeBuffers (Track& t)
{
    const int w = juce::jlimit (1, kMaxWidth, t.width.load());
    const int iw = std::max (2, w);
    t.buf.setSize (w, std::max (16, bs), false, false, true); t.post.setSize (w, std::max (16, bs), false, false, true); t.inst.setSize (iw, std::max (16, bs), false, false, true);
    t.buf.clear(); t.post.clear(); t.inst.clear();
    if ((int) t.align.size() != w) { t.align.resize ((size_t) w); for (auto& d : t.align) d.prepare (std::max (1024, t.alignDelay + bs + 16)); }
    if ((int) t.instDelay.size() != iw) { t.instDelay.resize ((size_t) iw); for (auto& d : t.instDelay) d.prepare (std::max (1024, t.inDelay + bs + 16)); }
}

void Session::setWidth (Track& t, int w)
{
    w = juce::jlimit (1, kMaxWidth, w);
    if (t.width.load() == w) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        t.width.store (w);
        sizeBuffers (t);
    }
    negotiate (t);
    rebuildLatency();
    dirty = true;
}

void Session::negotiate (Track& t)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int w = t.width.load();
    // what feeds the chain: the instrument or the clips (stereo, or as wide as the widest file), the device inputs,
    // and whatever other tracks route in (an encoder's 16 channels arriving on a decoder track)
    int cur = t.instrument ? std::max (2, t.instrument->configure (0, w)) : 2;
    if (t.isAudio())
    {
        for (auto& c : *t.aclips) if (c.sample) cur = std::max (cur, c.sample->data.getNumChannels());
        if (t.inputSource.load() == -2) cur = std::max (cur, t.inputCount.load());
    }
    for (auto& s : tracks) for (auto& r : *s->routes) if (r.dst == t.uid) cur = std::max (cur, r.dstOffset + r.channels);
    if (t.isReturn()) for (auto& s : tracks) cur = std::max (cur, s->width.load());   // sends carry the track's channels
    if (t.isMaster()) cur = w;
    cur = juce::jlimit (1, w, cur);
    t.sourceWidth = cur;
    for (auto& d : t.inserts) { d->configure (cur, w); cur = juce::jlimit (1, w, widthAfter (cur, *d)); }
}

bool Session::wouldCycle (const Track& src, int dstUid) const
{
    // following routes from dst, do we reach src?
    std::vector<int> stack { dstUid }; std::vector<int> seen;
    auto* self = const_cast<Session*> (this);
    while (! stack.empty())
    {
        const int u = stack.back(); stack.pop_back();
        if (u == src.uid) return true;
        if (std::find (seen.begin(), seen.end(), u) != seen.end()) continue;
        seen.push_back (u);
        if (auto* t = self->trackByUid (u)) for (auto& r : *t->routes) if (! r.feedback) stack.push_back (r.dst);
    }
    return false;
}

bool Session::addRoute (Track& src, Route r)
{
    if (r.dst == src.uid || r.dst == 0 || src.isReturn() || src.isMaster()) return false;
    auto* dst = trackByUid (r.dst);
    if (! dst) return false;
    if (wouldCycle (src, r.dst)) r.feedback = true;   // closes a loop: allowed, one buffer late and limited
    r.channels = juce::jlimit (1, kMaxWidth, r.channels);
    r.srcOffset = juce::jlimit (0, kMaxWidth - 1, r.srcOffset); r.dstOffset = juce::jlimit (0, kMaxWidth - 1, r.dstOffset);
    auto run = std::make_unique<RouteRun>();
    run->delay.resize ((size_t) r.channels); for (auto& d : run->delay) d.prepare (std::max (1024, 2 * bs + 16));
    auto v = std::make_shared<std::vector<Route>> (*src.routes); v->push_back (r);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        src.routes = v; src.routeRun.push_back (std::move (run));
    }
    negotiate (*dst);    // the destination's first device now reads this many channels
    rebuildLatency();
    dirty = true;
    return true;
}
void Session::removeRoute (Track& src, int index)
{
    if (index < 0 || index >= (int) src.routes->size()) return;
    auto v = std::make_shared<std::vector<Route>> (*src.routes); v->erase (v->begin() + index);
    std::unique_ptr<RouteRun> gone;
    const int dstUid = (*src.routes)[(size_t) index].dst;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        src.routes = v; gone = std::move (src.routeRun[(size_t) index]); src.routeRun.erase (src.routeRun.begin() + index);
    }
    if (auto* dst = trackByUid (dstUid)) negotiate (*dst);
    rebuildLatency();
    dirty = true;
}
void Session::editRoutes (Track& src, const std::function<void (std::vector<Route>&)>& fn)
{
    std::vector<Route> v = *src.routes; fn (v);
    // rebuild: remove all, add back (keeps the cycle check and the delay lines right)
    while (! src.routes->empty()) removeRoute (src, 0);
    for (auto& r : v) addRoute (src, r);
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
    // order: tracks sorted by depth along the routes (a destination after all its sources); returns after tracks
    for (auto& t : tracks) { t->latency = chain (*t); t->depth = -1; }
    for (auto& t : returns) { t->latency = chain (*t); t->depth = -1; }
    std::function<int (Track&)> depthOf = [&] (Track& t) -> int
    {
        if (t.depth >= 0) return t.depth;
        t.depth = 0;   // guards cycles
        int d = 0;
        for (auto& s : tracks) for (auto& r : *s->routes) if (r.dst == t.uid && ! r.feedback) d = std::max (d, depthOf (*s) + 1);
        t.depth = d;
        return d;
    };
    order.clear();
    for (auto& t : tracks) depthOf (*t);
    for (auto& t : tracks) order.push_back (t.get());
    std::stable_sort (order.begin(), order.end(), [] (Track* a, Track* b) { return a->depth < b->depth; });
    // arrival times: a track's inputs (routes) arrive when the latest source is done
    std::function<int (Track&)> arrivalOf = [&] (Track& t) -> int
    {
        int a = 0;
        for (auto& s : tracks) for (auto& r : *s->routes) if (r.dst == t.uid && ! r.feedback) a = std::max (a, s->arrival + s->latency);
        return a;
    };
    for (auto* t : order) t->arrival = arrivalOf (*t);
    latTracks = 0;
    for (auto* t : order) latTracks = std::max (latTracks, t->arrival + t->latency);
    for (auto* t : order)
    {
        t->inDelay = t->arrival;
        t->alignDelay = latTracks - (t->arrival + t->latency);
        for (size_t i = 0; i < t->routes->size() && i < t->routeRun.size(); ++i)
        {
            auto* dst = trackByUid ((*t->routes)[i].dst);
            const int d = dst ? dst->arrival - (t->arrival + t->latency) : 0;
            auto& run = *t->routeRun[i];
            run.samples = (*t->routes)[i].feedback ? 0 : std::max (0, d);
            const int need = (*t->routes)[i].feedback ? 2 * bs + 16 : run.samples + bs + 16;
            for (auto& dl : run.delay) if (dl.mask + 1 < need) dl.prepare (need);
        }
    }
    latReturns = 0;
    for (auto& r : returns) { r->arrival = latTracks; r->inDelay = 0; latReturns = std::max (latReturns, r->latency); order.push_back (r.get()); }
    for (auto& r : returns) r->alignDelay = latReturns - r->latency;
    master->latency = chain (*master); latMaster = master->latency; master->alignDelay = 0; master->inDelay = 0;
    latTotal = latTracks + latReturns + latMaster;
    for (auto* t : order) t->directExtra = t->isReturn() ? latMaster : latReturns + latMaster;
    auto size = [&] (Track& t)
    {
        const int need = t.alignDelay + t.directExtra + bs + 16;
        if ((int) t.align.size() != t.width.load()) t.align.resize ((size_t) t.width.load());
        for (auto& d : t.align) if (d.mask + 1 < need) d.prepare (need);
        if ((int) t.instDelay.size() != std::max (2, t.width.load())) t.instDelay.resize ((size_t) std::max (2, t.width.load()));
        for (auto& d : t.instDelay) if (d.mask + 1 < t.inDelay + bs + 16) d.prepare (t.inDelay + bs + 16);
        if (t.directExtra > 0 && t.outChannel.load() > 0)
        {
            if ((int) t.direct.size() != t.width.load()) t.direct.resize ((size_t) t.width.load());
            for (auto& d : t.direct) if (d.mask + 1 < t.directExtra + bs + 16) d.prepare (t.directExtra + bs + 16);
        }
    };
    for (auto* t : order) size (*t);
    size (*master);
}

void Session::assignOutputs()
{
    const int mw = master->width.load();
    int c = 1 + mw;
    for (auto& t : tracks) { t->outChannel.store (c <= kOutChannels ? c : 0); t->toMaster.store (false); c += t->width.load(); }
    for (auto& r : returns) { r->outChannel.store (c <= kOutChannels ? c : 0); r->toMaster.store (false); c += r->width.load(); }
    masterOut.store (1);
    multiOut.store (true);
    dirty = true;
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
void Session::editMods (Track& t, const std::function<void (std::vector<Modulator>&)>& fn)
{
    std::vector<Modulator> m = *t.mods; fn (m);
    for (auto& x : m) if (x.id <= 0) x.id = nextModId++;
    auto v = std::make_shared<const std::vector<Modulator>> (m);
    std::vector<ModState> st (m.size());
    // keep the running state of modulators that survive
    for (size_t i = 0; i < m.size(); ++i)
        for (size_t k = 0; k < t.mods->size() && k < t.modState.size(); ++k)
            if ((*t.mods)[k].id == m[i].id) { st[i] = t.modState[k]; break; }
    { const juce::SpinLock::ScopedLockType sl (lock); t.mods = v; t.modState = std::move (st); }
    dirty = true;
}
Modulator* Session::findMod (Track& t, int id) const
{
    for (auto& m : *t.mods) if (m.id == id) return const_cast<Modulator*> (&m);
    return nullptr;
}
int Session::addModulator (Track& t, Modulator m)
{
    m.id = nextModId++;
    const int id = m.id;
    editMods (t, [&] (std::vector<Modulator>& v) { v.push_back (m); });
    return id;
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
void Session::editKeyMaps (const std::function<void (std::vector<KeyMap>&)>& fn)
{
    auto v = std::make_shared<std::vector<KeyMap>> (*keyMaps); fn (*v);
    { const juce::SpinLock::ScopedLockType sl (lock); keyMaps = v; }
    dirty = true;
}
bool Session::keyEvent (int key, bool down)
{
    if (const int learn = keyLearning.load(); learn != 0 && down)
    {
        keyLearning.store (0);
        const Target t = keyLearnTarget; const bool hold = learn == 2;
        editKeyMaps ([=] (std::vector<KeyMap>& m)
        {
            m.erase (std::remove_if (m.begin(), m.end(), [=] (const KeyMap& x) { return x.key == key || x.target == t; }), m.end());
            m.push_back ({ key, t, hold });
        });
        return true;
    }
    bool used = false;
    for (auto& m : *keyMaps)
    {
        if (m.key != key) continue;
        auto* d = resolve (m.target);
        if (! d) continue;
        used = true;
        if (! m.momentary && ! down) continue;
        const float v = m.momentary ? (down ? 1.0f : 0.0f) : (d->baseValue (m.target.param) > 0.5f ? 0.0f : 1.0f);
        d->setBase (m.target.param, v);
        if (autoWrite.load() && playPpq.load() >= 0) recordPoint (m.target, v);   // a played mute or throw is written like any other move
    }
    return used;
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

// ---- samples and audio clips ------------------------------------------------------------------------
SamplePtr Session::loadSample (const juce::File& f, juce::String* error)
{
    const auto key = f.getFullPathName();
    if (auto it = sampleCache.find (key); it != sampleCache.end()) if (auto s = it->second.lock()) return s;
    std::unique_ptr<juce::AudioFormatReader> rd (audioFormats.createReaderFor (f));
    if (! rd) { if (error) *error = "cannot read " + f.getFileName(); return nullptr; }
    if (rd->lengthInSamples > (juce::int64) rd->sampleRate * 60 * 30) { if (error) *error = f.getFileName() + " is longer than 30 minutes"; return nullptr; }
    auto s = std::make_shared<Sample>();
    s->path = key; s->rate = rd->sampleRate;
    const int ch = std::min (64, (int) rd->numChannels);
    s->data.setSize (ch, (int) rd->lengthInSamples);
    rd->read (&s->data, 0, (int) rd->lengthInSamples, 0, true, ch > 1);
    s->onsets = detectOnsets (s->data, s->rate);
    sampleCache[key] = s;
    return s;
}
AudioClip* Session::addAudioClip (Track& t, const juce::File& f, double startBar, juce::String* error)
{
    auto s = loadSample (f, error);
    if (! s) return nullptr;
    AudioClip c;
    c.id = nextClipId++; c.sample = s; c.path = f.getFullPathName(); c.startBar = std::max (0.0, startBar);
    c.bars = std::max (0.01, s->seconds() / (60.0 / tempo() * 4.0));
    if (t.kind != TrackKind::Audio) { const juce::SpinLock::ScopedLockType sl (lock); t.kind = TrackKind::Audio; }
    const int id = c.id;
    editAudioClips (t, [&] (std::vector<AudioClip>& v) { v.push_back (c); });
    for (auto& x : *t.aclips) if (x.id == id) return const_cast<AudioClip*> (&x);
    return nullptr;
}
void Session::editAudioClips (Track& t, const std::function<void (std::vector<AudioClip>&)>& fn)
{
    std::vector<AudioClip> v = *t.aclips; fn (v);
    for (auto& c : v) if (c.id <= 0) c.id = nextClipId++;
    std::sort (v.begin(), v.end(), [] (const AudioClip& a, const AudioClip& b) { return a.startBar < b.startBar; });
    auto np = std::make_shared<const std::vector<AudioClip>> (v);
    std::vector<std::unique_ptr<ClipState>> st;
    for (auto& c : v)
    {
        std::unique_ptr<ClipState> keep;
        for (auto& old : t.clipState) if (old && old->id == c.id) { keep = std::move (old); break; }
        if (! keep) { keep = std::make_unique<ClipState>(); keep->id = c.id; }
        const size_t grains = (size_t) (c.bars * 4.0 * 60.0 / tempo() * sr / kHop) + 64;
        if (keep->off.size() != grains) keep->off.assign (grains, kUnknown); else std::fill (keep->off.begin(), keep->off.end(), kUnknown);
        st.push_back (std::move (keep));
    }
    { const juce::SpinLock::ScopedLockType sl (lock); t.aclips = np; t.clipState = std::move (st); }
    int cw = 2; for (auto& c : v) if (c.sample) cw = std::max (cw, c.sample->data.getNumChannels());
    if (std::min (cw, t.width.load()) != t.sourceWidth) negotiate (t);
    dirty = true;
}
void Session::splitClip (Track& t, int clipId, double atBar)
{
    editAudioClips (t, [&] (std::vector<AudioClip>& v)
    {
        for (size_t i = 0; i < v.size(); ++i)
        {
            auto& c = v[i];
            if (c.id != clipId || atBar <= c.startBar + 1.0e-6 || atBar >= c.startBar + c.bars - 1.0e-6) continue;
            AudioClip b = c;
            const double frac = (atBar - c.startBar) / c.bars;
            const double secs = c.bars * 4.0 * 60.0 / tempo();
            double srcSecs = secs;   // seconds of source consumed by the whole clip
            if (c.mode == 0) srcSecs = secs * std::exp2 (c.pitch / 12.0);
            else if (c.mode == 1) srcSecs = secs / std::max (0.05, c.stretch);
            else srcSecs = c.sourceSeconds();
            b.id = 0; b.startBar = atBar; b.bars = c.bars - (atBar - c.startBar); b.offset = c.offset + srcSecs * frac; b.fadeIn = 0;
            c.bars = atBar - c.startBar; c.fadeOut = 0;
            v.push_back (b);
            break;
        }
    });
}
juce::File Session::collectSample (const juce::File& f)
{
    auto dst = audioDir().getChildFile (f.getFileName());
    if (dst == f || (dst.existsAsFile() && dst.getSize() == f.getSize())) return dst;
    int k = 2;
    while (dst.existsAsFile()) dst = audioDir().getChildFile (f.getFileNameWithoutExtension() + " " + juce::String (k++) + f.getFileExtension());
    f.copyFileTo (dst);
    return dst;
}

// ---- recording ---------------------------------------------------------------------------------------
void Session::startRecording()
{
    if (recording.load()) return;
    juce::WavAudioFormat wav;
    for (auto& t : tracks)
    {
        if (! t->armed.load() || t->kind != TrackKind::Audio) continue;
        const int ch = t->inputSource.load() == -2 ? juce::jlimit (1, kMaxWidth, t->inputCount.load()) : (t->inputSource.load() == 0 ? master->width.load() : 2);
        juce::File f = audioDir().getChildFile (t->name + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav");
        int k = 2; while (f.existsAsFile()) f = audioDir().getChildFile (t->name + " " + juce::String (k++) + ".wav");
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) ch, 32, {}, 0));
        if (! w) continue;
        auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), *writerThread, 1 << 18);
        t->recFile = f; t->recChannels = ch; t->recWritten = 0; t->recDropped = 0; t->recClipId = nextClipId++;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            t->recWriter = tw.get(); t->recActive.store (false);
        }
        recWriters.push_back (std::move (tw));
    }
    recording.store (true);
}
void Session::stopRecording()
{
    if (! recording.load()) return;
    recording.store (false);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& t : tracks) { t->recActive.store (false); t->recWriter = nullptr; }
    }
    recWriters.clear();   // flushes and closes the files
    finishRecordings();
}
void Session::finishRecordings()
{
    for (auto& t : tracks)
    {
        if (t->recFile == juce::File() || t->recWritten <= 0) { t->recFile = juce::File(); continue; }
        juce::String err;
        auto s = loadSample (t->recFile, &err);
        if (s)
        {
            // the input arrived `latency` samples late: the clip starts earlier, or when that would be before bar 1, skips into the file
            AudioClip c; c.id = t->recClipId; c.sample = s; c.path = t->recFile.getFullPathName();
            const double startPpq = t->recStartPpq;
            if (startPpq >= 0) c.startBar = startPpq / 4.0;
            else { c.startBar = 0; c.offset = -startPpq * 60.0 / tempo(); }
            c.bars = ((double) t->recWritten / sr - c.offset) / (60.0 / tempo() * 4.0);
            editAudioClips (*t, [&] (std::vector<AudioClip>& v) { v.push_back (c); });
        }
        t->recFile = juce::File(); t->recWritten = 0;
    }
}

Track* Session::captureJam()
{
    if (recording.load()) return nullptr;
    auto* t = addTrack ("jam", -1, TrackKind::Audio);
    if (! t) return nullptr;
    if (master->width.load() != 2) setWidth (*t, master->width.load());
    for (auto& o : tracks) if (o.get() != t && o->isAudio()) o->armed.store (false);   // only the take is written
    t->inputSource.store (0); t->armed.store (true); t->monitor.store ((int) Monitor::Off); t->toMaster.store (false);
    startRecording();
    if (! running.load()) play();
    return t;
}

void Session::freezeEvolution (Track& t)
{
    const int pi = juce::jlimit (0, kPatterns - 1, t.activePattern >= 0 ? t.activePattern : t.pattern.load());
    std::array<juce::uint8, kMaxSteps> flip; std::array<float, kMaxSteps> drift;
    { const juce::SpinLock::ScopedLockType sl (lock); flip = t.flip; drift = t.drift; t.flip.fill (0); t.drift.fill (0.0f); }
    editPattern (t, pi, [&] (Pattern& p)
    {
        for (int i = 0; i < p.length; ++i)
        {
            auto& st = p.s[(size_t) i];
            if (flip[(size_t) i] != 0) { st.on = ! st.on; if (st.on) st.vel = 0.55f; }
            st.vel = juce::jlimit (0.1f, 1.0f, st.vel + drift[(size_t) i]);
        }
    });
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
        const int p = sc.patternFor (i);
        if (p == -2) continue;
        if (p == -1) stopTrack (*tracks[(size_t) i]); else launch (*tracks[(size_t) i], p);
    }
    currentScene.store (s);
}
double Session::songBars() const { double b = 0; for (auto& e : *song) b += e.bars; return b; }
double Session::arrangementBars() const
{
    double b = 0;
    for (auto& t : tracks) { for (auto& c : *t->clips) b = std::max (b, c.startBar + c.bars); for (auto& c : *t->aclips) b = std::max (b, c.startBar + c.bars); }
    return b;
}
double Session::projectBars() const
{
    const auto m = (PlayMode) mode.load();
    const double b = m == PlayMode::Song ? songBars() : m == PlayMode::Arrange ? arrangementBars() : loopEnd.load();
    return std::max (1.0, b);
}
double Session::stepBeatsOf (const Track& t, double bpm) const
{
    if (t.freeClock.load (std::memory_order_relaxed)) return std::max (1.0, (double) t.stepMs.load (std::memory_order_relaxed)) / 1000.0 * bpm / 60.0;
    return 0.25 / speedMul (t.speed.load (std::memory_order_relaxed));
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
void Session::recomputeIds()
{
    int u = 1, cl = 1, mo = 1;
    auto scan = [&] (Track& t)
    {
        u = std::max (u, t.uid + 1);
        t.forEachDevice ([&] (Device& d) { u = std::max (u, d.uid + 1); });
        for (auto& c : *t.aclips) cl = std::max (cl, c.id + 1);
        for (auto& m : *t.mods) mo = std::max (mo, m.id + 1);
    };
    for (auto& t : tracks) scan (*t);
    for (auto& r : returns) scan (*r);
    scan (*master);
    nextUid = u; nextClipId = cl; nextModId = mo;
}

// ---- the browser's audition ---------------------------------------------------------------------------
bool Session::preview (const juce::File& f, juce::String* error)
{
    auto smp = loadSample (f, error);
    if (! smp) return false;
    auto p = std::make_shared<Preview>(); p->sample = smp;
    { const juce::SpinLock::ScopedLockType sl (lock); previewOld = previewNow; previewNow = p; previewOn.store (true); }
    previewOld.reset();
    return true;
}
void Session::stopPreview()
{
    { const juce::SpinLock::ScopedLockType sl (lock); previewOld = previewNow; previewNow.reset(); previewOn.store (false); }
    previewOld.reset();
}

juce::Array<juce::File> Session::suitePlugins()
{
    // the plug-ins that ship with the suite but are not compiled into the studio; found in the usual VST3 folders
    // (and three of the same maker's that belong in a studio: the Leitner spatializer, openfield, Quell)
    static const char* names[] = { "Partials", "Cloudfin", "ConvoSolution", "ObliqSound", "ObliqSonic", "ballonwinkel", "ballonpaleis",
                                   "ballonwinkel deep", "ballonpaleis deep", "Leitner", "openfield", "openprocess", "Quell" };
    juce::Array<juce::File> out;
    juce::VST3PluginFormat fmt;
    const auto paths = fmt.getDefaultLocationsToSearch();
    for (auto* n : names)
        for (int i = 0; i < paths.getNumPaths(); ++i)
        {
            const auto f = paths[i].getChildFile (juce::String (n) + ".vst3");
            if (f.exists()) { out.add (f); break; }
        }
    return out;
}

void Session::renderRange (double& fromBar, double& toBar) const
{
    if (loopOn.load() && loopEnd.load() > loopStart.load()) { fromBar = loopStart.load(); toBar = loopEnd.load(); return; }
    fromBar = 0.0;
    const auto m = (PlayMode) mode.load();
    toBar = std::max (1.0, m == PlayMode::Song ? songBars() : m == PlayMode::Arrange ? arrangementBars() : loopEnd.load());
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

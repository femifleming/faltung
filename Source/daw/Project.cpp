// faltung sndstudio — project files (xml), new / clear, and the starter templates.
#include "Session.h"

namespace fal::daw
{
static const int kFormatVersion = 2;

static void saveStep (juce::XmlElement& e, int i, const Step& s)
{
    auto* x = e.createNewChildElement ("s");
    x->setAttribute ("i", i);
    if (! s.on) x->setAttribute ("off", 1);
    x->setAttribute ("v", s.vel);
    if (s.pitch != 0) x->setAttribute ("p", s.pitch);
    if (s.prob < 0.999f) x->setAttribute ("pr", s.prob);
    if (s.ratchet > 1) x->setAttribute ("r", s.ratchet);
    if (s.rcurve != 0.0f) x->setAttribute ("rc", s.rcurve);
    if (s.micro != 0.0f) x->setAttribute ("m", s.micro);
    if (std::abs (s.len - 0.5f) > 1.0e-4f) x->setAttribute ("l", s.len);
    if (std::abs (s.dur - 1.0f) > 1.0e-6f) x->setAttribute ("du", s.dur);
    if (s.cond != Cond::Always) x->setAttribute ("c", (int) s.cond);
    if (s.slide) x->setAttribute ("sl", 1);
    if (s.slice >= 0) x->setAttribute ("sc", s.slice);
    if (s.chord > 0) x->setAttribute ("ch", s.chord);
    for (auto& l : s.locks)
        if (l.param >= 0) { auto* k = x->createNewChildElement ("k"); k->setAttribute ("d", l.device); k->setAttribute ("p", l.param); k->setAttribute ("v", l.value); }
}
static Step loadStep (const juce::XmlElement& x)
{
    Step s;
    s.on = x.getIntAttribute ("off", 0) == 0;
    s.vel = (float) x.getDoubleAttribute ("v", 0.8);
    s.pitch = x.getIntAttribute ("p", 0);
    s.prob = (float) x.getDoubleAttribute ("pr", 1.0);
    s.ratchet = juce::jlimit (1, 8, x.getIntAttribute ("r", 1));
    s.rcurve = juce::jlimit (-1.0f, 1.0f, (float) x.getDoubleAttribute ("rc", 0.0));
    s.micro = (float) x.getDoubleAttribute ("m", 0.0);
    s.len = (float) x.getDoubleAttribute ("l", 0.5);
    s.dur = juce::jlimit (0.05f, 16.0f, (float) x.getDoubleAttribute ("du", 1.0));
    s.cond = (Cond) juce::jlimit (0, (int) Cond::Count - 1, x.getIntAttribute ("c", 0));
    s.slide = x.getIntAttribute ("sl", 0) != 0;
    s.slice = x.getIntAttribute ("sc", -1);
    s.chord = juce::jlimit (0, kChords - 1, x.getIntAttribute ("ch", 0));
    int n = 0;
    for (auto* k : x.getChildWithTagNameIterator ("k"))
        if (n < kLocks) s.locks[(size_t) n++] = { k->getIntAttribute ("d"), k->getIntAttribute ("p"), (float) k->getDoubleAttribute ("v") };
    return s;
}

static void saveTrack (juce::XmlElement& e, const Track& t, const juce::File& projectDir)
{
    e.setAttribute ("uid", t.uid); e.setAttribute ("name", t.name); e.setAttribute ("colour", juce::String::toHexString ((int) t.colour));
    e.setAttribute ("kind", (int) t.kind);
    e.setAttribute ("pattern", t.pattern.load()); e.setAttribute ("speed", t.speed.load()); e.setAttribute ("root", t.rootNote.load());
    e.setAttribute ("swing", t.swingOffset.load()); e.setAttribute ("shift", t.shiftMs.load()); e.setAttribute ("prob", t.probMul.load());
    e.setAttribute ("free", t.freeClock.load() ? 1 : 0); e.setAttribute ("stepMs", t.stepMs.load());
    if (t.evolve.load() > 0.0f) e.setAttribute ("evolve", t.evolve.load());
    if (t.chokeGroup.load() > 0) e.setAttribute ("choke", t.chokeGroup.load());
    if (std::abs (t.phaseRate.load() - 1.0f) > 1.0e-6f) e.setAttribute ("phase", t.phaseRate.load());
    if (t.scale) { auto* x = e.createNewChildElement ("scale"); x->setAttribute ("name", t.scale->name); x->setAttribute ("period", t.scale->period); x->setAttribute ("cents", t.scale->text()); }
    e.setAttribute ("solo", t.solo.load() ? 1 : 0); e.setAttribute ("armed", t.armed.load() ? 1 : 0); e.setAttribute ("chainOn", t.chainOn.load() ? 1 : 0);
    e.setAttribute ("width", t.width.load()); e.setAttribute ("toMaster", t.toMaster.load() ? 1 : 0); e.setAttribute ("out", t.outChannel.load()); e.setAttribute ("key", t.keyTrack.load());
    e.setAttribute ("inSrc", t.inputSource.load()); e.setAttribute ("inCh", t.inputChannel.load()); e.setAttribute ("inCount", t.inputCount.load()); e.setAttribute ("monitor", t.monitor.load());
    juce::String pre; for (auto& p : t.sendPre) pre << (p.load() ? "1" : "0"); e.setAttribute ("pre", pre);
    t.mixer->save (*e.createNewChildElement ("mixer"));
    if (t.instrument) t.instrument->save (*e.createNewChildElement ("instrument"));
    for (auto& d : t.inserts) d->save (*e.createNewChildElement ("insert"));
    for (int i = 0; i < kPatterns; ++i)
    {
        auto p = t.patterns[(size_t) i];
        if (! p || p->empty()) continue;
        auto* x = e.createNewChildElement ("pattern");
        x->setAttribute ("i", i); x->setAttribute ("length", p->length);
        for (int s = 0; s < kMaxSteps; ++s) if (p->s[(size_t) s].on || p->s[(size_t) s].micro != 0.0f || std::abs (p->s[(size_t) s].dur - 1.0f) > 1.0e-6f) saveStep (*x, s, p->s[(size_t) s]);
    }
    for (auto& c : *t.clips) { auto* x = e.createNewChildElement ("clip"); x->setAttribute ("id", c.id); x->setAttribute ("pattern", c.pattern); x->setAttribute ("start", c.startBar); x->setAttribute ("bars", c.bars); if (c.offset != 0) x->setAttribute ("offset", c.offset); if (c.muted) x->setAttribute ("muted", 1); if (c.name.isNotEmpty()) x->setAttribute ("name", c.name); if (c.colour) x->setAttribute ("colour", juce::String::toHexString ((int) c.colour)); }
    for (auto& c : *t.aclips)
    {
        auto* x = e.createNewChildElement ("aclip");
        juce::File f (c.path);
        x->setAttribute ("id", c.id);
        x->setAttribute ("path", f.isAChildOf (projectDir) ? f.getRelativePathFrom (projectDir) : c.path);
        x->setAttribute ("start", c.startBar); x->setAttribute ("bars", c.bars); x->setAttribute ("offset", c.offset); x->setAttribute ("gain", c.gain);
        x->setAttribute ("fin", c.fadeIn); x->setAttribute ("fout", c.fadeOut); x->setAttribute ("rev", c.reverse ? 1 : 0); x->setAttribute ("loop", c.loop ? 1 : 0);
        x->setAttribute ("pitch", c.pitch); x->setAttribute ("mode", c.mode); x->setAttribute ("stretch", c.stretch);
        if (c.muted) x->setAttribute ("muted", 1);
        if (c.name.isNotEmpty()) x->setAttribute ("name", c.name);
        if (c.colour) x->setAttribute ("colour", juce::String::toHexString ((int) c.colour));
        if (c.takeGroup) { x->setAttribute ("takes", c.takeGroup); x->setAttribute ("take", c.take); }
    }
    if (! t.chain->empty()) { juce::StringArray a; for (int p : *t.chain) a.add (juce::String (p)); e.createNewChildElement ("chain")->addTextElement (a.joinIntoString (" ")); }
    for (auto& r : *t.routes) { auto* x = e.createNewChildElement ("route"); x->setAttribute ("dst", r.dst); x->setAttribute ("ch", r.channels); x->setAttribute ("so", r.srcOffset); x->setAttribute ("do", r.dstOffset); x->setAttribute ("gain", r.gain); if (r.feedback) x->setAttribute ("fb", 1); }
    for (auto& m : *t.mods)
    {
        auto* x = e.createNewChildElement ("mod");
        x->setAttribute ("id", m.id); x->setAttribute ("on", m.on ? 1 : 0); x->setAttribute ("src", m.src); x->setAttribute ("shape", m.shape);
        x->setAttribute ("sync", m.sync ? 1 : 0); x->setAttribute ("div", m.div); x->setAttribute ("hz", m.hz); x->setAttribute ("phase", m.phase); x->setAttribute ("retrig", m.retrig ? 1 : 0);
        x->setAttribute ("skew", m.skew); x->setAttribute ("pw", m.pw); x->setAttribute ("slew", m.slew); x->setAttribute ("prob", m.prob); x->setAttribute ("depth", m.depth); x->setAttribute ("pol", m.polarity);
        x->setAttribute ("macro", m.macro); x->setAttribute ("cc", m.cc); x->setAttribute ("a", m.attackMs); x->setAttribute ("d", m.decayMs); x->setAttribute ("steplen", m.stepLen);
        juce::StringArray a; for (auto v : m.steps) a.add (juce::String (v, 4)); x->setAttribute ("steps", a.joinIntoString (" "));
        x->setAttribute ("dev", m.device); x->setAttribute ("param", m.param);
    }
}

void Session::loadTrackXml (Track& t, const juce::XmlElement& e, juce::StringArray& problems)
{
    t.name = e.getStringAttribute ("name", t.name);
    t.colour = (juce::uint32) e.getStringAttribute ("colour", juce::String::toHexString ((int) t.colour)).getHexValue32();
    t.kind = (TrackKind) e.getIntAttribute ("kind", 0);
    t.pattern.store (e.getIntAttribute ("pattern", 0)); t.speed.store (e.getIntAttribute ("speed", 3)); t.rootNote.store (e.getIntAttribute ("root", 60));
    t.swingOffset.store ((float) e.getDoubleAttribute ("swing", 0.0)); t.shiftMs.store ((float) e.getDoubleAttribute ("shift", 0.0)); t.probMul.store ((float) e.getDoubleAttribute ("prob", 1.0));
    t.freeClock.store (e.getIntAttribute ("free", 0) != 0); t.stepMs.store ((float) e.getDoubleAttribute ("stepMs", 125.0));
    t.evolve.store ((float) e.getDoubleAttribute ("evolve", 0.0)); t.chokeGroup.store (juce::jlimit (0, 8, e.getIntAttribute ("choke", 0)));
    t.phaseRate.store (juce::jlimit (0.5f, 2.0f, (float) e.getDoubleAttribute ("phase", 1.0)));
    if (auto* x = e.getChildByName ("scale"))
    {
        auto sc = std::make_shared<Tuning>(); sc->name = x->getStringAttribute ("name"); sc->period = x->getDoubleAttribute ("period", 1200.0);
        sc->cents.clear(); for (auto& c : juce::StringArray::fromTokens (x->getStringAttribute ("cents"), " ", "")) if (c.isNotEmpty()) sc->cents.push_back (c.getDoubleValue());
        if (sc->cents.empty()) sc->cents.push_back (0.0);
        setScale (t, sc);
    }
    else setScale (t, nullptr);
    t.solo.store (e.getIntAttribute ("solo", 0) != 0); t.armed.store (e.getIntAttribute ("armed", 0) != 0); t.chainOn.store (e.getIntAttribute ("chainOn", 0) != 0);
    t.toMaster.store (e.getIntAttribute ("toMaster", 1) != 0); t.outChannel.store (e.getIntAttribute ("out", 0)); t.keyTrack.store (e.getIntAttribute ("key", 0));
    t.inputSource.store (e.getIntAttribute ("inSrc", -2)); t.inputChannel.store (e.getIntAttribute ("inCh", 1)); t.inputCount.store (e.getIntAttribute ("inCount", 2)); t.monitor.store (e.getIntAttribute ("monitor", (int) Monitor::Auto));
    const auto pre = e.getStringAttribute ("pre");
    for (int r = 0; r < kReturns; ++r) t.sendPre[(size_t) r].store (r < pre.length() && pre[r] == '1');
    setWidth (t, e.getIntAttribute ("width", 2));
    if (auto* m = e.getChildByName ("mixer")) t.mixer->load (*m);
    auto make = [&] (const juce::XmlElement& x) -> std::unique_ptr<Device>
    {
        juce::String err;
        std::unique_ptr<Device> d;
        const auto type = x.getStringAttribute ("type");
        if (type.startsWith ("fal:") || type.startsWith ("st:")) d = createDevice (type, &err);
        else if (auto* px = x.getChildByName ("PLUGIN"))
        {
            juce::PluginDescription desc;
            if (desc.loadFromXml (*px)) { d = createHosted (desc, &err); if (! d && known.getTypeForFile (desc.fileOrIdentifier) == nullptr) err = "not installed: " + desc.name; }
        }
        if (! d) { problems.add (x.getStringAttribute ("name") + ": " + err); return nullptr; }
        d->uid = x.getIntAttribute ("uid", d->uid);
        nextUid = std::max (nextUid, d->uid + 1);
        d->load (x);
        return d;
    };
    if (auto* x = e.getChildByName ("instrument")) if (auto d = make (*x)) setInstrument (t, std::move (d));
    for (auto* x : e.getChildWithTagNameIterator ("insert")) if (auto d = make (*x)) addInsert (t, std::move (d));
    for (auto* x : e.getChildWithTagNameIterator ("pattern"))
    {
        const int i = x->getIntAttribute ("i", -1);
        if (i < 0 || i >= kPatterns) continue;
        Pattern p; p.length = juce::jlimit (1, kMaxSteps, x->getIntAttribute ("length", 16));
        for (auto* s : x->getChildWithTagNameIterator ("s")) { const int k = s->getIntAttribute ("i", -1); if (k >= 0 && k < kMaxSteps) p.s[(size_t) k] = loadStep (*s); }
        setPattern (t, i, std::make_shared<const Pattern> (p));
    }
    std::vector<Clip> clips;
    for (auto* x : e.getChildWithTagNameIterator ("clip"))
    {
        Clip c; c.pattern = x->getIntAttribute ("pattern"); c.startBar = x->getDoubleAttribute ("start"); c.bars = x->getDoubleAttribute ("bars", 4.0);
        c.offset = x->getDoubleAttribute ("offset"); c.muted = x->getIntAttribute ("muted") != 0; c.id = x->getIntAttribute ("id", 0);
        c.name = x->getStringAttribute ("name"); c.colour = (juce::uint32) x->getStringAttribute ("colour").getHexValue32();
        nextClipId = std::max (nextClipId, c.id + 1);
        clips.push_back (c);
    }
    editClips (t, [&] (std::vector<Clip>& c) { c = clips; });
    std::vector<AudioClip> aclips;
    for (auto* x : e.getChildWithTagNameIterator ("aclip"))
    {
        AudioClip c;
        c.id = x->getIntAttribute ("id", 0);
        const auto p = x->getStringAttribute ("path");
        juce::File f = juce::File::isAbsolutePath (p) ? juce::File (p) : projectDir().getChildFile (p);
        juce::String err;
        c.sample = loadSample (f, &err);
        if (! c.sample) { problems.add ("missing audio: " + p + " (" + err + ")"); }
        c.path = f.getFullPathName();
        c.startBar = x->getDoubleAttribute ("start"); c.bars = x->getDoubleAttribute ("bars", 1.0); c.offset = x->getDoubleAttribute ("offset"); c.gain = (float) x->getDoubleAttribute ("gain", 1.0);
        c.fadeIn = x->getDoubleAttribute ("fin"); c.fadeOut = x->getDoubleAttribute ("fout"); c.reverse = x->getIntAttribute ("rev") != 0; c.loop = x->getIntAttribute ("loop") != 0;
        c.pitch = (float) x->getDoubleAttribute ("pitch"); c.mode = x->getIntAttribute ("mode"); c.stretch = x->getDoubleAttribute ("stretch", 1.0); c.muted = x->getIntAttribute ("muted") != 0;
        c.name = x->getStringAttribute ("name"); c.colour = (juce::uint32) x->getStringAttribute ("colour").getHexValue32();
        c.takeGroup = x->getIntAttribute ("takes"); c.take = x->getIntAttribute ("take");
        nextTakeGroup = std::max (nextTakeGroup, c.takeGroup + 1);
        nextClipId = std::max (nextClipId, c.id + 1);
        aclips.push_back (c);
    }
    if (! aclips.empty()) editAudioClips (t, [&] (std::vector<AudioClip>& v) { v = aclips; });
    if (auto* c = e.getChildByName ("chain")) { std::vector<int> ch; for (auto& s : juce::StringArray::fromTokens (c->getAllSubText(), " ", "")) if (s.isNotEmpty()) ch.push_back (s.getIntValue()); setChain (t, ch); }
    std::vector<Modulator> mods;
    for (auto* x : e.getChildWithTagNameIterator ("mod"))
    {
        Modulator m;
        m.id = x->getIntAttribute ("id"); m.on = x->getIntAttribute ("on", 1) != 0; m.src = x->getIntAttribute ("src"); m.shape = x->getIntAttribute ("shape");
        m.sync = x->getIntAttribute ("sync", 1) != 0; m.div = x->getIntAttribute ("div", 11); m.hz = (float) x->getDoubleAttribute ("hz", 1.0); m.phase = (float) x->getDoubleAttribute ("phase"); m.retrig = x->getIntAttribute ("retrig") != 0;
        m.skew = (float) x->getDoubleAttribute ("skew", 0.5); m.pw = (float) x->getDoubleAttribute ("pw", 0.5); m.slew = (float) x->getDoubleAttribute ("slew"); m.prob = (float) x->getDoubleAttribute ("prob", 0.5);
        m.depth = (float) x->getDoubleAttribute ("depth", 0.25); m.polarity = x->getIntAttribute ("pol"); m.macro = x->getIntAttribute ("macro"); m.cc = x->getIntAttribute ("cc", 1);
        m.attackMs = (float) x->getDoubleAttribute ("a", 2.0); m.decayMs = (float) x->getDoubleAttribute ("d", 220.0); m.stepLen = x->getIntAttribute ("steplen", 16);
        auto a = juce::StringArray::fromTokens (x->getStringAttribute ("steps"), " ", "");
        for (int i = 0; i < kStepMod && i < a.size(); ++i) m.steps[(size_t) i] = a[i].getFloatValue();
        m.device = x->getIntAttribute ("dev"); m.param = x->getIntAttribute ("param", -1);
        nextModId = std::max (nextModId, m.id + 1);
        mods.push_back (m);
    }
    if (! mods.empty()) editMods (t, [&] (std::vector<Modulator>& v) { v = mods; });
}

std::unique_ptr<juce::XmlElement> Session::save() const
{
    const_cast<Session*> (this)->recomputeIds();
    auto e = std::make_unique<juce::XmlElement> ("studio");
    e->setAttribute ("version", kFormatVersion);
    e->setAttribute ("suite", kDeep ? "deep" : "faltung");
    e->setAttribute ("name", projectName);
    e->setAttribute ("mode", mode.load()); e->setAttribute ("loop", loopOn.load() ? 1 : 0); e->setAttribute ("loopStart", loopStart.load()); e->setAttribute ("loopEnd", loopEnd.load());
    e->setAttribute ("micro", microMs.load()); e->setAttribute ("masterOut", masterOut.load()); e->setAttribute ("quant", launchQuant.load()); e->setAttribute ("selected", selectedTrack.load());
    e->setAttribute ("multiOut", multiOut.load() ? 1 : 0); e->setAttribute ("masterBypass", masterBypass.load() ? 1 : 0); e->setAttribute ("snap", snap.load() ? 1 : 0); e->setAttribute ("grid", gridIndex.load()); e->setAttribute ("follow", follow.load() ? 1 : 0); e->setAttribute ("punch", punch.load() ? 1 : 0); e->setAttribute ("click", metronome.load() ? 1 : 0); e->setAttribute ("countIn", countInBars.load());
    e->setAttribute ("nextUid", nextUid); e->setAttribute ("nextClip", nextClipId); e->setAttribute ("nextMod", nextModId);
    global->save (*e->createNewChildElement ("global"));
    for (int i = 0; i < kMacros; ++i) { auto* m = e->createNewChildElement ("macro"); m->setAttribute ("i", i); m->setAttribute ("name", macroNames[(size_t) i]); }
    const auto pdir = projectDir();
    for (auto& t : tracks) saveTrack (*e->createNewChildElement ("track"), *t, pdir);
    for (auto& r : returns) saveTrack (*e->createNewChildElement ("return"), *r, pdir);
    saveTrack (*e->createNewChildElement ("master"), *master, pdir);
    for (int i = 0; i < (int) scenes->size(); ++i)
    {
        const auto& sc = (*scenes)[(size_t) i];
        bool any = sc.name != juce::String (i + 1);
        for (int k = 0; k < (int) tracks.size(); ++k) any |= sc.patternFor (k) != -2;
        if (! any) continue;
        auto* x = e->createNewChildElement ("scene"); x->setAttribute ("i", i); x->setAttribute ("name", sc.name);
        juce::StringArray a; for (int k = 0; k < (int) tracks.size(); ++k) a.add (juce::String (sc.patternFor (k))); x->setAttribute ("p", a.joinIntoString (" "));
    }
    for (auto& s : *song) { auto* x = e->createNewChildElement ("song"); x->setAttribute ("scene", s.scene); x->setAttribute ("bars", s.bars); }
    for (auto& m : *markers) { auto* x = e->createNewChildElement ("marker"); x->setAttribute ("name", m.name); x->setAttribute ("bar", m.bar); }
    if (! meter->plain()) for (auto& m : meter->getChanges()) { auto* x = e->createNewChildElement ("meter"); x->setAttribute ("bar", m.bar); x->setAttribute ("sigs", timeline::sigsText (m.sigs)); }
    for (auto& l : *lanes)
    {
        auto* x = e->createNewChildElement ("lane"); x->setAttribute ("target", l->target.toString()); x->setAttribute ("on", l->enabled ? 1 : 0);
        for (auto& p : l->pts) { auto* q = x->createNewChildElement ("pt"); q->setAttribute ("b", p.beat); q->setAttribute ("v", p.value); if (p.curve != 0.0f) q->setAttribute ("c", p.curve); }
    }
    for (auto& m : *midiMaps) { auto* x = e->createNewChildElement ("midimap"); x->setAttribute ("cc", m.cc); x->setAttribute ("target", m.target.toString()); x->setAttribute ("lo", m.lo); x->setAttribute ("hi", m.hi); }
    for (auto& m : *keyMaps) { auto* x = e->createNewChildElement ("keymap"); x->setAttribute ("key", m.key); x->setAttribute ("target", m.target.toString()); if (m.momentary) x->setAttribute ("hold", 1); }
    return e;
}

void Session::clear()
{
    epoch.fetch_add (1);   // every track object is about to be replaced: views holding them must rebuild, even if the uids come back the same
    stop();
    stopRecording();
    while (! tracks.empty()) removeTrack ((int) tracks.size() - 1);
    while (! returns.empty()) removeReturn ((int) returns.size() - 1);
    for (int r = 0; r < 2; ++r) addReturn();
    while (! master->inserts.empty()) retire (removeDevice (*master, master->inserts.back()->uid));
    // the globals and the master's strip stay the same objects (the header's controls are bound to them): only their values start over
    for (auto* d : { master->mixer.get(), global.get() })
        for (int i = 0; i < d->numParams(); ++i) { d->setBase (i, d->defaultValue (i)); d->macro[(size_t) i].index.store (-1); d->macro[(size_t) i].lo.store (0.0f); d->macro[(size_t) i].hi.store (1.0f); }
    { const juce::SpinLock::ScopedLockType sl (lock); master->mods = std::make_shared<const std::vector<Modulator>>(); master->modState.clear(); }
    setWidth (*master, 2);
    for (int i = 0; i < kMacros; ++i) macroNames[(size_t) i] = "macro " + juce::String (i + 1);
    editScenes ([] (std::vector<Scene>& s) { s.assign ((size_t) kScenes, Scene()); for (int i = 0; i < kScenes; ++i) s[(size_t) i].name = juce::String (i + 1); });
    editSong ([] (std::vector<SongEntry>& s) { s.clear(); });
    editMarkers ([] (std::vector<Marker>& m) { m.clear(); });
    setMeter ({});
    editMidiMaps ([] (std::vector<MidiMap>& m) { m.clear(); });
    editKeyMaps ([] (std::vector<KeyMap>& m) { m.clear(); });
    stopPreview(); keyLearning.store (0); learning.store (false);
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = std::make_shared<const std::vector<LanePtr>>(); }
    mode.store ((int) PlayMode::Session); loopOn.store (false); loopStart.store (0); loopEnd.store (8); microMs.store (12.0f); masterOut.store (1); launchQuant.store (1);
    multiOut.store (false); masterBypass.store (false); snap.store (true); gridIndex.store (0); follow.store (true); punch.store (false); metronome.store (false); countInBars.store (1);
    selectedTrack.store (0); currentScene.store (-1); internalPpq.store (0.0);
    projectName = "untitled"; projectFile = juce::File();
    nextUid = 1; nextClipId = 1; nextModId = 1; nextTakeGroup = 1;
    rebuildLatency();
    dirty = false;
}

bool Session::loadImpl (const juce::XmlElement& e, juce::String* error)
{
    if (! e.hasTagName ("studio")) { if (error) *error = "not a studio project"; return false; }
    clear();
    juce::StringArray problems;
    projectName = e.getStringAttribute ("name", "untitled");
    mode.store (e.getIntAttribute ("mode", 0)); loopOn.store (e.getIntAttribute ("loop", 0) != 0);
    loopStart.store (e.getDoubleAttribute ("loopStart", 0.0)); loopEnd.store (e.getDoubleAttribute ("loopEnd", 8.0));
    microMs.store ((float) e.getDoubleAttribute ("micro", 12.0)); masterOut.store (e.getIntAttribute ("masterOut", 1)); launchQuant.store (e.getIntAttribute ("quant", 1));
    multiOut.store (e.getIntAttribute ("multiOut", 0) != 0); masterBypass.store (e.getIntAttribute ("masterBypass", 0) != 0); snap.store (e.getIntAttribute ("snap", 1) != 0); gridIndex.store (e.getIntAttribute ("grid", 0)); follow.store (e.getIntAttribute ("follow", 1) != 0); punch.store (e.getIntAttribute ("punch", 0) != 0); metronome.store (e.getIntAttribute ("click", 0) != 0); countInBars.store (juce::jlimit (0, 4, e.getIntAttribute ("countIn", 1)));
    if (auto* g = e.getChildByName ("global")) global->load (*g);
    for (auto* m : e.getChildWithTagNameIterator ("macro")) { const int i = m->getIntAttribute ("i", -1); if (i >= 0 && i < kMacros) macroNames[(size_t) i] = m->getStringAttribute ("name"); }
    for (auto* x : e.getChildWithTagNameIterator ("track"))
    {
        auto* t = addTrack (x->getStringAttribute ("name", "track"), -1, (TrackKind) x->getIntAttribute ("kind", 0));
        if (! t) break;
        t->uid = x->getIntAttribute ("uid", t->uid);
        nextUid = std::max (nextUid, t->uid + 1);
    }
    // routes refer to uids, so load the tracks' contents once every track exists
    int ti = 0;
    for (auto* x : e.getChildWithTagNameIterator ("track")) if (ti < (int) tracks.size()) loadTrackXml (*tracks[(size_t) ti++], *x, problems);
    while (! returns.empty()) removeReturn ((int) returns.size() - 1);
    for (auto* x : e.getChildWithTagNameIterator ("return")) if (auto* r = addReturn (x->getStringAttribute ("name"))) loadTrackXml (*r, *x, problems);
    if (returns.empty()) for (int r = 0; r < 2; ++r) addReturn();
    if (auto* x = e.getChildByName ("master")) loadTrackXml (*master, *x, problems);
    ti = 0;
    for (auto* x : e.getChildWithTagNameIterator ("track"))
    {
        if (ti >= (int) tracks.size()) break;
        auto& t = *tracks[(size_t) ti++];
        for (auto* r : x->getChildWithTagNameIterator ("route"))
            if (r->getIntAttribute ("fb", 0) == 0)
                addRoute (t, { r->getIntAttribute ("dst"), r->getIntAttribute ("ch", 2), r->getIntAttribute ("so"), r->getIntAttribute ("do"), (float) r->getDoubleAttribute ("gain", 1.0) });
    }
    // the feedback routes last: they are the ones that close a loop over the others
    ti = 0;
    for (auto* x : e.getChildWithTagNameIterator ("track"))
    {
        if (ti >= (int) tracks.size()) break;
        auto& t = *tracks[(size_t) ti++];
        for (auto* r : x->getChildWithTagNameIterator ("route"))
            if (r->getIntAttribute ("fb", 0) != 0)
                addRoute (t, { r->getIntAttribute ("dst"), r->getIntAttribute ("ch", 2), r->getIntAttribute ("so"), r->getIntAttribute ("do"), (float) r->getDoubleAttribute ("gain", 1.0), true });
    }
    editScenes ([&] (std::vector<Scene>& s)
    {
        for (auto* x : e.getChildWithTagNameIterator ("scene"))
        {
            const int i = x->getIntAttribute ("i", -1); if (i < 0 || i >= kScenes) continue;
            s[(size_t) i].name = x->getStringAttribute ("name", juce::String (i + 1));
            auto a = juce::StringArray::fromTokens (x->getStringAttribute ("p"), " ", "");
            s[(size_t) i].pattern.assign ((size_t) std::max ((int) tracks.size(), a.size()), -2);
            for (int k = 0; k < a.size(); ++k) s[(size_t) i].pattern[(size_t) k] = a[k].getIntValue();
        }
    });
    editSong ([&] (std::vector<SongEntry>& s) { for (auto* x : e.getChildWithTagNameIterator ("song")) s.push_back ({ x->getIntAttribute ("scene"), x->getIntAttribute ("bars", 8) }); });
    editMarkers ([&] (std::vector<Marker>& m) { for (auto* x : e.getChildWithTagNameIterator ("marker")) m.push_back ({ x->getStringAttribute ("name"), x->getDoubleAttribute ("bar") }); });
    {
        std::vector<timeline::MeterChange> mc;
        for (auto* x : e.getChildWithTagNameIterator ("meter")) { std::vector<timeline::Sig> sg; if (timeline::parseSigs (x->getStringAttribute ("sigs"), sg)) mc.push_back ({ x->getIntAttribute ("bar"), sg }); }
        setMeter (mc);
    }
    for (auto* x : e.getChildWithTagNameIterator ("lane"))
    {
        const Target t = Target::fromString (x->getStringAttribute ("target"));
        if (! t.valid()) continue;
        editLane (t, [&] (AutoLane& l)
        {
            l.enabled = x->getIntAttribute ("on", 1) != 0; l.pts.clear();
            for (auto* q : x->getChildWithTagNameIterator ("pt")) l.pts.push_back ({ q->getDoubleAttribute ("b"), (float) q->getDoubleAttribute ("v"), (float) q->getDoubleAttribute ("c", 0.0) });
        });
    }
    editMidiMaps ([&] (std::vector<MidiMap>& m) { for (auto* x : e.getChildWithTagNameIterator ("midimap")) m.push_back ({ x->getIntAttribute ("cc"), Target::fromString (x->getStringAttribute ("target")), (float) x->getDoubleAttribute ("lo", 0.0), (float) x->getDoubleAttribute ("hi", 1.0) }); });
    editKeyMaps ([&] (std::vector<KeyMap>& m) { for (auto* x : e.getChildWithTagNameIterator ("keymap")) m.push_back ({ x->getIntAttribute ("key"), Target::fromString (x->getStringAttribute ("target")), x->getIntAttribute ("hold", 0) != 0 }); });
    selectedTrack.store (juce::jlimit (-100, std::max (0, (int) tracks.size() - 1), e.getIntAttribute ("selected", 0)));
    nextUid = std::max (nextUid, e.getIntAttribute ("nextUid", nextUid));
    nextClipId = std::max (nextClipId, e.getIntAttribute ("nextClip", nextClipId));
    recomputeIds();
    rebuildLatency();
    dirty = false;
    if (error) *error = problems.joinIntoString ("\n");
    return true;
}

// ---- templates ------------------------------------------------------------------------------------
juce::StringArray Session::templateNames()
{
    return { "empty", kDeep ? "deep house starter" : "microhouse starter", kDeep ? "dub house" : "dub techno", kDeep ? "long form (deep)" : "long form (rominimal)", "multi-out to ableton",
             "bass first (half time, 172)", "two-step (garage, 132)", "spatial (encoder, 24 speakers, binaural)", "half-step (140)" };
}

namespace
{
/** 16 characters per 16 steps: x on · X accent · o soft · . off · ? 50% · r ratchet · < early · > late · f fill-only · ! not on fill */
Pattern pat (const juce::String& str, int length = -1)
{
    Pattern p;
    const auto s = str.removeCharacters (" ");
    p.length = length > 0 ? length : juce::jlimit (1, kMaxSteps, s.length());
    for (int i = 0; i < kMaxSteps && i < s.length(); ++i)
    {
        Step& st = p.s[(size_t) i];
        const auto c = s[i];
        if (c == '.') continue;
        st.on = true; st.vel = 0.8f;
        switch (c)
        {
            case 'X': st.vel = 1.0f; break;
            case 'o': st.vel = 0.5f; break;
            case '?': st.prob = 0.5f; break;
            case 'r': st.ratchet = 2; break;
            case '<': st.micro = -0.35f; break;
            case '>': st.micro = 0.35f; break;
            case 'f': st.cond = Cond::Fill; break;
            case '!': st.cond = Cond::NotFill; break;
            case '2': st.cond = Cond::X2of2; break;
            case '4': st.cond = Cond::X4of4; break;
            default: break;
        }
    }
    return p;
}
Pattern withPitch (Pattern p, std::initializer_list<int> pitches, float len = 0.6f)
{
    int k = 0; const std::vector<int> v (pitches);
    for (auto& s : p.s) if (s.on && ! v.empty()) { s.pitch = v[(size_t) (k++ % (int) v.size())]; s.len = len; }
    return p;
}
Pattern thinned (Pattern p, uint32_t seed, float keep)
{
    Rng r; r.seed (seed);
    for (auto& s : p.s) if (s.on && r.uni() > keep) s.on = false;
    return p;
}
int programNamed (const Device& d, const juce::String& name)
{
    for (int i = 0; i < d.numPrograms(); ++i) if (d.programName (i) == name) return i;
    return -1;
}
struct Def { const char* name; const char* type; const char* preset; int root; };
}

void Session::newFromTemplateImpl (int which)
{
    clear();
    if (which == 0)
    {
        auto* t = addTrack ("kick"); setInstrument (*t, createDevice ("fal:bd1"));
        projectName = "untitled";
        dirty = false;
        return;
    }
    if (which == 4) { newFromTemplateImpl (1); projectName = "multi-out to ableton"; assignOutputs(); masterBypass.store (true); dirty = false; return; }
    if (which == 7)
    {
        // the ambisonic session: the starter's tracks behind one encoder each would be a lot to look at, so three sources
        // (kick, hat, chords) go through encoders at 16 channels into a 24-speaker decoder and a binaural decoder
        newFromTemplate (0);
        projectName = templateNames()[7];
        struct Src { const char* name; const char* type; const char* preset; float az; const char* steps; int root; };
        const Src src[3] = { { "kick", "fal:bd1", kDeep ? "deep round" : "tok", 0.0f, "X...x...X...x...", 60 }, { "hat", "fal:hh4", kDeep ? "warm hats" : "808 hats", 90.0f, "x.o.x.o.x.o.x.o?", 60 },
                             { "perc", "fal:pc7", kDeep ? "conga low" : "conga", -120.0f, ".x..?.x....x.?..", 60 } };
        removeTrack (0);
        auto* dec = addTrack ("speakers 24"); setWidth (*dec, 24);
        if (auto d = createDevice ("st:decoder")) { d->setBase (d->paramIndexForId ("layout"), 0.5f); addInsert (*dec, std::move (d)); }
        auto* bin = addTrack ("binaural"); setWidth (*bin, 16);
        if (auto d = createDevice ("st:binaural")) addInsert (*bin, std::move (d));
        for (int i = 0; i < 3; ++i)
        {
            auto* t = addTrack (src[i].name, i); t->rootNote.store (src[i].root);
            auto dev = createDevice (src[i].type);
            if (dev) { const int p = programNamed (*dev, src[i].preset); if (p >= 0) dev->setProgram (p); }
            setInstrument (*t, std::move (dev));
            setWidth (*t, 16); t->toMaster.store (false);
            if (auto e = createDevice ("st:encoder")) { e->setBase (e->paramIndexForId ("azimuth"), (src[i].az + 180.0f) / 360.0f); addInsert (*t, std::move (e)); }
            setPattern (*t, 0, std::make_shared<const Pattern> (pat (src[i].steps)));
            addRoute (*t, { dec->uid, 16, 0, 0, 1.0f }); addRoute (*t, { bin->uid, 16, 0, 0, 1.0f });
            t->pattern.store (0); t->pendingPattern.store (0); t->pendingAt.store (0.0);
        }
        // the hat circles the room: a slow ramp on its encoder's azimuth
        { auto* t = tracks[1].get(); Modulator m; m.src = MOD_LFO; m.shape = 3; m.div = 16; m.depth = 0.5f; m.polarity = 0; m.device = t->inserts[0]->uid; m.param = t->inserts[0]->paramIndexForId ("azimuth"); addModulator (*t, m); }
        // arranging on headphones: the master is stereo, the 24-speaker decoder is muted. For the room: master width 24, unmute it, mute the binaural
        dec->mixer->setBase (MP_MUTE, 1.0f);
        editScenes ([&] (std::vector<Scene>& s) { s[0].name = "all"; s[0].pattern.assign (tracks.size(), -2); for (int k = 0; k < 3; ++k) s[0].pattern[(size_t) k] = 0; });
        loopEnd.store (4);
        rebuildLatency();
        dirty = false;
        return;
    }
    const bool dub = which == 2, longForm = which == 3, half = which == 5, garage = which == 6, halfStep = which == 8;
    projectName = templateNames()[which];
    std::vector<Def> defs;
    if (kDeep)
        defs = { { "kick", "fal:bd1", "deep round", 60 }, { "rim", "fal:rm3", "wooden rim", 60 }, { "hat", "fal:hh4", "warm hats", 60 }, { "open hat", "fal:hh4", "warm hats", 70 },
                 { "clap", "fal:cp6", "soft room clap", 60 }, { "shaker", "fal:hh4", "shaker", 60 }, { "conga", "fal:pc7", "conga low", 60 }, { "stabs", "fal:sb1", "", 48 } };
    else
        defs = { { "kick", "fal:bd1", dub ? "perlon thud" : "tok", 60 }, { "rim", "fal:rm3", "tok", 60 }, { "hat", "fal:hh4", "808 hats", 60 }, { "open hat", "fal:hh4", "808 hats", 70 },
                 { "clap", "fal:cp6", "tight", 60 }, { "perc", "fal:pc7", "conga", 60 }, { "click", "fal:cl8", "tick", 60 },
                 { dub ? "chords" : "bass", "fal:wb1", dub ? "neuro stab" : half ? "reese" : garage ? "square pump" : halfStep ? "sub wob" : "sub pressure", dub ? 48 : 36 } };
    if (dub) defs.push_back ({ "sub", "fal:wb1", "sub pressure", 36 });
    std::vector<Track*> tr;
    for (auto& d : defs)
    {
        auto* t = addTrack (d.name);
        auto dev = createDevice (d.type);
        if (dev && juce::String (d.preset).isNotEmpty()) { const int p = programNamed (*dev, d.preset); if (p >= 0) dev->setProgram (p); }
        if (dev && juce::String (d.name) == "chords")
        {
            // a minor 9 stab with a slow filter and short amp envelope: the dub chord
            dev->setBase (dev->paramIndexForId ("chord"), 9.0f / 14.0f); dev->setBase (dev->paramIndexForId ("voicing"), 0.5f);
            dev->setBase (dev->paramIndexForId ("ad"), 0.35f); dev->setBase (dev->paramIndexForId ("as"), 0.0f); dev->setBase (dev->paramIndexForId ("ar"), 0.3f);
            dev->setBase (dev->paramIndexForId ("cutoff"), 0.55f); dev->setBase (dev->paramIndexForId ("level"), 0.45f);
        }
        setInstrument (*t, std::move (dev));
        t->rootNote.store (d.root);
        tr.push_back (t);
    }
    // the base patterns: kick rim hat open clap perc click bass (sub)
    std::vector<Pattern> base = {
        pat (dub ? "X...X...X...X..." : "X...x...X...x..."),
        pat (dub ? "....x.......x.<." : "..<...x...<...x>"),
        pat ("x.o.x.o.x.o.x.o?"),
        pat ("..x...x...x...x."),
        pat ("....X.......X..."),
        pat (".x..?.x....x.?..", 12),
        pat ("?...o.?.....o..?", 7),
        dub ? withPitch (pat ("x.....x...x....."), { 0, 0, 3 }, 1.2f) : withPitch (pat ("x..x..x...x..x.."), { 0, 0, 7, 0, 3, 0 }, 0.9f),
    };
    if (dub) base.push_back (withPitch (pat ("x.......x......."), { 0 }, 3.5f));
    if (half)
    {
        // half time at 172: the kick on one, the clap on three, and the bass line carrying it
        base[0] = pat ("X.........x....."); base[1] = pat ("......<.......x>"); base[2] = pat ("x.x.x.x.x.x.x.x?"); base[3] = pat ("......x.......x.");
        base[4] = pat ("........X......."); base[5] = pat (".x....x..?.x....", 12); base[6] = pat ("?..o..?.....o..?", 7);
        base[7] = withPitch (pat ("x..x..x...x.x..."), { 0, 0, 3, 0, -2 }, 1.4f);
    }
    if (halfStep)
    {
        // 140, the weight on one and three: a kick, a clap on the three, sparse hats, and the wobbling sub doing the talking
        base[0] = pat ("X.........o....."); base[1] = pat ("......<........>"); base[2] = pat ("..x...x...x..?x."); base[3] = pat ("..............x.");
        base[4] = pat ("........X......."); base[5] = pat (".....x.....?....", 12); base[6] = pat ("?.....o......?..", 7);
        base[7] = withPitch (pat ("x.......x..x...."), { 0, 0, -2 }, 3.0f);
    }
    if (garage)
    {
        base[0] = pat ("X......x..X....."); base[1] = pat ("..<....x..<...x>"); base[2] = pat ("x.xx.xx.x.xx.xx."); base[3] = pat ("..x...x...x...x.");
        base[4] = pat ("....X.......X..."); base[5] = pat (".x..?.x....x.?..", 12); base[6] = pat ("?...o.?.....o..?", 7);
        base[7] = withPitch (pat ("x..x...x..x...x."), { 0, 7, 0, 3, 10 }, 0.7f);
    }
    const char* fills[9] = { "X...x...X...x.rr", "..<.r.x...<.x.x>", "x.o.x.o.x.o.xrrr", "..x...x...x...xx", "....X.......XXrr", ".x..x.x..x.x.x.x", "?.?.o.?.?.?.o.??", "x..x..x..xx.x.xx", "x.......x...x..." };
    for (size_t i = 0; i < tr.size(); ++i)
    {
        for (int p = 0; p < 8; ++p)
        {
            Pattern v = base[i];
            if (p == 7) { v = pat (fills[i], base[i].length); if (i == 7) v = withPitch (v, { 0, 0, 7, 0, 3, 0, 10, 12 }, 0.6f); }
            else if (p > 0 && p % 2 == 1) v = thinned (v, (uint32_t) (i * 31 + p), 0.75f);
            else if (p > 0) { v = base[i]; for (auto& s : v.s) if (s.on && p >= 4) s.vel = juce::jlimit (0.3f, 1.0f, s.vel + 0.1f * (p - 4)); }
            setPattern (*tr[i], p, std::make_shared<const Pattern> (v));
        }
    }
    tr[2]->chokeGroup.store (1); tr[3]->chokeGroup.store (1);   // the closed hat cuts the open one, as on a drum machine
    // returns: dub on 1, room on 2
    returns[0]->name = "dub"; returns[1]->name = "room";
    { auto d = createDevice ("fal:dl4"); if (d) { d->setBase (d->paramIndexForId ("division"), 9.0f / 18.0f); d->setBase (d->paramIndexForId ("feedback"), dub ? 0.62f : 0.5f); d->setBase (d->paramIndexForId ("mix"), 1.0f); addInsert (*returns[0], std::move (d)); } }
    { auto d = createDevice ("fal:rv5"); if (d) { d->setBase (d->paramIndexForId ("model"), dub ? 1.0f : 2.0f / 3.0f); d->setBase (d->paramIndexForId ("mix"), 1.0f); d->setBase (d->paramIndexForId ("gate"), dub ? 0.0f : 0.45f); d->setBase (d->paramIndexForId ("decay"), dub ? 0.55f : 0.3f); addInsert (*returns[1], std::move (d)); } }
    // master: filter (macro) → glue, and the mastering bus bypassed (Ableton masters) — in produce mode it is on
    { auto d = createDevice ("fal:fl7"); if (d) { d->macro[(size_t) d->paramIndexForId ("sweep")].index.store (2); d->macro[(size_t) d->paramIndexForId ("sweep")].lo.store (0.5f); d->macro[(size_t) d->paramIndexForId ("sweep")].hi.store (0.95f); addInsert (*master, std::move (d)); } }
    { auto d = createDevice ("fal:gl8"); if (d) addInsert (*master, std::move (d)); }
    if (! kDeep) { auto d = createDevice ("fal:ms0"); if (d) { d->bypass.store (true); addInsert (*master, std::move (d)); } }
    // sends
    tr[1]->mixer->setBase (MP_SEND1, dub ? 0.75f : 0.6f); tr[4]->mixer->setBase (MP_SEND1 + 1, 0.65f); tr[6]->mixer->setBase (MP_SEND1, 0.45f); tr[5]->mixer->setBase (MP_SEND1 + 1, 0.5f);
    if (dub) { tr[7]->mixer->setBase (MP_SEND1, 0.8f); tr[7]->mixer->setBase (MP_SEND1 + 1, 0.5f); }
    tr[3]->mixer->setBase (MP_LEVEL, 0.7f); tr[6]->mixer->setBase (MP_LEVEL, 0.65f); tr[7]->mixer->setBase (MP_LEVEL, 0.78f);
    tr[1]->mixer->setBase (MP_PAN, 0.42f); tr[5]->mixer->setBase (MP_PAN, 0.62f); tr[6]->mixer->setBase (MP_PAN, 0.35f);
    global->setBase (GP_MASTER, 0.74f);
    // macros
    macroNames[0] = "dub"; macroNames[1] = "room"; macroNames[2] = "filter"; macroNames[3] = "hats"; macroNames[4] = "bass cut";
    if (auto* d = returns[0]->inserts.empty() ? nullptr : returns[0]->inserts[0].get()) { const int fb = d->paramIndexForId ("feedback"); d->macro[(size_t) fb].index.store (0); d->macro[(size_t) fb].lo.store (0.3f); d->macro[(size_t) fb].hi.store (0.9f); }
    if (auto* d = returns[1]->inserts.empty() ? nullptr : returns[1]->inserts[0].get()) { const int dc = d->paramIndexForId ("decay"); d->macro[(size_t) dc].index.store (1); d->macro[(size_t) dc].lo.store (0.1f); d->macro[(size_t) dc].hi.store (0.7f); }
    { auto* d = tr[2]->instrument.get(); const int i = d->paramIndexForId ("decay"); d->macro[(size_t) i].index.store (3); d->macro[(size_t) i].lo.store (0.1f); d->macro[(size_t) i].hi.store (0.6f); }
    { auto* d = tr[7]->instrument.get(); const int i = d->paramIndexForId ("cutoff"); if (i >= 0) { d->macro[(size_t) i].index.store (4); d->macro[(size_t) i].lo.store (d->baseValue (i)); d->macro[(size_t) i].hi.store (0.85f); } }
    setMacro (0, 0.3f); setMacro (1, 0.5f); setMacro (2, 0.0f); setMacro (3, 0.4f); setMacro (4, 0.0f);
    // modulators: the hats breathe with a one-bar lfo, the click wanders in the stereo field, the rim's tune drifts
    { Modulator m; m.src = MOD_LFO; m.div = 14; m.shape = 0; m.depth = 0.12f; m.polarity = 1; m.device = tr[2]->instrument->uid; m.param = tr[2]->instrument->paramIndexForId ("tone"); addModulator (*tr[2], m); }
    { Modulator m; m.src = MOD_HIT; m.depth = 0.3f; m.device = 0; m.param = MP_PAN; addModulator (*tr[6], m); }
    { Modulator m; m.src = MOD_LFO; m.shape = 9; m.div = 15; m.depth = 0.04f; m.device = tr[1]->instrument->uid; m.param = tr[1]->instrument->paramIndexForId ("tune"); addModulator (*tr[1], m); }
    if (dub) { Modulator m; m.src = MOD_RANDOM; m.div = 13; m.slew = 0.6f; m.depth = 0.2f; m.device = tr[7]->instrument->uid; m.param = tr[7]->instrument->paramIndexForId ("cutoff"); addModulator (*tr[7], m); }
    // scenes: an eight-scene build, and the song that plays them
    const int sc[8][9] = { { 0, -1, 0, -1, -1, -1, 0, -1, -1 }, { 0, 0, 0, 0, -1, -1, 0, -1, 0 }, { 0, 0, 2, 0, -1, -1, 1, 0, 0 }, { 2, 1, 2, 2, 0, 0, 2, 0, 0 },
                           { 4, 4, 4, 4, 4, 4, 4, 4, 4 }, { -1, 5, 5, 5, 6, 1, 5, 2, 2 }, { 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 7, 7, 7, 7, 7, 7, 7, 7, 7 } };
    const char* names[8] = { "intro", "rim in", "bass in", "clap", "full", "break", "full 2", "fill out" };
    const int nt = (int) tr.size();
    if (! longForm)
    {
        editScenes ([&] (std::vector<Scene>& s) { for (int i = 0; i < 8; ++i) { s[(size_t) i].name = names[i]; s[(size_t) i].pattern.assign ((size_t) nt, -2); for (int k = 0; k < nt; ++k) s[(size_t) i].pattern[(size_t) k] = sc[i][k]; } });
        editSong ([] (std::vector<SongEntry>& s) { for (int i = 0; i < 8; ++i) s.push_back ({ i, 8 }); });
        editMarkers ([&] (std::vector<Marker>& m) { for (int i = 0; i < 8; ++i) m.push_back ({ names[i], 8.0 * i }); });
        for (int k = 0; k < nt; ++k) editClips (*tr[(size_t) k], [&] (std::vector<Clip>& c) { for (int i = 0; i < 8; ++i) if (sc[i][k] >= 0) c.push_back ({ sc[i][k], 8.0 * i, 8.0 }); });
    }
    else
    {
        // an eight-minute scaffold at 124 bpm: 16 sections of 8 bars (≈ 8:15), intro · build · main · break · main · outro
        const int secScene[16] = { 0, 0, 1, 2, 3, 4, 4, 6, 5, 5, 6, 4, 6, 3, 1, 0 };
        const char* secName[16] = { "intro", "intro 2", "rim in", "bass in", "clap", "main", "main 2", "main 3", "break", "break 2", "drop", "main 4", "main 5", "thin out", "outro", "end" };
        editScenes ([&] (std::vector<Scene>& s) { for (int i = 0; i < 8; ++i) { s[(size_t) i].name = names[i]; s[(size_t) i].pattern.assign ((size_t) nt, -2); for (int k = 0; k < nt; ++k) s[(size_t) i].pattern[(size_t) k] = sc[i][k]; } });
        editSong ([&] (std::vector<SongEntry>& s) { for (int i = 0; i < 16; ++i) s.push_back ({ secScene[i], 8 }); });
        editMarkers ([&] (std::vector<Marker>& m) { for (int i = 0; i < 16; ++i) m.push_back ({ secName[i], 8.0 * i }); });
        for (int k = 0; k < nt; ++k) editClips (*tr[(size_t) k], [&] (std::vector<Clip>& c) { for (int i = 0; i < 16; ++i) if (sc[secScene[i]][k] >= 0) c.push_back ({ sc[secScene[i]][k], 8.0 * i, 8.0 }); });
        mode.store ((int) PlayMode::Song);
        loopEnd.store (128);
    }
    // automation: the rim's dub send grows across the song, the filter macro opens in the break
    const double songLen = longForm ? 512.0 : 256.0;
    editLane (targetFor (*tr[1], *tr[1]->mixer, MP_SEND1), [=] (AutoLane& l) { l.pts = { { 0.0, 0.35f, 0.0f }, { songLen * 0.6, 0.75f, 0.0f }, { songLen, 0.5f, 0.0f } }; });
    editLane ({ 0, 0, GP_MACRO1 + 2 }, [=] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { songLen * 0.6, 0.0f, 0.0f }, { songLen * 0.72, 0.8f, 0.6f }, { songLen * 0.75, 0.0f, 0.0f } }; });
    for (auto* t : tr) { t->pattern.store (0); t->pendingPattern.store (0); t->pendingAt.store (0.0); }
    if (dub) setTempo (120.0f);
    if (half)
    {
        // the bass is where the track starts: everything else ducks under the kick through a sidechain on it
        setTempo (172.0f);
        if (auto d = createDevice ("st:sidechain")) { d->setBase (d->paramIndexForId ("shape"), 0.5f); d->setBase (d->paramIndexForId ("key"), 1.0f); addInsert (*tr[7], std::move (d)); }
        tr[7]->keyTrack.store (tr[0]->uid);
        selectedTrack.store (7);
    }
    if (halfStep) { setTempo (140.0f); global->setBase (GP_SWING, 0.0f); selectedTrack.store (7); }
    if (garage)
    {
        // shuffled hats, a skipping kick, and a sampler waiting for the vocal to cut up
        setTempo (132.0f); global->setBase (GP_SWING, 0.48f);
        auto* v = addTrack ("vocal"); setInstrument (*v, createDevice ("st:sampler"));
        if (v->instrument) { v->instrument->setBase (v->instrument->paramIndexForId ("mode"), 1.0f); v->instrument->setBase (v->instrument->paramIndexForId ("slicemode"), 1.0f); }
        v->mixer->setBase (MP_SEND1, 0.55f);
        setPattern (*v, 0, std::make_shared<const Pattern> ([] { Pattern p = pat ("x..x.x..x.x..x.x"); int k = 0; for (auto& st : p.s) if (st.on) { st.slice = k; k = (k + 3) % 8; st.len = 0.9f; } return p; }()));
        editScenes ([] (std::vector<Scene>& sc2) { for (size_t i = 2; i < 8; ++i) if (sc2[i].pattern.size() > 8) sc2[i].pattern[8] = 0; });
        v->pendingPattern.store (0); v->pendingAt.store (0.0);
    }
    if (! longForm) loopEnd.store (8);
    rebuildLatency();
    dirty = false;
}
} // namespace fal::daw

// faltung st0 studio — project files (xml), new / clear, and the starter templates.
#include "Session.h"

namespace fal::daw
{
static const int kFormatVersion = 1;

static void saveStep (juce::XmlElement& e, int i, const Step& s)
{
    auto* x = e.createNewChildElement ("s");
    x->setAttribute ("i", i);
    if (! s.on) x->setAttribute ("off", 1);
    x->setAttribute ("v", s.vel);
    if (s.pitch != 0) x->setAttribute ("p", s.pitch);
    if (s.prob < 0.999f) x->setAttribute ("pr", s.prob);
    if (s.ratchet > 1) x->setAttribute ("r", s.ratchet);
    if (s.micro != 0.0f) x->setAttribute ("m", s.micro);
    if (std::abs (s.len - 0.5f) > 1.0e-4f) x->setAttribute ("l", s.len);
    if (s.cond != Cond::Always) x->setAttribute ("c", (int) s.cond);
    if (s.slide) x->setAttribute ("sl", 1);
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
    s.micro = (float) x.getDoubleAttribute ("m", 0.0);
    s.len = (float) x.getDoubleAttribute ("l", 0.5);
    s.cond = (Cond) juce::jlimit (0, (int) Cond::Count - 1, x.getIntAttribute ("c", 0));
    s.slide = x.getIntAttribute ("sl", 0) != 0;
    int n = 0;
    for (auto* k : x.getChildWithTagNameIterator ("k"))
        if (n < kLocks) s.locks[(size_t) n++] = { k->getIntAttribute ("d"), k->getIntAttribute ("p"), (float) k->getDoubleAttribute ("v") };
    return s;
}

static void saveTrack (juce::XmlElement& e, const Track& t)
{
    e.setAttribute ("uid", t.uid); e.setAttribute ("name", t.name); e.setAttribute ("colour", juce::String::toHexString ((int) t.colour));
    e.setAttribute ("pattern", t.pattern.load()); e.setAttribute ("speed", t.speed.load()); e.setAttribute ("root", t.rootNote.load());
    e.setAttribute ("swing", t.swingOffset.load()); e.setAttribute ("shift", t.shiftMs.load()); e.setAttribute ("prob", t.probMul.load());
    e.setAttribute ("solo", t.solo.load() ? 1 : 0); e.setAttribute ("armed", t.armed.load() ? 1 : 0); e.setAttribute ("chainOn", t.chainOn.load() ? 1 : 0);
    e.setAttribute ("out", t.outPair.load());
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
        for (int s = 0; s < kMaxSteps; ++s) if (p->s[(size_t) s].on || p->s[(size_t) s].micro != 0.0f) saveStep (*x, s, p->s[(size_t) s]);
    }
    for (auto& c : *t.clips) { auto* x = e.createNewChildElement ("clip"); x->setAttribute ("pattern", c.pattern); x->setAttribute ("start", c.startBar); x->setAttribute ("bars", c.bars); }
    if (! t.chain->empty()) { juce::StringArray a; for (int p : *t.chain) a.add (juce::String (p)); e.createNewChildElement ("chain")->addTextElement (a.joinIntoString (" ")); }
    auto* m = e.createNewChildElement ("mod");
    for (auto& l : t.mod->lfo)
    {
        auto* x = m->createNewChildElement ("lfo");
        x->setAttribute ("shape", l.shape); x->setAttribute ("div", l.div); x->setAttribute ("sync", l.sync ? 1 : 0); x->setAttribute ("hz", l.hz);
        x->setAttribute ("phase", l.phase); x->setAttribute ("retrig", l.retrig ? 1 : 0); x->setAttribute ("skew", l.skew); x->setAttribute ("smooth", l.smooth);
    }
    { auto* x = m->createNewChildElement ("env"); x->setAttribute ("a", t.mod->env.attackMs); x->setAttribute ("d", t.mod->env.decayMs); }
    { auto* x = m->createNewChildElement ("steps"); x->setAttribute ("length", t.mod->steps.length); x->setAttribute ("div", t.mod->steps.div); x->setAttribute ("glide", t.mod->steps.glide);
      juce::StringArray a; for (auto v : t.mod->steps.v) a.add (juce::String (v, 4)); x->setAttribute ("v", a.joinIntoString (" ")); }
    for (auto& s : t.mod->slots)
    {
        auto* x = m->createNewChildElement ("slot");
        x->setAttribute ("src", s.src); x->setAttribute ("dev", s.device); x->setAttribute ("param", s.param); x->setAttribute ("amt", s.amount); x->setAttribute ("cc", s.cc);
    }
}

void Session::loadTrackXml (Track& t, const juce::XmlElement& e, juce::StringArray& problems)
{
    t.name = e.getStringAttribute ("name", t.name);
    t.colour = (juce::uint32) e.getStringAttribute ("colour", juce::String::toHexString ((int) t.colour)).getHexValue32();
    t.pattern.store (e.getIntAttribute ("pattern", 0)); t.speed.store (e.getIntAttribute ("speed", 3)); t.rootNote.store (e.getIntAttribute ("root", 60));
    t.swingOffset.store ((float) e.getDoubleAttribute ("swing", 0.0)); t.shiftMs.store ((float) e.getDoubleAttribute ("shift", 0.0)); t.probMul.store ((float) e.getDoubleAttribute ("prob", 1.0));
    t.solo.store (e.getIntAttribute ("solo", 0) != 0); t.armed.store (e.getIntAttribute ("armed", 0) != 0); t.chainOn.store (e.getIntAttribute ("chainOn", 0) != 0);
    t.outPair.store (e.getIntAttribute ("out", 0));
    const auto pre = e.getStringAttribute ("pre");
    for (int r = 0; r < kReturns; ++r) t.sendPre[(size_t) r].store (r < pre.length() && pre[r] == '1');
    if (auto* m = e.getChildByName ("mixer")) t.mixer->load (*m);
    auto make = [&] (const juce::XmlElement& x) -> std::unique_ptr<Device>
    {
        juce::String err;
        std::unique_ptr<Device> d;
        const auto type = x.getStringAttribute ("type");
        if (type.startsWith ("fal:")) d = createDevice (type, &err);
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
    for (auto* x : e.getChildWithTagNameIterator ("clip")) clips.push_back ({ x->getIntAttribute ("pattern"), x->getDoubleAttribute ("start"), x->getDoubleAttribute ("bars", 4.0) });
    editClips (t, [&] (std::vector<Clip>& c) { c = clips; });
    if (auto* c = e.getChildByName ("chain")) { std::vector<int> ch; for (auto& s : juce::StringArray::fromTokens (c->getAllSubText(), " ", "")) if (s.isNotEmpty()) ch.push_back (s.getIntValue()); setChain (t, ch); }
    if (auto* m = e.getChildByName ("mod"))
        editMod (t, [&] (ModDef& md)
        {
            int k = 0;
            for (auto* x : m->getChildWithTagNameIterator ("lfo"))
                if (k < kLfos)
                {
                    auto& l = md.lfo[(size_t) k++];
                    l.shape = x->getIntAttribute ("shape"); l.div = x->getIntAttribute ("div", 11); l.sync = x->getIntAttribute ("sync", 1) != 0; l.hz = (float) x->getDoubleAttribute ("hz", 1.0);
                    l.phase = (float) x->getDoubleAttribute ("phase"); l.retrig = x->getIntAttribute ("retrig") != 0; l.skew = (float) x->getDoubleAttribute ("skew", 0.5); l.smooth = (float) x->getDoubleAttribute ("smooth");
                }
            if (auto* x = m->getChildByName ("env")) { md.env.attackMs = (float) x->getDoubleAttribute ("a", 2.0); md.env.decayMs = (float) x->getDoubleAttribute ("d", 220.0); }
            if (auto* x = m->getChildByName ("steps"))
            {
                md.steps.length = x->getIntAttribute ("length", 16); md.steps.div = x->getIntAttribute ("div", 4); md.steps.glide = (float) x->getDoubleAttribute ("glide");
                auto a = juce::StringArray::fromTokens (x->getStringAttribute ("v"), " ", "");
                for (int i = 0; i < kStepMod && i < a.size(); ++i) md.steps.v[(size_t) i] = a[i].getFloatValue();
            }
            int s = 0;
            for (auto* x : m->getChildWithTagNameIterator ("slot"))
                if (s < kModSlots) { auto& sl = md.slots[(size_t) s++]; sl.src = x->getIntAttribute ("src"); sl.device = x->getIntAttribute ("dev"); sl.param = x->getIntAttribute ("param", -1); sl.amount = (float) x->getDoubleAttribute ("amt"); sl.cc = x->getIntAttribute ("cc", 1); }
        });
}

std::unique_ptr<juce::XmlElement> Session::save() const
{
    auto e = std::make_unique<juce::XmlElement> ("studio");
    e->setAttribute ("version", kFormatVersion);
    e->setAttribute ("suite", kDeep ? "deep" : "faltung");
    e->setAttribute ("name", projectName);
    e->setAttribute ("mode", mode.load()); e->setAttribute ("loop", loopOn.load() ? 1 : 0); e->setAttribute ("loopStart", loopStart.load()); e->setAttribute ("loopEnd", loopEnd.load());
    e->setAttribute ("micro", microMs.load()); e->setAttribute ("masterOut", masterOut.load()); e->setAttribute ("quant", launchQuant.load()); e->setAttribute ("selected", selectedTrack.load());
    e->setAttribute ("nextUid", nextUid);
    global->save (*e->createNewChildElement ("global"));
    for (int i = 0; i < kMacros; ++i) { auto* m = e->createNewChildElement ("macro"); m->setAttribute ("i", i); m->setAttribute ("name", macroNames[(size_t) i]); }
    for (auto& t : tracks) saveTrack (*e->createNewChildElement ("track"), *t);
    for (auto& r : returns) saveTrack (*e->createNewChildElement ("return"), *r);
    saveTrack (*e->createNewChildElement ("master"), *master);
    for (int i = 0; i < (int) scenes->size(); ++i)
    {
        const auto& sc = (*scenes)[(size_t) i];
        bool any = sc.name != juce::String (i + 1);
        for (int k = 0; k < (int) tracks.size(); ++k) any |= sc.pattern[(size_t) k] != -2;
        if (! any) continue;
        auto* x = e->createNewChildElement ("scene"); x->setAttribute ("i", i); x->setAttribute ("name", sc.name);
        juce::StringArray a; for (int k = 0; k < (int) tracks.size(); ++k) a.add (juce::String (sc.pattern[(size_t) k])); x->setAttribute ("p", a.joinIntoString (" "));
    }
    for (auto& s : *song) { auto* x = e->createNewChildElement ("song"); x->setAttribute ("scene", s.scene); x->setAttribute ("bars", s.bars); }
    for (auto& m : *markers) { auto* x = e->createNewChildElement ("marker"); x->setAttribute ("name", m.name); x->setAttribute ("bar", m.bar); }
    for (auto& l : *lanes)
    {
        auto* x = e->createNewChildElement ("lane"); x->setAttribute ("target", l->target.toString()); x->setAttribute ("on", l->enabled ? 1 : 0);
        for (auto& p : l->pts) { auto* q = x->createNewChildElement ("pt"); q->setAttribute ("b", p.beat); q->setAttribute ("v", p.value); if (p.curve != 0.0f) q->setAttribute ("c", p.curve); }
    }
    for (auto& m : *midiMaps) { auto* x = e->createNewChildElement ("midimap"); x->setAttribute ("cc", m.cc); x->setAttribute ("target", m.target.toString()); x->setAttribute ("lo", m.lo); x->setAttribute ("hi", m.hi); }
    return e;
}

void Session::clear()
{
    stop();
    while (! tracks.empty()) removeTrack ((int) tracks.size() - 1);
    for (auto& r : returns) { while (! r->inserts.empty()) removeDevice (*r, r->inserts.back()->uid); r->mixer = Device::makeMixer(); r->mod = std::make_shared<const ModDef>(); }
    while (! master->inserts.empty()) removeDevice (*master, master->inserts.back()->uid);
    { const juce::SpinLock::ScopedLockType sl (lock); master->mixer = Device::makeMixer(); global = Device::makeGlobal(); }
    if (prepared) { global->prepare (sr, bs); prepareTrack (*master); for (auto& r : returns) prepareTrack (*r); }
    for (int i = 0; i < kMacros; ++i) macroNames[(size_t) i] = "macro " + juce::String (i + 1);
    editScenes ([] (std::vector<Scene>& s) { s.assign ((size_t) kScenes, Scene()); for (int i = 0; i < kScenes; ++i) s[(size_t) i].name = juce::String (i + 1); });
    editSong ([] (std::vector<SongEntry>& s) { s.clear(); });
    editMarkers ([] (std::vector<Marker>& m) { m.clear(); });
    editMidiMaps ([] (std::vector<MidiMap>& m) { m.clear(); });
    { const juce::SpinLock::ScopedLockType sl (lock); lanes = std::make_shared<const std::vector<LanePtr>>(); }
    mode.store ((int) PlayMode::Session); loopOn.store (false); loopStart.store (0); loopEnd.store (8); microMs.store (12.0f); masterOut.store (1); launchQuant.store (1);
    selectedTrack.store (0); currentScene.store (-1); internalPpq.store (0.0);
    projectName = "untitled"; projectFile = juce::File();
    nextUid = 1;
    dirty = false;
}

bool Session::load (const juce::XmlElement& e, juce::String* error)
{
    if (! e.hasTagName ("studio")) { if (error) *error = "not a studio project"; return false; }
    clear();
    juce::StringArray problems;
    projectName = e.getStringAttribute ("name", "untitled");
    mode.store (e.getIntAttribute ("mode", 0)); loopOn.store (e.getIntAttribute ("loop", 0) != 0);
    loopStart.store (e.getDoubleAttribute ("loopStart", 0.0)); loopEnd.store (e.getDoubleAttribute ("loopEnd", 8.0));
    microMs.store ((float) e.getDoubleAttribute ("micro", 12.0)); masterOut.store (e.getIntAttribute ("masterOut", 1)); launchQuant.store (e.getIntAttribute ("quant", 1));
    if (auto* g = e.getChildByName ("global")) global->load (*g);
    for (auto* m : e.getChildWithTagNameIterator ("macro")) { const int i = m->getIntAttribute ("i", -1); if (i >= 0 && i < kMacros) macroNames[(size_t) i] = m->getStringAttribute ("name"); }
    for (auto* x : e.getChildWithTagNameIterator ("track"))
    {
        auto* t = addTrack (x->getStringAttribute ("name", "track"));
        if (! t) break;
        t->uid = x->getIntAttribute ("uid", t->uid);
        nextUid = std::max (nextUid, t->uid + 1);
        loadTrackXml (*t, *x, problems);
    }
    int r = 0;
    for (auto* x : e.getChildWithTagNameIterator ("return")) if (r < kReturns) loadTrackXml (*returns[(size_t) r++], *x, problems);
    if (auto* x = e.getChildByName ("master")) loadTrackXml (*master, *x, problems);
    editScenes ([&] (std::vector<Scene>& s)
    {
        for (auto* x : e.getChildWithTagNameIterator ("scene"))
        {
            const int i = x->getIntAttribute ("i", -1); if (i < 0 || i >= kScenes) continue;
            s[(size_t) i].name = x->getStringAttribute ("name", juce::String (i + 1));
            auto a = juce::StringArray::fromTokens (x->getStringAttribute ("p"), " ", "");
            for (int k = 0; k < kMaxTracks && k < a.size(); ++k) s[(size_t) i].pattern[(size_t) k] = a[k].getIntValue();
        }
    });
    editSong ([&] (std::vector<SongEntry>& s) { for (auto* x : e.getChildWithTagNameIterator ("song")) s.push_back ({ x->getIntAttribute ("scene"), x->getIntAttribute ("bars", 8) }); });
    editMarkers ([&] (std::vector<Marker>& m) { for (auto* x : e.getChildWithTagNameIterator ("marker")) m.push_back ({ x->getStringAttribute ("name"), x->getDoubleAttribute ("bar") }); });
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
    selectedTrack.store (juce::jlimit (-100, std::max (0, (int) tracks.size() - 1), e.getIntAttribute ("selected", 0)));
    nextUid = std::max (nextUid, e.getIntAttribute ("nextUid", nextUid));
    rebuildLatency();
    dirty = false;
    if (error) *error = problems.joinIntoString ("\n");
    return true;
}

// ---- templates ------------------------------------------------------------------------------------
juce::StringArray Session::templateNames() { return { "empty", kDeep ? "deep house starter" : "microhouse starter" }; }

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
} // namespace

void Session::newFromTemplate (int which)
{
    clear();
    if (which == 0)
    {
        auto* t = addTrack ("kick"); setInstrument (*t, createDevice ("fal:bd1"));
        projectName = "untitled";
        dirty = false;
        return;
    }
    projectName = kDeep ? "deep house starter" : "microhouse starter";
    struct Def { const char* name; const char* type; const char* preset; int root; };
    const std::vector<Def> defs = kDeep
        ? std::vector<Def> { { "kick", "fal:bd1", "deep round", 60 }, { "rim", "fal:rm3", "wooden rim", 60 }, { "hat", "fal:hh4", "warm hats", 60 }, { "open hat", "fal:hh4", "warm hats", 70 },
                             { "clap", "fal:cp6", "soft room clap", 60 }, { "shaker", "fal:hh4", "shaker", 60 }, { "conga", "fal:pc7", "conga low", 60 }, { "stabs", "fal:sb1", "", 48 } }
        : std::vector<Def> { { "kick", "fal:bd1", "tok", 60 }, { "rim", "fal:rm3", "tok", 60 }, { "hat", "fal:hh4", "808 hats", 60 }, { "open hat", "fal:hh4", "808 hats", 70 },
                             { "clap", "fal:cp6", "tight", 60 }, { "perc", "fal:pc7", "conga", 60 }, { "click", "fal:cl8", "tick", 60 }, { "bass", "fal:wb1", "sub pressure", 36 } };
    std::vector<Track*> tr;
    for (auto& d : defs)
    {
        auto* t = addTrack (d.name);
        auto dev = createDevice (d.type);
        if (dev && juce::String (d.preset).isNotEmpty()) { const int p = programNamed (*dev, d.preset); if (p >= 0) dev->setProgram (p); }
        setInstrument (*t, std::move (dev));
        t->rootNote.store (d.root);
        tr.push_back (t);
    }
    // the eight base patterns: kick rim hat open clap perc click bass
    const std::vector<Pattern> base = {
        pat ("X...x...X...x..."),
        pat ("..<...x...<...x>"),
        pat ("x.o.x.o.x.o.x.o?"),
        pat ("..x...x...x...x."),
        pat ("....X.......X..."),
        pat (".x..?.x....x.?..", 12),
        pat ("?...o.?.....o..?", 7),
        withPitch (pat ("x..x..x...x..x.."), { 0, 0, 7, 0, 3, 0 }, 0.9f),
    };
    const char* fills[8] = { "X...x...X...x.rr", "..<.r.x...<.x.x>", "x.o.x.o.x.o.xrrr", "..x...x...x...xx", "....X.......XXrr", ".x..x.x..x.x.x.x", "?.?.o.?.?.?.o.??", "x..x..x..xx.x.xx" };
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
        editClips (*tr[i], [] (std::vector<Clip>& c) { c.clear(); });
    }
    // returns: dub on 1, room on 2
    { auto d = createDevice ("fal:dl4"); if (d) { d->setBase (d->paramIndexForId ("division"), 9.0f / 18.0f); d->setBase (d->paramIndexForId ("feedback"), 0.5f); d->setBase (d->paramIndexForId ("mix"), 1.0f); addInsert (*returns[0], std::move (d)); } returns[0]->name = "dub"; }
    { auto d = createDevice ("fal:rv5"); if (d) { d->setBase (d->paramIndexForId ("model"), 2.0f / 3.0f); d->setBase (d->paramIndexForId ("mix"), 1.0f); d->setBase (d->paramIndexForId ("gate"), 0.45f); addInsert (*returns[1], std::move (d)); } returns[1]->name = "room"; }
    // master: filter (macro) → glue, and the mastering bus bypassed (Ableton masters)
    { auto d = createDevice ("fal:fl7"); if (d) { d->macro[(size_t) d->paramIndexForId ("sweep")].index.store (2); d->macro[(size_t) d->paramIndexForId ("sweep")].lo.store (0.5f); d->macro[(size_t) d->paramIndexForId ("sweep")].hi.store (0.95f); addInsert (*master, std::move (d)); } }
    { auto d = createDevice ("fal:gl8"); if (d) addInsert (*master, std::move (d)); }
    if (! kDeep) { auto d = createDevice ("fal:ms0"); if (d) { d->bypass.store (true); addInsert (*master, std::move (d)); } }
    // sends
    tr[1]->mixer->setBase (MP_SEND1, 0.55f); tr[4]->mixer->setBase (MP_SEND1 + 1, 0.6f); tr[6]->mixer->setBase (MP_SEND1, 0.35f); tr[5]->mixer->setBase (MP_SEND1 + 1, 0.4f);
    tr[3]->mixer->setBase (MP_LEVEL, 0.7f); tr[6]->mixer->setBase (MP_LEVEL, 0.65f); tr[7]->mixer->setBase (MP_LEVEL, 0.78f);
    tr[1]->mixer->setBase (MP_PAN, 0.42f); tr[5]->mixer->setBase (MP_PAN, 0.62f); tr[6]->mixer->setBase (MP_PAN, 0.35f);
    // macros
    macroNames[0] = "dub"; macroNames[1] = "room"; macroNames[2] = "filter"; macroNames[3] = "hats"; macroNames[4] = "bass cut";
    if (auto* d = returns[0]->inserts.empty() ? nullptr : returns[0]->inserts[0].get()) { const int fb = d->paramIndexForId ("feedback"); d->macro[(size_t) fb].index.store (0); d->macro[(size_t) fb].lo.store (0.3f); d->macro[(size_t) fb].hi.store (0.9f); }
    if (auto* d = returns[1]->inserts.empty() ? nullptr : returns[1]->inserts[0].get()) { const int dc = d->paramIndexForId ("decay"); d->macro[(size_t) dc].index.store (1); d->macro[(size_t) dc].lo.store (0.1f); d->macro[(size_t) dc].hi.store (0.7f); }
    { auto* d = tr[2]->instrument.get(); const int i = d->paramIndexForId ("decay"); d->macro[(size_t) i].index.store (3); d->macro[(size_t) i].lo.store (0.1f); d->macro[(size_t) i].hi.store (0.6f); }
    { auto* d = tr[7]->instrument.get(); const int i = d->paramIndexForId ("cutoff"); if (i >= 0) { d->macro[(size_t) i].index.store (4); d->macro[(size_t) i].lo.store (d->baseValue (i)); d->macro[(size_t) i].hi.store (0.85f); } }
    setMacro (0, 0.3f); setMacro (1, 0.5f); setMacro (2, 0.0f); setMacro (3, 0.4f); setMacro (4, 0.0f);
    // modulation: the hats breathe with a one-bar lfo, the click wanders in the stereo field, the rim's tune drifts a little
    editMod (*tr[2], [&] (ModDef& m) { m.lfo[0].div = 14; m.lfo[0].shape = 0; m.slots[0] = { MS_LFO1, tr[2]->instrument->uid, tr[2]->instrument->paramIndexForId ("tone"), 0.12f, 1 }; });
    editMod (*tr[6], [&] (ModDef& m) { m.slots[0] = { MS_RANDOM, 0, MP_PAN, 0.3f, 1 }; });
    editMod (*tr[1], [&] (ModDef& m) { m.lfo[1].shape = 9; m.lfo[1].div = 15; m.slots[0] = { MS_LFO2, tr[1]->instrument->uid, tr[1]->instrument->paramIndexForId ("tune"), 0.04f, 1 }; });
    // scenes: an eight-scene build, and the song that plays them
    const int sc[8][8] = { { 0, -1, 0, -1, -1, -1, 0, -1 }, { 0, 0, 0, 0, -1, -1, 0, -1 }, { 0, 0, 2, 0, -1, -1, 1, 0 }, { 2, 1, 2, 2, 0, 0, 2, 0 },
                           { 4, 4, 4, 4, 4, 4, 4, 4 }, { -1, 5, 5, 5, 6, 1, 5, 2 }, { 6, 6, 6, 6, 6, 6, 6, 6 }, { 7, 7, 7, 7, 7, 7, 7, 7 } };
    const char* names[8] = { "intro", "rim in", "bass in", "clap", "full", "break", "full 2", "fill out" };
    editScenes ([&] (std::vector<Scene>& s) { for (int i = 0; i < 8; ++i) { s[(size_t) i].name = names[i]; for (int k = 0; k < 8; ++k) s[(size_t) i].pattern[(size_t) k] = sc[i][k]; } });
    editSong ([] (std::vector<SongEntry>& s) { for (int i = 0; i < 8; ++i) s.push_back ({ i, 8 }); });
    editMarkers ([&] (std::vector<Marker>& m) { for (int i = 0; i < 8; ++i) m.push_back ({ names[i], 8.0 * i }); });
    for (int k = 0; k < 8; ++k)
        editClips (*tr[(size_t) k], [&] (std::vector<Clip>& c) { for (int i = 0; i < 8; ++i) if (sc[i][k] >= 0) c.push_back ({ sc[i][k], 8.0 * i, 8.0 }); });
    // automation: the rim's dub send grows across the song, the filter macro opens in the break
    editLane (targetFor (*tr[1], *tr[1]->mixer, MP_SEND1), [] (AutoLane& l) { l.pts = { { 0.0, 0.35f, 0.0f }, { 160.0, 0.75f, 0.0f }, { 256.0, 0.5f, 0.0f } }; });
    editLane ({ 0, 0, GP_MACRO1 + 2 }, [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 160.0, 0.0f, 0.0f }, { 184.0, 0.8f, 0.6f }, { 192.0, 0.0f, 0.0f } }; });
    for (auto* t : tr) { t->pattern.store (0); t->pendingPattern.store (0); t->pendingAt.store (0.0); }
    loopEnd.store (8);
    rebuildLatency();
    dirty = false;
}
} // namespace fal::daw

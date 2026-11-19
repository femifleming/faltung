#include "StudioEditor.h"
#include "StudioPages.h"

namespace fal::daw
{
/** a momentary button (fill): held while the mouse is down */
class HoldLink : public ui::TextLink
{
public:
    HoldLink (juce::String t, ui::Theme th, std::function<void (bool)> fn) : ui::TextLink (std::move (t), th), on (std::move (fn)) {}
    void mouseDown (const juce::MouseEvent&) override { on (true); setLit (true); }
    void mouseUp (const juce::MouseEvent&) override { on (false); setLit (false); }
    std::function<void (bool)> on;
};

class StudioView : public juce::Component
{
public:
    explicit StudioView (StudioProcessor& p)
        : proc (p), s (p.session), theme (studioTheme()),
          ctx { p, p.session, [this] { return selected(); }, [this] (Track* t) { select (t); }, [this] (int pg) { setPage (pg); }, [this] (Device& d) { openWindow (d); }, [this] (Track& t, Device& d) { removeDevice (t, d); }, [this] (const juce::String& m) { status = m; repaint (0, getHeight() - 20, getWidth(), 20); } },
          play ("play", theme), stop ("stop", theme), rec ("rec", theme), loop ("loop", theme), mode ("session", theme), outMode ("produce", theme), save ("save", theme), panic ("panic", theme), autoWrite ("write", theme), audio ("audio", theme), file ("file", theme), punch ("punch", theme), capture ("capture", theme),
          fill ("fill", theme, [this] (bool on) { s.fill.store (on); }),
          bpm (s, *s.master, *s.global, GP_TEMPO, "bpm", 34), swing (s, *s.master, *s.global, GP_SWING, "swing", 44), master (s, *s.master, *s.global, GP_MASTER, "master", 50)
    {
        pages.add (new SessionPage (ctx)); pages.add (new ArrangePage (ctx)); pages.add (new SeqPage (ctx)); pages.add (new MixPage (ctx)); pages.add (new ModPage (ctx)); pages.add (new BrowsePage (ctx));
        for (auto* pg : pages) addChildComponent (pg);
        const char* names[6] = { "session", "arrange", "seq", "mix", "mod", "browse" };
        for (int i = 0; i < 6; ++i) { auto* t = tabs.add (new ui::TextLink (names[i], theme)); t->onClick = [this, i] { setPage (i); }; addAndMakeVisible (t); }
        for (auto* l : std::initializer_list<juce::Component*> { &play, &stop, &rec, &loop, &mode, &outMode, &save, &panic, &autoWrite, &audio, &file, &punch, &capture, &fill, &bpm, &swing, &master }) addAndMakeVisible (l);
        capture.onClick = [this] { captureJam(); };
        capture.setTooltip ("capture: a new audio track records everything you play (the master) from now until you click again; cut the take up afterwards");
        // the tempo, the swing and the master level take modulators, lanes, controllers and keys like any other control
        for (auto* sl : { &bpm, &swing, &master }) sl->onMenu = [this] (DevSlider& x) { ParamMenu::show (s, *s.master, *s.global, x.index, &x, [this] (int) { select (s.master.get()); setPage (1); }); };
        file.onClick = [this] { fileMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&file), [this] (int r) { if (r > 0) fileAction (r); }); };
        punch.onClick = [this] { s.punch.store (! s.punch.load()); };
        punch.setTooltip ("punch: with the loop on, recording only happens inside the loop");
        autoWrite.setTooltip ("write: while playing, moving a control records automation");
        if (auto* mp = dynamic_cast<MixPage*> (pages[3])) mp->onAutoMaster = [this] { autoMaster(); };
        play.onClick = [this] { s.play(); };
        stop.onClick = [this] { if (s.isRecording()) s.stopRecording(); s.stop(); };
        rec.onClick = [this] { if (s.isRecording()) s.stopRecording(); else { s.startRecording(); if (! s.running.load()) s.play(); } };
        loop.onClick = [this] { s.loopOn.store (! s.loopOn.load()); };
        mode.onClick = [this] { s.mode.store ((s.mode.load() + 1) % 3); };
        outMode.onClick = [this] { s.multiOut.store (! s.multiOut.load()); };
        autoWrite.onClick = [this] { s.autoWrite.store (! s.autoWrite.load()); };
        panic.onClick = [this] { s.panic(); };
        audio.onClick = [this] { if (proc.openAudioSettings) proc.openAudioSettings(); };
        save.onClick = [this] { fileAction (2); };
        setWantsKeyboardFocus (true);
        setPage (0);
        rebuildAll();
        proc.onProjectReplaced = [this] { windows.clear(); select (nullptr); rebuildAll(); };
    }
    ~StudioView() override { proc.onProjectReplaced = nullptr; windows.clear(); }

    // ---- selection --------------------------------------------------------------------------------
    Track* selected()
    {
        if (selUid > 0) return s.trackByUid (selUid);
        if (selUid < 0) return s.trackByUid (selUid);
        return s.tracks.empty() ? nullptr : s.tracks[(size_t) juce::jlimit (0, (int) s.tracks.size() - 1, s.selectedTrack.load())].get();
    }
    void select (Track* t)
    {
        selUid = t ? t->uid : 0;
        if (t) { const int i = s.indexOf (t); s.selectedTrack.store (i == -1 ? 0 : i); }
        structure = -1;   // rebuild the pages that show the selection
    }
    void setPage (int pg)
    {
        status.clear();
        page = juce::jlimit (0, pages.size() - 1, pg);
        for (int i = 0; i < pages.size(); ++i) { pages[i]->setVisible (i == page); tabs[i]->setLit (i == page); }
        pages[page]->rebuild();
        resized();
    }
    int numPages() const { return pages.size(); }
    Page* pageAt (int i) { return pages[i]; }

    // ---- the file menu (the header's "file", and the app's menu bar) -------------------------------------
    juce::PopupMenu fileMenu()
    {
        juce::PopupMenu m, t; const auto names = Session::templateNames();
        for (int k = 0; k < names.size(); ++k) t.addItem (100 + k, names[k]);
        m.addSubMenu ("new from template", t);
        m.addItem (1, "open..."); m.addItem (2, "save"); m.addItem (3, "save as..."); m.addItem (6, "open the autosave"); m.addSeparator();
        m.addItem (7, "export master (wav, limited at -1 dbtp)..."); m.addItem (8, "export master (32-bit float, as it is)..."); m.addItem (4, "export stems..."); m.addItem (5, "export midi..."); m.addSeparator();
        double a, b; s.renderRange (a, b);
        const juce::String range = s.loopOn.load() ? "the loop, bars " + juce::String (a + 1, 0) + " - " + juce::String (b, 0) : juce::String (b - a, 0) + " bars";
        m.addItem (30, "render the project into a new audio track (" + range + ")");
        m.addItem (31, "the same, before the master chain");
        if (auto* t2 = selected()) if (! t2->isMaster()) { m.addItem (32, "bounce " + t2->name + " into a new audio track"); m.addItem (33, "bounce " + t2->name + " into a sampler (sliced)"); }
        return m;
    }
    void say (const juce::String& m) { status = m; repaint (0, getHeight() - 20, getWidth(), 20); }
    void fileAction (int id)
    {
        juce::String err;
        if (id >= 100 && id < 200) { windows.clear(); s.newFromTemplate (id - 100); select (nullptr); rebuildAll(); say ("new: " + Session::templateNames()[id - 100]); return; }
        auto choose = [this] (const juce::String& title, const juce::File& start, const juce::String& pattern, int flags, std::function<void (juce::File)> then)
        {
            chooser = std::make_unique<juce::FileChooser> (title, start, pattern);
            chooser->launchAsync (flags, [then] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) then (c.getResult()); });
        };
        const int saveFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
        const int openFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        switch (id)
        {
            case 1: choose ("open project", StudioProcessor::projectsDir(), "*.fstudio", openFlags, [this] (juce::File f) { juce::String e; windows.clear(); proc.loadProject (f, e); select (nullptr); say (e.isNotEmpty() ? e : "opened " + f.getFileName()); }); break;
            case 2: if (s.projectFile != juce::File()) { proc.saveProject (s.projectFile, err); say (err.isNotEmpty() ? err : "saved " + s.projectFile.getFileName()); break; } [[fallthrough]];
            case 3: choose ("save project", StudioProcessor::projectsDir().getChildFile (s.projectName + ".fstudio"), "*.fstudio", saveFlags, [this] (juce::File f) { juce::String e; proc.saveProject (f.withFileExtension ("fstudio"), e); say (e.isNotEmpty() ? e : "saved " + f.getFileName()); }); break;
            case 4: choose ("export stems into a folder", StudioProcessor::projectsDir(), {}, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (juce::File f)
                    { juce::String e; double a, b; s.renderRange (a, b); const auto dir = f.getChildFile (s.projectName + " stems"); s.renderStems (dir, a, b, nullptr, e); say (e.isNotEmpty() ? e : "stems in " + dir.getFullPathName()); }); break;
            case 5: choose ("export midi", StudioProcessor::projectsDir().getChildFile (s.projectName + ".mid"), "*.mid", saveFlags, [this] (juce::File f) { juce::String e; double a, b; s.renderRange (a, b); s.exportMidi (f.withFileExtension ("mid"), a, b, e); say (e.isNotEmpty() ? e : "midi written"); }); break;
            case 6: if (proc.autosaveFile().existsAsFile()) { windows.clear(); proc.loadProject (proc.autosaveFile(), err); select (nullptr); say (err.isNotEmpty() ? err : juce::String ("autosave opened")); } else say ("no autosave yet"); break;
            case 7: case 8:
                choose ("export master", StudioProcessor::projectsDir().getChildFile (s.projectName + ".wav"), "*.wav", saveFlags, [this, id] (juce::File f)
                { juce::String e; double a, b; s.renderRange (a, b); ExportOptions o; if (id == 7) o.limit = true; else { o.bits = 32; o.dither = false; } s.renderMaster (f.withFileExtension ("wav"), a, b, nullptr, e, o); say (e.isNotEmpty() ? e : "master written: " + f.getFileName()); }); break;
            case 30: case 31: renderProject (id == 31); break;
            case 32: case 33:
                if (auto* t = selected())
                {
                    double a, b; s.renderRange (a, b);
                    auto* made = id == 33 ? s.renderTrackToSampler (*t, a, b, err) : s.renderTrackToTrack (*t, a, b, err);
                    if (made) { select (made); say (t->name + " bounced into " + made->name); } else say (err);
                }
                break;
            default: break;
        }
    }
    /** resample the project (the loop when it is on) into a new audio track */
    Track* renderProject (bool preMaster)
    {
        double a, b; s.renderRange (a, b); juce::String err;
        auto* made = s.renderProjectToTrack (a, b, preMaster, err);
        if (made) { select (made); say ("project rendered (bars " + juce::String (a + 1, 0) + " - " + juce::String (b, 0) + ") into " + made->name); } else say (err.isNotEmpty() ? err : juce::String ("nothing rendered"));
        return made;
    }
    /** capture a jam: click to start recording the master into a new audio track, click again to stop */
    void captureJam()
    {
        if (capturing && s.isRecording()) { s.stopRecording(); capturing = false; say ("captured: the take is on the track \"" + (s.trackByUid (captureUid) ? s.trackByUid (captureUid)->name : juce::String ("jam")) + "\""); if (auto* t = s.trackByUid (captureUid)) { t->armed.store (false); t->toMaster.store (true); t->mixer->setBase (MP_MUTE, 1.0f); } return; }
        if (auto* t = s.captureJam()) { capturing = true; captureUid = t->uid; say ("capturing everything you play into \"" + t->name + "\" (click capture again to stop; the take comes back muted)"); }
        else say ("capture: already recording");
    }
    /** one click: ms0 on the master listens for twenty seconds of the track and sets itself once */
    bool autoMaster()
    {
        if (kDeep) { say ("ms0 is not part of faltung deep"); return false; }
        Device* ms = nullptr;
        for (auto& d : s.master->inserts) if (d->typeId == "fal:ms0") ms = d.get();
        if (! ms) ms = s.addInsert (*s.master, s.createDevice ("fal:ms0"));
        auto* fp = ms ? dynamic_cast<fal::Processor*> (ms->processor()) : nullptr;
        if (! fp) { say ("no room for ms0 on the master"); return false; }
        if (fp->autoListening()) { fp->autoStop(); return true; }
        ms->bypass.store (false); s.masterBypass.store (false); s.rebuildLatency();
        fp->autoStart (20.0);
        if (s.playPpq.load() < 0) s.play();
        autoDevice = ms->uid;
        say ("auto master: listening (20 s of sound; click again to decide early)");
        return true;
    }

    // ---- device windows -------------------------------------------------------------------------------
    void openWindow (Device& d)
    {
        for (auto& w : windows) if (w->device == &d) { w->toFront (true); return; }
        windows.push_back (std::make_unique<DeviceWindow> (d, [this, dp = &d] { juce::MessageManager::callAsync ([this, dp] { closeWindow (dp); }); }));
    }
    void closeWindow (Device* d) { windows.erase (std::remove_if (windows.begin(), windows.end(), [d] (auto& w) { return w->device == d; }), windows.end()); }
    void removeDevice (Track& t, Device& d)
    {
        closeWindow (&d);
        auto gone = s.removeDevice (t, d.uid);
        gone.reset();
        structure = -1;
    }

    // ---- structure watch: rebuild the pages when tracks / devices / selection change ----------------
    long long structureKey() const
    {
        long long k = (long long) s.epoch.load() * 1000000007LL + (long long) s.tracks.size() * 1000003 + (long long) s.returns.size() * 7919 + selUid * 31;
        for (auto& t : s.tracks) k = k * 31 + t->uid * 7 + t->numDevices() + (t->isAudio() ? 1 : 0);
        for (auto& r : s.returns) k = k * 31 + r->numDevices();
        k = k * 31 + s.master->numDevices();
        return k;
    }
    void rebuildAll()
    {
        // automation recording: every device reports gestures and edits
        auto wire = [this] (Track& t)
        {
            t.forEachDevice ([this, tp = &t] (Device& d)
            {
                Device* dp = &d;
                d.onGesture = [this, tp, dp] (int param, bool begin) { const Target tg = s.targetFor (*tp, *dp, param); if (begin) s.beginGesture (tg); else s.endGesture (tg); };
                d.onEdited = [this, tp, dp] (int param, float v) { if (! s.autoWrite.load() || s.playPpq.load() < 0) return; const Target tg = s.targetFor (*tp, *dp, param); if (s.inGesture (tg)) s.recordPoint (tg, v); };
            });
        };
        for (auto& t : s.tracks) wire (*t);
        for (auto& r : s.returns) wire (*r);
        wire (*s.master);
        // every page, the hidden ones too: a hidden page still holds sliders attached to the devices it showed, and
        // those must not outlive them (a late slider update or the page's own destructor would reach a dead device)
        for (auto* pg : pages) pg->rebuild();
        structure = structureKey();
    }
    void tick()
    {
        if (structureKey() != structure) rebuildAll();
        // drop windows of devices that vanished
        windows.erase (std::remove_if (windows.begin(), windows.end(), [this] (auto& w) { bool alive = false; auto chk = [&] (Track& t) { t.forEachDevice ([&] (Device& d) { if (&d == w->device) alive = true; }); }; for (auto& t : s.tracks) chk (*t); for (auto& r : s.returns) chk (*r); chk (*s.master); return ! alive; }), windows.end());
        pages[page]->tick();
        if (autoDevice != 0)
        {
            Device* ms = nullptr; for (auto& d : s.master->inserts) if (d->uid == autoDevice) ms = d.get();
            auto* fp = ms ? dynamic_cast<fal::Processor*> (ms->processor()) : nullptr;
            if (! fp) autoDevice = 0;
            else if (fp->autoReady()) { const auto summary = fp->applyAuto(); ms->pullBases(); autoDevice = 0; s.dirty = true; say ("auto master: " + (summary.isNotEmpty() ? summary : juce::String ("done"))); }
            else if (fp->autoListening()) say ("auto master: listening " + juce::String (juce::roundToInt (fp->autoProgress() * 100.0f)) + "% (click again to decide early)");
            else autoDevice = 0;
        }
        bpm.refresh(); swing.refresh(); master.refresh();
        repaint (0, 0, getWidth(), 48);
        repaint (0, getHeight() - 20, getWidth(), 20);
        // momentary keys
        const bool f = juce::KeyPress::isKeyCurrentlyDown ('f') && hasKeyboardFocus (true) && ! keyMapped ('f');
        if (f != keyFill) { keyFill = f; s.fill.store (f || fill.lit); }
        // keys held for a mapped control: released when they come up
        for (size_t i = 0; i < held.size();) { if (! juce::KeyPress::isKeyCurrentlyDown (held[i])) { s.keyEvent (held[i], false); held.erase (held.begin() + (long) i); } else ++i; }
    }

    // ---- layout and paint ------------------------------------------------------------------------------
    void resized() override
    {
        const int w = getWidth();
        int x = 150;
        for (auto* l : { &play, &stop, &rec }) { l->setBounds (x, 12, 46, 20); x += 48; }
        fill.setBounds (x, 12, 40, 20); x += 44;
        loop.setBounds (x, 12, 44, 20); x += 48;
        bpm.setBounds (x, 12, 156, 20); x += 166;
        swing.setBounds (x, 12, 136, 20); x += 144;
        mode.setBounds (x, 12, 70, 20); x += 74;
        outMode.setBounds (x, 12, 76, 20); x += 80;
        autoWrite.setBounds (x, 12, 50, 20); x += 54;
        master.setBounds (x, 12, 160, 20); x += 166;
        punch.setBounds (x, 12, 50, 20); x += 52; capture.setBounds (x, 12, 60, 20); x += 66;
        panic.setBounds (std::max (x, w - 240), 12, 50, 20); audio.setBounds (w - 188, 12, 50, 20); file.setBounds (w - 136, 12, 50, 20); save.setBounds (w - 82, 12, 50, 20);
        int tx = 12;
        for (auto* t : tabs) { t->setBounds (tx, 46, 70, 20); tx += 72; }
        for (auto* p : pages) p->setBounds (0, 70, w, getHeight() - 70 - 20);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        if (theme.mirror) { g.setColour (theme.ink.withAlpha (0.08f)); for (int y = 90; y < getHeight(); y += 90) g.fillRect (0, y, getWidth(), 1); }
        g.setColour (theme.ink); g.setFont (theme.mirror ? ui::cond (15.0f, true) : ui::sans (14.0f, true));
        g.drawText (juce::String (studioInfo().code), 12, 8, 120, 18, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.75f));
        g.drawText (s.projectName + (s.dirty.load() ? " *" : ""), 12, 26, 136, 14, juce::Justification::centredLeft, true);
        // position
        const double bar = s.currentBar();
        const int b = (int) std::floor (bar), beat = (int) std::floor ((bar - b) * 4.0);
        g.setFont (uiFont (11.0f, true)); g.setColour (s.playPpq.load() >= 0 ? juce::Colours::white : theme.ink);
        const double secs = bar * 4.0 * 60.0 / std::max (20.0, (double) s.bpmShown.load());
        g.drawText (juce::String (b + 1) + "." + juce::String (beat + 1) + "   " + juce::String ((int) secs / 60) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2) + "   " + juce::String (s.bpmShown.load(), 1) + " bpm", getWidth() - 330, 34, 260, 14, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.7f));
        juce::String info = juce::String (s.tracks.size()) + " tracks  " + juce::String (s.returns.size()) + " returns";
        info << ui::U ("  ·  latency ") << juce::String (s.totalLatency() * 1000.0 / s.sampleRate(), 1) << " ms" << ui::U ("  ·  cpu ") << juce::String (juce::roundToInt (s.cpu.load() * 100)) << "%";
        if (s.isRecording()) info << ui::U ("  ·  recording");
        if (s.learning.load()) info << ui::U ("  ·  midi learn: move a controller");
        if (s.keyLearning.load() != 0) info << ui::U ("  ·  press a key to map it");
        if (s.previewing()) info << ui::U ("  ·  auditioning");
        g.drawText (info, 150, 34, getWidth() - 490, 14, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (0, 68, getWidth(), 1);
        g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (uiFont (9.5f));
        g.drawText (status.isEmpty() ? ui::U ("space: play / stop · 1-8: scenes · shift+1-8: patterns of the selected track · m / s: mute / solo · f: fill · r: record · ↑↓: track · tab: next page") : status, 12, getHeight() - 18, getWidth() - 24, 14, juce::Justification::centredLeft, true);
        play.setLit (s.playPpq.load() >= 0); rec.setLit (s.isRecording()); loop.setLit (s.loopOn.load()); autoWrite.setLit (s.autoWrite.load()); punch.setLit (s.punch.load()); capture.setLit (capturing && s.isRecording());
        mode.setText (playModeName ((PlayMode) s.mode.load())); outMode.setText (s.multiOut.load() ? "multi-out" : "produce"); outMode.setLit (s.multiOut.load());
    }
    bool keyMapped (int code) const { for (auto& m : *s.keyMaps) if (m.key == code) return true; return false; }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto c = k.getTextCharacter();
        auto* t = selected();
        // a key that is being learned, or that plays a control, comes before the built-in keys
        if (s.keyLearning.load() != 0 || keyMapped (k.getKeyCode()))
        {
            if (std::find (held.begin(), held.end(), k.getKeyCode()) != held.end()) return true;   // auto-repeat while held
            if (s.keyEvent (k.getKeyCode(), true)) { held.push_back (k.getKeyCode()); return true; }
        }
        if (k.getKeyCode() == juce::KeyPress::spaceKey) { s.togglePlay(); return true; }
        if (k.getKeyCode() == juce::KeyPress::tabKey) { setPage ((page + 1) % pages.size()); return true; }
        if ((c == ',' || c == '.') && page == 1) if (auto* ap = dynamic_cast<ArrangePage*> (pages[1])) { ap->nudge (c == ',' ? -5.0 : 5.0); return true; }
        if (c >= '1' && c <= '8')
        {
            if (k.getModifiers().isShiftDown()) { if (t) s.launch (*t, c - '1'); }
            else s.launchScene (c - '1');
            return true;
        }
        if (c == 'm' && t) { t->mixer->setBase (MP_MUTE, t->muted() ? 0.0f : 1.0f); return true; }
        if (c == 's' && t) { t->solo.store (! t->solo.load()); return true; }
        if (c == 'r') { rec.onClick(); return true; }
        if (c == 'l') { s.loopOn.store (! s.loopOn.load()); return true; }
        if (c == 'f') { return true; }   // momentary, watched in tick()
        if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
        {
            const int i = juce::jlimit (0, std::max (0, (int) s.tracks.size() - 1), s.selectedTrack.load() + (k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1));
            if (! s.tracks.empty()) select (s.tracks[(size_t) i].get());
            return true;
        }
        if (k.getKeyCode() == juce::KeyPress::returnKey && t) { s.stopTrack (*t); return true; }
        return false;
    }
    void pageForTest (int pg) { setPage (pg); }

    StudioProcessor& proc; Session& s; ui::Theme theme; Ctx ctx;
    juce::OwnedArray<Page> pages; juce::OwnedArray<ui::TextLink> tabs; int page = 0;
    ui::TextLink play, stop, rec, loop, mode, outMode, save, panic, autoWrite, audio, file, punch, capture; HoldLink fill;
    DevSlider bpm, swing, master;
    std::vector<int> held; int autoDevice = 0; bool capturing = false; int captureUid = 0;
    std::vector<std::unique_ptr<DeviceWindow>> windows;
    std::unique_ptr<juce::FileChooser> chooser;
    int selUid = 0; long long structure = -1; bool keyFill = false; juce::String status;
};

StudioEditor::StudioEditor (StudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    root = std::make_unique<StudioView> (p);
    nominalW = studioInfo().w; nominalH = studioInfo().h;
    root->setBounds (0, 0, nominalW, nominalH);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (nominalW / 2, nominalH / 2, nominalW * 2, nominalH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) nominalW / nominalH);
    setSize (nominalW, nominalH);
    startTimerHz (30);
}
StudioEditor::~StudioEditor() { stopTimer(); root.reset(); }
void StudioEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (studioInfo().ground)); }
void StudioEditor::resized() { root->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) nominalW)); }
void StudioEditor::timerCallback() { root->tick(); }
void StudioEditor::showPage (int p) { root->pageForTest (p); }
int StudioEditor::numPages() const { return root->numPages(); }
void StudioEditor::tickForTest() { root->tick(); }
juce::PopupMenu StudioEditor::fileMenu() { return root->fileMenu(); }
void StudioEditor::fileAction (int id) { root->fileAction (id); }
Track* StudioEditor::renderProject (bool preMaster) { return root->renderProject (preMaster); }
void StudioEditor::captureJam() { root->captureJam(); }
bool StudioEditor::autoMaster() { return root->autoMaster(); }
juce::Component* StudioEditor::pageComponent (int i) { return root->pageAt (i); }
void StudioEditor::selectTrack (Track* t) { root->select (t); root->tick(); }
bool StudioEditor::key (const juce::KeyPress& k) { return root->keyPressed (k); }
} // namespace fal::daw

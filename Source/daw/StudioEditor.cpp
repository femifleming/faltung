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

/** the engine's load, always in the bottom right corner: a bar (smoothed), a tick at the highest load since the last
    reset, and the number of overruns (blocks that took longer than they last: the interface drops out). Click: reset. */
class CpuMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    CpuMeter (Session& se, ui::Theme th) : s (se), theme (th) { setTooltip ("cpu: the engine's load (time spent per block / the block's length). The tick is the highest since the last reset; a lit count means blocks ran over their time. Click: reset the peak and the count"); }
    void paint (juce::Graphics& g) override
    {
        const float load = s.cpu.load(), peak = s.cpuPeak.load(); const int over = s.overruns.load();
        auto r = getLocalBounds().toFloat();
        g.setFont (uiFont (9.5f, true)); g.setColour (theme.ink);
        g.drawText ("cpu", r.removeFromLeft (24.0f), juce::Justification::centredLeft);
        auto bar = r.removeFromLeft (64.0f).reduced (0.0f, 4.0f);
        g.setColour (theme.ink.withAlpha (0.18f)); g.fillRect (bar);
        const auto hot = juce::Colour (0xffee8e6e);
        g.setColour (load > 0.8f ? hot : theme.ink.withAlpha (0.85f)); g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, load)));
        g.setColour (peak > 0.8f ? hot : theme.ink); g.fillRect (bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, peak) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        r.removeFromLeft (5.0f);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink);
        g.drawText (juce::String (juce::roundToInt (load * 100)) + "%  pk " + juce::String (juce::roundToInt (peak * 100)) + "%", r.removeFromLeft (70.0f), juce::Justification::centredLeft);
        g.setColour (over > 0 ? hot : theme.ink.withAlpha (0.55f)); g.setFont (uiFont (9.5f, over > 0));
        g.drawText (juce::String (over) + " over", r, juce::Justification::centredLeft);
    }
    void mouseUp (const juce::MouseEvent&) override { s.resetCpuMeter(); repaint(); }
    Session& s; ui::Theme theme;
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
        addAndMakeVisible (cpuMeter);
        for (auto* l : std::initializer_list<juce::Component*> { &play, &stop, &rec, &loop, &mode, &outMode, &save, &panic, &autoWrite, &audio, &file, &punch, &capture, &fill, &bpm, &swing, &master }) addAndMakeVisible (l);
        capture.onClick = [this] { captureJam(); };
        capture.setTooltip ("capture: a new audio track records everything you play (the master) from now until you click again; cut the take up afterwards");
        // the tempo, the swing and the master level take modulators, lanes, controllers and keys like any other control
        for (auto* sl : { &bpm, &swing, &master }) sl->onMenu = [this] (DevSlider& x) { ParamMenu::show (s, *s.master, *s.global, x.index, &x, [this] (int) { select (s.master.get()); setPage (1); }); };
        file.onClick = [this] { auto m = fileMenu(); m.addSeparator(); for (juce::PopupMenu::MenuItemIterator it (editMenu()); it.next();) m.addItem (it.getItem()); m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&file), [this] (int r) { if (r > 0) fileAction (r); }); };
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
        const juce::String cmd = juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? "cmd" : "ctrl";
        m.addItem (1, "open...  (" + cmd + "+o)"); m.addItem (2, "save  (" + cmd + "+s)"); m.addItem (3, "save as...  (" + cmd + "+shift+s)"); m.addItem (6, "open the autosave"); m.addSeparator();
        m.addItem (7, "export audio (mix, stems, selected tracks, a time selection, multichannel)...  (" + cmd + "+e)"); m.addItem (5, "export midi..."); m.addSeparator();
        double a, b; s.renderRange (a, b);
        const juce::String range = s.loopOn.load() ? "the loop, bars " + juce::String (a + 1, 0) + " - " + juce::String (b, 0) : juce::String (b - a, 0) + " bars";
        m.addItem (30, "render the project into a new audio track (" + range + ")");
        m.addItem (31, "the same, before the master chain");
        if (auto* t2 = selected()) if (! t2->isMaster()) { m.addItem (32, "bounce " + t2->name + " into a new audio track"); m.addItem (33, "bounce " + t2->name + " into a sampler (sliced)"); }
        return m;
    }
    juce::PopupMenu editMenu()
    {
        const juce::String cmd = juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? "cmd" : "ctrl";
        juce::PopupMenu m;
        m.addItem (40, "undo  (" + cmd + "+z)" + (proc.history.canUndo() ? "   " + juce::String (proc.history.undoSteps()) + " steps" : juce::String()), proc.history.canUndo());
        m.addItem (41, "redo  (" + cmd + "+shift+z)", proc.history.canRedo());
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
        if (id == 40 || id == 41)
        {
            const int keep = selUid;
            const bool ok = id == 40 ? proc.history.undo() : proc.history.redo();
            if (ok) { if (auto* t = s.trackByUid (keep)) select (t); say (id == 40 ? "undo" : "redo"); }
            else say (id == 40 ? "nothing to undo" : "nothing to redo");
            return;
        }
        switch (id)
        {
            case 1: choose ("open project", StudioProcessor::projectsDir(), "*.fstudio", openFlags, [this] (juce::File f) { juce::String e; windows.clear(); proc.loadProject (f, e); select (nullptr); say (e.isNotEmpty() ? e : "opened " + f.getFileName()); }); break;
            case 2: if (s.projectFile != juce::File()) { proc.saveProject (s.projectFile, err); say (err.isNotEmpty() ? err : "saved " + s.projectFile.getFileName()); break; } [[fallthrough]];
            case 3: choose ("save project", StudioProcessor::projectsDir().getChildFile (s.projectName + ".fstudio"), "*.fstudio", saveFlags, [this] (juce::File f) { juce::String e; proc.saveProject (f.withFileExtension ("fstudio"), e); say (e.isNotEmpty() ? e : "saved " + f.getFileName()); }); break;
            case 5: choose ("export midi", StudioProcessor::projectsDir().getChildFile (s.projectName + ".mid"), "*.mid", saveFlags, [this] (juce::File f) { juce::String e; double a, b; s.renderRange (a, b); s.exportMidi (f.withFileExtension ("mid"), a, b, e); say (e.isNotEmpty() ? e : "midi written"); }); break;
            case 6: if (proc.autosaveFile().existsAsFile()) { windows.clear(); proc.recoverAutosave (err); select (nullptr); say (err.isNotEmpty() ? err : "autosave opened (from " + proc.autosaveFile().getLastModificationTime().formatted ("%H:%M") + "; unsaved)"); } else say ("no autosave yet"); break;
            case 7: exportDialog(); break;
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
    /** the export dialog: what (the mix, the mix before the master chain, stems of everything, stems of the tracks
        named), which bars (the project, the loop / time selection, bars typed), and the file (type, bits, rate,
        normalise, dither, the limiter for the mix). A master wider than stereo is written as one multichannel file. */
    void exportDialog()
    {
        auto* w = new juce::AlertWindow ("export audio", "the master is " + juce::String (s.master->width.load()) + " channels wide: the mix is written with that many (stems: each track at its own width)", juce::MessageBoxIconType::NoIcon);
        w->addComboBox ("what", { "the mix", "the mix before the master chain", "stems: every track, return and the master", "stems: the tracks numbered below" }, "export");
        const int sel = s.selectedTrack.load();
        w->addTextEditor ("tracks", juce::String (juce::jmax (0, sel) + 1), "track numbers (for the stems of chosen tracks), e.g. 1 3 4");
        double a = 0, b = 0; s.renderRange (a, b);
        const double pb = s.projectBars();
        w->addComboBox ("range", { "the whole project (bars 1 - " + juce::String (pb + 1, 0) + ")", "the loop / time selection (bars " + juce::String (s.loopStart.load() + 1, 2) + " - " + juce::String (s.loopEnd.load() + 1, 2) + ")", "the bars typed below" }, "range");
        w->getComboBoxComponent ("range")->setSelectedItemIndex (s.loopOn.load() ? 1 : 0);
        w->addTextEditor ("bars", "1 9", "from bar, to bar (the end is not included)");
        w->addComboBox ("format", { "wav", "aiff", "flac" }, "file");
        w->addComboBox ("bits", { "16 bit", "24 bit", "32 bit float (wav)" }, "word length"); w->getComboBoxComponent ("bits")->setSelectedItemIndex (1);
        w->addComboBox ("rate", { "the project's rate (" + juce::String (s.sampleRate(), 0) + " hz)", "44100 hz", "48000 hz", "88200 hz", "96000 hz" }, "sample rate");
        w->addComboBox ("norm", { "no normalising", "normalise to -1 dbfs", "normalise to -0.3 dbfs", "normalise to 0 dbfs" }, "normalise");
        w->addComboBox ("dither", { "dither (below 32 bit)", "no dither" }, "dither");
        w->addComboBox ("limit", { "no limiter", "true-peak limiter at -1 dbtp (the mix)" }, "limiter");
        for (auto* id : { "what", "format", "rate", "norm", "dither", "limit" }) if (w->getComboBoxComponent (id)->getSelectedItemIndex() < 0) w->getComboBoxComponent (id)->setSelectedItemIndex (0);
        w->addButton ("export...", 1); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            ExportOptions o;
            const int what = w->getComboBoxComponent ("what")->getSelectedItemIndex();
            o.format = w->getComboBoxComponent ("format")->getSelectedItemIndex();
            o.bits = std::array<int, 3> { 16, 24, 32 }[(size_t) juce::jlimit (0, 2, w->getComboBoxComponent ("bits")->getSelectedItemIndex())];
            o.sampleRate = std::array<double, 5> { 0, 44100, 48000, 88200, 96000 }[(size_t) juce::jlimit (0, 4, w->getComboBoxComponent ("rate")->getSelectedItemIndex())];
            const int nm = w->getComboBoxComponent ("norm")->getSelectedItemIndex();
            o.normalise = nm > 0; o.normaliseDb = nm == 1 ? -1.0f : nm == 2 ? -0.3f : 0.0f;
            o.dither = w->getComboBoxComponent ("dither")->getSelectedItemIndex() == 0;
            o.limit = w->getComboBoxComponent ("limit")->getSelectedItemIndex() == 1;
            double a = 0, b = s.projectBars();
            const int range = w->getComboBoxComponent ("range")->getSelectedItemIndex();
            if (range == 1) { a = s.loopStart.load(); b = s.loopEnd.load(); }
            else if (range == 2) { auto t = juce::StringArray::fromTokens (w->getTextEditorContents ("bars"), " ,-", ""); t.removeEmptyStrings(); a = std::max (0.0, t[0].getDoubleValue() - 1.0); b = std::max (a + 0.25, t[1].getDoubleValue() - 1.0); }
            if (what == 3)
                for (auto& n : juce::StringArray::fromTokens (w->getTextEditorContents ("tracks"), " ,", ""))
                    if (const int k = n.getIntValue() - 1; k >= 0 && k < (int) s.tracks.size()) o.only.push_back (s.tracks[(size_t) k]->uid);
            if (what == 3 && o.only.empty()) { say ("export: no such track numbers"); return; }
            const auto ext = juce::String (ExportOptions::extension (o.format));
            if (what <= 1)
                choose ("export the mix", StudioProcessor::projectsDir().getChildFile (s.projectName + ext), "*" + ext, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, o, a, b, what, ext] (juce::File f) { juce::String e; s.renderMaster (f.withFileExtension (ext), a, b, nullptr, e, o, what == 1); say (e.isNotEmpty() ? e : "written: " + f.withFileExtension (ext).getFileName()); });
            else
                choose ("export stems into a folder", StudioProcessor::projectsDir(), {}, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                        [this, o, a, b] (juce::File f) { juce::String e; const auto dir = f.getChildFile (s.projectName + " stems"); s.renderStems (dir, a, b, nullptr, e, o); say (e.isNotEmpty() ? e : "stems in " + dir.getFullPathName()); });
        }), true);
    }
    void choose (const juce::String& title, const juce::File& start, const juce::String& pattern, int flags, std::function<void (juce::File)> then)
    {
        chooser = std::make_unique<juce::FileChooser> (title, start, pattern);
        chooser->launchAsync (flags, [then] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) then (c.getResult()); });
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
                d.onGesture = [this, tp, dp] (int param, bool begin) { const Target tg = s.targetFor (*tp, *dp, param); if (begin) s.beginGesture (tg); else { s.endGesture (tg); s.dirty = true; } };
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
        repaint (0, getHeight() - 20, getWidth(), 20); cpuMeter.repaint();
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
        cpuMeter.setBounds (w - 222, getHeight() - 19, 214, 18);
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
        info << ui::U ("  ·  latency ") << juce::String (s.totalLatency() * 1000.0 / s.sampleRate(), 1) << " ms" ;
        if (s.isRecording()) info << ui::U ("  ·  recording");
        if (s.learning.load()) info << ui::U ("  ·  midi learn: move a controller");
        if (s.keyLearning.load() != 0) info << ui::U ("  ·  press a key to map it");
        if (s.previewing()) info << ui::U ("  ·  auditioning");
        g.drawText (info, 150, 34, getWidth() - 490, 14, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (0, 68, getWidth(), 1);
        g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (uiFont (9.5f));
        g.drawText (status.isEmpty() ? ui::U ("space: play / stop · cmd+z / cmd+shift+z: undo / redo · 1-8: scenes · shift+1-8: patterns of the selected track · m / s: mute / solo · f: fill · r: record · ↑↓: track · tab: next page") : status, 12, getHeight() - 18, getWidth() - 24 - 230, 14, juce::Justification::centredLeft, true);
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
        // the usual keys of a document application
        if (k.getModifiers().isCommandDown())
        {
            const int kc = k.getKeyCode(); const bool sh = k.getModifiers().isShiftDown();
            const int low = (kc >= 'A' && kc <= 'Z') ? kc + 32 : kc;
            if (low == 'z') { fileAction (sh ? 41 : 40); return true; }
            if (low == 'y') { fileAction (41); return true; }
            if (low == 's') { fileAction (sh ? 3 : 2); return true; }
            if (low == 'o') { fileAction (1); return true; }
            if (low == 'n') { fileAction (100); return true; }
            if (low == 'e') { fileAction (7); return true; }
            return false;
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
        if (c == 'm' && t) { t->mixer->setBase (MP_MUTE, t->muted() ? 0.0f : 1.0f); s.dirty = true; return true; }
        if (c == 's' && t) { t->solo.store (! t->solo.load()); s.dirty = true; return true; }
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
    CpuMeter cpuMeter { s, theme };
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
juce::PopupMenu StudioEditor::editMenu() { return root->editMenu(); }
void StudioEditor::fileAction (int id) { root->fileAction (id); }
Track* StudioEditor::renderProject (bool preMaster) { return root->renderProject (preMaster); }
void StudioEditor::captureJam() { root->captureJam(); }
bool StudioEditor::autoMaster() { return root->autoMaster(); }
juce::Component* StudioEditor::pageComponent (int i) { return root->pageAt (i); }
void StudioEditor::selectTrack (Track* t) { root->select (t); root->tick(); }
bool StudioEditor::key (const juce::KeyPress& k) { return root->keyPressed (k); }
} // namespace fal::daw

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
        const auto hot = hotColour();
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

/** every keyboard shortcut (the sheet on ?, and the manual) */
inline const std::vector<std::pair<juce::String, juce::String>>& shortcutList()
{
    static const std::vector<std::pair<const char*, const char*>> raw {
        { "space", "play / stop" }, { "shift+space", "play from the selection (or the loop's start)" }, { "/", "back to where playing last started" }, { "r or *", "record" },
        { "l", "loop on / off" }, { "f (hold)", "fill" }, { "1 - 8", "launch a scene" }, { "shift+1 - 8", "a pattern of the selected track" }, { "m · s", "mute · solo the selected track" },
        { "up · down", "the track above / below" }, { "tab", "the next page" }, { "enter", "stop the selected track" }, { "?", "this sheet" },
        { "cmd+z · cmd+shift+z", "undo · redo" }, { "cmd+alt+z", "the undo history" }, { "cmd+s · cmd+shift+s", "save · save as" }, { "cmd+o · cmd+n", "open · new" }, { "cmd+e", "export" },
        { "cmd+f", "find tracks, clips, markers, devices" }, { "cmd+b", "the browser" }, { "cmd+= · cmd+- · cmd+0", "everything bigger · smaller · actual size" },
        { "cmd+x · cmd+c · cmd+v", "cut · copy · paste clips" }, { "cmd+d", "duplicate the clips" }, { "cmd+t", "split at the playhead" }, { "delete", "delete the clips" },
        { "cmd+j", "join the clips" }, { "cmd+u", "quantise the clips to the grid" }, { "cmd+i", "rename the clip (or the track)" }, { "cmd+shift+m", "mute / unmute the clips" },
        { "cmd+a", "select every clip" }, { "cmd+l", "loop the selection" }, { "cmd+r", "render the time selection in place" }, { "cmd+shift+r", "reverse the time selection" },
        { "cmd+alt+backspace", "silence the time selection" }, { "cmd+shift+i", "insert time" }, { "cmd+shift+backspace", "delete the looped time" },
        { "cmd+k", "a marker at the playhead" }, { "[ · ]", "the marker before / after" }, { "home · end", "the start · the end" }, { "cmd+g", "go to a position" },
        { "alt+1 - 9", "go to a locator" }, { "alt+shift+1 - 9", "keep this place as a locator" },
        { "cmd+shift+t · cmd+alt+t", "new instrument · audio track" }, { "cmd+shift+d", "duplicate the track" }, { "cmd+alt+f", "freeze / unfreeze the track" },
        { "left · right", "the playhead or the clips by the grid" }, { "cmd+left · right", "the clips by a tick" }, { "alt+left · right", "the clips by a millisecond" },
        { ", · .", "nudge the clips by 5 ms" }, { "alt+up · down", "the take before / after" },
        { "= · -", "zoom in · out (where the mouse is)" }, { "z · shift+z", "zoom to the selection · the whole project" }, { "n", "snap on / off" },
        { "cmd+1 · cmd+2", "the grid finer · coarser" }, { "cmd+3", "the grid triplet / straight" }, { "cmd+4", "snap on / off" },
        { "esc", "deselect; close an editor" }, { "note editor: q", "quantise the notes" }, { "note editor: up · down", "transpose (shift: an octave)" }, { "note editor: cmd+a", "every note" },
    };
    // (the source is utf-8: the dots and arrows read as such)
    static const std::vector<std::pair<juce::String, juce::String>> v = [] { std::vector<std::pair<juce::String, juce::String>> o; for (auto& [k, w] : raw) o.push_back ({ juce::String::fromUTF8 (k), juce::String::fromUTF8 (w) }); return o; }();
    return v;
}

/** search: tracks, clips (their names, their files), markers, locators and devices, by part of a name */
class SearchPanel : public juce::Component
{
public:
    struct Hit { int kind; juce::String text; int trackUid = 0; ClipRef clip; double bar = -1; };   // kind 0 track 1 clip 2 marker 3 locator 4 device
    SearchPanel (Session& s, std::function<void (const Hit&)> go) : session (s), onGo (std::move (go)), theme (studioTheme())
    {
        addAndMakeVisible (box); box.setTextToShowWhenEmpty ("a name, part of one, a file ...", theme.ink.withAlpha (0.5f));
        box.setColour (juce::TextEditor::backgroundColourId, theme.ground); box.setColour (juce::TextEditor::textColourId, theme.ink); box.setColour (juce::TextEditor::outlineColourId, theme.ink.withAlpha (0.4f));
        box.setFont (uiFont (12.0f));
        box.onTextChange = [this] { find(); };
        box.onReturnKey = [this] { if (! hits.empty() && onGo) onGo (hits[(size_t) std::max (0, chosen)]); };
        box.onEscapeKey = [this] { if (auto* c = findParentComponentOfClass<juce::CallOutBox>()) c->dismiss(); };
        setSize (460, 380); find();
    }
    void visibilityChanged() override { if (isVisible()) box.grabKeyboardFocus(); }
    void find()
    {
        hits.clear(); chosen = 0;
        const auto q = box.getText().trim().toLowerCase();
        auto has = [&] (const juce::String& x) { return q.isEmpty() || x.toLowerCase().contains (q); };
        for (auto& tp : session.tracks)
        {
            auto& t = *tp;
            if (has (t.name)) hits.push_back ({ 0, "track  " + t.name, t.uid });
            for (auto& c : *t.clips) { const auto nm = c.name.isNotEmpty() ? c.name : "p" + juce::String (c.pattern + 1); if (q.isNotEmpty() && has (nm)) hits.push_back ({ 1, "clip  " + nm + "  on " + t.name + "  at " + session.formatRuler (session.rulerMain.load(), c.startBar * 4.0), t.uid, { t.uid, c.id, false }, c.startBar }); }
            for (auto& c : *t.aclips) { const auto nm = c.name.isNotEmpty() ? c.name : juce::File (c.path).getFileNameWithoutExtension(); if (q.isNotEmpty() && (has (nm) || has (juce::File (c.path).getFileName()))) hits.push_back ({ 1, "audio  " + nm + "  on " + t.name + "  at " + session.formatRuler (session.rulerMain.load(), c.startBar * 4.0), t.uid, { t.uid, c.id, true }, c.startBar }); }
            t.forEachDevice ([&] (Device& d) { if (q.isNotEmpty() && d.uid != 0 && has (d.name)) hits.push_back ({ 4, "device  " + d.name + "  on " + t.name, t.uid }); });
        }
        for (auto& m : *session.markers) if (has (m.name)) hits.push_back ({ 2, "marker  " + m.name + "  at " + session.formatRuler (session.rulerMain.load(), m.bar * 4.0), 0, {}, m.bar });
        for (auto& L : session.locators) if (L.set && has (L.name)) hits.push_back ({ 3, "locator  " + L.name + "  at " + session.formatRuler (session.rulerMain.load(), L.bar * 4.0), 0, {}, L.bar });
        if (hits.size() > 200) hits.resize (200);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        g.setFont (uiFont (10.5f));
        for (int i = 0; i < (int) hits.size() && 40 + i * 20 < getHeight(); ++i)
        {
            if (i == chosen) { g.setColour (theme.fill); g.fillRect (4, 40 + i * 20, getWidth() - 8, 20); g.setColour (ui::inkFor (theme.fill)); } else g.setColour (theme.ink);
            g.drawText (hits[(size_t) i].text, 10, 40 + i * 20, getWidth() - 20, 20, juce::Justification::centredLeft, true);
        }
        if (hits.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("nothing by that name", 10, 40, getWidth() - 20, 20, juce::Justification::centredLeft); }
    }
    void resized() override { box.setBounds (6, 6, getWidth() - 12, 26); }
    void mouseDown (const juce::MouseEvent& e) override { const int i = (e.y - 40) / 20; if (e.y >= 40 && i < (int) hits.size()) { chosen = i; if (onGo) onGo (hits[(size_t) i]); } }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::downKey) { chosen = std::min ((int) hits.size() - 1, chosen + 1); repaint(); return true; }
        if (k.getKeyCode() == juce::KeyPress::upKey) { chosen = std::max (0, chosen - 1); repaint(); return true; }
        return false;
    }
    std::vector<Hit> hits; int chosen = 0; juce::TextEditor box;
private:
    Session& session; std::function<void (const Hit&)> onGo; ui::Theme theme;
};

/** every step of the history: click one to go to it (back or forward) */
class HistoryPanel : public juce::Component, private juce::Timer
{
public:
    HistoryPanel (UndoHistory& h, std::function<void (const juce::String&)> s) : history (h), say (std::move (s)), theme (studioTheme())
    {
        viewport.setViewedComponent (&list, false); viewport.setScrollBarsShown (true, false); addAndMakeVisible (viewport);
        list.paintFn = [this] (juce::Graphics& g) { paintList (g); };
        list.clickFn = [this] (int y) { const auto e = history.entries(); const int i = y / rowH; if (i >= 0 && i < (int) e.size() && ! e[(size_t) i].now) { history.jumpTo (e[(size_t) i].index); say ("history: " + e[(size_t) i].label); refresh(); } };
        setSize (420, 420); refresh(); startTimerHz (4);
    }
    struct List : juce::Component { std::function<void (juce::Graphics&)> paintFn; std::function<void (int)> clickFn; void paint (juce::Graphics& g) override { if (paintFn) paintFn (g); } void mouseDown (const juce::MouseEvent& e) override { if (clickFn) clickFn (e.y); } };
    static constexpr int rowH = 20;
    void refresh() { const auto n = (int) history.entries().size(); list.setSize (viewport.getWidth() - 10, std::max (viewport.getHeight(), n * rowH)); list.repaint(); repaint(); }
    void paintList (juce::Graphics& g)
    {
        const auto e = history.entries();
        g.setFont (uiFont (10.0f));
        for (int i = 0; i < (int) e.size(); ++i)
        {
            const bool past = ! e[(size_t) i].now && e[(size_t) i].index < (int) std::count_if (e.begin(), e.end(), [] (auto& x) { return ! x.now; }) && [&] { for (auto& x : e) if (x.now) return e[(size_t) i].index < x.index; return false; }();
            if (e[(size_t) i].now) { g.setColour (theme.fill); g.fillRect (0, i * rowH, list.getWidth(), rowH); g.setColour (ui::inkFor (theme.fill)); }
            else g.setColour (theme.ink.withAlpha (past ? 0.9f : 0.45f));
            g.drawText (juce::String (i + 1) + ".  " + e[(size_t) i].label + (e[(size_t) i].now ? "   (now)" : juce::String()), 8, i * rowH, list.getWidth() - 16, rowH, juce::Justification::centredLeft, true);
        }
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme.ground); g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText ("undo history: click a step to go to it (greyed: undone, can be redone)", 8, 4, getWidth() - 16, 18, juce::Justification::centredLeft); }
    void resized() override { viewport.setBounds (4, 26, getWidth() - 8, getHeight() - 30); refresh(); }
private:
    void timerCallback() override { refresh(); }
    UndoHistory& history; std::function<void (const juce::String&)> say; ui::Theme theme;
    juce::Viewport viewport; List list;
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
        addAndMakeVisible (cpuMeter); addAndMakeVisible (click); addAndMakeVisible (countIn);
        for (auto* l : std::initializer_list<juce::Component*> { &play, &stop, &rec, &loop, &mode, &outMode, &save, &panic, &autoWrite, &audio, &file, &punch, &capture, &fill, &bpm, &swing, &master }) addAndMakeVisible (l);
        capture.onClick = [this] { captureJam(); };
        capture.setTooltip ("capture: a new audio track records everything you play (the master) from now until you click again; cut the take up afterwards");
        // the tempo, the swing and the master level take modulators, lanes, controllers and keys like any other control
        for (auto* sl : { &bpm, &swing, &master }) sl->onMenu = [this] (DevSlider& x) { ParamMenu::show (s, *s.master, *s.global, x.index, &x, [this] (int) { select (s.master.get()); setPage (1); }); };
        file.onClick = [this] { auto m = fileMenu(); m.addSeparator(); m.addSubMenu ("edit", editMenu()); m.addSubMenu ("view", viewMenu()); m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&file), [this] (int r) { if (r > 0) fileAction (r); }); };
        punch.onClick = [this] { s.punch.store (! s.punch.load()); };
        punch.setTooltip ("punch: with the loop on, recording only happens inside the loop");
        autoWrite.setTooltip ("write: while playing, moving a control records automation");
        if (auto* mp = dynamic_cast<MixPage*> (pages[3])) mp->onAutoMaster = [this] { autoMaster(); };
        play.onClick = [this] { s.play(); };
        stop.onClick = [this] { if (s.isRecording()) s.stopRecording(); s.stop(); };
        rec.onClick = [this] { if (s.isRecording()) s.stopRecording(); else s.recordWithCountIn(); };
        rec.setTooltip ("record: audio into armed audio tracks, notes from your midi keyboard into the steps of armed instrument tracks (from a stop, after the count-in)");
        click.onClick = [this] { s.metronome.store (! s.metronome.load()); s.dirty = true; };
        click.setTooltip ("the metronome: a click on every beat while playing");
        countIn.onClick = [this] { s.countInBars.store ((s.countInBars.load() + 1) % 3); s.dirty = true; };
        countIn.setTooltip ("bars of clicks before recording starts from a stop (none, 1 or 2)");
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
        proc.onProjectReplaced = [this] { windows.clear(); select (nullptr); rebuildAll(); s.freeRetired(); };
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
        m.addItem (1, "open...  (" + cmd + "+o)"); m.addItem (2, "save  (" + cmd + "+s)"); m.addItem (3, "save as...  (" + cmd + "+shift+s)"); m.addItem (6, "open the autosave");
        {
            juce::PopupMenu bk; backupList = s.projectFile != juce::File() ? StudioProcessor::backups (s.projectFile) : juce::Array<juce::File>();
            for (int k = 0; k < backupList.size(); ++k) bk.addItem (600 + k, backupList[k].getFileNameWithoutExtension().fromLastOccurrenceOf (s.projectFile.getFileNameWithoutExtension() + " ", false, false).replace (" ", "  ").replaceCharacter ('-', '-'));
            m.addSubMenu ("restore a backup", bk, ! backupList.isEmpty());
        }
        m.addItem (9, "collect all and save (every file into the project's folder)");
        { const auto ms = s.missing(); m.addItem (8, "find missing files..." + (ms.empty() ? juce::String() : "  (" + juce::String (ms.audio.size()) + " audio, " + juce::String (ms.plugins.size()) + " plug-ins)")); }
        m.addSeparator();
        m.addItem (7, "export audio (mix, stems, selected tracks, a time selection, multichannel)...  (" + cmd + "+e)"); m.addItem (5, "export midi..."); m.addSeparator();
        double a, b; s.renderRange (a, b);
        const juce::String range = s.loopOn.load() ? "the loop, " + timeline::formatPosition (a * 4.0, s.meterMap()) + " - " + timeline::formatPosition (b * 4.0, s.meterMap()) : juce::String (s.meterMap().barsBetween (a * 4.0, b * 4.0), 0) + " bars";
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
        m.addItem (46, "undo history...  (" + cmd + "+alt+z)");
        m.addSeparator();
        auto* ap = arrange();
        const bool any = ap && ! ap->sel.empty();
        m.addItem (51, "cut clips  (" + cmd + "+x)", any); m.addItem (50, "copy clips  (" + cmd + "+c)", any);
        m.addItem (52, "paste at the playhead  (" + cmd + "+v)", ap && ! ap->board.empty());
        m.addItem (53, "duplicate  (" + cmd + "+d)", any); m.addItem (56, "delete clips  (delete)", any);
        m.addItem (55, "split at the playhead  (" + cmd + "+t)"); m.addItem (57, "mute / unmute clips  (" + cmd + "+shift+m)", any);
        m.addItem (54, "select all clips  (" + cmd + "+a)");
        m.addItem (81, "join the selected clips  (" + cmd + "+j)", any); m.addItem (85, "quantise the selected clips to the grid  (" + cmd + "+u)", any);
        m.addItem (92, "rename the clip or track  (" + cmd + "+i)");
        m.addSeparator();
        m.addItem (82, "render the time selection in place  (" + cmd + "+r)"); m.addItem (83, "reverse the time selection  (" + cmd + "+shift+r)"); m.addItem (84, "silence the time selection  (" + cmd + "+alt+backspace)");
        m.addSeparator();
        m.addItem (86, "a marker at the playhead  (" + cmd + "+k)");
        m.addItem (87, "new instrument track  (" + cmd + "+shift+t)"); m.addItem (88, "new audio track  (" + cmd + "+alt+t)"); m.addItem (89, "duplicate the track  (" + cmd + "+shift+d)");
        m.addItem (90, "freeze / unfreeze the track  (" + cmd + "+alt+f)"); m.addItem (91, "find...  (" + cmd + "+f)"); m.addItem (98, "keyboard shortcuts...  (?)");
        m.addSeparator();
        m.addItem (60, "loop the selected clips  (" + cmd + "+l)", any);
        m.addItem (58, "insert time at the playhead...  (" + cmd + "+shift+i)");
        m.addItem (59, "delete the looped time  (" + cmd + "+shift+backspace)", s.loopOn.load());
        m.addItem (61, "go to position...  (" + cmd + "+g)");
        m.addItem (62, "time signature at the playhead's bar...  (now " + s.meterMap().sigAt (s.currentBar() * 4.0).text() + ")");
        return m;
    }
    juce::PopupMenu viewMenu()
    {
        const juce::String cmd = juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? "cmd" : "ctrl";
        juce::PopupMenu m;
        m.addItem (70, "bigger  (" + cmd + "+=)"); m.addItem (71, "smaller  (" + cmd + "+-)"); m.addItem (72, "actual size  (" + cmd + "+0)");
        m.addSeparator();
        auto* ap = arrange();
        m.addItem (73, "zoom in on the timeline  (=)"); m.addItem (74, "zoom out  (-)");
        m.addItem (76, "zoom to the selection / the loop  (z)"); m.addItem (75, "the whole project  (shift+z)");
        m.addItem (77, "taller rows  (alt + scroll)"); m.addItem (78, "shorter rows");
        m.addSeparator();
        m.addItem (79, "follow the playhead", true, s.follow.load());
        m.addItem (80, "snap  (n)", true, s.snap.load());
        juce::PopupMenu gm; const auto& g = timeline::grids();
        for (int i = 0; i < (int) g.size(); ++i) gm.addItem (300 + i, i == 0 && ap ? timeline::gridName (0, ap->pxPerBeat()) : juce::String (g[(size_t) i].name), true, s.gridIndex.load() == i);
        m.addSubMenu ("grid", gm);
        m.addSeparator();
        juce::PopupMenu th; const auto& pal = studioPalettes();
        for (int i = 0; i < (int) pal.size(); ++i) th.addItem (400 + i, pal[(size_t) i].name, true, studioThemeIndex() == i);
        m.addSubMenu ("colour theme", th);
        {
            juce::PopupMenu lc;
            for (int i = 0; i < 9; ++i) { const auto& L = s.locators[(size_t) i]; lc.addItem (700 + i, (L.set ? L.name + "  " + s.formatRuler (s.rulerMain.load(), L.bar * 4.0) : juce::String ("(empty)")) + "  (alt+" + juce::String (i + 1) + ")", L.set); }
            lc.addSeparator(); juce::PopupMenu st; for (int i = 0; i < 9; ++i) st.addItem (720 + i, juce::String (i + 1) + "  (alt+shift+" + juce::String (i + 1) + ")"); lc.addSubMenu ("keep this place as", st);
            juce::PopupMenu rn; for (int i = 0; i < 9; ++i) rn.addItem (740 + i, juce::String (i + 1), s.locators[(size_t) i].set); lc.addSubMenu ("rename", rn);
            m.addSubMenu ("locators", lc);
        }
        m.addSeparator();
        juce::PopupMenu r1, r2, fps, tb;
        for (int f = 0; f < 4; ++f) r1.addItem (460 + f, timeline::rulerName (f), true, s.rulerMain.load() == f);
        for (int f = 0; f < 5; ++f) r2.addItem (470 + f, timeline::rulerName (f), true, s.rulerSub.load() == f);
        const float rates[] = { 24.0f, 25.0f, 29.97f, 30.0f, 50.0f, 60.0f };
        for (int k = 0; k < 6; ++k) fps.addItem (480 + k, juce::String (rates[k], rates[k] == 29.97f ? 2 : 0) + " fps", true, std::abs (s.timecodeFps.load() - rates[k]) < 0.01f);
        tb.addItem (490, "beats: clips and automation move with the tempo", true, s.timebase.load() == 0);
        tb.addItem (491, "time: everything keeps its place in seconds (tracks and clips can still say otherwise)", true, s.timebase.load() == 1);
        m.addSubMenu ("main ruler (" + juce::String (timeline::rulerName (s.rulerMain.load())) + ")", r1);
        m.addSubMenu ("second ruler (" + juce::String (timeline::rulerName (s.rulerSub.load())) + ")", r2);
        m.addSubMenu ("timecode rate", fps);
        m.addSubMenu ("project timebase (" + juce::String (s.timebase.load() == 1 ? "time" : "beats") + ")", tb);
        return m;
    }
    ArrangePage* arrange() { return dynamic_cast<ArrangePage*> (pages[1]); }
    /** go to a position typed as bars.beats.sixteenths.ticks or as time */
    void goToDialog()
    {
        auto* w = new juce::AlertWindow ("go to", "bars.beats.sixteenths.ticks (17.2.3.120), or time (1:05.250, 90s)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", timeline::formatPosition (s.currentBar() * 4.0, s.meterMap()));
        w->addButton ("go", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            const auto txt = w->getTextEditorContents ("v");
            const double b = timeline::parse (txt, false, [this] (double secs) { return s.beatAt (secs); }, s.meterMap());
            if (b < 0) { say ("go to: could not read \"" + txt + "\""); return; }
            s.locate (b / 4.0);
            if (auto* ap = arrange()) if (b / 4.0 < ap->scrollBar || b / 4.0 > ap->scrollBar + ap->visibleBars()) ap->scrollBar = std::max (0.0, b / 4.0 - ap->visibleBars() * 0.25);
            say ("at " + timeline::formatPosition (b, s.meterMap()) + "  " + timeline::formatTime (s.secondsAt (b)));
        }), true);
    }
    std::function<void (int)> uiZoom;   // +1 bigger, -1 smaller, 0 actual size
    std::function<void (int)> setThemeFn;   // a colour theme: the editor builds the view again in it
    void say (const juce::String& m) { status = m; repaint (0, getHeight() - 20, getWidth(), 20); }
    void fileAction (int id)
    {
        juce::String err;
        if (id >= 700 && id < 760)
        {
            const int i = (id - 700) % 20;
            if (id < 720) locator (i, false); else if (id < 740) locator (i, true);
            else { auto* w = new juce::AlertWindow ("rename the locator", "a name", juce::MessageBoxIconType::NoIcon); w->addTextEditor ("t", s.locators[(size_t) i].name); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                   w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, i] (int r) { if (r == 1) { s.locators[(size_t) i].name = w->getTextEditorContents ("t").trim(); s.dirty = true; } }), true); }
            return;
        }
        if (id >= 600 && id < 700)
        {
            if (id - 600 >= backupList.size()) return;
            const auto b = backupList[id - 600]; juce::String err;
            windows.clear();
            if (proc.restoreBackup (b, err)) { rebuildAll(); say ("restored the backup of " + b.getFileNameWithoutExtension().fromLastOccurrenceOf (s.projectName + " ", false, false) + " (save to keep it; the version now is kept as a backup then)"); }
            else say (err);
            return;
        }
        if (id >= 460 && id < 492)
        {
            const float rates[] = { 24.0f, 25.0f, 29.97f, 30.0f, 50.0f, 60.0f };
            if (id < 464) { s.rulerMain.store (id - 460); if (s.rulerSub.load() == id - 460) s.rulerSub.store (id == 460 ? 1 : 0); }
            else if (id >= 470 && id < 475) s.rulerSub.store (id - 470);
            else if (id >= 480 && id < 486) s.timecodeFps.store (rates[id - 480]);
            else if (id == 490 || id == 491)
            {
                s.timebase.store (id - 490);
                // a project in time counts in time: the ruler too (bars below, for reference)
                if (id == 491 && s.rulerMain.load() == timeline::RULER_BARS) { s.rulerMain.store (timeline::RULER_TIME); s.rulerSub.store (timeline::RULER_BARS); }
                if (id == 490 && s.rulerMain.load() != timeline::RULER_BARS) { s.rulerMain.store (timeline::RULER_BARS); s.rulerSub.store (timeline::RULER_TIME); }
                say (id == 491 ? "the project is in time: clips, markers, the loop and automation keep their seconds when the tempo changes" : "the project is in beats: everything moves with the tempo");
            }
            s.dirty = true; repaint(); for (auto* pg : pages) pg->repaint(); return;
        }
        if (id >= 400 && id < 450) { if (setThemeFn) { const int th = id - 400; auto fn = setThemeFn; juce::MessageManager::callAsync ([fn, th] { fn (th); }); } return; }
        if (id >= 100 && id < 200) { windows.clear(); s.newFromTemplate (id - 100); select (nullptr); rebuildAll(); say ("new: " + Session::templateNames()[id - 100]); return; }
        auto choose = [this] (const juce::String& title, const juce::File& start, const juce::String& pattern, int flags, std::function<void (juce::File)> then)
        {
            chooser = std::make_unique<juce::FileChooser> (title, start, pattern);
            chooser->launchAsync (flags, [then] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) then (c.getResult()); });
        };
        const int saveFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
        const int openFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        if (id >= 50 && id < 100)
        {
            auto* ap = arrange();
            if (! ap) return;
            if (((id <= 62 && id != 61) || (id >= 81 && id <= 86) || id == 92 || id == 99) && page != 1) setPage (1);
            switch (id)
            {
                case 50: ap->copySel(); break;              case 51: ap->cutSel(); break;
                case 52: ap->pasteAtPlayhead(); break;      case 53: ap->duplicateSel(); break;
                case 54: ap->selectAll(); break;            case 55: ap->splitSel(); break;
                case 56: ap->deleteSel(); break;            case 57: ap->muteSel(); break;
                case 58: ap->insertBars(); break;           case 59: ap->deleteLoopTime(); break;
                case 60: ap->loopSelection(); break;        case 61: goToDialog(); break;
                case 62: ap->signatureDialog ((int) s.meterMap().barAt (s.currentBar() * 4.0 + 1.0e-9)); break;
                case 70: case 71: case 72: if (uiZoom) uiZoom (id == 70 ? 1 : id == 71 ? -1 : 0); break;
                case 73: setPage (1); ap->zoomKey (true); break;  case 74: setPage (1); ap->zoomKey (false); break;
                case 75: setPage (1); ap->zoomToFit(); break;     case 76: setPage (1); ap->zoomToSelection(); break;
                case 77: ap->setTrackHeight (ap->laneH + 12); break; case 78: ap->setTrackHeight (ap->laneH - 12); break;
                case 79: s.follow.store (! s.follow.load()); s.dirty = true; break;
                case 80: ap->toggleSnap(); break;
                case 81: { juce::String e; auto j = s.joinClips (ap->sel, e); if (! j.empty()) ap->sel = j; say (e.isNotEmpty() ? e : juce::String (j.size()) + " joined clip(s)"); break; }
                case 82: case 83: case 84:
                {
                    double a = 0, b = 0; std::vector<Track*> ts;
                    if (! range (a, b, ts)) { say ("no time selection: drag in the ruler, or select clips (" + juce::String (id == 82 ? "render" : id == 83 ? "reverse" : "silence") + " works on it)"); break; }
                    if (id == 82) { juce::String e; auto made = s.renderInPlace (ts, a, b, e); say (e.isNotEmpty() ? e : juce::String (made.size()) + " track(s) rendered in place, the originals muted there"); }
                    else if (id == 83) { s.reverseRange (ts, a, b); say ("reversed the audio from " + s.formatRuler (s.rulerMain.load(), a * 4.0) + " to " + s.formatRuler (s.rulerMain.load(), b * 4.0)); }
                    else { s.silenceRange (ts, a, b); say ("silenced"); }
                    break;
                }
                case 85: { for (auto& r : ap->sel) { double s0, l; if (s.clipSpan (r, s0, l)) s.moveClips ({ r }, ap->snapBar (s0) - s0); } say ("quantised to the grid"); break; }
                case 86: { const double b = s.currentBar(); const int n = (int) s.markers->size() + 1; s.editMarkers ([b, n] (std::vector<Marker>& v) { v.push_back ({ "marker " + juce::String (n), b }); }); say ("a marker at " + s.formatRuler (s.rulerMain.load(), b * 4.0)); break; }
                case 87: case 88: { if (auto* t = s.addTrack (id == 87 ? "instrument" : "audio", -1, id == 87 ? TrackKind::Instrument : TrackKind::Audio)) select (t); break; }
                case 89: { if (auto* t = selected()) { juce::String e; if (auto* nt = s.duplicateTrack (*t, e)) { select (nt); say ("duplicated: " + nt->name); } else say (e); } break; }
                case 90: { if (auto* t = selected()) { juce::String e; if (t->isFrozen()) { s.unfreezeTrack (*t); say (t->name + " live again"); } else if (s.freezeTrack (*t, e)) say (t->name + " frozen"); else say (e); } break; }
                case 91: openSearch(); break;
                case 92:
                {
                    if (! ap->sel.empty()) { const auto r = ap->sel.front(); ap->ask ("rename the clip", "a name", {}, [this, r] (juce::String n) { s.setClipName (r, n.trim()); }); }
                    else if (auto* t = selected()) ap->ask ("rename the track", "a name", t->name, [this, t] (juce::String n) { if (n.trim().isNotEmpty()) { t->name = n.trim(); s.dirty = true; rebuildAll(); } });
                    break;
                }
                case 93: case 94: case 95:
                {
                    if (ap->timeMode()) { const int nG = (int) timeline::timeGrids().size(); int g = s.timeGridIndex.load(); if (g == 0) g = 8; g = juce::jlimit (1, nG - 2, g + (id == 93 ? -1 : 1)); s.timeGridIndex.store (g); }
                    else
                    {
                        int g = s.gridIndex.load(); if (g == 0) g = 5;
                        if (id == 95) { static const int tri[] = { 0, 0, 9, 10, 11, 12, 13, 0, 0, 2, 3, 4, 5, 6 }; g = g < 14 && tri[g] ? tri[g] : g; }
                        else if (g >= 1 && g <= 8) g = juce::jlimit (1, 8, g + (id == 93 ? 1 : -1));
                        else if (g >= 9 && g <= 13) g = juce::jlimit (9, 13, g + (id == 93 ? 1 : -1));
                        s.gridIndex.store (g);
                    }
                    s.snap.store (true); s.dirty = true; say ("grid " + (ap->timeMode() ? juce::String (timeline::timeGrids()[(size_t) s.timeGridIndex.load()].name) : juce::String (timeline::grids()[(size_t) s.gridIndex.load()].name)));
                    break;
                }
                case 97: setPage (5); break;
                case 98: openShortcuts(); break;
                case 99:
                {
                    double from = s.loopOn.load() ? s.loopStart.load() : s.currentBar();
                    if (! ap->sel.empty()) { from = 1.0e12; for (auto& r : ap->sel) { double s0, l; if (s.clipSpan (r, s0, l)) from = std::min (from, s0); } }
                    s.locate (from); if (! s.running.load()) s.togglePlay();
                    break;
                }
                default: break;
            }
            ap->repaint();
            return;
        }
        if (id >= 300 && id < 400) { s.gridIndex.store (id - 300); s.snap.store (true); s.dirty = true; if (auto* ap = arrange()) ap->repaint(); return; }
        if (id == 40 || id == 41 || id == 46)
        {
            const int keep = selUid;
            if (id == 46) { auto* hp = new HistoryPanel (proc.history, [this] (const juce::String& m) { say (m); }); juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (hp), getScreenBounds().withSizeKeepingCentre (10, 10).translated (0, -200), nullptr); return; }
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
            case 9:
            {
                if (s.projectFile == juce::File()) { say ("collect all: save the project first"); break; }
                juce::String e; const int n = s.collectAll (e);
                if (e.isEmpty()) proc.saveProject (s.projectFile, e);
                say (e.isNotEmpty() ? e : juce::String (n) + " file(s) copied into " + s.audioDir().getFullPathName() + "; saved");
                break;
            }
            case 8:
            {
                const auto ms = s.missing();
                if (ms.empty()) { say ("nothing is missing"); break; }
                juce::String msg;
                if (! ms.audio.isEmpty()) msg << ms.audio.size() << " audio file(s) missing:\n" << ms.audio.joinIntoString ("\n").substring (0, 1200) << "\n\n";
                if (! ms.plugins.isEmpty()) msg << ms.plugins.size() << " plug-in(s) not installed here (kept in the project as they were; they come back where they are installed):\n" << ms.plugins.joinIntoString ("\n").substring (0, 800);
                if (ms.audio.isEmpty()) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "missing", msg); break; }
                auto* w = new juce::AlertWindow ("missing files", msg, juce::MessageBoxIconType::NoIcon);
                w->addButton ("look in a folder...", 1); w->addButton ("close", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, choose] (int r)
                {
                    if (r != 1) return;
                    choose ("where the files are (every folder in it is looked through)", juce::File::getSpecialLocation (juce::File::userMusicDirectory), {}, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                            [this] (juce::File f) { const int n = s.findMissing (f); const auto left = s.missing().audio.size(); say ("found " + juce::String (n) + " file(s)" + (left > 0 ? ", " + juce::String (left) + " still missing" : juce::String (", nothing missing now"))); });
                }), true);
                break;
            }
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
        w->addComboBox ("what", { "the mix", "the mix before the master chain", "stems: every track, return and the master", "stems: the tracks numbered below", "every marker region as its own file (named by its marker)" }, "export");
        const int sel = s.selectedTrack.load();
        w->addTextEditor ("tracks", juce::String (juce::jmax (0, sel) + 1), "track numbers (for the stems of chosen tracks), e.g. 1 3 4");
        double a = 0, b = 0; s.renderRange (a, b);
        const double pb = s.projectBars();
        const auto& mm = s.meterMap();
        auto P = [this] (double units) { return s.formatRuler (s.rulerMain.load(), units * 4.0); }; (void) mm;
        w->addComboBox ("range", { "the whole project (" + P (0) + " - " + P (pb) + ")", "the loop / time selection (" + P (s.loopStart.load()) + " - " + P (s.loopEnd.load()) + ")", "the bars typed below" }, "range");
        w->getComboBoxComponent ("range")->setSelectedItemIndex (s.loopOn.load() ? 1 : 0);
        w->addTextEditor ("bars", s.timeRuler() ? "0:00 0:30" : "1 9", "from, to: bars (1 9) or times (0:30 1:45.500, 90s 120s); the end is not included");
        w->addComboBox ("format", { "wav", "aiff", "flac" }, "file");
        w->addComboBox ("bits", { "16 bit", "24 bit", "32 bit float (wav)" }, "word length"); w->getComboBoxComponent ("bits")->setSelectedItemIndex (1);
        w->addComboBox ("rate", { "the project's rate (" + juce::String (s.sampleRate(), 0) + " hz)", "44100 hz", "48000 hz", "88200 hz", "96000 hz" }, "sample rate");
        w->addComboBox ("norm", { "no normalising", "normalise to -1 dbfs", "normalise to -0.3 dbfs", "normalise to 0 dbfs" }, "normalise");
        w->addComboBox ("dither", { "dither (below 32 bit)", "no dither" }, "dither");
        w->addComboBox ("limit", { "no limiter", "true-peak limiter at -1 dbtp (the mix)" }, "limiter");
        w->addComboBox ("lufs", { "loudness as it is", "to -14 lufs (spotify, youtube, tidal)", "to -16 lufs (apple music)", "to -11 lufs (club, loud)", "to -9 lufs (very loud)", "to -23 lufs (broadcast, ebu r128)" }, "loudness (integrated; the true peak stays under the ceiling)");
        for (auto* id : { "what", "format", "rate", "norm", "dither", "limit", "lufs" }) if (w->getComboBoxComponent (id)->getSelectedItemIndex() < 0) w->getComboBoxComponent (id)->setSelectedItemIndex (0);
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
            if (const int lu = w->getComboBoxComponent ("lufs")->getSelectedItemIndex(); lu > 0) { o.loudnessTarget = true; o.targetLufs = std::array<float, 6> { 0, -14, -16, -11, -9, -23 }[(size_t) juce::jlimit (0, 5, lu)]; if (! o.limit) o.ceilingDb = -1.0f; o.limit = o.limit; }
            double a = 0, b = s.projectBars();
            const int range = w->getComboBoxComponent ("range")->getSelectedItemIndex();
            if (range == 1) { a = s.loopStart.load(); b = s.loopEnd.load(); }
            else if (range == 2)
            {
                // two positions: bars (of the meter) or times, as typed
                auto t = juce::StringArray::fromTokens (w->getTextEditorContents ("bars"), " ,", ""); t.removeEmptyStrings();
                const auto& mm = s.meterMap(); auto toBeats = [this] (double sec) { return s.beatAt (sec); };
                auto one = [&] (const juce::String& x) { if (x.containsAnyOf (":sm")) return timeline::parse (x, false, toBeats, mm); return mm.barStart (std::max (0LL, (long long) x.getIntValue() - 1)); };
                const double pa = t.size() > 0 ? one (t[0]) : 0.0, pb = t.size() > 1 ? one (t[1]) : -1.0;
                a = std::max (0.0, pa) / 4.0; b = std::max (a + 1.0e-3, pb > 0 ? pb / 4.0 : a + 1.0);
            }
            if (what == 3)
                for (auto& n : juce::StringArray::fromTokens (w->getTextEditorContents ("tracks"), " ,", ""))
                    if (const int k = n.getIntValue() - 1; k >= 0 && k < (int) s.tracks.size()) o.only.push_back (s.tracks[(size_t) k]->uid);
            if (what == 3 && o.only.empty()) { say ("export: no such track numbers"); return; }
            const auto ext = juce::String (ExportOptions::extension (o.format));
            if (what == 4)
            {
                // regions: from each marker to the next (the last to the end of the project), a file each, named by the marker
                if (s.markers->empty()) { say ("export: no markers (put markers where the regions start)"); return; }
                choose ("export the regions into a folder", StudioProcessor::projectsDir(), {}, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, o] (juce::File f)
                {
                    const auto dir = f.getChildFile (s.projectName + " regions"); juce::String e;
                    const int n = s.renderRegions (dir, o, e);
                    say (e.isNotEmpty() ? e : juce::String (n) + " regions in " + dir.getFullPathName());
                });
                return;
            }
            if (what <= 1)
                choose ("export the mix", StudioProcessor::projectsDir().getChildFile (s.projectName + ext), "*" + ext, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, o, a, b, what, ext] (juce::File f)
                        {
                            juce::String e; s.renderMaster (f.withFileExtension (ext), a, b, nullptr, e, o, what == 1);
                            const float lu = s.exportLufs.load(), tp = s.exportTruePeak.load();
                            say (e.isNotEmpty() ? e : "written: " + f.withFileExtension (ext).getFileName() + (lu > -70.0f ? "   " + juce::String (lu, 1) + " lufs integrated, true peak " + juce::String (tp, 1) + " dbtp" : juce::String()));
                        });
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
        if (made) { select (made); say ("project rendered (" + timeline::formatPosition (a * 4.0, s.meterMap()) + " - " + timeline::formatPosition (b * 4.0, s.meterMap()) + ") into " + made->name); } else say (err.isNotEmpty() ? err : juce::String ("nothing rendered"));
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
        s.retire (s.removeDevice (t, d.uid));   // (freed in tick, once the pages have let go of it)
        structure = -1;
    }

    // ---- structure watch: rebuild the pages when tracks / devices / selection change ----------------
    long long structureKey() const
    {
        using U = unsigned long long;   // (a hash: unsigned, so it wraps instead of overflowing)
        U k = (U) s.retirements.load() * 998244353ULL + (U) s.epoch.load() * 1000000007ULL + (U) s.tracks.size() * 1000003ULL + (U) s.returns.size() * 7919ULL + (U) (long long) selUid * 31ULL;
        for (auto& t : s.tracks) k = k * 31ULL + (U) (long long) t->uid * 7ULL + (U) t->numDevices() + (t->isAudio() ? 1ULL : 0ULL);
        for (auto& r : s.returns) k = k * 31ULL + (U) r->numDevices();
        k = k * 31ULL + (U) s.master->numDevices();
        return (long long) (k >> 1);
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
    /** a controller's command (a mapped button, or a mackie control surface) */
    /** the time selection (the loop region, or the selected clips' span) and the tracks to work on (the selected clips',
        or the selected one) */
    bool range (double& a, double& b, std::vector<Track*>& ts)
    {
        auto* ap = arrange(); ts.clear();
        if (ap) for (auto& r : ap->sel) if (auto* t = s.clipTrack (r)) if (std::find (ts.begin(), ts.end(), t) == ts.end()) ts.push_back (t);
        if (ts.empty()) if (auto* t = selected()) if (! t->isMaster()) ts.push_back (t);
        if (s.loopOn.load() && s.loopEnd.load() > s.loopStart.load()) { a = s.loopStart.load(); b = s.loopEnd.load(); }
        else if (ap && ! ap->sel.empty()) { a = 1.0e12; b = 0; for (auto& r : ap->sel) { double s0, l; if (s.clipSpan (r, s0, l)) { a = std::min (a, s0); b = std::max (b, s0 + l); } } }
        else return false;
        return b > a && ! ts.empty();
    }
    /** every key, searchable */
    void openShortcuts()
    {
        struct Sheet : juce::Component
        {
            Sheet() : theme (studioTheme())
            {
                addAndMakeVisible (box); box.setTextToShowWhenEmpty ("find a key or an action ...", theme.ink.withAlpha (0.5f));
                box.setColour (juce::TextEditor::backgroundColourId, theme.ground); box.setColour (juce::TextEditor::textColourId, theme.ink); box.setFont (uiFont (12.0f));
                box.onTextChange = [this] { repaint(); };
                setSize (620, 560);
            }
            void resized() override { box.setBounds (6, 6, getWidth() - 12, 24); }
            void paint (juce::Graphics& g) override
            {
                g.fillAll (theme.ground); const auto q = box.getText().trim().toLowerCase(); int y = 38; g.setFont (uiFont (10.0f));
                for (auto& [k, w] : shortcutList())
                {
                    if (q.isNotEmpty() && ! k.toLowerCase().contains (q) && ! w.toLowerCase().contains (q)) continue;
                    if (y > getHeight() - 16) break;
                    g.setColour (theme.fill); g.drawText (k, 10, y, 190, 16, juce::Justification::centredLeft, true);
                    g.setColour (theme.ink); g.drawText (w, 200, y, getWidth() - 210, 16, juce::Justification::centredLeft, true); y += 17;
                }
            }
            ui::Theme theme; juce::TextEditor box;
        };
        auto* sh = new Sheet();
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (sh), getScreenBounds().withSizeKeepingCentre (10, 10).translated (0, -250), nullptr);
    }
    /** search, and going to what was found */
    void openSearch()
    {
        auto* sp = new SearchPanel (s, [this] (const SearchPanel::Hit& h) { goTo (h); });
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (sp), getScreenBounds().withSizeKeepingCentre (10, 10).translated (0, -220), nullptr);
    }
    void goTo (const SearchPanel::Hit& h)
    {
        if (h.trackUid) if (auto* t = s.trackByUid (h.trackUid)) select (t);
        if (h.kind == 4) { setPage (2); return; }
        setPage (1);
        if (auto* ap = arrange())
        {
            if (h.kind == 1) { double s0 = 0, len = 0; if (s.clipSpan (h.clip, s0, len)) { ap->sel = { h.clip }; ap->showBars (std::max (0.0, s0 - len * 0.25), s0 + len * 1.25); s.locate (s0); } }
            else if (h.bar >= 0) { s.locate (h.bar); const double vb = ap->visibleBars(); ap->showBars (std::max (0.0, h.bar - vb * 0.1), std::max (0.0, h.bar - vb * 0.1) + vb); }
            if (h.trackUid) if (auto* t = s.trackByUid (h.trackUid)) { const int row = ap->rowOf (t); if (row >= 0) ap->scrollY = std::max (0, row * ap->laneH - ap->laneH); }
            ap->repaint();
        }
        say ("found: " + h.text);
    }
    /** a locator: keep the place (store), or go to it */
    void locator (int i, bool store)
    {
        auto& L = s.locators[(size_t) juce::jlimit (0, 8, i)]; auto* ap = arrange();
        if (store)
        {
            L.set = true; L.bar = s.currentBar(); if (L.name.isEmpty()) L.name = "locator " + juce::String (i + 1);
            if (ap) { L.viewFrom = ap->scrollBar; L.viewTo = ap->scrollBar + ap->visibleBars(); }
            L.loop = s.loopOn.load(); L.loopA = s.loopStart.load(); L.loopB = s.loopEnd.load(); s.dirty = true;
            say (L.name + " kept: " + s.formatRuler (s.rulerMain.load(), L.bar * 4.0) + " (alt+" + juce::String (i + 1) + " comes back here)");
            return;
        }
        if (! L.set) { say ("locator " + juce::String (i + 1) + " is empty: alt+shift+" + juce::String (i + 1) + " keeps the place there"); return; }
        s.locate (L.bar); if (ap) ap->showBars (L.viewFrom, L.viewTo);
        if (L.loop) { s.loopStart.store (L.loopA); s.loopEnd.store (L.loopB); s.loopOn.store (true); }
        say (L.name + ": " + s.formatRuler (s.rulerMain.load(), L.bar * 4.0));
    }
    void runCommand (int c)
    {
        auto* t = s.trackAt (s.selectedTrack.load());
        auto arrKey = [this] (juce::juce_wchar ch) { if (auto* ap = dynamic_cast<ArrangePage*> (pages[1])) ap->key (juce::KeyPress (ch, {}, ch)); };
        auto bankTrack = [this] (int ch) -> Track* { const int i = s.mackieBank.load() + ch; return i >= 0 && i < (int) s.tracks.size() ? s.tracks[(size_t) i].get() : nullptr; };
        if (c < 0)
        {
            // a learnt button: -(command·1000 + (cc: 500) + number)
            const int v = -c, cmd = v / 1000, rest = v % 1000; const bool cc = rest >= 500; const int num = cc ? rest - 500 : rest;
            auto maps = *s.cmdMaps; maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const CmdMap& m) { return m.command == cmd || (m.note == ! cc && m.number == num); }), maps.end());
            maps.push_back ({ ! cc, num, cmd }); s.setCmdMaps (maps);
            say (commandName (cmd) + ": " + (cc ? "cc " : "note ") + juce::String (num)); return;
        }
        switch (c)
        {
            case CMD_PLAY: s.togglePlay(); break;
            case CMD_STOP: s.stop(); break;
            case CMD_RECORD: rec.onClick(); break;
            case CMD_LOOP: s.loopOn.store (! s.loopOn.load()); break;
            case CMD_CLICK: s.metronome.store (! s.metronome.load()); break;
            case CMD_PREV_MARKER: arrKey ('['); break;
            case CMD_NEXT_MARKER: arrKey (']'); break;
            case CMD_UNDO: fileAction (40); break;
            case CMD_REDO: fileAction (41); break;
            case CMD_PREV_TRACK: case CMD_NEXT_TRACK: { const int i = juce::jlimit (0, std::max (0, (int) s.tracks.size() - 1), s.selectedTrack.load() + (c == CMD_PREV_TRACK ? -1 : 1)); if (! s.tracks.empty()) select (s.tracks[(size_t) i].get()); break; }
            case CMD_ARM: if (t) t->armed.store (! t->armed.load()); break;
            case CMD_MUTE: if (t) { t->mixer->setBase (MP_MUTE, t->muted() ? 0.0f : 1.0f); s.dirty = true; } break;
            case CMD_SOLO: if (t) { t->solo.store (! t->solo.load()); s.dirty = true; } break;
            case CMD_TO_START: s.locate (0.0); break;
            case CMD_BACK_BAR: s.locate (std::max (0.0, std::ceil (s.currentBar() - 1.0e-6) - 1.0)); break;
            case CMD_FWD_BAR: s.locate (std::floor (s.currentBar() + 1.0e-6) + 1.0); break;
            case CMD_SAVE: fileAction (2); break;
            case CMD_MCU_BANK_DOWN: s.mackieBank.store (std::max (0, s.mackieBank.load() - 8)); say ("mackie: tracks " + juce::String (s.mackieBank.load() + 1) + "-" + juce::String (s.mackieBank.load() + 8)); break;
            case CMD_MCU_BANK_UP: s.mackieBank.store (std::max (0, std::min ((int) s.tracks.size() - 1, s.mackieBank.load() + 8))); say ("mackie: tracks " + juce::String (s.mackieBank.load() + 1) + "-" + juce::String (s.mackieBank.load() + 8)); break;
            case CMD_MCU_CH_DOWN: s.mackieBank.store (std::max (0, s.mackieBank.load() - 1)); break;
            case CMD_MCU_CH_UP: s.mackieBank.store (std::max (0, std::min ((int) s.tracks.size() - 1, s.mackieBank.load() + 1))); break;
            default:
                if (c >= CMD_SCENE1 && c <= CMD_LAST) s.launchScene (c - CMD_SCENE1);
                else if (c >= CMD_MCU_ARM && c < CMD_MCU_ARM + 8) { if (auto* bt = bankTrack (c - CMD_MCU_ARM)) bt->armed.store (! bt->armed.load()); }
                else if (c >= CMD_MCU_SOLO && c < CMD_MCU_SOLO + 8) { if (auto* bt = bankTrack (c - CMD_MCU_SOLO)) bt->solo.store (! bt->solo.load()); }
                else if (c >= CMD_MCU_MUTE && c < CMD_MCU_MUTE + 8) { if (auto* bt = bankTrack (c - CMD_MCU_MUTE)) bt->mixer->setBase (MP_MUTE, bt->muted() ? 0.0f : 1.0f); }
                else if (c >= CMD_MCU_SELECT && c < CMD_MCU_SELECT + 8) { if (auto* bt = bankTrack (c - CMD_MCU_SELECT)) select (bt); }
                break;
        }
    }
    void tick()
    {
        for (int c : s.takeCommands()) runCommand (c);
        s.retime();   // (a tempo changed by its control: what is set to time keeps its seconds)
        // the master's loudness: its numbers, and a new measurement each time the transport starts
        s.loudness.update();
        if (const bool pl = s.playPpq.load() >= 0; pl != wasPlayingLoud) { if (pl) s.loudness.reset(); wasPlayingLoud = pl; }
        if (structureKey() != structure) rebuildAll();
        // drop windows of devices that vanished
        windows.erase (std::remove_if (windows.begin(), windows.end(), [this] (auto& w) { bool alive = false; auto chk = [&] (Track& t) { t.forEachDevice ([&] (Device& d) { if (&d == w->device) alive = true; }); }; for (auto& t : s.tracks) chk (*t); for (auto& r : s.returns) chk (*r); chk (*s.master); return ! alive; }), windows.end());
        if (! s.retiredDevices.empty() || ! s.retiredTracks.empty()) s.freeRetired();   // (the pages and windows above let go of them)
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
        const bool narrow = w < 1430;   // (a big ui scale or a small window: the rest goes on the second row)
        if (! narrow) { master.setBounds (x, 12, 160, 20); x += 166; punch.setBounds (x, 12, 50, 20); x += 52; capture.setBounds (x, 12, 60, 20); x += 66; }
        panic.setBounds (std::max (x, w - 240), 12, 50, 20); audio.setBounds (w - 188, 12, 50, 20); file.setBounds (w - 136, 12, 50, 20); save.setBounds (w - 82, 12, 50, 20);
        int tx = 12;
        for (auto* t : tabs) { t->setBounds (tx, 46, 70, 20); tx += 72; }
        click.setBounds (tx + 16, 46, 44, 20); countIn.setBounds (tx + 64, 46, 92, 20);
        if (narrow) { int x2 = tx + 172; master.setBounds (x2, 46, 160, 20); x2 += 166; punch.setBounds (x2, 46, 50, 20); x2 += 52; capture.setBounds (x2, 46, 60, 20); }
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
        g.setFont (uiFont (11.0f, true)); g.setColour (s.playPpq.load() >= 0 ? brightColour() : theme.ink);
        {
            // the counters: the main ruler's format, then the second's (like a main and a sub counter)
            const int m1 = s.rulerMain.load(), m2 = s.rulerSub.load();
            juce::String txt = s.formatRuler (m1, bar * 4.0);
            if (m2 != timeline::RULER_NONE && m2 != m1) txt << "   " << s.formatRuler (m2, bar * 4.0);
            if (s.timebase.load() == 0 || m1 == timeline::RULER_BARS) txt << "   " << juce::String (s.bpmShown.load(), 1) << " bpm   " << s.meterMap().sigAt (bar * 4.0).text();
            g.drawText (txt, positionRect(), juce::Justification::centredLeft);
        }
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
        click.setLit (s.metronome.load()); countIn.setText (s.countInBars.load() == 0 ? "no count-in" : "count-in " + juce::String (s.countInBars.load())); countIn.setLit (s.countInLeft.load() > 0);
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
            const bool alt = k.getModifiers().isAltDown();
            if (low == 'z' && alt) { fileAction (46); return true; }
            if (low == 'z') { fileAction (sh ? 41 : 40); return true; }
            if (low == 'j') { fileAction (81); return true; }
            if (low == 'r') { fileAction (sh ? 83 : 82); return true; }
            if (low == 'u') { fileAction (85); return true; }
            if (low == 'k') { fileAction (86); return true; }
            if (low == 't' && sh) { fileAction (87); return true; }
            if (low == 't' && alt) { fileAction (88); return true; }
            if (low == 'd' && sh) { fileAction (89); return true; }
            if (low == 'f' && alt) { fileAction (90); return true; }
            if (low == 'f') { fileAction (91); return true; }
            if (low == 'i' && ! sh) { fileAction (92); return true; }
            if (low == 'b') { fileAction (97); return true; }
            if (kc == '1' || kc == '2' || kc == '3') { fileAction (kc == '1' ? 93 : kc == '2' ? 94 : 95); return true; }
            if (kc == '4') { fileAction (80); return true; }
            if (alt && (kc == juce::KeyPress::backspaceKey || kc == juce::KeyPress::deleteKey)) { fileAction (84); return true; }
            if (low == 'y') { fileAction (41); return true; }
            if (low == 's') { fileAction (sh ? 3 : 2); return true; }
            if (low == 'o') { fileAction (1); return true; }
            if (low == 'n') { fileAction (100); return true; }
            if (low == 'e') { fileAction (7); return true; }
            if (page == 1 && low == 'c') { fileAction (50); return true; }
            if (page == 1 && low == 'x') { fileAction (sh ? 59 : 51); return true; }
            if (page == 1 && low == 'v') { fileAction (52); return true; }
            if (page == 1 && low == 'd') { fileAction (53); return true; }
            if (page == 1 && low == 'a') { fileAction (54); return true; }
            if (page == 1 && low == 't') { fileAction (55); return true; }
            if (page == 1 && low == 'm' && sh) { fileAction (57); return true; }
            if (page == 1 && low == 'i' && sh) { fileAction (58); return true; }
            if (page == 1 && (kc == juce::KeyPress::backspaceKey || kc == juce::KeyPress::deleteKey) && sh) { fileAction (59); return true; }
            if (page == 1 && low == 'l') { fileAction (60); return true; }
            if (low == 'g') { fileAction (61); return true; }
            const auto ch = k.getTextCharacter();
            if (kc == '=' || ch == '=' || ch == '+') { fileAction (70); return true; }
            if (kc == '-' || ch == '-' || ch == '_') { fileAction (71); return true; }
            if (kc == '0' || ch == '0') { fileAction (72); return true; }
            if (page == 1 && (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey)) if (auto* ap = arrange()) return ap->key (k);
            return false;
        }
        if (page == 1) if (auto* ap = arrange()) if (ap->key (k)) return true;
        if (k.getKeyCode() == juce::KeyPress::spaceKey && k.getModifiers().isShiftDown()) { fileAction (99); return true; }
        if (k.getKeyCode() == juce::KeyPress::spaceKey) { if (! s.running.load()) lastStart = s.currentBar(); s.togglePlay(); return true; }
        if (c == '/') { s.locate (lastStart); say ("back to where playing last started: " + s.formatRuler (s.rulerMain.load(), lastStart * 4.0)); return true; }
        if (c == '*') { rec.onClick(); return true; }
        if (c == '?') { openShortcuts(); return true; }
        if (k.getKeyCode() == juce::KeyPress::tabKey) { setPage ((page + 1) % pages.size()); return true; }
        if ((c == ',' || c == '.') && page == 1) if (auto* ap = dynamic_cast<ArrangePage*> (pages[1])) { ap->nudge (c == ',' ? -5.0 : 5.0); return true; }
        // locators: alt+1-9 goes to one, alt+shift+1-9 keeps the place there
        if (k.getModifiers().isAltDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '9') { locator (k.getKeyCode() - '1', k.getModifiers().isShiftDown()); return true; }
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
    juce::Rectangle<int> positionRect() const { return { getWidth() - 360, 31, 300, 16 }; }
    void mouseDown (const juce::MouseEvent& e) override { if (positionRect().contains (e.getPosition())) goToDialog(); }
    void pageForTest (int pg) { setPage (pg); }
    int currentPage() const { return page; }

    StudioProcessor& proc; Session& s; ui::Theme theme; Ctx ctx;
    juce::OwnedArray<Page> pages; juce::OwnedArray<ui::TextLink> tabs; int page = 0;
    ui::TextLink play, stop, rec, loop, mode, outMode, save, panic, autoWrite, audio, file, punch, capture; HoldLink fill;
    bool wasPlayingLoud = false; double lastStart = 0;
    juce::Array<juce::File> backupList;
    CpuMeter cpuMeter { s, theme };
    ui::TextLink click { "click", theme }, countIn { "count-in 1", theme };
    DevSlider bpm, swing, master;
    std::vector<int> held; int autoDevice = 0; bool capturing = false; int captureUid = 0;
    std::vector<std::unique_ptr<DeviceWindow>> windows;
    std::unique_ptr<juce::FileChooser> chooser;
    int selUid = 0; long long structure = -1; bool keyFill = false; juce::String status;
};

StudioEditor::StudioEditor (StudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    studioThemeIndex() = juce::jlimit (0, (int) studioPalettes().size() - 1, p.theme);
    nominalW = studioInfo().w; nominalH = studioInfo().h;
    makeView();
    uiScale = juce::jlimit (0.5f, 2.0f, p.uiScale);
    setResizable (true, true);
    setResizeLimits (nominalW / 3, nominalH / 3, 10000, 8000);
    setSize (juce::roundToInt (nominalW * uiScale), juce::roundToInt (nominalH * uiScale));
    startTimerHz (30);
}
void StudioEditor::setTheme (int i)
{
    i = juce::jlimit (0, (int) studioPalettes().size() - 1, i);
    studioThemeIndex() = i; proc.theme = i; if (proc.onTheme) proc.onTheme (i);
    // the view again, in the new colours (the colours are in every control): the same page and track
    const int pg = root ? root->currentPage() : 0; const int sel = proc.session.selectedTrack.load();
    if (root) { removeChildComponent (root.get()); root.reset(); }
    makeView();
    root->pageForTest (pg); proc.session.selectedTrack.store (sel);
    resized(); repaint();
    root->say ("colour theme: " + juce::String (studioPalettes()[(size_t) i].name));
}
void StudioEditor::makeView()
{
    root = std::make_unique<StudioView> (proc);
    root->setBounds (0, 0, nominalW, nominalH);
    addAndMakeVisible (*root);
    root->setThemeFn = [this] (int i) { setTheme (i); };
    root->uiZoom = [this] (int d)
    {
        static const float steps[] = { 0.5f, 0.6f, 0.67f, 0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.33f, 1.5f, 1.75f, 2.0f };
        float next = 1.0f;
        if (d > 0) { next = steps[12]; for (float st : steps) if (st > uiScale + 0.001f) { next = st; break; } }
        else if (d < 0) { next = steps[0]; for (float st : steps) if (st < uiScale - 0.001f) next = st; }
        setUiScale (next);
        root->say ("size " + juce::String (juce::roundToInt (uiScale * 100)) + "%" + (effectiveScale() < uiScale - 0.001f ? "  (" + juce::String (juce::roundToInt (effectiveScale() * 100)) + "% fits this window: make it bigger for more)" : juce::String()));
    };
}
StudioEditor::~StudioEditor() { stopTimer(); root.reset(); }
void StudioEditor::paint (juce::Graphics& g) { g.fillAll (studioTheme().ground); }
float StudioEditor::effectiveScale() const
{
    // the scale asked for, unless the window is too small to give the pages their least room at it
    return std::max (0.2f, std::min ({ uiScale, (float) getWidth() / (float) kMinW, (float) getHeight() / (float) kMinH }));
}
void StudioEditor::resized()
{
    const float sc = effectiveScale();
    root->setTransform ({});
    root->setBounds (0, 0, (int) std::ceil (getWidth() / sc - 0.01f), (int) std::ceil (getHeight() / sc - 0.01f));
    root->setTransform (juce::AffineTransform::scale (sc));
}
void StudioEditor::setUiScale (float sc)
{
    uiScale = juce::jlimit (0.5f, 2.0f, sc);
    proc.uiScale = uiScale;
    if (proc.onUiScale) proc.onUiScale (uiScale);
    resized();
}
juce::PopupMenu StudioEditor::viewMenu() { return root->viewMenu(); }
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

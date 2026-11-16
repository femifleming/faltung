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
          play ("play", theme), stop ("stop", theme), rec ("rec", theme), loop ("loop", theme), mode ("session", theme), outMode ("produce", theme), save ("save", theme), panic ("panic", theme), autoWrite ("write", theme), audio ("audio", theme),
          fill ("fill", theme, [this] (bool on) { s.fill.store (on); }),
          bpm (s.global->base (GP_TEMPO), theme, "bpm"), swing (s.global->base (GP_SWING), theme, "swing"), master (s.global->base (GP_MASTER), theme, "master")
    {
        pages.add (new SessionPage (ctx)); pages.add (new ArrangePage (ctx)); pages.add (new SeqPage (ctx)); pages.add (new MixPage (ctx)); pages.add (new ModPage (ctx)); pages.add (new BrowsePage (ctx));
        for (auto* pg : pages) addChildComponent (pg);
        const char* names[6] = { "session", "arrange", "seq", "mix", "mod", "browse" };
        for (int i = 0; i < 6; ++i) { auto* t = tabs.add (new ui::TextLink (names[i], theme)); t->onClick = [this, i] { setPage (i); }; addAndMakeVisible (t); }
        for (auto* l : std::initializer_list<juce::Component*> { &play, &stop, &rec, &loop, &mode, &outMode, &save, &panic, &autoWrite, &audio, &fill, &bpm, &swing, &master }) addAndMakeVisible (l);
        bpm.setLabelWidth (34); swing.setLabelWidth (44); master.setLabelWidth (50);
        play.onClick = [this] { s.play(); };
        stop.onClick = [this] { if (s.isRecording()) s.stopRecording(); s.stop(); };
        rec.onClick = [this] { if (s.isRecording()) s.stopRecording(); else { s.startRecording(); if (! s.running.load()) s.play(); } };
        loop.onClick = [this] { s.loopOn.store (! s.loopOn.load()); };
        mode.onClick = [this] { s.mode.store ((s.mode.load() + 1) % 3); };
        outMode.onClick = [this] { s.multiOut.store (! s.multiOut.load()); };
        autoWrite.onClick = [this] { s.autoWrite.store (! s.autoWrite.load()); };
        panic.onClick = [this] { s.panic(); };
        audio.onClick = [this] { if (proc.openAudioSettings) proc.openAudioSettings(); };
        audio.setVisible (proc.isStandaloneApp.load());
        save.onClick = [this]
        {
            juce::String err;
            if (s.projectFile != juce::File()) { proc.saveProject (s.projectFile, err); status = err.isNotEmpty() ? err : "saved " + s.projectFile.getFileName(); return; }
            chooser = std::make_unique<juce::FileChooser> ("save project", StudioProcessor::projectsDir().getChildFile (s.projectName + ".fstudio"), "*.fstudio");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& c)
            { if (c.getResult() != juce::File()) { juce::String e; proc.saveProject (c.getResult().withFileExtension ("fstudio"), e); status = e.isNotEmpty() ? e : "saved"; } });
        };
        setWantsKeyboardFocus (true);
        setPage (0);
        rebuildAll();
    }
    ~StudioView() override { windows.clear(); }

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
        page = juce::jlimit (0, pages.size() - 1, pg);
        for (int i = 0; i < pages.size(); ++i) { pages[i]->setVisible (i == page); tabs[i]->setLit (i == page); }
        pages[page]->rebuild();
        resized();
    }
    int numPages() const { return pages.size(); }

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
        long long k = (long long) s.tracks.size() * 1000003 + (long long) s.returns.size() * 7919 + selUid * 31;
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
        pages[page]->rebuild();
        structure = structureKey();
    }
    void tick()
    {
        if (structureKey() != structure) rebuildAll();
        // drop windows of devices that vanished
        windows.erase (std::remove_if (windows.begin(), windows.end(), [this] (auto& w) { bool alive = false; auto chk = [&] (Track& t) { t.forEachDevice ([&] (Device& d) { if (&d == w->device) alive = true; }); }; for (auto& t : s.tracks) chk (*t); for (auto& r : s.returns) chk (*r); chk (*s.master); return ! alive; }), windows.end());
        pages[page]->tick();
        repaint (0, 0, getWidth(), 48);
        repaint (0, getHeight() - 20, getWidth(), 20);
        // momentary keys
        const bool f = juce::KeyPress::isKeyCurrentlyDown ('f') && hasKeyboardFocus (true);
        if (f != keyFill) { keyFill = f; s.fill.store (f || fill.lit); }
    }

    // ---- layout and paint ------------------------------------------------------------------------------
    void resized() override
    {
        const int w = getWidth();
        int x = 150;
        for (auto* l : { &play, &stop, &rec }) { l->setBounds (x, 12, 46, 20); x += 48; }
        fill.setBounds (x, 12, 40, 20); x += 44;
        loop.setBounds (x, 12, 44, 20); x += 48;
        bpm.setBounds (x, 12, 170, 20); x += 176;
        swing.setBounds (x, 12, 150, 20); x += 156;
        mode.setBounds (x, 12, 70, 20); x += 74;
        outMode.setBounds (x, 12, 76, 20); x += 80;
        autoWrite.setBounds (x, 12, 50, 20); x += 54;
        master.setBounds (x, 12, 180, 20); x += 186;
        panic.setBounds (w - 190, 12, 50, 20); audio.setBounds (w - 136, 12, 50, 20); save.setBounds (w - 82, 12, 50, 20);
        int tx = 12;
        for (auto* t : tabs) { t->setBounds (tx, 46, 70, 20); tx += 72; }
        for (auto* p : pages) p->setBounds (0, 70, w, getHeight() - 70 - 20);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        if (theme.mirror) { g.setColour (theme.ink.withAlpha (0.08f)); for (int y = 90; y < getHeight(); y += 90) g.fillRect (0, y, getWidth(), 1); }
        g.setColour (theme.ink); g.setFont (kDeep ? ui::cond (15.0f, true) : ui::sans (14.0f, true));
        g.drawText (juce::String (studioInfo().code), 12, 8, 120, 18, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.75f));
        g.drawText (s.projectName + (s.dirty.load() ? " *" : ""), 12, 26, 136, 14, juce::Justification::centredLeft, true);
        // position
        const double bar = s.currentBar();
        const int b = (int) std::floor (bar), beat = (int) std::floor ((bar - b) * 4.0);
        g.setFont (uiFont (11.0f, true)); g.setColour (s.playPpq.load() >= 0 ? juce::Colours::white : theme.ink);
        g.drawText (juce::String (b + 1) + "." + juce::String (beat + 1) + "   " + juce::String (s.bpmShown.load(), 1) + " bpm", 1052, 34, 240, 14, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.7f));
        juce::String info = juce::String (s.hostDriven.load() ? "host clock" : "internal clock") + ui::U ("  ·  ") + juce::String (s.tracks.size()) + " tracks  " + juce::String (s.returns.size()) + " returns";
        info << ui::U ("  ·  latency ") << juce::String (s.totalLatency() * 1000.0 / s.sampleRate(), 1) << " ms" << ui::U ("  ·  cpu ") << juce::String (juce::roundToInt (s.cpu.load() * 100)) << "%";
        if (s.isRecording()) info << ui::U ("  ·  recording");
        g.drawText (info, 150, 34, 900, 14, juce::Justification::centredLeft, true);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (0, 68, getWidth(), 1);
        g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (uiFont (9.5f));
        g.drawText (status.isEmpty() ? ui::U ("space: play / stop · 1-8: scenes · shift+1-8: patterns of the selected track · m / s: mute / solo · f: fill · r: record · ↑↓: track · tab: next page") : status, 12, getHeight() - 18, getWidth() - 24, 14, juce::Justification::centredLeft, true);
        play.setLit (s.playPpq.load() >= 0); rec.setLit (s.isRecording()); loop.setLit (s.loopOn.load()); autoWrite.setLit (s.autoWrite.load());
        mode.setText (playModeName ((PlayMode) s.mode.load())); outMode.setText (s.multiOut.load() ? "multi-out" : "produce"); outMode.setLit (s.multiOut.load());
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto c = k.getTextCharacter();
        auto* t = selected();
        if (k.getKeyCode() == juce::KeyPress::spaceKey) { s.togglePlay(); return true; }
        if (k.getKeyCode() == juce::KeyPress::tabKey) { setPage ((page + 1) % pages.size()); return true; }
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
    ui::TextLink play, stop, rec, loop, mode, outMode, save, panic, autoWrite, audio; HoldLink fill;
    ui::ParamSlider bpm, swing, master;
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
} // namespace fal::daw

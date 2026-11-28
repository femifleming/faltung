#pragma once
// faltung sndstudio — the pages: session (scenes), arrange (timeline + automation), seq (steps + devices),
// mix (strips, outputs, routes), mod (modulators, macros, midi), browse (devices, plug-ins, files, templates).
#include "StudioWidgets.h"
#include "PatternTools.h"
#include "Timeline.h"
#include "NoteEditor.h"
#include "SpectralEditor.h"
#include <map>
#include <set>
#include <juce_audio_processors/juce_audio_processors.h>

namespace fal::daw
{
/** what every page needs from the editor */
struct Ctx
{
    StudioProcessor& proc; Session& s;
    std::function<Track*()> selected;
    std::function<void (Track*)> select;
    std::function<void (int)> showPage;
    std::function<void (Device&)> openWindow;
    std::function<void (Track&, Device&)> removeDevice;
    std::function<void (const juce::String&)> status;
};

class Page : public juce::Component
{
public:
    explicit Page (Ctx& c) : ctx (c) {}
    virtual void tick() {}
    virtual void rebuild() {}
    Ctx& ctx;
    ui::Theme theme = studioTheme();
    void paintPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (r);
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText (title, r.getX() + 8, r.getY() + 4, r.getWidth() - 16, 16, juce::Justification::centredLeft);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (r.getX() + 4, r.getY() + 22, r.getWidth() - 8, 1);
    }
};

// =================================================================================================
//  session: the scene grid
// =================================================================================================
class SessionPage : public Page
{
public:
    explicit SessionPage (Ctx& c) : Page (c), addTrack ("+ track", theme), addAudio ("+ audio", theme), addReturn ("+ return", theme), addScene ("+ scene", theme)
    {
        for (auto* l : { &addTrack, &addAudio, &addReturn, &addScene }) addAndMakeVisible (l);
        addTrack.onClick = [this] { if (auto* t = ctx.s.addTrack()) { ctx.s.setInstrument (*t, ctx.s.createDevice ("fal:bd1")); ctx.select (t); } repaint(); };
        addAudio.onClick = [this] { if (auto* t = ctx.s.addTrack ({}, -1, TrackKind::Audio)) ctx.select (t); repaint(); };
        addReturn.onClick = [this] { ctx.s.addReturn(); repaint(); };
        addScene.onClick = [this] { visibleScenes = std::min (kScenes, visibleScenes + 8); repaint(); };
    }
    int visibleScenes = 16;
    const int colW = 92, rowH = 24, left = 150, top = 64;
    void resized() override
    {
        addTrack.setBounds (8, 30, 64, 20); addAudio.setBounds (76, 30, 64, 20); addReturn.setBounds (8, 8, 70, 20); addScene.setBounds (80, 8, 70, 20);
    }
    void paint (juce::Graphics& g) override
    {
        auto& s = ctx.s;
        g.setFont (uiFont (11.0f));
        const int nt = (int) s.tracks.size();
        const int cur = s.currentScene.load();
        // track headers
        for (int i = 0; i < nt; ++i)
        {
            auto& t = *s.tracks[(size_t) i];
            const auto r = juce::Rectangle<int> (left + i * colW - scrollX, 8, colW - 4, top - 14);
            const bool sel = ctx.selected() == &t;
            g.setColour (trackColour (t).withAlpha (sel ? 1.0f : 0.75f));
            if (theme.mirror) g.fillRect (r.getX(), r.getBottom() - 3, r.getWidth(), 3); else g.fillPath (ui::skewRect (r.toFloat().removeFromBottom (12.0f), 0.3f));
            g.setColour (theme.ink); g.setFont (uiFont (11.0f, sel));
            g.drawText (t.name, r.withHeight (18), juce::Justification::centredLeft, true);
            g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.7f));
            juce::String sub = t.isAudio() ? "audio" : (t.instrument ? t.instrument->name : "no instrument");
            if (t.muted()) sub = "muted"; else if (t.solo.load()) sub = "solo";
            g.drawText (sub, r.withTrimmedTop (18).withHeight (14), juce::Justification::centredLeft, true);
            const int ps = t.playStep.load();
            if (ps >= 0) { g.setColour (brightColour()); g.fillRect (r.getX() + (ps % 16) * (r.getWidth() / 16), r.getBottom() - 6, 4, 3); }
        }
        // scenes
        for (int sc = 0; sc < visibleScenes && sc < (int) s.scenes->size(); ++sc)
        {
            const auto& scene = (*s.scenes)[(size_t) sc];
            const int y = top + sc * rowH - scrollY;
            if (y + rowH < top || y > getHeight()) continue;
            const bool playing = sc == cur;
            g.setColour (theme.ink.withAlpha (playing ? 1.0f : 0.8f)); g.setFont (uiFont (11.0f, playing));
            g.drawText (juce::String (sc + 1), 8, y, 24, rowH, juce::Justification::centredLeft);
            g.drawText (scene.name, 34, y, left - 70, rowH, juce::Justification::centredLeft, true);
            g.setColour (playing ? brightColour() : theme.ink.withAlpha (0.6f));
            juce::Path tri; tri.addTriangle ((float) left - 22.0f, (float) y + 7.0f, (float) left - 22.0f, (float) y + rowH - 7.0f, (float) left - 10.0f, (float) y + rowH * 0.5f);
            g.fillPath (tri);
            for (int i = 0; i < nt; ++i)
            {
                auto& t = *s.tracks[(size_t) i];
                const int p = scene.patternFor (i);
                auto r = juce::Rectangle<float> ((float) (left + i * colW - scrollX), (float) y + 2.0f, (float) colW - 6.0f, (float) rowH - 4.0f);
                const bool live = t.shownPattern.load() == p && p >= 0 && s.playPpq.load() >= 0;
                if (p >= 0)
                {
                    g.setColour (trackColour (t).withAlpha (live ? 1.0f : 0.55f));
                    if (theme.mirror) g.fillRect (r.reduced (0.0f, 4.0f)); else g.fillPath (ui::skewRect (r, 0.3f));
                    g.setColour (ui::inkFor (trackColour (t)));
                    g.drawText (juce::String (p + 1), r, juce::Justification::centred);
                }
                else
                {
                    g.setColour (theme.ink.withAlpha (p == -1 ? 0.5f : 0.18f));
                    if (theme.mirror) g.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth(), 1.0f); else g.strokePath (ui::skewRect (r, 0.3f), juce::PathStrokeType (1.0f));
                    if (p == -1) { g.setColour (theme.ink.withAlpha (0.6f)); g.fillRect (r.getCentreX() - 3.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f); }
                }
            }
        }
        g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (uiFont (10.0f));
        g.drawText (ui::U ("click a cell: launch that pattern · shift-click: put the track's pattern here · alt-click: stop / keep · right-click: choose · double-click a scene name: rename · keys 1-8 launch scenes"), 8, getHeight() - 22, getWidth() - 16, 16, juce::Justification::centredLeft, true);
    }
    bool hit (juce::Point<int> p, int& track, int& scene) const
    {
        track = (p.x - left + scrollX) / colW; scene = (p.y - top + scrollY) / rowH;
        return p.x >= left && track >= 0 && track < (int) ctx.s.tracks.size() && p.y >= top && scene >= 0 && scene < visibleScenes;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        int ti, sc;
        if (e.y < top && e.x >= left)
        {
            ti = (e.x - left + scrollX) / colW;
            if (ti >= 0 && ti < (int) s.tracks.size())
            {
                if (e.mods.isPopupMenu()) { trackMenu (*s.tracks[(size_t) ti], ti); return; }
                ctx.select (s.tracks[(size_t) ti].get()); repaint();
            }
            return;
        }
        if (e.x < left && e.y >= top)
        {
            sc = (e.y - top + scrollY) / rowH;
            if (sc < 0 || sc >= visibleScenes) return;
            if (e.getNumberOfClicks() > 1) { rename (sc); return; }
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m; m.addItem (1, "capture: put every track's playing pattern in this scene"); m.addItem (2, "clear this scene"); m.addItem (3, "rename");
                m.showMenuAsync (juce::PopupMenu::Options(), [this, sc] (int r)
                {
                    auto& ss = ctx.s;
                    if (r == 1) ss.editScenes ([&] (std::vector<Scene>& v) { for (int i = 0; i < (int) ss.tracks.size(); ++i) { if ((int) v[(size_t) sc].pattern.size() <= i) v[(size_t) sc].pattern.resize (ss.tracks.size(), -2); v[(size_t) sc].pattern[(size_t) i] = ss.tracks[(size_t) i]->shownPattern.load() >= 0 ? ss.tracks[(size_t) i]->shownPattern.load() : -1; } });
                    if (r == 2) ss.editScenes ([&] (std::vector<Scene>& v) { v[(size_t) sc].pattern.assign (ss.tracks.size(), -2); });
                    if (r == 3) rename (sc);
                    repaint();
                });
                return;
            }
            s.launchScene (sc); repaint();
            return;
        }
        if (! hit (e.getPosition(), ti, sc)) return;
        auto& t = *s.tracks[(size_t) ti];
        const int p = (*s.scenes)[(size_t) sc].patternFor (ti);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            for (int b = 0; b < kPatterns; b += 8) { juce::PopupMenu sub; for (int k = b; k < b + 8; ++k) sub.addItem (100 + k, "pattern " + juce::String (k + 1), true, p == k); m.addSubMenu ("patterns " + juce::String (b + 1) + "-" + juce::String (b + 8), sub); }
            m.addSeparator(); m.addItem (1, "stop the track here", true, p == -1); m.addItem (2, "keep playing (empty)", true, p == -2);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, ti, sc] (int r)
            {
                if (r == 0) return;
                const int v = r >= 100 ? r - 100 : r == 1 ? -1 : -2;
                ctx.s.editScenes ([&] (std::vector<Scene>& vv) { if ((int) vv[(size_t) sc].pattern.size() <= ti) vv[(size_t) sc].pattern.resize ((size_t) ti + 1, -2); vv[(size_t) sc].pattern[(size_t) ti] = v; });
                repaint();
            });
            return;
        }
        if (e.mods.isShiftDown()) { const int cp = t.pattern.load(); s.editScenes ([&] (std::vector<Scene>& v) { if ((int) v[(size_t) sc].pattern.size() <= ti) v[(size_t) sc].pattern.resize ((size_t) ti + 1, -2); v[(size_t) sc].pattern[(size_t) ti] = cp; }); repaint(); return; }
        if (e.mods.isAltDown()) { s.editScenes ([&] (std::vector<Scene>& v) { if ((int) v[(size_t) sc].pattern.size() <= ti) v[(size_t) sc].pattern.resize ((size_t) ti + 1, -2); auto& x = v[(size_t) sc].pattern[(size_t) ti]; x = x == -1 ? -2 : -1; }); repaint(); return; }
        ctx.select (&t);
        if (p >= 0) s.launch (t, p); else if (p == -1) s.stopTrack (t);
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scrollY = juce::jlimit (0, std::max (0, visibleScenes * rowH - (getHeight() - top - 30)), scrollY - (int) (w.deltaY * 60));
        scrollX = juce::jlimit (0, std::max (0, (int) ctx.s.tracks.size() * colW - (getWidth() - left)), scrollX - (int) (w.deltaX * 60));
        repaint();
    }
    void rename (int sc)
    {
        auto* w = new juce::AlertWindow ("scene name", "", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", (*ctx.s.scenes)[(size_t) sc].name); w->addButton ("ok", 1); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, sc, w] (int r) { if (r == 1) { const auto n = w->getTextEditorContents ("name"); ctx.s.editScenes ([&] (std::vector<Scene>& v) { v[(size_t) sc].name = n; }); } repaint(); }), true);
    }
    void trackMenu (Track& t, int index)
    {
        juce::PopupMenu m;
        m.addItem (1, "rename"); m.addItem (2, "remove"); m.addItem (3, "duplicate patterns to a new track"); m.addSeparator();
        m.addItem (4, "move left", index > 0); m.addItem (5, "move right", index < (int) ctx.s.tracks.size() - 1);
        juce::PopupMenu col; for (int i = 0; i < 12; ++i) col.addColouredItem (100 + i, "colour " + juce::String (i + 1), juce::Colour (colourOf (i)), true, false);
        m.addSubMenu ("colour", col);
        Track* tp = &t;
        m.showMenuAsync (juce::PopupMenu::Options(), [this, tp, index] (int r)
        {
            auto& s = ctx.s;
            if (r == 1)
            {
                auto* w = new juce::AlertWindow ("track name", "", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", tp->name); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, tp, w] (int rr) { if (rr == 1) tp->name = w->getTextEditorContents ("name"); repaint(); }), true);
            }
            else if (r == 2) { if (ctx.selected() == tp) ctx.select (nullptr); s.removeTrack (index); }
            else if (r == 3) { if (auto* nt = s.addTrack (tp->name + " copy", index + 1, tp->kind)) { for (int i = 0; i < kPatterns; ++i) if (tp->patterns[(size_t) i]) s.setPattern (*nt, i, tp->patterns[(size_t) i]); if (tp->instrument) s.setInstrument (*nt, s.createDevice (tp->instrument->typeId)); } }
            else if (r == 4) s.moveTrack (index, index - 1);
            else if (r == 5) s.moveTrack (index, index + 1);
            else if (r >= 100) tp->colour = colourOf (r - 100);
            repaint();
        });
    }
    static juce::uint32 colourOf (int i) { static const juce::uint32 c[12] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0, 0xffc9b53b, 0xff79e04a, 0xff8593d4, 0xffe3bf5f }; return c[((i % 12) + 12) % 12]; }
    void tick() override { repaint(); }
private:
    ui::TextLink addTrack, addAudio, addReturn, addScene;
    int scrollX = 0, scrollY = 0;
};

// =================================================================================================
//  arrange: timeline, clips, audio, markers, loop, automation lanes, song list
// =================================================================================================
class ArrangePage : public Page, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    explicit ArrangePage (Ctx& c) : Page (c), addLane ("+ lane", theme), addSong ("+ song entry", theme), snapLink ("snap", theme), gridLink ("grid", theme),
                                    zoomIn ("+", theme), zoomOut ("-", theme), fitLink ("fit", theme), selLink ("sel", theme), taller ("rows +", theme), shorter ("rows -", theme), followLink ("follow", theme)
    {
        for (auto* l : { &addLane, &addSong, &snapLink, &gridLink, &zoomIn, &zoomOut, &fitLink, &selLink, &taller, &shorter, &followLink }) addAndMakeVisible (l);
        addLane.onClick = [this] { laneMenu(); };
        addSong.onClick = [this] { ctx.s.editSong ([] (std::vector<SongEntry>& v) { v.push_back ({ (int) v.size() % kScenes, 8 }); }); repaint(); };
        snapLink.onClick = [this] { toggleSnap(); };
        gridLink.onClick = [this] { gridMenu (&gridLink); };
        zoomIn.onClick = [this] { zoomKey (true); };
        zoomOut.onClick = [this] { zoomKey (false); };
        fitLink.onClick = [this] { zoomToFit(); };
        selLink.onClick = [this] { zoomToSelection(); };
        taller.onClick = [this] { setTrackHeight (laneH + 12); };
        shorter.onClick = [this] { setTrackHeight (laneH - 12); };
        followLink.onClick = [this] { ctx.s.follow.store (! ctx.s.follow.load()); ctx.s.dirty = true; };
        snapLink.setTooltip ("snap (n): edits land on the grid, and on clip edges, markers, the loop and the playhead when they are close. Hold cmd (ctrl) while dragging to place freely, to the sample");
        gridLink.setTooltip ("the grid: adaptive (finer as you zoom in) or a fixed division, straight, triplet, dotted, quintuplet or septuplet");
        zoomIn.setTooltip ("zoom in (=, or cmd / ctrl + scroll, or pinch, where the mouse is)"); zoomOut.setTooltip ("zoom out (-)");
        fitLink.setTooltip ("the whole project in view (shift+z)"); selLink.setTooltip ("zoom to the selected clips, or the loop (z)");
        taller.setTooltip ("taller rows (alt + scroll)"); shorter.setTooltip ("shorter rows");
        followLink.setTooltip ("follow: the view turns the page with the playhead while playing");
    }
    // ---- the view: px per bar, scroll, row height -------------------------------------------------------
    double pxPerBar = 24.0; double scrollBar = 0; int scrollY = 0;
    int laneH = 40;
    static constexpr double kMinPxPerBar = 0.25, kMaxPxPerBar = 40000.0;   // ~1000 bars across ... a few samples a pixel
    const int left = 140, top = 22, ruler = 52, songW = 190, autoH = 216, sbH = 10;
    int laneScroll = 0;                       // the first automation lane shown
    std::vector<ClipRef> sel;                 // the selected clips
    double hoverBar = -1;                     // under the mouse (the readout)

    double xd (double bar) const { return left + (bar - scrollBar) * pxPerBar; }
    int x (double bar) const { return (int) std::lround (juce::jlimit (-1.0e6, 1.0e6, xd (bar))); }
    double bar (double px) const { return (px - left) / pxPerBar + scrollBar; }
    double pxPerBeat() const { return pxPerBar / 4.0; }
    double gridBars() const
    {
        if (! timeMode()) return timeline::gridBeats (ctx.s.gridIndex.load(), pxPerBeat()) / 4.0;
        // the time grid, as bars where the view is
        const double c = scrollBar * 4.0, sec = ctx.s.secondsAt (c);
        return std::max (1.0e-9, (ctx.s.beatAt (sec + gridSec()) - c) / 4.0);
    }
    /** the main ruler counts time (min:sec, samples, timecode): the grid and snapping go by seconds */
    bool timeMode() const { return ctx.s.timeRuler(); }
    double pxPerSecond() const { const double c = scrollBar * 4.0; const double d = ctx.s.secondsAt (c + 4.0) - ctx.s.secondsAt (c); return d > 0 ? pxPerBar / d : pxPerBar; }
    double gridSec() const { return timeline::timeGridSeconds (ctx.s.timeGridIndex.load(), pxPerSecond(), ctx.s.timecodeFps.load()); }
    double snapSec (double sec) const { const double g = gridSec(); return std::max (0.0, std::round (sec / g) * g); }
    double visibleBars() const { return std::max (1.0e-6, (lanesArea().getWidth() - left) / pxPerBar); }
    juce::Rectangle<int> lanesArea() const { return { 0, ruler, getWidth() - songW, std::max (0, getHeight() - ruler - autoH - sbH) }; }
    juce::Rectangle<int> scrollArea() const { return { left, getHeight() - autoH - sbH, getWidth() - songW - left, sbH }; }
    juce::Rectangle<int> autoArea() const { return { 0, getHeight() - autoH, getWidth() - songW, autoH }; }
    juce::Rectangle<int> songArea() const { return { getWidth() - songW, 0, songW, getHeight() }; }
    int rowY (int row) const { return lanesArea().getY() + row * laneH - scrollY; }
    int rowAt (int py) const { return (int) std::floor ((py - lanesArea().getY() + scrollY) / (double) laneH); }

    /** the plain grid (no magnet), what keyboard moves use */
    const timeline::MeterMap& meter() const { return ctx.s.meterMap(); }
    double snapBar (double b) const
    {
        if (! ctx.s.snap.load()) return std::max (0.0, b);
        if (timeMode()) return ctx.s.beatAt (snapSec (ctx.s.secondsAt (b * 4.0))) / 4.0;
        return timeline::snap (b * 4.0, gridBars() * 4.0, meter()) / 4.0;
    }
    juce::String pos (double bar) const { return ctx.s.formatRuler (ctx.s.rulerMain.load(), bar * 4.0); }
    /** the grid plus the magnet: clip edges (other than the ones moving), markers, the loop, the playhead */
    double magnetBar (double b, bool free, const std::vector<ClipRef>& moving, bool* hit = nullptr) const
    {
        if (hit) *hit = false;
        if (free || ! ctx.s.snap.load()) return std::max (0.0, b);
        std::vector<double> t;
        for (auto& r : ctx.s.allClips())
        {
            if (std::find (moving.begin(), moving.end(), r) != moving.end()) continue;
            double s0, len; if (ctx.s.clipSpan (r, s0, len)) { t.push_back (s0 * 4.0); t.push_back ((s0 + len) * 4.0); }
        }
        for (auto& m : *ctx.s.markers) t.push_back (m.bar * 4.0);
        if (ctx.s.loopOn.load()) { t.push_back (ctx.s.loopStart.load() * 4.0); t.push_back (ctx.s.loopEnd.load() * 4.0); }
        t.push_back (ctx.s.currentBar() * 4.0);
        bool h = false;
        const double r = timeline::magnet (b * 4.0, 0.0, t, pxPerBeat(), 7.0, &h);
        if (hit) *hit = h;
        return h ? r / 4.0 : snapBar (b);
    }

    void zoomAt (double factor, double px)
    {
        const double anchor = bar (px);
        pxPerBar = juce::jlimit (kMinPxPerBar, kMaxPxPerBar, pxPerBar * factor);
        scrollBar = std::max (0.0, anchor - (px - left) / pxPerBar);
        repaint();
    }
    /** = and -: around the playhead when it is in view, else the middle */
    void zoomKey (bool in)
    {
        const double px = xd (ctx.s.currentBar());
        zoomAt (in ? 1.5 : 1.0 / 1.5, px >= left && px <= lanesArea().getRight() ? px : left + (lanesArea().getWidth() - left) * 0.5);
    }
    void showBars (double a, double b)
    {
        if (b <= a) b = a + 1;
        const double w = std::max (20.0, (double) lanesArea().getWidth() - left - 16);
        pxPerBar = juce::jlimit (kMinPxPerBar, kMaxPxPerBar, w / (b - a));
        scrollBar = std::max (0.0, a);
        repaint();
    }
    void zoomToFit() { showBars (0.0, std::max (4.0, ctx.s.projectBars())); }
    void zoomToSelection()
    {
        double a = 1.0e12, b = -1;
        for (auto& r : sel) { double s0, len; if (ctx.s.clipSpan (r, s0, len)) { a = std::min (a, s0); b = std::max (b, s0 + len); } }
        if (b < 0 && ctx.s.loopOn.load()) { a = ctx.s.loopStart.load(); b = ctx.s.loopEnd.load(); }
        if (b < 0) { zoomToFit(); return; }
        const double pad = (b - a) * 0.04;
        showBars (std::max (0.0, a - pad), b + pad);
    }
    void setTrackHeight (int h) { laneH = juce::jlimit (22, 220, h); scrollY = juce::jlimit (0, maxScrollY(), scrollY); repaint(); }
    int maxScrollY() const { return std::max (0, (int) ctx.s.tracks.size() * laneH - lanesArea().getHeight() + 8); }
    void toggleSnap() { ctx.s.snap.store (! ctx.s.snap.load()); ctx.s.dirty = true; repaint(); }
    juce::String timeGridLabel() const
    {
        const double g = gridSec();
        if (ctx.s.timeGridIndex.load() == (int) timeline::timeGrids().size() - 1) return "1 frame";
        return g >= 60 ? juce::String (g / 60, 0) + " min" : g >= 1 ? juce::String (g, g < 10 && std::abs (g - std::round (g)) > 1.0e-9 ? 1 : 0) + " s" : juce::String (g * 1000, g < 0.001 ? 1 : 0) + " ms";
    }
    void gridMenu (juce::Component* near)
    {
        juce::PopupMenu m;
        if (timeMode())
        {
            const auto& tg = timeline::timeGrids();
            for (int i = 0; i < (int) tg.size(); ++i) m.addItem (500 + i, i == 0 ? "adaptive (" + timeGridLabel() + ")" : juce::String (tg[(size_t) i].name), true, ctx.s.timeGridIndex.load() == i);
            m.addSeparator(); m.addItem (199, "snap", true, ctx.s.snap.load());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (near), [this] (int r) { if (r == 199) toggleSnap(); else if (r >= 500) { ctx.s.timeGridIndex.store (r - 500); ctx.s.snap.store (true); ctx.s.dirty = true; repaint(); } });
            return;
        }
        const auto& g = timeline::grids();
        for (int i = 0; i < (int) g.size(); ++i)
        {
            if (i == 9 || i == 14 || i == 17) m.addSeparator();
            m.addItem (200 + i, i == 0 ? timeline::gridName (0, pxPerBeat()) : juce::String (g[(size_t) i].name), true, ctx.s.gridIndex.load() == i);
        }
        m.addSeparator(); m.addItem (199, "snap", true, ctx.s.snap.load());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (near), [this] (int r) { if (r == 199) toggleSnap(); else if (r >= 200) { ctx.s.gridIndex.store (r - 200); ctx.s.snap.store (true); ctx.s.dirty = true; repaint(); } });
    }

    void resized() override
    {
        if (noteEd) noteEd->setBounds (lanesArea());
        if (specEd) specEd->setBounds (lanesArea());
        auto sa = songArea();
        addSong.setBounds (sa.getX() + 8, sa.getY() + 6, 100, 20);
        int bx = 8;
        auto place = [&] (juce::Component& c, int w) { c.setBounds (bx, 2, w, 18); bx += w + 4; };
        place (snapLink, 40); place (gridLink, 156); bx += 8;
        place (zoomOut, 18); place (zoomIn, 18); place (fitLink, 28); place (selLink, 28); bx += 8;
        place (shorter, 50); place (taller, 50); bx += 8; place (followLink, 50);
        addLane.setBounds (8, autoArea().getY() + 4, 60, 18);
    }
    /** the tracks, one a row: a folded group shows only its first track (its folder row) */
    std::vector<Track*> rows() const
    {
        std::vector<Track*> v; std::set<int> shown;
        for (auto& t : ctx.s.tracks)
        {
            const int g = t->group.load();
            if (const auto* gr = g ? ctx.s.groupOf (g) : nullptr) if (gr->folded) { if (shown.count (g)) continue; shown.insert (g); }
            v.push_back (t.get());
        }
        return v;
    }
    static float gainY (juce::Rectangle<float> r, float db) { return r.getY() + 2.0f + (12.0f - juce::jlimit (-36.0f, 12.0f, db)) / 48.0f * (r.getHeight() - 4.0f); }
    static float gainDbAt (juce::Rectangle<float> r, float y) { return juce::jlimit (-36.0f, 12.0f, 12.0f - (y - r.getY() - 2.0f) / std::max (1.0f, r.getHeight() - 4.0f) * 48.0f); }
    ClipRef gainClip;                       // the audio clip whose gain line is being drawn
    ClipRef warpClip; int warpDrag = -1;
    ClipRef envClip; int envIndex = -1, envDrag = -1;   // the clip whose own automation is being drawn, which, the point dragged
    /** a clip's envelopes, where its rectangle is, and the bar its envelopes count from */
    const std::vector<ClipEnv>* envsOf (ClipRef r, juce::Rectangle<float>* full = nullptr, double* origin = nullptr) const
    {
        auto* t = ctx.s.clipTrack (r); if (! t) return nullptr;
        const int row = rowOf (t); const double y = row >= -999 ? (double) rowY (row) : 0.0;
        if (r.audio) { for (auto& c : *t->aclips) if (c.id == r.id) { if (full) *full = { (float) xd (c.startBar), (float) y + 3.0f, (float) (c.bars * pxPerBar) - 1.0f, (float) laneH - 6.0f }; if (origin) *origin = c.startBar; return &c.envs; } }
        else for (auto& c : *t->clips) if (c.id == r.id) { if (full) *full = { (float) xd (c.startBar), (float) y + 3.0f, (float) (c.bars * pxPerBar) - 1.0f, (float) laneH - 7.0f }; if (origin) *origin = c.startBar - c.offset / 4.0; return &c.envs; }
        return nullptr;
    }
    void setEnvs (ClipRef r, std::vector<ClipEnv> e)
    {
        auto* t = ctx.s.clipTrack (r); if (! t) return;
        if (r.audio) ctx.s.editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == r.id) c.envs = e; });
        else ctx.s.editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == r.id) c.envs = e; });
    }
    static float envY (juce::Rectangle<float> r, float v) { return r.getBottom() - 2.0f - v * (r.getHeight() - 4.0f); }
    static float envValueAt (juce::Rectangle<float> r, float y) { return juce::jlimit (0.0f, 1.0f, (r.getBottom() - 2.0f - y) / std::max (1.0f, r.getHeight() - 4.0f)); }
    void paintEnvs (juce::Graphics& g, ClipRef r)
    {
        juce::Rectangle<float> full; double origin = 0; auto* ev = envsOf (r, &full, &origin);
        if (! ev || ev->empty()) return;
        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (full.toNearestInt());
        for (int k = 0; k < (int) ev->size(); ++k)
        {
            const auto& e = (*ev)[(size_t) k]; if (e.pts.empty()) continue;
            const bool ed = envClip == r && envIndex == k;
            juce::Path p; p.startNewSubPath (full.getX(), envY (full, e.pts.front().second));
            for (auto& [b, v] : e.pts) p.lineTo ((float) xd (origin + b / 4.0), envY (full, v));
            p.lineTo (full.getRight(), envY (full, e.pts.back().second));
            g.setColour (coolColour().withAlpha (ed ? 1.0f : 0.55f)); g.strokePath (p, juce::PathStrokeType (ed ? 1.6f : 1.0f));
            if (ed) for (auto& [b, v] : e.pts) { const float px = (float) xd (origin + b / 4.0), py = envY (full, v); g.fillEllipse (px - 3.0f, py - 3.0f, 6.0f, 6.0f); }
        }
    }     // the audio clip whose warp markers are being set, and the marker being dragged
    const AudioClip* audioClipOf (ClipRef r) const { if (auto* t = ctx.s.clipTrack (r)) for (auto& c : *t->aclips) if (c.id == r.id) return &c; return nullptr; }
    /** warp markers: every transient of the clip's source, where it plays now */
    void warpAtTransients (ClipRef r)
    {
        auto* c = audioClipOf (r); if (! c || ! c->sample) return;
        const double beats = c->bars * 4.0, avail = std::max (0.0, c->sample->seconds() - c->offset);
        auto cur = *c; if (cur.mode != 3) { cur.warp.clear(); }
        std::vector<std::pair<double, double>> w;
        const double rateNow = cur.mode == 3 ? 0.0 : (beats > 0 ? avail / beats : 1.0);
        for (int on : c->sample->onsets)
        {
            const double src = on / c->sample->rate - c->offset; if (src <= 0.01 || src >= avail) continue;
            const double b = cur.mode == 3 ? Session::warpBeat (cur, src, beats, avail) : src / rateNow;
            if (b > 0 && b < beats && (w.empty() || b - w.back().first > 1.0 / 32.0)) w.push_back ({ b, src });
        }
        auto* t = ctx.s.clipTrack (r);
        ctx.s.editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& x : v) if (x.id == r.id) { x.mode = 3; x.warp = w; } });
        ctx.status (juce::String (w.size()) + " warp markers at the transients: drag one onto the grid, or quantise them");
    }
    /** every marker onto the nearest grid line (the audio between follows) */
    void warpQuantise (ClipRef r)
    {
        auto* c = audioClipOf (r); auto* t = ctx.s.clipTrack (r); if (! c || ! t) return;
        auto w = c->warp; const double c0 = c->startBar;
        for (auto& [b, src] : w) b = (snapBar (c0 + b / 4.0) - c0) * 4.0;
        // keep them in order, one per grid line (the later of two on one line goes)
        std::vector<std::pair<double, double>> out; for (auto& m : w) if (m.first > 1.0e-9 && (out.empty() || m.first > out.back().first + 1.0e-9) && (out.empty() || m.second > out.back().second)) out.push_back (m);
        ctx.s.editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& x : v) if (x.id == r.id) { x.mode = 3; x.warp = out; } });
    }
    int gainDrag = -1;                      // its point being dragged
    std::unique_ptr<NoteEditor> noteEd, retiredEd;     // the note clip being edited (and the one just closed)
    /** a note clip's editor, over the lanes */
    void editNotes (ClipRef r)
    {
        auto* t = ctx.s.clipTrack (r); if (! t || r.audio) return;
        if (noteEd) { removeChildComponent (noteEd.get()); retiredEd = std::move (noteEd); }
        noteEd = std::make_unique<NoteEditor> (ctx.s, t->uid, r.id);
        // (closed from inside its own mouse / key handler: hidden now, deleted with the next editor or the page)
        noteEd->onClose = [this] { if (noteEd) { noteEd->setVisible (false); removeChildComponent (noteEd.get()); retiredEd = std::move (noteEd); repaint(); } };
        addAndMakeVisible (*noteEd); noteEd->setBounds (lanesArea().withTrimmedLeft (0)); noteEd->grabKeyboardFocus();
    }
    NoteEditor* noteEditor() { return noteEd.get(); }
    std::unique_ptr<SpectralEditor> specEd, retiredSpec;
    void editSpectrum (ClipRef r)
    {
        if (specEd) { removeChildComponent (specEd.get()); retiredSpec = std::move (specEd); }
        specEd = std::make_unique<SpectralEditor> (ctx.s, r);
        specEd->onClose = [this] { if (specEd) { specEd->setVisible (false); removeChildComponent (specEd.get()); retiredSpec = std::move (specEd); repaint(); } };
        addAndMakeVisible (*specEd); specEd->setBounds (lanesArea()); specEd->grabKeyboardFocus();
    }
    SpectralEditor* spectralEditor() { return specEd.get(); }
    juce::Array<juce::File> tplFiles;
    std::vector<std::pair<int, int>> envTargets;   // (device uid, parameter) offered by the clip menu
    int groupSize (int g) const { int n = 0; for (auto& t : ctx.s.tracks) if (t->group.load() == g) ++n; return n; }
    /** the header's menu: groups (folders, vcas) */
    void trackMenu (Track& t)
    {
        auto& s = ctx.s;
        juce::PopupMenu m;
        const int g = t.group.load(); const auto* gr = g ? s.groupOf (g) : nullptr;
        m.addItem (1, "new group with this track");
        juce::PopupMenu to; for (auto& x : *s.groups) to.addItem (100 + x.id, x.name, x.id != g, x.id == g);
        m.addSubMenu ("into the group", to, ! s.groups->empty());
        m.addSeparator();
        if (! t.isMaster() && ! t.isReturn()) m.addItem (t.isFrozen() ? 7 : 6, t.isFrozen() ? "unfreeze (live again)" : "freeze (render its chain; no cpu)");
        {
            juce::PopupMenu tb; const int cur = t.timebase.load();
            tb.addItem (40, "the project's (" + juce::String (s.timebase.load() == 1 ? "time" : "beats") + ")", true, cur < 0); tb.addItem (41, "beats: its clips move with the tempo", true, cur == 0); tb.addItem (42, "time: its clips keep their seconds", true, cur == 1);
            m.addSubMenu ("timebase", tb);
        }
        if (t.instrument && dynamic_cast<LoopLengths*> (t.instrument->processor())) m.addItem (10, "tape loops: when do they line up?");
        m.addItem (8, "save as a track template...");
        if (! t.isMaster() && ! t.isReturn()) m.addItem (9, "spectral freeze at the playhead (an endless pad on a new track)");
        {
            juce::PopupMenu tl; int k = 0; tplFiles.clear();
            for (const auto& f : Session::trackTemplatesDir().findChildFiles (juce::File::findFiles, false, "*.ftrack")) { tplFiles.add (f); tl.addItem (500 + k++, f.getFileNameWithoutExtension()); }
            m.addSubMenu ("new track from a template", tl, k > 0);
        }
        m.addSeparator();
        if (gr)
        {
            m.addItem (2, "out of " + gr->name);
            m.addItem (3, gr->folded ? "unfold " + gr->name : "fold " + gr->name);
            m.addItem (4, "rename " + gr->name + "...");
            m.addItem (5, "ungroup " + gr->name + " (every track)");
        }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, &t, g] (int r)
        {
            auto& s = ctx.s;
            if (r == 1) { const int id = s.addGroup ("group " + juce::String (s.groups->size() + 1)); s.editGroups ([id, &t, this] (std::vector<Group>& v) { for (auto& x : v) if (x.id == id) x.colour = trackColour (t).getARGB(); }); s.setTrackGroup (t, id); }
            else if (r == 2) s.setTrackGroup (t, 0);
            else if (r == 3) s.editGroups ([g] (std::vector<Group>& v) { for (auto& x : v) if (x.id == g) x.folded = ! x.folded; });
            else if (r == 4) { const auto* gr = s.groupOf (g); ask ("rename the group", "a name", gr ? gr->name : juce::String(), [this, g] (juce::String n) { ctx.s.editGroups ([g, n] (std::vector<Group>& v) { for (auto& x : v) if (x.id == g) x.name = n.trim(); }); repaint(); }); }
            else if (r == 5) s.removeGroup (g);
            else if (r == 6) { juce::String err; ctx.status ("freezing " + t.name + "..."); if (s.freezeTrack (t, err)) ctx.status (t.name + " frozen: its file plays (unfreeze to change it)"); else ctx.status ("freeze: " + err); }
            else if (r == 7) { s.unfreezeTrack (t); ctx.status (t.name + " live again"); }
            else if (r >= 40 && r <= 42) { t.timebase.store (r - 41); s.dirty = true; ctx.status (t.name + ": " + (r == 40 ? juce::String ("the project's timebase") : r == 41 ? juce::String ("beats") : juce::String ("time (its clips keep their seconds)"))); }
            else if (r == 10)
            {
                auto* ll = dynamic_cast<LoopLengths*> (t.instrument->processor()); const auto lens = ll->loopLengths();
                auto when = [] (double v) { return v < 0 ? juce::String ("not within a day") : v < 3600 ? timeline::formatTime (v) + " (" + juce::String (v, 1) + " s)" : juce::String (v / 3600.0, 2) + " hours"; };
                const auto sc = scanLoops (lens);
                juce::String msg; msg << "the loops playing: "; for (size_t k = 0; k < lens.size(); ++k) msg << juce::String (lens[k], 2) << " s  ";
                msg << "\n\nall of them start together again after: " << when (sc.all) << "\n\npairs:\n";
                for (auto& [a, b, v] : sc.pairs) msg << "  " << juce::String (lens[(size_t) a], 2) << " s and " << juce::String (lens[(size_t) b], 2) << " s: " << when (v) << "\n";
                msg << "\n(within 10 ms; the loops are counted from the start of the song)";
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "tape loops", msg);
            }
            else if (r == 9) { juce::String err; if (auto* nt = s.spectralFreeze (t, s.currentBar(), 16.0, err)) { ctx.select (nt); ctx.status ("frozen spectrum of " + t.name + " at " + pos (s.currentBar()) + ": a looping pad on " + nt->name); } else ctx.status ("spectral freeze: " + err); }
            else if (r == 8) ask ("save as a track template", "a name", t.name, [this, &t] (juce::String n) { juce::String err; if (ctx.s.saveTrackTemplate (t, n.trim(), err)) ctx.status ("track template saved: " + n.trim()); else ctx.status (err); });
            else if (r >= 500 && r - 500 < tplFiles.size()) { juce::String err; if (auto* nt = s.addTrackFromTemplate (tplFiles[r - 500], err)) { ctx.select (nt); ctx.status ("new track: " + nt->name + (err.isNotEmpty() ? " (" + err + ")" : juce::String())); } else ctx.status (err); }
            else if (r >= 100) s.setTrackGroup (t, r - 100);
            repaint();
        });
    }

    // ---- paint --------------------------------------------------------------------------------------------
    /** every line of a ladder (in bars) between two bars */
    template <typename Fn> void eachLine (double b0, double b1, double step, Fn fn) const
    {
        if (step <= 0) return;
        const long long k0 = (long long) std::floor (b0 / step), k1 = (long long) std::ceil (b1 / step);
        if (k1 - k0 > 4000) return;
        for (long long k = std::max (0LL, k0); k <= k1; ++k) fn ((double) k * step, k);
    }
    /** the bars (every step-th) whose lines fall between two positions, in units of 4 beats: fn (bar index, start) */
    template <typename Fn> void eachBar (double b0, double b1, long long step, Fn fn) const
    {
        const auto& m = meter();
        long long k = m.barAt (std::max (0.0, b0 * 4.0)); k = (k / step) * step;
        for (int guard = 0; guard < 4000; ++guard, k += step) { const double st = m.barStart (k) / 4.0; if (st > b1) break; fn (k, st); }
    }
    /** every beat (the denominator's note) and every grid line, restarting at each bar: fn (position, kind 0 grid 1 beat 2 group) */
    template <typename Fn> void eachLine (double b0, double b1, bool beats, double grid, Fn fn) const
    {
        const auto& m = meter();
        eachBar (b0, b1, 1, [&] (long long k, double st)
        {
            const auto& sg = m.sigOf (k); const double e = m.barStart (k + 1) / 4.0, bl = sg.beatLen() / 4.0;
            if (grid > 0) for (int i = 1; st + i * grid < e - 1.0e-9 && i < 4096; ++i) { const double q = st + i * grid; if (q >= b0 && q <= b1) fn (q, 0); }
            if (beats) for (int i = 1; st + i * bl < e - 1.0e-9 && i < 256; ++i) { const double q = st + i * bl; if (q >= b0 && q <= b1) fn (q, sg.groupStart (i) ? 2 : 1); }
        });
    }
    /** bars between lines (a power of two) so lines are at least px apart */
    long long barStep (double px) const { const double avg = meter().sigAt (scrollBar * 4.0).beats() / 4.0 * pxPerBar; long long st = 1; while (st * avg < px && st < 4096) st *= 2; return st; }
    /** a row of time labels (min:sec, samples or timecode) at round steps, with ticks: y the label's top */
    void paintTimeRow (juce::Graphics& g, int format, int y, bool main, const std::vector<juce::Rectangle<int>>& taken)
    {
        auto& s = ctx.s; const auto la = lanesArea();
        const double b0 = bar (left), b1 = bar (la.getRight());
        const double t0 = s.secondsAt (b0 * 4.0), t1 = s.secondsAt (b1 * 4.0);
        double ts = timeline::adaptiveSeconds (pxPerSecond(), main ? 70.0 : 64.0);
        if (format == timeline::RULER_TIMECODE) { const double fr = 1.0 / std::max (1.0f, s.timecodeFps.load()); if (ts < 1.0) ts = std::max (fr, std::ceil (ts / fr) * fr); }
        g.setFont (uiFont (main ? 9.5f : 8.5f));
        for (double t = std::floor (t0 / ts) * ts; t <= t1 + ts; t += ts)
        {
            if (t < -1.0e-9) continue;
            const int px = x (s.beatAt (t) / 4.0); if (px < left) continue;
            g.setColour (theme.ink.withAlpha (main ? 0.7f : 0.55f)); g.fillRect (px, main ? ruler - 10 : top + 26, 1, main ? 10 : 4);
            juce::String lbl = format == timeline::RULER_SAMPLES ? timeline::formatSamples (t, s.sampleRate()) : format == timeline::RULER_TIMECODE ? timeline::formatTimecode (t, s.timecodeFps.load()) : timeline::formatTime (t, ts < 1.0);
            const juce::Rectangle<int> r (px + 3, y, (int) lbl.length() * 6 + 2, main ? 12 : 11);
            if (std::any_of (taken.begin(), taken.end(), [&] (const juce::Rectangle<int>& k) { return k.intersects (r); })) continue;
            g.setColour (main ? theme.ink : theme.ink.withAlpha (0.55f)); g.drawText (lbl, r.withWidth (110), juce::Justification::centredLeft);
        }
        if (main)
        {
            // the finer grid's ticks between the labels
            const double gs = gridSec();
            if (gs * pxPerSecond() >= 5) for (double t = std::floor (t0 / gs) * gs; t <= t1 + gs; t += gs) { if (t < 0) continue; const int px = x (s.beatAt (t) / 4.0); g.setColour (theme.ink.withAlpha (0.3f)); g.fillRect (px, ruler - 3, 1, 3); }
        }
    }
    void paintRuler (juce::Graphics& g)
    {
        if (! timeMode()) { paintBarRuler (g); return; }
        // time first: the main ruler's labels on the top row, the second ruler (bars, or another time) below, then the markers
        auto& s = ctx.s; const auto la = lanesArea(); const auto& m = meter();
        const double b0 = bar (left), b1 = bar (la.getRight());
        g.setColour (theme.ink.withAlpha (0.04f)); g.fillRect (left, top, la.getWidth() - left, ruler - top);
        paintTimeRow (g, s.rulerMain.load(), top + 1, true, {});
        std::vector<juce::Rectangle<int>> taken;
        for (auto& mk : *s.markers) { const int px = x (mk.bar); taken.push_back ({ px - 2, top + 14, std::min (110, 8 + (int) (mk.name.length() * 6)) + 4, 11 }); }
        const int sub = s.rulerSub.load();
        if (sub == timeline::RULER_BARS)
        {
            g.setFont (uiFont (8.5f));
            eachBar (b0 - 1, b1, barStep (44), [&] (long long k, double st)
            {
                if (st < b0 - 1.0e-9) return; const int px = x (st);
                g.setColour (theme.ink.withAlpha (0.45f)); g.fillRect (px, top + 26, 1, 4);
                const juce::Rectangle<int> r (px + 3, top + 14, 40, 11);
                if (std::any_of (taken.begin(), taken.end(), [&] (const juce::Rectangle<int>& q) { return q.intersects (r); })) return;
                g.setColour (theme.ink.withAlpha (0.55f)); g.drawText ("bar " + juce::String (k + 1), r.withWidth (60), juce::Justification::centredLeft);
            });
            (void) m;
        }
        else if (sub != timeline::RULER_NONE && sub != s.rulerMain.load()) paintTimeRow (g, sub, top + 14, false, taken);
        if (s.loopOn.load()) { g.setColour (theme.fill.withAlpha (0.7f)); g.fillRect (x (s.loopStart.load()), ruler - 4, std::max (2, x (s.loopEnd.load()) - x (s.loopStart.load())), 3); }
        for (auto& mk : *s.markers)
        {
            const int px = x (mk.bar); if (px < left || px > la.getRight()) continue;
            g.setColour (hotColour()); g.fillRect (px, top, 1, ruler - top);
            g.setFont (uiFont (9.0f)); const int w = std::min (110, 8 + (int) (mk.name.length() * 6));
            g.setColour (theme.ground.withAlpha (0.85f)); g.fillRect (px + 1, top + 14, w, 11);
            g.setColour (hotColour()); g.drawText (mk.name, px + 3, top + 14, w - 2, 11, juce::Justification::centredLeft, true);
        }
    }
    void paintBarRuler (juce::Graphics& g)
    {
        auto& s = ctx.s;
        const auto& m = meter();
        const auto la = lanesArea();
        const double b0 = bar (left), b1 = bar (la.getRight());
        g.setColour (theme.ink.withAlpha (0.04f)); g.fillRect (left, top, la.getWidth() - left, ruler - top);
        // ticks: the grid, the beats (stronger on a group's first), the bars
        const double gb = gridBars(), beatPx = m.sigAt (scrollBar * 4.0).beatLen() / 4.0 * pxPerBar;
        eachLine (b0, b1, beatPx >= 5, gb < 1.0 - 1.0e-9 && gb * pxPerBar >= 4 ? gb : 0.0, [&] (double q, int kind)
        {
            g.setColour (theme.ink.withAlpha (kind == 0 ? 0.3f : kind == 2 ? 0.6f : 0.45f)); const int h = kind == 0 ? 3 : kind == 2 ? 8 : 6; g.fillRect (x (q), ruler - h, 1, h);
        });
        eachBar (b0, b1, barStep (5), [&] (long long, double st) { g.setColour (theme.ink.withAlpha (0.7f)); g.fillRect (x (st), ruler - 10, 1, 10); });
        // labels: bars (every few when far out), then beats, sixteenths and ticks as there is room
        const long long ls = barStep (44);
        const double beatW = beatPx, sixW = pxPerBar / 16.0;
        g.setFont (uiFont (9.5f));
        std::vector<juce::Rectangle<int>> tags;   // the signatures' tags: no label under them
        for (auto& c : m.getChanges())
        {
            const int px = x (m.barStart (c.bar) / 4.0), tx = std::max (left, px + 8 + 6 * (int) juce::String (c.bar + 1).length());
            tags.push_back ({ tx - 2, top + 1, 14 + (int) (timeline::sigsText (c.sigs) + (c.sigs.size() > 1 ? " ..." : "")).length() * 6, 12 });
        }
        auto label = [&] (double q, int depth)
        {
            const int px = x (q); if (px < left - 1) return;
            if (std::any_of (tags.begin(), tags.end(), [&] (const juce::Rectangle<int>& k) { return k.intersects (juce::Rectangle<int> (px + 2, top + 1, 14, 12)); })) return;
            auto txt = pos (q);
            auto parts = juce::StringArray::fromTokens (txt, ".", "");
            parts.removeRange (depth, 4);
            g.setColour (theme.ink); g.drawText (parts.joinIntoString ("."), px + 3, top + 1, 90, 12, juce::Justification::centredLeft);
        };
        eachBar (b0 - 1, b1, ls, [&] (long long, double st) { if (st >= b0 - 1.0e-9) label (st, 1); });
        if (ls == 1 && beatW >= 46) eachLine (b0, b1, true, sixW >= 70 ? 0.0625 : 0.0, [&] (double q, int kind) { label (q, kind == 0 ? 3 : 2); });
        if (ls == 1 && sixW >= 900) eachLine (b0, b1, false, 1.0 / 256.0, [&] (double q, int) { label (q, 4); });
        // the time signatures, where they change
        for (auto& c : m.getChanges())
        {
            const double st = m.barStart (c.bar) / 4.0; const int px = x (st);
            if (px < left - 60 || px > la.getRight()) continue;
            const auto txt = timeline::sigsText (c.sigs) + (c.sigs.size() > 1 ? " ..." : "");
            // on the bar numbers' row, just after the number (markers and times use the row below)
            const int w = 10 + (int) txt.length() * 6, tx = std::max (left, px + 8 + 6 * (int) juce::String (c.bar + 1).length());
            g.setColour (theme.fill.withAlpha (0.9f)); g.fillRect (tx, top + 1, w, 12);
            g.setColour (ui::inkFor (theme.fill)); g.setFont (uiFont (9.0f, true)); g.drawText (txt, tx + 4, top + 1, w - 4, 12, juce::Justification::centredLeft);
        }
        // the second ruler: time (or samples, timecode) at round numbers, under the bars
        const int subF = s.rulerSub.load();
        if (subF == timeline::RULER_SAMPLES || subF == timeline::RULER_TIMECODE)
        {
            std::vector<juce::Rectangle<int>> tk;
            for (auto& mk : *s.markers) { const int px = x (mk.bar); tk.push_back ({ px - 2, top + 14, std::min (110, 8 + (int) (mk.name.length() * 6)) + 4, 11 }); }
            paintTimeRow (g, subF, top + 14, false, tk);
        }
        const double t0 = subF == timeline::RULER_TIME ? s.secondsAt (b0 * 4.0) : 1.0e12, t1 = s.secondsAt (b1 * 4.0);
        const double pxPerSec = (t1 > t0 ? (b1 - b0) / (t1 - t0) : 0.5) * pxPerBar;
        static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
        double ts = 600; for (double st : steps) if (st * pxPerSec >= 64) { ts = st; break; }
        g.setFont (uiFont (8.5f)); g.setColour (theme.ink.withAlpha (0.55f));
        std::vector<juce::Rectangle<int>> taken;   // markers' names and signatures: a time label never hides under one
        for (auto& mk : *s.markers) { const int px = x (mk.bar); taken.push_back ({ px - 2, top + 14, std::min (110, 8 + (int) (mk.name.length() * 6)) + 4, 11 }); }
        for (double t = std::floor (t0 / ts) * ts; t <= t1 + ts; t += ts)
        {
            if (t < 0) continue;
            const int px = x (s.beatAt (t) / 4.0); if (px < left) continue;
            g.setColour (theme.ink.withAlpha (0.55f)); g.fillRect (px, top + 26, 1, 4);
            const auto lbl = timeline::formatTime (t, ts < 1.0);
            const juce::Rectangle<int> r (px + 3, top + 14, (int) lbl.length() * 6 + 2, 11);
            if (std::any_of (taken.begin(), taken.end(), [&] (const juce::Rectangle<int>& k) { return k.intersects (r); })) continue;
            g.drawText (lbl, r.withWidth (90), juce::Justification::centredLeft);
        }
        // the loop and the markers
        if (s.loopOn.load()) { g.setColour (theme.fill.withAlpha (0.7f)); g.fillRect (x (s.loopStart.load()), ruler - 4, std::max (2, x (s.loopEnd.load()) - x (s.loopStart.load())), 3); }
        for (auto& mk : *s.markers)
        {
            const int px = x (mk.bar); if (px < left || px > la.getRight()) continue;
            g.setColour (hotColour()); g.fillRect (px, top, 1, ruler - top);
            g.setFont (uiFont (9.0f)); const int w = std::min (110, 8 + (int) (mk.name.length() * 6));
            g.setColour (theme.ground.withAlpha (0.85f)); g.fillRect (px + 1, top + 14, w, 11);
            g.setColour (hotColour()); g.drawText (mk.name, px + 3, top + 14, w - 2, 11, juce::Justification::centredLeft, true);
        }
    }
    void paint (juce::Graphics& g) override
    {
        auto& s = ctx.s;
        const auto la = lanesArea();
        paintSong (g);
        juce::Graphics::ScopedSaveState clipAll (g);
        g.reduceClipRegion (0, 0, la.getWidth(), getHeight());
        // the toolbar's readout: the grid, and where the mouse is
        snapLink.setLit (s.snap.load()); followLink.setLit (s.follow.load());
        gridLink.setText ("grid " + (timeMode() ? (s.timeGridIndex.load() == 0 ? "adaptive (" + timeGridLabel() + ")" : juce::String (timeline::timeGrids()[(size_t) s.timeGridIndex.load()].name)) : timeline::gridName (s.gridIndex.load(), pxPerBeat())));
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.8f));
        if (hoverBar >= 0) g.drawText (pos (hoverBar) + "   " + s.formatRuler (s.rulerSub.load() == timeline::RULER_NONE ? (timeMode() ? 0 : 1) : s.rulerSub.load(), hoverBar * 4.0), la.getRight() - 260, 3, 252, 16, juce::Justification::centredRight);
        {
            juce::Graphics::ScopedSaveState rs (g); g.reduceClipRegion (left, top, la.getWidth() - left, ruler - top);
            paintRuler (g);
        }
        // grid lines behind the lanes
        {
            juce::Graphics::ScopedSaveState gs (g); g.reduceClipRegion (left, la.getY(), la.getWidth() - left, la.getHeight());
            const double b0 = bar (left), b1 = bar (la.getRight()), gb = gridBars();
            const double beatPx = meter().sigAt (scrollBar * 4.0).beatLen() / 4.0 * pxPerBar;
            if (timeMode())
            {
                const double t0 = s.secondsAt (b0 * 4.0), t1 = s.secondsAt (b1 * 4.0), gs = gridSec(), big = timeline::adaptiveSeconds (pxPerSecond(), 70.0);
                if (gs * pxPerSecond() >= 5) for (double t = std::floor (t0 / gs) * gs; t <= t1 + gs; t += gs) if (t >= 0) { g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (x (s.beatAt (t) / 4.0), la.getY(), 1, la.getHeight()); }
                for (double t = std::floor (t0 / big) * big; t <= t1 + big; t += big) if (t >= 0) { g.setColour (theme.ink.withAlpha (0.18f)); g.fillRect (x (s.beatAt (t) / 4.0), la.getY(), 1, la.getHeight()); }
            }
            else eachLine (b0, b1, beatPx >= 6, gb < 1.0 - 1.0e-9 && gb * pxPerBar >= 5 ? gb : 0.0, [&] (double q, int kind) { g.setColour (theme.ink.withAlpha (kind == 0 ? 0.05f : kind == 2 ? 0.14f : 0.09f)); g.fillRect (x (q), la.getY(), 1, la.getHeight()); });
            if (! timeMode()) eachBar (b0, b1, barStep (5), [&] (long long, double st) { g.setColour (theme.ink.withAlpha (0.2f)); g.fillRect (x (st), la.getY(), 1, la.getHeight()); });
            if (s.loopOn.load()) { g.setColour (theme.fill.withAlpha (0.06f)); g.fillRect (x (s.loopStart.load()), la.getY(), std::max (1, x (s.loopEnd.load()) - x (s.loopStart.load())), la.getHeight()); }
        }
        // lanes
        auto rs = rows();
        for (int i = 0; i < (int) rs.size(); ++i)
        {
            auto& t = *rs[(size_t) i];
            const int y = rowY (i);
            if (y + laneH < la.getY() || y > la.getBottom()) continue;
            juce::Graphics::ScopedSaveState rowClip (g);
            g.reduceClipRegion (0, la.getY(), la.getWidth(), la.getHeight());
            const bool selT = ctx.selected() == &t;
            g.setColour (theme.ink.withAlpha (selT ? 0.12f : 0.05f)); g.fillRect (0, y, left, laneH - 1);
            g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (left, y + laneH - 1, la.getWidth() - left, 1);
            g.setColour (trackColour (t)); g.fillRect (0, y, 4, laneH - 1);
            const int tg = t.group.load(); const auto* tgr = tg ? s.groupOf (tg) : nullptr;
            if (tgr)
            {
                // the group: a stripe in its colour, a triangle that folds it (the first track of a folded group is its folder)
                g.setColour (tgr->colour ? juce::Colour (tgr->colour) : theme.ink.withAlpha (0.5f)); g.fillRect (5, y, 3, laneH - 1);
                juce::Path tri; const float tx = (float) left - 14.0f, ty = (float) y + 6.0f;
                if (tgr->folded) tri.addTriangle (tx, ty, tx + 7.0f, ty + 4.0f, tx, ty + 8.0f); else tri.addTriangle (tx, ty, tx + 8.0f, ty, tx + 4.0f, ty + 7.0f);
                g.setColour (theme.ink.withAlpha (0.8f)); g.fillPath (tri);
            }
            g.setColour (theme.ink); g.setFont (uiFont (11.0f, selT));
            if (t.isFrozen() && laneH >= 22) { g.setColour (coolColour()); g.setFont (uiFont (8.5f)); g.drawText ("frozen", left - 54, y + laneH - 14, 40, 12, juce::Justification::centredRight); g.setColour (theme.ink); g.setFont (uiFont (11.0f, selT)); }
            if (tgr && tgr->folded) g.drawText (tgr->name + "  (" + juce::String (groupSize (tg)) + " tracks)", 12, y + 2, left - 30, std::min (16, laneH - 2), juce::Justification::centredLeft, true);
            else g.drawText (t.name, 12, y + 2, left - (tgr ? 30 : 14), std::min (16, laneH - 2), juce::Justification::centredLeft, true);
            if (laneH >= 34) { g.setFont (uiFont (9.0f)); g.setColour (theme.ink.withAlpha (0.7f)); g.drawText (tgr && tgr->folded ? juce::String ("folded: ") + t.name + " ..." : (tgr ? tgr->name + " / " : juce::String()) + (t.isAudio() ? "audio" : (t.instrument ? t.instrument->name : "")), 12, y + 18, left - 16, 14, juce::Justification::centredLeft, true); }
            g.reduceClipRegion (left, la.getY(), la.getWidth() - left, la.getHeight());
            const double vis0 = bar (left), vis1 = bar (la.getRight());
            for (auto& c : *t.clips)
            {
                if (c.startBar + c.bars < vis0 || c.startBar > vis1) continue;
                auto r = juce::Rectangle<float> ((float) xd (c.startBar), (float) y + 3.0f, (float) (c.bars * pxPerBar) - 1.0f, (float) laneH - 7.0f);
                r = r.getIntersection (juce::Rectangle<float> ((float) left - 4.0f, (float) y, (float) la.getWidth() + 8.0f, (float) laneH));
                const bool live = ! c.muted && s.mode.load() == (int) PlayMode::Arrange && t.shownPattern.load() == c.pattern && s.currentBar() >= c.startBar && s.currentBar() < c.startBar + c.bars && s.playPpq.load() >= 0;
                const auto cc = c.colour ? juce::Colour (c.colour) : trackColour (t);
                g.setColour (cc.withAlpha (c.muted ? 0.18f : live ? 1.0f : 0.6f));
                if (theme.mirror) g.fillRect (r); else g.fillPath (ui::skewRect (r, 0.15f));
                if (isSelected ({ t.uid, c.id, false })) { g.setColour (brightColour()); g.drawRect (r, 2.0f); }
                if (c.notes && ! c.notes->empty() && r.getHeight() > 12)
                {
                    // its notes, small: the clip's pitch range top to bottom
                    int lo = 127, hi = 0; for (auto& nt : *c.notes) { lo = std::min (lo, nt.pitch); hi = std::max (hi, nt.pitch); }
                    const float top = r.getY() + 13.0f, h = std::max (4.0f, r.getHeight() - 16.0f), span = (float) std::max (1, hi - lo + 1);
                    g.setColour (ui::inkFor (cc).withAlpha (c.muted ? 0.3f : 0.85f));
                    for (auto& nt : *c.notes)
                    {
                        const double b0 = c.startBar + (nt.beat - c.offset) / 4.0, b1 = b0 + nt.len / 4.0;
                        if (b1 <= c.startBar || b0 >= c.startBar + c.bars) continue;
                        const float nx0 = (float) xd (std::max (b0, c.startBar)), nx1 = (float) xd (std::min (b1, c.startBar + c.bars));
                        g.fillRect (nx0, top + h - (float) (nt.pitch - lo + 1) / span * h, std::max (1.0f, nx1 - nx0 - 1.0f), std::max (1.5f, h / span - 0.5f));
                    }
                }
                g.setColour (ui::inkFor (cc).withAlpha (c.muted ? 0.5f : 1.0f)); g.setFont (uiFont (10.0f));
                juce::String label = c.notes ? (c.name.isNotEmpty() ? c.name : juce::String ("notes")) + " (" + juce::String (c.notes->size()) + ")"
                                             : c.name.isNotEmpty() ? c.name + "  p" + juce::String (c.pattern + 1) : "p" + juce::String (c.pattern + 1); if (c.offset > 1.0e-9) label << " +" << timeline::formatLength (c.offset).fromFirstOccurrenceOf (".", false, false); if (c.muted) label << " (muted)";
                g.drawText (label, r.reduced (4.0f, 0.0f).withHeight (std::min (r.getHeight(), 16.0f)), juce::Justification::centredLeft, true);
                if (! c.envs.empty()) paintEnvs (g, { t.uid, c.id, false });
            }
            for (auto& c : *t.aclips)
            {
                if (c.startBar + c.bars < vis0 || c.startBar > vis1) continue;
                // a quiet take under the one that plays: only its badge shows (alt+up / down, or the clip's menu)
                int takeN = 0, takeK = 0;
                if (c.takeGroup)
                {
                    bool covered = false;
                    for (auto& o : *t.aclips) if (o.takeGroup == c.takeGroup && &o != &c && o.startBar < c.startBar + c.bars - 1.0e-9 && o.startBar + o.bars > c.startBar + 1.0e-9) { ++takeN; if (! o.muted && c.muted) covered = true; }
                    ++takeN; takeK = c.take;
                    if (covered) continue;
                }
                auto full = juce::Rectangle<float> ((float) xd (c.startBar), (float) y + 3.0f, (float) (c.bars * pxPerBar) - 1.0f, (float) laneH - 6.0f);
                auto r = full.getIntersection (juce::Rectangle<float> ((float) left - 4.0f, (float) y, (float) la.getWidth() + 8.0f, (float) laneH));
                const auto cc = c.colour ? juce::Colour (c.colour) : trackColour (t);
                g.setColour (cc.withAlpha (c.muted ? 0.12f : 0.35f)); g.fillRect (r);
                if (! c.muted) drawWave (g, c, full, r, ui::inkFor (theme.ground));
                g.setColour (isSelected ({ t.uid, c.id, true }) ? brightColour() : cc); g.drawRect (r, isSelected ({ t.uid, c.id, true }) ? 2.0f : 1.0f);
                // the fades, and their handles at the top corners
                const double secPerBar = 4.0 * 60.0 / s.tempo();
                const float fi = (float) (c.fadeIn / secPerBar * pxPerBar), fo = (float) (c.fadeOut / secPerBar * pxPerBar);
                g.setColour (theme.ink.withAlpha (0.8f));
                if (fi > 1) g.drawLine (full.getX(), full.getBottom(), full.getX() + fi, full.getY(), 1.0f);
                if (fo > 1) g.drawLine (full.getRight() - fo, full.getY(), full.getRight(), full.getBottom(), 1.0f);
                if (laneH >= 30) { g.fillRect (full.getX() + fi - 2.0f, full.getY(), 4.0f, 4.0f); g.fillRect (full.getRight() - fo - 2.0f, full.getY(), 4.0f, 4.0f); }
                // crossfades with the clips it overlaps: the curves over the overlap
                if (! c.muted && (c.xIn > 0 || c.xOut > 0))
                {
                    g.setColour (hotColour().withAlpha (0.9f));
                    auto curve = [&] (float xa, float xb, bool rising)
                    {
                        juce::Path pth; for (int k = 0; k <= 16; ++k) { const float u = k / 16.0f; const float v = std::sin (juce::MathConstants<float>::halfPi * (rising ? u : 1.0f - u)); const float px = xa + (xb - xa) * u, py = full.getBottom() - v * full.getHeight(); if (k == 0) pth.startNewSubPath (px, py); else pth.lineTo (px, py); }
                        g.strokePath (pth, juce::PathStrokeType (1.2f));
                    };
                    if (c.xIn > 0) curve (full.getX(), (float) xd (c.startBar + c.xIn), true);
                    if (c.xOut > 0) curve ((float) xd (c.startBar + c.bars - c.xOut), full.getRight(), false);
                }
                // warp markers: a line and a handle each (brighter while they are being set)
                if (c.mode == 3 && (! c.warp.empty() || warpClip == ClipRef { t.uid, c.id, true }))
                {
                    const bool wEdit = warpClip == ClipRef { t.uid, c.id, true };
                    for (auto& [wb, ws] : c.warp)
                    {
                        const float wx = (float) xd (c.startBar + wb / 4.0);
                        if (wx < r.getX() || wx > r.getRight()) continue;
                        g.setColour (coolColour().withAlpha (wEdit ? 0.95f : 0.5f)); g.fillRect (wx, full.getY(), 1.0f, full.getHeight());
                        juce::Path tri; tri.addTriangle (wx - 4.0f, full.getY(), wx + 4.0f, full.getY(), wx, full.getY() + 6.0f); g.fillPath (tri);
                    }
                }
                // the gain line (db: +12 at the top, -36 at the bottom), its points while it is being drawn
                const bool gEdit = gainClip == ClipRef { t.uid, c.id, true };
                if (! c.gainLine.empty() || gEdit)
                {
                    juce::Graphics::ScopedSaveState gs (g); g.reduceClipRegion (r.toNearestInt());
                    g.setColour (warmColour()); juce::Path pth;
                    if (c.gainLine.empty()) { const float gy = gainY (full, 0.0f); pth.startNewSubPath (full.getX(), gy); pth.lineTo (full.getRight(), gy); }
                    else
                    {
                        pth.startNewSubPath (full.getX(), gainY (full, c.gainLine.front().second));
                        for (auto& [gb, db] : c.gainLine) pth.lineTo ((float) xd (c.startBar + gb / 4.0), gainY (full, db));
                        pth.lineTo (full.getRight(), gainY (full, c.gainLine.back().second));
                    }
                    g.strokePath (pth, juce::PathStrokeType (gEdit ? 1.6f : 1.0f));
                    if (gEdit) for (auto& [gb, db] : c.gainLine) { const float px = (float) xd (c.startBar + gb / 4.0), py = gainY (full, db); g.fillEllipse (px - 3.0f, py - 3.0f, 6.0f, 6.0f); }
                }
                g.setColour (theme.ink.withAlpha (c.muted ? 0.5f : 1.0f)); g.setFont (uiFont (9.0f));
                juce::String label = c.name.isNotEmpty() ? c.name : juce::File (c.path).getFileNameWithoutExtension();
                if (takeN > 1) label << "  take " << takeK << " of " << takeN;
                if (std::abs (c.gain - 1.0f) > 1.0e-4f) label << "  " << juce::String (juce::Decibels::gainToDecibels (c.gain), 1) << " db";
                if (c.muted) label << " (muted)";
                g.drawText (label, r.reduced (5.0f, 0.0f).withHeight (12.0f).translated (0.0f, 4.0f), juce::Justification::centredLeft, true);
                if (! c.envs.empty()) paintEnvs (g, { t.uid, c.id, true });
            }
        }
        // the band being drawn
        if (dragMode == DragMode::Band) { g.setColour (brightColour().withAlpha (0.12f)); g.fillRect (bandRect()); g.setColour (brightColour().withAlpha (0.7f)); g.drawRect (bandRect(), 1); }
        // the horizontal scroll bar: the view against the whole project
        {
            const auto sa = scrollArea();
            const double total = std::max (s.projectBars() + 8.0, scrollBar + visibleBars());
            g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (sa);
            const int tx = sa.getX() + (int) (sa.getWidth() * scrollBar / total), tw = std::max (12, (int) (sa.getWidth() * visibleBars() / total));
            g.setColour (theme.ink.withAlpha (dragMode == DragMode::Scroll ? 0.6f : 0.35f)); g.fillRect (tx, sa.getY() + 2, tw, sa.getHeight() - 4);
        }
        // playhead
        const double cb = s.currentBar();
        if (xd (cb) >= left && xd (cb) <= la.getRight()) { g.setColour (brightColour()); g.fillRect (x (cb), top, 1, la.getBottom() - top); }
        paintLanes (g);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (left - 1, top, 1, la.getBottom() - top);
        if (ctx.status && hoverBar >= 0) ctx.status (ui::U ("arrange: click a clip: select (shift: add) · drag: move (cmd: free, alt: copy) · edges: trim · top corners: fades · drag empty space: select · cmd+c x v d: copy cut paste duplicate · cmd+t: split · delete · = -: zoom · z: zoom to selection · ←→: move by the grid · [ ]: markers"));
    }
    void drawWave (juce::Graphics& g, const AudioClip& c, juce::Rectangle<float> full, juce::Rectangle<float> vis, juce::Colour ink)
    {
        if (! c.sample) return;
        const auto& d = c.sample->data; const int n = d.getNumSamples();
        if (n < 2 || full.getWidth() < 1) return;
        const double srcLen = c.mode == 0 ? std::min ((double) n, (c.bars * 4.0 * 60.0 / ctx.s.tempo()) * c.sample->rate * std::exp2 (c.pitch / 12.0)) : c.sourceSeconds() * c.sample->rate;
        const double perPx = srcLen / std::max (1.0f, full.getWidth());
        g.setColour (ink.withAlpha (0.8f));
        const float mid = full.getCentreY(), hh = full.getHeight() * 0.42f * std::min (2.0f, c.gain);
        const int i0 = std::max (0, (int) (vis.getX() - full.getX())), i1 = (int) (vis.getRight() - full.getX());
        const int stepPx = perPx < 1.0 ? 1 : 2;
        const double beatsW = c.bars * 4.0, availW = std::max (0.0, c.sourceSeconds());
        for (int i = i0; i < i1; i += stepPx)
        {
            const double off = c.offset * c.sample->rate;
            int a = (int) (off + perPx * i), b = (int) (off + perPx * (i + stepPx));
            if (c.mode == 3)
            {
                const double bA = (double) i / full.getWidth() * beatsW, bB = (double) (i + stepPx) / full.getWidth() * beatsW;
                a = (int) (off + Session::warpSrc (c, bA, beatsW, availW) * c.sample->rate); b = (int) (off + Session::warpSrc (c, bB, beatsW, availW) * c.sample->rate);
            }
            if (c.reverse) { a = (int) (off + srcLen - perPx * (i + stepPx)); b = (int) (off + srcLen - perPx * i); }
            if (a >= n || b < 0) continue;
            a = std::max (0, a); b = std::min (n, std::max (a + 1, b));
            float lo = 0, hi = 0;
            const int stride = std::max (1, (b - a) / 64);
            for (int k = a; k < b; k += stride) { const float v = d.getSample (0, k); lo = std::min (lo, v); hi = std::max (hi, v); }
            g.fillRect (full.getX() + (float) i, mid - hi * hh, (float) stepPx, std::max (1.0f, (hi - lo) * hh));
        }
    }
    // ---- automation lanes of the selected track ----------------------------------------------------
    std::vector<Target> laneTargets() const
    {
        std::vector<Target> v;
        auto* t = ctx.selected();
        for (auto& l : *ctx.s.lanes) if ((t && l->target.track == t->uid) || l->target.track == 0) v.push_back (l->target);
        return v;
    }
    void paintLanes (juce::Graphics& g)
    {
        auto& s = ctx.s;
        const auto aa = autoArea();
        g.setColour (theme.ground); g.fillRect (aa);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (aa.getX(), aa.getY(), aa.getWidth(), 1);
        auto targets = laneTargets();
        const int lh = 52;
        g.setFont (uiFont (10.0f));
        const int fits = std::max (1, (aa.getHeight() - 26 - 22) / lh);
        laneScroll = juce::jlimit (0, std::max (0, (int) targets.size() - fits), laneScroll);
        if ((int) targets.size() > fits) { g.setColour (theme.ink.withAlpha (0.7f)); g.drawText ("lanes " + juce::String (laneScroll + 1) + "-" + juce::String (laneScroll + fits) + " of " + juce::String (targets.size()) + "  (scroll)", 76, aa.getY() + 4, 220, 18, juce::Justification::centredLeft); }
        for (int i = laneScroll; i < (int) targets.size() && i < laneScroll + fits; ++i)
        {
            const int y = aa.getY() + 26 + (i - laneScroll) * lh;
            auto l = s.lane (targets[(size_t) i]);
            if (! l) continue;
            g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (0, y, aa.getWidth(), lh - 2);
            g.setColour (theme.ink); g.drawText (s.targetName (targets[(size_t) i]), 8, y + 2, left - 12, 14, juce::Justification::centredLeft, true);
            g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (l->enabled ? "on" : "off", 8, y + 18, 40, 14, juce::Justification::centredLeft);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (left, y, aa.getWidth() - left, lh);
            if (pxPerBeat() >= 6) eachLine (bar (left), bar (aa.getWidth()), true, 0.0, [&] (double q, int) { g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (x (q), y, 1, lh - 2); });
            juce::Path p;
            const double bStart = scrollBar, bEnd = bar (aa.getWidth());
            for (int px = left; px < aa.getWidth(); px += 2)
            {
                const double b = bar (px);
                const float v = l->valueAt (b * 4.0);
                const float yy = (float) y + (float) (lh - 6) - v * (float) (lh - 10);
                if (px == left) p.startNewSubPath ((float) px, yy); else p.lineTo ((float) px, yy);
            }
            g.setColour (hotColour()); g.strokePath (p, juce::PathStrokeType (1.5f));
            for (auto& pt : l->pts)
            {
                const double b = pt.beat / 4.0;
                if (b < bStart || b > bEnd) continue;
                const float px = (float) xd (b), yy = (float) y + (float) (lh - 6) - pt.value * (float) (lh - 10);
                const int ki = (int) (&pt - &l->pts[0]);
                const bool sp = selLane == targets[(size_t) i] && std::find (selPts.begin(), selPts.end(), ki) != selPts.end();
                g.setColour (brightColour()); g.fillEllipse (px - 3.0f, yy - 3.0f, 6.0f, 6.0f);
                if (sp) { g.setColour (hotColour()); g.drawEllipse (px - 6.0f, yy - 6.0f, 12.0f, 12.0f, 2.0f); }
            }
        }
        if (dragMode == DragMode::PointBand) { const auto r = juce::Rectangle<int> (bandA, bandB); g.setColour (brightColour().withAlpha (0.12f)); g.fillRect (r); g.setColour (brightColour().withAlpha (0.7f)); g.drawRect (r, 1); }
        if (targets.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (ui::U ("automation: right-click any control → automate it on the timeline, or + lane · click: a point · shift-drag: draw freehand · alt-drag a point: its curve · cmd-click / cmd-drag: select points (drag them together, right-click for more)"), left + 8, aa.getY() + 6, aa.getWidth() - left - 16, 16, juce::Justification::centredLeft, true); }
    }
    void laneMenu()
    {
        auto* t = ctx.selected();
        if (! t) return;
        juce::PopupMenu m;
        int id = 1; std::vector<Target> list;
        m.addItem (id++, "tempo (ramps and changes over the song)"); list.push_back ({ 0, 0, GP_TEMPO }); m.addSeparator();
        auto addDev = [&] (Device& d, const juce::String& name)
        {
            juce::PopupMenu sub;
            for (int i = 0; i < std::min (d.numParams(), 120); ++i) { sub.addItem (id++, d.paramName (i)); list.push_back (ctx.s.targetFor (*t, d, i)); }
            m.addSubMenu (name, sub);
        };
        if (! t->isMaster()) addDev (*t->mixer, "strip");
        if (t->instrument) addDev (*t->instrument, t->instrument->name);
        for (auto& d : t->inserts) addDev (*d, d->name);
        juce::PopupMenu glob; for (int i = 0; i < GP_COUNT; ++i) { glob.addItem (id++, globalParamName (i)); list.push_back ({ 0, 0, i }); } m.addSubMenu ("global (macros, tempo, swing, master)", glob);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, list] (int r)
        {
            if (r <= 0 || r > (int) list.size()) return;
            const Target tg = list[(size_t) r - 1];
            auto* d = ctx.s.resolve (tg);
            if (d && ! ctx.s.lane (tg)) ctx.s.editLane (tg, [&] (AutoLane& l) { l.pts = { { 0.0, d->baseValue (tg.param), 0.0f } }; });
            repaint();
        });
    }
    void paintSong (juce::Graphics& g)
    {
        auto& s = ctx.s;
        const auto sa = songArea();
        g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (sa);
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText ("song", sa.getX() + 8, sa.getY() + 30, 80, 16, juce::Justification::centredLeft);
        g.setFont (uiFont (10.0f));
        double b = 0;
        const int cur = s.currentScene.load();
        for (int i = 0; i < (int) s.song->size(); ++i)
        {
            const auto& e = (*s.song)[(size_t) i];
            const int y = sa.getY() + 50 + i * 20;
            const bool live = s.mode.load() == (int) PlayMode::Song && e.scene == cur && s.currentBar() >= b && s.currentBar() < b + e.bars;
            g.setColour (theme.ink.withAlpha (live ? 1.0f : 0.8f)); g.setFont (uiFont (10.0f, live));
            const juce::String nm = e.scene >= 0 && e.scene < (int) s.scenes->size() ? (*s.scenes)[(size_t) e.scene].name : "-";
            g.drawText (juce::String (i + 1) + ". " + nm, sa.getX() + 8, y, sa.getWidth() - 80, 18, juce::Justification::centredLeft, true);
            g.drawText (juce::String (e.bars) + " bars", sa.getRight() - 70, y, 62, 18, juce::Justification::centredRight);
            b += e.bars;
        }
        g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (juce::String (b) + " bars  " + juce::String (s.secondsAt (meter().barStart ((long long) b)) / 60.0, 1) + " min", sa.getX() + 8, sa.getBottom() - 40, sa.getWidth() - 16, 16, juce::Justification::centredLeft);
        g.drawText (ui::U ("click: scene · drag: bars · right-click: remove"), sa.getX() + 8, sa.getBottom() - 22, sa.getWidth() - 16, 16, juce::Justification::centredLeft, true);
    }

    // ---- selection --------------------------------------------------------------------------------------
    bool isSelected (const ClipRef& r) const { return std::find (sel.begin(), sel.end(), r) != sel.end(); }
    void pruneSelection() { sel.erase (std::remove_if (sel.begin(), sel.end(), [this] (const ClipRef& r) { double a, b; return ! ctx.s.clipSpan (r, a, b); }), sel.end()); }
    void selectAll() { sel = ctx.s.allClips(); repaint(); }

    // ---- editing (the edit menu and the keys) ---------------------------------------------------------------
    Session::ClipBoard board;
    void copySel() { if (pointsSelected()) { copyPoints(); return; } pruneSelection(); if (! sel.empty()) { board = ctx.s.copyClips (sel); say (juce::String (board.items.size()) + " clip(s) copied"); } }
    void cutSel() { if (pointsSelected()) { copyPoints(); deletePoints(); return; } copySel(); deleteSel(); }
    void deleteSel() { if (pointsSelected()) { deletePoints(); return; } pruneSelection(); if (sel.empty()) return; ctx.s.deleteClips (sel); say (juce::String (sel.size()) + " clip(s) removed"); sel.clear(); repaint(); }
    void pasteAtPlayhead()
    {
        if (! pointBoard.empty() && ctx.s.lane (selLane) && (board.empty() || pointsSelected() || sel.empty())) { pastePoints(); return; }
        if (board.empty()) { say ("nothing to paste"); return; }
        auto made = ctx.s.pasteClips (board, ctx.s.currentBar(), ctx.selected());
        if (! made.empty()) { sel = made; say (juce::String (made.size()) + " clip(s) pasted at " + pos (ctx.s.currentBar())); }
        else say ("paste: no track of the right kind");
        repaint();
    }
    void duplicateSel() { pruneSelection(); if (sel.empty()) return; sel = ctx.s.duplicateClips (sel); say ("duplicated"); repaint(); }
    void splitSel()
    {
        pruneSelection();
        const double at = ctx.s.currentBar();
        auto targets = sel;
        if (targets.empty()) { if (auto* t = ctx.selected()) targets = ctx.s.clipsBetween (at, at, { t->uid }); }
        auto made = ctx.s.splitClips (targets, at);
        say (made.empty() ? juce::String ("split: no clip under the playhead") : juce::String (made.size()) + " clip(s) split at " + pos (at));
        repaint();
    }
    void muteSel() { pruneSelection(); if (sel.empty()) return; bool anyOn = false; for (auto& r : sel) if (! ctx.s.clipMuted (r)) anyOn = true; ctx.s.setClipsMuted (sel, anyOn); say (anyOn ? "muted" : "unmuted"); repaint(); }
    void loopSelection()
    {
        double a = 1.0e12, b = -1;
        for (auto& r : sel) { double s0, len; if (ctx.s.clipSpan (r, s0, len)) { a = std::min (a, s0); b = std::max (b, s0 + len); } }
        if (b < 0) { say ("loop: select clips first"); return; }
        ctx.s.loopStart.store (a); ctx.s.loopEnd.store (b); ctx.s.loopOn.store (true); ctx.s.dirty = true; repaint();
    }
    /** move the selection by bars; with nothing selected the playhead moves */
    void moveSel (double bars)
    {
        pruneSelection();
        if (sel.empty()) { ctx.s.locate (std::max (0.0, ctx.s.currentBar() + bars)); repaint(); return; }
        ctx.s.moveClips (sel, bars); repaint();
    }
    /** move the selected clips by milliseconds, off the grid (what , and . do) */
    bool nudge (double ms)
    {
        pruneSelection();
        if (sel.empty()) return false;
        ctx.s.moveClips (sel, ms * 0.001 / (60.0 / ctx.s.tempo() * 4.0)); repaint(); return true;
    }
    void stepGrid (bool forward, const juce::ModifierKeys& mods)
    {
        double d = gridBars();
        if (d >= 1.0 - 1.0e-9 && ! sel.empty()) { double s0, l0; if (ctx.s.clipSpan (sel.front(), s0, l0)) d = meter().sigAt (s0 * 4.0).beats() / 4.0 * std::round (d); }   // (whole bars: the meter's)
        if (mods.isCommandDown()) d = 1.0 / (4.0 * timeline::kTicksPerBeat);          // one tick
        else if (mods.isAltDown()) d = 0.001 / (60.0 / ctx.s.tempo() * 4.0);           // one ms
        if (timeMode() && sel.empty() && ! mods.isAnyModifierKeyDown())
        {
            const double sec = ctx.s.secondsAt (ctx.s.currentBar() * 4.0), g = gridSec();
            const double nx = forward ? (std::floor (sec / g + 1.0e-6) + 1.0) * g : std::max (0.0, (std::ceil (sec / g - 1.0e-6) - 1.0) * g);
            ctx.s.locate (ctx.s.beatAt (nx) / 4.0); repaint(); return;
        }
        if (sel.empty() && ! mods.isAnyModifierKeyDown())
        {
            // the playhead to the next / previous grid line
            const double cb = ctx.s.currentBar() * 4.0, g = gridBars() * 4.0;
            ctx.s.locate (std::max (0.0, (forward ? timeline::nextGridLine (cb, g, meter()) : timeline::prevGridLine (cb, g, meter())) / 4.0)); repaint(); return;
        }
        moveSel (forward ? d : -d);
    }
    void jumpMarker (bool forward)
    {
        const double cb = ctx.s.currentBar();
        double best = forward ? 1.0e12 : -1;
        for (auto& m : *ctx.s.markers) { if (forward && m.bar > cb + 1.0e-6) best = std::min (best, m.bar); if (! forward && m.bar < cb - 1.0e-6) best = std::max (best, m.bar); }
        if (! forward && best < 0) best = 0;
        if (best < 1.0e11) { ctx.s.locate (best); repaint(); }
    }
    void insertBars()
    {
        ask ("insert time", "how long, from the playhead (bars.beats.sixteenths.ticks, or time)", "1", [this] (juce::String t)
        {
            const double at = ctx.s.currentBar();
            const double len = timeline::parse (t, true, [this, at] (double secs) { return ctx.s.beatAt (ctx.s.secondsAt (at * 4.0) + secs) - at * 4.0; }, meter(), at * 4.0) / 4.0;
            if (len <= 0) { say ("insert time: could not read \"" + t + "\""); return; }
            ctx.s.insertTime (at, len); say (timeline::formatLength (at * 4.0, len * 4.0, meter()) + " inserted at " + pos (at)); repaint();
        });
    }
    void deleteLoopTime()
    {
        if (! ctx.s.loopOn.load()) { say ("delete time: set the loop around the bars to remove first"); return; }
        const double a = ctx.s.loopStart.load(), b = ctx.s.loopEnd.load();
        ctx.s.deleteTime (a, b); sel.clear(); say ("bars " + pos (a) + " - " + pos (b) + " removed"); repaint();
    }
    /** type where a clip starts and how long it is (the rest of the selection moves with it) */
    void positionDialog (ClipRef r)
    {
        double s0, len; if (! ctx.s.clipSpan (r, s0, len)) return;
        auto* w = new juce::AlertWindow ("position", "bars.beats.sixteenths.ticks (960 ticks a beat), or time: 1:05.250", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("start", pos (s0), "start");
        w->addTextEditor ("length", timeline::formatLength (s0 * 4.0, len * 4.0, meter()), "length");
        w->addButton ("ok", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, r, s0, len] (int res)
        {
            if (res != 1) return;
            auto toBeats = [this] (double secs) { return ctx.s.beatAt (secs); };
            const double ns = timeline::parse (w->getTextEditorContents ("start"), false, toBeats, meter()) / 4.0;
            const double from = ns >= 0 ? ns : s0;
            const double nl = timeline::parse (w->getTextEditorContents ("length"), true, [this, from] (double secs) { return ctx.s.beatAt (ctx.s.secondsAt (from * 4.0) + secs) - from * 4.0; }, meter(), from * 4.0) / 4.0;
            if (ns >= 0 && std::abs (ns - s0) > 1.0e-12) { auto moving = isSelected (r) ? sel : std::vector<ClipRef> { r }; ctx.s.moveClips (moving, ns - s0); }
            if (nl > 0 && std::abs (nl - len) > 1.0e-12) ctx.s.setClipLength (r, nl);
            if (ns < 0 || nl <= 0) say ("position: could not read what was typed");
            repaint();
        }), true);
    }
    void ask (const juce::String& title, const juce::String& msg, const juce::String& def, std::function<void (juce::String)> then)
    {
        auto* w = new juce::AlertWindow (title, msg, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("v", def); w->addButton ("ok", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, then] (int r) { if (r == 1) then (w->getTextEditorContents ("v")); }), true);
    }
    void say (const juce::String& m) { if (ctx.status) ctx.status (m); }

    /** the arrangement's own keys (the view sends them first while this page shows). True when used */
    bool key (const juce::KeyPress& k)
    {
        const auto c = k.getTextCharacter(); const int kc = k.getKeyCode(); const auto mods = k.getModifiers();
        if (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey) { stepGrid (kc == juce::KeyPress::rightKey, mods); return true; }
        if (mods.isCommandDown()) return false;
        if (c == '=' || c == '+') { zoomKey (true); return true; }
        if (c == '-' || c == '_') { zoomKey (false); return true; }
        if (c == 'z') { zoomToSelection(); return true; }
        if (c == 'Z') { zoomToFit(); return true; }
        if (c == 'n') { toggleSnap(); return true; }
        if (c == '[' || c == ']') { jumpMarker (c == ']'); return true; }
        if (kc == juce::KeyPress::homeKey) { ctx.s.locate (0.0); scrollBar = 0; repaint(); return true; }
        if (kc == juce::KeyPress::endKey) { ctx.s.locate (ctx.s.projectBars()); return true; }
        if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey) { deleteSel(); return true; }
        if (kc == juce::KeyPress::escapeKey) { sel.clear(); selPts.clear(); repaint(); return true; }
        // alt+up / alt+down: the take before / after (of the selected clip's takes)
        if (mods.isAltDown() && (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey) && ! sel.empty())
        {
            auto takes = ctx.s.takesOf (sel.front());
            if (takes.size() > 1)
            {
                int cur = 0; for (int k = 0; k < (int) takes.size(); ++k) if (! ctx.s.clipMuted (takes[(size_t) k])) cur = k;
                const int nk = juce::jlimit (0, (int) takes.size() - 1, cur + (kc == juce::KeyPress::upKey ? -1 : 1));
                ctx.s.chooseTake (takes[(size_t) nk]); sel = { takes[(size_t) nk] }; say ("take " + juce::String (nk + 1) + " of " + juce::String (takes.size())); repaint();
            }
            return true;
        }
        return false;
    }

    // ---- mouse --------------------------------------------------------------------------------------
    enum class DragMode { None, Ruler, Loop, Marker, Song, LanePoint, Freehand, Move, TrimStart, TrimEnd, FadeIn, FadeOut, Band, Scroll, PointBand, PointsMove, GainPoint, WarpPoint, EnvPoint };
    // the automation points selected (in one lane), and what a drag of them started from
    Target selLane; std::vector<int> selPts; std::vector<AutoPoint> ptOrig; std::vector<bool> ptFlag; int ptBandRow = 0;
    std::vector<AutoPoint> pointBoard;   // copied points (beats from the first)
    bool pointsSelected() const { return ! selPts.empty() && ctx.s.lane (selLane) != nullptr; }
    void editSelectedPoints (std::function<void (std::vector<AutoPoint>&, const std::vector<int>&)> fn)
    {
        auto sel = selPts;
        ctx.s.editLane (selLane, [&] (AutoLane& l) { std::vector<int> ok; for (int k : sel) if (k < (int) l.pts.size()) ok.push_back (k); fn (l.pts, ok); });
        repaint();
    }
    void deletePoints()
    {
        std::vector<int> sel = selPts; std::sort (sel.rbegin(), sel.rend());
        editSelectedPoints ([sel] (std::vector<AutoPoint>& pts, const std::vector<int>&) { for (int k : sel) if (k < (int) pts.size()) pts.erase (pts.begin() + k); });
        say (juce::String (sel.size()) + " point(s) removed"); selPts.clear();
    }
    void copyPoints()
    {
        pointBoard.clear(); auto l = ctx.s.lane (selLane); if (! l) return;
        std::vector<int> sel = selPts; std::sort (sel.begin(), sel.end());
        double first = 1.0e12; for (int k : sel) if (k < (int) l->pts.size()) first = std::min (first, l->pts[(size_t) k].beat);
        for (int k : sel) if (k < (int) l->pts.size()) { auto pt = l->pts[(size_t) k]; pt.beat -= first; pointBoard.push_back (pt); }
        say (juce::String (pointBoard.size()) + " point(s) copied");
    }
    /** the copied points into the selected lane (or the lane they came from) at the playhead, replacing what was there */
    void pastePoints()
    {
        if (pointBoard.empty() || ! ctx.s.lane (selLane)) return;
        const double at = ctx.s.currentBar() * 4.0, len = pointBoard.back().beat;
        auto board = pointBoard;
        ctx.s.editLane (selLane, [&] (AutoLane& l)
        {
            l.pts.erase (std::remove_if (l.pts.begin(), l.pts.end(), [&] (const AutoPoint& p) { return p.beat >= at - 1.0e-9 && p.beat <= at + len + 1.0e-9; }), l.pts.end());
            for (auto pt : board) { pt.beat += at; l.pts.push_back (pt); }
        });
        auto l = ctx.s.lane (selLane); selPts.clear();
        for (int k = 0; l && k < (int) l->pts.size(); ++k) if (l->pts[(size_t) k].beat >= at - 1.0e-9 && l->pts[(size_t) k].beat <= at + len + 1.0e-9) selPts.push_back (k);
        say (juce::String (board.size()) + " point(s) pasted at " + pos (at / 4.0)); repaint();
    }
    void pointsMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "remove " + juce::String (selPts.size()) + " point(s)  (delete)");
        m.addItem (2, "copy  (cmd+c)"); m.addItem (3, "set the value..."); m.addItem (4, "scale the values (%)..."); m.addItem (5, "move the values up / down (%)...");
        m.addItem (6, "flatten (all to their average)"); m.addItem (7, "invert (top to bottom)"); m.addItem (8, "straight lines between them (no curves)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r == 1) deletePoints(); else if (r == 2) copyPoints();
            else if (r == 3) ask ("set the value", "0 - 100 %", "50", [this] (juce::String t) { const float v = juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f); editSelectedPoints ([v] (std::vector<AutoPoint>& p, const std::vector<int>& k) { for (int i : k) p[(size_t) i].value = v; }); });
            else if (r == 4) ask ("scale the values", "percent of each value (around 0)", "50", [this] (juce::String t) { const float f = t.getFloatValue() / 100.0f; editSelectedPoints ([f] (std::vector<AutoPoint>& p, const std::vector<int>& k) { for (int i : k) p[(size_t) i].value = juce::jlimit (0.0f, 1.0f, p[(size_t) i].value * f); }); });
            else if (r == 5) ask ("move the values", "percentage points, + up or - down", "10", [this] (juce::String t) { const float d = t.getFloatValue() / 100.0f; editSelectedPoints ([d] (std::vector<AutoPoint>& p, const std::vector<int>& k) { for (int i : k) p[(size_t) i].value = juce::jlimit (0.0f, 1.0f, p[(size_t) i].value + d); }); });
            else if (r == 6) editSelectedPoints ([] (std::vector<AutoPoint>& p, const std::vector<int>& k) { float a = 0; for (int i : k) a += p[(size_t) i].value; a /= (float) std::max<size_t> (1, k.size()); for (int i : k) p[(size_t) i].value = a; });
            else if (r == 7) editSelectedPoints ([] (std::vector<AutoPoint>& p, const std::vector<int>& k) { for (int i : k) p[(size_t) i].value = 1.0f - p[(size_t) i].value; });
            else if (r == 8) editSelectedPoints ([] (std::vector<AutoPoint>& p, const std::vector<int>& k) { for (int i : k) p[(size_t) i].curve = 0.0f; });
        });
    }
    struct Hit { Track* t = nullptr; ClipRef ref; bool found = false; enum Part { Body, Start, End, FadeIn, FadeOut } part = Body; };
    Hit hitClip (juce::Point<int> p) const
    {
        Hit h; const auto la = lanesArea();
        if (! la.contains (p) || p.x < left) return h;
        auto rs = rows();
        const int row = rowAt (p.y);
        if (row < 0 || row >= (int) rs.size()) return h;
        h.t = rs[(size_t) row];
        const double b = bar (p.x);
        const int y = rowY (row);
        auto edges = [&] (double s0, double len)
        {
            const double x0 = xd (s0), x1 = xd (s0 + len), w = x1 - x0;
            const double zone = std::min (6.0, w * 0.25);
            if (p.x - x0 <= zone) h.part = Hit::Start; else if (x1 - p.x <= zone) h.part = Hit::End;
        };
        // (stacked takes: the one that plays is the one under the mouse)
        const AudioClip* hitA = nullptr;
        for (auto& c : *h.t->aclips) if (b >= c.startBar && b < c.startBar + c.bars && (! hitA || (hitA->muted && ! c.muted))) hitA = &c;
        for (auto& c : *h.t->aclips)
            if (&c == hitA)
            {
                h.ref = { h.t->uid, c.id, true }; h.found = true; edges (c.startBar, c.bars);
                const double secPerBar = 4.0 * 60.0 / ctx.s.tempo();
                const double fiX = xd (c.startBar) + c.fadeIn / secPerBar * pxPerBar, foX = xd (c.startBar + c.bars) - c.fadeOut / secPerBar * pxPerBar;
                if (p.y < y + 3 + 8 && laneH >= 30) { if (std::abs (p.x - fiX) <= 5) h.part = Hit::FadeIn; else if (std::abs (p.x - foX) <= 5) h.part = Hit::FadeOut; }
                return h;
            }
        for (auto& c : *h.t->clips)
            if (b >= c.startBar && b < c.startBar + c.bars) { h.ref = { h.t->uid, c.id, false }; h.found = true; edges (c.startBar, c.bars); return h; }
        return h;
    }
    juce::Rectangle<int> bandRect() const { return juce::Rectangle<int> (bandA, bandB).getIntersection (lanesArea().withTrimmedLeft (left)); }
    int markerAt (int px) const { for (int i = 0; i < (int) ctx.s.markers->size(); ++i) if (std::abs (x ((*ctx.s.markers)[(size_t) i].bar) - px) <= 5) return i; return -1; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const double hb = e.x >= left && e.x < lanesArea().getRight() ? bar (e.position.x) : -1;
        if (hb != hoverBar) { hoverBar = hb; repaint (0, 0, lanesArea().getWidth(), top); if (ctx.status && hb >= 0) repaint(); }
        auto h = hitClip (e.getPosition());
        auto cur = juce::MouseCursor::NormalCursor;
        if (h.found) cur = h.part == Hit::Start || h.part == Hit::End ? juce::MouseCursor::LeftRightResizeCursor : h.part == Hit::Body ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
        else if (e.y >= top && e.y < ruler && markerAt (e.x) >= 0) cur = juce::MouseCursor::LeftRightResizeCursor;
        setMouseCursor (cur);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverBar = -1; repaint (0, 0, lanesArea().getWidth(), top); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        const auto la = lanesArea(), aa = autoArea(), sa = songArea();
        dragMode = DragMode::None; dragLane = -1; downPos = e.getPosition(); downBar = bar (e.position.x); moved = false;
        if (sa.contains (e.getPosition()))
        {
            const int i = (e.y - sa.getY() - 50) / 20;
            if (i >= 0 && i < (int) s.song->size())
            {
                if (e.mods.isPopupMenu()) { s.editSong ([i] (std::vector<SongEntry>& v) { v.erase (v.begin() + i); }); repaint(); return; }
                dragMode = DragMode::Song; songDrag = i; songStart = (*s.song)[(size_t) i].bars;
                if (e.getNumberOfClicks() > 1 || e.mods.isShiftDown())
                {
                    juce::PopupMenu m; for (int k = 0; k < (int) s.scenes->size() && k < 32; ++k) m.addItem (k + 1, juce::String (k + 1) + " " + (*s.scenes)[(size_t) k].name);
                    m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r) { if (r > 0) ctx.s.editSong ([i, r] (std::vector<SongEntry>& v) { v[(size_t) i].scene = r - 1; }); repaint(); });
                }
            }
            return;
        }
        if (scrollArea().contains (e.getPosition()))
        {
            dragMode = DragMode::Scroll; scrollStart = scrollBar;
            const auto sr = scrollArea(); const double total = std::max (s.projectBars() + 8.0, scrollBar + visibleBars());
            const int tx = sr.getX() + (int) (sr.getWidth() * scrollBar / total), tw = std::max (12, (int) (sr.getWidth() * visibleBars() / total));
            if (e.x < tx || e.x > tx + tw) { scrollBar = std::max (0.0, (e.x - sr.getX()) / (double) sr.getWidth() * total - visibleBars() * 0.5); scrollStart = scrollBar; repaint(); }
            return;
        }
        if (e.y >= top && e.y < ruler && e.x >= left)
        {
            const int mk = markerAt (e.x);
            if (mk >= 0 && e.mods.isPopupMenu()) { s.editMarkers ([mk] (std::vector<Marker>& m) { if (mk < (int) m.size()) m.erase (m.begin() + mk); }); repaint(); return; }
            if (mk >= 0 && e.getNumberOfClicks() > 1) { renameMarker (mk); return; }
            if (mk >= 0) { dragMode = DragMode::Marker; markerDrag = mk; return; }
            if (e.mods.isPopupMenu()) { rulerMenu ((int) meter().barAt (bar (e.position.x) * 4.0)); return; }
            if (e.mods.isShiftDown()) { const double b = magnetBar (bar (e.position.x), e.mods.isCommandDown(), {}); s.editMarkers ([b] (std::vector<Marker>& m) { m.push_back ({ "marker " + juce::String (m.size() + 1), b }); }); repaint(); return; }
            dragMode = DragMode::Ruler; loopA = magnetBar (bar (e.position.x), e.mods.isCommandDown(), {});
            return;
        }
        if (aa.contains (e.getPosition()))
        {
            auto targets = laneTargets();
            if (e.y < aa.getY() + 26) return;
            const int i = (e.y - aa.getY() - 26) / 52 + laneScroll;
            if (i < 0 || i >= (int) targets.size()) return;
            if (e.x < left) { if (e.mods.isPopupMenu()) { s.removeLane (targets[(size_t) i]); repaint(); } else s.editLane (targets[(size_t) i], [] (AutoLane& l) { l.enabled = ! l.enabled; }); repaint(); return; }
            dragLane = i - laneScroll; laneTarget = targets[(size_t) i];
            const int y = aa.getY() + 26 + (i - laneScroll) * 52;
            if (e.mods.isShiftDown())
            {
                // freehand: the drag itself is the curve (an arc drawn by hand, a point every few pixels)
                dragMode = DragMode::Freehand; lastFreeBeat = bar (e.position.x) * 4.0;
                const double fb = std::max (0.0, lastFreeBeat); const float fv = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
                s.editLane (laneTarget, [fb, fv] (AutoLane& ll) { ll.pts.erase (std::remove_if (ll.pts.begin(), ll.pts.end(), [fb] (const AutoPoint& p) { return std::abs (p.beat - fb) < 0.02; }), ll.pts.end()); ll.pts.push_back ({ fb, fv, 0.0f }); });
                repaint(); return;
            }
            const double beat = bar (e.position.x) * 4.0; const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
            auto l = s.lane (laneTarget);
            int near = -1; double bestPx = 6.0;
            for (int k = 0; l && k < (int) l->pts.size(); ++k) { const double d = std::abs (l->pts[(size_t) k].beat - beat) * pxPerBeat(); if (d < bestPx) { near = k; bestPx = d; } }
            const bool inSel = near >= 0 && selLane == laneTarget && std::find (selPts.begin(), selPts.end(), near) != selPts.end();
            if (e.mods.isPopupMenu() && inSel) { pointsMenu(); return; }
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) { if (near >= 0) s.editLane (laneTarget, [near] (AutoLane& ll) { ll.pts.erase (ll.pts.begin() + near); }); selPts.clear(); repaint(); return; }
            // cmd: points into / out of the selection, or a box around them
            if (e.mods.isCommandDown())
            {
                if (selLane != laneTarget) { selPts.clear(); selLane = laneTarget; }
                if (near >= 0) { if (inSel) selPts.erase (std::remove (selPts.begin(), selPts.end(), near), selPts.end()); else selPts.push_back (near); }
                else { dragMode = DragMode::PointBand; bandA = bandB = e.getPosition(); ptBandRow = dragLane; }
                repaint(); return;
            }
            // a selected point: the selection moves together
            if (inSel && ! e.mods.isAltDown())
            {
                dragMode = DragMode::PointsMove; dragPoint = near;
                if (auto ll = s.lane (laneTarget)) { ptOrig = ll->pts; ptFlag.assign (ptOrig.size(), false); for (int k : selPts) if (k < (int) ptFlag.size()) ptFlag[(size_t) k] = true; }
                repaint(); return;
            }
            selPts.clear();
            dragMode = DragMode::LanePoint;
            if (near >= 0) { dragPoint = near; curveDrag = e.mods.isAltDown(); }
            else { const double nb = magnetBar (beat / 4.0, e.mods.isCommandDown(), {}) * 4.0; s.editLane (laneTarget, [nb, v] (AutoLane& ll) { ll.pts.push_back ({ nb, v, 0.0f }); }); l = s.lane (laneTarget); dragPoint = -1; for (int k = 0; k < (int) l->pts.size(); ++k) if (std::abs (l->pts[(size_t) k].beat - nb) < 1.0e-9) dragPoint = k; curveDrag = false; }
            repaint();
            return;
        }
        if (! la.contains (e.getPosition())) return;
        if (e.x < left)
        {
            auto rs = rows(); const int row = rowAt (e.y);
            if (row >= 0 && row < (int) rs.size())
            {
                auto* t = rs[(size_t) row];
                ctx.select (t);
                if (e.mods.isPopupMenu()) { trackMenu (*t); return; }
                // the group's triangle: fold / unfold
                if (const int g = t->group.load(); g && e.x >= left - 18 && e.y < rowY (row) + 16)
                    s.editGroups ([g] (std::vector<Group>& v) { for (auto& x : v) if (x.id == g) x.folded = ! x.folded; });
            }
            repaint(); return;
        }
        // a clip's own automation being drawn: click adds a point, drag moves it, double-click removes
        if (envClip.track != 0)
        {
            juce::Rectangle<float> full; double origin = 0; auto* ev = envsOf (envClip, &full, &origin);
            if (ev && envIndex >= 0 && envIndex < (int) ev->size() && full.contains (e.position))
            {
                auto all = *ev; auto& pts = all[(size_t) envIndex].pts; int near = -1;
                for (int k = 0; k < (int) pts.size(); ++k) if (std::abs ((float) xd (origin + pts[(size_t) k].first / 4.0) - e.position.x) < 6.0f && std::abs (envY (full, pts[(size_t) k].second) - e.position.y) < 6.0f) near = k;
                if (near >= 0 && (e.getNumberOfClicks() > 1 || e.mods.isPopupMenu())) { pts.erase (pts.begin() + near); setEnvs (envClip, all); repaint(); return; }
                if (near < 0)
                {
                    const double b = std::max (0.0, (snapBar (bar (e.position.x)) - origin) * 4.0);
                    pts.push_back ({ b, envValueAt (full, e.position.y) }); std::sort (pts.begin(), pts.end());
                    for (int k = 0; k < (int) pts.size(); ++k) if (std::abs (pts[(size_t) k].first - b) < 1.0e-9) near = k;
                    setEnvs (envClip, all);
                }
                envDrag = near; dragMode = DragMode::EnvPoint; repaint(); return;
            }
            envClip = {}; envIndex = -1;
        }
        // warp markers: click adds one where the mouse is (at the source playing there), drag moves it in time, double-click removes
        if (warpClip.track != 0)
        {
            auto* wt = s.clipTrack (warpClip); const AudioClip* wc = audioClipOf (warpClip);
            const int wrow = rowOf (wt);
            if (wc && wrow >= 0 && wc->sample)
            {
                const auto full = juce::Rectangle<float> ((float) xd (wc->startBar), (float) rowY (wrow) + 3.0f, (float) (wc->bars * pxPerBar) - 1.0f, (float) laneH - 6.0f);
                if (full.contains (e.position))
                {
                    auto w = wc->warp; int near = -1;
                    for (int k = 0; k < (int) w.size(); ++k) if (std::abs ((float) xd (wc->startBar + w[(size_t) k].first / 4.0) - e.position.x) < 5.0f) near = k;
                    auto setW = [&] (std::vector<std::pair<double, double>> nw) { const int id = warpClip.id; s.editAudioClips (*wt, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) { c.mode = 3; c.warp = nw; } }); };
                    if (near >= 0 && (e.getNumberOfClicks() > 1 || e.mods.isPopupMenu())) { w.erase (w.begin() + near); setW (w); repaint(); return; }
                    if (near < 0)
                    {
                        const double beats = wc->bars * 4.0, avail = std::max (0.0, wc->sample->seconds() - wc->offset);
                        const double b = (bar (e.position.x) - wc->startBar) * 4.0;
                        const double src = wc->mode == 3 ? Session::warpSrc (*wc, b, beats, avail) : b / beats * avail;
                        if (wc->mode != 3) w.clear();
                        w.push_back ({ b, src }); std::sort (w.begin(), w.end());
                        setW (w);
                        for (int k = 0; k < (int) w.size(); ++k) if (std::abs (w[(size_t) k].first - b) < 1.0e-12) near = k;
                    }
                    warpDrag = near; dragMode = DragMode::WarpPoint; repaint(); return;
                }
            }
            warpClip = {};
        }
        // the gain line being drawn: click adds a point, drag moves it, double-click takes it away
        if (gainClip.track != 0)
        {
            auto* gt = s.clipTrack (gainClip); const AudioClip* gc = nullptr;
            if (gt) for (auto& c : *gt->aclips) if (c.id == gainClip.id) gc = &c;
            const auto rs = rows(); int grow = -1; for (int i = 0; i < (int) rs.size(); ++i) if (rs[(size_t) i] == gt) grow = i;
            if (gc && grow >= 0)
            {
                const auto full = juce::Rectangle<float> ((float) xd (gc->startBar), (float) rowY (grow) + 3.0f, (float) (gc->bars * pxPerBar) - 1.0f, (float) laneH - 6.0f);
                if (full.contains (e.position))
                {
                    auto line = gc->gainLine; int near = -1;
                    for (int k = 0; k < (int) line.size(); ++k) if (std::abs ((float) xd (gc->startBar + line[(size_t) k].first / 4.0) - e.position.x) < 6.0f && std::abs (gainY (full, line[(size_t) k].second) - e.position.y) < 6.0f) near = k;
                    if (near >= 0 && (e.getNumberOfClicks() > 1 || e.mods.isPopupMenu())) { line.erase (line.begin() + near); s.setClipGainLine (gainClip, line); repaint(); return; }
                    if (near < 0)
                    {
                        const double gb = std::max (0.0, (snapBar (bar (e.position.x)) - gc->startBar) * 4.0);
                        line.push_back ({ gb, gainDbAt (full, e.position.y) }); std::sort (line.begin(), line.end());
                        s.setClipGainLine (gainClip, line);
                        for (int k = 0; k < (int) line.size(); ++k) if (std::abs (line[(size_t) k].first - gb) < 1.0e-9) near = k;
                    }
                    gainDrag = near; dragMode = DragMode::GainPoint; repaint(); return;
                }
            }
            gainClip = {};   // a click anywhere else: done drawing
        }
        auto h = hitClip (e.getPosition());
        if (h.t) ctx.select (h.t);
        if (! h.found)
        {
            if (h.t && e.getNumberOfClicks() > 1 && ! h.t->isAudio())
            {
                const double b = snapBar (bar (e.position.x)); const int p = h.t->pattern.load();
                const double len = (meter().barStart (meter().barAt (b * 4.0) + 4) - meter().barStart (meter().barAt (b * 4.0))) / 4.0;   // four bars of the meter
                s.editClips (*h.t, [b, p, len] (std::vector<Clip>& v) { v.push_back ({ p, b, len }); }); repaint(); return;
            }
            if (e.mods.isPopupMenu())
            {
                if (h.t && ! h.t->isAudio())
                {
                    const double b = snapBar (bar (e.position.x)); auto* t = h.t;
                    const double len = (meter().barStart (meter().barAt (b * 4.0) + 4) - meter().barStart (meter().barAt (b * 4.0))) / 4.0;
                    juce::PopupMenu m;
                    m.addItem (1, "new pattern clip here (pattern " + juce::String (t->pattern.load() + 1) + ")");
                    m.addItem (2, "new note clip here (free notes)");
                    m.addItem (3, "a midi file here...");
                    m.showMenuAsync (juce::PopupMenu::Options(), [this, t, b, len] (int r)
                    {
                        auto& s = ctx.s;
                        if (r == 1) { const int p = t->pattern.load(); s.editClips (*t, [b, p, len] (std::vector<Clip>& v) { v.push_back ({ p, b, len }); }); }
                        else if (r == 2) { const int id = s.addNoteClip (*t, b, len, {}); sel = { { t->uid, id, false } }; editNotes (sel.front()); }
                        else if (r == 3)
                        {
                            chooser = std::make_unique<juce::FileChooser> ("a midi file", juce::File(), "*.mid;*.midi");
                            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, t, b] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) midiClip (*t, fc.getResult(), b); });
                        }
                        repaint();
                    });
                }
                return;
            }
            dragMode = DragMode::Band; bandA = bandB = e.getPosition(); bandAdd = e.mods.isShiftDown() || e.mods.isCommandDown(); bandBase = bandAdd ? sel : std::vector<ClipRef>();
            repaint(); return;
        }
        // a clip: select it (shift / cmd: add or take away)
        if (e.mods.isShiftDown() || (e.mods.isCommandDown() && ! e.mods.isPopupMenu()))
        {
            if (isSelected (h.ref)) { sel.erase (std::remove (sel.begin(), sel.end(), h.ref), sel.end()); repaint(); return; }
            sel.push_back (h.ref);
        }
        else if (! isSelected (h.ref)) sel = { h.ref };
        if (e.mods.isPopupMenu()) { clipMenu (h); repaint(); return; }
        if (h.ref.audio && e.getNumberOfClicks() > 1) { clipEditor (h.ref); return; }
        if (! h.ref.audio && e.getNumberOfClicks() > 1) { if (auto* t = s.clipTrack (h.ref)) for (auto& c : *t->clips) if (c.id == h.ref.id && c.notes) { editNotes (h.ref); return; } }
        anchor = h.ref; s.clipSpan (h.ref, anchorStart, anchorLen);
        dragTrackRow = rowAt (e.y);
        if (h.part == Hit::FadeIn || h.part == Hit::FadeOut) { dragMode = h.part == Hit::FadeIn ? DragMode::FadeIn : DragMode::FadeOut; return; }
        if (h.part == Hit::Start || h.part == Hit::End) { dragMode = h.part == Hit::Start ? DragMode::TrimStart : DragMode::TrimEnd; return; }
        dragMode = DragMode::Move;
        if (e.mods.isAltDown())
        {
            // copy, then drag the copies
            auto b = s.copyClips (sel);
            auto made = s.pasteClips (b, b.startBar);
            for (size_t k = 0; k < sel.size() && k < made.size(); ++k) if (sel[k] == anchor) anchor = made[k];
            sel = made;
        }
        origins.clear();
        for (auto& r : sel) { double a0, l0; if (s.clipSpan (r, a0, l0)) origins.push_back ({ r, a0 }); }
    }
    /** the ruler's menu: the time signature from this bar, a marker */
    void rulerMenu (int barIndex)
    {
        juce::PopupMenu m;
        const auto& mm = meter();
        bool here = false; for (auto& c : mm.getChanges()) if (c.bar == barIndex && barIndex > 0) here = true;
        m.addItem (1, "time signature from bar " + juce::String (barIndex + 1) + "...  (now " + mm.sigOf (barIndex).text() + ")");
        if (here) m.addItem (2, "remove the change at bar " + juce::String (barIndex + 1));
        m.addItem (3, "a marker at bar " + juce::String (barIndex + 1));
        m.showMenuAsync (juce::PopupMenu::Options(), [this, barIndex] (int r)
        {
            if (r == 1) signatureDialog (barIndex);
            else if (r == 2) { ctx.s.setMeterAt (barIndex, {}); say ("bar " + juce::String (barIndex + 1) + " goes on in the meter before it"); repaint(); }
            else if (r == 3) { const double b = meter().barStart (barIndex) / 4.0; ctx.s.editMarkers ([b] (std::vector<Marker>& v) { v.push_back ({ "marker " + juce::String (v.size() + 1), b }); }); repaint(); }
        });
    }
    /** a time signature, or a cycle of them, from a bar on (until the next change) */
    void signatureDialog (int barIndex)
    {
        const auto& mm = meter();
        juce::String now = mm.sigOf (barIndex).text();
        for (auto& c : mm.getChanges()) if (c.bar == barIndex) now = timeline::sigsText (c.sigs);
        ask ("time signature from bar " + juce::String (barIndex + 1),
             "any signature: 7/8, 5/4, 13/16, 3/2; grouped: 2+2+3/8 (the groups accent the click); fractional: 3.5/4; irrational: 4/3, 5/6. "
             "Several in turn make a cycle that repeats until the next change: 7/8 5/8, or 3/4 3/4 2/4. Empty: none here (the meter before goes on)", now,
             [this, barIndex] (juce::String t)
             {
                 if (t.trim().isEmpty()) { ctx.s.setMeterAt (barIndex, {}); repaint(); return; }
                 std::vector<timeline::Sig> sg;
                 if (! timeline::parseSigs (t, sg)) { say ("time signature: could not read \"" + t + "\" (e.g. 7/8, 2+2+3/8, 7/8 5/8)"); return; }
                 ctx.s.setMeterAt (barIndex, sg);
                 say ("from bar " + juce::String (barIndex + 1) + ": " + timeline::sigsText (sg) + (sg.size() > 1 ? ", in turn" : ""));
                 repaint();
             });
    }
    void renameMarker (int mk)
    {
        if (mk < 0 || mk >= (int) ctx.s.markers->size()) return;
        ask ("marker", "its name", (*ctx.s.markers)[(size_t) mk].name, [this, mk] (juce::String n)
        {
            ctx.s.editMarkers ([mk, n] (std::vector<Marker>& m) { if (mk < (int) m.size()) m[(size_t) mk].name = n.trim().isEmpty() ? m[(size_t) mk].name : n.trim(); });
            repaint();
        });
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        if (e.getDistanceFromDragStart() > 3) moved = true;
        const bool free = e.mods.isCommandDown();
        mouseMove (e);
        switch (dragMode)
        {
            case DragMode::Scroll:
            {
                const double total = std::max (s.projectBars() + 8.0, scrollStart + visibleBars());
                scrollBar = std::max (0.0, scrollStart + e.getDistanceFromDragStartX() / (double) scrollArea().getWidth() * total);
                repaint(); return;
            }
            case DragMode::Ruler:
                if (! moved) return;
                dragMode = DragMode::Loop; [[fallthrough]];
            case DragMode::Loop:
            {
                const double b = magnetBar (bar (e.position.x), free, {});
                const double minLen = std::max (1.0 / 256.0, s.snap.load() && ! free ? gridBars() : 1.0 / 256.0);
                s.loopStart.store (std::min (loopA, b)); s.loopEnd.store (std::max (std::min (loopA, b) + minLen, std::max (loopA, b))); s.loopOn.store (true);
                repaint(); return;
            }
            case DragMode::Marker:
            {
                const double b = magnetBar (bar (e.position.x), free, {}); const int mk = markerDrag;
                // (editMarkers sorts: follow the marker by its name and old place)
                if (mk >= 0 && mk < (int) s.markers->size())
                {
                    const auto name = (*s.markers)[(size_t) mk].name;
                    s.editMarkers ([mk, b] (std::vector<Marker>& m) { if (mk < (int) m.size()) m[(size_t) mk].bar = b; });
                    for (int i = 0; i < (int) s.markers->size(); ++i) if ((*s.markers)[(size_t) i].name == name && std::abs ((*s.markers)[(size_t) i].bar - b) < 1.0e-12) markerDrag = i;
                }
                repaint(); return;
            }
            case DragMode::Song: { const int d = e.getDistanceFromDragStartX() / 12; const int i = songDrag; const int nb = std::max (1, songStart + d); s.editSong ([i, nb] (std::vector<SongEntry>& v) { if (i < (int) v.size()) v[(size_t) i].bars = nb; }); repaint(); return; }
            case DragMode::Freehand:
            {
                const int y = autoArea().getY() + 26 + dragLane * 52;
                const double b1 = std::max (0.0, bar (e.position.x) * 4.0); const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
                const double lo = std::min (lastFreeBeat, b1), hi = std::max (lastFreeBeat, b1);
                if ((hi - lo) * pxPerBeat() < 3.0) return;   // a point every three pixels
                s.editLane (laneTarget, [=] (AutoLane& l) { l.pts.erase (std::remove_if (l.pts.begin(), l.pts.end(), [=] (const AutoPoint& p) { return p.beat > lo + 1.0e-6 && p.beat <= hi + 1.0e-6; }), l.pts.end()); l.pts.push_back ({ b1, v, 0.0f }); });
                lastFreeBeat = b1; repaint(); return;
            }
            case DragMode::LanePoint:
            {
                if (dragPoint < 0) return;
                const int y = autoArea().getY() + 26 + dragLane * 52;
                const double beat = magnetBar (bar (e.position.x), free, {}) * 4.0; const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
                const int k = dragPoint; const bool cv = curveDrag; const float dy = (float) e.getDistanceFromDragStartY();
                s.editLane (laneTarget, [=] (AutoLane& l) { if (k < (int) l.pts.size()) { if (cv) l.pts[(size_t) k].curve = juce::jlimit (-1.0f, 1.0f, -dy / 40.0f); else { l.pts[(size_t) k].beat = std::max (0.0, beat); l.pts[(size_t) k].value = v; } } });
                repaint(); return;
            }
            case DragMode::Band: { bandB = e.getPosition(); applyBand(); repaint(); return; }
            case DragMode::EnvPoint:
            {
                juce::Rectangle<float> full; double origin = 0; auto* ev = envsOf (envClip, &full, &origin);
                if (! ev || envIndex < 0 || envIndex >= (int) ev->size() || envDrag < 0) return;
                auto all = *ev; auto& pts = all[(size_t) envIndex].pts; if (envDrag >= (int) pts.size()) return;
                double s0 = 0, len = 0; ctx.s.clipSpan (envClip, s0, len);
                const double lo = (s0 - origin) * 4.0, hi = (s0 + len - origin) * 4.0;
                pts[(size_t) envDrag] = { juce::jlimit (lo, hi, (snapBar (bar (e.position.x)) - origin) * 4.0), envValueAt (full, e.position.y) };
                const auto moved = pts[(size_t) envDrag]; std::sort (pts.begin(), pts.end());
                for (int k = 0; k < (int) pts.size(); ++k) if (pts[(size_t) k] == moved) envDrag = k;
                setEnvs (envClip, all);
                if (auto* t = ctx.s.clipTrack (envClip)) if (auto* d = t->device (all[(size_t) envIndex].device)) ctx.status (d->paramName (all[(size_t) envIndex].param) + ": " + d->paramText (all[(size_t) envIndex].param, moved.second));
                repaint(); return;
            }
            case DragMode::WarpPoint:
            {
                auto* wt = s.clipTrack (warpClip); const AudioClip* wc = audioClipOf (warpClip);
                if (! wt || ! wc || warpDrag < 0 || warpDrag >= (int) wc->warp.size()) return;
                auto w = wc->warp;
                // between its neighbours (in time and in the source), on the grid unless cmd is down
                const double lo = warpDrag > 0 ? w[(size_t) warpDrag - 1].first + 1.0 / 256.0 : 1.0 / 256.0;
                const double hi = warpDrag + 1 < (int) w.size() ? w[(size_t) warpDrag + 1].first - 1.0 / 256.0 : wc->bars * 4.0 - 1.0 / 256.0;
                const double raw = e.mods.isCommandDown() ? bar (e.position.x) : snapBar (bar (e.position.x));
                w[(size_t) warpDrag].first = juce::jlimit (lo, std::max (lo, hi), (raw - wc->startBar) * 4.0);
                const int id = warpClip.id;
                s.editAudioClips (*wt, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) c.warp = w; });
                ctx.status ("warp marker at " + pos (wc->startBar + w[(size_t) warpDrag].first / 4.0) + ", source " + juce::String (w[(size_t) warpDrag].second, 3) + " s");
                repaint(); return;
            }
            case DragMode::GainPoint:
            {
                auto* gt = s.clipTrack (gainClip); if (! gt || gainDrag < 0) return;
                const AudioClip* gc = nullptr; for (auto& c : *gt->aclips) if (c.id == gainClip.id) gc = &c;
                if (! gc || gainDrag >= (int) gc->gainLine.size()) return;
                const auto rs = rows(); int grow = 0; for (int i = 0; i < (int) rs.size(); ++i) if (rs[(size_t) i] == gt) grow = i;
                const auto full = juce::Rectangle<float> ((float) xd (gc->startBar), (float) rowY (grow) + 3.0f, (float) (gc->bars * pxPerBar) - 1.0f, (float) laneH - 6.0f);
                auto line = gc->gainLine;
                const double gb = juce::jlimit (0.0, gc->bars * 4.0, (snapBar (bar (e.position.x)) - gc->startBar) * 4.0);
                line[(size_t) gainDrag] = { gb, gainDbAt (full, e.position.y) };
                const auto moved = line[(size_t) gainDrag];
                std::sort (line.begin(), line.end());
                for (int k = 0; k < (int) line.size(); ++k) if (line[(size_t) k] == moved) gainDrag = k;
                s.setClipGainLine (gainClip, line);
                ctx.status ("gain " + juce::String (moved.second, 1) + " db at " + pos (gc->startBar + moved.first / 4.0));
                repaint(); return;
            }
            case DragMode::PointBand:
            {
                bandB = e.getPosition();
                const int y = autoArea().getY() + 26 + ptBandRow * 52;
                const auto r = juce::Rectangle<int> (bandA, bandB);
                const double b0 = bar ((float) r.getX()) * 4.0, b1 = bar ((float) r.getRight()) * 4.0;
                const float v1 = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - r.getY()) / 42.0f), v0 = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - r.getBottom()) / 42.0f);
                selPts.clear();
                if (auto ll = s.lane (laneTarget)) for (int k = 0; k < (int) ll->pts.size(); ++k) { const auto& pt = ll->pts[(size_t) k]; if (pt.beat >= b0 && pt.beat <= b1 && pt.value >= v0 - 0.02f && pt.value <= v1 + 0.02f) selPts.push_back (k); }
                repaint(); return;
            }
            case DragMode::PointsMove:
            {
                if (ptOrig.empty() || dragPoint < 0 || dragPoint >= (int) ptOrig.size()) return;
                // the point under the mouse lands on the grid (or the magnet); the others keep their distance in time and value
                const double anchor = ptOrig[(size_t) dragPoint].beat;
                double nb = magnetBar ((anchor / 4.0) + (bar (e.position.x) - downBar), free, {}) * 4.0;
                double db = nb - anchor; for (size_t k = 0; k < ptOrig.size(); ++k) if (ptFlag[k]) db = std::max (db, -ptOrig[k].beat);
                const float dv = -(float) e.getDistanceFromDragStartY() / 42.0f;
                std::vector<std::pair<AutoPoint, bool>> v;
                for (size_t k = 0; k < ptOrig.size(); ++k) { auto pt = ptOrig[k]; if (ptFlag[k]) { pt.beat += db; pt.value = juce::jlimit (0.0f, 1.0f, pt.value + dv); } v.push_back ({ pt, ptFlag[k] }); }
                std::stable_sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.first.beat < b.first.beat; });
                std::vector<AutoPoint> pts; selPts.clear();
                for (size_t k = 0; k < v.size(); ++k) { pts.push_back (v[k].first); if (v[k].second) selPts.push_back ((int) k); }
                s.editLane (laneTarget, [&pts] (AutoLane& l) { l.pts = pts; });
                repaint(); return;
            }
            case DragMode::TrimEnd:
            {
                double end = magnetBar (anchorStart + anchorLen + (bar (e.position.x) - downBar), free, { anchor });
                s.setClipLength (anchor, end - anchorStart); repaint(); return;
            }
            case DragMode::TrimStart:
            {
                const double ns = magnetBar (anchorStart + (bar (e.position.x) - downBar), free, { anchor });
                s.trimClipStart (anchor, ns); repaint(); return;
            }
            case DragMode::FadeIn: case DragMode::FadeOut:
            {
                auto* t = s.clipTrack (anchor); if (! t) return;
                const double secPerBar = 4.0 * 60.0 / s.tempo();
                const bool in = dragMode == DragMode::FadeIn;
                const double secs = juce::jlimit (0.0, anchorLen * secPerBar, (in ? bar (e.position.x) - anchorStart : anchorStart + anchorLen - bar (e.position.x)) * secPerBar);
                const int id = anchor.id;
                s.editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) { if (in) c.fadeIn = std::min (secs, c.bars * secPerBar - c.fadeOut); else c.fadeOut = std::min (secs, c.bars * secPerBar - c.fadeIn); } });
                repaint(); return;
            }
            case DragMode::Move:
            {
                if (! moved) return;
                // the clicked clip's start (or end) lands on the grid / the magnet; the others keep their distance
                const double raw = anchorStart + (bar (e.position.x) - downBar);
                bool hit = false;
                double ns = magnetBar (raw, free, sel, &hit);
                if (! hit && ! free && s.snap.load()) { bool endHit = false; const double ne = magnetBar (raw + anchorLen, free, sel, &endHit); if (endHit) ns = ne - anchorLen; }
                double delta = ns - anchorStart;
                for (auto& o : origins) delta = std::max (delta, -o.start);
                // to another track (a clip only lands on a track of its kind)
                const int rowDelta = rowAt (e.y) - dragTrackRow;
                if (rowDelta != 0) relocate (rowDelta);
                std::map<int, std::vector<std::pair<int, double>>> pat, aud;
                for (auto& o : origins) (o.ref.audio ? aud : pat)[o.ref.track].push_back ({ o.ref.id, o.start + delta });
                for (auto& [uid, v] : pat) if (auto* t = s.clipTrack ({ uid, 0, false })) s.editClips (*t, [&v] (std::vector<Clip>& cs) { for (auto& c : cs) for (auto& [id, st] : v) if (c.id == id) c.startBar = st; });
                for (auto& [uid, v] : aud) if (auto* t = s.clipTrack ({ uid, 0, true })) s.editAudioClips (*t, [&v] (std::vector<AudioClip>& cs) { for (auto& c : cs) for (auto& [id, st] : v) if (c.id == id) c.startBar = st; });
                repaint(); return;
            }
            default: return;
        }
    }
    /** the clips being dragged go up or down a number of rows (each keeps its id) */
    int rowOf (const Track* t) const { auto rs = rows(); for (int i = 0; i < (int) rs.size(); ++i) if (rs[(size_t) i] == t) return i; return -1000; }
    void relocate (int rowDelta)
    {
        auto& s = ctx.s; const auto rs = rows();
        // every clip must have a track of its kind there, or none moves
        for (auto& o : origins)
        {
            auto* t = s.clipTrack (o.ref); const int row = t ? rowOf (t) + rowDelta : -1;
            if (row < 0 || row >= (int) rs.size() || rs[(size_t) row]->isAudio() != o.ref.audio) return;
        }
        for (auto& o : origins)
        {
            auto* from = s.clipTrack (o.ref); auto* to = rs[(size_t) (rowOf (from) + rowDelta)];
            if (o.ref.audio)
            {
                AudioClip moved; bool got = false;
                for (auto& c : *from->aclips) if (c.id == o.ref.id) { moved = c; got = true; }
                if (! got) continue;
                s.editAudioClips (*from, [&] (std::vector<AudioClip>& v) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const AudioClip& c) { return c.id == o.ref.id; }), v.end()); });
                s.editAudioClips (*to, [&] (std::vector<AudioClip>& v) { v.push_back (moved); });
            }
            else
            {
                Clip moved; bool got = false;
                for (auto& c : *from->clips) if (c.id == o.ref.id) { moved = c; got = true; }
                if (! got) continue;
                s.editClips (*from, [&] (std::vector<Clip>& v) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const Clip& c) { return c.id == o.ref.id; }), v.end()); });
                s.editClips (*to, [&] (std::vector<Clip>& v) { v.push_back (moved); });
            }
            const ClipRef old = o.ref; o.ref.track = to->uid;
            for (auto& r : sel) if (r == old) r = o.ref;
            if (anchor == old) anchor = o.ref;
        }
        dragTrackRow += rowDelta;
    }
    void applyBand()
    {
        const auto r = bandRect();
        sel = bandBase;
        if (r.getWidth() < 2 && r.getHeight() < 2) return;
        const double a = bar (r.getX()), b = bar (r.getRight());
        std::vector<int> uids;
        auto rs = rows();
        for (int i = 0; i < (int) rs.size(); ++i) { const int y = rowY (i); if (y + laneH > r.getY() && y < r.getBottom()) uids.push_back (rs[(size_t) i]->uid); }
        if (uids.empty()) return;
        for (auto& c : ctx.s.clipsBetween (a, b, uids)) if (! isSelected (c)) sel.push_back (c);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        if (dragMode == DragMode::Ruler && ! moved && ! e.mods.isPopupMenu()) s.locate (magnetBar (bar (e.position.x), e.mods.isCommandDown(), {}));
        if (dragMode == DragMode::Band && ! moved)
        {
            // a click on empty space: the playhead goes there, the selection clears
            if (! bandAdd) sel.clear();
            s.locate (magnetBar (bar (e.position.x), e.mods.isCommandDown(), {}));
        }
        songDrag = -1; dragLane = -1; dragPoint = -1; markerDrag = -1; origins.clear();
        dragMode = DragMode::None; repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // double-click the ruler (not a marker): a marker there
        if (e.y >= top && e.y < ruler && e.x >= left && markerAt (e.x) < 0)
        {
            const double b = magnetBar (bar (e.position.x), e.mods.isCommandDown(), {});
            ctx.s.editMarkers ([b] (std::vector<Marker>& m) { m.push_back ({ "marker " + juce::String (m.size() + 1), b }); });
            repaint();
        }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (autoArea().contains (e.getPosition()) && ! e.mods.isCommandDown()) { laneScroll = std::max (0, laneScroll + (w.deltaY < 0 ? 1 : w.deltaY > 0 ? -1 : 0)); repaint(); return; }
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoomAt (std::pow (2.0, (double) w.deltaY * 3.0), juce::jmax ((double) left, (double) e.x)); return; }
        if (e.mods.isAltDown()) { setTrackHeight (laneH + (w.deltaY > 0 ? 6 : w.deltaY < 0 ? -6 : 0)); return; }
        const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX, dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
        scrollBar = std::max (0.0, scrollBar - dx * 500.0 / pxPerBar);
        scrollY = juce::jlimit (0, maxScrollY(), scrollY - (int) (dy * 400));
        repaint();
    }
    void mouseMagnify (const juce::MouseEvent& e, float scale) override { zoomAt (scale, juce::jmax ((double) left, (double) e.x)); }
    void clipMenu (Hit h)
    {
        juce::PopupMenu m;
        const bool muted = ctx.s.clipMuted (h.ref);
        if (h.ref.audio) m.addItem (1, "edit clip (gain, fades, pitch, stretch)...");
        bool hasLine = false, isNotes = false;
        if (auto* ct = ctx.s.clipTrack (h.ref)) { if (h.ref.audio) { for (auto& c : *ct->aclips) if (c.id == h.ref.id) hasLine = ! c.gainLine.empty(); } else for (auto& c : *ct->clips) if (c.id == h.ref.id) isNotes = c.notes != nullptr; }
        if (h.ref.audio) m.addItem (15, "spectral edit (its spectrogram: cut or boost frequencies over time)...");
        if (h.ref.audio) { m.addItem (11, "draw its gain line (click: a point, drag, double-click: remove)"); if (hasLine) m.addItem (12, "clear its gain line"); }
        if (h.ref.audio)
        {
            juce::PopupMenu wm;
            wm.addItem (20, "set warp markers (click: a marker, drag it, double-click: remove)");
            wm.addItem (21, "markers at the transients");
            juce::PopupMenu sl;
            sl.addItem (40, "at its hits (transients)"); sl.addItem (41, "at the grid (" + timeline::gridName (ctx.s.gridIndex.load(), pxPerBeat()) + ")");
            for (int k : { 2, 4, 8, 16, 32 }) sl.addItem (50 + k, "into " + juce::String (k) + " equal parts");
            sl.addItem (42, "at its warp markers"); sl.addSeparator(); sl.addItem (43, "into a sampler on a new track (a slice a step)");
            m.addSubMenu ("slice", sl); wm.addItem (22, "markers to the grid (quantise the audio)"); wm.addItem (23, "clear the markers");
            m.addSubMenu ("warp", wm);
        }
        {
            int ctb = -1; if (auto* ct = ctx.s.clipTrack (h.ref)) { if (h.ref.audio) { for (auto& c : *ct->aclips) if (c.id == h.ref.id) ctb = c.timebase; } else for (auto& c : *ct->clips) if (c.id == h.ref.id) ctb = c.timebase; }
            juce::PopupMenu tb; tb.addItem (30, "the track's", true, ctb < 0); tb.addItem (31, "beats", true, ctb == 0); tb.addItem (32, "time (keeps its seconds)", true, ctb == 1);
            m.addSubMenu ("timebase", tb);
        }
        {
            // the clip's own automation: any control of its track (the devices, the strip)
            juce::PopupMenu add, ed, rem; envTargets.clear();
            if (auto* ct = ctx.s.clipTrack (h.ref))
            {
                auto addDev = [&] (Device& d, const juce::String& nm) { juce::PopupMenu sub; for (int i = 0; i < std::min (d.numParams(), 120); ++i) { sub.addItem (1000 + (int) envTargets.size(), d.paramName (i)); envTargets.push_back ({ d.uid, i }); } add.addSubMenu (nm, sub); };
                addDev (*ct->mixer, "strip"); if (ct->instrument) addDev (*ct->instrument, ct->instrument->name); for (auto& d : ct->inserts) addDev (*d, d->name);
                if (auto* ev = envsOf (h.ref)) for (int k = 0; k < (int) ev->size(); ++k)
                {
                    auto* d = ct->device ((*ev)[(size_t) k].device); const auto nm = d ? d->name + ": " + d->paramName ((*ev)[(size_t) k].param) : juce::String ("?");
                    ed.addItem (3000 + k, nm); rem.addItem (4000 + k, nm);
                }
            }
            juce::PopupMenu ca; ca.addSubMenu ("add", add); ca.addSubMenu ("edit", ed, ed.getNumItems() > 0); ca.addSubMenu ("remove", rem, rem.getNumItems() > 0);
            m.addSubMenu ("clip automation", ca);
        }
        if (isNotes) m.addItem (13, "edit the notes...  (double-click)");
        else if (! h.ref.audio) m.addItem (14, "a note clip from its pattern (free notes)");
        m.addItem (6, "position and length...");
        m.addItem (2, "split at the playhead  (cmd+t)");
        m.addItem (7, "duplicate  (cmd+d)");
        m.addItem (8, muted ? "unmute" : "mute", true, muted);
        m.addItem (9, "loop these clips");
        m.addItem (10, "rename...");
        {
            juce::PopupMenu col; static const juce::uint32 cols[] = { 0xffee8e6e, 0xfff6c590, 0xffc9b53b, 0xff9cc788, 0xff76a582, 0xff8ec3e6, 0xff6e80c8, 0xff8d9bd6, 0xffcfe6f0, 0xffdfe9ed, 0xffb88a26, 0xff2f4a52 };
            col.addItem (200, "the track's colour");
            for (int k = 0; k < 12; ++k) { juce::Image sw (juce::Image::ARGB, 16, 12, true); juce::Graphics (sw).fillAll (juce::Colour (cols[k])); col.addItem (201 + k, "colour " + juce::String (k + 1), true, false, sw); }
            m.addSubMenu ("colour", col);
        }
        const auto takes = ctx.s.takesOf (h.ref);
        if (takes.size() > 1)
        {
            juce::PopupMenu tk;
            for (int k = 0; k < (int) takes.size(); ++k) tk.addItem (300 + k, "take " + juce::String (k + 1), true, ! ctx.s.clipMuted (takes[(size_t) k]));
            m.addSubMenu ("takes (alt+up / down)", tk);
        }
        if (h.ref.audio) m.addItem (3, "reverse");
        else { juce::PopupMenu sub; for (int k = 0; k < kPatterns; ++k) sub.addItem (100 + k, "pattern " + juce::String (k + 1)); m.addSubMenu ("pattern", sub); }
        m.addSeparator(); m.addItem (4, "remove  (delete)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, h] (int r)
        {
            auto& s = ctx.s;
            auto* t = s.clipTrack (h.ref);
            if (r == 0 || ! t) return;
            const int id = h.ref.id;
            if (r == 1) clipEditor (h.ref);
            else if (r == 2) splitSel();
            else if (r == 3) s.editAudioClips (*t, [id] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) c.reverse = ! c.reverse; });
            else if (r == 4) deleteSel();
            else if (r == 6) positionDialog (h.ref);
            else if (r == 7) duplicateSel();
            else if (r == 8) muteSel();
            else if (r == 9) loopSelection();
            else if (r >= 1000 && r < 3000 && r - 1000 < (int) envTargets.size())
            {
                const auto tg = envTargets[(size_t) (r - 1000)]; auto* ev = envsOf (h.ref); if (! ev) return;
                auto all = *ev; float now = 0.5f; if (auto* d = t->device (tg.first)) now = d->baseValue (tg.second);
                double s0 = 0, len = 0, origin = 0; s.clipSpan (h.ref, s0, len); juce::Rectangle<float> fr; envsOf (h.ref, &fr, &origin);
                all.push_back ({ tg.first, tg.second, { { (s0 - origin) * 4.0, now }, { (s0 + len - origin) * 4.0, now } } });
                setEnvs (h.ref, all); envClip = h.ref; envIndex = (int) all.size() - 1;
                ctx.status ("clip automation: click in the clip for a point, drag it, double-click to remove; click outside when done");
            }
            else if (r >= 3000 && r < 4000) { envClip = h.ref; envIndex = r - 3000; }
            else if (r >= 4000 && r < 5000) { if (auto* ev = envsOf (h.ref)) { auto all = *ev; if (r - 4000 < (int) all.size()) all.erase (all.begin() + (r - 4000)); setEnvs (h.ref, all); } envClip = {}; }
            else if (r >= 30 && r <= 32)
            {
                const int tbv = r - 31; auto targets = isSelected (h.ref) ? sel : std::vector<ClipRef> { h.ref };
                for (auto& cr : targets) if (auto* ct = s.clipTrack (cr)) { if (cr.audio) s.editAudioClips (*ct, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == cr.id) c.timebase = tbv; }); else s.editClips (*ct, [&] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == cr.id) c.timebase = tbv; }); }
            }
            else if (r == 20) { warpClip = h.ref; s.editAudioClips (*t, [id] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id && c.mode != 3) { c.mode = 3; c.warp.clear(); } }); ctx.status ("warp markers: click the audio for a marker, drag it in time (the audio stretches to it), double-click to remove; click outside when done"); }
            else if (r == 21) { warpAtTransients (h.ref); warpClip = h.ref; }
            else if (r >= 40 && r <= 42) { auto pieces = s.sliceClip (h.ref, r == 40 ? 0 : r == 41 ? 1 : 3, gridBars(), 0); sel = pieces; ctx.status ("sliced into " + juce::String (pieces.size()) + " clips"); }
            else if (r >= 52 && r <= 82) { auto pieces = s.sliceClip (h.ref, 2, 0, r - 50); sel = pieces; ctx.status ("sliced into " + juce::String (pieces.size()) + " clips"); }
            else if (r == 43) { juce::String err; if (auto* nt = s.clipToSampler (h.ref, err)) { ctx.select (nt); ctx.status ("sliced into a sampler on " + nt->name + ui::U (": a slice a step (reorder the steps, or seq → slices)")); } else ctx.status (err); }
            else if (r == 22) warpQuantise (h.ref);
            else if (r == 23) s.editAudioClips (*t, [id] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) c.warp.clear(); });
            else if (r == 11) { gainClip = h.ref; ctx.status ("drawing the gain line: click inside the clip for a point, drag it, double-click to remove; click outside when done"); }
            else if (r == 12) s.setClipGainLine (h.ref, {});
            else if (r == 13) editNotes (h.ref);
            else if (r == 15) editSpectrum (h.ref);
            else if (r == 14) notesFromPattern (h.ref);
            else if (r == 10)
            {
                juce::String now; if (h.ref.audio) { for (auto& c : *t->aclips) if (c.id == id) now = c.name; } else for (auto& c : *t->clips) if (c.id == id) now = c.name;
                const auto ref = h.ref;
                ask ("rename the clip", "a name (empty: the file's or the pattern's)", now, [this, ref] (juce::String n) { ctx.s.setClipName (ref, n.trim()); repaint(); });
            }
            else if (r >= 200 && r < 213)
            {
                static const juce::uint32 cols[] = { 0xffee8e6e, 0xfff6c590, 0xffc9b53b, 0xff9cc788, 0xff76a582, 0xff8ec3e6, 0xff6e80c8, 0xff8d9bd6, 0xffcfe6f0, 0xffdfe9ed, 0xffb88a26, 0xff2f4a52 };
                auto targets = isSelected (h.ref) ? sel : std::vector<ClipRef> { h.ref };
                ctx.s.setClipColour (targets, r == 200 ? 0u : cols[r - 201]);
            }
            else if (r >= 300) { const auto tks = ctx.s.takesOf (h.ref); if (r - 300 < (int) tks.size()) { ctx.s.chooseTake (tks[(size_t) (r - 300)]); sel = { tks[(size_t) (r - 300)] }; } }
            else if (r >= 100) s.editClips (*t, [id, r] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == id) c.pattern = r - 100; });
            repaint();
        });
    }
    /** a pattern clip into a note clip: what the pattern's steps play over the clip, as notes (the step's pitch, length,
        velocity, chords; not its chances or conditions: a note clip plays what it shows) */
    void notesFromPattern (ClipRef r)
    {
        auto& s = ctx.s; auto* t = s.clipTrack (r); if (! t) return;
        const Clip* c = nullptr; for (auto& x : *t->clips) if (x.id == r.id) c = &x;
        if (! c) return;
        auto pat = t->patternAt (c->pattern); if (! pat) return;
        const double stepB = s.stepBeatsOf (*t, s.tempo());
        const int len = juce::jlimit (1, kMaxSteps, pat->length);
        std::vector<Note> v; double at = 0; const double end = c->offset + c->bars * 4.0;
        const int root = t->rootNote.load();
        for (int k = 0; at < end - 1.0e-9 && k < 100000; ++k)
        {
            const Step& st = pat->s[(size_t) (k % len)];
            const double d = juce::jlimit (0.05f, 16.0f, st.dur) * stepB;
            if (st.on && at >= c->offset - 1.0e-9)
            {
                const int note = st.slice >= 0 ? 36 + st.slice : root + st.pitch;
                const int* stack = nullptr; const int extra = st.slice >= 0 ? 0 : chordNotes (st.chord, stack);
                for (int q = -1; q < extra; ++q) v.push_back ({ at, std::max (0.02, (double) st.len) * stepB, juce::jlimit (0, 127, q < 0 ? note : note + stack[q]), st.vel });
            }
            at += d;
        }
        s.editClips (*t, [&] (std::vector<Clip>& cs) { for (auto& x : cs) if (x.id == r.id) { x.notes = std::make_shared<const std::vector<Note>> (v); if (x.name.isEmpty()) x.name = "p" + juce::String (x.pattern + 1) + " notes"; } });
        ctx.status (juce::String (v.size()) + " notes from pattern " + juce::String (c->pattern + 1));
    }
    void clipEditor (ClipRef ref)
    {
        auto* t = ctx.s.clipTrack (ref);
        if (! t || ! ref.audio) return;
        const AudioClip* found = nullptr; for (auto& c : *t->aclips) if (c.id == ref.id) found = &c;
        if (! found) return;
        struct Ed : juce::Component
        {
            Ed (Session& s, Track& t, int id) : session (s), track (t), clipId (id)
            {
                const AudioClip* c = nullptr; for (auto& x : *t.aclips) if (x.id == id) c = &x;
                if (! c) return;
                add (gain = new ValueRow ("gain", -48.0f, 12.0f, juce::Decibels::gainToDecibels (c->gain, -48.0f), [] (float v) { return juce::String (v, 1) + " db"; }));
                add (fin = new ValueRow ("fade in", 0.0f, 4.0f, (float) c->fadeIn, [] (float v) { return juce::String (v * 1000, 0) + " ms"; }));
                add (fout = new ValueRow ("fade out", 0.0f, 4.0f, (float) c->fadeOut, [] (float v) { return juce::String (v * 1000, 0) + " ms"; }));
                add (pitch = new ValueRow ("pitch", -24.0f, 24.0f, c->pitch, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; }));
                add (mode = new ValueRow ("mode", { "repitch", "stretch", "warp to tempo", "warp markers" }, c->mode));
                add (stretch = new ValueRow ("stretch", 0.25f, 4.0f, (float) c->stretch, [] (float v) { return "x" + juce::String (v, 2); }));
                add (offset = new ValueRow ("start in file", 0.0f, (float) std::max (0.01, c->sample ? c->sample->seconds() : 1.0), (float) c->offset, [] (float v) { return juce::String (v, 3) + " s"; }));
                add (rev = new ValueRow ("reverse", { "off", "on" }, c->reverse ? 1 : 0)); rev->boolStyle = true;
                add (loop = new ValueRow ("loop", { "off", "on" }, c->loop ? 1 : 0)); loop->boolStyle = true;
                add (mute = new ValueRow ("mute", { "off", "on" }, c->muted ? 1 : 0)); mute->boolStyle = true;
                for (auto* r : rows) r->onChange = [this] (float) { push(); };
                setSize (320, 24 + (int) rows.size() * 20 + 8);
            }
            void push()
            {
                session.editAudioClips (track, [this] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == clipId) { c.gain = juce::Decibels::decibelsToGain (gain->get(), -48.0f); c.fadeIn = fin->get(); c.fadeOut = fout->get(); c.pitch = pitch->get(); c.mode = mode->getInt(); c.stretch = stretch->get(); c.offset = offset->get(); c.reverse = rev->getInt() > 0; c.loop = loop->getInt() > 0; c.muted = mute->getInt() > 0; } });
            }
            void add (ValueRow* r) { rows.push_back (r); owned.add (r); addAndMakeVisible (r); }
            void resized() override { int y = 22; for (auto* r : rows) { r->setBounds (8, y, getWidth() - 16, 18); y += 20; } }
            void paint (juce::Graphics& g) override { g.fillAll (studioTheme().ground.brighter (0.12f)); g.setColour (studioTheme().ink); g.setFont (uiFont (11.0f, true)); g.drawText ("clip", 8, 4, 200, 16, juce::Justification::centredLeft); }
            Session& session; Track& track; int clipId; std::vector<ValueRow*> rows; juce::OwnedArray<ValueRow> owned;
            ValueRow *gain = nullptr, *fin = nullptr, *fout = nullptr, *pitch = nullptr, *mode = nullptr, *stretch = nullptr, *offset = nullptr, *rev = nullptr, *loop = nullptr, *mute = nullptr;
        };
        auto* ed = new Ed (ctx.s, *t, ref.id);
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (ed), juce::Rectangle<int> (getScreenX() + x (found->startBar), getScreenY() + lanesArea().getY(), 10, 10), nullptr);
    }
    // ---- drops --------------------------------------------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi")) return true; return false; }
    void filesDropped (const juce::StringArray& files, int px, int py) override { for (auto& f : files) dropFile (juce::File (f), px, py); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("file:"); }
    void itemDropped (const SourceDetails& d) override { dropFile (juce::File (d.description.toString().substring (5)), d.localPosition.x, d.localPosition.y); }
    std::unique_ptr<juce::FileChooser> chooser;
    /** a midi file as a note clip (its first track with notes), as long as its notes in whole bars */
    int midiClip (Track& t, const juce::File& f, double atBar)
    {
        double len = 0; CtrlPtr ctl; auto notes = Session::notesFromMidi (f, len, &ctl);
        if (! notes) { ctx.status ("no notes in " + f.getFileName()); return 0; }
        const auto& m = meter(); const long long b0 = m.barAt (atBar * 4.0); long long b1 = b0 + 1; while (m.barStart (b1) - atBar * 4.0 < len - 1.0e-9) ++b1;
        const int id = ctx.s.addNoteClip (t, atBar, (m.barStart (b1) - atBar * 4.0) / 4.0, *notes, f.getFileNameWithoutExtension());
        if (ctl) ctx.s.editClips (t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == id) c.ctrl = ctl; });
        sel = { { t.uid, id, false } }; ctx.status (juce::String (notes->size()) + " notes from " + f.getFileName() + " on " + t.name); repaint();
        return id;
    }
    void dropFile (const juce::File& f, int px, int py)
    {
        auto& s = ctx.s;
        const auto la = lanesArea();
        auto rs = rows();
        const int row = rowAt (py);
        Track* t = (la.contains (px, py) && row >= 0 && row < (int) rs.size()) ? rs[(size_t) row] : nullptr;
        if (f.hasFileExtension ("mid;midi"))
        {
            if (! t || t->isAudio()) t = ctx.selected() && ! ctx.selected()->isAudio() ? ctx.selected() : nullptr;
            if (t) midiClip (*t, f, px >= left ? magnetBar (bar (px), false, {}) : s.currentBar()); else ctx.status ("a midi file goes on an instrument track");
            return;
        }
        if (t && ! t->isAudio() && t->instrument)
        {
            // onto an instrument track: a sampler device takes the file
            if (auto* h = dynamic_cast<SampleHolder*> (t->instrument->processor())) { h->setSampleFile (f); ctx.status ("sample loaded into " + t->name); repaint(); return; }
            t = nullptr;
        }
        if (! t) { t = s.addTrack (f.getFileNameWithoutExtension(), -1, TrackKind::Audio); if (! t) return; }
        const double b = px >= left ? magnetBar (bar (px), false, {}) : s.currentBar();
        juce::String err;
        if (auto* c = s.addAudioClip (*t, f, b, &err)) sel = { { t->uid, c->id, true } };
        if (err.isNotEmpty()) ctx.status (err); else ctx.status (f.getFileName() + " on " + t->name);
        ctx.select (t);
        repaint();
    }
    void tick() override
    {
        // follow: turn the page when the playhead leaves the view (not while dragging)
        auto& s = ctx.s;
        if (s.follow.load() && s.playPpq.load() >= 0 && dragMode == DragMode::None && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        {
            const double cb = s.currentBar(), vb = visibleBars();
            if (cb > scrollBar + vb * 0.92 || cb < scrollBar) scrollBar = std::max (0.0, cb - vb * 0.05);
        }
        repaint();
    }
private:
    ui::TextLink addLane, addSong, snapLink, gridLink, zoomIn, zoomOut, fitLink, selLink, taller, shorter, followLink;
    DragMode dragMode = DragMode::None;
    juce::Point<int> downPos, bandA, bandB; double downBar = 0; bool moved = false, bandAdd = false; std::vector<ClipRef> bandBase;
    struct Origin { ClipRef ref; double start = 0; };
    std::vector<Origin> origins; ClipRef anchor; double anchorStart = 0, anchorLen = 0; int dragTrackRow = 0;
    double loopA = 0, scrollStart = 0; int songDrag = -1, songStart = 0, markerDrag = -1;
    int dragLane = -1, dragPoint = -1; bool curveDrag = false; double lastFreeBeat = 0; Target laneTarget;
};

// =================================================================================================
//  seq: the step grid, the step, the pattern tools, the device chain
// =================================================================================================
/** rules between tracks: one a row, every part a menu (click it) */
class RulesPanel : public juce::Component
{
public:
    explicit RulesPanel (Ctx& c) : ctx (c), theme (studioTheme()) { setSize (720, 60 + 22 * 8); }
    static constexpr int rowH = 22, top = 40;
    std::vector<Rule> list() const { return *ctx.s.rules; }
    juce::String trackName (int uid) const { if (auto* t = ctx.s.trackByUid (uid)) return t->name; return "(no track)"; }
    // the parts of a row: x positions
    struct Part { int x, w; };
    std::array<Part, 9> parts() const { return { { { 8, 24 }, { 36, 34 }, { 70, 120 }, { 190, 150 }, { 340, 120 }, { 460, 150 }, { 610, 50 }, { 660, 30 }, { 690, 24 } } }; }
    juce::String partText (const Rule& r, int k) const
    {
        switch (k)
        {
            case 0: return r.on ? "on" : "off";
            case 1: return "when";
            case 2: return trackName (r.src);
            case 3: return r.when == RULE_EVERY ? "has played " + juce::String (r.n) + " times" : juce::String (ruleWhenName (r.when));
            case 4: return trackName (r.dst);
            case 5:
                if (r.what == RULE_TRANSPOSE) return "is transposed " + juce::String ((int) r.value) + " st";
                if (r.what == RULE_TRIGGER) return "plays a note (+" + juce::String ((int) r.value) + ")";
                if (r.what == RULE_ACCENT) return "is played x" + juce::String (r.value, 2);
                if (r.what == RULE_PATTERN) return "switches to p" + juce::String ((int) r.value + 1);
                return ruleDoName (r.what);
            case 6: return juce::String (juce::roundToInt (r.chance * 100)) + "%";
            case 7: return juce::String ((int) r.windowMs) + "ms";
            default: return "x";
        }
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground); g.setColour (theme.ink.withAlpha (0.3f)); g.drawRect (getLocalBounds());
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText ("rules between tracks", 8, 6, 300, 16, juce::Justification::centredLeft);
        g.setFont (uiFont (9.0f)); g.setColour (theme.ink.withAlpha (0.7f));
        g.drawText (ui::U ("click a part to change it · the last two: how often it applies, how close in time counts as together"), 8, 22, getWidth() - 16, 14, juce::Justification::centredLeft);
        g.setFont (uiFont (10.0f));
        const auto v = list(); int y = top;
        for (auto& r : v)
        {
            for (int k = 0; k < 9; ++k)
            {
                const auto pt = parts()[(size_t) k];
                g.setColour (k == 1 ? theme.ink.withAlpha (0.6f) : k == 8 ? hotColour() : (k == 2 || k == 4) ? theme.ink : theme.ink.withAlpha (r.on ? 0.95f : 0.4f));
                if (k == 0) { g.setColour (r.on ? theme.fill : theme.ink.withAlpha (0.15f)); g.fillRect (pt.x, y + 3, pt.w, rowH - 6); g.setColour (r.on ? ui::inkFor (theme.fill) : theme.ink); }
                g.drawText (partText (r, k), pt.x + 2, y, pt.w - 4, rowH, juce::Justification::centredLeft, true);
            }
            y += rowH;
        }
        g.setColour (theme.ink); g.drawText ("+ rule", 8, y + 2, 80, rowH, juce::Justification::centredLeft);
        g.drawText ("close", getWidth() - 60, 6, 52, 16, juce::Justification::centredRight);
    }
    std::function<void()> onClose;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 22 && e.x > getWidth() - 70) { if (onClose) onClose(); return; }
        auto v = list();
        const int row = (e.y - top) / rowH;
        if (e.y >= top && row == (int) v.size())
        {
            Rule r; if (ctx.s.tracks.size() >= 2) { r.src = ctx.s.tracks[0]->uid; r.dst = ctx.s.tracks[1]->uid; } else if (! ctx.s.tracks.empty()) r.src = r.dst = ctx.s.tracks[0]->uid;
            if (auto* t = ctx.selected()) r.dst = t->uid;
            v.push_back (r); ctx.s.setRules (v); grow(); return;
        }
        if (e.y < top || row < 0 || row >= (int) v.size()) return;
        int k = -1; for (int i = 0; i < 9; ++i) { const auto pt = parts()[(size_t) i]; if (e.x >= pt.x && e.x < pt.x + pt.w) k = i; }
        auto setRule = [this, row] (std::function<void (Rule&)> fn) { auto w = list(); if (row < (int) w.size()) { fn (w[(size_t) row]); ctx.s.setRules (w); repaint(); } };
        juce::PopupMenu m;
        if (k == 0) { setRule ([] (Rule& r) { r.on = ! r.on; }); return; }
        if (k == 8) { v.erase (v.begin() + row); ctx.s.setRules (v); grow(); return; }
        if (k == 2 || k == 4) { for (int i = 0; i < (int) ctx.s.tracks.size(); ++i) m.addItem (100 + i, ctx.s.tracks[(size_t) i]->name); }
        else if (k == 3) { m.addItem (1, ruleWhenName (RULE_WITH)); m.addItem (2, ruleWhenName (RULE_WITHOUT)); juce::PopupMenu ev; for (int nn : { 2, 3, 4, 5, 6, 7, 8, 12, 16 }) ev.addItem (10 + nn, "every " + juce::String (nn) + "th"); m.addSubMenu ("every n-th time it plays", ev); }
        else if (k == 5)
        {
            m.addItem (1, ruleDoName (RULE_ONLY)); m.addItem (2, ruleDoName (RULE_MUTE));
            juce::PopupMenu tr, tg, ac, pt;
            for (int st : { -12, -7, -5, -2, 2, 3, 5, 7, 12 }) tr.addItem (100 + st + 24, (st > 0 ? "+" : "") + juce::String (st) + " st");
            for (int st : { 0, 3, 5, 7, 12 }) tg.addItem (200 + st, "its root +" + juce::String (st));
            for (int a : { 25, 50, 75, 125, 150, 200 }) ac.addItem (300 + a, "x" + juce::String (a / 100.0, 2));
            for (int p = 0; p < 16; ++p) pt.addItem (600 + p, "pattern " + juce::String (p + 1));
            m.addSubMenu ("is transposed (toggles each time)", tr); m.addSubMenu ("plays a note", tg); m.addSubMenu ("is accented / softened", ac); m.addSubMenu ("switches pattern (session)", pt);
        }
        else if (k == 6) { for (int c : { 100, 75, 50, 33, 25, 10 }) m.addItem (c, juce::String (c) + "%"); }
        else if (k == 7) { for (int w : { 2, 5, 10, 20, 40, 80, 160 }) m.addItem (w, juce::String (w) + " ms"); }
        else return;
        m.showMenuAsync (juce::PopupMenu::Options(), [this, k, setRule] (int r)
        {
            if (r <= 0) return;
            setRule ([this, k, r] (Rule& x)
            {
                if (k == 2 && r >= 100) x.src = ctx.s.tracks[(size_t) (r - 100)]->uid;
                else if (k == 4 && r >= 100) x.dst = ctx.s.tracks[(size_t) (r - 100)]->uid;
                else if (k == 3) { if (r == 1) x.when = RULE_WITH; else if (r == 2) x.when = RULE_WITHOUT; else { x.when = RULE_EVERY; x.n = r - 10; } }
                else if (k == 5)
                {
                    if (r == 1) x.what = RULE_ONLY; else if (r == 2) x.what = RULE_MUTE;
                    else if (r >= 100 && r < 200) { x.what = RULE_TRANSPOSE; x.value = (float) (r - 124); }
                    else if (r >= 200 && r < 300) { x.what = RULE_TRIGGER; x.value = (float) (r - 200); }
                    else if (r >= 300 && r < 600) { x.what = RULE_ACCENT; x.value = (r - 300) / 100.0f; }
                    else if (r >= 600) { x.what = RULE_PATTERN; x.value = (float) (r - 600); }
                }
                else if (k == 6) x.chance = r / 100.0f;
                else if (k == 7) x.windowMs = (float) r;
            });
        });
    }
    void grow() { setSize (getWidth(), top + rowH * ((int) list().size() + 1) + 10); repaint(); }
private:
    Ctx& ctx; ui::Theme theme;
};

class SeqPage : public Page
{
public:
    explicit SeqPage (Ctx& c) : Page (c)
    {
        auto link = [&] (ui::TextLink*& l, const char* name, std::function<void()> fn) { l = links.add (new ui::TextLink (name, theme)); l->onClick = std::move (fn); addAndMakeVisible (l); };
        link (tEuclid, "euclid", [this] { euclid(); });
        link (tRotL, "< rotate", [this] { tool ([] (Pattern& p) { tools::rotate (p, -1); }); });
        link (tRotR, "rotate >", [this] { tool ([] (Pattern& p) { tools::rotate (p, 1); }); });
        link (tRev, "reverse", [this] { tool ([] (Pattern& p) { tools::reverse (p); }); });
        link (tMutate, "mutate", [this] { tool ([this] (Pattern& p) { tools::mutate (p, rng); }); });
        link (tHuman, "humanise", [this] { tool ([this] (Pattern& p) { tools::humanise (p, rng); }); });
        link (tRandom, "random", [this] { tool ([this] (Pattern& p) { tools::randomise (p, rng); }); });
        link (tMarkov, "markov", [this] { tool ([this] (Pattern& p) { tools::markov (p, rng); }); });
        link (tRules, "rewrite", [this] { tool ([] (Pattern& p) { tools::rewrite (p); }); });
        link (tDouble, "double", [this] { tool ([] (Pattern& p) { tools::doubleLength (p); }); });
        link (tClear, "clear", [this] { tool ([] (Pattern& p) { p = Pattern(); }); });
        // second row: time and pitch systems
        link (tRatios, "ratios", [this] { ask ("step times", "one number per step: how long it lasts, in steps. e.g. 1 1 2/3 2/3 2/3 3/2", "1 1 2/3 2/3 2/3 3/2", [this] (juce::String t) { tool ([t] (Pattern& p) { tools::ratios (p, t); }); }); });
        link (tAccel, "accelerate", [this] { ask ("accelerate", "every step lasts this many times the one before (below 1: faster and faster; the loop keeps its length)", "0.9", [this] (juce::String t) { const float f = t.getFloatValue(); tool ([f] (Pattern& p) { tools::accelerate (p, f); }); }); });
        link (tEven, "even", [this] { tool ([] (Pattern& p) { tools::evenTime (p); }); });
        link (tStrict, "strict", [this] { ask ("strict grid", "velocity levels (1 = all the same, 2 = accent or not ...), then the note length in steps", "2 0.5", [this] (juce::String t) { auto a = juce::StringArray::fromTokens (t, " ,", ""); const int lv = a[0].getIntValue(); const float ln = a.size() > 1 ? a[1].getFloatValue() : 0.5f; tool ([lv, ln] (Pattern& p) { tools::strictGrid (p, lv, ln > 0 ? ln : 0.5f); }); }); });
        link (tSieve, "sieve", [this] { ask ("sieve: which steps", "residue classes modulus@residue, | joins, & intersects, ! excludes. e.g. 3@0 | 4@1", "3@0 | 4@1", [this] (juce::String t) { bool ok = false; tools::sieveHolds (t, 0, &ok); if (! ok) { ctx.status ("sieve: could not read \"" + t + "\""); return; } tool ([t] (Pattern& p) { tools::sieveRhythm (p, t); }); }); });
        link (tSievePitch, "sieve pitch", [this] { ask ("sieve: the pitches of the hits", "the sieve as a scale in semitones, e.g. 3@0 | 4@1 (add \"random\" to draw from it)", "3@0 | 4@1", [this] (juce::String t) { const bool rand = t.containsIgnoreCase ("random"); const auto e = t.replace ("random", "", true).trim(); tool ([this, e, rand] (Pattern& p) { tools::sievePitch (p, e, rand ? &rng : nullptr); }); }); });
        link (tCloud, "cloud", [this] { ask ("stochastic cloud", "events a step (the times between them are exponential), then the pitch range in semitones", "1 12", [this] (juce::String t) { auto a = juce::StringArray::fromTokens (t, " ,", ""); const float d = a[0].getFloatValue(); const int r = a.size() > 1 ? a[1].getIntValue() : 12; tool ([this, d, r] (Pattern& p) { tools::stochastic (p, rng, d > 0 ? d : 1.0f, r); }); }); });
        link (tFreeze, "freeze", [this] { if (auto* t = ctx.selected()) { ctx.s.freezeEvolution (*t); refresh(); } });
        link (tAdditive, "additive", [this] { processMenu(); });
        tAdditive->setTooltip (ui::U ("the pattern as a cell that grows note by note (1, 1 2, 1 2 3 …), shrinks, or both: the figures go into the next patterns and play as a chain"));
        link (tType, "type", [this] { typeDialog(); });
        link (tSlices, "slices", [this] { slicesMenu(); });
        tSlices->setTooltip ("chop: the steps' sample slices in order, reversed, shuffled, wandering, any slice each time; the sampler's start moved a little on every step");
        link (tGroove, "groove", [this] { grooveMenu(); });
        tGroove->setTooltip ("a groove for this track: built-in swings and feels, or one taken from a clip (an audio loop's hits, a note clip's notes) or a pattern; how much; or written into the steps");
        link (tFollow, "follow", [this] { followMenu(); });
        tFollow->setTooltip (ui::U ("follow actions (session): after this pattern has played n times, play the next, another, any, the first … (with a chance for a second choice)"));
        link (tMidiFx, "midi fx", [this] { midiFxMenu(); });
        link (tTrackRules, "rules", [this] { auto* rp = new RulesPanel (ctx); rp->grow(); juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (rp), tTrackRules->getScreenBounds(), nullptr); });
        tTrackRules->setTooltip ("rules between tracks: when one plays (or doesn't, or every n-th time), another plays only then, is silenced, plays a note, is transposed, accented, or switches pattern");
        tMidiFx->setTooltip ("midi effects in front of the instrument: arpeggiator, chord, generator, chance");
        tType->setTooltip ("type the pattern: one word per step. a number is a note (a degree with a scale), x the root, . a rest, _ holds; ! accent, ~ soft, ? half chance, *3 ratchet, ' and , an octave up / down");
        tFreeze->setTooltip ("write what the evolving pattern has drifted to into the pattern");
        link (tCopy, "copy", [this] { if (auto* t = ctx.selected()) if (auto p = t->patternAt (t->pattern.load())) clip = *p; });
        link (tPaste, "paste", [this] { tool ([this] (Pattern& p) { p = clip; }); });
        rng.seed ((uint32_t) juce::Random::getSystemRandom().nextInt());
        link (tAddDevice, "+ device", [this] { addDeviceMenu(); });
        link (tAddLock, "+ lock", [this] { lockMenu(); });
        juce::StringArray pats; for (int i = 0; i < kPatterns; ++i) pats.add (juce::String (i + 1));
        auto row = [&] (ValueRow*& r, ValueRow* nr) { r = rows.add (nr); addAndMakeVisible (r); };
        row (rPattern, new ValueRow ("pattern", pats, 0));
        row (rLength, new ValueRow ("length", 1.0f, (float) kMaxSteps, 16.0f, [] (float v) { return juce::String (juce::roundToInt (v)); }));
        juce::StringArray spd; for (int i = 0; i < kSpeeds; ++i) spd.add (speedName (i));
        row (rSpeed, new ValueRow ("speed", spd, 3));
        row (rSwing, new ValueRow ("swing", -0.25f, 0.25f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (rShift, new ValueRow ("shift", -30.0f, 30.0f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " ms"; }));
        row (rProb, new ValueRow ("probability", 0.0f, 1.0f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (rRoot, new ValueRow ("root note", 0.0f, 127.0f, 60.0f, [] (float v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); }));
        row (rFree, new ValueRow ("clock", { "grid", "free (ms)" }, 0));
        row (rStepMs, new ValueRow ("step ms", 10.0f, 2000.0f, 125.0f, [] (float v) { return juce::String (v, 1) + " ms"; }));
        row (rChain, new ValueRow ("chain", { "off", "on" }, 0)); rChain->boolStyle = true;
        row (rQuant, new ValueRow ("launch", { "now", "1 bar", "2 bars", "4 bars" }, 1));
        row (rEvolve, new ValueRow ("evolve", 0.0f, 1.0f, 0.0f, [] (float v) { return v < 0.005f ? juce::String ("off") : juce::String (juce::roundToInt (v * 100)) + "%"; }));
        rEvolve->setTooltip ("on every pass a few steps change or come back: the pattern drifts around what is written. \"freeze\" writes the drift into the pattern");
        row (rChoke, new ValueRow ("choke group", { "off", "a", "b", "c", "d", "e", "f", "g", "h" }, 0));
        { juce::StringArray sc { "semitones" }; sc.addArray (builtinScaleNames()); sc.add ("a scala file (.scl)..."); row (rScale, new ValueRow ("scale", sc, 0)); }
        rScale->setTooltip ("the pitch lane's scale: each step's pitch is a degree of it (0 the root). Built-in scales, or any Scala .scl tuning (Colundi sets included): degrees between the keys reach the instrument as a pitch bend");
        row (rPhase, new ValueRow ("phase", -5.0f, 5.0f, 0.0f, [] (float v) { return std::abs (v) < 0.005f ? juce::String ("in step") : (v > 0 ? "+" : "") + juce::String (v, 2) + "% " + (v > 0 ? "faster" : "slower"); }));
        rPhase->setTooltip ("this part runs a little faster or slower than the rest, so it drifts out of phase with them and, much later, back in");
        rChoke->setTooltip ("a hit on one track of a group cuts the other tracks of that group (closed hat / open hat, chopped samples)");
        // the step
        row (sVel, new ValueRow ("velocity", 0.0f, 1.0f, 0.8f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (sPitch, new ValueRow ("pitch", -36.0f, 36.0f, 0.0f, [this] (float v) { return pitchText (juce::roundToInt (v)); }));
        row (sProb, new ValueRow ("chance", 0.0f, 1.0f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (sRatchet, new ValueRow ("ratchet", 1.0f, 8.0f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v)); }));
        row (sMicro, new ValueRow ("micro", -1.0f, 1.0f, 0.0f, [this] (float v) { return (v > 0 ? "+" : "") + juce::String (v * ctx.s.microMs.load(), 1) + " ms"; }));
        row (sLen, new ValueRow ("length", 0.05f, 16.0f, 0.5f, [] (float v) { return juce::String (v, 2) + " steps"; }));
        row (sDur, new ValueRow ("step time", 0.25f, 4.0f, 1.0f, [] (float v) { return "x" + juce::String (v, 2); }));
        sDur->setTooltip ("how long this step lasts, as a ratio of the track's step: uneven grids (a triplet next to a straight step). The pattern's loop is the sum of its steps");
        juce::StringArray conds; for (int i = 0; i < (int) Cond::Count; ++i) conds.add (condName ((Cond) i));
        row (sCond, new ValueRow ("condition", conds, 0));
        row (sSlide, new ValueRow ("slide", { "off", "on" }, 0)); sSlide->boolStyle = true;
        row (sSlice, new ValueRow ("slice", -2.0f, 63.0f, -1.0f, [] (float v) { return v < -1.5f ? juce::String ("any (random)") : v < 0 ? juce::String ("note") : juce::String (juce::roundToInt (v) + 1); }));
        { juce::StringArray ch; for (int i = 0; i < kChords; ++i) ch.add (chordName (i)); row (sChord, new ValueRow ("chord", ch, 0)); }
        row (sRCurve, new ValueRow ("ratchet curve", -1.0f, 1.0f, 0.0f, [] (float v) { return std::abs (v) < 0.02f ? juce::String ("even") : (v > 0 ? "speeds up " : "slows ") + juce::String (juce::roundToInt (std::abs (v) * 100)) + "%"; }));
        sRCurve->setLabelWidth (90);
        for (int k = 0; k < kLocks; ++k) { row (sLock[k], new ValueRow ("lock " + juce::String (k + 1), 0.0f, 1.0f, 0.0f, [] (float v) { return juce::String (v, 2); })); sLock[k]->setLabelWidth (150); }
        wire();
        viewport.setViewedComponent (&chain, false); viewport.setScrollBarsShown (true, false); addAndMakeVisible (viewport);
    }
    ValueRow *rPattern, *rLength, *rSpeed, *rSwing, *rShift, *rProb, *rRoot, *rFree, *rStepMs, *rChain, *rQuant, *rEvolve, *rChoke, *rScale, *rPhase;
    ValueRow *sVel, *sPitch, *sProb, *sRatchet, *sMicro, *sLen, *sDur, *sCond, *sSlide, *sSlice, *sChord, *sRCurve; ValueRow* sLock[kLocks];
    /** a one-line question (the tools that take a text) */
    void ask (const juce::String& title, const juce::String& what, const juce::String& initial, std::function<void (juce::String)> then)
    {
        auto* w = new juce::AlertWindow (title, what, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("t", initial); w->addButton ("ok", 1); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, then] (int r) { if (r == 1) then (w->getTextEditorContents ("t")); }), true);
    }
    int selectedStep = 0, page = 0;
    void wire()
    {
        rPattern->onChange = [this] (float v) { if (auto* t = ctx.selected()) { ctx.s.launch (*t, (int) v); } refresh(); };
        rLength->onChange = [this] (float v) { editStep ([v] (Pattern& p, int) { p.length = (int) v; }); };
        rSpeed->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->speed.store ((int) v); };
        rSwing->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->swingOffset.store (v); };
        rShift->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->shiftMs.store (v); };
        rProb->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->probMul.store (v); };
        rRoot->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->rootNote.store ((int) v); };
        rFree->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->freeClock.store (v > 0.5f); };
        rStepMs->onChange = [this] (float v) { if (auto* t = ctx.selected()) t->stepMs.store (v); };
        rChain->onChange = [this] (float v) { if (auto* t = ctx.selected()) { t->chainOn.store (v > 0.5f); if (v > 0.5f && t->chain->empty()) chainEditor(); } };
        rChain->onMenu = [this] { chainEditor(); };
        rQuant->onChange = [this] (float v) { const int q[4] = { 0, 1, 2, 4 }; ctx.s.launchQuant.store (q[juce::jlimit (0, 3, (int) v)]); };
        rEvolve->onChange = [this] (float v) { if (auto* t = ctx.selected()) { t->evolve.store (v); ctx.s.dirty = true; } };
        rChoke->onChange = [this] (float v) { if (auto* t = ctx.selected()) { t->chokeGroup.store ((int) v); ctx.s.dirty = true; } };
        rPhase->onChange = [this] (float v) { if (auto* t = ctx.selected()) { t->phaseRate.store (1.0f + (std::abs (v) < 0.005f ? 0.0f : v) / 100.0f); ctx.s.dirty = true; } };
        rScale->onChange = [this] (float v)
        {
            auto* t = ctx.selected(); if (! t) return;
            const int i = (int) v, nb = builtinScaleNames().size();
            if (i == 0) ctx.s.setScale (*t, nullptr);
            else if (i <= nb) ctx.s.setScale (*t, builtinScale (i - 1));
            else
            {
                Track* tp = t;
                scl = std::make_unique<juce::FileChooser> ("a scala tuning", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.scl");
                scl->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, tp] (const juce::FileChooser& c)
                {
                    const auto f = c.getResult(); juce::String err;
                    if (f.existsAsFile() && ctx.s.indexOf (tp) >= 0) { if (auto sc = parseScala (f.loadFileAsString(), f.getFileNameWithoutExtension(), &err)) { ctx.s.setScale (*tp, sc); ctx.status ("scale: " + sc->name + " (" + juce::String (sc->size()) + " degrees)"); } else ctx.status (err); }
                    refresh();
                });
            }
            refresh();
        };
        sChord->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].chord = (int) v; }); };
        sRCurve->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].rcurve = std::abs (v) < 0.02f ? 0.0f : v; }); };
        sVel->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].vel = v; }); };
        sPitch->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].pitch = (int) std::round (v); }); };
        sProb->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].prob = v; }); };
        sRatchet->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].ratchet = (int) std::round (v); }); };
        sMicro->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].micro = v; }); };
        sLen->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].len = v; }); };
        sDur->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].dur = std::abs (v - 1.0f) < 0.02f ? 1.0f : v; }); };
        sCond->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].cond = (Cond) (int) v; }); };
        sSlide->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].slide = v > 0.5f; }); };
        sSlice->onChange = [this] (float v) { editStep ([v] (Pattern& p, int i) { p.s[(size_t) i].slice = (int) std::round (v); }); };
        for (int k = 0; k < kLocks; ++k)
        {
            sLock[k]->onChange = [this, k] (float v) { editStep ([v, k] (Pattern& p, int i) { p.s[(size_t) i].locks[(size_t) k].value = v; }); };
            sLock[k]->onMenu = [this, k] { juce::PopupMenu m; m.addItem (1, "remove this lock"); m.showMenuAsync (juce::PopupMenu::Options(), [this, k] (int r) { if (r == 1) editStep ([k] (Pattern& p, int i) { p.s[(size_t) i].locks[(size_t) k] = Lock(); }); }); };
        }
    }
    void editStep (std::function<void (Pattern&, int)> fn)
    {
        auto* t = ctx.selected(); if (! t) return;
        const int i = selectedStep;
        ctx.s.editPattern (*t, t->pattern.load(), [&] (Pattern& p) { fn (p, i); });
        repaint();
    }
    void tool (std::function<void (Pattern&)> fn) { auto* t = ctx.selected(); if (! t) return; ctx.s.editPattern (*t, t->pattern.load(), fn); refresh(); }
    // ---- chopping ----------------------------------------------------------------------------------------------------
    void slicesMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        juce::PopupMenu m;
        m.addItem (1, "in order (the steps that play: slice 1, 2, 3 ...)"); m.addItem (2, "reversed"); m.addItem (3, "shuffled (the same slices, another order)");
        m.addItem (4, "wandering (each a slice or two from the last)"); m.addItem (5, "any slice, a new one each time (random)"); m.addItem (6, "back to notes (no slices)");
        const bool sampler = t->instrument && t->instrument->paramIndexForId ("start") >= 0;
        m.addSeparator(); m.addItem (7, "start jitter: the sampler's start a little different on every step (locks)", sampler); m.addItem (8, "no start locks", sampler);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t] (int r)
        {
            if (r <= 0) return;
            const int nSl = [t] { if (t->instrument) if (auto* h = dynamic_cast<SampleHolder*> (t->instrument->processor())) return std::max (1, h->numSlices()); return 16; }();
            if (r <= 6) tool ([this, r, nSl] (Pattern& p) { tools::slices (p, r, nSl, rng); });
            else
            {
                const int dev = t->instrument->uid, prm = t->instrument->paramIndexForId ("start");
                tool ([this, r, dev, prm] (Pattern& p)
                {
                    for (int k = 0; k < juce::jlimit (1, kMaxSteps, p.length); ++k)
                    {
                        auto& st = p.s[(size_t) k];
                        for (auto& l : st.locks) if (l.device == dev && l.param == prm) l.param = -1;
                        if (r == 7 && st.on) for (auto& l : st.locks) if (l.param < 0) { l.device = dev; l.param = prm; l.value = rng.uni() * 0.35f; break; }
                    }
                });
            }
        });
    }
    // ---- grooves -----------------------------------------------------------------------------------------------------
    std::vector<GroovePtr> grooveList;
    void grooveMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        auto& s = ctx.s;
        juce::PopupMenu m, built, saved, amt;
        grooveList = Session::builtinGrooves(); const int nb = (int) grooveList.size();
        for (int k = 0; k < nb; ++k) built.addItem (100 + k, grooveList[(size_t) k]->name, true, t->groove && t->groove->name == grooveList[(size_t) k]->name);
        for (auto& g : s.savedGrooves()) grooveList.push_back (g);
        for (int k = nb; k < (int) grooveList.size(); ++k) saved.addItem (100 + k, grooveList[(size_t) k]->name, true, t->groove && t->groove->name == grooveList[(size_t) k]->name);
        m.addItem (1, "none", true, t->groove == nullptr);
        m.addSubMenu ("built in", built); m.addSubMenu ("saved", saved, (int) grooveList.size() > nb);
        m.addItem (2, "from the clip under the playhead (this track: an audio loop's hits, a note clip's notes)");
        m.addItem (3, "from this pattern (its micro timing and accents)");
        for (int a : { 25, 50, 75, 100, 125, 150 }) amt.addItem (300 + a, juce::String (a) + "%", true, std::abs (t->grooveAmount.load() * 100.0f - (float) a) < 0.5f);
        m.addSubMenu ("amount", amt, t->groove != nullptr);
        m.addItem (4, "write it into this pattern's steps", t->groove != nullptr); m.addItem (5, "save this groove...", t->groove != nullptr);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t] (int r)
        {
            auto& s = ctx.s; juce::String err;
            if (r == 1) s.setGroove (*t, nullptr, 1.0f);
            else if (r >= 100 && r < 300 && r - 100 < (int) grooveList.size()) s.setGroove (*t, grooveList[(size_t) (r - 100)], t->grooveAmount.load());
            else if (r == 2)
            {
                const double b = s.currentBar(); ClipRef found;
                for (auto& c : *t->aclips) if (b >= c.startBar && b < c.startBar + c.bars) found = { t->uid, c.id, true };
                for (auto& c : *t->clips) if (c.notes && b >= c.startBar && b < c.startBar + c.bars) found = { t->uid, c.id, false };
                if (found.track == 0) { ctx.status ("groove: no audio or note clip on " + t->name + " under the playhead"); return; }
                if (auto g = s.grooveFromClip (found, err)) { s.setGroove (*t, g, 1.0f); ctx.status ("groove taken from the clip: " + juce::String (g->steps) + " sixteenths"); } else ctx.status ("groove: " + err);
            }
            else if (r == 3) { if (auto g = s.grooveFromPattern (*t, t->pattern.load())) s.setGroove (*t, g, 1.0f); }
            else if (r >= 300) t->grooveAmount.store ((r - 300) / 100.0f), s.dirty = true;
            else if (r == 4) { s.commitGroove (*t, t->pattern.load()); ctx.status ("the groove is in the steps now (micro timing and velocity)"); }
            else if (r == 5) ask ("save the groove", "a name", t->groove->name, [this, t] (juce::String n) { Groove g = *t->groove; g.name = n.trim(); juce::String e; if (ctx.s.saveGroove (g, e)) ctx.status ("groove saved: " + g.name); else ctx.status (e); });
            refresh();
        });
    }
    // ---- follow actions ----------------------------------------------------------------------------------------------
    void followMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        const int pi = t->pattern.load(); const auto fa = (*t->follow)[(size_t) pi];
        juce::PopupMenu m, after, a, b, ch;
        for (int n : { 0, 1, 2, 3, 4, 6, 8, 12, 16, 32 }) after.addItem (100 + n, n == 0 ? juce::String ("off") : juce::String (n) + (n == 1 ? " time" : " times"), true, fa.after == n);
        for (int k = 0; k < FOLLOW_COUNT; ++k) { a.addItem (200 + k, followName (k), true, fa.a == k); b.addItem (300 + k, followName (k), true, fa.b == k); }
        for (int c : { 100, 90, 75, 66, 50, 33, 25, 10 }) ch.addItem (400 + c, juce::String (c) + "% the first, " + juce::String (100 - c) + "% the second", true, std::abs (fa.chanceA * 100.0f - (float) c) < 0.5f);
        m.addSectionHeader ("pattern " + juce::String (pi + 1) + ": " + (fa.after > 0 ? "after " + juce::String (fa.after) + ", " + followName (fa.a) + (fa.chanceA < 0.999f ? " / " + juce::String (followName (fa.b)) : juce::String()) : juce::String ("no follow action")));
        m.addSubMenu ("after it has played", after); m.addSubMenu ("then", a); m.addSubMenu ("or else", b); m.addSubMenu ("chance", ch);
        m.addItem (1, "the same for every pattern of this track");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, pi, fa] (int r)
        {
            auto f = fa;
            if (r >= 100 && r < 200) f.after = r - 100; else if (r >= 200 && r < 300) f.a = r - 200; else if (r >= 300 && r < 400) f.b = r - 300; else if (r >= 400) f.chanceA = (r - 400) / 100.0f;
            else if (r == 1) { for (int k = 0; k < kPatterns; ++k) ctx.s.setFollow (*t, k, fa); ctx.status ("every pattern of " + t->name + ": the same follow action"); return; }
            else return;
            if (f.after == 0 && r >= 200) f.after = 1;
            ctx.s.setFollow (*t, pi, f); refresh();
        });
    }
    // ---- midi effects ------------------------------------------------------------------------------------------------
    void midiFxMenu()
    {
        auto* t = ctx.selected(); if (! t || t->isAudio()) { ctx.status ("midi effects go on instrument tracks"); return; }
        const auto fx = *t->midiFx;
        juce::PopupMenu m;
        for (int i = 0; i < (int) fx.size(); ++i)
        {
            juce::PopupMenu sub; sub.addItem (1000 + i * 10, "edit..."); sub.addItem (1001 + i * 10, fx[(size_t) i].on ? "off" : "on"); sub.addItem (1002 + i * 10, "remove");
            if (i > 0) sub.addItem (1003 + i * 10, "earlier");
            m.addSubMenu (juce::String (i + 1) + ". " + midiFxName (fx[(size_t) i].kind) + (fx[(size_t) i].on ? "" : " (off)"), sub);
        }
        if (! fx.empty()) m.addSeparator();
        for (int k = 0; k < MFX_COUNT; ++k) m.addItem (10 + k, "+ " + juce::String (midiFxName (k)), (int) fx.size() < kMidiFx);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, t, fx] (int r)
        {
            auto v = fx;
            if (r >= 10 && r < 10 + MFX_COUNT)
            {
                MidiFx f; f.kind = r - 10;
                if (f.kind == MFX_GEN) { f.div = 4; f.density = 0.35f; f.gate = 0.8f; f.vel = 0.75f; f.spread = 0.4f; f.range = 12; }
                if (f.kind == MFX_CHANCE) { f.vel = 0.3f; f.spread = 0.0f; }
                f.seed = juce::Random::getSystemRandom().nextInt (100000);
                v.push_back (f); ctx.s.setMidiFx (*t, v); editMidiFx (*t, (int) v.size() - 1); return;
            }
            if (r < 1000) return;
            const int i = (r - 1000) / 10, a = (r - 1000) % 10; if (i >= (int) v.size()) return;
            if (a == 0) { editMidiFx (*t, i); return; }
            if (a == 1) v[(size_t) i].on = ! v[(size_t) i].on;
            if (a == 2) v.erase (v.begin() + i);
            if (a == 3 && i > 0) std::swap (v[(size_t) i], v[(size_t) i - 1]);
            ctx.s.setMidiFx (*t, v); refresh();
        });
    }
    void editMidiFx (Track& t, int index)
    {
        struct Ed : juce::Component
        {
            Ed (Session& s, Track& tr, int i) : session (s), track (tr), idx (i)
            {
                const auto f = (*tr.midiFx)[(size_t) i];
                const auto divs = divisionNames();
                if (f.kind == MFX_ARP)
                {
                    add ("div", new ValueRow ("rate", divs, f.div)); add ("mode", new ValueRow ("mode", { "up", "down", "up and down", "random", "as played" }, f.mode));
                    add ("oct", new ValueRow ("octaves", 1.0f, 4.0f, (float) f.octaves, [] (float v) { return juce::String (juce::roundToInt (v)); }));
                    add ("gate", new ValueRow ("gate", 0.05f, 1.0f, f.gate, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                    add ("vel", new ValueRow ("velocity", 0.05f, 1.0f, f.vel, [] (float v) { return juce::String (juce::roundToInt (v * 127)); }));
                }
                else if (f.kind == MFX_CHORD)
                {
                    for (int k = 0; k < 4; ++k) add ("iv" + juce::String (k), new ValueRow ("note " + juce::String (k + 2), -24.0f, 24.0f, (float) (k < f.nIntervals ? f.intervals[(size_t) k] : 0), [] (float v) { const int n = juce::roundToInt (v); return n == 0 ? juce::String ("-") : (n > 0 ? "+" : "") + juce::String (n) + " st"; }));
                    add ("vel", new ValueRow ("their velocity", 0.05f, 1.0f, f.vel, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                }
                else if (f.kind == MFX_GEN)
                {
                    add ("div", new ValueRow ("rate", divs, f.div)); add ("density", new ValueRow ("density", 0.0f, 1.0f, f.density, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                    add ("scale", new ValueRow ("scale", { "chromatic", "major", "minor", "pentatonic", "dorian", "phrygian", "the track's scale" }, f.scale));
                    add ("range", new ValueRow ("range", 0.0f, 36.0f, (float) f.range, [] (float v) { return juce::String (juce::roundToInt (v)) + " st"; }));
                    add ("root", new ValueRow ("root", -24.0f, 24.0f, (float) f.root, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }));
                    add ("gate", new ValueRow ("length", 0.05f, 4.0f, f.gate, [] (float v) { return juce::String (v, 2) + " x rate"; }));
                    add ("vel", new ValueRow ("velocity", 0.05f, 1.0f, f.vel, [] (float v) { return juce::String (juce::roundToInt (v * 127)); }));
                    add ("spread", new ValueRow ("velocity spread", 0.0f, 1.0f, f.spread, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                }
                else
                {
                    add ("chance", new ValueRow ("notes through", 0.0f, 1.0f, f.chance, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                    add ("vel", new ValueRow ("velocity spread", 0.0f, 1.0f, f.vel, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                    add ("spread", new ValueRow ("octave jumps", 0.0f, 1.0f, f.spread, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
                }
                for (auto& [k, r] : rows) r->onChange = [this] (float) { push(); };
                setSize (300, 26 + (int) rows.size() * 20 + 6);
            }
            void add (const juce::String& key, ValueRow* r) { rows.push_back ({ key, r }); owned.add (r); addAndMakeVisible (r); }
            float v (const juce::String& k) const { for (auto& [kk, r] : rows) if (kk == k) return r->get(); return 0; }
            void push()
            {
                auto all = *track.midiFx; if (idx >= (int) all.size()) return;
                auto& f = all[(size_t) idx];
                if (f.kind == MFX_ARP) { f.div = (int) v ("div"); f.mode = (int) v ("mode"); f.octaves = juce::roundToInt (v ("oct")); f.gate = v ("gate"); f.vel = v ("vel"); }
                else if (f.kind == MFX_CHORD) { int n = 0; for (int k = 0; k < 4; ++k) { const int iv = juce::roundToInt (v ("iv" + juce::String (k))); if (iv != 0) f.intervals[(size_t) n++] = iv; } f.nIntervals = n; f.vel = v ("vel"); }
                else if (f.kind == MFX_GEN) { f.div = (int) v ("div"); f.density = v ("density"); f.scale = (int) v ("scale"); f.range = juce::roundToInt (v ("range")); f.root = juce::roundToInt (v ("root")); f.gate = v ("gate"); f.vel = v ("vel"); f.spread = v ("spread"); }
                else { f.chance = v ("chance"); f.vel = v ("vel"); f.spread = v ("spread"); }
                session.setMidiFx (track, all);
            }
            void paint (juce::Graphics& g) override { g.fillAll (studioTheme().ground); g.setColour (studioTheme().ink); g.setFont (uiFont (11.0f, true)); g.drawText (midiFxName ((*track.midiFx)[(size_t) std::min (idx, (int) track.midiFx->size() - 1)].kind), 8, 4, getWidth() - 16, 16, juce::Justification::centredLeft); }
            void resized() override { int y = 24; for (auto& [k, r] : rows) { r->setBounds (8, y, getWidth() - 16, 18); y += 20; } }
            Session& session; Track& track; int idx;
            std::vector<std::pair<juce::String, ValueRow*>> rows; juce::OwnedArray<ValueRow> owned;
        };
        if (index < 0 || index >= (int) t.midiFx->size()) return;
        juce::CallOutBox::launchAsynchronously (std::make_unique<Ed> (ctx.s, t, index), tMidiFx->getScreenBounds(), nullptr);
    }
    /** additive / subtractive: the figures of the cell (the pattern shown) into the patterns after it, as a chain */
    void processMenu()
    {
        juce::PopupMenu m; m.addSectionHeader ("the pattern shown is the cell; its figures go into the next patterns");
        m.addItem (1, "additive: 1, 1 2, 1 2 3 ..."); m.addItem (2, "subtractive: the whole cell, then one note fewer each time"); m.addItem (3, "both: up, then down again");
        juce::PopupMenu rep; for (int r : { 1, 2, 3, 4, 8 }) rep.addItem (10 + r, "each figure " + juce::String (r) + (r == 1 ? " time" : " times"), true, repeats == r);
        m.addSubMenu ("repeats", rep);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tAdditive), [this] (int r)
        {
            if (r > 10) { repeats = r - 10; return; }
            if (r < 1 || r > 3) return;
            auto* t = ctx.selected(); if (! t) return;
            const int made = applyProcess (ctx.s, *t, r - 1, repeats);
            ctx.status (made > 0 ? juce::String (made) + " figures in patterns " + juce::String (t->pattern.load() + 2) + " - " + juce::String (t->pattern.load() + 1 + made) + ", chained" : juce::String ("additive: the pattern has no notes (or no room for the figures)"));
            refresh();
        });
    }
    /** (the page's additive / subtractive, as a function the tests use too) */
    static int applyProcess (Session& s, Track& t, int kind, int repeats)
    {
        const int cur = t.pattern.load();
        auto cell = t.patternAt (cur);
        if (! cell) return 0;
        const auto figs = tools::process (*cell, kind);
        if (figs.empty() || cur + (int) figs.size() >= kPatterns) return 0;
        std::vector<int> chain;
        for (size_t k = 0; k < figs.size(); ++k)
        {
            s.setPattern (t, cur + 1 + (int) k, std::make_shared<const Pattern> (figs[k]));
            for (int r = 0; r < std::max (1, repeats); ++r) chain.push_back (cur + 1 + (int) k);
        }
        s.setChain (t, chain); t.chainOn.store (true);
        s.launch (t, cur + 1);
        return (int) figs.size();
    }
    void typeDialog()
    {
        auto* t = ctx.selected(); if (! t) return;
        const int n = t->scale ? t->scale->size() : 12;
        ask ("type a pattern", "one word per step: 0 2 4 numbers are notes (" + juce::String (t->scale ? "degrees of " + t->scale->name : juce::String ("semitones")) + "), x the root, . rest, _ hold; ! accent, ~ soft, ? half chance, *3 ratchet, ' , octave", lastTyped, [this, n] (juce::String text)
        {
            juce::String err; Pattern p;
            if (! tools::typed (p, text, n, &err)) { ctx.status ("type: " + err); return; }
            lastTyped = text;
            tool ([p] (Pattern& x) { x = p; });
        });
    }
    int repeats = 2; juce::String lastTyped { "0 . 2 _ 4! . 7? . 0' . 4~ 2*3 . . ." };
    void euclid()
    {
        auto* w = new juce::AlertWindow ("euclid", "hits over the pattern length, rotated", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("hits", "5"); w->addTextEditor ("rot", "0"); w->addButton ("ok", 1); w->addButton ("cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            const int hits = w->getTextEditorContents ("hits").getIntValue(), rot = w->getTextEditorContents ("rot").getIntValue();
            tool ([hits, rot] (Pattern& p) { tools::euclid (p, hits, rot); });
        }), true);
    }
    void chainEditor()
    {
        auto* t = ctx.selected(); if (! t) return;
        juce::StringArray cur; for (int p : *t->chain) cur.add (juce::String (p + 1));
        auto* w = new juce::AlertWindow ("pattern chain", "patterns in order, e.g. 1 1 2 3", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("chain", cur.joinIntoString (" ")); w->addButton ("ok", 1); w->addButton ("cancel", 0);
        Track* tp = t;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, tp] (int r) { if (r != 1) return; std::vector<int> c; for (auto& s : juce::StringArray::fromTokens (w->getTextEditorContents ("chain"), " ,", "")) if (s.isNotEmpty()) c.push_back (juce::jlimit (0, kPatterns - 1, s.getIntValue() - 1)); ctx.s.setChain (*tp, c); }), true);
    }
    void lockMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        juce::PopupMenu m; int id = 1; std::vector<std::pair<int, int>> list;
        auto addDev = [&] (Device& d, const juce::String& name) { juce::PopupMenu sub; for (int i = 0; i < std::min (d.numParams(), 120); ++i) { sub.addItem (id++, d.paramName (i)); list.push_back ({ d.uid, i }); } m.addSubMenu (name, sub); };
        if (t->isMaster() || t->isReturn() || t->isAudio()) { ctx.status ("locks belong to the steps of an instrument track"); return; }
        addDev (*t->mixer, "strip"); if (t->instrument) addDev (*t->instrument, t->instrument->name); for (auto& d : t->inserts) addDev (*d, d->name);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, list] (int r)
        {
            if (r <= 0 || r > (int) list.size()) return;
            auto* t = ctx.selected(); if (! t) return;
            auto* d = t->device (list[(size_t) r - 1].first); if (! d) return;
            const float v = d->baseValue (list[(size_t) r - 1].second);
            const auto pr = list[(size_t) r - 1];
            editStep ([pr, v] (Pattern& p, int i) { auto& st = p.s[(size_t) i]; st.on = true; for (auto& l : st.locks) if (l.param < 0 || (l.device == pr.first && l.param == pr.second)) { l = { pr.first, pr.second, v }; return; } st.locks[kLocks - 1] = { pr.first, pr.second, v }; });
            refresh();
        });
    }
    void addDeviceMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        juce::PopupMenu m; auto types = ctx.s.deviceTypes();
        juce::PopupMenu inst, fx, st, plug;
        for (int i = 0; i < (int) types.size(); ++i)
        {
            auto& e = types[(size_t) i];
            if (e.typeId.startsWith ("fal:")) (e.instrument ? inst : fx).addItem (i + 1, e.name);
            else if (e.typeId.startsWith ("st:")) st.addItem (i + 1, e.name + (e.instrument ? " (instrument)" : ""));
            else plug.addItem (i + 1, e.name);
        }
        m.addSubMenu (kDeep ? "faltung deep instruments" : "faltung instruments", inst); m.addSubMenu ("faltung effects", fx); m.addSubMenu ("studio devices", st); m.addSubMenu ("plug-ins", plug);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tAddDevice), [this, types] (int r)
        {
            if (r <= 0 || r > (int) types.size()) return;
            auto* t = ctx.selected(); if (! t) return;
            juce::String err;
            auto d = ctx.s.createDevice (types[(size_t) r - 1].typeId, &err);
            if (! d) { ctx.status (err); return; }
            if (d->isInstrument() && ! t->isAudio()) ctx.s.setInstrument (*t, std::move (d)); else ctx.s.addInsert (*t, std::move (d));
            rebuild();
        });
    }
    void rebuild() override
    {
        panels.clear(); chain.removeAllChildren();
        auto* t = ctx.selected();
        if (! t) { refresh(); return; }
        auto add = [&] (Device& d)
        {
            auto* p = panels.add (new DevicePanel (ctx.s, *t, d, ctx.openWindow, [this] (Device& dd) { if (auto* tt = ctx.selected()) ctx.removeDevice (*tt, dd); rebuild(); }, [this] (int) { ctx.showPage (1); }));
            chain.addAndMakeVisible (p);
        };
        if (t->instrument) add (*t->instrument);
        for (auto& d : t->inserts) add (*d);
        resized();
        refresh();
    }
    void refresh()
    {
        auto* t = ctx.selected();
        if (! t) return;
        rPattern->set ((float) t->pattern.load());
        auto p = t->patternAt (t->pattern.load());
        rLength->set (p ? (float) p->length : 16.0f);
        rSpeed->set ((float) t->speed.load()); rSwing->set (t->swingOffset.load()); rShift->set (t->shiftMs.load()); rProb->set (t->probMul.load()); rRoot->set ((float) t->rootNote.load());
        rFree->set (t->freeClock.load() ? 1.0f : 0.0f); rStepMs->set (t->stepMs.load()); rChain->set (t->chainOn.load() ? 1.0f : 0.0f);
        rEvolve->set (t->evolve.load()); rChoke->set ((float) t->chokeGroup.load());
        rPhase->set ((t->phaseRate.load() - 1.0f) * 100.0f);
        { int si = 0; if (t->scale) { si = builtinScaleNames().indexOf (t->scale->name) + 1; if (si <= 0) si = builtinScaleNames().size() + 1; } rScale->set ((float) si); rScale->setTooltip (t->scale ? "scale: " + t->scale->name + " (" + juce::String (t->scale->size()) + " degrees, period " + juce::String (t->scale->period, 1) + " cents)" : juce::String ("steps in semitones")); }
        const int q = ctx.s.launchQuant.load(); rQuant->set (q == 0 ? 0.0f : q == 1 ? 1.0f : q == 2 ? 2.0f : 3.0f);
        const Step st = p ? p->s[(size_t) juce::jlimit (0, kMaxSteps - 1, selectedStep)] : Step();
        sVel->set (st.vel); sPitch->set ((float) st.pitch); sProb->set (st.prob); sRatchet->set ((float) st.ratchet); sMicro->set (st.micro); sLen->set (st.len); sDur->set (st.dur); sCond->set ((float) st.cond); sSlide->set (st.slide ? 1.0f : 0.0f); sSlice->set ((float) st.slice); sChord->set ((float) st.chord); sRCurve->set (st.rcurve);
        for (int k = 0; k < kLocks; ++k)
        {
            const auto& l = st.locks[(size_t) k];
            auto* d = l.param >= 0 ? t->device (l.device) : nullptr;
            sLock[k]->name = d ? (d->uid == 0 ? juce::String ("strip ") : d->name + " ") + d->paramName (l.param) : "lock " + juce::String (k + 1) + " (none)";
            sLock[k]->set (d ? l.value : 0.0f);
        }
        repaint();
    }
    void resized() override
    {
        const int w = getWidth();
        // left: track settings; middle: the grid + step; bottom: the chain
        int y = 30;
        for (auto* r : { rPattern, rLength, rSpeed, rSwing, rShift, rProb, rRoot, rFree, rStepMs, rChain, rQuant, rEvolve, rChoke, rScale, rPhase }) { r->setBounds (8, y, 210, 18); y += 20; }
        const int gx = 236, gw = w - gx - 8;
        int x = gx;
        for (auto* l : { tEuclid, tRotL, tRotR, tRev, tMutate, tHuman, tRandom, tMarkov, tRules, tDouble, tClear, tCopy, tPaste }) { const int lw = (int) juce::GlyphArrangement::getStringWidth (uiFont (11.0f), l->text) + 16; l->setBounds (x, 196, lw, 18); x += lw + 2; }
        x = gx;
        for (auto* l : { tRatios, tAccel, tEven, tStrict, tSieve, tSievePitch, tCloud, tFreeze, tAdditive, tType, tSlices, tGroove, tFollow, tMidiFx, tTrackRules }) { const int lw = (int) juce::GlyphArrangement::getStringWidth (uiFont (11.0f), l->text) + 16; l->setBounds (x, 214, lw, 18); x += lw + 2; }
        tAddLock->setBounds (gx + 540, 240, 60, 18);
        int sy = 240;
        ValueRow* steps[] = { sVel, sPitch, sProb, sRatchet, sMicro, sRCurve, sLen, sDur, sCond, sSlice, sChord, sSlide };
        for (int i = 0; i < 12; ++i) steps[i]->setBounds (gx + (i % 2) * 230, sy + (i / 2) * 20, 220, 18);
        for (int k = 0; k < kLocks; ++k) sLock[k]->setBounds (gx + 470, 262 + k * 20, gw - 470, 18);
        tAddDevice->setBounds (8, 366, 80, 18);
        viewport.setBounds (8, 386, w - 16, getHeight() - 394);
        int cy = 0; const int cw = viewport.getWidth() - 12;
        for (auto* p : panels) { const int h = p->wanted (cw); p->setBounds (0, cy, cw, h); cy += h + 6; }
        chain.setSize (cw, std::max (cy, 10));
    }
    juce::Rectangle<float> cell (int i) const { const int gx = 236; const float cw = (float) (getWidth() - gx - 28) / 16.0f; return { gx + (i % 16) * cw + 2.0f, 60.0f + (i / 16) * 0.0f, cw - 4.0f, 68.0f }; }
    // ---- the pitch lane: every step's pitch as a degree of the track's scale; drag across it to draw a contour ----
    static constexpr float laneTop = 134.0f, laneBottom = 178.0f;
    void laneRange (const Track* t, int& lo, int& hi) const { const int n = t && t->scale ? t->scale->size() : 12; lo = -n; hi = 2 * n; }
    float laneY (const Track* t, int deg) const { int lo, hi; laneRange (t, lo, hi); const float h = (laneBottom - laneTop) / (float) (hi - lo + 1); return laneBottom - ((float) (juce::jlimit (lo, hi, deg) - lo) + 0.5f) * h; }
    int laneDegree (const Track* t, float y) const { int lo, hi; laneRange (t, lo, hi); const float h = (laneBottom - laneTop) / (float) (hi - lo + 1); return juce::jlimit (lo, hi, lo + (int) std::floor ((laneBottom - y) / h)); }
    juce::String pitchText (int p) const
    {
        auto* t = ctx.selected();
        if (t && t->scale) { const double c = t->scale->centsOf (p); return "degree " + juce::String (p) + " (" + (c >= 0 ? "+" : "") + juce::String (c, 0) + " c)"; }
        return (p > 0 ? "+" : "") + juce::String (p) + " st";
    }
    void setLanePitch (int step, int deg) { editStep ([step, deg] (Pattern& p, int) { auto& s = p.s[(size_t) step]; if (! s.on) { s.on = true; s.vel = 0.8f; s.prob = 1.0f; s.ratchet = 1; s.micro = 0; } s.pitch = deg; s.slice = -1; }); }
    void paint (juce::Graphics& g) override
    {
        auto* t = ctx.selected();
        g.setFont (uiFont (11.0f, true)); g.setColour (theme.ink);
        g.drawText (t ? t->name + ui::U ("  ·  ") + (t->isAudio() ? "audio track" : (t->instrument ? t->instrument->name : "no instrument")) : "no track selected", 8, 6, 400, 18, juce::Justification::centredLeft, true);
        g.setFont (uiFont (11.0f, true)); g.drawText ("step " + juce::String (selectedStep + 1), 236 + 470, 240, 100, 16, juce::Justification::centredLeft);
        g.drawText ("devices", 90, 366, 200, 18, juce::Justification::centredLeft);
        if (! t) return;
        auto p = t->patternAt (t->pattern.load());
        const int len = p ? p->length : 16;
        const int pages = (len + 15) / 16;
        page = juce::jlimit (0, pages - 1, page);
        const int playing = t->playStep.load();
        const juce::Colour col = trackColour (*t);
        // page tabs
        for (int k = 0; k < pages; ++k) { g.setColour (k == page ? theme.ink : theme.ink.withAlpha (0.4f)); g.drawText ("page " + juce::String (k + 1), 236 + k * 60, 34, 56, 16, juce::Justification::centredLeft); }
        g.setFont (uiFont (9.0f));
        for (int k = 0; k < 16; ++k)
        {
            const int i = page * 16 + k;
            auto c = cell (k);
            g.setColour (theme.ink.withAlpha (0.75f)); g.drawText (juce::String (i + 1), c.withHeight (12.0f).translated (0, -14), juce::Justification::centredLeft);
            if (i >= len) { g.setColour (theme.ink.withAlpha (0.12f)); g.strokePath (ui::skewRect (c, 0.12f), juce::PathStrokeType (1.0f)); continue; }
            const Step st = p ? p->s[(size_t) i] : Step();
            if (st.on)
            {
                auto r = c.translated (st.micro * c.getWidth() * 0.25f, 0.0f);
                const float a = 0.35f + 0.65f * st.vel;
                auto shape = ui::skewRect (r, 0.12f);
                g.setColour (col.withAlpha (a));
                if (st.prob < 0.999f) { g.strokePath (shape, juce::PathStrokeType (1.2f)); juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.withTrimmedTop (r.getHeight() * (1.0f - st.prob)).expanded (8, 0).toNearestInt()); g.fillPath (shape); }
                else g.fillPath (shape);
                g.setColour (ui::inkFor (col).withAlpha (0.9f)); g.setFont (uiFont (9.0f));
                juce::String info;
                if (st.pitch != 0) info << (st.pitch > 0 ? "+" : "") << st.pitch << "\n";
                if (st.ratchet > 1) info << "x" << st.ratchet << "\n";
                if (st.cond != Cond::Always) info << condName (st.cond) << "\n";
                if (st.slide) info << "slide\n";
                if (st.slice >= 0) info << "s" << (st.slice + 1) << "\n"; else if (st.slice == -2) info << "s?\n";
                if (st.chord > 0) info << chordName (st.chord) << "\n";
                if (std::abs (st.dur - 1.0f) > 1.0e-4f) info << "t x" << juce::String (st.dur, 2) << "\n";
                int locks = 0; for (auto& l : st.locks) if (l.param >= 0) ++locks;
                if (locks) info << locks << " lock" << (locks > 1 ? "s" : "") << "\n";
                g.drawFittedText (info.trim(), r.withTrimmedLeft (17.0f).withTrimmedRight (2.0f).reduced (0.0f, 6.0f).toNearestInt(), juce::Justification::topLeft, 7, 1.0f);
                if (st.ratchet > 1) { g.setColour (theme.ink.withAlpha (0.8f)); for (int q = 1; q < st.ratchet; ++q) g.fillRect (r.getX() + r.getWidth() * q / st.ratchet, r.getBottom() - 10.0f, 1.0f, 8.0f); }
            }
            else
            {
                g.setColour (theme.ink.withAlpha (0.22f)); g.strokePath (ui::skewRect (c, 0.12f), juce::PathStrokeType (1.0f));
                if (std::abs (st.dur - 1.0f) > 1.0e-4f) { g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (uiFont (9.0f)); g.drawText ("t x" + juce::String (st.dur, 2), c.reduced (4.0f, 6.0f), juce::Justification::topLeft); }
            }
            if (i == selectedStep) { g.setColour (brightColour()); g.strokePath (ui::skewRect (c.expanded (1.0f), 0.12f), juce::PathStrokeType (1.5f)); }
            if (i == playing) { g.setColour (brightColour()); g.fillRect (c.getX(), c.getBottom() + 2.0f, c.getWidth(), 2.0f); }
        }
        // the pitch lane
        {
            int lo, hi; laneRange (t, lo, hi); const int n = t->scale ? t->scale->size() : 12;
            const float x0 = cell (0).getX(), x1 = cell (15).getRight();
            g.setColour (theme.ink.withAlpha (0.05f)); g.fillRect (x0, laneTop, x1 - x0, laneBottom - laneTop);
            for (int d = lo; d <= hi; d += n) { g.setColour (theme.ink.withAlpha (d == 0 ? 0.45f : 0.2f)); g.fillRect (x0, laneY (t, d), x1 - x0, 1.0f); }
            g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (uiFont (8.5f));
            g.drawText ("pitch lane: " + (t->scale ? t->scale->name : juce::String ("semitones")), (int) x1 - 264, (int) laneBottom - 11, 260, 11, juce::Justification::centredRight, true);
            for (int k = 0; k < 16; ++k)
            {
                const int i = page * 16 + k; if (i >= len || ! p) continue;
                const Step& st = p->s[(size_t) i]; if (! st.on || st.slice >= 0) continue;
                const auto c = cell (k);
                g.setColour (col.withAlpha (0.4f + 0.6f * st.vel)); g.fillRect (c.getX() + 2.0f, laneY (t, st.pitch) - 1.5f, c.getWidth() - 4.0f, 3.0f);
                if (i == selectedStep) { g.setColour (theme.ink); g.drawText (juce::String (st.pitch), c.withY (laneTop).withHeight (11.0f).toNearestInt(), juce::Justification::centredRight); }
            }
        }
        g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (uiFont (9.5f));
        g.drawText (ui::U ("pitch lane: click or drag across it to set the steps' degrees · click: on / off and select · drag up / down: velocity · drag across: paint · shift-drag: micro · alt-click: chance · right-click: ratchet · right-click a slider below: map an lfo / random / gate, automate, midi, key, macro"), 236, 182, getWidth() - 244, 14, juce::Justification::centredLeft, true);
        // the evolving copy: steps that have drifted from what is written carry a mark
        if (t->evolve.load() > 0.0005f)
            for (int k = 0; k < 16; ++k) { const int i = page * 16 + k; if (i < len && (t->flip[(size_t) i] != 0 || t->drift[(size_t) i] != 0.0f)) { g.setColour (hotColour()); g.fillRect (cell (k).getX() + 14.0f, 58.0f - 6.0f, 10.0f, 3.0f); } }
    }
    int stepAt (juce::Point<float> p) const { for (int k = 0; k < 16; ++k) if (cell (k).expanded (2.0f).contains (p)) return page * 16 + k; return -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selected(); if (! t) return;
        if (e.y >= 34 && e.y < 50 && e.x >= 236) { const int k = (e.x - 236) / 60; auto p = t->patternAt (t->pattern.load()); const int pages = ((p ? p->length : 16) + 15) / 16; if (k >= 0 && k < pages) { page = k; repaint(); } return; }
        if (e.position.y >= laneTop && e.position.y <= laneBottom)
        {
            const int i = stepAt ({ e.position.x, cell (0).getCentreY() });
            if (i < 0) return;
            selectedStep = i; mode = Lane; setLanePitch (i, laneDegree (t, e.position.y)); refresh(); return;
        }
        const int i = stepAt (e.position);
        if (i < 0) return;
        mode = None; downStep = i;
        auto p = t->patternAt (t->pattern.load());
        const Step st = p ? p->s[(size_t) i] : Step();
        startVel = st.vel; startMicro = st.micro;
        selectedStep = i;
        if (e.mods.isPopupMenu()) { editStep ([] (Pattern& pp, int k) { auto& s = pp.s[(size_t) k]; if (! s.on) s.on = true; s.ratchet = s.ratchet % 4 + 1; }); refresh(); return; }
        if (e.mods.isAltDown()) { editStep ([] (Pattern& pp, int k) { auto& s = pp.s[(size_t) k]; s.on = true; s.prob = s.prob > 0.99f ? 0.75f : s.prob > 0.7f ? 0.5f : s.prob > 0.45f ? 0.25f : 1.0f; }); refresh(); return; }
        if (e.mods.isShiftDown()) { editStep ([] (Pattern& pp, int k) { pp.s[(size_t) k].on = true; }); mode = Micro; refresh(); return; }
        if (! st.on) { editStep ([] (Pattern& pp, int k) { auto& s = pp.s[(size_t) k]; s.on = true; s.vel = 0.8f; s.prob = 1.0f; s.ratchet = 1; s.micro = 0; }); paintOn = true; turnedOn = true; }
        else { paintOn = false; turnedOn = false; }
        mode = Pending;
        refresh();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == None) return;
        const int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
        if (mode == Pending) { if (std::abs (dy) > 4 && std::abs (dy) > std::abs (dx)) mode = Vertical; else if (std::abs (dx) > 6) mode = Paint; }
        if (mode == Lane)
        {
            auto* t = ctx.selected(); const int i = stepAt ({ e.position.x, cell (0).getCentreY() });
            if (t && i >= 0) { auto p = t->patternAt (t->pattern.load()); if (i < (p ? p->length : 16)) setLanePitch (i, laneDegree (t, e.position.y)); }
            refresh(); return;
        }
        if (mode == Vertical) { const float v = juce::jlimit (0.05f, 1.0f, startVel - dy / 80.0f); const int ds = downStep; editStep ([v, ds] (Pattern& p, int) { p.s[(size_t) ds].on = true; p.s[(size_t) ds].vel = v; }); }
        else if (mode == Micro) { const float v = juce::jlimit (-1.0f, 1.0f, startMicro + dx / (cell (0).getWidth() * 1.4f)); const int ds = downStep; editStep ([v, ds] (Pattern& p, int) { p.s[(size_t) ds].micro = v; }); }
        else if (mode == Paint) { const int i = stepAt ({ e.position.x, cell (0).getCentreY() }); if (i >= 0) { const bool on = paintOn; editStep ([i, on] (Pattern& p, int) { auto& s = p.s[(size_t) i]; if (s.on != on) { s.on = on; if (on) { s.vel = 0.8f; s.prob = 1; s.ratchet = 1; s.micro = 0; } } }); } }
        refresh();
    }
    void mouseUp (const juce::MouseEvent&) override { if (mode == Pending && ! turnedOn) { const int ds = downStep; editStep ([ds] (Pattern& p, int) { p.s[(size_t) ds].on = false; }); } mode = None; refresh(); }
    void tick() override { for (auto* p : panels) p->tick(); repaint (236, 30, getWidth() - 236, 160); }
private:
    juce::OwnedArray<ui::TextLink> links; juce::OwnedArray<ValueRow> rows; juce::OwnedArray<DevicePanel> panels;
    ui::TextLink *tEuclid, *tRotL, *tRotR, *tRev, *tMutate, *tHuman, *tRandom, *tMarkov, *tRules, *tDouble, *tClear, *tCopy, *tPaste, *tAddDevice, *tAddLock;
    ui::TextLink *tRatios, *tAccel, *tEven, *tStrict, *tSieve, *tSievePitch, *tCloud, *tFreeze, *tAdditive, *tType, *tSlices, *tGroove, *tFollow, *tMidiFx, *tTrackRules;
    juce::Component chain; juce::Viewport viewport;
    Pattern clip; Rng rng;
    enum Mode { None, Pending, Vertical, Paint, Micro, Lane } mode = None;
    std::unique_ptr<juce::FileChooser> scl;
    int downStep = 0; float startVel = 0, startMicro = 0; bool paintOn = true, turnedOn = false;
};

// =================================================================================================
//  mix: strips, outputs, widths, routes
// =================================================================================================
/** the route icon of a strip: drag it onto another strip to route this track there; a click lists the routes */
class RouteHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    RouteHandle() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("route: drag this onto another track's strip to send this track's channels into it; click for the routes it has"); }
    std::function<void()> onClick; std::function<void (juce::Point<int> screen)> onDragTo, onDrop;
    bool active = false; int count = 0;
    void paint (juce::Graphics& g) override
    {
        const auto t = studioTheme(); auto b = getLocalBounds().toFloat().reduced (2.0f);
        const float cy = b.getCentreY();
        g.setColour (active ? hotColour() : t.ink);
        g.drawEllipse (b.getX() + 2.0f, cy - 5.0f, 10.0f, 10.0f, 1.4f);
        if (count > 0) g.fillEllipse (b.getX() + 4.5f, cy - 2.5f, 5.0f, 5.0f);
        juce::Path p; p.startNewSubPath (b.getX() + 13.0f, cy); p.lineTo (b.getX() + 24.0f, cy); p.startNewSubPath (b.getX() + 20.0f, cy - 4.0f); p.lineTo (b.getX() + 24.0f, cy); p.lineTo (b.getX() + 20.0f, cy + 4.0f);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        g.setFont (uiFont (10.5f)); g.drawText (count > 0 ? "route (" + juce::String (count) + ")" : juce::String ("route"), b.withTrimmedLeft (30.0f), juce::Justification::centredLeft, true);
    }
    void mouseDown (const juce::MouseEvent&) override { dragged = false; }
    void mouseDrag (const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 5) { dragged = true; active = true; repaint(); if (onDragTo) onDragTo (e.getScreenPosition()); } }
    void mouseUp (const juce::MouseEvent& e) override
    {
        active = false; repaint();
        if (dragged) { if (onDrop) onDrop (e.getScreenPosition()); }
        else if (onClick) onClick();
        dragged = false;
    }
private:
    bool dragged = false;
};

/** the popup after a route drag: how many channels, from where, into where */
class RoutePopup : public juce::Component
{
public:
    RoutePopup (Session& s, Track& from, Track& to, std::function<void()> done)
        : session (s), src (from), dst (to), close (std::move (done)), cancel ("cancel", studioTheme()), custom ("other...", studioTheme())
    {
        feedback = s.wouldFeedBack (from, to.uid);
        suggested = std::min (from.width.load(), to.width.load());
        for (int c : { 1, 2, 4, 6, 8, 9, 12, 16, 24, 25, 32, 36, 49, 64 })
        {
            if (c > from.width.load()) continue;
            auto* l = counts.add (new ui::TextLink (juce::String (c), studioTheme()));
            l->setLit (c == suggested); l->onClick = [this, c] { choose (c); };
            addAndMakeVisible (l);
        }
        if (counts.isEmpty() || from.width.load() != suggested) { }
        fromCh = std::make_unique<ValueRow> ("from channel", 1.0f, (float) std::max (1, from.width.load()), 1.0f, [] (float v) { return juce::String (juce::roundToInt (v)); });
        toCh = std::make_unique<ValueRow> ("into channel", 1.0f, (float) std::max (1, to.width.load()), 1.0f, [] (float v) { return juce::String (juce::roundToInt (v)); });
        gain = std::make_unique<ValueRow> ("gain", -24.0f, 12.0f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " db"; });
        for (auto* r : { fromCh.get(), toCh.get(), gain.get() }) { r->setLabelWidth (90); addAndMakeVisible (r); }
        addAndMakeVisible (cancel); addAndMakeVisible (custom);
        cancel.onClick = [this] { if (close) close(); };
        custom.onClick = [this]
        {
            auto* w = new juce::AlertWindow ("route", "how many channels", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("ch", juce::String (suggested)); w->addButton ("route", 1); w->addButton ("cancel", 0);
            juce::Component::SafePointer<RoutePopup> safe (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r) { if (r == 1 && safe) safe->choose (w->getTextEditorContents ("ch").getIntValue()); }), true);
        };
        setSize (std::max (340, 16 + counts.size() * 34), 168);
    }
    /** make the route over this many channels (what a click on a number does) */
    bool choose (int channels)
    {
        Route r; r.dst = dst.uid; r.channels = juce::jlimit (1, kMaxWidth, channels);
        r.srcOffset = juce::roundToInt (fromCh->get()) - 1; r.dstOffset = juce::roundToInt (toCh->get()) - 1; r.gain = juce::Decibels::decibelsToGain (gain->get());
        const bool ok = session.addRoute (src, r);
        if (close) close();
        return ok;
    }
    int suggestion() const { return suggested; }
    void resized() override
    {
        int x = 8; for (auto* l : counts) { l->setBounds (x, 48, 32, 22); x += 34; }
        fromCh->setBounds (8, 80, getWidth() - 16, 18); toCh->setBounds (8, 100, getWidth() - 16, 18); gain->setBounds (8, 120, getWidth() - 16, 18);
        custom.setBounds (8, 142, 70, 20); cancel.setBounds (getWidth() - 70, 142, 62, 20);
    }
    void paint (juce::Graphics& g) override
    {
        const auto t = studioTheme();
        g.fillAll (t.ground.brighter (t.mirror ? 0.25f : 0.14f)); g.setColour (t.ink); g.drawRect (getLocalBounds(), 1);
        g.setFont (uiFont (11.0f, true)); g.drawText ("route " + src.name + " (" + juce::String (src.width.load()) + " ch)" + ui::U (" → ") + dst.name + " (" + juce::String (dst.width.load()) + " ch)", 8, 6, getWidth() - 16, 16, juce::Justification::centredLeft, true);
        g.setFont (uiFont (10.5f));
        g.drawText (feedback ? ui::U ("how many channels?  ·  this closes a loop: a feedback route, one buffer late, limited") : juce::String ("how many channels?"), 8, 26, getWidth() - 16, 16, juce::Justification::centredLeft, true);
    }
    Session& session; Track& src; Track& dst;
private:
    std::function<void()> close; juce::OwnedArray<ui::TextLink> counts; ui::TextLink cancel, custom;
    std::unique_ptr<ValueRow> fromCh, toCh, gain; int suggested = 2; bool feedback = false;
};

class Strip : public juce::Component
{
public:
    Strip (Ctx& c, Track& t) : ctx (c), track (t), theme (studioTheme()), meter (t),
        mute ("mute", theme), solo ("solo", theme), arm ("rec", theme), out ("out", theme), width ("2 ch", theme), toMaster ("to master", theme), input ("in", theme), key ("key", theme), bounce ("bounce", theme), autoMaster ("auto master", theme)
    {
        addAndMakeVisible (meter);
        Device& lvlDev = t.isMaster() ? *c.s.global : *t.mixer;
        level = std::make_unique<BarSlider> (lvlDev, t.isMaster() ? (int) GP_MASTER : (int) MP_LEVEL, "level");
        Track* menuTrack = &t;
        level->onMenu = [this, menuTrack] (BarSlider& b) { ParamMenu::show (ctx.s, track.isMaster() ? *ctx.s.master : *menuTrack, b.device, b.index, &b, [this] (int) { ctx.select (&track); ctx.showPage (1); }); };
        addAndMakeVisible (*level);
        if (! t.isMaster())
        {
            pan = std::make_unique<BarSlider> (*t.mixer, MP_PAN, "pan");
            pan->onMenu = [this] (BarSlider& b) { ParamMenu::show (ctx.s, track, b.device, b.index, &b, [this] (int) { ctx.select (&track); ctx.showPage (1); }); };
            addAndMakeVisible (*pan);
        }
        if (! t.isMaster() && ! t.isReturn())
            for (int r = 0; r < kReturns; ++r)
            {
                auto* s = sends.add (new BarSlider (*t.mixer, MP_SEND1 + r, juce::String (r + 1)));
                s->onMenu = [this, r] (BarSlider& b)
                {
                    if (juce::ModifierKeys::currentModifiers.isAltDown()) { track.sendPre[(size_t) r].store (! track.sendPre[(size_t) r].load()); return; }
                    ParamMenu::show (ctx.s, track, b.device, b.index, &b, [this] (int) { ctx.select (&track); ctx.showPage (1); });
                };
                addAndMakeVisible (s);
            }
        for (auto* l : { &mute, &solo, &arm, &out, &width, &toMaster, &input, &key, &bounce, &autoMaster }) addAndMakeVisible (l);
        addAndMakeVisible (route);
        mute.onClick = [this] { track.mixer->setBase (MP_MUTE, track.muted() ? 0.0f : 1.0f); };
        solo.onClick = [this] { track.solo.store (! track.solo.load()); };
        arm.onClick = [this] { track.armed.store (! track.armed.load()); };
        toMaster.onClick = [this] { track.toMaster.store (! track.toMaster.load()); ctx.s.dirty = true; };
        out.onClick = [this] { outMenu(); };
        width.onClick = [this] { widthMenu(); };
        route.onClick = [this] { routeMenu(); };
        input.onClick = [this] { inputMenu(); };
        key.onClick = [this] { keyMenu(); };
        bounce.onClick = [this] { bounceMenu(); };
        autoMaster.onClick = [this] { if (onAutoMaster) onAutoMaster(); };
        mute.setTooltip ("mute (m)"); solo.setTooltip ("solo (s)"); toMaster.setTooltip ("to master: off, the track only feeds its routes and its own output channels");
        width.setTooltip ("how many channels this track has (1 - 64): reads > makes / width");
        out.setTooltip ("its own output channels (multi-out), and pre / post fader sends");
        key.setTooltip ("the track the sidechain devices on this track listen to");
        bounce.setTooltip (t.isMaster() ? "render the project (or the loop) into a new audio track" : "render this track into a new audio track or a sampler");
        autoMaster.setTooltip ("ms0 listens to the track for 20 seconds and sets the mastering chain once");
    }
    std::function<void()> onAutoMaster; std::function<void (Strip&, juce::Point<int>)> onRouteDrag, onRouteDrop;
    RouteHandle route;
    void outMenu()
    {
        juce::PopupMenu m;
        if (track.isMaster())
        {
            for (int c = 1; c + track.width.load() - 1 <= kOutChannels && c <= 65; c += 2) m.addItem (100 + c, "master on channels " + juce::String (c) + "-" + juce::String (c + track.width.load() - 1), true, ctx.s.masterOut.load() == c);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&out), [this] (int r) { if (r >= 100) { ctx.s.masterOut.store (r - 100); ctx.s.dirty = true; } });
            return;
        }
        m.addItem (1, "no own output (master only)", true, track.outChannel.load() == 0);
        for (int b = 0; b < kOutChannels; b += 16) { juce::PopupMenu sub; for (int c = b + 1; c <= b + 16; c += 2) sub.addItem (100 + c, "channels " + juce::String (c) + "-" + juce::String (c + track.width.load() - 1), true, track.outChannel.load() == c); m.addSubMenu ("channels " + juce::String (b + 1) + ".." + juce::String (b + 16), sub); }
        if (! track.isReturn()) for (int r = 0; r < (int) ctx.s.returns.size(); ++r) { const bool pre = track.sendPre[(size_t) r].load(); m.addItem (300 + r, "send " + juce::String (r + 1) + " (" + ctx.s.returns[(size_t) r]->name + ")" + (pre ? ": pre fader" : ": post fader")); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&out), [this] (int r)
        {
            if (r == 1) track.outChannel.store (0); else if (r >= 100 && r < 300) { track.outChannel.store (r - 100); ctx.s.multiOut.store (true); }
            else if (r >= 300) track.sendPre[(size_t) (r - 300)].store (! track.sendPre[(size_t) (r - 300)].load());
            ctx.s.rebuildLatency(); ctx.s.dirty = true;
        });
    }
    static const std::vector<int>& widths() { static const std::vector<int> w { 1, 2, 4, 6, 8, 9, 12, 16, 24, 25, 32, 36, 49, 64 }; return w; }
    void widthMenu()
    {
        juce::PopupMenu m;
        for (int w : widths())
        {
            if (track.isMaster() && w < 2) continue;
            juce::String n = juce::String (w) + " ch";
            if (w == 1) n << "  mono"; else if (w == 2) n << "  stereo";
            else if (w == 4 || w == 9 || w == 16 || w == 25 || w == 36 || w == 49 || w == 64) n << "  ambisonics, order " << juce::roundToInt (std::sqrt ((double) w)) - 1;
            m.addItem (w, n, true, track.width.load() == w);
        }
        m.addItem (1000, "other...");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&width), [this] (int r)
        {
            if (r > 0 && r < 1000) setWidth (r);
            else if (r == 1000)
            {
                auto* w = new juce::AlertWindow ("width", "channels (1 - 64)", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("ch", juce::String (track.width.load())); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                juce::Component::SafePointer<Strip> safe (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int rr) { if (rr == 1 && safe) safe->setWidth (w->getTextEditorContents ("ch").getIntValue()); }), true);
            }
        });
    }
    /** what the width menu does */
    void setWidth (int w) { ctx.s.setWidth (track, track.isMaster() ? std::max (2, w) : w); if (auto* p = getParentComponent()) p->repaint(); }
    void routeMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("drag the route icon onto another strip, or:");
        int id = 1; std::vector<int> dst;
        for (auto& t : ctx.s.tracks) if (t.get() != &track) { m.addItem (id++, "route to " + t->name + " (" + juce::String (t->width.load()) + " ch)..."); dst.push_back (t->uid); }
        for (auto& t : ctx.s.returns) if (t.get() != &track) { m.addItem (id++, "route to " + t->name + "..."); dst.push_back (t->uid); }
        if (! track.routes->empty()) { m.addSeparator(); for (int i = 0; i < (int) track.routes->size(); ++i) { const auto& r = (*track.routes)[(size_t) i]; auto* d = ctx.s.trackByUid (r.dst); m.addItem (500 + i, "remove the route to " + (d ? d->name : juce::String ("?")) + " (" + juce::String (r.channels) + " ch" + (r.feedback ? ", feedback" : "") + ")"); } }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&route), [this, dst] (int r)
        {
            if (r >= 500) { ctx.s.removeRoute (track, r - 500); return; }
            if (r <= 0 || r > (int) dst.size()) return;
            if (auto* d = ctx.s.trackByUid (dst[(size_t) r - 1])) if (onRouteAsk) onRouteAsk (*this, *d);
        });
    }
    std::function<void (Strip&, Track&)> onRouteAsk;
    void inputMenu()
    {
        juce::PopupMenu m;
        const int n = std::max (2, ctx.s.inputChannels.load()), w = track.width.load();
        m.addSectionHeader ("device input");
        for (int c = 1; c <= n; c += 2) m.addItem (100 + c, "input " + juce::String (c) + "-" + juce::String (c + 1), true, track.inputSource.load() == -2 && track.inputChannel.load() == c && track.inputCount.load() == 2);
        juce::PopupMenu mono; for (int c = 1; c <= n; ++c) mono.addItem (300 + c, "input " + juce::String (c), true, track.inputSource.load() == -2 && track.inputChannel.load() == c && track.inputCount.load() == 1);
        m.addSubMenu ("mono", mono);
        if (w > 2) { juce::PopupMenu wide; for (int c = 1; c + w - 1 <= std::max (n, w); c += (w > 8 ? w : 2)) wide.addItem (600 + c, "inputs " + juce::String (c) + "-" + juce::String (c + w - 1) + " (" + juce::String (w) + " ch)", true, track.inputSource.load() == -2 && track.inputChannel.load() == c && track.inputCount.load() == w); m.addSubMenu (juce::String (w) + " channels (the track's width)", wide); }
        m.addSeparator();
        m.addItem (2, "resample: the master", true, track.inputSource.load() == 0);
        juce::PopupMenu rs; for (auto& t : ctx.s.tracks) if (t.get() != &track) rs.addItem (1000 + t->uid, t->name, true, track.inputSource.load() == t->uid); m.addSubMenu ("resample: a track", rs);
        m.addSeparator();
        m.addItem (3, "monitor: off", true, track.monitor.load() == 0); m.addItem (4, "monitor: in", true, track.monitor.load() == 1); m.addItem (5, "monitor: auto (when armed)", true, track.monitor.load() == 2);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&input), [this] (int r)
        {
            if (r >= 1000) { track.inputSource.store (r - 1000); }
            else if (r >= 600) { track.inputSource.store (-2); track.inputChannel.store (r - 600); track.inputCount.store (track.width.load()); }
            else if (r >= 300) { track.inputSource.store (-2); track.inputChannel.store (r - 300); track.inputCount.store (1); }
            else if (r >= 100) { track.inputSource.store (-2); track.inputChannel.store (r - 100); track.inputCount.store (2); }
            else if (r == 2) track.inputSource.store (0);
            else if (r >= 3 && r <= 5) track.monitor.store (r - 3);
            if (r > 0) { ctx.s.negotiate (track); ctx.s.dirty = true; }
        });
    }
    void keyMenu()
    {
        juce::PopupMenu m; m.addItem (1, "key: the master", true, track.keyTrack.load() == 0);
        for (auto& t : ctx.s.tracks) if (t.get() != &track) m.addItem (1000 + t->uid, "key: " + t->name, true, track.keyTrack.load() == t->uid);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&key), [this] (int r) { if (r == 1) track.keyTrack.store (0); else if (r >= 1000) track.keyTrack.store (r - 1000); });
    }
    /** bounce: 1 a new audio track · 2 a sampler track · on the master: 1 the project (through the master chain) · 2 before the master chain */
    void bounceTo (int what)
    {
        double a, b; ctx.s.renderRange (a, b);
        juce::String err; Track* made = nullptr;
        if (track.isMaster()) made = ctx.s.renderProjectToTrack (a, b, what == 2, err);
        else made = what == 2 ? ctx.s.renderTrackToSampler (track, a, b, err) : ctx.s.renderTrackToTrack (track, a, b, err);
        if (made) { ctx.select (made); ctx.status ((track.isMaster() ? juce::String ("project") : track.name) + " rendered (bars " + juce::String (a + 1, 0) + " - " + juce::String (b, 0) + ") into " + made->name); }
        else ctx.status (err.isNotEmpty() ? err : juce::String ("nothing rendered"));
    }
    void bounceMenu()
    {
        juce::PopupMenu m;
        double a, b; ctx.s.renderRange (a, b);
        const juce::String range = ctx.s.loopOn.load() ? "the loop" : juce::String (playModeName ((PlayMode) ctx.s.mode.load())) + ", " + juce::String (b - a, 0) + " bars";
        if (track.isMaster()) { m.addItem (1, "render the project into a new audio track (" + range + ")"); m.addItem (2, "the same, before the master chain"); }
        else { m.addItem (1, "bounce this track into a new audio track (" + range + ")"); m.addItem (2, "bounce it into a sampler on a new track (sliced)"); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bounce), [this] (int r) { if (r > 0) bounceTo (r); });
    }
    void resized() override
    {
        const int w = getWidth(), in = 4, cw = w - 2 * in, half = cw / 2;
        int y = 40;
        const int meterH = juce::jlimit (90, 240, getHeight() - 430);
        meter.setBounds (in + 2, y, cw - 4, meterH); y += meterH + 8;
        level->setBounds (in, y, cw, 22); y += 23;
        if (pan) { pan->setBounds (in, y, cw, 22); y += 23; }
        y += 4;
        const int nr = (int) ctx.s.returns.size();
        for (int r = 0; r < sends.size(); ++r)
        {
            sends[r]->setVisible (r < nr);
            if (r < nr) { sends[r]->setLabel (ui::U ("→ ") + ctx.s.returns[(size_t) r]->name); sends[r]->setBounds (in, y, cw, 22); y += 23; }
        }
        y += 6;
        const bool master = track.isMaster(), ret = track.isReturn();
        mute.setVisible (! master); solo.setVisible (! master && ! ret); arm.setVisible (track.isAudio()); input.setVisible (track.isAudio());
        toMaster.setVisible (! master); route.setVisible (! master && ! ret); key.setVisible (! master); autoMaster.setVisible (master && ! kDeep);
        if (! master) { mute.setBounds (in, y, half, 20); solo.setBounds (in + half, y, half, 20); y += 22; }
        if (track.isAudio()) { arm.setBounds (in, y, half, 20); input.setBounds (in + half, y, half, 20); y += 22; }
        if (! master) { toMaster.setBounds (in, y, cw, 20); y += 22; }
        width.setBounds (in, y, cw, 20); y += 22;
        out.setBounds (in, y, cw, 20); y += 22;
        if (route.isVisible()) { route.setBounds (in, y, cw, 22); y += 24; }
        if (! master) { key.setBounds (in, y, half, 20); bounce.setBounds (in + half, y, half, 20); y += 22; }
        else { bounce.setBounds (in, y, cw, 20); y += 22; autoMaster.setBounds (in, y, cw, 20); y += 22; loudY = y + 4; y += 84; }
        routesY = y + 2;
    }
    void paint (juce::Graphics& g) override
    {
        const bool sel = ctx.selected() == &track;
        g.setColour (theme.ink.withAlpha (sel ? 0.12f : 0.05f)); g.fillRect (getLocalBounds().reduced (1, 0));
        g.setColour (trackColour (track)); g.fillRect (0, 0, getWidth(), 4);
        g.setColour (theme.ink); g.setFont (uiFont (11.5f, sel));
        g.drawText (track.name, 6, 8, getWidth() - 12, 16, juce::Justification::centredLeft, true);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.75f));
        juce::String sub;
        if (track.isMaster()) sub = juce::String (track.width.load()) + " ch, channels " + juce::String (ctx.s.masterOut.load()) + "-" + juce::String (ctx.s.masterOut.load() + track.width.load() - 1);
        else sub = track.isAudio() ? "audio" : track.isReturn() ? "return" : (track.instrument ? track.instrument->name : juce::String ("no instrument"));
        // its delay: its own chain's (plug-in latency) and what it waits to line up with the rest (compensation)
        if (! track.isMaster())
        {
            const double msPer = 1000.0 / std::max (1.0, ctx.s.sampleRate());
            const int own = ctx.s.trackLatency (track), wait = ctx.s.trackAlignDelay (track);
            if (own > 0 || wait > 0) sub << "  " << ui::U ("·") << " " << (own > 0 ? juce::String (own * msPer, 1) + " ms" : juce::String()) << (wait > 0 ? " +" + juce::String (wait * msPer, 1) + " wait" : juce::String());
        }
        g.drawText (sub, 6, 23, getWidth() - 12, 13, juce::Justification::centredLeft, true);
        mute.setLit (track.muted()); solo.setLit (track.solo.load()); arm.setLit (track.armed.load()); toMaster.setLit (track.toMaster.load());
        toMaster.setText (track.toMaster.load() ? "to master" : "not to master");
        width.setText (track.isMaster() ? "width: " + juce::String (track.width.load()) + " ch" : "width: " + juce::String (track.inWidth()) + ui::U (" → ") + juce::String (track.outWidth()) + " / " + juce::String (track.width.load()) + " ch");
        const int oc = track.outChannel.load();
        out.setText (track.isMaster() ? "out " + juce::String (ctx.s.masterOut.load()) + "-" + juce::String (ctx.s.masterOut.load() + track.width.load() - 1)
                                      : (oc > 0 && ctx.s.multiOut.load() ? "out " + juce::String (oc) + "-" + juce::String (oc + track.width.load() - 1) : juce::String ("out: master only")));
        out.setLit (! track.isMaster() && oc > 0 && ctx.s.multiOut.load());
        if (track.isAudio()) input.setText (track.inputSource.load() == -2 ? "in " + juce::String (track.inputChannel.load()) + (track.inputCount.load() > 1 ? "-" + juce::String (track.inputChannel.load() + track.inputCount.load() - 1) : juce::String()) : track.inputSource.load() == 0 ? juce::String ("in: master") : juce::String ("in: track"));
        route.count = (int) track.routes->size();
        // the routes this track has, under the buttons
        g.setFont (uiFont (9.5f)); int y = routesY;
        for (auto& r : *track.routes)
        {
            auto* d = ctx.s.trackByUid (r.dst);
            g.setColour (hotColour()); g.drawText (ui::U ("→ ") + (d ? d->name : juce::String ("?")) + "  " + juce::String (r.channels) + " ch" + (r.feedback ? " fb" : ""), 6, y, getWidth() - 12, 13, juce::Justification::centredLeft, true);
            y += 13;
        }
        for (int r = 0; r < sends.size() && r < (int) ctx.s.returns.size(); ++r)
            if (track.sendPre[(size_t) r].load()) { g.setColour (theme.ink.withAlpha (0.7f)); g.drawText ("pre", sends[r]->getRight() - 60, sends[r]->getY() - 1, 24, 10, juce::Justification::centredLeft); }
        g.setColour (theme.ink.withAlpha (0.2f)); g.fillRect (getWidth() - 1, 0, 1, getHeight());
        if (dropTarget) { g.setColour (hotColour()); g.drawRect (getLocalBounds().reduced (1), 2); }
        paintLoudness (g);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 40) ctx.select (&track);
        if (track.isMaster() && loudY >= 0 && e.y >= loudY && e.y < loudY + 82) { ctx.s.loudness.reset(); ctx.status ("loudness: measuring again from now"); }
    }
    void tick() { meter.tick(); repaint(); }
    int loudY = -1;
    /** the master's loudness: momentary, short-term, integrated (since play, or a click), range, true peak */
    void paintLoudness (juce::Graphics& g)
    {
        if (! track.isMaster() || loudY < 0) return;
        const auto& L = ctx.s.loudness;
        auto num = [] (float v) { return v <= -70.0f ? juce::String ("-") : juce::String (v, 1); };
        const int x = 8, w = getWidth() - 16; int y = loudY;
        g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (x - 2, y - 2, w + 4, 82);
        g.setFont (uiFont (9.0f)); g.setColour (theme.ink.withAlpha (0.7f)); g.drawText ("loudness (lufs)", x, y, w, 12, juce::Justification::centredLeft); y += 13;
        auto row = [&] (const juce::String& k, const juce::String& v, bool big = false, juce::Colour c = {})
        {
            g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (uiFont (9.5f)); g.drawText (k, x, y, w / 2, 13, juce::Justification::centredLeft);
            g.setColour (c.isTransparent() ? theme.ink : c); g.setFont (uiFont (big ? 11.5f : 10.0f, big)); g.drawText (v, x + w / 2, y, w - w / 2, 13, juce::Justification::centredRight); y += 13;
        };
        row ("momentary", num (L.momentary)); row ("short-term", num (L.shortTerm)); row ("integrated", num (L.integrated), true);
        row ("range", L.range > 0 ? juce::String (L.range, 1) + " lu" : juce::String ("-"));
        const float tp = L.truePeakDb();
        row ("true peak", tp <= -70.0f ? juce::String ("-") : juce::String (tp, 1) + " dbtp", false, tp > -1.0f ? hotColour() : juce::Colour());
    }
    Ctx& ctx; Track& track; ui::Theme theme; bool dropTarget = false;
    Meter meter; std::unique_ptr<BarSlider> level, pan; juce::OwnedArray<BarSlider> sends;
    ui::TextLink mute, solo, arm, out, width, toMaster, input, key, bounce, autoMaster;
    int routesY = 0;
};

/** a group's vca: a fader that scales every member's level (their own faders keep their places), its mute and solo */
class VcaStrip : public juce::Component
{
public:
    VcaStrip (Ctx& c, int groupId) : ctx (c), id (groupId), theme (studioTheme()) {}
    const Group* group() const { return ctx.s.groupOf (id); }
    juce::Rectangle<float> faderArea() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (64.0f).withTrimmedBottom (54.0f); }
    static float dbToY01 (float db) { return db <= -60.0f ? 0.0f : std::pow ((db + 60.0f) / 72.0f, 1.6f); }      // -60 … +12 db
    static float y01ToDb (float v) { return v <= 0.0005f ? -96.0f : std::pow (juce::jlimit (0.0f, 1.0f, v), 1.0f / 1.6f) * 72.0f - 60.0f; }
    void edit (const std::function<void (Group&)>& fn) { ctx.s.editGroups ([this, &fn] (std::vector<Group>& v) { for (auto& g : v) if (g.id == id) fn (g); }); repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto* gr = group(); if (! gr) return;
        auto b = getLocalBounds();
        g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (b.reduced (2, 0));
        g.setColour (gr->colour ? juce::Colour (gr->colour) : theme.ink.withAlpha (0.5f)); g.fillRect (2, 0, b.getWidth() - 4, 4);
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText (gr->name, 8, 8, b.getWidth() - 16, 16, juce::Justification::centredLeft, true);
        g.setFont (uiFont (9.0f)); g.setColour (theme.ink.withAlpha (0.7f));
        juce::StringArray members; for (auto& t : ctx.s.tracks) if (t->group.load() == id) members.add (t->name);
        g.drawFittedText ("vca: " + (members.isEmpty() ? juce::String ("no tracks") : members.joinIntoString (", ")), 8, 26, b.getWidth() - 16, 34, juce::Justification::topLeft, 3);
        const auto fa = faderArea();
        g.setColour (theme.ink.withAlpha (0.15f)); g.fillRect (fa.getCentreX() - 1.5f, fa.getY(), 3.0f, fa.getHeight());
        const float zeroY = fa.getBottom() - dbToY01 (0.0f) * fa.getHeight();
        g.setColour (theme.ink.withAlpha (0.4f)); g.fillRect (fa.getX() + 6.0f, zeroY, fa.getWidth() - 12.0f, 1.0f);
        const float y = fa.getBottom() - dbToY01 (gr->vcaDb) * fa.getHeight();
        g.setColour (theme.fill); g.fillRect (fa.getX() + 2.0f, y - 5.0f, fa.getWidth() - 4.0f, 10.0f);
        g.setColour (theme.ink); g.setFont (uiFont (10.0f));
        g.drawText (gr->vcaDb <= -95.0f ? juce::String ("-inf db") : (gr->vcaDb > 0 ? "+" : "") + juce::String (gr->vcaDb, 1) + " db", 4, b.getHeight() - 52, b.getWidth() - 8, 16, juce::Justification::centred);
        auto btn = [&] (juce::Rectangle<int> r, const juce::String& t, bool on) { g.setColour (on ? theme.fill : theme.ink.withAlpha (0.1f)); g.fillRect (r); g.setColour (on ? ui::inkFor (theme.fill) : theme.ink); g.drawText (t, r, juce::Justification::centred); };
        btn (muteBox(), "mute", gr->mute); btn (soloBox(), "solo", gr->solo);
    }
    juce::Rectangle<int> muteBox() const { return { 6, getHeight() - 30, getWidth() / 2 - 8, 20 }; }
    juce::Rectangle<int> soloBox() const { return { getWidth() / 2 + 2, getHeight() - 30, getWidth() / 2 - 8, 20 }; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* gr = group(); if (! gr) return;
        if (muteBox().contains (e.getPosition())) { edit ([] (Group& g) { g.mute = ! g.mute; }); return; }
        if (soloBox().contains (e.getPosition())) { edit ([] (Group& g) { g.solo = ! g.solo; }); return; }
        if (e.y < 26 && e.getNumberOfClicks() > 1)
        {
            auto* w = new juce::AlertWindow ("rename the group", "a name", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("t", gr->name); w->addButton ("ok", 1); w->addButton ("cancel", 0);
            juce::Component::SafePointer<VcaStrip> sp (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r) { if (sp && r == 1) { const auto n = w->getTextEditorContents ("t").trim(); sp->edit ([n] (Group& g) { g.name = n; }); } }), true);
            return;
        }
        if (faderArea().expanded (4.0f).contains (e.position))
        {
            if (e.getNumberOfClicks() > 1) { edit ([] (Group& g) { g.vcaDb = 0.0f; }); return; }
            startDb = gr->vcaDb; dragging = true;
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto fa = faderArea();
        const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
        const float v = dbToY01 (startDb) - (float) e.getDistanceFromDragStartY() / fa.getHeight() * fine;
        const float db = juce::jlimit (-96.0f, 12.0f, y01ToDb (v));
        edit ([db] (Group& g) { g.vcaDb = db; });
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    Ctx& ctx; const int id;
private:
    ui::Theme theme; float startDb = 0; bool dragging = false;
};

class MixPage : public Page
{
public:
    explicit MixPage (Ctx& c) : Page (c), mode ("produce", theme), assign ("assign all outputs", theme), bypass ("master chain bypass", theme), lowLat ("low latency", theme)
    {
        for (auto* l : { &mode, &assign, &bypass, &lowLat }) addAndMakeVisible (l);
        lowLat.onClick = [this] { ctx.s.setLowLatency (! ctx.s.lowLatency.load()); ctx.status (ctx.s.lowLatency.load() ? "low latency: devices that delay the sound more than " + juce::String (ctx.s.lowLatencyLimit * 1000.0 / std::max (1.0, ctx.s.sampleRate()), 1) + " ms are left out (to record and play in); off again when done" : "low latency off: every device is back"); };
        lowLat.setTooltip ("for recording: leaves out the devices that delay the sound (look-ahead limiters, linear-phase eqs, some plug-ins), so what you play is heard at once; the latency compensation shrinks with them");
        mode.onClick = [this] { ctx.s.multiOut.store (! ctx.s.multiOut.load()); ctx.s.dirty = true; };
        assign.onClick = [this] { ctx.s.assignOutputs(); rebuild(); };
        bypass.onClick = [this] { ctx.s.masterBypass.store (! ctx.s.masterBypass.load()); ctx.s.dirty = true; };
        mode.setTooltip ("produce: everything is mixed to the master. multi-out: tracks and returns leave on their own channels");
        assign.setTooltip ("master on channels 1 ..., then every track and return on its own channels, to-master off");
        bypass.setTooltip ("bypass the master's devices (when mixing and mastering happen in another daw)");
        viewport.setViewedComponent (&row, false); viewport.setScrollBarsShown (false, true); addAndMakeVisible (viewport);
    }
    void rebuild() override
    {
        popup.reset();
        strips.clear(); row.removeAllChildren();
        auto add = [&] (Track& t)
        {
            auto* s = strips.add (new Strip (ctx, t)); row.addAndMakeVisible (s);
            s->route.onDragTo = [this, s] (juce::Point<int> screen) { dragFrom = s; dragAt = getLocalPoint (nullptr, screen); for (auto* o : strips) o->dropTarget = o != s && o->getScreenBounds().contains (screen) && ! o->track.isMaster(); repaint(); };
            s->route.onDrop = [this, s] (juce::Point<int> screen) { routeDropped (*s, screen); };
            s->onRouteAsk = [this] (Strip& from, Track& to) { askRoute (from.track, to, from.getBounds().getCentre() + viewport.getPosition() - viewport.getViewPosition()); };
            s->onAutoMaster = [this] { if (onAutoMaster) onAutoMaster(); };
        };
        for (auto& t : ctx.s.tracks) add (*t);
        for (auto& t : ctx.s.returns) add (*t);
        add (*ctx.s.master);
        vcas.clear();
        for (auto& g : *ctx.s.groups) row.addAndMakeVisible (vcas.add (new VcaStrip (ctx, g.id)));
        shownGroups = ctx.s.groups.get(); shownGroupCount = ctx.s.groups->size();
        resized();
    }
    VcaStrip* vcaFor (int groupId) { for (auto* v : vcas) if (v->id == groupId) return v; return nullptr; }
    std::function<void()> onAutoMaster;
    /** the strip of a track (scripts and tests) */
    Strip* stripFor (const Track& t) { for (auto* s : strips) if (&s->track == &t) return s; return nullptr; }
    RoutePopup* routePopup() { return popup.get(); }
    void routeDropped (Strip& from, juce::Point<int> screen)
    {
        dragFrom = nullptr;
        Strip* target = nullptr;
        for (auto* o : strips) { if (o != &from && o->getScreenBounds().contains (screen) && ! o->track.isMaster()) target = o; o->dropTarget = false; }
        repaint();
        if (! target) { ctx.status ("route: drop the icon on another track's strip"); return; }
        askRoute (from.track, target->track, getLocalPoint (nullptr, screen));
    }
    /** the popup that asks how many channels */
    void askRoute (Track& from, Track& to, juce::Point<int> at)
    {
        popup = std::make_unique<RoutePopup> (ctx.s, from, to, [this] { juce::Component::SafePointer<MixPage> safe (this); juce::MessageManager::callAsync ([safe] { if (safe) { safe->popup.reset(); safe->rebuildKeepingScroll(); } }); closing = true; });
        closing = false;
        addAndMakeVisible (*popup);
        popup->setTopLeftPosition (juce::jlimit (4, std::max (4, getWidth() - popup->getWidth() - 4), at.x - popup->getWidth() / 2), juce::jlimit (36, std::max (36, getHeight() - popup->getHeight() - 30), at.y - 40));
        popup->toFront (false);
    }
    void rebuildKeepingScroll() { const auto p = viewport.getViewPosition(); resized(); viewport.setViewPosition (p); repaint(); }
    void resized() override
    {
        mode.setBounds (8, 6, 90, 20); assign.setBounds (104, 6, 150, 20); bypass.setBounds (260, 6, 160, 20); lowLat.setBounds (426, 6, 96, 20);
        viewport.setBounds (0, 32, getWidth(), getHeight() - 58);
        const int n = std::max (1, strips.size());
        const int sw = juce::jlimit (vcas.isEmpty() ? 118 : 100, 150, (getWidth() - 2 - vcas.size() * 102) / n); int x = 0;
        for (auto* s : strips) { s->setBounds (x, 0, sw, viewport.getHeight() - 12); x += sw; }
        for (auto* v : vcas) { v->setBounds (x + 6, 0, 96, viewport.getHeight() - 12); x += 102; }
        row.setSize (std::max (x, 10), viewport.getHeight() - 12);
    }
    void paint (juce::Graphics& g) override
    {
        mode.setText (ctx.s.multiOut.load() ? "multi-out" : "produce"); mode.setLit (ctx.s.multiOut.load()); bypass.setLit (ctx.s.masterBypass.load()); lowLat.setLit (ctx.s.lowLatency.load());
        g.setColour (theme.ink.withAlpha (0.75f)); g.setFont (uiFont (10.0f));
        g.drawText (ui::U ("drag a strip's route icon onto another strip to route it there · width: the track's channels (1 - 64) · alt-right-click a send: pre / post fader"), 530, 8, getWidth() - 540, 16, juce::Justification::centredLeft, true);
        juce::String routes;
        for (auto& t : ctx.s.tracks) for (auto& r : *t->routes) if (auto* d = ctx.s.trackByUid (r.dst)) routes << t->name << ui::U (" → ") << d->name << " (" << r.channels << " ch" << (r.feedback ? ", feedback" : "") << ")   ";
        g.drawText (routes.isEmpty() ? juce::String (ctx.s.multiOut.load() ? "multi-out: every track and return with an output leaves on its own device channels (1-2 ... 127-128); record them in another daw over a 128-channel device or the plug-in's buses"
                                                                           : "produce: everything is mixed to the master. multi-out puts tracks on their own channels")
                                     : "routes: " + routes, 8, getHeight() - 22, getWidth() - 16, 16, juce::Justification::centredLeft, true);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        // the routes as cables between the strips, and the one being dragged
        const juce::Colour cable (0xffee8e6e);
        for (auto* s : strips)
            for (auto& r : *s->track.routes)
                for (auto* d : strips)
                    if (d->track.uid == r.dst && &d->track != ctx.s.master.get())
                    {
                        const auto a = getLocalPoint (&s->route, juce::Point<int> (8, s->route.getHeight() / 2)).toFloat(), b = getLocalPoint (d, juce::Point<int> (d->getWidth() / 2, 36)).toFloat();
                        if (a.x < -50 || b.x < -50 || a.x > getWidth() + 50 || b.x > getWidth() + 50) continue;
                        juce::Path p; p.startNewSubPath (a); p.cubicTo (a.x, a.y + 60.0f, b.x, b.y - 30.0f, b.x, b.y);
                        g.setColour (cable.withAlpha (r.feedback ? 0.35f : 0.55f)); g.strokePath (p, juce::PathStrokeType (1.3f));
                        g.fillEllipse (b.x - 3.0f, b.y - 3.0f, 6.0f, 6.0f);
                    }
        if (dragFrom)
        {
            const auto a = getLocalPoint (&dragFrom->route, juce::Point<int> (8, dragFrom->route.getHeight() / 2)).toFloat();
            g.setColour (cable); g.drawLine (a.x, a.y, (float) dragAt.x, (float) dragAt.y, 2.0f);
            g.fillEllipse ((float) dragAt.x - 4.0f, (float) dragAt.y - 4.0f, 8.0f, 8.0f);
        }
    }
    void tick() override
    {
        // a group made, removed or joined elsewhere (the arrangement's track menu): the vca strips again
        if (ctx.s.groups->size() != shownGroupCount) rebuildKeepingScrollFull();
        for (auto* s : strips) s->tick();
        for (auto* v : vcas) v->repaint();
        repaint (0, 0, getWidth(), 30);
    }
    void rebuildKeepingScrollFull() { const auto p = viewport.getViewPosition(); rebuild(); viewport.setViewPosition (p); }
private:
    juce::OwnedArray<VcaStrip> vcas; const void* shownGroups = nullptr; size_t shownGroupCount = 0;
    ui::TextLink mode, assign, bypass, lowLat;
    juce::Component row; juce::Viewport viewport; juce::OwnedArray<Strip> strips;
    std::unique_ptr<RoutePopup> popup; Strip* dragFrom = nullptr; juce::Point<int> dragAt; bool closing = false;
};

// =================================================================================================
//  mod: modulators of the selected track, macros, midi maps
// =================================================================================================
/** controller buttons for commands (learnt), and a mackie control surface */
class ControllerPanel : public juce::Component, private juce::Timer
{
public:
    explicit ControllerPanel (Session& s) : session (s), theme (studioTheme()) { startTimerHz (10); }
    std::function<void()> onClose;
    static constexpr int rowH = 19;
    std::vector<int> commands() const { std::vector<int> v; for (int c = CMD_PLAY; c <= CMD_LAST; ++c) v.push_back (c); return v; }
    juce::String mappingOf (int c) const { for (auto& m : *session.cmdMaps) if (m.command == c) return (m.note ? "note " : "cc ") + juce::String (m.number); return {}; }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground.withAlpha (0.98f)); g.setColour (theme.ink.withAlpha (0.3f)); g.drawRect (getLocalBounds());
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true)); g.drawText ("controllers", 10, 6, 200, 16, juce::Justification::centredLeft);
        g.setFont (uiFont (10.0f)); g.drawText ("close", getWidth() - 60, 6, 50, 16, juce::Justification::centredRight);
        const bool mk = session.mackie.load();
        g.setColour (mk ? theme.fill : theme.ink.withAlpha (0.1f)); g.fillRect (mackieBox());
        g.setColour (mk ? ui::inkFor (theme.fill) : theme.ink); g.drawText (mk ? "mackie control: on" : "mackie control: off", mackieBox(), juce::Justification::centred);
        g.setColour (theme.ink.withAlpha (0.75f)); g.setFont (uiFont (9.0f));
        g.drawFittedText (mk ? "faders, v-pots (pan), rec / solo / mute / select, bank and channel, the transport. tracks " + juce::String (session.mackieBank.load() + 1) + "-" + juce::String (session.mackieBank.load() + 8)
                             : "a surface that speaks mackie control (most fader boxes and controllers have the mode): its faders, buttons and transport", 10, 50, getWidth() - 20, 24, juce::Justification::topLeft, 2);
        g.setFont (uiFont (10.0f));
        int y = top;
        for (int c : commands())
        {
            const bool learning = session.cmdLearning.load() == c;
            g.setColour (theme.ink); g.drawText (commandName (c), 10, y, 190, rowH, juce::Justification::centredLeft, true);
            const auto mp = mappingOf (c);
            g.setColour (learning ? hotColour() : theme.ink.withAlpha (mp.isEmpty() ? 0.4f : 1.0f));
            g.drawText (learning ? juce::String ("press a button...") : mp.isEmpty() ? juce::String ("-") : mp, 200, y, 110, rowH, juce::Justification::centredLeft, true);
            g.setColour (theme.ink.withAlpha (0.85f)); g.drawText ("learn", getWidth() - 100, y, 44, rowH, juce::Justification::centredRight); if (mp.isNotEmpty()) g.drawText ("clear", getWidth() - 50, y, 42, rowH, juce::Justification::centredRight);
            y += rowH;
        }
    }
    juce::Rectangle<int> mackieBox() const { return { 10, 26, 180, 20 }; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y < 24 && e.x > getWidth() - 70) { if (onClose) onClose(); return; }
        if (mackieBox().contains (e.getPosition())) { session.mackie.store (! session.mackie.load()); session.dirty = true; repaint(); return; }
        const int i = (e.y - top) / rowH; const auto cs = commands();
        if (e.y < top || i < 0 || i >= (int) cs.size()) return;
        const int c = cs[(size_t) i];
        if (e.x >= getWidth() - 100 && e.x < getWidth() - 54) session.cmdLearning.store (session.cmdLearning.load() == c ? 0 : c);
        else if (e.x >= getWidth() - 54) { auto v = *session.cmdMaps; v.erase (std::remove_if (v.begin(), v.end(), [c] (const CmdMap& m) { return m.command == c; }), v.end()); session.setCmdMaps (v); }
        repaint();
    }
    int preferredHeight() const { return top + (int) commands().size() * rowH + 8; }
private:
    void timerCallback() override { repaint(); }
    Session& session; ui::Theme theme; static constexpr int top = 80;
};

class ModPage : public Page
{
public:
    explicit ModPage (Ctx& c) : Page (c), addMod ("+ modulator", theme), ctlLink ("controllers...", theme)
    {
        addAndMakeVisible (addMod); addAndMakeVisible (ctlLink);
        ctlLink.onClick = [this]
        {
            if (ctl) { ctl.reset(); return; }
            ctl = std::make_unique<ControllerPanel> (ctx.s);
            ctl->onClose = [this] { juce::Component::SafePointer<ModPage> sp (this); juce::MessageManager::callAsync ([sp] { if (sp) sp->ctl.reset(); }); if (ctl) ctl->setVisible (false); };
            addAndMakeVisible (*ctl); resized();
        };
        ctlLink.setTooltip (ui::U ("controller buttons for play, record, markers, scenes … (learn), and mackie control surfaces"));
        addMod.onClick = [this] { targetMenu(); };
        for (int i = 0; i < kMacros; ++i)
        {
            auto* s = macros.add (new ui::ParamSlider (ctx.s.global->base (GP_MACRO1 + i), theme, ctx.s.macroNames[(size_t) i]));
            s->setLabelWidth (90); addAndMakeVisible (s);
            auto* r = renames.add (new ui::TextLink ("name", theme)); addAndMakeVisible (r);
            r->onClick = [this, i]
            {
                auto* w = new juce::AlertWindow ("macro name", "", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", ctx.s.macroNames[(size_t) i]); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, i, w] (int rr) { if (rr == 1) { ctx.s.macroNames[(size_t) i] = w->getTextEditorContents ("name"); rebuild(); } }), true);
            };
        }
    }
    void rebuild() override
    {
        macros.clear(); renames.clear();
        for (int i = 0; i < kMacros; ++i)
        {
            auto* s = macros.add (new ui::ParamSlider (ctx.s.global->base (GP_MACRO1 + i), theme, ctx.s.macroNames[(size_t) i]));
            s->setLabelWidth (90); addAndMakeVisible (s);
            auto* r = renames.add (new ui::TextLink ("name", theme)); addAndMakeVisible (r);
            r->onClick = [this, i]
            {
                auto* w = new juce::AlertWindow ("macro name", "", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", ctx.s.macroNames[(size_t) i]); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, i, w] (int rr) { if (rr == 1) { ctx.s.macroNames[(size_t) i] = w->getTextEditorContents ("name"); rebuild(); } }), true);
            };
        }
        resized(); repaint();
    }
    void targetMenu()
    {
        auto* t = ctx.selected(); if (! t) return;
        juce::PopupMenu m; int id = 1; std::vector<std::pair<int, int>> list;
        auto addDev = [&] (Device& d, const juce::String& name) { juce::PopupMenu sub; for (int i = 0; i < std::min (d.numParams(), 120); ++i) { sub.addItem (id++, d.paramName (i)); list.push_back ({ d.uid, i }); } m.addSubMenu (name, sub); };
        if (t->isMaster()) addDev (*ctx.s.global, "global (tempo, swing, master level, macros)"); else addDev (*t->mixer, "strip");
        if (t->instrument) addDev (*t->instrument, t->instrument->name); for (auto& d : t->inserts) addDev (*d, d->name);
        juce::PopupMenu mods; for (int i = 0; i < (int) t->mods->size(); ++i) { mods.addItem (id++, "rate of modulator " + juce::String ((*t->mods)[(size_t) i].id)); list.push_back ({ -1, i }); } m.addSubMenu ("another modulator's rate", mods);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addMod), [this, list] (int r)
        {
            if (r <= 0 || r > (int) list.size()) return;
            auto* t = ctx.selected(); if (! t) return;
            Modulator mm; mm.device = list[(size_t) r - 1].first; mm.param = list[(size_t) r - 1].second;
            const int mid = ctx.s.addModulator (*t, mm);
            auto* pop = new ModPopup (ctx.s, *t, mid);
            juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (pop), addMod.getScreenBounds(), nullptr);
            repaint();
        });
    }
    void resized() override
    {
        addMod.setBounds (8, 30, 100, 20);
        ctlLink.setBounds (getWidth() - 120, 272, 110, 18);
        if (ctl) { const int h = std::min (getHeight() - 20, ctl->preferredHeight()); ctl->setBounds (getWidth() / 2 + 8, std::max (4, getHeight() - h - 4), getWidth() / 2 - 12, h); ctl->toFront (false); }
        const int mx = getWidth() / 2 + 20;
        for (int i = 0; i < macros.size(); ++i) { macros[i]->setBounds (mx, 56 + i * 22, getWidth() - mx - 70, 20); renames[i]->setBounds (getWidth() - 62, 56 + i * 22, 54, 20); }
    }
    void paint (juce::Graphics& g) override
    {
        auto* t = ctx.selected();
        paintPanel (g, { 0, 0, getWidth() / 2, getHeight() }, t ? "modulators of " + t->name : "modulators");
        paintPanel (g, { getWidth() / 2 + 8, 0, getWidth() / 2 - 8, 260 }, "macros (the host sees these as parameters)");
        paintPanel (g, { getWidth() / 2 + 8, 270, getWidth() / 2 - 8, getHeight() - 270 }, "midi controllers and keys");
        g.setFont (uiFont (10.5f));
        if (t)
        {
            int y = 56;
            for (auto& m : *t->mods)
            {
                g.setColour (theme.ink.withAlpha (m.on ? 1.0f : 0.5f));
                juce::String line = juce::String (m.id) + "  " + modSrcName (m.src);
                if (m.src == MOD_LFO) line << " " << lfoShapeName (m.shape);
                line << "  " << (m.sync ? divisionNames()[juce::jlimit (0, 18, m.div)] : juce::String (m.hz, 2) + " hz");
                line << "  depth " << (m.depth > 0 ? "+" : "") << juce::roundToInt (m.depth * 100) << "%  " << polarityName (m.polarity);
                if (m.slew > 0.001f) line << "  slew";
                line << ui::U ("  →  ") << (m.device == -1 ? "rate of modulator " + juce::String (m.param + 1) : ctx.s.targetName ({ t->uid, m.device, m.param }));
                g.drawText (line, 8, y, getWidth() / 2 - 16, 18, juce::Justification::centredLeft, true);
                // a live trace of the source
                const int idx = (int) (&m - t->mods->data());
                if (idx < (int) t->modState.size()) { g.setColour (hotColour()); g.fillRect (getWidth() / 2 - 60, y + 14, (int) (t->modState[(size_t) idx].out * 50), 2); }
                y += 20;
            }
            if (t->mods->empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("none yet: + modulator, or right-click any control", 8, y, 400, 18, juce::Justification::centredLeft); }
        }
        int y = 300;
        for (auto& mm : *ctx.s.midiMaps) { g.setColour (theme.ink); g.drawText ("cc " + juce::String (mm.cc) + ui::U ("  →  ") + ctx.s.targetName (mm.target), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        if (ctx.s.learning.load()) { g.setColour (hotColour()); g.drawText ("learning: move a controller for " + ctx.s.targetName (ctx.s.learnTarget) + ui::U ("  (click here to cancel)"), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        for (auto& km : *ctx.s.keyMaps) { g.setColour (theme.ink); g.drawText ("key " + juce::KeyPress (km.key).getTextDescription() + (km.momentary ? " (held)" : " (toggle)") + ui::U ("  →  ") + ctx.s.targetName (km.target), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        if (ctx.s.keyLearning.load() != 0) { g.setColour (hotColour()); g.drawText ("press a key for " + ctx.s.targetName (ctx.s.keyLearnTarget) + ui::U ("  (click here to cancel)"), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); }
        g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (uiFont (9.5f));
        g.drawText ("right-click a map to remove it", getWidth() / 2 + 16, getHeight() - 22, 300, 16, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selected();
        if (e.x < getWidth() / 2 && t)
        {
            const int i = (e.y - 56) / 20;
            if (i >= 0 && i < (int) t->mods->size())
            {
                const int id = (*t->mods)[(size_t) i].id;
                auto* pop = new ModPopup (ctx.s, *t, id);
                juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (pop), juce::Rectangle<int> (getScreenX() + e.x, getScreenY() + e.y, 10, 10), nullptr);
            }
            return;
        }
        if (e.x > getWidth() / 2 && e.y >= 300)
        {
            if (ctx.s.learning.load()) { ctx.s.midiLearnCancel(); repaint(); return; }
            if (ctx.s.keyLearning.load() != 0) { ctx.s.keyLearning.store (0); repaint(); return; }
            const int i = (e.y - 300) / 20, nm = (int) ctx.s.midiMaps->size();
            if (e.mods.isPopupMenu() && i >= 0 && i < nm) ctx.s.editMidiMaps ([i] (std::vector<MidiMap>& v) { v.erase (v.begin() + i); });
            else if (e.mods.isPopupMenu() && i >= nm && i - nm < (int) ctx.s.keyMaps->size()) { const int k = i - nm; ctx.s.editKeyMaps ([k] (std::vector<KeyMap>& v) { v.erase (v.begin() + k); }); }
            repaint();
        }
    }
    void tick() override { repaint(); }
private:
public:
    std::unique_ptr<ControllerPanel> ctl; ui::TextLink ctlLink;
private:
    ControllerPanel* controllerPanel() { return ctl.get(); }
    ui::TextLink addMod; juce::OwnedArray<ui::ParamSlider> macros; juce::OwnedArray<ui::TextLink> renames;
};

// =================================================================================================
//  browse: devices, plug-in scan, files, templates
// =================================================================================================
class BrowsePage : public Page, private juce::Thread, public juce::DragAndDropContainer
{
public:
    explicit BrowsePage (Ctx& c) : Page (c), juce::Thread ("plugin scan"), scan ("scan plug-ins", theme), folder ("choose folder...", theme), newTemplate ("new from template", theme), up (".. up", theme), audition ("audition", theme)
    {
        for (auto* l : { &scan, &folder, &newTemplate, &up, &audition }) addAndMakeVisible (l);
        audition.setLit (true);
        audition.onClick = [this] { auditionOn = ! auditionOn; audition.setLit (auditionOn); if (! auditionOn) ctx.s.stopPreview(); };
        audition.setTooltip ("click a file to hear it through the main output; click it again to stop");
        scan.onClick = [this] { if (! isThreadRunning()) startThread(); };
        folder.onClick = [this] { chooser = std::make_unique<juce::FileChooser> ("samples folder", dir); chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) { dir = fc.getResult(); ctx.s.browseDir = dir; list(); } }); };
        up.onClick = [this] { dir = dir.getParentDirectory(); ctx.s.browseDir = dir; list(); };
        newTemplate.onClick = [this] { juce::PopupMenu m; auto n = Session::templateNames(); for (int i = 0; i < n.size(); ++i) m.addItem (i + 1, n[i]); m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newTemplate), [this] (int r) { if (r > 0) { ctx.s.newFromTemplate (r - 1); ctx.select (nullptr); ctx.showPage (0); } }); };
        dir = ctx.s.browseDir.isDirectory() ? ctx.s.browseDir : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        list();
    }
    ~BrowsePage() override { stopThread (5000); ctx.s.stopPreview(); }
    void visibilityChanged() override { if (! isVisible()) ctx.s.stopPreview(); }
    static bool isAudio (const juce::File& f) { return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"); }
    /** what a click on a file does */
    void clickFile (const juce::File& f)
    {
        if (! auditionOn || ! isAudio (f)) return;
        if (previewFile == f && ctx.s.previewing()) { ctx.s.stopPreview(); previewFile = juce::File(); }
        else { juce::String err; if (ctx.s.preview (f, &err)) previewFile = f; else ctx.status (err); }
        repaint();
    }
    void list() { files.clear(); for (auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false)) if (! f.getFileName().startsWith (".")) files.add (f); files.sort(); fileScroll = 0; repaint(); }
    void run() override
    {
        // every file in a child process: a plug-in that crashes is put on the blacklist, the studio carries on
        const int failed = ctx.s.scanPlugins (nullptr, [this] (const juce::String& now) { progress = now; return ! threadShouldExit(); });
        juce::MessageManager::callAsync ([this, failed] { progress = juce::String (ctx.s.known.getNumTypes()) + " plug-ins known" + (failed > 0 ? ", " + juce::String (failed) + " failed (blacklisted)" : juce::String()); repaint(); });
    }
    juce::Rectangle<int> devArea() const { return { 0, 0, getWidth() / 2 - 4, getHeight() }; }
    juce::Rectangle<int> fileArea() const { return { getWidth() / 2 + 4, 0, getWidth() / 2 - 4, getHeight() }; }
    void resized() override
    {
        scan.setBounds (8, 30, 110, 20); newTemplate.setBounds (124, 30, 140, 20);
        const auto fa = fileArea(); folder.setBounds (fa.getX() + 8, 30, 120, 20); up.setBounds (fa.getX() + 136, 30, 60, 20); audition.setBounds (fa.getX() + 204, 30, 80, 20);
    }
    void paint (juce::Graphics& g) override
    {
        paintPanel (g, devArea(), ui::U ("devices  (click: add to the selected track · right-click: as instrument / insert / on a return / on the master)"));
        paintPanel (g, fileArea(), ui::U ("files  (click: hear it · double-click: onto the selected track · drag onto the arrangement · double-click a folder)"));
        g.setFont (uiFont (10.5f));
        g.setColour (theme.ink.withAlpha (0.7f)); g.drawText (progress, 270, 32, devArea().getWidth() - 280, 16, juce::Justification::centredLeft, true);
        types = ctx.s.deviceTypes();
        int y = 58 - devScroll; juce::String cat;
        for (auto& e : types)
        {
            if (e.category != cat) { cat = e.category; g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (uiFont (10.5f, true)); g.drawText (cat, 8, y, 300, 18, juce::Justification::centredLeft); y += 20; }
            if (y > getHeight()) break;
            if (y > 50)
            {
                g.setColour (theme.ink); g.setFont (uiFont (10.5f));
                g.drawText ((e.instrument ? ui::U ("♪ ") : ui::U ("~ ")) + e.name + (e.typeId.startsWith ("st:") ? "  " + studioDeviceBlurb (e.typeId) : juce::String()), 20, y, devArea().getWidth() - 28, 18, juce::Justification::centredLeft, true);
            }
            y += 18;
        }
        const auto fa = fileArea();
        g.setColour (theme.ink.withAlpha (0.7f)); g.drawText (dir.getFullPathName(), fa.getX() + 8, 52, fa.getWidth() - 16, 16, juce::Justification::centredLeft, true);
        y = 72 - fileScroll;
        for (auto& f : files)
        {
            if (y > getHeight()) break;
            if (y > 60)
            {
                const bool playing = f == previewFile && ctx.s.previewing();
                g.setColour (playing ? hotColour() : theme.ink.withAlpha (f.isDirectory() ? 0.8f : 1.0f));
                g.drawText ((f.isDirectory() ? "/ " : playing ? "> " : "  ") + f.getFileName(), fa.getX() + 8, y, fa.getWidth() - 16, 18, juce::Justification::centredLeft, true);
            }
            y += 18;
        }
    }
    int typeAt (int py) const
    {
        int y = 58 - devScroll; juce::String cat;
        for (int i = 0; i < (int) types.size(); ++i) { if (types[(size_t) i].category != cat) { cat = types[(size_t) i].category; y += 20; } if (py >= y && py < y + 18) return i; y += 18; }
        return -1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (devArea().contains (e.getPosition()))
        {
            const int i = typeAt (e.y);
            if (i < 0) return;
            const auto t = types[(size_t) i];
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu m; m.addItem (1, "as the instrument of the selected track", t.instrument); m.addItem (2, "as an insert on the selected track");
                for (int r = 0; r < (int) ctx.s.returns.size(); ++r) m.addItem (10 + r, "on " + ctx.s.returns[(size_t) r]->name);
                m.addItem (3, "on the master"); m.addItem (4, "as a new track");
                m.showMenuAsync (juce::PopupMenu::Options(), [this, t] (int r) { place (t, r); });
            }
            else place (t, t.instrument ? 1 : 2);
            return;
        }
        if (fileArea().contains (e.getPosition()))
        {
            const int i = (e.y - 72 + fileScroll) / 18;
            if (i < 0 || i >= files.size()) return;
            const auto f = files[i];
            if (f.isDirectory()) { if (e.getNumberOfClicks() > 1) { dir = f; list(); } return; }
            dragFile = f;
            if (e.getNumberOfClicks() == 1) clickFile (f);
            if (e.getNumberOfClicks() > 1) { ctx.s.stopPreview(); if (auto* t = ctx.selected()) { if (t->isAudio()) { juce::String err; ctx.s.addAudioClip (*t, f, ctx.s.currentBar(), &err); ctx.status (err.isNotEmpty() ? err : "added to " + t->name); } else if (t->instrument) if (auto* h = dynamic_cast<SampleHolder*> (t->instrument->processor())) { h->setSampleFile (f); ctx.status ("sample loaded into " + t->name); } } }
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFile.existsAsFile() && e.getDistanceFromDragStart() > 8 && ! isDragAndDropActive()) startDragging ("file:" + dragFile.getFullPathName(), this);
    }
    void mouseUp (const juce::MouseEvent&) override { dragFile = juce::File(); }
    void place (const Session::TypeEntry& t, int where)
    {
        if (where <= 0) return;
        juce::String err;
        auto d = ctx.s.createDevice (t.typeId, &err);
        if (! d) { ctx.status (err); return; }
        auto* sel = ctx.selected();
        if (where == 4 || (where == 1 && ! sel)) { auto* nt = ctx.s.addTrack (t.name.upToFirstOccurrenceOf (" ", false, false)); if (! nt) return; if (d->isInstrument()) ctx.s.setInstrument (*nt, std::move (d)); else ctx.s.addInsert (*nt, std::move (d)); ctx.select (nt); }
        else if (where == 1 && sel) ctx.s.setInstrument (*sel, std::move (d));
        else if (where == 2 && sel) ctx.s.addInsert (*sel, std::move (d));
        else if (where == 3) ctx.s.addInsert (*ctx.s.master, std::move (d));
        else if (where >= 10 && where - 10 < (int) ctx.s.returns.size()) ctx.s.addInsert (*ctx.s.returns[(size_t) (where - 10)], std::move (d));
        ctx.status (t.name + " added");
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (devArea().contains (e.getPosition())) devScroll = std::max (0, devScroll - (int) (w.deltaY * 60)); else fileScroll = std::max (0, fileScroll - (int) (w.deltaY * 60));
        repaint();
    }
    void tick() override { if (previewFile != juce::File() && ! ctx.s.previewing()) { previewFile = juce::File(); repaint(); } }
private:
    ui::TextLink scan, folder, newTemplate, up, audition; bool auditionOn = true;
    juce::File dir, dragFile, previewFile; juce::Array<juce::File> files; std::vector<Session::TypeEntry> types;
    juce::String progress; int devScroll = 0, fileScroll = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace fal::daw

#pragma once
// faltung sndstudio — the pages: session (scenes), arrange (timeline + automation), seq (steps + devices),
// mix (strips, outputs, routes), mod (modulators, macros, midi), browse (devices, plug-ins, files, templates).
#include "StudioWidgets.h"
#include "PatternTools.h"
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
            if (ps >= 0) { g.setColour (juce::Colours::white); g.fillRect (r.getX() + (ps % 16) * (r.getWidth() / 16), r.getBottom() - 6, 4, 3); }
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
            g.setColour (playing ? juce::Colours::white : theme.ink.withAlpha (0.6f));
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
    explicit ArrangePage (Ctx& c) : Page (c), addLane ("+ lane", theme), addSong ("+ song entry", theme), snapLink ("snap", theme), zoomIn ("+", theme), zoomOut ("-", theme)
    {
        for (auto* l : { &addLane, &addSong, &snapLink, &zoomIn, &zoomOut }) addAndMakeVisible (l);
        addLane.onClick = [this] { laneMenu(); };
        addSong.onClick = [this] { ctx.s.editSong ([] (std::vector<SongEntry>& v) { v.push_back ({ (int) v.size() % kScenes, 8 }); }); repaint(); };
        snapLink.onClick = [this] { ctx.s.snap.store (! ctx.s.snap.load()); snapLink.setLit (ctx.s.snap.load()); };
        snapLink.setLit (ctx.s.snap.load());
        zoomIn.onClick = [this] { pxPerBar = std::min (200.0, pxPerBar * 1.4); repaint(); };
        zoomOut.onClick = [this] { pxPerBar = std::max (4.0, pxPerBar / 1.4); repaint(); };
    }
    double pxPerBar = 24.0; double scrollBar = 0; int scrollY = 0;
    const int left = 140, ruler = 26, laneH = 40, songW = 190, autoH = 216;
    int laneScroll = 0;                       // the first automation lane shown
    Track* touched = nullptr; int touchedClip = 0;   // the audio clip last clicked (its id): , and . nudge it
    /** move the clip last touched by a few milliseconds, off the grid (what the , and . keys do) */
    bool nudge (double ms)
    {
        if (! touched || ctx.s.indexOf (touched) < 0) return false;
        const double bars = ms * 0.001 / (60.0 / ctx.s.tempo() * 4.0); const int id = touchedClip; bool done = false;
        ctx.s.editAudioClips (*touched, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == id) { c.startBar = std::max (0.0, c.startBar + bars); done = true; } });
        repaint(); return done;
    }
    int x (double bar) const { return left + (int) ((bar - scrollBar) * pxPerBar); }
    double bar (int px) const { return (px - left) / pxPerBar + scrollBar; }
    double snapBar (double b) const { if (! ctx.s.snap.load()) return std::max (0.0, b); const double q = pxPerBar > 60 ? 0.25 : 1.0; return std::max (0.0, std::round (b / q) * q); }
    juce::Rectangle<int> lanesArea() const { return { 0, ruler, getWidth() - songW, getHeight() - ruler - autoH }; }
    juce::Rectangle<int> autoArea() const { return { 0, getHeight() - autoH, getWidth() - songW, autoH }; }
    juce::Rectangle<int> songArea() const { return { getWidth() - songW, 0, songW, getHeight() }; }
    void resized() override
    {
        auto sa = songArea();
        addSong.setBounds (sa.getX() + 8, sa.getY() + 6, 100, 20);
        snapLink.setBounds (8, 4, 44, 18); zoomOut.setBounds (56, 4, 18, 18); zoomIn.setBounds (76, 4, 18, 18);
        addLane.setBounds (8, autoArea().getY() + 4, 60, 18);
    }
    std::vector<Track*> rows() const
    {
        std::vector<Track*> v; for (auto& t : ctx.s.tracks) v.push_back (t.get()); return v;
    }
    void paint (juce::Graphics& g) override
    {
        auto& s = ctx.s;
        const auto la = lanesArea();
        g.setFont (uiFont (10.0f));
        // ruler
        paintSong (g);
        juce::Graphics::ScopedSaveState clipAll (g);
        g.reduceClipRegion (0, 0, la.getWidth(), getHeight());
        const double b0 = scrollBar, b1 = bar (la.getRight());
        const int step = pxPerBar >= 40 ? 1 : pxPerBar >= 12 ? 4 : 16;
        for (int b = (int) std::floor (b0 / step) * step; b <= (int) b1 + step; b += step)
        {
            const int px = x (b);
            if (px < left) continue;
            g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (px, ruler - 6, 1, la.getHeight() + 6);
            g.setColour (theme.ink); g.drawText (juce::String (b + 1), px + 3, 4, 40, 16, juce::Justification::centredLeft);
        }
        // loop and markers on the ruler
        if (s.loopOn.load()) { g.setColour (theme.fill.withAlpha (0.7f)); g.fillRect (x (s.loopStart.load()), ruler - 4, std::max (2, x (s.loopEnd.load()) - x (s.loopStart.load())), 3); }
        for (auto& m : *s.markers) { const int px = x (m.bar); if (px < left) continue; g.setColour (juce::Colour (0xffee8e6e)); g.fillRect (px, 0, 1, ruler); g.setFont (uiFont (9.0f)); g.drawText (m.name, px + 3, ruler - 12, 90, 11, juce::Justification::centredLeft, true); }
        // lanes
        auto rs = rows();
        g.setFont (uiFont (11.0f));
        for (int i = 0; i < (int) rs.size(); ++i)
        {
            auto& t = *rs[(size_t) i];
            const int y = la.getY() + i * laneH - scrollY;
            if (y + laneH < la.getY() || y > la.getBottom()) continue;
            const bool sel = ctx.selected() == &t;
            g.setColour (theme.ink.withAlpha (sel ? 0.12f : 0.05f)); g.fillRect (0, y, la.getWidth(), laneH - 1);
            g.setColour (trackColour (t)); g.fillRect (0, y, 4, laneH - 1);
            g.setColour (theme.ink); g.setFont (uiFont (11.0f, sel)); g.drawText (t.name, 10, y + 2, left - 14, 16, juce::Justification::centredLeft, true);
            g.setFont (uiFont (9.0f)); g.setColour (theme.ink.withAlpha (0.7f));
            g.drawText (t.isAudio() ? "audio" : (t.instrument ? t.instrument->name : ""), 10, y + 18, left - 14, 14, juce::Justification::centredLeft, true);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (left, la.getY(), la.getWidth() - left, la.getHeight());
            for (auto& c : *t.clips)
            {
                auto r = juce::Rectangle<float> ((float) x (c.startBar), (float) y + 4.0f, (float) (c.bars * pxPerBar) - 2.0f, (float) laneH - 10.0f);
                if (r.getRight() < left || r.getX() > la.getRight()) continue;
                const bool live = s.mode.load() == (int) PlayMode::Arrange && t.shownPattern.load() == c.pattern && s.currentBar() >= c.startBar && s.currentBar() < c.startBar + c.bars && s.playPpq.load() >= 0;
                g.setColour (trackColour (t).withAlpha (live ? 1.0f : 0.6f));
                if (theme.mirror) g.fillRect (r); else g.fillPath (ui::skewRect (r, 0.15f));
                g.setColour (ui::inkFor (trackColour (t))); g.setFont (uiFont (10.0f));
                g.drawText ("p" + juce::String (c.pattern + 1), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
            }
            for (auto& c : *t.aclips)
            {
                auto r = juce::Rectangle<float> ((float) x (c.startBar), (float) y + 3.0f, (float) (c.bars * pxPerBar) - 2.0f, (float) laneH - 8.0f);
                if (r.getRight() < left || r.getX() > la.getRight()) continue;
                g.setColour (trackColour (t).withAlpha (0.35f)); g.fillRect (r);
                g.setColour (trackColour (t)); g.drawRect (r, 1.0f);
                drawWave (g, c, r, ui::inkFor (theme.ground));
                g.setColour (theme.ink); g.setFont (uiFont (9.0f));
                g.drawText (juce::File (c.path).getFileNameWithoutExtension(), r.reduced (3.0f, 0.0f).withHeight (12.0f), juce::Justification::centredLeft, true);
            }
        }
        // playhead
        const double cb = s.currentBar();
        if (cb >= b0) { g.setColour (juce::Colours::white); g.fillRect (x (cb), 0, 1, la.getBottom()); }
        paintLanes (g);
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (left - 1, 0, 1, la.getBottom());
        g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (uiFont (9.5f));
        if (ctx.status) ctx.status (ui::U ("arrange: drag clips · edge: length · alt-drag: copy · double-click a lane: add the track's pattern · drop audio files here · right-click a clip · , and . nudge the clip last touched by 5 ms · ruler drag: loop · shift-click ruler: marker"));
    }
    void drawWave (juce::Graphics& g, const AudioClip& c, juce::Rectangle<float> r, juce::Colour ink)
    {
        if (! c.sample) return;
        const auto& d = c.sample->data; const int n = d.getNumSamples();
        if (n < 2) return;
        const double srcLen = c.mode == 0 ? std::min ((double) n, (c.bars * 4.0 * 60.0 / ctx.s.tempo()) * c.sample->rate * std::exp2 (c.pitch / 12.0)) : c.sourceSeconds() * c.sample->rate;
        const int px = (int) r.getWidth();
        g.setColour (ink.withAlpha (0.8f));
        const float mid = r.getCentreY(), hh = r.getHeight() * 0.45f;
        for (int i = 0; i < px; i += 2)
        {
            const int a = (int) (c.offset * c.sample->rate + srcLen * i / std::max (1, px)), b = (int) (c.offset * c.sample->rate + srcLen * (i + 2) / std::max (1, px));
            if (a >= n) break;
            float lo = 0, hi = 0;
            for (int k = a; k < std::min (n, std::max (a + 1, b)); k += std::max (1, (b - a) / 64)) { const float v = d.getSample (0, k); lo = std::min (lo, v); hi = std::max (hi, v); }
            g.fillRect (r.getX() + i, mid - hi * hh, 2.0f, std::max (1.0f, (hi - lo) * hh));
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
            juce::Path p;
            const double bStart = scrollBar, bEnd = bar (aa.getWidth());
            for (int px = left; px < aa.getWidth(); px += 2)
            {
                const double b = bar (px);
                const float v = l->valueAt (b * 4.0);
                const float yy = (float) y + (float) (lh - 6) - v * (float) (lh - 10);
                if (px == left) p.startNewSubPath ((float) px, yy); else p.lineTo ((float) px, yy);
            }
            g.setColour (juce::Colour (0xffee8e6e)); g.strokePath (p, juce::PathStrokeType (1.5f));
            for (auto& pt : l->pts)
            {
                const double b = pt.beat / 4.0;
                if (b < bStart || b > bEnd) continue;
                const float px = (float) x (b), yy = (float) y + (float) (lh - 6) - pt.value * (float) (lh - 10);
                g.setColour (juce::Colours::white); g.fillEllipse (px - 3.0f, yy - 3.0f, 6.0f, 6.0f);
            }
        }
        if (targets.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (ui::U ("automation: right-click any control → automate it on the timeline, or + lane · click: a point · shift-drag: draw freehand · alt-drag a point: its curve"), left + 8, aa.getY() + 6, aa.getWidth() - left - 16, 16, juce::Justification::centredLeft, true); }
    }
    void laneMenu()
    {
        auto* t = ctx.selected();
        if (! t) return;
        juce::PopupMenu m;
        int id = 1; std::vector<Target> list;
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
        g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (juce::String (b) + " bars  " + juce::String (b * 4.0 * 60.0 / s.tempo() / 60.0, 1) + " min", sa.getX() + 8, sa.getBottom() - 40, sa.getWidth() - 16, 16, juce::Justification::centredLeft);
        g.drawText (ui::U ("click: scene · drag: bars · right-click: remove"), sa.getX() + 8, sa.getBottom() - 22, sa.getWidth() - 16, 16, juce::Justification::centredLeft, true);
    }
    // ---- mouse --------------------------------------------------------------------------------------
    struct Hit { Track* t = nullptr; int clip = -1; bool audio = false; bool edge = false; };
    Hit hitClip (juce::Point<int> p)
    {
        Hit h; const auto la = lanesArea();
        if (! la.contains (p) || p.x < left) return h;
        auto rs = rows();
        const int row = (p.y - la.getY() + scrollY) / laneH;
        if (row < 0 || row >= (int) rs.size()) return h;
        h.t = rs[(size_t) row];
        const double b = bar (p.x);
        for (int i = 0; i < (int) h.t->clips->size(); ++i) { auto& c = (*h.t->clips)[(size_t) i]; if (b >= c.startBar && b < c.startBar + c.bars) { h.clip = i; h.edge = x (c.startBar + c.bars) - p.x < 6; return h; } }
        for (int i = 0; i < (int) h.t->aclips->size(); ++i) { auto& c = (*h.t->aclips)[(size_t) i]; if (b >= c.startBar && b < c.startBar + c.bars) { h.clip = i; h.audio = true; h.edge = x (c.startBar + c.bars) - p.x < 6; return h; } }
        return h;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        const auto la = lanesArea(), aa = autoArea(), sa = songArea();
        drag = Hit(); dragLane = -1;
        if (sa.contains (e.getPosition()))
        {
            const int i = (e.y - sa.getY() - 50) / 20;
            if (i >= 0 && i < (int) s.song->size())
            {
                if (e.mods.isPopupMenu()) { s.editSong ([i] (std::vector<SongEntry>& v) { v.erase (v.begin() + i); }); repaint(); return; }
                songDrag = i; songStart = (*s.song)[(size_t) i].bars;
                if (e.getNumberOfClicks() > 1 || e.mods.isShiftDown())
                {
                    juce::PopupMenu m; for (int k = 0; k < (int) s.scenes->size() && k < 32; ++k) m.addItem (k + 1, juce::String (k + 1) + " " + (*s.scenes)[(size_t) k].name);
                    m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r) { if (r > 0) ctx.s.editSong ([i, r] (std::vector<SongEntry>& v) { v[(size_t) i].scene = r - 1; }); repaint(); });
                }
            }
            return;
        }
        if (e.y < ruler && e.x >= left)
        {
            if (e.mods.isShiftDown()) { const double b = snapBar (bar (e.x)); s.editMarkers ([b] (std::vector<Marker>& m) { m.push_back ({ "marker " + juce::String (m.size() + 1), b }); }); repaint(); return; }
            if (e.mods.isPopupMenu()) { const double b = bar (e.x); s.editMarkers ([b] (std::vector<Marker>& m) { m.erase (std::remove_if (m.begin(), m.end(), [b] (const Marker& x) { return std::abs (x.bar - b) < 0.5; }), m.end()); }); repaint(); return; }
            loopDrag = true; loopA = snapBar (bar (e.x)); s.loopStart.store (loopA); s.loopEnd.store (loopA + 1); s.loopOn.store (true);
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
                freehand = true; lastFreeBeat = bar (e.x) * 4.0;
                const double fb = std::max (0.0, lastFreeBeat); const float fv = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
                s.editLane (laneTarget, [fb, fv] (AutoLane& ll) { ll.pts.erase (std::remove_if (ll.pts.begin(), ll.pts.end(), [fb] (const AutoPoint& p) { return std::abs (p.beat - fb) < 0.02; }), ll.pts.end()); ll.pts.push_back ({ fb, fv, 0.0f }); });
                repaint(); return;
            }
            const double beat = bar (e.x) * 4.0; const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
            auto l = s.lane (laneTarget);
            int near = -1; double best = 0.3;
            for (int k = 0; l && k < (int) l->pts.size(); ++k) { const double d = std::abs (l->pts[(size_t) k].beat - beat) / 4.0 * pxPerBar; if (d < 6 && d < best * pxPerBar) { near = k; best = d / pxPerBar; } }
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) { if (near >= 0) s.editLane (laneTarget, [near] (AutoLane& ll) { ll.pts.erase (ll.pts.begin() + near); }); repaint(); return; }
            if (near >= 0) { dragPoint = near; curveDrag = e.mods.isAltDown(); }
            else { const double nb = s.snap.load() ? snapBar (beat / 4.0) * 4.0 : beat; s.editLane (laneTarget, [nb, v] (AutoLane& ll) { ll.pts.push_back ({ nb, v, 0.0f }); }); l = s.lane (laneTarget); dragPoint = -1; for (int k = 0; k < (int) l->pts.size(); ++k) if (std::abs (l->pts[(size_t) k].beat - nb) < 1.0e-9) dragPoint = k; curveDrag = false; }
            repaint();
            return;
        }
        if (! la.contains (e.getPosition())) return;
        if (e.x < left)
        {
            auto rs = rows(); const int row = (e.y - la.getY() + scrollY) / laneH;
            if (row >= 0 && row < (int) rs.size()) ctx.select (rs[(size_t) row]);
            repaint(); return;
        }
        auto h = hitClip (e.getPosition());
        if (h.t) ctx.select (h.t);
        if (h.clip < 0)
        {
            if (h.t && e.getNumberOfClicks() > 1 && ! h.t->isAudio()) { const double b = snapBar (bar (e.x)); const int p = h.t->pattern.load(); s.editClips (*h.t, [b, p] (std::vector<Clip>& v) { v.push_back ({ p, b, 4.0 }); }); }
            else if (! e.mods.isPopupMenu()) { s.locate (snapBar (bar (e.x))); }
            repaint(); return;
        }
        if (e.mods.isPopupMenu()) { clipMenu (h); return; }
        drag = h; dragStartBar = bar (e.x);
        if (h.audio) { const auto& c = (*h.t->aclips)[(size_t) h.clip]; dragClipStart = c.startBar; dragClipLen = c.bars; touched = h.t; touchedClip = c.id; if (e.getNumberOfClicks() > 1) { clipEditor (h); drag = Hit(); } }
        else { const auto& c = (*h.t->clips)[(size_t) h.clip]; dragClipStart = c.startBar; dragClipLen = c.bars; }
        if (e.mods.isAltDown() && h.t)
        {
            // duplicate, then drag the copy
            if (h.audio) { s.editAudioClips (*h.t, [&] (std::vector<AudioClip>& v) { AudioClip c = v[(size_t) h.clip]; c.id = 0; v.push_back (c); }); drag.clip = (int) h.t->aclips->size() - 1; }
            else { s.editClips (*h.t, [&] (std::vector<Clip>& v) { v.push_back (v[(size_t) h.clip]); }); drag.clip = (int) h.t->clips->size() - 1; }
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& s = ctx.s;
        if (loopDrag) { const double b = snapBar (bar (e.x)); s.loopStart.store (std::min (loopA, b)); s.loopEnd.store (std::max (loopA + (s.snap.load() ? 1.0 : 0.25), std::max (loopA, b))); repaint(); return; }
        if (songDrag >= 0) { const int d = e.getDistanceFromDragStartX() / 12; const int i = songDrag; const int nb = std::max (1, songStart + d); s.editSong ([i, nb] (std::vector<SongEntry>& v) { if (i < (int) v.size()) v[(size_t) i].bars = nb; }); repaint(); return; }
        if (dragLane >= 0 && freehand)
        {
            const int y = autoArea().getY() + 26 + dragLane * 52;
            const double b1 = std::max (0.0, bar (e.x) * 4.0); const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
            const double lo = std::min (lastFreeBeat, b1), hi = std::max (lastFreeBeat, b1);
            if (hi - lo < 4.0 * 3.0 / pxPerBar) return;   // a point every three pixels
            s.editLane (laneTarget, [=] (AutoLane& l) { l.pts.erase (std::remove_if (l.pts.begin(), l.pts.end(), [=] (const AutoPoint& p) { return p.beat > lo + 1.0e-6 && p.beat <= hi + 1.0e-6; }), l.pts.end()); l.pts.push_back ({ b1, v, 0.0f }); });
            lastFreeBeat = b1; repaint(); return;
        }
        if (dragLane >= 0 && dragPoint >= 0)
        {
            const int y = autoArea().getY() + 26 + dragLane * 52;
            const double beat = (s.snap.load() ? snapBar (bar (e.x)) : bar (e.x)) * 4.0; const float v = juce::jlimit (0.0f, 1.0f, (float) (y + 46 - e.y) / 42.0f);
            const int k = dragPoint; const bool cv = curveDrag; const float dy = (float) e.getDistanceFromDragStartY();
            s.editLane (laneTarget, [=] (AutoLane& l) { if (k < (int) l.pts.size()) { if (cv) l.pts[(size_t) k].curve = juce::jlimit (-1.0f, 1.0f, -dy / 40.0f); else { l.pts[(size_t) k].beat = std::max (0.0, beat); l.pts[(size_t) k].value = v; } } });
            repaint(); return;
        }
        if (! drag.t || drag.clip < 0) return;
        const double d = bar (e.x) - dragStartBar;
        Track* t = drag.t; const int ci = drag.clip; const bool edge = drag.edge;
        const double start = dragClipStart, len = dragClipLen;
        if (drag.audio) s.editAudioClips (*t, [&] (std::vector<AudioClip>& v) { if (ci < (int) v.size()) { if (edge) v[(size_t) ci].bars = std::max (0.05, snapBar (start + len + d) - start); else v[(size_t) ci].startBar = snapBar (start + d); } });
        else s.editClips (*t, [&] (std::vector<Clip>& v) { if (ci < (int) v.size()) { if (edge) v[(size_t) ci].bars = std::max (0.25, snapBar (start + len + d) - start); else v[(size_t) ci].startBar = snapBar (start + d); } });
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { loopDrag = false; songDrag = -1; drag = Hit(); dragLane = -1; dragPoint = -1; freehand = false; }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (autoArea().contains (e.getPosition())) { laneScroll = std::max (0, laneScroll + (w.deltaY < 0 ? 1 : w.deltaY > 0 ? -1 : 0)); repaint(); return; }
        if (e.mods.isCommandDown()) { pxPerBar = juce::jlimit (4.0, 200.0, pxPerBar * (1.0 + w.deltaY)); }
        else { scrollBar = std::max (0.0, scrollBar - w.deltaX * 8.0 / pxPerBar * 10.0); scrollY = juce::jlimit (0, std::max (0, (int) ctx.s.tracks.size() * laneH - lanesArea().getHeight()), scrollY - (int) (w.deltaY * 60)); }
        repaint();
    }
    void clipMenu (Hit h)
    {
        juce::PopupMenu m;
        if (h.audio) { m.addItem (1, "edit clip (gain, fades, pitch, stretch)"); m.addItem (2, "split at the playhead"); m.addItem (3, "reverse"); m.addItem (4, "remove"); }
        else { juce::PopupMenu sub; for (int k = 0; k < kPatterns; ++k) sub.addItem (100 + k, "pattern " + juce::String (k + 1)); m.addSubMenu ("pattern", sub); m.addItem (4, "remove"); }
        m.showMenuAsync (juce::PopupMenu::Options(), [this, h] (int r)
        {
            auto& s = ctx.s; const int ci = h.clip;
            if (r == 0 || ! h.t) return;
            if (h.audio)
            {
                if (r == 1) clipEditor (h);
                else if (r == 2) { const int id = (*h.t->aclips)[(size_t) ci].id; s.splitClip (*h.t, id, s.currentBar()); }
                else if (r == 3) s.editAudioClips (*h.t, [ci] (std::vector<AudioClip>& v) { if (ci < (int) v.size()) v[(size_t) ci].reverse = ! v[(size_t) ci].reverse; });
                else if (r == 4) s.editAudioClips (*h.t, [ci] (std::vector<AudioClip>& v) { if (ci < (int) v.size()) v.erase (v.begin() + ci); });
            }
            else
            {
                if (r >= 100) s.editClips (*h.t, [ci, r] (std::vector<Clip>& v) { if (ci < (int) v.size()) v[(size_t) ci].pattern = r - 100; });
                else if (r == 4) s.editClips (*h.t, [ci] (std::vector<Clip>& v) { if (ci < (int) v.size()) v.erase (v.begin() + ci); });
            }
            repaint();
        });
    }
    void clipEditor (Hit h)
    {
        if (! h.t || h.clip < 0 || h.clip >= (int) h.t->aclips->size()) return;
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
                add (mode = new ValueRow ("mode", { "repitch", "stretch", "warp to tempo" }, c->mode));
                add (stretch = new ValueRow ("stretch", 0.25f, 4.0f, (float) c->stretch, [] (float v) { return "x" + juce::String (v, 2); }));
                add (offset = new ValueRow ("start in file", 0.0f, (float) std::max (0.01, c->sample ? c->sample->seconds() : 1.0), (float) c->offset, [] (float v) { return juce::String (v, 2) + " s"; }));
                add (rev = new ValueRow ("reverse", { "off", "on" }, c->reverse ? 1 : 0)); rev->boolStyle = true;
                add (loop = new ValueRow ("loop", { "off", "on" }, c->loop ? 1 : 0)); loop->boolStyle = true;
                for (auto* r : rows) r->onChange = [this] (float) { push(); };
                setSize (320, 24 + (int) rows.size() * 20 + 8);
            }
            void push()
            {
                session.editAudioClips (track, [this] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == clipId) { c.gain = juce::Decibels::decibelsToGain (gain->get(), -48.0f); c.fadeIn = fin->get(); c.fadeOut = fout->get(); c.pitch = pitch->get(); c.mode = mode->getInt(); c.stretch = stretch->get(); c.offset = offset->get(); c.reverse = rev->getInt() > 0; c.loop = loop->getInt() > 0; } });
            }
            void add (ValueRow* r) { rows.push_back (r); owned.add (r); addAndMakeVisible (r); }
            void resized() override { int y = 22; for (auto* r : rows) { r->setBounds (8, y, getWidth() - 16, 18); y += 20; } }
            void paint (juce::Graphics& g) override { g.fillAll (studioTheme().ground.brighter (0.12f)); g.setColour (studioTheme().ink); g.setFont (uiFont (11.0f, true)); g.drawText ("clip", 8, 4, 200, 16, juce::Justification::centredLeft); }
            Session& session; Track& track; int clipId; std::vector<ValueRow*> rows; juce::OwnedArray<ValueRow> owned;
            ValueRow *gain = nullptr, *fin = nullptr, *fout = nullptr, *pitch = nullptr, *mode = nullptr, *stretch = nullptr, *offset = nullptr, *rev = nullptr, *loop = nullptr;
        };
        const int id = (*h.t->aclips)[(size_t) h.clip].id;
        auto* ed = new Ed (ctx.s, *h.t, id);
        juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (ed), juce::Rectangle<int> (getScreenX() + x ((*h.t->aclips)[(size_t) h.clip].startBar), getScreenY() + lanesArea().getY(), 10, 10), nullptr);
    }
    // ---- drops --------------------------------------------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true; return false; }
    void filesDropped (const juce::StringArray& files, int px, int py) override { for (auto& f : files) dropFile (juce::File (f), px, py); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("file:"); }
    void itemDropped (const SourceDetails& d) override { dropFile (juce::File (d.description.toString().substring (5)), d.localPosition.x, d.localPosition.y); }
    void dropFile (const juce::File& f, int px, int py)
    {
        auto& s = ctx.s;
        const auto la = lanesArea();
        auto rs = rows();
        const int row = (py - la.getY() + scrollY) / laneH;
        Track* t = (la.contains (px, py) && row >= 0 && row < (int) rs.size()) ? rs[(size_t) row] : nullptr;
        if (t && ! t->isAudio() && t->instrument)
        {
            // onto an instrument track: a sampler device takes the file
            if (auto* h = dynamic_cast<SampleHolder*> (t->instrument->processor())) { h->setSampleFile (f); ctx.status ("sample loaded into " + t->name); repaint(); return; }
            t = nullptr;
        }
        if (! t) { t = s.addTrack (f.getFileNameWithoutExtension(), -1, TrackKind::Audio); if (! t) return; }
        const double b = px >= left ? snapBar (bar (px)) : s.currentBar();
        juce::String err;
        s.addAudioClip (*t, f, b, &err);
        if (err.isNotEmpty()) ctx.status (err); else ctx.status (f.getFileName() + " on " + t->name);
        ctx.select (t);
        repaint();
    }
    void tick() override { repaint(); }
private:
    ui::TextLink addLane, addSong, snapLink, zoomIn, zoomOut;
    Hit drag; double dragStartBar = 0, dragClipStart = 0, dragClipLen = 0;
    bool loopDrag = false; double loopA = 0; int songDrag = -1, songStart = 0;
    int dragLane = -1, dragPoint = -1; bool curveDrag = false, freehand = false; double lastFreeBeat = 0; Target laneTarget;
};

// =================================================================================================
//  seq: the step grid, the step, the pattern tools, the device chain
// =================================================================================================
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
        rChoke->setTooltip ("a hit on one track of a group cuts the other tracks of that group (closed hat / open hat, chopped samples)");
        // the step
        row (sVel, new ValueRow ("velocity", 0.0f, 1.0f, 0.8f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (sPitch, new ValueRow ("pitch", -36.0f, 36.0f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }));
        row (sProb, new ValueRow ("chance", 0.0f, 1.0f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        row (sRatchet, new ValueRow ("ratchet", 1.0f, 8.0f, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v)); }));
        row (sMicro, new ValueRow ("micro", -1.0f, 1.0f, 0.0f, [this] (float v) { return (v > 0 ? "+" : "") + juce::String (v * ctx.s.microMs.load(), 1) + " ms"; }));
        row (sLen, new ValueRow ("length", 0.05f, 16.0f, 0.5f, [] (float v) { return juce::String (v, 2) + " steps"; }));
        row (sDur, new ValueRow ("step time", 0.25f, 4.0f, 1.0f, [] (float v) { return "x" + juce::String (v, 2); }));
        sDur->setTooltip ("how long this step lasts, as a ratio of the track's step: uneven grids (a triplet next to a straight step). The pattern's loop is the sum of its steps");
        juce::StringArray conds; for (int i = 0; i < (int) Cond::Count; ++i) conds.add (condName ((Cond) i));
        row (sCond, new ValueRow ("condition", conds, 0));
        row (sSlide, new ValueRow ("slide", { "off", "on" }, 0)); sSlide->boolStyle = true;
        row (sSlice, new ValueRow ("slice", -1.0f, 63.0f, -1.0f, [] (float v) { return v < 0 ? juce::String ("note") : juce::String (juce::roundToInt (v) + 1); }));
        { juce::StringArray ch; for (int i = 0; i < kChords; ++i) ch.add (chordName (i)); row (sChord, new ValueRow ("chord", ch, 0)); }
        row (sRCurve, new ValueRow ("ratchet curve", -1.0f, 1.0f, 0.0f, [] (float v) { return std::abs (v) < 0.02f ? juce::String ("even") : (v > 0 ? "speeds up " : "slows ") + juce::String (juce::roundToInt (std::abs (v) * 100)) + "%"; }));
        sRCurve->setLabelWidth (90);
        for (int k = 0; k < kLocks; ++k) { row (sLock[k], new ValueRow ("lock " + juce::String (k + 1), 0.0f, 1.0f, 0.0f, [] (float v) { return juce::String (v, 2); })); sLock[k]->setLabelWidth (150); }
        wire();
        viewport.setViewedComponent (&chain, false); viewport.setScrollBarsShown (true, false); addAndMakeVisible (viewport);
    }
    ValueRow *rPattern, *rLength, *rSpeed, *rSwing, *rShift, *rProb, *rRoot, *rFree, *rStepMs, *rChain, *rQuant, *rEvolve, *rChoke;
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
        for (auto* r : { rPattern, rLength, rSpeed, rSwing, rShift, rProb, rRoot, rFree, rStepMs, rChain, rQuant, rEvolve, rChoke }) { r->setBounds (8, y, 210, 18); y += 20; }
        const int gx = 236, gw = w - gx - 8;
        int x = gx;
        for (auto* l : { tEuclid, tRotL, tRotR, tRev, tMutate, tHuman, tRandom, tMarkov, tRules, tDouble, tClear, tCopy, tPaste }) { const int lw = (int) juce::GlyphArrangement::getStringWidth (uiFont (11.0f), l->text) + 16; l->setBounds (x, 196, lw, 18); x += lw + 2; }
        x = gx;
        for (auto* l : { tRatios, tAccel, tEven, tStrict, tSieve, tSievePitch, tCloud, tFreeze }) { const int lw = (int) juce::GlyphArrangement::getStringWidth (uiFont (11.0f), l->text) + 16; l->setBounds (x, 214, lw, 18); x += lw + 2; }
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
    juce::Rectangle<float> cell (int i) const { const int gx = 236; const float cw = (float) (getWidth() - gx - 28) / 16.0f; return { gx + (i % 16) * cw + 2.0f, 60.0f + (i / 16) * 0.0f, cw - 4.0f, 118.0f }; }
    void paint (juce::Graphics& g) override
    {
        auto* t = ctx.selected();
        g.setFont (uiFont (11.0f, true)); g.setColour (theme.ink);
        g.drawText (t ? t->name + "  ·  " + (t->isAudio() ? "audio track" : (t->instrument ? t->instrument->name : "no instrument")) : "no track selected", 8, 6, 400, 18, juce::Justification::centredLeft, true);
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
                if (st.slice >= 0) info << "s" << (st.slice + 1) << "\n";
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
            if (i == selectedStep) { g.setColour (juce::Colours::white); g.strokePath (ui::skewRect (c.expanded (1.0f), 0.12f), juce::PathStrokeType (1.5f)); }
            if (i == playing) { g.setColour (juce::Colours::white); g.fillRect (c.getX(), c.getBottom() + 2.0f, c.getWidth(), 2.0f); }
        }
        g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (uiFont (9.5f));
        g.drawText (ui::U ("click: on / off and select · drag up / down: velocity · drag across: paint · shift-drag: micro · alt-click: chance · right-click: ratchet · right-click a slider below: map an lfo / random / gate, automate, midi, key, macro"), 236, 182, getWidth() - 244, 14, juce::Justification::centredLeft, true);
        // the evolving copy: steps that have drifted from what is written carry a mark
        if (t->evolve.load() > 0.0005f)
            for (int k = 0; k < 16; ++k) { const int i = page * 16 + k; if (i < len && (t->flip[(size_t) i] != 0 || t->drift[(size_t) i] != 0.0f)) { g.setColour (juce::Colour (0xffee8e6e)); g.fillRect (cell (k).getX() + 14.0f, 58.0f - 6.0f, 10.0f, 3.0f); } }
    }
    int stepAt (juce::Point<float> p) const { for (int k = 0; k < 16; ++k) if (cell (k).expanded (2.0f).contains (p)) return page * 16 + k; return -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* t = ctx.selected(); if (! t) return;
        if (e.y >= 34 && e.y < 50 && e.x >= 236) { const int k = (e.x - 236) / 60; auto p = t->patternAt (t->pattern.load()); const int pages = ((p ? p->length : 16) + 15) / 16; if (k >= 0 && k < pages) { page = k; repaint(); } return; }
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
    ui::TextLink *tRatios, *tAccel, *tEven, *tStrict, *tSieve, *tSievePitch, *tCloud, *tFreeze;
    juce::Component chain; juce::Viewport viewport;
    Pattern clip; Rng rng;
    enum Mode { None, Pending, Vertical, Paint, Micro } mode = None;
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
        g.setColour (active ? juce::Colour (0xffee8e6e) : t.ink);
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
        else { bounce.setBounds (in, y, cw, 20); y += 22; autoMaster.setBounds (in, y, cw, 20); y += 22; }
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
            g.setColour (juce::Colour (0xffee8e6e)); g.drawText (ui::U ("→ ") + (d ? d->name : juce::String ("?")) + "  " + juce::String (r.channels) + " ch" + (r.feedback ? " fb" : ""), 6, y, getWidth() - 12, 13, juce::Justification::centredLeft, true);
            y += 13;
        }
        for (int r = 0; r < sends.size() && r < (int) ctx.s.returns.size(); ++r)
            if (track.sendPre[(size_t) r].load()) { g.setColour (theme.ink.withAlpha (0.7f)); g.drawText ("pre", sends[r]->getRight() - 60, sends[r]->getY() - 1, 24, 10, juce::Justification::centredLeft); }
        g.setColour (theme.ink.withAlpha (0.2f)); g.fillRect (getWidth() - 1, 0, 1, getHeight());
        if (dropTarget) { g.setColour (juce::Colour (0xffee8e6e)); g.drawRect (getLocalBounds().reduced (1), 2); }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.y < 40) ctx.select (&track); }
    void tick() { meter.tick(); repaint(); }
    Ctx& ctx; Track& track; ui::Theme theme; bool dropTarget = false;
    Meter meter; std::unique_ptr<BarSlider> level, pan; juce::OwnedArray<BarSlider> sends;
    ui::TextLink mute, solo, arm, out, width, toMaster, input, key, bounce, autoMaster;
    int routesY = 0;
};

class MixPage : public Page
{
public:
    explicit MixPage (Ctx& c) : Page (c), mode ("produce", theme), assign ("assign all outputs", theme), bypass ("master chain bypass", theme)
    {
        for (auto* l : { &mode, &assign, &bypass }) addAndMakeVisible (l);
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
        resized();
    }
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
        mode.setBounds (8, 6, 90, 20); assign.setBounds (104, 6, 150, 20); bypass.setBounds (260, 6, 160, 20);
        viewport.setBounds (0, 32, getWidth(), getHeight() - 58);
        const int n = std::max (1, strips.size());
        const int sw = juce::jlimit (118, 150, (getWidth() - 2) / n); int x = 0;
        for (auto* s : strips) { s->setBounds (x, 0, sw, viewport.getHeight() - 12); x += sw; }
        row.setSize (std::max (x, 10), viewport.getHeight() - 12);
    }
    void paint (juce::Graphics& g) override
    {
        mode.setText (ctx.s.multiOut.load() ? "multi-out" : "produce"); mode.setLit (ctx.s.multiOut.load()); bypass.setLit (ctx.s.masterBypass.load());
        g.setColour (theme.ink.withAlpha (0.75f)); g.setFont (uiFont (10.0f));
        g.drawText (ui::U ("drag a strip's route icon onto another strip to route it there · width: the track's channels (1 - 64) · alt-right-click a send: pre / post fader"), 430, 8, getWidth() - 440, 16, juce::Justification::centredLeft, true);
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
    void tick() override { for (auto* s : strips) s->tick(); repaint (0, 0, getWidth(), 30); }
private:
    ui::TextLink mode, assign, bypass;
    juce::Component row; juce::Viewport viewport; juce::OwnedArray<Strip> strips;
    std::unique_ptr<RoutePopup> popup; Strip* dragFrom = nullptr; juce::Point<int> dragAt; bool closing = false;
};

// =================================================================================================
//  mod: modulators of the selected track, macros, midi maps
// =================================================================================================
class ModPage : public Page
{
public:
    explicit ModPage (Ctx& c) : Page (c), addMod ("+ modulator", theme)
    {
        addAndMakeVisible (addMod);
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
                if (idx < (int) t->modState.size()) { g.setColour (juce::Colour (0xffee8e6e)); g.fillRect (getWidth() / 2 - 60, y + 14, (int) (t->modState[(size_t) idx].out * 50), 2); }
                y += 20;
            }
            if (t->mods->empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("none yet: + modulator, or right-click any control", 8, y, 400, 18, juce::Justification::centredLeft); }
        }
        int y = 300;
        for (auto& mm : *ctx.s.midiMaps) { g.setColour (theme.ink); g.drawText ("cc " + juce::String (mm.cc) + ui::U ("  →  ") + ctx.s.targetName (mm.target), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        if (ctx.s.learning.load()) { g.setColour (juce::Colour (0xffee8e6e)); g.drawText ("learning: move a controller for " + ctx.s.targetName (ctx.s.learnTarget) + ui::U ("  (click here to cancel)"), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        for (auto& km : *ctx.s.keyMaps) { g.setColour (theme.ink); g.drawText ("key " + juce::KeyPress (km.key).getTextDescription() + (km.momentary ? " (held)" : " (toggle)") + ui::U ("  →  ") + ctx.s.targetName (km.target), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); y += 20; }
        if (ctx.s.keyLearning.load() != 0) { g.setColour (juce::Colour (0xffee8e6e)); g.drawText ("press a key for " + ctx.s.targetName (ctx.s.keyLearnTarget) + ui::U ("  (click here to cancel)"), getWidth() / 2 + 16, y, getWidth() / 2 - 24, 18, juce::Justification::centredLeft, true); }
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
        auto& s = ctx.s;
        for (auto* f : s.formats.getFormats())
        {
            juce::FileSearchPath paths = f->getDefaultLocationsToSearch();
            juce::PluginDirectoryScanner sc (s.known, *f, paths, true, Session::dataDir().getChildFile ("scan-deadmans.txt"), true);
            juce::String name;
            while (! threadShouldExit())
            {
                progress = f->getName() + ": " + sc.getNextPluginFileThatWillBeScanned();
                if (! sc.scanNextFile (true, name)) break;
            }
        }
        juce::MessageManager::callAsync ([this] { ctx.s.saveKnown(); progress = juce::String (ctx.s.known.getNumTypes()) + " plug-ins known"; repaint(); });
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
                g.setColour (playing ? juce::Colour (0xffee8e6e) : theme.ink.withAlpha (f.isDirectory() ? 0.8f : 1.0f));
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

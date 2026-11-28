#pragma once
// sndstudio — the note clip editor: free notes for an instrument (a hosted vst / au, or the suite's own), opened from an
// arrangement note clip. Click: a note (the grid's length); drag: move it (time on the grid, pitch); its right end: its
// length; alt-drag up / down: how hard; double-click or right-click: remove; cmd-drag: a box; cmd+a, delete, arrows
// (up / down: a semitone, shift: an octave), q: quantise to the grid. Edits go straight into the clip (one undo each).
#include "StudioWidgets.h"
#include "Session.h"
#include "Timeline.h"
#include <set>

namespace fal::daw
{
class NoteEditor : public juce::Component
{
public:
    NoteEditor (Session& s, int trackUid, int clipId)
        : session (s), uid (trackUid), id (clipId), theme (studioTheme())
    {
        setWantsKeyboardFocus (true);
        if (auto* c = clip()) { if (c->notes && ! c->notes->empty()) { int lo = 127, hi = 0; for (auto& n : *c->notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); } topKey = juce::jlimit (12, 127, (hi + lo) / 2 + 12); } }
    }
    std::function<void()> onClose;

    const Clip* clip() const
    {
        if (auto* t = session.trackByUid (uid)) for (auto& c : *t->clips) if (c.id == id) return &c;
        return nullptr;
    }
    std::vector<Note> notes() const { auto* c = clip(); return c && c->notes ? *c->notes : std::vector<Note>(); }
    void setNotes (std::vector<Note> v)
    {
        std::stable_sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
        if (auto* t = session.trackByUid (uid)) session.editClips (*t, [&] (std::vector<Clip>& cs) { for (auto& c : cs) if (c.id == id) c.notes = std::make_shared<const std::vector<Note>> (v); });
        repaint();
    }
    /** the arrangement's grid at this zoom (beats), and a beat of the clip onto it (the meter counts: the clip's beats
        are from where its notes start, so the grid is the song's) */
    double grid() const { return timeline::gridBeats (session.gridIndex.load(), roll().getWidth() / beats()); }
    double anchor() const { auto* c = clip(); return c ? c->startBar * 4.0 - c->offset : 0.0; }
    double snap (double b) const { return session.snap.load() ? timeline::snap (anchor() + b, grid(), session.meterMap()) - anchor() : b; }
    // the view: the clip's beats across, keys down from topKey
    double beat0() const { auto* c = clip(); return c ? c->offset : 0.0; }
    double beats() const { auto* c = clip(); return c ? std::max (0.25, c->bars * 4.0) : 4.0; }
    juce::Rectangle<int> roll() const { return getLocalBounds().withTrimmedTop (head).withTrimmedLeft (keysW).withTrimmedBottom (laneH); }
    /** the lane under the notes: velocity, or a controller (cc 0-127, pitch bend, pressure) */
    juce::Rectangle<int> lane() const { return getLocalBounds().withTrimmedLeft (keysW).removeFromBottom (laneH - 4); }
    int laneKind = -1;   // -1 velocity
    static juce::String kindName (int k)
    {
        if (k < 0) return "velocity"; if (k == kCtrlBend) return "pitch bend"; if (k == kCtrlPressure) return "pressure";
        if (k == 1) return "mod wheel (cc 1)"; if (k == 2) return "breath (cc 2)"; if (k == 11) return "expression (cc 11)"; if (k == 64) return "sustain (cc 64)"; if (k == 74) return "brightness (cc 74)";
        return "cc " + juce::String (k);
    }
    std::vector<Ctrl> ctrls() const { auto* c = clip(); return c && c->ctrl ? *c->ctrl : std::vector<Ctrl>(); }
    void setCtrls (std::vector<Ctrl> v)
    {
        std::stable_sort (v.begin(), v.end(), [] (const Ctrl& a, const Ctrl& b) { return a.beat < b.beat; });
        if (auto* t = session.trackByUid (uid)) session.editClips (*t, [&] (std::vector<Clip>& cs) { for (auto& c : cs) if (c.id == id) c.ctrl = v.empty() ? nullptr : std::make_shared<const std::vector<Ctrl>> (v); });
        repaint();
    }
    float laneValueAt (float y) const { const auto l = lane(); return juce::jlimit (0.0f, 1.0f, 1.0f - (y - (float) l.getY()) / (float) l.getHeight()); }
    float laneY (float v) const { const auto l = lane(); return (float) l.getY() + (1.0f - v) * (float) l.getHeight(); }
    juce::Rectangle<int> laneLabel() const { return { 2, lane().getY(), keysW - 4, 14 }; }
    void laneMenu()
    {
        juce::PopupMenu m; const int kinds[] = { -1, 1, kCtrlBend, kCtrlPressure, 11, 64, 2, 74 };
        for (int k : kinds) m.addItem (1000 + k, kindName (k), true, laneKind == k);
        std::set<int> used; for (auto& c : ctrls()) used.insert (c.kind);
        juce::PopupMenu inClip; for (int k : used) inClip.addItem (1000 + k, kindName (k), true, laneKind == k);
        if (! used.empty()) m.addSubMenu ("in this clip", inClip);
        m.addItem (5000, "another cc..."); m.addSeparator(); m.addItem (5001, "clear this lane", laneKind >= 0);
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            if (r >= 999 && r < 1200) { laneKind = r - 1000; repaint(); }
            else if (r == 5001) { auto v = ctrls(); v.erase (std::remove_if (v.begin(), v.end(), [this] (const Ctrl& c) { return c.kind == laneKind; }), v.end()); setCtrls (v); }
            else if (r == 5000)
            {
                auto* w = new juce::AlertWindow ("a controller", "cc number (0 - 127)", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("t", "7"); w->addButton ("ok", 1); w->addButton ("cancel", 0);
                juce::Component::SafePointer<NoteEditor> sp (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int rr) { if (sp && rr == 1) { sp->laneKind = juce::jlimit (0, 127, w->getTextEditorContents ("t").getIntValue()); sp->repaint(); } }), true);
            }
        });
    }
    /** draw a controller from a to b (the lane's points between them replaced, one a grid step) */
    void drawCtrl (double b0, float v0, double b1, float v1)
    {
        if (b1 < b0) { std::swap (b0, b1); std::swap (v0, v1); }
        auto v = ctrls();
        v.erase (std::remove_if (v.begin(), v.end(), [&] (const Ctrl& c) { return c.kind == laneKind && c.beat >= b0 - 1.0e-9 && c.beat <= b1 + 1.0e-9; }), v.end());
        const double st = std::max (1.0 / 64.0, grid() / 2.0);
        for (double b = std::floor (b0 / st) * st; b <= b1 + 1.0e-9; b += st) if (b >= b0 - 1.0e-9) { const double u = b1 > b0 ? (b - b0) / (b1 - b0) : 0.0; v.push_back ({ b, laneKind, (float) (v0 + (v1 - v0) * u) }); }
        setCtrls (v);
    }
    double beatAt (float x) const { const auto r = roll(); return beat0() + (x - r.getX()) / (double) r.getWidth() * beats(); }
    float xOf (double b) const { const auto r = roll(); return (float) r.getX() + (float) ((b - beat0()) / beats() * r.getWidth()); }
    int keyAt (float y) const { return topKey - (int) std::floor ((y - roll().getY()) / keyH); }
    float yOf (int key) const { return (float) roll().getY() + (float) (topKey - key) * keyH; }
    int noteAt (juce::Point<float> p) const
    {
        auto v = notes();
        for (int k = (int) v.size() - 1; k >= 0; --k) { const auto& n = v[(size_t) k]; if (juce::Rectangle<float> (xOf (n.beat), yOf (n.pitch), std::max (4.0f, xOf (n.beat + n.len) - xOf (n.beat)), keyH).contains (p)) return k; }
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground.withAlpha (0.985f));
        const auto r = roll();
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true));
        auto* c = clip();
        g.drawText ("notes" + (c && c->name.isNotEmpty() ? juce::String (": ") + c->name : juce::String()) + "   " + juce::String (notes().size()) + " notes", 8, 4, getWidth() - 200, 18, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.7f));
        g.drawText (ui::U ("click: a note · drag: move · its end: length · alt-drag: velocity · double-click: remove · cmd-drag: select · q: quantise · ↑↓: transpose · esc: close"), 8, 20, getWidth() - 16, 14, juce::Justification::centredLeft);
        g.setColour (theme.ink); g.setFont (uiFont (11.0f)); g.drawText ("close", getWidth() - 60, 4, 52, 18, juce::Justification::centredRight);
        // keys
        for (int k = topKey; yOf (k) < (float) r.getBottom(); --k)
        {
            if (k < 0) break;
            const float y = yOf (k); const int pc = k % 12; const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour (black ? theme.ink.withAlpha (0.05f) : juce::Colours::transparentBlack); g.fillRect ((float) r.getX(), y, (float) r.getWidth(), keyH);
            g.setColour (black ? theme.ink.withAlpha (0.55f) : theme.ink.withAlpha (0.12f)); g.fillRect (0.0f, y, (float) keysW - 2.0f, keyH - 1.0f);
            if (pc == 0) { g.setColour (theme.ink); g.setFont (uiFont (9.0f)); g.drawText ("c" + juce::String (k / 12 - 1), 2, (int) y, keysW - 6, (int) keyH, juce::Justification::centredRight); g.setColour (theme.ink.withAlpha (0.15f)); g.fillRect ((float) r.getX(), y + keyH - 1.0f, (float) r.getWidth(), 1.0f); }
        }
        // the grid
        const double gb = std::max (1.0 / 64.0, grid());
        for (double b = std::ceil (beat0() / gb) * gb; b <= beat0() + beats() + 1.0e-9; b += gb)
        {
            const bool beat = std::abs (b - std::round (b)) < 1.0e-9, bar4 = beat && ((long long) std::llround (b)) % 4 == 0;
            g.setColour (theme.ink.withAlpha (bar4 ? 0.3f : beat ? 0.14f : 0.06f)); g.fillRect (xOf (b), (float) r.getY(), 1.0f, (float) r.getHeight());
        }
        // notes (velocity: how full)
        auto v = notes();
        {
        juce::Graphics::ScopedSaveState clipNotes (g); g.reduceClipRegion (r);
        for (int k = 0; k < (int) v.size(); ++k)
        {
            const auto& n = v[(size_t) k];
            auto nr = juce::Rectangle<float> (xOf (n.beat), yOf (n.pitch) + 1.0f, std::max (3.0f, xOf (n.beat + n.len) - xOf (n.beat) - 1.0f), keyH - 2.0f);
            if (! nr.intersects (r.toFloat())) continue;
            const bool s = std::find (sel.begin(), sel.end(), k) != sel.end();
            g.setColour (theme.fill.withAlpha (0.3f + 0.7f * n.vel)); g.fillRect (nr);
            g.setColour (s ? brightColour() : theme.ink.withAlpha (0.6f)); g.drawRect (nr, s ? 2.0f : 1.0f);
        }
        }
        // the lane: velocity bars, or the controller as a stepped line
        {
            const auto l = lane();
            g.setColour (theme.ink.withAlpha (0.06f)); g.fillRect (l);
            g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (l.getX(), l.getY() - 2, l.getWidth(), 1);
            g.setColour (theme.ink); g.setFont (uiFont (9.0f)); g.drawFittedText (kindName (laneKind), laneLabel().withHeight (l.getHeight()), juce::Justification::topLeft, 4);
            if (laneKind < 0)
                for (int k = 0; k < (int) v.size(); ++k)
                {
                    const auto& n = v[(size_t) k]; const float x = xOf (n.beat);
                    if (x < (float) l.getX() || x > (float) l.getRight()) continue;
                    const bool s = std::find (sel.begin(), sel.end(), k) != sel.end();
                    g.setColour (s ? brightColour() : theme.fill); g.fillRect (x, laneY (n.vel), 3.0f, (float) l.getBottom() - laneY (n.vel));
                }
            else
            {
                juce::Path p; bool started = false; float lastY = laneY (laneKind == kCtrlBend ? 0.5f : 0.0f);
                p.startNewSubPath ((float) l.getX(), lastY); started = true;
                for (auto& c : ctrls()) if (c.kind == laneKind) { const float x = juce::jlimit ((float) l.getX(), (float) l.getRight(), xOf (c.beat)); p.lineTo (x, lastY); lastY = laneY (c.value); p.lineTo (x, lastY); }
                if (started) p.lineTo ((float) l.getRight(), lastY);
                g.setColour (warmColour()); g.strokePath (p, juce::PathStrokeType (1.4f));
                if (laneKind == kCtrlBend) { g.setColour (theme.ink.withAlpha (0.2f)); g.fillRect ((float) l.getX(), laneY (0.5f), (float) l.getWidth(), 1.0f); }
            }
        }
        if (band) { g.setColour (brightColour().withAlpha (0.12f)); g.fillRect (bandRect()); g.setColour (brightColour().withAlpha (0.7f)); g.drawRect (bandRect(), 1); }
    }
    juce::Rectangle<int> bandRect() const { return juce::Rectangle<int> (bandA, bandB); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.y < head && e.x > getWidth() - 70) { if (onClose) onClose(); return; }
        if (e.x < keysW && e.y >= lane().getY()) { laneMenu(); return; }
        if (lane().contains (e.getPosition()))
        {
            laneDrag = true; lastLaneBeat = beatAt (e.position.x); lastLaneV = laneValueAt (e.position.y);
            laneEdit (e); return;
        }
        if (! roll().contains (e.getPosition())) return;
        const int k = noteAt (e.position);
        if (k >= 0 && (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)) { auto v = notes(); v.erase (v.begin() + k); sel.clear(); setNotes (v); return; }
        if (e.mods.isCommandDown())
        {
            if (k >= 0) { auto it = std::find (sel.begin(), sel.end(), k); if (it != sel.end()) sel.erase (it); else sel.push_back (k); repaint(); return; }
            band = true; bandA = bandB = e.getPosition(); repaint(); return;
        }
        if (k < 0)
        {
            if (e.mods.isPopupMenu()) return;
            // a new note, the grid's length, on the grid
            auto v = notes();
            Note n; n.beat = std::max (beat0(), snap (beatAt (e.position.x))); n.len = std::max (1.0 / 64.0, grid()); n.pitch = juce::jlimit (0, 127, keyAt (e.position.y)); n.vel = lastVel;
            v.push_back (n); setNotes (v);
            v = notes(); for (int i = 0; i < (int) v.size(); ++i) if (std::abs (v[(size_t) i].beat - n.beat) < 1.0e-9 && v[(size_t) i].pitch == n.pitch) sel = { i };
        }
        else if (std::find (sel.begin(), sel.end(), k) == sel.end()) sel = { k };
        // drag what is selected (the note under the mouse leads)
        orig = notes(); dragSel = sel; lead = sel.empty() ? -1 : sel.back();
        if (lead >= 0 && lead < (int) orig.size())
        {
            const auto& n = orig[(size_t) lead];
            mode = e.mods.isAltDown() ? Mode::Velocity : (e.position.x > xOf (n.beat + n.len) - 6.0f ? Mode::Length : Mode::Move);
        }
        repaint();
    }
    /** the lane: velocity of the notes under the mouse (the selected ones, if it is on one of them), or a controller drawn */
    void laneEdit (const juce::MouseEvent& e)
    {
        const double b = beatAt (e.position.x); const float val = laneValueAt (e.position.y);
        if (laneKind < 0)
        {
            auto v = notes(); bool any = false;
            const double lo = std::min (lastLaneBeat, b) - 0.5 * beats() / std::max (1, lane().getWidth()) * 6.0, hi = std::max (lastLaneBeat, b) + 0.5 * beats() / std::max (1, lane().getWidth()) * 6.0;
            for (int k = 0; k < (int) v.size(); ++k) if (v[(size_t) k].beat >= lo && v[(size_t) k].beat <= hi) { v[(size_t) k].vel = std::max (0.02f, val); any = true; }
            if (any) setNotes (v);
        }
        else if (e.mods.isAltDown())
        {
            auto v = ctrls(); const double lo = std::min (lastLaneBeat, b), hi = std::max (lastLaneBeat, b);
            v.erase (std::remove_if (v.begin(), v.end(), [&] (const Ctrl& c) { return c.kind == laneKind && c.beat >= lo && c.beat <= hi; }), v.end()); setCtrls (v);
        }
        else drawCtrl (lastLaneBeat, lastLaneV, std::max (beat0(), b), val);
        lastLaneBeat = std::max (beat0(), b); lastLaneV = val;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (laneDrag) { laneEdit (e); return; }
        if (band)
        {
            bandB = e.getPosition(); sel.clear();
            const auto br = bandRect().toFloat(); auto v = notes();
            for (int k = 0; k < (int) v.size(); ++k) if (br.intersects (juce::Rectangle<float> { xOf (v[(size_t) k].beat), yOf (v[(size_t) k].pitch), std::max (3.0f, xOf (v[(size_t) k].beat + v[(size_t) k].len) - xOf (v[(size_t) k].beat)), keyH })) sel.push_back (k);
            repaint(); return;
        }
        if (lead < 0 || lead >= (int) orig.size() || mode == Mode::None) return;
        const auto& L = orig[(size_t) lead];
        auto v = orig;
        if (mode == Mode::Move)
        {
            const double nb = std::max (0.0, snap (L.beat + (beatAt (e.position.x) - beatAt ((float) e.getMouseDownX()))));
            const int dk = keyAt (e.position.y) - keyAt ((float) e.getMouseDownY());
            double db = nb - L.beat; for (int k : dragSel) db = std::max (db, -orig[(size_t) k].beat);
            for (int k : dragSel) { auto& n = v[(size_t) k]; n.beat += db; n.pitch = juce::jlimit (0, 127, n.pitch + dk); }
        }
        else if (mode == Mode::Length)
        {
            const double end = snap (beatAt (e.position.x)); const double nl = std::max (1.0 / 64.0, end - L.beat);
            const double f = nl / std::max (1.0e-9, L.len);
            for (int k : dragSel) v[(size_t) k].len = std::max (1.0 / 64.0, orig[(size_t) k].len * f);
        }
        else
        {
            const float dv = -(float) e.getDistanceFromDragStartY() / 120.0f;
            for (int k : dragSel) v[(size_t) k].vel = juce::jlimit (0.02f, 1.0f, orig[(size_t) k].vel + dv);
            lastVel = v[(size_t) lead].vel;
        }
        // the clip gets them in time order; the selection follows its notes (the drag keeps working from the originals)
        std::vector<std::pair<Note, bool>> tagged; for (int k = 0; k < (int) v.size(); ++k) tagged.push_back ({ v[(size_t) k], std::find (dragSel.begin(), dragSel.end(), k) != dragSel.end() });
        std::stable_sort (tagged.begin(), tagged.end(), [] (auto& a, auto& b) { return a.first.beat < b.first.beat; });
        std::vector<Note> out; std::vector<int> ns;
        for (int k = 0; k < (int) tagged.size(); ++k) { out.push_back (tagged[(size_t) k].first); if (tagged[(size_t) k].second) ns.push_back (k); }
        setNotes (out); sel = ns;
    }
    void mouseUp (const juce::MouseEvent&) override { laneDrag = false; band = false; mode = Mode::None; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { topKey = juce::jlimit (12, 127, topKey + (w.deltaY > 0 ? 2 : w.deltaY < 0 ? -2 : 0)); repaint(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const int kc = k.getKeyCode(); const auto m = k.getModifiers();
        if (kc == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
        {
            auto v = notes(); auto s = sel; std::sort (s.rbegin(), s.rend()); for (int i : s) if (i < (int) v.size()) v.erase (v.begin() + i); sel.clear(); setNotes (v); return true;
        }
        if (m.isCommandDown() && k.getTextCharacter() == 'a') { sel.clear(); for (int i = 0; i < (int) notes().size(); ++i) sel.push_back (i); repaint(); return true; }
        if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey)
        {
            const int d = (kc == juce::KeyPress::upKey ? 1 : -1) * (m.isShiftDown() ? 12 : 1);
            auto v = notes(); for (int i : sel) if (i < (int) v.size()) v[(size_t) i].pitch = juce::jlimit (0, 127, v[(size_t) i].pitch + d); setNotes (v); return true;
        }
        if (k.getTextCharacter() == 'q') { quantise (sel.empty()); return true; }
        return false;
    }
    /** the notes (the selected ones, or all) onto the grid */
    void quantise (bool all)
    {
        auto v = notes();
        for (int i = 0; i < (int) v.size(); ++i) if (all || std::find (sel.begin(), sel.end(), i) != sel.end()) v[(size_t) i].beat = std::max (0.0, snap (v[(size_t) i].beat));
        sel.clear(); setNotes (v);
    }
    std::vector<int> sel;
    int topKey = 72;
private:
    enum class Mode { None, Move, Length, Velocity };
    Session& session; int uid, id;
    ui::Theme theme;
    std::vector<Note> orig; std::vector<int> dragSel; int lead = -1; Mode mode = Mode::None; float lastVel = 0.8f;
    bool band = false; juce::Point<int> bandA, bandB;
    bool laneDrag = false; double lastLaneBeat = 0; float lastLaneV = 0;
    static constexpr int head = 38, keysW = 44, laneH = 84; static constexpr float keyH = 10.0f;
};
} // namespace fal::daw

#pragma once
// faltung sndstudio — the small controls every page is built from: value rows, device sliders with the
// map / automate / learn menu, meters, the modulator popup, the device chain panel, plug-in windows.
#include "StudioProcessor.h"
#include "devices/StudioDevices.h"
#include "../Ui.h"
#include "../Editor.h"
#include "../dsp/Bass.h"

namespace fal::daw
{
/** the studio's theme comes from the suite's own palette code (fal::themeFor), fed with the studio's entry in the product
    table (studioInfo, Products.cpp): whatever a suite's look becomes there, the studio follows */
/** colour themes: the field, the ink, the fill (lit things), the edges, and three accents (hot: cables, warnings, the
    automation; cool: markers, frozen, warp; warm: gain lines, controllers). 0 is the suite's own look */
struct StudioPalette { const char* name; juce::uint32 ground, ink, fill, edge, hot, cool, warm; };
inline const std::vector<StudioPalette>& studioPalettes()
{
    static const std::vector<StudioPalette> p {
        { "faltung (the suite's)", 0, 0, 0, 0, 0xffee8e6e, 0xff8ec3e6, 0xfff6c590 },
        { "deep night",   0xff14191c, 0xffd8e2e6, 0xff6e9fb8, 0xff2a3338, 0xffe07a5f, 0xff7fb7d6, 0xffd9b26f },
        { "paper",        0xfff2efe8, 0xff1d1d1b, 0xff2f5d8a, 0xffcfc9bd, 0xffc4452c, 0xff2f6f9f, 0xffb3862b },
        { "terminal",     0xff050805, 0xff9cf09c, 0xff3fbf5f, 0xff163016, 0xffffb000, 0xff4fd1c5, 0xffd0f070 },
        { "riso",         0xfff7e8df, 0xff2a2a72, 0xffff5fa2, 0xffe6cfc4, 0xffff4a3d, 0xff2a6ad8, 0xffe09a00 },
        { "concrete",     0xff8a8d8f, 0xff111213, 0xff2b2d2f, 0xff6f7274, 0xffb8340f, 0xff1f4e79, 0xff8a6a10 },
        { "amber cell",   0xff1a1208, 0xffffc46b, 0xffff8f1f, 0xff3a2810, 0xffff5a36, 0xffffd99a, 0xffffa733 },
        { "moss",         0xff1f2a20, 0xffdbe5c9, 0xff8fb36a, 0xff334433, 0xffe2845a, 0xff86b7a5, 0xffd8c46a },
        { "ultraviolet",  0xff120b24, 0xffe8dcff, 0xff9a6bff, 0xff2a1d4a, 0xffff5fa2, 0xff5fd1ff, 0xffffc857 },
        { "sand",         0xffe4d6bd, 0xff3b2f22, 0xffa4572f, 0xffc9b998, 0xffb4361f, 0xff3d6a73, 0xff8f6a14 },
        { "high contrast",0xff000000, 0xffffffff, 0xffffe600, 0xff444444, 0xffff3b30, 0xff00c8ff, 0xffffe600 },
    };
    return p;
}
inline int& studioThemeIndex() { static int i = 0; return i; }
inline const StudioPalette& studioPalette() { const auto& p = studioPalettes(); return p[(size_t) juce::jlimit (0, (int) p.size() - 1, studioThemeIndex())]; }
inline ui::Theme studioTheme()
{
    static const ui::Theme suite = []
    {
        const auto& si = studioInfo();
        const ProductInfo pi { Pid::kt1, si.code, si.name, si.blurb, fal::Kind::Machine, Style::Snd, si.ground, si.accent, VType::Kick, si.w, si.h };
        return themeFor (pi);
    }();
    const auto& p = studioPalette();
    if (p.ground == 0) return suite;
    ui::Theme t = suite; t.ground = juce::Colour (p.ground); t.ink = juce::Colour (p.ink); t.fill = juce::Colour (p.fill); t.edge = juce::Colour (p.edge);
    return t;
}
inline juce::Colour hotColour() { return juce::Colour (studioPalette().hot); }
inline juce::Colour coolColour() { return juce::Colour (studioPalette().cool); }
inline juce::Colour warmColour() { return juce::Colour (studioPalette().warm); }
/** the brightest mark (the playhead, what is selected): white on dark themes, the ink on light ones */
inline juce::Colour brightColour() { const auto g = studioTheme().ground; return g.getPerceivedBrightness() > 0.55f ? studioTheme().ink : juce::Colours::white; }
inline juce::Font uiFont (float size, bool medium = false) { return studioTheme().mirror ? ui::cond (size + 1.5f, medium) : ui::mono (size, medium); }
inline juce::Colour trackColour (const Track& t) { return juce::Colour (t.colour); }

/** a generic "label · value" row: drag to change, click a choice for its menu, double-click to reset */
class ValueRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueRow (juce::String label, float lo, float hi, float def, std::function<juce::String (float)> text = {})
        : name (std::move (label)), lo_ (lo), hi_ (hi), def_ (def), value (def), fmt (std::move (text)) { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
    ValueRow (juce::String label, juce::StringArray items, int def)
        : name (std::move (label)), lo_ (0), hi_ ((float) std::max (1, items.size() - 1)), def_ ((float) def), value ((float) def), choices (std::move (items)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void (float)> onChange;
    std::function<void (bool)> onGesture;
    float get() const { return value; }
    int getInt() const { return juce::roundToInt (value); }
    void set (float v, bool notify = false) { v = juce::jlimit (lo_, hi_, v); if (! choices.isEmpty()) v = std::round (v); if (std::abs (v - value) < 1.0e-7f) return; value = v; repaint(); if (notify && onChange) onChange (value); }
    void setLabelWidth (int w) { labelW = w; repaint(); }
    void setBool (bool b) { set (b ? 1.0f : 0.0f); }
    bool isBool() const { return choices.size() == 2 && lo_ == 0 && hi_ == 1 && boolStyle; }
    bool boolStyle = false;
    juce::String text() const
    {
        if (! choices.isEmpty()) return choices[juce::jlimit (0, choices.size() - 1, getInt())];
        return fmt ? fmt (value) : juce::String (value, 2);
    }
    void paint (juce::Graphics& g) override
    {
        auto t = studioTheme(); auto b = getLocalBounds().toFloat();
        g.setColour (t.ink); g.setFont (uiFont (11.0f));
        if (isBool())
        {
            const auto r = juce::Rectangle<float> (0.0f, b.getCentreY() - 5.0f, 18.0f, 10.0f);
            auto p = ui::skewRect (r);
            if (value > 0.5f) { g.setColour (t.fill); g.fillPath (p); }
            g.setColour (t.ink); g.strokePath (p, juce::PathStrokeType (1.0f));
            g.drawText (name, b.withTrimmedLeft (26.0f), juce::Justification::centredLeft, true);
            return;
        }
        g.drawText (name, b.removeFromLeft ((float) labelW), juce::Justification::centredLeft, true);
        if (! choices.isEmpty())
        {
            g.setFont (uiFont (11.0f, true)); g.drawText (text(), b, juce::Justification::centredLeft, true);
            g.setColour (t.ink.withAlpha (0.35f)); g.fillRect (b.getX(), b.getBottom() - 2.0f, std::min (b.getWidth(), 90.0f), 1.0f);
            return;
        }
        g.drawText (text(), b.removeFromRight (64.0f), juce::Justification::centredRight, true);
        const auto tr = b.reduced (4.0f, 0.0f); const float cy = tr.getCentreY();
        g.setColour (t.ink.withAlpha (0.35f)); g.fillRect (tr.getX(), cy, tr.getWidth(), 1.0f);
        const float v01 = (value - lo_) / std::max (1.0e-6f, hi_ - lo_);
        const bool bipolar = lo_ < 0 && hi_ > 0;
        const float zero = bipolar ? (0 - lo_) / (hi_ - lo_) : 0.0f;
        const float a = tr.getX() + tr.getWidth() * std::min (zero, v01), e = tr.getX() + tr.getWidth() * std::max (zero, v01);
        if (e - a > 0.5f) { g.setColour (t.fill.withAlpha (0.92f)); if (t.mirror) g.fillRect (a, cy - 1.0f, e - a, 2.0f); else g.fillPath (ui::skewRect ({ a, cy - 4.5f, e - a, 9.0f })); }
        g.setColour (t.edge); g.fillRect (tr.getX() + tr.getWidth() * v01 - 1.0f, cy - 5.5f, 2.0f, 11.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }
        if (isBool()) { set (value > 0.5f ? 0.0f : 1.0f, true); return; }
        if (! choices.isEmpty())
        {
            if (e.mods.isAltDown()) { set ((float) ((getInt() + 1) % choices.size()), true); return; }
            juce::PopupMenu m;
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == getInt());
            juce::Component::SafePointer<ValueRow> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe && r > 0) safe->set ((float) (r - 1), true); });
            return;
        }
        dragStart = value; dragging = true; if (onGesture) onGesture (true);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float w = std::max (40.0f, (float) (getWidth() - labelW - 72));
        const float sens = e.mods.isShiftDown() ? 0.12f : 1.0f;
        const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY() * 0.6f) / w * sens;
        set (dragStart + d * (hi_ - lo_), true);
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; if (onGesture) onGesture (false); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (def_, true); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! choices.isEmpty()) { set (value + (w.deltaY > 0 ? 1.0f : -1.0f), true); return; }
        set (value + (hi_ - lo_) * (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f), true);
    }
    std::function<void()> onMenu;
    juce::String name;
private:
    float lo_, hi_, def_, value; std::function<juce::String (float)> fmt; juce::StringArray choices;
    int labelW = 64; float dragStart = 0; bool dragging = false;
};

/** the parameter slider of a device: bound to the base, shows the modulation band and the lock, right-click for the map / automate / learn menu */
class DevSlider : public ui::ParamSlider
{
public:
    DevSlider (Session& s, Track& t, Device& d, int param, juce::String label = {}, int labelWidth = 78)
        : ui::ParamSlider (d.base (param), studioTheme(), label.isEmpty() ? d.paramName (param) : label), session (s), track (t), device (d), index (param), lw (labelWidth)
    {
        setLabelWidth (lw);
        setTooltip (d.paramName (param) + ui::U ("  (drag · double-click default · right-click: map a modulator, automation, midi learn, macro)"));
    }
    std::function<void (DevSlider&)> onMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu (*this); return; }
        ui::ParamSlider::mouseDown (e);
    }
    void paint (juce::Graphics& g) override
    {
        ui::ParamSlider::paint (g);
        // the modulation band and the lock marker
        const float lo = device.modMin (index), hi = device.modMax (index);
        auto b = getLocalBounds().toFloat(); b.removeFromLeft ((float) lw); b.removeFromRight (70.0f); b = b.reduced (4.0f, 0.0f);
        const float base = device.baseValue (index);
        if (hi - lo > 0.002f || modulated)
        {
            const float a = b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, base + lo), e = b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, base + hi);
            g.setColour (hotColour().withAlpha (0.9f));
            g.fillRect (a, b.getBottom() - 3.0f, std::max (2.0f, e - a), 2.0f);
        }
        if (device.isLocked (index)) { g.setColour (brightColour()); g.drawRect (juce::Rectangle<float> (b.getX() + b.getWidth() * device.finalValue (index) - 3.0f, b.getCentreY() - 7.0f, 6.0f, 14.0f), 1.0f); }
        if (hasLane) { g.setColour (hotColour()); g.fillEllipse (b.getX() - 7.0f, b.getCentreY() - 2.0f, 4.0f, 4.0f); }
    }
    void refresh()
    {
        bool m = false; for (auto& x : *track.mods) if (x.targets (device.uid, index)) m = true;
        const bool l = session.lane (session.targetFor (track, device, index)) != nullptr;
        if (m != modulated || l != hasLane) { modulated = m; hasLane = l; }
        repaint();
    }
    Session& session; Track& track; Device& device; const int index; const int lw;
    bool modulated = false, hasLane = false;
};

/** a compact slider for narrow places (the mixer strips): the name and the value on one line, the bar under them */
class BarSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    BarSlider (Device& d, int prm, juce::String name)
        : device (d), index (prm), label (std::move (name)), param (d.base (prm)),
          attach (param, [this] (float v) { value01 = param.convertTo0to1 (v); repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTooltip (d.paramName (prm) + ui::U ("  (drag · shift = fine · double-click = default · right-click: map a modulator, automation, midi, key, macro)"));
    }
    std::function<void (BarSlider&)> onMenu;
    void setLabel (juce::String l) { label = std::move (l); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto t = studioTheme(); auto b = getLocalBounds().toFloat().reduced (3.0f, 0.0f);
        const auto text = b.withHeight (b.getHeight() - 6.0f);
        g.setColour (t.ink); g.setFont (uiFont (10.0f));
        const auto value = param.getCurrentValueAsText();
        const float vw = std::min (text.getWidth() * 0.62f, juce::GlyphArrangement::getStringWidth (uiFont (10.0f), value) + 4.0f);
        g.drawText (value, text.withLeft (text.getRight() - vw), juce::Justification::centredRight, false);
        g.setColour (t.ink.withAlpha (0.75f)); g.drawText (label, text.withRight (text.getRight() - vw - 2.0f), juce::Justification::centredLeft, true);
        const float y = b.getBottom() - 4.5f;
        g.setColour (t.ink.withAlpha (0.25f)); g.fillRect (b.getX(), y, b.getWidth(), 3.0f);
        g.setColour (t.fill.withAlpha (dragging ? 1.0f : 0.9f)); g.fillRect (b.getX(), y, b.getWidth() * value01, 3.0f);
        const float lo = device.modMin (index), hi = device.modMax (index);
        if (hi - lo > 0.002f)
        {
            const float a = b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, value01 + lo), e = b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, value01 + hi);
            g.setColour (hotColour()); g.fillRect (a, y + 3.0f, std::max (2.0f, e - a), 1.5f);
        }
        g.setColour (t.edge); g.fillRect (b.getX() + b.getWidth() * value01 - 1.0f, y - 2.0f, 2.0f, 7.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu (*this); return; }
        dragging = true; dragStart = value01; attach.beginGesture(); repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float w = std::max (60.0f, (float) getWidth() - 6.0f), sens = e.mods.isShiftDown() ? 0.12f : 1.0f;
        attach.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, dragStart + ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY() * 0.6f) / w * sens)));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; attach.endGesture(); repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY + w.deltaX >= 0 ? 1.0f : -1.0f);
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value01 + step)));
    }
    Device& device; const int index;
private:
    juce::String label; juce::RangedAudioParameter& param; juce::ParameterAttachment attach;
    float value01 = 0, dragStart = 0; bool dragging = false;
};

/** level meter: one bar per channel (up to the track width) */
class Meter : public juce::Component
{
public:
    explicit Meter (Track& t) : track (t) {}
    void paint (juce::Graphics& g) override
    {
        auto t = studioTheme();
        const int w = std::min (kMaxWidth, track.width.load());
        auto b = getLocalBounds().toFloat();
        const float cw = b.getWidth() / (float) std::max (1, w);
        for (int c = 0; c < w; ++c)
        {
            const float pk = shown[(size_t) c];
            const float dbv = juce::Decibels::gainToDecibels (pk, -60.0f);
            const float h = juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f) * b.getHeight();
            auto r = juce::Rectangle<float> (b.getX() + c * cw, b.getBottom() - h, std::max (1.0f, cw - 1.0f), h);
            g.setColour (t.ink.withAlpha (0.18f)); g.fillRect (juce::Rectangle<float> (b.getX() + c * cw, b.getY(), std::max (1.0f, cw - 1.0f), b.getHeight()));
            g.setColour (dbv > -1.0f ? hotColour() : t.fill); g.fillRect (r);
        }
        g.setColour (t.ink.withAlpha (0.5f));
        const float y0 = b.getBottom() - (60.0f - 18.0f) / 60.0f * b.getHeight(), y6 = b.getBottom() - (60.0f - 6.0f) / 60.0f * b.getHeight();
        g.fillRect (b.getX(), y0, b.getWidth(), 1.0f); g.fillRect (b.getX(), y6, b.getWidth(), 1.0f);
    }
    void tick()
    {
        for (int c = 0; c < kMaxWidth; ++c) { const float v = track.meter[(size_t) c].load(); shown[(size_t) c] = v > shown[(size_t) c] ? v : shown[(size_t) c] * 0.85f; }
        repaint();
    }
private:
    Track& track; std::array<float, kMaxWidth> shown {};
};

/** a small popup to create / edit one modulator on a parameter */
class ModPopup : public juce::Component
{
public:
    ModPopup (Session& s, Track& t, int modId) : session (s), track (t), id (modId)
    {
        auto* m = session.findMod (track, id);
        Modulator mm = m ? *m : Modulator();
        juce::StringArray srcs; for (int i = 0; i < MOD_COUNT; ++i) srcs.add (modSrcName (i));
        juce::StringArray shapes; for (int i = 0; i < kLfoShapes; ++i) shapes.add (lfoShapeName (i));
        add (src = new ValueRow ("source", srcs, mm.src));
        add (shape = new ValueRow ("shape", shapes, mm.shape));
        add (sync = new ValueRow ("sync", { "free", "sync" }, mm.sync ? 1 : 0)); sync->boolStyle = true;
        add (div = new ValueRow ("rate", divisionNames(), mm.div));
        add (hz = new ValueRow ("hz", 0.01f, 40.0f, mm.hz, [] (float v) { return juce::String (v, v < 1 ? 3 : 2) + " hz"; }));
        add (depth = new ValueRow ("depth", -1.0f, 1.0f, mm.depth, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; }));
        add (pol = new ValueRow ("polarity", { "bipolar", "up", "down" }, mm.polarity));
        add (phase = new ValueRow ("phase", 0.0f, 1.0f, mm.phase, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        add (skew = new ValueRow ("skew", 0.0f, 1.0f, mm.skew, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        add (pw = new ValueRow ("pulse width", 0.0f, 1.0f, mm.pw, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        add (slew = new ValueRow ("slew", 0.0f, 1.0f, mm.slew, [] (float v) { return juce::String (juce::roundToInt (v * v * 2000)) + " ms"; }));
        add (prob = new ValueRow ("chance", 0.0f, 1.0f, mm.prob, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }));
        add (retrig = new ValueRow ("retrigger", { "off", "on" }, mm.retrig ? 1 : 0)); retrig->boolStyle = true;
        add (att = new ValueRow ("attack", 0.0f, 2000.0f, mm.attackMs, [] (float v) { return juce::String (v, 0) + " ms"; }));
        add (dec = new ValueRow ("decay", 1.0f, 4000.0f, mm.decayMs, [] (float v) { return juce::String (v, 0) + " ms"; }));
        add (macro = new ValueRow ("macro", { "1", "2", "3", "4", "5", "6", "7", "8" }, mm.macro));
        add (cc = new ValueRow ("cc", 0.0f, 127.0f, (float) mm.cc, [] (float v) { return juce::String (juce::roundToInt (v)); }));
        add (stepLen = new ValueRow ("steps", 1.0f, (float) kStepMod, (float) mm.stepLen, [] (float v) { return juce::String (juce::roundToInt (v)); }));
        add (voice = new ValueRow ("which of the four", { "1 (lowest)", "2", "3", "4 (highest)" }, juce::jlimit (0, 3, mm.macro)));
        add (on = new ValueRow ("on", { "off", "on" }, mm.on ? 1 : 0)); on->boolStyle = true;
        steps = mm.steps;
        remove = std::make_unique<ui::TextLink> ("remove", studioTheme()); addAndMakeVisible (*remove);
        remove->onClick = [this] { session.editMods (track, [this] (std::vector<Modulator>& v) { v.erase (std::remove_if (v.begin(), v.end(), [this] (const Modulator& x) { return x.id == id; }), v.end()); }); if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss(); };
        for (auto* r : rows) r->onChange = [this] (float) { push(); };
        setSize (300, 24 + (int) rows.size() * 20 + 30);
    }
    void push()
    {
        session.editMods (track, [this] (std::vector<Modulator>& v)
        {
            for (auto& m : v)
                if (m.id == id)
                {
                    m.src = src->getInt(); m.shape = shape->getInt(); m.sync = sync->getInt() > 0; m.div = div->getInt(); m.hz = hz->get(); m.depth = depth->get(); m.polarity = pol->getInt();
                    m.phase = phase->get(); m.skew = skew->get(); m.pw = pw->get(); m.slew = slew->get(); m.prob = prob->get(); m.retrig = retrig->getInt() > 0;
                    m.attackMs = att->get(); m.decayMs = dec->get(); m.macro = src->getInt() == MOD_FOURSES ? voice->getInt() : macro->getInt(); m.cc = cc->getInt(); m.on = on->getInt() > 0;
                    m.stepLen = stepLen->getInt(); m.steps = steps;
                }
        });
        resized();
    }
    /** scripted use and the menus: set the source (and the lfo shape) as if chosen in the popup */
    void choose (int source, int lfoShape = -1) { src->set ((float) source); if (lfoShape >= 0) shape->set ((float) lfoShape); push(); }
    juce::Rectangle<int> stepArea() const { return stepBox; }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (src->getInt() != MOD_STEPS || ! stepBox.expanded (0, 6).contains (e.getPosition())) return;
        const int n = juce::jlimit (1, kStepMod, stepLen->getInt());
        const int i = juce::jlimit (0, n - 1, (e.x - stepBox.getX()) * n / std::max (1, stepBox.getWidth()));
        steps[(size_t) i] = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (e.y - stepBox.getY()) / (float) std::max (1, stepBox.getHeight()));
        push(); repaint();
    }
    void resized() override
    {
        int y = 22;
        const int s = src->getInt();
        auto show = [&] (ValueRow* r, bool vis) { r->setVisible (vis); if (vis) { r->setBounds (8, y, getWidth() - 16, 18); y += 20; } };
        show (src, true);
        show (shape, s == MOD_LFO);
        const bool timed = s == MOD_LFO || s == MOD_GATE || s == MOD_RANDOM || s == MOD_CHAOS || s == MOD_STEPS || s == MOD_FOURSES;
        show (sync, timed);
        show (div, timed && sync->getInt() > 0);
        show (hz, timed && sync->getInt() == 0);
        show (depth, true); show (pol, true);
        show (phase, s == MOD_LFO); show (skew, s == MOD_LFO); show (pw, s == MOD_LFO && shape->getInt() == 11);
        show (slew, true); show (prob, s == MOD_GATE); show (retrig, s == MOD_LFO);
        show (att, s == MOD_ENV); show (dec, s == MOD_ENV); show (macro, s == MOD_MACRO); show (cc, s == MOD_CC);
        show (stepLen, s == MOD_STEPS); show (voice, s == MOD_FOURSES);
        stepBox = {};
        if (s == MOD_STEPS) { stepBox = { 8, y + 2, getWidth() - 16, 64 }; y += 72; }
        show (on, true);
        traceBox = { 8, y + 2, getWidth() - 16, 26 }; y += 32;
        remove->setBounds (8, y + 4, 80, 20);
        setSize (getWidth(), y + 30);
    }
    void paint (juce::Graphics& g) override
    {
        auto t = studioTheme();
        g.fillAll (t.ground.brighter (t.mirror ? 0.25f : 0.12f));
        g.setColour (t.ink); g.setFont (uiFont (11.0f, true));
        g.drawText ("modulator " + juce::String (id) + ui::U ("  ·  ") + session.targetName (target()), 8, 4, getWidth() - 16, 16, juce::Justification::centredLeft);
        if (! stepBox.isEmpty())
        {
            // the step values as bars: drag across them to draw
            const int n = juce::jlimit (1, kStepMod, stepLen->getInt()); const float w = (float) stepBox.getWidth() / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const float h = steps[(size_t) i] * (float) stepBox.getHeight();
                g.setColour (t.ink.withAlpha (0.12f)); g.fillRect ((float) stepBox.getX() + w * (float) i, (float) stepBox.getY(), w - 2.0f, (float) stepBox.getHeight());
                g.setColour (t.fill); g.fillRect ((float) stepBox.getX() + w * (float) i, (float) stepBox.getBottom() - h, w - 2.0f, h);
            }
        }
        // one cycle of what the source does, as far as it can be drawn (shapes, gates and steps; the rest is live only)
        g.setColour (t.ink.withAlpha (0.1f)); g.fillRect (traceBox);
        const int s = src->getInt();
        if (s == MOD_LFO || s == MOD_GATE || s == MOD_RANDOM || s == MOD_STEPS)
        {
            juce::Path pth; Rng r; r.seed (12345);
            float sh = r.uni(), prev = r.uni(), gate = r.uni() < prob->get() ? 1.0f : 0.0f; int lastCycle = 0;
            for (int px = 0; px < traceBox.getWidth(); ++px)
            {
                const float cyc = (float) px / (float) traceBox.getWidth() * 4.0f; const int ci = (int) cyc; float ph = cyc - (float) ci;
                if (ci != lastCycle) { lastCycle = ci; prev = sh; sh = r.uni(); gate = r.uni() < prob->get() ? 1.0f : 0.0f; }
                float v = 0;
                if (s == MOD_LFO) { ph += phase->get(); ph -= std::floor (ph); v = shape->getInt() == 9 ? 0.5f + 0.3f * std::sin (cyc * 1.7f) * std::sin (cyc * 0.61f + 1.0f) : lfoValue (shape->getInt(), skewPhase (std::min (ph, 0.99999f), skew->get()), sh, prev, pw->get()); }
                else if (s == MOD_GATE) v = gate;
                else if (s == MOD_RANDOM) v = sh;
                else { const int n = juce::jlimit (1, kStepMod, stepLen->getInt()); v = steps[(size_t) juce::jlimit (0, n - 1, (int) (cyc / 4.0f * (float) n))]; }
                const float yy = (float) traceBox.getBottom() - 2.0f - v * (float) (traceBox.getHeight() - 4);
                if (px == 0) pth.startNewSubPath ((float) traceBox.getX(), yy); else pth.lineTo ((float) (traceBox.getX() + px), yy);
            }
            g.setColour (hotColour()); g.strokePath (pth, juce::PathStrokeType (1.2f));
        }
        else { g.setColour (t.ink.withAlpha (0.6f)); g.setFont (uiFont (9.5f)); g.drawText (modSrcName (s), traceBox.reduced (4, 0), juce::Justification::centredLeft); }
    }
    Target target() const { auto* m = session.findMod (track, id); return m ? Target { track.uid, m->device, m->param } : Target(); }
private:
    template <typename T> void add (T* r) { rows.push_back (r); owned.add (r); addAndMakeVisible (r); }
    Session& session; Track& track; int id;
    std::vector<ValueRow*> rows; juce::OwnedArray<ValueRow> owned;
    ValueRow *src, *shape, *sync, *div, *hz, *depth, *pol, *phase, *skew, *pw, *slew, *prob, *retrig, *att, *dec, *macro, *cc, *stepLen, *voice, *on;
    std::array<float, kStepMod> steps {}; juce::Rectangle<int> stepBox, traceBox;
    std::unique_ptr<ui::TextLink> remove;
};

/** a window around a device's own editor (the suite's plug-ins show their own looks; hosted plug-ins theirs) */
class DeviceWindow : public juce::DocumentWindow
{
public:
    DeviceWindow (Device& d, std::function<void()> closed)
        : DocumentWindow (d.name, studioTheme().ground, DocumentWindow::closeButton), device (&d), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        if (auto* p = d.processor())
        {
            juce::AudioProcessorEditor* ed = p->hasEditor() ? p->createEditorIfNeeded() : nullptr;
            if (! ed) ed = new juce::GenericAudioProcessorEditor (*p);
            editor.reset (ed);
            setContentNonOwned (editor.get(), true);
            setResizable (editor->isResizable(), false);
        }
        setVisible (true);
        toFront (true);
    }
    ~DeviceWindow() override
    {
        clearContentComponent();
        if (editor && device && device->processor()) device->processor()->editorBeingDeleted (editor.get());
        editor.reset();
    }
    void closeButtonPressed() override { if (onClosed) onClosed(); }
    Device* device;
private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<void()> onClosed;
};

/** the common right-click menu of a parameter: one entry per kind of modulator (an lfo, a random, a slewed random, a
    random gate …), the automation lane, midi learn, a computer key, a macro. `act` does what an entry does, so
    scripts and tests take the same path as the menu. */
struct ParamMenu
{
    enum { Lfo = 10, Random, Slewed, Gate, Envelope, Steps, Follower, Bounce, Lane = 2, RemoveLane, MidiLearn, MidiForget, Reset, KeyToggle, KeyHold, KeyForget };
    /** creates the modulator (or lane, or map) and returns the modulator's id when one was made */
    static int act (Session& s, Track& t, Device& d, int param, int choice)
    {
        const Target tg = s.targetFor (t, d, param);
        auto make = [&] (int src, int shape, float slew, float depth)
        {
            Modulator m; m.device = d.uid; m.param = param; m.src = src; m.shape = shape; m.slew = slew; m.depth = depth; m.polarity = 0; m.div = 11; m.hz = 1.0f;
            return s.addModulator (t, m);
        };
        switch (choice)
        {
            case Lfo:      return make (MOD_LFO, 0, 0.0f, 0.25f);
            case Random:   return make (MOD_RANDOM, 0, 0.0f, 0.25f);
            case Slewed:   return make (MOD_RANDOM, 0, 0.45f, 0.25f);
            case Gate:     return make (MOD_GATE, 0, 0.0f, 0.5f);
            case Envelope: return make (MOD_ENV, 0, 0.0f, 0.4f);
            case Steps:    return make (MOD_STEPS, 0, 0.0f, 0.4f);
            case Follower: return make (MOD_FOLLOW, 0, 0.1f, 0.4f);
            case Bounce:   return make (MOD_FOURSES, 0, 0.0f, 0.3f);
            case Lane:     if (! s.lane (tg)) s.editLane (tg, [&] (AutoLane& l) { l.pts = { { 0.0, d.baseValue (param), 0.0f } }; }); return 0;
            case RemoveLane: s.removeLane (tg); return 0;
            case MidiLearn: s.midiLearnStart (tg); return 0;
            case MidiForget: s.editMidiMaps ([tg] (std::vector<MidiMap>& v) { v.erase (std::remove_if (v.begin(), v.end(), [tg] (const MidiMap& x) { return x.target == tg; }), v.end()); }); return 0;
            case KeyToggle: s.keyLearnStart (tg, false); return 0;
            case KeyHold:   s.keyLearnStart (tg, true); return 0;
            case KeyForget: s.editKeyMaps ([tg] (std::vector<KeyMap>& v) { v.erase (std::remove_if (v.begin(), v.end(), [tg] (const KeyMap& x) { return x.target == tg; }), v.end()); }); return 0;
            case Reset:    d.setBase (param, d.defaultValue (param)); return 0;
            default: break;
        }
        if (choice >= 200 && choice < 200 + kMacros) { d.macro[(size_t) param].index.store (choice - 200); d.macro[(size_t) param].lo.store (d.baseValue (param)); d.macro[(size_t) param].hi.store (1.0f); }
        else if (choice == 299) d.macro[(size_t) param].index.store (-1);
        return 0;
    }
    static void show (Session& s, Track& t, Device& d, int param, juce::Component* target, std::function<void (int laneTarget)> showLane)
    {
        juce::PopupMenu m;
        m.addSectionHeader (d.paramName (param));
        m.addItem (Lfo, "map an lfo (shape, rate)");
        m.addItem (Random, "map a random (a new value each period)");
        m.addItem (Slewed, "map a slewed random");
        m.addItem (Gate, "map a random gate (on / off)");
        juce::PopupMenu more; more.addItem (Envelope, "an envelope on every hit"); more.addItem (Steps, "a step sequence"); more.addItem (Follower, "the track's own level"); more.addItem (Bounce, "bounce (four ramps that never repeat)");
        m.addSubMenu ("map something else", more);
        int k = 100;
        for (auto& x : *t.mods) if (x.targets (d.uid, param)) m.addItem (k++, "edit modulator " + juce::String (x.id) + " (" + modSrcName (x.src) + ")");
        m.addSeparator();
        const Target tg = s.targetFor (t, d, param);
        m.addItem (Lane, s.lane (tg) ? "show its automation lane" : "automate it on the timeline");
        if (s.lane (tg)) m.addItem (RemoveLane, "remove the automation lane");
        m.addSeparator();
        m.addItem (MidiLearn, "midi learn (move a controller)");
        bool mapped = false; for (auto& x : *s.midiMaps) if (x.target == tg) mapped = true;
        if (mapped) m.addItem (MidiForget, "forget the midi controller");
        juce::String keyName; for (auto& x : *s.keyMaps) if (x.target == tg) keyName = juce::KeyPress (x.key).getTextDescription();
        m.addItem (KeyToggle, "play it from a key: toggle (press the key next)");
        m.addItem (KeyHold, "play it from a key: while held");
        if (keyName.isNotEmpty()) m.addItem (KeyForget, "forget the key (" + keyName + ")");
        juce::PopupMenu mac; for (int i = 0; i < kMacros; ++i) mac.addItem (200 + i, s.macroNames[(size_t) i], true, d.macro[(size_t) param].index.load() == i);
        mac.addItem (299, "none", true, d.macro[(size_t) param].index.load() < 0);
        m.addSubMenu ("macro", mac);
        m.addSeparator();
        m.addItem (Reset, "reset to default");
        juce::Component::SafePointer<juce::Component> safe (target);
        Session* sp = &s; Track* tp = &t; Device* dp = &d;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [=] (int r)
        {
            if (r == 0 || ! safe) return;
            auto popup = [&] (int modId) { juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (new ModPopup (*sp, *tp, modId)), safe->getScreenBounds(), nullptr); };
            if (r >= 100 && r < 200)
            {
                int idx = r - 100; for (auto& x : *tp->mods) if (x.targets (dp->uid, param)) { if (idx-- == 0) { popup (x.id); break; } }
                return;
            }
            const int made = act (*sp, *tp, *dp, param, r);
            if (made > 0) popup (made);   // the rate, shape and depth are right there
            if (r == Lane && showLane) showLane (1);
        });
    }
};

/** the parameters of one device, as sliders; a header with the name, bypass, window, presets, remove */
class DevicePanel : public juce::Component
{
public:
    DevicePanel (Session& s, Track& t, Device& d, std::function<void (Device&)> openWindow, std::function<void (Device&)> remove, std::function<void (int)> showLane)
        : session (s), track (t), device (d), open (std::move (openWindow)), rem (std::move (remove)), lane (std::move (showLane)),
          bypass ("bypass", studioTheme()), window ("window", studioTheme()), presets ("presets", studioTheme()), removeLink ("remove", studioTheme()), sample ("load sample", studioTheme())
    {
        for (auto* l : { &bypass, &window, &presets, &removeLink }) addAndMakeVisible (l);
        bypass.onClick = [this] { device.bypass.store (! device.bypass.load()); bypass.setLit (device.bypass.load()); session.rebuildLatency(); };
        bypass.setLit (device.bypass.load());
        window.onClick = [this] { if (open) open (device); };
        removeLink.onClick = [this] { if (rem) rem (device); };
        presets.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < device.numPrograms(); ++i) m.addItem (i + 1, device.programName (i));
            if (device.numPrograms() == 0) m.addItem (-1, "no presets", false);
            juce::Component::SafePointer<DevicePanel> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presets), [safe] (int r) { if (safe && r > 0) safe->device.setProgram (r - 1); });
        };
        if (dynamic_cast<SampleHolder*> (device.processor()))
        {
            addAndMakeVisible (sample);
            sample.onClick = [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("load a sample", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) loadSample (c.getResult()); });
            };
        }
        const int n = std::min (device.numParams(), 200);
        for (int i = 0; i < n; ++i)
        {
            auto* sl = sliders.add (new DevSlider (session, track, device, i));
            sl->onMenu = [this] (DevSlider& x) { ParamMenu::show (session, track, device, x.index, &x, lane); };
            addAndMakeVisible (sl);
        }
    }
    bool loadSample (const juce::File& f) { if (auto* h = dynamic_cast<SampleHolder*> (device.processor())) return h->setSampleFile (f); return false; }
    int wanted (int width) const { const int cols = std::max (1, (width - 8) / 264); return 28 + ((sliders.size() + cols - 1) / cols) * 20 + 8; }
    void resized() override
    {
        const int w = getWidth();
        int x = w - 8;
        for (auto* l : { &removeLink, &presets, &window, &bypass }) { const int lw = l == &removeLink ? 56 : l == &presets ? 60 : 56; l->setBounds (x - lw, 4, lw, 20); x -= lw + 4; }
        if (sample.isVisible()) { sample.setBounds (x - 90, 4, 90, 20); }
        const int cols = std::max (1, (w - 8) / 264), cw = (w - 8) / cols;
        for (int i = 0; i < sliders.size(); ++i) sliders[i]->setBounds (8 + (i % cols) * cw, 28 + (i / cols) * 20, cw - 28, 18);
    }
    void paint (juce::Graphics& g) override
    {
        auto t = studioTheme();
        g.setColour (t.ink.withAlpha (0.08f)); g.fillRect (getLocalBounds());
        g.setColour (t.ink); g.setFont (uiFont (11.0f, true));
        juce::String title = device.name;
        if (device.kind == Device::Kind::Hosted || device.kind == Device::Kind::Studio) title << "  " << device.inWidth << ui::U (" → ") << device.outWidth;
        if (auto* h = dynamic_cast<SampleHolder*> (device.processor())) if (h->sampleFile().isNotEmpty()) title << ui::U ("  ·  ") << juce::File (h->sampleFile()).getFileName();
        g.drawText (title, 8, 4, getWidth() - 300, 20, juce::Justification::centredLeft, true);
        g.setColour (t.ink.withAlpha (0.25f)); g.fillRect (4, 26, getWidth() - 8, 1);
    }
    void tick() { for (auto* s : sliders) s->refresh(); bypass.setLit (device.bypass.load()); }
    Session& session; Track& track; Device& device;
private:
    std::function<void (Device&)> open, rem; std::function<void (int)> lane;
    ui::TextLink bypass, window, presets, removeLink, sample;
    juce::OwnedArray<DevSlider> sliders;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace fal::daw

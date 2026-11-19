// Partials: what the window is built from. Part of the faltung series (stdio line, dark mode): a deep blue-slate
// field, slanted bars instead of knobs, lowercase Inter / IBM Plex Mono, thin rules.
#pragma once
#include "Core.h"
#include "BinaryData.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pt::ui
{
// dark mode of the faltung stdio line: deep blue-slate field, light ink, brighter blues
const juce::Colour cBg (0xff1b2731), cFg (0xffe9eef0), cDim (0xff9aabb4), cLine (0xff2c3b46), cTrack (0xff3a4b57), cHot (0xffef7a3c),
                   cFill (0xff5aaad8), cChev (0xff9fd3ec);
const juce::Colour cOval[4] = { juce::Colour (0xff58a2ec), juce::Colour (0xffef7a3c), juce::Colour (0xffeba3ab), juce::Colour (0xffa98ad6) };

inline juce::Typeface::Ptr face (const char* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); }
inline juce::Font fnt (float h, bool bold = false)
{
    static auto r = face (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static auto m = face (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    return juce::Font (juce::FontOptions (bold ? m : r).withHeight (h));
}
inline juce::Font mono (float h, bool bold = false)
{
    static auto r = face (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    static auto m = face (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
    return juce::Font (juce::FontOptions (bold ? m : r).withHeight (h));
}
// type sizes of the window (it is laid out 1857 wide and scaled to fit)
constexpr float kLabel = 14.0f, kValue = 12.5f, kCaption = 13.5f;

/** the faltung parallelogram */
inline juce::Path skew (juce::Rectangle<float> r, float k = 0.55f)
{
    const float d = r.getHeight() * k; juce::Path p;
    p.startNewSubPath (r.getX() + d, r.getY()); p.lineTo (r.getRight() + d, r.getY()); p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath();
    return p;
}
/** the same, standing up (for the vertical bars) */
inline juce::Path skewV (juce::Rectangle<float> r, float k = 0.55f)
{
    const float d = r.getWidth() * k; juce::Path p;
    p.startNewSubPath (r.getX(), r.getY() + d); p.lineTo (r.getRight(), r.getY()); p.lineTo (r.getRight(), r.getBottom() - d); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath();
    return p;
}
inline void chevrons (juce::Graphics& g, juce::Rectangle<float> b, int n, float alpha)
{
    for (int i = 0; i < n; ++i)
    {
        const float w = b.getWidth() * 0.16f, h = b.getHeight() * 0.9f, d = h * 0.42f;
        const float x = b.getX() + b.getWidth() * 0.12f + (float) i * b.getWidth() * 0.13f, y = b.getY() + b.getHeight() * 0.05f;
        juce::Path c; c.startNewSubPath (x, y + h); c.lineTo (x + w, y + h); c.lineTo (x + w + d, y + h * 0.5f); c.lineTo (x + w, y); c.lineTo (x, y); c.lineTo (x + d, y + h * 0.5f); c.closeSubPath();
        g.setColour (cChev.withAlpha (alpha * (i % 2 ? 1.0f : 0.75f))); g.fillPath (c);
    }
}
inline void cube (juce::Graphics& g, float x, float y, float s)
{
    auto P = [&] (float a, float b) { return juce::Point<float> (x + a * s, y + b * s); };
    juce::Path t, l, r;
    t.startNewSubPath (P (0, 30)); t.lineTo (P (52, 0)); t.lineTo (P (104, 30)); t.lineTo (P (52, 60)); t.closeSubPath();
    l.startNewSubPath (P (0, 30)); l.lineTo (P (52, 60)); l.lineTo (P (52, 112)); l.lineTo (P (0, 82)); l.closeSubPath();
    r.startNewSubPath (P (52, 60)); r.lineTo (P (104, 30)); r.lineTo (P (104, 82)); r.lineTo (P (52, 112)); r.closeSubPath();
    g.setColour (juce::Colour (0xffffe15a)); g.fillPath (t);
    g.setColour (juce::Colour (0xfff2c100)); g.fillPath (l);
    g.setColour (juce::Colour (0xffd8a800)); g.fillPath (r);
}
inline juce::String U (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
inline const char* const kDown = " \xe2\x96\xbe";

inline juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
inline juce::String hzStr (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
inline juce::String timeStr (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s"; }
/** a value squeezed to fit over a narrow bar: "20.0 kHz" becomes "20.0k" */
inline juce::String compact (juce::String t)
{
    t = t.toLowerCase().replace (" khz", "k").replace (" steps", "st").replace (" abs", "a").replace (" rel", "r");
    return t.removeCharacters (" ");
}

inline juce::String fmtParam (const PDef& d, float v)
{
    const juce::String id (d.id);
    if (d.kind == C) { auto ch = juce::StringArray::fromTokens (d.choices, "|", ""); return ch[juce::jlimit (0, ch.size() - 1, (int) v)]; }
    if (d.kind == B) return v > 0.5f ? "on" : "off";
    if (d.kind == I) return juce::String ((int) v);
    if (id == "f1Freq" || id == "f2Freq") return hzStr (20.0f * std::pow (1000.0f, v));
    if (id == "envVol" || id == "vol" || id == "masterVol" || id == "fxPre" || id == "fxPost") return v <= -59.9f ? juce::String ("-inf") : juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    if (id == "pitch" || id == "gPitch" || id.endsWith ("ResOfs") || id == "imgFormant" || id == "vibDepth") return juce::String (juce::roundToInt (v)) + " ct";
    if (id == "glideLimit" || id == "legLimit") return v < 1.0f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " ct";
    if (id == "vibSpeed") return juce::String (v, 1) + " Hz";
    if (id == "phaseStart" || id == "tremGap") return juce::String (juce::roundToInt (v)) + U ("\xc2\xb0");
    if (id == "tremSpeed") return timeStr (0.016f * std::pow (1000.0f, v));
    if (id == "declick") return juce::String (v, 1) + " ms";
    if (id == "glideTime" || id == "predelay") { if (std::abs (v) < 0.004f) return "off"; return v < 0 ? timeStr (v * v * (id == "glideTime" ? 1.5f : 1.0f)) : juce::String (v * 16.0f, 1) + " steps"; }
    if (id == "ramp") { if (std::abs (v) < 0.004f) return "0 ms"; return juce::String (juce::roundToInt (std::abs (v) * 200.0f)) + (v < 0 ? " ms abs" : " ms rel"); }
    if (id == "dlTime") return v >= 0 ? juce::String ((int) juce::jmax (1.0f, std::round (v * 16.0f))) + " steps" : timeStr (v * v * 2.0f);
    if (id == "phSpeed") { if (std::abs (v) <= 0.02f) return "still"; return juce::String (v > 0 ? "+" : "-") + juce::String (std::pow (128.0f, 1.0f - std::abs (v)), 1) + " st"; }
    if (id == "imgCoarse") return "x" + juce::String (std::pow (64.0f, v), 3);
    if (id == "freqMul" || id == "detMul") return juce::String (v, 4);
    if (id == "rvDecay") return timeStr (0.1f * std::pow (512.0f, v));
    if (id == "rvPredelay") return timeStr (v * 0.3f);
    if (id == "distHiCut") return v >= 0.995f ? juce::String ("off") : hzStr (500.0f * std::pow (40.0f, v));
    if (id.endsWith ("LowCut") || id.endsWith ("HiCut") || id == "rvDamp") return hzStr (20.0f * std::pow (1000.0f, v));
    if (id == "uniPhase" && v >= 0.99f) return "blur";
    if (id == "pluckTime" && v >= 0.999f) return "off";
    if ((d.lo == 0 || d.lo == -1 || d.lo == -2) && d.hi <= 2) return pct (v);
    return juce::String (v, 2);
}

//==================================================================================================
/** Menus in the window's own colours and type. */
struct Look : juce::LookAndFeel_V4
{
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, cBg.brighter (0.06f)); setColour (juce::PopupMenu::textColourId, cFg);
        setColour (juce::PopupMenu::headerTextColourId, cDim); setColour (juce::PopupMenu::highlightedBackgroundColourId, cFill.withAlpha (0.85f));
        setColour (juce::PopupMenu::highlightedTextColourId, cFg);
        setColour (juce::TextEditor::backgroundColourId, cBg); setColour (juce::TextEditor::textColourId, cFg);
        setColour (juce::TextEditor::outlineColourId, cLine); setColour (juce::TextEditor::focusedOutlineColourId, cFill);
        setColour (juce::TextEditor::highlightColourId, cFill.withAlpha (0.4f)); setColour (juce::CaretComponent::caretColourId, cFg);
        setColour (juce::ScrollBar::thumbColourId, cTrack);
    }
    juce::Font getPopupMenuFont() override { return fnt (15.0f); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override { g.fillAll (findColour (juce::PopupMenu::backgroundColourId)); g.setColour (cLine.brighter (0.2f)); g.drawRect (0, 0, w, h, 1); }
};

//==================================================================================================
/** One control. Four shapes of the same thing:
    Inline  label · slanted bar · value on one line (the pages along the bottom)
    Cell    value over a slanted bar over the label (where a round control would sit)
    Vert    a standing slanted bar (where a fader would sit)
    Num     a number to drag
    Menus (a choice) and switches (on / off) use the same class. */
struct Row : juce::Component
{
    enum Style { Inline, Cell, Vert, Num };
    Style style = Inline; bool big = false, mini = false, plainMenu = false, boxFirst = false;
    juce::String label, hintName; Kind kind = F; float lo = 0, hi = 1, def = 0; juce::StringArray choices;
    std::function<float()> get; std::function<void (float)> set; std::function<juce::String (float)> fmt; std::function<void()> begin, end;
    std::function<float (float)> toNorm, fromNorm;                 // for bars whose travel is not linear
    std::function<void (juce::PopupMenu&)> menuExtra; std::function<void (int)> menuResult;
    std::function<void (const juce::String&)> onHint; std::function<bool()> lit; std::function<juce::String (float)> hintExtra;
    juce::LookAndFeel* look = nullptr;
    float shown = -1.0e9f, startN = 0; int labelW = 80, valueW = 70; bool gesture = false, over = false, shownLit = false;

    float value() const { return get ? get() : 0.0f; }
    float norm (float v) const { return toNorm ? toNorm (v) : (hi > lo ? clamp01 ((v - lo) / (hi - lo)) : 0.0f); }
    float denorm (float n) const { return fromNorm ? fromNorm (clamp01 (n)) : lo + clamp01 (n) * (hi - lo); }
    juce::String text (float v) const { return fmt ? fmt (v) : juce::String (v, 2); }
    void refresh() { const float v = value(); const bool l = lit && lit(); if (v != shown || l != shownLit) repaint(); }
    void sendHint() { if (onHint) { const float v = value(); onHint ((hintName.isNotEmpty() ? hintName : label) + "   " + text (v) + (hintExtra ? hintExtra (v) : juce::String())); } }

    void bar (juce::Graphics& g, float bx, float cy, float bw, float n, float thick)
    {
        // thin rule · slanted fill · ink edge (the faltung slider)
        g.setColour (cFg.withAlpha (0.3f)); g.fillRect (bx, cy, bw, 1.0f);
        const float zero = (lo < 0 && ! toNorm) ? norm (0.0f) : (toNorm ? norm (def) : 0.0f);
        const float x0 = bx + juce::jmin (n, zero) * bw, x1 = bx + juce::jmax (n, zero) * bw;
        if (x1 - x0 > 0.5f) { g.setColour (cFill.withAlpha (gesture ? 1.0f : 0.85f)); g.fillPath (skew ({ x0, cy - thick * 0.5f, x1 - x0, thick })); }
        g.setColour (cFg); g.fillRect (bx + n * bw - 1.0f, cy - thick * 0.5f - 2.0f, 2.0f, thick + 5.0f);
    }
    void paint (juce::Graphics& g) override
    {
        const float v = value(); shown = v; shownLit = lit && lit();
        const int h = getHeight(), w = getWidth();
        const juce::String txt = text (v).toLowerCase();
        const juce::String lab = label.toLowerCase();
        auto mark = [&] (float x, float y) { if (shownLit) { g.setColour (cHot); g.fillPath (skew ({ x, y, 6.0f, 5.0f })); } };

        if (kind == B)
        {
            const float bwid = 17.0f, bh = 10.0f;
            if (style == Inline && ! boxFirst)
            {
                g.setFont (fnt (kLabel)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, 0, 0, w - 26, h, juce::Justification::centredLeft);
                const auto r = juce::Rectangle<float> ((float) w - 24.0f, (float) h * 0.5f - bh * 0.5f, bwid, bh);
                if (v > 0.5f) { g.setColour (cFill); g.fillPath (skew (r)); }
                g.setColour (cFg); g.strokePath (skew (r), juce::PathStrokeType (1.0f));
            }
            else
            {
                const auto r = juce::Rectangle<float> (1.0f, (float) h * 0.5f - bh * 0.5f, bwid, bh);
                if (v > 0.5f) { g.setColour (cFill); g.fillPath (skew (r)); }
                g.setColour (cFg); g.strokePath (skew (r), juce::PathStrokeType (1.0f));
                g.setFont (fnt (mini ? 12.5f : kLabel)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, 28, 0, w - 28, h, juce::Justification::centredLeft);
            }
            return;
        }
        if (kind == C)
        {
            g.setColour (cFg); g.setFont (mono (kValue + (big ? 1.0f : 0.0f)));
            if (style == Inline && labelW > 0)
            {
                g.setFont (fnt (kLabel)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, 0, 0, labelW, h, juce::Justification::centredLeft);
                g.setColour (cFg); g.setFont (mono (kValue));
                g.drawText (txt, labelW, 0, w - labelW, h, juce::Justification::centredRight);
                g.setColour (cFg.withAlpha (0.3f)); g.fillRect ((float) labelW + 4.0f, (float) h - 2.0f, (float) (w - labelW - 4), 1.0f);
            }
            else
            {
                g.drawText (txt + U (kDown), 2, 0, w - 4, h - 2, plainMenu ? juce::Justification::centredRight : juce::Justification::centredLeft);
                if (! plainMenu) { g.setColour (cFg.withAlpha (0.3f)); g.fillRect (0.0f, (float) h - 2.0f, (float) w, 1.0f); }
            }
            return;
        }
        const float n = norm (v);
        switch (style)
        {
            case Inline:
            {
                g.setFont (fnt (kLabel)); g.setColour (cFg.withAlpha (0.82f));
                g.drawText (lab, 0, 0, labelW, h, juce::Justification::centredLeft);
                const int bx = labelW, bw = juce::jmax (4, w - labelW - valueW - (valueW > 0 ? 8 : 4));
                bar (g, (float) bx, (float) (h / 2), (float) bw, n, 8.0f);
                g.setColour (cFg); g.setFont (mono (kValue));
                if (valueW > 0) g.drawText (txt, w - valueW, 0, valueW, h, juce::Justification::centredRight);
                mark ((float) labelW - 12.0f, (float) h * 0.5f - 2.5f);
                break;
            }
            case Cell:
            {
                if (mini)
                {
                    bar (g, 3.0f, 8.0f, (float) w - 8.0f, n, 6.0f);
                    g.setFont (fnt (12.5f)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, -6, 14, w + 12, h - 14, juce::Justification::centred);
                    mark ((float) w - 8.0f, (float) h - 8.0f);
                    break;
                }
                const float vh = 16.0f, lh = 18.0f;
                g.setColour (over || gesture ? cFg : cFg.withAlpha (0.78f)); g.setFont (mono (kValue));
                g.drawText (compact (txt), -10, 0, w + 20, (int) vh, juce::Justification::centred);
                const float cy = vh + ((float) h - vh - lh) * 0.5f;
                bar (g, 4.0f, cy, (float) w - 10.0f, n, big ? 11.0f : 8.0f);
                g.setFont (fnt (big ? kLabel + 1.0f : kLabel, big)); g.setColour (cFg.withAlpha (big ? 1.0f : 0.82f));
                g.drawText (lab, -8, h - (int) lh, w + 16, (int) lh, juce::Justification::centred);
                mark ((float) w - 9.0f, (float) h - lh - 6.0f);
                break;
            }
            case Vert:
            {
                const float lh = label.isNotEmpty() ? 18.0f : 0.0f, top = 6.0f, bot = (float) h - lh - 5.0f, cx = (float) w * 0.5f, len = bot - top;
                g.setColour (cFg.withAlpha (0.3f)); g.fillRect (cx, top, 1.0f, len);
                const float zero = lo < 0 ? norm (0.0f) : 0.0f;
                const float y0 = bot - juce::jmax (n, zero) * len, y1 = bot - juce::jmin (n, zero) * len;
                if (y1 - y0 > 0.5f) { g.setColour (cFill.withAlpha (gesture ? 1.0f : 0.85f)); g.fillPath (skewV ({ cx - 4.0f, y0 - 2.0f, 8.0f, y1 - y0 + 4.0f })); }
                g.setColour (cFg); g.fillRect (cx - 7.0f, bot - n * len - 1.0f, 15.0f, 2.0f);
                if (lh > 0) { g.setFont (fnt (kLabel)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, 0, h - (int) lh, w, (int) lh, juce::Justification::centred); }
                if (over || gesture) { g.setColour (cFg); g.setFont (mono (11.5f)); g.drawText (compact (txt), -20, juce::jlimit (0, h - 30, (int) (bot - n * len) - 16), w + 40, 13, juce::Justification::centred); }
                mark (cx + 5.0f, 2.0f);
                break;
            }
            case Num:
            {
                int x = 0;
                if (labelW > 0 && label.isNotEmpty()) { g.setFont (fnt (kLabel)); g.setColour (cFg.withAlpha (0.82f)); g.drawText (lab, 0, 0, labelW, h, juce::Justification::centredLeft); x = labelW; }
                g.setColour (cFill.withAlpha (gesture ? 0.35f : 0.16f)); g.fillPath (skew ({ (float) x + 1.0f, 2.0f, (float) (w - x) - 6.0f, (float) h - 5.0f }, 0.18f));
                g.setColour (cFg); g.setFont (mono (kValue + 1.0f, true)); g.drawText (txt, x, 0, w - x, h - 1, juce::Justification::centred);
                g.setColour (cFg.withAlpha (0.3f)); g.fillRect ((float) x, (float) h - 2.0f, (float) (w - x), 1.0f);
                break;
            }
        }
    }
    void change (float v)
    {
        v = clampf (v, lo, hi);
        if (kind != F) v = std::round (v);
        if (set) set (v);
        repaint(); sendHint();
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; sendHint(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; if (onHint) onHint ({}); repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { popup(); return; }
        const float v = value();
        if (kind == B) { if (begin) begin(); change (v > 0.5f ? 0.0f : 1.0f); if (end) end(); return; }
        if (kind == C)
        {
            juce::PopupMenu m; if (look) m.setLookAndFeel (look);
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == (int) v);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) { if (begin) begin(); change ((float) (r - 1)); if (end) end(); } });
            return;
        }
        startN = norm (v); startV = v; gesture = true; if (begin) begin();
    }
    float startV = 0;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! gesture) return;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        if (style == Num || kind == I)
        {
            // whole numbers: a step every few pixels, however long the bar is
            const float steps = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY()) * fine;
            if (kind == I) { change (startV + std::round (steps / juce::jmax (4.0f, 140.0f / juce::jmax (1.0f, hi - lo)))); return; }
            change (denorm (startN + steps / 260.0f)); return;
        }
        float travel = 160.0f;
        if (style == Inline) travel = (float) juce::jmax (60, getWidth() - labelW - valueW - 6);
        else if (style == Vert) travel = (float) juce::jmax (60, getHeight() - 30);
        const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY()) / travel * fine;
        change (denorm (startN + d));
    }
    void mouseUp (const juce::MouseEvent&) override { if (gesture) { gesture = false; if (end) end(); repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (kind == F || kind == I) { if (begin) begin(); change (def); if (end) end(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wh) override
    {
        if (kind == B || std::abs (wh.deltaY) <= 0.0f) { Component::mouseWheelMove (e, wh); return; }
        const float v = value();
        if (begin) begin();
        if (kind == F) change (denorm (norm (v) + (wh.deltaY > 0 ? 0.01f : -0.01f))); else change (v + (wh.deltaY > 0 ? 1.0f : -1.0f));
        if (end) end();
    }
    void popup()
    {
        juce::PopupMenu m; if (look) m.setLookAndFeel (look);
        m.addItem (1, "Reset");
        if (menuExtra) menuExtra (m);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 1) { if (begin) begin(); change (def); if (end) end(); }
            else if (r > 1 && menuResult) menuResult (r);
        });
    }
};

/** A two- or three-way switch written out in words: the one in use is filled. */
struct Seg : juce::Component
{
    juce::StringArray items; bool vertical = false; juce::String hintName;
    std::function<int()> get; std::function<void (int)> set; std::function<void (const juce::String&)> onHint;
    int shown = -1;
    void refresh() { if (get && get() != shown) repaint(); }
    juce::Rectangle<int> cellAt (int i) const
    {
        const int n = juce::jmax (1, items.size());
        return vertical ? juce::Rectangle<int> (0, i * getHeight() / n, getWidth(), getHeight() / n) : juce::Rectangle<int> (i * getWidth() / n, 0, getWidth() / n, getHeight());
    }
    void paint (juce::Graphics& g) override
    {
        const int cur = get ? get() : 0; shown = cur;
        for (int i = 0; i < items.size(); ++i)
        {
            const auto r = cellAt (i).toFloat();
            const auto box = r.reduced (2.0f, 2.0f).withWidth (r.getWidth() - 4.0f - r.getHeight() * 0.3f);
            if (i == cur) { g.setColour (cFill); g.fillPath (skew (box, 0.3f)); }
            else { g.setColour (cFg.withAlpha (0.35f)); g.strokePath (skew (box, 0.3f), juce::PathStrokeType (1.0f)); }
            g.setColour (i == cur ? cFg : cFg.withAlpha (0.7f)); g.setFont (mono (12.0f, i == cur));
            g.drawText (items[i].toLowerCase(), cellAt (i), juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < items.size(); ++i) if (cellAt (i).contains (e.getPosition())) { if (set) set (i); repaint(); }
        mouseEnter (e);
    }
    void mouseEnter (const juce::MouseEvent&) override { if (onHint) onHint (hintName + "   " + items[juce::jlimit (0, juce::jmax (0, items.size() - 1), get ? get() : 0)].toLowerCase()); }
    void mouseExit (const juce::MouseEvent&) override { if (onHint) onHint ({}); }
};

/** A button: a slanted outline, filled when on. It can carry a little drawing instead of a word. */
struct Btn : juce::Component
{
    juce::String text, hintText; std::function<bool()> isOn; std::function<void()> click; std::function<void()> rightClick; bool boxed = true; float textSize = 13.0f;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool)> draw; std::function<void (const juce::String&)> onHint;
    bool shownOn = false;
    void refresh() { const bool on = isOn && isOn(); if (on != shownOn) repaint(); }
    void paint (juce::Graphics& g) override
    {
        const bool on = isOn && isOn(); shownOn = on;
        const auto r = getLocalBounds().toFloat().reduced (5.0f, 1.0f).withX (2.0f).withWidth ((float) getWidth() - 4.0f - (float) getHeight() * 0.35f);
        auto shape = skew (r, 0.35f);
        if (on) { g.setColour (cFill); g.fillPath (shape); }
        else if (boxed) { g.setColour (cFg.withAlpha (0.8f)); g.strokePath (shape, juce::PathStrokeType (1.0f)); }
        if (draw) { draw (g, getLocalBounds().toFloat(), on); return; }
        g.setColour (cFg); g.setFont (mono (textSize, on));
        g.drawText (text.toLowerCase(), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && rightClick) rightClick(); else if (click) click(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { if (onHint && hintText.isNotEmpty()) onHint (hintText); }
    void mouseExit (const juce::MouseEvent&) override { if (onHint) onHint ({}); }
};

/** A titled group of inline rows, for the pages along the bottom. */
struct Section : juce::Component
{
    juce::String title; std::unique_ptr<Row> head; juce::OwnedArray<Row> rows; int rowH = 21, headW = 0;
    int wanted() const { return 26 + rows.size() * rowH; }
    void paint (juce::Graphics& g) override
    {
        g.setColour (cFg); g.setFont (mono (kCaption, true));
        g.drawText (title.toLowerCase(), 0, 0, getWidth(), 18, juce::Justification::centredLeft);
        g.setColour (cFill.withAlpha (0.7f)); g.fillRect (0, 20, getWidth(), 1);
    }
    void resized() override
    {
        const int hw = headW > 0 ? headW : getWidth() - 96;
        if (head) head->setBounds (getWidth() - hw, 0, hw, 19);
        for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, 25 + i * rowH, getWidth(), rowH);
    }
};
} // namespace pt::ui

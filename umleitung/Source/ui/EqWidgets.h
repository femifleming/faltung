#pragma once
// umleitung — the system eq's controls, made for precision: straight faders and sliders (no slanted fills), every
// value in a box you can click and type into, shift-drag for fine steps and cmd-drag for finer still, double-click
// to reset; a response plot with an exact scale and a readout under the mouse; a table of parametric bands with
// every number typed; a table of speakers.
#include "Widgets.h"

namespace um::ui
{
/** a number you can click and type: shows text, edits in place */
class ValueBox : public juce::Component
{
public:
    ValueBox (Theme t) : theme (t) { addChildComponent (ed); ed.setFont (mono (11.0f)); ed.setJustification (juce::Justification::centredRight);
        ed.setColour (juce::TextEditor::backgroundColourId, theme.ground.brighter (0.25f)); ed.setColour (juce::TextEditor::textColourId, theme.ink); ed.setColour (juce::TextEditor::outlineColourId, theme.ink.withAlpha (0.5f));
        ed.onReturnKey = [this] { commit(); }; ed.onFocusLost = [this] { commit(); }; ed.onEscapeKey = [this] { ed.setVisible (false); }; }
    void setText (const juce::String& t) { text = t; repaint(); }
    std::function<void (const juce::String&)> onTyped;
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (getLocalBounds());
        g.setColour (theme.ink.withAlpha (0.35f)); g.drawRect (getLocalBounds(), 1);
        g.setColour (theme.ink); g.setFont (mono (getWidth() < 46 ? 9.0f : 11.0f)); g.drawText (text, getLocalBounds().reduced (getWidth() < 46 ? 1 : 4, 0), getWidth() < 46 ? juce::Justification::centred : juce::Justification::centredRight, false);
    }
    void resized() override { ed.setBounds (getLocalBounds()); }
    void mouseDown (const juce::MouseEvent&) override { ed.setText (text.upToFirstOccurrenceOf (" ", false, false), false); ed.setVisible (true); ed.grabKeyboardFocus(); ed.selectAll(); }
private:
    void commit() { if (! ed.isVisible()) return; ed.setVisible (false); if (onTyped) onTyped (ed.getText()); }
    Theme theme; juce::String text; juce::TextEditor ed;
};

/** a straight horizontal slider with a typed value: label · track · value box */
class PreciseSlider : public juce::Component
{
public:
    PreciseSlider (Theme t, juce::String l, double lo, double hi, double def, int decimals, juce::String unit)
        : theme (t), label (std::move (l)), min (lo), max (hi), defaultValue (def), places (decimals), units (std::move (unit)), box (t)
    {
        addAndMakeVisible (box);
        box.onTyped = [this] (const juce::String& s) { if (s.trim().isNotEmpty()) set (s.getDoubleValue(), true); };
        value = def; box.setText (format());
    }
    double get() const { return value; }
    void set (double v, bool notify)
    {
        v = juce::jlimit (min, max, std::round (v * std::pow (10.0, places)) / std::pow (10.0, places));
        value = v; box.setText (format()); repaint();
        if (notify && onChange) onChange (v);
    }
    std::function<void (double)> onChange;
    int labelW = 92, boxW = 96;
    void resized() override { box.setBounds (getWidth() - boxW, 1, boxW, getHeight() - 2); }
    juce::Rectangle<float> track() const { return { (float) labelW, getHeight() * 0.5f - 2.0f, (float) (getWidth() - labelW - boxW - 12), 4.0f }; }
    float xOf (double v) const { auto t = track(); return t.getX() + t.getWidth() * (float) ((v - min) / (max - min)); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme.ink); g.setFont (mono (11.0f)); g.drawText (label, 0, 0, labelW - 6, getHeight(), juce::Justification::centredLeft, true);
        auto t = track();
        g.setColour (theme.ink.withAlpha (0.18f)); g.fillRect (t);
        const double origin = (min < 0 && max > 0) ? 0.0 : min;
        const float x0 = xOf (origin), x1 = xOf (value);
        g.setColour (theme.fill.darker (0.35f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, x1), t.getY(), std::max (x0, x1), t.getBottom()));
        if (min < 0 && max > 0) { g.setColour (theme.ink.withAlpha (0.5f)); g.fillRect (x0 - 0.5f, t.getY() - 4.0f, 1.0f, t.getHeight() + 8.0f); }
        g.setColour (theme.ink); g.fillRect (x1 - 1.5f, t.getY() - 7.0f, 3.0f, t.getHeight() + 14.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override { dragStart = value; lastX = e.position.x; if (e.getNumberOfClicks() > 1) set (defaultValue, true); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        // a pixel is a step of the track; shift: a tenth of that; cmd: a hundredth
        const double per = (max - min) / std::max (40.0f, track().getWidth()) * (e.mods.isCommandDown() ? 0.01 : e.mods.isShiftDown() ? 0.1 : 1.0);
        set (value + (e.position.x - lastX) * per, true); lastX = e.position.x;
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { const double step = std::pow (10.0, -places) * (e.mods.isShiftDown() ? 1.0 : 10.0); set (value + (w.deltaY > 0 ? step : -step), true); }
private:
    juce::String format() const { return juce::String (value, places) + (units.isNotEmpty() ? " " + units : juce::String()); }
    Theme theme; juce::String label; double min, max, defaultValue; int places; juce::String units; double value = 0, dragStart = 0; float lastX = 0;
    ValueBox box;
};

/** the graphic eq: a straight vertical fader per band, its value typed in the box under it */
class FaderBank : public juce::Component
{
public:
    FaderBank (Theme t, juce::ValueTree b) : theme (t), block (b) { rebuild(); }
    void rebuild()
    {
        boxes.clear();
        const auto g = Model::graphicBands (block);
        for (size_t i = 0; i < g.size(); ++i)
        {
            auto bx = std::make_unique<ValueBox> (theme); bx->setText (juce::String (g[i], 2));
            bx->onTyped = [this, i] (const juce::String& s) { if (s.trim().isNotEmpty()) set ((int) i, (float) s.getDoubleValue()); };
            addAndMakeVisible (*bx); boxes.push_back (std::move (bx));
        }
        resized(); repaint();
    }
    float range = 12.0f;   // ± db shown
    void resized() override { const int n = (int) boxes.size(); if (n == 0) return; const float w = getWidth() / (float) n; for (int i = 0; i < n; ++i) boxes[(size_t) i]->setBounds ((int) (i * w) + 1, getHeight() - 22, (int) w - 2, 20); }
    float colW() const { return getWidth() / (float) std::max<size_t> (1, boxes.size()); }
    /** 31.5, 125, 1.25k, 10k, 20k */
    static juce::String freqLabel (float hz)
    {
        if (hz < 1000) return std::fmod (hz, 1.0f) != 0 ? juce::String (hz, 1) : juce::String ((int) hz);
        auto t = juce::String (hz / 1000.0f, 2);
        if (t.contains (".")) t = t.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return t + "k";
    }
    juce::Rectangle<float> lane() const { return { 0.0f, 16.0f, (float) getWidth(), (float) getHeight() - 16.0f - 40.0f }; }
    float yOf (float db) const { auto l = lane(); return l.getCentreY() - db / range * l.getHeight() * 0.5f; }
    void paint (juce::Graphics& g) override
    {
        const auto gains = Model::graphicBands (block); const auto& f = dsp::graphicCentres ((int) gains.size());
        auto l = lane();
        g.setFont (mono (9.0f));
        for (float d = -range; d <= range + 0.01f; d += range / 4.0f) { g.setColour (theme.ink.withAlpha (d == 0 ? 0.45f : 0.12f)); g.fillRect (0.0f, yOf (d), (float) getWidth(), 1.0f); }
        for (size_t i = 0; i < gains.size(); ++i)
        {
            const float x = colW() * (i + 0.5f), y = yOf (juce::jlimit (-range, range, gains[i]));
            g.setColour (theme.ink.withAlpha (0.3f)); g.fillRect (x - 0.5f, l.getY(), 1.0f, l.getHeight());
            g.setColour (theme.fill.darker (0.3f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x - colW() * 0.18f, std::min (y, yOf (0)), x + colW() * 0.18f, std::max (y, yOf (0))));
            g.setColour (theme.ink); g.fillRect (x - colW() * 0.3f, y - 1.5f, colW() * 0.6f, 3.0f);
            g.setColour (theme.ink); g.drawText (freqLabel (f[i]), (int) (x - colW() * 0.5f), getHeight() - 38, (int) colW(), 14, juce::Justification::centred);
        }
        g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("+" + juce::String ((int) range) + " db", 2, 0, 60, 14, juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        band = juce::jlimit (0, (int) boxes.size() - 1, (int) (e.x / colW()));
        startDb = Model::graphicBands (block)[(size_t) band]; startY = e.position.y;
        if (e.getNumberOfClicks() > 1) set (band, 0.0f);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (band < 0) return;
        const float perPx = range * 2.0f / lane().getHeight() * (e.mods.isCommandDown() ? 0.01f : e.mods.isShiftDown() ? 0.1f : 1.0f);
        set (band, startDb - (e.position.y - startY) * perPx);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { const int i = juce::jlimit (0, (int) boxes.size() - 1, (int) (e.x / colW())); set (i, Model::graphicBands (block)[(size_t) i] + (w.deltaY > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 0.01f : 0.1f)); }
    std::function<void()> onEdit;
private:
    void set (int i, float v)
    {
        auto g = Model::graphicBands (block); if (i < 0 || i >= (int) g.size()) return;
        g[(size_t) i] = juce::jlimit (-24.0f, 24.0f, std::round (v * 100.0f) / 100.0f);
        block.setProperty (ids::bands, Model::graphicText (g), nullptr);
        boxes[(size_t) i]->setText (juce::String (g[(size_t) i], 2)); repaint(); if (onEdit) onEdit();
    }
    Theme theme; juce::ValueTree block; std::vector<std::unique_ptr<ValueBox>> boxes; int band = -1; float startDb = 0, startY = 0;
};

/** the response: every stage that is on, as the digital filters really do it, left and right; the spectrum behind;
    points to drag for a parametric stage; the value under the mouse */
class ResponsePlot : public juce::Component
{
public:
    ResponsePlot (Theme t, Engine& e) : theme (t), engine (e), spectrum (t) { addAndMakeVisible (spectrum); spectrum.setInterceptsMouseClicks (false, false); spectrum.scale = false; spectrum.showPeaks = false; }
    std::vector<juce::ValueTree> stages;   // eq blocks (graphic / parametric) whose bands add up
    juce::ValueTree editable;              // the parametric stage whose points can be dragged
    float range = 12.0f;
    std::function<void()> onEdit;
    void resized() override { spectrum.setBounds (getLocalBounds()); }
    void tick() { spectrum.tick(); repaint(); }
    void watch (int blockId) { spectrum.src = { &engine, blockId }; }
    double fs() const { return engine.sampleRate() > 0 ? engine.sampleRate() : 48000.0; }
    float xOf (double hz) const { return (float) (std::log (hz / 10.0) / std::log (2400.0)) * (float) getWidth(); }   // 10 hz … 24 khz
    double hzAt (float x) const { return 10.0 * std::pow (2400.0, x / std::max (1, getWidth())); }
    float yOf (double db) const { return (float) getHeight() * (0.5f - (float) db / (range * 2.2f)); }
    double dbAt (float y) const { return (0.5 - y / std::max (1.0, (double) getHeight())) * range * 2.2; }
    double totalDb (double hz, int ch) const
    {
        double d = 0;
        for (auto& b : stages) if (b.isValid() && ! (bool) b[ids::bypass]) { d += dsp::digitalDb (Engine::bandsOf (b), fs(), hz, ch); d += (double) b[ids::db]; }
        return d;
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        g.setFont (mono (9.0f));
        for (double f : { 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        { const float x = xOf (f); g.setColour (theme.ink.withAlpha (0.14f)); g.fillRect (x, 0.0f, 1.0f, h); g.setColour (theme.ink.withAlpha (0.6f)); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x + 3, (int) h - 13, 40, 12, juce::Justification::centredLeft); }
        for (double d = -range; d <= range + 0.001; d += range / 4.0)
        { const float y = yOf (d); g.setColour (theme.ink.withAlpha (std::abs (d) < 1.0e-9 ? 0.45f : 0.12f)); g.fillRect (0.0f, y, w, 1.0f); g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ((d > 0 ? "+" : "") + juce::String (d, range < 6 ? 2 : 1) + " db", 3, (int) y - 12, 70, 11, juce::Justification::centredLeft); }
        // the curves: left (ink) and right (dashed) when they differ
        bool differs = false;
        juce::Path pl, pr;
        for (int x = 0; x < (int) w; x += 2)
        {
            const double hz = hzAt ((float) x), l = totalDb (hz, 0), r = totalDb (hz, 1);
            if (std::abs (l - r) > 1.0e-6) differs = true;
            if (x == 0) { pl.startNewSubPath ((float) x, yOf (l)); pr.startNewSubPath ((float) x, yOf (r)); } else { pl.lineTo ((float) x, yOf (l)); pr.lineTo ((float) x, yOf (r)); }
        }
        g.setColour (theme.ink); g.strokePath (pl, juce::PathStrokeType (2.0f));
        if (differs) { juce::Path d; const float dash[] = { 5.0f, 4.0f }; juce::PathStrokeType (1.6f).createDashedStroke (d, pr, dash, 2); g.setColour (theme.ink.withAlpha (0.8f)); g.fillPath (d); g.drawText (juce::CharPointer_UTF8 ("left \xe2\x80\x94  right - -"), (int) w - 150, 4, 146, 12, juce::Justification::centredRight); }
        // the points of the editable stage
        if (editable.isValid())
        {
            auto bands = Model::peqBands (editable);
            for (int i = 0; i < (int) bands.size(); ++i)
            {
                const auto& b = bands[(size_t) i];
                const float x = xOf (b.freq), y = yOf (b.shape == 3 || b.shape == 4 || b.shape >= 5 ? 0.0 : b.db);
                g.setColour (i == selected ? theme.ink : theme.ground); g.fillEllipse (x - 7, y - 7, 14, 14);
                g.setColour (theme.ink); g.drawEllipse (x - 7, y - 7, 14, 14, 1.5f);
                g.setColour (i == selected ? theme.ground : theme.ink); g.setFont (mono (9.0f, true)); g.drawText (juce::String (i + 1), (int) x - 7, (int) y - 7, 14, 14, juce::Justification::centred);
            }
        }
        // the readout under the mouse
        if (hover.x >= 0)
        {
            const double hz = hzAt ((float) hover.x);
            juce::String t = juce::String (hz, hz < 100 ? 2 : hz < 1000 ? 1 : 0) + " hz   " + (totalDb (hz, 0) >= 0 ? "+" : "") + juce::String (totalDb (hz, 0), 2) + " db";
            if (differs) t << "  /  " << (totalDb (hz, 1) >= 0 ? "+" : "") << juce::String (totalDb (hz, 1), 2) << " db";
            g.setColour (theme.ground.withAlpha (0.9f)); g.fillRect (6, 4, 260, 16);
            g.setColour (theme.ink); g.setFont (mono (11.0f)); g.drawText (t, 10, 4, 256, 16, juce::Justification::centredLeft);
            g.setColour (theme.ink.withAlpha (0.4f)); g.fillRect ((float) hover.x, 0.0f, 1.0f, h);
        }
    }
    int hit (juce::Point<int> p) const
    {
        if (! editable.isValid()) return -1;
        auto bands = Model::peqBands (editable);
        for (int i = 0; i < (int) bands.size(); ++i) { const auto& b = bands[(size_t) i]; if (p.getDistanceFrom ({ (int) xOf (b.freq), (int) yOf (b.shape == 3 || b.shape == 4 || b.shape >= 5 ? 0.0 : b.db) }) < 10) return i; }
        return -1;
    }
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        selected = hit (e.getPosition());
        if (selected >= 0) { auto b = Model::peqBands (editable)[(size_t) selected]; startHz = b.freq; startDb = b.db; startPos = e.position; }
        else if (editable.isValid() && e.getNumberOfClicks() > 1)
        {
            auto v = Model::peqBands (editable); PeqBand b; b.freq = std::round (hzAt (e.position.x)); b.db = std::round (dbAt (e.position.y) * 10.0) / 10.0; b.q = 1.0; v.push_back (b);
            selected = (int) v.size() - 1; store (v);
        }
        if (onSelect) onSelect (selected);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selected < 0) return;
        auto v = Model::peqBands (editable); if (selected >= (int) v.size()) return;
        const double fine = e.mods.isCommandDown() ? 0.01 : e.mods.isShiftDown() ? 0.1 : 1.0;
        const auto d = (e.position - startPos) * (float) fine;
        v[(size_t) selected].freq = juce::jlimit (1.0, fs() * 0.499, hzAt (xOf (startHz) + d.x));
        v[(size_t) selected].freq = std::round (v[(size_t) selected].freq * 100.0) / 100.0;
        if (v[(size_t) selected].shape <= 2 || v[(size_t) selected].shape == 8) v[(size_t) selected].db = juce::jlimit (-30.0, 30.0, std::round ((startDb + (dbAt (yOf (startDb) + d.y) - startDb)) * 100.0) / 100.0);
        store (v);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int i = hit (e.getPosition()); if (i < 0) return;
        auto v = Model::peqBands (editable); v[(size_t) i].q = juce::jlimit (0.05, 50.0, v[(size_t) i].q * std::pow (e.mods.isShiftDown() ? 1.02 : 1.15, w.deltaY > 0 ? 1.0 : -1.0)); store (v);
    }
    std::function<void (int)> onSelect;
    int selected = -1;
private:
    void store (const std::vector<PeqBand>& v) { editable.setProperty (ids::bands, Model::peqText (v), nullptr); repaint(); if (onEdit) onEdit(); }
    Theme theme; Engine& engine; Spectrum spectrum; juce::Point<int> hover { -1, -1 };
    double startHz = 1000, startDb = 0; juce::Point<float> startPos;
};

/** the parametric bands as a table: every number typed */
class BandTable : public juce::Component
{
public:
    BandTable (Theme t, juce::ValueTree b) : theme (t), block (b) { rebuild(); }
    std::function<void()> onEdit;
    int highlighted = -1;
    void rebuild()
    {
        cells.clear(); rowsN = 0;
        auto bands = Model::peqBands (block);
        for (int i = 0; i < (int) bands.size(); ++i)
        {
            const auto& b = bands[(size_t) i];
            auto num = [&] (double v, int places, std::function<void (PeqBand&, double)> fn)
            {
                auto bx = std::make_unique<ValueBox> (theme); bx->setText (juce::String (v, places));
                bx->onTyped = [this, i, fn] (const juce::String& s) { if (s.trim().isEmpty()) return; auto v2 = Model::peqBands (block); if (i < (int) v2.size()) { fn (v2[(size_t) i], s.getDoubleValue()); store (v2); } };
                addAndMakeVisible (*bx); cells.push_back (std::move (bx));
            };
            num (b.freq, 2, [] (PeqBand& x, double v) { x.freq = juce::jlimit (1.0, 96000.0, v); });
            num (b.db, 2, [] (PeqBand& x, double v) { x.db = juce::jlimit (-40.0, 40.0, v); });
            num (b.q, 3, [] (PeqBand& x, double v) { x.q = juce::jlimit (0.025, 100.0, v); });
            ++rowsN;
        }
        setSize (getWidth(), 24 + rowsN * 24 + 28);
        resized(); repaint();
    }
    static constexpr int cOn = 8, cShape = 34, cFreq = 150, cGain = 250, cQ = 340, cSlope = 430, cCh = 560, cDel = 640;
    void resized() override
    {
        for (int i = 0; i < rowsN; ++i)
        {
            const int y = 24 + i * 24;
            if ((size_t) (i * 3 + 2) < cells.size()) { cells[(size_t) i * 3]->setBounds (cFreq, y + 1, 92, 21); cells[(size_t) i * 3 + 1]->setBounds (cGain, y + 1, 82, 21); cells[(size_t) i * 3 + 2]->setBounds (cQ, y + 1, 82, 21); }
        }
    }
    void paint (juce::Graphics& g) override
    {
        auto bands = Model::peqBands (block);
        g.setFont (mono (10.0f)); g.setColour (theme.ink.withAlpha (0.62f));
        g.drawText ("on", cOn, 4, 30, 14, juce::Justification::centredLeft); g.drawText ("type", cShape, 4, 100, 14, juce::Justification::centredLeft);
        g.drawText ("frequency (hz)", cFreq, 4, 100, 14, juce::Justification::centredLeft); g.drawText ("gain (db)", cGain, 4, 90, 14, juce::Justification::centredLeft);
        g.drawText ("q", cQ, 4, 90, 14, juce::Justification::centredLeft); g.drawText ("slope", cSlope, 4, 120, 14, juce::Justification::centredLeft); g.drawText ("channel", cCh, 4, 80, 14, juce::Justification::centredLeft);
        for (int i = 0; i < (int) bands.size(); ++i)
        {
            const auto& b = bands[(size_t) i]; const int y = 24 + i * 24;
            if (i == highlighted) { g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (0, y, getWidth(), 23); }
            g.setColour (theme.ink); g.drawRect (cOn + 4, y + 6, 12, 12, 1); if (b.on) g.fillRect (cOn + 7, y + 9, 6, 6);
            g.setFont (mono (11.0f)); g.drawText (juce::String (i + 1) + "  " + peqShapeName (b.shape), cShape, y, 110, 23, juce::Justification::centredLeft);
            const bool cut = b.shape == 3 || b.shape == 4;
            g.setColour (theme.ink.withAlpha (cut ? 1.0f : 0.3f)); g.drawText (cut ? juce::String (b.slope) + " db/oct " + (b.kind == 1 ? "lr" : "bw") : juce::String ("-"), cSlope, y, 120, 23, juce::Justification::centredLeft);
            g.setColour (theme.ink); g.drawText (b.ch == 0 ? "both" : b.ch == 1 ? "left" : b.ch == 2 ? "right" : "ch " + juce::String (b.ch), cCh, y, 80, 23, juce::Justification::centredLeft);
            g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("remove", cDel, y, 60, 23, juce::Justification::centredLeft);
        }
        g.setColour (theme.ink); g.setFont (mono (11.0f));
        g.drawText ("+ band", cOn, 24 + (int) bands.size() * 24 + 4, 80, 20, juce::Justification::centredLeft);
        if (bands.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("no bands: + band, or double-click the curve", 100, 28, 400, 18, juce::Justification::centredLeft); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto v = Model::peqBands (block);
        const int i = (e.y - 24) / 24;
        if (e.y >= 24 + (int) v.size() * 24 && e.y < 24 + (int) v.size() * 24 + 26 && e.x < cOn + 80) { PeqBand b; v.push_back (b); store (v); rebuild(); return; }
        if (i < 0 || i >= (int) v.size()) return;
        auto& b = v[(size_t) i];
        if (e.x < cShape) { b.on = ! b.on; store (v); return; }
        if (e.x < cFreq) { juce::PopupMenu m; for (int s = 0; s < kPeqShapes; ++s) m.addItem (s + 1, peqShapeName (s), true, b.shape == s);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r) { auto v2 = Model::peqBands (block); if (r > 0 && i < (int) v2.size()) { v2[(size_t) i].shape = r - 1; store (v2); } }); return; }
        if (e.x >= cSlope && e.x < cCh && (b.shape == 3 || b.shape == 4))
        {
            juce::PopupMenu m; int k = 1;
            for (int sl : { 6, 12, 18, 24, 30, 36, 42, 48 }) m.addItem (k++, juce::String (sl) + " db/oct butterworth", true, b.slope == sl && b.kind == 0);
            m.addSeparator(); for (int sl : { 12, 24, 48 }) m.addItem (100 + sl, juce::String (sl) + " db/oct linkwitz-riley", true, b.slope == sl && b.kind == 1);
            m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r) { auto v2 = Model::peqBands (block); if (r <= 0 || i >= (int) v2.size()) return; if (r >= 100) { v2[(size_t) i].slope = r - 100; v2[(size_t) i].kind = 1; } else { v2[(size_t) i].slope = 6 * r; v2[(size_t) i].kind = 0; } store (v2); });
            return;
        }
        if (e.x >= cCh && e.x < cDel) { b.ch = (b.ch + 1) % 3; store (v); return; }
        if (e.x >= cDel && e.x < cDel + 60) { v.erase (v.begin() + i); store (v); rebuild(); return; }
    }
private:
    void store (const std::vector<PeqBand>& v) { block.setProperty (ids::bands, Model::peqText (v), nullptr); for (int i = 0; i < (int) v.size() && (size_t) (i * 3 + 2) < cells.size(); ++i) { cells[(size_t) i * 3]->setText (juce::String (v[(size_t) i].freq, 2)); cells[(size_t) i * 3 + 1]->setText (juce::String (v[(size_t) i].db, 2)); cells[(size_t) i * 3 + 2]->setText (juce::String (v[(size_t) i].q, 3)); } repaint(); if (onEdit) onEdit(); }
    Theme theme; juce::ValueTree block; std::vector<std::unique_ptr<ValueBox>> cells; int rowsN = 0;
};

/** speakers: per channel trim, delay (ms, samples, distance), polarity, mute */
class SpeakerTable : public juce::Component
{
public:
    SpeakerTable (Theme t, juce::ValueTree b, int channels, double rate) : theme (t), block (b), sr (rate)
    {
        auto v = Model::speakers (b); v.resize ((size_t) std::max (2, channels));
        for (int c = 0; c < (int) v.size(); ++c)
        {
            auto trim = std::make_unique<PreciseSlider> (theme, c == 0 ? "left" : c == 1 ? "right" : "ch " + juce::String (c + 1), -24.0, 12.0, 0.0, 2, "db");
            trim->set (v[(size_t) c].db, false); trim->onChange = [this, c] (double x) { auto w = all(); w[(size_t) c].db = x; store (w); };
            auto delay = std::make_unique<PreciseSlider> (theme, "delay", 0.0, 50.0, 0.0, 3, "ms");
            delay->set (v[(size_t) c].ms, false); delay->onChange = [this, c] (double x) { auto w = all(); w[(size_t) c].ms = x; store (w); };
            addAndMakeVisible (*trim); addAndMakeVisible (*delay);
            trims.push_back (std::move (trim)); delays.push_back (std::move (delay));
        }
        n = (int) v.size();
        store (v, false);
    }
    void resized() override { for (int c = 0; c < n; ++c) { trims[(size_t) c]->setBounds (0, 24 + c * 60, getWidth() / 2 - 10, 24); delays[(size_t) c]->setBounds (getWidth() / 2 + 10, 24 + c * 60, getWidth() / 2 - 10, 24); } }
    void paint (juce::Graphics& g) override
    {
        auto v = all();
        g.setFont (mono (10.0f)); g.setColour (theme.ink.withAlpha (0.62f));
        g.drawText ("level trim", 0, 2, 200, 14, juce::Justification::centredLeft); g.drawText ("delay (to a fraction of a sample)", getWidth() / 2 + 10, 2, 300, 14, juce::Justification::centredLeft);
        for (int c = 0; c < n; ++c)
        {
            const int y = 24 + c * 60 + 28;
            g.setColour (theme.ink); g.setFont (mono (11.0f));
            g.drawRect (0, y + 4, 12, 12, 1); if (v[(size_t) c].invert) g.fillRect (3, y + 7, 6, 6);
            g.drawText ("polarity inverted", 18, y, 160, 20, juce::Justification::centredLeft);
            g.drawRect (190, y + 4, 12, 12, 1); if (v[(size_t) c].mute) g.fillRect (193, y + 7, 6, 6);
            g.drawText ("mute", 208, y, 60, 20, juce::Justification::centredLeft);
            const double ms = v[(size_t) c].ms;
            g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (mono (10.0f));
            g.drawText (juce::String (ms * 0.001 * sr, 2) + " samples at " + juce::String (sr / 1000.0, 1) + " khz  =  " + juce::String (ms * 34.3, 1) + " cm of air", getWidth() / 2 + 10, y, getWidth() / 2 - 10, 20, juce::Justification::centredLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int c = 0; c < n; ++c)
        {
            const int y = 24 + c * 60 + 28;
            if (e.y >= y && e.y < y + 20) { auto v = all(); if (e.x < 180) v[(size_t) c].invert = ! v[(size_t) c].invert; else if (e.x < 280) v[(size_t) c].mute = ! v[(size_t) c].mute; store (v); }
        }
    }
    int heightNeeded() const { return 24 + n * 60; }
private:
    std::vector<Model::Speaker> all() const { auto v = Model::speakers (block); v.resize ((size_t) n); return v; }
    void store (std::vector<Model::Speaker> v, bool notify = true) { v.resize ((size_t) std::max (n, 2)); block.setProperty (ids::align, Model::speakersText (v), nullptr); repaint(); (void) notify; }
    Theme theme; juce::ValueTree block; double sr; int n = 2;
    std::vector<std::unique_ptr<PreciseSlider>> trims, delays;
};
} // namespace um::ui

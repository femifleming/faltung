#pragma once
// umleitung — editors used on several pages: the graphic eq's ten sliders, the parametric eq's curve (drag the
// points), the channel matrix (any input to any output), the app picker, and controls bound to the model.
#include "Look.h"
#include "Meters.h"
#include "../platform/Apps.h"

namespace um::ui
{
// ---- controls bound to a block's property ---------------------------------------------------------------------
inline std::unique_ptr<ValueSlider> boundSlider (Theme t, juce::ValueTree b, const juce::Identifier& prop, const juce::String& label,
                                                 juce::NormalisableRange<float> range, float def, std::function<juce::String (float)> text)
{
    auto s = std::make_unique<ValueSlider> (t, label, range, (float) b.getProperty (prop, def), def);
    s->textFromValue = std::move (text);
    s->onChange = [b, prop] (float v) mutable { b.setProperty (prop, v, nullptr); };
    s->setLabelWidth (92);
    return s;
}
inline std::unique_ptr<ValueChoice> boundChoice (Theme t, juce::ValueTree b, const juce::Identifier& prop, const juce::String& label, juce::StringArray items, juce::StringArray values)
{
    int idx = values.indexOf (b[prop].toString()); if (idx < 0) idx = 0;
    auto c = std::make_unique<ValueChoice> (t, label, items, idx);
    c->labelW = 92;
    c->onChange = [b, prop, values] (int i) mutable { if (i >= 0 && i < values.size()) b.setProperty (prop, values[i], nullptr); };
    return c;
}
inline std::unique_ptr<ValueToggle> boundToggle (Theme t, juce::ValueTree b, const juce::Identifier& prop, const juce::String& label)
{
    auto c = std::make_unique<ValueToggle> (t, label, (bool) b[prop]);
    c->onChange = [b, prop] (bool v) mutable { b.setProperty (prop, v, nullptr); };
    return c;
}

/** the devices to choose from (inputs or outputs), as menu items and their uids */
inline void deviceLists (bool outputs, juce::StringArray& names, juce::StringArray& uids, bool withSystemDefault)
{
    if (withSystemDefault) { names.add ("the mac's output (follows it)"); uids.add (""); }
    for (auto& d : CoreAudioHost::devices())
    {
        if (d.isHidden || d.isAggregate) continue;
        if (outputs ? d.outChannels == 0 : d.inChannels == 0) continue;
        names.add (d.name + "  (" + juce::String (outputs ? d.outChannels : d.inChannels) + " ch" + (d.isOurs ? ", virtual" : "") + ")");
        uids.add (d.uid);
    }
}

// ---- the parametric eq's curve: points you drag (frequency, gain), wheel for q; the spectrum behind --------------------
class CurveEditor : public juce::Component
{
public:
    CurveEditor (Theme t, juce::ValueTree b, Engine& e) : theme (t), block (b), spectrum (t) { spectrum.src = { &e, (int) b[ids::id] }; spectrum.showPeaks = false; addAndMakeVisible (spectrum); spectrum.setInterceptsMouseClicks (false, false); }
    void resized() override { spectrum.setBounds (getLocalBounds()); }
    void tick() { spectrum.tick(); }
    float xOf (double hz) const { return spectrum.xOf (hz); }
    double hzAt (float x) const { return spectrum.hzAt (x); }
    float yOf (float db) const { return (float) getHeight() * (0.5f - db / 48.0f); }
    float dbAt (float y) const { return (0.5f - y / std::max (1.0f, (float) getHeight())) * 48.0f; }
    void paintOverChildren (juce::Graphics& g) override
    {
        auto bands = Model::peqBands (block);
        const double fs = 48000;
        const auto designed = Engine::bandsOf (block);
        juce::Path p;
        for (int x = 0; x < getWidth(); x += 2)
        {
            double d = dsp::digitalDb (designed, fs, hzAt ((float) x));
            d += (float) block[ids::db];
            const float y = yOf ((float) juce::jlimit (-24.0, 24.0, d));
            if (x == 0) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
        }
        g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect (0.0f, yOf (0), (float) getWidth(), 1.0f);
        g.setColour (theme.ink); g.strokePath (p, juce::PathStrokeType (2.0f));
        for (int i = 0; i < (int) bands.size(); ++i)
        {
            const auto& b = bands[(size_t) i];
            const float x = xOf (b.freq), y = yOf (b.shape >= 3 ? 0.0f : b.db);
            g.setColour (i == selected ? theme.ink : theme.fill.withAlpha (b.on ? 1.0f : 0.4f)); g.fillEllipse (x - 6, y - 6, 12, 12);
            g.setColour (ui::inkFor (theme.fill)); g.setFont (mono (8.0f, true)); g.drawText (juce::String (i + 1), (int) x - 6, (int) y - 6, 12, 12, juce::Justification::centred);
        }
        g.setColour (theme.ink.withAlpha (0.6f)); g.setFont (mono (9.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("drag a point · wheel: q (width) · double-click: a new band · right-click a point: its shape, remove")), 6, 4, getWidth() - 12, 12, juce::Justification::centredLeft);
    }
    int hit (juce::Point<int> p) const
    {
        auto bands = Model::peqBands (block);
        for (int i = 0; i < (int) bands.size(); ++i) if (p.getDistanceFrom ({ (int) xOf (bands[(size_t) i].freq), (int) yOf (bands[(size_t) i].shape >= 3 ? 0.0f : bands[(size_t) i].db) }) < 10) return i;
        return -1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        selected = hit (e.getPosition());
        if (e.mods.isPopupMenu() && selected >= 0)
        {
            juce::PopupMenu m; for (int s = 0; s < kPeqShapes; ++s) m.addItem (s + 1, peqShapeName (s), true, Model::peqBands (block)[(size_t) selected].shape == s);
            m.addSeparator(); m.addItem (100, Model::peqBands (block)[(size_t) selected].on ? "switch off" : "switch on"); m.addItem (101, "remove");
            const int sel = selected;
            m.showMenuAsync (juce::PopupMenu::Options(), [this, sel] (int r)
            {
                auto v = Model::peqBands (block); if (sel >= (int) v.size()) return;
                if (r >= 1 && r <= kPeqShapes) v[(size_t) sel].shape = r - 1; else if (r == 100) v[(size_t) sel].on = ! v[(size_t) sel].on; else if (r == 101) v.erase (v.begin() + sel); else return;
                store (v);
            });
            return;
        }
        if (e.getNumberOfClicks() > 1 && selected < 0)
        {
            auto v = Model::peqBands (block); PeqBand nb; nb.freq = std::round (hzAt ((float) e.x)); nb.db = juce::jlimit (-24.0, 24.0, (double) dbAt ((float) e.y)); nb.q = 1.0; v.push_back (nb);
            selected = (int) v.size() - 1; store (v);
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selected < 0) return;
        auto v = Model::peqBands (block); if (selected >= (int) v.size()) return;
        v[(size_t) selected].freq = juce::jlimit (5.0, 24000.0, std::round (hzAt ((float) e.x) * 10.0) / 10.0);
        if (v[(size_t) selected].shape < 3 || v[(size_t) selected].shape == 8) v[(size_t) selected].db = juce::jlimit (-24.0, 24.0, std::round ((double) dbAt ((float) e.y) * 100.0) / 100.0);
        store (v);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int i = hit (e.getPosition()); if (i < 0) return;
        auto v = Model::peqBands (block); v[(size_t) i].q = juce::jlimit (0.05, 50.0, v[(size_t) i].q * std::pow (1.25, (double) w.deltaY * 4.0)); store (v);
    }
    std::function<void()> onEdit;
    int selected = -1;
private:
    void store (const std::vector<PeqBand>& v) { block.setProperty (ids::bands, Model::peqText (v), nullptr); repaint(); if (onEdit) onEdit(); }
    Theme theme; juce::ValueTree block; Spectrum spectrum;
};

// ---- the channel matrix: rows are inputs, columns outputs; click a cell to connect, drag up/down for its level ------
class MatrixGrid : public juce::Component
{
public:
    MatrixGrid (Theme t, juce::ValueTree b, int ins) : theme (t), block (b), inputs (std::max (1, ins)) {}
    int outputs() const { return juce::jlimit (1, 256, (int) block[ids::outs]); }
    juce::Rectangle<float> cell (int i, int o) const { const float s = cellSize(); return { 30.0f + o * s, 18.0f + i * s, s - 2.0f, s - 2.0f }; }
    float cellSize() const { return juce::jlimit (6.0f, 26.0f, std::min ((getWidth() - 34.0f) / (float) outputs(), (getHeight() - 22.0f) / (float) inputs)); }
    void paint (juce::Graphics& g) override
    {
        auto cells = Model::matrixCells (block); const bool empty = cells.empty();
        const int outs = outputs();
        g.setFont (mono (8.0f));
        for (int o = 0; o < outs; ++o) if (outs <= 32 || o % 4 == 0) { g.setColour (theme.ink.withAlpha (0.6f)); auto c = cell (0, o); g.drawText (juce::String (o + 1), (int) c.getX(), 2, (int) c.getWidth(), 14, juce::Justification::centred); }
        for (int i = 0; i < inputs; ++i)
        {
            if (inputs <= 32 || i % 4 == 0) { g.setColour (theme.ink.withAlpha (0.6f)); auto c = cell (i, 0); g.drawText ("in " + juce::String (i + 1), 0, (int) c.getY(), 28, (int) c.getHeight(), juce::Justification::centredRight); }
            for (int o = 0; o < outs; ++o)
            {
                auto c = cell (i, o);
                float gain = empty && o % inputs == i ? 1.0f : 0.0f;
                for (auto& x : cells) if (x.in == i && x.out == o) gain = x.gain;
                g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (c);
                if (gain != 0.0f)
                {
                    g.setColour ((gain < 0 ? juce::Colour (0xffee8e6e) : theme.fill).withAlpha (juce::jlimit (0.25f, 1.0f, std::abs (gain))));
                    g.fillPath (skewRect (c.reduced (1.0f), 0.3f));
                    if (c.getWidth() > 18) { g.setColour (ui::inkFor (theme.fill)); g.drawText (std::abs (gain - 1.0f) < 1.0e-3f ? juce::String() : juce::String (gain, 1), c.toNearestInt(), juce::Justification::centred); }
                }
            }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        downCell = at (e.getPosition()); if (downCell.x < 0) return;
        auto cells = materialise();
        auto it = std::find_if (cells.begin(), cells.end(), [&] (auto& c) { return c.in == downCell.x && c.out == downCell.y; });
        if (e.mods.isPopupMenu()) { if (it != cells.end()) it->gain = -it->gain; else cells.push_back ({ downCell.x, downCell.y, -1.0f }); }
        else if (it != cells.end()) { startGain = it->gain; toggledOff = true; }
        else { cells.push_back ({ downCell.x, downCell.y, 1.0f }); startGain = 1.0f; toggledOff = false; }
        store (cells);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (downCell.x < 0 || std::abs (e.getDistanceFromDragStartY()) < 3) return;
        toggledOff = false;
        auto cells = materialise();
        for (auto& c : cells) if (c.in == downCell.x && c.out == downCell.y) c.gain = juce::jlimit (-2.0f, 2.0f, std::round ((startGain - e.getDistanceFromDragStartY() / 60.0f) * 20.0f) / 20.0f);
        store (cells);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (downCell.x >= 0 && toggledOff) { auto cells = materialise(); cells.erase (std::remove_if (cells.begin(), cells.end(), [&] (auto& c) { return c.in == downCell.x && c.out == downCell.y; }), cells.end()); store (cells.empty() ? std::vector<Model::Cell> { { 0, 0, 0.0f } } : cells); }
        downCell = { -1, -1 };
    }
    void preset (int which)
    {
        std::vector<Model::Cell> v; const int outs = outputs();
        if (which == 0) for (int o = 0; o < outs; ++o) v.push_back ({ o % inputs, o, 1.0f });                                  // straight (wrapping)
        else if (which == 1) for (int o = 0; o < outs; ++o) for (int i = 0; i < inputs; ++i) v.push_back ({ i, o, 1.0f / (float) inputs });   // everything to every output, summed
        else if (which == 2) for (int o = 0; o < outs; ++o) v.push_back ({ (o % 2 == 0 ? 1 : 0) % inputs, o, 1.0f });               // left and right swapped
        else if (which == 3) for (int i = 0; i < inputs; ++i) v.push_back ({ i, i % outs, 1.0f });                                  // fold: inputs wrap onto the outputs
        store (v);
    }
    std::function<void()> onEdit;
private:
    juce::Point<int> at (juce::Point<int> p) const { for (int i = 0; i < inputs; ++i) for (int o = 0; o < outputs(); ++o) if (cell (i, o).contains (p.toFloat())) return { i, o }; return { -1, -1 }; }
    std::vector<Model::Cell> materialise() { auto c = Model::matrixCells (block); if (c.empty()) for (int o = 0; o < outputs(); ++o) c.push_back ({ o % inputs, o, 1.0f }); c.erase (std::remove_if (c.begin(), c.end(), [] (auto& x) { return x.gain == 0.0f; }), c.end()); return c; }
    void store (const std::vector<Model::Cell>& v) { block.setProperty (ids::matrix, Model::matrixText (v), nullptr); repaint(); if (onEdit) onEdit(); }
    Theme theme; juce::ValueTree block; int inputs; juce::Point<int> downCell { -1, -1 }; float startGain = 1; bool toggledOff = false;
};

// ---- choose apps: the running apps that use audio (playing ones marked), ticked into the block -------------------------
class AppPicker : public juce::Component
{
public:
    AppPicker (Theme t, juce::ValueTree b, const juce::Identifier& p) : theme (t), block (b), prop (p) { reload(); }
    void reload()
    {
        rows.clear();
        std::map<juce::String, size_t> at;   // key → row
        auto addRow = [&] (const juce::String& key, const juce::String& name, bool playing)
        {
            if (auto it = at.find (key); it != at.end()) { rows[it->second].playing = rows[it->second].playing || playing; return; }
            at[key] = rows.size(); rows.push_back ({ key, name, playing });
        };
        // every running app, whether it has played anything yet or not
        for (auto& a : runningApps()) addRow (a.bundleId, a.name, false);
        // what is using audio: a helper process counts as the app it lives in (Google Chrome Helper is Google Chrome)
        for (auto& pr : CoreAudioHost::audioProcesses())
        {
            const auto owner = owningApp (pr.path);
            if (owner.bundleId.isNotEmpty()) { addRow (owner.bundleId, owner.name, pr.playing); continue; }
            if (! pr.playing && ! showAll) continue;   // the system's own helpers, unless they are playing
            addRow (pr.bundleId.isNotEmpty() ? pr.bundleId : "pid:" + juce::String (pr.pid), pr.name.isNotEmpty() ? pr.name : pr.bundleId, pr.playing);
        }
        // apps chosen before that aren't running now stay on the list
        for (auto& k : juce::StringArray::fromTokens (block[keys()].toString(), " ", "")) if (k.isNotEmpty() && ! at.count (k) && ! k.startsWith ("pid:")) addRow (k, k + " (not running)", false);
        std::sort (rows.begin(), rows.end(), [] (auto& a, auto& b) { return a.playing != b.playing ? a.playing : a.name.compareIgnoreCase (b.name) < 0; });
        setSize (getWidth(), std::max (40, (int) rows.size() * 20 + 4));
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto chosen = juce::StringArray::fromTokens (block[keys()].toString(), " ", "");
        g.setFont (mono (11.0f));
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto r = juce::Rectangle<float> (0.0f, i * 20.0f + 2.0f, (float) getWidth(), 18.0f);
            const bool on = chosen.contains (rows[(size_t) i].key);
            skewBox (g, r.removeFromLeft (16.0f).reduced (2.0f, 3.0f), theme.ink, on, 0.6f);
            g.setColour (rows[(size_t) i].playing ? theme.fill : theme.ink.withAlpha (0.2f)); g.fillEllipse (r.getX() + 4.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (theme.ink); g.drawText (rows[(size_t) i].name.toLowerCase(), r.withTrimmedLeft (16.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
        if (rows.empty()) { g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("no app is using audio right now", getLocalBounds(), juce::Justification::centred); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / 20; if (i < 0 || i >= (int) rows.size()) return;
        auto chosen = juce::StringArray::fromTokens (block[keys()].toString(), " ", ""); chosen.removeEmptyStrings();
        if (chosen.contains (rows[(size_t) i].key)) chosen.removeString (rows[(size_t) i].key); else chosen.add (rows[(size_t) i].key);
        // apps by bundle id (every process of the app, also later ones); apps without one by their pid
        juce::StringArray bundles, pids;
        for (auto& k : chosen) { if (k.startsWith ("pid:")) pids.add (k.fromFirstOccurrenceOf ("pid:", false, false)); else bundles.add (k); }
        block.setProperty (keys(), chosen.joinIntoString (" "), nullptr);
        if (prop == ids::bundles) { block.setProperty (ids::pids, pids.joinIntoString (" "), nullptr); block.setProperty (ids::bundles, bundles.joinIntoString (" "), nullptr); }
        else block.setProperty (prop, bundles.joinIntoString (" "), nullptr);
        if (prop == ids::bundles && bundles.size() + pids.size() == 1) block.setProperty (ids::name, rows[(size_t) i].name.toLowerCase(), nullptr);
        repaint();
    }
private:
    juce::Identifier keys() const { return prop == ids::bundles ? juce::Identifier ("chosen") : prop; }
public:
    bool showAll = false;
private:
    struct Row { juce::String key, name; bool playing = false; };
    Theme theme; juce::ValueTree block; juce::Identifier prop; std::vector<Row> rows;
};
} // namespace um::ui

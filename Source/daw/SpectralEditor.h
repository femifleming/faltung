#pragma once
// sndstudio — spectral editing of an audio clip: its sound as a spectrogram (time across, frequency up, on a log scale),
// and boxes drawn on it that turn those frequencies, at those times, down (or up). Drag: a box at the amount chosen;
// click a box: select it; delete: remove; the amount: click it. Every change is rendered at once (the original file is
// kept; the clip plays the rendering).
#include "StudioWidgets.h"
#include "Session.h"
#include <juce_dsp/juce_dsp.h>

namespace fal::daw
{
class SpectralEditor : public juce::Component
{
public:
    SpectralEditor (Session& s, ClipRef r) : session (s), ref (r), theme (studioTheme())
    {
        setWantsKeyboardFocus (true);
        if (auto* c = clip())
        {
            edits = c->spec;
            const juce::File of (c->origPath.isNotEmpty() ? c->origPath : c->path);
            juce::String err; src = session.loadSample (of, &err);
        }
    }
    std::function<void()> onClose;
    const AudioClip* clip() const { if (auto* t = session.clipTrack (ref)) for (auto& c : *t->aclips) if (c.id == ref.id) return &c; return nullptr; }
    double seconds() const { return src ? src->data.getNumSamples() / src->rate : 1.0; }
    juce::Rectangle<int> area() const { return getLocalBounds().withTrimmedTop (head).withTrimmedLeft (axisW).withTrimmedBottom (4); }
    // time ↔ x, frequency ↔ y (log, 20 hz … nyquist)
    double nyq() const { return src ? src->rate * 0.5 : 24000.0; }
    float xOf (double sec) const { const auto a = area(); return (float) a.getX() + (float) (sec / seconds() * a.getWidth()); }
    double secAt (float x) const { const auto a = area(); return juce::jlimit (0.0, seconds(), (x - a.getX()) / (double) a.getWidth() * seconds()); }
    float yOf (double hz) const { const auto a = area(); const double u = std::log (std::max (20.0, hz) / 20.0) / std::log (nyq() / 20.0); return (float) a.getBottom() - (float) (u * a.getHeight()); }
    double hzAt (float y) const { const auto a = area(); const double u = juce::jlimit (0.0, 1.0, (a.getBottom() - y) / (double) a.getHeight()); return 20.0 * std::pow (nyq() / 20.0, u); }
    float amount = -60.0f;
    static juce::String dbName (float db) { return db <= -59.0f ? juce::String ("cut") : (db > 0 ? "+" : "") + juce::String (db, 0) + " db"; }

    void makeImage()
    {
        const auto a = area(); if (! src || a.isEmpty()) return;
        img = juce::Image (juce::Image::RGB, a.getWidth(), a.getHeight(), true);
        constexpr int order = 11, N = 1 << order;
        juce::dsp::FFT fft (order); std::vector<float> fr ((size_t) N * 2);
        const auto& d = src->data; const int ns = d.getNumSamples();
        const auto lo = theme.ground, hi = theme.ink, hot = hotColour();
        for (int x = 0; x < a.getWidth(); ++x)
        {
            const int pos = (int) ((double) x / a.getWidth() * ns) - N / 2;
            for (int i = 0; i < N; ++i) { const int si = pos + i; const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N); fr[(size_t) i] = si >= 0 && si < ns ? d.getSample (0, si) * w : 0.0f; }
            fft.performFrequencyOnlyForwardTransform (fr.data());
            for (int y = 0; y < a.getHeight(); ++y)
            {
                const double hz = hzAt ((float) (a.getY() + y)); const int k = juce::jlimit (0, N / 2, (int) std::lround (hz / (src->rate) * N));
                const float db = juce::Decibels::gainToDecibels (fr[(size_t) k] / (N * 0.25f), -120.0f);
                const float u = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f);
                img.setPixelAt (x, y, u < 0.7f ? lo.interpolatedWith (hi, u / 0.7f) : hi.interpolatedWith (hot, (u - 0.7f) / 0.3f));
            }
        }
    }
    void resized() override { makeImage(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground.withAlpha (0.99f));
        const auto a = area();
        if (img.isValid()) g.drawImageAt (img, a.getX(), a.getY());
        // frequency axis
        g.setFont (uiFont (8.5f));
        for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (hz > nyq()) break; const float y = yOf (hz);
            g.setColour (theme.ink.withAlpha (0.25f)); g.fillRect ((float) a.getX(), y, (float) a.getWidth(), 1.0f);
            g.setColour (theme.ink); g.drawText (hz >= 1000 ? juce::String (hz / 1000, 0) + "k" : juce::String (hz, 0), 0, (int) y - 6, axisW - 4, 12, juce::Justification::centredRight);
        }
        // the boxes
        for (int i = 0; i < (int) edits.size(); ++i)
        {
            const auto& e = edits[(size_t) i];
            auto r = juce::Rectangle<float>::leftTopRightBottom (xOf (e.t0), yOf (e.f1), xOf (e.t1), yOf (e.f0));
            g.setColour ((e.db < 0 ? coolColour() : warmColour()).withAlpha (0.25f)); g.fillRect (r);
            g.setColour (i == selected ? brightColour() : (e.db < 0 ? coolColour() : warmColour())); g.drawRect (r, i == selected ? 2.0f : 1.0f);
            g.setFont (uiFont (9.0f)); g.drawText (dbName (e.db), r.reduced (3.0f).toNearestInt(), juce::Justification::topLeft, false);
        }
        if (dragging) { g.setColour (brightColour().withAlpha (0.8f)); g.drawRect (juce::Rectangle<int> (dragA, dragB), 1); }
        g.setColour (theme.ink); g.setFont (uiFont (11.0f, true));
        g.drawText ("spectral: " + (src ? juce::File (clip() ? (clip()->origPath.isNotEmpty() ? clip()->origPath : clip()->path) : juce::String()).getFileName() : juce::String ("no audio")), 8, 4, getWidth() / 2, 16, juce::Justification::centredLeft);
        g.setFont (uiFont (9.5f)); g.setColour (theme.ink.withAlpha (0.75f));
        g.drawText (ui::U ("drag: a box (time × frequency) turned by the amount · click a box: select · delete: remove · esc: close"), 8, 20, getWidth() - 300, 14, juce::Justification::centredLeft);
        g.setColour (theme.fill); g.fillRect (amountBox()); g.setColour (ui::inkFor (theme.fill)); g.drawText ("amount: " + dbName (amount), amountBox(), juce::Justification::centred);
        g.setColour (theme.ink); g.drawText ("clear all", clearBox(), juce::Justification::centred); g.drawText ("close", getWidth() - 60, 4, 52, 16, juce::Justification::centredRight);
    }
    juce::Rectangle<int> amountBox() const { return { getWidth() - 290, 6, 120, 20 }; }
    juce::Rectangle<int> clearBox() const { return { getWidth() - 160, 6, 80, 20 }; }
    int boxAt (juce::Point<float> p) const
    {
        for (int i = (int) edits.size() - 1; i >= 0; --i) { const auto& e = edits[(size_t) i]; if (juce::Rectangle<float>::leftTopRightBottom (xOf (e.t0), yOf (e.f1), xOf (e.t1), yOf (e.f0)).contains (p)) return i; }
        return -1;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.y < head && e.x > getWidth() - 70) { if (onClose) onClose(); return; }
        if (amountBox().contains (e.getPosition()))
        {
            juce::PopupMenu m; for (float db : { -60.0f, -24.0f, -12.0f, -6.0f, -3.0f, 3.0f, 6.0f, 12.0f }) m.addItem ((int) db + 100, dbName (db), true, std::abs (amount - db) < 0.1f);
            juce::Component::SafePointer<SpectralEditor> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [sp] (int r) { if (sp && r > 0) { sp->amount = (float) (r - 100); if (sp->selected >= 0) { sp->edits[(size_t) sp->selected].db = sp->amount; sp->apply(); } sp->repaint(); } });
            return;
        }
        if (clearBox().contains (e.getPosition())) { edits.clear(); selected = -1; apply(); return; }
        if (! area().contains (e.getPosition())) return;
        selected = boxAt (e.position);
        if (selected < 0) { dragging = true; dragA = dragB = e.getPosition(); }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) { dragB = e.getPosition(); repaint(); } }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        const auto r = juce::Rectangle<int> (dragA, dragB);
        if (r.getWidth() < 3 || r.getHeight() < 3) { repaint(); return; }
        AudioClip::SpecEdit se; se.t0 = secAt ((float) r.getX()); se.t1 = secAt ((float) r.getRight()); se.f1 = hzAt ((float) r.getY()); se.f0 = hzAt ((float) r.getBottom()); se.db = amount;
        edits.push_back (se); selected = (int) edits.size() - 1; apply();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if ((k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) && selected >= 0 && selected < (int) edits.size()) { edits.erase (edits.begin() + selected); selected = -1; apply(); return true; }
        return false;
    }
    void apply() { juce::String err; if (! session.applySpectral (ref, edits, err)) session.dirty = true; status = err; repaint(); }
    std::vector<AudioClip::SpecEdit> edits; int selected = -1; juce::String status;
private:
    Session& session; ClipRef ref; ui::Theme theme; SamplePtr src; juce::Image img;
    bool dragging = false; juce::Point<int> dragA, dragB;
    static constexpr int head = 34, axisW = 40;
};
} // namespace fal::daw

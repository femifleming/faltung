#pragma once
// umleitung — seeing the sound: a meter bridge (any number of channels, peak + rms + hold + clip), needle vu meters,
// loudness (momentary, short-term, integrated against a target), a spectrum (fft, log frequency, peak hold), a
// spectrogram, a stereo field (goniometer + correlation) and a scope. They all read a block's running state
// (BlockRt: its LevelTap, ScopeFifo, Loudness) and repaint from the page's tick.
#include "Look.h"
#include <juce_dsp/juce_dsp.h>

namespace um::ui
{
/** looks at one block (it may come and go as the plan is rebuilt) */
struct Watched
{
    Engine* engine = nullptr; int blockId = 0;
    std::shared_ptr<BlockRt> rt() const { return engine && blockId ? engine->runtimeShared (blockId) : nullptr; }
};

// ---- the meter bridge -------------------------------------------------------------------------------------------
class MeterBridge : public juce::Component
{
public:
    explicit MeterBridge (Theme t) : theme (t) {}
    Watched src; bool vertical = true, labels = true, compact = false;
    void tick()
    {
        auto rt = src.rt();
        const int n = rt ? std::min (rt->levels.channels.load(), dsp::LevelTap::kMax) : 0;
        if ((int) pk.size() != n) { pk.assign ((size_t) n, 0.0f); rm.assign ((size_t) n, 0.0f); hold.assign ((size_t) n, 0.0f); holdAge.assign ((size_t) n, 0); clip.assign ((size_t) n, false); }
        for (int c = 0; c < n; ++c)
        {
            const float p = rt->levels.takePeak (c), r = rt->levels.rms[(size_t) c].load();
            pk[(size_t) c] = std::max (p, pk[(size_t) c] * 0.86f);          // ~ 20 db/s fall at 30 hz
            rm[(size_t) c] = r > rm[(size_t) c] ? r : rm[(size_t) c] * 0.9f + r * 0.1f;
            if (p >= hold[(size_t) c]) { hold[(size_t) c] = p; holdAge[(size_t) c] = 0; } else if (++holdAge[(size_t) c] > 45) hold[(size_t) c] *= 0.95f;
            if (p >= 1.0f) clip[(size_t) c] = true;
        }
        repaint();
    }
    void mouseDown (const juce::MouseEvent&) override { std::fill (clip.begin(), clip.end(), false); std::fill (hold.begin(), hold.end(), 0.0f); }
    void paint (juce::Graphics& g) override
    {
        const int n = (int) pk.size();
        auto r = getLocalBounds().toFloat();
        if (n == 0) { g.setColour (theme.ink.withAlpha (0.4f)); g.setFont (mono (10.0f)); g.drawText ("no signal here (the session is off)", r, juce::Justification::centred); return; }
        const float scaleW = labels && ! compact ? 26.0f : 0.0f;
        auto area = r.withTrimmedLeft (scaleW).withTrimmedBottom (labels ? 14.0f : 0.0f);
        const float gap = n > 32 ? 1.0f : 2.0f, w = (area.getWidth() - gap * (n - 1)) / (float) n;
        // the scale
        if (scaleW > 0)
        {
            g.setFont (mono (9.0f)); g.setColour (theme.ink.withAlpha (0.55f));
            for (float d : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
            { const float y = area.getBottom() - area.getHeight() * meterPos (dsp::dbToGain (d)); g.drawText (juce::String ((int) d), 0, (int) y - 6, (int) scaleW - 4, 12, juce::Justification::centredRight); g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (area.getX(), y, area.getWidth(), 1.0f); g.setColour (theme.ink.withAlpha (0.55f)); }
        }
        for (int c = 0; c < n; ++c)
        {
            auto bar = juce::Rectangle<float> (area.getX() + c * (w + gap), area.getY(), std::max (1.0f, w), area.getHeight());
            g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (bar);
            const float hp = meterPos (pk[(size_t) c]), hr = meterPos (rm[(size_t) c]);
            g.setColour (meterColour (dsp::gainToDb (pk[(size_t) c]), theme.fill).withAlpha (0.55f)); g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * hp));
            g.setColour (meterColour (dsp::gainToDb (rm[(size_t) c]), theme.fill)); g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * hr));
            const float hy = bar.getBottom() - bar.getHeight() * meterPos (hold[(size_t) c]);
            g.setColour (theme.ink); g.fillRect (bar.getX(), hy, bar.getWidth(), 1.5f);
            if (clip[(size_t) c]) { g.setColour (juce::Colour (0xffee5a3e)); g.fillRect (bar.withHeight (4.0f)); }
            if (labels && (n <= 32 || c % 4 == 0)) { g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (mono (9.0f)); g.drawText (juce::String (c + 1), (int) bar.getX() - 6, (int) area.getBottom() + 1, (int) bar.getWidth() + 12, 12, juce::Justification::centred); }
        }
    }
private:
    Theme theme; std::vector<float> pk, rm, hold; std::vector<int> holdAge; std::vector<bool> clip;
};

// ---- a needle vu (300 ms, -20 … +3 vu, 0 vu = -18 dbfs) ---------------------------------------------------------
class VuMeter : public juce::Component
{
public:
    VuMeter (Theme t, int ch) : theme (t), channel (ch) {}
    Watched src;
    void tick()
    {
        auto rt = src.rt();
        const float r = rt && channel < rt->levels.channels.load() ? rt->levels.rms[(size_t) channel].load() : 0.0f;
        // vu ballistics: a second-order response reaching 99 % in 300 ms (approximated at 30 frames a second)
        const float target = dsp::gainToDb (r * 1.41421356f) + 18.0f;   // sine rms → peak, then 0 vu = -18 dbfs
        vel += (target - pos) * 0.35f; vel *= 0.45f; pos += vel;
        pos = juce::jlimit (-25.0f, 6.0f, pos);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        g.setColour (juce::Colour (0xfff4ead2)); g.fillRect (r);
        g.setColour (juce::Colour (0xff1d1c15).withAlpha (0.3f)); g.drawRect (r, 1.0f);
        const auto pivot = juce::Point<float> (r.getCentreX(), r.getBottom() + r.getHeight() * 0.35f);
        const float rad = r.getHeight() * 1.05f;
        auto ang = [] (float vu) { const float t = juce::jlimit (0.0f, 1.0f, (std::pow (10.0f, vu / 20.0f) - 0.1f) / (std::pow (10.0f, 0.15f) - 0.1f)); return juce::degreesToRadians (-48.0f + 96.0f * t); };
        g.setFont (mono (9.0f));
        for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const float a = ang (vu); const auto p1 = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * rad, p2 = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (rad - 8.0f);
            g.setColour (vu >= 0 ? juce::Colour (0xffd2452c) : juce::Colour (0xff1d1c15)); g.drawLine ({ p1, p2 }, 1.2f);
            const auto pt = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (rad - 18.0f);
            g.drawText (juce::String ((int) std::abs (vu)), (int) pt.x - 10, (int) pt.y - 6, 20, 12, juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff1d1c15)); g.setFont (mono (11.0f, true)); g.drawText ("vu", r.withTrimmedTop (r.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredTop);
        g.setFont (mono (9.0f)); g.drawText (label, r.reduced (6.0f).toNearestInt(), juce::Justification::bottomLeft);
        const float a = ang (pos);
        g.setColour (juce::Colour (0xff1d1c15)); g.drawLine ({ pivot, pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (rad - 2.0f) }, 1.6f);
    }
    juce::String label;
private:
    Theme theme; int channel; float pos = -25.0f, vel = 0;
};

// ---- loudness -------------------------------------------------------------------------------------------------------
class LoudnessView : public juce::Component
{
public:
    explicit LoudnessView (Theme t) : theme (t) {}
    Watched src; float target = -16.0f;   // podcasts / streaming
    void tick() { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { if (auto rt = src.rt()) rt->loudness.reset(); }
    void paint (juce::Graphics& g) override
    {
        auto rt = src.rt();
        const float m = rt ? rt->loudness.momentary.load() : -120, s = rt ? rt->loudness.shortTerm.load() : -120, i = rt ? rt->loudness.integrated.load() : -120;
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f)); g.drawText (juce::String (juce::CharPointer_UTF8 ("loudness (lufs) · target ")) + juce::String (target, 0) + juce::String (juce::CharPointer_UTF8 (" · click: reset")), r.removeFromTop (16.0f), juce::Justification::centredLeft);
        auto row = [&] (const juce::String& name, float v, juce::Rectangle<float> rr)
        {
            g.setColour (theme.ink.withAlpha (0.62f)); g.setFont (mono (10.0f)); g.drawText (name, rr.removeFromLeft (90.0f), juce::Justification::centredLeft);
            auto val = rr.removeFromRight (70.0f);
            g.setColour (theme.ink); g.setFont (mono (13.0f, true)); g.drawText (v <= -119 ? juce::String ("--") : juce::String (v, 1), val, juce::Justification::centredRight);
            rr = rr.reduced (6.0f, 6.0f);
            g.setColour (theme.ink.withAlpha (0.1f)); g.fillRect (rr);
            const float t = juce::jlimit (0.0f, 1.0f, (v + 50.0f) / 50.0f);
            g.setColour (std::abs (v - target) < 1.0f ? theme.fill : v > target ? juce::Colour (0xffe3bf5f) : theme.fill.withAlpha (0.6f));
            g.fillPath (skewRect (rr.withWidth (rr.getWidth() * t), 0.4f));
            const float tx = rr.getX() + rr.getWidth() * (target + 50.0f) / 50.0f; g.setColour (theme.ink); g.fillRect (tx, rr.getY() - 3.0f, 1.5f, rr.getHeight() + 6.0f);
        };
        const float h = r.getHeight() / 3.0f;
        row ("momentary", m, r.removeFromTop (h)); row ("short-term", s, r.removeFromTop (h)); row ("integrated", i, r);
    }
private:
    Theme theme;
};

// ---- the spectrum (and the spectrogram below it shares its fft) ------------------------------------------------------
class Spectrum : public juce::Component
{
public:
    explicit Spectrum (Theme t) : theme (t), fft (kOrder), window ((size_t) kN, juce::dsp::WindowingFunction<float>::blackmanHarris) { mag.assign (kN / 2, -120.0f); peak.assign (kN / 2, -120.0f); }
    Watched src; bool showPeaks = true;
    std::vector<std::pair<float, float>> overlay;   // a curve (hz, db) drawn over it: the eq's
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    double sampleRate() const { return src.engine ? std::max (8000.0, src.engine->sampleRate()) : 48000.0; }
    void tick()
    {
        auto rt = src.rt();
        if (rt)
        {
            std::vector<float> L ((size_t) kN), R ((size_t) kN); rt->scope.latest (L.data(), R.data(), kN);
            std::vector<float> buf ((size_t) kN * 2, 0.0f);
            for (int i = 0; i < kN; ++i) buf[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
            window.multiplyWithWindowingTable (buf.data(), (size_t) kN);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int k = 0; k < kN / 2; ++k)
            {
                const float d = dsp::gainToDb (buf[(size_t) k] * 2.0f / (kN * 0.42f));
                mag[(size_t) k] = d > mag[(size_t) k] ? d : mag[(size_t) k] * 0.7f + d * 0.3f;
                peak[(size_t) k] = std::max (d, peak[(size_t) k] - 0.4f);
            }
            if (onFrame) onFrame (mag);
        }
        repaint();
    }
    std::function<void (const std::vector<float>&)> onFrame;
    float xOf (double hz) const { return (float) (std::log (hz / 20.0) / std::log (1000.0)) * (float) getWidth(); }
    double hzAt (float x) const { return 20.0 * std::pow (1000.0, x / std::max (1, getWidth())); }
    float yOf (float db) const { return (float) getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo))); }
    float lo = -96.0f, hi = 6.0f; bool scale = true;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.ink.withAlpha (0.05f)); g.fillRect (r);
        g.setFont (mono (9.0f));
        if (scale)
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        { const float x = xOf (f); g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (x, 0.0f, 1.0f, r.getHeight()); g.setColour (theme.ink.withAlpha (0.5f)); g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f), (int) x + 2, (int) r.getHeight() - 12, 30, 12, juce::Justification::centredLeft); }
        if (scale) for (float d = hi - 6.0f; d > lo; d -= 12.0f) { const float y = yOf (d); g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (0.0f, y, r.getWidth(), 1.0f); g.setColour (theme.ink.withAlpha (0.45f)); g.drawText (juce::String ((int) d), 2, (int) y, 30, 11, juce::Justification::centredLeft); }
        const double sr = sampleRate();
        auto path = [&] (const std::vector<float>& v)
        {
            juce::Path p; bool started = false;
            for (int x = 0; x < getWidth(); x += 2)
            {
                const double f0 = hzAt ((float) x), f1 = hzAt ((float) x + 2.0f);
                const int k0 = juce::jlimit (1, kN / 2 - 1, (int) (f0 / sr * kN)), k1 = juce::jlimit (k0, kN / 2 - 1, (int) (f1 / sr * kN));
                float m = -200; for (int k = k0; k <= k1; ++k) m = std::max (m, v[(size_t) k]);
                const float y = yOf (m);
                if (! started) { p.startNewSubPath ((float) x, y); started = true; } else p.lineTo ((float) x, y);
            }
            return p;
        };
        auto pm = path (mag);
        juce::Path fill = pm; fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (0.0f, r.getBottom()); fill.closeSubPath();
        g.setColour (theme.fill.withAlpha (0.25f)); g.fillPath (fill);
        g.setColour (theme.fill); g.strokePath (pm, juce::PathStrokeType (1.3f));
        if (showPeaks) { g.setColour (theme.ink.withAlpha (0.35f)); g.strokePath (path (peak), juce::PathStrokeType (0.8f)); }
        if (! overlay.empty())
        {
            juce::Path c; bool st = false;
            for (auto& [hz, db] : overlay) { const float x = xOf (hz), y = (float) getHeight() * (0.5f - db / 48.0f); if (! st) { c.startNewSubPath (x, y); st = true; } else c.lineTo (x, y); }
            g.setColour (theme.ink); g.strokePath (c, juce::PathStrokeType (2.0f));
        }
    }
private:
    Theme theme; juce::dsp::FFT fft; juce::dsp::WindowingFunction<float> window; std::vector<float> mag, peak;
};

class Spectrogram : public juce::Component
{
public:
    explicit Spectrogram (Theme t) : theme (t) {}
    double sampleRate = 48000;
    void push (const std::vector<float>& mag)
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        if (img.getWidth() != getWidth() || img.getHeight() != getHeight()) { img = juce::Image (juce::Image::RGB, getWidth(), getHeight(), true); juce::Graphics (img).fillAll (juce::Colours::black); }
        img.moveImageSection (0, 0, 1, 0, img.getWidth() - 1, img.getHeight());
        const int x = img.getWidth() - 1, n = (int) mag.size();
        for (int y = 0; y < img.getHeight(); ++y)
        {
            const double hz = 20.0 * std::pow (1000.0, 1.0 - (double) y / img.getHeight());
            const int k = juce::jlimit (0, n - 1, (int) (hz / sampleRate * n * 2));
            const float t = juce::jlimit (0.0f, 1.0f, (mag[(size_t) k] + 100.0f) / 100.0f);
            img.setPixelAt (x, y, heat (t));
        }
        repaint();
    }
    static juce::Colour heat (float t)
    {
        // slate → blue → sky → pale yellow → white, the faltung night field's colours
        const juce::Colour stops[] = { juce::Colour (0xff10171c), juce::Colour (0xff2f4a8f), juce::Colour (0xff5aaad8), juce::Colour (0xffe3e9a4), juce::Colour (0xfffbfbf8) };
        const float p = t * 4.0f; const int i = juce::jlimit (0, 3, (int) p);
        return stops[i].interpolatedWith (stops[i + 1], p - (float) i);
    }
    void paint (juce::Graphics& g) override { g.drawImageAt (img, 0, 0); g.setColour (theme.ink.withAlpha (0.4f)); g.setFont (mono (9.0f)); g.drawText ("spectrogram", 4, 2, 120, 12, juce::Justification::centredLeft); }
private:
    Theme theme; juce::Image img;
};

// ---- stereo field: goniometer (mid up, side across) and correlation --------------------------------------------------
class Goniometer : public juce::Component
{
public:
    explicit Goniometer (Theme t) : theme (t) {}
    Watched src;
    void tick()
    {
        auto rt = src.rt();
        pts.clear();
        if (rt)
        {
            constexpr int n = 2048; std::vector<float> L (n), R (n); rt->scope.latest (L.data(), R.data(), n);
            double lr = 0, ll = 0, rr = 0;
            for (int i = 0; i < n; ++i) { pts.push_back ({ (R[(size_t) i] - L[(size_t) i]) * 0.7071f, (L[(size_t) i] + R[(size_t) i]) * 0.7071f }); lr += L[(size_t) i] * R[(size_t) i]; ll += L[(size_t) i] * L[(size_t) i]; rr += R[(size_t) i] * R[(size_t) i]; }
            const float c = (ll > 1.0e-9 && rr > 1.0e-9) ? (float) (lr / std::sqrt (ll * rr)) : 0.0f;
            corr = corr * 0.8f + c * 0.2f;
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto bar = r.removeFromBottom (18.0f);
        const float s = std::min (r.getWidth(), r.getHeight()) * 0.5f - 4.0f; const auto c = r.getCentre();
        g.setColour (theme.ink.withAlpha (0.12f)); g.drawLine (c.x - s, c.y, c.x + s, c.y); g.drawLine (c.x, c.y - s, c.x, c.y + s);
        g.drawLine (c.x - s * 0.7071f, c.y - s * 0.7071f, c.x + s * 0.7071f, c.y + s * 0.7071f); g.drawLine (c.x + s * 0.7071f, c.y - s * 0.7071f, c.x - s * 0.7071f, c.y + s * 0.7071f);
        g.setFont (mono (9.0f)); g.setColour (theme.ink.withAlpha (0.5f)); g.drawText ("l", (int) (c.x - s * 0.72f) - 10, (int) (c.y - s * 0.72f) - 12, 10, 10, juce::Justification::centred); g.drawText ("r", (int) (c.x + s * 0.72f), (int) (c.y - s * 0.72f) - 12, 10, 10, juce::Justification::centred);
        g.setColour (theme.fill.withAlpha (0.55f));
        for (auto& p : pts) { const float x = c.x + juce::jlimit (-1.0f, 1.0f, p.x) * s, y = c.y - juce::jlimit (-1.0f, 1.0f, p.y) * s; g.fillRect (x, y, 1.5f, 1.5f); }
        // correlation: -1 (out of phase) … +1 (mono)
        bar = bar.reduced (4.0f, 3.0f);
        g.setColour (theme.ink.withAlpha (0.1f)); g.fillRect (bar);
        const float cx = bar.getCentreX(), x = cx + bar.getWidth() * 0.5f * corr;
        g.setColour (corr < 0 ? juce::Colour (0xffee5a3e) : theme.fill); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, x), bar.getY(), std::max (cx, x), bar.getBottom()));
        g.setColour (theme.ink); g.setFont (mono (9.0f)); g.drawText ("correlation " + juce::String (corr, 2), bar.toNearestInt(), juce::Justification::centred);
    }
    float corr = 0;
private:
    Theme theme; std::vector<juce::Point<float>> pts;
};

// ---- scope: the waveform, triggered on a rising zero crossing ------------------------------------------------------
class Scope : public juce::Component
{
public:
    explicit Scope (Theme t) : theme (t) {}
    Watched src; int span = 1024;
    void tick()
    {
        auto rt = src.rt(); L.assign ((size_t) span, 0.0f); R.assign ((size_t) span, 0.0f);
        if (rt)
        {
            const int n = span * 3; std::vector<float> a ((size_t) n), b ((size_t) n); rt->scope.latest (a.data(), b.data(), n);
            int start = n - span; for (int i = n - span - 1; i > span; --i) if (a[(size_t) i - 1] < 0 && a[(size_t) i] >= 0) { start = i - span / 2; break; }
            start = juce::jlimit (0, n - span, start);
            for (int i = 0; i < span; ++i) { L[(size_t) i] = a[(size_t) (start + i)]; R[(size_t) i] = b[(size_t) (start + i)]; }
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme.ink.withAlpha (0.05f)); g.fillRect (r);
        g.setColour (theme.ink.withAlpha (0.15f)); g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
        auto draw = [&] (const std::vector<float>& v, juce::Colour c)
        {
            juce::Path p; for (int i = 0; i < (int) v.size(); ++i) { const float x = r.getX() + r.getWidth() * i / (float) std::max (1, (int) v.size() - 1), y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, v[(size_t) i]) * r.getHeight() * 0.45f; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.2f));
        };
        draw (R, theme.ink.withAlpha (0.45f)); draw (L, theme.fill);
        g.setColour (theme.ink.withAlpha (0.5f)); g.setFont (mono (9.0f)); g.drawText (juce::String (juce::CharPointer_UTF8 ("scope · l (bright) r (dim)")), 4, 2, 200, 12, juce::Justification::centredLeft);
    }
private:
    Theme theme; std::vector<float> L, R;
};
} // namespace um::ui

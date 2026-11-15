#pragma once
// ms0 master: the window. Dark stdio line of the faltung look.
//   top    — analyser (pre / post, mid blue, side orange) with the eq curves and draggable band points,
//            crossovers and per-band width; the imager (polar sample / lissajous, correlation, band widths)
//   bottom — eq band editor · dynamic eq · multiband + imaging · saturation + ambience · glue + limiter + output
// Included from Editor.cpp inside namespace fal (uses View, P(), ui::*).

namespace msui
{
const juce::Colour cMid (0xff58a2ec), cSide (0xffef7a3c), cPre (0x667f8c94), cCurveM (0xffe9eef0), cCurveS (0xffeba3ab);
inline float xOfHz (float hz, float w) { return w * std::log (std::max (10.0f, hz) / 20.0f) / std::log (1000.0f); }
inline float hzOfX (float x, float w) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / w)); }
inline juce::Colour chCol (int ch) { return ch == 1 ? cMid : (ch == 2 ? cSide : cCurveM); }
}

// ---------------------------------------------------------------------------------------------------
class Analyser : public juce::Component, public juce::SettableTooltipClient
{
public:
    Analyser (Processor& proc, ui::Theme t) : p (proc), theme (t), fft (13)
    {
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (kN - 1));
        setTooltip (ui::U ("drag a point: frequency / gain · wheel: q · double-click empty space: new band · right-click a point: type, channel, off · drag the dashed lines: crossovers"));
    }
    std::function<void (int)> onSelect;   // band index 0..7
    int selected = 0;
    bool showPre = true;

    void tick()
    {
        spectrum (p.master()->postMS, postM, postS);
        if (showPre) spectrum (p.master()->preMS, preM, preS);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth(), h = b.getHeight();
        g.setColour (theme.ground.brighter (0.06f)); g.fillRect (b);
        // frequency regions from the mastering notes
        static const std::pair<float, const char*> regions[] = { { 20, "subsonic" }, { 40, "sub" }, { 80, "body" }, { 160, "low-mid" }, { 350, "box" },
                                                                  { 700, "mid" }, { 1500, "presence" }, { 4000, "detail" }, { 8000, "air" }, { 16000, "ext" } };
        g.setFont (ui::mono (9.0f));
        for (size_t i = 0; i < std::size (regions); ++i)
        {
            const float x0 = msui::xOfHz (regions[i].first, w), x1 = i + 1 < std::size (regions) ? msui::xOfHz (regions[i + 1].first, w) : w;
            if (i % 2 == 0) { g.setColour (theme.ink.withAlpha (0.035f)); g.fillRect (x0, 0.0f, x1 - x0, h); }
            g.setColour (theme.ink.withAlpha (0.45f));
            g.drawText (regions[i].second, juce::Rectangle<float> (x0 + 3, h - 14, x1 - x0, 12), juce::Justification::centredLeft, true);
        }
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = msui::xOfHz (f, w);
            g.setColour (theme.ink.withAlpha (0.08f)); g.fillRect (x, 0.0f, 1.0f, h - 16);
            g.setColour (theme.ink.withAlpha (0.4f));
            g.drawText (f >= 1000 ? juce::String ((int) f / 1000) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 2, 16, 30, 10), juce::Justification::left);
        }
        for (int db : { -12, -6, 0, 6, 12 })
        {
            const float y = yOfDb ((float) db);
            g.setColour (theme.ink.withAlpha (db == 0 ? 0.18f : 0.07f)); g.fillRect (0.0f, y, w, 1.0f);
            g.setColour (theme.ink.withAlpha (0.4f)); g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (w - 26, y - 10, 24, 10), juce::Justification::right);
        }
        // spectra (Oval-coloured lines): pre faint, post mid / side
        if (showPre) { drawSpec (g, preM, msui::cPre, 1.0f); }
        drawSpec (g, postS, msui::cSide.withAlpha (0.85f), 1.4f);
        drawSpec (g, postM, msui::cMid, 1.8f);

        // crossovers + band widths
        const auto mp = p.readMaster();
        const float xs[3] = { mp.x1, mp.x2, mp.x3 };
        const float dash[2] = { 3, 4 };
        for (int k = 0; k < 3; ++k)
        {
            const float x = msui::xOfHz (xs[k], w);
            g.setColour (theme.ink.withAlpha (0.5f));
            g.drawDashedLine ({ x, 14.0f, x, h - 16 }, dash, 2, 1.0f);
        }
        auto* mc = p.master();
        static const char* bn[4] = { "sub", "low-mid", "mid-high", "high" };
        for (int k = 0; k < 4; ++k)
        {
            const float x0 = msui::xOfHz (k == 0 ? 20.0f : xs[k - 1], w), x1 = msui::xOfHz (k == 3 ? 20000.0f : xs[k], w);
            const float set = mp.mb[(size_t) k].width / 2.0f, meas = mc->bandWidth[(size_t) k].load();
            const float bw = (x1 - x0) - 12.0f;
            g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (x0 + 6, h - 30.0f, bw, 5.0f);
            g.setColour (msui::cSide.withAlpha (0.8f)); g.fillPath (ui::skewRect ({ x0 + 6, h - 30.0f, bw * juce::jlimit (0.0f, 1.0f, meas * 1.4f), 5.0f }, 0.5f));
            g.setColour (theme.ink); g.fillRect (x0 + 6 + bw * set, h - 33.0f, 2.0f, 11.0f);
            g.setFont (ui::mono (9.0f)); g.setColour (theme.ink.withAlpha (0.7f));
            g.drawText (juce::String (bn[k]) + " " + juce::String (juce::roundToInt (mp.mb[(size_t) k].width * 100)) + "%",
                        juce::Rectangle<float> (x0 + 6, h - 44.0f, bw, 11.0f), juce::Justification::centredLeft, true);
            const float gr = mc->bandGr[(size_t) k].load();
            if (gr > 0.05f) { g.setColour (theme.fill); g.fillRect (x0 + 6, 14.0f, bw * juce::jlimit (0.0f, 1.0f, gr / 6.0f), 3.0f); }
        }

        // eq curves: mid and side
        for (int ch : { 2, 1 })
        {
            juce::Path path;
            for (int x = 0; x <= (int) w; x += 2)
            {
                const double db = 20.0 * std::log10 (std::max (1.0e-6, MasterChain::eqMag (mp, msui::hzOfX ((float) x, w), ch)));
                const float y = yOfDb ((float) db);
                if (x == 0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
            }
            g.setColour (ch == 1 ? msui::cCurveM : msui::cCurveS);
            g.strokePath (path, juce::PathStrokeType (ch == 1 ? 2.0f : 1.6f));
        }
        // dynamic bands: a small bracket showing their reduction
        for (int k = 0; k < 2; ++k)
        {
            const auto& d = mp.dyn[(size_t) k];
            if (! mp.dynOn || ! d.on) continue;
            const float x = msui::xOfHz (d.freq, w), gr = mc->dynGr[(size_t) k].load();
            g.setColour (msui::chCol (d.ch).withAlpha (0.9f));
            g.drawLine (x, yOfDb (0), x, yOfDb (gr), 3.0f);
            g.setFont (ui::mono (9.0f)); g.drawText ("d" + juce::String (k + 1), juce::Rectangle<float> (x - 10, yOfDb (0) - 16, 20, 10), juce::Justification::centred);
        }
        // band points
        handles.clear();
        for (int k = 0; k < 8; ++k)
        {
            const auto& e = mp.eq[(size_t) k];
            const float x = msui::xOfHz (e.freq, w), y = yOfDb (e.type == 3 ? 0.0f : e.gain);
            handles.push_back ({ x, y });
            if (! e.on) continue;
            const auto r = juce::Rectangle<float> (x - 9, y - 6, 18, 12);
            g.setColour (msui::chCol (e.ch).withAlpha (k == selected ? 1.0f : 0.75f));
            g.fillPath (ui::skewRect (r, 0.4f));
            g.setColour (theme.ground); g.setFont (ui::mono (9.0f, true));
            g.drawText (juce::String (k + 1), r.translated (2, 0), juce::Justification::centred);
            if (k == selected) { g.setColour (juce::Colours::white); g.strokePath (ui::skewRect (r.expanded (2, 2), 0.4f), juce::PathStrokeType (1.2f)); }
        }
        g.setColour (theme.ink.withAlpha (0.7f)); g.setFont (ui::mono (9.0f));
        // legend, in the line colours
        float lx = 8;
        for (auto [txt, col] : { std::pair<const char*, juce::Colour> { "mid", msui::cMid }, { "side", msui::cSide }, { "eq mid", msui::cCurveM }, { "eq side", msui::cCurveS }, { "pre", msui::cPre } })
        {
            g.setColour (col); g.fillRect (lx, 6.0f, 12.0f, 2.0f);
            g.setColour (theme.ink.withAlpha (0.75f)); g.drawText (txt, juce::Rectangle<float> (lx + 15, 1, 60, 11), juce::Justification::left);
            lx += 22.0f + (float) juce::String (txt).length() * 6.5f;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1; xover = -1;
        const auto mp = p.readMaster();
        const float w = (float) getWidth();
        for (int k = 0; k < 8; ++k)
            if (mp.eq[(size_t) k].on && handles.size() > (size_t) k && handles[(size_t) k].getDistanceFrom (e.position) < 12)
            {
                drag = k; selected = k; if (onSelect) onSelect (k);
                if (e.mods.isPopupMenu()) { menu (k); drag = -1; return; }
                begin (k); return;
            }
        const float xs[3] = { mp.x1, mp.x2, mp.x3 };
        for (int k = 0; k < 3; ++k)
            if (std::abs (msui::xOfHz (xs[k], w) - e.position.x) < 5) { xover = k; P (p, "x" + juce::String (k + 1)).beginChangeGesture(); return; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float w = (float) getWidth();
        if (drag >= 0)
        {
            const juce::String x = "e" + juce::String (drag + 1);
            set (x + "freq", msui::hzOfX (e.position.x, w));
            if ((int) p.param (x + "type") != 3) set (x + "gain", juce::jlimit (-18.0f, 18.0f, dbOfY (e.position.y)));
        }
        else if (xover >= 0) set ("x" + juce::String (xover + 1), msui::hzOfX (e.position.x, w));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0) end (drag);
        if (xover >= 0) P (p, "x" + juce::String (xover + 1)).endChangeGesture();
        drag = xover = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto mp = p.readMaster();
        for (int k = 0; k < 8; ++k)
            if (! mp.eq[(size_t) k].on)
            {
                const juce::String x = "e" + juce::String (k + 1);
                p.setParam (x + "on", 1); p.setParam (x + "freq", msui::hzOfX (e.position.x, (float) getWidth()));
                p.setParam (x + "gain", juce::jlimit (-18.0f, 18.0f, dbOfY (e.position.y)));
                selected = k; if (onSelect) onSelect (k);
                return;
            }
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wd) override
    {
        const juce::String x = "e" + juce::String (selected + 1);
        p.setParam (x + "q", juce::jlimit (0.1f, 18.0f, p.param (x + "q") * (wd.deltaY > 0 ? 1.1f : 0.9f)));
    }

private:
    Processor& p; ui::Theme theme;
    static constexpr int kN = 8192;
    juce::dsp::FFT fft;
    std::array<float, kN> win {};
    std::array<float, 2 * kN> bufM {}, bufS {};
    std::array<float, kN> tmpM {}, tmpS {};
    std::vector<float> postM = std::vector<float> (kN / 2, -120.0f), postS = postM, preM = postM, preS = postM;
    std::vector<juce::Point<float>> handles;
    int drag = -1, xover = -1;

    float yOfDb (float db) const { return (float) getHeight() * 0.5f - db / 15.0f * ((float) getHeight() * 0.42f); }
    float dbOfY (float y) const { return ((float) getHeight() * 0.5f - y) / ((float) getHeight() * 0.42f) * 15.0f; }
    float yOfSpec (float db) const { return juce::jmap (db, -90.0f, 0.0f, (float) getHeight() - 16.0f, 14.0f); }

    void spectrum (const ScopeRing& ring, std::vector<float>& m, std::vector<float>& s)
    {
        ring.read (tmpM.data(), tmpS.data(), kN);
        std::fill (bufM.begin(), bufM.end(), 0.0f); std::fill (bufS.begin(), bufS.end(), 0.0f);
        for (int i = 0; i < kN; ++i) { bufM[(size_t) i] = tmpM[(size_t) i] * win[(size_t) i]; bufS[(size_t) i] = tmpS[(size_t) i] * win[(size_t) i]; }
        fft.performFrequencyOnlyForwardTransform (bufM.data(), true);
        fft.performFrequencyOnlyForwardTransform (bufS.data(), true);
        for (int k = 1; k < kN / 2; ++k)
        {
            // +3 dB / octave tilt so pink-ish music reads flat
            const float tilt = 10.0f * std::log10 ((float) k / (kN / 2.0f) * 24.0f + 1.0e-6f) * 0.5f;
            const float dm = 20.0f * std::log10 (bufM[(size_t) k] / (kN * 0.25f) + 1.0e-9f) + tilt;
            const float ds = 20.0f * std::log10 (bufS[(size_t) k] / (kN * 0.25f) + 1.0e-9f) + tilt;
            m[(size_t) k] = std::max (dm, m[(size_t) k] * 0.75f + dm * 0.25f - 1.5f);
            s[(size_t) k] = std::max (ds, s[(size_t) k] * 0.75f + ds * 0.25f - 1.5f);
        }
    }
    void drawSpec (juce::Graphics& g, const std::vector<float>& v, juce::Colour c, float th)
    {
        const float w = (float) getWidth(), sr = (float) p.getSampleRate() > 0 ? (float) p.getSampleRate() : 48000.0f;
        juce::Path path; bool started = false;
        for (int x = 0; x < (int) w; x += 2)
        {
            const float f0 = msui::hzOfX ((float) x, w), f1 = msui::hzOfX ((float) x + 2.0f, w);
            const int k0 = std::max (1, (int) (f0 / sr * kN)), k1 = std::min (kN / 2 - 1, std::max (k0, (int) (f1 / sr * kN)));
            float mx = -120.0f; for (int k = k0; k <= k1; ++k) mx = std::max (mx, v[(size_t) k]);
            const float y = yOfSpec (mx);
            if (! started) { path.startNewSubPath ((float) x, y); started = true; } else path.lineTo ((float) x, y);
        }
        g.setColour (c); g.strokePath (path, juce::PathStrokeType (th));
    }
    void set (const juce::String& id, float v) { if (auto* r = p.apvts.getParameter (id)) r->setValueNotifyingHost (r->convertTo0to1 (v)); }
    void begin (int k) { for (auto s : { "freq", "gain" }) P (p, "e" + juce::String (k + 1) + s).beginChangeGesture(); }
    void end (int k) { for (auto s : { "freq", "gain" }) P (p, "e" + juce::String (k + 1) + s).endChangeGesture(); }
    void menu (int k)
    {
        juce::PopupMenu m;
        const juce::String x = "e" + juce::String (k + 1);
        const int type = (int) p.param (x + "type"), ch = (int) p.param (x + "ch");
        const char* types[4] = { "bell", "low shelf", "high shelf", "notch" };
        const char* chs[3] = { "stereo", "mid", "side" };
        for (int i = 0; i < 4; ++i) m.addItem (1 + i, types[i], true, i == type);
        m.addSeparator();
        for (int i = 0; i < 3; ++i) m.addItem (10 + i, chs[i], true, i == ch);
        m.addSeparator(); m.addItem (20, "band off");
        juce::Component::SafePointer<Analyser> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, x] (int r)
        {
            if (! safe || r <= 0) return;
            if (r <= 4) safe->p.setParam (x + "type", (float) (r - 1));
            else if (r < 20) safe->p.setParam (x + "ch", (float) (r - 10));
            else safe->p.setParam (x + "on", 0);
        });
    }
};

// ---------------------------------------------------------------------------------------------------
/** stereo imager: polar sample (like Ozone's) or lissajous, correlation, per-band width */
class Imager : public juce::Component, public juce::SettableTooltipClient
{
public:
    Imager (Processor& proc, ui::Theme t) : p (proc), theme (t) { setTooltip ("click: polar sample / polar level / lissajous"); }
    void tick() { p.master()->postLR.read (l.data(), r.data(), kN); repaint(); }
    void mouseDown (const juce::MouseEvent&) override { mode = (mode + 1) % 3; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (theme.ground.brighter (0.06f)); g.fillRect (b);
        const float w = b.getWidth();
        const auto scope = juce::Rectangle<float> (10, 22, w - 20, b.getHeight() - 130);
        const auto c = juce::Point<float> (scope.getCentreX(), mode == 2 ? scope.getCentreY() : scope.getBottom() - 4);
        const float R = mode == 2 ? std::min (scope.getWidth(), scope.getHeight()) * 0.48f : std::min (scope.getWidth() * 0.5f, scope.getHeight() - 8);
        g.setFont (ui::mono (10.0f, true)); g.setColour (theme.ink);
        g.drawText (mode == 0 ? "imager  polar sample" : (mode == 1 ? "imager  polar level" : "imager  lissajous"), juce::Rectangle<float> (10, 4, w - 20, 14), juce::Justification::left);
        // guides: L / R at 45 degrees, mono vertical, out-of-phase horizontal
        g.setColour (theme.ink.withAlpha (0.18f));
        if (mode == 2) g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.0f);
        else { juce::Path arc; arc.addCentredArc (c.x, c.y, R, R, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true); g.strokePath (arc, juce::PathStrokeType (1.0f)); }
        const float d = R * 0.7071f;
        g.drawLine (c.x, c.y, c.x - d, c.y - d, 1.0f); g.drawLine (c.x, c.y, c.x + d, c.y - d, 1.0f);
        g.drawLine (c.x, c.y, c.x, c.y - R, 1.0f);
        if (mode == 2) { g.drawLine (c.x, c.y, c.x + d, c.y + d, 1.0f); g.drawLine (c.x, c.y, c.x - d, c.y + d, 1.0f); g.drawLine (c.x, c.y, c.x, c.y + R, 1.0f); }
        g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.0f);
        g.setFont (ui::mono (9.0f)); g.setColour (theme.ink.withAlpha (0.6f));
        g.drawText ("L", juce::Rectangle<float> (c.x - d - 14, c.y - d - 12, 12, 10), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (c.x + d + 2, c.y - d - 12, 12, 10), juce::Justification::centred);
        // the samples
        float pk = 1.0e-4f; for (int i = 0; i < kN; ++i) pk = std::max (pk, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
        const float sc = R / std::max (0.05f, pk) * 0.95f;
        if (mode == 1)
        {
            // polar level: energy per angle sector, drawn as rays
            std::array<float, 64> bins {};
            for (int i = 0; i < kN; ++i)
            {
                const float m = 0.5f * (l[(size_t) i] + r[(size_t) i]), s = 0.5f * (l[(size_t) i] - r[(size_t) i]);
                const float a = std::atan2 (s * (m >= 0 ? 1.0f : -1.0f), std::abs (m));   // -pi/2 .. pi/2
                const int k = juce::jlimit (0, 63, (int) ((a / juce::MathConstants<float>::pi + 0.5f) * 64.0f));
                bins[(size_t) k] += m * m + s * s;
            }
            float mx = 1.0e-9f; for (auto v : bins) mx = std::max (mx, v);
            for (int k = 0; k < 64; ++k)
            {
                const float a = ((float) k + 0.5f) / 64.0f * juce::MathConstants<float>::pi - juce::MathConstants<float>::halfPi;
                const float len = R * std::sqrt (bins[(size_t) k] / mx);
                g.setColour ((std::abs (a) > 0.785f ? msui::cSide : msui::cMid).withAlpha (0.85f));
                g.drawLine (c.x, c.y, c.x + std::sin (a) * len, c.y - std::cos (a) * len, 2.0f);
            }
        }
        else
            for (int i = 0; i < kN; i += 2)
            {
                const float m = 0.5f * (l[(size_t) i] + r[(size_t) i]), s = 0.5f * (l[(size_t) i] - r[(size_t) i]);
                float x, y;
                if (mode == 0) { const float sg = m >= 0 ? 1.0f : -1.0f; x = c.x + s * sg * sc * 1.414f; y = c.y - std::abs (m) * sc * 1.414f; }
                else { x = c.x + s * sc; y = c.y - m * sc; }
                const bool wide = std::abs (s) > std::abs (m);
                g.setColour ((wide ? msui::cSide : msui::cMid).withAlpha (0.35f));
                g.fillRect (x, y, 1.6f, 1.6f);
            }
        // correlation
        auto* mc = p.master();
        const float co = mc->corr.load();
        const auto cb = juce::Rectangle<float> (10, b.getHeight() - 100, w - 20, 8);
        g.setColour (theme.ink.withAlpha (0.15f)); g.fillRect (cb);
        g.setColour (co < 0 ? msui::cSide : theme.fill);
        const float cx = cb.getX() + cb.getWidth() * (co + 1.0f) * 0.5f;
        g.fillRect (std::min (cx, cb.getCentreX()), cb.getY(), std::abs (cx - cb.getCentreX()), cb.getHeight());
        g.setColour (theme.ink); g.fillRect (cx - 1, cb.getY() - 3, 2.0f, cb.getHeight() + 6);
        g.setFont (ui::mono (9.0f));
        g.drawText ("-1", cb.translated (0, 10).withHeight (10), juce::Justification::left);
        g.drawText ("correlation " + juce::String (co, 2), cb.translated (0, 10).withHeight (10), juce::Justification::centred);
        g.drawText ("+1", cb.translated (0, 10).withHeight (10), juce::Justification::right);
        // band widths
        static const char* bn[4] = { "sub", "low-mid", "mid-high", "high" };
        for (int k = 0; k < 4; ++k)
        {
            const float y = b.getHeight() - 70 + k * 15.0f;
            const float meas = mc->bandWidth[(size_t) k].load(), set = p.param ("b" + juce::String (k + 1) + "width") / 2.0f;
            g.setColour (theme.ink.withAlpha (0.8f)); g.drawText (bn[k], juce::Rectangle<float> (10, y, 60, 11), juce::Justification::left);
            const auto bar = juce::Rectangle<float> (72, y + 2, w - 130, 7);
            g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (bar);
            g.setColour (msui::cSide.withAlpha (0.85f)); g.fillPath (ui::skewRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, meas * 1.4f)), 0.5f));
            g.setColour (theme.ink); g.fillRect (bar.getX() + bar.getWidth() * set - 1, y, 2.0f, 11.0f);
            g.drawText (juce::String (juce::roundToInt (set * 200)) + "%", juce::Rectangle<float> (w - 54, y, 44, 11), juce::Justification::right);
        }
    }
private:
    Processor& p; ui::Theme theme; int mode = 0;
    static constexpr int kN = 2048;
    std::array<float, kN> l {}, r {};
};

// ---------------------------------------------------------------------------------------------------
/** a titled stack of controls; the title carries the stage's on switch */
class Stage : public juce::Component
{
public:
    Stage (juce::String t, ui::Theme th) : title (std::move (t)), theme (th) {}
    void setSwitch (juce::RangedAudioParameter& prm) { sw = std::make_unique<ui::ToggleBox> (prm, theme, "on"); addAndMakeVisible (*sw); }
    void add (juce::Component* c, int h = 17, bool own = true) { if (own) owned.add (c); items.push_back (c); heights.push_back (h); addAndMakeVisible (c); }
    void addGap() { items.push_back (nullptr); heights.push_back (7); }
    void clearItems() { for (auto* c : items) if (c) removeChildComponent (c); items.clear(); heights.clear(); owned.clear(); }
    int topPad = 0;
    int wanted() const { int h = 22 + topPad; for (size_t i = 0; i < heights.size(); ++i) h += heights[i] + 1; return h; }
    void resized() override
    {
        if (sw) sw->setBounds (getWidth() - 56, 0, 56, 16);
        int y = 22 + topPad;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (auto* c = items[i]) c->setBounds (0, y, getWidth(), heights[i]);
            y += heights[i] + 1;
        }
    }
    std::vector<int> heights;
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme.ink); g.setFont (ui::mono (10.5f, true));
        g.drawText (title, 0, 0, getWidth() - 60, 16, juce::Justification::centredLeft);
        g.setColour (theme.fill.withAlpha (0.7f)); g.fillRect (0, 17, getWidth(), 1);
    }
    std::vector<juce::Component*> items;
    juce::OwnedArray<juce::Component> owned;
private:
    juce::String title; ui::Theme theme; std::unique_ptr<ui::ToggleBox> sw;
};

class MasterView : public View, private juce::Timer
{
public:
    explicit MasterView (Processor& proc) : p (proc), pi (proc.pinfo), theme (makeTheme (proc.pinfo)), analyser (proc, theme), imager (proc, theme),
        presetBar (proc, theme), listen (P (proc, "listen"), theme, " "), bypass (P (proc, "bypass"), theme, "compare"),
        match (P (proc, "match"), theme, "level-matched"), reset ("reset meters", theme),
        abA ("a", theme), abB ("b", theme), abCopy ("copy", theme), goTarget ("go to target", theme), autoBtn ("auto", theme)
    {
        // auto: listen to the track (20 s of sound), then set everything once
        addAndMakeVisible (autoBtn);
        autoBtn.onClick = [this]
        {
            if (p.autoListening()) p.autoStop();          // early: decides from what it heard (4 s at least)
            else { p.autoStart (20.0); autoNote = "listening: play the track (the loudest, busiest part is best)"; }
        };
        startTimerHz (10);
        listen.asTabs = true;
        for (auto* c : std::initializer_list<juce::Component*> { &analyser, &imager, &presetBar, &listen, &bypass, &match, &reset }) addAndMakeVisible (c);
        reset.onClick = [this] { if (auto* m = p.master()) m->resetMeters(); };
        for (auto* c : std::initializer_list<juce::Component*> { &abA, &abB, &abCopy }) addAndMakeVisible (c);
        abA.onClick = [this] { p.abSwitch (0); refreshAb(); };
        abB.onClick = [this] { p.abSwitch (1); refreshAb(); };
        abCopy.onClick = [this] { p.abCopy(); };
        goTarget.onClick = [this] { p.goToTarget(); };
        refreshAb();
        analyser.onSelect = [this] (int k) { selectBand (k); };

        // eq band editor (rebuilt on selection)
        eqStage = std::make_unique<Stage> ("eq  (linear phase)", theme); eqStage->setSwitch (P (p, "eqon")); addAndMakeVisible (*eqStage);
        for (int k = 0; k < 10; ++k)
        {
            auto* t = tabs.add (new ui::TextLink (k < 8 ? juce::String (k + 1) : (k == 8 ? "hp" : "lp"), theme));
            t->onClick = [this, k] { selectBand (k); };
            addAndMakeVisible (t);
        }
        auto addS = [this] (Stage& s, const juce::String& id, const juce::String& label)
        {
            auto& prm = P (p, id);
            juce::Component* c;
            if (dynamic_cast<juce::AudioParameterChoice*> (&prm)) c = new ui::ChoiceBox (prm, theme, label);
            else if (dynamic_cast<juce::AudioParameterBool*> (&prm)) c = new ui::ToggleBox (prm, theme, label);
            else { auto* sl = new ui::ParamSlider (prm, theme, label); sl->setLabelWidth (62); c = sl; }
            s.add (c);
        };
        dyn = std::make_unique<Stage> ("dynamic eq  (linear phase)", theme); dyn->setSwitch (P (p, "dynon"));
        for (int k = 1; k <= 2; ++k)
        {
            const juce::String x = "d" + juce::String (k);
            addS (*dyn, x + "on", "band " + juce::String (k)); addS (*dyn, x + "ch", "channel"); addS (*dyn, x + "freq", "freq"); addS (*dyn, x + "q", "q");
            addS (*dyn, x + "thr", "threshold"); addS (*dyn, x + "range", "range"); addS (*dyn, x + "att", "attack"); addS (*dyn, x + "rel", "release");
            if (k == 1) dyn->addGap();
        }
        mb = std::make_unique<Stage> ("multiband + imaging", theme); mb->setSwitch (P (p, "mbon"));
        addS (*mb, "x1", "x-over 1"); addS (*mb, "x2", "x-over 2"); addS (*mb, "x3", "x-over 3"); addS (*mb, "imgon", "imaging"); addS (*mb, "air", "air");
        addS (*mb, "midlvl", "mid level"); addS (*mb, "sidelvl", "side level");
        mb->addGap();
        mb->add (new BandTable (p, theme), 16 + 7 * 18);
        sat = std::make_unique<Stage> ("saturation  (4x)", theme); sat->setSwitch (P (p, "saton"));
        addS (*sat, "satcurve", "curve"); addS (*sat, "satfocus", "focus"); addS (*sat, "satdrive", "drive"); addS (*sat, "satmix", "mix");
        amb = std::make_unique<Stage> ("ambience  (parallel)", theme); amb->setSwitch (P (p, "ambon"));
        addS (*amb, "amblevel", "level"); addS (*amb, "ambsize", "size"); addS (*amb, "ambdecay", "decay"); addS (*amb, "ambhp", "high-pass"); addS (*amb, "amblp", "low-pass"); addS (*amb, "ambwidth", "width");
        glue = std::make_unique<Stage> ("glue", theme); glue->setSwitch (P (p, "glueon"));
        addS (*glue, "gthr", "threshold"); addS (*glue, "gratio", "ratio"); addS (*glue, "gatt", "attack"); addS (*glue, "grel", "release"); addS (*glue, "gauto", "auto release"); addS (*glue, "gmakeup", "makeup"); addS (*glue, "gmix", "mix");
        clip = std::make_unique<Stage> ("clipper  (oversampled)", theme); clip->setSwitch (P (p, "clipon"));
        addS (*clip, "clipdrive", "drive"); addS (*clip, "clipsoft", "softness");
        lim = std::make_unique<Stage> ("true-peak limiter", theme); lim->setSwitch (P (p, "limon"));
        addS (*lim, "limgain", "gain"); addS (*lim, "ceiling", "ceiling"); addS (*lim, "limos", "oversample"); addS (*lim, "limrel", "release");
        addS (*lim, "limauto", "auto release"); addS (*lim, "lookahead", "lookahead"); addS (*lim, "limlink", "stereo link");
        io = std::make_unique<Stage> ("input / output", theme);
        addS (*io, "trim", "input"); addS (*io, "balance", "balance"); addS (*io, "dcon", "dc filter"); addS (*io, "target", "target");
        io->add (&goTarget, 17, false);
        addS (*io, "out", "output"); addS (*io, "dither", "dither"); addS (*io, "shaping", "shaping");
        for (auto* s : { dyn.get(), mb.get(), sat.get(), amb.get(), clip.get(), glue.get(), lim.get(), io.get() }) addAndMakeVisible (s);
        selectBand (0);
        setSize (pi.w, pi.h);
    }

    static ui::Theme makeTheme (const ProductInfo& pi)
    {
        const juce::Colour g (pi.ground);
        return { g, ui::inkFor (g), juce::Colour (pi.accent), juce::Colours::white };
    }

    void selectBand (int k)
    {
        band = k;
        analyser.selected = std::min (k, 7);
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setLit (i == k);
        eqStage->clearItems(); eqStage->topPad = 44;
        auto add = [this] (const juce::String& id, const juce::String& label)
        {
            auto& prm = P (p, id);
            juce::Component* c;
            if (dynamic_cast<juce::AudioParameterChoice*> (&prm)) c = new ui::ChoiceBox (prm, theme, label);
            else if (dynamic_cast<juce::AudioParameterBool*> (&prm)) c = new ui::ToggleBox (prm, theme, label);
            else { auto* sl = new ui::ParamSlider (prm, theme, label); sl->setLabelWidth (62); c = sl; }
            eqStage->add (c);
        };
        if (k < 8)
        {
            const juce::String x = "e" + juce::String (k + 1);
            add (x + "on", "band " + juce::String (k + 1)); add (x + "type", "type"); add (x + "ch", "channel");
            add (x + "freq", "freq"); add (x + "gain", "gain"); add (x + "q", "q");
        }
        else
        {
            const juce::String x = k == 8 ? "hp" : "lp";
            add (x + "on", k == 8 ? "high-pass" : "low-pass"); add (x + "freq", "freq"); add (x + "slope", "slope"); add (x + "ch", "channel");
        }
        eqStage->resized();
        resized();
    }

    void resized() override
    {
        const int w = getWidth();
        listen.setBounds (370, 13, 300, 22);
        bypass.setBounds (684, 13, 110, 22);
        match.setBounds (796, 13, 140, 22);
        presetBar.setBounds (w - 236, 13, 200, 22);
        autoBtn.setBounds (w - 36 - 110 - 14 - 200, 410, 200, 18);
        abA.setBounds (w - 236 - 150, 13, 34, 22); abB.setBounds (w - 236 - 114, 13, 34, 22); abCopy.setBounds (w - 236 - 78, 13, 60, 22);
        analyser.setBounds (36, 52, w - 36 - 36 - 300 - 16, 360);
        imager.setBounds (w - 36 - 300, 52, 300, 360);
        const int top = 430, colW = (w - 72 - 4 * 22) / 5;
        auto col = [&] (int i) { return 36 + i * (colW + 22); };
        // column 1: eq editor
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds (col (0) + (i % 5) * (colW / 5), top + 24 + (i / 5) * 20, colW / 5, 18);
        eqStage->setBounds (col (0), top, colW, eqStage->wanted());
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->toFront (false);
        dyn->setBounds (col (1), top, colW, dyn->wanted());
        mb->setBounds (col (2), top, colW, mb->wanted());
        sat->setBounds (col (3), top, colW, sat->wanted());
        amb->setBounds (col (3), top + sat->wanted() + 12, colW, amb->wanted());
        clip->setBounds (col (3), top + sat->wanted() + amb->wanted() + 24, colW, clip->wanted());
        glue->setBounds (col (4), top, colW, glue->wanted());
        lim->setBounds (col (4), top + glue->wanted() + 14, colW, lim->wanted());
        io->setBounds (col (0), top + 196, colW, io->wanted());
        reset.setBounds (w - 36 - 110, 410, 110, 18);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme.ground);
        // the cube + title
        auto P2 = [&] (float a, float b) { return juce::Point<float> (36 + a * 0.29f, 9 + b * 0.29f); };
        juce::Path t, l, r;
        t.startNewSubPath (P2 (0, 30)); t.lineTo (P2 (52, 0)); t.lineTo (P2 (104, 30)); t.lineTo (P2 (52, 60)); t.closeSubPath();
        l.startNewSubPath (P2 (0, 30)); l.lineTo (P2 (52, 60)); l.lineTo (P2 (52, 112)); l.lineTo (P2 (0, 82)); l.closeSubPath();
        r.startNewSubPath (P2 (52, 60)); r.lineTo (P2 (104, 30)); r.lineTo (P2 (104, 82)); r.lineTo (P2 (52, 112)); r.closeSubPath();
        g.setColour (juce::Colour (0xffffe15a)); g.fillPath (t); g.setColour (juce::Colour (0xfff2c100)); g.fillPath (l); g.setColour (juce::Colour (0xffd8a800)); g.fillPath (r);
        g.setColour (theme.ink); g.setFont (ui::sans (18.0f, true)); g.drawText ("ms0 master", 76, 10, 160, 26, juce::Justification::centredLeft);
        g.setFont (ui::mono (10.0f)); g.setColour (theme.ink.withAlpha (0.6f)); g.drawText ("faltung", 76, 32, 160, 12, juce::Justification::centredLeft);
        g.setColour (theme.ink); g.setFont (ui::mono (10.0f)); g.drawText ("listen", 320, 13, 48, 22, juce::Justification::centredLeft);
        // auto: its progress under the button, and what it decided (or is doing) on the line below
        if (p.autoListening())
        {
            auto b = autoBtn.getBounds().toFloat();
            g.setColour (theme.ink.withAlpha (0.15f)); g.fillRect (b.getX(), b.getBottom() + 2, b.getWidth(), 3.0f);
            g.setColour (theme.fill); g.fillRect (b.getX(), b.getBottom() + 2, b.getWidth() * p.autoProgress(), 3.0f);
        }
        if (autoNote.isNotEmpty())
        {
            g.setColour (theme.ink.withAlpha (0.8f)); g.setFont (ui::mono (9.5f));
            g.drawFittedText (autoNote, juce::Rectangle<int> (370, 37, getWidth() - 370 - 36, 13), juce::Justification::centredLeft, 1, 0.8f);
        }
        // meters line
        if (auto* m = p.master())
        {
            const auto& o = m->outMeter;
            auto lu = [] (float v) { return v < -99 ? juce::String ("-") : juce::String (v, 1); };
            g.setFont (ui::mono (10.5f));
            const float iv = o.integrated.load(), tpv = m->truePeak.load();
            juce::String tgt;
            const int ti = (int) p.param ("target");
            if (ti > 0 && iv > -70) { const float d = Processor::targetLufs (ti) - iv; tgt = "   to target " + juce::String (d > 0 ? "+" : "") + juce::String (d, 1); }
            const juce::String txt = "m " + lu (o.momentary.load()) + "  s " + lu (o.shortTerm.load()) + "  i " + lu (iv) + " lufs  lra " + juce::String (o.range.load(), 1)
                                   + tgt + "    tp " + lu (tpv) + " dbtp  plr " + (iv > -70 ? juce::String (tpv - iv, 1) : juce::String ("-"))
                                   + "    glue " + juce::String (m->glueGr.load(), 1) + "  limit " + juce::String (m->limGr.load(), 1) + " db"
                                   + "    latency " + juce::String (1000.0 * m->latency() / std::max (1.0, p.getSampleRate()), 0) + " ms";
            g.drawText (txt, juce::Rectangle<float> (36, 414, (float) getWidth() - 72, 14), juce::Justification::centredLeft);
            // gain reduction bars
            const float lg = m->limGr.load();
            g.setColour (theme.fill); g.fillPath (ui::skewRect ({ (float) getWidth() - 236, 418, juce::jmin (200.0f, lg * 20.0f), 6 }, 0.5f));
        }
        g.setColour (theme.ink.withAlpha (0.3f)); g.fillRect (36.0f, 428.0f, (float) getWidth() - 72, 1.0f);
        g.setColour (theme.ink.withAlpha (0.55f)); g.setFont (ui::mono (9.5f));
        g.drawText ("listen modes are for monitoring - set stereo before you bounce.  compare = bypass with matched loudness, to judge each change honestly.",
                    juce::Rectangle<float> (36, (float) getHeight() - 20, (float) getWidth() - 72, 12), juce::Justification::centredLeft);
    }
    void tick() override { analyser.tick(); imager.tick(); repaint (0, 405, getWidth(), 25); }

private:
    /** the 4 x 7 band table */
    struct BandTable : juce::Component
    {
        BandTable (Processor& proc, ui::Theme t) : p (proc), theme (t)
        {
            static const char* rows[7] = { "thr", "ratio", "att", "rel", "gain", "width", "solo" };
            for (int r = 0; r < 7; ++r)
                for (int b = 1; b <= 4; ++b)
                {
                    const juce::String id = "b" + juce::String (b) + rows[r];
                    auto& prm = P (p, id);
                    juce::Component* c;
                    if (dynamic_cast<juce::AudioParameterBool*> (&prm)) c = new ui::ToggleBox (prm, theme, " ");
                    else c = new Cell (prm, theme);
                    cells.add (c); addAndMakeVisible (c);
                }
        }
        int rowsWanted = 7;
        void resized() override
        {
            const int lw = 40, cw = (getWidth() - lw) / 4;
            for (int i = 0; i < cells.size(); ++i) cells[i]->setBounds (lw + (i % 4) * cw, 16 + (i / 4) * 18, cw - 4, 17);
        }
        void paint (juce::Graphics& g) override
        {
            static const char* rows[7] = { "thr", "ratio", "att", "rel", "gain", "width", "solo" };
            static const char* heads[4] = { "sub", "lo-mid", "mi-hi", "high" };
            const int lw = 40, cw = (getWidth() - lw) / 4;
            g.setFont (ui::mono (9.5f, true)); g.setColour (theme.ink);
            for (int b = 0; b < 4; ++b) g.drawText (heads[b], lw + b * cw, 0, cw, 14, juce::Justification::centredLeft);
            g.setFont (ui::mono (9.5f));
            for (int r = 0; r < 7; ++r) g.drawText (rows[r], 0, 16 + r * 18, lw, 17, juce::Justification::centredLeft);
        }
        /** compact value: text + bar, vertical drag */
        struct Cell : juce::Component
        {
            Cell (juce::RangedAudioParameter& pr, ui::Theme t) : prm (pr), theme (t), att (pr, [this] (float) { repaint(); }, nullptr)
            { att.sendInitialUpdate(); setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
            void paint (juce::Graphics& g) override
            {
                const float v = prm.getValue();
                g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (0.0f, (float) getHeight() - 3, (float) getWidth(), 2.0f);
                g.setColour (theme.fill); g.fillRect (0.0f, (float) getHeight() - 3, (float) getWidth() * v, 2.0f);
                g.setColour (theme.ink); g.setFont (ui::mono (9.5f));
                auto t = prm.getCurrentValueAsText().replace (" db", "").replace (" ms", "").replace (":1", "");
                g.drawText (t, getLocalBounds().withTrimmedBottom (3), juce::Justification::centredLeft, true);
            }
            void mouseDown (const juce::MouseEvent&) override { start = prm.getValue(); att.beginGesture(); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                const float v = juce::jlimit (0.0f, 1.0f, start - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 600.0f : 150.0f));
                att.setValueAsPartOfGesture (prm.convertFrom0to1 (v));
            }
            void mouseUp (const juce::MouseEvent&) override { att.endGesture(); }
            void mouseDoubleClick (const juce::MouseEvent&) override { att.setValueAsCompleteGesture (prm.convertFrom0to1 (prm.getDefaultValue())); }
            juce::RangedAudioParameter& prm; ui::Theme theme; juce::ParameterAttachment att; float start = 0;
        };
        Processor& p; ui::Theme theme; juce::OwnedArray<juce::Component> cells;
    };

    Processor& p;
    const ProductInfo& pi;
    ui::Theme theme;
    Analyser analyser;
    Imager imager;
    ui::PresetBar presetBar;
    ui::ChoiceBox listen;
    ui::ToggleBox bypass, match;
    ui::TextLink reset, abA, abB, abCopy, goTarget, autoBtn;
    juce::String autoNote;
    void timerCallback() override
    {
        if (p.autoReady())
        {
            autoNote = p.applyAuto();
            refreshAb();
            repaint();
        }
        const bool l = p.autoListening();
        autoBtn.setLit (l);
        autoBtn.setText (l ? "listening " + juce::String ((int) p.autoHeard()) + " / 20 s  (stop)" : "auto");
        if (l) repaint (autoBtn.getBounds().expanded (0, 6));
    }
    void refreshAb() { abA.setLit (p.abCurrent == 0); abB.setLit (p.abCurrent == 1); }
    juce::OwnedArray<ui::TextLink> tabs;
    std::unique_ptr<Stage> eqStage, dyn, mb, sat, amb, clip, glue, lim, io;
    int band = 0;
};

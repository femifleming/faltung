#pragma once
// ms0 master: the window. Dark stdio line of the faltung look.
//   top    — analyser (pre / post, mid blue, side orange) with the eq curves and draggable band points,
//            crossovers and per-band width; the imager (polar sample / lissajous, correlation, band widths)
//   bottom — eq band editor · dynamic eq · multiband + imaging · saturation + ambience · glue + limiter + output
//            under the dynamic eq its two bands in numbers; under the limiter its meter (vu, true peak, gain reduction)
// Included from Editor.cpp inside namespace fal (uses View, P(), ui::*).

namespace msui
{
const juce::Colour cMid (0xff58a2ec), cSide (0xffef7a3c), cPre (0x667f8c94), cCurveM (0xffe9eef0), cCurveS (0xffeba3ab);
inline float xOfHz (float hz, float w) { return w * std::log (std::max (10.0f, hz) / 20.0f) / std::log (1000.0f); }
inline float hzOfX (float x, float w) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / w)); }
inline juce::Colour chCol (int ch) { return ch == 1 ? cMid : (ch == 2 ? cSide : cCurveM); }
/** "412 hz" / "2.80 khz"; precise adds a digit, for the read-outs while a band moves */
inline juce::String hzText (float f, bool precise = false)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, (f >= 10000.0f ? 1 : 2) + (precise ? 1 : 0)) + " khz";
    if (precise || f < 100.0f) return juce::String (f, 1) + " hz";
    return juce::String (juce::roundToInt (f)) + " hz";
}
/** "291" / "1.98k": a frequency in as little room as possible */
inline juce::String hzShort (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "k";
    return f < 100.0f ? juce::String (f, 1) : juce::String (juce::roundToInt (f));
}
/** "291 – 583 hz": the two -3 dB edges of a band */
inline juce::String edgesText (float lo, float hi) { return hzShort (lo) + ui::U (" \xe2\x80\x93 ") + hzShort (hi) + " hz"; }
inline juce::String dbText (float v, int dp = 1) { return (v > 0 ? "+" : "") + juce::String (v, dp) + " db"; }
}

// ---------------------------------------------------------------------------------------------------
class Analyser : public juce::Component, public juce::SettableTooltipClient
{
public:
    Analyser (Processor& proc, ui::Theme t) : p (proc), theme (t), fft (13)
    {
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (kN - 1));
        setTooltip (ui::U ("drag a point: frequency / gain (d1, d2: frequency / range) · shift: fine · alt-drag or wheel: q · cmd-drag d1, d2: threshold · "
                           "double-click a point: type its frequency · double-click empty space: new band · right-click a point: type, channel, exact values, off · "
                           "drag the dashed lines: crossovers"));
    }
    std::function<void (int)> onSelect;      // eq band index 0..7
    std::function<void (int)> onSelectDyn;   // dynamic band 0..1
    /** asks the window for a text field over `where` (in this component) to type the parameter's value */
    std::function<void (juce::RangedAudioParameter&, juce::Rectangle<int> where)> onType;
    int selected = 0;
    int dynSel = -1;      // the dynamic band in focus: drawn with its edges labelled, and shown even while it is off
    bool showPre = true;

    void tick()
    {
        spectrum (p.master()->postMS, postM, postS);
        if (showPre) spectrum (p.master()->preMS, preM, preS);
        repaint();
    }
    /** brings a dynamic band forward (its sliders were touched): edges labelled, the wheel turns its q */
    void focusDyn (int k) { dynSel = k; lastKind = 2; repaint(); }
    /** an eq band was chosen: the wheel turns its q again */
    void focusEq (int k) { selected = k; lastKind = 1; repaint(); }
    /** screenshots / tests: as if the mouse rested on a dynamic band's point (-1: nowhere) */
    void hoverDynForTest (int k) { hover = k < 0 ? Hit {} : Hit { 2, k }; if (k >= 0) dynSel = k; repaint(); }
    const DynResponse& response (int k) const { return dynResp[(size_t) k]; }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth(), h = b.getHeight();
        const int W = (int) w;
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
        // the frequency axis: a line at every 1-2-5 step, labelled; faint ticks at the steps between
        for (float dec : { 10.0f, 100.0f, 1000.0f, 10000.0f })
            for (int m = 2; m <= 10; ++m)
            {
                const float f = dec * (float) m;
                if (f < 30.0f || f > 19000.0f) continue;
                const bool major = m == 2 || m == 5 || m == 10;
                const float x = msui::xOfHz (f, w);
                g.setColour (theme.ink.withAlpha (major ? 0.08f : 0.035f)); g.fillRect (x, major ? 0.0f : 12.0f, 1.0f, h - 16 - (major ? 0.0f : 12.0f));
                if (! major) continue;
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

        // the eq, mid and side, at every pixel (also what the dynamic bands sit on)
        eqLin[0].resize ((size_t) W + 1); eqLin[1].resize ((size_t) W + 1);
        for (int x = 0; x <= W; ++x)
        {
            const float f = msui::hzOfX ((float) x, w);
            eqLin[0][(size_t) x] = (float) MasterChain::eqMag (mp, f, 1);
            eqLin[1][(size_t) x] = (float) MasterChain::eqMag (mp, f, 2);
        }

        // dynamic bands, behind the eq curves: the bell each band really has (measured from its filter).
        // outline = the most it may do (its range) · filled = what it is doing now · the bar across = its -3 dB width
        const double srate = p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0;
        const float yZero = yOfDb (0);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (0, 12, W, (int) h - 12 - 16);
            for (int k = 0; k < 2; ++k)
            {
                const auto& d = mp.dyn[(size_t) k];
                const bool live = mp.dynOn && d.on, focus = dynFocus (k);
                dynShown[(size_t) k] = live || focus;
                if (! dynShown[(size_t) k]) continue;
                auto& R = dynResp[(size_t) k]; R.update (srate, d.freq, d.q);
                const auto col = msui::chCol (d.ch);
                const float a = live ? 1.0f : 0.45f, gr = live ? mc->dynGr[(size_t) k].load() : 0.0f;
                const auto& eq = eqLin[d.ch == 2 ? 1 : 0];
                juce::Path most, now;
                most.startNewSubPath (0.0f, yZero); now.startNewSubPath (0.0f, yZero);
                for (int x = 0; x <= W; ++x)
                {
                    const float f = msui::hzOfX ((float) x, w);
                    most.lineTo ((float) x, yOfDb (R.changeDb (f, d.range, eq[(size_t) x])));
                    now.lineTo ((float) x, yOfDb (R.changeDb (f, gr, eq[(size_t) x])));
                }
                most.lineTo (w, yZero); now.lineTo (w, yZero);
                const float xl = msui::xOfHz (std::max (20.0f, R.loHz), w), xh = msui::xOfHz (std::min (20000.0f, R.hiHz), w);
                if (focus) { g.setColour (col.withAlpha (0.07f)); g.fillRect (xl, 12.0f, xh - xl, h - 28.0f); }
                g.setColour (col.withAlpha (0.10f * a)); g.fillPath (most);
                if (std::abs (gr) > 0.02f) { g.setColour (col.withAlpha (0.42f)); g.fillPath (now); }
                juce::Path dashed; const float dl[2] = { 4, 3 };
                juce::PathStrokeType (1.1f).createDashedStroke (dashed, most, dl, 2);
                g.setColour (col.withAlpha (0.8f * a)); g.fillPath (dashed);
                if (std::abs (gr) > 0.02f) { g.setColour (col); g.strokePath (now, juce::PathStrokeType (1.7f)); }
            }
        }

        // eq curves: mid and side
        for (int ch : { 2, 1 })
        {
            juce::Path path;
            const auto& eq = eqLin[ch == 2 ? 1 : 0];
            for (int x = 0; x <= W; x += 2)
            {
                const float y = yOfDb (20.0f * std::log10 (std::max (1.0e-6f, eq[(size_t) x])));
                if (x == 0) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
            }
            g.setColour (ch == 1 ? msui::cCurveM : msui::cCurveS);
            g.strokePath (path, juce::PathStrokeType (ch == 1 ? 2.0f : 1.6f));
        }
        // eq band points
        handles.clear();
        for (int k = 0; k < 8; ++k)
        {
            const auto& e = mp.eq[(size_t) k];
            const float x = msui::xOfHz (e.freq, w), y = juce::jlimit (20.0f, h - 24.0f, yOfDb (e.type == 3 ? 0.0f : e.gain));
            handles.push_back ({ x, y });
            if (! e.on) continue;
            const auto r = juce::Rectangle<float> (x - 9, y - 6, 18, 12);
            g.setColour (msui::chCol (e.ch).withAlpha (k == selected ? 1.0f : 0.75f));
            g.fillPath (ui::skewRect (r, 0.4f));
            g.setColour (theme.ground); g.setFont (ui::mono (9.0f, true));
            g.drawText (juce::String (k + 1), r.translated (2, 0), juce::Justification::centred);
            if (k == selected && lastKind == 1) { g.setColour (theme.edge); g.strokePath (ui::skewRect (r.expanded (2, 2), 0.4f), juce::PathStrokeType (1.2f)); }
        }
        // dynamic band points, with the width bar: its ends are the -3 dB edges of the band's filter
        for (int k = 0; k < 2; ++k)
        {
            const auto& d = mp.dyn[(size_t) k];
            const float x = msui::xOfHz (d.freq, w), y = juce::jlimit (22.0f, h - 54.0f, yOfDb (d.range));
            dynHandles[(size_t) k] = { x, y };
            if (! dynShown[(size_t) k]) continue;
            const auto& R = dynResp[(size_t) k];
            const bool live = mp.dynOn && d.on, focus = dynFocus (k);
            const auto col = msui::chCol (d.ch).withAlpha (live ? 1.0f : 0.5f);
            const float xl = msui::xOfHz (std::max (20.0f, R.loHz), w), xh = msui::xOfHz (std::min (20000.0f, R.hiHz), w);
            const float yc = juce::jlimit (20.0f, h - 50.0f, yOfDb (R.changeDb (R.loHz, d.range)));
            g.setColour (col);
            g.fillRect (xl, yc - 0.75f, xh - xl, 1.5f);
            g.fillRect (xl - 0.75f, yc - 5.0f, 1.5f, 10.0f); g.fillRect (xh - 0.75f, yc - 5.0f, 1.5f, 10.0f);
            if (focus)
            {
                // the edges in hz, either side of the bar
                g.setFont (ui::mono (9.5f, true));
                const auto lo = msui::hzShort (R.loHz), hi = msui::hzShort (R.hiHz);
                const float lw = (float) lo.length() * 5.9f + 6.0f, hw = (float) hi.length() * 5.9f + 6.0f;
                const juce::Rectangle<float> lr (xl - 4 - lw, yc - 6, lw, 12), hr (xh + 4, yc - 6, hw, 12);
                g.setColour (theme.ground.withAlpha (0.8f)); g.fillRect (lr); g.fillRect (hr);
                g.setColour (col); g.drawText (lo, lr, juce::Justification::centred); g.drawText (hi, hr, juce::Justification::centred);
            }
            const auto r = juce::Rectangle<float> (x - 12, y - 7, 24, 14);
            const auto shape = ui::skewRect (r, 0.4f);
            g.setColour (focus ? col : theme.ground); g.fillPath (shape);
            g.setColour (focus ? theme.edge : col); g.strokePath (shape, juce::PathStrokeType (focus ? 1.2f : 1.5f));
            g.setColour (focus ? theme.ground : col); g.setFont (ui::mono (9.0f, true));
            g.drawText ("d" + juce::String (k + 1), r.translated (2, 0), juce::Justification::centred);
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
        g.setColour (theme.ink.withAlpha (0.75f));
        g.drawText (ui::U ("d1 d2 dynamic eq: dashed = range · filled = now · bar = width at -3 db"), juce::Rectangle<float> (lx + 6, 1, 420, 11), juce::Justification::left);

        // the read-out beside the point under the mouse (or being dragged)
        const Hit shown = dragHit.kind != 0 ? dragHit : hover;
        if (shown.kind == 1 && mp.eq[(size_t) shown.index].on)
        {
            const auto& e = mp.eq[(size_t) shown.index];
            static const char* types[4] = { "bell", "low shelf", "high shelf", "notch" };
            static const char* chs[3] = { "stereo", "mid", "side" };
            juce::StringArray lines;
            lines.add (ui::U ("eq ") + juce::String (shown.index + 1) + ui::U (" · ") + types[juce::jlimit (0, 3, e.type)] + ui::U (" · ") + chs[juce::jlimit (0, 2, e.ch)]);
            lines.add (msui::hzText (e.freq, true) + "   " + ui::noteName (e.freq));
            lines.add ((e.type == 3 ? juce::String() : "gain " + msui::dbText (e.gain) + ui::U (" · ")) + "q " + juce::String (e.q, 2)
                       + (e.type == 0 || e.type == 3 ? ui::U (" · ") + juce::String (DynResponse::octavesForQ (e.q), 2) + " oct" : juce::String()));
            infoBox (g, handles[(size_t) shown.index], lines, msui::chCol (e.ch));
        }
        else if (shown.kind == 2 && dynShown[(size_t) shown.index])
        {
            const auto& d = mp.dyn[(size_t) shown.index];
            const auto& R = dynResp[(size_t) shown.index];
            static const char* chs[3] = { "stereo", "mid", "side" };
            const bool live = mp.dynOn && d.on;
            juce::StringArray lines;
            lines.add ("dynamic " + juce::String (shown.index + 1) + ui::U (" · ") + chs[juce::jlimit (0, 2, d.ch)] + (live ? juce::String() : ui::U (" · off")));
            lines.add (msui::hzText (d.freq, true) + "   " + ui::noteName (d.freq));
            lines.add ("q " + juce::String (d.q, 2) + ui::U (" · ") + juce::String (R.octaves(), 2) + ui::U (" oct · ") + msui::edgesText (R.loHz, R.hiHz));
            if (R.peak < 0.94f) lines.add ("this narrow, this low: reaches " + juce::String (juce::roundToInt (R.peak * 100.0f)) + "% of the range");
            lines.add ("range " + msui::dbText (d.range) + ui::U (" · threshold ") + msui::dbText (d.thr));
            {
                // the band adds to the eq's output: where the eq has moved the level, the same range changes it by more or less
                const float eqHere = (float) MasterChain::eqMag (mp, d.freq, d.ch == 2 ? 2 : 1);
                const float real = R.changeDb (d.freq, d.range, eqHere), alone = R.changeDb (d.freq, d.range);
                if (std::abs (real - alone) > 0.3f) lines.add ("on the eq's " + msui::dbText (20.0f * std::log10 (std::max (1.0e-4f, eqHere))) + " here: " + msui::dbText (real) + " at most");
            }
            if (live) lines.add ("in band " + juce::String (mc->dynLevel[(size_t) shown.index].load(), 1) + ui::U (" db · now ") + msui::dbText (mc->dynGr[(size_t) shown.index].load()));
            infoBox (g, dynHandles[(size_t) shown.index], lines, msui::chCol (d.ch));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = pointAt (e.position);
        if (hit.kind != hover.kind || hit.index != hover.index) { hover = hit; repaint(); }
        setMouseCursor (hit.kind == 3 ? juce::MouseCursor::LeftRightResizeCursor : (hit.kind != 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
    }
    void mouseExit (const juce::MouseEvent&) override { if (hover.kind != 0) { hover = {}; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragHit = {};
        const auto hit = pointAt (e.position);
        if (hit.kind == 1)
        {
            selected = hit.index; lastKind = 1; if (onSelect) onSelect (hit.index);
            if (e.mods.isPopupMenu()) { menu (hit.index); return; }
        }
        else if (hit.kind == 2)
        {
            dynSel = hit.index; lastKind = 2; if (onSelectDyn) onSelectDyn (hit.index);
            if (e.mods.isPopupMenu()) { dynMenu (hit.index); return; }
        }
        else if (hit.kind == 0 && dynSel >= 0) dynSel = -1;   // a click on nothing lets the dynamic band go
        dragHit = hit;
        anchorMode = -1;   // the first drag sets the anchor
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragHit.kind == 0) return;
        const float w = (float) getWidth();
        if (dragHit.kind == 3) { setG ("x" + juce::String (dragHit.index + 1), msui::hzOfX (e.position.x, w)); return; }
        const bool dyn = dragHit.kind == 2, fine = e.mods.isShiftDown();
        const juce::String x = (dyn ? "d" : "e") + juce::String (dragHit.index + 1), gainId = x + (dyn ? "range" : "gain");
        // alt: q · cmd (dynamic bands): threshold · else frequency + gain. shift slows any of them ten times.
        const int mode = e.mods.isAltDown() ? 1 : (dyn && e.mods.isCommandDown() ? 2 : 0);
        if (mode != anchorMode || fine != anchorFine)
        {
            // values and mouse position the movement is measured from; taken again whenever a modifier changes,
            // so nothing jumps
            anchorMode = mode; anchorFine = fine; anchor = e.position;
            aFreq = p.param (x + "freq"); aGain = p.param (gainId); aQ = p.param (x + "q"); aThr = dyn ? p.param (x + "thr") : 0.0f;
        }
        const float k = fine ? 0.1f : 1.0f, dx = (e.position.x - anchor.x) * k, dy = (e.position.y - anchor.y) * k;
        if (mode == 1) setG (x + "q", aQ * std::exp2 (-dy / 40.0f));                 // up = narrower, an octave of q per 40 px
        else if (mode == 2) setG (x + "thr", aThr - dy * 0.25f);                     // up = higher threshold, 4 px per db
        else
        {
            setG (x + "freq", msui::hzOfX (msui::xOfHz (aFreq, w) + dx, w));
            if (dyn || (int) p.param (x + "type") != 3) setG (gainId, aGain - dy / ((float) getHeight() * 0.42f) * 15.0f);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& id : gestures) P (p, id).endChangeGesture();
        gestures.clear();
        dragHit = {};
        hover = pointAt (e.position);
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // on a point: type its frequency
        const auto hit = pointAt (e.position);
        if (hit.kind == 1) { typeAt ("e" + juce::String (hit.index + 1) + "freq", handles[(size_t) hit.index]); return; }
        if (hit.kind == 2) { typeAt ("d" + juce::String (hit.index + 1) + "freq", dynHandles[(size_t) hit.index]); return; }
        if (hit.kind != 0) return;
        const auto mp = p.readMaster();
        for (int k = 0; k < 8; ++k)
            if (! mp.eq[(size_t) k].on)
            {
                const juce::String x = "e" + juce::String (k + 1);
                p.setParam (x + "on", 1); p.setParam (x + "freq", msui::hzOfX (e.position.x, (float) getWidth()));
                p.setParam (x + "gain", juce::jlimit (-18.0f, 18.0f, dbOfY (e.position.y)));
                selected = k; lastKind = 1; if (onSelect) onSelect (k);
                return;
            }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        const float turn = wd.deltaY + wd.deltaX;   // (with shift held macOS sends the wheel sideways)
        if (std::abs (turn) < 1.0e-6f) return;
        // the point under the mouse, else the band last touched
        Hit t = hover.kind == 1 || hover.kind == 2 ? hover : (lastKind == 2 && dynSel >= 0 ? Hit { 2, dynSel } : Hit { 1, selected });
        const juce::String id = (t.kind == 2 ? "d" : "e") + juce::String (t.index + 1) + "q";
        const float step = e.mods.isShiftDown() ? 0.02f : 0.1f;
        p.setParam (id, p.param (id) * (turn > 0 ? 1.0f + step : 1.0f - step));
    }

private:
    struct Hit { int kind = 0; int index = -1; };   // kind: 0 nothing · 1 eq point · 2 dynamic point · 3 crossover
    Processor& p; ui::Theme theme;
    static constexpr int kN = 8192;
    juce::dsp::FFT fft;
    std::array<float, kN> win {};
    std::array<float, 2 * kN> bufM {}, bufS {};
    std::array<float, kN> tmpM {}, tmpS {};
    std::vector<float> postM = std::vector<float> (kN / 2, -120.0f), postS = postM, preM = postM, preS = postM;
    std::vector<juce::Point<float>> handles;
    std::array<juce::Point<float>, 2> dynHandles {};
    std::array<bool, 2> dynShown {};
    std::array<DynResponse, 2> dynResp;
    std::array<std::vector<float>, 2> eqLin;
    Hit hover, dragHit;
    int lastKind = 1;                       // what the wheel turns when the mouse is on no point: 1 eq, 2 dynamic
    int anchorMode = -1; bool anchorFine = false;
    juce::Point<float> anchor;
    float aFreq = 0, aGain = 0, aQ = 1, aThr = 0;
    juce::StringArray gestures;             // parameters with an open change gesture (ended on mouse up)

    float yOfDb (float db) const { return (float) getHeight() * 0.5f - db / 15.0f * ((float) getHeight() * 0.42f); }
    float dbOfY (float y) const { return ((float) getHeight() * 0.5f - y) / ((float) getHeight() * 0.42f) * 15.0f; }
    float yOfSpec (float db) const { return juce::jmap (db, -90.0f, 0.0f, (float) getHeight() - 16.0f, 14.0f); }
    bool dynFocus (int k) const { return dynSel == k || (hover.kind == 2 && hover.index == k) || (dragHit.kind == 2 && dragHit.index == k); }

    Hit pointAt (juce::Point<float> pos) const
    {
        Hit best; float bd = 13.0f;
        for (int k = 0; k < 2; ++k)
            if (dynShown[(size_t) k]) { const float d = dynHandles[(size_t) k].getDistanceFrom (pos); if (d < bd) { bd = d; best = { 2, k }; } }
        const auto mp = p.readMaster();
        for (int k = 0; k < 8 && k < (int) handles.size(); ++k)
            if (mp.eq[(size_t) k].on) { const float d = handles[(size_t) k].getDistanceFrom (pos); if (d < bd - 0.5f) { bd = d; best = { 1, k }; } }
        if (best.kind != 0) return best;
        const float xs[3] = { mp.x1, mp.x2, mp.x3 };
        for (int k = 0; k < 3; ++k)
            if (std::abs (msui::xOfHz (xs[k], (float) getWidth()) - pos.x) < 5) return { 3, k };
        return {};
    }
    /** sets a parameter as part of the drag; its gesture opens on first use */
    void setG (const juce::String& id, float v)
    {
        auto* r = p.apvts.getParameter (id);
        if (! r) return;
        if (! gestures.contains (id)) { r->beginChangeGesture(); gestures.add (id); }
        const auto range = r->getNormalisableRange();
        r->setValueNotifyingHost (r->convertTo0to1 (juce::jlimit (range.start, range.end, v)));
    }
    void typeAt (const juce::String& id, juce::Point<float> at)
    {
        if (! onType) return;
        auto r = juce::Rectangle<int> ((int) at.x + 16, (int) at.y - 9, 84, 18);
        r.setPosition (juce::jlimit (0, getWidth() - r.getWidth(), r.getX()), juce::jlimit (0, getHeight() - r.getHeight(), r.getY()));
        onType (P (p, id), r);
    }
    void infoBox (juce::Graphics& g, juce::Point<float> at, const juce::StringArray& lines, juce::Colour col)
    {
        g.setFont (ui::mono (10.0f));
        float tw = 0; for (auto& l : lines) tw = std::max (tw, (float) l.length() * 6.05f);
        // above the point and above the 0 db line, so it never covers the bell it describes
        const float bh = (float) lines.size() * 13.0f + 8;
        juce::Rectangle<float> r (at.x + 18, std::min (at.y, yOfDb (0)) - 16 - bh, tw + 16, bh);
        if (r.getRight() > (float) getWidth() - 4) r.setX (at.x - 18 - r.getWidth());
        if (r.getY() < 14) r.setY (std::max (at.y, yOfDb (0)) + 18);
        r.setX (juce::jlimit (2.0f, std::max (2.0f, (float) getWidth() - r.getWidth() - 2), r.getX()));
        g.setColour (theme.ground.withAlpha (0.93f)); g.fillRect (r);
        g.setColour (col); g.fillRect (r.getX(), r.getY(), 2.0f, r.getHeight());
        g.setColour (theme.ink.withAlpha (0.25f)); g.drawRect (r, 1.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setFont (ui::mono (10.0f, i <= 1));
            g.setColour (theme.ink.withAlpha (i == 0 ? 0.7f : 1.0f));
            g.drawText (lines[i], juce::Rectangle<float> (r.getX() + 9, r.getY() + 4 + (float) i * 13.0f, r.getWidth() - 12, 13), juce::Justification::centredLeft);
        }
    }

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
        m.addSeparator();
        m.addItem (30, "type the frequency..."); m.addItem (31, "type the gain...", type != 3); m.addItem (32, "type the q...");
        m.addSeparator(); m.addItem (20, "band off");
        juce::Component::SafePointer<Analyser> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, x, k] (int r)
        {
            if (! safe || r <= 0) return;
            if (r <= 4) safe->p.setParam (x + "type", (float) (r - 1));
            else if (r < 20) safe->p.setParam (x + "ch", (float) (r - 10));
            else if (r == 20) safe->p.setParam (x + "on", 0);
            else { static const char* ids[3] = { "freq", "gain", "q" }; safe->typeAt (x + ids[r - 30], safe->handles[(size_t) k]); }
        });
    }
    void dynMenu (int k)
    {
        juce::PopupMenu m;
        const juce::String x = "d" + juce::String (k + 1);
        const int ch = (int) p.param (x + "ch"); const bool on = p.param (x + "on") > 0.5f;
        const char* chs[3] = { "stereo", "mid", "side" };
        for (int i = 0; i < 3; ++i) m.addItem (10 + i, chs[i], true, i == ch);
        m.addSeparator();
        m.addItem (30, "type the frequency..."); m.addItem (31, "type the q..."); m.addItem (32, "type the threshold..."); m.addItem (33, "type the range...");
        m.addSeparator(); m.addItem (20, on ? "band off" : "band on");
        juce::Component::SafePointer<Analyser> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, x, k, on] (int r)
        {
            if (! safe || r <= 0) return;
            if (r < 20) safe->p.setParam (x + "ch", (float) (r - 10));
            else if (r == 20) safe->p.setParam (x + "on", on ? 0.0f : 1.0f);
            else { static const char* ids[4] = { "freq", "q", "thr", "range" }; safe->typeAt (x + ids[r - 30], safe->dynHandles[(size_t) k]); }
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

// ---------------------------------------------------------------------------------------------------
/** a slider whose value can also be typed: double-click the value at its right ("1.2k", "850", "a4", "-4.5") */
class TypeSlider : public ui::ParamSlider
{
public:
    TypeSlider (juce::RangedAudioParameter& prm, ui::Theme t, juce::String label) : ui::ParamSlider (prm, t, std::move (label))
    {
        setTooltip (prm.getName (64) + ui::U ("  (drag · shift = fine · double-click the value = type it · double-click the bar = default)"));
    }
    std::function<void (juce::RangedAudioParameter&, juce::Component&, juce::Rectangle<int>)> onType;
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (onType && e.x >= getWidth() - 70) onType (param, *this, getLocalBounds().removeFromRight (78));
        else ui::ParamSlider::mouseDoubleClick (e);
    }
};

// ---------------------------------------------------------------------------------------------------
/** the two dynamic bands in numbers: centre and its note, q and the width that q gives, the -3 dB edges, what the
    band changes now, and what it hears against its threshold. Double-click the frequency or the q to type it. */
class DynReadout : public juce::Component, public juce::SettableTooltipClient
{
public:
    DynReadout (Processor& proc, ui::Theme t) : p (proc), theme (t)
    {
        setTooltip (ui::U ("each dynamic band: centre and note · q = width in octaves · the -3 db edges · hears: the level in the band (bar) against the threshold (mark) · "
                           "double-click the frequency or the q to type it"));
    }
    std::function<void (juce::RangedAudioParameter&, juce::Component&, juce::Rectangle<int>)> onType;
    std::function<void (int)> onSelect;
    static constexpr int kRow = 47, kHeight = 2 * kRow - 4;

    void paint (juce::Graphics& g) override
    {
        auto* mc = p.master();
        if (! mc) return;
        const auto mp = p.readMaster();
        const double srate = p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0;
        const float w = (float) getWidth();
        for (int k = 0; k < 2; ++k)
        {
            const auto& d = mp.dyn[(size_t) k];
            const bool live = mp.dynOn && d.on;
            auto& R = resp[(size_t) k]; R.update (srate, d.freq, d.q);
            const float y = (float) (k * kRow), a = live ? 1.0f : 0.45f;
            const auto col = msui::chCol (d.ch);
            const float gr = live ? mc->dynGr[(size_t) k].load() : 0.0f, lv = live ? mc->dynLevel[(size_t) k].load() : -120.0f;
            // line 1: d1 · centre · note · what it does now
            g.setColour (col.withAlpha (a)); g.fillPath (ui::skewRect ({ 0.0f, y + 3.0f, 9.0f, 7.0f }, 0.4f));
            g.setFont (ui::mono (10.0f, true)); g.setColour (theme.ink.withAlpha (a));
            g.drawText ("d" + juce::String (k + 1), juce::Rectangle<float> (14, y, 18, 12), juce::Justification::centredLeft);
            g.drawText (msui::hzText (d.freq, true), freqArea (k).toFloat(), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f));
            if (R.peak < 0.94f) { g.setColour (msui::cSide.withAlpha (a)); g.drawText ("reach " + juce::String (juce::roundToInt (R.peak * 100.0f)) + "%", juce::Rectangle<float> (98, y, 62, 12), juce::Justification::centredLeft); }
            else { g.setColour (theme.ink.withAlpha (0.7f * a)); g.drawText (ui::noteName (d.freq), juce::Rectangle<float> (98, y, 62, 12), juce::Justification::centredLeft); }
            g.setColour (std::abs (gr) > 0.05f ? col : theme.ink.withAlpha (0.6f * a));
            g.drawText (live ? juce::String (gr > 0 ? "+" : "") + juce::String (gr, 1) + " db" : juce::String ("off"), juce::Rectangle<float> (w - 70, y, 70, 12), juce::Justification::centredRight);
            // line 2: q · the width it gives · the edges
            g.setColour (theme.ink.withAlpha (a)); g.setFont (ui::mono (10.0f, true));
            g.drawText ("q " + juce::String (d.q, 2), qArea (k).toFloat(), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f)); g.setColour (theme.ink.withAlpha (0.85f * a));
            g.drawText (juce::String (R.octaves(), 2) + " oct", juce::Rectangle<float> (78, y + 13, 54, 12), juce::Justification::centredLeft);
            g.drawText (msui::edgesText (R.loHz, R.hiHz), juce::Rectangle<float> (120, y + 13, w - 120, 12), juce::Justification::centredRight);
            // line 3: what the band hears, against its threshold (-60 .. 0 db)
            g.setColour (theme.ink.withAlpha (0.7f * a)); g.setFont (ui::mono (9.0f));
            g.drawText ("hears", juce::Rectangle<float> (0, y + 27, 30, 11), juce::Justification::centredLeft);
            const juce::Rectangle<float> bar (32, y + 29, w - 32 - 56, 7);
            auto xOf = [&] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
            g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (bar);
            const float xl = xOf (lv), xt = xOf (d.thr);
            if (xl > bar.getX() + 0.5f) { g.setColour (theme.fill.withAlpha (0.9f)); g.fillPath (ui::skewRect (bar.withRight (std::min (xl, xt)), 0.5f)); }
            if (xl > xt) { g.setColour (col); g.fillPath (ui::skewRect ({ xt, bar.getY(), xl - xt, bar.getHeight() }, 0.5f)); }   // over the threshold: the band works
            g.setColour (theme.edge.withAlpha (a)); g.fillRect (xt - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
            g.setColour (theme.ink.withAlpha (a)); g.setFont (ui::mono (9.5f));
            g.drawText (lv < -100.0f ? juce::String ("-") : juce::String (lv, 1) + " db", juce::Rectangle<float> (w - 56, y + 27, 56, 11), juce::Justification::centredRight);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (onSelect) onSelect (e.y >= kRow ? 1 : 0); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! onType) return;
        for (int k = 0; k < 2; ++k)
        {
            const juce::String x = "d" + juce::String (k + 1);
            if (freqArea (k).contains (e.getPosition())) { onType (P (p, x + "freq"), *this, freqArea (k).withWidth (84).expanded (2, 3)); return; }
            if (qArea (k).contains (e.getPosition())) { onType (P (p, x + "q"), *this, qArea (k).withWidth (64).expanded (2, 3)); return; }
        }
    }
private:
    Processor& p; ui::Theme theme;
    std::array<DynResponse, 2> resp;
    juce::Rectangle<int> freqArea (int k) const { return { 32, k * kRow, 64, 12 }; }
    juce::Rectangle<int> qArea (int k) const { return { 32, k * kRow + 13, 44, 12 }; }
};

// ---------------------------------------------------------------------------------------------------
/** the limiter's meter. Going in (after the limiter's gain) and coming out, left and right: a VU bar (the 300 ms
    needle; a sine at the reference level reads 0 vu), the true peak as a thin mark and its 2 s hold as a white one,
    the ceiling as a line. Below: the gain reduction, from the right, with its own hold. */
class LimiterMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LimiterMeter (Processor& proc, ui::Theme t) : p (proc), theme (t)
    {
        setTooltip (ui::U ("bar: vu (300 ms needle; a sine at the reference level reads 0 vu) · short mark: true peak · tall thin mark: its hold (2 s) · "
                           "line: the ceiling · in = after the limiter's gain · gr: what the limiter takes off, and the most in the last 2 s · "
                           "click \"0 vu = ...\" to choose the reference level"));
    }
    static constexpr int kHeight = 100;
    static constexpr float kLo = -42.0f, kHi = 6.0f, kGrMax = 12.0f;

    void paint (juce::Graphics& g) override
    {
        auto* m = p.master();
        if (! m) return;
        const float w = (float) getWidth();
        g.setColour (theme.ink); g.setFont (ui::mono (10.5f, true));
        g.drawText ("limiter meter", 0, 0, 110, 16, juce::Justification::centredLeft);
        g.setFont (ui::mono (9.5f)); g.setColour (theme.ink.withAlpha (0.8f));
        g.drawText ("0 vu = " + juce::String (p.vuRef) + " dbfs", refArea(), juce::Justification::centredRight);
        g.setColour (theme.fill.withAlpha (0.7f)); g.fillRect (0, 17, getWidth(), 1);

        const float x0 = 34.0f, x1 = w - 66.0f, bw = x1 - x0, top = 32.0f;
        auto xOf = [&] (float db) { return x0 + bw * juce::jlimit (0.0f, 1.0f, (db - kLo) / (kHi - kLo)); };
        auto rowY = [&] (int i) { return top + (float) i * 12.0f + (i >= 2 ? 3.0f : 0.0f); };
        const float bottom = rowY (3) + 11.0f;
        g.setFont (ui::mono (8.5f));
        for (int db : { -36, -24, -12, 0 })
        {
            const float x = xOf ((float) db);
            g.setColour (theme.ink.withAlpha (0.45f)); g.drawText (juce::String (db), juce::Rectangle<float> (x - 14, 20, 28, 10), juce::Justification::centred);
            g.setColour (theme.ink.withAlpha (0.14f)); g.fillRect (x, top - 1.0f, 1.0f, bottom - top + 2.0f);
        }
        g.setColour (theme.ink.withAlpha (0.6f));
        g.drawText ("vu", juce::Rectangle<float> (x1 + 2, 20, 30, 10), juce::Justification::centredRight);
        g.drawText ("dbtp", juce::Rectangle<float> (x1 + 34, 20, 32, 10), juce::Justification::centredRight);

        const float ceil = p.param ("ceiling"), xc = xOf (ceil);
        static const char* names[4] = { "in  l", "    r", "out l", "    r" };
        for (int i = 0; i < 4; ++i)
        {
            const auto& lm = i < 2 ? m->limIn : m->limOut;
            const int c = i % 2;
            const float y = rowY (i), vu = lm.vuDb (c), pk = lm.peakDb (c), hd = lm.holdDb (c);
            g.setFont (ui::mono (9.5f)); g.setColour (theme.ink.withAlpha (0.85f));
            g.drawText (names[i], juce::Rectangle<float> (0, y, x0 - 2, 11), juce::Justification::centredLeft);
            g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (x0, y + 2.0f, bw, 7.0f);
            const float xv = xOf (vu), xp = xOf (pk);
            if (xv > x0 + 0.5f) { g.setColour (theme.fill); g.fillPath (ui::skewRect ({ x0, y + 2.0f, xv - x0, 7.0f }, 0.5f)); }
            // peaks above the ceiling: on the way in, what the limiter has to take off
            if (pk > ceil && xp > xc) { g.setColour (msui::cSide.withAlpha (0.6f)); g.fillRect (xc, y + 4.0f, xp - xc, 3.0f); }
            if (pk > kLo) { g.setColour (theme.ink); g.fillRect (xp - 1.0f, y + 2.0f, 2.0f, 7.0f); }
            if (hd > kLo) { g.setColour (theme.edge); g.fillRect (xOf (hd) - 0.5f, y - 0.5f, 1.0f, 12.0f); }
            g.setColour (theme.ink);
            g.drawText (vu < -80.0f ? juce::String ("-") : juce::String (vu - (float) p.vuRef > 0 ? "+" : "") + juce::String (vu - (float) p.vuRef, 1),
                        juce::Rectangle<float> (x1 + 2, y, 30, 11), juce::Justification::centredRight);
            if (i >= 2 && hd > ceil + 0.05f) g.setColour (msui::cSide);
            g.drawText (hd < -80.0f ? juce::String ("-") : juce::String (hd > 0 ? "+" : "") + juce::String (hd, 1), juce::Rectangle<float> (x1 + 34, y, 32, 11), juce::Justification::centredRight);
        }
        // the reference (0 vu) and the ceiling, across the four rows
        const float xr = xOf ((float) p.vuRef);
        const float dash[2] = { 2, 2 };
        g.setColour (theme.ink.withAlpha (0.6f)); g.drawDashedLine ({ xr, top - 2.0f, xr, bottom + 1.0f }, dash, 2, 1.0f);
        g.setColour (msui::cSide); g.fillRect (xc - 0.5f, top - 2.0f, 1.0f, bottom - top + 3.0f);

        // gain reduction: grows from the right
        const float y = bottom + 5.0f, gr = m->limGrMeter.load(), gh = m->limGrHold.load();
        g.setFont (ui::mono (9.5f)); g.setColour (theme.ink.withAlpha (0.85f));
        g.drawText ("gr", juce::Rectangle<float> (0, y, x0 - 2, 11), juce::Justification::centredLeft);
        g.setColour (theme.ink.withAlpha (0.12f)); g.fillRect (x0, y + 2.0f, bw, 7.0f);
        for (int db = 3; db < (int) kGrMax; db += 3) { g.setColour (theme.ground.withAlpha (0.8f)); g.fillRect (x1 - bw * (float) db / kGrMax, y + 2.0f, 1.0f, 7.0f); }
        const float gw = bw * juce::jlimit (0.0f, 1.0f, gr / kGrMax);
        if (gw > 0.5f) { g.setColour (msui::cSide); g.fillPath (ui::skewRect ({ x1 - gw, y + 2.0f, gw, 7.0f }, -0.5f)); }
        if (gh > 0.05f) { g.setColour (theme.edge); g.fillRect (x1 - bw * juce::jlimit (0.0f, 1.0f, gh / kGrMax) - 0.75f, y, 1.5f, 11.0f); }
        g.setColour (theme.ink);
        g.drawText (gr < 0.05f ? juce::String ("0.0") : "-" + juce::String (gr, 1), juce::Rectangle<float> (x1 + 2, y, 30, 11), juce::Justification::centredRight);
        g.drawText (gh < 0.05f ? juce::String ("0.0") : "-" + juce::String (gh, 1), juce::Rectangle<float> (x1 + 34, y, 32, 11), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! refArea().contains (e.getPosition())) return;
        juce::PopupMenu menu;
        for (int db : { -24, -20, -18, -16, -14, -12, -10, -8, -6 }) menu.addItem (100 + db, "0 vu = " + juce::String (db) + " dbfs", true, db == p.vuRef);
        juce::Component::SafePointer<LimiterMeter> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) { if (safe && r > 0) { safe->p.vuRef = r - 100; safe->repaint(); } });
    }
private:
    Processor& p; ui::Theme theme;
    juce::Rectangle<int> refArea() const { return { getWidth() - 120, 0, 120, 16 }; }
};

class MasterView : public View, private juce::Timer
{
public:
    explicit MasterView (Processor& proc) : p (proc), pi (proc.pinfo), theme (makeTheme (proc.pinfo)), analyser (proc, theme), imager (proc, theme),
        dynRead (proc, theme), limMeter (proc, theme), presetBar (proc, theme), listen (P (proc, "listen"), theme, " "), bypass (P (proc, "bypass"), theme, "compare"),
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
        analyser.onType = [this] (juce::RangedAudioParameter& prm, juce::Rectangle<int> r) { typeValue (prm, analyser, r); };
        dynRead.onType = [this] (juce::RangedAudioParameter& prm, juce::Component& c, juce::Rectangle<int> r) { typeValue (prm, c, r); };
        dynRead.onSelect = [this] (int k) { analyser.focusDyn (k); };
        addAndMakeVisible (dynRead); addAndMakeVisible (limMeter);

        // eq band editor (rebuilt on selection)
        eqStage = std::make_unique<Stage> ("eq  (linear phase)", theme); eqStage->setSwitch (P (p, "eqon")); addAndMakeVisible (*eqStage);
        for (int k = 0; k < 10; ++k)
        {
            auto* t = tabs.add (new ui::TextLink (k < 8 ? juce::String (k + 1) : (k == 8 ? "hp" : "lp"), theme));
            t->onClick = [this, k] { selectBand (k); };
            addAndMakeVisible (t);
        }
        auto addS = [this] (Stage& s, const juce::String& id, const juce::String& label) -> juce::Component*
        {
            auto* c = makeControl (id, label);
            s.add (c);
            return c;
        };
        dyn = std::make_unique<Stage> ("dynamic eq  (linear phase)", theme); dyn->setSwitch (P (p, "dynon"));
        for (int k = 1; k <= 2; ++k)
        {
            const juce::String x = "d" + juce::String (k);
            addS (*dyn, x + "on", "band " + juce::String (k)); addS (*dyn, x + "ch", "channel");
            // touching a band's sliders brings it forward in the display (its edges labelled; drawn even while it is off)
            for (auto [suffix, label] : { std::pair<const char*, const char*> { "freq", "freq" }, { "q", "q" }, { "thr", "threshold" }, { "range", "range" }, { "att", "attack" }, { "rel", "release" } })
                if (auto* sl = dynamic_cast<ui::ParamSlider*> (addS (*dyn, x + suffix, label)))
                {
                    sl->onTouch = [this, k] { analyser.focusDyn (k - 1); };
                    if (juce::String (suffix) == "thr") dynThr[(size_t) (k - 1)] = sl;
                    if (juce::String (suffix) == "range") dynRange[(size_t) (k - 1)] = sl;
                }
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

    ~MasterView() override { if (entry) { entry->onFocusLost = nullptr; entry->onDone = nullptr; } }

    /** the control for a parameter: choice, toggle, or a slider whose value can be typed */
    juce::Component* makeControl (const juce::String& id, const juce::String& label)
    {
        auto& prm = P (p, id);
        if (dynamic_cast<juce::AudioParameterChoice*> (&prm)) return new ui::ChoiceBox (prm, theme, label);
        if (dynamic_cast<juce::AudioParameterBool*> (&prm)) return new ui::ToggleBox (prm, theme, label);
        auto* sl = new TypeSlider (prm, theme, label);
        sl->setLabelWidth (62);
        sl->onType = [this] (juce::RangedAudioParameter& pr, juce::Component& c, juce::Rectangle<int> r) { typeValue (pr, c, r); };
        return sl;
    }
    /** opens the text field over `where` (in `from`) to type a parameter's value */
    void typeValue (juce::RangedAudioParameter& prm, juce::Component& from, juce::Rectangle<int> where)
    {
        if (entry) entry->finish (true);
        entry = std::make_unique<ui::ValueEntry> (prm, theme);
        addAndMakeVisible (*entry);
        entry->setBounds (getLocalArea (&from, where).constrainedWithin (getLocalBounds()));
        entry->onDone = [this, e = entry.get()]
        {
            juce::Component::SafePointer<MasterView> safe (this);
            juce::MessageManager::callAsync ([safe, e] { if (safe && safe->entry.get() == e) safe->entry.reset(); });
        };
        entry->grabKeyboardFocus();
    }
    /** screenshots / tests: 0 plain · 1 the mouse on dynamic band 1 · 2 typing "1.2k" into dynamic band 1's frequency · 3 return */
    void setPage (int pg) override
    {
        if (pg == 3) { if (entry) { entry->finish (true); entry.reset(); } return; }   // as return would
        if (entry) { entry->finish (false); entry.reset(); }
        analyser.hoverDynForTest (pg == 1 ? 0 : -1);
        if (pg == 0) analyser.dynSel = -1;
        if (pg == 2)
            for (auto* c : dyn->items)
                if (auto* sl = dynamic_cast<TypeSlider*> (c))
                    if (sl->param.paramID == "d1freq") { analyser.dynSel = 0; typeValue (sl->param, *sl, sl->getLocalBounds().removeFromRight (78)); entry->setText ("1.2k", false); }
    }

    static ui::Theme makeTheme (const ProductInfo& pi)
    {
        const juce::Colour g (pi.ground);
        return { g, ui::inkFor (g), juce::Colour (pi.accent), juce::Colours::white };
    }

    void selectBand (int k)
    {
        band = k;
        analyser.focusEq (std::min (k, 7));
        for (int i = 0; i < tabs.size(); ++i) tabs[i]->setLit (i == k);
        eqStage->clearItems(); eqStage->topPad = 44;
        auto add = [this] (const juce::String& id, const juce::String& label) { eqStage->add (makeControl (id, label)); };
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
        limMeter.setBounds (col (4), lim->getBottom() + 6, colW, LimiterMeter::kHeight);
        dynRead.setBounds (col (1), dyn->getBottom() + 6, colW, DynReadout::kHeight);
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
    void tick() override
    {
        analyser.tick(); imager.tick(); dynRead.repaint(); limMeter.repaint();
        repaint (0, 405, getWidth(), 25);
        // on the dynamic eq's sliders, as hollow ticks: the level each band hears (on its threshold), its gain now (on its range)
        if (auto* m = p.master())
            for (size_t k = 0; k < 2; ++k)
            {
                const float lv = m->dynLevel[k].load(), gr = m->dynGr[k].load();
                if (dynThr[k]) dynThr[k]->setGhost (lv > -100.0f ? dynThr[k]->param.convertTo0to1 (juce::jlimit (-60.0f, 0.0f, lv)) : -1.0f);
                if (dynRange[k]) dynRange[k]->setGhost (std::abs (gr) > 0.05f ? dynRange[k]->param.convertTo0to1 (juce::jlimit (-18.0f, 12.0f, gr)) : -1.0f);
            }
    }

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
    DynReadout dynRead;
    LimiterMeter limMeter;
    std::unique_ptr<ui::ValueEntry> entry;
    std::array<ui::ParamSlider*, 2> dynThr {}, dynRange {};
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

#pragma once
// The faltung look of ballonwinkel and ballonpaleis: what both windows draw with.
//
// A flat colour field per product, thin wireframe solids with white highlight faces, folded paper strips in the
// suite's eight pastels, parallelograms for everything that is switched or slid, lowercase Inter and IBM Plex
// Mono. The balloons are faceted solids on a thread; how far the colour has risen in one is its value.
//
// Colours are written once, as the light ones. Everything passes through field() / accent() / paperInk(), so a
// build with FALTUNG_DEEP=1 (the "… deep" twins) is the same window on the dark counterparts, as faltung deep is
// of faltung.
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <cmath>
#include <vector>
#include "Ui.h"   // (the suite's: Source/Ui.h)

#ifndef BALLON_PRODUCT
 #define BALLON_PRODUCT 0
#endif
#ifndef FALTUNG_DEEP
 #define FALTUNG_DEEP 0
#endif

// Balloon colours by bunch (the names are the originals': each is one of the suite's strip colours here).
enum BalloonHue { balloonBlue = 0, balloonYellow, balloonPink, balloonMint, balloonLilac, balloonOrange, balloonTeal, numHues };
struct BalloonColours { juce::Colour fill, line, shade, shine; };
const BalloonColours& balloonColours (int hue);

// Where a balloon's body and knot sit inside a square of space, for a given inflation.
struct BalloonShape
{
    float cx, cy, rx, ry, knot, lw;
    juce::Point<float> knotBottom() const noexcept { return { cx, cy + ry + knot }; }
};
BalloonShape balloonShape (juce::Rectangle<float> bounds, float inflate);

namespace bal
{
using juce::Colour;
using juce::Graphics;
using juce::Justification;
using juce::Path;
using juce::Point;
using juce::Rectangle;
using juce::String;
namespace ui = fal::ui;

// ---- the two products ------------------------------------------------------------------------------
struct Product
{
    const char* name;      // "ballonwinkel"
    const char* code;      // "bw1" (as kt1, dl4 … in the rest of the suite)
    const char* blurb;
    const char* original;  // what it is the twin of
    juce::uint32 ground;   // the colour field (light mode)
    juce::uint32 accent;   // the second colour: wireframe lines
};
inline const Product& product()
{
    static const Product p[2] = {
        { "ballonwinkel", "bw1", "a multi-effect played as balloons",            "Pinkie Pie's Balloon Shop",   0xffc9567f, 0xfff3c1d1 },
        { "ballonpaleis", "bp2", "the same shop in a sphere: ambisonics, 1st to 7th order", "Pinkie Pie's Balloon Palace", 0xff3d1f3a, 0xffe58aa6 },
    };
    return p[BALLON_PRODUCT == 1 ? 1 : 0];
}

// ---- light / dark ------------------------------------------------------------------------------------
// The suite's one rule (Source/Ui.h): a FALTUNG_DEEP=1 build draws every colour field on its dark counterpart.
constexpr bool kDark = ui::kDark;
inline Colour darkField (Colour c) { return ui::darkField (c); }
inline Colour darkAccent (Colour c) { return ui::darkAccent (c); }
inline Colour field (Colour c)  { return ui::field (c); }
inline Colour accent (Colour c) { return ui::accent (c); }
inline Colour paperInk()        { return ui::paperInk(); }

// ---- the colours of a window -------------------------------------------------------------------------
inline Colour ground()    { return field (Colour (product().ground)); }
inline Colour ink()       { return ui::inkFor (ground()); }
inline Colour line()      { return accent (Colour (product().accent)); }
/** highlight faces and marks: white on these fields (ink on a pale one) */
inline Colour white()     { return ui::edgeFor (ground()); }
/** the paper of the pages that come forward (help, the store, the sliders, the board): the suite's off-white */
inline Colour paper()     { return field (Colour (0xfff4f2ec)); }
inline Colour paperLine() { return paperInk().withAlpha (0.35f); }
/** the eight pastels of the folded strips (the suite's Fold palette) */
inline Colour strip (int i)
{
    static const juce::uint32 c[8] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0 };
    return accent (Colour (c[((i % 8) + 8) % 8]));
}
/** a bunch's colour */
inline Colour hue (int balloonHue)
{
    static const int s[numHues] = { 2, 5, 7, 1, 3, 4, 0 };   // smear sky, pitch peach, loop pale, grains green, reverb periwinkle, stutter coral, caught deep green
    return strip (s[juce::jlimit (0, (int) numHues - 1, balloonHue)]);
}
inline ui::Theme theme()      { return { ground(), ink(), line(), white(), false }; }
inline ui::Theme paperTheme() { return { paper(), paperInk(), strip (4), ui::edgeFor (paper()), false }; }
/** the seven colours of a rainbow, as lines (Oval's coloured lines) */
inline Colour rainbow (int band)
{
    static const juce::uint32 c[7] = { 0xffe8554e, 0xffee8e6e, 0xfff0c94a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xff9a78c8 };
    return accent (Colour (c[juce::jlimit (0, 6, band)]));
}

// ---- small things ------------------------------------------------------------------------------------
inline juce::Font sans (float h, bool medium = false) { return ui::sans (h, medium); }
inline juce::Font mono (float h, bool medium = false) { return ui::mono (h, medium); }

inline Path poly (std::initializer_list<Point<float>> pts)
{
    Path p; bool first = true;
    for (auto& q : pts) { if (first) p.startNewSubPath (q); else p.lineTo (q); first = false; }
    p.closeSubPath();
    return p;
}
inline Path polyline (const std::vector<Point<float>>& pts)
{
    Path p;
    for (size_t i = 0; i < pts.size(); ++i) { if (i == 0) p.startNewSubPath (pts[i]); else p.lineTo (pts[i]); }
    return p;
}
/** a white highlight face: a thin white fill and a firm white edge */
inline void face (Graphics& g, const Path& p, float fillAlpha = 0.3f, float stroke = 1.6f, Colour c = white())
{
    if (fillAlpha > 0.004f) { g.setColour (c.withMultipliedAlpha (fillAlpha)); g.fillPath (p); }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (stroke));
}
inline void label (Graphics& g, const String& s, Rectangle<float> r, Colour c, float size = 10.0f, Justification j = Justification::centred, bool medium = false)
{
    g.setColour (c); g.setFont (mono (size, medium)); g.drawText (s, r, j, false);
}
inline void title (Graphics& g, const String& s, Rectangle<float> r, Colour c, float size = 15.0f, Justification j = Justification::centredLeft, bool medium = false)
{
    g.setColour (c); g.setFont (sans (size, medium)); g.drawText (s, r, j, false);
}
/** a switch or a button: the house parallelogram, filled when on, with its name beside or inside it */
inline void chip (Graphics& g, Rectangle<float> r, const String& text, bool on, bool hover, Colour fill, Colour inkC, float size = 10.0f)
{
    const auto p = ui::skewRect (r.reduced (r.getHeight() * 0.3f, 1.0f), 0.35f);
    if (on) { g.setColour (fill); g.fillPath (p); }
    g.setColour (inkC.withAlpha (hover ? 1.0f : (on ? 0.9f : 0.55f)));
    g.strokePath (p, juce::PathStrokeType (hover ? 1.4f : 1.0f));
    label (g, text, r, on ? ui::inkFor (fill) : inkC, size);
}
/** a small toggle mark with its name to the right, as the suite's ToggleBox draws it */
inline void toggle (Graphics& g, Rectangle<float> r, const String& text, bool on, bool hover, Colour fill, Colour inkC, float size = 10.0f)
{
    const auto m = Rectangle<float> (r.getX(), r.getCentreY() - 5.0f, 18.0f, 10.0f);
    const auto p = ui::skewRect (m);
    if (on) { g.setColour (fill); g.fillPath (p); }
    g.setColour (inkC.withAlpha (hover ? 1.0f : 0.8f));
    g.strokePath (p, juce::PathStrokeType (hover ? 1.4f : 1.0f));
    label (g, text, r.withTrimmedLeft (28.0f), inkC, size, Justification::centredLeft);
}
/** a value on a rule: thin line, slanted fill, edge mark (the suite's slider, drawn anywhere) */
inline void bar (Graphics& g, Rectangle<float> track, float v01, Colour fill, Colour inkC, Colour edge, float zero01 = 0.0f)
{
    const float cy = track.getCentreY();
    g.setColour (inkC.withAlpha (0.35f));
    g.fillRect (track.getX(), cy, track.getWidth(), 1.0f);
    const float a = track.getX() + track.getWidth() * juce::jmin (zero01, v01), e = track.getX() + track.getWidth() * juce::jmax (zero01, v01);
    if (e - a > 0.5f) { g.setColour (fill); g.fillPath (ui::skewRect ({ a, cy - 4.5f, e - a, 9.0f })); }
    g.setColour (edge);
    g.fillRect (track.getX() + track.getWidth() * v01 - 1.0f, cy - 5.5f, 2.0f, 11.0f);
}
/** one run of a folded strip (the snd sleeves): a face, the darker sliver where the paper turns, a name on it */
inline void foldStrip (Graphics& g, Rectangle<float> r, Colour c, const String& text, bool outline = false, float glow = 0.0f, float textSize = 11.0f)
{
    const float d = r.getHeight() * 0.45f;
    const Colour fc = c.interpolatedWith (juce::Colours::white, juce::jlimit (0.0f, 0.6f, glow * 0.6f));
    const Path f = poly ({ { r.getX() + d, r.getY() }, { r.getRight() + d, r.getY() }, { r.getRight(), r.getBottom() }, { r.getX(), r.getBottom() } });
    g.setColour (fc); g.fillPath (f);
    g.setColour (fc.darker (0.28f));
    g.fillPath (poly ({ { r.getRight() + d, r.getY() }, { r.getRight() + d, r.getY() + r.getHeight() * 0.42f }, { r.getRight(), r.getBottom() } }));
    if (outline) { g.setColour (white()); g.strokePath (f, juce::PathStrokeType (1.6f)); }
    if (text.isNotEmpty())
    {
        g.setColour (ui::inkFor (fc)); g.setFont (sans (textSize));
        g.drawText (text, r.translated (d * 0.5f, 0.0f), Justification::centred, false);
    }
}

// ---- solids --------------------------------------------------------------------------------------------
struct V3 { float x, y, z; };
struct Solid
{
    std::vector<V3> v;
    std::vector<std::array<int, 2>> e;
    std::vector<std::vector<int>> f;   // faces that may be lit
};
inline const Solid& solid (int which)
{
    static const std::array<Solid, 6> s = []
    {
        std::array<Solid, 6> a;
        // 0 tetrahedron
        a[0].v = { { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } };
        a[0].e = { { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 2 }, { 1, 3 }, { 2, 3 } };
        a[0].f = { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 2, 3 }, { 1, 2, 3 } };
        // 1 cube
        for (int i = 0; i < 8; ++i) a[1].v.push_back ({ (i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f });
        a[1].e = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        a[1].f = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
        // 2 octahedron
        a[2].v = { { 1.3f, 0, 0 }, { -1.3f, 0, 0 }, { 0, 1.3f, 0 }, { 0, -1.3f, 0 }, { 0, 0, 1.3f }, { 0, 0, -1.3f } };
        a[2].e = { { 0, 2 }, { 0, 3 }, { 0, 4 }, { 0, 5 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 1, 5 }, { 2, 4 }, { 2, 5 }, { 3, 4 }, { 3, 5 } };
        a[2].f = { { 0, 2, 4 }, { 0, 3, 4 }, { 1, 2, 4 }, { 1, 3, 4 }, { 0, 2, 5 }, { 0, 3, 5 }, { 1, 2, 5 }, { 1, 3, 5 } };
        // 3 icosahedron
        {
            const float t = 1.618034f, k = 0.72f;
            for (int i = 0; i < 4; ++i) { const float p = (i & 1) ? 1.0f : -1.0f, q = (i & 2) ? t : -t;
                a[3].v.push_back ({ 0, p * k, q * k }); a[3].v.push_back ({ p * k, q * k, 0 }); a[3].v.push_back ({ q * k, 0, p * k }); }
            for (int i = 0; i < 12; ++i)
                for (int j = i + 1; j < 12; ++j)
                {
                    const auto& p = a[3].v[(size_t) i]; const auto& q = a[3].v[(size_t) j];
                    const float d = std::sqrt ((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
                    if (d < 2.0f * k + 0.05f) a[3].e.push_back ({ i, j });
                }
            for (int i = 0; i < 12; ++i)
                for (int j = i + 1; j < 12; ++j)
                    for (int m = j + 1; m < 12; ++m)
                    {
                        auto near = [&] (int x, int y) { const auto& p = a[3].v[(size_t) x]; const auto& q = a[3].v[(size_t) y];
                            return std::sqrt ((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)) < 2.0f * k + 0.05f; };
                        if (near (i, j) && near (j, m) && near (i, m)) a[3].f.push_back ({ i, j, m });
                    }
        }
        // 4 a prism on five sides
        for (int i = 0; i < 5; ++i)
        {
            const float an = 6.2831853f * (float) i / 5.0f;
            a[4].v.push_back ({ std::cos (an) * 1.1f, -0.9f, std::sin (an) * 1.1f });
            a[4].v.push_back ({ std::cos (an) * 1.1f, 0.9f, std::sin (an) * 1.1f });
        }
        for (int i = 0; i < 5; ++i)
        {
            const int n = (i + 1) % 5;
            a[4].e.push_back ({ 2 * i, 2 * i + 1 }); a[4].e.push_back ({ 2 * i, 2 * n }); a[4].e.push_back ({ 2 * i + 1, 2 * n + 1 });
            a[4].f.push_back ({ 2 * i, 2 * n, 2 * n + 1, 2 * i + 1 });
        }
        // 5 a leaning slab (the suite's lit panels)
        a[5].v = { { -1.4f, -0.5f, -0.7f }, { 1.4f, -0.8f, -0.5f }, { 1.2f, 0.6f, -0.9f }, { -1.2f, 0.8f, -0.6f },
                   { -1.1f, -0.6f, 0.8f }, { 1.5f, -0.4f, 0.6f }, { 1.0f, 0.7f, 0.9f }, { -1.5f, 0.5f, 0.5f } };
        a[5].e = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, { 0, 6 }, { 1, 7 } };
        a[5].f = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 } };
        return a;
    }();
    return s[(size_t) juce::jlimit (0, 5, which)];
}
/** a solid seen from a little above, turned by ay (round the upright) and ax; sx, sy: its size on the page */
struct View
{
    Point<float> c; float sx, sy, ay, ax;
    Point<float> at (V3 p) const noexcept
    {
        const float ca = std::cos (ay), sa = std::sin (ay);
        const float x = p.x * ca + p.z * sa, z = -p.x * sa + p.z * ca;
        const float cb = std::cos (ax), sb = std::sin (ax);
        const float y = p.y * cb - z * sb;
        return { c.x + x * sx, c.y + y * sy };
    }
    float depth (V3 p) const noexcept
    {
        const float z = -p.x * std::sin (ay) + p.z * std::cos (ay);
        return p.y * std::sin (ax) + z * std::cos (ax);
    }
};
inline Path facePath (const Solid& s, const View& v, int f)
{
    Path p;
    const auto& idx = s.f[(size_t) (((f % (int) s.f.size()) + (int) s.f.size()) % (int) s.f.size())];
    for (size_t i = 0; i < idx.size(); ++i) { const auto q = v.at (s.v[(size_t) idx[i]]); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
    p.closeSubPath();
    return p;
}
inline void wireSolid (Graphics& g, const Solid& s, const View& v, Colour c, float thickness = 1.0f)
{
    g.setColour (c);
    for (auto& e : s.e) g.drawLine ({ v.at (s.v[(size_t) e[0]]), v.at (s.v[(size_t) e[1]]) }, thickness);
}
inline void drawSolid (Graphics& g, int which, Point<float> centre, float size, float ay, float ax, Colour c, float thickness = 1.0f,
                       int litFace = -1, float litAlpha = 0.3f, Colour lit = white())
{
    const auto& s = solid (which);
    const View v { centre, size, size, ay, ax };
    wireSolid (g, s, v, c, thickness);
    if (litFace >= 0) face (g, facePath (s, v, litFace), litAlpha, juce::jmax (1.2f, thickness + 0.6f), lit);
}

/** the big figure behind everything: one per preset (the number of its picture), breathing with the sound.
    Fell's wireframe: a ring of points lifted into a leaning prism, a few diagonals, two or three lit panels. */
inline void backdrop (Graphics& g, Rectangle<float> area, int which, float level, float t, float alpha, Colour lineC, Colour litC)
{
    juce::Random r ((juce::int64) (which * 7919 + 17));
    const int n = 6 + r.nextInt (4);
    const auto c = area.getCentre().translated ((r.nextFloat() - 0.5f) * area.getWidth() * 0.12f, area.getHeight() * 0.06f);
    const float R = juce::jmin (area.getWidth() * 0.36f, area.getHeight() * 0.62f) * (1.0f + 0.03f * level);
    const float height = area.getHeight() * (0.22f + 0.3f * r.nextFloat()), lean = (r.nextFloat() - 0.5f) * 150.0f;
    std::vector<Point<float>> base ((size_t) n), top ((size_t) n);
    float a0 = r.nextFloat() * 6.283f;
    for (int i = 0; i < n; ++i)
    {
        const float a = a0 + 6.2831853f * ((float) i + 0.5f * (r.nextFloat() - 0.5f)) / (float) n + 0.03f * std::sin (t * 0.2f + (float) i);
        const float rr = R * (0.55f + 0.6f * r.nextFloat());
        base[(size_t) i] = c + Point<float> (std::cos (a) * rr * 1.35f, std::sin (a) * rr * 0.4f + height * 0.35f);
        top[(size_t) i] = base[(size_t) i] + Point<float> (lean * (0.4f + 0.12f * (float) i), -height * (0.6f + 0.12f * (float) (i % 3)) * (1.0f + 0.05f * level));
    }
    g.setColour (lineC.withMultipliedAlpha (alpha));
    for (int i = 0; i < n; ++i)
    {
        const int j = (i + 1) % n;
        g.drawLine ({ base[(size_t) i], base[(size_t) j] }, 1.0f);
        g.drawLine ({ top[(size_t) i], top[(size_t) j] }, 1.0f);
        if (i % 2 == 0 || i == n - 1) g.drawLine ({ base[(size_t) i], top[(size_t) i] }, 1.0f);
        if (i % 3 == 1) g.drawLine ({ base[(size_t) i], top[(size_t) ((i + 2) % n)] }, 1.0f);
    }
    g.drawLine ({ top[0], base[(size_t) (n / 2)] }, 1.0f);
    // lit panels
    const int panels = 2 + r.nextInt (2);
    for (int k = 0; k < panels; ++k)
    {
        const int i = r.nextInt (n), j = (i + 1) % n;
        const float u0 = 0.15f + 0.3f * r.nextFloat(), u1 = u0 + 0.2f + 0.25f * r.nextFloat(), h0 = 0.15f + 0.3f * r.nextFloat(), h1 = h0 + 0.2f + 0.3f * r.nextFloat();
        auto at = [&] (float u, float h)
        {
            const auto b = base[(size_t) i] + (base[(size_t) j] - base[(size_t) i]) * u, tp = top[(size_t) i] + (top[(size_t) j] - top[(size_t) i]) * u;
            return b + (tp - b) * h;
        };
        face (g, poly ({ at (u0, h0), at (u1, h0), at (u1, h1), at (u0, h1) }), (0.10f + 0.25f * level) * alpha, 1.6f, litC.withMultipliedAlpha (0.55f * alpha));
    }
}

// ---- balloons ------------------------------------------------------------------------------------------
/** a balloon's outline: a faceted solid, wide at the shoulders, drawn in to the knot */
inline std::array<Point<float>, 10> balloonRing (const BalloonShape& sh)
{
    static const float u[10][2] = { { 0.0f, -1.0f }, { 0.60f, -0.84f }, { 1.0f, -0.28f }, { 0.84f, 0.38f }, { 0.34f, 0.90f },
                                    { 0.0f, 1.0f }, { -0.34f, 0.90f }, { -0.84f, 0.38f }, { -1.0f, -0.28f }, { -0.60f, -0.84f } };
    std::array<Point<float>, 10> p;
    for (int i = 0; i < 10; ++i) p[(size_t) i] = { sh.cx + u[i][0] * sh.rx, sh.cy + u[i][1] * sh.ry };
    return p;
}
inline Path balloonOutline (const BalloonShape& sh)
{
    const auto r = balloonRing (sh);
    Path p;
    p.startNewSubPath (r[0]);
    for (size_t i = 1; i < r.size(); ++i) p.lineTo (r[i]);
    p.closeSubPath();
    return p;
}
/** A control as a balloon. level: how far the colour has risen (0..1), or < 0 for a whole one (a caught sound).
    wrinkles: empty (a switch that is off: only its edges).  wet: rained on.  murk: muffled, or stepping aside. */
inline void balloon (Graphics& g, const BalloonShape& sh, int balloonHue, const String& text, const String& value, bool hot,
                     float wrinkles, float wet, float murk, float labelSize, float level)
{
    const Colour base = hue (balloonHue).interpolatedWith (ground(), juce::jlimit (0.0f, 0.75f, murk * 0.75f));
    const auto ring = balloonRing (sh);
    const Path body = balloonOutline (sh);
    const bool hollow = wrinkles > 0.5f;
    const float cx = sh.cx, cy = sh.cy, rx = sh.rx, ry = sh.ry;

    // the knot: a small triangle under the body
    g.setColour (base.darker (0.25f));
    g.fillPath (poly ({ { cx, cy + ry * 0.96f }, { cx + rx * 0.16f, cy + ry + sh.knot * 0.8f }, { cx - rx * 0.16f, cy + ry + sh.knot * 0.8f } }));

    if (hollow)
    {
        g.setColour (ground().withAlpha (0.55f));
        g.fillPath (body);
    }
    else
    {
        // the skin (a paler print of the colour), and the colour itself risen to the value, its edge slanted
        g.setColour (level < 0.0f ? base : base.interpolatedWith (ground(), 0.5f).withAlpha (0.92f));
        g.fillPath (body);
        if (level >= 0.0f)
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (body);
            const float top = cy + ry - 2.0f * ry * juce::jlimit (0.0f, 1.0f, level), slant = rx * 0.22f;
            g.setColour (base);
            g.fillPath (poly ({ { cx - rx - 2.0f, top + slant }, { cx + rx + 2.0f, top - slant }, { cx + rx + 2.0f, cy + ry + 2.0f }, { cx - rx - 2.0f, cy + ry + 2.0f } }));
        }
    }

    // the facets: an inner ring pulled toward the light, joined to the outline
    const Point<float> ic (cx - rx * 0.14f, cy - ry * 0.16f);
    std::array<Point<float>, 5> in;
    static const int outer[5] = { 0, 2, 4, 6, 8 };
    for (int i = 0; i < 5; ++i) in[(size_t) i] = ic + (ring[(size_t) outer[i]] - ic) * 0.52f;
    const Colour facet = (hollow ? base : ui::inkFor (base)).withAlpha (hollow ? 0.8f : 0.22f);
    g.setColour (facet);
    for (int i = 0; i < 5; ++i)
    {
        g.drawLine ({ in[(size_t) i], in[(size_t) ((i + 1) % 5)] }, 0.8f);
        g.drawLine ({ in[(size_t) i], ring[(size_t) outer[i]] }, 0.8f);
        g.drawLine ({ in[(size_t) i], ring[(size_t) ((outer[i] + 1) % 10)] }, 0.8f);
    }
    // the lit face, upper left
    face (g, poly ({ ring[9], ring[0], in[0], in[4], ring[8] }), hollow ? 0.0f : 0.5f, hollow ? 1.0f : 1.2f, juce::Colours::white.withAlpha (hollow ? 0.5f : 0.9f));

    // rained on: short slanted strokes
    if (wet > 0.01f)
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        static const float d[7][2] = { { -0.4f, -0.2f }, { 0.3f, -0.45f }, { 0.45f, 0.1f }, { -0.1f, 0.4f }, { -0.5f, 0.25f }, { 0.1f, -0.05f }, { 0.55f, -0.2f } };
        g.setColour (juce::Colours::white.withAlpha (0.8f * juce::jmin (1.0f, wet)));
        const int n = juce::jlimit (0, 7, (int) std::ceil (wet * 7.0f));
        for (int i = 0; i < n; ++i)
            g.drawLine (cx + d[i][0] * rx, cy + d[i][1] * ry, cx + d[i][0] * rx - 2.0f, cy + d[i][1] * ry + 6.0f, 1.0f);
    }

    g.setColour (hollow ? base : base.darker (0.35f));
    g.strokePath (body, juce::PathStrokeType (hollow ? 1.2f : 1.0f));
    if (hot) { g.setColour (juce::Colours::white); g.strokePath (body, juce::PathStrokeType (1.8f)); }

    // its name on it
    {
        const Colour inkC = hollow ? ink() : ui::inkFor (base);
        juce::StringArray lines;
        lines.addTokens (text, "\n", {});
        const float lh = labelSize + 0.5f;
        const float total = (float) lines.size() * lh + (value.isNotEmpty() ? 11.0f : 0.0f);
        float y = cy + ry * 0.08f - total * 0.5f;
        g.setFont (sans (labelSize, true));
        g.setColour (inkC);
        for (auto& l : lines)
        {
            g.drawText (l, Rectangle<float> (cx - rx - 12.0f, y, 2.0f * rx + 24.0f, lh), Justification::centred, false);
            y += lh;
        }
        if (value.isNotEmpty())
        {
            g.setFont (mono (9.0f));
            g.drawText (value, Rectangle<float> (cx - rx - 16.0f, y + 1.0f, 2.0f * rx + 32.0f, 10.0f), Justification::centred, false);
        }
    }
}
/** ice over a balloon as the snow freezes everything (0..1): white edges thickening, crystals from the rim */
inline void frost (Graphics& g, const BalloonShape& sh, float amount, float seed)
{
    const Path body = balloonOutline (sh);
    g.setColour (juce::Colours::white.withAlpha (0.35f * amount));
    g.fillPath (body);
    g.setColour (juce::Colours::white.withAlpha (0.85f * amount));
    g.strokePath (body, juce::PathStrokeType (0.8f + 1.6f * amount));
    juce::Random r ((juce::int64) (seed * 1000.0f));
    const int crystals = 2 + (int) (5.0f * amount);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (body);
    for (int i = 0; i < crystals; ++i)
    {
        const float ang = r.nextFloat() * juce::MathConstants<float>::twoPi;
        const Point<float> c (sh.cx + std::cos (ang) * sh.rx * 0.95f, sh.cy + std::sin (ang) * sh.ry * 0.95f);
        const float len = (5.0f + 9.0f * r.nextFloat()) * amount;
        for (int arm = 0; arm < 3; ++arm)
        {
            const float a = (float) arm * juce::MathConstants<float>::pi / 3.0f + r.nextFloat() * 0.3f;
            g.drawLine (c.x - std::cos (a) * len, c.y - std::sin (a) * len, c.x + std::cos (a) * len, c.y + std::sin (a) * len, 0.9f);
        }
    }
}
/** a balloon as a mark (in lists and on the store's sign): r is about half its width */
inline void tinyBalloon (Graphics& g, Point<float> anchor, float r, Colour c, bool lit = true)
{
    BalloonShape sh { anchor.x, anchor.y - r * 2.2f, r * 0.9f, r * 1.06f, r * 0.3f, 1.0f };
    g.setColour (c.darker (0.2f));
    g.drawLine (sh.cx, sh.cy + sh.ry, anchor.x, anchor.y, 0.8f);
    const Path body = balloonOutline (sh);
    g.setColour (c); g.fillPath (body);
    if (lit)
    {
        const auto ring = balloonRing (sh);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (poly ({ ring[9], ring[0], { sh.cx - sh.rx * 0.1f, sh.cy - sh.ry * 0.3f }, ring[8] }), juce::PathStrokeType (0.8f));
    }
}

// ---- things on the floor ---------------------------------------------------------------------------------
/** what a bunch is tied to: a short folded strip in its colour, its code on it */
inline void knotStrip (Graphics& g, Point<float> at, Colour c, const String& code, bool lit = false)
{
    foldStrip (g, Rectangle<float> (54.0f, 15.0f).withCentre (at.translated (-3.0f, 2.0f)), c, code, lit, 0.0f, 10.0f);
}
/** what a store effect's four balloons are tied to: a small wire cube, its lid lit */
inline void box (Graphics& g, Point<float> knot, Colour c, float t = 0.0f)
{
    const Point<float> centre (knot.x, knot.y + 12.0f);
    drawSolid (g, 1, centre, 9.0f, 0.6f + 0.05f * std::sin (t), 0.45f, c, 1.2f, 2, 0.45f);
}
/** a cloud: stacked arcs of thin line (Oval's coloured lines), darker when it carries rain */
inline void cloud (Graphics& g, Point<float> c, float s, float dark, Colour lineC, Colour fillC)
{
    static const float b[5][3] = { { -34.0f, 6.0f, 20.0f }, { -12.0f, -6.0f, 26.0f }, { 14.0f, -2.0f, 22.0f }, { 36.0f, 8.0f, 16.0f }, { 2.0f, 10.0f, 24.0f } };
    Path all;
    for (auto& k : b) all.addEllipse (c.x + (k[0] - k[2]) * s, c.y + (k[1] - k[2]) * s, 2.0f * k[2] * s, 2.0f * k[2] * s);
    g.setColour (fillC.withMultipliedAlpha (0.35f + 0.35f * dark));
    g.fillPath (all);
    for (int ring = 0; ring < 3; ++ring)
    {
        g.setColour (lineC.withMultipliedAlpha (ring == 0 ? 1.0f : 0.55f));
        for (auto& k : b)
        {
            const float rr = (k[2] - 5.0f * (float) ring) * s;
            if (rr <= 2.0f) continue;
            Path arc;
            arc.addCentredArc (c.x + k[0] * s, c.y + k[1] * s, rr, rr, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (1.0f));
        }
    }
    g.setColour (lineC);
    g.drawLine (c.x - 54.0f * s, c.y + 14.0f * s, c.x + 52.0f * s, c.y + 14.0f * s, 1.0f);
}
} // namespace bal

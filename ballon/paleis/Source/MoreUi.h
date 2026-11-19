#pragma once
#include "PluginEditor.h"
#include <functional>
#include <vector>

// Shared by ballonwinkel and ballonpaleis (keep the two copies identical):
//   SkyArt       the pictures and the switches of the newer weather (fog, wind, hail, aurora, twister, rainbow)
//   GumballPage  the gumball menu: every balloon's control again as a gumball that stays put

//==============================================================================
// The weather row: ten switches in one row along the top, all the same size, each a little picture
// with its name under it, lit up while it's on (cloudy, rain, snow, heatwave, fog, wind, hail, aurora,
// twister, auto). And the newer weather's pictures, drawn over the room (the rainbow and the aurora
// behind the balloons, the rest in front of them).
struct SkyArt
{
    enum { fog = 0, wind, hail, aurora, twister, kButtons };
    struct In
    {
        float fog = 0, windGust = 0, windLean = 0, hailAmt = 0, rainbow = 0, aurora = 0, twister = 0, twisterAngle = 0;
        float curtains[4] {};
        int hailHits = 0;
    };

    // the row's ten places, left to right
    enum { slotCloudy = 0, slotRain, slotSnow, slotHeat, slotFog, slotWind, slotHail, slotAurora, slotTwister, slotAuto, kSlots };
    static juce::Rectangle<float> slot (int i) noexcept { return { 296.0f + 52.0f * (float) i, 2.0f, 52.0f, 58.0f }; }
    static juce::Rectangle<float> button (int i) noexcept { return slot (slotFog + i); }   // fog, wind, hail, aurora, twister
    static const char* name (int i) noexcept
    {
        static const char* const n[kButtons] = { "fog", "wind", "hail", "aurora", "twister" };
        return n[juce::jlimit (0, kButtons - 1, i)];
    }
    static int buttonAt (juce::Point<float> p) noexcept
    {
        for (int i = 0; i < kButtons; ++i) if (button (i).contains (p)) return i;
        return -1;
    }

    void step (float dt, const In& now, float W, float H, juce::Random& rnd)
    {
        in = now; w = W; h = H; t += dt;
        // hailstones: one for every bounce heard (a few at most each frame), falling and bouncing on the floor
        int fresh = hitsSeen < 0 ? 0 : juce::jlimit (0, 5, in.hailHits - hitsSeen);
        hitsSeen = in.hailHits;
        while (fresh-- > 0 && stones.size() < 90)
            stones.push_back ({ { rnd.nextFloat() * W, -10.0f - rnd.nextFloat() * 60.0f }, { (rnd.nextFloat() - 0.5f) * 120.0f + 260.0f * in.windLean, 620.0f + rnd.nextFloat() * 300.0f },
                                2.5f + rnd.nextFloat() * 3.0f, 0 });
        for (auto& s : stones)
        {
            s.v.y += 1700.0f * dt;
            s.p += s.v * dt;
            if (s.p.y > H - 14.0f && s.v.y > 0.0f) { s.p.y = H - 14.0f; s.v.y *= -0.45f; s.v.x *= 0.7f; ++s.bounces; }
        }
        stones.erase (std::remove_if (stones.begin(), stones.end(), [W] (const Stone& s) { return s.bounces > 3 || s.p.x < -40.0f || s.p.x > W + 40.0f; }), stones.end());

        // the wind: streaks of air going the way it blows
        const float dir = in.windLean < -0.05f ? -1.0f : 1.0f;
        streakBudget += in.windGust * 26.0f * dt;
        while (streakBudget >= 1.0f && streaks.size() < 80)
        {
            streakBudget -= 1.0f;
            streaks.push_back ({ { dir > 0.0f ? -60.0f : W + 60.0f, 60.0f + rnd.nextFloat() * (H - 140.0f) }, { dir * (520.0f + rnd.nextFloat() * 520.0f), (rnd.nextFloat() - 0.5f) * 40.0f },
                                 40.0f + rnd.nextFloat() * 90.0f, rnd.nextFloat() * 6.28f });
        }
        if (streakBudget > 2.0f) streakBudget = 2.0f;
        for (auto& s : streaks) { s.p += s.v * dt; s.p.y += std::sin (t * 5.0f + s.phase) * 22.0f * dt; }
        streaks.erase (std::remove_if (streaks.begin(), streaks.end(), [W] (const Streak& s) { return s.p.x < -200.0f || s.p.x > W + 200.0f; }), streaks.end());
    }

    // how the wind and a twister push something floating at p (pixels per second squared)
    juce::Point<float> pushAt (juce::Point<float> p) const noexcept
    {
        juce::Point<float> a (700.0f * in.windLean * (0.4f + in.windGust), 0.0f);
        if (in.twister > 0.0f)
        {
            const auto c = funnelFoot();
            const float swirl = std::cos (in.twisterAngle * 1.5f + p.y * 0.02f);
            a += juce::Point<float> ((c.x - p.x) * 5.0f + swirl * 900.0f, -260.0f) * in.twister;
        }
        return a;
    }

    bool anything() const noexcept
    {
        return in.fog > 0.0f || in.windGust > 0.0f || in.rainbow > 0.0f || in.aurora > 0.0f || in.twister > 0.0f || ! stones.empty() || ! streaks.empty();
    }

    // behind the balloons: the aurora's curtains across the top of the sky, the rainbow standing on the ground
    void paintBehind (juce::Graphics& g) const
    {
        // (the faltung twins: the aurora as hanging lines in three of the strip colours, the rainbow as seven
        //  thin arcs)
        using namespace juce;
        if (in.aurora > 0.002f)
        {
            for (int j = 0; j < 4; ++j)
            {
                const float bright = in.aurora * (0.2f + 0.8f * in.curtains[j]);
                const float x0 = w * (0.08f + 0.23f * (float) j) + 30.0f * std::sin (t * 0.21f + (float) j * 1.7f);
                const float tall = 150.0f + 150.0f * in.curtains[j];
                constexpr int strips = 9;
                const float sw = 190.0f / (float) strips;
                g.setColour (bal::strip (j + 1).withAlpha (jmin (1.0f, 0.9f * bright)));
                for (int s = 0; s < strips; ++s)
                {
                    const float x = x0 + (float) s * sw + 12.0f * std::sin (t * 0.6f + (float) s * 0.8f + (float) j);
                    const float top = 8.0f + 14.0f * std::sin (t * 0.4f + (float) s * 0.9f + (float) j * 2.0f);
                    const float edge = std::sin (MathConstants<float>::pi * ((float) s + 0.5f) / (float) strips);
                    g.drawLine (x, top, x + 6.0f * std::sin (t * 0.3f + (float) s), top + tall * edge, 1.0f);
                }
            }
        }
        if (in.rainbow > 0.002f)
        {
            const Point<float> c (w * 0.5f, h * 0.96f);
            for (int k = 0; k < 7; ++k)
            {
                const float r = h * 0.72f - 8.0f * (float) k;   // red outermost
                Path arc;
                arc.addCentredArc (c.x, c.y, r, r, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
                g.setColour (bal::rainbow (k).withAlpha (0.9f * in.rainbow));
                g.strokePath (arc, PathStrokeType (1.6f));
            }
        }
    }

    // in front of the balloons: fog banks, the wind's streaks, hailstones, the twister
    void paintOver (juce::Graphics& g) const
    {
        using namespace juce;
        if (in.fog > 0.002f)
        {
            g.setColour (Colour (0xffe9eef5).withAlpha (0.16f * in.fog));
            g.fillRect (Rectangle<float> (0.0f, 0.0f, w, h));
            for (int k = 0; k < 5; ++k)
            {
                const float span = w + 700.0f;
                float x = std::fmod ((float) k * 310.0f + t * (9.0f + 4.0f * (float) k), span);
                x -= 350.0f;
                const float y = h * (0.28f + 0.15f * (float) k) + 18.0f * std::sin (t * 0.17f + (float) k);
                const float rx = 330.0f + 40.0f * (float) (k % 3), ry = 70.0f + 14.0f * (float) (k % 2);
                // (a round gradient squashed flat, so the bank fades out at its top and bottom too)
                Graphics::ScopedSaveState ss (g);
                g.addTransform (AffineTransform::scale (1.0f, ry / rx, x, y));
                g.setGradientFill (ColourGradient (Colour (0xfff4f7fb).withAlpha (0.36f * in.fog), x, y, Colour (0xfff4f7fb).withAlpha (0.0f), x + rx, y, true));
                g.fillEllipse (Rectangle<float> (rx * 2.0f, rx * 2.0f).withCentre ({ x, y }));
            }
        }
        for (auto& s : streaks)
        {
            const float sgn = s.v.x > 0.0f ? 1.0f : -1.0f;
            Path p;
            p.startNewSubPath (s.p);
            p.quadraticTo (s.p.x - sgn * s.len * 0.5f, s.p.y - 5.0f, s.p.x - sgn * s.len, s.p.y + 2.0f);
            g.setColour (Colours::white.withAlpha (0.22f + 0.2f * in.windGust));
            g.strokePath (p, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        for (auto& s : stones)
        {
            g.setColour (Colours::white.withAlpha (0.9f));
            g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre (s.p));
            g.setColour (Colour (0xffbcd6ff).withAlpha (0.8f));
            g.drawEllipse (Rectangle<float> (s.size, s.size).withCentre (s.p), 0.8f);
        }
        if (in.twister > 0.002f)
        {
            const auto foot = funnelFoot();
            constexpr int rings = 16;
            for (int i = 0; i < rings; ++i)
            {
                const float u = (float) i / (float) (rings - 1);
                const float y = foot.y - u * (h - 150.0f);
                const float rw = 14.0f + 120.0f * u * u + 20.0f * u;
                const float x = foot.x + (10.0f + 30.0f * u) * std::sin (in.twisterAngle * 0.9f + u * 5.0f);
                const float a = in.twister * (0.85f - 0.35f * u);
                g.setColour (Colour (0xffd8dde8).withAlpha (a * 0.35f));
                g.fillEllipse (Rectangle<float> (rw * 2.0f, 10.0f + 12.0f * u).withCentre ({ x, y }));
                g.setColour (Colours::white.withAlpha (a));
                Path arc;
                const float turn = in.twisterAngle * (1.6f - u) + (float) i;
                arc.addCentredArc (x, y, rw, 5.0f + 6.0f * u, 0.0f, turn, turn + 2.4f, true);
                g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
    }

    // one place in the weather row: its picture, its name underneath, and a lit plate behind it while it's on
    void drawSlot (juce::Graphics& g, int i, bool on, const juce::String& label) const
    {
        // (the faltung twins: every picture a few thin white lines; a switch that is on stands on a filled
        //  parallelogram in the window's line colour, its name in mono underneath)
        using namespace juce;
        const auto r = slot (i);
        const Point<float> c (r.getCentreX(), r.getY() + 22.0f);
        const float s = 30.0f;
        if (on)
        {
            g.setColour (bal::line().withAlpha (0.85f));
            g.fillPath (fal::ui::skewRect (r.reduced (6.0f, 4.0f).withTrimmedBottom (14.0f), 0.3f));
        }
        const auto ink = on ? fal::ui::inkFor (bal::line()) : bal::white().withAlpha (0.85f);
        const float th = 1.2f;
        auto stroke = [&] (const Path& p) { g.setColour (ink); g.strokePath (p, PathStrokeType (th)); };
        auto seg = [&] (Point<float> a, Point<float> b) { g.setColour (ink); g.drawLine ({ a, b }, th); };
        auto cloud = [&] (Point<float> at, float size)   // a cloud: a low faceted hull
        {
            stroke (bal::poly ({ { at.x - size * 0.5f, at.y + size * 0.22f }, { at.x - size * 0.42f, at.y - size * 0.02f },
                                 { at.x - size * 0.18f, at.y - size * 0.12f }, { at.x, at.y - size * 0.3f }, { at.x + size * 0.26f, at.y - size * 0.16f },
                                 { at.x + size * 0.48f, at.y + size * 0.0f }, { at.x + size * 0.5f, at.y + size * 0.22f } }));
            seg ({ at.x - size * 0.18f, at.y - size * 0.12f }, { at.x - size * 0.05f, at.y + size * 0.22f });
        };
        auto sun = [&] (Point<float> at, float rad, float turn)   // an octagon with short rays
        {
            Path p;
            for (int k = 0; k < 8; ++k)
            {
                const float ang = (float) k * 0.7854f + turn;
                const Point<float> q (at.x + std::cos (ang) * rad, at.y + std::sin (ang) * rad);
                if (k == 0) p.startNewSubPath (q); else p.lineTo (q);
                seg ({ at.x + std::cos (ang) * rad * 1.35f, at.y + std::sin (ang) * rad * 1.35f }, { at.x + std::cos (ang) * rad * 1.75f, at.y + std::sin (ang) * rad * 1.75f });
            }
            p.closeSubPath();
            stroke (p);
        };
        const float move = on ? t : 0.0f;   // (the pictures only move while their weather is on)
        switch (i)
        {
            case slotCloudy:
                sun (c.translated (-s * 0.2f, -s * 0.18f), s * 0.17f, move * 0.5f);
                cloud (c.translated (s * 0.08f + (on ? 1.5f * std::sin (t * 0.8f) : 0.0f), s * 0.12f), s * 0.8f);
                break;
            case slotRain:
                cloud (c.translated (0.0f, -s * 0.1f), s * 0.85f);
                for (int k = 0; k < 3; ++k)
                {
                    const float u = on ? (move * 1.6f + (float) k * 0.33f) - std::floor (move * 1.6f + (float) k * 0.33f) : 0.0f;
                    const float x = c.x - s * 0.25f + (float) k * s * 0.25f, y = c.y + s * 0.24f + u * s * 0.16f;
                    seg ({ x, y }, { x - s * 0.06f, y + s * 0.2f });
                }
                break;
            case slotSnow:
                for (int k = 0; k < 3; ++k)
                {
                    const float ang = (float) k * 1.0472f + move * 0.4f;
                    seg ({ c.x - std::cos (ang) * s * 0.42f, c.y - std::sin (ang) * s * 0.42f }, { c.x + std::cos (ang) * s * 0.42f, c.y + std::sin (ang) * s * 0.42f });
                }
                break;
            case slotHeat:
                sun (c.translated (0.0f, -s * 0.12f), s * 0.17f, move * 0.6f);
                for (int k = 0; k < 2; ++k)
                {
                    std::vector<Point<float>> pts;
                    for (int j = 0; j <= 8; ++j)
                        pts.push_back ({ c.x - s * 0.35f + (float) j * s * 0.0875f, c.y + s * 0.32f + (float) k * s * 0.14f + ((j % 2) ? 1.0f : -1.0f) * s * 0.04f * (on ? std::cos (move * 4.0f) : 1.0f) });
                    stroke (bal::polyline (pts));
                }
                break;
            case slotFog:
            {
                const float rows[3][3] = { { -0.4f, 0.4f, -0.22f }, { -0.28f, 0.44f, 0.0f }, { -0.4f, 0.2f, 0.22f } };
                for (int k = 0; k < 3; ++k)
                {
                    const float dx = on ? 2.0f * std::sin (t * 1.3f + (float) k * 2.0f) : 0.0f;
                    seg ({ c.x + rows[k][0] * s + dx, c.y + rows[k][2] * s }, { c.x + rows[k][1] * s + dx, c.y + rows[k][2] * s });
                }
                break;
            }
            case slotWind:
                for (int k = 0; k < 2; ++k)
                {
                    const float dx = on ? 2.5f * std::sin (t * 4.0f + (float) k * 1.5f) : 0.0f;
                    const float y = c.y - s * 0.12f + (float) k * s * 0.26f, turn = k == 0 ? -1.0f : 1.0f;
                    stroke (bal::polyline ({ { c.x - s * 0.42f + dx, y }, { c.x + s * 0.18f + dx, y }, { c.x + s * 0.3f + dx, y + turn * s * 0.12f },
                                             { c.x + s * 0.18f + dx, y + turn * s * 0.24f }, { c.x + s * 0.08f + dx, y + turn * s * 0.14f } }));
                }
                break;
            case slotHail:
                cloud (c.translated (0.0f, -s * 0.15f), s * 0.8f);
                for (int k = 0; k < 3; ++k)
                {
                    const float bounce = on ? std::abs (std::sin (t * 5.0f + (float) k * 1.1f)) * s * 0.12f : 0.0f;
                    const auto q = Point<float> (c.x - s * 0.24f + (float) k * s * 0.24f, c.y + s * 0.4f + (float) (k % 2) * s * 0.06f - bounce);
                    stroke (bal::poly ({ q.translated (0.0f, -3.0f), q.translated (3.0f, 0.0f), q.translated (0.0f, 3.0f), q.translated (-3.0f, 0.0f) }));
                }
                break;
            case slotAurora:
                for (int k = 0; k < 3; ++k)
                {
                    Path p;
                    const float x = c.x - s * 0.26f + (float) k * s * 0.26f, sway = on ? 2.0f * std::sin (t * 1.5f + (float) k) : 0.0f;
                    p.startNewSubPath (x + sway, c.y - s * 0.38f);
                    p.quadraticTo (x + s * 0.16f - sway, c.y, x + sway, c.y + s * 0.38f);
                    g.setColour (on ? ink : bal::strip (k + 1).withAlpha (0.95f));
                    g.strokePath (p, PathStrokeType (1.6f));
                }
                break;
            case slotTwister:
                for (int k = 0; k < 4; ++k)
                {
                    const float half = s * (0.42f - 0.1f * (float) k);
                    const float dx = on ? 2.0f * std::sin (t * 9.0f + (float) k * 1.3f) : (float) (k % 2) * s * 0.05f;
                    seg ({ c.x - half + dx, c.y - s * 0.32f + (float) k * s * 0.22f }, { c.x + half + dx, c.y - s * 0.32f + (float) k * s * 0.22f });
                }
                break;
            default:   // auto: the suite's toggle mark
            {
                const auto m = Rectangle<float> (s * 0.84f, s * 0.4f).withCentre (c);
                const auto p = fal::ui::skewRect (m);
                if (on) { g.setColour (ink); g.fillPath (p); }
                g.setColour (ink); g.strokePath (p, PathStrokeType (th));
                break;
            }
        }
        bal::label (g, label, r.withTop (r.getY() + 41.0f).withHeight (13.0f), on ? bal::white() : bal::ink().withAlpha (0.75f), 8.5f);
    }

    // (the first version's small switch with its name beside it; kept for anything that still wants one)
    void drawButton (juce::Graphics& g, int i, bool on) const
    {
        using namespace juce;
        const auto r = button (i);
        const Point<float> c (r.getX() + 13.0f, r.getCentreY());
        const float a = on ? 1.0f : 0.55f;
        const auto ink = Colours::white.withAlpha (0.9f * a);
        auto stroke = [&] (const Path& p, Colour col, float th) { g.setColour (col); g.strokePath (p, PathStrokeType (th, PathStrokeType::curved, PathStrokeType::rounded)); };
        switch (i)
        {
            case fog:
                for (int k = 0; k < 3; ++k)
                {
                    const float dx = on ? 2.0f * std::sin (t * 1.3f + (float) k * 2.0f) : 0.0f;
                    g.setColour (ink.withMultipliedAlpha (1.0f - 0.2f * (float) k));
                    g.fillRoundedRectangle (c.x - 9.0f + dx + (k == 1 ? 3.0f : 0.0f), c.y - 6.0f + 5.0f * (float) k, 16.0f - (k == 2 ? 5.0f : 0.0f), 2.4f, 1.2f);
                }
                break;
            case wind:
                for (int k = 0; k < 2; ++k)
                {
                    const float dx = on ? 2.5f * std::sin (t * 4.0f + (float) k * 1.5f) : 0.0f;
                    Path p;
                    const float y = c.y - 3.0f + 6.0f * (float) k;
                    p.startNewSubPath (c.x - 10.0f + dx, y);
                    p.lineTo (c.x + 3.0f + dx, y);
                    p.addCentredArc (c.x + 3.0f + dx, y - 3.0f + 6.0f * (float) k, 3.0f, 3.0f, 0.0f, k == 0 ? MathConstants<float>::pi : 0.0f, k == 0 ? MathConstants<float>::pi * 0.1f : MathConstants<float>::pi * 0.9f, false);
                    stroke (p, ink, 1.6f);
                }
                break;
            case hail:
                for (int k = 0; k < 3; ++k)
                {
                    const float u = on ? std::abs (std::sin (t * 4.0f + (float) k * 1.1f)) : 0.3f + 0.2f * (float) k;
                    g.setColour (ink);
                    g.fillEllipse (Rectangle<float> (4.4f, 4.4f).withCentre ({ c.x - 7.0f + 7.0f * (float) k, c.y + 5.0f - 10.0f * u }));
                }
                break;
            case aurora:
            {
                static const uint32 cols[3] = { 0xff4cffa0, 0xff59e0ff, 0xffa77bff };
                for (int k = 0; k < 3; ++k)
                {
                    Path p;
                    const float x = c.x - 7.0f + 7.0f * (float) k, sway = on ? 2.0f * std::sin (t * 1.5f + (float) k) : 0.0f;
                    p.startNewSubPath (x + sway, c.y - 8.0f);
                    p.quadraticTo (x + 3.0f - sway, c.y, x + sway, c.y + 8.0f);
                    stroke (p, Colour (cols[k]).withAlpha (a), 2.4f);
                }
                break;
            }
            default:
                for (int k = 0; k < 4; ++k)
                {
                    const float half = 9.0f - 2.2f * (float) k;
                    const float dx = on ? 2.0f * std::sin (t * 9.0f + (float) k * 1.3f) : 0.6f * (float) (k % 2);
                    g.setColour (ink);
                    g.fillRoundedRectangle (c.x - half + dx, c.y - 8.0f + 4.6f * (float) k, half * 2.0f, 2.2f, 1.1f);
                }
                break;
        }
        g.setFont (SadLookAndFeel::uiFont (10.5f, true));
        g.setColour (palette::blush.withAlpha (on ? 0.95f : 0.6f));
        g.drawText (String (name (i)) + (on ? (i == twister ? "!" : ": on") : ""), r.withTrimmedLeft (28.0f), Justification::centredLeft);
    }

private:
    juce::Point<float> funnelFoot() const noexcept { return { w * 0.5f + w * 0.24f * std::sin (in.twisterAngle * 0.06f), h - 34.0f }; }
    struct Stone { juce::Point<float> p, v; float size; int bounces; };
    struct Streak { juce::Point<float> p, v; float len, phase; };
    In in;
    std::vector<Stone> stones;
    std::vector<Streak> streaks;
    float w = 1060.0f, h = 600.0f, t = 0.0f, streakBudget = 0.0f;
    int hitsSeen = -1;
};

//==============================================================================
// The sliders page (the original's gumball menu): every balloon's control on it again, as a slider that stays
// where it is. (The stutter effect is called the gumball machine; this page is its menu of gumballs.) One row for each effect, in its balloon's colour. Each gumball is a glass ball that fills
// with colour as its value goes up, with the value written inside.
//   drag up or down: turn it (shift: finely)   scroll: turn it   click a switch: flip it
//   click a choice: the next one (right-click: the one before)   double-click: back to where it starts
// They are the same parameters as the balloons: turn one and the other follows. When an effect's
// balloons pop in or out of the room (bought at the store, lent by the lightning, popped), its row of
// gumballs pops in or out here.
class GumballPage : public juce::Component
{
public:
    struct Knob { juce::RangedAudioParameter* param = nullptr; juce::String label; bool toggle = false, choice = false, stepped = false; };
    struct Row { int key = 0; juce::String name; int hue = 0; std::vector<Knob> knobs; };

    std::function<std::vector<Row>()> getRows;   // what's in the room right now
    std::function<void()> onActivity;

    GumballPage()
    {
        setInterceptsMouseClicks (true, false);
        setWantsKeyboardFocus (true);
    }

    static constexpr float W = 1060.0f, H = 600.0f, R = 19.0f, knobPitch = 68.0f, linePitch = 64.0f, labelW = 104.0f, top = 62.0f, left = 28.0f;

    void open()
    {
        setBounds (0, 0, (int) W, (int) H);
        shown.clear();
        sync (true);
        setVisible (true);
        toFront (true);
        target = 1.0f;
        scroll = 0.0f;
    }
    void close() { target = 0.0f; endDrag(); }
    bool isOpen() const noexcept { return isVisible() && target > 0.5f; }

    bool step (float dt)
    {
        if (anim != target)
        {
            anim = target > anim ? juce::jmin (target, anim + dt * 4.0f) : juce::jmax (target, anim - dt * 4.0f);
            if (anim <= 0.0f) { setVisible (false); shown.clear(); }
        }
        if (! isVisible()) return false;
        t += dt;
        sync (false);
        for (auto& s : shown)
        {
            // each row pops in with a little bounce, and out
            const float to = s.leaving ? 0.0f : 1.0f;
            s.popVel += ((to - s.pop) * 190.0f - s.popVel * 15.0f) * dt;
            s.pop += s.popVel * dt;
            if (s.leaving && s.pop < 0.03f) s.gone = true;
            s.pos += (s.home - s.pos) * juce::jmin (1.0f, dt * 12.0f);
        }
        for (auto& s : shown)
            if (s.gone) for (int k = 0; k < 10; ++k)
                sparks.push_back ({ s.pos + juce::Point<float> (labelW + 20.0f * (float) k, 0.0f), { (float) (k % 5 - 2) * 46.0f, -90.0f - 14.0f * (float) (k % 4) }, 0.5f, s.row.hue });
        shown.erase (std::remove_if (shown.begin(), shown.end(), [] (const Shown& s) { return s.gone; }), shown.end());
        for (auto& sp : sparks) { sp.v.y += 500.0f * dt; sp.p += sp.v * dt; sp.life -= dt; }
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
        repaint();
        return true;
    }

    // where a gumball is (for the tests and the manual): row by key, knob by index; empty if it isn't there
    juce::Point<float> knobCentre (int key, int index) const
    {
        for (auto& s : shown)
            if (s.row.key == key && juce::isPositiveAndBelow (index, (int) s.row.knobs.size())) return centreOf (s, index);
        return {};
    }
    juce::Point<float> centreFor (const juce::RangedAudioParameter* prm) const
    {
        for (auto& s : shown)
            for (int k = 0; k < (int) s.row.knobs.size(); ++k)
                if (s.row.knobs[(size_t) k].param == prm && ! s.leaving) return centreOf (s, k).translated (0.0f, -scroll);
        return {};
    }
    int numRows() const noexcept { int n = 0; for (auto& s : shown) n += s.leaving ? 0 : 1; return n; }
    int numKnobs() const noexcept { int n = 0; for (auto& s : shown) if (! s.leaving) n += (int) s.row.knobs.size(); return n; }
    juce::Rectangle<float> closeArea() const noexcept { return { W - 46.0f, 18.0f, 28.0f, 28.0f }; }

    void paint (juce::Graphics& g) override
    {
        // (the faltung twins: a sheet of the suite's paper over the room, every control a gumball drawn as a
        //  thin octagon that fills with its row's colour, the names in mono)
        using namespace juce;
        const float a = anim * anim * (3.0f - 2.0f * anim);
        g.setColour (bal::paper().withAlpha (0.97f * a));
        g.fillRect (getLocalBounds());
        g.setOpacity (a);
        bal::title (g, "sliders", Rectangle<float> (left, 14.0f, 320.0f, 34.0f), bal::paperInk().withAlpha (a), 24.0f);
        bal::label (g, "drag along or up and down, or scroll  -  click a switch  -  double-click: back to the start  -  the balloons follow",
                    Rectangle<float> (300.0f, 18.0f, W - 360.0f, 28.0f), bal::paperInk().withAlpha (0.6f * a), 10.0f, Justification::centredRight);
        {
            const auto c = closeArea().reduced (7.0f);
            g.setColour (bal::paperInk().withAlpha (a));
            g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.4f);
            g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.4f);
        }

        g.saveState();
        g.reduceClipRegion (Rectangle<int> (0, (int) top - 34, (int) W, (int) (H - top) + 34));
        for (auto& s : shown)
        {
            const float pop = jlimit (0.0f, 1.25f, s.pop);
            if (pop <= 0.01f) continue;
            const auto rowC = bal::hue (s.row.hue);
            bal::foldStrip (g, Rectangle<float> (s.pos.x, s.pos.y - scroll - 10.0f, labelW - 22.0f, 20.0f), rowC.withMultipliedAlpha (a * jmin (1.0f, pop)), s.row.name, false, 0.0f, 11.0f);
            for (int k = 0; k < (int) s.row.knobs.size(); ++k)
                drawGumball (g, centreOf (s, k).translated (0.0f, -scroll), R * pop, s.row.hue, s.row.knobs[(size_t) k], a,
                             hoverKey == s.row.key && hoverIndex == k);
        }
        for (auto& sp : sparks)
        {
            g.setColour (bal::hue (sp.hue).withAlpha (a * jlimit (0.0f, 1.0f, sp.life * 2.0f)));
            g.drawLine ({ sp.p.translated (-2.5f, -scroll), sp.p.translated (2.5f, -scroll) }, 1.2f);
        }
        g.restoreState();

        // what you're pointing at, in full
        if (auto* kn = knobAt (hoverKey, hoverIndex))
        {
            const auto c = knobCentre (hoverKey, hoverIndex).translated (0.0f, -scroll);
            const String text = kn->param->getName (40) + ":  " + kn->param->getCurrentValueAsText();
            const float tw = GlyphArrangement::getStringWidth (bal::mono (11.0f), text) + 18.0f;
            auto box = Rectangle<float> (tw, 20.0f).withCentre ({ jlimit (tw * 0.5f + 6.0f, W - tw * 0.5f - 6.0f, c.x), c.y - R - 16.0f });
            if (box.getY() < top - 30.0f) box = box.withY (c.y + R + 26.0f);
            g.setColour (bal::paperInk().withAlpha (0.92f * a));
            g.fillPath (fal::ui::skewRect (box, 0.3f));
            bal::label (g, text, box, bal::paper().withAlpha (a), 11.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hit (e.position, hoverKey, hoverIndex); }
    void mouseExit (const juce::MouseEvent&) override { if (dragKnob == nullptr) hoverKey = hoverIndex = -1; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onActivity) onActivity();
        if (closeArea().contains (e.position)) { close(); return; }
        int key = -1, index = -1;
        hit (e.position, key, index);
        auto* kn = knobAt (key, index);
        if (kn == nullptr) return;
        hoverKey = key; hoverIndex = index;
        auto& p = *kn->param;
        if (kn->toggle)
        {
            setOnce (p, p.getValue() > 0.5f ? 0.0f : 1.0f);
            return;
        }
        if (kn->choice)
        {
            const int n = juce::jmax (2, p.getNumSteps());
            const int now = juce::roundToInt (p.getValue() * (float) (n - 1));
            const int next = (now + (e.mods.isPopupMenu() ? n - 1 : 1)) % n;
            setOnce (p, (float) next / (float) (n - 1));
            return;
        }
        dragKnob = &p;
        dragFrom = p.getValue();
        dragY = e.position.y;
        dragX = e.position.x;
        p.beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragKnob == nullptr) return;
        const float range = e.mods.isShiftDown() ? 900.0f : 170.0f;
        dragKnob->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragFrom + ((dragY - e.position.y) + (e.position.x - dragX)) / range));   // (up or to the right)
        if (onActivity) onActivity();
    }

    void mouseUp (const juce::MouseEvent&) override { endDrag(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        int key = -1, index = -1;
        hit (e.position, key, index);
        if (auto* kn = knobAt (key, index))
            if (! kn->toggle && ! kn->choice) setOnce (*kn->param, kn->param->getDefaultValue());
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        int key = -1, index = -1;
        hit (e.position, key, index);
        auto* kn = knobAt (key, index);
        if (kn == nullptr)
        {
            scroll = juce::jlimit (0.0f, maxScroll, scroll - wd.deltaY * 220.0f);   // (only when there are more rows than fit)
            return;
        }
        if (kn->toggle) return;
        auto& p = *kn->param;
        if (kn->choice || kn->stepped)
        {
            wheelAcc += wd.deltaY;
            if (std::abs (wheelAcc) < 0.12f) return;
            const int n = juce::jmax (2, p.getNumSteps());
            const int now = juce::roundToInt (p.getValue() * (float) (n - 1));
            const int next = juce::jlimit (0, n - 1, now + (wheelAcc > 0.0f ? 1 : -1));
            wheelAcc = 0.0f;
            setOnce (p, (float) next / (float) (n - 1));
            return;
        }
        setOnce (p, juce::jlimit (0.0f, 1.0f, p.getValue() + wd.deltaY * (e.mods.isShiftDown() ? 0.05f : 0.3f)));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

private:
    struct Shown
    {
        Row row;
        juce::Point<float> pos, home;   // the row's left edge and middle line (page coordinates, before scrolling)
        float pop = 1.0f, popVel = 0.0f;
        bool leaving = false, gone = false;
    };
    struct Spark { juce::Point<float> p, v; float life; int hue; };

    juce::Point<float> centreOf (const Shown& s, int k) const noexcept { return { s.pos.x + labelW + R + knobPitch * (float) k, s.pos.y }; }
    static float rowWidth (const Row& r) noexcept { return labelW + knobPitch * (float) r.knobs.size(); }

    // bring the list in line with the room: new rows pop in, missing ones pop out, and everything finds its place
    void sync (bool atOnce)
    {
        if (! getRows) return;
        auto rows = getRows();
        for (auto& s : shown) s.leaving = true;
        for (auto& r : rows)
        {
            auto it = std::find_if (shown.begin(), shown.end(), [&] (const Shown& s) { return s.row.key == r.key; });
            if (it != shown.end()) { it->row = std::move (r); it->leaving = false; continue; }
            Shown s;
            s.row = std::move (r);
            s.pop = atOnce ? 1.0f : 0.0f;
            s.pos = { -1.0f, -1.0f };
            // keep the room's order: after the last row that comes before it
            auto at = shown.end();
            for (auto j = shown.begin(); j != shown.end(); ++j) if (j->row.key > s.row.key) { at = j; break; }
            shown.insert (at, std::move (s));
        }
        // one row to an effect while they all fit; with more, short rows share a line
        int staying = 0;
        for (auto& s : shown) staying += s.leaving ? 0 : 1;
        const bool onePerLine = top + linePitch * (float) staying <= H + 6.0f;
        float x = left, y = top + R + 2.0f;
        bool first = true;
        for (auto& s : shown)
        {
            const float rw = rowWidth (s.row);
            if (! first && (onePerLine || x + rw > W - 12.0f)) { x = left; y += linePitch; }
            s.home = { x, y };
            if (s.pos.x < 0.0f || atOnce) s.pos = s.home;
            x += rw + 14.0f;
            first = false;
        }
        maxScroll = juce::jmax (0.0f, y + R + 30.0f - H);
        scroll = juce::jmin (scroll, maxScroll);
    }

    void hit (juce::Point<float> p, int& key, int& index) const
    {
        key = index = -1;
        p.y += scroll;
        for (auto& s : shown)
        {
            if (s.leaving) continue;
            for (int k = 0; k < (int) s.row.knobs.size(); ++k)
                if (centreOf (s, k).getDistanceFrom (p) <= R + 5.0f) { key = s.row.key; index = k; return; }
        }
    }

    const Knob* knobAt (int key, int index) const
    {
        for (auto& s : shown)
            if (s.row.key == key && ! s.leaving && juce::isPositiveAndBelow (index, (int) s.row.knobs.size())) return &s.row.knobs[(size_t) index];
        return nullptr;
    }

    void setOnce (juce::RangedAudioParameter& p, float v)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (v);
        p.endChangeGesture();
        if (onActivity) onActivity();
    }

    void endDrag()
    {
        if (dragKnob != nullptr) dragKnob->endChangeGesture();
        dragKnob = nullptr;
    }

    // the filling gumball
    void drawGumball (juce::Graphics& g, juce::Point<float> c, float r, int hue, const Knob& kn, float alpha, bool hot) const
    {
        // (the faltung twins: each control is the suite's slider, a thin rule with a slanted fill and an edge
        //  mark; a switch is the suite's toggle mark; the name and the value underneath, in mono)
        using namespace juce;
        if (kn.param == nullptr || r < 1.0f) return;
        const auto col = bal::hue (hue).withMultipliedAlpha (alpha);
        const auto inkC = bal::paperInk().withAlpha (alpha);
        const float v = jlimit (0.0f, 1.0f, kn.param->getValue());
        const float w = (knobPitch - 14.0f) * jlimit (0.0f, 1.0f, r / R);
        const auto track = Rectangle<float> (w, 14.0f).withCentre (c.translated (0.0f, -4.0f));
        if (kn.toggle)
        {
            const auto p = fal::ui::skewRect (Rectangle<float> (22.0f, 11.0f).withCentre (track.getCentre()));
            if (v > 0.5f) { g.setColour (col); g.fillPath (p); }
            g.setColour (inkC.withMultipliedAlpha (hot ? 1.0f : 0.8f));
            g.strokePath (p, PathStrokeType (hot ? 1.5f : 1.0f));
        }
        else
            bal::bar (g, track, v, col, inkC.withMultipliedAlpha (hot ? 1.6f : 1.0f), inkC);

        const String value = kn.param->getCurrentValueAsText();
        const auto under = Rectangle<float> (knobPitch - 2.0f, 24.0f).withCentre ({ c.x, c.y + 16.0f });
        g.setFont (bal::mono (9.0f, true));
        g.setColour (inkC.withMultipliedAlpha (hot ? 1.0f : 0.85f));
        g.drawFittedText (kn.label, under.withHeight (11.0f).toNearestInt(), Justification::centred, 1, 0.75f);
        g.setFont (bal::mono (8.6f));
        g.setColour (inkC.withMultipliedAlpha (0.6f));
        g.drawFittedText (value, under.withTrimmedTop (11.0f).toNearestInt(), Justification::centred, 1, 0.7f);
    }

    std::vector<Shown> shown;
    std::vector<Spark> sparks;
    float anim = 0.0f, target = 0.0f, t = 0.0f, scroll = 0.0f, maxScroll = 0.0f, wheelAcc = 0.0f;
    int hoverKey = -1, hoverIndex = -1;
    juce::RangedAudioParameter* dragKnob = nullptr;
    float dragFrom = 0.0f, dragY = 0.0f, dragX = 0.0f;
};

#include "Art.h"

namespace fal
{
using juce::Colour;
using juce::Point;
using juce::Rectangle;

juce::Colour Fold::palette (int i)
{
    static const juce::uint32 c[8] = { 0xff5e8f6a, 0xff8dbb7a, 0xff8ec3e6, 0xff6e80c8, 0xffee8e6e, 0xfff6c590, 0xff97adc4, 0xffcfe6f0 };
    return Colour (c[((i % 8) + 8) % 8]);
}

Point<float> Fold::segment (juce::Graphics& g, Point<float> s, float len, float depth, bool up, Colour c,
                            const juce::String& label, Colour ink, bool outline, float glow)
{
    // flat run: along x, width leans back; rising run: up-right, width along x
    const Point<float> D = up ? Point<float> (len * 0.55f, -len * 0.82f) : Point<float> (len, 0.0f);
    const Point<float> W = up ? Point<float> (depth, 0.0f) : Point<float> (depth * 0.62f, -depth * 0.78f);
    juce::Path face;
    face.startNewSubPath (s); face.lineTo (s + D); face.lineTo (s + D + W); face.lineTo (s + W); face.closeSubPath();
    const Colour fc = c.interpolatedWith (juce::Colours::white, juce::jlimit (0.0f, 0.6f, glow * 0.6f));
    g.setColour (fc);
    g.fillPath (face);
    // the fold: a darker sliver along the far edge reads as the paper turning
    juce::Path fold;
    const Point<float> F = up ? Point<float> (depth * 0.45f, depth * 0.35f) : Point<float> (0.0f, depth * 0.42f);
    fold.startNewSubPath (s + D); fold.lineTo (s + D + F); fold.lineTo (s + D + W + F * 0.0f); fold.closeSubPath();
    g.setColour (fc.darker (0.28f));
    g.fillPath (fold);
    if (outline)
    {
        g.setColour (juce::Colours::white);
        g.strokePath (face, juce::PathStrokeType (2.0f));
    }
    if (label.isNotEmpty())
    {
        const float ang = std::atan2 (D.y, D.x);
        const auto mid = s + D * 0.5f + W * 0.5f;
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (ang, mid.x, mid.y));
        g.setColour (ink);
        g.setFont (ui::sans (15.0f));
        g.drawText (label, Rectangle<float> (mid.x - 60, mid.y - 10, 120, 20), juce::Justification::centred, false);
    }
    return s + D;
}

Art::Art (Processor& proc) : p (proc), pi (proc.pinfo)
{
    for (int i = 0; i < 16; ++i) seenHits[(size_t) i] = p.hitCount[(size_t) i].load();
    if (pi.kind == Kind::Module) setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Art::mouseDown (const juce::MouseEvent& e)
{
    if (pi.kind != Kind::Module) return;
    const bool open = pi.vtype == VType::Hat && e.x > getWidth() / 2;
    p.uiTrigger (open ? 1 : 0, 0.6f + 0.4f * (1.0f - (float) e.y / (float) std::max (1, getHeight())));
}

void Art::tick()
{
    time += 1.0f / 30.0f;
    for (int i = 0; i < 16; ++i)
    {
        const int h = p.hitCount[(size_t) i].load();
        if (h != seenHits[(size_t) i]) { seenHits[(size_t) i] = h; flash[(size_t) i] = 1.0f; }
        else flash[(size_t) i] *= 0.82f;
    }
    if (pi.id == Pid::cl8 && flash[0] > 0.99f)
        sparks.push_back ({ 0.0f, 0.0f, 1.0f });
    for (auto& s : sparks) s.life -= 0.025f;
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0; }), sparks.end());
    if (sparks.size() > 24) sparks.erase (sparks.begin());
    repaint();
}

std::vector<std::pair<juce::String, float>> Art::values() const
{
    std::vector<std::pair<juce::String, float>> v;
    if (pi.kind == Kind::Module)
    {
        const auto& vi = vtypeInfo (pi.vtype);
        for (int s = 0; s < kSlots; ++s)
        {
            const juce::String n (vi.slot[(size_t) s]);
            v.push_back ({ n.substring (std::max (0, n.length() - 3)) + juce::String (s + 1), p.param (vi.slot[(size_t) s]) });
        }
        return v;
    }
    int i = 0;
    for (auto* prm : p.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (! r || dynamic_cast<juce::AudioParameterChoice*> (prm) || dynamic_cast<juce::AudioParameterBool*> (prm)) continue;
        if (r->getParameterID().startsWith ("step")) continue;
        const juce::String n = r->getParameterID();
        v.push_back ({ n.substring (std::max (0, n.length() - 3)) + juce::String (++i), r->getValue() });
        if (v.size() >= 8) break;
    }
    return v;
}

void Art::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colour (pi.ground));
    g.fillRect (b);
    switch (pi.style)
    {
        case Style::Fell:  paintFell (g, b); break;
        case Style::Snd:   paintSnd (g, b); break;
        case Style::Oval:  paintOval (g, b); break;
        case Style::Stdio: paintStdio (g, b); break;
    }
    paintTitle (g, b);
}

// ---- Fell: wireframe polyhedra, white highlight faces ----------------------------------------------
void Art::paintFell (juce::Graphics& g, Rectangle<float> b)
{
    const Colour tint (pi.accent);
    const float fl = flash[0];
    const auto c = Point<float> (b.getCentreX() - 40.0f, b.getCentreY() + 40.0f);

    if (pi.id == Pid::wb1)
    {
        auto* bs = p.bass();
        const float wob = bs ? bs->showWob.load() : 0.0f;
        const float env = bs ? bs->showEnv.load() : 0.0f;
        const float vowel = p.param ("vowel");
        const float cutoff01 = std::log2 (p.param ("cutoff") / 20.0f) / std::log2 (1000.0f);
        const auto cc = Point<float> (b.getCentreX() - 30.0f, b.getCentreY() + 18.0f);
        static const float ang[9] = { 190, 225, 262, 300, 335, 15, 60, 110, 150 };
        std::array<Point<float>, 9> base, top;
        const float h = 30.0f + 70.0f * cutoff01 + 60.0f * wob;
        for (int i = 0; i < 9; ++i)
        {
            const float a = juce::degreesToRadians (ang[i] + wob * 14.0f * std::sin ((float) i * 1.7f));
            const float r = (150.0f + 60.0f * std::sin ((float) i * 2.3f)) * (1.0f + 0.28f * wob * std::sin ((float) i * 1.3f + time * 3.0f)) * (1.0f + 0.08f * env);
            base[(size_t) i] = cc + Point<float> (std::cos (a) * r * 1.5f, std::sin (a) * r * 0.32f + 30.0f);
            top[(size_t) i] = base[(size_t) i] + Point<float> ((wob - 0.5f) * 60.0f * (float) (i % 3), -h * (0.7f + 0.15f * (float) (i % 4)));
        }
        g.setColour (tint);
        for (int i = 0; i < 9; ++i)
        {
            g.drawLine ({ base[(size_t) i], base[(size_t) ((i + 1) % 9)] }, 1.0f);
            g.drawLine ({ top[(size_t) i], top[(size_t) ((i + 1) % 9)] }, 1.0f);
            if (i % 2 == 0) g.drawLine ({ base[(size_t) i], top[(size_t) i] }, 1.0f);
            if (i % 3 == 1) g.drawLine ({ base[(size_t) i], top[(size_t) ((i + 3) % 9)] }, 1.0f);
        }
        // the mouth: how open the vowel is, breathing with the wobble
        static const float openness[5] = { 1.0f, 0.6f, 0.22f, 0.8f, 0.4f };
        const int vi = std::min (3, (int) vowel); const float vt = vowel - (float) vi;
        const float open = lerp (openness[vi], openness[vi + 1], vt) * (0.4f + 0.6f * (p.param ("fmtmix") > 0.01f ? 1.0f : 0.4f));
        const auto mc = cc + Point<float> (-60.0f, -h * 0.4f);
        const float mw = 70.0f + 40.0f * (1.0f - open), mh = 6.0f + 46.0f * open * (0.5f + 0.5f * wob);
        juce::Path mouth;
        mouth.startNewSubPath (mc + Point<float> (-mw * 0.5f, 0)); mouth.lineTo (mc + Point<float> (0, -mh * 0.5f));
        mouth.lineTo (mc + Point<float> (mw * 0.5f, 0)); mouth.lineTo (mc + Point<float> (0, mh * 0.5f)); mouth.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * flash[0]));
        g.fillPath (mouth);
        g.setColour (juce::Colours::white);
        g.strokePath (mouth, juce::PathStrokeType (2.0f));
        // the wobble itself: a lit slab that slides with the lfo
        const auto s0 = base[3] + (base[4] - base[3]) * (0.15f + 0.6f * wob);
        juce::Path slab;
        slab.startNewSubPath (s0); slab.lineTo (s0 + Point<float> (46, -10)); slab.lineTo (s0 + Point<float> (46, -10 - 30 * env - 10));
        slab.lineTo (s0 + Point<float> (0, -30 * env - 10)); slab.closeSubPath();
        g.strokePath (slab, juce::PathStrokeType (2.0f));
        // formant ticks
        float f[3], gg[3]; vowelAt (vowel, f, gg);
        g.setColour (tint.brighter (0.4f));
        for (int k = 0; k < 3; ++k)
        {
            const float x = b.getRight() - 330.0f + std::log2 (f[k] / 200.0f) / 4.5f * 260.0f;
            g.drawLine (x, b.getBottom() - 30.0f, x, b.getBottom() - 30.0f - 50.0f * gg[k], 2.0f);
        }
        g.drawLine (b.getRight() - 330.0f, b.getBottom() - 30.0f, b.getRight() - 70.0f, b.getBottom() - 30.0f, 1.0f);
        g.setFont (ui::mono (10.0f));
        g.drawText ("formants", juce::Rectangle<float> (b.getRight() - 330.0f, b.getBottom() - 26.0f, 120.0f, 12.0f), juce::Justification::centredLeft);
        return;
    }

    if (pi.id == Pid::sw6)
    {
        auto* sw = p.swing();
        const int cur = sw ? sw->shownStep.load() : 0;
        const bool playing = p.playPpq.load() >= 0.0f;
        const int templ = (int) p.param ("template");
        const float swing = p.param ("swing"), amount = p.param ("amount");
        std::array<Point<float>, 16> pt;
        for (int i = 0; i < 16; ++i)
        {
            float off, acc; Swing::templ (templ, i, swing, off, acc);
            off = (off + p.param ("step" + juce::String (i + 1))) * amount;
            const float a = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * ((float) i + off) / 16.0f;
            const float r = 1.0f + off * 0.6f;
            pt[(size_t) i] = c + Point<float> (std::cos (a) * 230.0f * r, std::sin (a) * 88.0f * r - 22.0f);
        }
        g.setColour (tint);
        for (int i = 0; i < 16; ++i)
        {
            g.drawLine ({ pt[(size_t) i], pt[(size_t) ((i + 1) % 16)] }, 1.0f);
            if (i % 2 == 0) g.drawLine ({ pt[(size_t) i], pt[(size_t) ((i + 5) % 16)] }, 1.0f);
            if (i % 4 == 0) g.drawLine ({ pt[(size_t) i], pt[(size_t) ((i + 8) % 16)] }, 1.0f);
            // the grid position each step would have without groove
            const float a = -juce::MathConstants<float>::halfPi + juce::MathConstants<float>::twoPi * (float) i / 16.0f;
            const auto gp = c + Point<float> (std::cos (a) * 230.0f, std::sin (a) * 88.0f - 22.0f);
            g.drawLine ({ gp, pt[(size_t) i] }, 1.0f);
        }
        // the "foil" face: the step being moved right now
        if (playing)
        {
            juce::Path foil;
            foil.startNewSubPath (pt[(size_t) ((cur + 15) % 16)]); foil.lineTo (pt[(size_t) cur]); foil.lineTo (pt[(size_t) ((cur + 1) % 16)]); foil.lineTo (c); foil.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillPath (foil);
            g.setColour (juce::Colours::white);
            g.strokePath (foil, juce::PathStrokeType (1.5f));
        }
        for (int i = 0; i < 16; ++i)
        {
            g.setColour (i == cur && playing ? juce::Colours::white : tint.darker (0.4f));
            g.fillPath (ui::skewRect ({ pt[(size_t) i].x - 6, pt[(size_t) i].y - 4, 12, 8 }));
        }
        return;
    }

    std::array<float, 8> v {};
    const auto vals = values();
    for (size_t i = 0; i < 8 && i < vals.size(); ++i) v[i] = vals[i].second;
    static const float ang[7] = { 200, 250, 300, 345, 30, 95, 150 };
    std::array<Point<float>, 7> base, top;
    const float height = 40.0f + 120.0f * v[1];
    const float skew = -50.0f + 110.0f * v[5];
    for (int i = 0; i < 7; ++i)
    {
        const float a = juce::degreesToRadians (ang[i]);
        const float r = (90.0f + 150.0f * v[(size_t) ((i * 3) % 8)]) * (1.0f + 0.05f * fl);
        base[(size_t) i] = c + Point<float> (std::cos (a) * r * 1.25f, std::sin (a) * r * 0.42f);
        top[(size_t) i] = base[(size_t) i] + Point<float> (skew * (0.4f + 0.12f * i), -height * (0.6f + 0.1f * (i % 3)));
    }
    g.setColour (tint);
    for (int i = 0; i < 7; ++i)
    {
        g.drawLine ({ base[(size_t) i], base[(size_t) ((i + 1) % 7)] }, 1.0f);
        g.drawLine ({ top[(size_t) i], top[(size_t) ((i + 1) % 7)] }, 1.0f);
        if (i % 2 == 0 || i == 5) g.drawLine ({ base[(size_t) i], top[(size_t) i] }, 1.0f);
        if (i == 1 || i == 3) g.drawLine ({ base[(size_t) i], top[(size_t) ((i + 2) % 7)] }, 1.0f);
    }
    g.drawLine ({ top[0], base[3] }, 1.0f);
    g.drawLine ({ base[5], top[2] }, 1.0f);

    // white faces: the click (a lit panel), the body (a small box), the tail (a slab)
    auto quad = [&] (Point<float> a, Point<float> bb, Point<float> cc, Point<float> d, float fillA)
    {
        juce::Path q; q.startNewSubPath (a); q.lineTo (bb); q.lineTo (cc); q.lineTo (d); q.closeSubPath();
        if (fillA > 0.01f) { g.setColour (juce::Colours::white.withAlpha (fillA)); g.fillPath (q); }
        g.setColour (juce::Colours::white);
        g.strokePath (q, juce::PathStrokeType (2.0f));
    };
    const auto m1 = base[1] + (base[2] - base[1]) * 0.25f;
    const auto m2 = base[1] + (base[2] - base[1]) * (0.45f + 0.3f * v[2]);
    const float lift = 18.0f + 50.0f * v[2];
    quad (m1, m2, m2 + Point<float> (8, -lift), m1 + Point<float> (4, -lift * 0.95f), fl * 0.45f);

    const float bs = 10.0f + 26.0f * v[3];
    const auto bc = (base[4] + top[4]) * 0.5f;
    juce::Path box;
    box.startNewSubPath (bc); box.lineTo (bc + Point<float> (bs, -bs * 0.55f)); box.lineTo (bc + Point<float> (bs * 1.6f, 0)); box.lineTo (bc + Point<float> (bs * 0.6f, bs * 0.55f)); box.closeSubPath();
    box.startNewSubPath (bc + Point<float> (bs * 0.6f, bs * 0.55f)); box.lineTo (bc + Point<float> (bs * 0.6f, bs * 0.55f + bs * 0.5f));
    box.lineTo (bc + Point<float> (bs * 1.6f, bs * 0.5f)); box.lineTo (bc + Point<float> (bs * 1.6f, 0));
    box.startNewSubPath (bc); box.lineTo (bc + Point<float> (0, bs * 0.5f)); box.lineTo (bc + Point<float> (bs * 0.6f, bs * 0.55f + bs * 0.5f));
    g.setColour (juce::Colours::white);
    g.strokePath (box, juce::PathStrokeType (2.0f));

    const auto s1 = (base[6] * 0.6f + top[5] * 0.4f);
    const float sl = 30.0f + 70.0f * v[4];
    quad (s1, s1 + Point<float> (sl, -sl * 0.18f), s1 + Point<float> (sl * 0.9f, sl * 0.25f), s1 + Point<float> (-sl * 0.08f, sl * 0.33f), 0.0f);
}

// ---- Snd: folded pastel strips ------------------------------------------------------------------
void Art::paintSnd (juce::Graphics& g, Rectangle<float> b)
{
    const auto vals = values();
    const Colour ground (pi.ground);
    auto colourFor = [&] (int i)
    {
        Colour c = Fold::palette (i + (int) pi.id);
        if (std::abs (c.getPerceivedBrightness() - ground.getPerceivedBrightness()) < 0.06f && std::abs (c.getHue() - ground.getHue()) < 0.08f)
            c = Fold::palette (i + (int) pi.id + 3);
        return c;
    };
    const Colour ink (0xff1e2a20);
    const float fl = flash[0] + flash[1];

    if (pi.id == Pid::fl7)
    {
        auto* f = p.filter();
        const float bands[3] = { p.param ("low"), p.param ("mid"), p.param ("high") };
        const char* names[3] = { "low1", "mid2", "igh3" };
        for (int i = 0; i < 3; ++i)
        {
            const float h01 = bands[i] <= -39.5f ? 0.0f : juce::jmap (bands[i], -40.0f, 6.0f, 0.08f, 1.0f);
            const float x = b.getX() + 70.0f + i * 150.0f, y = b.getBottom() - 50.0f;
            const float w = 110.0f, d = 46.0f, h = 160.0f * h01;
            const Colour c = colourFor (i * 2);
            juce::Path front, side, topF;
            front.addRectangle (x, y - h, w, h);
            side.startNewSubPath (x + w, y); side.lineTo (x + w + d * 0.62f, y - d * 0.78f); side.lineTo (x + w + d * 0.62f, y - d * 0.78f - h); side.lineTo (x + w, y - h); side.closeSubPath();
            topF.startNewSubPath (x, y - h); topF.lineTo (x + w, y - h); topF.lineTo (x + w + d * 0.62f, y - h - d * 0.78f); topF.lineTo (x + d * 0.62f, y - h - d * 0.78f); topF.closeSubPath();
            if (h01 <= 0.0f)
            {
                g.setColour (ink.withAlpha (0.5f));
                const float dash[2] = { 4, 4 };
                juce::Path out; juce::PathStrokeType (1.0f).createDashedStroke (out, topF, dash, 2);
                g.fillPath (out);
            }
            else
            {
                g.setColour (c); g.fillPath (front);
                g.setColour (c.darker (0.3f)); g.fillPath (side);
                g.setColour (c.brighter (0.25f)); g.fillPath (topF);
            }
            g.setColour (ink); g.setFont (ui::sans (15.0f));
            g.drawText (names[i], Rectangle<float> (x, y - 22, w, 20), juce::Justification::centred);
        }
        // the sweep: a translucent plane crossing the bands
        const float sw = f ? f->shownSweep.load() : 0.0f;
        if (std::abs (sw) > 0.015f)
        {
            const float x = b.getX() + 60.0f + (sw + 1.0f) * 0.5f * 470.0f;
            juce::Path plane;
            plane.startNewSubPath (x, b.getBottom() - 20); plane.lineTo (x + 70, b.getBottom() - 110); plane.lineTo (x + 70, b.getY() + 30); plane.lineTo (x, b.getY() + 120); plane.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.45f)); g.fillPath (plane);
            g.setColour (juce::Colours::white); g.strokePath (plane, juce::PathStrokeType (2.0f));
        }
        return;
    }

    // two chains, like the two figures on the sleeve
    const int n = (int) vals.size();
    auto chain = [&] (Point<float> start, int from, int to)
    {
        bool up = false;
        for (int i = from; i < to && i < n; ++i)
        {
            const float len = 46.0f + 104.0f * vals[(size_t) i].second;
            start = Fold::segment (g, start, len, 44.0f, up, colourFor (i), vals[(size_t) i].first, ink, i == focus, fl);
            up = ! up;
        }
    };
    chain ({ b.getX() + 28.0f, b.getY() + 175.0f }, 0, 4);
    chain ({ b.getX() + 250.0f, b.getBottom() - 30.0f }, 4, 8);

    if (pi.id == Pid::mc3)
        if (auto* m = p.micro())
        {
            const int act = m->activity.load();
            juce::Random r ((juce::int64) (time * 30.0f));
            const int count = std::min (40, act / 64);
            for (int i = 0; i < count; ++i)
            {
                const float x = b.getX() + 40.0f + r.nextFloat() * (b.getWidth() - 260.0f);
                const float y = b.getY() + 40.0f + r.nextFloat() * (b.getHeight() - 80.0f);
                g.setColour ((r.nextBool() ? juce::Colours::white : Fold::palette (r.nextInt (8))).withAlpha (0.85f));
                g.fillPath (ui::skewRect ({ x, y, 6.0f + r.nextFloat() * 20.0f, 6.0f }));
            }
        }
}

// ---- Oval: coloured lines on white -----------------------------------------------------------------
void Art::paintOval (juce::Graphics& g, Rectangle<float> b)
{
    const float fb = juce::jlimit (0.0f, 1.1f, p.param ("feedback"));
    const bool frz = p.param ("freeze") > 0.5f;
    const float wow = p.param ("wow"), age = p.param ("age");
    const float tsec = p.dub() ? p.dub()->shownTime.load() : 0.375f;
    static const juce::uint32 cols[4] = { 0xffe06a2e, 0xffe9a0a8, 0xff2f7fd1, 0xff8f6bb8 };
    const float spacing = 7.0f + tsec * 26.0f;
    float lvl = 1.0f;
    for (int k = 1; k <= 14; ++k)
    {
        lvl = frz ? 1.0f : lvl * std::max (0.02f, fb);
        if (lvl < 0.035f) break;
        const float j = std::sin (time * (1.3f + k * 0.2f)) * wow * 9.0f;
        juce::Path arc;
        const float y0 = b.getY() + 40.0f + k * spacing;
        arc.startNewSubPath (b.getX() - 40, y0 + 20);
        arc.quadraticTo (b.getCentreX() - 60 + j, y0 - 120 + j, b.getRight() - 120, y0 - 10);
        g.setColour (Colour (cols[k % 4]).withAlpha (0.35f + 0.65f * std::min (1.0f, lvl)));
        juce::PathStrokeType st (1.0f + 6.0f * std::min (1.0f, lvl), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (frz) { juce::Path dashed; const float d[2] = { 10, 6 }; st.createDashedStroke (dashed, arc, d, 2); g.fillPath (dashed); }
        else g.strokePath (arc, st);
    }
    // the signal bundle
    const Point<float> a (b.getX() - 20, b.getBottom() - 40), e (b.getRight() + 20, b.getY() + 120);
    const float widths[4] = { 5, 3, 2, 7 };
    const juce::uint32 bc[4] = { 0xff2f7fd1, 0xffe06a2e, 0xff8f6bb8, 0x99c9c2a8 };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (Colour (bc[i]));
        g.drawLine ({ a + Point<float> (0, i * 12.0f), e + Point<float> (0, i * 12.0f) }, widths[i]);
    }
    // taps: one dot per repeat, sized by its level
    lvl = 1.0f;
    g.setColour (Colour (0xff1d1d1d));
    for (int k = 1; k <= 8; ++k)
    {
        lvl = frz ? 1.0f : lvl * std::max (0.02f, fb);
        const float t = std::min (1.0f, tsec * k / 3.0f);
        const auto pnt = a + (e - a) * (0.08f + 0.88f * t);
        const float r = 1.5f + 3.5f * std::min (1.0f, lvl);
        g.fillEllipse (pnt.x - r, pnt.y - r, 2 * r, 2 * r);
        g.drawLine (pnt.x, pnt.y, pnt.x, pnt.y + 30 + 12 * k, 1.0f);
    }
    // age: hatching
    g.setColour (Colour (0xff1d1d1d).withAlpha (0.55f));
    const int hatch = (int) (age * 14.0f);
    for (int i = 0; i < hatch; ++i)
        g.drawLine (b.getX() + 360.0f + i * 6.0f, b.getBottom() - 70.0f, b.getX() + 368.0f + i * 6.0f, b.getBottom() - 84.0f, 0.8f);
    g.setColour (Colour (0x998fbf6a));
    g.fillPath (ui::skewRect ({ b.getX() + 220.0f, b.getBottom() - 110.0f, 70.0f, 34.0f }, 0.4f));
    g.setColour (Colour (0xb3f6c590));
    g.fillRect (b.getRight() - 230.0f, b.getY() + 150.0f, 44.0f, 26.0f);
}

// ---- Stdio: pale chevrons, the yellow cube, a tracklist --------------------------------------------
void Art::paintStdio (juce::Graphics& g, Rectangle<float> b)
{
    const Colour chev (0xff9fd3ec);
    int count = 5; float step = 80.0f; float alpha = 0.34f;
    if (pi.id == Pid::rv5)
    {
        count = 3 + (int) (p.param ("size") * 7.0f);
        step = 50.0f + p.param ("predelay") * 0.8f;
        alpha = 0.18f + 0.3f * std::min (1.0f, p.param ("decay") / 4.0f);
        if (auto* r = p.room()) alpha *= 0.35f + 0.65f * r->gateOpen.load();
    }
    if (pi.id == Pid::gl8)
        if (auto* gl = p.glue()) { count = 2 + (int) std::min (10.0f, gl->gr.load() / 1.5f); step = 56.0f; }
    auto chevron = [&] (float x, float y, float w, float h, float a)
    {
        const float d = h * 0.42f;
        juce::Path c;
        c.startNewSubPath (x, y + h); c.lineTo (x + w, y + h); c.lineTo (x + w + d, y + h * 0.5f);
        c.lineTo (x + w, y); c.lineTo (x, y); c.lineTo (x + d, y + h * 0.5f); c.closeSubPath();
        g.setColour (chev.withAlpha (a)); g.fillPath (c);
    };
    for (int i = 0; i < count; ++i)
        chevron (b.getX() + 300.0f + i * step * 0.8f, b.getY() + 150.0f - i * step * 0.55f, 110.0f, 300.0f, alpha * (i % 2 ? 1.0f : 0.8f));
    for (auto& s : sparks)
    {
        const float k = 1.0f - s.life;
        chevron (b.getX() + 260.0f + k * 300.0f, b.getY() + 120.0f - k * 120.0f, 60.0f, 160.0f, 0.5f * s.life);
    }
    g.setColour (Colour (0xff7cc2e4).withAlpha (0.7f));
    for (int i = 0; i < 3; ++i)
    {
        const float x = b.getX() + 340.0f + i * 160.0f, y = b.getY() + 280.0f - i * 100.0f;
        g.drawLine (x, y, x + 100, y - 120, 1.0f); g.drawLine (x + 100, y - 120, x, y - 240, 1.0f);
    }

    // the cube
    const float cx = b.getX() + 70.0f, cy = b.getY() + 120.0f;
    const float pulse = 1.0f + 0.08f * flash[0];
    juce::Path topF, l, r;
    auto P = [&] (float x, float y) { return Point<float> (cx + x * pulse, cy + y * pulse); };
    topF.startNewSubPath (P (0, 30)); topF.lineTo (P (52, 0)); topF.lineTo (P (104, 30)); topF.lineTo (P (52, 60)); topF.closeSubPath();
    l.startNewSubPath (P (0, 30)); l.lineTo (P (52, 60)); l.lineTo (P (52, 112)); l.lineTo (P (0, 82)); l.closeSubPath();
    r.startNewSubPath (P (52, 60)); r.lineTo (P (104, 30)); r.lineTo (P (104, 82)); r.lineTo (P (52, 112)); r.closeSubPath();
    g.setColour (Colour (0xffffe15a)); g.fillPath (topF);
    g.setColour (Colour (0xfff2c100)); g.fillPath (l);
    g.setColour (Colour (0xffd8a800)); g.fillPath (r);
    g.setColour (Colour (0xff2a2100)); g.setFont (ui::sans (13.0f, true));
    g.drawText (pi.code, Rectangle<float> (cx + 22, cy + 22, 60, 16), juce::Justification::centred);

    // tracklist: every value with a timecode, like the back of the sleeve
    g.setFont (ui::mono (10.0f));
    const auto ink = ui::inkFor (Colour (pi.ground));
    float y = b.getY() + 34.0f;
    int i = 0;
    for (auto* prm : p.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (! rp) continue;
        const juce::String tc = "01 : " + juce::String (i / 6).paddedLeft ('0', 2) + " : " + juce::String ((i * 17) % 60).paddedLeft ('0', 2);
        g.setColour (ink.withAlpha (0.55f));
        g.drawText (tc, Rectangle<float> (b.getRight() - 300.0f, y, 90.0f, 12.0f), juce::Justification::centredLeft);
        g.setColour (ink);
        g.drawText (rp->getName (20), Rectangle<float> (b.getRight() - 205.0f, y, 90.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (rp->getCurrentValueAsText(), Rectangle<float> (b.getRight() - 112.0f, y, 80.0f, 12.0f), juce::Justification::centredRight);
        y += 13.0f; ++i;
        if (y > b.getBottom() - 16.0f) break;
    }
    if (pi.id == Pid::gl8)
        if (auto* gl = p.glue())
        {
            const float grDb = gl->gr.load();
            g.setColour (ink);
            g.setFont (ui::mono (11.0f, true));
            g.drawText ("gr -" + juce::String (grDb, 1) + " db", Rectangle<float> (b.getX() + 40, b.getBottom() - 40, 140, 16), juce::Justification::centredLeft);
            g.setColour (Colour (pi.accent));
            g.fillPath (ui::skewRect ({ b.getX() + 40, b.getBottom() - 18, std::min (240.0f, grDb * 16.0f), 8 }));
        }
}

// ---- titles ------------------------------------------------------------------------------------------
void Art::paintTitle (juce::Graphics& g, Rectangle<float> b)
{
    const Colour ground (pi.ground);
    const auto ink = ui::inkFor (ground);
    switch (pi.style)
    {
        case Style::Fell:
            g.setColour (juce::Colours::white);
            g.setFont (ui::sans (26.0f, true));
            g.drawText ("FALTUNG", Rectangle<float> (40, 26, 300, 32), juce::Justification::centredLeft);
            g.setColour (ink);
            g.setFont (ui::sans (13.0f));
            g.drawText (juce::String (pi.code).toUpperCase() + " " + juce::String (pi.name).toUpperCase(), Rectangle<float> (40, 56, 300, 18), juce::Justification::centredLeft);
            break;
        case Style::Snd:
        {
            const Rectangle<float> box (b.getRight() - 186.0f, 48.0f, 150.0f, 98.0f);
            g.setColour (Colour (pi.accent).interpolatedWith (ground, 0.35f));
            g.fillRect (box);
            const auto bi = ui::inkFor (Colour (pi.accent));
            g.setColour (bi);
            g.setFont (ui::sans (17.0f, true));
            g.drawText (pi.code, box.reduced (16, 12).removeFromTop (22), juce::Justification::topLeft);
            g.setFont (ui::sans (15.0f));
            g.drawText (pi.name, box.reduced (16, 12).withTrimmedTop (22).removeFromTop (20), juce::Justification::topLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText ("faltung", box.reduced (16, 10), juce::Justification::bottomLeft);
            break;
        }
        case Style::Oval:
        {
            const float x = b.getRight() - 300.0f, y = b.getBottom() - 70.0f;
            g.setColour (ink);
            g.fillRect (x, y, 1.0f, 30.0f);
            g.setFont (ui::sans (16.0f));
            g.drawText (juce::String (pi.code) + pi.name, Rectangle<float> (x + 8, y - 2, 200, 20), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText (pi.blurb, Rectangle<float> (x + 8, y + 16, 280, 14), juce::Justification::centredLeft);
            break;
        }
        case Style::Stdio:
            g.setColour (ink);
            g.setFont (ui::sans (15.0f, true));
            g.drawText (juce::String ("faltung ") + pi.name, Rectangle<float> (40, 240, 240, 20), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText (pi.blurb, Rectangle<float> (40, 258, 300, 14), juce::Justification::centredLeft);
            break;
    }
}
} // namespace fal

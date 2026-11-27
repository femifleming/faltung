namespace fal::art
{
using juce::Colour;
using juce::Point;
using juce::Rectangle;
namespace pal = ui::palette;

juce::Colour Fold::palette (int i, bool dark)
{
    return ui::accent (Colour (pal::fold::colours[((i % 8) + 8) % 8]), dark);
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
    fold.startNewSubPath (s + D); fold.lineTo (s + D + F); fold.lineTo (s + D + W); fold.closeSubPath();
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

void chevron (juce::Graphics& g, float x, float y, float w, float h, Colour col)
{
    const float d = h * 0.42f;
    juce::Path c;
    c.startNewSubPath (x, y + h); c.lineTo (x + w, y + h); c.lineTo (x + w + d, y + h * 0.5f);
    c.lineTo (x + w, y); c.lineTo (x, y); c.lineTo (x + d, y + h * 0.5f); c.closeSubPath();
    g.setColour (col); g.fillPath (c);
}

void cube (juce::Graphics& g, Point<float> o, float k, const juce::String& code, bool dark)
{
    juce::Path topF, l, r;
    auto P = [&] (float x, float y) { return Point<float> (o.x + x * k, o.y + y * k); };
    topF.startNewSubPath (P (0, 30)); topF.lineTo (P (52, 0)); topF.lineTo (P (104, 30)); topF.lineTo (P (52, 60)); topF.closeSubPath();
    l.startNewSubPath (P (0, 30)); l.lineTo (P (52, 60)); l.lineTo (P (52, 112)); l.lineTo (P (0, 82)); l.closeSubPath();
    r.startNewSubPath (P (52, 60)); r.lineTo (P (104, 30)); r.lineTo (P (104, 82)); r.lineTo (P (52, 112)); r.closeSubPath();
    g.setColour (ui::accent (Colour (pal::stdio::cubeTop), dark));   g.fillPath (topF);
    g.setColour (ui::accent (Colour (pal::stdio::cubeLeft), dark));  g.fillPath (l);
    g.setColour (ui::accent (Colour (pal::stdio::cubeRight), dark)); g.fillPath (r);
    if (code.isNotEmpty())
    {
        g.setColour (Colour (pal::stdio::cubeInk)); g.setFont (ui::sans (13.0f, true));
        g.drawText (code, Rectangle<float> (o.x + 22, o.y + 22, 60, 16), juce::Justification::centred);
    }
}

// ---- Fell: wireframe polyhedra, white highlight faces ----------------------------------------------
void fell (juce::Graphics& g, Rectangle<float> b, const std::vector<Value>& vals, float fl, Colour tint)
{
    const auto c = Point<float> (b.getCentreX() - 40.0f, b.getCentreY() + 40.0f);
    std::array<float, 8> v {};
    for (size_t i = 0; i < 8 && i < vals.size(); ++i) v[i] = juce::jlimit (0.0f, 1.0f, vals[i].value);
    static const float ang[7] = { 200, 250, 300, 345, 30, 95, 150 };
    std::array<Point<float>, 7> base, top;
    const float height = 40.0f + 120.0f * v[1];
    const float skew = -50.0f + 110.0f * v[5];
    for (int i = 0; i < 7; ++i)
    {
        const float a = juce::degreesToRadians (ang[i]);
        const float r = (90.0f + 150.0f * v[(size_t) ((i * 3) % 8)]) * (1.0f + 0.05f * fl);
        base[(size_t) i] = c + Point<float> (std::cos (a) * r * 1.25f, std::sin (a) * r * 0.42f);
        top[(size_t) i] = base[(size_t) i] + Point<float> (skew * (0.4f + 0.12f * (float) i), -height * (0.6f + 0.1f * (float) (i % 3)));
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
void snd (juce::Graphics& g, Rectangle<float> b, const std::vector<Value>& vals, float fl, Colour ground, int seed, int focus)
{
    auto colourFor = [&] (int i)
    {
        Colour c = Fold::palette (i + seed);
        if (std::abs (c.getPerceivedBrightness() - ground.getPerceivedBrightness()) < 0.06f && std::abs (c.getHue() - ground.getHue()) < 0.08f)
            c = Fold::palette (i + seed + 3);
        return c;
    };
    const Colour ink (pal::fold::inkOnStrips);   // on the strips (they stay light enough for it in dark mode)
    // two chains, like the two figures on the sleeve
    const int n = (int) vals.size();
    auto chain = [&] (Point<float> start, int from, int to)
    {
        bool up = false;
        for (int i = from; i < to && i < n; ++i)
        {
            const float len = 46.0f + 104.0f * juce::jlimit (0.0f, 1.0f, vals[(size_t) i].value);
            start = Fold::segment (g, start, len, 44.0f, up, colourFor (i), vals[(size_t) i].label, ink, i == focus, fl);
            up = ! up;
        }
    };
    chain ({ b.getX() + 28.0f, b.getY() + 175.0f }, 0, 4);
    chain ({ b.getX() + 250.0f, b.getBottom() - 30.0f }, 4, 8);
}

// ---- Oval: coloured lines on white -----------------------------------------------------------------
void oval (juce::Graphics& g, Rectangle<float> b, const OvalParams& p)
{
    const float fb = juce::jlimit (0.0f, 1.1f, p.feedback);
    const bool frz = p.freeze;
    const float spacing = 7.0f + p.timeSec * 26.0f;
    float lvl = 1.0f;
    for (int k = 1; k <= 14; ++k)
    {
        lvl = frz ? 1.0f : lvl * std::max (0.02f, fb);
        if (lvl < 0.035f) break;
        const float j = std::sin (p.time * (1.3f + (float) k * 0.2f)) * p.wow * 9.0f;
        juce::Path arc;
        const float y0 = b.getY() + 40.0f + (float) k * spacing;
        arc.startNewSubPath (b.getX() - 40, y0 + 20);
        arc.quadraticTo (b.getCentreX() - 60 + j, y0 - 120 + j, b.getRight() - 120, y0 - 10);
        g.setColour (ui::accent (Colour (pal::oval::lines[k % 4])).withAlpha (0.35f + 0.65f * std::min (1.0f, lvl)));
        juce::PathStrokeType st (1.0f + 6.0f * std::min (1.0f, lvl), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (frz) { juce::Path dashed; const float d[2] = { 10, 6 }; st.createDashedStroke (dashed, arc, d, 2); g.fillPath (dashed); }
        else g.strokePath (arc, st);
    }
    // the signal bundle
    const Point<float> a (b.getX() - 20, b.getBottom() - 40), e (b.getRight() + 20, b.getY() + 120);
    const float widths[4] = { 5, 3, 2, 7 };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (ui::accent (Colour (pal::oval::bundle[i])));
        g.drawLine ({ a + Point<float> (0, (float) i * 12.0f), e + Point<float> (0, (float) i * 12.0f) }, widths[i]);
    }
    // taps: one dot per repeat, sized by its level
    lvl = 1.0f;
    g.setColour (ui::paperInk());
    for (int k = 1; k <= 8; ++k)
    {
        lvl = frz ? 1.0f : lvl * std::max (0.02f, fb);
        const float t = std::min (1.0f, p.timeSec * (float) k / 3.0f);
        const auto pnt = a + (e - a) * (0.08f + 0.88f * t);
        const float r = 1.5f + 3.5f * std::min (1.0f, lvl);
        g.fillEllipse (pnt.x - r, pnt.y - r, 2 * r, 2 * r);
        g.drawLine (pnt.x, pnt.y, pnt.x, pnt.y + 30 + 12 * (float) k, 1.0f);
    }
    // age: hatching
    g.setColour (ui::paperInk().withAlpha (0.55f));
    const int hatch = (int) (p.age * 14.0f);
    for (int i = 0; i < hatch; ++i)
        g.drawLine (b.getX() + 360.0f + (float) i * 6.0f, b.getBottom() - 70.0f, b.getX() + 368.0f + (float) i * 6.0f, b.getBottom() - 84.0f, 0.8f);
    g.setColour (Colour (pal::oval::patchGreen).withAlpha (0.6f));
    g.fillPath (ui::skewRect ({ b.getX() + 220.0f, b.getBottom() - 110.0f, 70.0f, 34.0f }, 0.4f));
    g.setColour (Colour (pal::oval::patchPeach).withAlpha (0.7f));
    g.fillRect (b.getRight() - 230.0f, b.getY() + 150.0f, 44.0f, 26.0f);
}

// ---- Stdio: pale chevrons, the yellow cube, a tracklist --------------------------------------------
void stdio (juce::Graphics& g, Rectangle<float> b, const StdioParams& p, float flash)
{
    const Colour chev (ui::accent (Colour (pal::stdio::chevron)));
    for (int i = 0; i < p.count; ++i)
        chevron (g, b.getX() + 300.0f + (float) i * p.step * 0.8f, b.getY() + 150.0f - (float) i * p.step * 0.55f, 110.0f, 300.0f,
                 chev.withAlpha (p.alpha * (i % 2 ? 1.0f : 0.8f)));
    for (float life : p.sparks)
    {
        const float k = 1.0f - life;
        chevron (g, b.getX() + 260.0f + k * 300.0f, b.getY() + 120.0f - k * 120.0f, 60.0f, 160.0f, chev.withAlpha (0.5f * life));
    }
    g.setColour (ui::accent (Colour (pal::stdio::chevronLine)).withAlpha (0.7f));
    for (int i = 0; i < 3; ++i)
    {
        const float x = b.getX() + 340.0f + (float) i * 160.0f, y = b.getY() + 280.0f - (float) i * 100.0f;
        g.drawLine (x, y, x + 100, y - 120, 1.0f); g.drawLine (x + 100, y - 120, x, y - 240, 1.0f);
    }

    // the cube (pulses on a hit, about its top-left like the original)
    cube (g, { b.getX() + 70.0f, b.getY() + 120.0f }, 1.0f + 0.08f * flash, p.code);

    // tracklist: every value with a timecode, like the back of the sleeve
    g.setFont (ui::mono (10.0f));
    const auto ink = ui::inkFor (p.ground);
    float y = b.getY() + 34.0f;
    int i = 0;
    for (auto& row : p.rows)
    {
        const juce::String tc = "01 : " + juce::String (i / 6).paddedLeft ('0', 2) + " : " + juce::String ((i * 17) % 60).paddedLeft ('0', 2);
        g.setColour (ink.withAlpha (0.55f));
        g.drawText (tc, Rectangle<float> (b.getRight() - 300.0f, y, 90.0f, 12.0f), juce::Justification::centredLeft);
        g.setColour (ink);
        g.drawText (row.name, Rectangle<float> (b.getRight() - 205.0f, y, 90.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (row.value, Rectangle<float> (b.getRight() - 112.0f, y, 80.0f, 12.0f), juce::Justification::centredRight);
        y += 13.0f; ++i;
        if (y > b.getBottom() - 16.0f) break;
    }
}

// ---- titles ------------------------------------------------------------------------------------------
void title (juce::Graphics& g, Rectangle<float> b, Style style, const juce::String& code, const juce::String& name,
            const juce::String& blurb, Colour ground, Colour accentC, const juce::String& brand)
{
    const auto ink = ui::inkFor (ground);
    const float x0 = b.getX(), y0 = b.getY();
    switch (style)
    {
        case Style::Fell:
            g.setColour (juce::Colours::white);
            g.setFont (ui::sans (26.0f, true));
            g.drawText (brand.toUpperCase(), Rectangle<float> (x0 + 40, y0 + 26, 300, 32), juce::Justification::centredLeft);
            g.setColour (ink);
            g.setFont (ui::sans (13.0f));
            g.drawText ((code + " " + name).trim().toUpperCase(), Rectangle<float> (x0 + 40, y0 + 56, 300, 18), juce::Justification::centredLeft);
            break;
        case Style::Snd:
        {
            const Rectangle<float> box (b.getRight() - 186.0f, y0 + 48.0f, 150.0f, 98.0f);
            g.setColour (accentC.interpolatedWith (ground, 0.35f));
            g.fillRect (box);
            g.setColour (ui::inkFor (accentC));
            g.setFont (ui::sans (17.0f, true));
            g.drawText (code, box.reduced (16, 12).removeFromTop (22), juce::Justification::topLeft);
            g.setFont (ui::sans (15.0f));
            g.drawText (name, box.reduced (16, 12).withTrimmedTop (22).removeFromTop (20), juce::Justification::topLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText (brand, box.reduced (16, 10), juce::Justification::bottomLeft);
            break;
        }
        case Style::Oval:
        {
            const float x = b.getRight() - 300.0f, y = b.getBottom() - 70.0f;
            g.setColour (ink);
            g.fillRect (x, y, 1.0f, 30.0f);
            g.setFont (ui::sans (16.0f));
            g.drawText (code + name, Rectangle<float> (x + 8, y - 2, 200, 20), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText (blurb, Rectangle<float> (x + 8, y + 16, 280, 14), juce::Justification::centredLeft);
            break;
        }
        case Style::Stdio:
            g.setColour (ink);
            g.setFont (ui::sans (15.0f, true));
            g.drawText (brand + " " + name, Rectangle<float> (x0 + 40, y0 + 240, 240, 20), juce::Justification::centredLeft);
            g.setFont (ui::mono (10.0f));
            g.drawText (blurb, Rectangle<float> (x0 + 40, y0 + 258, 300, 14), juce::Justification::centredLeft);
            break;
    }
}

// ---- ArtComponent --------------------------------------------------------------------------------------
ArtComponent::ArtComponent (Style s, Colour lg, Colour la) : style (s), ground (lg), accentC (la)
{
    startTimerHz (30);
}

void ArtComponent::hit (float amount)
{
    flash = std::max (flash, amount);
    if (style == Style::Stdio) { sparks.push_back (1.0f); if (sparks.size() > 24) sparks.erase (sparks.begin()); }
    repaint();
}

void ArtComponent::mouseDown (const juce::MouseEvent& e) { if (onClick) onClick (e); }

void ArtComponent::timerCallback()
{
    time += 1.0f / 30.0f;
    flash *= 0.82f;
    if (flash < 0.001f) flash = 0.0f;
    for (auto& s : sparks) s -= 0.025f;
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (float s) { return s <= 0.0f; }), sparks.end());
    repaint();
}

void ArtComponent::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const Colour gr = ui::field (ground), ac = ui::accent (accentC);
    g.setColour (gr);
    g.fillRect (full);

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (getLocalBounds());
    auto b = full;
    if (scaleToFit)
    {
        const float k = std::min (full.getWidth() / kDesignWidth, full.getHeight() / kDesignHeight);
        if (k > 0.0f)
        {
            // keep the design's aspect; the extra room goes to the wider side, centred
            const float w = full.getWidth() / k, h = full.getHeight() / k;
            g.addTransform (juce::AffineTransform::scale (k));
            b = { 0.0f, 0.0f, w, h };
        }
    }
    switch (style)
    {
        case Style::Fell:  fell (g, b, values, flash, ac); break;
        case Style::Snd:   snd (g, b, values, flash, gr, seed, focus); break;
        case Style::Oval:  { auto p = ovalP; p.time = time; oval (g, b, p); break; }
        case Style::Stdio:
        {
            auto p = stdioP;
            p.ground = gr;
            if (p.code.isEmpty()) p.code = tCode;
            if (p.rows.empty()) for (auto& v : values) p.rows.push_back ({ v.label, juce::String (v.value, 2) });
            p.sparks.insert (p.sparks.end(), sparks.begin(), sparks.end());
            stdio (g, b, p, flash);
            break;
        }
    }
    if (showTitle) title (g, b, style, tCode, tName, tBlurb, gr, ac, brand);
}
} // namespace fal::art

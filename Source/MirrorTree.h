#pragma once
// faltung deep: the moving mirrored tree. A canopy of branching trees is drawn into a small float
// buffer every frame (wind sway, leaves shimmering, a glow that breathes with the level), softened,
// mirrored around the centre and coloured in one duotone. Cheap enough to run at 30 fps.
#include <juce_graphics/juce_graphics.h>
#include "dsp/Core.h"

namespace fal
{
class MirrorTree
{
public:
    /** w, h: the size it will be drawn at; the tree is computed at half that */
    void build (uint32_t seed, int w, int h, int trees = 5, int depth = 9)
    {
        W = std::max (32, w / 2); H = std::max (32, h / 2);
        acc.assign ((size_t) (W * H), 0.0f); tmp.assign ((size_t) (W * H), 0.0f);
        img = juce::Image (juce::Image::ARGB, W, H, true);
        br.clear();
        Rng r; r.seed (seed | 1u);
        const float half = (float) W * 0.5f;
        for (int t = 0; t < trees; ++t)
        {
            Root ro;
            ro.x = half - 5.0f - r.uni() * 0.08f * (float) W;
            ro.y = (float) H * (0.93f + r.uni() * 0.05f);
            ro.a = -kPi * 0.5f - 0.6f - r.uni() * 0.5f;
            ro.len = (float) H * (0.19f + r.uni() * 0.04f);
            ro.first = (int) br.size();
            grow (-1, depth, r, true);
            roots.push_back (ro);
        }
        pos.assign (br.size(), { 0, 0, 0 });
    }
    void setColours (juce::Colour dark, juce::Colour light)
    {
        for (int i = 0; i < 256; ++i)
        {
            const float t = (float) i / 255.0f;
            lut[(size_t) i] = dark.interpolatedWith (light, t);
        }
    }
    /** time in seconds, level 0..1 (output loudness), flash 0..1 (a hit) */
    const juce::Image& render (float time, float level, float flash)
    {
        std::fill (acc.begin(), acc.end(), 0.0f);
        const float half = (float) W * 0.5f;
        // background glow, brighter with the level
        const float gcx = half, gcy = (float) H * 0.42f, gr = (float) H * 0.62f;
        const float glow = 0.42f + 0.25f * level;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < (int) half + 1; ++x)
            {
                const float d = std::hypot ((float) x - gcx, (float) y - gcy) / gr;
                if (d < 1.0f) acc[(size_t) (y * W + x)] = glow * (1.0f - d) * (1.0f - d);
            }
        const float sway = 0.018f + 0.10f * level;
        const float leafBoost = 1.0f + 1.6f * flash;
        for (size_t ri = 0; ri < roots.size(); ++ri)
        {
            const Root& ro = roots[ri];
            // the branches are stored depth-first; walk them with their parents' end points
            const int end = ri + 1 < roots.size() ? roots[ri + 1].first : (int) br.size();
            for (int i = ro.first; i < end; ++i)
            {
                const Br& b = br[(size_t) i];
                float px, py, pa, plen;
                if (b.parent < 0) { px = ro.x; py = ro.y; pa = ro.a; plen = ro.len; }
                else { const auto& P = pos[(size_t) b.parent]; px = P.x; py = P.y; pa = P.a; plen = br[(size_t) b.parent].len * 0; plen = P.len; }
                const float depthFromTip = (float) b.depth;           // high = trunk
                const float wind = std::sin (time * (0.55f + 0.07f * (float) (i % 5)) + b.phase) * sway * (1.6f - 0.12f * depthFromTip)
                                 + std::sin (time * 1.9f + b.phase * 2.0f) * sway * 0.25f;
                const float a = pa + b.da + wind;
                const float len = (b.parent < 0 ? ro.len : plen * b.ratio);
                const float x2 = px + std::cos (a) * len, y2 = py + std::sin (a) * len;
                pos[(size_t) i] = { x2, y2, a, len };
                line (px, py, x2, y2, 0.45f * depthFromTip * 0.5f + 0.4f, 0.16f + depthFromTip * 0.045f);
                // leaves: soft dots around the branch end, shimmering
                for (int k = 0; k < b.leaves; ++k)
                {
                    const float ox = b.lx[k] * len, oy = b.ly[k] * len;
                    const float sh = 0.75f + 0.25f * std::sin (time * 2.3f + b.phase * 7.0f + (float) k);
                    dot (x2 + ox, y2 + oy, b.lr[k], b.li[k] * sh * leafBoost);
                }
            }
        }
        blur();
        // mirror + duotone
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const int sx = x < W / 2 ? x : W - 1 - x;
                const float v = clampf (tmp[(size_t) (y * W + sx)], 0.0f, 1.0f);
                bd.setPixelColour (x, y, lut[(size_t) (v * 255.0f)]);
            }
        return img;
    }

private:
    struct Br { int parent; int depth; float da, ratio, phase; float len; int leaves; float lx[6], ly[6], lr[6], li[6]; };
    struct Root { float x, y, a, len; int first; };
    struct Pos { float x, y, a; float len = 0; };
    std::vector<Br> br; std::vector<Root> roots; std::vector<Pos> pos;
    std::vector<float> acc, tmp;
    juce::Image img;
    std::array<juce::Colour, 256> lut {};
    int W = 64, H = 64;

    void grow (int parent, int depth, Rng& r, bool trunk)
    {
        if (depth <= 0) return;
        Br b {};
        b.parent = parent; b.depth = depth;
        b.da = trunk ? 0.0f : (r.uni() < 0.5f ? -(0.35f + r.uni() * 0.4f) : (0.25f + r.uni() * 0.4f));
        b.ratio = trunk ? 1.0f : 0.62f + r.uni() * 0.16f;
        b.phase = r.uni() * 6.283f;
        b.leaves = depth <= 5 ? 4 + (int) (r.uni() * 2.0f) : 1;
        for (int k = 0; k < 6; ++k) { b.lx[k] = r.bi() * 0.6f; b.ly[k] = r.bi() * 0.6f; b.lr[k] = 0.5f + r.uni() * 1.6f; b.li[k] = 0.05f + r.uni() * 0.16f; }
        const int me = (int) br.size();
        br.push_back (b);
        // two children: one leaning left, one right (stored depth-first, so parents come first)
        const int saveIdx = (int) br.size();
        juce::ignoreUnused (saveIdx);
        growChild (me, depth - 1, r, -1);
        growChild (me, depth - 1, r, +1);
    }
    void growChild (int parent, int depth, Rng& r, int side)
    {
        if (depth <= 0) return;
        Br b {};
        b.parent = parent; b.depth = depth;
        b.da = side < 0 ? -(0.35f + r.uni() * 0.4f) : (0.25f + r.uni() * 0.4f);
        b.ratio = side < 0 ? 0.68f + r.uni() * 0.12f : 0.62f + r.uni() * 0.12f;
        b.phase = r.uni() * 6.283f;
        b.leaves = depth <= 5 ? 4 + (int) (r.uni() * 2.0f) : 1;
        for (int k = 0; k < 6; ++k) { b.lx[k] = r.bi() * 0.6f; b.ly[k] = r.bi() * 0.6f; b.lr[k] = 0.5f + r.uni() * 1.6f; b.li[k] = 0.05f + r.uni() * 0.16f; }
        const int me = (int) br.size();
        br.push_back (b);
        growChild (me, depth - 1, r, -1);
        growChild (me, depth - 1, r, +1);
    }
    void add (int x, int y, float v)
    {
        if (x < 0 || y < 0 || x > W / 2 || y >= H) return;    // left half only; mirrored later
        float& a = acc[(size_t) (y * W + x)];
        a = a + v * (1.0f - a * 0.6f);
    }
    void line (float x0, float y0, float x1, float y1, float width, float v)
    {
        const float dx = x1 - x0, dy = y1 - y0, len = std::max (1.0f, std::hypot (dx, dy));
        const int steps = (int) len;
        const int rad = (int) std::ceil (width);
        for (int s = 0; s <= steps; ++s)
        {
            const float t = (float) s / (float) steps;
            const float cx = x0 + dx * t, cy = y0 + dy * t;
            for (int oy = -rad; oy <= rad; ++oy)
                for (int ox = -rad; ox <= rad; ++ox)
                {
                    const float d = std::hypot ((float) ox, (float) oy);
                    if (d <= width) add ((int) (cx + (float) ox), (int) (cy + (float) oy), v * 0.35f * (1.0f - d / (width + 0.5f)));
                }
        }
    }
    void dot (float cx, float cy, float rad, float v)
    {
        const int R = (int) std::ceil (rad);
        for (int oy = -R; oy <= R; ++oy)
            for (int ox = -R; ox <= R; ++ox)
            {
                const float d = std::hypot ((float) ox, (float) oy);
                if (d <= rad) add ((int) (cx + (float) ox), (int) (cy + (float) oy), v * (1.0f - d / (rad + 0.5f)));
            }
    }
    void blur()
    {
        // one 3x3 box pass on the left half (+1 column for the seam)
        const int hw = W / 2 + 1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < hw; ++x)
            {
                float s = 0; int n = 0;
                for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        const int xx = x + ox, yy = y + oy;
                        if (xx < 0 || yy < 0 || xx >= hw || yy >= H) continue;
                        s += acc[(size_t) (yy * W + xx)]; ++n;
                    }
                tmp[(size_t) (y * W + x)] = s / (float) n;
            }
    }
};
} // namespace fal

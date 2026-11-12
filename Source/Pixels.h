// Pixel-level drawing shared by the module display and the plug-in window.
#pragma once
#include <juce_graphics/juce_graphics.h>
#include "ExFont.h"

namespace ecr::pix
{
namespace col
{
    const juce::Colour white  { 0xfff2f2f2 };
    const juce::Colour wave   { 0xffb9b9b9 };
    const juce::Colour blue   { 0xff3d8bff };   // convolver A, envelope, files with a patch
    const juce::Colour orange { 0xffff7a1c };   // convolver B
    const juce::Colour red    { 0xffff2a1c };
    const juce::Colour black  { 0xff000000 };
}

inline void px (juce::Image::BitmapData& bm, int x, int y, juce::Colour c)
{
    if (x >= 0 && y >= 0 && x < bm.width && y < bm.height) bm.setPixelColour (x, y, c);
}

inline void fill (juce::Image::BitmapData& bm, int x, int y, int w, int h, juce::Colour c)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) px (bm, i, j, c);
}

// 5x7 glyphs in an 8-pixel-high cell; `advance` sets the spacing (7 = main screen, 8 = lists)
inline void text (juce::Image::BitmapData& bm, int x, int y, const juce::String& s, juce::Colour c, int advance = 8)
{
    const int inset = (advance - 5) / 2;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto ch = (unsigned) s[i];
        if (ch >= 32 && ch < 127)
            for (int row = 0; row < 7; ++row)
            {
                const auto bits = ex::kFont5x7[ch - 32][row];
                for (int b = 0; b < 5; ++b)
                    if (bits & (0x10 >> b)) px (bm, x + inset + b, y + 1 + row, c);
            }
        x += advance;
    }
}

inline void line (juce::Image::BitmapData& bm, int x0, int y0, int x1, int y1, juce::Colour c)
{
    const int dx = std::abs (x1 - x0), dy = -std::abs (y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4000; ++guard)
    {
        px (bm, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

inline void arrow (juce::Image::BitmapData& bm, int x, int y, bool up, juce::Colour c)
{
    for (int r = 0; r < 3; ++r)
        for (int i = -r; i <= r; ++i) px (bm, x + 3 + i, up ? y + r : y + 7 - r, c);
    for (int r = 3; r < 8; ++r) px (bm, x + 3, up ? y + r : y + 7 - r, c);
}

inline void frame (juce::Image::BitmapData& bm, int x, int y, int w, int h, juce::Colour c)
{
    fill (bm, x, y, w, 1, c); fill (bm, x, y + h - 1, w, 1, c);
    fill (bm, x, y, 1, h, c); fill (bm, x + w - 1, y, 1, h, c);
}
} // namespace ecr::pix

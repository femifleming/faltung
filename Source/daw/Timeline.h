#pragma once
// faltung sndstudio — the timeline's arithmetic: grid divisions, the adaptive grid, snapping (to the grid and to
// nearby edges), and positions written and read as bars.beats.sixteenths.ticks or as time. The studio counts in 4/4;
// a beat has 960 ticks (240 to a sixteenth). Pure functions, so the tests can check them directly.
#include <juce_core/juce_core.h>
#include <cmath>
#include <functional>
#include <vector>

namespace fal::daw::timeline
{
constexpr int kTicksPerBeat = 960;
constexpr int kTicksPer16th = 240;
constexpr int kBeatsPerBar = 4;

struct GridDef { const char* name; double beats; };   // beats 0: adaptive (follows the zoom)

/** the grid divisions to choose from: straight, triplet, dotted, quintuplet and septuplet */
inline const std::vector<GridDef>& grids()
{
    static const std::vector<GridDef> g {
        { "adaptive", 0.0 },
        { "bar", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 }, { "1/64", 0.0625 }, { "1/128", 0.03125 },
        { "1/2 triplet", 4.0 / 3.0 }, { "1/4 triplet", 2.0 / 3.0 }, { "1/8 triplet", 1.0 / 3.0 }, { "1/16 triplet", 1.0 / 6.0 }, { "1/32 triplet", 1.0 / 12.0 },
        { "1/4 dotted", 1.5 }, { "1/8 dotted", 0.75 }, { "1/16 dotted", 0.375 },
        { "1/16 quintuplet", 0.2 }, { "1/16 septuplet", 1.0 / 7.0 },
    };
    return g;
}

/** the adaptive grid: the finest power-of-two division (a 1/512 note at most, 64 bars at least) whose lines are at
    least minPx apart */
inline double adaptiveBeats (double pxPerBeat, double minPx = 14.0)
{
    double b = 4.0;
    while (b * pxPerBeat < minPx && b < 256.0) b *= 2.0;
    while ((b * 0.5) * pxPerBeat >= minPx && b * 0.5 >= 1.0 / 128.0) b *= 0.5;
    return b;
}

/** the grid in beats for a choice from grids() */
inline double gridBeats (int index, double pxPerBeat)
{
    const auto& g = grids();
    if (index <= 0 || index >= (int) g.size()) return adaptiveBeats (pxPerBeat);
    return g[(size_t) index].beats;
}

inline juce::String gridName (int index, double pxPerBeat)
{
    const auto& g = grids();
    if (index > 0 && index < (int) g.size()) return g[(size_t) index].name;
    const double b = adaptiveBeats (pxPerBeat);
    if (b >= 4.0) return "adaptive (" + juce::String ((int) std::lround (b / 4.0)) + (b > 4.0 ? " bars)" : " bar)");
    return "adaptive (1/" + juce::String ((int) std::lround (4.0 / b)) + ")";
}

/** the nearest grid line (never below zero) */
inline double snap (double beat, double grid)
{
    if (grid <= 0) return std::max (0.0, beat);
    return std::max (0.0, std::round (beat / grid) * grid);
}

/** the target nearest to beat within radiusPx (the magnet: clip edges, markers, the loop, the playhead); else the
    grid. hitTarget says which one won */
inline double magnet (double beat, double grid, const std::vector<double>& targets, double pxPerBeat, double radiusPx, bool* hitTarget = nullptr)
{
    double best = -1, bestPx = radiusPx;
    for (double t : targets) { const double d = std::abs (t - beat) * pxPerBeat; if (d <= bestPx) { bestPx = d; best = t; } }
    if (hitTarget) *hitTarget = best >= 0;
    return best >= 0 ? std::max (0.0, best) : snap (beat, grid);
}

// ---- positions ------------------------------------------------------------------------------------------------
inline long long toTicks (double beats) { return std::llround (beats * kTicksPerBeat); }

/** a position: "17.2.3.120" (bar 17, beat 2, sixteenth 3, tick 120), counted from 1 like every daw */
inline juce::String formatPosition (double beats)
{
    const long long t = std::max (0LL, toTicks (beats));
    const long long perBar = (long long) kBeatsPerBar * kTicksPerBeat;
    return juce::String (t / perBar + 1) + "." + juce::String ((t / kTicksPerBeat) % kBeatsPerBar + 1) + "."
         + juce::String ((t / kTicksPer16th) % 4 + 1) + "." + juce::String (t % kTicksPer16th);
}

/** a length: "2.1.0.0" (two bars, a beat), counted from 0 */
inline juce::String formatLength (double beats)
{
    const long long t = std::max (0LL, toTicks (beats));
    const long long perBar = (long long) kBeatsPerBar * kTicksPerBeat;
    return juce::String (t / perBar) + "." + juce::String ((t / kTicksPerBeat) % kBeatsPerBar) + "."
         + juce::String ((t / kTicksPer16th) % 4) + "." + juce::String (t % kTicksPer16th);
}

/** time: "1:05.250", or "1:02:05.250" past an hour */
inline juce::String formatTime (double seconds, bool millis = true)
{
    const long long ms = std::llround (std::max (0.0, seconds) * 1000.0);
    const long long s = ms / 1000, h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    juce::String out = h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m);
    out << ":" << juce::String (sec).paddedLeft ('0', 2);
    if (millis) out << "." << juce::String (ms % 1000).paddedLeft ('0', 3);
    return out;
}

/** reads what someone typed, in beats; -1 when it cannot be read. Positions: "17", "17.2", "17.2.3", "17.2.3.120"
    (dots or spaces), or time: "1:05.250", "1:02:05", "90s", "250ms" (secondsToBeats converts). Lengths (isLength)
    count from 0: "2" is two bars, "0.0.1" a sixteenth */
inline double parse (const juce::String& typed, bool isLength, const std::function<double (double)>& secondsToBeats)
{
    auto s = typed.trim().toLowerCase();
    if (s.isEmpty()) return -1;
    auto num = [] (const juce::String& x) { return x.containsOnly ("0123456789.") && x.isNotEmpty() && x.containsAnyOf ("0123456789"); };
    double secs = -1;
    if (s.endsWith ("ms") && num (s.dropLastCharacters (2).trim())) secs = s.dropLastCharacters (2).trim().getDoubleValue() / 1000.0;
    else if (s.endsWith ("s") && num (s.dropLastCharacters (1).trim())) secs = s.dropLastCharacters (1).trim().getDoubleValue();
    else if (s.contains (":"))
    {
        auto parts = juce::StringArray::fromTokens (s, ":", "");
        if (parts.size() < 2 || parts.size() > 3) return -1;
        for (auto& p : parts) if (! num (p.trim())) return -1;
        secs = 0; for (auto& p : parts) secs = secs * 60.0 + p.trim().getDoubleValue();
    }
    if (secs >= 0) return secondsToBeats ? secondsToBeats (secs) : -1;
    auto parts = juce::StringArray::fromTokens (s, ". ", ""); parts.removeEmptyStrings();
    if (parts.isEmpty() || parts.size() > 4) return -1;
    for (auto& p : parts) if (! p.containsOnly ("0123456789")) return -1;
    double v[4] = { isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, 0.0 };
    for (int i = 0; i < parts.size(); ++i) v[i] = parts[i].getDoubleValue();
    if (! isLength) { if (v[0] < 1 || v[1] < 1 || v[2] < 1) return -1; v[0] -= 1; v[1] -= 1; v[2] -= 1; }
    return v[0] * kBeatsPerBar + v[1] + v[2] * 0.25 + v[3] / kTicksPerBeat;
}
} // namespace fal::daw::timeline

#pragma once
// faltung sndstudio — the timeline's arithmetic: grid divisions, the adaptive grid, snapping (to the grid and to
// nearby edges), positions written and read as bars.beats.sixteenths.ticks or as time, and the meter map: time
// signatures (any numerator, whole or fractional or added up as 2+2+3, over any denominator, irrational ones too)
// changing at any bar, a change being one signature or a cycle of them (7/8 5/8 7/8 5/8 …).
// Underneath, time is counted in quarter notes ("beats", 960 ticks each; 240 to a sixteenth). The arrangement's bar
// values (startBar, loopStart …) are units of four quarter notes; the meter map says where the real bars fall.
// Pure functions, so the tests can check them directly.
#include <juce_core/juce_core.h>
#include <cmath>
#include <functional>
#include <vector>
#include <algorithm>

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

// ---- time signatures ----------------------------------------------------------------------------------------
struct Sig
{
    double num = 4; int den = 4;
    std::vector<int> groups;                       // 2+2+3: the beats in groups (accents, stronger lines); empty: plain
    double beats() const { return num * 4.0 / den; }        // the bar's length in quarter notes
    double beatLen() const { return 4.0 / den; }            // one beat (the denominator's note) in quarter notes
    int beatCount() const { return std::max (1, (int) std::ceil (num - 1.0e-9)); }
    bool groupStart (int beat) const { if (groups.empty()) return beat == 0; int a = 0; for (int gsz : groups) { if (beat == a) return true; a += gsz; } return false; }
    juce::String text() const
    {
        juce::String n;
        if (! groups.empty()) { juce::StringArray a; for (int gsz : groups) a.add (juce::String (gsz)); n = a.joinIntoString ("+"); }
        else n = std::abs (num - std::round (num)) < 1.0e-9 ? juce::String ((int) std::lround (num)) : juce::String (num, 3).trimCharactersAtEnd ("0");
        return n + "/" + juce::String (den);
    }
    bool operator== (const Sig& o) const { return std::abs (num - o.num) < 1.0e-9 && den == o.den && groups == o.groups; }
};

/** "7/8", "2+2+3/8", "3.5/4", "4/3": one signature; false when it cannot be read */
inline bool parseSig (const juce::String& t, Sig& out)
{
    auto parts = juce::StringArray::fromTokens (t.trim(), "/", "");
    if (parts.size() != 2) return false;
    const auto n = parts[0].trim(), d = parts[1].trim();
    if (! d.containsOnly ("0123456789") || d.isEmpty()) return false;
    Sig s; s.den = d.getIntValue();
    if (s.den < 1 || s.den > 128) return false;
    if (n.contains ("+"))
    {
        int total = 0;
        for (auto& g : juce::StringArray::fromTokens (n, "+", ""))
        {
            if (! g.trim().containsOnly ("0123456789") || g.trim().isEmpty()) return false;
            const int v = g.trim().getIntValue(); if (v < 1) return false;
            s.groups.push_back (v); total += v;
        }
        s.num = total;
    }
    else
    {
        if (! n.containsOnly ("0123456789.") || ! n.containsAnyOf ("0123456789")) return false;
        s.num = n.getDoubleValue();
    }
    if (s.num <= 0 || s.num > 128 || s.beats() < 1.0 / 64.0) return false;
    out = s; return true;
}
/** "7/8 5/8" (a cycle), separated by spaces or commas */
inline bool parseSigs (const juce::String& t, std::vector<Sig>& out)
{
    std::vector<Sig> v;
    for (auto& tok : juce::StringArray::fromTokens (t, " ,;", "")) { if (tok.trim().isEmpty()) continue; Sig s; if (! parseSig (tok, s)) return false; v.push_back (s); }
    if (v.empty()) return false;
    out = v; return true;
}
inline juce::String sigsText (const std::vector<Sig>& v) { juce::StringArray a; for (auto& s : v) a.add (s.text()); return a.joinIntoString (" "); }

/** from this bar (counted from 0) on, the signatures in turn, until the next change */
struct MeterChange { int bar = 0; std::vector<Sig> sigs { Sig() }; };

class MeterMap
{
public:
    MeterMap() { set ({}); }
    explicit MeterMap (std::vector<MeterChange> c) { set (std::move (c)); }
    /** the changes (sorted, merged; bar 0 is 4/4 unless one says otherwise) */
    void set (std::vector<MeterChange> c)
    {
        c.erase (std::remove_if (c.begin(), c.end(), [] (const MeterChange& m) { return m.sigs.empty() || m.bar < 0; }), c.end());
        std::stable_sort (c.begin(), c.end(), [] (const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
        std::vector<MeterChange> u;
        for (auto& m : c) { if (! u.empty() && u.back().bar == m.bar) u.back() = m; else u.push_back (m); }
        if (u.empty() || u.front().bar != 0) u.insert (u.begin(), MeterChange());
        changes = u;
        segs.clear();
        for (auto& m : changes)
        {
            Seg sg; sg.bar = m.bar; sg.sigs = m.sigs; sg.cycle = 0; for (auto& x : m.sigs) sg.cycle += x.beats();
            sg.beat = segs.empty() ? 0.0 : startIn (segs.back(), m.bar);
            segs.push_back (sg);
        }
    }
    const std::vector<MeterChange>& getChanges() const { return changes; }
    bool plain() const { return changes.size() == 1 && changes[0].sigs.size() == 1 && changes[0].sigs[0] == Sig(); }

    /** where a bar starts, in quarter notes */
    double barStart (long long bar) const { if (bar <= 0) return bar < 0 ? (double) bar * 4.0 : 0.0; return startIn (segFor (bar), bar); }
    /** the bar a position is in (and where that bar starts) */
    long long barAt (double beat, double* start = nullptr) const
    {
        if (beat < 0) { if (start) *start = std::floor (beat / 4.0) * 4.0; return (long long) std::floor (beat / 4.0); }
        const Seg* sg = &segs.front();
        for (auto& x : segs) if (x.beat <= beat + 1.0e-9) sg = &x;
        const double rel = beat - sg->beat;
        const long long full = sg->cycle > 0 ? (long long) std::floor (rel / sg->cycle + 1.0e-12) : 0;
        long long b = sg->bar + full * (long long) sg->sigs.size(); double pos = sg->beat + (double) full * sg->cycle;
        for (size_t j = 0; j < sg->sigs.size(); ++j)
        {
            const double len = sg->sigs[j].beats();
            if (pos + len > beat + 1.0e-9) break;
            pos += len; ++b;
        }
        if (start) *start = pos;
        return b;
    }
    const Sig& sigOf (long long bar) const { const auto& sg = segFor (std::max (0LL, bar)); const long long k = std::max (0LL, bar) - sg.bar; return sg.sigs[(size_t) (k % (long long) sg.sigs.size())]; }
    const Sig& sigAt (double beat) const { return sigOf (barAt (beat)); }
    /** the first beat line (the denominator's note, restarting at every bar line) after a position. kind: 2 the bar,
        1 a group's first beat (2+2+3), 0 another beat */
    double nextBeat (double beat, int* kind = nullptr) const
    {
        double s; const long long bar = barAt (beat, &s);
        const Sig& sg = sigOf (bar); const double bl = sg.beatLen(), e = barStart (bar + 1);
        const long long k = (long long) std::floor ((beat - s) / bl + 1.0e-9) + 1;
        const double t = s + (double) k * bl;
        if (t >= e - 1.0e-9) { if (kind) *kind = 2; return e; }
        if (kind) *kind = sg.groupStart ((int) k) ? 1 : 0;
        return t;
    }
    /** the first bar line after a position */
    double nextBarLine (double beat) const { return barStart (barAt (beat) + 1); }
    /** bars (whole and part) from one position to another */
    double barsBetween (double a, double b) const
    {
        double sa, sb; const long long ba = barAt (a, &sa), bb = barAt (b, &sb);
        auto frac = [this] (long long bar, double s, double x) { return (x - s) / std::max (1.0e-9, sigOf (bar).beats()); };
        return (double) (bb - ba) + frac (bb, sb, b) - frac (ba, sa, a);
    }

private:
    struct Seg { long long bar = 0; double beat = 0, cycle = 4; std::vector<Sig> sigs; };
    const Seg& segFor (long long bar) const { const Seg* sg = &segs.front(); for (auto& x : segs) if (x.bar <= bar) sg = &x; return *sg; }
    static double startIn (const Seg& sg, long long bar)
    {
        const long long k = bar - sg.bar, n = (long long) sg.sigs.size();
        double b = sg.beat + (double) (k / n) * sg.cycle;
        for (long long j = 0; j < k % n; ++j) b += sg.sigs[(size_t) j].beats();
        return b;
    }
    std::vector<MeterChange> changes;
    std::vector<Seg> segs;
};

/** 4/4 throughout (what a project without changes has) */
inline const MeterMap& plainMeter() { static const MeterMap m; return m; }

/** a position in a meter: bar.beat.sixteenth.tick, the beat being the denominator's note (an eighth in 7/8) */
inline juce::String formatPosition (double beats, const MeterMap& m)
{
    beats = std::max (0.0, beats);
    double s; const long long bar = m.barAt (beats, &s);
    const Sig& sg = m.sigOf (bar);
    const long long within = std::llround ((beats - s) * kTicksPerBeat);
    const double beatTicks = sg.beatLen() * kTicksPerBeat;
    long long bi = (long long) std::floor ((double) within / beatTicks + 1.0e-9);
    bi = std::min<long long> (bi, sg.beatCount() - 1);
    const long long rem = within - std::llround ((double) bi * beatTicks);
    const long long six = beatTicks >= kTicksPer16th - 1.0e-9 ? std::max (0LL, rem) / kTicksPer16th : 0, tick = std::max (0LL, rem - six * kTicksPer16th);
    return juce::String (bar + 1) + "." + juce::String (bi + 1) + "." + juce::String (six + 1) + "." + juce::String (tick);
}
/** a length from a position: bars.beats.sixteenths.ticks, counted from 0 */
inline juce::String formatLength (double from, double beats, const MeterMap& m)
{
    double s; const long long b0 = m.barAt (from, &s);
    // whole bars while they fit, then the rest in the beats of the bar it ends in
    long long bars = 0; double at = from;
    while (bars < 100000 && m.barStart (b0 + bars + 1) + (from - s) <= from + beats + 1.0e-9) { ++bars; at = m.barStart (b0 + bars) + (from - s); }
    const double rest = std::max (0.0, from + beats - at);
    const Sig& sg = m.sigAt (at);
    const long long t = std::llround (rest * kTicksPerBeat);
    const double beatTicks = sg.beatLen() * kTicksPerBeat;
    const long long bi = (long long) std::floor ((double) t / beatTicks + 1.0e-9), r = t - std::llround ((double) bi * beatTicks);
    const long long six = beatTicks >= kTicksPer16th - 1.0e-9 ? std::max (0LL, r) / kTicksPer16th : 0;
    return juce::String (bars) + "." + juce::String (bi) + "." + juce::String (six) + "." + juce::String (std::max (0LL, r - six * kTicksPer16th));
}
/** reads a position (or, isLength, a length from `from`) in a meter; -1 when it cannot be read. Times as parse() */
inline double parse (const juce::String& typed, bool isLength, const std::function<double (double)>& secondsToBeats, const MeterMap& m, double from = 0)
{
    auto s = typed.trim().toLowerCase();
    if (s.isEmpty()) return -1;
    if (s.contains (":") || s.endsWith ("s")) return parse (typed, isLength, secondsToBeats);
    auto parts = juce::StringArray::fromTokens (s, ". ", ""); parts.removeEmptyStrings();
    if (parts.isEmpty() || parts.size() > 4) return -1;
    for (auto& p : parts) if (! p.containsOnly ("0123456789")) return -1;
    double v[4] = { isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, isLength ? 0.0 : 1.0, 0.0 };
    for (int i = 0; i < parts.size(); ++i) v[i] = parts[i].getDoubleValue();
    if (! isLength)
    {
        if (v[0] < 1 || v[1] < 1 || v[2] < 1) return -1;
        const long long bar = (long long) v[0] - 1;
        return m.barStart (bar) + (v[1] - 1) * m.sigOf (bar).beatLen() + (v[2] - 1) * 0.25 + v[3] / kTicksPerBeat;
    }
    double s0; const long long b0 = m.barAt (from, &s0);
    const double at = m.barStart (b0 + (long long) v[0]) + (from - s0);
    return at + v[1] * m.sigAt (at).beatLen() + v[2] * 0.25 + v[3] / kTicksPerBeat - from;
}
/** the nearest grid line, the grid starting again at every bar line (a 1/4 grid in 7/8 ends on the bar, not past
    it). gridBeats >= 4: whole bars, every round (gridBeats / 4)th */
inline double snap (double beat, double gridBeats, const MeterMap& m)
{
    beat = std::max (0.0, beat);
    double s; const long long bar = m.barAt (beat, &s);
    const double e = m.barStart (bar + 1);
    if (gridBeats >= 4.0 - 1.0e-9)
    {
        const long long k = std::max (1LL, (long long) std::llround (gridBeats / 4.0));
        const long long lo = (bar / k) * k, hi = lo + k;
        const double a = m.barStart (lo), b = m.barStart (hi);
        return beat - a <= b - beat ? a : b;
    }
    if (gridBeats <= 0) return beat;
    const double g = std::min (std::round ((beat - s) / gridBeats) * gridBeats + s, e);
    return std::abs (e - beat) < std::abs (g - beat) ? e : g;
}
/** the next / previous grid line from a position (the grid restarting at every bar line; >= 4 beats: whole bars) */
inline double nextGridLine (double beat, double gridBeats, const MeterMap& m)
{
    double s; const long long bar = m.barAt (beat + 1.0e-6, &s);
    const double e = m.barStart (bar + 1);
    if (gridBeats >= 4.0 - 1.0e-9) { const long long k = std::max (1LL, (long long) std::llround (gridBeats / 4.0)); return m.barStart ((bar / k + 1) * k); }
    return std::min (e, s + (std::floor ((beat - s) / gridBeats + 1.0e-6) + 1.0) * gridBeats);
}
inline double prevGridLine (double beat, double gridBeats, const MeterMap& m)
{
    if (beat <= 1.0e-9) return 0.0;
    double s; long long bar = m.barAt (beat - 1.0e-6, &s);   // (well inside: barAt's own tolerance is 1e-9)
    if (gridBeats >= 4.0 - 1.0e-9) { const long long k = std::max (1LL, (long long) std::llround (gridBeats / 4.0)); const long long b = (bar / k) * k; return m.barStart (m.barStart (b) < beat - 1.0e-9 ? b : std::max (0LL, b - k)); }
    return s + std::floor ((beat - s - 1.0e-6) / gridBeats) * gridBeats;
}
} // namespace fal::daw::timeline

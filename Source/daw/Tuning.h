#pragma once
// faltung sndstudio — scales and tunings for the pitch lane. A track with a scale reads each step's pitch as a degree of
// that scale (0 the root, 7 the octave of a seven-note scale, -1 the degree below the root …). Built-in scales, or any
// Scala .scl file (the format the tuning archives use, Colundi sets included): degrees in cents or ratios, the last one
// the period. A degree between the keys reaches the instrument as a note and a pitch bend (±2 semitones).
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <cmath>

namespace fal::daw
{
struct Tuning
{
    juce::String name;
    std::vector<double> cents { 0.0 };   // the degrees within one period, cents[0] = 0
    double period = 1200.0;
    int size() const { return std::max (1, (int) cents.size()); }
    double centsOf (int degree) const
    {
        const int n = size();
        const int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        return oct * period + cents[(size_t) (degree - oct * n)];
    }
    /** the degree nearest to a pitch (cents above the root) */
    int nearestDegree (double c) const
    {
        const int n = size();
        int best = 0; double bestD = 1.0e18;
        const int oct = (int) std::floor (c / period);
        for (int o = oct - 1; o <= oct + 1; ++o)
            for (int k = 0; k < n; ++k) { const double d = std::abs (o * period + cents[(size_t) k] - c); if (d < bestD) { bestD = d; best = o * n + k; } }
        return best;
    }
    /** true when some degree falls between the keys of the piano */
    bool microtonal() const
    {
        for (auto c : cents) if (std::abs (c / 100.0 - std::round (c / 100.0)) > 0.005) return true;
        return std::abs (period - 1200.0) > 0.5;
    }
    juce::String text() const { juce::StringArray a; for (auto c : cents) a.add (juce::String (c, 3)); return a.joinIntoString (" "); }
};
using TuningPtr = std::shared_ptr<const Tuning>;

inline juce::StringArray builtinScaleNames()
{
    return { "major", "minor", "dorian", "phrygian", "lydian", "mixolydian", "harmonic minor", "major pentatonic", "minor pentatonic",
             "whole tone", "just major (5-limit)", "7-limit (after the well-tuned piano)", "harmonic series 8 - 15" };
}
inline TuningPtr builtinScale (int i)
{
    auto semis = [] (std::initializer_list<int> s) { std::vector<double> v; for (int x : s) v.push_back (x * 100.0); return v; };
    auto ratios = [] (std::initializer_list<double> r) { std::vector<double> v; for (double x : r) v.push_back (1200.0 * std::log2 (x)); return v; };
    auto t = std::make_shared<Tuning>();
    t->name = builtinScaleNames()[i];
    switch (i)
    {
        case 0: t->cents = semis ({ 0, 2, 4, 5, 7, 9, 11 }); break;
        case 1: t->cents = semis ({ 0, 2, 3, 5, 7, 8, 10 }); break;
        case 2: t->cents = semis ({ 0, 2, 3, 5, 7, 9, 10 }); break;
        case 3: t->cents = semis ({ 0, 1, 3, 5, 7, 8, 10 }); break;
        case 4: t->cents = semis ({ 0, 2, 4, 6, 7, 9, 11 }); break;
        case 5: t->cents = semis ({ 0, 2, 4, 5, 7, 9, 10 }); break;
        case 6: t->cents = semis ({ 0, 2, 3, 5, 7, 8, 11 }); break;
        case 7: t->cents = semis ({ 0, 2, 4, 7, 9 }); break;
        case 8: t->cents = semis ({ 0, 3, 5, 7, 10 }); break;
        case 9: t->cents = semis ({ 0, 2, 4, 6, 8, 10 }); break;
        case 10: t->cents = ratios ({ 1.0, 9.0 / 8, 5.0 / 4, 4.0 / 3, 3.0 / 2, 5.0 / 3, 15.0 / 8 }); break;
        case 11: t->cents = ratios ({ 1.0, 9.0 / 8, 147.0 / 128, 21.0 / 16, 3.0 / 2, 7.0 / 4, 63.0 / 32 }); break;
        case 12: t->cents = ratios ({ 1.0, 9.0 / 8, 10.0 / 8, 11.0 / 8, 12.0 / 8, 13.0 / 8, 14.0 / 8, 15.0 / 8 }); break;
        default: return nullptr;
    }
    return t;
}

/** a Scala file: a description line, the number of notes, then one pitch per line (a dot: cents; otherwise a ratio
    a/b or a whole number); "!" lines are comments. The last pitch is the period. */
inline TuningPtr parseScala (const juce::String& text, const juce::String& fallbackName, juce::String* error = nullptr)
{
    juce::StringArray lines;
    for (auto l : juce::StringArray::fromLines (text)) { l = l.trim(); if (! l.startsWith ("!")) lines.add (l); }
    auto fail = [&] (const juce::String& e) { if (error) *error = e; return TuningPtr(); };
    if (lines.size() < 2) return fail ("not a scala file");
    const juce::String desc = lines[0];
    const int count = lines[1].upToFirstOccurrenceOf (" ", false, false).getIntValue();
    if (count < 1 || lines.size() < 2 + count) return fail ("the scala file says " + juce::String (count) + " pitches but has " + juce::String (lines.size() - 2));
    std::vector<double> c { 0.0 };
    for (int i = 0; i < count; ++i)
    {
        const auto w = lines[2 + i].upToFirstOccurrenceOf (" ", false, false).trim();
        double cents;
        if (w.contains (".")) cents = w.getDoubleValue();
        else if (w.contains ("/")) { const double a = w.upToFirstOccurrenceOf ("/", false, false).getDoubleValue(), b = w.fromFirstOccurrenceOf ("/", false, false).getDoubleValue(); if (a <= 0 || b <= 0) return fail ("cannot read the pitch \"" + w + "\""); cents = 1200.0 * std::log2 (a / b); }
        else { const double a = w.getDoubleValue(); if (a <= 0) return fail ("cannot read the pitch \"" + w + "\""); cents = 1200.0 * std::log2 (a); }
        c.push_back (cents);
    }
    auto t = std::make_shared<Tuning>();
    t->period = c.back(); c.pop_back();
    if (t->period <= 0.0) return fail ("the period must be above the root");
    std::sort (c.begin(), c.end());
    t->cents = c;
    t->name = desc.isNotEmpty() ? desc : fallbackName;
    return t;
}
} // namespace fal::daw

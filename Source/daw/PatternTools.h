#pragma once
// faltung sndstudio — the pattern tools: what the links above the step grid do, as plain functions on a pattern
// (so a script or a test uses the very thing the page uses). Generative ones take the random source they draw from.
#include "Model.h"
#include "../dsp/Core.h"

namespace fal::daw::tools
{
inline void euclid (Pattern& p, int hits, int rot)
{
    const int n = p.length;
    for (int i = 0; i < n; ++i) { const int k = ((i - rot) % n + n) % n; const bool on = hits > 0 && ((k * hits) % n) < hits; p.s[(size_t) i].on = on; if (on) p.s[(size_t) i].vel = 0.8f; }
}
inline void rotate (Pattern& p, int by)
{
    const int n = p.length; if (n < 2) return;
    Pattern q = p; for (int i = 0; i < n; ++i) p.s[(size_t) (((i + by) % n + n) % n)] = q.s[(size_t) i];
}
inline void reverse (Pattern& p) { std::reverse (p.s.begin(), p.s.begin() + p.length); }
inline void mutate (Pattern& p, Rng& r, float amount = 0.12f)
{
    for (int i = 0; i < p.length; ++i) if (r.uni() < amount) { auto& s = p.s[(size_t) i]; s.on = ! s.on; if (s.on) s.vel = 0.5f + 0.5f * r.uni(); }
}
inline void humanise (Pattern& p, Rng& r)
{
    for (int i = 0; i < p.length; ++i) if (p.s[(size_t) i].on) { auto& s = p.s[(size_t) i]; s.micro = juce::jlimit (-1.0f, 1.0f, s.micro + (r.uni() - 0.5f) * 0.5f); s.vel = juce::jlimit (0.1f, 1.0f, s.vel + (r.uni() - 0.5f) * 0.2f); }
}
inline void randomise (Pattern& p, Rng& r, float density = 0.4f)
{
    for (int i = 0; i < p.length; ++i) { auto& s = p.s[(size_t) i]; s.on = r.uni() < density; s.vel = 0.4f + 0.6f * r.uni(); }
}
/** a two-state walk: a hit follows a hit with 0.6, a rest with 0.25; the velocities wander */
inline void markov (Pattern& p, Rng& r)
{
    bool prev = p.s[0].on; float v = 0.7f;
    for (int i = 0; i < p.length; ++i) { const bool on = r.uni() < (prev ? 0.6f : 0.25f); v = juce::jlimit (0.2f, 1.0f, v + (r.uni() - 0.5f) * 0.3f); p.s[(size_t) i].on = on; if (on) p.s[(size_t) i].vel = v; prev = on; }
}
/** a rewriting rule: two hits in a row ratchet; a hit before a rest echoes into it */
inline void rewrite (Pattern& p)
{
    Pattern q = p; const int n = p.length;
    for (int i = 0; i < n; ++i)
    {
        const auto& a = p.s[(size_t) i]; const auto& b = p.s[(size_t) ((i + 1) % n)]; auto& o = q.s[(size_t) i];
        if (a.on && b.on) { o.on = true; o.ratchet = std::min (8, a.ratchet + 1); }
        else if (a.on && ! b.on) { o = a; q.s[(size_t) ((i + 1) % n)].on = true; q.s[(size_t) ((i + 1) % n)].vel = a.vel * 0.6f; }
    }
    p = q;
}
inline void doubleLength (Pattern& p)
{
    const int n = p.length; if (n * 2 > kMaxSteps) return;
    for (int i = 0; i < n; ++i) p.s[(size_t) (n + i)] = p.s[(size_t) i];
    p.length = n * 2;
}

// ---- timing ratios and strict grids (snd, Mark Fell) ---------------------------------------------------------
/** "1 1 2/3 2/3 2/3 3/2": one step per entry, each lasting that many of the track's steps. The pattern takes that
    many steps; steps that were on stay on. False when the text holds no usable number. */
inline bool ratios (Pattern& p, const juce::String& text)
{
    std::vector<float> d;
    for (auto& tok : juce::StringArray::fromTokens (text, " ,;", ""))
    {
        if (tok.isEmpty()) continue;
        const double num = tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
        const double den = tok.contains ("/") ? tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue() : 1.0;
        if (num > 0 && den > 0) d.push_back (juce::jlimit (0.05f, 16.0f, (float) (num / den)));
    }
    if (d.empty()) return false;
    p.length = juce::jlimit (1, kMaxSteps, (int) d.size());
    for (int i = 0; i < p.length; ++i) p.s[(size_t) i].dur = d[(size_t) i];
    return true;
}
/** every step lasts `factor` times the one before; the loop keeps its length (one control stretches the pattern) */
inline void accelerate (Pattern& p, float factor)
{
    const int n = p.length; factor = juce::jlimit (0.5f, 2.0f, factor);
    double sum = 0, d = 1.0; std::vector<double> v ((size_t) n);
    for (int i = 0; i < n; ++i) { v[(size_t) i] = d; sum += d; d *= factor; }
    for (int i = 0; i < n; ++i) p.s[(size_t) i].dur = juce::jlimit (0.05f, 16.0f, (float) (v[(size_t) i] * n / sum));
}
inline void evenTime (Pattern& p) { for (auto& s : p.s) s.dur = 1.0f; }
/** velocities onto a grid of `levels` values (2: accent or not), note lengths all the same */
inline void strictGrid (Pattern& p, int levels, float noteLength)
{
    levels = juce::jlimit (1, 16, levels);
    for (int i = 0; i < p.length; ++i)
    {
        auto& s = p.s[(size_t) i];
        s.vel = levels == 1 ? 0.8f : juce::jlimit (1.0f / (float) levels, 1.0f, std::round (s.vel * (float) levels) / (float) levels);
        s.len = noteLength; s.micro = 0.0f;
    }
}

// ---- sieves and stochastic clouds (Xenakis) -------------------------------------------------------------------
/** a sieve: residue classes "modulus@residue" joined by | (union) and & (intersection), ! complements a class.
    "3@0 | 4@1" holds 0 1 3 5 6 9 … ; & binds tighter than |. Returns whether n belongs. `ok` is false on a text
    that could not be read. */
inline bool sieveHolds (const juce::String& expr, int n, bool* ok = nullptr)
{
    bool any = false, valid = false;
    for (auto& un : juce::StringArray::fromTokens (expr, "|+", ""))
    {
        bool all = true, had = false;
        for (auto term : juce::StringArray::fromTokens (un, "&", ""))
        {
            term = term.trim(); if (term.isEmpty()) continue;
            const bool neg = term.startsWith ("!"); if (neg) term = term.substring (1).trim();
            const int m = term.upToFirstOccurrenceOf ("@", false, false).getIntValue();
            const int r = term.contains ("@") ? term.fromFirstOccurrenceOf ("@", false, false).getIntValue() : 0;
            if (m <= 0) continue;
            had = true; valid = true;
            const bool in = ((n - r) % m + m) % m == 0;
            all = all && (neg ? ! in : in);
        }
        if (had && all) any = true;
    }
    if (ok) *ok = valid;
    return any;
}
/** the steps that belong to the sieve are on, the others off */
inline bool sieveRhythm (Pattern& p, const juce::String& expr)
{
    bool ok = false; sieveHolds (expr, 0, &ok); if (! ok) return false;
    for (int i = 0; i < p.length; ++i) { auto& s = p.s[(size_t) i]; const bool on = sieveHolds (expr, i); if (on && ! s.on) s.vel = 0.8f; s.on = on; }
    return true;
}
/** the steps that are on take their pitches from the sieve, read as a scale: the k-th hit plays its k-th member
    (in order, or drawn at random from the first `span` semitones) */
inline bool sievePitch (Pattern& p, const juce::String& expr, Rng* random = nullptr, int span = 24)
{
    bool ok = false; sieveHolds (expr, 0, &ok); if (! ok) return false;
    std::vector<int> scale; for (int n = 0; n < span; ++n) if (sieveHolds (expr, n)) scale.push_back (n);
    if (scale.empty()) return false;
    int k = 0;
    for (int i = 0; i < p.length; ++i) if (p.s[(size_t) i].on) p.s[(size_t) i].pitch = scale[(size_t) (random ? (int) (random->uni() * (float) scale.size()) % (int) scale.size() : k++ % (int) scale.size())];
    return true;
}
/** a cloud after the laws Xenakis used: the times between events are exponential (a Poisson stream at `density`
    events a step), the pitches move by intervals with a linear density over ± `range` semitones, the dynamics are
    gaussian. The loop keeps its length; every step is an event. */
inline void stochastic (Pattern& p, Rng& r, float density, int range)
{
    const int n = p.length; density = juce::jlimit (0.1f, 4.0f, density);
    std::vector<double> gap ((size_t) n); double sum = 0;
    for (int i = 0; i < n; ++i) { gap[(size_t) i] = juce::jlimit (0.08, 6.0, -std::log (std::max (1.0e-6, (double) r.uni())) / density); sum += gap[(size_t) i]; }
    int pitch = 0;
    for (int i = 0; i < n; ++i)
    {
        auto& s = p.s[(size_t) i];
        s.on = true; s.dur = juce::jlimit (0.05f, 16.0f, (float) (gap[(size_t) i] * n / sum));
        const float g = (r.uni() + r.uni() + r.uni() + r.uni() - 2.0f) * 0.5f;                 // near enough a bell
        s.vel = juce::jlimit (0.15f, 1.0f, 0.65f + 0.3f * g);
        pitch += juce::roundToInt ((float) range * (1.0f - std::sqrt (r.uni())) * (r.uni() < 0.5f ? -1.0f : 1.0f));   // interval law: density falls off linearly
        if (pitch > range || pitch < -range) pitch = pitch > 0 ? 2 * range - pitch : -2 * range - pitch;       // reflects at the edges
        s.pitch = juce::jlimit (-36, 36, pitch); s.len = std::min (1.0f, s.dur * 0.8f); s.micro = 0.0f; s.ratchet = 1;
    }
}
} // namespace fal::daw::tools

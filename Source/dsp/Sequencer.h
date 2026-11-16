#pragma once
// faltung — step sequencer shared by kt1 kit (8 tracks) and kt2 loop (1 track of slices).
// Sample-accurate against the host's ppq; per-step microtiming, probability, ratchets with a curve; polymeter;
// a clock curve over each loop (after AERIA OS / norns' clockabout).
#include "Core.h"
#include <array>
#include <string>
#include <sstream>

namespace fal
{
constexpr int kSteps = 16;
constexpr int kPatterns = 8;

struct Step
{
    bool on = false;
    float vel = 0.8f;    // 0..1
    float micro = 0.0f;  // -1..1 → ± micro range (ms)
    float prob = 1.0f;   // 0..1
    int ratchet = 1;     // 1..4
    int slice = 0;       // kt2 only
    bool rev = false;    // kt2 only
    int curve = 0;       // the ratchet's curve, -7..7 (as AERIA's CURV): > 0 "in", the hits crowd together toward
                         // the end of the step like a ball coming to rest; < 0 "out", they spread; 0 even
};

/** the ratchet's curve: each gap is the one before times a ratio (2^(-|curve|/6): 0.89 at 1, 0.45 at 7),
    and the row of hits still fits the step exactly. Writes `rat` hit times as fractions 0..1 of the step. */
inline void ratchetTimes (int rat, int curve, float* out)
{
    rat = std::max (1, std::min (4, rat));
    out[0] = 0.0f;
    if (rat == 1) return;
    const double ratio = curve == 0 ? 1.0 : std::exp2 (-std::abs (curve) / 6.0);
    const double g0 = ratio == 1.0 ? 1.0 / rat : (1.0 - ratio) / (1.0 - std::pow (ratio, rat));   // the first gap of a row that sums to 1
    double gap = curve < 0 ? g0 * std::pow (ratio, rat - 1) : g0;                                 // "out": the smallest gap first
    const double mul = curve < 0 ? 1.0 / ratio : ratio;
    double t = 0.0;
    for (int r = 1; r < rat; ++r) { t += gap; gap *= mul; out[r] = (float) std::min (0.999, t); }
}

/** the clock's curve (after norns' clockabout, as AERIA's CLK): over each loop of a track the steps speed up or
    slow down along a shape, and the loop still takes its plain time, so step 1 always lands on time. */
struct SeqCurve
{
    int shape = 0;        // 0 off, 1 accel, 2 decel, 3 arch, 4 bulge, 5 wave, 6 rand
    float amount = 0.0f;  // 0..1 (1: about ± 90 %)
    bool active (int len) const { return shape > 0 && amount > 0.001f && len >= 2; }
};
inline float curveWeight (const SeqCurve& c, int k, int len)
{
    if (! c.active (len)) return 1.0f;
    const float x = (2.0f * (float) k + 1.0f) / (2.0f * (float) len);   // 0..1 across the loop
    float g;
    switch (c.shape)
    {
        case 1: g = 1.0f - 2.0f * x; break;                              // accel: long steps first, short at the end
        case 2: g = 2.0f * x - 1.0f; break;                              // decel
        case 3: g = 2.0f * std::abs (2.0f * x - 1.0f) - 1.0f; break;     // arch: slow at both ends, fast in the middle
        case 4: g = 1.0f - 2.0f * std::abs (2.0f * x - 1.0f); break;     // bulge: fast ends, slow middle
        case 5: { const float tt = std::fmod (x * 4.0f, 4.0f); g = tt < 2.0f ? tt - 1.0f : 3.0f - tt; } break;   // wave: a push-pull a loop
        default: { uint32_t r = (uint32_t) (k + 1) * 2654435761u ^ 40503u; r ^= r >> 15; r *= 2246822519u; r ^= r >> 13; g = (float) (r % 8193u) / 4096.0f - 1.0f; } break;   // rand: a fixed random set
    }
    return std::max (0.1f, 1.0f + g * c.amount * 0.9f);
}
/** where step `si` of a `len`-step loop starts (in steps from the loop's start) and how long it lasts (in steps) */
inline void curvedStep (const SeqCurve& c, int si, int len, float& start, float& dur)
{
    if (! c.active (len)) { start = (float) si; dur = 1.0f; return; }
    float sum = 0, before = 0, mine = 1;
    for (int k = 0; k < len; ++k)
    {
        const float w = curveWeight (c, k, len);
        if (k < si) before += w;
        if (k == si) mine = w;
        sum += w;
    }
    start = before / sum * (float) len; dur = mine / sum * (float) len;
}
/** which step of a `len`-step loop is sounding at `ppq` (for the play position) */
inline int stepAtPpq (double ppq, int len, const SeqCurve& c = {})
{
    len = std::max (1, std::min (kSteps, len));
    const long long k = (long long) std::floor (ppq / 0.25);
    const int si = (int) (((k % len) + len) % len);
    if (! c.active (len)) return si;
    const double pos = ppq / 0.25 - (double) (k - si);   // 0..len into the loop
    for (int s = len - 1; s >= 0; --s) { float st, d; curvedStep (c, s, len, st, d); if (pos >= st) return s; }
    return 0;
}

template <int NT>
struct Pattern
{
    std::array<std::array<Step, kSteps>, NT> t;
};

template <int NT>
struct Bank
{
    std::array<Pattern<NT>, kPatterns> p;

    std::string toString() const
    {
        std::ostringstream o;
        o.precision (4);
        for (int pi = 0; pi < kPatterns; ++pi)
            for (int ti = 0; ti < NT; ++ti)
                for (int si = 0; si < kSteps; ++si)
                {
                    const Step& s = p[(size_t) pi].t[(size_t) ti][(size_t) si];
                    if (! s.on && s.micro == 0.0f && s.slice == 0 && s.curve == 0) continue;
                    o << pi << ' ' << ti << ' ' << si << ' ' << (int) s.on << ' ' << s.vel << ' ' << s.micro << ' '
                      << s.prob << ' ' << s.ratchet << ' ' << s.slice << ' ' << (int) s.rev << ' ' << s.curve << '\n';
                }
        return o.str();
    }
    void fromString (const std::string& str)
    {
        *this = Bank();
        std::istringstream in (str);
        std::string line;
        while (std::getline (in, line))
        {
            std::istringstream l (line);
            int pi, ti, si, on, rat, sl, rv, cv = 0; float vel, mic, pr;
            if (! (l >> pi >> ti >> si >> on >> vel >> mic >> pr >> rat >> sl >> rv)) continue;
            l >> cv;   // 1.1: the ratchet's curve (older states have no eleventh field)
            if (pi < 0 || pi >= kPatterns || ti < 0 || ti >= NT || si < 0 || si >= kSteps) continue;
            Step& s = p[(size_t) pi].t[(size_t) ti][(size_t) si];
            s.on = on != 0; s.vel = clampf (vel, 0, 1); s.micro = clampf (mic, -1, 1); s.prob = clampf (pr, 0, 1);
            s.ratchet = std::max (1, std::min (4, rat)); s.slice = std::max (0, std::min (63, sl)); s.rev = rv != 0;
            s.curve = std::max (-7, std::min (7, cv));
        }
    }
};

/** per-track settings read from parameters every block */
struct TrackCtl
{
    int length = 16;
    float prob = 1.0f;     // multiplies step probability
    float shiftMs = 0.0f;  // whole-track push/pull
    bool mute = false;
};

struct SeqEvent { int sample; int track; int step; float vel; };

/** Internal or host clock + scheduling. Call `run` once per block; it calls `fire` for each hit. */
template <int NT>
class Sequencer
{
public:
    void prepare (double sampleRate) { sr = sampleRate; internalPpq = 0; wasRunning = false; }
    double positionPpq() const { return lastPpq; }
    bool isRunning() const { return running; }
    int currentStep (int length) const { return stepAtPpq (lastPpq, length, lastCurve); }

    template <typename Fire>
    void run (const Pattern<NT>& pat, const std::array<TrackCtl, NT>& ctl, int nSamples,
              bool hostPlaying, bool hasPpq, double hostPpq, double bpm,
              bool internalRun, float swing /*0.5..0.75*/, float microMs, Rng& rng, Fire&& fire, const SeqCurve& curve = {})
    {
        bpm = std::max (20.0, bpm);
        const double spb = 60.0 / bpm * sr;            // samples per beat
        double ppq0;
        if (hostPlaying && hasPpq) { ppq0 = hostPpq; running = true; }
        else if (internalRun)
        {
            if (! wasRunning) internalPpq = -0.0001; // start just before 1
            ppq0 = internalPpq; internalPpq += nSamples / spb; running = true;
        }
        else { running = false; wasRunning = false; return; }
        wasRunning = internalRun && ! (hostPlaying && hasPpq);
        const double ppq1 = ppq0 + nSamples / spb;
        lastPpq = ppq0; lastCurve = curve;

        // a hit can move up to ~1 step early/late, and the clock curve moves a step by up to half a loop,
        // so scan a margin of a loop and two steps each side
        const long long k0 = (long long) std::floor (ppq0 / 0.25) - kSteps - 2;
        const long long k1 = (long long) std::floor (ppq1 / 0.25) + kSteps + 2;
        const double msToPpq = bpm / 60000.0;
        for (long long k = k0; k <= k1; ++k)
        {
            const bool odd = (k & 1) != 0;
            const double swingOff = odd ? (swing - 0.5) * 0.5 : 0.0;
            for (int ti = 0; ti < NT; ++ti)
            {
                const TrackCtl& c = ctl[(size_t) ti];
                if (c.mute) continue;
                const int len = std::max (1, std::min (kSteps, c.length));
                const int si = (int) (((k % len) + len) % len);
                const Step& s = pat.t[(size_t) ti][(size_t) si];
                if (! s.on) continue;
                float start, dur;
                curvedStep (curve, si, len, start, dur);          // the clock curve: where this step sits in its loop
                const double base = ((double) (k - si) + start) * 0.25 + swingOff + (s.micro * microMs + c.shiftMs) * msToPpq;
                const int rat = std::max (1, std::min (4, s.ratchet));
                float at[4]; ratchetTimes (rat, s.curve, at);
                for (int r = 0; r < rat; ++r)
                {
                    const double e = base + at[r] * dur * 0.25;
                    if (e < ppq0 || e >= ppq1) continue;
                    // probability is rolled at the moment the hit is due (once per hit)
                    if (rng.uni() > s.prob * c.prob) continue;
                    const int smp = std::min (nSamples - 1, std::max (0, (int) ((e - ppq0) * spb)));
                    const float v = s.vel * (r == 0 ? 1.0f : 0.8f - 0.1f * r);
                    fire (SeqEvent { smp, ti, si, v });
                }
            }
        }
    }

private:
    double sr = 48000, internalPpq = 0, lastPpq = 0;
    bool wasRunning = false, running = false;
    SeqCurve lastCurve;
};
} // namespace fal

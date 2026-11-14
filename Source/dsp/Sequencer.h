#pragma once
// faltung — step sequencer shared by kt1 kit (8 tracks) and kt2 loop (1 track of slices).
// Sample-accurate against the host's ppq; per-step microtiming, probability, ratchets; polymeter.
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
};

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
                    if (! s.on && s.micro == 0.0f && s.slice == 0) continue;
                    o << pi << ' ' << ti << ' ' << si << ' ' << (int) s.on << ' ' << s.vel << ' ' << s.micro << ' '
                      << s.prob << ' ' << s.ratchet << ' ' << s.slice << ' ' << (int) s.rev << '\n';
                }
        return o.str();
    }
    void fromString (const std::string& str)
    {
        *this = Bank();
        std::istringstream in (str);
        int pi, ti, si, on, rat, sl, rv; float vel, mic, pr;
        while (in >> pi >> ti >> si >> on >> vel >> mic >> pr >> rat >> sl >> rv)
        {
            if (pi < 0 || pi >= kPatterns || ti < 0 || ti >= NT || si < 0 || si >= kSteps) continue;
            Step& s = p[(size_t) pi].t[(size_t) ti][(size_t) si];
            s.on = on != 0; s.vel = clampf (vel, 0, 1); s.micro = clampf (mic, -1, 1); s.prob = clampf (pr, 0, 1);
            s.ratchet = std::max (1, std::min (4, rat)); s.slice = std::max (0, std::min (63, sl)); s.rev = rv != 0;
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
    int currentStep (int track, int length) const
    {
        const long long k = (long long) std::floor (lastPpq / 0.25);
        return (int) (((k % length) + length) % length);
    }

    template <typename Fire>
    void run (const Pattern<NT>& pat, const std::array<TrackCtl, NT>& ctl, int nSamples,
              bool hostPlaying, bool hasPpq, double hostPpq, double bpm,
              bool internalRun, float swing /*0.5..0.75*/, float microMs, Rng& rng, Fire&& fire)
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
        lastPpq = ppq0;

        // a hit can move up to ~1 step early/late, so scan a margin of two steps each side
        const long long k0 = (long long) std::floor (ppq0 / 0.25) - 2;
        const long long k1 = (long long) std::floor (ppq1 / 0.25) + 2;
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
                const double base = k * 0.25 + swingOff + (s.micro * microMs + c.shiftMs) * msToPpq;
                const int rat = std::max (1, s.ratchet);
                for (int r = 0; r < rat; ++r)
                {
                    const double e = base + r * (0.25 / rat);
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
};
} // namespace fal

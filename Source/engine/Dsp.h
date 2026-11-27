#pragma once
// umleitung — the signal processing: filters (the eqs), level, channels, dynamics, generators and the meters.
// Real-time safe: everything is sized in prepare(); process() never allocates.
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <complex>
#include <algorithm>
#include <vector>
#include <array>
#include <atomic>

namespace um::dsp
{
constexpr double kPi = 3.14159265358979323846;
inline float dbToGain (float db) { return db <= -120.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }

// ---- biquads (RBJ cookbook), transposed direct form II in double -------------------------------------------
struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
inline Coefs makeFilter (int shape, double fs, double f, double db, double q)
{
    f = std::clamp (f, 5.0, fs * 0.49); q = std::max (0.05, q);
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * f / fs, c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (shape)
    {
        case 0: b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;              // bell
        case 1: { const double r = 2 * std::sqrt (A) * al;                                                                     // low shelf
                  b0 = A * ((A + 1) - (A - 1) * c + r); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - r);
                  a0 = (A + 1) + (A - 1) * c + r; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - r; break; }
        case 2: { const double r = 2 * std::sqrt (A) * al;                                                                     // high shelf
                  b0 = A * ((A + 1) + (A - 1) * c + r); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - r);
                  a0 = (A + 1) - (A - 1) * c + r; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - r; break; }
        case 3: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;                   // low cut (high pass)
        case 4: b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;                      // high cut (low pass)
        case 5: b0 = 1; b1 = -2 * c; b2 = 1; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;                                          // notch
        case 6: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;                                             // band pass
        default: break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}
/** the magnitude of a filter at a frequency, in db (for drawing the curves) */
inline double magnitudeDb (const Coefs& k, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs;
    const std::complex<double> z = std::polar (1.0, -w), z2 = z * z;
    const auto h = (k.b0 + k.b1 * z + k.b2 * z2) / (1.0 + k.a1 * z + k.a2 * z2);
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}
struct Biquad
{
    Coefs k; double z1 = 0, z2 = 0;
    inline float tick (float x) { const double y = k.b0 * x + z1; z1 = k.b1 * x - k.a1 * y + z2; z2 = k.b2 * x - k.a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
};

/** a bank of biquads per channel; coefficients are swapped in at the start of a block (from the message thread) */
class FilterBank
{
public:
    static constexpr int kMax = 32;
    void prepare (int channels, double fs) { sr = fs; state.assign ((size_t) channels, {}); }
    /** message thread: new filters (shape, freq, db, q) */
    void set (const std::vector<std::array<double, 4>>& bands)
    {
        Pending p; p.n = std::min ((int) bands.size(), kMax);
        for (int i = 0; i < p.n; ++i) p.k[(size_t) i] = makeFilter ((int) bands[(size_t) i][0], sr, bands[(size_t) i][1], bands[(size_t) i][2], bands[(size_t) i][3]);
        const juce::SpinLock::ScopedLockType l (lock); pending = p; dirty = true;
    }
    double sampleRate() const { return sr; }
    void process (juce::AudioBuffer<float>& b, int n)
    {
        if (dirty.load()) { const juce::SpinLock::ScopedTryLockType l (lock); if (l.isLocked()) { active = pending; dirty = false; } }
        const int ch = std::min (b.getNumChannels(), (int) state.size());
        for (int c = 0; c < ch; ++c)
        {
            auto* x = b.getWritePointer (c); auto& st = state[(size_t) c];
            for (int f = 0; f < active.n; ++f)
            {
                auto& q = st[(size_t) f]; q.k = active.k[(size_t) f];
                for (int i = 0; i < n; ++i) x[i] = q.tick (x[i]);
            }
            for (int i = 0; i < n; ++i) if (! std::isfinite (x[i])) { for (auto& q : st) q.reset(); std::fill (x, x + n, 0.0f); break; }
        }
    }
private:
    struct Pending { std::array<Coefs, kMax> k; int n = 0; };
    Pending pending, active; juce::SpinLock lock; std::atomic<bool> dirty { false };
    std::vector<std::array<Biquad, kMax>> state; double sr = 48000;
};

/** the graphic eq's bands (constant-q bells an octave wide) */
inline std::vector<std::array<double, 4>> graphicFilters (const std::array<float, 10>& g, const std::array<float, 10>& freqs)
{
    std::vector<std::array<double, 4>> v;
    for (size_t i = 0; i < 10; ++i) if (std::abs (g[i]) > 0.01f) v.push_back ({ 0.0, (double) freqs[i], (double) g[i], 1.41 });
    return v;
}

// ---- level ----------------------------------------------------------------------------------------------------
/** a gain that glides (no zipper noise) */
struct SmoothGain
{
    float cur = 1, target = 1;
    void set (float g) { target = g; }
    void snap() { cur = target; }
    /** apply to one channel; the ramp ends at target over n samples (all channels use the same start: call next() once per block) */
    float next (int n) { const float a = cur; cur = target; (void) n; return a; }
};
inline void rampGain (float* x, int n, float g0, float g1) { if (g0 == g1) { if (g1 != 1.0f) for (int i = 0; i < n; ++i) x[i] *= g1; return; } const float d = (g1 - g0) / (float) std::max (1, n); float g = g0; for (int i = 0; i < n; ++i) { g += d; x[i] *= g; } }
/** equal-power balance for channel c of n: -1 left … +1 right (pairs: even left, odd right) */
inline float balanceGain (int c, int n, float bal)
{
    if (n < 2 || std::abs (bal) < 1.0e-4f) return 1.0f;
    const bool left = (c % 2) == 0;
    return left ? (bal > 0 ? std::cos (bal * (float) kPi * 0.5f) : 1.0f) : (bal < 0 ? std::cos (-bal * (float) kPi * 0.5f) : 1.0f);
}

// ---- dynamics ------------------------------------------------------------------------------------------------------
/** a look-ahead peak limiter linked across channels (never above the ceiling) */
struct Limiter
{
    int L = 64, nCh = 2, pos = 0; std::vector<float> delay, req, box; float env = 1, boxSum = 64, rel = 0.001f; int boxPos = 0; float gr = 1;
    void prepare (double sr, int ch) { L = std::max (8, (int) (0.0015 * sr)); nCh = std::max (1, ch); delay.assign ((size_t) (nCh * L), 0.0f); req.assign ((size_t) L, 1.0f); box.assign ((size_t) L, 1.0f); boxSum = (float) L; pos = boxPos = 0; env = 1; setRelease (80, sr); }
    void setRelease (float ms, double sr) { rel = 1.0f - std::exp (-1.0f / (std::max (1.0f, ms) * 0.001f * (float) sr)); }
    int latency() const { return L - 1; }
    void process (juce::AudioBuffer<float>& b, int n, float ceiling)
    {
        const int ch = std::min (b.getNumChannels(), nCh); float minG = 1;
        for (int i = 0; i < n; ++i)
        {
            float pk = 0; for (int c = 0; c < ch; ++c) pk = std::max (pk, std::abs (b.getSample (c, i)));
            req[(size_t) pos] = pk > ceiling ? ceiling / pk : 1.0f;
            float m = 1; for (float r : req) m = std::min (m, r);
            env = m < env ? m : env + (m - env) * rel;
            boxSum += env - box[(size_t) boxPos]; box[(size_t) boxPos] = env; boxPos = (boxPos + 1) % L;
            const float g = boxSum / (float) L; minG = std::min (minG, g);
            const int rd = (pos + 1) % L;
            for (int c = 0; c < ch; ++c)
            {
                float* d = &delay[(size_t) (c * L)];
                const float x = b.getSample (c, i);
                b.setSample (c, i, juce::jlimit (-ceiling, ceiling, d[rd] * g));
                d[pos] = x;
            }
            pos = (pos + 1) % L;
            if (boxPos == 0) { double s = 0; for (float v : box) s += v; boxSum = (float) s; }
        }
        gr = minG;
    }
};

/** the leveler: slow automatic gain toward a target loudness (rms over ~400 ms), at most maxDb up or down, with a
    peak limiter after it */
struct Leveler
{
    double sr = 48000; float msq = 0, gainDb = 0; Limiter lim;
    void prepare (double fs, int ch) { sr = fs; msq = 0; gainDb = 0; lim.prepare (fs, ch); }
    void process (juce::AudioBuffer<float>& b, int n, float targetDb, float maxDb)
    {
        const int ch = b.getNumChannels();
        const float a = 1.0f - std::exp (-1.0f / (0.4f * (float) sr));
        const float g0 = dbToGain (gainDb);
        for (int i = 0; i < n; ++i) { float s = 0; for (int c = 0; c < ch; ++c) s += b.getSample (c, i) * b.getSample (c, i); msq += (s / (float) std::max (1, ch) - msq) * a; }
        const float level = gainToDb (std::sqrt (msq) + 1.0e-9f);
        float want = level < -60.0f ? gainDb : juce::jlimit (-maxDb, maxDb, targetDb - level);   // silence: hold
        const float rate = (want > gainDb ? 3.0f : 9.0f) * (float) n / (float) sr;                   // db per block: up slowly, down faster
        gainDb += juce::jlimit (-rate, rate, want - gainDb);
        const float g1 = dbToGain (gainDb);
        for (int c = 0; c < ch; ++c) rampGain (b.getWritePointer (c), n, g0, g1);
        lim.process (b, n, dbToGain (-1.0f));
    }
};

/** a noise gate with hold and release */
struct Gate
{
    double sr = 48000; float env = 0, g = 0; int hold = 0;
    void prepare (double fs) { sr = fs; env = 0; g = 0; hold = 0; }
    void process (juce::AudioBuffer<float>& b, int n, float thrDb, float releaseMs)
    {
        const int ch = b.getNumChannels(); const float thr = dbToGain (thrDb);
        const float att = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr)), rel = 1.0f - std::exp (-1.0f / (std::max (1.0f, releaseMs) * 0.001f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            float pk = 0; for (int c = 0; c < ch; ++c) pk = std::max (pk, std::abs (b.getSample (c, i)));
            env = std::max (pk, env * 0.9995f);
            if (env > thr) hold = (int) (0.05 * sr);
            const float target = hold > 0 ? 1.0f : 0.0f; if (hold > 0) --hold;
            g += (target - g) * (target > g ? att : rel);
            for (int c = 0; c < ch; ++c) b.setSample (c, i, b.getSample (c, i) * g);
        }
    }
};

/** a delay line per channel, up to 5 s */
struct Delay
{
    std::vector<std::vector<float>> buf; int w = 0, len = 1;
    void prepare (double sr, int ch) { len = (int) (5.0 * sr) + 1; buf.assign ((size_t) ch, std::vector<float> ((size_t) len, 0.0f)); w = 0; }
    void process (juce::AudioBuffer<float>& b, int n, int delaySamples)
    {
        delaySamples = juce::jlimit (0, len - 1, delaySamples);
        const int ch = std::min (b.getNumChannels(), (int) buf.size());
        for (int i = 0; i < n; ++i)
        {
            const int r = (w - delaySamples + len) % len;
            for (int c = 0; c < ch; ++c) { auto& d = buf[(size_t) c]; d[(size_t) w] = b.getSample (c, i); b.setSample (c, i, d[(size_t) r]); }
            w = (w + 1) % len;
        }
    }
};

/** a test signal: sine, pink noise, white noise, a log sweep (20 hz - 20 khz in 10 s), or pulses each second */
struct Tone
{
    double sr = 48000, ph = 0, sweepT = 0; uint32_t rng = 0x1234567u; float b0 = 0, b1 = 0, b2 = 0; int pulse = 0;
    void prepare (double fs) { sr = fs; ph = 0; sweepT = 0; pulse = 0; }
    float white() { rng = rng * 1664525u + 1013904223u; return (float) ((int32_t) rng) / 2147483648.0f; }
    void process (juce::AudioBuffer<float>& b, int n, int shape, float freq, float db)
    {
        const float g = dbToGain (db); auto* x = b.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            float v = 0;
            switch (shape)
            {
                case 0: v = (float) std::sin (2.0 * kPi * ph); ph += freq / sr; if (ph >= 1) ph -= 1; break;
                case 1: { const float w = white(); b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f; v = (b0 + b1 + b2 + w * 0.1848f) * 0.2f; break; }
                case 2: v = white() * 0.5f; break;
                case 3: { const double f = 20.0 * std::pow (1000.0, sweepT / 10.0); v = (float) std::sin (2.0 * kPi * ph); ph += f / sr; if (ph >= 1) ph -= 1; sweepT += 1.0 / sr; if (sweepT >= 10.0) sweepT = 0; break; }
                default: v = pulse < (int) (0.002 * sr) ? 1.0f : 0.0f; if (++pulse >= (int) sr) pulse = 0; break;
            }
            x[i] = v * g;
        }
        for (int c = 1; c < b.getNumChannels(); ++c) b.copyFrom (c, 0, b, 0, 0, n);
    }
};
inline const char* toneShapeName (int s) { static const char* n[] = { "sine", "pink noise", "white noise", "sweep", "pulse" }; return n[juce::jlimit (0, 4, s)]; }

// ---- meters ------------------------------------------------------------------------------------------------------
/** per channel: peak and rms over the last block, read by the ui (lock free); up to 256 channels */
struct LevelTap
{
    static constexpr int kMax = 256;
    std::array<std::atomic<float>, kMax> peak {}, rms {};
    std::atomic<int> channels { 0 };
    std::atomic<long long> clips { 0 };
    void write (const juce::AudioBuffer<float>& b, int n)
    {
        const int ch = std::min (b.getNumChannels(), kMax);
        channels.store (ch, std::memory_order_relaxed);
        for (int c = 0; c < ch; ++c)
        {
            const float* x = b.getReadPointer (c); float pk = 0; double s = 0;
            for (int i = 0; i < n; ++i) { const float a = std::abs (x[i]); pk = std::max (pk, a); s += (double) x[i] * x[i]; }
            if (pk >= 1.0f) clips.fetch_add (1, std::memory_order_relaxed);
            // the ui takes the highest since it last looked (it resets with take)
            float old = peak[(size_t) c].load (std::memory_order_relaxed); while (pk > old && ! peak[(size_t) c].compare_exchange_weak (old, pk)) {}
            rms[(size_t) c].store ((float) std::sqrt (s / std::max (1, n)), std::memory_order_relaxed);
        }
    }
    float takePeak (int c) { return c < kMax ? peak[(size_t) c].exchange (0.0f) : 0.0f; }
};

/** loudness after ITU-R BS.1770 / EBU R128: k-weighting, momentary (400 ms), short-term (3 s), integrated (gated),
    true-ish peak (4x oversampled by interpolation is left out: sample peak), loudness range left out */
struct Loudness
{
    double sr = 48000; int nCh = 2;
    std::vector<std::array<Biquad, 2>> kw;
    std::vector<double> blocks;             // 100 ms blocks' mean squares (weighted sum over channels)
    double acc = 0; int accN = 0, blockN = 4800;
    std::atomic<float> momentary { -120 }, shortTerm { -120 }, integrated { -120 };
    std::vector<double> history;            // momentary 400 ms block powers for the integrated value (gated)
    std::array<double, 30> ring {}; int ringPos = 0, ringCount = 0;
    void prepare (double fs, int ch)
    {
        sr = fs; nCh = std::max (1, ch); blockN = (int) (0.1 * fs);
        kw.assign ((size_t) nCh, {});
        // stage 1: a high shelf (+4 db at high frequencies), stage 2: a high pass (rlb), for any rate
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Coefs s1 { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan (kPi * f1 / fs);
        Coefs s2 { 1.0, -2.0, 1.0, 2.0 * (K1 * K1 - 1.0) / (1.0 + K1 / Q1 + K1 * K1), (1.0 - K1 / Q1 + K1 * K1) / (1.0 + K1 / Q1 + K1 * K1) };
        for (auto& c : kw) { c[0].k = s1; c[1].k = s2; }
        acc = 0; accN = 0; history.clear(); history.reserve (36000); ringPos = ringCount = 0;
        momentary = shortTerm = integrated = -120.0f;
    }
    void reset() { prepare (sr, nCh); }
    void process (const juce::AudioBuffer<float>& b, int n)
    {
        const int ch = std::min (b.getNumChannels(), nCh);
        for (int i = 0; i < n; ++i)
        {
            double s = 0;
            for (int c = 0; c < ch; ++c)
            {
                const float y = kw[(size_t) c][1].tick (kw[(size_t) c][0].tick (b.getSample (c, i)));
                const double w = (ch >= 5 && (c == 3 || c == 4)) ? 1.41 : (ch >= 4 && c == 3 && ch < 5) ? 0.0 : 1.0;   // surrounds +1.5 db (5.1: lfe left out)
                s += w * (double) y * y;
            }
            acc += s; if (++accN >= blockN) push();
        }
    }
    static float lufs (double ms) { return ms <= 1.0e-12 ? -120.0f : (float) (-0.691 + 10.0 * std::log10 (ms)); }
private:
    void push()
    {
        ring[(size_t) ringPos] = acc / accN; ringPos = (ringPos + 1) % (int) ring.size(); ringCount = std::min (ringCount + 1, (int) ring.size());
        acc = 0; accN = 0;
        auto meanOf = [&] (int k) { double s = 0; const int m = std::min (k, ringCount); for (int j = 1; j <= m; ++j) s += ring[(size_t) ((ringPos - j + (int) ring.size()) % (int) ring.size())]; return m > 0 ? s / m : 0.0; };
        const double m4 = meanOf (4);
        momentary.store (lufs (m4)); shortTerm.store (lufs (meanOf (30)));
        if (ringCount >= 4 && history.size() < history.capacity()) history.push_back (m4);
        // gating: absolute -70 lufs, then relative -10 lu
        double s = 0; int k = 0; for (double h : history) if (lufs (h) > -70.0f) { s += h; ++k; }
        if (k > 0) { const double rel = lufs (s / k) - 10.0; double s2 = 0; int k2 = 0; for (double h : history) if (lufs (h) > -70.0f && lufs (h) > rel) { s2 += h; ++k2; } integrated.store (k2 > 0 ? lufs (s2 / k2) : -120.0f); }
    }
};

/** samples for the ui's pictures (spectrum, spectrogram, stereo field, scope): a lock-free ring of left / right */
struct ScopeFifo
{
    static constexpr int kSize = 1 << 15;
    std::array<float, kSize> l {}, r {};
    std::atomic<int> write { 0 };
    void push (const juce::AudioBuffer<float>& b, int n)
    {
        const int ch = b.getNumChannels(); if (ch == 0) return;
        int w = write.load (std::memory_order_relaxed);
        const float* a = b.getReadPointer (0); const float* c = b.getReadPointer (ch > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i) { l[(size_t) w] = a[i]; r[(size_t) w] = c[i]; w = (w + 1) & (kSize - 1); }
        write.store (w, std::memory_order_release);
    }
    /** the latest n samples (n <= kSize), oldest first */
    void latest (float* outL, float* outR, int n) const
    {
        const int w = write.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) { const int k = (w - n + i + kSize) & (kSize - 1); if (outL) outL[i] = l[(size_t) k]; if (outR) outR[i] = r[(size_t) k]; }
    }
};
} // namespace um::dsp

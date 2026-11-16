#pragma once
// faltung ms0 "master" — mastering bus for dense electronic / electroacoustic work and microhouse.
// Chain: trim → [stage A] linear-phase EQ (8 bands + hp/lp, each stereo / mid / side) + 2 linear-phase
// dynamic EQ bands → [stage B] linear-phase 4-band split (M and S): gentle compression, per-band width,
// air → saturation (2× oversampled, emphasis focus) → parallel ambience → glue → true-peak limiter →
// dither. Meters: BS.1770 loudness (M / S / I), true peak, correlation, spectrum (pre / post, M / S).
#include <juce_dsp/juce_dsp.h>
#include "Core.h"
#include "Fx.h"
#include <array>
#include <atomic>
#include <complex>
#include <memory>
#include <cstring>
#include <vector>

namespace fal
{
// =================================================================================================
//  analog prototype magnitudes (linear), used both to design the FIRs and to draw the curves
// =================================================================================================
namespace mag
{
inline double bell (double f, double f0, double db, double q)
{
    if (std::abs (db) < 1.0e-3) return 1.0;
    const double A = std::pow (10.0, db / 40.0), w = f / f0, w2 = w * w;
    // |H(jw)|² of the RBJ analog peaking filter: (1 - w²)² + (w A / Q)² over (1 - w²)² + (w / (A Q))²
    const double num = (1 - w2) * (1 - w2) + (w * A / q) * (w * A / q);
    const double den = (1 - w2) * (1 - w2) + (w / (A * q)) * (w / (A * q));
    return std::sqrt (num / den);
}
inline double lowShelf (double f, double f0, double db, double q)
{
    if (std::abs (db) < 1.0e-3) return 1.0;
    const double A = std::pow (10.0, db / 40.0), w = f / f0, w2 = w * w, sA = std::sqrt (A);
    // analog RBJ low shelf: A (s² + √A/Q s + A) / (A s² + √A/Q s + 1)
    const double nr = A - w2, ni = sA / q * w, dr = 1 - A * w2, di = sA / q * w;
    return A * std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
}
inline double highShelf (double f, double f0, double db, double q)
{
    if (std::abs (db) < 1.0e-3) return 1.0;
    const double A = std::pow (10.0, db / 40.0), w = f / f0, w2 = w * w, sA = std::sqrt (A);
    // analog RBJ high shelf: A (A s² + √A/Q s + 1) / (s² + √A/Q s + A)
    const double nr = 1 - A * w2, ni = sA / q * w, dr = A - w2, di = sA / q * w;
    return A * std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
}
inline double notch (double f, double f0, double q)
{
    const double w = f / f0, w2 = w * w;
    const double num = (1 - w2) * (1 - w2), den = (1 - w2) * (1 - w2) + (w / q) * (w / q);
    return std::sqrt (num / std::max (1.0e-30, den));
}
inline double bandPass (double f, double f0, double q)   // peak 1
{
    const double w = f / f0, w2 = w * w;
    const double num = (w / q) * (w / q), den = (1 - w2) * (1 - w2) + (w / q) * (w / q);
    return std::sqrt (num / std::max (1.0e-30, den));
}
inline double highPass (double f, double fc, int order) { return 1.0 / std::sqrt (1.0 + std::pow (fc / std::max (1.0e-6, f), 2.0 * order)); }
inline double lowPass (double f, double fc, int order) { return 1.0 / std::sqrt (1.0 + std::pow (f / fc, 2.0 * order)); }
/** Linkwitz-Riley-shaped low-pass (squared Butterworth); bands built from differences sum to 1 */
inline double lrLow (double f, double fc) { const double b = 1.0 / (1.0 + std::pow (f / fc, 8.0)); return b; }
} // namespace mag

// =================================================================================================
//  FIR design by frequency sampling: zero-phase magnitude → symmetric, Kaiser-windowed, N taps
// =================================================================================================
class FirDesigner
{
public:
    explicit FirDesigner (int nTaps) : N (nTaps), order ((int) std::log2 (nTaps)), fft (order), buf ((size_t) (2 * nTaps), 0.0f), win ((size_t) nTaps)
    {
        const double beta = 7.0;
        auto i0 = [] (double z) { double s = 1, t = 1; for (int j = 1; j < 40; ++j) { t *= (z / (2 * j)) * (z / (2 * j)); s += t; } return s; };
        for (int n = 0; n < N; ++n)
        {
            const double x = 2.0 * n / (N - 1) - 1.0;
            win[(size_t) n] = (float) (i0 (beta * std::sqrt (std::max (0.0, 1 - x * x))) / i0 (beta));
        }
    }
    int size() const { return N; }
    /** fills out[N] from magnitude(f) */
    template <typename Mag>
    void design (double sr, Mag&& magnitude, float* out)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        // JUCE real-only layout: interleaved re/im for bins 0..N/2
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = (double) k * sr / N;
            buf[(size_t) (2 * k)] = (float) magnitude (std::max (f, 0.5));
            buf[(size_t) (2 * k + 1)] = 0.0f;
        }
        fft.performRealOnlyInverseTransform (buf.data());
        // zero-phase impulse is centred on 0: rotate to N/2 and window
        for (int n = 0; n < N; ++n)
        {
            const int src = (n - N / 2 + N) % N;
            out[n] = buf[(size_t) src] * win[(size_t) n];
        }
    }
private:
    int N, order;
    juce::dsp::FFT fft;
    std::vector<float> buf, win;
};

// =================================================================================================
//  Uniformly partitioned FFT convolution (mono, overlap-save) with click-free IR swaps: a new IR is
//  staged from another thread and faded in over one block.
// =================================================================================================
class PartConv
{
public:
    static constexpr int B = 256;           // partition / block (= latency)
    static constexpr int F = 2 * B;         // fft size

    void prepare (int irLen)
    {
        P = (irLen + B - 1) / B;
        for (auto& sl : slot) sl.assign ((size_t) (P * (F + 2)), 0.0f);
        fdl.assign ((size_t) (P * (F + 2)), 0.0f);
        inBuf.assign ((size_t) F, 0.0f);
        outNew.assign ((size_t) B, 0.0f);
        outOld.assign ((size_t) B, 0.0f);
        tmp.assign ((size_t) F * 2, 0.0f);
        acc.assign ((size_t) F * 2, 0.0f);
        fdlPos = 0; pos = 0; active.store (0); old = -1; state.store (0); hasIr = false;
    }
    static int latency() { return B; }

    /** from any non-audio thread; false if a swap is still in progress */
    bool stage (const float* ir, int irLen)
    {
        if (state.load() != 0) return false;
        const int idx = (active.load() + 1) % 3;
        fill (slot[(size_t) idx], ir, irLen);
        staged = idx;
        state.store (1);
        return true;
    }
    /** before playback */
    void loadNow (const float* ir, int irLen)
    {
        fill (slot[0], ir, irLen);
        active.store (0); old = -1; state.store (0); hasIr = true;
    }

    void process (float* io, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            inBuf[(size_t) (B + pos)] = io[i];
            float y = outNew[(size_t) pos];
            if (old >= 0) { const float t = (float) pos / (float) B; y = outOld[(size_t) pos] * (1.0f - t) + y * t; }
            io[i] = hasIr ? y : 0.0f;
            if (++pos == B) { pos = 0; block(); }
        }
    }

private:
    int P = 1, fdlPos = 0, pos = 0, old = -1, staged = 0;
    bool hasIr = false;
    std::atomic<int> active { 0 }, state { 0 };   // state: 0 free · 1 staged · 2 fading
    juce::dsp::FFT fft { 9 };                     // 512 = F
    std::array<std::vector<float>, 3> slot;
    std::vector<float> fdl, inBuf, outNew, outOld, tmp, acc;

    void fill (std::vector<float>& dst, const float* ir, int irLen)
    {
        std::vector<float> t ((size_t) F * 2, 0.0f);
        for (int p = 0; p < P; ++p)
        {
            std::fill (t.begin(), t.end(), 0.0f);
            for (int n = 0; n < B; ++n) { const int i = p * B + n; t[(size_t) n] = i < irLen ? ir[i] : 0.0f; }
            juce::dsp::FFT f (9);
            f.performRealOnlyForwardTransform (t.data(), true);
            std::copy (t.begin(), t.begin() + F + 2, dst.begin() + p * (F + 2));
        }
    }
    void convolveWith (int s, std::vector<float>& out)
    {
        std::fill (acc.begin(), acc.end(), 0.0f);
        const auto& H = slot[(size_t) s];
        for (int p = 0; p < P; ++p)
        {
            const int xi = ((fdlPos - p) % P + P) % P;
            const float* X = fdl.data() + xi * (F + 2);
            const float* h = H.data() + p * (F + 2);
            float* a = acc.data();
            for (int k = 0; k <= B; ++k)
            {
                const float xr = X[2 * k], xim = X[2 * k + 1], hr = h[2 * k], him = h[2 * k + 1];
                a[2 * k] += xr * hr - xim * him;
                a[2 * k + 1] += xr * him + xim * hr;
            }
        }
        fft.performRealOnlyInverseTransform (acc.data());
        for (int n = 0; n < B; ++n) out[(size_t) n] = acc[(size_t) (B + n)];
    }
    void block()
    {
        fdlPos = (fdlPos + 1) % P;
        std::copy (inBuf.begin(), inBuf.end(), tmp.begin());
        std::fill (tmp.begin() + F, tmp.end(), 0.0f);
        fft.performRealOnlyForwardTransform (tmp.data(), true);
        std::copy (tmp.begin(), tmp.begin() + F + 2, fdl.begin() + fdlPos * (F + 2));
        std::copy (inBuf.begin() + B, inBuf.end(), inBuf.begin());

        if (state.load() == 2) { old = -1; state.store (0); }       // last block faded: free
        if (state.load() == 1)
        {
            old = active.load(); active.store (staged); state.store (2);
            convolveWith (old, outOld);
        }
        convolveWith (active.load(), outNew);
    }
};

// =================================================================================================
//  parameters
// =================================================================================================
struct MasterParams
{
    struct Eq { bool on = false; int type = 0; float freq = 1000, gain = 0, q = 0.7f; int ch = 0; };   // type: bell, low shelf, high shelf, notch · ch: stereo, mid, side
    struct Dyn { bool on = false; int ch = 1; float freq = 400, q = 1.5f, thr = -30, range = -4, attack = 10, release = 120; };
    struct Band { float thr = -18, ratio = 1.5f, attack = 30, release = 160, gain = 0, width = 1; bool solo = false; };
    float trim = 0; int listen = 0; bool bypass = false, match = true; bool dcOn = true; float balance = 0;
    bool eqOn = true; std::array<Eq, 8> eq; bool hpOn = false; float hpF = 22; int hpOrd = 2; int hpCh = 0; bool lpOn = false; float lpF = 20000; int lpOrd = 2; int lpCh = 0;
    bool dynOn = true; std::array<Dyn, 2> dyn;
    bool mbOn = true; float x1 = 80, x2 = 350, x3 = 4000; std::array<Band, 4> mb; bool imgOn = true; float air = 0, midLvl = 0, sideLvl = 0;
    bool satOn = false; int satCurve = 0, satFocus = 0; float satDrive = 3, satMix = 1;
    bool ambOn = false; float ambLevel = -30, ambSize = 0.3f, ambDecay = 0.6f, ambHp = 300, ambLp = 10000, ambWidth = 1;
    bool glueOn = true; float gThr = -16, gRatio = 2, gAtt = 20, gRel = 150; bool gAuto = true; float gMakeup = 0, gMix = 1;
    bool clipOn = false; float clipDrive = 0, clipSoft = 0.5f;
    bool limOn = true; float limGain = 0, ceiling = -1, limRel = 120; bool limAuto = true; float lookahead = 3; int limOs = 4; float limLink = 1;
    int dither = 0; bool shaping = false; float out = 0;
};

// =================================================================================================
//  helpers
// =================================================================================================
/** 2× oversampling with linear-phase half-band FIRs (63 taps); total latency 31 base samples */
class Over2
{
public:
    Over2()
    {
        const int N = kT, M = N / 2; double sum = 0;
        auto i0 = [] (double z) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (z / (2 * j)) * (z / (2 * j)); s += t; } return s; };
        for (int n = 0; n < N; ++n)
        {
            const double k = n - M, sinc = k == 0 ? 1.0 : std::sin (kPi * 0.5 * k) / (kPi * 0.5 * k);
            const double x = 2.0 * n / (N - 1) - 1.0;
            h[(size_t) n] = (float) (0.5 * sinc * i0 (9.0 * std::sqrt (std::max (0.0, 1 - x * x))) / i0 (9.0));
            sum += h[(size_t) n];
        }
        for (auto& c : h) c = (float) (c / sum);
    }
    void reset() { up.fill (0); dn.fill (0); up_i = dn_i = 0; }
    static constexpr int latency = 31;
    void upsample (float x, float& y0, float& y1)
    {
        push (up, up_i, x * 2.0f); y0 = conv (up, up_i);
        push (up, up_i, 0.0f);     y1 = conv (up, up_i);
    }
    float downsample (float a, float b) { push (dn, dn_i, a); push (dn, dn_i, b); return conv (dn, dn_i); }
private:
    static constexpr int kT = 63, kB = 64;
    std::array<float, kT> h {};
    std::array<float, kB> up {}, dn {};
    int up_i = 0, dn_i = 0;
    static void push (std::array<float, kB>& b, int& i, float x) { b[(size_t) (i & (kB - 1))] = x; ++i; }
    float conv (const std::array<float, kB>& b, int i) const
    {
        float y = 0; for (int k = 0; k < kT; ++k) y += h[(size_t) k] * b[(size_t) ((i - 1 - k) & (kB - 1))]; return y;
    }
};

/** 4× windowed-sinc interpolation for true-peak detection; output is delayed by `latency` samples */
class TruePeak
{
public:
    static constexpr int kTaps = 16, latency = 8;
    TruePeak()
    {
        for (int ph = 1; ph < 4; ++ph)
            for (int k = 0; k < kTaps; ++k)
            {
                const double t = (k - kTaps / 2 + 1) - ph / 4.0;   // position relative to the interpolated point
                const double sinc = std::sin (kPi * t) / (kPi * t);
                const double w = 0.5 + 0.5 * std::cos (kPi * t / (kTaps / 2.0));
                c[(size_t) (ph - 1)][(size_t) k] = (float) (sinc * w);
            }
    }
    void reset() { buf.fill (0); i = 0; }
    /** push a sample, return |peak| (incl. inter-sample) around the sample `latency` back */
    float push (float x)
    {
        buf[(size_t) (i & 31)] = x; ++i;
        float pk = std::abs (buf[(size_t) ((i - 1 - latency) & 31)]);
        for (int ph = 0; ph < 3; ++ph)
        {
            float y = 0;
            for (int k = 0; k < kTaps; ++k) y += c[(size_t) ph][(size_t) k] * buf[(size_t) ((i - kTaps + k) & 31)];
            pk = std::max (pk, std::abs (y));
        }
        return pk;
    }
private:
    std::array<std::array<float, kTaps>, 3> c {};
    std::array<float, 32> buf {}; int i = 0;
};

/** a half-band stage (2x up / down) with T taps; latency (T-1) samples at the higher rate, round trip */
class Halfband
{
public:
    explicit Halfband (int taps = 63, double beta = 9.0) : T (taps), h ((size_t) taps), up ((size_t) 128, 0.0f), dn ((size_t) 128, 0.0f)
    {
        const int M = T / 2; double sum = 0;
        auto i0 = [] (double z) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (z / (2 * j)) * (z / (2 * j)); s += t; } return s; };
        for (int n = 0; n < T; ++n)
        {
            const double k = n - M, sinc = k == 0 ? 1.0 : std::sin (kPi * 0.5 * k) / (kPi * 0.5 * k);
            const double x = 2.0 * n / (T - 1) - 1.0;
            h[(size_t) n] = (float) (0.5 * sinc * i0 (beta * std::sqrt (std::max (0.0, 1 - x * x))) / i0 (beta));
            sum += h[(size_t) n];
        }
        for (auto& c : h) c = (float) (c / sum);
    }
    void reset() { std::fill (up.begin(), up.end(), 0.0f); std::fill (dn.begin(), dn.end(), 0.0f); ui = di = 0; }
    void upsample (float x, float& y0, float& y1) { push (up, ui, x * 2.0f); y0 = conv (up, ui); push (up, ui, 0.0f); y1 = conv (up, ui); }
    /** keeps the even phase, so the round trip is exactly (T - 1) / 2 base samples */
    float downsample (float a, float b) { push (dn, di, a); const float y = conv (dn, di); push (dn, di, b); return y; }
private:
    int T; std::vector<float> h, up, dn; int ui = 0, di = 0;
    static void push (std::vector<float>& b, int& i, float x) { b[(size_t) (i & 127)] = x; ++i; }
    float conv (const std::vector<float>& b, int i) const { float y = 0; for (int k = 0; k < T; ++k) y += h[(size_t) k] * b[(size_t) ((i - 1 - k) & 127)]; return y; }
};

/** 1x / 2x / 4x / 8x oversampling with cascaded linear-phase half-bands. Latency (base samples): 0 / 31 / 39 / 43. */
class OverN
{
public:
    static int latencyFor (int f) { return f >= 8 ? 43 : (f >= 4 ? 39 : (f >= 2 ? 31 : 0)); }
    OverN() : st { Halfband (63, 9.0), Halfband (33, 7.0), Halfband (33, 7.0) } {}
    void prepare (int maxBlock) { bufA.assign ((size_t) maxBlock * 8 + 8, 0.0f); bufB.assign ((size_t) maxBlock * 8 + 8, 0.0f); reset(); }
    void reset() { for (auto& x : st) x.reset(); }
    static int stagesFor (int f) { return f >= 8 ? 3 : (f >= 4 ? 2 : (f >= 2 ? 1 : 0)); }
    /** n base samples → n * f samples in out */
    void up (const float* in, int n, int f, float* out)
    {
        const int S = stagesFor (f);
        if (S == 0) { std::copy_n (in, n, out); return; }
        const float* src = in; int len = n;
        for (int s = 0; s < S; ++s)
        {
            float* dst = s == S - 1 ? out : (s % 2 == 0 ? bufA.data() : bufB.data());
            for (int i = 0; i < len; ++i) st[(size_t) s].upsample (src[i], dst[2 * i], dst[2 * i + 1]);
            src = dst; len *= 2;
        }
    }
    /** nOs oversampled samples → nOs / f base samples in out */
    void down (const float* in, int nOs, int f, float* out)
    {
        const int S = stagesFor (f);
        if (S == 0) { std::copy_n (in, nOs, out); return; }
        const float* src = in; int len = nOs;
        for (int s = S - 1, k = 0; s >= 0; --s, ++k)
        {
            float* dst = s == 0 ? out : (k % 2 == 0 ? bufA.data() : bufB.data());
            for (int i = 0; i < len / 2; ++i) dst[i] = st[(size_t) s].downsample (src[2 * i], src[2 * i + 1]);
            src = dst; len /= 2;
        }
    }
private:
    std::array<Halfband, 3> st;
    std::vector<float> bufA, bufB;
};

/** look-ahead peak limiter: sliding minimum + box-car (never late), program-dependent release, stereo link,
    optional 4x true-peak detection. Runs at whatever rate it is prepared for. */
class PeakLimiter
{
public:
    void prepare (int lookMaxSamples, bool truePeakDetect)
    {
        lookMax = std::max (1, lookMaxSamples); tpOn = truePeakDetect;
        lat = lookMax + (tpOn ? TruePeak::latency : 0);
        for (auto& d : dl) d.prepare (lat + 8);
        for (int c = 0; c < 2; ++c) { mq[(size_t) c].assign ((size_t) (lookMax + 4), { 0, 1.0f }); head[(size_t) c] = tail[(size_t) c] = 0; box[(size_t) c].assign ((size_t) (lookMax + 2), 1.0f); boxSum[(size_t) c] = 0; g[(size_t) c] = 1.0f; tp[(size_t) c].reset(); dpk[(size_t) c].assign ((size_t) (TruePeak::latency + 2), 0.0f); }
        idx = 0; boxI = 0; boxWin = -1;
    }
    int latency() const { return lat; }
    float grDb = 0;
    void process (float* L, float* R, int n, bool on, float ceil, int win, float relCoefFast, float relCoefSlow, bool autoRel, float link)
    {
        win = std::clamp (win, 1, lookMax);
        float* io[2] = { L, R };
        float maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            float need[2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i];
                dl[(size_t) c].push (x);
                const float pk = tpOn ? tp[(size_t) c].push (x) : std::abs (x);
                need[c] = (on && pk > ceil) ? ceil / pk : 1.0f;
            }
            const float lk = std::min (need[0], need[1]);
            ++idx;
            if (win != boxWin || boxI % 4096 == 0)
            {
                boxWin = win;
                for (int c = 0; c < 2; ++c) { boxSum[(size_t) c] = 0; for (int k = 0; k < win; ++k) boxSum[(size_t) c] += box[(size_t) c][(size_t) k]; }
            }
            for (int c = 0; c < 2; ++c)
            {
                const float nd = need[c] + (lk - need[c]) * link;
                auto& q = mq[(size_t) c]; int& hd = head[(size_t) c]; int& tl = tail[(size_t) c]; const int cap = (int) q.size();
                while (tl != hd && q[(size_t) ((tl - 1 + cap) % cap)].second >= nd) tl = (tl - 1 + cap) % cap;
                q[(size_t) tl] = { idx, nd }; tl = (tl + 1) % cap;
                while (q[(size_t) hd].first <= idx - (lookMax + 1)) hd = (hd + 1) % cap;
                const float hold = q[(size_t) hd].second;
                boxSum[(size_t) c] += hold - box[(size_t) c][(size_t) (boxI % win)];
                box[(size_t) c][(size_t) (boxI % win)] = hold;
                const float target = (float) (boxSum[(size_t) c] / win);
                float& gc = g[(size_t) c];
                if (target < gc) gc = target;
                else { const float k = autoRel ? (gc < 0.89f ? relCoefSlow : relCoefFast) : relCoefFast; gc = std::min (target, target + k * (gc - target)); }
                const float y = dl[(size_t) c].readInt (lat + 1) * gc;
                io[c][i] = on ? clampf (y, -ceil, ceil) : y;
                maxGr = std::max (maxGr, -gainToDb (gc));
            }
            ++boxI;
        }
        grDb = maxGr;
    }
private:
    int lookMax = 1, lat = 1; bool tpOn = false;
    std::array<DelayLine, 2> dl;
    std::array<std::vector<std::pair<long long, float>>, 2> mq;
    std::array<int, 2> head {}, tail {};
    std::array<std::vector<float>, 2> box;
    std::array<double, 2> boxSum {};
    std::array<float, 2> g { 1.0f, 1.0f };
    std::array<TruePeak, 2> tp;
    std::array<std::vector<float>, 2> dpk;
    long long idx = 0; int boxI = 0, boxWin = -1;
};

/** BS.1770-4 loudness: momentary (400 ms), short-term (3 s), integrated (gated) */
class Loudness
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        auto shelf = [&] (Biquad& b)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1 + K / Q + K * K;
            b.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0); b.b1 = (float) (2 * (K * K - Vh) / a0); b.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            b.a1 = (float) (2 * (K * K - 1) / a0); b.a2 = (float) ((1 - K / Q + K * K) / a0);
        };
        auto hp = [&] (Biquad& b)
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / sr), a0 = 1 + K / Q + K * K;
            b.b0 = 1; b.b1 = -2; b.b2 = 1; b.a1 = (float) (2 * (K * K - 1) / a0); b.a2 = (float) ((1 - K / Q + K * K) / a0);
        };
        for (int c = 0; c < 2; ++c) { shelf (s1[(size_t) c]); hp (s2[(size_t) c]); s1[(size_t) c].reset(); s2[(size_t) c].reset(); }
        blockLen = (int) std::round (sr * 0.1);
        reset();
    }
    void reset()
    {
        acc = 0; cnt = 0; ring.fill (0); ringN = 0; ringI = 0; hist.fill (0); histE.fill (0); stHist.fill (0); stSkip = 0; range.store (0);
        momentary.store (-100); shortTerm.store (-100); integrated.store (-100);
    }
    void process (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = s2[0].tick (s1[0].tick (L[i])), r = s2[1].tick (s1[1].tick (R[i]));
            acc += (double) l * l + (double) r * r;
            if (++cnt >= blockLen) step();
        }
    }
    std::atomic<float> momentary { -100 }, shortTerm { -100 }, integrated { -100 }, range { 0 };
private:
    std::array<int, 800> stHist {}; int stSkip = 0;
    double sr = 48000, acc = 0; int cnt = 0, blockLen = 4800;
    std::array<Biquad, 2> s1, s2;
    std::array<double, 30> ring {}; int ringN = 0, ringI = 0;
    std::array<int, 800> hist {}; std::array<double, 800> histE {};
    static float lufs (double meanSq) { return meanSq <= 1.0e-12 ? -100.0f : (float) (-0.691 + 10.0 * std::log10 (meanSq)); }
    void step()
    {
        ring[(size_t) ringI] = acc / cnt; ringI = (ringI + 1) % 30; ringN = std::min (30, ringN + 1);
        acc = 0; cnt = 0;
        auto mean = [&] (int k) { double s = 0; const int m = std::min (k, ringN); for (int j = 1; j <= m; ++j) s += ring[(size_t) ((ringI - j + 30) % 30)]; return m > 0 ? s / m : 0.0; };
        const double m4 = mean (4);
        momentary.store (lufs (m4)); shortTerm.store (lufs (mean (30)));
        // loudness range (EBU Tech 3342): short-term values, gated -70 / -20, 10th → 95th percentile
        if (ringN >= 30 && ++stSkip % 2 == 0)
        {
            const float stv = lufs (mean (30));
            if (stv > -70.0f) stHist[(size_t) std::clamp ((int) ((stv + 70.0f) * 10.0f), 0, 799)]++;
            double e = 0; long c = 0;
            for (int b = 0; b < 800; ++b) if (stHist[(size_t) b]) { e += stHist[(size_t) b] * std::pow (10.0, (b * 0.1 - 70.0) / 10.0); c += stHist[(size_t) b]; }
            if (c > 20)
            {
                const int gateBin = std::max (0, (int) (((float) (10.0 * std::log10 (e / c)) - 20.0f + 70.0f) * 10.0f));
                long n2 = 0; for (int b = gateBin; b < 800; ++b) n2 += stHist[(size_t) b];
                long acc2 = 0; int lo = -1, hi = -1;
                for (int b = gateBin; b < 800; ++b)
                {
                    acc2 += stHist[(size_t) b];
                    if (lo < 0 && acc2 >= (long) (0.10 * n2)) lo = b;
                    if (hi < 0 && acc2 >= (long) (0.95 * n2)) hi = b;
                }
                if (lo >= 0 && hi >= 0) range.store ((hi - lo) * 0.1f);
            }
        }
        if (ringN >= 4)
        {
            const float l = lufs (m4);
            if (l > -70.0f) { const int b = std::clamp ((int) ((l + 70.0f) * 10.0f), 0, 799); hist[(size_t) b]++; histE[(size_t) b] += m4; }
            // gating: absolute -70, relative -10 below the ungated mean
            double e = 0; long c = 0;
            for (int b = 0; b < 800; ++b) { e += histE[(size_t) b]; c += hist[(size_t) b]; }
            if (c > 0)
            {
                const float rel = lufs (e / (double) c) - 10.0f;
                double e2 = 0; long c2 = 0;
                for (int b = std::max (0, (int) ((rel + 70.0f) * 10.0f)); b < 800; ++b) { e2 += histE[(size_t) b]; c2 += hist[(size_t) b]; }
                if (c2 > 0) integrated.store (lufs (e2 / (double) c2));
            }
        }
    }
};

/** lock-free ring of recent samples for the editor (analyser, vectorscope) */
struct ScopeRing
{
    static constexpr int N = 16384;
    std::array<float, N> a {}, b {};
    std::atomic<int> w { 0 };
    void push (float x, float y) { const int i = w.load (std::memory_order_relaxed); a[(size_t) i] = x; b[(size_t) i] = y; w.store ((i + 1) & (N - 1), std::memory_order_release); }
    /** copy the last n samples */
    void read (float* x, float* y, int n) const
    {
        const int e = w.load (std::memory_order_acquire);
        for (int k = 0; k < n; ++k) { const int idx = (e - n + k + N) & (N - 1); x[k] = a[(size_t) idx]; y[k] = b[(size_t) idx]; }
    }
};

// =================================================================================================
//  the chain
// =================================================================================================
class MasterChain
{
public:
    // meters + editor feeds
    Loudness outMeter, inMeter, wetMeter;   // wetMeter: the processed signal before the compare switch (for level matching)
    std::atomic<float> truePeak { -100 }, corr { 1 }, limGr { 0 }, glueGr { 0 };
    std::array<std::atomic<float>, 4> bandGr {}, bandWidth {};
    std::array<std::atomic<float>, 2> dynGr {};
    ScopeRing preMS, postMS, postLR;

    int firLen() const { return N; }
    int latency() const { return latencyNoDry(); }

    void prepare (double sampleRate, int)
    {
        sr = sampleRate;
        N = sr <= 50000 ? 8192 : (sr <= 100000 ? 16384 : 32768);
        designer = std::make_unique<FirDesigner> (N);
        ir.assign ((size_t) N, 0.0f); ir2.assign ((size_t) N, 0.0f);
        for (auto* c : allConvs()) c->prepare (N);
        const int stageLat = PartConv::B + N / 2;
        for (auto& d : dlyA) d.prepare (stageLat + 8);
        for (auto& d : dlyB) d.prepare (stageLat + 8);
        lookMax = (int) std::ceil (0.005 * sr);
        lookS = (int) std::ceil (0.0015 * sr);
        static const int fs[4] = { 1, 2, 4, 8 };
        for (int k = 0; k < 4; ++k) limF[(size_t) k].prepare (lookMax * fs[k], fs[k] == 1);
        safety.prepare (lookS, true);
        limDelay = lookMax + 43 + safety.latency();
        for (auto& d : limPad) d.prepare (64);
        for (auto& o : limOver) o.prepare (4096);
        for (auto& o : satOver) o.prepare (4096);
        osL.assign (4096 * 8 + 8, 0.0f); osR.assign (4096 * 8 + 8, 0.0f); osT.assign (4096 * 8 + 8, 0.0f);
        for (auto& d : dlyDry) d.prepare (latencyNoDry() + kMax + 8);   // a block of input sits in front of the latency
        for (auto& d : satDry) d.prepare (kSatOs + 8);
        for (auto& d : dcIn) { d.reset(); d.r = 1.0f - kTwoPi * 3.0f / (float) sr; }
        midSm.prepare ((float) sr, 20.0f); sideSm.prepare ((float) sr, 20.0f); midSm.snap (1.0f); sideSm.snap (1.0f);
        for (auto& t : tpOut) t.reset();
        room.prepare ((float) sr);
        glue.prepare ((float) sr);
        outMeter.prepare (sr); inMeter.prepare (sr); wetMeter.prepare (sr);
        matchSm.prepare ((float) sr, 400.0f); matchSm.snap (1.0f);
        bypSm.prepare ((float) sr, 30.0f); bypSm.snap (0.0f);
        trimSm.prepare ((float) sr, 20.0f); outSm.prepare ((float) sr, 20.0f); limGainSm.prepare ((float) sr, 20.0f);
        trimSm.snap (1.0f); outSm.snap (1.0f); limGainSm.snap (1.0f); firstBlock = true;
        airAp.fill (Allpass()); { int lens[4] = { 113, 173, 241, 337 }; for (int k = 0; k < 4; ++k) airAp[(size_t) k].prepare ((int) (lens[k] * sr / 48000.0)); }
        bandEnv.fill (0); bandGrDb.fill (0); dynEnv.fill (0); dynGrDb.fill (0);
        corrLR = corrLL = corrRR = 0; tpHold = 0;
        lastHashA = lastHashB = 0;
        rng.seed (12345);
        ditherErr.fill (0);
        for (auto& ch : satPre) for (auto& f : ch) f.reset();
        for (auto& ch : satPost) for (auto& f : ch) f.reset();
        for (auto& ch : ambF) for (auto& f : ch) f.reset();
    }

    /** design FIRs for the current parameters; call from the design thread (or before playback with now = true) */
    void design (const MasterParams& p, bool now)
    {
        const uint64_t ha = hashA (p), hb = hashB (p);
        if (ha != lastHashA || now)
        {
            bool ok = true;
            ok &= stageFir (convA[0], [&] (double f) { return eqMag (p, f, 1); }, now);
            ok &= stageFir (convA[1], [&] (double f) { return eqMag (p, f, 2); }, now);
            for (int k = 0; k < 2; ++k)
            {
                const auto& d = p.dyn[(size_t) k];
                ok &= stageFir (convDyn[(size_t) k][0], [&] (double f) { return mag::bandPass (f, d.freq, d.q); }, now);
                ok &= stageFir (convDyn[(size_t) k][1], [&] (double f) { return mag::bandPass (f, d.freq, d.q); }, now);
            }
            if (ok) lastHashA = ha;
        }
        if (hb != lastHashB || now)
        {
            bool ok = true;
            const float xs[3] = { p.x1, std::max (p.x2, p.x1 * 1.2f), std::max (p.x3, std::max (p.x2, p.x1 * 1.2f) * 1.2f) };
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 2; ++c)
                    ok &= stageFir (convB[(size_t) k][(size_t) c], [&] (double f) { return mag::lrLow (f, xs[k]); }, now);
            if (ok) lastHashB = hb;
        }
    }

    /** total EQ magnitude for a channel (1 mid, 2 side) — also used to draw the curves */
    static double eqMag (const MasterParams& p, double f, int ch)
    {
        if (! p.eqOn) return 1.0;
        double m = 1.0;
        auto applies = [ch] (int c) { return c == 0 || c == ch; };
        for (auto& b : p.eq)
        {
            if (! b.on || ! applies (b.ch)) continue;
            switch (b.type)
            {
                case 0: m *= mag::bell (f, b.freq, b.gain, b.q); break;
                case 1: m *= mag::lowShelf (f, b.freq, b.gain, b.q); break;
                case 2: m *= mag::highShelf (f, b.freq, b.gain, b.q); break;
                default: m *= mag::notch (f, b.freq, b.q); break;
            }
        }
        if (p.hpOn && applies (p.hpCh)) m *= mag::highPass (f, p.hpF, p.hpOrd);
        if (p.lpOn && applies (p.lpCh)) m *= mag::lowPass (f, p.lpF, p.lpOrd);
        return m;
    }

    void process (float* L, float* R, int n, const MasterParams& p)
    {
        const int stageLat = PartConv::B + N / 2;
        // ---- input: trim, meter, pre analyser, dry copy ----------------------------------------
        trimSm.target = dbToGain (p.trim);
        if (firstBlock) { trimSm.snap (trimSm.target); outSm.snap (dbToGain (p.out)); limGainSm.snap (dbToGain (p.limOn ? p.limGain : 0.0f)); firstBlock = false; }
        for (int i = 0; i < n; ++i)
        {
            const float g = trimSm.tick();
            L[i] *= g * std::min (1.0f, 1.0f - p.balance); R[i] *= g * std::min (1.0f, 1.0f + p.balance);
            if (p.dcOn) { L[i] = dcIn[0].tick (L[i]); R[i] = dcIn[1].tick (R[i]); }
            dlyDry[0].push (L[i]); dlyDry[1].push (R[i]);
            M[(size_t) i] = 0.5f * (L[i] + R[i]); S[(size_t) i] = 0.5f * (L[i] - R[i]);
            preMS.push (M[(size_t) i], S[(size_t) i]);
        }
        inMeter.process (L, R, n);

        // ---- stage A: linear-phase EQ + dynamic EQ ----------------------------------------------
        std::copy_n (M.data(), n, eqM.data()); std::copy_n (S.data(), n, eqS.data());
        convA[0].process (eqM.data(), n); convA[1].process (eqS.data(), n);
        for (int k = 0; k < 2; ++k)
            {
                // the band-pass FIRs always run, so switching a band on never starts from stale state
                const auto& d = p.dyn[(size_t) k];
                std::copy_n (M.data(), n, bpM.data()); std::copy_n (S.data(), n, bpS.data());
                convDyn[(size_t) k][0].process (bpM.data(), n); convDyn[(size_t) k][1].process (bpS.data(), n);
                if (! p.dynOn || ! d.on) { dynGrDb[(size_t) k] = 0; dynEnv[(size_t) k] = 0; dynGr[(size_t) k].store (0); continue; }
                const float aK = tcoef (std::max (0.1f, d.attack), (float) sr), rK = tcoef (std::max (5.0f, d.release), (float) sr);
                float maxGr = 0;
                for (int i = 0; i < n; ++i)
                {
                    const float m = bpM[(size_t) i], s = bpS[(size_t) i];
                    const float e = d.ch == 1 ? m * m : (d.ch == 2 ? s * s : std::max (m * m, s * s));
                    dynEnv[(size_t) k] = e + (e > dynEnv[(size_t) k] ? aK : rK) * (dynEnv[(size_t) k] - e);
                    const float lv = 10.0f * std::log10 (dynEnv[(size_t) k] + 1.0e-12f) + 3.0f;
                    float want = 0;
                    if (lv > d.thr) want = std::clamp ((lv - d.thr) * 0.5f, 0.0f, std::abs (d.range)) * (d.range < 0 ? -1.0f : 1.0f);
                    dynGrDb[(size_t) k] = want + (std::abs (want) > std::abs (dynGrDb[(size_t) k]) ? aK : rK) * (dynGrDb[(size_t) k] - want);
                    const float g = dbToGain (dynGrDb[(size_t) k]) - 1.0f;
                    if (d.ch != 2) eqM[(size_t) i] += m * g;
                    if (d.ch != 1) eqS[(size_t) i] += s * g;
                    maxGr = std::max (maxGr, std::abs (dynGrDb[(size_t) k]));
                }
                dynGr[(size_t) k].store (dynGrDb[(size_t) k]);
                juce::ignoreUnused (maxGr);
            }

        // ---- stage B: 4-band split of M and S (complementary), dynamics + width -----------------
        for (int k = 0; k < 3; ++k)
        {
            std::copy_n (eqM.data(), n, lp[(size_t) k][0].data()); std::copy_n (eqS.data(), n, lp[(size_t) k][1].data());
            convB[(size_t) k][0].process (lp[(size_t) k][0].data(), n); convB[(size_t) k][1].process (lp[(size_t) k][1].data(), n);
        }
        bool anySolo = false; for (auto& b : p.mb) anySolo |= b.solo;
        std::array<float, 4> aK {}, rK {}, mk {}, wd {};
        for (int b = 0; b < 4; ++b)
        {
            aK[(size_t) b] = tcoef (std::max (0.5f, p.mb[(size_t) b].attack), (float) sr);
            rK[(size_t) b] = tcoef (std::max (10.0f, p.mb[(size_t) b].release), (float) sr);
            mk[(size_t) b] = dbToGain (p.mb[(size_t) b].gain);
            wd[(size_t) b] = p.imgOn ? p.mb[(size_t) b].width : 1.0f;
        }
        std::array<double, 4> eM {}, eS {};
        midSm.target = dbToGain (p.imgOn ? p.midLvl : 0.0f); sideSm.target = dbToGain (p.imgOn ? p.sideLvl : 0.0f);
        for (int i = 0; i < n; ++i)
        {
            dlyB[0].push (eqM[(size_t) i]); dlyB[1].push (eqS[(size_t) i]);
            const float dM = dlyB[0].readInt (stageLat + 1), dS = dlyB[1].readInt (stageLat + 1);
            const float l1M = lp[0][0][(size_t) i], l2M = lp[1][0][(size_t) i], l3M = lp[2][0][(size_t) i];
            const float l1S = lp[0][1][(size_t) i], l2S = lp[1][1][(size_t) i], l3S = lp[2][1][(size_t) i];
            float bm[4] = { l1M, l2M - l1M, l3M - l2M, dM - l3M };
            float bs[4] = { l1S, l2S - l1S, l3S - l2S, dS - l3S };
            // air: a decorrelated copy of the top band's mid, added to its side
            if (p.imgOn && p.air > 0.001f)
            {
                float a = bm[3];
                for (int k = 0; k < 4; ++k) a = airAp[(size_t) k].tick (a, 0.6f);
                bs[3] += a * p.air * 0.7f;
            }
            float om = 0, os = 0;
            for (int b = 0; b < 4; ++b)
            {
                float g = 1.0f;
                if (p.mbOn)
                {
                    const auto& bp = p.mb[(size_t) b];
                    const float e = bm[b] * bm[b] + bs[b] * bs[b];
                    bandEnv[(size_t) b] = e + (e > bandEnv[(size_t) b] ? aK[(size_t) b] : rK[(size_t) b]) * (bandEnv[(size_t) b] - e);
                    const float lv = 10.0f * std::log10 (bandEnv[(size_t) b] + 1.0e-12f) + 3.0f;
                    const float over = lv - bp.thr, knee = 6.0f;
                    float red = 0;
                    if (over > knee * 0.5f) red = over * (1.0f - 1.0f / bp.ratio);
                    else if (over > -knee * 0.5f) red = (1.0f - 1.0f / bp.ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                    bandGrDb[(size_t) b] = red + (red > bandGrDb[(size_t) b] ? aK[(size_t) b] : rK[(size_t) b]) * (bandGrDb[(size_t) b] - red);
                    g = dbToGain (-bandGrDb[(size_t) b]) * mk[(size_t) b];
                }
                const float m2 = bm[b] * g, s2 = bs[b] * g * wd[(size_t) b];
                eM[(size_t) b] += (double) m2 * m2; eS[(size_t) b] += (double) s2 * s2;
                if (anySolo && ! p.mb[(size_t) b].solo) continue;
                om += m2; os += s2;
            }
            const float mg = midSm.tick(), sg = sideSm.tick();
            om *= mg; os *= sg;
            L[i] = om + os; R[i] = om - os;
        }
        for (int b = 0; b < 4; ++b)
        {
            bandGr[(size_t) b].store (p.mbOn ? bandGrDb[(size_t) b] : 0.0f);
            const double tot = eM[(size_t) b] + eS[(size_t) b];
            const float w = tot > 1.0e-12 ? (float) std::sqrt (eS[(size_t) b] / tot) : 0.0f;
            bandWidth[(size_t) b].store (0.8f * bandWidth[(size_t) b].load() + 0.2f * w);
        }

        // ---- saturation (4× oversampled, focus by emphasis) --------------------------------------
        setupSat (p);
        {
            const float drive = dbToGain (p.satDrive);
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                float* x = io[c];
                for (int i = 0; i < n; ++i)
                {
                    satDry[(size_t) c].push (x[i]);
                    satDryBuf[(size_t) i] = satDry[(size_t) c].readInt (kSatOs + 1);
                    float pre = x[i];
                    for (auto& f : satPre[(size_t) c]) pre = f.tick (pre);
                    satIn[(size_t) i] = pre;
                }
                satOver[(size_t) c].up (satIn.data(), n, 4, osT.data());
                for (int i = 0; i < n * 4; ++i) osT[(size_t) i] = curve (osT[(size_t) i] * drive, p.satCurve) / drive;
                satOver[(size_t) c].down (osT.data(), n * 4, 4, satIn.data());
                for (int i = 0; i < n; ++i)
                {
                    float y = satIn[(size_t) i];
                    for (auto& f : satPost[(size_t) c]) y = f.tick (y);
                    const float dry = satDryBuf[(size_t) i];
                    x[i] = p.satOn ? lerp (dry, y, p.satMix) : dry;
                }
            }
        }

        // ---- ambience (parallel early reflections, filtered, quiet) ---------------------------------
        if (p.ambOn)
        {
            std::copy_n (L, n, aL.data()); std::copy_n (R, n, aR.data());
            RoomParams rp; rp.model = 0; rp.size = p.ambSize; rp.decay = p.ambDecay; rp.predelay = 4; rp.damp = 0.5f;
            rp.lowcut = 40; rp.diffusion = 0.75f; rp.mod = 0.2f; rp.gate = 0; rp.width = p.ambWidth; rp.mix = 1.0f;
            room.process (aL.data(), aR.data(), n, rp);
            for (int c = 0; c < 2; ++c) { ambF[(size_t) c][0].set (p.ambHp, 0.707f, (float) sr); ambF[(size_t) c][1].set (p.ambLp, 0.707f, (float) sr); }
            const float lv = dbToGain (p.ambLevel);
            for (int i = 0; i < n; ++i)
            {
                L[i] += ambF[0][1].tickLP (ambF[0][0].tickHP (aL[(size_t) i])) * lv;
                R[i] += ambF[1][1].tickLP (ambF[1][0].tickHP (aR[(size_t) i])) * lv;
            }
        }

        // ---- glue ----------------------------------------------------------------------------------
        if (p.glueOn)
        {
            GlueParams gp; gp.threshold = p.gThr; gp.ratio = p.gRatio; gp.attack = p.gAtt; gp.release = p.gRel; gp.autoRelease = p.gAuto;
            gp.scHp = 60; gp.knee = 8; gp.makeup = p.gMakeup; gp.tape = 0; gp.mix = p.gMix; gp.out = 0;
            glue.process (L, R, n, gp);
            glueGr.store (glue.gr.load());
        }
        else glueGr.store (0);

        // ---- true-peak limiter -----------------------------------------------------------------------
        limiter (L, R, n, p);

        // ---- output trim, dither, bypass / match, listen, meters -----------------------------------
        outSm.target = dbToGain (p.out);
        bypSm.target = p.bypass ? 1.0f : 0.0f;
        // the match gain brings the dry signal to the loudness of the processed one: measured on the processed
        // signal itself (wetMeter), not on what leaves the plug-in, so it holds while compare is on
        const float inLu = inMeter.shortTerm.load(), wetLu = wetMeter.shortTerm.load();
        matchSm.target = (p.match && inLu > -70 && wetLu > -70) ? dbToGain (clampf (wetLu - inLu, -24.0f, 24.0f)) : 1.0f;
        const int totalLat = latencyNoDry();
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            const float og = outSm.tick();
            float l = L[i] * og, r = R[i] * og;
            if (p.dither > 0) { l = dither (l, 0, p); r = dither (r, 1, p); }
            aL[(size_t) i] = l; aR[(size_t) i] = r;
            const float bp = bypSm.tick(), mg = matchSm.tick();
            if (bp > 0.0001f)
            {
                // the whole block was pushed into the dry line at the input: input sample j is (n - j) back,
                // and output sample i belongs to input sample i - latency
                const int d = n - i + totalLat;
                const float dl = dlyDry[0].readInt (d) * mg, dr = dlyDry[1].readInt (d) * mg;
                l = lerp (l, dl, bp); r = lerp (r, dr, bp);
            }
            L[i] = l; R[i] = r;
        }
        wetMeter.process (aL.data(), aR.data(), n);
        outMeter.process (L, R, n);
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            peak = std::max (peak, std::max (tpOut[0].push (l), tpOut[1].push (r)));
            corrLR = 0.9995 * corrLR + (double) l * r; corrLL = 0.9995 * corrLL + (double) l * l; corrRR = 0.9995 * corrRR + (double) r * r;
            postMS.push (0.5f * (l + r), 0.5f * (l - r));
            postLR.push (l, r);
            // listen modes are for monitoring only
            switch (p.listen)
            {
                case 1: { const float m = 0.5f * (l + r); L[i] = R[i] = m; break; }   // mono
                case 2: { const float s = 0.5f * (l - r); L[i] = s; R[i] = s; break; } // side
                case 3: L[i] = r; R[i] = l; break;                                    // swap

                default: break;
            }
        }
        tpHold = std::max (tpHold * 0.99995f, peak);
        truePeak.store (gainToDb (tpHold));
        const double den = std::sqrt (corrLL * corrRR);
        corr.store (den > 1.0e-9 ? (float) (corrLR / den) : 1.0f);
    }
    void resetMeters() { outMeter.reset(); inMeter.reset(); wetMeter.reset(); tpHold = 0; }

private:
    double sr = 48000;
    int N = 8192, lookMax = 240, limDelay = 248;
    std::unique_ptr<FirDesigner> designer;
    std::vector<float> ir, ir2;
    std::array<PartConv, 2> convA;
    std::array<std::array<PartConv, 2>, 2> convDyn;
    std::array<std::array<PartConv, 2>, 3> convB;
    uint64_t lastHashA = 0, lastHashB = 0;
    static constexpr int kMax = 8192;
    std::array<float, kMax> M {}, S {}, eqM {}, eqS {}, bpM {}, bpS {}, aL {}, aR {};
    std::array<std::array<std::array<float, kMax>, 2>, 3> lp {};
    std::array<DelayLine, 2> dlyA, dlyB, dlyDry, satDry;
    std::array<float, 4> bandEnv {}, bandGrDb {};
    std::array<float, 2> dynEnv {}, dynGrDb {};
    std::array<Allpass, 4> airAp;
    std::array<std::array<Biquad, 2>, 2> satPre, satPost;
    int satFocusSet = -1;
    Room room; Glue glue;
    std::array<std::array<Svf, 2>, 2> ambF;
    std::array<TruePeak, 2> tpOut;
    std::array<PeakLimiter, 4> limF; PeakLimiter safety; int lookS = 72;
    std::array<OverN, 2> limOver, satOver;
    std::array<DelayLine, 2> limPad;
    std::vector<float> osL, osR, osT;
    std::array<float, kMax> satIn {}, satDryBuf {};
    std::array<DcBlock, 2> dcIn;
    Smooth midSm, sideSm;
    bool firstBlock = true;
    Smooth matchSm, bypSm, trimSm, outSm, limGainSm;
    double corrLR = 0, corrLL = 0, corrRR = 0; float tpHold = 0;
    Rng rng; std::array<float, 2> ditherErr {};

    int latencyNoDry() const { return 2 * (PartConv::B + N / 2) + kSatOs + limDelay; }
    static constexpr int kSatOs = 39;   // OverN::latencyFor (4)
    std::vector<PartConv*> allConvs()
    {
        std::vector<PartConv*> v { &convA[0], &convA[1] };
        for (auto& d : convDyn) for (auto& c : d) v.push_back (&c);
        for (auto& b : convB) for (auto& c : b) v.push_back (&c);
        return v;
    }
    template <typename Mag>
    bool stageFir (PartConv& c, Mag&& m, bool now)
    {
        designer->design (sr, m, ir.data());
        if (now) { c.loadNow (ir.data(), N); return true; }
        return c.stage (ir.data(), N);
    }
    static uint64_t mix (uint64_t h, float v) { uint32_t u; std::memcpy (&u, &v, 4); return (h ^ u) * 1099511628211ull; }
    static uint64_t hashA (const MasterParams& p)
    {
        uint64_t h = 1469598103934665603ull;
        h = mix (h, p.eqOn ? 1.0f : 0.0f);
        for (auto& b : p.eq) { h = mix (h, b.on ? 1.f : 0.f); h = mix (h, (float) b.type); h = mix (h, b.freq); h = mix (h, b.gain); h = mix (h, b.q); h = mix (h, (float) b.ch); }
        h = mix (h, p.hpOn ? 1.f : 0.f); h = mix (h, p.hpF); h = mix (h, (float) p.hpOrd); h = mix (h, (float) p.hpCh);
        h = mix (h, p.lpOn ? 1.f : 0.f); h = mix (h, p.lpF); h = mix (h, (float) p.lpOrd); h = mix (h, (float) p.lpCh);
        for (auto& d : p.dyn) { h = mix (h, d.freq); h = mix (h, d.q); }
        return h;
    }
    static uint64_t hashB (const MasterParams& p) { uint64_t h = 7; h = mix (h, p.x1); h = mix (h, p.x2); h = mix (h, p.x3); return h; }

    static float curve (float x, int type)
    {
        switch (type)
        {
            case 0: return std::tanh (x + 0.08f) - 0.0798298f;                 // tape: slight asymmetry
            case 1: return x > 0 ? std::tanh (x) : 0.7f * std::tanh (x / 0.7f) * 1.0f; // tube
            case 2: { const float c = clampf (x, -1.5f, 1.5f); return c - c * c * c / 6.75f; }   // soft clip (cubic)
            default: return x / std::sqrt (1.0f + x * x);                       // smooth
        }
    }
    void setupSat (const MasterParams& p)
    {
        if (p.satFocus == satFocusSet) return;
        satFocusSet = p.satFocus;
        // emphasis before the curve, its exact inverse after: focus where harmonics are generated
        float lowDb = 0, highDb = 0, lf = 250, hf = 2500;
        switch (p.satFocus)
        {
            case 1: lowDb = 9; highDb = -9; break;            // lows
            case 2: lowDb = -9; highDb = 9; break;            // highs
            case 3: lowDb = 8; highDb = 8; lf = 200; hf = 2000; break;   // ends: avoid the crowded 200 Hz – 2 kHz
            default: break;
        }
        for (int c = 0; c < 2; ++c)
        {
            satPre[(size_t) c][0].lowShelf (lf, lowDb, (float) sr, 0.7f);  satPre[(size_t) c][1].highShelf (hf, highDb, (float) sr, 0.7f);
            satPost[(size_t) c][0].lowShelf (lf, -lowDb, (float) sr, 0.7f); satPost[(size_t) c][1].highShelf (hf, -highDb, (float) sr, 0.7f);
        }
    }

    /** limiter section: gain → clipper and limiter at 1x / 2x / 4x / 8x → down → pad → true-peak safety */
    void limiter (float* L, float* R, int n, const MasterParams& p)
    {
        static const int fs[4] = { 1, 2, 4, 8 };
        const int fi = p.limOs >= 8 ? 3 : (p.limOs >= 4 ? 2 : (p.limOs >= 2 ? 1 : 0)), f = fs[fi];
        limGainSm.target = dbToGain (p.limOn ? p.limGain : 0.0f);
        for (int i = 0; i < n; ++i) { const float g0 = limGainSm.tick(); L[i] *= g0; R[i] *= g0; }
        const float ceil = dbToGain (p.ceiling);
        limOver[0].up (L, n, f, osL.data()); limOver[1].up (R, n, f, osR.data());
        const int nOs = n * f;
        if (p.clipOn)
        {
            const float drive = dbToGain (p.clipDrive), k = clampf (p.clipSoft, 0.0f, 1.0f);
            for (int i = 0; i < nOs; ++i) { osL[(size_t) i] = clip (osL[(size_t) i] * drive, ceil, k); osR[(size_t) i] = clip (osR[(size_t) i] * drive, ceil, k); }
        }
        const double osr = sr * f;
        const int win = std::max (1, (int) std::round (p.lookahead * 0.001 * osr));
        const float fast = tcoef (std::max (5.0f, p.limRel * 0.3f), (float) osr), slow = tcoef (std::max (20.0f, p.limRel * 2.0f), (float) osr);
        const float rel = tcoef (std::max (5.0f, p.limRel), (float) osr);
        auto& lim = limF[(size_t) fi];
        lim.process (osL.data(), osR.data(), nOs, p.limOn, ceil, win, p.limAuto ? fast : rel, p.limAuto ? slow : rel, p.limAuto, p.limLink);
        limOver[0].down (osL.data(), nOs, f, L); limOver[1].down (osR.data(), nOs, f, R);
        // pad to the 8x latency so the plug-in latency never changes
        const int pad = 43 - (OverN::latencyFor (f) + (f == 1 ? TruePeak::latency : 0));
        for (int i = 0; i < n; ++i)
        {
            limPad[0].push (L[i]); limPad[1].push (R[i]);
            L[i] = limPad[0].readInt (pad + 1); R[i] = limPad[1].readInt (pad + 1);
        }
        // final true-peak safety (catches what the down-sampling filters add back)
        safety.process (L, R, n, p.limOn, ceil, lookS, tcoef (40.0f, (float) sr), tcoef (40.0f, (float) sr), false, 1.0f);
        limGr.store (std::max (lim.grDb, safety.grDb));
    }
    static float clip (float x, float t, float k)
    {
        const float a = std::abs (x), knee = t * (1.0f - k * 0.5f);
        if (a <= knee) return x;
        if (k <= 0.001f) return std::copysign (std::min (a, t), x);
        const float span = t - knee;
        return std::copysign (knee + span * std::tanh ((a - knee) / span), x);
    }

    float dither (float x, int c, const MasterParams& p)
    {
        const float bits = p.dither == 2 ? 16.0f : 24.0f;
        const float q = std::exp2 (bits - 1.0f), lsb = 1.0f / q;
        const float v = x - (p.shaping ? ditherErr[(size_t) c] : 0.0f);
        const float tpdf = (rng.uni() - rng.uni()) * lsb;
        const float out = std::round ((v + tpdf) * q) / q;
        ditherErr[(size_t) c] = out - v;
        return out;
    }
};
} // namespace fal

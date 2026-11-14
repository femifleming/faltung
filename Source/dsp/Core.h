#pragma once
// faltung — shared DSP building blocks. Header-only, no JUCE dependency, allocation-free.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fal
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float x, float lo, float hi) { return std::min (std::max (x, lo), hi); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10 (g); }
/** exponential map of a 0..1 value onto [lo, hi] */
inline float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, clampf (v, 0.0f, 1.0f)); }
inline float semis (float st) { return std::exp2 (st / 12.0f); }
/** time constant coefficient: one-pole that covers ~63% in `ms` */
inline float tcoef (float ms, float sr) { return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (0.001f * ms * sr)); }
/** per-sample multiplier giving -60 dB after `seconds` */
inline float t60coef (float seconds, float sr) { return std::exp (-6.907755f / std::max (1.0e-4f, seconds * sr)); }
inline bool finite (float x) { return std::isfinite (x); }
inline float flushDenorm (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

// -----------------------------------------------------------------------------------------------
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x1234567u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

/** Pink-ish noise (Kellet economy filter) — warmer than white for brushes and air */
struct PinkNoise
{
    float b0 = 0, b1 = 0, b2 = 0;
    float tick (float white)
    {
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return (b0 + b1 + b2 + white * 0.1848f) * 0.22f;
    }
};

// -----------------------------------------------------------------------------------------------
/** Topology-preserving state variable filter (Zavalishin). Stable under fast modulation. */
struct Svf
{
    float g = 0, k = 1.414f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void set (float fc, float q, float sr)
    {
        fc = clampf (fc, 8.0f, sr * 0.48f);
        g = std::tan (kPi * fc / sr);
        k = 1.0f / std::max (q, 0.025f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void tick (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenorm (2.0f * v1 - ic1);
        ic2 = flushDenorm (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    float tickLP (float x) { tick (x); return lp; }
    float tickHP (float x) { tick (x); return hp; }
    float tickBP (float x) { tick (x); return bp; }
    /** band-pass normalised to unity gain at the centre */
    float tickBPn (float x) { tick (x); return bp * k; }
    void reset() { ic1 = ic2 = lp = bp = hp = 0; }
};

/** one-pole low/high pass (matched-z) */
struct OnePole
{
    float a = 0, z = 0;
    void set (float fc, float sr) { a = std::exp (-kTwoPi * clampf (fc, 1.0f, sr * 0.49f) / sr); }
    float lp (float x) { z = flushDenorm (x + a * (z - x)); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

/** DC blocker */
struct DcBlock
{
    float x1 = 0, y1 = 0, r = 0.9995f;
    void set (float sr) { r = 1.0f - (kTwoPi * 12.0f / sr); }
    float tick (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = flushDenorm (y); return y; }
    void reset() { x1 = y1 = 0; }
};

/** RBJ shelving / peaking biquad (transposed direct form II) */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = flushDenorm (b1 * x - a1 * y + z2);
        z2 = flushDenorm (b2 * x - a2 * y);
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void norm (float B0, float B1, float B2, float A0, float A1, float A2)
    { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void lowShelf (float fc, float db, float sr, float S = 0.8f)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, c = std::cos (w), s = std::sin (w);
        const float al = s * 0.5f * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
              (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (float fc, float db, float sr, float S = 0.8f)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, c = std::cos (w), s = std::sin (w);
        const float al = s * 0.5f * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
              (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    void peak (float fc, float q, float db, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = kTwoPi * fc / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
};

// -----------------------------------------------------------------------------------------------
/** A single resonant mode: complex one-pole rotation. Impulse response = decaying sine of unit
    amplitude. Frequency can be changed every sample without blowing up. */
struct Mode
{
    float re = 0, im = 0, c = 1, s = 0, r = 0;
    void set (float hz, float t60, float sr)
    {
        const float w = kTwoPi * clampf (hz, 1.0f, sr * 0.47f) / sr;
        c = std::cos (w); s = std::sin (w);
        r = t60coef (t60, sr);
    }
    void setFreq (float hz, float sr)
    {
        const float w = kTwoPi * clampf (hz, 1.0f, sr * 0.47f) / sr;
        c = std::cos (w); s = std::sin (w);
    }
    float tick (float x)
    {
        const float nr = r * (c * re - s * im) + x;
        const float ni = r * (s * re + c * im);
        re = flushDenorm (nr); im = flushDenorm (ni);
        return im;
    }
    void reset() { re = im = 0; }
};

// -----------------------------------------------------------------------------------------------
/** First-order antiderivative anti-aliased tanh: clean saturation without oversampling. */
struct AdaaTanh
{
    float x1 = 0, F1 = 0;
    static float logcosh (float x)
    {
        const float a = std::abs (x);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
    }
    float tick (float x)
    {
        const float F0 = logcosh (x);
        const float d = x - x1;
        const float y = std::abs (d) < 1.0e-4f ? std::tanh (0.5f * (x + x1)) : (F0 - F1) / d;
        x1 = x; F1 = F0;
        return y;
    }
    void reset() { x1 = 0; F1 = 0; }
};

/** drive stage: unity-ish small-signal gain, tanh soft clip, level compensated */
struct Drive
{
    AdaaTanh sh;
    float tick (float x, float drive01)
    {
        if (drive01 <= 0.001f) return x;
        const float g = 1.0f + drive01 * drive01 * 14.0f;
        return sh.tick (x * g) / std::tanh (g) ;
    }
    void reset() { sh.reset(); }
};

// -----------------------------------------------------------------------------------------------
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
/** band-limited square */
struct BlepSquare
{
    float ph = 0;
    float tick (float hz, float sr)
    {
        const float dt = clampf (hz / sr, 0.0f, 0.45f);
        float v = ph < 0.5f ? 1.0f : -1.0f;
        v += polyBlep (ph, dt);
        float t2 = ph + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
        v -= polyBlep (t2, dt);
        ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        return v;
    }
};

// -----------------------------------------------------------------------------------------------
/** one-pole parameter smoother */
struct Smooth
{
    float cur = 0, target = 0, k = 0.999f;
    void prepare (float sr, float ms = 20.0f) { k = tcoef (ms, sr); }
    void snap (float v) { cur = target = v; }
    float tick() { cur = target + k * (cur - target); return cur; }
};

/** constant-power pan, p in -1..1 */
inline void panGains (float p, float& l, float& r)
{
    const float a = (clampf (p, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (a) * 1.41421356f; r = std::sin (a) * 1.41421356f;
    l = std::min (l, 1.0f); r = std::min (r, 1.0f);
}

// -----------------------------------------------------------------------------------------------
/** fractional delay line with 4-point Hermite interpolation; power-of-two ring */
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    /** delay in samples (>= 1) measured from the most recently pushed sample */
    float read (float d) const
    {
        d = clampf (d, 1.0f, (float) (mask - 3));
        const float rp = (float) w - d;
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
        const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
};

/** Schroeder allpass with integer delay */
struct Allpass
{
    std::vector<float> buf; int idx = 0, len = 1;
    void prepare (int n) { len = std::max (1, n); buf.assign ((size_t) len, 0.0f); idx = 0; }
    float tick (float x, float g)
    {
        const float d = buf[(size_t) idx];
        const float v = x + g * d;
        buf[(size_t) idx] = flushDenorm (v);
        if (++idx >= len) idx = 0;
        return d - g * v;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

/** Hermite read from an arbitrary buffer (sample playback) */
inline float hermiteAt (const float* d, int n, double pos)
{
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return (k >= 0 && k < n) ? d[k] : 0.0f; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
} // namespace fal

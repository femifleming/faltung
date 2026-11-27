#pragma once
// umleitung — the signal processing: filters (the eqs), level, channels, dynamics, generators and the meters.
// Real-time safe: everything is sized in prepare(); process() never allocates.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
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

// ---- the hi-fi eq: bands designed by matching the analog filter ------------------------------------------------
// Every band is an analog prototype (the ideal filter). The digital biquads are "matched" to it (M. Vicanek,
// Matched Second Order Digital Filters, 2016): the poles are mapped exactly (z = e^(sT)), and the numerator is solved
// so the magnitude equals the analog one at 0 hz, at the band's frequency and at nyquist. Unlike the usual bilinear
// (cookbook) filters, a bell or a shelf near the top keeps its shape: no cramping at 10 - 20 khz. All in double.
enum Shape { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, AllPass, Tilt, NumShapes };
inline const char* shapeName (int s) { static const char* n[] = { "bell", "low shelf", "high shelf", "low cut", "high cut", "notch", "band pass", "all-pass", "tilt" }; return n[juce::jlimit (0, NumShapes - 1, s)]; }

/** a band as the eqs describe it: slope (db / octave, the cuts: 6 … 48), kind (cuts: 0 butterworth, 1 linkwitz-riley),
    ch (0 every channel, n the n-th) */
struct Band { int shape = Bell; double freq = 1000, db = 0, q = 0.7071; int slope = 12, kind = 0, ch = 0; bool on = true; };

/** one analog section in s normalised to the band's frequency: (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0); first: first order */
struct Analog { double n2 = 0, n1 = 0, n0 = 1, d2 = 0, d1 = 0, d0 = 1; double f0 = 1000; bool first = false; double gain = 1; };

inline std::vector<Analog> analogSections (const Band& b)
{
    std::vector<Analog> v;
    const double f = b.freq, q = std::max (0.025, b.q), A = std::pow (10.0, b.db / 40.0), sa = std::sqrt (A);
    auto sec2 = [&] (double n2, double n1, double n0, double d2, double d1, double d0, double gain = 1) { Analog x; x.n2 = n2; x.n1 = n1; x.n0 = n0; x.d2 = d2; x.d1 = d1; x.d0 = d0; x.f0 = f; x.gain = gain; v.push_back (x); };
    auto sec1 = [&] (bool high) { Analog x; x.first = true; x.n1 = high ? 1 : 0; x.n0 = high ? 0 : 1; x.d1 = 1; x.d0 = 1; x.f0 = f; v.push_back (x); };
    switch (b.shape)
    {
        case Bell:      sec2 (1, A / q, 1, 1, 1 / (A * q), 1); break;
        case LowShelf:  sec2 (A * 1, A * sa / q, A * A, A, sa / q, 1); break;
        case HighShelf: sec2 (A * A, A * sa / q, A * 1, 1, sa / q, A); break;
        case Notch:     sec2 (1, 0, 1, 1, 1 / q, 1); break;
        case BandPass:  sec2 (0, 1 / q, 0, 1, 1 / q, 1); break;
        case AllPass:   sec2 (1, -1 / q, 1, 1, 1 / q, 1); break;
        case Tilt:      sec2 (A * A, A * sa / 0.5, A * 1, 1, sa / 0.5, A, 1.0 / A); break;   // a gentle high shelf, the middle at 0 db
        case LowCut: case HighCut:
        {
            const bool high = b.shape == LowCut;
            int order = juce::jlimit (1, 8, (int) std::lround (b.slope / 6.0));
            auto butter = [&] (int n, double userQ)
            {
                if (n % 2 == 1) sec1 (high);
                for (int k = 1; k <= n / 2; ++k)
                {
                    const double phi = (n % 2 == 0) ? (2.0 * k - 1.0) * dsp::kPi / (2.0 * n) : k * dsp::kPi / n;
                    const double qq = (n == 2 && userQ > 0) ? userQ : 1.0 / (2.0 * std::cos (phi));
                    if (high) sec2 (1, 0, 0, 1, 1 / qq, 1); else sec2 (0, 0, 1, 1, 1 / qq, 1);
                }
            };
            if (b.kind == 1) { order = std::max (2, order - order % 2); butter (order / 2, -1); butter (order / 2, -1); }   // linkwitz-riley: butterworth squared
            else butter (order, order == 2 ? q : -1);
            break;
        }
        default: break;
    }
    return v;
}
/** the analog magnitude² of a section at a frequency */
inline double analogMag2 (const Analog& a, double hz)
{
    const double x = hz / a.f0;
    if (a.first) { const double nr = a.n0, ni = a.n1 * x, dr = a.d0, di = a.d1 * x; return a.gain * a.gain * (nr * nr + ni * ni) / std::max (1.0e-300, dr * dr + di * di); }
    const double nr = a.n0 - a.n2 * x * x, ni = a.n1 * x, dr = a.d0 - a.d2 * x * x, di = a.d1 * x;
    return a.gain * a.gain * (nr * nr + ni * ni) / std::max (1.0e-300, dr * dr + di * di);
}
/** the matched digital section */
inline Coefs matched (const Analog& a, double fs)
{
    const double T = 1.0 / fs, w0 = 2.0 * kPi * std::min (a.f0, fs * 0.4999);
    Analog an = a; an.f0 = std::min (a.f0, fs * 0.4999);
    if (a.first)
    {
        const double z = std::exp (-w0 * T), a1 = -z;
        const double h0 = std::sqrt (analogMag2 (an, 0.0)), hn = std::sqrt (analogMag2 (an, fs * 0.5));
        // (b0 + b1) / (1 + a1) = h0, (b0 - b1) / (1 - a1) = hn
        const double sum = h0 * (1 + a1), diff = hn * (1 - a1);
        return { 0.5 * (sum + diff), 0.5 * (sum - diff), 0, a1, 0 };
    }
    // the poles, exactly: roots of d2 s² + d1 s + d0 (s normalised), then z = e^(s w0 T)
    double a1, a2;
    const double d2 = std::max (1.0e-12, a.d2), bb = a.d1 / d2, cc = a.d0 / d2, disc = bb * bb - 4 * cc;
    if (disc < 0) { const double re = -0.5 * bb * w0 * T, im = 0.5 * std::sqrt (-disc) * w0 * T; a1 = -2.0 * std::exp (re) * std::cos (im); a2 = std::exp (2.0 * re); }
    else { const double r1 = (-bb + std::sqrt (disc)) * 0.5 * w0 * T, r2 = (-bb - std::sqrt (disc)) * 0.5 * w0 * T; a1 = -(std::exp (r1) + std::exp (r2)); a2 = std::exp (r1 + r2); }
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
    auto phis = [] (double w, double& p0, double& p1, double& p2) { p1 = std::sin (w * 0.5); p1 *= p1; p0 = 1 - p1; p2 = 4 * p0 * p1; };
    auto den = [&] (double w) { double p0, p1, p2; phis (w, p0, p1, p2); return A0 * p0 + A1 * p1 + A2 * p2; };
    const double B0 = analogMag2 (an, 0.0) * A0, B1 = analogMag2 (an, fs * 0.5) * A1;
    const double wm = w0 * T;   // match at the band's own frequency
    double p0, p1, p2; phis (wm, p0, p1, p2);
    const double B2 = (analogMag2 (an, an.f0) * den (wm) - B0 * p0 - B1 * p1) / std::max (1.0e-300, p2);
    const double sB0 = std::sqrt (std::max (0.0, B0)), sB1 = std::sqrt (std::max (0.0, B1));
    const double W = 0.5 * (sB0 + sB1);
    const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
    const double b1 = 0.5 * (sB0 - sB1), b2 = W - b0;
    // an all-pass keeps its phase: the numerator is the denominator mirrored
    if (a.n1 < 0 && a.n2 == a.d2 && a.n0 == a.d0) return { a2, a1, 1.0, a1, a2 };
    return { b0, b1, b2, a1, a2 };
}
/** every section of a band (a steep cut is several) */
inline std::vector<Coefs> designBand (const Band& b, double fs) { std::vector<Coefs> v; if (b.on) for (auto& s : analogSections (b)) v.push_back (matched (s, fs)); return v; }
/** per channel (the bands meant for it and the ones for all) */
inline std::vector<std::vector<Coefs>> designBank (const std::vector<Band>& bands, double fs, int channels)
{
    std::vector<std::vector<Coefs>> v ((size_t) std::max (1, channels));
    for (auto& b : bands) { if (! b.on) continue; auto k = designBand (b, fs); for (int c = 0; c < (int) v.size(); ++c) if (b.ch == 0 || b.ch == c + 1) v[(size_t) c].insert (v[(size_t) c].end(), k.begin(), k.end()); }
    return v;
}
/** the ideal (analog) response of bands on a channel, in db */
inline double analogDb (const std::vector<Band>& bands, double hz, int channel = 0)
{
    double m2 = 1; for (auto& b : bands) if (b.on && (b.ch == 0 || b.ch == channel + 1)) for (auto& s : analogSections (b)) m2 *= analogMag2 (s, hz);
    return 10.0 * std::log10 (std::max (1.0e-30, m2));
}
/** what the digital filters really do (for the curve on the page) */
inline double digitalDb (const std::vector<Band>& bands, double fs, double hz, int channel = 0)
{
    double d = 0; for (auto& b : bands) if (b.on && (b.ch == 0 || b.ch == channel + 1)) for (auto& k : designBand (b, fs)) d += magnitudeDb (k, fs, hz);
    return d;
}

/** a bank of biquads, its own list per channel; a new set is swapped in at the start of a block */
class FilterBank
{
public:
    void prepare (int channels, double fs) { sr = fs; state.assign ((size_t) channels, std::vector<Biquad> (256)); }
    /** message thread */
    void set (std::vector<std::vector<Coefs>> perChannel)
    {
        for (auto& v : perChannel) if (v.size() > 256) v.resize (256);
        const juce::SpinLock::ScopedLockType l (lock); pending = std::move (perChannel); dirty = true;
    }
    void set (const std::vector<Band>& bands) { set (designBank (bands, sr, std::max (1, (int) state.size()))); }
    double sampleRate() const { return sr; }
    void process (juce::AudioBuffer<float>& b, int n)
    {
        if (dirty.load()) { const juce::SpinLock::ScopedTryLockType l (lock); if (l.isLocked()) { std::swap (active, pending); dirty = false; } }
        const int ch = std::min (b.getNumChannels(), (int) state.size());
        for (int c = 0; c < ch; ++c)
        {
            if (active.empty()) return;
            const auto& ks = active[(size_t) std::min (c, (int) active.size() - 1)];
            auto& st = state[(size_t) c];
            if (st.size() < ks.size()) continue;   // (sized in prepare: 256 sections)
            auto* x = b.getWritePointer (c);
            for (size_t f = 0; f < ks.size(); ++f) { auto& q = st[f]; q.k = ks[f]; for (int i = 0; i < n; ++i) x[i] = q.tick (x[i]); }
            for (int i = 0; i < n; ++i) if (! std::isfinite (x[i])) { for (auto& q : st) q.reset(); std::fill (x, x + n, 0.0f); break; }
        }
    }
private:
    std::vector<std::vector<Coefs>> pending, active; juce::SpinLock lock; std::atomic<bool> dirty { false };
    std::vector<std::vector<Biquad>> state; double sr = 48000;
};

/** the graphic eqs: constant-q bells on the iso centres (10 bands an octave wide, 31 a third of an octave) */
inline const std::vector<float>& graphicCentres (int count)
{
    static const std::vector<float> ten { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    static const std::vector<float> third { 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    return count >= 31 ? third : ten;
}
inline std::vector<Band> graphicBandsOf (const std::vector<float>& gains)
{
    const auto& f = graphicCentres ((int) gains.size());
    const double q = gains.size() >= 31 ? 4.318 : 1.414;   // a third of an octave / an octave
    std::vector<Band> v;
    for (size_t i = 0; i < gains.size() && i < f.size(); ++i) if (std::abs (gains[i]) > 1.0e-4f) { Band b; b.shape = Bell; b.freq = f[i]; b.db = gains[i]; b.q = q; v.push_back (b); }
    return v;
}

/** a linear-phase fir with the bands' exact (analog) magnitude, per channel: n taps, its delay n / 2 */
inline juce::AudioBuffer<float> linearPhaseIR (const std::vector<Band>& bands, double gainDb, double fs, int channels, int order)
{
    const int n = 1 << order;
    juce::AudioBuffer<float> ir (channels, n);
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2);
    const double g = std::pow (10.0, gainDb / 20.0);
    for (int c = 0; c < channels; ++c)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double hz = (double) k * fs / n;
            const double m = g * std::pow (10.0, analogDb (bands, std::max (0.01, hz), c) / 20.0);
            buf[(size_t) (2 * k)] = (float) m; buf[(size_t) (2 * k + 1)] = 0.0f;   // zero phase
        }
        fft.performRealOnlyInverseTransform (buf.data());
        auto* y = ir.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const int k = (i + n / 2) % n;                                         // centred: the delay is n / 2
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (i + 0.5) / n);     // hann
            y[i] = (float) (buf[(size_t) k] * w);
        }
    }
    return ir;
}

/** a minimum-phase fir with the bands' exact (analog) magnitude: the same level at every frequency as the ideal curve,
    the least delay any filter with that magnitude can have (the real-cepstrum method: log magnitude → cepstrum →
    folded → exponent → impulse) */
inline juce::AudioBuffer<float> minimumPhaseIR (const std::vector<Band>& bands, double gainDb, double fs, int channels, int order)
{
    const int n = 1 << order;
    juce::AudioBuffer<float> ir (channels, n);
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) n), b ((size_t) n);
    const double g = std::pow (10.0, gainDb / 20.0);
    for (int c = 0; c < channels; ++c)
    {
        // log magnitude on the whole circle (real, even)
        for (int k = 0; k <= n / 2; ++k)
        {
            const double hz = std::max (0.01, (double) k * fs / n);
            const double lm = std::log (std::max (1.0e-12, g * std::pow (10.0, analogDb (bands, hz, c) / 20.0)));
            a[(size_t) k] = { (float) lm, 0.0f }; if (k > 0 && k < n / 2) a[(size_t) (n - k)] = { (float) lm, 0.0f };
        }
        fft.perform (a.data(), b.data(), true);                       // the real cepstrum (inverse transform)
        // fold it: what is causal doubles, the rest goes
        for (int k = 0; k < n; ++k) { const float re = b[(size_t) k].real(); a[(size_t) k] = { k == 0 || k == n / 2 ? re : (k < n / 2 ? 2.0f * re : 0.0f), 0.0f }; }
        fft.perform (a.data(), b.data(), false);                      // back: log of the minimum-phase spectrum
        for (int k = 0; k < n; ++k) b[(size_t) k] = std::exp (b[(size_t) k]);
        fft.perform (b.data(), a.data(), true);                       // the impulse response
        auto* y = ir.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const double fade = i < n * 3 / 4 ? 1.0 : 0.5 + 0.5 * std::cos (kPi * (i - n * 3.0 / 4.0) / (n / 4.0));   // the tail out smoothly
            y[i] = (float) (a[(size_t) i].real() * fade);
        }
    }
    return ir;
}

/** speakers: per channel a trim, a delay (to a fraction of a sample: whole samples, then a thiran all-pass for the
    rest, so the level stays flat), polarity and mute */
struct SpeakerAlign
{
    struct Ch { float gain = 1; int whole = 0; double frac = 0, ap = 0, z = 0; };
    std::vector<std::vector<float>> line; std::vector<Ch> ch; int w = 0, len = 1; double sr = 48000;
    void prepare (double fs, int channels) { sr = fs; len = (int) (0.2 * fs) + 4; line.assign ((size_t) channels, std::vector<float> ((size_t) len, 0.0f)); ch.assign ((size_t) channels, {}); w = 0; }
    /** message thread: gains (linear, sign = polarity, 0 = mute) and delays (ms) */
    void set (const std::vector<float>& gains, const std::vector<double>& ms)
    {
        const juce::SpinLock::ScopedLockType l (lock); pg = gains; pm = ms; dirty = true;
    }
    void process (juce::AudioBuffer<float>& b, int n)
    {
        if (dirty.load()) { const juce::SpinLock::ScopedTryLockType l (lock); if (l.isLocked())
        {
            for (size_t c = 0; c < ch.size(); ++c)
            {
                ch[c].gain = c < pg.size() ? pg[c] : 1.0f;
                const double d = juce::jlimit (0.0, (len - 3) / sr * 1000.0, c < pm.size() ? pm[c] : 0.0) * 0.001 * sr;
                // thiran first-order all-pass delays well around 1 sample: whole = floor (d) - 1 when there is room, frac in [1, 2)
                int whole = (int) std::floor (d); double fr = d - whole;
                if (whole >= 1 && fr < 0.5) { whole -= 1; fr += 1.0; }
                if (ch[c].whole != whole || std::abs (ch[c].frac - fr) > 1.0e-12) { ch[c].whole = whole; ch[c].frac = fr; ch[c].ap = fr > 1.0e-9 ? (1.0 - fr) / (1.0 + fr) : 0.0; }
            }
            dirty = false;
        } }
        const int C = std::min (b.getNumChannels(), (int) ch.size());
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < C; ++c)
            {
                auto& L = line[(size_t) c]; auto& k = ch[(size_t) c];
                L[(size_t) w] = b.getSample (c, i);
                const float x = L[(size_t) ((w - k.whole + len) % len)];
                double y = x;
                if (k.frac > 1.0e-9) { y = k.ap * x + k.z; k.z = x - k.ap * y; }   // y[n] = a x[n] + x[n-1] - a y[n-1]
                b.setSample (c, i, (float) (y * k.gain));
            }
            w = (w + 1) % len;
        }
    }
    std::vector<float> pg; std::vector<double> pm; juce::SpinLock lock; std::atomic<bool> dirty { false };
};

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

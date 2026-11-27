#pragma once
// sndstudio — loudness as streaming services and broadcasters measure it (ITU-R BS.1770-4 / EBU R128): k-weighted
// mean square in 100 ms steps; momentary (400 ms), short-term (3 s), integrated (gated: -70 LUFS absolute, -10 LU
// relative, over 400 ms blocks overlapping by 75 %), loudness range (EBU Tech 3342: the short-term values gated at
// -70 / -20, from the 10th to the 95th percentile), and true peak (4x oversampled, dBTP).
// The audio thread calls process(); the message thread calls update() and reads the numbers.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace fal::daw
{
class LoudnessMeter
{
public:
    static constexpr float kSilent = -144.0f;
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // the k-weighting filter at this rate (the two stages of BS.1770's annex, from their analogue prototypes)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        step = std::max (1, (int) std::lround (sr * 0.1));
        // the true-peak interpolator: a windowed sinc, 4 phases of 12 taps
        for (int p = 0; p < 4; ++p)
            for (int k = 0; k < kTaps; ++k)
            {
                const double x = (k - kTaps / 2 + 1) - p / 4.0;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / (kTaps / 2.0));
                fir[(size_t) p][(size_t) k] = (float) (sinc * std::max (0.0, w));
            }
        resetAudio();
    }
    void resetAudio() { for (auto& c : ch) c = {}; acc = 0; accN = 0; }
    /** the audio thread: the first `n` samples of up to two channels */
    void process (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            double sq = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float* x = c == 0 ? L : (R ? R : nullptr);
                if (! x) continue;
                auto& st = ch[(size_t) c];
                const double in = x[i];
                const double y1 = s1[0] * in + st.z1a; st.z1a = s1[1] * in - s1[3] * y1 + st.z1b; st.z1b = s1[2] * in - s1[4] * y1;
                const double y2 = s2[0] * y1 + st.z2a; st.z2a = s2[1] * y1 - s2[3] * y2 + st.z2b; st.z2b = s2[2] * y1 - s2[4] * y2;
                sq += y2 * y2;
                // true peak: the sample and the three between it and the last
                st.hist[(size_t) st.hp] = (float) in; st.hp = (st.hp + 1) % kTaps;
                float pk = std::abs ((float) in);
                for (int p = 1; p < 4; ++p)
                {
                    float v = 0; for (int k = 0; k < kTaps; ++k) v += fir[(size_t) p][(size_t) k] * st.hist[(size_t) ((st.hp + k) % kTaps)];
                    pk = std::max (pk, std::abs (v));
                }
                if (pk > tpHold) tpHold = pk;
            }
            if (! R && L) sq *= 2.0;   // (mono counts as both sides)
            acc += sq; if (++accN >= step) { push ((float) (acc / accN)); acc = 0; accN = 0; }
        }
        float cur = truePeak.load (std::memory_order_relaxed);
        if (tpHold > cur) truePeak.store (tpHold, std::memory_order_relaxed);
        tpHold = 0;
    }
    /** the message thread: takes the 100 ms steps measured since the last call and works the numbers out */
    void update()
    {
        int s1i, n1, s2i, n2; const int r = fifo.getNumReady(); fifo.prepareToRead (r, s1i, n1, s2i, n2);
        for (int i = 0; i < n1; ++i) addStep (buf[(size_t) (s1i + i)]);
        for (int i = 0; i < n2; ++i) addStep (buf[(size_t) (s2i + i)]);
        fifo.finishedRead (n1 + n2);
    }
    void reset() { stepCount = 0; steps.clear(); blocks.clear(); shorts.clear(); momentary = shortTerm = integrated = kSilent; range = 0; truePeak.store (0); maxMomentary = kSilent; }
    float momentary = kSilent, shortTerm = kSilent, integrated = kSilent, range = 0, maxMomentary = kSilent;
    std::atomic<float> truePeak { 0 };
    float truePeakDb() const { const float t = truePeak.load(); return t > 1.0e-9f ? 20.0f * std::log10 (t) : kSilent; }
    static float lufs (double meanSquare) { return meanSquare > 1.0e-15 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : kSilent; }
    /** a whole buffer measured at once (offline): its integrated loudness and true peak */
    static void measure (const juce::AudioBuffer<float>& b, double sampleRate, float& integratedLufs, float& truePeakDb, float* rangeLu = nullptr)
    {
        LoudnessMeter m; m.prepare (sampleRate);
        const int n = b.getNumSamples(), bs = 4096;
        for (int i = 0; i < n; i += bs) { m.process (b.getReadPointer (0) + i, b.getNumChannels() > 1 ? b.getReadPointer (1) + i : nullptr, std::min (bs, n - i)); m.update(); }
        integratedLufs = m.integrated; truePeakDb = m.truePeakDb(); if (rangeLu) *rangeLu = m.range;
    }
private:
    void push (float ms) { int a, na, b2, nb; fifo.prepareToWrite (1, a, na, b2, nb); if (na > 0) { buf[(size_t) a] = ms; fifo.finishedWrite (1); } }
    void addStep (float ms)
    {
        steps.push_back (ms);
        auto meanOf = [&] (size_t k) { if (steps.size() < k) return -1.0; double s = 0; for (size_t i = steps.size() - k; i < steps.size(); ++i) s += steps[i]; return s / (double) k; };
        if (const double m4 = meanOf (4); m4 >= 0) { momentary = lufs (m4); blocks.push_back (m4); maxMomentary = std::max (maxMomentary, momentary); }
        ++stepCount;
        if (const double m30 = meanOf (30); m30 >= 0) { shortTerm = lufs (m30); if (stepCount % 10 == 0) shorts.push_back (shortTerm); }   // (a short-term value a second, for the range)
        if (steps.size() > 40) steps.erase (steps.begin(), steps.begin() + (long) (steps.size() - 40));
        // integrated: the 400 ms blocks above -70, then above (their mean - 10 LU)
        double s = 0; int k = 0;
        for (double bl : blocks) if (lufs (bl) > -70.0f) { s += bl; ++k; }
        if (k > 0)
        {
            const float rel = lufs (s / k) - 10.0f; double s2 = 0; int k2 = 0;
            for (double bl : blocks) if (lufs (bl) > -70.0f && lufs (bl) > rel) { s2 += bl; ++k2; }
            integrated = k2 > 0 ? lufs (s2 / k2) : kSilent;
        }
        // loudness range: the short-term values above -70, then above (their integrated - 20 LU), 10th to 95th percentile
        std::vector<float> v; double e = 0; int ke = 0;
        for (float st : shorts) if (st > -70.0f) { e += std::pow (10.0, (st + 0.691) / 10.0); ++ke; }
        if (ke > 1)
        {
            const float rel = (float) (-0.691 + 10.0 * std::log10 (e / ke)) - 20.0f;
            for (float st : shorts) if (st > -70.0f && st > rel) v.push_back (st);
            if (v.size() > 1) { std::sort (v.begin(), v.end()); range = v[(size_t) std::lround (0.95 * (v.size() - 1))] - v[(size_t) std::lround (0.10 * (v.size() - 1))]; }
        }
    }
    struct Ch { double z1a = 0, z1b = 0, z2a = 0, z2b = 0; std::array<float, 12> hist {}; int hp = 0; };
    static constexpr int kTaps = 12;
    double sr = 48000; std::array<double, 5> s1 {}, s2 {};
    std::array<Ch, 2> ch {}; std::array<std::array<float, kTaps>, 4> fir {};
    double acc = 0; int accN = 0, step = 4800; float tpHold = 0;
    juce::AbstractFifo fifo { 512 }; std::array<float, 512> buf {};
    std::vector<double> steps, blocks; std::vector<float> shorts; long long stepCount = 0;
};
} // namespace fal::daw

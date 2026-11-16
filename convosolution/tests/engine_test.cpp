// Stand-alone check of ConvEngine against a direct convolution, plus a speed figure.
#include "../Source/dsp/ConvEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using namespace ecr;

static int runCase (int irLen, int paths, int seed)
{
    std::mt19937 rng ((unsigned) seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    std::vector<float> h[4];
    const float* hp[4] = { nullptr, nullptr, nullptr, nullptr };
    for (int p = 0; p < paths; ++p)
    {
        h[p].resize ((size_t) irLen);
        for (int i = 0; i < irLen; ++i) h[p][(size_t) i] = u (rng) * std::exp (-3.0f * (float) i / (float) irLen);
        hp[p] = h[p].data();
    }
    const int n = irLen + 150000;
    // sparse input, so the reference is cheap to compute exactly
    std::vector<float> x[2] = { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
    std::vector<std::pair<int, int>> hits;
    std::uniform_int_distribution<int> pos (0, 140000);
    for (int k = 0; k < 40; ++k)
    {
        const int c = k & 1, t = k == 0 ? 0 : pos (rng);
        x[c][(size_t) t] += u (rng);
    }
    for (int c = 0; c < 2; ++c)
        for (int t = 0; t < n; ++t)
            if (x[c][(size_t) t] != 0.0f) hits.push_back ({ c, t });
    std::vector<double> ref[2] = { std::vector<double> ((size_t) n, 0.0), std::vector<double> ((size_t) n, 0.0) };
    static const int inOf[4] = { 0, 1, 0, 1 }, outOf[4] = { 0, 1, 1, 0 };
    for (auto [c, t] : hits)
        for (int p = 0; p < paths; ++p)
            if (inOf[p] == c)
                for (int i = 0; i < irLen && t + i < n; ++i)
                    ref[outOf[p]][(size_t) (t + i)] += (double) x[c][(size_t) t] * h[p][(size_t) i];

    ConvEngine e (hp, irLen, seed);
    std::vector<float> y[2] = { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    std::uniform_int_distribution<int> blk (1, 700);
    for (int done = 0; done < n;)
    {
        const int m = std::min (n - done, blk (rng));
        e.process (x[0].data() + done, x[1].data() + done, y[0].data() + done, y[1].data() + done, m);
        done += m;
    }
    double err = 0, mag = 0;
    for (int c = 0; c < 2; ++c)
        for (int t = 0; t < n; ++t)
        {
            err = std::max (err, std::fabs ((double) y[c][(size_t) t] - ref[c][(size_t) t]));
            mag = std::max (mag, std::fabs (ref[c][(size_t) t]));
        }
    const bool ok = err < 2e-4 * std::max (1.0, mag);
    std::printf ("len %8d paths %d  max err %.2e (peak %.2f)  %s\n", irLen, paths, err, mag, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main()
{
    int fails = 0;
    const int lens[] = { 1, 37, 64, 65, 1000, 1024, 1025, 8192, 8193, 30000, 65536, 65537, 200000, 1104000 };
    int seed = 1;
    for (int len : lens)
        for (int paths : { 1, 2, 4 })
            fails += runCase (len, paths, seed++);

    // speed: 23 s true-stereo response at 48 kHz, 64-sample callbacks
    {
        const int len = 23 * 48000;
        std::vector<float> h ((size_t) len, 0.001f);
        const float* hp[4] = { h.data(), h.data(), h.data(), h.data() };
        ConvEngine e (hp, len, 0);
        std::vector<float> in (64, 0.1f), oL (64), oR (64);
        const int blocks = 48000 * 20 / 64;
        double worst = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            const auto a = std::chrono::steady_clock::now();
            e.process (in.data(), in.data(), oL.data(), oR.data(), 64);
            worst = std::max (worst, std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count());
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("23 s true stereo: %.1f%% of one core, worst 64-sample callback %.0f us (budget 1333 us)\n",
                     100.0 * secs / 20.0, worst * 1e6);
    }
    std::printf (fails ? "FAILED: %d\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}

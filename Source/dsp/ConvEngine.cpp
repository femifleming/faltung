#include "ConvEngine.h"
#include "pffft.h"
#include <algorithm>
#include <map>
#include <mutex>

namespace ecr
{
void ABuf::alloc (size_t size)
{
    release();
    n = size;
    if (n > 0)
    {
        d = static_cast<float*> (pffft_aligned_malloc (n * sizeof (float)));
        std::memset (d, 0, n * sizeof (float));
    }
}

void ABuf::release()
{
    if (d != nullptr) pffft_aligned_free (d);
    d = nullptr;
    n = 0;
}

// PFFFT setups are read-only once made, so engines share them.
static PFFFT_Setup* setupFor (int fftSize)
{
    static std::mutex m;
    static std::map<int, PFFFT_Setup*> cache;
    std::lock_guard<std::mutex> lock (m);
    auto& s = cache[fftSize];
    if (s == nullptr) s = pffft_new_setup (fftSize, PFFFT_REAL);
    return s;
}

static const int inOfPath[4]  = { 0, 1, 0, 1 };
static const int outOfPath[4] = { 0, 1, 1, 0 };

ConvEngine::ConvEngine (const float* const h[4], int len, int phaseSeed) : length (std::max (1, len))
{
    for (int p = 0; p < 4; ++p)
        if (h[p] != nullptr)
            pathIdx[numPaths++] = p;

    for (int c = 0; c < 2; ++c) hist[c].alloc ((size_t) (2 * kHead));

    for (int a = 0; a < numPaths; ++a)
    {
        const int p = pathIdx[a];
        headRev[p].alloc ((size_t) kHead);
        for (int j = 0; j < std::min (kHead, len); ++j)
            headRev[p].d[kHead - 1 - j] = h[p][j];
    }

    // block size, index of the first partition (1 = no slack, 2 = one block of slack), tap range
    struct Plan { int N, first, start, end; };
    static const Plan plan[] = { { 64, 1, 64, 1024 }, { 512, 2, 1024, 8192 }, { 4096, 2, 8192, 65536 }, { 32768, 2, 65536, 1 << 30 } };

    for (const auto& pl : plan)
    {
        if (len <= pl.start) break;
        auto l = std::make_unique<Level>();
        l->N = pl.N;
        l->first = pl.first;
        l->sync = pl.first == 1;
        const int covered = std::min (len, pl.end) - pl.start;
        l->P = (covered + pl.N - 1) / pl.N;
        const size_t F = (size_t) (2 * pl.N);
        l->fft = setupFor ((int) F);
        l->work.alloc (F);
        l->tmp.alloc (F);
        for (int c = 0; c < 2; ++c)
        {
            l->X[c].alloc (F * (size_t) l->P);
            l->in[c].alloc (F);
            l->fftIn[c].alloc (F);
            l->acc[c].alloc (F);
            l->out[c].alloc ((size_t) pl.N);
            l->next[c].alloc ((size_t) pl.N);
        }
        for (int a = 0; a < numPaths; ++a)
        {
            const int p = pathIdx[a];
            l->H[p].alloc (F * (size_t) l->P);
            for (int q = 0; q < l->P; ++q)
            {
                const int from = pl.start + q * pl.N;
                const int count = std::min (pl.N, len - from);
                std::memset (l->tmp.d, 0, F * sizeof (float));
                if (count > 0) std::memcpy (l->tmp.d, h[p] + from, (size_t) count * sizeof (float));
                pffft_transform (l->fft, l->tmp.d, l->H[p].d + F * (size_t) q, l->work.d, PFFFT_FORWARD);
            }
        }
        l->units = numPaths * l->P;
        l->numSteps = 2 + (l->units + kMacPerStep - 1) / kMacPerStep + 2;
        if (pl.N > 64)
            l->pos = (((phaseSeed % 4) * (pl.N / 4) + (phaseSeed % 4 ? pl.N / 8 : 0)) / 64 * 64) % pl.N;
        levels.push_back (std::move (l));
    }
}

ConvEngine::~ConvEngine() = default;

void ConvEngine::runStep (Level& l)
{
    const size_t F = (size_t) (2 * l.N);
    const int s = l.step++;
    const int macSteps = l.numSteps - 4;

    if (s < 2)
    {
        if (s == 0)
            for (int c = 0; c < 2; ++c) std::memset (l.acc[c].d, 0, F * sizeof (float));
        pffft_transform (l.fft, l.fftIn[s].d, l.X[s].d + F * (size_t) (l.jobBlock % l.P), l.work.d, PFFFT_FORWARD);
    }
    else if (s < 2 + macSteps)
    {
        const float scale = 1.0f / (float) F;
        const int u0 = (s - 2) * kMacPerStep;
        const int u1 = std::min (l.units, u0 + kMacPerStep);
        for (int u = u0; u < u1; ++u)
        {
            const int p = pathIdx[u % numPaths];
            const int q = u / numPaths;
            const long xb = ((l.jobBlock - q) % l.P + l.P) % l.P;
            pffft_zconvolve_accumulate (l.fft, l.X[inOfPath[p]].d + F * (size_t) xb, l.H[p].d + F * (size_t) q,
                                        l.acc[outOfPath[p]].d, scale);
        }
    }
    else
    {
        const int c = s - 2 - macSteps;
        pffft_transform (l.fft, l.acc[c].d, l.tmp.d, l.work.d, PFFFT_BACKWARD);
        std::memcpy (l.next[c].d, l.tmp.d + l.N, (size_t) l.N * sizeof (float));
        if (c == 1) l.jobActive = false;
    }
}

void ConvEngine::boundary (Level& l)
{
    const size_t Nb = (size_t) l.N * sizeof (float);
    if (! l.sync)
    {
        finishJob (l);                        // normally already done
        for (int c = 0; c < 2; ++c) std::swap (l.out[c], l.next[c]);
    }
    for (int c = 0; c < 2; ++c)
    {
        std::memcpy (l.fftIn[c].d, l.in[c].d, 2 * Nb);
        std::memcpy (l.in[c].d, l.in[c].d + l.N, Nb);
    }
    l.jobBlock = l.block;
    l.step = 0;
    l.jobActive = true;
    if (l.sync)
    {
        finishJob (l);
        for (int c = 0; c < 2; ++c) std::swap (l.out[c], l.next[c]);
    }
    ++l.block;
    l.pos = 0;
}

void ConvEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float* in[2] = { inL, inR };
    float* out[2] = { outL, outR };
    int done = 0;

    while (done < n)
    {
        const int m = std::min (n - done, kHead - chunkPos);
        const size_t mb = (size_t) m * sizeof (float);

        // direct-form head: taps 0..63
        for (int c = 0; c < 2; ++c)
        {
            std::memcpy (hist[c].d + (kHead - 1) + chunkPos, in[c] + done, mb);
            std::memset (out[c] + done, 0, mb);
        }
        for (int a = 0; a < numPaths; ++a)
        {
            const int p = pathIdx[a];
            const float* x = hist[inOfPath[p]].d + chunkPos;
            const float* hr = headRev[p].d;
            float* y = out[outOfPath[p]] + done;
            for (int k = 0; k < kHead; ++k)
            {
                const float g = hr[k];
                if (g == 0.0f) continue;
                const float* xk = x + k;
                for (int i = 0; i < m; ++i) y[i] += g * xk[i];
            }
        }

        // frequency-domain levels
        for (auto& lp : levels)
        {
            Level& l = *lp;
            for (int c = 0; c < 2; ++c)
            {
                std::memcpy (l.in[c].d + l.N + l.pos, in[c] + done, mb);
                const float* o = l.out[c].d + l.pos;
                float* y = out[c] + done;
                for (int i = 0; i < m; ++i) y[i] += o[i];
            }
            l.pos += m;
        }

        chunkPos += m;
        done += m;

        if (chunkPos == kHead)
        {
            chunkPos = 0;
            for (int c = 0; c < 2; ++c)
                std::memmove (hist[c].d, hist[c].d + kHead, (size_t) (kHead - 1) * sizeof (float));

            for (auto& lp : levels)
            {
                Level& l = *lp;
                if (l.pos >= l.N)
                    boundary (l);
                else if (l.jobActive)
                {
                    const int chunks = l.N / kHead, ci = l.pos / kHead;
                    const int target = chunks <= 2 ? l.numSteps
                                                   : std::min (l.numSteps, (l.numSteps * ci + chunks - 2) / (chunks - 1));
                    while (l.jobActive && l.step < target) runStep (l);
                }
            }
        }
    }
}
} // namespace ecr

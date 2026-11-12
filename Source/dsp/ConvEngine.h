// Zero-latency true-stereo convolution engine.
//
// The impulse response is split Gardner-style into a short direct-form head and
// four frequency-domain levels with growing block sizes. Every level except the
// first has one whole block of slack, and its work (FFTs + spectrum products) is
// spread evenly over that block instead of being done in one burst, so the cost
// per audio callback stays flat even for 20+ second responses, with no helper
// thread and identical results in real-time and offline rendering.
#pragma once
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

struct PFFFT_Setup;

namespace ecr
{
// SIMD-aligned float buffer (PFFFT needs 16-byte alignment).
struct ABuf
{
    float* d = nullptr;
    size_t n = 0;
    ABuf() = default;
    explicit ABuf (size_t size) { alloc (size); }
    ABuf (const ABuf&) = delete;
    ABuf& operator= (const ABuf&) = delete;
    ABuf (ABuf&& o) noexcept : d (o.d), n (o.n) { o.d = nullptr; o.n = 0; }
    ABuf& operator= (ABuf&& o) noexcept { if (this != &o) { release(); d = o.d; n = o.n; o.d = nullptr; o.n = 0; } return *this; }
    ~ABuf() { release(); }
    void alloc (size_t size);
    void release();
    void clear() { if (d) std::memset (d, 0, n * sizeof (float)); }
};

// Anything the audio thread hands back to a helper thread for deletion.
struct Disposable { virtual ~Disposable() = default; };

class ConvEngine : public Disposable
{
public:
    // Paths: 0 = L in -> L out, 1 = R in -> R out, 2 = L in -> R out, 3 = R in -> L out.
    // A null pointer means the path is absent. `phaseSeed` staggers the block
    // boundaries of engines that run side by side.
    ConvEngine (const float* const h[4], int length, int phaseSeed = 0);
    ~ConvEngine() override;

    // Overwrites outL/outR. Any n; in and out must not alias.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    int irLength() const { return length; }

    // set by the owner, read by the audio thread when the engine is installed
    float impulseOnInstall = 0.0f;

private:
    static constexpr int kHead = 64;
    static constexpr int kMacPerStep = 8;

    struct Level
    {
        int N = 0, first = 0, P = 0;
        bool sync = false;
        PFFFT_Setup* fft = nullptr;
        ABuf H[4];              // P spectra of 2N floats each
        ABuf X[2];              // ring of P input spectra
        ABuf in[2];             // [previous block | block being filled]
        ABuf fftIn[2];          // snapshot the running job transforms
        ABuf acc[2];            // output spectra being accumulated
        ABuf out[2], next[2];   // block being played / block being computed
        ABuf work, tmp;
        int pos = 0;
        long block = 0;         // index of the input block being filled
        long jobBlock = 0;      // newest input block the running job uses
        int step = 0, numSteps = 0, units = 0;
        bool jobActive = false;
    };

    void boundary (Level&);
    void runStep (Level&);
    void finishJob (Level& l) { while (l.jobActive) runStep (l); }

    int length = 0;
    int numPaths = 0;
    int pathIdx[4] {};          // active paths
    ABuf headRev[4];            // time-reversed first kHead taps
    ABuf hist[2];               // kHead-1 samples of history + the current chunk
    int chunkPos = 0;
    std::vector<std::unique_ptr<Level>> levels;
};
} // namespace ecr

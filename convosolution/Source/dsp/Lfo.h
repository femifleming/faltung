// Modulation sources: plain LFO shapes, random ones, and three small
// simulations (Lorenz and Roessler attractors, a flock of boids). Stepped once
// per audio block; output is -1..+1.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ecr
{
enum LfoShape { kSine, kTriangle, kSaw, kSquare, kRandom, kDrift, kLorenz, kRossler, kBoid, kNumLfoShapes };
inline const char* lfoShapeName (int s)
{
    static const char* const names[kNumLfoShapes] = { "SINE", "TRI", "SAW", "SQUARE", "RANDOM", "DRIFT", "LORENZ", "ROSSLR", "BOID" };
    return names[std::clamp (s, 0, kNumLfoShapes - 1)];
}
inline double lfoRateHz (double knob) { return 0.01 * std::pow (2000.0, knob); }   // 0.01 .. 20 Hz

class Lfo
{
public:
    explicit Lfo (uint32_t seed = 1) : rng (seed * 2654435761u + 12345u)
    {
        lx = 0.1 + 0.3 * rnd(); ly = 0.0; lz = 20.0;
        rx = 1.0 + rnd(); ry = 1.0; rz = 1.0;
        for (auto& b : boids) { b.x = bip(); b.y = bip(); b.vx = bip(); b.vy = bip(); }
        from = bip(); to = bip();
    }

    // Advances by `seconds` at `hz` and returns the new value.
    float advance (int shape, double hz, double seconds)
    {
        const double cycles = hz * seconds;
        phase += cycles;
        bool wrapped = false;
        if (phase >= 1.0) { phase -= std::floor (phase); wrapped = true; }
        const double twoPi = 6.283185307179586;

        switch (shape)
        {
            case kSine:     value = std::sin (twoPi * phase); break;
            case kTriangle: value = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase; break;
            case kSaw:      value = 2.0 * phase - 1.0; break;
            case kSquare:   value = phase < 0.5 ? 1.0 : -1.0; break;
            case kRandom:
                if (wrapped) to = bip();
                value = to;
                break;
            case kDrift:
                if (wrapped) { from = to; to = bip(); }
                value = from + (to - from) * (0.5 - 0.5 * std::cos (3.141592653589793 * phase));
                break;
            case kLorenz:
            {
                double t = cycles * 0.9;                 // about one lobe orbit per cycle
                while (t > 0.0)
                {
                    const double h = std::min (t, 0.002);
                    const double dx = 10.0 * (ly - lx), dy = lx * (28.0 - lz) - ly, dz = lx * ly - (8.0 / 3.0) * lz;
                    lx += dx * h; ly += dy * h; lz += dz * h;
                    t -= h;
                }
                value = lx / 20.0;
                break;
            }
            case kRossler:
            {
                double t = cycles * 6.0;
                while (t > 0.0)
                {
                    const double h = std::min (t, 0.01);
                    const double dx = -ry - rz, dy = rx + 0.2 * ry, dz = 0.2 + rz * (rx - 5.7);
                    rx += dx * h; ry += dy * h; rz += dz * h;
                    t -= h;
                }
                value = rx / 11.0;
                break;
            }
            case kBoid:
            {
                double t = cycles * 2.5;
                while (t > 0.0) { const double h = std::min (t, 0.02); stepBoids (h); t -= h; }
                value = boids[0].x;
                break;
            }
            default: value = 0.0; break;
        }
        value = std::clamp (value, -1.0, 1.0);
        return (float) value;
    }

    // For drawing: attractor position (x, y, z scaled to about -1..1), then the
    // flock as x,y pairs, bird 0 (the one the output follows) first.
    static constexpr int kViewSize = 3 + 2 * 7;
    void view (int shape, float* out) const
    {
        if (shape == kRossler) { out[0] = (float) (rx / 11.0); out[1] = (float) (ry / 11.0); out[2] = (float) (rz / 20.0); }
        else                   { out[0] = (float) (lx / 20.0); out[1] = (float) (ly / 27.0); out[2] = (float) ((lz - 25.0) / 25.0); }
        for (int i = 0; i < kBoids; ++i) { out[3 + 2 * i] = (float) boids[i].x; out[4 + 2 * i] = (float) boids[i].y; }
    }

private:
    struct Boid { double x, y, vx, vy; };

    double rnd() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }
    double bip() { return rnd() * 2.0 - 1.0; }

    // The classic three rules (stay close, don't collide, fly the same way) plus
    // a little wandering; the output follows one bird of the flock.
    void stepBoids (double h)
    {
        double cx = 0, cy = 0, avx = 0, avy = 0;
        for (const auto& b : boids) { cx += b.x; cy += b.y; avx += b.vx; avy += b.vy; }
        cx /= kBoids; cy /= kBoids; avx /= kBoids; avy /= kBoids;
        for (int i = 0; i < kBoids; ++i)
        {
            auto& b = boids[i];
            double ax = (cx - b.x) * 0.8 + (avx - b.vx) * 0.4 + bip() * 3.0;
            double ay = (cy - b.y) * 0.8 + (avy - b.vy) * 0.4 + bip() * 3.0;
            for (int j = 0; j < kBoids; ++j)
            {
                if (j == i) continue;
                const double dx = b.x - boids[j].x, dy = b.y - boids[j].y, d2 = dx * dx + dy * dy + 1e-4;
                if (d2 < 0.16) { ax += dx / d2 * 0.05; ay += dy / d2 * 0.05; }
            }
            ax -= b.x * b.x * b.x * 4.0;                // soft walls
            ay -= b.y * b.y * b.y * 4.0;
            b.vx += ax * h; b.vy += ay * h;
            const double sp = std::sqrt (b.vx * b.vx + b.vy * b.vy);
            if (sp > 1.6) { b.vx *= 1.6 / sp; b.vy *= 1.6 / sp; }
            else if (sp < 0.5 && sp > 1e-6) { b.vx *= 0.5 / sp; b.vy *= 0.5 / sp; }
            b.x += b.vx * h; b.y += b.vy * h;
        }
    }

    static constexpr int kBoids = 7;
    uint32_t rng;
    double phase = 0.0, value = 0.0, from = 0.0, to = 0.0;
    double lx, ly, lz, rx, ry, rz;
    Boid boids[kBoids];
};
} // namespace ecr

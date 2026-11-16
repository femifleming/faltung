#pragma once
// faltung — the processes: dl4 dub, rv5 room, fl7 filter, gl8 glue, mc3 micro, sw6 swing.
// Each takes a plain parameter struct per block; no JUCE here.
#include "Core.h"
#include <array>
#include <atomic>
#include <climits>

namespace fal
{
struct Transport
{
    bool playing = false;
    bool hasPpq = false;
    double ppq = 0.0;   // at the first sample of the block
    double bpm = 120.0;
};

/** tempo divisions shared by delay / filter lfo / micro, in quarter notes */
struct Division { const char* name; double beats; };
inline const std::array<Division, 19>& divisions()
{
    static const std::array<Division, 19> d { {
        { "1/64", 1.0 / 16 }, { "1/32", 1.0 / 8 }, { "1/32 d", 3.0 / 16 }, { "1/16 t", 1.0 / 6 }, { "1/16", 0.25 },
        { "3/32", 3.0 / 8 / 1.0 }, { "1/16 d", 0.375 }, { "1/8 t", 1.0 / 3 }, { "1/8", 0.5 }, { "1/8 d", 0.75 },
        { "1/4 t", 2.0 / 3 }, { "1/4", 1.0 }, { "1/4 d", 1.5 }, { "1/2", 2.0 }, { "1 bar", 4.0 },
        { "2 bars", 8.0 }, { "4 bars", 16.0 }, { "8 bars", 32.0 }, { "16 bars", 64.0 } } };
    return d;
}

// =================================================================================================
//  dl4 dub — tape / bbd echo with the filter inside the feedback loop
// =================================================================================================
struct DubParams
{
    bool sync = true; int division = 6; float ms = 375.0f;
    float feedback = 0.55f;   // 0..1.1
    float age = 0.35f, wow = 0.2f;
    float hp = 200.0f, lp = 3500.0f;
    float spread = 0.5f;      // 0 = mono echo, 1 = full ping-pong
    int mode = 0;             // tape, bbd, clean
    bool freeze = false;
    float mix = 0.35f;
    float input = 1.0f;       // input send gain
};

class DubDelay
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& d : line) d.prepare ((int) (sr * 4.2f));
        timeSm.prepare (sr, 140.0f);   // tape-ish glide when the time moves
        fbSm.prepare (sr, 30.0f); mixSm.prepare (sr, 30.0f); frzSm.prepare (sr, 40.0f); inSm.prepare (sr, 20.0f);
        driftLp.set (0.7f, sr);
        for (auto& d : dcb) d.set (sr);
        rng.seed (555);
        reset();
    }
    void reset()
    {
        for (auto& d : line) d.clear();
        for (auto& f : hpF) f.reset();
        for (auto& f : lpF) f.reset();
        for (auto& f : bbdF) f.reset();
        for (auto& s : sat) s.reset();
        for (auto& d : dcb) d.reset();
        fbL = fbR = 0; lfo = 0; flut = 0; drift = 0; driftLp.reset();
        timeSm.snap (-1.0f); first = true;
    }
    /** echo spacing in seconds, for the editor */
    std::atomic<float> shownTime { 0.375f };

    void process (float* L, float* R, int n, const DubParams& p, const Transport& tr)
    {
        float tSec = p.ms * 0.001f;
        if (p.sync) tSec = (float) (divisions()[(size_t) std::clamp (p.division, 0, 18)].beats * 60.0 / std::max (20.0, tr.bpm));
        tSec = clampf (tSec, 0.002f, 4.0f);
        shownTime.store (tSec);
        const float target = tSec * sr;
        if (timeSm.cur < 0) timeSm.snap (target);
        timeSm.target = target;
        fbSm.target = p.freeze ? 1.0f : clampf (p.feedback, 0.0f, 1.1f);
        mixSm.target = clampf (p.mix, 0.0f, 1.0f);
        frzSm.target = p.freeze ? 1.0f : 0.0f;
        inSm.target = p.input;
        if (first) { fbSm.snap (fbSm.target); mixSm.snap (mixSm.target); frzSm.snap (frzSm.target); inSm.snap (inSm.target); first = false; }

        const float lpHz = std::min (p.lp, p.mode == 1 ? 4200.0f : 20000.0f) * (1.0f - 0.55f * p.age * (p.mode == 2 ? 0.0f : 1.0f));
        for (int c = 0; c < 2; ++c)
        {
            hpF[c].set (p.hp, 0.65f, sr);
            lpF[c].set (std::max (lpHz, p.hp * 1.5f), 0.65f, sr);
            bbdF[c].set (6000.0f, 0.6f, sr);
        }
        const float wowDepth = p.mode == 2 ? 0.0f : p.wow * 0.0035f * sr;
        const float flutDepth = p.mode == 2 ? 0.0f : (0.00012f + p.age * 0.00025f) * sr;
        const float drive = p.mode == 2 ? 0.0f : 0.15f + p.age * 0.85f;
        const float cross = clampf (p.spread, 0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            const float d = timeSm.tick();
            const float fb = fbSm.tick();
            const float frz = frzSm.tick();
            const float in = inSm.tick() * (1.0f - frz);

            lfo += kTwoPi * 0.55f / sr; if (lfo > kTwoPi) lfo -= kTwoPi;
            flut += kTwoPi * 6.3f / sr; if (flut > kTwoPi) flut -= kTwoPi;
            drift = driftLp.lp (rng.bi());
            const float mod = wowDepth * (std::sin (lfo) * 0.7f + drift * 8.0f * 0.3f) + flutDepth * std::sin (flut);

            float yL = line[0].read (d + mod + wowDepth + 2.0f);
            float yR = line[1].read (d * (1.0f + 0.012f * cross) + mod * 0.8f + wowDepth + 2.0f);

            // the loop: filters → saturation, bypassed progressively when frozen so the loop holds
            auto shape = [&] (int c, float y)
            {
                float f = lpF[c].tickLP (hpF[c].tickHP (y));
                if (p.mode == 1) f = bbdF[c].tickLP (f);
                if (drive > 0.0f) f = sat[c].tick (f * (1.0f + drive * 2.0f)) / (1.0f + drive * 0.9f);
                f = dcb[c].tick (f);
                return lerp (f, y, frz);
            };
            fbL = shape (0, yL) * fb;
            fbR = shape (1, yR) * fb;

            const float xl = L[i] * in, xr = R[i] * in;
            const float mono = 0.5f * (xl + xr);
            // ping-pong: input enters the left line, feedback crosses between the lines
            const float wl = lerp (xl, mono * 1.4f, cross) + lerp (fbL, fbR, cross);
            const float wr = lerp (xr, 0.0f, cross) + lerp (fbR, fbL, cross);
            line[0].push (clampf (wl, -4.0f, 4.0f));
            line[1].push (clampf (wr, -4.0f, 4.0f));

            const float m = mixSm.tick();
            const float dry = std::cos (m * 0.5f * kPi), wet = std::sin (m * 0.5f * kPi);
            L[i] = L[i] * dry + yL * wet;
            R[i] = R[i] * dry + yR * wet;
        }
    }

private:
    float sr = 48000;
    std::array<DelayLine, 2> line;
    std::array<Svf, 2> hpF, lpF, bbdF;
    std::array<AdaaTanh, 2> sat;
    std::array<DcBlock, 2> dcb;
    Smooth timeSm, fbSm, mixSm, frzSm, inSm;
    OnePole driftLp;
    Rng rng; bool first = true;
    float fbL = 0, fbR = 0, lfo = 0, flut = 0, drift = 0;
};

// =================================================================================================
//  rv5 room — 8-line feedback delay network with early reflections and a tail gate
// =================================================================================================
struct RoomParams
{
    int model = 0;        // room, plate, air, chamber
    float size = 0.4f;    // 0..1
    float decay = 0.8f;   // seconds
    float predelay = 8.0f;// ms
    float damp = 0.5f;    // 0..1
    float lowcut = 150.0f;
    float diffusion = 0.7f;
    float mod = 0.3f;
    float gate = 0.0f;    // 0 = off
    float width = 1.0f;
    float mix = 0.25f;
};

class Room
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        const float k = sr / 48000.0f;
        for (int i = 0; i < 8; ++i) lines[(size_t) i].prepare ((int) (kBase[i] * 2.0f * k) + 64);
        pre.prepare ((int) (0.4f * sr) + 8);
        static const int apl[8] = { 142, 107, 379, 277, 211, 163, 457, 331 };
        for (int i = 0; i < 8; ++i) ap[(size_t) i].prepare ((int) (apl[i] * k));
        mixSm.prepare (sr, 30.0f); gateSm.prepare (sr, 40.0f);
        for (auto& d : dcb) d.set (sr);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.clear();
        pre.clear();
        for (auto& a : ap) a.clear();
        for (auto& f : damp) f.reset();
        for (auto& f : lc) f.reset();
        state.fill (0); env = 0; holdCount = 0; gateSm.snap (1.0f); lfo = 0; first = true;
        for (auto& d : dcb) d.reset();
    }
    std::atomic<float> gateOpen { 1.0f };

    void process (float* L, float* R, int n, const RoomParams& p)
    {
        const float k = sr / 48000.0f;
        const bool plate = p.model == 1, air = p.model == 2, chamber = p.model == 3;
        float scale = lerp (0.18f, 1.6f, p.size);
        if (air) scale *= 0.35f;
        if (chamber) scale *= 1.15f;
        for (int i = 0; i < 8; ++i)
        {
            len[(size_t) i] = kBase[i] * scale * k;
            const float g = std::pow (10.0f, -3.0f * len[(size_t) i] / (sr * std::max (0.05f, p.decay)));
            gain[(size_t) i] = g;
            damp[(size_t) i].set (expMap (1.0f - p.damp, 1500.0f, 18000.0f) * (plate ? 1.3f : 1.0f), sr);
        }
        for (auto& f : lc) f.set (p.lowcut, 0.7f, sr);
        const float apg = clampf (p.diffusion, 0.0f, 1.0f) * (plate ? 0.72f : 0.6f);
        const int nAp = plate ? 8 : 4;
        const float preS = std::max (1.0f, p.predelay * 0.001f * sr);
        const float modDepth = p.mod * 6.0f * k;
        mixSm.target = clampf (p.mix, 0.0f, 1.0f);
        if (first) { mixSm.snap (mixSm.target); first = false; }
        const float gateThr = dbToGain (lerp (-60.0f, -12.0f, p.gate));
        const int hold = (int) (0.06f * sr);
        const float envK = tcoef (5.0f, sr);

        for (int i = 0; i < n; ++i)
        {
            const float inM = 0.5f * (L[i] + R[i]);
            pre.push (inM);
            float x = pre.read (preS);
            x = lc[0].tickHP (x);

            // early reflections: sparse taps off the pre-delay line (not for plate)
            float erL = 0, erR = 0;
            if (! plate)
            {
                const float erScale = (chamber ? 1.4f : 1.0f) * scale;
                for (int t = 0; t < 6; ++t)
                {
                    const float v = pre.read (preS + kEr[t] * erScale * k) * kErG[t];
                    if (t & 1) erR += v; else erL += v;
                }
            }
            for (int a = 0; a < nAp; ++a) x = ap[(size_t) a].tick (x, apg);

            // FDN: read, damp, Hadamard mix, write
            lfo += kTwoPi * 0.37f / sr; if (lfo > kTwoPi) lfo -= kTwoPi;
            std::array<float, 8> y {};
            for (int j = 0; j < 8; ++j)
            {
                const float m = modDepth * std::sin (lfo + (float) j * 0.785f);
                y[(size_t) j] = damp[(size_t) j].lp (lines[(size_t) j].read (len[(size_t) j] + m)) * gain[(size_t) j];
            }
            hadamard8 (y);
            for (int j = 0; j < 8; ++j)
                lines[(size_t) j].push (y[(size_t) j] + x * ((j & 1) ? 0.35f : -0.35f));

            float wl = (y[0] + y[2] + y[4] + y[6]) * 0.6f + erL * 0.6f;
            float wr = (y[1] + y[3] + y[5] + y[7]) * 0.6f + erR * 0.6f;
            wl = dcb[0].tick (wl); wr = dcb[1].tick (wr);
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * p.width;
            wl = mid + side; wr = mid - side;

            // gate: closes the tail when the input stops (the 80s/ minimal "gated air")
            if (p.gate > 0.001f)
            {
                env = std::max (std::abs (inM), env * envK);
                if (env > gateThr) holdCount = hold; else if (holdCount > 0) --holdCount;
                gateSm.target = holdCount > 0 ? 1.0f : 0.0f;
            }
            else gateSm.target = 1.0f;
            const float g = gateSm.tick();
            const float mx = mixSm.tick();
            const float dry = std::cos (mx * 0.5f * kPi), wet = std::sin (mx * 0.5f * kPi) * g;
            L[i] = L[i] * dry + wl * wet;
            R[i] = R[i] * dry + wr * wet;
        }
        gateOpen.store (gateSm.cur);
    }

private:
    static constexpr float kBase[8] = { 1031, 1327, 1523, 1871, 2053, 2381, 2713, 2999 };
    static constexpr float kEr[6] = { 190, 377, 541, 809, 1103, 1499 };
    static constexpr float kErG[6] = { 0.8f, 0.7f, 0.55f, 0.45f, 0.35f, 0.25f };
    static void hadamard8 (std::array<float, 8>& v)
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; ++j)
                { const float a = v[(size_t) j], b = v[(size_t) (j + h)]; v[(size_t) j] = a + b; v[(size_t) (j + h)] = a - b; }
        for (auto& x : v) x *= 0.35355339f;
    }
    float sr = 48000;
    std::array<DelayLine, 8> lines;
    std::array<float, 8> len {}, gain {}, state {};
    std::array<OnePole, 8> damp;
    std::array<Allpass, 8> ap;
    std::array<Svf, 1> lc;
    std::array<DcBlock, 2> dcb;
    DelayLine pre;
    Smooth mixSm, gateSm;
    float lfo = 0, env = 0; int holdCount = 0; bool first = true;
};

// =================================================================================================
//  fl7 filter — 3-band isolator (LR4) + resonant DJ sweep with a synced lfo
// =================================================================================================
struct FilterParams
{
    float low = 0, mid = 0, high = 0;   // dB, -40 = kill
    float xlow = 250.0f, xhigh = 2500.0f;
    float sweep = 0.0f;                 // -1 lp … 0 off … +1 hp
    float res = 0.3f;
    float drive = 0.1f;
    int lfoDiv = 11; float lfoDepth = 0.0f; int lfoShape = 0;  // sine, tri, square, s&h
    float mix = 1.0f;
};

class Isolator
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto* s : { &lo, &mi, &hi, &sw }) s->prepare (sr, 25.0f);
        lo.snap (1); mi.snap (1); hi.snap (1); sw.snap (0);
        mixSm.prepare (sr, 25.0f); mixSm.snap (1);
        rng.seed (777);
        reset();
    }
    void reset()
    {
        for (auto& ch : f) for (auto& x : ch) x.reset();
        for (auto& ch : dj) for (auto& x : ch) x.reset();
        for (auto& s : sat) s.reset();
        lfoPh = 0; sh = 0;
    }
    std::atomic<float> shownSweep { 0.0f };

    void process (float* L, float* R, int n, const FilterParams& p, const Transport& tr)
    {
        auto bandGain = [] (float db) { return db <= -39.5f ? 0.0f : dbToGain (db); };
        lo.target = bandGain (p.low); mi.target = bandGain (p.mid); hi.target = bandGain (p.high);
        mixSm.target = p.mix;
        const float xl = clampf (p.xlow, 40.0f, 1200.0f), xh = clampf (p.xhigh, std::max (xl * 2.0f, 600.0f), 14000.0f);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 2; ++k)
            {
                f[c][k].set (xl, 0.70710678f, sr);      // low lp (x2 = LR4)
                f[c][2 + k].set (xl, 0.70710678f, sr);  // low hp
                f[c][4 + k].set (xh, 0.70710678f, sr);  // mid lp
                f[c][6 + k].set (xh, 0.70710678f, sr);  // high hp
                f[c][8 + k].set (xh, 0.70710678f, sr);  // allpass comp for low band (lp half)
                f[c][10 + k].set (xh, 0.70710678f, sr); // allpass comp for low band (hp half)
            }

        // lfo phase from the host when playing, free-running otherwise
        const double beats = divisions()[(size_t) std::clamp (p.lfoDiv, 0, 18)].beats;
        const double bps = std::max (20.0, tr.bpm) / 60.0;
        if (tr.playing && tr.hasPpq) lfoPh = std::fmod (tr.ppq / beats, 1.0);
        const double inc = bps / beats / sr;
        const float q = 0.55f + p.res * p.res * 11.0f;

        for (int i = 0; i < n; ++i)
        {
            const double prev = lfoPh;
            lfoPh += inc; if (lfoPh >= 1.0) lfoPh -= 1.0;
            if (lfoPh < prev) sh = rng.bi();
            float lv = 0.0f;
            const float ph = (float) lfoPh;
            switch (p.lfoShape)
            {
                case 0: lv = std::sin (kTwoPi * ph); break;
                case 1: lv = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                case 2: lv = ph < 0.5f ? 1.0f : -1.0f; break;
                default: lv = sh; break;
            }
            sw.target = clampf (p.sweep + lv * p.lfoDepth, -1.0f, 1.0f);
            const float s = sw.tick();
            if ((i & 15) == 0)
            {
                // sweep law: exponential cutoff across the travel, small dead zone at the centre
                const float a = std::abs (s);
                const float fc = s < 0 ? expMap (1.0f - a, 45.0f, 20000.0f) : expMap (a, 18.0f, 12000.0f);
                for (int c = 0; c < 2; ++c) { dj[c][0].set (fc, 0.707f, sr); dj[c][1].set (fc, q, sr); }
                djWet = clampf ((a - 0.015f) * 25.0f, 0.0f, 1.0f);
                djHp = s > 0;
            }
            const float gl = lo.tick(), gm = mi.tick(), gh = hi.tick(), mx = mixSm.tick();
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i];
                auto& F = f[c];
                const float low = F[1].tickLP (F[0].tickLP (x));
                const float rest = F[3].tickHP (F[2].tickHP (x));
                const float midb = F[5].tickLP (F[4].tickLP (rest));
                const float hig = F[7].tickHP (F[6].tickHP (rest));
                // allpass the low band through the second crossover (LR4 lp + hp) so the sum is flat
                const float lowAp = F[9].tickLP (F[8].tickLP (low)) + F[11].tickHP (F[10].tickHP (low));
                float y = lowAp * gl + midb * gm + hig * gh;

                if (djWet > 0.0f)
                {
                    float z = sat[c].tick (y * (1.0f + p.drive * 3.0f)) / (1.0f + p.drive * 1.2f);
                    z = djHp ? dj[c][1].tickHP (dj[c][0].tickHP (z)) : dj[c][1].tickLP (dj[c][0].tickLP (z));
                    y = lerp (y, z, djWet);
                }
                io[c][i] = lerp (x, y, mx);
            }
        }
        shownSweep.store (sw.cur);
    }

private:
    float sr = 48000;
    std::array<std::array<Svf, 12>, 2> f;
    std::array<std::array<Svf, 2>, 2> dj;
    std::array<AdaaTanh, 2> sat;
    Smooth lo, mi, hi, sw, mixSm;
    Rng rng;
    double lfoPh = 0; float sh = 0, djWet = 0; bool djHp = false;
};

// =================================================================================================
//  gl8 glue — feed-forward bus compressor + tape saturation
// =================================================================================================
struct GlueParams
{
    float threshold = -14.0f, ratio = 2.0f, attack = 10.0f, release = 200.0f;
    bool autoRelease = true;
    float scHp = 90.0f, knee = 6.0f, makeup = 0.0f;
    float tape = 0.25f, bump = 0.5f, hf = 0.3f, bias = 0.2f;
    float mix = 1.0f, out = 0.0f;
};

class Glue
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        mixSm.prepare (sr, 25.0f); outSm.prepare (sr, 25.0f); mkSm.prepare (sr, 25.0f);
        for (auto& d : dcb) d.set (sr);
        reset();
    }
    void reset()
    {
        grDb = 0; fastEnv = slowEnv = 0;
        for (auto& f : sc) f.reset();
        for (auto& s : sat) s.reset();
        for (auto& b : bumpF) b.reset();
        for (auto& b : hfF) b.reset();
        for (auto& d : dcb) d.reset();
        gr.store (0.0f); first = true;
    }
    std::atomic<float> gr { 0.0f }, inLevel { 0.0f }, outLevel { 0.0f };

    void process (float* L, float* R, int n, const GlueParams& p)
    {
        for (auto& f : sc) f.set (p.scHp, 0.707f, sr);
        const float aK = tcoef (std::max (0.05f, p.attack), sr);
        const float rK = tcoef (std::max (10.0f, p.release), sr);
        const float rFast = tcoef (60.0f, sr), rSlow = tcoef (900.0f, sr);
        for (int c = 0; c < 2; ++c)
        {
            bumpF[c].lowShelf (70.0f, 3.5f * p.bump * p.tape, sr);
            hfF[c].highShelf (9000.0f, -9.0f * p.hf * (0.3f + p.tape), sr);
        }
        mixSm.target = p.mix; outSm.target = dbToGain (p.out); mkSm.target = dbToGain (p.makeup);
        if (first) { mixSm.snap (mixSm.target); outSm.snap (outSm.target); mkSm.snap (mkSm.target); first = false; }
        const float drive = p.tape * p.tape * 6.0f;
        float maxGr = 0, inPk = 0, outPk = 0;

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            // detector: stereo-linked peak of the high-passed sidechain
            const float d = std::max (std::abs (sc[0].tickHP (xl)), std::abs (sc[1].tickHP (xr)));
            const float lvl = gainToDb (d);
            // soft-knee gain computer
            const float over = lvl - p.threshold;
            float want = 0.0f;
            if (2.0f * over < -p.knee) want = 0.0f;
            else if (2.0f * std::abs (over) <= p.knee) want = (1.0f / p.ratio - 1.0f) * (over + p.knee * 0.5f) * (over + p.knee * 0.5f) / (2.0f * p.knee);
            else want = (1.0f / p.ratio - 1.0f) * over;
            want = -want; // positive reduction in dB
            // smoothing in the dB domain; auto release = program dependent dual stage
            if (want > grDb) grDb = want + aK * (grDb - want);
            else if (p.autoRelease)
            {
                fastEnv = want + rFast * (fastEnv - want);
                slowEnv = want + rSlow * (slowEnv - want);
                fastEnv = std::min (fastEnv, grDb); slowEnv = std::min (slowEnv, grDb);
                grDb = std::max (want, 0.6f * fastEnv + 0.4f * slowEnv);
            }
            else grDb = want + rK * (grDb - want);
            if (want > grDb - 0.0001f) { fastEnv = slowEnv = grDb; }
            grDb = flushDenorm (grDb);
            const float g = dbToGain (-grDb) * mkSm.tick();
            maxGr = std::max (maxGr, grDb);

            float yl = xl * g, yr = xr * g;
            if (p.tape > 0.001f)
            {
                // asymmetric tape curve: bias adds even harmonics, then head bump + hf loss
                const float b = p.bias * 0.3f;
                auto tape = [&] (int c, float x)
                {
                    const float y = (sat[c].tick ((x + b) * (1.0f + drive)) - std::tanh (b * (1.0f + drive))) / (1.0f + drive * 0.55f);
                    return hfF[c].tick (bumpF[c].tick (dcb[c].tick (y)));
                };
                yl = lerp (yl, tape (0, yl), std::min (1.0f, p.tape * 2.0f));
                yr = lerp (yr, tape (1, yr), std::min (1.0f, p.tape * 2.0f));
            }
            const float mx = mixSm.tick(), og = outSm.tick();
            L[i] = (xl + (yl - xl) * mx) * og;
            R[i] = (xr + (yr - xr) * mx) * og;
            inPk = std::max (inPk, std::max (std::abs (xl), std::abs (xr)));
            outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        gr.store (maxGr); inLevel.store (inPk); outLevel.store (outPk);
    }

private:
    float sr = 48000;
    float grDb = 0, fastEnv = 0, slowEnv = 0;
    std::array<Svf, 2> sc;
    std::array<AdaaTanh, 2> sat;
    std::array<Biquad, 2> bumpF, hfF;
    std::array<DcBlock, 2> dcb;
    Smooth mixSm, outSm, mkSm; bool first = true;
};

// =================================================================================================
//  mc3 micro — live micro-sampling: stutter cuts, scattered grains, clicks made from the input
// =================================================================================================
struct MicroParams
{
    int mode = 1;          // cut, grain, click
    int division = 4;      // index into divisions(); kFree: the grid runs at freeHz, whatever the tempo
    float freeHz = 8.0f;   // the free rate (division == kFree)
    static constexpr int kFree = 19;
    float density = 0.5f;  // probability per grid tick
    float sizeMs = 60.0f;
    float pitch = 0.0f;    // semitones
    float scatter = 0.3f;  // 0..1 of the last 2 s
    float reverse = 0.0f;  // probability
    float shape = 0.5f;    // 0 hard … 1 smooth
    float mix = 0.5f;
    bool hold = false;     // stop recording
    float seedVar = 0.0f;
};

class Micro
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        cap = (int) (sr * 4.0f);
        for (auto& b : buf) b.assign ((size_t) cap, 0.0f);
        mixSm.prepare (sr, 25.0f);
        rng.seed (888);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; filled = 0; freeCount = 0; lastTick = -1;
        for (auto& g : grains) g.on = false;
        cutOn = false; first = true;
    }
    std::atomic<int> activity { 0 }; bool first = true;
    std::atomic<int> ticks { 0 };   // grid ticks so far (tests)

    void process (float* L, float* R, int n, const MicroParams& p, const Transport& tr)
    {
        mixSm.target = p.mix;
        if (first) { mixSm.snap (mixSm.target); first = false; }
        const bool free = p.division >= MicroParams::kFree;
        const double beats = divisions()[(size_t) std::clamp (p.division, 0, 18)].beats;
        const double spb = 60.0 / std::max (20.0, tr.bpm) * sr;      // samples per beat
        const double stepS = free ? sr / std::max (0.1f, p.freeHz) : beats * spb;
        const bool sync = ! free && tr.playing && tr.hasPpq;
        int act = 0;

        for (int i = 0; i < n; ++i)
        {
            // record (unless held)
            if (! p.hold)
            {
                buf[0][(size_t) w] = L[i]; buf[1][(size_t) w] = R[i];
                w = (w + 1) % cap; filled = std::min (filled + 1, cap);
            }
            // grid tick?
            long long tick;
            if (sync) tick = (long long) std::floor ((tr.ppq + i / spb) / beats);
            else tick = (long long) std::floor ((double) (freeCount++) / stepS);
            if (tick != lastTick)
            {
                lastTick = tick;
                ticks.fetch_add (1);
                onTick (p, (int) std::max (8.0, stepS));
            }

            float wl = 0, wr = 0;
            if (p.mode == 0)
            {
                if (cutOn)
                {
                    const double pos = cutStart + std::fmod (cutPhase, (double) cutLen);
                    wl = readBuf (0, pos); wr = readBuf (1, pos);
                    const float edge = edgeWin ((float) std::abs (std::fmod (cutPhase, (double) cutLen)), (float) cutLen, p.shape);
                    wl *= edge; wr *= edge;
                    cutPhase += cutRate;
                    ++act;
                }
            }
            for (auto& g : grains)
            {
                if (! g.on) continue;
                if (g.wait > 0) { --g.wait; continue; }
                const float win = edgeWin ((float) g.age, (float) g.len, p.shape);
                const double pos = g.start + g.pos;
                wl += readBuf (0, pos) * win * g.gainL;
                wr += readBuf (1, pos) * win * g.gainR;
                g.pos += g.rate;
                if (++g.age >= g.len) g.on = false;
                ++act;
            }
            const float mx = mixSm.tick();
            const float duck = p.mode == 0 ? (cutOn ? mx : 0.0f) : mx * 0.35f;
            L[i] = L[i] * (1.0f - duck) + wl * mx;
            R[i] = R[i] * (1.0f - duck) + wr * mx;
        }
        activity.store (act);
    }

private:
    struct Grain { bool on = false; double start = 0, pos = 0, rate = 1; int age = 0, len = 1, wait = 0; float gainL = 1, gainR = 1; };
    float sr = 48000;
    std::array<std::vector<float>, 2> buf;
    int cap = 1, w = 0, filled = 0;
    long long freeCount = 0, lastTick = -1;
    std::array<Grain, 24> grains;
    bool cutOn = false; double cutStart = 0, cutPhase = 0, cutRate = 1; int cutLen = 1;
    Smooth mixSm;
    Rng rng;

    float readBuf (int c, double absPos) const
    {
        // absPos is in "ring" coordinates; wrap into the buffer
        double p = std::fmod (absPos, (double) cap); if (p < 0) p += cap;
        const int i = (int) p; const float f = (float) (p - i);
        const auto& b = buf[(size_t) c];
        const float x0 = b[(size_t) i], x1 = b[(size_t) ((i + 1) % cap)];
        const float xm = b[(size_t) ((i - 1 + cap) % cap)], x2 = b[(size_t) ((i + 2) % cap)];
        const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    static float edgeWin (float age, float len, float shape)
    {
        // shape 0 → 0.5 ms edges (hard cut), 1 → full hann
        const float edge = std::max (2.0f, len * (0.01f + 0.49f * shape));
        float e = 1.0f;
        if (age < edge) e = age / edge;
        else if (age > len - edge) e = (len - age) / edge;
        e = clampf (e, 0.0f, 1.0f);
        return e * e * (3.0f - 2.0f * e);
    }
    void onTick (const MicroParams& p, int stepLen)
    {
        const bool fire = rng.uni() < p.density;
        const float rate = semis (p.pitch);
        const bool rev = rng.uni() < p.reverse;
        if (p.mode == 0)
        {
            if (! fire) { cutOn = false; return; }
            // repeat the most recent slice of length `size` (or one step) for the coming step
            const int len = std::min ((int) (p.sizeMs * 0.001f * sr), std::max (16, std::min (filled - 4, stepLen)));
            if (len < 16) { cutOn = false; return; }
            const int back = (int) (p.scatter * rng.uni() * std::max (0, std::min (filled, (int) (2.0f * sr)) - len));
            cutStart = (double) (w - len - back);
            cutLen = len; cutPhase = 0.0; cutRate = rate; cutOn = true;
            if (rev) { cutStart = (double) (w - back) - 1; cutRate = -rate; }
            return;
        }
        if (! fire) return;
        const int count = p.mode == 2 ? 1 + (int) (rng.uni() * 3.0f) : 1;
        for (int k = 0; k < count; ++k)
        {
            Grain* g = nullptr;
            for (auto& gg : grains) if (! gg.on) { g = &gg; break; }
            if (! g) return;
            const float sizeMs = p.mode == 2 ? lerp (0.6f, 14.0f, std::min (1.0f, p.sizeMs / 250.0f)) : p.sizeMs;
            const int len = std::max (8, (int) (sizeMs * 0.001f * sr));
            const int span = std::max (0, std::min (filled, (int) (2.0f * sr)) - (int) (len * rate) - 8);
            const int back = (int) (p.scatter * rng.uni() * span) + (int) (len * rate) + 4;
            g->on = true; g->age = 0; g->len = len; g->wait = k == 0 ? 0 : (int) (rng.uni() * stepLen * 0.5f);
            g->rate = rev ? -rate : rate;
            g->start = (double) (w - back) + (rev ? len * rate : 0.0);
            g->pos = 0;
            const float pan = p.mode == 2 ? rng.bi() * 0.8f : rng.bi() * 0.4f;
            panGains (pan, g->gainL, g->gainR);
            if (p.mode == 2) { g->gainL *= 1.4f; g->gainR *= 1.4f; }
        }
    }
};

// =================================================================================================
//  sw6 swing — moves the audio itself onto a groove: each 16th is read from a delay line with its
//  own offset (fixed latency), switched with short crossfades just before the grid.
// =================================================================================================
struct SwingParams
{
    int templ = 2;                 // straight, mpc, shuffle, push, drag, drunk
    float swing = 0.56f;           // 0.5 straight … 0.75
    float amount = 1.0f;           // scales everything
    float humanize = 0.0f;         // ms of random per step
    std::array<float, 16> step {}; // per-step offset in fractions of a 16th (-0.5 .. 0.5)
    float accent = 0.0f;           // depth of the template's level accents
    float xfadeMs = 4.0f;
};

class Swing
{
public:
    static constexpr float kMaxMs = 70.0f;
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        L = (int) (kMaxMs * 0.001f * sr);
        for (auto& d : dl) d.prepare (2 * L + 16);
        gSm.prepare (sr, 6.0f); gSm.snap (1.0f);
        reset();
    }
    void reset()
    {
        for (auto& d : dl) d.clear();
        curD = nextD = (float) L; xf = 1.0f; lastStep = LLONG_MIN;
        learnSum.fill (0); learnCnt.fill (0); env = 0; prevEnv = 0;
    }
    int latency() const { return L; }

    /** groove learning: onsets are compared with the grid and averaged per 16th position */
    std::atomic<bool> learning { false };
    std::array<float, 16> learnSum {}; std::array<int, 16> learnCnt {};
    std::atomic<int> shownStep { 0 };

    static const char* templName (int i)
    {
        static const char* n[6] = { "straight", "mpc", "shuffle", "push", "drag", "drunk" };
        return n[std::clamp (i, 0, 5)];
    }
    /** template offset in fractions of a 16th, plus a level accent in dB */
    static void templ (int t, int pos, float swing, float& off, float& accDb)
    {
        off = 0; accDb = 0;
        const bool odd = (pos & 1) != 0;
        const float sw = (swing - 0.5f) * 2.0f; // 0 .. 0.5 of a 16th → as fraction of 16th
        switch (t)
        {
            case 1: off = odd ? sw : 0.0f; accDb = odd ? -2.0f : (pos % 4 == 0 ? 1.0f : 0.0f); break;
            case 2: // shuffle: swung 16ths, the 2 and 4 laid back, offbeat 8ths pushed
                off = (odd ? sw : 0.0f) + (pos == 4 || pos == 12 ? 0.06f : 0.0f) + (pos == 2 || pos == 10 ? -0.03f : 0.0f);
                accDb = odd ? -3.0f : (pos % 4 == 2 ? 1.5f : 0.0f); break;
            case 3: off = (odd ? sw * 0.6f : 0.0f) - (pos % 4 == 2 ? 0.08f : 0.0f); accDb = pos % 4 == 2 ? 2.0f : -1.0f; break;
            case 4: off = (odd ? sw : 0.0f) + (pos % 4 != 0 ? 0.07f : 0.0f); accDb = pos % 4 == 0 ? 1.5f : -1.5f; break;
            case 5: off = (odd ? sw : 0.0f); break; // + humanize carries it
            default: break;
        }
    }

    void process (float* Lb, float* Rb, int n, const SwingParams& p, const Transport& tr)
    {
        const bool sync = tr.playing && tr.hasPpq;
        const double spb = 60.0 / std::max (20.0, tr.bpm) * sr;
        const double sixteenth = spb * 0.25;
        const int xfLen = std::max (16, (int) (p.xfadeMs * 0.001f * sr));
        const double preQ = (xfLen + 0.002 * sr) / spb; // switch just before the grid, in beats
        for (int i = 0; i < n; ++i)
        {
            dl[0].push (Lb[i]); dl[1].push (Rb[i]);
            float accentDb = 0.0f;
            if (sync)
            {
                // nominal input time currently leaving the line = now - L
                const double q = tr.ppq + (i - L) / spb;
                const long long k = (long long) std::floor ((q + preQ) / 0.25);
                if (k != lastStep)
                {
                    lastStep = k;
                    const int pos = (int) (((k % 16) + 16) % 16);
                    float off, acc; templ (p.templ, pos, p.swing, off, acc);
                    off = (off + p.step[(size_t) pos]) * p.amount;
                    float offS = (float) (off * sixteenth);
                    if (p.humanize > 0.0f || p.templ == 5)
                    {
                        Rng h; h.seed ((uint32_t) (k * 2654435761u + 7u));
                        const float hm = p.humanize + (p.templ == 5 ? 6.0f : 0.0f);
                        offS += h.bi() * hm * 0.001f * sr;
                    }
                    offS = clampf (offS, (float) -(L - xfLen), (float) (L - xfLen));
                    curD = lerp (curD, nextD, xf); // freeze whatever mix we are at
                    nextD = (float) L + offS; xf = 0.0f;
                    stepAccent = acc * p.accent;
                    shownStep.store (pos);
                }
                accentDb = stepAccent;
            }
            else if (std::abs (nextD - (float) L) > 0.5f || xf < 1.0f)
            {
                if (xf >= 1.0f) { curD = nextD; nextD = (float) L; xf = 0.0f; }
            }
            float yl, yr;
            if (xf < 1.0f)
            {
                xf = std::min (1.0f, xf + 1.0f / (float) xfLen);
                const float a = std::sin (xf * 0.5f * kPi), b = std::cos (xf * 0.5f * kPi);
                yl = dl[0].read (std::max (1.0f, nextD)) * a + dl[0].read (std::max (1.0f, curD)) * b;
                yr = dl[1].read (std::max (1.0f, nextD)) * a + dl[1].read (std::max (1.0f, curD)) * b;
            }
            else
            {
                yl = dl[0].read (std::max (1.0f, nextD));
                yr = dl[1].read (std::max (1.0f, nextD));
            }
            gSm.target = dbToGain (accentDb);
            const float g = gSm.tick();
            Lb[i] = yl * g; Rb[i] = yr * g;

            // learning: simple onset detector on the incoming signal
            if (learning.load (std::memory_order_relaxed) && sync)
            {
                const float a = std::abs (dl[0].readInt (1)) + std::abs (dl[1].readInt (1));
                env = std::max (a, env * 0.9995f);
                fast = 0.7f * fast + 0.3f * a;
                if (fast > 0.05f && fast > prevEnv * 2.5f && refractory <= 0)
                {
                    const double qIn = tr.ppq + i / spb;
                    const double pos16 = qIn / 0.25;
                    const double nearest = std::round (pos16);
                    const int pos = (int) ((((long long) nearest % 16) + 16) % 16);
                    learnSum[(size_t) pos] += (float) (pos16 - nearest);
                    learnCnt[(size_t) pos] += 1;
                    refractory = (int) (0.05f * sr);
                }
                prevEnv = 0.995f * prevEnv + 0.005f * fast + 1.0e-6f;
                if (refractory > 0) --refractory;
            }
        }
    }

private:
    float sr = 48000;
    int L = 1000;
    std::array<DelayLine, 2> dl;
    float curD = 0, nextD = 0, xf = 1, stepAccent = 0;
    long long lastStep = 0;
    Smooth gSm;
    float env = 0, prevEnv = 0, fast = 0; int refractory = 0;
};
} // namespace fal

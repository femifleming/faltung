#pragma once
// faltung — the eight drum engines. One Voice class renders any type; kt1 kit and the single
// modules (bd1 … tm9) use exactly this code, so a sound moves between them unchanged.
#include "Core.h"
#include <array>

namespace fal
{
enum class VType { Kick, Snare, Rim, Hat, Clap, Perc, Click, Tom };
constexpr int kNumVTypes = 8;
constexpr int kSlots = 8; // macro controls per voice

/** Everything a hit needs. Slots are 0..1; their meaning depends on the type (see labels). */
struct VoiceParams
{
    int model = 0;
    std::array<float, kSlots> s { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f };
    float level = 0.8f;   // 0..1 → -48..+6 dB
    float pan = 0.0f;     // -1..1
    float vel = 0.7f;     // velocity sensitivity
};

inline float levelToGain (float v) { return v <= 0.001f ? 0.0f : dbToGain (-48.0f + 54.0f * v); }

// ---------- descriptive tables used by both DSP and UI ------------------------------------------
struct VTypeInfo
{
    const char* name;
    std::array<const char*, kSlots> slot;
    std::array<const char*, 4> models;
};

inline const VTypeInfo& vtypeInfo (VType t)
{
    static const VTypeInfo info[kNumVTypes] = {
        { "kick",  { "tune", "decay", "click", "sub",   "tok",   "sweep", "bend",  "drive" }, { "tok", "thud", "909", "sine" } },
        { "snare", { "tune", "decay", "snap",  "body",  "noise", "sweep", "tone",  "drive" }, { "tuned", "fm", "brush", "808" } },
        { "rim",   { "tune", "decay", "click", "body",  "noise", "sweep", "hard",  "drive" }, { "rim", "wood", "tok", "metal" } },
        { "hat",   { "tune", "decay", "click", "metal", "air",   "chirp", "tone",  "drive" }, { "808", "noise", "ring", "tick" } },
        { "clap",  { "tune", "decay", "snap",  "claps", "noise", "spread","tone",  "drive" }, { "clap", "snap", "room", "tight" } },
        { "perc",  { "tune", "decay", "click", "body",  "noise", "sweep", "hard",  "drive" }, { "conga", "bongo", "bell", "glass" } },
        { "click", { "tune", "decay", "click", "res",   "dust",  "sweep", "tone",  "drive" }, { "tick", "pop", "dust", "blip" } },
        { "tom",   { "tune", "decay", "click", "body",  "noise", "sweep", "bend",  "drive" }, { "tom", "blip", "fm", "low" } },
    };
    return info[(int) t];
}

/** tuning range per type (Hz) */
inline float tuneHz (VType t, float v)
{
    switch (t)
    {
        case VType::Kick:  return expMap (v, 30.0f, 140.0f);
        case VType::Snare: return expMap (v, 110.0f, 420.0f);
        case VType::Rim:   return expMap (v, 180.0f, 2400.0f);
        case VType::Hat:   return expMap (v, 0.5f, 2.0f);       // ratio, shown as multiplier
        case VType::Clap:  return expMap (v, 500.0f, 3200.0f);
        case VType::Perc:  return expMap (v, 60.0f, 1400.0f);
        case VType::Click: return expMap (v, 300.0f, 14000.0f);
        case VType::Tom:   return expMap (v, 45.0f, 420.0f);
    }
    return 100.0f;
}
/** decay range per type (seconds) */
inline float decaySec (VType t, float v)
{
    switch (t)
    {
        case VType::Kick:  return expMap (v, 0.035f, 1.6f);
        case VType::Snare: return expMap (v, 0.04f, 0.9f);
        case VType::Rim:   return expMap (v, 0.008f, 0.6f);
        case VType::Hat:   return expMap (v, 0.006f, 1.4f);
        case VType::Clap:  return expMap (v, 0.03f, 1.0f);
        case VType::Perc:  return expMap (v, 0.02f, 2.5f);
        case VType::Click: return expMap (v, 0.0008f, 0.08f);
        case VType::Tom:   return expMap (v, 0.05f, 1.6f);
    }
    return 0.2f;
}
inline float sweepSemis (VType t, float v)
{
    switch (t)
    {
        case VType::Kick: return v * 48.0f;
        case VType::Tom:  return v * 36.0f;
        case VType::Click: return v * 48.0f;
        default: return v * 12.0f;
    }
}

// -----------------------------------------------------------------------------------------------
class Voice
{
public:
    void prepare (float sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        rng.seed (seed);
        dc.set (sr);
        levelSm.prepare (sr, 15.0f);
        panSm.prepare (sr, 15.0f);
        reset();
    }
    void reset()
    {
        active = false; t = 0; fade = 1; fading = false;
        for (auto& m : modes) m.reset();
        for (auto& f : f1) f.reset();
        for (auto& f : f2) f.reset();
        drive.reset(); dc.reset(); hp.reset();
    }
    bool isActive() const { return active; }
    /** quick fade-out (choke / voice steal), ~4 ms */
    void release() { if (active) { fading = true; fadeStep = 1.0f / (0.004f * sr); } }
    float envelope() const { return active ? lastAmp : 0.0f; }

    void trigger (VType type, const VoiceParams& p, float velocity, float semitoneOffset)
    {
        ty = type; P = p;
        const float vs = clampf (p.vel, 0.0f, 1.0f);
        velGain = (1.0f - vs) + vs * velocity * velocity;
        velTone = 0.75f + 0.5f * velocity * vs;  // harder hit = brighter
        noteMul = semis (semitoneOffset);
        t = 0; active = true; fading = false; fade = 1.0f; silentRun = 0;
        for (auto& m : modes) m.reset();
        for (auto& f : f1) f.reset();
        for (auto& f : f2) f.reset();
        drive.reset(); hp.reset();
        ph = ph2 = 0; fmPh = 0; hatPh.fill (0);
        for (int i = 0; i < 6; ++i) hatSq[(size_t) i].ph = rng.uni();
        static const float norm[kNumVTypes][4] = {
            { 0.7999f, 0.8086f, 0.9025f, 0.8806f },
            { 0.9951f, 1.1820f, 1.5880f, 1.1110f },
            { 0.1692f, 0.2422f, 0.1937f, 0.2420f },
            { 1.4872f, 2.0203f, 1.4194f, 1.6909f },
            { 1.7025f, 0.7300f, 1.3392f, 1.5484f },
            { 0.1338f, 0.1373f, 0.1829f, 0.1812f },
            { 0.7714f, 0.4552f, 1.6769f, 0.6871f },
            { 1.0399f, 1.0382f, 0.9738f, 0.8706f } };
        normGain = norm[(int) type][P.model & 3];
        levelSm.snap (levelToGain (p.level));
        panSm.snap (p.pan);
        setupHit();
    }

    /** live (smoothed) controls that may move while a hit rings */
    void setLive (float level01, float pan)
    {
        levelSm.target = levelToGain (level01);
        panSm.target = pan;
    }

    /** add n samples to L/R */
    void render (float* L, float* R, int n)
    {
        if (! active) return;
        for (int i = 0; i < n; ++i)
        {
            float x = tick() * normGain;   // calibrated so every engine sits at a similar level
            x = drive.tick (x, P.s[7]);
            x = dc.tick (x);
            const float g = levelSm.tick() * velGain * fade;
            float gl, gr; panGains (panSm.tick(), gl, gr);
            L[i] += x * g * gl;
            R[i] += x * g * gr;
            const float a = std::abs (x);
            lastAmp = std::max (a, lastAmp * 0.9995f);
            if (fading) { fade -= fadeStep; if (fade <= 0.0f) { active = false; return; } }
            if (a < 1.0e-5f) { if (++silentRun > (int) (0.02f * sr) && t > minLen) { active = false; return; } }
            else silentRun = 0;
        }
    }

private:
    // ---- derived per-hit state ----------------------------------------------------------------
    float sr = 48000.0f;
    VType ty = VType::Kick;
    VoiceParams P;
    bool active = false, fading = false;
    float fade = 1, fadeStep = 0, lastAmp = 0;
    int silentRun = 0;
    int t = 0, minLen = 0;
    float velGain = 1, velTone = 1, noteMul = 1, normGain = 1;
    Rng rng;
    PinkNoise pink;
    DcBlock dc;
    OnePole hp;
    Drive drive;
    Smooth levelSm, panSm;

    std::array<Mode, 8> modes;
    std::array<float, 8> modeAmp {};
    std::array<float, 8> modeHz {};
    int numModes = 0;
    std::array<Svf, 3> f1, f2;
    std::array<BlepSquare, 6> hatSq;
    std::array<float, 6> hatPh {};

    double ph = 0, ph2 = 0, fmPh = 0;
    float f0 = 100, ampK = 0.999f, ampK2 = 0.999f, holdN = 0, sweepSt = 0, sweepK = 0.99f, sweepCur = 0;
    float clickK = 0.9f, noiseK = 0.99f, noiseEnv = 0, clickEnv = 0, amp = 0, amp2 = 0;
    int clapBursts = 3, clapGap = 400;
    float dustRate = 0;

    float s (int i) const { return P.s[(size_t) i]; }

    void setupHit()
    {
        const float dec = decaySec (ty, s (1));
        ampK = t60coef (dec, sr);
        ampK2 = ampK;
        amp = 1.0f; amp2 = 1.0f; holdN = 0;
        sweepSt = sweepSemis (ty, s (5));
        sweepCur = 1.0f;
        clickEnv = 1.0f; noiseEnv = 1.0f;
        minLen = (int) (0.005f * sr);
        numModes = 0;
        const int m = P.model;

        switch (ty)
        {
            case VType::Kick:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                const float bendMs = expMap (s (6), 2.0f, 140.0f);
                sweepK = tcoef (bendMs, sr);
                if (m == 1) holdN = dec * 0.35f * sr;          // thud: hold the body
                if (m == 0) ampK = t60coef (dec * 0.6f, sr);   // tok: tighter
                ampK2 = t60coef (dec * 1.35f, sr);             // sub tail
                clickK = t60coef (0.006f, sr);
                // "tok" overtone: a woody mode well above the fundamental
                modes[0].set (f0 * (m == 0 ? 6.3f : 5.1f), 0.012f + 0.04f * s (4), sr);
                modes[1].set (2600.0f + 2400.0f * velTone * 0.5f, 0.004f, sr);
                modeAmp[0] = s (4) * (m == 0 ? 1.4f : 0.8f);
                modeAmp[1] = s (2);
                f1[0].set (4200.0f * velTone, 0.8f, sr);   // click noise band
                break;
            }
            case VType::Snare:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                sweepK = tcoef (12.0f, sr);
                ampK = t60coef (dec * (m == 2 ? 0.25f : 0.45f), sr);   // body
                noiseK = t60coef (dec, sr);                             // wires
                clickK = t60coef (0.004f, sr);
                const float r808[2] = { 1.0f, 2.0f };
                const float rTun[3] = { 1.0f, 1.53f, 2.43f };
                if (m == 3) { for (int i = 0; i < 2; ++i) { modes[(size_t) i].set (f0 * r808[i], dec * 0.4f, sr); modeAmp[(size_t) i] = i == 0 ? 1.0f : 0.5f; } numModes = 2; }
                else        { for (int i = 0; i < 3; ++i) { modes[(size_t) i].set (f0 * rTun[i], dec * (0.45f - 0.1f * i), sr); modeAmp[(size_t) i] = 1.0f / (1 + i); } numModes = 3; }
                for (int i = 0; i < numModes; ++i) modeHz[(size_t) i] = f0 * (m == 3 ? r808[i] : rTun[i]);
                const float tone = expMap (s (6), 1800.0f, 13000.0f) * velTone;
                f1[0].set (tone, 0.6f, sr);       // wire LP
                f1[1].set (m == 2 ? 300.0f : 900.0f, 0.7f, sr); // wire HP
                f1[2].set (6500.0f, 0.9f, sr);    // snap band
                break;
            }
            case VType::Rim:
            case VType::Perc:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                static const float rim[4][6] = {
                    { 1.0f, 1.47f, 2.09f, 2.56f, 3.40f, 4.21f },  // rim
                    { 1.0f, 2.57f, 4.20f, 5.90f, 7.10f, 9.20f },  // wood
                    { 1.0f, 1.72f, 2.40f, 3.90f, 5.20f, 6.60f },  // tok
                    { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 0.5f }   // metal
                };
                static const float perc[4][6] = {
                    { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f },  // conga (membrane)
                    { 1.0f, 1.59f, 2.14f, 2.65f, 3.16f, 3.50f },  // bongo
                    { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 0.5f },  // bell
                    { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.6f }   // glass
                };
                const float* r = ty == VType::Rim ? rim[m & 3] : perc[m & 3];
                const bool ringy = (ty == VType::Rim && m == 3) || (ty == VType::Perc && m >= 2);
                const float body = s (3);
                numModes = 6;
                for (int i = 0; i < 6; ++i)
                {
                    const float hz = f0 * r[i];
                    float t60 = dec / (1.0f + (ringy ? 0.35f : 1.1f) * i);
                    if (ty == VType::Perc && m <= 1 && i == 0) t60 *= 1.4f;
                    modes[(size_t) i].set (hz, t60, sr);
                    modeHz[(size_t) i] = hz;
                    // body tilts energy toward the fundamental
                    modeAmp[(size_t) i] = std::pow (0.5f + 0.45f * body, (float) i) * (hz < sr * 0.45f ? 1.0f : 0.0f);
                }
                sweepK = tcoef (ty == VType::Perc ? 35.0f : 8.0f, sr);
                // mallet hardness = excitation brightness and length
                const float hard = s (6) * velTone;
                f1[0].set (expMap (hard, 500.0f, 16000.0f), 0.7f, sr);
                clickK = t60coef (expMap (1.0f - hard, 0.0006f, 0.006f), sr);
                noiseK = t60coef (dec * 0.3f, sr);
                f1[1].set (f0 * 3.0f, 1.2f, sr);
                break;
            }
            case VType::Hat:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                static const float base[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                for (int i = 0; i < 6; ++i) modeHz[(size_t) i] = base[i] * f0;
                const float tone = expMap (s (6), 5000.0f, 15000.0f) * velTone;
                f1[0].set (tone, m == 1 ? 2.2f : 1.2f, sr);  // band
                f1[1].set (6500.0f * std::sqrt (f0), 0.7f, sr); // HP
                f1[2].set (tone * 0.8f, 3.0f, sr);              // noise ring
                sweepK = tcoef (18.0f, sr);
                clickK = t60coef (0.0025f, sr);
                if (m == 3) ampK = t60coef (dec * 0.4f, sr);
                break;
            }
            case VType::Clap:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                const float q = 0.6f + 3.0f * s (6);
                f1[0].set (f0, q, sr);
                f1[1].set (f0 * 2.2f, 1.5f, sr);
                clapBursts = m == 3 ? 2 : 2 + (int) std::round (s (3) * 4.0f);
                clapGap = (int) (expMap (s (5), 0.004f, 0.026f) * sr);
                noiseK = t60coef (0.0055f, sr);      // each burst
                ampK = t60coef (dec, sr);             // tail
                clickK = t60coef (0.003f, sr);
                ampK2 = 1.0f;
                modes[0].set (2200.0f * (f0 / 1200.0f), 0.03f, sr);  // finger snap body
                minLen = clapBursts * clapGap + (int) (0.01f * sr);
                if (m == 2) { f2[0].set (900.0f, 0.5f, sr); }
                break;
            }
            case VType::Click:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                const float resT = expMap (s (3), 0.0008f, 0.04f);
                modes[0].set (f0, resT, sr);
                modes[1].set (f0 * 1.51f, resT * 0.6f, sr);
                modeAmp[0] = 1.0f; modeAmp[1] = 0.4f * s (3);
                clickK = t60coef (0.0004f, sr);
                ampK = t60coef (dec, sr);
                sweepK = tcoef (expMap (s (1), 0.4f, 12.0f), sr);
                dustRate = (40.0f + 3000.0f * s (4) * s (4)) / sr;   // impulses per sample
                f1[0].set (expMap (s (6), 30.0f, 4000.0f), 0.707f, sr);  // HP colour
                noiseK = t60coef (dec * 0.6f, sr);
                break;
            }
            case VType::Tom:
            {
                f0 = tuneHz (ty, s (0)) * noteMul;
                const float bendMs = expMap (s (6), 3.0f, 220.0f);
                sweepK = tcoef (m == 1 ? bendMs * 0.35f : bendMs, sr);
                if (m == 3) ampK = t60coef (dec * 1.3f, sr);
                ampK2 = t60coef (dec * 0.5f, sr);
                clickK = t60coef (0.004f, sr);
                noiseK = t60coef (dec * 0.25f, sr);
                f1[0].set (f0 * 2.1f, 1.4f, sr);
                modes[0].set (3200.0f, 0.003f, sr);
                break;
            }
        }
    }

    float noise() { return rng.bi(); }

    float tick()
    {
        const float k1 = 1.0f / sr;
        const int m = P.model;
        float out = 0.0f;
        ++t;

        // shared pitch-sweep envelope (1 → 0)
        sweepCur *= sweepK;
        const float sweepMul = semis (sweepSt * sweepCur);

        // amp envelope with optional hold
        if (holdN > 0) holdN -= 1.0f; else amp *= ampK;
        amp2 *= ampK2;
        clickEnv *= clickK;
        noiseEnv *= noiseK;
        // 0.3 ms attack ramp avoids the hard edge without softening the transient
        const float att = std::min (1.0f, (float) t / (0.0003f * sr));

        switch (ty)
        {
            case VType::Kick:
            {
                const float f = f0 * sweepMul;
                ph += f * k1; if (ph >= 1.0) ph -= 1.0;
                ph2 += f0 * 0.5 * k1; if (ph2 >= 1.0) ph2 -= 1.0;
                float body = std::sin (kTwoPi * (float) ph);
                if (m == 2) // 909: waveshaped toward triangle, slightly asymmetric
                    body = 0.6f * body + 0.4f * (2.0f / kPi) * std::asin (clampf (body, -1.0f, 1.0f));
                body *= amp * att;
                const float sub = std::sin (kTwoPi * (float) ph2) * amp2 * s (3) * 0.8f;
                // click: filtered noise burst + bright mode ping
                const float cn = f1[0].tickBPn (noise()) * clickEnv * s (2) * (m == 3 ? 0.3f : 0.7f);
                const float ping = modes[1].tick (t == 1 ? 1.0f : 0.0f) * modeAmp[1] * 0.5f;
                const float tok = modes[0].tick (t == 1 ? 1.0f : 0.0f) * modeAmp[0] * 0.6f;
                out = body + sub + cn + ping + tok;
                break;
            }
            case VType::Snare:
            {
                float body = 0;
                if (m == 1) // fm body
                {
                    const float f = f0 * sweepMul;
                    fmPh += f * 1.47f * k1; if (fmPh >= 1.0) fmPh -= 1.0;
                    const float mod = std::sin (kTwoPi * (float) fmPh) * 2.4f * amp;
                    ph += f * k1; if (ph >= 1.0) ph -= 1.0;
                    body = std::sin (kTwoPi * (float) ph + mod) * amp;
                }
                else
                {
                    const float ex = t == 1 ? 1.0f : 0.0f;
                    for (int i = 0; i < numModes; ++i)
                    {
                        modes[(size_t) i].setFreq (modeHz[(size_t) i] * sweepMul, sr);
                        body += modes[(size_t) i].tick (ex) * modeAmp[(size_t) i];
                    }
                    body *= 0.8f;
                }
                if (m == 2) body *= 0.5f;
                float wn = (m == 2 ? pink.tick (noise()) * 2.0f : noise());
                wn = f1[1].tickHP (f1[0].tickLP (wn));
                float wenv = noiseEnv;
                if (m == 2) wenv *= std::min (1.0f, (float) t / (0.008f * sr)); // brush swell
                const float snap = f1[2].tickBPn (noise()) * clickEnv * s (2) * 1.2f;
                out = body * s (3) * 1.2f * att + wn * wenv * s (4) * 0.9f + snap;
                break;
            }
            case VType::Rim:
            case VType::Perc:
            {
                // excitation: band-limited impulse (one-sample spike through the mallet LP) + noise
                const float spike = (t <= 2 ? 1.0f : 0.0f);
                const float ex = f1[0].tickLP (spike * 3.0f + noise() * clickEnv * 0.6f);
                float sum = 0.0f;
                for (int i = 0; i < numModes; ++i)
                {
                    if (sweepSt > 0.01f) modes[(size_t) i].setFreq (modeHz[(size_t) i] * (1.0f + (sweepMul - 1.0f) * 0.5f), sr);
                    sum += modes[(size_t) i].tick (ex) * modeAmp[(size_t) i];
                }
                const float click = ex * s (2) * 0.5f;
                const float nz = f1[1].tickBPn (noise()) * noiseEnv * s (4) * 0.5f;
                out = sum * 0.45f + click + nz;
                break;
            }
            case VType::Hat:
            {
                float metal = 0.0f;
                const float chirp = 1.0f + (sweepMul - 1.0f);
                if (m == 0 || m == 3)
                    for (int i = 0; i < 6; ++i) metal += hatSq[(size_t) i].tick (modeHz[(size_t) i] * chirp, sr);
                else if (m == 2) // ring: inharmonic FM pair
                {
                    fmPh += modeHz[5] * 1.414f * chirp * k1; if (fmPh >= 1.0) fmPh -= 1.0;
                    ph += modeHz[3] * 2.7f * chirp * k1; if (ph >= 1.0) ph -= 1.0;
                    metal = std::sin (kTwoPi * (float) ph + 3.5f * std::sin (kTwoPi * (float) fmPh)) * 2.0f;
                }
                metal *= 0.25f;
                const float air = (m == 1 ? f1[2].tickBPn (noise()) * 1.6f : noise());
                const float mix = s (3) * metal + s (4) * air * 0.8f;
                float y = f1[1].tickHP (f1[0].tickBPn (mix) + 0.35f * mix);
                y *= amp * att;
                const float click = noise() * clickEnv * s (2);
                out = y + click * 0.6f;
                break;
            }
            case VType::Clap:
            {
                // burst train: retrigger the short burst envelope every gap, then the tail
                const int idx = (t - 1) / std::max (1, clapGap);
                float burst = 0.0f;
                if (idx < clapBursts)
                {
                    const int within = (t - 1) - idx * clapGap;
                    const float e = std::exp (-(float) within / (0.0032f * sr));
                    burst = e * (idx == clapBursts - 1 ? 1.0f : 0.75f + 0.2f * (float) ((idx * 7) % 3) / 2.0f);
                }
                const int tailStart = (clapBursts - 1) * clapGap;
                float tail = 0.0f;
                if (t > tailStart) { tail = amp2; amp2 *= ampK; } else amp2 = 1.0f;
                const float nz = noise();
                float y = f1[0].tickBPn (nz) * (burst + tail * 0.8f);
                y += f1[1].tickBPn (nz) * s (4) * (burst + tail * 0.4f) * 0.6f;
                if (m == 1) y = y * 0.4f + modes[0].tick (t == 1 ? 1.0f : 0.0f) * 0.9f + nz * clickEnv * 0.5f;
                if (m == 2) y += f2[0].tickBPn (y) * tail * 0.6f;
                y += noise() * clickEnv * s (2) * 0.5f;
                out = y * 0.9f;
                break;
            }
            case VType::Click:
            {
                float y = 0.0f;
                const float fsw = sweepMul;
                switch (m)
                {
                    case 0: // tick: impulse into a bright resonance
                    {
                        const float ex = t <= 1 ? 1.0f : 0.0f;
                        y = modes[0].tick (ex) * modeAmp[0] + modes[1].tick (ex) * modeAmp[1];
                        y += ex * s (2);
                        break;
                    }
                    case 1: // pop: one windowed sine cycle
                    {
                        const float len = std::max (2.0f, sr / (f0 * fsw) * (1.0f + 3.0f * s (3)));
                        if ((float) t < len)
                        {
                            const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) t / len);
                            ph += f0 * fsw * k1; if (ph >= 1.0) ph -= 1.0;
                            y = std::sin (kTwoPi * (float) ph) * w * 1.6f;
                        }
                        minLen = (int) len;
                        break;
                    }
                    case 2: // dust: sparse random impulses into the resonance
                    {
                        const float ex = rng.uni() < dustRate ? rng.bi() : 0.0f;
                        y = modes[0].tick (ex + (t <= 1 ? 1.0f : 0.0f)) * noiseEnv;
                        break;
                    }
                    default: // blip: sine with a very fast drop
                    {
                        ph += f0 * fsw * k1; if (ph >= 1.0) ph -= 1.0;
                        y = std::sin (kTwoPi * (float) ph) * amp;
                        break;
                    }
                }
                y += noise() * noiseEnv * s (4) * 0.3f * (m == 2 ? 0.0f : 1.0f);
                if (m != 3) y *= (m == 1 ? 1.0f : amp);
                out = f1[0].tickHP (y);
                break;
            }
            case VType::Tom:
            {
                const float f = f0 * sweepMul;
                ph += f * k1; if (ph >= 1.0) ph -= 1.0;
                float y = std::sin (kTwoPi * (float) ph);
                if (m == 2)
                {
                    fmPh += f * 1.41f * k1; if (fmPh >= 1.0) fmPh -= 1.0;
                    y = std::sin (kTwoPi * (float) ph + 1.8f * amp2 * std::sin (kTwoPi * (float) fmPh));
                }
                if (m == 0) y = 0.8f * y + 0.2f * (2.0f / kPi) * std::asin (clampf (y, -1.0f, 1.0f));
                y *= amp * att * (0.6f + 0.6f * s (3));
                if (m == 3) { ph2 += f * 0.5f * k1; if (ph2 >= 1.0) ph2 -= 1.0; y += std::sin (kTwoPi * (float) ph2) * amp * 0.5f * s (3); }
                const float nz = f1[0].tickBPn (noise()) * noiseEnv * s (4) * 0.5f;
                const float click = modes[0].tick (t == 1 ? 1.0f : 0.0f) * s (2) * 0.5f;
                out = y + nz + click;
                break;
            }
        }
        if (! finite (out)) { out = 0.0f; active = false; }
        return out;
    }
};
} // namespace fal

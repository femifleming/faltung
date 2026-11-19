#pragma once
// faltung wb1 "wub" — dubstep / neuro bass synth.
// Two unison oscillators (B can phase-modulate A), clean sub, noise → filter ↔ distortion → formant
// ("yoy") → comb → amp, all at 2× oversampling; then 3-band upward/downward compression ("ott"),
// mono-below-140 Hz, width, soft ceiling. Wobble lfo synced to the host.
#include "Core.h"
#include "Fx.h"
#include <array>

namespace fal
{
struct BassParams
{
    // oscillators: shape 0 sine · 1 triangle · 2 saw · 3 square · 4 hard (saw × square)
    struct Osc { float shape = 2, pw = 0.5f; int oct = 0; float semi = 0, fine = 0, level = 1; int unison = 1; float detune = 0.2f, spread = 0.5f; };
    Osc a, b;
    float fm = 0;                 // b → a phase modulation, 0..1
    float sub = 0.8f; int subWave = 0, subOct = -1; float noise = 0;
    int fType = 0;                // lp24, lp12, bp, hp, notch
    float cutoff = 900, res = 0.3f, fDrive = 0.2f, keytrack = 0.5f;
    float vowel = 0, fmtQ = 0.5f, fmtShift = 0, fmtMix = 0;
    float combTune = 0, combFb = 0.6f, combMix = 0;
    int dMode = 0; float dDrive = 0.3f, dTone = 0.7f, dMix = 1; int order = 0; // 0 filter→dist, 1 dist→filter
    int wDiv = 8, wShape = 0; bool wRetrig = true; float wPhase = 0;
    static constexpr int kFree = 19;      // wDiv / stepDiv == kFree: off the grid, at wFreeHz / stepFreeMs per step
    float wFreeHz = 2.0f, stepFreeMs = 125.0f;
    float wCut = 3.5f, wVowel = 0, wFm = 0, wDrive = 0, wPitch = 0, wAmp = 0, wComb = 0;
    float mA = 1, mD = 250, mS = 0, mR = 200, mCut = 0, mPitch = 0, mFm = 0, mVowel = 0;
    float aA = 1, aD = 300, aS = 1, aR = 80;
    float ott = 0.3f, ottTime = 0.4f;
    bool mono = true, legato = true; float glide = 40, bend = 2; int wheel = 1; float vel = 0.3f;
    float width = 0.6f, level = -6;

    // ---- modulation: two more lfos, a 16-step lfo, an 8-slot matrix --------------------------
    struct Lfo { int div = 11; bool sync = true; float hz = 1.0f; int shape = 0; bool retrig = true; float phase = 0; int mode = 0; float skew = 0.5f, smooth = 0; };
    Lfo l2, l3;
    std::array<float, 16> steps { 1, 0.2f, 0.6f, 0.1f, 0.9f, 0.3f, 0.5f, 0, 1, 0.4f, 0.7f, 0.2f, 0.8f, 0.1f, 0.5f, 0.3f };
    int stepLen = 16, stepDiv = 4; float stepGlide = 0.15f; bool stepRetrig = true;
    struct Slot { int src = 0, dst = 0; float amt = 0; };
    std::array<Slot, 8> mods;
    int chord = 0; int voicing = 0;   // one key plays a chord (deep house stabs); voicing: close, open, drop 2
};

/** chord shapes in semitones above the played key (index = the "chord" parameter) */
inline const std::vector<int>& chordShape (int i)
{
    static const std::vector<std::vector<int>> c = {
        { 0 }, { 0, 7 }, { 0, 12 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10 },
        { 0, 4, 7, 11, 14 }, { 0, 3, 7, 10, 14 }, { 0, 3, 7, 10, 14, 17 }, { 0, 5, 7 }, { 0, 4, 7, 9, 14 }, { 0, 3, 10, 14 }, { 0, 7, 10, 14, 17 } };
    return c[(size_t) std::clamp (i, 0, (int) c.size() - 1)];
}
inline const char* chordName (int i)
{
    static const char* n[15] = { "off", "fifth", "octave", "major", "minor", "major 7", "minor 7", "dominant 7", "major 9", "minor 9", "minor 11", "sus 4", "6 / 9", "minor 9 (no 5th)", "deep stack" };
    return n[std::clamp (i, 0, 14)];
}

// modulation sources and destinations (indices are stored in presets / sessions: append only)
enum ModSrc { MS_OFF, MS_WOBBLE, MS_LFO2, MS_LFO3, MS_STEPS, MS_MENV, MS_AENV, MS_VEL, MS_WHEEL, MS_NOTE, MS_RANDOM, MS_PRESSURE, MS_COUNT };
enum ModDst { MD_OFF, MD_CUTOFF, MD_RES, MD_VOWEL, MD_FMTSHIFT, MD_FM, MD_DRIVE, MD_PITCH, MD_VOLUME, MD_COMBTUNE, MD_COMBFB,
              MD_AWAVE, MD_BWAVE, MD_PW, MD_DETUNE, MD_NOISE, MD_SUB, MD_DMIX, MD_WOBRATE, MD_L2RATE, MD_L3RATE, MD_FINE, MD_FMTMIX, MD_STEPRATE, MD_COUNT };
inline const char* modSrcName (int i)
{
    static const char* n[MS_COUNT] = { "off", "wobble", "lfo 2", "lfo 3", "steps", "mod env", "amp env", "velocity", "mod wheel", "note", "random", "aftertouch" };
    return n[std::clamp (i, 0, (int) MS_COUNT - 1)];
}
inline const char* modDstName (int i)
{
    static const char* n[MD_COUNT] = { "off", "cutoff", "resonance", "vowel", "formant shift", "fm", "drive", "pitch", "volume", "comb tune", "comb feedback",
                                       "osc a wave", "osc b wave", "pulse width", "detune", "noise", "sub", "dist mix", "wobble rate", "lfo 2 rate", "lfo 3 rate",
                                       "fine pitch", "formant mix", "steps rate" };
    return n[std::clamp (i, 0, (int) MD_COUNT - 1)];
}

// ---- formant table: F1..F3 (Hz) and gains for a e i o u -----------------------------------------
inline void vowelAt (float v, float f[3], float g[3])
{
    static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 280, 2250, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    static const float G[5][3] = { { 1.0f, 0.5f, 0.25f }, { 1.0f, 0.45f, 0.3f }, { 1.0f, 0.3f, 0.3f }, { 1.0f, 0.55f, 0.15f }, { 1.0f, 0.4f, 0.12f } };
    v = clampf (v, 0.0f, 4.0f);
    const int i = std::min (3, (int) v); const float t = v - (float) i;
    for (int k = 0; k < 3; ++k)
    {
        f[k] = F[i][k] * std::pow (F[i + 1][k] / F[i][k], t);
        g[k] = lerp (G[i][k], G[i + 1][k], t);
    }
}

/** 2:1 decimator, windowed-sinc half-band (≈100 dB stop band) */
class Decimator
{
public:
    Decimator()
    {
        const int N = kTaps, M = N / 2;
        double sum = 0;
        for (int n = 0; n < N; ++n)
        {
            const double k = n - M;
            const double sinc = k == 0 ? 1.0 : std::sin (kPi * 0.5 * k) / (kPi * 0.5 * k);
            const double x = (2.0 * n / (N - 1)) - 1.0, beta = 9.0;
            auto i0 = [] (double z) { double s = 1, t = 1; for (int j = 1; j < 30; ++j) { t *= (z / (2 * j)) * (z / (2 * j)); s += t; } return s; };
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1 - x * x))) / i0 (beta);
            h[(size_t) n] = (float) (0.5 * sinc * w);
            sum += h[(size_t) n];
        }
        for (auto& c : h) c = (float) (c / sum);
    }
    void reset() { buf.fill (0); pos = 0; }
    /** push two oversampled samples, get one */
    float process (float x0, float x1)
    {
        push (x0); push (x1);
        float y = 0;
        for (int k = 0; k < kTaps; ++k) y += h[(size_t) k] * buf[(size_t) ((pos - 1 - k) & (kBuf - 1))];
        return y;
    }
private:
    static constexpr int kTaps = 63, kBuf = 64;
    std::array<float, kTaps> h {};
    std::array<float, kBuf> buf {};
    int pos = 0;
    void push (float x) { buf[(size_t) (pos & (kBuf - 1))] = x; ++pos; }
};

struct Adsr
{
    enum St { Idle, A, D, S, R } st = Idle;
    float v = 0, aInc = 1, dK = 0.99f, rK = 0.99f, sus = 1;
    void set (float aMs, float dMs, float s, float rMs, float sr)
    {
        aInc = 1.0f / std::max (1.0f, aMs * 0.001f * sr);
        dK = t60coef (std::max (0.001f, dMs * 0.001f), sr);
        rK = t60coef (std::max (0.001f, rMs * 0.001f), sr);
        sus = s;
    }
    void on() { st = A; }
    void off() { if (st != Idle) st = R; }
    float tick()
    {
        switch (st)
        {
            case A: v += aInc; if (v >= 1.0f) { v = 1.0f; st = D; } break;
            case D: v = sus + (v - sus) * dK; if (std::abs (v - sus) < 1.0e-4f) { v = sus; st = S; } break;
            case S: v = sus; break;
            case R: v *= rK; if (v < 1.0e-5f) { v = 0; st = Idle; } break;
            default: break;
        }
        return v;
    }
};

/** up to 7 detuned copies, stereo */
struct UniOsc
{
    std::array<double, 7> ph {};
    static float wave (float p, float dt, float shape, float pw)
    {
        auto sine = [&] { return std::sin (kTwoPi * p); };
        auto tri = [&] { return 1.0f - 4.0f * std::abs (p - 0.5f); };
        auto saw = [&] { return 2.0f * p - 1.0f - polyBlep (p, dt); };
        auto sq = [&]
        {
            const float w = clampf (pw, 0.05f, 0.95f);
            float v = p < w ? 1.0f : -1.0f;
            v += polyBlep (p, dt);
            float q = p - w; if (q < 0) q += 1.0f;
            v -= polyBlep (q, dt);
            return v;
        };
        auto at = [&] (int i)
        {
            switch (i)
            {
                case 0: return sine();
                case 1: return tri();
                case 2: return saw();
                case 3: return sq();
                default: { const float s = saw(), q = sq(); return clampf (s * q * 1.3f + s * 0.4f, -1.4f, 1.4f); }
            }
        };
        shape = clampf (shape, 0.0f, 4.0f);
        const int i = std::min (3, (int) shape);
        const float t = shape - (float) i;
        if (t < 0.001f) return at (i);
        return at (i) * (1.0f - t) + at (i + 1) * t;
    }
    void reset (Rng& r, bool randomise)
    {
        for (size_t i = 0; i < ph.size(); ++i) ph[i] = (i == 0 || ! randomise) ? 0.0 : r.uni();
    }
    /** returns mono sum, writes stereo */
    float tick (const BassParams::Osc& o, float hz, float sr, float pm, float& l, float& r)
    {
        const int n = std::clamp (o.unison, 1, 7);
        const float norm = 1.0f / std::sqrt ((float) n);
        float mono = 0; l = r = 0;
        for (int i = 0; i < n; ++i)
        {
            const float d = n == 1 ? 0.0f : (float) i / (float) (n - 1) * 2.0f - 1.0f;
            const float cents = d * std::pow (o.detune, 1.5f) * 60.0f;
            const float f = hz * std::exp2 (cents / 1200.0f);
            const float dt = clampf (f / sr, 0.0f, 0.45f);
            float p = (float) ph[(size_t) i] + pm;
            p -= std::floor (p);
            const float v = wave (p, dt, o.shape, o.pw) * norm;
            ph[(size_t) i] += dt; if (ph[(size_t) i] >= 1.0) ph[(size_t) i] -= 1.0;
            float gl, gr; panGains (d * o.spread, gl, gr);
            l += v * gl; r += v * gr; mono += v;
        }
        return mono;
    }
};

inline float lfoShape (float ph, int shape, float sh)
{
    switch (shape)
    {
        case 0: return 0.5f - 0.5f * std::cos (kTwoPi * ph);                       // sine (starts closed)
        case 1: return ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;                     // triangle
        case 2: return 1.0f - ph;                                                    // ramp down
        case 3: return ph;                                                           // ramp up
        case 4: return ph < 0.5f ? 1.0f : 0.0f;                                      // square
        case 5: { const float s = std::sin (kPi * std::pow (ph, 0.55f)); return s * s; }   // wow: fast open, slow close
        case 6: { const float s = std::sin (kTwoPi * ph); return ph < 0.5f ? s * s : 0.45f * s * s; } // yoy: big + small
        default: return sh;                                                          // stepped random
    }
}

struct BassShared;

/** running state of a modulation lfo */
struct ModLfoState { float ph = 0, sh = 0, shPrev = 0, out = 0; bool done = false; };

inline float skewPhase (float ph, float skew)
{
    skew = clampf (skew, 0.02f, 0.98f);
    return ph < skew ? 0.5f * ph / skew : 0.5f + 0.5f * (ph - skew) / (1.0f - skew);
}
/** advance one sample and return 0..1 */
inline float runModLfo (const BassParams::Lfo& l, ModLfoState& st, float inc, bool hostLocked, double hostPhase, Rng& rng, float smoothK)
{
    const float prev = st.ph;
    if (hostLocked) st.ph = (float) hostPhase;
    else if (! st.done)
    {
        st.ph += inc;
        if (st.ph >= 1.0f) { if (l.mode == 1) { st.ph = 1.0f; st.done = true; } else st.ph -= std::floor (st.ph); }
    }
    if (st.ph < prev) { st.shPrev = st.sh; st.sh = rng.uni(); }
    const float ph = skewPhase (std::min (st.ph, 0.99999f), l.skew);
    float v;
    if (l.shape == 8) { const float t = ph * ph * (3.0f - 2.0f * ph); v = lerp (st.shPrev, st.sh, t); }   // smooth random
    else v = lfoShape (ph, l.shape, st.sh);
    st.out += (v - st.out) * smoothK;
    return st.out;
}

// =================================================================================================
class BassVoice
{
public:
    int note = -1, root = -1;
    bool gate = false;
    float vel = 1;
    uint32_t age = 0;

    void prepare (float osr, uint32_t seed)
    {
        sr = osr; rng.seed (seed);
        for (auto& c : comb) c.prepare ((int) (sr / 18.0f) + 8);
        reset();
    }
    void reset()
    {
        amp.st = Adsr::Idle; amp.v = 0; menv.st = Adsr::Idle; menv.v = 0;
        for (auto& ch : flt) for (auto& f : ch) f.reset();
        for (auto& ch : fmt) for (auto& f : ch) f.reset();
        for (auto& c : comb) c.clear();
        for (auto& s : sat) s.reset();
        for (auto& t : toneLp) t.reset();
        note = -1; root = -1; gate = false; age = 0;
        // free-running state too, so an offline render repeats exactly
        subPh = 0; crushHold.fill (0); crushCount = 0; ctl = 0; pitch = pitchTarget = 60;
        lfoPh = lfoVal = shVal = 0; lastWob = lastMenv = lastL2 = lastL3 = lastSteps = 0; l2s = {}; l3s = {}; stepPh = stepOut = noteRand = 0; lastMod.fill (0);
        a.reset (rng, false); b.reset (rng, false);
    }
    bool active() const { return amp.st != Adsr::Idle; }
    void setEnv (const BassParams& p, float osRate)
    {
        amp.set (p.aA, p.aD, p.aS, p.aR, osRate);
        menv.set (p.mA, p.mD, p.mS, p.mR, osRate);
    }

    void noteOn (int n, float velocity, bool retrigger, bool glideFrom, const BassParams& p)
    {
        const float target = (float) n;
        if (! glideFrom || note < 0 || p.glide < 1.0f) pitch = target;
        note = n; pitchTarget = target; vel = velocity; gate = true;
        if (retrigger)
        {
            a.reset (rng, true); b.reset (rng, true);
            amp.on(); menv.on();
            lfoPh = p.wPhase;
            if (p.l2.retrig) { l2s.ph = p.l2.phase; l2s.done = false; }
            if (p.l3.retrig) { l3s.ph = p.l3.phase; l3s.done = false; }
            if (p.stepRetrig) stepPh = 0;
            noteRand = rng.bi();
            if (amp.v < 0.05f) { for (auto& ch : flt) for (auto& f : ch) f.reset(); for (auto& ch : fmt) for (auto& f : ch) f.reset(); }
        }
    }
    void noteOff() { gate = false; amp.off(); menv.off(); }
    void glideTo (int n) { note = n; pitchTarget = (float) n; }

    /** one oversampled stereo sample; `wobHost` is the host-synced lfo value (used when not retriggering) */
    void tick (const BassParams& p, const struct BassShared& s, float& outL, float& outR);

    float lfoPh = 0, lfoVal = 0, shVal = 0;
    float lastWob = 0, lastMenv = 0, lastL2 = 0, lastL3 = 0, lastSteps = 0;
    ModLfoState l2s, l3s; float stepPh = 0, stepOut = 0, noteRand = 0;
    std::array<float, MD_COUNT> lastMod {};

private:
    float sr = 96000;
    Rng rng;
    UniOsc a, b;
    Adsr amp, menv;
    float pitch = 60, pitchTarget = 60;
    double subPh = 0;
    std::array<std::array<Svf, 2>, 2> flt;   // [channel][stage]
    std::array<std::array<Svf, 3>, 2> fmt;   // [channel][formant]
    std::array<DelayLine, 2> comb;
    std::array<AdaaTanh, 2> sat;
    std::array<OnePole, 2> toneLp;
    std::array<float, 2> crushHold {}; int crushCount = 0;
    int ctl = 0;
    float fmtF[3] {}, fmtG[3] {};
};

/** values computed once per oversampled sample by the engine, shared by all voices */
struct BassShared
{
    float hostLfo = 0;       // host / free lfo value (0..1)
    float lfoInc = 0;        // per oversampled sample (for retriggered voices)
    float bendSt = 0;
    float wheel = 0;         // 0..1
    float cutoffOct = 0;     // smoothed log2(cutoff)
    float vowel = 0, fm = 0, drive = 0, fmtMix = 0, combMix = 0, res = 0;
    float aLevel = 1, bLevel = 0, subLevel = 0.8f, noise = 0;
    float depthScale = 1;    // mod wheel → wobble depth
    double ppq = 0; bool playing = false; float bpm = 120; float pressure = 0;
};

inline void BassVoice::tick (const BassParams& p, const BassShared& s, float& outL, float& outR)
{
    // glide in pitch space
    if (pitch != pitchTarget)
    {
        const float k = p.glide < 1.0f ? 1.0f : 1.0f - std::exp (-1.0f / (p.glide * 0.001f * sr * 0.35f));
        pitch += (pitchTarget - pitch) * k;
        if (std::abs (pitchTarget - pitch) < 0.0005f) pitch = pitchTarget;
    }
    // wobble: own phase when retriggered, otherwise the host-synced one
    float wob;
    auto rateMul = [&] (int d) { return std::exp2 (clampf (lastMod[(size_t) d], -1.0f, 1.0f) * 3.0f); };
    if (p.wRetrig)
    {
        const float prev = lfoPh;
        lfoPh += s.lfoInc * rateMul (MD_WOBRATE); if (lfoPh >= 1.0f) lfoPh -= std::floor (lfoPh);
        if (lfoPh < prev) shVal = rng.uni();
        wob = lfoShape (lfoPh, p.wShape, shVal);
    }
    else wob = s.hostLfo;
    lfoVal += (wob - lfoVal) * 0.012f;   // ~1 ms smoothing at 96k: no clicks from steps / ramps
    wob = lfoVal * s.depthScale;
    const float me = menv.tick();
    lastWob = lfoVal; lastMenv = me;

    // ---- lfo 2, lfo 3, steps ------------------------------------------------------------------
    auto lfoInc = [&] (const BassParams::Lfo& l, int rateDst)
    {
        const double hz = l.sync ? s.bpm / 60.0 / divisions()[(size_t) std::clamp (l.div, 0, 18)].beats : (double) l.hz;
        return (float) (hz / sr) * rateMul (rateDst);
    };
    auto hostPh = [&] (const BassParams::Lfo& l)
    {
        const double beats = divisions()[(size_t) std::clamp (l.div, 0, 18)].beats;
        const double v = s.ppq / beats + l.phase; return v - std::floor (v);
    };
    auto smoothK = [&] (float sm) { return sm <= 0.001f ? 1.0f : 1.0f - std::exp (-1.0f / (sm * sm * 0.25f * sr)); };
    const float l2v = runModLfo (p.l2, l2s, lfoInc (p.l2, MD_L2RATE), ! p.l2.retrig && p.l2.sync && s.playing, hostPh (p.l2), rng, smoothK (p.l2.smooth));
    const float l3v = runModLfo (p.l3, l3s, lfoInc (p.l3, MD_L3RATE), ! p.l3.retrig && p.l3.sync && s.playing, hostPh (p.l3), rng, smoothK (p.l3.smooth));
    float stv;
    {
        const int len = std::clamp (p.stepLen, 1, 16);
        const bool stepFree = p.stepDiv >= BassParams::kFree;
        const double beatsPerStep = divisions()[(size_t) std::clamp (p.stepDiv, 0, 18)].beats;
        if (stepFree) { stepPh += (float) (1000.0 / std::max (1.0, (double) p.stepFreeMs) / len / sr) * rateMul (MD_STEPRATE); stepPh -= std::floor (stepPh); }
        else if (! p.stepRetrig && s.playing) { const double v = s.ppq / (beatsPerStep * len); stepPh = (float) (v - std::floor (v)); }
        else { stepPh += (float) (s.bpm / 60.0 / (beatsPerStep * len) / sr) * rateMul (MD_STEPRATE); stepPh -= std::floor (stepPh); }
        const float pos = stepPh * (float) len;
        const int i0 = std::min (len - 1, (int) pos), i1 = (i0 + 1) % len;
        const float fr = pos - (float) i0, g = clampf (p.stepGlide, 0.0f, 1.0f);
        float v = p.steps[(size_t) i0];
        if (g > 0.001f && fr > 1.0f - g) { const float t = (fr - (1.0f - g)) / g; v = lerp (v, p.steps[(size_t) i1], t * t * (3.0f - 2.0f * t)); }
        stepOut += (v - stepOut) * 0.02f;   // de-click
        stv = stepOut;
    }
    lastL2 = l2v; lastL3 = l3v; lastSteps = stv;

    // ---- the matrix ------------------------------------------------------------------------------
    std::array<float, MD_COUNT> md {};
    {
        const float src[MS_COUNT] = { 0.0f, wob, l2v, l3v, stv, me, amp.v, vel, s.wheel, clampf ((pitch - 60.0f) / 24.0f, -1.0f, 1.0f), noteRand, s.pressure };
        for (const auto& sl : p.mods)
            if (sl.src > 0 && sl.dst > 0 && sl.src < MS_COUNT && sl.dst < MD_COUNT) md[(size_t) sl.dst] += sl.amt * src[sl.src];
        lastMod = md;
    }
    BassParams::Osc oa = p.a, ob = p.b;
    oa.shape = clampf (oa.shape + md[MD_AWAVE] * 4.0f, 0.0f, 4.0f); ob.shape = clampf (ob.shape + md[MD_BWAVE] * 4.0f, 0.0f, 4.0f);
    oa.pw = clampf (oa.pw + md[MD_PW] * 0.45f, 0.05f, 0.95f); ob.pw = clampf (ob.pw + md[MD_PW] * 0.45f, 0.05f, 0.95f);
    oa.detune = clampf (oa.detune + md[MD_DETUNE], 0.0f, 1.0f); ob.detune = clampf (ob.detune + md[MD_DETUNE], 0.0f, 1.0f);

    const float pitchMod = s.bendSt + wob * p.wPitch + me * p.mPitch + md[MD_PITCH] * 24.0f + md[MD_FINE];
    const float baseHz = 440.0f * std::exp2 ((pitch + pitchMod - 69.0f) / 12.0f);
    const float fA = baseHz * std::exp2 ((float) p.a.oct + (p.a.semi + p.a.fine * 0.01f) / 12.0f);
    const float fB = baseHz * std::exp2 ((float) p.b.oct + (p.b.semi + p.b.fine * 0.01f) / 12.0f);

    float bl, br, al, ar;
    const float bMono = b.tick (ob, fB, sr, 0.0f, bl, br);
    const float fmIdx = clampf (s.fm + wob * p.wFm + me * p.mFm + md[MD_FM], 0.0f, 1.5f);
    a.tick (oa, fA, sr, fmIdx * fmIdx * 1.6f * bMono, al, ar);
    const float nz = rng.bi() * clampf (s.noise + md[MD_NOISE], 0.0f, 1.0f);
    float x[2] = { al * s.aLevel + bl * s.bLevel + nz, ar * s.aLevel + br * s.bLevel + nz };

    // control-rate updates (every 8 oversampled samples)
    if ((ctl++ & 7) == 0)
    {
        const float oct = s.cutoffOct + wob * p.wCut + me * p.mCut + p.keytrack * (pitch - 60.0f) / 12.0f + md[MD_CUTOFF] * 6.0f;
        const float fc = clampf (std::exp2 (oct), 20.0f, 20000.0f);
        const float rs = clampf (s.res + md[MD_RES], 0.0f, 1.0f);
        const float rq = p.fType == 0 ? 0.5f + rs * rs * 9.0f : 0.6f + rs * rs * 18.0f;
        for (int c = 0; c < 2; ++c)
        {
            flt[(size_t) c][0].set (fc, p.fType == 0 ? 0.54f : rq, sr);
            flt[(size_t) c][1].set (fc, rq, sr);
        }
        const float v = clampf (s.vowel + wob * p.wVowel * 4.0f + me * p.mVowel * 4.0f + md[MD_VOWEL] * 4.0f, 0.0f, 4.0f);
        vowelAt (v, fmtF, fmtG);
        const float shift = std::exp2 ((p.fmtShift + md[MD_FMTSHIFT] * 12.0f) / 12.0f);
        const float q = 3.0f + p.fmtQ * 22.0f;
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 3; ++k) fmt[(size_t) c][(size_t) k].set (fmtF[k] * shift, q * (1.0f + 0.5f * k), sr);
        const float toneHz = expMap (p.dTone, 1200.0f, 20000.0f);
        for (auto& t : toneLp) t.set (toneHz, sr);
    }

    const float drv = clampf (s.drive + wob * p.wDrive + md[MD_DRIVE], 0.0f, 1.0f);
    const float dmix = clampf (p.dMix + md[MD_DMIX], 0.0f, 1.0f);
    auto filter = [&] (int c, float in)
    {
        auto& F = flt[(size_t) c];
        const float pre = sat[(size_t) c].tick (in * (1.0f + p.fDrive * 4.0f)) / (1.0f + p.fDrive * 1.5f);
        switch (p.fType)
        {
            case 0: return F[1].tickLP (F[0].tickLP (pre));
            case 1: return F[1].tickLP (pre);
            case 2: return F[1].tickBPn (pre);
            case 3: return F[1].tickHP (pre);
            default: F[1].tick (pre); return F[1].lp + F[1].hp;
        }
    };
    auto dist = [&] (int c, float in)
    {
        if (drv <= 0.001f && p.dMode != 4) return in;
        const float g = 1.0f + drv * drv * 30.0f;
        float y;
        switch (p.dMode)
        {
            case 0: y = std::tanh (in * g); break;                                        // soft
            case 1: y = std::sin (in * g * 1.5707963f); break;                             // fold
            case 2: y = clampf (in * g, -1.0f, 1.0f); break;                               // hard
            case 3: { const float z = std::tanh (in * g); y = z > 0 ? z : 0.35f * z; break; } // tube / rectify
            case 4:                                                                        // crush
            {
                const int hold = 1 + (int) (drv * 15.0f);
                if (c == 0 && ++crushCount >= hold) crushCount = 0;
                if (crushCount == 0) { const float steps = std::exp2 (14.0f - drv * 11.0f); crushHold[(size_t) c] = std::round (in * steps) / steps; }
                y = crushHold[(size_t) c];
                break;
            }
            default: { const float z = in * g; y = z / (1.0f + std::abs (z)) * 1.4f; break; }   // smooth
        }
        y = toneLp[(size_t) c].lp (y);
        return lerp (in, y, dmix);
    };

    const float combHz = baseHz * std::exp2 ((p.combTune + wob * p.wComb * 24.0f + md[MD_COMBTUNE] * 24.0f) / 12.0f);
    const float combFb = clampf (p.combFb + md[MD_COMBFB] * 0.95f, -0.95f, 0.95f);
    const float combD = clampf (sr / std::max (20.0f, combHz), 2.0f, (float) (sr / 18.0f));
    const float av = amp.tick();
    const float velG = (1.0f - p.vel) + p.vel * vel;
    const float wAmp = lerp (1.0f, wob, p.wAmp) * clampf (1.0f + md[MD_VOLUME], 0.0f, 2.0f);
    const float fmtMix = clampf (s.fmtMix + md[MD_FMTMIX], 0.0f, 1.0f);
    float y[2];
    for (int c = 0; c < 2; ++c)
    {
        float z = x[c];
        if (p.order == 0) z = dist (c, filter (c, z)); else z = filter (c, dist (c, z));
        if (fmtMix > 0.001f)
        {
            float f = 0;
            for (int k = 0; k < 3; ++k) f += fmt[(size_t) c][(size_t) k].tickBPn (z) * fmtG[k];
            z = lerp (z, f * 1.6f, fmtMix);
        }
        if (s.combMix > 0.001f)
        {
            const float d = comb[(size_t) c].read (combD);
            const float o = z + combFb * d;
            comb[(size_t) c].push (clampf (o, -4.0f, 4.0f));
            z = lerp (z, o * (1.0f - std::abs (combFb) * 0.5f), s.combMix);
        }
        y[c] = z * av * velG * wAmp;
    }
    // clean sub, untouched by filter and distortion
    const float subHz = baseHz * std::exp2 ((float) p.subOct);
    subPh += subHz / sr; if (subPh >= 1.0) subPh -= 1.0;
    float sub = std::sin (kTwoPi * (float) subPh);
    if (p.subWave == 1) sub = std::tanh (sub * 3.0f) * 0.85f;
    sub *= clampf (s.subLevel + md[MD_SUB], 0.0f, 1.5f) * av * velG * wAmp;
    outL = y[0] + sub; outR = y[1] + sub;
    if (! finite (outL) || ! finite (outR)) { outL = outR = 0; reset(); }
}

// =================================================================================================
struct BassEvent { int sample; int type; int a; float v; }; // type: 0 on, 1 off, 2 bend(v -1..1), 3 wheel(v 0..1), 4 all off

class BassSynth
{
public:
    static constexpr int kVoices = 12;   // room for two overlapping 6-note chords
    std::atomic<float> showWob { 0 }, showEnv { 0 }, showVowel { 0 }, showLevel { 0 }, showL2 { 0 }, showL3 { 0 }, showSteps { 0 }, showStepPh { 0 }, showCut { 0 };
    std::array<std::atomic<float>, 4> showMod {};

    void prepare (float sampleRate)
    {
        sr = sampleRate; osr = sr * 2.0f;
        uint32_t seed = 77;
        for (auto& v : voices) v.prepare (osr, seed += 101);
        for (auto& d : dec) d.reset();
        for (auto* sm : { &cutSm, &vowSm, &fmSm, &drvSm, &fmtSm, &combSm, &resSm, &aSm, &bSm, &subSm, &nzSm })
            sm->prepare (osr, 8.0f);
        first = true;
        for (auto& b : ott) for (auto& f : b) f.reset();
        bandEnv.fill (0);
        sideHp.reset();
        heldCount = 0;
        hostPh = 0; lastHostPh = 0; hostSh = 0; clock = 0;
        rng.seed (4242);
    }

    void process (float* L, float* R, int n, const BassParams& p, const Transport& tr, const BassEvent* ev, int nev)
    {
        // smoothed targets
        cutSm.target = std::log2 (std::max (20.0f, p.cutoff)); vowSm.target = p.vowel; fmSm.target = p.fm;
        drvSm.target = p.dDrive; fmtSm.target = p.fmtMix; combSm.target = p.combMix; resSm.target = p.res;
        aSm.target = p.a.level; bSm.target = p.b.level; subSm.target = p.sub; nzSm.target = p.noise;
        if (first) { for (auto* sm : { &cutSm, &vowSm, &fmSm, &drvSm, &fmtSm, &combSm, &resSm, &aSm, &bSm, &subSm, &nzSm }) sm->snap (sm->target); first = false; }

        const float bpm = (float) std::max (20.0, tr.bpm);
        const bool wFree = p.wDiv >= BassParams::kFree;
        int div = p.wDiv;
        if (p.wheel == 2) div = std::clamp (div - (int) std::round (wheel * 4.0f), 0, 18);  // wheel = faster wobble
        const double beats = divisions()[(size_t) std::clamp (div, 0, 18)].beats;
        const float lfoInc = wFree ? (float) (std::clamp ((double) p.wFreeHz, 0.001, 200.0) * (p.wheel == 2 ? std::exp2 (wheel * 2.0) : 1.0) / osr)
                                   : (float) (bpm / 60.0 / beats / osr);
        const bool sync = tr.playing && tr.hasPpq && ! wFree;
        const double ppqPerOs = bpm / 60.0 / osr;

        for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { ottX[c][k].set (k == 0 ? 150.0f : 2500.0f, 0.70710678f, sr); }
        setOtt (p);
        sideHp.set (140.0f, 0.707f, sr);
        const float lvl = dbToGain (p.level);
        for (auto& v : voices) v.setEnv (p, osr);

        int e = 0;
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nev && ev[e].sample <= i) handle (ev[e++], p);
            float os[2][2] {};
            for (int k = 0; k < 2; ++k)
            {
                // host-synced lfo
                double ph;
                if (sync) ph = std::fmod ((tr.ppq + (i * 2 + k) * ppqPerOs) / beats + p.wPhase, 1.0);
                else { hostPh += lfoInc; if (hostPh >= 1.0) hostPh -= 1.0; ph = std::fmod (hostPh + p.wPhase, 1.0); }
                if (ph < lastHostPh) hostSh = rng.uni();
                lastHostPh = ph;
                BassShared s;
                s.hostLfo = lfoShape ((float) ph, p.wShape, hostSh);
                s.lfoInc = lfoInc; s.bendSt = bend * p.bend; s.wheel = wheel;
                s.ppq = tr.ppq + (i * 2 + k) * ppqPerOs; s.playing = sync; s.bpm = bpm; s.pressure = pressure;
                s.depthScale = p.wheel == 1 ? wheel : 1.0f;
                s.cutoffOct = cutSm.tick(); s.vowel = vowSm.tick(); s.fm = fmSm.tick(); s.drive = drvSm.tick();
                s.fmtMix = fmtSm.tick(); s.combMix = combSm.tick(); s.res = resSm.tick();
                s.aLevel = aSm.tick(); s.bLevel = bSm.tick(); s.subLevel = subSm.tick(); s.noise = nzSm.tick();
                for (auto& v : voices)
                {
                    if (! v.active()) continue;
                    float l, r; v.tick (p, s, l, r);
                    os[0][k] += l; os[1][k] += r;
                }
            }
            float l = dec[0].process (os[0][0], os[0][1]);
            float r = dec[1].process (os[1][0], os[1][1]);
            // ott
            if (p.ott > 0.001f) { l = ottTick (0, l, p); r = ottTick (1, r, p); updateOttEnv (p); }
            // mono below 140 Hz, width above
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r);
            const float sh = sideHp.tickHP (sd) * p.width * 2.0f;
            l = (m + sh) * lvl; r = (m - sh) * lvl;
            l = ceiling (l); r = ceiling (r);
            L[i] = l; R[i] = r;
            peak = std::max (peak, std::abs (l));
        }
        while (e < nev) handle (ev[e++], p);
        // ui
        float w = 0, en = 0;
        for (auto& v : voices) if (v.active()) { w = v.lastWob; en = std::max (en, v.lastMenv); }
        if (! p.wRetrig) w = voices[0].lastWob;
        showWob.store (w); showEnv.store (en); showVowel.store (p.vowel); showLevel.store (peak);
        const BassVoice* sv = &voices[0];
        for (auto& v : voices) if (v.active()) { sv = &v; break; }
        showL2.store (sv->lastL2); showL3.store (sv->lastL3); showSteps.store (sv->lastSteps); showStepPh.store (sv->stepPh);
        for (int d = 0; d < 4; ++d) showMod[(size_t) d].store (0);
        showCut.store (sv->lastMod[MD_CUTOFF]);
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active() ? 1 : 0; return c; }

private:
    float sr = 48000, osr = 96000;
    std::array<BassVoice, kVoices> voices;
    std::array<Decimator, 2> dec;
    Smooth cutSm, vowSm, fmSm, drvSm, fmtSm, combSm, resSm, aSm, bSm, subSm, nzSm;
    bool first = true;
    Rng rng;
    double hostPh = 0, lastHostPh = 0; float hostSh = 0;
    float bend = 0, wheel = 1.0f, pressure = 0;
    std::array<int, 16> held {}; int heldCount = 0;
    uint32_t clock = 0;
    // ott
    std::array<std::array<Svf, 2>, 2> ottX;
    std::array<std::array<Svf, 8>, 2> ott;
    std::array<float, 3> bandEnv {}, bandGain { 1, 1, 1 }, bandPk {};
    float ottK = 0.99f;
    Svf sideHp;

    static float ceiling (float x)
    {
        const float a = std::abs (x);
        if (a < 0.9f) return x;
        return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x);
    }

    void setOtt (const BassParams& p)
    {
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < 2; ++k) { ott[(size_t) c][(size_t) k].set (150.0f, 0.70710678f, sr); ott[(size_t) c][(size_t) (2 + k)].set (150.0f, 0.70710678f, sr); }
            for (int k = 0; k < 2; ++k) { ott[(size_t) c][(size_t) (4 + k)].set (2500.0f, 0.70710678f, sr); ott[(size_t) c][(size_t) (6 + k)].set (2500.0f, 0.70710678f, sr); }
        }
        ottK = tcoef (lerp (4.0f, 120.0f, p.ottTime), sr);
    }
    std::array<std::array<float, 3>, 2> bandSample {};
    float ottTick (int c, float x, const BassParams&)
    {
        auto& F = ott[(size_t) c];
        const float lo = F[1].tickLP (F[0].tickLP (x));
        const float rest = F[3].tickHP (F[2].tickHP (x));
        const float mi = F[5].tickLP (F[4].tickLP (rest));
        const float hi = F[7].tickHP (F[6].tickHP (rest));
        bandSample[(size_t) c] = { lo, mi, hi };
        return lo * bandGain[0] + mi * bandGain[1] + hi * bandGain[2];
    }
    void updateOttEnv (const BassParams& p)
    {
        for (int b = 0; b < 3; ++b)
        {
            const float x2 = std::max (bandSample[0][(size_t) b] * bandSample[0][(size_t) b], bandSample[1][(size_t) b] * bandSample[1][(size_t) b]);
            bandEnv[(size_t) b] = x2 + ottK * (bandEnv[(size_t) b] - x2);
            const float lv = 10.0f * std::log10 (bandEnv[(size_t) b] + 1.0e-12f);
            float gdb = 0;
            if (lv > -24.0f) gdb -= (lv + 24.0f) * 0.75f;                          // down: 4:1 above -24
            else if (lv > -75.0f) gdb += std::min (20.0f, (-38.0f - std::min (lv, -38.0f)) * 0.66f); // up: below -38
            const float makeup = b == 1 ? 3.0f : 4.5f;
            bandGain[(size_t) b] = dbToGain ((gdb + makeup) * p.ott);
        }
    }

    BassVoice* allocate (int noteNum)
    {
        for (auto& v : voices) if (v.note == noteNum && v.active()) return &v;
        for (auto& v : voices) if (! v.active()) return &v;
        BassVoice* oldest = &voices[0];
        for (auto& v : voices) if (v.age < oldest->age) oldest = &v;
        return oldest;
    }

    void handle (const BassEvent& e, const BassParams& p)
    {
        switch (e.type)
        {
            case 0:
            {
                const bool wasHeld = heldCount > 0;
                if (heldCount < 16) held[(size_t) heldCount++] = e.a;
                if (p.chord > 0)
                {
                    // a chord from one key: every chord tone gets its own voice
                    auto shape = chordShape (p.chord);
                    if (p.voicing == 1 && shape.size() > 2) shape[1] += 12;                                  // open
                    if (p.voicing == 2 && shape.size() > 3) shape[shape.size() - 2] -= 12;                  // drop 2
                    for (auto& v : voices) if (v.root == e.a && v.gate) v.noteOff();
                    for (int iv : shape)
                    {
                        auto* v = allocate (e.a + iv);
                        v->noteOn (e.a + iv, e.v, true, false, p);
                        v->root = e.a; v->age = ++clock;
                    }
                    break;
                }
                if (p.mono)
                {
                    auto& v = voices[0];
                    for (size_t k = 1; k < voices.size(); ++k) voices[k].noteOff();
                    const bool legatoNow = p.legato && wasHeld && v.gate && v.root < 0;
                    v.noteOn (e.a, e.v, ! legatoNow, v.note >= 0 && v.active() && v.root < 0, p);
                    v.root = -1;
                }
                else
                {
                    auto* v = allocate (e.a);
                    v->noteOn (e.a, e.v, true, false, p);
                    v->root = -1; v->age = ++clock;
                }
                break;
            }
            case 1:
            {
                for (int k = 0; k < heldCount; ++k)
                    if (held[(size_t) k] == e.a) { for (int j = k; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) (j + 1)]; --heldCount; break; }
                {
                    bool chordVoice = false;
                    for (auto& v : voices) if (v.root == e.a && v.gate) { v.noteOff(); chordVoice = true; }
                    if (chordVoice) break;
                }
                if (p.mono)
                {
                    auto& v = voices[0];
                    if (v.note == e.a)
                    {
                        if (heldCount > 0)
                        {
                            const int back = held[(size_t) (heldCount - 1)];
                            if (p.legato) v.glideTo (back); else v.noteOn (back, v.vel, true, true, p);
                        }
                        else v.noteOff();
                    }
                }
                else for (auto& v : voices) if (v.note == e.a && v.gate) v.noteOff();
                break;
            }
            case 2: bend = e.v; break;
            case 3: wheel = e.v; break;
            case 5: pressure = e.v; break;
            default: heldCount = 0; for (auto& v : voices) v.noteOff(); break;
        }
    }

};
} // namespace fal

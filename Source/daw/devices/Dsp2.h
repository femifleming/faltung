#pragma once
// faltung sndstudio — dsp cores of the studio's own devices. Header-only, no JUCE, allocation-free after prepare.
// Built from the suite's Core.h blocks (svf, biquad, adaa drive, delay line, allpass, rng).
#include "../../dsp/Core.h"
#include <array>
#include <vector>

namespace fal::dsp2
{
using namespace fal;

// ---- biquad high / low pass (RBJ, 12 db) ------------------------------------------------------------
struct Hp12 : Biquad
{
    void set (float fc, float sr, float q = 0.7071f)
    {
        const float w = kTwoPi * clampf (fc, 5.0f, sr * 0.45f) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        norm ((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
};
struct Lp12 : Biquad
{
    void set (float fc, float sr, float q = 0.7071f)
    {
        const float w = kTwoPi * clampf (fc, 5.0f, sr * 0.45f) / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        norm ((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
};

// ---- envelope follower -------------------------------------------------------------------------------
struct Follower
{
    float env = 0, ka = 0, kr = 0;
    void set (float attackMs, float releaseMs, float sr) { ka = tcoef (attackMs, sr); kr = tcoef (releaseMs, sr); }
    float tick (float x) { const float a = std::abs (x); env = a > env ? a + ka * (env - a) : a + kr * (env - a); return env; }
    void reset() { env = 0; }
};

// ---- channel strip: hp, lp, four bands, tilt, saturation, expander ------------------------------------
struct StripParams
{
    bool hpOn = false; float hp = 60; bool lpOn = false; float lp = 18000;
    struct Band { bool on = false; float freq = 200, gain = 0, q = 0.8f; int type = 1; } b[4];   // 0 low shelf, 1 bell, 2 high shelf
    float tilt = 0;            // db, + = brighter
    float sat = 0;             // 0..1
    float gateThr = -80, gateRatio = 1, gateAtt = 1, gateRel = 80, gateRange = -60;
    float out = 0;
};
struct StripChannel
{
    Hp12 hp; Lp12 lp; std::array<Biquad, 4> band; Biquad tiltLo, tiltHi; Drive drive; Follower gateEnv;
    float gateG = 1;
    void prepare (float sr) { gateEnv.set (1, 80, sr); reset(); }
    void reset() { hp.reset(); lp.reset(); for (auto& b : band) b.reset(); tiltLo.reset(); tiltHi.reset(); drive.reset(); gateEnv.reset(); gateG = 1; }
    void set (const StripParams& p, float sr)
    {
        hp.set (p.hp, sr); lp.set (p.lp, sr);
        for (int i = 0; i < 4; ++i)
        {
            const auto& b = p.b[(size_t) i];
            if (b.type == 0) band[(size_t) i].lowShelf (b.freq, b.gain, sr);
            else if (b.type == 2) band[(size_t) i].highShelf (b.freq, b.gain, sr);
            else band[(size_t) i].peak (b.freq, std::max (0.1f, b.q), b.gain, sr);
        }
        tiltLo.lowShelf (650.0f, -p.tilt, sr, 0.6f); tiltHi.highShelf (650.0f, p.tilt, sr, 0.6f);
        gateEnv.set (p.gateAtt, p.gateRel, sr);
    }
    float tick (float x, const StripParams& p)
    {
        if (p.hpOn) x = hp.tick (x);
        for (int i = 0; i < 4; ++i) if (p.b[(size_t) i].on) x = band[(size_t) i].tick (x);
        if (std::abs (p.tilt) > 0.01f) x = tiltHi.tick (tiltLo.tick (x));
        if (p.lpOn) x = lp.tick (x);
        if (p.sat > 0.001f) x = drive.tick (x, p.sat);
        if (p.gateRatio > 1.01f)
        {
            const float e = gainToDb (gateEnv.tick (x));
            float g = 0;
            if (e < p.gateThr) g = std::max (p.gateRange, (e - p.gateThr) * (p.gateRatio - 1.0f));
            const float target = dbToGain (g);
            gateG += (target - gateG) * 0.02f;
            x *= gateG;
        }
        return x * dbToGain (p.out);
    }
};

// ---- transient designer ------------------------------------------------------------------------------
struct TransientParams { float attack = 0, sustain = 0; float speed = 0.5f; float mix = 1; };
struct TransientChannel
{
    Follower fast, slow, sus;
    void reset() { fast.reset(); slow.reset(); sus.reset(); }
    void prepare (float sr) { set (0.5f, sr); reset(); }
    void set (float speed, float sr) { fast.set (0.3f + 2.0f * speed, 20.0f + 60.0f * speed, sr); slow.set (8.0f + 30.0f * speed, 20.0f + 60.0f * speed, sr); sus.set (10.0f, 150.0f + 400.0f * speed, sr); }
    float tick (float x, const TransientParams& p)
    {
        const float f = fast.tick (x), s = slow.tick (x);
        const float att = std::max (0.0f, f - s) / (s + 1.0e-4f);           // transient excess, 0..
        const float gA = dbToGain (p.attack * 12.0f * clampf (att, 0.0f, 1.0f));
        const float sustainAmt = sus.tick (x) > 1.0e-5f ? clampf (1.0f - att, 0.0f, 1.0f) : 0.0f;
        const float gS = dbToGain (p.sustain * 12.0f * sustainAmt);
        const float y = x * gA * gS;
        return x + (y - x) * p.mix;
    }
};

// ---- sidechain compressor with pump shapes ------------------------------------------------------------
struct SideParams
{
    float threshold = -24, ratio = 4, attack = 5, release = 120, knee = 6, makeup = 0, mix = 1, range = -60;
    int key = 1;               // 0 internal, 1 external (another track)
    float keyHp = 80;
    int shape = 0;             // 0 compressor, 1 duck (gate-shaped pump), 2 pump with hold
    float hold = 60;           // ms, shape 2
};
struct SideComp
{
    Follower env; Hp12 keyHp; float sr = 48000; float grDb = 0; float holdLeft = 0; float trig = 0;
    std::atomic<float> shownGr { 0 };
    void prepare (float s) { sr = s; env.set (5, 120, sr); keyHp.set (80, sr); grDb = 0; }
    void set (const SideParams& p) { env.set (p.attack, p.release, sr); keyHp.set (p.keyHp, sr); }
    /** gain for this sample given the key sample */
    float tick (float key, const SideParams& p)
    {
        key = keyHp.tick (key);
        const float e = gainToDb (env.tick (key) + 1.0e-6f);
        float gr = 0;
        if (p.shape == 0)
        {
            const float over = e - p.threshold;
            if (over > p.knee * 0.5f) gr = over * (1.0f - 1.0f / p.ratio);
            else if (over > -p.knee * 0.5f) { const float t = over + p.knee * 0.5f; gr = t * t / (2.0f * p.knee) * (1.0f - 1.0f / p.ratio); }
            gr = -gr;
        }
        else
        {
            // duck / pump: above the threshold the signal is pushed down by the full amount, then it recovers
            if (e > p.threshold) { trig = 1.0f; holdLeft = p.hold * 0.001f * sr; }
            else if (p.shape == 2 && holdLeft > 0) --holdLeft;
            else trig = 0;
            gr = trig > 0.5f ? std::max (p.range, -(1.0f - 1.0f / p.ratio) * 24.0f) : 0.0f;
        }
        gr = std::max (p.range, gr);
        shownGr.store (grDb);
        grDb += (gr - grDb) * (gr < grDb ? 1.0f - tcoef (p.attack, sr) : 1.0f - tcoef (p.release, sr));
        return dbToGain (grDb + p.makeup);
    }
};

// ---- colour: tape / console stage with drift -----------------------------------------------------------
struct ColourParams { float drive = 0.2f, bias = 0.1f, hfLoss = 0.2f, bump = 0.3f, drift = 0.2f, mix = 1; int mode = 0; /* tape, console */ };
struct ColourChannel
{
    AdaaTanh sh; OnePole lp; Biquad bump; DcBlock dc; Rng rng; float driftV = 0, driftT = 0; float sr = 48000;
    void prepare (float s, uint32_t seed) { sr = s; rng.seed (seed); dc.set (sr); reset(); }
    void reset() { sh.reset(); lp.reset(); bump.reset(); dc.reset(); driftV = 0; }
    void set (const ColourParams& p)
    {
        lp.set (expMap (1.0f - p.hfLoss, 4000.0f, 20000.0f) * (1.0f + 0.05f * driftV), sr);
        bump.lowShelf (p.mode == 0 ? 70.0f : 120.0f, p.bump * (p.mode == 0 ? 3.0f : 1.5f), sr);
    }
    float tick (float x, const ColourParams& p)
    {
        // a slow random walk: the "drift" of a real machine, on gain and tone
        driftT += 1.0f / sr;
        if (driftT > 0.02f) { driftT = 0; driftV += rng.bi() * 0.05f * p.drift - driftV * 0.01f; driftV = clampf (driftV, -1.0f, 1.0f); }
        const float g = (1.0f + p.drive * (p.mode == 0 ? 5.0f : 3.0f)) * (1.0f + 0.03f * driftV * p.drift);
        float y = x * g + p.bias * 0.3f * p.drive;
        y = sh.tick (y) / std::tanh (std::max (1.0f, g));
        y = dc.tick (y);
        y = bump.tick (y);
        y = lp.lp (y);
        return x + (y - x) * p.mix;
    }
};

// ---- stereo tool -----------------------------------------------------------------------------------------
struct StereoParams { float width = 1, mid = 0, side = 0, haasMs = 0, monoBelow = 0, balance = 0; bool swap = false; };
struct StereoTool
{
    DelayLine haas; Svf loL, loR; float sr = 48000;
    void prepare (float s) { sr = s; haas.prepare ((int) (0.05f * sr) + 8); loL.set (120, 0.7f, sr); loR.set (120, 0.7f, sr); }
    void set (const StereoParams& p) { loL.set (std::max (20.0f, p.monoBelow), 0.7071f, sr); loR.set (std::max (20.0f, p.monoBelow), 0.7071f, sr); }
    void tick (float& l, float& r, const StereoParams& p)
    {
        if (p.swap) std::swap (l, r);
        if (p.haasMs > 0.01f) { haas.push (r); r = haas.read (std::max (1.0f, p.haasMs * 0.001f * sr)); }
        float m = (l + r) * 0.5f, s = (l - r) * 0.5f;
        m *= dbToGain (p.mid); s *= dbToGain (p.side) * p.width;
        l = m + s; r = m - s;
        if (p.monoBelow > 20.0f)
        {
            const float lowL = loL.tickLP (l), lowR = loR.tickLP (r);
            const float lowM = (lowL + lowR) * 0.5f;
            l = l - lowL + lowM; r = r - lowR + lowM;
        }
        float gl, gr; panGains (p.balance, gl, gr); l *= gl; r *= gr;
    }
};

// ---- throw: a dub send delay with a throw button and hold ---------------------------------------------
struct ThrowParams
{
    bool sync = true; int div = 9; float ms = 375; float feedback = 0.6f; float hp = 150, lp = 4000; float sat = 0.3f;
    float wow = 0.15f; float spread = 0.6f; float mix = 1; bool thrown = true; bool hold = false; float input = 1;
};
struct ThrowDelay
{
    DelayLine dl, dr; Svf hpL, hpR, lpL, lpR; AdaaTanh satL, satR; float sr = 48000; float fbL = 0, fbR = 0; double lfo = 0; float curD = 0; Rng rng; float drift = 0;
    void prepare (float s) { sr = s; dl.prepare ((int) (4.0f * sr)); dr.prepare ((int) (4.0f * sr)); reset(); }
    void reset() { dl.clear(); dr.clear(); fbL = fbR = 0; hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset(); satL.reset(); satR.reset(); }
    void process (float* L, float* R, int n, const ThrowParams& p, double bpm)
    {
        const double beats = p.sync ? divisionBeatsOf (p.div) : 0.0;
        const float ms = p.sync ? (float) (beats * 60000.0 / bpm) : p.ms;
        const float target = clampf (ms, 1.0f, 3900.0f) * 0.001f * sr;
        if (curD <= 0) curD = target;
        hpL.set (p.hp, 0.7f, sr); hpR.set (p.hp, 0.7f, sr); lpL.set (p.lp, 0.7f, sr); lpR.set (p.lp, 0.7f, sr);
        const float fb = p.hold ? 1.0f : clampf (p.feedback, 0.0f, 1.1f);
        for (int i = 0; i < n; ++i)
        {
            curD += (target - curD) * 0.0005f;
            lfo += 0.4 / sr; if (lfo > 1) lfo -= 1;
            if ((i & 255) == 0) drift += (rng.bi() * 0.3f - drift) * 0.1f;
            const float mod = 1.0f + p.wow * 0.004f * (std::sin ((float) (kTwoPi * lfo)) + drift);
            const float inL = p.hold ? 0.0f : (p.thrown ? L[i] * p.input : 0.0f), inR = p.hold ? 0.0f : (p.thrown ? R[i] * p.input : 0.0f);
            const float outL = dl.read (curD * mod), outR = dr.read (curD * mod * (1.0f + 0.003f * p.spread));
            float wl = inL + fbL * fb, wr = inR + fbR * fb;
            wl = satL.tick (wl * (1.0f + p.sat * 2.0f)) / (1.0f + p.sat * 2.0f); wr = satR.tick (wr * (1.0f + p.sat * 2.0f)) / (1.0f + p.sat * 2.0f);
            wl = lpL.tickLP (hpL.tickHP (wl)); wr = lpR.tickLP (hpR.tickHP (wr));
            dl.push (wl); dr.push (wr);
            // ping-pong: part of each side feeds the other
            fbL = outL * (1.0f - p.spread * 0.5f) + outR * p.spread * 0.5f;
            fbR = outR * (1.0f - p.spread * 0.5f) + outL * p.spread * 0.5f;
            L[i] = L[i] * (1.0f - p.mix) + outL * p.mix; R[i] = R[i] * (1.0f - p.mix) + outR * p.mix;
        }
    }
    static double divisionBeatsOf (int i)
    {
        static const double d[19] = { 1.0 / 16, 1.0 / 8, 3.0 / 16, 1.0 / 6, 0.25, 3.0 / 8, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
        return d[std::max (0, std::min (18, i))];
    }
};

// ---- spring (and freeze) ---------------------------------------------------------------------------------
struct SpringParams { float tension = 0.5f, decay = 0.5f, tone = 0.5f, drip = 0.5f, mix = 0.3f; bool freeze = false; };
struct Spring
{
    static constexpr int kAp = 12;
    std::array<Allpass, kAp> apL, apR; DelayLine loopL, loopR; Svf toneL, toneR; OnePole hpL, hpR; float fbL = 0, fbR = 0; float sr = 48000; double lfo = 0;
    void prepare (float s)
    {
        sr = s;
        for (int i = 0; i < kAp; ++i) { apL[(size_t) i].prepare ((int) (sr * (0.0011f + 0.00037f * i))); apR[(size_t) i].prepare ((int) (sr * (0.0013f + 0.00041f * i))); }
        loopL.prepare ((int) (0.2f * sr)); loopR.prepare ((int) (0.2f * sr)); hpL.set (120, sr); hpR.set (120, sr); reset();
    }
    void reset() { for (auto& a : apL) a.clear(); for (auto& a : apR) a.clear(); loopL.clear(); loopR.clear(); fbL = fbR = 0; toneL.reset(); toneR.reset(); }
    void process (float* L, float* R, int n, const SpringParams& p)
    {
        const float g = 0.5f + 0.35f * p.drip;            // dispersion: allpass coefficient
        const float loopMs = 28.0f + 40.0f * (1.0f - p.tension);
        const float fb = p.freeze ? 0.995f : 0.5f + 0.47f * p.decay;
        toneL.set (expMap (p.tone, 1200.0f, 9000.0f), 0.6f, sr); toneR.set (expMap (p.tone, 1200.0f, 9000.0f), 0.6f, sr);
        for (int i = 0; i < n; ++i)
        {
            lfo += 0.7 / sr; if (lfo > 1) lfo -= 1;
            const float wob = 1.0f + 0.002f * std::sin ((float) (kTwoPi * lfo));
            float inL = p.freeze ? 0.0f : hpL.hp (L[i]), inR = p.freeze ? 0.0f : hpR.hp (R[i]);
            float xl = inL + fbL * fb, xr = inR + fbR * fb;
            for (int k = 0; k < kAp; ++k) { xl = apL[(size_t) k].tick (xl, g); xr = apR[(size_t) k].tick (xr, g); }
            xl = toneL.tickLP (xl); xr = toneR.tickLP (xr);
            loopL.push (xl); loopR.push (xr);
            fbL = loopL.read (loopMs * 0.001f * sr * wob); fbR = loopR.read (loopMs * 0.001f * sr * 1.07f * wob);
            const float wl = (fbL + 0.3f * fbR) * 0.8f, wr = (fbR + 0.3f * fbL) * 0.8f;
            L[i] = L[i] * (1.0f - p.mix) + wl * p.mix; R[i] = R[i] * (1.0f - p.mix) + wr * p.mix;
        }
    }
};

// ---- vinyl: wow, flutter, crackle, bandwidth, saturation -------------------------------------------------
struct VinylParams { float wow = 0.3f, wowRate = 0.5f, flutter = 0.2f, crackle = 0.3f, crackleLevel = 0.3f, noise = 0.1f, bandwidth = 0.6f, lowcut = 0.1f, sat = 0.3f, mix = 1; };
struct Vinyl
{
    DelayLine dL, dR; Svf lpL, lpR, hpL, hpR; AdaaTanh sL, sR; Rng rng; PinkNoise pink; double wowPh = 0, flPh = 0; float sr = 48000; float popEnv = 0, popV = 0; float drift = 0;
    void prepare (float s, uint32_t seed) { sr = s; rng.seed (seed); dL.prepare ((int) (0.05f * sr)); dR.prepare ((int) (0.05f * sr)); reset(); }
    void reset() { dL.clear(); dR.clear(); lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); sL.reset(); sR.reset(); }
    void process (float* L, float* R, int n, const VinylParams& p)
    {
        lpL.set (expMap (p.bandwidth, 2500.0f, 18000.0f), 0.6f, sr); lpR.set (expMap (p.bandwidth, 2500.0f, 18000.0f), 0.6f, sr);
        hpL.set (expMap (p.lowcut, 20.0f, 300.0f), 0.6f, sr); hpR.set (expMap (p.lowcut, 20.0f, 300.0f), 0.6f, sr);
        const float base = 0.02f * sr;
        const float crackleRate = (2.0f + 300.0f * p.crackle * p.crackle) / sr;
        for (int i = 0; i < n; ++i)
        {
            wowPh += (0.3 + 1.2 * p.wowRate) / sr; if (wowPh > 1) wowPh -= 1;
            flPh += 7.3 / sr; if (flPh > 1) flPh -= 1;
            if ((i & 127) == 0) drift += (rng.bi() - drift) * 0.05f;
            const float d = base * (1.0f + 0.004f * p.wow * (std::sin ((float) (kTwoPi * wowPh)) + 0.5f * drift) + 0.0012f * p.flutter * std::sin ((float) (kTwoPi * flPh)));
            dL.push (L[i]); dR.push (R[i]);
            float l = dL.read (d), r = dR.read (d);
            l = lpL.tickLP (hpL.tickHP (l)); r = lpR.tickLP (hpR.tickHP (r));
            if (p.sat > 0.001f) { const float g = 1.0f + p.sat * 3.0f; l = sL.tick (l * g) / g; r = sR.tick (r * g) / g; }
            // crackle: sparse pops with a short decay; a pink floor
            if (rng.uni() < crackleRate) { popEnv = 1.0f; popV = rng.bi() * p.crackleLevel * 0.6f; }
            popEnv *= 0.93f;
            const float pop = popV * popEnv, floor = pink.tick (rng.bi()) * p.noise * 0.04f;
            l += pop + floor; r += pop * 0.8f + floor;
            L[i] = L[i] * (1.0f - p.mix) + l * p.mix; R[i] = R[i] * (1.0f - p.mix) + r * p.mix;
        }
    }
};

// ---- repeat: buffer glitch / beat repeat -------------------------------------------------------------------
struct RepeatParams
{
    bool sync = true; int gridDiv = 4; float gridMs = 125; int lenDiv = 4; float lenMs = 125; float chance = 0.3f; int repeats = 4;
    float pitchDecay = 0; float reverse = 0; float bits = 24; int rateDiv = 1; float gate = 1; float mix = 1; bool force = false; float variation = 0;
};
struct Repeater
{
    std::vector<float> bufL, bufR; int cap = 1, w = 0; float sr = 48000; Rng rng;
    bool active = false; int start = 0, len = 1, rep = 0, maxRep = 0; double pos = 0; float rate = 1; bool rev = false; long long lastTick = -1;
    float holdL = 0, holdR = 0; int holdN = 0;
    void prepare (float s, uint32_t seed) { sr = s; rng.seed (seed); cap = (int) (sr * 4.0f); bufL.assign ((size_t) cap, 0.0f); bufR.assign ((size_t) cap, 0.0f); w = 0; active = false; }
    void process (float* L, float* R, int n, const RepeatParams& p, double ppq0, double bpm)
    {
        const double spb = 60.0 / bpm * sr;
        const double gridBeats = p.sync ? ThrowDelay::divisionBeatsOf (p.gridDiv) : p.gridMs * 0.001 * bpm / 60.0;
        const double lenBeats = p.sync ? ThrowDelay::divisionBeatsOf (p.lenDiv) : p.lenMs * 0.001 * bpm / 60.0;
        const int lenS = std::max (16, std::min (cap / 2, (int) (lenBeats * spb)));
        const float q = std::pow (2.0f, std::floor (clampf (p.bits, 2.0f, 24.0f)) - 1.0f);
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) w] = L[i]; bufR[(size_t) w] = R[i];
            const double ppq = ppq0 + i / spb;
            const long long tick = (long long) std::floor (ppq / gridBeats);
            if (tick != lastTick)
            {
                lastTick = tick;
                if (! active && (p.force || rng.uni() < p.chance))
                {
                    active = true; start = w; len = lenS; rep = 0; pos = 0; rate = 1.0f; rev = rng.uni() < p.reverse;
                    maxRep = std::max (1, p.repeats + (p.variation > 0 ? (int) std::round (rng.bi() * p.variation * p.repeats) : 0));
                }
            }
            float outL = L[i], outR = R[i];
            if (active)
            {
                if (pos >= len)
                {
                    pos -= len; ++rep;
                    rate *= std::pow (2.0f, p.pitchDecay / 12.0f);
                    if (rep >= maxRep || (p.force == false && rep >= maxRep)) active = p.force;
                    if (p.force && rep >= maxRep) { rep = 0; rate = 1.0f; }
                }
                if (active)
                {
                    const double rp = rev ? (len - 1 - pos) : pos;
                    const int idx = start - len + (int) rp;
                    const float fr = (float) (rp - std::floor (rp));
                    auto at = [&] (const std::vector<float>& b, int k) { k = ((k % cap) + cap) % cap; return b[(size_t) k]; };
                    outL = lerp (at (bufL, idx), at (bufL, idx + 1), fr); outR = lerp (at (bufR, idx), at (bufR, idx + 1), fr);
                    const float env = pos < 64 ? (float) pos / 64.0f : (pos > len - 64 ? (float) (len - pos) / 64.0f : 1.0f);
                    const float g = env * (pos < len * p.gate ? 1.0f : 0.0f);
                    outL *= g; outR *= g;
                    pos += rate;
                }
            }
            // digital colour: bits and rate
            if (p.rateDiv > 1) { if (holdN <= 0) { holdL = outL; holdR = outR; holdN = p.rateDiv; } --holdN; outL = holdL; outR = holdR; }
            if (p.bits < 23.5f) { outL = std::round (outL * q) / q; outR = std::round (outR * q) / q; }
            L[i] = L[i] * (1.0f - p.mix) + outL * p.mix; R[i] = R[i] * (1.0f - p.mix) + outR * p.mix;
            if (++w >= cap) w = 0;
        }
    }
};

// ---- grain: a granular cloud from the live input ------------------------------------------------------------
struct GrainParams { float size = 80, density = 0.5f, pitch = 0, spray = 0.3f, jitter = 0.2f, reverse = 0, shape = 0.5f, mix = 0.5f; bool freeze = false; float spread = 0.5f; };
struct Grainer
{
    static constexpr int kGrains = 32;
    struct G { bool on = false; double pos = 0; float rate = 1; int len = 1, age = 0; float gl = 1, gr = 1; };
    std::vector<float> bufL, bufR; int cap = 1, w = 0; float sr = 48000; Rng rng; std::array<G, kGrains> g; float spawn = 0;
    void prepare (float s, uint32_t seed) { sr = s; rng.seed (seed); cap = (int) (sr * 2.0f); bufL.assign ((size_t) cap, 0.0f); bufR.assign ((size_t) cap, 0.0f); w = 0; for (auto& x : g) x.on = false; }
    void process (float* L, float* R, int n, const GrainParams& p)
    {
        const int len = std::max (32, (int) (p.size * 0.001f * sr));
        const float perSample = (p.density * p.density * 200.0f + 1.0f) / sr;
        for (int i = 0; i < n; ++i)
        {
            if (! p.freeze) { bufL[(size_t) w] = L[i]; bufR[(size_t) w] = R[i]; }
            spawn += perSample;
            if (spawn >= 1.0f)
            {
                spawn -= 1.0f;
                for (auto& x : g)
                    if (! x.on)
                    {
                        x.on = true; x.age = 0; x.len = (int) (len * (1.0f + rng.bi() * p.jitter * 0.5f));
                        const float back = len + rng.uni() * p.spray * (float) (cap - len - 16);
                        x.pos = w - back; x.rate = std::pow (2.0f, (p.pitch + rng.bi() * p.jitter * 2.0f) / 12.0f) * (rng.uni() < p.reverse ? -1.0f : 1.0f);
                        panGains (rng.bi() * p.spread, x.gl, x.gr);
                        break;
                    }
            }
            float outL = 0, outR = 0;
            for (auto& x : g)
            {
                if (! x.on) continue;
                const float t = (float) x.age / (float) x.len;
                const float win = p.shape < 0.5f ? (t < 0.1f ? t / 0.1f : (t > 0.9f ? (1.0f - t) / 0.1f : 1.0f)) : 0.5f - 0.5f * std::cos (kTwoPi * t);
                const double rp = x.pos + x.age * x.rate;
                const int k = (int) std::floor (rp); const float fr = (float) (rp - k);
                auto at = [&] (const std::vector<float>& b, int idx) { idx = ((idx % cap) + cap) % cap; return b[(size_t) idx]; };
                outL += lerp (at (bufL, k), at (bufL, k + 1), fr) * win * x.gl; outR += lerp (at (bufR, k), at (bufR, k + 1), fr) * win * x.gr;
                if (++x.age >= x.len) x.on = false;
            }
            L[i] = L[i] * (1.0f - p.mix) + outL * 0.5f * p.mix; R[i] = R[i] * (1.0f - p.mix) + outR * 0.5f * p.mix;
            if (++w >= cap) w = 0;
        }
    }
};

// ---- multiband distortion --------------------------------------------------------------------------------
struct MultibandParams { float xLo = 200, xHi = 2500; float drive[3] = { 0.3f, 0.3f, 0.3f }; int mode[3] = { 0, 0, 0 }; float level[3] = { 0, 0, 0 }; float mix = 1; };
struct MultibandChannel
{
    Svf lo1, lo2, hi1, hi2; AdaaTanh sh[3]; float sr = 48000;
    void prepare (float s) { sr = s; reset(); }
    void reset() { lo1.reset(); lo2.reset(); hi1.reset(); hi2.reset(); for (auto& x : sh) x.reset(); }
    void set (const MultibandParams& p) { lo1.set (p.xLo, 0.7071f, sr); lo2.set (p.xLo, 0.7071f, sr); hi1.set (p.xHi, 0.7071f, sr); hi2.set (p.xHi, 0.7071f, sr); }
    static float shape (float x, int mode, float drive, AdaaTanh& t)
    {
        const float g = 1.0f + drive * drive * 20.0f;
        switch (mode)
        {
            case 1: { float y = x * g; y = std::fmod (y + 1.0f, 4.0f); if (y < 0) y += 4.0f; y = y > 2.0f ? 3.0f - y : y - 1.0f; return y / std::sqrt (g); }   // fold
            case 2: return clampf (x * g, -1.0f, 1.0f) / std::sqrt (g);  // hard
            default: return t.tick (x * g) / std::tanh (g) * std::sqrt (std::max (1.0f, g * 0.5f)) / std::max (1.0f, g * 0.5f);
        }
    }
    float tick (float x, const MultibandParams& p)
    {
        // linkwitz-riley 4th order split: lo = lp(lp), hi = hp(hp), mid = rest
        const float lo = lo2.tickLP (lo1.tickLP (x));
        const float hi = hi2.tickHP (hi1.tickHP (x));
        const float mid = x - lo - hi;
        const float y = shape (lo, p.mode[0], p.drive[0], sh[0]) * dbToGain (p.level[0]) + shape (mid, p.mode[1], p.drive[1], sh[1]) * dbToGain (p.level[1]) + shape (hi, p.mode[2], p.drive[2], sh[2]) * dbToGain (p.level[2]);
        return x + (y - x) * p.mix;
    }
};

// ---- digital clip / bits / rate ---------------------------------------------------------------------------
struct ClipParams { float drive = 0, ceiling = 0, bits = 24; int rateDiv = 1; int mode = 0; float mix = 1; bool dc = true; };
struct ClipChannel
{
    DcBlock dcb; float hold = 0; int holdN = 0; AdaaTanh soft;
    void prepare (float sr) { dcb.set (sr); hold = 0; holdN = 0; soft.reset(); }
    float tick (float x, const ClipParams& p)
    {
        float y = x * dbToGain (p.drive);
        const float c = dbToGain (p.ceiling);
        if (p.mode == 0) y = clampf (y, -c, c);
        else if (p.mode == 1) y = soft.tick (y / c) * c;
        else { y /= c; y = std::fmod (y + 1.0f, 4.0f); if (y < 0) y += 4.0f; y = (y > 2.0f ? 3.0f - y : y - 1.0f) * c; }
        if (p.rateDiv > 1) { if (holdN <= 0) { hold = y; holdN = p.rateDiv; } --holdN; y = hold; }
        if (p.bits < 23.5f) { const float q = std::pow (2.0f, std::floor (p.bits) - 1.0f); y = std::round (y * q) / q; }
        if (p.dc) y = dcb.tick (y);
        return x + (y - x) * p.mix;
    }
};

// ---- precision tone generator ------------------------------------------------------------------------------
struct ToneParams { int wave = 0; /* sine, pulse, click, impulse, white, pink */ float hz = 1000; float pw = 0.5f; bool keytrack = false; bool gate = false; float durationMs = 10; float level = -12; float attackMs = 0; };
struct ToneVoice
{
    bool on = false; double ph = 0; int age = 0, len = 0; float hz = 1000; float vel = 1; int note = 60; bool held = false; Rng rng; PinkNoise pink; DcBlock dc;
    void start (int n, float v, const ToneParams& p, float sr)
    {
        on = true; age = 0; note = n; vel = v; held = true; ph = 0;
        hz = p.keytrack ? 440.0f * std::exp2 ((n - 69) / 12.0f) : p.hz;
        len = p.gate ? 1 << 30 : std::max (1, (int) (p.durationMs * 0.001f * sr));
        if (p.wave == 2) len = std::max (1, (int) (0.002f * sr));
        if (p.wave == 3) len = 1;
        dc.set (sr);
    }
    void release() { held = false; }
    float tick (const ToneParams& p, float sr)
    {
        if (! on) return 0.0f;
        if (p.gate && ! held) { on = false; return 0.0f; }
        float y = 0;
        switch (p.wave)
        {
            case 0: y = std::sin ((float) (kTwoPi * ph)); break;
            case 1: y = ph < p.pw ? 1.0f : -1.0f; break;
            case 2: y = std::sin ((float) (kTwoPi * ph)) * (1.0f - (float) age / (float) len); break;
            case 3: y = age == 0 ? 1.0f : 0.0f; break;
            case 4: y = rng.bi(); break;
            default: y = pink.tick (rng.bi()) * 2.0f; break;
        }
        ph += hz / sr; if (ph >= 1.0) ph -= 1.0;
        const float att = p.attackMs > 0.01f ? std::min (1.0f, (float) age / (p.attackMs * 0.001f * sr)) : 1.0f;
        if (++age >= len) on = false;
        y *= att * vel;
        return p.wave == 3 ? y : dc.tick (y);
    }
};

// ---- data burst generator (bytes → pulses) ----------------------------------------------------------------
struct DataParams { float bitRate = 2000; int bits = 64; float density = 0.5f; int seedMode = 0; /* 0 note number, 1 random each hit */ float level = -12; float tone = 1; };
struct DataVoice
{
    bool on = false; int bit = 0; double acc = 0; uint32_t word = 0; Rng rng; float cur = 0; OnePole lp; int total = 0;
    void start (int note, const DataParams& p, float sr) { on = true; bit = 0; acc = 0; total = p.bits; rng.seed (p.seedMode == 0 ? (uint32_t) (note * 2654435761u + 7) : (uint32_t) rng.next()); lp.set (expMap (p.tone, 1000.0f, 20000.0f), sr); cur = 0; }
    float tick (const DataParams& p, float sr)
    {
        if (! on) return 0.0f;
        acc += p.bitRate / sr;
        if (acc >= 1.0) { acc -= 1.0; cur = rng.uni() < p.density ? (rng.uni() < 0.5f ? 1.0f : -1.0f) : 0.0f; if (++bit >= total) on = false; }
        return lp.lp (cur);
    }
};

// ---- 4-operator fm percussion -------------------------------------------------------------------------------
struct FmParams
{
    float ratio[4] = { 1, 2, 3.5f, 7 }; float level[4] = { 1, 0.6f, 0.3f, 0 }; float decay[4] = { 0.4f, 0.25f, 0.15f, 0.1f };
    int algo = 0;              // 0: 4→3→2→1 chain, 1: (2+3+4)→1 parallel, 2: 2→1, 4→3, both out
    float feedback = 0.1f; float pitch = 0.5f; float sweep = 0.3f; float sweepMs = 40; float noise = 0; float noiseDecay = 0.1f; float drive = 0.1f; bool keytrack = true; float level_ = -6;
};
struct FmVoice
{
    bool on = false; std::array<double, 4> ph {}; std::array<float, 4> env {}; float fb = 0; float sweep = 1; float nz = 1; int note = 60; float vel = 1; Rng rng; Drive drv; Svf nf; float f0 = 100; float sr = 48000;
    void prepare (float s, uint32_t seed) { sr = s; rng.seed (seed); }
    void start (int n, float v, const FmParams& p)
    {
        on = true; note = n; vel = v; ph.fill (0); env.fill (1.0f); fb = 0; sweep = 1; nz = 1;
        f0 = p.keytrack ? 440.0f * std::exp2 ((n - 69) / 12.0f) : expMap (p.pitch, 30.0f, 2000.0f);
        nf.set (f0 * 6.0f, 1.0f, sr);
    }
    float tick (const FmParams& p)
    {
        if (! on) return 0.0f;
        const float f = f0 * (1.0f + p.sweep * 3.0f * sweep);
        sweep *= tcoef (std::max (1.0f, p.sweepMs), sr);
        float op[4];
        auto osc = [&] (int i, float mod) { ph[(size_t) i] += (f * p.ratio[i] / sr); if (ph[(size_t) i] >= 1.0) ph[(size_t) i] -= 1.0; return std::sin ((float) (kTwoPi * ph[(size_t) i]) + mod) * env[(size_t) i]; };
        for (int i = 0; i < 4; ++i) env[(size_t) i] *= t60coef (std::max (0.005f, p.decay[i]), sr);
        float out = 0;
        if (p.algo == 0)
        {
            op[3] = osc (3, fb * p.feedback * 6.0f); op[2] = osc (2, op[3] * p.level[3] * 4.0f); op[1] = osc (1, op[2] * p.level[2] * 4.0f); op[0] = osc (0, op[1] * p.level[1] * 4.0f);
            fb = op[3]; out = op[0] * p.level[0];
        }
        else if (p.algo == 1)
        {
            op[3] = osc (3, fb * p.feedback * 6.0f); op[2] = osc (2, 0); op[1] = osc (1, 0);
            op[0] = osc (0, (op[1] * p.level[1] + op[2] * p.level[2] + op[3] * p.level[3]) * 4.0f);
            fb = op[3]; out = op[0] * p.level[0];
        }
        else
        {
            op[1] = osc (1, fb * p.feedback * 6.0f); op[0] = osc (0, op[1] * p.level[1] * 4.0f);
            op[3] = osc (3, 0); op[2] = osc (2, op[3] * p.level[3] * 4.0f);
            fb = op[1]; out = op[0] * p.level[0] + op[2] * p.level[2];
        }
        if (p.noise > 0.001f) { nz *= t60coef (std::max (0.005f, p.noiseDecay), sr); out += nf.tickBPn (rng.bi()) * nz * p.noise; }
        out = drv.tick (out * 0.7f, p.drive) * vel;
        if (env[0] < 1.0e-4f && nz < 1.0e-4f) on = false;
        return out;
    }
};
} // namespace fal::dsp2

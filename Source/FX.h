// Partials: the effects that work on the finished sound. Distortion, chorus, delay, reverb, compression.
#pragma once
#include "Core.h"

namespace pt
{
struct OnePole
{
    float z = 0;
    inline float lp (float x, float c) { z += (x - z) * c; return z; }
    static float coef (float hz, float sr) { return 1.0f - std::exp (-kTwoPi * juce::jmin (hz, sr * 0.45f) / sr); }
};
inline float cutHz (float v) { return 20.0f * std::pow (1000.0f, clamp01 (v)); }       // 0..1 to 20 Hz..20 kHz

struct DelayLine
{
    std::vector<float> b; int w = 0, mask = 0;
    void resize (int n) { int s = 1; while (s < n + 4) s <<= 1; b.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
    inline void push (float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
    inline float tap (float d) const
    {
        const float rp = (float) w - 1.0f - d;
        const int i = (int) std::floor (rp); const float f = rp - (float) i;
        return b[(size_t) (i & mask)] * (1.0f - f) + b[(size_t) ((i + 1) & mask)] * f;
    }
    inline float tapInt (int d) const { return b[(size_t) ((w - 1 - d) & mask)]; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        os.reset (new juce::dsp::Oversampling<float> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true));
        os->initProcessing ((size_t) maxBlock);
        for (auto& d : chL) d.resize ((int) (sr * 0.08f));
        for (auto& d : chR) d.resize ((int) (sr * 0.08f));
        dlL.resize ((int) (sr * 4.1f)); dlR.resize ((int) (sr * 4.1f));
        rvPre[0].resize ((int) (sr * 0.32f)); rvPre[1].resize ((int) (sr * 0.32f));
        static const float base[8] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0533f, 0.0617f, 0.0683f, 0.0797f };
        for (int i = 0; i < 8; ++i) { rvLen[i] = base[i]; rvLine[i].resize ((int) (sr * 0.2f)); }
        static const float ap[4] = { 0.0047f, 0.0036f, 0.0127f, 0.0093f };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) { rvAp[c][i].resize ((int) (sr * 0.02f)); rvApLen[i] = (int) (ap[i] * sr); }
        reset();
    }
    void reset()
    {
        if (os) os->reset();
        for (auto& d : chL) d.clear(); for (auto& d : chR) d.clear();
        dlL.clear(); dlR.clear(); rvPre[0].clear(); rvPre[1].clear();
        for (auto& d : rvLine) d.clear(); for (auto& c : rvAp) for (auto& d : c) d.clear();
        for (auto& f : filt) f.z = 0; for (auto& f : rvDampF) f.z = 0; for (auto& f : rvLowF) f.z = 0;
        for (auto& e : cpEnv) e = 0; limEnv = 0; chPhase = 0; crushHold[0] = crushHold[1] = 0; crushCount = 0;
        dlTimeSm = -1;
    }

    void process (float* L, float* R, int n, const float* g, const int* order, float bps)
    {
        for (int i = 0; i < FXU::Count; ++i)
            switch (order[i])
            {
                case FXU::Distortion:  if ((int) g[GP::distType] > 0) distortion (L, R, n, g); break;
                case FXU::Chorus:      if ((int) g[GP::chOrder] > 0) chorus (L, R, n, g); break;
                case FXU::Delay:       if (g[GP::dlOn] > 0.5f) delay (L, R, n, g, bps); break;
                case FXU::Reverb:      if (g[GP::rvOn] > 0.5f) reverb (L, R, n, g); break;
                case FXU::Compression: if ((int) g[GP::cpType] > 0) compress (L, R, n, g); break;
                default: break;
            }
    }

private:
    //----------------------------------------------------------------------------------------------
    static inline float shape (int type, float x, float drive, float amt)
    {
        const float d = x * drive;
        switch (type)
        {
            case 1:  return std::tanh (d);
            case 2:  return std::sin (clampf (d, -3.14159f, 3.14159f));
            case 3:  return clampf (d, -1.0f, 1.0f) * 0.9f + 0.1f * std::tanh (d * 0.3f);
            case 4:  { const float c = clampf (d, -1.0f, 1.0f); return 1.5f * (c - c * c * c / 3.0f); }
            case 5:  return (d < 0 ? -1.0f : 1.0f) * std::log1p (std::abs (d) * 4.0f) / std::log1p (4.0f * juce::jmax (1.0f, drive * 0.5f));
            case 6:  return x / (1.0f + std::abs (x) * amt * 2.0f) * (1.0f + amt);
            case 7:  { const float ad = std::abs (d); return (d < 0 ? -1.0f : 1.0f) * ((1.0f - std::exp (-ad)) + 0.35f * std::sin (3.0f * ad) * std::exp (-ad * 0.7f)); }
            case 8:  { float t = d * 0.25f + 0.25f; t -= std::floor (t); return 4.0f * std::abs (t - 0.5f) - 1.0f; }
            case 9:  return std::sin (d * 3.14159f);
            default: return d;
        }
    }
    void distortion (float* L, float* R, int n, const float* g)
    {
        const int type = (int) g[GP::distType];
        const float amt = g[GP::distAmt], asym = g[GP::distAsym], wet = g[GP::distWet] * 2.0f, m = g[GP::distMix];
        const float am = std::abs (m);
        const float wetA = (am <= 0.5f ? 0.5f + am : 2.0f * (1.0f - am)) * wet, dryA = am <= 0.5f ? 0.5f - am : 2.0f * am - 1.0f;
        const float pol = m < 0 ? -1.0f : 1.0f;
        const float drive = 1.0f + amt * amt * 40.0f;
        const float comp = 1.0f / std::sqrt (drive);
        const float biasL = asym > 0 ? asym * 0.6f : asym * 0.6f, biasR = asym > 0 ? asym * 0.6f : -asym * 0.6f;
        const float hc = g[GP::distHiCut] >= 0.995f ? 1.0f : OnePole::coef (500.0f * std::pow (40.0f, g[GP::distHiCut]), sr);
        tmpL.resize ((size_t) n); tmpR.resize ((size_t) n);
        std::memcpy (tmpL.data(), L, sizeof (float) * (size_t) n); std::memcpy (tmpR.data(), R, sizeof (float) * (size_t) n);
        if (type >= 10)
        {
            // bit crush: fewer levels and a held sample; bit jam: plays with the bits themselves
            const float levels = std::exp2 (12.0f - 10.0f * amt);
            const int hold = 1 + (int) (amt * amt * 24.0f);
            for (int i = 0; i < n; ++i)
            {
                if (crushCount <= 0) { crushHold[0] = L[i] + biasL * 0.2f; crushHold[1] = R[i] + biasR * 0.2f; crushCount = hold; }
                --crushCount;
                for (int c = 0; c < 2; ++c)
                {
                    float x = crushHold[c];
                    if (type == 10) x = std::round (x * levels) / levels;
                    else
                    {
                        int q = (int) (clampf (x, -1.0f, 1.0f) * 32767.0f);
                        const int sh = 2 + (int) (amt * 9.0f);
                        q = (q ^ (q >> sh)) & ~((1 << sh / 2) - 1);
                        x = (float) q / 32767.0f;
                    }
                    (c == 0 ? L : R)[i] = x;
                }
            }
        }
        else
        {
            float* ch[2] = { L, R };
            juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
            auto up = os->processSamplesUp (blk);
            const size_t un = up.getNumSamples();
            float* ul = up.getChannelPointer (0); float* ur = up.getChannelPointer (1);
            const float oL = shape (type, biasL, drive, amt), oR = shape (type, biasR, drive, amt);
            for (size_t i = 0; i < un; ++i)
            {
                ul[i] = (shape (type, ul[i] + biasL, drive, amt) - oL) * (type == 6 ? 1.0f : comp * 2.0f);
                ur[i] = (shape (type, ur[i] + biasR, drive, amt) - oR) * (type == 6 ? 1.0f : comp * 2.0f);
            }
            os->processSamplesDown (blk);
        }
        for (int i = 0; i < n; ++i)
        {
            const float wl = hc >= 1.0f ? L[i] : filt[0].lp (L[i], hc), wr = hc >= 1.0f ? R[i] : filt[1].lp (R[i], hc);
            L[i] = pol * (wl * wetA + tmpL[(size_t) i] * dryA);
            R[i] = pol * (wr * wetA + tmpR[(size_t) i] * dryA);
        }
    }

    //----------------------------------------------------------------------------------------------
    void chorus (float* L, float* R, int n, const float* g)
    {
        const int order = juce::jlimit (1, 9, (int) g[GP::chOrder]);
        const float depth = g[GP::chDepth] * g[GP::chDepth] * 0.008f * sr, rate = 0.02f * std::pow (500.0f, g[GP::chSpeed] * 0.75f);
        const float baseD = (0.001f + g[GP::chDelay] * 0.03f) * sr, spread = g[GP::chSpread], cross = g[GP::chCross], m = g[GP::chMix];
        const float dry = juce::jmin (1.0f, 2.0f * (1.0f - std::abs (m))), wet = juce::jmin (1.0f, 2.0f * std::abs (m)) * (m < 0 ? -1.0f : 1.0f) / std::sqrt ((float) order);
        const float inc = rate / sr;
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            float wl = 0, wr = 0;
            chPhase += inc; if (chPhase >= 1.0f) chPhase -= 1.0f;
            for (int v = 0; v < order; ++v)
            {
                chL[v].push (xl); chR[v].push (xr);
                const float po = spread * (float) v / (float) order + (1.0f - spread) * 0.37f * (float) v * (float) v;
                const float ml = 0.5f + 0.5f * std::sin (kTwoPi * (chPhase + po)), mr = 0.5f + 0.5f * std::sin (kTwoPi * (chPhase + po + 0.25f));
                const float dv = baseD * (1.0f + spread * (float) v / (float) order);
                wl += chL[v].tap (dv + depth * ml + 1.0f); wr += chR[v].tap (dv + depth * mr + 1.0f);
            }
            const float cl = wl + cross * wr, cr = wr + cross * wl;
            L[i] = xl * dry + cl * wet; R[i] = xr * dry + cr * wet;
        }
    }

    //----------------------------------------------------------------------------------------------
    void delay (float* L, float* R, int n, const float* g, float bps)
    {
        const float t = g[GP::dlTime];
        float sec = t >= 0 ? juce::jmax (1.0f, std::round (t * 16.0f)) * 0.25f / bps : t * t * 2.0f;
        sec = clampf (sec, 0.001f, 3.9f);
        const float target = sec * sr;
        if (dlTimeSm < 0) dlTimeSm = target;
        const float sofs = g[GP::dlSofs], fb = g[GP::dlFb] * 0.98f, vol = g[GP::dlVol], pan = g[GP::dlPan];
        const int mode = (int) g[GP::dlMode];
        const bool damp = g[GP::dlDamp] > 0.5f;
        const float lc = OnePole::coef (cutHz (g[GP::dlLowCut]), sr), hc = OnePole::coef (cutHz (g[GP::dlHiCut]), sr);
        const bool lcOn = g[GP::dlLowCut] > 0.005f, hcOn = g[GP::dlHiCut] < 0.995f;
        const float inL = vol * juce::jmin (1.0f, 1.0f - pan), inR = vol * juce::jmin (1.0f, 1.0f + pan);
        for (int i = 0; i < n; ++i)
        {
            dlTimeSm += (target - dlTimeSm) * 0.0005f;
            const float dL = juce::jmax (2.0f, dlTimeSm * (1.0f - juce::jmax (0.0f, -sofs) * 0.5f)), dR = juce::jmax (2.0f, dlTimeSm * (1.0f - juce::jmax (0.0f, sofs) * 0.5f));
            float tl = dlL.tap (dL), tr = dlR.tap (dR);
            float ol = tl, orr = tr;
            if (lcOn) { ol -= filt[2].lp (ol, lc); orr -= filt[3].lp (orr, lc); }
            if (hcOn) { ol = filt[4].lp (ol, hc); orr = filt[5].lp (orr, hc); }
            const float fl = damp ? ol : tl, fr = damp ? orr : tr;
            float il = L[i] * inL, ir = R[i] * inR;
            if (mode == 0)      { dlL.push (il + fl * fb); dlR.push (ir + fr * fb); }
            else if (mode == 1) { dlL.push (il + fr * fb); dlR.push (ir + fl * fb); }
            else                { dlL.push (il + ir + fr * fb); dlR.push (fl * fb); }
            L[i] += ol; R[i] += orr;
        }
    }

    //----------------------------------------------------------------------------------------------
    void reverb (float* L, float* R, int n, const float* g)
    {
        const int color = (int) g[GP::rvColor];
        const float size = 0.35f + g[GP::rvSize] * 1.9f;
        const float decay = 0.1f * std::pow (512.0f, g[GP::rvDecay]);                 // 0.1 s .. 51 s
        const float w = g[GP::rvWet];
        const float dry = juce::jmin (1.0f, 2.0f * (1.0f - std::abs (w))), wet = juce::jmin (1.0f, 2.0f * std::abs (w)) * (w < 0 ? -0.5f : 0.5f);
        const float lc = OnePole::coef (cutHz (g[GP::rvLowCut]), sr), hc = OnePole::coef (cutHz (g[GP::rvHiCut]), sr);
        const float dampC = OnePole::coef (cutHz (g[GP::rvDamp]) * (0.6f + 0.8f * g[GP::rvSize]), sr);
        const float lowC = OnePole::coef (250.0f, sr);
        static const float lowMul[5] = { 0.45f, 0.72f, 1.0f, 1.25f, 1.6f };              // how long the bass rings compared to the rest
        const float diff = 0.7f * g[GP::rvDiff];
        const float pre = g[GP::rvPredelay] * 0.3f * sr;
        float gHi[8], gLo[8]; int len[8];
        for (int i = 0; i < 8; ++i)
        {
            len[i] = juce::jmin ((int) (sr * 0.19f), (int) (rvLen[i] * size * sr));
            const float secs = (float) len[i] / sr;
            gHi[i] = std::pow (10.0f, -3.0f * secs / decay);
            gLo[i] = juce::jmin (0.9995f, std::pow (10.0f, -3.0f * secs / (decay * lowMul[color])));
        }
        for (int s = 0; s < n; ++s)
        {
            float il = L[s], ir = R[s];
            il -= filt[6].lp (il, lc); ir -= filt[7].lp (ir, lc);
            il = filt[8].lp (il, hc); ir = filt[9].lp (ir, hc);
            rvPre[0].push (il); rvPre[1].push (ir);
            il = rvPre[0].tap (pre); ir = rvPre[1].tap (pre);
            float in[2] = { il, ir };
            for (int c = 0; c < 2; ++c)
                for (int a = 0; a < 4; ++a)
                {
                    const float d = rvAp[c][a].tapInt (rvApLen[a] + c * 7);
                    const float x = in[c] + d * diff;
                    rvAp[c][a].push (x);
                    in[c] = d - x * diff;
                }
            float o[8], sum = 0;
            for (int i = 0; i < 8; ++i) { o[i] = rvLine[i].tapInt (len[i]); sum += o[i]; }
            sum *= 0.25f;
            float ol = 0, orr = 0;
            for (int i = 0; i < 8; ++i)
            {
                float x = o[i] - sum;                                   // Householder mix
                x = rvDampF[i].lp (x, dampC);
                const float low = rvLowF[i].lp (x, lowC);
                x = (x - low) * gHi[i] + low * gLo[i];
                rvLine[i].push (x + in[i & 1]);
                if (i & 1) orr += o[i]; else ol += o[i];
            }
            L[s] = L[s] * dry + ol * wet; R[s] = R[s] * dry + orr * wet;
        }
    }

    //----------------------------------------------------------------------------------------------
    void compress (float* L, float* R, int n, const float* g)
    {
        const int type = (int) g[GP::cpType];
        const float amt = type == 1 ? 0.0f : g[GP::cpAmt];
        const float ratioK = type == 2 ? 0.5f : (type == 3 ? 0.7f : 0.85f);          // how much of the overshoot is taken away
        const float bass = type == 2 ? 2.0f : (type == 3 ? 4.0f : (type == 4 ? 6.5f : 0.0f));
        const float bandG[3] = { dbToGain ((g[GP::cpLow] - 0.5f) * 24.0f + bass * amt), dbToGain ((g[GP::cpMid] - 0.5f) * 24.0f + bass * amt * 0.3f), dbToGain ((g[GP::cpHigh] - 0.5f) * 24.0f) };
        const float thr = dbToGain (-6.0f - 18.0f * amt), makeup = dbToGain (9.0f * amt * ratioK);
        const float c1 = OnePole::coef (180.0f, sr), c2 = OnePole::coef (2800.0f, sr);
        const float att = 1.0f - std::exp (-1.0f / (0.003f * sr)), rel = 1.0f - std::exp (-1.0f / (0.12f * sr));
        const float lrel = 1.0f - std::exp (-1.0f / (0.08f * sr));
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], R[i] }, band[2][3];
            for (int c = 0; c < 2; ++c)
            {
                const float lo = filt[10 + c].lp (x[c], c1), lm = filt[12 + c].lp (x[c], c2);
                band[c][0] = lo; band[c][1] = lm - lo; band[c][2] = x[c] - lm;
            }
            float out[2] = { 0, 0 };
            for (int b = 0; b < 3; ++b)
            {
                float gr = 1.0f;
                if (type > 1)
                {
                    const float lvl = juce::jmax (std::abs (band[0][b]), std::abs (band[1][b]));
                    cpEnv[b] += (lvl - cpEnv[b]) * (lvl > cpEnv[b] ? att : rel);
                    if (cpEnv[b] > thr) gr = std::pow (thr / cpEnv[b], ratioK * amt);
                    gr *= makeup;
                }
                out[0] += band[0][b] * gr * bandG[b]; out[1] += band[1][b] * gr * bandG[b];
            }
            // limiter on the sum
            const float pk = juce::jmax (std::abs (out[0]), std::abs (out[1]));
            if (pk > limEnv) limEnv = pk; else limEnv += (pk - limEnv) * lrel;
            const float lg = limEnv > 0.891f ? 0.891f / limEnv : 1.0f;
            L[i] = out[0] * lg; R[i] = out[1] * lg;
        }
    }

    float sr = 48000.0f;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<float> tmpL, tmpR;
    OnePole filt[14];
    DelayLine chL[9], chR[9], dlL, dlR, rvPre[2], rvLine[8], rvAp[2][4];
    OnePole rvDampF[8], rvLowF[8];
    float rvLen[8]; int rvApLen[4];
    float chPhase = 0, dlTimeSm = -1, cpEnv[3] = { 0, 0, 0 }, limEnv = 0, crushHold[2] = { 0, 0 };
    int crushCount = 0;
};
} // namespace pt

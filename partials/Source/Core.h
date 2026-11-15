// Partials: shared definitions. Parameters, line shapes, modulation targets and the per-part data
// that the editor writes and the engine reads.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace pt
{
constexpr int NH = 516;        // harmonics per voice
constexpr int NX = 5;          // extra partials: 3 subs below the fundamental + 2 filter self-oscillations
constexpr int NP = 528;        // array size for NH + NX, padded
constexpr int MAXU = 9;        // unison order
constexpr int MAXV = 16;       // polyphony
constexpr float kTwoPi = 6.283185307179586f;
constexpr float kC4 = 261.6255653f;

inline float clamp01 (float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float midiHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

struct Rng
{
    uint32_t s = 0x12345678u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }          // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                                      // -1..1
};

//==================================================================================================
// Parameters
enum Kind { F, I, B, C };
struct PDef { const char* id; const char* name; float lo, hi, def; Kind kind; const char* choices; };

#define PT_FILTER_TYPES "Crude low pass|Classic low pass|Steep low pass|Wide low pass|Crude high pass|Classic high pass|Steep high pass|Wide high pass|Crude band pass|Classic band pass|Flat band pass|Leaky band pass|Band stop|Notch|Phaser|Band phaser|Comb|Custom shape 1|Custom shape 2"
#define PT_RES_TYPES "Classic|Cuberdon|Pedestal|Sedge hat|Wide bump|Double cone|Well|Wormhole|Noise|Custom 1|Custom 2|Low pass"
#define PT_INTERP "Block|Cap|Bell|Linear|Cubic|Rise|Fall"

#define PT_FILTER_PARAMS(X, p, P) \
    X(p##Type,     P " shape",              0, 18, 0,   C, PT_FILTER_TYPES) \
    X(p##Env,      P " envelope amount",   -1, 1, 1,    F, "") \
    X(p##Freq,     P " frequency",          0, 1, 1,    F, "") \
    X(p##Kb,       P " key tracking",      -2, 2, 0,    F, "") \
    X(p##Width,    P " width",              0, 1, 0.5f, F, "") \
    X(p##Res,      P " resonance",          0, 1, 0,    F, "") \
    X(p##ResWidth, P " resonance width",    0, 1, 0.5f, F, "") \
    X(p##Adapt,    P " adaptive resonance", 0, 1, 0,    B, "") \
    X(p##ResOfs,   P " resonance offset",  -2400, 2400, 0, F, "") \
    X(p##ResOsc,   P " self-oscillation",   0, 1, 0,    F, "") \
    X(p##ResType,  P " resonance type",     0, 11, 11,  C, PT_RES_TYPES) \
    X(p##ResScale, P " resonance scale",    0, 1, 0,    C, "oct|Hz") \
    X(p##Scale,    P " scale",              0, 1, 0,    C, "oct|Hz") \
    X(p##AdaptEnv, P " adaptive envelope",  0, 1, 0,    B, "")

#define PT_PART_PARAMS(X) \
    X(timbreMix,  "Timbre mix",        0, 1, 0,     F, "") \
    X(blendMode,  "Timbre blend",      0, 5, 0,     C, "Fade|Subtract|Multiply|Maximum|Minimum|Pluck") \
    X(phaseStart, "Phase start",       0, 360, 180, F, "") \
    X(phaseRand,  "Phase randomness", -1, 1, 0,     F, "") \
    X(sub1,       "Sub 1",             0, 1, 0,     F, "") \
    X(sub2,       "Sub 2",             0, 1, 0,     F, "") \
    X(sub3,       "Sub 3",             0, 1, 0,     F, "") \
    X(subMode,    "Sub mode",          0, 1, 0,     C, "Around fundamental|Below fundamental") \
    X(prot,       "Harmonic protection", 0, 1, 0,   F, "") \
    X(clip,       "Clipping threshold", 0, 1, 1,    F, "") \
    X(clipMode,   "Clipping mode",     0, 4, 0,     C, "High threshold|Soft low threshold|Sharp low threshold|Hard low threshold|Subtraction") \
    X(fxMix,      "FX dry/wet",       -1, 1, 0,     F, "") \
    X(autoGain,   "Auto-gain",         0, 1, 0,     F, "") \
    X(autoGainMode, "Auto-gain mode",  0, 1, 1,     C, "Relative|Absolute") \
    X(envVol,     "Envelope volume",  -60, 12, 0,   F, "") \
    X(vol,        "Volume",           -60, 12, 0,   F, "") \
    X(velAtkSkip, "Velocity skips attack", 0, 1, 0, B, "") \
    X(velAtkSpeed,"Velocity to attack speed", 0, 1, 0, B, "") \
    X(velRel,     "Velocity to release", 0, 1, 0,   B, "") \
    X(velVol,     "Velocity to volume", 0, 1, 1,    B, "") \
    X(lfoAmt,     "LFO amount",        0, 1, 1,     F, "") \
    X(freqMul,    "Frequency multiply", 1, 16, 1,   F, "") \
    X(freqDiv,    "Frequency divide",  1, 16, 1,    I, "") \
    X(detMul,     "Detune multiply",   0, 16, 1,    F, "") \
    X(detDiv,     "Detune divide",     1, 16, 1,    I, "") \
    X(pitchMode,  "Partial distribution", 0, 1, 0,  C, "Hz|oct") \
    X(pitch,      "Pitch",            -2400, 2400, 0, F, "") \
    X(pitchEnv,   "Pitch envelope amount", -1, 1, 1, F, "") \
    X(pan,        "Panning",          -1, 1, 0,     F, "") \
    X(vibDepth,   "Vibrato depth",     0, 100, 0,   F, "") \
    X(vibSpeed,   "Vibrato speed",     1, 16, 5,    F, "") \
    X(legato,     "Legato",            0, 1, 0,     B, "") \
    X(porta,      "Portamento",        0, 1, 0,     B, "") \
    X(glideTime,  "Glide time",       -1, 1, 0,     F, "") \
    X(glideLog,   "Glide logarithmic", 0, 1, 0,     B, "") \
    X(glideVar,   "Glide variable time", 0, 1, 0,   B, "") \
    X(glideLimit, "Glide limit",       0, 2400, 0,  F, "") \
    X(glideVel,   "Velocity to glide time", 0, 1, 0, B, "") \
    X(uniOrder,   "Unison order",      1, 9, 1,     I, "") \
    X(uniType,    "Unison type",       0, 4, 0,     C, "Classic|Uniform|Blurred|Random|Hz") \
    X(uniPan,     "Unison panning",    0, 1, 0.5f,  F, "") \
    X(uniPitch,   "Unison pitch",      0, 1, 0.35f, F, "") \
    X(uniPhase,   "Unison phase",      0, 1, 1,     F, "") \
    X(uniAlt,     "Unison alternate",  0, 1, 0,     F, "") \
    PT_FILTER_PARAMS(X, f1, "Filter 1") \
    PT_FILTER_PARAMS(X, f2, "Filter 2") \
    X(filtMix,    "Filter 1 & 2 mix", -1, 1, 1,     F, "") \
    X(blurMix,    "Blur mix",          0, 1, 1,     F, "") \
    X(blurHarm,   "Harmonic blur",     0, 1, 0,     F, "") \
    X(blurTop,    "Blur top tension",  0, 1, 0.5f,  F, "") \
    X(blurBottom, "Blur bottom tension", 0, 1, 0.5f, F, "") \
    X(blurAttack, "Time blur attack",  0, 1, 0,     F, "") \
    X(blurDecay,  "Time blur decay",   0, 1, 0,     F, "") \
    X(pluckTime,  "Pluck time",        0, 1, 1,     F, "") \
    X(pluckBlur,  "Pluck uses blur decay", 0, 1, 0, B, "") \
    X(phType,     "Phaser type",       0, 10, 0,    C, "Classic|Triangle|Eggs|Deep|Deeper|Condom|Twins|Cascade|Box|Freq|Custom") \
    X(phMix,      "Phaser mix",        0, 1, 0,     F, "") \
    X(phWidth,    "Phaser width",      0, 1, 0.5f,  F, "") \
    X(phOfs,      "Phaser offset",     0, 1, 0,     F, "") \
    X(phSpeed,    "Phaser speed",     -1, 1, 0.286f, F, "") \
    X(phKb,       "Phaser key tracking", -2, 2, 0,  F, "") \
    X(phScale,    "Phaser scale",      0, 2, 0,     C, "oct|Hz|Harmonic") \
    X(phLfo,      "Phaser width LFO",  0, 1, 0,     B, "") \
    X(prismAmt,   "Prism amount",     -1, 1, 0,     F, "") \
    X(prismMode,  "Prism mode",        0, 1, 0,     C, "Add|Multiply") \
    X(prismLvl,   "Level to prism",    0, 2, 0,     C, "Off|On|Inverted") \
    X(hzMix,      "Harmonizer mix",    0, 1, 0,     F, "") \
    X(hzWidth,    "Harmonizer width",  0, 1, 0.5f,  F, "") \
    X(hzStr,      "Harmonizer strength", 0, 1, 1,   F, "") \
    X(hzShiftOfs, "Harmonizer shift offset", 0, 9, 0, I, "") \
    X(hzShiftStep,"Harmonizer shift step", 1, 8, 1, I, "") \
    X(hzShiftMul, "Harmonizer shift mode", 0, 1, 0, C, "+|x") \
    X(hzGapOfs,   "Harmonizer gap offset", 0, 9, 0, I, "") \
    X(hzGapStep,  "Harmonizer gap step", 1, 8, 1,   I, "") \
    X(hzGapMul,   "Harmonizer gap mode", 0, 1, 1,   C, "+|x") \
    X(tremSpeed,  "Tremolo speed",     0, 1, 0.5f,  F, "") \
    X(tremGap,    "Tremolo gap",       0, 360, 90,  F, "") \
    X(tremDepth,  "Tremolo depth",     0, 1, 0,     F, "") \
    X(eqAmt,      "EQ amount",         0, 1, 1,     F, "") \
    X(imgSpeed,   "Image speed",      -2, 2, 1,     F, "") \
    X(imgCoarse,  "Image coarse speed", -1, 1, 0,   F, "") \
    X(imgTime,    "Image time offset", 0, 1, 0,     F, "") \
    X(imgSmooth,  "Image offset smoothing", -1, 1, 0, F, "") \
    X(imgLoop,    "Image looping",     0, 2, 0,     C, "One-shot|Loop|Ping pong") \
    X(imgSharp,   "Image sharpening", -1, 1, 0,     F, "") \
    X(imgGainScale, "Image gain scale", 0, 1, 0.5f, F, "") \
    X(imgGainMix, "Image gain mix",    0, 1, 1,     F, "") \
    X(imgFreqScale, "Image frequency scale", -2, 2, 1, F, "") \
    X(imgFreqMode, "Image frequency mode", 0, 3, 0, C, "Oct|Hz|Wide Hz|Unison") \
    X(imgFormant, "Image formant shift", -600, 600, 0, F, "") \
    X(imgFormMix, "Image formant mix", 0, 1, 1,     F, "") \
    X(imgGainInterp, "Image gain interpolation", 0, 6, 3, C, PT_INTERP) \
    X(imgFreqInterp, "Image frequency interpolation", 0, 6, 3, C, PT_INTERP) \
    X(imgDenoise, "Image denoise",     0, 1, 0,     F, "") \
    X(predelay,   "Predelay",         -1, 1, 0,     F, "") \
    X(polyRel,    "Polyphonic release scale", 0, 1, 1, F, "") \
    X(ramp,       "Ramping time",     -1, 1, 0.125f, F, "") \
    X(declick,    "Declick",           0, 25, 3,    F, "")

#define PT_GLOBAL_PARAMS(X) \
    X(masterVol,  "Master volume",    -60, 6, 0,    F, "") \
    X(abMix,      "Part A/B mix",      0, 1, 0.5f,  F, "") \
    X(partAOn,    "Part A on",         0, 1, 1,     B, "") \
    X(partBOn,    "Part B on",         0, 1, 0,     B, "") \
    X(modX,       "Modulation X",      0, 1, 0,     F, "") \
    X(modY,       "Modulation Y",      0, 1, 0,     F, "") \
    X(modZ,       "Modulation Z",      0, 1, 0,     F, "") \
    X(smoothMod,  "Smooth modulation", 0, 1, 1,     B, "") \
    X(invertArp,  "Invert arpeggio",   0, 1, 0,     B, "") \
    X(mono,       "Monophonic",        0, 3, 0,     C, "Off|Kill same|Kill all|Release all") \
    X(randomness, "Randomness",        0, 3, 0,     C, "Per voice|Per part voice|Per unison voice|Off") \
    X(sameMaps,   "Same random for all maps", 0, 1, 0, B, "") \
    X(partials,   "Partial count",     12, 516, 516, I, "") \
    X(precision,  "Precision",         0, 2, 0,     C, "Average|High|Perfect") \
    X(hq,         "HQ rendering",      0, 1, 1,     B, "") \
    X(sideStereo, "Side processing (A/B stereo)", 0, 1, 0, B, "") \
    X(bendRange,  "Pitch bend range",  0, 24, 2,    I, "") \
    X(strumTime,  "Strum time",        0, 1, 0,     F, "") \
    X(strumMode,  "Strum mode",        0, 3, 0,     C, "Up|Down|Alt|Random") \
    X(strumTension, "Strum tension",  -1, 1, 0,     F, "") \
    X(fxPre,      "Pre-FX volume",    -24, 12, 0,   F, "") \
    X(fxPost,     "Post-FX volume",   -24, 12, 0,   F, "") \
    X(distType,   "Distortion type",   0, 11, 0,    C, "Off|Classic|Hill|Flat hat|Cube|Log|Softsat|Rubber|Ribbon|Sincrush|Bitcrush|Bitjam") \
    X(distAmt,    "Distortion amount", 0, 1, 0.5f,  F, "") \
    X(distAsym,   "Distortion asymmetry", -1, 1, 0, F, "") \
    X(distWet,    "Distortion wet volume", 0, 1, 0.5f, F, "") \
    X(distMix,    "Distortion mix",   -1, 1, 0.5f,  F, "") \
    X(distHiCut,  "Distortion high cut", 0, 1, 0.85f, F, "") \
    X(chOrder,    "Chorus order",      0, 9, 0,     I, "") \
    X(chDepth,    "Chorus depth",      0, 1, 0.5f,  F, "") \
    X(chSpeed,    "Chorus speed",      0, 1, 0.75f, F, "") \
    X(chDelay,    "Chorus delay",      0, 1, 0.06f, F, "") \
    X(chSpread,   "Chorus spread",     0, 1, 1,     F, "") \
    X(chCross,    "Chorus cross",     -1, 1, -0.25f, F, "") \
    X(chMix,      "Chorus mix",       -1, 1, 0.5f,  F, "") \
    X(dlOn,       "Delay on",          0, 1, 0,     B, "") \
    X(dlVol,      "Delay input volume", -1, 1, 0.5f, F, "") \
    X(dlPan,      "Delay input panning", -1, 1, 0,  F, "") \
    X(dlMode,     "Delay feedback mode", 0, 2, 0,   C, "Normal|Inverted|Ping pong") \
    X(dlFb,       "Delay feedback",    0, 1, 0.5f,  F, "") \
    X(dlTime,     "Delay time",       -1, 1, 0.1875f, F, "") \
    X(dlLowCut,   "Delay low cut",     0, 1, 0,     F, "") \
    X(dlHiCut,    "Delay high cut",    0, 1, 1,     F, "") \
    X(dlDamp,     "Delay feedback damping", 0, 1, 0, B, "") \
    X(dlSofs,     "Delay stereo offset", -1, 1, 0,  F, "") \
    X(rvOn,       "Reverb on",         0, 1, 0,     B, "") \
    X(rvColor,    "Reverb color",      0, 4, 2,     C, "Brighter|Bright|Flat|Warm|Warmer") \
    X(rvLowCut,   "Reverb low cut",    0, 1, 0.19f, F, "") \
    X(rvHiCut,    "Reverb high cut",   0, 1, 0.77f, F, "") \
    X(rvPredelay, "Reverb predelay",   0, 1, 0,     F, "") \
    X(rvSize,     "Reverb room size",  0, 1, 0.4f,  F, "") \
    X(rvDiff,     "Reverb diffusion",  0, 1, 1,     F, "") \
    X(rvDecay,    "Reverb decay",      0, 1, 0.45f, F, "") \
    X(rvDamp,     "Reverb high damping", 0, 1, 0.77f, F, "") \
    X(rvWet,      "Reverb wet",       -1, 1, 0.5f,  F, "") \
    X(cpType,     "Compression type",  0, 4, 0,     C, "Off|Limiting|Warming|Heating|Burning") \
    X(cpAmt,      "Compression amount", 0, 1, 0.5f, F, "") \
    X(cpLow,      "Compression low band", 0, 1, 0.5f, F, "") \
    X(cpMid,      "Compression mid band", 0, 1, 0.5f, F, "") \
    X(cpHigh,     "Compression high band", 0, 1, 0.5f, F, "")

#define PT_ENUM(id, name, lo, hi, def, kind, ch) id,
#define PT_DEF(id, name, lo, hi, def, kind, ch) { #id, name, (float) (lo), (float) (hi), (float) (def), kind, ch },
namespace PP { enum : int { PT_PART_PARAMS (PT_ENUM) Count }; }
namespace GP { enum : int { PT_GLOBAL_PARAMS (PT_ENUM) Count }; }
inline const PDef partDefs[] = { PT_PART_PARAMS (PT_DEF) };
inline const PDef globalDefs[] = { PT_GLOBAL_PARAMS (PT_DEF) };
constexpr int kFilterStride = PP::f2Type - PP::f1Type;

//==================================================================================================
// Line shapes: the point lists behind every envelope, map and harmonic graph
enum SegType { Hold, Smooth, Single, Single2, Single3, Double, Double2, Double3, HalfSine, Stairs, SmoothStairs, Pulse, WaveTri, WaveSine, NumSegTypes };
inline const char* const segTypeNames[] = { "Hold", "Smooth", "Single curve", "Single curve 2", "Single curve 3", "Double curve", "Double curve 2",
                                            "Double curve 3", "Half sine", "Stairs", "Smooth stairs", "Pulse", "Wave (triangle)", "Wave (sine)" };

struct Pt { float x = 0, y = 0, t = 0; uint8_t type = Single; uint8_t arp = 0; };   // arp: 0 none, 1 previous, 2 same, 3 next

struct Curve
{
    std::vector<Pt> pts;
    std::vector<float> raw;     // per-harmonic data from analysis; overrides the points when present

    static float bend (float u, float t)
    {
        if (std::abs (t) < 1.0e-3f) return u;
        const float k = t * 8.0f;
        return (std::exp (k * u) - 1.0f) / (std::exp (k) - 1.0f);
    }
    static float seg (int type, float u, float t)
    {
        const float pi = 3.14159265f;
        switch (type)
        {
            case Hold:    return u >= 1.0f ? 1.0f : 0.0f;
            case Smooth:  return bend (u * u * (3.0f - 2.0f * u), t);
            case Single:  return bend (u, t);
            case Single2: return bend (u, t * 2.0f);
            case Single3: return std::pow (u, std::exp2 (t * 4.0f));
            case Double:  return u < 0.5f ? 0.5f * bend (2 * u, t) : 1.0f - 0.5f * bend (2 - 2 * u, t);
            case Double2: return u < 0.5f ? 0.5f * bend (2 * u, t * 2) : 1.0f - 0.5f * bend (2 - 2 * u, t * 2);
            case Double3: { float p = std::exp2 (t * 4.0f); return u < 0.5f ? 0.5f * std::pow (2 * u, p) : 1.0f - 0.5f * std::pow (2 - 2 * u, p); }
            case HalfSine: return bend (0.5f - 0.5f * std::cos (pi * u), t);
            case Stairs:  { int n = 2 + (int) ((t + 1.0f) * 7.0f); return juce::jmin (1.0f, std::floor (u * (float) n) / (float) (n - 1)); }
            case SmoothStairs: { int n = 2 + (int) ((t + 1.0f) * 7.0f); float p = u * (float) n, f = p - std::floor (p); f = f * f * (3 - 2 * f);
                                 return juce::jmin (1.0f, (std::floor (p) + f) / (float) n); }
            case Pulse:   { int n = 1 + (int) ((t + 1.0f) * 8.0f); return u >= 1.0f ? 1.0f : (float) (((int) (u * 2.0f * (float) n)) & 1); }
            case WaveTri: { float n = 0.5f + std::floor ((t + 1.0f) * 8.0f); float p = u * n; p -= std::floor (p); return p < 0.5f ? 2 * p : 2 - 2 * p; }
            case WaveSine: { float n = 0.5f + std::floor ((t + 1.0f) * 8.0f); return 0.5f - 0.5f * std::cos (kTwoPi * n * u); }
            default: return u;
        }
    }
    float eval (float x) const
    {
        if (! raw.empty())
        {
            const float p = clamp01 (x) * (float) (raw.size() - 1);
            const int i = (int) p, j = juce::jmin (i + 1, (int) raw.size() - 1);
            return raw[(size_t) i] + (raw[(size_t) j] - raw[(size_t) i]) * (p - (float) i);
        }
        const int n = (int) pts.size();
        if (n == 0) return 0.0f;
        if (x <= pts[0].x) return pts[0].y;
        if (x >= pts[(size_t) n - 1].x) return pts[(size_t) n - 1].y;
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) { const int m = (lo + hi) / 2; if (pts[(size_t) m].x <= x) lo = m; else hi = m; }
        const Pt& a = pts[(size_t) lo]; const Pt& b = pts[(size_t) hi];
        const float w = b.x - a.x;
        const float u = w > 1.0e-9f ? (x - a.x) / w : 1.0f;
        return a.y + (b.y - a.y) * seg (b.type, u, b.t);
    }
    bool empty() const { return pts.empty() && raw.empty(); }
    float endX() const { return pts.empty() ? 0.0f : pts.back().x; }
    void line (std::initializer_list<std::pair<float, float>> xy) { pts.clear(); raw.clear(); for (auto& p : xy) pts.push_back ({ p.first, p.second, 0, Single, 0 }); }
    void sort() { std::stable_sort (pts.begin(), pts.end(), [] (const Pt& a, const Pt& b) { return a.x < b.x; }); }

    juce::String toString() const
    {
        juce::String s;
        if (! raw.empty())
        {
            juce::MemoryBlock mb (raw.data(), raw.size() * sizeof (float));
            return "raw:" + mb.toBase64Encoding();
        }
        for (auto& p : pts)
            s << juce::String (p.x, 6) << ',' << juce::String (p.y, 6) << ',' << juce::String (p.t, 4) << ',' << (int) p.type << ',' << (int) p.arp << ';';
        return s;
    }
    void fromString (const juce::String& s)
    {
        pts.clear(); raw.clear();
        if (s.startsWith ("raw:"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (s.substring (4)))
            {
                raw.resize (mb.getSize() / sizeof (float));
                if (! raw.empty()) std::memcpy (raw.data(), mb.getData(), raw.size() * sizeof (float));
            }
            return;
        }
        for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
        {
            auto f = juce::StringArray::fromTokens (tok, ",", "");
            if (f.size() >= 2)
                pts.push_back ({ f[0].getFloatValue(), f[1].getFloatValue(), f.size() > 2 ? f[2].getFloatValue() : 0.0f,
                                 (uint8_t) (f.size() > 3 ? f[3].getIntValue() : Single), (uint8_t) (f.size() > 4 ? f[4].getIntValue() : 0) });
        }
    }
};

//==================================================================================================
// Shaping targets: one graph each, turned into a 516-entry table for the engine
namespace SH { enum : int { LocalEQ, Phase, Timbre1, Timbre2, HarmRand, HarmProt, HarmClip, HarmPrism, HarmUniPitch, ImgFormMut,
                            FiltShape1, FiltShape2, ResShape1, ResShape2, PluckShape, PhaserShape, HzDist, GlobalEQ,
                            F1Mask, F2Mask, F1ResMask, F2ResMask, PhMask, HzMask, BlurMask, ImgFreqMask, Count }; }

enum Axis { AxHarm, AxFreq, AxUnit };      // x is harmonic number (log), absolute frequency (log) or plain 0..1
enum YMap { YLevel, YEq, YUni, YBi };      // how the 0..1 graph height becomes a value
struct ShapeDef { const char* name; Axis axis; YMap ymap; };
inline const ShapeDef shapeDefs[SH::Count] = {
    { "Local EQ", AxHarm, YEq }, { "Harmonic phase", AxHarm, YBi }, { "Timbre 1 harmonic level", AxHarm, YLevel }, { "Timbre 2 harmonic level", AxHarm, YLevel },
    { "Harmonic randomness", AxHarm, YUni }, { "Harmonic protection", AxHarm, YUni }, { "Harmonic clipping", AxHarm, YUni }, { "Harmonic prism", AxHarm, YBi },
    { "Harmonic unison pitch", AxHarm, YUni }, { "Image formant mutation", AxHarm, YBi },
    { "Filter shape 1", AxUnit, YUni }, { "Filter shape 2", AxUnit, YUni }, { "Resonance shape 1", AxUnit, YUni }, { "Resonance shape 2", AxUnit, YUni },
    { "Pluck shape", AxHarm, YUni }, { "Phaser shape", AxUnit, YUni }, { "Harmonizer distribution", AxUnit, YUni }, { "Global EQ", AxFreq, YEq },
    { "Filter 1 mask", AxFreq, YUni }, { "Filter 2 mask", AxFreq, YUni }, { "Filter 1 resonance mask", AxFreq, YUni }, { "Filter 2 resonance mask", AxFreq, YUni },
    { "Phaser mask", AxFreq, YUni }, { "Harmonizer local mask", AxHarm, YUni }, { "Blur local mask", AxHarm, YUni }, { "Image freq local mask", AxHarm, YUni } };

constexpr float kLog2Fmin = 4.0f;          // 16 Hz: left edge of the frequency axis
constexpr float kFreqOctaves = 10.5f;      // up to about 23 kHz
inline float harmToX (int h) { return std::log2 ((float) (h + 1)) / std::log2 ((float) NH); }        // h is zero-based
inline float levelFromY (float y) { const float g = 2.0f * y; return g * g * g; }                     // 0.5 is unity, 1.0 is +18 dB
inline float yFromLevel (float g) { return clamp01 (0.5f * std::cbrt (juce::jmax (0.0f, g))); }
inline float eqFromY (float y) { return y <= 0.002f ? 0.0f : std::pow (10.0f, (y - 0.5f) * 2.4f); }   // +-24 dB

//==================================================================================================
// Articulation: what can move a target over time or per note
namespace TG { enum : int { Pan, Volume, FxMix, TimbreMix, F1Freq, F2Freq, F1Width, F2Width, F1Res, F2Res, F1ResWidth, F2ResWidth, F1ResOfs, F2ResOfs,
                            FiltMix, PluckAmt, PhMix, PhWidth, PhOfs, HzMix, HzWidth, Clip, BlurHarm, PrismAmt, Pitch, VibDepth, UniPitch,
                            ImgTime, ImgSpeed, ImgFormant, ImgFreqScale,
                            // mapping targets (no envelope or LFO)
                            F1Env, F2Env, EnvAtkTime, EnvAtkScale, EnvDecScale, EnvSusOfs, EnvRelScale, LfoAmt, LfoSpeed, LfoPhase, UniPhase, Predelay, Count,
                            FirstMapping = F1Env }; }
enum Combine { UProd, BProd, BSum };
struct TargetInfo { const char* name; int pp; Combine mode; float range; float base; };
inline const TargetInfo targetInfo[TG::Count] = {
    { "Panning", PP::pan, BSum, 1, 0 }, { "Volume", -1, UProd, 1, 1 }, { "Effects dry / wet mix", PP::fxMix, BProd, 1, 0 }, { "Timbre 1 & 2 mix", PP::timbreMix, UProd, 1, 0 },
    { "Filter 1 frequency", PP::f1Freq, BSum, 1, 0 }, { "Filter 2 frequency", PP::f2Freq, BSum, 1, 0 }, { "Filter 1 width", PP::f1Width, UProd, 1, 0 }, { "Filter 2 width", PP::f2Width, UProd, 1, 0 },
    { "Filter 1 resonance amount", PP::f1Res, UProd, 1, 0 }, { "Filter 2 resonance amount", PP::f2Res, UProd, 1, 0 },
    { "Filter 1 resonance width", PP::f1ResWidth, UProd, 1, 0 }, { "Filter 2 resonance width", PP::f2ResWidth, UProd, 1, 0 },
    { "Filter 1 resonance offset", PP::f1ResOfs, BSum, 2400, 0 }, { "Filter 2 resonance offset", PP::f2ResOfs, BSum, 2400, 0 },
    { "Filter 1 & 2 mix", PP::filtMix, BProd, 1, 0 }, { "Pluck amount", PP::pluckTime, BSum, 1, 0 }, { "Phaser mix", PP::phMix, UProd, 1, 0 },
    { "Phaser width", PP::phWidth, BSum, 1, 0 }, { "Phaser offset", PP::phOfs, BSum, 1, 0 }, { "Harmonizer mix", PP::hzMix, UProd, 1, 0 }, { "Harmonizer width", PP::hzWidth, UProd, 1, 0 },
    { "Harmonic clipping threshold", PP::clip, UProd, 1, 0 }, { "Harmonic blur amount", PP::blurHarm, UProd, 1, 0 }, { "Prism amount", PP::prismAmt, BProd, 1, 0 },
    { "Pitch", PP::pitch, BSum, 4800, 0 }, { "Pitch vibrato depth", PP::vibDepth, UProd, 1, 0 }, { "Unison pitch thickness", PP::uniPitch, UProd, 1, 0 },
    { "Image time offset", PP::imgTime, BSum, 1, 0 }, { "Image fine speed", PP::imgSpeed, BProd, 1, 0 }, { "Image formant shift", PP::imgFormant, BSum, 600, 0 },
    { "Image frequency pixel scale", PP::imgFreqScale, BProd, 1, 0 },
    { "Filter 1 envelope amount", PP::f1Env, UProd, 1, 0 }, { "Filter 2 envelope amount", PP::f2Env, UProd, 1, 0 },
    { "Global envelope attack time", -1, BSum, 1, 0 }, { "Global envelope attack scale", -1, UProd, 1, 1 }, { "Global envelope decay scale", -1, UProd, 1, 1 },
    { "Global envelope sustain offset", -1, BSum, 1, 0 }, { "Global envelope release scale", -1, UProd, 1, 1 },
    { "Global LFO amount", PP::lfoAmt, BProd, 1, 0 }, { "Global LFO speed", -1, BProd, 1, 1 }, { "Global LFO phase", -1, BSum, 1, 0 },
    { "Unison phase", PP::uniPhase, BProd, 1, 0 }, { "Predelay", PP::predelay, BProd, 1, 0 } };

namespace MAP { enum : int { Key, Vel, ModX, ModY, ModZ, Random, Unison, Held, XSpeed, Count }; }
inline const char* const mapNames[MAP::Count] = { "Keyboard mapping", "Velocity mapping", "Modulation X mapping", "Modulation Y mapping", "Modulation Z mapping",
                                                  "Random mapping", "Unison index mapping", "Held index mapping", "Mod X speed mapping" };

struct EnvDef
{
    bool on = false, tempo = false, global = false;
    Curve c;                    // x in seconds (or beats when tempo is on), y 0..1
    int sustain = -1, loopStart = -1;
    void adsr (float a, float d, float s, float r)
    {
        c.pts.clear();
        c.pts.push_back ({ 0, 0, 0, Single, 0 });
        c.pts.push_back ({ a, 1, 0, Single, 0 });
        c.pts.push_back ({ a + d, s, -0.4f, Single, 0 });
        c.pts.push_back ({ a + d + r, 0, -0.4f, Single, 0 });
        sustain = 2; loopStart = -1;
    }
};
struct LfoDef
{
    bool on = false, tempo = false, global = false;
    float amt = 1.0f, speed = 0.5f, tension = 0.32f, skew = 0.0f, pw = 0.0f, phase = 0.0f;
    Curve amp;                  // optional amplitude over time (x seconds, y 0..1)
};
struct TargetDef
{
    EnvDef env; LfoDef lfo;
    bool mapOn[MAP::Count] = {};
    Curve map[MAP::Count];
    float smooth = 0.0f;        // 0..1, up to half a second
    bool active() const
    {
        if (env.on || lfo.on) return true;
        for (bool m : mapOn) if (m) return true;
        return false;
    }
};

namespace UNIT { enum : int { Prism, Harmonizer, Blur, Pluck, Filter, Phaser, EQ, HarmProt, HarmClip, VolumeArt, Count }; }
inline const char* const unitNames[UNIT::Count] = { "PRISM", "HARMONIZER", "BLUR", "PLUCK", "FILTERS", "PHASER", "EQ", "HARM PROT", "HARM CLIP", "VOLUME ART" };
namespace FXU { enum : int { Distortion, Chorus, Delay, Reverb, Compression, Count }; }
inline const char* const fxNames[FXU::Count] = { "DISTORTION", "CHORUS", "DELAY", "REVERB", "COMPRESSION" };

struct ImageData;

// Everything in a part that is not a host parameter. The editor edits a copy and hands the engine a new one.
struct PartData
{
    Curve shape[SH::Count];
    float tab[SH::Count][NH];
    TargetDef tg[TG::Count];
    int unitOrder[UNIT::Count];
    std::shared_ptr<const ImageData> image;

    PartData() { reset(); }
    void reset()
    {
        for (int i = 0; i < UNIT::Count; ++i) unitOrder[i] = i;
        for (auto& t : tg) t = TargetDef();
        tg[TG::Volume].env.adsr (0.002f, 0.3f, 1.0f, 0.15f);
        tg[TG::Volume].env.on = true;   // the amp envelope plays from the start, so attack / decay / sustain / release always do something
        image.reset();
        for (int s = 0; s < SH::Count; ++s) defaultShape (s);
        renderAll();
    }
    void defaultShape (int s)
    {
        Curve& c = shape[s];
        c.pts.clear(); c.raw.clear();
        switch (s)
        {
            case SH::LocalEQ: case SH::GlobalEQ: case SH::Phase: case SH::ImgFormMut: case SH::Timbre1: c.line ({ { 0, 0.5f }, { 1, 0.5f } }); break;
            case SH::Timbre2: c.raw.resize (NH); for (int h = 0; h < NH; ++h) c.raw[(size_t) h] = (h & 1) ? 0.0f : 0.5f; break;
            case SH::HarmRand: c.line ({ { 0, 0 }, { 1, 0 } }); break;
            case SH::HarmProt: c.line ({ { 0, 1 }, { 0.1f, 0 }, { 1, 0 } }); break;
            case SH::HarmPrism: c.raw.resize (NH); for (int h = 0; h < NH; ++h) c.raw[(size_t) h] = 0.5f + 0.5f * std::sin (kTwoPi * std::log2 ((float) (h + 1))); break;
            case SH::FiltShape1: case SH::FiltShape2: c.line ({ { 0, 1 }, { 0.5f, 1 }, { 0.7f, 0 }, { 1, 0 } }); break;
            case SH::ResShape1: case SH::ResShape2: c.line ({ { 0, 0 }, { 0.4f, 0 }, { 0.5f, 1 }, { 0.6f, 0 }, { 1, 0 } }); break;
            case SH::PluckShape: c.line ({ { 0, 1 }, { 1, 0 } }); break;
            case SH::PhaserShape: c.line ({ { 0, 1 }, { 0.5f, 0 }, { 1, 1 } }); break;
            default: c.line ({ { 0, 1 }, { 1, 1 } }); break;
        }
    }
    void render (int s)
    {
        const Curve& c = shape[s];
        const ShapeDef& d = shapeDefs[s];
        for (int i = 0; i < NH; ++i)
        {
            float y;
            if (c.raw.size() == (size_t) NH) y = c.raw[(size_t) i];
            else y = c.eval (d.axis == AxHarm ? harmToX (i) : (float) i / (float) (NH - 1));
            switch (d.ymap)
            {
                case YLevel: y = levelFromY (y); break;
                case YEq:    y = eqFromY (y); break;
                case YBi:    y = 2.0f * y - 1.0f; break;
                default: break;
            }
            tab[s][i] = y;
        }
    }
    void renderAll() { for (int s = 0; s < SH::Count; ++s) render (s); }
};
} // namespace pt

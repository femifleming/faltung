#include "Products.h"
#include "dsp/Sequencer.h"

namespace fal
{
static juce::String ui_pm() { return juce::String::fromUTF8 ("\xc2\xb1"); }
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using APFA = juce::AudioParameterFloatAttributes;
using NR = juce::NormalisableRange<float>;

const ProductInfo& info (Pid p)
{
    static const ProductInfo t[kNumProducts] = {
        { Pid::kt1, "kt1", "kit",    "eight-voice drum synth + microtime sequencer", Kind::Machine, Style::Snd,   0xffc9b53b, 0xffd8c64e, VType::Kick,  1000, 640 },
        { Pid::kt2, "kt2", "loop",   "loop slicer + sequencer, cut-ups from any break", Kind::Machine, Style::Oval, 0xfff4f2ec, 0xff2f7fd1, VType::Kick, 1000, 640 },
        { Pid::bd1, "bd1", "kick",   "tok / thud / click kick designer",            Kind::Module,  Style::Fell,  0xffb88a26, 0xffe3bf5f, VType::Kick,  760, 480 },
        { Pid::sd2, "sd2", "snare",  "tuned modes + wires",                          Kind::Module,  Style::Snd,   0xff9cc788, 0xff5e8f6a, VType::Snare, 760, 480 },
        { Pid::rm3, "rm3", "rim",    "modal rim, wood and tok",                      Kind::Module,  Style::Snd,   0xff8ec3e6, 0xff6e80c8, VType::Rim,   760, 480 },
        { Pid::hh4, "hh4", "hats",   "metallic hats, closed / open with choke",      Kind::Module,  Style::Snd,   0xff8d9bd6, 0xffcfe6f0, VType::Hat,   760, 480 },
        { Pid::cp6, "cp6", "clap",   "burst-train clap, snap and room",              Kind::Module,  Style::Snd,   0xffa9bccf, 0xffee8e6e, VType::Clap,  760, 480 },
        { Pid::pc7, "pc7", "perc",   "membranes, bells, glass",                      Kind::Module,  Style::Snd,   0xfff6c590, 0xff8ec3e6, VType::Perc,  760, 480 },
        { Pid::cl8, "cl8", "click",  "ticks, pops, dust and blips",                  Kind::Module,  Style::Stdio, 0xffeef6fa, 0xff9fd3ec, VType::Click, 760, 480 },
        { Pid::tm9, "tm9", "tom",    "toms and minimal blips",                       Kind::Module,  Style::Fell,  0xff76a582, 0xffb9dcbf, VType::Tom,   760, 480 },
        { Pid::mc3, "mc3", "micro",  "live micro-sampling: cuts, grains, clicks",    Kind::Effect,  Style::Snd,   0xffee8e6e, 0xfff6c590, VType::Kick,  760, 480 },
        { Pid::dl4, "dl4", "dub",    "tape / bbd echo, filter in the loop",          Kind::Effect,  Style::Oval,  0xfffbfbf8, 0xff2f7fd1, VType::Kick,  760, 480 },
        { Pid::rv5, "rv5", "room",   "small rooms, plates, gated air",               Kind::Effect,  Style::Stdio, 0xfff4f9fb, 0xff9fd3ec, VType::Kick,  760, 480 },
        { Pid::sw6, "sw6", "swing",  "moves the audio onto a groove",                Kind::Effect,  Style::Fell,  0xff79e04a, 0xffb7f39a, VType::Kick,  760, 480 },
        { Pid::fl7, "fl7", "filter", "3-band isolator + resonant sweep",             Kind::Effect,  Style::Snd,   0xff8593d4, 0xffcfe6f0, VType::Kick,  760, 480 },
        { Pid::gl8, "gl8", "glue",   "bus compressor + tape saturation",             Kind::Effect,  Style::Stdio, 0xffdfe9ed, 0xff8fa9b5, VType::Kick,  760, 480 },
        { Pid::wb1, "wb1", "wub",    "dubstep + neuro bass synth",                   Kind::Synth,   Style::Fell,  0xff2f4a52, 0xff7fd3c8, VType::Kick, 1100, 760 },
    };
    return t[(int) p];
}

const KitVoiceInfo& kitVoice (int v)
{
    static const KitVoiceInfo k[kKitVoices] = {
        { "bd1", VType::Kick,  0xff5e8f6a, 36 },
        { "sd2", VType::Snare, 0xff8dbb7a, 37 },
        { "rm3", VType::Rim,   0xff8ec3e6, 38 },
        { "hh4", VType::Hat,   0xff6e80c8, 39 },
        { "oh5", VType::Hat,   0xffee8e6e, 40 },
        { "cp6", VType::Clap,  0xff97adc4, 41 },
        { "pc7", VType::Perc,  0xfff6c590, 42 },
        { "cl8", VType::Click, 0xffcfe6f0, 43 },
    };
    return k[std::clamp (v, 0, kKitVoices - 1)];
}

juce::String kitPrefix (int v) { return "v" + juce::String (v + 1) + "_"; }

static juce::String num (float v, int dp) { return dp <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, dp); }
static juce::String hz (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " khz";
    return num (f, f < 100.0f ? 1 : 0) + " hz";
}
static juce::String ms (float s)
{
    if (s >= 1.0f) return juce::String (s, 2) + " s";
    return num (s * 1000.0f, s < 0.01f ? 1 : 0) + " ms";
}

juce::String slotText (VType t, int slot, float v)
{
    switch (slot)
    {
        case 0: return t == VType::Hat ? "x" + juce::String (tuneHz (t, v), 2) : hz (tuneHz (t, v));
        case 1: return ms (decaySec (t, v));
        case 3:
            if (t == VType::Clap) return juce::String (2 + (int) std::round (v * 4.0f)) + " claps";
            if (t == VType::Click) return ms (expMap (v, 0.0008f, 0.04f));
            break;
        case 5:
            if (t == VType::Clap) return ms (expMap (v, 0.004f, 0.026f));
            return "+" + juce::String (sweepSemis (t, v), 1) + " st";
        case 6:
            if (t == VType::Kick) return ms (expMap (v, 0.002f, 0.14f));
            if (t == VType::Tom) return ms (expMap (v, 0.003f, 0.22f));
            if (t == VType::Snare) return hz (expMap (v, 1800.0f, 13000.0f));
            if (t == VType::Hat) return hz (expMap (v, 5000.0f, 15000.0f));
            if (t == VType::Click) return hz (expMap (v, 30.0f, 4000.0f));
            break;
        default: break;
    }
    return juce::String (v, 2);
}

const VoiceParams& defaultVoice (VType t)
{
    static VoiceParams d[kNumVTypes];
    static bool init = false;
    if (! init)
    {
        auto set = [] (VoiceParams& p, int model, std::array<float, 8> s, float level, float pan = 0)
        { p.model = model; p.s = s; p.level = level; p.pan = pan; p.vel = 0.6f; };
        set (d[0], 0, { 0.40f, 0.42f, 0.35f, 0.25f, 0.45f, 0.42f, 0.30f, 0.15f }, 0.86f);
        set (d[1], 0, { 0.45f, 0.40f, 0.45f, 0.55f, 0.55f, 0.20f, 0.55f, 0.10f }, 0.80f);
        set (d[2], 2, { 0.46f, 0.22f, 0.68f, 0.41f, 0.12f, 0.30f, 0.55f, 0.00f }, 0.80f, -0.12f);
        set (d[3], 0, { 0.50f, 0.32f, 0.30f, 0.60f, 0.40f, 0.10f, 0.55f, 0.00f }, 0.74f, 0.15f);
        set (d[4], 1, { 0.40f, 0.55f, 0.50f, 0.55f, 0.45f, 0.30f, 0.45f, 0.10f }, 0.80f);
        set (d[5], 0, { 0.40f, 0.30f, 0.50f, 0.50f, 0.30f, 0.45f, 0.30f, 0.00f }, 0.80f, 0.05f);
        set (d[6], 0, { 0.35f, 0.30f, 0.55f, 0.60f, 0.10f, 0.20f, 0.45f, 0.00f }, 0.80f, -0.3f);
        set (d[7], 0, { 0.70f, 0.30f, 0.60f, 0.40f, 0.20f, 0.00f, 0.30f, 0.00f }, 0.70f, 0.35f);
        init = true;
    }
    return d[(int) t];
}

// -------------------------------------------------------------------------------------------------
namespace
{
struct L
{
    juce::AudioProcessorValueTreeState::ParameterLayout& lay;
    void f (const juce::String& id, const juce::String& name, NR r, float def, std::function<juce::String (float)> fmt = {}, const juce::String& unit = {})
    {
        auto a = APFA().withLabel (unit);
        if (fmt) a = a.withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); });
        lay.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def, a));
    }
    void c (const juce::String& id, const juce::String& name, juce::StringArray items, int def)
    { lay.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def)); }
    void b (const juce::String& id, const juce::String& name, bool def)
    {
        lay.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def,
                 juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return juce::String (v ? "on" : "off"); })));
    }
};

juce::StringArray modelNames (VType t)
{
    juce::StringArray a;
    for (auto* m : vtypeInfo (t).models) a.add (m);
    return a;
}
juce::StringArray divNames()
{
    juce::StringArray a;
    for (auto& d : divisions()) a.add (d.name);
    return a;
}
auto dbText = [] (float v) { return v <= -39.5f ? juce::String ("kill") : (v > 0 ? "+" : "") + juce::String (v, 1) + " db"; };
auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
auto plain2 = [] (float v) { return juce::String (v, 2); };
auto levelText = [] (float v) { return v <= 0.001f ? juce::String ("off") : juce::String (-48.0f + 54.0f * v, 1) + " db"; };
auto panText = [] (float v)
{
    const int p = juce::roundToInt (v * 50.0f);
    return p == 0 ? juce::String ("c") : (p < 0 ? "l " + juce::String (-p) : "r " + juce::String (p));
};

void voiceParams (L& l, VType t, const juce::String& pre, const juce::String& namePre, const VoiceParams& d)
{
    const auto& vi = vtypeInfo (t);
    l.c (pre + "model", namePre + "model", modelNames (t), d.model);
    for (int s = 0; s < kSlots; ++s)
        l.f (pre + vi.slot[(size_t) s], namePre + vi.slot[(size_t) s], NR (0.0f, 1.0f), d.s[(size_t) s],
             [t, s] (float v) { return slotText (t, s, v); });
    l.f (pre + "level", namePre + "level", NR (0.0f, 1.0f), d.level, levelText);
    l.f (pre + "pan", namePre + "pan", NR (-1.0f, 1.0f), d.pan, panText);
    l.f (pre + "vel", namePre + "velocity", NR (0.0f, 1.0f), d.vel, pct);
}

juce::String waveText (float v)
{
    static const char* n[5] = { "sine", "tri", "saw", "square", "hard" };
    const int i = std::min (3, (int) v); const float t = v - (float) i;
    if (t < 0.02f) return n[i];
    if (t > 0.98f) return n[i + 1];
    return juce::String (n[i]) + ">" + n[i + 1] + " " + juce::String (juce::roundToInt (t * 100)) + "%";
}
juce::String vowelText (float v)
{
    static const char* n[5] = { "a", "e", "i", "o", "u" };
    const int i = std::min (3, (int) v); const float t = v - (float) i;
    if (t < 0.05f) return n[i];
    if (t > 0.95f) return n[i + 1];
    return juce::String (n[i]) + "-" + n[i + 1];
}
auto stText = [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.01f ? 0 : 1) + " st"; };
auto octText = [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " oct"; };
auto msText = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; };
auto bip = [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; };

void bassOsc (L& l, const juce::String& x, float shape, float level, int uni)
{
    const juce::String n = x.substring (0, 1);   // "a" / "b": parameter names read "a wave", "b detune"
    l.f (x + "shape", n + " wave", NR (0.0f, 4.0f), shape, waveText);
    l.f (x + "pw", n + " width", NR (0.05f, 0.95f), 0.5f, pct);
    l.c (x + "oct", n + " octave", { "-3", "-2", "-1", "0", "+1", "+2", "+3" }, 3);
    l.f (x + "semi", n + " semi", NR (-12.0f, 12.0f, 1.0f), 0.0f, stText);
    l.f (x + "fine", n + " fine", NR (-100.0f, 100.0f), 0.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; });
    l.f (x + "level", n + " level", NR (0.0f, 1.0f), level, pct);
    l.c (x + "uni", n + " unison", { "1", "2", "3", "4", "5", "6", "7" }, uni - 1);
    l.f (x + "detune", n + " detune", NR (0.0f, 1.0f), 0.25f, pct);
    l.f (x + "spread", n + " spread", NR (0.0f, 1.0f), 0.6f, pct);
}

void bassParams (L& l)
{
    bassOsc (l, "a_", 2.0f, 1.0f, 1);
    bassOsc (l, "b_", 3.0f, 0.0f, 1);
    l.f ("fm", "fm b>a", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("sub", "sub", NR (0.0f, 1.0f), 0.8f, pct);
    l.c ("subwave", "sub wave", { "sine", "round" }, 0);
    l.c ("suboct", "sub octave", { "-2", "-1", "0" }, 1);
    l.f ("noise", "noise", NR (0.0f, 1.0f), 0.0f, pct);
    l.c ("ftype", "filter", { "lp24", "lp12", "bp", "hp", "notch" }, 0);
    l.f ("cutoff", "cutoff", NR (20.0f, 20000.0f, 0.0f, 0.25f), 260.0f, hz);
    l.f ("res", "resonance", NR (0.0f, 1.0f), 0.35f, plain2);
    l.f ("fdrive", "filter drive", NR (0.0f, 1.0f), 0.3f, plain2);
    l.f ("keytrack", "key track", NR (0.0f, 1.0f), 0.5f, pct);
    l.f ("vowel", "vowel", NR (0.0f, 4.0f), 0.0f, vowelText);
    l.f ("fmtq", "formant q", NR (0.0f, 1.0f), 0.45f, plain2);
    l.f ("fmtshift", "formant shift", NR (-12.0f, 12.0f), 0.0f, stText);
    l.f ("fmtmix", "formant mix", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("combtune", "comb tune", NR (-24.0f, 24.0f), 0.0f, stText);
    l.f ("combfb", "comb feedback", NR (-0.95f, 0.95f), 0.6f, bip);
    l.f ("combmix", "comb mix", NR (0.0f, 1.0f), 0.0f, pct);
    l.c ("dmode", "distortion", { "soft", "fold", "hard", "tube", "crush", "smooth" }, 0);
    l.f ("ddrive", "drive", NR (0.0f, 1.0f), 0.25f, plain2);
    l.f ("dtone", "dist tone", NR (0.0f, 1.0f), 0.8f, [] (float v) { return hz (expMap (v, 1200.0f, 20000.0f)); });
    l.f ("dmix", "dist mix", NR (0.0f, 1.0f), 1.0f, pct);
    l.c ("order", "order", { "filter > dist", "dist > filter" }, 0);
    l.c ("wdiv", "wobble rate", divNames(), 8);
    l.c ("wshape", "wobble shape", { "sine", "triangle", "ramp down", "ramp up", "square", "wow", "yoy", "random" }, 0);
    l.b ("wretrig", "wobble retrigger", true);
    l.f ("wphase", "wobble phase", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("wcut", "wob > cutoff", NR (-6.0f, 6.0f), 4.0f, octText);
    l.f ("wvowel", "wob > vowel", NR (-1.0f, 1.0f), 0.0f, bip);
    l.f ("wfm", "wob > fm", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("wdrive", "wob > drive", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("wpitch", "wob > pitch", NR (-24.0f, 24.0f), 0.0f, stText);
    l.f ("wamp", "wob > volume", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("wcomb", "wob > comb", NR (-1.0f, 1.0f), 0.0f, bip);
    l.f ("ma", "env attack", NR (0.0f, 2000.0f, 0.0f, 0.3f), 1.0f, msText);
    l.f ("md", "env decay", NR (5.0f, 4000.0f, 0.0f, 0.3f), 250.0f, msText);
    l.f ("ms", "env sustain", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("mr", "env release", NR (5.0f, 4000.0f, 0.0f, 0.3f), 200.0f, msText);
    l.f ("mcut", "env > cutoff", NR (-6.0f, 6.0f), 0.0f, octText);
    l.f ("mpitch", "env > pitch", NR (-24.0f, 24.0f), 0.0f, stText);
    l.f ("mfm", "env > fm", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("mvowel", "env > vowel", NR (-1.0f, 1.0f), 0.0f, bip);
    l.f ("aa", "amp attack", NR (0.0f, 2000.0f, 0.0f, 0.3f), 1.0f, msText);
    l.f ("ad", "amp decay", NR (5.0f, 4000.0f, 0.0f, 0.3f), 300.0f, msText);
    l.f ("as", "amp sustain", NR (0.0f, 1.0f), 1.0f, pct);
    l.f ("ar", "amp release", NR (5.0f, 4000.0f, 0.0f, 0.3f), 80.0f, msText);
    l.f ("ott", "ott", NR (0.0f, 1.0f), 0.25f, pct);
    l.f ("otttime", "ott time", NR (0.0f, 1.0f), 0.4f, plain2);
    l.c ("voices", "voices", { "mono", "poly" }, 0);
    l.b ("legato", "legato", true);
    l.f ("glide", "glide", NR (0.0f, 1000.0f, 0.0f, 0.4f), 40.0f, msText);
    l.f ("bend", "bend range", NR (0.0f, 24.0f, 1.0f), 2.0f, stText);
    l.c ("wheel", "mod wheel", { "off", "wobble depth", "wobble rate" }, 0);
    l.f ("vel", "velocity", NR (0.0f, 1.0f), 0.3f, pct);
    l.f ("width", "width", NR (0.0f, 1.0f), 0.6f, pct);
    l.f ("level", "level", NR (-24.0f, 6.0f), -6.0f, dbText);
}

void seqGlobals (L& l, bool kit)
{
    l.b ("run", "run (internal clock)", false);
    l.f ("bpm", "internal bpm", NR (60.0f, 180.0f, 0.01f), 124.0f, [] (float v) { return juce::String (v, 2) + " bpm"; });
    l.f ("swing", "swing", NR (0.5f, 0.75f), 0.54f, pct);
    l.f ("micro", "micro range", NR (0.0f, 40.0f), 12.0f, [] (float v) { return fal::ui_pm() + juce::String (v, 1) + " ms"; });
    juce::StringArray pats; for (int i = 0; i < kPatterns; ++i) pats.add ("a" + juce::String (i + 1));
    l.c ("pattern", "pattern", pats, 0);
    l.f ("master", "master", NR (-24.0f, 6.0f), kit ? -4.0f : 0.0f, dbText);
    juce::ignoreUnused (kit);
}

void trackCtl (L& l, const juce::String& pre, const juce::String& namePre, int len = 16)
{
    l.f (pre + "len", namePre + "length", NR (1.0f, 16.0f, 1.0f), (float) len, [] (float v) { return juce::String (juce::roundToInt (v)); });
    l.f (pre + "prob", namePre + "probability", NR (0.0f, 1.0f), 1.0f, pct);
    l.f (pre + "shift", namePre + "shift", NR (-30.0f, 30.0f), 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " ms"; });
    l.b (pre + "mute", namePre + "mute", false);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (Pid p)
{
    juce::AudioProcessorValueTreeState::ParameterLayout lay;
    L l { lay };
    const auto& pi = info (p);
    switch (pi.kind)
    {
        case Kind::Module:
        {
            const auto t = pi.vtype;
            voiceParams (l, t, "", "", defaultVoice (t));
            l.b ("keytrack", "key tracking", t == VType::Perc || t == VType::Tom || t == VType::Click);
            l.b ("choke", "choke (mono)", t == VType::Kick || t == VType::Hat || t == VType::Snare);
            if (t == VType::Hat)
                l.f ("open", "open decay", NR (0.0f, 1.0f), 0.62f, [] (float v) { return slotText (VType::Hat, 1, v); });
            break;
        }
        case Kind::Machine:
        {
            if (p == Pid::kt1)
            {
                seqGlobals (l, true);
                for (int v = 0; v < kKitVoices; ++v)
                {
                    const auto& kv = kitVoice (v);
                    const auto pre = kitPrefix (v);
                    const juce::String np = juce::String (kv.id) + " ";
                    voiceParams (l, kv.type, pre, np, defaultVoice (kv.type));
                    trackCtl (l, pre, np);
                }
            }
            else
            {
                seqGlobals (l, false);
                l.c ("slices", "slices", { "4", "8", "16", "32" }, 2);
                l.c ("slicemode", "slice mode", { "grid", "onsets" }, 0);
                l.f ("start", "start", NR (0.0f, 1.0f), 0.0f, pct);
                l.f ("pitch", "pitch", NR (-24.0f, 24.0f, 0.01f), 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; });
                l.f ("gate", "gate", NR (0.02f, 1.0f), 1.0f, pct);
                l.f ("attack", "attack", NR (0.0f, 30.0f), 0.5f, [] (float v) { return juce::String (v, 1) + " ms"; });
                l.f ("release", "release", NR (0.5f, 200.0f, 0.0f, 0.4f), 8.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
                l.f ("tone", "tone", NR (0.0f, 1.0f), 1.0f, [] (float v) { return hz (expMap (v, 300.0f, 20000.0f)); });
                l.f ("revprob", "reverse chance", NR (0.0f, 1.0f), 0.0f, pct);
                l.f ("spread", "spread", NR (0.0f, 1.0f), 0.2f, pct);
                l.f ("level", "level", NR (0.0f, 1.0f), 0.85f, levelText);
                l.f ("vel", "velocity", NR (0.0f, 1.0f), 0.5f, pct);
                l.b ("fit", "fit loop to tempo", true);
                l.f ("loopbeats", "loop length", NR (1.0f, 16.0f, 1.0f), 4.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " beats"; });
                trackCtl (l, "", "");
            }
            break;
        }
        case Kind::Synth:
            bassParams (l);
            break;
        case Kind::Effect:
        {
            switch (p)
            {
                case Pid::dl4:
                    l.b ("sync", "sync", true);
                    l.c ("division", "division", divNames(), 6);
                    l.f ("time", "time", NR (2.0f, 2000.0f, 0.0f, 0.4f), 375.0f, [] (float v) { return num (v, v < 100 ? 1 : 0) + " ms"; });
                    l.f ("feedback", "feedback", NR (0.0f, 1.1f), 0.55f, pct);
                    l.f ("age", "age", NR (0.0f, 1.0f), 0.35f, plain2);
                    l.f ("wow", "wow", NR (0.0f, 1.0f), 0.2f, plain2);
                    l.f ("hp", "loop hp", NR (20.0f, 2000.0f, 0.0f, 0.3f), 200.0f, hz);
                    l.f ("lp", "loop lp", NR (500.0f, 20000.0f, 0.0f, 0.3f), 3500.0f, hz);
                    l.f ("spread", "spread", NR (0.0f, 1.0f), 0.5f, pct);
                    l.c ("mode", "mode", { "tape", "bbd", "clean" }, 0);
                    l.b ("freeze", "freeze", false);
                    l.f ("input", "send", NR (0.0f, 1.0f), 1.0f, pct);
                    l.f ("mix", "mix", NR (0.0f, 1.0f), 0.35f, pct);
                    break;
                case Pid::rv5:
                    l.c ("model", "model", { "room", "plate", "air", "chamber" }, 0);
                    l.f ("size", "size", NR (0.0f, 1.0f), 0.35f, pct);
                    l.f ("decay", "decay", NR (0.1f, 8.0f, 0.0f, 0.4f), 0.7f, [] (float v) { return juce::String (v, 2) + " s"; });
                    l.f ("predelay", "predelay", NR (0.0f, 120.0f), 6.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
                    l.f ("damp", "damp", NR (0.0f, 1.0f), 0.5f, plain2);
                    l.f ("lowcut", "low cut", NR (20.0f, 1000.0f, 0.0f, 0.4f), 160.0f, hz);
                    l.f ("diffusion", "diffusion", NR (0.0f, 1.0f), 0.7f, plain2);
                    l.f ("mod", "modulation", NR (0.0f, 1.0f), 0.25f, plain2);
                    l.f ("gate", "gate", NR (0.0f, 1.0f), 0.0f, [] (float v) { return v < 0.001f ? juce::String ("off") : num (-60.0f + 48.0f * v, 0) + " db"; });
                    l.f ("width", "width", NR (0.0f, 1.5f), 1.0f, pct);
                    l.f ("mix", "mix", NR (0.0f, 1.0f), 0.22f, pct);
                    break;
                case Pid::fl7:
                    l.f ("low", "low", NR (-40.0f, 6.0f), 0.0f, dbText);
                    l.f ("mid", "mid", NR (-40.0f, 6.0f), 0.0f, dbText);
                    l.f ("high", "high", NR (-40.0f, 6.0f), 0.0f, dbText);
                    l.f ("xlow", "low / mid", NR (40.0f, 1200.0f, 0.0f, 0.4f), 250.0f, hz);
                    l.f ("xhigh", "mid / high", NR (600.0f, 14000.0f, 0.0f, 0.4f), 2500.0f, hz);
                    l.f ("sweep", "filter", NR (-1.0f, 1.0f), 0.0f, [] (float v)
                         { return std::abs (v) < 0.015f ? juce::String ("off") : (v < 0 ? "lp " + hz (expMap (1.0f + v, 45.0f, 20000.0f)) : "hp " + hz (expMap (v, 18.0f, 12000.0f))); });
                    l.f ("res", "resonance", NR (0.0f, 1.0f), 0.3f, plain2);
                    l.f ("drive", "drive", NR (0.0f, 1.0f), 0.1f, plain2);
                    l.c ("lfodiv", "lfo rate", divNames(), 13);
                    l.f ("lfodepth", "lfo depth", NR (0.0f, 1.0f), 0.0f, pct);
                    l.c ("lfoshape", "lfo shape", { "sine", "triangle", "square", "s&h" }, 0);
                    l.f ("mix", "mix", NR (0.0f, 1.0f), 1.0f, pct);
                    break;
                case Pid::gl8:
                    l.f ("threshold", "threshold", NR (-40.0f, 0.0f), -14.0f, [] (float v) { return juce::String (v, 1) + " db"; });
                    l.c ("ratio", "ratio", { "1.5", "2", "4", "10" }, 1);
                    l.f ("attack", "attack", NR (0.1f, 30.0f, 0.0f, 0.4f), 10.0f, [] (float v) { return juce::String (v, v < 1 ? 2 : 1) + " ms"; });
                    l.f ("release", "release", NR (50.0f, 1200.0f, 0.0f, 0.5f), 200.0f, [] (float v) { return num (v, 0) + " ms"; });
                    l.b ("auto", "auto release", true);
                    l.f ("schp", "sidechain hp", NR (20.0f, 300.0f, 0.0f, 0.5f), 90.0f, hz);
                    l.f ("makeup", "makeup", NR (-6.0f, 18.0f), 0.0f, dbText);
                    l.f ("tape", "tape", NR (0.0f, 1.0f), 0.25f, plain2);
                    l.f ("bump", "head bump", NR (0.0f, 1.0f), 0.5f, plain2);
                    l.f ("hf", "hf loss", NR (0.0f, 1.0f), 0.3f, plain2);
                    l.f ("bias", "bias", NR (0.0f, 1.0f), 0.2f, plain2);
                    l.f ("mix", "mix", NR (0.0f, 1.0f), 1.0f, pct);
                    l.f ("out", "output", NR (-18.0f, 6.0f), 0.0f, dbText);
                    break;
                case Pid::mc3:
                    l.c ("mode", "mode", { "cut", "grain", "click" }, 1);
                    l.c ("division", "rate", divNames(), 4);
                    l.f ("density", "density", NR (0.0f, 1.0f), 0.45f, pct);
                    l.f ("size", "size", NR (2.0f, 250.0f, 0.0f, 0.5f), 60.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
                    l.f ("pitch", "pitch", NR (-24.0f, 24.0f, 0.01f), 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; });
                    l.f ("scatter", "scatter", NR (0.0f, 1.0f), 0.3f, pct);
                    l.f ("reverse", "reverse", NR (0.0f, 1.0f), 0.0f, pct);
                    l.f ("shape", "shape", NR (0.0f, 1.0f), 0.5f, plain2);
                    l.b ("hold", "hold", false);
                    l.f ("mix", "mix", NR (0.0f, 1.0f), 0.5f, pct);
                    break;
                case Pid::sw6:
                {
                    juce::StringArray tn; for (int i = 0; i < 6; ++i) tn.add (Swing::templName (i));
                    l.c ("template", "template", tn, 2);
                    l.f ("swing", "swing", NR (0.5f, 0.75f), 0.56f, pct);
                    l.f ("amount", "amount", NR (0.0f, 2.0f), 1.0f, pct);
                    l.f ("humanize", "humanize", NR (0.0f, 15.0f), 0.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
                    l.f ("accent", "accent", NR (0.0f, 1.0f), 0.0f, pct);
                    l.f ("xfade", "crossfade", NR (1.0f, 12.0f), 4.0f, [] (float v) { return juce::String (v, 1) + " ms"; });
                    for (int s = 0; s < 16; ++s)
                        l.f ("step" + juce::String (s + 1), "step " + juce::String (s + 1), NR (-0.5f, 0.5f), 0.0f,
                             [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; });
                    break;
                }
                default: break;
            }
            break;
        }
    }
    return lay;
}

// -------------------------------------------------------------------------------------------------
//  factory presets
// -------------------------------------------------------------------------------------------------
namespace
{
using V = std::vector<std::pair<juce::String, float>>;
V voiceV (VType t, const juce::String& pre, int model, std::array<float, 8> s, float level = -1, float pan = -9)
{
    const auto& vi = vtypeInfo (t);
    V v { { pre + "model", (float) model } };
    for (int i = 0; i < 8; ++i) v.push_back ({ pre + vi.slot[(size_t) i], s[(size_t) i] });
    if (level >= 0) v.push_back ({ pre + "level", level });
    if (pan > -9) v.push_back ({ pre + "pan", pan });
    return v;
}
V cat (std::initializer_list<V> parts) { V r; for (auto& p : parts) r.insert (r.end(), p.begin(), p.end()); return r; }
}

const std::vector<Preset>& presets (Pid p)
{
    static std::vector<Preset> table[kNumProducts];
    static bool built = false;
    if (! built)
    {
        auto& k = table[(int) Pid::bd1];
        k = { { "tok", voiceV (VType::Kick, "", 0, { 0.40f, 0.42f, 0.35f, 0.25f, 0.45f, 0.42f, 0.30f, 0.15f }) },
              { "perlon thud", voiceV (VType::Kick, "", 1, { 0.30f, 0.62f, 0.20f, 0.55f, 0.10f, 0.35f, 0.45f, 0.25f }) },
              { "909 short", voiceV (VType::Kick, "", 2, { 0.45f, 0.45f, 0.65f, 0.15f, 0.05f, 0.55f, 0.25f, 0.35f }) },
              { "sub sine", voiceV (VType::Kick, "", 3, { 0.22f, 0.75f, 0.05f, 0.40f, 0.00f, 0.25f, 0.55f, 0.00f }) },
              { "knock", voiceV (VType::Kick, "", 0, { 0.62f, 0.25f, 0.55f, 0.00f, 0.80f, 0.30f, 0.15f, 0.40f }) } };
        table[(int) Pid::sd2] = {
              { "tuned", voiceV (VType::Snare, "", 0, { 0.45f, 0.40f, 0.45f, 0.55f, 0.55f, 0.20f, 0.55f, 0.10f }) },
              { "fm crack", voiceV (VType::Snare, "", 1, { 0.55f, 0.30f, 0.70f, 0.60f, 0.40f, 0.35f, 0.70f, 0.20f }) },
              { "brush", voiceV (VType::Snare, "", 2, { 0.40f, 0.55f, 0.10f, 0.20f, 0.80f, 0.00f, 0.35f, 0.00f }) },
              { "808", voiceV (VType::Snare, "", 3, { 0.50f, 0.35f, 0.40f, 0.65f, 0.60f, 0.15f, 0.60f, 0.05f }) } };
        table[(int) Pid::rm3] = {
              { "tok", voiceV (VType::Rim, "", 2, { 0.46f, 0.22f, 0.68f, 0.41f, 0.12f, 0.30f, 0.55f, 0.00f }) },
              { "rimshot", voiceV (VType::Rim, "", 0, { 0.55f, 0.18f, 0.80f, 0.50f, 0.25f, 0.10f, 0.80f, 0.15f }) },
              { "woodblock", voiceV (VType::Rim, "", 1, { 0.60f, 0.30f, 0.40f, 0.70f, 0.00f, 0.00f, 0.50f, 0.00f }) },
              { "metal tick", voiceV (VType::Rim, "", 3, { 0.75f, 0.35f, 0.50f, 0.30f, 0.10f, 0.00f, 0.85f, 0.10f }) } };
        table[(int) Pid::hh4] = {
              { "808 hats", voiceV (VType::Hat, "", 0, { 0.50f, 0.32f, 0.30f, 0.60f, 0.40f, 0.10f, 0.55f, 0.00f }) },
              { "noise hats", voiceV (VType::Hat, "", 1, { 0.50f, 0.28f, 0.20f, 0.10f, 0.80f, 0.00f, 0.65f, 0.00f }) },
              { "ring", voiceV (VType::Hat, "", 2, { 0.40f, 0.30f, 0.25f, 0.80f, 0.20f, 0.20f, 0.50f, 0.05f }) },
              { "tick hats", voiceV (VType::Hat, "", 3, { 0.65f, 0.08f, 0.70f, 0.50f, 0.50f, 0.00f, 0.80f, 0.00f }) } };
        table[(int) Pid::cp6] = {
              { "clap", voiceV (VType::Clap, "", 0, { 0.40f, 0.30f, 0.50f, 0.50f, 0.30f, 0.45f, 0.30f, 0.00f }) },
              { "snap", voiceV (VType::Clap, "", 1, { 0.55f, 0.15f, 0.70f, 0.00f, 0.20f, 0.20f, 0.50f, 0.00f }) },
              { "room clap", voiceV (VType::Clap, "", 2, { 0.35f, 0.65f, 0.40f, 0.75f, 0.40f, 0.60f, 0.25f, 0.05f }) },
              { "tight", voiceV (VType::Clap, "", 3, { 0.50f, 0.15f, 0.60f, 0.00f, 0.20f, 0.30f, 0.45f, 0.10f }) } };
        table[(int) Pid::pc7] = {
              { "conga", voiceV (VType::Perc, "", 0, { 0.35f, 0.30f, 0.55f, 0.60f, 0.10f, 0.20f, 0.45f, 0.00f }) },
              { "bongo", voiceV (VType::Perc, "", 1, { 0.50f, 0.22f, 0.65f, 0.55f, 0.05f, 0.25f, 0.60f, 0.00f }) },
              { "bell", voiceV (VType::Perc, "", 2, { 0.70f, 0.55f, 0.30f, 0.40f, 0.00f, 0.00f, 0.75f, 0.00f }) },
              { "glass", voiceV (VType::Perc, "", 3, { 0.80f, 0.45f, 0.40f, 0.30f, 0.05f, 0.00f, 0.85f, 0.00f }) } };
        table[(int) Pid::cl8] = {
              { "tick", voiceV (VType::Click, "", 0, { 0.70f, 0.30f, 0.60f, 0.40f, 0.20f, 0.00f, 0.30f, 0.00f }) },
              { "pop", voiceV (VType::Click, "", 1, { 0.40f, 0.30f, 0.30f, 0.20f, 0.00f, 0.00f, 0.20f, 0.00f }) },
              { "dust", voiceV (VType::Click, "", 2, { 0.65f, 0.70f, 0.20f, 0.30f, 0.50f, 0.00f, 0.40f, 0.00f }) },
              { "blip", voiceV (VType::Click, "", 3, { 0.45f, 0.45f, 0.20f, 0.20f, 0.00f, 0.60f, 0.10f, 0.00f }) } };
        table[(int) Pid::tm9] = {
              { "tom", voiceV (VType::Tom, "", 0, { 0.35f, 0.45f, 0.40f, 0.60f, 0.20f, 0.30f, 0.45f, 0.00f }) },
              { "blip", voiceV (VType::Tom, "", 1, { 0.55f, 0.25f, 0.20f, 0.50f, 0.00f, 0.55f, 0.35f, 0.00f }) },
              { "fm tom", voiceV (VType::Tom, "", 2, { 0.40f, 0.40f, 0.30f, 0.55f, 0.10f, 0.35f, 0.40f, 0.10f }) },
              { "low", voiceV (VType::Tom, "", 3, { 0.15f, 0.65f, 0.20f, 0.80f, 0.05f, 0.25f, 0.50f, 0.15f }) } };

        // kit kits: voice tables + patterns. Pattern lines: id + 16 chars (x on, X accent, o soft, r ratchet, ? 50%)
        auto kit = [] (const char* name, std::array<int, 8> models, std::array<std::array<float, 8>, 8> slots, const char* pattern, float swing)
        {
            Preset pr; pr.name = name; pr.pattern = pattern;
            for (int v = 0; v < kKitVoices; ++v)
            {
                auto vv = voiceV (kitVoice (v).type, kitPrefix (v), models[(size_t) v], slots[(size_t) v]);
                pr.values.insert (pr.values.end(), vv.begin(), vv.end());
            }
            pr.values.push_back ({ "swing", swing });
            return pr;
        };
        const std::array<std::array<float, 8>, 8> base = { {
            { 0.40f, 0.42f, 0.35f, 0.25f, 0.45f, 0.42f, 0.30f, 0.15f }, { 0.45f, 0.40f, 0.45f, 0.55f, 0.55f, 0.20f, 0.55f, 0.10f },
            { 0.46f, 0.22f, 0.68f, 0.41f, 0.12f, 0.30f, 0.55f, 0.00f }, { 0.50f, 0.18f, 0.30f, 0.60f, 0.40f, 0.10f, 0.55f, 0.00f },
            { 0.40f, 0.55f, 0.50f, 0.55f, 0.45f, 0.30f, 0.45f, 0.10f }, { 0.40f, 0.30f, 0.50f, 0.50f, 0.30f, 0.45f, 0.30f, 0.00f },
            { 0.35f, 0.30f, 0.55f, 0.60f, 0.10f, 0.20f, 0.45f, 0.00f }, { 0.70f, 0.30f, 0.60f, 0.40f, 0.20f, 0.00f, 0.30f, 0.00f } } };
        table[(int) Pid::kt1] = {
            kit ("a3 tok", { 0, 0, 2, 0, 1, 0, 0, 0 }, base,
                 "bd1 X...x...X...x...\nsd2 ....?.......o..x\nrm3 ..x..x.x..x...x.\nhh4 x.xxo.xxx.xxo.xx\noh5 ..x...x...x...x.\ncp6 ....x.........x.\npc7 .x..o..x.x...o..\ncl8 x.o.x.o.xro.x.o.", 0.56f),
            kit ("perlon shuffle", { 1, 2, 0, 0, 1, 1, 0, 2 }, base,
                 "bd1 X.....x.X.......\nsd2 ................\nrm3 ...x..x....x..x.\nhh4 ..x...x...x...x.\noh5 ................\ncp6 ....x.......x...\npc7 x..o..x...o..x..\ncl8 .?.?.?.?.?.?.?.?", 0.62f),
            kit ("bucharest", { 0, 3, 2, 1, 1, 3, 1, 0 }, base,
                 "bd1 X...X...X...X...\nsd2 ................\nrm3 .x...x.x.o...x..\nhh4 xoxoxoxoxoxoxoxo\noh5 ..o...o...o...o.\ncp6 ....o.......o...\npc7 ..x.....x..x....\ncl8 r.......r.......", 0.53f),
            kit ("empty", { 0, 0, 2, 0, 1, 0, 0, 0 }, base, "", 0.54f) };
        // kt2: a few slice patterns ("sl" lines: 16 hex slice indices, '.' = off)
        table[(int) Pid::kt2] = {
            { "straight", {}, "sl 0123456789abcdef" },
            { "cut-up", { { "revprob", 0.15f } }, "sl 0.2.4401.a8.c4e" },
            { "stutter", { { "gate", 0.45f } }, "sl 00.4448.0cc.4ee" },
            { "sparse", { { "gate", 0.3f } }, "sl 0...3..6..9...c" } };
        table[(int) Pid::dl4] = {
            { "dub 3/16", { { "division", 6 }, { "feedback", 0.62f }, { "age", 0.4f }, { "hp", 250 }, { "lp", 2800 }, { "mix", 0.35f } } },
            { "ping 1/8 d", { { "division", 9 }, { "feedback", 0.5f }, { "spread", 1.0f }, { "lp", 4500 }, { "mix", 0.3f } } },
            { "bbd slap", { { "sync", 0 }, { "time", 95 }, { "feedback", 0.25f }, { "mode", 1 }, { "mix", 0.3f } } },
            { "runaway", { { "division", 6 }, { "feedback", 1.02f }, { "age", 0.7f }, { "wow", 0.5f }, { "hp", 400 }, { "lp", 2000 }, { "mix", 0.4f } } } };
        table[(int) Pid::rv5] = {
            { "small room", { { "model", 0 }, { "size", 0.3f }, { "decay", 0.6f }, { "mix", 0.2f } } },
            { "plate", { { "model", 1 }, { "size", 0.55f }, { "decay", 1.8f }, { "damp", 0.35f }, { "mix", 0.22f } } },
            { "gated air", { { "model", 2 }, { "size", 0.6f }, { "decay", 1.5f }, { "gate", 0.5f }, { "mix", 0.3f } } },
            { "chamber", { { "model", 3 }, { "size", 0.7f }, { "decay", 2.6f }, { "predelay", 20 }, { "mix", 0.25f } } } };
        table[(int) Pid::fl7] = {
            { "flat", {} },
            { "low kill", { { "low", -40 } } },
            { "lp sweep 2 bars", { { "sweep", -0.35f }, { "res", 0.55f }, { "lfodiv", 14 }, { "lfodepth", 0.3f } } },
            { "hp tension", { { "sweep", 0.45f }, { "res", 0.7f }, { "drive", 0.3f } } } };
        table[(int) Pid::gl8] = {
            { "glue 2:1", { { "threshold", -14 }, { "ratio", 1 }, { "attack", 10 }, { "tape", 0.25f } } },
            { "drum crush", { { "threshold", -28 }, { "ratio", 3 }, { "attack", 0.3f }, { "release", 80 }, { "auto", 0 }, { "makeup", 9 }, { "mix", 0.45f } } },
            { "tape only", { { "threshold", 0 }, { "tape", 0.6f }, { "bump", 0.7f }, { "hf", 0.5f } } } };
        table[(int) Pid::mc3] = {
            { "grains", { { "mode", 1 }, { "division", 4 }, { "density", 0.45f }, { "size", 60 } } },
            { "stutter 1/32", { { "mode", 0 }, { "division", 1 }, { "density", 0.35f }, { "size", 80 }, { "mix", 0.8f } } },
            { "clicks", { { "mode", 2 }, { "division", 4 }, { "density", 0.7f }, { "size", 40 }, { "scatter", 0.8f }, { "mix", 0.4f } } },
            { "reverse dust", { { "mode", 1 }, { "division", 3 }, { "reverse", 0.7f }, { "pitch", 12 }, { "size", 25 }, { "mix", 0.35f } } } };
        table[(int) Pid::sw6] = {
            { "mpc 56", { { "template", 1 }, { "swing", 0.56f } } },
            { "shuffle", { { "template", 2 }, { "swing", 0.58f }, { "accent", 0.5f } } },
            { "push", { { "template", 3 }, { "swing", 0.54f } } },
            { "drag", { { "template", 4 }, { "swing", 0.6f } } },
            { "drunk", { { "template", 5 }, { "humanize", 4 } } } };
        // wb1: dubstep / neuro basses
        auto B = [] (const char* name, V v) { return Preset { name, v, {} }; };
        table[(int) Pid::wb1] = {
            B ("dmz sub wob", { { "a_shape", 2 }, { "b_shape", 3 }, { "b_oct", 2 }, { "b_level", 0.4f }, { "sub", 0.9f }, { "cutoff", 220 }, { "res", 0.35f },
                                { "fdrive", 0.35f }, { "wdiv", 8 }, { "wshape", 0 }, { "wcut", 4.5f }, { "ddrive", 0.25f }, { "ott", 0.2f } }),
            B ("hench square", { { "a_shape", 3 }, { "a_pw", 0.42f }, { "sub", 0.85f }, { "cutoff", 160 }, { "res", 0.45f }, { "wdiv", 4 }, { "wshape", 4 },
                                 { "wcut", 5.0f }, { "dmode", 3 }, { "ddrive", 0.45f }, { "ott", 0.35f } }),
            B ("skream wow", { { "a_shape", 2 }, { "b_shape", 2 }, { "b_semi", 7 }, { "b_level", 0.5f }, { "cutoff", 520 }, { "fmtmix", 0.8f }, { "vowel", 0 },
                               { "wvowel", 0.85f }, { "wcut", 2.5f }, { "wshape", 5 }, { "wdiv", 11 }, { "ddrive", 0.35f }, { "ott", 0.3f } }),
            B ("rusko yoy", { { "a_shape", 2.6f }, { "a_uni", 2 }, { "a_detune", 0.15f }, { "cutoff", 700 }, { "fmtmix", 1.0f }, { "fmtq", 0.6f }, { "wshape", 6 },
                              { "wvowel", 1.0f }, { "wcut", 2.0f }, { "wdiv", 7 }, { "mpitch", -5 }, { "md", 180 }, { "dmode", 1 }, { "ddrive", 0.3f }, { "ott", 0.4f } }),
            B ("loefah sub", { { "a_shape", 0 }, { "a_level", 0.35f }, { "sub", 1.0f }, { "suboct", 1 }, { "cutoff", 380 }, { "wcut", 0 }, { "ddrive", 0.12f },
                               { "aa", 6 }, { "ar", 220 }, { "ott", 0 }, { "glide", 90 } }),
            B ("noisa reese", { { "a_shape", 2 }, { "a_uni", 6 }, { "a_detune", 0.45f }, { "a_spread", 0.85f }, { "b_shape", 2 }, { "b_uni", 4 }, { "b_oct", 2 },
                                { "b_detune", 0.3f }, { "b_level", 0.6f }, { "cutoff", 1400 }, { "res", 0.2f }, { "wcut", 0 }, { "dmode", 5 }, { "ddrive", 0.5f },
                                { "combmix", 0.25f }, { "combfb", 0.5f }, { "ott", 0.6f }, { "glide", 80 }, { "sub", 0.6f } }),
            B ("current value fm", { { "a_shape", 0 }, { "b_shape", 0 }, { "b_oct", 4 }, { "b_level", 0 }, { "fm", 0.42f }, { "wfm", 0.3f }, { "wdiv", 4 }, { "wshape", 1 },
                                     { "wcut", 1.5f }, { "cutoff", 1800 }, { "res", 0.25f }, { "dmode", 1 }, { "ddrive", 0.3f }, { "dtone", 0.6f }, { "fmtmix", 0.3f }, { "ott", 0.6f } }),
            B ("billain growl", { { "a_shape", 2 }, { "b_shape", 3 }, { "b_fine", 7 }, { "b_level", 0.3f }, { "fm", 0.35f }, { "fmtmix", 0.7f }, { "vowel", 1 },
                                  { "wvowel", 0.7f }, { "wdiv", 9 }, { "wshape", 1 }, { "wcut", 1.5f }, { "cutoff", 900 }, { "dmode", 2 }, { "ddrive", 0.4f },
                                  { "combmix", 0.3f }, { "wcomb", 0.4f }, { "ott", 0.8f } }),
            B ("talk box", { { "a_shape", 2 }, { "a_uni", 3 }, { "cutoff", 2400 }, { "wcut", 0 }, { "fmtmix", 1.0f }, { "fmtq", 0.8f }, { "mvowel", 0.8f }, { "md", 400 },
                             { "dmode", 3 }, { "ddrive", 0.3f }, { "ott", 0.4f } }),
            B ("neuro stab", { { "voices", 1 }, { "a_shape", 4 }, { "a_uni", 5 }, { "a_detune", 0.35f }, { "cutoff", 200 }, { "wcut", 0 }, { "mcut", 4.0f }, { "md", 160 },
                               { "dmode", 1 }, { "ddrive", 0.6f }, { "ott", 0.8f }, { "ad", 300 }, { "as", 0 }, { "ar", 120 }, { "sub", 0.5f } }),
        };
        built = true;
    }
    return table[(int) p];
}
} // namespace fal

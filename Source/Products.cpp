#include "Products.h"
#include "dsp/Sequencer.h"
#include "dsp/Bass.h"

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
   #if FALTUNG_DEEP
    // faltung deep: the deep house suite. Mirror look: a moving mirrored tree in one duotone per plug-in (dark, light)
    static const ProductInfo d[kNumProducts] = {
        { Pid::kt1, "kt1", "kit",    "deep house drum machine, eight voices + sequencer", Kind::Machine, Style::Mirror, 0xff1d3772, 0xffd8e3f7, VType::Kick,  1000, 640 },
        { Pid::kt2, "kt2", "loop",   "loop slicer for breaks and chopped grooves",   Kind::Machine, Style::Mirror, 0xff33441c, 0xffe3ecc9, VType::Kick, 1000, 640 },
        { Pid::bd1, "bd1", "kick",   "round, warm deep house kicks",                 Kind::Module,  Style::Mirror, 0xff5a2414, 0xfff3d6c8, VType::Kick,  760, 480 },
        { Pid::sd2, "sd2", "snare",  "soft snares and brushes",                      Kind::Module,  Style::Mirror, 0xff2e3566, 0xffdfe2f7, VType::Snare, 760, 480 },
        { Pid::rm3, "rm3", "rim",    "wooden rims and sticks",                       Kind::Module,  Style::Mirror, 0xff3b2c2a, 0xfff2dcd6, VType::Rim,   760, 480 },
        { Pid::hh4, "hh4", "hats",   "hats and shakers",                             Kind::Module,  Style::Mirror, 0xff1f3d52, 0xffd5e6ef, VType::Hat,   760, 480 },
        { Pid::cp6, "cp6", "clap",   "soft claps and snaps",                         Kind::Module,  Style::Mirror, 0xff5b3416, 0xfff1dcc4, VType::Clap,  760, 480 },
        { Pid::pc7, "pc7", "perc",   "congas, bongos, bells",                        Kind::Module,  Style::Mirror, 0xff2c4a34, 0xffdbead9, VType::Perc,  760, 480 },
        { Pid::cl8, "cl8", "dust",   "vinyl dust, ticks and pops",                   Kind::Module,  Style::Mirror, 0xff26355c, 0xffe1e8f6, VType::Click, 760, 480 },
        { Pid::tm9, "tm9", "tom",    "deep toms",                                    Kind::Module,  Style::Mirror, 0xff4a1e1a, 0xfff0d6cf, VType::Tom,   760, 480 },
        { Pid::mc3, "mc3", "micro",  "micro-sampling for chopped textures",          Kind::Effect,  Style::Mirror, 0xff2b2e5c, 0xffdfe0f5, VType::Kick,  760, 480 },
        { Pid::dl4, "dl4", "dub",    "tape echo for chords and percussion",          Kind::Effect,  Style::Mirror, 0xff183d4a, 0xffd3e9ec, VType::Kick,  760, 480 },
        { Pid::rv5, "rv5", "room",   "warm rooms and plates",                        Kind::Effect,  Style::Mirror, 0xff40331f, 0xffefe2cc, VType::Kick,  760, 480 },
        { Pid::sw6, "sw6", "swing",  "the swing of a drum machine, on any audio",    Kind::Effect,  Style::Mirror, 0xff3b2450, 0xffeadcf2, VType::Kick,  760, 480 },
        { Pid::fl7, "fl7", "filter", "dj isolator and warm sweep",                   Kind::Effect,  Style::Mirror, 0xff2f4316, 0xffe5efcf, VType::Kick,  760, 480 },
        { Pid::gl8, "gl8", "glue",   "bus compressor and tape",                      Kind::Effect,  Style::Mirror, 0xff2a323c, 0xffe1e6eb, VType::Kick,  760, 480 },
        { Pid::wb1, "sb1", "stabs",  "chord stabs, organs and sub bass",             Kind::Synth,   Style::Mirror, 0xff14123f, 0xfff0dfe6, VType::Kick, 1100, 760 },
        { Pid::ms0, "ms0", "master", "(not part of faltung deep)",                   Kind::Effect,  Style::Mirror, 0xff1a242c, 0xff5aaad8, VType::Kick, 1320, 880 },
    };
    return d[(int) p];
   #endif
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
        { Pid::ms0, "ms0", "master", "mastering bus: linear-phase eq, mid / side, imaging, limiter", Kind::Effect, Style::Stdio, 0xff1a242c, 0xff5aaad8, VType::Kick, 1320, 880 },
    };
    return t[(int) p];
}

const KitVoiceInfo& kitVoice (int v)
{
   #if FALTUNG_DEEP
    static const KitVoiceInfo d[kKitVoices] = {
        { "bd1", VType::Kick,  0xff5e8f6a, 36 }, { "rm2", VType::Rim,   0xff8dbb7a, 37 }, { "cp3", VType::Clap, 0xff8ec3e6, 38 }, { "ch4", VType::Hat, 0xff6e80c8, 39 },
        { "oh5", VType::Hat,   0xffee8e6e, 40 }, { "sh6", VType::Hat,   0xff97adc4, 41 }, { "cg7", VType::Perc, 0xfff6c590, 42 }, { "tm8", VType::Tom, 0xffcfe6f0, 43 } };
    return d[std::clamp (v, 0, kKitVoices - 1)];
   #endif
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

VoiceParams kitDefaultVoice (int v)
{
   #if FALTUNG_DEEP
    VoiceParams p = defaultVoice (kitVoice (v).type);
    auto set = [&] (int model, std::array<float, 8> sl, float level, float pan) { p.model = model; p.s = sl; p.level = level; p.pan = pan; p.vel = 0.5f; };
    switch (v)
    {
        case 0: set (1, { 0.30f, 0.55f, 0.22f, 0.45f, 0.10f, 0.32f, 0.42f, 0.12f }, 0.86f, 0.0f); break;     // round kick
        case 1: set (1, { 0.52f, 0.20f, 0.45f, 0.62f, 0.05f, 0.10f, 0.45f, 0.00f }, 0.72f, -0.18f); break;   // wooden rim
        case 2: set (2, { 0.35f, 0.45f, 0.35f, 0.70f, 0.35f, 0.55f, 0.22f, 0.05f }, 0.76f, 0.04f); break;    // soft room clap
        case 3: set (0, { 0.45f, 0.22f, 0.18f, 0.55f, 0.45f, 0.05f, 0.42f, 0.00f }, 0.64f, 0.12f); break;    // closed hat
        case 4: set (0, { 0.42f, 0.55f, 0.15f, 0.50f, 0.50f, 0.05f, 0.40f, 0.00f }, 0.66f, -0.08f); break;   // open hat
        case 5: set (1, { 0.62f, 0.20f, 0.05f, 0.00f, 0.95f, 0.00f, 0.40f, 0.00f }, 0.56f, 0.25f); break;    // shaker
        case 6: set (0, { 0.30f, 0.36f, 0.45f, 0.65f, 0.08f, 0.20f, 0.40f, 0.00f }, 0.74f, -0.28f); break;   // conga
        default: set (0, { 0.25f, 0.50f, 0.30f, 0.70f, 0.10f, 0.25f, 0.45f, 0.05f }, 0.74f, 0.20f); break;  // tom
    }
    return p;
   #else
    return defaultVoice (kitVoice (v).type);
   #endif
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
    { juce::StringArray ch; for (int i = 0; i < 15; ++i) ch.add (chordName (i)); l.c ("chord", "chord", ch, 0); }
    l.c ("voicing", "chord voicing", { "close", "open", "drop 2" }, 0);
    // ---- modulation --------------------------------------------------------------------------------
    const juce::StringArray lfoShapes { "sine", "triangle", "ramp down", "ramp up", "square", "wow", "yoy", "random", "smooth random" };
    for (int k = 2; k <= 3; ++k)
    {
        const juce::String x = "l" + juce::String (k), n = "lfo " + juce::String (k) + " ";
        l.c (x + "div", n + "rate", divNames(), k == 2 ? 16 : 11);
        l.b (x + "sync", n + "sync", true);
        l.f (x + "hz", n + "free rate", NR (0.01f, 40.0f, 0.0f, 0.3f), 1.0f, [] (float v) { return juce::String (v, v < 1 ? 2 : 1) + " hz"; });
        l.c (x + "shape", n + "shape", lfoShapes, k == 2 ? 3 : 0);
        l.b (x + "retrig", n + "retrigger", k == 2);
        l.f (x + "phase", n + "phase", NR (0.0f, 1.0f), 0.0f, pct);
        l.c (x + "mode", n + "mode", { "loop", "one-shot" }, 0);
        l.f (x + "skew", n + "skew", NR (0.0f, 1.0f), 0.5f, pct);
        l.f (x + "smooth", n + "smooth", NR (0.0f, 1.0f), 0.0f, pct);
    }
    const float st[16] = { 1, 0.2f, 0.6f, 0.1f, 0.9f, 0.3f, 0.5f, 0, 1, 0.4f, 0.7f, 0.2f, 0.8f, 0.1f, 0.5f, 0.3f };
    for (int k = 1; k <= 16; ++k) l.f ("stp" + juce::String (k), "step " + juce::String (k), NR (0.0f, 1.0f), st[k - 1], pct);
    l.f ("steplen", "steps length", NR (1.0f, 16.0f, 1.0f), 16.0f, [] (float v) { return juce::String (juce::roundToInt (v)); });
    l.c ("stepdiv", "steps rate", divNames(), 4);
    l.f ("stepglide", "steps glide", NR (0.0f, 1.0f), 0.15f, pct);
    l.b ("stepretrig", "steps retrigger", true);
    juce::StringArray srcs, dsts;
    for (int i = 0; i < MS_COUNT; ++i) srcs.add (modSrcName (i));
    for (int i = 0; i < MD_COUNT; ++i) dsts.add (modDstName (i));
    for (int k = 1; k <= 8; ++k)
    {
        const juce::String x = "m" + juce::String (k), n = "mod " + juce::String (k) + " ";
        l.c (x + "src", n + "source", srcs, 0);
        l.c (x + "dst", n + "destination", dsts, 0);
        l.f (x + "amt", n + "amount", NR (-1.0f, 1.0f), 0.0f, [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; });
    }
}

void masterParams (L& l)
{
    const juce::StringArray chs { "stereo", "mid", "side" };
    const juce::StringArray slopes { "12 db", "24 db", "48 db", "96 db" };
    auto on = [&] (const juce::String& id, const juce::String& n, bool d) { l.b (id, n, d); };
    auto db = [] (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " db"; };
    auto ms = [] (float v) { return v < 10 ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; };
    auto ratio = [] (float v) { return juce::String (v, 2) + ":1"; };
    l.f ("trim", "input trim", NR (-24.0f, 24.0f), 0.0f, db);
    l.c ("listen", "listen", { "stereo", "mono", "side", "swap" }, 0);
    on ("bypass", "compare (bypass)", false);
    on ("match", "level-matched compare", true);
    on ("dcon", "dc filter", true);
    l.f ("balance", "balance", NR (-1.0f, 1.0f), 0.0f, [] (float v) { const int p = juce::roundToInt (v * 100); return p == 0 ? juce::String ("c") : (p < 0 ? "l " + juce::String (-p) : "r " + juce::String (p)); });
    l.c ("target", "loudness target", { "off", "-8 lufs", "-9 lufs (club)", "-10 lufs", "-11 lufs", "-12 lufs", "-14 lufs (streaming)", "-16 lufs (apple)", "-18 lufs (dynamic)", "-23 lufs (broadcast)" }, 0);
    // eq
    on ("eqon", "eq on", true);
    const float f0[8] = { 40, 120, 300, 700, 1500, 3500, 8000, 14000 };
    for (int k = 1; k <= 8; ++k)
    {
        const juce::String x = "e" + juce::String (k), n = "eq " + juce::String (k) + " ";
        on (x + "on", n + "on", false);
        l.c (x + "type", n + "type", { "bell", "low shelf", "high shelf", "notch" }, k == 1 ? 1 : (k == 8 ? 2 : 0));
        l.f (x + "freq", n + "freq", NR (20.0f, 20000.0f, 0.0f, 0.25f), f0[k - 1], hz);
        l.f (x + "gain", n + "gain", NR (-18.0f, 18.0f), 0.0f, db);
        l.f (x + "q", n + "q", NR (0.1f, 18.0f, 0.0f, 0.35f), 0.7f, plain2);
        l.c (x + "ch", n + "channel", chs, 0);
    }
    on ("hpon", "high-pass on", false);
    l.f ("hpfreq", "high-pass freq", NR (10.0f, 500.0f, 0.0f, 0.35f), 22.0f, hz);
    l.c ("hpslope", "high-pass slope", slopes, 1);
    l.c ("hpch", "high-pass channel", chs, 0);
    on ("lpon", "low-pass on", false);
    l.f ("lpfreq", "low-pass freq", NR (2000.0f, 22000.0f, 0.0f, 0.5f), 20000.0f, hz);
    l.c ("lpslope", "low-pass slope", slopes, 1);
    l.c ("lpch", "low-pass channel", chs, 0);
    // dynamic eq
    on ("dynon", "dynamic eq on", true);
    for (int k = 1; k <= 2; ++k)
    {
        const juce::String x = "d" + juce::String (k), n = "dyn " + juce::String (k) + " ";
        on (x + "on", n + "on", false);
        l.c (x + "ch", n + "channel", chs, 1);
        l.f (x + "freq", n + "freq", NR (20.0f, 20000.0f, 0.0f, 0.25f), k == 1 ? 400.0f : 3000.0f, hz);
        l.f (x + "q", n + "q", NR (0.3f, 12.0f, 0.0f, 0.4f), 1.4f, plain2);
        l.f (x + "thr", n + "threshold", NR (-60.0f, 0.0f), -30.0f, db);
        l.f (x + "range", n + "range", NR (-18.0f, 12.0f), -4.0f, db);
        l.f (x + "att", n + "attack", NR (0.5f, 100.0f, 0.0f, 0.4f), 10.0f, ms);
        l.f (x + "rel", n + "release", NR (10.0f, 1000.0f, 0.0f, 0.4f), 120.0f, ms);
    }
    // multiband + imaging
    on ("mbon", "multiband on", true);
    l.f ("x1", "crossover 1", NR (30.0f, 300.0f, 0.0f, 0.5f), 80.0f, hz);
    l.f ("x2", "crossover 2", NR (150.0f, 2000.0f, 0.0f, 0.5f), 350.0f, hz);
    l.f ("x3", "crossover 3", NR (1000.0f, 12000.0f, 0.0f, 0.5f), 4000.0f, hz);
    const char* bn[4] = { "sub", "low-mid", "mid-high", "high" };
    const float wd[4] = { 0.0f, 0.85f, 1.0f, 1.1f };
    for (int k = 1; k <= 4; ++k)
    {
        const juce::String x = "b" + juce::String (k), n = juce::String (bn[k - 1]) + " ";
        l.f (x + "thr", n + "threshold", NR (-60.0f, 0.0f), -18.0f, db);
        l.f (x + "ratio", n + "ratio", NR (1.0f, 4.0f, 0.0f, 0.6f), 1.5f, ratio);
        l.f (x + "att", n + "attack", NR (1.0f, 200.0f, 0.0f, 0.4f), k == 1 ? 40.0f : 25.0f, ms);
        l.f (x + "rel", n + "release", NR (20.0f, 1000.0f, 0.0f, 0.4f), k == 1 ? 220.0f : 150.0f, ms);
        l.f (x + "gain", n + "gain", NR (-12.0f, 12.0f), 0.0f, db);
        l.f (x + "width", n + "width", NR (0.0f, 2.0f), wd[k - 1], pct);
        on (x + "solo", n + "solo", false);
    }
    on ("imgon", "imaging on", true);
    l.f ("air", "air (top-band width)", NR (0.0f, 1.0f), 0.0f, pct);
    l.f ("midlvl", "mid level", NR (-12.0f, 12.0f), 0.0f, db);
    l.f ("sidelvl", "side level", NR (-12.0f, 12.0f), 0.0f, db);
    // saturation
    on ("saton", "saturation on", false);
    l.c ("satcurve", "saturation curve", { "tape", "tube", "soft clip", "smooth" }, 0);
    l.c ("satfocus", "saturation focus", { "full", "lows", "highs", "ends (avoid 200-2k)" }, 0);
    l.f ("satdrive", "saturation drive", NR (0.0f, 18.0f), 3.0f, db);
    l.f ("satmix", "saturation mix", NR (0.0f, 1.0f), 1.0f, pct);
    // ambience
    on ("ambon", "ambience on", false);
    l.f ("amblevel", "ambience level", NR (-60.0f, -6.0f), -30.0f, db);
    l.f ("ambsize", "ambience size", NR (0.0f, 1.0f), 0.3f, pct);
    l.f ("ambdecay", "ambience decay", NR (0.1f, 3.0f, 0.0f, 0.5f), 0.6f, [] (float v) { return juce::String (v, 2) + " s"; });
    l.f ("ambhp", "ambience high-pass", NR (40.0f, 1000.0f, 0.0f, 0.4f), 300.0f, hz);
    l.f ("amblp", "ambience low-pass", NR (2000.0f, 20000.0f, 0.0f, 0.4f), 10000.0f, hz);
    l.f ("ambwidth", "ambience width", NR (0.0f, 1.5f), 1.0f, pct);
    // glue
    on ("glueon", "glue on", true);
    l.f ("gthr", "glue threshold", NR (-40.0f, 0.0f), -16.0f, db);
    l.f ("gratio", "glue ratio", NR (1.0f, 4.0f, 0.0f, 0.6f), 2.0f, ratio);
    l.f ("gatt", "glue attack", NR (0.1f, 50.0f, 0.0f, 0.4f), 20.0f, ms);
    l.f ("grel", "glue release", NR (50.0f, 1200.0f, 0.0f, 0.5f), 150.0f, ms);
    on ("gauto", "glue auto release", true);
    l.f ("gmakeup", "glue makeup", NR (-6.0f, 12.0f), 0.0f, db);
    l.f ("gmix", "glue mix", NR (0.0f, 1.0f), 1.0f, pct);
    // limiter + output
    on ("clipon", "clipper on", false);
    l.f ("clipdrive", "clipper drive", NR (0.0f, 12.0f), 1.0f, db);
    l.f ("clipsoft", "clipper softness", NR (0.0f, 1.0f), 0.5f, pct);
    on ("limon", "limiter on", true);
    l.c ("limos", "limiter oversampling", { "off", "2x", "4x", "8x" }, 2);
    l.f ("limlink", "limiter stereo link", NR (0.0f, 1.0f), 1.0f, pct);
    l.f ("limgain", "limiter gain", NR (0.0f, 18.0f), 0.0f, db);
    l.f ("ceiling", "ceiling (true peak)", NR (-6.0f, 0.0f), -1.0f, [] (float v) { return juce::String (v, 1) + " dbtp"; });
    l.f ("limrel", "limiter release", NR (10.0f, 1000.0f, 0.0f, 0.4f), 120.0f, ms);
    on ("limauto", "limiter auto release", true);
    l.f ("lookahead", "limiter lookahead", NR (0.5f, 5.0f), 3.0f, ms);
    l.f ("out", "output", NR (-24.0f, 6.0f), 0.0f, db);
    l.c ("dither", "dither", { "off", "24 bit", "16 bit" }, 0);
    on ("shaping", "noise shaping", false);
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
                    voiceParams (l, kv.type, pre, np, kitDefaultVoice (v));
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
                case Pid::ms0: masterParams (l); break;
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

#if FALTUNG_DEEP
static const std::vector<Preset>& deepPresets (Pid p)
{
    static std::vector<Preset> T[kNumProducts];
    static bool built = false;
    if (! built)
    {
        auto M = [] (VType t, const char* name, int model, std::array<float, 8> sl) { return Preset { name, voiceV (t, "", model, sl), {} }; };
        T[(int) Pid::bd1] = { M (VType::Kick, "deep round", 1, { 0.30f, 0.55f, 0.22f, 0.45f, 0.10f, 0.32f, 0.42f, 0.12f }),
                              M (VType::Kick, "lofi 909", 2, { 0.38f, 0.45f, 0.45f, 0.25f, 0.05f, 0.40f, 0.30f, 0.32f }),
                              M (VType::Kick, "soft sine", 3, { 0.26f, 0.60f, 0.08f, 0.55f, 0.00f, 0.22f, 0.55f, 0.05f }),
                              M (VType::Kick, "dusty tok", 0, { 0.36f, 0.38f, 0.30f, 0.30f, 0.40f, 0.35f, 0.30f, 0.25f }),
                              M (VType::Kick, "long thud", 1, { 0.24f, 0.72f, 0.18f, 0.60f, 0.05f, 0.30f, 0.50f, 0.15f }) };
        T[(int) Pid::sd2] = { M (VType::Snare, "brush", 2, { 0.40f, 0.55f, 0.08f, 0.15f, 0.78f, 0.00f, 0.32f, 0.00f }),
                              M (VType::Snare, "soft tuned", 0, { 0.42f, 0.35f, 0.25f, 0.65f, 0.40f, 0.15f, 0.40f, 0.05f }),
                              M (VType::Snare, "808 warm", 3, { 0.45f, 0.38f, 0.30f, 0.65f, 0.45f, 0.10f, 0.45f, 0.10f }) };
        T[(int) Pid::rm3] = { M (VType::Rim, "wooden rim", 1, { 0.52f, 0.20f, 0.45f, 0.62f, 0.05f, 0.10f, 0.45f, 0.00f }),
                              M (VType::Rim, "deep tok", 2, { 0.38f, 0.26f, 0.50f, 0.55f, 0.10f, 0.25f, 0.45f, 0.00f }),
                              M (VType::Rim, "soft rimshot", 0, { 0.50f, 0.16f, 0.55f, 0.50f, 0.15f, 0.05f, 0.55f, 0.08f }),
                              M (VType::Rim, "stick", 1, { 0.70f, 0.12f, 0.65f, 0.40f, 0.05f, 0.00f, 0.75f, 0.00f }) };
        T[(int) Pid::hh4] = { M (VType::Hat, "warm hats", 0, { 0.45f, 0.22f, 0.18f, 0.55f, 0.45f, 0.05f, 0.42f, 0.00f }),
                              M (VType::Hat, "shaker", 1, { 0.62f, 0.20f, 0.05f, 0.00f, 0.95f, 0.00f, 0.40f, 0.00f }),
                              M (VType::Hat, "noise hats", 1, { 0.50f, 0.26f, 0.15f, 0.10f, 0.80f, 0.00f, 0.55f, 0.00f }),
                              M (VType::Hat, "tick hats", 3, { 0.55f, 0.10f, 0.40f, 0.45f, 0.45f, 0.00f, 0.55f, 0.00f }) };
        T[(int) Pid::cp6] = { M (VType::Clap, "soft room clap", 2, { 0.35f, 0.45f, 0.35f, 0.70f, 0.35f, 0.55f, 0.22f, 0.05f }),
                              M (VType::Clap, "snap", 1, { 0.50f, 0.18f, 0.55f, 0.00f, 0.20f, 0.20f, 0.45f, 0.00f }),
                              M (VType::Clap, "tight clap", 3, { 0.45f, 0.20f, 0.45f, 0.00f, 0.25f, 0.30f, 0.40f, 0.05f }) };
        T[(int) Pid::pc7] = { M (VType::Perc, "conga low", 0, { 0.30f, 0.36f, 0.45f, 0.65f, 0.08f, 0.20f, 0.40f, 0.00f }),
                              M (VType::Perc, "conga high", 0, { 0.42f, 0.30f, 0.50f, 0.60f, 0.08f, 0.25f, 0.45f, 0.00f }),
                              M (VType::Perc, "bongo", 1, { 0.52f, 0.22f, 0.60f, 0.55f, 0.05f, 0.25f, 0.55f, 0.00f }),
                              M (VType::Perc, "soft bell", 2, { 0.62f, 0.50f, 0.20f, 0.45f, 0.00f, 0.00f, 0.50f, 0.00f }) };
        T[(int) Pid::cl8] = { M (VType::Click, "vinyl dust", 2, { 0.55f, 0.75f, 0.10f, 0.25f, 0.40f, 0.00f, 0.45f, 0.00f }),
                              M (VType::Click, "soft tick", 0, { 0.55f, 0.25f, 0.40f, 0.30f, 0.10f, 0.00f, 0.25f, 0.00f }),
                              M (VType::Click, "pop", 1, { 0.35f, 0.30f, 0.25f, 0.20f, 0.00f, 0.00f, 0.15f, 0.00f }) };
        T[(int) Pid::tm9] = { M (VType::Tom, "deep tom", 0, { 0.25f, 0.50f, 0.30f, 0.70f, 0.10f, 0.25f, 0.45f, 0.05f }),
                              M (VType::Tom, "low tom", 3, { 0.15f, 0.62f, 0.20f, 0.80f, 0.05f, 0.25f, 0.50f, 0.10f }),
                              M (VType::Tom, "blip", 1, { 0.45f, 0.30f, 0.15f, 0.50f, 0.00f, 0.45f, 0.35f, 0.00f }) };
        // kit grooves (ids: bd1 rm2 cp3 ch4 oh5 sh6 cg7 tm8)
        auto kit = [] (const char* name, const char* pattern, float swing, std::vector<std::pair<juce::String, float>> extra = {})
        {
            Preset pr; pr.name = name; pr.pattern = pattern;
            for (int v = 0; v < kKitVoices; ++v)
            {
                const auto d = kitDefaultVoice (v);
                auto vv = voiceV (kitVoice (v).type, kitPrefix (v), d.model, d.s, d.level, d.pan);
                pr.values.insert (pr.values.end(), vv.begin(), vv.end());
            }
            pr.values.push_back ({ "swing", swing });
            for (auto& e : extra) pr.values.push_back (e);
            return pr;
        };
        T[(int) Pid::kt1] = {
            kit ("warehouse", "bd1 X...X...X...X...\nrm2 ...x..x....x.?.\ncp3 ....X.......X...\nch4 o.o.o.o.o.o.o.o.\noh5 ..x...x...x...x.\nsh6 xoxoxoxoxoxoxoxo\ncg7 ......x..x....x.\ntm8 ..............r.", 0.56f),
            kit ("jazzy", "bd1 X.....x.X.......\nrm2 ..x..x....x..x..\ncp3 ....X.......X..o\nch4 x.xxx.xxx.xxx.xx\noh5 ..o...o...o...o.\nsh6 o.o.o.o.o.o.o.o.\ncg7 .x..o..x..x..o..\ntm8 ..........x.....", 0.6f),
            kit ("minimal deep", "bd1 X...X...X...X...\nrm2 .......x......x.\ncp3 ....x.......x...\nch4 ................\noh5 ..x...x...x...x.\nsh6 x?x?x?x?x?x?x?x?\ncg7 ...x......x.....\ntm8 ................", 0.55f),
            kit ("broken", "bd1 X..x..X...x.....\nrm2 ...x..x.x....x..\ncp3 ....X.......X...\nch4 x.x.x.x.x.x.x.x.\noh5 ......x.......x.\nsh6 oxoxoxoxoxoxoxox\ncg7 x.....x..x......\ntm8 .............x.x", 0.57f),
            kit ("empty", "", 0.56f) };
        T[(int) Pid::kt2] = { { "straight", {}, "sl 0123456789abcdef" }, { "chopped", { { "revprob", 0.1f } }, "sl 0.2.4401.a8.c4e" },
                              { "half", { { "gate", 0.6f } }, "sl 0.2.4.6.8.a.c.e" }, { "sparse", { { "gate", 0.4f } }, "sl 0...4...8...c..." } };
        T[(int) Pid::mc3] = { { "soft grains", { { "mode", 1 }, { "division", 4 }, { "density", 0.3f }, { "size", 80 }, { "mix", 0.3f } } },
                              { "stutter fill", { { "mode", 0 }, { "division", 1 }, { "density", 0.25f }, { "size", 90 }, { "mix", 0.7f } } },
                              { "dust from audio", { { "mode", 2 }, { "division", 4 }, { "density", 0.6f }, { "scatter", 0.7f }, { "mix", 0.25f } } } };
        T[(int) Pid::dl4] = { { "chord echo 1/8 d", { { "division", 9 }, { "feedback", 0.45f }, { "age", 0.3f }, { "hp", 300 }, { "lp", 4500 }, { "spread", 0.8f }, { "mix", 0.3f } } },
                              { "tape space 3/16", { { "division", 6 }, { "feedback", 0.55f }, { "age", 0.5f }, { "wow", 0.3f }, { "hp", 250 }, { "lp", 3000 }, { "mix", 0.3f } } },
                              { "dub throw", { { "division", 8 }, { "feedback", 0.75f }, { "age", 0.45f }, { "hp", 400 }, { "lp", 2500 }, { "mix", 0.45f }, { "input", 0.0f } } } };
        T[(int) Pid::rv5] = { { "warm room", { { "model", 0 }, { "size", 0.4f }, { "decay", 0.9f }, { "damp", 0.6f }, { "mix", 0.22f } } },
                              { "plate", { { "model", 1 }, { "size", 0.55f }, { "decay", 1.8f }, { "damp", 0.45f }, { "mix", 0.2f } } },
                              { "big chamber", { { "model", 3 }, { "size", 0.8f }, { "decay", 3.0f }, { "predelay", 25 }, { "damp", 0.55f }, { "mix", 0.25f } } } };
        T[(int) Pid::sw6] = { { "deep swing 56", { { "template", 1 }, { "swing", 0.56f } } }, { "mpc 58", { { "template", 1 }, { "swing", 0.58f }, { "accent", 0.4f } } },
                              { "shuffle", { { "template", 2 }, { "swing", 0.6f }, { "accent", 0.5f } } }, { "loose", { { "template", 5 }, { "humanize", 3 } } } };
        T[(int) Pid::fl7] = { { "flat", {} }, { "warm lowpass", { { "sweep", -0.25f }, { "res", 0.25f }, { "drive", 0.2f } } },
                              { "dj sweep 4 bars", { { "sweep", -0.3f }, { "res", 0.45f }, { "lfodiv", 16 }, { "lfodepth", 0.3f } } }, { "low kill", { { "low", -40 } } } };
        T[(int) Pid::gl8] = { { "warm glue", { { "threshold", -16 }, { "ratio", 1 }, { "attack", 15 }, { "tape", 0.3f }, { "bump", 0.6f } } },
                              { "tape only", { { "threshold", 0 }, { "tape", 0.55f }, { "bump", 0.7f }, { "hf", 0.45f } } },
                              { "pumping", { { "threshold", -26 }, { "ratio", 2 }, { "attack", 8 }, { "release", 180 }, { "auto", 0 }, { "makeup", 5 }, { "mix", 0.7f } } } };
        auto S = [] (const char* name, std::vector<std::pair<juce::String, float>> v) { return Preset { name, v, {} }; };
        const std::vector<std::pair<juce::String, float>> stabBase = { { "voices", 1 }, { "wcut", 0 }, { "sub", 0.0f }, { "ott", 0.15f }, { "width", 0.8f }, { "glide", 0 } };
        auto withBase = [&] (std::vector<std::pair<juce::String, float>> v) { auto b = stabBase; b.insert (b.end(), v.begin(), v.end()); return b; };
        T[(int) Pid::wb1] = {
            S ("minor 9 stab", withBase ({ { "chord", 9 }, { "a_shape", 2.3f }, { "b_shape", 3 }, { "b_level", 0.35f }, { "ftype", 1 }, { "cutoff", 900 }, { "res", 0.3f },
                                           { "mcut", 2.2f }, { "md", 260 }, { "aa", 2 }, { "ad", 600 }, { "as", 0.0f }, { "ar", 300 } })),
            S ("organ stab", withBase ({ { "chord", 6 }, { "a_shape", 0 }, { "b_shape", 3 }, { "b_oct", 4 }, { "b_level", 0.5f }, { "a_level", 0.8f }, { "cutoff", 3500 }, { "res", 0.1f },
                                         { "aa", 2 }, { "ad", 400 }, { "as", 0.0f }, { "ar", 120 }, { "dmode", 3 }, { "ddrive", 0.15f } })),
            S ("electric piano", withBase ({ { "chord", 8 }, { "a_shape", 0 }, { "b_shape", 0 }, { "b_oct", 5 }, { "b_level", 0 }, { "fm", 0.28f }, { "mfm", 0.3f }, { "md", 500 },
                                             { "cutoff", 5000 }, { "aa", 2 }, { "ad", 1400 }, { "as", 0.15f }, { "ar", 500 }, { "l2div", 11 }, { "l2shape", 0 }, { "m1src", MS_LFO2 }, { "m1dst", MD_VOLUME }, { "m1amt", 0.12f } })),
            S ("dub chord", withBase ({ { "chord", 13 }, { "voicing", 1 }, { "a_shape", 2 }, { "ftype", 1 }, { "cutoff", 700 }, { "res", 0.45f }, { "mcut", 2.8f }, { "md", 180 },
                                        { "aa", 1 }, { "ad", 350 }, { "as", 0.0f }, { "ar", 150 } })),
            S ("warm pad", withBase ({ { "chord", 10 }, { "a_shape", 2 }, { "a_uni", 3 }, { "a_detune", 0.2f }, { "cutoff", 1100 }, { "aa", 350 }, { "ad", 800 }, { "as", 0.8f }, { "ar", 900 },
                                       { "l2div", 15 }, { "l2shape", 0 }, { "l2retrig", 0 }, { "m1src", MS_LFO2 }, { "m1dst", MD_CUTOFF }, { "m1amt", 0.12f } })),
            S ("deep sub", { { "voices", 0 }, { "chord", 0 }, { "a_shape", 0 }, { "a_level", 0.4f }, { "sub", 1.0f }, { "suboct", 1 }, { "cutoff", 400 }, { "wcut", 0 }, { "glide", 30 },
                             { "aa", 3 }, { "ad", 600 }, { "as", 0.8f }, { "ar", 120 }, { "ott", 0.0f } }),
            S ("round bass", { { "voices", 0 }, { "chord", 0 }, { "a_shape", 2 }, { "ftype", 0 }, { "cutoff", 320 }, { "res", 0.25f }, { "mcut", 1.5f }, { "md", 220 }, { "sub", 0.7f },
                               { "wcut", 0 }, { "aa", 2 }, { "ad", 500 }, { "as", 0.6f }, { "ar", 100 }, { "ott", 0.2f } }),
            S ("plucked chord", withBase ({ { "chord", 5 }, { "a_shape", 3 }, { "a_pw", 0.3f }, { "ftype", 1 }, { "cutoff", 600 }, { "mcut", 3.2f }, { "md", 140 }, { "aa", 1 }, { "ad", 300 }, { "as", 0.0f }, { "ar", 200 } })) };
        built = true;
    }
    return T[(int) p];
}
#endif

const std::vector<Preset>& presets (Pid p)
{
    static std::vector<Preset> table[kNumProducts];
   #if FALTUNG_DEEP
    return deepPresets (p);
   #endif
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
            B ("sub wob", { { "a_shape", 2 }, { "b_shape", 3 }, { "b_oct", 2 }, { "b_level", 0.4f }, { "sub", 0.9f }, { "cutoff", 220 }, { "res", 0.35f },
                                { "fdrive", 0.35f }, { "wdiv", 8 }, { "wshape", 0 }, { "wcut", 4.5f }, { "ddrive", 0.25f }, { "ott", 0.2f } }),
            B ("square pump", { { "a_shape", 3 }, { "a_pw", 0.42f }, { "sub", 0.85f }, { "cutoff", 160 }, { "res", 0.45f }, { "wdiv", 4 }, { "wshape", 4 },
                                 { "wcut", 5.0f }, { "dmode", 3 }, { "ddrive", 0.45f }, { "ott", 0.35f } }),
            B ("wow", { { "a_shape", 2 }, { "b_shape", 2 }, { "b_semi", 7 }, { "b_level", 0.5f }, { "cutoff", 520 }, { "fmtmix", 0.8f }, { "vowel", 0 },
                               { "wvowel", 0.85f }, { "wcut", 2.5f }, { "wshape", 5 }, { "wdiv", 11 }, { "ddrive", 0.35f }, { "ott", 0.3f } }),
            B ("yoy", { { "a_shape", 2.6f }, { "a_uni", 2 }, { "a_detune", 0.15f }, { "cutoff", 700 }, { "fmtmix", 1.0f }, { "fmtq", 0.6f }, { "wshape", 6 },
                              { "wvowel", 1.0f }, { "wcut", 2.0f }, { "wdiv", 7 }, { "mpitch", -5 }, { "md", 180 }, { "dmode", 1 }, { "ddrive", 0.3f }, { "ott", 0.4f } }),
            B ("sub pressure", { { "a_shape", 0 }, { "a_level", 0.35f }, { "sub", 1.0f }, { "suboct", 1 }, { "cutoff", 380 }, { "wcut", 0 }, { "ddrive", 0.12f },
                               { "aa", 6 }, { "ar", 220 }, { "ott", 0 }, { "glide", 90 } }),
            B ("reese", { { "a_shape", 2 }, { "a_uni", 6 }, { "a_detune", 0.45f }, { "a_spread", 0.85f }, { "b_shape", 2 }, { "b_uni", 4 }, { "b_oct", 2 },
                                { "b_detune", 0.3f }, { "b_level", 0.6f }, { "cutoff", 1400 }, { "res", 0.2f }, { "wcut", 0 }, { "dmode", 5 }, { "ddrive", 0.5f },
                                { "combmix", 0.25f }, { "combfb", 0.5f }, { "ott", 0.6f }, { "glide", 80 }, { "sub", 0.6f } }),
            B ("fm growl", { { "a_shape", 0 }, { "b_shape", 0 }, { "b_oct", 4 }, { "b_level", 0 }, { "fm", 0.42f }, { "wfm", 0.3f }, { "wdiv", 4 }, { "wshape", 1 },
                                     { "wcut", 1.5f }, { "cutoff", 1800 }, { "res", 0.25f }, { "dmode", 1 }, { "ddrive", 0.3f }, { "dtone", 0.6f }, { "fmtmix", 0.3f }, { "ott", 0.6f } }),
            B ("neuro growl", { { "a_shape", 2 }, { "b_shape", 3 }, { "b_fine", 7 }, { "b_level", 0.3f }, { "fm", 0.35f }, { "fmtmix", 0.7f }, { "vowel", 1 },
                                  { "wvowel", 0.7f }, { "wdiv", 9 }, { "wshape", 1 }, { "wcut", 1.5f }, { "cutoff", 900 }, { "dmode", 2 }, { "ddrive", 0.4f },
                                  { "combmix", 0.3f }, { "wcomb", 0.4f }, { "ott", 0.8f } }),
            B ("talk box", { { "a_shape", 2 }, { "a_uni", 3 }, { "cutoff", 2400 }, { "wcut", 0 }, { "fmtmix", 1.0f }, { "fmtq", 0.8f }, { "mvowel", 0.8f }, { "md", 400 },
                             { "dmode", 3 }, { "ddrive", 0.3f }, { "ott", 0.4f } }),
            B ("slow sweep wob", { { "a_shape", 2.3f }, { "b_shape", 3 }, { "b_oct", 2 }, { "b_level", 0.35f }, { "cutoff", 140 }, { "res", 0.4f },
                                   { "wdiv", 8 }, { "wcut", 3.0f }, { "l2div", 16 }, { "l2shape", 3 }, { "l2mode", 1 }, { "l2retrig", 1 },
                                   { "m1src", MS_LFO2 }, { "m1dst", MD_CUTOFF }, { "m1amt", 0.5f }, { "m2src", MS_LFO2 }, { "m2dst", MD_DRIVE }, { "m2amt", 0.35f }, { "ott", 0.3f } }),
            B ("step wub", { { "a_shape", 2 }, { "a_uni", 2 }, { "a_detune", 0.12f }, { "cutoff", 180 }, { "wcut", 0 }, { "fmtmix", 0.6f },
                             { "stepdiv", 4 }, { "stepglide", 0.35f },
                             { "stp1", 1 }, { "stp2", 0.15f }, { "stp3", 0.7f }, { "stp4", 0.0f }, { "stp5", 0.9f }, { "stp6", 0.9f }, { "stp7", 0.2f }, { "stp8", 0.5f },
                             { "stp9", 1 }, { "stp10", 0.0f }, { "stp11", 0.6f }, { "stp12", 0.3f }, { "stp13", 0.8f }, { "stp14", 0.1f }, { "stp15", 1 }, { "stp16", 0.4f },
                             { "m1src", MS_STEPS }, { "m1dst", MD_CUTOFF }, { "m1amt", 0.75f }, { "m2src", MS_STEPS }, { "m2dst", MD_VOWEL }, { "m2amt", 0.6f },
                             { "dmode", 3 }, { "ddrive", 0.4f }, { "ott", 0.45f } }),
            B ("rate riser", { { "a_shape", 2 }, { "b_shape", 3 }, { "b_level", 0.3f }, { "cutoff", 160 }, { "wdiv", 6 }, { "wcut", 4.5f },
                               { "l2div", 14 }, { "l2shape", 3 }, { "l2mode", 1 }, { "l2retrig", 1 },
                               { "m1src", MS_LFO2 }, { "m1dst", MD_WOBRATE }, { "m1amt", 0.67f }, { "m2src", MS_LFO2 }, { "m2dst", MD_RES }, { "m2amt", 0.3f },
                               { "dmode", 0 }, { "ddrive", 0.35f } }),
            B ("neuro movement", { { "a_shape", 2 }, { "a_uni", 4 }, { "a_detune", 0.3f }, { "b_shape", 3 }, { "b_fine", 6 }, { "b_level", 0.3f }, { "fm", 0.2f },
                                   { "cutoff", 1100 }, { "wcut", 0 }, { "fmtmix", 0.6f }, { "vowel", 1 }, { "combmix", 0.3f }, { "combfb", 0.55f },
                                   { "l2div", 11 }, { "l2shape", 8 }, { "l2retrig", 0 }, { "l3div", 9 }, { "l3shape", 1 }, { "l3retrig", 0 },
                                   { "m1src", MS_LFO2 }, { "m1dst", MD_FM }, { "m1amt", 0.45f }, { "m2src", MS_LFO3 }, { "m2dst", MD_COMBTUNE }, { "m2amt", 0.3f },
                                   { "m3src", MS_STEPS }, { "m3dst", MD_VOWEL }, { "m3amt", 0.5f }, { "m4src", MS_LFO2 }, { "m4dst", MD_CUTOFF }, { "m4amt", 0.25f },
                                   { "dmode", 2 }, { "ddrive", 0.45f }, { "ott", 0.8f } }),
            B ("talking steps", { { "a_shape", 2.5f }, { "cutoff", 1600 }, { "wcut", 0 }, { "fmtmix", 1.0f }, { "fmtq", 0.7f }, { "stepdiv", 6 }, { "stepglide", 0.6f },
                                  { "stp1", 0.0f }, { "stp2", 0.5f }, { "stp3", 1.0f }, { "stp4", 0.25f }, { "stp5", 0.75f }, { "stp6", 0.0f }, { "stp7", 0.5f }, { "stp8", 1.0f },
                                  { "m1src", MS_STEPS }, { "m1dst", MD_VOWEL }, { "m1amt", 1.0f }, { "steplen", 8 }, { "dmode", 3 }, { "ddrive", 0.3f }, { "ott", 0.4f } }),
            B ("pressure growl", { { "a_shape", 2 }, { "b_shape", 0 }, { "b_oct", 4 }, { "b_level", 0 }, { "fm", 0.25f }, { "cutoff", 400 }, { "wcut", 2.5f }, { "wheel", 0 },
                                   { "m1src", MS_PRESSURE }, { "m1dst", MD_DRIVE }, { "m1amt", 0.6f }, { "m2src", MS_WHEEL }, { "m2dst", MD_CUTOFF }, { "m2amt", 0.45f },
                                   { "m3src", MS_VEL }, { "m3dst", MD_FM }, { "m3amt", 0.3f }, { "dmode", 1 }, { "ddrive", 0.25f }, { "ott", 0.5f } }),
            B ("neuro stab", { { "voices", 1 }, { "a_shape", 4 }, { "a_uni", 5 }, { "a_detune", 0.35f }, { "cutoff", 200 }, { "wcut", 0 }, { "mcut", 4.0f }, { "md", 160 },
                               { "dmode", 1 }, { "ddrive", 0.6f }, { "ott", 0.8f }, { "ad", 300 }, { "as", 0 }, { "ar", 120 }, { "sub", 0.5f } }),
        };
        // ms0 master
        auto Mp = [] (const char* name, V v) { return Preset { name, v, {} }; };
        table[(int) Pid::ms0] = {
            Mp ("transparent", { { "mbon", 0 }, { "glueon", 0 }, { "dynon", 0 }, { "b1width", 1 }, { "b2width", 1 }, { "b3width", 1 }, { "b4width", 1 } }),
            Mp ("microhouse club", { { "hpon", 1 }, { "hpfreq", 24 }, { "hpslope", 1 },
                { "e2on", 1 }, { "e2freq", 250 }, { "e2gain", -1.0f }, { "e2q", 0.8f }, { "e2ch", 1 },
                { "e5on", 1 }, { "e5freq", 420 }, { "e5gain", -1.5f }, { "e5q", 0.9f }, { "e5ch", 2 },
                { "e6on", 1 }, { "e6freq", 3000 }, { "e6gain", -0.8f }, { "e6q", 1.0f }, { "e6ch", 1 },
                { "e8on", 1 }, { "e8freq", 10000 }, { "e8gain", 1.5f }, { "e8ch", 2 },
                { "b1width", 0 }, { "b2width", 0.8f }, { "b3width", 1.05f }, { "b4width", 1.2f },
                { "saton", 1 }, { "satfocus", 1 }, { "satdrive", 3 }, { "satmix", 0.5f },
                { "gthr", -18 }, { "limgain", 4 } }),
            Mp ("dense experimental", { { "e3on", 1 }, { "e3freq", 300 }, { "e3gain", -1.5f }, { "e3q", 0.9f }, { "e3ch", 1 },
                { "e5on", 1 }, { "e5freq", 1200 }, { "e5gain", -1.0f }, { "e5q", 1.2f }, { "e5ch", 1 },
                { "e6on", 1 }, { "e6freq", 450 }, { "e6gain", -1.5f }, { "e6q", 0.8f }, { "e6ch", 2 },
                { "e8on", 1 }, { "e8freq", 9000 }, { "e8gain", 1.5f }, { "e8ch", 2 },
                { "d1on", 1 }, { "d1ch", 1 }, { "d1freq", 400 }, { "d1thr", -30 }, { "d1range", -4 },
                { "d2on", 1 }, { "d2ch", 1 }, { "d2freq", 2800 }, { "d2thr", -32 }, { "d2range", -3 },
                { "b1ratio", 1.3f }, { "b2ratio", 1.4f }, { "b3ratio", 1.4f }, { "b4ratio", 1.3f },
                { "b1width", 0 }, { "b2width", 0.85f }, { "b3width", 1.15f }, { "b4width", 1.35f }, { "air", 0.15f },
                { "ambon", 1 }, { "amblevel", -34 }, { "ambsize", 0.25f }, { "glueon", 0 }, { "limgain", 1.5f } }),
            Mp ("electroacoustic, dynamic", { { "mbon", 0 }, { "glueon", 0 }, { "b1width", 0.3f }, { "b2width", 1 }, { "b3width", 1 }, { "b4width", 1 },
                { "d1on", 1 }, { "d1ch", 0 }, { "d1freq", 3500 }, { "d1thr", -24 }, { "d1range", -3 }, { "d1q", 1.0f },
                { "limgain", 0 }, { "ceiling", -1.0f }, { "limrel", 300 } }),
            Mp ("wide air, deep space", { { "e8on", 1 }, { "e8freq", 12000 }, { "e8gain", 2.0f }, { "e8ch", 2 },
                { "b1width", 0 }, { "b2width", 0.9f }, { "b3width", 1.2f }, { "b4width", 1.5f }, { "air", 0.35f },
                { "ambon", 1 }, { "amblevel", -30 }, { "ambsize", 0.4f }, { "ambdecay", 0.9f }, { "limgain", 2 } }),
            Mp ("mono-safe low end", { { "x1", 110 }, { "x2", 300 }, { "b1width", 0 }, { "b2width", 0.5f }, { "b3width", 1 }, { "b4width", 1.15f },
                { "hpon", 1 }, { "hpfreq", 120 }, { "hpch", 2 }, { "hpslope", 2 } }),
            Mp ("loud club", { { "hpon", 1 }, { "hpfreq", 25 }, { "b1ratio", 2 }, { "b2ratio", 2 }, { "b3ratio", 1.8f }, { "b4ratio", 1.6f },
                { "b1width", 0 }, { "b2width", 0.8f }, { "b3width", 1.05f }, { "b4width", 1.2f },
                { "saton", 1 }, { "satcurve", 2 }, { "satfocus", 3 }, { "satdrive", 4 }, { "satmix", 0.6f },
                { "gthr", -20 }, { "gratio", 2 }, { "limgain", 7 }, { "lookahead", 2 } }),
            Mp ("vinyl pre-master", { { "hpon", 1 }, { "hpfreq", 25 }, { "lpon", 1 }, { "lpfreq", 18000 },
                { "x1", 150 }, { "b1width", 0 }, { "b2width", 0.75f }, { "b3width", 1 }, { "b4width", 1 },
                { "ceiling", -1.5f }, { "limgain", 0 } }),
            Mp ("dreamstate (from your mixes)", { { "hpon", 1 }, { "hpfreq", 15 }, { "hpslope", 0 },
                { "e3on", 1 }, { "e3freq", 300 }, { "e3gain", -2.0f }, { "e3q", 0.8f }, { "e3ch", 2 },
                { "e4on", 1 }, { "e4freq", 460 }, { "e4gain", -1.0f }, { "e4q", 1.0f }, { "e4ch", 1 },
                { "e8on", 1 }, { "e8freq", 10000 }, { "e8gain", 1.0f }, { "e8ch", 2 },
                { "d1on", 1 }, { "d1ch", 1 }, { "d1freq", 250 }, { "d1q", 1.2f }, { "d1thr", -28 }, { "d1range", -3 },
                { "d2on", 1 }, { "d2ch", 2 }, { "d2freq", 320 }, { "d2q", 1.0f }, { "d2thr", -32 }, { "d2range", -3 },
                { "x1", 90 }, { "b1ratio", 1.3f }, { "b2ratio", 1.3f }, { "b3ratio", 1.25f }, { "b4ratio", 1.2f },
                { "b1width", 0 }, { "b2width", 0.85f }, { "b3width", 1.0f }, { "b4width", 1.1f },
                { "glueon", 0 }, { "clipon", 1 }, { "clipdrive", 1.0f }, { "clipsoft", 0.6f },
                { "limgain", 2.0f }, { "ceiling", -1.0f }, { "limos", 3 }, { "limrel", 160 } }),
            Mp ("16-bit export", { { "mbon", 0 }, { "glueon", 0 }, { "dynon", 0 }, { "b1width", 1 }, { "b2width", 1 }, { "b3width", 1 }, { "b4width", 1 },
                { "dither", 2 }, { "shaping", 1 } }),
        };
        built = true;
    }
    return table[(int) p];
}
} // namespace fal

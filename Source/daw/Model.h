#pragma once
// faltung st0 studio — the project model: steps, patterns, arrangement, automation, modulation.
// Plain structs; everything the audio thread reads is either an atomic or an immutable snapshot
// swapped under the session lock (see Session.h). XML (de)serialisation lives in Project.cpp.
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <memory>

namespace fal::daw
{
constexpr int kMaxTracks = 32;     // instrument tracks
constexpr int kReturns = 8;        // send / return buses
constexpr int kPatterns = 64;      // patterns per track
constexpr int kMaxSteps = 64;      // steps per pattern (16 per page)
constexpr int kScenes = 64;
constexpr int kMacros = 8;
constexpr int kLocks = 4;          // parameter locks per step
constexpr int kLfos = 2;           // per track
constexpr int kModSlots = 8;       // per track
constexpr int kOutPairs = 64;      // 128 channels
constexpr int kMaxInserts = 8;
constexpr int kStepMod = 16;

/** step conditions, Elektron style */
enum class Cond : int { Always, X1of2, X2of2, X1of3, X2of3, X3of3, X1of4, X2of4, X3of4, X4of4, X1of8, X8of8, Fill, NotFill, First, NotFirst, Prev, NotPrev, Count };
inline const char* condName (Cond c)
{
    static const char* n[(int) Cond::Count] = { "always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1:8", "8:8",
                                                "fill", "not fill", "first", "not first", "prev", "not prev" };
    return n[juce::jlimit (0, (int) Cond::Count - 1, (int) c)];
}

/** one automatable thing: a track (uid > 0), a return (uid < 0) or the globals (0); a device on it (uid, 0 = the mixer
    strip / the global set) and a parameter index inside that device */
struct Target
{
    int track = 0, device = 0, param = -1;
    bool valid() const { return param >= 0; }
    bool operator== (const Target& o) const { return track == o.track && device == o.device && param == o.param; }
    bool operator!= (const Target& o) const { return ! (*this == o); }
    juce::String toString() const { return juce::String (track) + ":" + juce::String (device) + ":" + juce::String (param); }
    static Target fromString (const juce::String& s)
    {
        auto p = juce::StringArray::fromTokens (s, ":", "");
        if (p.size() != 3) return {};
        return { p[0].getIntValue(), p[1].getIntValue(), p[2].getIntValue() };
    }
};

// mixer device (device 0 on a track or return) parameter indices
enum MixerParam { MP_LEVEL = 0, MP_PAN, MP_MUTE, MP_SEND1, MP_COUNT = MP_SEND1 + kReturns };
// global set (track 0, device 0): the macros, tempo, swing, master level
enum GlobalParam { GP_MACRO1 = 0, GP_TEMPO = kMacros, GP_SWING, GP_MASTER, GP_COUNT };

struct Lock { int device = 0; int param = -1; float value = 0.0f; };   // value is normalised 0..1

struct Step
{
    bool on = false;
    float vel = 0.8f;        // 0..1
    int pitch = 0;           // semitones from the track's root note
    float prob = 1.0f;       // 0..1
    int ratchet = 1;         // 1..8
    float micro = 0.0f;      // -1..1 → ± micro range (ms)
    float len = 0.5f;        // note length in steps (0.05 … 16)
    Cond cond = Cond::Always;
    bool slide = false;      // glide the locked values to the next locked step
    std::array<Lock, kLocks> locks;
    bool hasLock (int dev, int prm) const { for (auto& l : locks) if (l.param == prm && l.device == dev) return true; return false; }
    Lock* findLock (int dev, int prm) { for (auto& l : locks) if (l.param == prm && l.device == dev) return &l; return nullptr; }
};

struct Pattern
{
    int length = 16;         // 1..64
    std::array<Step, kMaxSteps> s;
    bool empty() const { for (auto& st : s) if (st.on) return false; return true; }
};
using PatternPtr = std::shared_ptr<const Pattern>;

struct Clip { int pattern = 0; double startBar = 0, bars = 4; };
struct Scene { juce::String name; std::array<int, kMaxTracks> pattern; Scene() { pattern.fill (-2); } };   // -1 stop, -2 keep playing
struct SongEntry { int scene = 0; int bars = 8; };
struct Marker { juce::String name; double bar = 0; };

struct AutoPoint { double beat = 0; float value = 0; float curve = 0; };   // curve -1..1 bends the segment towards the next point
struct AutoLane
{
    Target target;
    std::vector<AutoPoint> pts;      // sorted by beat
    bool enabled = true;
    float valueAt (double beat) const;
};
using LanePtr = std::shared_ptr<const AutoLane>;

/** per-track modulation */
struct LfoDef { int shape = 0; int div = 11; bool sync = true; float hz = 1.0f; float phase = 0; bool retrig = false; float skew = 0.5f; float smooth = 0; };
struct EnvDef { float attackMs = 2.0f, decayMs = 220.0f; };
struct StepModDef { std::array<float, kStepMod> v {}; int length = 16; int div = 4; float glide = 0.0f; };
enum ModSrc { MS_OFF = 0, MS_LFO1, MS_LFO2, MS_ENV, MS_STEPS, MS_VEL, MS_RANDOM, MS_MACRO1, MS_MACRO8 = MS_MACRO1 + kMacros - 1, MS_CC, MS_COUNT };
inline juce::String modSrcName (int s)
{
    if (s >= MS_MACRO1 && s <= MS_MACRO8) return "macro " + juce::String (s - MS_MACRO1 + 1);
    static const char* n[] = { "off", "lfo 1", "lfo 2", "envelope", "steps", "velocity", "random", "", "", "", "", "", "", "", "", "midi cc" };
    return n[juce::jlimit (0, MS_COUNT - 1, s)];
}
inline const char* lfoShapeName (int i)
{
    static const char* n[11] = { "sine", "triangle", "ramp down", "ramp up", "square", "wow", "yoy", "s&h", "smooth random", "drift", "mpc shuffle" };
    return n[juce::jlimit (0, 10, i)];
}
constexpr int kLfoShapes = 11;
struct ModSlot { int src = MS_OFF; int device = 0; int param = -1; float amount = 0.0f; int cc = 1; };
struct ModDef
{
    std::array<LfoDef, kLfos> lfo;
    EnvDef env;
    StepModDef steps;
    std::array<ModSlot, kModSlots> slots;
    ModDef() { lfo[1].div = 14; lfo[1].shape = 1; for (int i = 0; i < kStepMod; ++i) steps.v[(size_t) i] = (i % 4 == 0) ? 1.0f : 0.25f * (float) (i % 4); }
};
using ModPtr = std::shared_ptr<const ModDef>;

struct MacroMap { Target target; float lo = 0, hi = 1; };
struct MacroDef { juce::String name; std::vector<MacroMap> maps; };
struct MidiMap { int cc = -1; Target target; float lo = 0, hi = 1; };

/** the tempo divisions in beats (shared with the suite's effects) */
double divisionBeats (int index);
juce::StringArray divisionNames();

/** speed multipliers of a track's sequencer */
inline float speedMul (int i) { static const float m[7] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f }; return m[juce::jlimit (0, 6, i)]; }
inline const char* speedName (int i) { static const char* n[7] = { "x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x4" }; return n[juce::jlimit (0, 6, i)]; }
constexpr int kSpeeds = 7;

enum class PlayMode : int { Session, Song, Arrange };
inline const char* playModeName (PlayMode m) { static const char* n[3] = { "session", "song", "arrange" }; return n[juce::jlimit (0, 2, (int) m)]; }
} // namespace fal::daw

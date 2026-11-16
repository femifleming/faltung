#pragma once
// faltung sndstudio — the project model: steps, patterns, arrangement, automation, modulators, routes, clips.
// Plain structs; everything the audio thread reads is either an atomic or an immutable snapshot
// swapped under the session lock (see Session.h). XML (de)serialisation lives in Project.cpp.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <memory>

namespace fal::daw
{
constexpr int kReturns = 8;        // send / return buses (the strip has eight send knobs)
constexpr int kPatterns = 64;      // patterns per track
constexpr int kMaxSteps = 64;      // steps per pattern (16 per page)
constexpr int kScenes = 64;
constexpr int kMacros = 8;
constexpr int kLocks = 4;          // parameter locks per step
constexpr int kOutChannels = 128;  // 64 stereo pairs
constexpr int kMaxWidth = 64;      // channels of one track / the master
constexpr int kMaxInserts = 12;
constexpr int kStepMod = 16;
constexpr int kHostTracks = 32;    // strips the host sees as parameters (the first 32 tracks)

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
    int slice = -1;          // sample slice / one-shot index for sampler tracks (-1 = the note)
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
struct Scene { juce::String name; std::vector<int> pattern; int patternFor (int track) const { return track >= 0 && track < (int) pattern.size() ? pattern[(size_t) track] : -2; } };   // -1 stop, -2 keep playing
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

/** a modulator: one source mapped to one parameter of this track (any number of them per parameter; they add) */
enum ModSrc { MOD_LFO = 0, MOD_GATE, MOD_RANDOM, MOD_ENV, MOD_FOLLOW, MOD_STEPS, MOD_MACRO, MOD_VEL, MOD_HIT, MOD_CC, MOD_CHAOS, MOD_COUNT };
inline const char* modSrcName (int s)
{
    static const char* n[MOD_COUNT] = { "lfo", "random gate", "random", "envelope", "follower", "steps", "macro", "velocity", "per hit", "midi cc", "chaos" };
    return n[juce::jlimit (0, MOD_COUNT - 1, s)];
}
inline const char* lfoShapeName (int i)
{
    static const char* n[12] = { "sine", "triangle", "ramp down", "ramp up", "square", "wow", "yoy", "s&h", "smooth random", "drift", "mpc shuffle", "pulse" };
    return n[juce::jlimit (0, 11, i)];
}
constexpr int kLfoShapes = 12;
struct Modulator
{
    int id = 0;
    bool on = true;
    int src = MOD_LFO;
    int shape = 0;
    bool sync = true; int div = 11; float hz = 1.0f;     // rate: a division of the beat, or free in hz
    float phase = 0.0f; bool retrig = false; float skew = 0.5f, pw = 0.5f;
    float slew = 0.0f;         // 0..1 → 0 … 2 s smoothing
    float prob = 0.5f;         // random gate: chance of "on" per period
    float depth = 0.25f;       // -1..1
    int polarity = 0;          // 0 bipolar around the base, 1 up, 2 down
    int macro = 0, cc = 1;
    float attackMs = 2.0f, decayMs = 220.0f;   // envelope
    std::array<float, kStepMod> steps { 1, 0.25f, 0.5f, 0.75f, 1, 0.25f, 0.5f, 0.75f, 1, 0.25f, 0.5f, 0.75f, 1, 0.25f, 0.5f, 0.75f }; int stepLen = 16;
    int device = 0, param = -1;                 // the target on this track
    bool targets (int dev, int prm) const { return device == dev && param == prm; }
};
inline const char* polarityName (int p) { static const char* n[3] = { "bipolar", "up", "down" }; return n[juce::jlimit (0, 2, p)]; }

struct MacroMap { Target target; float lo = 0, hi = 1; };
struct MidiMap { int cc = -1; Target target; float lo = 0, hi = 1; };

/** audio from one track into another (post-fader), `channels` wide, with channel offsets */
struct Route { int dst = 0; int channels = 2; int srcOffset = 0, dstOffset = 0; float gain = 1.0f; };

/** a sample in memory (loaded on the message thread, shared with the audio thread) */
struct Sample
{
    juce::String path;
    juce::AudioBuffer<float> data;
    double rate = 48000.0;
    std::vector<int> onsets;         // transient positions, for the stretcher and the slicer
    double seconds() const { return data.getNumSamples() / rate; }
};
using SamplePtr = std::shared_ptr<const Sample>;

/** an audio clip on an audio track's timeline */
struct AudioClip
{
    int id = 0;
    SamplePtr sample;
    juce::String path;               // as saved (relative to the project when possible)
    double startBar = 0;             // on the timeline
    double bars = 1;                 // length on the timeline
    double offset = 0;               // seconds into the file where the clip starts
    float gain = 1.0f;               // linear
    double fadeIn = 0, fadeOut = 0;  // seconds
    bool reverse = false, loop = false;
    float pitch = 0.0f;              // semitones (+ cents as fraction)
    int mode = 0;                    // 0 repitch (speed = pitch), 1 stretch (length free, pitch kept), 2 warp (fits the bars at the project tempo)
    double stretch = 1.0;            // mode 1: output seconds / source seconds
    double sourceSeconds() const { return sample ? std::max (0.0, sample->seconds() - offset) : 0.0; }
};
using ClipsPtr = std::shared_ptr<const std::vector<AudioClip>>;

/** the tempo divisions in beats (shared with the suite's effects) */
double divisionBeats (int index);
juce::StringArray divisionNames();

/** speed multipliers of a track's sequencer */
inline float speedMul (int i) { static const float m[7] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f }; return m[juce::jlimit (0, 6, i)]; }
inline const char* speedName (int i) { static const char* n[7] = { "x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x4" }; return n[juce::jlimit (0, 6, i)]; }
constexpr int kSpeeds = 7;

enum class PlayMode : int { Session, Song, Arrange };
inline const char* playModeName (PlayMode m) { static const char* n[3] = { "session", "song", "arrange" }; return n[juce::jlimit (0, 2, (int) m)]; }
enum class TrackKind : int { Instrument, Audio };
enum class Monitor : int { Off, In, Auto };
} // namespace fal::daw

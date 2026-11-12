// Response samples: loading, level matching, and the per-convolver treatment
// (pitch / sample-rate conversion, start position, attack-release envelope).
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <memory>
#include <vector>

namespace ecr
{
constexpr double kMaxIrSeconds = 23.0;      // the module's limit for responses and recordings
constexpr double kMaxPitchedSeconds = 30.0; // cap after pitching down
constexpr int kKnobSteps = 1023;            // the module reads its pots in 1024 steps

// A response as it sits on disk (or in the plug-in's built-in set, or a recording).
struct IrSource
{
    juce::String name;          // file name shown on the display
    juce::String id;            // "Internal/NAME", "Folder/file.wav", "file.wav" or "rec:..."
    juce::File file;            // empty for built-in responses and unsaved recordings
    double sampleRate = 48000.0;
    int numChannels = 0;        // 1, 2 or 4 (L>L, R>R, L>R, R>L)
    int length = 0;
    std::vector<float> ch[4];
    float gain = 1.0f;          // level matching, already applied to ch[]
    float peak = 1.0f;          // after gain
    bool unsaved = false;       // a recording that has not been stored yet
    std::array<std::array<uint8_t, 128>, 4> overview {}; // log amplitude per display column, 0..255
};
using IrSourcePtr = std::shared_ptr<const IrSource>;

// Decodes a file or memory block. Returns null when it cannot be read.
std::shared_ptr<IrSource> loadIrFromStream (std::unique_ptr<juce::InputStream>, const juce::String& name);
std::shared_ptr<IrSource> loadIrFromFile (const juce::File&);
// Wraps raw audio (e.g. a recording); channels are copied.
std::shared_ptr<IrSource> makeIrFromAudio (const float* const* channels, int numChannels, int length,
                                           double sampleRate, const juce::String& name);
// Level-matches and builds the display overview. Called by the functions above.
void finishIrSource (IrSource&);

// Knob (0..1) to real-world value, shared by the DSP and the display.
namespace curve
{
    inline double timeSeconds (double k)   { return kMaxIrSeconds * k * k * k; }          // attack / release
    inline bool   releaseIsFull (double k) { return k >= 0.995; }
    inline double predelaySeconds (double k) { return 2.0 * k * k * k; }
    inline double pitchOctaves (double k)  { const double o = (k - 0.5) * 4.0; return std::abs (o) < 0.03 ? 0.0 : o; }
    inline double hpfHz (double k)         { return k <= 0.001 ? 0.0 : 10.0 * std::pow (200.0, k); } // 0 = off
    inline double width (double k)         { return 2.0 * k; }
    inline double gain (double k)          { return k * k; }
    inline double gainDb (double k)        { return k <= 0.00001 ? -100.0 : juce::jmax (-100.0, 40.0 * std::log10 (k)); }
    inline double slewSeconds (double k)   { return 0.001 * std::pow (2000.0, k); }
}

// What the knobs ask of one convolver, quantised like the hardware's pots.
struct IrSettings
{
    int pos = 0, attack = 0, release = kKnobSteps, pitch = 512; // 0..1023
    bool operator== (const IrSettings& o) const { return pos == o.pos && attack == o.attack && release == o.release && pitch == o.pitch; }
    bool operator!= (const IrSettings& o) const { return ! (*this == o); }
};

struct ProcessedIr
{
    std::vector<float> h[4];
    bool active[4] {};
    int length = 0;
};

// Pitch/sample-rate conversion is the slow part, so its result is kept between
// calls while only the position or envelope changes.
class IrRenderer
{
public:
    ProcessedIr render (const IrSourcePtr&, const IrSettings&, double hostSampleRate);

    // Band-limited resampling. `ratio` = source samples per output sample.
    static void resample (const std::vector<float>& in, std::vector<float>& out, double ratio, int maxOut);

private:
    IrSourcePtr cachedSource;
    double cachedRatio = 0.0;
    std::vector<float> cached[4];
    int cachedLen = 0;
};
} // namespace ecr

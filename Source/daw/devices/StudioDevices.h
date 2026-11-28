#pragma once
// faltung sndstudio — the studio's own devices (strip, transient, sidechain, colour, stereo, throw, spring, vinyl,
// repeat, grain, multiband, generators, sampler …): AudioProcessors with stable parameter ids, created by type id.
#include <juce_audio_processors/juce_audio_processors.h>

namespace fal::daw
{
/** a device that takes a key / sidechain input from another track */
struct KeyedProcessor
{
    virtual ~KeyedProcessor() = default;
    virtual void setKey (const float* l, const float* r) = 0;
};

/** a device whose input and output widths differ (encoders, decoders): it says what it wants for an incoming width
    on a track of a given width */
struct WidthNegotiator
{
    virtual ~WidthNegotiator() = default;
    virtual std::pair<int, int> widthsFor (int incomingWidth, int trackWidth) const = 0;
};

/** a device that holds a sample (the sampler): the browser drops files onto it */
struct SampleHolder
{
    virtual ~SampleHolder() = default;
    virtual bool setSampleFile (const juce::File&) = 0;
    virtual juce::String sampleFile() const = 0;
    virtual int numSlices() const = 0;
    /** every file it holds (tape loops hold several), and one swapped for a copy elsewhere */
    virtual juce::StringArray files() const { juce::StringArray a; if (sampleFile().isNotEmpty()) a.add (sampleFile()); return a; }
    virtual void replaceFile (const juce::String& old, const juce::File& now) { if (sampleFile() == old) setSampleFile (now); }
};

/** a device of loops with their own lengths (tape loops): the lengths that play, in seconds of the song */
struct LoopLengths
{
    virtual ~LoopLengths() = default;
    virtual std::vector<double> loopLengths() const = 0;
};
/** when loops of these lengths (seconds) start together again: all of them, and each pair; within `tolerance` seconds,
    looked for up to `horizon` seconds. -1: not within it */
struct LoopScan { double all = -1; std::vector<std::tuple<int, int, double>> pairs; };
inline LoopScan scanLoops (const std::vector<double>& len, double tolerance = 0.01, double horizon = 24.0 * 3600.0)
{
    LoopScan r;
    auto together = [&] (const std::vector<double>& ls) -> double
    {
        if (ls.size() < 2) return ls.empty() ? -1.0 : ls[0];
        // the longest loop's starts, checked against every other loop (near one of its starts)
        const double big = *std::max_element (ls.begin(), ls.end());
        for (long long k = 1; (double) k * big <= horizon; ++k)
        {
            const double t = (double) k * big; bool ok = true;
            for (double l : ls) { const double m = std::fmod (t, l); if (std::min (m, l - m) > tolerance) { ok = false; break; } }
            if (ok) return t;
        }
        return -1.0;
    };
    r.all = together (len);
    for (size_t i = 0; i < len.size(); ++i) for (size_t j = i + 1; j < len.size(); ++j) r.pairs.push_back ({ (int) i, (int) j, together ({ len[i], len[j] }) });
    return r;
}
/** transient positions of a sample (energy rise), in samples (defined in Session.cpp) */
std::vector<int> detectOnsets (const juce::AudioBuffer<float>& b, double rate);

std::unique_ptr<juce::AudioProcessor> createStudioProcessor (const juce::String& typeId);
juce::StringArray studioDeviceTypes();                          // "st:strip" …
juce::String studioDeviceName (const juce::String& typeId);     // "strip"
juce::String studioDeviceBlurb (const juce::String& typeId);
bool studioDeviceIsInstrument (const juce::String& typeId);
} // namespace fal::daw

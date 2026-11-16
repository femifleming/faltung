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

/** a device that holds a sample (the sampler): the browser drops files onto it */
struct SampleHolder
{
    virtual ~SampleHolder() = default;
    virtual bool setSampleFile (const juce::File&) = 0;
    virtual juce::String sampleFile() const = 0;
    virtual int numSlices() const = 0;
};

/** transient positions of a sample (energy rise), in samples (defined in Session.cpp) */
std::vector<int> detectOnsets (const juce::AudioBuffer<float>& b, double rate);

std::unique_ptr<juce::AudioProcessor> createStudioProcessor (const juce::String& typeId);
juce::StringArray studioDeviceTypes();                          // "st:strip" …
juce::String studioDeviceName (const juce::String& typeId);     // "strip"
juce::String studioDeviceBlurb (const juce::String& typeId);
bool studioDeviceIsInstrument (const juce::String& typeId);
} // namespace fal::daw

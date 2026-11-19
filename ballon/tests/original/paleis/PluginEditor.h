// (test fixture) The original processors are compiled into the tests exactly as they are in
// github.com/femifleming/pinkiepiesballoonshop; this stands in for their window, which is not needed there.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class FreakiePieEditor : public juce::AudioProcessorEditor
{
public:
    explicit FreakiePieEditor (juce::AudioProcessor& p) : juce::AudioProcessorEditor (p) { setSize (100, 100); }
};

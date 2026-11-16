#include "StudioProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fal::daw::StudioProcessor();
}

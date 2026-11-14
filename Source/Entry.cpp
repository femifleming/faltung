#include "Processor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new fal::Processor ((fal::Pid) FALTUNG_PRODUCT);
}

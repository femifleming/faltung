#pragma once
#include <juce_core/juce_core.h>
#include <vector>

//==============================================================================
// Factory presets. Each one has a solid of its own drawn behind the balloons (the number in
// 'background', see ballon/Look.h). Every preset starts from the parameters' defaults, then sets its
// own values; store effects it uses are bought, the rest are left on the shelf.
namespace presets
{
inline constexpr int numBackgrounds = 14;

struct Value { juce::String id; float v; };
struct Preset
{
    const char* name;
    const char* group;
    int background;               // -1: the plain night-sky look
    juce::String scale;           // scale name (empty: keep the default)
    std::vector<Value> values;    // real units (choices and switches by index)
    std::vector<int> bought;      // store effects to buy (fp::StoreFX::Kind)
};

const std::vector<Preset>& list();
}

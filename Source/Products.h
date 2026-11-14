#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/Voice.h"
#include "dsp/Fx.h"

namespace fal
{
enum class Pid { kt1, kt2, bd1, sd2, rm3, hh4, cp6, pc7, cl8, tm9, mc3, dl4, rv5, sw6, fl7, gl8 };
constexpr int kNumProducts = 16;

enum class Kind { Machine, Module, Effect };
enum class Style { Snd, Fell, Oval, Stdio };

struct ProductInfo
{
    Pid id;
    const char* code;    // "kt1"
    const char* name;    // "kit"
    const char* blurb;   // one line
    Kind kind;
    Style style;
    juce::uint32 ground; // colour field
    juce::uint32 accent; // second colour (strips / lines)
    VType vtype;         // modules
    int w, h;
};

const ProductInfo& info (Pid p);
inline const ProductInfo& info (int i) { return info ((Pid) i); }

// kit voices --------------------------------------------------------------------------------------
constexpr int kKitVoices = 8;
struct KitVoiceInfo { const char* id; VType type; juce::uint32 colour; int defaultNote; };
const KitVoiceInfo& kitVoice (int v);

/** parameter layout for a product */
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (Pid p);

/** text for a voice slot value, e.g. "412 hz" */
juce::String slotText (VType t, int slot, float v);

/** factory presets: name + (param id → plain value) */
struct Preset { juce::String name; std::vector<std::pair<juce::String, float>> values; juce::String pattern; };
const std::vector<Preset>& presets (Pid p);

/** default slot values for each type (the "init" sound) */
const VoiceParams& defaultVoice (VType t);

juce::String kitPrefix (int v);  // "v1_"
} // namespace fal

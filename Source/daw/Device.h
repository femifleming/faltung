#pragma once
// faltung st0 studio — a device: one of the suite's own processors (bd1 … ms0, hosted in-process) or a
// third-party VST3 / AU instance. Every device parameter gets a "base" mirror parameter that the studio
// automates, modulates and locks; the sum is written into the real parameter on the audio thread.
#include "Model.h"
#include "../Products.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace fal::daw
{
class Device : private juce::AudioProcessorListener
{
public:
    enum class Kind { Mixer, Global, Internal, Hosted };

    /** the suite's own processors: typeId "fal:bd1" … "fal:ms0" (deep: the same ids, the deep sounds) */
    static std::unique_ptr<Device> makeInternal (int uid, Pid pid);
    static std::unique_ptr<Device> makeMixer();
    static std::unique_ptr<Device> makeGlobal();
    static std::unique_ptr<Device> makeHosted (int uid, std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& desc);
    ~Device() override;

    int uid = 0;
    Kind kind = Kind::Internal;
    juce::String typeId, name;        // "fal:bd1", "vst3:…", "mixer", "global"
    Pid pid = Pid::bd1;               // internal devices
    juce::PluginDescription desc;     // hosted devices
    bool isInstrument() const;
    bool isEffect() const { return ! isInstrument() && kind != Kind::Mixer && kind != Kind::Global; }
    std::atomic<bool> bypass { false };

    juce::AudioProcessor* processor() const { return proc.get(); }
    int numParams() const { return (int) mirrors.size(); }
    /** the base value parameter (0..1) the UI binds to */
    juce::RangedAudioParameter& base (int i) { return *mirrors[(size_t) i]; }
    float baseValue (int i) const { return mirrors[(size_t) i]->getValue(); }
    void setBase (int i, float v01, bool notify = true);
    juce::String paramName (int i) const;
    juce::String paramId (int i) const;    // stable id of the underlying parameter
    juce::String paramText (int i, float v01) const;
    int paramIndexForId (const juce::String& id) const;
    float finalValue (int i) const { return finals[(size_t) i].load (std::memory_order_relaxed); }
    bool isLocked (int i) const { return lockOn[(size_t) i].load (std::memory_order_relaxed) != 0; }
    float defaultValue (int i) const { return mirrors[(size_t) i]->getDefaultValue(); }
    /** the UI is dragging this parameter: automation leaves it alone */
    void setGesture (int i, bool on) { if (i >= 0 && i < numParams()) gestureOn[(size_t) i].store (on ? 1 : 0); }
    bool inGesture (int i) const { return gestureOn[(size_t) i].load (std::memory_order_relaxed) != 0; }

    // macro assignment per parameter (message thread writes, audio thread reads)
    struct Macro { std::atomic<int> index { -1 }; std::atomic<float> lo { 0 }, hi { 1 }; };
    std::vector<Macro> macro;

    // ---- audio thread ----------------------------------------------------------------------------
    void prepare (double sampleRate, int blockSize);
    void release();
    /** parameter locks from the sequencer: value for `param` until the next hit changes it */
    void lock (int param, float v01, bool slide, float slideTo, double ppqFrom, double ppqTo);
    void unlockAll();
    bool clearLocksExcept (const Lock* keep, int n);   // true when a lock was dropped
    int paramIndexForName (const juce::String& name) const;
    std::vector<float> modOffset;    // the engine zeroes and fills this every block
    /** base + macro + modulation + lock → the real parameter (writes only what changed) */
    void apply (const float* macros, double ppq);
    /** process one block in place (stereo). Instruments ignore the input. */
    void process (juce::AudioBuffer<float>& stereo, juce::MidiBuffer& midi);
    int latency() const { return proc ? proc->getLatencySamples() : 0; }
    void setPlayHead (juce::AudioPlayHead* h) { if (proc) proc->setPlayHead (h); }
    void reset() { if (proc) proc->reset(); }

    // ---- message thread --------------------------------------------------------------------------
    /** the device's own editor changed a parameter (or automation was written): for automation recording */
    std::function<void (int param, float v01)> onEdited;
    std::function<void (int param, bool begin)> onGesture;
    void save (juce::XmlElement& e) const;
    void load (const juce::XmlElement& e);
    /** apply one of the internal processor's factory presets */
    void setProgram (int index);
    int numPrograms() const { return proc ? proc->getNumPrograms() : 0; }
    juce::String programName (int i) const { return proc ? proc->getProgramName (i) : juce::String(); }
    /** copy the processor's current parameter values into the bases (after a preset / state load) */
    void pullBases();

private:
    Device() = default;
    struct Holder;
    std::unique_ptr<Holder> holder;
    std::unique_ptr<juce::AudioProcessor> proc;
    std::vector<juce::RangedAudioParameter*> mirrors;
    std::vector<std::atomic<float>> finals;
    std::vector<float> lastWritten;
    std::vector<std::atomic<int>> lockOn, gestureOn;
    std::vector<float> lockVal, slideFrom, slideTo;
    std::vector<double> slideA, slideB;
    std::atomic<bool> writing { false };
    juce::AudioBuffer<float> scratch;
    int scratchChannels = 2;
    bool prepared = false;
    void buildMirrors (std::function<juce::String (int)> nameOf, std::function<juce::String (int, float)> textOf, std::function<float (int)> defOf, int n);
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override;
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override;
    JUCE_DECLARE_NON_COPYABLE (Device)
};

/** mixer strip parameter helpers */
inline float mixerLevelDb (float v) { return v <= 0.001f ? -100.0f : -48.0f + 60.0f * v; }     // 0.8 = 0 db, 1 = +12 db
inline float mixerLevelFromDb (float db) { return db <= -99.0f ? 0.0f : (db + 48.0f) / 60.0f; }
juce::String mixerParamName (int i);
juce::String mixerParamText (int i, float v);
juce::String globalParamName (int i);
juce::String globalParamText (int i, float v);
inline float tempoFromNorm (float v) { return 60.0f + 120.0f * v; }
inline float tempoToNorm (float bpm) { return juce::jlimit (0.0f, 1.0f, (bpm - 60.0f) / 120.0f); }
inline float swingFromNorm (float v) { return 0.5f + 0.25f * v; }
} // namespace fal::daw

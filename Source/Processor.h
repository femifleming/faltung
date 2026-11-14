#pragma once
#include "Products.h"
#include "dsp/Sequencer.h"
#include "dsp/Bass.h"

namespace fal
{
/** A sliceable sample for kt2 loop */
struct LoopSample
{
    juce::AudioBuffer<float> buf;   // stereo
    double rate = 48000.0;
    juce::String name;
    std::vector<int> onsets;        // detected hit positions
};

class Processor : public juce::AudioProcessor
{
public:
    explicit Processor (Pid product);
    ~Processor() override;

    const Pid pid;
    const ProductInfo& pinfo;
    juce::AudioProcessorValueTreeState apvts;

    // --- AudioProcessor ---------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return juce::String (pinfo.code) + " " + pinfo.name; }
    bool acceptsMidi() const override { return pinfo.kind != Kind::Effect; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return std::max (1, (int) presets (pid).size()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- editor-facing ----------------------------------------------------------------------------
    /** audition from the UI (thread-safe): voice/track index, velocity */
    void uiTrigger (int index, float velocity = 0.85f);
    std::array<std::atomic<int>, 16> hitCount {};     // increments on each hit per voice/track
    std::atomic<float> playPpq { -1.0 };               // < 0 when stopped
    std::atomic<float> outLevel { 0.0f };

    // sequencer data (kt1: 8 tracks; kt2: 1 track)
    juce::SpinLock bankLock;
    Bank<8> kitBank;
    Bank<1> loopBank;
    int currentPattern() const;
    void applyPatternText (const juce::String& text, int patternIndex);
    juce::String patternLengthsText() const;

    // kt2 sample
    bool loadSample (const juce::File&);
    std::shared_ptr<const LoopSample> getSample() const;
    std::vector<std::pair<int, int>> currentSlices (const LoopSample&) const; // start, end in samples

    // effects, read by the editor
    DubDelay* dub() { return dubFx.get(); }
    Room* room() { return roomFx.get(); }
    Isolator* filter() { return filterFx.get(); }
    Glue* glue() { return glueFx.get(); }
    Micro* micro() { return microFx.get(); }
    Swing* swing() { return swingFx.get(); }
    BassSynth* bass() { return bassFx.get(); }
    /** synth keyboard in the editor: velocity 0 = note off */
    void uiNote (int note, float velocity) { uiTrigger (note, velocity); }
    void swingLearn (bool start);

    float param (const juce::String& id) const
    {
        auto* p = apvts.getRawParameterValue (id);
        return p ? p->load() : 0.0f;
    }
    void setParam (const juce::String& id, float plain);

private:
    double sr = 48000.0;
    int blockSize = 512;
    float lastBpm = 120.0f;
    juce::String samplePath;
    int currentProgram = 0;
    Rng rng;

    // modules + kit voices
    std::array<std::array<Voice, 2>, kKitVoices> kitVoices;
    std::array<int, kKitVoices> kitNext {};
    std::array<Voice, 6> modVoices;
    int modNext = 0;
    Sequencer<8> kitSeq;
    Sequencer<1> loopSeq;
    juce::AbstractFifo uiFifo { 64 };
    std::array<std::pair<int, float>, 64> uiQueue {};

    // kt2
    struct SliceVoice
    {
        bool on = false; double pos = 0, rate = 1, start = 0, end = 0; int age = 0, gateLen = 1;
        float env = 0, gl = 1, gr = 1, vel = 1; bool rev = false; bool releasing = false; float relK = 0.99f;
        Svf fl, fr;
    };
    std::array<SliceVoice, 6> slicers;
    int sliceNext = 0;
    std::shared_ptr<const LoopSample> sample;
    mutable juce::SpinLock sampleLock;

    std::unique_ptr<DubDelay> dubFx;
    std::unique_ptr<Room> roomFx;
    std::unique_ptr<Isolator> filterFx;
    std::unique_ptr<Glue> glueFx;
    std::unique_ptr<Micro> microFx;
    std::unique_ptr<Swing> swingFx;
    std::unique_ptr<BassSynth> bassFx;
    BassParams readBass() const;
    void processSynth (juce::AudioBuffer<float>&, juce::MidiBuffer&, const Transport&);
    Smooth masterSm;

    static BusesProperties busesFor (Pid);
    Transport readTransport();
    VoiceParams readVoice (const juce::String& pre, VType t) const;
    void triggerKit (int v, float vel);
    void triggerModule (int note, float vel);
    void triggerSlice (int slice, float vel, bool rev);
    void renderSlices (float* L, float* R, int n);
    void processKit (juce::AudioBuffer<float>&, juce::MidiBuffer&, const Transport&);
    void processModule (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void processLoop (juce::AudioBuffer<float>&, juce::MidiBuffer&, const Transport&);
    void processEffect (juce::AudioBuffer<float>&, const Transport&);
    void setSample (std::shared_ptr<const LoopSample>);
    std::shared_ptr<LoopSample> renderDefaultLoop();
    static void detectOnsets (LoopSample&);
    juce::String sampleToBase64 (const LoopSample&) const;
    std::shared_ptr<LoopSample> sampleFromBase64 (const juce::String&, const juce::String& name) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};
} // namespace fal

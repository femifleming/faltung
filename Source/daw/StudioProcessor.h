#pragma once
// faltung sndstudio — the AudioProcessor around the session: 64 stereo output buses (128 channels), the
// host-visible parameters (macros, master, the first 32 strips' level / pan / mute / sends), the project as plug-in state.
#include "Session.h"

namespace fal::daw
{
class StudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    StudioProcessor();
    ~StudioProcessor() override;
    Session session;
    juce::AudioProcessorValueTreeState apvts;

    void prepareToPlay (double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return studioInfo().code; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "project"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- project files (message thread) -----------------------------------------------------------
    bool saveProject (const juce::File&, juce::String& error);
    bool loadProject (const juce::File&, juce::String& error);
    void autosave();
    juce::File autosaveFile() const { return Session::dataDir().getChildFile ("autosave.fstudio"); }
    static juce::File projectsDir();
    /** the standalone's midi clock output (set by the app) */
    std::function<void (const juce::MidiMessage&)> midiClockOut;
    std::atomic<bool> isStandaloneApp { false };
    /** the standalone's callback hands the device inputs first in the buffer: how many, and their latency */
    std::atomic<int> standaloneInputs { 0 }, standaloneInputLatency { 0 };
    /** the standalone's audio settings page (set by the app; the editor shows it) */
    std::function<void()> openAudioSettings;
    /** the editor can close plug-in windows before a device is deleted */
    std::function<void (Device*)> onDeviceRemoving;

    static juce::String hostParamId (int track, int param);   // track: 0..31, -1..-8 returns, -100 master; param: MP_*
private:
    static BusesProperties buses();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    struct Bridge { juce::RangedAudioParameter* host; int trackSel; int param; float lastHost = -1, lastBase = -1; };
    std::vector<Bridge> bridges;
    void buildBridges();
public:
    /** the project's strip values into the host-visible parameters, at once */
    void pushToHost();
private:
    void syncFromHost();
    void timerCallback() override;
    int lastLatency = -1;
    juce::int64 lastAutosave = 0;
    std::array<const float*, kMaxWidth * 2> inPtrs {};
    double clockPpq = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioProcessor)
};
} // namespace fal::daw

#pragma once
// faltung sndstudio — the engine wrapper the standalone application drives: its audio callback hands it one buffer with
// the device inputs first and then up to 128 device outputs; project files, autosave and crash recovery.
// sndstudio is a standalone application only (no plug-in formats): there are no host parameters and no host transport.
#include "Session.h"
#include "Undo.h"

namespace fal::daw
{
class StudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    StudioProcessor();
    ~StudioProcessor() override;
    Session session;
    UndoHistory history { session };

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
    /** after a crash: the autosave as the project (it keeps the file it was an autosave of, and stays unsaved) */
    bool recoverAutosave (juce::String& error);
    /** a marker that lives while the app runs: found at start, the last run did not close properly */
    static juce::File runningMarker() { return Session::dataDir().getChildFile ("running.marker"); }
    juce::File autosaveFile() const { return autosaveOverride != juce::File() ? autosaveOverride : Session::dataDir().getChildFile ("autosave.fstudio"); }
    juce::File autosaveOverride;   // (tests: somewhere other than the user's own autosave)
    static juce::File projectsDir();
    /** the standalone's midi clock output (set by the app) */
    std::function<void (const juce::MidiMessage&)> midiClockOut;
    /** the standalone's callback hands the device inputs first in the buffer: how many, and their latency */
    std::atomic<int> standaloneInputs { 0 }, standaloneInputLatency { 0 };
    /** the standalone's audio settings page (set by the app; the editor shows it) */
    std::function<void()> openAudioSettings;
    /** called on the message thread right after a project replaced the session (open, undo): the editor
        rebuilds its views before anything paints the tracks that are gone */
    std::function<void()> onProjectReplaced;
    void projectReplaced() { if (onProjectReplaced && juce::MessageManager::existsAndIsCurrentThread()) onProjectReplaced(); }
    /** the editor can close plug-in windows before a device is deleted */
    std::function<void (Device*)> onDeviceRemoving;

private:
    static BusesProperties buses();
    void timerCallback() override;
    int lastLatency = -1;
    juce::int64 lastAutosave = 0;
    std::array<const float*, kMaxWidth * 2> inPtrs {};
    double clockPpq = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioProcessor)
};
} // namespace fal::daw

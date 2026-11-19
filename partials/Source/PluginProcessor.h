#pragma once
#include "Core.h"
#include "Image.h"
#include "Engine.h"
#include "FX.h"

class PartialsProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PartialsProcessor();
    ~PartialsProcessor() override = default;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Partials"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //----------------------------------------------------------------------------------------------
    // parameters
    static juce::String partId (int part, int pp) { return juce::String (part == 0 ? "a_" : "b_") + pt::partDefs[pp].id; }
    juce::RangedAudioParameter* partParam (int part, int pp) { return apvts.getParameter (partId (part, pp)); }
    juce::RangedAudioParameter* globalParam (int gp) { return apvts.getParameter (pt::globalDefs[gp].id); }
    // parameters added later (appended to the host's list): the extra globals and the four modifiers of every envelope
    juce::RangedAudioParameter* extraParam (int xp) { return apvts.getParameter (pt::extraDefs[xp].id); }
    float extraValue (int xp) const { return rawExtra[xp]->load(); }
    static juce::String envModId (int part, int target, int mod) { return juce::String (part == 0 ? "a_env" : "b_env") + juce::String (target) + "_" + pt::envModIds[mod]; }
    juce::RangedAudioParameter* envModParam (int part, int target, int mod) { return apvts.getParameter (envModId (part, target, mod)); }
    float envModValue (int part, int target, int mod) const { return rawEnvMod[part][target][mod]->load(); }
    float partValue (int part, int pp) const { return rawPart[part][pp]->load(); }
    float globalValue (int gp) const { return rawGlobal[gp]->load(); }
    void setParam (juce::RangedAudioParameter* p, float plainValue)
    {
        if (p == nullptr) return;
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (plainValue)); p->endChangeGesture();
    }

    // the part data the editor works on (message thread only); call commit after changing it
    pt::PartData& edit (int part) { return *editData[part]; }
    void commit (int part, int shapeChanged = -1);
    void setImage (int part, std::shared_ptr<const pt::ImageData> img) { editData[part]->image = std::move (img); commit (part); }

    bool loadAudioFile (int part, const juce::File&, bool highPrecision, int channelMode = 0);
    bool loadPictureFile (int part, const juce::File&, bool intoFreqPlane = false);
    bool analyzeWaveform (int part, int timbre, const juce::File&);      // single-cycle waveform into timbre 1 or 2 (and the phases)

    // presets
    juce::ValueTree makeState (bool includeParams = true);
    void applyState (const juce::ValueTree&);
    juce::ValueTree makePartState (int part);
    void applyPartState (int part, const juce::ValueTree&);
    void copyPart (int from, int to);
    void initPreset();
    static juce::File presetFolder();
    juce::String presetName { "Default" };
    juce::String presetInfo;                 // the free text in the info box, saved with the sound

    // X / Y / Z from MIDI: the controller numbers (-1: none), MIDI learn, and what the engine is using right now
    std::atomic<int> modCC[3] { { 1 }, { 74 }, { 71 } };
    std::atomic<int> learnMod { -1 };        // 0..2 while waiting for a controller to be moved
    std::atomic<float> modNow[3] { { 0.0f }, { 0.0f }, { 0.0f } };

    juce::AudioProcessorValueTreeState apvts;
    pt::Engine engine;
    int fxOrder[pt::FXU::Count];
    std::atomic<int> fxOrderRT[pt::FXU::Count];
    void setFxOrder (const int* o) { for (int i = 0; i < pt::FXU::Count; ++i) { fxOrder[i] = o[i]; fxOrderRT[i].store (o[i]); } }

    std::atomic<float> meterL { 0 }, meterR { 0 }, cpu { 0 };
    juce::MidiKeyboardState keyboard;
    std::atomic<double> hostBpm { 120.0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float>* rawPart[2][pt::PP::Count];
    std::atomic<float>* rawGlobal[pt::GP::Count];
    std::atomic<float>* rawExtra[pt::XP::Count];
    std::atomic<float>* rawEnvMod[2][pt::TG::FirstMapping][pt::EM::Count];
    // a controller value is used at once by the engine and handed to the host parameter from the message thread
    float ccValue[3] = { 0, 0, 0 }, ccLastParam[3] = { -1, -1, -1 }; bool ccActive[3] = { false, false, false };
    std::atomic<float> ccPending[3] { { -1.0f }, { -1.0f }, { -1.0f } }, ccEcho[3] { { -1.0f }, { -1.0f }, { -1.0f } };
    void timerCallback() override;
    std::unique_ptr<pt::PartData> editData[2];
    juce::SpinLock swapLock;
    std::shared_ptr<const pt::PartData> pending[2];
    bool pendingNew[2] = { false, false };
    pt::FxChain fx;
    juce::AudioBuffer<float> work;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PartialsProcessor)
};

// for the test tool and the manual's pictures: put the window in a given state (tab 0..3; a shape, or a target and one of its parts)
void partialsEditorShow (juce::AudioProcessorEditor*, int tab, int shape = -1, int target = -1, int art = 0, int part = 0);
#if PARTIALS_TEST
juce::StringArray partialsEditorSelfTest (juce::AudioProcessorEditor*);
#endif

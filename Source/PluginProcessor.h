#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/ConvEngine.h"
#include "dsp/IrData.h"
#include "dsp/Lfo.h"
#include "Library.h"

namespace ecr
{
class Ui;

// System-menu settings (the module's "system settings").
struct Settings
{
    bool directShift = false;   // shift latches instead of having to be held
    bool chooser = true;        // show the file list while browsing
    bool autoExit = true;       // leave the file list after 4 s
    int cvMap = 2;              // CV inputs act on: 0 = A, 1 = B, 2 = both
    int aux = 0;                // Aux CV destination: 0 = wet, 1 = tune, 2 = input level
    int svr = 0;                // screensaver: 0 = off, 1 = 30 s, 2 = 2 min, 3 = 5 min
    bool convB = true;          // second convolver enabled
    bool autoGain = true;       // limit the wet sum instead of letting it clip
};

enum SlotParam { kPos, kAttack, kRelease, kPredelay, kHpf, kWidth, kPitch, kNumSlotParams };
enum GlobalParam { kDry, kWet, kXfade, kSlew, kInLvl, kCvPos, kCvX, kCvWidth, kCvAux, kNumGlobalParams };

// modulation matrix: every LFO can be sent to each of these, in either direction
enum ModDest { dPos, dWidth, dPitch, dXfade, dPredelay, dHpf, dWet, dDry, dAttack, dRelease, dInLvl, kNumModDests };
constexpr int kNumLfos = 3;

class EcrProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    EcrProcessor();
    ~EcrProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return kMaxPitchedSeconds + 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Front-panel side (message thread) =====================================
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* slotParam (int slot, int which) const { return slotParams[slot][which]; }
    juce::RangedAudioParameter* globalParam (int which) const { return globalParams[which]; }
    static const char* slotParamName (int which);
    static const char* modDestName (int dest);          // full name
    static const char* modDestShort (int dest);         // three letters
    juce::RangedAudioParameter* lfoShapeParam (int lfo) const { return lfoShape[lfo]; }
    juce::RangedAudioParameter* lfoRateParam (int lfo) const { return lfoRate[lfo]; }
    juce::RangedAudioParameter* modAmountParam (int lfo, int dest) const { return modAmount[lfo][dest]; }
    float lfoValue (int lfo) const { return lfoOut[lfo].load(); }
    void lfoView (int lfo, float* out) const { for (int i = 0; i < Lfo::kViewSize; ++i) out[i] = lfoViz[lfo][i].load (std::memory_order_relaxed); }

    Settings settings;
    void settingsChanged();                         // push `settings` to the audio thread
    std::atomic<int> foreground { 0 };              // convolver under the knobs: 0 = A, 1 = B

    IrLibrary library;
    void selectResponse (int slot, const juce::String& id);   // loads in the background
    juce::String responseId (int slot) const;
    IrSourcePtr responseSource (int slot) const;
    bool isLoading (int slot) const { return slots[slot].pendingLoad.load(); }

    // preview ("play" button)
    void startPreview (bool processed);
    void stopPreview();
    bool previewActive() const { return previewOn.load(); }

    // recording
    void toggleRecording();
    bool isRecording() const { return recState.load() == 1; }
    double recordedSeconds() const { return recLength.load() / juce::jmax (1.0, currentRate.load()); }
    bool storeRecording (int slot);                 // writes an unsaved recording to the response folder
    bool storePresets();                            // shift + store
    bool loadPatch (int slot);                      // knob settings saved next to the current response
    void loadStoredDefaults();

    std::atomic<bool> clipping { false };           // wet sum is being limited
    std::unique_ptr<Ui> ui;

    // Blocks until both convolvers reflect the current knobs (offline use, tests).
    void waitForEngines (int timeoutMs);

private:
    struct Slot
    {
        // shared between threads
        juce::CriticalSection lock;                 // guards src / pendingId (never taken by the audio thread)
        IrSourcePtr src;
        juce::String id, pendingId, pendingPath;
        bool hasPending = false;
        uint32_t srcGen = 0;
        std::atomic<bool> pendingLoad { false };
        std::atomic<uint32_t> srcGenAtomic { 0 };
        std::atomic<int> wPos { 0 }, wAttack { 0 }, wRelease { kKnobSteps }, wPitch { 512 };
        std::atomic<uint64_t> builtKey { ~(uint64_t) 0 };
        std::atomic<ConvEngine*> ready { nullptr };
        std::atomic<float> fireImpulse { 0.0f };    // audition: play the processed response once
        std::atomic<bool> retrigger { false };      // a new response arrived (audition it if play is on)

        // helper thread
        IrRenderer renderer;
        double builtRate = 0.0;
        int buildCount = 0;

        // audio thread
        ConvEngine* cur = nullptr;
        ConvEngine* old = nullptr;
        enum { InFade, Ring, OutFade } oldState = Ring;
        int fadePos = 0, fadeLen = 1;
        juce::int64 ringLeft = 0;
        juce::int64 silentFor = 0;
        float pendingImpulse = 0.0f;
        std::vector<float> delay[2];
        int delayWrite = 0, delayCur = 0, delayNew = 0, delayFade = 0;
        float hpG = 0.0f, hpS1[2] {}, hpS2[2] {};
        float widthNow = 1.0f;
    };

    struct PreviewClip : Disposable { std::vector<float> l, r; double step = 1.0; };

    void run() override;                            // helper thread: loads, renders, builds engines
    void publishWanted();                           // knobs + CV + LFOs -> what each convolver should be
    void advanceLfos (int numSamples);
    uint64_t wantedKey (const Slot&) const;
    void processSlot (Slot&, int slotIndex, const float* inL, const float* inR, float* outL, float* outR, int n,
                      float predelaySeconds, float hpfHz, float width, int slewSamples);
    void dispose (Disposable*);
    void emptyBin();
    void clearEngines();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::shared_ptr<IrSource> loadById (const juce::String& id, const juce::String& fallbackPath = {});
    juce::File patchFileFor (const IrSource&) const;

    Slot slots[2];
    juce::RangedAudioParameter* slotParams[2][kNumSlotParams] {};
    juce::RangedAudioParameter* globalParams[kNumGlobalParams] {};
    std::atomic<float>* slotRaw[2][kNumSlotParams] {};
    std::atomic<float>* globalRaw[kNumGlobalParams] {};
    juce::RangedAudioParameter* lfoShape[kNumLfos] {};
    juce::RangedAudioParameter* lfoRate[kNumLfos] {};
    juce::RangedAudioParameter* modAmount[kNumLfos][kNumModDests] {};
    std::atomic<float>* lfoShapeRaw[kNumLfos] {};
    std::atomic<float>* lfoRateRaw[kNumLfos] {};
    std::atomic<float>* modRaw[kNumLfos][kNumModDests] {};
    Lfo lfos[kNumLfos] { Lfo (1), Lfo (2), Lfo (3) };
    std::atomic<float> lfoOut[kNumLfos] {};
    std::atomic<float> lfoViz[kNumLfos][Lfo::kViewSize] {};

    std::atomic<double> currentRate { 0.0 };
    std::atomic<int> sCvMap { 2 }, sAux { 0 };
    std::atomic<bool> sConvB { true }, sAutoGain { true };

    // audio-thread state
    static constexpr int kSub = 256;
    juce::AudioBuffer<float> scratch;               // 12 x kSub
    juce::SmoothedValue<float> inGain, dryGain, wetGain, xfade;
    float limiterGain = 1.0f, limiterRelease = 0.0f;
    int clipHold = 0;
    float wetK = 1.0f, dryK = 1.0f, inK = 1.0f, xfK = 0.0f, widthK[2] { 0.5f, 0.5f }, predelayK[2] {}, hpfK[2] {};

    // preview
    std::atomic<bool> previewOn { false }, previewProcessedMode { false };
    std::atomic<PreviewClip*> previewReady { nullptr };
    std::atomic<bool> previewStop { false };
    PreviewClip* previewClip = nullptr;
    double previewPos = 0.0;

    // recording
    std::atomic<int> recState { 0 };                // 0 idle, 1 recording, 2 waiting to be collected
    std::atomic<int> recLength { 0 };
    std::vector<float> recBuf[2];
    int recSlot = 0;
    int recCounter = 0;

    std::atomic<Disposable*> bin[32] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EcrProcessor)
};
} // namespace ecr

#pragma once
// umleitung — the engine: the sessions that are on become one CoreAudio configuration (devices + taps, one aggregate,
// one clock) and a plan: for each session, its blocks in signal order. The IO thread runs the plan: sources read
// their channels from the callback's inputs, every block sums what is wired into it, processes, meters; outputs add
// into the callback's outputs, recorders hand their audio to a writer thread.
//
// The message thread edits the model; the engine listens, refreshes block parameters at once and rebuilds the plan
// (and, if the devices or taps changed, restarts the IO) a moment later.
#include "Model.h"
#include "Dsp.h"
#include "../platform/CoreAudioHost.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

namespace um
{
class PluginHost;

/** one block, running */
struct BlockRt
{
    int id = 0, sessionId = 0; BlockType type = BlockType::Volume;
    juce::String name;
    int channels = 2;                       // what it puts out
    int inChannels = 2;                     // what reaches it
    juce::AudioBuffer<float> buf;           // its output (channels x max block)
    std::atomic<bool> bypass { false };
    std::atomic<bool> active { true };      // false: its device or tap is missing (status says why)
    juce::String status;                    // message thread
    // parameters (written by the message thread, read by the io thread)
    std::atomic<float> gainDb { 0 }, balance { 0 }, a { 0 }, b { 0 };
    std::atomic<bool> mute { false }, invert { false };
    std::atomic<int> shape { 0 }, delaySamples { 0 };
    dsp::SmoothGain gain;
    dsp::FilterBank filters;
    dsp::Limiter limiter; dsp::Leveler leveler; dsp::Gate gate; dsp::Delay delay; dsp::Tone tone;
    // the channel matrix: gains [out][in], swapped in whole
    std::vector<float> matrix, matrixPending; int matrixIns = 0; juce::SpinLock matrixLock; std::atomic<bool> matrixDirty { false };
    juce::AudioBuffer<float> scratch;
    // meters and pictures
    dsp::LevelTap levels; dsp::ScopeFifo scope; dsp::Loudness loudness; std::atomic<bool> analysed { false };
    // the hi-fi eq: speakers (trim / delay / polarity per channel); linear phase (one fir for the session's linear eqs)
    dsp::SpeakerAlign speakers;
    std::unique_ptr<juce::dsp::Convolution> conv; std::atomic<bool> linearPass { false }; int linearLatency = 0;
    // file player
    juce::AudioBuffer<float> file; double filePos = 0, fileRatio = 1; std::atomic<bool> fileLoop { true };
    // plug-in
    std::unique_ptr<juce::AudioPluginInstance> plugin; juce::MidiBuffer midi; juce::SpinLock pluginLock;
    // recorder
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> writer { nullptr };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writerOwned;
    std::atomic<long long> recorded { 0 };  // samples into the current file
    std::atomic<long long> recordedTotal { 0 };
    std::atomic<bool> silent { false };     // the recorder is pausing on silence
    juce::File currentFile; juce::Time fileStarted; int fileIndex = 0; double startedAt = 0;
};

class Engine : public AudioCallback, private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit Engine (Model&, PluginHost* plugins = nullptr);
    ~Engine() override;

    /** rebuild now (normally it happens by itself a moment after an edit) */
    void rebuild();
    /** the configuration the sessions that are on need, and which tap / device each block uses */
    IOConfig wantedConfig();
    CoreAudioHost& host() { return coreAudio; }
    bool running() const { return coreAudio.running(); }
    juce::String lastError() const { return error; }

    /** a block's running state (null when its session is off) */
    BlockRt* runtime (int blockId);
    std::shared_ptr<BlockRt> runtimeShared (int blockId);
    /** the system eq (the eq page): its session, made if missing */
    juce::ValueTree systemEqSession();

    // ---- recorder ----
    void startRecording (int blockId);
    void stopRecording (int blockId);
    bool recording (int blockId);
    static juce::File recordingFolder (const juce::ValueTree& block);
    /** an eq block's bands (graphic or parametric), as the dsp designs them */
    static std::vector<dsp::Band> bandsOf (const juce::ValueTree& block);
    /** linear phase: the fir's length (2^order) at a rate (about 1.4 s: fine enough for room modes; its delay is half) */
    static int firOrder (double sr) { int o = 10; while ((1 << o) < sr * 1.3 && o < 18) ++o; return o; }
    static juce::String fileName (const juce::ValueTree& block, const juce::String& session, int index);

    // ---- offline use (tests, and the engine's own checks): run the plan with a layout of our own ----
    void runOffline (const IOLayout&);   // prepares with this layout instead of starting CoreAudio
    bool offline = false;

    // AudioCallback
    void prepare (const IOLayout&) override;
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) override;
    void stopped() override;

    double sampleRate() const { return layout.sampleRate; }
    const IOLayout& currentLayout() const { return layout; }
    std::function<void()> onRebuilt;    // the ui refreshes its block states
    static constexpr int kMaxBlock = 4096;

private:
    struct Step
    {
        std::shared_ptr<BlockRt> rt;
        std::vector<std::pair<BlockRt*, float>> ins;   // wired into it, with the wire's gain
        int inPart = -1, firstIn = 0, countIn = 0;      // sources: channels of the callback's inputs
        int outPart = -1, firstOut = 0, countOut = 0;   // outputs: channels of the callback's outputs
    };
    struct Plan { std::vector<Step> steps; };

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { structureChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { structureChanged(); }
    void valueTreeRedirected (juce::ValueTree&) override { structureChanged(); }
    void timerCallback() override;
    void structureChanged() { dirty = true; }

    std::shared_ptr<BlockRt> makeRuntime (const juce::ValueTree& block, int sessionId);
    void refreshParams (BlockRt&, const juce::ValueTree& block);
    void prepareRuntime (BlockRt&, const juce::ValueTree& block);
    void compile();
    void runBlock (Step&, const float* const* in, int numIn, float* const* out, int numOut, int n);
    void recorderTick (BlockRt&, const juce::ValueTree& block);
    juce::String resolveOutput (const juce::ValueTree& block) const;

    Model& model; PluginHost* plugins;
    CoreAudioHost coreAudio;
    IOLayout layout; IOConfig running_;
    std::map<int, std::shared_ptr<BlockRt>> rts;
    std::shared_ptr<Plan> plan;             // the io thread's (swapped under planLock)
    juce::SpinLock planLock;
    std::vector<std::shared_ptr<Plan>> retired;
    std::map<int, int> tapOfBlock, inDevOfBlock, outDevOfBlock;   // block id → index into the config's lists
    bool dirty = true; juce::String error;
    juce::TimeSliceThread writerThread { "umleitung recorder" };
    juce::AudioFormatManager formats;
    double offlineTime = 0;
};
} // namespace um

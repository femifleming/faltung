#pragma once
// faltung st0 studio — the session: tracks, returns, master, arrangement, automation, modulation, and the
// audio engine that plays it. One instance is shared by the standalone app and the plug-in.
// Threads: the message thread edits the structure under `lock`; the audio thread takes the lock with
// tryEnter for the whole block (silence if it cannot) and reads everything directly. Pattern, lane and
// arrangement edits replace immutable snapshots (shared_ptr) under the same lock.
#include "Device.h"
#include "../dsp/Core.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace fal::daw
{
struct Track
{
    int uid = 0;                      // > 0 tracks, < 0 returns, 0 master
    juce::String name;
    juce::uint32 colour = 0xff8ec3e6;
    std::unique_ptr<Device> mixer;    // device uid 0: level, pan, mute, sends
    std::unique_ptr<Device> instrument;
    std::vector<std::unique_ptr<Device>> inserts;
    std::array<PatternPtr, kPatterns> patterns;      // null = empty
    std::shared_ptr<const std::vector<Clip>> clips;  // arrangement
    std::shared_ptr<const std::vector<int>> chain;   // pattern chain (session mode)
    ModPtr mod;

    // sequencer settings
    std::atomic<int> pattern { 0 };          // session mode: the pattern playing (or about to)
    std::atomic<int> pendingPattern { -1 };
    std::atomic<double> pendingAt { -1.0 };  // ppq where the pending pattern starts
    std::atomic<int> speed { 3 };            // index into speedMul
    std::atomic<int> rootNote { 60 };
    std::atomic<float> swingOffset { 0.0f }; // added to the global swing (-0.25 … 0.25)
    std::atomic<float> shiftMs { 0.0f };
    std::atomic<float> probMul { 1.0f };
    std::atomic<bool> solo { false }, armed { false }, chainOn { false };
    std::atomic<int> outPair { 0 };          // 0 = master, 1 … 64 = channel pair
    std::array<std::atomic<bool>, kReturns> sendPre;

    // runtime (audio thread)
    juce::AudioBuffer<float> buf, post;
    DelayLine alignL, alignR;
    int latency = 0, alignDelay = 0;
    struct NoteOff { int note; int left; };
    std::array<NoteOff, 96> pending {}; int nPending = 0;
    double anchor = 0, seqAnchor = 0; long long loopOffset = 0; int activePattern = -1; bool silent = true;
    long long lastStepN = -1; bool prevFired = false; int chainPos = 0; bool snapMixer = false;
    std::array<float, kLfos> lfoPh {}, lfoSh {}, lfoShPrev {}, lfoOut {}; std::array<double, kLfos> lfoCycle {};
    float envV = 0; double envT = -1; float lastVel = 0, lastRandom = 0; float stepsV = 0; double lastHitPpq = -1e9;
    Smooth levelSm, panSm; std::array<Smooth, kReturns> sendSm;
    Rng rng;
    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<int> playStep { -1 }, hits { 0 }, shownPattern { -1 };
    std::array<std::atomic<float>, kLfos> showLfo {}; std::atomic<float> showEnv { 0 }, showSteps { 0 };

    Track();
    bool isReturn() const { return uid < 0; }
    bool isMaster() const { return uid == 0; }
    Device* device (int devUid);
    const Device* device (int devUid) const { return const_cast<Track*> (this)->device (devUid); }
    int numDevices() const { return 1 + (instrument ? 1 : 0) + (int) inserts.size(); }
    /** all devices in processing order: instrument, inserts, mixer */
    template <typename F> void forEachDevice (F&& f) { if (instrument) f (*instrument); for (auto& d : inserts) f (*d); f (*mixer); }
    bool muted() const { return mixer->baseValue (MP_MUTE) > 0.5f; }
    PatternPtr patternAt (int i) const { return i >= 0 && i < kPatterns ? patterns[(size_t) i] : nullptr; }
};

struct HostInfo
{
    bool playing = false, hasPpq = false;
    double ppq = 0, bpm = 124;
    bool standalone = false;
};

/** where the 64 output pairs go this block: 128 channel pointers (null = not available: folds into the master) */
struct Outs { std::array<float*, 2 * kOutPairs> ch {}; };

class Session : private juce::AudioPlayHead
{
public:
    Session();
    ~Session() override;

    juce::SpinLock lock;
    std::vector<std::unique_ptr<Track>> tracks;
    std::array<std::unique_ptr<Track>, kReturns> returns;
    std::unique_ptr<Track> master;
    std::unique_ptr<Device> global;   // macros, tempo, swing, master level
    std::array<juce::String, kMacros> macroNames;
    std::shared_ptr<const std::vector<Scene>> scenes;
    std::shared_ptr<const std::vector<SongEntry>> song;
    std::shared_ptr<const std::vector<Marker>> markers;
    std::shared_ptr<const std::vector<LanePtr>> lanes;
    std::shared_ptr<const std::vector<MidiMap>> midiMaps;
    juce::String projectName { "untitled" };
    juce::File projectFile;
    std::atomic<bool> dirty { false };

    std::atomic<int> mode { (int) PlayMode::Session };
    std::atomic<bool> loopOn { false }; std::atomic<double> loopStart { 0 }, loopEnd { 8 };   // bars
    std::atomic<bool> fill { false }, running { false }, autoWrite { false };
    std::atomic<float> microMs { 12.0f };
    std::atomic<int> masterOut { 1 };
    std::atomic<int> selectedTrack { 0 };       // index into tracks; < 0: -1-r for return r; -100 master
    std::atomic<int> currentScene { -1 };
    std::atomic<int> launchQuant { 1 };         // bars (0 = immediate)
    std::array<std::atomic<float>, 128> cc;     // last midi cc values 0..1
    std::atomic<int> lastCc { -1 };             // last cc number received (for learn)
    std::atomic<bool> learning { false };
    Target learnTarget;

    // playback position (read by the UI)
    std::atomic<double> playPpq { -1.0 };       // < 0 when stopped
    std::atomic<double> internalPpq { 0.0 };
    std::atomic<float> bpmShown { 124.0f };
    std::atomic<bool> hostDriven { false };
    std::atomic<float> cpu { 0 };
    std::atomic<bool> rendering { false };
    std::function<void()> onBlock;              // called inside the lock at the start of every block (host parameter sync)

    // plug-in hosting
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::File knownFile() const;
    void loadKnown();
    void saveKnown();
    static juce::File dataDir();

    // ---- structure (message thread) ---------------------------------------------------------------
    Track* addTrack (const juce::String& name = {}, int at = -1);
    void removeTrack (int index);
    void moveTrack (int from, int to);
    Track* trackByUid (int uid);                 // tracks, returns (< 0) and master (0)
    Track* trackAt (int selection);              // the selectedTrack convention
    int indexOf (const Track*) const;
    std::unique_ptr<Device> createDevice (const juce::String& typeId, juce::String* error = nullptr);
    std::unique_ptr<Device> createHosted (const juce::PluginDescription&, juce::String* error = nullptr);
    void setInstrument (Track&, std::unique_ptr<Device>);
    Device* addInsert (Track&, std::unique_ptr<Device>, int at = -1);
    std::unique_ptr<Device> removeDevice (Track&, int devUid);   // the caller frees it after closing its window
    void moveInsert (Track&, int from, int to);
    void rebuildLatency();
    int totalLatency() const { return latTotal; }
    static juce::StringArray internalInstrumentTypes();
    static juce::StringArray internalEffectTypes();
    static juce::String typeDisplayName (const juce::String& typeId);

    // ---- pattern / arrangement / automation edits (copy on write) ---------------------------------
    void editPattern (Track&, int index, const std::function<void (Pattern&)>&);
    void setPattern (Track&, int index, PatternPtr);
    void editClips (Track&, const std::function<void (std::vector<Clip>&)>&);
    void setChain (Track&, std::vector<int>);
    void editMod (Track&, const std::function<void (ModDef&)>&);
    void editScenes (const std::function<void (std::vector<Scene>&)>&);
    void editSong (const std::function<void (std::vector<SongEntry>&)>&);
    void editMarkers (const std::function<void (std::vector<Marker>&)>&);
    void editMidiMaps (const std::function<void (std::vector<MidiMap>&)>&);
    LanePtr lane (const Target&) const;
    void editLane (const Target&, const std::function<void (AutoLane&)>&);   // creates the lane if needed
    void removeLane (const Target&);
    void recordPoint (const Target&, float v01);
    void beginGesture (const Target&); void endGesture (const Target&);
    bool inGesture (const Target&) const;

    // ---- transport ---------------------------------------------------------------------------------
    void play(); void stop(); void togglePlay();
    void locate (double bar);
    double currentBar() const { const double p = playPpq.load(); return (p < 0 ? internalPpq.load() : p) / 4.0; }
    void launch (Track&, int p);
    void launchScene (int scene);
    void stopTrack (Track&);
    void setMacro (int i, float v) { global->setBase (GP_MACRO1 + i, v); }
    float macroValue (int i) const { return global->finalValue (GP_MACRO1 + i); }
    float tempo() const { return tempoFromNorm (global->finalValue (GP_TEMPO)); }
    void setTempo (float bpm) { global->setBase (GP_TEMPO, tempoToNorm (bpm)); }
    float swing() const { return swingFromNorm (global->finalValue (GP_SWING)); }
    double songBars() const;
    double arrangementBars() const;

    // ---- targets -----------------------------------------------------------------------------------
    Device* resolve (const Target&);
    Track* trackOf (const Target& t) { return trackByUid (t.track); }
    juce::String targetName (const Target&) const;
    Target targetFor (const Track& t, const Device& d, int param) const { return { t.uid, d.uid, param }; }
    void midiLearnStart (const Target& t) { learnTarget = t; lastCc.store (-1); learning.store (true); }
    void midiLearnCancel() { learning.store (false); }
    bool midiLearnPoll();   // message thread: true when a cc arrived and the map was made

    // ---- audio -------------------------------------------------------------------------------------
    void prepare (double sampleRate, int blockSize);
    void releaseResources();
    /** one block: live midi in, the output channels in `outs` (already cleared), `numSamples` */
    void process (juce::MidiBuffer& midi, const HostInfo& h, const Outs& outs, int numSamples);
    double sampleRate() const { return sr; }
    void panic();

    // ---- export (message thread; the engine is taken over while it runs) --------------------------
    bool renderStems (const juce::File& dir, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error);
    bool exportMidi (const juce::File& file, double fromBar, double toBar, juce::String& error);

    // ---- state -------------------------------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> save() const;
    bool load (const juce::XmlElement&, juce::String* error = nullptr);
    void clear();
    void newFromTemplate (int which);
    void loadTrackXml (Track&, const juce::XmlElement&, juce::StringArray& problems);
    static juce::StringArray templateNames();

private:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    double sr = 48000; int bs = 512;
    int latTotal = 0, latTrackMax = 0, latRetMax = 0, latMaster = 0;
    double headPpq = 0, headBpm = 124; bool headPlaying = false;
    juce::MidiBuffer scratchMidi, emptyMidi;
    std::vector<Target> gestures; mutable juce::CriticalSection gestureLock; double lastRecorded = 0;
    std::atomic<bool> panicFlag { false }, relocate { false };
    int nextUid = 1;
    bool prepared = false;
    juce::Random random;

    void renderSpan (int n, double ppq0, double bpm, const Outs& outs, juce::MidiBuffer& live, bool playing);
    void resolveTrack (Track&, int index, double ppq0);
    void modulate (Track&, double ppq0, int n, double bpm);
    void sequence (Track&, double ppq0, double ppq1, double spb, double bpm, juce::MidiBuffer& out);
    void renderTrack (Track&, int n, double ppq, const float* macros, bool anySolo, const Outs& outs, juce::MidiBuffer& midi, bool isLive);
    void routeOut (Track&, const Outs&);
    void automate (double ppq);
    void applyLocks (Track&, const Pattern&, int stepIndex, double eventPpq, double stepBeats);
    void prepareTrack (Track&);
    Device* newMixer();
    juce::String uniqueTrackName (const juce::String& base) const;
    static juce::uint32 trackColour (int i);
    JUCE_DECLARE_NON_COPYABLE (Session)
};

/** tempo-synced lfo value 0..1 for a shape, phase and the per-cycle random */
float lfoValue (int shape, float ph, float sh, float shPrev);
} // namespace fal::daw

#pragma once
// faltung sndstudio — the session: tracks, returns, master, routes, arrangement, automation, modulators, clips,
// and the audio engine that plays it. One instance is shared by the standalone app and the plug-in.
// Threads: the message thread edits the structure under `lock`; the audio thread takes the lock with
// tryEnter for the whole block (silence if it cannot) and reads everything directly. Pattern, lane, clip,
// modulator and arrangement edits replace immutable snapshots (shared_ptr) under the same lock.
#include "Device.h"
#include "../dsp/Core.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace fal::daw
{
/** running state of one modulator (one per entry of Track::mods) */
struct ModState { double cycle = -1; float ph = 0, sh = 0, shPrev = 0, out = 0, gate = 0, env = 0, follow = 0, chaos = 0.37f, walk = 0.5f; double envT = -1; bool init = false; };
/** running state of one audio clip (the stretcher's grains) */
constexpr int kGrain = 2048, kHop = 1024;                 // the stretcher's grains
constexpr float kUnknown = -1.0e9f;
struct ClipState { int id = -1; std::vector<float> off; };   // per grain: the source shift found by the stretcher
/** a route's delay (latency compensation between tracks) */
struct RouteRun { std::vector<DelayLine> delay; int samples = 0; };

struct Track
{
    int uid = 0;                      // > 0 tracks, < 0 returns, 0 master
    juce::String name;
    juce::uint32 colour = 0xff8ec3e6;
    TrackKind kind = TrackKind::Instrument;
    std::unique_ptr<Device> mixer;    // device uid 0: level, pan, mute, sends
    std::unique_ptr<Device> instrument;
    std::vector<std::unique_ptr<Device>> inserts;
    std::array<PatternPtr, kPatterns> patterns;      // null = empty
    std::shared_ptr<const std::vector<Clip>> clips;  // arrangement (patterns)
    std::shared_ptr<const std::vector<int>> chain;   // pattern chain (session mode)
    std::shared_ptr<const std::vector<Modulator>> mods;
    std::vector<ModState> modState;                  // same size as *mods (resized under the lock)
    std::shared_ptr<const std::vector<Route>> routes;
    std::vector<std::unique_ptr<RouteRun>> routeRun;  // same size as *routes
    ClipsPtr aclips;                                 // audio clips (audio tracks)
    std::vector<std::unique_ptr<ClipState>> clipState;

    // sequencer settings
    std::atomic<int> pattern { 0 };          // session mode: the pattern playing (or about to)
    std::atomic<int> pendingPattern { -1 };
    std::atomic<double> pendingAt { -1.0 };  // ppq where the pending pattern starts
    std::atomic<int> speed { 3 };            // index into speedMul
    std::atomic<int> rootNote { 60 };
    std::atomic<float> swingOffset { 0.0f }; // added to the global swing (-0.25 … 0.25)
    std::atomic<float> shiftMs { 0.0f };
    std::atomic<float> probMul { 1.0f };
    std::atomic<bool> freeClock { false };   // steps in ms instead of the beat grid
    std::atomic<float> stepMs { 125.0f };
    std::atomic<bool> solo { false }, armed { false }, chainOn { false };
    // routing
    std::atomic<int> width { 2 };            // the chain / output width, 1 … 64
    std::atomic<bool> toMaster { true };
    std::atomic<int> outChannel { 0 };       // multi-out: first device channel (1 … 128), 0 = none
    std::atomic<int> keyTrack { 0 };         // sidechain source for keyed devices (track uid; 0 = master)
    std::array<std::atomic<bool>, kReturns> sendPre;
    // audio input
    std::atomic<int> inputSource { -2 };     // -2 device input, 0 master (resample), uid = another track
    std::atomic<int> inputChannel { 1 };     // first device input channel (1-based)
    std::atomic<int> inputCount { 2 };
    std::atomic<int> monitor { (int) Monitor::Auto };

    // runtime (audio thread)
    juce::AudioBuffer<float> buf, post, inst;        // chain buffer (width), output (width), instrument (2)
    std::vector<DelayLine> align;                    // width lines: the chain output lands at the same time as every other track
    std::vector<DelayLine> direct;                   // multi-out: the direct channels wait for the returns and the master too
    std::array<DelayLine, 2> instDelay;              // the instrument / clips wait for routed inputs
    int latency = 0, alignDelay = 0, inDelay = 0, arrival = 0, depth = 0, directExtra = 0;
    struct NoteOff { int note; int left; };
    std::array<NoteOff, 96> pending {}; int nPending = 0;
    double anchor = 0, seqAnchor = 0; long long loopOffset = 0; int activePattern = -1; bool silent = true;
    long long lastStepN = -1; bool prevFired = false; bool snapMixer = false;
    float lastVel = 0, lastRandom = 0; double lastHitPpq = -1e9; float hitEnvT = -1;
    Smooth levelSm, panSm; std::array<Smooth, kReturns> sendSm;
    Rng rng;
    std::array<std::atomic<float>, kMaxWidth> meter;
    std::atomic<int> playStep { -1 }, hits { 0 }, shownPattern { -1 };
    std::atomic<float> showFollow { 0 };
    // recording (set up on the message thread, written on the audio thread)
    juce::AudioFormatWriter::ThreadedWriter* recWriter = nullptr;
    std::atomic<bool> recActive { false };
    double recStartPpq = 0; juce::int64 recWritten = 0, recDropped = 0; int recChannels = 2; juce::File recFile; int recClipId = 0;

    Track();
    bool isReturn() const { return uid < 0; }
    bool isMaster() const { return uid == 0; }
    bool isAudio() const { return kind == TrackKind::Audio; }
    Device* device (int devUid);
    const Device* device (int devUid) const { return const_cast<Track*> (this)->device (devUid); }
    int numDevices() const { return 1 + (instrument ? 1 : 0) + (int) inserts.size(); }
    /** all devices in processing order: instrument, inserts, mixer */
    template <typename F> void forEachDevice (F&& f) { if (instrument) f (*instrument); for (auto& d : inserts) f (*d); f (*mixer); }
    bool muted() const { return mixer->baseValue (MP_MUTE) > 0.5f; }
    PatternPtr patternAt (int i) const { return i >= 0 && i < kPatterns ? patterns[(size_t) i] : nullptr; }
    int outWidth() const;    // what the chain produces (the last device's output width, or 2)
    int inWidth() const;     // what the first device reads
};

struct HostInfo
{
    bool playing = false, hasPpq = false;
    double ppq = 0, bpm = 124;
    bool standalone = false;
    const float* const* in = nullptr; int numIn = 0;    // device inputs (standalone)
    int inputLatency = 0;                                 // samples, for placing recordings
};

struct ExportOptions { int bits = 24; bool dither = true; bool limit = false; float ceilingDb = -1.0f; bool tail = true; };

/** where the 128 output channels go this block: channel pointers (null = not available: folds into the master) */
struct Outs { std::array<float*, kOutChannels> ch {}; };

class Session : private juce::AudioPlayHead
{
public:
    Session();
    ~Session() override;

    juce::SpinLock lock;
    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<std::unique_ptr<Track>> returns;    // up to kReturns
    std::unique_ptr<Track> master;
    std::vector<Track*> order;                      // processing order of tracks and returns (routes first)
    std::unique_ptr<Device> global;   // macros, tempo, swing, master level
    std::array<juce::String, kMacros> macroNames;
    std::shared_ptr<const std::vector<Scene>> scenes;
    std::shared_ptr<const std::vector<SongEntry>> song;
    std::shared_ptr<const std::vector<Marker>> markers;
    std::shared_ptr<const std::vector<LanePtr>> lanes;
    std::shared_ptr<const std::vector<MidiMap>> midiMaps;
    juce::File browseDir;                           // the browser's folder (the music folder when empty)
    juce::String projectName { "untitled" };
    juce::File projectFile;
    std::atomic<bool> dirty { false };

    std::atomic<int> mode { (int) PlayMode::Session };
    std::atomic<bool> loopOn { false }; std::atomic<double> loopStart { 0 }, loopEnd { 8 };   // bars
    std::atomic<bool> fill { false }, running { false }, autoWrite { false }, recording { false }, punch { false };
    std::atomic<float> microMs { 12.0f };
    std::atomic<bool> multiOut { false };        // produce (everything to the master) or multi-out (tracks on their own channels)
    std::atomic<bool> masterBypass { false };    // bypass the master chain (mixing elsewhere)
    std::atomic<int> masterOut { 1 };            // first device channel of the master
    std::atomic<int> selectedTrack { 0 };        // index into tracks; < 0: -1-r for return r; -100 master
    std::atomic<int> currentScene { -1 };
    std::atomic<int> launchQuant { 1 };          // bars (0 = immediate)
    std::atomic<bool> snap { true };             // timeline edits snap to the grid
    std::array<std::atomic<float>, 128> cc;      // last midi cc values 0..1
    std::atomic<int> lastCc { -1 };              // last cc number received (for learn)
    std::atomic<bool> learning { false };
    Target learnTarget;

    // playback position (read by the UI)
    std::atomic<double> playPpq { -1.0 };       // < 0 when stopped
    std::atomic<double> internalPpq { 0.0 };
    std::atomic<float> bpmShown { 124.0f };
    std::atomic<bool> hostDriven { false };
    std::atomic<float> cpu { 0 };
    std::atomic<bool> rendering { false };
    std::atomic<int> inputChannels { 0 };       // what the device offers (standalone)
    std::function<void()> onBlock;              // called inside the lock at the start of every block (host parameter sync)

    // plug-in hosting
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::File knownFile() const;
    void loadKnown();
    void saveKnown();
    static juce::File dataDir();
    juce::File projectDir() const;              // where the project's audio folder lives
    juce::File audioDir() const;

    // ---- structure (message thread) ---------------------------------------------------------------
    Track* addTrack (const juce::String& name = {}, int at = -1, TrackKind kind = TrackKind::Instrument);
    void removeTrack (int index);
    void moveTrack (int from, int to);
    Track* addReturn (const juce::String& name = {});
    void removeReturn (int index);
    Track* trackByUid (int uid);                 // tracks, returns (< 0) and master (0)
    Track* trackAt (int selection);              // the selectedTrack convention
    int indexOf (const Track*) const;
    std::unique_ptr<Device> createDevice (const juce::String& typeId, juce::String* error = nullptr);
    std::unique_ptr<Device> createHosted (const juce::PluginDescription&, juce::String* error = nullptr);
    void setInstrument (Track&, std::unique_ptr<Device>);
    Device* addInsert (Track&, std::unique_ptr<Device>, int at = -1);
    std::unique_ptr<Device> removeDevice (Track&, int devUid);   // the caller frees it after closing its window
    void moveInsert (Track&, int from, int to);
    void setWidth (Track&, int width);
    void negotiate (Track&);                     // device widths along the chain
    bool addRoute (Track& src, Route);           // false on a cycle
    void removeRoute (Track& src, int index);
    void editRoutes (Track& src, const std::function<void (std::vector<Route>&)>&);
    void rebuildLatency();                       // also the processing order and route delays
    int totalLatency() const { return latTotal; }
    void assignOutputs();                        // multi-out: master on 1 … w, then every track and return on its own channels
    static juce::StringArray internalInstrumentTypes();
    static juce::StringArray internalEffectTypes();
    static juce::String typeDisplayName (const juce::String& typeId);
    /** every device type the browser offers: internal, studio, hosted (from the known list) */
    struct TypeEntry { juce::String typeId, name, category; bool instrument; };
    std::vector<TypeEntry> deviceTypes() const;

    // ---- pattern / arrangement / automation edits (copy on write) ---------------------------------
    void editPattern (Track&, int index, const std::function<void (Pattern&)>&);
    void setPattern (Track&, int index, PatternPtr);
    void editClips (Track&, const std::function<void (std::vector<Clip>&)>&);
    void setChain (Track&, std::vector<int>);
    void editMods (Track&, const std::function<void (std::vector<Modulator>&)>&);
    Modulator* findMod (Track&, int id) const;
    int addModulator (Track&, Modulator m);      // returns its id
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

    // ---- audio clips and samples --------------------------------------------------------------------
    SamplePtr loadSample (const juce::File&, juce::String* error = nullptr);
    AudioClip* addAudioClip (Track&, const juce::File&, double startBar, juce::String* error = nullptr);
    void editAudioClips (Track&, const std::function<void (std::vector<AudioClip>&)>&);
    void splitClip (Track&, int clipId, double atBar);
    juce::File collectSample (const juce::File&);   // copy into the project's audio folder
    // recording
    void startRecording();                           // arms the writers for armed audio tracks; recording follows play
    void stopRecording();                            // finishes the clips
    bool isRecording() const { return recording.load(); }
    std::unique_ptr<juce::TimeSliceThread> writerThread;
    /** render the whole project (or the loop) offline into a new audio track: "resample project" */
    Track* renderProjectToTrack (double fromBar, double toBar, bool preMaster, juce::String& error);
    /** render one track's output offline into a new audio track */
    Track* renderTrackToTrack (Track& src, double fromBar, double toBar, juce::String& error);

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
    double projectBars() const;                  // by mode: song, arrangement, or the loop

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
    /** one block: live midi in, device inputs in `h`, the output channels in `outs` (already cleared), `numSamples` */
    void process (juce::MidiBuffer& midi, const HostInfo& h, const Outs& outs, int numSamples);
    double sampleRate() const { return sr; }
    int blockSize() const { return bs; }
    void panic();

    // ---- export (message thread; the engine is taken over while it runs) --------------------------
    bool renderStems (const juce::File& dir, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error, ExportOptions = {});
    bool renderMaster (const juce::File& file, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error, ExportOptions = {}, bool preMaster = false);
    bool exportMidi (const juce::File& file, double fromBar, double toBar, juce::String& error);

    // ---- state -------------------------------------------------------------------------------------
    std::unique_ptr<juce::XmlElement> save() const;
    bool load (const juce::XmlElement&, juce::String* error = nullptr);
    void clear();
    void newFromTemplate (int which);
    static juce::StringArray templateNames();
    void loadTrackXml (Track&, const juce::XmlElement&, juce::StringArray& problems);

private:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
    double sr = 48000; int bs = 512;
    int latTotal = 0, latTracks = 0, latReturns = 0, latMaster = 0;
    double headPpq = 0, headBpm = 124; bool headPlaying = false;
    juce::MidiBuffer scratchMidi, emptyMidi;
    std::vector<Target> gestures; mutable juce::CriticalSection gestureLock; double lastRecorded = 0;
    std::atomic<bool> panicFlag { false }, relocate { false };
    int nextUid = 1, nextClipId = 1, nextModId = 1;
    bool prepared = false;
    std::map<juce::String, std::weak_ptr<const Sample>> sampleCache;
    juce::AudioFormatManager audioFormats;
    std::vector<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>> recWriters;
    std::vector<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>> oldWriters;
    juce::AudioBuffer<float> recScratch;

    void renderSpan (int n, double ppq0, double bpm, const Outs& outs, juce::MidiBuffer& live, bool playing, const HostInfo& h);
    void resolveTrack (Track&, int index, double ppq0);
    void modulate (Track&, double ppq0, int n, double bpm);
    void sequence (Track&, double ppq0, double ppq1, double spb, double bpm, juce::MidiBuffer& out);
    void renderTrack (Track&, int n, double ppq0, double bpm, const float* macros, bool anySolo, const Outs& outs, juce::MidiBuffer& midi, const HostInfo& h, bool playing);
    void playClips (Track&, int n, double ppq0, double bpm, bool playing);
    void automate (double ppq);
    void applyLocks (Track&, const Pattern&, int stepIndex, double eventPpq, double stepBeats);
    void prepareTrack (Track&);
    void sizeBuffers (Track&);
    bool wouldCycle (const Track& src, int dstUid) const;
    double stepBeatsOf (const Track&, double bpm) const;
    juce::String uniqueTrackName (const juce::String& base) const;
    static juce::uint32 trackColour (int i);
    void finishRecordings();
    void resetForOffline();
    void recomputeIds();
    double grainOffset (ClipState*, const AudioClip&, int k, double stretch, double ratio, double fileRate);
    JUCE_DECLARE_NON_COPYABLE (Session)
};

/** tempo-synced lfo value 0..1 for a shape, phase and the per-cycle random */
float lfoValue (int shape, float ph, float sh, float shPrev, float pw);
/** transient positions of a sample (energy rise), in samples */
std::vector<int> detectOnsets (const juce::AudioBuffer<float>& b, double rate);
} // namespace fal::daw

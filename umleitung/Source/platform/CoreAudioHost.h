#pragma once
// umleitung — the Mac's audio, seen from the app: devices, the processes that play or record, process taps
// (an app's sound, or the whole system's, captured and optionally muted at the source: macOS 14.2+), and one
// engine that runs them all on one clock: a private aggregate device made of the input and output devices and
// the taps a configuration needs, with drift compensation, and one IO callback that sees every channel.
//
// Thread rules: everything here is called on the message thread except AudioCallback::process (the IO thread).
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>
#include <sys/types.h>

namespace um
{
struct DeviceInfo
{
    juce::String uid, name, manufacturer;
    uint32_t id = 0;                     // AudioObjectID (valid until the device list changes)
    int inChannels = 0, outChannels = 0;
    double sampleRate = 48000;
    std::vector<double> sampleRates;     // the nominal rates it offers
    juce::String transport;              // "built-in", "usb", "bluetooth", "virtual", "aggregate", "airplay", ...
    bool isDefaultInput = false, isDefaultOutput = false, isDefaultSystemOutput = false;
    bool isOurs = false;                 // a device of umleitung's own driver (manufacturer "umleitung")
    bool isAggregate = false, isHidden = false;
    bool hasVolume = false;              // the output can be turned up and down (hardware / driver volume)
};

struct ProcessInfo
{
    pid_t pid = 0;
    uint32_t objectId = 0;               // the CoreAudio process object
    juce::String bundleId, name, path;   // name: the app's name (from the bundle), or the executable
    bool playing = false, recording = false;   // running output / input IO right now
};

/** what a tap captures */
struct TapSpec
{
    enum class Kind { System, Processes, AllButProcesses };
    Kind kind = Kind::Processes;
    std::vector<pid_t> pids;             // Processes: these; AllButProcesses: everything but these (umleitung itself is always left out)
    std::vector<juce::String> bundleIds; // also matched (every process of the app, also ones started later)
    bool muteSource = false;             // the captured apps go silent on their own output (the copy is what you hear)
    bool mono = false;                   // a mono mixdown (else stereo)
    juce::String deviceUid;              // tap what goes to this output device (empty: the default output)
    juce::String name;                   // shown in the layout
};

/** what the engine runs */
struct IOConfig
{
    std::vector<juce::String> inputDevices;    // UIDs (input channels of each)
    std::vector<juce::String> outputDevices;   // UIDs (output channels of each)
    std::vector<TapSpec> taps;
    juce::String clockDevice;            // the device the others follow (empty: the first output, else the first input)
    double sampleRate = 0;               // 0: the clock device's current rate
    int bufferSize = 512;
};

/** where each source / destination sits in the callback's flat channel arrays */
struct IOLayout
{
    struct Part
    {
        enum class Kind { Tap, InputDevice, OutputDevice };
        Kind kind = Kind::InputDevice;
        int index = 0;                   // into IOConfig::taps / inputDevices / outputDevices
        juce::String uid, name;
        int firstChannel = 0, numChannels = 0;
    };
    std::vector<Part> inputs, outputs;
    int totalIn = 0, totalOut = 0;
    double sampleRate = 48000;
    int bufferSize = 512;
};

class AudioCallback
{
public:
    virtual ~AudioCallback() = default;
    /** before the first process(), on the message thread */
    virtual void prepare (const IOLayout&) = 0;
    /** the IO thread: in[ch] (layout.totalIn channels, deinterleaved, read only), out[ch] (layout.totalOut channels,
        cleared before the call); never allocate or lock here */
    virtual void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) = 0;
    /** after the engine stopped (message thread) */
    virtual void stopped() {}
};

class CoreAudioHost
{
public:
    CoreAudioHost();
    ~CoreAudioHost();

    static std::vector<DeviceInfo> devices();
    static std::vector<ProcessInfo> audioProcesses();   // every process CoreAudio knows (not umleitung itself)
    static juce::String defaultOutputUid();
    static juce::String defaultInputUid();
    static juce::String defaultSystemOutputUid();      // alerts and sound effects
    static bool setDefaultOutput (const juce::String& uid);
    static bool setDefaultInput (const juce::String& uid);
    static bool setDefaultSystemOutput (const juce::String& uid);
    /** an output device's own volume (0..1) and mute, where it has them */
    static float deviceVolume (const juce::String& uid);
    static bool setDeviceVolume (const juce::String& uid, float v01);
    static bool deviceMuted (const juce::String& uid);
    static bool setDeviceMuted (const juce::String& uid, bool);

    /** process taps exist on this system (macOS 14.2 and later) */
    static bool tapsSupported();
    /** whether umleitung may capture other apps' audio ("system audio recording only" in privacy settings):
        unknown until a tap has run; the app shows how to allow it when it is denied */
    enum class Permission { Unknown, Granted, Denied };
    static Permission capturePermission();

    /** start (or restart with) a configuration: builds the aggregate device and the taps, runs the callback.
        False and a message when it cannot (a device gone, taps refused ...) */
    bool start (const IOConfig&, AudioCallback*, juce::String& error);
    void stop();
    bool running() const;
    const IOLayout& layout() const;
    const IOConfig& config() const;
    /** the IO thread's load (0..1, time in the callback over the buffer's length), and dropouts counted */
    float load() const;
    int overloads() const;

    /** devices added / removed / changed (defaults, rates), and processes starting or stopping their audio: called on
        the message thread */
    std::function<void()> onDevicesChanged;
    std::function<void()> onProcessesChanged;
    /** the engine stopped by itself (a device in use disappeared ...), with the reason */
    std::function<void (juce::String)> onStoppedUnexpectedly;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace um

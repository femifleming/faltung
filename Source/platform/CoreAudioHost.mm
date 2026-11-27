// umleitung — the CoreAudio side: devices, processes, process taps and the one aggregate engine.
//
// How the engine is put together (all of it found out on the machine, macOS 26/27, and checked by tests/HostCheck):
//  * one private aggregate device: sub-devices = every input and output device of the configuration (a device used for
//    both directions is one sub-device), the clock device as main sub-device, drift compensation on every other one;
//    the taps in kAudioAggregateDeviceTapListKey, each with drift compensation.
//  * the aggregate's streams come in this order: the sub-devices in their active-list order (each with all its streams
//    of that direction), then the taps in their active-list order (one input stream each). The layout is computed from
//    the aggregate's own stream configuration with that rule, and start() refuses an aggregate whose streams don't add
//    up, so the channel mapping is exact or nothing runs.
//  * the IOProc sees one AudioBuffer per stream (interleaved) or per channel (non-interleaved streams): both are
//    handled through one table aggregate-channel → flat-channel. Streams nobody uses are switched off with
//    kAudioDevicePropertyIOProcStreamUsage (an input-only device's outputs, an output-only device's inputs).
//
// Capture permission ("System Audio Recording Only" in Privacy & Security): macOS has no public query. When it is
// denied a tap still gets created and runs, it only delivers digital silence. So:
//  * Granted as soon as any tap delivers one non-zero sample.
//  * Denied when the taps delivered only exact zeros for 3 seconds of running while a process they capture was
//    running output IO (kAudioProcessPropertyIsRunningOutput). An app that runs its output but plays pure digital
//    silence looks the same, so Denied is a strong hint, not proof; it turns into Granted with the first real sample.
//  * Unknown before that. Nothing here ever asks again or loops: macOS shows its prompt once, on the first tap read.
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>
#include <libproc.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <set>
#include "CoreAudioHost.h"

// the virtual main volume lives only in AudioHardwareService (deprecated, still the one way to get it)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace um
{
namespace
{
// ------------------------------------------------------------------------------------------------ property helpers
AudioObjectPropertyAddress addr (AudioObjectPropertySelector s, AudioObjectPropertyScope sc = kAudioObjectPropertyScopeGlobal,
                                 AudioObjectPropertyElement e = kAudioObjectPropertyElementMain)
{
    return { s, sc, e };
}

template <typename T>
bool getProp (AudioObjectID obj, AudioObjectPropertyAddress a, T& out, UInt32 qualSize = 0, const void* qual = nullptr)
{
    if (! AudioObjectHasProperty (obj, &a)) return false;
    UInt32 size = sizeof (T);
    return AudioObjectGetPropertyData (obj, &a, qualSize, qual, &size, &out) == noErr && size == sizeof (T);
}

template <typename T>
std::vector<T> getArray (AudioObjectID obj, AudioObjectPropertyAddress a)
{
    UInt32 size = 0;
    if (! AudioObjectHasProperty (obj, &a) || AudioObjectGetPropertyDataSize (obj, &a, 0, nullptr, &size) != noErr || size == 0)
        return {};
    std::vector<T> v (size / sizeof (T));
    if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, v.data()) != noErr) return {};
    v.resize (size / sizeof (T));
    return v;
}

juce::String toJuce (NSString* s) { return s != nil ? juce::String::fromUTF8 (s.UTF8String) : juce::String(); }
NSString* toNS (const juce::String& s) { return [NSString stringWithUTF8String: s.toRawUTF8()]; }

juce::String getString (AudioObjectID obj, AudioObjectPropertyAddress a)
{
    CFStringRef s = nullptr;
    if (! getProp (obj, a, s) || s == nullptr) return {};
    return toJuce ((__bridge_transfer NSString*) s);
}

AudioObjectID deviceForUid (const juce::String& uid)
{
    if (uid.isEmpty()) return kAudioObjectUnknown;
    AudioObjectID dev = kAudioObjectUnknown;
    CFStringRef s = (__bridge CFStringRef) toNS (uid);
    AudioValueTranslation t { &s, sizeof (s), &dev, sizeof (dev) };
    UInt32 size = sizeof (t);
    auto a = addr (kAudioHardwarePropertyDeviceForUID);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &t) != noErr) return kAudioObjectUnknown;
    return dev;
}

juce::String uidOf (AudioObjectID dev) { return getString (dev, addr (kAudioDevicePropertyDeviceUID)); }

int channelCount (AudioObjectID dev, AudioObjectPropertyScope scope)
{
    auto a = addr (kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (! AudioObjectHasProperty (dev, &a) || AudioObjectGetPropertyDataSize (dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::vector<uint8_t> mem (size);
    auto* abl = reinterpret_cast<AudioBufferList*> (mem.data());
    if (AudioObjectGetPropertyData (dev, &a, 0, nullptr, &size, abl) != noErr) return 0;
    int n = 0;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) n += (int) abl->mBuffers[i].mNumberChannels;
    return n;
}

/** per buffer of the IOProc's AudioBufferList: its channel count */
std::vector<int> bufferChannels (AudioObjectID dev, AudioObjectPropertyScope scope)
{
    auto a = addr (kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (dev, &a, 0, nullptr, &size) != noErr || size == 0) return {};
    std::vector<uint8_t> mem (size);
    auto* abl = reinterpret_cast<AudioBufferList*> (mem.data());
    if (AudioObjectGetPropertyData (dev, &a, 0, nullptr, &size, abl) != noErr) return {};
    std::vector<int> v;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) v.push_back ((int) abl->mBuffers[i].mNumberChannels);
    return v;
}

double nominalRate (AudioObjectID dev)
{
    Float64 r = 0;
    getProp (dev, addr (kAudioDevicePropertyNominalSampleRate), r);
    return r;
}

std::vector<double> availableRates (AudioObjectID dev)
{
    static const double common[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000 };
    std::set<double> s;
    for (auto& r : getArray<AudioValueRange> (dev, addr (kAudioDevicePropertyAvailableNominalSampleRates)))
    {
        if (r.mMinimum == r.mMaximum) s.insert (r.mMinimum);
        else for (double c : common) if (c >= r.mMinimum && c <= r.mMaximum) s.insert (c);
    }
    return { s.begin(), s.end() };
}

juce::String transportName (UInt32 t)
{
    switch (t)
    {
        case kAudioDeviceTransportTypeBuiltIn:     return "built-in";
        case kAudioDeviceTransportTypeAggregate:   return "aggregate";
        case kAudioDeviceTransportTypeVirtual:     return "virtual";
        case kAudioDeviceTransportTypePCI:         return "pci";
        case kAudioDeviceTransportTypeUSB:         return "usb";
        case kAudioDeviceTransportTypeFireWire:    return "firewire";
        case kAudioDeviceTransportTypeBluetooth:   return "bluetooth";
        case kAudioDeviceTransportTypeBluetoothLE: return "bluetooth";
        case kAudioDeviceTransportTypeHDMI:        return "hdmi";
        case kAudioDeviceTransportTypeDisplayPort: return "displayport";
        case kAudioDeviceTransportTypeAirPlay:     return "airplay";
        case kAudioDeviceTransportTypeAVB:         return "avb";
        case kAudioDeviceTransportTypeThunderbolt: return "thunderbolt";
        case kAudioDeviceTransportTypeContinuityCaptureWired:
        case kAudioDeviceTransportTypeContinuityCaptureWireless: return "continuity";
        case kAudioDeviceTransportTypeUnknown:     return "unknown";
        default: break;
    }
    return "other";
}

AudioObjectID defaultDevice (AudioObjectPropertySelector sel)
{
    AudioObjectID d = kAudioObjectUnknown;
    getProp (kAudioObjectSystemObject, addr (sel), d);
    return d;
}

bool setDefaultDevice (AudioObjectPropertySelector sel, const juce::String& uid)
{
    AudioObjectID d = deviceForUid (uid);
    if (d == kAudioObjectUnknown) return false;
    auto a = addr (sel);
    return AudioObjectSetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, sizeof (d), &d) == noErr;
}

AudioObjectID processObjectFor (pid_t pid)
{
    AudioObjectID obj = kAudioObjectUnknown;
    auto a = addr (kAudioHardwarePropertyTranslatePIDToProcessObject);
    UInt32 size = sizeof (obj);
    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, sizeof (pid), &pid, &size, &obj) != noErr) return kAudioObjectUnknown;
    return obj;
}

struct RawProcess { AudioObjectID obj; pid_t pid; juce::String bundleId; };

std::vector<RawProcess> rawProcesses()
{
    std::vector<RawProcess> v;
    for (auto obj : getArray<AudioObjectID> (kAudioObjectSystemObject, addr (kAudioHardwarePropertyProcessObjectList)))
    {
        RawProcess p { obj, 0, {} };
        getProp (obj, addr (kAudioProcessPropertyPID), p.pid);
        p.bundleId = getString (obj, addr (kAudioProcessPropertyBundleID));
        v.push_back (p);
    }
    return v;
}

bool processRunningOutput (AudioObjectID obj)
{
    UInt32 r = 0;
    return getProp (obj, addr (kAudioProcessPropertyIsRunningOutput), r) && r != 0;
}

/** bundle id b matches an app and its helpers: "com.google.Chrome" takes "com.google.Chrome.helper" too */
bool bundleMatches (const juce::String& processBundle, const juce::String& wanted)
{
    if (processBundle.isEmpty() || wanted.isEmpty()) return false;
    return processBundle == wanted || processBundle.startsWith (wanted + ".");
}

std::atomic<int> gPermission { (int) CoreAudioHost::Permission::Unknown };

// ------------------------------------------------------------------------------------------------ volume
bool scalarAt (AudioObjectID dev, AudioObjectPropertySelector sel, AudioObjectPropertyElement el, Float32& v)
{
    return getProp (dev, addr (sel, kAudioDevicePropertyScopeOutput, el), v);
}

bool settable (AudioObjectID dev, AudioObjectPropertyAddress a)
{
    Boolean s = false;
    return AudioObjectHasProperty (dev, &a) && AudioObjectIsPropertySettable (dev, &a, &s) == noErr && s;
}

/** the elements that carry a volume: the main element, else every channel that has one */
std::vector<AudioObjectPropertyElement> volumeElements (AudioObjectID dev, AudioObjectPropertySelector sel)
{
    if (settable (dev, addr (sel, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain))) return { kAudioObjectPropertyElementMain };
    std::vector<AudioObjectPropertyElement> v;
    const int n = channelCount (dev, kAudioDevicePropertyScopeOutput);
    for (int ch = 1; ch <= n; ++ch)
        if (settable (dev, addr (sel, kAudioDevicePropertyScopeOutput, (UInt32) ch))) v.push_back ((UInt32) ch);
    return v;
}

bool hasVirtualMainVolume (AudioObjectID dev)
{
    auto a = addr (kAudioHardwareServiceDeviceProperty_VirtualMainVolume, kAudioDevicePropertyScopeOutput);
    Boolean s = false;
    return AudioHardwareServiceHasProperty (dev, &a) && AudioHardwareServiceIsPropertySettable (dev, &a, &s) == noErr && s;
}
} // namespace

// ================================================================================================ Impl
struct CoreAudioHost::Impl
{
    explicit Impl (CoreAudioHost& o) : owner (o) {}

    CoreAudioHost& owner;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // ---- the running engine
    struct TapRun
    {
        AudioObjectID id = kAudioObjectUnknown;
        juce::String uuid;
        std::vector<AudioObjectID> objects;   // the processes it was made with (to include / exclude)
        int channels = 0;
    };
    IOConfig cfg;
    IOLayout layout;
    AudioCallback* callback = nullptr;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    std::vector<TapRun> taps;
    std::vector<AudioObjectID> members;       // sub-devices
    std::vector<juce::String> memberUids;
    bool isRunning = false;

    // IO tables (read by the IO thread; only changed while it is stopped)
    std::vector<int> inChanToFlat, outChanToFlat;   // aggregate channel → flat channel (-1: unused)
    std::vector<int> inBufFirst, outBufFirst;       // per buffer: its first aggregate channel
    UInt32 numInBuffers = 0, numOutBuffers = 0;
    int capacity = 0;
    std::vector<float> inStore, outStore;
    std::vector<float*> inPtrs, outPtrs;
    std::vector<const float*> inConstPtrs;
    int tapChannels = 0;                            // flat inputs [0, tapChannels) are the taps
    double hostTicksPerSecond = 1.0e9;

    std::atomic<float> loadValue { 0.0f };
    std::atomic<int> overloadCount { 0 };
    std::atomic<bool> tapSawSignal { false };
    std::atomic<int64_t> tapFrames { 0 };
    std::atomic<bool> layoutBroken { false };

    // permission inference
    dispatch_source_t permTimer = nullptr;
    int64_t lastTapFrames = 0;
    double silentWhilePlaying = 0;

    // ---- listeners
    struct Listener { AudioObjectID obj; AudioObjectPropertyAddress a; dispatch_queue_t queue; AudioObjectPropertyListenerBlock block; };
    std::vector<Listener> systemListeners, deviceListeners, processListeners, runListeners;
    bool devicesPending = false, processesPending = false;

    void listen (std::vector<Listener>& into, AudioObjectID obj, AudioObjectPropertyAddress a, dispatch_queue_t q,
                 void (^fn)(void))
    {
        auto token = alive;
        // the work runs later, never inside the HAL's call: removing a listener from within its own call deadlocks
        const bool onMain = q == dispatch_get_main_queue();
        AudioObjectPropertyListenerBlock block = ^(UInt32, const AudioObjectPropertyAddress*)
        {
            if (! onMain) { if (token->load()) fn(); return; }
            dispatch_async (dispatch_get_main_queue(), ^{ if (token->load()) fn(); });
        };
        if (AudioObjectAddPropertyListenerBlock (obj, &a, q, block) == noErr)
            into.push_back ({ obj, a, q, block });
    }

    void unlistenAll (std::vector<Listener>& v)
    {
        for (auto& l : v) AudioObjectRemovePropertyListenerBlock (l.obj, &l.a, l.queue, l.block);
        v.clear();
    }

    void installSystemListeners()
    {
        auto* q = dispatch_get_main_queue();
        Impl* self = this;
        for (UInt32 sel : std::initializer_list<UInt32> { kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultInputDevice,
                          kAudioHardwarePropertyDefaultOutputDevice, kAudioHardwarePropertyDefaultSystemOutputDevice })
            listen (systemListeners, kAudioObjectSystemObject, addr (sel), q, ^{ self->devicesChanged (true); });
        listen (systemListeners, kAudioObjectSystemObject, addr (kAudioHardwarePropertyProcessObjectList), q, ^{ self->processesChanged (true); });
        refreshDeviceListeners();
        refreshProcessListeners();
    }

    void refreshDeviceListeners()
    {
        unlistenAll (deviceListeners);
        Impl* self = this;
        for (auto dev : getArray<AudioObjectID> (kAudioObjectSystemObject, addr (kAudioHardwarePropertyDevices)))
            for (UInt32 sel : std::initializer_list<UInt32> { kAudioDevicePropertyNominalSampleRate, kAudioDevicePropertyStreamConfiguration })
                for (UInt32 sc : std::initializer_list<UInt32> { kAudioObjectPropertyScopeGlobal, kAudioDevicePropertyScopeInput, kAudioDevicePropertyScopeOutput })
                {
                    if (sel == kAudioDevicePropertyNominalSampleRate && sc != kAudioObjectPropertyScopeGlobal) continue;
                    if (sel == kAudioDevicePropertyStreamConfiguration && sc == kAudioObjectPropertyScopeGlobal) continue;
                    auto a = addr (sel, sc);
                    if (AudioObjectHasProperty (dev, &a))
                        listen (deviceListeners, dev, a, dispatch_get_main_queue(), ^{ self->devicesChanged (false); });
                }
    }

    void refreshProcessListeners()
    {
        unlistenAll (processListeners);
        Impl* self = this;
        for (auto obj : getArray<AudioObjectID> (kAudioObjectSystemObject, addr (kAudioHardwarePropertyProcessObjectList)))
            for (UInt32 sel : std::initializer_list<UInt32> { kAudioProcessPropertyIsRunningInput, kAudioProcessPropertyIsRunningOutput })
                listen (processListeners, obj, addr (sel), dispatch_get_main_queue(), ^{ self->processesChanged (false); });
    }

    // coalesced: one call for a burst of notifications
    void devicesChanged (bool listChanged)
    {
        if (listChanged) refreshDeviceListeners();
        if (isRunning) checkMembersAlive();
        if (devicesPending) return;
        devicesPending = true;
        auto token = alive;
        Impl* self = this;
        dispatch_async (dispatch_get_main_queue(), ^{
            if (! token->load()) return;
            self->devicesPending = false;
            if (self->owner.onDevicesChanged) self->owner.onDevicesChanged();
        });
    }

    void processesChanged (bool listChanged)
    {
        if (listChanged) refreshProcessListeners();
        if (isRunning && listChanged) checkTapTracking();
        if (processesPending) return;
        processesPending = true;
        auto token = alive;
        Impl* self = this;
        dispatch_async (dispatch_get_main_queue(), ^{
            if (! token->load()) return;
            self->processesPending = false;
            if (self->owner.onProcessesChanged) self->owner.onProcessesChanged();
        });
    }

    // ---- a member gone → stop
    void checkMembersAlive()
    {
        if (! isRunning) return;
        UInt32 aliveFlag = 1;
        if (! getProp (aggregate, addr (kAudioDevicePropertyDeviceIsAlive), aliveFlag) || aliveFlag == 0)
            return lost ("the engine's aggregate device went away");
        for (size_t i = 0; i < members.size(); ++i)
        {
            if (deviceForUid (memberUids[i]) == kAudioObjectUnknown)
                return lost ("device gone: " + memberUids[i]);
            aliveFlag = 1;
            if (getProp (members[i], addr (kAudioDevicePropertyDeviceIsAlive), aliveFlag) && aliveFlag == 0)
                return lost ("device gone: " + memberUids[i]);
        }
        if (layoutBroken.load()) return lost ("the aggregate device's streams changed");
    }

    void lost (const juce::String& why)
    {
        if (! isRunning) return;
        stopEngine (true);
        if (owner.onStoppedUnexpectedly) owner.onStoppedUnexpectedly (why);
    }

    // ---- taps
    static std::vector<AudioObjectID> resolveProcesses (const TapSpec& s, const std::vector<RawProcess>& procs)
    {
        std::vector<AudioObjectID> v;
        const pid_t me = getpid();
        for (auto& p : procs)
        {
            if (p.pid == me) continue;
            bool take = std::find (s.pids.begin(), s.pids.end(), p.pid) != s.pids.end();
            for (auto& b : s.bundleIds) take = take || bundleMatches (p.bundleId, b);
            if (take) v.push_back (p.obj);
        }
        return v;
    }

    static CATapDescription* describe (const TapSpec& s, const std::vector<AudioObjectID>& objects)
    {
        NSMutableArray<NSNumber*>* list = [NSMutableArray array];
        for (auto o : objects) [list addObject: @(o)];
        CATapDescription* d = nil;
        if (s.kind == TapSpec::Kind::Processes)
            d = s.mono ? [[CATapDescription alloc] initMonoMixdownOfProcesses: list]
                       : [[CATapDescription alloc] initStereoMixdownOfProcesses: list];
        else
        {
            const AudioObjectID me = processObjectFor (getpid());
            if (me != kAudioObjectUnknown) [list addObject: @(me)];
            d = s.mono ? [[CATapDescription alloc] initMonoGlobalTapButExcludeProcesses: list]
                       : [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: list];
        }
        d.name = toNS (s.name.isNotEmpty() ? "umleitung: " + s.name : juce::String ("umleitung tap"));
        d.privateTap = YES;
        d.muteBehavior = s.muteSource ? CATapMuted : CATapUnmuted;
        if (s.deviceUid.isNotEmpty())
        {
            d.deviceUID = toNS (s.deviceUid);
            d.stream = @0;
        }
        if (@available (macOS 26.0, *))
        {
            // (UMLEITUNG_NO_NATIVE_BUNDLE_TAPS=1: leave it to the rebuild below, as on macOS 14/15: for the host check)
            const bool native = getenv ("UMLEITUNG_NO_NATIVE_BUNDLE_TAPS") == nullptr;
            if (native && s.kind != TapSpec::Kind::System && ! s.bundleIds.empty())
            {
                NSMutableArray<NSString*>* b = [NSMutableArray array];
                for (auto& id : s.bundleIds) [b addObject: toNS (id)];
                d.bundleIDs = b;
                d.processRestoreEnabled = YES;
            }
        }
        return d;
    }

    /** a process of a tapped app started since the tap was made, and the tap didn't take it in by itself: rebuild */
    void checkTapTracking()
    {
        if (! isRunning) return;
        auto procs = rawProcesses();
        bool rebuild = false;
        for (size_t i = 0; i < taps.size() && i < cfg.taps.size(); ++i)
        {
            const auto& s = cfg.taps[i];
            if (s.kind == TapSpec::Kind::System || (s.pids.empty() && s.bundleIds.empty())) continue;
            std::set<AudioObjectID> have (taps[i].objects.begin(), taps[i].objects.end());
            // what the tap holds now (macOS 26+ follows bundle ids by itself)
            CFTypeRef r = nullptr;
            if (getProp (taps[i].id, addr (kAudioTapPropertyDescription), r) && r != nullptr)
            {
                CATapDescription* cur = (__bridge_transfer CATapDescription*) r;
                for (NSNumber* n in cur.processes) have.insert ((AudioObjectID) n.unsignedIntValue);
            }
            for (auto o : resolveProcesses (s, procs))
                if (! have.count (o)) rebuild = true;
        }
        if (! rebuild) return;
        auto c = cfg;
        auto* cb = callback;
        stopEngine (false);
        juce::String err;
        if (! startEngine (c, cb, err, true))
        {
            if (cb != nullptr) cb->stopped();
            if (owner.onStoppedUnexpectedly) owner.onStoppedUnexpectedly ("rebuilding the taps failed: " + err);
        }
    }

    // ---- the IO
    void ioCycle (const AudioBufferList* in, AudioBufferList* out)
    {
        const uint64_t t0 = mach_absolute_time();
        for (UInt32 b = 0; out != nullptr && b < out->mNumberBuffers; ++b)
            if (out->mBuffers[b].mData != nullptr) memset (out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);

        if ((in != nullptr && in->mNumberBuffers != numInBuffers) || (out != nullptr && out->mNumberBuffers != numOutBuffers))
        {
            layoutBroken.store (true);
            return;
        }
        int frames = -1;
        auto framesOf = [] (const AudioBuffer& b) { return b.mNumberChannels > 0 && b.mData != nullptr ? (int) (b.mDataByteSize / (sizeof (float) * b.mNumberChannels)) : -1; };
        for (UInt32 b = 0; out != nullptr && b < out->mNumberBuffers && frames < 0; ++b) frames = framesOf (out->mBuffers[b]);
        for (UInt32 b = 0; in != nullptr && b < in->mNumberBuffers && frames < 0; ++b) frames = framesOf (in->mBuffers[b]);
        if (frames <= 0) return;

        const int totalIn = layout.totalIn, totalOut = layout.totalOut;
        bool tapSignal = false;
        for (int done = 0; done < frames;)
        {
            const int n = std::min (capacity, frames - done);
            for (int ch = 0; ch < totalIn; ++ch) memset (inPtrs[(size_t) ch], 0, sizeof (float) * (size_t) n);
            for (int ch = 0; ch < totalOut; ++ch) memset (outPtrs[(size_t) ch], 0, sizeof (float) * (size_t) n);

            for (UInt32 b = 0; in != nullptr && b < in->mNumberBuffers; ++b)
            {
                const auto& buf = in->mBuffers[b];
                if (buf.mData == nullptr || buf.mNumberChannels == 0) continue;
                const int nch = (int) buf.mNumberChannels;
                const int bufFrames = (int) (buf.mDataByteSize / (sizeof (float) * (size_t) nch));
                const int m = std::min (n, bufFrames - done);
                const auto* src = static_cast<const float*> (buf.mData) + (size_t) done * (size_t) nch;
                for (int c = 0; c < nch; ++c)
                {
                    const int aggCh = inBufFirst[b] + c;
                    const int flat = aggCh < (int) inChanToFlat.size() ? inChanToFlat[(size_t) aggCh] : -1;
                    if (flat < 0) continue;
                    float* dst = inPtrs[(size_t) flat];
                    for (int i = 0; i < m; ++i) dst[i] = src[(size_t) i * (size_t) nch + (size_t) c];
                }
            }
            for (int ch = 0; ch < tapChannels && ! tapSignal; ++ch)
                for (int i = 0; i < n; ++i)
                    if (inPtrs[(size_t) ch][i] != 0.0f) { tapSignal = true; break; }

            if (callback != nullptr)
                callback->process (inConstPtrs.data(), totalIn, outPtrs.data(), totalOut, n);

            for (UInt32 b = 0; out != nullptr && b < out->mNumberBuffers; ++b)
            {
                auto& buf = out->mBuffers[b];
                if (buf.mData == nullptr || buf.mNumberChannels == 0) continue;
                const int nch = (int) buf.mNumberChannels;
                const int bufFrames = (int) (buf.mDataByteSize / (sizeof (float) * (size_t) nch));
                const int m = std::min (n, bufFrames - done);
                auto* dst = static_cast<float*> (buf.mData) + (size_t) done * (size_t) nch;
                for (int c = 0; c < nch; ++c)
                {
                    const int aggCh = outBufFirst[b] + c;
                    const int flat = aggCh < (int) outChanToFlat.size() ? outChanToFlat[(size_t) aggCh] : -1;
                    if (flat < 0) continue;
                    const float* src = outPtrs[(size_t) flat];
                    for (int i = 0; i < m; ++i) dst[(size_t) i * (size_t) nch + (size_t) c] = src[i];
                }
            }
            done += n;
        }
        if (tapChannels > 0)
        {
            if (tapSignal) tapSawSignal.store (true, std::memory_order_relaxed);
            tapFrames.fetch_add (frames, std::memory_order_relaxed);
        }
        const double used = (double) (mach_absolute_time() - t0) / hostTicksPerSecond;
        const double avail = frames / layout.sampleRate;
        const float l = (float) (used / avail);
        const float old = loadValue.load (std::memory_order_relaxed);
        loadValue.store (l > old ? l : old + 0.05f * (l - old), std::memory_order_relaxed);
    }

    // ---- permission inference (message thread, while taps run)
    void permissionTick()
    {
        if (! isRunning || taps.empty()) return;
        if (tapSawSignal.load())
        {
            gPermission = (int) Permission::Granted;
            silentWhilePlaying = 0;
            return;
        }
        const int64_t f = tapFrames.load();
        const double ran = (double) (f - lastTapFrames) / layout.sampleRate;
        lastTapFrames = f;
        if (ran <= 0) return;
        // is anything the taps capture playing?
        bool playing = false;
        const pid_t me = getpid();
        auto procs = rawProcesses();
        for (size_t i = 0; i < taps.size() && ! playing; ++i)
        {
            const auto& s = cfg.taps[i];
            if (s.kind == TapSpec::Kind::Processes)
            {
                for (auto o : resolveProcesses (s, procs)) playing = playing || processRunningOutput (o);
            }
            else
            {
                auto excluded = resolveProcesses (s, procs);
                for (auto& p : procs)
                    if (p.pid != me && std::find (excluded.begin(), excluded.end(), p.obj) == excluded.end())
                        playing = playing || processRunningOutput (p.obj);
            }
        }
        if (! playing) return;
        silentWhilePlaying += ran;
        if (silentWhilePlaying >= 3.0 && gPermission.load() != (int) Permission::Granted)
            gPermission = (int) Permission::Denied;
    }

    // ---- start / stop
    bool startEngine (const IOConfig& c, AudioCallback* cb, juce::String& error, bool keepCallbackPrepared = false);
    void stopEngine (bool notify);
    void destroyParts();
};

void CoreAudioHost::Impl::destroyParts()
{
    unlistenAll (runListeners);
    if (permTimer != nullptr) { dispatch_source_cancel (permTimer); permTimer = nullptr; }
    if (aggregate != kAudioObjectUnknown)
    {
        if (ioProc != nullptr)
        {
            AudioDeviceStop (aggregate, ioProc);
            AudioDeviceDestroyIOProcID (aggregate, ioProc);
        }
        AudioHardwareDestroyAggregateDevice (aggregate);
    }
    ioProc = nullptr;
    aggregate = kAudioObjectUnknown;
    if (@available (macOS 14.2, *))
        for (auto& t : taps) if (t.id != kAudioObjectUnknown) AudioHardwareDestroyProcessTap (t.id);
    taps.clear();
    members.clear();
    memberUids.clear();
}

void CoreAudioHost::Impl::stopEngine (bool notify)
{
    const bool was = isRunning;
    isRunning = false;
    destroyParts();
    auto* cb = callback;
    callback = nullptr;
    if (was && notify && cb != nullptr) cb->stopped();
}

bool CoreAudioHost::Impl::startEngine (const IOConfig& c, AudioCallback* cb, juce::String& error, bool)
{
    error = {};
    auto fail = [&] (const juce::String& e) { error = e; destroyParts(); return false; };
    cfg = c;
    layout = {};
    layoutBroken = false;
    loadValue = 0.0f;
    tapSawSignal = false;
    tapFrames = 0;
    lastTapFrames = 0;
    silentWhilePlaying = 0;

    // ---- devices
    std::vector<juce::String> uids;
    auto addUid = [&] (const juce::String& u) { if (std::find (uids.begin(), uids.end(), u) == uids.end()) uids.push_back (u); };
    juce::String clockUid = c.clockDevice;
    if (clockUid.isEmpty()) clockUid = ! c.outputDevices.empty() ? c.outputDevices.front() : (! c.inputDevices.empty() ? c.inputDevices.front() : juce::String());
    if (clockUid.isNotEmpty()) addUid (clockUid);
    for (auto& u : c.inputDevices) addUid (u);
    for (auto& u : c.outputDevices) addUid (u);
    for (auto& u : uids)
    {
        auto d = deviceForUid (u);
        if (d == kAudioObjectUnknown) return fail ("device not found: " + u);
        members.push_back (d);
        memberUids.push_back (u);
    }
    if (members.empty() && c.taps.empty()) return fail ("nothing to run");

    // ---- taps
    if (! c.taps.empty())
    {
        if (! tapsSupported()) return fail ("process taps need macOS 14.2 or later");
        if (@available (macOS 14.2, *))
        {
            auto procs = rawProcesses();
            for (auto& s : c.taps)
            {
                TapRun t;
                t.objects = resolveProcesses (s, procs);
                CATapDescription* d = describe (s, t.objects);
                OSStatus e = AudioHardwareCreateProcessTap (d, &t.id);
                if (e != noErr || t.id == kAudioObjectUnknown)
                    return fail ("could not make the tap " + s.name + " (error " + juce::String ((int) e) + ")");
                t.uuid = toJuce (d.UUID.UUIDString);
                AudioStreamBasicDescription f {};
                getProp (t.id, addr (kAudioTapPropertyFormat), f);
                t.channels = (int) f.mChannelsPerFrame;
                taps.push_back (t);
            }
        }
    }

    // ---- the aggregate
    NSMutableArray* subs = [NSMutableArray array];
    for (auto& u : memberUids)
    {
        NSMutableDictionary* sd = [NSMutableDictionary dictionaryWithObject: toNS (u) forKey: @kAudioSubDeviceUIDKey];
        if (u != clockUid)
        {
            sd[@kAudioSubDeviceDriftCompensationKey] = @1;
            sd[@kAudioSubDeviceDriftCompensationQualityKey] = @(kAudioAggregateDriftCompensationMaxQuality);
        }
        [subs addObject: sd];
    }
    NSMutableArray* tapList = [NSMutableArray array];
    for (auto& t : taps)
        [tapList addObject: @{ @kAudioSubTapUIDKey: toNS (t.uuid), @kAudioSubTapDriftCompensationKey: @1,
                               @kAudioSubTapDriftCompensationQualityKey: @(kAudioAggregateDriftCompensationMaxQuality) }];
    NSString* aggUid = [@"studio.umleitung.engine." stringByAppendingString: [NSUUID UUID].UUIDString];
    NSMutableDictionary* comp = [@{ @kAudioAggregateDeviceUIDKey: aggUid,
                                    @kAudioAggregateDeviceNameKey: @"umleitung engine",
                                    @kAudioAggregateDeviceIsPrivateKey: @1,
                                    @kAudioAggregateDeviceIsStackedKey: @0,
                                    @kAudioAggregateDeviceSubDeviceListKey: subs,
                                    @kAudioAggregateDeviceTapListKey: tapList } mutableCopy];
    if (clockUid.isNotEmpty())
    {
        comp[@kAudioAggregateDeviceMainSubDeviceKey] = toNS (clockUid);
    }
    OSStatus e = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) comp, &aggregate);
    if (e != noErr || aggregate == kAudioObjectUnknown) { aggregate = kAudioObjectUnknown; return fail ("could not make the aggregate device (error " + juce::String ((int) e) + ")"); }

    // the aggregate fills in its sub-devices asynchronously: wait for all the streams
    auto streamsOf = [] (AudioObjectID dev, AudioObjectPropertyScope sc) { return getArray<AudioObjectID> (dev, addr (kAudioDevicePropertyStreams, sc)); };
    size_t wantIn = taps.size(), wantOut = 0;
    for (auto m : members) { wantIn += streamsOf (m, kAudioDevicePropertyScopeInput).size(); wantOut += streamsOf (m, kAudioDevicePropertyScopeOutput).size(); }
    for (int tries = 0; tries < 200; ++tries)
    {
        if (streamsOf (aggregate, kAudioDevicePropertyScopeInput).size() == wantIn && streamsOf (aggregate, kAudioDevicePropertyScopeOutput).size() == wantOut) break;
        usleep (10000);
    }

    // ---- rate and buffer size
    const double rate = c.sampleRate > 0 ? c.sampleRate : nominalRate (members.empty() ? aggregate : members.front());
    if (rate > 0 && nominalRate (aggregate) != rate)
    {
        Float64 r = rate;
        auto a = addr (kAudioDevicePropertyNominalSampleRate);
        if (AudioObjectSetPropertyData (aggregate, &a, 0, nullptr, sizeof (r), &r) != noErr)
            return fail ("the devices can't run at " + juce::String (rate) + " Hz");
        for (int tries = 0; tries < 300 && nominalRate (aggregate) != rate; ++tries) usleep (10000);
        if (nominalRate (aggregate) != rate) return fail ("the devices didn't change to " + juce::String (rate) + " Hz");
    }
    {
        AudioValueRange range { 0, 0 };
        UInt32 frames = (UInt32) juce::jmax (16, c.bufferSize);
        if (getProp (aggregate, addr (kAudioDevicePropertyBufferFrameSizeRange), range) && range.mMaximum > 0)
            frames = (UInt32) juce::jlimit (range.mMinimum, range.mMaximum, (Float64) frames);
        auto a = addr (kAudioDevicePropertyBufferFrameSize);
        AudioObjectSetPropertyData (aggregate, &a, 0, nullptr, sizeof (frames), &frames);
    }
    UInt32 actualFrames = (UInt32) c.bufferSize;
    getProp (aggregate, addr (kAudioDevicePropertyBufferFrameSize), actualFrames);
    layout.sampleRate = nominalRate (aggregate);
    layout.bufferSize = (int) actualFrames;

    // ---- the stream order → the layout. Rule: active sub-devices in order (all their streams), then the active taps.
    auto activeSubs = getArray<AudioObjectID> (aggregate, addr (kAudioAggregateDevicePropertyActiveSubDeviceList));
    std::vector<AudioObjectID> activeTaps;
    if (@available (macOS 14.2, *))
        activeTaps = getArray<AudioObjectID> (aggregate, addr (kAudioAggregateDevicePropertySubTapList));
    if (activeSubs.size() != members.size() || activeTaps.size() != taps.size())
        return fail ("the aggregate device didn't take every device and tap (" + juce::String ((int) activeSubs.size()) + "/"
                     + juce::String ((int) members.size()) + " devices, " + juce::String ((int) activeTaps.size()) + "/" + juce::String ((int) taps.size()) + " taps)");

    struct Owner { int memberIndex = -1, tapIndex = -1, channels = 0; };
    auto ownersFor = [&] (AudioObjectPropertyScope sc, std::vector<Owner>& owners) -> bool
    {
        auto aggStreams = streamsOf (aggregate, sc);
        std::vector<int> streamCh;
        for (auto st : aggStreams)
        {
            AudioStreamBasicDescription f {};
            getProp (st, addr (kAudioStreamPropertyVirtualFormat), f);
            streamCh.push_back ((int) f.mChannelsPerFrame);
        }
        size_t si = 0;
        for (auto sub : activeSubs)
        {
            auto it = std::find (members.begin(), members.end(), sub);
            if (it == members.end()) return false;
            const int mi = (int) (it - members.begin());
            for (auto st : streamsOf (sub, sc))
            {
                AudioStreamBasicDescription f {};
                getProp (st, addr (kAudioStreamPropertyVirtualFormat), f);
                if (si >= aggStreams.size() || streamCh[si] != (int) f.mChannelsPerFrame) return false;
                owners.push_back ({ mi, -1, streamCh[si++] });
            }
        }
        if (sc == kAudioDevicePropertyScopeInput)
            for (auto tp : activeTaps)
            {
                int ti = -1;
                const auto tuid = getString (tp, addr (kAudioTapPropertyUID));
                for (size_t k = 0; k < taps.size(); ++k) if (taps[k].uuid.equalsIgnoreCase (tuid) || taps[k].id == tp) ti = (int) k;
                if (ti < 0) ti = (int) (owners.size() - (size_t) std::count_if (owners.begin(), owners.end(), [] (const Owner& o) { return o.tapIndex < 0; }));
                if (ti >= (int) taps.size()) ti = -1;
                if (ti < 0 || si >= aggStreams.size()) return false;
                owners.push_back ({ -1, ti, streamCh[si++] });
            }
        return si == aggStreams.size();
    };
    std::vector<Owner> inOwners, outOwners;
    if (! ownersFor (kAudioDevicePropertyScopeInput, inOwners) || ! ownersFor (kAudioDevicePropertyScopeOutput, outOwners))
        return fail ("the aggregate device's streams are not in the expected order");

    // channels per member / tap in the aggregate
    std::vector<int> memberIn (members.size(), 0), memberOut (members.size(), 0), tapCh (taps.size(), 0);
    for (auto& o : inOwners) (o.tapIndex >= 0 ? tapCh[(size_t) o.tapIndex] : memberIn[(size_t) o.memberIndex]) += o.channels;
    for (auto& o : outOwners) memberOut[(size_t) o.memberIndex] += o.channels;
    auto memberOf = [&] (const juce::String& u) { return (int) (std::find (memberUids.begin(), memberUids.end(), u) - memberUids.begin()); };

    // flat positions: taps first, then input devices; output devices
    std::vector<int> tapFlat (taps.size(), 0), memberInFlat (members.size(), -1), memberOutFlat (members.size(), -1);
    int flat = 0;
    for (size_t t = 0; t < taps.size(); ++t)
    {
        IOLayout::Part p;
        p.kind = IOLayout::Part::Kind::Tap;
        p.index = (int) t;
        p.uid = taps[t].uuid;
        p.name = c.taps[t].name.isNotEmpty() ? c.taps[t].name : juce::String ("tap ") + juce::String ((int) t + 1);
        p.firstChannel = flat;
        p.numChannels = tapCh[t];
        tapFlat[t] = flat;
        flat += p.numChannels;
        layout.inputs.push_back (p);
    }
    tapChannels = flat;
    for (size_t i = 0; i < c.inputDevices.size(); ++i)
    {
        const int m = memberOf (c.inputDevices[i]);
        IOLayout::Part p;
        p.kind = IOLayout::Part::Kind::InputDevice;
        p.index = (int) i;
        p.uid = c.inputDevices[i];
        p.name = getString (members[(size_t) m], addr (kAudioObjectPropertyName));
        p.numChannels = memberIn[(size_t) m];
        if (memberInFlat[(size_t) m] < 0) { memberInFlat[(size_t) m] = flat; p.firstChannel = flat; flat += p.numChannels; }
        else p.firstChannel = memberInFlat[(size_t) m];   // the same device listed twice: both parts read the same channels
        layout.inputs.push_back (p);
    }
    layout.totalIn = flat;
    flat = 0;
    for (size_t i = 0; i < c.outputDevices.size(); ++i)
    {
        const int m = memberOf (c.outputDevices[i]);
        IOLayout::Part p;
        p.kind = IOLayout::Part::Kind::OutputDevice;
        p.index = (int) i;
        p.uid = c.outputDevices[i];
        p.name = getString (members[(size_t) m], addr (kAudioObjectPropertyName));
        p.numChannels = memberOut[(size_t) m];
        if (memberOutFlat[(size_t) m] < 0) { memberOutFlat[(size_t) m] = flat; p.firstChannel = flat; flat += p.numChannels; }
        else p.firstChannel = memberOutFlat[(size_t) m];
        layout.outputs.push_back (p);
    }
    layout.totalOut = flat;

    // aggregate channel → flat channel
    auto mapScope = [&] (const std::vector<Owner>& owners, bool input, std::vector<int>& chanToFlat)
    {
        chanToFlat.clear();
        std::vector<int> used (std::max (members.size(), taps.size()) + 1, 0);
        std::vector<int> usedTap (taps.size(), 0);
        for (auto& o : owners)
            for (int k = 0; k < o.channels; ++k)
            {
                int f = -1;
                if (o.tapIndex >= 0) f = tapFlat[(size_t) o.tapIndex] + usedTap[(size_t) o.tapIndex]++;
                else
                {
                    const int base = input ? memberInFlat[(size_t) o.memberIndex] : memberOutFlat[(size_t) o.memberIndex];
                    const int idx = used[(size_t) o.memberIndex]++;
                    f = base >= 0 ? base + idx : -1;
                }
                chanToFlat.push_back (f);
            }
    };
    mapScope (inOwners, true, inChanToFlat);
    mapScope (outOwners, false, outChanToFlat);

    // buffers (the IOProc's view: per stream, or per channel for non-interleaved streams)
    auto bufFirsts = [] (const std::vector<int>& chans, std::vector<int>& firsts)
    {
        firsts.clear();
        int ch = 0;
        for (int n : chans) { firsts.push_back (ch); ch += n; }
        return ch;
    };
    auto inBufs = bufferChannels (aggregate, kAudioDevicePropertyScopeInput);
    auto outBufs = bufferChannels (aggregate, kAudioDevicePropertyScopeOutput);
    if (bufFirsts (inBufs, inBufFirst) != (int) inChanToFlat.size() || bufFirsts (outBufs, outBufFirst) != (int) outChanToFlat.size())
        return fail ("the aggregate device's channel counts don't add up");
    numInBuffers = (UInt32) inBufs.size();
    numOutBuffers = (UInt32) outBufs.size();

    // every stream must be 32 bit float (the HAL's virtual format)
    for (UInt32 sc : std::initializer_list<UInt32> { kAudioDevicePropertyScopeInput, kAudioDevicePropertyScopeOutput })
        for (auto st : streamsOf (aggregate, sc))
        {
            AudioStreamBasicDescription f {};
            getProp (st, addr (kAudioStreamPropertyVirtualFormat), f);
            if (f.mFormatID != kAudioFormatLinearPCM || ! (f.mFormatFlags & kAudioFormatFlagIsFloat) || f.mBitsPerChannel != 32)
                return fail ("a stream isn't 32 bit float");
        }

    // flat buffers
    capacity = (int) std::max<UInt32> (actualFrames, 4096);
    inStore.assign ((size_t) std::max (1, layout.totalIn) * (size_t) capacity, 0.0f);
    outStore.assign ((size_t) std::max (1, layout.totalOut) * (size_t) capacity, 0.0f);
    inPtrs.clear(); outPtrs.clear(); inConstPtrs.clear();
    for (int ch = 0; ch < layout.totalIn; ++ch) { inPtrs.push_back (inStore.data() + (size_t) ch * (size_t) capacity); inConstPtrs.push_back (inPtrs.back()); }
    for (int ch = 0; ch < layout.totalOut; ++ch) outPtrs.push_back (outStore.data() + (size_t) ch * (size_t) capacity);
    if (inConstPtrs.empty()) inConstPtrs.push_back (nullptr);
    if (outPtrs.empty()) outPtrs.push_back (nullptr);

    mach_timebase_info_data_t tb;
    mach_timebase_info (&tb);
    hostTicksPerSecond = 1.0e9 * (double) tb.denom / (double) tb.numer;

    // ---- the callback
    callback = cb;
    if (callback != nullptr) callback->prepare (layout);
    Impl* self = this;
    e = AudioDeviceCreateIOProcIDWithBlock (&ioProc, aggregate, nullptr,
        ^(const AudioTimeStamp*, const AudioBufferList* inData, const AudioTimeStamp*, AudioBufferList* outData, const AudioTimeStamp*)
        { self->ioCycle (inData, outData); });
    if (e != noErr || ioProc == nullptr) { ioProc = nullptr; callback = nullptr; return fail ("could not make the IO callback (error " + juce::String ((int) e) + ")"); }

    // streams nobody uses: off
    auto streamUsage = [&] (AudioObjectPropertyScope sc, const std::vector<Owner>& owners, const std::vector<int>& chanToFlat)
    {
        auto a = addr (kAudioDevicePropertyIOProcStreamUsage, sc);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (aggregate, &a, 0, nullptr, &size) != noErr || size == 0) return;
        std::vector<uint8_t> mem (size);
        auto* u = reinterpret_cast<AudioHardwareIOProcStreamUsage*> (mem.data());
        u->mIOProc = (void*) ioProc;
        if (AudioObjectGetPropertyData (aggregate, &a, 0, nullptr, &size, u) != noErr) return;
        int ch = 0;
        for (UInt32 s = 0; s < u->mNumberStreams && s < owners.size(); ++s)
        {
            bool on = false;
            for (int k = 0; k < owners[s].channels; ++k) on = on || chanToFlat[(size_t) (ch + k)] >= 0;
            ch += owners[s].channels;
            u->mStreamIsOn[s] = on ? 1 : 0;
        }
        AudioObjectSetPropertyData (aggregate, &a, 0, nullptr, size, u);
    };
    streamUsage (kAudioDevicePropertyScopeInput, inOwners, inChanToFlat);
    streamUsage (kAudioDevicePropertyScopeOutput, outOwners, outChanToFlat);

    // ---- listeners of the running engine
    auto* main = dispatch_get_main_queue();
    listen (runListeners, aggregate, addr (kAudioDevicePropertyDeviceIsAlive), main, ^{ self->checkMembersAlive(); });
    for (auto m : members)
        listen (runListeners, m, addr (kAudioDevicePropertyDeviceIsAlive), main, ^{ self->checkMembersAlive(); });
    listen (runListeners, aggregate, addr (kAudioDeviceProcessorOverload), dispatch_get_global_queue (QOS_CLASS_UTILITY, 0),
            ^{ self->overloadCount.fetch_add (1); });

    e = AudioDeviceStart (aggregate, ioProc);
    if (e != noErr) { callback = nullptr; return fail ("could not start the aggregate device (error " + juce::String ((int) e) + ")"); }
    isRunning = true;

    if (! taps.empty())
    {
        permTimer = dispatch_source_create (DISPATCH_SOURCE_TYPE_TIMER, 0, 0, main);
        dispatch_source_set_timer (permTimer, dispatch_time (DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), 500 * NSEC_PER_MSEC, 50 * NSEC_PER_MSEC);
        auto token = alive;
        dispatch_source_set_event_handler (permTimer, ^{ if (token->load()) self->permissionTick(); });
        dispatch_resume (permTimer);
    }
    return true;
}

// ================================================================================================ CoreAudioHost
CoreAudioHost::CoreAudioHost() : impl (std::make_unique<Impl> (*this))
{
    impl->installSystemListeners();
}

CoreAudioHost::~CoreAudioHost()
{
    impl->stopEngine (true);
    impl->alive->store (false);
    impl->unlistenAll (impl->systemListeners);
    impl->unlistenAll (impl->deviceListeners);
    impl->unlistenAll (impl->processListeners);
}

std::vector<DeviceInfo> CoreAudioHost::devices()
{
    std::vector<DeviceInfo> v;
    const auto defIn = defaultDevice (kAudioHardwarePropertyDefaultInputDevice);
    const auto defOut = defaultDevice (kAudioHardwarePropertyDefaultOutputDevice);
    const auto defSys = defaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice);
    for (auto dev : getArray<AudioObjectID> (kAudioObjectSystemObject, addr (kAudioHardwarePropertyDevices)))
    {
        DeviceInfo d;
        d.id = dev;
        d.uid = uidOf (dev);
        d.name = getString (dev, addr (kAudioObjectPropertyName));
        d.manufacturer = getString (dev, addr (kAudioObjectPropertyManufacturer));
        d.inChannels = channelCount (dev, kAudioDevicePropertyScopeInput);
        d.outChannels = channelCount (dev, kAudioDevicePropertyScopeOutput);
        d.sampleRate = nominalRate (dev);
        d.sampleRates = availableRates (dev);
        UInt32 t = 0;
        getProp (dev, addr (kAudioDevicePropertyTransportType), t);
        d.transport = transportName (t);
        AudioClassID cls = 0;
        getProp (dev, addr (kAudioObjectPropertyClass), cls);
        d.isAggregate = t == kAudioDeviceTransportTypeAggregate || cls == kAudioAggregateDeviceClassID;
        UInt32 hidden = 0;
        getProp (dev, addr (kAudioDevicePropertyIsHidden), hidden);
        d.isHidden = hidden != 0;
        d.isDefaultInput = dev == defIn;
        d.isDefaultOutput = dev == defOut;
        d.isDefaultSystemOutput = dev == defSys;
        d.isOurs = d.manufacturer.equalsIgnoreCase ("umleitung");
        d.hasVolume = d.outChannels > 0 && (hasVirtualMainVolume (dev) || ! volumeElements (dev, kAudioDevicePropertyVolumeScalar).empty());
        v.push_back (d);
    }
    return v;
}

std::vector<ProcessInfo> CoreAudioHost::audioProcesses()
{
    std::vector<ProcessInfo> v;
    const pid_t me = getpid();
    for (auto& p : rawProcesses())
    {
        if (p.pid == me || p.pid <= 0) continue;
        ProcessInfo i;
        i.pid = p.pid;
        i.objectId = p.obj;
        i.bundleId = p.bundleId;
        UInt32 r = 0;
        i.recording = getProp (p.obj, addr (kAudioProcessPropertyIsRunningInput), r) && r != 0;
        r = 0;
        i.playing = getProp (p.obj, addr (kAudioProcessPropertyIsRunningOutput), r) && r != 0;
        char path[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_pidpath (p.pid, path, sizeof (path)) > 0) i.path = juce::String::fromUTF8 (path);
        @autoreleasepool
        {
            NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: p.pid];
            if (app != nil && app.localizedName.length > 0) i.name = toJuce (app.localizedName);
        }
        if (i.name.isEmpty()) i.name = i.path.isNotEmpty() ? i.path.fromLastOccurrenceOf ("/", false, false) : i.bundleId;
        if (i.name.isEmpty()) i.name = "process " + juce::String (p.pid);
        v.push_back (i);
    }
    return v;
}

juce::String CoreAudioHost::defaultOutputUid()       { return uidOf (defaultDevice (kAudioHardwarePropertyDefaultOutputDevice)); }
juce::String CoreAudioHost::defaultInputUid()        { return uidOf (defaultDevice (kAudioHardwarePropertyDefaultInputDevice)); }
juce::String CoreAudioHost::defaultSystemOutputUid() { return uidOf (defaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice)); }
bool CoreAudioHost::setDefaultOutput (const juce::String& uid)       { return setDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice, uid); }
bool CoreAudioHost::setDefaultInput (const juce::String& uid)        { return setDefaultDevice (kAudioHardwarePropertyDefaultInputDevice, uid); }
bool CoreAudioHost::setDefaultSystemOutput (const juce::String& uid) { return setDefaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice, uid); }

float CoreAudioHost::deviceVolume (const juce::String& uid)
{
    const auto dev = deviceForUid (uid);
    if (dev == kAudioObjectUnknown) return 0.0f;
    if (hasVirtualMainVolume (dev))
    {
        Float32 v = 0;
        UInt32 size = sizeof (v);
        auto a = addr (kAudioHardwareServiceDeviceProperty_VirtualMainVolume, kAudioDevicePropertyScopeOutput);
        if (AudioHardwareServiceGetPropertyData (dev, &a, 0, nullptr, &size, &v) == noErr) return v;
    }
    auto els = volumeElements (dev, kAudioDevicePropertyVolumeScalar);
    if (els.empty()) return 1.0f;   // no volume: full
    float sum = 0;
    for (auto el : els) { Float32 v = 0; scalarAt (dev, kAudioDevicePropertyVolumeScalar, el, v); sum += v; }
    return sum / (float) els.size();
}

bool CoreAudioHost::setDeviceVolume (const juce::String& uid, float v01)
{
    const auto dev = deviceForUid (uid);
    if (dev == kAudioObjectUnknown) return false;
    Float32 v = juce::jlimit (0.0f, 1.0f, v01);
    if (hasVirtualMainVolume (dev))
    {
        auto a = addr (kAudioHardwareServiceDeviceProperty_VirtualMainVolume, kAudioDevicePropertyScopeOutput);
        if (AudioHardwareServiceSetPropertyData (dev, &a, 0, nullptr, sizeof (v), &v) == noErr) return true;
    }
    auto els = volumeElements (dev, kAudioDevicePropertyVolumeScalar);
    bool ok = ! els.empty();
    for (auto el : els)
    {
        auto a = addr (kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput, el);
        ok = AudioObjectSetPropertyData (dev, &a, 0, nullptr, sizeof (v), &v) == noErr && ok;
    }
    return ok;
}

bool CoreAudioHost::deviceMuted (const juce::String& uid)
{
    const auto dev = deviceForUid (uid);
    if (dev == kAudioObjectUnknown) return false;
    auto els = volumeElements (dev, kAudioDevicePropertyMute);
    if (els.empty()) return false;
    for (auto el : els)
    {
        UInt32 m = 0;
        if (! getProp (dev, addr (kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput, el), m) || m == 0) return false;
    }
    return true;
}

bool CoreAudioHost::setDeviceMuted (const juce::String& uid, bool muted)
{
    const auto dev = deviceForUid (uid);
    if (dev == kAudioObjectUnknown) return false;
    auto els = volumeElements (dev, kAudioDevicePropertyMute);
    bool ok = ! els.empty();
    UInt32 m = muted ? 1 : 0;
    for (auto el : els)
    {
        auto a = addr (kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput, el);
        ok = AudioObjectSetPropertyData (dev, &a, 0, nullptr, sizeof (m), &m) == noErr && ok;
    }
    return ok;
}

bool CoreAudioHost::tapsSupported()
{
    if (@available (macOS 14.2, *)) return true;
    return false;
}

CoreAudioHost::Permission CoreAudioHost::capturePermission() { return (Permission) gPermission.load(); }

bool CoreAudioHost::start (const IOConfig& c, AudioCallback* cb, juce::String& error)
{
    impl->stopEngine (true);
    return impl->startEngine (c, cb, error);
}

void CoreAudioHost::stop()                       { impl->stopEngine (true); }
bool CoreAudioHost::running() const              { return impl->isRunning; }
const IOLayout& CoreAudioHost::layout() const    { return impl->layout; }
const IOConfig& CoreAudioHost::config() const    { return impl->cfg; }
float CoreAudioHost::load() const                { return impl->loadValue.load(); }
int CoreAudioHost::overloads() const             { return impl->overloadCount.load(); }
} // namespace um

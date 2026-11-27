// umleitung driver test harness: loads the built umleitung.driver in-process with CFPlugIn, plays the host
// (coreaudiod) with a fake AudioServerPlugInHostInterface (records PropertiesChanged, keeps storage in memory,
// performs configuration changes at once) and checks the device list property, persistence, every object's
// properties and the IO loopback. Nothing is installed; coreaudiod is never touched.

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>

#ifndef UM_DRIVER_BUNDLE
#error "UM_DRIVER_BUNDLE: the path of the built umleitung.driver"
#endif

enum { kUmPropertyDevices = 'umdv', kUmPropertyVersion = 'umvr', kRing = 16384 };

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                                        \
    do { ++gChecks; if (! (cond)) { ++gFailures; printf ("FAIL %s:%d: ", __FILE__, __LINE__);    \
         printf (__VA_ARGS__); printf ("\n"); } } while (0)

// ---------------------------------------------------------------------------------------------------------------------
// the fake host

static AudioServerPlugInDriverRef gDriver;
static CFMutableDictionaryRef     gStorage;
typedef struct { AudioObjectID object; AudioObjectPropertySelector selector; } Change;
static Change gChanges[4096];
static int    gNumChanges = 0;
static int    gConfigRequests = 0;

static OSStatus host_PropertiesChanged (AudioServerPlugInHostRef h, AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress* a)
{
    for (UInt32 i = 0; i < n && gNumChanges < 4096; ++i) { gChanges[gNumChanges].object = obj; gChanges[gNumChanges].selector = a[i].mSelector; ++gNumChanges; }
    return noErr;
}
static OSStatus host_CopyFromStorage (AudioServerPlugInHostRef h, CFStringRef key, CFPropertyListRef* out)
{
    CFPropertyListRef v = CFDictionaryGetValue (gStorage, key);
    // as coreaudiod would: a deep copy, so the driver can't hold on to ours
    *out = v ? CFPropertyListCreateDeepCopy (NULL, v, kCFPropertyListImmutable) : NULL;
    return v ? noErr : kAudioHardwareUnknownPropertyError;
}
static OSStatus host_WriteToStorage (AudioServerPlugInHostRef h, CFStringRef key, CFPropertyListRef data)
{
    CFPropertyListRef copy = CFPropertyListCreateDeepCopy (NULL, data, kCFPropertyListImmutable);
    CFDictionarySetValue (gStorage, key, copy);
    CFRelease (copy);
    return noErr;
}
static OSStatus host_DeleteFromStorage (AudioServerPlugInHostRef h, CFStringRef key)
{
    CFDictionaryRemoveValue (gStorage, key);
    return noErr;
}
static OSStatus host_RequestDeviceConfigurationChange (AudioServerPlugInHostRef h, AudioObjectID device, UInt64 action, void* info)
{
    ++gConfigRequests;
    return (*gDriver)->PerformDeviceConfigurationChange (gDriver, device, action, info);   // the HAL: stop IO, perform, restart
}

static AudioServerPlugInHostInterface gHostInterface =
{
    host_PropertiesChanged, host_CopyFromStorage, host_WriteToStorage, host_DeleteFromStorage, host_RequestDeviceConfigurationChange
};

static Boolean changed (AudioObjectID obj, AudioObjectPropertySelector sel)
{
    for (int i = 0; i < gNumChanges; ++i) if (gChanges[i].object == obj && gChanges[i].selector == sel) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// property helpers

static AudioObjectPropertyAddress addr (AudioObjectPropertySelector s, AudioObjectPropertyScope sc, AudioObjectPropertyElement e)
{
    AudioObjectPropertyAddress a = { s, sc, e };
    return a;
}
#define G(sel) addr ((sel), kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain)

static OSStatus get (AudioObjectID obj, AudioObjectPropertyAddress a, UInt32 size, void* data, UInt32* outSize)
{
    UInt32 got = 0;
    OSStatus err = (*gDriver)->GetPropertyData (gDriver, obj, 0, &a, 0, NULL, size, &got, data);
    if (outSize) *outSize = got;
    return err;
}
static UInt32 getU32 (AudioObjectID obj, AudioObjectPropertyAddress a)
{
    UInt32 v = 0xdeadbeef;
    get (obj, a, sizeof (v), &v, NULL);
    return v;
}
static Float64 getF64 (AudioObjectID obj, AudioObjectPropertyAddress a)
{
    Float64 v = -1;
    get (obj, a, sizeof (v), &v, NULL);
    return v;
}
static Float32 getF32 (AudioObjectID obj, AudioObjectPropertyAddress a)
{
    Float32 v = -1;
    get (obj, a, sizeof (v), &v, NULL);
    return v;
}
static Boolean cfStringEquals (CFTypeRef s, const char* c)
{
    if (s == NULL || CFGetTypeID (s) != CFStringGetTypeID ()) return false;
    char buf[512];
    return CFStringGetCString ((CFStringRef) s, buf, sizeof (buf), kCFStringEncodingUTF8) && strcmp (buf, c) == 0;
}
static Boolean stringIs (AudioObjectID obj, AudioObjectPropertyAddress a, const char* expect)
{
    CFStringRef s = NULL;
    if (get (obj, a, sizeof (s), &s, NULL) != noErr || s == NULL) return false;
    Boolean eq = cfStringEquals (s, expect);
    CFRelease (s);
    return eq;
}
static UInt32 idList (AudioObjectID obj, AudioObjectPropertyAddress a, AudioObjectID* ids, UInt32 max)
{
    UInt32 size = 0;
    if ((*gDriver)->GetPropertyDataSize (gDriver, obj, 0, &a, 0, NULL, &size) != noErr) return 0xffffffff;
    UInt32 got = 0;
    if ((*gDriver)->GetPropertyData (gDriver, obj, 0, &a, 0, NULL, max * sizeof (AudioObjectID), &got, ids) != noErr) return 0xffffffff;
    CFIndex n = got / sizeof (AudioObjectID);
    if (got != size && size <= max * sizeof (AudioObjectID)) return 0xfffffffe;
    return (UInt32) n;
}
static UInt32 deviceList (AudioObjectID* ids) { return idList (kAudioObjectPlugInObject, G (kAudioPlugInPropertyDeviceList), ids, 256); }

static AudioObjectID translate (const char* uid)
{
    CFStringRef s = CFStringCreateWithCString (NULL, uid, kCFStringEncodingUTF8);
    AudioObjectPropertyAddress a = G (kAudioPlugInPropertyTranslateUIDToDevice);
    AudioObjectID id = 0xffffffff; UInt32 got = 0;
    (*gDriver)->GetPropertyData (gDriver, kAudioObjectPlugInObject, 0, &a, sizeof (s), &s, sizeof (id), &got, &id);
    CFRelease (s);
    return id;
}

static CFArrayRef getDevicesProperty (void)
{
    CFArrayRef arr = NULL; UInt32 got = 0;
    AudioObjectPropertyAddress a = G (kUmPropertyDevices);
    OSStatus err = (*gDriver)->GetPropertyData (gDriver, kAudioObjectPlugInObject, 0, &a, 0, NULL, sizeof (arr), &got, &arr);
    return err == noErr ? arr : NULL;
}

static CFDictionaryRef makeSpec (const char* uid, const char* name, int channels, Boolean hidden)
{
    CFMutableDictionaryRef d = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (uid) { CFStringRef s = CFStringCreateWithCString (NULL, uid, kCFStringEncodingUTF8); CFDictionarySetValue (d, CFSTR ("uid"), s); CFRelease (s); }
    if (name) { CFStringRef s = CFStringCreateWithCString (NULL, name, kCFStringEncodingUTF8); CFDictionarySetValue (d, CFSTR ("name"), s); CFRelease (s); }
    CFNumberRef n = CFNumberCreate (NULL, kCFNumberIntType, &channels);
    CFDictionarySetValue (d, CFSTR ("channels"), n); CFRelease (n);
    CFDictionarySetValue (d, CFSTR ("hidden"), hidden ? kCFBooleanTrue : kCFBooleanFalse);
    return d;
}

static OSStatus setDevices (CFDictionaryRef* specs, int n)
{
    CFArrayRef arr = CFArrayCreate (NULL, (const void**) specs, n, &kCFTypeArrayCallBacks);
    AudioObjectPropertyAddress a = G (kUmPropertyDevices);
    OSStatus err = (*gDriver)->SetPropertyData (gDriver, kAudioObjectPlugInObject, 0, &a, 0, NULL, sizeof (arr), &arr);
    CFRelease (arr);
    return err;
}
static OSStatus setPlist (CFPropertyListRef p)
{
    AudioObjectPropertyAddress a = G (kUmPropertyDevices);
    return (*gDriver)->SetPropertyData (gDriver, kAudioObjectPlugInObject, 0, &a, 0, NULL, sizeof (p), &p);
}

typedef struct { AudioObjectID device, in, out, volume, mute; } Objects;

static Objects objectsOf (AudioObjectID dev)
{
    Objects o = { dev, 0, 0, 0, 0 };
    AudioObjectID ids[4];
    if (idList (dev, addr (kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, 0), ids, 4) == 1) o.in = ids[0];
    if (idList (dev, addr (kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, 0), ids, 4) == 1) o.out = ids[0];
    UInt32 n = idList (dev, G (kAudioObjectPropertyControlList), ids, 4);
    for (UInt32 i = 0; i < n && n < 0xfffffff0; ++i)
    {
        UInt32 cls = getU32 (ids[i], G (kAudioObjectPropertyClass));
        if (cls == kAudioVolumeControlClassID) o.volume = ids[i];
        if (cls == kAudioMuteControlClassID) o.mute = ids[i];
    }
    return o;
}

// checks a device's identity, streams and formats
static void checkDevice (AudioObjectID dev, const char* uid, const char* name, UInt32 channels, Boolean hidden, Float64 rate)
{
    CHECK (getU32 (dev, G (kAudioObjectPropertyClass)) == kAudioDeviceClassID, "%s: class", uid);
    CHECK (stringIs (dev, G (kAudioDevicePropertyDeviceUID), uid), "%s: uid", uid);
    CHECK (stringIs (dev, G (kAudioObjectPropertyName), name), "%s: name %s", uid, name);
    CHECK (stringIs (dev, G (kAudioObjectPropertyManufacturer), "umleitung"), "%s: manufacturer", uid);
    CHECK (stringIs (dev, G (kAudioObjectPropertyModelName), "umleitung virtual device"), "%s: model", uid);
    CHECK (getU32 (dev, G (kAudioDevicePropertyIsHidden)) == (hidden ? 1u : 0u), "%s: hidden", uid);
    CHECK (getU32 (dev, G (kAudioDevicePropertyDeviceCanBeDefaultDevice)) == (hidden ? 0u : 1u), "%s: can be default", uid);
    CHECK (getU32 (dev, G (kAudioDevicePropertyTransportType)) == kAudioDeviceTransportTypeVirtual, "%s: transport", uid);
    CHECK (getF64 (dev, G (kAudioDevicePropertyNominalSampleRate)) == rate, "%s: rate %g", uid, getF64 (dev, G (kAudioDevicePropertyNominalSampleRate)));
    CHECK (translate (uid) == dev, "%s: translate UID", uid);
    Objects o = objectsOf (dev);
    CHECK (o.in && o.out && o.volume && o.mute, "%s: streams + controls (%u %u %u %u)", uid, o.in, o.out, o.volume, o.mute);
    for (int s = 0; s < 2; ++s)
    {
        AudioObjectID st = s == 0 ? o.in : o.out;
        CHECK (getU32 (st, G (kAudioStreamPropertyDirection)) == (s == 0 ? 1u : 0u), "%s: direction", uid);
        CHECK (getU32 (st, G (kAudioObjectPropertyOwner)) == dev, "%s: stream owner", uid);
        AudioObjectPropertySelector fmts[2] = { kAudioStreamPropertyVirtualFormat, kAudioStreamPropertyPhysicalFormat };
        for (int f = 0; f < 2; ++f)
        {
            AudioStreamBasicDescription asbd; memset (&asbd, 0, sizeof (asbd)); UInt32 got = 0;
            CHECK (get (st, G (fmts[f]), sizeof (asbd), &asbd, &got) == noErr && got == sizeof (asbd), "%s: format", uid);
            CHECK (asbd.mChannelsPerFrame == channels && asbd.mSampleRate == rate && asbd.mFormatID == kAudioFormatLinearPCM
                   && (asbd.mFormatFlags & kAudioFormatFlagIsFloat) && asbd.mBitsPerChannel == 32 && asbd.mBytesPerFrame == 4 * channels,
                   "%s: format %u ch @ %g", uid, asbd.mChannelsPerFrame, asbd.mSampleRate);
        }
        AudioStreamRangedDescription r[8]; UInt32 got = 0;
        get (st, G (kAudioStreamPropertyAvailablePhysicalFormats), sizeof (r), r, &got);
        CHECK (got == 6 * sizeof (AudioStreamRangedDescription) && r[5].mFormat.mSampleRate == 192000 && r[0].mFormat.mChannelsPerFrame == channels, "%s: available formats", uid);
    }
    // channel names: element 1…channels
    CHECK ((*gDriver)->HasProperty (gDriver, dev, 0, &(AudioObjectPropertyAddress) { kAudioObjectPropertyElementName, kAudioObjectPropertyScopeOutput, channels }), "%s: last channel name", uid);
    CHECK (! (*gDriver)->HasProperty (gDriver, dev, 0, &(AudioObjectPropertyAddress) { kAudioObjectPropertyElementName, kAudioObjectPropertyScopeOutput, channels + 1 }), "%s: no name past the last channel", uid);
}

static AudioObjectID deviceWithUID (const char* uid) { return translate (uid); }

// every selector the HAL may ask about, on every object, every scope: HasProperty ⇔ size ⇔ data agree
static void sweepProperties (AudioObjectID obj, const char* what)
{
    static const AudioObjectPropertySelector sels[] =
    {
        kAudioObjectPropertyBaseClass, kAudioObjectPropertyClass, kAudioObjectPropertyOwner, kAudioObjectPropertyName,
        kAudioObjectPropertyModelName, kAudioObjectPropertyManufacturer, kAudioObjectPropertyOwnedObjects,
        kAudioObjectPropertyCustomPropertyInfoList, kAudioObjectPropertyControlList, kAudioObjectPropertyElementName,
        kAudioPlugInPropertyBoxList, kAudioPlugInPropertyDeviceList, kAudioPlugInPropertyClockDeviceList,
        kAudioPlugInPropertyResourceBundle, kAudioPlugInPropertyTranslateUIDToDevice, kAudioPlugInPropertyTranslateUIDToBox,
        kAudioDevicePropertyDeviceUID, kAudioDevicePropertyModelUID, kAudioDevicePropertyTransportType,
        kAudioDevicePropertyRelatedDevices, kAudioDevicePropertyClockDomain, kAudioDevicePropertyDeviceIsAlive,
        kAudioDevicePropertyDeviceIsRunning, kAudioDevicePropertyDeviceCanBeDefaultDevice,
        kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioDevicePropertyLatency, kAudioDevicePropertyStreams,
        kAudioDevicePropertySafetyOffset, kAudioDevicePropertyNominalSampleRate, kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioDevicePropertyIsHidden, kAudioDevicePropertyPreferredChannelsForStereo, kAudioDevicePropertyPreferredChannelLayout,
        kAudioDevicePropertyZeroTimeStampPeriod, kAudioDevicePropertyClockIsStable, kAudioDevicePropertyClockAlgorithm,
        kAudioDevicePropertyIcon,
        kAudioStreamPropertyIsActive, kAudioStreamPropertyDirection, kAudioStreamPropertyTerminalType,
        kAudioStreamPropertyStartingChannel, kAudioStreamPropertyLatency, kAudioStreamPropertyVirtualFormat,
        kAudioStreamPropertyAvailableVirtualFormats, kAudioStreamPropertyPhysicalFormat, kAudioStreamPropertyAvailablePhysicalFormats,
        kAudioControlPropertyScope, kAudioControlPropertyElement, kAudioLevelControlPropertyScalarValue,
        kAudioLevelControlPropertyDecibelValue, kAudioLevelControlPropertyDecibelRange,
        kAudioLevelControlPropertyConvertScalarToDecibels, kAudioLevelControlPropertyConvertDecibelsToScalar,
        kAudioBooleanControlPropertyValue, kUmPropertyDevices, kUmPropertyVersion, 'zzzz'
    };
    static const AudioObjectPropertyScope scopes[] = { kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyScopeInput, kAudioObjectPropertyScopeOutput };
    int has = 0;
    for (size_t i = 0; i < sizeof (sels) / sizeof (sels[0]); ++i)
        for (size_t s = 0; s < 3; ++s)
            for (UInt32 e = 0; e <= 1; ++e)
            {
                AudioObjectPropertyAddress a = { sels[i], scopes[s], e };
                Boolean h = (*gDriver)->HasProperty (gDriver, obj, 0, &a);
                UInt32 size = 0;
                CFStringRef uidq = CFSTR ("nope");
                const void* qual = sels[i] == kAudioPlugInPropertyTranslateUIDToDevice || sels[i] == kAudioPlugInPropertyTranslateUIDToBox ? &uidq : NULL;
                UInt32 qualSize = qual ? sizeof (uidq) : 0;
                OSStatus e1 = (*gDriver)->GetPropertyDataSize (gDriver, obj, 0, &a, qualSize, qual, &size);
                CHECK ((e1 == noErr) == h, "%s '%.4s' scope %.4s el %u: HasProperty %d but size err %d", what, (char*) &(UInt32) { CFSwapInt32HostToBig (sels[i]) }, (char*) &(UInt32) { CFSwapInt32HostToBig (scopes[s]) }, e, h, (int) e1);
                if (! h) continue;
                ++has;
                Boolean settable = 2;
                CHECK ((*gDriver)->IsPropertySettable (gDriver, obj, 0, &a, &settable) == noErr && settable <= 1, "%s: settable", what);
                UInt8* buf = (UInt8*) calloc (1, size + 16);
                if (sels[i] == kAudioLevelControlPropertyConvertScalarToDecibels) *(Float32*) buf = 0.5f;
                if (sels[i] == kAudioLevelControlPropertyConvertDecibelsToScalar) *(Float32*) buf = -12.0f;
                UInt32 got = 0;
                OSStatus e2 = (*gDriver)->GetPropertyData (gDriver, obj, 0, &a, qualSize, qual, size, &got, buf);
                CHECK (e2 == noErr && got == size, "%s '%.4s': data err %d, %u of %u bytes", what, (char*) &(UInt32) { CFSwapInt32HostToBig (sels[i]) }, (int) e2, got, size);
                if (e2 == noErr && size == sizeof (CFTypeRef) && (sels[i] == kAudioObjectPropertyName || sels[i] == kAudioObjectPropertyManufacturer
                    || sels[i] == kAudioObjectPropertyModelName || sels[i] == kAudioDevicePropertyDeviceUID || sels[i] == kAudioDevicePropertyModelUID
                    || sels[i] == kAudioPlugInPropertyResourceBundle || sels[i] == kAudioObjectPropertyElementName
                    || sels[i] == kUmPropertyDevices || sels[i] == kUmPropertyVersion))
                {
                    CFTypeRef cf = *(CFTypeRef*) buf;
                    CHECK (cf != NULL, "%s: CF value", what);
                    if (cf) CFRelease (cf);
                }
                // too small a buffer for a fixed-size value is refused, not overrun
                if (size == 4 || size == 8)
                {
                    UInt32 tiny = 0; got = 0;
                    OSStatus e3 = (*gDriver)->GetPropertyData (gDriver, obj, 0, &a, qualSize, qual, 2, &got, &tiny);
                    CHECK (e3 != noErr || got <= 2, "%s: small buffer", what);
                }
                free (buf);
            }
    CHECK (has > 5, "%s: has %d properties", what, has);
    // an unknown object
    AudioObjectPropertyAddress a = G (kAudioObjectPropertyClass);
    CHECK (! (*gDriver)->HasProperty (gDriver, 999999, 0, &a), "unknown object");
}

// ---------------------------------------------------------------------------------------------------------------------
// IO helpers

static OSStatus io (AudioObjectID dev, AudioObjectID stream, UInt32 op, Float64 sampleTime, UInt32 frames, Float32* buf)
{
    AudioServerPlugInIOCycleInfo info; memset (&info, 0, sizeof (info));
    info.mInputTime.mSampleTime = sampleTime;
    info.mOutputTime.mSampleTime = sampleTime;
    info.mInputTime.mFlags = info.mOutputTime.mFlags = kAudioTimeStampSampleTimeValid;
    (*gDriver)->BeginIOOperation (gDriver, dev, 1, op, frames, &info);
    OSStatus err = (*gDriver)->DoIOOperation (gDriver, dev, stream, 1, op, frames, &info, buf, NULL);
    (*gDriver)->EndIOOperation (gDriver, dev, 1, op, frames, &info);
    return err;
}
static Float32 sig (SInt64 t, UInt32 c) { return (Float32) (((t * 7 + c * 131) % 2001) - 1000) / 1000.0f; }
static void fill (Float32* b, SInt64 t0, UInt32 frames, UInt32 ch, Float32 scale)
{
    for (UInt32 i = 0; i < frames; ++i) for (UInt32 c = 0; c < ch; ++c) b[i * ch + c] = sig (t0 + i, c) * scale;
}
// max |error| between b and signal × scale (scale 0: expects silence)
static Float32 compare (const Float32* b, SInt64 t0, UInt32 frames, UInt32 ch, Float32 scale)
{
    Float32 worst = 0;
    for (UInt32 i = 0; i < frames; ++i) for (UInt32 c = 0; c < ch; ++c)
    {
        Float32 e = fabsf (b[i * ch + c] - sig (t0 + i, c) * scale);
        if (e > worst) worst = e;
    }
    return worst;
}

static void testIO (AudioObjectID dev, UInt32 ch, const char* what)
{
    Objects o = objectsOf (dev);
    const UInt32 frames = 512;
    Float32* w = (Float32*) malloc (sizeof (Float32) * frames * ch);
    Float32* r = (Float32*) malloc (sizeof (Float32) * frames * ch);

    CHECK ((*gDriver)->StartIO (gDriver, dev, 1) == noErr, "%s: StartIO", what);
    CHECK (getU32 (dev, G (kAudioDevicePropertyDeviceIsRunning)) == 1, "%s: running", what);
    Float64 st = -1; UInt64 ht = 0, seed = 0;
    CHECK ((*gDriver)->GetZeroTimeStamp (gDriver, dev, 1, &st, &ht, &seed) == noErr && st == 0 && ht > 0 && seed > 0, "%s: zero time stamp", what);
    Boolean will = 0, inPlace = 0;
    (*gDriver)->WillDoIOOperation (gDriver, dev, 1, kAudioServerPlugInIOOperationReadInput, &will, &inPlace);
    CHECK (will && inPlace, "%s: will read input", what);
    (*gDriver)->WillDoIOOperation (gDriver, dev, 1, kAudioServerPlugInIOOperationWriteMix, &will, &inPlace);
    CHECK (will && inPlace, "%s: will write mix", what);
    (*gDriver)->WillDoIOOperation (gDriver, dev, 1, kAudioServerPlugInIOOperationProcessMix, &will, &inPlace);
    CHECK (! will, "%s: won't process mix", what);

    // nothing written yet: silence
    for (UInt32 i = 0; i < frames * ch; ++i) r[i] = 99;
    CHECK (io (dev, o.in, kAudioServerPlugInIOOperationReadInput, 0, frames, r) == noErr && compare (r, 0, frames, ch, 0) == 0, "%s: silence before writing", what);

    // the HAL's cycle: output written ahead of input (output at t + 2 blocks, input at t), across the ring's wrap
    Float32 worst = 0;
    const SInt64 lead = 2 * frames;
    for (SInt64 t = 0; t < 3 * kRing; t += frames)
    {
        fill (w, t + lead, frames, ch, 1.0f);
        io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) (t + lead), frames, w);
        if (t >= lead || t == 0)
        {
            io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
            if (t >= lead) { Float32 e = compare (r, t, frames, ch, 1.0f); if (e > worst) worst = e; }
        }
    }
    CHECK (worst == 0, "%s: exact loopback on every channel over 3 ring lengths (err %g)", what, worst);

    // two writers into the same frames mix (sum)
    SInt64 t = 10 * kRing;
    fill (w, t, frames, ch, 0.25f);
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) t, frames, w);
    fill (w, t, frames, ch, 0.5f);
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) t, frames, w);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, t, frames, ch, 0.75f) < 1e-6f, "%s: two writers mix (err %g)", what, compare (r, t, frames, ch, 0.75f));
    // frames in the gap before that write, and after it: silence (not old ring contents)
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) (t - frames), frames, r);
    CHECK (compare (r, 0, frames, ch, 0) == 0, "%s: gap is silent", what);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) (t + frames), frames, r);
    CHECK (compare (r, 0, frames, ch, 0) == 0, "%s: unwritten future is silent", what);
    // a read that straddles written and unwritten frames
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) (t + frames / 2), frames, r);
    CHECK (compare (r, t + frames / 2, frames / 2, ch, 0.75f) < 1e-6f && compare (r + (frames / 2) * ch, 0, frames / 2, ch, 0) == 0, "%s: straddling read", what);
    // more than a ring later the old data is gone
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) (t + 2 * kRing), frames, w);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, 0, frames, ch, 0) == 0, "%s: data older than the ring is silent", what);

    // volume: scalar 0.5 → gain 0.25 (−12 dB)
    Float32 v = 0.5f;
    AudioObjectPropertyAddress sa = G (kAudioLevelControlPropertyScalarValue);
    gNumChanges = 0;
    CHECK ((*gDriver)->SetPropertyData (gDriver, o.volume, 0, &sa, 0, NULL, sizeof (v), &v) == noErr, "%s: set volume", what);
    CHECK (changed (o.volume, kAudioLevelControlPropertyScalarValue) && changed (o.volume, kAudioLevelControlPropertyDecibelValue), "%s: volume notified", what);
    CHECK (fabsf (getF32 (o.volume, G (kAudioLevelControlPropertyDecibelValue)) - (-12.0412f)) < 0.01f, "%s: volume dB %g", what, getF32 (o.volume, G (kAudioLevelControlPropertyDecibelValue)));
    t = 20 * kRing;
    fill (w, t, frames, ch, 1.0f);
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) t, frames, w);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, t, frames, ch, 0.25f) < 1e-6f, "%s: volume applied (err %g)", what, compare (r, t, frames, ch, 0.25f));
    // dB setter
    Float32 db = -6.0206f;
    AudioObjectPropertyAddress da = G (kAudioLevelControlPropertyDecibelValue);
    (*gDriver)->SetPropertyData (gDriver, o.volume, 0, &da, 0, NULL, sizeof (db), &db);
    CHECK (fabsf (getF32 (o.volume, sa) - 0.70711f) < 1e-3f, "%s: dB → scalar %g", what, getF32 (o.volume, sa));
    // mute
    UInt32 m = 1;
    AudioObjectPropertyAddress ma = G (kAudioBooleanControlPropertyValue);
    CHECK ((*gDriver)->SetPropertyData (gDriver, o.mute, 0, &ma, 0, NULL, sizeof (m), &m) == noErr && getU32 (o.mute, ma) == 1, "%s: mute", what);
    t += frames;
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) t, frames, w);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, 0, frames, ch, 0) == 0, "%s: mute applied", what);
    m = 0; v = 1.0f;
    (*gDriver)->SetPropertyData (gDriver, o.mute, 0, &ma, 0, NULL, sizeof (m), &m);
    (*gDriver)->SetPropertyData (gDriver, o.volume, 0, &sa, 0, NULL, sizeof (v), &v);
    t += frames;
    fill (w, t, frames, ch, 1.0f);
    io (dev, o.out, kAudioServerPlugInIOOperationWriteMix, (Float64) t, frames, w);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, t, frames, ch, 1.0f) == 0, "%s: unmuted, full volume", what);

    CHECK ((*gDriver)->StopIO (gDriver, dev, 1) == noErr, "%s: StopIO", what);
    CHECK (getU32 (dev, G (kAudioDevicePropertyDeviceIsRunning)) == 0, "%s: stopped", what);
    // a restart starts a new timeline: old data doesn't come back
    (*gDriver)->StartIO (gDriver, dev, 1);
    io (dev, o.in, kAudioServerPlugInIOOperationReadInput, (Float64) t, frames, r);
    CHECK (compare (r, 0, frames, ch, 0) == 0, "%s: silent after restart", what);
    (*gDriver)->StopIO (gDriver, dev, 1);
    free (w); free (r);
}

static void checkStoredList (int expectCount)
{
    CFArrayRef stored = (CFArrayRef) CFDictionaryGetValue (gStorage, CFSTR ("devices"));
    CHECK (stored && CFGetTypeID (stored) == CFArrayGetTypeID () && CFArrayGetCount (stored) == expectCount, "storage holds %d devices", expectCount);
}

// ---------------------------------------------------------------------------------------------------------------------

// ---- the IO thread runs while another thread changes the device list and the controls ----
typedef struct { AudioObjectID dev, in, out; UInt32 ch; atomic_int stop; long cycles; Float32 worst; } StressIO;
static void* stressThread (void* p)
{
    StressIO* s = (StressIO*) p;
    Float32 w[2 * 256], r[2 * 256];
    for (SInt64 t = 0; ! atomic_load (&s->stop); t += 256, ++s->cycles)
    {
        fill (w, t + 512, 256, s->ch, 1.0f);
        io (s->dev, s->out, kAudioServerPlugInIOOperationWriteMix, (Float64) (t + 512), 256, w);
        io (s->dev, s->in, kAudioServerPlugInIOOperationReadInput, (Float64) t, 256, r);
        if (t >= 512) { Float32 e = compare (r, t, 256, s->ch, 1.0f); if (e > s->worst) s->worst = e; }
    }
    return NULL;
}
static void stressTest (void)
{
    CFDictionaryRef a = makeSpec ("um:io", "IO", 2, false), b = makeSpec ("um:other", "Other", 8, false), a2 = makeSpec ("um:io", "IO renamed", 2, true);
    CFDictionaryRef one[1] = { a }, two[2] = { a, b }, renamed[2] = { a2, b };
    setDevices (one, 1);
    StressIO s; memset (&s, 0, sizeof (s));
    s.dev = deviceWithUID ("um:io"); s.ch = 2;
    Objects o = objectsOf (s.dev); s.in = o.in; s.out = o.out;
    (*gDriver)->StartIO (gDriver, s.dev, 1);
    pthread_t th; pthread_create (&th, NULL, stressThread, &s);
    AudioObjectPropertyAddress ma = G (kAudioBooleanControlPropertyValue);
    for (int i = 0; i < 300; ++i)
    {
        setDevices ((i % 3) == 0 ? two : (i % 3) == 1 ? renamed : one, (i % 3) == 2 ? 1 : 2);
        UInt32 m = 0;
        (*gDriver)->SetPropertyData (gDriver, o.mute, 0, &ma, 0, NULL, sizeof (m), &m);
        AudioObjectID ids[256]; deviceList (ids);
        sweepProperties (deviceWithUID ("um:io"), "device under IO");
    }
    atomic_store (&s.stop, 1);
    pthread_join (th, NULL);
    CHECK (deviceWithUID ("um:io") == s.dev, "the device kept its object through renames");
    CHECK (s.cycles > 100 && s.worst == 0, "loopback stayed exact during %ld cycles of list changes (err %g)", s.cycles, s.worst);
    (*gDriver)->StopIO (gDriver, s.dev, 1);
    CFRelease (a); CFRelease (b); CFRelease (a2);
}

int main (void)
{
    gStorage = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // ---- load the bundle the way coreaudiod does: CFPlugIn, the factory for kAudioServerPlugInTypeUUID ----
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8*) UM_DRIVER_BUNDLE, strlen (UM_DRIVER_BUNDLE), true);
    CFPlugInRef plugin = CFPlugInCreate (NULL, url);
    CFRelease (url);
    if (plugin == NULL)
    {
        CFURLRef u2 = CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8*) UM_DRIVER_BUNDLE, strlen (UM_DRIVER_BUNDLE), true);
        CFBundleRef b = CFBundleCreate (NULL, u2);
        printf ("FAIL: cannot load %s (bundle %p)\n", UM_DRIVER_BUNDLE, (void*) b);
        if (b) CFShow (CFBundleGetInfoDictionary (b));
        CFShow (u2);
        printf ("access %d\n", access (UM_DRIVER_BUNDLE "/Contents/Info.plist", R_OK));
        return 1;
    }
    CFArrayRef factories = CFPlugInFindFactoriesForPlugInTypeInPlugIn (kAudioServerPlugInTypeUUID, plugin);
    if (factories == NULL || CFArrayGetCount (factories) != 1) { printf ("FAIL: no factory for the AudioServerPlugIn type\n"); return 1; }
    CFUUIDRef factory = (CFUUIDRef) CFArrayGetValueAtIndex (factories, 0);
    IUnknownVTbl** unknown = (IUnknownVTbl**) CFPlugInInstanceCreate (NULL, factory, kAudioServerPlugInTypeUUID);
    CFRelease (factories);
    if (unknown == NULL) { printf ("FAIL: factory made nothing\n"); return 1; }
    CHECK ((*unknown)->QueryInterface (unknown, CFUUIDGetUUIDBytes (kAudioServerPlugInDriverInterfaceUUID), (LPVOID*) &gDriver) == S_OK && gDriver != NULL, "driver interface");
    void* nothing = NULL;
    CHECK ((*unknown)->QueryInterface (unknown, CFUUIDGetUUIDBytes (kAudioServerPlugInTypeUUID), &nothing) == E_NOINTERFACE, "no other interface");
    (*unknown)->Release (unknown);

    AudioServerPlugInHostRef host = &gHostInterface;
    CHECK ((*gDriver)->Initialize (gDriver, host) == noErr, "Initialize");

    // ---- first run: one device "umleitung", 2 channels; written to storage ----
    AudioObjectID ids[256];
    CHECK (deviceList (ids) == 1, "one default device");
    checkDevice (ids[0], "umleitung:default", "umleitung", 2, false, 48000);
    checkStoredList (1);
    CHECK (getU32 (kAudioObjectPlugInObject, G (kAudioObjectPropertyClass)) == kAudioPlugInClassID, "plug-in class");
    CHECK (stringIs (kAudioObjectPlugInObject, G (kAudioObjectPropertyManufacturer), "umleitung"), "plug-in manufacturer");
    CHECK (stringIs (kAudioObjectPlugInObject, G (kUmPropertyVersion), "1.0.0"), "version from the bundle");
    {
        AudioServerPlugInCustomPropertyInfo info[4]; UInt32 got = 0;
        get (kAudioObjectPlugInObject, G (kAudioObjectPropertyCustomPropertyInfoList), sizeof (info), info, &got);
        CHECK (got == 2 * sizeof (info[0]) && info[0].mSelector == kUmPropertyDevices && info[0].mPropertyDataType == kAudioServerPlugInCustomPropertyDataTypeCFPropertyList
               && info[1].mSelector == kUmPropertyVersion && info[1].mPropertyDataType == kAudioServerPlugInCustomPropertyDataTypeCFString, "custom property info list");
        Boolean settable = 0;
        AudioObjectPropertyAddress a = G (kUmPropertyDevices);
        (*gDriver)->IsPropertySettable (gDriver, kAudioObjectPlugInObject, 0, &a, &settable);
        CHECK (settable, "'umdv' settable");
    }

    // ---- three devices: 2, 16, 64 channels (one hidden, one without a UID: the driver picks one) ----
    gNumChanges = 0;
    AudioObjectID oldDefault = ids[0];
    CFDictionaryRef three[3] = { makeSpec ("umleitung:default", "Main", 2, false), makeSpec ("um:sixteen", "Sixteen", 16, true), makeSpec (NULL, "Big", 64, false) };
    CHECK (setDevices (three, 3) == noErr, "set 3 devices");
    for (int i = 0; i < 3; ++i) CFRelease (three[i]);
    CHECK (changed (kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList) && changed (kAudioObjectPlugInObject, kAudioObjectPropertyOwnedObjects), "device list change notified");
    CHECK (changed (oldDefault, kAudioObjectPropertyName), "rename notified on the device");
    CHECK (deviceList (ids) == 3, "3 devices");
    CHECK (ids[0] == oldDefault, "the kept device keeps its object ID");
    CFArrayRef list = getDevicesProperty ();
    CHECK (list && CFArrayGetCount (list) == 3, "'umdv' reads back 3");
    char bigUID[128] = "";
    if (list && CFArrayGetCount (list) == 3)
    {
        CFDictionaryRef big = CFArrayGetValueAtIndex (list, 2);
        CFStringRef u = CFDictionaryGetValue (big, CFSTR ("uid"));
        CFStringGetCString (u, bigUID, sizeof (bigUID), kCFStringEncodingUTF8);
        CHECK (strncmp (bigUID, "umleitung:", 10) == 0 && strlen (bigUID) > 12, "generated UID %s", bigUID);
        int ch = 0; CFNumberGetValue (CFDictionaryGetValue (big, CFSTR ("channels")), kCFNumberIntType, &ch);
        CHECK (ch == 64, "'umdv' channels");
        CHECK (CFDictionaryGetValue (CFArrayGetValueAtIndex (list, 1), CFSTR ("hidden")) == kCFBooleanTrue, "'umdv' hidden");
    }
    if (list) CFRelease (list);
    checkDevice (ids[0], "umleitung:default", "Main", 2, false, 48000);
    checkDevice (ids[1], "um:sixteen", "Sixteen", 16, true, 48000);
    checkDevice (ids[2], bigUID, "Big", 64, false, 48000);
    checkStoredList (3);

    // ---- every object type answers its properties consistently ----
    sweepProperties (kAudioObjectPlugInObject, "plug-in");
    for (int i = 0; i < 3; ++i)
    {
        Objects o = objectsOf (ids[i]);
        sweepProperties (o.device, "device");
        sweepProperties (o.in, "input stream");
        sweepProperties (o.out, "output stream");
        sweepProperties (o.volume, "volume");
        sweepProperties (o.mute, "mute");
    }
    {
        AudioObjectID owned[8];
        CHECK (idList (ids[1], G (kAudioObjectPropertyOwnedObjects), owned, 8) == 4, "device owns 2 streams + 2 controls");
        CHECK (idList (ids[1], addr (kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeInput, 0), owned, 8) == 1, "input scope owns 1");
        // a short buffer gets the first IDs, not an overrun
        CHECK (idList (ids[1], G (kAudioObjectPropertyOwnedObjects), owned, 2) == 2, "short list buffer");
        UInt32 size = 0;
        AudioObjectPropertyAddress la = addr (kAudioDevicePropertyPreferredChannelLayout, kAudioObjectPropertyScopeOutput, 0);
        (*gDriver)->GetPropertyDataSize (gDriver, ids[2], 0, &la, 0, NULL, &size);
        AudioChannelLayout* l = (AudioChannelLayout*) malloc (size);
        UInt32 got = 0;
        CHECK ((*gDriver)->GetPropertyData (gDriver, ids[2], 0, &la, 0, NULL, size, &got, l) == noErr && l->mNumberChannelDescriptions == 64, "64-channel layout");
        free (l);
        CHECK (stringIs (ids[0], addr (kAudioObjectPropertyElementName, kAudioObjectPropertyScopeInput, 2), "Right"), "stereo channel names");
        CHECK (stringIs (ids[2], addr (kAudioObjectPropertyElementName, kAudioObjectPropertyScopeOutput, 64), "Channel 64"), "channel names");
        CHECK (translate ("nope") == kAudioObjectUnknown, "unknown UID");
    }

    // ---- bad lists are refused and change nothing ----
    {
        CFDictionaryRef zero = makeSpec ("a", "A", 0, false), huge = makeSpec ("a", "A", 257, false);
        CFDictionaryRef dup[2] = { makeSpec ("a", "A", 2, false), makeSpec ("a", "B", 2, false) };
        CHECK (setDevices (&zero, 1) != noErr, "0 channels refused");
        CHECK (setDevices (&huge, 1) != noErr, "257 channels refused");
        CHECK (setDevices (dup, 2) != noErr, "duplicate UIDs refused");
        CHECK (setPlist (CFSTR ("not a list")) != noErr, "a string refused");
        CFArrayRef notDicts = CFArrayCreate (NULL, (const void**) &(CFStringRef) { CFSTR ("x") }, 1, &kCFTypeArrayCallBacks);
        CHECK (setPlist (notDicts) != noErr, "an array of strings refused");
        CFRelease (notDicts);
        CFRelease (zero); CFRelease (huge); CFRelease (dup[0]); CFRelease (dup[1]);
        AudioObjectID now[256];
        CHECK (deviceList (now) == 3 && now[0] == ids[0] && now[1] == ids[1] && now[2] == ids[2], "list unchanged after refusals");
    }

    // ---- IO: exact loopback on every channel, mixing, silence, volume, mute ----
    testIO (ids[0], 2, "2ch");
    testIO (ids[1], 16, "16ch");
    testIO (ids[2], 64, "64ch");

    // ---- sample rate: a configuration change through the host ----
    {
        Float64 rate = 96000;
        AudioObjectPropertyAddress ra = G (kAudioDevicePropertyNominalSampleRate);
        Boolean settable = 0;
        (*gDriver)->IsPropertySettable (gDriver, ids[1], 0, &ra, &settable);
        CHECK (settable, "rate settable");
        gConfigRequests = 0;
        CHECK ((*gDriver)->SetPropertyData (gDriver, ids[1], 0, &ra, 0, NULL, sizeof (rate), &rate) == noErr && gConfigRequests == 1, "rate change requested");
        CHECK (getF64 (ids[1], ra) == 96000, "rate is 96 kHz");
        AudioStreamBasicDescription f; UInt32 got = 0;
        Objects o = objectsOf (ids[1]);
        get (o.in, G (kAudioStreamPropertyVirtualFormat), sizeof (f), &f, &got);
        CHECK (f.mSampleRate == 96000, "stream format follows the rate");
        rate = 12345;
        CHECK ((*gDriver)->SetPropertyData (gDriver, ids[1], 0, &ra, 0, NULL, sizeof (rate), &rate) != noErr, "unsupported rate refused");
        // through the stream format
        f.mSampleRate = 44100;
        AudioObjectPropertyAddress fa = G (kAudioStreamPropertyPhysicalFormat);
        CHECK ((*gDriver)->SetPropertyData (gDriver, o.out, 0, &fa, 0, NULL, sizeof (f), &f) == noErr && getF64 (ids[1], ra) == 44100, "rate via the stream format");
        f.mChannelsPerFrame = 3;
        CHECK ((*gDriver)->SetPropertyData (gDriver, o.out, 0, &fa, 0, NULL, sizeof (f), &f) != noErr, "a different channel count is refused");
        // every rate
        static const Float64 rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
        for (int i = 0; i < 6; ++i)
        {
            rate = rates[i];
            (*gDriver)->SetPropertyData (gDriver, ids[2], 0, &ra, 0, NULL, sizeof (rate), &rate);
            CHECK (getF64 (ids[2], ra) == rates[i], "rate %g", rates[i]);
        }
        // the clock: at 192 kHz a zero time stamp period (16384 frames) is 85 ms
        (*gDriver)->StartIO (gDriver, ids[2], 1);
        Float64 st0, st1; UInt64 ht0, ht1, seed;
        (*gDriver)->GetZeroTimeStamp (gDriver, ids[2], 1, &st0, &ht0, &seed);
        usleep (200000);
        (*gDriver)->GetZeroTimeStamp (gDriver, ids[2], 1, &st1, &ht1, &seed);
        struct mach_timebase_info tb; mach_timebase_info (&tb);
        double ns = (double) (ht1 - ht0) * tb.numer / tb.denom;
        CHECK (st1 - st0 == 2 * 16384 && fabs (ns - 2 * 16384 / 192000.0 * 1e9) < 1000, "clock: %g frames in %g ms", st1 - st0, ns / 1e6);
        (*gDriver)->StopIO (gDriver, ids[2], 1);
        testIO (ids[2], 64, "64ch@192k");
        // the 'umdv' list can set a rate too
        CFDictionaryRef s = makeSpec ("umleitung:default", "Main", 2, false);
        CFMutableDictionaryRef ms = CFDictionaryCreateMutableCopy (NULL, 0, s);
        Float64 r88 = 88200; CFNumberRef n = CFNumberCreate (NULL, kCFNumberFloat64Type, &r88);
        CFDictionarySetValue (ms, CFSTR ("rate"), n); CFRelease (n);
        CFArrayRef cur = getDevicesProperty ();
        CFMutableArrayRef next = CFArrayCreateMutableCopy (NULL, 0, cur);
        CFArraySetValueAtIndex (next, 0, ms);
        CHECK (setPlist (next) == noErr && getF64 (ids[0], ra) == 88200, "rate through 'umdv'");
        CFRelease (s); CFRelease (ms); CFRelease (cur); CFRelease (next);
    }

    // ---- remove one, rebuild one (channel count), rename one ----
    {
        CFArrayRef cur = getDevicesProperty ();
        CFMutableArrayRef next = CFArrayCreateMutable (NULL, 0, &kCFTypeArrayCallBacks);
        CFArrayAppendValue (next, CFArrayGetValueAtIndex (cur, 0));     // keep
        CFArrayAppendValue (next, CFArrayGetValueAtIndex (cur, 2));     // keep the 64 (the 16 goes)
        gNumChanges = 0;
        CHECK (setPlist (next) == noErr, "remove one");
        AudioObjectID now[256];
        CHECK (deviceList (now) == 2 && now[0] == ids[0] && now[1] == ids[2], "the 16-channel device is gone, the others keep their IDs");
        CHECK (translate ("um:sixteen") == kAudioObjectUnknown, "its UID is gone");
        Objects gone = { ids[1], 0, 0, 0, 0 };
        AudioObjectPropertyAddress ca = G (kAudioObjectPropertyClass);
        CHECK (! (*gDriver)->HasProperty (gDriver, gone.device, 0, &ca), "its object is gone");
        CHECK (changed (kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList), "removal notified");
        checkStoredList (2);
        CFRelease (next);

        // rebuild: 64 → 8 channels, same UID, new object
        CFDictionaryRef rebuilt = makeSpec (bigUID, "Big", 8, false);
        CFDictionaryRef keep = CFArrayGetValueAtIndex (cur, 0);
        CFDictionaryRef two[2] = { keep, rebuilt };
        CHECK (setDevices (two, 2) == noErr, "rebuild");
        CHECK (deviceList (now) == 2 && now[0] == ids[0] && now[1] != ids[2], "rebuilt device has a new object ID");
        checkDevice (now[1], bigUID, "Big", 8, false, 192000);    // keeps its rate
        CFRelease (rebuilt);
        CFRelease (cur);
        ids[1] = now[1];
    }

    // ---- a device removed while IO runs on it lives until its StopIO ----
    {
        CFDictionaryRef tmp[3] = { makeSpec ("umleitung:default", "Main", 2, false), makeSpec (bigUID, "Big", 8, false), makeSpec ("um:temp", "Temp", 4, false) };
        setDevices (tmp, 3);
        AudioObjectID t = deviceWithUID ("um:temp");
        Objects o = objectsOf (t);
        (*gDriver)->StartIO (gDriver, t, 1);
        setDevices (tmp, 2);
        Float32 buf[4 * 64];
        CHECK (io (t, o.out, kAudioServerPlugInIOOperationWriteMix, 0, 64, buf) == kAudioHardwareBadDeviceError, "IO on a removed device fails cleanly");
        CHECK ((*gDriver)->StopIO (gDriver, t, 1) == noErr, "StopIO on a removed device");
        CHECK ((*gDriver)->StopIO (gDriver, t, 1) != noErr, "it is gone after");
        for (int i = 0; i < 3; ++i) CFRelease (tmp[i]);
    }

    // ---- the maximum: 256 channels ----
    {
        CFDictionaryRef s[2] = { makeSpec ("umleitung:default", "Main", 2, false), makeSpec ("um:max", "Max", 256, false) };
        CHECK (setDevices (s, 2) == noErr, "256 channels");
        AudioObjectID d = deviceWithUID ("um:max");
        checkDevice (d, "um:max", "Max", 256, false, 48000);
        testIO (d, 256, "256ch");
        CFRelease (s[0]); CFRelease (s[1]);
    }

    // ---- persistence: a new Initialize (coreaudiod restarting) reads the list back from storage ----
    {
        CFDictionaryRef s[3] = { makeSpec ("umleitung:default", "Main", 2, false), makeSpec ("um:p16", "Persist 16", 16, true), makeSpec ("um:p64", "Persist 64", 64, false) };
        setDevices (s, 3);
        Float64 rate = 96000;
        AudioObjectPropertyAddress ra = G (kAudioDevicePropertyNominalSampleRate);
        (*gDriver)->SetPropertyData (gDriver, deviceWithUID ("um:p64"), 0, &ra, 0, NULL, sizeof (rate), &rate);
        for (int i = 0; i < 3; ++i) CFRelease (s[i]);
        CHECK ((*gDriver)->Initialize (gDriver, host) == noErr, "second Initialize");
        AudioObjectID now[256];
        CHECK (deviceList (now) == 3, "3 devices after restart");
        checkDevice (now[0], "umleitung:default", "Main", 2, false, 88200);
        checkDevice (now[1], "um:p16", "Persist 16", 16, true, 48000);
        checkDevice (now[2], "um:p64", "Persist 64", 64, false, 96000);
        // an empty list is allowed (and persists)
        CHECK (setDevices (NULL, 0) == noErr && deviceList (now) == 0, "no devices");
        (*gDriver)->Initialize (gDriver, host);
        CHECK (deviceList (now) == 0, "no devices after restart");
        // no storage at all: the default again
        CFDictionaryRemoveAllValues (gStorage);
        (*gDriver)->Initialize (gDriver, host);
        CHECK (deviceList (now) == 1, "default after storage loss");
        checkDevice (now[0], "umleitung:default", "umleitung", 2, false, 48000);
    }

    stressTest ();

    // tidy: no devices, so everything the driver allocated is freed (for the leak check)
    setDevices (NULL, 0);
    (*gDriver)->Release (gDriver);
    CFRelease (gStorage);

    printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

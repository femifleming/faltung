// umleitung.driver — an AudioServerPlugIn (HAL driver) that publishes any number of virtual loopback devices.
//
// Each device has a UID, a name and a channel count (1…256, the same in and out). What clients play into a device's
// output comes out of its input: one ring buffer per device, written by WriteMix, read by ReadInput, both at the
// sample times the HAL asks for. Frames nobody wrote read as silence. The clock is the driver's own (a zero time
// stamp every kZeroTimeStampPeriod frames, against the host clock).
//
// The device list is a custom property on the plug-in object ('umdv', a CFArray of CFDictionaries {uid, name,
// channels, hidden, rate}); setting it adds, removes and rebuilds devices and tells the host through
// PropertiesChanged on the plug-in's device list. The list is kept in the host's storage (key "devices").
//
// Written from Apple's AudioServerPlugIn.h documentation and the design of Apple's NullAudio sample.
//
// Threads: every property call and Start/StopIO take gStateLock. The IO calls (GetZeroTimeStamp, Will/Begin/Do/
// EndIOOperation) never lock and never allocate: they find their device in gSlots (atomic pointers), and a device
// that is removed while IO runs on it is parked on a zombie list and freed at its last StopIO.

#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark constants

#define kBundleID               "com.faltung.umleitung.driver"
#define kManufacturer           "umleitung"
#define kModelName              "umleitung virtual device"
#define kModelUID               "umleitung:model"
#define kStorageKey             "devices"
#define kDefaultUID             "umleitung:default"
#define kDefaultName            "umleitung"

enum
{
    kMaxDevices          = 256,
    kMaxChannels         = 256,
    kRingFrames          = 16384,      // a power of two, ≥ the zero time stamp period
    kRingMask            = kRingFrames - 1,
    kZeroTimeStampPeriod = kRingFrames,
    kMaxNotes            = 1024
};

// the custom properties on the plug-in object
enum
{
    kUmPropertyDevices = 'umdv',   // CFArray of CFDictionary {uid, name, channels, hidden, rate}; get + set
    kUmPropertyVersion = 'umvr'    // CFString, the bundle's CFBundleShortVersionString; get
};

static const Float64 kSampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
enum { kNumSampleRates = sizeof (kSampleRates) / sizeof (kSampleRates[0]) };

static const Float32 kVolumeMinDB = -96.0f;
static const Float32 kVolumeMaxDB = 0.0f;

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark state

typedef struct Device Device;
struct Device
{
    AudioObjectID deviceID, inStreamID, outStreamID, volumeID, muteID;
    CFStringRef   uid;
    CFStringRef   name;
    UInt32        channels;
    Boolean       hidden;
    Float64       sampleRate;
    Boolean       inActive, outActive;

    UInt32        ioCount;            // under gStateLock
    Boolean       zombie;             // removed from the list while IO was running
    Device*       nextZombie;

    // clock (written by StartIO before IO starts and by config changes while IO is stopped; then the IO thread's)
    UInt64        anchorHostTime;
    Float64       hostTicksPerFrame;
    UInt64        timeStampCount;
    UInt64        seed;

    // loopback ring (the IO thread's)
    Float32*      ring;               // kRingFrames × channels, interleaved
    SInt64        writeHigh;          // one past the newest frame written
    Boolean       written;            // anything written since IO started

    _Atomic(Float32) volume;          // scalar 0…1
    _Atomic(UInt32)  mute;
};

typedef struct DeviceSpec
{
    CFStringRef uid;     // retained
    CFStringRef name;    // retained
    UInt32      channels;
    Boolean     hidden;
    Float64     sampleRate;   // 0: keep / default
} DeviceSpec;

typedef struct Note { AudioObjectID object; AudioObjectPropertyAddress address; } Note;
typedef struct Notes { Note items[kMaxNotes]; UInt32 count; } Notes;

static pthread_mutex_t          gStateLock = PTHREAD_MUTEX_INITIALIZER;
static AudioServerPlugInHostRef gHost = NULL;
static UInt32                   gRefCount = 0;
static AudioObjectID            gNextObjectID = 2;           // 1 is the plug-in (kAudioObjectPlugInObject)
static Device* _Atomic          gSlots[kMaxDevices];          // where IO finds devices
static Device*                  gOrder[kMaxDevices];          // the published order
static UInt32                   gNumDevices = 0;
static Device*                  gZombies = NULL;
static CFStringRef              gVersion = NULL;
static Float64                  gHostTicksPerSecond = 0;

static inline void lock (void)   { pthread_mutex_lock (&gStateLock); }
static inline void unlock (void) { pthread_mutex_unlock (&gStateLock); }

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark helpers

static Boolean isSupportedRate (Float64 r)
{
    for (int i = 0; i < kNumSampleRates; ++i)
        if (fabs (kSampleRates[i] - r) < 0.5) return true;
    return false;
}

static Float32 scalarToDB (Float32 s)
{
    if (s <= 0.0f) return kVolumeMinDB;
    if (s >= 1.0f) return kVolumeMaxDB;
    Float32 db = 40.0f * log10f (s);                 // gain = scalar², so dB = 20·log10(s²)
    return db < kVolumeMinDB ? kVolumeMinDB : db;
}

static Float32 dbToScalar (Float32 db)
{
    if (db <= kVolumeMinDB) return 0.0f;
    if (db >= kVolumeMaxDB) return 1.0f;
    return powf (10.0f, db / 40.0f);
}

static void computeTicks (Device* d)
{
    d->hostTicksPerFrame = gHostTicksPerSecond / d->sampleRate;
}

static void addNote (Notes* n, AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope)
{
    if (n == NULL || n->count >= kMaxNotes) return;
    n->items[n->count].object = obj;
    n->items[n->count].address.mSelector = sel;
    n->items[n->count].address.mScope = scope;
    n->items[n->count].address.mElement = kAudioObjectPropertyElementMain;
    n->count++;
}

// called without gStateLock held
static void sendNotes (const Notes* n)
{
    if (gHost == NULL || n == NULL) return;
    for (UInt32 i = 0; i < n->count;)
    {
        UInt32 j = i;
        AudioObjectPropertyAddress addrs[16];
        UInt32 k = 0;
        while (j < n->count && n->items[j].object == n->items[i].object && k < 16)
            addrs[k++] = n->items[j++].address;
        gHost->PropertiesChanged (gHost, n->items[i].object, k, addrs);
        i = j;
    }
}

static Device* findDevice (AudioObjectID objectID)       // any object of a device; under lock
{
    for (UInt32 i = 0; i < gNumDevices; ++i)
    {
        Device* d = gOrder[i];
        if (objectID >= d->deviceID && objectID <= d->muteID) return d;
    }
    return NULL;
}

static Device* findDeviceForIO (AudioObjectID deviceID)  // lock-free
{
    for (UInt32 i = 0; i < kMaxDevices; ++i)
    {
        Device* d = atomic_load_explicit (&gSlots[i], memory_order_acquire);
        if (d != NULL && d->deviceID == deviceID) return d;
    }
    return NULL;
}

static Device* findDeviceByUID (CFStringRef uid)
{
    for (UInt32 i = 0; i < gNumDevices; ++i)
        if (CFStringCompare (gOrder[i]->uid, uid, 0) == kCFCompareEqualTo) return gOrder[i];
    return NULL;
}

static CFStringRef makeUID (void)
{
    return CFStringCreateWithFormat (NULL, NULL, CFSTR ("umleitung:%08x%04x"), arc4random (), arc4random () & 0xffff);
}

static void fillASBD (const Device* d, AudioStreamBasicDescription* f)
{
    memset (f, 0, sizeof (*f));
    f->mSampleRate       = d->sampleRate;
    f->mFormatID         = kAudioFormatLinearPCM;
    f->mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    f->mBitsPerChannel   = 32;
    f->mChannelsPerFrame = d->channels;
    f->mBytesPerFrame    = 4 * d->channels;
    f->mFramesPerPacket  = 1;
    f->mBytesPerPacket   = 4 * d->channels;
}

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark devices

static Device* createDevice (const DeviceSpec* s)
{
    Device* d = (Device*) calloc (1, sizeof (Device));
    if (d == NULL) return NULL;
    d->channels = s->channels;
    d->ring = (Float32*) calloc ((size_t) kRingFrames * d->channels, sizeof (Float32));
    if (d->ring == NULL) { free (d); return NULL; }
    d->deviceID    = gNextObjectID++;
    d->inStreamID  = gNextObjectID++;
    d->outStreamID = gNextObjectID++;
    d->volumeID    = gNextObjectID++;
    d->muteID      = gNextObjectID++;
    d->uid  = (CFStringRef) CFRetain (s->uid);
    d->name = (CFStringRef) CFRetain (s->name);
    d->hidden = s->hidden;
    d->sampleRate = isSupportedRate (s->sampleRate) ? s->sampleRate : 48000.0;
    d->inActive = d->outActive = true;
    d->seed = 1;
    atomic_store (&d->volume, 1.0f);
    atomic_store (&d->mute, 0u);
    computeTicks (d);
    return d;
}

static void freeDevice (Device* d)
{
    if (d == NULL) return;
    free (d->ring);
    if (d->uid) CFRelease (d->uid);
    if (d->name) CFRelease (d->name);
    free (d);
}

static Boolean addToSlots (Device* d)
{
    for (UInt32 i = 0; i < kMaxDevices; ++i)
        if (atomic_load (&gSlots[i]) == NULL) { atomic_store_explicit (&gSlots[i], d, memory_order_release); return true; }
    return false;
}

static void removeDevice (Device* d)       // from slots; frees it now or at its last StopIO
{
    for (UInt32 i = 0; i < kMaxDevices; ++i)
        if (atomic_load (&gSlots[i]) == d) atomic_store_explicit (&gSlots[i], NULL, memory_order_release);
    if (d->ioCount == 0) freeDevice (d);
    else { d->zombie = true; d->nextZombie = gZombies; gZombies = d; }
}

static void freeSpecs (DeviceSpec* specs, UInt32 n)
{
    for (UInt32 i = 0; i < n; ++i)
    {
        if (specs[i].uid) CFRelease (specs[i].uid);
        if (specs[i].name) CFRelease (specs[i].name);
    }
    free (specs);
}

static Boolean numberValue (CFTypeRef v, double* out)
{
    if (v == NULL) return false;
    if (CFGetTypeID (v) == CFNumberGetTypeID ()) return CFNumberGetValue ((CFNumberRef) v, kCFNumberDoubleType, out);
    if (CFGetTypeID (v) == CFBooleanGetTypeID ()) { *out = CFBooleanGetValue ((CFBooleanRef) v) ? 1 : 0; return true; }
    return false;
}

// parses the property list into specs; false (and nothing allocated) when it is malformed
static Boolean parseSpecs (CFPropertyListRef plist, DeviceSpec** outSpecs, UInt32* outCount)
{
    *outSpecs = NULL; *outCount = 0;
    if (plist == NULL || CFGetTypeID (plist) != CFArrayGetTypeID ()) return false;
    CFArrayRef arr = (CFArrayRef) plist;
    CFIndex n = CFArrayGetCount (arr);
    if (n > kMaxDevices) return false;
    DeviceSpec* specs = (DeviceSpec*) calloc ((size_t) (n > 0 ? n : 1), sizeof (DeviceSpec));
    if (specs == NULL) return false;
    for (CFIndex i = 0; i < n; ++i)
    {
        CFTypeRef item = CFArrayGetValueAtIndex (arr, i);
        if (item == NULL || CFGetTypeID (item) != CFDictionaryGetTypeID ()) { freeSpecs (specs, (UInt32) n); return false; }
        CFDictionaryRef dict = (CFDictionaryRef) item;
        DeviceSpec* s = &specs[i];

        CFTypeRef uid = CFDictionaryGetValue (dict, CFSTR ("uid"));
        if (uid != NULL && CFGetTypeID (uid) == CFStringGetTypeID () && CFStringGetLength ((CFStringRef) uid) > 0)
            s->uid = (CFStringRef) CFRetain (uid);
        else
            s->uid = makeUID ();

        CFTypeRef name = CFDictionaryGetValue (dict, CFSTR ("name"));
        if (name != NULL && CFGetTypeID (name) == CFStringGetTypeID () && CFStringGetLength ((CFStringRef) name) > 0)
            s->name = (CFStringRef) CFRetain (name);
        else
            s->name = (CFStringRef) CFRetain (CFSTR (kDefaultName));

        double v = 2;
        if (CFDictionaryGetValue (dict, CFSTR ("channels")) != NULL)
        {
            if (! numberValue (CFDictionaryGetValue (dict, CFSTR ("channels")), &v) || v < 1 || v > kMaxChannels)
                { freeSpecs (specs, (UInt32) n); return false; }
        }
        s->channels = (UInt32) v;

        double h = 0;
        numberValue (CFDictionaryGetValue (dict, CFSTR ("hidden")), &h);
        s->hidden = h != 0;

        double r = 0;
        if (numberValue (CFDictionaryGetValue (dict, CFSTR ("rate")), &r) && isSupportedRate (r)) s->sampleRate = r;

        for (CFIndex j = 0; j < i; ++j)       // UIDs must be unique
            if (CFStringCompare (specs[j].uid, s->uid, 0) == kCFCompareEqualTo) { freeSpecs (specs, (UInt32) n); return false; }
    }
    *outSpecs = specs; *outCount = (UInt32) n;
    return true;
}

static CFDictionaryRef copyDeviceDictionary (const Device* d)
{
    SInt32 ch = (SInt32) d->channels;
    CFNumberRef chNum = CFNumberCreate (NULL, kCFNumberSInt32Type, &ch);
    CFNumberRef rateNum = CFNumberCreate (NULL, kCFNumberFloat64Type, &d->sampleRate);
    const void* keys[]   = { CFSTR ("uid"), CFSTR ("name"), CFSTR ("channels"), CFSTR ("hidden"), CFSTR ("rate") };
    const void* values[] = { d->uid, d->name, chNum, d->hidden ? kCFBooleanTrue : kCFBooleanFalse, rateNum };
    CFDictionaryRef dict = CFDictionaryCreate (NULL, keys, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease (chNum);
    CFRelease (rateNum);
    return dict;
}

static CFArrayRef copyDeviceList (void)      // under lock
{
    CFMutableArrayRef arr = CFArrayCreateMutable (NULL, gNumDevices, &kCFTypeArrayCallBacks);
    for (UInt32 i = 0; i < gNumDevices; ++i)
    {
        CFDictionaryRef dict = copyDeviceDictionary (gOrder[i]);
        CFArrayAppendValue (arr, dict);
        CFRelease (dict);
    }
    return arr;
}

static void saveDeviceList (void)            // under lock
{
    if (gHost == NULL) return;
    CFArrayRef arr = copyDeviceList ();
    gHost->WriteToStorage (gHost, CFSTR (kStorageKey), arr);
    CFRelease (arr);
}

// makes the published devices exactly the specs (by UID): new ones are created, missing ones go, a changed channel
// count rebuilds a device (new object IDs, same UID), name / hidden change in place. Under lock.
static OSStatus applySpecs (const DeviceSpec* specs, UInt32 n, Notes* notes)
{
    Device* newOrder[kMaxDevices];
    Boolean keep[kMaxDevices];
    memset (keep, 0, sizeof (keep));
    Boolean listChanged = false;

    // how many slots the new list needs on top of the kept devices
    UInt32 needed = 0, freeSlots = 0;
    for (UInt32 i = 0; i < n; ++i)
    {
        Device* old = findDeviceByUID (specs[i].uid);
        if (old == NULL || old->channels != specs[i].channels) ++needed;
    }
    for (UInt32 i = 0; i < kMaxDevices; ++i) if (atomic_load (&gSlots[i]) == NULL) ++freeSlots;
    // devices that go free their slots before the new ones are made
    for (UInt32 i = 0; i < gNumDevices; ++i)
    {
        Boolean stays = false;
        for (UInt32 j = 0; j < n; ++j)
            if (CFStringCompare (gOrder[i]->uid, specs[j].uid, 0) == kCFCompareEqualTo && gOrder[i]->channels == specs[j].channels) stays = true;
        if (! stays) ++freeSlots;
    }
    if (needed > freeSlots) return kAudioHardwareIllegalOperationError;

    // keep / update
    for (UInt32 i = 0; i < n; ++i)
    {
        newOrder[i] = NULL;
        for (UInt32 k = 0; k < gNumDevices; ++k)
        {
            Device* d = gOrder[k];
            if (CFStringCompare (d->uid, specs[i].uid, 0) != kCFCompareEqualTo || d->channels != specs[i].channels) continue;
            keep[k] = true;
            newOrder[i] = d;
            if (CFStringCompare (d->name, specs[i].name, 0) != kCFCompareEqualTo)
            {
                CFRelease (d->name);
                d->name = (CFStringRef) CFRetain (specs[i].name);
                addNote (notes, d->deviceID, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal);
            }
            if (d->hidden != specs[i].hidden)
            {
                d->hidden = specs[i].hidden;
                addNote (notes, d->deviceID, kAudioDevicePropertyIsHidden, kAudioObjectPropertyScopeGlobal);
                addNote (notes, d->deviceID, kAudioDevicePropertyDeviceCanBeDefaultDevice, kAudioObjectPropertyScopeGlobal);
                addNote (notes, d->deviceID, kAudioDevicePropertyDeviceCanBeDefaultSystemDevice, kAudioObjectPropertyScopeGlobal);
            }
            break;
        }
    }
    // remove the rest
    for (UInt32 k = 0; k < gNumDevices; ++k)
        if (! keep[k]) { removeDevice (gOrder[k]); listChanged = true; }
    // create the new ones
    for (UInt32 i = 0; i < n; ++i)
    {
        if (newOrder[i] != NULL) continue;
        DeviceSpec s = specs[i];
        Device* d = createDevice (&s);
        if (d == NULL || ! addToSlots (d))
        {
            freeDevice (d);
            // keep what was made so far consistent: drop the missing entries
            UInt32 m = 0;
            for (UInt32 j = 0; j < n; ++j) if (newOrder[j] != NULL) newOrder[m++] = newOrder[j];
            memcpy (gOrder, newOrder, m * sizeof (Device*));
            gNumDevices = m;
            addNote (notes, kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal);
            addNote (notes, kAudioObjectPlugInObject, kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal);
            return kAudioHardwareUnspecifiedError;
        }
        newOrder[i] = d;
        listChanged = true;
    }
    // the order changed?
    if (! listChanged && n == gNumDevices)
        for (UInt32 i = 0; i < n; ++i) if (gOrder[i] != newOrder[i]) listChanged = true;

    memcpy (gOrder, newOrder, n * sizeof (Device*));
    gNumDevices = n;
    if (listChanged)
    {
        addNote (notes, kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList, kAudioObjectPropertyScopeGlobal);
        addNote (notes, kAudioObjectPlugInObject, kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal);
    }
    return noErr;
}

// the specs carry a rate only when the list came from storage or the app passed one: otherwise keep the device's
static void fillMissingRates (DeviceSpec* specs, UInt32 n)
{
    for (UInt32 i = 0; i < n; ++i)
    {
        if (specs[i].sampleRate != 0) continue;
        Device* d = findDeviceByUID (specs[i].uid);
        specs[i].sampleRate = d != NULL ? d->sampleRate : 48000.0;
    }
}

// a rate in the specs that differs from a kept device's rate needs a configuration change (the host stops IO first);
// collected under the lock, requested after it
typedef struct RateChange { AudioObjectID device; Float64 rate; } RateChange;

static UInt32 collectRateChanges (const DeviceSpec* specs, UInt32 n, RateChange* out)
{
    UInt32 count = 0;
    for (UInt32 i = 0; i < n; ++i)
    {
        Device* d = findDeviceByUID (specs[i].uid);
        if (d != NULL && d->channels == specs[i].channels && specs[i].sampleRate != 0 && fabs (d->sampleRate - specs[i].sampleRate) > 0.5)
            { out[count].device = d->deviceID; out[count].rate = specs[i].sampleRate; ++count; }
    }
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark the interface: forward declarations

static HRESULT  um_QueryInterface (void*, REFIID, LPVOID*);
static ULONG    um_AddRef (void*);
static ULONG    um_Release (void*);
static OSStatus um_Initialize (AudioServerPlugInDriverRef, AudioServerPlugInHostRef);
static OSStatus um_CreateDevice (AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*);
static OSStatus um_DestroyDevice (AudioServerPlugInDriverRef, AudioObjectID);
static OSStatus um_AddDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus um_RemoveDeviceClient (AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*);
static OSStatus um_PerformDeviceConfigurationChange (AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static OSStatus um_AbortDeviceConfigurationChange (AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*);
static Boolean  um_HasProperty (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*);
static OSStatus um_IsPropertySettable (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, Boolean*);
static OSStatus um_GetPropertyDataSize (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
static OSStatus um_GetPropertyData (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, UInt32*, void*);
static OSStatus um_SetPropertyData (AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, const void*);
static OSStatus um_StartIO (AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus um_StopIO (AudioServerPlugInDriverRef, AudioObjectID, UInt32);
static OSStatus um_GetZeroTimeStamp (AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64*, UInt64*, UInt64*);
static OSStatus um_WillDoIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean*, Boolean*);
static OSStatus um_BeginIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);
static OSStatus um_DoIOOperation (AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*, void*, void*);
static OSStatus um_EndIOOperation (AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*);

static AudioServerPlugInDriverInterface gInterface =
{
    NULL,
    um_QueryInterface, um_AddRef, um_Release,
    um_Initialize, um_CreateDevice, um_DestroyDevice, um_AddDeviceClient, um_RemoveDeviceClient,
    um_PerformDeviceConfigurationChange, um_AbortDeviceConfigurationChange,
    um_HasProperty, um_IsPropertySettable, um_GetPropertyDataSize, um_GetPropertyData, um_SetPropertyData,
    um_StartIO, um_StopIO, um_GetZeroTimeStamp,
    um_WillDoIOOperation, um_BeginIOOperation, um_DoIOOperation, um_EndIOOperation
};
static AudioServerPlugInDriverInterface* gInterfacePtr = &gInterface;
static AudioServerPlugInDriverRef        gDriverRef = &gInterfacePtr;

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark factory + IUnknown

__attribute__((visibility ("default")))
void* umleitung_Create (CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID)
{
    (void) allocator;
    if (CFEqual (requestedTypeUUID, kAudioServerPlugInTypeUUID)) { um_AddRef (gDriverRef); return gDriverRef; }
    return NULL;
}

static HRESULT um_QueryInterface (void* driver, REFIID iid, LPVOID* out)
{
    if (driver != gDriverRef || out == NULL) return kAudioHardwareBadObjectError;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes (NULL, iid);
    if (requested == NULL) return kAudioHardwareIllegalOperationError;
    HRESULT result = E_NOINTERFACE;
    if (CFEqual (requested, IUnknownUUID) || CFEqual (requested, kAudioServerPlugInDriverInterfaceUUID))
    {
        um_AddRef (driver);
        *out = gDriverRef;
        result = S_OK;
    }
    CFRelease (requested);
    return result;
}

static ULONG um_AddRef (void* driver)
{
    if (driver != gDriverRef) return 0;
    lock ();
    ULONG r = ++gRefCount;
    unlock ();
    return r;
}

static ULONG um_Release (void* driver)
{
    if (driver != gDriverRef) return 0;
    lock ();
    ULONG r = gRefCount > 0 ? --gRefCount : 0;
    unlock ();
    return r;     // the driver is a static object: nothing to free
}

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark basic operations

static OSStatus um_Initialize (AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host)
{
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;

    struct mach_timebase_info tb;
    mach_timebase_info (&tb);
    gHostTicksPerSecond = 1.0e9 * (Float64) tb.denom / (Float64) tb.numer;

    lock ();
    gHost = host;

    if (gVersion == NULL)
    {
        CFBundleRef bundle = CFBundleGetBundleWithIdentifier (CFSTR (kBundleID));
        CFTypeRef v = bundle ? CFBundleGetValueForInfoDictionaryKey (bundle, CFSTR ("CFBundleShortVersionString")) : NULL;
        gVersion = (v != NULL && CFGetTypeID (v) == CFStringGetTypeID ()) ? (CFStringRef) CFRetain (v) : CFSTR ("0");
    }

    // a fresh start (Initialize is called once by coreaudiod; a second call, as the test harness does, starts over)
    for (UInt32 i = 0; i < gNumDevices; ++i) removeDevice (gOrder[i]);
    gNumDevices = 0;

    DeviceSpec* specs = NULL;
    UInt32 n = 0;
    Boolean fromStorage = false;
    CFPropertyListRef stored = NULL;
    if (host != NULL && host->CopyFromStorage (host, CFSTR (kStorageKey), &stored) == noErr && stored != NULL)
    {
        fromStorage = parseSpecs (stored, &specs, &n);
        CFRelease (stored);
    }
    if (! fromStorage)
    {
        specs = (DeviceSpec*) calloc (1, sizeof (DeviceSpec));
        specs[0].uid = (CFStringRef) CFRetain (CFSTR (kDefaultUID));
        specs[0].name = (CFStringRef) CFRetain (CFSTR (kDefaultName));
        specs[0].channels = 2;
        specs[0].sampleRate = 48000.0;
        n = 1;
    }
    fillMissingRates (specs, n);
    applySpecs (specs, n, NULL);
    freeSpecs (specs, n);
    if (! fromStorage) saveDeviceList ();
    unlock ();
    return noErr;
}

static OSStatus um_CreateDevice (AudioServerPlugInDriverRef d, CFDictionaryRef desc, const AudioServerPlugInClientInfo* ci, AudioObjectID* out)
{
    (void) d; (void) desc; (void) ci; (void) out;
    return kAudioHardwareUnsupportedOperationError;   // devices come from the 'umdv' property
}

static OSStatus um_DestroyDevice (AudioServerPlugInDriverRef d, AudioObjectID id)
{
    (void) d; (void) id;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus um_AddDeviceClient (AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo* ci)
{
    (void) id; (void) ci;
    return d == gDriverRef ? noErr : kAudioHardwareBadObjectError;
}

static OSStatus um_RemoveDeviceClient (AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo* ci)
{
    (void) id; (void) ci;
    return d == gDriverRef ? noErr : kAudioHardwareBadObjectError;
}

// the only configuration change is the sample rate: the action is the new rate in Hz
static OSStatus um_PerformDeviceConfigurationChange (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt64 action, void* info)
{
    (void) info;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    Float64 rate = (Float64) action;
    if (! isSupportedRate (rate)) return kAudioHardwareIllegalOperationError;
    lock ();
    Device* d = findDevice (deviceID);
    if (d == NULL || d->deviceID != deviceID) { unlock (); return kAudioHardwareBadObjectError; }
    d->sampleRate = rate;
    computeTicks (d);
    d->seed++;
    d->written = false;
    saveDeviceList ();
    unlock ();
    return noErr;
}

static OSStatus um_AbortDeviceConfigurationChange (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt64 action, void* info)
{
    (void) deviceID; (void) action; (void) info;
    return driver == gDriverRef ? noErr : kAudioHardwareBadObjectError;
}

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark properties

typedef struct Query
{
    AudioObjectID                     object;
    const AudioObjectPropertyAddress* address;
    UInt32                            qualifierSize;
    const void*                       qualifier;
    UInt32                            inSize;      // room in out
    void*                             out;         // NULL: size only
    UInt32                            outSize;
} Query;

#define FIXED(type, value)                                                                     \
    do {                                                                                       \
        if (q->out != NULL) {                                                                  \
            if (q->inSize < sizeof (type)) return kAudioHardwareBadPropertySizeError;          \
            *(type*) q->out = (value);                                                         \
        }                                                                                      \
        q->outSize = sizeof (type);                                                            \
        return noErr;                                                                          \
    } while (0)

#define CFOBJ(value)                                                                           \
    do {                                                                                       \
        if (q->out != NULL) {                                                                  \
            if (q->inSize < sizeof (CFTypeRef)) return kAudioHardwareBadPropertySizeError;     \
            *(CFTypeRef*) q->out = (value);                                                    \
        }                                                                                      \
        q->outSize = sizeof (CFTypeRef);                                                       \
        return noErr;                                                                          \
    } while (0)

static OSStatus idList (Query* q, const AudioObjectID* ids, UInt32 n)
{
    if (q->out == NULL) { q->outSize = n * (UInt32) sizeof (AudioObjectID); return noErr; }
    UInt32 fit = q->inSize / (UInt32) sizeof (AudioObjectID);
    if (fit > n) fit = n;
    memcpy (q->out, ids, fit * sizeof (AudioObjectID));
    q->outSize = fit * (UInt32) sizeof (AudioObjectID);
    return noErr;
}

static OSStatus pluginProperty (Query* q)
{
    const AudioObjectPropertyAddress* a = q->address;
    switch (a->mSelector)
    {
        case kAudioObjectPropertyBaseClass:  FIXED (AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:      FIXED (AudioClassID, kAudioPlugInClassID);
        case kAudioObjectPropertyOwner:      FIXED (AudioObjectID, kAudioObjectUnknown);
        case kAudioObjectPropertyManufacturer: CFOBJ (CFRetain (CFSTR (kManufacturer)));
        case kAudioObjectPropertyName:       CFOBJ (CFRetain (CFSTR ("umleitung")));
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
        {
            AudioObjectID ids[kMaxDevices];
            for (UInt32 i = 0; i < gNumDevices; ++i) ids[i] = gOrder[i]->deviceID;
            return idList (q, ids, gNumDevices);
        }
        case kAudioPlugInPropertyBoxList:
        case kAudioPlugInPropertyClockDeviceList:
            return idList (q, NULL, 0);
        case kAudioPlugInPropertyTranslateUIDToDevice:
        {
            AudioObjectID found = kAudioObjectUnknown;
            if (q->out != NULL)
            {
                if (q->qualifierSize < sizeof (CFStringRef) || q->qualifier == NULL) return kAudioHardwareBadPropertySizeError;
                CFStringRef uid = *(const CFStringRef*) q->qualifier;
                Device* d = uid != NULL && CFGetTypeID (uid) == CFStringGetTypeID () ? findDeviceByUID (uid) : NULL;
                if (d != NULL) found = d->deviceID;
            }
            FIXED (AudioObjectID, found);
        }
        case kAudioPlugInPropertyTranslateUIDToBox:
        case kAudioPlugInPropertyTranslateUIDToClockDevice:
            FIXED (AudioObjectID, kAudioObjectUnknown);
        case kAudioPlugInPropertyResourceBundle: CFOBJ (CFRetain (CFSTR ("")));
        case kAudioObjectPropertyCustomPropertyInfoList:
        {
            AudioServerPlugInCustomPropertyInfo info[2] =
            {
                { kUmPropertyDevices, kAudioServerPlugInCustomPropertyDataTypeCFPropertyList, kAudioServerPlugInCustomPropertyDataTypeNone },
                { kUmPropertyVersion, kAudioServerPlugInCustomPropertyDataTypeCFString, kAudioServerPlugInCustomPropertyDataTypeNone }
            };
            if (q->out == NULL) { q->outSize = sizeof (info); return noErr; }
            UInt32 fit = q->inSize / (UInt32) sizeof (info[0]);
            if (fit > 2) fit = 2;
            memcpy (q->out, info, fit * sizeof (info[0]));
            q->outSize = fit * (UInt32) sizeof (info[0]);
            return noErr;
        }
        case kUmPropertyDevices: CFOBJ (copyDeviceList ());
        case kUmPropertyVersion: CFOBJ (CFRetain (gVersion ? gVersion : CFSTR ("0")));
    }
    return kAudioHardwareUnknownPropertyError;
}

static CFStringRef copyChannelName (const Device* d, UInt32 channel)
{
    if (d->channels == 2) return CFRetain (channel == 1 ? CFSTR ("Left") : CFSTR ("Right"));
    if (d->channels == 1) return CFRetain (CFSTR ("Mono"));
    return CFStringCreateWithFormat (NULL, NULL, CFSTR ("Channel %u"), (unsigned) channel);
}

static OSStatus channelLayout (Query* q, const Device* d)
{
    UInt32 size = (UInt32) offsetof (AudioChannelLayout, mChannelDescriptions) + d->channels * (UInt32) sizeof (AudioChannelDescription);
    if (q->out != NULL)
    {
        if (q->inSize < size) return kAudioHardwareBadPropertySizeError;
        AudioChannelLayout* l = (AudioChannelLayout*) q->out;
        memset (l, 0, size);
        l->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
        l->mNumberChannelDescriptions = d->channels;
        for (UInt32 i = 0; i < d->channels; ++i)
        {
            AudioChannelLabel label = kAudioChannelLabel_Discrete_0 + i;
            if (d->channels == 1) label = kAudioChannelLabel_Mono;
            else if (d->channels == 2) label = i == 0 ? kAudioChannelLabel_Left : kAudioChannelLabel_Right;
            l->mChannelDescriptions[i].mChannelLabel = label;
        }
    }
    q->outSize = size;
    return noErr;
}

static OSStatus deviceProperty (Query* q, Device* d)
{
    const AudioObjectPropertyAddress* a = q->address;
    const Boolean in = a->mScope == kAudioObjectPropertyScopeInput, out = a->mScope == kAudioObjectPropertyScopeOutput;
    const Boolean global = a->mScope == kAudioObjectPropertyScopeGlobal;
    switch (a->mSelector)
    {
        case kAudioObjectPropertyBaseClass:  FIXED (AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:      FIXED (AudioClassID, kAudioDeviceClassID);
        case kAudioObjectPropertyOwner:      FIXED (AudioObjectID, kAudioObjectPlugInObject);
        case kAudioObjectPropertyName:       CFOBJ (CFRetain (d->name));
        case kAudioObjectPropertyManufacturer: CFOBJ (CFRetain (CFSTR (kManufacturer)));
        case kAudioObjectPropertyModelName:  CFOBJ (CFRetain (CFSTR (kModelName)));
        case kAudioObjectPropertyOwnedObjects:
        {
            AudioObjectID ids[4]; UInt32 n = 0;
            if (global || in) ids[n++] = d->inStreamID;
            if (global || out) { ids[n++] = d->outStreamID; ids[n++] = d->volumeID; ids[n++] = d->muteID; }
            return idList (q, ids, n);
        }
        case kAudioDevicePropertyDeviceUID:  CFOBJ (CFRetain (d->uid));
        case kAudioDevicePropertyModelUID:   CFOBJ (CFRetain (CFSTR (kModelUID)));
        case kAudioDevicePropertyTransportType: FIXED (UInt32, kAudioDeviceTransportTypeVirtual);
        case kAudioDevicePropertyRelatedDevices: return idList (q, &d->deviceID, 1);
        case kAudioDevicePropertyClockDomain:  FIXED (UInt32, 0);
        case kAudioDevicePropertyDeviceIsAlive: FIXED (UInt32, 1);
        case kAudioDevicePropertyDeviceIsRunning: FIXED (UInt32, d->ioCount > 0 ? 1 : 0);
        case kAudioDevicePropertyDeviceCanBeDefaultDevice: FIXED (UInt32, d->hidden ? 0 : 1);
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: FIXED (UInt32, d->hidden ? 0 : 1);
        case kAudioDevicePropertyLatency:    FIXED (UInt32, 0);
        case kAudioDevicePropertySafetyOffset: FIXED (UInt32, 0);
        case kAudioDevicePropertyStreams:
        {
            AudioObjectID ids[2]; UInt32 n = 0;
            if (global || in) ids[n++] = d->inStreamID;
            if (global || out) ids[n++] = d->outStreamID;
            return idList (q, ids, n);
        }
        case kAudioObjectPropertyControlList:
        {
            AudioObjectID ids[2] = { d->volumeID, d->muteID };
            return idList (q, ids, 2);
        }
        case kAudioDevicePropertyNominalSampleRate: FIXED (Float64, d->sampleRate);
        case kAudioDevicePropertyAvailableNominalSampleRates:
        {
            if (q->out == NULL) { q->outSize = kNumSampleRates * (UInt32) sizeof (AudioValueRange); return noErr; }
            UInt32 fit = q->inSize / (UInt32) sizeof (AudioValueRange);
            if (fit > kNumSampleRates) fit = kNumSampleRates;
            AudioValueRange* r = (AudioValueRange*) q->out;
            for (UInt32 i = 0; i < fit; ++i) { r[i].mMinimum = kSampleRates[i]; r[i].mMaximum = kSampleRates[i]; }
            q->outSize = fit * (UInt32) sizeof (AudioValueRange);
            return noErr;
        }
        case kAudioDevicePropertyIsHidden:   FIXED (UInt32, d->hidden ? 1 : 0);
        case kAudioDevicePropertyZeroTimeStampPeriod: FIXED (UInt32, kZeroTimeStampPeriod);
        case kAudioDevicePropertyClockIsStable: FIXED (UInt32, 1);
        case kAudioDevicePropertyClockAlgorithm: FIXED (UInt32, kAudioDeviceClockAlgorithmRaw);
        case kAudioDevicePropertyPreferredChannelsForStereo:
        {
            if (q->out != NULL)
            {
                if (q->inSize < 2 * sizeof (UInt32)) return kAudioHardwareBadPropertySizeError;
                ((UInt32*) q->out)[0] = 1;
                ((UInt32*) q->out)[1] = d->channels >= 2 ? 2 : 1;
            }
            q->outSize = 2 * sizeof (UInt32);
            return noErr;
        }
        case kAudioDevicePropertyPreferredChannelLayout:
            return channelLayout (q, d);
        case kAudioObjectPropertyElementName:
        {
            if (! (in || out) || a->mElement < 1 || a->mElement > d->channels) return kAudioHardwareUnknownPropertyError;
            CFOBJ (q->out ? copyChannelName (d, a->mElement) : NULL);
        }
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus streamProperty (Query* q, Device* d, Boolean input)
{
    const AudioObjectPropertyAddress* a = q->address;
    switch (a->mSelector)
    {
        case kAudioObjectPropertyBaseClass:  FIXED (AudioClassID, kAudioObjectClassID);
        case kAudioObjectPropertyClass:      FIXED (AudioClassID, kAudioStreamClassID);
        case kAudioObjectPropertyOwner:      FIXED (AudioObjectID, d->deviceID);
        case kAudioObjectPropertyOwnedObjects: return idList (q, NULL, 0);
        case kAudioObjectPropertyName:       CFOBJ (CFRetain (input ? CFSTR ("umleitung in") : CFSTR ("umleitung out")));
        case kAudioStreamPropertyIsActive:   FIXED (UInt32, (input ? d->inActive : d->outActive) ? 1 : 0);
        case kAudioStreamPropertyDirection:  FIXED (UInt32, input ? 1 : 0);
        case kAudioStreamPropertyTerminalType: FIXED (UInt32, kAudioStreamTerminalTypeLine);
        case kAudioStreamPropertyStartingChannel: FIXED (UInt32, 1);
        case kAudioStreamPropertyLatency:    FIXED (UInt32, 0);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
        {
            if (q->out != NULL)
            {
                if (q->inSize < sizeof (AudioStreamBasicDescription)) return kAudioHardwareBadPropertySizeError;
                fillASBD (d, (AudioStreamBasicDescription*) q->out);
            }
            q->outSize = sizeof (AudioStreamBasicDescription);
            return noErr;
        }
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
        {
            if (q->out == NULL) { q->outSize = kNumSampleRates * (UInt32) sizeof (AudioStreamRangedDescription); return noErr; }
            UInt32 fit = q->inSize / (UInt32) sizeof (AudioStreamRangedDescription);
            if (fit > kNumSampleRates) fit = kNumSampleRates;
            AudioStreamRangedDescription* r = (AudioStreamRangedDescription*) q->out;
            for (UInt32 i = 0; i < fit; ++i)
            {
                fillASBD (d, &r[i].mFormat);
                r[i].mFormat.mSampleRate = kSampleRates[i];
                r[i].mSampleRateRange.mMinimum = r[i].mSampleRateRange.mMaximum = kSampleRates[i];
            }
            q->outSize = fit * (UInt32) sizeof (AudioStreamRangedDescription);
            return noErr;
        }
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus volumeProperty (Query* q, Device* d)
{
    const AudioObjectPropertyAddress* a = q->address;
    switch (a->mSelector)
    {
        case kAudioObjectPropertyBaseClass:  FIXED (AudioClassID, kAudioLevelControlClassID);
        case kAudioObjectPropertyClass:      FIXED (AudioClassID, kAudioVolumeControlClassID);
        case kAudioObjectPropertyOwner:      FIXED (AudioObjectID, d->deviceID);
        case kAudioObjectPropertyOwnedObjects: return idList (q, NULL, 0);
        case kAudioControlPropertyScope:     FIXED (AudioObjectPropertyScope, kAudioObjectPropertyScopeOutput);
        case kAudioControlPropertyElement:   FIXED (AudioObjectPropertyElement, kAudioObjectPropertyElementMain);
        case kAudioLevelControlPropertyScalarValue: FIXED (Float32, atomic_load (&d->volume));
        case kAudioLevelControlPropertyDecibelValue: FIXED (Float32, scalarToDB (atomic_load (&d->volume)));
        case kAudioLevelControlPropertyDecibelRange:
        {
            AudioValueRange r = { kVolumeMinDB, kVolumeMaxDB };
            FIXED (AudioValueRange, r);
        }
        case kAudioLevelControlPropertyConvertScalarToDecibels:
            FIXED (Float32, scalarToDB (*(Float32*) q->out));     // in place: the value to convert comes in out
        case kAudioLevelControlPropertyConvertDecibelsToScalar:
            FIXED (Float32, dbToScalar (*(Float32*) q->out));
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus muteProperty (Query* q, Device* d)
{
    const AudioObjectPropertyAddress* a = q->address;
    switch (a->mSelector)
    {
        case kAudioObjectPropertyBaseClass:  FIXED (AudioClassID, kAudioBooleanControlClassID);
        case kAudioObjectPropertyClass:      FIXED (AudioClassID, kAudioMuteControlClassID);
        case kAudioObjectPropertyOwner:      FIXED (AudioObjectID, d->deviceID);
        case kAudioObjectPropertyOwnedObjects: return idList (q, NULL, 0);
        case kAudioControlPropertyScope:     FIXED (AudioObjectPropertyScope, kAudioObjectPropertyScopeOutput);
        case kAudioControlPropertyElement:   FIXED (AudioObjectPropertyElement, kAudioObjectPropertyElementMain);
        case kAudioBooleanControlPropertyValue: FIXED (UInt32, atomic_load (&d->mute));
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus query (Query* q)       // under lock
{
    if (q->object == kAudioObjectPlugInObject) return pluginProperty (q);
    Device* d = findDevice (q->object);
    if (d == NULL) return kAudioHardwareBadObjectError;
    if (q->object == d->deviceID)    return deviceProperty (q, d);
    if (q->object == d->inStreamID)  return streamProperty (q, d, true);
    if (q->object == d->outStreamID) return streamProperty (q, d, false);
    if (q->object == d->volumeID)    return volumeProperty (q, d);
    if (q->object == d->muteID)      return muteProperty (q, d);
    return kAudioHardwareBadObjectError;
}

static Boolean um_HasProperty (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address)
{
    (void) pid;
    if (driver != gDriverRef || address == NULL) return false;
    Query q = { object, address, 0, NULL, 0, NULL, 0 };
    lock ();
    OSStatus err = query (&q);
    unlock ();
    return err == noErr;
}

static OSStatus um_IsPropertySettable (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, Boolean* outSettable)
{
    (void) pid;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    if (address == NULL || outSettable == NULL) return kAudioHardwareIllegalOperationError;
    Query q = { object, address, 0, NULL, 0, NULL, 0 };
    lock ();
    OSStatus err = query (&q);
    Boolean settable = false;
    if (err == noErr)
    {
        const AudioObjectPropertySelector s = address->mSelector;
        if (object == kAudioObjectPlugInObject) settable = s == kUmPropertyDevices;
        else
        {
            Device* d = findDevice (object);
            if (object == d->deviceID) settable = s == kAudioDevicePropertyNominalSampleRate;
            else if (object == d->inStreamID || object == d->outStreamID)
                settable = s == kAudioStreamPropertyIsActive || s == kAudioStreamPropertyVirtualFormat || s == kAudioStreamPropertyPhysicalFormat;
            else if (object == d->volumeID)
                settable = s == kAudioLevelControlPropertyScalarValue || s == kAudioLevelControlPropertyDecibelValue;
            else if (object == d->muteID)
                settable = s == kAudioBooleanControlPropertyValue;
        }
    }
    unlock ();
    *outSettable = settable;
    return err;
}

static OSStatus um_GetPropertyDataSize (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualifierSize, const void* qualifier, UInt32* outSize)
{
    (void) pid;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    if (address == NULL || outSize == NULL) return kAudioHardwareIllegalOperationError;
    Query q = { object, address, qualifierSize, qualifier, 0, NULL, 0 };
    lock ();
    OSStatus err = query (&q);
    unlock ();
    if (err == noErr) *outSize = q.outSize;
    return err;
}

static OSStatus um_GetPropertyData (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualifierSize, const void* qualifier, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
    (void) pid;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    if (address == NULL || outDataSize == NULL || outData == NULL) return kAudioHardwareIllegalOperationError;
    Query q = { object, address, qualifierSize, qualifier, inDataSize, outData, 0 };
    lock ();
    OSStatus err = query (&q);
    unlock ();
    if (err == noErr) *outDataSize = q.outSize;
    return err;
}

static OSStatus requestRate (Device* d, Float64 rate)       // under lock
{
    if (! isSupportedRate (rate)) return kAudioDeviceUnsupportedFormatError;
    if (fabs (rate - d->sampleRate) < 0.5) return noErr;
    if (gHost == NULL) return kAudioHardwareNotRunningError;
    AudioObjectID id = d->deviceID;
    AudioServerPlugInHostRef host = gHost;
    unlock ();                       // the host may perform the change right away (on another thread or this one)
    OSStatus err = host->RequestDeviceConfigurationChange (host, id, (UInt64) rate, NULL);
    lock ();
    return err;
}

static OSStatus um_SetPropertyData (AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t pid, const AudioObjectPropertyAddress* address, UInt32 qualifierSize, const void* qualifier, UInt32 inDataSize, const void* inData)
{
    (void) pid; (void) qualifierSize; (void) qualifier;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    if (address == NULL || inData == NULL) return kAudioHardwareIllegalOperationError;
    const AudioObjectPropertySelector s = address->mSelector;

    Notes* local = (Notes*) malloc (sizeof (Notes));     // not the IO path
    if (local == NULL) return kAudioHardwareUnspecifiedError;
    local->count = 0;
    OSStatus err = kAudioHardwareUnknownPropertyError;
    RateChange rates[kMaxDevices];
    UInt32 numRates = 0;

    lock ();
    if (object == kAudioObjectPlugInObject)
    {
        if (s == kUmPropertyDevices)
        {
            if (inDataSize < sizeof (CFPropertyListRef)) err = kAudioHardwareBadPropertySizeError;
            else
            {
                DeviceSpec* specs = NULL; UInt32 n = 0;
                if (! parseSpecs (*(const CFPropertyListRef*) inData, &specs, &n)) err = kAudioHardwareIllegalOperationError;
                else
                {
                    numRates = collectRateChanges (specs, n, rates);
                    fillMissingRates (specs, n);
                    err = applySpecs (specs, n, local);
                    saveDeviceList ();
                    freeSpecs (specs, n);
                }
            }
        }
        else if (s == kUmPropertyVersion) err = kAudioHardwareUnsupportedOperationError;
    }
    else
    {
        Device* d = findDevice (object);
        if (d == NULL) err = kAudioHardwareBadObjectError;
        else if (object == d->deviceID)
        {
            if (s == kAudioDevicePropertyNominalSampleRate)
                err = inDataSize < sizeof (Float64) ? kAudioHardwareBadPropertySizeError : requestRate (d, *(const Float64*) inData);
        }
        else if (object == d->inStreamID || object == d->outStreamID)
        {
            const Boolean input = object == d->inStreamID;
            if (s == kAudioStreamPropertyIsActive)
            {
                if (inDataSize < sizeof (UInt32)) err = kAudioHardwareBadPropertySizeError;
                else
                {
                    Boolean v = *(const UInt32*) inData != 0;
                    Boolean* field = input ? &d->inActive : &d->outActive;
                    if (*field != v) { *field = v; addNote (local, object, kAudioStreamPropertyIsActive, kAudioObjectPropertyScopeGlobal); }
                    err = noErr;
                }
            }
            else if (s == kAudioStreamPropertyVirtualFormat || s == kAudioStreamPropertyPhysicalFormat)
            {
                if (inDataSize < sizeof (AudioStreamBasicDescription)) err = kAudioHardwareBadPropertySizeError;
                else
                {
                    const AudioStreamBasicDescription* f = (const AudioStreamBasicDescription*) inData;
                    AudioStreamBasicDescription mine; fillASBD (d, &mine);
                    if (f->mFormatID != mine.mFormatID || f->mFormatFlags != mine.mFormatFlags || f->mBitsPerChannel != mine.mBitsPerChannel
                        || f->mChannelsPerFrame != mine.mChannelsPerFrame || f->mBytesPerFrame != mine.mBytesPerFrame
                        || f->mFramesPerPacket != mine.mFramesPerPacket)
                        err = kAudioDeviceUnsupportedFormatError;
                    else err = requestRate (d, f->mSampleRate);
                }
            }
        }
        else if (object == d->volumeID)
        {
            if (s == kAudioLevelControlPropertyScalarValue || s == kAudioLevelControlPropertyDecibelValue)
            {
                if (inDataSize < sizeof (Float32)) err = kAudioHardwareBadPropertySizeError;
                else
                {
                    Float32 v = *(const Float32*) inData;
                    if (s == kAudioLevelControlPropertyDecibelValue) v = dbToScalar (v);
                    if (! (v >= 0.0f)) v = 0.0f;       // also NaN
                    if (v > 1.0f) v = 1.0f;
                    if (v != atomic_load (&d->volume))
                    {
                        atomic_store (&d->volume, v);
                        addNote (local, object, kAudioLevelControlPropertyScalarValue, kAudioObjectPropertyScopeGlobal);
                        addNote (local, object, kAudioLevelControlPropertyDecibelValue, kAudioObjectPropertyScopeGlobal);
                    }
                    err = noErr;
                }
            }
        }
        else if (object == d->muteID)
        {
            if (s == kAudioBooleanControlPropertyValue)
            {
                if (inDataSize < sizeof (UInt32)) err = kAudioHardwareBadPropertySizeError;
                else
                {
                    UInt32 v = *(const UInt32*) inData != 0;
                    if (v != atomic_load (&d->mute))
                    {
                        atomic_store (&d->mute, v);
                        addNote (local, object, kAudioBooleanControlPropertyValue, kAudioObjectPropertyScopeGlobal);
                    }
                    err = noErr;
                }
            }
        }
    }
    unlock ();
    sendNotes (local);
    free (local);
    for (UInt32 i = 0; i < numRates; ++i)
        if (gHost != NULL) gHost->RequestDeviceConfigurationChange (gHost, rates[i].device, (UInt64) rates[i].rate, NULL);
    return err;
}

// ---------------------------------------------------------------------------------------------------------------------
#pragma mark IO

static OSStatus um_StartIO (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID)
{
    (void) clientID;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    lock ();
    Device* d = findDevice (deviceID);
    if (d == NULL || d->deviceID != deviceID) { unlock (); return kAudioHardwareBadDeviceError; }
    if (d->ioCount == 0)
    {
        d->anchorHostTime = mach_absolute_time ();
        d->timeStampCount = 0;
        d->written = false;
        d->writeHigh = 0;
    }
    d->ioCount++;
    unlock ();
    return noErr;
}

static OSStatus um_StopIO (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID)
{
    (void) clientID;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    lock ();
    Device* d = findDevice (deviceID);
    if (d != NULL && d->deviceID == deviceID)
    {
        if (d->ioCount > 0) d->ioCount--;
        unlock ();
        return noErr;
    }
    for (Device** z = &gZombies; *z != NULL; z = &(*z)->nextZombie)
    {
        if ((*z)->deviceID != deviceID) continue;
        Device* dead = *z;
        if (dead->ioCount > 0) dead->ioCount--;
        if (dead->ioCount == 0) { *z = dead->nextZombie; freeDevice (dead); }
        unlock ();
        return noErr;
    }
    unlock ();
    return kAudioHardwareBadDeviceError;
}

static OSStatus um_GetZeroTimeStamp (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed)
{
    (void) clientID;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    Device* d = findDeviceForIO (deviceID);
    if (d == NULL) return kAudioHardwareBadDeviceError;
    const Float64 ticksPerPeriod = d->hostTicksPerFrame * (Float64) kZeroTimeStampPeriod;
    const UInt64 now = mach_absolute_time ();
    // move on as many whole periods as have passed (normally at most one per call)
    while ((Float64) d->anchorHostTime + (Float64) (d->timeStampCount + 1) * ticksPerPeriod <= (Float64) now)
        d->timeStampCount++;
    *outSampleTime = (Float64) (d->timeStampCount * kZeroTimeStampPeriod);
    *outHostTime = d->anchorHostTime + (UInt64) ((Float64) d->timeStampCount * ticksPerPeriod);
    *outSeed = d->seed;
    return noErr;
}

static OSStatus um_WillDoIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID, UInt32 operationID, Boolean* outWillDo, Boolean* outWillDoInPlace)
{
    (void) deviceID; (void) clientID;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    const Boolean will = operationID == kAudioServerPlugInIOOperationReadInput || operationID == kAudioServerPlugInIOOperationWriteMix;
    if (outWillDo) *outWillDo = will;
    if (outWillDoInPlace) *outWillDoInPlace = true;
    return noErr;
}

static OSStatus um_BeginIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID, UInt32 operationID, UInt32 frames, const AudioServerPlugInIOCycleInfo* info)
{
    (void) deviceID; (void) clientID; (void) operationID; (void) frames; (void) info;
    return driver == gDriverRef ? noErr : kAudioHardwareBadObjectError;
}

static OSStatus um_EndIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, UInt32 clientID, UInt32 operationID, UInt32 frames, const AudioServerPlugInIOCycleInfo* info)
{
    (void) deviceID; (void) clientID; (void) operationID; (void) frames; (void) info;
    return driver == gDriverRef ? noErr : kAudioHardwareBadObjectError;
}

// frames at sample time t live at ring[(t & mask) × channels]. Valid frames: [writeHigh − ring, writeHigh).
// The first write of a frame (t ≥ writeHigh) replaces what the ring held, later writes of it add (mix).
static void ringWrite (Device* d, SInt64 t0, UInt32 frames, const Float32* src, Float32 gain)
{
    const UInt32 ch = d->channels;
    if (frames > kRingFrames) { src += (size_t) (frames - kRingFrames) * ch; t0 += frames - kRingFrames; frames = kRingFrames; }
    if (! d->written) { d->writeHigh = t0; d->written = true; }
    if (t0 > d->writeHigh)                      // a gap: what was there is stale, make it silence
    {
        SInt64 from = d->writeHigh;
        if (t0 - from > kRingFrames) from = t0 - kRingFrames;
        for (SInt64 t = from; t < t0; ++t) memset (d->ring + (size_t) ((UInt64) t & kRingMask) * ch, 0, ch * sizeof (Float32));
        d->writeHigh = t0;
    }
    const SInt64 low = d->writeHigh - kRingFrames;
    for (UInt32 i = 0; i < frames; ++i)
    {
        const SInt64 t = t0 + i;
        if (t < low) continue;                  // too old: its slot belongs to a newer frame
        Float32* dst = d->ring + (size_t) ((UInt64) t & kRingMask) * ch;
        const Float32* s = src + (size_t) i * ch;
        if (t < d->writeHigh) for (UInt32 c = 0; c < ch; ++c) dst[c] += s[c] * gain;
        else                  for (UInt32 c = 0; c < ch; ++c) dst[c]  = s[c] * gain;
    }
    if (t0 + frames > d->writeHigh) d->writeHigh = t0 + frames;
}

static void ringRead (Device* d, SInt64 t0, UInt32 frames, Float32* dst)
{
    const UInt32 ch = d->channels;
    const SInt64 low = d->writeHigh - kRingFrames, high = d->writeHigh;
    for (UInt32 i = 0; i < frames; ++i)
    {
        const SInt64 t = t0 + i;
        Float32* o = dst + (size_t) i * ch;
        if (d->written && t >= low && t < high) memcpy (o, d->ring + (size_t) ((UInt64) t & kRingMask) * ch, ch * sizeof (Float32));
        else memset (o, 0, ch * sizeof (Float32));
    }
}

static OSStatus um_DoIOOperation (AudioServerPlugInDriverRef driver, AudioObjectID deviceID, AudioObjectID streamID, UInt32 clientID, UInt32 operationID, UInt32 frames, const AudioServerPlugInIOCycleInfo* info, void* mainBuffer, void* secondaryBuffer)
{
    (void) clientID; (void) secondaryBuffer;
    if (driver != gDriverRef) return kAudioHardwareBadObjectError;
    Device* d = findDeviceForIO (deviceID);
    if (d == NULL) return kAudioHardwareBadDeviceError;
    if (mainBuffer == NULL || info == NULL) return kAudioHardwareIllegalOperationError;

    if (operationID == kAudioServerPlugInIOOperationReadInput)
    {
        if (streamID != d->inStreamID) return kAudioHardwareBadStreamError;
        ringRead (d, (SInt64) floor (info->mInputTime.mSampleTime), frames, (Float32*) mainBuffer);
    }
    else if (operationID == kAudioServerPlugInIOOperationWriteMix)
    {
        if (streamID != d->outStreamID) return kAudioHardwareBadStreamError;
        const Float32 v = atomic_load_explicit (&d->volume, memory_order_relaxed);
        const Float32 gain = atomic_load_explicit (&d->mute, memory_order_relaxed) ? 0.0f : v * v;
        ringWrite (d, (SInt64) floor (info->mOutputTime.mSampleTime), frames, (const Float32*) mainBuffer, gain);
    }
    return noErr;
}

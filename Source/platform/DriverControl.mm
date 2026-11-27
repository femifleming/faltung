// umleitung — DriverControl: finds umleitung's HAL driver, reads and sets its device list ('umdv' on the plug-in
// object), installs / removes it. CoreFoundation + CoreAudio + JUCE only (no Objective-C objects), so it builds the
// same with or without ARC.
#include "DriverControl.h"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <set>

namespace um
{
namespace
{
constexpr const char* kBundleID     = "com.faltung.umleitung.driver";
constexpr const char* kDriverName   = "umleitung.driver";
constexpr const char* kHALFolder    = "/Library/Audio/Plug-Ins/HAL";
constexpr AudioObjectPropertySelector kDevicesProperty = 'umdv';   // CFArray of {uid, name, channels, hidden, rate}
constexpr AudioObjectPropertySelector kVersionProperty = 'umvr';   // CFString
constexpr int kMaxDevices = 256, kMaxChannels = 256;

AudioObjectPropertyAddress globalAddress (AudioObjectPropertySelector s)
{
    return { s, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
}

juce::String statusText (OSStatus err)
{
    const UInt32 be = CFSwapInt32HostToBig ((UInt32) err);
    char c[5] = {};
    memcpy (c, &be, 4);
    bool printable = true;
    for (int i = 0; i < 4; ++i) printable = printable && c[i] >= 32 && c[i] < 127;
    return printable ? juce::String ((int) err) + " ('" + juce::String (c) + "')" : juce::String ((int) err);
}

AudioObjectID findPlugIn()
{
    CFStringRef bundleID = CFStringCreateWithCString (nullptr, kBundleID, kCFStringEncodingUTF8);
    AudioObjectID plugIn = kAudioObjectUnknown;
    AudioValueTranslation t { &bundleID, sizeof (bundleID), &plugIn, sizeof (plugIn) };
    UInt32 size = sizeof (t);
    auto addr = globalAddress (kAudioHardwarePropertyPlugInForBundleID);
    const OSStatus err = AudioObjectGetPropertyData (kAudioObjectSystemObject, &addr, 0, nullptr, &size, &t);
    CFRelease (bundleID);
    return err == noErr ? plugIn : kAudioObjectUnknown;
}

juce::String stringFrom (CFTypeRef v)
{
    if (v == nullptr || CFGetTypeID (v) != CFStringGetTypeID()) return {};
    return juce::String::fromCFString ((CFStringRef) v);
}

juce::String versionOfBundleAt (const juce::File& bundle)
{
    const auto plist = bundle.getChildFile ("Contents/Info.plist");
    juce::MemoryBlock data;
    if (! plist.loadFileAsData (data)) return {};
    CFDataRef cfData = CFDataCreate (nullptr, (const UInt8*) data.getData(), (CFIndex) data.getSize());
    CFPropertyListRef dict = CFPropertyListCreateWithData (nullptr, cfData, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease (cfData);
    juce::String version;
    if (dict != nullptr && CFGetTypeID (dict) == CFDictionaryGetTypeID())
        version = stringFrom (CFDictionaryGetValue ((CFDictionaryRef) dict, CFSTR ("CFBundleShortVersionString")));
    if (dict != nullptr) CFRelease (dict);
    return version;
}

// runs a shell command as root through the system's administrator prompt (osascript's "with administrator
// privileges"); false and why when it fails or the user cancels
bool runAsAdministrator (const juce::String& shellCommand, juce::String& error)
{
    auto appleScriptString = shellCommand.replace ("\\", "\\\\").replace ("\"", "\\\"");
    juce::StringArray args { "/usr/bin/osascript", "-e",
                             "do shell script \"" + appleScriptString + "\" with administrator privileges" };
    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "could not run osascript";
        return false;
    }
    const auto output = p.readAllProcessOutput();
    p.waitForProcessToFinish (-1);
    const auto code = p.getExitCode();
    if (code == 0) return true;
    if (output.contains ("-128")) error = "cancelled";
    else error = output.trim().isNotEmpty() ? output.trim() : "the administrator command failed (" + juce::String ((int) code) + ")";
    return false;
}

juce::String shellQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "'\\''") + "'";
}

// coreaudiod's restart: launchctl's kickstart (current systems), killall as the fallback
const char* kRestartCoreAudio = "(/bin/launchctl kickstart -kp system/com.apple.audio.coreaudiod || /usr/bin/killall coreaudiod)";

bool waitFor (bool wantInstalled, int milliseconds)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (DriverControl::installed() == wantInstalled) return true;
        juce::Thread::sleep (250);
    }
    return DriverControl::installed() == wantInstalled;
}
} // namespace

bool DriverControl::installed()
{
    return findPlugIn() != kAudioObjectUnknown;
}

juce::String DriverControl::installedVersion()
{
    const auto plugIn = findPlugIn();
    if (plugIn == kAudioObjectUnknown) return {};
    auto addr = globalAddress (kVersionProperty);
    CFStringRef v = nullptr;
    UInt32 size = sizeof (v);
    if (AudioObjectHasProperty (plugIn, &addr)
        && AudioObjectGetPropertyData (plugIn, &addr, 0, nullptr, &size, &v) == noErr && v != nullptr)
    {
        auto s = stringFrom (v);
        CFRelease (v);
        if (s.isNotEmpty()) return s;
    }
    return versionOfBundleAt (juce::File (kHALFolder).getChildFile (kDriverName));
}

juce::String DriverControl::bundledVersion()
{
    const auto f = bundledDriver();
    return f.isDirectory() ? versionOfBundleAt (f) : juce::String();
}

std::vector<VirtualDevice> DriverControl::devices()
{
    std::vector<VirtualDevice> result;
    const auto plugIn = findPlugIn();
    if (plugIn == kAudioObjectUnknown) return result;
    auto addr = globalAddress (kDevicesProperty);
    CFPropertyListRef list = nullptr;
    UInt32 size = sizeof (list);
    if (AudioObjectGetPropertyData (plugIn, &addr, 0, nullptr, &size, &list) != noErr || list == nullptr) return result;
    if (CFGetTypeID (list) == CFArrayGetTypeID())
    {
        const auto n = CFArrayGetCount ((CFArrayRef) list);
        for (CFIndex i = 0; i < n; ++i)
        {
            auto item = CFArrayGetValueAtIndex ((CFArrayRef) list, i);
            if (item == nullptr || CFGetTypeID (item) != CFDictionaryGetTypeID()) continue;
            auto dict = (CFDictionaryRef) item;
            VirtualDevice d;
            d.uid = stringFrom (CFDictionaryGetValue (dict, CFSTR ("uid")));
            d.name = stringFrom (CFDictionaryGetValue (dict, CFSTR ("name")));
            if (auto ch = CFDictionaryGetValue (dict, CFSTR ("channels")); ch != nullptr && CFGetTypeID (ch) == CFNumberGetTypeID())
                CFNumberGetValue ((CFNumberRef) ch, kCFNumberIntType, &d.channels);
            if (auto h = CFDictionaryGetValue (dict, CFSTR ("hidden")); h != nullptr && CFGetTypeID (h) == CFBooleanGetTypeID())
                d.hidden = CFBooleanGetValue ((CFBooleanRef) h);
            result.push_back (d);
        }
    }
    CFRelease (list);
    return result;
}

bool DriverControl::setDevices (const std::vector<VirtualDevice>& list, juce::String& error)
{
    if ((int) list.size() > kMaxDevices) { error = "at most " + juce::String (kMaxDevices) + " devices"; return false; }
    std::set<juce::String> uids;
    for (const auto& d : list)
    {
        if (d.channels < 1 || d.channels > kMaxChannels)
            { error = "\"" + d.name + "\": channels must be 1 to " + juce::String (kMaxChannels); return false; }
        if (d.uid.isNotEmpty() && ! uids.insert (d.uid).second)
            { error = "two devices have the UID " + d.uid; return false; }
    }
    const auto plugIn = findPlugIn();
    if (plugIn == kAudioObjectUnknown) { error = "the umleitung driver is not installed"; return false; }

    CFMutableArrayRef array = CFArrayCreateMutable (nullptr, (CFIndex) list.size(), &kCFTypeArrayCallBacks);
    for (const auto& d : list)
    {
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable (nullptr, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (d.uid.isNotEmpty())
        {
            CFStringRef uid = d.uid.toCFString();
            CFDictionarySetValue (dict, CFSTR ("uid"), uid);
            CFRelease (uid);
        }
        CFStringRef name = (d.name.isNotEmpty() ? d.name : juce::String ("umleitung")).toCFString();
        CFDictionarySetValue (dict, CFSTR ("name"), name);
        CFRelease (name);
        int channels = d.channels;
        CFNumberRef ch = CFNumberCreate (nullptr, kCFNumberIntType, &channels);
        CFDictionarySetValue (dict, CFSTR ("channels"), ch);
        CFRelease (ch);
        CFDictionarySetValue (dict, CFSTR ("hidden"), d.hidden ? kCFBooleanTrue : kCFBooleanFalse);
        CFArrayAppendValue (array, dict);
        CFRelease (dict);
    }
    auto addr = globalAddress (kDevicesProperty);
    CFPropertyListRef value = array;
    const OSStatus err = AudioObjectSetPropertyData (plugIn, &addr, 0, nullptr, sizeof (value), &value);
    CFRelease (array);
    if (err != noErr) { error = "the driver refused the device list (" + statusText (err) + ")"; return false; }
    return true;
}

bool DriverControl::install (juce::String& error)
{
    const auto source = bundledDriver();
    if (! source.isDirectory()) { error = "the driver is missing from the app"; return false; }
    const auto target = juce::File (kHALFolder).getChildFile (kDriverName).getFullPathName();
    const juce::String command =
        "/bin/mkdir -p " + shellQuote (kHALFolder)
        + " && /bin/rm -rf " + shellQuote (target)
        + " && /usr/bin/ditto " + shellQuote (source.getFullPathName()) + " " + shellQuote (target)
        + " && /usr/sbin/chown -R root:wheel " + shellQuote (target)
        + " && /bin/chmod -R go-w " + shellQuote (target)
        + " && " + kRestartCoreAudio;
    if (! runAsAdministrator (command, error)) return false;
    if (! waitFor (true, 15000))
    {
        error = "the driver was copied, but Core Audio has not loaded it (yet)";
        return false;
    }
    return true;
}

bool DriverControl::uninstall (juce::String& error)
{
    const auto target = juce::File (kHALFolder).getChildFile (kDriverName).getFullPathName();
    const juce::String command = "/bin/rm -rf " + shellQuote (target) + " && " + kRestartCoreAudio;
    if (! runAsAdministrator (command, error)) return false;
    if (! waitFor (false, 15000))
    {
        error = "the driver was removed, but Core Audio still has it loaded";
        return false;
    }
    return true;
}

juce::File DriverControl::bundledDriver()
{
    juce::Array<juce::File> candidates;
    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    if (app.getFileExtension() == ".app") candidates.add (app.getChildFile ("Contents/Resources").getChildFile (kDriverName));
    candidates.add (exe.getSiblingFile (kDriverName));
    candidates.add (exe.getParentDirectory().getSiblingFile ("Resources").getChildFile (kDriverName));
    // the build folder: build/driver/umleitung.driver, whichever folder below build the program runs from
    for (auto dir = exe.getParentDirectory(); dir.getParentDirectory() != dir; dir = dir.getParentDirectory())
        candidates.add (dir.getChildFile ("driver").getChildFile (kDriverName));
   #ifdef UM_DRIVER_BUILD_PATH
    candidates.add (juce::File (UM_DRIVER_BUILD_PATH));
   #endif
    for (const auto& f : candidates)
        if (f.getChildFile ("Contents/Info.plist").existsAsFile()) return f;
    return {};
}
} // namespace um

#pragma once
// umleitung — virtual audio devices, like Loopback's: umleitung's own HAL driver (driver/, an AudioServerPlugIn
// installed in /Library/Audio/Plug-Ins/HAL) publishes any number of devices, each with a name and a channel count.
// Whatever any app plays into a device's output comes out of its input (a loopback), so umleitung can feed other
// apps (they pick the device as a microphone) and take sound from them (they pick it as their speaker).
// The app sets the list of devices through the driver's custom property; the driver keeps it across restarts.
#include <juce_core/juce_core.h>
#include <vector>

namespace um
{
struct VirtualDevice
{
    juce::String uid;                 // stable ("umleitung:<random>"); empty when making a new one: the driver picks one
    juce::String name = "umleitung";
    int channels = 2;                 // 1 … 256 in and out
    bool hidden = false;              // not offered to other apps (umleitung's own use)
    bool mirrorToAll = true;          // (reserved)
    bool operator== (const VirtualDevice& o) const { return uid == o.uid && name == o.name && channels == o.channels && hidden == o.hidden; }
};

class DriverControl
{
public:
    /** the driver is installed and loaded by coreaudiod */
    static bool installed();
    static juce::String installedVersion();     // empty when not installed
    static juce::String bundledVersion();       // the driver inside the app (to offer an update)
    /** the devices it publishes now */
    static std::vector<VirtualDevice> devices();
    /** publish exactly these (new ones appear, missing ones go, changed ones are rebuilt). False and why, if it cannot */
    static bool setDevices (const std::vector<VirtualDevice>&, juce::String& error);
    /** install / update / remove: asks for an administrator password (the system's own dialog), copies the driver and
        restarts coreaudiod (every app's sound stops for a moment). Message thread; blocks until done */
    static bool install (juce::String& error);
    static bool uninstall (juce::String& error);
    /** where the bundled driver is (inside the app's Resources, or the build folder) */
    static juce::File bundledDriver();
};
} // namespace um

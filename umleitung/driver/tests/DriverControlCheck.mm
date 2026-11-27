// Checks DriverControl without installing anything: finds the built driver, reads its version, asks the HAL whether
// the driver is loaded (read-only), and checks that bad device lists are refused before anything reaches the HAL.
// It never calls install() / uninstall().
#include "platform/DriverControl.h"
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (! (c)) { ++failures; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok: %s\n", msg); } while (0)

int main()
{
    const auto bundled = um::DriverControl::bundledDriver();
    std::printf ("bundled driver: %s\n", bundled.getFullPathName().toRawUTF8());
    CHECK (bundled.isDirectory(), "finds the built driver");
    CHECK (um::DriverControl::bundledVersion() == "1.0.0", "bundled version 1.0.0");

    const bool installed = um::DriverControl::installed();
    std::printf ("installed: %s (version '%s')\n", installed ? "yes" : "no", um::DriverControl::installedVersion().toRawUTF8());
    CHECK (installed == um::DriverControl::installedVersion().isNotEmpty(), "installed ⇔ a version");

    juce::String error;
    um::VirtualDevice bad; bad.channels = 0;
    CHECK (! um::DriverControl::setDevices ({ bad }, error) && error.contains ("channels"), "0 channels refused");
    um::VirtualDevice a; a.uid = "umleitung:x"; um::VirtualDevice b = a; b.name = "other";
    CHECK (! um::DriverControl::setDevices ({ a, b }, error) && error.contains ("UID"), "duplicate UIDs refused");
    if (! installed)
    {
        CHECK (um::DriverControl::devices().empty(), "no devices without the driver");
        CHECK (! um::DriverControl::setDevices ({ a }, error) && error.contains ("not installed"), "setDevices says it isn't installed");
    }
    std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

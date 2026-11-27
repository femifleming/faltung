#include "Apps.h"
#import <AppKit/AppKit.h>

namespace um
{
static juce::String str (NSString* s) { return s ? juce::String::fromUTF8 ([s UTF8String]) : juce::String(); }

std::vector<AppInfo> runningApps()
{
    std::vector<AppInfo> v;
    @autoreleasepool
    {
        for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
        {
            if (a.activationPolicy == NSApplicationActivationPolicyProhibited) continue;   // background agents
            if (a.processIdentifier == getpid() || a.bundleIdentifier == nil) continue;
            // menu-bar apps count, the system's own agents (accessibility, coreauthd, dock extras …) don't
            const auto where = str (a.bundleURL.path);
            if (a.activationPolicy == NSApplicationActivationPolicyAccessory && (where.startsWith ("/System/") || where.startsWith ("/usr/") || where.startsWith ("/Library/Apple/"))) continue;
            if (where.contains (".app/")) continue;   // a helper app inside another app (it counts as that app)
            v.push_back ({ str (a.bundleIdentifier), str (a.localizedName), str (a.bundleURL.path), a.processIdentifier });
        }
    }
    std::sort (v.begin(), v.end(), [] (const AppInfo& x, const AppInfo& y) { return x.name.compareIgnoreCase (y.name) < 0; });
    return v;
}

AppInfo owningApp (const juce::String& path)
{
    AppInfo r;
    const int i = path.indexOf (".app/");
    if (i < 0) return r;
    const auto appDir = path.substring (0, i + 4);
    @autoreleasepool
    {
        NSBundle* b = [NSBundle bundleWithPath: [NSString stringWithUTF8String: appDir.toRawUTF8()]];
        if (! b) return r;
        r.bundleId = str (b.bundleIdentifier);
        NSString* n = [b objectForInfoDictionaryKey: @"CFBundleDisplayName"];
        if (! n) n = [b objectForInfoDictionaryKey: @"CFBundleName"];
        r.name = n ? str (n) : juce::File (appDir).getFileNameWithoutExtension();
        r.path = appDir;
    }
    return r;
}

juce::String appPath (const juce::String& bundleId)
{
    @autoreleasepool
    {
        for (NSRunningApplication* a in [NSRunningApplication runningApplicationsWithBundleIdentifier: [NSString stringWithUTF8String: bundleId.toRawUTF8()]])
            if (a.bundleURL) return str (a.bundleURL.path);
        NSURL* u = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: [NSString stringWithUTF8String: bundleId.toRawUTF8()]];
        return u ? str (u.path) : juce::String();
    }
}
} // namespace um

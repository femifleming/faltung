#pragma once
// umleitung — apps as a person sees them: every running app (not only the ones that have opened audio yet), and the
// app a process belongs to (Chrome, Safari, Electron apps … play through helper processes inside their own bundle:
// "Google Chrome Helper" is Google Chrome).
#include <juce_core/juce_core.h>
#include <sys/types.h>
#include <vector>

namespace um
{
struct AppInfo { juce::String bundleId, name, path; pid_t pid = 0; };

/** the running apps with a window or a dock icon (and menu-bar apps), by name */
std::vector<AppInfo> runningApps();
/** the outermost .app a file lives in (empty when it isn't inside one) */
AppInfo owningApp (const juce::String& path);
/** where an app is installed (empty if unknown) */
juce::String appPath (const juce::String& bundleId);
} // namespace um

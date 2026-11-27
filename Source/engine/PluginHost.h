#pragma once
// umleitung — hosting VST3 and Audio Unit plug-ins in the signal path. Scans run in child processes (the app started
// again with --scan-plugin), so a plug-in that crashes while it is looked at is put on a list and skipped, and the
// app carries on. The list of what was found is kept in the app's folder.
#include <juce_audio_processors/juce_audio_processors.h>

namespace um
{
class PluginHost
{
public:
    explicit PluginHost (juce::File dataFolder);
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    juce::StringArray blacklisted() const;

    /** look for plug-ins in the usual folders (each file in a child process); progress (0..1, the file) on the
        message thread. Returns how many were found */
    int scan (std::function<void (float, const juce::String&)> progress = {}, std::function<bool()> cancelled = {});
    /** scan one file in a child process (the app itself with --scan-plugin) */
    bool scanFile (const juce::String& formatName, const juce::String& file, juce::String& error);
    /** the child's side: describe one file into an xml file; the return code is the process's */
    static int scanChild (const juce::String& formatName, const juce::String& file, const juce::File& out);
    /** the program the children run (the app itself; tests point it at their own executable) */
    static juce::File& scannerExecutable();

    /** an instance from a saved description (xml), prepared */
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& descriptionXml, double sampleRate, int blockSize, juce::String& error);
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);
    void save();
    juce::File listFile, blacklistFile;

private:
    juce::StringArray black;
};
} // namespace um

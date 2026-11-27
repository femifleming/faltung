#include "PluginHost.h"

namespace um
{
PluginHost::PluginHost (juce::File dataFolder)
{
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    dataFolder.createDirectory();
    listFile = dataFolder.getChildFile ("plugins.xml");
    blacklistFile = dataFolder.getChildFile ("plugins-blacklist.txt");
    if (auto x = juce::XmlDocument::parse (listFile)) known.recreateFromXml (*x);
    black = juce::StringArray::fromLines (blacklistFile.loadFileAsString()); black.removeEmptyStrings();
}
juce::StringArray PluginHost::blacklisted() const { return black; }
juce::File& PluginHost::scannerExecutable() { static juce::File f; return f; }
void PluginHost::save()
{
    if (auto x = known.createXml()) x->writeTo (listFile);
    blacklistFile.replaceWithText (black.joinIntoString ("\n"));
}

int PluginHost::scanChild (const juce::String& formatName, const juce::String& file, const juce::File& out)
{
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats()) if (f->getName() == formatName) f->findAllTypesForFile (found, file);
    juce::XmlElement x ("found");
    for (auto* d : found) x.addChildElement (d->createXml().release());
    return x.writeTo (out) ? 0 : 2;
}

bool PluginHost::scanFile (const juce::String& formatName, const juce::String& file, juce::String& error)
{
    auto exe = scannerExecutable();
    if (exe == juce::File()) exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto out = juce::File::createTempFile (".xml");
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { exe.getFullPathName(), "--scan-plugin", formatName, file, out.getFullPathName() }, 0)) { error = "could not start the scanner"; return false; }
    const bool done = p.waitForProcessToFinish (60000);
    if (! done) p.kill();
    const auto code = done ? p.getExitCode() : 999u;
    auto x = juce::XmlDocument::parse (out); out.deleteFile();
    if (! done || code != 0 || ! x)
    {
        black.addIfNotAlreadyThere (file); save();
        error = juce::File (file).getFileName() + (done ? " crashed while it was scanned" : " hung while it was scanned") + ": left out";
        return false;
    }
    for (auto* e : x->getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml (*e)) known.addType (d);
    }
    return true;
}

int PluginHost::scan (std::function<void (float, const juce::String&)> progress, std::function<bool()> cancelled)
{
    juce::StringArray todo, fmt;
    for (auto* f : formats.getFormats())
    {
        auto files = f->searchPathsForPlugins (f->getDefaultLocationsToSearch(), true, true);
        for (auto& p : files) if (! black.contains (p)) { todo.add (p); fmt.add (f->getName()); }
    }
    const int before = known.getNumTypes();
    for (int i = 0; i < todo.size(); ++i)
    {
        if (cancelled && cancelled()) break;
        bool already = false;
        for (auto& t : known.getTypes()) if (t.fileOrIdentifier == todo[i]) already = true;
        if (progress) progress ((float) i / (float) std::max (1, todo.size()), todo[i]);
        if (already) continue;
        juce::String err; scanFile (fmt[i], todo[i], err);
    }
    save();
    if (progress) progress (1.0f, {});
    return known.getNumTypes() - before;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::String& xml, double sr, int bs, juce::String& error)
{
    auto x = juce::XmlDocument::parse (xml);
    juce::PluginDescription d;
    if (! x || ! d.loadFromXml (*x)) { error = "no plug-in chosen"; return nullptr; }
    return create (d, sr, bs, error);
}
std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::PluginDescription& d, double sr, int bs, juce::String& error)
{
    auto p = formats.createPluginInstance (d, sr, bs, error);
    return p;
}
} // namespace um

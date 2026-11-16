#include "Library.h"
#include <juce_data_structures/juce_data_structures.h>
#include "EcrData.h"

namespace ecr
{
static juce::PropertiesFile::Options globalOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "ConvoSolution";
    o.filenameSuffix = ".settings";
    o.folderName = "ConvoSolution";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

juce::File IrLibrary::userRoot() const
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("CONVOSOLUTION_IR_ROOT", {});
    if (env.isNotEmpty()) return juce::File (env);

    juce::PropertiesFile props (globalOptions());
    const auto stored = props.getValue ("irRoot");
    if (stored.isNotEmpty() && juce::File::isAbsolutePath (stored)) return juce::File (stored);

    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto root = music.getChildFile ("ConvoSolution"), old = music.getChildFile ("ECR Convolver");
    if (! root.exists() && old.isDirectory()) old.moveFileTo (root);    // the plug-in's earlier name
    return root;
}

void IrLibrary::setUserRoot (const juce::File& f)
{
    juce::PropertiesFile props (globalOptions());
    props.setValue ("irRoot", f.getFullPathName());
    props.saveIfNeeded();
}

juce::File IrLibrary::sharedRoot()
{
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
        .getChildFile ("ConvoSolution").getChildFile ("IRs");
}

bool IrLibrary::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac") && ! f.getFileName().startsWithChar ('.');
}

juce::File IrLibrary::patchFile (const juce::String& id, const juce::File& audioFile) const
{
    // patches go next to the audio, except in linked folders that belong to another product
    if (audioFile != juce::File() && audioFile.getParentDirectory().hasWriteAccess() && ! audioFile.getParentDirectory().isSymbolicLink())
        return audioFile.withFileExtension ("ini");
    return userRoot().getChildFile ("Patches").getChildFile (juce::File::createLegalFileName (id.replaceCharacter ('/', '_')) + ".ini");
}

void IrLibrary::addFolder (const juce::File& dir, const juce::String& folderName, const juce::String& idPrefix)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    files.sort();
    for (const auto& f : files)
    {
        if (! isAudioFile (f)) continue;
        // "Name L" + "Name R" pairs are one true-stereo response: list it once, under the L file
        const auto stem = f.getFileNameWithoutExtension();
        if (stem.endsWith (" R") && f.getSiblingFile (stem.dropLastCharacters (1) + "L" + f.getFileExtension()).existsAsFile()) continue;
        LibEntry e;
        e.name = f.getFileName();
        e.folder = folderName;
        e.id = idPrefix + f.getFileName();
        e.file = f;
        if (indexOf (e.id) >= 0) continue;          // the user's folder wins over the shared one
        e.hasPatch = patchFile (e.id, f).existsAsFile();
        list.push_back (e);
    }
}

void IrLibrary::scan()
{
    list.clear();

    juce::StringArray internal;
    for (int i = 0; i < EcrData::namedResourceListSize; ++i)
    {
        const juce::String original (EcrData::originalFilenames[i]);
        if (original.endsWithIgnoreCase (".flac")) internal.add (original);
    }
    internal.sort (true);
    for (const auto& n : internal)
    {
        LibEntry e;
        e.name = n.upToLastOccurrenceOf (".", false, false) + ".wav";
        e.folder = "Internal";
        e.id = "Internal/" + n.upToLastOccurrenceOf (".", false, false);
        e.hasPatch = patchFile (e.id, {}).existsAsFile();
        list.push_back (e);
    }

    for (const auto& root : { userRoot(), sharedRoot() })
    {
        if (! root.isDirectory()) continue;
        addFolder (root, root.getFileName(), {});
        auto dirs = root.findChildFiles (juce::File::findDirectories, false);
        dirs.sort();
        for (const auto& d : dirs)
            if (! d.getFileName().startsWithChar ('.') && d.getFileName() != "Patches")
                addFolder (d, d.getFileName(), d.getFileName() + "/");
    }
}

int IrLibrary::indexOf (const juce::String& id) const
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) return (int) i;
    return -1;
}

int IrLibrary::nextFolderStart (int index, int direction) const
{
    const int n = (int) list.size();
    if (n == 0) return 0;
    index = juce::jlimit (0, n - 1, index);
    const auto here = list[(size_t) index].folder;
    if (direction > 0)
    {
        for (int i = index + 1; i < n; ++i)
            if (list[(size_t) i].folder != here) return i;
        return index;
    }
    // backwards: start of this folder, or of the previous one if already there
    int start = index;
    while (start > 0 && list[(size_t) start - 1].folder == here) --start;
    if (start != index || start == 0) return start;
    const auto prev = list[(size_t) start - 1].folder;
    int s = start - 1;
    while (s > 0 && list[(size_t) s - 1].folder == prev) --s;
    return s;
}

juce::StringPairArray readIni (const juce::File& f)
{
    juce::StringPairArray out;
    juce::StringArray lines;
    f.readLines (lines);
    for (const auto& line : lines)
    {
        const auto t = line.trim();
        if (t.isEmpty() || t.startsWithChar ('#') || t.startsWithChar (';') || t.startsWithChar ('[')) continue;
        const int eq = t.indexOfChar ('=');
        if (eq > 0) out.set (t.substring (0, eq).trim(), t.substring (eq + 1).trim());
    }
    return out;
}

bool writeIni (const juce::File& f, const juce::StringPairArray& values)
{
    juce::String text;
    for (const auto& k : values.getAllKeys())
        text << k << "=" << values[k] << "\n";
    return f.getParentDirectory().createDirectory().wasOk() && f.replaceWithText (text);
}
} // namespace ecr

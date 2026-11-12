// The flat list of responses the Select knob scrolls through: the built-in set
// first, then every audio file in the response folder and its sub-folders
// (one level deep, like the module's USB stick).
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace ecr
{
struct LibEntry
{
    juce::String name;      // file name
    juce::String folder;    // "Internal", or the folder name
    juce::String id;        // stable identifier stored in sessions
    juce::File file;        // empty for built-in responses
    bool hasPatch = false;  // a saved knob patch exists for it
};

class IrLibrary
{
public:
    void scan();
    const std::vector<LibEntry>& entries() const { return list; }
    int indexOf (const juce::String& id) const;
    int nextFolderStart (int index, int direction) const;

    juce::File userRoot() const;                    // writable: recordings, patches, settings
    void setUserRoot (const juce::File&);
    static juce::File sharedRoot();                 // optional machine-wide factory folder
    juce::File patchFile (const juce::String& id, const juce::File& audioFile) const;
    juce::File settingsFile() const { return userRoot().getChildFile ("convosolution-settings.ini"); }

    static bool isAudioFile (const juce::File&);

private:
    void addFolder (const juce::File& dir, const juce::String& folderName, const juce::String& idPrefix);
    std::vector<LibEntry> list;
};

// minimal "key=value" files, readable and editable by hand like the module's
juce::StringPairArray readIni (const juce::File&);
bool writeIni (const juce::File&, const juce::StringPairArray&);
} // namespace ecr

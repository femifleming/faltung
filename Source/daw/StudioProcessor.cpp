#include "StudioProcessor.h"
#include "StudioEditor.h"

namespace fal::daw
{
juce::AudioProcessor::BusesProperties StudioProcessor::buses()
{
    // (the standalone's callback hands processBlock a buffer of its own: inputs first, then the device outputs)
    return BusesProperties().withOutput ("main", juce::AudioChannelSet::stereo(), true);
}

StudioProcessor::StudioProcessor()
    : AudioProcessor (buses())
{
    session.onReplaced = [this] { projectReplaced(); };
    // a new window opens on the starter project so there is sound at once
    session.newFromTemplate (1);
    session.launchScene (4);
    history.reset();
    startTimerHz (10);
}
StudioProcessor::~StudioProcessor() { stopTimer(); }

// message thread: latency, midi learn, autosave
void StudioProcessor::timerCallback()
{
    const int lat = session.totalLatency();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples (lat); }
    session.midiLearnPoll();
    const auto now = juce::Time::currentTimeMillis();
    if (session.dirty.load() && now - lastAutosave > 60000) { autosave(); lastAutosave = now; }
    if (session.isRecording()) session.collectRecordedNotes();
    history.poll();
}

void StudioProcessor::prepareToPlay (double sr, int bs)
{
    session.prepare (sr, bs);
    lastLatency = session.totalLatency();
    setLatencySamples (lastLatency);
}
void StudioProcessor::releaseResources() { session.releaseResources(); }

bool StudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.inputBuses.isEmpty();
}

void StudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    HostInfo h;
    // the callback puts the device inputs first (numInputs channels), then the outputs
    const int ins = juce::jlimit (0, buffer.getNumChannels(), standaloneInputs.load());
    if (ins > 0)
    {
        for (int c = 0; c < ins && c < (int) inPtrs.size(); ++c) inPtrs[(size_t) c] = buffer.getReadPointer (c);
        h.in = inPtrs.data(); h.numIn = std::min (ins, (int) inPtrs.size());
        h.inputLatency = standaloneInputLatency.load();
    }
    Outs outs;
    // output channels follow the inputs in the callback buffer: pair p = channels 2p-1, 2p of the device
    for (int c = 0; c < std::min (buffer.getNumChannels() - ins, kOutChannels); ++c) outs.ch[(size_t) c] = buffer.getWritePointer (ins + c);
    for (int c = ins; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    // a count-in that ends inside this block: the clicks up to its end, the transport from there (to the sample)
    if (const auto ci = session.countInLeft.load(); ci > 0 && ci < n)
    {
        const int a = (int) ci;
        juce::MidiBuffer m1, m2;
        for (const auto ev : midi) (ev.samplePosition < a ? m1 : m2).addEvent (ev.getMessage(), ev.samplePosition < a ? ev.samplePosition : ev.samplePosition - a);
        session.process (m1, h, outs, a);
        Outs o2; for (size_t c = 0; c < o2.ch.size(); ++c) o2.ch[c] = outs.ch[c] ? outs.ch[c] + a : nullptr;
        HostInfo h2 = h; std::array<const float*, kMaxWidth * 2> in2 {};
        if (h.in) { for (int c = 0; c < h.numIn; ++c) in2[(size_t) c] = h.in[c] + a; h2.in = in2.data(); }
        session.process (m2, h2, o2, n - a);
    }
    else session.process (midi, h, outs, n);
    midi.clear();
    // midi clock out: 24 ticks per beat
    if (midiClockOut)
    {
        const double p = session.playPpq.load();
        if (p >= 0)
        {
            const double p1 = session.internalPpq.load();
            if (clockPpq < 0) { midiClockOut (juce::MidiMessage::midiStart()); clockPpq = p; }
            for (double t = std::ceil (clockPpq * 24.0); t < p1 * 24.0; t += 1.0) midiClockOut (juce::MidiMessage::midiClock());
            clockPpq = p1;
        }
        else if (clockPpq >= 0) { midiClockOut (juce::MidiMessage::midiStop()); clockPpq = -1; }
    }
    for (int c = ins; c < buffer.getNumChannels(); ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
    }
}

juce::AudioProcessorEditor* StudioProcessor::createEditor() { return new StudioEditor (*this); }

// (an AudioProcessor must offer these; in sndstudio they are the project as xml)
void StudioProcessor::getStateInformation (juce::MemoryBlock& dest) { auto x = session.save(); copyXmlToBinary (*x, dest); }
void StudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size)) { juce::String err; session.load (*x, &err); }
}

juce::File StudioProcessor::projectsDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (kDeep ? "faltung deep sndstudio" : "faltung sndstudio");
    d.createDirectory();
    return d;
}
bool StudioProcessor::saveProject (const juce::File& f, juce::String& error)
{
    session.projectName = f.getFileNameWithoutExtension();
    session.projectFile = f;
    auto x = session.save();
    // the version being replaced goes into the backups first (the last kBackups of them are kept)
    if (f.existsAsFile())
    {
        const auto dir = backupsDir (f); dir.createDirectory();
        const auto b = dir.getChildFile (f.getFileNameWithoutExtension() + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".fstudio");
        if (! b.existsAsFile()) f.copyFileTo (b);
        auto all = backups (f);
        for (int i = kBackups; i < all.size(); ++i) all[i].deleteFile();
    }
    // written beside, then moved over: a crash while saving never leaves half a project
    const auto tmp = f.getSiblingFile (f.getFileName() + ".saving");
    if (! x->writeTo (tmp) || ! tmp.moveFileTo (f)) { tmp.deleteFile(); error = "could not write " + f.getFullPathName(); return false; }
    session.dirty = false;
    return true;
}
juce::File StudioProcessor::backupsDir (const juce::File& project) { return project.getParentDirectory().getChildFile ("backups"); }
juce::Array<juce::File> StudioProcessor::backups (const juce::File& project)
{
    auto v = backupsDir (project).findChildFiles (juce::File::findFiles, false, project.getFileNameWithoutExtension() + " ????-??-?? ??????.fstudio");
    std::sort (v.begin(), v.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });   // newest first
    return v;
}
bool StudioProcessor::restoreBackup (const juce::File& backup, juce::String& error)
{
    // the backup's content, as the project it was a backup of (saving it again keeps today's version as a backup)
    const auto project = session.projectFile;
    auto x = juce::XmlDocument::parse (backup);
    if (! x) { error = "cannot read " + backup.getFullPathName(); return false; }
    if (! session.load (*x, &error)) return false;
    session.projectFile = project; session.projectName = project.getFileNameWithoutExtension(); session.dirty = true;
    return true;
}
bool StudioProcessor::loadProject (const juce::File& f, juce::String& error)
{
    auto x = juce::XmlDocument::parse (f);
    if (! x) { error = "cannot read " + f.getFullPathName(); return false; }
    session.projectFile = f; session.projectName = f.getFileNameWithoutExtension();
    if (! session.load (*x, &error)) return false;
    session.projectFile = f; session.projectName = f.getFileNameWithoutExtension(); session.dirty = false;
    return true;
}
void StudioProcessor::autosave()
{
    auto x = session.save();
    if (session.projectFile != juce::File()) x->setAttribute ("autosaveOf", session.projectFile.getFullPathName());
    const auto tmp = autosaveFile().getSiblingFile ("autosave.tmp");
    if (x->writeTo (tmp)) tmp.moveFileTo (autosaveFile());   // never half a file where the autosave was
}
bool StudioProcessor::recoverAutosave (juce::String& error)
{
    auto x = juce::XmlDocument::parse (autosaveFile());
    if (! x) { error = "no autosave"; return false; }
    const juce::File of (x->getStringAttribute ("autosaveOf"));
    session.load (*x, &error);
    session.projectFile = of.getFullPathName().isNotEmpty() ? of : juce::File();
    session.projectName = of.getFullPathName().isNotEmpty() ? of.getFileNameWithoutExtension() : x->getStringAttribute ("name", "recovered");
    session.dirty = true;
    history.reset();
    return true;
}
} // namespace fal::daw

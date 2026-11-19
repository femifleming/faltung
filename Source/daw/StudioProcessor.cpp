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
    if (session.dirty.load() && now - lastAutosave > 120000) { autosave(); lastAutosave = now; }
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
    session.process (midi, h, outs, n);
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
    if (! x->writeTo (f)) { error = "could not write " + f.getFullPathName(); return false; }
    session.dirty = false;
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
    x->writeTo (autosaveFile());
}
} // namespace fal::daw

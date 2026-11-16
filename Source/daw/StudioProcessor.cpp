#include "StudioProcessor.h"
#include "StudioEditor.h"

namespace fal::daw
{
juce::AudioProcessor::BusesProperties StudioProcessor::buses()
{
    BusesProperties b;
    const auto st = juce::AudioChannelSet::stereo();
    b = b.withOutput ("main", st, true);
    for (int p = 2; p <= kOutPairs; ++p) b = b.withOutput ("out " + juce::String (2 * p - 1) + "-" + juce::String (2 * p), st, false);
    return b;
}

juce::String StudioProcessor::hostParamId (int sel, int param)
{
    juce::String pre;
    if (sel == -100) pre = "m";
    else if (sel < 0) pre = "r" + juce::String (-sel);
    else pre = "t" + juce::String (sel + 1).paddedLeft ('0', 2);
    switch (param) { case MP_LEVEL: return pre + "level"; case MP_PAN: return pre + "pan"; case MP_MUTE: return pre + "mute"; default: return pre + "send" + juce::String (param - MP_SEND1 + 1); }
}

juce::AudioProcessorValueTreeState::ParameterLayout StudioProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto add = [&] (const juce::String& id, const juce::String& name, float def, std::function<juce::String (float)> text)
    {
        auto a = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([text] (float v, int) { return text (v); });
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def, a));
    };
    for (int i = 0; i < kMacros; ++i) add ("macro" + juce::String (i + 1), "macro " + juce::String (i + 1), 0.0f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
    add ("master", "master level", 0.8f, [] (float v) { return mixerParamText (MP_LEVEL, v); });
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const juce::String n = "track " + juce::String (t + 1) + " ";
        add (hostParamId (t, MP_LEVEL), n + "level", 0.8f, [] (float v) { return mixerParamText (MP_LEVEL, v); });
        add (hostParamId (t, MP_PAN), n + "pan", 0.5f, [] (float v) { return mixerParamText (MP_PAN, v); });
        add (hostParamId (t, MP_MUTE), n + "mute", 0.0f, [] (float v) { return mixerParamText (MP_MUTE, v); });
        for (int s = 0; s < 4; ++s) add (hostParamId (t, MP_SEND1 + s), n + "send " + juce::String (s + 1), 0.0f, [] (float v) { return mixerParamText (MP_SEND1, v); });
    }
    for (int r = 0; r < kReturns; ++r) add (hostParamId (-1 - r, MP_LEVEL), "return " + juce::String (r + 1) + " level", 0.8f, [] (float v) { return mixerParamText (MP_LEVEL, v); });
    return l;
}

StudioProcessor::StudioProcessor()
    : AudioProcessor (buses()), apvts (*this, nullptr, "studio", layout())
{
    buildBridges();
    session.onBlock = [this] { syncFromHost(); };
    startTimerHz (10);
}
StudioProcessor::~StudioProcessor() { stopTimer(); session.onBlock = nullptr; }

void StudioProcessor::buildBridges()
{
    bridges.clear();
    for (int i = 0; i < kMacros; ++i) bridges.push_back ({ apvts.getParameter ("macro" + juce::String (i + 1)), -200, GP_MACRO1 + i });
    bridges.push_back ({ apvts.getParameter ("master"), -200, GP_MASTER });
    for (int t = 0; t < kMaxTracks; ++t)
    {
        for (int p : { MP_LEVEL, MP_PAN, MP_MUTE }) bridges.push_back ({ apvts.getParameter (hostParamId (t, p)), t, p });
        for (int s = 0; s < 4; ++s) bridges.push_back ({ apvts.getParameter (hostParamId (t, MP_SEND1 + s)), t, MP_SEND1 + s });
    }
    for (int r = 0; r < kReturns; ++r) bridges.push_back ({ apvts.getParameter (hostParamId (-1 - r, MP_LEVEL)), -1 - r, MP_LEVEL });
    for (auto& b : bridges) b.lastHost = b.host->getValue();   // the project's values win until the host moves something
}

// audio thread, inside the session lock: host automation of the strips reaches the bases
void StudioProcessor::syncFromHost()
{
    for (auto& b : bridges)
    {
        const float hv = b.host->getValue();
        Device* d = b.trackSel == -200 ? session.global.get() : (session.trackAt (b.trackSel) ? session.trackAt (b.trackSel)->mixer.get() : nullptr);
        if (! d) continue;
        if (std::abs (hv - b.lastHost) > 1.0e-6f) { b.lastHost = hv; if (std::abs (hv - d->baseValue (b.param)) > 1.0e-6f) d->setBase (b.param, hv); b.lastBase = hv; }
    }
}

// message thread: strips moved in the studio reach the host (so Live can record them)
void StudioProcessor::timerCallback()
{
    for (auto& b : bridges)
    {
        Device* d = b.trackSel == -200 ? session.global.get() : (session.trackAt (b.trackSel) ? session.trackAt (b.trackSel)->mixer.get() : nullptr);
        if (! d) continue;
        const float bv = d->baseValue (b.param);
        if (std::abs (bv - b.lastBase) > 1.0e-6f && std::abs (bv - b.host->getValue()) > 1.0e-6f)
        {
            b.lastBase = bv; b.lastHost = bv;
            b.host->setValueNotifyingHost (bv);
        }
    }
    const int lat = session.totalLatency();
    if (lat != lastLatency) { lastLatency = lat; setLatencySamples (lat); }
    session.midiLearnPoll();
    const auto now = juce::Time::currentTimeMillis();
    if (session.dirty.load() && now - lastAutosave > 120000) { autosave(); lastAutosave = now; }
}

void StudioProcessor::prepareToPlay (double sr, int bs)
{
    session.prepare (sr, bs);
    standaloneScratch.assign ((size_t) (2 * kOutPairs * bs), 0.0f);
    lastLatency = session.totalLatency();
    setLatencySamples (lastLatency);
}
void StudioProcessor::releaseResources() { session.releaseResources(); }

bool StudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto st = juce::AudioChannelSet::stereo();
    if (l.getMainOutputChannelSet() != st) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != st) return false;
    for (auto& in : l.inputBuses) if (! in.isDisabled()) return false;
    return true;
}

void StudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    HostInfo h;
    h.standalone = isStandaloneApp.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) h.bpm = *b;
            h.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { h.ppq = *q; h.hasPpq = true; }
        }
    Outs outs;
    if (h.standalone)
    {
        // the device's channels in order: pair p = channels 2p-1, 2p
        for (int c = 0; c < std::min (buffer.getNumChannels(), 2 * kOutPairs); ++c) outs.ch[(size_t) c] = buffer.getWritePointer (c);
    }
    else
    {
        for (int b = 0; b < getBusCount (false) && b < kOutPairs; ++b)
        {
            auto* bus = getBus (false, b);
            if (! bus || ! bus->isEnabled()) continue;
            auto bb = getBusBuffer (buffer, false, b);
            if (bb.getNumChannels() >= 2) { outs.ch[(size_t) (2 * b)] = bb.getWritePointer (0); outs.ch[(size_t) (2 * b + 1)] = bb.getWritePointer (1); }
        }
    }
    session.process (midi, h, outs, n);
    midi.clear();
    // midi clock for the standalone: 24 ticks per beat
    if (h.standalone && midiClockOut)
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
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
    }
}

juce::AudioProcessorEditor* StudioProcessor::createEditor() { return new StudioEditor (*this); }

void StudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto x = session.save();
    copyXmlToBinary (*x, dest);
}
void StudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size)) { juce::String err; session.load (*x, &err); }
}

juce::File StudioProcessor::projectsDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (kDeep ? "faltung deep studio" : "faltung studio");
    d.createDirectory();
    return d;
}
bool StudioProcessor::saveProject (const juce::File& f, juce::String& error)
{
    session.projectName = f.getFileNameWithoutExtension();
    auto x = session.save();
    if (! x->writeTo (f)) { error = "could not write " + f.getFullPathName(); return false; }
    session.projectFile = f; session.dirty = false;
    return true;
}
bool StudioProcessor::loadProject (const juce::File& f, juce::String& error)
{
    auto x = juce::XmlDocument::parse (f);
    if (! x) { error = "cannot read " + f.getFullPathName(); return false; }
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

#include "PluginProcessor.h"

using namespace pt;

static std::unique_ptr<juce::RangedAudioParameter> makeParam (const juce::String& id, const juce::String& name, const PDef& d)
{
    juce::ParameterID pid (id, 1);
    switch (d.kind)
    {
        case I: return std::make_unique<juce::AudioParameterInt> (pid, name, (int) d.lo, (int) d.hi, (int) d.def);
        case B: return std::make_unique<juce::AudioParameterBool> (pid, name, d.def > 0.5f);
        case C: return std::make_unique<juce::AudioParameterChoice> (pid, name, juce::StringArray::fromTokens (d.choices, "|", ""), (int) d.def);
        default: return std::make_unique<juce::AudioParameterFloat> (pid, name, juce::NormalisableRange<float> (d.lo, d.hi), d.def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PartialsProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < GP::Count; ++i) l.add (makeParam (globalDefs[i].id, globalDefs[i].name, globalDefs[i]));
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < PP::Count; ++i)
            l.add (makeParam (partId (p, i), juce::String (p == 0 ? "A " : "B ") + partDefs[i].name, partDefs[i]));
    return l;
}

PartialsProcessor::PartialsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "params", layout())
{
    for (int i = 0; i < GP::Count; ++i) rawGlobal[i] = apvts.getRawParameterValue (globalDefs[i].id);
    for (int p = 0; p < 2; ++p)
    {
        for (int i = 0; i < PP::Count; ++i) rawPart[p][i] = apvts.getRawParameterValue (partId (p, i));
        editData[p] = std::make_unique<PartData>();
    }
    for (int i = 0; i < FXU::Count; ++i) { fxOrder[i] = i; fxOrderRT[i].store (i); }
    commit (0); commit (1);
}

void PartialsProcessor::commit (int part, int shapeChanged)
{
    if (shapeChanged >= 0) editData[part]->render (shapeChanged); else editData[part]->renderAll();
    auto snap = std::make_shared<const PartData> (*editData[part]);
    std::shared_ptr<const PartData> old;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        old = std::move (pending[part]);
        pending[part] = std::move (snap);
        pendingNew[part] = true;
    }
}

void PartialsProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    engine.prepare (sr);
    fx.prepare (sr, juce::jmax (block, 4096));
    work.setSize (4, juce::jmax (block, 4096));
    setLatencySamples (engine.latency());
}

void PartialsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    if (work.getNumSamples() < n) work.setSize (4, n, false, false, true);

    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (sl.isLocked())
            for (int p = 0; p < 2; ++p)
                if (pendingNew[p]) { engine.swapPartData (p, pending[p]); pendingNew[p] = false; }
    }
    float g[GP::Count], a[PP::Count], b[PP::Count];
    for (int i = 0; i < GP::Count; ++i) g[i] = rawGlobal[i]->load (std::memory_order_relaxed);
    for (int i = 0; i < PP::Count; ++i) { a[i] = rawPart[0][i]->load (std::memory_order_relaxed); b[i] = rawPart[1][i]->load (std::memory_order_relaxed); }
    double bpm = 120.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto bp = pos->getBpm()) bpm = *bp;
    hostBpm.store (bpm);
    engine.setParams (g, a, b, bpm, isNonRealtime());

    keyboard.processNextMidiBuffer (midi, 0, n, true);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = meta.samplePosition;
        if (m.isNoteOn()) engine.addEvent (at, Engine::NoteOn, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.addEvent (at, Engine::NoteOff, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isPitchWheel()) engine.addEvent (at, Engine::Bend, 0, ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f);
        else if (m.isSustainPedalOn() || m.isSustainPedalOff()) engine.addEvent (at, Engine::Sustain, 0, m.isSustainPedalOn() ? 1.0f : 0.0f);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.addEvent (at, Engine::AllOff, 0, 0);
    }
    midi.clear();

    float* out[4] = { work.getWritePointer (0), work.getWritePointer (1), work.getWritePointer (2), work.getWritePointer (3) };
    engine.render (out, n);

    const float pre = dbToGain (g[GP::fxPre]), post = dbToGain (g[GP::fxPost]), master = dbToGain (g[GP::masterVol]) * 0.25f;
    juce::FloatVectorOperations::multiply (out[0], pre, n); juce::FloatVectorOperations::multiply (out[1], pre, n);
    int order[FXU::Count]; for (int i = 0; i < FXU::Count; ++i) order[i] = fxOrderRT[i].load (std::memory_order_relaxed);
    const float fxScale = 0.25f;        // effects see the level you hear, not the engine's internal one
    juce::FloatVectorOperations::multiply (out[0], fxScale, n); juce::FloatVectorOperations::multiply (out[1], fxScale, n);
    fx.process (out[0], out[1], n, g, order, (float) bpm / 60.0f);
    const float back = post * master / fxScale;

    float* L = buffer.getWritePointer (0); float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float pkL = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        float l = out[0][i] * back + out[2][i] * master, r = out[1][i] * back + out[3][i] * master;
        if (! std::isfinite (l)) l = 0; if (! std::isfinite (r)) r = 0;
        l = clampf (l, -4.0f, 4.0f); r = clampf (r, -4.0f, 4.0f);
        pkL = juce::jmax (pkL, std::abs (l)); pkR = juce::jmax (pkR, std::abs (r));
        L[i] = l; if (R != nullptr) R[i] = r;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    meterL.store (juce::jmax (pkL, meterL.load() * 0.9f)); meterR.store (juce::jmax (pkR, meterR.load() * 0.9f));
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / ((double) n / sampleRate);
    cpu.store (cpu.load() + ((float) used - cpu.load()) * 0.1f);
}

//==================================================================================================
// state
static juce::ValueTree targetToTree (const TargetDef& t, int index)
{
    juce::ValueTree v ("T");
    v.setProperty ("i", index, nullptr); v.setProperty ("smooth", t.smooth, nullptr);
    v.setProperty ("envOn", t.env.on, nullptr); v.setProperty ("envTempo", t.env.tempo, nullptr); v.setProperty ("envGlobal", t.env.global, nullptr);
    v.setProperty ("env", t.env.c.toString(), nullptr); v.setProperty ("sus", t.env.sustain, nullptr); v.setProperty ("loop", t.env.loopStart, nullptr);
    v.setProperty ("lfoOn", t.lfo.on, nullptr); v.setProperty ("lfoTempo", t.lfo.tempo, nullptr); v.setProperty ("lfoGlobal", t.lfo.global, nullptr);
    v.setProperty ("lfoAmt", t.lfo.amt, nullptr); v.setProperty ("lfoSpeed", t.lfo.speed, nullptr); v.setProperty ("lfoTension", t.lfo.tension, nullptr);
    v.setProperty ("lfoSkew", t.lfo.skew, nullptr); v.setProperty ("lfoPw", t.lfo.pw, nullptr); v.setProperty ("lfoPhase", t.lfo.phase, nullptr);
    v.setProperty ("lfoAmp", t.lfo.amp.toString(), nullptr);
    for (int m = 0; m < MAP::Count; ++m)
        if (t.mapOn[m] || ! t.map[m].empty())
        {
            v.setProperty ("mapOn" + juce::String (m), t.mapOn[m], nullptr);
            v.setProperty ("map" + juce::String (m), t.map[m].toString(), nullptr);
        }
    return v;
}
static void treeToTarget (const juce::ValueTree& v, TargetDef& t)
{
    t = TargetDef();
    t.smooth = v.getProperty ("smooth", 0.0f);
    t.env.on = v.getProperty ("envOn", false); t.env.tempo = v.getProperty ("envTempo", false); t.env.global = v.getProperty ("envGlobal", false);
    t.env.c.fromString (v.getProperty ("env").toString()); t.env.sustain = v.getProperty ("sus", -1); t.env.loopStart = v.getProperty ("loop", -1);
    t.lfo.on = v.getProperty ("lfoOn", false); t.lfo.tempo = v.getProperty ("lfoTempo", false); t.lfo.global = v.getProperty ("lfoGlobal", false);
    t.lfo.amt = v.getProperty ("lfoAmt", 1.0f); t.lfo.speed = v.getProperty ("lfoSpeed", 0.5f); t.lfo.tension = v.getProperty ("lfoTension", 0.32f);
    t.lfo.skew = v.getProperty ("lfoSkew", 0.0f); t.lfo.pw = v.getProperty ("lfoPw", 0.0f); t.lfo.phase = v.getProperty ("lfoPhase", 0.0f);
    t.lfo.amp.fromString (v.getProperty ("lfoAmp").toString());
    for (int m = 0; m < MAP::Count; ++m)
    {
        t.mapOn[m] = v.getProperty ("mapOn" + juce::String (m), false);
        t.map[m].fromString (v.getProperty ("map" + juce::String (m)).toString());
    }
}

juce::ValueTree PartialsProcessor::makePartState (int part)
{
    const PartData& d = *editData[part];
    juce::ValueTree v ("Part");
    juce::ValueTree params ("Params");
    for (int i = 0; i < PP::Count; ++i) params.setProperty (partDefs[i].id, rawPart[part][i]->load(), nullptr);
    v.addChild (params, -1, nullptr);
    juce::ValueTree shapes ("Shapes");
    for (int s = 0; s < SH::Count; ++s) shapes.setProperty ("s" + juce::String (s), d.shape[s].toString(), nullptr);
    v.addChild (shapes, -1, nullptr);
    juce::String uo; for (int u : d.unitOrder) uo << u << ' ';
    v.setProperty ("unitOrder", uo.trim(), nullptr);
    PartData def;
    for (int t = 0; t < TG::Count; ++t)
        if (d.tg[t].active() || ! d.tg[t].env.c.pts.empty() || t == TG::Volume) v.addChild (targetToTree (d.tg[t], t), -1, nullptr);
    if (d.image != nullptr && d.image->valid()) { juce::ValueTree im ("Image"); d.image->save (im); v.addChild (im, -1, nullptr); }
    return v;
}

void PartialsProcessor::applyPartState (int part, const juce::ValueTree& v)
{
    PartData& d = *editData[part];
    d.reset();
    auto params = v.getChildWithName ("Params");
    for (int i = 0; i < PP::Count; ++i)
        if (auto* p = partParam (part, i))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) params.getProperty (partDefs[i].id, partDefs[i].def)));
    auto shapes = v.getChildWithName ("Shapes");
    for (int s = 0; s < SH::Count; ++s)
        if (shapes.hasProperty ("s" + juce::String (s))) d.shape[s].fromString (shapes.getProperty ("s" + juce::String (s)).toString());
    auto uo = juce::StringArray::fromTokens (v.getProperty ("unitOrder").toString(), " ", "");
    if (uo.size() == UNIT::Count)
    {
        bool seen[UNIT::Count] = {}; bool ok = true;
        for (int i = 0; i < UNIT::Count; ++i) { const int u = uo[i].getIntValue(); if (u < 0 || u >= UNIT::Count || seen[u]) ok = false; else seen[u] = true; }
        if (ok) for (int i = 0; i < UNIT::Count; ++i) d.unitOrder[i] = uo[i].getIntValue();
    }
    for (auto c : v)
    {
        if (c.hasType ("T")) { const int i = c.getProperty ("i", -1); if (i >= 0 && i < TG::Count) treeToTarget (c, d.tg[i]); }
        else if (c.hasType ("Image")) d.image = ImageData::load (c);
    }
    commit (part);
}

juce::ValueTree PartialsProcessor::makeState (bool)
{
    juce::ValueTree v ("Partials");
    v.setProperty ("version", 1, nullptr); v.setProperty ("name", presetName, nullptr);
    juce::ValueTree g ("Global");
    for (int i = 0; i < GP::Count; ++i) g.setProperty (globalDefs[i].id, rawGlobal[i]->load(), nullptr);
    juce::String fo; for (int f : fxOrder) fo << f << ' ';
    g.setProperty ("fxOrder", fo.trim(), nullptr);
    v.addChild (g, -1, nullptr);
    for (int p = 0; p < 2; ++p) { auto pv = makePartState (p); pv.setProperty ("index", p, nullptr); v.addChild (pv, -1, nullptr); }
    return v;
}

void PartialsProcessor::applyState (const juce::ValueTree& v)
{
    if (! v.hasType ("Partials")) return;
    presetName = v.getProperty ("name", "Default").toString();
    auto g = v.getChildWithName ("Global");
    for (int i = 0; i < GP::Count; ++i)
        if (auto* p = globalParam (i)) p->setValueNotifyingHost (p->convertTo0to1 ((float) g.getProperty (globalDefs[i].id, globalDefs[i].def)));
    auto fo = juce::StringArray::fromTokens (g.getProperty ("fxOrder").toString(), " ", "");
    int o[FXU::Count]; for (int i = 0; i < FXU::Count; ++i) o[i] = i;
    if (fo.size() == FXU::Count)
    {
        bool seen[FXU::Count] = {}; bool ok = true;
        for (int i = 0; i < FXU::Count; ++i) { const int u = fo[i].getIntValue(); if (u < 0 || u >= FXU::Count || seen[u]) ok = false; else seen[u] = true; }
        if (ok) for (int i = 0; i < FXU::Count; ++i) o[i] = fo[i].getIntValue();
    }
    setFxOrder (o);
    for (auto c : v) if (c.hasType ("Part")) { const int p = c.getProperty ("index", 0); if (p >= 0 && p < 2) applyPartState (p, c); }
}

void PartialsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream os (dest, false);
    makeState().writeToStream (os);
}
void PartialsProcessor::setStateInformation (const void* data, int size)
{
    auto v = juce::ValueTree::readFromData (data, (size_t) size);
    if (v.isValid()) applyState (v);
}

void PartialsProcessor::copyPart (int from, int to)
{
    auto v = makePartState (from);
    applyPartState (to, v);
}

void PartialsProcessor::initPreset()
{
    juce::ValueTree v ("Partials");
    v.setProperty ("name", "Default", nullptr);
    v.addChild (juce::ValueTree ("Global"), -1, nullptr);
    for (int p = 0; p < 2; ++p) { juce::ValueTree pv ("Part"); pv.setProperty ("index", p, nullptr); v.addChild (pv, -1, nullptr); }
    applyState (v);
}

juce::File PartialsProcessor::presetFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Partials").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

//==================================================================================================
// loading sound and pictures
static bool readAudio (const juce::File& f, juce::AudioBuffer<float>& buf, double& sr, std::vector<float>* markers = nullptr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 600.0));
    buf.setSize ((int) juce::jmin<unsigned int> (2, r->numChannels), len);
    r->read (&buf, 0, len, 0, true, true);
    sr = r->sampleRate;
    if (markers != nullptr)
    {
        const int nc = r->metadataValues.getValue ("NumCuePoints", "0").getIntValue();
        for (int i = 0; i < nc; ++i)
        {
            const double off = r->metadataValues.getValue ("Cue" + juce::String (i) + "Offset", "-1").getDoubleValue();
            if (off >= 0) markers->push_back ((float) (off / (double) len));
        }
        std::sort (markers->begin(), markers->end());
    }
    return true;
}

bool PartialsProcessor::loadAudioFile (int part, const juce::File& f, bool highPrecision, int channelMode)
{
    juce::AudioBuffer<float> buf; double sr = 44100.0; std::vector<float> markers;
    if (! readAudio (f, buf, sr, &markers)) return false;
    auto img = ImageData::fromAudio (buf, sr, highPrecision, channelMode);
    if (img == nullptr) return false;
    img->name = f.getFileNameWithoutExtension(); img->markers = markers;
    setImage (part, img);
    return true;
}

bool PartialsProcessor::loadPictureFile (int part, const juce::File& f, bool intoFreqPlane)
{
    auto im = juce::ImageFileFormat::loadFrom (f);
    auto img = ImageData::fromPicture (im, editData[part]->image, intoFreqPlane);
    if (img == nullptr) return false;
    img->name = f.getFileNameWithoutExtension();
    setImage (part, img);
    return true;
}

bool PartialsProcessor::analyzeWaveform (int part, int timbre, const juce::File& f)
{
    juce::AudioBuffer<float> buf; double sr = 44100.0;
    if (! readAudio (f, buf, sr)) return false;
    const int len = buf.getNumSamples();
    if (len < 8) return false;
    const int order = 11, W = 1 << order;
    std::vector<float> x ((size_t) W * 2, 0.0f);
    for (int i = 0; i < W; ++i)
    {
        const float p = (float) i * (float) len / (float) W;
        const int i0 = (int) p, i1 = (i0 + 1) % len;
        x[(size_t) i] = buf.getSample (0, i0) + (buf.getSample (0, i1) - buf.getSample (0, i0)) * (p - (float) i0);
    }
    juce::dsp::FFT fft (order);
    fft.performRealOnlyForwardTransform (x.data(), true);
    std::vector<float> lev ((size_t) NH, 0.0f), ph ((size_t) NH, 0.5f);
    float mx = 1.0e-9f;
    for (int h = 0; h < NH && h + 1 < W / 2; ++h)
    {
        const float re = x[(size_t) (2 * (h + 1))], im = x[(size_t) (2 * (h + 1) + 1)];
        lev[(size_t) h] = std::sqrt (re * re + im * im) * (float) (h + 1);
        mx = juce::jmax (mx, lev[(size_t) h]);
        float cyc = (std::atan2 (im, re) + 1.5707963f) / kTwoPi;            // relative to a sine
        cyc -= std::round (cyc);
        ph[(size_t) h] = 0.5f + cyc;
    }
    PartData& d = *editData[part];
    Curve& tc = d.shape[timbre == 0 ? SH::Timbre1 : SH::Timbre2];
    tc.pts.clear(); tc.raw.resize ((size_t) NH);
    for (int h = 0; h < NH; ++h) tc.raw[(size_t) h] = yFromLevel (lev[(size_t) h] / mx);
    Curve& pc = d.shape[SH::Phase];
    pc.pts.clear(); pc.raw = ph;
    if (auto* p = partParam (part, PP::phaseStart)) p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    commit (part);
    return true;
}

#if ! PARTIALS_TEST
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PartialsProcessor(); }
#endif

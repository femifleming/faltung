#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Ui.h"
#include "BinaryData.h"

namespace ecr
{
static const char* const slotIds[kNumSlotParams]     = { "pos", "attack", "release", "predelay", "hpf", "width", "pitch" };
static const char* const slotNames[kNumSlotParams]   = { "Position", "Attack", "Release", "Predelay", "HPF Cutoff", "Stereo Width", "Pitch" };
static const float slotDefaults[kNumSlotParams]      = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f };
static const char* const globalIds[kNumGlobalParams] = { "dry", "wet", "xfade", "slew", "inlvl", "cv_pos", "cv_x", "cv_width", "cv_aux" };
static const char* const globalNames[kNumGlobalParams] = { "Dry", "Wet", "Crossfade", "Slew Time", "Input Level", "CV Pos", "CV X", "CV Width", "CV Aux" };
static const float globalDefaults[kNumGlobalParams]  = { 1.0f, 0.6f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static const char* const destNames[kNumModDests] = { "Pos", "Width", "Pitch", "Xfade", "Predelay", "HPF", "Wet", "Dry", "Attack", "Release", "In lvl" };
static const char* const destShort[kNumModDests] = { "POS", "WID", "PIT", "XFD", "PRE", "HPF", "WET", "DRY", "ATK", "REL", "INL" };
static const char* const destIds[kNumModDests]   = { "pos", "width", "pitch", "xfade", "predelay", "hpf", "wet", "dry", "attack", "release", "inlvl" };

const char* EcrProcessor::slotParamName (int which) { return slotNames[which]; }
const char* EcrProcessor::modDestName (int d) { return destNames[d]; }
const char* EcrProcessor::modDestShort (int d) { return destShort[d]; }

static juce::String slotText (int which, float v)
{
    switch (which)
    {
        case kPos:      return juce::String (v, 3);
        case kAttack:   return juce::String (curve::timeSeconds (v), 3) + " s";
        case kRelease:  return curve::releaseIsFull (v) ? juce::String ("full") : juce::String (curve::timeSeconds (v), 3) + " s";
        case kPredelay: return juce::String (curve::predelaySeconds (v), 3) + " s";
        case kHpf:      return curve::hpfHz (v) <= 0.0 ? juce::String ("off") : juce::String (juce::roundToInt (curve::hpfHz (v))) + " Hz";
        case kWidth:    return juce::String (curve::width (v), 2);
        case kPitch:    return juce::String (curve::pitchOctaves (v), 2) + " oct";
        default:        return {};
    }
}

static juce::String globalText (int which, float v)
{
    switch (which)
    {
        case kDry: case kWet: case kInLvl: return juce::String (curve::gainDb (v), 1) + " dB";
        case kXfade: return juce::String (juce::roundToInt ((1.0f - v) * 100.0f)) + "/" + juce::String (juce::roundToInt (v * 100.0f));
        case kSlew:  return juce::String (curve::slewSeconds (v), 3) + " s";
        default:     return juce::String (v * 5.0f, 2) + " V";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout EcrProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int g = 0; g < kNumGlobalParams; ++g)
    {
        const bool cv = g >= kCvPos;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (globalIds[g], 1), globalNames[g],
            juce::NormalisableRange<float> (cv ? -1.0f : 0.0f, 1.0f), globalDefaults[g],
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([g] (float v, int) { return globalText (g, v); })));
    }
    for (int s = 0; s < 2; ++s)
        for (int p = 0; p < kNumSlotParams; ++p)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (juce::String (s == 0 ? "a_" : "b_") + slotIds[p], 1),
                juce::String (s == 0 ? "A " : "B ") + slotNames[p],
                juce::NormalisableRange<float> (0.0f, 1.0f), slotDefaults[p],
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([p] (float v, int) { return slotText (p, v); })));
    juce::StringArray shapes;
    for (int i = 0; i < kNumLfoShapes; ++i) shapes.add (juce::String (lfoShapeName (i)).toLowerCase());
    for (int l = 0; l < kNumLfos; ++l)
    {
        const juce::String n (l + 1);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("lfo" + n + "_shape", 1), "LFO " + n + " Shape",
                                                                  shapes, l == 0 ? 0 : (l == 1 ? (int) kLorenz : (int) kBoid)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID ("lfo" + n + "_rate", 1), "LFO " + n + " Rate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (lfoRateHz (v), 2) + " Hz"; })));
        for (int d = 0; d < kNumModDests; ++d)
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID ("lfo" + n + "_to_" + destIds[d], 1), "LFO " + n + " > " + destNames[d],
                juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    }
    return layout;
}

//==============================================================================
EcrProcessor::EcrProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("ConvoSolution response builder"),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int g = 0; g < kNumGlobalParams; ++g)
    {
        globalParams[g] = apvts.getParameter (globalIds[g]);
        globalRaw[g] = apvts.getRawParameterValue (globalIds[g]);
    }
    for (int s = 0; s < 2; ++s)
        for (int p = 0; p < kNumSlotParams; ++p)
        {
            const auto id = juce::String (s == 0 ? "a_" : "b_") + slotIds[p];
            slotParams[s][p] = apvts.getParameter (id);
            slotRaw[s][p] = apvts.getRawParameterValue (id);
        }

    for (int l = 0; l < kNumLfos; ++l)
    {
        const juce::String n (l + 1);
        lfoShape[l] = apvts.getParameter ("lfo" + n + "_shape");   lfoShapeRaw[l] = apvts.getRawParameterValue ("lfo" + n + "_shape");
        lfoRate[l]  = apvts.getParameter ("lfo" + n + "_rate");    lfoRateRaw[l]  = apvts.getRawParameterValue ("lfo" + n + "_rate");
        for (int d = 0; d < kNumModDests; ++d)
        {
            modAmount[l][d] = apvts.getParameter ("lfo" + n + "_to_" + destIds[d]);
            modRaw[l][d] = apvts.getRawParameterValue ("lfo" + n + "_to_" + destIds[d]);
        }
    }

    library.scan();
    ui = std::make_unique<Ui> (*this);

    // the module's "getting started" state: first two built-in responses
    const auto& e = library.entries();
    if (! e.empty())
    {
        selectResponse (0, e[0].id);
        selectResponse (1, e[std::min<size_t> (1, e.size() - 1)].id);
    }
    loadStoredDefaults();
    startThread (juce::Thread::Priority::normal);
}

EcrProcessor::~EcrProcessor()
{
    stopThread (10000);
    clearEngines();
    delete previewReady.exchange (nullptr);
    delete previewClip;
    emptyBin();
}

bool EcrProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

juce::AudioProcessorEditor* EcrProcessor::createEditor() { return new EcrEditor (*this); }

//== disposal of objects the audio thread is finished with ====================
void EcrProcessor::dispose (Disposable* d)
{
    if (d == nullptr) return;
    for (auto& b : bin)
    {
        Disposable* expected = nullptr;
        if (b.compare_exchange_strong (expected, d)) return;
    }
    delete d; // bin full: should never happen, the helper thread empties it every few ms
}

void EcrProcessor::emptyBin()
{
    for (auto& b : bin)
        delete b.exchange (nullptr);
}

void EcrProcessor::clearEngines()
{
    for (auto& s : slots)
    {
        delete s.ready.exchange (nullptr);
        delete s.cur; s.cur = nullptr;
        delete s.old; s.old = nullptr;
        s.builtKey = ~(uint64_t) 0;
    }
}

//== settings / wanted state ====================================================
void EcrProcessor::settingsChanged()
{
    sCvMap = settings.cvMap;
    sAux = settings.aux;
    sConvB = settings.convB;
    sAutoGain = settings.autoGain;
}

void EcrProcessor::advanceLfos (int numSamples)
{
    const double seconds = numSamples / juce::jmax (1.0, currentRate.load());
    for (int l = 0; l < kNumLfos; ++l)
    {
        const int shape = juce::roundToInt (lfoShapeRaw[l]->load());
        lfoOut[l] = lfos[l].advance (shape, lfoRateHz (lfoRateRaw[l]->load()), seconds);
        float v[Lfo::kViewSize];
        lfos[l].view (shape, v);
        for (int i = 0; i < Lfo::kViewSize; ++i) lfoViz[l][i].store (v[i], std::memory_order_relaxed);
    }
}

void EcrProcessor::publishWanted()
{
    const float cvPos = globalRaw[kCvPos]->load(), cvX = globalRaw[kCvX]->load();
    const float cvWidth = globalRaw[kCvWidth]->load(), cvAux = globalRaw[kCvAux]->load();
    const int map = sCvMap.load(), aux = sAux.load();
    auto clamp01 = [] (float v) { return juce::jlimit (0.0f, 1.0f, v); };

    // modulation matrix: amount x LFO, summed per destination, in knob units
    float mod[kNumModDests] {};
    for (int l = 0; l < kNumLfos; ++l)
    {
        const float v = lfoOut[l].load();
        for (int d = 0; d < kNumModDests; ++d) mod[d] += modRaw[l][d]->load() * v;
    }

    // per-convolver destinations follow the CV MAP setting, like the CV inputs
    for (int s = 0; s < 2; ++s)
    {
        const bool mapped = map == 2 || map == s;
        const float on = mapped ? 1.0f : 0.0f;
        auto& sl = slots[s];
        sl.wPos     = juce::roundToInt (clamp01 (slotRaw[s][kPos]->load() + on * (cvPos + mod[dPos])) * kKnobSteps);
        sl.wAttack  = juce::roundToInt (clamp01 (slotRaw[s][kAttack]->load() + on * mod[dAttack]) * kKnobSteps);
        sl.wRelease = juce::roundToInt (clamp01 (slotRaw[s][kRelease]->load() + on * mod[dRelease]) * kKnobSteps);
        sl.wPitch   = juce::roundToInt (clamp01 (slotRaw[s][kPitch]->load() + on * ((aux == 1 ? cvAux * 0.5f : 0.0f) + mod[dPitch] * 0.5f)) * kKnobSteps);
        widthK[s]    = clamp01 (slotRaw[s][kWidth]->load() + on * (cvWidth + mod[dWidth]) * 0.5f);
        predelayK[s] = clamp01 (slotRaw[s][kPredelay]->load() + on * mod[dPredelay]);
        hpfK[s]      = clamp01 (slotRaw[s][kHpf]->load() + on * mod[dHpf]);
    }
    wetK = clamp01 (globalRaw[kWet]->load() + (aux == 0 ? cvAux : 0.0f) + mod[dWet]);
    dryK = clamp01 (globalRaw[kDry]->load() + mod[dDry]);
    inK  = clamp01 (globalRaw[kInLvl]->load() + (aux == 2 ? cvAux : 0.0f) + mod[dInLvl]);
    xfK  = clamp01 (globalRaw[kXfade]->load() + cvX + mod[dXfade]);
}

static std::atomic<uint32_t> prepareCounter { 0 };

uint64_t EcrProcessor::wantedKey (const Slot& s) const
{
    return (uint64_t) s.wPos.load() | ((uint64_t) s.wAttack.load() << 10) | ((uint64_t) s.wRelease.load() << 20)
         | ((uint64_t) s.wPitch.load() << 30) | ((uint64_t) (s.srcGenAtomic.load() & 0xffff) << 40)
         | ((uint64_t) (prepareCounter.load() & 0xff) << 56);
}

void EcrProcessor::waitForEngines (int timeoutMs)
{
    const auto start = juce::Time::getMillisecondCounter();
    for (;;)
    {
        bool ok = true;
        for (auto& s : slots)
            ok = ok && ! s.pendingLoad.load() && s.builtKey.load() == wantedKey (s);
        if (ok || juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs || ! isThreadRunning())
            return;
        juce::Thread::sleep (1);
    }
}

//== responses ==================================================================
std::shared_ptr<IrSource> EcrProcessor::loadById (const juce::String& id, const juce::String& fallbackPath)
{
    std::shared_ptr<IrSource> src;
    if (id.startsWith ("Internal/"))
    {
        const auto wanted = id.fromFirstOccurrenceOf ("/", false, false) + ".flac";
        for (int i = 0; i < BinaryData::namedResourceListSize && src == nullptr; ++i)
            if (wanted == BinaryData::originalFilenames[i])
            {
                int size = 0;
                const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                src = loadIrFromStream (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false),
                                        id.fromFirstOccurrenceOf ("/", false, false) + ".wav");
            }
    }
    else
    {
        const int idx = library.indexOf (id);
        if (idx >= 0) src = loadIrFromFile (library.entries()[(size_t) idx].file);
        if (src == nullptr && fallbackPath.isNotEmpty() && juce::File::isAbsolutePath (fallbackPath))
            src = loadIrFromFile (juce::File (fallbackPath));
    }
    if (src != nullptr) src->id = id;
    return src;
}

void EcrProcessor::selectResponse (int slot, const juce::String& id)
{
    auto& s = slots[slot];
    const juce::ScopedLock sl (s.lock);
    s.pendingId = id;
    s.hasPending = true;
    s.pendingLoad = true;
}

juce::String EcrProcessor::responseId (int slot) const
{
    auto& s = slots[slot];
    const juce::ScopedLock sl (s.lock);
    return s.hasPending ? s.pendingId : s.id;
}

IrSourcePtr EcrProcessor::responseSource (int slot) const
{
    auto& s = slots[slot];
    const juce::ScopedLock sl (s.lock);
    return s.src;
}

//== preview ======================================================================
void EcrProcessor::startPreview (bool processed)
{
    const int fg = foreground.load();
    auto src = responseSource (fg);
    if (src == nullptr) return;
    previewOn = true;
    previewProcessedMode = processed;
    if (processed)
    {
        slots[fg].fireImpulse = 0.7f / src->peak;
    }
    else
    {
        auto* clip = new PreviewClip();
        const float g = 0.5f / src->peak;
        clip->l = src->ch[0];
        clip->r = src->numChannels > 1 ? src->ch[1] : src->ch[0];
        for (auto& v : clip->l) v *= g;
        for (auto& v : clip->r) v *= g;
        clip->step = src->sampleRate / juce::jmax (1.0, currentRate.load());
        delete previewReady.exchange (clip);
    }
}

void EcrProcessor::stopPreview()
{
    previewOn = false;
    delete previewReady.exchange (nullptr);
    previewStop = true;
}

//== recording ====================================================================
void EcrProcessor::toggleRecording()
{
    int expected = 1;
    if (recState.compare_exchange_strong (expected, 2)) return;   // stop
    if (recState.load() == 0 && ! recBuf[0].empty())
    {
        recSlot = foreground.load();
        recLength = 0;
        recState = 1;
    }
}

static bool writeFloatWav (const juce::File& f, const IrSource& s)
{
    juce::MemoryOutputStream o;
    const int ch = s.numChannels, n = s.length;
    const juce::uint32 dataBytes = (juce::uint32) (ch * n * 4);
    o.write ("RIFF", 4); o.writeInt ((int) (36 + dataBytes)); o.write ("WAVEfmt ", 8);
    o.writeInt (16); o.writeShort (3); o.writeShort ((short) ch);
    o.writeInt ((int) s.sampleRate); o.writeInt ((int) s.sampleRate * ch * 4);
    o.writeShort ((short) (ch * 4)); o.writeShort (32);
    o.write ("data", 4); o.writeInt ((int) dataBytes);
    const float undo = s.gain != 0.0f ? 1.0f / s.gain : 1.0f;      // store what was actually recorded
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c)
            o.writeFloat (s.ch[c][(size_t) i] * undo);
    return f.getParentDirectory().createDirectory().wasOk() && f.replaceWithData (o.getData(), o.getDataSize());
}

bool EcrProcessor::storeRecording (int slot)
{
    auto src = responseSource (slot);
    if (src == nullptr || ! src->unsaved) return false;
    const auto file = library.userRoot().getChildFile (src->name);
    if (! writeFloatWav (file, *src)) return false;

    auto saved = std::make_shared<IrSource> (*src);
    saved->unsaved = false;
    saved->file = file;
    saved->id = src->name;
    library.scan();
    auto& s = slots[slot];
    const juce::ScopedLock sl (s.lock);
    if (s.src == src) { s.src = saved; s.id = saved->id; }   // same audio: no rebuild needed
    return true;
}

//== presets ("shift + store") ====================================================
juce::File EcrProcessor::patchFileFor (const IrSource& src) const { return library.patchFile (src.id, src.file); }

bool EcrProcessor::storePresets()
{
    bool ok = true;
    juce::StringPairArray sys;
    sys.set ("direct_shift", juce::String ((int) settings.directShift));
    sys.set ("chooser", juce::String ((int) settings.chooser));
    sys.set ("auto_exit", juce::String ((int) settings.autoExit));
    sys.set ("cv_map", juce::String (settings.cvMap));
    sys.set ("aux", juce::String (settings.aux));
    sys.set ("svr", juce::String (settings.svr));
    sys.set ("conv_b", juce::String ((int) settings.convB));
    sys.set ("autogain", juce::String ((int) settings.autoGain));
    for (int g = 0; g <= kInLvl; ++g) sys.set (globalIds[g], juce::String (globalRaw[g]->load(), 4));

    for (int s = 0; s < 2; ++s)
    {
        auto src = responseSource (s);
        if (src == nullptr || src->unsaved) continue;
        sys.set (s == 0 ? "sample_a" : "sample_b", src->id);
        juce::StringPairArray patch;
        for (int p = 0; p < kNumSlotParams; ++p) patch.set (slotIds[p], juce::String (slotRaw[s][p]->load(), 4));
        ok = writeIni (patchFileFor (*src), patch) && ok;
    }
    ok = writeIni (library.settingsFile(), sys) && ok;
    library.scan();
    return ok;
}

static void setParam (juce::RangedAudioParameter* p, float value)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (value));
    p->endChangeGesture();
}

bool EcrProcessor::loadPatch (int slot)
{
    auto src = responseSource (slot);
    if (src == nullptr) return false;
    const auto f = patchFileFor (*src);
    if (! f.existsAsFile()) return false;
    const auto ini = readIni (f);
    for (int p = 0; p < kNumSlotParams; ++p)
        if (ini.containsKey (slotIds[p]))
            setParam (slotParams[slot][p], juce::jlimit (0.0f, 1.0f, ini[slotIds[p]].getFloatValue()));
    return true;
}

void EcrProcessor::loadStoredDefaults()
{
    const auto f = library.settingsFile();
    if (! f.existsAsFile()) return;
    const auto ini = readIni (f);
    auto flag = [&] (const char* k, bool d) { return ini.containsKey (k) ? ini[k].getIntValue() != 0 : d; };
    settings.directShift = flag ("direct_shift", settings.directShift);
    settings.chooser = flag ("chooser", settings.chooser);
    settings.autoExit = flag ("auto_exit", settings.autoExit);
    settings.convB = flag ("conv_b", settings.convB);
    settings.autoGain = flag ("autogain", settings.autoGain);
    if (ini.containsKey ("cv_map")) settings.cvMap = juce::jlimit (0, 2, ini["cv_map"].getIntValue());
    if (ini.containsKey ("aux")) settings.aux = juce::jlimit (0, 2, ini["aux"].getIntValue());
    if (ini.containsKey ("svr")) settings.svr = juce::jlimit (0, 3, ini["svr"].getIntValue());
    settingsChanged();
    for (int g = 0; g <= kInLvl; ++g)
        if (ini.containsKey (globalIds[g]))
            globalParams[g]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, ini[globalIds[g]].getFloatValue()));

    for (int s = 0; s < 2; ++s)
    {
        const auto id = ini[s == 0 ? "sample_a" : "sample_b"];
        if (id.isEmpty() || (! id.startsWith ("Internal/") && library.indexOf (id) < 0)) continue;
        selectResponse (s, id);
        const int idx = library.indexOf (id);
        const auto pf = library.patchFile (id, idx >= 0 ? library.entries()[(size_t) idx].file : juce::File());
        if (! pf.existsAsFile()) continue;
        const auto patch = readIni (pf);
        for (int p = 0; p < kNumSlotParams; ++p)
            if (patch.containsKey (slotIds[p]))
                slotParams[s][p]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, patch[slotIds[p]].getFloatValue()));
    }
}

//== helper thread ================================================================
void EcrProcessor::run()
{
    while (! threadShouldExit())
    {
        emptyBin();

        for (int si = 0; si < 2; ++si)
        {
            auto& s = slots[si];

            // 1. load a newly selected response
            juce::String id, path;
            bool load = false;
            {
                const juce::ScopedLock sl (s.lock);
                if (s.hasPending) { id = s.pendingId; path = s.pendingPath; s.pendingPath.clear(); s.hasPending = false; load = true; }
            }
            if (load)
            {
                auto src = loadById (id, path);
                const juce::ScopedLock sl (s.lock);
                if (! s.hasPending)
                {
                    if (src != nullptr || s.src == nullptr)   // an unreadable file leaves the current response in place
                    {
                        s.src = src;
                        s.id = id;
                        s.srcGenAtomic = ++s.srcGen;
                        s.retrigger = true;
                    }
                    s.pendingLoad = false;
                }
            }

            // 2. rebuild the engine when the response or its treatment changed
            const double rate = currentRate.load();
            if (rate <= 0.0) continue;
            const uint64_t key = wantedKey (s);
            if (key == s.builtKey.load()) continue;

            IrSourcePtr src;
            { const juce::ScopedLock sl (s.lock); src = s.src; }
            IrSettings st;
            st.pos = (int) (key & 1023); st.attack = (int) ((key >> 10) & 1023);
            st.release = (int) ((key >> 20) & 1023); st.pitch = (int) ((key >> 30) & 1023);

            auto ir = s.renderer.render (src, st, rate);
            const float zero = 0.0f;
            const float* h[4] = { nullptr, nullptr, nullptr, nullptr };
            if (ir.length > 0) { for (int c = 0; c < 4; ++c) if (ir.active[c]) h[c] = ir.h[c].data(); }
            else h[0] = &zero;

            auto* engine = new ConvEngine (h, std::max (1, ir.length), si * 2 + (s.buildCount++ & 1));

            // browsing with "play" on: audition every newly loaded response
            if (s.retrigger.exchange (false) && previewOn.load() && si == foreground.load() && src != nullptr)
            {
                if (previewProcessedMode.load()) engine->impulseOnInstall = 0.7f / src->peak;
                else startPreview (false);
            }
            delete s.ready.exchange (engine);
            s.builtKey = key;
        }

        // 3. turn a finished recording into a response
        if (recState.load() == 2)
        {
            wait (30); // let the audio thread finish the block it was writing
            const int len = recLength.load();
            if (len > 0)
            {
                const float* chans[2] = { recBuf[0].data(), recBuf[1].data() };
                auto src = makeIrFromAudio (chans, 2, len, currentRate.load(),
                                            "rec" + juce::String (++recCounter + (int) (juce::Time::currentTimeMillis() / 1000 % 100000) * 10).paddedLeft ('0', 6) + ".wav");
                src->unsaved = true;
                src->id = "rec:" + src->name;
                auto& s = slots[recSlot];
                const juce::ScopedLock sl (s.lock);
                s.src = src;
                s.id = src->id;
                s.hasPending = false;
                s.pendingLoad = false;
                s.srcGenAtomic = ++s.srcGen;
            }
            recState = 0;
        }

        wait (4);
    }
}

//== audio ========================================================================
void EcrProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    ++prepareCounter;
    clearEngines();

    scratch.setSize (12, kSub);
    const int delaySize = (int) (2.0 * sampleRate) + kSub + 16;
    for (auto& s : slots)
    {
        for (auto& d : s.delay) d.assign ((size_t) delaySize, 0.0f);
        s.delayWrite = s.delayFade = 0;
        s.delayCur = s.delayNew = -1;
        s.hpG = 0.0f;
        s.hpS1[0] = s.hpS1[1] = s.hpS2[0] = s.hpS2[1] = 0.0f;
        s.silentFor = 0;
        s.pendingImpulse = 0.0f;
        s.widthNow = -1.0f;
    }
    if (recState.load() == 0)
        for (auto& r : recBuf) r.assign ((size_t) (kMaxIrSeconds * sampleRate), 0.0f);

    publishWanted();
    for (auto* sm : { &inGain, &dryGain, &wetGain, &xfade }) sm->reset (sampleRate, 0.02);
    inGain.setCurrentAndTargetValue ((float) curve::gain (inK));
    dryGain.setCurrentAndTargetValue ((float) curve::gain (dryK));
    wetGain.setCurrentAndTargetValue ((float) curve::gain (wetK));
    xfade.setCurrentAndTargetValue (sConvB.load() ? xfK : 0.0f);
    limiterGain = 1.0f;
    limiterRelease = (float) std::exp (-1.0 / (0.25 * sampleRate));

    // have the reverb ready before the first block
    waitForEngines (10000);
    for (auto& s : slots) s.cur = s.ready.exchange (nullptr);
}

void EcrProcessor::reset()
{
    // drop the tails: engines are rebuilt silent by the helper thread
    for (auto& s : slots)
    {
        for (auto& d : s.delay) std::fill (d.begin(), d.end(), 0.0f);
        s.hpS1[0] = s.hpS1[1] = s.hpS2[0] = s.hpS2[1] = 0.0f;
    }
}

void EcrProcessor::processSlot (Slot& s, int, const float* inL, const float* inR, float* outL, float* outR, int n,
                                float predelaySeconds, float hpfHz, float width, int slewSamples)
{
    const double rate = currentRate.load();
    float* curIn[2] = { scratch.getWritePointer (6), scratch.getWritePointer (7) };
    float* oldIn[2] = { scratch.getWritePointer (8), scratch.getWritePointer (9) };
    float* tmp[2]   = { scratch.getWritePointer (10), scratch.getWritePointer (11) };
    const float* in[2] = { inL, inR };

    // predelay: the read tap jumps by crossfading, so turning the knob never bends pitch
    {
        const int size = (int) s.delay[0].size();
        const int target = juce::jlimit (0, size - kSub - 8, juce::roundToInt (predelaySeconds * rate));
        const int fadeLen = std::max (1, (int) (0.02 * rate));
        if (s.delayCur < 0) s.delayCur = s.delayNew = target;
        if (s.delayFade == 0 && target != s.delayCur) { s.delayNew = target; s.delayFade = fadeLen; }
        int w = s.delayWrite;
        for (int i = 0; i < n; ++i)
        {
            int r1 = w - s.delayCur; if (r1 < 0) r1 += size;
            int r2 = w - s.delayNew; if (r2 < 0) r2 += size;
            const float f = s.delayFade > 0 ? 1.0f - (float) s.delayFade / (float) fadeLen : 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                s.delay[c][(size_t) w] = in[c][i];
                const float a = s.delay[c][(size_t) r1];
                curIn[c][i] = s.delayFade > 0 ? a + (s.delay[c][(size_t) r2] - a) * f : a;
            }
            if (s.delayFade > 0 && --s.delayFade == 0) s.delayCur = s.delayNew;
            if (++w >= size) w = 0;
        }
        s.delayWrite = w;
    }

    // 12 dB/oct high-pass (also blocks DC when "off")
    {
        const float target = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (2.0, 0.45 * rate, (double) hpfHz) / rate);
        const float g0 = s.hpG <= 0.0f ? target : s.hpG, dg = (target - g0) / (float) n;
        const float k = 1.41421356f;
        for (int c = 0; c < 2; ++c)
        {
            float s1 = s.hpS1[c], s2 = s.hpS2[c], g = g0;
            float* x = curIn[c];
            for (int i = 0; i < n; ++i)
            {
                g += dg;
                const float hp = (x[i] - (g + k) * s1 - s2) / (1.0f + g * (g + k));
                const float bp = g * hp + s1;  s1 = g * hp + bp;
                const float lp = g * bp + s2;  s2 = g * bp + lp;
                x[i] = hp;
            }
            s.hpS1[c] = s1; s.hpS2[c] = s2;
        }
        s.hpG = target;
    }

    // take over a freshly built engine, or hurry the previous one out of the way
    if (s.old == nullptr)
    {
        if (auto* e = s.ready.exchange (nullptr))
        {
            if (s.cur != nullptr) { s.old = s.cur; s.oldState = Slot::InFade; s.fadePos = 0; s.fadeLen = std::max (1, slewSamples); }
            s.cur = e;
            s.silentFor = 0;
            if (e->impulseOnInstall > 0.0f) s.pendingImpulse = e->impulseOnInstall;
        }
    }
    else if (s.oldState == Slot::Ring && s.ready.load() != nullptr)
    {
        s.oldState = Slot::OutFade; s.fadePos = 0; s.fadeLen = std::max (1, slewSamples);
    }
    const float fire = s.fireImpulse.exchange (0.0f);
    if (fire > 0.0f) s.pendingImpulse = fire;

    if (s.cur == nullptr)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
        return;
    }

    // The *inputs* are crossfaded between the old and new response; the old one
    // then rings out naturally, so changing a response never cuts a tail.
    const bool inFade = s.old != nullptr && s.oldState == Slot::InFade;
    if (inFade)
    {
        const float inv = 1.0f / (float) s.fadeLen;
        for (int i = 0; i < n; ++i)
        {
            const float f = std::min (1.0f, (float) (s.fadePos + i) * inv);
            for (int c = 0; c < 2; ++c) { const float x = curIn[c][i]; oldIn[c][i] = x * (1.0f - f); curIn[c][i] = x * f; }
        }
    }
    else if (s.old != nullptr)
    {
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::clear (oldIn[c], n);
    }

    if (s.pendingImpulse > 0.0f) { curIn[0][0] += s.pendingImpulse; curIn[1][0] += s.pendingImpulse; s.pendingImpulse = 0.0f; }

    // idle convolvers cost nothing
    const auto rangeL = juce::FloatVectorOperations::findMinAndMax (curIn[0], n);
    const auto rangeR = juce::FloatVectorOperations::findMinAndMax (curIn[1], n);
    const bool silent = rangeL.getStart() == 0.0f && rangeL.getEnd() == 0.0f && rangeR.getStart() == 0.0f && rangeR.getEnd() == 0.0f;
    s.silentFor = silent ? s.silentFor + n : 0;
    if (s.silentFor > (juce::int64) s.cur->irLength() + 70000)
    {
        juce::FloatVectorOperations::clear (outL, n);
        juce::FloatVectorOperations::clear (outR, n);
    }
    else
        s.cur->process (curIn[0], curIn[1], outL, outR, n);

    if (s.old != nullptr)
    {
        s.old->process (oldIn[0], oldIn[1], tmp[0], tmp[1], n);
        bool dead = false;
        if (s.oldState == Slot::OutFade)
        {
            const float inv = 1.0f / (float) s.fadeLen;
            for (int i = 0; i < n; ++i)
            {
                const float g = std::max (0.0f, 1.0f - (float) (s.fadePos + i) * inv);
                outL[i] += tmp[0][i] * g; outR[i] += tmp[1][i] * g;
            }
            s.fadePos += n;
            dead = s.fadePos >= s.fadeLen;
        }
        else
        {
            juce::FloatVectorOperations::add (outL, tmp[0], n);
            juce::FloatVectorOperations::add (outR, tmp[1], n);
            if (s.oldState == Slot::InFade)
            {
                s.fadePos += n;
                if (s.fadePos >= s.fadeLen) { s.oldState = Slot::Ring; s.ringLeft = (juce::int64) s.old->irLength() + 2 * 32768 + 128; }
            }
            else
            {
                s.ringLeft -= n;
                dead = s.ringLeft <= 0;
            }
        }
        if (dead) { dispose (s.old); s.old = nullptr; }
    }

    // mid/side width, after the convolution so mono input can still be widened
    {
        if (s.widthNow < 0.0f) s.widthNow = width;
        const float w0 = s.widthNow, dw = (width - w0) / (float) n;
        if (std::abs (w0 - 1.0f) > 1e-6f || std::abs (width - 1.0f) > 1e-6f)
            for (int i = 0; i < n; ++i)
            {
                const float w = w0 + dw * (float) (i + 1);
                const float m = 0.5f * (outL[i] + outR[i]), sd = 0.5f * (outL[i] - outR[i]) * w;
                outL[i] = m + sd; outR[i] = m - sd;
            }
        s.widthNow = width;
    }
}

void EcrProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut < 1 || scratch.getNumChannels() < 12) return;

    advanceLfos (n);
    publishWanted();
    if (isNonRealtime()) waitForEngines (5000);    // offline bounces follow automation exactly

    const double rate = currentRate.load();
    const bool convB = sConvB.load(), autoGain = sAutoGain.load();
    inGain.setTargetValue ((float) curve::gain (inK));
    dryGain.setTargetValue ((float) curve::gain (dryK));
    wetGain.setTargetValue ((float) curve::gain (wetK));
    xfade.setTargetValue (convB ? xfK : 0.0f);
    const int slew = (int) (curve::slewSeconds (globalRaw[kSlew]->load()) * rate);

    if (auto* c = previewReady.exchange (nullptr)) { dispose (previewClip); previewClip = c; previewPos = 0.0; }
    if (previewStop.exchange (false)) { dispose (previewClip); previewClip = nullptr; }

    float* x[2]  = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
    float* a[2]  = { scratch.getWritePointer (2), scratch.getWritePointer (3) };
    float* b[2]  = { scratch.getWritePointer (4), scratch.getWritePointer (5) };

    for (int off = 0; off < n;)
    {
        const int m = std::min (kSub, n - off);
        const float* inL = numIn > 0 ? buffer.getReadPointer (0) + off : nullptr;
        const float* inR = numIn > 1 ? buffer.getReadPointer (1) + off : inL;
        for (int i = 0; i < m; ++i)
        {
            const float g = inGain.getNextValue();
            x[0][i] = inL != nullptr ? inL[i] * g : 0.0f;
            x[1][i] = inR != nullptr ? inR[i] * g : 0.0f;
        }

        const bool recording = recState.load() == 1;
        if (recording)
        {
            // the convolvers rest while recording: only the dry signal is heard
            const int have = recLength.load(), room = (int) recBuf[0].size() - have;
            const int count = std::min (m, room);
            if (count > 0)
            {
                std::copy (x[0], x[0] + count, recBuf[0].begin() + have);
                std::copy (x[1], x[1] + count, recBuf[1].begin() + have);
                recLength = have + count;
            }
            if (count < m) { int expected = 1; recState.compare_exchange_strong (expected, 2); }
            for (int c = 0; c < 2; ++c) { juce::FloatVectorOperations::clear (a[c], m); juce::FloatVectorOperations::clear (b[c], m); }
        }
        else
        {
            processSlot (slots[0], 0, x[0], x[1], a[0], a[1], m,
                         (float) curve::predelaySeconds (predelayK[0]), (float) curve::hpfHz (hpfK[0]),
                         (float) curve::width (widthK[0]), slew);
            if (convB)
                processSlot (slots[1], 1, x[0], x[1], b[0], b[1], m,
                             (float) curve::predelaySeconds (predelayK[1]), (float) curve::hpfHz (hpfK[1]),
                             (float) curve::width (widthK[1]), slew);
            else
                for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::clear (b[c], m);
        }

        float* outL = buffer.getWritePointer (0) + off;
        float* outR = numOut > 1 ? buffer.getWritePointer (1) + off : nullptr;
        bool limited = false;
        for (int i = 0; i < m; ++i)
        {
            const float xf = xfade.getNextValue(), wg = wetGain.getNextValue(), dg = dryGain.getNextValue();
            float wl = (a[0][i] + (b[0][i] - a[0][i]) * xf) * wg;
            float wr = (a[1][i] + (b[1][i] - a[1][i]) * xf) * wg;

            // automatic gain: the wet sum never exceeds full scale
            const float peak = std::max (std::abs (wl), std::abs (wr));
            if (autoGain)
            {
                if (peak * limiterGain > 1.0f) limiterGain = 1.0f / peak;
                wl *= limiterGain; wr *= limiterGain;
                if (limiterGain < 0.97f) limited = true;
                limiterGain = 1.0f - (1.0f - limiterGain) * limiterRelease;
            }
            else if (peak > 1.0f) limited = true;

            if (previewClip != nullptr)
            {
                const auto idx = (size_t) previewPos;
                if (idx + 1 < previewClip->l.size())
                {
                    const float fr = (float) (previewPos - (double) idx);
                    wl += previewClip->l[idx] + (previewClip->l[idx + 1] - previewClip->l[idx]) * fr;
                    wr += previewClip->r[idx] + (previewClip->r[idx + 1] - previewClip->r[idx]) * fr;
                    previewPos += previewClip->step;
                }
            }

            const float l = x[0][i] * dg + wl, r = x[1][i] * dg + wr;
            outL[i] = l;
            if (outR != nullptr) outR[i] = r;
        }
        if (limited) clipHold = (int) (rate * 0.15);
        else clipHold = std::max (0, clipHold - m);
        off += m;
    }
    clipping = clipHold > 0;
    for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, n);
}

//== session state ================================================================
void EcrProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("ECR");
    extra.setProperty ("fg", foreground.load(), nullptr);
    extra.setProperty ("directShift", settings.directShift, nullptr);
    extra.setProperty ("chooser", settings.chooser, nullptr);
    extra.setProperty ("autoExit", settings.autoExit, nullptr);
    extra.setProperty ("cvMap", settings.cvMap, nullptr);
    extra.setProperty ("aux", settings.aux, nullptr);
    extra.setProperty ("svr", settings.svr, nullptr);
    extra.setProperty ("convB", settings.convB, nullptr);
    extra.setProperty ("autoGain", settings.autoGain, nullptr);
    for (int s = 0; s < 2; ++s)
    {
        const juce::String suffix (s == 0 ? "A" : "B");
        auto src = responseSource (s);
        extra.setProperty ("id" + suffix, responseId (s), nullptr);
        if (src == nullptr) continue;
        extra.setProperty ("path" + suffix, src->file.getFullPathName(), nullptr);
        if (src->unsaved)
        {
            // an unstored recording travels inside the session
            juce::ValueTree rec ("REC");
            rec.setProperty ("slot", s, nullptr);
            rec.setProperty ("name", src->name, nullptr);
            rec.setProperty ("rate", src->sampleRate, nullptr);
            rec.setProperty ("gain", src->gain, nullptr);
            for (int c = 0; c < 2; ++c)
                rec.setProperty (c == 0 ? "l" : "r", juce::MemoryBlock (src->ch[c].data(), src->ch[c].size() * sizeof (float)), nullptr);
            extra.appendChild (rec, nullptr);
        }
    }
    state.appendChild (extra, nullptr);
    juce::MemoryOutputStream out (dest, false);
    state.writeToStream (out);
}

void EcrProcessor::setStateInformation (const void* data, int size)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) size);
    if (! state.isValid()) return;
    auto extra = state.getChildWithName ("ECR");
    if (extra.isValid()) state.removeChild (extra, nullptr);
    apvts.replaceState (state);
    if (! extra.isValid()) return;

    foreground = juce::jlimit (0, 1, (int) extra.getProperty ("fg", 0));
    settings.directShift = extra.getProperty ("directShift", settings.directShift);
    settings.chooser = extra.getProperty ("chooser", settings.chooser);
    settings.autoExit = extra.getProperty ("autoExit", settings.autoExit);
    settings.cvMap = juce::jlimit (0, 2, (int) extra.getProperty ("cvMap", settings.cvMap));
    settings.aux = juce::jlimit (0, 2, (int) extra.getProperty ("aux", settings.aux));
    settings.svr = juce::jlimit (0, 3, (int) extra.getProperty ("svr", settings.svr));
    settings.convB = extra.getProperty ("convB", settings.convB);
    settings.autoGain = extra.getProperty ("autoGain", settings.autoGain);
    settingsChanged();

    bool restored[2] = { false, false };
    for (const auto& rec : extra)
    {
        if (! rec.hasType ("REC")) continue;
        const int s = juce::jlimit (0, 1, (int) rec.getProperty ("slot", 0));
        const auto* l = rec.getProperty ("l").getBinaryData();
        const auto* r = rec.getProperty ("r").getBinaryData();
        if (l == nullptr || r == nullptr || l->getSize() != r->getSize() || l->getSize() < sizeof (float)) continue;
        const int len = (int) (l->getSize() / sizeof (float));
        const float undo = 1.0f / juce::jmax (1e-9f, (float) rec.getProperty ("gain", 1.0f));
        std::vector<float> chans[2] = { std::vector<float> ((size_t) len), std::vector<float> ((size_t) len) };
        std::memcpy (chans[0].data(), l->getData(), l->getSize());
        std::memcpy (chans[1].data(), r->getData(), r->getSize());
        for (auto& ch : chans) for (auto& v : ch) v *= undo;
        const float* ptrs[2] = { chans[0].data(), chans[1].data() };
        auto src = makeIrFromAudio (ptrs, 2, len, (double) rec.getProperty ("rate", 48000.0), rec.getProperty ("name").toString());
        src->unsaved = true;
        src->id = "rec:" + src->name;
        auto& sl = slots[s];
        const juce::ScopedLock lock (sl.lock);
        sl.src = src; sl.id = src->id; sl.hasPending = false; sl.pendingLoad = false;
        sl.srcGenAtomic = ++sl.srcGen;
        restored[s] = true;
    }
    for (int s = 0; s < 2; ++s)
    {
        if (restored[s]) continue;
        const juce::String suffix (s == 0 ? "A" : "B");
        const auto id = extra.getProperty ("id" + suffix).toString();
        if (id.isEmpty()) continue;
        auto& sl = slots[s];
        const juce::ScopedLock lock (sl.lock);
        sl.pendingId = id;
        sl.pendingPath = extra.getProperty ("path" + suffix).toString();
        sl.hasPending = true;
        sl.pendingLoad = true;
    }
    if (ui != nullptr) ui->stateRestored();
}
} // namespace ecr

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ecr::EcrProcessor(); }

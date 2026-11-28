// faltung sndstudio — the studio's own devices as AudioProcessors with stable parameter ids.
#include "StudioDevices.h"
#include "Dsp2.h"
#include "../PatternTools.h"
#include "../../dsp/Fx.h"
#include "../../dsp/Bass.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace fal::daw
{
using namespace fal::dsp2;

// ---- a parameter spec and the processor base ---------------------------------------------------------
struct PSpec
{
    juce::String id, name; float lo = 0, hi = 1, def = 0; float skew = 1.0f; int steps = 0;
    juce::StringArray choices; int kind = 0;   // 0 float, 1 choice, 2 bool
    std::function<juce::String (float)> text;
};
static PSpec F (const char* id, const char* name, float lo, float hi, float def, std::function<juce::String (float)> text = {}, float skew = 1.0f, int steps = 0)
{ PSpec p; p.id = id; p.name = name; p.lo = lo; p.hi = hi; p.def = def; p.text = std::move (text); p.skew = skew; p.steps = steps; return p; }
static PSpec C (const char* id, const char* name, juce::StringArray choices, int def) { PSpec p; p.id = id; p.name = name; p.choices = std::move (choices); p.def = (float) def; p.kind = 1; return p; }
static PSpec B (const char* id, const char* name, bool def) { PSpec p; p.id = id; p.name = name; p.def = def ? 1.0f : 0.0f; p.kind = 2; return p; }
static juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " khz" : juce::String (v, v < 100.0f ? 1 : 0) + " hz"; }
static juce::String dbText (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " db"; }
static juce::String msText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10 ? 1 : 0) + " ms"; }
static juce::String pctText (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }
static juce::String stText (float v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; }
static juce::String plainText (float v) { return juce::String (v, 2); }
static juce::StringArray divNames() { juce::StringArray a; for (auto& d : divisions()) a.add (d.name); return a; }

class StudioFx : public juce::AudioProcessor, public KeyedProcessor
{
public:
    StudioFx (const juce::String& type, const juce::String& displayName, int ins, int outs, bool midi, std::vector<PSpec> specs)
        : juce::AudioProcessor (buses (ins, outs)), typeId (type), name (displayName), wantsMidi (midi), nIns (ins), nOuts (outs),
          apvts (*this, nullptr, "p", layout (specs))
    {
        for (auto& s : specs) raw.push_back (apvts.getRawParameterValue (s.id));
    }
    const juce::String typeId, name; const bool wantsMidi; const int nIns, nOuts;
    juce::AudioProcessorValueTreeState apvts;
    std::vector<std::atomic<float>*> raw;
    const float* keyL = nullptr; const float* keyR = nullptr;
    void setKey (const float* l, const float* r) override { keyL = l; keyR = r; }
    float P (int i) const { return raw[(size_t) i]->load (std::memory_order_relaxed); }
    int Pi (int i) const { return (int) std::round (P (i)); }
    bool Pb (int i) const { return P (i) > 0.5f; }
    void setPlain (const juce::String& id, float plain) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain)); }

    static BusesProperties buses (int ins, int outs)
    {
        BusesProperties b;
        if (ins > 0) b = b.withInput ("in", juce::AudioChannelSet::discreteChannels (ins), true);
        b = b.withOutput ("out", juce::AudioChannelSet::discreteChannels (outs), true);
        return b;
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const int o = l.getMainOutputChannelSet().size();
        if (o < 1 || o > 64) return false;
        if (nIns > 0) { const int i = l.getMainInputChannelSet().size(); if (i < 1 || i > 64) return false; if (i != o) return false; }
        else if (l.inputBuses.size() > 0 && ! l.getMainInputChannelSet().isDisabled()) return false;
        return true;
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout (const std::vector<PSpec>& specs)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout lay;
        for (auto& s : specs)
        {
            if (s.kind == 1) lay.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { s.id, 1 }, s.name, s.choices, (int) s.def));
            else if (s.kind == 2) lay.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { s.id, 1 }, s.name, s.def > 0.5f,
                                           juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return juce::String (v ? "on" : "off"); })));
            else
            {
                juce::NormalisableRange<float> r (s.lo, s.hi, s.steps > 0 ? (s.hi - s.lo) / (float) s.steps : 0.0f, s.skew);
                auto a = juce::AudioParameterFloatAttributes();
                if (s.text) { auto t = s.text; a = a.withStringFromValueFunction ([t] (float v, int) { return t (v); }); }
                lay.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { s.id, 1 }, s.name, r, s.def, a));
            }
        }
        return lay;
    }
    // transport helpers
    double bpm() const { if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) if (auto b = p->getBpm()) return std::max (20.0, *b); return 120.0; }
    double ppq() const { if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) if (auto q = p->getPpqPosition()) return *q; return 0.0; }
    /** sync / free time: when the switch flips, the other side takes over the current time so nothing jumps */
    void keepTime (const juce::String& syncId, const juce::String& divId, const juce::String& msId, bool& lastSync)
    {
        auto* sp = apvts.getRawParameterValue (syncId); auto* dp = apvts.getRawParameterValue (divId); auto* mp = apvts.getRawParameterValue (msId);
        if (! sp || ! dp || ! mp) return;
        const bool sync = sp->load() > 0.5f;
        if (sync == lastSync) return;
        lastSync = sync;
        const double b = bpm();
        if (! sync) setPlain (msId, (float) (divisions()[(size_t) juce::jlimit (0, 18, (int) dp->load())].beats * 60000.0 / b));
        else
        {
            const double beats = mp->load() * 0.001 * b / 60.0; int best = 0; double err = 1e9;
            for (int i = 0; i < 19; ++i) { const double e = std::abs (std::log (divisions()[(size_t) i].beats / beats)); if (e < err) { err = e; best = i; } }
            setPlain (divId, (float) best);
        }
    }
    const juce::String getName() const override { return name; }
    double getTailLengthSeconds() const override { return 4.0; }
    bool acceptsMidi() const override { return wantsMidi; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "init"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        auto x = apvts.copyState().createXml();
        if (x) { saveExtra (*x); copyXmlToBinary (*x, dest); }
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size)) { apvts.replaceState (juce::ValueTree::fromXml (*x)); loadExtra (*x); }
    }
    virtual void saveExtra (juce::XmlElement&) {}
    virtual void loadExtra (const juce::XmlElement&) {}
    void releaseResources() override {}
protected:
    double sr = 48000; int bs = 512;
};

// ---- strip -----------------------------------------------------------------------------------------------
class StripFx : public StudioFx
{
public:
    enum { HPON, HP, LPON, LP, B1ON, B1F, B1G, B1Q, B1T, B2ON, B2F, B2G, B2Q, B2T, B3ON, B3F, B3G, B3Q, B3T, B4ON, B4F, B4G, B4Q, B4T, TILT, SAT, GTHR, GRATIO, GATT, GREL, GRANGE, OUT };
    StripFx() : StudioFx ("st:strip", "strip", 2, 2, false, specs()) {}
    static std::vector<PSpec> specs()
    {
        std::vector<PSpec> v;
        v.push_back (B ("hpon", "high-pass on", false)); v.push_back (F ("hp", "high-pass", 20, 500, 60, hzText, 0.4f));
        v.push_back (B ("lpon", "low-pass on", false)); v.push_back (F ("lp", "low-pass", 2000, 20000, 18000, hzText, 0.5f));
        const float f0[4] = { 90, 300, 1800, 8000 }; const int t0[4] = { 0, 1, 1, 2 };
        for (int k = 1; k <= 4; ++k)
        {
            const auto x = juce::String ("b") + juce::String (k); const auto n = juce::String ("band ") + juce::String (k) + " ";
            v.push_back (B ((x + "on").toRawUTF8(), (n + "on").toRawUTF8(), false));
            v.push_back (F ((x + "f").toRawUTF8(), (n + "freq").toRawUTF8(), 20, 20000, f0[k - 1], hzText, 0.25f));
            v.push_back (F ((x + "g").toRawUTF8(), (n + "gain").toRawUTF8(), -18, 18, 0, dbText));
            v.push_back (F ((x + "q").toRawUTF8(), (n + "q").toRawUTF8(), 0.2f, 12, 0.8f, plainText, 0.4f));
            v.push_back (C ((x + "t").toRawUTF8(), (n + "type").toRawUTF8(), { "low shelf", "bell", "high shelf" }, t0[k - 1]));
        }
        v.push_back (F ("tilt", "tilt", -6, 6, 0, dbText));
        v.push_back (F ("sat", "saturation", 0, 1, 0, pctText));
        v.push_back (F ("gthr", "gate threshold", -80, 0, -80, dbText));
        v.push_back (F ("gratio", "gate ratio", 1, 8, 1, [] (float v) { return "1:" + juce::String (v, 1); }));
        v.push_back (F ("gatt", "gate attack", 0.1f, 50, 1, msText, 0.4f));
        v.push_back (F ("grel", "gate release", 5, 1000, 80, msText, 0.4f));
        v.push_back (F ("grange", "gate range", -80, 0, -60, dbText));
        v.push_back (F ("out", "output", -24, 12, 0, dbText));
        return v;
    }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; ch.resize ((size_t) std::max (1, getTotalNumOutputChannels())); for (auto& c : ch) c.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        StripParams p;
        p.hpOn = Pb (HPON); p.hp = P (HP); p.lpOn = Pb (LPON); p.lp = P (LP);
        for (int k = 0; k < 4; ++k) { auto& b = p.b[(size_t) k]; const int base = B1ON + k * 5; b.on = Pb (base); b.freq = P (base + 1); b.gain = P (base + 2); b.q = P (base + 3); b.type = Pi (base + 4); }
        p.tilt = P (TILT); p.sat = P (SAT); p.gateThr = P (GTHR); p.gateRatio = P (GRATIO); p.gateAtt = P (GATT); p.gateRel = P (GREL); p.gateRange = P (GRANGE); p.out = P (OUT);
        const int n = buf.getNumSamples();
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c)
        {
            ch[(size_t) c].set (p, (float) sr);
            auto* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = ch[(size_t) c].tick (d[i], p);
        }
    }
    std::vector<StripChannel> ch;
};

// ---- transient -------------------------------------------------------------------------------------------
class TransientFx : public StudioFx
{
public:
    enum { ATT, SUS, SPEED, MIX };
    TransientFx() : StudioFx ("st:transient", "transient", 2, 2, false, { F ("attack", "attack", -1, 1, 0, pctText), F ("sustain", "sustain", -1, 1, 0, pctText), F ("speed", "speed", 0, 1, 0.5f, pctText), F ("mix", "mix", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; ch.resize ((size_t) std::max (1, getTotalNumOutputChannels())); for (auto& c : ch) c.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        TransientParams p { P (ATT), P (SUS), P (SPEED), P (MIX) };
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c) { ch[(size_t) c].set (p.speed, (float) sr); auto* d = buf.getWritePointer (c); for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = ch[(size_t) c].tick (d[i], p); }
    }
    std::vector<TransientChannel> ch;
};

// ---- sidechain -------------------------------------------------------------------------------------------
class SidechainFx : public StudioFx
{
public:
    enum { THR, RATIO, ATT, REL, KNEE, MAKEUP, MIX, RANGE, KEY, KEYHP, SHAPE, HOLD };
    SidechainFx() : StudioFx ("st:sidechain", "sidechain", 2, 2, false, {
        F ("threshold", "threshold", -60, 0, -24, dbText), F ("ratio", "ratio", 1, 20, 4, [] (float v) { return juce::String (v, 1) + ":1"; }, 0.5f),
        F ("attack", "attack", 0.1f, 100, 5, msText, 0.4f), F ("release", "release", 10, 2000, 120, msText, 0.4f), F ("knee", "knee", 0, 18, 6, dbText),
        F ("makeup", "makeup", -12, 24, 0, dbText), F ("mix", "mix", 0, 1, 1, pctText), F ("range", "range", -60, 0, -60, dbText),
        C ("key", "key", { "internal", "external (key track)" }, 1), F ("keyhp", "key high-pass", 20, 1000, 80, hzText, 0.4f),
        C ("shape", "shape", { "compressor", "duck", "pump (hold)" }, 0), F ("hold", "hold", 1, 500, 60, msText, 0.5f) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; comp.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        SideParams p; p.threshold = P (THR); p.ratio = P (RATIO); p.attack = P (ATT); p.release = P (REL); p.knee = P (KNEE); p.makeup = P (MAKEUP); p.mix = P (MIX); p.range = P (RANGE);
        p.key = Pi (KEY); p.keyHp = P (KEYHP); p.shape = Pi (SHAPE); p.hold = P (HOLD);
        comp.set (p);
        const int n = buf.getNumSamples(), nc = buf.getNumChannels();
        const bool ext = p.key == 1 && keyL != nullptr;
        for (int i = 0; i < n; ++i)
        {
            float k;
            if (ext) k = 0.5f * (keyL[i] + (keyR ? keyR[i] : keyL[i]));
            else { k = 0; for (int c = 0; c < std::min (2, nc); ++c) k = std::max (k, std::abs (buf.getSample (c, i))); }
            const float g = comp.tick (k, p);
            for (int c = 0; c < nc; ++c) { auto* d = buf.getWritePointer (c); d[i] = d[i] * (1.0f - p.mix) + d[i] * g * p.mix; }
        }
    }
    SideComp comp;
};

// ---- colour ----------------------------------------------------------------------------------------------
class ColourFx : public StudioFx
{
public:
    enum { MODE, DRIVE, BIAS, HF, BUMP, DRIFT, MIX };
    ColourFx() : StudioFx ("st:colour", "colour", 2, 2, false, { C ("mode", "mode", { "tape", "console" }, 0), F ("drive", "drive", 0, 1, 0.2f, pctText), F ("bias", "bias", 0, 1, 0.1f, pctText),
        F ("hf", "hf loss", 0, 1, 0.2f, pctText), F ("bump", "head bump", 0, 1, 0.3f, pctText), F ("drift", "drift", 0, 1, 0.2f, pctText), F ("mix", "mix", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; ch.resize ((size_t) std::max (1, getTotalNumOutputChannels())); uint32_t seed = 77; for (auto& c : ch) c.prepare ((float) sr, seed += 101); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        ColourParams p { P (DRIVE), P (BIAS), P (HF), P (BUMP), P (DRIFT), P (MIX), Pi (MODE) };
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c) { ch[(size_t) c].set (p); auto* d = buf.getWritePointer (c); for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = ch[(size_t) c].tick (d[i], p); }
    }
    std::vector<ColourChannel> ch;
};

// ---- stereo ----------------------------------------------------------------------------------------------
class StereoFx : public StudioFx
{
public:
    enum { WIDTH, MID, SIDE, HAAS, MONO, BAL, SWAP };
    StereoFx() : StudioFx ("st:stereo", "stereo", 2, 2, false, { F ("width", "width", 0, 2, 1, pctText), F ("mid", "mid", -12, 12, 0, dbText), F ("side", "side", -12, 12, 0, dbText),
        F ("haas", "haas", 0, 30, 0, msText), F ("mono", "mono below", 0, 400, 0, hzText), F ("balance", "balance", -1, 1, 0, pctText), B ("swap", "swap", false) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; tool.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        StereoParams p { P (WIDTH), P (MID), P (SIDE), P (HAAS), P (MONO), P (BAL), Pb (SWAP) };
        tool.set (p);
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i) tool.tick (L[i], R[i], p);
    }
    StereoTool tool;
};

// ---- throw -----------------------------------------------------------------------------------------------
class ThrowFx : public StudioFx
{
public:
    enum { SYNC, DIV, MS, FB, HP, LP, SAT, WOW, SPREAD, MIX, THROW, HOLD, INPUT };
    ThrowFx() : StudioFx ("st:throw", "throw", 2, 2, false, { B ("sync", "sync", true), C ("division", "division", divNames(), 9), F ("time", "time", 1, 3000, 375, msText, 0.35f),
        F ("feedback", "feedback", 0, 1.1f, 0.6f, pctText), F ("hp", "loop hp", 20, 2000, 150, hzText, 0.35f), F ("lp", "loop lp", 500, 20000, 4000, hzText, 0.35f),
        F ("sat", "saturation", 0, 1, 0.3f, pctText), F ("wow", "wow", 0, 1, 0.15f, pctText), F ("spread", "ping-pong", 0, 1, 0.6f, pctText), F ("mix", "mix", 0, 1, 1, pctText),
        B ("throw", "throw (input in)", true), B ("hold", "hold", false), F ("input", "input", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; d.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        keepTime ("sync", "division", "time", lastSync);
        ThrowParams p; p.sync = Pb (SYNC); p.div = Pi (DIV); p.ms = P (MS); p.feedback = P (FB); p.hp = P (HP); p.lp = P (LP); p.sat = P (SAT); p.wow = P (WOW); p.spread = P (SPREAD); p.mix = P (MIX); p.thrown = Pb (THROW); p.hold = Pb (HOLD); p.input = P (INPUT);
        d.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples(), p, bpm());
    }
    ThrowDelay d; bool lastSync = true;
};

// ---- spring ----------------------------------------------------------------------------------------------
class SpringFx : public StudioFx
{
public:
    enum { TENSION, DECAY, TONE, DRIP, MIX, FREEZE };
    SpringFx() : StudioFx ("st:spring", "spring", 2, 2, false, { F ("tension", "tension", 0, 1, 0.5f, pctText), F ("decay", "decay", 0, 1, 0.5f, pctText), F ("tone", "tone", 0, 1, 0.5f, pctText),
        F ("drip", "drip", 0, 1, 0.5f, pctText), F ("mix", "mix", 0, 1, 0.3f, pctText), B ("freeze", "freeze", false) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; sp.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        SpringParams p { P (TENSION), P (DECAY), P (TONE), P (DRIP), P (MIX), Pb (FREEZE) };
        sp.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples(), p);
    }
    Spring sp;
};

// ---- vinyl -----------------------------------------------------------------------------------------------
class VinylFx : public StudioFx
{
public:
    enum { WOW, WOWRATE, FLUTTER, CRACKLE, CRACKLEL, NOISE, BW, LOWCUT, SAT, MIX };
    VinylFx() : StudioFx ("st:vinyl", "vinyl", 2, 2, false, { F ("wow", "wow", 0, 1, 0.3f, pctText), F ("wowrate", "wow rate", 0, 1, 0.5f, pctText), F ("flutter", "flutter", 0, 1, 0.2f, pctText),
        F ("crackle", "crackle", 0, 1, 0.3f, pctText), F ("cracklelevel", "crackle level", 0, 1, 0.3f, pctText), F ("noise", "noise floor", 0, 1, 0.1f, pctText),
        F ("bandwidth", "bandwidth", 0, 1, 0.6f, pctText), F ("lowcut", "low cut", 0, 1, 0.1f, pctText), F ("sat", "saturation", 0, 1, 0.3f, pctText), F ("mix", "mix", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; v.prepare ((float) sr, 4242); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        VinylParams p { P (WOW), P (WOWRATE), P (FLUTTER), P (CRACKLE), P (CRACKLEL), P (NOISE), P (BW), P (LOWCUT), P (SAT), P (MIX) };
        v.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples(), p);
    }
    Vinyl v;
};

// ---- repeat ----------------------------------------------------------------------------------------------
class RepeatFx : public StudioFx
{
public:
    enum { SYNC, GRID, GRIDMS, LEN, LENMS, CHANCE, REPEATS, PITCH, REV, BITS, RATE, GATE, MIX, FORCE, VAR };
    RepeatFx() : StudioFx ("st:repeat", "repeat", 2, 2, false, { B ("sync", "sync", true), C ("grid", "grid", divNames(), 4), F ("gridms", "grid ms", 5, 2000, 125, msText, 0.4f),
        C ("length", "length", divNames(), 4), F ("lengthms", "length ms", 5, 2000, 125, msText, 0.4f), F ("chance", "chance", 0, 1, 0.3f, pctText),
        F ("repeats", "repeats", 1, 32, 4, [] (float v) { return juce::String (juce::roundToInt (v)); }, 0.5f, 31), F ("pitch", "pitch decay", -12, 12, 0, stText),
        F ("reverse", "reverse", 0, 1, 0, pctText), F ("bits", "bits", 2, 24, 24, [] (float v) { return juce::String (juce::roundToInt (v)) + " bit"; }, 1.0f, 22),
        F ("rate", "rate divide", 1, 64, 1, [] (float v) { return "1/" + juce::String (juce::roundToInt (v)); }, 0.5f, 63), F ("gate", "gate", 0, 1, 1, pctText), F ("mix", "mix", 0, 1, 1, pctText),
        B ("force", "override (repeat now)", false), F ("variation", "variation", 0, 1, 0, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; r.prepare ((float) sr, 999); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        keepTime ("sync", "grid", "gridms", lastSync);
        RepeatParams p; p.sync = Pb (SYNC); p.gridDiv = Pi (GRID); p.gridMs = P (GRIDMS); p.lenDiv = Pi (LEN); p.lenMs = P (LENMS); p.chance = P (CHANCE); p.repeats = Pi (REPEATS);
        p.pitchDecay = P (PITCH); p.reverse = P (REV); p.bits = P (BITS); p.rateDiv = Pi (RATE); p.gate = P (GATE); p.mix = P (MIX); p.force = Pb (FORCE); p.variation = P (VAR);
        r.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples(), p, ppq(), bpm());
    }
    Repeater r; bool lastSync = true;
};

// ---- grain -----------------------------------------------------------------------------------------------
class GrainFx : public StudioFx
{
public:
    enum { SIZE, DENSITY, PITCH, SPRAY, JITTER, REV, SHAPE, MIX, FREEZE, SPREAD };
    GrainFx() : StudioFx ("st:grain", "grain", 2, 2, false, { F ("size", "size", 5, 500, 80, msText, 0.5f), F ("density", "density", 0, 1, 0.5f, pctText), F ("pitch", "pitch", -24, 24, 0, stText),
        F ("spray", "spray", 0, 1, 0.3f, pctText), F ("jitter", "jitter", 0, 1, 0.2f, pctText), F ("reverse", "reverse", 0, 1, 0, pctText), F ("shape", "shape", 0, 1, 0.5f, pctText),
        F ("mix", "mix", 0, 1, 0.5f, pctText), B ("freeze", "freeze", false), F ("spread", "spread", 0, 1, 0.5f, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; g.prepare ((float) sr, 31337); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        GrainParams p { P (SIZE), P (DENSITY), P (PITCH), P (SPRAY), P (JITTER), P (REV), P (SHAPE), P (MIX), Pb (FREEZE), P (SPREAD) };
        g.process (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples(), p);
    }
    Grainer g;
};

// ---- multiband -------------------------------------------------------------------------------------------
class MultibandFx : public StudioFx
{
public:
    enum { XLO, XHI, D1, M1, L1, D2, M2, L2, D3, M3, L3, MIX };
    MultibandFx() : StudioFx ("st:multiband", "multiband", 2, 2, false, { F ("xlo", "low / mid", 40, 1000, 200, hzText, 0.4f), F ("xhi", "mid / high", 800, 12000, 2500, hzText, 0.4f),
        F ("drive1", "low drive", 0, 1, 0.3f, pctText), C ("mode1", "low mode", { "soft", "fold", "hard" }, 0), F ("level1", "low level", -24, 12, 0, dbText),
        F ("drive2", "mid drive", 0, 1, 0.3f, pctText), C ("mode2", "mid mode", { "soft", "fold", "hard" }, 0), F ("level2", "mid level", -24, 12, 0, dbText),
        F ("drive3", "high drive", 0, 1, 0.3f, pctText), C ("mode3", "high mode", { "soft", "fold", "hard" }, 0), F ("level3", "high level", -24, 12, 0, dbText), F ("mix", "mix", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; ch.resize ((size_t) std::max (1, getTotalNumOutputChannels())); for (auto& c : ch) c.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        MultibandParams p; p.xLo = P (XLO); p.xHi = P (XHI); for (int k = 0; k < 3; ++k) { p.drive[k] = P (D1 + k * 3); p.mode[k] = Pi (M1 + k * 3); p.level[k] = P (L1 + k * 3); } p.mix = P (MIX);
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c) { ch[(size_t) c].set (p); auto* d = buf.getWritePointer (c); for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = ch[(size_t) c].tick (d[i], p); }
    }
    std::vector<MultibandChannel> ch;
};

// ---- ott: three bands, each compressed down above one level and up below another (the over-the-top sound) -------
class OttFx : public StudioFx
{
public:
    enum { DEPTH, TIME, INGAIN, OUTGAIN, UPW, DOWNW, XLO, XHI, LOW, MID, HIGH };
    OttFx() : StudioFx ("st:ott", "ott", 2, 2, false, {
        F ("depth", "depth", 0, 1, 1.0f, pctText), F ("time", "time", 0.1f, 4, 1.0f, [] (float v) { return juce::String (v * 100, 0) + "%"; }, 0.5f),
        F ("in", "in gain", -24, 24, 0, dbText), F ("out", "out gain", -24, 24, 0, dbText),
        F ("upward", "upward", 0, 1, 1.0f, pctText), F ("downward", "downward", 0, 1, 1.0f, pctText),
        F ("xlo", "low / mid", 40, 1000, 120, hzText, 0.4f), F ("xhi", "mid / high", 800, 12000, 2500, hzText, 0.4f),
        F ("low", "low", -24, 24, 0, dbText), F ("mid", "mid", -24, 24, 0, dbText), F ("high", "high", -24, 24, 0, dbText) }) {}
    void prepareToPlay (double s, int b) override
    {
        sr = s; bs = b;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max (16, b), 2 };
        for (auto* f : { &lp1, &hp1, &lp2, &hp2, &ap }) { f->prepare (spec); f->reset(); }
        lp1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); hp1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        lp2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); hp2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        ap.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        env.fill (-120.0f);
    }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), nc = std::min (2, buf.getNumChannels());
        lp1.setCutoffFrequency (P (XLO)); hp1.setCutoffFrequency (P (XLO)); ap.setCutoffFrequency (std::max (P (XHI), P (XLO) * 1.5f));
        lp2.setCutoffFrequency (std::max (P (XHI), P (XLO) * 1.5f)); hp2.setCutoffFrequency (std::max (P (XHI), P (XLO) * 1.5f));
        const float depth = P (DEPTH), up = P (UPW), down = P (DOWNW), tm = P (TIME);
        const float inG = juce::Decibels::decibelsToGain (P (INGAIN)), outG = juce::Decibels::decibelsToGain (P (OUTGAIN));
        const float bandG[3] = { juce::Decibels::decibelsToGain (P (LOW)), juce::Decibels::decibelsToGain (P (MID)), juce::Decibels::decibelsToGain (P (HIGH)) };
        // the bands' own speeds (the lows slower), scaled by time; thresholds and ratios as the classic preset has them
        static const float attMs[3] = { 47.8f, 22.4f, 13.5f }, relMs[3] = { 282.0f, 282.0f, 132.0f };
        static const float dnThr[3] = { -33.8f, -30.2f, -35.5f }, upThr[3] = { -40.8f, -41.8f, -40.8f };
        constexpr float dnRatio = 66.7f, upRatio = 4.17f, maxUp = 36.0f;
        float ca[3], cr[3];
        for (int k = 0; k < 3; ++k) { ca[k] = std::exp (-1.0f / (attMs[k] * tm * 0.001f * (float) sr)); cr[k] = std::exp (-1.0f / (relMs[k] * tm * 0.001f * (float) sr)); }
        for (int i = 0; i < n; ++i)
        {
            float band[3][2] = {};
            float lvl[3] = { 0, 0, 0 };
            for (int c = 0; c < nc; ++c)
            {
                const float x = buf.getSample (c, i) * inG;
                const float lo = lp1.processSample (c, x), rest = hp1.processSample (c, x);
                const float mid = lp2.processSample (c, rest), hi = hp2.processSample (c, rest);
                band[0][c] = ap.processSample (c, lo);   // (through the upper split's all-pass, so the three bands sum flat)
                band[1][c] = mid; band[2][c] = hi;
                for (int k = 0; k < 3; ++k) lvl[k] = std::max (lvl[k], std::abs (band[k][c]));
            }
            for (int k = 0; k < 3; ++k)
            {
                const float db = juce::Decibels::gainToDecibels (lvl[k], -120.0f);
                env[(size_t) k] = db > env[(size_t) k] ? db + (env[(size_t) k] - db) * ca[k] : db + (env[(size_t) k] - db) * cr[k];
                const float e = env[(size_t) k];
                float gDb = 0;
                if (e > dnThr[k]) gDb -= down * (e - dnThr[k]) * (1.0f - 1.0f / dnRatio);
                else if (e < upThr[k] && e > -70.0f) gDb += up * std::min (maxUp, (upThr[k] - e) * (1.0f - 1.0f / upRatio));
                gain[(size_t) k] = juce::Decibels::decibelsToGain (gDb * depth) * bandG[k];
            }
            for (int c = 0; c < nc; ++c) buf.setSample (c, i, (band[0][c] * gain[0] + band[1][c] * gain[1] + band[2][c] * gain[2]) * outG);
        }
    }
    juce::dsp::LinkwitzRileyFilter<float> lp1, hp1, lp2, hp2, ap;
    std::array<float, 3> env {}, gain { 1, 1, 1 };
};

// ---- xmod: the key track's audio modulates this one, sample by sample ---------------------------------------------
class XmodFx : public StudioFx
{
public:
    enum { MODE, AMOUNT, SMOOTH, BASEMS, CUTOFF, RES, MIX, KEYGAIN };
    XmodFx() : StudioFx ("st:xmod", "xmod", 2, 2, false, {
        C ("mode", "mode", { "ring", "amplitude", "phase (delay)", "filter", "fold", "sample & hold" }, 0),
        F ("amount", "amount", 0, 1, 0.5f, pctText), F ("smooth", "smooth", 0, 50, 0, msText, 0.5f),
        F ("basems", "delay", 0.1f, 20, 2, msText, 0.5f), F ("cutoff", "cutoff", 40, 16000, 800, hzText, 0.3f), F ("res", "resonance", 0, 1, 0.3f, pctText),
        F ("mix", "mix", 0, 1, 1, pctText), F ("keygain", "key gain", -24, 24, 0, dbText) }) {}
    void prepareToPlay (double s, int b) override
    {
        sr = s; bs = b; for (auto& d : dl) d.assign ((size_t) (sr * 0.05) + 8, 0.0f); wp = 0; ic1.fill (0); ic2.fill (0); held.fill (0); lastK = 0; sm = 0;
    }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples(), nc = std::min (2, buf.getNumChannels());
        const int mode = Pi (MODE); const float amt = P (AMOUNT), mix = P (MIX), kg = juce::Decibels::decibelsToGain (P (KEYGAIN));
        const float smc = P (SMOOTH) > 0.01f ? std::exp (-1.0f / (P (SMOOTH) * 0.001f * (float) sr)) : 0.0f;
        const int L = (int) dl[0].size();
        for (int i = 0; i < n; ++i)
        {
            const float kraw = keyL ? 0.5f * (keyL[i] + (keyR ? keyR[i] : keyL[i])) * kg : 0.0f;
            sm = kraw + (sm - kraw) * smc;
            const float k = juce::jlimit (-1.0f, 1.0f, sm);
            for (int c = 0; c < nc; ++c)
            {
                const float x = buf.getSample (c, i); float y = x;
                switch (mode)
                {
                    case 0: y = x * k * (amt * 4.0f); break;                                   // ring: the two multiplied
                    case 1: y = x * juce::jlimit (0.0f, 2.0f, 1.0f + amt * k * 2.0f); break;    // amplitude
                    case 2:
                    {
                        auto& d = dl[(size_t) c]; d[(size_t) wp] = x;
                        const float ms = juce::jlimit (0.05f, 45.0f, P (BASEMS) * (1.0f + amt * k * 4.0f));
                        const float pos = (float) wp - ms * 0.001f * (float) sr; float rp = pos; while (rp < 0) rp += (float) L;
                        const int i0 = (int) rp; const float fr = rp - (float) i0;
                        y = d[(size_t) (i0 % L)] * (1.0f - fr) + d[(size_t) ((i0 + 1) % L)] * fr; break;
                    }
                    case 3:
                    {
                        // a state-variable low-pass whose cutoff moves with the key, at audio rate (±4 octaves)
                        const float fc = juce::jlimit (20.0f, (float) sr * 0.45f, P (CUTOFF) * std::exp2 (amt * k * 4.0f));
                        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr), kk = 2.0f - 1.9f * P (RES);
                        const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
                        const float v3 = x - ic2[(size_t) c], v1 = a1 * ic1[(size_t) c] + a2 * v3, v2 = ic2[(size_t) c] + a2 * ic1[(size_t) c] + a3 * v3;
                        ic1[(size_t) c] = 2.0f * v1 - ic1[(size_t) c]; ic2[(size_t) c] = 2.0f * v2 - ic2[(size_t) c]; y = v2; break;
                    }
                    case 4: { const float dr = 1.0f + amt * 6.0f * std::abs (k); y = std::sin (x * dr * 1.5707963f) / std::max (0.5f, std::min (1.0f, dr * 0.5f)); break; }
                    default: { if ((lastK <= 0.0f && k > 0.0f) || amt >= 0.999f) held[(size_t) c] = x; y = held[(size_t) c] * amt + x * (1.0f - amt); break; }
                }
                buf.setSample (c, i, x * (1.0f - mix) + y * mix);
            }
            lastK = k; wp = (wp + 1) % L;
        }
    }
    std::array<std::vector<float>, 2> dl; int wp = 0; std::array<float, 2> ic1 {}, ic2 {}, held {}; float lastK = 0, sm = 0;
};

// ---- clip ------------------------------------------------------------------------------------------------
class ClipFx : public StudioFx
{
public:
    enum { DRIVE, CEIL, BITS, RATE, MODE, MIX, DC };
    ClipFx() : StudioFx ("st:clip", "clip", 2, 2, false, { F ("drive", "drive", 0, 36, 0, dbText), F ("ceiling", "ceiling", -24, 0, 0, dbText), F ("bits", "bits", 2, 24, 24, [] (float v) { return juce::String (juce::roundToInt (v)) + " bit"; }, 1.0f, 22),
        F ("rate", "rate divide", 1, 64, 1, [] (float v) { return "1/" + juce::String (juce::roundToInt (v)); }, 0.5f, 63), C ("mode", "mode", { "hard", "soft", "fold" }, 0), F ("mix", "mix", 0, 1, 1, pctText), B ("dc", "dc filter", true) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; ch.resize ((size_t) std::max (1, getTotalNumOutputChannels())); for (auto& c : ch) c.prepare ((float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        ClipParams p { P (DRIVE), P (CEIL), P (BITS), Pi (RATE), Pi (MODE), P (MIX), Pb (DC) };
        for (int c = 0; c < buf.getNumChannels() && c < (int) ch.size(); ++c) { auto* d = buf.getWritePointer (c); for (int i = 0; i < buf.getNumSamples(); ++i) d[i] = ch[(size_t) c].tick (d[i], p); }
    }
    std::vector<ClipChannel> ch;
};

// ---- noise bed -------------------------------------------------------------------------------------------
class NoiseFx : public StudioFx
{
public:
    enum { COLOUR, LEVEL, HP, LP, DRIFT, CRACKLE };
    NoiseFx() : StudioFx ("st:noise", "noise bed", 2, 2, false, { C ("colour", "colour", { "white", "pink", "tape", "brown" }, 2), F ("level", "level", -80, 0, -40, dbText), F ("hp", "high-pass", 20, 2000, 100, hzText, 0.4f),
        F ("lp", "low-pass", 1000, 20000, 9000, hzText, 0.4f), F ("drift", "drift", 0, 1, 0.3f, pctText), F ("crackle", "crackle", 0, 1, 0, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; rng.seed (8181); hpL.set (100, (float) sr); hpR.set (100, (float) sr); lpL.set (9000, (float) sr); lpR.set (9000, (float) sr); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        if (buf.getNumChannels() < 2) return;
        const int colour = Pi (COLOUR); const float lvl = dbToGain (P (LEVEL)); const float dr = P (DRIFT), cr = P (CRACKLE);
        hpL.set (P (HP), (float) sr); hpR.set (P (HP), (float) sr); lpL.set (P (LP), (float) sr); lpR.set (P (LP), (float) sr);
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            if ((i & 255) == 0) drift += (rng.bi() * dr * 0.3f - drift) * 0.02f;
            auto gen = [&] (PinkNoise& pk, float& brown)
            {
                const float w = rng.bi();
                switch (colour) { case 0: return w; case 1: return pk.tick (w) * 2.0f; case 3: brown = clampf (brown + w * 0.02f, -1.0f, 1.0f); return brown * 3.0f; default: return pk.tick (w) * 1.4f + w * 0.3f; }
            };
            float l = gen (pinkL, brownL), r = gen (pinkR, brownR);
            if (cr > 0.001f && rng.uni() < cr * cr * 0.002f) { l += rng.bi() * 2.0f; r += rng.bi() * 2.0f; }
            l = lpL.lp (hpL.hp (l)); r = lpR.lp (hpR.hp (r));
            const float g = lvl * (1.0f + drift);
            L[i] += l * g; R[i] += r * g;
        }
    }
    Rng rng; PinkNoise pinkL, pinkR; float brownL = 0, brownR = 0, drift = 0; OnePole hpL, hpR, lpL, lpR;
};

// ---- tone generator (instrument) ----------------------------------------------------------------------------
class ToneGen : public StudioFx
{
public:
    enum { WAVE, HZ, PW, KEYTRACK, GATE, DUR, LEVEL, ATT, PAIR, DIFF, RATIO, APART, TUNING };
    ToneGen() : StudioFx ("st:tone", "tone", 0, 2, true, { C ("wave", "wave", { "sine", "pulse", "click", "impulse", "white", "pink" }, 0), F ("hz", "frequency", 0.1f, 20000, 1000, [] (float v) { return juce::String (v, 3) + " hz"; }, 0.3f),
        F ("pw", "pulse width", 0.01f, 0.99f, 0.5f, pctText), B ("keytrack", "key tracking", false), B ("gate", "gate (note length)", false), F ("duration", "duration", 0.1f, 10000, 10, msText, 0.3f),
        F ("level", "level", -60, 0, -12, dbText), F ("attack", "attack", 0, 100, 0, msText),
        // a second pure tone next to the first: the ear makes the difference tone itself when each has its own channel
        C ("pair", "second tone", { "off", "a difference away", "a ratio away" }, 0), F ("difference", "difference", 0.1f, 2000, 40, [] (float v) { return juce::String (v, 2) + " hz"; }, 0.3f),
        F ("ratio", "ratio", 1.01f, 2.0f, 1.22f, [] (float v) { return juce::String (v, 3); }), F ("apart", "one tone per channel", 0, 1, 1, pctText),
        C ("tuning", "tuning", { "equal", "just (5-limit)", "just (7-limit)", "harmonic series" }, 0) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; for (auto& v : voices) { v.on = false; v.rng.seed (17 + (uint32_t) (&v - voices.data())); } ph2.fill (0.0); }
    /** the frequency of a key in a tuning (c is 1/1 in every octave) */
    static float tuned (int note, int tuning)
    {
        static const double t[3][12] = {
            { 1.0, 16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4, 4.0 / 3, 45.0 / 32, 3.0 / 2, 8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8 },
            { 1.0, 567.0 / 512, 9.0 / 8, 147.0 / 128, 21.0 / 16, 1323.0 / 1024, 189.0 / 128, 3.0 / 2, 49.0 / 32, 7.0 / 4, 441.0 / 256, 63.0 / 32 },
            { 1.0, 17.0 / 16, 9.0 / 8, 19.0 / 16, 5.0 / 4, 21.0 / 16, 11.0 / 8, 3.0 / 2, 13.0 / 8, 27.0 / 16, 7.0 / 4, 15.0 / 8 } };
        if (tuning <= 0 || tuning > 3) return 440.0f * std::exp2 ((float) (note - 69) / 12.0f);
        const int deg = ((note % 12) + 12) % 12, oct = (note - deg) / 12 - 5;
        return (float) (261.6255653 * std::exp2 ((double) oct) * t[tuning - 1][deg]);
    }
    std::array<double, 8> ph2 {}; float hzNow = 0.0f;
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        ToneParams p { Pi (WAVE), P (HZ), P (PW), Pb (KEYTRACK), Pb (GATE), P (DUR), P (LEVEL), P (ATT) };
        const int n = buf.getNumSamples(); buf.clear();
        const float g = dbToGain (p.level);
        int pos = 0;
        const int pair = Pi (PAIR); const float apart = P (APART);
        // a tone that is not following the keys follows the frequency control while it sounds: a drawn arc is a glissando
        const float glide = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        if (hzNow <= 0.0f) hzNow = p.hz;
        auto renderTo = [&] (int to)
        {
            for (; pos < to; ++pos)
            {
                hzNow += (p.hz - hzNow) * glide;
                if (! p.keytrack) for (auto& v : voices) if (v.on) v.hz = hzNow;
                float y = 0, y2 = 0;
                for (size_t k = 0; k < voices.size(); ++k)
                {
                    auto& v = voices[k];
                    if (pair != 0 && v.on && ! (p.gate && ! v.held))
                    {
                        const float f2 = pair == 1 ? v.hz + P (DIFF) : v.hz * P (RATIO);
                        const float att = p.attackMs > 0.01f ? std::min (1.0f, (float) v.age / (p.attackMs * 0.001f * (float) sr)) : 1.0f;
                        y2 += std::sin ((float) (kTwoPi * ph2[k])) * att;
                        ph2[k] += f2 / sr; if (ph2[k] >= 1.0) ph2[k] -= 1.0;
                    }
                    y += v.tick (p, (float) sr);
                }
                y *= g; y2 *= g;
                // apart: the first tone on the left only, the second on the right only
                buf.setSample (0, pos, y + y2 * (1.0f - apart));
                if (buf.getNumChannels() > 1) buf.setSample (1, pos, y * (pair != 0 ? 1.0f - apart : 1.0f) + y2);
                for (int c = 2; c < buf.getNumChannels(); ++c) buf.setSample (c, pos, y);
            }
        };
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            renderTo (std::min (n, m.samplePosition));
            if (msg.isNoteOn()) { ToneVoice* free = nullptr; for (auto& v : voices) if (! v.on) { free = &v; break; } if (! free) free = &voices[0]; free->start (msg.getNoteNumber(), msg.getFloatVelocity(), p, (float) sr);
                if (p.keytrack && Pi (TUNING) > 0) free->hz = tuned (msg.getNoteNumber(), Pi (TUNING));
                if (p.keytrack && std::abs (bendSemis) > 1.0e-6f) free->hz *= std::exp2 (bendSemis / 12.0f);   // (the pitch lane's degrees between the keys)
                ph2[(size_t) (free - voices.data())] = 0.0; }
            else if (msg.isNoteOff()) { for (auto& v : voices) if (v.on && v.note == msg.getNoteNumber()) v.release(); }
            else if (msg.isPitchWheel()) bendSemis = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v.on = false;
        }
        renderTo (n);
        midi.clear();
    }
    std::array<ToneVoice, 8> voices; float bendSemis = 0.0f;
};

// ---- data burst generator (instrument) -------------------------------------------------------------------
class DataGen : public StudioFx
{
public:
    enum { RATE, BITS, DENSITY, SEED, LEVEL, TONE };
    DataGen() : StudioFx ("st:data", "data", 0, 2, true, { F ("bitrate", "bit rate", 50, 20000, 2000, [] (float v) { return juce::String (juce::roundToInt (v)) + " bit/s"; }, 0.3f),
        F ("bits", "bits per hit", 1, 512, 64, [] (float v) { return juce::String (juce::roundToInt (v)); }, 0.4f, 511), F ("density", "density", 0, 1, 0.5f, pctText),
        C ("seed", "pattern", { "from the note", "random each hit" }, 0), F ("level", "level", -60, 0, -12, dbText), F ("tone", "tone", 0, 1, 1, pctText) }) {}
    void prepareToPlay (double s, int b) override { sr = s; bs = b; for (auto& v : voices) { v.on = false; v.rng.seed (23 + (uint32_t) (&v - voices.data())); } }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        DataParams p { P (RATE), Pi (BITS), P (DENSITY), Pi (SEED), P (LEVEL), P (TONE) };
        const int n = buf.getNumSamples(); buf.clear(); const float g = dbToGain (p.level);
        int pos = 0;
        auto renderTo = [&] (int to) { for (; pos < to; ++pos) { float y = 0; for (auto& v : voices) y += v.tick (p, (float) sr); y *= g; for (int c = 0; c < buf.getNumChannels(); ++c) buf.setSample (c, pos, y); } };
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            renderTo (std::min (n, m.samplePosition));
            if (msg.isNoteOn()) { DataVoice* free = nullptr; for (auto& v : voices) if (! v.on) { free = &v; break; } if (! free) free = &voices[0]; free->start (msg.getNoteNumber(), p, (float) sr); }
        }
        renderTo (n);
        midi.clear();
    }
    std::array<DataVoice, 4> voices;
};

// ---- fm percussion (instrument) ---------------------------------------------------------------------------
class FmPerc : public StudioFx
{
public:
    enum { R1, L1, D1, R2, L2, D2, R3, L3, D3, R4, L4, D4, ALGO, FB, PITCH, SWEEP, SWEEPMS, NOISE, NOISEDEC, DRIVE, KEYTRACK, LEVEL, MORPHTO, MORPH };
    static std::vector<PSpec> specs()
    {
        std::vector<PSpec> v;
        const float r0[4] = { 1, 2, 3.5f, 7 }, l0[4] = { 1, 0.6f, 0.3f, 0 }, d0[4] = { 0.4f, 0.25f, 0.15f, 0.1f };
        for (int k = 1; k <= 4; ++k)
        {
            const auto x = juce::String (k); const auto n = "op " + x + " ";
            v.push_back (F (("ratio" + x).toRawUTF8(), (n + "ratio").toRawUTF8(), 0.25f, 16, r0[k - 1], [] (float q) { return "x" + juce::String (q, 2); }, 0.5f));
            v.push_back (F (("level" + x).toRawUTF8(), (n + "level").toRawUTF8(), 0, 1, l0[k - 1], pctText));
            v.push_back (F (("decay" + x).toRawUTF8(), (n + "decay").toRawUTF8(), 0.005f, 3, d0[k - 1], [] (float q) { return msText (q * 1000.0f); }, 0.35f));
        }
        v.push_back (C ("algo", "algorithm", { "4>3>2>1", "2+3+4>1", "2>1 and 4>3" }, 0));
        v.push_back (F ("feedback", "feedback", 0, 1, 0.1f, pctText));
        v.push_back (F ("pitch", "pitch", 0, 1, 0.5f, [] (float q) { return hzText (expMap (q, 30.0f, 2000.0f)); }));
        v.push_back (F ("sweep", "sweep", 0, 1, 0.3f, pctText)); v.push_back (F ("sweepms", "sweep time", 1, 500, 40, msText, 0.4f));
        v.push_back (F ("noise", "noise", 0, 1, 0, pctText)); v.push_back (F ("noisedecay", "noise decay", 0.005f, 2, 0.1f, [] (float q) { return msText (q * 1000.0f); }, 0.35f));
        v.push_back (F ("drive", "drive", 0, 1, 0.1f, pctText)); v.push_back (B ("keytrack", "key tracking", true)); v.push_back (F ("level", "level", -24, 6, -6, dbText));
        // preset morph: the sound slides from what the controls say towards one of the presets
        juce::StringArray names; for (auto& pr : presets()) names.add (pr.name);
        v.push_back (C ("morphto", "morph to", names, 3)); v.push_back (F ("morph", "morph", 0, 1, 0, pctText));
        return v;
    }
    struct FmPreset { const char* name; FmParams p; };
    static const std::vector<FmPreset>& presets()
    {
        auto mk = [] (std::array<float, 4> r, std::array<float, 4> l, std::array<float, 4> d, int algo, float fb, float pitch, float sweep, float sweepMs, float noise, float noiseDecay, float drive)
        {
            FmParams p; for (int k = 0; k < 4; ++k) { p.ratio[k] = r[(size_t) k]; p.level[k] = l[(size_t) k]; p.decay[k] = d[(size_t) k]; }
            p.algo = algo; p.feedback = fb; p.pitch = pitch; p.sweep = sweep; p.sweepMs = sweepMs; p.noise = noise; p.noiseDecay = noiseDecay; p.drive = drive;
            return p;
        };
        static const std::vector<FmPreset> v = {
            { "init",       FmParams() },
            { "fm kick",    mk ({ 1, 1.5f, 2, 3 },        { 1, 0.5f, 0.15f, 0 },    { 0.45f, 0.08f, 0.04f, 0.03f }, 0, 0.05f, 0.12f, 0.75f, 30, 0, 0.05f, 0.2f) },
            { "tom",        mk ({ 1, 1.48f, 2.2f, 3 },    { 1, 0.45f, 0.2f, 0 },    { 0.5f, 0.2f, 0.1f, 0.05f }, 0, 0.1f, 0.3f, 0.4f, 60, 0, 0.05f, 0.1f) },
            { "bell",       mk ({ 1, 3.5f, 7.1f, 11 },    { 1, 0.7f, 0.5f, 0.3f },  { 1.6f, 1.2f, 0.7f, 0.4f }, 1, 0.0f, 0.62f, 0.0f, 10, 0, 0.05f, 0.0f) },
            { "snare",      mk ({ 1, 1.81f, 2.4f, 5.3f }, { 0.9f, 0.6f, 0.5f, 0.4f }, { 0.16f, 0.12f, 0.1f, 0.07f }, 2, 0.5f, 0.42f, 0.3f, 25, 0.7f, 0.14f, 0.25f) },
            { "ring hat",   mk ({ 1, 2.63f, 5.41f, 8.2f }, { 0.6f, 0.8f, 0.8f, 0.7f }, { 0.05f, 0.05f, 0.04f, 0.03f }, 1, 0.7f, 0.9f, 0.0f, 5, 0.3f, 0.03f, 0.1f) },
            { "wood",       mk ({ 1, 2.76f, 5.4f, 1 },    { 1, 0.5f, 0.25f, 0 },    { 0.09f, 0.04f, 0.02f, 0.02f }, 0, 0.0f, 0.56f, 0.1f, 8, 0, 0.02f, 0.0f) },
            { "zap",        mk ({ 1, 1, 2, 4 },           { 1, 0.9f, 0.6f, 0.2f },  { 0.25f, 0.2f, 0.12f, 0.1f }, 0, 0.6f, 0.5f, 1.0f, 180, 0, 0.05f, 0.4f) },
            { "glass",      mk ({ 1, 4.07f, 9.3f, 13.1f }, { 0.8f, 0.5f, 0.4f, 0.3f }, { 0.7f, 0.3f, 0.2f, 0.15f }, 1, 0.0f, 0.8f, 0.0f, 5, 0.05f, 0.01f, 0.0f) },
        };
        return v;
    }
    FmPerc() : StudioFx ("st:fm", "fm perc", 0, 2, true, specs()) {}
    int getNumPrograms() override { return (int) presets().size(); }
    int getCurrentProgram() override { return program; }
    const juce::String getProgramName (int i) override { return i >= 0 && i < (int) presets().size() ? juce::String (presets()[(size_t) i].name) : juce::String(); }
    void setCurrentProgram (int i) override
    {
        if (i < 0 || i >= (int) presets().size()) return;
        program = i; const auto& p = presets()[(size_t) i].p;
        for (int k = 0; k < 4; ++k) { const auto x = juce::String (k + 1); setPlain ("ratio" + x, p.ratio[k]); setPlain ("level" + x, p.level[k]); setPlain ("decay" + x, p.decay[k]); }
        setPlain ("algo", (float) p.algo); setPlain ("feedback", p.feedback); setPlain ("pitch", p.pitch); setPlain ("sweep", p.sweep); setPlain ("sweepms", p.sweepMs);
        setPlain ("noise", p.noise); setPlain ("noisedecay", p.noiseDecay); setPlain ("drive", p.drive); setPlain ("morph", 0.0f);
    }
    int program = 0;
    void prepareToPlay (double s, int b) override { sr = s; bs = b; uint32_t seed = 5; for (auto& v : voices) { v.prepare ((float) sr, seed += 97); v.on = false; } }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        FmParams p;
        for (int k = 0; k < 4; ++k) { p.ratio[k] = P (R1 + k * 3); p.level[k] = P (L1 + k * 3); p.decay[k] = P (D1 + k * 3); }
        p.algo = Pi (ALGO); p.feedback = P (FB); p.pitch = P (PITCH); p.sweep = P (SWEEP); p.sweepMs = P (SWEEPMS); p.noise = P (NOISE); p.noiseDecay = P (NOISEDEC); p.drive = P (DRIVE); p.keytrack = Pb (KEYTRACK); p.level_ = P (LEVEL);
        if (const float mo = P (MORPH); mo > 0.0005f)
        {
            const auto& q = presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, Pi (MORPHTO))].p;
            auto mix = [mo] (float a, float b) { return a + (b - a) * mo; };
            for (int k = 0; k < 4; ++k) { p.ratio[k] = p.ratio[k] * std::pow (q.ratio[k] / std::max (0.01f, p.ratio[k]), mo); p.level[k] = mix (p.level[k], q.level[k]); p.decay[k] = mix (p.decay[k], q.decay[k]); }
            p.feedback = mix (p.feedback, q.feedback); p.pitch = mix (p.pitch, q.pitch); p.sweep = mix (p.sweep, q.sweep); p.sweepMs = mix (p.sweepMs, q.sweepMs);
            p.noise = mix (p.noise, q.noise); p.noiseDecay = mix (p.noiseDecay, q.noiseDecay); p.drive = mix (p.drive, q.drive);
            if (mo > 0.5f) p.algo = q.algo;
        }
        const int n = buf.getNumSamples(); buf.clear(); const float g = dbToGain (p.level_);
        int pos = 0;
        auto renderTo = [&] (int to) { for (; pos < to; ++pos) { float y = 0; for (auto& v : voices) y += v.tick (p); y *= g; for (int c = 0; c < buf.getNumChannels(); ++c) buf.setSample (c, pos, y); } };
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            renderTo (std::min (n, m.samplePosition));
            if (msg.isNoteOn()) { FmVoice* free = nullptr; for (auto& v : voices) if (! v.on) { free = &v; break; } if (! free) free = &voices[next++ % voices.size()]; free->start (msg.getNoteNumber(), msg.getFloatVelocity(), p); }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v.on = false;
        }
        renderTo (n);
        midi.clear();
    }
    std::array<FmVoice, 6> voices; size_t next = 0;
};

// ---- sampler (instrument): one-shots, loops, slices to steps --------------------------------------------
class SamplerFx : public StudioFx, public SampleHolder
{
public:
    enum { MODE, ROOT, PITCH, START, END, SLICES, SLICEMODE, CUTOFF, RES, ATT, DEC, SUS, REL, GAIN, CHOKE, REVERSE, VEL, LOOP, MICRO, MICROGRID, SLICEBY, SEED };
    SamplerFx() : StudioFx ("st:sampler", "sampler", 0, 2, true, { C ("mode", "mode", { "one-shot", "loop", "slices" }, 0), F ("root", "root note", 0, 127, 60, [] (float v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); }, 1.0f, 127),
        F ("pitch", "pitch", -24, 24, 0, stText), F ("start", "start", 0, 1, 0, pctText), F ("end", "end", 0, 1, 1, pctText),
        F ("slices", "slices", 1, 64, 16, [] (float v) { return juce::String (juce::roundToInt (v)); }, 1.0f, 63), C ("slicemode", "slice mode", { "grid", "onsets" }, 0),
        F ("cutoff", "cutoff", 20, 20000, 20000, hzText, 0.25f), F ("res", "resonance", 0, 1, 0.1f, pctText),
        F ("attack", "attack", 0, 2000, 1, msText, 0.3f), F ("decay", "decay", 1, 4000, 400, msText, 0.3f), F ("sustain", "sustain", 0, 1, 1, pctText), F ("release", "release", 1, 4000, 30, msText, 0.3f),
        F ("gain", "gain", -24, 12, 0, dbText), B ("choke", "choke (mono)", false), B ("reverse", "reverse", false), F ("vel", "velocity", 0, 1, 0.5f, pctText), B ("loopnote", "loop while held", false),
        // micro-loops: a sliver of the sample from where the note starts, repeated while the note lasts (clicks, crackle,
        // a fragment of a record turned into a tone or a pulse)
        F ("microloop", "micro-loop", 0, 1000, 0, [] (float v) { return v < 0.05f ? juce::String ("off") : juce::String (v, v < 10 ? 2 : 1) + " ms"; }, 0.3f),
        C ("microgrid", "micro-loop length", { "in ms", "1/16 note", "1/32 note", "1/64 note", "1/128 note", "1/256 note" }, 0),
        // more ways to cut (after the two of slice mode): the strongest hits only, where it comes out of a silence, random lengths, the grid moved onto zero crossings
        C ("sliceby", "cut at", { "as slice mode says", "the strongest hits", "after silences", "random lengths", "the grid, on zero crossings" }, 0),
        F ("seed", "random cuts", 0, 99, 1, [] (float v) { return juce::String (juce::roundToInt (v)); }, 1.0f, 99) }) {}
    struct Voice { bool on = false; double pos = 0; double rate = 1; int note = 60; float vel = 1; int start = 0, end = 0; Adsr env; Svf fL, fR; bool rev = false, micro = false; int age = 0; };
    void prepareToPlay (double s, int b) override { sr = s; bs = b; for (auto& v : voices) v.on = false; }
    bool setSampleFile (const juce::File& f) override
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (! rd) return false;
        auto ns = std::make_shared<Sampled>();
        ns->path = f.getFullPathName(); ns->rate = rd->sampleRate;
        ns->data.setSize (2, (int) rd->lengthInSamples);
        rd->read (&ns->data, 0, (int) rd->lengthInSamples, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) ns->data.copyFrom (1, 0, ns->data, 0, 0, ns->data.getNumSamples());
        ns->onsets = detectOnsets (ns->data, ns->rate);
        // the hits by strength (their peak in 10 ms), and the places the sound comes back after a silence (below -40 db for 30 ms)
        {
            std::vector<std::pair<float, int>> st;
            const int pk = (int) (0.01 * ns->rate);
            for (int on : ns->onsets) { float m = 0; for (int i = std::max (0, on - pk / 2); i < std::min (ns->data.getNumSamples(), on + pk); ++i) m = std::max (m, std::abs (ns->data.getSample (0, i))); st.push_back ({ m, on }); }
            std::sort (st.begin(), st.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            for (auto& [m, on] : st) ns->byStrength.push_back (on);
            const int win = std::max (16, (int) (0.005 * ns->rate)), need = std::max (1, (int) (0.03 * ns->rate) / win);
            int quiet = 0;
            for (int i = 0; i + win <= ns->data.getNumSamples(); i += win)
            {
                double e = 0; for (int k = 0; k < win; ++k) e += ns->data.getSample (0, i + k) * ns->data.getSample (0, i + k);
                const bool q = std::sqrt (e / win) < 0.01;
                if (! q && quiet >= need) ns->gaps.push_back (i);
                quiet = q ? quiet + 1 : 0;
            }
        }
        { const juce::SpinLock::ScopedLockType sl (lock); sample = ns; }
        return true;
    }
    juce::String sampleFile() const override { auto s = sample; return s ? s->path : juce::String(); }
    int numSlices() const override { return juce::roundToInt (P (SLICES)); }
    void saveExtra (juce::XmlElement& x) override { if (sample) x.setAttribute ("sample", sample->path); }
    void loadExtra (const juce::XmlElement& x) override { if (x.hasAttribute ("sample")) setSampleFile (juce::File (x.getStringAttribute ("sample"))); }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const int n = buf.getNumSamples(); buf.clear();
        std::shared_ptr<Sampled> s;
        { const juce::SpinLock::ScopedTryLockType sl (lock); if (sl.isLocked()) cur = sample; }
        s = cur;
        if (! s || s->data.getNumSamples() < 4) { midi.clear(); return; }
        const int mode = Pi (MODE), root = Pi (ROOT); const float pitch = P (PITCH), st = P (START), en = P (END); const int slices = std::max (1, Pi (SLICES)), sliceMode = Pi (SLICEMODE);
        const float cutoff = P (CUTOFF), res = P (RES); const float a = P (ATT), d = P (DEC), su = P (SUS), re = P (REL); const float g = dbToGain (P (GAIN)); const bool choke = Pb (CHOKE), rev = Pb (REVERSE); const float vs = P (VEL); const bool loopNote = Pb (LOOP) || mode == 1;
        const int total = s->data.getNumSamples();
        const int s0 = (int) (st * total), s1 = std::max (s0 + 16, (int) (en * total));
        // the micro-loop's length in the file's samples: in ms, or a fraction of the beat at the tempo playing
        int microLen = 0;
        {
            double secs = 0.0; const int grid = Pi (MICROGRID);
            if (grid == 0) secs = P (MICRO) / 1000.0;
            else { double bpm = 120.0; if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b; secs = 60.0 / bpm / (double) (4 << (grid - 1)); }
            if (secs > 0.00005) microLen = std::max (2, (int) std::lround (secs * s->rate));
        }
        auto sliceBounds = [&] (int k, int& from, int& to)
        {
            k = juce::jlimit (0, slices - 1, k);
            const int by = Pi (SLICEBY);
            if (by > 0)
            {
                // the cut points for this way of cutting, in order (at most 64), from s0 to s1
                std::array<int, 66> cuts {}; int nc = 0; cuts[(size_t) nc++] = s0;
                if (by == 1) { std::array<int, 64> top {}; int nt = 0; for (int on : s->byStrength) if (on > s0 && on < s1 && nt < std::min (63, slices - 1)) top[(size_t) nt++] = on; std::sort (top.begin(), top.begin() + nt); for (int i = 0; i < nt; ++i) cuts[(size_t) nc++] = top[(size_t) i]; }
                else if (by == 2) { for (int gp : s->gaps) if (gp > s0 && gp < s1 && nc < std::min (64, slices)) cuts[(size_t) nc++] = gp; }
                else if (by == 3)
                {
                    Rng rr; rr.seed ((uint32_t) (1 + Pi (SEED) * 7919));
                    std::array<double, 64> w {}; double sum = 0; for (int i = 0; i < slices; ++i) { w[(size_t) i] = 0.25 + rr.uni() * 1.5; sum += w[(size_t) i]; }
                    double acc = 0; for (int i = 0; i + 1 < slices && nc < 64; ++i) { acc += w[(size_t) i]; cuts[(size_t) nc++] = s0 + (int) ((s1 - s0) * acc / sum); }
                }
                else
                {
                    const auto* d0 = s->data.getReadPointer (0); const int len = (s1 - s0) / slices;
                    for (int i = 1; i < slices && nc < 64; ++i)
                    {
                        int c = s0 + i * len, best = c; for (int dd = 0; dd < 512; ++dd) { if (c + dd + 1 < total && (d0[c + dd] <= 0.0f) != (d0[c + dd + 1] <= 0.0f)) { best = c + dd; break; } if (c - dd - 1 > 0 && (d0[c - dd] <= 0.0f) != (d0[c - dd - 1] <= 0.0f)) { best = c - dd; break; } }
                        cuts[(size_t) nc++] = best;
                    }
                }
                cuts[(size_t) nc] = s1;
                const int m = std::max (1, nc);
                from = cuts[(size_t) (k % m)]; to = cuts[(size_t) (k % m) + 1]; if (to <= from) to = std::min (s1, from + 64);
                return;
            }
            if (sliceMode == 1 && ! s->onsets.empty())
            {
                const int m = (int) s->onsets.size();
                from = s->onsets[(size_t) (k % m)]; to = k + 1 < m ? s->onsets[(size_t) (k + 1)] : s1;
            }
            else { const int len = (s1 - s0) / slices; from = s0 + k * len; to = from + len; }
        };
        int pos = 0;
        auto renderTo = [&] (int to)
        {
            auto* L = buf.getWritePointer (0); auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
            for (; pos < to; ++pos)
                for (auto& v : voices)
                {
                    if (! v.on) continue;
                    const float e = v.env.tick();
                    if (v.env.st == Adsr::Idle) { v.on = false; continue; }
                    double rp = v.rev ? (v.end - 1 - (v.pos - v.start)) : v.pos;
                    if (rp < 0 || rp >= total - 2) { if (loopNote || v.micro) { v.pos = v.start; rp = v.rev ? v.end - 1 : v.start; } else { v.on = false; continue; } }
                    float l = hermiteAt (s->data.getReadPointer (0), total, rp), r = hermiteAt (s->data.getReadPointer (1), total, rp);
                    if (cutoff < 19000.0f) { l = v.fL.tickLP (l); r = v.fR.tickLP (r); }
                    const float gg = e * v.vel * g;
                    L[pos] += l * gg; if (R != L) R[pos] += r * gg;
                    v.pos += v.rate;
                    if (v.pos >= v.end) { if (loopNote || v.micro) v.pos = v.start + std::fmod (v.pos - v.end, (double) std::max (1, v.end - v.start)); else v.env.off(); }
                }
        };
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            renderTo (std::min (n, m.samplePosition));
            if (msg.isNoteOn())
            {
                if (choke) for (auto& v : voices) if (v.on) v.env.off();
                Voice* free = nullptr; for (auto& v : voices) if (! v.on) { free = &v; break; }
                if (! free) { free = &voices[0]; for (auto& v : voices) if (v.age > free->age) free = &v; }
                auto& v = *free;
                v.on = true; v.note = msg.getNoteNumber(); v.age = 0;
                v.vel = (1.0f - vs) + vs * msg.getFloatVelocity();
                v.env.set (a, d, su, re, (float) sr); v.env.v = 0; v.env.on();
                v.fL.set (cutoff, 0.5f + res * 6.0f, (float) sr); v.fR.set (cutoff, 0.5f + res * 6.0f, (float) sr); v.fL.reset(); v.fR.reset();
                const double fileRate = s->rate / sr;
                if (mode == 2) { int from, to; sliceBounds (v.note - 36, from, to); v.start = from; v.end = std::max (from + 16, to); v.rate = fileRate * std::exp2 (pitch / 12.0); }
                else { v.start = s0; v.end = s1; v.rate = fileRate * std::exp2 ((pitch + bendSemis + (v.note - root)) / 12.0); }
                v.micro = microLen > 0;
                if (v.micro) v.end = std::min (total - 2, v.start + microLen);
                v.rev = rev; v.pos = v.start;
                for (auto& o : voices) ++o.age;
            }
            else if (msg.isNoteOff()) { for (auto& v : voices) if (v.on && v.note == msg.getNoteNumber() && (loopNote || mode != 0 || v.micro)) v.env.off(); }
            else if (msg.isPitchWheel()) bendSemis = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v.on = false;
        }
        renderTo (n);
        midi.clear();
    }
    struct Sampled { juce::String path; juce::AudioBuffer<float> data; double rate = 48000; std::vector<int> onsets, byStrength, gaps; };
    juce::SpinLock lock; std::shared_ptr<Sampled> sample, cur;
    std::array<Voice, 12> voices; float bendSemis = 0.0f;
};

// ---- granular: a sample played as a cloud of grains, a cloud a note ------------------------------------------------
class GranularFx : public StudioFx, public SampleHolder
{
public:
    enum { POS, SPRAY, SIZE, DENSITY, PITCH, PSPRAY, SCAN, REVERSE, WIDTH, ATT, REL, GAIN, ROOT, SHAPE, FOLLOW };
    GranularFx() : StudioFx ("st:granular", "granular", 0, 2, true, {
        F ("pos", "position", 0, 1, 0.3f, pctText), F ("spray", "spray", 0, 1, 0.05f, pctText),
        F ("size", "grain size", 2, 1000, 80, msText, 0.35f), F ("density", "density", 0.5f, 400, 30, [] (float v) { return juce::String (v, v < 10 ? 1 : 0) + " /s"; }, 0.35f),
        F ("pitch", "pitch", -48, 48, 0, stText), F ("pspray", "pitch spray", 0, 24, 0, stText, 0.5f),
        F ("scan", "scan", -2, 2, 0, [] (float v) { return std::abs (v) < 0.005f ? juce::String ("still") : "x" + juce::String (v, 2); }),
        F ("reverse", "reverse grains", 0, 1, 0, pctText), F ("width", "width", 0, 1, 0.6f, pctText),
        F ("attack", "attack", 0, 5000, 20, msText, 0.3f), F ("release", "release", 1, 10000, 400, msText, 0.3f),
        F ("gain", "gain", -36, 12, -6, dbText), F ("root", "root note", 0, 127, 60, [] (float v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, 3); }, 1.0f, 127),
        C ("shape", "grain shape", { "hann", "triangle", "tukey (flat)", "percussive" }, 0), B ("follow", "notes set the pitch", true) }) {}
    struct Sampled { juce::String path; juce::AudioBuffer<float> data; double rate = 48000; };
    bool setSampleFile (const juce::File& f) override
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f)); if (! rd) return false;
        auto ns = std::make_shared<Sampled>(); ns->path = f.getFullPathName(); ns->rate = rd->sampleRate;
        ns->data.setSize (2, (int) rd->lengthInSamples); rd->read (&ns->data, 0, (int) rd->lengthInSamples, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) ns->data.copyFrom (1, 0, ns->data, 0, 0, ns->data.getNumSamples());
        { const juce::SpinLock::ScopedLockType sl (lock); sample = ns; }
        return true;
    }
    juce::String sampleFile() const override { auto s = sample; return s ? s->path : juce::String(); }
    int numSlices() const override { return 1; }
    void saveExtra (juce::XmlElement& x) override { if (sample) x.setAttribute ("sample", sample->path); }
    void loadExtra (const juce::XmlElement& x) override { if (x.hasAttribute ("sample")) setSampleFile (juce::File (x.getStringAttribute ("sample"))); }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; for (auto& v : voices) v = Voice(); for (auto& g : grains) g.on = false; }
    struct Voice { bool on = false; int note = 60; float vel = 1; Adsr env; double scanPos = 0, untilNext = 0; };
    struct Grain { bool on = false; double pos = 0, step = 1; int age = 0, len = 1; float gl = 1, gr = 1; int voice = 0; };
    float window (int shape, float u) const
    {
        switch (shape)
        {
            case 1: return 1.0f - std::abs (2.0f * u - 1.0f);
            case 2: return u < 0.15f ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u / 0.15f) : u > 0.85f ? 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (1.0f - u) / 0.15f) : 1.0f;
            case 3: return std::min (1.0f, u * 40.0f) * std::exp (-5.0f * u);
            default: return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * u);
        }
    }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        const int n = buf.getNumSamples(); buf.clear();
        { const juce::SpinLock::ScopedTryLockType sl (lock); if (sl.isLocked()) cur = sample; }
        auto s = cur; if (! s || s->data.getNumSamples() < 64) { midi.clear(); return; }
        const int total = s->data.getNumSamples();
        for (auto& v : voices) v.env.set (P (ATT), 1.0f, 1.0f, P (REL), (float) sr);
        auto* L = buf.getWritePointer (0); auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        const float gain = juce::Decibels::decibelsToGain (P (GAIN)), width = P (WIDTH);
        const int shape = Pi (SHAPE); const double grainLen = P (SIZE) * 0.001 * s->rate, density = P (DENSITY);
        auto it = midi.cbegin(); const auto end = midi.cend();
        for (int i = 0; i < n; ++i)
        {
            for (; it != end && (*it).samplePosition <= i; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    Voice* v = nullptr; for (auto& x : voices) if (! x.on) { v = &x; break; } if (! v) v = &voices[0];
                    v->on = true; v->note = m.getNoteNumber(); v->vel = m.getFloatVelocity(); v->env.on(); v->scanPos = 0; v->untilNext = 0;
                }
                else if (m.isNoteOff()) for (auto& x : voices) if (x.on && x.note == m.getNoteNumber() && x.env.st != Adsr::R) x.env.off();
            }
            // each sounding note: new grains at its density, around its (scanning) position
            for (int vi = 0; vi < (int) voices.size(); ++vi)
            {
                auto& v = voices[(size_t) vi]; if (! v.on) continue;
                v.scanPos += P (SCAN) / sr;
                v.untilNext -= 1.0;
                if (v.untilNext <= 0)
                {
                    v.untilNext += sr / std::max (0.5, density) * (0.5 + rng.uni());
                    Grain* g = nullptr; for (auto& x : grains) if (! x.on) { g = &x; break; }
                    if (g)
                    {
                        double ps = P (POS) + P (SPRAY) * (rng.uni() * 2.0f - 1.0f) + v.scanPos * s->rate / total;
                        ps -= std::floor (ps);
                        const float semis = P (PITCH) + (Pb (FOLLOW) ? (float) (v.note - Pi (ROOT)) : 0.0f) + P (PSPRAY) * (rng.uni() * 2.0f - 1.0f);
                        const bool rev = rng.uni() < P (REVERSE);
                        const float pan = width * (rng.uni() * 2.0f - 1.0f);
                        g->on = true; g->voice = vi; g->age = 0; g->len = std::max (8, (int) (grainLen * sr / s->rate));
                        g->step = std::exp2 (semis / 12.0) * s->rate / sr * (rev ? -1.0 : 1.0);
                        g->pos = ps * (total - 2); g->gl = std::sqrt (0.5f * (1.0f - pan)); g->gr = std::sqrt (0.5f * (1.0f + pan));
                    }
                }
            }
            float envs[16]; for (int vi = 0; vi < (int) voices.size(); ++vi) { auto& v = voices[(size_t) vi]; envs[vi] = v.on ? v.env.tick() * v.vel : 0.0f; if (v.on && v.env.st == Adsr::Idle) v.on = false; }
            float l = 0, r = 0;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float w = window (shape, (float) g.age / (float) g.len) * envs[g.voice];
                double p = std::fmod (g.pos, (double) (total - 2)); if (p < 0) p += total - 2;
                l += hermiteAt (s->data.getReadPointer (0), total, p) * w * g.gl; r += hermiteAt (s->data.getReadPointer (1), total, p) * w * g.gr;
                g.pos += g.step; if (++g.age >= g.len) g.on = false;
            }
            const float norm = gain / std::sqrt (std::max (1.0f, (float) (density * P (SIZE) * 0.001)));
            L[i] += l * norm; if (R != L) R[i] += r * norm;
        }
        midi.clear();
    }
    juce::SpinLock lock; std::shared_ptr<Sampled> sample, cur;
    std::array<Voice, 12> voices; std::array<Grain, 160> grains; Rng rng;
};

// ---- tape loops: six loops of their own lengths (in seconds), drifting against each other, wearing as they pass -----
class TapeLoopsFx : public StudioFx, public SampleHolder, public LoopLengths
{
public:
    static constexpr int kLoops = 6, kPer = 6;   // per loop: length, start, level, pan, wear, speed
    enum { TARGET = kLoops * kPer, HISS, XFADE };
    static std::vector<PSpec> specs()
    {
        std::vector<PSpec> v;
        static const float lens[kLoops] = { 17.3f, 23.1f, 31.7f, 11.9f, 41.3f, 7.7f };
        for (int i = 0; i < kLoops; ++i)
        {
            const juce::String k (i + 1);
            // (the ids and names made here, as strings: never pointers into something that moves)
            auto spec = [&] (const juce::String& id, const juce::String& name, float lo, float hi, float def, std::function<juce::String (float)> text, float skew = 1.0f) { PSpec p = F ("x", "x", lo, hi, def, std::move (text), skew); p.id = id; p.name = name; return p; };
            v.push_back (spec ("len" + k, "loop " + k + " length", 0.2f, 180.0f, lens[i], [] (float x) { return juce::String (x, x < 10 ? 2 : 1) + " s"; }, 0.35f));
            v.push_back (spec ("start" + k, "loop " + k + " start", 0, 1, 0.0f, pctText));
            v.push_back (spec ("level" + k, "loop " + k + " level", -60, 6, i == 0 ? -6.0f : -60.0f, [] (float x) { return x <= -59.9f ? juce::String ("off") : dbText (x); }));
            v.push_back (spec ("pan" + k, "loop " + k + " pan", -1, 1, 0, pctText));
            v.push_back (spec ("wear" + k, "loop " + k + " wear", 0, 1, 0.2f, pctText));
            v.push_back (spec ("speed" + k, "loop " + k + " speed", 0.25f, 2.0f, 1.0f, [] (float x) { return "x" + juce::String (x, 3); }, 0.5f));
        }
        v.push_back (F ("target", "files go to loop", 1, kLoops, 1, [] (float x) { return juce::String (juce::roundToInt (x)); }, 1.0f, kLoops - 1));
        v.push_back (F ("hiss", "tape hiss", 0, 1, 0.1f, pctText));
        v.push_back (F ("xfade", "seam", 1, 200, 20, msText, 0.5f));
        return v;
    }
    TapeLoopsFx() : StudioFx ("st:tapeloops", "tape loops", 0, 2, true, specs()) {}
    struct Loop { std::shared_ptr<juce::AudioBuffer<float>> data; double rate = 48000; juce::String path; };
    bool setSampleFile (const juce::File& f) override
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f)); if (! rd) return false;
        auto b = std::make_shared<juce::AudioBuffer<float>> (2, (int) rd->lengthInSamples);
        rd->read (b.get(), 0, (int) rd->lengthInSamples, 0, true, rd->numChannels > 1);
        if (rd->numChannels == 1) b->copyFrom (1, 0, *b, 0, 0, b->getNumSamples());
        // into the loop chosen if it is empty, else the next empty one (else the one chosen, replacing it)
        int slot = juce::jlimit (0, kLoops - 1, forced >= 0 ? forced : Pi (TARGET) - 1);
        if (forced < 0 && loops[(size_t) slot].data) for (int k = 1; k < kLoops; ++k) { const int q = (slot + k) % kLoops; if (! loops[(size_t) q].data) { slot = q; break; } }
        { const juce::SpinLock::ScopedLockType sl (lock); loops[(size_t) slot] = { b, rd->sampleRate, f.getFullPathName() }; }
        // a loop that gets a file and was off comes on
        if (P (slot * kPer + 2) <= -59.9f) setPlain ("level" + juce::String (slot + 1), -6.0f);
        return true;
    }
    int forced = -1;
    bool loadInto (int slot, const juce::File& f) { forced = slot; const bool ok = setSampleFile (f); forced = -1; return ok; }
    juce::String sampleFile() const override { const int slot = juce::jlimit (0, kLoops - 1, Pi (TARGET) - 1); return loops[(size_t) slot].path; }
    int numSlices() const override { return kLoops; }
    void saveExtra (juce::XmlElement& x) override { for (int i = 0; i < kLoops; ++i) if (loops[(size_t) i].path.isNotEmpty()) x.setAttribute ("file" + juce::String (i + 1), loops[(size_t) i].path); }
    void loadExtra (const juce::XmlElement& x) override { for (int i = 0; i < kLoops; ++i) if (x.hasAttribute ("file" + juce::String (i + 1))) loadInto (i, juce::File (x.getStringAttribute ("file" + juce::String (i + 1)))); }
    std::vector<double> loopLengths() const override { return lengths(); }
    juce::StringArray files() const override { juce::StringArray a; for (auto& l : loops) if (l.path.isNotEmpty()) a.add (l.path); return a; }
    void replaceFile (const juce::String& old, const juce::File& now) override { for (int i = 0; i < kLoops; ++i) if (loops[(size_t) i].path == old) loadInto (i, now); }
    std::vector<double> lengths() const { std::vector<double> v; for (int i = 0; i < kLoops; ++i) if (P (i * kPer + 2) > -59.9f && loops[(size_t) i].data) v.push_back (P (i * kPer) / P (i * kPer + 5)); return v; }
    void prepareToPlay (double s, int b) override { sr = s; bs = b; for (auto& z : lp) z = {}; }
    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        midi.clear(); buf.clear();
        const int n = buf.getNumSamples();
        double t0 = 0; bool playing = false;
        if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) { playing = p->getIsPlaying(); if (auto s = p->getTimeInSeconds()) t0 = *s; }
        if (! playing) return;
        std::array<Loop, kLoops> ls; { const juce::SpinLock::ScopedTryLockType sl (lock); if (sl.isLocked()) cache = loops; } ls = cache;
        auto* L = buf.getWritePointer (0); auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
        const float hiss = P (HISS); const double xf = std::max (0.001, P (XFADE) * 0.001);
        for (int i = 0; i < kLoops; ++i)
        {
            const auto& lp0 = ls[(size_t) i];
            const float lvlDb = P (i * kPer + 2); if (lvlDb <= -59.9f || ! lp0.data) continue;
            const auto& d = *lp0.data; const int ns = d.getNumSamples(); if (ns < 8) continue;
            const double len = P (i * kPer), speed = P (i * kPer + 5), wear = P (i * kPer + 4);
            const double startSec = P (i * kPer + 1) * ns / lp0.rate;
            const float g = juce::Decibels::decibelsToGain (lvlDb), pan = P (i * kPer + 3);
            const float gl = g * std::sqrt (0.5f * (1.0f - pan)), gr = g * std::sqrt (0.5f * (1.0f + pan));
            for (int k = 0; k < n; ++k)
            {
                // where in the loop (tape seconds), which pass; the seam crossfaded; the wear of this pass
                const double tape = (t0 + k / sr) * speed, u = std::fmod (tape, len); const long long pass = (long long) std::floor (tape / len);
                auto at = [&] (double sec, int ch) { double p = (startSec + sec) * lp0.rate; p = std::fmod (p, (double) (ns - 2)); if (p < 0) p += ns - 2; return hermiteAt (d.getReadPointer (ch), ns, p); };
                float l = at (u, 0), r = at (u, 1);
                if (u > len - xf) { const float w = (float) ((u - (len - xf)) / xf); l = l * (1.0f - w) + at (u - len, 0) * w; r = r * (1.0f - w) + at (u - len, 1) * w; }
                const float decay = (float) std::pow (1.0 - wear * 0.035, (double) pass), cut = (float) std::max (300.0, 18000.0 * std::pow (1.0 - wear * 0.06, (double) pass));
                const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cut / (float) sr);
                lp[(size_t) i].l += (l - lp[(size_t) i].l) * a; lp[(size_t) i].r += (r - lp[(size_t) i].r) * a;
                const float hs = hiss * (float) (0.002 * std::sqrt ((double) pass + 1.0)) * (noise.uni() * 2.0f - 1.0f);
                L[k] += (lp[(size_t) i].l * std::max (0.15f, decay) + hs) * gl; if (R != L) R[k] += (lp[(size_t) i].r * std::max (0.15f, decay) + hs) * gr;
            }
        }
    }
    juce::SpinLock lock; std::array<Loop, kLoops> loops, cache;
    struct Lp { float l = 0, r = 0; }; std::array<Lp, kLoops> lp {};
    Rng noise;
};

#include "Spatial.h"
#include "Loops.h"

// ---- registry ---------------------------------------------------------------------------------------------
struct Entry { const char* type; const char* name; const char* blurb; bool instrument; std::function<std::unique_ptr<juce::AudioProcessor>()> make; };
static const std::vector<Entry>& registry()
{
    static const std::vector<Entry> r = {
        { "st:strip", "strip", "channel strip: hp, lp, four bands, tilt, saturation, gate / expander", false, [] { return std::make_unique<StripFx>(); } },
        { "st:transient", "transient", "transient designer: attack and sustain", false, [] { return std::make_unique<TransientFx>(); } },
        { "st:sidechain", "sidechain", "compressor keyed from any track, with duck and pump shapes", false, [] { return std::make_unique<SidechainFx>(); } },
        { "st:colour", "colour", "tape / console stage with drift", false, [] { return std::make_unique<ColourFx>(); } },
        { "st:stereo", "stereo", "width, mid / side, haas, mono below, balance", false, [] { return std::make_unique<StereoFx>(); } },
        { "st:throw", "throw", "dub send delay with a throw button, hold, filtered saturating feedback", false, [] { return std::make_unique<ThrowFx>(); } },
        { "st:spring", "spring", "spring reverb with freeze", false, [] { return std::make_unique<SpringFx>(); } },
        { "st:vinyl", "vinyl", "wow, flutter, crackle, bandwidth, saturation", false, [] { return std::make_unique<VinylFx>(); } },
        { "st:repeat", "repeat", "beat repeat / buffer glitch with pitch, reverse, bits and rate", false, [] { return std::make_unique<RepeatFx>(); } },
        { "st:grain", "grain", "granular cloud from the live input", false, [] { return std::make_unique<GrainFx>(); } },
        { "st:multiband", "multiband", "three-band distortion", false, [] { return std::make_unique<MultibandFx>(); } },
        { "st:clip", "clip", "digital clipper, bits and rate", false, [] { return std::make_unique<ClipFx>(); } },
        { "st:ott", "ott", "three-band upward and downward compression: the over-the-top sound (basses, risers, whole mixes)", false, [] { return std::make_unique<OttFx>(); } },
        { "st:xmod", "xmod", "the key track's audio modulates this one sample by sample: ring, amplitude, phase, filter, fold, sample & hold", false, [] { return std::make_unique<XmodFx>(); } },
        { "st:noise", "noise bed", "tape hiss / noise floor with drift and crackle", false, [] { return std::make_unique<NoiseFx>(); } },
        { "st:encoder", "encoder", "ambisonic encoder: mono / stereo to order 1 - 7 (acn, sn3d)", false, [] { return std::make_unique<EncoderFx>(); } },
        { "st:decoder", "decoder", "ambisonic decoder to a ring, dome or sphere of speakers", false, [] { return std::make_unique<DecoderFx>(); } },
        { "st:binaural", "binaural", "ambisonics to headphones", false, [] { return std::make_unique<BinauralFx>(); } },
        { "st:line", "line", "lines of sound travelling through the speakers of a wide track", false, [] { return std::make_unique<LineFx>(); } },
        { "st:wear", "wear", "a loop of the input that wears out a little on every pass", false, [] { return std::make_unique<WearFx>(); } },
        { "st:skip", "skip", "a sample read like a skipping disc: fragments repeat, the read point jumps", true, [] { return std::make_unique<SkipFx>(); } },
        { "st:tone", "tone", "precision sine / pulse / click / impulse / noise generator", true, [] { return std::make_unique<ToneGen>(); } },
        { "st:data", "data", "byte-to-pulse bursts", true, [] { return std::make_unique<DataGen>(); } },
        { "st:fm", "fm perc", "four-operator fm percussion", true, [] { return std::make_unique<FmPerc>(); } },
        { "st:granular", "granular", "a sample as a cloud of grains a note: position, spray, size, density, pitch and its spray, scan, reverse, width", true, [] { return std::make_unique<GranularFx>(); } },
        { "st:tapeloops", "tape loops", "six tape loops of their own lengths in seconds, drifting against each other and wearing as they pass (eno, basinski)", true, [] { return std::make_unique<TapeLoopsFx>(); } },
        { "st:sampler", "sampler", "one-shots, loops, slices to steps, micro-loops shorter than a beat", true, [] { return std::make_unique<SamplerFx>(); } },
    };
    return r;
}
std::unique_ptr<juce::AudioProcessor> createStudioProcessor (const juce::String& typeId)
{
    for (auto& e : registry()) if (typeId == e.type) return e.make();
    return nullptr;
}
juce::StringArray studioDeviceTypes() { juce::StringArray a; for (auto& e : registry()) a.add (e.type); return a; }
juce::String studioDeviceName (const juce::String& typeId) { for (auto& e : registry()) if (typeId == e.type) return e.name; return typeId.fromFirstOccurrenceOf (":", false, false); }
juce::String studioDeviceBlurb (const juce::String& typeId) { for (auto& e : registry()) if (typeId == e.type) return e.blurb; return {}; }
bool studioDeviceIsInstrument (const juce::String& typeId) { for (auto& e : registry()) if (typeId == e.type) return e.instrument; return false; }
} // namespace fal::daw

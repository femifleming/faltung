// ballonwinkelTest / ballonpaleisTest
//
// The faltung twin against the plug-in it was made from, and by itself, with no host and no screen.
//
//   (no arguments)         every check; the exit code is the number that failed
//   --params               the parameter table: index, id, name, range, default, the text of the default
//   --screenshots <dir>    a picture of every page of the window (png)
//   --render <out.wav>     ten seconds of the default patch on the test signal (a quick listen)
//
// "The original" here is the original's own processor: tests/original/<product>/PluginProcessor.cpp and
// Presets.cpp are the files of github.com/femifleming/pinkiepiesballoonshop (commit f8b16b3) byte for byte,
// compiled into this program inside a namespace, beside the twin's processor. Both use the same dsp/ headers,
// which the twins carry unchanged (tests/dsp.sha256 lists them; tests/check_dsp.sh compares). The same
// parameter script and the same input go through both, and the two outputs must be the same samples.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

// (the two processors keep a few random generators private that are seeded from the clock: the lightning's,
//  the sky's and, in the palace, the boom's. The comparison needs both sides to roll the same numbers. The twin
//  lets BallonTestAccess in; the original, compiled whole inside this file, is opened up for it. Only the
//  original: MSVC writes a member's access into its linker name, so the twin, whose code is in other files,
//  must be seen here as it is everywhere else.)
#include "PluginProcessor.h"
#define private public
namespace original
{
   #if BALLON_PRODUCT == 0
    #include "original/winkel/PluginProcessor.cpp"
    #include "original/winkel/Presets.cpp"
   #else
    #include "original/paleis/PluginProcessor.cpp"
    #include "original/paleis/Presets.cpp"
   #endif
}
#undef private

#if ! BALLON_TEST_NO_EDITOR
 #include "PluginEditor.h"
 #include "MoreUi.h"
#else
 juce::AudioProcessorEditor* FreakiePieProcessor::createEditor() { return nullptr; }
#endif

using namespace juce;
using New = ::FreakiePieProcessor;
using Old = original::FreakiePieProcessor;

static constexpr bool kPaleis = BALLON_PRODUCT == 1;
static int fails = 0, checks = 0;
static void check (bool ok, const String& what)
{
    ++checks;
    if (! ok) ++fails;
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}
static double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

//==============================================================================
// the test signal: two different channels of tones, sweeps, noise bursts and clicks (the same every time)
struct Signal
{
    uint32_t s = 0x1234567u;
    double ph1 = 0, ph2 = 0, t = 0;
    float noise() { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    void next (double sr, float& l, float& r)
    {
        const double sec = t / sr;
        const double f1 = 110.0 * std::pow (2.0, 3.0 * (0.5 + 0.5 * std::sin (sec * 0.7)));
        const double f2 = 330.0 * std::pow (2.0, 2.0 * (0.5 + 0.5 * std::sin (sec * 0.43 + 1.0)));
        ph1 += f1 / sr; ph2 += f2 / sr;
        const float burst = std::fmod (sec, 1.3) < 0.12 ? 0.5f : 0.0f;
        const float click = std::fmod (sec, 0.77) < 1.0 / sr * 4.0 ? 0.9f : 0.0f;
        const float gate = std::fmod (sec, 5.0) < 4.2 ? 1.0f : 0.0f;   // some silence, so tails are heard alone
        l = gate * (0.35f * (float) std::sin (2.0 * MathConstants<double>::pi * ph1) + burst * noise() + click);
        r = gate * (0.30f * (float) std::sin (2.0 * MathConstants<double>::pi * ph2) + 0.6f * burst * noise() - 0.5f * click);
        t += 1.0;
    }
};

// what a script may do to a processor between blocks (the same for the original and the twin)
template <typename P> static void setParam (P& p, const char* id, float plain)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    else { std::printf ("  (no parameter '%s')\n", id); ++fails; }
}
template <typename P> static void setParam01 (P& p, const String& id, float v01)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
}
struct BallonTestAccess
{
    template <typename P> static void seed (P& p, int64 seed)
    {
        p.strikeRnd.setSeed (seed); p.skyRnd.setSeed (seed + 1);
       #if BALLON_PRODUCT == 1
        p.rbRnd.setSeed (seed + 2);
       #endif
    }
};
template <typename P> static void seedClocks (P& p, int64 seed) { BallonTestAccess::seed (p, seed); }

struct Layout { int ins, outs; bool ambisonic; };

template <typename P> static bool applyLayout (P& p, Layout l)
{
    AudioProcessor::BusesLayout bl;
    bl.inputBuses.add (l.ins == 1 ? AudioChannelSet::mono() : AudioChannelSet::stereo());
    AudioChannelSet out = l.outs == 1 ? AudioChannelSet::mono() : l.outs == 2 ? AudioChannelSet::stereo() : AudioChannelSet::discreteChannels (l.outs);
    if (l.ambisonic) for (int o = 1; o <= 7; ++o) if ((o + 1) * (o + 1) == l.outs) out = AudioChannelSet::ambisonic (o);
    bl.outputBuses.add (out);
    return p.setBusesLayout (bl);
}

// a higher order chosen: in a host the processor's once-a-second timer makes the master stages ready for it (the
// order is held down until then); headless there is no message loop, so run that tick by hand
template <typename P>
static void readyHigherOrder (P& p) { ((juce::Timer&) p).timerCallback(); }   // (a private base: only this cast reaches it)

// a script: called before every block with the processor, the time in seconds and the block's index
template <typename P, typename Script>
static AudioBuffer<float> render (P& p, double sr, double seconds, Layout lay, Script&& script, int blockBase = 512, bool varyBlocks = true)
{
    const bool laid = applyLayout (p, lay);
    if (! laid) { std::printf ("  (layout %d>%d refused)\n", lay.ins, lay.outs); return {}; }
    p.setRateAndBufferSizeDetails (sr, blockBase);
    p.prepareToPlay (sr, blockBase);
    seedClocks (p, 4242);
    const int total = (int) (seconds * sr);
    const int chans = jmax (lay.ins, lay.outs);
    AudioBuffer<float> out (lay.outs, total), buf (chans, blockBase);
    MidiBuffer midi;
    Signal sig;
    static const int sizes[] = { 512, 64, 333, 512, 1, 480, 128, 257 };
    int pos = 0, blk = 0;
    while (pos < total)
    {
        int n = varyBlocks ? jmin (blockBase, sizes[blk % 8]) : blockBase;
        n = jmin (n, total - pos);
        script (p, (double) pos / sr, blk);
        buf.setSize (chans, n, false, false, true);
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            float l, r; sig.next (sr, l, r);
            buf.setSample (0, i, l);
            if (lay.ins > 1) buf.setSample (1, i, r);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < lay.outs; ++c) out.copyFrom (c, pos, buf, c, 0, n);
        pos += n; ++blk;
    }
    return out;
}

struct Diff { double peakA = 0, rmsA = 0, peakD = 0, rmsD = 0; bool finite = true; int firstBad = -1, badChan = -1; };
static Diff compare (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    Diff d;
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples() || a.getNumSamples() == 0) { d.finite = false; return d; }
    double sa = 0, sd = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const float x = a.getSample (c, i), y = b.getSample (c, i);
            if (! std::isfinite (x) || ! std::isfinite (y)) d.finite = false;
            d.peakA = std::max (d.peakA, (double) std::abs (x));
            const double e = std::abs ((double) x - (double) y);
            if (e > d.peakD) d.peakD = e;
            if (e != 0.0 && d.firstBad < 0) { d.firstBad = i; d.badChan = c; }
            sa += (double) x * x; sd += e * e;
        }
    const double n = (double) a.getNumChannels() * a.getNumSamples();
    d.rmsA = std::sqrt (sa / n); d.rmsD = std::sqrt (sd / n);
    return d;
}

// both processors, the same script: the outputs must be the same samples
template <typename Script>
static void nullTest (const String& name, double sr, double seconds, Layout lay, Script&& script, bool mustSound = true)
{
    Old a; New b;
    const auto oa = render (a, sr, seconds, lay, script);
    const auto ob = render (b, sr, seconds, lay, script);
    const auto d = compare (oa, ob);
    String s = name + String::formatted ("  [%d>%d ch, %.0f Hz, %.0f s]  out %.1f dBFS rms, peak %.1f; difference %s",
                                         lay.ins, lay.outs, sr, seconds, db (d.rmsA), db (d.peakA),
                                         d.peakD == 0.0 ? "none (the same samples)" : String::formatted ("peak %.1f dB, rms %.1f dB below the signal (first at sample %d, channel %d)",
                                                                                                          db (d.peakD), db (d.rmsA) - db (d.rmsD), d.firstBad, d.badChan).toRawUTF8());
    check (d.finite && d.peakD == 0.0 && (! mustSound || d.rmsA > 1.0e-4), s);
}

//==============================================================================
// the scripts
static const char* const kToggles[] = { "rain", "helium", "cloudy", "fog", "hail", "aurora", "sgFreeze", "grFreeze", "grRandOct", "balloonSfx", "popsThru", "fxBypass" };

template <typename P> static void walkParams (P& p, double t, int blk, uint32_t& lcg, bool weather)
{
    // every continuous control wanders (a new place about four times a second, one control at a time),
    // switches flip now and then, the store opens and closes
    if (blk % 23 != 0) return;
    auto next = [&lcg] { lcg = lcg * 1664525u + 1013904223u; return (float) (lcg >> 8) * (1.0f / 16777216.0f); };
    auto& params = p.getParameters();
    for (int k = 0; k < 3; ++k)
    {
        auto* prm = dynamic_cast<RangedAudioParameter*> (params[(int) (next() * (float) params.size()) % params.size()]);
        if (prm == nullptr) continue;
        const String id = prm->getParameterID();
        const bool isSwitch = dynamic_cast<AudioParameterBool*> (prm) != nullptr;
        // (auto weather rolls its own dice from the clock, on the message thread: left alone here; the snow
        //  and the heat freeze everything for minutes: they get their own script)
        if (id == "autoWeather" || id == "snow" || id == "heat" || id == "twister" || id == "rbBoom") continue;
        if (! weather && (id == "wind" || id == "cloudy" || id == "rain")) continue;
        if (isSwitch) { if (next() < 0.35f) prm->setValueNotifyingHost (next() < 0.5f ? 1.0f : 0.0f); }
        else prm->setValueNotifyingHost (next());
    }
    ignoreUnused (t);
}

template <typename P> static void playBalloons (P& p, double t, int blk)
{
    // what the window does through the processor's atomics: the pumps, pops, caught sounds, the board, the order
    auto& fx = p.balloonFX;
    fx.pumping = (t > 0.5 && t < 3.0) || (t > 7.0 && t < 8.0);
    fx.pumpSize = (float) jlimit (0.0, 1.0, (t - 0.5) / 2.5);
    if (blk == 300 || blk == 520 || blk == 900) { fx.popSize = 0.4f + 0.1f * (float) (blk % 7); ++fx.popRequests; }
    // catch two sounds: inflate (record), then freeze (play), retune one, pop one
    auto& v0 = fx.voice[0]; auto& v1 = fx.voice[1];
    if (t > 1.0 && t < 2.5) { v0.mode = 1; v0.size = (float) ((t - 1.0) / 1.5); }
    if (t >= 2.5 && t < 9.0) v0.mode = 2;
    if (t >= 9.0) v0.mode = 0;
    if (t > 3.0 && t < 3.8) { v1.mode = 1; v1.size = (float) ((t - 3.0) / 0.8); v1.semis = 7.0f; }
    if (t >= 3.8) { v1.mode = 2; v1.semis = t > 6.0 ? -5.0f : 7.0f; }
    if (blk == 200)
    {
        for (int i = 0; i < fp::ChalkShape::N; ++i) p.chalk.set (i, 0.5f + 0.5f * std::sin (0.37f * (float) i));
        p.chalk.updatePower();
        setParam (p, "grShape", 6.0f);
    }
    if (blk == 420)
    {
        for (int i = 0; i < fp::EqCurve::N; ++i) p.eq.set (i, 0.5f + 0.3f * std::sin (0.21f * (float) i));
        p.eq.fit();
    }
    if (blk == 650) { auto o = fp::Engine::defaultOrder(); std::reverse (o.begin(), o.begin() + 5); p.setShelfOrder (o); }
    if (blk == 1000) { auto o = fp::Engine::defaultOrder(); std::swap (o[1], o[7]); std::swap (o[0], o[3]); p.setShelfOrder (o); }
    if (blk == 500) setParam (p, "grainsThru", 1.0f);
    if (blk == 800) setParam (p, "grainsThru", 0.35f);
    if (blk == 700) setParam (p, "catchFrom", 1.0f);
    if (blk == 1100) p.resetToNormal();
}

template <typename P> static void weatherScript (P& p, double t, int blk)
{
    auto on = [&] (const char* id, double from, double to) { setParam (p, id, t >= from && t < to ? 1.0f : 0.0f); };
    if (blk % 8 != 0) return;
    on ("rain", 0.5, 6.0); on ("cloudy", 1.0, 9.0);            // a storm from 1 to 6 s: lightning lends store effects
    on ("wind", 2.0, 7.0);                                       // windy and cloudy: a twister may come by itself
    on ("hail", 3.0, 8.0); on ("aurora", 4.0, 10.0); on ("fog", 7.5, 11.0); on ("helium", 5.0, 9.5);
    on ("twister", 6.0, 6.5);                                    // the switch: one twister
    on ("rbBoom", 8.0, 8.4);                                     // the boom, from its switch
    if (blk == 1000) p.requestTwister();
    if (blk == 1304) p.requestRainbow();
    if (blk == 1600) p.requestRainboom (0.5f);
    if (blk == 1904) p.trixieNoise.request (77u, 2.0f);
    setParam (p, "rbHits", t > 9.0 ? 1.0f : 0.0f);
}

template <typename P> static void snowAndHeat (P& p, double t, int blk)
{
    if (blk == 0) { setParam (p, "snowIn", 5.0f); setParam (p, "snowOut", 5.0f); setParam (p, "heatIn", 5.0f); setParam (p, "heatOut", 5.0f); }
    if (blk % 8 != 0) return;
    setParam (p, "heat", t >= 0.5 && t < 8.0 ? 1.0f : 0.0f);
    setParam (p, "snow", t >= 10.0 && t < 18.0 ? 1.0f : 0.0f);
}

//==============================================================================
static String rangeText (RangedAudioParameter& p)
{
    const auto r = p.getNormalisableRange();
    String s = String (r.start) + " .. " + String (r.end);
    if (r.interval > 0.0f) s << " step " << String (r.interval);
    if (std::abs (r.skew - 1.0f) > 1.0e-6f) s << " skew " << String (r.skew, 4);
    if (auto* c = dynamic_cast<AudioParameterChoice*> (&p)) s = String (c->choices.size()) + " choices";
    if (dynamic_cast<AudioParameterBool*> (&p) != nullptr) s = "off / on";
    return s;
}

static void printParams()
{
    New p;
    std::printf ("%s: %d parameters, %d programs, latency after prepare at 48 kHz: ", BALLON_NAME, p.getParameters().size(), p.getNumPrograms());
    p.prepareToPlay (48000.0, 512);
    std::printf ("%d samples\n", p.getLatencySamples());
    std::printf ("%-4s %-22s %-34s %-30s %-12s %s\n", "#", "id", "name", "range", "default", "the default reads");
    int i = 0;
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
        {
            const float def = r->convertFrom0to1 (r->getDefaultValue());
            std::printf ("%-4d %-22s %-34s %-30s %-12s %s\n", i++, r->getParameterID().toRawUTF8(), r->getName (64).toRawUTF8(),
                         rangeText (*r).toRawUTF8(), String (def, 4).toRawUTF8(), r->getText (r->getDefaultValue(), 64).toRawUTF8());
        }
    std::printf ("\nprograms:\n");
    for (int k = 0; k < p.getNumPrograms(); ++k) std::printf ("  %2d  %s\n", k, p.getProgramName (k).toRawUTF8());
}

// the twin's parameters against the original's: the same ids in the same order, the same ranges, defaults and steps
static void parameterChecks()
{
    std::printf ("\nparameters (the twin against the original)\n");
    Old a; New b;
    auto& pa = a.getParameters(); auto& pb = b.getParameters();
    check (pa.size() == pb.size(), "the same number of parameters (" + String (pb.size()) + ")");
    int sameId = 0, sameRange = 0, sameDefault = 0, sameSteps = 0, sameKind = 0, sameText = 0, lower = 0, sameStart = 0;
    const int n = jmin (pa.size(), pb.size());
    StringArray textDiffers;
    for (int i = 0; i < n; ++i)
    {
        auto* x = dynamic_cast<RangedAudioParameter*> (pa[i]); auto* y = dynamic_cast<RangedAudioParameter*> (pb[i]);
        if (x == nullptr || y == nullptr) continue;
        sameId += x->getParameterID() == y->getParameterID() ? 1 : 0;
        bool r = true;
        for (float v : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f }) r = r && x->convertFrom0to1 (v) == y->convertFrom0to1 (v);
        sameRange += r ? 1 : 0;
        sameDefault += x->getDefaultValue() == y->getDefaultValue() ? 1 : 0;
        sameSteps += x->getNumSteps() == y->getNumSteps() && x->isDiscrete() == y->isDiscrete() && x->isBoolean() == y->isBoolean() ? 1 : 0;
        sameKind += (dynamic_cast<AudioParameterBool*> (x) != nullptr) == (dynamic_cast<AudioParameterBool*> (y) != nullptr)
                 && (dynamic_cast<AudioParameterChoice*> (x) != nullptr) == (dynamic_cast<AudioParameterChoice*> (y) != nullptr)
                 && (dynamic_cast<AudioParameterInt*> (x) != nullptr) == (dynamic_cast<AudioParameterInt*> (y) != nullptr) ? 1 : 0;
        bool t = true;
        for (float v : { 0.0f, 0.3f, 0.5f, 1.0f }) t = t && x->getText (v, 64).toLowerCase() == y->getText (v, 64).toLowerCase();
        sameText += t ? 1 : 0;
        if (! t) textDiffers.add (y->getParameterID());
        lower += y->getName (64) == y->getName (64).toLowerCase() ? 1 : 0;
        sameStart += x->getValue() == y->getValue() ? 1 : 0;   // (the original here has no last-patch file: see main)
    }
    check (sameId == n, "the same ids in the same order (" + String (sameId) + " of " + String (n) + ")");
    check (sameRange == n, "the same ranges and curves (" + String (sameRange) + " of " + String (n) + ")");
    check (sameDefault == n, "the same defaults (" + String (sameDefault) + " of " + String (n) + ")");
    check (sameSteps == n && sameKind == n, "the same kinds and steps (" + String (jmin (sameSteps, sameKind)) + " of " + String (n) + ")");
    check (sameText == n, "the same value texts, but for the case of the units (" + String (sameText) + " of " + String (n) + ")" + (textDiffers.isEmpty() ? String() : ": " + textDiffers.joinIntoString (" ")));
    check (lower == n, "every name in lowercase, as the suite writes them (" + String (lower) + " of " + String (n) + ")");
    check (sameStart == n, "a new one starts where a new original does (its default patch): " + String (sameStart) + " of " + String (n) + " values");
    check (a.getNumPrograms() == b.getNumPrograms(), "the same number of programs (" + String (b.getNumPrograms()) + ")");
    // every program sets the same values
    int same = 0;
    for (int k = 0; k < jmin (a.getNumPrograms(), b.getNumPrograms()); ++k)
    {
        a.setCurrentProgram (k); b.setCurrentProgram (k);
        bool eq = true;
        for (int i = 0; i < n; ++i) eq = eq && pa[i]->getValue() == pb[i]->getValue();
        same += eq ? 1 : 0;
    }
    check (same == b.getNumPrograms(), "every program sets what the original's does (" + String (same) + " of " + String (b.getNumPrograms()) + ")");
    check (a.getTailLengthSeconds() == b.getTailLengthSeconds() && a.acceptsMidi() == b.acceptsMidi() && a.producesMidi() == b.producesMidi(),
           "the same tail (" + String (b.getTailLengthSeconds()) + " s) and no MIDI either way, as the original");
    a.prepareToPlay (48000.0, 512); b.prepareToPlay (48000.0, 512);
    check (a.getLatencySamples() == b.getLatencySamples(), "the same latency (" + String (b.getLatencySamples()) + " samples at 48 kHz)");
    check (b.getName() == BALLON_NAME, "its own name: " + b.getName());
    check (a.apvts.state.getType() != b.apvts.state.getType(), "its own state: <" + b.apvts.state.getType().toString() + "> (the original's is <" + a.apvts.state.getType().toString() + ">)");
}

// the bus layouts each accepts
static void layoutChecks()
{
    std::printf ("\nbus layouts (the twin against the original)\n");
    Old a; New b;
    StringArray accepted;
    int same = 0, tried = 0;
    for (int ins : { 1, 2, 4 })
        for (int outs : { 1, 2, 3, 4, 6, 8, 9, 16, 25, 36, 49, 64, 32 })
            for (bool amb : { false, true })
            {
                AudioProcessor::BusesLayout bl;
                bl.inputBuses.add (ins == 1 ? AudioChannelSet::mono() : ins == 2 ? AudioChannelSet::stereo() : AudioChannelSet::discreteChannels (ins));
                AudioChannelSet out = outs == 1 ? AudioChannelSet::mono() : outs == 2 ? AudioChannelSet::stereo() : AudioChannelSet::discreteChannels (outs);
                bool isAmb = false;
                if (amb) for (int o = 1; o <= 7; ++o) if ((o + 1) * (o + 1) == outs) { out = AudioChannelSet::ambisonic (o); isAmb = true; }
                if (amb && ! isAmb) continue;
                bl.outputBuses.add (out);
                const bool x = a.checkBusesLayoutSupported (bl), y = b.checkBusesLayoutSupported (bl);
                ++tried; same += x == y ? 1 : 0;
                if (y) accepted.add (String (ins) + ">" + String (outs) + (isAmb ? " (ambisonic)" : ""));
            }
    check (same == tried, "accepts and refuses the same layouts as the original (" + String (same) + " of " + String (tried) + " tried)");
    std::printf ("        accepted: %s\n", accepted.joinIntoString (", ").toRawUTF8());
    check (a.getBusesLayout() == b.getBusesLayout(), "the same default layout: " + String (b.getTotalNumInputChannels()) + " in, " + String (b.getTotalNumOutputChannels()) + " out ("
                                                      + b.getBusesLayout().getMainOutputChannelSet().getDescription() + ")");
}

static void nullChecks()
{
    std::printf ("\nthe same sound (the original's processor and the twin's, the same script and input)\n");
    const Layout main = kPaleis ? Layout { 2, 16, true } : Layout { 2, 2, false };
    auto nothing = [] (auto&, double, int) {};
    nullTest ("the default patch", 48000.0, 8.0, main, nothing);
    nullTest ("the default patch", 44100.0, 4.0, main, nothing);
    nullTest ("the default patch", 96000.0, 3.0, main, nothing);
    nullTest ("mono in", 48000.0, 4.0, Layout { 1, main.outs, main.ambisonic }, nothing);
    if (! kPaleis) nullTest ("mono in and out", 48000.0, 4.0, Layout { 1, 1, false }, nothing);
    if (kPaleis)
    {
        nullTest ("on a stereo track", 48000.0, 4.0, Layout { 2, 2, false }, nothing);
        nullTest ("1st order", 48000.0, 3.0, Layout { 2, 4, true }, [] (auto& p, double, int blk) { if (blk == 0) setParam (p, "ambiOrder", 0.0f); });
        nullTest ("7th order, 64 channels", 48000.0, 3.0, Layout { 2, 64, true }, [] (auto& p, double, int blk) { if (blk == 0) { setParam (p, "ambiOrder", 6.0f); readyHigherOrder (p); } });
        nullTest ("16 plain channels", 48000.0, 3.0, Layout { 2, 16, false }, nothing);
    }
    {
        Old a;
        const int np = a.getNumPrograms();
        nullTest ("every program in turn (" + String (np) + ")", 48000.0, 2.5 * np, main,
                  [] (auto& p, double t, int) { const int k = (int) (t / 2.5); if (k != p.getCurrentProgram() && k < p.getNumPrograms()) p.setCurrentProgram (k); });
    }
    {
        uint32_t l = 99;   // (each render starts the walk again)
        nullTest ("every control wandering, switches flipping, the store opening and closing", 48000.0, 30.0, main,
                  [&] (auto& p, double t, int blk) { if (blk == 0) l = 99; walkParams (p, t, blk, l, false); });
    }
    {
        uint32_t l = 7;
        nullTest ("the same with wind, cloud and rain (storms, lightning, twisters)", 48000.0, 30.0, main,
                  [&] (auto& p, double t, int blk) { if (blk == 0) l = 7; walkParams (p, t, blk, l, true); });
    }
    nullTest ("the pumps, pops, caught sounds, the board's shape and eq, the order, reset", 48000.0, 14.0, main,
              [] (auto& p, double t, int blk) { playBalloons (p, t, blk); });
    nullTest ("the weather: storm, wind, hail, aurora, fog, helium, twister, rainbow, the boom, the visitor's noise", 48000.0, 14.0, main,
              [] (auto& p, double t, int blk) { weatherScript (p, t, blk); });
    nullTest ("a heatwave, then snow (5 s fades)", 48000.0, 24.0, main, [] (auto& p, double t, int blk) { snowAndHeat (p, t, blk); });
    nullTest ("the stutter bought, on a host's tempo", 48000.0, 8.0, main,
              [] (auto& p, double, int blk) { if (blk == 0) { setParam (p, "stOwned", 1.0f); setParam (p, "stAmount", 0.8f); setParam (p, "stCutup", 0.6f); setParam (p, "stSway", 0.3f); } });
    if (kPaleis)
    {
       #if BALLON_PRODUCT == 1
        for (int mode = 0; mode < 3; ++mode)
            nullTest (String ("grain mode ") + (mode == 0 ? "spatial" : mode == 1 ? "scatter" : "copies") + ", 9 grains, every flight in turn", 48000.0, 2.0 * fp::Flight::numModes, main,
                      [mode] (auto& p, double t, int blk)
                      {
                          if (blk == 0) { setParam (p, "grainMode", (float) mode); setParam (p, "grains", 9.0f); setParam (p, "deviate", 0.8f); setParam (p, "flightSpeed", 0.8f); }
                          if (blk % 16 == 0) setParam (p, "flight", (float) jmin (fp::Flight::numModes - 1, (int) (t / 2.0)));
                          if (blk == 400) p.setGrainHome (2, { 0.3f, -0.5f, 0.4f });
                          if (blk == 600) { p.srcX = -0.4f; p.srcY = 0.3f; p.srcZ = 0.2f; }
                          if (blk == 700) setParam (p, "spread", 0.9f);
                          if (blk == 900) setParam (p, "doppler", 0.0f);
                          if (blk == 1000) { p.heldGrain = 1; p.heldX = 0.2f; p.heldY = 0.2f; p.heldZ = 0.6f; }
                          if (blk == 1200) p.heldGrain = -1;
                      });
        nullTest ("the boom in the sphere: the switch, a throw followed, fast grains", 48000.0, 12.0, main,
                  [] (auto& p, double t, int blk)
                  {
                      if (blk == 0) { setParam (p, "rbFast", 1.0f); setParam (p, "flight", 10.0f); setParam (p, "flightSpeed", 1.0f); setParam (p, "spread", 1.0f); }
                      if (blk % 8 == 0) setParam (p, "rbBoom", t > 1.0 && t < 1.5 ? 1.0f : 0.0f);
                      if (blk == 900) { p.rbTargetX = 0.5f; p.rbTargetY = -0.2f; p.rbTargetZ = 0.3f; p.requestRainboom (0.6f, true); }
                  });
       #endif
    }
}

// by itself: nothing random at the start, two renders alike, state there and back, channels kept apart
static void selfChecks()
{
    std::printf ("\nby itself\n");
    {
        // every new one starts the same, on program 0, whatever was open before
        std::vector<float> first;
        bool same = true; int prog = 0;
        for (int k = 0; k < 4; ++k)
        {
            New p;
            if (k == 1) { p.setCurrentProgram (3); uint32_t l = 5; for (int i = 0; i < 400; ++i) walkParams (p, 0.0, 0, l, true); }   // (leave a mess behind in one of them)
            New q;
            std::vector<float> v;
            for (auto* prm : q.getParameters()) v.push_back (prm->getValue());
            if (first.empty()) first = v; else same = same && v == first;
            prog += q.getCurrentProgram();
        }
        check (same && prog == 0, "every new instance starts with the same values, on program 0 (" + New().getProgramName (0) + "): nothing random, no last patch read");
        New p;
        int atDefault = 0, n = 0;
        for (auto* prm : p.getParameters()) { ++n; atDefault += prm->getValue() == prm->getDefaultValue() ? 1 : 0; }
        std::printf ("        (%d of %d values are the parameters' own defaults; the rest are program 0's, as in the original)\n", atDefault, n);
    }
    const Layout main = kPaleis ? Layout { 2, 16, true } : Layout { 2, 2, false };
    auto busy = [] (auto& p, double t, int blk) { playBalloons (p, t, blk); };
    {
        New a, b;
        const auto d = compare (render (a, 48000.0, 10.0, main, busy), render (b, 48000.0, 10.0, main, busy));
        check (d.finite && d.peakD == 0.0 && d.rmsA > 1.0e-4, "two instances given the same script play the same samples");
    }
    {
        // state: there and back, in another instance, the values and the sound
        New a;
        a.setCurrentProgram (kPaleis ? 2 : 3);
        setParam (a, "psShift", -5.0f); setParam (a, "grSize", 333.0f); setParam (a, "stRingModOwned", 1.0f); setParam (a, "stRingModA", 620.0f);
        setParam (a, "rain", 1.0f); setParam (a, "grShape", 6.0f); setParam (a, "psScale", 4.0f); setParam (a, "snowIn", 77.0f);
        if (kPaleis) { setParam (a, "ambiOrder", 4.0f); setParam (a, "flight", 7.0f); setParam (a, "grains", 11.0f); }
        for (int i = 0; i < fp::ChalkShape::N; ++i) a.chalk.set (i, (float) (i % 5) / 4.0f);
        a.chalk.updatePower();
        for (int i = 0; i < fp::EqCurve::N; ++i) a.eq.set (i, 0.5f + 0.2f * std::cos (0.3f * (float) i));
        a.eq.fit();
        auto o = fp::Engine::defaultOrder(); std::rotate (o.begin(), o.begin() + 2, o.begin() + 6); a.setShelfOrder (o);
        MemoryBlock mb; a.getStateInformation (mb);
        New b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        int same = 0, n = 0;
        for (int i = 0; i < a.getParameters().size(); ++i) { ++n; same += a.getParameters()[i]->getValue() == b.getParameters()[i]->getValue() ? 1 : 0; }
        bool chalk = true, eq = true;
        for (int i = 0; i < fp::ChalkShape::N; ++i) chalk = chalk && std::abs (a.chalk.v[(size_t) i].load() - b.chalk.v[(size_t) i].load()) < 1.0e-4f;
        for (int i = 0; i < fp::EqCurve::N; ++i) eq = eq && std::abs (a.eq.v[(size_t) i].load() - b.eq.v[(size_t) i].load()) < 1.0e-4f;
        check (same == n, "state saved and loaded into a new instance: " + String (same) + " of " + String (n) + " values come back");
        check (chalk && eq && a.getShelfOrder() == b.getShelfOrder() && a.getCurrentProgram() == b.getCurrentProgram(),
               "and the drawn grain shape, the drawn eq, the order of the effects and the program");
        MemoryBlock mb2; b.getStateInformation (mb2);
        New c; c.setStateInformation (mb2.getData(), (int) mb2.getSize());
        auto nothing = [] (auto&, double, int) {};
        const auto d = compare (render (b, 48000.0, 6.0, main, nothing), render (c, 48000.0, 6.0, main, nothing));
        check (d.finite && d.peakD == 0.0 && d.rmsA > 1.0e-4, "a session reopened plays the same samples as before it was closed" + String::formatted (" (%.1f dBFS rms)", db (d.rmsA)));
        // the original's state does not load (its own tag), and garbage is ignored
        Old orig; MemoryBlock ob; orig.getStateInformation (ob);
        New e; std::vector<float> before; for (auto* prm : e.getParameters()) before.push_back (prm->getValue());
        setParam (orig, "psShift", 11.0f); orig.getStateInformation (ob);
        e.setStateInformation (ob.getData(), (int) ob.getSize());
        const char junk[] = "not a state at all";
        e.setStateInformation (junk, (int) sizeof (junk));
        e.setStateInformation (nullptr, 0);
        std::vector<float> after; for (auto* prm : e.getParameters()) after.push_back (prm->getValue());
        check (before == after, "another plug-in's state (the original's, or junk) is left alone");
    }
    {
        // stereo stays stereo: a sound on the left only must not come out the same on both sides
        New p;
        p.setCurrentProgram (0);
        for (auto* id : { "sgMix", "psMix", "grMix", "rvMix" }) setParam (p, id, 0.0f);
        const Layout st { 2, 2, false };
        applyLayout (p, st);
        p.prepareToPlay (48000.0, 512);
        AudioBuffer<float> buf (2, 512); MidiBuffer midi;
        double eL = 0, eR = 0, eLonR = 0;
        double ph = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i) { ph += 440.0 / 48000.0; buf.setSample (0, i, 0.5f * (float) std::sin (MathConstants<double>::twoPi * ph)); }
            p.processBlock (buf, midi);
            if (blk > 20) { eL += buf.getRMSLevel (0, 0, 512); eR += buf.getRMSLevel (1, 0, 512); }
            ignoreUnused (eLonR);
        }
        if (! kPaleis)
            check (eL > 1.0 && eR < eL * 0.01, String::formatted ("stereo stays stereo: a tone on the left only comes out on the left (left %.1f dB, right %.1f dB)", db (eL / 180.0), db (eR / 180.0)));
        else
            check (eL > 0.1, String::formatted ("on a stereo track the first two channels of the scene come out (W %.1f dB, Y %.1f dB)", db (eL / 180.0), db (eR / 180.0)));
    }
    if (! kPaleis)
    {
        // with every mix at its start the two sides of a wide input stay apart through the whole chain
        New p;
        const auto out = render (p, 48000.0, 6.0, Layout { 2, 2, false }, [] (auto&, double, int) {});
        double sl = 0, sr = 0, sd = 0;
        for (int i = 48000; i < out.getNumSamples(); ++i)
        {
            const double l = out.getSample (0, i), r = out.getSample (1, i);
            sl += l * l; sr += r * r; sd += (l - r) * (l - r);
        }
        check (sd > 0.05 * (sl + sr), String::formatted ("the default patch keeps its width: the side signal is %.1f dB below the whole", 10.0 * std::log10 ((sl + sr) / std::max (sd, 1.0e-12))));
    }
   #if BALLON_PRODUCT == 1
    {
        // the sphere: at 3rd order all 16 channels carry sound; on 64 channels at 3rd order the rest stay silent;
        // at 7th order all 64 do; a source ahead is louder in W and X than in Y and Z
        auto energy = [] (const AudioBuffer<float>& b, int c) { return (double) b.getRMSLevel (c, b.getNumSamples() / 4, b.getNumSamples() * 3 / 4); };
        {
            New p;
            const auto out = render (p, 48000.0, 6.0, Layout { 2, 16, true }, [] (auto&, double, int) {});
            int live = 0; for (int c = 0; c < 16; ++c) live += energy (out, c) > 1.0e-5 ? 1 : 0;
            check (live == 16, "3rd order: all 16 channels carry sound (" + String (live) + ")");
            check (p.orderNow.load() == 3 && ! p.ambiFallback.load(), "and the plug-in says 3rd order, nothing folded down");
        }
        {
            New p;
            const auto out = render (p, 48000.0, 6.0, Layout { 2, 64, true }, [] (auto&, double, int) {});
            int live = 0, silent = 0;
            for (int c = 0; c < 16; ++c) live += energy (out, c) > 1.0e-5 ? 1 : 0;
            for (int c = 16; c < 64; ++c) silent += energy (out, c) == 0.0 ? 1 : 0;
            check (live == 16 && silent == 48, "on a 64-channel track at 3rd order: channels 1 to 16 carry sound, 17 to 64 are silent (" + String (live) + ", " + String (silent) + ")");
        }
        {
            New p;
            const auto out = render (p, 48000.0, 6.0, Layout { 2, 64, true }, [] (auto& q, double, int blk) { if (blk == 0) { setParam (q, "ambiOrder", 6.0f); readyHigherOrder (q); } });
            int live = 0; for (int c = 0; c < 64; ++c) live += energy (out, c) > 1.0e-6 ? 1 : 0;
            check (live == 64, "7th order: all 64 channels carry sound (" + String (live) + ")");
        }
        {
            New p;
            const auto out = render (p, 48000.0, 5.0, Layout { 2, 4, true }, [] (auto& q, double, int blk)
            {
                if (blk != 0) return;
                q.setCurrentProgram (0);
                for (auto* id : { "sgMix", "psMix", "grMix", "rvMix" }) setParam (q, id, 0.0f);
                setParam (q, "grains", 1.0f); setParam (q, "flight", 0.0f); setParam (q, "ambiOrder", 0.0f); setParam (q, "balloonSfx", 0.0f);
                q.srcX = 0.9f; q.srcY = 0.0f; q.srcZ = 0.0f;
            });
            // ACN: W, Y, Z, X.  ahead is +x
            const double w = energy (out, 0), y = energy (out, 1), z = energy (out, 2), x = energy (out, 3);
            check (w > 1.0e-4 && x > 4.0 * y && x > 4.0 * z, String::formatted ("the dry sound placed ahead comes from ahead (ACN / SN3D: W %.1f, Y %.1f, Z %.1f, X %.1f dB)", db (w), db (y), db (z), db (x)));
        }
        {
            New p;
            const auto out = render (p, 48000.0, 3.0, Layout { 2, 2, false }, [] (auto& q, double, int blk) { if (blk == 0) setParam (q, "ambiOrder", 4.0f); });
            check (p.outChannels.load() == 2 && p.ambiFallback.load(), "on a stereo track it says the scene does not fit (the first two channels come out)");
        }
    }
   #endif
    {
        // never clips, never blows up: everything turned up, feedback full, every store effect bought
        New p;
        const Layout lay = kPaleis ? Layout { 2, 16, true } : Layout { 2, 2, false };
        const auto out = render (p, 48000.0, 12.0, lay, [] (auto& q, double, int blk)
        {
            if (blk != 0) return;
            for (auto* prm : q.getParameters())
                if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
                {
                    const String id = r->getParameterID();
                    if (id.endsWith ("Owned")) r->setValueNotifyingHost (1.0f);
                    else if (dynamic_cast<AudioParameterBool*> (r) == nullptr && dynamic_cast<AudioParameterChoice*> (r) == nullptr
                             && id != "snowIn" && id != "snowOut" && id != "heatIn" && id != "heatOut" && id != "grains") r->setValueNotifyingHost (1.0f);
                }
        });
        bool finite = true; float peak = 0;
        for (int c = 0; c < out.getNumChannels(); ++c)
            for (int i = 0; i < out.getNumSamples(); ++i) { const float v = out.getSample (c, i); finite = finite && std::isfinite (v); peak = jmax (peak, std::abs (v)); }
        check (finite && peak < (kPaleis ? 4.0f : 1.0f), String::formatted ("everything turned all the way up stays finite and under the limiter (peak %.2f dBFS)", db (peak)));
    }
}

#if ! BALLON_TEST_NO_EDITOR
int editorChecks (const juce::File& shots);   // (BallonTestUi.h)
 #include "BallonTestUi.h"
#endif

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    // (the original, compiled in here, would read and write its "last patch" file in the user's settings:
    //  the tests leave that alone)
    Old::rememberLastState = false;
    StringArray args (argv + 1, argc - 1);
    if (args.contains ("--params")) { printParams(); return 0; }
    if (args.contains ("--render"))
    {
        const File f (File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--render") + 1]));
        New p;
        const Layout lay = kPaleis ? Layout { 2, 16, true } : Layout { 2, 2, false };
        const auto out = render (p, 48000.0, 10.0, lay, [] (auto&, double, int) {});
        f.deleteFile();
        WavAudioFormat wav;
        if (auto os = f.createOutputStream())
            if (auto* w = wav.createWriterFor (os.release(), 48000.0, (unsigned) out.getNumChannels(), 24, {}, 0))
            {
                w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
                delete w;
                std::printf ("wrote %s (%d channels)\n", f.getFullPathName().toRawUTF8(), out.getNumChannels());
            }
        return 0;
    }
    File shots;
    if (args.contains ("--screenshots"))
    {
        shots = File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--screenshots") + 1]);
        shots.createDirectory();
    }
    std::printf ("%s %s: the faltung twin of %s\n", BALLON_NAME, JucePlugin_VersionString, Old().getName().toRawUTF8());
    if (shots == File() || args.contains ("--all"))
    {
        parameterChecks();
        layoutChecks();
        nullChecks();
        selfChecks();
    }
   #if ! BALLON_TEST_NO_EDITOR
    editorChecks (shots);
   #endif
    std::printf ("\n%d checks, %d failed\n", checks, fails);
    return fails;
}

// sndstudioTest — renders sndstudio projects offline and checks the engine feature by feature.
//   sndstudioTest [--screenshots dir] [--wav dir] [--vst3 "path/faltung dl4.vst3"]
#include "daw/StudioProcessor.h"
#include <cstdio>
#include "daw/StudioEditor.h"
#include "daw/StudioPages.h"
#include "daw/devices/StudioDevices.h"
#include <juce_dsp/juce_dsp.h>
#include <iostream>

using namespace fal::daw;
using fal::kDeep; using fal::Rng; using fal::kTwoPi;   // (not all of fal: its kit Pattern would collide with the studio's)

static int failures = 0;
static void check (bool ok, const juce::String& what) { if (! ok) { ++failures; std::cout << "  FAIL " << what << "\n"; } }
static juce::String db (float x) { return juce::String (juce::Decibels::gainToDecibels (x, -150.0f), 1) + " db"; }

static const double kSr = 48000.0; static const int kBs = 512;
static double spb (double bpm) { return 60.0 / bpm * kSr; }

/** renders `bars` with the internal clock (standalone mode: raw channels), returns peak per output channel and the first 16 */
struct Render
{
    std::array<float, kOutChannels> peak {};
    juce::AudioBuffer<float> out;   // first 16 output channels
};
static Render render (StudioProcessor& p, double bars, std::function<void (int block, double ppq)> each = {}, int ins = 0,
                      std::function<void (juce::AudioBuffer<float>&, int pos)> fillIn = {}, std::function<void (int block, juce::AudioBuffer<float>&)> after = {})
{
    p.standaloneInputs = ins;
    p.setRateAndBufferSizeDetails (kSr, kBs);
    p.prepareToPlay (kSr, kBs);
    auto& s = p.session;
    const double bpm = s.tempo();
    const int total = (int) std::ceil (bars * 4.0 * 60.0 / bpm * kSr);
    Render r; r.out.setSize (16, total); r.out.clear();
    juce::AudioBuffer<float> blk (ins + kOutChannels, kBs);
    s.locate (0.0); s.play();
    for (int pos = 0, b = 0; pos < total; pos += kBs, ++b)
    {
        blk.clear();
        if (fillIn) fillIn (blk, pos);
        juce::MidiBuffer m;
        if (each) each (b, s.internalPpq.load());
        p.processBlock (blk, m);
        if (after) after (b, blk);
        const int n = std::min (kBs, total - pos);
        for (int c = 0; c < kOutChannels; ++c)
        {
            r.peak[(size_t) c] = std::max (r.peak[(size_t) c], blk.getMagnitude (ins + c, 0, n));
            if (c < 16) r.out.copyFrom (c, pos, blk, ins + c, 0, n);
        }
    }
    s.stop();
    return r;
}
static std::vector<int> onsets (const juce::AudioBuffer<float>& b, int ch, float thresh = 0.05f, int hold = 2000)
{
    std::vector<int> v; int last = -hold;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > thresh && i - last > hold) { v.push_back (i); last = i; }
    return v;
}
static void shortHit (Track& t)   // a 35 ms kick with no sweep, so onsets are unambiguous
{
    auto* d = t.instrument.get();
    d->setBase (d->paramIndexForId ("decay"), 0.0f); d->setBase (d->paramIndexForId ("sweep"), 0.0f);
}
static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr = kSr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) b.getNumChannels(), 32, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
static juce::AudioBuffer<float> readWav (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    juce::AudioBuffer<float> b;
    if (! rd) return b;
    b.setSize ((int) rd->numChannels, (int) rd->lengthInSamples);
    rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true);
    return b;
}

// a test encoder (2 → 16) and decoder (16 → 2), standing in for ambisonic plug-ins
struct WideProc : juce::AudioProcessor
{
    int nIn, nOut;
    WideProc (int in, int out) : juce::AudioProcessor (BusesProperties().withInput ("in", juce::AudioChannelSet::discreteChannels (in), true).withOutput ("out", juce::AudioChannelSet::discreteChannels (out), true)), nIn (in), nOut (out)
    {
        addParameter (new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "gain", 0.0f, 1.0f, 1.0f));
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet().size() == nIn && l.getMainOutputChannelSet().size() == nOut; }
    const juce::String getName() const override { return nIn < nOut ? "test encoder" : "test decoder"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const int n = b.getNumSamples();
        const float g = getParameters()[0]->getValue();
        if (nIn < nOut) { for (int c = 2; c < nOut; ++c) b.copyFrom (c, 0, b, c % 2, 0, n); b.applyGain (g); }
        else { for (int c = 2; c < nIn; ++c) b.addFrom (c % 2, 0, b, c, 0, n); b.applyGain (0, 0, n, g / 8.0f); b.applyGain (1, 0, n, g / 8.0f); for (int c = 2; c < b.getNumChannels(); ++c) b.clear (c, 0, n); }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

int main (int argc, char** argv)
{
    // the child side of a plug-in scan (Session::scanPlugins starts this program again with these arguments)
    if (argc >= 5 && juce::String (argv[1]) == "--scan-plugin") return fal::daw::Session::scanOneFile (argv[2], argv[3], juce::File (juce::String (argv[4])));
    std::cout << std::unitbuf; std::setvbuf (stdout, nullptr, _IONBF, 0);   // (a crash in CI shows the line it happened after)
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File shotDir, wavDir, vst3;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--screenshots" && i + 1 < argc) shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--wav" && i + 1 < argc) wavDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--vst3" && i + 1 < argc) vst3 = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--probe" && i + 3 < argc)
        {
            // what a plug-in gets on a track: sndstudioTest --probe "x.vst3" <incoming channels> <track width>
            Session s; const juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]));
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* fmt : s.formats.getFormats()) if (fmt->fileMightContainThisPluginType (f.getFullPathName())) fmt->findAllTypesForFile (found, f.getFullPathName());
            if (found.isEmpty()) { std::cout << "no plug-in in " << f.getFullPathName() << "\n"; return 1; }
            juce::String err; auto d = s.createHosted (*found[0], &err);
            if (! d) { std::cout << "cannot load: " << err << "\n"; return 1; }
            const int in = juce::String (argv[i + 2]).getIntValue(), w = juce::String (argv[i + 3]).getIntValue();
            std::cout << d->name << ": arrives with " << d->defIn << " in, " << d->defOut << " out";
            d->configure (in, w);
            std::cout << "; fed " << in << " on a " << w << "-channel track it reads " << d->inWidth << " and writes " << d->outWidth << " (its buses: " << d->processor()->getTotalNumInputChannels() << " in, " << d->processor()->getTotalNumOutputChannels() << " out)\n";
            return 0;
        }
        if (a == "--dump-params" && i + 1 < argc)
        {
            // every device the studio has, with every parameter: for the manual
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
            Session s; juce::Array<juce::var> devs;
            auto add = [&] (const juce::String& type, const juce::String& family)
            {
                auto d = s.createDevice (type); if (! d) return;
                auto* o = new juce::DynamicObject(); o->setProperty ("type", type); o->setProperty ("name", d->name); o->setProperty ("family", family); o->setProperty ("instrument", d->isInstrument());
                o->setProperty ("blurb", type.startsWith ("st:") ? studioDeviceBlurb (type) : juce::String());
                juce::Array<juce::var> presets; for (int k = 0; k < d->numPrograms(); ++k) presets.add (d->programName (k)); o->setProperty ("presets", presets);
                juce::Array<juce::var> ps;
                for (int k = 0; k < d->numParams(); ++k)
                {
                    auto* q = new juce::DynamicObject(); q->setProperty ("id", d->paramId (k)); q->setProperty ("name", d->paramName (k));
                    q->setProperty ("min", d->paramText (k, 0.0f)); q->setProperty ("max", d->paramText (k, 1.0f)); q->setProperty ("default", d->paramText (k, d->defaultValue (k)));
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (d->processor()->getParameters()[k])) { juce::Array<juce::var> ch; for (auto& x : c->choices) ch.add (x); q->setProperty ("choices", ch); }
                    if (dynamic_cast<juce::AudioParameterBool*> (d->processor()->getParameters()[k])) q->setProperty ("switch", true);
                    ps.add (juce::var (q));
                }
                o->setProperty ("params", ps);
                devs.add (juce::var (o));
            };
            for (auto& t : Session::internalInstrumentTypes()) add (t, "instrument");
            for (auto& t : Session::internalEffectTypes()) add (t, "effect");
            for (auto& t : studioDeviceTypes()) add (t, "studio");
            auto* root = new juce::DynamicObject(); root->setProperty ("suite", kDeep ? "faltung deep" : "faltung"); root->setProperty ("devices", devs);
            juce::Array<juce::var> tn; for (auto& n : Session::templateNames()) tn.add (n); root->setProperty ("templates", tn);
            juce::Array<juce::var> dn; for (auto& n : divisionNames()) dn.add (n); root->setProperty ("divisions", dn);
            out.replaceWithText (juce::JSON::toString (juce::var (root)));
            std::cout << devs.size() << " devices written to " << out.getFullPathName() << "\n";
            return 0;
        }
    }
    if (shotDir != juce::File()) shotDir.createDirectory();
    if (wavDir != juce::File()) wavDir.createDirectory();
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sndstudio test"); tmp.createDirectory();

    // ---- 1. the template plays, through the sends, to the 128 outputs ---------------------------------
    std::cout << "== template: " << Session::templateNames()[1] << "\n";
    {
        StudioProcessor p;
        auto& s = p.session;
        s.newFromTemplate (1);
        check (s.tracks.size() == 8, "template has 8 tracks");
        check (s.returns.size() == 2 && s.returns[0]->inserts.size() == 1 && s.returns[1]->inserts.size() == 1, "template returns have dub and room");
        check (s.master->inserts.size() >= 2, "template master chain");
        s.mode.store ((int) PlayMode::Session);
        s.launchScene (4);   // "full"
        auto r = render (p, 2.0);
        std::cout << "  main out peak " << db (r.peak[0]) << " / " << db (r.peak[1]) << "\n";
        check (r.peak[0] > 0.05f && r.peak[1] > 0.05f, "template main output has sound");
        check (r.peak[2] < 1.0e-6f && r.peak[4] < 1.0e-6f, "other channels silent in produce mode");
        for (int c = 0; c < kOutChannels; ++c) check (std::isfinite (r.peak[(size_t) c]), "finite");
        if (wavDir != juce::File()) { juce::AudioBuffer<float> st (2, r.out.getNumSamples()); st.copyFrom (0, 0, r.out, 0, 0, r.out.getNumSamples()); st.copyFrom (1, 0, r.out, 1, 0, r.out.getNumSamples()); writeWav (wavDir.getChildFile ("sndstudio template.wav"), st); }

        // multi-out: kick → channels 5-6, return 1 (dub) → 7-8, everything still on the master too
        s.multiOut.store (true);
        s.tracks[0]->outChannel.store (5); s.returns[0]->outChannel.store (7);
        auto r2 = render (p, 2.0);
        std::cout << "  kick on 5-6: " << db (r2.peak[4]) << "   dub return on 7-8: " << db (r2.peak[6]) << "   main: " << db (r2.peak[0]) << "\n";
        check (r2.peak[4] > 0.05f && r2.peak[5] > 0.05f, "track routed to its own channel pair");
        check (r2.peak[6] > 0.001f && r2.peak[7] > 0.001f, "return routed to its own channel pair");
        check (r2.peak[2] < 1.0e-6f && r2.peak[8] < 1.0e-6f, "unused channels stay silent");
        s.tracks[0]->toMaster.store (false);
        auto r2b = render (p, 2.0);
        check (r2b.peak[0] < r.peak[0] * 0.95f, "to master off: the main lost the kick");
        s.tracks[0]->outChannel.store (127);
        auto r3 = render (p, 1.0);
        check (r3.peak[126] > 0.05f && r3.peak[127] > 0.05f, "channels 127-128 reachable");
        s.tracks[0]->toMaster.store (true); s.tracks[0]->outChannel.store (0); s.returns[0]->outChannel.store (0); s.multiOut.store (false);

        // mute and solo
        s.tracks[0]->mixer->setBase (MP_MUTE, 1.0f);
        auto r4 = render (p, 1.0);
        check (r4.peak[0] < r.peak[0] * 0.9f, "mute lowers the output");
        s.tracks[0]->mixer->setBase (MP_MUTE, 0.0f);
        s.tracks[2]->solo.store (true);
        float kickMeter = 0, hatMeter = 0;
        render (p, 1.0, [&] (int, double) { kickMeter = std::max (kickMeter, s.tracks[0]->meter[0].load()); hatMeter = std::max (hatMeter, s.tracks[2]->meter[0].load()); });
        std::cout << "  solo on the hat: kick strip " << db (kickMeter) << ", hat strip " << db (hatMeter) << "\n";
        check (kickMeter < 1.0e-6f && hatMeter > 0.01f, "solo silences the kick");
        s.tracks[2]->solo.store (false);

        bool ms0 = false; for (auto& d : s.master->inserts) if (d->typeId == "fal:ms0") { ms0 = true; check (d->bypass.load(), "ms0 bypassed for ableton"); }
        check (ms0 || kDeep, "ms0 on the master");
        // the strip's level reaches the output
        s.tracks[0]->mixer->setBase (MP_LEVEL, 0.0f);
        { auto r6 = render (p, 1.0); check (r6.peak[0] < r.peak[0] * 0.95f, "level down: the kick is gone"); }
        s.tracks[0]->mixer->setBase (MP_LEVEL, 0.8f);
        check (p.getTotalNumInputChannels() == 0, "no inputs");
        // the other templates load and play
        for (int k = 2; k < Session::templateNames().size(); ++k)
        {
            s.newFromTemplate (k);
            auto rk = render (p, 1.0);
            float pk = 0; for (float x : rk.peak) pk = std::max (pk, x);
            std::cout << "  template \"" << Session::templateNames()[k] << "\": " << s.tracks.size() << " tracks, peak " << db (pk) << "\n";
            check (pk > 0.02f, "template plays: " + Session::templateNames()[k]);
        }
    }

    // ---- 2. sequencer timing: steps, micro, swing, speed, length, ratchets, free clock -----------------
    std::cout << "== sequencer timing\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t);
        s.setTempo (120.0f); s.global->setBase (GP_SWING, 0.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].vel = 1; pt.s[8].on = true; pt.s[8].vel = 1; });
        s.launch (t, 0);
        auto r = render (p, 1.0);
        auto on = onsets (r.out, 0);
        std::cout << "  onsets at " << (on.size() > 0 ? on[0] : -1) << ", " << (on.size() > 1 ? on[1] : -1) << " samples (expected 0, " << (int) (8 * 0.25 * spb (120)) << ")\n";
        check (on.size() == 2, "two hits in a bar");
        if (on.size() == 2) { check (std::abs (on[0] - 0) <= 4, "first hit on the one"); check (std::abs (on[1] - (int) (8 * 0.25 * spb (120))) <= 4, "second hit on the three"); }
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].micro = 1.0f; });
        auto rm = render (p, 1.0); auto onm = onsets (rm.out, 0);
        if (onm.size() == 2) { const int exp = (int) (8 * 0.25 * spb (120) + 0.012 * kSr); std::cout << "  micro +12 ms: " << onm[1] << " (expected " << exp << ")\n"; check (std::abs (onm[1] - exp) <= 2, "microtiming"); }
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].micro = 0; });
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].on = false; pt.s[1].on = true; pt.s[1].vel = 1; });
        s.global->setBase (GP_SWING, 0.4f);   // 60%
        auto rs = render (p, 1.0); auto ons = onsets (rs.out, 0);
        if (ons.size() == 2) { const int exp = (int) ((0.25 + 0.1 * 2 * 0.25) * spb (120)); std::cout << "  swing 60%: " << ons[1] << " (expected " << exp << ")\n"; check (std::abs (ons[1] - exp) <= 2, "swing"); }
        s.global->setBase (GP_SWING, 0.0f);
        t.speed.store (5);
        auto rx = render (p, 1.0); auto onx = onsets (rx.out, 0);
        if (onx.size() >= 2) { const int exp = (int) (0.125 * spb (120)); std::cout << "  speed x2: " << onx[1] << " (expected " << exp << ")\n"; check (std::abs (onx[1] - exp) <= 2, "speed multiplier"); }
        t.speed.store (3);
        // free clock: 100 ms per step, two hits 800 ms apart regardless of tempo
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].vel = 1; pt.s[8].on = true; pt.s[8].vel = 1; });
        t.freeClock.store (true); t.stepMs.store (100.0f); s.setTempo (90.0f);
        auto rf = render (p, 1.0); auto onf = onsets (rf.out, 0);
        std::cout << "  free clock 100 ms: second hit at " << (onf.size() > 1 ? onf[1] : -1) << " (expected " << (int) (0.8 * kSr) << ")\n";
        check (onf.size() >= 2 && std::abs (onf[1] - (int) (0.8 * kSr)) <= 2, "free clock");
        t.freeClock.store (false); s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 12; pt.s[0].on = true; pt.s[0].vel = 1; });
        auto rl = render (p, 2.0); auto onl = onsets (rl.out, 0);
        std::cout << "  length 12 over 2 bars: " << onl.size() << " hits (expected 3):"; for (int o : onl) std::cout << " " << o; std::cout << "\n";
        check (onl.size() == 3, "polymeter length");
        if (onl.size() == 3) check (std::abs (onl[1] - (int) (12 * 0.25 * spb (120))) <= 2, "wrap at step 12");
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].vel = 1; pt.s[0].ratchet = 4; });
        auto rr = render (p, 1.0); auto onr = onsets (rr.out, 0, 0.2f, 1200);
        std::cout << "  ratchet 4: " << onr.size() << " hits\n";
        check (onr.size() == 4, "ratchet 4");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].ratchet = 1; pt.s[0].prob = 0.0f; });
        auto rp = render (p, 2.0);
        check (rp.peak[0] < 1.0e-6f, "probability 0 is silent");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].prob = 1.0f; pt.s[0].cond = Cond::X1of2; });
        auto rc = render (p, 4.0); auto onc = onsets (rc.out, 0);
        std::cout << "  1:2 over 4 bars: " << onc.size() << " hits\n"; check (onc.size() == 2, "condition 1:2");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::Fill; });
        auto rfl = render (p, 2.0); check (rfl.peak[0] < 1.0e-6f, "fill steps silent without fill");
        s.fill.store (true); auto rf2 = render (p, 2.0); s.fill.store (false); check (onsets (rf2.out, 0).size() == 2, "fill steps play with fill");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::First; });
        auto r1 = render (p, 4.0); check (onsets (r1.out, 0).size() == 1, "condition first");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::Always; });
        auto fundamental = [] (const juce::AudioBuffer<float>& b, int from)
        {
            const int N = 32768; std::vector<float> x ((size_t) N * 2, 0.0f);
            for (int k = 0; k < N && from + k < b.getNumSamples(); ++k) x[(size_t) k] = b.getSample (0, from + k);
            juce::dsp::FFT fft (15); fft.performFrequencyOnlyForwardTransform (x.data(), true);
            int best = 1; for (int k = 2; k < N / 2; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k;
            return best * kSr / N;
        };
        { auto* d = t.instrument.get(); d->setBase (d->paramIndexForId ("decay"), 0.6f); d->setBase (d->paramIndexForId ("click"), 0.0f); d->setBase (d->paramIndexForId ("tok"), 0.0f); d->setBase (d->paramIndexForId ("sub"), 0.0f); d->setBase (d->paramIndexForId ("model"), 1.0f); }
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].vel = 1; });
        auto rp0 = render (p, 1.0); const double f0 = fundamental (rp0.out, 2000);
        t.instrument->setBase (t.instrument->paramIndexForId ("keytrack"), 1.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].pitch = 12; });
        auto rp1 = render (p, 1.0); const double f1 = fundamental (rp1.out, 2000);
        std::cout << "  pitch +12: fundamental " << f0 << " hz -> " << f1 << " hz\n";
        check (std::abs (f1 / std::max (1.0, f0) - 2.0) < 0.1, "step pitch");
    }

    // ---- 3. parameter locks and slides --------------------------------------------------------------
    std::cout << "== parameter locks\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; auto* inst = t.instrument.get(); shortHit (t);
        const int tune = inst->paramIndexForId ("tune");
        inst->setBase (tune, 0.3f);
        s.setTempo (120.0f);
        s.editPattern (t, 0, [&] (Pattern& pt) { pt = Pattern(); pt.length = 16; for (int k : { 0, 4, 8, 12 }) { pt.s[(size_t) k].on = true; } pt.s[4].locks[0] = { inst->uid, tune, 0.9f }; });
        s.launch (t, 0);
        std::vector<std::pair<double, float>> seen;
        render (p, 1.0, [&] (int, double ppq) { seen.push_back ({ ppq, inst->finalValue (tune) }); });
        float atStep2 = -1, atStep5 = -1, atStep9 = -1;
        for (auto& [q, v] : seen) { if (q >= 0.5 && q < 0.75) atStep2 = v; if (q >= 1.25 && q < 1.5) atStep5 = v; if (q >= 2.25 && q < 2.5) atStep9 = v; }
        std::cout << "  tune at step 2: " << atStep2 << "  step 5 (locked): " << atStep5 << "  step 9 (free): " << atStep9 << "\n";
        check (std::abs (atStep2 - 0.3f) < 0.01f, "base before the lock");
        check (std::abs (atStep5 - 0.9f) < 0.01f, "lock applies at its step");
        check (std::abs (atStep9 - 0.3f) < 0.01f, "the next unlocked step returns to the base");
        check (std::abs (inst->baseValue (tune) - 0.3f) < 1.0e-5f, "the base itself is untouched");
        s.editPattern (t, 0, [&] (Pattern& pt) { pt.s[4].locks[0] = { inst->uid, tune, 0.2f }; pt.s[4].slide = true; pt.s[12].locks[0] = { inst->uid, tune, 0.8f }; });
        seen.clear();
        render (p, 1.0, [&] (int, double ppq) { seen.push_back ({ ppq, inst->finalValue (tune) }); });
        float mid = -1; for (auto& [q, v] : seen) if (q >= 2.0 && q < 2.05) mid = v;
        std::cout << "  slide half way: " << mid << " (expected ~0.5)\n";
        check (std::abs (mid - 0.5f) < 0.06f, "slide between locks");
        s.editPattern (t, 0, [&] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].locks[0] = { 0, MP_PAN, 1.0f }; pt.s[8].on = true; });
        auto rpan = render (p, 1.0);
        const int half = (int) (2.0 * spb (120));
        const float lA = rpan.out.getRMSLevel (0, 0, half), rA = rpan.out.getRMSLevel (1, 0, half);
        const float lB = rpan.out.getRMSLevel (0, half, rpan.out.getNumSamples() - half), rB = rpan.out.getRMSLevel (1, half, rpan.out.getNumSamples() - half);
        std::cout << "  pan lock: first hit L " << db (lA) << " R " << db (rA) << "; second L " << db (lB) << " R " << db (rB) << "\n";
        check (rA > lA * 10.0f, "pan lock hard right");
        check (std::abs (lB - rB) < 0.1f * std::max (lB, rB), "next step back to centre");
    }

    // ---- 4. automation lanes, modulators, macros, midi learn ------------------------------------------
    std::cout << "== automation + modulators\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t);
        s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[4].on = true; pt.s[8].on = true; pt.s[12].on = true; });
        s.launch (t, 0);
        s.editLane (s.targetFor (t, *t.mixer, MP_LEVEL), [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 16.0, 1.0f, 0.0f } }; });
        float atBar2 = -1;
        render (p, 4.0, [&] (int, double ppq) { if (ppq >= 8.0 && ppq < 8.1) atBar2 = t.mixer->baseValue (MP_LEVEL); });
        std::cout << "  level lane 0→1 over 4 bars, at bar 3: " << atBar2 << " (expected 0.5)\n";
        check (std::abs (atBar2 - 0.5f) < 0.02f, "automation value at a bar");
        s.editLane (s.targetFor (t, *t.mixer, MP_LEVEL), [] (AutoLane& l) { l.pts[0].curve = 0.8f; });
        float curved = -1;
        render (p, 4.0, [&] (int, double ppq) { if (ppq >= 8.0 && ppq < 8.1) curved = t.mixer->baseValue (MP_LEVEL); });
        check (curved < 0.2f, "curve bends the segment");
        s.removeLane (s.targetFor (t, *t.mixer, MP_LEVEL));
        t.mixer->setBase (MP_LEVEL, 0.8f);
        s.autoWrite.store (true);
        {
            const Target tg = s.targetFor (t, *t.mixer, MP_PAN);
            render (p, 2.0, [&] (int b, double) { if (b == 10) s.beginGesture (tg); if (b >= 10 && b < 60) { t.mixer->setBase (MP_PAN, 0.1f + 0.01f * (b - 10)); s.recordPoint (tg, 0.1f + 0.01f * (b - 10)); } if (b == 60) s.endGesture (tg); });
            auto l = s.lane (tg);
            std::cout << "  recorded " << (l ? (int) l->pts.size() : 0) << " points\n";
            check (l && l->pts.size() >= 20, "automation recording");
            s.removeLane (tg);
        }
        s.autoWrite.store (false);
        // modulator: a one-bar ramp lfo on pan, up, depth 0.5 → the pan final climbs across the bar
        auto* inst = t.instrument.get();
        Modulator m; m.src = MOD_LFO; m.div = 14; m.shape = 3; m.depth = 0.5f; m.polarity = 1; m.device = 0; m.param = MP_PAN;
        const int mid = s.addModulator (t, m);
        float early = -1, late = -1, band = 0;
        render (p, 1.0, [&] (int, double ppq) { if (ppq >= 0.0 && ppq < 0.1) early = t.mixer->finalValue (MP_PAN); if (ppq >= 3.8 && ppq < 3.95) { late = t.mixer->finalValue (MP_PAN); band = t.mixer->modMax (MP_PAN); } });
        std::cout << "  lfo → pan: " << early << " → " << late << "\n";
        check (late > early + 0.3f, "lfo modulation depth");
        check (band > 0.3f, "the modulation band is reported for the ui");
        // sync / free equivalence: 1 bar at 120 = 0.5 hz; the free version lands at the same value
        s.editMods (t, [&] (std::vector<Modulator>& v) { for (auto& x : v) if (x.id == mid) { x.sync = false; x.hz = 0.5f; } });
        float lateFree = -1;
        render (p, 1.0, [&] (int, double ppq) { if (ppq >= 3.8 && ppq < 3.95) lateFree = t.mixer->finalValue (MP_PAN); });
        std::cout << "  same lfo free at 0.5 hz: " << lateFree << "\n";
        check (std::abs (lateFree - late) < 0.05f, "sync and free rates agree");
        s.editMods (t, [&] (std::vector<Modulator>& v) { v.clear(); });
        // random gate statistics: 1/16 periods at 50% → about half the periods are on
        { Modulator g; g.src = MOD_GATE; g.div = 4; g.prob = 0.5f; g.depth = 1.0f; g.polarity = 1; g.device = 0; g.param = MP_PAN; s.addModulator (t, g); }
        int onCount = 0, total = 0; double lastQ = -1;
        render (p, 16.0, [&] (int, double ppq) { const double q = std::floor (ppq / 0.25); if (q != lastQ && ppq > 0.01) { lastQ = q; ++total; if (t.mixer->finalValue (MP_PAN) > 0.9f) ++onCount; } });
        std::cout << "  random gate at 50%: " << onCount << " of " << total << " periods on\n";
        check (total > 200 && onCount > total * 0.35 && onCount < total * 0.65, "random gate probability");
        s.editMods (t, [&] (std::vector<Modulator>& v) { v.clear(); });
        // slewed random: with slew the value never jumps more than a little per block
        { Modulator g; g.src = MOD_RANDOM; g.div = 4; g.slew = 0.5f; g.depth = 1.0f; g.polarity = 1; g.device = 0; g.param = MP_PAN; s.addModulator (t, g); }
        float prev = -1, maxJump = 0;
        render (p, 4.0, [&] (int, double) { const float v = t.mixer->finalValue (MP_PAN); if (prev >= 0) maxJump = std::max (maxJump, std::abs (v - prev)); prev = v; });
        std::cout << "  slewed random: largest jump per block " << maxJump << "\n";
        check (maxJump < 0.08f, "slew smooths the random");
        s.editMods (t, [&] (std::vector<Modulator>& v) { v.clear(); });
        // modulation of modulation: a slow lfo on the rate of a fast free lfo changes its period
        {
            Modulator fast; fast.src = MOD_LFO; fast.sync = false; fast.hz = 2.0f; fast.shape = 4; fast.depth = 1.0f; fast.polarity = 1; fast.device = 0; fast.param = MP_PAN;
            Modulator slow; slow.src = MOD_LFO; slow.sync = false; slow.hz = 0.1f; slow.shape = 3; slow.depth = 0.5f; slow.polarity = 1; slow.device = -1; slow.param = 1;   // → modulator index 1 (fast)
            s.editMods (t, [&] (std::vector<Modulator>& v) { v.push_back (slow); v.push_back (fast); });
            int flips = 0; float last = -1;
            render (p, 4.0, [&] (int, double) { const float v = t.mixer->finalValue (MP_PAN); if (last >= 0 && ((v > 0.9f) != (last > 0.9f))) ++flips; last = v; });
            std::cout << "  rate-modulated square: " << flips << " flips in 8 s (plain 2 hz would give 32)\n";
            check (flips > 36, "modulating a modulator's rate speeds it up");
            s.editMods (t, [&] (std::vector<Modulator>& v) { v.clear(); });
        }
        const int tune = inst->paramIndexForId ("tune");
        inst->macro[(size_t) tune].index.store (0); inst->macro[(size_t) tune].lo.store (0.2f); inst->macro[(size_t) tune].hi.store (0.9f);
        s.setMacro (0, 0.5f);
        render (p, 0.25);
        check (std::abs (inst->finalValue (tune) - 0.55f) < 0.01f, "macro mapping");
        s.midiLearnStart (s.targetFor (t, *t.mixer, MP_LEVEL));
        {
            p.prepareToPlay (kSr, kBs);
            juce::AudioBuffer<float> blk (kOutChannels, kBs); juce::MidiBuffer mm; mm.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
            p.processBlock (blk, mm);
            check (s.midiLearnPoll(), "midi learn caught cc 74");
            mm.clear(); mm.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0); blk.clear();
            p.processBlock (blk, mm);
            check (std::abs (t.mixer->baseValue (MP_LEVEL) - 64.0f / 127.0f) < 0.01f, "midi map moves the strip");
        }
    }

    // ---- 5. scenes, song and arrangement -----------------------------------------------------------------
    std::cout << "== scenes / song / arrangement\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t);
        s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; });
        s.editPattern (t, 1, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; });
        s.editScenes ([] (std::vector<Scene>& sc) { for (auto& x : sc) x.pattern.assign (1, -2); sc[0].pattern[0] = 0; sc[1].pattern[0] = 1; sc[2].pattern[0] = -1; });
        s.editSong ([] (std::vector<SongEntry>& so) { so = { { 0, 1 }, { 1, 1 }, { 2, 1 } }; });
        s.mode.store ((int) PlayMode::Song);
        auto r = render (p, 3.0); auto on = onsets (r.out, 0);
        std::cout << "  song 1+2+silent bars: " << on.size() << " hits (expected 3)\n";
        check (on.size() == 3, "song mode plays scenes in order");
        if (on.size() == 3) check (std::abs (on[2] - (int) (6.0 * spb (120))) <= 2, "second scene's pattern starts on its bar");
        s.mode.store ((int) PlayMode::Arrange);
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 1, 2.0, 1.0 } }; });
        auto ra = render (p, 4.0); auto ona = onsets (ra.out, 0);
        std::cout << "  arrangement clip at bar 3: " << ona.size() << " hits (expected 2), first at " << (ona.empty() ? -1 : ona[0]) << "\n";
        check (ona.size() == 2 && std::abs (ona[0] - (int) (8.0 * spb (120))) <= 4, "arrangement clips");
        s.mode.store ((int) PlayMode::Session);
        s.launch (t, 0);
        auto rl = render (p, 4.0, [&] (int b, double) { if (b == 20) s.launch (t, 1); });
        auto onl = onsets (rl.out, 0);
        std::cout << "  launch of pattern 2 after bar 1: " << onl.size() << " hits (expected 7)\n";
        check (onl.size() == 7, "quantised launch");
        s.setChain (t, { 0, 1 }); t.chainOn.store (true); s.launch (t, 0);
        auto rc = render (p, 4.0); auto onc = onsets (rc.out, 0);
        std::cout << "  chain 0,1 over 4 bars: " << onc.size() << " hits (expected 6)\n";
        check (onc.size() == 6, "pattern chain");
        t.chainOn.store (false);
        s.launch (t, 1); s.loopOn.store (true); s.loopStart.store (0); s.loopEnd.store (1);
        auto rlp = render (p, 3.0); auto onlp = onsets (rlp.out, 0);
        std::cout << "  1-bar loop over 3 bars: " << onlp.size() << " hits (expected 6)\n";
        check (onlp.size() == 6, "loop region");
        s.loopOn.store (false);
        // dynamic tracks: forty of them
        for (int k = 0; k < 40; ++k) { auto* nt = s.addTrack ("t" + juce::String (k)); s.setInstrument (*nt, s.createDevice ("fal:cl8")); s.editPattern (*nt, 0, [k] (Pattern& pt) { pt = Pattern(); pt.s[(size_t) (k % 16)].on = true; }); s.launch (*nt, 0); }
        check (s.tracks.size() == 41, "41 tracks");
        auto r40 = render (p, 1.0);
        check (r40.peak[0] > 0.01f, "forty extra tracks play");
        while (s.tracks.size() > 1) s.removeTrack ((int) s.tracks.size() - 1);
        check (s.tracks.size() == 1 && (*s.scenes)[0].pattern.size() == 1, "tracks removed, scenes follow");
    }

    // ---- 6. save / load round trip ---------------------------------------------------------------------
    std::cout << "== project round trip\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        s.tracks[1]->instrument->setBase (3, 0.123f);
        s.editPattern (*s.tracks[2], 5, [&] (Pattern& pt) { pt.s[3].on = true; pt.s[3].pitch = -5; pt.s[3].cond = Cond::X3of4; pt.s[3].locks[1] = { s.tracks[2]->instrument->uid, 2, 0.77f }; pt.s[3].slide = true; pt.length = 23; });
        s.tracks[3]->outChannel.store (7); s.tracks[3]->sendPre[1].store (true); s.tracks[3]->toMaster.store (false);
        s.setWidth (*s.tracks[4], 6);
        check (s.addRoute (*s.tracks[4], { s.tracks[5]->uid, 4, 1, 0, 0.5f }), "route added");
        s.editMidiMaps ([&] (std::vector<MidiMap>& m) { m.push_back ({ 21, s.targetFor (*s.tracks[0], *s.tracks[0]->mixer, MP_LEVEL), 0.1f, 0.9f }); });
        auto x1 = s.save();
        const auto s1 = x1->toString();
        juce::MemoryBlock mb; p.getStateInformation (mb);
        StudioProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto x2 = q.session.save();
        const auto s2 = x2->toString();
        check (s1 == s2, "xml identical after reload");
        if (s1 != s2)
        {
            auto a = juce::StringArray::fromLines (s1), b = juce::StringArray::fromLines (s2);
            for (int i = 0; i < std::max (a.size(), b.size()); ++i) if (i >= a.size() || i >= b.size() || a[i] != b[i]) { std::cout << "    first difference at line " << i << ":\n    " << (i < a.size() ? a[i] : "") << "\n    " << (i < b.size() ? b[i] : "") << "\n"; break; }
        }
        check (q.session.tracks.size() == 8 && q.session.tracks[3]->outChannel.load() == 7 && ! q.session.tracks[3]->toMaster.load(), "routing survives");
        check (q.session.tracks[4]->width.load() == 6 && q.session.tracks[4]->routes->size() == 1 && (*q.session.tracks[4]->routes)[0].channels == 4, "width and routes survive");
        check (q.session.tracks[2]->patternAt (5) && q.session.tracks[2]->patternAt (5)->length == 23 && q.session.tracks[2]->patternAt (5)->s[3].cond == Cond::X3of4, "steps survive");
        check (std::abs (q.session.tracks[1]->instrument->baseValue (3) - 0.123f) < 1.0e-5f, "device base values survive");
        check (q.session.midiMaps->size() == 1 && (*q.session.midiMaps)[0].cc == 21, "midi maps survive");
        check (q.session.lanes->size() == s.lanes->size() && q.session.tracks[2]->mods->size() == 1, "lanes and modulators survive");
        q.session.launchScene (4);
        auto r = render (q, 1.0);
        check (r.peak[0] > 0.05f, "reloaded project plays");
        auto f = tmp.getChildFile ("sndstudio test.fstudio");
        juce::String err;
        check (p.saveProject (f, err) && q.loadProject (f, err), "project file save / load");
    }

    // ---- 7. latency alignment and routes ------------------------------------------------------------------
    std::cout << "== latency compensation + routes\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto* t2 = s.addTrack ("second"); s.setInstrument (*t2, s.createDevice ("fal:bd1"));
        s.setTempo (120.0f);
        for (auto* t : { s.tracks[0].get(), t2 }) { shortHit (*t); s.editPattern (*t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; }); s.launch (*t, 0); }
        s.multiOut.store (true);
        s.tracks[0]->outChannel.store (3); t2->outChannel.store (5);
        auto d = s.createDevice ("fal:sw6"); d->setBase (d->paramIndexForId ("amount"), 0.0f); s.addInsert (*t2, std::move (d));
        auto r = render (p, 2.0);
        std::cout << "  total latency " << s.totalLatency() << " samples\n";
        check (s.totalLatency() > 3000, "sw6 latency reported");
        auto a = onsets (r.out, 2), b = onsets (r.out, 4);
        std::cout << "  hits: plain track " << (a.empty() ? -1 : a[0]) << ", track with sw6 " << (b.empty() ? -1 : b[0]) << "\n";
        check (! a.empty() && ! b.empty() && std::abs (a[0] - b[0]) <= 3, "tracks aligned");
        if (! a.empty()) check (std::abs (a[0] - s.totalLatency()) <= 3, "the whole output is delayed by the reported latency");
        // a route from the plain track into the sw6 track: the routed copy must also line up (compensated along the route)
        auto* t3 = s.addTrack ("third"); t3->outChannel.store (7);
        check (s.addRoute (*s.tracks[0], { t3->uid, 2, 0, 0, 1.0f }), "route track 1 → third");
        auto dd = s.createDevice ("fal:sw6"); dd->setBase (dd->paramIndexForId ("amount"), 0.0f); s.addInsert (*t3, std::move (dd));
        auto r2 = render (p, 2.0);
        auto c = onsets (r2.out, 6), a2 = onsets (r2.out, 2);
        std::cout << "  routed copy through another sw6: " << (c.empty() ? -1 : c[0]) << " vs source " << (a2.empty() ? -1 : a2[0]) << ", total latency " << s.totalLatency() << "\n";
        check (! c.empty() && ! a2.empty() && std::abs (c[0] - a2[0]) <= 3, "route compensated");
        check (s.addRoute (*t3, { s.tracks[0]->uid, 2, 0, 0, 1.0f }) && (*t3->routes)[0].feedback, "a route that closes a loop becomes a feedback route");
        check (! s.addRoute (*t3, { t3->uid, 2, 0, 0, 1.0f }), "a track cannot route into itself");
    }

    // ---- 8. spatial: widths, routes, the master width ----------------------------------------------------
    std::cout << "== spatial workflow\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& enc = *s.tracks[0]; shortHit (enc);
        s.setTempo (120.0f);
        s.editPattern (enc, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; });
        s.launch (enc, 0);
        s.setWidth (enc, 16); enc.toMaster.store (false);
        auto* e = s.addInsert (enc, Device::makeFromProcessor (900, std::make_unique<WideProc> (2, 16), "test:enc", "test encoder"));
        check (e && e->inWidth == 2 && e->outWidth == 16, "encoder negotiated 2 → 16");
        auto* dec = s.addTrack ("decoder"); s.setWidth (*dec, 24);
        auto* d = s.addInsert (*dec, Device::makeFromProcessor (901, std::make_unique<WideProc> (16, 2), "test:dec", "test decoder"));
        check (d && d->inWidth == 16 && d->outWidth == 2, "decoder negotiated 16 → 2");
        check (s.addRoute (enc, { dec->uid, 16, 0, 0, 1.0f }), "encoder → decoder over 16 channels");
        s.setWidth (*s.master, 24);
        check (s.master->width.load() == 24 && s.master->buf.getNumChannels() == 24, "master is 24 wide");
        auto r = render (p, 1.0);
        std::cout << "  master channels 1-2 " << db (r.peak[0]) << ", 3 " << db (r.peak[2]) << ", 24 " << db (r.peak[23]) << "\n";
        check (r.peak[0] > 0.01f && r.peak[1] > 0.01f, "the decoded stereo reaches the master");
        check (r.peak[2] < 1.0e-6f && r.peak[23] < 1.0e-6f, "the rest of the 24 master channels is silent (the decoder outputs 2)");
        float encMeter16 = 0; render (p, 0.5, [&] (int, double) { encMeter16 = std::max (encMeter16, enc.meter[15].load()); });
        check (encMeter16 > 0.01f, "the encoder track meters 16 channels");
        // the encoder alone must not reach the master: remove the route
        s.removeRoute (enc, 0);
        auto r0 = render (p, 1.0);
        check (r0.peak[0] < 1.0e-6f, "encoder with to-master off and no route is silent on the master");
        s.addRoute (enc, { dec->uid, 16, 0, 0, 1.0f });
        // a binaural track: 16 in, a 2-out device, to the master; mute the 24-ch decoder, master back to 2
        auto* bin = s.addTrack ("binaural"); s.setWidth (*bin, 16);
        s.addInsert (*bin, Device::makeFromProcessor (902, std::make_unique<WideProc> (16, 2), "test:bin", "test binaural"));
        check (s.addRoute (enc, { bin->uid, 16, 0, 0, 1.0f }), "encoder → binaural too");
        dec->mixer->setBase (MP_MUTE, 1.0f);
        s.setWidth (*s.master, 2);
        auto rb = render (p, 1.0);
        check (rb.peak[0] > 0.01f && rb.peak[2] < 1.0e-6f, "binaural stereo on a 2-channel master, decoder muted");
        // stereo clip on a 16-channel track: the clip feeds channels 1-2, the encoder spreads it; bypassed, 1-2 only
        juce::AudioBuffer<float> sine (2, 48000); for (int i = 0; i < 48000; ++i) { const float v = 0.5f * std::sin (i * 0.05f); sine.setSample (0, i, v); sine.setSample (1, i, v); }
        auto wf = tmp.getChildFile ("sine.wav"); writeWav (wf, sine);
        auto* at = s.addTrack ("audio", -1, TrackKind::Audio); s.setWidth (*at, 16); at->toMaster.store (false);
        juce::String err; check (s.addAudioClip (*at, wf, 0.0, &err) != nullptr, "clip added: " + err);
        auto* ae = s.addInsert (*at, Device::makeFromProcessor (903, std::make_unique<WideProc> (2, 16), "test:enc", "test encoder"));
        s.multiOut.store (true); at->outChannel.store (1);
        auto rc = render (p, 0.5);
        check (rc.peak[0] > 0.1f && rc.peak[15] > 0.1f && rc.peak[16] < 1.0e-6f, "stereo clip → encoder → 16 channels");
        ae->bypass.store (true);
        auto rc2 = render (p, 0.5);
        check (rc2.peak[0] > 0.1f && rc2.peak[2] < 1.0e-6f, "encoder bypassed: the clip stays on 1-2");
        // a 16-channel hosted layout: the test processors stand in; a real plug-in is tried below when a vst3 is given
    }

    // ---- 9. audio clips: position, gain, fades, split, stretch ---------------------------------------------
    std::cout << "== audio clips\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        s.removeTrack (0);
        s.setTempo (120.0f);
        // a 1 s file: silence, then an impulse at 0.1 s, then a 440 hz tone from 0.2 s
        juce::AudioBuffer<float> file (2, 48000); file.clear();
        file.setSample (0, 4800, 0.9f); file.setSample (1, 4800, 0.9f);
        for (int i = 9600; i < 48000; ++i) { const float v = 0.3f * std::sin (kTwoPi * 440.0f * i / 48000.0f); file.setSample (0, i, v); file.setSample (1, i, v); }
        auto wf = tmp.getChildFile ("clip.wav"); writeWav (wf, file);
        auto* at = s.addTrack ("audio", -1, TrackKind::Audio);
        juce::String err;
        auto* c = s.addAudioClip (*at, wf, 2.0, &err);
        check (c != nullptr, "clip at bar 3: " + err);
        auto r = render (p, 4.0);
        auto on = onsets (r.out, 0, 0.5f, 100);
        const int expect = (int) (8.0 * spb (120)) + 4800;
        std::cout << "  impulse at " << (on.empty() ? -1 : on[0]) << " (expected " << expect << ")\n";
        check (! on.empty() && std::abs (on[0] - expect) <= 1, "clip plays at its bar, sample accurate");
        check (std::abs (r.out.getMagnitude (0, expect - 2, 5) - 0.9f) < 0.02f, "unity gain");
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v[0].gain = 0.5f; v[0].fadeIn = 0.05; });
        auto r2 = render (p, 4.0);
        check (std::abs (r2.out.getMagnitude (0, expect - 2, 5) - 0.45f) < 0.02f, "clip gain");
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v[0].gain = 1.0f; v[0].fadeIn = 0.2; });
        auto r3 = render (p, 4.0);
        std::cout << "  with a 200 ms fade in, the impulse at 100 ms is " << r3.out.getMagnitude (0, expect - 2, 5) << " (expected ~0.45)\n";
        check (std::abs (r3.out.getMagnitude (0, expect - 2, 5) - 0.45f) < 0.03f, "fade in");
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v[0].fadeIn = 0; v[0].pitch = 12.0f; });
        auto r4 = render (p, 4.0); auto on4 = onsets (r4.out, 0, 0.5f, 100);
        std::cout << "  repitched +12: impulse at " << (on4.empty() ? -1 : on4[0]) << " (expected " << (int) (8.0 * spb (120)) + 2400 << ")\n";
        check (! on4.empty() && std::abs (on4[0] - ((int) (8.0 * spb (120)) + 2400)) <= 2, "repitch doubles the speed");
        // stretch: twice as long, the impulse arrives at 200 ms, the tone stays at 440 hz
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v[0].pitch = 0; v[0].mode = 1; v[0].stretch = 2.0; v[0].bars = 4.0; });
        auto r5 = render (p, 6.0); auto on5 = onsets (r5.out, 0, 0.3f, 100);
        std::cout << "  stretched x2: first transient at " << (on5.empty() ? -1 : on5[0]) << " (expected ~" << (int) (8.0 * spb (120)) + 9600 << ")\n";
        check (! on5.empty() && std::abs (on5[0] - ((int) (8.0 * spb (120)) + 9600)) <= 1100, "stretch keeps transients in time");
        {
            const int from = (int) (8.0 * spb (120)) + 24000; const int N = 16384; std::vector<float> x ((size_t) N * 2, 0.0f);
            for (int k = 0; k < N; ++k) x[(size_t) k] = r5.out.getSample (0, from + k);
            juce::dsp::FFT fft (14); fft.performFrequencyOnlyForwardTransform (x.data(), true);
            int best = 1; for (int k = 2; k < N / 2; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k;
            const double hz = best * kSr / N;
            std::cout << "  stretched tone at " << hz << " hz (expected 440)\n";
            check (std::abs (hz - 440.0) < 8.0, "stretch keeps the pitch");
        }
        // split at bar 4: two clips, the second starting where the first ends
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v[0].mode = 0; v[0].stretch = 1.0; v[0].bars = 1.0; });
        s.splitClip (*at, (*at->aclips)[0].id, 2.5);
        check (at->aclips->size() == 2 && std::abs ((*at->aclips)[1].startBar - 2.5) < 1.0e-9 && std::abs ((*at->aclips)[1].offset - 1.0) < 1.0e-6, "split clip (second half starts 1 s into the file)");
        auto r6 = render (p, 4.0); auto on6 = onsets (r6.out, 0, 0.5f, 100);
        check (! on6.empty() && std::abs (on6[0] - expect) <= 1, "after the split the impulse is where it was");
        // save / load keeps the clips
        auto x = s.save(); StudioProcessor q; juce::String e2; q.session.load (*x, &e2);
        check (q.session.tracks.size() == 1 && q.session.tracks[0]->isAudio() && q.session.tracks[0]->aclips->size() == 2 && (*q.session.tracks[0]->aclips)[0].sample != nullptr, "audio clips reload: " + e2);
    }

    // ---- 10. recording, resampling ------------------------------------------------------------------------
    std::cout << "== recording + resampling\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        s.removeTrack (0);
        s.setTempo (120.0f);
        s.projectFile = tmp.getChildFile ("rec.fstudio"); s.projectName = "rec";
        auto* at = s.addTrack ("in", -1, TrackKind::Audio);
        at->armed.store (true); at->inputSource.store (-2); at->inputChannel.store (1); at->inputCount.store (2); at->monitor.store ((int) Monitor::Off);
        p.standaloneInputLatency = 100;
        s.startRecording();
        // the device input carries a known ramp; the engine records it onto the armed track
        auto r = render (p, 2.0, {}, 2, [] (juce::AudioBuffer<float>& b, int pos) { for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.25f * std::sin ((pos + i) * 0.01f); b.setSample (0, i, v); b.setSample (1, i, -v); } });
        std::cout << "  engine wrote " << at->recWritten << " samples, dropped " << at->recDropped << "\n";
        s.stopRecording();
        check (at->aclips->size() == 1, "a clip was recorded");
        if (at->aclips->size() == 1)
        {
            const auto& c = (*at->aclips)[0];
            auto rec = readWav (juce::File (c.path));
            std::cout << "  recorded " << rec.getNumSamples() << " samples (engine wrote " << at->recWritten << "), clip starts at bar " << c.startBar << ", offset " << c.offset << " s (latency 100 samples compensated)\n";
            check (rec.getNumSamples() >= (int) (2.0 * 4.0 * spb (120)) - kBs, "recorded length");
            bool exact = rec.getNumSamples() > 2000;
            for (int i = 0; i < 2000 && exact; ++i) exact = std::abs (rec.getSample (0, i) - 0.25f * std::sin (i * 0.01f)) < 1.0e-5f && std::abs (rec.getSample (1, i) + 0.25f * std::sin (i * 0.01f)) < 1.0e-5f;
            check (exact, "the recording holds the exact input samples");
            check (c.startBar == 0.0 && std::abs (c.offset - 100.0 / kSr) < 1.0e-9, "clip moved back by the input latency (as an offset into the file)");
            // playing it back lands the samples where they were (input latency aside)
            at->armed.store (false);
            auto rp = render (p, 1.0);
            const int k = 3000; const int shift = 100;
            check (std::abs (rp.out.getSample (0, k - shift) - 0.25f * std::sin (k * 0.01f)) < 0.01f, "playback of the recording is sample aligned");
        }
        // determinism of the engine: one kick, rendered twice
        {
            s.newFromTemplate (0); s.projectFile = tmp.getChildFile ("det.fstudio"); s.projectName = "det";
            p.prepareToPlay (kSr, kBs); juce::String err;
            auto& t = *s.tracks[0]; s.setTempo (120.0f);
            s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; }); s.launch (t, 0);
            auto d1 = tmp.getChildFile ("det1"), d2 = tmp.getChildFile ("det2");
            ExportOptions so; so.bits = 32; so.dither = false; so.tail = false;
            juce::StringArray fin1, fin2;
            s.renderStems (d1, 0.0, 1.0, nullptr, err, so); for (int i = 0; i < t.instrument->numParams(); ++i) fin1.add (juce::String (t.instrument->finalValue (i), 5));
            s.renderStems (d2, 0.0, 1.0, nullptr, err, so); for (int i = 0; i < t.instrument->numParams(); ++i) fin2.add (juce::String (t.instrument->finalValue (i), 5));
            auto x = readWav (d1.getChildFile ("01 kick.wav")), y = readWav (d2.getChildFile ("01 kick.wav"));
            float mdd = 0; int fdd = -1;
            for (int i = 0; i < std::min (x.getNumSamples(), y.getNumSamples()); ++i) { const float d = std::abs (x.getSample (0, i) - y.getSample (0, i)); if (d > 1.0e-6f && fdd < 0) fdd = i; mdd = std::max (mdd, d); }
            std::cout << "  one kick twice: max diff " << mdd << " first at " << fdd << "  [" << x.getSample (0, 0) << " " << x.getSample (0, 1) << " " << x.getSample (0, 2) << "] vs [" << y.getSample (0, 0) << " " << y.getSample (0, 1) << " " << y.getSample (0, 2) << "]\n";
            if (fin1 != fin2) std::cout << "    finals differ: " << fin1.joinIntoString (" ") << "  vs  " << fin2.joinIntoString (" ") << "\n";
            check (mdd < 1.0e-6f, "a single kick renders identically twice");
        }
        // resample project: the new track's clip equals the master export
        s.newFromTemplate (1); s.projectFile = tmp.getChildFile ("rs.fstudio"); s.projectName = "rs";
        p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        juce::String err;
        auto mf = tmp.getChildFile ("master.wav");
        ExportOptions o; o.bits = 32; o.dither = false; o.tail = false;
        check (s.renderMaster (mf, 0.0, 2.0, nullptr, err, o), "master export: " + err);
        {
            auto mf2 = tmp.getChildFile ("master2.wav");
            check (s.renderMaster (mf2, 0.0, 2.0, nullptr, err, o), "second master export");
            auto a = readWav (mf), b = readWav (mf2); float md = 0; int fd = -1;
            for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i) { const float d = std::abs (a.getSample (0, i) - b.getSample (0, i)); if (d > 1.0e-6f && fd < 0) fd = i; md = std::max (md, d); }
            std::cout << "  export twice: max difference " << md << " (first at " << fd << ")\n";
            if (md >= 1.0e-6f)
            {
                auto head = [] (const juce::AudioBuffer<float>& x) { juce::String r; for (int i = 0; i < 6; ++i) r << juce::String (x.getSample (0, i), 6) << " "; return r; };
                std::cout << "    first  " << head (a) << "\n    second " << head (b) << "\n";
                {
                    // is the second render the first one advanced by some lag?
                    int bestLag = 0; double bestErr = 1e30;
                    for (int lag = -12000; lag <= 12000; lag += 1)
                    {
                        double e = 0; int cnt = 0;
                        for (int i = 2000; i < 30000; i += 7) { const int j = i + lag; if (j < 0 || j >= a.getNumSamples()) continue; const double d = b.getSample (0, i) - a.getSample (0, j); e += d * d; ++cnt; }
                        if (cnt > 100) { e /= cnt; if (e < bestErr) { bestErr = e; bestLag = lag; } }
                    }
                    std::cout << "    second ~= first shifted by " << bestLag << " samples (mean sq err " << bestErr << "); total latency " << s.totalLatency() << "\n";
                    for (auto& d : s.master->inserts) std::cout << "    master insert " << d->name << " latency " << d->latency() << " bypass " << (int) d->bypass.load() << "\n";
                    for (auto& t : s.tracks) std::cout << "    track " << t->name << " latency " << t->latency << " align " << t->alignDelay << " arrival " << t->arrival << " inDelay " << t->inDelay << "\n";
                    for (auto& r : s.returns) std::cout << "    return " << r->name << " latency " << r->latency << " align " << r->alignDelay << "\n";
                }
                auto mf3 = tmp.getChildFile ("master3.wav"); s.renderMaster (mf3, 0.0, 2.0, nullptr, err, o); auto c3 = readWav (mf3);
                float m23 = 0; for (int i = 0; i < std::min (b.getNumSamples(), c3.getNumSamples()); ++i) m23 = std::max (m23, std::abs (b.getSample (0, i) - c3.getSample (0, i)));
                std::cout << "    second vs third: " << m23 << "\n";
                // without the master chain: is it the chain?
                s.newFromTemplate (1); p.prepareToPlay (kSr, kBs); s.launchScene (4);
                auto p1 = tmp.getChildFile ("pre1.wav"), p2 = tmp.getChildFile ("pre2.wav");
                s.renderMaster (p1, 0.0, 2.0, nullptr, err, o, true); s.renderMaster (p2, 0.0, 2.0, nullptr, err, o, true);
                auto x1 = readWav (p1), x2 = readWav (p2); float mp = 0; int fp = -1;
                for (int i = 0; i < std::min (x1.getNumSamples(), x2.getNumSamples()); ++i) { const float d = std::abs (x1.getSample (0, i) - x2.getSample (0, i)); if (d > 1.0e-6f && fp < 0) fp = i; mp = std::max (mp, d); }
                std::cout << "    pre-master twice (fresh template): max " << mp << " first at " << fp << "\n    first  " << head (x1) << "\n    second " << head (x2) << "\n";
                // and with the chain, fresh again, but no scene launch
                s.newFromTemplate (1); p.prepareToPlay (kSr, kBs);
                s.renderMaster (p1, 0.0, 2.0, nullptr, err, o); s.renderMaster (p2, 0.0, 2.0, nullptr, err, o);
                x1 = readWav (p1); x2 = readWav (p2); mp = 0; fp = -1;
                for (int i = 0; i < std::min (x1.getNumSamples(), x2.getNumSamples()); ++i) { const float d = std::abs (x1.getSample (0, i) - x2.getSample (0, i)); if (d > 1.0e-6f && fp < 0) fp = i; mp = std::max (mp, d); }
                std::cout << "    with chain, no scene launch: max " << mp << " first at " << fp << "\n";
                s.newFromTemplate (1); p.prepareToPlay (kSr, kBs); s.launchScene (4); s.renderMaster (mf, 0.0, 2.0, nullptr, err, o); s.renderMaster (mf, 0.0, 2.0, nullptr, err, o);
            }
            check (md < 1.0e-6f, "two exports in a row are identical");
            if (md >= 1.0e-6f)
            {
                // the kick's parameters after each render, and the kick alone with the template's returns and master
                auto kickFinals = [&] { juce::StringArray a; for (int i = 0; i < s.tracks[0]->instrument->numParams(); ++i) a.add (juce::String (s.tracks[0]->instrument->finalValue (i), 5)); return a.joinIntoString (" "); };
                const auto diag = tmp.getChildFile ("diag.wav");
                s.renderMaster (diag, 0.0, 1.0, nullptr, err, o); const auto k1 = kickFinals();
                s.renderMaster (diag, 0.0, 1.0, nullptr, err, o); const auto k2 = kickFinals();
                std::cout << "    kick finals " << (k1 == k2 ? "identical" : "DIFFER: " + k1 + " | " + k2) << "\n";
                // which stem differs?
                auto d1 = tmp.getChildFile ("stems1"), d2 = tmp.getChildFile ("stems2");
                ExportOptions so; so.bits = 32; so.dither = false; so.tail = false;
                s.renderStems (d1, 0.0, 1.0, nullptr, err, so); s.renderStems (d2, 0.0, 1.0, nullptr, err, so);
                for (auto& f : d1.findChildFiles (juce::File::findFiles, false, "*.wav"))
                {
                    auto x = readWav (f), y = readWav (d2.getChildFile (f.getFileName()));
                    float mdd = 0; int fdd = -1;
                    for (int i = 0; i < std::min (x.getNumSamples(), y.getNumSamples()); ++i) { const float d = std::abs (x.getSample (0, i) - y.getSample (0, i)); if (d > 1.0e-6f && fdd < 0) fdd = i; mdd = std::max (mdd, d); }
                    std::cout << "    " << f.getFileName() << ": max diff " << mdd << " first at " << fdd << "  [" << x.getSample (0, 0) << " " << x.getSample (0, 1) << "] vs [" << y.getSample (0, 0) << " " << y.getSample (0, 1) << "]\n";
                }
            }
        }
        auto* rt = s.renderProjectToTrack (0.0, 2.0, false, err);
        check (rt != nullptr && rt->aclips->size() == 1, "resample project made a track: " + err);
        if (rt && rt->aclips->size() == 1)
        {
            auto a = readWav (mf), b = readWav (juce::File ((*rt->aclips)[0].path));
            bool same = a.getNumSamples() == b.getNumSamples() && a.getNumSamples() > 1000;
            float maxDiff = 0;
            int firstDiff = -1;
            for (int i = 0; same && i < a.getNumSamples(); ++i) { const float d = std::abs (a.getSample (0, i) - b.getSample (0, i)); if (d > 1.0e-6f && firstDiff < 0) firstDiff = i; maxDiff = std::max (maxDiff, d); }
            std::cout << "  resample vs export: " << a.getNumSamples() << " / " << b.getNumSamples() << " samples, max difference " << maxDiff << " (first at " << firstDiff << ")\n";
            if (firstDiff >= 0) std::cout << "    export " << a.getSample (0, firstDiff) << " " << a.getSample (0, firstDiff + 1) << " " << a.getSample (0, firstDiff + 2) << "  resample " << b.getSample (0, firstDiff) << " " << b.getSample (0, firstDiff + 1) << " " << b.getSample (0, firstDiff + 2) << "\n";
            check (same && maxDiff < 1.0e-6f, "resample project equals the master export sample for sample");
        }
        // resample one track
        auto* bt = s.renderTrackToTrack (*s.tracks[0], 0.0, 1.0, err);
        check (bt != nullptr && bt->aclips->size() == 1, "track bounce: " + err);
        // master export with limiter + dither at 24 bit stays under the ceiling
        ExportOptions lim; lim.limit = true; lim.ceilingDb = -1.0f; lim.bits = 24; lim.tail = false;
        s.global->setBase (GP_MASTER, 1.0f);
        check (s.renderMaster (mf, 0.0, 1.0, nullptr, err, lim), "limited export");
        auto lm = readWav (mf);
        std::cout << "  limited master peak " << db (lm.getMagnitude (0, lm.getNumSamples())) << "\n";
        check (lm.getMagnitude (0, lm.getNumSamples()) <= juce::Decibels::decibelsToGain (-0.9f), "export limiter holds the ceiling");
    }

    // ---- 11. hosting a plug-in ----------------------------------------------------------------------------
    std::cout << "== hosting\n";
    {
        juce::File f = vst3;
        if (f == juce::File())
        {
            auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
            for (int up = 0; up < 5 && f == juce::File(); ++up)
            {
                auto cand = dir.getChildFile (kDeep ? "faltung_deep_dl4_artefacts/Release/VST3/faltung deep dl4.vst3" : "faltung_dl4_artefacts/Release/VST3/faltung dl4.vst3");
                if (cand.exists()) f = cand;
                dir = dir.getParentDirectory();
            }
        }
        if (f.exists())
        {
            StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* fmt : s.formats.getFormats()) if (fmt->fileMightContainThisPluginType (f.getFullPathName())) fmt->findAllTypesForFile (found, f.getFullPathName());
            check (! found.isEmpty(), "plug-in description found for " + f.getFileName());
            if (! found.isEmpty())
            {
                s.known.addType (*found[0]);
                juce::String err;
                auto d = s.createHosted (*found[0], &err);
                check (d != nullptr, "instance created: " + err);
                if (d)
                {
                    std::cout << "  hosted " << d->name << " with " << d->numParams() << " parameters, in " << d->inWidth << " out " << d->outWidth << "\n";
                    const int mix = d->paramIndexForName ("mix"), fb = d->paramIndexForName ("feedback");
                    check (mix >= 0 && fb >= 0, "hosted parameters found by name");
                    if (mix >= 0) d->setBase (mix, 1.0f); if (fb >= 0) d->setBase (fb, 0.6f);
                    auto* dev = s.addInsert (*s.returns[0], std::move (d));
                    auto& t = *s.tracks[0]; shortHit (t);
                    s.setTempo (120.0f);
                    s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; });
                    s.launch (t, 0);
                    t.mixer->setBase (MP_SEND1, 0.8f); s.multiOut.store (true); s.returns[0]->outChannel.store (9);
                    auto r = render (p, 2.0);
                    auto on = onsets (r.out, 8, 0.01f);
                    std::cout << "  echoes on the return's channels: " << on.size() << " onsets, peak " << db (r.peak[8]) << "\n";
                    check (on.size() >= 3, "hosted delay makes echoes");
                    // a modulator on a hosted parameter
                    if (fb >= 0)
                    {
                        Modulator m; m.src = MOD_LFO; m.div = 14; m.shape = 3; m.depth = 0.4f; m.polarity = 1; m.device = dev->uid; m.param = fb;
                        s.addModulator (*s.returns[0], m);
                        float early = -1, late = -1;
                        render (p, 1.0, [&] (int, double ppq) { if (ppq < 0.1) early = dev->finalValue (fb); if (ppq > 3.8) late = dev->finalValue (fb); });
                        std::cout << "  lfo on the hosted feedback: " << early << " → " << late << "\n";
                        check (late > early + 0.2f, "hosted parameter modulated");
                    }
                    auto x = s.save();
                    StudioProcessor q; q.session.known.addType (*found[0]);
                    juce::String e2; q.session.load (*x, &e2);
                    check (q.session.returns[0]->inserts.size() == 1 && q.session.returns[0]->inserts[0]->kind == Device::Kind::Hosted, "hosted device reloads from the project: " + e2);
                    if (q.session.returns[0]->inserts.size() == 1 && fb >= 0) check (std::abs (q.session.returns[0]->inserts[0]->baseValue (fb) - 0.6f) < 1.0e-4f, "hosted base values reload");
                    // a 16-channel layout request on a stereo-only plug-in falls back cleanly
                    auto d16 = s.createHosted (*found[0], &err);
                    if (d16) { const int w = d16->configure (16, 16); std::cout << "  stereo plug-in asked for 16 channels: in " << d16->inWidth << " out " << w << "\n"; check (w >= 1, "layout fallback"); }
                }
            }
        }
        else std::cout << "  (no built dl4.vst3 found: skipped; pass --vst3)\n";
    }

    // ---- 13. export: stems and midi; produce vs multi-out -------------------------------------------------
    std::cout << "== export + modes\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        auto dir = tmp.getChildFile ("stems");
        juce::String err;
        check (s.renderStems (dir, 0.0, 2.0, nullptr, err), "stems rendered: " + err);
        int files = 0, sounding = 0;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            ++files;
            auto b = readWav (f);
            if (b.getNumSamples() > 0 && b.getMagnitude (0, b.getNumSamples()) > 0.01f) ++sounding;
            if (f.getFileName().startsWith ("master")) check (std::abs ((double) b.getNumSamples() - 3.0 * 4.0 * 60.0 / s.tempo() * kSr) < kBs * 2, "master stem length = 2 bars + 1 bar tail");
        }
        std::cout << "  " << files << " stems, " << sounding << " with sound\n";
        check (files == 8 + 2 + 1, "a stem per track, used return and master");
        check (sounding >= 8, "stems have sound");
        dir.deleteRecursively();
        auto mf = tmp.getChildFile ("sndstudio test.mid");
        check (s.exportMidi (mf, 0.0, 2.0, err), "midi exported: " + err);
        juce::FileInputStream in (mf); juce::MidiFile midi; check (midi.readFrom (in), "midi file reads back");
        int notes = 0; for (int t = 0; t < midi.getNumTracks(); ++t) for (auto* e : *midi.getTrack (t)) if (e->message.isNoteOn()) ++notes;
        check (midi.getNumTracks() == 8 && notes > 20, "midi export has the notes");
        // produce mode: everything on the master; multi-out (assign all): each track on its own channels, master silent
        s.multiOut.store (false);
        auto rp = render (p, 1.0);
        check (rp.peak[0] > 0.05f && rp.peak[2] < 1.0e-6f, "produce mode: stereo master only");
        s.assignOutputs();
        check (s.multiOut.load() && s.tracks[0]->outChannel.load() == 3 && s.tracks[7]->outChannel.load() == 17 && s.returns[0]->outChannel.load() == 19, "assign all: consecutive channels after the master");
        for (auto& t : s.tracks) std::cout << "    " << t->name << " out " << t->outChannel.load() << " toMaster " << t->toMaster.load() << "\n";
        for (auto& t : s.returns) std::cout << "    " << t->name << " out " << t->outChannel.load() << " toMaster " << t->toMaster.load() << "\n";
        float masterMeter = 0; int firstLoud = -1;
        float masterLate = 0;   // the first blocks carry the tail of the previous (produce) render through the master chain's filters
        auto rm = render (p, 1.0, {}, 0, {}, [&] (int b, juce::AudioBuffer<float>& blk) { const float m = s.master->meter[0].load(); if (m > 0.01f && firstLoud < 0) firstLoud = b; masterMeter = std::max (masterMeter, m); if (b >= 8) masterLate = std::max (masterLate, blk.getMagnitude (0, 0, kBs)); });
        std::cout << "  master strip meter " << db (masterMeter) << " (first loud block " << firstLoud << "), from block 8 on: " << db (masterLate) << "\n";
        std::cout << "  multi-out: master " << db (rm.peak[0]) << ", kick 3-4 " << db (rm.peak[2]) << ", bass 17-18 " << db (rm.peak[16]) << ", dub return 19-20 " << db (rm.peak[18]) << "\n";
        check (masterLate < 1.0e-6f, "nothing on the master when every track goes direct");
        check (rm.peak[2] > 0.05f && rm.peak[16] > 0.01f, "tracks on their channels");
        check (rm.peak[18] > 0.001f, "returns on their channels");
    }

    // ---- 14. the studio devices -----------------------------------------------------------------------------
    std::cout << "== studio devices\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        // the suite's two machines as instruments on a track (their own sequencers follow the studio's clock)
        for (auto* type : { "fal:kt1", "fal:kt2" })
        {
            s.newFromTemplate (0);
            juce::String err; auto d = s.createDevice (type, &err);
            check (d != nullptr && d->isInstrument(), juce::String (type) + " is an instrument: " + err);
            if (! d) continue;
            s.setInstrument (*s.tracks[0], std::move (d));
            s.editPattern (*s.tracks[0], 0, [] (Pattern& pt) { pt = Pattern(); for (int k = 0; k < 16; k += 4) pt.s[(size_t) k].on = true; }); s.launch (*s.tracks[0], 0);
            auto r = render (p, 2.0);
            std::cout << "  " << type << " on a track: peak " << db (r.peak[0]) << "\n";
        }
        s.newFromTemplate (0);
        auto make = [&] (const char* type) { juce::String err; auto d = s.createDevice (type, &err); check (d != nullptr, juce::String ("create ") + type + ": " + err); if (d) d->prepare (kSr, kBs); return d; };
        auto setPlain = [] (Device& d, const char* id, float plain)
        {
            const int i = d.paramIndexForId (id); check (i >= 0, juce::String ("parameter ") + id + " of " + d.name);
            if (i < 0) return;
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d.processor()->getParameters()[i]);
            d.setBase (i, rp ? rp->convertTo0to1 (plain) : plain);
        };
        auto run = [&] (Device& d, juce::AudioBuffer<float>& io, int blocks, std::function<void (juce::AudioBuffer<float>&, int)> fill = {})
        {
            juce::AudioBuffer<float> out (2, blocks * kBs); juce::MidiBuffer none;
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> blk (2, kBs); blk.clear();
                if (fill) fill (blk, b); else if (io.getNumSamples() >= (b + 1) * kBs) for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, io, c, b * kBs, kBs);
                d.apply (nullptr, 0.0); none.clear();
                d.process (blk, none);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs);
            }
            return out;
        };
        auto sine = [] (float hz, int n, float amp = 0.5f) { juce::AudioBuffer<float> b (2, n); for (int i = 0; i < n; ++i) { const float v = amp * std::sin (kTwoPi * hz * i / (float) kSr); b.setSample (0, i, v); b.setSample (1, i, v); } return b; };
        auto impulse = [] (int n) { juce::AudioBuffer<float> b (2, n); b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); return b; };
        auto rms = [] (const juce::AudioBuffer<float>& b, int from, int len) { return b.getRMSLevel (0, from, std::min (len, b.getNumSamples() - from)); };
        const int N = 32 * kBs, NB = 32;
        // ms0 on its own: an impulse through the chain twice, with a re-prepare between (the offline render's reset)
        {
            auto d = make ("fal:ms0");
            if (d)
            {
                auto pass = [&] { d->prepare (kSr, kBs); juce::AudioBuffer<float> out (2, 64 * kBs); juce::MidiBuffer none;
                    for (int b = 0; b < 64; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); if (b == 0) { blk.setSample (0, 0, 0.5f); blk.setSample (1, 0, 0.5f); } d->apply (nullptr, 0.0); none.clear(); d->process (blk, none); for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); }
                    return out; };
                auto o1 = pass(); auto o2 = pass(); auto o3 = pass();
                auto peakAt = [] (const juce::AudioBuffer<float>& b) { int at = 0; float m = 0; for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > m) { m = std::abs (b.getSample (0, i)); at = i; } return std::make_pair (at, m); };
                auto first = [] (const juce::AudioBuffer<float>& b) { for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1.0e-4f) return i; return -1; };
                float d12 = 0, d23 = 0; for (int i = 0; i < o1.getNumSamples(); ++i) { d12 = std::max (d12, std::abs (o1.getSample (0, i) - o2.getSample (0, i))); d23 = std::max (d23, std::abs (o2.getSample (0, i) - o3.getSample (0, i))); }
                std::cout << "  ms0 impulse: pass 1 first sound at " << first (o1) << " peak " << peakAt (o1).second << " at " << peakAt (o1).first << "; pass 2 first at " << first (o2) << " peak at " << peakAt (o2).first << "; pass 3 first at " << first (o3) << "; latency reported " << d->latency() << "; diff 1-2 " << d12 << " diff 2-3 " << d23 << "\n";
                check (d12 < 1.0e-6f && d23 < 1.0e-6f, "ms0 renders identically after a re-prepare");
            }
        }
        // every device: instantiates, processes finite audio
        for (auto& type : studioDeviceTypes())
        {
            auto d = make (type.toRawUTF8()); if (! d) continue;
            auto in = sine (220.0f, N);
            auto out = run (*d, in, NB);
            bool finite = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < N; ++i) finite &= std::isfinite (out.getSample (c, i));
            check (finite, type + " finite");
            check (d->numParams() > 0, type + " has parameters");
        }
        { auto d = make ("st:strip"); auto in = sine (50.0f, N); auto flat = run (*d, in, NB); setPlain (*d, "hpon", 1); setPlain (*d, "hp", 500); auto hp = run (*d, in, NB);
          std::cout << "  strip: 50 hz through a 500 hz high-pass " << db (rms (hp, N / 2, N / 2) / rms (flat, N / 2, N / 2)) << "\n"; check (rms (hp, N / 2, N / 2) < rms (flat, N / 2, N / 2) * 0.1f, "strip high-pass");
          setPlain (*d, "hpon", 0); setPlain (*d, "gratio", 8); setPlain (*d, "gthr", -20); auto quiet = sine (220.0f, N, 0.01f); auto gated = run (*d, quiet, NB);
          check (rms (gated, N / 2, N / 2) < rms (quiet, N / 2, N / 2) * 0.3f, "strip gate closes on a quiet signal"); }
        { auto d = make ("st:transient"); juce::AudioBuffer<float> burst (2, N); burst.clear(); for (int i = 0; i < N; ++i) { const float v = 0.5f * std::sin (kTwoPi * 200.0f * i / (float) kSr) * std::exp (-(float) (i % (N / 2)) / 6000.0f); burst.setSample (0, i, v); burst.setSample (1, i, v); }
          auto plain = run (*d, burst, NB); setPlain (*d, "attack", 1.0f); auto att = run (*d, burst, NB);
          const float pa = att.getMagnitude (0, N / 2, 400), pp = plain.getMagnitude (0, N / 2, 400), ta = rms (att, N / 2 + 4000, 2000), tp = rms (plain, N / 2 + 4000, 2000);
          std::cout << "  transient: attack +100% peak " << db (pa / pp) << ", tail " << db (ta / tp) << "\n"; check (pa > pp * 1.3f && ta < tp * 1.3f, "transient designer lifts the attack"); }
        { auto d = make ("st:sidechain"); setPlain (*d, "shape", 1); setPlain (*d, "threshold", -30); setPlain (*d, "key", 1);
          auto in = sine (220.0f, N); std::vector<float> key ((size_t) kBs), silent ((size_t) kBs, 0.0f);
          for (int i = 0; i < kBs; ++i) key[(size_t) i] = 0.9f * std::sin (kTwoPi * 750.0f * i / (float) kSr);   // 750 hz: 64 samples per cycle, repeats per block
          d->keyL = silent.data(); d->keyR = silent.data(); auto open = run (*d, in, NB);
          d->keyL = key.data(); d->keyR = key.data(); auto ducked = run (*d, in, NB);
          std::cout << "  sidechain duck: " << db (rms (ducked, N / 2, N / 2) / rms (open, N / 2, N / 2)) << "\n"; check (rms (ducked, N / 2, N / 2) < rms (open, N / 2, N / 2) * 0.3f, "sidechain ducks with a loud key"); }
        { auto d = make ("st:colour"); setPlain (*d, "drive", 0.8f); auto in = sine (110.0f, N); auto out = run (*d, in, NB);
          float diff = 0; for (int i = N / 2; i < N; ++i) diff = std::max (diff, std::abs (out.getSample (0, i) - in.getSample (0, i)));
          check (diff > 0.02f, "colour colours"); setPlain (*d, "mix", 0.0f); auto dry = run (*d, in, NB); float dd = 0; for (int i = N / 2; i < N; ++i) dd = std::max (dd, std::abs (dry.getSample (0, i) - in.getSample (0, i))); check (dd < 1.0e-4f, "colour mix 0 is the input"); }
        { auto d = make ("st:stereo"); juce::AudioBuffer<float> in (2, N); for (int i = 0; i < N; ++i) { in.setSample (0, i, 0.5f * std::sin (i * 0.05f)); in.setSample (1, i, 0.5f * std::sin (i * 0.031f)); } setPlain (*d, "width", 0.0f); auto out = run (*d, in, NB);
          float dlr = 0; for (int i = 0; i < N; ++i) dlr = std::max (dlr, std::abs (out.getSample (0, i) - out.getSample (1, i))); check (dlr < 1.0e-4f, "stereo width 0 is mono"); }
        { auto d = make ("st:throw"); setPlain (*d, "sync", 0); { auto sil = impulse (kBs); sil.clear(); run (*d, sil, 1); } setPlain (*d, "time", 100); setPlain (*d, "feedback", 0.5f); setPlain (*d, "mix", 1.0f); setPlain (*d, "spread", 0.0f);
          std::cout << "  throw: after flipping to free the time is " << d->paramText (d->paramIndexForId ("time"), d->finalValue (d->paramIndexForId ("time"))) << " (the division's), then set to 100 ms\n";
          d->prepare (kSr, kBs); auto in = impulse (N); auto out = run (*d, in, NB); auto on = onsets (out, 0, 0.02f, 1000);
          std::cout << "  throw: echoes at"; for (int o : on) std::cout << " " << o; std::cout << " (100 ms = 4800)\n"; check (on.size() >= 2 && std::abs (on[1] - on[0] - 4800) <= 60, "throw echo time");
          d->prepare (kSr, kBs); juce::AudioBuffer<float> held (2, N); juce::MidiBuffer none;
          for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); if (b == 0) { blk.setSample (0, 0, 1.0f); blk.setSample (1, 0, 1.0f); } if (b == 2) setPlain (*d, "hold", 1); d->apply (nullptr, 0.0); none.clear(); d->process (blk, none); for (int c = 0; c < 2; ++c) held.copyFrom (c, b * kBs, blk, c, 0, kBs); }
          std::cout << "  throw: second half with feedback 50% " << db (rms (out, N / 2, N / 2)) << ", held " << db (rms (held, N / 2, N / 2)) << "\n";
          check (rms (held, N / 2, N / 2) > rms (out, N / 2, N / 2) * 2.0f, "hold keeps the loop"); }
        { auto d = make ("st:spring"); setPlain (*d, "mix", 1.0f); setPlain (*d, "decay", 0.5f); auto in = impulse (N); auto out = run (*d, in, NB); check (rms (out, 2000, 4000) > 1.0e-4f && rms (out, N - 4000, 4000) < rms (out, 2000, 4000), "spring rings and decays");
          d->prepare (kSr, kBs); juce::AudioBuffer<float> fr (2, N); juce::MidiBuffer none;
          for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); if (b == 0) { blk.setSample (0, 0, 1.0f); blk.setSample (1, 0, 1.0f); } if (b == 2) setPlain (*d, "freeze", 1); d->apply (nullptr, 0.0); none.clear(); d->process (blk, none); for (int c = 0; c < 2; ++c) fr.copyFrom (c, b * kBs, blk, c, 0, kBs); }
          std::cout << "  spring: tail at the end " << db (rms (out, N - 4000, 4000)) << ", frozen " << db (rms (fr, N - 4000, 4000)) << "\n";
          check (rms (fr, N - 4000, 4000) > rms (out, N - 4000, 4000) * 2.0f, "freeze holds the tail"); }
        { auto d = make ("st:vinyl"); setPlain (*d, "noise", 1.0f); setPlain (*d, "crackle", 1.0f); setPlain (*d, "cracklelevel", 1.0f); juce::AudioBuffer<float> in (2, N); in.clear(); auto out = run (*d, in, NB); check (out.getMagnitude (0, 0, N) > 1.0e-4f, "vinyl adds crackle and noise");
          setPlain (*d, "noise", 0.0f); setPlain (*d, "crackle", 0.0f); setPlain (*d, "cracklelevel", 0.0f); auto sn = sine (440.0f, N); auto wow = run (*d, sn, NB); check (wow.getMagnitude (0, N / 2, N / 2) > 0.2f, "vinyl passes audio"); }
        { auto d = make ("st:repeat"); setPlain (*d, "chance", 0.0f); auto in = sine (330.0f, N); auto dry = run (*d, in, NB); float dd = 0; for (int i = 0; i < N; ++i) dd = std::max (dd, std::abs (dry.getSample (0, i) - in.getSample (0, i))); check (dd < 1.0e-4f, "repeat with no chance is the input");
          setPlain (*d, "sync", 0); { auto sil = sine (330.0f, kBs); run (*d, sil, 1); } setPlain (*d, "force", 1); setPlain (*d, "gridms", 50); setPlain (*d, "lengthms", 50); setPlain (*d, "pitch", -12); d->prepare (kSr, kBs);
          std::cout << "  repeat sees force " << d->finalValue (d->paramIndexForId ("force")) << " grid " << d->paramText (d->paramIndexForId ("gridms"), d->finalValue (d->paramIndexForId ("gridms"))) << " mix " << d->finalValue (d->paramIndexForId ("mix")) << "\n";
          auto rep = run (*d, in, NB); float dr = 0; for (int i = N / 2; i < N; ++i) dr = std::max (dr, std::abs (rep.getSample (0, i) - in.getSample (0, i))); std::cout << "  repeat forced: output peak " << db (rep.getMagnitude (0, N / 2, N / 2)) << ", difference to the input " << dr << "\n"; check (dr > 0.05f, "repeat repeats"); }
        { auto d = make ("st:grain"); juce::AudioBuffer<float> sil (2, N); sil.clear(); auto o1 = run (*d, sil, NB); check (o1.getMagnitude (0, 0, N) < 1.0e-6f, "grain: silence in, silence out"); setPlain (*d, "density", 1.0f); setPlain (*d, "mix", 1.0f); auto in = sine (300.0f, N); auto o2 = run (*d, in, NB); check (o2.getMagnitude (0, N / 2, N / 2) > 0.01f, "grain makes grains"); }
        { auto d = make ("st:multiband"); setPlain (*d, "drive1", 1.0f); setPlain (*d, "mode1", 2); auto in = sine (60.0f, N); auto out = run (*d, in, NB); float dd = 0; for (int i = N / 2; i < N; ++i) dd = std::max (dd, std::abs (out.getSample (0, i) - in.getSample (0, i))); check (dd > 0.05f, "multiband distorts the low band"); }
        { auto d = make ("st:clip"); setPlain (*d, "drive", 24); setPlain (*d, "ceiling", -6); setPlain (*d, "dc", 0); auto in = sine (100.0f, N); auto out = run (*d, in, NB); std::cout << "  clip: +24 db into -6 db ceiling → peak " << db (out.getMagnitude (0, N / 2, N / 2)) << "\n"; check (std::abs (out.getMagnitude (0, N / 2, N / 2) - juce::Decibels::decibelsToGain (-6.0f)) < 0.01f, "clip holds the ceiling"); }
        { auto d = make ("st:noise"); setPlain (*d, "level", -20); juce::AudioBuffer<float> sil (2, N); sil.clear(); auto out = run (*d, sil, NB); std::cout << "  noise bed at -20 db: rms " << db (rms (out, N / 2, N / 2)) << "\n"; check (rms (out, N / 2, N / 2) > 0.01f, "noise bed makes noise"); }
        { auto d = make ("st:tone"); setPlain (*d, "hz", 1000); setPlain (*d, "duration", 100); setPlain (*d, "level", -6);
          juce::AudioBuffer<float> out (2, N); juce::MidiBuffer m;
          for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); d->apply (nullptr, 0.0); d->process (blk, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); }
          const int M = 4096; std::vector<float> x ((size_t) M * 2, 0.0f); for (int k = 0; k < M; ++k) x[(size_t) k] = out.getSample (0, 100 + k); juce::dsp::FFT fft (12); fft.performFrequencyOnlyForwardTransform (x.data(), true);
          int best = 1; for (int k = 2; k < M / 2; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k;
          std::cout << "  tone: 1000 hz asked, peak bin at " << best * kSr / M << " hz, level " << db (out.getMagnitude (0, 100, 2000)) << "\n";
          check (std::abs (best * kSr / M - 1000.0) < 15.0, "tone frequency"); check (std::abs (out.getMagnitude (0, 100, 2000) - 0.5f) < 0.02f, "tone level"); check (out.getMagnitude (0, (int) (0.2 * kSr), 1000) < 1.0e-5f, "tone stops after its duration"); }
        { auto d = make ("st:data"); juce::AudioBuffer<float> out (2, N); juce::MidiBuffer m; for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); d->apply (nullptr, 0.0); d->process (blk, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); } check (out.getMagnitude (0, 0, 2000) > 0.05f && out.getMagnitude (0, N - 1000, 1000) < 1.0e-5f, "data burst plays and ends"); }
        { auto d = make ("st:fm"); juce::AudioBuffer<float> out (2, N); juce::MidiBuffer m; for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); d->apply (nullptr, 0.0); d->process (blk, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); } check (out.getMagnitude (0, 0, 2000) > 0.05f, "fm perc hits"); }
        { auto d = make ("st:sampler"); auto sn = sine (440.0f, 48000, 0.5f); auto wf = tmp.getChildFile ("sampler.wav"); writeWav (wf, sn);
          auto* h = dynamic_cast<SampleHolder*> (d->processor()); check (h != nullptr && h->setSampleFile (wf), "sampler loads a file");
          auto play = [&] (int note) { d->prepare (kSr, kBs); juce::AudioBuffer<float> out (2, N); juce::MidiBuffer m; for (int b = 0; b < NB; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0); d->apply (nullptr, 0.0); d->process (blk, m); for (int c = 0; c < 2; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); } return out; };
          auto o60 = play (60); check (o60.getMagnitude (0, 100, 2000) > 0.2f, "sampler plays");
          auto fund = [&] (const juce::AudioBuffer<float>& b) { const int M = 8192; std::vector<float> x ((size_t) M * 2, 0.0f); for (int k = 0; k < M; ++k) x[(size_t) k] = b.getSample (0, 100 + k); juce::dsp::FFT fft (13); fft.performFrequencyOnlyForwardTransform (x.data(), true); int best = 1; for (int k = 2; k < M / 2; ++k) if (x[(size_t) k] > x[(size_t) best]) best = k; return best * kSr / M; };
          auto o72 = play (72); std::cout << "  sampler: c3 " << fund (o60) << " hz, c4 " << fund (o72) << " hz (ratio " << fund (o72) / fund (o60) << ")\n"; check (std::abs (fund (o72) / fund (o60) - 2.0) < 0.06, "sampler repitches by the note");
          setPlain (*d, "mode", 2); setPlain (*d, "slices", 4); auto sl = play (36 + 1); check (sl.getMagnitude (0, 100, 2000) > 0.2f, "sampler slice 2 plays"); }
    }

    // ---- 15. uneven grids, off-grid modulators, the tempo as a target ----------------------------------------
    std::cout << "== uneven steps + free modulators\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t); s.setTempo (120.0f); s.global->setBase (GP_SWING, 0.0f);
        // four steps lasting 1, 0.5, 1.5 and 1 sixteenths: hits at 0, 1, 1.5 and 3 sixteenths; the loop is 4 sixteenths
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 4; for (int i = 0; i < 4; ++i) { pt.s[(size_t) i].on = true; pt.s[(size_t) i].vel = 1; } pt.s[1].dur = 0.5f; pt.s[2].dur = 1.5f; });
        s.launch (t, 0);
        auto r = render (p, 0.5); auto on = onsets (r.out, 0, 0.05f, 1500);
        const double six = 0.25 * spb (120);
        std::cout << "  step times 1 / 0.5 / 1.5 / 1: hits at"; for (int o : on) std::cout << " " << o; std::cout << "  (expected 0 " << (int) six << " " << (int) (1.5 * six) << " " << (int) (3 * six) << " " << (int) (4 * six) << " ...)\n";
        check (on.size() == 8, "uneven pattern: eight hits in two beats");
        if (on.size() >= 5) { check (std::abs (on[1] - (int) six) <= 2 && std::abs (on[2] - (int) (1.5 * six)) <= 2 && std::abs (on[3] - (int) (3.0 * six)) <= 2, "every step starts after the durations before it"); check (std::abs (on[4] - (int) (4.0 * six)) <= 2, "the loop is the sum of the step times"); }
        check ((*t.patterns[0]).span() == 4.0 && ! (*t.patterns[0]).even(), "pattern span");
        // a slide across uneven steps lands on the later step's time
        auto x = s.save(); StudioProcessor q; q.session.load (*x, nullptr);
        check (q.session.tracks[0]->patternAt (0) && std::abs (q.session.tracks[0]->patternAt (0)->s[2].dur - 1.5f) < 1.0e-6f, "step times survive save / load");
        // speed x8: 1/128 notes
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[8].on = true; });
        t.speed.store (7);
        auto r8 = render (p, 0.25); auto on8 = onsets (r8.out, 0, 0.05f, 3000);
        check (on8.size() >= 2 && std::abs (on8[1] - (int) (8 * 0.25 / 8.0 * spb (120))) <= 2, "speed x8: steps of a 1/128 note");
        t.speed.store (3);
        // a step modulator off the grid: 4 steps a second, whatever the tempo
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; });
        Modulator m; m.src = MOD_STEPS; m.sync = false; m.hz = 4.0f; m.stepLen = 4; m.steps = { 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; m.depth = 1.0f; m.polarity = 1; m.device = 0; m.param = MP_PAN;
        t.mixer->setBase (MP_PAN, 0.0f);
        s.addModulator (t, m);
        auto countFlips = [&] (float bpm) { s.setTempo (bpm); int flips = 0; float last = -1; render (p, bpm / 60.0 * 2.0 / 4.0, [&] (int, double) { const float v = t.mixer->finalValue (MP_PAN); if (last >= 0 && ((v > 0.5f) != (last > 0.5f))) ++flips; last = v; }); return flips; };
        const int f120 = countFlips (120.0f), f90 = countFlips (90.0f);
        std::cout << "  free step modulator at 4 hz over 2 s: " << f120 << " flips at 120 bpm, " << f90 << " at 90 bpm (expected 7 or 8 both)\n";
        check (f120 >= 6 && f120 <= 9 && std::abs (f120 - f90) <= 1, "free step modulator ignores the tempo");
        s.editMods (t, [] (std::vector<Modulator>& v) { v.clear(); });
        // the master's modulators reach the globals: an lfo on the tempo
        s.setTempo (120.0f);
        Modulator tm; tm.src = MOD_LFO; tm.shape = 3; tm.sync = false; tm.hz = 0.5f; tm.depth = 0.25f; tm.polarity = 1; tm.device = 0; tm.param = GP_TEMPO;
        s.addModulator (*s.master, tm);
        float lo = 1e9f, hi = 0;
        render (p, 2.0, [&] (int, double) { lo = std::min (lo, s.tempo()); hi = std::max (hi, s.tempo()); });
        std::cout << "  an lfo on the tempo: " << lo << " ... " << hi << " bpm\n";
        check (hi > lo + 15.0f && lo >= 119.0f, "tempo is a modulator target");
        check (s.targetName ({ 0, 0, GP_TEMPO }).contains ("tempo"), "global target name");
        s.editMods (*s.master, [] (std::vector<Modulator>& v) { v.clear(); });
    }

    // ---- 16. one gesture: mapping modulators, lanes and keys from a control --------------------------------
    std::cout << "== mapping from a control\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; auto* inst = t.instrument.get(); const int tune = inst->paramIndexForId ("tune");
        const int a = ParamMenu::act (s, t, *inst, tune, ParamMenu::Lfo), b = ParamMenu::act (s, t, *inst, tune, ParamMenu::Random);
        const int c = ParamMenu::act (s, t, *inst, tune, ParamMenu::Slewed), d = ParamMenu::act (s, t, *inst, tune, ParamMenu::Gate);
        check (t.mods->size() == 4 && a > 0 && b > 0 && c > 0 && d > 0, "four modulators mapped, one gesture each");
        if (t.mods->size() == 4)
        {
            check ((*t.mods)[0].src == MOD_LFO && (*t.mods)[1].src == MOD_RANDOM && (*t.mods)[1].slew == 0.0f && (*t.mods)[2].src == MOD_RANDOM && (*t.mods)[2].slew > 0.2f && (*t.mods)[3].src == MOD_GATE, "lfo, random, slewed random, random gate");
            for (auto& m : *t.mods) check (m.device == inst->uid && m.param == tune, "mapped to the control that was clicked");
        }
        // the popup edits the shape and the rate of the one just made
        { ModPopup pop (s, t, a); pop.choose (MOD_LFO, 4); check (s.findMod (t, a) && s.findMod (t, a)->shape == 4, "the popup sets the shape"); }
        // the steps of a step modulator are drawn in the popup
        {
            const int st = ParamMenu::act (s, t, *inst, tune, ParamMenu::Steps);
            ModPopup pop (s, t, st); pop.setSize (300, 400); pop.resized();
            const auto box = pop.stepArea();
            check (! box.isEmpty(), "the step editor is shown for a step modulator");
            const juce::Point<float> at ((float) box.getX() + 2.0f, (float) box.getY() + 1.0f);
            pop.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &pop, &pop, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false));
            check (s.findMod (t, st) && s.findMod (t, st)->steps[0] > 0.9f, "drawing in the popup sets a step");
        }
        s.editMods (t, [] (std::vector<Modulator>& v) { v.clear(); });
        ParamMenu::act (s, t, *t.mixer, MP_SEND1, ParamMenu::Lane);
        check (s.lane (s.targetFor (t, *t.mixer, MP_SEND1)) != nullptr, "an automation lane for a send, from the control");
        // keys: learn "k" as a toggle of the mute, "j" as a hold of send 1
        ParamMenu::act (s, t, *t.mixer, MP_MUTE, ParamMenu::KeyToggle);
        check (s.keyEvent ('k', true), "key learned"); s.keyEvent ('k', false);
        ParamMenu::act (s, t, *t.mixer, MP_SEND1, ParamMenu::KeyHold);
        s.keyEvent ('j', true); s.keyEvent ('j', false);
        check (s.keyMaps->size() == 2, "two key maps");
        t.mixer->setBase (MP_SEND1, 0.0f);
        s.keyEvent ('k', true); check (t.muted(), "the key mutes"); s.keyEvent ('k', false); check (t.muted(), "a toggle stays");
        s.keyEvent ('k', true); check (! t.muted(), "and toggles back"); s.keyEvent ('k', false);
        s.keyEvent ('j', true); check (t.mixer->baseValue (MP_SEND1) > 0.99f, "a held key opens the send"); s.keyEvent ('j', false); check (t.mixer->baseValue (MP_SEND1) < 0.01f, "and closes it on release");
        check (! s.keyEvent ('q', true), "an unmapped key is left alone");
        auto x = s.save(); StudioProcessor q; q.session.load (*x, nullptr);
        check (q.session.keyMaps->size() == 2 && (*q.session.keyMaps)[1].momentary && (*q.session.keyMaps)[0].key == 'k', "key maps survive save / load");
        // with write on, a played mute lands in a lane
        s.removeLane (s.targetFor (t, *t.mixer, MP_MUTE));
        s.autoWrite.store (true);
        render (p, 1.0, [&] (int blk, double) { if (blk == 20) s.keyEvent ('k', true); if (blk == 40) s.keyEvent ('k', true); });
        s.autoWrite.store (false);
        auto l = s.lane (s.targetFor (t, *t.mixer, MP_MUTE));
        check (l && l->pts.size() >= 2, "key presses are recorded as automation");
    }

    // ---- 17. the spatial devices, and the workflow with them -------------------------------------------------
    std::cout << "== spatial devices\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto runWide = [&] (Device& d, int width, std::function<void (juce::AudioBuffer<float>&)> fill, int blocks = 8)
        {
            d.prepare (kSr, kBs); juce::AudioBuffer<float> out (width, blocks * kBs); juce::MidiBuffer none;
            for (int b = 0; b < blocks; ++b) { juce::AudioBuffer<float> blk (width, kBs); blk.clear(); fill (blk); d.apply (nullptr, 0.0); d.process (blk, none); for (int c = 0; c < width; ++c) out.copyFrom (c, b * kBs, blk, c, 0, kBs); }
            return out;
        };
        auto dc = [] (juce::AudioBuffer<float>& b) { for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 1.0f); };
        juce::String err;
        auto enc = s.createDevice ("st:encoder", &err); check (enc != nullptr, "encoder: " + err);
        if (enc)
        {
            check (enc->configure (2, 16) == 16 && enc->inWidth == 2, "encoder on a 16-channel track: 2 in, 16 out");
            check (enc->configure (2, 24) == 16, "on a 24-channel track it still makes third order (16)");
            check (enc->configure (2, 64) == 64 && enc->configure (1, 9) == 9 && enc->inWidth == 1, "seventh order on 64 channels; mono in, second order on 9");
            enc->configure (2, 16);
            auto set = [&] (const char* id, float plain) { const int i = enc->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (enc->processor()->getParameters()[i]); enc->setBase (i, rp->convertTo0to1 (plain)); };
            set ("width", 0.0f); set ("azimuth", 0.0f);
            auto front = runWide (*enc, 16, dc);
            const int last = front.getNumSamples() - 1;
            std::cout << "  encoder, a source in front: w " << front.getSample (0, last) << "  y " << front.getSample (1, last) << "  z " << front.getSample (2, last) << "  x " << front.getSample (3, last) << "\n";
            check (std::abs (front.getSample (0, last) - 1.0f) < 1.0e-3f && std::abs (front.getSample (3, last) - 1.0f) < 1.0e-3f && std::abs (front.getSample (1, last)) < 1.0e-3f && std::abs (front.getSample (2, last)) < 1.0e-3f, "front: w = x = 1, y = z = 0 (acn / sn3d)");
            set ("azimuth", 90.0f);
            auto left = runWide (*enc, 16, dc);
            check (std::abs (left.getSample (1, last) - 1.0f) < 1.0e-3f && std::abs (left.getSample (3, last)) < 1.0e-3f, "90 degrees left: y = 1, x = 0");
            // second order, acn 4 (xy term): sqrt(3)/2 sin(2 az) cos^2(el) → 0 at 90°, and acn 8: sqrt(3)/2 cos(2 az) → -0.866
            check (std::abs (left.getSample (8, last) + 0.8660254f) < 1.0e-3f, "second order term");
            set ("elevation", 90.0f);
            auto up = runWide (*enc, 16, dc);
            check (std::abs (up.getSample (2, last) - 1.0f) < 1.0e-3f && std::abs (up.getSample (1, last)) < 1.0e-3f, "straight up: z = 1");
        }
        // decoder: a ring of eight, the source at 90° left comes out of the speaker at 90° left
        auto dec = s.createDevice ("st:decoder", &err);
        if (dec && enc)
        {
            check (dec->configure (16, 8) == 8 && dec->inWidth == 16, "decoder on an 8-channel track fed 16: 16 in, 8 out");
            auto setE = [&] (const char* id, float plain) { const int i = enc->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (enc->processor()->getParameters()[i]); enc->setBase (i, rp->convertTo0to1 (plain)); };
            setE ("elevation", 0.0f); setE ("azimuth", 90.0f); enc->configure (2, 16);
            dec->setBase (dec->paramIndexForId ("direction"), 0.0f);   // anticlockwise: speaker k at k * 45° to the left
            enc->prepare (kSr, kBs); dec->prepare (kSr, kBs);
            juce::AudioBuffer<float> blk (16, kBs); juce::MidiBuffer none; float pk[8] = {};
            for (int b = 0; b < 8; ++b) { blk.clear(); for (int i = 0; i < kBs; ++i) blk.setSample (0, i, 1.0f); enc->apply (nullptr, 0.0); enc->process (blk, none); dec->apply (nullptr, 0.0); dec->process (blk, none); for (int c = 0; c < 8; ++c) pk[c] = blk.getMagnitude (c, 0, kBs); }
            std::cout << "  decoder, ring of 8, source at 90 left:"; for (float v : pk) std::cout << " " << juce::String (v, 2); std::cout << "\n";
            int best = 0; for (int c = 1; c < 8; ++c) if (pk[c] > pk[best]) best = c;
            check (best == 2, "the loudest speaker is the one at 90 degrees");
            check (pk[6] < pk[2] * 0.2f, "the opposite speaker is quiet");
            float e = 0; for (float v : pk) e += v * v;
            check (std::abs (e - 1.0f) < 0.15f, "unity power over the ring");
            check (dec->configure (16, 24) == 24, "24 speakers on a 24-channel track");
        }
        // binaural: a source on the left is louder and earlier in the left ear
        auto bin = s.createDevice ("st:binaural", &err);
        if (bin && enc)
        {
            check (bin->configure (16, 16) == 2 && bin->inWidth == 16, "binaural on a 16-channel track: 16 in, 2 out");
            enc->prepare (kSr, kBs); bin->prepare (kSr, kBs);
            juce::AudioBuffer<float> out (2, 8 * kBs); juce::AudioBuffer<float> blk (16, kBs); juce::MidiBuffer none; Rng rn; rn.seed (3);
            for (int b = 0; b < 8; ++b) { blk.clear(); for (int i = 0; i < kBs; ++i) blk.setSample (0, i, b == 0 && i == 100 ? 1.0f : 0.0f); enc->apply (nullptr, 0.0); enc->process (blk, none); bin->apply (nullptr, 0.0); bin->process (blk, none); out.copyFrom (0, b * kBs, blk, 0, 0, kBs); out.copyFrom (1, b * kBs, blk, 1, 0, kBs); check (blk.getMagnitude (2, 0, kBs) < 1.0e-9f, "binaural clears the ambisonic channels"); }
            auto energy = [&] (int ch) { double e = 0; for (int i = 0; i < out.getNumSamples(); ++i) e += out.getSample (ch, i) * out.getSample (ch, i); return e; };
            auto first = [&] (int ch) { float m = out.getMagnitude (ch, 0, out.getNumSamples()); for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (ch, i)) > m * 0.5f) return i; return -1; };
            std::cout << "  binaural, source at 90 left: left ear " << db ((float) std::sqrt (energy (0))) << " from sample " << first (0) << ", right ear " << db ((float) std::sqrt (energy (1))) << " from sample " << first (1) << "\n";
            check (energy (0) > energy (1) * 1.5, "louder in the left ear");
            check (first (1) > first (0), "and earlier");
        }
    }
    std::cout << "== spatial workflow with the studio's own devices, through the mixer page\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        se->showPage (3); se->tickForTest();
        auto* mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        check (mix != nullptr, "the mix page");
        // "i make a new channel that defaults stereo, i change it to be a 16 ch track, i tell it to not send audio to the master"
        // (a hat: bright enough for the head's shadow to show which side it is on)
        auto& encT = *s.tracks[0]; s.setInstrument (encT, s.createDevice ("fal:hh4")); s.setTempo (120.0f);
        s.editPattern (encT, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; }); s.launch (encT, 0);
        check (encT.width.load() == 2, "a new track is stereo");
        auto* es = mix->stripFor (encT);
        es->setWidth (16); es->toMaster.onClick();
        check (encT.width.load() == 16 && ! encT.toMaster.load(), "16 channels, not to the master");
        auto* e = s.addInsert (encT, s.createDevice ("st:encoder"));
        e->setBase (e->paramIndexForId ("azimuth"), (90.0f + 180.0f) / 360.0f);
        check (e->inWidth == 2 && e->outWidth == 16 && encT.outWidth() == 16 && encT.inWidth() == 2, "stereo in, 16 out on that track");
        // "i make another channel that will be my decoder channel. i set that channel to 24"
        auto* decT = s.addTrack ("decoder"); se->tickForTest(); mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        mix->stripFor (*decT)->setWidth (24);
        auto* d = s.addInsert (*decT, s.createDevice ("st:decoder"));
        // "i drag the route icon from the 16ch track to the 24 ch track and a popup shows me how many channels i want to route, i say 16"
        se->tickForTest(); mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        es = mix->stripFor (encT); auto* ds = mix->stripFor (*decT);
        check (es && ds && es->route.isVisible(), "the route icon is on the strip");
        {
            auto& h = es->route;
            const auto now = juce::Time::getCurrentTime(); auto src = juce::Desktop::getInstance().getMainMouseSource();
            const juce::Point<float> down (8.0f, 8.0f);
            const auto target = (ds->getScreenBounds().getCentre() - h.getScreenPosition()).toFloat();
            h.mouseDown (juce::MouseEvent (src, down, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &h, &h, now, down, now, 1, false));
            h.mouseDrag (juce::MouseEvent (src, target, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &h, &h, now, down, now, 1, true));
            check (ds->dropTarget, "the strip under the cable lights up");
            h.mouseUp (juce::MouseEvent (src, target, juce::ModifierKeys(), 0, 0, 0, 0, 0, &h, &h, now, down, now, 1, true));
        }
        auto* pop = mix->routePopup();
        check (pop != nullptr && pop->isVisible(), "dropping the route icon on another strip opens the channel popup");
        if (pop)
        {
            check (&pop->src == &encT && &pop->dst == decT && pop->suggestion() == 16, "the popup is for encoder > decoder and suggests 16 channels");
            if (shotDir != juce::File()) { for (int k = 0; k < 3; ++k) se->tickForTest(); juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); const auto f = shotDir.getChildFile ("sndstudio-route.png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os); }
            check (pop->choose (16), "16 chosen");
        }
        check (encT.routes->size() == 1 && (*encT.routes)[0].dst == decT->uid && (*encT.routes)[0].channels == 16 && ! (*encT.routes)[0].feedback, "the encoder is routed to the decoder over 16 channels");
        check (d->inWidth == 16 && d->outWidth == 24 && decT->inWidth() == 16 && decT->outWidth() == 24, "the decoder reads the 16 routed channels and feeds 24 speakers");
        check (decT->toMaster.load(), "the decoder goes to the master");
        // "i open the master channel and make sure the number of output channels is set to 24 and the vu meters show 24"
        mix->rebuild(); auto* ms = mix->stripFor (*s.master);
        ms->setWidth (24);
        check (s.master->width.load() == 24, "the master is 24 channels wide");
        float masterMeters[24] = {}; int lit = 0;
        auto r = render (p, 1.0, [&] (int, double) { for (int c = 0; c < 24; ++c) masterMeters[c] = std::max (masterMeters[c], s.master->meter[(size_t) c].load()); });
        for (float v : masterMeters) if (v > 1.0e-4f) ++lit;
        int outsLit = 0; for (int c = 0; c < 24; ++c) if (r.peak[(size_t) c] > 1.0e-4f) ++outsLit;
        std::cout << "  encoder (16) > decoder (24) > master (24): " << lit << " of 24 master meters move, " << outsLit << " of 24 device channels carry sound, channel 25 " << db (r.peak[24]) << "\n";
        check (lit >= 20 && outsLit >= 20, "24 meters on the master, 24 channels out");
        check (r.peak[24] < 1.0e-9f, "nothing beyond the master's 24 channels");
        // "i place a third channel and set it to 16 channels, i route my encoder track also to this channel over 16 channels and place a binaural decoder"
        auto* binT = s.addTrack ("binaural"); se->tickForTest(); mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        mix->stripFor (*binT)->setWidth (16);
        auto* b = s.addInsert (*binT, s.createDevice ("st:binaural"));
        mix->askRoute (encT, *binT, { 300, 300 });
        check (mix->routePopup() && mix->routePopup()->suggestion() == 16 && mix->routePopup()->choose (16), "encoder > binaural over 16 channels");
        check (b->inWidth == 16 && b->outWidth == 2 && binT->outWidth() == 2, "16 in, only 2 out of the binaural track");
        // "i can set my master out back to 2 channels, mute my 24ch decoder, and work on the arrangement in stereo binaural"
        mix->rebuild(); mix->stripFor (*s.master)->setWidth (2); mix->stripFor (*decT)->mute.onClick();
        check (s.master->width.load() == 2 && decT->muted(), "master back to 2 channels, the decoder muted");
        auto rb = render (p, 1.0);
        const float l = rb.out.getRMSLevel (0, 0, rb.out.getNumSamples()), rr = rb.out.getRMSLevel (1, 0, rb.out.getNumSamples());
        std::cout << "  binaural on a stereo master, the source at 90 left: L " << db (l) << "  R " << db (rr) << "  channel 3 " << db (rb.peak[2]) << "\n";
        check (l > 1.0e-3f && l > rr * 1.2f && rb.peak[2] < 1.0e-9f, "stereo binaural on the master, the image to the left");
        // the project carries all of it
        auto x = s.save(); StudioProcessor q; juce::String e2; q.session.load (*x, &e2);
        check (q.session.tracks.size() == 3 && q.session.tracks[0]->width.load() == 16 && q.session.tracks[0]->routes->size() == 2 && q.session.tracks[1]->inserts.size() == 1 && q.session.tracks[1]->inserts[0]->inWidth == 16 && q.session.tracks[1]->inserts[0]->outWidth == 24 && q.session.tracks[2]->inserts[0]->outWidth == 2, "the spatial session reloads: " + e2);
        // a stereo effect on the 16-channel track leaves channels 3 … 16 alone
        s.addInsert (encT, s.createDevice ("fal:fl7"));
        check (encT.outWidth() == 16, "a stereo device after the encoder keeps the 16 channels");
        float m16 = 0; render (p, 0.5, [&] (int, double) { m16 = std::max (m16, encT.meter[8].load()); });
        check (m16 > 1.0e-4f, "and they still carry sound");
        // a stereo file on the 16-channel track is the encoder's input
        juce::AudioBuffer<float> st2 (2, 24000); for (int i = 0; i < 24000; ++i) { st2.setSample (0, i, 0.4f * std::sin (i * 0.05f)); st2.setSample (1, i, 0.4f * std::sin (i * 0.031f)); }
        auto wf = tmp.getChildFile ("stereo for the encoder.wav"); writeWav (wf, st2);
        auto* at = s.addTrack ("clip", -1, TrackKind::Audio); s.setWidth (*at, 16); at->toMaster.store (false);
        s.addAudioClip (*at, wf, 0.0); auto* ae = s.addInsert (*at, s.createDevice ("st:encoder"));
        check (ae->inWidth == 2 && ae->outWidth == 16 && at->sourceWidth == 2, "the stereo clip is the encoder's stereo input on the 16-channel track");
        float w0 = 0, w8 = 0, w14 = 0; render (p, 0.25, [&] (int, double) { w0 = std::max (w0, at->meter[0].load()); w8 = std::max (w8, at->meter[8].load()); w14 = std::max (w14, at->meter[11].load()); });   // (acn 8 and 11: terms that are not zero in the horizontal plane)
        check (w0 > 0.05f && w8 > 1.0e-3f && w14 > 1.0e-3f, "stereo in, 16 channels out");
    }

    // ---- 18. widths: routes decide what a chain reads; wide files; feedback routes ---------------------------
    std::cout << "== widths, wide files, feedback routes\n";
    {
        // a device that takes any layout (like a real decoder plug-in): what it reads must follow the routes
        struct Flex : WideProc { Flex() : WideProc (2, 2) {} bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet().size() >= 1 && l.getMainOutputChannelSet().size() >= 1; }
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {} };
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& a = *s.tracks[0]; s.setWidth (a, 16);
        auto* bT = s.addTrack ("b"); s.setWidth (*bT, 24);
        auto* f = s.addInsert (*bT, Device::makeFromProcessor (950, std::make_unique<Flex>(), "test:flex", "flex"));
        check (f->inWidth == 2, "nothing routed in: the device reads the stereo source");
        s.addRoute (a, { bT->uid, 16, 0, 0, 1.0f });
        check (f->inWidth == 16 && bT->sourceWidth == 16, "16 channels routed in: the device reads 16");
        s.removeRoute (a, 0);
        check (f->inWidth == 2, "route removed: back to stereo");
        s.addRoute (a, { bT->uid, 4, 0, 8, 1.0f });
        check (f->inWidth == 12, "4 channels into channel 9: the chain is 12 wide");
        // feedback: a > b is a normal route, b > a closes the loop
        s.removeRoute (a, 0); s.setWidth (a, 2); s.setWidth (*bT, 2); s.removeDevice (*bT, 950);
        shortHit (a); s.setTempo (120.0f);
        s.editPattern (a, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; }); s.launch (a, 0);
        check (s.addRoute (a, { bT->uid, 2, 0, 0, 1.0f }), "a > b");
        s.multiOut.store (true); bT->outChannel.store (5); bT->toMaster.store (false); a.toMaster.store (false);
        auto dry = render (p, 1.0);
        check (s.addRoute (*bT, { a.uid, 2, 0, 0, 0.9f }), "b > a is accepted");
        check ((*bT->routes)[0].feedback && ! (*a.routes)[0].feedback, "and marked as a feedback route");
        auto wet = render (p, 1.0);
        float diff = 0, peak = 0; bool finite = true;
        for (int i = 0; i < wet.out.getNumSamples(); ++i) { diff = std::max (diff, std::abs (wet.out.getSample (4, i) - dry.out.getSample (4, i))); peak = std::max (peak, std::abs (wet.out.getSample (4, i))); finite &= std::isfinite (wet.out.getSample (4, i)); }
        // the first return arrives one buffer after the hit
        int firstDiff = -1; for (int i = 0; i < wet.out.getNumSamples() && firstDiff < 0; ++i) if (std::abs (wet.out.getSample (4, i) - dry.out.getSample (4, i)) > 1.0e-5f) firstDiff = i;
        std::cout << "  feedback loop at 0.9: differs from the dry by " << diff << " from sample " << firstDiff << " (one buffer = " << kBs << "), peak " << db (peak) << "\n";
        check (finite && diff > 0.01f && std::abs (firstDiff - kBs) <= 8, "the loop feeds back one buffer late");
        s.editRoutes (*bT, [] (std::vector<Route>& v) { v[0].gain = 4.0f; });
        check ((*bT->routes)[0].feedback, "still a feedback route after an edit");
        auto loud = render (p, 4.0);
        float lp = 0; bool lf = true; for (int i = 0; i < loud.out.getNumSamples(); ++i) { lp = std::max (lp, std::abs (loud.out.getSample (4, i))); lf &= std::isfinite (loud.out.getSample (4, i)); }
        std::cout << "  the same loop with a gain of 4 (+12 db): peak " << db (lp) << " after 8 s\n";
        check (lf && lp < 8.0f, "a runaway loop is held by the limiter");
        auto x = s.save(); StudioProcessor q; q.session.load (*x, nullptr);
        check (q.session.tracks[1]->routes->size() == 1 && (*q.session.tracks[1]->routes)[0].feedback && ! (*q.session.tracks[0]->routes)[0].feedback, "feedback routes survive save / load");
        auto again = render (p, 1.0); auto again2 = render (p, 1.0);
        (void) again; (void) again2;
        // a four-channel file plays all four channels on a four-channel track
        s.newFromTemplate (0); s.removeTrack (0); p.prepareToPlay (kSr, kBs);
        juce::AudioBuffer<float> quad (4, 24000); for (int c = 0; c < 4; ++c) for (int i = 0; i < 24000; ++i) quad.setSample (c, i, 0.1f * (float) (c + 1));
        auto qf = tmp.getChildFile ("quad.wav"); writeWav (qf, quad);
        auto* qt = s.addTrack ("quad", -1, TrackKind::Audio); s.setWidth (*qt, 4);
        check (s.addAudioClip (*qt, qf, 0.0) != nullptr && qt->sourceWidth == 4, "a four-channel clip makes the track's source four wide");
        s.multiOut.store (true); qt->outChannel.store (9); qt->toMaster.store (false);
        auto rq = render (p, 0.2);
        std::cout << "  a 4-channel file on a 4-channel track: " << rq.peak[8] << " " << rq.peak[9] << " " << rq.peak[10] << " " << rq.peak[11] << "\n";
        check (std::abs (rq.peak[8] - 0.1f) < 0.01f && std::abs (rq.peak[9] - 0.2f) < 0.01f && std::abs (rq.peak[10] - 0.3f) < 0.01f && std::abs (rq.peak[11] - 0.4f) < 0.01f, "every channel of the file plays");
        s.setWidth (*qt, 2);
        auto rq2 = render (p, 0.2);
        check (std::abs (rq2.peak[8] - 0.1f) < 0.01f && rq2.peak[10] < 1.0e-9f, "on a stereo track the first pair plays");
        // resampling a 4-channel master gives a 4-channel track
        s.setWidth (*qt, 4); s.setWidth (*s.master, 4); qt->toMaster.store (true); s.multiOut.store (false);
        s.projectFile = tmp.getChildFile ("wide.fstudio"); s.projectName = "wide";
        juce::String err; auto* rs = s.renderProjectToTrack (0.0, 0.25, false, err);
        check (rs && rs->width.load() == 4 && rs->aclips->size() == 1 && (*rs->aclips)[0].sample && (*rs->aclips)[0].sample->data.getNumChannels() == 4, "a 4-channel master resamples into a 4-channel track: " + err);
    }

    // ---- 19. the browser's audition, bouncing, the file menu, auto master, keys: through the editor --------
    std::cout << "== the pages, used\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.projectFile = tmp.getChildFile ("pages.fstudio"); s.projectName = "pages";
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        auto pump = [&] (int blocks) { juce::AudioBuffer<float> blk (kOutChannels, kBs); float pk = 0; for (int b = 0; b < blocks; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); pk = std::max (pk, blk.getMagnitude (0, 0, kBs)); if (b % 8 == 0) se->tickForTest(); } return pk; };
        auto click = [] (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime(); auto src = juce::Desktop::getInstance().getMainMouseSource();
            c.mouseDown (juce::MouseEvent (src, at, mods, 0, 0, 0, 0, 0, &c, &c, now, at, now, clicks, false));
            c.mouseUp (juce::MouseEvent (src, at, juce::ModifierKeys(), 0, 0, 0, 0, 0, &c, &c, now, at, now, clicks, false));
        };
        // session: a click on a cell launches that pattern on that track
        se->showPage (0);
        auto* session = dynamic_cast<SessionPage*> (se->pageComponent (0));
        s.play(); pump (4);
        click (*session, { (float) (session->left + 1 * session->colW + 20), (float) (session->top + 2 * session->rowH + 6) });   // rim, scene 3 ("bass in": pattern 1)
        check (s.tracks[1]->pendingPattern.load() == 0 || s.tracks[1]->pattern.load() == 0, "a cell click launches the scene's pattern on that track");
        click (*session, { 20.0f, (float) (session->top + 4 * session->rowH + 6) });   // the scene "full"
        check (s.currentScene.load() == 4, "a click on a scene launches it");
        check (pump (200) > 0.05f, "and it plays");
        // seq: a click on a step switches it on, the step's time row makes it uneven
        se->selectTrack (s.tracks[1].get()); se->showPage (2);
        auto* seq = dynamic_cast<SeqPage*> (se->pageComponent (2));
        const int pi = s.tracks[1]->pattern.load();
        const bool was = s.tracks[1]->patternAt (pi) && s.tracks[1]->patternAt (pi)->s[4].on;
        click (*seq, seq->cell (4).getCentre());
        check (s.tracks[1]->patternAt (pi) && s.tracks[1]->patternAt (pi)->s[4].on != was, "a click on a step toggles it");
        seq->sDur->set (1.5f, true);
        check (std::abs (s.tracks[1]->patternAt (pi)->s[4].dur - 1.5f) < 1.0e-6f, "the step time row sets the selected step's duration");
        seq->sDur->set (1.0f, true);
        // arrange: dropping a file makes an audio track with a clip at that bar; a lane click adds a point
        juce::AudioBuffer<float> tone (2, 48000); for (int i = 0; i < 48000; ++i) { const float v = 0.3f * std::sin (kTwoPi * 330.0f * i / 48000.0f) * std::exp (-i / 12000.0f); tone.setSample (0, i, v); tone.setSample (1, i, v); }
        auto tf = tmp.getChildFile ("drop me.wav"); writeWav (tf, tone);
        se->showPage (1);
        auto* arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
        const int before = (int) s.tracks.size();
        arr->dropFile (tf, arr->x (4.0), arr->lanesArea().getBottom() - 4);   // below the lanes: a new audio track
        check ((int) s.tracks.size() == before + 1 && s.tracks.back()->isAudio() && s.tracks.back()->aclips->size() == 1 && std::abs ((*s.tracks.back()->aclips)[0].startBar - 4.0) < 1.0e-6, "a dropped file becomes a clip on the timeline at that bar");
        auto* at = s.tracks.back().get();
        s.addInsert (*at, s.createDevice ("fal:dl4"));   // "and effect them"
        se->selectTrack (at); se->showPage (1); arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
        ParamMenu::act (s, *at, *at->mixer, MP_LEVEL, ParamMenu::Lane);   // "and automate them"
        const auto aa = arr->autoArea();
        int row = 0; { const auto tg = arr->laneTargets(); for (int i = 0; i < (int) tg.size(); ++i) if (tg[(size_t) i] == s.targetFor (*at, *at->mixer, MP_LEVEL)) row = i; }
        click (*arr, { (float) arr->x (6.0), (float) (aa.getY() + 26 + row * 52 + 10) });
        auto ln = s.lane (s.targetFor (*at, *at->mixer, MP_LEVEL));
        check (ln && ln->pts.size() == 2 && std::abs (ln->pts[1].beat - 24.0) < 0.01, "a click in the lane adds a point at that bar");
        // browse: a click auditions the file through the main output
        se->showPage (5);
        auto* br = dynamic_cast<BrowsePage*> (se->pageComponent (5));
        s.stop(); pump (60);
        const float silent = pump (8);
        br->clickFile (tf);
        check (s.previewing(), "a click on a file auditions it");
        const float heard = pump (20);
        std::cout << "  audition: stopped " << db (silent) << ", with the file " << db (heard) << "\n";
        check (heard > 0.05f && silent < heard * 0.2f, "the audition is heard on the main output while stopped");
        br->clickFile (tf); check (! s.previewing(), "a second click stops it");
        br->clickFile (tf); pump (200); check (! s.previewing(), "it ends with the file");
        // mix: bounce a track to audio and to a sampler; the master's bounce renders the project
        se->showPage (3);
        auto* mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        s.loopOn.store (true); s.loopStart.store (0); s.loopEnd.store (2);
        const int n0 = (int) s.tracks.size();
        mix->stripFor (*s.tracks[0])->bounceTo (1);
        check ((int) s.tracks.size() == n0 + 1 && s.tracks[1]->isAudio() && s.tracks[1]->aclips->size() == 1 && std::abs ((*s.tracks[1]->aclips)[0].bars - 2.0) < 1.0e-9, "bounce: the kick as a two-bar clip on a new audio track next to it");
        se->tickForTest(); mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        mix->stripFor (*s.tracks[0])->bounceTo (2);
        check ((int) s.tracks.size() == n0 + 2 && ! s.tracks[1]->isAudio() && s.tracks[1]->instrument && s.tracks[1]->instrument->typeId == "st:sampler", "bounce to a sampler track");
        if (s.tracks[1]->instrument) { auto* h = dynamic_cast<SampleHolder*> (s.tracks[1]->instrument->processor()); check (h && h->sampleFile().isNotEmpty() && s.tracks[1]->patternAt (0) && s.tracks[1]->patternAt (0)->s[3].slice == 3, "the sampler holds the bounce, the steps play its slices"); }
        se->tickForTest(); mix = dynamic_cast<MixPage*> (se->pageComponent (3));
        const int n1 = (int) s.tracks.size();
        mix->stripFor (*s.master)->bounceTo (1);
        check ((int) s.tracks.size() == n1 + 1 && s.tracks.back()->name.startsWith ("resample") && s.tracks.back()->aclips->size() == 1, "the master strip renders the project into a new audio track");
        // the file menu: the same from there, for the loop
        const int n2 = (int) s.tracks.size();
        bool hasRender = false; { auto m = se->fileMenu(); juce::PopupMenu::MenuItemIterator it (m, true); while (it.next()) if (it.getItem().itemID == 30) hasRender = it.getItem().text.contains ("render the project"); }
        check (hasRender, "the file menu has: render the project into a new audio track");
        se->fileAction (30);
        check ((int) s.tracks.size() == n2 + 1 && std::abs ((*s.tracks.back()->aclips)[0].bars - 2.0) < 1.0e-9 && (*s.tracks.back()->aclips)[0].startBar == 0.0, "file > render: the loop, as a clip at the loop start");
        s.loopOn.store (false);
        // keys: m mutes the selected track; a learned key comes first
        se->selectTrack (s.tracks[0].get());
        se->key (juce::KeyPress ('m', juce::ModifierKeys(), 'm')); check (s.tracks[0]->muted(), "m mutes the selected track");
        se->key (juce::KeyPress ('m', juce::ModifierKeys(), 'm')); check (! s.tracks[0]->muted(), "and unmutes");
        // auto master: one click, ms0 listens while the track plays and sets itself
        if (! kDeep)
        {
            Device* ms = nullptr; for (auto& d : s.master->inserts) if (d->typeId == "fal:ms0") ms = d.get();
            check (ms && ms->bypass.load(), "ms0 is on the master, bypassed");
            juce::StringArray beforeVals; if (ms) for (int i = 0; i < ms->numParams(); ++i) beforeVals.add (juce::String (ms->baseValue (i), 4));
            s.launchScene (4);
            check (se->autoMaster(), "auto master starts");
            check (ms && ! ms->bypass.load() && s.running.load(), "ms0 is switched in and the transport runs");
            auto* fp = ms ? dynamic_cast<fal::Processor*> (ms->processor()) : nullptr;
            int blocks = 0; while (fp && (fp->autoListening() || fp->autoReady()) && blocks < 4000) { pump (8); blocks += 8; }
            juce::StringArray afterVals; if (ms) for (int i = 0; i < ms->numParams(); ++i) afterVals.add (juce::String (ms->baseValue (i), 4));
            int changed = 0; for (int i = 0; i < beforeVals.size(); ++i) if (beforeVals[i] != afterVals[i]) ++changed;
            std::cout << "  auto master: listened for " << juce::String (blocks * kBs / kSr, 1) << " s, changed " << changed << " of " << beforeVals.size() << " ms0 parameters: " << (fp ? fp->autoSummary : juce::String()) << "\n";
            check (fp && ! fp->autoListening() && changed >= 3 && fp->autoSummary.isNotEmpty(), "ms0 set itself from what it heard");
            if (ms && fp) { bool same = true; for (int i = 0; i < ms->numParams(); ++i) same &= std::abs (ms->baseValue (i) - fp->getParameters()[i]->getValue()) < 1.0e-5f; check (same, "the studio's values and ms0's agree after auto"); }
            s.stop();
        }
        // bounce to sampler plays
        s.stop(); pump (30);
    }

    // ---- 20. templates, fm morph, the suite's own plug-ins ------------------------------------------------------
    std::cout << "== templates + devices 2\n";
    {
        StudioProcessor p; auto& s = p.session;
        check (Session::templateNames().size() == 9, "nine templates");
        s.newFromTemplate (5);
        check (std::abs (s.tempo() - 172.0f) < 0.1f && s.tracks.size() == 8 && s.tracks[7]->inserts.size() == 1 && s.tracks[7]->keyTrack.load() == s.tracks[0]->uid, "bass first: 172 bpm, the bass ducked by the kick");
        s.newFromTemplate (6);
        check (std::abs (s.tempo() - 132.0f) < 0.1f && s.swing() > 0.6f && s.tracks.size() == 9 && s.tracks[8]->instrument && s.tracks[8]->instrument->typeId == "st:sampler", "two-step: 132 bpm, swung, a sampler for the vocal");
        s.newFromTemplate (7);
        check (s.tracks.size() == 5 && s.tracks[0]->width.load() == 16 && ! s.tracks[0]->toMaster.load() && s.tracks[0]->routes->size() == 2 && s.tracks[3]->width.load() == 24 && s.tracks[4]->outWidth() == 2 && s.master->width.load() == 2, "spatial template: three encoders, a 24-speaker decoder, a binaural track");
        s.launchScene (0);
        auto rs = render (p, 2.0);
        const float l = rs.out.getRMSLevel (0, 0, rs.out.getNumSamples()), r = rs.out.getRMSLevel (1, 0, rs.out.getNumSamples());
        float d = 0; for (int i = 0; i < rs.out.getNumSamples(); ++i) d = std::max (d, std::abs (rs.out.getSample (0, i) - rs.out.getSample (1, i)));
        std::cout << "  spatial template on headphones: L " << db (l) << "  R " << db (r) << "  L-R " << db (d) << "\n";
        check (l > 1.0e-3f && r > 1.0e-3f && d > 1.0e-3f, "binaural stereo with a real image");
        s.tracks[3]->mixer->setBase (MP_MUTE, 0.0f); s.tracks[4]->mixer->setBase (MP_MUTE, 1.0f); s.setWidth (*s.master, 24);
        auto r24 = render (p, 2.0); int lit = 0; for (int c = 0; c < 24; ++c) if (r24.peak[(size_t) c] > 1.0e-4f) ++lit;
        check (lit >= 20, "the same session on 24 speakers");
        // fm perc: presets, and the morph towards one
        s.newFromTemplate (0);
        auto fm = s.createDevice ("st:fm");
        check (fm && fm->numPrograms() >= 8 && fm->programName (3) == "bell", "fm perc has presets");
        auto play = [&] { fm->prepare (kSr, kBs); juce::AudioBuffer<float> out (2, 16 * kBs); juce::MidiBuffer m; for (int b = 0; b < 16; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); fm->apply (nullptr, 0.0); fm->process (blk, m); out.copyFrom (0, b * kBs, blk, 0, 0, kBs); out.copyFrom (1, b * kBs, blk, 1, 0, kBs); } return out; };
        auto diffOf = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) { float d = 0; for (int i = 0; i < a.getNumSamples(); ++i) d = std::max (d, std::abs (a.getSample (0, i) - b.getSample (0, i))); return d; };
        fm->setProgram (1); auto kick = play();
        fm->setProgram (3); auto bell = play();
        fm->setProgram (1); fm->setBase (fm->paramIndexForId ("morphto"), 3.0f / 8.0f); fm->setBase (fm->paramIndexForId ("morph"), 1.0f); auto morphed = play();
        fm->setBase (fm->paramIndexForId ("morph"), 0.5f); auto half = play();
        std::cout << "  fm morph kick > bell: at 100% differs from the bell by " << diffOf (morphed, bell) << ", from the kick by " << diffOf (morphed, kick) << "; at 50% from both " << diffOf (half, kick) << " / " << diffOf (half, bell) << "\n";
        check (diffOf (morphed, bell) < 1.0e-4f && diffOf (morphed, kick) > 0.05f && diffOf (half, kick) > 0.02f && diffOf (half, bell) > 0.02f, "fm preset morph");
        // the suite's plug-ins that are installed next to it are offered without a scan, and load
        const auto suite = Session::suitePlugins();
        std::cout << "  suite plug-ins found:"; for (auto& f : suite) std::cout << " " << f.getFileNameWithoutExtension(); std::cout << (suite.isEmpty() ? " none (not installed here)" : "") << "\n";
        for (auto& f : suite)
        {
            bool listed = false; for (auto& e : s.deviceTypes()) if (e.typeId == "suite:" + f.getFullPathName()) listed = true;
            check (listed, f.getFileNameWithoutExtension() + " is in the device list");
            juce::String err; auto d = s.createDevice ("suite:" + f.getFullPathName(), &err);
            check (d != nullptr, "hosts " + f.getFileNameWithoutExtension() + ": " + err);
            if (d) std::cout << "    " << d->name << ": " << d->numParams() << " parameters, " << d->inWidth << " in " << d->outWidth << " out, " << (d->isInstrument() ? "instrument" : "effect") << "\n";
        }
    }

    // ---- 21. what a new window opens on; state round trips ------------------------------------------------------
    std::cout << "== a new instance\n";
    {
        StudioProcessor a, b;
        check (a.session.save()->toString() == b.session.save()->toString(), "two new instances open on the same project, to the last value");
        check (a.session.projectName == Session::templateNames()[1], "a new instance opens on the starter");
        auto ra = render (a, 2.0); auto rb = render (b, 2.0);
        float md = 0; for (int i = 0; i < ra.out.getNumSamples(); ++i) md = std::max (md, std::abs (ra.out.getSample (0, i) - rb.out.getSample (0, i)) + std::abs (ra.out.getSample (1, i) - rb.out.getSample (1, i)));
        std::cout << "  two new instances, two bars: largest difference " << md << ", peak " << db (ra.peak[0]) << "\n";
        check (md < 1.0e-6f && ra.peak[0] > 0.05f, "and sound the same, sample for sample");
        // the stereo image is a real one (hard-panned and decorrelated parts stay apart)
        double ll = 0, rr = 0, lr = 0; for (int i = 0; i < ra.out.getNumSamples(); ++i) { const double l = ra.out.getSample (0, i), r = ra.out.getSample (1, i); ll += l * l; rr += r * r; lr += l * r; }
        const double corr = lr / std::sqrt (ll * rr + 1.0e-20);
        std::cout << "  left / right correlation of the starter " << corr << " (a centred kick and bass, the rest around them)\n";
        check (corr < 0.99995 && corr > 0.5, "the output is stereo, not two copies of one channel");
        // hard left and hard right stay apart all the way out
        b.session.tracks[0]->mixer->setBase (MP_PAN, 0.0f); b.session.tracks[2]->mixer->setBase (MP_PAN, 1.0f);
        for (int t : { 1, 3, 4, 5, 6, 7 }) b.session.tracks[(size_t) t]->mixer->setBase (MP_MUTE, 1.0f);
        auto rw = render (b, 2.0);
        ll = rr = lr = 0; for (int i = 0; i < rw.out.getNumSamples(); ++i) { const double l = rw.out.getSample (0, i), r = rw.out.getSample (1, i); ll += l * l; rr += r * r; lr += l * r; }
        std::cout << "  kick hard left, hat hard right: correlation " << lr / std::sqrt (ll * rr + 1.0e-20) << "\n";
        check (std::abs (lr / std::sqrt (ll * rr + 1.0e-20)) < 0.3, "hard-panned tracks stay decorrelated at the output");
        // the project as state: save while it has been playing (modulators mid-flight), a fresh instance, load:
        // every base value comes back, and from then on the state is the same bytes
        a.session.tracks[2]->mixer->setBase (MP_PAN, 0.9f); a.session.setMacro (1, 0.77f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        StudioProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
        juce::MemoryBlock mc; c.getStateInformation (mc);
        StudioProcessor c2; c2.setStateInformation (mc.getData(), (int) mc.getSize());
        juce::MemoryBlock mc2; c2.getStateInformation (mc2);
        int baseDiffs = 0, compared = 0;
        auto cmp = [&] (Track& x, Track& y) { if (x.inserts.size() != y.inserts.size() || (x.instrument == nullptr) != (y.instrument == nullptr)) { ++baseDiffs; return; }
            auto dev = [&] (Device& p1, Device& p2) { if (p1.numParams() != p2.numParams()) { ++baseDiffs; return; } for (int i = 0; i < p1.numParams(); ++i) { ++compared; if (std::abs (p1.baseValue (i) - p2.baseValue (i)) > 1.0e-6f) { if (baseDiffs < 3) std::cout << "    base differs: " << x.name << " / " << p1.name << " / " << p1.paramName (i) << "  " << p1.baseValue (i) << " vs " << p2.baseValue (i) << "\n"; ++baseDiffs; } } };
            dev (*x.mixer, *y.mixer); if (x.instrument) dev (*x.instrument, *y.instrument); for (size_t i = 0; i < x.inserts.size(); ++i) dev (*x.inserts[i], *y.inserts[i]); };
        for (size_t i = 0; i < a.session.tracks.size() && i < c.session.tracks.size(); ++i) cmp (*a.session.tracks[i], *c.session.tracks[i]);
        for (size_t i = 0; i < a.session.returns.size(); ++i) cmp (*a.session.returns[i], *c.session.returns[i]);
        cmp (*a.session.master, *c.session.master);
        std::cout << "  state of an instance that has been playing, into a fresh one: " << baseDiffs << " of " << compared << " base values differ\n";
        check (c.session.tracks.size() == a.session.tracks.size() && baseDiffs == 0, "every base value survives save / reload, modulated ones included");
        check (mc == mc2, "state out, into a fresh instance, out again: the same bytes");
        // a macro moves what it is mapped to
        c.session.setMacro (0, 1.0f);
        render (c, 0.25);
        auto* dl = c.session.returns[0]->inserts[0].get(); const int fb = dl->paramIndexForId ("feedback");
        check (std::abs (dl->finalValue (fb) - 0.9f) < 0.01f, "a macro moves its target (the dub feedback)");
    }

    // ---- 22. real ambisonic plug-ins, when they are installed (IEM, SPARTA) ----------------------------------
    std::cout << "== hosted ambisonic plug-ins\n";
    {
        juce::Array<juce::File> roots; juce::VST3PluginFormat vf; const auto sp = vf.getDefaultLocationsToSearch();
        auto find = [&] (const juce::String& name) -> juce::File { for (int i = 0; i < sp.getNumPaths(); ++i) { auto f = sp[i].getChildFile (name); if (f.exists()) return f; } return {}; };
        struct Set { const char* label; juce::String enc, dec, bin; };
        const Set sets[] = { { "IEM", "IEM 2/StereoEncoder.vst3", "IEM 2/SimpleDecoder.vst3", "IEM 2/BinauralDecoder.vst3" }, { "SPARTA", "sparta_ambiENC.vst3", "sparta_ambiDEC.vst3", "sparta_ambiBIN.vst3" } };
        int tried = 0;
        for (auto& set : sets)
        {
            const auto ef = find (set.enc), bf = find (set.bin), df = find (set.dec);
            if (! ef.exists() || ! bf.exists()) continue;
            ++tried;
            StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
            auto host = [&] (const juce::File& f) -> std::unique_ptr<Device>
            {
                juce::OwnedArray<juce::PluginDescription> found;
                for (auto* fmt : s.formats.getFormats()) if (fmt->getName() == "VST3" && fmt->fileMightContainThisPluginType (f.getFullPathName())) fmt->findAllTypesForFile (found, f.getFullPathName());
                if (found.isEmpty()) return nullptr;
                s.known.addType (*found[0]); juce::String err; return s.createHosted (*found[0], &err);
            };
            auto& encT = *s.tracks[0]; s.setInstrument (encT, s.createDevice ("fal:hh4")); s.setTempo (120.0f);
            s.editPattern (encT, 0, [] (Pattern& pt) { pt = Pattern(); for (int k = 0; k < 16; k += 2) pt.s[(size_t) k].on = true; }); s.launch (encT, 0);
            s.setWidth (encT, 16); encT.toMaster.store (false);
            auto ed = host (ef); check (ed != nullptr, juce::String (set.label) + " encoder loads");
            if (! ed) continue;
            auto* e = s.addInsert (encT, std::move (ed));
            std::cout << "  " << set.label << " encoder on a 16-channel track: " << e->inWidth << " in, " << e->outWidth << " out (" << e->numParams() << " parameters)\n";
            check (e->inWidth == 2 && e->outWidth == 16, juce::String (set.label) + " encoder: stereo in, 16 out");
            // turn the source to the left
            for (int i = 0; i < e->numParams(); ++i) { const auto n = e->paramName (i).toLowerCase(); if (n.contains ("azim") && ! n.contains ("mask") && ! n.contains ("lfo")) { e->setBase (i, 0.75f); break; } }
            auto* binT = s.addTrack ("binaural"); s.setWidth (*binT, 16);
            auto bd = host (bf); check (bd != nullptr, juce::String (set.label) + " binaural decoder loads");
            if (! bd) continue;
            auto* b = s.addInsert (*binT, std::move (bd));
            check (s.addRoute (encT, { binT->uid, 16, 0, 0, 1.0f }), "route over 16 channels");
            std::cout << "  " << set.label << " binaural decoder on the 16-channel track fed by the route: " << b->inWidth << " in, " << b->outWidth << " out\n";
            check (b->inWidth == 16 && b->outWidth == 2 && binT->outWidth() == 2, juce::String (set.label) + " binaural decoder: 16 in, 2 out");
            float m[4] = {}; float encHi = 0;
            auto r = render (p, 2.0, [&] (int, double) { for (int c = 0; c < 4; ++c) m[c] = std::max (m[c], encT.meter[(size_t) c].load()); for (int c = 4; c < 16; ++c) encHi = std::max (encHi, encT.meter[(size_t) c].load()); });
            const float l = r.out.getRMSLevel (0, 0, r.out.getNumSamples()), rr = r.out.getRMSLevel (1, 0, r.out.getNumSamples());
            std::cout << "    encoder track w y z x: " << db (m[0]) << " " << db (m[1]) << " " << db (m[2]) << " " << db (m[3]) << ", higher orders " << db (encHi) << "; master L " << db (l) << " R " << db (rr) << ", channel 3 " << db (r.peak[2]) << "\n";
            if (juce::String (set.label) == "SPARTA")
                std::cout << "    (SPARTA starts its codecs from a timer on the message thread, which this console tool does not run: only its bus layouts are checked here)\n";
            else
            {
                check (m[0] > 1.0e-4f && encHi > 1.0e-5f, juce::String (set.label) + ": the encoder fills the ambisonic channels");
                check (l > 1.0e-5f && rr > 1.0e-5f && std::abs (l - rr) > 0.02f * std::max (l, rr), juce::String (set.label) + ": binaural stereo on the master, an image off centre");
            }
            if (df.exists())
            {
                auto* decT = s.addTrack ("speakers"); s.setWidth (*decT, 24);
                if (auto dd = host (df))
                {
                    auto* d = s.addInsert (*decT, std::move (dd)); s.addRoute (encT, { decT->uid, 16, 0, 0, 1.0f });
                    std::cout << "  " << set.label << " decoder on a 24-channel track fed 16: " << d->inWidth << " in, " << d->outWidth << " out\n";
                    check (d->inWidth == 16 && d->outWidth == 24, juce::String (set.label) + " decoder reads the 16 routed channels and writes the track's 24");
                }
            }
            // the project reloads with the plug-ins and their widths
            auto x = s.save(); StudioProcessor q; for (auto& t : s.known.getTypes()) q.session.known.addType (t);
            juce::String e2; q.session.load (*x, &e2);
            check (q.session.tracks.size() >= 2 && q.session.tracks[0]->inserts.size() == 1 && q.session.tracks[0]->inserts[0]->outWidth == e->outWidth && q.session.tracks[1]->inserts.size() == 1 && q.session.tracks[1]->inserts[0]->inWidth == b->inWidth, juce::String (set.label) + " session reloads: " + e2);
        }
        if (tried == 0) std::cout << "  (no IEM or SPARTA plug-ins installed here: skipped)\n";
    }

    // ---- 23. methods taken from the artists the studio is made for: tools, devices, engine ---------------------
    std::cout << "== pattern tools\n";
    {
        using namespace fal::daw::tools;
        auto ons = [] (const Pattern& p) { juce::String s; for (int i = 0; i < p.length; ++i) s << (p.s[(size_t) i].on ? "x" : "."); return s; };
        Pattern p; p.length = 16; euclid (p, 5, 0);
        std::cout << "  euclid 5 over 16: " << ons (p) << "\n";
        check (ons (p) == "x...x..x..x..x..", "euclid");
        rotate (p, 1); check (ons (p) == ".x...x..x..x..x.", "rotate"); reverse (p); check (ons (p) == ".x..x..x..x...x.", "reverse");
        Pattern d = p; doubleLength (d); check (d.length == 32 && d.s[17].on == d.s[1].on, "double");
        // ratios: a triplet next to straight steps
        Pattern r; r.length = 16; r.s[0].on = r.s[2].on = true;
        check (ratios (r, "1 1 2/3 2/3 2/3 3/2") && r.length == 6 && std::abs (r.s[2].dur - 2.0f / 3.0f) < 1.0e-6f && std::abs (r.s[5].dur - 1.5f) < 1.0e-6f && std::abs (r.span() - 5.5) < 1.0e-5, "ratios: one step per number, the loop is their sum");
        check (! ratios (r, "nothing here"), "ratios refuses a text without numbers");
        Pattern a; a.length = 8; accelerate (a, 0.8f);
        check (std::abs (a.span() - 8.0) < 1.0e-4 && a.s[7].dur < a.s[0].dur * 0.25f && std::abs (a.s[1].dur / a.s[0].dur - 0.8f) < 1.0e-4f, "accelerate: a constant factor step to step, the loop keeps its length");
        evenTime (a); check (a.even(), "even");
        Pattern g; g.length = 4; for (int i = 0; i < 4; ++i) { g.s[(size_t) i].on = true; g.s[(size_t) i].vel = 0.3f + 0.2f * i; g.s[(size_t) i].micro = 0.2f; }
        strictGrid (g, 2, 0.25f); check (g.s[0].vel == 0.5f && g.s[3].vel == 1.0f && g.s[1].len == 0.25f && g.s[2].micro == 0.0f, "strict: two velocity levels, one note length, no micro-timing");
        // sieves
        juce::String members; for (int n = 0; n < 16; ++n) if (sieveHolds ("3@0 | 4@1", n)) members << n << " ";
        std::cout << "  sieve 3@0 | 4@1 holds: " << members << "\n";
        check (members == "0 1 3 5 6 9 12 13 15 ", "sieve: union of residue classes");
        check (sieveHolds ("3@0 & 2@0", 6) && ! sieveHolds ("3@0 & 2@0", 3) && sieveHolds ("!2@0", 5) && ! sieveHolds ("!2@0", 4), "sieve: intersection and complement");
        Pattern sv; sv.length = 16; check (sieveRhythm (sv, "3@0 | 4@1") && ons (sv) == "xx.x.xx..x..xx.x", "sieve as a rhythm");
        bool ok = true; sieveHolds ("hello", 0, &ok); check (! ok && ! sieveRhythm (sv, "hello"), "a sieve that cannot be read changes nothing");
        check (sievePitch (sv, "3@0 | 4@1") && sv.s[0].pitch == 0 && sv.s[1].pitch == 1 && sv.s[3].pitch == 3 && sv.s[5].pitch == 5 && sv.s[6].pitch == 6, "sieve as a scale for the hits");
        // the cloud: exponential gaps, the loop length kept, the same seed gives the same cloud
        Rng r1; r1.seed (99); Rng r2; r2.seed (99); Pattern c1, c2; c1.length = c2.length = 32;
        stochastic (c1, r1, 1.0f, 12); stochastic (c2, r2, 1.0f, 12);
        float lo = 1e9f, hi = 0; bool same = true, inRange = true; double mean = 0;
        for (int i = 0; i < 32; ++i) { lo = std::min (lo, c1.s[(size_t) i].dur); hi = std::max (hi, c1.s[(size_t) i].dur); same &= c1.s[(size_t) i].dur == c2.s[(size_t) i].dur && c1.s[(size_t) i].pitch == c2.s[(size_t) i].pitch; inRange &= std::abs (c1.s[(size_t) i].pitch) <= 12; mean += c1.s[(size_t) i].dur; }
        int shortOnes = 0; for (int i = 0; i < 32; ++i) if (c1.s[(size_t) i].dur < 1.0f) ++shortOnes;
        std::cout << "  cloud of 32 events: step times " << lo << " ... " << hi << ", " << shortOnes << " shorter than the mean (an exponential law gives about 20)\n";
        check (std::abs (c1.span() - 32.0) < 1.0e-3 && same && inRange && hi > lo * 4.0f && shortOnes >= 16 && shortOnes <= 26, "stochastic cloud");
        Rng m; m.seed (5); Pattern mk; mk.length = 16; markov (mk, m); Pattern mu = mk; mutate (mu, m, 0.5f); int changed = 0; for (int i = 0; i < 16; ++i) if (mu.s[(size_t) i].on != mk.s[(size_t) i].on) ++changed;
        check (changed >= 3 && changed <= 13, "mutate flips some steps");
        Pattern rw; rw.length = 4; rw.s[0].on = rw.s[1].on = true; rewrite (rw); check (rw.s[0].ratchet == 2 && rw.s[2].on, "rewrite: two hits ratchet, a hit before a rest echoes");
    }
    std::cout << "== evolving patterns, choke groups, chords, ratchet curves\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t); s.setTempo (120.0f); s.global->setBase (GP_SWING, 0.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); for (int k : { 0, 4, 8, 12 }) pt.s[(size_t) k].on = true; }); s.launch (t, 0);
        auto perBar = [&] (const juce::AudioBuffer<float>& b) { std::vector<int> v; const int bar = (int) (4.0 * spb (120)); auto on = onsets (b, 0, 0.03f, 1500); for (int k = 0; k * bar < b.getNumSamples(); ++k) { int c = 0; for (int o : on) if (o >= k * bar && o < (k + 1) * bar) ++c; v.push_back (c); } return v; };
        auto plain = perBar (render (p, 12.0).out);
        bool steady = true; for (int c : plain) steady &= c == 4;
        check (steady, "without evolve every bar is the written pattern");
        t.evolve.store (1.0f);
        auto e1 = perBar (render (p, 12.0).out), e2 = perBar (render (p, 12.0).out);
        std::cout << "  evolve 100%, hits per bar:"; for (int c : e1) std::cout << " " << c; std::cout << "\n";
        int different = 0; for (int c : e1) if (c != 4) ++different;
        check (e1[0] == 4 && different >= 3, "evolve: the first pass is as written, later passes drift");
        check (e1 == e2, "and the drift is the same on every render");
        int near = 0; for (int c : e1) if (c >= 2 && c <= 9) ++near; check (near == (int) e1.size(), "it stays around the written pattern");
        // freeze writes the drift down
        juce::AudioBuffer<float> blk (kOutChannels, kBs); s.locate (0.0); s.play();
        for (int b = 0; b < 600; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); }
        bool anyFlip = false; for (int i = 0; i < 16; ++i) anyFlip |= t.flip[(size_t) i] != 0;
        const auto before = *t.patternAt (0); s.freezeEvolution (t); const auto after = *t.patternAt (0);
        int diff = 0; for (int i = 0; i < 16; ++i) if (before.s[(size_t) i].on != after.s[(size_t) i].on) ++diff;
        check (anyFlip == (diff > 0) && t.flip[3] == 0, "freeze: the drifted steps are in the pattern, the drift starts again");
        s.stop(); t.evolve.store (0.0f);
        auto x = s.save(); check (! x->toString().contains ("evolve="), "evolve off is not written"); t.evolve.store (0.4f); t.chokeGroup.store (2);
        StudioProcessor q; q.session.load (*s.save(), nullptr); check (std::abs (q.session.tracks[0]->evolve.load() - 0.4f) < 1.0e-6f && q.session.tracks[0]->chokeGroup.load() == 2, "evolve and choke group survive save / load");
        t.evolve.store (0.0f); t.chokeGroup.store (0);

        // choke: a long tone on one track, a kick on another; in one group the kick cuts the tone
        auto* tone = s.addTrack ("tone"); s.setInstrument (*tone, s.createDevice ("st:tone"));
        { auto* d = tone->instrument.get(); auto set = [&] (const char* id, float plain) { const int i = d->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d->processor()->getParameters()[i]); d->setBase (i, rp->convertTo0to1 (plain)); }; set ("duration", 1500.0f); set ("hz", 440.0f); set ("level", -6.0f); }
        s.editPattern (*tone, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; }); s.launch (*tone, 0);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[4].on = true; });
        s.multiOut.store (true); tone->outChannel.store (5); tone->toMaster.store (false);
        const int beat = (int) spb (120);
        auto free = render (p, 1.0);
        tone->chokeGroup.store (3); t.chokeGroup.store (3);
        auto cut = render (p, 1.0);
        const float fBefore = free.out.getRMSLevel (4, beat / 2, beat / 4), fAfter = free.out.getRMSLevel (4, beat + beat / 4, beat / 4), cBefore = cut.out.getRMSLevel (4, beat / 2, beat / 4), cAfter = cut.out.getRMSLevel (4, beat + beat / 4, beat / 4);
        std::cout << "  a tone, and a kick one beat later: the tone after the kick " << db (fAfter) << " ungrouped, " << db (cAfter) << " in one choke group (before the kick " << db (cBefore) << ")\n";
        check (fAfter > fBefore * 0.8f && cBefore > fBefore * 0.9f && cAfter < cBefore * 0.01f, "a hit on one track of a choke group cuts the other");
        const float click = [&] { float m = 0; for (int i = beat - 200; i < beat + 600; ++i) m = std::max (m, std::abs (cut.out.getSample (4, i) - cut.out.getSample (4, i - 1))); return m; }();
        check (click < 0.06f, "the cut is a short fade, not a click");
        auto again = render (p, 2.0);
        check (again.out.getRMSLevel (4, 4 * beat + beat / 2, beat / 4) > cBefore * 0.9f, "the cut track opens again on its own next hit");
        tone->chokeGroup.store (0); t.chokeGroup.store (0);
        // a chord on a step: the tone plays the note and the notes stacked on it
        { auto* d = tone->instrument.get(); d->setBase (d->paramIndexForId ("keytrack"), 1.0f); }
        s.editPattern (*tone, 0, [] (Pattern& pt) { pt.s[0].chord = 2; pt.s[0].len = 8.0f; });   // a fifth
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); });
        auto ch = render (p, 1.0);
        auto level = [&] (const juce::AudioBuffer<float>& b, int chn, double hz) { double re = 0, im = 0; const int N = 24000; for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (kTwoPi * i / (N - 1)); re += b.getSample (chn, 2000 + i) * w * std::cos (kTwoPi * hz * i / kSr); im += b.getSample (chn, 2000 + i) * w * std::sin (kTwoPi * hz * i / kSr); } return (float) (std::sqrt (re * re + im * im) / (N * 0.25)); };
        const double c4 = 261.6256, g4 = c4 * std::exp2 (7.0 / 12.0);
        std::cout << "  a step with the chord \"fifth\": " << db (level (ch.out, 4, c4)) << " at c, " << db (level (ch.out, 4, g4)) << " at g, " << db (level (ch.out, 4, c4 * std::exp2 (4.0 / 12.0))) << " at e\n";
        check (level (ch.out, 4, c4) > 0.1f && level (ch.out, 4, g4) > 0.1f && level (ch.out, 4, c4 * std::exp2 (4.0 / 12.0)) < 0.01f, "a chord on a step stacks its notes");
        // just tuning: the major third of a 5-limit scale is 5/4, not the equal one
        { auto* d = tone->instrument.get(); d->setBase (d->paramIndexForId ("tuning"), 1.0f / 3.0f); }
        s.editPattern (*tone, 0, [] (Pattern& pt) { pt.s[0].chord = 0; pt.s[0].pitch = 4; });
        auto ju = render (p, 1.0);
        const double third = [&] { int n = 0; const int from = 2000, len = 60000; for (int i = from + 1; i < from + len; ++i) if (ju.out.getSample (4, i - 1) < 0 && ju.out.getSample (4, i) >= 0) ++n; return n * kSr / (double) len; }();
        std::cout << "  the third in 5-limit tuning: " << third << " hz (5/4 above c is " << c4 * 1.25 << ", the equal third " << c4 * std::exp2 (4.0 / 12.0) << ")\n";
        check (std::abs (third - c4 * 1.25) < 0.7, "tuning: key-tracked notes in just intonation");
        { auto* d = tone->instrument.get(); d->setBase (d->paramIndexForId ("tuning"), 0.0f); d->setBase (d->paramIndexForId ("keytrack"), 0.0f); }
        // a pair of tones 40 hz apart, one per channel: nothing at 40 hz comes out of the device itself
        { auto* d = tone->instrument.get(); auto set = [&] (const char* id, float plain) { const int i = d->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d->processor()->getParameters()[i]); d->setBase (i, rp->convertTo0to1 (plain)); }; set ("hz", 1000.0f); set ("pair", 1.0f); set ("difference", 40.0f); set ("apart", 1.0f); }
        s.editPattern (*tone, 0, [] (Pattern& pt) { pt.s[0].pitch = 0; });
        auto pr = render (p, 1.0);
        std::cout << "  second tone 40 hz away, apart: left " << db (level (pr.out, 4, 1000.0)) << " at 1000 and " << db (level (pr.out, 4, 1040.0)) << " at 1040; right " << db (level (pr.out, 5, 1000.0)) << " and " << db (level (pr.out, 5, 1040.0)) << "; at 40 hz " << db (level (pr.out, 4, 40.0)) << "\n";
        check (level (pr.out, 4, 1000.0) > 0.2f && level (pr.out, 4, 1040.0) < 0.01f && level (pr.out, 5, 1040.0) > 0.2f && level (pr.out, 5, 1000.0) < 0.01f && level (pr.out, 4, 40.0) < 0.002f, "a pair of pure tones, one per channel, no difference tone in the signal");
        // ratchet curves: four hits in a step, even / speeding up / slowing down
        s.removeTrack (1); s.multiOut.store (false);
        t.speed.store (0);   // a long step (a whole beat) so the hits are well apart
        auto gaps = [&] (float curve) { s.editPattern (t, 0, [curve] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].ratchet = 4; pt.s[0].rcurve = curve; pt.s[0].vel = 1; }); auto on = onsets (render (p, 0.5).out, 0, 0.12f, 2500); std::vector<int> gp; for (size_t i = 1; i < on.size() && i < 4; ++i) gp.push_back (on[i] - on[i - 1]); return gp; };
        auto ge = gaps (0.0f), gu = gaps (0.3f), gd = gaps (-0.3f);
        std::cout << "  ratchet 4 in one beat, gaps: even"; for (int v : ge) std::cout << " " << v; std::cout << " · speeding up"; for (int v : gu) std::cout << " " << v; std::cout << " · slowing"; for (int v : gd) std::cout << " " << v; std::cout << "\n";
        check (ge.size() == 3 && std::abs (ge[0] - ge[2]) <= 2 && gu.size() == 3 && gu[0] > gu[1] + 1000 && gu[1] > gu[2] + 1000 && gd.size() == 3 && gd[0] + 1000 < gd[1] && gd[1] + 1000 < gd[2], "ratchet curve");
        t.speed.store (3);
        // the bounce modulator: four values that keep their order and do not repeat
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; });
        std::vector<float> v0, v3;
        { Modulator m0; m0.src = MOD_FOURSES; m0.sync = false; m0.hz = 3.0f; m0.depth = 1.0f; m0.polarity = 1; m0.macro = 0; m0.device = 0; m0.param = MP_PAN; s.addModulator (t, m0);
          Modulator m3 = m0; m3.macro = 3; m3.param = MP_SEND1; s.addModulator (t, m3); t.mixer->setBase (MP_PAN, 0.0f); t.mixer->setBase (MP_SEND1, 0.0f); }
        render (p, 8.0, [&] (int, double) { v0.push_back (t.mixer->finalValue (MP_PAN)); v3.push_back (t.mixer->finalValue (MP_SEND1)); });
        bool ordered = true; float lo0 = 1, hi0 = 0, lo3 = 1, hi3 = 0; for (size_t i = 0; i < v0.size(); ++i) { ordered &= v0[i] <= v3[i] + 1.0e-4f; lo0 = std::min (lo0, v0[i]); hi0 = std::max (hi0, v0[i]); lo3 = std::min (lo3, v3[i]); hi3 = std::max (hi3, v3[i]); }
        // does the second half repeat the first at any lag? (a plain lfo would)
        double bestErr = 1e9; const size_t half = v0.size() / 2;
        for (size_t lag = 40; lag < half; ++lag) { double e = 0; size_t cnt = 0; for (size_t i = 0; i + lag < v0.size() && cnt < 400; ++i, ++cnt) e += std::abs (v0[i] - v0[i + lag]); bestErr = std::min (bestErr, e / (double) std::max<size_t> (1, cnt)); }
        std::cout << "  bounce: the lowest of the four moves over " << lo0 << " ... " << hi0 << ", the highest " << lo3 << " ... " << hi3 << "; closest self-repeat differs by " << bestErr << " on average\n";
        check (ordered && hi0 - lo0 > 0.1f && hi3 - lo3 > 0.1f && bestErr > 0.04 * (hi0 - lo0), "bounce: four related values, each inside its neighbours, never repeating");
        s.editMods (t, [] (std::vector<Modulator>& v) { v.clear(); });
        // the tempo on a lane
        s.editLane ({ 0, 0, GP_TEMPO }, [] (AutoLane& l) { l.pts = { { 0.0, tempoToNorm (100.0f), 0.0f }, { 16.0, tempoToNorm (160.0f), 0.0f } }; });
        float t0 = 0, t1 = 0; render (p, 2.0, [&] (int b, double) { if (b == 1) t0 = s.tempo(); t1 = s.tempo(); });
        check (t0 < 104.0f && t1 > 120.0f, "the tempo follows an automation lane");
        s.removeLane ({ 0, 0, GP_TEMPO });
        // midi clock from the standalone: 24 to the beat, a start and a stop
        int clocks = 0, starts = 0, stops = 0;
        p.midiClockOut = [&] (const juce::MidiMessage& m) { if (m.isMidiClock()) ++clocks; if (m.isMidiStart()) ++starts; if (m.isMidiStop()) ++stops; };
        s.setTempo (120.0f); render (p, 1.0);
        { juce::AudioBuffer<float> b2 (kOutChannels, kBs); juce::MidiBuffer m; b2.clear(); p.processBlock (b2, m); }
        p.midiClockOut = nullptr;
        std::cout << "  midi clock over one bar: " << clocks << " clocks, " << starts << " start, " << stops << " stop\n";
        check (std::abs (clocks - 96) <= 1 && starts == 1 && stops == 1, "midi clock out: 24 to the beat");
        s.newFromTemplate (8); check (std::abs (s.tempo() - 140.0f) < 0.1f && s.tracks.size() == 8, "the half-step template");
    }
    std::cout << "== skip, wear, line\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto setP = [] (Device& d, const char* id, float plain) { const int i = d.paramIndexForId (id); check (i >= 0, juce::String ("parameter ") + id); if (i < 0) return; auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d.processor()->getParameters()[i]); d.setBase (i, rp->convertTo0to1 (plain)); };
        // skip: a file whose samples say where they are (a slow ramp on a carrier), so jumps can be seen
        const int N = 96000; juce::AudioBuffer<float> file (2, N);
        for (int i = 0; i < N; ++i) { const float v = 0.5f * std::sin (kTwoPi * 220.0f * i / 48000.0f) * (0.3f + 0.7f * (float) i / N); file.setSample (0, i, v); file.setSample (1, i, v); }
        auto wf = tmp.getChildFile ("skip source.wav"); writeWav (wf, file);
        auto sk = s.createDevice ("st:skip"); check (sk != nullptr && sk->isInstrument(), "skip is an instrument");
        auto* h = dynamic_cast<SampleHolder*> (sk->processor()); check (h && h->setSampleFile (wf), "skip takes a file");
        auto playSkip = [&] (float intensity, int blocks = 90) { setP (*sk, "intensity", intensity); sk->prepare (kSr, kBs); juce::AudioBuffer<float> out (2, blocks * kBs); juce::MidiBuffer m;
            for (int b = 0; b < blocks; ++b) { juce::AudioBuffer<float> blk (2, kBs); blk.clear(); m.clear(); if (b == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); sk->apply (nullptr, 0.0); sk->process (blk, m); out.copyFrom (0, b * kBs, blk, 0, 0, kBs); out.copyFrom (1, b * kBs, blk, 1, 0, kBs); } return out; };
        auto clean = playSkip (0.0f);
        double num = 0, ea = 0, eb = 0; for (int i = 0; i < clean.getNumSamples() && i < N; ++i) { num += clean.getSample (0, i) * file.getSample (0, i); ea += clean.getSample (0, i) * clean.getSample (0, i); eb += file.getSample (0, i) * file.getSample (0, i); }
        std::cout << "  skip at intensity 0: correlation with the file " << num / std::sqrt (ea * eb + 1e-12) << "\n";
        check (num / std::sqrt (ea * eb + 1e-12) > 0.97, "an undamaged disc plays the file");
        auto broken = playSkip (0.85f), broken2 = playSkip (0.85f);
        double num2 = 0, ea2 = 0; float d12 = 0; for (int i = 0; i < broken.getNumSamples() && i < N; ++i) { num2 += broken.getSample (0, i) * file.getSample (0, i); ea2 += broken.getSample (0, i) * broken.getSample (0, i); d12 = std::max (d12, std::abs (broken.getSample (0, i) - broken2.getSample (0, i))); }
        // repeats: the same short stretch of the file comes round again right after itself
        int repeats = 0; { const int win = 400; for (int at = 2000; at + 14400 < broken.getNumSamples(); at += 1100) for (int lag = 1800; lag < 14000; lag += 3) { double e = 0, en = 0; for (int i = 0; i < win; ++i) { const double a = broken.getSample (0, at + i), b = broken.getSample (0, at + lag + i); e += (a - b) * (a - b); en += a * a + b * b; } if (en > 1.0e-3 && e / en < 1.0e-4) { ++repeats; break; } } }
        std::cout << "  skip at intensity 85%: correlation with the file " << num2 / std::sqrt (ea2 * eb + 1e-12) << ", stretches that come round again exactly: " << repeats << ", two renders differ by " << d12 << "\n";
        check (num2 / std::sqrt (ea2 * eb + 1e-12) < 0.6 && ea2 > 0.01 && repeats >= 3, "a damaged one repeats fragments and jumps");
        check (d12 < 1.0e-6f, "and breaks the same way on every render");
        // steps address places in the file: the note of slice 9 starts later in the ramp than the note of slice 1
        auto startLevel = [&] (int note) { setP (*sk, "intensity", 0.0f); sk->prepare (kSr, kBs); juce::AudioBuffer<float> blk (2, kBs); blk.clear(); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0); sk->apply (nullptr, 0.0); sk->process (blk, m); return blk.getMagnitude (0, 0, kBs); };
        check (startLevel (36 + 9) > startLevel (36 + 1) * 1.5f, "the steps' slices are places in the file");
        // wear: a loop recorded once, then left to go round
        auto we = s.createDevice ("st:wear"); check (we != nullptr, "wear");
        setP (*we, "sync", 0.0f); { juce::AudioBuffer<float> sil (2, kBs); sil.clear(); juce::MidiBuffer m; we->prepare (kSr, kBs); we->apply (nullptr, 0.0); we->process (sil, m); }
        auto runWear = [&] (float wear, float drops)
        {
            setP (*we, "time", 256.0f * 1000.0f / 48.0f / 1000.0f * 8.0f * 1.0f); setP (*we, "time", 512.0f * 16.0f / 48.0f);   // 16 blocks exactly
            setP (*we, "wear", wear); setP (*we, "dropouts", drops); setP (*we, "record", 1.0f); setP (*we, "mix", 1.0f);
            we->prepare (kSr, kBs); Rng nz; nz.seed (21); std::vector<float> passRms, passHf; juce::MidiBuffer m;
            for (int pass = 0; pass < 10; ++pass)
            {
                double e = 0, eh = 0; float prev = 0;
                for (int b = 0; b < 16; ++b)
                {
                    juce::AudioBuffer<float> blk (2, kBs); blk.clear();
                    if (pass == 0) for (int i = 0; i < kBs; ++i) { const float v = 0.4f * nz.bi(); blk.setSample (0, i, v); blk.setSample (1, i, v); }
                    if (pass == 1 && b == 0) setP (*we, "record", 0.0f);
                    we->apply (nullptr, 0.0); we->process (blk, m);
                    for (int i = 0; i < kBs; ++i) { const float v = blk.getSample (0, i); e += v * v; eh += (v - prev) * (v - prev); prev = v; }
                }
                passRms.push_back ((float) std::sqrt (e / (16.0 * kBs))); passHf.push_back ((float) std::sqrt (eh / (16.0 * kBs)));
            }
            return std::make_pair (passRms, passHf);
        };
        auto perfect = runWear (0.0f, 0.0f); auto worn = runWear (0.6f, 0.8f);
        std::cout << "  wear 0: the loop on passes 2 / 5 / 9: " << db (perfect.first[2]) << " " << db (perfect.first[5]) << " " << db (perfect.first[9]) << "\n";
        std::cout << "  wear 60%: " << db (worn.first[2]) << " " << db (worn.first[5]) << " " << db (worn.first[9]) << "; its highs " << db (worn.second[2]) << " " << db (worn.second[5]) << " " << db (worn.second[9]) << "\n";
        check (perfect.first[2] > 0.1f && std::abs (perfect.first[9] - perfect.first[2]) < perfect.first[2] * 0.001f, "without wear the loop goes round unchanged");
        check (worn.first[5] < worn.first[2] * 0.9f && worn.first[9] < worn.first[5] * 0.9f && worn.second[9] / worn.first[9] < worn.second[2] / worn.first[2] * 0.8f, "with wear every pass is quieter and duller than the one before");
        // line: one line of sound along eight speakers, there and back in one second each way
        auto li = s.createDevice ("st:line"); check (li != nullptr && li->configure (2, 8) == 8 && li->inWidth == 2, "line on an 8-channel track: 2 in, 8 out");
        check (li->configure (2, 24) == 24, "24 speakers on a 24-channel track"); li->configure (2, 8);
        setP (*li, "time", 1.0f); setP (*li, "legato", 1.0f); setP (*li, "rest", 0.0f);
        auto runLine = [&] (int blocks, std::function<void (int, const float*)> see) { li->prepare (kSr, kBs); juce::MidiBuffer m; for (int b = 0; b < blocks; ++b) { juce::AudioBuffer<float> blk (8, kBs); blk.clear(); for (int i = 0; i < kBs; ++i) blk.setSample (0, i, 0.5f); li->apply (nullptr, 0.0); li->process (blk, m); float pk[8]; for (int c = 0; c < 8; ++c) pk[c] = blk.getMagnitude (c, 0, kBs); see (b, pk); } };
        std::vector<int> loudest; float worstPower = 0;
        runLine (188, [&] (int, const float* pk) { int best = 0; float e = 0; for (int c = 0; c < 8; ++c) { if (pk[c] > pk[best]) best = c; e += pk[c] * pk[c]; } loudest.push_back (best); worstPower = std::max (worstPower, std::abs (e - 0.25f)); });
        std::cout << "  line, there and back over 8 speakers, the loudest every ~1/8 s:"; for (size_t i = 0; i < loudest.size(); i += 12) std::cout << " " << loudest[i] + 1; std::cout << "\n";
        check (loudest.front() == 0 && loudest[46] >= 3 && loudest[46] <= 4 && loudest[92] >= 6 && loudest[140] >= 3 && loudest[140] <= 4 && loudest[186] <= 1, "the line travels to the last speaker and back");
        check (worstPower < 0.06f, "equal power on the way");
        setP (*li, "sieve", 1.0f); bool odd = false; runLine (100, [&] (int, const float* pk) { odd |= pk[1] > 1.0e-6f || pk[3] > 1.0e-6f || pk[5] > 1.0e-6f || pk[7] > 1.0e-6f; });
        check (! odd, "a sieve leaves speakers out");
        setP (*li, "sieve", 0.0f); setP (*li, "legato", 0.0f); int two = 0; runLine (90, [&] (int, const float* pk) { int n = 0; for (int c = 0; c < 8; ++c) if (pk[c] > 0.05f) ++n; if (n > 1) ++two; });
        setP (*li, "legato", 1.0f); int melt = 0; runLine (90, [&] (int, const float* pk) { int n = 0; for (int c = 0; c < 8; ++c) if (pk[c] > 0.05f) ++n; if (n > 1) ++melt; });
        std::cout << "  blocks with two speakers sounding in one pass: " << two << " with legato 0, " << melt << " with legato 100%\n";
        check (two <= 16 && melt >= 60, "legato: from a switch between speakers to a melt");
        // on a track: a hat as a line through eight speakers, the master eight wide
        s.newFromTemplate (0); auto& t = *s.tracks[0]; s.setInstrument (t, s.createDevice ("fal:hh4")); s.setWidth (t, 8); s.setWidth (*s.master, 8);
        auto* ld = s.addInsert (t, s.createDevice ("st:line")); setP (*ld, "time", 0.5f); setP (*ld, "figure", 1.0f);
        check (ld->inWidth == 2 && ld->outWidth == 8 && t.outWidth() == 8, "line on the track: stereo in, the track's 8 out");
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); for (int k = 0; k < 16; ++k) pt.s[(size_t) k].on = true; }); s.launch (t, 0);
        auto rl = render (p, 2.0); int lit = 0; for (int c = 0; c < 8; ++c) if (rl.peak[(size_t) c] > 1.0e-3f) ++lit;
        check (lit == 8 && rl.peak[8] < 1.0e-9f, "the hats go round all eight speakers of the master");
        // the user's own spatializer, when it is installed: found without a scan, stereo in, 16 out on a 16-channel track
        for (auto& f : Session::suitePlugins())
        {
            if (f.getFileNameWithoutExtension() != "Leitner") continue;
            s.newFromTemplate (0); auto& lt = *s.tracks[0]; s.setInstrument (lt, s.createDevice ("fal:hh4")); s.setWidth (lt, 16); lt.toMaster.store (false);
            juce::String err; auto dv = s.createDevice ("suite:" + f.getFullPathName(), &err); check (dv != nullptr, "hosts the Leitner plug-in: " + err);
            if (! dv) break;
            auto* d = s.addInsert (lt, std::move (dv));
            auto* bin = s.addTrack ("binaural"); s.setWidth (*bin, 16); s.addInsert (*bin, s.createDevice ("st:binaural")); s.addRoute (lt, { bin->uid, 16, 0, 0, 1.0f });
            s.editPattern (lt, 0, [] (Pattern& pt) { pt = Pattern(); for (int k = 0; k < 16; ++k) pt.s[(size_t) k].on = true; }); s.launch (lt, 0);
            float hi = 0; auto r = render (p, 2.0, [&] (int, double) { for (int c = 4; c < 16; ++c) hi = std::max (hi, lt.meter[(size_t) c].load()); });
            std::cout << "  Leitner on a 16-channel track: " << d->inWidth << " in, " << d->outWidth << " out; higher-order channels " << db (hi) << "; through the binaural track L " << db (r.peak[0]) << " R " << db (r.peak[1]) << "\n";
            check (d->inWidth == 2 && d->outWidth == 16 && hi > 1.0e-5f && r.peak[0] > 1.0e-5f, "the Leitner plug-in encodes onto the 16-channel track and is heard through the binaural one");
        }
    }
    std::cout << "== arcs, capture, nudge: through the editor\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.projectFile = tmp.getChildFile ("arcs.fstudio"); s.projectName = "arcs";
        auto& t = *s.tracks[0]; s.setInstrument (t, s.createDevice ("st:tone")); s.setTempo (120.0f);
        auto* d = t.instrument.get(); const int hz = d->paramIndexForId ("hz");
        d->setBase (d->paramIndexForId ("gate"), 1.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].len = 16.0f; }); s.launch (t, 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); auto* se = dynamic_cast<StudioEditor*> (ed.get());
        se->selectTrack (&t); ParamMenu::act (s, t, *d, hz, ParamMenu::Lane); se->showPage (1);
        auto* arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
        // a drawn arc: shift-drag across the lane from low on the left to high on the right
        const auto aa = arr->autoArea(); const int y0 = aa.getY() + 26;
        auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
        const juce::Point<float> a ((float) arr->x (0.0) + 1.0f, (float) (y0 + 44)), b ((float) arr->x (1.0), (float) (y0 + 6));
        arr->mouseDown (juce::MouseEvent (src, a, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, arr, arr, now, a, now, 1, false));
        for (int k = 1; k <= 20; ++k) { const juce::Point<float> q (a.x + (b.x - a.x) * k / 20.0f, a.y + (b.y - a.y) * k / 20.0f); arr->mouseDrag (juce::MouseEvent (src, q, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, arr, arr, now, a, now, 1, true)); }
        arr->mouseUp (juce::MouseEvent (src, b, juce::ModifierKeys(), 0, 0, 0, 0, 0, arr, arr, now, a, now, 1, true));
        auto ln = s.lane (s.targetFor (t, *d, hz));
        std::cout << "  a freehand arc over one bar: " << (ln ? (int) ln->pts.size() : 0) << " points, from " << (ln && ! ln->pts.empty() ? ln->pts.front().value : -1.0f) << " to " << (ln && ! ln->pts.empty() ? ln->pts.back().value : -1.0f) << "\n";
        check (ln && ln->pts.size() >= 6 && ln->pts[1].value < 0.2f && ln->pts.back().value > 0.8f && ln->pts[4].value > ln->pts[2].value, "shift-drag draws an arc into the lane");
        s.editLane (s.targetFor (t, *d, hz), [] (AutoLane& l) { if (! l.pts.empty()) l.pts.erase (l.pts.begin()); });   // (the point the lane started with)
        // the arc is a glissando of the tone
        auto r = render (p, 1.0);
        auto zc = [&] (int from, int len) { int n = 0; for (int i = from + 1; i < from + len; ++i) if ((r.out.getSample (0, i - 1) < 0) != (r.out.getSample (0, i) < 0)) ++n; return n * kSr / (2.0 * len); };
        const double f1 = zc (6000, 6000), f2 = zc (48000, 6000), f3 = zc (84000, 6000);
        std::cout << "  the tone under the arc: " << f1 << " hz, " << f2 << " hz, " << f3 << " hz\n";
        check (f2 > f1 * 1.3 && f3 > f2 * 1.3, "the drawn arc is a glissando");
        // capture: everything played, into a new track, one click to start and one to stop
        s.removeLane (s.targetFor (t, *d, hz)); d->setBase (hz, 0.4f);
        juce::AudioBuffer<float> blk (kOutChannels, kBs);
        se->captureJam();
        check (s.isRecording() && s.running.load() && s.tracks.size() == 2 && s.tracks[1]->name == "jam" && s.tracks[1]->armed.load() && s.tracks[1]->inputSource.load() == 0, "capture: a new track records the master, the transport runs");
        for (int k = 0; k < 190; ++k) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); }
        se->captureJam();
        auto* jam = s.tracks.size() > 1 ? s.tracks[1].get() : nullptr;
        check (! s.isRecording() && jam && jam->aclips->size() == 1 && (*jam->aclips)[0].sample && (*jam->aclips)[0].sample->seconds() > 1.9 && (*jam->aclips)[0].sample->data.getMagnitude (0, (*jam->aclips)[0].sample->data.getNumSamples()) > 0.05f, "the take is a clip with what was played");
        check (jam && jam->muted() && ! jam->armed.load(), "it comes back muted and disarmed");
        s.stop();
        // nudge: the clip last touched moves by 5 ms, off the grid
        if (jam && jam->aclips->size() == 1)
        {
            se->selectTrack (jam); se->showPage (1); arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
            const double before = (*jam->aclips)[0].startBar;
            const juce::Point<float> at ((float) arr->x (before) + 6.0f, (float) (arr->lanesArea().getY() + 1 * arr->laneH + 12));
            arr->mouseDown (juce::MouseEvent (src, at, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, arr, arr, now, at, now, 1, false));
            arr->mouseUp (juce::MouseEvent (src, at, juce::ModifierKeys(), 0, 0, 0, 0, 0, arr, arr, now, at, now, 1, false));
            se->key (juce::KeyPress ('.', juce::ModifierKeys(), '.'));
            const double moved = ((*jam->aclips)[0].startBar - before) * 4.0 * 60.0 / s.tempo();
            std::cout << "  nudge: the clip moved by " << moved * 1000.0 << " ms\n";
            check (std::abs (moved - 0.005) < 1.0e-6, "nudging the clip last touched moves it 5 ms");
        }
    }

    // ---- 23b. the timeline: grid and positions, clip editing, time, zoom, the ui scale ----------------------------
    std::cout << "== timeline: grid, positions, editing\n";
    {
        namespace tl = fal::daw::timeline;
        // positions and times, written and read
        check (tl::formatPosition (0.0) == "1.1.1.0", "bar 1 is 1.1.1.0");
        check (tl::formatPosition (65.625) == "17.2.3.120", "65.625 beats is 17.2.3.120 (" + tl::formatPosition (65.625) + ")");
        check (tl::formatLength (8.0 + 1.0 + 0.25) == "2.1.1.0", "a length counts from 0");
        check (tl::formatTime (65.25) == "1:05.250" && tl::formatTime (3725.0) == "1:02:05.000", "times: " + tl::formatTime (65.25) + ", " + tl::formatTime (3725.0));
        auto at120 = [] (double secs) { return secs * 2.0; };
        check (std::abs (tl::parse ("17.2.3.120", false, at120) - 65.625) < 1.0e-12, "17.2.3.120 read back");
        check (std::abs (tl::parse ("17", false, at120) - 64.0) < 1.0e-12 && std::abs (tl::parse ("17 2", false, at120) - 65.0) < 1.0e-12, "a bar, a bar and a beat");
        check (std::abs (tl::parse ("2", true, at120) - 8.0) < 1.0e-12 && std::abs (tl::parse ("0.0.1", true, at120) - 0.25) < 1.0e-12, "lengths: two bars, a sixteenth");
        check (std::abs (tl::parse ("1:00", false, at120) - 120.0) < 1.0e-9 && std::abs (tl::parse ("250ms", false, at120) - 0.5) < 1.0e-9 && std::abs (tl::parse ("90s", false, at120) - 180.0) < 1.0e-9, "times typed: 1:00, 250ms, 90s");
        check (tl::parse ("x", false, at120) < 0 && tl::parse ("0.1.1", false, at120) < 0 && tl::parse ("", false, at120) < 0, "nonsense is refused");
        // the grid
        check (std::abs (tl::adaptiveBeats (100.0) - 0.25) < 1.0e-12 && std::abs (tl::adaptiveBeats (1.0) - 16.0) < 1.0e-12, "the adaptive grid follows the zoom (1/16 at 100 px a beat, 4 bars at 1)");
        check (std::abs (tl::adaptiveBeats (10000.0) - 1.0 / 128.0) < 1.0e-12, "fully zoomed in, a 1/512 note");
        check (std::abs (tl::snap (1.13, 0.25) - 1.25) < 1.0e-12 && std::abs (tl::snap (0.3, 1.0 / 3.0) - 1.0 / 3.0) < 1.0e-12 && std::abs (tl::snap (0.3, 1.0 / 7.0) - 2.0 / 7.0) < 1.0e-12, "snaps to straight, triplet and septuplet lines");
        bool hit = false;
        check (std::abs (tl::magnet (3.9, 1.0, { 4.03 }, 40.0, 7.0, &hit) - 4.03) < 1.0e-12 && hit, "the magnet catches an edge 5 px away");
        check (std::abs (tl::magnet (3.9, 1.0, { 4.03 }, 100.0, 7.0, &hit) - 4.0) < 1.0e-12 && ! hit, "but not one 13 px away: the grid then");

        // clips: split, trim, mute, through the engine
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t);
        s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; });   // beats 1 and 3
        s.mode.store ((int) PlayMode::Arrange);
        const double sb = spb (120);
        auto hitsAt = [&] (double bars) { std::vector<double> v; for (int o : onsets (render (p, bars).out, 0)) v.push_back (o / sb); return v; };
        auto same = [] (const std::vector<double>& a, std::vector<double> b) { if (a.size() != b.size()) return false; for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i] - b[i]) > 0.01) return false; return true; };
        auto str = [] (const std::vector<double>& v) { juce::String o; for (double x : v) o << juce::String (x, 2) << " "; return o.trim(); };
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 0.0, 2.0 } }; });
        const int id0 = (*t.clips)[0].id;
        check (id0 > 0, "a pattern clip has an id");
        auto whole = hitsAt (2.0);
        auto halves = s.splitClips ({ { t.uid, id0, false } }, 0.5);
        check (halves.size() == 1 && t.clips->size() == 2 && std::abs ((*t.clips)[1].offset - 2.0) < 1.0e-12, "split: the right half starts two beats into the pattern");
        auto split = hitsAt (2.0);
        std::cout << "  hits (beats): whole clip " << str (whole) << "; split at beat 3: " << str (split) << "\n";
        check (same (whole, { 0, 2, 4, 6 }) && same (split, whole), "a split clip plays exactly as before");
        s.setClipsMuted (halves, true);
        check (same (hitsAt (2.0), { 0 }), "the muted half is silent");
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 1.0, 1.0 } }; });
        const ClipRef one { t.uid, (*t.clips)[0].id, false };
        s.trimClipStart (one, 1.25);
        check (same (hitsAt (2.0), { 6 }), "trimmed from the left: the hits stay where they were (" + str (hitsAt (2.0)) + ")");
        s.trimClipStart (one, 0.5);
        auto wrapped = hitsAt (2.0);
        check (same (wrapped, { 2, 4, 6 }), "extended to the left: the loop wraps, as if it had been playing (" + str (wrapped) + ")");
        double st, len; s.clipSpan (one, st, len);
        check (std::abs (st - 0.5) < 1.0e-12 && std::abs (len - 1.5) < 1.0e-12, "start 0.5, length 1.5");

        // audio: trimmed from the left, the sound stays put
        juce::AudioBuffer<float> file (2, 48000); file.clear(); file.setSample (0, 4800, 0.9f); file.setSample (1, 4800, 0.9f);
        auto wf = tmp.getChildFile ("trim.wav"); writeWav (wf, file);
        auto* at = s.addTrack ("audio", -1, TrackKind::Audio);
        juce::String err;
        auto* ac = s.addAudioClip (*at, wf, 2.0, &err);
        const ClipRef aref { at->uid, ac ? ac->id : 0, true };
        s.editClips (t, [] (std::vector<Clip>& c) { c.clear(); });
        auto impulse = [&] { auto on = onsets (render (p, 4.0).out, 0, 0.5f, 100); return on.empty() ? -1 : on[0]; };
        const int before = impulse();
        s.trimClipStart (aref, 2.0 + 0.05 / 2.0);   // 50 ms (a bar is 2 s)
        const int after = impulse();
        std::cout << "  impulse at " << before << ", after trimming 50 ms from the left " << after << "\n";
        check (before > 0 && before == after && std::abs ((*at->aclips)[0].offset - 0.05) < 1.0e-9, "an audio clip trimmed from the left: the sound does not move, to the sample");
        s.trimClipStart (aref, 1.0);
        check (std::abs ((*at->aclips)[0].offset) < 1.0e-12 && std::abs ((*at->aclips)[0].startBar - 2.0) < 1.0e-9, "and stops at the start of the file");

        // copy, paste, duplicate, move, delete
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 0.0, 1.0 }, { 1, 2.0, 1.0 } }; });
        std::vector<ClipRef> two; for (auto& c : *t.clips) two.push_back ({ t.uid, c.id, false });
        auto dup = s.duplicateClips (two);
        check (dup.size() == 2 && t.clips->size() == 4 && std::abs ((*t.clips)[2].startBar - 3.0) < 1.0e-12 && std::abs ((*t.clips)[3].startBar - 5.0) < 1.0e-12, "duplicate: the pair again right after itself");
        auto board = s.copyClips (two);
        auto pasted = s.pasteClips (board, 10.0, &t);
        check (pasted.size() == 2 && s.clipSpan (pasted[1], st, len) && std::abs (st - 12.0) < 1.0e-12, "paste keeps the distance between the clips");
        auto onAudio = s.pasteClips (board, 20.0, at);   // a pattern clip does not land on an audio track: back to its own
        check (onAudio.size() == 2 && onAudio[0].track == t.uid, "a clip only lands on a track of its kind");
        s.moveClips (two, -5.0);
        check (s.clipSpan (two[0], st, len) && std::abs (st) < 1.0e-12 && s.clipSpan (two[1], st, len) && std::abs (st - 2.0) < 1.0e-12, "moving a block before the start stops it at the start");
        s.deleteClips (dup); s.deleteClips (pasted); s.deleteClips (onAudio);
        check (t.clips->size() == 2, "deleted");

        // time: inserted and removed, across clips, automation, markers and the loop
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 0.0, 4.0 }, { 0, 8.0, 4.0 } }; });
        s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { v.clear(); });
        s.editMarkers ([] (std::vector<Marker>& m) { m = { { "drop", 8.0 } }; });
        const Target lt = s.targetFor (t, *t.mixer, MP_LEVEL);
        s.editLane (lt, [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 48.0, 1.0f, 0.0f } }; });
        s.loopOn.store (true); s.loopStart.store (8.0); s.loopEnd.store (12.0);
        s.insertTime (2.0, 2.0);
        auto starts = [&] { std::vector<double> v; for (auto& c : *t.clips) v.push_back (c.startBar); return v; };
        auto ln = s.lane (lt);
        std::cout << "  after inserting 2 bars at bar 3: clips at " << str (starts()) << "; marker at " << (*s.markers)[0].bar << "\n";
        check (same (starts(), { 0, 4, 10 }) && std::abs ((*t.clips)[1].offset - 8.0) < 1.0e-12, "insert time: the clip across it is cut, the rest moves 2 bars");
        check (std::abs ((*s.markers)[0].bar - 10.0) < 1.0e-12 && std::abs (s.loopStart.load() - 10.0) < 1.0e-12 && std::abs (s.loopEnd.load() - 14.0) < 1.0e-12, "markers and the loop move too");
        check (ln && std::abs (ln->valueAt (56.0) - 1.0f) < 1.0e-5f && std::abs (ln->valueAt (12.0) - 1.0f / 6.0f) < 1.0e-5f, "automation: moved, and holding its value through the new bars");
        s.deleteTime (2.0, 4.0);
        ln = s.lane (lt);
        check (same (starts(), { 0, 2, 8 }) && std::abs ((*s.markers)[0].bar - 8.0) < 1.0e-12 && ln && std::abs (ln->valueAt (48.0) - 1.0f) < 1.0e-4f && std::abs (ln->valueAt (4.0) - 1.0f / 12.0f) < 1.0e-4f,
               "delete time puts it all back (" + str (starts()) + ")");
        s.loopOn.store (false);
        s.removeLane (lt);

        // the tempo map
        check (std::abs (s.secondsAt (8.0) - 4.0) < 1.0e-9 && std::abs (s.beatAt (4.0) - 8.0) < 1.0e-9, "120 bpm: 8 beats are 4 s");
        const Target tt { 0, 0, GP_TEMPO };
        s.editLane (tt, [] (AutoLane& l) { l.pts = { { 0.0, tempoToNorm (60.0f), 0.0f }, { 400.0, tempoToNorm (60.0f), 0.0f } }; });
        check (std::abs (s.secondsAt (8.0) - 8.0) < 1.0e-6 && std::abs (s.beatAt (8.0) - 8.0) < 1.0e-6, "with a tempo lane at 60 bpm, the map follows it");
        s.removeLane (tt);

        // save and reopen: offsets, mutes, ids, the grid, follow
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 0.0, 2.0 } }; });
        auto hv = s.splitClips ({ { t.uid, (*t.clips)[0].id, false } }, 1.0);
        s.setClipsMuted (hv, true);
        s.gridIndex.store (12); s.follow.store (false);
        const auto pf = tmp.getChildFile ("timeline.fstudio");
        check (p.saveProject (pf, err), "save: " + err);
        StudioProcessor p2; check (p2.loadProject (pf, err), "reopen: " + err);
        auto& t2 = *p2.session.tracks[0];
        check (t2.clips->size() == 2 && (*t2.clips)[1].muted && std::abs ((*t2.clips)[1].offset - 4.0) < 1.0e-12 && (*t2.clips)[1].id == hv[0].id, "clip offset, mute and id come back");
        check (p2.session.gridIndex.load() == 12 && ! p2.session.follow.load(), "the grid and follow come back");
    }
    std::cout << "== time signatures\n";
    {
        namespace tl = fal::daw::timeline;
        tl::Sig sg;
        check (tl::parseSig ("7/8", sg) && std::abs (sg.beats() - 3.5) < 1.0e-12 && sg.beatCount() == 7, "7/8 is 3.5 quarter notes, 7 beats");
        check (tl::parseSig ("2+2+3/8", sg) && sg.groups.size() == 3 && std::abs (sg.num - 7.0) < 1.0e-12 && sg.groupStart (4) && ! sg.groupStart (3) && sg.text() == "2+2+3/8", "2+2+3/8: groups");
        check (tl::parseSig ("3.5/4", sg) && std::abs (sg.beats() - 3.5) < 1.0e-12 && sg.text() == "3.5/4", "a fractional numerator");
        check (tl::parseSig ("4/3", sg) && std::abs (sg.beats() - 16.0 / 3.0) < 1.0e-12, "an irrational meter (4/3)");
        check (! tl::parseSig ("7/0", sg) && ! tl::parseSig ("x/8", sg) && ! tl::parseSig ("0/4", sg) && ! tl::parseSig ("7", sg), "nonsense is refused");
        std::vector<tl::Sig> cyc; check (tl::parseSigs ("3/4 5/8", cyc) && cyc.size() == 2, "a cycle");
        tl::Sig s78, s34, s58; tl::parseSig ("7/8", s78); tl::parseSig ("3/4", s34); tl::parseSig ("5/8", s58);
        tl::MeterMap m ({ { 2, { s78 } }, { 4, { s34, s58 } } });
        const double expect[] = { 0, 4, 8, 11.5, 15, 18, 20.5, 23.5, 26 };
        bool ok = true; juce::String got; for (int b = 0; b < 9; ++b) { got << m.barStart (b) << " "; ok = ok && std::abs (m.barStart (b) - expect[b]) < 1.0e-12; }
        check (ok, "bar lines: 4/4 4/4 7/8 7/8, then 3/4 5/8 in turn (" + got.trim() + ")");
        double st; check (m.barAt (12.0, &st) == 3 && std::abs (st - 11.5) < 1.0e-12 && m.barAt (20.49, &st) == 5 && m.barAt (20.5) == 6, "which bar a position is in");
        check (tl::formatPosition (12.0, m) == "4.2.1.0" && std::abs (tl::parse ("4.2", false, {}, m) - 12.0) < 1.0e-12, "positions in 7/8 count eighths: 4.2.1.0 (" + tl::formatPosition (12.0, m) + ")");
        check (std::abs (m.nextBarLine (15.0) - 18.0) < 1.0e-12, "the next bar line");
        int kind = -1; const double nb = m.nextBeat (15.0, &kind); int k2 = -1; const double nb2 = m.nextBeat (17.5, &k2);
        check (std::abs (nb - 16.0) < 1.0e-12 && kind == 0 && std::abs (nb2 - 18.0) < 1.0e-12 && k2 == 2, "beats: quarters in 3/4, the bar line after");
        check (std::abs (tl::snap (11.4, 1.0, m) - 11.5) < 1.0e-12 && std::abs (tl::snap (11.2, 1.0, m) - 11.0) < 1.0e-12, "a quarter grid in 7/8 starts again at each bar line");
        check (std::abs (tl::nextGridLine (8.0, 4.0, m) - 11.5) < 1.0e-12 && std::abs (tl::prevGridLine (11.5, 1.0, m) - 11.0) < 1.0e-12, "grid steps follow the meter (" + juce::String (tl::nextGridLine (8.0, 4.0, m)) + ", " + juce::String (tl::prevGridLine (11.5, 1.0, m)) + ")");
        check (std::abs (m.barsBetween (8.0, 15.0) - 2.0) < 1.0e-12, "two bars of 7/8 are 7 quarter notes");
        check (std::abs (tl::parse ("1", true, {}, m, 8.0) - 3.5) < 1.0e-12, "a bar's length from bar 3 is a 7/8 bar");

        // through the engine: the metronome in 7/8, a song in 7/8
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        while (! s.tracks.empty()) s.removeTrack (0);
        s.setTempo (120.0f);
        s.setMeterAt (0, { s78 });
        s.metronome.store (true);
        auto r = render (p, 2.0);   // 8 quarter notes: two bars of 7/8 and the start of a third
        auto on = onsets (r.out, 0, 0.05f, 2000);
        const double eighth = spb (120) * 0.5;
        bool even = on.size() == 16; for (size_t i = 0; even && i < on.size(); ++i) even = std::abs (on[i] - (double) i * eighth) <= 2;
        std::cout << "  7/8 at 120 bpm, 8 quarter notes: " << on.size() << " clicks (expected 16, an eighth apart)\n";
        check (even, "the metronome clicks the eighths of 7/8");
        const float accent = r.out.getMagnitude (0, on.size() > 7 ? on[7] : 0, 400), plain = r.out.getMagnitude (0, on.size() > 1 ? on[1] : 0, 400);
        check (on.size() > 7 && std::abs (on[7] - (int) (7 * eighth)) <= 2, "bar 2 starts after seven eighths");
        (void) accent; (void) plain;
        s.metronome.store (false);
        auto* t = s.addTrack ("kick"); s.setInstrument (*t, s.createDevice ("fal:bd1")); shortHit (*t);
        s.editPattern (*t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; });
        s.editScenes ([] (std::vector<Scene>& sc) { for (auto& x : sc) x.pattern.assign (1, 0); });
        s.editSong ([] (std::vector<SongEntry>& so) { so = { { 0, 1 }, { 0, 1 }, { 0, 1 } }; });
        s.mode.store ((int) PlayMode::Song);
        auto rs = render (p, 2.75); auto ons = onsets (rs.out, 0);
        std::cout << "  a song of three 7/8 bars: hits at"; for (int o : ons) std::cout << " " << o / spb (120); std::cout << " (quarters)\n";
        check (ons.size() == 3 && std::abs (ons[1] - 3.5 * spb (120)) <= 2 && std::abs (ons[2] - 7.0 * spb (120)) <= 2, "the song's scenes start on 7/8 bar lines");
        check (std::abs (s.songBars() * 4.0 - 10.5) < 1.0e-9, "the song is three bars of 7/8 long");
        // save and reopen
        s.setMeterAt (8, { s34, s58 });
        juce::String err; const auto f = tmp.getChildFile ("meter.fstudio");
        check (p.saveProject (f, err), "save: " + err);
        StudioProcessor p2; check (p2.loadProject (f, err), "reopen: " + err);
        const auto& m2 = p2.session.meterMap();
        check (m2.getChanges().size() == 2 && m2.sigOf (0).text() == "7/8" && m2.sigOf (9).text() == "5/8" && std::abs (m2.barStart (10) - (8 * 3.5 + 3 + 2.5)) < 1.0e-12, "the meter comes back");
    }
    std::cout << "== takes: recording over the loop, choosing, comping; clip names and colours\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        s.removeTrack (0); s.setTempo (120.0f);
        s.projectFile = tmp.getChildFile ("takes.fstudio"); s.projectName = "takes";
        auto* at = s.addTrack ("vox", -1, TrackKind::Audio);
        at->armed.store (true); at->inputSource.store (-2); at->inputChannel.store (1); at->inputCount.store (2); at->monitor.store ((int) Monitor::Off);
        p.standaloneInputLatency = 0;
        s.loopOn.store (true); s.loopStart.store (0.0); s.loopEnd.store (1.0);
        const int bar = (int) (4.0 * spb (120));
        s.startRecording();
        // the input says which pass it is: 0.1 the first time round, 0.2, 0.3
        render (p, 3.0, {}, 2, [bar] (juce::AudioBuffer<float>& b, int pos) { for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.1f * (float) (1 + (pos + i) / bar); b.setSample (0, i, v); b.setSample (1, i, v); } });
        s.stopRecording();
        std::cout << "  three passes of a one-bar loop: " << at->aclips->size() << " clips\n";
        check (at->aclips->size() == 3, "a take a pass");
        if (at->aclips->size() == 3)
        {
            std::vector<AudioClip> tk (at->aclips->begin(), at->aclips->end());
            std::sort (tk.begin(), tk.end(), [] (auto& a, auto& b) { return a.take < b.take; });
            bool shape = true;
            for (int k = 0; k < 3; ++k) shape = shape && tk[(size_t) k].takeGroup == tk[0].takeGroup && tk[(size_t) k].take == k + 1 && std::abs (tk[(size_t) k].startBar) < 1.0e-9 && std::abs (tk[(size_t) k].bars - 1.0) < 0.01 && tk[(size_t) k].muted == (k < 2);
            check (shape, "three takes, one group, each the loop's bar long, the last one playing");
            auto rec = readWav (juce::File (tk[0].path));
            bool right = true; for (int k = 0; k < 3; ++k) { const int at0 = (int) std::lround (tk[(size_t) k].offset * kSr) + 1000; right = right && at0 < rec.getNumSamples() && std::abs (rec.getSample (0, at0) - 0.1f * (k + 1)) < 1.0e-6f; }
            check (right, "each take starts where its pass is in the file");
            // playback: the last take; then take 1 chosen; then comped (take 1, then take 2 from half way)
            at->armed.store (false);
            auto level = [&] (double atBar) { auto r = render (p, 1.0); return r.out.getSample (0, (int) (atBar * bar)); };
            check (std::abs (level (0.25) - 0.3f) < 1.0e-3f, "the last take plays");
            const ClipRef t1 { at->uid, tk[0].id, true };
            check (s.takesOf (t1).size() == 3, "its takes: three");
            s.chooseTake (t1);
            check (std::abs (level (0.25) - 0.1f) < 1.0e-3f, "take 1 chosen: take 1 plays");
            s.splitClips (s.allClips(), 0.5);
            ClipRef secondHalfOf2 {};
            for (auto& c : *at->aclips) if (c.take == 2 && c.startBar > 0.4) secondHalfOf2 = { at->uid, c.id, true };
            s.chooseTake (secondHalfOf2);
            const float a = level (0.25), b = level (0.75);
            std::cout << "  comped: the first half " << a << ", the second " << b << "\n";
            check (std::abs (a - 0.1f) < 1.0e-3f && std::abs (b - 0.2f) < 1.0e-3f, "comping: take 1 then take 2 (split, and a take chosen for the second part)");
            // names and colours, saved and reopened
            s.setClipName (t1, "verse");
            s.setClipColour ({ t1 }, 0xff8ec3e6);
            juce::String err; check (p.saveProject (s.projectFile, err), "save: " + err);
            StudioProcessor p2; check (p2.loadProject (s.projectFile, err), "reopen: " + err);
            bool kept = false; int groups = 0;
            for (auto& c : *p2.session.tracks[0]->aclips) { if (c.name == "verse" && c.colour == 0xff8ec3e6) kept = true; if (c.takeGroup) ++groups; }
            check (kept && groups == (int) at->aclips->size(), "names, colours and takes come back");
        }
    }
    std::cout << "== automation points: selected, moved together, copied, pasted, scaled, removed\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        auto& t = *s.tracks[0];
        const Target lt = s.targetFor (t, *t.mixer, MP_LEVEL);
        s.editLane (lt, [] (AutoLane& l) { l.pts = { { 0.0, 0.5f, 0.0f }, { 4.0, 0.2f, 0.0f }, { 8.0, 0.8f, 0.0f }, { 12.0, 0.4f, 0.0f } }; });
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); auto* se = dynamic_cast<StudioEditor*> (ed.get());
        ed->setSize (1440, 900); se->selectTrack (&t); se->showPage (1);
        auto* arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
        arr->showBars (0.0, 4.0); s.gridIndex.store (3); s.snap.store (true);
        const auto now = juce::Time::getCurrentTime(); auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto ev = [&] (juce::Point<float> q, juce::ModifierKeys m, juce::Point<float> down, bool dragged) { return juce::MouseEvent (src, q, m, 0, 0, 0, 0, 0, arr, arr, now, down, now, 1, dragged); };
        int row = 0; { const auto tg = arr->laneTargets(); for (int i = 0; i < (int) tg.size(); ++i) if (tg[(size_t) i] == lt) row = i; }
        const float y0 = (float) (arr->autoArea().getY() + 26 + row * 52);
        auto yOf = [&] (float v) { return y0 + 46.0f - v * 42.0f; };
        // cmd-drag a box around the two middle points
        const juce::Point<float> a ((float) arr->xd (0.9), y0 + 2.0f), b ((float) arr->xd (2.1), y0 + 48.0f);
        arr->mouseDown (ev (a, juce::ModifierKeys::commandModifier | juce::ModifierKeys::leftButtonModifier, a, false));
        for (int k = 1; k <= 5; ++k) arr->mouseDrag (ev (a + (b - a) * (k / 5.0f), juce::ModifierKeys::commandModifier | juce::ModifierKeys::leftButtonModifier, a, true));
        arr->mouseUp (ev (b, juce::ModifierKeys(), a, true));
        check (arr->selPts.size() == 2 && arr->selLane == lt, "a box (cmd-drag) selects the points in it (" + juce::String (arr->selPts.size()) + ")");
        // drag one of them a bar later and up: both move
        const juce::Point<float> d0 ((float) arr->xd (1.0), yOf (0.2f)), d1 ((float) arr->xd (2.0), yOf (0.2f) - 4.2f);
        arr->mouseDown (ev (d0, juce::ModifierKeys::leftButtonModifier, d0, false));
        for (int k = 1; k <= 8; ++k) arr->mouseDrag (ev (d0 + (d1 - d0) * (k / 8.0f), juce::ModifierKeys::leftButtonModifier, d0, true));
        arr->mouseUp (ev (d1, juce::ModifierKeys(), d0, true));
        auto l = s.lane (lt);
        juce::String got; for (auto& pt : l->pts) got << pt.beat << ":" << juce::String (pt.value, 2) << " ";
        std::cout << "  after moving the pair a bar later and up: " << got << "\n";
        check (l->pts.size() == 4 && std::abs (l->pts[1].beat - 8.0) < 1.0e-9 && std::abs (l->pts[2].beat - 12.0) < 1.0e-9 && std::abs (l->pts[1].value - 0.3f) < 0.02f && std::abs (l->pts[2].value - 0.9f) < 0.02f,
               "the selected points moved together, on the grid, the same way in value");
        // copy, paste at the playhead
        s.locate (5.0); se->key (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 'c')); se->key (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 'v'));
        l = s.lane (lt);
        bool pasted = false; for (auto& pt : l->pts) if (std::abs (pt.beat - 20.0) < 1.0e-9) pasted = true;
        check (l->pts.size() == 6 && pasted, "cmd+c, cmd+v: the points again at the playhead (bar 6)");
        // scale, invert, delete (the pasted pair is selected now)
        arr->editSelectedPoints ([] (std::vector<AutoPoint>& pts, const std::vector<int>& k) { for (int i : k) pts[(size_t) i].value *= 0.5f; });
        l = s.lane (lt); float sum = 0; for (int k : arr->selPts) sum += l->pts[(size_t) k].value;
        check (std::abs (sum - 0.6f) < 0.03f, "scaled to half");
        se->key (juce::KeyPress (juce::KeyPress::backspaceKey));
        check (s.lane (lt)->pts.size() == 4 && arr->selPts.empty(), "delete: the selected points are gone, nothing else");
    }

    std::cout << "== timeline: through the editor (ui scale, zoom, select, drag, keys)\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        auto& t = *s.tracks[0];
        s.setTempo (120.0f);
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 0.0, 1.0 }, { 0, 4.0, 1.0 } }; });
        p.history.poll (false); p.history.poll (false);   // (the history starts here)
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        // the ui scale: the window keeps its size, the pages get more or less room
        ed->setSize (1440, 900);
        se->setUiScale (1.25f);
        std::cout << "  ui 125% in a 1440 x 900 window: drawn at " << se->effectiveScale() << ", the pages get " << se->pageComponent (0)->getParentComponent()->getWidth() << " x " << se->pageComponent (0)->getParentComponent()->getHeight() << "\n";
        check (std::abs (se->effectiveScale() - 1.2f) < 1.0e-4f && se->pageComponent (0)->getParentComponent()->getWidth() == 1200, "bigger: limited by the window, so everything still fits");
        ed->setSize (2400, 1500);
        check (std::abs (se->effectiveScale() - 1.25f) < 1.0e-4f && se->pageComponent (0)->getParentComponent()->getWidth() == 1920, "in a bigger window it gets the whole 125%");
        se->setUiScale (0.5f);
        check (se->pageComponent (0)->getParentComponent()->getWidth() == 4800 && se->pageComponent (0)->getParentComponent()->getHeight() == 3000, "smaller: more room for the timeline");
        se->key (juce::KeyPress ('0', juce::ModifierKeys::commandModifier, '0'));
        check (std::abs (se->getUiScale() - 1.0f) < 1.0e-6f, "cmd+0: actual size");
        se->key (juce::KeyPress ('=', juce::ModifierKeys::commandModifier, '='));
        check (se->getUiScale() > 1.05f, "cmd+=: bigger");
        se->setUiScale (1.0f); ed->setSize (1440, 900);

        se->showPage (1);
        auto* arr = dynamic_cast<ArrangePage*> (se->pageComponent (1));
        // zoom keeps the bar under the mouse where it is, from a thousand bars across down to a few samples a pixel
        arr->pxPerBar = 24.0; arr->scrollBar = 0;
        const double under = arr->bar (600);
        arr->zoomAt (8.0, 600);
        check (std::abs (arr->bar (600) - under) < 1.0e-9 && std::abs (arr->pxPerBar - 192.0) < 1.0e-9, "zoom about the mouse");
        arr->zoomAt (1.0e9, 600);
        const double samplesPerPx = 1.0 / arr->pxPerBar * 4.0 * spb (120);
        std::cout << "  fully zoomed in: " << samplesPerPx << " samples a pixel, grid " << fal::daw::timeline::gridName (s.gridIndex.load(), arr->pxPerBeat()) << "\n";
        check (samplesPerPx < 5.0, "fully zoomed in, a pixel is a few samples");
        arr->zoomToFit();
        check (arr->bar (arr->lanesArea().getRight() - 1) >= s.projectBars() - 1.0e-6, "fit: the whole project in view");
        arr->showBars (0.0, 8.0);
        s.gridIndex.store (3); s.snap.store (true);   // 1/4
        // drag the first clip: it lands on the grid (or on the playhead / other clip's edge nearby)
        const auto now = juce::Time::getCurrentTime(); auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto ev = [&] (juce::Point<float> q, juce::ModifierKeys m, juce::Point<float> down, bool dragged) { return juce::MouseEvent (src, q, m, 0, 0, 0, 0, 0, arr, arr, now, down, now, 1, dragged); };
        const float rowMid = (float) (arr->lanesArea().getY() + arr->laneH / 2);
        const juce::Point<float> a ((float) arr->xd (0.5), rowMid), b ((float) arr->xd (0.5 + 1.07), rowMid);
        s.locate (7.5);
        arr->mouseDown (ev (a, juce::ModifierKeys::leftButtonModifier, a, false));
        for (int k = 1; k <= 10; ++k) arr->mouseDrag (ev (a + (b - a) * (k / 10.0f), juce::ModifierKeys::leftButtonModifier, a, true));
        arr->mouseUp (ev (b, juce::ModifierKeys(), a, true));
        std::cout << "  dragged 1.07 bars with a 1/4 grid: the clip now starts at " << (*t.clips)[0].startBar << "\n";
        check (std::abs ((*t.clips)[0].startBar - 1.0) < 1.0e-9 && arr->sel.size() == 1, "a dragged clip lands on the grid, and is selected");
        const juce::Point<float> c2 ((float) arr->xd (1.5), rowMid), d2 ((float) arr->xd (1.5 + 0.1234), rowMid);
        arr->mouseDown (ev (c2, juce::ModifierKeys::leftButtonModifier, c2, false));
        for (int k = 1; k <= 10; ++k) arr->mouseDrag (ev (c2 + (d2 - c2) * (k / 10.0f), juce::ModifierKeys::commandModifier | juce::ModifierKeys::leftButtonModifier, c2, true));
        arr->mouseUp (ev (d2, juce::ModifierKeys(), c2, true));
        check (std::abs ((*t.clips)[0].startBar - (1.0 + (d2.x - c2.x) / arr->pxPerBar)) < 1.0e-6, "with cmd held it goes exactly where it is put");
        // a band across both clips selects them; cmd+d duplicates; delete removes
        arr->sel.clear();
        const juce::Point<float> ba ((float) arr->xd (0.2), (float) arr->lanesArea().getY() + 2.0f), bb ((float) arr->xd (6.0), rowMid + 4.0f);
        arr->mouseDown (ev (ba, juce::ModifierKeys::leftButtonModifier, ba, false));
        for (int k = 1; k <= 6; ++k) arr->mouseDrag (ev (ba + (bb - ba) * (k / 6.0f), juce::ModifierKeys::leftButtonModifier, ba, true));
        arr->mouseUp (ev (bb, juce::ModifierKeys(), ba, true));
        check (arr->sel.size() == 2, "a band selects the clips it touches (" + juce::String (arr->sel.size()) + ")");
        se->key (juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 'd'));
        check (t.clips->size() == 4 && arr->sel.size() == 2, "cmd+d: duplicated, the copies selected");
        se->key (juce::KeyPress (juce::KeyPress::backspaceKey));
        check (t.clips->size() == 2 && arr->sel.empty(), "delete: the copies are gone");
        // the arrow keys move the playhead along the grid, then the selection
        s.locate (0.0);
        se->key (juce::KeyPress (juce::KeyPress::rightKey));
        check (std::abs (s.currentBar() - 0.25) < 1.0e-9, "-> with nothing selected: the playhead to the next grid line");
        se->key (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 'a'));
        if (t.clips->empty()) s.editClips (t, [] (std::vector<Clip>& c) { c = { { 0, 1.0, 1.0 } }; });
        const double s0 = (*t.clips)[0].startBar;
        se->key (juce::KeyPress (juce::KeyPress::rightKey));
        check (std::abs ((*t.clips)[0].startBar - s0 - 0.25) < 1.0e-9, "-> with clips selected: they move by the grid");
        se->key (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0));
        check (std::abs ((*t.clips)[0].startBar - s0 - 0.25 + 1.0 / 3840.0) < 1.0e-9, "cmd+<- moves them by one tick");
        // the ruler: a click puts the playhead there, a drag sets the loop
        const float ry = (float) (arr->top + 8);
        const juce::Point<float> r1 ((float) arr->xd (3.0), ry), r2 ((float) arr->xd (5.0), ry);
        arr->mouseDown (ev (r1, juce::ModifierKeys::leftButtonModifier, r1, false)); arr->mouseUp (ev (r1, juce::ModifierKeys(), r1, false));
        check (std::abs (s.currentBar() - 3.0) < 1.0e-9, "a click on the ruler: the playhead");
        arr->mouseDown (ev (r1, juce::ModifierKeys::leftButtonModifier, r1, false));
        for (int k = 1; k <= 5; ++k) arr->mouseDrag (ev (r1 + (r2 - r1) * (k / 5.0f), juce::ModifierKeys::leftButtonModifier, r1, true));
        arr->mouseUp (ev (r2, juce::ModifierKeys(), r1, true));
        check (s.loopOn.load() && std::abs (s.loopStart.load() - 3.0) < 1.0e-9 && std::abs (s.loopEnd.load() - 5.0) < 1.0e-9, "a drag on the ruler: the loop");
        // the edit menu's undo history takes it all in
        p.history.poll (false);
        check (p.history.canUndo(), "the edits are undoable");
    }

    // ---- 24. screenshots ----------------------------------------------------------------------------------
    if (shotDir != juce::File())
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        {
            auto sdir = tmp.getChildFile ("samples"); sdir.createDirectory();
            juce::AudioBuffer<float> b (2, 48000); Rng r; r.seed (7);
            for (int i = 0; i < 48000; ++i) { const float e = std::exp (-i / 9000.0f); const float v = e * (0.6f * std::sin (kTwoPi * 55.0f * i / (float) kSr) + 0.1f * r.bi()); b.setSample (0, i, v); b.setSample (1, i, v); }
            for (auto* n : { "kick loop 124.wav", "field recording.wav", "vocal chop.wav", "rim one-shot.wav", "pad c minor.wav" }) writeWav (sdir.getChildFile (n), b);
            sdir.getChildFile ("one-shots").createDirectory(); sdir.getChildFile ("loops").createDirectory();
            s.browseDir = sdir;
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        juce::AudioBuffer<float> blk (kOutChannels, kBs);
        s.play();
        for (int b = 0; b < 200; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); if (b % 10 == 0 && se) se->tickForTest(); }
        const int pages = se ? se->numPages() : 1;
        for (int pg = 0; pg < pages; ++pg)
        {
            if (pg == 4 && se) { s.selectedTrack.store (2); se->tickForTest(); }
            if (se) { se->showPage (pg); for (int k = 0; k < 4; ++k) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); se->tickForTest(); } }
            s.resetCpuMeter(); if (se) se->tickForTest();   // (the pictures show the studio, not how busy this machine is)
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto f = shotDir.getChildFile ("sndstudio-" + juce::String (pg) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        }
        auto snap = [&] (juce::Component& c, const juce::String& name)
        {
            s.resetCpuMeter(); if (se) se->tickForTest();
            juce::Image img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            const auto f = shotDir.getChildFile (name + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        };
        auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); if (b % 6 == 0 && se) se->tickForTest(); } };
        if (se)
        {
            // the arrangement with a sample on the timeline and its lanes; a modulator's popup; a step with locks
            const auto sample = s.browseDir.getChildFile ("vocal chop.wav");
            auto* at = s.addTrack ("vocal", -1, TrackKind::Audio);
            s.addAudioClip (*at, sample, 8.0); s.addAudioClip (*at, sample, 16.0); s.addAudioClip (*at, sample, 17.0);
            s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { if (v.size() > 2) { v[1].bars = 1.0; v[2].reverse = true; v[2].pitch = -5.0f; } });
            s.addInsert (*at, s.createDevice ("fal:dl4")); s.addInsert (*at, s.createDevice ("st:vinyl"));
            s.editLane (s.targetFor (*at, *at->mixer, MP_LEVEL), [] (AutoLane& l) { l.pts = { { 0.0, 0.3f, 0.0f }, { 32.0, 0.8f, 0.5f }, { 64.0, 0.8f, 0.0f }, { 72.0, 0.2f, -0.5f }, { 96.0, 0.7f, 0.0f } }; });
            s.editLane (s.targetFor (*at, *at->mixer, MP_SEND1), [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 60.0, 0.0f, 0.0f }, { 64.0, 0.9f, 0.0f }, { 68.0, 0.0f, 0.0f } }; });
            s.mode.store ((int) PlayMode::Arrange);
            se->selectTrack (at); se->showPage (1); run (60); snap (*ed, "sndstudio-arrange");
            if (auto* arr = dynamic_cast<ArrangePage*> (se->pageComponent (1)))
            {
                // close in: two clips selected, a fade, a split, the fine grid and the time ruler
                s.editAudioClips (*at, [] (std::vector<AudioClip>& v) { if (v.size() > 1) { v[0].fadeIn = 0.25; v[1].fadeOut = 0.4; } });
                arr->sel.clear(); for (auto& c : *at->aclips) if (c.startBar >= 15.9) arr->sel.push_back ({ at->uid, c.id, true });
                s.editMarkers ([] (std::vector<Marker>& m) { m.push_back ({ "vocal in", 16.0 }); });
                s.gridIndex.store (12); arr->showBars (15.6, 18.4); arr->setTrackHeight (72); s.locate (16.75);
                arr->scrollY = std::max (0, s.indexOf (at) * arr->laneH - 3 * arr->laneH);
                if (at->aclips->size() > 1) { const ClipRef r { at->uid, (*at->aclips)[1].id, true }; s.setClipName (r, "verse (take 2)"); s.setClipColour ({ r }, 0xff8d9bd6); }
                arr->selLane = s.targetFor (*at, *at->mixer, MP_LEVEL); arr->selPts = { 1, 2 };
                run (20); snap (*ed, "sndstudio-arrange-zoom");
                arr->setTrackHeight (40); s.gridIndex.store (0); arr->sel.clear(); arr->selPts.clear();
                // time signatures: 7/8 from bar 17, a 2+2+3/8 3/4 cycle from bar 21
                { fal::daw::timeline::Sig a78, a7g, a34; fal::daw::timeline::parseSig ("7/8", a78); fal::daw::timeline::parseSig ("2+2+3/8", a7g); fal::daw::timeline::parseSig ("3/4", a34);
                  s.setMeterAt (16, { a78 }); s.setMeterAt (20, { a7g, a34 }); }
                arr->scrollY = 0; s.gridIndex.store (4); arr->showBars (14.0, 24.0); s.locate (17.2);
                run (20); snap (*ed, "sndstudio-meter");
                s.setMeter ({}); s.gridIndex.store (0);
                // the whole window bigger and smaller
                const auto w0 = ed->getWidth(), h0 = ed->getHeight();
                ed->setSize (1800, 1125); se->setUiScale (1.25f); arr->zoomToFit(); run (12); snap (*ed, "sndstudio-ui-125");
                ed->setSize (1440, 900); se->setUiScale (0.75f); arr->zoomToFit(); run (12); snap (*ed, "sndstudio-ui-75");
                ed->setSize (1440, 900); se->setUiScale (1.2f); run (12); snap (*ed, "sndstudio-ui-narrow");
                for (int pg : { 0, 2, 3, 4, 5 }) { se->showPage (pg); run (12); snap (*ed, "sndstudio-ui-narrow-" + juce::String (pg)); }
                se->showPage (1);
                se->setUiScale (1.0f); ed->setSize (w0, h0); run (6);
            }
            s.mode.store ((int) PlayMode::Session);
            auto& bass = *s.tracks[7];
            const int cut = bass.instrument->paramIndexForId ("cutoff"), res = bass.instrument->paramIndexForId ("res");
            s.editPattern (bass, bass.pattern.load(), [&] (Pattern& pt) { pt.s[3].locks[0] = { bass.instrument->uid, cut, 0.8f }; pt.s[3].locks[1] = { bass.instrument->uid, res, 0.7f }; pt.s[3].slide = true; pt.s[6].ratchet = 3; pt.s[10].locks[0] = { bass.instrument->uid, cut, 0.3f }; pt.s[10].prob = 0.5f; pt.s[13].cond = Cond::X2of2; pt.s[13].dur = 1.5f; pt.s[14].dur = 0.5f; });
            const int mid = ParamMenu::act (s, bass, *bass.instrument, cut, ParamMenu::Slewed);
            ParamMenu::act (s, bass, *bass.instrument, res, ParamMenu::Gate);
            se->selectTrack (&bass); se->showPage (2);
            if (auto* seq = dynamic_cast<SeqPage*> (se->pageComponent (2))) { seq->selectedStep = 3; seq->refresh(); }
            run (40); snap (*ed, "sndstudio-seq-locks");
            { ModPopup pop (s, bass, mid); pop.setSize (300, 400); pop.resized(); snap (pop, "sndstudio-modpopup"); }
            { const int st = ParamMenu::act (s, bass, *bass.instrument, cut, ParamMenu::Steps); ModPopup pop (s, bass, st); pop.setSize (300, 400); pop.resized(); snap (pop, "sndstudio-modpopup-steps"); s.editMods (bass, [st] (std::vector<Modulator>& v) { v.erase (std::remove_if (v.begin(), v.end(), [st] (const Modulator& x) { return x.id == st; }), v.end()); }); }
            se->showPage (4); run (20); snap (*ed, "sndstudio-mod");
            // multi-out: every track on its own channels
            s.assignOutputs(); se->selectTrack (s.tracks[0].get()); se->showPage (3); run (60); snap (*ed, "sndstudio-multiout");
            // the spatial session: encoders, 24 speakers, binaural
            s.stop(); s.newFromTemplate (7); s.launchScene (0); s.setWidth (*s.master, 24); s.tracks[3]->mixer->setBase (MP_MUTE, 0.0f);
            s.play(); se->selectTrack (s.tracks[1].get()); se->showPage (3); run (120); snap (*ed, "sndstudio-spatial");
            se->showPage (2); run (30); snap (*ed, "sndstudio-spatial-seq");
        }
        s.stop();
        std::cout << "  " << pages << " + 8 screenshots in " << shotDir.getFullPathName() << "\n";
    }

    // ---- 26. undo / redo, autosave and recovery, the cpu meter ---------------------------------------------------
    std::cout << "== undo / redo, autosave, recovery, cpu meter\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& h = p.history; h.reset();
        check (! h.canUndo() && ! h.canRedo(), "a new project has no history");
        const int before = (int) s.tracks.size();
        auto* t = s.tracks[0].get();
        s.editPattern (*t, 3, [] (Pattern& pt) { pt.s[5].on = true; pt.s[5].pitch = 7; });
        check (! h.poll (true), "no step while a mouse button is held (a drag is one step)");
        check (h.poll (false) && h.undoSteps() == 1, "an edit is one step");
        check (! h.poll (false), "nothing new: no step");
        s.addTrack ("extra"); h.poll (false);
        t->mixer->setBase (MP_LEVEL, 0.2f); s.dirty = true; h.poll (false);   // a fader moved (its gesture ended)
        check (h.undoSteps() == 3, "three steps");
        s.play();
        check (h.undo(), "undo");
        check (std::abs (s.tracks[0]->mixer->baseValue (MP_LEVEL) - 0.2f) > 1.0e-3f, "undo: the fader is back");
        check (s.running.load(), "undo leaves the transport running");
        s.stop();
        h.undo();
        check ((int) s.tracks.size() == before, "undo: the added track is gone");
        h.undo();
        check (! s.tracks[0]->patternAt (3) || ! s.tracks[0]->patternAt (3)->s[5].on, "undo: the step is off again");
        check (! h.canUndo() && h.redoSteps() == 3, "at the start of the history, three steps to redo");
        h.redo(); h.redo();
        check (s.tracks[0]->patternAt (3) && s.tracks[0]->patternAt (3)->s[5].on && s.tracks[0]->patternAt (3)->s[5].pitch == 7 && (int) s.tracks.size() == before + 1, "redo twice: the step and the track are back");
        s.editPattern (*s.tracks[1], 0, [] (Pattern& pt) { pt.length = 7; pt.s[0].on = true; });
        h.poll (false);
        check (! h.canRedo(), "a new edit clears redo");
        h.redo(); h.undo();
        check (! s.tracks[1]->patternAt (0) || s.tracks[1]->patternAt (0)->length != 7, "undo of the newest edit");
        s.newFromTemplate (1); h.poll (false);
        check (! h.canUndo(), "a new project starts a new history");

        // autosave → a crash → recovery (into a scratch autosave, never the user's own)
        p.autosaveOverride = tmp.getChildFile ("autosave test.fstudio");
        s.projectFile = tmp.getChildFile ("my song.fstudio"); s.projectName = "my song";
        s.tracks[2]->mixer->setBase (MP_PAN, 0.11f);
        p.autosave();
        check (p.autosaveFile().existsAsFile() && ! p.autosaveFile().getSiblingFile ("autosave.tmp").existsAsFile(), "autosave written whole");
        StudioProcessor q; q.autosaveOverride = p.autosaveFile();
        juce::String err;
        check (q.recoverAutosave (err), "recover: " + err);
        check (q.session.projectFile == s.projectFile && q.session.projectName == "my song", "the recovered project knows its file");
        check (q.session.dirty.load() && q.session.tracks.size() == s.tracks.size() && std::abs (q.session.tracks[2]->mixer->baseValue (MP_PAN) - 0.11f) < 1.0e-5f, "recovered, unsaved, with the last values");

        // the cpu meter: a known load reads as that load
        s.simulateLoad = 0.5f; s.resetCpuMeter();
        s.cpu.store (0.0f);
        render (p, 2.0);
        std::cout << "  simulated load 50%: the meter reads " << juce::roundToInt (s.cpu.load() * 100) << "%, peak " << juce::roundToInt (s.cpuPeak.load() * 100) << "%, overruns " << s.overruns.load() << "\n";
        check (s.cpu.load() > 0.48f && s.cpu.load() < 0.95f && s.cpuPeak.load() >= s.cpu.load(), "the meter reads the simulated load");
        s.simulateLoad = 0.0f;
        s.resetCpuMeter();   // (a busy machine may have pushed a block or two over: counted, but not what is checked here)
        s.noteLoad (2.0 * kBs / kSr, kBs);
        check (s.overruns.load() == 1 && s.cpuPeak.load() >= 1.99f, "a block that took twice its time counts as an overrun");
        s.resetCpuMeter();
        check (s.overruns.load() == 0 && s.cpuPeak.load() == 0.0f, "reset");
        if (shotDir != juce::File())
        {
            StudioEditor ed (p); ed.setSize (1440, 900);
            s.simulateLoad = 0.35f; s.newFromTemplate (1); s.launchScene (4); render (p, 1.0); s.simulateLoad = 0.0f; s.noteLoad (1.3 * kBs / kSr, kBs);
            ed.tickForTest();
            auto img = ed.createComponentSnapshot ({ 1440 - 420, 900 - 60, 420, 60 }, true, 2.0f);
            juce::FileOutputStream os (shotDir.getChildFile ("sndstudio-cpu.png")); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }

    // ---- 28. export options: file types, word lengths, rates, normalising, stems of chosen tracks, a time selection, multichannel
    std::cout << "== export options\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1); s.launchScene (4);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        auto info = [&] (const juce::File& f, int& ch, double& rate, int& bits, juce::int64& len, float& peak)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (! r) { ch = 0; return; }
            ch = (int) r->numChannels; rate = r->sampleRate; bits = (int) r->bitsPerSample; len = r->lengthInSamples;
            juce::AudioBuffer<float> b (ch, (int) len); r->read (&b, 0, (int) len, 0, true, true); peak = b.getMagnitude (0, (int) len);
        };
        juce::String err; int ch = 0, bits = 0; double rate = 0; juce::int64 len = 0; float peak = 0;
        ExportOptions o; o.format = 1; o.bits = 16; o.sampleRate = 44100; o.normalise = true; o.normaliseDb = -1.0f; o.tail = false;
        const auto aif = tmp.getChildFile ("mix.aif");
        check (s.renderMaster (aif, 0.0, 2.0, nullptr, err, o), "aiff export: " + err);
        info (aif, ch, rate, bits, len, peak);
        const double bars2 = 2.0 * 4.0 * 60.0 / s.tempo();
        std::cout << "  aiff: " << ch << " ch, " << rate << " hz, " << bits << " bit, " << len << " samples (" << len / rate << " s for " << bars2 << " s), peak " << db (peak) << "\n";
        check (ch == 2 && std::abs (rate - 44100.0) < 1 && bits == 16 && std::abs ((double) len / rate - bars2) < 0.01, "aiff, 16 bit, resampled to 44.1 khz, two bars long");
        check (std::abs (juce::Decibels::gainToDecibels (peak) + 1.0f) < 0.3f, "normalised to -1 dbfs");
        check (! aif.getSiblingFile ("mix.render.wav").exists(), "the float render is cleaned up");
        o = ExportOptions(); o.format = 2; o.bits = 24; o.tail = false;
        const auto flac = tmp.getChildFile ("mix.flac");
        check (s.renderMaster (flac, 0.0, 1.0, nullptr, err, o), "flac export: " + err);
        info (flac, ch, rate, bits, len, peak);
        check (ch == 2 && bits == 24 && std::abs (rate - kSr) < 1 && peak > 0.05f, "flac, 24 bit, the project's rate");
        o = ExportOptions(); o.bits = 32; o.tail = false;
        const auto wf = tmp.getChildFile ("mix32.wav");
        s.renderMaster (wf, 0.0, 1.0, nullptr, err, o); info (wf, ch, rate, bits, len, peak);
        check (bits == 32 && peak > 0.05f, "wav, 32-bit float");
        // a time selection: bars 3 to 4 only
        const auto sel = tmp.getChildFile ("selection.wav");
        s.renderMaster (sel, 2.0, 3.0, nullptr, err, o); info (sel, ch, rate, bits, len, peak);
        check (std::abs ((double) len - 4.0 * 60.0 / s.tempo() * kSr) < 2.0, "a time selection of one bar is one bar long");
        // stems of two chosen tracks
        const auto dir = tmp.getChildFile ("chosen stems");
        o.only = { s.tracks[0]->uid, s.tracks[2]->uid }; o.format = 0; o.bits = 24;
        check (s.renderStems (dir, 0.0, 1.0, nullptr, err, o), "stems of chosen tracks: " + err);
        const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
        std::cout << "  stems of tracks 1 and 3: "; for (auto& f : files) std::cout << "\"" << f.getFileName() << "\" "; std::cout << "\n";
        check (files.size() == 2, "exactly the two chosen tracks");
        // multichannel: the spatial template's 24-channel master as one file
        s.newFromTemplate (7); s.launchScene (0); s.setWidth (*s.master, 24); p.prepareToPlay (kSr, kBs);
        const auto mc = tmp.getChildFile ("spatial.wav");
        o = ExportOptions(); o.tail = false;
        check (s.renderMaster (mc, 0.0, 1.0, nullptr, err, o), "multichannel export: " + err);
        info (mc, ch, rate, bits, len, peak);
        std::cout << "  the spatial master: " << ch << " channels in one file\n";
        check (ch == 24, "a 24-channel master is one 24-channel file");
    }

    // ---- 29. recording from a midi keyboard into the steps, with a count-in and the metronome ---------------------
    std::cout << "== midi recording into steps, count-in, metronome\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        s.setTempo (120.0f); s.microMs.store (12.0f);
        auto* t = s.addTrack ("keys"); s.setInstrument (*t, s.createDevice ("st:tone"));
        t->rootNote.store (60); t->armed.store (true);
        s.editPattern (*t, 0, [] (Pattern& pt) { pt = Pattern(); });
        p.standaloneInputs = 0; p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.countInBars.store (1); s.metronome.store (false);
        s.locate (0.0); p.history.reset();
        s.recordWithCountIn();
        juce::AudioBuffer<float> blk (kOutChannels, kBs);
        const double spbeat = spb (120.0);   // samples per beat
        const int countInSamples = (int) std::llround (4 * spbeat);
        int pos = 0; float clickPeak = 0; bool startedEarly = false; int startedAt = -1;
        // the notes, in samples from the end of the count-in: e4 played 10 ms late on beat 2 (step 5), held one step;
        // g4 early by 5 ms on step 11, held two steps
        struct Ev { double at; int note; bool on; };
        const double stepS = spbeat / 4.0, ms = kSr / 1000.0;
        const std::vector<Ev> evs { { 4 * stepS + 10 * ms, 64, true }, { 5 * stepS + 10 * ms, 64, false }, { 10 * stepS - 5 * ms, 67, true }, { 12 * stepS - 5 * ms, 67, false } };
        for (int b = 0; b < 400; ++b, pos += kBs)
        {
            blk.clear(); juce::MidiBuffer m;
            for (auto& e : evs) { const int at = countInSamples + (int) std::llround (e.at); if (at >= pos && at < pos + kBs) m.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.9f) : juce::MidiMessage::noteOff (1, e.note), at - pos); }
            const bool runningBefore = s.running.load();
            p.processBlock (blk, m);
            if (pos + kBs <= countInSamples) { clickPeak = std::max (clickPeak, blk.getMagnitude (0, 0, kBs)); startedEarly |= runningBefore; }
            if (startedAt < 0 && s.running.load()) startedAt = pos + kBs;
            if (b % 20 == 0) s.collectRecordedNotes();
        }
        s.stopRecording(); s.stop();
        std::cout << "  count-in: clicks at " << db (clickPeak) << ", the transport started after " << startedAt << " samples (one bar = " << countInSamples << ")\n";
        check (clickPeak > 0.1f && ! startedEarly && std::abs (startedAt - countInSamples) <= kBs, "one bar of clicks, then the transport starts");
        auto pt = t->patternAt (0);
        check (pt != nullptr, "the take is in the pattern");
        if (pt)
        {
            const auto& a = pt->s[4]; const auto& b = pt->s[10];
            std::cout << "  step 5: " << (a.on ? "on" : "off") << " pitch " << a.pitch << " micro " << a.micro * 12.0f << " ms length " << a.len << "; step 11: " << (b.on ? "on" : "off") << " pitch " << b.pitch << " micro " << b.micro * 12.0f << " ms length " << b.len << "\n";
            check (a.on && a.pitch == 4 && std::abs (a.micro * 12.0f - 10.0f) < 1.5f && std::abs (a.len - 1.0f) < 0.1f, "e4 lands on step 5, 10 ms late, one step long");
            check (b.on && b.pitch == 7 && std::abs (b.micro * 12.0f + 5.0f) < 1.5f && std::abs (b.len - 2.0f) < 0.1f, "g4 lands on step 11, 5 ms early, two steps long");
            int on = 0; for (int i = 0; i < pt->length; ++i) on += pt->s[(size_t) i].on ? 1 : 0;
            check (on == 2, "nothing else written");
        }
        {
            const bool polled = p.history.poll (false); const bool undone = p.history.undo();
            Track* k = nullptr; for (auto& x : s.tracks) if (x->name == "keys") k = x.get();
            if (! (polled && undone)) std::cout << "    (history: polled " << (int) polled << ", undone " << (int) undone << ", steps " << p.history.undoSteps() << ")\n";
            const bool gone = k && (! k->patternAt (0) || ! k->patternAt (0)->s[4].on);
            t = nullptr;
            check (polled && undone && gone, "the take is one undo step: the keys track is back to an empty pattern");
        }
        // the metronome while playing
        s.metronome.store (true); s.newFromTemplate (0); s.metronome.store (true);
        auto r = render (p, 1.0);
        std::vector<int> on = onsets (r.out, 0, 0.05f, 4000);
        std::cout << "  metronome, one bar at " << s.tempo() << " bpm: " << on.size() << " clicks\n";
        check (on.size() == 4, "four clicks in a bar");
        s.metronome.store (false);
        auto r2 = render (p, 1.0);
        check (r2.peak[0] < 1.0e-4f, "and none with the metronome off");
    }

    // ---- 30. the pitch lane: scales and scala tunings; parts drifting out of phase ------------------------------
    std::cout << "== pitch lane: scales, scala tunings; phase\n";
    {
        // reading scala files
        juce::String err;
        auto sc = parseScala ("! test.scl\n!\nquarter tones and a 7/4\n 3\n!\n 150.0\n 7/4\n 2/1\n", "test", &err);
        check (sc && sc->size() == 3 && std::abs (sc->cents[1] - 150.0) < 1.0e-9 && std::abs (sc->cents[2] - 968.826) < 0.01 && std::abs (sc->period - 1200.0) < 1.0e-6 && sc->name == "quarter tones and a 7/4", "a scala file read: cents, ratios, the period");
        check (sc && std::abs (sc->centsOf (4) - 1350.0) < 1.0e-6 && std::abs (sc->centsOf (-1) - (968.826 - 1200.0)) < 0.01, "degrees wrap at the period, below the root too");
        check (sc && sc->microtonal() && ! builtinScale (0)->microtonal(), "microtonal told apart");
        check (! parseScala ("nothing\n5\n100.0\n", "x", &err) && err.isNotEmpty(), "a broken file is refused with a reason");
        check (builtinScale (0)->nearestDegree (400.0) == 2 && builtinScale (0)->nearestDegree (-100.0) == -1, "a played note finds its degree");
        // the sound: a tone keyed by the steps, degree 2 of major is e4; degree 1 of the scala file is c4 + 150 cents
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0); s.setTempo (120.0f);
        auto* t = s.addTrack ("lane"); s.setInstrument (*t, s.createDevice ("st:tone"));
        { auto* d = t->instrument.get(); auto set = [&] (const char* id, float plain) { const int i = d->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d->processor()->getParameters()[i]); d->setBase (i, rp->convertTo0to1 (plain)); }; set ("duration", 400.0f); set ("level", -6.0f); d->setBase (d->paramIndexForId ("keytrack"), 1.0f); }
        t->rootNote.store (60);
        auto hz = [&] (int degree) -> double
        {
            s.editPattern (*t, 0, [degree] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].pitch = degree; pt.s[0].len = 8.0f; });
            s.launch (*t, 0);
            auto r = render (p, 0.5);
            int cross = 0, first = -1, last = -1; const int from = 2400, to = 2400 + 9600;
            for (int i = from; i < to; ++i) if (r.out.getSample (0, i - 1) <= 0.0f && r.out.getSample (0, i) > 0.0f) { if (first < 0) first = i; last = i; ++cross; }
            return cross > 1 ? (cross - 1) * kSr / (double) (last - first) : 0.0;
        };
        s.setScale (*t, builtinScale (0));
        const double e4 = hz (2), c5 = hz (7), b3 = hz (-1);
        s.setScale (*t, sc);
        const double q = hz (1), sev = hz (2);
        s.setScale (*t, nullptr);
        const double semis = hz (4);
        std::cout << "  major: degree 2 " << juce::String (e4, 1) << " hz, 7 " << juce::String (c5, 1) << ", -1 " << juce::String (b3, 1) << "; scala: degree 1 " << juce::String (q, 1) << " hz, degree 2 " << juce::String (sev, 1) << "; semitones +4 " << juce::String (semis, 1) << "\n";
        auto near = [] (double a, double b) { return std::abs (a / b - 1.0) < 0.004; };
        const double c4 = 261.6256;
        check (near (e4, c4 * std::exp2 (4.0 / 12)) && near (c5, c4 * 2.0) && near (b3, c4 * std::exp2 (-1.0 / 12)), "major: degrees 2, 7 and -1 are e4, c5 and b3");
        check (near (q, c4 * std::exp2 (1.5 / 12)) && near (sev, c4 * 7.0 / 4.0), "the scala file's 150 cents and 7/4, between the keys (a pitch bend)");
        check (near (semis, c4 * std::exp2 (4.0 / 12)), "without a scale the steps are semitones, as before");
        // the scale is kept with the project
        s.setScale (*t, sc);
        StudioProcessor q2; q2.session.load (*s.save(), nullptr);
        Track* t2 = nullptr; for (auto& x : q2.session.tracks) if (x->name == "lane") t2 = x.get();
        check (t2 && t2->scale && t2->scale->size() == 3 && std::abs (t2->scale->centsOf (2) - 968.826) < 0.01, "the scale survives save / load");

        // phase: the same one-hit bar on two tracks, one 1% faster: it pulls ahead a little more every bar
        s.setScale (*t, nullptr);
        StudioProcessor pp; auto& ss = pp.session; ss.newFromTemplate (0); ss.setTempo (120.0f);
        auto* a = ss.addTrack ("a"); ss.setInstrument (*a, ss.createDevice ("fal:bd1")); shortHit (*a);
        auto* b = ss.addTrack ("b"); ss.setInstrument (*b, ss.createDevice ("fal:bd1")); shortHit (*b);
        for (auto* tr : { a, b }) { ss.editPattern (*tr, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1.0f; }); ss.launch (*tr, 0); tr->toMaster.store (false); }
        ss.multiOut.store (true); a->outChannel.store (3); b->outChannel.store (5); b->phaseRate.store (1.01f);
        auto r = render (pp, 4.2);
        const auto oa = onsets (r.out, 2), ob = onsets (r.out, 4);
        const double bar = 4.0 * spb (120.0);
        std::cout << "  phase: a at"; for (int x : oa) std::cout << " " << x; std::cout << "; b (1% faster) at"; for (int x : ob) std::cout << " " << x; std::cout << "\n";
        bool drift = oa.size() >= 4 && ob.size() >= 4;
        for (size_t k = 0; drift && k < 4; ++k) drift = std::abs ((oa[k] - ob[k]) - k * bar * (1.0 - 1.0 / 1.01)) < 30.0;
        check (drift, "the faster part is ahead by 1% of a bar more on every bar");
    }

    // ---- 31. note entry: typed patterns, additive / subtractive figures --------------------------------------------
    std::cout << "== typed patterns, additive and subtractive process\n";
    {
        juce::String err; Pattern p;
        check (tools::typed (p, "0 . 2 _ 4! . 7? . 0' . 4~ 2*3 -3 0, x", 7, &err), "a typed pattern reads: " + err);
        check (p.length == 15 && p.s[0].on && p.s[0].pitch == 0 && ! p.s[1].on && p.s[2].pitch == 2 && std::abs (p.s[2].len - 1.9f) < 1.0e-4f && ! p.s[3].on, "notes, rests and a held note");
        check (p.s[4].vel == 1.0f && p.s[6].prob == 0.5f && p.s[8].pitch == 7 && p.s[10].vel < 0.5f && p.s[11].ratchet == 3 && p.s[12].pitch == -3, "accent, chance, an octave of seven degrees, soft, ratchet, below the root");
        check (p.s[13].on && p.s[13].pitch == -7 && p.s[14].on && p.s[14].pitch == 0, "an octave down, and x for the root");
        check (! tools::typed (p, "0 2 banana", 12, &err) && err.contains ("banana"), "a word it cannot read is named");
        // additive: a cell of three notes (lengths 2, 1, 3 steps)
        Pattern cell; cell.length = 6; cell.s[0].on = true; cell.s[0].pitch = 0; cell.s[2].on = true; cell.s[2].pitch = 4; cell.s[3].on = true; cell.s[3].pitch = 7;
        auto add = tools::process (cell, 0), sub = tools::process (cell, 1), both = tools::process (cell, 2);
        auto lens = [] (const std::vector<Pattern>& v) { juce::StringArray a; for (auto& x : v) a.add (juce::String (x.length)); return a.joinIntoString (" "); };
        std::cout << "  cell 2+1+3 steps: additive " << lens (add) << ", subtractive " << lens (sub) << ", both " << lens (both) << "\n";
        check (lens (add) == "2 3 6" && lens (sub) == "6 3 2" && lens (both) == "2 3 6 3 2", "figures grow and shrink by whole notes");
        check (add[1].s[2].on && add[1].s[2].pitch == 4 && ! add[0].s[1].on, "a figure holds the cell's own notes");
        // as a chain on a track: the figures play one after another, each repeated
        StudioProcessor pr; auto& s = pr.session; s.newFromTemplate (0); s.setTempo (120.0f);
        auto* t = s.addTrack ("cell"); s.setInstrument (*t, s.createDevice ("fal:bd1")); shortHit (*t);
        s.setPattern (*t, 0, std::make_shared<const Pattern> (cell)); t->pattern.store (0);
        const int made = SeqPage::applyProcess (s, *t, 0, 2);
        check (made == 3 && t->chainOn.load() && t->chain->size() == 6, "three figures, each twice, chained");
        auto r = render (pr, 2.0);
        const auto on = onsets (r.out, 0, 0.05f, 1500);
        const double step = spb (120.0) / 4.0;
        // figures 2, 2, 3, 3, 6, 6 steps: hits at 0 2 | 4 6 7 ... 
        std::vector<double> expect; double at = 0;
        for (int f : { 0, 0, 1, 1, 2, 2 }) { const auto& fig = add[(size_t) f]; for (int i = 0; i < fig.length; ++i) if (fig.s[(size_t) i].on) expect.push_back ((at + i) * step); at += fig.length; }
        bool ok = on.size() >= 8;
        for (size_t k = 0; ok && k < 8; ++k) ok = std::abs (on[k] - expect[k]) < 64.0;
        std::cout << "  the chain plays at steps"; for (size_t k = 0; k < std::min<size_t> (on.size(), 10); ++k) std::cout << " " << juce::String (on[k] / step, 2); std::cout << "\n";
        check (ok, "the chain plays the figures in order");
    }

    // ---- 32. micro-loops in the sampler --------------------------------------------------------------------------------
    std::cout << "== sampler micro-loops\n";
    {
        // a second of noise as the sample: a micro-loop turns it into a pitched buzz that repeats exactly
        juce::AudioBuffer<float> noise (2, 48000); Rng rr; rr.seed (5); for (int i = 0; i < 48000; ++i) { const float v = rr.bi() * 0.5f; noise.setSample (0, i, v); noise.setSample (1, i, v); }
        const auto nf = tmp.getChildFile ("noise.wav"); writeWav (nf, noise);
        StudioProcessor pr; auto& s = pr.session; s.newFromTemplate (0); s.setTempo (120.0f);
        auto* t = s.addTrack ("micro"); s.setInstrument (*t, s.createDevice ("st:sampler"));
        auto* d = t->instrument.get();
        check (dynamic_cast<SampleHolder*> (d->processor())->setSampleFile (nf), "the sample loads");
        auto setPlain = [&] (const char* id, float plain) { const int i = d->paramIndexForId (id); auto* rp = dynamic_cast<juce::RangedAudioParameter*> (d->processor()->getParameters()[i]); d->setBase (i, rp->convertTo0to1 (plain)); };
        setPlain ("microloop", 12.5f); setPlain ("attack", 0.0f); setPlain ("vel", 0.0f);
        s.editPattern (*t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].len = 8.0f; }); s.launch (*t, 0);
        auto period = [&] (int lag)
        {
            auto r = render (pr, 0.5);
            double num = 0, den = 0; for (int i = 2000; i < 12000; ++i) { const double a = r.out.getSample (0, i), b = r.out.getSample (0, i + lag); num += (a - b) * (a - b); den += a * a; }
            return den > 0 ? num / den : 1.0;
        };
        const double at12 = period (600), off = period (611);
        std::cout << "  micro-loop 12.5 ms: the output against itself 12.5 ms later differs by " << at12 << " (at 12.73 ms: " << off << ")\n";
        check (at12 < 1.0e-3 && off > 0.1, "a 12.5 ms micro-loop repeats every 12.5 ms");
        setPlain ("microgrid", 3.0f);   // 1/64 note at 120 bpm = a 16th of a beat = 31.25 ms = 1500 samples
        const double g = period (1500), g2 = period (600);
        std::cout << "  on the grid, 1/64 note at 120 bpm: " << g << " at 1500 samples, " << g2 << " at 600\n";
        check (g < 1.0e-3 && g2 > 0.1, "a micro-loop of a 64th note follows the tempo");
        setPlain ("microloop", 0.0f); setPlain ("microgrid", 0.0f);
        check (period (600) > 0.1, "off: the sample plays on");
    }

    // ---- 27. hosting the user's own plug-ins: a crash-safe scan, instruments and effects, state, automation, latency ----
    std::cout << "== hosting: scan in child processes, instruments, effects, state, automation, latency\n";
    {
        juce::VST3PluginFormat vf; const auto sp = vf.getDefaultLocationsToSearch();
        auto find = [&] (const juce::String& n) { for (int i = 0; i < sp.getNumPaths(); ++i) { auto f = sp[i].getChildFile (n); if (f.exists()) return f; } return juce::File(); };
        const auto quell = find ("Quell.vst3"), convo = find ("ConvoSolution.vst3"), ms0 = find ("faltung ms0.vst3"), fm = find ("EX Poly FM.vst3");
        if (! quell.exists() || ! ms0.exists() || ! fm.exists()) std::cout << "  (Quell, faltung ms0 or EX Poly FM not installed here: skipped)\n";
        else
        {
            const auto folder = tmp.getChildFile ("plug-ins"); folder.createDirectory();
            for (auto& f : { quell, convo, ms0, fm }) if (f.exists()) f.createSymbolicLink (folder.getChildFile (f.getFileName()), true);
            quell.copyDirectoryTo (folder.getChildFile ("crash me.vst3"));   // (a copy: a link would be followed to Quell itself)
           #if JUCE_WINDOWS
            _putenv_s ("SNDSTUDIO_SCAN_CRASH_ON", "crash me");
           #else
            setenv ("SNDSTUDIO_SCAN_CRASH_ON", "crash me", 1);
           #endif
            Session::scannerExecutable() = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
            s.knownOverride = tmp.getChildFile ("known plug-ins.xml"); s.known.clear(); s.known.clearBlacklistedFiles();
            const juce::FileSearchPath path (folder.getFullPathName());
            int files = 0;
            const int failed = s.scanPlugins (&path, [&] (const juce::String&) { ++files; return true; });
           #if JUCE_WINDOWS
            _putenv_s ("SNDSTUDIO_SCAN_CRASH_ON", "");
           #else
            unsetenv ("SNDSTUDIO_SCAN_CRASH_ON");
           #endif
            juce::StringArray names; for (auto& d : s.known.getTypes()) names.add (d.name + (d.isInstrument ? " (instrument)" : ""));
            std::cout << "  scanned " << files << " files in child processes: " << names.joinIntoString (", ") << "; blacklisted: " << s.blacklisted().joinIntoString (", ") << "\n";
            check (failed == 1 && s.blacklisted().size() == 1 && s.blacklisted()[0].contains ("crash me"), "the plug-in that crashed its scan is blacklisted, the studio carried on");
            auto typeOf = [&] (const juce::String& n) { for (auto& e : s.deviceTypes()) if (e.name.startsWith (n + " (")) return e; return Session::TypeEntry(); };
            check (typeOf ("Quell").typeId.isNotEmpty() && typeOf ("EX Poly FM").typeId.isNotEmpty() && typeOf ("faltung ms0").typeId.isNotEmpty(), "the scanned plug-ins are in the device list");
            check (typeOf ("EX Poly FM").instrument && ! typeOf ("Quell").instrument, "instruments and effects told apart");
            check (s.known.getTypes().size() >= 3 && s.knownOverride.existsAsFile(), "the known list is saved");
            // an instrument and two effects on a track, an effect on a return and on the master
            auto& t = *s.tracks[0];
            juce::String err;
            auto inst = s.createDevice (typeOf ("EX Poly FM").typeId, &err); check (inst != nullptr, "EX Poly FM loads: " + err);
            if (inst)
            {
                s.setInstrument (t, std::move (inst));
                s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); for (int k = 0; k < 16; k += 4) { pt.s[(size_t) k].on = true; pt.s[(size_t) k].len = 2.0f; } }); s.launch (t, 0);
                auto* q = s.addInsert (t, s.createDevice (typeOf ("Quell").typeId, &err));
                check (q != nullptr, "Quell loads on the track");
                const int lat0 = s.totalLatency();
                auto* m = s.addInsert (*s.returns[0], s.createDevice (typeOf ("faltung ms0").typeId, &err));
                check (m != nullptr, "ms0 loads on a return");
                auto r = render (p, 2.0);
                const int devLat = m ? m->processor()->getLatencySamples() : 0;
                std::cout << "  hosted EX Poly FM + Quell: peak " << db (r.peak[0]) << "; ms0 on a return reports " << devLat << " samples, the studio's latency " << lat0 << " -> " << s.totalLatency() << "\n";
                check (r.peak[0] > 0.01f, "the hosted instrument plays through the hosted effect");
                check (devLat > 0 && s.totalLatency() >= lat0 + devLat, "the hosted plug-in's latency is compensated");
                // automation of a hosted parameter
                if (q && q->numParams() > 0)
                {
                    int pi = 0; for (int i = 0; i < q->numParams(); ++i) if (q->paramName (i).containsIgnoreCase ("mix")) { pi = i; break; }
                    const Target tg = s.targetFor (t, *q, pi);
                    s.editLane (tg, [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 4.0, 1.0f, 0.0f } }; });
                    float seen = 0; render (p, 1.0, [&] (int, double ppq) { if (ppq > 3.0) seen = std::max (seen, q->baseValue (pi)); });
                    std::cout << "  automation of Quell's \"" << q->paramName (pi) << "\": " << seen << " near the end of the ramp\n";
                    check (seen > 0.7f, "a hosted parameter follows its automation lane");
                    s.removeLane (tg);
                    q->setBase (pi, 0.37f);
                }
                // state with the project
                const auto f = tmp.getChildFile ("hosted.fstudio");
                check (p.saveProject (f, err), "save with hosted plug-ins");
                StudioProcessor p2; p2.session.knownOverride = s.knownOverride; p2.session.loadKnown();
                check (p2.loadProject (f, err), "reopen: " + err);
                auto& t2 = *p2.session.tracks[0];
                check (t2.instrument && t2.instrument->kind == Device::Kind::Hosted && t2.inserts.size() == t.inserts.size(), "the hosted instrument and effects come back");
                if (q && t2.inserts.size() == t.inserts.size())
                {
                    Device* q2 = nullptr; for (auto& d : t2.inserts) if (d->name == q->name) q2 = d.get();
                    int pi = 0; for (int i = 0; i < q->numParams(); ++i) if (q->paramName (i).containsIgnoreCase ("mix")) { pi = i; break; }
                    check (q2 && std::abs (q2->baseValue (pi) - 0.37f) < 0.01f, "a hosted plug-in's value survives save / reopen");
                }
                check (p2.session.returns[0]->inserts.size() == 1, "the return's hosted plug-in comes back");
            }
            Session::scannerExecutable() = juce::File();
        }
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

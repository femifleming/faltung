// sndstudioTest — renders sndstudio projects offline and checks the engine feature by feature.
//   sndstudioTest [--screenshots dir] [--wav dir] [--vst3 "path/faltung dl4.vst3"]
#include "daw/StudioProcessor.h"
#include "daw/StudioEditor.h"
#include <juce_dsp/juce_dsp.h>
#include <iostream>

using namespace fal;
using namespace fal::daw;

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
                      std::function<void (juce::AudioBuffer<float>&, int pos)> fillIn = {})
{
    p.isStandaloneApp = true;
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
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File shotDir, wavDir, vst3;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--screenshots" && i + 1 < argc) shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--wav" && i + 1 < argc) wavDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        if (a == "--vst3" && i + 1 < argc) vst3 = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
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
        if (auto* hp = p.apvts.getParameter (StudioProcessor::hostParamId (0, MP_LEVEL)))
        {
            hp->setValueNotifyingHost (0.0f);
            auto r6 = render (p, 1.0);
            check (std::abs (s.tracks[0]->mixer->baseValue (MP_LEVEL)) < 1.0e-4f, "host parameter reaches the strip");
            check (r6.peak[0] < r.peak[0] * 0.95f, "and the kick is gone");
            hp->setValueNotifyingHost (0.8f);
        }
        check (p.getBusCount (false) == kOutChannels / 2, "64 output buses");
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
        if (on.size() == 2) { check (std::abs (on[0] - 0) <= 2, "first hit on the one"); check (std::abs (on[1] - (int) (8 * 0.25 * spb (120))) <= 2, "second hit on the three"); }
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
        check (ona.size() == 2 && std::abs (ona[0] - (int) (8.0 * spb (120))) <= 2, "arrangement clips");
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
        check (! s.addRoute (*t3, { s.tracks[0]->uid, 2, 0, 0, 1.0f }), "a cycle is refused");
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
        // resample project: the new track's clip equals the master export
        s.newFromTemplate (1); s.projectFile = tmp.getChildFile ("rs.fstudio"); s.projectName = "rs";
        p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        juce::String err;
        auto mf = tmp.getChildFile ("master.wav");
        ExportOptions o; o.bits = 32; o.dither = false; o.tail = false;
        check (s.renderMaster (mf, 0.0, 2.0, nullptr, err, o), "master export: " + err);
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

    // ---- 12. plug-in mode: the aux buses -------------------------------------------------------------------
    std::cout << "== plug-in buses\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.isStandaloneApp = false;
        auto layout = p.getBusesLayout();
        for (int i = 0; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = (i == 0 || i == 2 || i == 63) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        check (p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout), "main + bus 3 + bus 64 enabled");
        check (p.getTotalNumOutputChannels() == 6, "6 channels active");
        auto& t = *s.tracks[0]; shortHit (t); s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; pt.s[8].on = true; });
        s.launch (t, 0); s.multiOut.store (true); t.outChannel.store (127); t.toMaster.store (false);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        struct Head : juce::AudioPlayHead { double ppq = 0; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120.0); i.setIsPlaying (true); i.setPpqPosition (ppq); return i; } } head;
        p.setPlayHead (&head);
        juce::AudioBuffer<float> blk (6, kBs); float pk[6] = {};
        for (int b = 0; b < 100; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); for (int c = 0; c < 6; ++c) pk[c] = std::max (pk[c], blk.getMagnitude (c, 0, kBs)); head.ppq += kBs / kSr * 2.0; }
        std::cout << "  bus 64 (channels 5-6 of the active set): " << db (pk[4]) << ", main: " << db (pk[0]) << "\n";
        check (pk[4] > 0.05f && pk[5] > 0.05f, "track reaches the enabled aux bus");
        check (pk[0] < 1.0e-6f, "and not the main bus");
        t.outChannel.store (19);   // a disabled bus folds back into the main output
        for (int c = 0; c < 6; ++c) pk[c] = 0;
        for (int b = 0; b < 100; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); for (int c = 0; c < 6; ++c) pk[c] = std::max (pk[c], blk.getMagnitude (c, 0, kBs)); head.ppq += kBs / kSr * 2.0; }
        check (pk[0] > 0.05f, "a disabled pair folds into the main output");
        check (s.hostDriven.load(), "follows the host transport");
        p.setPlayHead (nullptr);
    }

    // ---- 13. export: stems and midi; produce vs multi-out -------------------------------------------------
    std::cout << "== export + modes\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.isStandaloneApp = true; p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
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
        auto rm = render (p, 1.0, [&] (int b, double) { const float m = s.master->meter[0].load(); if (m > 0.01f && firstLoud < 0) firstLoud = b; masterMeter = std::max (masterMeter, m); });
        std::cout << "  master strip meter " << db (masterMeter) << " (first loud block " << firstLoud << "), masterOut " << s.masterOut.load() << ", multiOut " << s.multiOut.load() << "\n";
        std::cout << "  multi-out: master " << db (rm.peak[0]) << ", kick 3-4 " << db (rm.peak[2]) << ", bass 17-18 " << db (rm.peak[16]) << ", dub return 19-20 " << db (rm.peak[18]) << "\n";
        check (rm.peak[0] < 1.0e-6f, "nothing on the master when every track goes direct");
        check (rm.peak[2] > 0.05f && rm.peak[16] > 0.01f, "tracks on their channels");
        check (rm.peak[18] > 0.001f, "returns on their channels");
    }

    // ---- 14. screenshots ----------------------------------------------------------------------------------
    if (shotDir != juce::File())
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.isStandaloneApp = true; p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        juce::AudioBuffer<float> blk (kOutChannels, kBs);
        s.play();
        for (int b = 0; b < 200; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); if (b % 10 == 0 && se) se->tickForTest(); }
        const int pages = se ? se->numPages() : 1;
        for (int pg = 0; pg < pages; ++pg)
        {
            if (se) { se->showPage (pg); for (int k = 0; k < 4; ++k) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); se->tickForTest(); } }
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto f = shotDir.getChildFile ("sndstudio-" + juce::String (pg) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        }
        s.stop();
        std::cout << "  " << pages << " screenshots in " << shotDir.getFullPathName() << "\n";
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

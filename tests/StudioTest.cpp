// faltungStudioTest — renders st0 studio projects offline and checks the engine feature by feature.
//   faltungStudioTest [--screenshots dir] [--wav dir] [--vst3 "path/faltung dl4.vst3"] [--keep]
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

/** renders `bars` with the internal clock (standalone mode: raw channels), returns peak per channel and the whole output */
struct Render
{
    std::array<float, 128> peak {};
    juce::AudioBuffer<float> out;   // first 16 channels
};
static Render render (StudioProcessor& p, double bars, int channels = 128, std::function<void (int block, double ppq)> each = {})
{
    p.isStandaloneApp = true;
    p.setRateAndBufferSizeDetails (kSr, kBs);
    p.prepareToPlay (kSr, kBs);
    auto& s = p.session;
    const double bpm = s.tempo();
    const int total = (int) std::ceil (bars * 4.0 * 60.0 / bpm * kSr);
    Render r; r.out.setSize (16, total); r.out.clear();
    juce::AudioBuffer<float> blk (channels, kBs);
    s.locate (0.0); s.play();
    for (int pos = 0, b = 0; pos < total; pos += kBs, ++b)
    {
        blk.clear();
        juce::MidiBuffer m;
        if (each) each (b, s.internalPpq.load());
        p.processBlock (blk, m);
        const int n = std::min (kBs, total - pos);
        for (int c = 0; c < channels; ++c)
        {
            r.peak[(size_t) c] = std::max (r.peak[(size_t) c], blk.getMagnitude (c, 0, n));
            if (c < 16) r.out.copyFrom (c, pos, blk, c, 0, n);
        }
    }
    s.stop();
    return r;
}
static void shortHit (Track& t)   // a 35 ms kick with no sweep, so onsets are unambiguous
{
    auto* d = t.instrument.get();
    d->setBase (d->paramIndexForId ("decay"), 0.0f); d->setBase (d->paramIndexForId ("sweep"), 0.0f);
}
static std::vector<int> onsets (const juce::AudioBuffer<float>& b, int ch, float thresh = 0.05f, int hold = 2000)
{
    std::vector<int> v; int last = -hold;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > thresh && i - last > hold) { v.push_back (i); last = i; }
    return v;
}
static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, int ch0, int nch)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), kSr, (unsigned) nch, 24, {}, 0));
    juce::AudioBuffer<float> sub (nch, b.getNumSamples());
    for (int c = 0; c < nch; ++c) sub.copyFrom (c, 0, b, ch0 + c, 0, b.getNumSamples());
    if (w) w->writeFromAudioSampleBuffer (sub, 0, sub.getNumSamples());
}

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
    const auto spb = [] (double bpm) { return 60.0 / bpm * kSr; };

    // ---- 1. the template plays, through the sends, to the 128 outputs ---------------------------------
    std::cout << "== template: " << Session::templateNames()[1] << "\n";
    {
        StudioProcessor p;
        auto& s = p.session;
        s.newFromTemplate (1);
        check (s.tracks.size() == 8, "template has 8 tracks");
        check (s.returns[0]->inserts.size() == 1 && s.returns[1]->inserts.size() == 1, "template returns have dub and room");
        check (s.master->inserts.size() >= 2, "template master chain");
        s.mode.store ((int) PlayMode::Session);
        s.launchScene (4);   // "full"
        auto r = render (p, 2.0);
        std::cout << "  main out peak " << db (r.peak[0]) << " / " << db (r.peak[1]) << "\n";
        check (r.peak[0] > 0.05f && r.peak[1] > 0.05f, "template main output has sound");
        check (r.peak[2] < 1.0e-6f && r.peak[4] < 1.0e-6f, "other channels silent when everything goes to the master");
        for (int c = 0; c < 128; ++c) check (std::isfinite (r.peak[(size_t) c]), "finite");
        if (wavDir != juce::File()) writeWav (wavDir.getChildFile ("st0 template.wav"), r.out, 0, 2);

        // route: kick → pair 3 (channels 5-6), return 1 (dub) → pair 4 (channels 7-8)
        s.tracks[0]->outPair.store (3); s.returns[0]->outPair.store (4);
        auto r2 = render (p, 2.0);
        std::cout << "  kick on 5-6: " << db (r2.peak[4]) << "   dub return on 7-8: " << db (r2.peak[6]) << "   main: " << db (r2.peak[0]) << "\n";
        check (r2.peak[4] > 0.05f && r2.peak[5] > 0.05f, "track routed to its own channel pair");
        check (r2.peak[6] > 0.001f && r2.peak[7] > 0.001f, "return routed to its own channel pair");
        check (r2.peak[0] < r.peak[0] * 0.95f, "main lost the kick");
        check (r2.peak[2] < 1.0e-6f && r2.peak[8] < 1.0e-6f, "unused pairs stay silent");
        // pair 64 = channels 127-128
        s.tracks[0]->outPair.store (64);
        auto r3 = render (p, 1.0);
        check (r3.peak[126] > 0.05f && r3.peak[127] > 0.05f, "pair 64 reaches channels 127-128");
        s.tracks[0]->outPair.store (0); s.returns[0]->outPair.store (0);

        // mute and solo
        s.tracks[0]->mixer->setBase (MP_MUTE, 1.0f);
        auto r4 = render (p, 1.0);
        check (r4.peak[0] < r.peak[0] * 0.9f, "mute lowers the output");
        s.tracks[0]->mixer->setBase (MP_MUTE, 0.0f);
        s.tracks[2]->solo.store (true);
        for (int k = 1; k < 8; ++k) if (k != 2) s.tracks[(size_t) k]->mixer->setBase (MP_LEVEL, 0.0f);
        s.tracks[0]->mixer->setBase (MP_LEVEL, 0.8f);
        float kickMeter = 0, hatMeter = 0;
        render (p, 1.0, 128, [&] (int, double) { kickMeter = std::max (kickMeter, s.tracks[0]->meterL.load()); hatMeter = std::max (hatMeter, s.tracks[2]->meterL.load()); });
        std::cout << "  solo on the hat: kick strip " << db (kickMeter) << ", hat strip " << db (hatMeter) << "\n";
        check (kickMeter < 1.0e-6f && hatMeter > 0.01f, "solo silences the kick");
        s.tracks[2]->solo.store (false);
        for (int k = 0; k < 8; ++k) s.tracks[(size_t) k]->mixer->setBase (MP_LEVEL, 0.8f);

        // the ms0 master chain is there, bypassed
        bool ms0 = false; for (auto& d : s.master->inserts) if (d->typeId == "fal:ms0") { ms0 = true; check (d->bypass.load(), "ms0 bypassed for ableton"); }
        check (ms0 || kDeep, "ms0 on the master");

        // host parameters: automating "track 1 level" from the host reaches the strip
        if (auto* hp = p.apvts.getParameter (StudioProcessor::hostParamId (0, MP_LEVEL)))
        {
            hp->setValueNotifyingHost (0.0f);
            auto r6 = render (p, 1.0);
            check (std::abs (s.tracks[0]->mixer->baseValue (MP_LEVEL)) < 1.0e-4f, "host parameter reaches the strip");
            check (r6.peak[0] < r.peak[0] * 0.95f, "and the kick is gone");
            hp->setValueNotifyingHost (0.8f);
        }
        check (p.getBusCount (false) == kOutPairs, "64 output buses");
        check (p.getTotalNumInputChannels() == 0, "no inputs");
    }

    // ---- 2. sequencer timing: steps, micro, swing, speed, length, ratchets ----------------------------
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
        // micro: step 8 late by the full micro range (12 ms)
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].micro = 1.0f; });
        auto rm = render (p, 1.0); auto onm = onsets (rm.out, 0);
        if (onm.size() == 2) { const int exp = (int) (8 * 0.25 * spb (120) + 0.012 * kSr); std::cout << "  micro +12 ms: " << onm[1] << " (expected " << exp << ")\n"; check (std::abs (onm[1] - exp) <= 2, "microtiming"); }
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].micro = 0; });
        // swing: an offbeat 16th (step 1) moves later by (swing - 0.5) * 2 * 16th
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[8].on = false; pt.s[1].on = true; pt.s[1].vel = 1; });
        s.global->setBase (GP_SWING, 0.4f);   // 60%
        auto rs = render (p, 1.0); auto ons = onsets (rs.out, 0);
        if (ons.size() == 2) { const int exp = (int) ((0.25 + 0.1 * 2 * 0.25) * spb (120)); std::cout << "  swing 60%: " << ons[1] << " (expected " << exp << ")\n"; check (std::abs (ons[1] - exp) <= 2, "swing"); }
        s.global->setBase (GP_SWING, 0.0f);
        // speed x2: step 1 lands on the 32nd
        t.speed.store (5);
        auto rx = render (p, 1.0); auto onx = onsets (rx.out, 0);
        if (onx.size() >= 2) { const int exp = (int) (0.125 * spb (120)); std::cout << "  speed x2: " << onx[1] << " (expected " << exp << ")\n"; check (std::abs (onx[1] - exp) <= 2, "speed multiplier"); }
        t.speed.store (3);
        // length 12: the pattern wraps after 12 steps
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 12; pt.s[0].on = true; pt.s[0].vel = 1; });
        auto rl = render (p, 2.0); auto onl = onsets (rl.out, 0);
        std::cout << "  length 12 over 2 bars: " << onl.size() << " hits (expected 3)\n";
        check (onl.size() == 3, "polymeter length");
        if (onl.size() == 3) check (std::abs (onl[1] - (int) (12 * 0.25 * spb (120))) <= 2, "wrap at step 12");
        // ratchet 4 on step 0
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].vel = 1; pt.s[0].ratchet = 4; });
        auto rr = render (p, 1.0); auto onr = onsets (rr.out, 0, 0.2f, 1200);
        std::cout << "  ratchet 4: " << onr.size() << " hits\n";
        check (onr.size() == 4, "ratchet 4");
        // probability 0: nothing
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].ratchet = 1; pt.s[0].prob = 0.0f; });
        auto rp = render (p, 2.0);
        check (rp.peak[0] < 1.0e-6f, "probability 0 is silent");
        // conditions: 1:2 → one hit in two bars, fill → only when fill is held, first → only the first loop
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].prob = 1.0f; pt.s[0].cond = Cond::X1of2; });
        auto rc = render (p, 4.0); auto onc = onsets (rc.out, 0);
        std::cout << "  1:2 over 4 bars: " << onc.size() << " hits\n"; check (onc.size() == 2, "condition 1:2");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::Fill; });
        auto rf = render (p, 2.0); check (rf.peak[0] < 1.0e-6f, "fill steps silent without fill");
        s.fill.store (true); auto rf2 = render (p, 2.0); s.fill.store (false); check (onsets (rf2.out, 0).size() == 2, "fill steps play with fill");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::First; });
        auto r1 = render (p, 4.0); check (onsets (r1.out, 0).size() == 1, "condition first");
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].cond = Cond::Always; });
        // pitch: +12 on a keytracked module doubles the fundamental (a plain sine body: no click, no sweep, long decay)
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
        s.editPattern (t, 0, [] (Pattern& pt) { pt.s[0].pitch = 0; });
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
        render (p, 1.0, 128, [&] (int, double ppq) { seen.push_back ({ ppq, inst->finalValue (tune) }); });
        float atStep2 = -1, atStep5 = -1, atStep9 = -1;
        for (auto& [q, v] : seen) { if (q >= 0.5 && q < 0.75) atStep2 = v; if (q >= 1.25 && q < 1.5) atStep5 = v; if (q >= 2.25 && q < 2.5) atStep9 = v; }
        std::cout << "  tune at step 2: " << atStep2 << "  step 5 (locked): " << atStep5 << "  step 9 (free): " << atStep9 << "\n";
        check (std::abs (atStep2 - 0.3f) < 0.01f, "base before the lock");
        check (std::abs (atStep5 - 0.9f) < 0.01f, "lock applies at its step");
        check (std::abs (atStep9 - 0.3f) < 0.01f, "the next unlocked step returns to the base");
        check (std::abs (inst->baseValue (tune) - 0.3f) < 1.0e-5f, "the base itself is untouched");
        // slide: step 4 locked 0.2 → step 12 locked 0.8, slide on: half way (step 8) about 0.5
        s.editPattern (t, 0, [&] (Pattern& pt) { pt.s[4].locks[0] = { inst->uid, tune, 0.2f }; pt.s[4].slide = true; pt.s[12].locks[0] = { inst->uid, tune, 0.8f }; });
        seen.clear();
        render (p, 1.0, 128, [&] (int, double ppq) { seen.push_back ({ ppq, inst->finalValue (tune) }); });
        float mid = -1; for (auto& [q, v] : seen) if (q >= 2.0 && q < 2.05) mid = v;
        std::cout << "  slide half way: " << mid << " (expected ~0.5)\n";
        check (std::abs (mid - 0.5f) < 0.06f, "slide between locks");
        // a lock on the mixer (device 0): pan
        s.editPattern (t, 0, [&] (Pattern& pt) { pt = Pattern(); pt.length = 16; pt.s[0].on = true; pt.s[0].locks[0] = { 0, MP_PAN, 1.0f }; pt.s[8].on = true; });
        auto rpan = render (p, 1.0);
        const int half = (int) (2.0 * spb (120));
        const float lA = rpan.out.getRMSLevel (0, 0, half), rA = rpan.out.getRMSLevel (1, 0, half);
        const float lB = rpan.out.getRMSLevel (0, half, rpan.out.getNumSamples() - half), rB = rpan.out.getRMSLevel (1, half, rpan.out.getNumSamples() - half);
        std::cout << "  pan lock: first hit L " << db (lA) << " R " << db (rA) << "; second L " << db (lB) << " R " << db (rB) << "\n";
        check (rA > lA * 10.0f, "pan lock hard right");
        check (std::abs (lB - rB) < 0.1f * std::max (lB, rB), "next step back to centre");
    }

    // ---- 4. automation lanes and modulation -----------------------------------------------------------
    std::cout << "== automation + modulation\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0];
        s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[4].on = true; pt.s[8].on = true; pt.s[12].on = true; });
        s.launch (t, 0);
        s.editLane (s.targetFor (t, *t.mixer, MP_LEVEL), [] (AutoLane& l) { l.pts = { { 0.0, 0.0f, 0.0f }, { 16.0, 1.0f, 0.0f } }; });
        float atBar2 = -1;
        render (p, 4.0, 128, [&] (int, double ppq) { if (ppq >= 8.0 && ppq < 8.1) atBar2 = t.mixer->baseValue (MP_LEVEL); });
        std::cout << "  level lane 0→1 over 4 bars, at bar 3: " << atBar2 << " (expected 0.5)\n";
        check (std::abs (atBar2 - 0.5f) < 0.02f, "automation value at a bar");
        // curve: bent towards the end → below the line at the middle
        s.editLane (s.targetFor (t, *t.mixer, MP_LEVEL), [] (AutoLane& l) { l.pts[0].curve = 0.8f; });
        float curved = -1;
        render (p, 4.0, 128, [&] (int, double ppq) { if (ppq >= 8.0 && ppq < 8.1) curved = t.mixer->baseValue (MP_LEVEL); });
        std::cout << "  curved: " << curved << "\n";
        check (curved < 0.2f, "curve bends the segment");
        s.removeLane (s.targetFor (t, *t.mixer, MP_LEVEL));
        t.mixer->setBase (MP_LEVEL, 0.8f);
        // recording: a gesture while playing writes points
        s.autoWrite.store (true);
        {
            const Target tg = s.targetFor (t, *t.mixer, MP_PAN);
            render (p, 2.0, 128, [&] (int b, double) { if (b == 10) s.beginGesture (tg); if (b >= 10 && b < 60) { t.mixer->setBase (MP_PAN, 0.1f + 0.01f * (b - 10)); s.recordPoint (tg, 0.1f + 0.01f * (b - 10)); } if (b == 60) s.endGesture (tg); });
            auto l = s.lane (tg);
            std::cout << "  recorded " << (l ? (int) l->pts.size() : 0) << " points\n";
            check (l && l->pts.size() >= 20, "automation recording");
            s.removeLane (tg);
        }
        s.autoWrite.store (false);
        // modulation: lfo 1 (one bar, ramp up) → pan at +1: the pan final value climbs across the bar
        auto* inst = t.instrument.get();
        s.editMod (t, [&] (ModDef& m) { m.lfo[0].div = 14; m.lfo[0].shape = 3; m.slots[0] = { MS_LFO1, 0, MP_PAN, 0.5f, 1 }; });
        float early = -1, late = -1;
        render (p, 1.0, 128, [&] (int, double ppq) { if (ppq >= 0.0 && ppq < 0.1) early = t.mixer->finalValue (MP_PAN); if (ppq >= 3.8 && ppq < 3.95) late = t.mixer->finalValue (MP_PAN); });
        std::cout << "  lfo → pan: " << early << " → " << late << "\n";
        check (late > early + 0.3f, "lfo modulation depth");
        // macro 1 → instrument tune (lo 0.2, hi 0.9)
        const int tune = inst->paramIndexForId ("tune");
        inst->macro[(size_t) tune].index.store (0); inst->macro[(size_t) tune].lo.store (0.2f); inst->macro[(size_t) tune].hi.store (0.9f);
        s.setMacro (0, 0.5f);
        render (p, 0.25);
        std::cout << "  macro 1 at 50% → tune " << inst->finalValue (tune) << " (expected 0.55)\n";
        check (std::abs (inst->finalValue (tune) - 0.55f) < 0.01f, "macro mapping");
        s.editMod (t, [] (ModDef& m) { m.slots[0] = {}; });
        // midi learn: cc 74 → mixer level
        s.midiLearnStart (s.targetFor (t, *t.mixer, MP_LEVEL));
        {
            p.prepareToPlay (kSr, kBs);
            juce::AudioBuffer<float> blk (128, kBs); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
            p.processBlock (blk, m);
            check (s.midiLearnPoll(), "midi learn caught cc 74");
            m.clear(); m.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0); blk.clear();
            p.processBlock (blk, m);
            std::cout << "  cc 74 = 64 → level " << t.mixer->baseValue (MP_LEVEL) << "\n";
            check (std::abs (t.mixer->baseValue (MP_LEVEL) - 64.0f / 127.0f) < 0.01f, "midi map moves the strip");
        }
    }

    // ---- 5. scenes, song and arrangement -----------------------------------------------------------------
    std::cout << "== scenes / song / arrangement\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto& t = *s.tracks[0]; shortHit (t);
        s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; });            // one hit per bar
        s.editPattern (t, 1, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[8].on = true; });   // two hits per bar
        s.editScenes ([] (std::vector<Scene>& sc) { sc[0].pattern[0] = 0; sc[1].pattern[0] = 1; sc[2].pattern[0] = -1; });
        s.editSong ([] (std::vector<SongEntry>& so) { so = { { 0, 1 }, { 1, 1 }, { 2, 1 } }; });
        s.mode.store ((int) PlayMode::Song);
        auto r = render (p, 3.0); auto on = onsets (r.out, 0);
        std::cout << "  song 1+2+silent bars: " << on.size() << " hits (expected 3)\n";
        check (on.size() == 3, "song mode plays scenes in order");
        if (on.size() == 3) check (std::abs (on[2] - (int) (6.0 * spb (120))) <= 2, "second scene's pattern starts on its bar");
        // arrangement: a clip of pattern 1 at bar 2 for one bar
        s.mode.store ((int) PlayMode::Arrange);
        s.editClips (t, [] (std::vector<Clip>& c) { c = { { 1, 2.0, 1.0 } }; });
        auto ra = render (p, 4.0); auto ona = onsets (ra.out, 0);
        std::cout << "  arrangement clip at bar 3: " << ona.size() << " hits (expected 2), first at " << (ona.empty() ? -1 : ona[0]) << "\n";
        check (ona.size() == 2 && std::abs (ona[0] - (int) (8.0 * spb (120))) <= 2, "arrangement clips");
        // session: launch quantised to the next bar
        s.mode.store ((int) PlayMode::Session);
        s.launch (t, 0);
        auto rl = render (p, 4.0, 128, [&] (int b, double) { if (b == 20) s.launch (t, 1); });
        auto onl = onsets (rl.out, 0);
        std::cout << "  launch of pattern 2 after bar 1: " << onl.size() << " hits (expected 1 + 3 * 2 = 7)\n";
        check (onl.size() == 7, "quantised launch");
        // chain: 0 → 1, loops
        s.setChain (t, { 0, 1 }); t.chainOn.store (true); s.launch (t, 0);
        auto rc = render (p, 4.0); auto onc = onsets (rc.out, 0);
        std::cout << "  chain 0,1 over 4 bars: " << onc.size() << " hits (expected 6)\n";
        check (onc.size() == 6, "pattern chain");
        t.chainOn.store (false);
        // loop region: bars 0-1 looped for 3 bars → 3 hits of pattern 0 on the one of each bar... pattern 1 at 2 hits/bar, loop 1 bar
        s.launch (t, 1); s.loopOn.store (true); s.loopStart.store (0); s.loopEnd.store (1);
        auto rlp = render (p, 3.0); auto onlp = onsets (rlp.out, 0);
        std::cout << "  1-bar loop over 3 bars: " << onlp.size() << " hits (expected 6)\n";
        check (onlp.size() == 6, "loop region");
        s.loopOn.store (false);
    }

    // ---- 6. save / load round trip ---------------------------------------------------------------------
    std::cout << "== project round trip\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        s.tracks[1]->instrument->setBase (3, 0.123f);
        s.editPattern (*s.tracks[2], 5, [&] (Pattern& pt) { pt.s[3].on = true; pt.s[3].pitch = -5; pt.s[3].cond = Cond::X3of4; pt.s[3].locks[1] = { s.tracks[2]->instrument->uid, 2, 0.77f }; pt.s[3].slide = true; pt.length = 23; });
        s.tracks[3]->outPair.store (7); s.tracks[3]->sendPre[2].store (true);
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
        check (q.session.tracks.size() == 8 && q.session.tracks[3]->outPair.load() == 7, "routing survives");
        check (q.session.tracks[2]->patternAt (5) && q.session.tracks[2]->patternAt (5)->length == 23 && q.session.tracks[2]->patternAt (5)->s[3].cond == Cond::X3of4, "steps survive");
        check (std::abs (q.session.tracks[1]->instrument->baseValue (3) - 0.123f) < 1.0e-5f, "device base values survive");
        check (q.session.midiMaps->size() == 1 && (*q.session.midiMaps)[0].cc == 21, "midi maps survive");
        check (q.session.lanes->size() == s.lanes->size(), "lanes survive");
        // and it still plays
        q.session.launchScene (4);
        auto r = render (q, 1.0);
        check (r.peak[0] > 0.05f, "reloaded project plays");
        // file save / load
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("st0 test.fstudio");
        juce::String err;
        check (p.saveProject (f, err) && q.loadProject (f, err), "project file save / load");
        f.deleteFile();
    }

    // ---- 7. latency alignment: a swing (70 ms) on one track does not move its hits against the others ----
    std::cout << "== latency compensation\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        auto* t2 = s.addTrack ("second"); s.setInstrument (*t2, s.createDevice ("fal:bd1"));
        s.setTempo (120.0f);
        for (auto* t : { s.tracks[0].get(), t2 }) { s.editPattern (*t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; }); s.launch (*t, 0); }
        s.tracks[0]->outPair.store (2); t2->outPair.store (3);
        auto d = s.createDevice ("fal:sw6"); d->setBase (d->paramIndexForId ("amount"), 0.0f); s.addInsert (*t2, std::move (d));
        for (auto* t : { s.tracks[0].get(), t2 }) shortHit (*t);
        auto r = render (p, 2.0);
        std::cout << "  total latency " << s.totalLatency() << " samples\n";
        check (s.totalLatency() > 3000, "sw6 latency reported");
        auto a = onsets (r.out, 2), b = onsets (r.out, 4);
        std::cout << "  hits: plain track " << (a.empty() ? -1 : a[0]) << ", track with sw6 " << (b.empty() ? -1 : b[0]) << "\n";
        check (! a.empty() && ! b.empty() && std::abs (a[0] - b[0]) <= 3, "tracks aligned");
        if (! a.empty()) check (std::abs (a[0] - s.totalLatency()) <= 3, "the whole output is delayed by the reported latency");
    }

    // ---- 8. hosting a plug-in ---------------------------------------------------------------------------
    std::cout << "== hosting\n";
    {
        juce::File f = vst3;
        if (f == juce::File())
        {
            // look next to this executable (the build tree) for the suite's dl4
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
                    std::cout << "  hosted " << d->name << " with " << d->numParams() << " parameters\n";
                    const int mix = d->paramIndexForName ("mix"), fb = d->paramIndexForName ("feedback");
                    std::cout << "  channels in " << d->processor()->getTotalNumInputChannels() << " out " << d->processor()->getTotalNumOutputChannels() << ", mix id " << d->paramId (mix) << "\n";
                    check (mix >= 0 && fb >= 0, "hosted parameters found by name");
                    d->setBase (mix, 1.0f); d->setBase (fb, 0.6f);
                    auto* dev = s.addInsert (*s.returns[0], std::move (d));
                    auto& t = *s.tracks[0]; shortHit (t);
                    s.setTempo (120.0f);
                    s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; });
                    s.launch (t, 0);
                    t.mixer->setBase (MP_SEND1, 0.8f); s.returns[0]->outPair.store (5);
                    auto r = render (p, 2.0);
                    {
                        // the device on its own: an impulse in, something out
                        juce::AudioBuffer<float> b (2, kBs); b.clear(); b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
                        juce::MidiBuffer none; float pk = 0;
                        for (int k = 0; k < 20; ++k) { dev->process (b, none); pk = std::max (pk, b.getMagnitude (0, kBs)); b.clear(); }
                        std::cout << "  hosted device alone: peak " << db (pk) << ", latency " << dev->latency() << ", bus in enabled " << (dev->processor()->getBus (true, 0) && dev->processor()->getBus (true, 0)->isEnabled()) << ", suspended " << dev->processor()->isSuspended() << "\n";
                    }
                    auto on = onsets (r.out, 8, 0.01f);
                    std::cout << "  echoes on the return's pair: " << on.size() << " onsets, peak " << db (r.peak[8]) << "\n";
                    check (on.size() >= 3, "hosted delay makes echoes");
                    // the project remembers it
                    auto x = s.save();
                    StudioProcessor q; q.session.known.addType (*found[0]);
                    juce::String e2; q.session.load (*x, &e2);
                    check (q.session.returns[0]->inserts.size() == 1 && q.session.returns[0]->inserts[0]->kind == Device::Kind::Hosted, "hosted device reloads from the project: " + e2);
                    if (q.session.returns[0]->inserts.size() == 1 && fb >= 0) check (std::abs (q.session.returns[0]->inserts[0]->baseValue (fb) - 0.6f) < 1.0e-4f, "hosted base values reload");
                    juce::ignoreUnused (dev);
                }
            }
        }
        else std::cout << "  (no built dl4.vst3 found: skipped; pass --vst3)\n";
    }

    // ---- 9. plug-in mode: the aux buses ---------------------------------------------------------------
    std::cout << "== plug-in buses\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (0);
        p.isStandaloneApp = false;
        auto layout = p.getBusesLayout();
        for (int i = 0; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = (i == 0 || i == 2 || i == 63) ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        check (p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout), "main + bus 3 + bus 64 enabled");
        std::cout << "  total output channels " << p.getTotalNumOutputChannels() << "\n";
        check (p.getTotalNumOutputChannels() == 6, "6 channels active");
        auto& t = *s.tracks[0]; s.setTempo (120.0f);
        s.editPattern (t, 0, [] (Pattern& pt) { pt = Pattern(); pt.s[0].on = true; pt.s[0].vel = 1; pt.s[8].on = true; });
        s.launch (t, 0); t.outPair.store (64);
        p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        struct Head : juce::AudioPlayHead { double ppq = 0; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120.0); i.setIsPlaying (true); i.setPpqPosition (ppq); return i; } } head;
        p.setPlayHead (&head);
        juce::AudioBuffer<float> blk (6, kBs); float pk[6] = {};
        for (int b = 0; b < 100; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); for (int c = 0; c < 6; ++c) pk[c] = std::max (pk[c], blk.getMagnitude (c, 0, kBs)); head.ppq += kBs / kSr * 2.0; }
        std::cout << "  bus 64 (channels 5-6 of the active set): " << db (pk[4]) << ", main: " << db (pk[0]) << "\n";
        check (pk[4] > 0.05f && pk[5] > 0.05f, "track reaches the enabled aux bus");
        check (pk[0] < 1.0e-6f, "and not the main bus");
        t.outPair.store (10);   // a disabled bus folds back into the main output
        for (int c = 0; c < 6; ++c) pk[c] = 0;
        for (int b = 0; b < 100; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); for (int c = 0; c < 6; ++c) pk[c] = std::max (pk[c], blk.getMagnitude (c, 0, kBs)); head.ppq += kBs / kSr * 2.0; }
        check (pk[0] > 0.05f, "a disabled pair folds into the main output");
        check (s.hostDriven.load(), "follows the host transport");
        p.setPlayHead (nullptr);
    }

    // ---- 10. export: stems and midi ----------------------------------------------------------------------
    std::cout << "== export\n";
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.isStandaloneApp = true; p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("st0 stems");
        juce::String err;
        check (s.renderStems (dir, 0.0, 2.0, nullptr, err), "stems rendered: " + err);
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        int files = 0, sounding = 0;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            ++files;
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (! rd) continue;
            juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples); rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, true);
            if (b.getMagnitude (0, b.getNumSamples()) > 0.01f) ++sounding;
            if (f.getFileName().startsWith ("master")) check (std::abs ((double) rd->lengthInSamples - 3.0 * 4.0 * 60.0 / s.tempo() * kSr) < kBs * 2, "master stem length = 2 bars + 1 bar tail");
        }
        std::cout << "  " << files << " stems, " << sounding << " with sound\n";
        check (files == 8 + 2 + 1, "a stem per track, used return and master");
        check (sounding >= 8, "stems have sound");
        dir.deleteRecursively();
        auto mf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("st0 test.mid");
        check (s.exportMidi (mf, 0.0, 2.0, err), "midi exported: " + err);
        juce::FileInputStream in (mf); juce::MidiFile midi; check (midi.readFrom (in), "midi file reads back");
        int notes = 0; for (int t = 0; t < midi.getNumTracks(); ++t) for (auto* e : *midi.getTrack (t)) if (e->message.isNoteOn()) ++notes;
        std::cout << "  midi: " << midi.getNumTracks() << " tracks, " << notes << " notes\n";
        check (midi.getNumTracks() == 8 && notes > 20, "midi export has the notes");
        mf.deleteFile();
    }

    // ---- 11. screenshots -----------------------------------------------------------------------------
    if (shotDir != juce::File())
    {
        StudioProcessor p; auto& s = p.session; s.newFromTemplate (1);
        p.isStandaloneApp = true; p.setRateAndBufferSizeDetails (kSr, kBs); p.prepareToPlay (kSr, kBs);
        s.launchScene (4);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* se = dynamic_cast<StudioEditor*> (ed.get());
        juce::AudioBuffer<float> blk (128, kBs);
        s.play();
        for (int b = 0; b < 200; ++b) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); if (b % 10 == 0 && se) se->tickForTest(); }
        const int pages = se ? se->numPages() : 1;
        for (int pg = 0; pg < pages; ++pg)
        {
            if (se) { se->showPage (pg); for (int k = 0; k < 4; ++k) { blk.clear(); juce::MidiBuffer m; p.processBlock (blk, m); se->tickForTest(); } }
            juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto f = shotDir.getChildFile ("st0-" + juce::String (pg) + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        }
        s.stop();
        std::cout << "  " << pages << " screenshots in " << shotDir.getFullPathName() << "\n";
    }

    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

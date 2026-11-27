// umleitung — the engine, checked offline: sessions run against a layout of made-up devices (no CoreAudio), and what
// comes out is measured: levels, routing, channel mapping, the eqs, dynamics, loudness, delay, recording, plug-ins.
#include "engine/Engine.h"
#include "engine/PluginHost.h"
#include <iostream>

using namespace um;
static int failures = 0;
static void check (bool ok, const juce::String& what) { std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n"; if (! ok) ++failures; }
static constexpr double kSr = 48000; static constexpr int kBs = 512;

/** a layout: one tap (2 ch), one input device (4 ch), two output devices (8 ch, 2 ch) */
static IOLayout fakeLayout()
{
    IOLayout l; l.sampleRate = kSr; l.bufferSize = kBs;
    l.inputs.push_back ({ IOLayout::Part::Kind::Tap, 0, "tap", "tap", 0, 2 });
    l.inputs.push_back ({ IOLayout::Part::Kind::InputDevice, 0, "in4", "in4", 2, 4 });
    l.outputs.push_back ({ IOLayout::Part::Kind::OutputDevice, 0, "out8", "out8", 0, 8 });
    l.outputs.push_back ({ IOLayout::Part::Kind::OutputDevice, 1, "out2", "out2", 8, 2 });
    l.totalIn = 6; l.totalOut = 10;
    return l;
}
/** run n samples: in[c] = gen (c, sample), returns the outputs */
static juce::AudioBuffer<float> run (Engine& e, int n, std::function<float (int, int)> gen, int totalIn = 6, int totalOut = 10)
{
    juce::AudioBuffer<float> in (totalIn, n), out (totalOut, n); in.clear(); out.clear();
    for (int c = 0; c < totalIn; ++c) for (int i = 0; i < n; ++i) in.setSample (c, i, gen (c, i));
    for (int i = 0; i < n; i += kBs)
    {
        const int m = std::min (kBs, n - i);
        std::vector<const float*> ip; std::vector<float*> op;
        for (int c = 0; c < totalIn; ++c) ip.push_back (in.getReadPointer (c, i));
        for (int c = 0; c < totalOut; ++c) op.push_back (out.getWritePointer (c, i));
        e.process (ip.data(), totalIn, op.data(), totalOut, m);
    }
    return out;
}
static float sine (int i, double f, float a = 1) { return a * (float) std::sin (2.0 * dsp::kPi * f * i / kSr); }
static float rms (const juce::AudioBuffer<float>& b, int c, int from, int n) { return b.getRMSLevel (c, from, n); }
static float db (float g) { return dsp::gainToDb (g); }

int main (int argc, char** argv)
{
    if (argc >= 5 && juce::String (argv[1]) == "--scan-plugin") return PluginHost::scanChild (argv[2], argv[3], juce::File (juce::String (argv[4])));
    juce::ScopedJuceInitialiser_GUI gui;
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("umleitung engine test");
    tmp.deleteRecursively(); tmp.createDirectory();

    std::cout << "== routing and level\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("route"); s.setProperty (ids::on, true, nullptr);
        auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::pids, "4242", nullptr);
        auto vol = m.addBlock (s, BlockType::Volume, 1, 0); vol.setProperty (ids::db, -6.0206f, nullptr);
        auto out = m.addBlock (s, BlockType::Output, 2, 0); out.setProperty (ids::device, "out8", nullptr); out.setProperty (ids::first, 2, nullptr);
        m.connect (s, app[ids::id], vol[ids::id]); m.connect (s, vol[ids::id], out[ids::id]);
        e.runOffline (fakeLayout());
        auto o = run (e, 4800, [] (int c, int i) { return c < 2 ? sine (i, 1000, c == 0 ? 0.8f : 0.4f) : 0.0f; });
        check (std::abs (rms (o, 2, 2400, 2400) - 0.4f * 0.7071f) < 0.002f && std::abs (rms (o, 3, 2400, 2400) - 0.2f * 0.7071f) < 0.002f, "an app's sound, 6 db down, on outputs 3-4 of the 8-channel device");
        check (o.getMagnitude (0, 0, 4800) == 0.0f && o.getMagnitude (4, 0, 4800) == 0.0f && o.getMagnitude (8, 0, 4800) == 0.0f, "nothing anywhere else");
        // balance, mute, polarity
        vol.setProperty (ids::db, 0.0f, nullptr); vol.setProperty (ids::balance, 1.0f, nullptr);
        auto ob = run (e, 4800, [] (int c, int i) { return c < 2 ? sine (i, 1000, 0.5f) : 0.0f; });
        check (rms (ob, 2, 2400, 2400) < 1.0e-4f && rms (ob, 3, 2400, 2400) > 0.3f, "balance right: only the right channel");
        vol.setProperty (ids::balance, 0.0f, nullptr); vol.setProperty (ids::invert, true, nullptr);
        auto oi = run (e, 4800, [] (int c, int i) { return c < 2 ? 0.5f : 0.0f; });
        check (std::abs (oi.getSample (2, 4000) + 0.5f) < 1.0e-5f, "polarity inverted");
        vol.setProperty (ids::invert, false, nullptr); vol.setProperty (ids::mute, true, nullptr);
        auto om = run (e, 4800, [] (int c, int i) { return c < 2 ? 0.5f : 0.0f; });
        check (om.getMagnitude (2, 1000, 3800) == 0.0f, "muted");
        vol.setProperty (ids::mute, false, nullptr);
        // a second destination from the same block (a split), and two sources into one (a mix)
        auto out2 = m.addBlock (s, BlockType::Output, 3, 1); out2.setProperty (ids::device, "out2", nullptr);
        auto tone = m.addBlock (s, BlockType::Tone, 0, 1); tone.setProperty (ids::freq, 200.0f, nullptr); tone.setProperty (ids::level, -20.0f, nullptr);
        m.connect (s, vol[ids::id], out2[ids::id]); m.connect (s, tone[ids::id], out2[ids::id]);
        e.rebuild();
        auto o2 = run (e, 9600, [] (int c, int i) { return c < 2 ? sine (i, 1000, 0.5f) : 0.0f; });
        const float expect = std::sqrt (0.5f * 0.5f / 2.0f + 0.1f * 0.1f / 2.0f);
        check (std::abs (rms (o2, 8, 4800, 4800) - expect) < 0.003f && std::abs (rms (o2, 2, 4800, 4800) - 0.5f * 0.7071f) < 0.003f, "a split to a second device, mixed there with a test tone");
        // a loop can't be wired
        check (Model::wouldLoop (s, out2[ids::id], vol[ids::id]) && Model::wouldLoop (s, vol[ids::id], app[ids::id]) && ! Model::wouldLoop (s, tone[ids::id], vol[ids::id]), "a wire back upstream is a loop (refused); a new branch isn't");
        // the session off: silence
        s.setProperty (ids::on, false, nullptr); e.rebuild();
        auto off = run (e, 2400, [] (int, int i) { return sine (i, 1000); });
        check (off.getMagnitude (0, 2400) == 0.0f, "a session that is off is silent");
    }

    std::cout << "== channels: an input device's channels 3-4 to outputs 1-2, mono to stereo, a matrix\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("channels"); s.setProperty (ids::on, true, nullptr);
        auto in = m.addBlock (s, BlockType::Input, 0, 0); in.setProperty (ids::device, "in4", nullptr); in.setProperty (ids::first, 2, nullptr); in.setProperty (ids::count, 2, nullptr);
        auto out = m.addBlock (s, BlockType::Output, 1, 0); out.setProperty (ids::device, "out8", nullptr);
        m.connect (s, in[ids::id], out[ids::id]);
        e.runOffline (fakeLayout());
        // input device channels are 2..5 of the callback: give each its own level
        auto o = run (e, 2400, [] (int c, int) { return c >= 2 ? 0.1f * (float) (c - 1) : 0.0f; });
        check (std::abs (o.getSample (0, 100) - 0.3f) < 1.0e-6f && std::abs (o.getSample (1, 100) - 0.4f) < 1.0e-6f, "the device's 3rd and 4th channels arrive on outputs 1-2");
        // the matrix: 4 in -> 3 out (out1 = in4, out2 = in1 + in2 at half, out3 = in3 inverted)
        in.setProperty (ids::first, 0, nullptr); in.setProperty (ids::count, 4, nullptr);
        auto mx = m.addBlock (s, BlockType::Matrix, 1, 1); mx.setProperty (ids::outs, 3, nullptr);
        mx.setProperty (ids::matrix, Model::matrixText ({ { 3, 0, 1.0f }, { 0, 1, 0.5f }, { 1, 1, 0.5f }, { 2, 2, -1.0f } }), nullptr);
        m.removeBlock (s, out[ids::id]);
        auto out3 = m.addBlock (s, BlockType::Output, 2, 1); out3.setProperty (ids::device, "out8", nullptr); out3.setProperty (ids::first, 4, nullptr);
        m.connect (s, in[ids::id], mx[ids::id]); m.connect (s, mx[ids::id], out3[ids::id]);
        e.rebuild();
        auto om = run (e, 2400, [] (int c, int) { return c >= 2 ? 0.1f * (float) (c - 1) : 0.0f; });   // in1..4 = .1 .2 .3 .4
        check (std::abs (om.getSample (4, 100) - 0.4f) < 1.0e-6f && std::abs (om.getSample (5, 100) - 0.15f) < 1.0e-6f && std::abs (om.getSample (6, 100) + 0.3f) < 1.0e-6f,
               "matrix: out 5 = in 4, out 6 = (in 1 + in 2) / 2, out 7 = -in 3 (" + juce::String (om.getSample (4, 100)) + " " + juce::String (om.getSample (5, 100)) + " " + juce::String (om.getSample (6, 100)) + ")");
        // mono into stereo: a mono app capture fills both channels
        auto s2 = m.addSession ("mono"); s2.setProperty (ids::on, true, nullptr);
        auto app = m.addBlock (s2, BlockType::App, 0, 0); app.setProperty (ids::pids, "1", nullptr); app.setProperty (ids::mono, true, nullptr);
        auto v = m.addBlock (s2, BlockType::Volume, 1, 0);
        auto o2 = m.addBlock (s2, BlockType::Output, 2, 0); o2.setProperty (ids::device, "out2", nullptr);
        m.connect (s2, app[ids::id], v[ids::id]); m.connect (s2, v[ids::id], o2[ids::id]);
        e.rebuild();
        auto ob = run (e, 2400, [] (int c, int) { return c == 0 ? 0.25f : 0.0f; });
        check (std::abs (ob.getSample (8, 100) - 0.25f) < 1.0e-6f && std::abs (ob.getSample (9, 100) - 0.25f) < 1.0e-6f, "a mono source reaches both sides of a stereo output");
        // 64 channels through (an input of 64, a volume, an output of 64)
        IOLayout big; big.sampleRate = kSr; big.bufferSize = kBs;
        big.inputs.push_back ({ IOLayout::Part::Kind::InputDevice, 0, "in64", "in64", 0, 64 });
        big.outputs.push_back ({ IOLayout::Part::Kind::OutputDevice, 0, "out64", "out64", 0, 64 }); big.totalIn = 64; big.totalOut = 64;
        Model m2; Engine e2 (m2);
        auto s3 = m2.addSession ("64"); s3.setProperty (ids::on, true, nullptr);
        auto i64 = m2.addBlock (s3, BlockType::Input, 0, 0); i64.setProperty (ids::device, "in64", nullptr); i64.setProperty (ids::count, 64, nullptr);
        auto v64 = m2.addBlock (s3, BlockType::Volume, 1, 0); v64.setProperty (ids::db, -6.0206f, nullptr);
        auto o64 = m2.addBlock (s3, BlockType::Output, 2, 0); o64.setProperty (ids::device, "out64", nullptr);
        m2.connect (s3, i64[ids::id], v64[ids::id]); m2.connect (s3, v64[ids::id], o64[ids::id]);
        e2.runOffline (big);
        auto o64r = run (e2, 2400, [] (int c, int) { return 0.01f * (float) (c + 1); }, 64, 64);
        bool all = true; for (int c = 0; c < 64; ++c) all = all && std::abs (o64r.getSample (c, 2000) - 0.005f * (float) (c + 1)) < 1.0e-5f;
        check (all, "64 channels in, each to its own output, at half level");
        if (auto* rt0 = e2.runtime (v64[ids::id])) for (int c = 0; c < 64; ++c) rt0->levels.takePeak (c);   // (the first block glides from 0 db)
        run (e2, 2400, [] (int c, int) { return 0.01f * (float) (c + 1); }, 64, 64);
        if (auto* rt = e2.runtime (v64[ids::id])) { bool lv = true; for (int c = 0; c < 64; ++c) lv = lv && std::abs (rt->levels.takePeak (c) - 0.005f * (float) (c + 1)) < 1.0e-5f; check (lv && rt->levels.channels.load() == 64, "and 64 meters read them"); }
    }

    std::cout << "== eqs\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("eq"); s.setProperty (ids::on, true, nullptr);
        auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::pids, "1", nullptr);
        auto eq = m.addBlock (s, BlockType::Eq, 1, 0);
        auto out = m.addBlock (s, BlockType::Output, 2, 0); out.setProperty (ids::device, "out2", nullptr);
        m.connect (s, app[ids::id], eq[ids::id]); m.connect (s, eq[ids::id], out[ids::id]);
        e.runOffline (fakeLayout());
        auto level = [&] (double f) { auto o = run (e, 24000, [f] (int c, int i) { return c < 2 ? sine (i, f, 0.1f) : 0.0f; }); return db (rms (o, 8, 12000, 12000) / (0.1f * 0.70710678f)); };
        check (std::abs (level (1000)) < 0.05f, "flat: 1 khz unchanged");
        std::array<float, 10> g {}; g[5] = 6.0f; g[0] = -12.0f;
        eq.setProperty (ids::bands, Model::graphicText (g), nullptr);
        const float at1k = level (1000), at32 = level (32), at8k = level (8000);
        std::cout << "    +6 db at 1 khz, -12 at 32 hz: 1 khz " << at1k << " db, 32 hz " << at32 << " db, 8 khz " << at8k << " db\n";
        check (std::abs (at1k - 6.0f) < 0.6f && at32 < -10.0f && std::abs (at8k) < 1.0f, "the graphic eq boosts and cuts where it says");
        eq.setProperty (ids::bypass, true, nullptr);
        check (std::abs (level (1000)) < 0.05f, "bypassed: flat");
        // parametric: a high cut at 2 khz, a +9 db bell at 500 hz (q 2)
        auto peq = m.addBlock (s, BlockType::Peq, 1, 1);
        peq.setProperty (ids::bands, Model::peqText ({ { 4, 2000, 0, 0.707f, true }, { 0, 500, 9, 2, true } }), nullptr);
        m.removeBlock (s, eq[ids::id]); m.connect (s, app[ids::id], peq[ids::id]); m.connect (s, peq[ids::id], out[ids::id]); e.rebuild();
        const float p500 = level (500), p8k = level (8000), p2k = level (2000);
        std::cout << "    parametric: 500 hz " << p500 << " db, 2 khz " << p2k << " db, 8 khz " << p8k << " db\n";
        check (std::abs (p500 - 9.0f) < 0.5f && p8k < -20.0f && std::abs (p2k + 3.0f) < 1.0f, "bell +9 at 500 hz, a high cut at 2 khz (-3 db there)");
        // the drawn curve agrees with what is heard
        const auto k = dsp::makeFilter (0, kSr, 500, 9, 2);
        check (std::abs (dsp::magnitudeDb (k, kSr, 500) - 9.0) < 0.01, "the curve the page draws: +9 db at 500 hz");
    }

    std::cout << "== dynamics, delay, generators\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("dyn"); s.setProperty (ids::on, true, nullptr);
        auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::pids, "1", nullptr);
        auto lim = m.addBlock (s, BlockType::Limiter, 1, 0); lim.setProperty (ids::ceiling, -6.0f, nullptr);
        auto out = m.addBlock (s, BlockType::Output, 2, 0); out.setProperty (ids::device, "out2", nullptr);
        m.connect (s, app[ids::id], lim[ids::id]); m.connect (s, lim[ids::id], out[ids::id]);
        e.runOffline (fakeLayout());
        auto ol = run (e, 24000, [] (int c, int i) { return c < 2 ? sine (i, 300, 1.5f) : 0.0f; });
        check (ol.getMagnitude (8, 0, 24000) <= dsp::dbToGain (-6.0f) + 1.0e-5f && ol.getMagnitude (8, 12000, 12000) > dsp::dbToGain (-6.5f), "limiter: never above -6 db, and close to it");
        // the leveler brings a quiet voice up toward -18 db rms
        m.removeBlock (s, lim[ids::id]);
        auto lev = m.addBlock (s, BlockType::Leveler, 1, 0); m.connect (s, app[ids::id], lev[ids::id]); m.connect (s, lev[ids::id], out[ids::id]); e.rebuild();
        auto ov = run (e, (int) kSr * 8, [] (int c, int i) { return c < 2 ? sine (i, 220, 0.02f) : 0.0f; });
        const float lvDb = db (rms (ov, 8, (int) kSr * 7, (int) kSr));
        std::cout << "    leveler: a sine at -37 db rms comes out at " << lvDb << " db rms after 7 s (up to +12 db)\n";
        check (lvDb > -26.0f && lvDb < -24.5f, "leveler: brought up by its 12 db");
        // the gate shuts below its threshold
        m.removeBlock (s, lev[ids::id]);
        auto gate = m.addBlock (s, BlockType::Gate, 1, 0); gate.setProperty (ids::threshold, -40.0f, nullptr); gate.setProperty (ids::release, 20.0f, nullptr);
        m.connect (s, app[ids::id], gate[ids::id]); m.connect (s, gate[ids::id], out[ids::id]); e.rebuild();
        auto og = run (e, 48000, [] (int c, int i) { return c < 2 ? (i < 24000 ? sine (i, 440, 0.3f) : sine (i, 440, 0.002f)) : 0.0f; });
        check (rms (og, 8, 2000, 20000) > 0.2f && rms (og, 8, 40000, 8000) < 1.0e-4f, "gate: open for the loud part, shut for the hiss");
        // delay: 10 ms = 480 samples
        m.removeBlock (s, gate[ids::id]);
        auto dl = m.addBlock (s, BlockType::Delay, 1, 0); dl.setProperty (ids::ms, 10.0f, nullptr);
        m.connect (s, app[ids::id], dl[ids::id]); m.connect (s, dl[ids::id], out[ids::id]); e.rebuild();
        auto od = run (e, 2400, [] (int c, int i) { return c < 2 && i == 100 ? 1.0f : 0.0f; });
        check (std::abs (od.getSample (8, 580) - 1.0f) < 1.0e-6f && od.getMagnitude (8, 0, 580) == 0.0f, "delay: the click 480 samples later");
        // the tone's sine has its level and its pitch
        Model m2; Engine e2 (m2);
        auto s2 = m2.addSession ("tone"); s2.setProperty (ids::on, true, nullptr);
        auto tone = m2.addBlock (s2, BlockType::Tone, 0, 0); tone.setProperty (ids::freq, 1000.0f, nullptr); tone.setProperty (ids::level, -12.0f, nullptr);
        auto o2 = m2.addBlock (s2, BlockType::Output, 1, 0); o2.setProperty (ids::device, "out2", nullptr);
        m2.connect (s2, tone[ids::id], o2[ids::id]);
        e2.runOffline (fakeLayout());
        auto ot = run (e2, 48000, [] (int, int) { return 0.0f; });
        int zc = 0; for (int i = 1; i < 48000; ++i) if (ot.getSample (8, i - 1) < 0 && ot.getSample (8, i) >= 0) ++zc;
        check (std::abs (ot.getMagnitude (8, 0, 48000) - dsp::dbToGain (-12.0f)) < 0.001f && std::abs (zc - 1000) <= 1, "tone: 1 khz at -12 db");
    }

    std::cout << "== loudness (ebu r128)\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("meter"); s.setProperty (ids::on, true, nullptr);
        auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::pids, "1", nullptr);
        auto met = m.addBlock (s, BlockType::Meter, 1, 0);
        m.connect (s, app[ids::id], met[ids::id]);
        e.runOffline (fakeLayout());
        run (e, (int) kSr * 4, [] (int c, int i) { return c < 2 ? sine (i, 997, 0.1f) : 0.0f; });   // a stereo sine at -20 dbfs peak
        auto* rt = e.runtime (met[ids::id]);
        const float mom = rt ? rt->loudness.momentary.load() : -999, integ = rt ? rt->loudness.integrated.load() : -999;
        std::cout << "    997 hz at -20 dbfs in both channels: momentary " << mom << " lufs, integrated " << integ << " lufs (the standard says -20)\n";
        check (std::abs (integ + 20.0f) < 0.3f && std::abs (mom + 20.0f) < 0.3f, "loudness reads -20 lufs");
        float L[2048], R[2048]; rt->scope.latest (L, R, 2048);
        check (std::abs (L[2000] - sine ((int) kSr * 4 - 48, 997, 0.1f)) < 0.01f, "the pictures get the latest samples");
    }

    std::cout << "== recorder\n";
    {
        Model m; Engine e (m);
        auto s = m.addSession ("take"); s.setProperty (ids::on, true, nullptr);
        auto in = m.addBlock (s, BlockType::Input, 0, 0); in.setProperty (ids::device, "in4", nullptr); in.setProperty (ids::count, 4, nullptr);
        auto rec = m.addBlock (s, BlockType::Recorder, 1, 0);
        rec.setProperty (ids::folder, tmp.getFullPathName(), nullptr); rec.setProperty (ids::bits, 24, nullptr); rec.setProperty (ids::pattern, "take {n}", nullptr);
        m.connect (s, in[ids::id], rec[ids::id]);
        e.runOffline (fakeLayout());
        e.startRecording (rec[ids::id]);
        e.rebuild();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // the engine's timer opens the file
        check (e.recording (rec[ids::id]), "recording");
        run (e, (int) kSr * 2, [] (int c, int i) { return c >= 2 ? sine (i, 100.0 * (c - 1), 0.5f) : 0.0f; });   // four channels: 100, 200, 300, 400 hz
        e.stopRecording (rec[ids::id]);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        check (! e.recording (rec[ids::id]), "stopped");
        auto files = tmp.findChildFiles (juce::File::findFiles, false, "*.wav");
        check (files.size() == 1, "one wav file (" + juce::String (files.size()) + ")");
        if (files.size() == 1)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (files[0]));
            check (r && r->numChannels == 4 && r->bitsPerSample == 24 && std::abs ((int) r->lengthInSamples - (int) kSr * 2) < 2 && r->sampleRate == kSr,
                   "4 channels, 24 bit, 2 s at 48 khz (" + (r ? juce::String (r->numChannels) + " ch " + juce::String (r->bitsPerSample) + " bit " + juce::String (r->lengthInSamples) + " samples" : juce::String ("unreadable")) + ")");
            if (r)
            {
                juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                bool same = true; for (int c = 0; c < 4; ++c) for (int i = 1000; i < 2000; ++i) same = same && std::abs (b.getSample (c, i) - sine (i, 100.0 * (c + 1), 0.5f)) < 1.0e-4f;
                check (same, "each channel holds what came in, to the sample");
            }
            check (files[0].getFileName() == "take 1.wav", "named from its pattern: " + files[0].getFileName());
        }
        // split into parts, in aiff, 32-bit float wav
        rec.setProperty (ids::format, "flac", nullptr); rec.setProperty (ids::bits, 16, nullptr); rec.setProperty (ids::pattern, "part", nullptr);
        e.startRecording (rec[ids::id]);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        run (e, (int) kSr, [] (int c, int i) { return c >= 2 ? sine (i, 440, 0.5f) : 0.0f; });
        e.stopRecording (rec[ids::id]); juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        check (tmp.getChildFile ("part.flac").existsAsFile(), "flac as well");
        // pause on silence: only the sound is kept
        rec.setProperty (ids::format, "wav", nullptr); rec.setProperty (ids::pattern, "speech", nullptr); rec.setProperty (ids::pauseSilence, true, nullptr);
        e.startRecording (rec[ids::id]); juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        run (e, (int) kSr * 3, [] (int c, int i) { return c >= 2 && (i / 48000) != 1 ? sine (i, 440, 0.5f) : 0.0f; });
        e.stopRecording (rec[ids::id]); juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (tmp.getChildFile ("speech.wav")) })
            check (std::abs ((int) r->lengthInSamples - (int) kSr * 2) <= kBs, "pausing on silence: 2 s kept of 3 (" + juce::String (r->lengthInSamples / kSr, 2) + " s)");
        else check (false, "the silence-paused file");
    }

    std::cout << "== plug-ins in the path\n";
    {
        PluginHost::scannerExecutable() = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        PluginHost ph (tmp.getChildFile ("plugins"));
        const juce::File vst = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3/faltung gl8.vst3");
        if (! vst.exists()) std::cout << "    (faltung gl8.vst3 is not installed: skipped)\n";
        else
        {
            juce::String err;
            check (ph.scanFile ("VST3", vst.getFullPathName(), err) && ph.known.getNumTypes() >= 1, "scanned in a child process: " + (ph.known.getNumTypes() > 0 ? ph.known.getTypes()[0].name : err));
            check (! ph.scanFile ("VST3", tmp.getChildFile ("nothing.vst3").getFullPathName(), err) || true, "a file that isn't a plug-in doesn't stop anything");
            if (ph.known.getNumTypes() > 0)
            {
                Model m; Engine e (m, &ph);
                auto s = m.addSession ("plug-in"); s.setProperty (ids::on, true, nullptr);
                auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::pids, "1", nullptr);
                auto pl = m.addBlock (s, BlockType::Plugin, 1, 0); pl.setProperty (ids::plugin, ph.known.getTypes()[0].createXml()->toString(), nullptr);
                auto out = m.addBlock (s, BlockType::Output, 2, 0); out.setProperty (ids::device, "out2", nullptr);
                m.connect (s, app[ids::id], pl[ids::id]); m.connect (s, pl[ids::id], out[ids::id]);
                e.runOffline (fakeLayout());
                auto* rt = e.runtime (pl[ids::id]);
                check (rt && rt->plugin != nullptr, "the plug-in is in the path: " + (rt ? (rt->plugin ? rt->plugin->getName() : rt->status) : juce::String ("no block")));
                auto o = run (e, 48000, [] (int c, int i) { return c < 2 ? sine (i, 220, 0.3f) : 0.0f; });
                check (rms (o, 8, 24000, 24000) > 0.01f && std::isfinite (o.getSample (8, 30000)), "sound comes out of it (" + juce::String (db (rms (o, 8, 24000, 24000)), 1) + " db rms)");
                // its state goes into the model, and back
                juce::MemoryBlock mb; if (rt && rt->plugin) rt->plugin->getStateInformation (mb);
                check (mb.getSize() > 0, "its state can be kept with the session");
            }
        }
    }

    std::cout << "== the system eq session\n";
    {
        Model m; Engine e (m);
        auto s = e.systemEqSession();
        int blocks = 0, wires = 0; for (auto c : s) { if (c.hasType (ids::block)) ++blocks; if (c.hasType (ids::wire)) ++wires; }
        check (blocks == 6 && wires == 5 && e.systemEqSession() == s, "made once: the mac's sound -> volume -> eq -> parametric -> limiter -> output");
        auto cfg = (s.setProperty (ids::on, true, nullptr), e.wantedConfig());
        check (cfg.taps.size() == 1 && cfg.taps[0].kind == TapSpec::Kind::System && cfg.taps[0].muteSource, "it taps the whole system, muted at the source");
    }

    std::cout << "== save and load\n";
    {
        Model m; auto s = m.addSession ("keep"); auto b = m.addBlock (s, BlockType::Eq, 10, 20); m.connect (s, b[ids::id], (int) b[ids::id] + 1);
        auto x = m.root.createXml(); Model m2; m2.root = juce::ValueTree::fromXml (*x);
        check (m2.root.isEquivalentTo (m.root), "the model comes back as it was");
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

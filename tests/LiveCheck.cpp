// umleitung — the whole engine on the real CoreAudio of this mac, silently (BlackHole as the loopback; nothing on
// the speakers, no default device changed):
//   1. session "play":   test tone 1 khz -12 db → BlackHole 16ch outputs 1-2
//      session "record": BlackHole 16ch inputs 1-2 → volume -6 db → graphic eq +6 db at 1 khz → recorder (wav) + meter
//      → the file holds 1 khz at -12 db; both sessions ran in one aggregate, on one clock
//   2. an app (the tone helper, playing 523 hz into BlackHole 2ch), captured by bundle id with its own output muted
//      → volume → recorder: the file holds 523 hz
#include "engine/Engine.h"
#include "engine/PluginHost.h"
#include <iostream>

using namespace um;
static int failures = 0;
static void check (bool ok, const juce::String& what) { std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n"; if (! ok) ++failures; }
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** peak, and frequency from zero crossings, of channel 0 after skipping the start */
static bool analyse (const juce::File& f, float& peak, double& hz, int& channels, double& seconds)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r) return false;
    channels = (int) r->numChannels; seconds = (double) r->lengthInSamples / r->sampleRate;
    const int skip = (int) (0.5 * r->sampleRate), n = (int) r->lengthInSamples - skip - (int) (0.2 * r->sampleRate);
    if (n <= 1000) return false;
    juce::AudioBuffer<float> b ((int) r->numChannels, n); r->read (&b, 0, n, skip, true, true);
    peak = b.getMagnitude (0, 0, n);
    int zc = 0; for (int i = 1; i < n; ++i) if (b.getSample (0, i - 1) < 0 && b.getSample (0, i) >= 0) ++zc;
    hz = zc / ((double) n / r->sampleRate);
    return true;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("umleitung live check");
    tmp.deleteRecursively(); tmp.createDirectory();
    bool haveBH = false; for (auto& d : CoreAudioHost::devices()) if (d.uid == "BlackHole16ch_UID") haveBH = true;
    if (! haveBH) { std::cout << "BlackHole 16ch is not installed: nothing to check against\n"; return 0; }

    std::cout << "== two sessions on one clock: a tone out through BlackHole 16ch, back in, processed, recorded\n";
    {
        Model m; Engine e (m);
        m.settings().setProperty (ids::device, "BlackHole16ch_UID", nullptr);
        auto a = m.addSession ("play"); auto b = m.addSession ("record");
        auto tone = m.addBlock (a, BlockType::Tone, 0, 0); tone.setProperty (ids::freq, 1000.0f, nullptr); tone.setProperty (ids::level, -12.0f, nullptr);
        auto out = m.addBlock (a, BlockType::Output, 1, 0); out.setProperty (ids::device, "BlackHole16ch_UID", nullptr);
        m.connect (a, tone[ids::id], out[ids::id]);
        auto in = m.addBlock (b, BlockType::Input, 0, 0); in.setProperty (ids::device, "BlackHole16ch_UID", nullptr); in.setProperty (ids::count, 2, nullptr);
        auto vol = m.addBlock (b, BlockType::Volume, 1, 0); vol.setProperty (ids::db, -6.0f, nullptr);
        auto eq = m.addBlock (b, BlockType::Eq, 2, 0); std::array<float, 10> g {}; g[5] = 6.0f; eq.setProperty (ids::bands, Model::graphicText (g), nullptr);
        auto rec = m.addBlock (b, BlockType::Recorder, 3, 0); rec.setProperty (ids::folder, tmp.getFullPathName(), nullptr); rec.setProperty (ids::pattern, "loop", nullptr);
        auto met = m.addBlock (b, BlockType::Meter, 3, 1);
        m.connect (b, in[ids::id], vol[ids::id]); m.connect (b, vol[ids::id], eq[ids::id]); m.connect (b, eq[ids::id], rec[ids::id]); m.connect (b, eq[ids::id], met[ids::id]);
        a.setProperty (ids::on, true, nullptr); b.setProperty (ids::on, true, nullptr);
        e.rebuild(); pump (300);
        check (e.running(), "the engine runs (" + (e.lastError().isNotEmpty() ? e.lastError() : juce::String (e.currentLayout().totalIn) + " in, " + juce::String (e.currentLayout().totalOut) + " out at " + juce::String (e.sampleRate())) + ")");
        pump (3000);
        check (e.recording (rec[ids::id]), "recording");
        if (auto* rt = e.runtime (met[ids::id])) std::cout << "    the meter block reads " << rt->loudness.momentary.load() << " lufs momentary\n";
        e.stopRecording (rec[ids::id]); pump (500);
        float pk = 0; double hz = 0, secs = 0; int ch = 0;
        const bool ok = analyse (tmp.getChildFile ("loop.wav"), pk, hz, ch, secs);
        std::cout << "    loop.wav: " << ch << " ch, " << secs << " s, peak " << dsp::gainToDb (pk) << " db, " << hz << " hz\n";
        check (ok && std::abs (dsp::gainToDb (pk) + 12.0f) < 0.5f && std::abs (hz - 1000.0) < 3.0 && ch == 2 && secs > 2.5, "the recording holds the tone: 1 khz at -12 db (-12, -6, +6)");
        a.setProperty (ids::on, false, nullptr); b.setProperty (ids::on, false, nullptr); e.rebuild(); pump (300);
        check (! e.running(), "everything off: the engine stops");
    }

    std::cout << "== an app captured by bundle id, muted at its source, recorded\n";
    {
        const juce::File toneApp (UM_TONE_PATH);
        if (! toneApp.existsAsFile()) { std::cout << "    (the tone helper isn't built: skipped)\n"; }
        else
        {
            Model m; Engine e (m);
            m.settings().setProperty (ids::device, "BlackHole16ch_UID", nullptr);
            auto s = m.addSession ("app");
            auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::bundles, "studio.umleitung.hostcheck.tone", nullptr); app.setProperty (ids::mute, true, nullptr);
            auto vol = m.addBlock (s, BlockType::Volume, 1, 0);
            auto rec = m.addBlock (s, BlockType::Recorder, 2, 0); rec.setProperty (ids::folder, tmp.getFullPathName(), nullptr); rec.setProperty (ids::pattern, "app", nullptr);
            m.connect (s, app[ids::id], vol[ids::id]); m.connect (s, vol[ids::id], rec[ids::id]);
            juce::ChildProcess helper;
            check (helper.start (juce::StringArray { toneApp.getFullPathName(), "BlackHole2ch_UID", "523", "0.2", "8" }, 0), "the tone helper plays 523 hz into BlackHole 2ch");
            pump (800);
            s.setProperty (ids::on, true, nullptr); e.rebuild(); pump (4000);
            check (e.running(), "the engine runs with the tap (" + e.lastError() + ")");
            e.stopRecording (rec[ids::id]); pump (500);
            float pk = 0; double hz = 0, secs = 0; int ch = 0;
            const bool ok = analyse (tmp.getChildFile ("app.wav"), pk, hz, ch, secs);
            std::cout << "    app.wav: " << ch << " ch, " << secs << " s, peak " << dsp::gainToDb (pk) << " db, " << hz << " hz;  capture permission: "
                      << (CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Granted ? "granted" : CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Denied ? "denied" : "unknown") << "\n";
            check (ok && std::abs (hz - 523.0) < 3.0 && std::abs (pk - 0.2f) < 0.02f, "the app's sound is in the recording (523 hz at its own level)");
            s.setProperty (ids::on, false, nullptr); e.rebuild(); pump (300);
            helper.kill();
        }
    }
    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

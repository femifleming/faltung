// umleitung — the whole engine on the real CoreAudio of this mac, silently (BlackHole as the loopback; nothing on
// the speakers, no default device changed):
//   1. session "play":   test tone 1 khz -12 db → BlackHole 16ch outputs 1-2
//      session "record": BlackHole 16ch inputs 1-2 → volume -6 db → graphic eq +6 db at 1 khz → recorder (wav) + meter
//      → the file holds 1 khz at -12 db; both sessions ran in one aggregate, on one clock
//   2. an app (the tone helper, playing 523 hz into BlackHole 2ch), captured by bundle id with its own output muted
//      → volume → recorder: the file holds 523 hz
#include "engine/Engine.h"
#include "engine/PluginHost.h"
#include "platform/Apps.h"
#include "platform/DriverControl.h"
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
    std::cout << "== an app that plays through a helper inside its bundle (like chrome), chosen by the app's own id\n";
    {
        const juce::File toneApp (UM_TONE_PATH);
        const auto toneBundle = toneApp.getParentDirectory().getParentDirectory().getParentDirectory();   // …/umleitung_host_check_tone.app
        if (! toneApp.existsAsFile()) std::cout << "    (the tone helper isn't built: skipped)\n";
        else
        {
            // Fake App.app (studio.umleitung.fakeapp) with Contents/Frameworks/Fake Helper.app (org.example.unrelated.helper) that plays
            const auto fake = tmp.getChildFile ("Fake App.app"), helper = fake.getChildFile ("Contents/Frameworks/Fake Helper.app");
            fake.getChildFile ("Contents/MacOS").createDirectory(); helper.getParentDirectory().createDirectory();
            fake.getChildFile ("Contents/Info.plist").replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict><key>CFBundleIdentifier</key><string>studio.umleitung.fakeapp</string><key>CFBundleName</key><string>Fake App</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>");
            juce::ChildProcess cp; cp.start (juce::StringArray { "/usr/bin/ditto", toneBundle.getFullPathName(), helper.getFullPathName() }); cp.waitForProcessToFinish (10000);
            auto plist = helper.getChildFile ("Contents/Info.plist");
            juce::ChildProcess pb; pb.start (juce::StringArray { "/usr/libexec/PlistBuddy", "-c", "Set :CFBundleIdentifier org.example.unrelated.helper", plist.getFullPathName() }); pb.waitForProcessToFinish (5000);
            juce::ChildProcess cs; cs.start (juce::StringArray { "/usr/bin/codesign", "--force", "--deep", "-s", "-", fake.getFullPathName() }); cs.waitForProcessToFinish (20000);
            const auto exe = helper.getChildFile ("Contents/MacOS/umleitung_host_check_tone");
            const auto owner = owningApp (exe.getFullPathName());
            check (owner.bundleId == "studio.umleitung.fakeapp" && owner.name == "Fake App", "the helper belongs to Fake App (" + owner.bundleId + ", " + owner.name + ")");
            juce::ChildProcess h;
            check (h.start (juce::StringArray { exe.getFullPathName(), "BlackHole2ch_UID", "659", "0.2", "9" }, 0), "the helper plays 659 hz");
            pump (1000);
            Model m; Engine e (m);
            m.settings().setProperty (ids::device, "BlackHole16ch_UID", nullptr);
            auto s = m.addSession ("fake");
            auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::bundles, "studio.umleitung.fakeapp", nullptr); app.setProperty (ids::mute, true, nullptr);
            auto rec = m.addBlock (s, BlockType::Recorder, 1, 0); rec.setProperty (ids::folder, tmp.getFullPathName(), nullptr); rec.setProperty (ids::pattern, "fake", nullptr);
            m.connect (s, app[ids::id], rec[ids::id]);
            const auto cfg = (s.setProperty (ids::on, true, nullptr), e.wantedConfig());
            juce::String ids2; if (! cfg.taps.empty()) for (auto& x : cfg.taps[0].bundleIds) ids2 << x << " ";
            check (! cfg.taps.empty() && ids2.contains ("org.example.unrelated.helper"), "choosing the app takes its helper in: " + ids2.trim());
            e.rebuild(); pump (4000);
            e.stopRecording (rec[ids::id]); pump (500);
            float pk = 0; double hz = 0, secs = 0; int ch = 0;
            const bool ok = analyse (tmp.getChildFile ("fake.wav"), pk, hz, ch, secs);
            std::cout << "    fake.wav: " << secs << " s, peak " << dsp::gainToDb (pk) << " db, " << hz << " hz\n";
            check (ok && std::abs (hz - 659.0) < 3.0 && pk > 0.15f, "the helper's sound is captured when the app is chosen");
            s.setProperty (ids::on, false, nullptr); e.rebuild(); pump (300);
            h.kill();
        }
    }

    std::cout << "== google chrome, if it is running (unmuted, only measured: nothing is recorded or kept)\n";
    {
        bool chrome = false; for (auto& a : runningApps()) if (a.bundleId == "com.google.Chrome") chrome = true;
        bool playing = false; for (auto& pr : CoreAudioHost::audioProcesses()) if (pr.playing && owningApp (pr.path).bundleId == "com.google.Chrome") playing = true;
        check (! chrome || [] { for (auto& a : runningApps()) if (a.bundleId == "com.google.Chrome" && a.name == "Google Chrome") return true; return false; }(), "google chrome is on the app list by its name");
        if (! chrome) std::cout << "    (chrome isn't running)\n";
        else if (! playing) std::cout << "    (chrome isn't playing anything right now: nothing to measure)\n";
        else
        {
            Model m; Engine e (m);
            m.settings().setProperty (ids::device, "BlackHole16ch_UID", nullptr);
            auto s = m.addSession ("chrome");
            auto app = m.addBlock (s, BlockType::App, 0, 0); app.setProperty (ids::bundles, "com.google.Chrome", nullptr);
            auto met = m.addBlock (s, BlockType::Meter, 1, 0);
            m.connect (s, app[ids::id], met[ids::id]);
            s.setProperty (ids::on, true, nullptr); e.rebuild(); pump (2500);
            float pk = 0; if (auto* rt = e.runtime (met[ids::id])) for (int c = 0; c < 2; ++c) pk = std::max (pk, rt->levels.takePeak (c));
            float lu = -120; if (auto* rt = e.runtime (met[ids::id])) lu = rt->loudness.momentary.load();
            std::cout << "    chrome through umleitung: peak " << dsp::gainToDb (pk) << " db, " << lu << " lufs momentary\n";
            check (pk > 1.0e-4f, "chrome's sound reaches umleitung");
            s.setProperty (ids::on, false, nullptr); e.rebuild(); pump (300);
        }
    }

    std::cout << "== umleitung's own driver (installed): devices made and removed by the app, a loopback through one\n";
    if (! DriverControl::installed()) std::cout << "    (the driver isn't installed: skipped)\n";
    else
    {
        const auto before = DriverControl::devices();
        std::cout << "    driver " << DriverControl::installedVersion() << ", " << before.size() << " device(s):"; for (auto& d : before) std::cout << " \"" << d.name << "\" (" << d.channels << " ch)"; std::cout << "\n";
        auto want = before; VirtualDevice t; t.name = "umleitung check 16"; t.channels = 16; want.push_back (t);
        juce::String err;
        check (DriverControl::setDevices (want, err), "a 16-channel device added: " + err);
        juce::String uid; int ch = 0;
        for (int k = 0; k < 40 && uid.isEmpty(); ++k) { pump (100); for (auto& d : CoreAudioHost::devices()) if (d.name == "umleitung check 16") { uid = d.uid; ch = d.inChannels; } }
        check (uid.isNotEmpty() && ch == 16, "it appears to the whole mac (" + uid + ", " + juce::String (ch) + " ch)");
        if (uid.isNotEmpty())
        {
            Model m; Engine e (m);
            m.settings().setProperty (ids::device, uid, nullptr);
            auto a = m.addSession ("in"); auto b = m.addSession ("out");
            auto tone = m.addBlock (a, BlockType::Tone, 0, 0); tone.setProperty (ids::freq, 750.0f, nullptr); tone.setProperty (ids::level, -9.0f, nullptr);
            auto mx = m.addBlock (a, BlockType::Matrix, 1, 0); mx.setProperty (ids::outs, 16, nullptr); mx.setProperty (ids::matrix, Model::matrixText ({ { 0, 12, 1.0f }, { 1, 13, 0.5f } }), nullptr);
            auto out = m.addBlock (a, BlockType::Output, 2, 0); out.setProperty (ids::device, uid, nullptr);
            m.connect (a, tone[ids::id], mx[ids::id]); m.connect (a, mx[ids::id], out[ids::id]);
            auto in = m.addBlock (b, BlockType::Input, 0, 0); in.setProperty (ids::device, uid, nullptr); in.setProperty (ids::first, 12, nullptr); in.setProperty (ids::count, 2, nullptr);
            auto rec = m.addBlock (b, BlockType::Recorder, 1, 0); rec.setProperty (ids::folder, tmp.getFullPathName(), nullptr); rec.setProperty (ids::pattern, "driver", nullptr);
            m.connect (b, in[ids::id], rec[ids::id]);
            a.setProperty (ids::on, true, nullptr); b.setProperty (ids::on, true, nullptr); e.rebuild(); pump (3000);
            e.stopRecording (rec[ids::id]); pump (500);
            float pk = 0; double hz = 0, secs = 0; int chs = 0;
            const bool ok = analyse (tmp.getChildFile ("driver.wav"), pk, hz, chs, secs);
            float pk2 = 0; { juce::AudioFormatManager fm; fm.registerBasicFormats(); if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (tmp.getChildFile ("driver.wav")) }) { juce::AudioBuffer<float> bb (2, (int) r->lengthInSamples); r->read (&bb, 0, (int) r->lengthInSamples, 0, true, true); pk2 = bb.getMagnitude (1, (int) (r->sampleRate * 0.5), (int) r->lengthInSamples - (int) (r->sampleRate * 0.7)); } }
            std::cout << "    through the driver, channels 13-14: " << dsp::gainToDb (pk) << " db / " << dsp::gainToDb (pk2) << " db, " << hz << " hz\n";
            check (ok && std::abs (dsp::gainToDb (pk) + 9.0f) < 0.05f && std::abs (dsp::gainToDb (pk2) + 15.02f) < 0.05f && std::abs (hz - 750.0) < 2.0, "a loopback through umleitung's device: channel 13 at -9 db, 14 at -15 db, 750 hz");
            a.setProperty (ids::on, false, nullptr); b.setProperty (ids::on, false, nullptr); e.rebuild(); pump (500);
        }
        check (DriverControl::setDevices (before, err), "the devices put back as they were: " + err);
        pump (1500);
        bool gone = true; for (auto& d : CoreAudioHost::devices()) if (d.name == "umleitung check 16") gone = false;
        check (gone && DriverControl::devices().size() == before.size(), "the test device is gone");
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all good\n" : juce::String (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}

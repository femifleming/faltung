// umleitung host check — proves CoreAudioHost on this machine: devices and processes, the aggregate engine (loopback
// through BlackHole with exact channel mapping and latency, two devices with drift compensation, rates, restart, a
// device vanishing) and process taps (muted / unmuted, system tap without umleitung, bundle id of an app started
// later, a quiet afplay on the default output with the source muted).
// Never changes the default devices; plays nothing audible (BlackHole only, plus a -60 dBFS muted afplay).
// usage: umleitung_host_check [list] [loop] [two] [hw] [rates] [restart] [remove] [taps]   (none: all)
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include "platform/CoreAudioHost.h"
#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/wait.h>

extern char** environ;
using namespace um;

static int failures = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); printf ("  [%s] ", ok_ ? " ok " : "FAIL"); printf (__VA_ARGS__); printf ("\n"); fflush (stdout); if (! ok_) ++failures; } while (0)

static const char* BH2 = "BlackHole2ch_UID";
static const char* BH16 = "BlackHole16ch_UID";
static const char* BH64 = "BlackHole64ch_UID";
static const char* BH128 = "BlackHole128ch_UID";

static void pump (double seconds)
{
    auto end = CFAbsoluteTimeGetCurrent() + seconds;
    while (CFAbsoluteTimeGetCurrent() < end)
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, std::min (0.05, end - CFAbsoluteTimeGetCurrent()), false);
}

static AudioObjectID devId (const char* uid)
{
    for (auto& d : CoreAudioHost::devices()) if (d.uid == uid) return d.id;
    return 0;
}
static double rateOf (const char* uid)
{
    Float64 r = 0; UInt32 s = sizeof (r);
    AudioObjectPropertyAddress a { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData (devId (uid), &a, 0, nullptr, &s, &r);
    return r;
}
static void setRate (const char* uid, double r)
{
    AudioObjectPropertyAddress a { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    Float64 v = r;
    AudioObjectSetPropertyData (devId (uid), &a, 0, nullptr, sizeof (v), &v);
    for (int i = 0; i < 100 && rateOf (uid) != r; ++i) usleep (10000);
}

// ------------------------------------------------------------------------------------------------ the test callback
struct Gen { enum Kind { Silence, Ramp, Sine } kind = Silence; double hz = 0, amp = 0; };

struct TestCallback : AudioCallback
{
    std::vector<Gen> gens;              // per flat output (missing: silence)
    int warmup = 9600, recordFrames = 48000;
    // results
    IOLayout lay;
    std::vector<std::vector<float>> rec; // per flat input
    std::atomic<int64_t> frames { 0 };
    std::atomic<int> recorded { 0 };
    int prepares = 0, stops = 0;
    double phase[256] = {};

    void prepare (const IOLayout& l) override
    {
        lay = l;
        ++prepares;
        rec.assign ((size_t) l.totalIn, std::vector<float> ((size_t) recordFrames, 0.0f));
        frames = 0;
        recorded = 0;
        recStart = -1;
        for (auto& p : phase) p = 0;
    }
    void stopped() override { ++stops; }
    void process (const float* const* in, int numIn, float* const* out, int numOut, int n) override
    {
        const int64_t t = frames.load (std::memory_order_relaxed);
        for (int ch = 0; ch < numOut && ch < (int) gens.size(); ++ch)
        {
            const auto& g = gens[(size_t) ch];
            if (g.kind == Gen::Ramp)
                for (int i = 0; i < n; ++i) out[ch][i] = (float) ch + (float) ((t + i) & 0xffff) / 65536.0f;
            else if (g.kind == Gen::Sine)
            {
                const double inc = 2 * M_PI * g.hz / lay.sampleRate;
                for (int i = 0; i < n; ++i) { out[ch][i] = (float) (g.amp * std::sin (phase[ch])); phase[ch] += inc; }
                phase[ch] = std::fmod (phase[ch], 2 * M_PI);
            }
        }
        // record after the warm-up: frame index t+i in the input matches what was written at that same index
        int r = recorded.load (std::memory_order_relaxed);
        for (int i = 0; i < n && r < recordFrames; ++i)
        {
            if (t + i < warmup) continue;
            for (int ch = 0; ch < numIn; ++ch) rec[(size_t) ch][(size_t) r] = in[ch][i];
            ++r;
        }
        recorded.store (r, std::memory_order_relaxed);
        recStart = recStart < 0 && r > 0 ? std::max<int64_t> (t, warmup) : recStart;
        frames.store (t + n, std::memory_order_relaxed);
    }
    int64_t recStart = -1;

    /** a ramp channel: the marker (which output it came from), how many samples carry it, the latency (mode) */
    struct RampResult { int marker = -1; double share = 0; int latency = -1; };
    RampResult ramp (int inCh) const
    {
        RampResult res;
        std::map<int, int> markers, lat;
        const int n = recorded.load();
        for (int i = 0; i < n; ++i)
        {
            const float v = rec[(size_t) inCh][(size_t) i];
            if (v == 0.0f) { markers[-1]++; continue; }
            const int m = (int) std::floor (v);
            markers[m]++;
            const int written = (int) std::lround ((v - (float) m) * 65536.0f) & 0xffff;
            const int now = (int) ((recStart + i) & 0xffff);
            lat[(now - written) & 0xffff]++;
        }
        int best = 0;
        for (auto& [m, c] : markers) if (c > best) { best = c; res.marker = m; }
        res.share = n > 0 ? (double) best / n : 0;
        best = 0;
        for (auto& [l, c] : lat) if (c > best) { best = c; res.latency = l; }
        return res;
    }
    /** amplitude of a sine at hz on an input (Goertzel over the recording) */
    double toneAmp (int inCh, double hz) const
    {
        const int n = recorded.load();
        if (n <= 0 || inCh >= (int) rec.size()) return 0;
        const double w = 2 * M_PI * hz / lay.sampleRate, k = 2 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) { const double s = rec[(size_t) inCh][(size_t) i] + k * s1 - s2; s2 = s1; s1 = s; }
        const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
        return 2 * std::sqrt (re * re + im * im) / n;
    }
    double rms (int inCh) const
    {
        const int n = recorded.load();
        double s = 0;
        for (int i = 0; i < n; ++i) s += (double) rec[(size_t) inCh][(size_t) i] * rec[(size_t) inCh][(size_t) i];
        return n > 0 ? std::sqrt (s / n) : 0;
    }
};

static bool runFor (CoreAudioHost& host, TestCallback& cb, double maxSeconds)
{
    auto end = CFAbsoluteTimeGetCurrent() + maxSeconds;
    while (CFAbsoluteTimeGetCurrent() < end && cb.recorded.load() < cb.recordFrames && host.running()) pump (0.05);
    return cb.recorded.load() >= cb.recordFrames;
}

static void printLayout (const IOLayout& l)
{
    printf ("  layout %.0f Hz / %d: in %d, out %d\n", l.sampleRate, l.bufferSize, l.totalIn, l.totalOut);
    for (auto& p : l.inputs)  printf ("    in  %-5s #%d %-28s ch %d..%d\n", p.kind == IOLayout::Part::Kind::Tap ? "tap" : "dev", p.index, p.name.toRawUTF8(), p.firstChannel, p.firstChannel + p.numChannels - 1);
    for (auto& p : l.outputs) printf ("    out dev   #%d %-28s ch %d..%d\n", p.index, p.name.toRawUTF8(), p.firstChannel, p.firstChannel + p.numChannels - 1);
}

/** every input channel must carry the ramp of output `expect[ch]` (−1: don't check); returns the latencies seen */
static std::map<int, int> checkRamp (const TestCallback& cb, const std::vector<int>& expect, const char* what)
{
    std::map<int, int> lat;
    int good = 0, checked = 0;
    for (size_t ch = 0; ch < expect.size(); ++ch)
    {
        if (expect[ch] < 0) continue;
        ++checked;
        auto r = cb.ramp ((int) ch);
        if (r.marker == expect[ch] && r.share > 0.99) { ++good; lat[r.latency]++; }
        else printf ("    in %zu: marker %d (want %d) share %.3f\n", ch, r.marker, expect[ch], r.share);
    }
    juce::String l;
    for (auto& [k, v] : lat) l << k << " frames x" << v << "  ";
    CHECK (good == checked && checked > 0, "%s: %d/%d inputs carry exactly the expected output's signal; latency %s", what, good, checked, l.toRawUTF8());
    return lat;
}

static pid_t spawn (std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back (a.data());
    argv.push_back (nullptr);
    pid_t pid = 0;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init (&fa);
    posix_spawn_file_actions_addopen (&fa, 1, "/dev/null", O_WRONLY, 0);
    if (posix_spawn (&pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) pid = 0;
    posix_spawn_file_actions_destroy (&fa);
    return pid;
}
static void reap (pid_t p) { if (p > 0) { kill (p, SIGTERM); int st; waitpid (p, &st, 0); } }

/** wait until CoreAudio sees the process running output */
static bool waitPlaying (pid_t pid, double secs)
{
    auto end = CFAbsoluteTimeGetCurrent() + secs;
    while (CFAbsoluteTimeGetCurrent() < end)
    {
        for (auto& p : CoreAudioHost::audioProcesses()) if (p.pid == pid && p.playing) return true;
        pump (0.05);
    }
    return false;
}

static const char* permName (CoreAudioHost::Permission p)
{
    return p == CoreAudioHost::Permission::Granted ? "Granted" : p == CoreAudioHost::Permission::Denied ? "Denied" : "Unknown";
}

// ------------------------------------------------------------------------------------------------ sections
static void sectionList (CoreAudioHost& host)
{
    printf ("\n== devices and processes\n");
    auto devs = CoreAudioHost::devices();
    for (auto& d : devs)
        printf ("  %-30s %-24s in %3d out %3d %6.0f Hz %-9s%s%s%s%s%s%s vol %s rates %d  [%s]\n", d.name.toRawUTF8(), d.manufacturer.toRawUTF8(), d.inChannels, d.outChannels,
                d.sampleRate, d.transport.toRawUTF8(), d.isDefaultOutput ? " DEF-OUT" : "", d.isDefaultInput ? " DEF-IN" : "", d.isDefaultSystemOutput ? " DEF-SYS" : "",
                d.isAggregate ? " aggregate" : "", d.isHidden ? " hidden" : "", d.isOurs ? " OURS" : "", d.hasVolume ? "yes" : "no", (int) d.sampleRates.size(), d.uid.toRawUTF8());
    int bh = 0;
    for (auto& d : devs) if (d.uid.startsWith ("BlackHole")) ++bh;
    CHECK (devs.size() > 3 && bh >= 4, "%d devices, %d BlackHoles", (int) devs.size(), bh);
    CHECK (CoreAudioHost::defaultOutputUid().isNotEmpty() && CoreAudioHost::defaultInputUid().isNotEmpty(), "defaults: out %s, in %s, system %s",
           CoreAudioHost::defaultOutputUid().toRawUTF8(), CoreAudioHost::defaultInputUid().toRawUTF8(), CoreAudioHost::defaultSystemOutputUid().toRawUTF8());
    // setting a default to what it already is: the path runs, nothing changes
    auto out = CoreAudioHost::defaultOutputUid();
    CHECK (CoreAudioHost::setDefaultOutput (out) && CoreAudioHost::defaultOutputUid() == out, "setDefaultOutput (the current one) works and changes nothing");

    auto procs = CoreAudioHost::audioProcesses();
    for (auto& p : procs) printf ("  pid %6d %-34s %-44s%s%s\n", p.pid, p.name.toRawUTF8(), p.bundleId.toRawUTF8(), p.playing ? " PLAYING" : "", p.recording ? " RECORDING" : "");
    bool self = false;
    for (auto& p : procs) self = self || p.pid == getpid();
    CHECK (! procs.empty() && ! self, "%d audio processes, umleitung itself not among them", (int) procs.size());

    // volume: read the default output's; set / restore on BlackHole 2ch only
    printf ("  default output volume %.3f muted %d\n", CoreAudioHost::deviceVolume (out), (int) CoreAudioHost::deviceMuted (out));
    for (auto& d : devs)
        if (d.uid == BH2 && d.hasVolume)
        {
            const float v = CoreAudioHost::deviceVolume (BH2);
            const bool m = CoreAudioHost::deviceMuted (BH2);
            CoreAudioHost::setDeviceVolume (BH2, 0.25f);
            const float v2 = CoreAudioHost::deviceVolume (BH2);
            CoreAudioHost::setDeviceMuted (BH2, ! m);
            const bool m2 = CoreAudioHost::deviceMuted (BH2);
            CoreAudioHost::setDeviceVolume (BH2, v);
            CoreAudioHost::setDeviceMuted (BH2, m);
            CHECK (std::abs (v2 - 0.25f) < 0.02f && m2 != m && std::abs (CoreAudioHost::deviceVolume (BH2) - v) < 0.02f,
                   "BlackHole 2ch volume set 0.25 → read %.3f, mute toggles, restored to %.3f", v2, v);
        }
    CHECK (CoreAudioHost::tapsSupported(), "process taps supported");
    (void) host;
}

static void sectionLoop (CoreAudioHost& host)
{
    printf ("\n== loopback: BlackHole 16ch out → BlackHole 16ch in through the aggregate\n");
    TestCallback cb;
    cb.gens.assign (16, { Gen::Ramp });
    IOConfig c;
    c.inputDevices = { BH16 };
    c.outputDevices = { BH16 };
    c.sampleRate = 48000;
    c.bufferSize = 512;
    juce::String err;
    CHECK (host.start (c, &cb, err), "start (%s)", err.toRawUTF8());
    printLayout (host.layout());
    const auto& l = host.layout();
    CHECK (l.totalIn == 16 && l.totalOut == 16 && l.inputs.size() == 1 && l.outputs.size() == 1 && l.bufferSize == 512, "layout: 16 in, 16 out, buffer 512");
    runFor (host, cb, 5);
    std::vector<int> expect (16);
    for (int i = 0; i < 16; ++i) expect[(size_t) i] = i;
    checkRamp (cb, expect, "BlackHole 16ch");
    printf ("  load %.3f, overloads %d, %lld frames\n", host.load(), host.overloads(), (long long) cb.frames.load());
    CHECK (host.load() > 0 && host.load() < 0.5f, "load %.4f", host.load());
    host.stop();
    CHECK (! host.running() && cb.stops == 1, "stopped, callback told once");
}

static void sectionTwo (CoreAudioHost& host)
{
    printf ("\n== two devices: BlackHole 2ch (clock) + BlackHole 64ch (drift compensated), outputs in the other order\n");
    TestCallback cb;
    cb.gens.assign (66, { Gen::Ramp });
    IOConfig c;
    c.inputDevices = { BH2, BH64 };
    c.outputDevices = { BH64, BH2 };
    c.clockDevice = BH2;
    c.sampleRate = 48000;
    c.bufferSize = 256;
    juce::String err;
    CHECK (host.start (c, &cb, err), "start (%s)", err.toRawUTF8());
    printLayout (host.layout());
    const auto& l = host.layout();
    CHECK (l.totalIn == 66 && l.totalOut == 66
           && l.inputs[0].firstChannel == 0 && l.inputs[0].numChannels == 2 && l.inputs[1].firstChannel == 2 && l.inputs[1].numChannels == 64
           && l.outputs[0].firstChannel == 0 && l.outputs[0].numChannels == 64 && l.outputs[1].firstChannel == 64 && l.outputs[1].numChannels == 2,
           "layout offsets: in BH2 0-1, BH64 2-65; out BH64 0-63, BH2 64-65");
    runFor (host, cb, 5);
    std::vector<int> expect (66);
    expect[0] = 64; expect[1] = 65;
    for (int i = 0; i < 64; ++i) expect[(size_t) i + 2] = i;
    checkRamp (cb, expect, "BlackHole 2ch + 64ch");
    printf ("  load %.3f, overloads %d\n", host.load(), host.overloads());

    printf ("\n== one device in both lists + an input-only and an output-only device (BH16 in/out, BH2 in, BH64 out)\n");
    TestCallback cb2;
    cb2.gens.assign (80, { Gen::Ramp });
    c.inputDevices = { BH16, BH2 };
    c.outputDevices = { BH64, BH16 };
    c.clockDevice = {};
    CHECK (host.start (c, &cb2, err), "restart with a new configuration (%s)", err.toRawUTF8());
    printLayout (host.layout());
    runFor (host, cb2, 5);
    std::vector<int> e2 (18, -1);
    for (int i = 0; i < 16; ++i) e2[(size_t) i] = 64 + i;     // BH16 in ← flat out 64.. (BH16 after BH64)
    checkRamp (cb2, e2, "BH16 loop with BH64 as clock");
    // BH2 is input only: its outputs are switched off, its inputs carry nothing of ours
    CHECK (cb2.rms (16) == 0 && cb2.rms (17) == 0, "input-only BlackHole 2ch: silent (its outputs unused)");
    host.stop();
}

static void sectionHardware (CoreAudioHost& host)
{
    printf ("\n== real hardware as the clock: MacBook Pro Speakers (silence, its current rate kept) + BlackHole 16ch loop\n");
    juce::String spk;
    for (auto& d : CoreAudioHost::devices()) if (d.transport == "built-in" && d.outChannels > 0) spk = d.uid;
    if (spk.isEmpty()) { printf ("  no built-in output: skipped\n"); return; }
    TestCallback cb;
    cb.gens.assign (18, { Gen::Ramp });
    cb.gens[0] = cb.gens[1] = {};                 // the speakers: silence
    IOConfig c;
    c.inputDevices = { BH16 };
    c.outputDevices = { spk, BH16 };
    c.sampleRate = 0;                             // the clock's own rate: the speakers' rate isn't touched
    juce::String err;
    CHECK (host.start (c, &cb, err), "start (%s)", err.toRawUTF8());
    printLayout (host.layout());
    runFor (host, cb, 5);
    std::vector<int> expect (16);
    for (int i = 0; i < 16; ++i) expect[(size_t) i] = 2 + i;
    checkRamp (cb, expect, "BH16 (drift compensated) against the speakers' clock");
    printf ("  load %.3f, overloads %d\n", host.load(), host.overloads());
    host.stop();
}

static void sectionRates (CoreAudioHost& host)
{
    printf ("\n== rates\n");
    for (double r : { 44100.0, 48000.0, 96000.0 })
    {
        TestCallback cb;
        cb.gens.assign (16, { Gen::Ramp });
        cb.warmup = (int) r / 5;
        cb.recordFrames = (int) r;
        IOConfig c;
        c.inputDevices = { BH16 };
        c.outputDevices = { BH16 };
        c.sampleRate = r;
        c.bufferSize = 128;
        juce::String err;
        const bool ok = host.start (c, &cb, err);
        auto t0 = CFAbsoluteTimeGetCurrent();
        auto f0 = cb.frames.load();
        pump (1.0);
        const double measured = (double) (cb.frames.load() - f0) / (CFAbsoluteTimeGetCurrent() - t0);
        runFor (host, cb, 4);
        CHECK (ok && host.layout().sampleRate == r && std::abs (measured - r) < r * 0.03 && host.layout().bufferSize == 128,
               "%.0f Hz: layout %.0f, device %.0f, callback runs at %.0f frames/s, buffer %d (%s)", r, host.layout().sampleRate, rateOf (BH16), measured, host.layout().bufferSize, err.toRawUTF8());
        std::vector<int> expect (16);
        for (int i = 0; i < 16; ++i) expect[(size_t) i] = i;
        checkRamp (cb, expect, (juce::String (r) + " Hz loop").toRawUTF8());
        host.stop();   // before cb goes (the host calls stopped() on it)
    }
}

static void sectionRestart (CoreAudioHost& host)
{
    printf ("\n== restart\n");
    TestCallback cb;
    cb.gens.assign (2, { Gen::Ramp });
    IOConfig c;
    c.inputDevices = { BH2 };
    c.outputDevices = { BH2 };
    juce::String err;
    int ok = 0;
    for (int i = 0; i < 5; ++i)
    {
        cb.recorded = 0;
        if (host.start (c, &cb, err) && runFor (host, cb, 3)) ++ok;
        if (i % 2 == 1) host.stop();
    }
    host.stop();
    int privateLeft = 0;
    for (auto& d : CoreAudioHost::devices()) if (d.name == "umleitung engine") ++privateLeft;
    CHECK (ok == 5 && cb.prepares == 5 && cb.stops == 5 && privateLeft == 0, "5 starts (restarts and stop/start), %d prepares, %d stops, %d engine aggregates left", cb.prepares, cb.stops, privateLeft);
    std::vector<int> expect { 0, 1 };
    checkRamp (cb, expect, "after restarts");

    IOConfig bad;
    bad.inputDevices = { "no-such-device" };
    CHECK (! host.start (bad, &cb, err) && ! host.running(), "a missing device: refused (\"%s\")", err.toRawUTF8());
}

static void sectionRemove (CoreAudioHost& host)
{
    printf ("\n== a device in use disappears\n");
    juce::String why;
    int stoppedCalls = 0;
    host.onStoppedUnexpectedly = [&] (juce::String w) { why = w; ++stoppedCalls; };

    // 1) a device of the configuration: a private aggregate made here (BlackHole 128ch), destroyed while running
    NSDictionary* comp = @{ @kAudioAggregateDeviceUIDKey: @"studio.umleitung.hostcheck.victim", @kAudioAggregateDeviceNameKey: @"hostcheck victim",
                            @kAudioAggregateDeviceIsPrivateKey: @1, @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: @(BH128) } ],
                            @kAudioAggregateDeviceMainSubDeviceKey: @(BH128) };
    AudioObjectID victim = 0;
    AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) comp, &victim);
    pump (0.3);
    TestCallback cb;
    IOConfig c;
    c.inputDevices = { "studio.umleitung.hostcheck.victim" };
    c.outputDevices = { BH2 };
    juce::String err;
    const bool started = host.start (c, &cb, err);
    printf ("  engine with a (nested) aggregate member: %s %s\n", started ? "started" : "refused:", err.toRawUTF8());
    if (started)
    {
        pump (0.5);
        AudioHardwareDestroyAggregateDevice (victim);
        victim = 0;
        for (int i = 0; i < 40 && host.running(); ++i) pump (0.05);
        CHECK (! host.running() && stoppedCalls == 1 && cb.stops == 1, "member destroyed → stopped, onStoppedUnexpectedly (\"%s\")", why.toRawUTF8());
    }
    if (victim) AudioHardwareDestroyAggregateDevice (victim);

    // 2) the engine's own aggregate destroyed under it
    stoppedCalls = 0;
    TestCallback cb2;
    c.inputDevices = { BH2 };
    CHECK (host.start (c, &cb2, err), "start (%s)", err.toRawUTF8());
    pump (0.5);
    for (auto& d : CoreAudioHost::devices())
        if (d.name == "umleitung engine") AudioHardwareDestroyAggregateDevice (d.id);
    for (int i = 0; i < 40 && host.running(); ++i) pump (0.05);
    CHECK (! host.running() && stoppedCalls == 1 && cb2.stops == 1, "engine aggregate destroyed → stopped, onStoppedUnexpectedly (\"%s\")", why.toRawUTF8());
    host.onStoppedUnexpectedly = nullptr;
}

static void sectionTaps (CoreAudioHost& host)
{
    printf ("\n== taps (permission now: %s)\n", permName (CoreAudioHost::capturePermission()));
    const std::string tone = HOSTCHECK_TONE_PATH;
    setRate (BH2, 48000);   // the helper's tone is made at the device's rate when it starts: keep it fixed
    juce::String err;

    // ---- a tone helper on BlackHole 2ch, tapped muted / unmuted, BlackHole 2ch's input alongside
    for (bool mute : { true, false })
    {
        pid_t helper = spawn ({ tone, BH2, "1000", "0.1", "30" });
        CHECK (helper > 0 && waitPlaying (helper, 5), "tone helper (pid %d) plays 1 kHz into BlackHole 2ch", helper);
        TestCallback cb;
        IOConfig c;
        TapSpec t;
        t.kind = TapSpec::Kind::Processes;
        t.pids = { helper };
        t.muteSource = mute;
        t.deviceUid = BH2;
        t.name = "helper";
        c.taps = { t };
        c.inputDevices = { BH2 };
        c.sampleRate = 48000;
        const bool ok = host.start (c, &cb, err);
        printLayout (host.layout());
        CHECK (ok && host.layout().totalIn == 4 && host.layout().inputs[0].kind == IOLayout::Part::Kind::Tap && host.layout().inputs[0].numChannels == 2
               && host.layout().inputs[1].firstChannel == 2, "tap first (ch 0-1), then BlackHole 2ch (ch 2-3) (%s)", err.toRawUTF8());
        runFor (host, cb, 5);
        pump (3.5);   // let the permission guess run
        const double tapA = cb.toneAmp (0, 1000), devA = cb.toneAmp (2, 1000);
        printf ("  muteSource=%d: tap 1 kHz amplitude %.4f (rms %.4f), BlackHole 2ch input 1 kHz %.4f; permission %s\n", (int) mute, tapA, cb.rms (0), devA,
                permName (CoreAudioHost::capturePermission()));
        if (CoreAudioHost::capturePermission() != CoreAudioHost::Permission::Denied)
            CHECK (std::abs (tapA - 0.1) < 0.02, "tap delivers the helper's tone (0.1 at 1 kHz): %.4f", tapA);
        CHECK (mute ? devA < 0.001 : std::abs (devA - 0.1) < 0.02, mute ? "muted at the source: BlackHole 2ch gets nothing (%.5f)" : "unmuted: BlackHole 2ch still gets the tone (%.4f)", devA);
        host.stop();
        reap (helper);
    }

    // ---- system tap of BlackHole 2ch, excluding umleitung: we play 440 Hz into it, the helper 1 kHz
    {
        pid_t helper = spawn ({ tone, BH2, "1000", "0.1", "30" });
        waitPlaying (helper, 5);
        TestCallback cb;
        cb.gens = { { Gen::Sine, 440, 0.1 }, { Gen::Sine, 440, 0.1 } };
        IOConfig c;
        TapSpec t;
        t.kind = TapSpec::Kind::System;
        t.deviceUid = BH2;
        t.name = "system";
        c.taps = { t };
        c.inputDevices = { BH2 };
        c.outputDevices = { BH2 };
        CHECK (host.start (c, &cb, err), "system tap + BlackHole 2ch in/out (%s)", err.toRawUTF8());
        runFor (host, cb, 5);
        const double t1k = cb.toneAmp (0, 1000), t440 = cb.toneAmp (0, 440), d1k = cb.toneAmp (2, 1000), d440 = cb.toneAmp (2, 440);
        printf ("  system tap: 1 kHz %.4f, 440 Hz (ours) %.4f;  BlackHole 2ch input: 1 kHz %.4f, 440 Hz %.4f\n", t1k, t440, d1k, d440);
        CHECK (std::abs (d440 - 0.1) < 0.02 && std::abs (d1k - 0.1) < 0.02, "BlackHole 2ch input carries both (ours and the helper's)");
        if (CoreAudioHost::capturePermission() != CoreAudioHost::Permission::Denied)
            CHECK (std::abs (t1k - 0.1) < 0.02 && t440 < 0.002, "system tap has the helper but not umleitung's own output");
        host.stop();
        reap (helper);
    }

    // ---- a tap by bundle id, the app started after the engine (macOS 26+: the tap follows the bundle id itself;
    //      the second round turns that off and proves the fallback: the engine rebuilds its taps)
    for (int round = 0; round < 2; ++round)
    {
        if (round == 1) setenv ("UMLEITUNG_NO_NATIVE_BUNDLE_TAPS", "1", 1);
        TestCallback cb;
        cb.warmup = 0;
        cb.recordFrames = 48000 * 8;
        IOConfig c;
        TapSpec t;
        t.kind = TapSpec::Kind::Processes;
        t.bundleIds = { "studio.umleitung.hostcheck.tone" };
        t.deviceUid = BH2;
        t.muteSource = true;
        t.name = "by bundle id";
        c.taps = { t };
        c.inputDevices = { BH2 };
        CHECK (host.start (c, &cb, err), "tap by bundle id, app not running yet (%s)", err.toRawUTF8());
        pump (1.0);
        pid_t helper = spawn ({ tone, BH2, "1000", "0.1", "30" });
        waitPlaying (helper, 5);
        bool seen = false;
        auto procs = CoreAudioHost::audioProcesses();
        for (auto& p : procs) if (p.pid == helper) { seen = true; printf ("  helper process: %s (%s)\n", p.name.toRawUTF8(), p.bundleId.toRawUTF8()); }
        CHECK (seen, "the helper is listed with its bundle id");
        cb.recorded = 0;
        pump (2.0);
        cb.recorded = 0;
        runFor (host, cb, 3);
        cb.recordFrames = cb.recorded.load();
        printf ("  prepares %d (2 = the engine rebuilt the tap), tap 1 kHz %.4f, BlackHole 2ch in 1 kHz %.5f, running %d\n", cb.prepares, cb.toneAmp (0, 1000), cb.toneAmp (2, 1000), (int) host.running());
        if (round == 1) CHECK (cb.prepares == 2, "fallback: the engine rebuilt itself when the app appeared (%d prepares)", cb.prepares);
        CHECK (host.running() && cb.toneAmp (2, 1000) < 0.001, "the later app is taken in: muted at its source (BlackHole 2ch input %.5f)", cb.toneAmp (2, 1000));
        if (CoreAudioHost::capturePermission() != CoreAudioHost::Permission::Denied)
            CHECK (std::abs (cb.toneAmp (0, 1000) - 0.1) < 0.02, "and its sound arrives in the tap");
        host.stop();
        reap (helper);
    }

    // ---- afplay on the default output, -60 dBFS, tapped with the source muted (mono), tap-only engine
    {
        NSString* wav = [NSTemporaryDirectory() stringByAppendingPathComponent: @"umleitung-hostcheck-523.wav"];
        {
            const int sr = 48000, n = sr * 12;
            std::vector<int16_t> s ((size_t) n);
            for (int i = 0; i < n; ++i) s[(size_t) i] = (int16_t) std::lround (32767 * 0.001 * std::sin (2 * M_PI * 523.0 * i / sr));
            NSMutableData* d = [NSMutableData data];
            auto u32 = [&] (uint32_t v) { [d appendBytes: &v length: 4]; };
            auto u16 = [&] (uint16_t v) { [d appendBytes: &v length: 2]; };
            [d appendBytes: "RIFF" length: 4]; u32 (36 + (uint32_t) n * 2); [d appendBytes: "WAVEfmt " length: 8];
            u32 (16); u16 (1); u16 (1); u32 (sr); u32 (sr * 2); u16 (2); u16 (16);
            [d appendBytes: "data" length: 4]; u32 ((uint32_t) n * 2); [d appendBytes: s.data() length: (NSUInteger) n * 2];
            [d writeToFile: wav atomically: YES];
        }
        // the tap exists (muted, by "afplay" not running yet → pid) before afplay starts: spawn, then tap at once
        pid_t afplay = spawn ({ "/usr/bin/afplay", wav.UTF8String });
        // the tap is made as soon as CoreAudio knows the process, before its IO starts producing sound
        AudioObjectID obj = 0;
        for (int i = 0; i < 200 && obj == 0; ++i) { for (auto& p : CoreAudioHost::audioProcesses()) if (p.pid == afplay) obj = p.objectId; if (! obj) usleep (5000); }
        TestCallback cb;
        IOConfig c;
        TapSpec t;
        t.kind = TapSpec::Kind::Processes;
        t.pids = { afplay };
        t.mono = true;
        t.muteSource = true;
        t.name = "afplay";
        c.taps = { t };
        const bool ok = host.start (c, &cb, err);
        printLayout (host.layout());
        CHECK (ok && host.layout().totalIn == 1 && host.layout().totalOut == 0, "tap-only engine, mono tap of afplay on the default output (%s)", err.toRawUTF8());
        runFor (host, cb, 5);
        pump (3.5);
        printf ("  afplay tap: 523 Hz amplitude %.5f (sent 0.001), rms %.6f; permission %s\n", cb.toneAmp (0, 523), cb.rms (0), permName (CoreAudioHost::capturePermission()));
        if (CoreAudioHost::capturePermission() != CoreAudioHost::Permission::Denied)
            CHECK (std::abs (cb.toneAmp (0, 523) - 0.001) < 0.0003, "the tap delivers afplay's 523 Hz");
        host.stop();
        reap (afplay);
        [[NSFileManager defaultManager] removeItemAtPath: wav error: nil];
    }
    printf ("  capture permission: %s\n", permName (CoreAudioHost::capturePermission()));
    if (CoreAudioHost::capturePermission() == CoreAudioHost::Permission::Denied)
        printf ("  → taps deliver silence: allow the app running this (Terminal / iTerm) under System Settings → Privacy & Security →\n"
                "    Screen & System Audio Recording → \"System Audio Recording Only\", then run again.\n");
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        std::set<std::string> want;
        for (int i = 1; i < argc; ++i) want.insert (argv[i]);
        auto on = [&] (const char* s) { return want.empty() || want.count (s) > 0; };

        // BlackHole rates are put back afterwards (never the default devices: they aren't touched at all)
        std::map<std::string, double> rates;
        for (auto* u : { BH2, BH16, BH64, BH128 }) rates[u] = rateOf (u);
        const auto defOut = CoreAudioHost::defaultOutputUid(), defIn = CoreAudioHost::defaultInputUid(), defSys = CoreAudioHost::defaultSystemOutputUid();

        int devChanges = 0, procChanges = 0;
        {
            CoreAudioHost host;
            host.onDevicesChanged = [&] { ++devChanges; };
            host.onProcessesChanged = [&] { ++procChanges; };
            if (on ("list"))    sectionList (host);
            if (on ("loop"))    sectionLoop (host);
            if (on ("two"))     sectionTwo (host);
            if (on ("hw"))      sectionHardware (host);
            if (on ("rates"))   sectionRates (host);
            if (on ("restart")) sectionRestart (host);
            if (on ("remove"))  sectionRemove (host);
            if (on ("taps"))    sectionTaps (host);
            pump (0.3);
            printf ("\n== notifications: onDevicesChanged %d, onProcessesChanged %d\n", devChanges, procChanges);
            if (on ("rates") || on ("taps")) CHECK (devChanges > 0 && (! on ("taps") || procChanges > 0), "device / process notifications arrived on the main thread");
        }
        for (auto& [u, r] : rates) if (r > 0 && rateOf (u.c_str()) != r) setRate (u.c_str(), r);
        CHECK (CoreAudioHost::defaultOutputUid() == defOut && CoreAudioHost::defaultInputUid() == defIn && CoreAudioHost::defaultSystemOutputUid() == defSys,
               "default devices unchanged");
        printf ("\n%s (%d failures)\n", failures == 0 ? "ALL OK" : "FAILURES", failures);
    }
    return failures == 0 ? 0 : 1;
}

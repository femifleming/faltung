// faltung st0 studio — the audio engine: clock, per-track sequencer, modulation, automation, routing to the
// 64 output pairs, latency alignment, stems rendering. Everything here runs on the audio thread (or on the
// render thread while exporting) under the session lock.
#include "Session.h"
#include "../dsp/Fx.h"
#include "../dsp/Bass.h"

namespace fal::daw
{
float lfoValue (int shape, float ph, float sh, float shPrev)
{
    switch (shape)
    {
        case 7: return sh;
        case 8: { const float t = ph * ph * (3.0f - 2.0f * ph); return shPrev + (sh - shPrev) * t; }
        case 9: return sh;
        case 10: { float off, acc; Swing::templ (1, (int) (ph * 16.0f) & 15, 0.62f, off, acc); return juce::jlimit (0.0f, 1.0f, 0.5f + off * 2.0f); }
        default: return lfoShape (ph, shape, sh);
    }
}

static bool condOk (Cond c, long long loop, bool fill, bool prev)
{
    switch (c)
    {
        case Cond::Always: return true;
        case Cond::X1of2: return loop % 2 == 0;   case Cond::X2of2: return loop % 2 == 1;
        case Cond::X1of3: return loop % 3 == 0;   case Cond::X2of3: return loop % 3 == 1;   case Cond::X3of3: return loop % 3 == 2;
        case Cond::X1of4: return loop % 4 == 0;   case Cond::X2of4: return loop % 4 == 1;   case Cond::X3of4: return loop % 4 == 2;   case Cond::X4of4: return loop % 4 == 3;
        case Cond::X1of8: return loop % 8 == 0;   case Cond::X8of8: return loop % 8 == 7;
        case Cond::Fill: return fill;             case Cond::NotFill: return ! fill;
        case Cond::First: return loop == 0;       case Cond::NotFirst: return loop > 0;
        case Cond::Prev: return prev;             case Cond::NotPrev: return ! prev;
        default: return true;
    }
}

// ---- play head for the devices ---------------------------------------------------------------------
juce::Optional<juce::AudioPlayHead::PositionInfo> Session::getPosition() const
{
    PositionInfo p;
    p.setBpm (headBpm); p.setIsPlaying (headPlaying); p.setPpqPosition (headPpq);
    p.setTimeSignature (TimeSignature { 4, 4 });
    p.setPpqPositionOfLastBarStart (std::floor (headPpq / 4.0) * 4.0);
    p.setTimeInSamples ((juce::int64) (headPpq * 60.0 / headBpm * sr));
    p.setTimeInSeconds (headPpq * 60.0 / headBpm);
    return p;
}

// ---- prepare --------------------------------------------------------------------------------------
void Session::prepareTrack (Track& t)
{
    t.buf.setSize (2, bs, false, false, true); t.post.setSize (2, bs, false, false, true);
    t.buf.clear(); t.post.clear();
    t.alignL.prepare (65536); t.alignR.prepare (65536);
    t.forEachDevice ([&] (Device& d) { d.setPlayHead (this); d.prepare (sr, bs); });
    t.levelSm.prepare ((float) sr, 15.0f); t.levelSm.snap (1.0f);
    t.panSm.prepare ((float) sr, 15.0f); t.panSm.snap (0.0f);
    for (auto& s : t.sendSm) { s.prepare ((float) sr, 15.0f); s.snap (0.0f); }
    t.nPending = 0; t.snapMixer = true;
    t.rng.seed ((uint32_t) (t.uid * 7919 + 13));
    t.envT = -1; t.envV = 0;
}
void Session::prepare (double sampleRate, int blockSize)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sr = sampleRate; bs = std::max (16, blockSize);
    for (auto& t : tracks) prepareTrack (*t);
    for (auto& r : returns) prepareTrack (*r);
    prepareTrack (*master);
    global->prepare (sr, bs);
    scratchMidi.ensureSize (4096);
    prepared = true;
    // the same as rebuildLatency, but we already hold the lock
    auto chain = [] (Track& t) { int l = 0; if (t.instrument && ! t.instrument->bypass.load()) l += t.instrument->latency(); for (auto& d : t.inserts) if (! d->bypass.load()) l += d->latency(); return l; };
    latTrackMax = 0; latRetMax = 0;
    for (auto& t : tracks) { t->latency = chain (*t); latTrackMax = std::max (latTrackMax, t->latency); }
    for (auto& r : returns) { r->latency = chain (*r); latRetMax = std::max (latRetMax, r->latency); }
    latMaster = master->latency = chain (*master);
    for (auto& t : tracks) t->alignDelay = latTrackMax - t->latency;
    for (auto& r : returns) r->alignDelay = latRetMax - r->latency;
    latTotal = latTrackMax + latRetMax + latMaster;
}
void Session::releaseResources()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& t : tracks) t->forEachDevice ([] (Device& d) { d.release(); });
    for (auto& r : returns) r->forEachDevice ([] (Device& d) { d.release(); });
    master->forEachDevice ([] (Device& d) { d.release(); });
    prepared = false;
}
void Session::panic() { panicFlag.store (true); }

// ---- the block ------------------------------------------------------------------------------------
void Session::process (juce::MidiBuffer& midi, const HostInfo& h, const Outs& outs, int n)
{
    if (rendering.load (std::memory_order_relaxed)) return;
    if (! lock.tryEnter()) return;
    struct Unlock { juce::SpinLock& l; ~Unlock() { l.exit(); } } unlock { lock };
    if (! prepared || n <= 0) return;
    if (onBlock) onBlock();
    const auto t0 = juce::Time::getHighResolutionTicks();

    // live cc: maps and learn
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isController()) continue;
        const int c = msg.getControllerNumber(); const float v = msg.getControllerValue() / 127.0f;
        cc[(size_t) c].store (v);
        lastCc.store (c);
        for (auto& map : *midiMaps)
            if (map.cc == c)
                if (auto* d = resolve (map.target)) d->setBase (map.target.param, map.lo + (map.hi - map.lo) * v);
    }

    const bool host = h.playing && h.hasPpq && ! h.standalone;
    hostDriven.store (host);
    bool playing = host || running.load();
    double ppq = host ? h.ppq : internalPpq.load();
    if (! host && relocate.exchange (false)) { ppq = internalPpq.load(); for (auto& t : tracks) t->lastStepN = -1; }
    if (! host && ! playing && playPpq.load() >= 0) { /* just stopped */ }
    if (panicFlag.exchange (false))
    {
        auto allOff = [] (Track& t)
        {
            t.nPending = 0;
            if (t.instrument && t.instrument->processor())
            {
                juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); off.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                juce::AudioBuffer<float> tmp (2, 16); tmp.clear();
                t.instrument->process (tmp, off);
            }
            t.activePattern = -1; t.silent = true; t.lastStepN = -1;
        };
        for (auto& t : tracks) allOff (*t);
        for (auto& t : tracks) if (mode.load() == (int) PlayMode::Session) { t->pendingPattern.store (t->pattern.load()); t->pendingAt.store (0.0); }
    }
    automate (ppq);
    global->apply (nullptr, ppq);
    const double bpm = host ? h.bpm : (double) tempo();
    bpmShown.store ((float) bpm);

    if (! playing)
    {
        playPpq.store (-1.0);
        renderSpan (n, ppq, bpm, outs, midi, false);
    }
    else
    {
        playPpq.store (ppq);
        int done = 0; double p = ppq;
        const bool loop = ! host && loopOn.load();
        const double lStart = loopStart.load() * 4.0, lEnd = loopEnd.load() * 4.0;
        while (done < n)
        {
            const double spb = 60.0 / bpm * sr;
            double limit = std::floor (p / 4.0 + 1.0e-9) * 4.0 + 4.0;
            if (loop && lEnd > lStart && p < lEnd - 1.0e-9) limit = std::min (limit, lEnd);
            int len = (int) std::ceil ((limit - p) * spb - 1.0e-6);
            len = juce::jlimit (1, n - done, len);
            Outs o; for (size_t i = 0; i < o.ch.size(); ++i) o.ch[i] = outs.ch[i] ? outs.ch[i] + done : nullptr;
            // the live midi of this span
            juce::MidiBuffer span;
            for (const auto m : midi) if (m.samplePosition >= done && m.samplePosition < done + len) span.addEvent (m.getMessage(), m.samplePosition - done);
            renderSpan (len, p, bpm, o, span, true);
            p += len / spb; done += len;
            if (std::abs (p - limit) < 1.0e-6) p = limit;   // land exactly on the bar line
            if (loop && lEnd > lStart && p >= lEnd - 1.0e-6) { p = lStart; for (auto& t : tracks) t->lastStepN = -1; }
        }
        if (! host) internalPpq.store (p);
        else internalPpq.store (p);
    }
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / (n / sr));
    cpu.store (cpu.load() * 0.9f + load * 0.1f);
}

void Session::automate (double ppq)
{
    for (auto& l : *lanes)
    {
        if (! l->enabled || l->pts.empty()) continue;
        auto* d = resolve (l->target);
        if (! d || d->inGesture (l->target.param)) continue;
        const float v = l->valueAt (ppq);
        if (std::abs (v - d->baseValue (l->target.param)) > 1.0e-4f) d->setBase (l->target.param, v);
    }
}

void Session::renderSpan (int n, double ppq0, double bpm, const Outs& outs, juce::MidiBuffer& live, bool playing)
{
    const double spb = 60.0 / bpm * sr;
    const double ppq1 = ppq0 + n / spb;
    headPpq = ppq0; headBpm = bpm; headPlaying = playing;
    float macros[kMacros];
    for (int i = 0; i < kMacros; ++i) macros[i] = global->finalValue (GP_MACRO1 + i);
    bool anySolo = false;
    for (auto& t : tracks) anySolo |= t->solo.load (std::memory_order_relaxed);
    for (auto& r : returns) r->buf.clear (0, n);
    master->buf.clear (0, n);
    bool anyArmed = false;
    for (auto& t : tracks) anyArmed |= t->armed.load (std::memory_order_relaxed);
    const int sel = selectedTrack.load();

    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        Track& t = *tracks[(size_t) i];
        if (playing) resolveTrack (t, i, ppq0);
        scratchMidi.clear();
        const bool takesLive = t.armed.load (std::memory_order_relaxed) || (! anyArmed && sel == i);
        if (takesLive)
            for (const auto m : live)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOnOrOff() || msg.isPitchWheel() || msg.isAftertouch() || msg.isChannelPressure() || (msg.isController() && msg.getControllerNumber() == 1))
                    scratchMidi.addEvent (msg, m.samplePosition);
                if (msg.isNoteOn()) { t.lastVel = msg.getFloatVelocity(); t.envT = 0; t.hits.fetch_add (1); }
            }
        modulate (t, ppq0, n, bpm);
        if (playing) sequence (t, ppq0, ppq1, spb, bpm, scratchMidi);
        else
        {
            // flush pending note-offs while stopped
            for (int k = 0; k < t.nPending;)
            {
                auto& p = t.pending[(size_t) k];
                if (p.left < n) { scratchMidi.addEvent (juce::MidiMessage::noteOff (1, p.note), std::max (0, p.left)); p = t.pending[(size_t) --t.nPending]; }
                else { p.left -= n; ++k; }
            }
            t.playStep.store (-1);
        }
        renderTrack (t, n, ppq0, macros, anySolo, outs, scratchMidi, true);
    }
    for (auto& r : returns)
    {
        modulate (*r, ppq0, n, bpm);
        scratchMidi.clear();
        renderTrack (*r, n, ppq0, macros, anySolo, outs, scratchMidi, false);
    }
    modulate (*master, ppq0, n, bpm);
    scratchMidi.clear();
    renderTrack (*master, n, ppq0, macros, anySolo, outs, scratchMidi, false);
}

// which pattern plays now, and since when
void Session::resolveTrack (Track& t, int index, double ppq0)
{
    const auto m = (PlayMode) mode.load (std::memory_order_relaxed);
    const double stepBeats = 0.25 / speedMul (t.speed.load (std::memory_order_relaxed));
    t.loopOffset = 0;
    if (m == PlayMode::Session)
    {
        const int pend = t.pendingPattern.load (std::memory_order_relaxed);
        if (pend != -1 && t.pendingAt.load (std::memory_order_relaxed) <= ppq0 + 1.0e-6)
        {
            if (pend == -2) { t.silent = true; t.activePattern = -1; }
            else { t.activePattern = pend; t.anchor = std::max (0.0, t.pendingAt.load()); t.silent = false; t.pattern.store (pend); }
            t.pendingPattern.store (-1); t.lastStepN = -1; t.prevFired = false;
        }
        if (! t.silent && t.chainOn.load (std::memory_order_relaxed) && ! t.chain->empty())
        {
            const auto& ch = *t.chain;
            long long total = 0;
            for (int p : ch) total += t.patternAt (p) ? t.patternAt (p)->length : 16;
            if (total > 0)
            {
                const long long E = std::max (0LL, (long long) std::floor ((ppq0 - t.anchor) / stepBeats));
                const long long pass = E / total; long long rem = E % total, cum = 0;
                size_t k = 0;
                for (; k < ch.size(); ++k) { const int len = t.patternAt (ch[k]) ? t.patternAt (ch[k])->length : 16; if (rem < cum + len) break; cum += len; }
                if (k >= ch.size()) k = ch.size() - 1;
                t.activePattern = ch[k];
                t.seqAnchor = t.anchor + (double) (pass * total + cum) * stepBeats;
                t.loopOffset = pass;
                t.shownPattern.store (t.activePattern);
                return;
            }
        }
        t.seqAnchor = t.anchor;
    }
    else if (m == PlayMode::Song)
    {
        const double bar = ppq0 / 4.0;
        double s = 0; int found = -1; double start = 0;
        for (size_t e = 0; e < song->size(); ++e)
        {
            const auto& en = (*song)[e];
            if (bar < s + en.bars - 1.0e-9) { found = (int) e; start = s; break; }
            s += en.bars;
        }
        if (found < 0 || (*song)[(size_t) found].scene < 0 || (*song)[(size_t) found].scene >= (int) scenes->size()) { t.silent = true; t.activePattern = -1; }
        else
        {
            const int p = (*scenes)[(size_t) (*song)[(size_t) found].scene].pattern[(size_t) juce::jlimit (0, kMaxTracks - 1, index)];
            if (p == -1) { t.silent = true; t.activePattern = -1; }
            else if (p >= 0 && (p != t.activePattern || std::abs (t.anchor - start * 4.0) > 1.0e-6 || t.silent))
            { t.activePattern = p; t.anchor = start * 4.0; t.silent = false; t.lastStepN = -1; t.prevFired = false; }
            else if (p == -2 && t.activePattern < 0) t.silent = true;
            currentScene.store ((*song)[(size_t) found].scene);
        }
        t.seqAnchor = t.anchor;
    }
    else
    {
        const double bar = ppq0 / 4.0;
        const Clip* c = nullptr;
        for (auto& cl : *t.clips) if (bar >= cl.startBar - 1.0e-9 && bar < cl.startBar + cl.bars - 1.0e-9) { c = &cl; break; }
        if (! c) { t.silent = true; t.activePattern = -1; }
        else if (c->pattern != t.activePattern || std::abs (t.anchor - c->startBar * 4.0) > 1.0e-6 || t.silent)
        { t.activePattern = c->pattern; t.anchor = c->startBar * 4.0; t.silent = false; t.lastStepN = -1; t.prevFired = false; }
        t.seqAnchor = t.anchor;
    }
    t.shownPattern.store (t.silent ? -1 : t.activePattern);
}

void Session::modulate (Track& t, double ppq0, int n, double bpm)
{
    const ModDef& m = *t.mod;
    t.forEachDevice ([] (Device& d) { std::fill (d.modOffset.begin(), d.modOffset.end(), 0.0f); });
    const double dt = n / sr;
    float src[MS_COUNT] = {};
    for (int k = 0; k < kLfos; ++k)
    {
        const auto& l = m.lfo[(size_t) k];
        float ph;
        if (l.sync)
        {
            const double beats = divisionBeats (l.div);
            const double from = l.retrig ? t.lastHitPpq : 0.0;
            const double cycles = std::max (0.0, ppq0 - from) / beats + l.phase;
            const double cy = std::floor (cycles);
            if (cy != t.lfoCycle[(size_t) k]) { t.lfoCycle[(size_t) k] = cy; t.lfoShPrev[(size_t) k] = t.lfoSh[(size_t) k]; t.lfoSh[(size_t) k] = t.rng.uni(); }
            ph = (float) (cycles - cy);
        }
        else
        {
            float p = t.lfoPh[(size_t) k] + (float) (dt * l.hz);
            if (p >= 1.0f) { p -= std::floor (p); t.lfoShPrev[(size_t) k] = t.lfoSh[(size_t) k]; t.lfoSh[(size_t) k] = t.rng.uni(); }
            t.lfoPh[(size_t) k] = p; ph = p + l.phase; ph -= std::floor (ph);
        }
        float v;
        if (l.shape == 9)
        {
            const double rate = l.sync ? bpm / 60.0 / divisionBeats (l.div) : (double) l.hz;
            float& o = t.lfoOut[(size_t) k];
            o += t.rng.bi() * (float) std::sqrt (rate * dt) * 0.35f + (0.5f - o) * (float) (rate * dt) * 0.2f;
            o = juce::jlimit (0.0f, 1.0f, o);
            v = o;
        }
        else
        {
            v = lfoValue (l.shape, skewPhase (std::min (ph, 0.99999f), l.skew), t.lfoSh[(size_t) k], t.lfoShPrev[(size_t) k]);
            if (l.smooth > 0.001f) { const float kk = 1.0f - std::exp ((float) -dt / (0.005f + 0.6f * l.smooth)); t.lfoOut[(size_t) k] += (v - t.lfoOut[(size_t) k]) * kk; v = t.lfoOut[(size_t) k]; }
            else t.lfoOut[(size_t) k] = v;
        }
        t.showLfo[(size_t) k].store (v);
        src[MS_LFO1 + k] = v * 2.0f - 1.0f;
    }
    // envelope
    if (t.envT >= 0)
    {
        const double a = m.env.attackMs * 0.001, d = std::max (0.001, m.env.decayMs * 0.001);
        t.envV = t.envT < a ? (float) (t.envT / std::max (1.0e-4, a)) : (float) std::exp (-(t.envT - a) / d * 4.0);
        t.envT += dt;
        if (t.envT > a + d * 3.0) { t.envT = -1; t.envV = 0; }
    }
    t.showEnv.store (t.envV);
    src[MS_ENV] = t.envV;
    // steps
    {
        const double beats = divisionBeats (m.steps.div);
        const int len = juce::jlimit (1, kStepMod, m.steps.length);
        const long long idx = (long long) std::floor (std::max (0.0, ppq0 - t.seqAnchor) / beats);
        const float target = m.steps.v[(size_t) (idx % len)];
        if (m.steps.glide > 0.001f) { const float kk = 1.0f - std::exp ((float) -dt / (0.002f + 0.5f * m.steps.glide)); t.stepsV += (target - t.stepsV) * kk; }
        else t.stepsV = target;
        t.showSteps.store (t.stepsV);
        src[MS_STEPS] = t.stepsV;
    }
    src[MS_VEL] = t.lastVel; src[MS_RANDOM] = t.lastRandom;
    for (int k = 0; k < kMacros; ++k) src[MS_MACRO1 + k] = global->finalValue (GP_MACRO1 + k);
    for (auto& s : m.slots)
    {
        if (s.src == MS_OFF || s.param < 0 || std::abs (s.amount) < 1.0e-5f) continue;
        auto* d = t.device (s.device);
        if (! d || s.param >= d->numParams()) continue;
        const float v = s.src == MS_CC ? cc[(size_t) juce::jlimit (0, 127, s.cc)].load() : src[juce::jlimit (0, MS_COUNT - 1, s.src)];
        d->modOffset[(size_t) s.param] += s.amount * v;
    }
}

void Session::applyLocks (Track& t, const Pattern& pat, int si, double e, double stepBeats)
{
    const Step& st = pat.s[(size_t) si];
    t.forEachDevice ([&] (Device& d) { if (d.clearLocksExcept (st.locks.data(), kLocks) && d.uid == 0) t.snapMixer = true; });
    for (auto& l : st.locks)
    {
        if (l.param < 0) continue;
        auto* d = t.device (l.device);
        if (! d || l.param >= d->numParams()) continue;
        if (st.slide)
        {
            const int len = juce::jlimit (1, kMaxSteps, pat.length);
            for (int j = 1; j <= len; ++j)
            {
                const Step& nx = pat.s[(size_t) ((si + j) % len)];
                if (! nx.on) continue;
                bool found = false;
                for (auto& nl : nx.locks) if (nl.param == l.param && nl.device == l.device) { d->lock (l.param, l.value, true, nl.value, e, e + j * stepBeats); found = true; break; }
                if (found) break;
                if (j == len) d->lock (l.param, l.value, false, l.value, e, e);
            }
        }
        else d->lock (l.param, l.value, false, l.value, e, e);
    }
}

void Session::sequence (Track& t, double ppq0, double ppq1, double spb, double bpm, juce::MidiBuffer& out)
{
    const int n = (int) std::round ((ppq1 - ppq0) * spb);
    for (int k = 0; k < t.nPending;)
    {
        auto& p = t.pending[(size_t) k];
        if (p.left < n) { out.addEvent (juce::MidiMessage::noteOff (1, p.note), std::max (0, p.left)); p = t.pending[(size_t) --t.nPending]; }
        else { p.left -= n; ++k; }
    }
    if (t.silent || t.activePattern < 0) { t.playStep.store (-1); return; }
    const PatternPtr pp = t.patterns[(size_t) t.activePattern];
    if (! pp) { t.playStep.store (-1); return; }
    const Pattern& pat = *pp;
    const double stepBeats = 0.25 / speedMul (t.speed.load (std::memory_order_relaxed));
    const int len = juce::jlimit (1, kMaxSteps, pat.length);
    const float sw = juce::jlimit (0.5f, 0.75f, swing() + t.swingOffset.load (std::memory_order_relaxed));
    const float micro = microMs.load (std::memory_order_relaxed), shift = t.shiftMs.load (std::memory_order_relaxed), pm = t.probMul.load (std::memory_order_relaxed);
    const double msToPpq = bpm / 60000.0;
    const bool fl = fill.load (std::memory_order_relaxed);
    const int root = t.rootNote.load (std::memory_order_relaxed);
    const double rel0 = ppq0 - t.seqAnchor, rel1 = ppq1 - t.seqAnchor;
    t.playStep.store (rel0 < 0 ? -1 : (int) (((long long) std::floor (rel0 / stepBeats)) % len));
    const long long n0 = std::max (0LL, (long long) std::floor (rel0 / stepBeats) - 2), n1 = (long long) std::floor (rel1 / stepBeats) + 2;
    for (long long N = n0; N <= n1; ++N)
    {
        const long long loop = N / len + t.loopOffset; const int si = (int) (N % len);
        const Step& st = pat.s[(size_t) si];
        if (! st.on) continue;
        const double base = t.seqAnchor + (double) N * stepBeats + ((si & 1) ? (sw - 0.5) * 2.0 * stepBeats : 0.0) + (st.micro * micro + shift) * msToPpq;
        const int rat = juce::jlimit (1, 8, st.ratchet);
        for (int r = 0; r < rat; ++r)
        {
            const double e = base + r * (stepBeats / rat);
            if (e < ppq0 - 1.0e-9 || e >= ppq1 - 1.0e-9) continue;
            if (r == 0)
            {
                const bool ok = condOk (st.cond, loop, fl, t.prevFired);
                const bool fired = ok && t.rng.uni() <= st.prob * pm;
                t.prevFired = ok;
                if (! fired) break;
                applyLocks (t, pat, si, e, stepBeats);
                for (auto& l : st.locks) if (l.param >= 0 && l.device == 0) t.snapMixer = true;
                t.lastVel = st.vel; t.lastRandom = t.rng.bi(); t.lastHitPpq = e; t.envT = 0;
                t.hits.fetch_add (1);
            }
            const int smp = juce::jlimit (0, n - 1, (int) ((e - ppq0) * spb));
            const int note = juce::jlimit (0, 127, root + st.pitch);
            const float vel = juce::jlimit (0.02f, 1.0f, st.vel * (r == 0 ? 1.0f : 0.85f - 0.05f * r));
            double lenBeats = std::max (0.02, (double) st.len) * stepBeats;
            if (r < rat - 1) lenBeats = std::min (lenBeats, stepBeats / rat * 0.9);
            const int lenS = std::max (1, (int) (lenBeats * spb));
            out.addEvent (juce::MidiMessage::noteOn (1, note, vel), smp);
            if (smp + lenS < n) out.addEvent (juce::MidiMessage::noteOff (1, note), smp + lenS);
            else if (t.nPending < (int) t.pending.size()) t.pending[(size_t) t.nPending++] = { note, smp + lenS - n };
        }
    }
}

void Session::renderTrack (Track& t, int n, double ppq, const float* macros, bool anySolo, const Outs& outs, juce::MidiBuffer& midi, bool isTrack)
{
    if (isTrack)
    {
        t.buf.clear (0, n);
        if (t.instrument)
        {
            t.instrument->apply (macros, ppq);
            juce::AudioBuffer<float> b (t.buf.getArrayOfWritePointers(), 2, n);
            t.instrument->process (b, midi);
        }
    }
    for (auto& d : t.inserts)
    {
        d->apply (macros, ppq);
        juce::AudioBuffer<float> b (t.buf.getArrayOfWritePointers(), 2, n);
        emptyMidi.clear();
        d->process (b, emptyMidi);
    }
    // latency alignment: every track output lands at the same time; direct outputs also wait for the returns and master
    int delay = t.alignDelay;
    const int pair = t.isMaster() ? masterOut.load() : t.outPair.load (std::memory_order_relaxed);
    if (! t.isMaster() && pair > 0) delay += t.isReturn() ? latMaster : latRetMax + latMaster;
    delay = std::min (delay, 65000);
    auto* L = t.buf.getWritePointer (0); auto* R = t.buf.getWritePointer (1);
    if (delay > 0)
        for (int i = 0; i < n; ++i)
        {
            t.alignL.push (L[i]); t.alignR.push (R[i]);
            L[i] = t.alignL.read ((float) delay); R[i] = t.alignR.read ((float) delay);
        }
    // the strip
    t.mixer->apply (macros, ppq);
    float lvl = t.isMaster() ? global->finalValue (GP_MASTER) : t.mixer->finalValue (MP_LEVEL);
    float gain = lvl <= 0.001f ? 0.0f : dbToGain (mixerLevelDb (lvl));
    if (t.mixer->finalValue (MP_MUTE) > 0.5f) gain = 0.0f;
    if (isTrack && anySolo && ! t.solo.load (std::memory_order_relaxed)) gain = 0.0f;
    t.levelSm.target = gain;
    t.panSm.target = t.mixer->finalValue (MP_PAN) * 2.0f - 1.0f;
    if (t.snapMixer) { t.snapMixer = false; t.levelSm.snap (gain); t.panSm.snap (t.panSm.target); }
    auto* PL = t.post.getWritePointer (0); auto* PR = t.post.getWritePointer (1);
    float pk = 0, pkR = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = t.levelSm.tick();
        float gl, gr; panGains (t.panSm.tick(), gl, gr);
        PL[i] = L[i] * g * gl; PR[i] = R[i] * g * gr;
        pk = std::max (pk, std::abs (PL[i])); pkR = std::max (pkR, std::abs (PR[i]));
    }
    t.meterL.store (std::max (pk, t.meterL.load() * 0.0f)); t.meterR.store (pkR);
    // sends (tracks only)
    if (isTrack)
        for (int r = 0; r < kReturns; ++r)
        {
            const float sv = t.mixer->finalValue (MP_SEND1 + r);
            const float sg = sv <= 0.001f ? 0.0f : dbToGain (-48.0f + 48.0f * sv);
            auto& sm = t.sendSm[(size_t) r];
            sm.target = sg;
            if (sg <= 0.0f && sm.cur < 1.0e-5f) { sm.cur = 0.0f; continue; }
            const bool pre = t.sendPre[(size_t) r].load (std::memory_order_relaxed);
            const float* sL = pre ? L : PL; const float* sR = pre ? R : PR;
            auto* dL = returns[(size_t) r]->buf.getWritePointer (0); auto* dR = returns[(size_t) r]->buf.getWritePointer (1);
            for (int i = 0; i < n; ++i) { const float g = sm.tick(); dL[i] += sL[i] * g; dR[i] += sR[i] * g; }
        }
    // out
    float* oL = nullptr; float* oR = nullptr;
    if (pair > 0 && pair <= kOutPairs) { oL = outs.ch[(size_t) (2 * (pair - 1))]; oR = outs.ch[(size_t) (2 * (pair - 1) + 1)]; }
    if (! t.isMaster() && (pair == 0 || ! oL || ! oR))
    {
        auto* mL = master->buf.getWritePointer (0); auto* mR = master->buf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { mL[i] += PL[i]; mR[i] += PR[i]; }
    }
    else if (oL && oR) for (int i = 0; i < n; ++i) { oL[i] += PL[i]; oR[i] += PR[i]; }
    else if (t.isMaster() && outs.ch[0] && outs.ch[1]) for (int i = 0; i < n; ++i) { outs.ch[0][i] += PL[i]; outs.ch[1][i] += PR[i]; }
}

// ---- export ---------------------------------------------------------------------------------------
bool Session::renderStems (const juce::File& dir, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error)
{
    if (! prepared) { error = "audio not running"; return false; }
    if (toBar <= fromBar) { error = "nothing to render"; return false; }
    dir.createDirectory();
    rendering.store (true);
    const juce::SpinLock::ScopedLockType sl (lock);
    const bool wasRunning = running.load(); const double savedPpq = internalPpq.load();
    running.store (false);
    juce::WavAudioFormat wav;
    struct Stem { Track* t; std::unique_ptr<juce::AudioFormatWriter> w; };
    std::vector<Stem> stems;
    auto open = [&] (Track* t, const juce::String& name)
    {
        auto f = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav"); f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (w) stems.push_back ({ t, std::move (w) });
    };
    for (int i = 0; i < (int) tracks.size(); ++i) open (tracks[(size_t) i].get(), juce::String (i + 1).paddedLeft ('0', 2) + " " + tracks[(size_t) i]->name);
    for (int r = 0; r < kReturns; ++r) if (returns[(size_t) r]->inserts.size() > 0) open (returns[(size_t) r].get(), "return " + juce::String (r + 1) + " " + returns[(size_t) r]->name);
    open (master.get(), "master");
    // silence the engines, then play from the start bar with the internal clock; one extra bar of tail
    for (auto& t : tracks) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->lastStepN = -1; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
    for (auto& r : returns) r->forEachDevice ([] (Device& d) { d.reset(); });
    master->forEachDevice ([] (Device& d) { d.reset(); });
    for (auto& t : tracks) if (mode.load() == (int) PlayMode::Session) { t->pendingPattern.store (t->pattern.load()); t->pendingAt.store (0.0); }
    std::vector<float> outMem ((size_t) (2 * kOutPairs * bs), 0.0f);
    Outs outs; for (int c = 0; c < 2 * kOutPairs; ++c) outs.ch[(size_t) c] = outMem.data() + (size_t) c * (size_t) bs;
    double p = fromBar * 4.0; const double end = (toBar + 1.0) * 4.0;
    juce::MidiBuffer none;
    bool ok = true;
    while (p < end && ok)
    {
        automate (p);
        global->apply (nullptr, p);
        const double bpm = tempo(); const double spb = 60.0 / bpm * sr;
        const double limit = std::floor (p / 4.0 + 1.0e-9) * 4.0 + 4.0;
        int len = juce::jlimit (1, bs, (int) std::ceil ((limit - p) * spb - 1.0e-6));
        std::fill (outMem.begin(), outMem.end(), 0.0f);
        headPpq = p;
        renderSpan (len, p, bpm, outs, none, p < toBar * 4.0);
        for (auto& s : stems) s.w->writeFromAudioSampleBuffer (s.t->post, 0, len);
        p += len / spb;
        if (std::abs (p - limit) < 1.0e-6) p = limit;
        if (progress && ! progress ((float) ((p - fromBar * 4.0) / (end - fromBar * 4.0)))) ok = false;
    }
    stems.clear();
    for (auto& t : tracks) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
    internalPpq.store (savedPpq); running.store (wasRunning);
    rendering.store (false);
    if (! ok) error = "cancelled";
    return ok;
}

bool Session::exportMidi (const juce::File& file, double fromBar, double toBar, juce::String& error)
{
    if (toBar <= fromBar) { error = "nothing to export"; return false; }
    const juce::SpinLock::ScopedLockType sl (lock);
    const bool wasRunning = running.load(); running.store (false);
    const int tpq = 960;
    juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq);
    const double bpm = tempo(); const double spbs = 60.0 / bpm * sr;
    std::vector<juce::MidiMessageSequence> seqs (tracks.size());
    for (auto& t : tracks) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->lastStepN = -1; }
    for (auto& t : tracks) if (mode.load() == (int) PlayMode::Session) { t->pendingPattern.store (t->pattern.load()); t->pendingAt.store (0.0); }
    double p = fromBar * 4.0; const double end = toBar * 4.0;
    while (p < end)
    {
        const double limit = std::min (end, std::floor (p / 4.0 + 1.0e-9) * 4.0 + 4.0);
        const int len = juce::jlimit (1, bs, (int) std::ceil ((limit - p) * spbs - 1.0e-6));
        const double ppq1 = p + len / spbs;
        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            Track& t = *tracks[(size_t) i];
            resolveTrack (t, i, p);
            juce::MidiBuffer mb;
            sequence (t, p, ppq1, spbs, bpm, mb);
            for (const auto m : mb)
            {
                auto msg = m.getMessage();
                msg.setTimeStamp ((p - fromBar * 4.0 + m.samplePosition / spbs) * tpq);
                seqs[(size_t) i].addEvent (msg);
            }
        }
        p = ppq1;
    }
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto& s = seqs[(size_t) i];
        // flush note-offs still pending
        for (int k = 0; k < tracks[(size_t) i]->nPending; ++k) { auto m = juce::MidiMessage::noteOff (1, tracks[(size_t) i]->pending[(size_t) k].note); m.setTimeStamp ((end - fromBar * 4.0) * tpq); s.addEvent (m); }
        tracks[(size_t) i]->nPending = 0;
        s.addEvent (juce::MidiMessage::textMetaEvent (3, tracks[(size_t) i]->name), 0.0);
        if (i == 0) s.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
        s.updateMatchedPairs(); s.sort();
        mf.addTrack (s);
    }
    for (auto& t : tracks) { t->activePattern = -1; t->silent = true; }
    running.store (wasRunning);
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (! os.openedOk()) { error = "cannot write " + file.getFullPathName(); return false; }
    return mf.writeTo (os);
}
} // namespace fal::daw

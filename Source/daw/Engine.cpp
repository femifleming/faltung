// faltung sndstudio — the audio engine: clock, per-track sequencer, modulators, automation, audio clips, routes,
// widths, the 128 output channels, latency alignment, recording taps, offline rendering. Everything here runs on
// the audio thread (or on the render thread while exporting) under the session lock.
#include "Session.h"
#include "../dsp/Fx.h"
#include "../dsp/Bass.h"
#include "../dsp/Master.h"
#include "devices/StudioDevices.h"

namespace fal::daw
{
float lfoValue (int shape, float ph, float sh, float shPrev, float pw)
{
    switch (shape)
    {
        case 7: return sh;
        case 8: { const float t = ph * ph * (3.0f - 2.0f * ph); return shPrev + (sh - shPrev) * t; }
        case 9: return sh;
        case 10: { float off, acc; Swing::templ (1, (int) (ph * 16.0f) & 15, 0.62f, off, acc); return juce::jlimit (0.0f, 1.0f, 0.5f + off * 2.0f); }
        case 11: return ph < pw ? 1.0f : 0.0f;
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
    sizeBuffers (t);
    t.instDelay.resize ((size_t) std::max (2, t.width.load()));
    for (auto& d : t.instDelay) d.prepare (std::max (1024, t.inDelay + bs + 16));
    t.forEachDevice ([&] (Device& d) { d.setPlayHead (this); d.prepare (sr, bs); });
    t.levelSm.prepare ((float) sr, 15.0f); t.levelSm.snap (1.0f);
    t.panSm.prepare ((float) sr, 15.0f); t.panSm.snap (0.0f);
    for (auto& s : t.sendSm) { s.prepare ((float) sr, 15.0f); s.snap (0.0f); }
    t.nPending = 0; t.snapMixer = true;
    t.rng.seed ((uint32_t) (t.uid * 7919 + 13));
    t.hitEnvT = -1;
    t.seqMidi.ensureSize (4096);
    t.evoRng.seed ((uint32_t) (t.uid * 104729 + 7)); t.flip.fill (0); t.drift.fill (0.0f); t.evoPass = -1;
    t.chokeGain = t.chokeTarget = 1.0f;
}
void Session::prepare (double sampleRate, int blockSize)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sr = sampleRate; bs = std::max (16, blockSize);
        for (auto& t : tracks) prepareTrack (*t);
        for (auto& r : returns) prepareTrack (*r);
        prepareTrack (*master);
        global->prepare (sr, bs);
        scratchMidi.ensureSize (4096);
        recScratch.setSize (kMaxWidth, bs, false, false, true);
        prepared = true;
    }
    for (auto& t : tracks) negotiate (*t);
    for (auto& r : returns) negotiate (*r);
    negotiate (*master);
    rebuildLatency();
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
    const auto t0 = juce::Time::getHighResolutionTicks();
    inputChannels.store (h.numIn);

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

    bool playing = running.load();
    double ppq = internalPpq.load();
    if (relocate.exchange (false)) { ppq = internalPpq.load(); for (auto& t : tracks) t->lastStepN = -1; }
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
    if (playing && ! wasPlaying)
    {
        // every start begins the same way: the steps' chance, the modulators and the drift start over, so the same
        // bars render the same notes twice
        auto fresh = [] (Track& t)
        {
            t.rng.seed ((uint32_t) (t.uid * 7919 + 13)); t.evoRng.seed ((uint32_t) (t.uid * 104729 + 7));
            t.flip.fill (0); t.drift.fill (0.0f); t.evoPass = -1; t.prevFired = false;
            t.snapMixer = true;   // levels, pans and sends start where they are set, not on the way there from before the stop
            for (auto& m : t.modState) m = ModState();
        };
        for (auto& t : tracks) fresh (*t);
        for (auto& r : returns) fresh (*r);
        fresh (*master);
    }
    wasPlaying = playing;
    automate (ppq);
    global->apply (nullptr, ppq);
    const double bpm = tempo();
    bpmShown.store ((float) bpm);

    if (! playing)
    {
        playPpq.store (-1.0);
        renderSpan (n, ppq, bpm, outs, midi, false, h);
    }
    else
    {
        playPpq.store (ppq);
        int done = 0; double p = ppq;
        const bool loop = loopOn.load();
        const double lStart = loopStart.load() * 4.0, lEnd = loopEnd.load() * 4.0;
        while (done < n)
        {
            const double spb = 60.0 / bpm * sr;
            double limit = std::floor (p / 4.0 + 1.0e-9) * 4.0 + 4.0;
            if (loop && lEnd > lStart && p < lEnd - 1.0e-9) limit = std::min (limit, lEnd);
            int len = (int) std::ceil ((limit - p) * spb - 1.0e-6);
            len = juce::jlimit (1, n - done, len);
            Outs o; for (size_t i = 0; i < o.ch.size(); ++i) o.ch[i] = outs.ch[i] ? outs.ch[i] + done : nullptr;
            HostInfo hs = h;
            std::array<const float*, kMaxWidth * 2> inPtr {};
            if (h.in && h.numIn > 0)
            {
                for (int c = 0; c < std::min (h.numIn, (int) inPtr.size()); ++c) inPtr[(size_t) c] = h.in[c] + done;
                hs.in = inPtr.data(); hs.numIn = std::min (h.numIn, (int) inPtr.size());
            }
            juce::MidiBuffer span;
            for (const auto m : midi) if (m.samplePosition >= done && m.samplePosition < done + len) span.addEvent (m.getMessage(), m.samplePosition - done);
            renderSpan (len, p, bpm, o, span, true, hs);
            p += len / spb; done += len;
            if (std::abs (p - limit) < 1.0e-6) p = limit;   // land exactly on the bar line
            if (loop && lEnd > lStart && p >= lEnd - 1.0e-6) { p = lStart; for (auto& t : tracks) t->lastStepN = -1; }
        }
        internalPpq.store (p);
    }
    if (const float sim = simulateLoad.load (std::memory_order_relaxed); sim > 0.0f)
    {
        const auto until = t0 + (juce::int64) (sim * n / sr * (double) juce::Time::getHighResolutionTicksPerSecond());
        while (juce::Time::getHighResolutionTicks() < until) {}
    }
    noteLoad (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), n);
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

void Session::renderSpan (int n, double ppq0, double bpm, const Outs& outs, juce::MidiBuffer& live, bool playing, const HostInfo& h)
{
    const double spb = 60.0 / bpm * sr;
    const double ppq1 = ppq0 + n / spb;
    headPpq = ppq0; headBpm = bpm; headPlaying = playing;
    float macros[kMacros];
    for (int i = 0; i < kMacros; ++i) macros[i] = global->finalValue (GP_MACRO1 + i);
    bool anySolo = false, anyArmed = false;
    for (auto& t : tracks) { anySolo |= t->solo.load (std::memory_order_relaxed); anyArmed |= t->armed.load (std::memory_order_relaxed); }
    for (auto* t : order) t->buf.clear (0, n);
    master->buf.clear (0, n);
    const int sel = selectedTrack.load();
    // feedback routes: what their source put out one buffer ago arrives now, through a soft limiter
    for (auto& tp : tracks)
        for (size_t ri = 0; ri < tp->routes->size() && ri < tp->routeRun.size(); ++ri)
        {
            const Route& r = (*tp->routes)[ri];
            if (! r.feedback) continue;
            Track* dst = trackByUid (r.dst); auto& run = *tp->routeRun[ri];
            if (! dst) continue;
            const int dw = dst->buf.getNumChannels();
            for (int c = 0; c < r.channels && c < (int) run.delay.size(); ++c)
            {
                const int dc = r.dstOffset + c;
                if (dc >= dw) continue;
                auto& dl = run.delay[(size_t) c];
                if (dl.mask < bs + n) continue;
                float* d = dst->buf.getWritePointer (dc);
                for (int i = 0; i < n; ++i)
                {
                    float x = dl.buf[(size_t) ((dl.w + i - bs) & dl.mask)];
                    const float a = std::abs (x);
                    if (a > 0.5f) x = (x < 0 ? -1.0f : 1.0f) * (0.5f + 0.5f * std::tanh ((a - 0.5f) * 2.0f));   // never above 1
                    d[i] += x;
                }
            }
        }

    // 1. every track's notes for this span, before any audio: a hit on one track of a choke group cuts the others
    for (auto* tp : order)
    {
        Track& t = *tp;
        juce::MidiBuffer& scratchMidi = t.seqMidi;
        scratchMidi.clear(); t.firstHit = -1;
        if (! t.isReturn())
        {
            int index = 0; for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].get() == &t) { index = (int) i; break; }
            if (playing) resolveTrack (t, index, ppq0);
            const bool takesLive = t.armed.load (std::memory_order_relaxed) || (! anyArmed && sel == index);
            if (takesLive)
                for (const auto m : live)
                {
                    const auto msg = m.getMessage();
                    if (msg.isNoteOnOrOff() || msg.isPitchWheel() || msg.isAftertouch() || msg.isChannelPressure() || (msg.isController() && msg.getControllerNumber() == 1))
                        scratchMidi.addEvent (msg, m.samplePosition);
                    if (msg.isNoteOn()) { t.lastVel = msg.getFloatVelocity(); t.hitEnvT = 0; t.lastHitPpq = ppq0 + m.samplePosition / spb; t.lastRandom = t.rng.uni(); t.hits.fetch_add (1); }
                }
            modulate (t, ppq0, n, bpm);
            if (playing && t.kind == TrackKind::Instrument) sequence (t, ppq0, ppq1, spb, bpm, scratchMidi);
            else
            {
                for (int k = 0; k < t.nPending;)
                {
                    auto& p = t.pending[(size_t) k];
                    if (p.left < n) { scratchMidi.addEvent (juce::MidiMessage::noteOff (1, p.note), std::max (0, p.left)); p = t.pending[(size_t) --t.nPending]; }
                    else { p.left -= n; ++k; }
                }
                t.playStep.store (-1);
            }
        }
        else modulate (t, ppq0, n, bpm);
        if (t.chokeGroup.load (std::memory_order_relaxed) > 0)
            for (const auto m : scratchMidi) if (m.getMessage().isNoteOn()) { t.firstHit = m.samplePosition; break; }
    }
    // 2. the audio, in routing order
    for (auto* tp : order) renderTrack (*tp, n, ppq0, bpm, macros, anySolo, outs, tp->seqMidi, h, playing);
    modulate (*master, ppq0, n, bpm);
    scratchMidi.clear();
    renderTrack (*master, n, ppq0, bpm, macros, anySolo, outs, scratchMidi, h, playing);

    // the browser's audition: straight to the main pair, past the master chain
    if (auto* pv = previewNow.get(); pv && pv->sample)
    {
        const auto& d = pv->sample->data; const int ns = d.getNumSamples(), nc = d.getNumChannels();
        const double step = pv->sample->rate / sr; const int first = masterOut.load() - 1;
        for (int c = 0; c < 2; ++c)
        {
            float* o = first + c >= 0 && first + c < kOutChannels ? outs.ch[(size_t) (first + c)] : nullptr;
            if (! o) continue;
            const float* src = d.getReadPointer (std::min (c, nc - 1));
            double pos = pv->pos;
            for (int i = 0; i < n && pos < ns - 1; ++i, pos += step) o[i] += hermiteAt (src, ns, pos) * pv->gain;
        }
        pv->pos += step * n;
        if (pv->pos >= ns - 1) previewOn.store (false);
    }

    // recording taps
    if (recording.load (std::memory_order_relaxed) && playing)
    {
        const bool pu = punch.load() && loopOn.load();
        const double lS = loopStart.load() * 4.0, lE = loopEnd.load() * 4.0;
        for (auto& tp2 : tracks)
        {
            Track& t = *tp2;
            if (! t.recWriter || ! t.armed.load (std::memory_order_relaxed)) continue;
            const bool inside = ! pu || (ppq0 >= lS - 1.0e-9 && ppq0 < lE - 1.0e-9);
            if (! t.recActive.load() && inside && t.recWritten == 0)
            {
                t.recActive.store (true);
                t.recStartPpq = ppq0 - (t.inputSource.load() == -2 ? h.inputLatency / spb : 0.0);
            }
            if (! t.recActive.load()) continue;
            if (pu && ppq0 >= lE - 1.0e-9) { t.recActive.store (false); continue; }
            std::array<const float*, kMaxWidth> ptr {};
            const int src = t.inputSource.load();
            int ch = t.recChannels;
            if (src == -2)
            {
                const int first = t.inputChannel.load() - 1;
                for (int c = 0; c < ch; ++c) { const int ic = first + c; ptr[(size_t) c] = (h.in && ic >= 0 && ic < h.numIn) ? h.in[ic] : nullptr; }
            }
            else
            {
                Track* s = src == 0 ? master.get() : trackByUid (src);
                if (! s) continue;
                for (int c = 0; c < ch; ++c) ptr[(size_t) c] = c < s->post.getNumChannels() ? s->post.getReadPointer (c) : nullptr;
            }
            for (int c = 0; c < ch; ++c) if (! ptr[(size_t) c]) { recScratch.clear (c, 0, n); ptr[(size_t) c] = recScratch.getReadPointer (c); }
            if (t.recWriter->write (ptr.data(), n)) t.recWritten += n; else t.recDropped += n;
        }
    }
}

// which pattern plays now, and since when
void Session::resolveTrack (Track& t, int index, double ppq0)
{
    const auto m = (PlayMode) mode.load (std::memory_order_relaxed);
    const double stepBeats = stepBeatsOf (t, headBpm);
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
            double total = 0;
            for (int p : ch) total += t.patternAt (p) ? t.patternAt (p)->span() : 16.0;
            if (total > 0)
            {
                const double E = std::max (0.0, (ppq0 - t.anchor) / stepBeats + 1.0e-9);
                const long long pass = (long long) std::floor (E / total); const double rem = E - (double) pass * total; double cum = 0;
                size_t k = 0;
                for (; k < ch.size(); ++k) { const double len = t.patternAt (ch[k]) ? t.patternAt (ch[k])->span() : 16.0; if (rem < cum + len) break; cum += len; }
                if (k >= ch.size()) { k = ch.size() - 1; cum -= t.patternAt (ch[k]) ? t.patternAt (ch[k])->span() : 16.0; }
                t.activePattern = ch[k];
                t.seqAnchor = t.anchor + ((double) pass * total + cum) * stepBeats;
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
            const int p = (*scenes)[(size_t) (*song)[(size_t) found].scene].patternFor (index);
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
    t.forEachDevice ([] (Device& d) { std::fill (d.modOffset.begin(), d.modOffset.end(), 0.0f); });
    if (t.isMaster()) std::fill (global->modOffset.begin(), global->modOffset.end(), 0.0f);   // the master's modulators reach the globals (tempo, swing, macros)
    const auto& mods = *t.mods;
    if (mods.empty() || t.modState.size() != mods.size()) return;
    const double dt = n / sr;
    // a modulator can move another modulator's rate (device -1, param = its index): collected first
    std::array<float, 64> rateMod {};
    for (size_t i = 0; i < mods.size(); ++i)
    {
        const Modulator& m = mods[i];
        ModState& s = t.modState[i];
        if (! m.on) { s.out = 0; continue; }
        float u = 0.0f;
        const float rate = i < rateMod.size() ? std::exp2 (rateMod[i] * 4.0f) : 1.0f;   // ±4 octaves
        auto cycleOf = [&] (float& ph, bool& newCycle)
        {
            newCycle = false;
            if (m.sync && std::abs (rate - 1.0f) < 1.0e-4f)
            {
                const double beats = divisionBeats (m.div);
                const double from = m.retrig ? t.lastHitPpq : 0.0;
                const double cycles = std::max (0.0, ppq0 - from) / beats + m.phase;
                const double cy = std::floor (cycles);
                if (cy != s.cycle) { s.cycle = cy; newCycle = true; }
                ph = (float) (cycles - cy);
            }
            else
            {
                const double hz = m.sync ? bpm / 60.0 / divisionBeats (m.div) * rate : (double) m.hz * rate;
                float p = s.ph + (float) (dt * hz);
                if (p >= 1.0f) { p -= std::floor (p); newCycle = true; }
                if (! s.init) { newCycle = true; }
                s.ph = p; ph = p + m.phase; ph -= std::floor (ph);
            }
        };
        switch (m.src)
        {
            case MOD_LFO:
            {
                float ph; bool nc; cycleOf (ph, nc);
                if (nc) { s.shPrev = s.sh; s.sh = t.rng.uni(); }
                if (m.shape == 9)
                {
                    const double r = m.sync ? bpm / 60.0 / divisionBeats (m.div) : (double) m.hz;
                    s.walk += t.rng.bi() * (float) std::sqrt (r * dt) * 0.35f + (0.5f - s.walk) * (float) (r * dt) * 0.2f;
                    u = s.walk = juce::jlimit (0.0f, 1.0f, s.walk);
                }
                else u = lfoValue (m.shape, skewPhase (std::min (ph, 0.99999f), m.skew), s.sh, s.shPrev, m.pw);
                break;
            }
            case MOD_GATE: { float ph; bool nc; cycleOf (ph, nc); if (nc) s.gate = t.rng.uni() < m.prob ? 1.0f : 0.0f; u = s.gate; break; }
            case MOD_RANDOM: { float ph; bool nc; cycleOf (ph, nc); if (nc) s.sh = t.rng.uni(); u = s.sh; break; }
            case MOD_CHAOS: { float ph; bool nc; cycleOf (ph, nc); if (nc) s.chaos = 3.9f * s.chaos * (1.0f - s.chaos); if (s.chaos < 0.001f || s.chaos > 0.999f) s.chaos = 0.37f; u = s.chaos; break; }
            case MOD_ENV:
            {
                if (t.hitEnvT >= 0)
                {
                    const double a = m.attackMs * 0.001, d = std::max (0.001, m.decayMs * 0.001);
                    const double et = t.hitEnvT;
                    u = et < a ? (float) (et / std::max (1.0e-4, a)) : (float) std::exp (-(et - a) / d * 4.0);
                }
                break;
            }
            case MOD_FOLLOW: u = s.follow = t.showFollow.load (std::memory_order_relaxed); break;
            case MOD_STEPS:
            {
                const int len = juce::jlimit (1, kStepMod, m.stepLen);
                if (m.sync && std::abs (rate - 1.0f) < 1.0e-4f)
                {
                    const long long idx = (long long) std::floor (std::max (0.0, ppq0 - t.seqAnchor) / divisionBeats (m.div));
                    u = m.steps[(size_t) (idx % len)];
                }
                else
                {
                    // off the grid: `hz` steps a second (or the division's rate, bent by another modulator)
                    const double hz = (m.sync ? bpm / 60.0 / divisionBeats (m.div) : (double) m.hz) * rate;
                    s.ph += (float) (dt * hz / len); s.ph -= std::floor (s.ph);
                    u = m.steps[(size_t) juce::jlimit (0, len - 1, (int) (s.ph * (float) len))];
                }
                break;
            }
            case MOD_FOURSES:
            {
                // four ramps at unrelated rates, each bouncing between the one below and the one above: none of them repeats
                // (and each one's speed leans on where another of them is, so they never fall into step)
                static const float ratio[4] = { 1.0f, 1.37f, 1.93f, 2.71f };
                const double hz = (m.sync ? bpm / 60.0 / divisionBeats (m.div) : (double) m.hz) * rate;
                int steps = juce::jlimit (1, 64, (int) std::ceil (dt * hz * 16.0)); const float h = (float) (dt * hz * 2.0) / (float) steps;
                for (int it = 0; it < steps; ++it)
                    for (int k = 0; k < 4; ++k)
                    {
                        float& x = s.four[(size_t) k]; float& d = s.fourDir[(size_t) k];
                        x += d * h * ratio[k] * (0.45f + 1.1f * s.four[(size_t) ((k + 2) & 3)]);
                        const float lo = k > 0 ? s.four[(size_t) k - 1] : 0.0f, hi = k < 3 ? s.four[(size_t) k + 1] : 1.0f;
                        if (x >= hi) { x = hi; d = -1.0f; } else if (x <= lo) { x = lo; d = 1.0f; }
                    }
                u = juce::jlimit (0.0f, 1.0f, s.four[(size_t) juce::jlimit (0, 3, m.macro)]);
                break;
            }
            case MOD_MACRO: u = global->finalValue (GP_MACRO1 + juce::jlimit (0, kMacros - 1, m.macro)); break;
            case MOD_VEL: u = t.lastVel; break;
            case MOD_HIT: u = t.lastRandom; break;
            case MOD_CC: u = cc[(size_t) juce::jlimit (0, 127, m.cc)].load (std::memory_order_relaxed); break;
            default: break;
        }
        s.init = true;
        if (m.slew > 0.001f) { const float k = 1.0f - std::exp ((float) -dt / (0.002f + 2.0f * m.slew * m.slew)); s.out += (u - s.out) * k; }
        else s.out = u;
        const float v = s.out;
        const float off = m.polarity == 1 ? v * m.depth : m.polarity == 2 ? -v * m.depth : (v * 2.0f - 1.0f) * m.depth;
        if (m.device == -1) { if (m.param >= 0 && m.param < (int) rateMod.size()) rateMod[(size_t) m.param] += off; continue; }
        if (m.param < 0) continue;
        auto* d = t.isMaster() && m.device == 0 ? global.get() : t.device (m.device);
        if (! d || m.param >= d->numParams()) continue;
        d->modOffset[(size_t) m.param] += off;
    }
    if (t.hitEnvT >= 0) { t.hitEnvT += (float) dt; if (t.hitEnvT > 20.0f) t.hitEnvT = -1; }
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
            bool found = false;
            double dist = 0;   // in steps: every step on the way counts with its own duration
            for (int j = 1; j <= len && ! found; ++j)
            {
                dist += juce::jlimit (0.05f, 16.0f, pat.s[(size_t) ((si + j - 1) % len)].dur);
                const Step& nx = pat.s[(size_t) ((si + j) % len)];
                if (! nx.on) continue;
                for (auto& nl : nx.locks) if (nl.param == l.param && nl.device == l.device) { d->lock (l.param, l.value, true, nl.value, e, e + dist * stepBeats); found = true; break; }
            }
            if (! found) d->lock (l.param, l.value, false, l.value, e, e);
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
    const double stepBeats = stepBeatsOf (t, bpm);
    const int len = juce::jlimit (1, kMaxSteps, pat.length);
    const float sw = juce::jlimit (0.5f, 0.75f, swing() + t.swingOffset.load (std::memory_order_relaxed));
    const float micro = microMs.load (std::memory_order_relaxed), shift = t.shiftMs.load (std::memory_order_relaxed), pm = t.probMul.load (std::memory_order_relaxed);
    const double msToPpq = bpm / 60000.0;
    const bool fl = fill.load (std::memory_order_relaxed);
    const int root = t.rootNote.load (std::memory_order_relaxed);
    const double rel0 = ppq0 - t.seqAnchor, rel1 = ppq1 - t.seqAnchor;
    // where every step starts, in steps: each step lasts its own duration (1 on an even grid, so step k starts at k)
    std::array<double, kMaxSteps + 1> at;
    at[0] = 0.0; for (int i = 0; i < len; ++i) at[(size_t) i + 1] = at[(size_t) i] + (double) juce::jlimit (0.05f, 16.0f, pat.s[(size_t) i].dur);
    const double span = at[(size_t) len], loopBeats = span * stepBeats;
    if (rel0 < 0) t.playStep.store (-1);
    else
    {
        const double u = std::fmod (rel0 / stepBeats, span);
        int k = 0; while (k < len - 1 && at[(size_t) k + 1] <= u) ++k;
        t.playStep.store (k);
    }
    // the evolving copy: on every new pass a few steps of the written pattern flip or change weight, and some that had
    // changed come home again, so the pattern wanders around what is written instead of away from it
    const float evo = t.evolve.load (std::memory_order_relaxed);
    if (rel0 >= 0)
    {
        const long long now = (long long) std::floor (rel0 / loopBeats) + t.loopOffset;
        if (now != t.evoPass)
        {
            const bool first = t.evoPass < 0;
            t.evoPass = now;
            if (evo <= 0.0005f) { t.flip.fill (0); t.drift.fill (0.0f); }
            else if (! first)
                for (int i = 0; i < len; ++i)
                {
                    if (t.flip[(size_t) i] != 0 || std::abs (t.drift[(size_t) i]) > 0.001f) { if (t.evoRng.uni() < 0.35f) { t.flip[(size_t) i] = 0; t.drift[(size_t) i] = 0.0f; } }
                    else if (t.evoRng.uni() < evo * 0.2f) { if (t.evoRng.uni() < 0.5f) t.flip[(size_t) i] = 1; else t.drift[(size_t) i] = t.evoRng.bi() * 0.35f; }
                }
        }
    }
    const long long l0 = std::max (0LL, (long long) std::floor (rel0 / loopBeats) - 1), l1 = (long long) std::floor (rel1 / loopBeats) + 1;
    for (long long pass = l0; pass <= l1; ++pass)
    for (int si = 0; si < len; ++si)
    {
        const long long loop = pass + t.loopOffset;
        Step evolved;
        const Step* sp = &pat.s[(size_t) si];
        if (evo > 0.0005f && (t.flip[(size_t) si] != 0 || t.drift[(size_t) si] != 0.0f))
        {
            evolved = *sp;
            if (t.flip[(size_t) si] != 0) { evolved.on = ! evolved.on; if (evolved.on) evolved.vel = 0.55f; }
            evolved.vel = juce::jlimit (0.1f, 1.0f, evolved.vel + t.drift[(size_t) si]);
            sp = &evolved;
        }
        const Step& st = *sp;
        if (! st.on) continue;
        const double own = (at[(size_t) si + 1] - at[(size_t) si]) * stepBeats;   // this step's own length in beats
        const double base = t.seqAnchor + ((double) pass * span + at[(size_t) si]) * stepBeats + ((si & 1) ? (sw - 0.5) * 2.0 * own : 0.0) + (st.micro * micro + shift) * msToPpq;
        if (base > ppq1 + own + 1.0 || base + own + 1.0 < ppq0) continue;
        const int rat = juce::jlimit (1, 8, st.ratchet);
        for (int r = 0; r < rat; ++r)
        {
            // the hits of a ratchet: even, or bent so they speed up towards the end of the step or slow down
            double ru = (double) r / rat;
            if (st.rcurve > 0.001f) ru = 1.0 - std::pow (1.0 - ru, 1.0 + 2.0 * st.rcurve); else if (st.rcurve < -0.001f) ru = std::pow (ru, 1.0 - 2.0 * st.rcurve);
            const double e = base + ru * own;
            if (e < ppq0 - 1.0e-9 || e >= ppq1 - 1.0e-9) continue;
            if (r == 0)
            {
                const bool ok = condOk (st.cond, loop, fl, t.prevFired);
                const bool fired = ok && t.rng.uni() <= st.prob * pm;
                t.prevFired = ok;
                if (! fired) break;
                applyLocks (t, pat, si, e, stepBeats);
                for (auto& l : st.locks) if (l.param >= 0 && l.device == 0) t.snapMixer = true;
                t.lastVel = st.vel; t.lastRandom = t.rng.uni(); t.lastHitPpq = e; t.hitEnvT = 0;
                t.hits.fetch_add (1);
            }
            const int smp = juce::jlimit (0, n - 1, (int) ((e - ppq0) * spb));
            const int note = juce::jlimit (0, 127, st.slice >= 0 ? 36 + st.slice : root + st.pitch);
            const float vel = juce::jlimit (0.02f, 1.0f, st.vel * (r == 0 ? 1.0f : 0.85f - 0.05f * r));
            double lenBeats = std::max (0.02, (double) st.len) * stepBeats;
            if (r < rat - 1) lenBeats = std::min (lenBeats, own / rat * 0.9);
            const int lenS = std::max (1, (int) (lenBeats * spb));
            // the note, and the notes stacked on it when the step plays a chord
            const int* stack = nullptr; const int extra = st.slice >= 0 ? 0 : chordNotes (st.chord, stack);
            for (int k = -1; k < extra; ++k)
            {
                const int nn = k < 0 ? note : juce::jlimit (0, 127, note + stack[k]);
                out.addEvent (juce::MidiMessage::noteOn (1, nn, vel), smp);
                if (smp + lenS < n) out.addEvent (juce::MidiMessage::noteOff (1, nn), smp + lenS);
                else if (t.nPending < (int) t.pending.size()) t.pending[(size_t) t.nPending++] = { nn, smp + lenS - n };
            }
        }
    }
}

// ---- audio clips -----------------------------------------------------------------------------------
/** the source-sample shift of grain k of a stretched clip (computed once, kept in the clip state) */
double Session::grainOffset (ClipState* st, const AudioClip& c, int k, double stretch, double ratio, double fileRate)
{
    if (! st || k < 0 || k >= (int) st->off.size()) return 0.0;
    float& o = st->off[(size_t) k];
    if (o != kUnknown) return o;
    const auto& smp = c.sample->data;
    const int ns = smp.getNumSamples();
    const double offS = c.offset * fileRate;                               // file samples
    const double natural = offS + ((double) k * kHop / sr) / stretch * fileRate;   // where grain k would start untouched
    const double step = ratio * fileRate / sr;                              // file samples per output sample inside a grain
    // 1. an onset inside this grain's output span pins the grain
    for (int on : c.sample->onsets)
    {
        const double tOn = ((on - offS) / fileRate) * stretch * sr;        // output sample of the onset
        const double k0 = (double) k * kHop;
        if (tOn >= k0 && tOn < k0 + kGrain) { o = (float) (on - natural - (tOn - k0) * step); return o; }
        if (tOn >= k0 + kGrain) break;
    }
    if (k == 0) { o = 0.0f; return 0.0; }
    // 2. otherwise continue the previous grain in phase: search ±L around the natural start for the best match
    const double prevStart = offS + ((double) (k - 1) * kHop / sr) / stretch * fileRate + grainOffset (st, c, k - 1, stretch, ratio, fileRate);
    const double refStart = prevStart + kHop * step;                        // where grain k-1 would be now
    const int W = 256, L = (int) std::min (1200.0, fileRate * 0.025);
    const float* d = smp.getReadPointer (0);
    auto corr = [&] (double a, double b)
    {
        double num = 0, ea = 0, eb = 0;
        for (int i = 0; i < W; ++i)
        {
            const int ia = (int) (a + i * step), ib = (int) (b + i * step);
            if (ia < 0 || ib < 0 || ia >= ns || ib >= ns) return -2.0;
            num += d[ia] * d[ib]; ea += d[ia] * d[ia]; eb += d[ib] * d[ib];
        }
        return num / std::sqrt (ea * eb + 1.0e-9);
    };
    int best = 0; double bestC = -3;
    for (int dlt = -L; dlt <= L; dlt += 4) { const double cc = corr (natural + dlt, refStart); if (cc > bestC) { bestC = cc; best = dlt; } }
    for (int dlt = best - 3; dlt <= best + 3; ++dlt) { const double cc = corr (natural + dlt, refStart); if (cc > bestC) { bestC = cc; best = dlt; } }
    o = (float) best;
    return o;
}

void Session::playClips (Track& t, int n, double ppq0, double bpm, bool playing)
{
    if (! playing) return;
    const double spb = 60.0 / bpm * sr, ppq1 = ppq0 + n / spb;
    auto* L = t.inst.getWritePointer (0); auto* R = t.inst.getWritePointer (1);
    const int iw = t.inst.getNumChannels();
    for (size_t ci = 0; ci < t.aclips->size(); ++ci)
    {
        const AudioClip& c = (*t.aclips)[ci];
        ClipState* st = ci < t.clipState.size() ? t.clipState[ci].get() : nullptr;
        if (! c.sample || c.bars <= 0 || ! st) continue;
        const double c0 = c.startBar * 4.0, c1 = (c.startBar + c.bars) * 4.0;
        if (c1 <= ppq0 || c0 >= ppq1) continue;
        const auto& smp = c.sample->data;
        const int nch = smp.getNumChannels(), ns = smp.getNumSamples();
        if (ns < 4) continue;
        const double fileRate = c.sample->rate;
        const double clipSecs = c.bars * 4.0 * 60.0 / bpm;         // output seconds
        const double srcAvail = std::max (0.0, c.sample->seconds() - c.offset);
        const double ratio = std::exp2 (c.pitch / 12.0);            // playback speed for repitch, grain pitch for stretch
        double stretch = 1.0;                                        // output seconds per source second
        if (c.mode == 1) stretch = std::max (0.05, c.stretch);
        else if (c.mode == 2) stretch = srcAvail > 1.0e-6 ? clipSecs / srcAvail : 1.0;
        const int s0 = std::max (0, (int) std::ceil ((c0 - ppq0) * spb - 1.0e-9)), s1 = std::min (n, (int) std::ceil ((c1 - ppq0) * spb - 1.0e-9));
        const double fadeIn = c.fadeIn * sr, fadeOut = c.fadeOut * sr, clipLen = clipSecs * sr;
        const float g = c.gain;
        const int G = kGrain, H = kHop;   // stretch grains (~43 ms at 48 k), half overlap
        for (int i = s0; i < s1; ++i)
        {
            const double tOut = (ppq0 - c0) * spb + i;              // output samples since the clip start
            if (tOut < 0) continue;
            float env = g;
            if (fadeIn > 1 && tOut < fadeIn) env *= (float) (tOut / fadeIn);
            if (fadeOut > 1 && clipLen - tOut < fadeOut) env *= (float) std::max (0.0, (clipLen - tOut) / fadeOut);
            auto read = [&] (double srcSec, int ch) -> float
            {
                double sec = c.reverse ? srcAvail - srcSec : srcSec;
                if (c.loop && srcAvail > 0) { sec = std::fmod (sec, srcAvail); if (sec < 0) sec += srcAvail; }
                else if (sec < 0 || sec >= srcAvail) return 0.0f;
                const double pos = (c.offset + sec) * fileRate;
                return hermiteAt (smp.getReadPointer (ch), ns, pos);
            };
            float outL = 0, outR = 0;
            if (c.mode == 0)
            {
                const double srcSec = tOut / sr * ratio;
                outL = read (srcSec, 0); outR = nch > 1 ? read (srcSec, 1) : outL;
                // a file wider than stereo (an ambisonic recording, a resampled 24-channel master) plays every channel the track has
                for (int ch = 2; ch < nch && ch < iw; ++ch) t.inst.getWritePointer (ch)[i] += read (srcSec, ch) * env;
            }
            else
            {
                // wsola: two overlapping grains on a fixed output grid (grain k starts at k·H out and reads the source
                // around k·H / stretch); each grain's source start is shifted so it continues the previous grain in phase,
                // and a grain that holds an onset is pinned so the onset lands exactly where it belongs in time
                const long long k1 = (long long) std::floor (tOut / H), k0 = k1 - 1;
                for (long long k = k0; k <= k1; ++k)
                {
                    if (k < 0) continue;
                    const double gStart = (double) k * H;
                    const double w = tOut - gStart;                     // 0 … G
                    if (w < 0 || w >= G) continue;
                    const float win = 0.5f - 0.5f * std::cos (kTwoPi * (float) (w / G));
                    const double off = grainOffset (st, c, (int) k, stretch, ratio, fileRate);
                    const double srcSec = gStart / sr / stretch + off / fileRate + w / sr * ratio;
                    outL += read (srcSec, 0) * win; outR += (nch > 1 ? read (srcSec, 1) : read (srcSec, 0)) * win;
                    for (int ch = 2; ch < nch && ch < iw; ++ch) t.inst.getWritePointer (ch)[i] += read (srcSec, ch) * win * env;
                }
            }
            L[i] += outL * env; R[i] += outR * env;
        }
    }
}

void Session::renderTrack (Track& t, int n, double ppq0, double bpm, const float* macros, bool anySolo, const Outs& outs, juce::MidiBuffer& midi, const HostInfo& h, bool playing)
{
    const int w = t.buf.getNumChannels();
    juce::AudioBuffer<float> buf (t.buf.getArrayOfWritePointers(), w, n);
    // 1. the track's own sources: instrument, clips, monitored input → inst (stereo)
    if (! t.isMaster())
    {
        t.inst.clear (0, n);
        const int iw = t.inst.getNumChannels();
        juce::AudioBuffer<float> ib (t.inst.getArrayOfWritePointers(), 2, n);
        if (t.kind == TrackKind::Instrument && t.instrument)
        {
            // the suite's instruments are stereo; a hosted one may write more channels (up to the track's width)
            const int ow = t.instrument->kind == Device::Kind::Internal ? 2 : juce::jlimit (2, iw, t.instrument->outWidth);
            juce::AudioBuffer<float> wide (t.inst.getArrayOfWritePointers(), ow, n);
            // (the notes are copied: the device clears what it is given, and the choke below wants to see them)
            scratchMidi.clear(); scratchMidi.addEvents (midi, 0, n, 0);
            t.instrument->apply (macros, ppq0); t.instrument->process (wide, scratchMidi);
            if (const int grp = t.chokeGroup.load (std::memory_order_relaxed); grp > 0)
            {
                int cut = -1;
                for (auto& o : tracks) if (o.get() != &t && o->chokeGroup.load (std::memory_order_relaxed) == grp && o->firstHit >= 0 && (cut < 0 || o->firstHit < cut)) cut = o->firstHit;
                const float k = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));   // 2 ms: a cut, not a click
                auto next = midi.cbegin(); const auto end = midi.cend();
                for (int i = 0; i < n; ++i)
                {
                    if (i == cut) t.chokeTarget = 0.0f;
                    while (next != end && (*next).samplePosition <= i) { if ((*next).getMessage().isNoteOn()) { t.chokeTarget = 1.0f; if (i != cut) t.chokeGain = std::max (t.chokeGain, 0.5f); } ++next; }
                    t.chokeGain += (t.chokeTarget - t.chokeGain) * k;
                    for (int c = 0; c < ow; ++c) t.inst.getWritePointer (c)[i] *= t.chokeGain;
                }
            }
            else t.chokeGain = t.chokeTarget = 1.0f;
        }
        else if (t.kind == TrackKind::Audio)
        {
            playClips (t, n, ppq0, bpm, playing);
            const int mon = t.monitor.load (std::memory_order_relaxed);
            if (h.in && h.numIn > 0 && t.inputSource.load() == -2 && (mon == (int) Monitor::In || (mon == (int) Monitor::Auto && t.armed.load (std::memory_order_relaxed))))
            {
                const int first = t.inputChannel.load() - 1, cnt = juce::jlimit (1, iw, t.inputCount.load());
                for (int c = 0; c < cnt; ++c) { const int ic = first + c; if (ic >= 0 && ic < h.numIn) juce::FloatVectorOperations::add (t.inst.getWritePointer (c), h.in[ic], n); }
                if (cnt == 1) ib.addFrom (1, 0, ib, 0, 0, n);
            }
        }
        const int sw = juce::jlimit (1, std::min (iw, w), std::max (2, t.sourceWidth));
        if (t.inDelay > 0)
            for (int c = 0; c < sw && c < (int) t.instDelay.size(); ++c) { auto* d = t.inst.getWritePointer (c); for (int i = 0; i < n; ++i) { t.instDelay[(size_t) c].push (d[i]); d[i] = t.instDelay[(size_t) c].readInt (t.inDelay); } }
        for (int c = 0; c < sw; ++c) juce::FloatVectorOperations::add (buf.getWritePointer (c), t.inst.getReadPointer (c), n);
    }
    // 2. the chain
    const bool bypassChain = t.isMaster() && masterBypass.load (std::memory_order_relaxed);
    if (! bypassChain)
        for (auto& d : t.inserts)
        {
            d->apply (macros, ppq0);
            if (d->kind == Device::Kind::Studio || d->kind == Device::Kind::Hosted)
            {
                Track* key = t.keyTrack.load() == 0 ? master.get() : trackByUid (t.keyTrack.load());
                d->keyL = key && key != &t ? key->post.getReadPointer (0) : nullptr;
                d->keyR = key && key != &t ? key->post.getReadPointer (std::min (1, key->post.getNumChannels() - 1)) : nullptr;
            }
            emptyMidi.clear();
            d->process (buf, emptyMidi);
        }
    // 3. the strip's gain (routes are post-fader; pan only on the first pair)
    t.mixer->apply (macros, ppq0);
    float lvl = t.isMaster() ? global->finalValue (GP_MASTER) : t.mixer->finalValue (MP_LEVEL);
    float gain = lvl <= 0.001f ? 0.0f : dbToGain (mixerLevelDb (lvl));
    if (t.mixer->finalValue (MP_MUTE) > 0.5f) gain = 0.0f;
    if (! t.isMaster() && ! t.isReturn() && anySolo && ! t.solo.load (std::memory_order_relaxed)) gain = 0.0f;
    t.levelSm.target = gain;
    t.panSm.target = t.mixer->finalValue (MP_PAN) * 2.0f - 1.0f;
    if (t.snapMixer) { t.snapMixer = false; t.levelSm.snap (gain); t.panSm.snap (t.panSm.target); for (size_t r = 0; r < returns.size(); ++r) t.sendSm[r].snap (sendGain (t.mixer->finalValue (MP_SEND1 + (int) r))); }
    // 4. routes read the chain output before alignment (each route has its own delay to the destination's arrival)
    for (size_t ri = 0; ri < t.routes->size() && ri < t.routeRun.size(); ++ri)
    {
        const Route& r = (*t.routes)[ri]; auto& run = *t.routeRun[ri];
        Track* dst = trackByUid (r.dst);
        if (! dst) continue;
        const int dw = dst->buf.getNumChannels();
        const float g = gain * r.gain;
        for (int c = 0; c < r.channels && c < (int) run.delay.size(); ++c)
        {
            const int sc = r.srcOffset + c, dc = r.dstOffset + c;
            if (sc >= w || dc >= dw) continue;
            const float* s = buf.getReadPointer (sc); float* d = dst->buf.getWritePointer (dc);
            auto& dl = run.delay[(size_t) c];
            if (r.feedback) { for (int i = 0; i < n; ++i) dl.push (s[i] * g); continue; }   // read at the top of the next buffer
            if (run.samples > 0) for (int i = 0; i < n; ++i) { dl.push (s[i]); d[i] += dl.readInt (run.samples) * g; }
            else for (int i = 0; i < n; ++i) d[i] += s[i] * g;
        }
    }
    // 5. alignment: the chain output lands at the same time as every other track's
    if (t.alignDelay > 0 && t.alignDelay < (int) t.align[0].buf.size() - bs - 8)
        for (int c = 0; c < w; ++c) { auto* d = buf.getWritePointer (c); auto& dl = t.align[(size_t) c]; for (int i = 0; i < n; ++i) { dl.push (d[i]); d[i] = dl.readInt (t.alignDelay); } }
    // 6. post = buf × gain × pan; meters
    juce::AudioBuffer<float> post (t.post.getArrayOfWritePointers(), w, n);
    for (int i = 0; i < n; ++i)
    {
        const float g = t.levelSm.tick();
        float gl, gr; panGains (t.panSm.tick(), gl, gr);
        for (int c = 0; c < w; ++c) post.getWritePointer (c)[i] = buf.getReadPointer (c)[i] * g * (c == 0 ? gl : c == 1 ? gr : 1.0f);
    }
    float pk0 = 0;
    for (int c = 0; c < w && c < kMaxWidth; ++c) { const float pk = post.getMagnitude (c, 0, n); t.meter[(size_t) c].store (pk); if (c < 2) pk0 = std::max (pk0, pk); }
    { float f = t.showFollow.load(); f = pk0 > f ? pk0 : f * std::exp (-(float) n / (0.08f * (float) sr)); t.showFollow.store (f); }
    // 7. sends (tracks only, into the returns at their width)
    if (! t.isMaster() && ! t.isReturn())
        for (size_t r = 0; r < returns.size(); ++r)
        {
            const float sv = t.mixer->finalValue (MP_SEND1 + (int) r);
            const float sg = sendGain (sv);
            auto& sm = t.sendSm[r];
            sm.target = sg;
            if (sg <= 0.0f && sm.cur < 1.0e-5f) { sm.cur = 0.0f; continue; }
            const bool pre = t.sendPre[r].load (std::memory_order_relaxed);
            auto& ret = *returns[r];
            const int rw = std::min (w, ret.buf.getNumChannels());
            for (int i = 0; i < n; ++i)
            {
                const float g = sm.tick();
                for (int c = 0; c < rw; ++c) ret.buf.getWritePointer (c)[i] += (pre ? buf.getReadPointer (c)[i] : post.getReadPointer (c)[i]) * g;
            }
        }
    // 8. out: the master, and / or the device channels
    const bool multi = multiOut.load (std::memory_order_relaxed);
    if (t.isMaster())
    {
        const int first = masterOut.load() - 1;
        for (int c = 0; c < w; ++c)
        {
            const int oc = first + c;
            float* o = oc >= 0 && oc < kOutChannels ? outs.ch[(size_t) oc] : nullptr;
            if (o) juce::FloatVectorOperations::add (o, post.getReadPointer (c), n);
        }
        return;
    }
    if (t.toMaster.load (std::memory_order_relaxed))
    {
        const int mw = std::min (w, master->buf.getNumChannels());
        for (int c = 0; c < mw; ++c) juce::FloatVectorOperations::add (master->buf.getWritePointer (c), post.getReadPointer (c), n);
    }
    const int oc0 = t.outChannel.load (std::memory_order_relaxed) - 1;
    if (multi && oc0 >= 0)
    {
        bool any = false;
        const bool delayed = t.directExtra > 0 && (int) t.direct.size() >= w && (int) t.direct[0].buf.size() > t.directExtra + bs + 4;
        for (int c = 0; c < w; ++c)
        {
            const int oc = oc0 + c;
            float* o = oc < kOutChannels ? outs.ch[(size_t) oc] : nullptr;
            const float* src = post.getReadPointer (c);
            if (delayed) { auto& dl = t.direct[(size_t) c]; if (o) { any = true; for (int i = 0; i < n; ++i) { dl.push (src[i]); o[i] += dl.readInt (t.directExtra); } } else for (int i = 0; i < n; ++i) dl.push (src[i]); }
            else if (o) { any = true; juce::FloatVectorOperations::add (o, src, n); }
        }
        if (! any && ! t.toMaster.load (std::memory_order_relaxed))
        {
            // the device has no such channels: fold into the master so nothing is lost
            const int mw = std::min (w, master->buf.getNumChannels());
            for (int c = 0; c < mw; ++c) juce::FloatVectorOperations::add (master->buf.getWritePointer (c), post.getReadPointer (c), n);
        }
    }
}

// ---- export ---------------------------------------------------------------------------------------
/** every offline render starts from the same state: devices re-prepared (their noise reseeded), modulators and clocks reset */
void Session::resetForOffline()
{
    auto resetTrack = [&] (Track& t)
    {
        t.nPending = 0; t.activePattern = -1; t.silent = true; t.lastStepN = -1; t.prevFired = false; t.hitEnvT = -1; t.lastVel = 0; t.lastRandom = 0; t.lastHitPpq = -1e9;
        t.rng.seed ((uint32_t) (t.uid * 7919 + 13));
        t.evoRng.seed ((uint32_t) (t.uid * 104729 + 7)); t.flip.fill (0); t.drift.fill (0.0f); t.evoPass = -1; t.chokeGain = t.chokeTarget = 1.0f;
        for (auto& m : t.modState) m = ModState();
        for (auto& c : t.clipState) if (c) std::fill (c->off.begin(), c->off.end(), kUnknown);
        t.forEachDevice ([&] (Device& d) { d.release(); d.setOffline (true); d.prepare (sr, bs); d.unlockAll(); });
        t.levelSm.snap (t.levelSm.target); t.panSm.snap (t.panSm.target); for (auto& s : t.sendSm) s.snap (s.target); t.snapMixer = true;
        for (auto& d : t.align) d.clear(); for (auto& d : t.instDelay) d.clear(); for (auto& d : t.direct) d.clear();
        for (auto& r : t.routeRun) for (auto& d : r->delay) d.clear();
    };
    for (auto* t : order) resetTrack (*t);
    resetTrack (*master);
    for (auto& t : tracks) if (mode.load() == (int) PlayMode::Session) { t->pendingPattern.store (t->pattern.load()); t->pendingAt.store (0.0); }
}

namespace
{
struct Dither { Rng r; float tick (float lsb) { return (r.uni() - r.uni()) * lsb; } };   // tpdf, ±1 lsb

/** the second pass of an export: the 32-bit float render into the file the options ask for (type, word length, rate,
    normalised, dithered). `peak` is the render's loudest sample. The float render is deleted afterwards. */
static bool finishExport (const juce::File& render, const juce::File& dest, const ExportOptions& opt, float peak, juce::String& error)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (new juce::FileInputStream (render), true));
    if (! rd) { error = "cannot read the render " + render.getFullPathName(); return false; }
    const int ch = (int) rd->numChannels; const double srcRate = rd->sampleRate;
    const double dstRate = opt.sampleRate > 0 ? opt.sampleRate : srcRate;
    std::unique_ptr<juce::AudioFormat> fmt;
    if (opt.format == 1) fmt = std::make_unique<juce::AiffAudioFormat>();
    else if (opt.format == 2) fmt = std::make_unique<juce::FlacAudioFormat>();
    else fmt = std::make_unique<juce::WavAudioFormat>();
    int bits = opt.bits <= 16 ? 16 : opt.bits >= 32 && opt.format == 0 ? 32 : 24;
    dest.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream> (dest);
    if (os->failedToOpen()) { error = "cannot write " + dest.getFullPathName(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> w (fmt->createWriterFor (os.get(), dstRate, (unsigned) ch, bits, {}, 0));
    if (! w) { error = juce::String (fmt->getFormatName()) + " cannot be written with " + juce::String (ch) + " channels, " + juce::String (bits) + " bit at " + juce::String (dstRate, 0) + " hz"; return false; }
    os.release();
    const float gain = opt.normalise && peak > 1.0e-9f ? juce::Decibels::decibelsToGain (opt.normaliseDb) / peak : 1.0f;
    const float lsb = bits >= 32 || ! opt.dither ? 0.0f : 1.0f / (float) (1 << (bits - 1));
    juce::AudioFormatReaderSource src (rd.get(), false);
    juce::ResamplingAudioSource rs (&src, false, ch);
    rs.setResamplingRatio (srcRate / dstRate);
    const int block = 4096;
    rs.prepareToPlay (block, dstRate);
    juce::AudioBuffer<float> b (ch, block);
    Dither dither;
    juce::int64 left = (juce::int64) std::ceil ((double) rd->lengthInSamples * dstRate / srcRate);
    bool ok = true;
    while (left > 0 && ok)
    {
        const int n = (int) std::min<juce::int64> (block, left);
        juce::AudioSourceChannelInfo info (&b, 0, n);
        if (std::abs (dstRate - srcRate) < 1.0e-6) src.getNextAudioBlock (info); else rs.getNextAudioBlock (info);
        for (int c = 0; c < ch; ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < n; ++i) { float v = d[i] * gain; if (lsb > 0.0f) v += dither.tick (lsb); d[i] = v; }
        }
        ok = w->writeFromAudioSampleBuffer (b, 0, n);
        left -= n;
    }
    w.reset(); rs.releaseResources();
    src.setLooping (false);
    rd.reset();
    render.deleteFile();
    if (! ok) error = "writing " + dest.getFileName() + " failed";
    return ok;
}
}

bool Session::renderStems (const juce::File& dir, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error, ExportOptions opt)
{
    if (! prepared) { error = "audio not running"; return false; }
    if (toBar <= fromBar) { error = "nothing to render"; return false; }
    dir.createDirectory();
    rendering.store (true);
    const juce::SpinLock::ScopedLockType sl (lock);
    const bool wasRunning = running.load(); const double savedPpq = internalPpq.load();
    running.store (false);
    juce::WavAudioFormat wav;
    struct Stem { Track* t; std::unique_ptr<juce::AudioFormatWriter> w; int ch; juce::File render, dest; float peak = 0; };
    std::vector<Stem> stems;
    auto wanted = [&] (const Track* t) { return opt.only.empty() || std::find (opt.only.begin(), opt.only.end(), t->uid) != opt.only.end(); };
    auto open = [&] (Track* t, const juce::String& name)
    {
        if (! wanted (t)) return;
        const auto legal = juce::File::createLegalFileName (name);
        auto f = dir.getChildFile (legal + ".render.wav"); f.deleteFile();
        const int ch = t->width.load();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) ch, 32, {}, 0));
        if (w) stems.push_back ({ t, std::move (w), ch, f, dir.getChildFile (legal + ExportOptions::extension (opt.format)) });
    };
    for (int i = 0; i < (int) tracks.size(); ++i) open (tracks[(size_t) i].get(), juce::String (i + 1).paddedLeft ('0', 2) + " " + tracks[(size_t) i]->name);
    for (size_t r = 0; r < returns.size(); ++r) if (returns[r]->inserts.size() > 0 || ! opt.only.empty()) open (returns[r].get(), "return " + juce::String (r + 1) + " " + returns[r]->name);
    open (master.get(), "master");
    resetForOffline();
    std::vector<float> outMem ((size_t) (kOutChannels * bs), 0.0f);
    Outs outs; for (int c = 0; c < kOutChannels; ++c) outs.ch[(size_t) c] = outMem.data() + (size_t) c * (size_t) bs;
    double p = fromBar * 4.0; const double end = (toBar + (opt.tail ? 1.0 : 0.0)) * 4.0;
    juce::MidiBuffer none; HostInfo h;
    juce::AudioBuffer<float> tmp (kMaxWidth, bs);
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
        renderSpan (len, p, bpm, outs, none, p < toBar * 4.0, h);
        for (auto& st : stems)
        {
            for (int c = 0; c < st.ch; ++c) st.peak = std::max (st.peak, st.t->post.getMagnitude (c, 0, len));
            st.w->writeFromAudioSampleBuffer (st.t->post, 0, len);
        }
        p += len / spb;
        if (std::abs (p - limit) < 1.0e-6) p = limit;
        if (progress && ! progress ((float) ((p - fromBar * 4.0) / (end - fromBar * 4.0)))) ok = false;
    }
    for (auto& st : stems) st.w.reset();
    if (ok) for (auto& st : stems) if (! finishExport (st.render, st.dest, opt, st.peak, error)) ok = false;
    if (! ok) for (auto& st : stems) st.render.deleteFile();
    stems.clear();
    for (auto* t : order) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
    internalPpq.store (savedPpq); running.store (wasRunning);
    for (auto* t : order) t->forEachDevice ([] (Device& d) { d.setOffline (false); });
    master->forEachDevice ([] (Device& d) { d.setOffline (false); });
    rendering.store (false);
    if (! ok && error.isEmpty()) error = "cancelled";
    return ok;
}

bool Session::renderMaster (const juce::File& file, double fromBar, double toBar, const std::function<bool (float)>& progress, juce::String& error, ExportOptions opt, bool preMaster)
{
    if (! prepared) { error = "audio not running"; return false; }
    if (toBar <= fromBar) { error = "nothing to render"; return false; }
    rendering.store (true);
    const juce::SpinLock::ScopedLockType sl (lock);
    const bool wasRunning = running.load(); const double savedPpq = internalPpq.load(); const bool savedBypass = masterBypass.load();
    running.store (false);
    if (preMaster) masterBypass.store (true);
    juce::WavAudioFormat wav;
    const auto render = file.getSiblingFile (file.getFileNameWithoutExtension() + ".render.wav"); render.deleteFile();
    const int ch = master->width.load();   // a 16 / 24 / 64-channel master is one file with that many channels
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (render), sr, (unsigned) ch, 32, {}, 0));
    if (! w) { error = "cannot write " + render.getFullPathName(); rendering.store (false); return false; }
    resetForOffline();
    std::vector<float> outMem ((size_t) (kOutChannels * bs), 0.0f);
    Outs outs; for (int c = 0; c < kOutChannels; ++c) outs.ch[(size_t) c] = outMem.data() + (size_t) c * (size_t) bs;
    double p = fromBar * 4.0; const double end = (toBar + (opt.tail ? 1.0 : 0.0)) * 4.0;
    juce::MidiBuffer none; HostInfo h;
    float peak = 0.0f;
    PeakLimiter limiter; const int look = (int) (0.003 * sr);
    if (opt.limit) limiter.prepare (look, true);
    juce::AudioBuffer<float> tmp (kMaxWidth, bs);
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
        renderSpan (len, p, bpm, outs, none, p < toBar * 4.0, h);
        for (int c = 0; c < ch; ++c) tmp.copyFrom (c, 0, master->post, c, 0, len);
        if (opt.limit && ch >= 2)
            limiter.process (tmp.getWritePointer (0), tmp.getWritePointer (1), len, true, dbToGain (opt.ceilingDb), look, tcoef (60.0f, (float) sr), tcoef (400.0f, (float) sr), true, 1.0f);
        for (int c = 0; c < ch; ++c) peak = std::max (peak, tmp.getMagnitude (c, 0, len));
        w->writeFromAudioSampleBuffer (tmp, 0, len);
        p += len / spb;
        if (std::abs (p - limit) < 1.0e-6) p = limit;
        if (progress && ! progress ((float) ((p - fromBar * 4.0) / (end - fromBar * 4.0)))) ok = false;
    }
    w.reset();
    if (ok) ok = finishExport (render, file, opt, peak, error);
    render.deleteFile();
    for (auto* t : order) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
    master->forEachDevice ([] (Device& d) { d.reset(); });
    masterBypass.store (savedBypass);
    internalPpq.store (savedPpq); running.store (wasRunning);
    for (auto* t : order) t->forEachDevice ([] (Device& d) { d.setOffline (false); });
    master->forEachDevice ([] (Device& d) { d.setOffline (false); });
    rendering.store (false);
    if (! ok && error.isEmpty()) error = "cancelled";
    return ok;
}

Track* Session::renderProjectToTrack (double fromBar, double toBar, bool preMaster, juce::String& error)
{
    auto f = audioDir().getChildFile ("resample " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav");
    ExportOptions o; o.bits = 32; o.dither = false; o.tail = false;
    if (! renderMaster (f, fromBar, toBar, nullptr, error, o, preMaster)) return nullptr;
    auto* t = addTrack ("resample", -1, TrackKind::Audio);
    if (! t) return nullptr;
    if (master->width.load() != 2) setWidth (*t, master->width.load());
    addAudioClip (*t, f, fromBar, &error);
    editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) c.bars = toBar - fromBar; });
    return t;
}

juce::File Session::renderTrackFile (Track& src, double fromBar, double toBar, juce::String& error)
{
    if (! prepared) { error = "audio not running"; return {}; }
    if (toBar <= fromBar) { error = "nothing to render"; return {}; }
    auto f = audioDir().getChildFile (juce::File::createLegalFileName (src.name) + " bounce " + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".wav");
    int k = 2; while (f.existsAsFile()) f = audioDir().getChildFile (juce::File::createLegalFileName (src.name) + " bounce " + juce::String (k++) + ".wav");
    {
        rendering.store (true);
        const juce::SpinLock::ScopedLockType sl (lock);
        const bool wasRunning = running.load(); const double savedPpq = internalPpq.load();
        running.store (false);
        juce::WavAudioFormat wav; f.deleteFile();
        const int ch = src.width.load();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) ch, 32, {}, 0));
        if (! w) { error = "cannot write"; rendering.store (false); return {}; }
        resetForOffline();
        std::vector<float> outMem ((size_t) (kOutChannels * bs), 0.0f);
        Outs outs; for (int c = 0; c < kOutChannels; ++c) outs.ch[(size_t) c] = outMem.data() + (size_t) c * (size_t) bs;
        double p = fromBar * 4.0; const double end = toBar * 4.0;
        juce::MidiBuffer none; HostInfo h;
        while (p < end)
        {
            automate (p); global->apply (nullptr, p);
            const double bpm = tempo(); const double spb = 60.0 / bpm * sr;
            const double limit = std::floor (p / 4.0 + 1.0e-9) * 4.0 + 4.0;
            int len = juce::jlimit (1, bs, (int) std::ceil ((limit - p) * spb - 1.0e-6));
            std::fill (outMem.begin(), outMem.end(), 0.0f);
            headPpq = p;
            renderSpan (len, p, bpm, outs, none, true, h);
            w->writeFromAudioSampleBuffer (src.post, 0, len);
            p += len / spb;
            if (std::abs (p - limit) < 1.0e-6) p = limit;
        }
        w.reset();
        for (auto* t : order) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
        internalPpq.store (savedPpq); running.store (wasRunning);
        for (auto* t : order) t->forEachDevice ([] (Device& d) { d.setOffline (false); });
        master->forEachDevice ([] (Device& d) { d.setOffline (false); });
        rendering.store (false);
    }
    return f;
}

Track* Session::renderTrackToTrack (Track& src, double fromBar, double toBar, juce::String& error)
{
    const auto f = renderTrackFile (src, fromBar, toBar, error);
    if (f == juce::File()) return nullptr;
    const int at = src.isMaster() || src.isReturn() ? -1 : indexOf (&src) + 1;
    auto* t = addTrack (src.name + " bounce", at, TrackKind::Audio);
    if (! t) return nullptr;
    if (src.width.load() != 2) setWidth (*t, src.width.load());
    addAudioClip (*t, f, fromBar, &error);
    editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) c.bars = toBar - fromBar; });
    return t;
}

Track* Session::renderTrackToSampler (Track& src, double fromBar, double toBar, juce::String& error)
{
    const auto f = renderTrackFile (src, fromBar, toBar, error);
    if (f == juce::File()) return nullptr;
    auto dev = createDevice ("st:sampler", &error);
    if (! dev) return nullptr;
    if (auto* h = dynamic_cast<SampleHolder*> (dev->processor())) h->setSampleFile (f);
    // slices on the transients, so the steps can play the bounce back in any order
    const int mode = dev->paramIndexForId ("mode"), slices = dev->paramIndexForId ("slicemode");
    if (mode >= 0) dev->setBase (mode, 1.0f);
    if (slices >= 0) dev->setBase (slices, 1.0f);
    const int at = src.isMaster() || src.isReturn() ? -1 : indexOf (&src) + 1;
    auto* t = addTrack (src.name + " sampled", at, TrackKind::Instrument);
    if (! t) return nullptr;
    setInstrument (*t, std::move (dev));
    // sixteen steps that play the first sixteen slices in order: a starting point to re-order
    editPattern (*t, 0, [] (Pattern& p) { p = Pattern(); p.length = 16; for (int i = 0; i < 16; ++i) { p.s[(size_t) i].on = true; p.s[(size_t) i].slice = i; p.s[(size_t) i].len = 1.0f; } });
    return t;
}

bool Session::exportMidi (const juce::File& file, double fromBar, double toBar, juce::String& error)
{
    if (toBar <= fromBar) { error = "nothing to export"; return false; }
    const juce::SpinLock::ScopedLockType sl (lock);
    const bool wasRunning = running.load(); running.store (false);
    const int tpq = 960;
    juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq);
    const double bpm = tempo(); const double spbs = 60.0 / bpm * sr;
    headBpm = bpm;
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
            if (t.kind != TrackKind::Instrument) continue;
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
        if (std::abs (p - limit) < 1.0e-6) p = limit;
    }
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto& s = seqs[(size_t) i];
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

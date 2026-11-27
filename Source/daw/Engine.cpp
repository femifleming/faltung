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
    double barStart = 0; const auto bar = meter->barAt (headPpq, &barStart);
    const auto& sig = meter->sigOf (bar);
    p.setTimeSignature (TimeSignature { std::max (1, (int) std::lround (sig.num)), sig.den });
    p.setBarCount (bar);
    p.setPpqPositionOfLastBarStart (barStart);
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
        loudness.prepare (sr);
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
        controllerMessage (msg);
        if (! msg.isController()) continue;
        const int c = msg.getControllerNumber(); const float v = msg.getControllerValue() / 127.0f;
        cc[(size_t) c].store (v);
        lastCc.store (c);
        for (auto& map : *midiMaps)
            if (map.cc == c)
                if (auto* d = resolve (map.target)) d->setBase (map.target.param, map.lo + (map.hi - map.lo) * v);
    }

    if (const auto ci = countInLeft.load(); ci > 0)
    {
        // the count-in: the transport waits, the clicks count the bars in, what comes in is still heard
        const double bpm0 = tempo(), spbt = 60.0 / bpm0 * sr;
        renderSpan (n, internalPpq.load(), bpm0, outs, midi, false, h);
        clicks (outs, n, countInFrom + (double) (countInTotal - ci) / spbt, spbt);   // (counted as the bars it leads into)
        if (ci - n <= 0) { countInLeft.store (0); running.store (true); }
        else countInLeft.store (ci - n);
        noteLoad (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0), n);
        return;
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
            for (auto& m : t.mfx) { m.nHeld = 0; m.orderN = 0; m.nOffs = 0; m.arpIndex = 0; }
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
        const bool mapped = tempoTable != nullptr;
        while (done < n)
        {
            // with a tempo lane the playhead goes through its table (the tempo where it is now), so the arrangement's seconds
            // and the playhead agree to the sample
            const double bpmHere = mapped ? std::max (1.0, 60.0 / 64.0 / std::max (1.0e-12, secondsAt (p + 1.0 / 64.0) - secondsAt (p))) : bpm;
            double spb = 60.0 / bpmHere * sr;
            double limit = meter->nextBarLine (p);
            if (loop && lEnd > lStart && p < lEnd - 1.0e-9) limit = std::min (limit, lEnd);
            // the arrangement: the blocks are also cut where clips start and end, so a clip anywhere (off the bar
            // line, split, trimmed, muted) starts and stops on its sample
            if (mode.load (std::memory_order_relaxed) == (int) PlayMode::Arrange)
                for (auto& t : tracks)
                    for (auto& c : *t->clips)
                    {
                        const double a = c.startBar * 4.0, b = (c.startBar + c.bars) * 4.0;
                        if (a > p + 1.0e-9) { limit = std::min (limit, a); break; }   // (sorted by start)
                        if (b > p + 1.0e-9) limit = std::min (limit, b);
                    }
            int len = (int) std::ceil ((limit - p) * spb - 1.0e-6);
            len = juce::jlimit (1, n - done, len);
            double pNext = p + len / spb;
            if (mapped)
            {
                pNext = beatAt (secondsAt (p) + len / sr);
                if (pNext > limit + 1.0e-9 && len > 1) { len = std::max (1, (int) std::floor ((secondsAt (limit) - secondsAt (p)) * sr + 1.0e-6)); pNext = beatAt (secondsAt (p) + len / sr); }
                if (pNext > p) spb = len / (pNext - p);
            }
            const double bpmSpan = 60.0 * sr / spb;
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
            renderSpan (len, p, bpmSpan, o, span, true, hs);
            if (metronome.load (std::memory_order_relaxed)) clicks (o, len, p, spb);
            p = mapped ? pNext : p + len / spb; done += len;
            if (std::abs (p - limit) < 1.0e-6) p = limit;   // land exactly on the bar line
            if (loop && lEnd > lStart && p >= lEnd - 1.0e-6)
            {
                p = lStart; for (auto& t : tracks) t->lastStepN = -1;
                // recording over the loop: a pass ends here (a take each time round)
                if (recording.load (std::memory_order_relaxed))
                    for (auto& t : tracks)
                        if (t->recWriter && t->recWritten > 0)
                        {
                            const int k = t->recWrapCount.load (std::memory_order_relaxed);
                            if (k < (int) t->recWraps.size()) { t->recWraps[(size_t) k] = t->recWritten; t->recWrapCount.store (k + 1, std::memory_order_release); }
                            t->recActive.store (true);   // (punch: in again for the next pass)
                        }
            }
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

void Session::clicks (const Outs& outs, int n, double beat0, double samplesPerBeat)
{
    const int first = masterOut.load() - 1;
    float* L = first >= 0 && first < kOutChannels ? outs.ch[(size_t) first] : nullptr;
    float* R = first + 1 >= 0 && first + 1 < kOutChannels ? outs.ch[(size_t) first + 1] : nullptr;
    const int len = (int) (0.03 * sr);
    // a click on every beat of the meter (the denominator's note): high on the bar, middle on a group (2+2+3), low on the rest
    int kind = 0;
    double next = meter->nextBeat (beat0 - 1.0e-9, &kind);
    for (int i = 0; i < n; ++i)
    {
        if ((double) i >= (next - beat0) * samplesPerBeat - 1.0e-6)
        {
            clickAge = 0; clickPh = 0; clickHz = kind == 2 ? 1760.0f : kind == 1 ? 1540.0f : 1320.0f;
            next = meter->nextBeat (next, &kind);
        }
        if (clickAge < 0) continue;
        const float env = std::exp (-(float) clickAge / (0.008f * (float) sr));
        const float v = 0.35f * env * std::sin ((float) (kTwoPi * clickPh));
        clickPh += clickHz / sr; if (clickPh >= 1.0) clickPh -= 1.0;
        if (L) L[i] += v;
        if (R) R[i] += v;
        if (++clickAge >= len) clickAge = -1;
    }
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
    for (auto& g : *groups) anySolo |= g.solo;
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
            // recording into the steps: an armed instrument track takes what is played into the pattern that is playing
            if (playing && recording.load (std::memory_order_relaxed) && t.kind == TrackKind::Instrument && t.armed.load (std::memory_order_relaxed))
            {
                const bool pu = punch.load() && loopOn.load();
                const double lS = loopStart.load() * 4.0, lE = loopEnd.load() * 4.0;
                const int pi = juce::jlimit (0, kPatterns - 1, t.activePattern >= 0 ? t.activePattern : t.pattern.load());
                const PatternPtr pp = t.patterns[(size_t) pi];
                const int plen = pp ? juce::jlimit (1, kMaxSteps, pp->length) : 16;
                const double sb = stepBeatsOf (t, bpm);
                const bool free = mode.load (std::memory_order_relaxed) == (int) PlayMode::Arrange;
                for (const auto m : live)
                {
                    const auto msg = m.getMessage();
                    const double e = ppq0 + m.samplePosition / spb;
                    if (pu && (e < lS || e >= lE)) continue;
                    if (free)
                    {
                        // the arrangement: into a note clip, as played (notes, controllers, pitch bend, pressure)
                        Track::RecNote rn; rn.free = true; rn.ppq = e;
                        if (msg.isNoteOnOrOff()) { rn.on = msg.isNoteOn(); rn.note = msg.getNoteNumber(); rn.vel = msg.getFloatVelocity(); }
                        else if (msg.isController()) { rn.kind = 1; rn.note = msg.getControllerNumber(); rn.value = msg.getControllerValue() / 127.0f; }
                        else if (msg.isPitchWheel()) { rn.kind = 2; rn.value = msg.getPitchWheelValue() / 16383.0f; }
                        else if (msg.isChannelPressure()) { rn.kind = 3; rn.value = msg.getChannelPressureValue() / 127.0f; }
                        else continue;
                        int s1, n1, s2, n2; t.noteFifo.prepareToWrite (1, s1, n1, s2, n2);
                        if (n1 > 0) { t.noteBuf[(size_t) s1] = rn; t.noteFifo.finishedWrite (1); }
                        continue;
                    }
                    if (! msg.isNoteOnOrOff()) continue;
                    Track::RecNote rn; rn.on = msg.isNoteOn(); rn.note = msg.getNoteNumber(); rn.vel = msg.getFloatVelocity(); rn.ppq = e; rn.pattern = pi; rn.stepBeats = sb;
                    // the nearest step start (steps may last different times), and how far off it the note was played
                    double span = 0; std::array<double, kMaxSteps + 1> at {};
                    for (int i = 0; i < plen; ++i) { at[(size_t) i] = span; span += pp ? juce::jlimit (0.05f, 16.0f, pp->s[(size_t) i].dur) : 1.0f; }
                    double u = std::fmod ((e - t.seqAnchor) / sb, span); if (u < 0) u += span;
                    int best = 0; double bestD = 1.0e9;
                    for (int i = 0; i <= plen; ++i) { const double st = i < plen ? at[(size_t) i] : span; const double d = u - st; if (std::abs (d) < std::abs (bestD)) { bestD = d; best = i % plen; } }
                    rn.step = best;
                    const double ms = bestD * sb * 60000.0 / bpm;
                    rn.micro = juce::jlimit (-1.0f, 1.0f, (float) (ms / std::max (1.0f, microMs.load())));
                    int s1, n1, s2, n2; t.noteFifo.prepareToWrite (1, s1, n1, s2, n2);
                    if (n1 > 0) { t.noteBuf[(size_t) s1] = rn; t.noteFifo.finishedWrite (1); }
                }
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
            if (t.kind == TrackKind::Instrument && ! t.midiFx->empty()) midiEffects (t, ppq0, n, spb, scratchMidi, playing);
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
    // the master's loudness (not while rendering offline: the export measures its own)
    if (! rendering.load (std::memory_order_relaxed)) loudness.process (master->post.getReadPointer (0), master->post.getNumChannels() > 1 ? master->post.getReadPointer (1) : nullptr, n);

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
                t.recLatency = t.inputSource.load() == -2 ? (double) h.inputLatency : 0.0;
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
    t.clipFrom = t.clipTo = -1; t.noteSrc = nullptr; t.ctrlSrc = nullptr;
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
        // follow actions: a pattern that has played its number of times hands over (on its own boundary)
        if (! t.silent && t.activePattern >= 0 && t.pendingPattern.load (std::memory_order_relaxed) == -1)
        {
            const auto& fa = (*t.follow)[(size_t) juce::jlimit (0, kPatterns - 1, t.activePattern)];
            const PatternPtr cur = t.patterns[(size_t) t.activePattern];
            if (fa.after > 0 && cur)
            {
                const double loopB = cur->span() * stepBeats, end = t.anchor + fa.after * loopB;
                if (ppq0 >= end - 1.0e-6)
                {
                    const int act = t.rng.uni() < fa.chanceA ? fa.a : fa.b;
                    auto used = [&] (int p) { return t.patterns[(size_t) p] && ! t.patterns[(size_t) p]->empty(); };
                    int next = t.activePattern;
                    auto step = [&] (int dir) { for (int k = 1; k <= kPatterns; ++k) { const int q = ((t.activePattern + dir * k) % kPatterns + kPatterns) % kPatterns; if (used (q)) return q; } return t.activePattern; };
                    std::array<int, kPatterns> pool {}; int np = 0; for (int q = 0; q < kPatterns; ++q) if (used (q)) pool[(size_t) np++] = q;
                    switch (act)
                    {
                        case FOLLOW_NEXT: next = step (1); break;
                        case FOLLOW_PREV: next = step (-1); break;
                        case FOLLOW_FIRST: next = np > 0 ? pool[0] : t.activePattern; break;
                        case FOLLOW_ANY: next = np > 0 ? pool[(size_t) std::min (np - 1, (int) (t.rng.uni() * np))] : t.activePattern; break;
                        case FOLLOW_OTHER: if (np > 1) { int q; do q = pool[(size_t) std::min (np - 1, (int) (t.rng.uni() * np))]; while (q == t.activePattern); next = q; } break;
                        default: break;
                    }
                    if (act == FOLLOW_STOP) { t.silent = true; t.activePattern = -1; }
                    else { t.activePattern = next; t.pattern.store (next); t.anchor = end; t.lastStepN = -1; t.prevFired = false; }
                }
            }
        }
        t.seqAnchor = t.anchor;
    }
    else if (m == PlayMode::Song)
    {
        // the song's entries last whole bars of the meter, one after the other from bar 1 (start: in units of 4 beats)
        long long s = 0; int found = -1; double start = 0;
        for (size_t e = 0; e < song->size(); ++e)
        {
            const auto& en = (*song)[e];
            if (ppq0 < meter->barStart (s + en.bars) - 1.0e-9) { found = (int) e; start = meter->barStart (s) / 4.0; break; }
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
        for (auto& cl : *t.clips) if (! cl.muted && bar >= cl.startBar - 1.0e-9 && bar < cl.startBar + cl.bars - 1.0e-9) { c = &cl; break; }
        // the pattern runs from where the clip starts in it (its offset: a clip split or trimmed from the left)
        const double anchor = c ? c->startBar * 4.0 - c->offset : 0.0;
        if (c) { t.clipFrom = c->startBar * 4.0; t.clipTo = (c->startBar + c->bars) * 4.0; }
        if (c && c->notes) { t.noteSrc = c->notes; t.ctrlSrc = c->ctrl; t.noteAnchor = anchor; t.silent = true; t.activePattern = -1; }
        else if (! c) { t.silent = true; t.activePattern = -1; }
        else if (c->pattern != t.activePattern || std::abs (t.anchor - anchor) > 1.0e-6 || t.silent)
        { t.activePattern = c->pattern; t.anchor = anchor; t.silent = false; t.lastStepN = -1; t.prevFired = false; }
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
    // a note clip: its notes, as they are (cut at the clip's end)
    if (t.noteSrc && t.clipTo > t.clipFrom)
    {
        const double lo = std::max (ppq0, t.clipFrom), hi = std::min (ppq1, t.clipTo);
        for (auto& nt : *t.noteSrc)
        {
            const double on = t.noteAnchor + nt.beat;
            if (on < lo - 1.0e-9 || on >= hi - 1.0e-9) continue;
            const int smp = juce::jlimit (0, n - 1, (int) std::floor ((on - ppq0) * spb + 1.0e-6));
            const int nn = juce::jlimit (0, 127, nt.pitch);
            out.addEvent (juce::MidiMessage::noteOn (1, nn, juce::jlimit (0.02f, 1.0f, nt.vel)), smp);
            const double off = std::min (on + nt.len, t.clipTo);
            const int offS = std::max (smp + 1, (int) std::floor ((off - ppq0) * spb + 1.0e-6));
            if (offS < n) out.addEvent (juce::MidiMessage::noteOff (1, nn), offS);
            else if (t.nPending < (int) t.pending.size()) t.pending[(size_t) t.nPending++] = { nn, offS - n };
        }
        if (t.ctrlSrc)
            for (auto& cv : *t.ctrlSrc)
            {
                const double at = t.noteAnchor + cv.beat;
                if (at < lo - 1.0e-9 || at >= hi - 1.0e-9) continue;
                const int smp = juce::jlimit (0, n - 1, (int) std::floor ((at - ppq0) * spb + 1.0e-6));
                if (cv.kind == kCtrlBend) out.addEvent (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) std::lround (cv.value * 16383.0f))), smp);
                else if (cv.kind == kCtrlPressure) out.addEvent (juce::MidiMessage::channelPressureChange (1, juce::jlimit (0, 127, (int) std::lround (cv.value * 127.0f))), smp);
                else out.addEvent (juce::MidiMessage::controllerEvent (1, juce::jlimit (0, 127, cv.kind), juce::jlimit (0, 127, (int) std::lround (cv.value * 127.0f))), smp);
            }
        t.playStep.store (-1); return;
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
    // the window the steps fire in: this span, inside the arrangement's clip. A block cut where a clip starts ends up
    // to a sample past it: the clip's first step is still caught (on the span's first sample)
    double lo = ppq0, hi = ppq1;
    if (t.clipTo >= 0) { hi = std::min (hi, t.clipTo); if (t.clipFrom <= ppq0 + 1.0e-9 && ppq0 - t.clipFrom < 1.0e-3) lo = t.clipFrom; }
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
        // the groove: the sixteenth of its cycle this step falls on pushes it by its share of a step (and weighs it)
        const auto* gr = t.groove.get(); const float ga = gr ? t.grooveAmount.load (std::memory_order_relaxed) : 0.0f;
        const int gk = gr ? (int) ((long long) std::llround ((double) pass * span + at[(size_t) si]) % std::max (1, gr->steps)) : 0;
        const double grooveShift = gr ? (double) gr->at (gk) * ga * stepBeats : 0.0;
        const float grooveVel = gr ? 1.0f + (gr->velAt (gk) - 1.0f) * ga : 1.0f;
        const double base = t.seqAnchor + ((double) pass * span + at[(size_t) si]) * stepBeats + ((si & 1) ? (sw - 0.5) * 2.0 * own : 0.0) + (st.micro * micro + shift) * msToPpq + grooveShift;
        if (base > ppq1 + own + 1.0 || base + own + 1.0 < ppq0) continue;
        const int rat = juce::jlimit (1, 8, st.ratchet);
        for (int r = 0; r < rat; ++r)
        {
            // the hits of a ratchet: even, or bent so they speed up towards the end of the step or slow down
            double ru = (double) r / rat;
            if (st.rcurve > 0.001f) ru = 1.0 - std::pow (1.0 - ru, 1.0 + 2.0 * st.rcurve); else if (st.rcurve < -0.001f) ru = std::pow (ru, 1.0 - 2.0 * st.rcurve);
            const double e = base + ru * own;
            if (e < lo - 1.0e-9 || e >= hi - 1.0e-9) continue;
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
            // the pitch lane: a degree of the track's scale (a note and, between the keys, a pitch bend of ±2 semitones)
            int note = 0; double bend = 0.0;
            // the slice: its own, or (-2) any slice the pattern's steps use, a new one each time it plays (chopped stabs that never repeat the same way)
            int slice = st.slice;
            if (slice == -2)
            {
                std::array<int, kMaxSteps> used {}; int nu = 0;
                for (int q = 0; q < len; ++q) if (pat.s[(size_t) q].on && pat.s[(size_t) q].slice >= 0) used[(size_t) nu++] = pat.s[(size_t) q].slice;
                slice = nu > 0 ? used[(size_t) std::min (nu - 1, (int) (t.rng.uni() * nu))] : std::min (15, (int) (t.rng.uni() * 16.0f));
            }
            if (slice >= 0) note = 36 + slice;
            else if (t.scale) { const double semis = t.scale->centsOf (st.pitch) / 100.0; const int k = (int) std::floor (semis + 0.5); bend = semis - k; note = root + k; }
            else note = root + st.pitch;
            note = juce::jlimit (0, 127, note);
            const float vel = juce::jlimit (0.02f, 1.0f, st.vel * grooveVel * (r == 0 ? 1.0f : 0.85f - 0.05f * r));
            double lenBeats = std::max (0.02, (double) st.len) * stepBeats;
            if (r < rat - 1) lenBeats = std::min (lenBeats, own / rat * 0.9);
            const int lenS = std::max (1, (int) (lenBeats * spb));
            // the note, and the notes stacked on it when the step plays a chord
            const int* stack = nullptr; const int extra = slice >= 0 ? 0 : chordNotes (st.chord, stack);
            // (a bend once sent stays with the instrument: back to the centre when the steps are on the keys again)
            if ((t.scale && t.scale->microtonal()) || std::abs (t.sentBend) > 1.0e-6)
            {
                out.addEvent (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + (int) std::lround (bend / 2.0 * 8192.0))), smp);
                t.sentBend = (float) bend;
            }
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
double Session::grainOffset (ClipState* st, const AudioClip& c, int k, const ClipTime& ct, double fileRate)
{
    if (! st || k < 0 || k >= (int) st->off.size()) return 0.0;
    float& o = st->off[(size_t) k];
    if (o != kUnknown) return o;
    const auto& smp = c.sample->data;
    const int ns = smp.getNumSamples();
    const double offS = c.offset * fileRate;                               // file samples
    const double ratio = ct.ratio;
    const double natural = offS + ct.srcAt ((double) k * kHop) * fileRate;   // where grain k would start untouched
    const double step = ratio * fileRate / sr;                              // file samples per output sample inside a grain
    // 1. an onset inside this grain's output span pins the grain
    for (int on : c.sample->onsets)
    {
        if (on < offS) continue;
        const double tOn = ct.outAt ((on - offS) / fileRate);              // output sample of the onset
        const double k0 = (double) k * kHop;
        if (tOn >= k0 && tOn < k0 + kGrain) { o = (float) (on - natural - (tOn - k0) * step); return o; }
        if (tOn >= k0 + kGrain) break;
    }
    if (k == 0) { o = 0.0f; return 0.0; }
    // 2. otherwise continue the previous grain in phase: search ±L around the natural start for the best match
    const double prevStart = offS + ct.srcAt ((double) (k - 1) * kHop) * fileRate + grainOffset (st, c, k - 1, ct, fileRate);
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
        if (! c.sample || c.bars <= 0 || ! st || c.muted) continue;
        const double c0 = c.startBar * 4.0, c1 = (c.startBar + c.bars) * 4.0;
        if (c1 <= ppq0 || c0 >= ppq1) continue;
        const auto& smp = c.sample->data;
        const int nch = smp.getNumChannels(), ns = smp.getNumSamples();
        if (ns < 4) continue;
        const double fileRate = c.sample->rate;
        // time through the tempo map: output samples since the clip began (the tempo may change inside it), and the source
        // second each output sample reads (repitch, stretch, warp to the bars, warp markers)
        const ClipTime ct = clipTime (c, fileRate);
        const double clipSecs = secondsAt (c1) - ct.sec0;            // output seconds
        const double srcAvail = ct.srcAvail;
        const double ratio = ct.ratio;                               // playback speed for repitch, grain pitch for stretch
        const double outBase = (secondsAt (ppq0) - ct.sec0) * sr;    // output samples since the clip start at this block's start
        if (const int tv = tempoVersion.load (std::memory_order_relaxed); st->tempoVer != tv) { std::fill (st->off.begin(), st->off.end(), kUnknown); st->tempoVer = tv; }
        long long natK[2] = { -9, -9 }; double natV[2] = { 0, 0 };
        auto naturalOf = [&] (long long k) { const int slot = (int) (k & 1); if (natK[slot] != k) { natK[slot] = k; natV[slot] = ct.srcAt ((double) k * kHop); } return natV[slot]; };
        const int s0 = std::max (0, (int) std::ceil ((c0 - ppq0) * spb - 1.0e-9)), s1 = std::min (n, (int) std::ceil ((c1 - ppq0) * spb - 1.0e-9));
        const double fadeIn = c.fadeIn * sr, fadeOut = c.fadeOut * sr, clipLen = clipSecs * sr;
        const float g = c.gain;
        // crossfades with the clips it overlaps (equal power), and the gain line drawn on it (db, linear between points)
        const double xInS = c.xIn * 4.0 * spb, xOutS = c.xOut * 4.0 * spb;
        const auto& gl = c.gainLine; size_t gk = 0;
        const int G = kGrain, H = kHop;   // stretch grains (~43 ms at 48 k), half overlap
        for (int i = s0; i < s1; ++i)
        {
            const double tOut = outBase + i;                         // output samples since the clip start
            if (tOut < 0) continue;
            float env = g;
            if (fadeIn > 1 && tOut < fadeIn) env *= (float) (tOut / fadeIn);
            if (fadeOut > 1 && clipLen - tOut < fadeOut) env *= (float) std::max (0.0, (clipLen - tOut) / fadeOut);
            if (xInS > 1 && tOut < xInS) env *= (float) std::sin (juce::MathConstants<double>::halfPi * tOut / xInS);
            if (xOutS > 1 && clipLen - tOut < xOutS) env *= (float) std::sin (juce::MathConstants<double>::halfPi * std::max (0.0, (clipLen - tOut) / xOutS));
            if (! gl.empty())
            {
                const double b = tOut / spb;
                while (gk + 1 < gl.size() && gl[gk + 1].first <= b) ++gk;
                double db;
                if (b <= gl.front().first) db = gl.front().second;
                else if (gk + 1 >= gl.size()) db = gl.back().second;
                else { const auto& p0 = gl[gk]; const auto& p1 = gl[gk + 1]; const double u = (b - p0.first) / std::max (1.0e-9, p1.first - p0.first); db = p0.second + (p1.second - p0.second) * u; }
                env *= (float) std::pow (10.0, db / 20.0);
            }
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
                    // (the first grain has no partner before it: its rising half is full, so the clip's start isn't faded in)
                    const float win = k == 0 && w < G / 2 ? 1.0f : 0.5f - 0.5f * std::cos (kTwoPi * (float) (w / G));
                    const double off = grainOffset (st, c, (int) k, ct, fileRate);
                    const double srcSec = naturalOf (k) + off / fileRate + w / sr * ratio;
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
    // frozen: its file, where the song is, instead of the sources and the chain
    const bool frozenNow = t.frozen != nullptr;
    if (frozenNow)
    {
        const auto& d = t.frozen->data; const int ns = d.getNumSamples();
        if (playing)
        {
            const double pos0 = (secondsAt (ppq0) - secondsAt (t.frozenFromBeat)) * t.frozen->rate, step = t.frozen->rate / sr;
            for (int c = 0; c < w; ++c)
            {
                const int sc = std::min (c, d.getNumChannels() - 1); auto* o = buf.getWritePointer (c);
                for (int i = 0; i < n; ++i) { const double p = pos0 + i * step; o[i] = p >= 0 && p < ns - 1 ? hermiteAt (d.getReadPointer (sc), ns, p) : 0.0f; }
            }
        }
    }
    // 1. the track's own sources: instrument, clips, monitored input → inst (stereo)
    if (! t.isMaster() && ! frozenNow)
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
    const bool bypassChain = (t.isMaster() && masterBypass.load (std::memory_order_relaxed)) || frozenNow;
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
            if (d->lowLatencyOff.load (std::memory_order_relaxed)) continue;
            emptyMidi.clear();
            d->process (buf, emptyMidi);
        }
    if (t.freezeTap) for (int c = 0; c < std::min (w, t.freezeTap->getNumChannels()); ++c) t.freezeTap->copyFrom (c, 0, buf, c, 0, n);
    // 3. the strip's gain (routes are post-fader; pan only on the first pair)
    t.mixer->apply (macros, ppq0);
    float lvl = t.isMaster() ? global->finalValue (GP_MASTER) : t.mixer->finalValue (MP_LEVEL);
    float gain = lvl <= 0.001f ? 0.0f : dbToGain (mixerLevelDb (lvl));
    if (t.mixer->finalValue (MP_MUTE) > 0.5f) gain = 0.0f;
    // the track's group: its vca fader scales the level, its mute mutes, its solo solos every member
    const Group* grp = nullptr;
    if (const int gid = t.group.load (std::memory_order_relaxed)) for (auto& g : *groups) if (g.id == gid) { grp = &g; break; }
    if (grp) { gain *= grp->vcaDb <= -96.0f ? 0.0f : std::pow (10.0f, grp->vcaDb / 20.0f); if (grp->mute) gain = 0.0f; }
    if (! t.isMaster() && ! t.isReturn() && anySolo && ! t.solo.load (std::memory_order_relaxed) && ! (grp && grp->solo)) gain = 0.0f;
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
static bool finishExport (const juce::File& render, const juce::File& dest, const ExportOptions& opt, float peak, juce::String& error, float* lufsOut = nullptr, float* tpOut = nullptr)
{
    juce::WavAudioFormat wav;
    // its loudness first (the integrated lufs and the true peak of what was rendered)
    float lufs = LoudnessMeter::kSilent, tp = LoudnessMeter::kSilent;
    {
        std::unique_ptr<juce::AudioFormatReader> mr (wav.createReaderFor (new juce::FileInputStream (render), true));
        if (mr)
        {
            LoudnessMeter m; m.prepare (mr->sampleRate);
            juce::AudioBuffer<float> mb ((int) std::max (1u, mr->numChannels), 8192);
            for (juce::int64 at = 0; at < mr->lengthInSamples; at += 8192)
            {
                const int n = (int) std::min<juce::int64> (8192, mr->lengthInSamples - at);
                mr->read (&mb, 0, n, at, true, true);
                m.process (mb.getReadPointer (0), mb.getNumChannels() > 1 ? mb.getReadPointer (1) : nullptr, n); m.update();
            }
            lufs = m.integrated; tp = m.truePeakDb();
        }
    }
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
    float gain = opt.normalise && peak > 1.0e-9f ? juce::Decibels::decibelsToGain (opt.normaliseDb) / peak : 1.0f;
    if (opt.loudnessTarget && lufs > -70.0f)
    {
        // to the target loudness; never past the ceiling (with the limiter on) or 0 dbtp: the true peak decides how far up
        float gDb = opt.targetLufs - lufs;
        const float ceiling = opt.limit ? opt.ceilingDb : 0.0f;
        if (tp + gDb > ceiling) gDb = ceiling - tp;
        gain = juce::Decibels::decibelsToGain (gDb);
    }
    const float gDbApplied = juce::Decibels::gainToDecibels (gain);
    if (lufsOut) *lufsOut = lufs > -70.0f ? lufs + gDbApplied : LoudnessMeter::kSilent;
    if (tpOut) *tpOut = tp > LoudnessMeter::kSilent ? tp + gDbApplied : LoudnessMeter::kSilent;
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
        const double limit = meter->nextBarLine (p);
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
        const double limit = meter->nextBarLine (p);
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
    float lu = LoudnessMeter::kSilent, tpk = LoudnessMeter::kSilent;
    if (ok) ok = finishExport (render, file, opt, peak, error, &lu, &tpk);
    exportLufs.store (lu); exportTruePeak.store (tpk);
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
            const double limit = meter->nextBarLine (p);
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
    for (auto& t : tracks) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->lastStepN = -1; for (auto& m : t->mfx) { m.nHeld = 0; m.orderN = 0; m.nOffs = 0; m.arpIndex = 0; } }
    for (auto& t : tracks) if (mode.load() == (int) PlayMode::Session) { t->pendingPattern.store (t->pattern.load()); t->pendingAt.store (0.0); }
    double p = fromBar * 4.0; const double end = toBar * 4.0;
    while (p < end)
    {
        const double limit = std::min (end, meter->nextBarLine (p));
        const int len = juce::jlimit (1, bs, (int) std::ceil ((limit - p) * spbs - 1.0e-6));
        const double ppq1 = p + len / spbs;
        for (int i = 0; i < (int) tracks.size(); ++i)
        {
            Track& t = *tracks[(size_t) i];
            if (t.kind != TrackKind::Instrument) continue;
            resolveTrack (t, i, p);
            juce::MidiBuffer mb;
            sequence (t, p, ppq1, spbs, bpm, mb);
            if (! t.midiFx->empty()) midiEffects (t, p, len, spbs, mb, true);   // (what the midi effects make is in the file too)
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
        if (i == 0)
        {
            s.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
            // the time signatures, bar by bar where they change (a midi file wants a power-of-two denominator and a
            // whole numerator: others are written as the nearest it can hold)
            juce::String last;
            for (long long b = meter->barAt (fromBar * 4.0); meter->barStart (b) < end - 1.0e-9; ++b)
            {
                const auto& sg = meter->sigOf (b);
                if (sg.text() == last) continue;
                last = sg.text();
                int den = 1; while (den * 2 <= sg.den && den < 64) den *= 2;
                const double at = std::max (0.0, meter->barStart (b) - fromBar * 4.0);
                s.addEvent (juce::MidiMessage::timeSignatureMetaEvent (std::max (1, (int) std::lround (sg.num * den / sg.den)), den), at * tpq);
            }
        }
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

namespace fal::daw
{
void Session::controllerMessage (const juce::MidiMessage& msg)
{
    // a button: a note on, or a cc going past half way
    const bool noteBtn = msg.isNoteOn(), ccBtn = msg.isController() && msg.getControllerValue() >= 64;
    if (const int learn = cmdLearning.load (std::memory_order_relaxed); learn > 0 && (noteBtn || ccBtn))
    {
        pushCommand (-(learn * 1000 + (noteBtn ? 0 : 500) + (noteBtn ? msg.getNoteNumber() : msg.getControllerNumber())));   // (the message thread stores it)
        cmdLearning.store (0);
        return;
    }
    if (noteBtn || ccBtn)
        for (auto& cm : *cmdMaps)
            if (cm.note == noteBtn && cm.number == (noteBtn ? msg.getNoteNumber() : msg.getControllerNumber())) pushCommand (cm.command);
    if (! mackie.load (std::memory_order_relaxed)) return;
    // mackie control: channel faders are pitch bend on channels 1-8 (9: the master), v-pots cc 16-23 (relative), buttons notes
    const int bank = mackieBank.load (std::memory_order_relaxed);
    if (msg.isPitchWheel())
    {
        const int ch = msg.getChannel() - 1; const float v = msg.getPitchWheelValue() / 16383.0f;
        if (ch == 8) global->setBase (GP_MASTER, v);
        else if (ch >= 0 && ch < 8 && bank + ch < (int) tracks.size()) tracks[(size_t) (bank + ch)]->mixer->setBase (MP_LEVEL, v);
        return;
    }
    if (msg.isController() && msg.getControllerNumber() >= 16 && msg.getControllerNumber() < 24)
    {
        const int ch = msg.getControllerNumber() - 16, raw = msg.getControllerValue();
        const int steps = (raw & 0x40) ? -(raw & 0x3f) : (raw & 0x3f);
        if (bank + ch < (int) tracks.size()) { auto& m = *tracks[(size_t) (bank + ch)]->mixer; m.setBase (MP_PAN, juce::jlimit (0.0f, 1.0f, m.baseValue (MP_PAN) + steps * 0.01f)); }
        return;
    }
    if (! msg.isNoteOn()) return;
    const int nn = msg.getNoteNumber();
    if (nn < 32) { pushCommand ((nn < 8 ? CMD_MCU_ARM : nn < 16 ? CMD_MCU_SOLO : nn < 24 ? CMD_MCU_MUTE : CMD_MCU_SELECT) + (nn & 7)); return; }
    switch (nn)
    {
        case 46: pushCommand (CMD_MCU_BANK_DOWN); break; case 47: pushCommand (CMD_MCU_BANK_UP); break;
        case 48: pushCommand (CMD_MCU_CH_DOWN); break;   case 49: pushCommand (CMD_MCU_CH_UP); break;
        case 91: pushCommand (CMD_BACK_BAR); break;      case 92: pushCommand (CMD_FWD_BAR); break;
        case 93: pushCommand (CMD_STOP); break;          case 94: pushCommand (CMD_PLAY); break;
        case 95: pushCommand (CMD_RECORD); break;        case 86: pushCommand (CMD_LOOP); break;
        case 89: pushCommand (CMD_CLICK); break;         case 84: pushCommand (CMD_NEXT_MARKER); break;
        case 81: pushCommand (CMD_UNDO); break;          case 80: pushCommand (CMD_SAVE); break;
        default: break;
    }
}
} // namespace fal::daw

namespace fal::daw
{
void Session::setFrozen (Track& t, SamplePtr smp, const juce::File& f, double fromBeat)
{
    { const juce::SpinLock::ScopedLockType sl (lock); t.frozen = smp; t.frozenFile = f; t.frozenFromBeat = fromBeat; }
    rebuildLatency(); dirty = true;
}
void Session::unfreezeTrack (Track& t) { setFrozen (t, nullptr, {}, 0.0); }
bool Session::freezeTrack (Track& src, juce::String& error)
{
    if (! prepared) { error = "audio not running"; return false; }
    if (src.isMaster() || src.isReturn()) { error = "only tracks freeze"; return false; }
    if (src.frozen) unfreezeTrack (src);
    const double fromBeat = 0.0, endBeat = (projectBars() + 1.0) * 4.0;
    auto f = audioDir().getChildFile (juce::File::createLegalFileName (src.name) + " frozen " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S") + ".wav");
    int k = 2; while (f.existsAsFile()) f = f.getSiblingFile (juce::File::createLegalFileName (src.name) + " frozen " + juce::String (k++) + ".wav");
    const int ch = std::max (2, src.width.load()), lat = src.latency;   // (the chain's own latency: the file is moved back by it)
    {
        rendering.store (true);
        const juce::SpinLock::ScopedLockType sl (lock);
        const bool wasRunning = running.load(); const double savedPpq = internalPpq.load();
        running.store (false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) ch, 32, {}, 0));
        if (! w) { error = "cannot write " + f.getFullPathName(); rendering.store (false); return false; }
        resetForOffline();
        std::vector<float> outMem ((size_t) (kOutChannels * bs), 0.0f);
        Outs outs; for (int c = 0; c < kOutChannels; ++c) outs.ch[(size_t) c] = outMem.data() + (size_t) c * (size_t) bs;
        juce::AudioBuffer<float> tap (ch, bs); src.freezeTap = &tap;
        double p = fromBeat; juce::MidiBuffer none; HostInfo h; int skip = lat;
        const double endWithLat = endBeat + (lat > 0 ? lat / sr * tempo() / 60.0 + 0.01 : 0.0);
        while (p < endWithLat)
        {
            automate (p); global->apply (nullptr, p);
            const double limit = meter->nextBarLine (p);
            const double spb = 60.0 / tempo() * sr;
            int len = juce::jlimit (1, bs, (int) std::ceil ((limit - p) * spb - 1.0e-6));
            double pNext = p + len / spb;
            if (tempoTable) { pNext = beatAt (secondsAt (p) + len / sr); }
            std::fill (outMem.begin(), outMem.end(), 0.0f); tap.clear();
            headPpq = p;
            renderSpan (len, p, 60.0 * (pNext - p) / (len / sr), outs, none, true, h);
            const int from = std::min (skip, len); skip -= from;
            if (len - from > 0) w->writeFromAudioSampleBuffer (juce::AudioBuffer<float> (tap.getArrayOfWritePointers(), ch, from, len - from), 0, len - from);
            p = std::abs (pNext - limit) < 1.0e-6 ? limit : pNext;
        }
        src.freezeTap = nullptr;
        w.reset();
        for (auto* t : order) { t->nPending = 0; t->activePattern = -1; t->silent = true; t->forEachDevice ([] (Device& d) { d.reset(); d.unlockAll(); }); }
        internalPpq.store (savedPpq); running.store (wasRunning);
        for (auto* t : order) t->forEachDevice ([] (Device& d) { d.setOffline (false); });
        master->forEachDevice ([] (Device& d) { d.setOffline (false); });
        rendering.store (false);
    }
    auto smp = loadSample (f, &error);
    if (! smp) return false;
    setFrozen (src, smp, f, fromBeat);
    return true;
}
} // namespace fal::daw

namespace fal::daw
{
// ---- midi effects ------------------------------------------------------------------------------------------------------
void Session::midiEffects (Track& t, double ppq0, int n, double spb, juce::MidiBuffer& io, bool playing)
{
    const auto& fxs = *t.midiFx;
    juce::MidiBuffer out;
    for (size_t fi = 0; fi < fxs.size() && fi < (size_t) kMidiFx; ++fi)
    {
        const MidiFx& fx = fxs[fi]; auto& st = t.mfx[fi];
        if (! fx.on) continue;
        out.clear();
        // notes this effect scheduled to end (arp / generator), in this block
        auto dueOffs = [&]
        {
            for (int k = 0; k < st.nOffs;)
            {
                auto& o = st.offs[(size_t) k];
                if (o.left < n) { out.addEvent (juce::MidiMessage::noteOff (1, o.note), std::max (0, o.left)); o = st.offs[(size_t) --st.nOffs]; }
                else { o.left -= n; ++k; }
            }
        };
        auto later = [&] (int note, int at) { if (st.nOffs < (int) st.offs.size()) st.offs[(size_t) st.nOffs++] = { note, at }; };
        const double divB = divisionBeats (fx.div);
        switch (fx.kind)
        {
            case MFX_CHORD:
                for (const auto m : io)
                {
                    const auto msg = m.getMessage(); out.addEvent (msg, m.samplePosition);
                    if (! msg.isNoteOnOrOff()) continue;
                    for (int k = 0; k < juce::jlimit (0, 4, fx.nIntervals); ++k)
                    {
                        const int nn = msg.getNoteNumber() + fx.intervals[(size_t) k]; if (nn < 0 || nn > 127 || fx.intervals[(size_t) k] == 0) continue;
                        out.addEvent (msg.isNoteOn() ? juce::MidiMessage::noteOn (1, nn, juce::jlimit (0.02f, 1.0f, msg.getFloatVelocity() * fx.vel)) : juce::MidiMessage::noteOff (1, nn), m.samplePosition);
                    }
                }
                break;
            case MFX_CHANCE:
                for (const auto m : io)
                {
                    const auto msg = m.getMessage();
                    if (msg.isNoteOn())
                    {
                        if (st.rng.uni() >= fx.chance) { st.held[(size_t) (msg.getNoteNumber() & 31)] = -1; continue; }   // (its note off goes too)
                        const int oct = fx.spread > 0 && st.rng.uni() < fx.spread * 0.5f ? (st.rng.uni() < 0.5f ? -12 : 12) : 0;
                        const int nn = juce::jlimit (0, 127, msg.getNoteNumber() + oct); st.held[(size_t) (msg.getNoteNumber() & 31)] = nn;
                        out.addEvent (juce::MidiMessage::noteOn (1, nn, juce::jlimit (0.02f, 1.0f, msg.getFloatVelocity() * (1.0f - fx.vel * 0.5f * st.rng.uni()))), m.samplePosition);
                    }
                    else if (msg.isNoteOff()) { const int h = st.held[(size_t) (msg.getNoteNumber() & 31)]; if (h >= 0) out.addEvent (juce::MidiMessage::noteOff (1, h), m.samplePosition); }
                    else out.addEvent (msg, m.samplePosition);
                }
                break;
            case MFX_ARP:
            {
                dueOffs();
                // the notes held (from what comes in), each note on / off at its sample, the arp's notes on its grid
                std::vector<std::pair<int, juce::MidiMessage>> ev; for (const auto m : io) ev.push_back ({ m.samplePosition, m.getMessage() });
                size_t ei = 0;
                const double p1 = ppq0 + n / spb;
                long long slot = (long long) std::ceil (ppq0 / divB - 1.0e-9);
                auto apply = [&] (int upTo)
                {
                    for (; ei < ev.size() && ev[ei].first <= upTo; ++ei)
                    {
                        const auto& msg = ev[ei].second;
                        if (msg.isNoteOn()) { const int nn = msg.getNoteNumber(); bool have = false; for (int k = 0; k < st.nHeld; ++k) if (st.held[(size_t) k] == nn) have = true; if (! have && st.nHeld < 32) { st.held[(size_t) st.nHeld++] = nn; if (st.orderN < 32) st.heldOrder[(size_t) st.orderN++] = nn; } }
                        else if (msg.isNoteOff())
                        {
                            const int nn = msg.getNoteNumber();
                            for (int k = 0; k < st.nHeld; ++k) if (st.held[(size_t) k] == nn) { st.held[(size_t) k] = st.held[(size_t) --st.nHeld]; break; }
                            for (int k = 0; k < st.orderN; ++k) if (st.heldOrder[(size_t) k] == nn) { for (int q = k; q + 1 < st.orderN; ++q) st.heldOrder[(size_t) q] = st.heldOrder[(size_t) q + 1]; --st.orderN; break; }
                        }
                        else out.addEvent (msg, ev[ei].first);
                    }
                };
                for (; playing && (double) slot * divB < p1 - 1.0e-9; ++slot)
                {
                    const int at = juce::jlimit (0, n - 1, (int) std::floor (((double) slot * divB - ppq0) * spb + 1.0e-6));
                    apply (at);
                    if (st.nHeld == 0) { st.arpIndex = 0; continue; }
                    std::array<int, 32> sorted {}; for (int k = 0; k < st.nHeld; ++k) sorted[(size_t) k] = st.held[(size_t) k];
                    std::sort (sorted.begin(), sorted.begin() + st.nHeld);
                    const int oct = juce::jlimit (1, 4, fx.octaves), total = st.nHeld * oct;
                    auto noteAt = [&] (int i) { const int base = fx.mode == 4 ? st.heldOrder[(size_t) (i % std::max (1, st.orderN))] : sorted[(size_t) (i % st.nHeld)]; return base + 12 * (i / st.nHeld); };
                    int idx;
                    if (fx.mode == 3) idx = std::min (total - 1, (int) (st.rng.uni() * total));
                    else if (fx.mode == 2)
                    {
                        const int cyc = std::max (1, 2 * total - 2); const int ph = st.arpIndex % cyc; idx = total <= 1 ? 0 : (ph < total ? ph : cyc - ph);
                    }
                    else idx = st.arpIndex % total;
                    if (fx.mode == 1) idx = total - 1 - idx;
                    ++st.arpIndex;
                    const int nn = juce::jlimit (0, 127, noteAt (idx));
                    out.addEvent (juce::MidiMessage::noteOn (1, nn, fx.vel), at);
                    const int len = std::max (1, (int) (divB * juce::jlimit (0.05f, 1.0f, fx.gate) * spb));
                    if (at + len < n) out.addEvent (juce::MidiMessage::noteOff (1, nn), at + len); else later (nn, at + len - n);
                }
                apply (n);
                break;
            }
            case MFX_GEN:
            {
                dueOffs();
                for (const auto m : io) out.addEvent (m.getMessage(), m.samplePosition);   // (what comes in plays too)
                if (! playing) break;
                static const int scales[6][8] = { { 0, 1, 2, 3, 4, 5, 6, 7 }, { 0, 2, 4, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 3, 5, 7, 10, -1, -1, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 }, { 0, 1, 3, 5, 7, 8, 10, -1 } };
                const double p1 = ppq0 + n / spb;
                for (long long slot = (long long) std::ceil (ppq0 / divB - 1.0e-9); (double) slot * divB < p1 - 1.0e-9; ++slot)
                {
                    if (st.rng.uni() >= fx.density) continue;
                    const int at = juce::jlimit (0, n - 1, (int) std::floor (((double) slot * divB - ppq0) * spb + 1.0e-6));
                    int semis;
                    if (fx.scale == 6 && t.scale) { const int deg = (int) (st.rng.uni() * (t.scale->size() * std::max (1, fx.range / 12 + 1))); semis = (int) std::lround (t.scale->centsOf (deg) / 100.0); }
                    else if (fx.scale == 0) semis = (int) (st.rng.uni() * (fx.range + 1));
                    else { const auto* sc = scales[juce::jlimit (0, 5, fx.scale)]; int cnt = 0; while (cnt < 8 && sc[cnt] >= 0) ++cnt; const int deg = (int) (st.rng.uni() * cnt * std::max (1, fx.range / 12 + 1)); semis = sc[deg % cnt] + 12 * (deg / cnt); if (semis > fx.range) semis -= 12 * ((semis - fx.range) / 12 + 1); }
                    const int nn = juce::jlimit (0, 127, t.rootNote.load() + fx.root + semis);
                    const float v = juce::jlimit (0.05f, 1.0f, fx.vel * (1.0f - fx.spread * st.rng.uni()));
                    out.addEvent (juce::MidiMessage::noteOn (1, nn, v), at);
                    const int len = std::max (1, (int) (divB * juce::jlimit (0.05f, 4.0f, fx.gate) * spb));
                    if (at + len < n) out.addEvent (juce::MidiMessage::noteOff (1, nn), at + len); else later (nn, at + len - n);
                }
                break;
            }
            default: out = io; break;
        }
        io.swapWith (out);
    }
}

// ---- grooves -----------------------------------------------------------------------------------------------------------
std::vector<GroovePtr> Session::builtinGrooves()
{
    std::vector<GroovePtr> v;
    auto mk = [&] (const juce::String& name, std::vector<float> off, std::vector<float> vel) { auto g = std::make_shared<Groove>(); g->name = name; g->steps = (int) off.size(); g->offset = off; g->vel = vel; v.push_back (g); };
    for (int sw : { 54, 58, 62, 66, 71 })
    {
        // mpc swing: every second sixteenth late by (swing - 50) % of a pair
        const float late = (sw - 50) / 50.0f;
        std::vector<float> o (16, 0.0f), w (16, 1.0f); for (int k = 1; k < 16; k += 2) { o[(size_t) k] = late; w[(size_t) k] = 0.82f; }
        mk ("mpc " + juce::String (sw) + "%", o, w);
    }
    mk ("triplet shuffle", { 0, 0.333f, 0, 0.333f, 0, 0.333f, 0, 0.333f, 0, 0.333f, 0, 0.333f, 0, 0.333f, 0, 0.333f }, { 1, 0.7f, 0.9f, 0.7f, 1, 0.7f, 0.9f, 0.7f, 1, 0.7f, 0.9f, 0.7f, 1, 0.7f, 0.9f, 0.7f });
    mk ("drunk (late backbeat)", { 0, 0.12f, 0.04f, 0.18f, 0.1f, 0.14f, 0.02f, 0.2f, 0, 0.1f, 0.06f, 0.16f, 0.12f, 0.15f, 0.03f, 0.22f }, { 1, 0.7f, 0.85f, 0.65f, 1, 0.72f, 0.8f, 0.6f, 1, 0.7f, 0.85f, 0.68f, 1, 0.74f, 0.8f, 0.62f });
    mk ("push (early offbeats)", { 0, -0.06f, -0.1f, -0.05f, 0, -0.06f, -0.1f, -0.05f, 0, -0.06f, -0.1f, -0.05f, 0, -0.06f, -0.1f, -0.05f }, { 1, 0.8f, 0.95f, 0.8f, 1, 0.8f, 0.95f, 0.8f, 1, 0.8f, 0.95f, 0.8f, 1, 0.8f, 0.95f, 0.8f });
    mk ("rominimal skip", { 0, 0.08f, -0.03f, 0.16f, 0.02f, 0.1f, -0.04f, 0.24f, 0, 0.06f, -0.02f, 0.18f, 0.03f, 0.12f, -0.05f, 0.28f }, { 1, 0.66f, 0.9f, 0.58f, 0.96f, 0.7f, 0.86f, 0.54f, 1, 0.68f, 0.92f, 0.6f, 0.94f, 0.72f, 0.84f, 0.5f });
    mk ("dilla (lazy, uneven)", { 0, 0.21f, 0.05f, 0.25f, -0.03f, 0.19f, 0.08f, 0.31f, 0.02f, 0.17f, 0.04f, 0.27f, -0.02f, 0.23f, 0.07f, 0.29f }, { 1, 0.62f, 0.88f, 0.55f, 0.97f, 0.66f, 0.83f, 0.52f, 1, 0.6f, 0.9f, 0.57f, 0.95f, 0.64f, 0.85f, 0.5f });
    return v;
}
juce::File Session::groovesDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (kDeep ? "faltung deep sndstudio" : "faltung sndstudio").getChildFile ("grooves");
    d.createDirectory(); return d;
}
static juce::String grooveText (const std::vector<float>& v) { juce::StringArray a; for (float x : v) a.add (juce::String (x, 4)); return a.joinIntoString (" "); }
static std::vector<float> grooveFloats (const juce::String& s) { std::vector<float> v; for (auto& t : juce::StringArray::fromTokens (s, " ", "")) if (t.isNotEmpty()) v.push_back (t.getFloatValue()); return v; }
std::vector<GroovePtr> Session::savedGrooves() const
{
    std::vector<GroovePtr> v;
    for (const auto& f : groovesDir().findChildFiles (juce::File::findFiles, false, "*.fgroove"))
        if (auto x = juce::XmlDocument::parse (f)) { auto g = std::make_shared<Groove>(); g->name = x->getStringAttribute ("name", f.getFileNameWithoutExtension()); g->offset = grooveFloats (x->getStringAttribute ("offset")); g->vel = grooveFloats (x->getStringAttribute ("vel")); g->steps = std::max (1, (int) g->offset.size()); v.push_back (g); }
    return v;
}
bool Session::saveGroove (const Groove& g, juce::String& error) const
{
    juce::XmlElement x ("groove"); x.setAttribute ("name", g.name); x.setAttribute ("offset", grooveText (g.offset)); x.setAttribute ("vel", grooveText (g.vel));
    const auto f = groovesDir().getChildFile (juce::File::createLegalFileName (g.name) + ".fgroove");
    if (! x.writeTo (f)) { error = "cannot write " + f.getFullPathName(); return false; }
    return true;
}
void Session::setGroove (Track& t, GroovePtr g, float amount)
{
    { const juce::SpinLock::ScopedLockType sl (lock); t.groove = std::move (g); }
    t.grooveAmount.store (amount); dirty = true;
}
GroovePtr Session::grooveFromPattern (const Track& t, int pattern) const
{
    auto p = t.patternAt (pattern); if (! p) return nullptr;
    auto g = std::make_shared<Groove>(); g->name = t.name + " p" + juce::String (pattern + 1);
    const int len = juce::jlimit (1, kMaxSteps, p->length); g->steps = len;
    const double stepMs = stepBeatsOf (t, tempo()) * 60000.0 / tempo();
    float top = 0.01f; for (int k = 0; k < len; ++k) if (p->s[(size_t) k].on) top = std::max (top, p->s[(size_t) k].vel);
    for (int k = 0; k < len; ++k) { const auto& st = p->s[(size_t) k]; g->offset.push_back (st.on ? juce::jlimit (-0.5f, 0.5f, (float) (st.micro * microMs.load() / stepMs)) : 0.0f); g->vel.push_back (st.on ? st.vel / top : 1.0f); }
    return g;
}
GroovePtr Session::grooveFromClip (ClipRef r, juce::String& error) const
{
    auto* t = const_cast<Session*> (this)->clipTrack (r); if (! t) { error = "no clip"; return nullptr; }
    // the hits: an audio clip's transients (where they play), a note clip's notes, in sixteenths from the clip's start
    std::vector<std::pair<double, float>> hits; double bars = 1;
    if (r.audio)
    {
        for (auto& c : *t->aclips) if (c.id == r.id && c.sample)
        {
            bars = c.bars; const auto ct = clipTime (c, c.sample->rate);
            const auto& d = c.sample->data;
            for (int on : c.sample->onsets)
            {
                const double src = on / c.sample->rate - c.offset; if (src < 0) continue;
                const double beat = ct.beatOf (ct.outAt (src)); if (beat < 0 || beat >= c.bars * 4.0) continue;
                float pk = 0; for (int i = on; i < std::min (d.getNumSamples(), on + (int) (0.02 * c.sample->rate)); ++i) pk = std::max (pk, std::abs (d.getSample (0, i)));
                hits.push_back ({ beat, pk });
            }
        }
    }
    else
        for (auto& c : *t->clips) if (c.id == r.id && c.notes) { bars = c.bars; for (auto& nt : *c.notes) { const double b = nt.beat - c.offset; if (b >= 0 && b < c.bars * 4.0) hits.push_back ({ b, nt.vel }); } }
    if (hits.empty()) { error = "no hits in the clip"; return nullptr; }
    const int steps = juce::jlimit (4, 64, (int) std::lround (std::min (bars, 4.0) * 16.0));
    auto g = std::make_shared<Groove>(); g->name = "groove from a clip"; g->steps = steps; g->offset.assign ((size_t) steps, 0.0f); g->vel.assign ((size_t) steps, 1.0f);
    std::vector<float> best ((size_t) steps, 9.0f), lvl ((size_t) steps, 0.0f); float top = 1.0e-6f;
    for (auto& [b, v] : hits) top = std::max (top, v);
    for (auto& [b, v] : hits)
    {
        const double u = b / 0.25; const int k = (int) std::lround (u); const int kk = ((k % steps) + steps) % steps;
        const float off = (float) (u - k);
        if (std::abs (off) < std::abs (best[(size_t) kk])) { best[(size_t) kk] = off; lvl[(size_t) kk] = v / top; }
    }
    for (int k = 0; k < steps; ++k) if (best[(size_t) k] < 8.0f) { g->offset[(size_t) k] = juce::jlimit (-0.5f, 0.5f, best[(size_t) k]); g->vel[(size_t) k] = juce::jlimit (0.1f, 1.0f, lvl[(size_t) k]); }
    return g;
}
void Session::commitGroove (Track& t, int pattern)
{
    auto g = t.groove; if (! g) return;
    const float ga = t.grooveAmount.load();
    const double stepMs = stepBeatsOf (t, tempo()) * 60000.0 / tempo();
    // the micro range must reach the groove's furthest push: widened if not, every step's micro kept in milliseconds
    double need = 0; for (int k = 0; k < g->steps; ++k) need = std::max (need, std::abs ((double) g->at (k) * ga * stepMs));
    if (need > microMs.load() - 0.01)
    {
        const float old = std::max (1.0f, microMs.load()), wide = (float) std::ceil (need + 1.0);
        for (auto& tr : tracks) for (int q = 0; q < kPatterns; ++q) if (tr->patternAt (q)) editPattern (*tr, q, [&] (Pattern& p) { for (auto& st : p.s) st.micro = juce::jlimit (-1.0f, 1.0f, st.micro * old / wide); });
        microMs.store (wide);
    }
    const double mm = std::max (1.0f, microMs.load());
    editPattern (t, pattern, [&] (Pattern& p)
    {
        double at = 0;
        for (int k = 0; k < juce::jlimit (1, kMaxSteps, p.length); ++k)
        {
            auto& st = p.s[(size_t) k]; const int gk = (int) std::llround (at) % std::max (1, g->steps);
            if (st.on) { st.micro = juce::jlimit (-1.0f, 1.0f, st.micro + (float) (g->at (gk) * ga * stepMs / mm)); st.vel = juce::jlimit (0.02f, 1.0f, st.vel * (1.0f + (g->velAt (gk) - 1.0f) * ga)); }
            at += juce::jlimit (0.05f, 16.0f, st.dur);
        }
    });
    setGroove (t, nullptr, 1.0f);
}
void Session::setFollow (Track& t, int pattern, FollowAction fa)
{
    auto v = std::make_shared<std::array<FollowAction, kPatterns>> (*t.follow); (*v)[(size_t) juce::jlimit (0, kPatterns - 1, pattern)] = fa;
    { const juce::SpinLock::ScopedLockType sl (lock); t.follow = v; }
    dirty = true;
}
void Session::setMidiFx (Track& t, std::vector<MidiFx> v)
{
    if ((int) v.size() > kMidiFx) v.resize ((size_t) kMidiFx);
    auto p = std::make_shared<const std::vector<MidiFx>> (std::move (v));
    { const juce::SpinLock::ScopedLockType sl (lock); t.midiFx = p; for (size_t i = 0; i < t.mfx.size(); ++i) { const auto seed = t.mfx[i].rng; t.mfx[i] = Track::MfxState(); t.mfx[i].rng = seed; t.mfx[i].rng.seed ((uint32_t) (t.uid * 131 + (int) i * 7 + (i < p->size() ? (*p)[i].seed : 1))); } }
    dirty = true;
}

// ---- spectral freeze ---------------------------------------------------------------------------------------------------
Track* Session::spectralFreeze (Track& src, double atBar, double seconds, juce::String& error)
{
    // the track at the playhead (a little before and after it), its spectrum from the middle, held
    const double win = std::max (0.25, 0.4 * tempo() / 60.0 / 4.0);
    const auto f = renderTrackFile (src, std::max (0.0, atBar - win * 0.5), std::max (0.0, atBar - win * 0.5) + win, error);
    if (f == juce::File()) return nullptr;
    auto smp = loadSample (f, &error); f.deleteFile();
    if (! smp) return nullptr;
    constexpr int order = 13, N = 1 << order, H = N / 4;
    const int ch = std::min (2, smp->data.getNumChannels());
    const int total = std::max (N * 2, (int) (seconds * sr));
    const int len = (total / H) * H;
    juce::AudioBuffer<float> outB (2, len); outB.clear();
    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N); for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    Rng rng; rng.seed (12345);
    for (int c = 0; c < 2; ++c)
    {
        const int sc = std::min (c, ch - 1);
        const auto* d = smp->data.getReadPointer (sc); const int ns = smp->data.getNumSamples();
        std::vector<float> buf ((size_t) N * 2, 0.0f);
        const int start = std::max (0, ns / 2 - N / 2);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = (start + i < ns ? d[start + i] : 0.0f) * window[(size_t) i];
        fft.performRealOnlyForwardTransform (buf.data());
        std::vector<float> mag ((size_t) N / 2 + 1), ph ((size_t) N / 2 + 1);
        for (int k = 0; k <= N / 2; ++k) { mag[(size_t) k] = std::hypot (buf[(size_t) 2 * k], buf[(size_t) 2 * k + 1]); ph[(size_t) k] = rng.uni() * juce::MathConstants<float>::twoPi; }
        // each frame: the same magnitudes, the phases moved on as a steady tone would (and a little drift so it breathes);
        // overlap-added round the end so the result loops without a seam
        std::vector<float> fr ((size_t) N * 2);
        auto* o = outB.getWritePointer (c);
        for (int pos = 0; pos < len; pos += H)
        {
            for (int k = 0; k <= N / 2; ++k)
            {
                ph[(size_t) k] += juce::MathConstants<float>::twoPi * (float) k * H / N + (rng.uni() - 0.5f) * 0.6f;
                fr[(size_t) 2 * k] = mag[(size_t) k] * std::cos (ph[(size_t) k]); fr[(size_t) 2 * k + 1] = mag[(size_t) k] * std::sin (ph[(size_t) k]);
            }
            for (int k = N / 2 + 1; k < N; ++k) { fr[(size_t) 2 * k] = 0; fr[(size_t) 2 * k + 1] = 0; }
            fft.performRealOnlyInverseTransform (fr.data());
            for (int i = 0; i < N; ++i) o[(pos + i) % len] += fr[(size_t) i] * window[(size_t) i] / (1.5f);
        }
    }
    const float pk = outB.getMagnitude (0, len);
    if (pk < 1.0e-9f) { error = "the track is silent there"; return nullptr; }
    outB.applyGain (0.5f / pk);
    auto file = audioDir().getChildFile (juce::File::createLegalFileName (src.name) + " frozen spectrum " + juce::Time::getCurrentTime().formatted ("%H%M%S") + ".wav");
    { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0)); if (! w) { error = "cannot write"; return nullptr; } w->writeFromAudioSampleBuffer (outB, 0, len); }
    auto* t = addTrack (src.name + " pad", indexOf (&src) + 1, TrackKind::Audio);
    if (! t) { error = "no room for a track"; return nullptr; }
    auto* c = addAudioClip (*t, file, atBar, &error);
    if (! c) return nullptr;
    const int id = c->id;
    editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& x : v) if (x.id == id) { x.loop = true; x.bars = 8.0; x.fadeIn = 0.5; x.fadeOut = 1.0; x.name = "frozen " + src.name; } });
    return t;
}
} // namespace fal::daw

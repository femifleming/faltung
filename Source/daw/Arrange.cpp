// faltung sndstudio — the arrangement edited like in any daw: clips found, moved, cut, trimmed, copied, pasted,
// muted; time inserted and removed across every track, lane and marker; the tempo map (seconds <-> beats).
// Message thread only: every edit goes through editClips / editAudioClips / editLane / editMarkers (copy on write).
#include "Session.h"
#include <set>

namespace fal::daw
{
namespace
{
constexpr double kMinPatternBars = 1.0 / 128.0, kMinAudioBars = 1.0 / 256.0, kEps = 1.0e-9;

/** source seconds an audio clip plays per bar of the timeline */
double sourcePerBar (const AudioClip& c, double bpm)
{
    const double barSecs = 4.0 * 60.0 / bpm;
    if (c.mode == 0) return barSecs * std::exp2 (c.pitch / 12.0);
    if (c.mode == 1) return barSecs / std::max (0.05, c.stretch);
    return c.bars > kEps ? c.sourceSeconds() / c.bars : barSecs;
}
} // namespace

Track* Session::clipTrack (const ClipRef& r) const
{
    for (auto& t : tracks) if (t->uid == r.track) return t.get();
    return nullptr;
}
bool Session::clipSpan (const ClipRef& r, double& start, double& bars) const
{
    auto* t = clipTrack (r);
    if (! t) return false;
    if (r.audio) { for (auto& c : *t->aclips) if (c.id == r.id) { start = c.startBar; bars = c.bars; return true; } }
    else for (auto& c : *t->clips) if (c.id == r.id) { start = c.startBar; bars = c.bars; return true; }
    return false;
}
bool Session::clipMuted (const ClipRef& r) const
{
    auto* t = clipTrack (r);
    if (! t) return false;
    if (r.audio) { for (auto& c : *t->aclips) if (c.id == r.id) return c.muted; }
    else for (auto& c : *t->clips) if (c.id == r.id) return c.muted;
    return false;
}
std::vector<ClipRef> Session::allClips() const { return clipsBetween (0.0, 1.0e12); }
std::vector<ClipRef> Session::clipsBetween (double a, double b, const std::vector<int>& uids) const
{
    std::vector<ClipRef> v;
    for (auto& t : tracks)
    {
        if (! uids.empty() && std::find (uids.begin(), uids.end(), t->uid) == uids.end()) continue;
        for (auto& c : *t->clips) if (c.startBar < b - kEps && c.startBar + c.bars > a + kEps) v.push_back ({ t->uid, c.id, false });
        for (auto& c : *t->aclips) if (c.startBar < b - kEps && c.startBar + c.bars > a + kEps) v.push_back ({ t->uid, c.id, true });
    }
    return v;
}

void Session::moveClips (const std::vector<ClipRef>& refs, double delta)
{
    // never before bar 1: the whole block stops at the start
    double first = 1.0e12;
    for (auto& r : refs) { double s, b; if (clipSpan (r, s, b)) first = std::min (first, s); }
    if (first > 1.0e11) return;
    delta = std::max (delta, -first);
    for (auto& t : tracks)
    {
        std::set<int> pat, aud;
        for (auto& r : refs) if (r.track == t->uid) (r.audio ? aud : pat).insert (r.id);
        if (! pat.empty()) editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (pat.count (c.id)) c.startBar = std::max (0.0, c.startBar + delta); });
        if (! aud.empty()) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (aud.count (c.id)) c.startBar = std::max (0.0, c.startBar + delta); });
    }
}

std::vector<ClipRef> Session::splitClips (const std::vector<ClipRef>& refs, double at)
{
    std::vector<ClipRef> made;
    for (auto& r : refs)
    {
        auto* t = clipTrack (r);
        double s, b;
        if (! t || ! clipSpan (r, s, b) || at <= s + kEps || at >= s + b - kEps) continue;
        const int before = nextClipId;
        if (r.audio) splitClip (*t, r.id, at);
        else
        {
            editClips (*t, [&] (std::vector<Clip>& v)
            {
                for (size_t i = 0; i < v.size(); ++i)
                {
                    if (v[i].id != r.id) continue;
                    Clip right = v[i];
                    right.id = 0; right.startBar = at; right.bars = s + b - at; right.offset = v[i].offset + (at - s) * 4.0;
                    v[i].bars = at - s;
                    v.push_back (right);
                    break;
                }
            });
        }
        if (nextClipId > before) made.push_back ({ t->uid, before, r.audio });
    }
    return made;
}

void Session::trimClipStart (const ClipRef& r, double newStart)
{
    auto* t = clipTrack (r);
    if (! t) return;
    if (r.audio)
    {
        const double bpm = tempo();
        editAudioClips (*t, [&] (std::vector<AudioClip>& v)
        {
            for (auto& c : v)
            {
                if (c.id != r.id) continue;
                const double end = c.startBar + c.bars;
                double ns = juce::jlimit (0.0, end - kMinAudioBars, newStart);
                const double per = sourcePerBar (c, bpm);
                double off = c.offset + (ns - c.startBar) * per;
                if (off < 0) { ns = c.startBar - c.offset / std::max (1.0e-9, per); off = 0; }   // the file has no more before its start
                c.offset = off; c.bars = end - ns; c.startBar = ns;
            }
        });
    }
    else
    {
        double loopBeats = 0;
        editClips (*t, [&] (std::vector<Clip>& v)
        {
            for (auto& c : v)
            {
                if (c.id != r.id) continue;
                const double end = c.startBar + c.bars;
                const double ns = juce::jlimit (0.0, end - kMinPatternBars, newStart);
                double off = c.offset + (ns - c.startBar) * 4.0;
                // the pattern loops: an offset before its start wraps round to the same place in the loop
                if (auto p = t->patternAt (c.pattern)) loopBeats = p->span() * stepBeatsOf (*t, tempo());
                if (loopBeats > kEps) { off = std::fmod (off, loopBeats); if (off < 0) off += loopBeats; }
                else off = std::max (0.0, off);
                c.offset = off; c.bars = end - ns; c.startBar = ns;
            }
        });
    }
}

void Session::setClipLength (const ClipRef& r, double bars)
{
    auto* t = clipTrack (r);
    if (! t) return;
    if (r.audio) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == r.id) c.bars = std::max (kMinAudioBars, bars); });
    else editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == r.id) c.bars = std::max (kMinPatternBars, bars); });
}

void Session::deleteClips (const std::vector<ClipRef>& refs)
{
    for (auto& t : tracks)
    {
        std::set<int> pat, aud;
        for (auto& r : refs) if (r.track == t->uid) (r.audio ? aud : pat).insert (r.id);
        if (! pat.empty()) editClips (*t, [&] (std::vector<Clip>& v) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const Clip& c) { return pat.count (c.id) > 0; }), v.end()); });
        if (! aud.empty()) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const AudioClip& c) { return aud.count (c.id) > 0; }), v.end()); });
    }
}

void Session::setClipsMuted (const std::vector<ClipRef>& refs, bool muted)
{
    for (auto& t : tracks)
    {
        std::set<int> pat, aud;
        for (auto& r : refs) if (r.track == t->uid) (r.audio ? aud : pat).insert (r.id);
        if (! pat.empty()) editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (pat.count (c.id)) c.muted = muted; });
        if (! aud.empty()) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (aud.count (c.id)) c.muted = muted; });
    }
}

Session::ClipBoard Session::copyClips (const std::vector<ClipRef>& refs) const
{
    ClipBoard b; b.startBar = 1.0e12; b.endBar = 0;
    for (auto& r : refs)
    {
        auto* t = clipTrack (r);
        if (! t) continue;
        ClipBoard::Item it; it.track = t->uid; it.row = indexOf (t); it.audio = r.audio;
        bool found = false;
        if (r.audio) { for (auto& c : *t->aclips) if (c.id == r.id) { it.aclip = c; found = true; } }
        else for (auto& c : *t->clips) if (c.id == r.id) { it.clip = c; found = true; }
        if (! found) continue;
        const double s = r.audio ? it.aclip.startBar : it.clip.startBar, len = r.audio ? it.aclip.bars : it.clip.bars;
        b.startBar = std::min (b.startBar, s); b.endBar = std::max (b.endBar, s + len);
        b.items.push_back (it);
    }
    if (b.items.empty()) b.startBar = 0;
    return b;
}

std::vector<ClipRef> Session::pasteClips (const ClipBoard& b, double at, Track* toTrack)
{
    std::vector<ClipRef> made;
    if (b.empty()) return made;
    int top = 1 << 30; for (auto& it : b.items) top = std::min (top, it.row);
    const int shift = toTrack && indexOf (toTrack) >= 0 ? indexOf (toTrack) - top : 0;
    at = std::max (0.0, at);
    for (auto& it : b.items)
    {
        auto fits = [&] (Track* t) { return t && t->isAudio() == it.audio; };
        Track* t = nullptr;
        if (toTrack) { const int row = it.row + shift; if (row >= 0 && row < (int) tracks.size()) t = tracks[(size_t) row].get(); }
        if (! fits (t)) t = clipTrack ({ it.track, 0, it.audio });
        if (! fits (t)) continue;
        const int id = nextClipId;
        if (it.audio) { AudioClip c = it.aclip; c.id = 0; c.startBar = at + (c.startBar - b.startBar); editAudioClips (*t, [&] (std::vector<AudioClip>& v) { v.push_back (c); }); }
        else { Clip c = it.clip; c.id = 0; c.startBar = at + (c.startBar - b.startBar); editClips (*t, [&] (std::vector<Clip>& v) { v.push_back (c); }); }
        made.push_back ({ t->uid, id, it.audio });
    }
    return made;
}

std::vector<ClipRef> Session::duplicateClips (const std::vector<ClipRef>& refs)
{
    auto b = copyClips (refs);
    if (b.empty()) return {};
    return pasteClips (b, b.endBar);
}

void Session::insertTime (double at, double bars)
{
    if (bars <= kEps) return;
    at = std::max (0.0, at);
    splitClips (clipsBetween (at, at), at);   // (an empty range: the clips that run across the point)
    for (auto& r : allClips()) { double s, b; if (clipSpan (r, s, b) && s >= at - kEps) moveClips ({ r }, bars); }
    // automation: the value at the point holds through the new bars, everything after moves later
    const auto laneList = lanes;   // (editLane replaces the list while we go through it)
    for (auto l : *laneList)
    {
        bool after = false; for (auto& p : l->pts) if (p.beat >= at * 4.0 - kEps) after = true;
        if (! after) continue;
        const float v = l->valueAt (at * 4.0);
        editLane (l->target, [&] (AutoLane& ln)
        {
            for (auto& p : ln.pts) if (p.beat >= at * 4.0 - kEps) p.beat += bars * 4.0;
            ln.pts.push_back ({ at * 4.0, v, 0.0f }); ln.pts.push_back ({ (at + bars) * 4.0, v, 0.0f });
        });
    }
    editMarkers ([&] (std::vector<Marker>& m) { for (auto& x : m) if (x.bar >= at - kEps) x.bar += bars; });
    if (loopStart.load() >= at - kEps) loopStart.store (loopStart.load() + bars);
    if (loopEnd.load() > at + kEps) loopEnd.store (loopEnd.load() + bars);
}

void Session::deleteTime (double a, double b)
{
    a = std::max (0.0, a);
    if (b <= a + kEps) return;
    const double d = b - a;
    splitClips (clipsBetween (a, a), a);
    splitClips (clipsBetween (b, b), b);
    std::vector<ClipRef> gone, later;
    for (auto& r : allClips()) { double s, len; if (! clipSpan (r, s, len)) continue; if (s >= a - kEps && s < b - kEps) gone.push_back (r); else if (s >= b - kEps) later.push_back (r); }
    deleteClips (gone);
    moveClips (later, -d);
    const auto laneList = lanes;   // (editLane replaces the list while we go through it)
    for (auto l : *laneList)
    {
        bool touched = false; for (auto& p : l->pts) if (p.beat >= a * 4.0 - kEps) touched = true;
        if (! touched) continue;
        const float va = l->valueAt (a * 4.0), vb = l->valueAt (b * 4.0);
        editLane (l->target, [&] (AutoLane& ln)
        {
            ln.pts.erase (std::remove_if (ln.pts.begin(), ln.pts.end(), [&] (const AutoPoint& p) { return p.beat >= a * 4.0 - kEps && p.beat <= b * 4.0 + kEps; }), ln.pts.end());
            for (auto& p : ln.pts) if (p.beat > b * 4.0) p.beat -= d * 4.0;
            // the curve reaches the cut as it did, and goes on from there as it did after the removed bars
            ln.pts.push_back ({ a * 4.0, va, 0.0f });
            if (std::abs (vb - va) > 1.0e-6f) ln.pts.push_back ({ a * 4.0 + 1.0 / 960.0, vb, 0.0f });
        });
    }
    editMarkers ([&] (std::vector<Marker>& m)
    {
        m.erase (std::remove_if (m.begin(), m.end(), [&] (const Marker& x) { return x.bar >= a - kEps && x.bar < b - kEps; }), m.end());
        for (auto& x : m) if (x.bar >= b - kEps) x.bar -= d;
    });
    auto fix = [&] (double x) { return x >= b ? x - d : x > a ? a : x; };
    const double ls = fix (loopStart.load()), le = fix (loopEnd.load());
    loopStart.store (ls); loopEnd.store (le > ls + kEps ? le : ls + 1.0);
}

// ---- the tempo map -------------------------------------------------------------------------------------------
double Session::secondsAt (double beat) const
{
    beat = std::max (0.0, beat);
    auto l = lane ({ 0, 0, GP_TEMPO });
    if (! l || ! l->enabled || l->pts.empty()) return beat * 60.0 / std::max (1.0f, tempo());
    // the lane is the tempo at every beat: a quarter of a beat at a time, the tempo at its middle
    constexpr double step = 0.25;
    double secs = 0, b = 0;
    while (b + step <= beat) { secs += step * 60.0 / std::max (1.0f, tempoFromNorm (l->valueAt (b + step * 0.5))); b += step; }
    if (beat > b) secs += (beat - b) * 60.0 / std::max (1.0f, tempoFromNorm (l->valueAt (0.5 * (b + beat))));
    return secs;
}
double Session::beatAt (double seconds) const
{
    seconds = std::max (0.0, seconds);
    auto l = lane ({ 0, 0, GP_TEMPO });
    if (! l || ! l->enabled || l->pts.empty()) return seconds * std::max (1.0f, tempo()) / 60.0;
    constexpr double step = 0.25;
    double secs = 0, b = 0;
    for (int guard = 0; guard < 4000000; ++guard)
    {
        const double dt = step * 60.0 / std::max (1.0f, tempoFromNorm (l->valueAt (b + step * 0.5)));
        if (secs + dt >= seconds) return b + step * (seconds - secs) / dt;
        secs += dt; b += step;
    }
    return b;
}
} // namespace fal::daw

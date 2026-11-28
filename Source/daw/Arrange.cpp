// faltung sndstudio — the arrangement edited like in any daw: clips found, moved, cut, trimmed, copied, pasted,
// muted; time inserted and removed across every track, lane and marker; the tempo map (seconds <-> beats).
// Message thread only: every edit goes through editClips / editAudioClips / editLane / editMarkers (copy on write).
#include "Session.h"
#include "devices/StudioDevices.h"
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

/** points (beats from a clip's start) after its start moved later by `cut` beats: the value at the cut becomes the first */
static void shiftPoints (std::vector<std::pair<double, float>>& pts, double cut)
{
    if (pts.empty() || std::abs (cut) < 1.0e-12) return;
    if (cut < 0) { for (auto& p : pts) p.first -= cut; return; }
    float v0 = pts.back().second;
    for (size_t i = 0; i < pts.size(); ++i) if (cut <= pts[i].first) { if (i == 0) v0 = pts[0].second; else { const auto& a = pts[i - 1]; const auto& d = pts[i]; v0 = (float) (a.second + (d.second - a.second) * (cut - a.first) / std::max (1.0e-9, d.first - a.first)); } break; }
    std::vector<std::pair<double, float>> out { { 0.0, v0 } };
    for (auto& [pb, pv] : pts) if (pb > cut + 1.0e-9) out.push_back ({ pb - cut, pv });
    pts = out;
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
                // its own lines and markers stay with the sound
                const double cut = (ns - c.startBar) * 4.0, srcCut = off - c.offset;
                shiftPoints (c.gainLine, cut); for (auto& e : c.envs) shiftPoints (e.pts, cut);
                if (c.mode == 3) { std::vector<std::pair<double, double>> w; for (auto& [wb, ws] : c.warp) if (wb - cut > 1.0e-9) w.push_back ({ wb - cut, ws - srcCut }); c.warp = w; }
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

std::vector<ClipRef> Session::takesOf (const ClipRef& r) const
{
    std::vector<ClipRef> v;
    auto* t = clipTrack (r);
    if (! t || ! r.audio) return v;
    const AudioClip* me = nullptr; for (auto& c : *t->aclips) if (c.id == r.id) me = &c;
    if (! me || me->takeGroup == 0) return v;
    std::vector<std::pair<int, int>> order;
    for (auto& c : *t->aclips)
        if (c.takeGroup == me->takeGroup && c.startBar < me->startBar + me->bars - kEps && c.startBar + c.bars > me->startBar + kEps) order.push_back ({ c.take, c.id });
    std::sort (order.begin(), order.end());
    for (auto& [k, id] : order) v.push_back ({ t->uid, id, true });
    return v;
}
void Session::chooseTake (const ClipRef& r)
{
    auto takes = takesOf (r);
    if (takes.empty()) return;
    auto* t = clipTrack (r);
    std::set<int> others; for (auto& x : takes) if (x.id != r.id) others.insert (x.id);
    editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) { if (c.id == r.id) c.muted = false; else if (others.count (c.id)) c.muted = true; } });
}
void Session::setClipName (const ClipRef& r, const juce::String& name)
{
    auto* t = clipTrack (r); if (! t) return;
    if (r.audio) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == r.id) c.name = name; });
    else editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (c.id == r.id) c.name = name; });
}
void Session::setClipColour (const std::vector<ClipRef>& refs, juce::uint32 colour)
{
    for (auto& t : tracks)
    {
        std::set<int> pat, aud;
        for (auto& r : refs) if (r.track == t->uid) (r.audio ? aud : pat).insert (r.id);
        if (! pat.empty()) editClips (*t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (pat.count (c.id)) c.colour = colour; });
        if (! aud.empty()) editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (aud.count (c.id)) c.colour = colour; });
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
    const auto tt = tempoTable;
    if (! tt || tt->secs.empty()) return beat * 60.0 / std::max (1.0f, tempo());
    const double u = beat / TempoTable::step; const size_t i = (size_t) u;
    if (i + 1 >= tt->secs.size()) return tt->secs.back() + (beat - (double) (tt->secs.size() - 1) * TempoTable::step) * 60.0 / tt->tailBpm;
    return tt->secs[i] + (tt->secs[i + 1] - tt->secs[i]) * (u - (double) i);
}
double Session::beatAt (double seconds) const
{
    seconds = std::max (0.0, seconds);
    const auto tt = tempoTable;
    if (! tt || tt->secs.empty()) return seconds * std::max (1.0f, tempo()) / 60.0;
    const auto& v = tt->secs;
    if (seconds >= v.back()) return (double) (v.size() - 1) * TempoTable::step + (seconds - v.back()) * tt->tailBpm / 60.0;
    const auto it = std::upper_bound (v.begin(), v.end(), seconds);
    const size_t i = (size_t) std::max<std::ptrdiff_t> (0, (it - v.begin()) - 1);
    const double d = v[i + 1] - v[i];
    return ((double) i + (d > 0 ? (seconds - v[i]) / d : 0.0)) * TempoTable::step;
}
double Session::TempoKey::secondsAt (double beat) const
{
    beat = std::max (0.0, beat);
    if (! keep || keep->secs.empty()) return beat * 60.0 / std::max (1.0, bpm);
    const double u = beat / TempoTable::step; const size_t i = (size_t) u;
    if (i + 1 >= keep->secs.size()) return keep->secs.back() + (beat - (double) (keep->secs.size() - 1) * TempoTable::step) * 60.0 / keep->tailBpm;
    return keep->secs[i] + (keep->secs[i + 1] - keep->secs[i]) * (u - (double) i);
}
juce::String Session::formatRuler (int f, double beat) const
{
    const double sec = secondsAt (beat);
    switch (f)
    {
        case timeline::RULER_TIME: return timeline::formatTime (sec);
        case timeline::RULER_SAMPLES: return timeline::formatSamples (sec, sr);
        case timeline::RULER_TIMECODE: return timeline::formatTimecode (sec, timecodeFps.load());
        case timeline::RULER_NONE: return {};
        default: return timeline::formatPosition (beat, *meter);
    }
}
void Session::retime()
{
    TempoKey now; now.keep = tempoTable; now.table = tempoTable.get(); now.bpm = tempo();
    if (lastTempo.bpm <= 0) { lastTempo = now; return; }
    if (now.table == lastTempo.table && (now.table != nullptr || std::abs (now.bpm - lastTempo.bpm) < 1.0e-6)) return;
    const TempoKey old = lastTempo; lastTempo = now;
    auto move = [&] (double beat) { return beatAt (old.secondsAt (beat)); };
    bool any = false;
    for (auto& tp : tracks)
    {
        auto& t = *tp;
        bool has = false;
        for (auto& c : *t.clips) has |= clipIsTime (t, c.timebase);
        for (auto& c : *t.aclips) has |= clipIsTime (t, c.timebase);
        if (! has) continue;
        any = true;
        editClips (t, [&] (std::vector<Clip>& v) { for (auto& c : v) if (clipIsTime (t, c.timebase)) { const double a = move (c.startBar * 4.0), b = move ((c.startBar + c.bars) * 4.0); c.startBar = a / 4.0; c.bars = std::max (1.0e-6, (b - a) / 4.0); } });
        editAudioClips (t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (clipIsTime (t, c.timebase)) { const double a = move (c.startBar * 4.0), b = move ((c.startBar + c.bars) * 4.0); c.startBar = a / 4.0; c.bars = std::max (1.0e-6, (b - a) / 4.0); } });
    }
    // a project in time: its markers, loop and automation (but not the tempo lane) keep their seconds too
    if (timebase.load() == 1)
    {
        any = true;
        editMarkers ([&] (std::vector<Marker>& m) { for (auto& k : m) k.bar = move (k.bar * 4.0) / 4.0; });
        const double ls = move (loopStart.load() * 4.0) / 4.0, le = move (loopEnd.load() * 4.0) / 4.0; loopStart.store (ls); loopEnd.store (le);
        std::vector<Target> ts; for (auto& l : *lanes) if (! (l->target == Target { 0, 0, GP_TEMPO })) ts.push_back (l->target);
        for (auto& tg : ts) editLane (tg, [&] (AutoLane& l) { for (auto& pt : l.pts) pt.beat = move (pt.beat); });
    }
    if (any) dirty = true;
}
void Session::tempoChanged()
{
    auto l = lane ({ 0, 0, GP_TEMPO });
    std::shared_ptr<const TempoTable> nt;
    if (l && l->enabled && ! l->pts.empty())
    {
        auto t = std::make_shared<TempoTable>();
        // a quarter beat at a time, the tempo at its middle; far enough past the last point (and the project) that the tail is flat
        const double end = std::max (l->pts.back().beat, projectBars() * 4.0) + 64.0;
        const size_t n = (size_t) (end / TempoTable::step) + 2;
        t->secs.resize (n); double secs = 0;
        for (size_t i = 0; i < n; ++i) { t->secs[i] = secs; secs += TempoTable::step * 60.0 / std::max (1.0f, tempoFromNorm (l->valueAt (((double) i + 0.5) * TempoTable::step))); }
        t->tailBpm = std::max (1.0f, tempoFromNorm (l->valueAt (end + 1.0e6)));
        nt = t;
    }
    { const juce::SpinLock::ScopedLockType sl (lock); tempoTable = nt; }
    tempoVersion.fetch_add (1);
    if (! inRetime) { inRetime = true; retime(); inRetime = false; }
}
double Session::warpSrc (const AudioClip& c, double beat, double beats, double srcAvail)
{
    // the markers with the implied one at the start; past the last, the last segment's rate (or the clip's own rate without markers)
    double pb = 0, ps = 0, rate = beats > 0 ? srcAvail / beats : 1.0;
    for (auto& [b, sv] : c.warp)
    {
        if (b <= pb + 1.0e-9) continue;
        const double seg = (sv - ps) / (b - pb);
        if (beat <= b) return ps + (beat - pb) * seg;
        pb = b; ps = sv;
    }
    return ps + (beat - pb) * rate;   // (after the last marker the audio goes on at its own tempo)
}
double Session::warpBeat (const AudioClip& c, double src, double beats, double srcAvail)
{
    double pb = 0, ps = 0, rate = beats > 0 ? srcAvail / beats : 1.0;
    for (auto& [b, sv] : c.warp)
    {
        if (b <= pb + 1.0e-9) continue;
        const double seg = (sv - ps) / (b - pb);
        if (src <= sv && std::abs (seg) > 1.0e-12) return pb + (src - ps) / seg;
        pb = b; ps = sv;
    }
    return std::abs (rate) > 1.0e-12 ? pb + (src - ps) / rate : pb;
}
Session::ClipTime Session::clipTime (const AudioClip& c, double fileRate) const
{
    ClipTime ct; ct.s = this; ct.c = &c; ct.mode = c.mode; ct.sr = sr;
    ct.ratio = std::exp2 (c.pitch / 12.0);
    ct.srcAvail = c.sample ? std::max (0.0, c.sample->seconds() - c.offset) : 0.0;
    ct.beats = c.bars * 4.0; ct.beat0 = c.startBar * 4.0; ct.sec0 = secondsAt (ct.beat0);
    const double clipSecs = secondsAt (ct.beat0 + ct.beats) - ct.sec0;
    if (c.mode == 1) ct.stretch = std::max (0.05, c.stretch);
    else if (c.mode == 2) ct.stretch = ct.srcAvail > 1.0e-6 ? clipSecs / ct.srcAvail : 1.0;
    (void) fileRate;
    return ct;
}
double Session::ClipTime::srcAt (double out) const
{
    if (mode == 0) return out / sr * ratio;
    if (mode == 1) return out / sr / stretch;
    const double b = beatOf (out);
    if (mode == 2) return beats > 0 ? b / beats * srcAvail : 0.0;
    return warpSrc (*c, b, beats, srcAvail);
}
double Session::ClipTime::outAt (double src) const
{
    if (mode == 0) return src / ratio * sr;
    if (mode == 1) return src * stretch * sr;
    const double b = mode == 2 ? (srcAvail > 0 ? src / srcAvail * beats : 0.0) : warpBeat (*c, src, beats, srcAvail);
    return outOf (b);
}
int Session::addNoteClip (Track& t, double startBar, double bars, std::vector<Note> notes, const juce::String& name)
{
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    const int id = nextClipId++;
    Clip c; c.pattern = t.pattern.load(); c.startBar = std::max (0.0, startBar); c.bars = std::max (kMinPatternBars, bars); c.id = id; c.name = name;
    c.notes = std::make_shared<const std::vector<Note>> (std::move (notes));
    editClips (t, [&] (std::vector<Clip>& v) { v.push_back (c); });
    return id;
}
void Session::setClipGainLine (ClipRef r, std::vector<std::pair<double, float>> line)
{
    auto* t = clipTrack (r); if (! t || ! r.audio) return;
    std::sort (line.begin(), line.end());
    editAudioClips (*t, [&] (std::vector<AudioClip>& v) { for (auto& c : v) if (c.id == r.id) c.gainLine = line; });
}
void Session::removeGroup (int id)
{
    for (auto& t : tracks) if (t->group.load() == id) t->group.store (0);
    editGroups ([id] (std::vector<Group>& v) { v.erase (std::remove_if (v.begin(), v.end(), [id] (const Group& g) { return g.id == id; }), v.end()); });
}
std::vector<ClipRef> Session::sliceClip (ClipRef r, int how, double step, int n)
{
    auto* t = clipTrack (r); if (! t || ! r.audio) return {};
    const AudioClip* c = nullptr; for (auto& x : *t->aclips) if (x.id == r.id) c = &x;
    if (! c || ! c->sample) return { r };
    const double a = c->startBar, b = c->startBar + c->bars;
    std::vector<double> at;
    if (how == 0)
    {
        const auto ct = clipTime (*c, c->sample->rate);
        for (int on : c->sample->onsets)
        {
            const double src = on / c->sample->rate - c->offset; if (src <= 0.005) continue;
            const double beat = ct.beat0 + ct.beatOf (ct.outAt (src)); at.push_back (beat / 4.0);
        }
    }
    else if (how == 1 && step > 1.0e-6) for (double p = std::ceil (a / step - 1.0e-9) * step; p < b; p += step) at.push_back (p);
    else if (how == 2 && n > 1) for (int k = 1; k < n; ++k) at.push_back (a + (b - a) * k / n);
    else if (how == 3) for (auto& [wb, ws] : c->warp) at.push_back (a + wb / 4.0);
    std::sort (at.begin(), at.end());
    std::vector<ClipRef> pieces { r }; ClipRef cur = r;
    for (double p : at)
    {
        if (p <= a + 1.0e-6 || p >= b - 1.0e-6) continue;
        double s0 = 0, len = 0; if (! clipSpan (cur, s0, len) || p <= s0 + 1.0e-6 || p >= s0 + len - 1.0e-6) continue;
        auto right = splitClips ({ cur }, p);
        if (right.empty()) continue;
        cur = right.front(); pieces.push_back (cur);
    }
    return pieces;
}
Track* Session::clipToSampler (ClipRef r, juce::String& error)
{
    auto* t = clipTrack (r); if (! t || ! r.audio) { error = "not an audio clip"; return nullptr; }
    juce::String path; double bars = 1; for (auto& x : *t->aclips) if (x.id == r.id) { path = x.path; bars = x.bars; }
    auto dev = createDevice ("st:sampler", &error); if (! dev) return nullptr;
    auto* h = dynamic_cast<SampleHolder*> (dev->processor());
    if (! h || ! h->setSampleFile (juce::File (path))) { error = "cannot load " + path; return nullptr; }
    const int mode = dev->paramIndexForId ("mode"), sm = dev->paramIndexForId ("slicemode");
    if (mode >= 0) dev->setBase (mode, 1.0f); if (sm >= 0) dev->setBase (sm, 1.0f);
    auto* nt = addTrack (juce::File (path).getFileNameWithoutExtension() + " slices", indexOf (t) + 1, TrackKind::Instrument);
    if (! nt) { error = "no room for a track"; return nullptr; }
    setInstrument (*nt, std::move (dev));
    const int steps = juce::jlimit (1, 64, (int) std::lround (bars * 16.0));
    editPattern (*nt, 0, [steps] (Pattern& p) { p = Pattern(); p.length = std::min (16, steps); for (int i = 0; i < p.length; ++i) { p.s[(size_t) i].on = true; p.s[(size_t) i].slice = i; p.s[(size_t) i].len = 1.0f; } });
    return nt;
}
} // namespace fal::daw

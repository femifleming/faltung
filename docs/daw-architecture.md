# sndstudio — how it is built

Source/daw. A standalone application only (no plug-in formats since 2026-11-19). One engine (`Session`), driven by
the app's own audio callback (`StudioApp.cpp`, so any device's 128 outputs and inputs are reachable) through
`StudioProcessor` (the inputs first in one buffer, then the outputs; project files, autosave, crash recovery and the
undo history, `Undo.h`). Plug-in scans run in child processes (`Session::scanPlugins`: the app itself with
`--scan-plugin`), so a plug-in that crashes is blacklisted instead of taking the app down.

## threads
- The message thread edits the structure (tracks, devices, routes) under `Session::lock` (a spin lock held only
  for the pointer swap; plug-in instantiation happens outside it). Patterns, automation lanes, scenes, clips and
  modulation definitions are immutable snapshots (`shared_ptr<const …>`) replaced copy-on-write.
- The audio thread takes the lock with `tryEnter` for the whole block (silence for that block if the UI is in the
  middle of a structural change) and reads everything directly. No allocation on the audio thread.

## devices
`Device` wraps either one of the suite's own processors (`fal::Processor`, bd1 … ms0, hosted in-process, so the
sounds and their preset lists are exactly the plug-ins'), a studio-only device (Source/daw/devices), or a hosted
VST3 / AU instance. Every real parameter gets a *base* mirror parameter (0..1) that the UI binds to; the engine
writes `base (+ macro) + modulation (+ lock / slide)` into the real parameter once per block, only when it
changed. The device's own window edits flow back into the base (and into automation recording).

## engine order, per block (split at bar lines, and in the arrangement wherever a clip starts or ends, so launches, scenes and clips start and stop on their sample)
1. automation lanes → base values (skipped for parameters the user is holding)
2. globals (macros, tempo, swing, master)
3. per track: resolve the pattern (session / song / arrangement / chain) → modulation sources → sequencer events
   (conditions, probability, ratchets, micro, swing, speed, locks) → instrument → inserts → latency alignment →
   strip (level, pan, mute, solo) → sends (pre / post) → routes → master or its own output pair
4. returns, then the master (its chain, e.g. ms0 bypassed when another program masters)
5. the metronome (and, from a stop, the count-in: the transport waits, split to the sample where it ends)

## latency
Every track is delayed to the longest track chain; returns to the longest return chain; direct outputs wait for
the returns and the master as well, so all 128 channels line up. The header shows the total.

## channels (spatial)
A track has a width (1–64, default 2). Clips and instruments feed channels 1–2; devices negotiate their widths
along the chain (a 2 → 16 encoder, a 16 → 2 binaural decoder); routes copy N channels from a track into another
track's input (with offsets); the master has its own width (2–64) and that many meters; "to master" per track.

## files
`.fstudio` is xml (`Project.cpp`): tracks, devices (type, state blob, base values, macro maps), patterns (sparse),
clips, chains, modulation, scenes, song, markers, lanes, midi maps. Autosave every two minutes while dirty.

## the timeline (Timeline.h, Arrange.cpp)
`Timeline.h` is the arithmetic, pure and tested on its own: the grid divisions (adaptive, straight, triplet, dotted,
quintuplet, septuplet), snapping and the magnet (clip edges, markers, the loop and the playhead within 7 px win over
the grid), positions written and read as bar.beat.sixteenth.tick (960 ticks a beat, counted from 1) or as time.
`Arrange.cpp` holds the arrangement's edits on the Session, by `ClipRef` (track uid, clip id, audio or pattern):
move, split, trim from the left (audio: the offset into the file follows; patterns: `Clip::offset`, the beats into
the pattern, wrapped round its loop), set the length, duplicate, copy / paste (a `ClipBoard`), mute, delete, insert
and delete time (clips cut at the edges; lanes, markers and the loop follow), and the tempo map
(`secondsAt` / `beatAt`, following a tempo lane). Pattern clips have ids like audio clips (saved, so the selection
survives undo). In the engine a pattern clip's anchor is `start - offset`; a track's steps fire only inside its clip
(`Track::clipFrom / clipTo`), and a span cut at a clip's start catches the step exactly there.

## the window
The editor lays the pages out at the window's size divided by the ui scale (0.5–2, kept in the app's settings), never
below 1200 × 720: a window too small for the scale draws at the largest scale that fits. The header moves master,
punch and capture to its second row below 1430 wide.

# sndstudio — how it is built

Source/daw. One engine (`Session`) shared by the standalone app (`StudioApp.cpp`, its own audio callback so any
device's 128 channels are reachable) and the plug-in (`StudioProcessor`, 64 stereo output buses, host sync, the
project as plug-in state).

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

## engine order, per block (split at bar lines so launches, scenes and clips start exactly on the beat)
1. automation lanes → base values (skipped for parameters the user is holding)
2. globals (macros, tempo, swing, master)
3. per track: resolve the pattern (session / song / arrangement / chain) → modulation sources → sequencer events
   (conditions, probability, ratchets, micro, swing, speed, locks) → instrument → inserts → latency alignment →
   strip (level, pan, mute, solo) → sends (pre / post) → routes → master or its own output pair
4. returns, then the master (its chain, e.g. ms0 bypassed for Ableton)

## latency
Every track is delayed to the longest track chain; returns to the longest return chain; direct outputs wait for
the returns and the master as well, so all 128 channels line up. The plug-in reports the total.

## channels (spatial)
A track has a width (1–64, default 2). Clips and instruments feed channels 1–2; devices negotiate their widths
along the chain (a 2 → 16 encoder, a 16 → 2 binaural decoder); routes copy N channels from a track into another
track's input (with offsets); the master has its own width (2–64) and that many meters; "to master" per track.

## files
`.fstudio` is xml (`Project.cpp`): tracks, devices (type, state blob, base values, macro maps), patterns (sparse),
clips, chains, modulation, scenes, song, markers, lanes, midi maps. Autosave every two minutes while dirty.

#!/usr/bin/env python3
"""Builds "sndstudio Manual.pdf" (the daw's own manual) in the faltung manual's look. Pictures from img/sndstudio-*.png
(sndstudioTest --screenshots docs/manual/img). Also exports `chapter(shot)` for the two suite manuals."""
import html, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
MAIN = open(os.path.join(HERE, 'make.py')).read()
FONTS = os.path.join(ROOT, 'Resources', 'fonts')
VERSION = '1.2.0'
e = html.escape
g = globals()
exec(MAIN[MAIN.index('def ptable'):MAIN.index('def shot')], g)
exec(MAIN[MAIN.index('def chevrons'):MAIN.index('# ------------------------------------------------------------------------------------------------\n#  document')], g)
exec(MAIN[MAIN.index("CSS = f'''"):MAIN.index("ms_page = ")], g)

def shot(name, cls='shot'):
    return f'<img class="{cls}" src="img/{name}.png" alt="{name}">'

def chapter(sh=shot):
    """the chapter that goes into the suite manuals: an overview, pointing at this manual"""
    return f'''<section class="page" id="sndstudio"><h2>sndstudio</h2>
<p class="lead">A whole studio made from the suite — tracks, a deep step sequencer, scenes and a song, a timeline with audio clips
and automation, modulators on any control, sends and returns, a mixer with up to 128 output channels so every track can be
recorded in Ableton on its own channel, plug-in hosting, recording, resampling, stems export. As a standalone app and as a
VST3 / AU / VST2 plug-in.</p>
{sh('sndstudio-0')}
<p>Its own manual, <b>sndstudio Manual.pdf</b>, is in the download next to this one. In short: <b>session</b> is the scene grid
(click a cell to launch a pattern, keys 1–8 launch scenes), <b>arrange</b> the timeline (pattern clips, audio clips dropped from the
Finder, markers, the loop, automation lanes), <b>seq</b> the step grid of the selected track with every step's velocity, pitch,
chance, ratchet, micro-timing, length, condition, slide and four parameter locks, the pattern tools (euclid, rotate, mutate,
humanise, markov …) and the track's devices, <b>mix</b> the strips with meters, sends, output channels, widths and routes,
<b>mod</b> the modulators, macros and midi maps, <b>browse</b> the devices, plug-ins (scan), files and templates.</p>
<p>Right-click any control for <i>map a modulator</i>, <i>add an automation lane</i>, <i>midi learn</i> and <i>macro</i>.
In <b>produce</b> mode everything is mixed to the stereo master (with ms0 on it if you want); switch to <b>multi-out</b> and
<i>assign all outputs</i> to put every track and return on its own channel pair — in Live, add the plug-in, enable its output
buses on audio tracks and record; from the standalone, use a 128-channel virtual device (BlackHole 128ch) as the output.</p></section>'''

doc = []
add = doc.append
add(f'''<section class="cover">{chevrons('#8ec3e6')}
<svg class="cube" viewBox="0 0 104 112"><polygon points="0,30 52,0 104,30 52,60" fill="#8ec3e6"/><polygon points="0,30 52,60 52,112 0,82" fill="#6e80c8"/>
<polygon points="52,60 104,30 104,82 52,112" fill="#2b3a40"/><text x="52" y="35" text-anchor="middle" font-size="11" font-weight="600" fill="#1d1c15" font-family="Inter">sndstudio</text></svg>
<div class="ct"><h1>sndstudio</h1><p class="sub">the faltung studio: tracks, scenes, song, sends, modulators, 128 outputs</p>
<p class="ver">manual · version {VERSION} · november 2026</p></div>
<div class="tl">''' + ''.join(f'<div><span class="chip" style="background:{c}"></span><span>{i + 1:02d}</span><b>{n}</b><span>{d}</span></div>' for i, (c, n, d) in enumerate([
    ('#8ec3e6', 'session', 'the scene grid'), ('#6e80c8', 'arrange', 'timeline, clips, automation'), ('#5e8f6a', 'seq', 'steps, locks, devices'),
    ('#ee8e6e', 'mix', 'strips, outputs, routes'), ('#f6c590', 'mod', 'modulators, macros, midi'), ('#cfe6f0', 'browse', 'devices, plug-ins, files')])) + '</div></section>')

add('''<section class="page"><h2>contents</h2><ol class="toc">
<li><a href="#intro">what it is</a></li><li><a href="#start">first ten minutes</a></li><li><a href="#session">session</a></li><li><a href="#arrange">arrange: timeline, clips, automation, song</a></li>
<li><a href="#seq">seq: steps, locks, tools, devices</a></li><li><a href="#mix">mix: strips, outputs, widths, routes</a></li><li><a href="#mod">mod: modulators, macros, midi</a></li>
<li><a href="#browse">browse: devices, plug-ins, files, templates</a></li><li><a href="#devices">the studio devices</a></li><li><a href="#modes">produce and multi-out, into ableton</a></li>
<li><a href="#spatial">spatial audio and ambisonics</a></li><li><a href="#audio">audio settings and recording</a></li><li><a href="#export">export and resampling</a></li><li><a href="#keys">keys</a></li><li><a href="#tech">technical notes</a></li></ol>
<h2 id="intro">what it is</h2>
<p class="lead">sndstudio is a studio for microhouse, rominimal, dub techno and the music around them, built out of the faltung suite:
its drum engines, bass synth, effects and mastering bus are the devices, and around them a step sequencer that goes further than kt1,
scenes and a song, a timeline, automation on everything, modulators on everything, sends and returns, a mixer whose channels
can leave through 128 outputs, hosted VST3 / AU plug-ins, audio tracks, recording, resampling and stems.</p>
<p>It comes twice: <b>faltung sndstudio</b> (the microhouse devices and templates) and <b>faltung deep sndstudio</b> (the deep house
twin, the mirror-tree look). Each is a standalone app and a plug-in. A project (<code>.fstudio</code>) opens in both forms.</p>
<p>Two ways of working, switched in the header or the mixer: <b>produce</b> — everything is mixed inside, to a stereo master with the
ms0 chain on it, and exported as a mastered wav and as stems; <b>multi-out</b> — every track, return and the master leave on their own
output channels (64 stereo buses as a plug-in; any multichannel device in the standalone), and mixing and mastering happen in Ableton.</p>
<h3>the pages</h3>
<table class="params"><tr><th>page</th><th>what is there</th></tr>
<tr><td class="n">session</td><td>tracks across, scenes down; every cell is a pattern of that track. Click a cell to launch it (quantised to the bar), the triangle launches the whole scene, keys 1–8 too.</td></tr>
<tr><td class="n">arrange</td><td>the timeline: pattern clips and audio clips per track, markers, the loop region, the playhead; under it the automation lanes of the selected track; to the right the song (scenes in order with bar counts).</td></tr>
<tr><td class="n">seq</td><td>the selected track: its pattern (1–64), length, speed, swing, shift, chance, root note, clock, chain; the 16-step grid (pages for longer patterns); the selected step's velocity, pitch, chance, ratchet, micro, length, condition, slide, slice and locks; the tools; the track's devices with every parameter as a slider.</td></tr>
<tr><td class="n">mix</td><td>strips for the tracks, returns and master: meters (one bar per channel), level, pan, eight sends, mute / solo / rec, output channels, width, to master, routes, input, key. The produce / multi-out switch, assign all outputs, the master width and the master chain bypass.</td></tr>
<tr><td class="n">mod</td><td>the modulators of the selected track (click one to edit it), the eight macros (named, seen by the host), the midi maps and midi learn.</td></tr>
<tr><td class="n">browse</td><td>every device (faltung, studio, plug-ins) to add to the selected track, a return or the master; scan plug-ins; a file browser whose files drag onto the timeline or onto a sampler; new from template.</td></tr></table></section>''')

add('''<section class="page"><h2 id="start">first ten minutes</h2>
<ol><li>Open the app. It starts on the <b>microhouse starter</b> (deep: <b>deep house starter</b>): eight tracks — kick, rim, hat, open hat, clap, perc, click, bass —
two returns (dub delay, room), a filter / glue / ms0 chain on the master, eight scenes, a song, two automation lanes, a few modulators and macros.</li>
<li>Press <b>space</b>. Scene "full" is playing (the triangle at the left of a scene launches it, keys 1–8 too). Click cells to change patterns per track.</li>
<li>Open <b>seq</b>. The rim is selected. Click steps, drag up and down for velocity, shift-drag to move a step early or late, alt-click for chance, right-click for ratchets. Press <b>+ lock</b> to lock a parameter on the selected step, or pick a slider below: it shows a white bracket where the lock sits.</li>
<li>Right-click the dub return's <i>feedback</i> slider (mix page → select the return → seq page) and choose <b>map a modulator</b>: a slow random with slew. The orange band under the slider is where it moves.</li>
<li>Switch the header to <b>song</b>: the eight scenes play in order, 8 bars each. Open <b>arrange</b> to see them as clips; drag an audio file from the Finder onto a lane.</li>
<li>Press <b>write</b>, play, and move anything: automation lanes appear under the track in arrange.</li>
<li><b>save</b> (header) writes a <code>.fstudio</code> next to its audio folder. <b>file → export master</b> renders a 24-bit wav through the master chain with a true-peak limiter; <b>export stems</b> one wav per track.</li>
<li>For Ableton: <b>mix → multi-out → assign all outputs</b>. As a plug-in in Live, enable the buses on audio tracks (the plug-in offers "out 3-4" … "out 127-128"); from the standalone, choose a 128-channel device (BlackHole 128ch) in <b>audio</b>.</li></ol>
<h2 id="session">session</h2>
''' + shot('sndstudio-0') + '''
<p>Tracks are columns, scenes rows. A cell shows the pattern that scene launches on that track (1–64), a square for <i>stop</i>, nothing for <i>keep playing</i>.
Click a cell to launch that pattern on that track at the next bar (<i>launch</i> in seq sets the quantisation: now, 1, 2 or 4 bars). Shift-click puts the
track's current pattern in the cell; alt-click cycles stop / keep; right-click chooses. The triangle (or keys 1–8) launches the scene. Right-click a scene name
to <i>capture</i> what is playing into it; double-click to rename. The white tick under a track header is its running step.</p>
<p>Track headers: click to select, right-click to rename, colour, move, duplicate or remove. <b>+ track</b> adds an instrument track (a kick, change it in seq),
<b>+ audio</b> an audio track, <b>+ return</b> a return (up to eight).</p></section>''')

add('''<section class="page"><h2 id="arrange">arrange</h2>
''' + shot('sndstudio-1') + '''
<p>The ruler counts bars; the loop region is the strip under it (drag on the ruler to set it, <b>l</b> or the header to switch it on); shift-click the ruler for a marker.
Each lane holds pattern clips (p1, p2 …) and audio clips (with their waveform). Drag a clip to move it, its right edge to change its length, alt-drag to copy,
double-click an empty spot to add the track's current pattern for four bars, right-click a clip for its pattern, split, reverse, remove. <b>snap</b> keeps edits on
the bar (or the beat when zoomed in); off, clips go where you drop them. <b>+ / -</b> and cmd-wheel zoom.</p>
<p><b>Audio clips.</b> Drop wav / aif / flac / mp3 files from the Finder or from the browser onto a lane (a new audio track if the lane is an instrument). Double-click a clip
(or right-click → edit) for gain, fades, pitch, mode, stretch, start in file, reverse, loop. Modes: <i>repitch</i> (speed follows pitch, like a varispeed deck),
<i>stretch</i> (length free, pitch kept, transient-preserving), <i>warp to tempo</i> (the clip fits its bars at the project tempo). Playback is sample accurate with
cubic interpolation; the stretcher is a wsola that pins every transient to its place in time.</p>
<p><b>Automation.</b> The lanes under the tracks belong to the selected track (and the globals: macros, tempo, swing, master). Add one with <b>+ lane</b> or from any
control's right-click. Click in a lane to add a point, drag it, alt-drag for the curve, double-click or right-click to delete; click the lane's name to switch it off,
right-click it to remove it. With <b>write</b> on, moving a control while playing records points. Automation is the base value; modulators ride on top of it.</p>
<p><b>Song.</b> The list on the right: scenes in order with bar counts. <b>+ song entry</b>, click one to choose its scene, drag to change its bars, right-click to remove.
The header's mode cycles session → song → arrange: in song mode the scenes play in order from bar 1; in arrange the clips on the timeline play.</p></section>''')

add('''<section class="page"><h2 id="seq">seq</h2>
''' + shot('sndstudio-2') + '''
<h3>the track</h3>
<table class="params"><tr><th>control</th><th>what it does</th></tr>
<tr><td class="n">pattern</td><td>which of the track's 64 patterns is playing (launched at the next quantise point)</td></tr>
<tr><td class="n">length</td><td>1–64 steps; longer than 16 shows pages above the grid. Different lengths on different tracks = polymeter.</td></tr>
<tr><td class="n">speed</td><td>x1/4 … x4: the step length relative to a 16th</td></tr>
<tr><td class="n">swing</td><td>added to the global swing for this track (±25%)</td></tr>
<tr><td class="n">shift</td><td>the whole track earlier or later, in ms</td></tr>
<tr><td class="n">probability</td><td>multiplies every step's chance</td></tr>
<tr><td class="n">root note</td><td>the note a step with pitch 0 plays (drums track pitch from C3 when their <i>key tracking</i> is on; the bass from its root). The open-hat track in the template uses A#4, which plays hh4's open sound.</td></tr>
<tr><td class="n">clock</td><td><i>grid</i>: steps follow the tempo. <i>free</i>: every step lasts <i>step ms</i> whatever the tempo (off-grid sequences, Second Woman clocks).</td></tr>
<tr><td class="n">chain</td><td>on: the patterns in the chain play in order (right-click the switch to type the chain: "1 1 2 3")</td></tr>
<tr><td class="n">launch</td><td>launch quantisation of patterns and scenes</td></tr></table>
<h3>the grid</h3><p>Click a step to switch it on and select it, drag up / down for velocity, drag across to paint, shift-drag for micro-timing (± the global <i>micro</i>
range, 12 ms by default), alt-click for chance (100 → 75 → 50 → 25%), right-click for ratchets. A step shows its pitch, ratchet, condition, slide, slice and how many locks it carries.</p>
<h3>the step</h3>
<table class="params"><tr><th>control</th><th>what it does</th></tr>
<tr><td class="n">velocity · pitch · chance</td><td>as named; pitch in semitones from the root note</td></tr>
<tr><td class="n">ratchet</td><td>1–8 hits inside the step, each a little softer</td></tr>
<tr><td class="n">micro · length</td><td>timing offset; note length in steps (synths hold the note; drums ignore it)</td></tr>
<tr><td class="n">condition</td><td>always · 1:2 … 4:4 · 1:8 · 8:8 (which pass of the pattern) · fill / not fill (the <b>fill</b> button or <b>f</b>) · first / not first (the first pass after a launch) · prev / not prev (whether the previous step played)</td></tr>
<tr><td class="n">slide</td><td>the step's locked values glide to the next step that locks the same parameter</td></tr>
<tr><td class="n">slice</td><td>for sampler tracks: which slice this step plays (otherwise the note)</td></tr>
<tr><td class="n">lock 1–4</td><td>parameter locks: <b>+ lock</b> picks any parameter of the track's strip or devices and locks it at its current value for this step; drag the value. The lock holds until the next step that plays without it. A locked parameter shows a white bracket on its slider.</td></tr></table>
<h3>tools</h3><p><b>euclid</b> (hits over the length, rotated) · <b>rotate</b> · <b>reverse</b> · <b>mutate</b> (flips a few steps) · <b>humanise</b> (micro and velocity) · <b>random</b> ·
<b>markov</b> (a two-state walk) · <b>rewrite</b> (a rule: two hits in a row ratchet, a hit before a rest echoes) · <b>double</b> · <b>clear</b> · <b>copy / paste</b> (between tracks and patterns).</p>
<h3>devices</h3><p>The track's instrument and inserts, every parameter as a slider in the house style. <b>+ device</b> adds a faltung engine, a studio device or a plug-in
(instruments replace the instrument, effects go on the chain). Each device has <b>bypass</b>, <b>window</b> (its own editor — the suite's plug-ins show their own looks,
hosted plug-ins theirs), <b>presets</b> (the plug-in's factory presets) and <b>remove</b>; the sampler has <b>load sample</b>. Right-click any slider: <b>map a modulator</b>,
<b>automation lane</b>, <b>midi learn</b>, <b>macro</b>, <b>reset</b>. The orange band under a slider is its modulation range; the dot at the left means it has a lane.</p></section>''')

add('''<section class="page"><h2 id="mix">mix</h2>
''' + shot('sndstudio-3') + '''
<p>A strip per track, return and master. The meter has one bar per channel of the track's width. <b>level</b> (0.8 = 0 db, up to +12), <b>pan</b> (the first pair),
<b>sends 1–8</b> (one per return; right-click the output button to make a send pre-fader), <b>mute · solo · rec</b> (arm, audio tracks), <b>to master</b> (off: the
track only feeds its routes and its own output channels), <b>out</b> (its own device channels; choosing one switches the mixer to multi-out), <b>width</b>
("2→2/2": what the first device reads → what the chain produces / the track's width; choose 1 … 64), <b>route</b> (copy this track, post fader, into another
track's input over N channels with channel offsets; the mixer refuses a cycle), <b>in</b> (audio tracks: the device input channels, mono or stereo; <i>resample</i>
the master or any track; monitoring off / in / auto), <b>key</b> (which track the sidechain devices on this track listen to).</p>
<p>The header row: <b>produce / multi-out</b>, <b>assign all outputs</b> (master on 1 … w, then every track and return consecutively, to-master off),
<b>master chain bypass</b>, <b>master width</b> (2 … 64, as many meters), the master level. Routes are listed along the bottom.</p>
<h2 id="mod">mod</h2>
''' + shot('sndstudio-4') + '''
<p><b>Modulators</b> belong to a track and target one of its parameters (or another modulator's rate). Make one from any control's right-click or <b>+ modulator</b>;
click one in the list to edit it, <b>remove</b> is in the popup. Sources: <b>lfo</b> (sine, triangle, ramps, square, wow, yoy, s&amp;h, smooth random, drift, mpc shuffle,
pulse with width; phase, skew, retrigger on each hit), <b>random gate</b> (on / off per period with a chance), <b>random</b> (a new value per period — with slew, a slewed
random), <b>envelope</b> (attack / decay on each hit), <b>follower</b> (the track's own level), <b>steps</b> (16 values on a division), <b>macro</b>, <b>velocity</b>, <b>per hit</b>
(a random value on each hit), <b>midi cc</b>, <b>chaos</b> (a logistic map). Rates are <b>sync</b> (divisions from 1/64 to 16 bars) or <b>free</b> (0.01–40 hz) — flipping the
switch keeps the current rate. <b>depth</b> ±100%, <b>polarity</b> bipolar (around the base) / up / down, <b>slew</b> smooths every source. Several modulators on one parameter add up.</p>
<p><b>Macros</b>: eight sliders the host sees as parameters; name them; map a parameter to one from its right-click (the macro sets it between the value it had and the top).
<b>Midi</b>: right-click a control → <i>midi learn</i>, move a controller; the map is listed here (right-click to remove). Controllers also reach modulators of source <i>midi cc</i>.</p>
<h2 id="browse">browse</h2>
''' + shot('sndstudio-5') + '''
<p>Devices by family: the faltung (or faltung deep) instruments and effects, the studio devices, and every plug-in the scan found. Click one to add it to the selected
track (instruments replace the instrument), right-click for a return, the master or a new track. <b>scan plug-ins</b> looks in the usual VST3 / AU folders (a plug-in that
crashes the scan is skipped next time). Files: choose a folder, drag files onto the arrangement or onto an instrument track with a sampler; double-click a file to put it on
the selected audio track at the playhead (or into the selected track's sampler). <b>new from template</b>: empty, microhouse / deep house starter, dub techno / dub house,
long form (an eight-minute scaffold), multi-out to ableton.</p></section>''')

add('''<section class="page"><h2 id="devices">the studio devices</h2>
<p>Next to every faltung engine and effect, sndstudio adds its own devices. All have stable parameter ids (automation and locks keep working across versions),
run at any track width where that makes sense (the strip, colour, clip, transient, multiband and sidechain per channel; the stereo devices on the first pair) and
have a test in <code>sndstudioTest</code>.</p>
<table class="params"><tr><th>device</th><th>what it is</th></tr>
<tr><td class="n">strip</td><td>channel strip: high-pass, low-pass, four parametric bands (low shelf / bell / high shelf), a tilt around 650 hz, saturation (anti-aliased), a gate / expander with ratio, attack, release and range for ghost-note shaping, output.</td></tr>
<tr><td class="n">transient</td><td>transient designer: attack and sustain ±100%, speed, mix.</td></tr>
<tr><td class="n">sidechain</td><td>a compressor keyed from any track (<i>key</i> on the strip; the master by default) or from itself; threshold, ratio, attack, release, knee, makeup, mix, range, a high-pass on the key; shapes: compressor, duck (the classic kick-ducked bass), pump with hold.</td></tr>
<tr><td class="n">colour</td><td>a tape or console stage: drive, bias (even harmonics), hf loss, head bump, and drift — a slow random walk of gain and tone, different on every track.</td></tr>
<tr><td class="n">stereo</td><td>width, mid and side levels, haas (a delay on the right side), mono below a frequency, balance, swap.</td></tr>
<tr><td class="n">throw</td><td>the dub send delay: time synced or free (flipping keeps the time), feedback above 100% (the loop saturates), high- and low-pass inside the loop, saturation, wow, ping-pong; <b>throw</b> lets the input in (map it to a key for the momentary throw), <b>hold</b> freezes the loop.</td></tr>
<tr><td class="n">spring</td><td>a spring reverb (dispersive allpass chains): tension, decay, tone, drip, mix, <b>freeze</b> for an infinite tail.</td></tr>
<tr><td class="n">vinyl</td><td>wow and flutter, crackle (density and level), noise floor, bandwidth, low cut, saturation, mix.</td></tr>
<tr><td class="n">repeat</td><td>beat repeat / buffer override: on a grid (synced or ms) with a chance, a slice of a length (synced or ms) repeats N times with a pitch decay per repeat, reverse chance, bits, rate divide, gate, variation; <b>override</b> forces it now.</td></tr>
<tr><td class="n">grain</td><td>a granular cloud from the last two seconds of input: size, density, pitch, spray, jitter, reverse, window shape, spread, freeze.</td></tr>
<tr><td class="n">multiband</td><td>three-band distortion (soft / fold / hard per band), crossovers, band levels, mix.</td></tr>
<tr><td class="n">clip</td><td>digital clipper (hard / soft / fold), drive, ceiling, bits, rate divide, dc filter.</td></tr>
<tr><td class="n">noise bed</td><td>white / pink / tape / brown noise added at a level, filtered, with slow drift and optional crackle.</td></tr>
<tr><td class="n">tone</td><td>precision generator (instrument): sine, pulse (width), click, impulse, white, pink; exact frequency in hz or key tracking; a duration in ms or the note's gate; dc-free.</td></tr>
<tr><td class="n">data</td><td>bursts of bits at a bit rate (bytes → pulses), from the note number or random on each hit, density, tone.</td></tr>
<tr><td class="n">fm perc</td><td>four-operator fm percussion: ratios, levels, decays, three algorithms, feedback, pitch and sweep, noise, drive, key tracking.</td></tr>
<tr><td class="n">sampler</td><td>one-shots, loops (while held) and <b>slices</b>: a sample cut on a grid or on its transients into up to 64 slices that steps address with their <i>slice</i> (or notes from C1); root, pitch, start / end, filter, adsr, gain, choke, reverse, velocity. Drop a file on it in the browser or on its track in the arrangement.</td></tr></table>
<h2 id="modes">produce and multi-out, into ableton</h2>
<p><b>produce</b>: every track and return goes to the master; the master's chain (filter → glue → ms0 in the template; ms0 is bypassed until you want it, its <i>auto</i>
listens to the track and sets a starting point) ends on the main output. Export the master as a wav (24-bit dithered or 32-bit float, true-peak limited at -1 dbtp if you like)
and the stems.</p>
<p><b>multi-out</b>: tracks and returns with an output channel go there instead of (or as well as, with <i>to master</i>) the master. <b>assign all outputs</b> puts the master on
channels 1 … w, then every track and return on consecutive channels, to-master off. As a plug-in, Live shows the buses "out 3-4" … "out 127-128": make an audio track per
pair with the plug-in's bus as its input and record. A pair that is not enabled in Live folds back into the main output so nothing is lost. The plug-in follows Live's tempo and
position. From the standalone, choose a 128-channel virtual device (BlackHole 128ch) as the output in <b>audio</b>, set the same device as the input of Live's tracks, and switch
<b>master chain bypass</b> on when the mastering happens there. The project remembers the mode.</p></section>''')

add('''<section class="page"><h2 id="spatial">spatial audio and ambisonics</h2>
<p>A track's <b>width</b> is the width of its chain and its output, 1 … 64 channels, changed any time; clips and instruments feed channels 1–2 of it and the devices negotiate their
widths along the chain. The workflow for an ambisonic session:</p>
<ol><li>Make a new track (stereo). In <b>mix</b>, set its <b>width</b> to 16 and switch <b>to master</b> off: it only feeds what you route it to.</li>
<li>Put the encoder on it (a hosted plug-in, or a studio device). The encoder reads the stereo (your clip, or the instrument) and produces 16 channels: the device header says "2 → 16".</li>
<li>Make the decoder track, set it to 24 (it will go out to a 24-channel array), put the decoder on it (16 → 24, or whatever it makes), keep <b>to master</b> on.</li>
<li>On the encoder track, <b>route</b> → the decoder track, 16 channels (from channel 1 into channel 1). Several routes per track are fine; a route that would feed a track back into itself is refused.</li>
<li>Set the <b>master width</b> to 24: the master strip shows 24 meters, and the master leaves on device channels 1–24.</li>
<li>For binaural work, a third track at 16 channels with a binaural decoder (16 → 2), routed from the encoder over 16 channels, to master on. Mute the 24-channel decoder, set the master back to 2, and arrange in stereo.</li></ol>
<p>A stereo file on a 16-channel track stays a stereo clip: it feeds channels 1–2, the encoder spreads it; bypass the encoder and only 1–2 carry it. A device with fewer inputs
than the incoming width reads the first ones; one with fewer outputs than the track width leaves the rest silent. Latency is compensated across routes (a route waits for
the destination's arrival time), so an encoder with latency still lands with everything else. Hosted plug-ins get the bus layout that matches (stereo in, 16 out for an encoder;
16 in, 2 out for a decoder; ambisonic layouts for 4, 9, 16, 25, 36, 49, 64 channels, discrete otherwise).</p>
<h2 id="audio">audio settings and recording</h2>
<p><b>audio</b> (header, standalone) opens the device page: input and output device, sample rate, buffer size, which channels are enabled (up to 128 each way), midi inputs.
Audio tracks pick their <b>in</b>: a mono or stereo pair of device inputs, the master (resampling) or any track; <b>monitor</b> off / in / auto (when armed) passes the input through
the track's inserts. Arm the track (<b>rec</b>), press <b>rec</b> in the header (or <b>r</b>) and play: a clip is written as 32-bit wav into the project's audio folder, placed at the
transport position with the device's input latency compensated; with the loop on and <b>punch</b>, only inside the loop. Stop ends the take; the clip appears on the timeline and
plays back sample aligned. Recording from the master or a track while playing is resampling; <b>render project to a new track</b> (file menu / browse) does the same offline for the
whole arrangement or the loop, 32-bit float, at bar 1 or the loop start, and equals the master export sample for sample. As a plug-in, recording takes the plug-in's own output only
(hosts do not give instrument plug-ins audio inputs).</p>
<h2 id="export">export and resampling</h2>
<table class="params"><tr><th>what</th><th>how</th></tr>
<tr><td class="n">export master</td><td>file menu: a wav through the master chain at the project sample rate, 24-bit with tpdf dither or 32-bit float, optionally true-peak limited at the ceiling; one bar of tail.</td></tr>
<tr><td class="n">export stems</td><td>one wav per track (post fader), per used return and the master, into a folder; the same options.</td></tr>
<tr><td class="n">export midi</td><td>the sequencer as a midi file, one track per track, conditions and chance resolved.</td></tr>
<tr><td class="n">resample</td><td>an audio track with <i>in</i> = the master or a track records what plays; <i>render project to a new track</i> renders offline; a track's output can be bounced to a new audio track from its strip.</td></tr></table>
<h2 id="keys">keys</h2>
<table class="params"><tr><th>key</th><th>does</th></tr>
<tr><td class="n">space</td><td>play / stop</td></tr><tr><td class="n">1–8</td><td>launch scene 1–8</td></tr><tr><td class="n">shift 1–8</td><td>launch pattern 1–8 on the selected track</td></tr>
<tr><td class="n">m · s</td><td>mute / solo the selected track</td></tr><tr><td class="n">f</td><td>fill, while held</td></tr><tr><td class="n">r</td><td>record</td></tr><tr><td class="n">l</td><td>loop</td></tr>
<tr><td class="n">↑ ↓</td><td>select the previous / next track</td></tr><tr><td class="n">return</td><td>stop the selected track at the next bar</td></tr><tr><td class="n">tab</td><td>next page</td></tr></table>
<h2 id="tech">technical notes</h2>
<ul><li>One engine for the app and the plug-in. The audio thread never allocates; structure changes happen on the message thread under a spin lock the audio thread only tries.</li>
<li>Every block is split at bar lines, so launches, scenes and clips start exactly on the beat; hits are sample accurate; the suite's engines read their parameters at the hit.</li>
<li>Latency: tracks are aligned to the longest chain, returns to theirs, routes to their destination's arrival; the plug-in reports the total. Direct outputs wait for the returns and the master so all 128 channels line up.</li>
<li>Devices: the suite's processors run in-process with their own presets and state; hosted VST3 / AU get stereo or multichannel layouts; a base value per parameter is what automation, modulators, macros and locks act on; the sum is written to the real parameter once per block when it changed.</li>
<li>Audio clips: cubic interpolation; the stretcher is a wsola (2048-sample grains, half overlap, phase-aligned by cross-correlation) with transients pinned to their time.</li>
<li>Exports: dither only at 24 bit, a true-peak limiter (4× interpolated detection) when asked, no processing otherwise.</li>
<li>Projects are xml; audio files are referenced relative to the project when they sit next to it; autosave every two minutes while there are changes.</li></ul>
</section>''')

if __name__ == '__main__':
    page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>sndstudio manual</title><style>{CSS}</style></head><body>{"".join(doc)}</body></html>'
    open(os.path.join(HERE, 'sndstudio.html'), 'w').write(page)
    chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
    pdf = os.path.join(HERE, 'sndstudio Manual.pdf')
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                    f'--print-to-pdf={pdf}', 'file://' + os.path.join(HERE, 'sndstudio.html')], check=True, capture_output=True)
    print(pdf)

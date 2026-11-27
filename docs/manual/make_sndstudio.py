#!/usr/bin/env python3
"""Builds "sndstudio Manual.pdf" (the daw's own manual) in the faltung manual's look. Pictures from img/sndstudio-*.png
(sndstudioTest --screenshots docs/manual/img). Also exports `chapter(shot)` for the two suite manuals."""
import html, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
MAIN = open(os.path.join(HERE, 'make.py')).read()
FONTS = '../../Resources/fonts'   # (relative: the html sits in docs/manual)
VERSION = '1.4.0'
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
sent to another program on its own channel, hosting of your plug-ins, recording of audio and of notes into the steps, undo, autosave,
export of mixes, stems and multichannel files. A standalone application (no plug-in versions).</p>
{sh('sndstudio-0')}
<p>Its own manual, <b>sndstudio Manual.pdf</b>, is in the download next to this one. In short: <b>session</b> is the scene grid
(click a cell to launch a pattern, keys 1–8 launch scenes), <b>arrange</b> the timeline (pattern clips, audio clips dropped from the
Finder, markers, the loop, automation lanes), <b>seq</b> the step grid of the selected track with every step's velocity, pitch,
chance, ratchet, micro-timing, length, condition, slide and four parameter locks, the pattern tools (euclid, rotate, mutate,
humanise, markov …) and the track's devices, <b>mix</b> the strips with meters, sends, output channels, widths and routes,
<b>mod</b> the modulators, macros and midi maps, <b>browse</b> the devices, plug-ins (scan), files and templates.</p>
<p>Right-click any control for <i>map a modulator</i>, <i>add an automation lane</i>, <i>midi learn</i> and <i>macro</i>.
In <b>produce</b> mode everything is mixed to the stereo master (with ms0 on it if you want); switch to <b>multi-out</b> and
<i>assign all outputs</i> to put every track and return on its own channel pair of the audio interface you chose — or of a
loopback device such as BlackHole, which another program records from.</p></section>'''

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
<li><a href="#intro">what it is</a></li><li><a href="#start">first ten minutes</a></li><li><a href="#session">session</a></li><li><a href="#arrange">arrange: timeline, clips, automation, song</a></li><li><a href="#timeline">the timeline: zoom, grid, editing</a></li><li><a href="#meter">time signatures</a></li><li><a href="#window">the window: bigger and smaller</a></li><li><a href="#noteclips">note clips: free notes</a></li><li><a href="#clipgain">crossfades and gain lines</a></li><li><a href="#groups">groups: folders and vcas</a></li><li><a href="#tempo">tempo changes and ramps</a></li><li><a href="#midirec">recording midi; controller lanes</a></li><li><a href="#warp">warp markers</a></li><li><a href="#freeze">freezing a track</a></li><li><a href="#sidechain">sidechain into plug-ins</a></li><li><a href="#latency">latency, low latency</a></li><li><a href="#controllers">controllers, mackie control</a></li><li><a href="#loudness">loudness (lufs, true peak)</a></li><li><a href="#tracktemplates">track templates</a></li><li><a href="#missing">missing files</a></li>
<li><a href="#seq">seq: steps, locks, tools, devices</a></li><li><a href="#mix">mix: strips, outputs, widths, routes</a></li><li><a href="#mod">mod: modulators, macros, midi</a></li>
<li><a href="#browse">browse: devices, plug-ins, files, templates</a></li><li><a href="#devices">the studio devices</a></li><li><a href="#modes">produce and multi-out</a></li>
<li><a href="#spatial">spatial audio and ambisonics</a></li><li><a href="#audio">audio settings and recording</a></li><li><a href="#export">export and resampling</a></li><li><a href="#keys">keys</a></li><li><a href="#tech">technical notes</a></li><li><a href="#timing">on the grid and off it: sync and free</a></li><li><a href="#tools2">more of the sequencer</a></li><li><a href="#devices2">more studio devices; capture, bounce, render</a></li><li><a href="#templates">templates</a></li><li><a href="#reference">every device, every parameter</a></li><li><a href="#blackhole">from the standalone into Ableton (BlackHole)</a></li><li><a href="#install">installing</a></li><li><a href="#daw">a new kind of daw: what it does for production</a></li><li><a href="#notes">notes without a piano roll (steps)</a></li><li><a href="#artists">to work like …</a></li></ol>
<h2 id="intro">what it is</h2>
<p class="lead">sndstudio is a studio for microhouse, rominimal, dub techno and the music around them, built out of the faltung suite:
its drum engines, bass synth, effects and mastering bus are the devices, and around them a step sequencer that goes further than kt1,
scenes and a song, a timeline, automation on everything, modulators on everything, sends and returns, a mixer whose channels
can leave through 128 outputs, hosted VST3 / AU plug-ins, audio tracks, recording of audio and of notes, resampling and stems.
It is not a copy of other daws: there is no piano roll, notes go into steps — played in, typed, drawn as a contour on the pitch lane,
grown and shrunk as additive figures, sieved and scattered — and everything else serves producing a finished track.</p>
<p>It comes twice: <b>faltung sndstudio</b> (the microhouse devices and templates) and <b>faltung deep sndstudio</b> (the deep house
twin, in faltung's dark mode). Both are standalone applications only: there are no VST3 / AU / VST2 versions. Projects from earlier versions open as they were.</p>
<p>Two ways of working, switched in the header or the mixer: <b>produce</b> — everything is mixed inside, to a stereo master with the
ms0 chain on it, and exported as a mastered wav and as stems; <b>multi-out</b> — every track, return and the master leave on their own
output channels of the interface you chose (up to 128), or of a loopback device such as BlackHole, so another program can record them and the mixing and mastering happen there.</p>
<h3>the pages</h3>
<table class="params"><tr><th>page</th><th>what is there</th></tr>
<tr><td class="n">session</td><td>tracks across, scenes down; every cell is a pattern of that track. Click a cell to launch it (quantised to the bar), the triangle launches the whole scene, keys 1–8 too.</td></tr>
<tr><td class="n">arrange</td><td>the timeline: pattern clips and audio clips per track, markers, the loop region, the playhead; under it the automation lanes of the selected track; to the right the song (scenes in order with bar counts).</td></tr>
<tr><td class="n">seq</td><td>the selected track: its pattern (1–64), length, speed, swing, shift, chance, root note, clock, chain; the 16-step grid (pages for longer patterns); the selected step's velocity, pitch, chance, ratchet, micro, length, condition, slide, slice and locks; the tools; the track's devices with every parameter as a slider.</td></tr>
<tr><td class="n">mix</td><td>strips for the tracks, returns and master: meters (one bar per channel), level, pan, eight sends, mute / solo / rec, output channels, width, to master, routes, input, key. The produce / multi-out switch, assign all outputs, the master width and the master chain bypass.</td></tr>
<tr><td class="n">mod</td><td>the modulators of the selected track (click one to edit it), the eight macros (named), the midi maps and midi learn.</td></tr>
<tr><td class="n">browse</td><td>every device (faltung, studio, plug-ins) to add to the selected track, a return or the master; scan plug-ins; a file browser whose files drag onto the timeline or onto a sampler; new from template.</td></tr></table></section>''')

add('''<section class="page"><h2 id="start">first ten minutes</h2>
<ol><li>Open the app. It starts on the <b>microhouse starter</b> (deep: <b>deep house starter</b>): eight tracks — kick, rim, hat, open hat, clap, perc, click, bass —
two returns (dub delay, room), a filter / glue / ms0 chain on the master, eight scenes, a song, two automation lanes, a few modulators and macros.</li>
<li>Press <b>space</b>. Scene "full" is playing (the triangle at the left of a scene launches it, keys 1–8 too). Click cells to change patterns per track.</li>
<li>Open <b>seq</b>. The rim is selected. Click steps, drag up and down for velocity, shift-drag to move a step early or late, alt-click for chance, right-click for ratchets. Press <b>+ lock</b> to lock a parameter on the selected step, or pick a slider below: it shows a white bracket where the lock sits.</li>
<li>Right-click the dub return's <i>feedback</i> slider (mix page → select the return → seq page) and choose <b>map a modulator</b>: a slow random with slew. The orange band under the slider is where it moves.</li>
<li>Switch the header to <b>song</b>: the eight scenes play in order, 8 bars each. Open <b>arrange</b> to see them as clips; drag an audio file from the Finder onto a lane.</li>
<li>Press <b>write</b>, play, and move anything: automation lanes appear under the track in arrange.</li>
<li><b>save</b> (header, cmd+s) writes a <code>.fstudio</code> next to its audio folder. <b>file → export audio</b> (cmd+e) renders the mix, stems, chosen tracks or a time selection as wav, aiff or flac.</li>
<li>Made a mistake? <b>cmd+z</b> undoes it, <b>cmd+shift+z</b> brings it back.</li>
<li>To feed another program: <b>mix → multi-out → assign all outputs</b>, and in <b>audio</b> choose an interface with enough outputs, or a loopback device such as BlackHole.</li></ol>
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
<p>The ruler counts bars, beats, sixteenths and ticks (as many as the zoom has room for) and, under them, minutes and seconds (to the millisecond when
zoomed in; it follows a tempo lane). Click the ruler to put the playhead there, drag on it to set the loop (<b>l</b> or the header switches it on), double-click or
shift-click it for a marker. Each lane holds pattern clips (p1, p2 …) and audio clips (with their waveform and fades). Click a clip to select it, drag to move,
drag either edge to trim, alt-drag to copy, right-click for its menu; double-click an empty spot to add the track's current pattern for four bars. Everything about
zooming, the grid, selecting and editing is on the next page.</p>
<p><b>Audio clips.</b> Drop wav / aif / flac / mp3 files from the Finder or from the browser onto a lane (a new audio track if the lane is an instrument). Double-click a clip
(or right-click → edit) for gain, fades, pitch, mode, stretch, start in file, reverse, loop. Modes: <i>repitch</i> (speed follows pitch, like a varispeed deck),
<i>stretch</i> (length free, pitch kept, transient-preserving), <i>warp to tempo</i> (the clip fits its bars at the project tempo). Playback is sample accurate with
cubic interpolation; the stretcher is a wsola that pins every transient to its place in time.</p>
<p><b>Automation.</b> The lanes under the tracks belong to the selected track (and the globals: macros, tempo, swing, master). Add one with <b>+ lane</b> or from any
control's right-click. Click in a lane to add a point, drag it, alt-drag for the curve, double-click or right-click to delete; click the lane's name to switch it off,
right-click it to remove it. With <b>write</b> on, moving a control while playing records points. Automation is the base value; modulators ride on top of it.</p>
<p><b>Song.</b> The list on the right: scenes in order with bar counts. <b>+ song entry</b>, click one to choose its scene, drag to change its bars, right-click to remove.
The header's mode cycles session → song → arrange: in song mode the scenes play in order from bar 1; in arrange the clips on the timeline play.</p></section>''')

add('''<section class="page"><h2 id="timeline">the timeline: zoom, grid, editing</h2>
''' + shot('sndstudio-arrange-zoom') + '''
<p><b>Zoom.</b> From a thousand bars across down to about two samples a pixel. Cmd (ctrl) + scroll or a trackpad pinch zooms where the mouse is; <b>=</b> and <b>-</b>
(or the toolbar's + and −) zoom around the playhead; <b>z</b> zooms to the selected clips (or the loop), <b>shift+z</b> shows the whole project. Scroll sideways
with the trackpad, shift + scroll or the bar under the lanes; alt + scroll or <i>rows + / −</i> makes the rows taller or shorter. <b>follow</b> turns the page
with the playhead while it plays.</p>
<p><b>The grid.</b> <i>grid</i> in the toolbar (or the view menu) chooses it: <i>adaptive</i> follows the zoom (from 64 bars to a 1/512 note), or a fixed bar, 1/2 …
1/128, triplets (1/2t … 1/32t), dotted (1/4d, 1/8d, 1/16d), a quintuplet or a septuplet sixteenth. With <b>snap</b> on (<b>n</b>) clips, edges, the loop, markers and
automation points land on the grid, and they also catch on anything close by: the edges of other clips, markers, the loop and the playhead. Hold <b>cmd</b>
(ctrl) while dragging to put things exactly where the mouse is, to the sample.</p>
<p><b>Selecting.</b> Click a clip; shift- or cmd-click adds or takes one away; drag across empty space to draw a box around clips (on several tracks at once);
<b>cmd+a</b> selects every clip, <b>esc</b> none. A click on empty space moves the playhead there. Dragging moves every selected clip together, and up or down onto
other tracks of the same kind.</p>
<table class="params"><tr><th>edit</th><th>keys</th><th>does</th></tr>
<tr><td class="n">cut · copy · paste</td><td>cmd+x · cmd+c · cmd+v</td><td>paste puts the clips at the playhead, the topmost on the selected track, the others keeping their distance below it</td></tr>
<tr><td class="n">duplicate</td><td>cmd+d</td><td>the selection again, right after itself</td></tr>
<tr><td class="n">split</td><td>cmd+t</td><td>cuts the selected clips (or the selected track's clip) at the playhead. A pattern clip's second half carries on in the pattern where the first stopped</td></tr>
<tr><td class="n">delete</td><td>delete / backspace</td><td>removes the selected clips</td></tr>
<tr><td class="n">mute</td><td>cmd+shift+m</td><td>a muted clip stays in place, greyed, silent</td></tr>
<tr><td class="n">trim</td><td>drag an edge</td><td>the right edge sets the length; the left edge moves the start while the sound stays where it is (audio: the start in the file moves with it; a pattern: it starts further into its loop)</td></tr>
<tr><td class="n">fades</td><td>drag a top corner</td><td>an audio clip's fade in and fade out, drawn on the clip</td></tr>
<tr><td class="n">move by the grid</td><td>← →</td><td>the selected clips by one grid step; cmd ← → by one tick (1/960 of a beat), alt ← → by a millisecond; <b>,</b> and <b>.</b> by 5 ms. With nothing selected the playhead moves from grid line to grid line</td></tr>
<tr><td class="n">position and length</td><td>right-click a clip</td><td>type where it starts and how long it is (the rest of the selection moves with it)</td></tr>
<tr><td class="n">loop the selection</td><td>cmd+l</td><td>the loop around the selected clips</td></tr>
<tr><td class="n">insert time</td><td>cmd+shift+i</td><td>empty bars at the playhead: clips across it are cut, everything after it (clips, automation, markers, the loop) moves later; automation holds its value through the gap</td></tr>
<tr><td class="n">delete time</td><td>cmd+shift+backspace</td><td>removes the looped bars from every track, lane and marker; everything after closes up</td></tr>
<tr><td class="n">go to</td><td>cmd+g, or click the position in the header</td><td>type a position as bars.beats.sixteenths.ticks (17.2.3.120) or a time (1:05.250, 90s, 250ms)</td></tr>
<tr><td class="n">markers</td><td>[ · ]</td><td>the playhead to the previous / next marker. Drag a marker to move it, double-click it to name it, right-click it to remove it</td></tr>
<tr><td class="n">start · end</td><td>home · end</td><td>the playhead to bar 1 / the end of the project</td></tr>
<tr><td class="n">takes</td><td>alt+&#8593; · alt+&#8595;, or the clip's menu</td><td>record with the loop on: every pass is a take, stacked on the loop, the last one playing. Choose another one; split the takes at the same place and choose one for each part (comping)</td></tr>
<tr><td class="n">names and colours</td><td>right-click a clip</td><td>rename a clip; give the selected clips a colour (or the track's again)</td></tr>
<tr><td class="n">automation points</td><td>cmd-click · cmd-drag</td><td>select points (one lane), drag them together in time and value, cmd+c / cmd+v to copy them to the playhead, delete; right-click for set, scale, move up / down, flatten, invert, straight lines</td></tr></table>
<p>Positions are written <i>bar.beat.sixteenth.tick</i>, counted from 1 like every daw, with 960 ticks a beat (240 a sixteenth); lengths count from 0 (<i>2.1.0.0</i> is two
bars and a beat). The header shows where the playhead is in both, and the arrangement's toolbar shows where the mouse is. Every edit is one step of undo.</p>
<h2 id="meter">time signatures</h2>
''' + shot('sndstudio-meter') + '''
<p>Any time signature, changing at any bar. Right-click the ruler at a bar (or edit → <i>time signature at the playhead's bar</i>) and type it:</p>
<table class="params"><tr><th>type</th><th>is</th></tr>
<tr><td class="n">7/8 · 5/4 · 13/16 · 3/2 · 11/32</td><td>any numerator over any denominator</td></tr>
<tr><td class="n">2+2+3/8 · 3+3+2/8</td><td>grouped: the beats are counted the same, the groups' first beats are accented (a middle click, a stronger line)</td></tr>
<tr><td class="n">3.5/4 · 2.75/4</td><td>a fractional numerator: the bar ends part way through a beat</td></tr>
<tr><td class="n">4/3 · 5/6 · 7/12</td><td>irrational meters: the beat is a third, a sixth, a twelfth of a whole note</td></tr>
<tr><td class="n">7/8 5/8 · 3/4 3/4 2/4</td><td>several in turn: a cycle that repeats bar by bar until the next change (dynamic meters: 7/8 5/8 7/8 5/8 …)</td></tr></table>
<p>The change starts at that bar and lasts until the next one; an empty entry (or right-click → remove) takes it away. The signatures show on the ruler, the current one in the header.
Everything counts in the meter: bar lines, the beat lines (the denominator's note), positions (<i>bar.beat.sixteenth.tick</i>, the beat being an eighth in 7/8),
the grid (it starts again at every bar line, so a 1/4 grid in 7/8 ends on the bar), snapping to bars, the metronome (high on the bar, middle on a group, low on
the other beats) and the count-in (whole bars of the meter it leads into), quantised launches (the next bar line), the song (its entries last bars of the meter),
the export's bar numbers, and midi files (time signature events; a midi file only holds a whole numerator over a power of two, so 4/3 is written as the nearest it can).
Hosted plug-ins are told the signature and where the bar started. The steps of a pattern stay sixteenths: give a pattern 14 steps for a bar of 7/8.</p>
<h2 id="window">the window: bigger and smaller</h2>
''' + shot('sndstudio-ui-125') + '''
<p>The window can be any size. <b>cmd+=</b> and <b>cmd+-</b> (the view menu) make everything bigger or smaller, from 50 % to 200 % (cmd+0: actual size); the size is
remembered. Smaller leaves more room for the timeline and the mixer; bigger is easier to read. When the window is too small for the size chosen, sndstudio draws
everything as large as still fits, and says so; make the window bigger to get the rest. In a narrow window the header puts the master, punch and capture on its second row.</p></section>''')

add('''<section class="page"><h2 id="noteclips">note clips: free notes</h2>
''' + shot('sndstudio-notes') + '''
<p>Patterns are steps. A <b>note clip</b> is the other way: notes placed anywhere, any length, as they are, for an instrument track. It plays whatever is on the
track: the suite's instruments, the studio devices, or a hosted vst3 / au instrument (it gets the notes as midi). A note clip plays exactly what it shows: no
chances, conditions or swing (those belong to the steps).</p>
<table class="params"><tr><th>to</th><th>do</th></tr>
<tr><td class="n">make one</td><td>right-click an empty place on an instrument track's lane: <i>new note clip here</i> (four bars of the meter), <i>a midi file here...</i>, or drop a <code>.mid</code> file on the track</td></tr>
<tr><td class="n">from a pattern</td><td>right-click a pattern clip: <i>a note clip from its pattern</i>: what its steps play over the clip, as notes, to change one by one</td></tr>
<tr><td class="n">edit</td><td>double-click it (or its menu: <i>edit the notes</i>): the note editor opens over the lanes</td></tr>
<tr><td class="n">a note</td><td>click an empty place: a note, the grid's length, on the grid (the arrangement's grid and snap)</td></tr>
<tr><td class="n">move · length</td><td>drag a note (in time on the grid, and in pitch); drag its right end for its length. The selected notes move and stretch together</td></tr>
<tr><td class="n">velocity</td><td>alt-drag up / down; the fuller the note, the harder</td></tr>
<tr><td class="n">select</td><td>cmd-click a note, cmd-drag a box, cmd+a all</td></tr>
<tr><td class="n">remove</td><td>double-click or right-click a note; delete removes the selected</td></tr>
<tr><td class="n">transpose · quantise</td><td>&#8593; &#8595; a semitone, shift: an octave; <b>q</b> the selected notes (or all) onto the grid</td></tr>
<tr><td class="n">close</td><td>esc, or <i>close</i></td></tr></table>
<p>In the arrangement a note clip shows its notes small, with its name and how many. Split, trim, duplicate, copy and move it like any clip: a part of it plays its
own part of the notes (a note is cut at the clip's end). Note clips are saved with the project and written into the midi export.</p></section>''')

add('''<section class="page"><h2 id="clipgain">crossfades and gain lines</h2>
''' + shot('sndstudio-xfade') + '''
<p><b>Crossfades</b> are made by themselves: where an audio clip starts inside another on the same track and ends after it, the earlier one fades out and the
later one in over the whole overlap, equal power (each at -3 db in the middle, so the loudness holds across different material). Drag the clips to make the overlap
longer or shorter; the curves show over it. A clip lying wholly inside another (layered) and muted clips (takes not chosen) don't crossfade. The clips' own fades
(the top corners) still work on top.</p>
''' + shot('sndstudio-gain') + '''
<p>A <b>gain line</b> is a clip's level drawn over its length, the clip's own automation (it moves with the clip, is copied and split with it). Right-click an
audio clip: <i>draw its gain line</i>. Then click inside the clip for a point (on the grid; +12 db at the top, -36 db at the bottom, 0 db a quarter down), drag a
point to move it, double-click it to take it away; the status line shows its db. Click outside the clip when done. Between points the level goes straight in db;
before the first and after the last it holds. <i>clear its gain line</i> takes it away. It comes after the clip's gain and fades, before the track's chain.</p></section>''')

add('''<section class="page"><h2 id="groups">groups: folders and vcas</h2>
''' + shot('sndstudio-groups') + '''
<p>A <b>group</b> is a set of tracks: a folder in the arrangement and a vca fader in the mixer. Right-click a track's name in the arrangement:</p>
<table class="params"><tr><th>item</th><th>does</th></tr>
<tr><td class="n">new group with this track</td><td>a group in the track's colour, the track in it</td></tr>
<tr><td class="n">into the group</td><td>the track joins a group (a track is in one group at most)</td></tr>
<tr><td class="n">out of …</td><td>the track leaves its group</td></tr>
<tr><td class="n">fold · unfold</td><td>or click the triangle by the name: a folded group is one row, its first track's, named after the group (the others' clips play as before)</td></tr>
<tr><td class="n">rename · ungroup</td><td>a name; ungroup takes every track out and removes the group</td></tr></table>
''' + shot('sndstudio-vca') + '''
<p>Every group has a <b>vca strip</b> at the right of the mixer: its fader (-inf to +12 db; shift: fine, double-click: 0 db) scales every member's level without
moving their own faders, so a whole drum group comes down 3 db and its balance stays. Its <b>mute</b> mutes the members; its <b>solo</b> solos them all (with any
tracks soloed on their own). Double-click its name to rename it. Groups, folds and vca settings are saved with the project.</p></section>''')

add('''<section class="page"><h2 id="tempo">tempo changes and ramps</h2>
''' + shot('sndstudio-tempo') + '''
<p>The tempo can change anywhere and glide from one to another. <b>+ lane</b> → <i>tempo</i> (at the top of the menu): a lane of the song's tempo. A point is a tempo
from there on; two points with the line between them sloping are a ramp (accelerando, ritardando); a point just before another at a new tempo is a jump. Draw it
like any automation lane (alt-drag a point for a curve).</p>
<p>Everything follows: the playhead and the clock in the header and on the ruler (the seconds are worked out through every change), the steps and note clips
(they play at the tempo where they are), the click, the modulators synced to the tempo, the hosted plug-ins (they are told the tempo as it changes), the export
and the midi file. Audio clips keep their own time: a <i>repitched</i> clip plays at its own speed across a change (sample for sample), a <i>stretched</i> one at its own
stretch, and clips set to <i>warp to tempo</i> or with <b>warp markers</b> stay on their bars, slowing down and speeding up with the song.</p>

<h2 id="midirec">recording midi into note clips</h2>
<p>In the arrangement, arm an instrument track and record: what you play goes into a <b>note clip</b>, as played: the notes where they were played and as long as they
were held, how hard, and the controllers moved while playing: the mod wheel and any cc, pitch bend and channel pressure. The clip starts at the bar of the first
thing played and grows as the recording goes on; with the loop on, every pass is added to the same clip (overdub). Stop: the notes still held end there.
In the session (and on the step pages) recording still goes into the steps of the pattern playing.</p>

<h2 id="ctllane">controller lanes in note clips</h2>
''' + shot('sndstudio-cclane') + '''
<p>Under the notes in the note editor is a lane: <b>velocity</b> (a bar a note: drag across them to set them), or a <b>controller</b>: click its name on the left for the mod wheel,
pitch bend, pressure, expression, sustain, breath, brightness, any cc by number, or the ones the clip already has. Drag to draw it (it steps at half the grid);
alt-drag to rub it out; <i>clear this lane</i> takes the controller away. The instrument gets them as midi with the notes: the suite's instruments and hosted plug-ins alike;
they are in the midi export, and a midi file brings its controllers with it.</p></section>''')

add('''<section class="page"><h2 id="warp">warping audio: warp markers</h2>
''' + shot('sndstudio-warp') + '''
<p>A clip's audio can be moved in time inside itself, without moving the clip: a loop that drags behind the beat, a live take with one hit late, a break turned into a
different groove. Right-click an audio clip → <b>warp</b>:</p>
<table class="params"><tr><th>item</th><th>does</th></tr>
<tr><td class="n">set warp markers</td><td>click the audio for a marker (it pins the sound under the mouse); drag a marker left or right (on the grid; cmd: free) and the audio before and after it stretches to fit; double-click a marker to remove it. Click outside the clip when done</td></tr>
<tr><td class="n">markers at the transients</td><td>a marker on every hit the stretcher found, where it plays now</td></tr>
<tr><td class="n">markers to the grid</td><td>every marker onto its nearest grid line: the audio quantised (with a triplet or swung grid, a groove put onto it)</td></tr>
<tr><td class="n">clear the markers</td><td>back to the clip's own timing</td></tr></table>
<p>The clip plays in the <i>warp markers</i> mode (also in its editor's mode list). Between markers the audio is stretched with the pitch kept; a hit at a marker lands
exactly on it (the stretcher pins transients). Before the first marker the audio runs from the clip's start; after the last it goes on at its own tempo. The markers
are on the bars, so they follow tempo changes. They show as blue lines with a handle; the waveform shows the audio where it plays.</p>

<h2 id="freeze">freezing a track</h2>
<p>Right-click a track's name → <b>freeze</b>: its whole chain (the instrument or the clips, and every device) is rendered to a file, from bar 1 to the end of the project and a bar
of tail, and the track plays the file instead: the same sound, and no processor time for its instrument and devices. The fader, pan, sends and routes still work (the file is the
chain before the fader). A frozen track says so by its name. <b>unfreeze</b> makes it live again (to change its notes, clips or devices: edits to a frozen track are heard once it is unfrozen).
Frozen tracks stay frozen when the project is saved and opened; the files are in the project's audio folder.</p>

<h2 id="sidechain">sidechain into hosted plug-ins</h2>
<p>A vst3 / au effect with a sidechain input (a compressor, a gate, a vocoder, a ducker) gets one: its sidechain bus is switched on (stereo, or mono) and fed from the
track's <b>key</b> (the <i>key</i> button on the strip: any track, or the master), the same key the suite's own devices listen to. So a pumping bass from the kick is: the
plug-in on the bass track, the key set to the kick.</p></section>''')

add('''<section class="page"><h2 id="latency">latency: what each track waits, low latency</h2>
<p>Devices that look ahead (limiters, linear-phase eqs, many plug-ins) delay the sound; sndstudio lines every track up with the slowest (delay compensation), so the mix stays
in time. Each strip in the mixer shows its own delay (<i>12.3 ms</i>) and what it waits to line up with the rest (<i>+4.0 wait</i>); the header shows the total.</p>
<p><b>low latency</b> (the mixer's top row): for recording and playing in. Devices that delay the sound by more than about 1.3 ms are left out, and the compensation shrinks to what is
left, so what you play is heard at once. The instruments stay. Turn it off when the recording is done and everything is back.</p>

<h2 id="controllers">controllers: buttons, mackie control</h2>
''' + shot('sndstudio-controllers') + '''
<p><b>mod</b> → <i>controllers...</i>: every command a button on a controller can run: play / pause, stop, record, loop, metronome, previous / next marker, undo, redo,
previous / next track, arm / mute / solo the selected track, to the start, a bar back / on, save, scenes 1-8. Click <i>learn</i>, press a button (a pad's note, or a button that
sends a cc): it is mapped; <i>clear</i> takes it away. Knobs and faders are mapped to any control as before (right-click it → midi learn).</p>
<p><b>mackie control</b>: for surfaces that speak it (most fader boxes and many keyboards have the mode). Its eight faders are eight tracks' levels and the ninth the master's;
the v-pots are their pans; rec, solo, mute and select are each channel's; bank and channel move the eight along the tracks; the transport plays, stops, records, loops and
moves by bars; click, marker, undo and save do what they say. (sndstudio listens; it doesn't move motor faders.)</p>

<h2 id="loudness">loudness: lufs and true peak</h2>
''' + shot('sndstudio-loudness') + '''
<p>The master strip measures loudness the way streaming services and broadcasters do (itu bs.1770, ebu r128): <b>momentary</b> (400 ms), <b>short-term</b> (3 s),
<b>integrated</b> (the whole time since play, gated), the <b>loudness range</b> (lu) and the <b>true peak</b> (between the samples, dbtp; red above -1). It measures again
each time playing starts; click it to start again by hand.</p>
<p>The export can aim at a loudness: <i>-14 lufs</i> (spotify, youtube, tidal), <i>-16</i> (apple music), <i>-11</i> (club), <i>-9</i> (very loud), <i>-23</i> (broadcast). The mix is turned up
or down to it, never past the ceiling (the limiter's -1 dbtp, or 0): when the peaks would go over, it stops there and says how loud it is. Every mix export says its integrated
loudness and true peak when it is done.</p></section>''')

add('''<section class="page"><h2 id="tracktemplates">track templates</h2>
<p>A track you like (an instrument with its sound, a chain of devices, the mixer settings, its patterns) can be kept and used again in any project: right-click its
name → <b>save as a track template...</b>. <b>new track from a template</b> (the same menu) adds it. They are files (<code>.ftrack</code>) in
<code>Documents/faltung sndstudio/track templates</code>, to copy to another computer. A template keeps the sound, not the song: its clips stay in their project.</p>

<h2 id="missing">missing files</h2>
<p>A project opened where its audio is somewhere else, or a plug-in isn't installed: <b>file → find missing files...</b> says what is missing. <i>look in a folder...</i>:
choose a folder (a drive, the music folder, a backup) and every folder in it is looked through for the files by name; what is found is loaded, and saved with its new place.
A plug-in that isn't installed is kept in the project exactly as it was (its settings too) and saved back that way, so on a computer that has it, it is there again.</p></section>''')

add('''<section class="page"><h2 id="seq">seq</h2>
''' + shot('sndstudio-2') + '''
<h3>the track</h3>
<table class="params"><tr><th>control</th><th>what it does</th></tr>
<tr><td class="n">pattern</td><td>which of the track's 64 patterns is playing (launched at the next quantise point)</td></tr>
<tr><td class="n">length</td><td>1–64 steps; longer than 16 shows pages above the grid. Different lengths on different tracks = polymeter.</td></tr>
<tr><td class="n">speed</td><td>x1/4 … x8: how fast the steps run, relative to 16ths</td></tr>
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
<p><b>Macros</b>: eight sliders; name them; map a parameter to one from its right-click (the macro sets it between the value it had and the top).
<b>Midi</b>: right-click a control → <i>midi learn</i>, move a controller; the map is listed here (right-click to remove). Controllers also reach modulators of source <i>midi cc</i>.</p>
<h2 id="browse">browse</h2>
''' + shot('sndstudio-5') + '''
<p>Devices by family: the faltung (or faltung deep) instruments and effects, the studio devices, and every plug-in the scan found. Click one to add it to the selected
track (instruments replace the instrument), right-click for a return, the master or a new track. <b>scan plug-ins</b> looks in the usual VST3 / AU folders; every file is opened in a separate
process, so a plug-in that crashes or hangs while it is scanned cannot take sndstudio down — it is put on a blacklist and skipped from then on. Files: choose a folder, drag files onto the arrangement or onto an instrument track with a sampler; double-click a file to put it on
the selected audio track at the playhead (or into the selected track's sampler). <b>new from template</b>: empty, microhouse / deep house starter, dub techno / dub house,
long form (an eight-minute scaffold), multi-out (every track on its own outputs).</p></section>''')

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
<h2 id="modes">produce and multi-out</h2>
<p><b>produce</b>: every track and return goes to the master; the master's chain (filter → glue → ms0 in the template; ms0 is bypassed until you want it, its <i>auto</i>
listens to the track and sets a starting point) ends on the main output. Export the mix (and stems) from <b>file → export audio</b>.</p>
<p><b>multi-out</b>: tracks and returns with an output channel go there instead of (or as well as, with <i>to master</i>) the master. <b>assign all outputs</b> puts the master on
channels 1 … w, then every track and return on consecutive channels, to-master off. The channels are those of the audio interface chosen in <b>audio</b> (up to 128):
to separate outputs of a mixing desk, or to a loopback device such as BlackHole, from which another program (Ableton, Logic, REAPER …) records each pair on its own track.
A channel the device does not have folds back into the master so nothing is lost. Switch <b>master chain bypass</b> on when the mastering happens elsewhere.<b>master chain bypass</b> on when the mastering happens there. The project remembers the mode.</p></section>''')

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
<p><b>audio</b> (header; also the app's <i>audio</i> menu) opens the device page: input and output device, sample rate, buffer size, which channels are enabled (up to 128 each way), the midi inputs
(your keyboards; on the first start every connected one is on) and a midi clock output.
Audio tracks pick their <b>in</b>: a mono or stereo pair of device inputs, the master (resampling) or any track; <b>monitor</b> off / in / auto (when armed) passes the input through
the track's inserts. Arm the track (<b>rec</b>), press <b>rec</b> in the header (or <b>r</b>) and play: a clip is written as 32-bit wav into the project's audio folder, placed at the
transport position with the device's input latency compensated; with the loop on and <b>punch</b>, only inside the loop. Stop ends the take; the clip appears on the timeline and
plays back sample aligned. Recording from the master or a track while playing is resampling; <b>render project to a new track</b> (file menu / browse) does the same offline for the
whole arrangement or the loop, 32-bit float, at bar 1 or the loop start, and equals the master export sample for sample.
Notes from a midi keyboard are recorded into the steps of armed instrument tracks: see <a href="#daw">recording notes</a>.</p>
<h2 id="export">export and resampling</h2>
<table class="params"><tr><th>what</th><th>how</th></tr>
<tr><td class="n">export audio</td><td>file menu, cmd+e: <i>what</i> — the mix through the master chain, the mix before the master chain, stems of every track, used return and the master, or stems of the tracks you number;
<i>which bars</i> — the whole project, the loop (the time selection: drag it on the ruler), or bars you type; <i>file</i> — wav, aiff or flac; 16 bit, 24 bit or 32-bit float (wav);
the project's rate or 44.1 / 48 / 88.2 / 96 khz; normalised to -1, -0.3 or 0 dbfs or not; tpdf dither below 32 bit; a true-peak limiter at -1 dbtp for the mix. A master wider than stereo
(a 16-, 24- or 64-channel spatial mix) is written as one file with that many channels; stems keep each track's width.</td></tr>
<tr><td class="n">export midi</td><td>the sequencer as a midi file, one track per track, conditions and chance resolved.</td></tr>
<tr><td class="n">resample</td><td>an audio track with <i>in</i> = the master or a track records what plays; <i>render project to a new track</i> renders offline; a track's output can be bounced to a new audio track from its strip.</td></tr></table>
<h2 id="keys">keys</h2>
<table class="params"><tr><th>key</th><th>does</th></tr>
<tr><td class="n">space</td><td>play / stop</td></tr><tr><td class="n">1–8</td><td>launch scene 1–8</td></tr><tr><td class="n">shift 1–8</td><td>launch pattern 1–8 on the selected track</td></tr>
<tr><td class="n">m · s</td><td>mute / solo the selected track</td></tr><tr><td class="n">f</td><td>fill, while held</td></tr><tr><td class="n">r</td><td>record</td></tr><tr><td class="n">l</td><td>loop</td></tr>
<tr><td class="n">↑ ↓</td><td>select the previous / next track</td></tr><tr><td class="n">return</td><td>stop the selected track at the next bar</td></tr><tr><td class="n">tab</td><td>next page</td></tr>
<tr><td class="n">cmd+z · cmd+shift+z</td><td>undo · redo (cmd+y too)</td></tr><tr><td class="n">cmd+= · cmd+- · cmd+0</td><td>everything bigger · smaller · actual size</td></tr>
<tr><td class="n">arrange</td><td>= − zoom · z zoom to the selection · shift+z the whole project · n snap · ← → by the grid · [ ] markers · cmd+x c v d t a l g · delete · esc (the table under <a href="#timeline">the timeline</a>)</td></tr><tr><td class="n">cmd+s · cmd+shift+s</td><td>save · save as</td></tr><tr><td class="n">cmd+o · cmd+n</td><td>open · a new empty project</td></tr><tr><td class="n">cmd+e</td><td>export audio</td></tr></table>
<h2 id="tech">technical notes</h2>
<ul><li>One engine, driven by the app's own audio callback (up to 128 outputs and 128 inputs). The audio thread never allocates; structure changes happen on the message thread under a spin lock the audio thread only tries.</li>
<li>Every block is split at bar lines and, in the arrangement, wherever a clip starts or ends, so launches, scenes and clips start and stop on their sample wherever they are; hits are sample accurate; the suite's engines read their parameters at the hit.</li>
<li>Latency: tracks are aligned to the longest chain, returns to theirs, routes to their destination's arrival; the header shows the total. Direct outputs wait for the returns and the master so all 128 channels line up.</li>
<li>Devices: the suite's processors run in-process with their own presets and state; hosted VST3 / AU get stereo or multichannel layouts; a base value per parameter is what automation, modulators, macros and locks act on; the sum is written to the real parameter once per block when it changed.</li>
<li>Audio clips: cubic interpolation; the stretcher is a wsola (2048-sample grains, half overlap, phase-aligned by cross-correlation) with transients pinned to their time.</li>
<li>Exports: rendered to 32-bit float first, then written in a second pass (resampled, normalised, dithered below 32 bit) so every option is exact; a true-peak limiter (4× interpolated detection) when asked.</li>
<li>Projects are xml; audio files are referenced relative to the project when they sit next to it; autosave every minute while there are changes.</li>
<li>Undo keeps the project as it was before each edit (up to 200 steps); a drag is one step, a recorded take is one step.</li></ul>
</section>''')

# ------------------------------------------------------------------------------------------------
#  the later chapters: timing, the new tools, the new devices, templates, the device reference, install, artists
import json as _json
_P = _json.load(open(os.path.join(HERE, 'sndstudio-params.json')))
_PD = _json.load(open(os.path.join(ROOT, 'docs', 'manual-deep', 'sndstudio-params.json')))

add('''<section class="page"><h2 id="timing">on the grid and off it: sync and free</h2>
<p>Everything in sndstudio that runs in time can follow the tempo or ignore it. The rule is the same everywhere: a <b>sync</b> setting
counts in divisions of the bar (the list below), a <b>free</b> setting in hz, ms or seconds; flipping between them keeps the time it had
as closely as the other side allows, so nothing jumps.</p>
<table class="params"><tr><th>what</th><th>on the grid</th><th>off the grid</th></tr>
<tr><td class="n">a track's steps</td><td><i>clock</i> grid: a step is a 16th times <i>speed</i> (x1/4 … x8) times the step's own <i>step time</i></td><td><i>clock</i> free: every step lasts <i>step ms</i>, whatever the tempo</td></tr>
<tr><td class="n">a step's place</td><td>on its step</td><td><i>micro</i> (shift-drag in the grid), the track's <i>shift</i> in ms, the tools <i>ratios</i> / <i>accelerate</i></td></tr>
<tr><td class="n">modulators</td><td><i>sync</i>: 1/64 … 16 bars</td><td><i>free</i>: 0.01 … 40 hz</td></tr>
<tr><td class="n">fl7's lfo</td><td><i>rate</i> divisions</td><td>the last entry of <i>rate</i>, <b>free</b>, and the <i>free rate</i> control (hz)</td></tr>
<tr><td class="n">wb1 / sb1 wobble and steps</td><td><i>rate</i> divisions</td><td><b>free</b> as the last entry of the rate, with <i>free rate</i> / <i>free time</i></td></tr>
<tr><td class="n">mc3</td><td>divisions</td><td><b>free</b>, as before</td></tr>
<tr><td class="n">throw, repeat</td><td>time / grid / slice on divisions</td><td>the same in ms</td></tr>
<tr><td class="n">line, wear, skip</td><td><i>on the grid</i> on: pass / loop / fragment on divisions</td><td>off: seconds or ms</td></tr>
<tr><td class="n">audio clips</td><td><i>warp to tempo</i></td><td><i>repitch</i> or <i>stretch</i>, snap off for placing them anywhere</td></tr>
<tr><td class="n">the arrangement</td><td><b>snap</b> on: the grid (adaptive, straight, triplet, dotted, quintuplet, septuplet), and the edges nearby</td><td>snap off, or cmd while dragging: to the sample; ← → by a tick or (alt) a millisecond; <b>,</b> and <b>.</b> by 5 ms</td></tr></table>
<p>The divisions, wherever a list of them appears: ''' + ' · '.join(e(x) for x in _P['divisions']) + ''' (d = dotted, t = triplet).</p>

<h2 id="tools2">more of the sequencer</h2>
<h3>per track</h3>
<table class="params"><tr><th>control</th><th>what it does</th></tr>
<tr><td class="n">evolve</td><td>off … 100%: on every pass a few steps change (a hit appears or goes, velocities drift) or come back to what is written; the pattern drifts around its written form and never far from it. <b>freeze</b> (tools) writes the current drift into the pattern.</td></tr>
<tr><td class="n">choke group</td><td>off, a … h: a hit on one track of a group cuts the other tracks of that group (closed hat / open hat, chopped samples on several tracks).</td></tr></table>
<h3>per step</h3>
<table class="params"><tr><th>control</th><th>what it does</th></tr>
<tr><td class="n">step time</td><td>x0.25 … x4: how long this step lasts, in steps — a triplet next to a straight step, a long step after short ones. The pattern's length in time follows.</td></tr>
<tr><td class="n">ratchet curve</td><td>even, or the hits of a ratchet <i>speed up</i> (crowd towards the end) or <i>slow</i> (spread out)</td></tr>
<tr><td class="n">chord</td><td>one note · octave · fifth · minor · major · minor 7 · major 7 · minor 9 · sus 4 · stack of fourths, stacked on the step's note (synths, tone, sampler)</td></tr></table>
<h3>the tools that ask</h3>
<table class="params"><tr><th>tool</th><th>what you type</th><th>what it does</th></tr>
<tr><td class="n">ratios</td><td>1 1 2/3 2/3 2/3 3/2</td><td>one number per step: its step time. The series repeats over the length.</td></tr>
<tr><td class="n">accelerate</td><td>0.9</td><td>every step lasts this many times the one before; the loop keeps its length (below 1: faster and faster, above 1: slower)</td></tr>
<tr><td class="n">even</td><td>—</td><td>every step time back to x1</td></tr>
<tr><td class="n">strict</td><td>2 0.5</td><td>velocities on so many levels (1 = all alike, 2 = accent or not …), then one note length in steps for all</td></tr>
<tr><td class="n">sieve</td><td>3@0 | 4@1</td><td>the steps a sieve keeps get a hit, the rest none: residue classes modulus@residue, <b>|</b> joins, <b>&amp;</b> intersects, <b>!</b> excludes</td></tr>
<tr><td class="n">sieve pitch</td><td>3@0 | 4@1 (random)</td><td>the sieve read as a scale in semitones; the hits climb it in order, or draw from it with <i>random</i></td></tr>
<tr><td class="n">cloud</td><td>1 12</td><td>a stochastic cloud: events per step (the times between them exponential), then the pitch range in semitones; velocities gaussian</td></tr>
<tr><td class="n">freeze</td><td>—</td><td>writes the drift of <i>evolve</i> into the pattern</td></tr></table>
<h3>playing controls from the keyboard</h3>
<p>Right-click any control: <b>play it from a key: toggle</b> or <b>… while held</b>, then press the key. The key then switches the control between its value and its far end
(a mute, a send, the throw), and with <b>write</b> on the moves are written as automation. Mapped keys are listed on the mod page (right-click to remove); a mapped key no longer does
its usual job (so map letters you do not need).</p>
<h3>the one gesture: control → modulator</h3>
<p>Right-click any slider (a device's, a strip's, the tempo, the swing, the master): <b>map an lfo</b>, <b>map a random</b> (a new value each period), <b>map a slewed random</b>,
<b>map a random gate</b> (on / off). The modulator is made at once and its popup opens with its shape, rate (sync or free), depth and polarity. <i>map something else</i> holds the
envelope, the step sequence, the follower and <b>bounce</b> (four ramps at unrelated rates, each turning round where it meets its neighbours: four related voltages that never repeat).
A modulator's rate is itself a target, so modulators modulate modulators.</p>
<h3>freehand automation</h3>
<p>In an automation lane, shift-drag draws the curve by hand (a point every few pixels): arcs drawn on <i>tone</i>'s frequency become glissandi, on an encoder's azimuth a path around the room.</p></section>''')

add('''<section class="page"><h2 id="devices2">more studio devices</h2>
<table class="params"><tr><th>device</th><th>what it is</th></tr>
<tr><td class="n">encoder</td><td>ambisonic encoder: a mono or stereo source to order 1 – 7 (acn channel order, sn3d), with azimuth, elevation, stereo width, order (as wide as the track, or 1 – 7) and gain; azimuth and elevation are natural targets for lanes and modulators. On a 16-channel track it reads the stereo clip or instrument and writes 16 channels (order 3).</td></tr>
<tr><td class="n">decoder</td><td>ambisonics to a ring, a dome or a sphere of speakers (rotate, numbering clockwise or not, a speaker or a pair at the front); as many speakers as the track is wide (24 in the spatial template).</td></tr>
<tr><td class="n">binaural</td><td>ambisonics to headphones, with head turn and head size (a spherical-head model, for placing things while you arrange; hosted decoders do measured-ear work).</td></tr>
<tr><td class="n">line</td><td>on a wide track, each input channel becomes a line of sound travelling along the track's speakers: a pass on the grid or in seconds, a rest at the ends, legato from a hard switch to a melt, the figure (there and back, round, swinging, at random), a second line against the first, and a sieve choosing which speakers sound.</td></tr>
<tr><td class="n">wear</td><td>a loop of the input that goes round and loses a little on every pass: first the highs, then dropouts, then level. Loop length on the grid or in seconds; <i>record</i> off keeps the loop going without new input; wear, dropouts, mix.</td></tr>
<tr><td class="n">skip</td><td>an instrument that reads a sample like a skipping disc: a fragment repeats, the read point jumps on, back or anywhere; read errors drop out or hold a frame. <i>intensity</i> sets how broken it is; fragments on the grid or in ms; <i>from</i> / <i>to</i> bound the region; pitch; it plays until the next note or while the note is held; steps address places in the file.</td></tr>
<tr><td class="n">tone (more)</td><td>a <b>second tone</b>, a difference in hz or a ratio away (1.22 by default), each tone on its own channel (<i>one tone per channel</i>), so the difference tone is made in the ear; a <b>tuning</b> for key-tracked notes: equal, just (5-limit), just (7-limit), the harmonic series.</td></tr></table>
<p>Also in the device list, without a scan: the suite's kit and loop (<b>kt1</b>, <b>kt2</b>) as track instruments, and the house's other plug-ins when they are installed
(Partials, Cloudfin, ConvoSolution, ObliqSound, ObliqSonic, ballonwinkel, ballonpaleis, and openprocess, openfield, Leitner, Quell) — hosted as VST3 / AU like any plug-in.</p>
<h3>routes that close a loop</h3>
<p>A route into a track that already feeds the source closes a loop. The popup says so; if you accept, it becomes a <b>feedback route</b>: one buffer late and soft-limited, so the
loop sings but cannot run away.</p>
<h3>capture, bounce, render</h3>
<table class="params"><tr><th>what</th><th>where</th><th>what it does</th></tr>
<tr><td class="n">capture</td><td>header</td><td>a new audio track records the master from now until you click again: a jam to cut up afterwards</td></tr>
<tr><td class="n">bounce</td><td>a track's strip, or file → bounce</td><td>the track (offline, its whole arrangement or the loop) into a new audio track, or into a sampler on a new track, sliced</td></tr>
<tr><td class="n">render</td><td>the master strip's bounce, or file → render the project into a new audio track</td><td>the whole project or the loop, through the master chain or before it, onto a new audio track</td></tr>
<tr><td class="n">auto master</td><td>the master strip</td><td>puts ms0 on the master if it is not there, switches it (and the master chain) on, starts playback if stopped, listens to up to 20 s and sets ms0's starting point (click again to decide early). Not in faltung deep, which has no ms0</td></tr></table>
<h2 id="templates">templates</h2>
<p><b>file → new from template</b> (or browse → new from template). Each is a full project to start from.</p>
<table class="params"><tr><th>faltung sndstudio</th><th>faltung deep sndstudio</th></tr>''' +
''.join(f'<tr><td>{e(a)}</td><td>{e(b)}</td></tr>' for a, b in zip(_P['templates'], _PD['templates'])) + '''</table>
<p><i>bass first</i> starts from the bass at 172 in half time; <i>two-step</i> is a garage skeleton at 132 with mpc-style swing; <i>half-step</i> a 140 frame with wb1 in front;
<i>spatial</i> builds the encoder → decoder (24 speakers) → master and encoder → binaural chain described in the spatial chapter, with the master 24 wide.</p></section>''')

# ---- every device, every parameter
def _devtable(dev):
    rows = []
    for p in dev['params']:
        if 'choices' in p and len(p['choices']) <= 12: rng = ' · '.join(p['choices'])
        elif 'choices' in p: rng = f"{p['min']} … {p['max']} ({len(p['choices'])} choices)"
        elif p.get('switch'): rng = 'off / on'
        else: rng = f"{p['min']} … {p['max']}"
        rows.append(f'<tr><td class="n">{e(p["name"])}</td><td class="r">{e(rng)}</td><td class="d">{e(p["default"])}</td></tr>')
    head = f'<h3>{e(dev["name"])} <span style="font-weight:400">({e(dev["type"])}, {e(dev["family"])}{", instrument" if dev["instrument"] and dev["family"] == "studio" else ""})</span></h3>'
    blurb = f'<p>{e(dev["blurb"])}.</p>' if dev.get('blurb') else ''
    pres = ('<p class="presets"><span>presets</span> ' + ' · '.join(e(n) for n in dev['presets']) + '</p>') if dev.get('presets') and dev['presets'] != ['init'] else ''
    return head + blurb + pres + '<table class="params"><tr><th>parameter</th><th>range</th><th>default</th></tr>' + ''.join(rows) + '</table>'

_ref = ['<section class="page"><h2 id="reference">every device, every parameter</h2>',
        '<p>Written from the devices themselves (<code>sndstudioTest --dump-params</code>), so names, ranges and defaults are exactly what the sliders show. '
        'The suite\'s engines (bd1 … ms0) are described control by control in <b>faltung Manual.pdf</b>; here is what each one offers inside the studio. '
        'Every parameter takes locks, lanes, modulators, macros, midi and keys. In <i>faltung deep sndstudio</i> the list is the same except: sb1 stabs in place of wb1, and no ms0 '
        '(the deep master chain is filter → glue). Deep-only device:</p>']
for _d in _PD['devices']:
    if _d['type'] not in {x['type'] for x in _P['devices']}: _ref.append(_devtable(_d))
for _d in _P['devices']: _ref.append(_devtable(_d))
_ref.append('</section>')
add(''.join(_ref))

add('''<section class="page"><h2 id="blackhole">into another program (BlackHole)</h2>
<p>sndstudio has no plug-in versions: to record its tracks separately in another program, the channels travel through a virtual audio device:</p>
<ol><li>Install BlackHole (<code>brew install --cask blackhole-64ch</code>, or the 128-channel build from existential.audio). It appears as an audio device with that many inputs and outputs.</li>
<li>In sndstudio: <b>audio</b> → output device <i>BlackHole 64ch</i> (or 128ch), the same sample rate as Live, all output channels enabled.</li>
<li><b>mix → multi-out → assign all outputs</b>. The master leaves on 1 … w (its width), then every track and return on its own channels in order; the strips show which.</li>
<li>In Live: <i>Settings → Audio</i>, input device <i>BlackHole</i>, <i>Input Config</i>: enable the pairs (or mono inputs, for wide tracks) you need. Make an audio track per pair, <i>Audio From: Ext. In</i>, the pair, <i>Monitor: In</i> to hear, arm, record.</li>
<li>To hear sndstudio while doing this, listen through Live (its output on your speakers or interface). Keep both apps at one sample rate; do not let either change the device's rate while the other runs.</li>
<li>If the mastering happens in Live, switch on <b>master chain bypass</b> in the mixer.</li></ol>
<p>A 16- or 64-channel track (spatial work) uses consecutive channels; make a Live track per channel, or record them as one multichannel file in a host that takes them.</p>

<h2 id="install">installing</h2>
<table class="params"><tr><th>what</th><th>macOS</th></tr>
<tr><td class="n">app</td><td>/Applications/faltung/faltung sndstudio.app (deep: /Applications/faltung deep/faltung deep sndstudio.app)</td></tr></table>
<p>With Homebrew: <code>brew tap femifleming/tap &amp;&amp; brew install --cask femifleming/tap/faltung</code> (deep: <code>…/faltung-deep</code>); or from the release zips
(Windows: the .exe; Linux: the program). sndstudio is an application only; older installs of its plug-in versions can be deleted. Projects (<code>.fstudio</code>) and their audio folders go to <i>Documents/faltung sndstudio</i> by default; settings and the plug-in scan list live in the
user's application data folder.</p></section>''')


add('''<section class="page"><h2 id="daw">a new kind of daw: what it does for production</h2>
<p class="lead">sndstudio is built from the techniques of the artists it was shaped around, not after other daws. What a finished production needs is all here; what is
only for playing live, and the piano roll, are left out on purpose.</p>
<h3>undo and redo</h3><p>Every edit is a step: steps, patterns, clips, lanes, devices, routes, a fader or a knob moved (a drag is one step), a recorded take. <b>cmd+z</b> goes back,
<b>cmd+shift+z</b> (or cmd+y) forward; the <i>edit</i> menu shows how many steps there are. The transport keeps playing while you undo. Opening a project or starting a new one starts a new history.</p>
<h3>autosave and crash recovery</h3><p>While there are unsaved changes the project is written to an autosave every minute (a whole file or nothing). If sndstudio did not close properly
last time, it offers the autosave at the next start; it opens unsaved, with the name of the project it came from: save it to keep it. <i>file → open the autosave</i> does the same by hand.
Quitting with unsaved changes asks whether to save.</p>
<h3>the cpu meter</h3><p>In the bottom right corner: the engine's load (the time a block takes against the time it lasts), a tick at the highest load since the last reset, and the number
of overruns (blocks that took longer than they last: the interface would drop out). Click it to reset. A bigger buffer in <b>audio</b> gives the engine more room.</p>
''' + shot('sndstudio-cpu', 'shot') + '''
<h3>recording notes into the steps</h3><p>Arm an instrument track (<b>rec</b> on its strip) and press <b>rec</b> in the header (or <b>r</b>). From a stop, the <b>count-in</b> clicks one or two bars first
(the link next to the pages cycles none / 1 / 2) and the transport starts on the downbeat, to the sample. What you play on a midi keyboard goes into the pattern that is playing: each note on
the nearest step, its pitch counted from the track's root (or its nearest degree, with a scale), its velocity, how early or late you played it as the step's micro-timing, and how long you held it
as its length. The pattern loops, so every pass can add notes (overdub); with the loop and <b>punch</b> on, only inside the loop. A take is one undo step. <b>click</b> switches the metronome on for
playing and recording.</p>
<h3>your plug-ins</h3><p><b>browse → scan plug-ins</b> finds the VST3 and Audio Unit plug-ins in the usual folders; each is opened in a process of its own, so one that crashes is blacklisted and the
scan goes on. Instruments go on an instrument track, effects on any chain — tracks, returns, the master, at any width (an encoder 2 → 16, a decoder 16 → 24). <b>window</b> opens the plug-in's own
editor. Its parameters take automation lanes, modulators, macros and step locks like any other; its state is saved with the project; its latency is compensated with everything else.
Tested with Quell, ConvoSolution, faltung ms0 (9106 samples of latency) and EX Poly FM as an instrument.</p>
<h3>export</h3><p>See <a href="#export">export and resampling</a>: the mix, stems, the tracks you choose, a time selection, multichannel files, wav / aiff / flac, 16 / 24 / 32-bit float, any common rate,
normalised, dithered.</p>
<h3>multi-out</h3><p>Every track, return and the master can leave on its own outputs of the interface (up to 128 channels) — to a desk, or through a loopback device into another program.
See <a href="#modes">produce and multi-out</a>.</p></section>''')

add('''<section class="page"><h2 id="notes">notes without a piano roll</h2>
<p class="lead">Notes are steps. These are the ways to make them, each from a way of working the studio was shaped around.</p>
<h3>the pitch lane, scales and tunings</h3><p>Under the step grid in <b>seq</b>: every step's pitch as a mark. Click in the lane to set a step's pitch; drag across it to draw a contour —
each step you pass takes the height under the mouse, snapped to the track's scale. <b>scale</b> (track settings on the left): <i>semitones</i> (as before), thirteen built-in scales (major,
minor and the modes, harmonic minor, the pentatonics, whole tone, just major in 5-limit ratios, a 7-limit scale after the well-tuned piano, the harmonic series 8–15), or <i>a scala file</i>:
any <code>.scl</code> tuning — the archives of tunings, Colundi sets among them — with its own period. With a scale, a step's pitch is a degree (0 the root, the scale's size an octave up,
negative below). Degrees between the keys reach the instrument as a note and a pitch bend (±2 semitones; <b>tone</b>, the <b>sampler</b>, wb1 and most plug-ins follow it; one bend per track
at a time, so chords on a microtonal step share it). Notes played in while recording land on their nearest degree.</p>
<h3>typing a pattern</h3><p><b>type</b> (second row of tools): one word per step. A number is a note (a degree with a scale, semitones without), <b>x</b> the root, <b>.</b> or <b>-</b> a rest,
<b>_</b> holds the note before one step longer. After a note: <b>!</b> accent, <b>~</b> soft, <b>?</b> half the chance, <b>*3</b> a ratchet of three, <b>'</b> an octave up, <b>,</b> an octave down.
<code>0 . 2 _ 4! . 7? . 0' . 4~ 2*3</code> is twelve steps.</p>
<h3>additive and subtractive figures</h3><p><b>additive</b>: the pattern shown is the cell. <i>additive</i> writes its figures into the next patterns — the first note, then the first two, the first
three … the whole cell — and chains them; <i>subtractive</i> goes from the whole cell down to one note; <i>both</i> grows and shrinks. <i>repeats</i> plays each figure 1–8 times. A note's
unit is the note and the rests after it, so the figures keep the cell's rhythm.</p>
<h3>parts out of phase</h3><p><b>phase</b> (track settings): this part runs a little faster or slower (±5%) than the others, so the same pattern on two tracks drifts apart bar by bar
and, much later, comes back together.</p>
<h3>chance, euclidean and ratio rhythms, mutate</h3><p>Every step has a <b>chance</b> (alt-click a step, or the slider) and a <b>condition</b> (1:2 … 8:8, fill, first, prev); the track a
<b>probability</b> on top. <b>euclid</b> spreads hits evenly (hits over the length, rotated); <b>ratios</b> gives every step its own time (1 1 2/3 2/3 2/3 3/2); <b>accelerate</b>, <b>strict</b>;
<b>mutate</b> nudges a few steps at a time, <b>evolve</b> lets the pattern drift on every pass and <b>freeze</b> keeps what it drifted to.</p>
<h3>sieves and clouds</h3><p><b>sieve</b> takes the steps from residue classes (<code>3@0 | 4@1</code>, &amp;, !), <b>sieve pitch</b> the pitches, <b>cloud</b> scatters events with exponential gaps and a
pitch range: stochastic note events after Xenakis.</p>
<h3>micro-loops</h3><p>The <b>sampler</b>'s <i>micro-loop</i>: from where the note starts, a sliver of the sample (in ms, down to a few hundredths, or a 16th to a 256th note at the tempo) repeats
for as long as the note lasts — a click becomes a pulse, a fragment of a record a tone. Its <i>cutoff</i> and <i>resonance</i> and a dub delay on a send do the rest.</p>
''' + shot('sndstudio-2', 'shot') + '''</section>''')

# ---- the artist appendix: one how-to per artist
_artists = [
 ('Ricardo Villalobos', 'Long recorded jams, cut afterwards; odd meters by polymeter; a console colour on everything.',
  'Set tracks to different lengths (a 20-step perc against a 16-step kick) and some to <i>clock free</i>. Put <b>colour</b> on the tracks and the master. Press <b>capture</b> and play scenes, mutes and sends for ten minutes; click capture again, then cut the take on the arrangement. Watch the bar · minute readout for the length.'),
 ('[a:rpia:r] (Rhadoo, Petre Inspirescu, Raresh)', 'An infinite groove, 7–8 minutes, percussion played through its parameters.',
  'Start from <i>long form</i>. Lock pitch and decay per step on rm3 / pc7 / cl8 and set <i>slide</i> on some steps; map a slow <i>slewed random</i> to the hats\' decay. Send to <b>throw</b> and rv5. Drop acoustic fragments on audio tracks in <i>repitch</i> or <i>stretch</i>.'),
 ('Basic Channel / Maurizio', 'The mixer is the instrument: dub sends thrown and muted by hand.',
  'On the stab track (wb1 / sb1) set <i>chord</i> to minor 7 or minor 9 on its steps. Put <b>throw</b> and <b>spring</b> on returns, <b>noise bed</b> on the master. Right-click the throw send → <i>play it from a key: while held</i>; switch <b>write</b> on and play the throws: they become automation.'),
 ('Dubtil', 'Hardware sequenced from the studio, vocal snippets, versions as scenes.',
  'Send midi clock to your hardware (the app\'s menu bar: <i>midi clock out</i>). Load vocal snippets into a <b>sampler</b> and add <b>grain</b>; make a scene per version.'),
 ('Autechre', 'Generative patterns that drift, rules that rewrite, modulators on modulators.',
  'Use <i>euclid</i>, <i>markov</i>, <i>rewrite</i>, <i>mutate</i>; set <i>evolve</i> to 20–40% so the pattern drifts, and <i>freeze</i> what you like. Give steps conditions (1:3, not prev). Map an lfo onto another modulator\'s rate. Use <b>repeat</b> on a send and fm perc for the hits.'),
 ('Mark Fell / snd', 'Time per step, ratio series, accelerating loops, strict velocity and length.',
  'Type a series into <i>ratios</i> (1 1 2/3 2/3 2/3 3/2) or press <i>accelerate</i> 0.9. Press <i>strict</i> 2 0.5. Use <b>fm perc</b> and lock its ratios per step. For snd\'s two-elements discipline, build scenes that keep two tracks playing and stop the rest.'),
 ('Grischa Lichtenberger', 'Signal paths that feed back; gates as instruments.',
  'Route a track into another that routes back: accept the <i>feedback route</i>. Use the gate of <b>strip</b> hard (high range, fast release) as a shaping tool; resample the result with <b>bounce</b>.'),
 ('Second Woman', 'Clocks off the grid, ratchets that bend.',
  'Set a track to <i>clock free</i> with a step ms that does not divide the tempo; give steps ratchets 4–8 with a <i>ratchet curve</i>; automate the tempo; <b>repeat</b> with pitch, <b>clip</b> on the master, bd1 tuned to 30 hz.'),
 ('Ryoji Ikeda', 'Pure tones, clicks and data at exact frequencies and durations.',
  'A <b>tone</b> track (sine at an exact hz, 10 ms) and a <b>data</b> track; speed x4 or x8, or the ms clock. Export 32-bit float without dither. For installation work set the master width up to 64.'),
 ('Noisia', 'Basses resampled and resampled again; multiband distortion.',
  'wb1 with a reese or neuro preset, <b>multiband</b> after it, <b>sidechain</b> keyed from the kick. <b>bounce</b> the bass into a sampler (sliced) and sequence the slices; ms0 multiband on the master.'),
 ('Current Value', 'Half-time drum & bass, modular voices.',
  'Start from <i>bass first (half time, 172)</i>; <b>transient</b> on the drums, <b>sidechain</b> with the master as key; host your modular-style plug-ins as devices.'),
 ('Shigeto', 'Live drums and recordings sliced into steps.',
  'Record onto an audio track (arm, rec). <b>bounce</b> it into a sampler (sliced on transients) and address slices per step with pitch and decay locks; gl8 on the drum route; export stems.'),
 ('Four Tet', 'Audio moved by ear, off the grid.',
  'Snap off; drag clips anywhere; <b>,</b> and <b>.</b> nudge by 5 ms; split and trim; audition files in the browser before dropping them; per-track swing; ms0 auto master at the end.'),
 ('Prefuse 73', 'Vocal cut-ups, chokes, stutters.',
  'Put sampler tracks for the cut-up in one <i>choke group</i>; pitch and slice per step; mutes from held keys; <b>repeat</b> for stutters; record a turntable through an audio input.'),
 ('Lapalux', 'Warped tape, drifting pitch.',
  '<b>vinyl</b> and <b>throw</b> on the track; a slow <i>slewed random</i> or drift lfo on pitch; resample to audio and split / duplicate clips for stutters.'),
 ('Todd Edwards', 'Syllables chopped, pitched and stacked.',
  'A sampler in slices mode on a vocal; one slice per step with pitch; <i>chord</i> on some steps; swing from sw6 or the per-track swing; start from <i>two-step (garage, 132)</i>.'),
 ('Oval (Markus Popp)', 'Skipping CDs: fragments that repeat and jump; tiny files placed in time.',
  'Put <b>skip</b> on a track with a sample; raise <i>intensity</i>; fragments on the grid or in ms; steps address places in the file. Loop and split audio clips on the timeline; micro-loops in the <b>sampler</b> for the smallest fragments. Your openprocess / openfield, if installed, are in the device list.'),
 ('Gábor Lázár', 'Bursts that accelerate, pulse widths and pitches locked per hit.',
  'Ratchets with a strong <i>ratchet curve</i>, <i>accelerate</i> on the pattern, locks with <i>slide</i> on <b>fm perc</b> and <b>tone</b> (pulse width, frequency); speed x8.'),
 ('Kindohm (Mike Hodnick)', 'Live-coded patterns: degrade, every nth, polymeter.',
  'Euclid, rotate, reverse; conditions 1:2 … 8:8 as "every"; chance per step and per track as "degrade"; different lengths per track; chopped breaks via bounce → sampler. Or drive the selected track with midi from your live-coding tool.'),
 ('Peter Blasser', 'Chaotic cross-modulation, touched nodes.',
  'Map <b>bounce</b> to a control (four never-repeating ramps); map an lfo onto that modulator\'s rate; route tracks into each other through feedback routes; hold a control open from a key like a touched plate.'),
 ('Iannis Xenakis', 'Sieves, stochastic clouds, drawn glissandi, sound in space.',
  'Use <i>sieve</i> for rhythm and <i>sieve pitch</i> for the scale (3@0 | 4@1, &amp;, !); <i>cloud</i> for stochastic textures; shift-drag freehand arcs onto <b>tone</b>\'s frequency (UPIC-style glissandi); automate an <b>encoder</b>\'s azimuth and elevation, or use <b>line</b>.'),
 ('Bernhard Leitner', 'Sound lines travelling through a space of speakers.',
  'Make a wide track (16 or 24 channels), put <b>line</b> on it: pass time, figure, legato, the speaker sieve. The user\'s Leitner plug-in, if installed, is in the device list and feeds a 16-channel track.'),
 ('Alex Chechile', 'Difference tones made in the ear.',
  '<b>tone</b> with <i>second tone</i> a difference (e.g. 40 hz) or a ratio (1.22) away, <i>one tone per channel</i> at 100%, quiet levels, on headphones or two speakers.'),
 ('La Monte Young', 'Drones in just intonation.',
  '<b>tone</b> held by the note\'s <i>gate</i>, <i>tuning</i> just (7-limit) or the harmonic series, key tracking on, chords on a long step for sine chords.'),
 ('William Basinski, Ian William Craig, Amulets', 'Loops that decay as they repeat, left to run for a long time.',
  '<b>wear</b> on the track (loop length on the grid or in seconds), <b>vinyl</b> and <b>colour</b> for wow, flutter and hiss, or your own tape plug-ins on the chain; layer looped audio clips on the timeline, a long arrangement, and a reverb on a return to fill the gaps.'),
 ('Venetian Snares', 'Breaks cut to pieces in odd meters, rolls that speed up and climb.',
  'Load a break into the <b>sampler</b> in slices mode; one slice per step, with ratchets up to 8 and a <i>ratchet curve</i> for rolls; lock the sampler\'s pitch per step and <i>slide</i> between steps for a climbing roll. Odd meters by pattern length: 14 steps is 7/8, 22 steps 11/8, or a ratio series; tracks of different lengths against each other.'),
 ('Fennesz', 'A guitar through the laptop: smeared, broken, washed in reverb.',
  'An audio track with your interface\'s guitar input, <i>monitor in</i>, armed; <b>grain</b> and <b>clip</b> (bits and rate) on its chain, or your own granular and reverb plug-ins (Cloudfin, ConvoSolution); a long reverb on a return with the send automated; record takes and cut them on the timeline.'),
 ('Philip Glass', 'Additive and subtractive cells, arpeggios, parts out of phase.',
  'Type a cell with <b>type</b> (e.g. <code>0 2 4 2</code>), then <b>additive</b> → <i>both</i>, repeats 2–4: the figures grow and shrink as a chain. Put the same cell on a second track with <b>phase</b> +1% so it drifts against the first. A scale on the pitch lane keeps every figure in the key.'),
 ('Aleksi Perälä', 'Colundi tunings, colourful sequences.',
  'Load a Colundi set (or any tuning) as a <b>scala file</b> in the track\'s <i>scale</i>; draw contours on the pitch lane — every step lands on a degree of the tuning; <b>tone</b> or the <b>sampler</b> (or your plug-in synths, which follow the pitch bend) as the instrument; <b>mutate</b> and <b>evolve</b> to keep the sequence moving.'),
 ('Boards of Canada', 'Detuned, warm, lo-fi, nostalgic.',
  '<b>vinyl</b> (wow and flutter detune the whole track), <b>colour</b> in tape mode, <b>clip</b> for fewer bits and a lower rate, a slow <i>slewed random</i> on a synth\'s pitch or filter; your own chorus and tape plug-ins on the chain. A just or harmonic-series scale on the pitch lane for the slightly-off intervals.'),
 ('Jan Jelinek', 'Micro-loops of jazz records and crackle, dub filtering.',
  'Put tiny fragments into the <b>sampler</b> and set its <i>micro-loop</i> to a 64th or 128th note (or a few ms): clicks become pulses; the sampler\'s cutoff and resonance, locked per step; <b>throw</b> or dl4 on a send for the dub; a <b>noise bed</b> for the crackle.'),
 ('Loefah, Skream, Rusko, Jakes, Billain', 'Half-step wobble and sub.',
  'Start from <i>half-step (140)</i>; wb1\'s wobble on the grid or free, its vowel filter, sub; <b>multiband</b>; bounce the bass to a sampler and resequence it.'),
]
add('<section class="page"><h2 id="artists">to work like …</h2><p>One how-to per artist the studio was shaped around. The reasons and sources are in <code>docs/daw-research.md</code>. '
    'No device, preset or template carries an artist\'s name: these are ways of using the studio, not imitations.</p>' +
    ''.join(f'<h3>{e(n)}</h3><p><i>{e(w)}</i> {h}</p>' for n, w, h in _artists) + '</section>')

if __name__ == '__main__':
    page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>sndstudio manual</title><style>{CSS} h2,h3{{break-after:avoid}} tr{{break-inside:avoid}}</style></head><body>{"".join(doc)}</body></html>'
    open(os.path.join(HERE, 'sndstudio.html'), 'w').write(page)
    chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
    pdf = os.path.join(HERE, 'sndstudio Manual.pdf')
    subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                    f'--print-to-pdf={pdf}', 'file://' + os.path.join(HERE, 'sndstudio.html')], check=True, capture_output=True)
    print(pdf)

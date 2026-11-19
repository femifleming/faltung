#!/usr/bin/env python3
"""Builds the convosolution manual: manual.html (A4, the faltung manual look) → "convosolution Manual.pdf" via Chrome.
Pictures come from the test tool, run from this folder:  <build>/convosolution/ECRTest_artefacts/Release/ECRTest --screenshots img
The fonts are the bundle's (Resources/fonts at the top of the faltung repository)."""
import html, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUNDLE = os.path.abspath(os.path.join(HERE, '..', '..', '..'))      # the faltung repository
FONTS = os.path.join(BUNDLE, 'Resources', 'fonts')
VERSION = '1.1.0'
GROUND, INK, ACCENT = '#6e80c8', '#1d1c15', '#f6c590'                 # the plug-in's field, its ink, the peach of its chips
e = html.escape

# ------------------------------------------------------------------------------------------------
#  words
# ------------------------------------------------------------------------------------------------
KNOBS = [  # (name, shift name, range, what it does)
 ('width', 'pitch', '0.00 – 2.00 (centre 1.00)',
  'mid/side width of this convolver\'s output, after the convolution, so a mono input can still be widened. 0 is mono, 1 leaves the response as it is, 2 doubles the sides. '
  'Hold shift: <b>pitch</b>, −2 … +2 octaves (centre = off): the response is resampled, so it plays slower and longer when lowered, faster and shorter when raised. A pitched response is capped at 30 s.'),
 ('crossfade', 'slew', '100/0 – 0/100',
  'balance between convolver A and convolver B (only when <i>convolver b</i> is on in the system menu; otherwise you hear A alone). '
  'Hold shift: <b>slew time</b>, 1 ms – 2 s: how long the sound takes to cross from the old response to the new one whenever position, attack, release, pitch or the response itself changes. The old response keeps ringing out underneath, so nothing is ever cut.'),
 ('predelay', 'hpf', '0 – 2.000 s',
  'a delay before this convolver, so the reverb arrives after the dry sound. Changing it crossfades between two read points rather than bending pitch. '
  'Hold shift: <b>hpf cutoff</b>, off / 10 Hz – 2 kHz: a 12 dB/oct high-pass on the way into the convolver, to keep the low end of the room out.'),
 ('position', 'input level', '0.000 – 1.000',
  'where in the response the convolution starts: 0 is the beginning, 1 the end. Move it into the tail of a long hall for a reverb without the early reflections, or into the middle of a recording to use only a slice of it. '
  'Hold shift: <b>input level</b>, −100 … 0 dB: the level into both convolvers and the recorder.'),
 ('dry', None, '−100 … 0 dB', 'level of the unprocessed signal at the output.'),
 ('wet', None, '−100 … 0 dB', 'level of the convolved signal (the A/B mix) at the output.'),
 ('attack', None, '0 – 23 s', 'a fade-in drawn over the response from the position onwards. 0 is instant. It removes the attack of a hit or softens the early reflections of a room.'),
 ('release', None, '0 – 23 s, or full', 'how long the response plays after the attack before fading out. At the top (<i>full</i>) the whole remaining response is used. Shorten it for a gated room, or to use the first second of a long tail.'),
]
GLOBAL = [
 ('cv pos', '−5 … +5 V', 'the module\'s position CV input, as a host parameter: it adds to the position of the mapped convolver(s). Automate it, or route a MIDI controller to it in the host.'),
 ('cv x', '−5 … +5 V', 'adds to the crossfade.'),
 ('cv width', '−5 … +5 V', 'adds to the stereo width of the mapped convolver(s).'),
 ('cv aux', '−5 … +5 V', 'goes wherever the <i>aux cv</i> setting sends it: wet level, pitch (tune) or input level.'),
]
SHAPES = [
 ('sine', 'a sine wave at the rate.'), ('tri', 'triangle.'), ('saw', 'a rising ramp that drops at the end of each cycle.'), ('square', 'high for half the cycle, low for the other half.'),
 ('random', 'sample and hold: a new random level at the start of every cycle.'), ('drift', 'random levels with a smooth glide between them (one glide per cycle).'),
 ('lorenz', 'the Lorenz attractor, run so that one orbit of a lobe takes about a cycle. It never repeats and flips between its two lobes when it feels like it.'),
 ('rosslr', 'the Rössler attractor: slow, mostly circular, with a sudden lift now and then.'),
 ('boid', 'a small flock of boids. The output is the position of one bird in the flock: it wanders, swerves and settles in a way no oscillator does.'),
]
DESTS = [
 ('pos', 'position', 'of the mapped convolver(s). Rebuilds the response as it moves, so it steps at the slew time.'),
 ('wid', 'stereo width', 'of the mapped convolver(s).'), ('pit', 'pitch', 'of the mapped convolver(s), ±1 octave at 100 %. Rebuilds the response.'),
 ('xfd', 'crossfade', 'between A and B.'), ('pre', 'predelay', 'of the mapped convolver(s).'), ('hpf', 'hpf cutoff', 'of the mapped convolver(s).'),
 ('wet', 'wet level', ''), ('dry', 'dry level', ''), ('atk', 'attack', 'of the mapped convolver(s). Rebuilds the response.'),
 ('rel', 'release', 'of the mapped convolver(s). Rebuilds the response.'), ('inl', 'input level', ''),
]
MENU = [
 ('direct shift', 'on / off', 'on: the shift link stays latched until you click it again. Off (the default): shift clears itself after one shifted action, the way the module does it.'),
 ('file chooser', 'on / off', 'on: the first step of browsing only opens the list of responses; further steps move through it. Off: every step loads the next response at once.'),
 ('auto exit', 'on / off', 'on: the list closes by itself four seconds after you stop browsing.'),
 ('cv map', 'A / B / AB', 'which convolver the CV inputs and the modulation matrix\'s per-convolver destinations (position, width, pitch, predelay, hpf, attack, release) act on.'),
 ('aux cv', 'wet / tune / inl', 'what the aux CV does: wet level, the pitch of the mapped convolver(s) (tune), or the input level.'),
 ('screensaver', 'off / 30 s / 2 m / 5 m', 'the module\'s display would dim after this long without any activity; the plug-in keeps the setting for the module\'s sake and stores it with the others.'),
 ('convolver b', 'on / off', 'off: convolver B is not computed and the crossfade does nothing, which halves the CPU load. On: both run and the crossfade mixes them.'),
 ('autogain', 'on / off', 'on: a fast limiter on the wet sum keeps it under full scale (a white <i>clip</i> chip in the picture shows when it is working). Off: the sum is left alone and the chip only warns.'),
 ('exit', '', 'back to the main screen.'),
]
INTERNAL = ['bottledungeon_48k', 'falkland_palace_48k', 'hamilton_mausoleum_48k', 'lady_chapel_48k', 'middle_tunnel_48k', 'r1_omni_48k', 'terrys_warehouse_48k', 'york_minster_48k']
RECIPES = [
 ('the tail only', 'position 0.4, attack 0, release full on a long hall: the reverb without its early reflections, which sits behind a dry drum bus without smearing the hits.'),
 ('gated room', 'a room response, release 0.3 s, attack 0: a gated reverb. Modulate release from an LFO near the tempo (there is no sync: set the rate by ear) for a gate that breathes.'),
 ('reverse', 'attack 2 s, release 0.1 s, position 0: the response swells in and stops, a reverse-reverb without a reverse.'),
 ('a drone from anything', 'record 4 s of a chord (record, then record again), pitch −1 octave, position in the middle, release full. Feed it a click track: every click plays the chord as a reverb.'),
 ('two rooms, one knob', 'A a small room, B a cathedral, convolver b on, LFO 1 drift at 0.05 Hz into xfd +60 %: the space breathes between the two.'),
 ('lorenz wander', 'LFO 2 lorenz, 0.2 Hz, into pit −30 % and pos +40 % on a recording of a voice: a slow, never-repeating granular smear. Slew time long (1 s) for smooth steps, short (30 ms) for stutter.'),
 ('boid flock on the width', 'LFO 3 boid at 0.5 Hz into wid +100 % and pre +20 %: the stereo image flutters like the flock.'),
]

# ------------------------------------------------------------------------------------------------
#  html
# ------------------------------------------------------------------------------------------------
def img(name, caption, w=100):
    return f'<figure style="width:{w}%"><img src="img/{name}.png"><figcaption>{caption}</figcaption></figure>'

def table(head, rows):
    h = ''.join(f'<th>{c}</th>' for c in head)
    b = ''.join('<tr>' + ''.join(f'<td>{c}</td>' for c in r) + '</tr>' for r in rows)
    return f'<table><thead><tr>{h}</tr></thead><tbody>{b}</tbody></table>'

parts = []
P = parts.append
P(f'''
<div class="cover">
  <div class="chips"><span></span><span></span><span></span></div>
  <h1>convosolution</h1>
  <p class="sub">convolution reverb · part of faltung</p>
  <p class="meta">manual · version {VERSION} · vst3 · vst2 · au · lv2 · app · macos · windows · linux</p>
  <figure><img src="img/main.png"><figcaption>fig. 1 · the window, knobs tab</figcaption></figure>
</div>
''')

P('''<h2>what it is</h2>
<p>convosolution is a convolution reverb: it takes a recording of how a space (or a device, or anything at all) answers to a click, the <i>impulse response</i>, and
plays your sound through it, so your sound comes out as if it had happened there. It has <b>two true-stereo convolvers, A and B</b>, each with its own response and its own
treatment, and a crossfade between them. The signal path, the knobs, the buttons and the system menu follow the <b>Tasty Chips ECR+</b> Eurorack convolver, with the
things a Eurorack module cannot have: three LFOs with a modulation matrix, a response library of 140 spaces, host automation of the CV inputs, and the faltung look:
one flat field, the response as a wireframe, chips and slider rows. This project is not affiliated with Tasty Chips Electronics.</p>
<p>Each convolver is <b>true stereo</b>: a four-channel response (l&gt;l, r&gt;r, l&gt;r, r&gt;l) is used as four convolutions; a stereo response as two; a mono one for both sides.
Every response can be started from any point, pitched two octaves either way, shaped with an attack and a release, delayed, high-passed and widened. Everything that
rebuilds the response happens on a helper thread, and the old response rings out underneath the new one, so you can browse and turn knobs while it plays and never
hear a cut. The plug-in adds <b>no latency</b>.</p>
<p>You can also <b>record</b> up to 23 seconds of whatever goes into the plug-in and use that as a response, store it in the library, and save a patch with every response.</p>

<h2>the window</h2>
<p>One slate-blue field, 560 × 372 at its smallest (drag a corner to make it bigger: everything scales as one picture). On the left the picture, the module's display;
on the right the buttons as text links, two tabs, and the rows. Names and values are lowercase mono text; a link that is lit, a selected row or the current tab sits on a
small peach parallelogram, the <i>chip</i> every faltung plug-in uses.</p>
''')
P(table(['part', 'what it does'], [
 ('<b>the picture</b> (fig. 1)', 'The response of the convolver in the foreground as a thin wire shell: one row per channel (l&gt;l and r&gt;r, plus l&gt;r and r&gt;l for a true-stereo file), the outline of each channel on ribs with struts between the rows, and the attack-release window drawn over it as a white face from the position. The knob you last touched is on the top line, the response on the bottom line, <b>a</b> or <b>b</b> in the corner. <b>Drag up or down, or scroll</b> on it to browse responses; <b>click</b> it to switch between convolver A and B; <b>press and hold</b> it to read the patch saved with the current response. Shift + click opens the system menu.'),
 ('<b>shift · record · store · play</b>', 'The module\'s four buttons. A lit button is a link on a chip.'),
 ('<b>&lt; prev · next &gt;</b>', 'Browse responses one at a time (the Select encoder). With shift held they jump a whole folder.'),
 ('<b>select a / select b</b>', 'Switch the foreground convolver: the picture, the per-convolver knob rows and the browse links all follow it. Hold shift and click it to open the system menu.'),
 ('<b>folder...</b>', 'The response folder: where it is, show it in the Finder/Explorer, choose another one, rescan it.'),
 ('<b>knobs · modulation</b>', 'Two tabs; the current one sits on a chip. Knobs is the module\'s panel as slider rows; modulation is the three LFOs and the matrix, with live figures of what they are doing.'),
 ('<b>a … b …</b>', 'The footer: the response loaded in each convolver, the foreground one in white.'),
]))
P('''<h3>links</h3>''')
P(table(['link', 'click', 'shift + click'], [
 ('<b>shift</b>', 'latches shift, so the next thing you move or click gets its second function (the first four knob rows change to their second names, in white, while it is on; the keyboard Shift key does the same for as long as it is held). With <i>direct shift</i> off it clears itself after one use.', ''),
 ('<b>record</b>', 'starts recording the input into the foreground convolver, and stops it (or it stops by itself at 23 s). While recording only the dry signal is heard, the link blinks and the picture says <i>recording</i>. The recording becomes the convolver\'s response straight away, unsaved: a new, unnamed file called rec…wav.', ''),
 ('<b>store</b>', 'writes the unsaved recording of the foreground convolver into the response folder as a 32-bit float WAV, and rescans, so it appears in the list.', 'stores the <b>presets</b>: the global knobs, the system settings and which response sits in A and B go to the settings file, and the knob settings of each convolver go into an .ini next to its response (or into Patches/ for the built-in ones). A response with a patch carries a small chip before its name in the list.'),
 ('<b>play</b>', 'auditions the foreground response on its own, dry, at the output; click again to stop. While it is on, every response you browse to plays as you land on it.', 'auditions the response <i>processed</i>: an impulse is fired through the convolver so you hear it with position, attack, release, pitch, predelay, HPF and width applied.'),
]))

P('''<h3>knobs</h3>
<p>Each row is a knob in the faltung way: the name, a thin rule with a slanted peach bar from zero to the value and a white edge at the value, and the value as text.
<b>Drag the bar sideways</b>, <b>scroll</b> on it, <b>double-click</b> to reset, hold <b>Cmd/Ctrl</b> for fine moves. The first four rows have a second function that you reach
with shift; their names change while it is on. The rows for width, pitch, predelay, HPF, position, attack and release belong to the convolver in the foreground (A or B); dry, wet,
crossfade, slew and input level are global. The picture reads back the exact value of whatever you touch.</p>''')
P(table(['knob', 'shift', 'range', 'what it does'], [(f'<b>{n}</b>', f'<i>{s}</i>' if s else '', f'<span class="nw">{r}</span>', w) for n, s, r, w in KNOBS]))
P('''<p>The host sees every one of these as a parameter: <b>A Position … A Pitch</b>, <b>B Position … B Pitch</b>, <b>Dry, Wet, Crossfade, Slew Time, Input Level</b>, plus the four CV inputs:</p>''')
P(table(['parameter', 'range', 'what it does'], [(f'<b>{n}</b>', r, w) for n, r, w in GLOBAL]))
P('''<p>Position, attack, release and pitch <b>rebuild</b> the response when they change (the pots are read in 1024 steps, as on the module), and the sound crosses to the
rebuilt response over the slew time, so these four move in steps of about twice the slew time rather than continuously. Predelay, HPF, width and all the levels
move at once.</p>''')

P('''<h2>responses</h2>
<p>A response is any audio file: <b>WAV, AIFF or FLAC</b>; mono, stereo, four-channel true stereo (l&gt;l, r&gt;r, l&gt;r, r&gt;l), or a true-stereo pair of files called
<i>Name L</i> and <i>Name R</i>, which is listed once. Every file is level-matched on loading so that browsing does not jump in volume. Any sample rate: a response is
resampled to the host's rate (and to the pitch) when it is built.</p>
<p>Eight responses are <b>built in</b> (from the OpenAIR library of the University of York):</p>
<p class="mono">''' + ' · '.join(INTERNAL) + '''</p>
<p>After those comes everything in <b>Music/ConvoSolution</b> in your home folder and in its sub-folders, one level deep, each sub-folder listed as a folder in the chooser.
<b>ConvoSolution-IRs.zip</b> from the faltung releases page is the full library, 140 spaces in folders, unpacked straight into that folder.
A shared copy can live in the system's application-data folder under ConvoSolution/IRs, and the environment variable <span class="mono">CONVOSOLUTION_IR_ROOT</span> overrides the
location. <b>folder...</b> shows the folder, lets you choose another one (remembered) or rescans it after you drop files in. Sources and licences of every
file are in <span class="mono">IR-CREDITS.txt</span> in the download.</p>''')
P(img('chooser', 'fig. 2 · the list of responses, after a step of browsing. The folder is the heading, the selected response sits on a chip, and a small chip before a name means a patch is stored with it.', 80))
P('''<h3>browsing</h3>
<p>Drag or scroll on the picture, or click <b>&lt; prev</b> and <b>next &gt;</b>. With <i>file chooser</i> on (the default) the first step opens the list and the next ones move
through it, loading each response as you land on it; click the picture (or select) to close the list, or let <i>auto exit</i> close it. With shift, a step jumps to the start of
the next folder. A response that cannot be read leaves the current one in place.</p>

<h3>recording your own</h3>
<ol>
<li>Pick the convolver (A or B) you want the recording in, and send sound into the plug-in: a clap in a room, a chord, a drum loop, anything.</li>
<li>Click <b>record</b>. The convolvers rest, you hear the dry input, the picture counts the seconds. Click <b>record</b> again to stop (it stops by itself at 23 s).</li>
<li>The recording is the convolver's response at once. Play through it; set position, attack and release to taste.</li>
<li>Click <b>store</b> to keep it: it is written into the response folder as rec<i>nnnnnn</i>.wav and listed with the others. Rename it in the folder if you like and <b>rescan</b>.</li>
</ol>

<h3>patches and presets</h3>
<p><b>Shift + store</b> saves two things: the knob settings of each convolver into an .ini file next to its response (so that response comes back the same way every time
you press and hold the picture, or when the plug-in starts), and a settings file with the global knobs, the system menu and the responses in A and B, which the plug-in
reads when it starts up. The host's own preset system still saves and recalls the whole state as usual.</p>''')

P('''<h2>modulation</h2>''')
P(img('mod', 'fig. 3 · the modulation tab. Three figures draw the LFOs; the fourth is the response.'))
P('''<p>Three LFOs, each with a <b>shape</b> (click the name for the list, or scroll on it), a <b>rate</b> (the bar, 0.01 – 20 Hz, scroll or drag) and a live <b>figure</b> of what it is doing in
thin peach lines: the plain shapes as a scrolling trace, lorenz and rosslr as their attractors, boid as the flock itself, with the bird you hear in white. The white mark
under each figure, and the white tick at the end of each row, is the LFO's output right now. LFOs 1, 2 and 3 start as sine, lorenz and boid.</p>''')
P(table(['shape', 'what it does'], [(f'<b>{n}</b>', w) for n, w in SHAPES]))
P('''<h3>the matrix</h3>
<p>Three rows (l1, l2, l3) by eleven destinations, each cell a chip. <b>Drag a cell up or down</b> to set how much of that LFO goes to that destination, −100 … +100 %: a positive
amount fills the chip upward from its middle in peach, a negative one downward in a darker peach; <b>double-click</b> clears a cell. The amounts from all three LFOs are summed per
destination and added to the knob, in knob units, so +100 % moves a knob across its whole range. The per-convolver destinations follow the <i>cv map</i> setting, like the CV
inputs do: A, B, or both.</p>''')
P(table(['cell', 'destination', 'notes'], [(f'<b>{s}</b>', d, n) for s, d, n in DESTS]))
P('''<p>Every matrix cell and every LFO shape and rate is also a host parameter (<b>LFO 1 Shape</b>, <b>LFO 1 Rate</b>, <b>LFO 1 &gt; Pos</b> … <b>LFO 3 &gt; In lvl</b>), so the host
can automate the modulation itself. Destinations that rebuild the response (position, pitch, attack, release) step at the slew time; set a long slew time for a smooth
wander and a short one for a stutter.</p>''')

P('''<h2>the system menu</h2>
<p>Shift + click the picture, or shift + click <b>select</b>. Browse with prev / next (or scroll), click select to change the item, exit (or shift + select) to leave. The settings are
part of the plug-in's state and go into the settings file with shift + store.</p>''')
P(img('menu', 'fig. 4 · the system menu', 80))
P(table(['item', 'values', 'what it does'], [(f'<b>{n}</b>', v, w) for n, v, w in MENU]))

P('''<h2>installing it</h2>
<p>convosolution ships in the <b>faltung</b> bundle and is built, released and installed with it.</p>''')
P(table(['', ''], [
 ('<b>macOS</b> (11 or later, Apple silicon and Intel)', '<span class="mono">brew install --cask femifleming/tap/faltung</span>, or download <span class="mono">faltung-macOS.zip</span> from the releases page: VST3 → <span class="mono">~/Library/Audio/Plug-Ins/VST3</span>, VST → <span class="mono">~/Library/Audio/Plug-Ins/VST</span>, the AU component → <span class="mono">~/Library/Audio/Plug-Ins/Components</span>, the app anywhere. The build is not notarised: if macOS refuses it, run <span class="mono">xattr -dr com.apple.quarantine</span> on each copy once.'),
 ('<b>Windows</b> (10 or 11, 64-bit)', 'download <span class="mono">faltung-Windows.zip</span>. VST3 → <span class="mono">C:\\Program Files\\Common Files\\VST3</span>, the VST2 .dll → your VST2 folder, the .exe anywhere.'),
 ('<b>Linux</b> (x86_64)', 'download <span class="mono">faltung-Linux.tar.gz</span>. VST3 → <span class="mono">~/.vst3</span>, LV2 → <span class="mono">~/.lv2</span>, VST2 .so → <span class="mono">~/.vst</span>, the binary anywhere on your PATH.'),
 ('<b>responses</b>', 'download <span class="mono">ConvoSolution-IRs.zip</span> from the same release page and unpack it so that its folders sit inside <span class="mono">Music/ConvoSolution</span> in your home folder. Eight responses are built in, so the plug-in works without it.'),
]))
P('''<p>Releases: <b>github.com/femifleming/faltung/releases</b>. Every download has this manual, the licence and the response credits (<span class="mono">IR-CREDITS.txt</span>).
Formats: VST3, VST2 (built with the open FST headers), AU on macOS, LV2 on Linux, and a standalone app on all three. The plug-in has a stereo (or mono) input and a stereo output.</p>
<p>To build it yourself, from the top of the faltung repository: <span class="mono">cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release &amp;&amp; cmake --build build --target ConvoSolution_All ECRTest</span>.
<span class="mono">ECRTest</span> checks the plug-in offline and writes the pictures in this manual (<span class="mono">--screenshots</span>);
<span class="mono">ECREngineTest</span> checks the convolution engine against a direct convolution.</p>

<h2>using it</h2>
<p>Put it on a send or straight on a track. Pick a response (drag on the picture), set <b>wet</b> to taste, and leave <b>dry</b> at 0 dB on an insert or at −100 dB on a send.
Turn <b>position</b> up until the early reflections are gone if the room smears the attack; add <b>predelay</b> for the same reason; shift + predelay for the <b>HPF</b> if
the low end gets muddy. Switch <b>convolver b</b> on in the system menu when you want two spaces at once or a crossfade between them; leave it off to save CPU.</p>''')
P(table(['recipe', ''], [(f'<b>{n}</b>', w) for n, w in RECIPES]))
P('''<h3>the picture</h3>
<p>The picture is what the module's 128 × 128 display would show, drawn in the faltung look. <b>a</b> or <b>b</b> in the top right corner is the foreground convolver; a white
<i>clip</i> chip next to it means the wet sum went over full scale, or that autogain is pulling it down; <i>recording</i> blinks in the middle while a recording runs. A message on the
top line names the knob you last moved and its value; the bottom line is the response. Pop-ups (stored, read patch..., no recording to store) come and go on a chip in the middle.</p>

<h2>licences</h2>
<p>Code: AGPL-3.0 (it links JUCE 9). Third-party: PFFFT (FFTPACK licence), FST headers (GPL-3), the Inter and IBM Plex Mono fonts (OFL), the 5×7 pixel font from EX-GF. Impulse responses:
CC BY 4.0 (OpenAIR, University of York) and CC BY-SA 4.0 (the HybridReverb2 database). The built-in set and the response pack list every source in IR-CREDITS.txt.
The ECR+ is a product of Tasty Chips Electronics; convosolution is not affiliated with them.</p>
<p class="small">Everything that was asked for while this was designed and built is in REQUESTS.md at the top of the faltung repository.</p>
''')

css = f'''
@font-face {{ font-family: Inter; src: url('file://{FONTS}/Inter-Regular.ttf'); font-weight: 400; }}
@font-face {{ font-family: Inter; src: url('file://{FONTS}/Inter-Medium.ttf'); font-weight: 600; }}
@font-face {{ font-family: Plex; src: url('file://{FONTS}/IBMPlexMono-Regular.ttf'); font-weight: 400; }}
@font-face {{ font-family: Plex; src: url('file://{FONTS}/IBMPlexMono-Medium.ttf'); font-weight: 600; }}
@page {{ size: A4; margin: 16mm 16mm 18mm 16mm; }}
@page :first {{ margin: 0; }}
html {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
body {{ font: 9.8pt/1.5 Inter, Helvetica, sans-serif; color: {INK}; background: #fff; margin: 0; }}
.cover {{ background: {GROUND}; color: {INK}; box-sizing: border-box; padding: 36mm 20mm 10mm; margin: 0; height: 296mm; overflow: hidden; page-break-after: always; }}
.cover .chips {{ margin: 0 0 10mm 2mm; }}
.cover .chips span {{ display: inline-block; width: 18pt; height: 9pt; margin-right: 4pt; background: {ACCENT}; transform: skewX(-28deg); }}
.cover .chips span:nth-child(2) {{ background: #fff; }}
.cover h1 {{ font: 600 40pt Inter; color: #fff; margin: 0 0 2mm; letter-spacing: -.01em; }}
.cover .sub {{ font: 11pt Plex; margin: 0 0 1mm; }}
.cover .meta {{ font: 8.5pt Plex; margin: 0 0 14mm; opacity: .75; }}
.cover figure {{ margin: 0; width: 100%; }}
.cover figure img {{ border: 2px solid #fff; display: block; width: 100%; }}
.cover figcaption {{ color: {INK}; }}
h1, h2, h3 {{ font-weight: 600; }}
h2 {{ font: 600 11pt Plex; text-transform: lowercase; border-bottom: .6pt solid {INK}; padding-bottom: 2pt; margin: 8mm 0 3mm; page-break-after: avoid; }}
h2:before {{ content: ""; display: inline-block; width: 16pt; height: 8pt; margin-right: 7pt; background: {GROUND}; transform: skewX(-28deg); }}
h2:first-of-type {{ margin-top: 0; }}
h3 {{ font: 600 9.8pt Plex; text-transform: lowercase; margin: 5mm 0 2mm; page-break-after: avoid; }}
p {{ margin: 0 0 7pt; }}
i {{ font-style: normal; font-family: Plex; font-size: 9pt; }}
.mono {{ font-family: Plex; font-size: 9pt; }}
.nw {{ white-space: nowrap; }}
p.small {{ font: 8.5pt Plex; color: #6b6a62; margin-top: 6mm; }}
table {{ border-collapse: collapse; width: 100%; margin: 2pt 0 8pt; font-size: 9.2pt; }}
th {{ text-align: left; font: 600 8.5pt Plex; text-transform: lowercase; padding: 2pt 6pt 3pt 0; border-bottom: .6pt solid {INK}; }}
td {{ padding: 3pt 8pt 4pt 0; vertical-align: top; border-bottom: .4pt solid rgba(29,28,21,.18); }}
tr {{ page-break-inside: avoid; }}
figure {{ margin: 2pt auto 8pt; page-break-inside: avoid; }}
figure img {{ width: 100%; display: block; }}
figcaption {{ font: 8.5pt Plex; margin-top: 4px; color: #6b6a62; }}
ol {{ margin: 0 0 8pt; padding-left: 16pt; }}
ol li {{ margin-bottom: 3pt; }}
'''
page = f"<!doctype html><html lang='en'><head><meta charset='utf-8'><title>convosolution manual</title><style>{css}</style></head><body>{''.join(parts)}</body></html>"
out_html = os.path.join(HERE, 'manual.html')
open(out_html, 'w', encoding='utf-8').write(page)
pdf = os.path.join(HERE, 'convosolution Manual.pdf')
chrome = next((c for c in ('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', shutil.which('google-chrome'), shutil.which('chromium')) if c and os.path.exists(c)), None)
if chrome is None: sys.exit('Chrome not found: open manual.html and print it to PDF by hand')
subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files', f'--print-to-pdf={pdf}', 'file://' + out_html], check=True, capture_output=True)
print('wrote', pdf)

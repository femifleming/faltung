#!/usr/bin/env python3
"""Builds the convosolution manual: manual.html (A4, a nod to the plug-in's 1996 web page) → "convosolution Manual.pdf" via Chrome.
Pictures come from the test tool, run from this folder:  ../../build/ECRTest_artefacts/Release/ECRTest --screenshots img"""
import html, os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
FONTS = os.path.join(ROOT, 'Resources', 'fonts')
VERSION = '1.0.1'
e = html.escape

# ------------------------------------------------------------------------------------------------
#  words
# ------------------------------------------------------------------------------------------------
KNOBS = [  # (name, shift name, range, what it does)
 ('stereo width', 'pitch', '0.00 – 2.00 (centre 1.00)',
  'mid/side width of this convolver\'s output, after the convolution, so a mono input can still be widened. 0 is mono, 1 leaves the response as it is, 2 doubles the sides. '
  'Hold shift: <b>pitch</b>, −2 … +2 octaves (centre = off): the response is resampled, so it plays slower and longer when lowered, faster and shorter when raised. A pitched response is capped at 30 s.'),
 ('crossfade', 'slew time', '100/0 – 0/100',
  'balance between convolver A and convolver B (only when <i>convolver b</i> is on in the system menu; otherwise you hear A alone). '
  'Hold shift: <b>slew time</b>, 1 ms – 2 s: how long the sound takes to cross from the old response to the new one whenever position, attack, release, pitch or the response itself changes. The old response keeps ringing out underneath, so nothing is ever cut.'),
 ('predelay', 'hpf cutoff', '0 – 2.000 s',
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
 ('screensaver', 'off / 30 s / 2 m / 5 m', 'the display dims after this long without any activity, and goes black five seconds later. Touch anything to wake it.'),
 ('convolver b', 'on / off', 'off: convolver B is not computed and the crossfade does nothing, which halves the CPU load. On: both run and the crossfade mixes them.'),
 ('autogain', 'on / off', 'on: a fast limiter on the wet sum keeps it under full scale (a C on the display, or CLIP on the page, shows when it is working). Off: the sum is left alone and CLIP only warns.'),
 ('exit', '', 'back to the main screen.'),
]
INTERNAL = ['BOTTLEDUNGEON_48K', 'FALKLAND_PALACE_48K', 'HAMILTON_MAUSOLEUM_48K', 'LADY_CHAPEL_48K', 'MIDDLE_TUNNEL_48K', 'R1_OMNI_48K', 'TERRYS_WAREHOUSE_48K', 'YORK_MINSTER_48K']
RECIPES = [
 ('the tail only', 'position 0.4, attack 0, release full on a long hall: the reverb without its early reflections, which sits behind a dry drum bus without smearing the hits.'),
 ('gated room', 'a room response, release 0.3 s, attack 0: a gated reverb. Modulate release from an LFO near the tempo (there is no sync: set the rate by ear) for a gate that breathes.'),
 ('reverse', 'attack 2 s, release 0.1 s, position 0: the response swells in and stops, a reverse-reverb without a reverse.'),
 ('a drone from anything', 'record 4 s of a chord (Record, then Record again), pitch −1 octave, position in the middle, release full. Feed it a click track: every click plays the chord as a reverb.'),
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
  <div class="rule"></div>
  <h1>convosolution</h1>
  <p class="sub">a dual convolution reverb that looks like a web page from 1996</p>
  <p class="meta">manual · version {VERSION} · VST3 · VST2 · AU · LV2 · app · macOS · Windows · Linux</p>
  <div class="rule"></div>
  <figure><img src="img/main.png"><figcaption>Fig. 1: the page, knobs tab</figcaption></figure>
  <p class="small">This page is best viewed at 800x600.</p>
</div>
''')

P('''<h2>what it is</h2>
<p>convosolution is a convolution reverb: it takes a recording of how a space (or a device, or anything at all) answers to a click, the <i>impulse response</i>, and
plays your sound through it, so your sound comes out as if it had happened there. It has <b>two true-stereo convolvers, A and B</b>, each with its own response and its own
treatment, and a crossfade between them. The signal path, the knobs, the buttons and the system menu follow the <b>Tasty Chips ECR+</b> Eurorack convolver, with the
things a Eurorack module cannot have: three LFOs with a modulation matrix, a response library of 140 spaces, host automation of the CV inputs, and a page drawn the
way the web looked in 1996. This project is not affiliated with Tasty Chips Electronics.</p>
<p>Each convolver is <b>true stereo</b>: a four-channel response (L&gt;L, R&gt;R, L&gt;R, R&gt;L) is used as four convolutions; a stereo response as two; a mono one for both sides.
Every response can be started from any point, pitched two octaves either way, shaped with an attack and a release, delayed, high-passed and widened. Everything that
rebuilds the response happens on a helper thread, and the old response rings out underneath the new one, so you can browse and turn knobs while it plays and never
hear a cut. The plug-in adds <b>no latency</b>.</p>
<p>You can also <b>record</b> up to 23 seconds of whatever goes into the plug-in and use that as a response, store it in the library, and save a patch with every response.</p>

<h2>the page</h2>
<p>The window is one web page. Everything on it is a link, a table row or a picture; there is nothing to drag but the bars in the tables and the cells of the matrix.</p>
''')
P(table(['part', 'what it does'], [
 ('<b>the picture</b> (Fig. 1)', 'The module\'s display. On the main screen it shows the response of the convolver in the foreground (A in blue, B in orange), one row per channel, with the attack-release envelope drawn over it in blue from the position, the knob you last touched on the top line and the name of the response on the bottom. <b>Drag sideways or scroll</b> on it to browse responses; <b>click</b> it to switch between convolver A and B; <b>press and hold</b> it to read the patch saved with the current response. Shift + click opens the system menu.'),
 ('<b>[ Shift | Record | Store | Play ]</b>', 'The module\'s four buttons. A lit link is a lit button.'),
 ('<b>&lt;&lt; Prev | Next &gt;&gt;</b>', 'Browse responses one at a time (the Select encoder). With shift held they jump a whole folder.'),
 ('<b>Select A / Select B</b>', 'Switch the foreground convolver: the picture, the per-convolver knob rows and the browse links all follow it. Hold shift and click it to open the system menu.'),
 ('<b>Folder...</b>', 'The response folder: where it is, show it in the Finder/Explorer, choose another one, rescan it.'),
 ('<b>Knobs | Modulation</b>', 'Two tabs. Knobs is the module\'s panel as a table; Modulation is the three LFOs and the matrix, with live figures of what they are doing.'),
 ('<b>A: … B: …</b>', 'The response loaded in each convolver.'),
]))
P('''<h3>links</h3>''')
P(table(['link', 'click', 'shift + click'], [
 ('<b>Shift</b>', 'latches shift, so the next thing you move or click gets its second function (the knob rows change their names to the italic ones while it is on, and the keyboard Shift key does the same for as long as it is held). With <i>direct shift</i> off it clears itself after one use.', ''),
 ('<b>Record</b>', 'starts recording the input into the foreground convolver, and stops it (or it stops by itself at 23 s). While recording only the dry signal is heard and the picture says RECORDING. The recording becomes the convolver\'s response straight away, unsaved: a new, unnamed file called rec…wav.', ''),
 ('<b>Store</b>', 'writes the unsaved recording of the foreground convolver into the response folder as a 32-bit float WAV, and rescans, so it appears in the list.', 'stores the <b>presets</b>: the global knobs, the system settings and which response sits in A and B go to the settings file, and the knob settings of each convolver go into an .ini next to its response (or into Patches/ for the built-in ones). A response with a patch is listed in blue.'),
 ('<b>Play</b>', 'auditions the foreground response on its own, dry, at the output; click again to stop. While it is on, every response you browse to plays as you land on it.', 'auditions the response <i>processed</i>: an impulse is fired through the convolver so you hear it with position, attack, release, pitch, predelay, HPF and width applied.'),
]))

P('''<h3>knobs</h3>
<p>Each row is a knob: <b>drag the bar sideways</b>, <b>scroll</b> on it, <b>double-click</b> to reset, hold <b>Cmd/Ctrl</b> for fine moves. The first four rows have a second function
in italics that you reach with shift. The rows for width, pitch, predelay, HPF, position, attack and release belong to the convolver in the foreground (A or B); dry, wet,
crossfade, slew and input level are global. The display reads back the exact value of whatever you touch.</p>''')
P(table(['knob', 'shift', 'range', 'what it does'], [(f'<b>{n}</b>', f'<i>{s}</i>' if s else '', r, w) for n, s, r, w in KNOBS]))
P('''<p>The host sees every one of these as a parameter: <b>A Position … A Pitch</b>, <b>B Position … B Pitch</b>, <b>Dry, Wet, Crossfade, Slew Time, Input Level</b>, plus the four CV inputs:</p>''')
P(table(['parameter', 'range', 'what it does'], [(f'<b>{n}</b>', r, w) for n, r, w in GLOBAL]))
P('''<p>Position, attack, release and pitch <b>rebuild</b> the response when they change (the pots are read in 1024 steps, as on the module), and the sound crosses to the
rebuilt response over the slew time, so these four move in steps of about twice the slew time rather than continuously. Predelay, HPF, width and all the levels
move at once.</p>''')

P('''<h2>responses</h2>
<p>A response is any audio file: <b>WAV, AIFF or FLAC</b>; mono, stereo, four-channel true stereo (L&gt;L, R&gt;R, L&gt;R, R&gt;L), or a true-stereo pair of files called
<i>Name L</i> and <i>Name R</i>, which is listed once. Every file is level-matched on loading so that browsing does not jump in volume. Any sample rate: a response is
resampled to the host's rate (and to the pitch) when it is built.</p>
<p>Eight responses are <b>built in</b> (from the OpenAIR library of the University of York):</p>
<p class="mono">''' + ' · '.join(INTERNAL) + '''</p>
<p>After those comes everything in <b>Music/ConvoSolution</b> in your home folder and in its sub-folders, one level deep, each sub-folder listed as a folder in the chooser.
<b>ConvoSolution-IRs.zip</b> from the releases page is the full library, 140 spaces in folders, unpacked straight into that folder.
A shared copy can live in the system's application-data folder under ConvoSolution/IRs, and the environment variable <span class="mono">CONVOSOLUTION_IR_ROOT</span> overrides the
location. <b>Folder...</b> on the page shows the folder, lets you choose another one (remembered) or rescans it after you drop files in. Sources and licences of every
file are in <span class="mono">IR-CREDITS.txt</span> in the download.</p>''')
P(img('chooser', 'Fig. 2: the list of responses, after a step of browsing. The folder is the heading; blue names have a patch stored with them.', 80))
P('''<h3>browsing</h3>
<p>Drag or scroll on the picture, or click <b>&lt;&lt; Prev</b> and <b>Next &gt;&gt;</b>. With <i>file chooser</i> on (the default) the first step opens the list and the next ones move
through it, loading each response as you land on it; click the picture (or Select) to close the list, or let <i>auto exit</i> close it. With shift, a step jumps to the start of
the next folder. A response that cannot be read leaves the current one in place.</p>

<h3>recording your own</h3>
<ol>
<li>Pick the convolver (A or B) you want the recording in, and send sound into the plug-in: a clap in a room, a chord, a drum loop, anything.</li>
<li>Click <b>Record</b>. The convolvers rest, you hear the dry input, the display counts the seconds. Click <b>Record</b> again to stop (it stops by itself at 23 s).</li>
<li>The recording is the convolver's response at once. Play through it; set position, attack and release to taste.</li>
<li>Click <b>Store</b> to keep it: it is written into the response folder as rec<i>nnnnnn</i>.wav and listed with the others. Rename it in the folder if you like and <b>Rescan</b>.</li>
</ol>

<h3>patches and presets</h3>
<p><b>Shift + Store</b> saves two things: the knob settings of each convolver into an .ini file next to its response (so that response comes back the same way every time
you press and hold the picture, or when the plug-in starts), and a settings file with the global knobs, the system menu and the responses in A and B, which the plug-in
reads when it starts up. The host's own preset system still saves and recalls the whole state as usual.</p>''')

P('''<h2>modulation</h2>''')
P(img('mod', 'Fig. 3: the modulation tab. Figures 1–3 draw each LFO; the fourth figure is the response.'))
P('''<p>Three LFOs, each with a <b>shape</b> (the menu), a <b>rate</b> (the bar, 0.01 – 20 Hz, scroll or drag) and a live <b>figure</b> of what it is doing: the plain shapes as a
scrolling trace, lorenz and rosslr as their attractors seen from above, boid as the flock itself. The red mark under each figure is the LFO's output right now.
LFOs 1, 2 and 3 start as sine, lorenz and boid.</p>''')
P(table(['shape', 'what it does'], [(f'<b>{n}</b>', w) for n, w in SHAPES]))
P('''<h3>the matrix</h3>
<p>Three rows (L1, L2, L3) by eleven destinations. <b>Drag a cell up or down</b> to set how much of that LFO goes to that destination, −100 … +100 %; blue is positive, red is
negative; <b>double-click</b> clears a cell. The amounts from all three LFOs are summed per destination and added to the knob, in knob units, so +100 % moves a knob across
its whole range. The per-convolver destinations follow the <i>cv map</i> setting, like the CV inputs do: A, B, or both.</p>''')
P(table(['cell', 'destination', 'notes'], [(f'<b>{s}</b>', d, n) for s, d, n in DESTS]))
P('''<p>Every matrix cell and every LFO shape and rate is also a host parameter (<b>LFO 1 Shape</b>, <b>LFO 1 Rate</b>, <b>LFO 1 &gt; Pos</b> … <b>LFO 3 &gt; In lvl</b>), so the host
can automate the modulation itself. Destinations that rebuild the response (position, pitch, attack, release) step at the slew time; set a long slew time for a smooth
wander and a short one for a stutter.</p>''')

P('''<h2>the system menu</h2>
<p>Shift + click the picture, or shift + click <b>Select</b>. Browse with Prev / Next (or scroll), click Select to change the item, Exit (or shift + Select) to leave. The settings are
part of the plug-in's state and go into the settings file with shift + Store.</p>''')
P(img('menu', 'Fig. 4: the system menu', 80))
P(table(['item', 'values', 'what it does'], [(f'<b>{n}</b>', v, w) for n, v, w in MENU]))

P('''<h2>installing it</h2>''')
P(table(['', ''], [
 ('<b>macOS</b> (11 or later, Apple silicon and Intel)', 'download <span class="mono">ConvoSolution-macOS.zip</span> from the releases page. VST3 → <span class="mono">~/Library/Audio/Plug-Ins/VST3</span>, VST → <span class="mono">~/Library/Audio/Plug-Ins/VST</span>, the AU component → <span class="mono">~/Library/Audio/Plug-Ins/Components</span>, the app anywhere. The build is not notarised: if macOS refuses it, run <span class="mono">xattr -dr com.apple.quarantine</span> on each copy once (the exact lines are in INSTALL.txt).'),
 ('<b>Windows</b> (10 or 11, 64-bit)', 'download <span class="mono">ConvoSolution-Windows.zip</span>. VST3 → <span class="mono">C:\\Program Files\\Common Files\\VST3</span>, the VST2 .dll → your VST2 folder, the .exe anywhere.'),
 ('<b>Linux</b> (x86_64)', 'download <span class="mono">ConvoSolution-Linux.tar.gz</span>. VST3 → <span class="mono">~/.vst3</span>, LV2 → <span class="mono">~/.lv2</span>, VST2 .so → <span class="mono">~/.vst</span>, the binary anywhere on your PATH.'),
 ('<b>Responses</b>', 'download <span class="mono">ConvoSolution-IRs.zip</span> from the same release and unpack it so that its folders sit inside <span class="mono">Music/ConvoSolution</span> in your home folder. Eight responses are built in, so the plug-in works without it.'),
]))
P('''<p>Releases: <b>github.com/femifleming/convosolution/releases</b>. Every download has <span class="mono">INSTALL.txt</span>, this manual, the licence and the response credits.
Formats: VST3, VST2 (built with the open FST headers), AU on macOS, LV2 on Linux, and a standalone app on all three. The plug-in has a stereo (or mono) input and a stereo output.
convosolution is not in the Homebrew tap yet; the zip is the way in.</p>
<p>To build it yourself: <span class="mono">git clone --recursive https://github.com/femifleming/convosolution</span>, then <span class="mono">cmake -S convosolution -B build</span> and
<span class="mono">cmake --build build --config Release</span>. <span class="mono">ECRTest</span> checks the plug-in offline and writes the pictures in this manual
(<span class="mono">--screenshots</span>); <span class="mono">ECREngineTest</span> checks the convolution engine against a direct convolution.</p>

<h2>using it</h2>
<p>Put it on a send or straight on a track. Pick a response (drag on the picture), set <b>wet</b> to taste, and leave <b>dry</b> at 0 dB on an insert or at −100 dB on a send.
Turn <b>position</b> up until the early reflections are gone if the room smears the attack; add <b>predelay</b> for the same reason; shift + predelay for the <b>HPF</b> if
the low end gets muddy. Switch <b>convolver b</b> on in the system menu when you want two spaces at once or a crossfade between them; leave it off to save CPU.</p>''')
P(table(['recipe', ''], [(f'<b>{n}</b>', w) for n, w in RECIPES]))
P('''<h3>the display and the page</h3>
<p>The picture is the module's 128 × 128 display, drawn the way the page draws things. <b>A</b> or <b>B</b> in the corner is the foreground convolver; <b>C</b> (CLIP on the page)
means the wet sum went over full scale, or that autogain is pulling it down; a red square means recording. A message on the top line names the knob you last moved
and its value; the bottom line is the response. Pop-ups (STORED, READ PATCH..., NO RECORDING TO STORE) come and go on their own.</p>

<h2>licences</h2>
<p>Code: AGPL-3.0 (it links JUCE 9). Third-party: PFFFT (FFTPACK licence), FST headers (GPL-3), Liberation fonts (OFL), the 5×7 pixel font from EX-GF. Impulse responses:
CC BY 4.0 (OpenAIR, University of York) and CC BY-SA 4.0 (the HybridReverb2 database). The built-in set and the response pack list every source in IR-CREDITS.txt.
The ECR+ is a product of Tasty Chips Electronics; convosolution is not affiliated with them.</p>
<p class="small">Everything that was asked for while this was designed and built is in REQUESTS.md in the repository.</p>
''')

css = f'''
@font-face {{ font-family: 'Lib Serif'; src: url('file://{FONTS}/LiberationSerif-Regular.ttf'); }}
@font-face {{ font-family: 'Lib Serif'; src: url('file://{FONTS}/LiberationSerif-Bold.ttf'); font-weight: bold; }}
@font-face {{ font-family: 'Lib Serif'; src: url('file://{FONTS}/LiberationSerif-Italic.ttf'); font-style: italic; }}
@page {{ size: A4; margin: 17mm 16mm 19mm 16mm; }}
body {{ font-family: 'Lib Serif', 'Times New Roman', Times, serif; color: #000; background: #fff; font-size: 10.6pt; line-height: 1.45; margin: 0; }}
.cover {{ background: #c0c0c0; padding: 14mm 12mm 8mm; margin: -17mm -16mm 10mm; min-height: 240mm; page-break-after: always; }}
.cover .rule {{ border-top: 1px solid #808080; border-bottom: 1px solid #fff; margin: 6mm 0; }}
.cover h1 {{ font-size: 44pt; margin: 4mm 0 0; font-weight: bold; letter-spacing: -.01em; text-align: center; }}
.cover .sub {{ font-size: 15pt; font-style: italic; margin: 1mm 0 2mm; text-align: center; }}
.cover .meta {{ font-size: 9.5pt; text-align: center; margin: 0; }}
.cover figure {{ margin: 8mm auto 0; width: 92%; }}
.cover figure img {{ border: 2px inset #e0e0e0; background: #c0c0c0; }}
h1, h2, h3 {{ font-family: 'Lib Serif', serif; font-weight: bold; }}
h2 {{ font-size: 17pt; margin: 1.8em 0 .5em; padding-top: .4em; border-top: 1px solid #808080; box-shadow: 0 -1px 0 #fff; page-break-after: avoid; }}
h2:first-of-type {{ margin-top: 0; }}
h3 {{ font-size: 12.5pt; margin: 1.3em 0 .4em; page-break-after: avoid; }}
p {{ margin: .45em 0 .7em; }}
.mono {{ font-family: Menlo, 'Courier New', monospace; font-size: 9pt; }}
p.small {{ font-size: 9pt; font-style: italic; text-align: center; color: #333; }}
table {{ border-collapse: separate; border-spacing: 2px; width: 100%; margin: .6em 0 1em; font-size: 9.6pt; background: #c0c0c0; border: 2px outset #e8e8e8; }}
th {{ text-align: left; font-family: 'Lib Serif', serif; font-weight: bold; background: #c0c0c0; padding: 3px 6px; border: 1px inset #e8e8e8; }}
td {{ padding: 3px 6px; vertical-align: top; background: #fff; border: 1px inset #e8e8e8; }}
tr {{ page-break-inside: avoid; }}
i {{ color: #551a8b; font-style: italic; }}
figure {{ margin: .8em auto 1em; page-break-inside: avoid; }}
figure img {{ width: 100%; border: 2px inset #e0e0e0; display: block; }}
figcaption {{ font-style: italic; font-size: 9pt; margin-top: 4px; }}
ol li {{ margin-bottom: .3em; }}
'''
page = f"<!doctype html><html><head><meta charset='utf-8'><title>convosolution — manual</title><style>{css}</style></head><body>{''.join(parts)}</body></html>"
out_html = os.path.join(HERE, 'manual.html')
open(out_html, 'w', encoding='utf-8').write(page)
pdf = os.path.join(HERE, 'convosolution Manual.pdf')
chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', f'--print-to-pdf={pdf}', 'file://' + out_html], check=True, capture_output=True)
print('wrote', pdf)

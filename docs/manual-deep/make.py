#!/usr/bin/env python3
"""faltung deep manual → "faltung deep Manual.pdf". The dark mode of the faltung manual: the same machinery as
docs/manual/make.py (whose control texts, helpers and page style it reuses) on dark pages; parameter tables from
params.json (faltungDeepTest --dump-params), pictures from img/ (faltungDeepTest --screenshots)."""
import colorsys, html, json, os, re, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
MAIN = open(os.path.join(ROOT, 'docs', 'manual', 'make.py')).read()
P = {p['code']: p for p in json.load(open(os.path.join(HERE, 'params.json')))}
FONTS = '../../Resources/fonts'
e = html.escape

# the colours: each plug-in's field is the dark counterpart of its faltung twin's (ui::darkField / ui::darkAccent in Source/Ui.cpp)
def _hsl(c): r, g, b = (int(c[i:i + 2], 16) / 255 for i in (1, 3, 5)); h, l, s_ = colorsys.rgb_to_hls(r, g, b); return h, s_, l
def _hex(h, s_, l): r, g, b = colorsys.hls_to_rgb(h, l, s_); return '#%02x%02x%02x' % (round(r * 255), round(g * 255), round(b * 255))
def dark_field(c):
    h, s_, l = _hsl(c); chroma = s_ * (1 - abs(2 * l - 1))
    return _hex(h, min(0.62, 0.10 + 0.80 * chroma + (0.12 * s_ if l > 0.9 else 0)), 0.085 + 0.085 * min(1, chroma * 1.6))
def dark_accent(c):
    h, s_, l = _hsl(c); return _hex(h, min(1, s_ * 0.94), 0.47 + (l - 0.47) * 0.62)
g = globals()
exec (MAIN[MAIN.index('GROUND = {'):MAIN.index('e = html.escape')], g)      # faltung's light fields
LIGHTGROUND = dict(GROUND); LIGHTGROUND['sb1'] = LIGHTGROUND['wb1']
GROUND = {c: dark_field(v) for c, v in LIGHTGROUND.items()}
INK, PAGE = '#f6f6f0', '#15171a'

# reuse the shared descriptions and helpers of the faltung manual
exec (MAIN[MAIN.index('SLOT = {'):MAIN.index('INTRO = {')], g)
exec (MAIN[MAIN.index('def ptable'):MAIN.index('def shot(')], g)
exec (MAIN[MAIN.index('def presets'):MAIN.index('# ------------------------------------------------------------------------------------------------\n#  document')], g)   # presets, chevrons, wire, divider
exec (MAIN[MAIN.index('WBMOD = {'):MAIN.index("add(divider('wb1 wub'")], g)
exec (MAIN[MAIN.index('def loop_desc'):MAIN.index('add(f\'\'\'<section class="page" id="kt2"')], g)   # the step-feature text of kt2

def shot(code, cls='shot'):
    return f'<img class="{cls}" src="img/{code}.png" alt="{code}">'

doc = []
add = doc.append
VERSION = re.search(r"VERSION = '([^']+)'", MAIN).group(1)
ORDER = ['kt1', 'kt2', 'bd1', 'sd2', 'rm3', 'hh4', 'cp6', 'pc7', 'cl8', 'tm9', 'mc3', 'dl4', 'rv5', 'sw6', 'fl7', 'gl8', 'sb1']

def ddivider(code, title, sub, accent):
    """a divider page: the plug-in's dark field with the wireframe solid, as in the faltung manual"""
    return divider(title, sub, GROUND[code], wire(dark_accent(accent)), INK, '#ffffff')

# ---- cover ------------------------------------------------------------------------------------------------
add(f'''<section class="cover">{chevrons(dark_accent('#9fd3ec'))}
<svg class="cube" viewBox="0 0 104 112"><polygon points="0,30 52,0 104,30 52,60" fill="{dark_accent('#ffe15a')}"/><polygon points="0,30 52,60 52,112 0,82" fill="{dark_accent('#f2c100')}"/>
<polygon points="52,60 104,30 104,82 52,112" fill="{dark_accent('#d8a800')}"/><text x="52" y="35" text-anchor="middle" font-size="10" font-weight="600" fill="#2a2100" font-family="Inter">faltung deep</text></svg>
<div class="ct"><h1>faltung deep</h1><p class="sub">drums, processes and stabs for deep house · faltung's dark mode</p>
<p class="ver">manual · version {VERSION} · november 2026</p></div>
<div class="tl">''' + ''.join(f'<div><span class="chip" style="background:{GROUND[c]}"></span><span>01 : {i:02d} : {(i*17)%60:02d}</span><b>{c}</b><span>{e(P[c]["name"])}</span><span>{e(P[c]["blurb"])}</span></div>'
                      for i, c in enumerate(ORDER)) + '</div></section>')

add('''<section class="page"><h2>contents</h2><ol class="toc">
<li><a href="#intro">introduction</a></li><li><a href="#install">installing</a></li><li><a href="#using">the controls and the pictures</a></li>
<li><a href="#auto">automation and midi</a></li><li><a href="#kt1">kt1 kit</a></li><li><a href="#kt2">kt2 loop</a></li>
<li><a href="#modules">the drum modules</a></li><li><a href="#fx">the processes</a></li><li><a href="#sb1">sb1 stabs</a></li><li><a href="#sndstudio">sndstudio</a></li><li><a href="#recipes">recipes</a></li></ol>
<h2 id="intro">introduction</h2>
<p class="lead">faltung deep is the deep house twin of faltung: the same synthesis and processing engines, retuned and re-preset for
warm, swung, late-night house — round kicks, wooden rims, soft claps, shakers and congas, chord stabs and sub bass.</p>
<p>It has seventeen plug-ins: the drum machine <b>kt1 kit</b>, the slicer <b>kt2 loop</b>, eight single drum modules, six effects and
<b>sb1 stabs</b>, a synth that plays a whole chord from one key. faltung's mastering bus and Partials are not part of it — use them from
faltung itself. Both suites install side by side; every faltung deep plug-in is called <i>faltung deep …</i>.</p>
<p><b>The look is faltung's, in the dark.</b> Every faltung deep plug-in has the layout, the type and the shapes of its faltung twin — snd's
folded strips, Mark Fell's wireframe solids, Oval's coloured lines, stdio's chevrons and tracklists — on the dark counterpart of the
twin's colour field: mustard becomes a dark olive, the pale papers go near black with their tint kept, and the strips, lines and
fills keep their hues a little dimmer. (Up to version 1.1.0 faltung deep had a look of its own, a mirrored tree on a record sleeve; sounds,
parameters, presets and saved sessions are unchanged.)</p>
<p>ballonwinkel and ballonpaleis, faltung's twins of Pinkie Pie's Balloon Shop and Balloon Palace, come in the dark too, as
<b>ballonwinkel deep</b> and <b>ballonpaleis deep</b>: the same plug-ins on the dark counterparts of their colour fields, installed
with faltung deep. Their controls are described in the faltung manual.</p>
<table class="params"><tr><th>id</th><th>name</th><th>what it is</th></tr>''' +
''.join(f'<tr><td class="n">{c}</td><td>{e(P[c]["name"])}</td><td>{e(P[c]["blurb"])}</td></tr>' for c in ORDER) + '''</table></section>''')

add('''<section><h2 id="install">installing</h2>
<p>Each plug-in comes as VST3, Audio Unit and VST2 (VST3, LV2 and VST2 on Linux) and as a standalone app.</p>
<table class="params"><tr><th>format</th><th>where (macOS)</th></tr>
<tr><td class="n">VST3</td><td>~/Library/Audio/Plug-Ins/VST3/faltung deep kt1.vst3 …</td></tr>
<tr><td class="n">Audio Unit</td><td>~/Library/Audio/Plug-Ins/Components/faltung deep kt1.component …</td></tr>
<tr><td class="n">VST2</td><td>~/Library/Audio/Plug-Ins/VST/faltung deep kt1.vst …</td></tr>
<tr><td class="n">apps</td><td>/Applications/faltung deep/</td></tr></table>
<p>With Homebrew: <code>brew install --cask femifleming/tap/faltung-deep</code>. On Windows copy the <code>.vst3</code> folders to
<i>C:\\Program Files\\Common Files\\VST3</i>; on Linux to <i>~/.vst3</i> (LV2: <i>~/.lv2</i>). In Ableton Live, rescan plug-ins; they appear under
<b>faltung</b>. Every plug-in opens on its first deep house preset.</p>

<h2 id="using">the controls and the pictures</h2>
<table class="params"><tr><th>control</th><th>how</th></tr>
<tr><td class="n">slider</td><td>a label, a thin rule, a slanted bar and a white edge mark. Drag left/right (or up/down) anywhere on it. <b>shift</b>-drag for fine moves,
<b>double-click</b> for the default, scroll wheel for small steps. Bipolar controls (pan, shift, offsets) fill from the centre.</td></tr>
<tr><td class="n">choice</td><td>click for a menu; drag up/down to step through.</td></tr>
<tr><td class="n">toggle</td><td>the small parallelogram; filled = on.</td></tr>
<tr><td class="n">presets</td><td>top right: <b>&lt;</b> and <b>&gt;</b> step through, click the name for the list; also in the host's program menu.</td></tr>
<tr><td class="n">picture</td><td>in the modules, click the picture to play the sound (hh4: left half closed, right half open). In kt1 click a strip; in kt2 click a slice.</td></tr>
<tr><td class="n">size</td><td>drag the bottom-right corner; the window scales between half and double size.</td></tr></table>
<p>The pictures are drawn from the settings and move as you play: the strips of a module grow and shrink with its eight controls and
flash on every hit, the wireframe solid of bd1, tm9 and sb1 changes shape with the sound, dl4's arcs are its repeats, the chevrons of
rv5 and gl8 follow the room and the gain reduction. Hover over any control for its full name.</p>

<h2 id="auto">automation and midi</h2>
<p>Every control is a host parameter with a readable name; in kt1 the names start with the voice (<i>bd1 decay</i>, <i>sh6 level</i>).
The pattern (a1–a8), track mutes, lengths, probabilities and shifts are parameters too, so arrangements can be automated.</p>
<table class="params"><tr><th>plug-in</th><th>notes</th></tr>
<tr><td class="n">kt1</td><td>C1–G1 (36–43): kick, rim, clap, closed hat, open hat, shaker, conga, tom. The sequencer follows the host transport.</td></tr>
<tr><td class="n">kt2</td><td>C1 and up play slices 0, 1, 2 …</td></tr>
<tr><td class="n">modules</td><td>any note; with key tracking the pitch follows the key (C3 = as tuned). hh4: A# notes play the open hat.</td></tr>
<tr><td class="n">sb1</td><td>notes (one key = a chord when <i>chord</i> is set), pitch bend, mod wheel, aftertouch.</td></tr></table></section>''')

# ---- kt1 ----------------------------------------------------------------------------------------------
NAMES = ['Kick', 'Rim', 'Clap', 'Closed hat', 'Open hat', 'Shaker', 'Conga', 'Tom']
IDS = ['bd1', 'rm2', 'cp3', 'ch4', 'oh5', 'sh6', 'cg7', 'tm8']
ENG = ['kick', 'rim', 'clap', 'hat', 'hat', 'hat (noise)', 'perc (conga)', 'tom']
add(ddivider('kt1', 'drum machines', 'kt1 kit · kt2 loop', '#e7d873'))
def kt1_rows():
    rows = []
    for p in P['kt1']['params']:
        pid = p['id']
        if pid.startswith('v') and '_' in pid:
            if not pid.startswith('v3_'): continue
            key = pid.split('_', 1)[1]
            desc = SEQ.get(key) or COMMON.get(key) or (SLOT['clap'].get(key, '') + ' (for the clap; each voice names its own)')
            rows.append((p['name'].replace('cp3 ', '<voice> '), rng_of(p), p['def'], desc))
        else: rows.append((p['name'], rng_of(p), p['def'], SEQ.get(pid, '')))
    return rows
add(f'''<section class="page" id="kt1"><h2>kt1 kit</h2><p class="lead">A deep house drum machine: eight voices with their own engines, a
16-step sequencer with swing, microtiming, probability and ratchets, and a separate output per voice.</p>{shot('kt1')}
<h3>the page</h3><p>The folded strips at the top left are the voice selector: click a strip (or a row name in the grid) to select and
hear that voice; it flashes on every hit. On the right, the selected voice's model and eight sound controls, then its track settings. At the
bottom the step grid and the global row: run, internal bpm, swing, micro range, patterns a1–a8 and copy / paste / clear.</p>
<table class="params"><tr><th>voice</th><th>engine</th><th>midi</th><th>output</th></tr>''' +
''.join(f'<tr><td class="n">{IDS[v]} {NAMES[v].lower()}</td><td>{ENG[v]}</td><td>{["C1","C#1","D1","D#1","E1","F1","F#1","G1"][v]}</td><td>{v + 2}</td></tr>' for v in range(8)) +
'''</table><h3>editing steps</h3><table class="params">
<tr><td class="n">click</td><td>step on / off</td></tr><tr><td class="n">drag up / down</td><td>velocity</td></tr><tr><td class="n">drag across</td><td>paint</td></tr>
<tr><td class="n">shift-drag</td><td>microtiming — the step slides early or late</td></tr><tr><td class="n">alt-click</td><td>probability 100 → 75 → 50 → 25 %</td></tr>
<tr><td class="n">right-click</td><td>ratchet 1–4</td></tr>
<tr><td class="n">alt-drag up / down · alt-scroll</td><td>the ratchet's curve, out 7 … even … in 7 (see kt2)</td></tr>
<tr><td class="n">click a step number</td><td>the selected track's length ends there</td></tr></table>
<p>Grooves: <b>warehouse</b> (four to the floor, offbeat open hat, shaker 16ths, conga answers), <b>jazzy</b> (broken kick, busy hats, rim
syncopation), <b>minimal deep</b> (half-probability shaker, sparse rims), <b>broken</b> (off-grid kick). Shaker and conga sit a little late
in every factory pattern; push them further with the track <i>shift</i>.</p>''' + f'''<h3>controls</h3>{ptable(kt1_rows())}{presets('kt1')}</section>''')

# ---- kt2 ----------------------------------------------------------------------------------------------
add(f'''<section class="page" id="kt2"><h2>kt2 loop</h2><p class="lead">A loop slicer for chopped breaks and resampled grooves. It starts with
a bar of kt1's "warehouse" groove; drop any loop onto the waveform to replace it.</p>{shot('kt2')}
<p>Slices are cut on a grid or on the hits (<b>onsets</b>); each lit step shows the slice it plays — drag up/down to choose another,
alt-click to reverse, shift-drag (or shift-scroll) for microtiming, right-click for a ratchet of 1–4 hits. <b>fit</b> keeps the loop in time with the host tempo.</p>
{g["STEP_FEATURES"]}{shot('kt2-steps')}
<h3>controls</h3>{ptable(params_rows('kt2', lambda pid: LOOP.get(pid) or SEQ.get(pid, ''), g["loop_skip"], g["loop_name"]))}{presets('kt2')}</section>''')

# ---- modules ----------------------------------------------------------------------------------------------
add(ddivider('bd1', 'the drum modules', 'bd1 kick · sd2 snare · rm3 rim · hh4 hats · cp6 clap · pc7 perc · cl8 dust · tm9 tom', '#e3bf5f'))
add('''<section class="page" id="modules"><h2>the drum modules</h2><p class="lead">Each module is one drum engine as its own instrument, for drum
racks and midi clips. Four models and eight controls per engine; level, pan, velocity, key tracking and choke on all of them. Click the
picture to hear the sound.</p></section>''')
MODTXT = {
 'bd1': 'Deep house kicks are round and a little long. <b>deep round</b> is the thud model with a soft click and some sub; <b>lofi 909</b> drives the 909 shape; <b>long thud</b> for sparse tracks.',
 'sd2': 'Snares are rare; <b>brush</b> is the one you will use most — a soft noise swell under the clap.',
 'rm3': 'Wooden rims and sticks for the syncopation. <b>wooden rim</b> and <b>deep tok</b> sit well a few ms early (track shift in kt1).',
 'hh4': 'Hats and shakers. <b>shaker</b> is pure noise with no metal; play it on 16ths with low velocity on the offs. A# notes play the open hat.',
 'cp6': '<b>soft room clap</b>: five hands spread wide with a short room — the classic deep house clap, best a little late.',
 'pc7': 'Congas, bongos and a soft bell. With key tracking on they can be played as a small percussion kit across the keyboard.',
 'cl8': 'Called <b>dust</b> here: vinyl crackle, soft ticks and pops to sit under everything.',
 'tm9': 'Deep toms for fills and the low "blip" toms of dub-house.',
}
for code, t in [('bd1', 'kick'), ('sd2', 'snare'), ('rm3', 'rim'), ('hh4', 'hat'), ('cp6', 'clap'), ('pc7', 'perc'), ('cl8', 'click'), ('tm9', 'tom')]:
    def d(pid, t=t): return SLOT[t].get(pid) or COMMON.get(pid, '')
    models = ''.join(f'<tr><td class="n">{e(m)}</td><td>{e(x)}</td></tr>' for m, x in MODELS[t])
    add(f'''<section class="page"><h2>{code} {e(P[code]['name'])}</h2><p class="lead">{MODTXT[code]}</p>{shot(code, 'shot half')}
<h3>models</h3><table class="params">{models}</table><h3>controls</h3>{ptable(params_rows(code, d))}{presets(code)}</section>''')

# ---- effects ----------------------------------------------------------------------------------------------
add(ddivider('dl4', 'the processes', 'mc3 micro · dl4 dub · rv5 room · sw6 swing · fl7 filter · gl8 glue', '#2f7fd1'))
DINTRO = {
 'mc3': 'Micro-sampling for chopped textures: grains and stutters cut from whatever passes through, in time with the host.',
 'dl4': 'Tape echo with the filter inside the repeats — for chord stabs (1/8 dotted), rim throws and dub washes.',
 'rv5': 'Warm rooms, plates and a big chamber. Keep the low cut up so the kick stays dry.',
 'sw6': 'The swing of a drum machine on any audio: put it on a straight loop or a whole drum bus.',
 'fl7': 'A DJ isolator with kills and a warm one-knob filter; the 4-bar sweep preset is the classic build.',
 'gl8': 'Bus compression and tape for the drum group — or pumping, sidechain-like movement in parallel.',
}
DTIPS = {
 'mc3': ['Soft grains at 30% mix under a pad make it shimmer.', 'Stutter fill: automate density to 100% for one bar before the drop.'],
 'dl4': ['Chord echo 1/8 d on sb1 stabs: classic deep house space.', 'Dub throw: send set to 0, automate it up for single hits.'],
 'rv5': ['Warm room on claps and rims, 20% mix.', 'Big chamber on a send, low cut 300 hz, for pads.'],
 'sw6': ['Deep swing 56 on the whole drum bus.', 'Learn groove from a sampled break and apply it to programmed drums.'],
 'fl7': ['Low kill on the build, back on the one.', 'Warm lowpass with a little drive on the drum bus for a dusty sound.'],
 'gl8': ['Warm glue on the drum group: 2:1, 15 ms attack, 2 db of reduction.', 'Pumping in parallel on chords for movement.'],
}
for code in ['mc3', 'dl4', 'rv5', 'sw6', 'fl7', 'gl8']:
    def d(pid, code=code):
        if code == 'sw6' and pid.startswith('step'): return FX['sw6']['step']
        return FX[code].get(pid, '')
    skip = (lambda pid: pid.startswith('step') and pid not in ('step1', 'step16')) if code == 'sw6' else (lambda pid: False)
    tips = ''.join(f'<li>{t}</li>' for t in DTIPS[code])
    add(f'''<section class="page"><h2>{code} {e(P[code]['name'])}</h2><p class="lead">{DINTRO[code]}</p>{shot(code, 'shot half')}
<h3>controls</h3>{ptable(params_rows(code, d, skip))}<h3>using it</h3><ul>{tips}</ul>{presets(code)}</section>''')

# ---- sb1 stabs ----------------------------------------------------------------------------------------------
add(ddivider('sb1', 'sb1 stabs', 'chord stabs, organs and sub bass', '#7fd3c8'))
CH = [('off', 'single notes'), ('fifth', '0 7'), ('octave', '0 12'), ('major', '0 4 7'), ('minor', '0 3 7'), ('major 7', '0 4 7 11'), ('minor 7', '0 3 7 10'),
      ('dominant 7', '0 4 7 10'), ('major 9', '0 4 7 11 14'), ('minor 9', '0 3 7 10 14'), ('minor 11', '0 3 7 10 14 17'), ('sus 4', '0 5 7'),
      ('6 / 9', '0 4 7 9 14'), ('minor 9 (no 5th)', '0 3 10 14'), ('deep stack', '0 7 10 14 17')]
add(f'''<section class="page" id="sb1"><h2>sb1 stabs</h2><p class="lead">Chord stabs, organs, electric pianos, pads and sub bass. The engine is
faltung's bass synth (two unison oscillators with fm, sub, filter, formant, comb, distortion, envelopes, three lfos, a step lfo and an
8-slot modulation matrix) with one addition: <b>one key plays a whole chord</b>.</p>{shot('sb1')}
<h3>chords</h3><p>Set <b>chord</b> and every key plays that chord with the key as its root; each chord tone gets its own voice (twelve
voices, so two overlapping six-note chords fit). <b>voicing</b>: close; open (the second tone up an octave — wider, more "house");
drop 2 (the second-highest tone down an octave — softer, jazzier).</p>
<table class="params"><tr><th>chord</th><th>semitones above the key</th></tr>{''.join(f'<tr><td class="n">{e(n)}</td><td>{t}</td></tr>' for n, t in CH)}</table>
<p>The deep house stab is <b>minor 9</b> or <b>minor 7</b>, short (decay 300–600 ms, no sustain), a filter that opens with the mod envelope,
and an echo (dl4 "chord echo 1/8 d"). Play it on the offbeats.</p>
<h3>modulation</h3><p>The <b>modulation</b> tab holds lfo 2, lfo 3 (to 16 bars, one-shot), a 16-step lfo and the matrix — slow filter
movement on pads, tremolo on the electric piano (as in its preset), stepped vowel patterns.</p>
<h3>controls</h3>{ptable(params_rows('sb1', wb_desc, wb_skip))}<p class="small">steps 2 – 15 and matrix slots 2 – 8 work like the ones shown.</p>{presets('sb1')}</section>''')

# ---- recipes ------------------------------------------------------------------------------------------------
import sys
sys.path.insert(0, os.path.join(ROOT, 'docs', 'manual'))
from make_sndstudio import chapter as sndstudio_chapter
add(divider('sndstudio', 'the deep house studio: tracks · scenes · song · sends · modulators · 128 outputs', '#151c2a', wire('#5b7aa3'), INK, '#ffffff'))
add(sndstudio_chapter(shot))

add('''<section class="page" id="recipes"><h2>recipes</h2>
<h3>a deep house groove in five minutes</h3><ol><li>kt1, preset <b>warehouse</b>, 122 bpm.</li><li>Shift the clap +6 ms and the shaker +4 ms (track shift).</li>
<li>Give the conga track length 12 so it rolls against the bar.</li><li>Clap and rim to a send with rv5 <b>warm room</b>.</li><li>Drum group: sw6 <b>deep swing 56</b>, then gl8 <b>warm glue</b>.</li></ol>
<h3>the stab</h3><p>sb1 <b>minor 9 stab</b>, chord voicing open, notes on the "and" of beats 2 and 4; dl4 <b>chord echo 1/8 d</b> after it, mix 25 %; automate
the cutoff slowly over 16 bars.</p>
<h3>bass under it</h3><p>sb1 <b>round bass</b> or <b>deep sub</b> on a second track, mono, following the root of the stabs an octave or two down.</p>
<h3>the build</h3><p>fl7 on the drum group: <b>low kill</b> for the last 8 bars before the drop, or the <b>dj sweep 4 bars</b> preset.</p>
<h3>texture</h3><p>cl8 <b>vinyl dust</b> on 16ths at low level, and mc3 <b>soft grains</b> on a pad.</p>
<h2>technical notes</h2><ul><li>Same engines and quality as faltung: band-limited oscillators, modal resonators, anti-aliased saturation, 2× oversampled synth voice.</li>
<li>Every plug-in passes the same offline tests as faltung (silence, overload, non-finite output, state recall), plus a chord check for sb1.</li>
<li>Licence AGPL-3.0. Fonts: Inter, IBM Plex Mono (SIL Open Font License).</li></ul></section>''')

exec ('CSS = f' + repr (MAIN[MAIN.index("CSS = f\'\'\'") + 8:MAIN.index("\'\'\'\nms_page")]), g)      # the faltung manual's sheet (fonts by relative path)
CSS += f'''
@page {{ background: {PAGE}; @bottom-right {{ color: {INK}; }} }}
html, body {{ background: {PAGE}; }}
body {{ color: {INK}; }}
h3 {{ border-bottom-color: {INK}; }}
p.lead {{ color: #a9c9d2; }}
code {{ background: #262b31; }}
table.params th {{ border-bottom-color: {INK}; }}
table.params td {{ border-bottom-color: #343a41; }}
table.params td.r, table.params td.d {{ color: #aab4b9; }}
img.shot {{ border-color: #343a41; }}
.presets span {{ background: {INK}; color: {PAGE}; }}
.small {{ color: #9aa3a8; }}
.cover {{ background: {dark_field('#fbfcfc')}; }}
.cover .sub {{ color: {dark_accent('#2f6f93')}; }} .cover .ver {{ color: #9aa9b1; }}
.cover .tl {{ background: rgba(21,23,26,.82); }}
.chip {{ border-color: #6b767c; }}
'''
page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>faltung deep manual</title><style>{CSS}</style></head><body>{"".join(doc)}</body></html>'
open(os.path.join(HERE, 'manual.html'), 'w').write(page)
chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
pdf = os.path.join(HERE, 'faltung deep Manual.pdf')
if os.path.exists(pdf): os.remove(pdf)
subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                f'--print-to-pdf={pdf}', 'file://' + os.path.join(HERE, 'manual.html')], capture_output=True)   # chrome may exit non-zero on teardown
assert os.path.getsize(pdf) > 100000, 'no pdf written'
print(pdf)

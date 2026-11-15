#!/usr/bin/env python3
"""faltung deep manual → "faltung deep Manual.pdf". Same machinery as docs/manual/make.py (whose control texts and
helpers it reuses); parameter tables from params.json (faltungDeepTest --dump-params), pictures from img/
(faltungDeepTest --screenshots, --tree; converted to jpeg with sips to keep the pdf small)."""
import html, json, os, re, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
MAIN = open(os.path.join(ROOT, 'docs', 'manual', 'make.py')).read()
P = {p['code']: p for p in json.load(open(os.path.join(HERE, 'params.json')))}
FONTS = os.path.join(ROOT, 'Resources', 'fonts')
e = html.escape
GROUND = {'kt1': '#1d3772', 'kt2': '#33441c', 'bd1': '#5a2414', 'sd2': '#2e3566', 'rm3': '#3b2c2a', 'hh4': '#1f3d52', 'cp6': '#5b3416', 'pc7': '#2c4a34',
          'cl8': '#26355c', 'tm9': '#4a1e1a', 'mc3': '#2b2e5c', 'dl4': '#183d4a', 'rv5': '#40331f', 'sw6': '#3b2450', 'fl7': '#2f4316', 'gl8': '#2a323c', 'sb1': '#14123f'}
LIGHT = {'kt1': '#d8e3f7', 'kt2': '#e3ecc9', 'bd1': '#f3d6c8', 'sd2': '#dfe2f7', 'rm3': '#f2dcd6', 'hh4': '#d5e6ef', 'cp6': '#f1dcc4', 'pc7': '#dbead9',
         'cl8': '#e1e8f6', 'tm9': '#f0d6cf', 'mc3': '#dfe0f5', 'dl4': '#d3e9ec', 'rv5': '#efe2cc', 'sw6': '#eadcf2', 'fl7': '#e5efcf', 'gl8': '#e1e6eb', 'sb1': '#f0dfe6'}

# reuse the shared descriptions and helpers of the faltung manual
g = globals()
exec (MAIN[MAIN.index('SLOT = {'):MAIN.index('INTRO = {')], g)
exec (MAIN[MAIN.index('def ptable'):MAIN.index('def chevrons')], g)
exec (MAIN[MAIN.index('WBMOD = {'):MAIN.index("add(divider('wb1 wub'")], g)

def shot(code, cls='shot'):
    return f'<img class="{cls}" src="img/{code}.jpg" alt="{code}">'

doc = []
add = doc.append
ORDER = ['kt1', 'kt2', 'bd1', 'sd2', 'rm3', 'hh4', 'cp6', 'pc7', 'cl8', 'tm9', 'mc3', 'dl4', 'rv5', 'sw6', 'fl7', 'gl8', 'sb1']

def sleeve(code, title, sideA, sideB, cat, art):
    """a divider page laid out like the back of a sleeve"""
    a = ''.join(f'<div>{i + 1}.&nbsp;&nbsp;&nbsp;{e(t)}</div>' for i, t in enumerate(sideA))
    b = ''.join(f'<div>{i + 1 + len(sideA)}.&nbsp;&nbsp;&nbsp;{e(t)}</div>' for i, t in enumerate(sideB))
    return f'''<section class="sleeve" style="background:{GROUND[code]};color:{LIGHT[code]}"><img class="tree" src="img/{art}"><div class="fade" style="background:linear-gradient(180deg,transparent 45%,{GROUND[code]} 92%)"></div>
<div class="s-l"><div>faltung deep</div><div>{e(title)}</div><div class="cat">{cat}</div></div>
<div class="s-a"><div>Side A</div>{a}</div><div class="s-b"><div>Side B</div>{b}</div>
<div class="vert">drums, processes and stabs for deep house &nbsp;·&nbsp; faltung deep &nbsp;·&nbsp; FreakiePie</div></section>'''

# ---- cover ------------------------------------------------------------------------------------------------
add(sleeve('kt1', 'MANUAL', ['kt1 kit', 'kt2 loop', 'bd1 kick', 'sd2 snare', 'rm3 rim', 'hh4 hats', 'cp6 clap', 'pc7 perc', 'cl8 dust'],
           ['tm9 tom', 'mc3 micro', 'dl4 dub', 'rv5 room', 'sw6 swing', 'fl7 filter', 'gl8 glue', 'sb1 stabs'], 'FDP000', 'tree-kt1.jpg'))

add('''<section class="page"><h2>contents</h2><ol class="toc">
<li><a href="#intro">introduction</a></li><li><a href="#install">installing</a></li><li><a href="#using">the controls and the tree</a></li>
<li><a href="#auto">automation and midi</a></li><li><a href="#kt1">kt1 kit</a></li><li><a href="#kt2">kt2 loop</a></li>
<li><a href="#modules">the drum modules</a></li><li><a href="#fx">the processes</a></li><li><a href="#sb1">sb1 stabs</a></li><li><a href="#recipes">recipes</a></li></ol>
<h2 id="intro">introduction</h2>
<p class="lead">faltung deep is the deep house twin of faltung: the same synthesis and processing engines, retuned and re-preset for
warm, swung, late-night house — round kicks, wooden rims, soft claps, shakers and congas, chord stabs and sub bass.</p>
<p>It has seventeen plug-ins: the drum machine <b>kt1 kit</b>, the slicer <b>kt2 loop</b>, eight single drum modules, six effects and
<b>sb1 stabs</b>, a synth that plays a whole chord from one key. faltung's mastering bus and Partials are not part of it — use them from
faltung itself. Both suites install side by side; every faltung deep plug-in is called <i>faltung deep …</i>.</p>
<p>The look follows record sleeves: a mirrored tree in one duotone per plug-in, and the controls laid out like the back of a sleeve —
title top left, a "Side A / Side B" track list, a catalogue number, small credits along the right edge.</p>
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
<b>FreakiePie</b>. Every plug-in opens on its first deep house preset.</p>

<h2 id="using">the controls and the tree</h2>
<table class="params"><tr><th>control</th><th>how</th></tr>
<tr><td class="n">slider</td><td>a condensed label, a hairline and a white tick. Drag sideways or up/down; <b>shift</b> for fine moves; <b>double-click</b> resets; the wheel steps.</td></tr>
<tr><td class="n">choice</td><td>click for a menu, drag up/down to step.</td></tr>
<tr><td class="n">switch</td><td>the small square: filled = on.</td></tr>
<tr><td class="n">presets</td><td>&lt; and &gt; step, click the name for the list; also in the host's program menu.</td></tr>
<tr><td class="n">size</td><td>drag the bottom-right corner (half to double size).</td></tr></table>
<p><b>The tree</b> is alive. It sways in a slow wind — every branch at its own pace, so it never repeats — and the sway grows with the
level coming out of the plug-in. Leaves brighten on each hit, and the glow behind the canopy breathes with the loudness. It is mirrored
around the centre line like the sleeves, and fades into the ground behind the controls so they stay readable. It is drawn at a reduced
size and scaled up, so it costs very little processor time.</p>

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
add(sleeve('kt1', 'KT1 KIT', NAMES[:4], NAMES[4:], 'FDP001', 'tree-kt1.jpg'))
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
<h3>the page</h3><p>The track list at the top is the voice selector: <b>Side A</b> 1–4 and <b>Side B</b> 5–8 — click a name to select and
hear it; it lights up on every hit. On the right, the selected voice's model and eight sound controls, then its track settings. At the
bottom the step grid and the global row: run, internal bpm, swing, micro range, patterns a1–a8 and copy / paste / clear.</p>
<table class="params"><tr><th>voice</th><th>engine</th><th>midi</th><th>output</th></tr>''' +
''.join(f'<tr><td class="n">{IDS[v]} {NAMES[v].lower()}</td><td>{ENG[v]}</td><td>{["C1","C#1","D1","D#1","E1","F1","F#1","G1"][v]}</td><td>{v + 2}</td></tr>' for v in range(8)) +
'''</table><h3>editing steps</h3><table class="params">
<tr><td class="n">click</td><td>step on / off</td></tr><tr><td class="n">drag up / down</td><td>velocity</td></tr><tr><td class="n">drag across</td><td>paint</td></tr>
<tr><td class="n">shift-drag</td><td>microtiming — the step slides early or late</td></tr><tr><td class="n">alt-click</td><td>probability 100 → 75 → 50 → 25 %</td></tr>
<tr><td class="n">right-click</td><td>ratchet 1–4</td></tr></table>
<p>Grooves: <b>warehouse</b> (four to the floor, offbeat open hat, shaker 16ths, conga answers), <b>jazzy</b> (broken kick, busy hats, rim
syncopation), <b>minimal deep</b> (half-probability shaker, sparse rims), <b>broken</b> (off-grid kick). Shaker and conga sit a little late
in every factory pattern; push them further with the track <i>shift</i>.</p>''' + f'''<h3>controls</h3>{ptable(kt1_rows())}{presets('kt1')}</section>''')

# ---- kt2 ----------------------------------------------------------------------------------------------
add(f'''<section class="page" id="kt2"><h2>kt2 loop</h2><p class="lead">A loop slicer for chopped breaks and resampled grooves. It starts with
a bar of kt1's "warehouse" groove; drop any loop onto the waveform to replace it.</p>{shot('kt2')}
<p>Slices are cut on a grid or on the hits (<b>onsets</b>); each lit step shows the slice it plays — drag up/down to choose another,
alt-click to reverse, shift-drag for microtiming. <b>fit</b> keeps the loop in time with the host tempo.</p>
<h3>controls</h3>{ptable(params_rows('kt2', lambda pid: LOOP.get(pid) or SEQ.get(pid, '')))}{presets('kt2')}</section>''')

# ---- modules ----------------------------------------------------------------------------------------------
add(sleeve('bd1', 'DRUM MODULES', ['bd1 kick', 'sd2 snare', 'rm3 rim', 'hh4 hats'], ['cp6 clap', 'pc7 perc', 'cl8 dust', 'tm9 tom'], 'FDP003', 'tree-bd1.jpg'))
add('''<section class="page" id="modules"><h2>the drum modules</h2><p class="lead">Each module is one drum engine as its own instrument, for drum
racks and midi clips. Four models and eight controls per engine; level, pan, velocity, key tracking and choke on all of them. Click the
tree to hear the sound.</p></section>''')
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
add(sleeve('dl4', 'PROCESSES', ['mc3 micro', 'dl4 dub', 'rv5 room'], ['sw6 swing', 'fl7 filter', 'gl8 glue'], 'FDP012', 'tree-dl4.jpg'))
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
add(sleeve('sb1', 'SB1 STABS', ['minor 9 stab', 'organ stab', 'electric piano', 'dub chord'], ['warm pad', 'deep sub', 'round bass', 'plucked chord'], 'FDP017', 'tree-sb1.jpg'))
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
<li>Licence AGPL-3.0. Fonts: Barlow, Inter, IBM Plex Mono (SIL Open Font License).</li></ul></section>''')

CSS = f'''
@font-face {{ font-family: Cond; src: url("file://{FONTS}/BarlowCondensed-Regular.ttf"); font-weight: 400; }}
@font-face {{ font-family: Cond; src: url("file://{FONTS}/BarlowCondensed-Medium.ttf"); font-weight: 600; }}
@font-face {{ font-family: Barlow; src: url("file://{FONTS}/Barlow-Light.ttf"); font-weight: 300; }}
@font-face {{ font-family: Barlow; src: url("file://{FONTS}/Barlow-Regular.ttf"); font-weight: 400; }}
@page {{ size: A4; margin: 15mm 15mm 16mm 15mm; @bottom-right {{ content: counter(page); font: 8pt Cond; }} }}
html {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
body {{ font: 9.6pt/1.5 Barlow, Helvetica, sans-serif; font-weight: 400; color: #1b2235; margin: 0; }}
h2 {{ font: 600 22pt Cond; letter-spacing: .3px; margin: 0 0 6pt; color: #1d3772; }}
h3 {{ font: 600 11pt Cond; letter-spacing: .6px; text-transform: uppercase; margin: 14pt 0 4pt; border-bottom: .6pt solid #1d3772; padding-bottom: 2pt; color: #1d3772; }}
p.lead {{ font-size: 11pt; font-weight: 300; line-height: 1.45; }}
.page {{ break-before: page; }}
code {{ font: 8.6pt Cond; background: #e8eef9; padding: 0 3px; }}
a {{ color: inherit; }}
table.params {{ width: 100%; border-collapse: collapse; margin: 4pt 0 6pt; font-size: 8.5pt; }}
table.params th {{ text-align: left; font: 600 8.5pt Cond; letter-spacing: .5px; text-transform: uppercase; border-bottom: .6pt solid #1d3772; padding: 2pt 6pt 2pt 0; }}
table.params td {{ border-bottom: .4pt solid #d5dbe8; padding: 1.8pt 6pt 1.8pt 0; vertical-align: top; }}
table.params td.n {{ font: 600 9pt Cond; white-space: nowrap; }}
table.params td.r, table.params td.d {{ font: 400 8.6pt Cond; color: #3b4660; }}
table.params tr {{ break-inside: avoid; }}
img.shot {{ width: 100%; margin: 6pt 0 2pt; }}
img.shot.half {{ width: 64%; }}
.presets {{ font: 9pt Cond; margin-top: 8pt; }} .presets span {{ background: #1d3772; color: #fff; padding: 1px 6px; margin-right: 6px; text-transform: uppercase; letter-spacing: .5px; }}
.small {{ font-size: 8pt; color: #555; }}
ol.toc {{ font: 13pt Cond; line-height: 1.8; }} ul, ol {{ padding-left: 14pt; }} li {{ margin: 2pt 0; }}
.sleeve {{ break-before: page; position: relative; height: 265mm; overflow: hidden; font-family: Cond; }}
.sleeve:first-child {{ break-before: auto; }}
.sleeve img.tree {{ position: absolute; inset: 0; width: 100%; height: 100%; object-fit: cover; }}
.sleeve .fade {{ position: absolute; inset: 0; }}
.sleeve .s-l {{ position: absolute; left: 12mm; top: 12mm; font-size: 14pt; line-height: 1.3; }} .sleeve .cat {{ margin-top: 12mm; font-weight: 600; letter-spacing: 1px; }}
.sleeve .s-a {{ position: absolute; left: 70mm; top: 12mm; font-size: 14pt; line-height: 1.3; }}
.sleeve .s-b {{ position: absolute; left: 125mm; top: 12mm; font-size: 14pt; line-height: 1.3; }}
.sleeve .vert {{ position: absolute; right: 6mm; top: 14mm; writing-mode: vertical-rl; font: 300 8pt Barlow; letter-spacing: .4px; }}
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

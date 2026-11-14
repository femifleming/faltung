#!/usr/bin/env python3
# Builds manual.html from the pictures in fig/ and img/, then prints manual.pdf with Chrome.
import os, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
out = []
def h1(t): out.append(f'<h1>{t}</h1>')
def h2(t): out.append(f'<h2>{t}</h2>')
def p(t): out.append(f'<p>{t}</p>')
def shot(name, cap=''): out.append(f'<figure class="wide"><img src="img/{name}.png"><figcaption>{cap}</figcaption></figure>')
def figs(*items, cols=2, scope=False):
    out.append(f'<div class="grid c{cols}{" sc" if scope else ""}">')
    for name, title, cap in items:
        axis = '' if scope else '<i class="ax">time &rarr;</i><i class="ay">pitch &uarr;</i>'
        out.append(f'<figure><div class="im"><img src="fig/{name}.png">{axis}</div><figcaption><b>{title}</b> {cap}</figcaption></figure>')
    out.append('</div>')
def table(rows):
    out.append('<table>' + ''.join(f'<tr><td>{a}</td><td>{b}</td></tr>' for a, b in rows) + '</table>')

out.append('<header><div class="t">Partials</div><div class="s">An additive synthesizer you can see. Manual.</div></header>')
p('Partials builds every note out of up to 516 sine waves, called partials. Almost everything in the top half of the window changes those partials before they are turned into sound: how loud each one is, where it sits in pitch, and how that changes over time. The display on the right shows them as you play.')
p('<b>How to read the pictures in this manual.</b> Every dark picture was made by the synth itself. Time runs left to right, pitch runs bottom to top, and each horizontal line is one partial. In most of them one note is held while one control is moved from one end to the other, so you can see what that control does. Brightness is tilted a little towards the top so high partials stay visible.')
shot('tab0', 'The whole window. Top: preset, parts A and B, mix, main LFO amount, volume. Middle: the controls in five columns and the partial display. Bottom: four pages (ENV, IMG, FX, ADV).')

h1('Getting around')
table([
 ('A bar', 'Drag sideways or up and down. Hold Shift for fine steps. Double-click to reset. The mouse wheel also works.'),
 ('A value with &#9662;', 'A menu. Click it.'),
 ('slanted box', 'A switch: filled = on. Click it.'),
 ('Right-click a bar', 'Reset, copy the value to the other part, open its articulator (envelope, LFO, mappings) in the ENV page, or quick-map it to X, Y or Z.'),
 ('Partial display', 'Shows the newest note of the part you are looking at. Click it to switch between an octave scale and a Hz scale. Hover to read a frequency.'),
 ('A and B', 'Two complete synths. The A / B buttons choose which one the controls show. "A on" / "B on" switch them on. "mix" balances them. "link" makes every edit apply to both.'),
 ('Preset menu', 'Built-in presets, your saved ones (in Music / Partials / Presets), save, open, and copy / paste of a whole preset or one part.'),
 ('Window size', 'Drag the bottom-right corner. Everything scales.'),
])

h1('Timbre')
shot('main', 'The five columns of controls and the partial display.')
p('The two small pictures at the top left are timbre 1 and timbre 2: the starting waveforms. Timbre 1 starts as a saw (every partial, each one quieter than the last), timbre 2 as a square (every other partial). Click a picture to draw its partials in the ENV page. Right-click it for saw, square, triangle, sine, random, or to analyze a single-cycle wave file. You can also drop a wave file on it.')
figs(('w_saw', 'Timbre 1.', 'The default saw.'), ('w_square', 'Timbre 2.', 'The default square.'), ('w_phase_rand', 'rand fully left.', 'Same partials, each with a random phase: same tone, different wave.'), cols=3, scope=True)
figs(('timbre_mix', 'mix', '0% to 100%. Fades from timbre 1 to timbre 2: every second partial disappears.'),
     ('clip_high', 'clip, mode High threshold', '100% down to 10%. A ceiling comes down on the loudest partials, so the low ones are pushed down first.'),
     ('clip_low', 'clip, mode Soft low threshold', '100% down to 0%. The opposite: only partials above the threshold survive, so the sound opens from the bottom up.'),
     ('prot', 'prot', '0% to 100%, with a high-pass filter removing the low end. Protection brings the fundamental back.'),
     ('subs_around', 'sub 1, sub 2, sub 3 (Around)', 'Switched on one after another. Sub 1 is an octave below the note; 2 and 3 are its 3rd and 5th harmonics.'),
     ('subs_below', 'sub 1, sub 2, sub 3 (Below)', 'Switched on one after another: one, two and three octaves below the note.'))
table([
 ('Blend menu', 'How the two timbres combine: Fade, Subtract, Multiply, Maximum, Minimum, or Pluck (timbre 2 appears as the pluck dies away).'),
 ('phase', 'Where in the wave each note starts, 0 to 360&deg;.'),
 ('rand', 'Left: each partial gets its own random phase. Right: the whole wave starts at a random point on each note.'),
 ('env / vol', 'Volume before the partials become sound (env) and after (vol).'),
 ('gain, gain mode', 'Auto-gain. Evens out the level when shaping removes or adds a lot. Absolute aims at a fixed level, Relative at the level before shaping.'),
 ('fx dry', 'How much of this part skips the effects. 0% is all through the effects, 100% all dry. Negative values flip the dry signal.'),
 ('VELOCITY', 'What key velocity does: volume, skipping part of the attack, attack speed, release length.'),
])

h1('Pitch, glide, unison')
figs(('detune', 'detune x', '1.000 to 2.000. The fundamental stays put while every partial above it spreads away. The note turns bell-like.'),
     ('octmode', 'Distribution: oct', 'Partials are spaced in equal musical steps instead of harmonics; detune x goes 1 to 6 and widens the steps.'),
     ('vibrato', 'vibrato', '0 to 100 cents at 4 Hz.'),
     ('unison', 'UNISON pitch, order 7', '0% to 100%. Seven copies of the note drift apart in pitch; each line fans out into a band.'),
     ('porta', 'portamento', 'Three separate notes. Each new note slides in from the previous one.'),
     ('legato', 'legato', 'One held note; two overlapping notes bend it up and it returns when they are released.'),
     ('strum', 'STRUM time', 'A five-note chord pressed at once. The notes start one after another.'))
table([
 ('freq x, freq /', 'Multiply and divide the pitch. 1/2 is an octave down, 2/1 an octave up.'),
 ('pitch', 'Fine pitch, two octaves either way. "env" scales the pitch envelope.'),
 ('GLIDE time', 'Left of centre: milliseconds (up to 1.5 s). Right: steps of the host tempo. "limit": legato only bends within this distance; portamento never slides further than this.'),
 ('logarithmic / variable time / velocity', 'Curved slide; longer slides for bigger jumps; harder notes slide faster.'),
 ('UNISON order', '1 to 9 copies. "pan" spreads them left and right, "phase" at 100% (blur) gives each copy random phases, lower values space them evenly, "alt" flips every second copy in the right channel for extra width.'),
 ('Unison type', 'Classic (wide), Uniform (even), Blurred (drifting), Random, Hz (same spread in Hz for every partial).'),
 ('STRUM tension', 'Left: the notes speed up through the chord. Right: they slow down. The menu sets the direction: Up, Down, Alt, Random.'),
])

h1('Filters')
p('Two filters. They work on the partials, so they add nothing of their own: a partial is simply made louder or quieter. Each picture below holds one note while <b>freq</b> is swept from high to low.')
figs(('filt_crude_lp', 'Crude low pass', ''), ('filt_classic_lp', 'Classic low pass', ''), ('filt_hp', 'Classic high pass', ''),
     ('filt_bp', 'Classic band pass', ''), ('filt_stop', 'Band stop', ''), ('filt_phaser', 'Phaser', 'A row of notches.'), cols=3)
figs(('filt_width', 'width', '100% to 0%, cutoff fixed. Wide is a gentle slope, narrow a sharp edge.'),
     ('filt_kb', 'kb at 100%', 'Four notes an octave apart. The cutoff follows the note, so every note keeps the same number of partials.'),
     ('res_amount', 'res', '0% to 100%. A bump grows at the cutoff.'),
     ('res_offset', 'offset', '-2400 to +2400 cents. The bump moves away from the cutoff, below it and above it.'),
     ('res_osc', 'osc', 'Adds a sine at the resonance. Here it follows the cutoff down.'))
h2('Resonance types')
p('Same sweep each time, res at 80%.')
figs(('res_lowpass', 'Low pass', 'A peak, then a dip.'), ('res_classic', 'Classic', 'One narrow peak.'), ('res_cuberdon', 'Cuberdon', ''), ('res_pedestal', 'Pedestal', ''),
     ('res_sedge', 'Sedge hat', ''), ('res_wide', 'Wide bump', ''), ('res_double', 'Double cone', ''), ('res_well', 'Well', 'A cut with a peak either side.'),
     ('res_wormhole', 'Wormhole', 'The same, tighter.'), ('res_noise', 'Noise', 'Partials near the cutoff flicker.'), cols=4)
table([
 ('env', 'How far the filter-frequency envelope moves the cutoff, either way. Nothing happens until that envelope is switched on (right-click freq, Edit articulator).'),
 ('r.width', 'Width of the resonance bump. "adaptive res" keeps it sounding the same up and down the keyboard.'),
 ('scale, res scale', 'Whether filter and resonance are measured in octaves or in Hz.'),
 ('adaptive env', 'Shortens the filter envelope for high notes.'),
 ('FILTERS 1 &amp; 2 mix', 'Right: filter 1 feeds filter 2 (series). Centre: filter 1 only. Left: both side by side, ending with filter 2 only.'),
 ('eq', 'How much of the Global EQ graph is applied. Draw it in the ENV page (Shaping, Global EQ).'),
 ('Custom shape 1 / 2', 'Draw your own filter or resonance curve in the ENV page.'),
])
figs(('eq', 'eq', '0% to 100% with a dip and a peak drawn in Global EQ. The EQ stays at fixed pitches whatever note you play.'), cols=2)

h1('Blur and pluck')
figs(('blur_harm', 'BLUR harm', '0% to 100% on a square. Level spills from each partial into the empty ones next to it.'),
     ('blur_time', 'BLUR attack and decay', 'A moving phaser, with time blur switched on part-way through. Changes are smeared in time.'),
     ('pluck', 'PLUCK time 45%', 'Three notes. High partials die first, like a plucked string.'),
     ('pluck_time', 'PLUCK time', 'The same note at 20%, 45% and 70%.'))
table([('top, bottom', 'Top tension limits how far blur spreads upwards. Bottom sets how loud the blurred partials are.'),
       ('blur decay', 'Gives the pluck a slower, softer tail.'),
       ('Pluck shape', 'In the ENV page: which partials die fastest.')])

h1('Phaser')
p('A moving pattern of notches. Each picture is one held note with the phaser sweeping.')
figs(('ph_classic', 'Classic', ''), ('ph_triangle', 'Triangle', ''), ('ph_eggs', 'Eggs', ''), ('ph_deep', 'Deep', ''), ('ph_deeper', 'Deeper', ''),
     ('ph_condom', 'Condom', ''), ('ph_twins', 'Twins', ''), ('ph_cascade', 'Cascade', ''), ('ph_box', 'Box', ''), cols=3)
figs(('ph_width', 'width', '0% to 100%, not moving. The notches go from close together to far apart.'),
     ('ph_offset', 'offset', '0% to 100%, not moving. Slides the pattern by hand.'))
table([('speed', 'Centre is still. Right sweeps up, left sweeps down; further out is faster. Shown as the number of steps one sweep takes.'),
       ('kb', 'Shifts the pattern with the note.'), ('scale', 'Notches spaced in octaves, in Hz, or by harmonic number.'),
       ('width lfo', 'Lets a slow LFO move the width.'), ('freq / custom', 'Freq bends the pitch of the partials with the Phaser shape graph; custom uses that graph as the notch shape.')])

h1('Prism and harmonizer')
figs(('prism_add', 'PRISM amount, Add', '-100% to +100%. Partials between the octaves are pushed off their places; the middle of the picture is 0%.'),
     ('prism_mul', 'PRISM amount, Multiply', 'The same, but the shift grows towards the top.'),
     ('hz_shift_plus', 'HARMONIZER shift step, +', 'Step 1 to 8. Copies of the sound are laid on at multiples of the step; watch which partials brighten.'),
     ('hz_shift_x', 'HARMONIZER shift step, x', 'Step 1 to 8 in multiply mode. The copies spread out faster.'),
     ('hz_gap', 'HARMONIZER gap step', 'Step 1 to 8. Only every n-th partial is copied and the ones between are turned down.'))
table([('by level', 'Prism moves loud partials more (On) or quiet ones more (Inverted).'),
       ('amount, width, strength', 'How much harmonizer, how high up it reaches, how strong the copies are.'),
       ('offset', 'Moves the shift or gap pattern up in octaves.')])

h1('The ENV page')
shot('page0', 'Left: the list of targets. Middle: the line editor. Right: envelope and LFO settings for the chosen target.')
p('This is where things move. Pick a target on the left, pick a <b>part</b> (Envelope, LFO, or one of the mappings), switch it <b>on</b>, and draw.')
figs(('env_volume', 'Volume, Envelope', 'Slow attack, decay to a lower level, release after the key is let go.'),
     ('env_filter', 'Filter 1 frequency, Envelope', 'Two notes. The cutoff jumps up and falls back each time.'),
     ('lfo_filter', 'Filter 1 frequency, LFO', 'The cutoff moves up and down on its own.'),
     ('lfo_pitch', 'Pitch, LFO with tension 100%', 'A square LFO: the pitch jumps between two notes.'),
     ('map_key', 'Filter 1 frequency, Keyboard mapping', 'Four notes. A rising line makes higher notes brighter.'),
     ('arp', 'Volume envelope with arpeggiator breaks', 'A held four-note chord. Each marked point passes the sound to the next note up.'))
h2('The three kinds of target')
table([
 ('Articulation', 'Things that can move during a note: volume, panning, filter frequency, pitch, phaser, prism and so on. Each has an envelope, an LFO and nine mappings.'),
 ('Mapping', 'Things set per note: envelope attack / decay / release scales, LFO speed and amount, unison phase, predelay. Mappings only.'),
 ('Shaping', 'One graph each: the partials of timbre 1 and 2, their phases, EQs, custom filter and resonance shapes, pluck shape, masks that keep an effect away from part of the range.'),
])
h2('The mappings')
table([
 ('Keyboard, Velocity', 'Left to right is low to high note, or soft to hard.'),
 ('Modulation X, Y, Z', 'The three x / y / z bars in the MODULATION section. Automate them from your host.'),
 ('Random', 'A new random position along the graph for each note.'),
 ('Unison index', 'A value per unison copy (works for panning, volume and pitch).'),
 ('Held index', 'First held note, second, third...'), ('Mod X speed', 'How fast X is moving.'),
])
h2('How a knob and its graphs combine')
table([
 ('Volume, mixes, widths, amounts', 'The knob is the maximum. The graphs scale it down: top of the graph is the knob value, bottom is zero.'),
 ('Pitch, panning, filter frequency, offsets', 'The knob is the centre. Graphs add to it: the middle line is no change. For pitch the top is +4 octaves and the bottom -4.'),
 ('Filter frequency envelope', 'The exception: bottom is no change, and the filter\'s "env" bar sets how far the top reaches, up or down.'),
])
h2('Drawing')
table([
 ('Add a point', 'Right-click or double-click the background.'), ('Move a point', 'Drag it. Shift keeps its height, Alt keeps its time.'),
 ('Delete a point', 'Alt-click it, or right-click it and choose Delete.'),
 ('Bend a line', 'Drag the small circle in the middle of a segment. Cmd-drag for fine steps. Right-click the circle to straighten.'),
 ('Right-click a point', 'Segment type (hold, smooth, curves, stairs, pulse, wave...), sustain / loop end (S), loop start (L), arpeggiator break.'),
 ('snap / slide / lock', 'Snap to the grid; moving a point moves everything after it; lock against changes.'),
 ('attack, decay, sustain, release', 'Stretch or shrink that part of the drawn envelope while you drag. They spring back to x1.00 when you let go.'),
 ('tempo / global', 'Tempo: times are in beats. Global: all notes share one envelope or LFO, restarted by each new note.'),
 ('smooth', 'Rounds off sudden jumps of the target.'),
 ('options', 'Reset, flip, normalize, decimate, smooth up, scale levels, draw per harmonic (bars), make all loop, follow an audio file, create a step sequence.'),
 ('Volume envelope off', 'The note simply starts and stops with the key.'),
])

h1('The IMG page')
shot('page1', 'A picture loaded as sound. The red line is the start position; a white line shows where the newest note is playing.')
p('Drop a sound file or a picture on the window. A <b>sound</b> is taken apart into partials and can then be played at any speed and pitch (resynthesis). A <b>picture</b> is played directly: left to right is time, bottom to top is partial 1 to 516, brightness is level.')
figs(('img_source', 'The picture', 'A random cloud (options, Generate random cloud).'), ('img_cloud', 'Played', 'The same picture as sound, at speed 200%.'),
     ('img_speed', 'speed', '+200% then -200% with ping pong looping: forwards, then backwards.'),
     ('img_formant', 'formant', '-600 to +600 cents with speed 0. The picture slides up through the partials while the pitch stays put.'))
table([
 ('Click in the image', 'Sets the start position (time). Shift-drag selects a stretch. Ctrl-drag or Cmd-drag sets the loop region.'),
 ('speed, coarse', 'Playback speed, backwards when negative; coarse multiplies it up to x64 or down to almost stopped.'),
 ('smooth', 'How gently a moved start position is followed. Right of centre, big jumps are instant.'),
 ('looping', 'One-shot, Loop, Ping pong.'), ('sharp', 'For sounds: puts the partials back in step at each attack so transients stay crisp.'),
 ('LEVEL scale, mix', 'Loudness of the picture; at mix 0% the picture is ignored and you hear the plain timbre.'),
 ('FREQUENCY scale, mode', 'How strongly, and how, the frequency plane bends the partials: by octaves, Hz, wide Hz, or as unison detune.'),
 ('curve', 'How one column blends into the next: Block, Cap, Bell, Linear, Cubic, Rise, Fall.'),
 ('plane', 'Look at the level picture (Gain), the pitch-bend picture (Freq), or both.'),
 ('options', 'Analyze audio, open picture, high precision analysis (516 bands, tuned to the sound, instead of 255), convert to image synthesis, save as PNG, loop region, map slices to keys, time envelopes, random cloud, degrade, crossfade, crop, invert, flip.'),
])

h1('The FX page')
shot('page2', 'Five effects on the finished sound. Their order is set on the ADV page.')
h2('Distortion')
p('A sine wave through each type at amount 60%.')
figs(*[(f'dist_{n}', t + '.', '') for n, t in [('off','Off'),('classic','Classic'),('hill','Hill'),('flathat','Flat hat'),('cube','Cube'),('log','Log'),('softsat','Softsat'),('rubber','Rubber'),('ribbon','Ribbon'),('sincrush','Sincrush'),('bitcrush','Bitcrush'),('bitjam','Bitjam')]], cols=4, scope=True)
table([('asym', 'Up: lopsided. Down: lopsided in opposite directions left and right.'), ('mix', '50% is distorted only, 0% an even blend, 100% clean. Negative flips it.'), ('high cut', 'Softens the result.')])
h2('Chorus, delay, reverb, compression')
figs(('fx_chorus', 'Chorus, order 5', 'Each partial is smeared into a slowly moving band.'),
     ('fx_delay', 'Delay', 'A short note and its echoes.'),
     ('fx_delay_damp', 'Delay with fb damping', 'Each echo is filtered again, so they get duller.'),
     ('fx_reverb', 'Reverb, Flat', 'A short note and its tail.'),
     ('fx_reverb_warm', 'Reverb, Warmer', 'The low end rings longest.'),
     ('fx_reverb_bright', 'Reverb, Brighter', 'The low end dies first.'), cols=3)
table([
 ('Chorus', 'order = number of voices (0 is off). The presets menu fills in classic settings. mix: centre dry, right chorus, left flipped chorus.'),
 ('Delay time', 'Right of centre: steps of the tempo. Left: milliseconds. Modes: Normal, Inverted (left and right swap each echo), Ping pong.'),
 ('Reverb', 'Presets, color, input low and high cut, predelay, room size, diffusion, decay, damping, wet.'),
 ('Compression', 'Limiting holds the peaks. Warming, Heating and Burning squeeze three bands and lift the low end more each time. low / mid / high set the band levels.'),
 ('LEVELS pre / post', 'Volume into and out of the effects.'),
])

h1('The ADV page')
shot('page3', 'Settings for the current part, global settings, and the two orders.')
figs(('ramp', 'ramp, right (relative)', 'The first half second of a note. Partials come in from the bottom up.'), ('ramp_abs', 'ramp, left (absolute)', 'All partials fade in together.'))
table([
 ('predelay', 'Waits before the note starts. Left: milliseconds. Right: tempo steps.'),
 ('poly rel', 'Below 100%, a new note shortens the release tails still sounding.'), ('declick', 'A very short fade at the start and end of notes.'),
 ('UNIT ORDER', 'The order the shaping stages run in. Click an entry to move it later, right-click to move it earlier. Example: put HARM PROT after HARM CLIP to keep the fundamental safe from clipping.'),
 ('EFFECTS ORDER', 'The same for the five effects.'),
 ('randomness', 'Whether random values are new per note, per part, per unison copy, or never change.'),
 ('A/B as stereo sides', 'Load the same stereo sound into A and B: A carries the middle, B the sides, and the A/B mix sets the width.'),
 ('partials', 'Fewer partials uses less processor and loses the top end.'),
 ('precision', 'How often the shaping is recalculated: Average about every 5 ms, High 2.7 ms, Perfect 1.3 ms. "HQ when rendering" uses Perfect for offline bounces.'),
 ('VOICES mono', 'Off; a repeated note cuts itself (Kill same); any new note cuts all (Kill all); or sends them into release (Release all).'),
])

h1('Good to know')
table([
 ('16 notes at once', 'Each with up to 9 unison copies of up to 516 partials. Low notes with high unison cost the most.'),
 ('It can get loud', 'High resonance with self-oscillation, or harmonizer and blur at full, add a lot of level. Auto-gain or the Limiting compressor help.'),
 ('Saved presets', 'Plain text files ending in .partials. Drop one on the window to load it.'),
 ('Not in this version', 'Copying pictures through the clipboard, copy / paste of line points, envelope state files, and the step-sequence editing window (there is a menu of ready-made sequences instead).'),
])

css = '''
@font-face{font-family:Inter;src:url("file:///Users/femishonugafleming/partials/Resources/fonts/Inter-Regular.ttf");font-weight:400}
@font-face{font-family:Inter;src:url("file:///Users/femishonugafleming/partials/Resources/fonts/Inter-Medium.ttf");font-weight:600}
@font-face{font-family:Plex;src:url("file:///Users/femishonugafleming/partials/Resources/fonts/IBMPlexMono-Regular.ttf");font-weight:400}
@font-face{font-family:Plex;src:url("file:///Users/femishonugafleming/partials/Resources/fonts/IBMPlexMono-Medium.ttf");font-weight:600}
*{box-sizing:border-box;margin:0;padding:0}
@page{size:A4;margin:14mm 13mm}
html{-webkit-print-color-adjust:exact;print-color-adjust:exact;background:#eef4f7}
body{background:#eef4f7;color:#1d1c15;font:10pt/1.5 Inter,Helvetica,Arial,sans-serif;max-width:900px;margin:0 auto;padding:28px 22px}
header{position:relative;border-bottom:1px solid #1d1c15;padding:0 0 14px 64px;margin-bottom:18px;min-height:56px}
header:before{content:"";position:absolute;left:0;top:2px;width:48px;height:52px;background:no-repeat center/contain url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 104 112'><polygon points='0,30 52,0 104,30 52,60' fill='%23ffe15a'/><polygon points='0,30 52,60 52,112 0,82' fill='%23f2c100'/><polygon points='52,60 104,30 104,82 52,112' fill='%23d8a800'/></svg>")}
header .t{font-size:30pt;font-weight:600;text-transform:lowercase}header .s{color:#2f6f93;font-size:11.5pt;text-transform:lowercase}
h1{font:600 10.5pt Plex;text-transform:lowercase;border-bottom:1px solid #4b9cc8;padding-bottom:4px;margin:30px 0 12px;break-after:avoid}
h2{font:600 9.8pt Plex;text-transform:lowercase;margin:16px 0 6px;break-after:avoid}
p{margin:0 0 10px;color:#2b2f30}b{color:#1d1c15;font-weight:600}
figure{break-inside:avoid;margin:0}figure.wide{margin:8px 0 14px}figure.wide img{width:100%;display:block;border:1px solid #cddbe2}
figcaption{color:#55636b;font-size:8.4pt;line-height:1.35;margin-top:4px}figcaption b{color:#1d1c15}
.grid{display:grid;gap:12px 14px;margin:8px 0 14px}.c2{grid-template-columns:1fr 1fr}.c3{grid-template-columns:repeat(3,1fr)}.c4{grid-template-columns:repeat(4,1fr)}
.im{position:relative;border:1px solid #cddbe2}.im img{width:100%;display:block}
.ax,.ay{position:absolute;font:6.5pt Plex;font-style:normal;color:#55636b}.ax{right:4px;bottom:2px}.ay{left:4px;top:2px}
table{width:100%;border-collapse:collapse;margin:6px 0 14px}td{vertical-align:top;padding:3px 0;border-bottom:1px solid #cddbe2;color:#2b2f30;font-size:9.2pt}
td:first-child{width:30%;font:600 8.6pt Plex;color:#1d1c15;padding-right:14px;text-transform:lowercase}tr{break-inside:avoid}
'''
# analysis figures re-inked for the light page: dark → pale field, mid → blue, bright → ink
from PIL import Image
FL = os.path.join(HERE, 'figl'); os.makedirs(FL, exist_ok=True)
def reink(v):
    t = v / 255.0
    bg, bl, ink = (238, 244, 247), (47, 127, 209), (29, 28, 21)
    if t < 0.5: k = t * 2; return tuple(int(bg[i] + (bl[i] - bg[i]) * k) for i in range(3))
    k = min(1.0, (t - 0.5) * 1.6); return tuple(int(bl[i] + (ink[i] - bl[i]) * k) for i in range(3))
LUT = [reink(v) for v in range(256)]
for f in os.listdir(os.path.join(HERE, 'fig')):
    src, dst = os.path.join(HERE, 'fig', f), os.path.join(FL, f)
    if not f.endswith('.png') or (os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src)): continue
    g = Image.open(src).convert('L')
    out_img = Image.new('RGB', g.size); out_img.putdata([LUT[v] for v in g.getdata()]); out_img.save(dst)
out = [l.replace('src="fig/', 'src="figl/') for l in out]
html = f'<!doctype html><meta charset="utf-8"><title>Partials manual</title><style>{css}</style>' + '\n'.join(out)
open(os.path.join(HERE, 'manual.html'), 'w').write(html)
missing = [l for l in out if 'src="' in l and not os.path.exists(os.path.join(HERE, l.split('src="')[1].split('"')[0]))]
print('missing pictures:', len(missing))
subprocess.run(['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                '--virtual-time-budget=5000', f'--print-to-pdf={HERE}/manual.pdf', 'file://' + os.path.join(HERE, 'manual.html')], stderr=subprocess.DEVNULL)

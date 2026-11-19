#!/usr/bin/env python3
# Builds manual.html from the pictures in fig/ and img/, then prints manual.pdf with Chrome.
#   python3 make.py                          the manual from the pictures that are there
#   python3 make.py --shots <PartialsTest>   first take the window pictures again with the test tool
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))

# the numbered map of the window: (number, name, x, y, w, h) on the 1857-wide picture
REGIONS = [
 (1, 'timbre', 34, 6, 770, 152), (2, 'blur', 34, 162, 314, 98), (3, 'trem', 352, 162, 222, 98), (4, 'prism', 62, 264, 176, 134),
 (5, 'harmonizer', 242, 264, 332, 134), (6, 'unison', 578, 218, 232, 180), (7, 'pitch', 62, 402, 348, 92), (8, 'vibrato', 414, 402, 190, 92),
 (9, 'legato', 608, 402, 202, 92), (10, 'filter 1 and 2', 832, 6, 654, 320), (11, 'pluck', 840, 332, 152, 162), (12, 'phaser', 996, 332, 358, 162),
 (13, 'eq', 1358, 332, 128, 162), (14, 'global', 14, 500, 380, 300), (15, 'parts a / b', 396, 500, 186, 116), (16, 'x / y pad, mod x y z', 396, 620, 186, 206),
 (17, 'info', 22, 806, 560, 102), (18, 'editor: target, part, tabs', 588, 500, 940, 46), (19, 'envelope / lfo / mapping graph', 588, 550, 940, 300),
 (20, 'on, att dec sus rel, tempo, global, tools', 588, 852, 940, 56), (21, 'partial display', 1536, 6, 316, 1010), (22, 'keyboard', 50, 914, 1440, 104),
 (23, 'about, sounds, keyboard', 2, 366, 40, 124), (24, 'display switches', 1490, 362, 40, 132),
]
def take_shots(tool):
    from PIL import Image, ImageDraw, ImageFont
    I = os.path.join(HERE, 'img'); os.makedirs(I, exist_ok=True)
    def run(name, *args): subprocess.run([tool, '--shot', os.path.join(I, name + '.png'), *[str(a) for a in args]], check=True)
    run('tab0', 0); run('tab1', 1); run('tab2', 2); run('tab3', 3)
    run('lfo', 0, 4, 1); run('modx', 0, 4, 4); run('shape', 0, -4, 0)          # filter 1 frequency: LFO, mod X mapping; timbre 1 levels
    full = Image.open(os.path.join(I, 'tab0.png')).convert('RGB')
    full.crop((0, 0, 1534, 498)).save(os.path.join(I, 'main.png'))
    full.crop((0, 498, 584, 912)).save(os.path.join(I, 'global.png'))
    full.crop((396, 618, 584, 830)).resize((376, 424), Image.LANCZOS).save(os.path.join(I, 'pad.png'))
    for n, src in (('page0', 'tab0'), ('page1', 'tab1'), ('page2', 'tab2'), ('page3', 'tab3'), ('page_lfo', 'lfo'), ('page_modx', 'modx'), ('page_shape', 'shape')):
        Image.open(os.path.join(I, src + '.png')).convert('RGB').crop((584, 498, 1534, 912)).save(os.path.join(I, n + '.png'))
    for n in ('lfo', 'modx', 'shape'): os.remove(os.path.join(I, n + '.png'))
    # the map: the whole window, dimmed, with every section outlined and numbered
    m = Image.blend(full, Image.new('RGB', full.size, (27, 39, 49)), 0.45)
    d = ImageDraw.Draw(m)
    font = ImageFont.truetype(os.path.join(HERE, '..', '..', 'Resources', 'fonts', 'IBMPlexMono-Medium.ttf'), 22)
    for num, name, x, y, w, h in REGIONS:
        d.rectangle((x, y, x + w, y + h), outline=(239, 122, 60), width=2)
        tw = d.textlength(str(num), font=font)
        d.rectangle((x, y, x + tw + 12, y + 28), fill=(239, 122, 60)); d.text((x + 6, y + 1), str(num), font=font, fill=(27, 39, 49))
    m.save(os.path.join(I, 'map.png'))
if len(sys.argv) > 2 and sys.argv[1] == '--shots': take_shots(sys.argv[2])

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
shot('tab0', 'The whole window. Top: the part on show (A or B), with its filters on the right. Bottom left: Global, with the X / Y pad. Bottom centre: the editor for envelopes, LFOs and mappings, with the ENV, IMG, FX and ADV tabs. Right: the partial display. Along the bottom: the keyboard.')

h1('Where everything is')
p('The window is laid out like Harmor\'s, section for section: if you know that synth, every control is where your hand expects it. The look is Partials\' own. Where Harmor has a knob there is a <b>bar</b> with its value above and its name below; where it has a fader there is a standing bar; where it has a switch with two or three positions the positions are written out and the one in use is filled.')
shot('map', 'The sections, numbered. The table below names them.')
table([
 ('1 timbre', 'phase (start, rand) &middot; timbre 1 &middot; mix &middot; timbre 2 &middot; sub configuration &middot; blend menu &middot; sub 1-3 &middot; prot &middot; clip and its mode &middot; fx, vol, env &middot; auto (gain) &middot; pan &middot; the three velocity switches.'),
 ('2 blur &middot; 3 trem', 'blur: mix, time (attack, decay), harm, top / bottom tension. trem: depth, speed, gap.'),
 ('4 prism &middot; 5 harmonizer &middot; 6 unison', 'prism: amt, mode (+ / x), from vol. harmonizer: amt, width, str, shift and gap (ofs, step). unison: order, type, alt, pan / pitch / phase.'),
 ('7 pitch &middot; 8 vibrato &middot; 9 legato', 'pitch: hz / oct, freq and detune ratios, pitch, env. vibrato: depth, speed. legato: lin / log, fix / var, time, limit.'),
 ('10 filters', 'Filter 1 on top, filter 2 below it. Left half: adapt, oct / hz, type, env, kb track, width, freq. Middle: the 1 &gt; 2 mix. Right half: resonance type, oct / hz, res, width, adapt, ofs, osc.'),
 ('11 pluck &middot; 12 phaser &middot; 13 eq', 'pluck: time, blur. phaser: type, oct / hz / harm, lfo, mix, width, ofs, speed, kb.t. eq: mix, and a button that opens its two curves.'),
 ('14 global', 'options (sounds, copy and paste) &middot; vol and meter &middot; lfo &middot; limit, legato, porta, vel &middot; vel (velocity to volume) &middot; pre and post fx &middot; pitch &middot; strum.'),
 ('15 parts', 'A and B, the two on switches with the mix between them, link.'),
 ('16 pad', 'The X / Y pad and mod x, y, z.'),
 ('17 info', 'The name of the sound (click it for the sounds menu) and a note you can write. While the mouse is over a control it shows that control\'s full name and value.'),
 ('18-20 editor', 'What the graph moves (target), with what (part), smoothing, the four tabs; the graph; under it the on switch, att / dec / sus / rel, tempo, global, options, lock / step / snap / slide.'),
 ('21 display &middot; 24', 'The partials of the newest note, scrolling by. The three buttons to its left: on / off, wide / narrow, oct / hz.'),
 ('22 keyboard &middot; 23', 'Click to play. The buttons on the left edge: about (and window size), sounds, show / hide the keyboard.'),
])

h1('Getting around')
table([
 ('A bar', 'Drag sideways or up and down. Hold Shift for fine steps. Double-click to reset. The mouse wheel also works. The value is the small number above it.'),
 ('A standing bar', 'The same, upright. Its value shows while the mouse is on it.'),
 ('A number in a slanted box', 'Drag up or down.'),
 ('A value with &#9662;', 'A menu. Click it.'),
 ('slanted box', 'A switch: filled = on. Click it. Where the choices are written out (oct / hz, lin / log), click the one you want.'),
 ('Right-click a bar', 'Reset, copy the value to the other part, open its articulator (envelope, LFO, mappings) in the editor, or quick-map it to X, Y or Z.'),
 ('Orange mark on a bar', 'Something is moving this control: an envelope, an LFO or a mapping is switched on for it.'),
 ('Partial display', 'Shows the newest note of the part you are looking at. Hover to read a frequency.'),
 ('A and B', 'Two complete synths. The big A / B buttons choose which one the controls show. The slanted boxes at the ends of the mix bar switch them on. "link" makes every edit apply to both.'),
 ('Sounds', 'Built-in sounds, your saved ones (in Music / Partials / Presets), save, open, copy / paste of a whole sound or one part, chorus and reverb presets: the arrow next to "global", the list button on the left edge, or the name in the info box.'),
 ('Window size', 'Drag the bottom-right corner; everything scales. The cube button on the left edge has fixed sizes.'),
])

h1('Timbre')
shot('main', 'The part\'s panel: timbre along the top; blur and trem; prism, harmonizer and unison; pitch, vibrato and legato; the filtering section on the right with pluck, phaser and eq under it.')
p('The two small pictures at the top left are timbre 1 and timbre 2: the starting waveforms. Timbre 1 starts as a saw (every partial, each one quieter than the last), timbre 2 as a square (every other partial). Click a picture to draw its partials in the editor. Right-click it for saw, square, triangle, sine, random, or to analyze a single-cycle wave file. You can also drop a wave file on it.')
figs(('w_saw', 'Timbre 1.', 'The default saw.'), ('w_square', 'Timbre 2.', 'The default square.'), ('w_phase_rand', 'rand fully left.', 'Same partials, each with a random phase: same tone, different wave.'), cols=3, scope=True)
figs(('timbre_mix', 'mix', '0% to 100%. Fades from timbre 1 to timbre 2: every second partial disappears.'),
     ('clip_high', 'clip, mode High threshold', '100% down to 10%. A ceiling comes down on the loudest partials, so the low ones are pushed down first.'),
     ('clip_low', 'clip, mode Soft low threshold', '100% down to 0%. The opposite: only partials above the threshold survive, so the sound opens from the bottom up.'),
     ('prot', 'prot', '0% to 100%, with a high-pass filter removing the low end. Protection brings the fundamental back.'),
     ('subs_around', 'sub 1, sub 2, sub 3 (Around)', 'Switched on one after another. Sub 1 is an octave below the note; 2 and 3 are its 3rd and 5th harmonics.'),
     ('subs_below', 'sub 1, sub 2, sub 3 (Below)', 'Switched on one after another: one, two and three octaves below the note.'))
table([
 ('Blend menu (next to "timbre")', 'How the two timbres combine: Fade, Subtract, Multiply, Maximum, Minimum, or Pluck (timbre 2 appears as the pluck dies away).'),
 ('start', 'Where in the wave each note starts, 0 to 360&deg;.'),
 ('rand', 'Left: each partial gets its own random phase. Right: the whole wave starts at a random point on each note.'),
 ('The small button with three bars', 'Sub configuration: around the fundamental, or below it.'),
 ('The button under clip', 'The clipping mode. The drawing on it is the curve in use.'),
 ('env / vol', 'Volume before the partials become sound (env) and after (vol).'),
 ('auto', 'Auto-gain. Evens out the level when shaping removes or adds a lot. Its mode (Absolute aims at a fixed level, Relative at the level before shaping) is on the ADV tab.'),
 ('fx', 'How much of this part skips the effects. 0% is all through the effects, 100% all dry. Negative values flip the dry signal.'),
 ('pan', 'Where the part sits between left and right.'),
 ('vel: time, scale, rel', 'What key velocity does to the volume envelope: skip part of the attack, change the attack speed, change the release length. Velocity to volume is the "vel" switch in Global.'),
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
 ('freq, detune', 'Two numbers each: multiply / divide. freq 1 / 2 is an octave down, 2 / 1 an octave up. detune spreads the partials (see the pictures).'),
 ('pitch', 'This part\'s pitch, two octaves either way. "env" scales its pitch envelope. The pitch bar in Global moves both parts.'),
 ('LEGATO time', 'Left of centre: milliseconds (up to 1.5 s). Right: steps of the host tempo. "limit": portamento never slides further than this. Legato and portamento themselves are switched on in Global.'),
 ('lin / log, fix / var', 'Straight or curved slide; the same time for every slide, or longer for bigger jumps.'),
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
 ('env', 'How far the filter-frequency envelope reaches. Nothing happens until that envelope is switched on (right-click freq, Edit articulator, then the switch under the graph). The envelope works <b>below</b> the freq bar: at the top of the graph the filter sits where freq is, at the bottom it is lower by the env amount. So with freq fully open and env at 100% (how a new sound starts) the envelope is heard at once. A negative amount turns the graph upside down.'),
 ('width (right half)', 'Width of the resonance bump. "adapt" under it keeps it sounding the same up and down the keyboard.'),
 ('oct / hz', 'Whether the filter (left) and its resonance (right) are measured in octaves or in Hz.'),
 ('adapt (left)', 'Shortens the filter envelope for high notes.'),
 ('1 &gt; 2 mix', 'Right (ser): filter 1 feeds filter 2. Centre: filter 1 only. Left (par): both side by side, ending with filter 2 only.'),
 ('eq mix', 'How much of the Global EQ graph is applied. The "curves" button opens the global EQ (fixed frequencies) or the local EQ (follows the note) in the editor.'),
 ('Custom shape 1 / 2', 'Draw your own filter or resonance curve in the editor (target menu, Shaping).'),
])
figs(('eq', 'eq', '0% to 100% with a dip and a peak drawn in Global EQ. The EQ stays at fixed pitches whatever note you play.'), cols=2)

h1('Blur and pluck')
figs(('blur_harm', 'BLUR harm', '0% to 100% on a square. Level spills from each partial into the empty ones next to it.'),
     ('blur_time', 'BLUR attack and decay', 'A moving phaser, with time blur switched on part-way through. Changes are smeared in time.'),
     ('pluck', 'PLUCK time 45%', 'Three notes. High partials die first, like a plucked string.'),
     ('pluck_time', 'PLUCK time', 'The same note at 20%, 45% and 70%.'))
table([('top, bottom', 'Top tension limits how far blur spreads upwards. Bottom sets how loud the blurred partials are.'),
       ('blur decay', 'Gives the pluck a slower, softer tail.'),
       ('Pluck shape', 'In the editor (Shaping): which partials die fastest.')])

h1('Phaser')
p('A moving pattern of notches. Each picture is one held note with the phaser sweeping.')
figs(('ph_classic', 'Classic', ''), ('ph_triangle', 'Triangle', ''), ('ph_eggs', 'Eggs', ''), ('ph_deep', 'Deep', ''), ('ph_deeper', 'Deeper', ''),
     ('ph_condom', 'Condom', ''), ('ph_twins', 'Twins', ''), ('ph_cascade', 'Cascade', ''), ('ph_box', 'Box', ''), cols=3)
figs(('ph_width', 'width', '0% to 100%, not moving. The notches go from close together to far apart.'),
     ('ph_offset', 'offset', '0% to 100%, not moving. Slides the pattern by hand.'))
table([('speed', 'Centre is still. Right sweeps up, left sweeps down; further out is faster. Shown as the number of steps one sweep takes.'),
       ('kb', 'Shifts the pattern with the note.'), ('scale', 'Notches spaced in octaves, in Hz, or by harmonic number.'),
       ('lfo', 'Lets a slow LFO move the width.'), ('freq / custom', 'Freq bends the pitch of the partials with the Phaser shape graph; custom uses that graph as the notch shape.')])

h1('Prism and harmonizer')
figs(('prism_add', 'PRISM amount, Add', '-100% to +100%. Partials between the octaves are pushed off their places; the middle of the picture is 0%.'),
     ('prism_mul', 'PRISM amount, Multiply', 'The same, but the shift grows towards the top.'),
     ('hz_shift_plus', 'HARMONIZER shift step, +', 'Step 1 to 8. Copies of the sound are laid on at multiples of the step; watch which partials brighten.'),
     ('hz_shift_x', 'HARMONIZER shift step, x', 'Step 1 to 8 in multiply mode. The copies spread out faster.'),
     ('hz_gap', 'HARMONIZER gap step', 'Step 1 to 8. Only every n-th partial is copied and the ones between are turned down.'))
table([('from vol', 'Prism moves loud partials more (on) or quiet ones more (inv).'),
       ('amt, width, str', 'How much harmonizer, how high up it reaches, how strong the copies are.'),
       ('shift, gap: ofs', 'Moves the shift or gap pattern up in octaves.'),
       ('shift, gap: step', 'One box for the step and how it counts: drag up for adding steps (+1 to +8), down for multiplying ones (x1 to x8).')])

h1('Global, and the X / Y / Z pad')
shot('global', 'Global. The square is the X / Y pad; mod x, y, z are under it.')
p('<b>X, Y and Z are three hand controls that do nothing until you point them at something.</b> Then one move of the pad can change as many things as you like, each in its own way.')
out.append('<div class="grid c2"><figure><div class="im"><img src="img/pad.png"></div><figcaption><b>The pad.</b> A room seen from the front. Left to right is X, bottom to top is Y. Z brings the far wall nearer. The puck and the mod x, y, z bars under the pad move together, in the same frame, whether you drag the pad, a bar, automate them or play a MIDI controller.</figcaption></figure>'
           '<figure><div class="im"><img src="img/page_modx.png"></div><figcaption><b>A mapping.</b> Filter 1 frequency, Modulation X mapping. Left to right is the pad going from 0 to 100%; the line says where that puts the filter. The orange line is the pad\'s position now.</figcaption></figure></div>')
table([
 ('Move it', 'Drag in the pad for X and Y. Z: the mouse wheel over the pad, or drag up and down with the middle button (or with Alt held). The three small bars x, y, z do the same one at a time.'),
 ('Point it at something, the quick way', 'Right-click any bar that can be articulated and choose Quick map to X (or Y, Z). The pad then sweeps that control over its travel, and the mapping opens in the editor so you can redraw it.'),
 ('Point it at something, by hand', 'In the editor choose the target, then the part "Modulation X mapping" (or Y, Z), and draw. Editing the line switches the mapping on. Every target has its own three mappings, so X can open a filter, slow an LFO and pan the sound at once.'),
 ('Automate it', 'Modulation X, Y and Z are host parameters: draw them in your DAW or record the pad.'),
 ('MIDI', 'X follows controller 1 (the mod wheel), Y controller 74, Z controller 71. Right-click the pad or one of x, y, z for MIDI learn (then move the controller you want), to forget a controller, or to go back to these three. The choice is saved with the sound.'),
 ('Smoothing', 'Only the sound is smoothed (about 30 ms) so pad moves never click; the parameters and the window follow the pad at once. For hard, percussive jumps switch "smooth mod" off (right-click the pad, or the ADV tab).'),
 ('Mod X speed', 'One more mapping: how fast X is moving. A quick stroke across the pad can open something that a slow move leaves alone.'),
])
table([
 ('vol', 'Master volume, with the output meter under it.'),
 ('lfo', 'Scales every LFO of the part on show.'),
 ('legato, porta', 'Legato: overlapping notes bend one voice instead of starting new ones. Porta: every new note slides in from the last. Their time and curve are in the part\'s LEGATO section.'),
 ('limit', 'Legato only happens within this distance; a bigger jump starts a normal new note. Off: always legato.'),
 ('vel (next to legato)', 'Harder notes slide faster.'), ('vel (under lfo)', 'Velocity changes the volume.'),
 ('pre, post', 'Volume into and out of the effects.'), ('pitch', 'Pitch of the whole instrument, two octaves either way.'),
 ('strum', 'A chord pressed at once is spread out: direction (Up, Down, Alt, Random), time, and tension (left: speeding up, right: slowing down).'),
])

h1('The editor: envelopes, LFOs, mappings')
shot('page0', 'The volume envelope. D marks where the decay starts, S where the envelope waits while the key is held.')
p('This is where things move. Choose <b>what</b> to move in the first menu (the target), and <b>with what</b> in the second (the part): an envelope, an LFO, or one of nine mappings. Each target can have all of them switched on at once. The slanted box at the bottom left of the graph switches the part on show on and off; touching the graph switches it on for you.')
figs(('env_volume', 'Volume, Envelope', 'Slow attack, decay to a lower level, release after the key is let go.'),
     ('env_filter', 'Filter 1 frequency, Envelope', 'Two notes. The cutoff starts at the freq bar and falls by the env amount each time.'),
     ('lfo_filter', 'Filter 1 frequency, LFO', 'The cutoff moves up and down on its own.'),
     ('lfo_pitch', 'Pitch, LFO with tension 100%', 'A square LFO: the pitch jumps between two notes.'),
     ('map_key', 'Filter 1 frequency, Keyboard mapping', 'Four notes. A rising line makes higher notes brighter.'),
     ('arp', 'Volume envelope with arpeggiator breaks', 'A held four-note chord. Each marked point passes the sound to the next note up.'))
h2('The target menu')
table([
 ('Articulation', 'Things that can move during a note: volume, panning, filter frequency, pitch, phaser, prism and so on. Each has an envelope, an LFO and nine mappings.'),
 ('Mapping', 'Things set per note: envelope attack / decay / release scales, LFO speed and amount, unison phase, predelay. Mappings only.'),
 ('Shaping', 'One graph each: the partials of timbre 1 and 2, their phases, EQs, custom filter and resonance shapes, pluck shape, masks that keep an effect away from part of the range.'),
 ('&#9666; on', 'In both menus, next to everything that is switched on.'),
])
h2('An envelope')
table([
 ('Add a point', 'Right-click or double-click the background.'), ('Move a point', 'Drag it. Shift keeps its height, Ctrl or Cmd keeps its time.'),
 ('Delete a point', 'Alt-click it, or right-click it and choose Delete.'),
 ('Bend a line', 'Drag the small circle in the middle of a segment. Ctrl- or Cmd-drag for fine steps. Right-click the circle to straighten.'),
 ('Right-click a point', 'Segment type (hold, smooth, curves, stairs, pulse, wave...), decay start (D), sustain / loop end (S), loop start (L), arpeggiator break.'),
 ('D, S, L', 'Up to D is the attack, D to S the decay, after S the release. While the key is held the envelope waits at S, or goes round between L and S if there is an L.'),
 ('att, dec, sus, rel', 'Four modifiers under the graph. They leave your drawing alone and stretch or shrink its attack, decay and release (x0.06 to x16) and scale the level of the S point (x0 to x2). An orange line shows the envelope as it now plays, and the info box gives the resulting time. They are host parameters, one set for every envelope, so they can be automated.'),
 ('tempo / global', 'Tempo: times are in beats. Global: all notes share one envelope, restarted by each new note.'),
 ('smoothing (the bar next to the part menu)', 'Rounds off sudden jumps of the target.'),
 ('lock / step / snap / slide', 'Lock against changes; draw freehand, one point per grid step (Shift for flat steps); snap to the grid; moving a point moves everything after it.'),
 ('Zoom', 'The mouse wheel over the graph zooms the time axis; drag the strip under it to scroll, double-click the strip to see everything.'),
 ('options', 'Reset, flip, normalize, decimate, smooth up, scale levels, copy to the other part, copy / paste, make all loop, follow an audio file, create a step sequence. For shapes: draw per harmonic (bars).'),
 ('The white line', 'Where the newest note is in the envelope.'),
 ('Volume envelope', 'On from the start in every new sound: 2 ms attack, 300 ms decay, 100 % sustain, 150 ms release. Switched off, a note simply starts and stops with the key.'),
])
h2('An LFO')
shot('page_lfo', 'Filter 1 frequency, LFO. The small square at the top left is the handle.')
table([
 ('The handle', 'Drag it up and down for the amplitude; below the middle the wave is turned over. Drag it to the right to make the LFO start later. Right-click to add more points: the line then sets the amplitude over time (fade in, fade out).'),
 ('amt, spd, tns, skew, pw, phase', 'Amount, speed, and the shape: tension 0 is a triangle, 32% a sine, 100% a square; skew leans it into a saw; pw makes the top wider or narrower than the bottom. Touching any of them switches the LFO on.'),
 ('tempo / global', 'Speed in beats; one LFO shared by all notes.'),
 ('lfo in Global', 'Scales all LFOs together. "Global LFO amount / speed / phase" in the Mapping column let a key, a velocity, the pad or the unison voice change every LFO at once.'),
])
h2('The mappings')
table([
 ('Keyboard, Velocity', 'Left to right is low to high note, or soft to hard.'),
 ('Modulation X, Y, Z', 'The pad. See above.'),
 ('Random', 'A new random position along the graph for each note.'),
 ('Unison index', 'A value per unison voice: left is the first voice, right the last (the grid shows them). It works for every target: two voices an octave apart, one bright and one dark, each with its own LFO phase...'),
 ('Held index', 'First held note, second, third...'), ('Mod X speed', 'How fast X is moving.'),
])
h2('How a control and its graphs combine')
table([
 ('Volume, mixes, widths, amounts (unipolar product)', 'The control is the maximum. The graphs scale it down: top of the graph is the control\'s value, bottom is zero. So a control left at 0% gives nothing to scale: raise it first. The info box says so when that is the case.'),
 ('Prism, fx, filter mix, LFO amount (bipolar product)', 'The same, but the middle of the graph is zero and the bottom is the control\'s value turned negative.'),
 ('Pitch, panning, filter frequency, offsets (bipolar sum)', 'The control is the centre. Graphs add to it: the middle line is no change. For pitch the top is +4 octaves and the bottom -4.'),
 ('Filter frequency envelope', 'The exception: the top of the graph is "no change" and the filter\'s env bar sets how far below the freq bar the bottom reaches (a negative amount: how far the top reaches, upside down). Sounds saved before this version keep the earlier rule, in which the envelope was added above the freq bar.'),
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
 ('pre / post', 'Volume into and out of the effects: the two standing bars in Global.'),
])

h1('The ADV page')
shot('page3', 'The two orders, settings for the part on show, voices, quality, and the controllers of the pad.')
figs(('ramp', 'ramp, right (relative)', 'The first half second of a note. Partials come in from the bottom up.'), ('ramp_abs', 'ramp, left (absolute)', 'All partials fade in together.'))
table([
 ('delay', 'Predelay: waits before the note starts. Left: milliseconds. Right: tempo steps. Its mappings (target menu, Mapping, Predelay) can make it depend on the key, the velocity or the pad.'),
 ('auto-gain', 'Relative or Absolute, for the "auto" bar of the part.'),
 ('smooth mod', 'Smoothing of the pad (X, Y, Z).'), ('mod controllers', 'The MIDI controller of X, Y and Z. Click one for MIDI learn.'),
 ('poly rel', 'Below 100%, a new note shortens the release tails still sounding.'), ('declick', 'A very short fade at the start and end of notes.'),
 ('UNIT ORDER', 'The order the shaping stages run in. Click an entry to move it later, right-click to move it earlier. Example: put HARM PROT after HARM CLIP to keep the fundamental safe from clipping.'),
 ('EFFECTS ORDER', 'The same for the five effects.'),
 ('randomness', 'Whether random values are new per note, per part, per unison copy, or never change.'),
 ('side (a/b as stereo)', 'Load the same stereo sound into A and B: A carries the middle, B the sides, and the A/B mix sets the width.'),
 ('partials', 'Fewer partials uses less processor and loses the top end.'),
 ('precision', 'How often the shaping is recalculated: Average about every 5 ms, High 2.7 ms, Perfect 1.3 ms. "HQ when rendering" uses Perfect for offline bounces.'),
 ('VOICES mono', 'Off (16-voice polyphony, the default); a repeated note cuts itself (Kill same); any new note cuts all (Kill all); or sends them into release (Release all). Glide legato also makes overlapping notes share one voice; leave it off for chords. The voice count is shown at the bottom of the partial display.'),
])

h1('Good to know')
table([
 ('16 notes at once', 'Each with up to 9 unison copies of up to 516 partials. Low notes with high unison cost the most.'),
 ('It can get loud', 'High resonance with self-oscillation, or harmonizer and blur at full, add a lot of level. Auto-gain or the Limiting compressor help.'),
 ('Saved presets', 'Plain text files ending in .partials. Drop one on the window to load it.'),
 ('Older sessions', 'Every parameter that existed keeps its name, range and place; sounds saved with earlier versions load and sound as they did.'),
 ('Not in this version', 'Copying pictures through the clipboard, selecting and copy / paste of single line points, envelope state files, the envelope filter tool, frozen and bipolar-tension LFOs, and the step-sequence editing window (there is a menu of ready-made sequences instead).'),
])

css = '''
@font-face{font-family:Inter;src:url("file://" + FONTDIR + "/Inter-Regular.ttf");font-weight:400}
@font-face{font-family:Inter;src:url("file://" + FONTDIR + "/Inter-Medium.ttf");font-weight:600}
@font-face{font-family:Plex;src:url("file://" + FONTDIR + "/IBMPlexMono-Regular.ttf");font-weight:400}
@font-face{font-family:Plex;src:url("file://" + FONTDIR + "/IBMPlexMono-Medium.ttf");font-weight:600}
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
FONTDIR = os.path.abspath (os.path.join (HERE, '..', '..', 'Resources', 'fonts'))
css = css.replace ('" + FONTDIR + "', FONTDIR)
html = f'<!doctype html><meta charset="utf-8"><title>Partials manual</title><style>{css}</style>' + '\n'.join(out)
open(os.path.join(HERE, 'manual.html'), 'w').write(html)
missing = [l for l in out if 'src="' in l and not os.path.exists(os.path.join(HERE, l.split('src="')[1].split('"')[0]))]
print('missing pictures:', len(missing))
subprocess.run(['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                '--virtual-time-budget=5000', f'--print-to-pdf={HERE}/manual.pdf', 'file://' + os.path.join(HERE, 'manual.html')], stderr=subprocess.DEVNULL)

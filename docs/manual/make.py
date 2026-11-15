#!/usr/bin/env python3
"""Builds the faltung manual: manual.html (A4, the plug-ins' own look) → "faltung Manual.pdf" via Chrome.
Parameter tables come from params.json (faltungTest --dump-params), pictures from img/ (faltungTest --screenshots)."""
import html, json, os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
P = {p['code']: p for p in json.load(open(os.path.join(HERE, 'params.json')))}
FONTS = os.path.join(ROOT, 'Resources', 'fonts')
VERSION = '1.0.0'

GROUND = {'kt1': '#c9b53b', 'kt2': '#f4f2ec', 'bd1': '#b88a26', 'sd2': '#9cc788', 'rm3': '#8ec3e6', 'hh4': '#8d9bd6', 'cp6': '#a9bccf',
          'pc7': '#f6c590', 'cl8': '#eef6fa', 'tm9': '#76a582', 'mc3': '#ee8e6e', 'dl4': '#fbfbf8', 'rv5': '#f4f9fb', 'sw6': '#79e04a',
          'fl7': '#8593d4', 'gl8': '#dfe9ed', 'wb1': '#2f4a52', 'ms0': '#1a242c'}
e = html.escape

# ------------------------------------------------------------------------------------------------
#  words
# ------------------------------------------------------------------------------------------------
SLOT = {  # per drum engine: what each of the eight macro controls does
 'kick': {'tune': 'pitch the body settles on, 30–140 hz', 'decay': 'length of the body, 35 ms – 1.6 s',
          'click': 'the attack: a filtered noise burst and a short bright ping', 'sub': 'a sine an octave under the body with a longer tail',
          'tok': 'a wooden overtone well above the fundamental — the dry Perlon "tok"', 'sweep': 'how far the pitch falls at the start, up to 4 octaves',
          'bend': 'how fast it falls, 2–140 ms', 'drive': 'saturation after the engine (anti-aliased tanh)'},
 'snare': {'tune': 'pitch of the shell modes, 110–420 hz', 'decay': 'length of the wires (the shell is about half)', 'snap': 'a bright noise transient',
           'body': 'level of the tuned shell', 'noise': 'level of the wires', 'sweep': 'pitch drop of the shell at the hit', 'tone': 'brightness of the wires',
           'drive': 'saturation'},
 'rim': {'tune': 'fundamental of the struck object, 180 hz – 2.4 khz', 'decay': 'ring time (higher modes die sooner)', 'click': 'level of the stick itself',
         'body': 'tilts energy towards the fundamental (low = thin and clacky)', 'noise': 'a band of noise around the third mode',
         'sweep': 'a small pitch fall at the hit', 'hard': 'stick hardness: brightness and shortness of the excitation', 'drive': 'saturation'},
 'hat': {'tune': 'scales all six metallic oscillators (x0.5 – x2)', 'decay': 'length (the open hat has its own decay in hh4)', 'click': 'stick transient',
         'metal': 'level of the square-wave metal', 'air': 'level of noise', 'chirp': 'a falling pitch at the hit', 'tone': 'centre of the band-pass, 5–15 khz',
         'drive': 'saturation'},
 'clap': {'tune': 'centre of the band-pass, 500 hz – 3.2 khz', 'decay': 'length of the tail', 'snap': 'bright transient on the first burst',
          'claps': 'number of hands, 2–6', 'noise': 'a second, higher noise band', 'spread': 'time between the hands, 4–26 ms',
          'tone': 'narrowness of the band (high = more pitched)', 'drive': 'saturation'},
 'perc': {'tune': 'fundamental, 60 hz – 1.4 khz', 'decay': 'ring time', 'click': 'level of the strike', 'body': 'energy in the fundamental',
          'noise': 'noise around the third mode', 'sweep': 'membrane pitch glide after the hit', 'hard': 'mallet hardness', 'drive': 'saturation'},
 'click': {'tune': 'frequency of the click, 300 hz – 14 khz', 'decay': 'overall length, 0.8–80 ms', 'click': 'the raw impulse',
           'res': 'ring of the resonance', 'dust': 'density of crackle (and noise)', 'sweep': 'pitch fall (for blips)', 'tone': 'high-pass that thins the click',
           'drive': 'saturation'},
 'tom': {'tune': 'pitch, 45–420 hz', 'decay': 'length', 'click': 'a short bright strike', 'body': 'weight of the fundamental',
         'noise': 'noise band at twice the pitch', 'sweep': 'pitch fall', 'bend': 'speed of the fall', 'drive': 'saturation'},
}
MODELS = {
 'kick': [('tok', 'tight and dry, strong wooden overtone; the minimal kick'), ('thud', 'holds its body then lets go: deep, rounded'),
          ('909', 'sine pushed towards a triangle with a noisy click'), ('sine', 'pure sine with a very soft click')],
 'snare': [('tuned', 'three shell modes and filtered wires'), ('fm', 'a frequency-modulated shell, cracky'), ('brush', 'pink-noise wires that swell in, little shell'),
           ('808', 'two bridged modes and high-passed noise')],
 'rim': [('rim', 'rim-shot mode ratios'), ('wood', 'woodblock ratios, hollow'), ('tok', 'low and close together: the Villalobos tok'), ('metal', 'bell-like, long')],
 'hat': [('808', 'six band-limited squares at the classic ratios'), ('noise', 'resonant band-passed noise'), ('ring', 'an inharmonic fm pair'), ('tick', 'very short metal')],
 'clap': [('clap', 'a burst train then a tail'), ('snap', 'finger snap: one burst and a resonance'), ('room', 'clap with a small room'), ('tight', 'two hands, short')],
 'perc': [('conga', 'membrane modes, low'), ('bongo', 'membrane, higher and brighter'), ('bell', 'inharmonic, long'), ('glass', 'clear, high, inharmonic')],
 'click': [('tick', 'an impulse into a resonance'), ('pop', 'one windowed sine cycle'), ('dust', 'random sparse impulses — vinyl crackle'), ('blip', 'a sine with a very fast pitch drop')],
 'tom': [('tom', 'sine leaning to triangle'), ('blip', 'pure sine, quick fall: the minimal blip'), ('fm', 'fm tom, woody'), ('low', 'extra sub, long')],
}
COMMON = {'model': 'which engine variant (see the models above)', 'level': 'output level, off … +6 db', 'pan': 'stereo position',
          'vel': 'how much velocity changes level and brightness', 'keytrack': 'notes change the pitch (c3 = as tuned)',
          'choke': 'a new hit cuts the previous one (mono)', 'open': 'decay of the open hat (a# notes)'}

FX = {
 'mc3': {'mode': 'cut: repeats the latest slice of input for the coming step · grain: scattered grains from the recent past · click: very short grains, several per step',
         'division': 'the grid that grains fire on, synced to the host', 'density': 'chance that something fires on each step', 'size': 'length of a grain or cut (click mode maps this to 0.6–14 ms)',
         'pitch': 'replay pitch of the grains', 'scatter': 'how far back in the last 2 seconds grains are taken from', 'reverse': 'chance a grain plays backwards',
         'shape': 'grain edges, hard cut … smooth', 'hold': 'stop recording: keep cutting what is already in memory', 'mix': 'level of the grains (cut mode ducks the dry signal while it repeats)'},
 'dl4': {'sync': 'time follows the host tempo', 'division': 'synced echo time', 'time': 'free echo time in ms', 'feedback': 'repeats; above 100% it builds up (the loop saturates instead of exploding)',
         'age': 'tape age: darker, more saturation, more flutter', 'wow': 'slow pitch wander', 'hp': 'high-pass inside the loop — each repeat gets thinner', 'lp': 'low-pass inside the loop — each repeat gets darker',
         'spread': 'from a mono echo to full ping-pong', 'mode': 'tape (saturation + wow) · bbd (darker bucket-brigade) · clean (digital)',
         'freeze': 'stop input, hold the loop at unity: an infinite dub loop', 'input': 'send level into the delay (automate it for dub throws)', 'mix': 'dry / echo balance'},
 'rv5': {'model': 'room · plate (denser, no early reflections) · air (tiny, bright) · chamber (large)', 'size': 'size of the space', 'decay': 'time to fall 60 db',
         'predelay': 'gap before the reverb', 'damp': 'high frequencies die faster', 'lowcut': 'high-pass on the reverb input', 'diffusion': 'how quickly echoes smear into a wash',
         'mod': 'slow movement in the tail (chorus-like, removes ringing)', 'gate': 'closes the tail when the input stops; the value is the threshold',
         'width': 'stereo width of the tail', 'mix': 'dry / reverb balance'},
 'sw6': {'template': 'straight · mpc (swung offbeats) · shuffle (swung, 2 and 4 laid back) · push (offbeats early) · drag (everything but the downbeats late) · drunk (random)',
         'swing': 'amount of offbeat swing used by the templates', 'amount': 'scales the whole groove, 0–200%', 'humanize': 'random timing per step, in ms',
         'accent': 'level accents that come with each template', 'xfade': 'length of the crossfade where a step switches its delay', 'step': 'offset for this 16th, in % of a 16th (positive = later)'},
 'fl7': {'low': 'low band level; at the bottom it is a kill', 'mid': 'mid band level', 'high': 'high band level', 'xlow': 'crossover between low and mid', 'xhigh': 'crossover between mid and high',
         'sweep': 'one-knob filter: left = low-pass closing, centre = off, right = high-pass opening', 'res': 'resonance of the sweep filter', 'drive': 'saturation before the sweep filter',
         'lfodiv': 'lfo rate, synced', 'lfodepth': 'how far the lfo moves the sweep', 'lfoshape': 'sine · triangle · square · sample & hold', 'mix': 'dry / filtered balance'},
 'gl8': {'threshold': 'level where compression starts', 'ratio': '1.5, 2, 4 or 10 to 1', 'attack': 'how fast it clamps down', 'release': 'how fast it lets go (when auto is off)',
         'auto': 'program-dependent release: fast on transients, slow on sustained material', 'schp': 'high-pass on the detector so the kick does not pump everything',
         'makeup': 'gain after compression', 'tape': 'tape saturation amount', 'bump': 'low head-bump that comes with the tape', 'hf': 'high-frequency loss of the tape',
         'bias': 'asymmetry: adds even harmonics', 'mix': 'parallel compression: dry / processed', 'out': 'output level'},
}

LOOP = {'slices': 'how many slices the loop is cut into', 'slicemode': 'grid: equal slices · onsets: slices start on detected hits',
        'start': 'skips the beginning of the loop', 'pitch': 'replay pitch', 'gate': 'how much of each slice plays', 'attack': 'fade-in of each slice',
        'release': 'fade-out of each slice (also the crossfade when slices cut each other)', 'tone': 'low-pass on the slices', 'revprob': 'chance a step plays reversed',
        'spread': 'random stereo placement per hit', 'level': 'output level', 'vel': 'velocity sensitivity', 'fit': 'replay speed follows the host tempo',
        'loopbeats': 'how many beats the loop is (set automatically when you load)'}
SEQ = {'outs': 'separate outputs: each voice to its own output 2–9 (VST3 does this by itself when you route an output; AU / VST2 hosts need this switch on, otherwise everything plays on the main pair)', 'run': 'internal clock (standalone, or when the host is stopped)', 'bpm': 'internal tempo', 'swing': 'offbeat 16ths later, 50% = straight',
       'micro': 'how far the per-step microtiming reaches (± ms)', 'pattern': 'current pattern a1–a8 — automate it to arrange', 'master': 'output level',
       'len': 'pattern length of the track in steps (different lengths = polymeter)', 'prob': 'probability multiplier for every step of the track',
       'shift': 'moves the whole track earlier or later in ms', 'mute': 'silences the track'}

WB = {'chord': 'one key plays a whole chord: fifth, octave, major, minor, 7ths, 9ths, minor 11, sus 4, 6/9 …', 'voicing': 'close; open (second tone up an octave); drop 2',
      'shape': 'waveform: sine → triangle → saw → square → hard (saw × square); in-between values blend', 'pw': 'pulse width of the square',
      'oct': 'octave', 'semi': 'semitones', 'fine': 'cents', 'level': 'level into the filter', 'uni': 'unison copies (1–7)',
      'detune': 'spread of the unison copies in pitch (up to ±60 cents)', 'spread': 'spread of the unison copies in stereo',
      'fm': 'osc b phase-modulates osc a (set b level to 0 to hear only the result)', 'sub': 'clean sub level — it bypasses filter and distortion',
      'subwave': 'sine or rounded square', 'suboct': 'sub octave relative to the note', 'noise': 'white noise level',
      'ftype': 'lp24 · lp12 · bp · hp · notch', 'cutoff': 'filter cutoff', 'res': 'resonance', 'fdrive': 'drive into the filter', 'keytrack': 'cutoff follows the note',
      'vowel': 'formant vowel a – e – i – o – u (in between = morph)', 'fmtq': 'sharpness of the formants', 'fmtshift': 'moves all formants up or down',
      'fmtmix': 'formant filter amount', 'combtune': 'comb pitch relative to the note', 'combfb': 'comb feedback (negative = hollow, odd harmonics)',
      'combmix': 'comb amount', 'dmode': 'soft · fold (wavefolder) · hard · tube (asymmetric) · crush (bits + rate) · smooth', 'ddrive': 'distortion drive',
      'dtone': 'low-pass after the distortion', 'dmix': 'dry / distorted', 'order': 'filter then distortion, or distortion then filter',
      'wdiv': 'wobble rate, synced to the host (1 bar … 1/64, dotted, triplets)', 'wshape': 'sine · triangle · ramp down · ramp up · square · wow (fast open, slow close) · yoy (big + small hump) · random',
      'wretrig': 'each new note restarts the wobble at the phase below; off = locked to the bar', 'wphase': 'start point of the wobble cycle',
      'wcut': 'wobble → cutoff, in octaves', 'wvowel': 'wobble → vowel (the "yoy")', 'wfm': 'wobble → fm amount', 'wdrive': 'wobble → distortion drive',
      'wpitch': 'wobble → pitch, semitones', 'wamp': 'wobble → volume (gating)', 'wcomb': 'wobble → comb pitch',
      'ma': 'mod envelope attack', 'md': 'mod envelope decay', 'ms': 'mod envelope sustain', 'mr': 'mod envelope release',
      'mcut': 'envelope → cutoff, octaves', 'mpitch': 'envelope → pitch (drops and rises)', 'mfm': 'envelope → fm', 'mvowel': 'envelope → vowel',
      'aa': 'amp attack', 'ad': 'amp decay', 'as': 'amp sustain', 'ar': 'amp release', 'ott': 'three-band upward + downward compression amount',
      'otttime': 'speed of the ott detectors', 'voices': 'mono (one voice, note priority = last) or poly (8 voices)', 'legato': 'overlapping notes glide without restarting envelopes',
      'glide': 'portamento time', 'bend': 'pitch-bend range', 'wheel': 'what the mod wheel does: off · wobble depth · wobble rate (faster divisions)',
      'vel': 'velocity sensitivity', 'width': 'stereo width above 140 hz (below is always mono)', 'level': 'output level'}

INTRO = {
 'kt1': "Eight synthesized voices and a 16-step sequencer built for the loose, shuffled grooves of 2000s minimal: every step can sit early or late, play only sometimes, or ratchet; every track can run its own length, so patterns phase against each other.",
 'kt2': "A loop slicer. Drop in any break or recording, cut it into slices on a grid or on its hits, and sequence the slices — reversed, re-pitched, gated — the Akufen / early-Perlon cut-up. It starts with a break rendered by kt1.",
 'mc3': "Live micro-sampling of whatever passes through it. On every step of a host-synced grid it may repeat the last slice (cut), scatter grains from the last two seconds (grain), or turn the input into tiny clicks (click).",
 'dl4': "A dub echo with the filters, saturation and wow inside the feedback loop, so every repeat is darker, thinner and wobblier than the last. Push feedback past 100% and the loop saturates rather than exploding. Freeze holds the loop forever.",
 'rv5': "An eight-line feedback delay network with early reflections — dense, smooth, no metallic ring — in four spaces, with a gate that shuts the tail when the input stops.",
 'sw6': "A groove box for audio. Each 16th of the incoming audio is read from a delay line with its own offset, with crossfades placed just before the grid where drums are quiet. Templates, sixteen step offsets, humanize, accents, and learn: play a loop and sw6 measures where its hits fall.",
 'fl7': "A DJ isolator: three Linkwitz-Riley bands that sum flat, each with a kill, plus the one-knob filter — low-pass to the left, high-pass to the right — with resonance, drive and a synced lfo.",
 'gl8': "A bus compressor with a program-dependent release and a sidechain high-pass, followed by tape: bias, head bump and high-frequency loss. Mix it in parallel for drum crush.",
 'wb1': "A dubstep and neuro bass synth after the DMZ and Hench era (Skream, Loefah, Rusko) and the neuro of Noisa, Current Value and Billain: wobbles, the formant \"yoy\", reese, fm growl.",
}
TIPS = {
 'mc3': ["Put it on a percussion bus at <b>grain</b>, rate 1/16, density 30%, mix 30%: a halo of micro-edits around the beat.",
         "<b>cut</b> at 1/32 with density 20% gives occasional stutters; automate <i>density</i> to 100% for a fill.",
         "<b>click</b> on a vocal or pad turns it into a crackle of percussion at the same pitch."],
 'dl4': ["Classic dub throw: mix 100%, automate <i>send</i> up for one hit and back down.",
         "3/16 against a straight 4/4 rim is the basic Basic Channel / Perlon echo.",
         "Feedback 102%, age high, hp 400 hz, then ride <i>loop lp</i>: the repeats build and darken but never clip."],
 'rv5': ["<b>air</b> with gate 50% on snares and claps: the short bright room of 2000s minimal.",
         "Low cut 300 hz keeps the reverb off the kick."],
 'sw6': ["Put sw6 on a straight drum loop; template <b>shuffle</b>, swing 58%, accent 50%.",
         "<b>learn groove</b>: click it, play a loop with the feel you want for a few bars, click again. The steps now hold that feel; apply it to other tracks.",
         "sw6 delays its output by 70 ms; the host compensates automatically."],
 'fl7': ["Kill the low band on the build, bring it back on the one.", "Automate <i>filter</i> slowly across 16 bars with resonance around 0.5."],
 'gl8': ["On the drum bus: ratio 2, attack 10 ms, auto release, 2–3 db of reduction, tape 0.25.",
         "Drum crush: ratio 10, attack 0.3 ms, makeup up, mix 40%."],
}
WB_RECIPES = [
 ("classic wobble", "osc a saw, lp24 cutoff ~200 hz, resonance 0.35, wobble sine, wob > cutoff +4.5 oct, retrigger on. Change <i>wobble rate</i> between notes (automate it) — 1/8 for the main line, 1/16 and 1/8 t for variations."),
 ("the yoy", "formant mix 100%, wobble shape <b>yoy</b>, wob > vowel +100%. Add env > pitch −5 st with decay 180 ms for the downward slur."),
 ("wow", "wobble shape <b>wow</b> at 1/4, wob > vowel +85% and wob > cutoff +2.5 oct: each cycle opens fast and closes like a mouth."),
 ("sub pressure", "sub 100% sine at −1 octave, osc a sine at low level, no wobble. Keep the main oscillators above the sub with osc a +1 octave if they muddy it."),
 ("reese", "osc a saw, unison 6–7, detune 40–50%, spread 80%; osc b saw −1 octave unison 4. Distortion <b>smooth</b>, a little comb, ott 60%. Slow wob > cutoff or nothing."),
 ("fm growl", "both oscillators sine, osc b +1 octave at level 0, fm 40%, wob > fm 30% at 1/16, fold distortion, formant mix 30%, ott 60–80%."),
 ("neuro stack", "start from <b>neuro growl</b>; route wobble to vowel, comb and drive together; then resample wb1 into kt2 loop and cut it up."),
]

# ------------------------------------------------------------------------------------------------
#  helpers
# ------------------------------------------------------------------------------------------------
def ptable(rows):
    out = ['<table class="params"><tr><th>control</th><th>range</th><th>default</th><th>what it does</th></tr>']
    for name, rng, d, desc in rows:
        out.append(f'<tr><td class="n">{e(name)}</td><td class="r">{e(rng)}</td><td class="d">{e(d)}</td><td>{desc}</td></tr>')
    out.append('</table>')
    return ''.join(out)

def rng_of(p):
    if p['type'] == 'choice': return p['choices']
    if p['type'] == 'bool': return 'off / on'
    return f"{p['min']} … {p['max']}"

def params_rows(code, lookup, skip=lambda pid: False, rename=lambda p: p['name']):
    rows = []
    for p in P[code]['params']:
        if skip(p['id']): continue
        rows.append((rename(p), rng_of(p), p['def'], lookup(p['id'])))
    return rows

def shot(code, cls='shot'):
    return f'<img class="{cls}" src="img/{code}.png" alt="{code}">'

def presets(code):
    return '<p class="presets"><span>presets</span> ' + ' · '.join(e(n) for n in P[code]['presets']) + '</p>'

def chevrons(colour='#9fd3ec', n=6):
    s = ['<svg class="chev" viewBox="0 0 800 600" preserveAspectRatio="xMidYMid slice">']
    for i in range(n):
        x, y = 160 + i * 90, 360 - i * 70
        s.append(f'<polygon points="{x},{y+300} {x+130},{y+300} {x+260},{y+150} {x+130},{y} {x},{y} {x+130},{y+150}" fill="{colour}" opacity="{0.28 + 0.08*(i%2)}"/>')
    s.append('</svg>')
    return ''.join(s)

def wire(colour, white='#fff'):
    return f'''<svg class="wire" viewBox="0 0 600 300"><g fill="none" stroke="{colour}" stroke-width="1.2">
<polyline points="40,250 120,40 300,90 480,70 580,170 540,260 300,285 40,250"/><polyline points="120,40 220,230 300,90"/>
<polyline points="220,230 480,70"/><polyline points="220,230 300,285"/><polyline points="480,70 540,260"/><polyline points="90,140 400,60 580,170"/></g>
<g fill="none" stroke="{white}" stroke-width="2.4"><polygon points="150,120 210,122 212,190 154,188"/><polygon points="390,190 450,176 446,224 388,228"/></g></svg>'''

def divider(title, sub, colour, art, ink='#1d1c15', head='#ffffff'):
    return f'<section class="divider" style="background:{colour};color:{ink}">{art}<div class="dt"><h1 style="color:{head}">{e(title)}</h1><p>{sub}</p></div></section>'

# ------------------------------------------------------------------------------------------------
#  document
# ------------------------------------------------------------------------------------------------
doc = []
add = doc.append

add(f'''<section class="cover">{chevrons()}
<svg class="cube" viewBox="0 0 104 112"><polygon points="0,30 52,0 104,30 52,60" fill="#ffe15a"/><polygon points="0,30 52,60 52,112 0,82" fill="#f2c100"/>
<polygon points="52,60 104,30 104,82 52,112" fill="#d8a800"/><text x="52" y="35" text-anchor="middle" font-size="12" font-weight="600" fill="#2a2100" font-family="Inter">faltung</text></svg>
<div class="ct"><h1>faltung</h1><p class="sub">drums, processes and bass for minimal, microhouse and dubstep</p>
<p class="ver">manual · version {VERSION} · november 2026</p></div>
<div class="tl">''' + ''.join(f'<div><span class="chip" style="background:{GROUND[c]}"></span><span>01 : {i:02d} : {(i*17)%60:02d}</span><b>{c}</b><span>{e(P[c]["name"])}</span><span>{e(P[c]["blurb"])}</span></div>'
                      for i, c in enumerate(['kt1', 'kt2', 'bd1', 'sd2', 'rm3', 'hh4', 'cp6', 'pc7', 'cl8', 'tm9', 'mc3', 'dl4', 'rv5', 'sw6', 'fl7', 'gl8', 'wb1', 'ms0'])) + '</div></section>')

add('''<section class="page"><h2>contents</h2><ol class="toc">
<li><a href="#intro">introduction</a></li><li><a href="#install">installing</a></li><li><a href="#using">using the controls</a></li>
<li><a href="#auto">automation in ableton</a></li><li><a href="#midi">midi</a></li>
<li><a href="#kt1">kt1 kit</a></li><li><a href="#kt2">kt2 loop</a></li><li><a href="#modules">the drum modules</a> — bd1 sd2 rm3 hh4 cp6 pc7 cl8 tm9</li>
<li><a href="#fx">the processes</a> — mc3 dl4 rv5 sw6 fl7 gl8</li><li><a href="#wb1">wb1 wub</a></li><li><a href="#ms0">ms0 master</a></li><li><a href="#recipes">recipes</a></li><li><a href="#tech">technical notes</a></li></ol>
<h2 id="intro">introduction</h2>
<p>faltung is a set of eighteen instruments and effects for the sound of 2000s minimal and microhouse — Perlon, [a:rpia:r], Villalobos, the Romanian
labels — and for the bass of early dubstep and neuro. Everything is synthesized or processed live; there are no sample libraries.</p>
<p>The drums come twice: as <b>kt1 kit</b>, a drum machine with eight voices and a sequencer, and as eight single <b>modules</b> (bd1 … tm9) that
you play from a midi clip or a drum rack. They share one synthesis engine, so a sound you design in a module behaves identically in the kit.
The <b>processes</b> are the effects that make that music move: micro-sampling, dub echo, small rooms, groove, the dj filter and bus glue.
<b>wb1 wub</b> is a bass synth for wobbles, formants and reese, and <b>ms0 master</b> a linear-phase mastering bus.</p>
<p>The look follows record sleeves of the period — Mark Fell's flat colour fields and wireframe solids, snd's folded pastel strips, Oval's coloured
lines, stdio's chevrons and tracklists. The pictures are not decoration: they are drawn from the current settings and move as you play.</p>
<table class="params"><tr><th>id</th><th>name</th><th>type</th><th>what it is</th></tr>''' +
''.join(f'<tr><td class="n">{c}</td><td>{e(P[c]["name"])}</td><td>{t}</td><td>{e(P[c]["blurb"])}</td></tr>' for c, t in
        [('kt1', 'drum machine'), ('kt2', 'drum machine'), ('bd1', 'module'), ('sd2', 'module'), ('rm3', 'module'), ('hh4', 'module'), ('cp6', 'module'),
         ('pc7', 'module'), ('cl8', 'module'), ('tm9', 'module'), ('mc3', 'effect'), ('dl4', 'effect'), ('rv5', 'effect'), ('sw6', 'effect'),
         ('fl7', 'effect'), ('gl8', 'effect'), ('wb1', 'synth'), ('ms0', 'mastering')]) + '</table></section>')

add('''<section class="page"><h2 id="install">installing</h2>
<p>Each plug-in comes as VST3, Audio Unit and VST2, plus a standalone app. On this Mac they are already installed:</p>
<table class="params"><tr><th>format</th><th>folder</th></tr>
<tr><td class="n">VST3</td><td>~/Library/Audio/Plug-Ins/VST3/faltung kt1.vst3 …</td></tr>
<tr><td class="n">Audio Unit</td><td>~/Library/Audio/Plug-Ins/Components/faltung kt1.component …</td></tr>
<tr><td class="n">VST2</td><td>~/Library/Audio/Plug-Ins/VST/faltung kt1.vst …</td></tr>
<tr><td class="n">apps</td><td>/Applications/faltung/faltung kt1.app …</td></tr></table>
<p>To reinstall after building from source, run <code>scripts/install-mac.sh</code> in the faltung folder. In Ableton Live, open
<i>Settings → Plug-Ins</i>, make sure <i>Use VST3 Plug-In System Folders</i> (or Audio Units) is on, and press <i>Rescan</i>. The plug-ins appear
under <b>FreakiePie</b> as <i>faltung kt1</i>, <i>faltung bd1</i> and so on. The drum machines, modules and wb1 are instruments; mc3 … gl8 are audio effects.</p>
<p>In the standalone apps there is no host clock: switch on <b>run</b> in kt1 or kt2 to start the internal sequencer, and set the tempo with <b>internal bpm</b>.
Choose the audio and midi devices with the options button at the top of the window.</p>

<h2 id="using">using the controls</h2>
<table class="params"><tr><th>control</th><th>how</th></tr>
<tr><td class="n">slider</td><td>a label, a thin rule, a slanted bar and a white edge mark. Drag left/right (or up/down) anywhere on it. <b>shift</b>-drag for fine moves,
<b>double-click</b> for the default, scroll wheel for small steps. Bipolar controls (pan, shift, offsets) fill from the centre.</td></tr>
<tr><td class="n">choice</td><td>click for a menu; drag up/down to step through.</td></tr>
<tr><td class="n">toggle</td><td>the small parallelogram; filled = on.</td></tr>
<tr><td class="n">presets</td><td>top right: <b>&lt;</b> and <b>&gt;</b> step through, click the name for the list. A preset sets every control; anything it does not
mention returns to its default. The same list appears in the host's program menu.</td></tr>
<tr><td class="n">picture</td><td>in the modules, click the picture to play the sound (hh4: left half closed, right half open). In kt1 click a strip; in kt2 click a slice.</td></tr>
<tr><td class="n">size</td><td>drag the bottom-right corner; the window scales between half and double size.</td></tr></table>
<p>Hover over any control for its full name.</p>

<h2 id="auto">automation in ableton</h2>
<p>Every control in every faltung plug-in is a host parameter, with a fixed id and a readable name, so nothing has to be "configured" first.
In Arrangement view, show the automation lane of the track, choose the device in the first chooser and the control in the second —
or simply move the control while recording automation. All moves are smoothed in the audio engine, so automation never zips or clicks.</p>
<ul><li>In <b>kt1</b> the names start with the voice: <i>bd1 decay</i>, <i>rm3 tune</i>, <i>hh4 length</i>, <i>cp6 mute</i>. That is 134 parameters, sorted by voice.</li>
<li><b>pattern</b> (a1–a8) is a parameter: automate it to arrange a whole track from one kt1 or kt2.</li>
<li>Track <b>mute</b>, <b>length</b>, <b>probability</b> and <b>shift</b> are parameters: thin a groove out, or push the hats later, over 32 bars.</li>
<li>In <b>sw6</b> each of the sixteen step offsets is a parameter (<i>step 1</i> … <i>step 16</i>).</li>
<li>In <b>wb1</b> automate <i>wobble rate</i> to change the wobble speed per note, the way the early dubstep tracks were written.</li>
<li>Step patterns themselves (which steps are on) are saved with the set, not automated — use patterns a1–a8 for changes.</li></ul>

<h2 id="midi">midi</h2>
<table class="params"><tr><th>plug-in</th><th>notes</th></tr>
<tr><td class="n">kt1</td><td>C1–G1 (36–43) play bd1, sd2, rm3, hh4, oh5, cp6, pc7, cl8 — the bottom row of a Push or a drum rack. The sequencer follows the host transport.</td></tr>
<tr><td class="n">kt2</td><td>C1 and up play slices 0, 1, 2 … The sequencer follows the host transport.</td></tr>
<tr><td class="n">modules</td><td>any note plays. With <b>key tracking</b> on the pitch follows the note (C3 = as tuned). hh4: A# notes (A#1 = 46, A#2 = 58 …) play the open hat, all others closed; closed chokes open.</td></tr>
<tr><td class="n">wb1</td><td>notes, pitch bend (range set by <i>bend range</i>), mod wheel (cc 1, its job set by <i>mod wheel</i>), all-notes-off.</td></tr></table>
<p>Ableton numbers middle C as C3; faltung uses the same names.</p></section>''')

# ---- kt1 ---------------------------------------------------------------------------------------------
add(divider('drum machines', 'kt1 kit · kt2 loop', GROUND['kt1'], wire('#e7d873')))
def kt1_rows():
    rows = []
    for p in P['kt1']['params']:
        pid = p['id']
        if pid.startswith('v') and '_' in pid:
            if not pid.startswith('v3_'): continue
            key = pid.split('_', 1)[1]
            name = p['name'].replace('rm3 ', '<voice> ')
            desc = SEQ.get(key) or SLOT['rim'].get(key) or COMMON.get(key, '')
            if key in SLOT['rim'] and key != 'tune' and key != 'decay': desc = 'see the voice\'s module (rm3: ' + SLOT['rim'][key] + ')'
            if key == 'tune': desc = 'pitch of the voice (range depends on the voice)'
            if key == 'decay': desc = 'length of the voice'
            rows.append((name, rng_of(p), p['def'], desc))
        else:
            rows.append((p['name'], rng_of(p), p['def'], SEQ.get(pid, '')))
    return rows

add(f'''<section class="page" id="kt1"><h2>kt1 kit</h2><p class="lead">{INTRO['kt1']}</p>{shot('kt1')}
<h3>the page</h3>
<p><b>Top left</b>, the eight voices as folded strips. Click one to select it and hear it; it flashes on every hit. <b>Right</b>, the selected voice:
its model and eight sound controls (the same as the matching module), level, pan and velocity, then its track settings — length, probability,
shift and mute. <b>Bottom</b>, the step grid with a white bar under each track's current step, and the global row: run, internal bpm, swing,
micro range, the pattern tabs a1–a8 and copy / paste / clear.</p>
<h3>the voices</h3>
<table class="params"><tr><th>voice</th><th>engine</th><th>midi</th><th>output</th></tr>
<tr><td class="n">bd1</td><td>kick</td><td>C1</td><td>2</td></tr><tr><td class="n">sd2</td><td>snare</td><td>C#1</td><td>3</td></tr>
<tr><td class="n">rm3</td><td>rim</td><td>D1</td><td>4</td></tr><tr><td class="n">hh4</td><td>hat (closed, chokes oh5)</td><td>D#1</td><td>5</td></tr>
<tr><td class="n">oh5</td><td>hat (open)</td><td>E1</td><td>6</td></tr><tr><td class="n">cp6</td><td>clap</td><td>F1</td><td>7</td></tr>
<tr><td class="n">pc7</td><td>perc</td><td>F#1</td><td>8</td></tr><tr><td class="n">cl8</td><td>click</td><td>G1</td><td>9</td></tr></table>
<h3>editing steps</h3>
<table class="params"><tr><th>gesture</th><th>does</th></tr>
<tr><td class="n">click</td><td>step on / off</td></tr><tr><td class="n">drag up / down on a step</td><td>velocity — shown as the strength of the colour</td></tr>
<tr><td class="n">drag across</td><td>paint steps on (or off, if you started on a lit step)</td></tr>
<tr><td class="n">shift-drag</td><td>microtiming: the step moves early or late, up to ± the <i>micro range</i>; you see it shift sideways</td></tr>
<tr><td class="n">alt-click</td><td>probability 100 → 75 → 50 → 25%: the step is drawn partly filled</td></tr>
<tr><td class="n">right-click</td><td>ratchet 1 → 2 → 3 → 4 hits inside the step (thin lines)</td></tr>
<tr><td class="n">click a row name</td><td>select and play that voice</td></tr></table>
<p>Steps past a track's length are dimmed. Give tracks different lengths — hats 16, rim 15, perc 12 — and the pattern slowly turns over itself,
which is most of the trick of long minimal grooves. The factory patterns already place offbeat hats and percussion a little late and some rims early.</p>
<h3>separate outputs in ableton</h3>
<p>kt1 has a main stereo output and eight more, one per voice. In Live, put kt1 on a midi track, create audio tracks, set their <i>audio from</i>
to the kt1 track and choose <i>bd1</i>, <i>sd2</i> … in the second chooser. A voice whose output is in use leaves the main mix.</p>
<h3>controls</h3>{ptable(kt1_rows())}{presets('kt1')}</section>''')

# ---- kt2 ---------------------------------------------------------------------------------------------
def loop_desc(pid): return LOOP.get(pid) or SEQ.get(pid, '')
add(f'''<section class="page" id="kt2"><h2>kt2 loop</h2><p class="lead">{INTRO['kt2']}</p>{shot('kt2')}
<h3>loading</h3><p>Drag an audio file (wav, aiff, flac, mp3, ogg; up to 60 s) onto the waveform, or use <b>load sample…</b>. kt2 guesses the length in
beats from the current tempo (<i>loop length</i>); correct it if needed. The sample is stored inside the project (up to 40 s), so the set opens on another computer.</p>
<h3>slices</h3><p>The black dots under the waveform are the slice starts, numbered 0, 1 … f. <b>grid</b> cuts equal slices; <b>onsets</b> starts each slice on a
detected hit, which keeps transients intact on loose breaks. Click a slice to hear it.</p>
<h3>the step row</h3><p>Each lit step shows the slice it plays (hex digit). Click to turn a step on (it takes the slice with the same number), drag up/down to choose another slice,
<b>alt-click</b> to reverse (<i>&lt;</i>), <b>shift-drag</b> for microtiming, right-click for ratchets. Slices are monophonic: a new one fades out the last over the release time.</p>
<h3>controls</h3>{ptable(params_rows('kt2', loop_desc))}{presets('kt2')}</section>''')

# ---- modules -------------------------------------------------------------------------------------------
add(divider('the drum modules', 'bd1 kick · sd2 snare · rm3 rim · hh4 hats · cp6 clap · pc7 perc · cl8 click · tm9 tom', GROUND['bd1'], wire('#e3bf5f')))
add('''<section class="page" id="modules"><h2>the drum modules</h2>
<p class="lead">Each module is one kt1 voice as its own instrument, for drum racks, midi clips and separate processing. Every engine has four models and
the same eight macro controls in the same places — tune, decay, a transient, two tone controls, a pitch sweep, a tone/shape control and drive —
named for what they do in that engine. All engines are calibrated to similar loudness, so changing models does not jump in level.</p>
<p>Common to every module: <b>level</b>, <b>pan</b>, <b>velocity</b> (how much velocity changes level and brightness), <b>key tracking</b> and <b>choke</b>.
The picture at the top plays the sound when clicked.</p></section>''')
MOD = [('bd1', 'kick'), ('sd2', 'snare'), ('rm3', 'rim'), ('hh4', 'hat'), ('cp6', 'clap'), ('pc7', 'perc'), ('cl8', 'click'), ('tm9', 'tom')]
MODTXT = {
 'bd1': 'The kick of minimal is short, dry and pitched. Start from <b>tok</b>, keep decay under 200 ms, add <i>tok</i> for the wooden knock and <i>sub</i> only if the track is otherwise empty below 60 hz.',
 'sd2': 'Snares are rare in minimal; when they appear they are tuned and short. <b>brush</b> with long decay makes the shuffling noise of a lot of Perlon records.',
 'rm3': 'The most important sound of the style. <b>tok</b> with tune around 400–600 hz and hard 0.5 is the Villalobos rim; detune two instances against each other for call and answer.',
 'hh4': 'Closed hats on any note, the open hat on A# notes; the closed hat cuts the open one. <i>open decay</i> sets the open length.',
 'cp6': 'The clap is a train of 2–6 short noise bursts, then a tail. <i>spread</i> sets how loose the hands are.',
 'pc7': 'Modal percussion: congas and bongos are membranes, bell and glass are inharmonic. With key tracking on, play it melodically.',
 'cl8': 'The smallest sounds: ticks, pops, crackle. Put it on 16ths at low level with probability and microtiming for the "micro" in microhouse.',
 'tm9': 'Toms and the minimal blip — <b>blip</b> with sweep 0.55 is the short descending sine of a thousand Romanian tracks.',
}
for code, t in MOD:
    def d(pid, t=t):
        if pid in SLOT[t]: return SLOT[t][pid]
        return COMMON.get(pid, '')
    models = ''.join(f'<tr><td class="n">{e(m)}</td><td>{e(x)}</td></tr>' for m, x in MODELS[t])
    add(f'''<section class="page"><h2>{code} {e(P[code]['name'])}</h2><p class="lead">{MODTXT[code]}</p>{shot(code, 'shot half')}
<h3>models</h3><table class="params">{models}</table><h3>controls</h3>{ptable(params_rows(code, d))}{presets(code)}</section>''')

# ---- effects --------------------------------------------------------------------------------------------
add(divider('the processes', 'mc3 micro · dl4 dub · rv5 room · sw6 swing · fl7 filter · gl8 glue', GROUND['dl4'],
            '<svg class="wire" viewBox="0 0 600 300"><g fill="none" stroke-linecap="round"><path d="M-20 260 L 620 60" stroke="#2f7fd1" stroke-width="7"/><path d="M-20 278 L 620 78" stroke="#e06a2e" stroke-width="4"/><path d="M-20 292 L 620 92" stroke="#8f6bb8" stroke-width="3"/><path d="M-40 90 Q 300 -60 640 40" stroke="#e9a0a8" stroke-width="6"/><path d="M-40 112 Q 300 -36 640 62" stroke="#e06a2e" stroke-width="3"/></g></svg>', head='#1d1c15'))
add('<section class="page" id="fx"><h2>the processes</h2><p class="lead">Six stereo effects. All tempo-related settings follow the host; with the host stopped they free-run at its tempo.</p></section>')
for code in ['mc3', 'dl4', 'rv5', 'sw6', 'fl7', 'gl8']:
    def d(pid, code=code):
        if code == 'sw6' and pid.startswith('step'): return FX['sw6']['step']
        return FX[code].get(pid, '')
    skip = (lambda pid: pid.startswith('step') and pid not in ('step1', 'step16')) if code == 'sw6' else (lambda pid: False)
    tips = ''.join(f'<li>{t}</li>' for t in TIPS.get(code, []))
    note = '<p class="small">step 2 … step 15 work the same way as step 1 and step 16. In the editor they are the sixteen bars at the bottom: drag a bar up (later) or down (earlier), alt-click to reset.</p>' if code == 'sw6' else ''
    add(f'''<section class="page"><h2>{code} {e(P[code]['name'])}</h2><p class="lead">{INTRO[code]}</p>{shot(code, 'shot half')}
<h3>controls</h3>{ptable(params_rows(code, d, skip))}{note}<h3>using it</h3><ul>{tips}</ul>{presets(code)}</section>''')

# ---- wb1 ------------------------------------------------------------------------------------------------
flow = '''<svg class="flow" viewBox="0 0 700 170" font-family="IBM Plex Mono" font-size="11">
<g fill="none" stroke="#7fd3c8" stroke-width="1.4">
<rect x="10" y="20" width="80" height="34"/><rect x="10" y="74" width="80" height="34"/><rect x="10" y="126" width="80" height="30"/>
<rect x="130" y="47" width="80" height="34"/><rect x="250" y="47" width="80" height="34"/><rect x="370" y="47" width="80" height="34"/><rect x="490" y="47" width="70" height="34"/>
<rect x="600" y="47" width="90" height="34"/>
<path d="M90 37 H120 V64 H130 M90 91 H120 V64 M50 74 V54 M210 64 H250 M330 64 H370 M450 64 H490 M560 64 H600 M90 141 H580 V81"/></g>
<g fill="#e8f1ef"><text x="18" y="41">osc a</text><text x="18" y="95">osc b</text><text x="18" y="145">sub</text><text x="54" y="68" font-size="9">fm</text>
<text x="138" y="61">filter</text><text x="138" y="75" font-size="9">or dist</text><text x="258" y="61">dist</text><text x="258" y="75" font-size="9">or filter</text>
<text x="378" y="68">formant</text><text x="498" y="68">comb</text><text x="608" y="61">amp · ott</text><text x="608" y="75" font-size="9">mono &lt;140hz</text>
<text x="230" y="125" font-size="10">sub bypasses filter, distortion, formant and comb</text></g>
<g fill="#ffffff"><text x="140" y="16" font-size="10">wobble + mod env → cutoff, vowel, fm, drive, pitch, volume, comb</text></g></svg>'''
WBSEC = [('osc a', 'a_'), ('osc b', 'b_')]
WBMOD = {'div': 'rate, synced (1/64 … 16 bars)', 'sync': 'synced to tempo, or free (hz)', 'hz': 'rate when not synced', 'shape': 'sine · triangle · ramps · square · wow · yoy · random · smooth random',
          'retrig': 'restart on each note (off: locked to the bar)', 'phase': 'start point', 'mode': 'loop, or one-shot (plays once per note and holds)', 'skew': 'moves the peak earlier or later',
          'smooth': 'rounds off steps and edges'}
def wb_desc(pid):
    import re
    k = pid[2:] if pid[:2] in ('a_', 'b_') else pid
    if k in WB: return WB[k]
    m = re.match(r'^l[23](.+)$', pid)
    if m and m.group(1) in WBMOD: return WBMOD[m.group(1)]
    if pid.startswith('stp'): return 'value of this step (draw them on the modulation page)'
    if pid == 'steplen': return 'number of steps in the pattern'
    if pid == 'stepdiv': return 'length of one step'
    if pid == 'stepglide': return 'slide into the next step'
    if pid == 'stepretrig': return 'restart the pattern on each note'
    if pid.endswith('src'): return 'modulation source'
    if pid.endswith('dst'): return 'what it moves'
    if pid.endswith('amt'): return 'how much, either direction'
    return ''
def wb_skip(pid):
    import re
    if re.match(r'^stp([2-9]|1[0-5])$', pid): return True
    if re.match(r'^m[2-8](src|dst|amt)$', pid): return True
    return False
add(divider('wb1 wub', 'dubstep and neuro bass', GROUND['wb1'], wire('#7fd3c8'), '#f6f6f0'))
add(f'''<section class="page" id="wb1"><h2>wb1 wub</h2><p class="lead">{INTRO['wb1']}</p>{shot('wb1')}
<h3>signal flow</h3><div class="dark">{flow}</div>
<p>Two oscillators (each up to seven detuned copies) and noise go through the filter and the distortion — in either order — then the formant filter
and the comb, then the amp envelope. The sub is added afterwards, clean. The voice runs at twice the sample rate, so heavy distortion and fm stay
clean, and is filtered back down with a steep half-band filter. After the voices: three-band "ott" compression, everything below 140 hz made mono,
width above, and a soft ceiling.</p>
<h3>the wobble</h3><p>The wobble lfo is locked to the host tempo. With <b>retrigger</b> on, each new note restarts it at <i>phase</i>, so every note
wobbles the same way; off, it runs with the bar. It moves seven destinations at once, each with its own depth (bipolar where it makes sense), so one
lfo can open the filter, shift the vowel and push the drive together. Shapes: sine, triangle, ramps, square, <b>wow</b> (opens fast, closes slowly),
<b>yoy</b> (a big hump then a small one — the "yoy-yoy") and random steps. With <i>mod wheel</i> on <b>wobble depth</b> the wheel plays the
wobble; on <b>wobble rate</b> it switches to faster divisions.</p>
<h3>modulation</h3><p>The <b>modulation</b> tab (top right) holds everything that moves the sound beyond the wobble.</p>
<img class="shot" src="img/wb1-mod.png" alt="wb1 modulation">
<p><b>lfo 2</b> and <b>lfo 3</b>: tempo-synced from 1/64 up to 16 bars (or free in hz), with every wobble shape plus <i>smooth random</i>.
<b>skew</b> bends the shape (early or late peak), <b>smooth</b> rounds it off, <b>retrigger</b> restarts it on each note (off: it runs with
the bar), <b>one-shot</b> plays it once and holds the end — a ramp up over 4 bars in one-shot is a filter sweep that rises across a long note.
The scope above each lfo shows the last seven seconds.</p>
<p><b>steps</b>: sixteen values you draw, played at a tempo division with a glide between them — the programmed wub. Length 1–16 makes
odd-length patterns against the bar.</p>
<p>The <b>matrix</b> has eight slots: a source, a destination and an amount (−100 … +100%). Sources: wobble, lfo 2, lfo 3, steps, mod env,
amp env, velocity, mod wheel, note, a random value per note, aftertouch. Destinations: cutoff (±6 octaves), resonance, vowel, formant shift,
formant mix, fm, drive, pitch (±24 st), fine pitch, volume, comb tune and feedback, the shape of either oscillator, pulse width, detune, noise,
sub, distortion mix — and the <b>rates</b> of the wobble, lfo 2, lfo 3 and the steps (±3 octaves), which is how the speeding-up wubs are made.
Several slots can share a source or a destination; they add.</p>
<table class="params">
<tr><td class="n">slow sweep wob</td><td>a 1/8 wobble while lfo 2 (ramp up, 4 bars, one-shot) opens the cutoff and drive over the note</td></tr>
<tr><td class="n">step wub</td><td>the steps play cutoff and vowel together, with glide</td></tr>
<tr><td class="n">rate riser</td><td>lfo 2 ramps the wobble rate up two octaves over half a bar</td></tr>
<tr><td class="n">neuro movement</td><td>smooth random on fm, a triangle on comb pitch, steps on vowel, a little random on cutoff</td></tr>
<tr><td class="n">talking steps</td><td>an 8-step vowel pattern with long glides</td></tr>
<tr><td class="n">pressure growl</td><td>aftertouch adds drive, the mod wheel opens the filter, velocity sets the fm</td></tr></table>
<h3>the keyboard</h3><p>The keys along the bottom play wb1 in the standalone app or without a controller. Drag across them to hear glide and legato; &lt; and &gt; change octave.</p>
<h3>recipes</h3><table class="params">{''.join(f'<tr><td class="n">{e(n)}</td><td>{t}</td></tr>' for n, t in WB_RECIPES)}</table>
<h3>controls</h3>{ptable(params_rows('wb1', wb_desc, wb_skip))}<p class="small">steps 2 – 15 and matrix slots 2 – 8 work like the ones shown; each is its own automatable parameter.</p>{presets('wb1')}</section>''')

# ---- ms0 master -------------------------------------------------------------------------------------------
MS = {
 'trim': 'input level. Aim for peaks around -6 dbfs into the chain', 'listen': 'monitoring only: stereo · mono (sum) · side (the difference) · swap (L / R). Set stereo before you bounce',
 'bypass': 'compare: hear the input instead (delayed to match the latency)', 'match': 'during compare, the input is level-matched to the output (short-term loudness), so louder never wins by default',
 'eqon': 'switch the whole linear-phase eq (stays at the same latency)', 'type': 'bell · low shelf · high shelf · notch', 'freq': 'centre or corner frequency', 'gain': 'boost or cut',
 'q': 'width: low = broad, high = narrow', 'ch': 'stereo (both) · mid (the centre) · side (the edges)', 'on': 'band on',
 'hpon': 'high-pass on', 'hpfreq': 'high-pass corner', 'hpslope': '12 – 96 db / octave', 'hpch': 'which part it acts on (side = remove lows from the edges only)',
 'lpon': 'low-pass on', 'lpfreq': 'low-pass corner', 'lpslope': '12 – 96 db / octave', 'lpch': 'which part it acts on',
 'dynon': 'switch both dynamic bands', 'thr': 'level at which the band starts acting', 'range': 'maximum cut (negative) or boost (positive)', 'att': 'how fast it reacts', 'rel': 'how fast it lets go',
 'mbon': 'switch the band compression (the split and widths stay)', 'x1': 'sub / low-mid crossover', 'x2': 'low-mid / mid-high crossover', 'x3': 'mid-high / high crossover',
 'ratio': 'compression ratio, keep it 1.2 – 2', 'width': 'stereo width of the band: 0% mono, 100% as is, 200% doubled side', 'solo': 'hear only the soloed bands',
 'imgon': 'switch the per-band widths', 'air': 'adds a decorrelated copy of the top band\'s centre to its sides: width for mono highs, no transient smear below the top crossover',
 'saton': 'saturation on', 'satcurve': 'tape · tube · soft clip · smooth', 'satfocus': 'where the harmonics are generated: full · lows · highs · ends (leaves 200 hz – 2 khz alone)',
 'satdrive': 'drive into the curve (level-compensated)', 'satmix': 'parallel: dry / saturated',
 'ambon': 'ambience on', 'amblevel': 'level of the parallel room, keep it very quiet', 'ambsize': 'room size', 'ambdecay': 'tail length', 'ambhp': 'high-pass on the room', 'amblp': 'low-pass on the room', 'ambwidth': 'stereo width of the room',
 'glueon': 'glue on', 'gthr': 'threshold', 'gratio': 'ratio', 'gatt': 'attack', 'grel': 'release', 'gauto': 'program-dependent release', 'gmakeup': 'makeup gain', 'gmix': 'parallel mix',
 'dcon': 'removes dc offset (a 3 hz high-pass, far below anything musical)', 'balance': 'left / right balance at the input',
 'target': 'a loudness goal; the meter line shows how far the integrated loudness is from it', 'midlvl': 'level of the centre after imaging', 'sidelvl': 'level of the sides after imaging',
 'clipon': 'clipper on', 'clipdrive': 'gain into the clipper; it clips at the ceiling, the limiter catches the rest', 'clipsoft': '0% hard clip … 100% very soft knee',
 'limos': 'off · 2x · 4x · 8x: the clipper and limiter run at this rate, so peaks between samples are caught where they happen and the clipping does not alias. Latency does not change between settings',
 'limlink': '100% both channels share one gain (stable image); lower lets each side limit on its own',
 'limon': 'limiter on', 'limgain': 'drive into the limiter', 'ceiling': 'maximum true peak (inter-sample peaks included)', 'limrel': 'release', 'limauto': 'fast release after short peaks, slower when held', 'lookahead': 'attack window (latency stays fixed at 5 ms)',
 'out': 'output level (before dither)', 'dither': 'TPDF dither for 24 or 16 bit export; off for 32-bit float', 'shaping': 'first-order noise shaping (pushes dither noise up in frequency)',
}
def ms_desc(pid):
    import re
    if pid in MS: return MS[pid]
    m = re.match(r'^[edb]\d(.+)$', pid)
    if m and m.group(1) in MS: return MS[m.group(1)]
    return ''
MS_PRESETS = [
 ('transparent', 'only the limiter at -1 dbtp. The starting point: switch stages on one at a time and keep only those that help'),
 ('microhouse club', 'subsonic high-pass, a little low-mid out of the centre, side mud cleared, side air, sub mono and widening above, tape saturation on the lows, light glue, +4 db into the limiter'),
 ('dense experimental', 'no high-pass (your lows stay), mid cuts at 300 hz and 1.2 khz, side cut at 450 hz, side air, two dynamic mid bands at 400 hz and 2.8 khz, gentle bands, widths 0 / 85 / 115 / 135%, air, a very quiet room, no glue, light limiting'),
 ('electroacoustic, dynamic', 'almost nothing: a dynamic band catching harshness around 3.5 khz, the sub narrowed, slow limiter release. Keeps the full dynamic range'),
 ('wide air, deep space', 'side high shelf, wide top band with air, a quiet room for depth'),
 ('mono-safe low end', 'everything under 110 hz mono, low-mids half width, side high-pass at 120 hz'),
 ('loud club', 'firmer bands, soft-clip saturation focused on the ends, glue, +7 db into the limiter with a short lookahead'),
 ('vinyl pre-master', 'high- and low-pass, everything under 150 hz mono, low-mids narrowed, ceiling -1.5 dbtp, no limiting gain'),
 ('dreamstate (from your mixes)', 'made from the analysis of your ten mixes: 15 hz linear-phase high-pass, side cut at 300 hz, a little centre cut at 460 hz, side air, dynamic mid band at 250 hz and side band at 320 hz, sub mono, very gentle bands, no glue, a light soft clipper, 8× oversampled limiter at -1 dbtp with +2 db'),
 ('16-bit export', 'transparent, with 16-bit TPDF dither and noise shaping for CD / 16-bit delivery'),
]
msflow = """<svg class="flow" viewBox="0 0 700 120" font-family="IBM Plex Mono" font-size="10"><g fill="none" stroke="#5aaad8" stroke-width="1.3">""" + \
  ''.join(f'<rect x="{10+i*86}" y="34" width="76" height="34"/>' for i in range(8)) + \
  ''.join(f'<path d="M{86+i*86} 51 H{96+i*86}"/>' for i in range(7)) + """</g><g fill="#e9eef0">""" + \
  ''.join(f'<text x="{16+i*86}" y="49">{a}</text><text x="{16+i*86}" y="62" font-size="8.5">{b}</text>' for i,(a,b) in enumerate(
   [('trim','listen'),('lin eq','8 + hp/lp'),('dyn eq','2 bands'),('split','4 bands'),('sat','2x'),('room','parallel'),('glue','bus comp'),('limit','true peak')])) + \
  """<text x="10" y="20">mid / side throughout · stages A and B are linear phase (FIR) · every stage can be switched off at the same latency</text>
<text x="10" y="96" font-size="9">meters: loudness momentary / short-term / integrated (BS.1770) · true peak · correlation · spectrum pre / post, mid / side · imager</text></g></svg>"""
ms0_html = f'''<section class="page" id="ms0"><h2>ms0 master</h2>
<p class="lead">A mastering bus for two kinds of work: the tight, dry, wide-but-centred sound of 2000s microhouse, and dense experimental and
electroacoustic music that reaches from the lowest lows to the highest highs. The aim is not maximum loudness or maximum width but
<b>spectral separation, depth and controlled stereo</b>, without flattening the density that makes the music what it is.</p>
{shot('ms0')}
<h3>the chain</h3><div class="dark">{msflow}</div>
<p>Everything runs in mid / side. The eq and the band split are <b>linear phase</b>: they change level without smearing the timing of
transients or the phase between frequencies, which is why the plug-in has about 190 ms of latency (8192-tap filters at 48 khz, longer
filters at higher rates so the resolution stays the same). Ableton compensates automatically. With every stage neutral the output equals the
input to within 135 db — the test suite checks this.</p>
<h3>a way of working</h3><ol>
<li><b>trim</b> so the loudest peaks sit near -6 dbfs. Listen in <b>mono</b> once for reference.</li>
<li>Look at the <b>analyser</b> (mid blue, side orange, the input faint) — especially 150 hz – 1.5 khz, where many independent sounds pile up.
The analyser shows where to listen, not where to cut.</li>
<li><b>eq</b>: remove what is not wanted. A high-pass only if the lows below 25 hz are not part of the music. Small cuts in the low-mids of the
<b>mid</b> if the centre is crowded; side-only cuts around 200 – 500 hz if the edges are muddy; a gentle side high shelf for air.
Double-click the analyser to add a band, drag it, scroll for q, right-click for type and channel.</li>
<li><b>dynamic eq</b> for buildups that only happen sometimes (a texture that blooms at 400 hz when it gets loud).</li>
<li><b>multiband</b>: gentle (1.3 – 2:1, 1 – 2 db of reduction) and only where the low end moves too much. Go easy on the low-mid band.
Then the <b>widths</b>: sub mono, low-mids slightly narrow, mids moderate, highs wider. Width and depth are different things; watch the imager and
the correlation.</li>
<li><b>saturation</b> only for definition (a low end that should be heard on small speakers). Focus <i>ends</i> keeps it out of 200 hz – 2 khz.</li>
<li><b>ambience</b> — a very quiet, filtered room — only if the mix lacks a sense of space. Often reducing competing information creates more space than adding a room.</li>
<li><b>glue</b> 0.5 – 1.5 db, or none.</li>
<li><b>clipper</b> (optional, 1 – 3 db) then <b>limiter</b>: ceiling -1 dbtp, oversampling 4× or 8×, ideally 1 – 2 db of reduction or less. Don't chase a loudness number.</li>
<li>Check everything with <b>compare</b> (level-matched): each stage must improve separation, depth or translation, or it goes. The best chain is often short.</li></ol>
<h3>the limiter section</h3><p>Gain → <b>clipper</b> → <b>limiter</b>, both running <b>oversampled</b> (2×, 4× or 8×, linear-phase
half-band filters), then a short <b>true-peak safety</b> stage at the normal rate that catches anything the down-sampling filters add back.
The limiter never acts late: a sliding minimum plus a box-car average over the look-ahead window guarantees the gain is already down when
a peak arrives. Release is program dependent in auto (fast after short peaks, slower when it is held). Measured on a drum loop pushed
18 db into it, the output stays at -1.000 dbtp with oversampling off, 2× and 4×, and -0.999 dbtp at 8× with the clipper. A clipper before
the limiter is the usual way to get loudness with fewer audible limiter movements; keep it at 1 – 3 db.</p>
<h3>a / b, compare and the target</h3><p><b>a</b> and <b>b</b> hold two complete settings; click to switch, <b>copy</b> copies the current one to the
other. <b>compare</b> plays the input instead (level-matched when <i>level-matched</i> is on). Choose a <b>target</b> (−14 lufs for streaming,
−9 for club …), play the whole song once so the integrated loudness is measured, then <b>go to target</b> sets the limiter gain to reach it.
For your music the target is often <i>off</i>: loudness range matters more than a number.</p>
<h3>the imager</h3><p>Click it to switch between three views. <b>Polar sample</b> (as in Ozone Imager): every sample is a dot; mono material
stands on the vertical line, hard left and right on the diagonals, out-of-phase material lies along the horizontal. Dots outside the
diagonals mean more side than mid. <b>Polar level</b>: the energy at each angle as rays. <b>Lissajous</b>: the classic goniometer. Below: the
<b>correlation</b> (+1 mono, 0 wide, below 0 phase trouble that will vanish in mono) and, for each band, the measured width (orange) against
the width you set (white mark). The same per-band width bars appear in the analyser under each band.</p>
<h3>meters</h3><p>The line under the analyser: momentary, short-term and integrated loudness (BS.1770 / EBU R128, gated), <b>lra</b> (loudness
range, EBU Tech 3342), the distance to the target, true peak (4× interpolated), <b>plr</b> (peak to loudness ratio — how much dynamic punch is
left), gain reduction of glue and limiter, and the latency. <b>reset meters</b> clears integrated loudness, loudness range and peak hold.</p>
<h3>your mixes</h3><p>ms0 was tuned on the ten final mixes in <i>Sadnoise_Cloudfin_Dreamstate / final wavs</i>, measured with its own meters:</p>
<table class="params"><tr><th>what</th><th>found</th><th>what ms0 does about it</th></tr>
<tr><td class="n">true peak</td><td>five tracks above 0 dbtp: track 9 +3.7, track 10 +2.3, track 1 +2.1, track 12 +0.7, track 4 +0.4</td><td>oversampled limiter + true-peak safety at -1 dbtp</td></tr>
<tr><td class="n">loudness</td><td>-10.8 … -15.7 lufs, lra 6 – 13 lu, crest 11 – 18 db: very dynamic</td><td>gentle presets, lra and plr meters, target usually off</td></tr>
<tr><td class="n">extreme lows</td><td>much energy under 30 hz (track 9, 4, 11, 1)</td><td>dc filter; a 15 hz linear-phase high-pass in the dreamstate preset that leaves 20 – 40 hz alone</td></tr>
<tr><td class="n">wide sub</td><td>sub side-width 0.50 (track 5), 0.41 (track 10), 0.30 (track 1)</td><td>sub band at 0% width: the low end survives mono and club systems</td></tr>
<tr><td class="n">very wide overall</td><td>correlation 0.26 (track 8), 0.48 (track 5), 0.49 (track 12)</td><td>the imager, per-band width, mid / side levels</td></tr>
<tr><td class="n">side low-mid mud</td><td>side energy at 160 – 350 hz high in tracks 5, 8, 12</td><td>a side cut at 300 hz and a dynamic side band at 320 hz</td></tr></table>
<p>Track 9 through <b>dreamstate (from your mixes)</b>: -15.7 → -14.9 lufs, loudness range 12.8 → 11.4 lu, true peak +3.69 → -1.00 dbtp.</p>
<h3>presets</h3><table class="params">{''.join(f'<tr><td class="n">{e(n)}</td><td>{e(t)}</td></tr>' for n, t in MS_PRESETS)}</table>
<p class="small">Presets never change listen, compare or input trim.</p>
<h3>controls</h3>{ptable(params_rows('ms0', ms_desc, skip=lambda pid: pid[0] in 'eb' and pid[1:2].isdigit() and pid[1] not in '1'))}
<p class="small">eq bands 2 – 8 work like band 1 and multiband bands 2 – 4 like the sub band; every one of them is its own automatable parameter
(<i>eq 5 gain</i>, <i>high width</i> …).</p>
<h3>export</h3><p>Bounce at the project's sample rate as 24-bit (dither 24) or 32-bit float (dither off). For 16-bit delivery, set dither to 16 bit;
nothing after ms0 should change the level.</p></section>'''
add(divider('ms0 master', 'linear-phase mastering bus · mid / side · imaging · true-peak limiter', '#1a242c', chevrons('#5aaad8', 5), '#e9eef0', '#e9eef0'))
add(ms0_html)

# ---- recipes + tech -----------------------------------------------------------------------------------------
add('''<section class="page" id="recipes"><h2>recipes</h2>
<h3>a long shuffled groove (kt1)</h3><ol><li>Load <b>a3 tok</b>. Set rm3 length 15, pc7 length 12, cl8 length 7.</li><li>Shift-drag a few rim steps early and hats late.</li>
<li>Alt-click the offbeat clicks to 50%.</li><li>Automate <i>pattern</i> between a1 and a2 every 16 bars, and <i>pc7 probability</i> slowly up.</li></ol>
<h3>the dub chain</h3><p>kt1 → rm3 on its own output → <b>dl4</b> (3/16, feedback 60%, age 0.4, send automated) → <b>rv5</b> air with gate. Main mix → <b>gl8</b>.</p>
<h3>cut-up from your own loop</h3><p>Resample a bar of kt1 (or anything) and drop it into <b>kt2</b>; onsets mode, 16 slices; load preset <b>cut-up</b>; put <b>mc3</b> after it in click mode.</p>
<h3>groove transplant</h3><p>Put <b>sw6</b> on a loop with the feel you want, <i>learn groove</i> for four bars, save the preset with your set, then put the same sw6 settings on straight tracks.</p>
<h3>a 2000s dubstep bassline</h3><p><b>wb1</b> preset <b>sub wob</b>, notes G0–D1 at 140 bpm. Automate <i>wobble rate</i>: 1/8 for two notes, 1/16 for one, 1/8 t for the last. Halfstep kick and snare from bd1 and cp6.</p>
<h3>neuro</h3><p><b>wb1</b> preset <b>neuro growl</b> or <b>reese</b>; resample several notes, cut them in kt2 with 32 slices, then gl8 with ratio 10 in parallel.</p>

<h2 id="tech">technical notes</h2>
<ul><li>Sample rates 44.1–192 khz; all processing in 32-bit float. Stereo throughout; kt1 has 9 stereo outputs.</li>
<li>Latency: none, except <b>sw6</b> — 70 ms, reported to the host for automatic compensation.</li>
<li>Drum engines: modal resonators (complex one-pole rotations), band-limited oscillators (polyBLEP), TPT state-variable filters,
anti-aliased (ADAA) saturation, per-engine loudness calibration.</li>
<li>wb1: 2× oversampled voice, 63-tap Kaiser half-band decimator, polyBLEP unison oscillators, phase modulation, 3-band ott with Linkwitz-Riley crossovers.</li>
<li>rv5: 8-line feedback delay network, Hadamard mixing, modulated lines, early-reflection taps. dl4: Hermite-interpolated tape lines with glide on time changes.</li>
<li>Every plug-in passes Apple's AU validation and an offline test that renders every preset and checks for silence, overload, non-finite output and state recall.</li>
<li>Source: ~/faltung. Build with CMake and JUCE; <code>faltungTest</code> renders and checks all plug-ins. Licence AGPL-3.0. Fonts: Inter and IBM Plex Mono (SIL Open Font License).</li></ul>
</section>''')

CSS = f'''
@font-face {{ font-family: Inter; src: url("file://{FONTS}/Inter-Regular.ttf"); font-weight: 400; }}
@font-face {{ font-family: Inter; src: url("file://{FONTS}/Inter-Medium.ttf"); font-weight: 600; }}
@font-face {{ font-family: Plex; src: url("file://{FONTS}/IBMPlexMono-Regular.ttf"); font-weight: 400; }}
@font-face {{ font-family: Plex; src: url("file://{FONTS}/IBMPlexMono-Medium.ttf"); font-weight: 600; }}
@page {{ size: A4; margin: 16mm 15mm 16mm 15mm; @bottom-right {{ content: counter(page); font: 8pt Plex; }} }}
html {{ -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
body {{ font: 9.4pt/1.5 Inter, Helvetica, sans-serif; color: #1d1c15; margin: 0; }}
h1, h2, h3 {{ font-weight: 600; letter-spacing: -0.2px; }}
h2 {{ font-size: 20pt; margin: 0 0 6pt; }}
h3 {{ font: 600 9.5pt Plex; margin: 14pt 0 4pt; border-bottom: 0.6pt solid #1d1c15; padding-bottom: 2pt; }}
p.lead {{ font-size: 11pt; line-height: 1.45; color: #2f4a52; }}
.page {{ break-before: page; }}
code {{ font: 8.5pt Plex; background: #eef3f3; padding: 0 3px; }}
a {{ color: inherit; }}
table.params {{ width: 100%; border-collapse: collapse; margin: 4pt 0 6pt; font-size: 8.4pt; }}
table.params th {{ text-align: left; font: 600 7.6pt Plex; border-bottom: 0.6pt solid #1d1c15; padding: 2pt 6pt 2pt 0; }}
table.params td {{ border-bottom: 0.4pt solid #d6d8d4; padding: 1.8pt 6pt 1.8pt 0; vertical-align: top; }}
table.params td.n {{ font: 600 8pt Plex; white-space: nowrap; }}
table.params td.r, table.params td.d {{ font: 7.6pt Plex; color: #3c4a4f; }}
table.params td.r {{ max-width: 52mm; }}
table.params tr {{ break-inside: avoid; }}
img.shot {{ width: 100%; margin: 6pt 0 2pt; border: 0.4pt solid #ccc; }}
img.shot.half {{ width: 60%; }}
.presets {{ font: 8pt Plex; margin-top: 8pt; }} .presets span {{ background: #1d1c15; color: #fff; padding: 1px 5px; margin-right: 6px; }}
.small {{ font-size: 8pt; color: #555; }}
ol.toc {{ font: 10pt Plex; line-height: 1.9; }}
ul, ol {{ padding-left: 14pt; }} li {{ margin: 2pt 0; }}
.dark {{ background: #2f4a52; padding: 8pt; }} svg.flow {{ width: 100%; }}
.cover {{ position: relative; height: 262mm; overflow: hidden; background: #fbfcfc; }}
.cover svg.chev {{ position: absolute; inset: 0; width: 100%; height: 100%; }}
.cover svg.cube {{ position: absolute; left: 14mm; top: 120mm; width: 30mm; }}
.cover .ct {{ position: absolute; left: 14mm; top: 16mm; }} .cover h1 {{ font-size: 40pt; margin: 0; }}
.cover .sub {{ font-size: 13pt; color: #2f6f93; margin: 2pt 0; }} .cover .ver {{ font: 9pt Plex; color: #4a5a63; }}
.cover .tl {{ position: absolute; right: 8mm; bottom: 12mm; width: 128mm; font: 7.6pt Plex; background: rgba(251,252,252,.85); padding: 6pt; }}
.cover .tl div {{ display: grid; grid-template-columns: 7mm 20mm 8mm 13mm 1fr; gap: 2mm; line-height: 1.7; align-items: center; }}
.chip {{ display: inline-block; width: 12px; height: 7px; transform: skewX(-30deg); border: 0.4pt solid #9aa; }}
.divider {{ break-before: page; position: relative; height: 262mm; overflow: hidden; }}
.divider svg.wire {{ position: absolute; left: 10mm; right: 10mm; top: 70mm; width: 170mm; }}
.divider .dt {{ position: absolute; left: 14mm; top: 16mm; }} .divider h1 {{ font-size: 34pt; margin: 0; text-transform: uppercase; }}
.divider p {{ font-size: 12pt; margin: 4pt 0; }}
'''
ms_page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>ms0 master manual</title><style>{CSS}</style></head><body>' + \
    divider('ms0 master', 'faltung · linear-phase mastering bus · manual', '#1a242c', chevrons('#5aaad8', 5), '#e9eef0', '#e9eef0') + ms0_html + '</body></html>'
open(os.path.join(HERE, 'ms0.html'), 'w').write(ms_page)
page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>faltung manual</title><style>{CSS}</style></head><body>{"".join(doc)}</body></html>'
open(os.path.join(HERE, 'manual.html'), 'w').write(page)
chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
pdf = os.path.join(HERE, 'faltung Manual.pdf')
subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                f'--print-to-pdf={pdf}', 'file://' + os.path.join(HERE, 'manual.html')], check=True, capture_output=True)
subprocess.run([chrome, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--allow-file-access-from-files',
                f'--print-to-pdf={os.path.join(HERE, "ms0 master Manual.pdf")}', 'file://' + os.path.join(HERE, 'ms0.html')], check=True, capture_output=True)
print(pdf)

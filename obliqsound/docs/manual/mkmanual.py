#!/usr/bin/env python3
"""obliqsound / obliqsonic manual -> "obliqsound Manual.pdf" (reportlab, the faltung fonts and colours).
Run from anywhere: the fonts come from Resources/fonts at the top of the faltung bundle, the pictures from img/
(written by ObliqSoundTest / ObliqSonicTest --screenshots img)."""
import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image, Table, TableStyle,
                                PageBreak, KeepTogether, NextPageTemplate, CondPageBreak)
from reportlab.platypus.tableofcontents import TableOfContents

HERE = os.path.dirname(os.path.abspath(__file__))                      # obliqsound/docs/manual
OBLIQ = os.path.abspath(os.path.join(HERE, '..', '..'))                # obliqsound/
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))           # the faltung bundle
FONTS = os.path.join(ROOT, 'Resources', 'fonts')
IMG = HERE + '/img/'
OUT = HERE + '/obliqsound Manual.pdf'
VERSION = '2.3.0'

pdfmetrics.registerFont(TTFont('Inter', os.path.join(FONTS, 'Inter-Regular.ttf')))
pdfmetrics.registerFont(TTFont('Inter-Medium', os.path.join(FONTS, 'Inter-Medium.ttf')))
pdfmetrics.registerFont(TTFont('Plex', os.path.join(FONTS, 'IBMPlexMono-Regular.ttf')))
pdfmetrics.registerFont(TTFont('Plex-Medium', os.path.join(FONTS, 'IBMPlexMono-Medium.ttf')))
# the Ge'ez numerals of the sampler squares: Inter has none, so a system font with Ethiopic glyphs when there is one
GEEZ = None
for cand in ('/System/Library/Fonts/Supplemental/KefaIII.ttf', '/usr/share/fonts/truetype/abyssinica/AbyssinicaSIL-Regular.ttf',
             '/usr/share/fonts/truetype/noto/NotoSansEthiopic-Regular.ttf', 'C:/Windows/Fonts/ebrima.ttf'):
    if os.path.exists(cand):
        pdfmetrics.registerFont(TTFont('Geez', cand)); GEEZ = 'Geez'; break

SAGE = colors.HexColor('#c8d6a0')        # the obliqsound field
SAGE_L = colors.HexColor('#edf1df')      # the field, paler, behind notes and table heads
GREEN = colors.HexColor('#1f3a2c')       # the obliqsonic field, the accent of obliqsound
INK = colors.HexColor('#1d1c15')
GREY = colors.HexColor('#6b6f62')
RULE = colors.HexColor('#c9cbc0')
STRIPS = [('polygon', '#8ec3e6'), ('odessa', '#f6c590'), ('lester', '#8dbb7a'), ('lfo', '#6e80c8'),
          ('matrix', '#cfe6f0'), ('swarm', '#ee8e6e'), ('space', '#97adc4')]

ss = {
    'h1': ParagraphStyle('h1', fontName='Inter-Medium', fontSize=18, leading=22, textColor=GREEN, spaceBefore=4, spaceAfter=8),
    'h2': ParagraphStyle('h2', fontName='Inter-Medium', fontSize=12.5, leading=16, textColor=INK, spaceBefore=10, spaceAfter=4),
    'h3': ParagraphStyle('h3', fontName='Plex-Medium', fontSize=9.5, leading=13, textColor=GREEN, spaceBefore=6, spaceAfter=2),
    'p': ParagraphStyle('p', fontName='Inter', fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=5),
    'li': ParagraphStyle('li', fontName='Inter', fontSize=9.6, leading=13.4, textColor=INK, leftIndent=12, bulletIndent=2, spaceAfter=2),
    'cap': ParagraphStyle('cap', fontName='Plex', fontSize=8, leading=10.5, textColor=GREY, alignment=TA_CENTER, spaceAfter=8),
    'cell': ParagraphStyle('cell', fontName='Inter', fontSize=8.4, leading=10.8, textColor=INK),
    'cellb': ParagraphStyle('cellb', fontName='Plex-Medium', fontSize=8.0, leading=10.6, textColor=INK),
    'note': ParagraphStyle('note', fontName='Inter', fontSize=9, leading=12.6, textColor=INK, backColor=SAGE_L,
                           borderPadding=(6, 7, 6, 7), spaceBefore=4, spaceAfter=9, leftIndent=2, rightIndent=2),
    'toc1': ParagraphStyle('toc1', fontName='Plex-Medium', fontSize=10.5, leading=15, textColor=INK),
    'toc2': ParagraphStyle('toc2', fontName='Inter', fontSize=9.4, leading=12.5, leftIndent=14, textColor=GREY),
}

def esc(t):
    return t.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
def lc(t):                 # everything in the bundle is lowercase; tags and entities stay as they are
    out, i = [], 0
    while i < len(t):
        if t[i] in '<&':
            j = t.find('>' if t[i] == '<' else ';', i)
            j = len(t) if j < 0 else j + 1
            out.append(t[i:j]); i = j
        else:
            out.append(t[i].lower()); i += 1
    return ''.join(out)

story = []
def H1(t):
    story.append(CondPageBreak(60 * mm))
    t = lc(t); p = Paragraph(t, ss['h1']); p.toc = (0, t); story.append(p)
def H2(t, toc=True):
    story.append(CondPageBreak(30 * mm))
    t = lc(t); p = Paragraph(t, ss['h2'])
    if toc: p.toc = (1, t)
    story.append(p)
def H3(t): story.append(Paragraph(lc(t), ss['h3']))
def P(t): story.append(Paragraph(t, ss['p']))
def NOTE(t): story.append(Paragraph(t, ss['note']))
def UL(items):
    for it in items: story.append(Paragraph(it, ss['li'], bulletText='-'))
    story.append(Spacer(1, 3))
def SHOT(fn, cap, w=170 * mm):
    im = Image(IMG + fn); r = im.imageHeight / im.imageWidth
    im.drawWidth, im.drawHeight = w, w * r
    story.append(KeepTogether([im, Paragraph(lc(cap), ss['cap'])]))
def TABLE(rows, widths, head=True, lower=(0,)):
    # the named columns (control names, abbreviations) are lowercased, as on the panel
    rows = [[lc(str(c)) if (j in lower or (head and i == 0)) else str(c) for j, c in enumerate(r)] for i, r in enumerate(rows)]
    data = [[Paragraph(c, ss['cellb'] if (head and i == 0) or j == 0 else ss['cell']) for j, c in enumerate(r)] for i, r in enumerate(rows)]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if head else 0)
    st = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LINEBELOW', (0, 0), (-1, -1), 0.3, RULE),
          ('TOPPADDING', (0, 0), (-1, -1), 3), ('BOTTOMPADDING', (0, 0), (-1, -1), 3), ('LEFTPADDING', (0, 0), (-1, -1), 4)]
    if head: st += [('BACKGROUND', (0, 0), (-1, 0), SAGE_L), ('LINEBELOW', (0, 0), (-1, 0), 0.8, GREEN)]
    t.setStyle(TableStyle(st)); story.append(t); story.append(Spacer(1, 7))
def CTRL(rows):           # control / range / what it does
    TABLE([['control', 'range', 'what it does']] + rows, [32, 26, 112])

# ============================================================== page furniture
class Doc(BaseDocTemplate):
    def afterFlowable(self, f):
        if hasattr(f, 'toc'):
            lvl, txt = f.toc
            key = 'h%d' % id(f)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(txt, key, level=lvl, closed=lvl > 0)
            self.notify('TOCEntry', (lvl, txt, self.page, key))

def para(c, x, y, w, h, fill, skew=0.55):
    """the house shape: a parallelogram leaning right"""
    d = h * skew
    p = c.beginPath(); p.moveTo(x + d, y + h); p.lineTo(x + w + d, y + h); p.lineTo(x + w, y); p.lineTo(x, y); p.close()
    c.setFillColor(fill); c.drawPath(p, stroke=0, fill=1)

def strip(c, x, y, length, depth, col, label, outline=False):
    """a folded paper strip, as the page tabs: a flat face, a darker sliver where the paper turns"""
    lean, h = depth * 0.62, depth * 0.78
    face = c.beginPath(); face.moveTo(x, y); face.lineTo(x + length, y); face.lineTo(x + length + lean, y + h); face.lineTo(x + lean, y + h); face.close()
    c.setFillColor(colors.HexColor(col)); c.drawPath(face, stroke=0, fill=1)
    fold = c.beginPath(); fold.moveTo(x + length, y); fold.lineTo(x + length, y - depth * 0.42); fold.lineTo(x + length + lean, y + h); fold.close()
    r, g, b = colors.HexColor(col).rgb()
    c.setFillColor(colors.Color(r * 0.72, g * 0.72, b * 0.72)); c.drawPath(fold, stroke=0, fill=1)
    if outline:
        c.setStrokeColor(colors.white); c.setLineWidth(1.6); c.drawPath(face, stroke=1, fill=0)
    c.setFillColor(colors.HexColor('#1e2a20')); c.setFont('Inter', 9)
    c.drawCentredString(x + length * 0.5 + lean * 0.5, y + h * 0.5 - 3, label)

def cover(c, d):
    W, H = A4
    c.setFillColor(SAGE); c.rect(0, 0, W, H, stroke=0, fill=1)
    c.drawImage(IMG + 'icon.png', 22 * mm, H - 50 * mm, 24 * mm, 24 * mm, mask='auto')
    c.setFillColor(INK)
    c.setFont('Inter-Medium', 40); c.drawString(22 * mm, H - 78 * mm, 'obliqsound')
    c.setFont('Inter-Medium', 40); c.drawString(22 * mm, H - 94 * mm, 'obliqsonic')
    c.setFont('Plex', 11); c.drawString(22 * mm, H - 106 * mm, 'part of faltung')
    # a skewed accent bar with a white tick, as every knob on the panel
    para(c, 22 * mm, H - 118 * mm, 54 * mm, 4.5 * mm, GREEN)
    c.setFillColor(colors.white); c.rect(22 * mm + 54 * mm + 1.2 * mm, H - 119.3 * mm, 1.2 * mm, 7 * mm, stroke=0, fill=1)
    c.setStrokeColor(colors.Color(0.11, 0.11, 0.08, alpha=0.35)); c.setLineWidth(0.6)
    c.line(22 * mm + 57 * mm, H - 115.8 * mm, 22 * mm + 100 * mm, H - 115.8 * mm)
    # the pages as a row of folded strips
    x = 22 * mm
    for i, (name, col) in enumerate(STRIPS):
        strip(c, x, H * 0.44 - (i % 2) * 1.5 * mm, 19 * mm, 6.5 * mm, col, name, outline=(i == 0))
        x += 23.2 * mm
    c.setFillColor(INK); c.setFont('Plex', 9.5)
    y = 92 * mm
    for line in ('polyphonic synthesizer and field-recording instrument',
                 'its twin: the same instrument as a third-order ambisonics sound source',
                 'vst3 · au · vst2 · desktop app   macos, windows, linux'):
        c.drawString(22 * mm, y, line); y -= 5.5 * mm
    c.setFont('Plex', 8); c.setFillColor(GREEN)
    c.drawString(22 * mm, 20 * mm, 'version %s · november 2026 · github.com/femifleming/faltung' % VERSION)

def page(c, d):
    W, H = A4
    c.setStrokeColor(RULE); c.setLineWidth(0.6); c.line(18 * mm, H - 14 * mm, W - 18 * mm, H - 14 * mm)
    c.setFont('Plex', 7.5); c.setFillColor(GREEN)
    c.drawString(18 * mm, H - 12 * mm, 'obliqsound / obliqsonic')
    c.drawRightString(W - 18 * mm, H - 12 * mm, 'manual · faltung')
    c.setFont('Plex', 8); c.setFillColor(GREY)
    c.drawCentredString(W / 2, 10 * mm, str(d.page))

doc = Doc(OUT, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=20 * mm, bottomMargin=18 * mm,
          title='obliqsound / obliqsonic manual', author='faltung')
fr = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id='f')
doc.addPageTemplates([PageTemplate('cover', [fr], onPage=cover), PageTemplate('body', [fr], onPage=page)])
story += [NextPageTemplate('body'), PageBreak()]

# ============================================================== contents
story.append(Paragraph('contents', ss['h1']))
toc = TableOfContents(); toc.levelStyles = [ss['toc1'], ss['toc2']]
story += [toc, PageBreak()]

# ============================================================== 1 intro
H1('1  Welcome')
P('<b>ObliqSound</b> is a polyphonic synthesizer and field-recording instrument: a virtual-analogue voice, a '
  'polygonal oscillator, an additive (Odessa-style) oscillator, a dual morphing filter (Bionic-Lester-style), '
  'a sampler of 8 synthesized nature scenes and 18 field recordings, two LFOs, envelopes, a huge pin matrix '
  'in the spirit of the EMS Synthi, and a "swarm" of boids and a Lorenz attractor that can move every parameter.')
P('<b>ObliqSonic</b> is the same instrument as a <b>spatial sound source</b>. It plays from MIDI exactly like '
  'ObliqSound, but instead of a stereo pair it sends <b>16 channels of third-order Ambisonics</b> (ACN channel order, '
  'SN3D normalisation, the format of the IEM Plug-in Suite and SPARTA). Every voice you play, six copies of the '
  'field recording, the effects returns and the nature bed are points that fly round the listener on a flock, '
  'deviated Lorenz attractors or gravity wells, with distance, Doppler and air absorption.')
P('Both share one panel, in the <b>faltung</b> look: a flat colour field (pale sage for ObliqSound, deep green for ObliqSonic), '
  'every knob a short skewed bar with a white tick, the pages on a row of folded paper strips, the pin matrix a grid of chips, '
  'and on ObliqSonic a thin wireframe sphere for the scene. Nothing on the panel is a picture: it is all drawn, so every window size is sharp. '
  'ObliqSound and ObliqSonic are part of the faltung bundle and are built, released and installed with it.')
H2('Installing: the faltung bundle')
P('The two come with faltung. On a Mac the easiest way is Homebrew:')
NOTE('<font name="Plex">brew install --cask femifleming/tap/faltung</font>')
P('That puts the plug-ins in their system folders and the apps in <b>/Applications/faltung</b>. Otherwise download '
  '<b>faltung-macOS.zip</b>, <b>faltung-Windows.zip</b> or <b>faltung-Linux.tar.gz</b> from '
  '<font name="Plex">https://github.com/femifleming/faltung/releases</font>. Inside are an <b>ObliqSound/</b> and an <b>ObliqSonic/</b> folder, '
  'each with the app, the AU (macOS), the VST3 and the VST2. Copy them where your system keeps them:')
TABLE([['item', 'where it goes'],
       ['ObliqSound.vst3, ObliqSonic.vst3', 'macOS: ~/Library/Audio/Plug-Ins/VST3/<br/>Windows: C:\\Program Files\\Common Files\\VST3\\<br/>Linux: ~/.vst3/'],
       ['ObliqSound.component, ObliqSonic.component (Audio Units, macOS only)', '~/Library/Audio/Plug-Ins/Components/'],
       ['ObliqSound.vst (VST2; a .dll on Windows, a .so on Linux)', 'your VST2 folder (macOS: ~/Library/Audio/Plug-Ins/VST/)'],
       ['desktop apps', 'macOS: ObliqSound.app and ObliqSonic.app in /Applications/faltung/<br/>Windows and Linux: a folder holding the program and its Resources folder; keep the two together, anywhere you like'],
       ['factory presets', 'inside each plug-in'],
       ['your presets', 'presets.json in a folder named after the instrument, in<br/>macOS: ~/Library/Application Support/<br/>Windows: AppData\\Roaming\\<br/>Linux: ~/.config/']],
      [70, 100], lower=())
P('A .vst3 is a folder on every system (a "bundle"): copy the whole folder, not only the file inside it. The layout, presets and recordings live in it. '
  'The VST2 is built with the free FST headers, so it exists for every system too.')
P('Both are instruments, listed under the manufacturer <b>ObliqSound</b>. The sound engine is SuperCollider\'s synthesis server, '
  'embedded in the plug-in: nothing else needs to be installed.')
H2('Requirements')
UL(['<b>macOS</b> 11 or later, Apple silicon or Intel.',
    '<b>Windows</b> 10 or 11, 64-bit. If the plug-in does not load, install the Microsoft Visual C++ Redistributable (x64).',
    '<b>Linux</b>, x86-64, built on Ubuntu 22.04 (glibc 2.35 or later); the app uses ALSA or JACK.',
    'A host that loads VST3, VST2 or Audio Unit instruments (REAPER, Ableton Live, Logic, Bitwig ...), or the desktop apps.',
    'For ObliqSonic: a host that can run a 16-channel track and an Ambisonics decoder (REAPER with the IEM Plug-in Suite or SPARTA '
    'is the reference set-up; see chapter 10).',
    'CPU: light. Sixteen voices with every layer on use roughly an eighth of one core at 48 kHz.'])

# ============================================================== 2 quick start
H1('2  Quick start')
H2('In a DAW')
UL(['Put <b>ObliqSound</b> on an instrument track and play. The default sound is a sawtooth through filter A with a little noise and the wind scene.',
    'Click the square buttons in the middle (the <b>sampler</b>) to choose a nature scene or a field recording; turn <b>SAMPLER</b> up to hear the bed, <b>KEYS</b> to play the recording from the keyboard.',
    'Click a folded strip under the sampler (polygon, odessa, lester, lfo, matrix, swarm, and space on ObliqSonic) to open that page in the lower half.',
    'Choose presets from the list at the top left (133 included; &lt; and &gt; step through them); <b>randomize</b> rolls a new sound.',
    'Press <b>help</b> (beside the preset list) or <b>F1</b> for the built-in help.'])
H2('As a desktop app')
UL(['Open <b>ObliqSound</b> or <b>ObliqSonic</b> from /Applications/faltung. The <b>Options</b> button at the top left opens the audio and MIDI settings (all MIDI inputs are enabled automatically, including ones you plug in later).',
    'With no keyboard attached, play from the computer keyboard (chapter 13).',
    'The app remembers the sound and the devices.'])
H2('ObliqSonic in REAPER (spatial)')
UL(['Insert <b>ObliqSonic</b> on a track. Click the track\'s <b>Route</b> button and set <b>Track channels: 16</b>.',
    'After it insert a decoder at <b>order 3, SN3D</b>: <b>IEM BinauralDecoder</b> for headphones or <b>IEM AllRADecoder</b> / '
    '<b>SPARTA AmbiDEC</b> for loudspeakers. Optionally add <b>IEM EnergyVisualizer</b> to watch the sound move.',
    'Play. Open the <b>SPACE</b> page to set how the points move.'])

# ============================================================== 3 panel overview
H1('3  The panel')
SHOT('obliqsound_polygon.png', 'obliqsound with the polygon page open. the upper half is always there; the lower half changes with the folded strips.')
P('Everything is placed in mirror symmetry about the centre line. The upper part is the instrument (oscillator spine in the middle, '
  'filters and envelopes on the wings, mixer and output in the shoulders, the sampler fan, the keys layer and the swarm depth at the bottom corners). '
  'The lower part is the page area, chosen by the seven (six on ObliqSound) folded strips under the sampler.')
H2('Header')
CTRL([['preset list', '133 + yours', 'Recalls a preset; &lt; and &gt; step through the list. Presets include every control, the pins of the matrix and the CC numbers.'],
      ['Save', '-', 'Saves the current sound under a name you type.'],
      ['Remove', '-', 'Deletes a saved preset.'],
      ['Randomize', '-', 'Rolls a new sound: both filters, envelopes, oscillator, polygon and Odessa settings, the effects, the field-recording '
       'window, the waveform, and picks a <b>random sampler square</b> with an audible SAMPLER level. On ObliqSonic it also rolls the motion (mode, '
       'speed, deviation, gravity, cohesion, field splay, height, rotation).'],
      ['Help', '-', 'Opens the built-in help window (chapter 12). F1 does the same.'],
      ['size', '50-200 %', 'Scales the whole window. The panel is drawn as vectors, so every size is sharp.']])
H2('The look')
P('The panel is the faltung look. The field is one flat colour: pale sage on ObliqSound, deep green on ObliqSonic, the one the accent of the other. '
  'Every knob keeps its place but is drawn as a <b>skewed bar</b>: a thin rule, an accent fill that grows from the left (or from the centre for a control '
  'that swings both ways) and a white tick at the value. Drag right or up to raise it, left or down to lower it; while you drag, a small chip shows the name and value. '
  'Vertical sliders are the same bar stood up. Switches are small parallelograms, filled when on. Lists are underlined names with a small arrow. '
  'The pages are a row of <b>folded paper strips</b> in the bundle\'s pastels; the open page has a white outline. '
  'The pin matrix is a grid of <b>chips</b>, and on ObliqSonic the scene is a thin <b>wireframe sphere</b>. Nothing moves on its own except what the sound moves: '
  'the lights, the scopes, the birds, the attractor and the points in the sphere.')

# ============================================================== 4 sound sources
H1('4  Voice: oscillators, mixer, filters, envelopes')
H2('Oscillator spine (centre)')
CTRL([['PITCH', '-15.7 .. +48 st', 'Coarse pitch of the whole instrument, in semitones (the big knob at the top).'],
      ['FINE', '1 .. 1.666', 'Fine tuning as a frequency ratio (up to about +8.8 semitones).'],
      ['WAVE', '4 positions', 'Waveform of the VCO: triangle, square/pulse, saw, sine (the small line drawing beside the bar shows it).'],
      ['-1  0  +1', 'buttons', 'Octave down / normal / up.'],
      ['P.WIDTH', '0.01 .. 0.99', 'Pulse width of the square wave (also shapes the other waves slightly).'],
      ['DETUNE', '0 .. 1', 'Detune of the unison stack (see UNISON on the LFO page).'],
      ['VIBRATO', '0 .. 1', 'Amount of LFO 2 on pitch.']])
H2('Mixer (left shoulder)')
CTRL([['SAMPLER', '0 .. 0.2', 'Level of the ambient bed: the nature scene or field recording chosen in the sampler. The curve is gentle at the bottom so small moves stay controllable.'],
      ['KEYS', '0 .. 1', 'Level of the field recording played from the keyboard (the keys layer, chapter 5).'],
      ['VCO', '0 .. 6', 'Level (and drive) of the virtual-analogue oscillator stack.'],
      ['NOISE', '0 .. 0.5', 'Noise level; its colour is set by TONE in the output section.'],
      ['POLYGON', '0 .. 1', 'Level of the polygonal oscillator (POLYGON page).'],
      ['ODESSA', '0 .. 1', 'Level of the additive oscillator (ODESSA page). At 0 it is not computed at all, which saves CPU.']])
H2('Filter A and Filter B (wings)')
P('Each voice runs through a dual <b>Bionic-Lester-style</b> filter: a gain/overdrive stage into a two-pole state-variable filter whose '
  '<b>MODE</b> morphs continuously LP > BP > HP > notch > comb (all-pass at zero resonance, positive comb towards noon, negative comb past noon). '
  'How A and B are connected, and their CV inputs, are set on the LESTER page.')
CTRL([['CUTOFF A / B', '35 Hz .. 18 kHz', 'Cutoff of each filter. A follows the filter envelope (ENV AMT).'],
      ['RES', '0 .. 0.99', 'Resonance.'],
      ['MODE', '0 .. 4', 'Morphing response: 0 LP, 1 BP, 2 HP, 3 notch, 4 comb.'],
      ['DRIVE', '0 .. 1', 'Input gain into the filter; past the middle it overdrives.'],
      ['ENV AMT', '0 .. 1', 'How far the filter envelope sweeps cutoff A.'],
      ['VCA INIT', '0 .. 1', 'Drone: after you let go, the last chord keeps sounding at this level until you play a new chord (polyphonic).']])
H2('Envelopes')
CTRL([['FILTER ENV  A D S R', 'A 0.01-1, D 0-1, S, R 0.01-2', 'The filter envelope (left wing).'],
      ['AMP ENV  A D S R', 'A 0-1, D 0-1, S, R 0.01-2', 'The amplitude envelope (right wing).']])
P('A third envelope, the <b>mod envelope</b> (ATTACK / DECAY on the LFO page), is a free modulation source, and <b>P.ENV</b> sends it to pitch.')
H2('Output (right shoulder)')
CTRL([['CHORUS', '0 .. 1', 'Three-tap ensemble chorus.'],
      ['TONE', '0.1 .. 6000', 'Colour of the noise generator (dark to bright).'],
      ['SPACE', '0 .. 1', 'Size and brightness of the reverb.'],
      ['REVERB', '0 .. 0.5', 'Reverb amount.'],
      ['OUTPUT', '0 .. 12', 'Master level (3 is unity).'],
      ['DRIVE', '0 .. 1', 'Master saturation before the effects (on ObliqSonic it is applied per voice, so the spatial image stays clean).']])
P('The master chain is drive > chorus > tempo-synced ping-pong delay (with a slightly wobbly, filtered feedback) > reverb > level > soft clip and limiter. '
  'Delay amount, time and feedback live on the LFO page.')

# ============================================================== 5 sampler
H1('5  The sampler: nature scenes and field recordings')
P('The fan of 26 square buttons in the middle chooses what the sampler plays. The squares are numbered in '
  '<b>Ge\'ez (Ethiopic) numerals</b>; the chip under the fan shows the current one. Squares 1-8 are synthesized '
  'nature scenes (generated live, never the same twice); squares 9-26 are field recordings: 8-second, 16-bit loops cut from the '
  'middle of longer recordings.')
num = ' ፩፪፫፬፭፮፯፰፱'
ten = ' ፲፳'
def eth(n): return (ten[n // 10] if n >= 10 else '') + (num[n % 10] if n % 10 else '')
NAT = ['wind', 'birds', 'beach', 'geyser', 'forest', 'water', 'thunder', 'night']
FILES = open(os.path.join(OBLIQ, 'samples', 'index.txt')).read().split()
rows = [['', 'No.', 'Source']]
lst = [(i + 1, 'nature: ' + NAT[i]) for i in range(8)] + [(9 + i, 'recording: ' + f) for i, f in enumerate(FILES)]
half = (len(lst) + 1) // 2
data = [['sq.', 'numeral', 'plays', 'sq.', 'numeral', 'plays']]
for i in range(half):
    r = []
    for k in (i, i + half):
        if k < len(lst):
            n, s = lst[k]
            r += [Paragraph(str(n), ss['cellb']), Paragraph(f'<font name="{GEEZ}" size="12">{eth(n)}</font>' if GEEZ else str(n), ss['cell']), Paragraph(esc(s), ss['cell'])]
        else:
            r += ['', '', '']
    data.append(r)
data[0] = [Paragraph(c, ss['cellb']) for c in data[0]]
t = Table(data, colWidths=[10 * mm, 18 * mm, 57 * mm] * 2, repeatRows=1)
t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, 0), SAGE_L), ('LINEBELOW', (0, 0), (-1, 0), 0.8, GREEN), ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
                       ('LINEBELOW', (0, 0), (-1, -1), 0.3, RULE), ('TOPPADDING', (0, 0), (-1, -1), 2), ('BOTTOMPADDING', (0, 0), (-1, -1), 2)]))
story += [t, Spacer(1, 8)]
H2('The ambient bed')
P('<b>SAMPLER</b> (mixer) sets the level of the chosen square as a continuous bed. For field recordings the bed loops a window of the '
  'file whose <b>start and end points are always slowly sliding</b>: the start wanders over the first half of the file and the loop length '
  'over 30-80 % of it, at an unhurried pace. So the bed never repeats exactly. <b>SPEED</b> sets its playback rate '
  'and <b>REVERSE</b> plays it backwards.')
H2('The keys layer')
P('<b>KEYS</b> (mixer) turns the recording into a playable instrument: every note micro-loops a piece of the recording.')
CTRL([['PITCH', '-24 .. +24 st', 'Transposition of the keys layer.'],
      ['POSITION', '0 .. 1', 'Where in the file the note\'s loop sits (also drifts slowly with the loop points).'],
      ['SIZE', '0.002 .. 1', 'Loop length, from a grain to the whole file (also breathes slowly).'],
      ['KEYTRACK', 'on / off', 'On: the keys play the recording chromatically. Off: every key plays it at its own pitch.'],
      ['SCATTER', '0 .. 1', 'Each note starts at a random offset around POSITION.'],
      ['SPEED', '0.25 .. 2', 'Playback rate of the bed.'],
      ['REVERSE', 'on / off', 'Plays the bed backwards.'],
      ['GLIDE', '0 .. 1', 'Portamento between notes (for all the voice\'s oscillators).']])
H2('Audio followers')
P('Two envelope followers turn audio into modulation: <b>Follower</b> listens to the sampler (bed + keys layer + nature), <b>Output</b> listens to the '
  'whole instrument. Their attack, release and gain are on the LFO page (F.ATTACK, F.RELEASE, F.GAIN). Use them in the matrix like any source.')
NOTE('<b>Nature resonator:</b> the RESONATE knob (LFO page) lets the selected nature scene ring through a waveguide tuned to each note, '
     'so wind or rain takes on the pitch of what you play.')

# ============================================================== 6 pages
H1('6  The pages')
H2('POLYGON - the polygonal oscillator')
P('Inspired by the E-RM Polygogo: a waveform made by tracing a polygon in X/Y. A continuous ORDER (number of sides, fractions allowed) and '
  'TEETH (star-shape) make the timbre; ROLL rotates it; a phase-modulation operator (1:N, AMT) adds sidebands. The screen draws the polygon live as a thin accent line.')
CTRL([['TUNE (slider)', '-24 .. +24', 'Pitch of the polygon relative to the VCO.'],
      ['ORDER', '2.1 .. 16', 'Number of sides, continuous.'],
      ['TEETH', '0 .. 1', 'Star-ness: pulls alternate vertices inward.'],
      ['ROLL', '0 .. 1', 'Rotation of the shape (one turn).'],
      ['1:N', '0.25 .. 16', 'Ratio of the phase-modulation operator.'],
      ['AMT', '0 .. 1', 'Amount of the operator.'],
      ['Jacks (knob + list above/below each slider)', '-1 .. 1 / source', 'Each slider has a jack: choose a modulation source in the list and its amount with the small knob (attenuverter).'],
      ['FM IN', '0 .. 1', 'Audio-rate FM from the VCO.'],
      ['FOLD', '0 .. 1', 'Wavefolder on the polygon output.'],
      ['SLEW', '0 .. 1', 'Glide time for ORDER and TEETH changes, so the shape morphs instead of jumping.'],
      ['WIDTH', '0 .. 1', 'Stereo width: the X trace goes left and the Y trace right; at 0 both are mixed to the centre.'],
      ['SKEW', '-1 .. 1', 'Phase skew of the trace.'],
      ['SPIN', '-10 .. 10', 'Precession: the polygon rotates continuously at this rate.'],
      ['CYCLE', 'on / off', 'Precession: the polygon turns once every ORDER periods, so its corners walk round the cycle.'],
      ['REGULAR', 'on / off', 'Quantises ORDER to whole numbers.'],
      ['OP RATIO', 'on / off', 'Snaps 1:N to musical ratios.']])
SHOT('obliqsound_odessa.png', 'the odessa page.')
H2('ODESSA - the additive oscillator')
P('After the XAOC Devices Odessa: up to 128 partials per voice with spectral tilt, tension (stretching), a comb-like density filter, '
  'warping and peaking, split into two banks, eight partial-group gates and a chord mode that stacks up to five voices.')
CTRL([['PARTIALS', '1 .. 128', 'Number of partials.'],
      ['TILT', '0 .. 1', 'Spectral slope: dark to bright.'],
      ['TENSION', '-1 .. 1', 'Stretches or squeezes the partial series (bell and string-like inharmonicity).'],
      ['DENSITY', '0 .. 1', 'A sweeping comb over the spectrum (the Odessa "density" filter).'],
      ['WARP', '0 .. 1', 'Warps the comb across the spectrum.'],
      ['PEAKING', '0 .. 1', 'Sharpness of the comb peaks.'],
      ['BANK', '-8 .. 8', 'Splits partials into two alternating banks (A left, B right) in groups of |BANK|; the sign picks which bank is retuned by FACTOR.'],
      ['FACTOR', '-7 .. 7', 'Frequency factor of the retuned bank (x2, x3 ... or /2, /3 ...).'],
      ['FUNDAMENTAL + list', '0 .. 1', 'Adds a sine or square fundamental under the stack.'],
      ['ODD / EVEN', '0 .. 1', 'Stereo width of the two banks (A left, B right); at 0 both banks sit in the middle.'],
      ['HEL CHORD + voices', '12 chords; 1/3/5', 'Stacks voices as unison, octaves, fifths, sus, major, minor, maj7, min7, add9, quartal, cluster or whole tone.'],
      ['Gates 1-8', 'on / off', 'Mute or pass the eight groups of partials.'],
      ['TUNE / FINE / SPREAD', '', 'Coarse and fine pitch, and detune spread of the chord voices.'],
      ['EXP FM, LIN FM, DENSITY, WARP CV', '-1 .. 1 / source', 'Four CV sliders, each with a source list (any matrix source).']])
P('The arc of small frames above PARTIALS is a spectrum strip; it is not animated.')
SHOT('obliqsound_lester.png', 'the lester page: cv inputs for a and b, the read-outs, the link mode and the a/b mix.')
H2('LESTER - the dual filter in depth')
CTRL([['Link mode', '5 modes', '<b>link off</b>: A on the left, B on the right, mixed by MIX. <b>A ctl</b>: B\'s cutoff follows A. <b>series</b>: A then B. '
       '<b>4pole</b>: B copies A and is in series (a steeper slope). <b>stereo</b>: A processes the left channel and B the right.'],
      ['A  MIX  B', '0 .. 1', 'Balance between the outputs of A and B.'],
      ['CV 1-3 (per filter)', 'source / dest / -1..1', 'Three CV inputs per filter: pick a source, a destination (cutoff, resonance, mode, gain) and an amount. CV 1 always goes to cutoff.'],
      ['RANDOMIZE', 'button', 'Rolls both filters (like the module\'s "randomize current").'],
      ['read-out', '-', 'The two lines in the middle show each filter\'s cutoff, resonance and mode.']])
SHOT('obliqsound_lfo.png', 'the lfo page with its two scopes.')
H2('LFO - modulators, voice character, delay')
CTRL([['LFO 1 / 2  RATE', '0.02 .. 20 Hz', 'LFO speed. With SYNC on it snaps to musical divisions of the host tempo.'],
      ['SHAPE', '1 .. 6', 'Sine, triangle, saw, square, sample & hold, smooth random.'],
      ['ATTACK / DECAY', 'seconds', 'The mod envelope (a matrix source; P.ENV sends it to pitch).'],
      ['F.ATTACK / F.RELEASE / F.GAIN', '', 'Response and sensitivity of the audio followers.'],
      ['DELAY / TIME / FEEDBACK', '', 'Ping-pong delay: amount, time in note values (1/32 .. 1/2 note incl. triplets and dotted), feedback.'],
      ['UNISON + LOCK', '1 .. 7', 'Voices in the unison stack; LOCK keeps their phases together.'],
      ['DRIFT', '0 .. 1', 'Slow analogue pitch drift per unison voice.'],
      ['SUB + SUB-2', '0 .. 1', 'Square sub oscillator one (or two) octaves down.'],
      ['P.ENV', '-1 .. 1', 'Mod envelope to pitch.'],
      ['FOLD, RATIO, FM', '', 'Wavefolder on the VCO and a sine FM pair (ratio and index).'],
      ['SPREAD', '0 .. 1', 'Stereo spread of the unison voices.'],
      ['BEND', '0 .. 24', 'Pitch-bend range in semitones.'],
      ['VELOCITY', '0 .. 1', 'How much velocity changes loudness.'],
      ['RESONATE', '0 .. 1', 'Nature resonator (chapter 5).'],
      ['STEREO', '0 .. 1', 'Random pan per note (on ObliqSonic: a random turn of each note round the listener).']])

# ============================================================== 7 matrix
H1('7  The pin matrix')
SHOT('obliqsound_matrix.png', 'the matrix page: sources are rows (names in the spine), destinations are columns (abbreviations along the top). the page strips sit in the gutter.')
P('Like the EMS Synthi\'s patch board, every source can be connected to every destination with a <b>pin</b>. There are 22 sources and 36 destinations '
  '(42 on ObliqSonic): 792 (924) pins, drawn as a grid of chips. <b>Drag a chip up</b> and it fills upward from its centre in the accent colour: a positive connection, more the further you drag. '
  '<b>Drag down</b> and it fills downward in salmon: an inverted connection. An empty outline is no connection. Hover a chip to see "source > destination" and the amount. '
  'Any number of pins can be in at once (up to 128 are processed); per-voice sources such as velocity, key and the envelopes act separately on each note.')
H2('Sources (rows)')
TABLE([['Source', 'What it is'],
       ['LFO 1, LFO 2', 'The two LFOs (LFO page).'],
       ['MOD ENV, AMP ENV', 'The mod envelope and the amplitude envelope, per note.'],
       ['VELOCITY, KEY', 'Note velocity, and key position (0 at middle C, +-1 two octaves away).'],
       ['MOD WHEEL, AFTERTOUCH', 'MIDI CC 1 and channel pressure.'],
       ['RANDOM', 'A new random value for each note.'],
       ['CHAOS', 'A slowly wandering chaotic signal.'],
       ['NATURE', 'Envelope of the nature scene.'],
       ['FOLLOWER, OUTPUT', 'The audio followers (sampler, and the instrument\'s output).'],
       ['BOID X, BOID Y', 'Each note follows its own bird of the 8-boid flock (SWARM page).'],
       ['LORENZ X, Y, Z', 'The three axes of the Lorenz attractor.'],
       ['CC A, CC B, CC C, CC D', '<b>Incoming MIDI controllers.</b> The number beside each row chooses the CC number (drag or type, 0-127; defaults 74, 71, 21, 22). Any MIDI channel is accepted; the most recently moved one wins. Values are smoothed.']],
      [45, 125])
H2('Destinations (columns)')
TABLE([['abbr.', 'destination', 'abbr.', 'destination'],
       ['PCH', 'Pitch', 'P.OR', 'Polygon order'], ['CUT', 'Cutoff A', 'P.TE', 'Polygon teeth'],
       ['RES', 'Resonance A', 'P.RL', 'Polygon roll'], ['PW', 'Pulse width', 'P.OA', 'Polygon operator amount'],
       ['FLD', 'Fold', 'P.LV', 'Polygon level'], ['FM', 'FM amount', 'P.FD', 'Polygon fold'],
       ['SUB', 'Sub level', 'O.PA', 'Odessa partials'], ['NOI', 'Noise level', 'O.TL', 'Odessa tilt'],
       ['RSN', 'Nature resonator', 'O.TN', 'Odessa tension'], ['DTN', 'Detune', 'O.DN', 'Odessa density'],
       ['AMP', 'Amplitude', 'O.WP', 'Odessa warp'], ['PAN', 'Pan (ObliqSonic: turn round the listener)', 'O.PK', 'Odessa peaking'],
       ['L1R, L2R', 'LFO 1 / 2 rate (global)', 'O.LV', 'Odessa level'], ['DLY', 'Delay send', 'F.PS, F.SZ, F.LV', 'Keys layer position, size, level'],
       ['REV', 'Reverb send', 'B.CT', 'Cutoff B'], ['L.MD, L.MX, L.GN', 'Lester mode, A/B mix, gain', '', ''],
       ['S.DS, S.AZ, S.EL', 'ObliqSonic only: the note\'s distance, azimuth, elevation', 'S.SP, S.DV, S.AR', 'ObliqSonic only: spread, deviation, air']],
      [22, 63, 25, 60], lower=(0, 2))
NOTE('Patches to try: LFO 1 > CUT (a slow sweep); VELOCITY > CUT (harder is brighter); CC A > O.DN (a knob on your controller sweeps the Odessa comb); '
     'BOID X > PAN or, on ObliqSonic, BOID X > S.DV and VELOCITY > S.AZ (each note flies off on its own path).')

# ============================================================== 8 swarm
H1('8  The swarm: boids and Lorenz on every parameter')
SHOT('obliqsound_swarm.png', 'the swarm page: the birds (white squares) and the lorenz attractor (a fading trail of accent squares).')
P('Every continuous parameter of the instrument (92 on ObliqSonic) rides its own boid coordinate and its own axis of a Lorenz attractor. '
  'Two knobs in the bottom corners of the upper panel set how much: <b>BOIDS</b> (left) and <b>LORENZ</b> (right). At zero the instrument is completely '
  'still; a little of each makes it breathe; a lot makes it wander.')
CTRL([['SPEED, COHESION, SEPARATION, ALIGNMENT', '0 .. 1', 'The flock of 8 agents: how fast they fly, how strongly they gather, avoid each other and match each other\'s heading.'],
      ['RATE, SIGMA, RHO, BETA', '', 'The Lorenz attractor: speed and its three constants (10, 28, 2.667 is the classic butterfly).'],
      ['SOURCE, POLYGON, ODESSA, FILTER, FX, MOTION', 'on / off', 'Which sections of the instrument the swarm moves.'],
      ['V.BOIDS', 'on / off', 'Scales the per-note BOID X / Y matrix sources.']])

# ============================================================== 9 obliqsonic
H1('9  ObliqSonic: the spatial instrument')
SHOT('obliqsonic_space.png', 'obliqsonic with the space page open: the wireframe sphere shows every point from above (front is up, left is left; a bigger chip is higher).')
P('ObliqSonic is an <b>Ambisonics encoder that plays itself</b>. It receives MIDI like ObliqSound, makes the same sounds, and places them in '
  'a sphere of sound around the listener. It does not decode: it outputs the 16 Ambisonic channels for a decoder to turn into speakers or headphones.')
H2('The points')
TABLE([['points', 'what they carry', 'chip colour'],
       ['12 voice birds', 'Each note rides bird (slot mod 12): consecutive notes of a chord take neighbouring birds, so a chord spreads out. The note\'s own '
        'matrix destinations (S.DS, S.AZ, S.EL, S.SP, S.DV, S.AR, PAN) push it further, round, up, wider or astray.', 'white'],
       ['6 field copies', 'Six copies of the selected field recording play at once, <b>each with its own randomly wandering loop start and end</b>, '
        'each on its own path. (Choose squares 9-26 and raise SAMPLER.)', 'salmon'],
       ['2 FX returns', 'The delay and reverb (and the VCA INIT drone), as two wide sources.', 'amber'],
       ['2 nature points', 'The nature scene, left and right halves, as two broad sources.', 'amber']], [30, 115, 25])
H2('Motion')
P('All 22 points move continuously. <b>MODE</b> morphs between three motion systems, crossfading their positions:')
UL(['<b>FLOCK</b> (MODE 0): a three-dimensional flock. The birds keep their distance from each other, match headings and gather, half towards the '
    'flock\'s centre and half towards a leader circling the listener, so the group sweeps round you.',
    '<b>LORENZ</b> (MODE 1): every point has its own Lorenz attractor, with its constants and starting point <b>deviated</b> and its butterfly '
    'turned to its own heading. The points scatter over the whole sphere and trace chaotic loops.',
    '<b>GRAVITY</b> (MODE 2): three gravity wells orbit slowly; the points fall round them, slingshot and gather in clusters.',
    'In between, the two neighbouring systems are blended (the label under the sphere shows flock + lorenz, lorenz + gravity ...). The swarm modulation '
    'can also move MODE itself.'])
H2('SPACE page controls')
CTRL([['MODE', '0 .. 2', 'Flock > Lorenz > Gravity.'],
      ['SPEED', '0 .. 1', 'How fast everything moves.'],
      ['DISTANCE', '1 .. 12 m', 'Radius of the scene. Far sources are quieter, later (Doppler), duller (air) and smaller.'],
      ['HEIGHT', '0 .. 1', 'Vertical extent: 0 keeps everything on the horizon, 1 uses the full sphere.'],
      ['DEVIATION', '0 .. 1', 'How far each point strays from its system: its own slow wander, its own Lorenz constants and heading.'],
      ['ROTATE', '-1 .. 1', 'Turns the whole scene round the listener, either way (up to one turn in 10 s).'],
      ['SPREAD', '0 .. 1', 'Size of the sources: turns the higher orders down, so each point becomes a broad, diffuse source.'],
      ['DOPPLER', '0 .. 1', 'Scales the propagation delay: points flying towards you rise in pitch, away fall.'],
      ['AIR', '0 .. 1', 'Air absorption: distant points lose their highs.'],
      ['GRAVITY', '0 .. 1', 'Strength of the gravity wells.'],
      ['COHESION', '0 .. 1', 'How tightly the flock gathers.'],
      ['FIELD SPLAY', '0 .. 1', 'Spreads the six field copies from their own centre: 0 one tight cloud, 1 all over the sphere.']])
H2('Distance model')
P('After the IEM RoomEncoder: each point has a propagation delay (distance / 343 m/s, scaled by DOPPLER), a 1/r level relative to a reference '
  'that follows DISTANCE (so turning DISTANCE up does not simply make everything quiet), and a low-pass for air absorption. Inside 1 m the higher orders '
  'fade so a source near your head spreads round you and gets up to 6 dB louder.')
H2('Output format')
TABLE([['', ''],
       ['Order', 'Third order: 16 channels'],
       ['Channel order', 'ACN (W, Y, Z, X, then the 2nd and 3rd order harmonics)'],
       ['Normalisation', 'SN3D (AmbiX). Set decoders to SN3D, order 3.'],
       ['Axes', 'x front, y left, z up (azimuth counter-clockwise, as in IEM and SPARTA)'],
       ['Output buses', '8 stereo buses = 16 channels. Hosts show 16 output pins; pin n goes to channel n.'],
       ['Limiter', 'A linked peak limiter driven by the omni (W) channel keeps the 16 channels in proportion.']], [40, 130], head=False)
P('The VU meters show a virtual cardioid pair (W+Y, W-Y), and the scopes and audio followers listen to the omni channel.')

# ============================================================== 10 routing
H1('10  Using ObliqSonic with IEM and SPARTA')
H2('REAPER (recommended)')
UL(['Insert ObliqSonic on a track. Track <b>Route</b> > <b>Track channels: 16</b>. Check the plug-in\'s pin connector (the "2 in 16 out" button in the FX window): outputs 1-16 should go to track channels 1-16 (the default).',
    'Headphones: insert <b>IEM BinauralDecoder</b> (order 3, SN3D) after ObliqSonic. Loudspeakers: <b>IEM AllRADecoder</b> or <b>SPARTA AmbiDEC</b>, with your speaker layout, and route the track to as many hardware outputs as speakers.',
    'Optional: <b>IEM SceneRotator</b> to rotate with a head tracker, <b>IEM FdnReverb</b> for an Ambisonic room, <b>IEM EnergyVisualizer</b> to see the energy.',
    'To mix several Ambisonic tracks, send them (16 channels) to one bus that holds the decoder.'])
H2('Other hosts')
P('Any host that can give an instrument 16 outputs and run a 16-channel decoder works (Bitwig, Nuendo, Pro Tools Ultimate in Ambisonics mode, Max, '
  'Plogue Bidule). Hosts that only use the first stereo bus will hear W and Y only: that is the omni channel plus left/right difference, a usable '
  'mid/side stereo, but not the full scene.')
NOTE('<b>Order and normalisation must match.</b> A decoder left at 1st order still works (it uses channels 1-4) but the image is blurrier. A decoder set '
     'to N3D will sound too bright and wide in the higher orders.')

# ============================================================== 11 presets / tips
H1('11  Presets, randomize and tips')
P('The 133 presets include the original Kia instrument sounds (with the new sections set up for them) and 32 new ones in the families XL, POLY, ODESSA, '
  'FIELD and LESTER. Older presets that used the earlier 8-slot mod matrix were converted into matrix pins. ObliqSonic ships with the same presets; '
  'the SPACE controls keep their current values when a preset from ObliqSound is loaded.')
UL(['<b>Slow evolving pad:</b> ODESSA up, PARTIALS 64, TILT 0.3, LFO 1 > O.DN, BOIDS and LORENZ at 0.2, REVERB 0.3.',
    '<b>Played field recording:</b> choose a recording square, KEYS 0.6, SIZE 0.05, KEYTRACK on, SCATTER 0.3: the recording becomes a granular instrument.',
    '<b>Spatial rain (ObliqSonic):</b> a water recording, SAMPLER 0.1, MODE 2 (gravity), FIELD SPLAY 1, DISTANCE 6, AIR 0.6.',
    '<b>Chord that explodes outward (ObliqSonic):</b> pin AMP ENV > S.DS (upward) and RANDOM > S.AZ: each note starts close and flies off on its own bearing.'])

# ============================================================== 12 help
H1('12  The help window')
SHOT('obliqsound_help.png', 'the help window, open on the start topic.')
P('Both plug-ins and both desktop apps have a built-in help page. Press <b>help</b>, beside the preset list at the top left, or <b>F1</b>. '
  'It opens over the panel as a flat window in the field colour, with the topics on the left and the text on the right.')
CTRL([['Topics', '10 (11 on ObliqSonic)', 'START, CONTROLS, VOICE, SAMPLER, PAGES, LFO + DELAY, MATRIX, SWARM, SPACE (ObliqSonic only), PRESETS, APP + ABOUT. '
       'Click a topic, or use the up and down arrow keys.'],
      ['manual (pdf)', 'link', 'Opens this manual, which is included inside each plug-in and app.'],
      ['close', '×, Esc', 'The × at the top right, the Esc key, F1 again, or a click anywhere outside the window.']])
P('The instrument keeps playing while help is open, from MIDI and from the computer keyboard, so you can read about a control and hear it. '
  'The controls under the window cannot be moved until it is closed.')
NOTE('<b>Two questions the help answers first.</b> The amp <b>ADSR</b> is the four small knobs A D S R on the right wing under FILTER B (AMP ENV); '
     'the filter envelope is the same four on the left wing. The <b>delay time and feedback</b> are on the LFO page, bottom left, under FOLLOW / DELAY.')

# ============================================================== 13 desktop app
H1('13  The desktop app')
P('<b>ObliqSound.app</b> and <b>ObliqSonic.app</b> (in /Applications/faltung, with the rest of the bundle) are the instruments on their own, with no DAW.')
CTRL([['Options (top left)', '', 'Audio output, sample rate, buffer size and MIDI inputs. Every MIDI input is turned on at start, and new devices are picked up when plugged in.'],
      ['Computer keyboard', '', 'Plays notes (below). Click the panel first so it has the keyboard.'],
      ['Help', 'F1', 'The same help window as in the plug-ins (chapter 12).'],
      ['ObliqSonic: output', 'size list', 'The size list at the top right also chooses the output: <b>stereo</b> (a decode to two virtual cardioids, the default in the app) '
       'or <b>16-channel Ambisonics</b> for an interface or virtual device with 16 outputs.']])
TABLE([['Keys', 'Notes'],
       ['A  W  S  E  D  F  T  G  Y  H  U  J  K  O  L  P  ;  \'', 'C  C#  D  D#  E  F  F#  G  G#  A  A#  B  C  C#  D  D#  E  F'],
       ['Z / X', 'Octave down / up'],
       ['C / V', 'Velocity down / up (steps of 20, starts at 100)']], [70, 100])
P('The keys are ignored while Command is held.')

# ============================================================== 13 tech
H1('14  Technical reference')
TABLE([['', ''],
       ['version', VERSION + ', part of faltung'],
       ['Engine', 'scsynth, the SuperCollider synthesis server (3.13), embedded in the plug-in and run in the host\'s audio callback. Both ObliqSound and ObliqSonic use it for all synthesis and sample playback; there is no other sound engine'],
       ['Custom UGens', 'Morphing zero-delay state-variable filter with drive and comb; 2x oversampled polygon oscillator; 128-partial additive bank; third-order Ambisonics encoder with distance'],
       ['Modulation', 'Computed in C++ once per 64 samples (envelopes, LFOs, pin matrix, swarm, motion) and written straight to the server\'s control buses; filter cutoff is smoothed per sample'],
       ['Polyphony', '16 voices with oldest-note stealing; a held VCA INIT chord keeps at most 8 notes'],
       ['Output stage', 'Look-ahead limiter, then 16-bit quantisation with dither'],
       ['Field recordings', '18 loops, 8 s, 16-bit stereo WAV, level-matched'],
       ['Swarm', '8 boids + Lorenz attractor moving the timbre parameters (never pitch, tuning, playback speed or levels)'],
       ['Matrix', '22 sources x 36 destinations (ObliqSound), x 42 (ObliqSonic); up to 128 active pins'],
       ['MIDI', 'Notes, velocity, pitch bend, mod wheel, channel pressure, any 4 CCs (any channel)'],
       ['ObliqSound output', 'Stereo'],
       ['ObliqSonic output', '16 channels, 3rd-order Ambisonics, ACN / SN3D; 22 moving point sources'],
       ['formats', 'macOS (Apple silicon and Intel): VST3, Audio Unit, VST2, desktop app. Windows (x64): VST3, VST2, desktop app. Linux (x86-64): VST3, VST2, LV2, desktop app'],
       ['source', 'github.com/femifleming/faltung. From the faltung root: <font name="Plex">obliqsound/sc/fetch_sc.sh</font> once, then '
        '<font name="Plex">cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release &amp;&amp; cmake --build build --target ObliqSound_All ObliqSonic_All ObliqSoundTest ObliqSonicTest</font>. '
        'The two Test tools check the sound, the presets, the state and every page, and write the pictures in this manual']], [40, 130], head=False)

H1('15  Troubleshooting')
TABLE([['Problem', 'What to do'],
       ['ObliqSonic sounds only in stereo or from one side', 'The track has fewer than 16 channels, or the decoder is missing. Set Track channels: 16 and insert a decoder at order 3, SN3D.'],
       ['ObliqSonic image is blurred or too bright', 'Check the decoder\'s order (3) and normalisation (SN3D).'],
       ['Crackles', 'Raise the host\'s buffer size (256 or 512).'],
       ['A CC does nothing', 'Check the number box in the matrix spine matches the CC your controller sends, and that a pin in that row is pulled up or down.'],
       ['Stuck notes', 'Send MIDI all-notes-off (CC 123), or play and release the note again.'],
       ['The bed is silent', 'SAMPLER is at 0, or a nature square is chosen with a very low level. Randomize always picks a square and an audible level.']],
      [55, 115])

# keep a heading on the same page as the screenshot that follows it
merged = []
for f in story:
    if isinstance(f, KeepTogether) and merged and isinstance(merged[-1], Paragraph) and getattr(merged[-1], 'toc', None):
        h = merged.pop()
        f = KeepTogether([h] + list(f._content))
    merged.append(f)
story[:] = merged
doc.multiBuild(story)
print('wrote', OUT, os.path.getsize(OUT) // 1024, 'KB')

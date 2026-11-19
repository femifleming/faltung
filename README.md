<p align="center"><img src="docs/icon.png" width="128" alt="faltung icon"></p>

# faltung

**Install:** `brew tap femifleming/tap && brew install --cask femifleming/tap/faltung` (macOS) — or download
`faltung-macOS.zip`, `faltung-Windows.zip` or `faltung-Linux.tar.gz` from the
[releases](https://github.com/femifleming/faltung/releases). Partials alone: `brew install --cask femifleming/tap/partials`; Cloudfin alone: `brew install --cask femifleming/tap/cloudfin`.
ConvoSolution, ObliqSound and ObliqSonic come with the faltung cask and zips (their old casks and repositories are retired); ConvoSolution's full response library, `ConvoSolution-IRs.zip` (140 spaces, CC BY / CC BY-SA), is a separate download on the releases page: unpack it into `Music/ConvoSolution`.

**Manuals:** [faltung Manual.pdf](docs/manual/faltung%20Manual.pdf) · [ms0 master Manual.pdf](docs/manual/ms0%20master%20Manual.pdf) · [Partials Manual.pdf](partials/docs/manual/manual.pdf) · [Cloudfin Manual.pdf](cloudfin/docs/manual/Cloudfin%20Manual.pdf) · [convosolution Manual.pdf](convosolution/docs/manual/convosolution%20Manual.pdf) · [obliqsound Manual.pdf](obliqsound/docs/manual/obliqsound%20Manual.pdf) (ObliqSound and ObliqSonic) · [faltung deep Manual.pdf](docs/manual-deep/faltung%20deep%20Manual.pdf) (also in every download).

**Two suites, one repository:** *faltung* (minimal / microhouse, below) and *faltung deep* (deep house, [further down](#faltung-deep)).
`brew install --cask femifleming/tap/faltung-deep`, or `faltung-deep-macOS.zip`, `faltung-deep-Windows.zip`, `faltung-deep-Linux.tar.gz` from the releases. They install side by side.

Drums and processes for minimal / microhouse, after the 2000–2015 sound of Perlon, [a:rpia:r],
Villalobos and the Romanian scene. Twenty-three plug-ins: two drum machines, eight single drum modules,
six effects, a bass synth, a linear-phase mastering bus, Partials, an additive synthesizer with image resynthesis, Cloudfin, a floating multi-effect,
convosolution, a dual convolution reverb, and obliqsound with its Ambisonics twin obliqsonic, a polyphonic synth and field-recording instrument.
Every drum module uses the same synthesis code as the kit, so a sound moves between them unchanged.

The look follows the sleeves of that era: Mark Fell's flat colour fields with wireframe solids, snd's
folded pastel strips, Oval's coloured lines, stdio's pale chevrons and tracklists. The pictures are
drawn from the parameters and move as you change them.

Formats: VST3, AU, VST2 and standalone on macOS; VST3, VST2 and standalone on Windows; VST3, LV2, VST2
and standalone on Linux. (ObliqSound and ObliqSonic on Windows and Linux keep an `ObliqSound Resources` / `ObliqSonic Resources` folder beside the program and the VST2: move them together.)

## The plug-ins

| | name | what it is |
|---|---|---|
| **kt1** | kit | 8 synthesized voices (bd1 sd2 rm3 hh4 oh5 cp6 pc7 cl8) with a 16-step sequencer: microtiming per step, per-track shift, probability, ratchets, per-track length (polymeter), 8 patterns, swing. 8 extra stereo outputs. |
| **kt2** | loop | loop slicer + sequencer. Drop in a break, cut it into 4–32 slices (grid or detected hits), sequence the slices, reverse, re-pitch, gate. Per step: pitch (`step 1 pitch` … `step 16 pitch`, ±24 st), microtiming, ratchet 1–4 with a curve (AERIA's: the hits crowd in or spread out), probability; pattern length 1–16 (click a step number); a clock curve over the loop (accel / decel / arch / bulge / wave / rand). Loop follows the tempo. Starts with a break made from kt1. |
| **bd1** | kick | tok, thud, 909, sine. Pitch sweep, click, sub, wooden "tok" overtone, drive. |
| **sd2** | snare | tuned modes + wires, fm crack, brush, 808. |
| **rm3** | rim | modal rim / wood / tok / metal with mallet hardness. |
| **hh4** | hats | six-square 808 metal, noise, ring (fm), tick. Closed / open with choke. |
| **cp6** | clap | burst-train clap, finger snap, room clap, tight. |
| **pc7** | perc | membranes (conga, bongo), bell, glass. |
| **cl8** | click | ticks, pops, dust, blips: the "micro" in microhouse. |
| **tm9** | tom | toms, minimal blips, fm toms, low toms. |
| **mc3** | micro | live micro-sampling of its input: stutter cuts, scattered grains, clicks made from the input, synced to the host — or free-running: the last entry of *rate* is **free**, and *free rate* (0.5–200 Hz) sets the grid on its own. |
| **dl4** | dub | tape / bbd echo with the filters and saturation inside the feedback loop, wow and flutter, ping-pong, freeze. |
| **rv5** | room | 8-line feedback delay network: room, plate, air, chamber, with a tail gate. |
| **sw6** | swing | moves the audio itself onto a groove: every 16th is read from a delay line with its own offset. Templates, 16 per-step offsets, humanize, accents, and *learn groove* from a playing loop. 70 ms latency, reported to the host. |
| **fl7** | filter | 3-band DJ isolator (Linkwitz-Riley, kills) + resonant one-knob lp/hp sweep with drive and a synced lfo. |
| **gl8** | glue | bus compressor (soft knee, sidechain hp, program-dependent auto release) + tape (bias, head bump, hf loss). |
| **ms0** | master | mastering bus for microhouse and dense experimental / electroacoustic work: linear-phase eq (8 bands + hp / lp, each stereo, mid or side), 2 linear-phase dynamic eq bands (drawn in the analyser as the bells they really have: range dashed, what they do now filled, a bar for the -3 db width with its edge frequencies; drag for frequency / range, shift for fine, alt-drag or wheel for q, read-outs to a tenth of a hertz), linear-phase 4-band split with gentle compression and per-band width (sub mono), air, 2× oversampled saturation with focus, parallel ambience, glue, true-peak limiter with its own meter (VU with the 300 ms needle against a reference level you choose, true peak with hold, gain reduction, in and out, left and right), dither; every value can be typed (`1.2k`, `2k5`, `a4`, `-4.5`); BS.1770 loudness, true peak, correlation, mid / side analyser with draggable eq points, an imager (polar sample / polar level / lissajous) and level-matched compare. ~190 ms latency (reported). 9 presets. Own manual. |
| **Partials** | partials | additive / subtractive synthesizer: 516 partials per voice, harmonizer, prism, blur, unison, two filters, phaser, image and sound resynthesis, articulation envelopes, effects. Its own manual and history live in [partials/](partials). |
| **Cloudfin** | cloudfin | floating multi-effect: spectral smear, scale-locked pitch, formant, grains, just-intonation delay, reverb and compander in any order (drag the folded strips), a feedback loop, a random drift on every control, a hydrophone recording, gravity (the clouds fall into the sea, the sea sloshes in the sound), day / dusk / night. Oval line clouds, mirrored. Its own manual and history live in [cloudfin/](cloudfin). |
| **ConvoSolution** | convosolution | dual true-stereo convolution reverb after the Tasty Chips ECR+: two convolvers (a / b) with crossfade, each with its own response, position, attack / release, pitch (±2 octaves), predelay, high-pass and width; eight built-in responses and a library folder (the 140-space pack `ConvoSolution-IRs.zip` is on the releases page); recording of your own responses; three lfos (sine, triangle, saw, square, random, drift, lorenz, rössler, boid flock) into an 11-destination matrix; zero latency. A slate-blue field with the response as a thin wireframe and the matrix as skewed chips. Its own manual and history live in [convosolution/](convosolution). |
| **ObliqSound** | obliqsound | polyphonic synthesizer and field-recording instrument (grown out of the Kia instrument): 16 voices, virtual-analogue stack, polygonal oscillator, 128-partial additive oscillator, dual morphing filter (lp > bp > hp > notch > comb), a sampler of nature scenes and 18 field recordings with drifting loop points, a Synthi-style pin matrix (22 sources × 36 destinations), boid and Lorenz swarm modulation. SuperCollider's server, embedded. A sage field with skewed sliders, folded-strip pages and a chip matrix. Its own manual and history live in [obliqsound/](obliqsound). |
| **ObliqSonic** | obliqsonic | ObliqSound as a sound source for third-order Ambisonics (16 channels, ACN / SN3D, for the IEM and SPARTA decoders): every voice, six copies of the field recording, the effects and the nature bed are points that fly on a flock, Lorenz attractors or gravity wells, with Doppler, distance and air; a space page with a thin wireframe sphere. Same manual as ObliqSound. |
| **wb1** | wub | dubstep / neuro bass synth, after DMZ, Hench, Skream, Rusko, Noisa, Current Value, Billain. Two oscillators (sine to saw to square to "hard", up to 7-voice unison, b phase-modulates a), clean sub, noise; lp24/lp12/bp/hp/notch filter with drive; formant filter (a e i o u, the "yoy"); comb tuned to the note; 6 distortions (pre or post filter); host-synced wobble with sine, wow, yoy, ramps, square, random, routable to cutoff, vowel, fm, drive, pitch, volume, comb; mod envelope; mono / legato / glide; mod wheel to wobble depth or rate; 3-band "ott" compression; mono below 140 Hz. Runs at 2× oversampling. 10 presets (sub wob, square pump, wow, yoy, sub pressure, reese, fm growl, neuro growl, talk box, neuro stab). |

## faltung deep

The deep house twin: the same engines, retuned and re-preset for warm, swung house — round kicks, wooden
rims, soft room claps, shakers and congas, chord stabs and sub bass. Seventeen plug-ins (no mastering bus,
no Partials): **kt1** kit (kick, rim, clap, closed / open hat, shaker, conga, tom), **kt2** loop, the eight
modules **bd1 sd2 rm3 hh4 cp6 pc7 cl8 tm9**, the six effects **mc3 dl4 rv5 sw6 fl7 gl8**, and **sb1 stabs** —
the bass synth with a chord section (one key plays a minor 9, minor 7, 6/9 … in close, open or drop-2 voicing)
and organ, electric piano, pad, stab and sub presets. Every plug-in is named *faltung deep …* and opens on a
deep house preset.

The look follows record sleeves of deep house labels: a mirrored tree in one duotone per plug-in that sways
in a slow wind, sways more as the output gets louder and lights up on every hit, with the controls set like
the back of a sleeve (Side A / Side B track list, catalogue number, vertical credits). Barlow type.

Build: `cmake --build build --target faltung_deep_all faltungDeepTest`; `./build/faltungDeepTest_artefacts/Release/faltungDeepTest`.

## Automation

Every control is a normal host parameter with a fixed id and a readable name, so everything shows up in
Ableton's automation chooser (and Configure). In kt1 the names carry the voice: `rm3 tune`, `bd1 decay`,
`hh4 length`. All parameter moves are smoothed. The pattern (`pattern` a1–a8), mutes, track lengths,
probabilities and shifts are parameters too, so arrangement automation can switch patterns and thin
out tracks. In sw6 every step offset (`step 1` … `step 16`) is a parameter; in kt2 every step's pitch
(`step 1 pitch` … `step 16 pitch`), the `length`, the `clock curve` and `curve amount` are.

## MIDI

- kt1: C1–G1 (36–43) play bd1, sd2, rm3, hh4, oh5, cp6, pc7, cl8 (Push / Drum Rack bottom row).
- kt2: C1 and up plays slices 0, 1, 2 …
- wb1: any note; pitch bend; mod wheel (set *mod wheel* to wobble depth or rate). The on-screen keys play it too (< > shift octaves).
- modules: any note. With *key tracking* on, pitch follows the note from C3. hh4: A# notes (46, 58 …) play the open hat.
- The machines follow the host transport. In the standalone apps switch on *run* for the internal clock.

## Step grid (kt1, kt2)

click: on / off · drag up/down: velocity (kt2: slice) · drag across: paint · shift-drag or shift-scroll: microtiming ·
alt-click: probability (kt2: reverse) · right-click: ratchet 1–4 · alt-drag or alt-scroll: the ratchet's curve
(out 7 … even … in 7, after AERIA OS: the hits spread out or crowd together toward the end of the step) ·
kt2: cmd-drag or scroll: pitch per step (±24 st) · click a step number: pattern length (the loop ends there) ·
copy / paste / clear per pattern. kt2's *clock curve* (accel, decel, arch, bulge, wave, rand) and *curve amount*
speed the steps up or slow them down over each loop; the loop keeps its length, so step 1 always lands on time.

## Building

```
git submodule update --init
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
obliqsound/sc/fetch_sc.sh                                     # once: SuperCollider's server for ObliqSound / ObliqSonic (skipped with a warning without it)
cmake --build build --target faltung_all faltungTest PartialsTest
./build/faltungTest_artefacts/Release/faltungTest              # renders and checks all seventeen faltung plug-ins
./build/partials/PartialsTest_artefacts/Release/PartialsTest   # Partials' own tests
./build/cloudfin/CloudfinTest_artefacts/Release/CloudfinTest   # Cloudfin's (and CloudfinHostTest for its window)
./build/convosolution/ECRTest_artefacts/Release/ECRTest        # ConvoSolution's (and ECREngineTest for the convolution engine)
./build/obliqsound/ObliqSoundTest_artefacts/Release/ObliqSoundTest   # ObliqSound's (ObliqSonicTest for the twin)
```

GitHub Actions builds and tests everything on macOS (universal), Windows and Linux; a `v*` tag publishes a release.
On Linux the plug-ins are VST3, LV2 and VST2; copy them to `~/.vst3`, `~/.lv2` and `~/.vst`.
On Windows copy the `.vst3` folders to `C:\Program Files\Common Files\VST3`.

`faltungTest --wav dir --screenshots dir` writes audio and UI pictures. The app icons of every product of the house come from `scripts/mkicon.py`.

## Requests

Everything here was made from typed requests; they are logged in [REQUESTS.md](REQUESTS.md).

## Licence

AGPL-3.0 (obliqsound/ is GPL-3.0, as SuperCollider requires). Fonts: Inter, IBM Plex Mono and Barlow, SIL Open Font License (Resources/fonts).
ConvoSolution's impulse responses are CC BY 4.0 (OpenAIR, University of York) and CC BY-SA 4.0 (the HybridReverb2 database): sources in [convosolution/Resources/IR-CREDITS.txt](convosolution/Resources/IR-CREDITS.txt).
ObliqSound's field recordings are the author's own.

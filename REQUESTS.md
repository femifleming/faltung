# requests

What was asked for, in order, as typed.

1. "i love the music of ricardo villalobos, perlon music, arpiar, and all of these labels: [a list of ~80 labels: 9VOLT-Musik, [a:rpia:r], Cadenza, Perlon, Minus, Playhouse, Raum...musik, Rawax, Eastenderz, Understand, …] specifically the microhouse and minimal one, era 2000-2015 mostly. Can you make me a bundle of drum vsts and effects that i can use to make this kind of music? make them all very high quality. [five record sleeves: Mark Fell — Ten Types of Elsewhere, Mark Fell — ULB, Oval — ovalcommers, stdio, snd — tender love] make this the aesthetic, show me a mockup of the type the ui for the bundle will look like before you make it." (Mockup: mockup/index.html.)
2. "show me on this computer not in a link"
3. "perfect, so you will make drum machines and indivisual drum modules as well? in addition to everything else?"
4. "do loop and micro and tom, make it !" (kt1 kit, kt2 loop, eight modules, six effects.)
5. "make sure its easity for me to automate the parameters in ableton from the automation timeline"
6. "in the same style, i love the sound of old school dubstep, the wowow sound and yoy sound, all the fomrant stuff, i love the sound of Jakes, Loefah, Skream, Rusko, noisa, billain, current value. can you make a dubstep bass and neurobass synth vst for me to use? this is mostly for the bass and synth design. i want to make crzy basslines like DMZ label and Hench label, and noisa and current value basses. mostly early and mid 2000s era" (wb1 wub.)
7. "make a pdf manual for everything you just made" (docs/manual.)
8. "change the partials vst that you made for me a while back to be in this same aesthetic series."
9. "make it dark mode"
10. "bundle partials into faltung, make sure it works on mac windows and linux and push to my github, and add the homebrew tap. makr sure both manuals are on the github as well." (Partials moved in with its history; CI for macOS / Windows / Linux; casks faltung and partials.)

Partials' own earlier requests are in [partials/REQUESTS.md](partials/REQUESTS.md).
11. "in WB1, dont put the actual artist names in the preset names." (wb1 presets renamed by sound: sub wob, square pump, wow, yoy, sub pressure, reese, fm growl, neuro growl.)
12. "i also want a mastering bus vst part of this bunde. designed for both mastering that microhouse sound anf my dense experimenta electronic/electroacosutic work. it needs to be extemeely high quality, the eqs linear phase. in the same aesthetic. read this convo: [a conversation about a master-bus chain for dense music with extreme lows and highs: linear-phase and mid/side eq, dynamic eq / gentle multiband, saturation, frequency-dependent stereo imaging with mono subs, subtle parallel ambience, glue, true-peak limiting at -1 dbtp, loudness / true-peak / correlation metering, and bypassing every stage to judge it]" (ms0 master.)
13. "make it have a visualizer for mid side like ozone imageer has" (the imager: polar sample, polar level, lissajous, correlation, per-band width.)
14. "and give it presets as well." (nine presets.)
15. "make a manual for it when youre done" (ms0 master Manual.pdf, and a chapter in the faltung manual.)
16. "[path to the final wavs of Sadnoise_Cloudfin_Dreamstate] this is my music that i make if you want to anylyze the songs. make sure the vst is as high quality as possible, limiter section should have oversampling. i want to use this as the only plugin on my master track so it must have everything i need." (Analysis of the ten mixes (`faltungTest --analyze`); limiter + clipper oversampled 1x–8x at constant latency with a true-peak safety stage; saturation 4x; dc filter, balance, mid / side levels, loudness range and plr meters, loudness target, a / b; preset "dreamstate (from your mixes)"; manual updated.)
17. "for wb1, i feel like a complex moculation of lfo section would be good rihgt? so i can get those filter sweeps and wubby sounds?" (lfo 2 and lfo 3 up to 16 bars with one-shot, skew, smooth, smooth random; a 16-step lfo; an 8-slot matrix with 11 sources and 23 destinations incl. the lfo rates; a modulation page with live scopes; six presets.)

## 2026-11-14
- "to my hifi master bus vst, add an auto button that listens to the track live and then decides what the settings should be and changes the settings once to fit what it thinks would be good mastering settings for the specific track" → ms0 "auto": listens to 20 s of sound (silence not counted; click again to stop early), measures loudness/lra/true peak/crest/correlation + mid/side energy in ten regions and the four multiband bands, then resets ms0 to defaults and sets eq (half the way to a target curve, max 3 dB), high-pass, mono lows, widths, dynamic cuts for side mud / harshness / boxiness, multiband thresholds just above each band, glue, clipper, highs saturation, target + limiter gain + -1 dBTP ceiling; summary line under the top bar. `faltungTest --auto files...` shows what it would decide.
18. "make a one shots sample pack with the whole bundle for me to use." + "wav 24bit 44.1khz" (474 one-shots rendered from the engines into ~/Music/faltung one-shots: drums by engine and model, dub / room processed hits, wb1 bass, Partials sounds; `faltungTest --pack`, `PartialsTest --pack`.)
19. "can you make me a deep house version of the entire faltung suite, leave out the master bus and partials, [three sleeve images] have it be inspired by this and perlon records album covers, show me a few visual options before deciding the ui" (four mockups in mockup/deep.)
20. "A, i love how you generated the mirror tree in the background, can you make that a visually moving element as well?" (faltung deep: 17 plug-ins, mirror-tree look with a live tree that sways with the level and lights on hits; sb1 stabs with chord section; deep presets everywhere.)
21. "make a smaple pack from this when youre done too, after youre done test faltug and faltug deep and all the vsts and make sure they work under mac windows and linux, make manuals for both suited, release both seuited under one github repo and push them to github, dont push the sample packs to github." (faltung deep one-shots in ~/Music, not in the repo; faltung deep Manual.pdf; CI builds and tests both suites on macOS / Windows / Linux; release v1.0.0 with both suites; cask faltung-deep.)
22. "i dont hear anything on the oitput of deep kt1, check the audio on all of these" (kt1 in both suites was silent as AU and VST2: those hosts keep every output bus on, so all eight voices went to outputs 2–9. Voices now stay on the main pair outside VST3 unless *separate outputs* is on. New `faltungHostCheck` loads the built VST3 / AU files like a DAW and checks every plug-in's output.)
23. "make cloudfin in the aesthetic of faltung and add it to that suite." + "delete the old cloudfin from here and github" (Cloudfin moved in with its history at cloudfin/, new window in the faltung look, CI + packaging + cask; old repo deleted. Its own log: cloudfin/REQUESTS.md.)
24. "for the faultung deep sampler i want to be able to have pitch per step, and change the pattern length, and have the curve feature aera has, in addition to the rathcet feature." (kt2 loop in both suites, 1.1.0: pitch per step as parameters `step 1 pitch` … `step 16 pitch` (cmd-drag or scroll on a step); the pattern length is set by clicking a step number (the `length` parameter); AERIA's two curves: the ratchet's curve per step (alt-drag / alt-scroll, out 7 … even … in 7) and the clock curve over the loop (`clock curve` accel / decel / arch / bulge / wave / rand, `curve amount`); ratchets unchanged (right-click 1–4). Preset "pitched roll". Manuals, tests.)
25. "in ms0, the compare/bypass doesnt work" (1.1.0: the compare switch read the dry delay line at one fixed index for every sample of a block, so it put out a held sample instead of the input; it now reads the input delayed by the reported latency. The level match is measured on the processed signal itself, so it holds while compare is on. Test in faltungTest.)
26. "also in the microsound effect, have it aslo hava a freerunning knob as a switch from the tempo divisions list." (mc3: "free" is the last entry of *rate*; the new *free rate* knob, 0.5–200 Hz, runs the grid on its own clock while it is chosen and is dimmed otherwise. Both suites; test.)

27. "make convosolution and all obliqsound apps in the same aesthetic as faltung and add them to the faltung bundle and remove them elsewhere." (ConvoSolution moved in with its history at convosolution/, ObliqSound and ObliqSonic at obliqsound/ as one commit; new windows in the faltung look (slate-blue field with the response as a thin wireframe and a chip matrix; sage / deep-green fields with skewed sliders, folded-strip pages, a chip matrix and a wireframe sphere); CMake, CI, packaging and manuals carry them; the old casks and repositories go with the next release. Their own logs follow.)
28. "also i need the peak limiter on ms0 to have a vu meter, and i need it to be easier to be more precise with the dynamic eq, right now its hard to visualize the q for it, and hard to select the frequwency for it with just the slider." (ms0. Under the limiter, a meter: in and out, left and right, as VU bars with the 300 ms needle (measured 301 ms, 1.25% overshoot; a sine reads its own level; 0 vu = -18 dBFS or a reference you choose), true peak with a 2 s hold, the ceiling as a line, gain reduction with hold. The dynamic eq is drawn in the analyser as the bell each band really has — range dashed, action now filled, a bar on the -3 dB edges for the width, the edges in hertz — on points you drag (frequency / range; shift = fine; alt-drag or wheel = q; cmd-drag = threshold) with a read-out to a tenth of a hertz, the note, q in octaves and the edges; the same numbers stay under the sliders with a bar for what the band hears against its threshold. Every slider value can be typed by double-clicking it: `1.2k`, `850`, `2k5`, `a4`. No parameter changed. Found in REAPER on the way and fixed: the VST2 versions came back at their preset when a session was reopened; automated eq / dynamic-eq frequencies lagged in offline bounces. Tests, manual.)
29. "make obliqsound and obliqsonic in the style and aesthetic of faltung and add them to the faltung suite." + "Also, make convolution in the aesthetic of faltung and add it to that suite as well." (Checked control by control against the originals: what the new look had dropped came back — in convosolution the display's message, clip, recording and loading lines on the modulation page, the centre mark of the width bar, the a / b mark, the list arrow, the empty-cell mark, the attack line and the full control names, and matrix cells that take the click where they are drawn; in obliqsound all four waveform pictures, the Ge'ez numerals from the instrument's own art on every system, the display reading "obliqsound : n", toggles that no longer run into each other, small pin amounts that show. Plug-in ids, parameters and state are the originals', so old sessions load. The engine's helper thread no longer sits at the lowest priority on macOS, where a busy machine could leave the instrument silent.)
30. "Change the aesthetic of faltung deep to just be the dark mode of the faltung plugins/apps." — "Change Faltung Deep's visual identity so that it functions as the dedicated dark mode of the Faltung plugins and applications." "Preserve the existing Faltung interface and design language. This should be a dark-mode variation, not an entirely separate aesthetic." (Every faltung deep plug-in is drawn with the layout, type and shapes of its faltung twin on the dark counterpart of the twin's colour field; the mirror tree and the sleeve layout are gone; sounds, parameters, presets, ids and saved sessions are unchanged. Before and after: mockup/dark/index.html. Both manuals rebuilt, the deep one on dark pages.)
31. "make sure all vsts work well. some vsts mono the sound as opposed to the vst3 that works well, just double check." (The VST2 and VST3 builds are loaded in a host with a stereo signal whose two sides have nothing in common, and their renders compared; ConvoSolution, ObliqSound and ObliqSonic come out the same in both — stereo stays stereo, ObliqSonic carries all sixteen channels. The same check on dl4, rv5, gl8, fl7, sw6, mc3, ms0, kt1, kt2, bd1, wb1, Cloudfin, Partials and the deep dl4, rv5, kt1, hh4 and sb1: VST2 and VST3 match, the stereo effects stay stereo, and the voices that are mono by design are mono in both. One thing did differ: reopened as a VST2, ms0 and the plug-ins that open on their own defaults came back with their first preset over the session (REAPER selects the saved program again after, or instead of, restoring the chunk); a host re-selecting the current program now changes nothing, while picking a preset in the window always loads it.)

29. "Make a copy of pinkie pies balloon shop and pinkie pies balloon palace that are also in the faltung style and aestrhic, with all of the same features, and also add that to the faltung suite. … To be clear, I don’t want you to delete pinkie pies balloon shop or palace, I just want a copy of them in the faltung suite that fits that aesthetic and visual."
30. "i want to keep pinkie pies balloon shop and palace but i want vst and standalong versions of balloon shop and palace that have dutch names and are in the aestetic of the faltung series." (**ballonwinkel** and **ballonpaleis**, in ballon/: the faltung-series twins of Pinkie Pie's Balloon Shop and Pinkie Pie's Balloon Palace, which stay as they are, in their own repository and with their own installs. The same engine and the same controls (109 and 118 parameters, the same ids, ranges, defaults, programs, layouts, latency and state), played as balloons, drawn the faltung way: a rose field and a deep plum field, faceted wire balloons that fill with colour to their value, folded strips, parallelogram tokens and switches, a sliders page, wire solids instead of the ponies. Their own names, codes, bundle ids and state, so both generations load side by side. `ballonwinkelTest` / `ballonpaleisTest` run the original's processor and the twin's on the same script and input: the same samples. VST3, AU, VST2 and app; dark twins in faltung deep.)

## 2026-11-15 — sndstudio
31. "can you make a whole DAW out of the faltung/deep suite that i can use to produce full length microhouse tracks. make it have everything i need to arrange and compose full microhouse tracks. i need modulation, effects, sequencer from the kit is great but even more in depth. make it in the faltung aesthetic, have it be able to multitrack so i can send it over to ableton over up to 128ch so i can record the different tracks and effects seperarly and mix and master in ableton. make the daw in depth all i would need for microhouse/rominimal. i love the kit vsts you made but its hard to preform with them live, and hard to arrange with them becuase i cant do effects sends per drum or automation for the sends, or automation for the parameters easliy. have the daw also let me use other vsts." (sndstudio, in both suites: Source/daw.)
32. "how is the daw coming along? make it really in depth and test it out completely to make sure all features work"
33. "make the daw super hifi, i want to be able to make and arrange tracks like villalobos, arpiar, raresh etc, with that quality, and that mixing and mastering quality. use all the exitgitng faltung fx, synths and engines and mastering stuff, and add new ones you think would be useuful,"
34. "i aslo want to be able to make music like basic channel, autechre, grischa litchenberger, second woman, maurizio, dubtil, mark fell, ryoji ikeda, noisia, current value. shigeto, four tet, prefuse 73, lapalix, and todd edwards, do your research and add features that will allow me to do this." (docs/daw-research.md)
35. "i want to be able to work on the grid and off the grid , so anything that has delay times or tempo syned parameter also needs to be able to have just free timings in ms modes. i also love mapping lfos and random lfo shapes to parameters, and being able to send slewed random and random on off gates to parameters, so make it easy if i want to change a knob slider or parameter, for me to map an lfo with shape and rate control to that parameter. also make it easy to atutomate parameters on a timeline."
36. "in addition to all the engines, i also want to be able to drag in samples and place them on the timleine, and effect them and automate them as well."
37. "i want there to be a mode where i can usr the daw stadnlone to produce entire tracks, but also i can have it have multiple outputs so i can map it to another daw if i like." (produce / multi-out modes.)
38. "i want to be able to use the daw for spatial audio too. so it needs to have the features that allow me to change how many channels the master out is, up to 64 channels. the tracks themselves, i need to be able to change how many channels there are. and i need to be able to say, dont sent this channel to the master out, so that i can route channels to other channels with the option for how many channels i want to route. the workflow for spatial audio and ambusonics should be this: i make an new channel that defaults stereo, i change it to be a 16 ch track, i tell it to not send audio to the master. i make another channel that eill be my decoder channel. i set that channel to 24ch, becuase it will go out to a 24 channel multichannel array. i drag the route icon from the 16ch track to the 24 ch track and a popup shows me how many channels i want to route, i say 16 becuse the multichannel encoder plugins send audio over 16 channels to the decoder. i make sure the decoder channel is sending to the master. i ipen the master channels and i make sure the number of output channrls is set to 24 and the vu meters show 24 meters on the output mastee track. now my encoder is routed to my decoder over 16 ch, and my decoder to the master over 24 ch. i place a third channel and set it to 16 channels, i route my encoder track also to this channel over 16 channels and place a binaural decoder on that channel. even tho that channel is 16 channels in, the binaural deocder only places 2ch out, so when its routed to the master, and im not in the spatial audio or ambisonics studio, i can set my master out back to 2 channels, mute my 24ch decoder, and work on the arrangement in stereo bianural." (track widths 1–64, routes between tracks, master width; manual chapter "spatial audio and ambisonics".)
39. "when i make a stero track 16 channels, and there is a stero file on the track, i want the stereo file to be the imput for the encoder that will be places on that track, so stero in, and 16 channel out (if i want)" (a track's width is its chain / output width; devices negotiate 2 → 16 → 2 along the chain.)
40. "in the daw i also need to be able to select interfaces and inputs/outputs and record audio directly into the daw." (audio settings page, per-track inputs, arm, monitoring, recording to clips.)
41. "call the daw sndstudio"
42. "i also need an option for resmapling the project into an audio track in the daw." (file → *render the project into a new audio track* (the loop when it is on; also before the master chain), the master strip's *bounce*, and per track *bounce* into a new audio track or into a sampler, sliced.)
43. "test it all out make sure it works" (`sndstudioTest` / `sndstudioDeepTest`: the engine feature by feature, and the pages driven the way a hand would: the route icon dragged from the 16-channel strip onto the 24-channel one, the channel popup, the master at 24 with 24 meters, the binaural track, files dropped on the timeline, lanes clicked and drawn, bounce, capture, auto master.)
44. "make a pdf manual for sndstudio too when youre donw with it" (docs/manual/sndstudio Manual.pdf, in every download.)

## 2026-11-19 — sndstudio, continued
45. "Continue on sndstudio project" + "contonue working on sndstudio with all of my requests for that." (what was open: every timed control on the grid or off it — the lfo of fl7, the wobble and the steps of wb1 / sb1 get *free* as the last entry of their rate, like mc3, and step modulators run at a free rate too; one gesture from any control to an lfo, a random, a slewed random or a random gate, with its shape and rate in the popup; the route icon is dragged from strip to strip and a popup asks how many channels; routes into a track decide what its first device reads, so a decoder plug-in takes the 16 routed channels and a binaural one gives 2; the studio's own encoder, decoder and binaural devices; wide files play every channel; the browser auditions files; keys play controls; ms0 *auto master* in one click from the master strip; the starter opens the same to the last value in every new instance and the host's parameters hold the project's values at once; a modulated parameter no longer comes back from a saved project with the modulated value as its base. Checked in a scripted host as well: the VST3 and the VST2 loaded, rendered, panned hard through host parameters, switched to multi-out through the state, automated, saved and reopened.)
46. "for sndstudio, remeber all of the musicians i have mentioned and implement their ideas and workflows into the daw." (docs/daw-research.md now covers everyone named across the projects, method by method; new for them: a time per step with the tools *ratios*, *accelerate*, *even*, *strict*; *sieve*, *sieve pitch* and *cloud*; *evolve* and *freeze*; choke groups across tracks; chords on a step; a curve on the ratchet; the modulator source *bounce*; feedback routes; the devices *skip*, *wear*, *line*, a second tone and tunings in *tone*; freehand arcs in the lanes; *capture*; nudge; the templates *bass first*, *two-step*, *half-step*, *spatial*; the kit and the loop as track instruments; Partials, Cloudfin and the other plug-ins of the house offered without a scan. The manual's last chapter says, artist by artist, how to work that way. No artist names in device, preset or template names.)

## convosolution (from femifleming/convosolution)


### 2026-11-08

> ~/Documents/Analysis\ and\ recreation\ of\ key\ features\ in\ selected\ Autechre\ tracks\ from\ 1998-2005.pdf read this pdf and add anything you think might be cool to this vst.


### 2026-11-09

> looks like toy took the filter effecr deom the octatrack, i dont need that one

> recreate the animation, this is just a fan project, its open source fun, https://github.com/marsboy02/bad-apple https://github.com/dt-7148/Bad-Apple many people have done it

> or play it off the card, make the video player, convery the video and download it


### 2026-11-10

> i want boid and lorenz curve modulation for every parameter, make the ui way more compact like a eurorack module would be. give me real looking knobs and switches. its also not too symettrical looking i want it mirror symetrrical, you can place thigns wherever you need them to go for it to perfecr mirror, aksi maekt the background an aluminum panel

> dont make it aluminum, change it back to the dark grey that it was

> install it

> i want the mirror to be more of an organic layout, not so grid and metal panel/old synth looking.

> https://www.tastychips.nl/product/ecrplus/ https://www.tastychips.nl/ecr-support/ make this eurorack module into a high quality vst vst3 and au effect, make sure it works on windows mac and linux. download the inpulse respinses from open source online databases.


### 2026-11-12

> Your claude.ai usage limit has reset. Continue the task you were working on when the limit was reached; do not repeat work that is already complete.

> make it look just like the ui/aesthetic of the eurorack module, but if youre gonna make a panel dont put mounting holes on it, priorize the screen too, i need it maybe bigger in the vst version to see what im doing

> vst and vst3

> this only needs to work on mac.

> ~/Music/Ableton/Factory\ Packs/Convolution\ Reverb/IRs add these

> jsut make the plugin really small and in the style of the screen resolution, and remove the eith inch jakcs because i cant patch them so they shouldnt be there.

> make it look windows 2000 grpahics and pixel quality style.

> add lfo params with various shapes like random lorenz and boid etc to the vst with a matrix of where i can map them to

> actually make it look like an old 90s html page instead

> chanhe the pixel display to also be html style

> call it ConvoSolution add visualizers for the boids and lorenz somewhere in the vst. then make it work on windoews and linux, bundle it and push it to my github.

> ~/Documents/Analysis\ and\ recreation\ of\ key\ features\ in\ selected\ Autechre\ tracks\ from\ 1998-2005.pdf i also like autechre and mark fell, gabor lazar, kindohm and second woman music, are there any effects, midi instumeunts, or sequencing things you migth add to make that kind of music? read the dis i have attached.

> ~/Documents/Analysis\ and\ recreation\ of\ key\ features\ in\ selected\ Autechre\ tracks\ from\ 1998-2005.pdf i also like autechre and mark fell, gabor lazar, kindohm and second woman music, are there any effects, midi instumeunts, or sequencing things you migth add to make that kind of music? read the dis i have attached. also in the midi sequencer/sampler mode make it so that i can multitrack the 8 tracks over usb audio so i can grab stems. it would be 8 stero tracks, this is heavy on cpu, i know from modwerk but see if we can make it work. for the sequencer mode and sequencer mode only, have the 16 step lignts be bright and steppy instead of smooth, they can be smooth in the other mode, but here i need to see where thigns are in the seqeuence, so not smooth lights as it steps thru.


1. "for every single app and vst on my github, just make the app logo icon extremely minimal, a letter from this font [Septambres Neau] in some color, on some color background, update all the app icons." (New app icons: one Septambres Neau glyph, one colour on one colour.)

## obliqsound / obliqsonic (from femifleming/obliqsound)


### 2026-11-10

> [Image #2] call it obliqesound, use this logo and color[Image #3] make it this aesthetic but with the color i just gave you instead of orange.

> check the gainstagin for stuff too, the curve on the feilrd recorgs kbob makes it able to turn up the volume way too much too fast.

> make vst and vst3

> go back to it looking organic layout and mirrored, use that logo and color, and use [Image #5] this and [Image #6] this aesthetic of old 90s render plugins, use old style renderings and that pixel quality of windows 2000. but make it mirror synetrical and have an organic layout.

> it looks beautiful. this is perfect, but add more and more layers of green around the main organic shape, almost like its topagraphical. make the outer layers darker and darker. when i switch modes, have the shape morph into the next shape liee the topogrpahy is moving. this resolution and pixel qualty is perfect.

> i want the background to also to always be kind of morphing slowly randomly. also make it a desktop app. the motion and lfo mapping tab i want to be a a huge big pin matrix like the ems synthi.

> randomize also needs to choose a random feild recording

> for the field recordings, the start and end point are contiinuously always slowly slew-ly changing. slowly at the same speed of the background. from what you opened, it seems the frame rate is super low too. make it high 30fps

> make a multichannel iem/sparta encoder instruemnt deviated flocking boid lorenz gravity moved version of this vst called obliqsonic, look at what youve done for mellyloon plugin and the other spatial plugin, the only difference is this is a sound source that receives midi. in the midi pin matrix, also let me map incoming midi cc to things.

> but only do that when youre done with this

> i liked where the different page buttons were before, move them from the bottom.

> in the spatial audio version, when youre ready for it, the feild reocrdings are copied and moving around, each one with its own random modulators for start and end point.

> replace all the names of the field recordings with etheopian numerals.

> make a comprehensive manual pdf for both vsts when you are done

> the morphoing of the background seems to just crossfade between images, it was really dynamically moving and morphoing before, go back to it looking like that.

> i need to close my mac and migrate, can i do that? i will
>   be back

> mak obliqesound and obliqsonic both desktop apps as well and push that. are they?

> open the pdf for me also

> install the latest version on my computer and delete the old verson, on my verion on my computer the displays seem broken, cna you check that?

> when youte done push the new version

> delete all that are not the latest ones and move the latest one to my applications folder

> Its called Obliqsound and Obliqsonic not kia instrument

> rename it all to ObliqSound and
>   ObliqSonic

> i jsut wanted to change the name of the vst so i can find it under ObliqSound and
>   ObliqSonic it doesnt need to be complex

> vst and the app*

> make vst and vst 3 for both

> update the github and remove all old versions from the github and from my computer

> the audio seems to be seriously distorting and low quality, and some of the feield recordings are distorted, i think the original oners from the kia app. the program is super laggy and the gainstaging weird, its clipping on the output and doesnt sound polyphonic up to 16 voices.

> all '!' command you have permission to go
>   ahead and run terminal commands youself

> samples should be 16 bit wav instead of 24 bit

> install it on my mac and remove all old versions, when youre finished, open it

> check obliqesonic as well. make both synth engines for both apps 16 bit. the app is still lagging and glitching hevily on my end when i send it a lot of midi notes.


### 2026-11-12

> continue

> dont use csound use a higher quality sound engine, something that makes sense.
>   try using supercollider for the base.

> i dont think its receiving midi

> midi works. i dont see delay time and feedback anywhere, and wheres the adsr for the synth?

> remove the sampler part entirely from both plugins but keep the aesthetic and design as similar as possible.

> make sure both plugins are supercollider based on the synthesis side

> nevermind, dont remove the sampler part.

> keep the sampler part, push both of these to github. also update github woth a log of everything ive asked for breaked down in a pdf as a log. make sure you update the manual as well. also add a help page to the vst's and standalone desktop apps before you push to github.

> push it all to github when youre done

> make sure latest build of vsts and standalones work on mac windows and linux

> hows the windows testing going

> doesnrt need to be exactly like that but take insperation from other 90s early 2000s vsts and give me a few more options.

> give me other variations inspired by the obliqsound one but different

> what if it was in the style of these gifs, look thru all these pages: https://gifcities.org/search?q=music&offset=0&page_size=200 https://gifcities.org/search?q=computer&offset=0&page_size=200

> i also really like the look of dithered images


1. "for every single app and vst on my github, just make the app logo icon extremely minimal, a letter from this font [Septambres Neau] in some color, on some color background, update all the app icons." (New app icons: one Septambres Neau glyph, one colour on one colour.)

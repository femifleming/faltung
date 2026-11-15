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

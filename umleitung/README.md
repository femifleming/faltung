# umleitung

A Mac app for routing, processing and recording sound, in the faltung look: virtual devices other apps can use
(like Loopback), sessions that capture apps or the whole Mac and run them through effects, your VST3 / Audio Unit
plug-ins and a recorder (like Audio Hijack), a system-wide eq (like eqMac), and meters for everything.
macOS 14.2 or later.

- **sessions**: blocks wired on a canvas. Sources: an app (by bundle id, muted at its source if you like), the
  whole system, an input device (any channels), a test tone, a file. Effects: volume, a 10-band graphic eq, a
  parametric eq, plug-ins, a channel matrix (any input channel to any output channel), delay, leveler, gate,
  limiter. Outputs: any device (or a virtual device), the recorder (wav / aiff / flac, 16 / 24 / 32-bit float, any
  number of channels, file-name patterns, new files every n minutes, a maximum length, pause on silence), meters.
  Sessions run on a timer (start at a time, stop after minutes). All sessions run on one clock: one private
  aggregate device of everything they use, with drift compensation.
- **system eq**: everything the Mac plays, captured, the original muted, through volume (with boost), balance, a
  basic / graphic / parametric eq and a limiter, to the Mac's output (following it). Presets, your own presets, an
  eq remembered per output device.
- **virtual devices**: umleitung's driver publishes devices with any name and 1–256 channels; each can have its
  own routing session.
- **meters**: a meter bridge (any number of channels), needle vu meters, loudness (EBU R128), spectrum,
  spectrogram, stereo field with correlation, scope, for any block.
- **recordings**, **plug-ins** (scanned in a child process), **settings**, and a menu-bar item.

## Building

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release        # JUCE from ../faltung/JUCE (or -DUMLEITUNG_JUCE=...)
cmake --build build --target umleitung
```

## Checks

- `umleitung_engine_test`: the engine offline (routing, channels, the eqs, dynamics, loudness, recorder, plug-ins)
- `umleitung_live_check`: the engine on the Mac's real CoreAudio, through BlackHole (silent)
- `umleitung_host_check`: devices, aggregates, rates, taps (needs BlackHole)
- `driver/umleitung_driver_test`: the driver, loaded in-process with a fake host (no install)
- `umleitung.app/Contents/MacOS/umleitung --screenshots <dir> [--demo | --demo-live]`: a picture of every page

Source: `Source/engine` (model, dsp, engine, plug-in host), `Source/platform` (CoreAudio, the driver's control),
`Source/ui`, `driver/` (the AudioServerPlugIn). The look comes from the faltung kit (`third_party/faltung_ui`).

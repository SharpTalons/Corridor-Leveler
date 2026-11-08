# Corridor Leveler

An automatic level rider for streaming and recording. Set a target and Corridor keeps your audio
there: quiet passages are lifted, loud ones lowered, silence and room noise are never boosted, and
the output never goes past the ceiling. Built for OBS on Windows, and works in any DAW.

![Corridor Leveler window](docs/window-dark.png)

## Formats

| Build | File | Where it's used |
|---|---|---|
| Native OBS filter | `corridor-leveler.dll` | OBS → Filters → **Corridor Leveler** (settings in OBS's filter panel) |
| VST3 | `Corridor Leveler.vst3` | DAWs; OBS through a VST3 host add-on |
| 2.x plugin | `Corridor Leveler.dll` | OBS's built-in **VST 2.x Plug-in** filter (full plugin window), older DAWs |
| Standalone | `Corridor Leveler.exe` | Try it live with your microphone |

All four run the same engine (`core/`). Mono and stereo, at whatever sample rate the host uses.

## Controls

- **Ride / Hold**: Ride adjusts the gain continuously. Hold measures a typical section when you
  press **Analyze**, then locks one fixed correction (press again to **Lock**, or it locks after 20 s).
- **Threshold** (dB RMS): below this it stops adjusting, so silence, breaths and noise never trigger a boost.
- **Target** (dB RMS): the level you want.
- **Response**: slow (1.5 s averaging, smooth) to fast (0.3 s). Drums and other percussive material are
  detected automatically and averaged over at least 0.8 s, so the gain follows the groove, not each hit.
  Within 0.75 dB of the target it holds still.
- **Range** (dB): the most it may boost or cut.
- **Ceiling** (dBFS): the highest peak allowed. Boosts stop short of it and a 5 ms look-ahead
  brick-wall limiter catches the rest.
- **Ease in silence**: off, the gain freezes below the threshold; on, after 2 s of silence it glides back to 0 dB.
- **Bypass**: click-free, and keeps the same latency so nothing jumps.

The window can be resized freely (it keeps a 16:9 shape), has light and dark themes, input peak meter on
the left, output on the right, and shows the latency in the bottom-right corner (5 ms at every sample rate).

## Installing

Download the latest **CorridorLeveler-…-Windows-Setup.exe** from [Releases](../../releases/latest), run it,
and restart OBS. See `installer/README.txt` for where each part goes.

## Versions and releases

Every commit that lands on `main` builds the installer and publishes it as a new GitHub Release.
The version is `MAJOR.MINOR.N`: `MAJOR.MINOR` comes from the `VERSION` file and `N` is the number of
commits on `main`, so it goes up by itself. To start a new minor or major version, edit `VERSION`
(for example `0.2` or `1.0`). The same number is built into the plugins, the installer and the release tag.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --parallel
```

```bash
ctest --test-dir build --output-on-failure
```

JUCE, FST and the libobs headers are downloaded at configure time. On Linux you also need the usual JUCE
libraries (ALSA, X11, FreeType, Fontconfig). Options: `CORRIDOR_BUILD_PLUGIN`, `CORRIDOR_BUILD_OBS`,
`CORRIDOR_BUILD_VST2`, `CORRIDOR_BUILD_TESTS`.

Tests:

- `tests/test_core.cpp`: engine behaviour at 44.1 to 192 kHz: reaching the target, stereo linking, threshold
  freeze, ease, range, ceiling, bit-exact bypass delay, Hold/Analyze, drums vs voice, mono.
- `tests/obs_harness.cpp`: loads the OBS filter with a stand-in libobs and pushes audio through it.
- `tests/plugin_check.cpp`: loads the built VST3 and 2.x binaries like a host, checks level, latency and
  bypass, and renders the window to PNG.

The `prototype/` folder holds the original browser prototype.

## Licences

Corridor Leveler is licensed under the **GNU AGPL v3 or later** (see `LICENSE`).

- [JUCE](https://juce.com) is used under its AGPLv3 open-source licence.
- The 2.x plugin is built with [FST](https://git.iem.at/zmoelnig/FST) (GPLv3 or later), an independent
  reimplementation of the 2.x plugin headers, because Steinberg no longer distributes that SDK.
  "VST" is a Steinberg trademark. FST's authors ask that plugins built with FST not be called "VST"
  or "VST compatible", so this project refers to that build as the 2.x plugin.
- The OBS filter compiles against the libobs headers from OBS Studio (GPLv2 or later).

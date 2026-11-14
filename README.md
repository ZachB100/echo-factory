# Echo Factory

A performance delay by ZBAudio, built first for screen-reader users. AU, VST3 and Standalone for macOS.

Every control has a spoken title, description and value with units, and is
reachable by keyboard. The Perform pad turns the computer keyboard (or a MIDI
pad controller) into an instrument for playing the delay live.

## Features

- Delay time in ms or tempo-synced divisions, with adjustable glide
- Feedback, ping-pong, stereo width, Low Cut and High Cut (in the loop or on the output)
- **Wear** in five **Styles**: Studio Tape, Tape Echo, Bucket Brigade, Lo-Fi Digital and Telephone
- **Diffusion**: a 16-line feedback delay network reverb that takes over the repeats
- **Shimmer**: the reverb fed back through a pitch shifter, from an octave down to two octaves up
- **Modulation**: chorus, flanger or phaser, in the feedback loop or on the output
- **Multi-Tap**: up to 16 taps, each with its own time, level, pan, pitch and Reverse
- **Groove** and **Feel**: swing for synced taps, and synced echoes ahead of or behind the beat
- **Gestures**: Throw, Freeze, Tapestop, Runaway and Reverse, held or latched
- Factory presets, each with a spoken hint for which gesture to try

See [docs/perform-map.md](docs/perform-map.md) for the Perform-mode keys and the MIDI map.

## Install

Download the zip from [Releases](../../releases) and copy:

- `Echo Factory.component` to `~/Library/Audio/Plug-Ins/Components`
- `Echo Factory.vst3` to `~/Library/Audio/Plug-Ins/VST3`
- `Echo Factory.app` anywhere you like

The builds are ad-hoc signed, not notarized. macOS will quarantine them, so
clear the flag after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Echo Factory.component" ~/Library/Audio/Plug-Ins/VST3/"Echo Factory.vst3"
```

## Build

Requires CMake 3.22+ and the Xcode Command Line Tools.

```bash
git clone --recursive https://github.com/ZachB100/echo-factory.git
cd echo-factory
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Engine tests:

```bash
cmake --build build --target EngineTests && ./build/EngineTests_artefacts/Release/EngineTests
```

## License

Echo Factory is licensed under the [GNU AGPLv3](LICENSE). It uses
[JUCE](https://juce.com), under JUCE's AGPLv3 option.

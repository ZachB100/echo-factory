# Echo Factory

A performance delay by ZBAudio, built first for screen-reader users. VST3 and Standalone for Windows and macOS, plus AU for macOS.

Every control has a spoken title, description and value with units, and is
reachable by keyboard. The Perform pad turns the computer keyboard (or a MIDI
pad controller) into an instrument for playing the delay live.

## Features

- Delay time in ms or tempo-synced divisions, with adjustable glide
- Feedback, ping-pong, stereo width, Low Cut and High Cut (in the loop or on the output)
- **Wear**: tape saturation, darkening, wow and flutter
- **Diffusion**: a 16-line feedback delay network reverb that takes over the repeats
- **Multi-Tap**: up to 16 taps, each with its own time, level, pan, pitch and Reverse
- **Gestures**: Throw, Freeze, Tapestop, Runaway and Reverse, held or latched
- Factory presets, each with a spoken hint for which gesture to try

See [docs/perform-map.md](docs/perform-map.md) for the Perform-mode keys and the MIDI map.

## Install

### Windows (64-bit)

Extract the Windows zip. Run `Standalone/Echo Factory.exe` for the standalone
effect and choose your audio input/output in its audio settings. This processes
incoming audio; it is not a synthesizer or a system-wide audio effect.

For a DAW, copy the **entire** `VST3/Echo Factory.vst3` folder to
`C:\Program Files\Common Files\VST3` (administrator permission may be needed),
then rescan plugins in your 64-bit VST3 host and add Echo Factory as an audio
effect. AU is a macOS-only format. Windows builds are unsigned.
If Windows reports a missing `VCRUNTIME140` or `MSVCP140` DLL, install Microsoft's
Visual C++ v14 Redistributable for x64.

### macOS

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

### Windows (64-bit)

Install Git and Visual Studio 2022 or 2026 with **Desktop development with C++**,
including an MSVC toolset, Windows SDK, and C++ CMake tools. Open the **Developer
PowerShell for Visual Studio** so `cmake` is on PATH.

```powershell
git clone --recursive https://github.com/MichaelJohann1/echo-factory.git
cd echo-factory
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DEF_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --parallel 4
cmake --build build --config Release --target EngineTests --parallel 4
& .\build\EngineTests_artefacts\Release\EngineTests.exe
```

For Visual Studio 2022, use `-G "Visual Studio 17 2022"` instead. CMake 3.22+
works with VS 2022; VS 2026 requires CMake 4.1+ (the bundled version is suitable).
If you already cloned without `--recursive`, run
`git submodule update --init --recursive` before configuring.

The outputs are:

- `build/EchoFactory_artefacts/Release/Standalone/Echo Factory.exe`
- `build/EchoFactory_artefacts/Release/VST3/Echo Factory.vst3/`

Windows defaults to building without installing plugins into Program Files.
`EF_COPY_PLUGIN_AFTER_BUILD` can opt into automatic installation. The standalone
uses a fallback tempo of 120 BPM; a DAW supplies tempo to the VST3 plugin.
Keyboard entry and Tab/Shift+Tab navigation have been confirmed by an NVDA user
in the Windows standalone app and REAPER. Other hosts and screen readers have
not been verified.

The editor accepts keyboard focus from its host window and transfers it to the
preset menu, so entering the interface does not require a mouse click. In
REAPER with OSARA, use F6 from the FX window to enter the plugin, then Tab and
Shift+Tab to navigate. The standalone window also allows keyboard entry into
the editor. Its Windows launcher explicitly activates the window and focuses
the preset menu after startup, so Tab works without an initial mouse click.
This startup handoff applies only to the standalone app, not the VST3 plugin.
Previously the editor was a non-focusable keyboard focus container,
which prevented the outer window from reaching its controls.

Editor traversal regression tests (no audio device needed):

```powershell
cmake --build build --config Release --target EditorFocusTests --parallel 4
& .\build\EditorFocusTests_artefacts\Release\EditorFocusTests.exe
```

These check host-to-editor focus eligibility, the preset-menu entry point, and
forward/backward traversal. They do not replace an NVDA/REAPER interaction test.

### macOS

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

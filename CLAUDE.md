# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Echo Factory is a JUCE 9 delay effect by ZBAudio, built as AU, VST3 and Standalone. Its identity is set at the top of `CMakeLists.txt`: manufacturer code `Zbau`, plugin code `Ecfy`, bundle ID `com.zbaudio.echofactory`. Don't change the codes, because that breaks saved host sessions.

**Screen-reader accessibility is a hard requirement.** Every control needs:
- an accessible title (`setTitle`)
- a description (`setDescription`)
- spoken value text with units
- keyboard focus
- an `setExplicitFocusOrder` slot

## Build

This Mac has only the Command Line Tools, not Xcode, so use the Makefile generator. `-G Xcode` fails.

```bash
git submodule update --init          # JUCE lives in ./JUCE
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 10
```

- `COPY_PLUGIN_AFTER_BUILD` installs the AU and VST3 into `~/Library/Audio/Plug-Ins`. It's set by `EF_COPY_PLUGIN_AFTER_BUILD`, which is on by default on macOS. With it off, a post-build step in `CMakeLists.txt` signs the VST3 again after `moduleinfo.json` is written. Without that step the bundle's signature is broken and Live rejects it.
- The Standalone app is at `build/EchoFactory_artefacts/Debug/Standalone/Echo Factory.app`.
- The Standalone app has no host tempo, so tempo sync falls back to 120 BPM there.
- The Standalone app needs `MICROPHONE_PERMISSION_ENABLED`, because otherwise macOS silently gives it zero input. It's ad-hoc signed, so macOS may ask for mic access again after a rebuild.
- Validation is:
  - **Engine tests:** run `cmake --build build --target EngineTests && ./build/EngineTests_artefacts/Debug/EngineTests`. They're offline checks of `DelayEngine`, not part of the default build: clean-path exactness, Runaway staying bounded, Wear decaying, and Tapestop returning exactly with no clicks. Run them after any DSP change.
  - **Warnings:** the build should produce none from `Source/`. JUCE's recommended warning flags are on.
  - **AU:** run `killall -9 AudioComponentRegistrar; auval -v aufx Ecfy Zbau`.
  - **Accessibility:** a manual VoiceOver pass in the Standalone app. osascript has no assistive access here, so the accessibility tree can't be inspected automatically.
  - **Layout:** `cmake --build build --target EditorSnapshot && ./build/EditorSnapshot_artefacts/Debug/EditorSnapshot main.png taps.png` paints the editor to PNG files (plain, and with the Taps panel) without a host, a window or audio.

## Architecture

- **Parameters** (`Source/Parameters.*`): the single source of truth for parameter IDs, ranges, defaults and value↔text functions.
  - Parameters added after release carry a version hint (2 for Multi-Tap, 3 for Style, Shimmer, Modulation, Feel and Groove) and stay at the end of the layout. Choice parameters whose order the engine depends on (Style, Modulation type, Shimmer Pitch) are listed here in the engine's order.
  - The sync-division table has two names per entry: a short one for the screen and a spoken one for screen readers ("1/8D" vs "1/8 dotted").
  - `formatMilliseconds` and `formatFrequency` take a `spoken` flag and are shared by the parameter text and the UI.
  - Filter "Off" is the end of the range: Low Cut at its minimum and High Cut at its maximum. The processor maps those values to 0, which bypasses the filter in the engine.
- **DSP** (`DelayEngine`): plain C++ with no JUCE parameter knowledge. `EchoFactoryProcessor::updateEngineParameters()` pushes raw APVTS values into it once per block.
  - Every value is smoothed with `SmoothedValue`. The delay-time ramp length comes from the `timeSmoothingMs` parameter. `setDelaySmoothingMs` keeps the glide going from its current position, because `SmoothedValue::reset` jumps straight to the target.
  - The per-sample loop does pop → filter → push, with the write path depending on filter position (in feedback loop or output only).
  - Ping-pong (stereo only) writes the mono input to the left line, sends the left echo to the right line at unity, and sends the right echo back to the left line scaled by feedback. So feedback 0 still gives two repeats, first left then right. Mode switches crossfade via `pingPongAmount`. While frozen in ping-pong, the two lines swap each pass so the audio keeps bouncing.
  - Stereo width is a separate short delay line on the right echo, applied on the output only, never in the feedback loop.
  - Freeze crossfades the write path to the buffer's own unfiltered output, and rounds the delay to whole samples while frozen.
  - Filters always run, even when bypassed, so switching them on doesn't click.
  - Wear and Runaway both go through `DelayEngine::tape()` on the write path (the frozen path skips it). `tape()` applies a DC blocker, a one-pole low cut, a `tanh` saturator with a ceiling, Lo-Fi sample-and-hold with fewer bits, and one-pole darkening. Each is crossfaded by an amount, so Wear 0 is bit-exact clean. Wear also adds wow, flutter and random drift to the delay time, which fade out while frozen.
    - The Style parameter picks what Wear does from the `styles` table in DelayEngine.cpp (ceiling, darkening cutoff, low cut, wow and flutter depths and rates, Lo-Fi). Studio Tape is the original character. The style's values are `SmoothedValue`s, so switching styles doesn't click; only the wow and flutter rates jump, and their phases carry on.
    - Lo-Fi rounds towards zero, so a repeat below one step dies instead of looping at that step forever.
  - Modulation (`Source/Modulation.*`, `Modulator`) is a chorus, flanger or phaser mixed half and half with the dry signal, so its gain never passes 1 and it's safe in the loop. There are two instances: one on the write path after `tape()` (skipped while frozen, like Wear), one on the heard echo after Diffusion. The position parameter crossfades between them. Each type has its own crossfade; `advance()` returns false when every crossfade is at 0, so Off is bit-exact and costs nothing. A modulator that has been off clears its line and allpasses when it starts again.
  - Tapestop, Reverse and Pitch live in `ReadHead` (`Source/ReadHead.*`), a read head with its own tape transport. The main output head is one `ReadHead`, and each multi-tap tap has its own. Pitch uses two heads sweeping a 50 ms window, crossfaded in, so 0 semitones is bit-exact. While reversed, pitch is done by speed instead: the reverse heads read back (1 + ratio) samples per sample. The window shifter on top of reverse would cancel it out, because at +12 the heads would stand still.
  - Tapestop runs on a second, non-advancing read head, which reads before the loop's `popSample`. It feeds the output only, through a separate copy of the filters (`outputFilter`). So the feedback loop and a frozen loop are never slowed.
    - When the stop completes, the output is silent. The head then jumps ahead by exactly what the spin-up will add, so it lands back on the loop. If you release early, a 50 ms splice brings it back instead.
    - When idle, the output head reads the same samples as the loop, so the output is unchanged.
  - Reverse is on the same output head. Two heads read backwards over segments one delay time long, half a segment apart, and their sin² windows sum to 1. Reads reach up to three delay times back, or four for a tap an octave up, so the delay line holds 4 × 5 s plus the tapestop and pitch headroom. The heads advance at tape speed, so Tapestop slows them too.
  - Diffusion is a reverb (`DiffusionNetwork`: a Jot feedback delay network with 16 lines, Hadamard mixing, 4 pre-diffusion allpasses, slow line modulation, and make-up gain to bring the level back to input level). It processes the heard echo after the loop, blended in equal power by angle Diffusion × 90°. It's never inside the loop, so it can have gain.
    - As Diffusion goes up, `handover` = Diffusion² scales down the delay's own feedback (Runaway is exempt). The reverb's RT60 moves from a 0.3 s room towards the decay the feedback would have given, 3·delay / −log10(fb), clamped to 2–20 s.
    - So at full Diffusion the delay feeds the reverb once, Feedback sets the reverb length, and no separate repeats remain. `makeupTrim` is measured by the "comes out at about input level" test, so re-measure it if the network changes.
    - Shimmer feeds the network's previous output back into its input through a `PitchShifter` (`Source/PitchShifter.h`, the same two-head window scheme as `ReadHead`'s pitch, on its own line). The feed is trimmed below 150 Hz and above 8 kHz so repeated shifting can't pile up at either end, and soft-limited as a safety net. Its send is Shimmer × 0.6. `setDecay` takes the send and shortens the lines' own decay by send²/16 in energy, so the tail stays as long as Feedback says. Shimmer Pitch is a choice of intervals without 0 semitones, because an unshifted feed would be coherent with the lines and could build up.
  - Multi-tap: up to 16 taps (`DelayEngine::maxTaps`) replace the main echo, crossfaded by `multiTapAmount`. The loop keeps running at Delay Time with Feedback, so each tap repeats every delay time. Each tap is an output-only `ReadHead` on the same line. Each tap has its own time, level, pan, pitch and Reverse.
    - A tap reverses if its own switch is on or the global gesture is. The global Tapestop stops every tap. There's no per-tap Tapestop.
    - A tap reads the mono sum of the line.
    - The taps' sum goes through `tapFilter` (a third copy of the filters) and takes the main echo's place before Diffusion and Width. The main head and its filters keep running, so switching back doesn't click.
    - With Multi-Tap off, the taps aren't processed, so the output is bit-exact.
    - The 16 voices are preallocated. `setTaps` writes their settings as atomics, and the audio thread reads them once per block.
    - Each tap has a `uid`. A voice given a different uid fades out and back in, instead of gliding, which happens when a tap in the middle is removed.
    - While synced, taps convert their beats to ms on the audio thread (`syncedTapMs`), so offline renders follow tempo. Groove swings them: within each beat the first half stretches and the second half shrinks, so at full an off-beat eighth lands on the triplet. Feel adds its milliseconds to every synced tap, and the processor adds it to the synced main delay too.
  - Runaway ramps feedback to 110–160% while pushing the saturator fully in. The loop stays bounded because the linear part of the loop gain stays below 1 throughout the ramp. The saturator comes in twice as fast as the feedback, so pressing Runaway can't overshoot. `tests/EngineTests.cpp` checks this.
- **Performance** (`docs/perform-map.md` is the spec for keys and MIDI):
  - Each gesture (Throw, Freeze, Tapestop, Runaway, Reverse) is a bool parameter. Held versus latched only exists in the UI and MIDI layers.
  - Throw is applied through `DelayEngine::setInputSend`. The processor sends Throw Level while throwing, and otherwise unity, or 0 in Throw Only mode.
  - The Reset parameter can change on any thread, so it only sets a flag. The processor's timer then calls `resetPerformance()` on the message thread, which fires `onPerformanceReset` so the editor can announce it.
  - MIDI uses a fixed map (no MIDI learn). On the audio thread, `handleMidi` only records events. Held notes and the sustain pedal act immediately: the engine sees a gesture as on if its parameter is on or MIDI holds it. Latch notes, Reset and CCs set flags or pending values, and the timer applies them to parameters on the message thread.
  - The AU is pinned to `aufx` even though it accepts MIDI, so it doesn't break sessions. auval warns about this, which is expected.
  - Perform-mode arrow keys go through `nudgeFeedback` and `nudgeDelayTime`, which remember the value before the first nudge so Reset can restore it.
  - `ui/PerformPad` is the focusable keyboard surface. It tracks physical keys to ignore auto-repeat, and releases held gestures when it loses focus.
- **Taps** (`Source/Taps.*`): taps aren't parameters, because they're added and removed. There are at most 16 (`Taps::maxTaps`); `ensureTree` trims extras, and Add Tap announces the limit. They live in a `TAPS` child of the APVTS state, with one `TAP` child per tap.
  - `Taps` is their single source of truth for identifiers, ranges, defaults and text. Only `multiTap` is a parameter, at version hint 2.
  - `Taps::ensureTree` gives sessions and presets from before Multi-Tap one default tap. It's called in the processor constructor, in `setStateInformation` and in `PresetManager::applyState`.
  - The processor listens to the state tree, sets `tapsDirty`, and its timer calls `engine.setTaps`.
- **State and presets:**
  - All state is the APVTS `ValueTree`, and the current preset name is stored as a property on it (`presetName`).
  - `PresetManager` writes and reads that tree as XML `.echopreset` files in `~/Documents/ZBAudio/Echo Factory/Presets`.
  - On load, any parameter the file doesn't contain is reset to its default. This way new parameters don't inherit stale values from older presets.
  - Factory presets are built in code in the `PresetManager` constructor with `addFactoryPreset(name, hint, { {&id, value}, … })`. Values are in real units, snapped to each parameter's range, and anything left out is the default. The hint is announced after "Loaded preset <name>" and tells the player which gesture to try.
  - The editor follows preset-name changes, including state the host restores, with a `ValueTree::Listener` that triggers an `AsyncUpdater`.
- **Editor accessibility:**
  - `ui/AccessibleSlider.h` supplies a custom `AccessibilityHandler`:
    - The value is exposed as a 0–1 proportion, so VoiceOver increments match keyboard steps in skewed space.
    - `spokenTextFromValue` is read to screen readers, separately from the on-screen text box.
    - Keys: arrows, Shift for fine, Page Up/Down, Home/End.
    - Use `setNumKeyboardSteps(numChoices - 1)` for choice parameters.
  - The Delay Time knob is shared between two parameters. A `ParameterAttachment` on `sync` swaps its `SliderAttachment` between `delayTimeMs` and `syncDivision`. It also swaps the knob's title, text functions and step count.
    - Use the value passed to the callback. The APVTS raw value is still stale when that callback fires.
  - `SavePresetDialog` is an in-editor modal overlay, not a `DialogWindow`, because that's more reliable in hosts.
    - While it's open, the editor hides the main controls so screen readers can't reach them.
    - The close callback runs through `MessageManager::callAsync` because it deletes the dialog.
  - Keyboard focus is shown by `EchoFactoryEditor::paintOverChildren`, which is driven by a `FocusChangeListener`.
  - Controls sit in `ui/ControlGroup` containers: Delay (with a nested Modes group for the toggles, plus Feel and Groove), Character (Style, Wear, Diffusion, Shimmer, Shimmer Pitch, Width), Filters, Modulation and Gesture Settings. The preset bar and the Perform pad are outside the groups. Each group has the group role and is a focus container but not a keyboard focus container, so VoiceOver sees a nested tree while Tab still runs through every control. A group lays out its children in its own coordinates, and its bounds include the label strip, because attached labels live in the slider's parent.
  - `ui/TapsPanel` is shown by Multi-Tap, and the editor widens from 980 to 1540. It's a "Taps" group with one "Tap N" group per tap, then Add Tap, in a viewport.
    - Its focus number puts it straight after the Delay group.
    - Control titles carry the tap number ("Tap 2 Level").
    - Rows bind to the tree with `Value::referTo`.
    - The viewport and list use the `ignored` role and aren't focus containers. JUCE builds a container's accessibility children from its focus traverser, so making them containers would hide the rows.
    - `ui/TimeKnob.h` swaps any time knob between ms and divisions. Both the main knob and the tap knobs use it.
  - Focus order is set with explicit numbers in the editor constructor. The numbers are local to each group, and the group's own number places it among its siblings. Renumber within the group when inserting a control.
  - Status changes such as loading and saving presets are announced with `AccessibilityHandler::postAnnouncement`.

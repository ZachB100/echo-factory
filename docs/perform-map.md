# Perform mode and MIDI map

Echo Factory is a performance delay built first for screen-reader users. This
file is the reference for how it's played: the keyboard layout of Perform mode
and the fixed MIDI map. Change it here first, then in code.

## Principles

- **The sound is the feedback.** Held (momentary) gestures are never announced.
- **Latched states are announced once, briefly**: "Freeze latched", "Freeze released", "Reset".
- **Every gesture is a host parameter**, so it can also be automated and mapped in the host.

## Gestures

| Gesture   | While on | Setting |
|-----------|----------|---------|
| Throw     | Sends the input into the delay at Throw Level. With **Input: Throw Only**, nothing else reaches the delay. | Throw Level |
| Freeze    | Stops taking in new audio and loops the buffer forever. | Freeze Fade |
| Tapestop  | The echoes slow to a stop and fade out; on release they spin back up in half the time and land exactly back on the loop. Works while frozen; the frozen loop is untouched. | Tapestop Time (100 ms–2 s) |
| Runaway   | Feedback above 100% into the Wear saturator. | Runaway Drive |
| Reverse   | Echoes play backwards in overlapping segments one delay time long; release and they're back exactly where they were. Works while frozen. | — |

Reset releases every gesture and latch, including Freeze and MIDI-held notes,
and undoes feedback and delay time changes made with the Perform-mode arrow keys.
Loading a preset always turns every gesture off; host sessions restore as saved.

## Perform mode (computer keyboard)

The **Perform pad** is one focusable control, first after the preset bar. While it
has keyboard focus it captures the keys below. Keys are placed around **F**, the
key with the tactile bump, so they can be found by touch.

| Key | Action | Finger |
|-----|--------|--------|
| F | Throw | left index, on the bump |
| D | Freeze | left middle |
| S | Tapestop | left ring |
| A | Runaway | left pinky, hardest to hit by accident |
| G | Reverse | left index, reach right |
| Backspace | Reset | right hand |
| Escape | Leave Perform mode (focus moves to the controls) | |
| ↑ / ↓ | Feedback ±5% (Shift ±1%), silent | right hand |
| ← / → | Delay time one division (synced) or 1/100 of the range, silent | right hand |

- **Hold** a key: the gesture is on while held.
- **Shift + key**: toggles a latch (announced).
- **Plain key on a latched gesture**: releases it when the key comes up (announced).
- Losing focus releases every held (not latched) gesture, so nothing sticks on.

Deliberately unused: Space (Live's transport if the key isn't passed through),
Ctrl-Option (VoiceOver modifier), number keys (often taken by Live).

## Fixed MIDI map

Any channel. The VST3 and Standalone receive MIDI. The AU stays type `aufx` so
saved sessions keep loading, which means most AU hosts (Logic) won't send it MIDI.
In Ableton, use the VST3 for MIDI. Note numbers are MIDI numbers; Live names 36 "C1", some hosts "C2".
Laid out for 4×4 pad controllers starting at 36.

| Notes | Action |
|-------|--------|
| 36 Throw, 37 Freeze, 38 Tapestop, 39 Runaway | held while the note is down |
| 40 Reverse (held), 43 Reset | |
| 44 Throw, 45 Freeze, 46 Tapestop, 47 Runaway | each note-on toggles the latch |
| 48 Reverse | latch toggle |

| CC | Control | CC | Control |
|----|---------|----|---------|
| 20 | Delay Time | 31 | Tapestop Time |
| 21 | Feedback | 32 | Shimmer |
| 22 | Mix | 33 | Shimmer Pitch |
| 23 | Wear | 34 | Modulation Rate |
| 24 | Diffusion | 35 | Modulation Depth |
| 25 | Low Cut | 36 | Feel |
| 26 | High Cut | 37 | Groove |
| 27 | Stereo Width | 85 | Runaway Drive |
| 28 | Smoothing | 102 | Sync (≥ 64 on) |
| 29 | Throw Level | 103 | Ping-Pong |
| 30 | Freeze Fade | 104 | Input: Throw Only |
| 38 | Brandon Feedback | 105 | Filter Position |
| 39 | Brandon Orbit | 106 | Multi-Tap (≥ 64 on) |
| 40 | Brandon Crush | 107 | Modulation (type) |
| 41 | Brandon Pitch | 108 | Modulation Position |
| 42 | Brandon Chaos | 109 | Style |
| 64 | Sustain pedal: Freeze, held | 110 | Brandon (≥ 64 on) |

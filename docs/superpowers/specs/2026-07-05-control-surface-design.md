# On-Screen Control Surface — Design Spec

## Purpose

Build the keystone for Ableton Move parity (see
`2026-07-05-move-parity-checklist.md`): an **on-screen reproduction of Move's
control surface** plus a normalized input layer, so that the Move manual's
control vocabulary — *"press a pad, press a step button, hold a step and turn the
wheel, press +/−, hold Shift"* — refers to something that actually exists in
Motus, no matter whether the user drives it by touch, gamepad, or keyboard.

This spec covers **only** the surface, its rendering, and the input routing that
feeds it. It deliberately stops short of implementing step-sequencing *behavior*
(that is the next spec) — the deliverable here is that every Move control emits a
well-defined event and lights up correctly, with one thin end-to-end binding
(transport + a pad that makes sound) to prove the pipe.

## Why this is the keystone

Motus already has the *engines* for much of Move (transport, clips, mixer,
automation, quantize, VST instruments). What it lacks is Move's *operating model*.
The manual never says "open the piano roll and drag a note" — it says "press a pad,
press a step." Until those controls exist as named, addressable objects, none of
the manual's step-by-step instructions map, even for features we already have.

## Faithful control inventory (Move hardware, manual §1.2)

- **32 pads**, 4 rows × 8 columns, velocity-sensitive + polyphonic aftertouch
- **16 step buttons**, multifunctional (sequencing + secondary functions under Shift)
- **9 touch-sensitive encoders** + **1 Volume encoder** (top-right) = 10 rotaries
- **1 clickable, touch-sensitive wheel**
- **Left / Right arrow** buttons (bar navigation, nudge)
- **+ / −** buttons (octave / transpose)
- **20 primary buttons**: Play, Record, Capture, Undo, Copy, Delete, Mute,
  Sampling, Shift, Loop, 4 track buttons, and navigation/mode buttons
- **Note / Session** toggle; **Back/Settings** (Shift+Back → Setup)
- **1.3" 128×64 OLED** contextual display

Motus reproduces all of the above as on-screen widgets with the **same names and
behaviors**. The 8" Legion Go touchscreen is the natural home; gamepad and
keyboard drive the same widgets through the input layer below.

## Architecture

The core idea is a **normalized control-event bus**. Three input adapters (touch,
gamepad, keyboard) translate raw input into a single `MoveControlEvent` vocabulary.
The active mode consumes those events. This decouples *how you touch it* from
*what it does*, which is exactly what lets one manual describe three input methods.

```
 Touch (screen widgets) ─┐
 Gamepad (GamepadHandler)─┼─► MoveControlEvent ─► ControlSurface (state+LEDs)
 Keyboard (QuickKeys) ────┘        bus                 │
                                                       ▼
                                              active Mode handler
                                     (Note / Session / Sampling / Mixing)
                                                       │
                                     drives SequencerEngine / ClipSlot /
                                     PluginHost / MixerComponent / clips
```

### New files

```
src/
  ControlSurfaceComponent.h/.cpp   — the on-screen surface: pads, steps,
                                      encoders, wheel, buttons, OLED strip.
                                      Renders LED/state; emits touch events.
  MoveControlEvent.h               — the normalized event vocabulary (enum+struct)
  MoveControlBus.h/.cpp            — fan-in from the 3 input adapters; dispatch
                                      to the active Mode; owns Shift/modifier state
  MoveMode.h                       — interface every mode implements
                                      (onControlEvent, refreshLeds, oledText)
```

### Reused / modified

- `GamepadHandler` — becomes a **gamepad adapter**: its existing Navigate/Play/Edit
  logic is re-expressed as `MoveControlEvent`s (its dormant `onPlaceNote`,
  `onAdjustLength`, `onTransposeNote` callbacks route through the bus).
- `QuickKeysHandler` — becomes the **keyboard adapter** (App B shortcut set later).
- `TouchPianoComponent` — folded into the pad grid's Note-mode layout.
- `MainComponent` — hosts `ControlSurfaceComponent` + `MoveControlBus`; owns the
  active-mode pointer; keeps the existing DAW views available behind the surface.

## MoveControlEvent vocabulary

The whole point of the surface is that this list is complete and stable. Every
manual instruction decomposes into these.

```cpp
enum class MoveControl {
    Pad,          // index 0..31, with velocity + aftertouch
    Step,         // index 0..15
    Encoder,      // index 0..8  (+ Volume encoder = index 9)
    Wheel,        // relative delta; also WheelClick
    ArrowLeft, ArrowRight,
    Plus, Minus,
    Shift,        // modifier: down/up, not a one-shot
    Play, Record, Capture, Undo, Copy, Delete, Mute, Loop, Sampling,
    Track,        // index 0..3
    NoteSessionToggle, BackSettings
};

enum class MovePhase { Down, Up, Hold, Turn }; // Hold = held past threshold

struct MoveControlEvent {
    MoveControl control;
    MovePhase   phase;
    int         index    = 0;      // pad/step/encoder/track index
    float       velocity = 1.0f;   // pads: 0..1
    float       pressure = 0.0f;   // pads: aftertouch 0..1
    float       delta    = 0.0f;   // encoder/wheel relative turn
    bool        shiftHeld = false; // bus stamps current modifier state
};
```

`MoveControlBus` owns `shiftHeld` and stamps it onto every event, so modes get
Move's "hold Shift to reach the secondary function" behavior for free. The 16
step buttons being *multifunctional* is expressed as: when `shiftHeld`, a `Step`
event is interpreted by the mode as a secondary-function shortcut rather than a
sequencer step.

## Input adapters

**Touch** — `ControlSurfaceComponent` child widgets emit events directly. Pad
velocity from press dynamics is unavailable on a touchscreen, so use **Full
Velocity** by default (manual §9.3) with an on-screen velocity strip as the
override; aftertouch from finger pressure where the panel reports it, else a
long-press ramp.

**Gamepad (Legion Go, primary)** — 32 pads cannot map 1:1 to buttons, so use a
**cursor model**: a highlighted cell on the pad grid / step row moved by D-pad or
left stick; `A` = press the focused pad/step; `LT` analog squeeze = velocity
(already prototyped in `GamepadHandler`); face/shoulder buttons map to the primary
function buttons; right stick = focused encoder; `RS`-click = wheel-click; `LB`/`RB`
= arrows; `View`/`Menu` = Shift / Back. Publish the mapping in an overlay
(reuse `GamepadOverlayComponent`).

**Keyboard** — `QuickKeysHandler` binds Move's App-B shortcuts (later spec); at
minimum Space=Play, R=Record, and a QWERTY step/pad row for headless testing.

## OLED strip

Reproduce Move's 128×64 contextual display as a small on-screen panel. Much of the
manual asserts what "the display shows" (current bar, edited parameter, tempo).
Each Mode returns an `oledText()` block the strip renders, so those manual claims
stay literally true. Style with the existing `KeystageLookAndFeel` monochrome look.

## Rendering & layout

Reuse the project's heavy custom `LookAndFeel` stack. A persistent bottom/side
panel holds the surface; the existing DAW views (piano roll, timeline, mixer,
visualizers) remain reachable but are no longer the primary way to operate the app.

```
┌──────────────────────────── OLED strip (128×64 look) ─────────────────────────┐
│  Track 1 · Bar 2/4 · Velocity 100                                              │
├───────────────────────────────────────────────────────────────────────────────┤
│  [enc1][enc2][enc3][enc4][enc5][enc6][enc7][enc8][enc9]        [ VOLUME ]      │
│                                                                                 │
│  PADS (4 × 8, velocity/AT)                       WHEEL (clickable)             │
│  ┌──┬──┬──┬──┬──┬──┬──┬──┐                          ◔                          │
│  ├──┼──┼──┼──┼──┼──┼──┼──┤        [Note/Session]  [−][+]  [◀][▶]              │
│  ├──┼──┼──┼──┼──┼──┼──┼──┤        [Shift][Play][Rec][Capture][Undo]           │
│  └──┴──┴──┴──┴──┴──┴──┴──┘        [Copy][Delete][Mute][Loop][Sampling]        │
│  STEPS  [1..16] ───────────────────────────────────  [T1][T2][T3][T4]         │
└───────────────────────────────────────────────────────────────────────────────┘
```

## Scope boundary (what "done" means here)

In scope:
1. `ControlSurfaceComponent` renders all controls with correct LED/highlight state.
2. `MoveControlEvent` + `MoveControlBus` complete; Shift modifier state handled.
3. Three input adapters emit events into the bus (touch full, gamepad cursor,
   keyboard minimal).
4. `MoveMode` interface + a stub Note mode that:
   - lights a pad and plays the loaded `PluginHost` instrument on Pad-down (proves
     touch → event → engine → sound),
   - drives `SequencerEngine` transport from Play/Record,
   - renders an OLED line.

Out of scope (next specs):
- Actual step sequencing & per-step editing (§9.5, §11) — the payoff this unlocks.
- Note-mode pad layouts / scales / 16-pitches (§8, §9.1–9.4).
- Sampler (§15). Groove, arpeggiator, per-step automation.

## Test plan

1. Launch; the surface renders with all controls visible and OLED showing status.
2. Touch a pad → it lights, aftertouch/velocity reflected, instrument sounds.
3. Press Play → transport runs (`SequencerEngine::isPlaying`); Record arms.
4. Same three actions via gamepad cursor + `A`/face buttons produce identical
   events (verify the bus, not the input, is what modes see).
5. Hold Shift → a Step press is reported with `shiftHeld = true`.
6. Arrow buttons emit `ArrowLeft/Right`; wheel emits `Turn` deltas and `WheelClick`.

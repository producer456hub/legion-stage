# On-Screen Control Surface — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an on-screen reproduction of Ableton Move's control surface plus a normalized input layer, so the Move manual's control vocabulary (pads, step buttons, encoders, wheel, +/−, Shift, function buttons) refers to real, addressable objects in Motus — driven identically by touch, gamepad, or keyboard.

**Architecture:** A normalized control-event bus. Three input adapters (touch widgets, `GamepadHandler`, `QuickKeysHandler`) translate raw input into one `MoveControlEvent` vocabulary. `MoveControlBus` fans them in, owns Shift/modifier state, and dispatches to the active `MoveMode`. A stub Note mode proves the pipe end-to-end (pad → sound, Play → transport).

**Tech Stack:** C++17, JUCE 7.0.12, XInput (gamepad), existing custom `LookAndFeel` stack.

**Spec:** `docs/superpowers/specs/2026-07-05-control-surface-design.md`
**Parity scoreboard:** `docs/superpowers/specs/2026-07-05-move-parity-checklist.md`

---

## File Structure

```
src/
  MoveControlEvent.h              — new: MoveControl enum, MovePhase, MoveControlEvent struct
  MoveMode.h                      — new: interface (onControlEvent, refreshLeds, oledText)
  MoveControlBus.h/.cpp           — new: fan-in, Shift state, dispatch to active mode
  ControlSurfaceComponent.h/.cpp  — new: renders pads/steps/encoders/wheel/buttons/OLED; emits touch events
  StubNoteMode.h/.cpp             — new: minimal mode — pad→PluginHost note, Play/Record→SequencerEngine
  GamepadHandler.h/.cpp           — modify: emit MoveControlEvents (cursor model); route dormant callbacks through bus
  QuickKeysHandler.h/.cpp         — modify: emit minimal MoveControlEvents (Space=Play, R=Record, QWERTY step/pad)
  MainComponent.h/.cpp            — modify: own ControlSurfaceComponent + MoveControlBus + active mode
  CMakeLists.txt                  — modify: add new sources
```

---

## Tasks

### 1. Event vocabulary  ✅ DONE (2026-07-05, compiles clean)
- [x] Add `MoveControlEvent.h`: `MoveControl` enum (Pad, Step, Encoder, Wheel, ArrowLeft/Right, Plus, Minus, Shift, Play, Record, Capture, Undo, Copy, Delete, Mute, Loop, Sampling, Track, NoteSessionToggle, BackSettings), `MovePhase` (Down/Up/Hold/Turn), and the `MoveControlEvent` struct (index, velocity, pressure, delta, shiftHeld). Includes `pad()`/`step()`/`encoder()`/`button()` factory helpers and `kMoveVolumeEncoderIndex`.
- [x] Add `MoveMode.h` interface: `onControlEvent`, `refreshLeds(ControlSurfaceComponent&)` (fwd-declared), `oledText()`, `name()`.
- [x] Build — compiled via MoveControlBus.cpp include chain; Ninja build passes.
- [ ] Commit: "feat: add MoveControlEvent vocabulary and MoveMode interface"

### 2. Control bus  ✅ DONE (2026-07-05, compiles clean)
- [x] Add `MoveControlBus.h/.cpp`: holds active `MoveMode*`, owns `bool shiftHeld`, exposes `post(MoveControlEvent)` which stamps `shiftHeld` and forwards to the mode. Shift Down/Up updates `shiftHeld` and is NOT forwarded as a normal press.
- [x] Added to `CMakeLists.txt`; incremental build compiles `MoveControlBus.cpp` and links the app (exit 0).
- [ ] Unit-check: posting a Step event while Shift is held yields `shiftHeld == true` at the mode. *(logic in place; no test harness run yet)*
- [ ] Commit: "feat: MoveControlBus with modifier state and mode dispatch"

### 3. Surface rendering (no input yet)
- [ ] Add `ControlSurfaceComponent.h/.cpp`: lay out 32 pads (4×8), 16 steps, 9 encoders + Volume, wheel, +/−, arrows, and the primary buttons per the spec's ASCII layout, plus an OLED strip (128×64 look, `KeystageLookAndFeel`).
- [ ] LED/highlight API: `setPadLit(i, colour)`, `setStepLit(i, state)`, `setOledText(String)` — driven by the mode's `refreshLeds`.
- [ ] Add to `MainComponent` as a persistent panel; existing DAW views remain reachable behind it.
- [ ] Build and eyeball layout on the target resolution.
- [ ] Commit: "feat: ControlSurfaceComponent renders Move layout + OLED strip"

### 4. Touch adapter + stub Note mode (the end-to-end proof)
- [ ] `ControlSurfaceComponent` child widgets emit `MoveControlEvent`s into the bus on mouse/touch down/up. Pads default to Full Velocity with an on-screen velocity-strip override.
- [~] Add `StubNoteMode.h`: on `Pad Down` → light the pad + `playNote(note,vel)` hook (note-off on Pad Up); on `Play`/`Record` → transport hooks; `oledText()` shows track + transport. **Class written (header-only, decoupled via std::function hooks); not yet included/wired — `refreshLeds` is a no-op until the surface exists.**
- [ ] Wire `MainComponent` to install `StubNoteMode` as active mode.
- [ ] **Verify:** touch a pad → it lights and sounds; press Play → transport runs.
- [ ] Commit: "feat: touch adapter + stub Note mode — pad plays, transport runs"

### 5. Gamepad adapter (cursor model)
- [ ] Re-express `GamepadHandler` output as `MoveControlEvent`s: D-pad/left-stick moves a focused-cell cursor on pads/steps; `A` = press focused pad/step; `LT` squeeze = pad velocity; face/shoulder buttons → primary function buttons; right stick → focused encoder Turn; `RS`-click → WheelClick; `LB`/`RB` → arrows; `View`/`Menu` → Shift/Back.
- [ ] Route the existing dormant `onPlaceNote` / `onAdjustLength` / `onTransposeNote` through the bus instead of leaving them unwired.
- [ ] Update `GamepadOverlayComponent` to show the Move mapping.
- [ ] **Verify:** the same three actions from Task 4 done via gamepad produce identical events at the mode.
- [ ] Commit: "feat: gamepad adapter emits MoveControlEvents (cursor model)"

### 6. Keyboard adapter (minimal)
- [ ] `QuickKeysHandler` emits `MoveControlEvent`s: Space=Play, R=Record, a QWERTY row → steps, a second row → pads (for headless testing).
- [ ] **Verify:** keyboard drives transport and pads through the same bus.
- [ ] Commit: "feat: keyboard adapter for control surface (minimal)"

### 7. Close-out
- [ ] Run the full test plan from the design spec (§Test plan, all 6 checks).
- [ ] Update the parity checklist: mark the control-surface keystone landed; note which 🟡 rows are now reachable.
- [ ] Commit: "docs: mark control-surface keystone complete in parity checklist"

---

## Out of scope (next plans)

- Step-sequencing behavior (§9.5) and per-step editing (§11) — the payoff this unlocks.
- Note-mode pad layouts / scales / 16-pitches / full-velocity / aftertouch (§8, §9.1–9.4).
- Sampler (§15), groove/swing (§10.2), arpeggiator (§11.6), per-step automation (§14.2.4).

## Definition of done

All controls render with correct LED/OLED state; every Move control emits a defined `MoveControlEvent`; touch, gamepad, and keyboard all feed one bus; and the stub Note mode proves touch/gamepad/keyboard → event → engine → sound + transport. No step-sequencing behavior yet — that is the next plan.

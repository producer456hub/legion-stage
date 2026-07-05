# Ableton Move Parity — Living Checklist

## Purpose

The north star for Motus is **functional parity with the Ableton Move manual**
(https://www.ableton.com/en/move/manual/): a user handed that manual should be
able to operate Motus, the only allowed difference being that Motus is on a
screen instead of Move's hardware.

This document is the scoreboard. It walks the manual chapter by chapter and marks
each feature against the current `legion-stage` codebase. Update the status marks
as features land — "does the manual work yet?" should be a thing we can measure.

**Status key**
- ✅ Works Move-like — the manual's instructions map onto Motus
- 🟡 Capability exists but the interaction differs, so the manual won't guide the user
- ❌ Missing
- ⛔ N/A — hardware-only or companion-app feature with no meaningful screen analog

**Root finding (2026-07-05):** the dominant gap is not missing features, it's a
missing *interaction surface*. The manual's entire vocabulary — "press a pad,
press a step button, hold a step and turn the wheel, press +/−, hold Shift" — has
no on-screen equivalent yet. Motus today is a VST-hosting mini-DAW driven by a
gamepad, a piano roll, and a timeline. Fixing the surface (see
`2026-07-05-control-surface-design.md`) is the keystone that makes most other
rows reachable.

---

## Scoreboard

### Ch 3 — Move Manager · Ch 4.3 — Cloud
- 3.x Move Manager (companion web app: sets/samples/presets) — ⛔
- 4.3 Ableton Cloud — ⛔
- 4.1.2 Audio interface / 4.1.3 MIDI devices — 🟡 (ASIO audio + VST MIDI input exist; not framed as Move's connectivity menu)
- 4.2 Ableton Link — ❌

### Ch 5 — Navigating Between Set Modes
- Mode system (Note / Session / Sampling / Mixing) — 🟡 (gamepad has Navigate/Play/Edit modes; not Move's mode set)

### Ch 6 — Set Overview
- 6.1.1 Create new set — 🟡 (project save/load is thin; ~6 refs in code)
- 6.1.2 Pad color / 6.1.4 Copy / 6.1.5 Delete — ❌

### Ch 7 — Using Instruments and Effects
- 7.1 Track presets — 🟡 (VST3 via `PluginHost`, not built-in devices)
- 7.2 Browsing / swapping presets, Autoload — 🟡 (plugin browser exists; no autoload)
- 7.3 Saving presets — ❌

### Ch 8 / 9.1 — Note Mode, Pad Layouts, Keys & Scales
- Pad grid note mode — ❌ (no pad grid; has `TouchPianoComponent`)
- Keys & scales — 🟡 (`GamepadHandler` `scaleIntervals` + `rootNote` exist; no scale-picker UI)

### Ch 9 — Playing and Sequencing Notes
- 9.2 16 Pitches layout — ❌
- 9.3 Full Velocity — ❌
- 9.4 Polyphonic aftertouch — ❌
- **9.5 Sequencing Notes (pad + step-button entry) — ❌  ← core gap**

### Ch 10 — Tempo, Groove, Metronome
- 10.1 Tempo — ✅ (`SequencerEngine::setBpm`)
- 10.2 Groove / swing — ❌
- 10.3 Metronome — ✅ (`SequencerEngine` metronome + count-in)

### Ch 11 — Editing Notes and Steps
- 11.1 Velocity — 🟡 (piano-roll + unwired `onPlaceNote` velocity-from-trigger)
- 11.2 Transposition — 🟡 (`onTransposeNote` exists, unwired)
- 11.3 Note length — 🟡 (`onAdjustLength` exists, unwired; piano-roll resize works)
- 11.4 Note nudge (micro-timing) — ❌
- 11.5 Adjusting notes in loop mode — ❌
- 11.6 Arpeggiator and Repeat — ❌
- 11.7 Quantizing notes — ✅ (roughly; `onQuantizeSelected`)
- 11.8 Copy notes / step ranges — 🟡 (partial)
- 11.9 Add/remove multiple notes from steps — ❌ (no steps)

### Ch 12 — Editing Clips
- 12.1 Loop length / 12.2 Double loop — 🟡 (timeline pieces; not Move's controls)
- 12.3 Duplicate clips — 🟡
- 12.4 Delete clips and notes — 🟡

### Ch 13 — Workflow Settings
- 13.1 Quantize — ✅
- 13.2 Step grid — ❌
- 13.3 Count-in and Autoload — 🟡 (count-in ✅; autoload ❌)

### Ch 14 — Recording and Capturing
- 14.1 Recording notes (+ count-in / metronome) — ✅ (live; `SequencerEngine::toggleRecord`)
- 14.2 Recording automation — 🟡 (continuous `AutomationLane` records/plays/draws)
- 14.2.4 Per-step automation — ❌
- 14.3 Capturing notes — ✅ (`MidiCaptureBuffer`, Ableton-Capture style)

### Ch 15 — Sampling
- 15.1–15.8 Sampling mode, input source, record, resample, mic/line, USB-C, params, playback & pitch — ❌ **entirely missing; large standalone subsystem**

### Ch 16 — Session Mode
- 16.1 Launch / stop clips — ✅ (`ClipSlot` grid)
- 16.2 Create new clips — ✅
- 16.3 Clip options — 🟡
- 16.4 Global effects — 🟡

### Ch 17 — Mixing
- 17.1 Volume / 17.2 Pan / 17.3 Mute & Solo — ✅ (`MixerComponent`)

### Ch 18 — Control Live Mode · Ch 19 — USB Operation Modes · Ch 20 — Troubleshooting · App A — Specs
- ⛔ (hardware/mode features; no meaningful screen analog)

### App B — Keyboard Shortcuts
- 🟡 (`QuickKeysHandler` exists with its own bindings; not Move's shortcut set)

---

## Build order to reach parity

Dependency-ordered. Item 1 gates almost everything else.

1. **On-screen control surface** — pad grid + 16 step buttons + encoders + wheel +
   Shift. Keystone. See `2026-07-05-control-surface-design.md`.
2. **Step sequencing (9.5) + per-step editing (11.1–11.4, 11.9)** — pad picks
   pitch, step commits, hold-step + encoder/wheel/±/arrows edits value. Wires up
   the existing dormant `onPlaceNote` / `onAdjustLength` / `onTransposeNote` hooks.
3. **Note Mode surface (8, 9.1–9.4)** — pad layouts, scale picker, 16-pitches,
   full velocity, aftertouch.
4. **Sampler (15)** — largest standalone build; a subsystem Motus lacks entirely.
5. **Fill-ins** — groove/swing (10.2), arpeggiator (11.6), per-step automation
   (14.2.4), step-grid & autoload settings (13), preset save (7.3).

Features already close (recording, session clips, mixing, tempo/metronome/count-in,
quantize, continuous automation) still need re-fronting with the item-1 controls
before the manual's instructions for them read correctly.

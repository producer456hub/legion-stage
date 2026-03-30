---
name: Seed
description: Start a track by laying a backing beat or recording an inspiring melody — use when beginning a new session from a blank project or starting a new section
stage_order: 1
next_stage: layer
---

## Goal

Get something playing that you can perform against. Either a rhythm to lock your timing to, or a melody that sets the mood and direction.

## Two Paths

### Path A: Beat First

When you want rhythmic structure before melody. Good for dance-oriented tracks — techno, house, EBM.

- Pick a drum plugin or use a simple kit
- Program or tap in a basic 4-bar pattern — kick, hat, maybe a clap
- Don't overthink it — this is scaffolding, not the final beat
- Keep it simple so you can play freely on top of it

[ACTION: set_bpm | value: 124 | label: "Set house tempo (124)"]
[ACTION: set_loop | bars: 1-4 | label: "Loop 4 bars"]
[ACTION: create_track | label: "Add drum track"]
[ACTION: start_playback | label: "Play"]

### Path B: Melody First

When inspiration hits with a sound or phrase. Good for ambient, melodic techno, synthwave.

- Pick a synth that excites you — Diva for analog warmth, Pigments for movement
- Record freely without quantizing — capture the feeling
- Let the notes breathe, refine later
- If a patch inspires you, go with it immediately

[ACTION: set_loop | bars: 1-8 | label: "Loop 8 bars"]
[ACTION: create_track | label: "Add synth track"]
[ACTION: start_recording | label: "Record"]

## When to Move On

You have a loop that makes you want to keep playing. You're nodding your head or feeling the vibe. Move to **Layer**.

## Edge Cases

- Nothing inspiring after 10 minutes? Change the sound entirely — new preset, different synth
- A vocal sample, field recording, or found sound counts as a seed too
- If your beat feels too busy, strip it back — just kick and one element

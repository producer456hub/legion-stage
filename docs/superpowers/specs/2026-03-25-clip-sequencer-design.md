# Sub-project 5: Clip Sequencer — Design Spec

## Purpose

Add session-view style clip sequencing. 16 tracks × 4 clip slots. Record MIDI into clips, trigger them to loop, stop them. Global transport with BPM control. This is the final sub-project — it turns the app from a live instrument host into a sequencer.

Builds on Sub-project 4 (16-track system with volume/pan/mute/solo).

## Core Data Structures

### MidiClip

A recorded MIDI sequence:
- `juce::MidiMessageSequence events` — note on/off events with timestamps in beats (not samples)
- `double lengthInBeats` — clip length (e.g., 4.0 = one bar at 4/4)
- Clips loop: when playback reaches `lengthInBeats`, it wraps to 0

### ClipSlot

Each cell in the grid:
- `std::unique_ptr<MidiClip> clip` — the MIDI data (null if empty)
- `enum State { Empty, Stopped, Playing, Recording }` — current state
- When state is `Playing`, the engine reads MIDI events from the clip and sends them to the track's plugin
- When state is `Recording`, incoming MIDI is captured into the clip

### Track Extension

Each track (from Sub-project 4) gains:
- `std::array<ClipSlot, 4> clipSlots` — 4 clip slots
- `int activeSlot = -1` — which slot is currently playing (-1 = none)
- `bool armed = false` — whether the track is armed for recording

## Transport / Sequencer Engine

### SequencerEngine

Owned by `PluginHost`. Handles all timing and clip playback logic.

**State:**
- `bool playing = false` — global transport state
- `bool recording = false` — global record-enable
- `double bpm = 120.0` — beats per minute
- `double positionInBeats = 0.0` — current playback position (incremented each audio block)

**Per audio block (called from PluginHost::processBlock):**
1. If not playing, do nothing (live MIDI still passes through from MidiMessageCollector)
2. Calculate how many beats this block covers: `beatsPerBlock = (bpm / 60.0) * (numSamples / sampleRate)`
3. For each track with an active playing clip:
   - Calculate clip-local position: `clipPos = fmod(positionInBeats, clip.lengthInBeats)`
   - Find all MIDI events in the clip between `clipPos` and `clipPos + beatsPerBlock`
   - Convert beat timestamps to sample offsets within the current block
   - Inject these events into the track's plugin node via the graph
4. For each track that is recording:
   - Capture incoming MIDI events from the MidiMessageCollector
   - Convert sample offsets to beat timestamps relative to clip start
   - Add to the recording clip's MidiMessageSequence
5. Advance `positionInBeats += beatsPerBlock`

### MIDI Injection Per Track

Sub-projects 2-4 routed all MIDI through a single MIDI input node to the selected track. For clip playback, we need to inject MIDI into specific tracks independently.

**Approach:** Each track gets its own pending MIDI buffer. During processBlock:
- Live MIDI from MidiMessageCollector goes to the selected track (as before)
- Clip playback MIDI goes to each track's buffer independently
- Before calling `AudioProcessorGraph::processBlock()`, inject each track's pending MIDI into the graph by directly calling `track.pluginNode->getProcessor()->processBlock()` — **NO, this bypasses the graph.**

**Correct approach:** Use multiple MIDI input nodes in the graph — one per track. Each track's clip playback feeds into its own MIDI input node. The MidiMessageCollector feeds into the selected track's MIDI input node for live playing.

**Simpler MVP approach (recommended):** Since we only need clip playback + live MIDI, and only one track plays live MIDI at a time:
- Keep the single MidiMessageCollector for live input → selected track
- For clip playback, inject MIDI events directly into each plugin's processBlock by subclassing the plugin wrapper or using a custom node that prepends MIDI
- **Simplest:** Create a `ClipPlayerNode` custom AudioProcessor per track that sits between the MIDI input and the plugin. It has no audio I/O — just MIDI pass-through + clip MIDI injection. During processBlock, it adds clip MIDI events to the outgoing MidiBuffer.

### ClipPlayerNode

A minimal AudioProcessor per track:
- Holds a pointer to the track's clip slots and the engine's transport state
- In `processBlock`: if the track has an active playing clip, calculate which MIDI events fall in the current block and add them to the MidiBuffer. Pass through any incoming MIDI (from live input).
- If recording, capture incoming MIDI events and store in the recording clip.

**Graph layout becomes:**
```
MIDI Input ──► ClipPlayerNode 0 ──► Plugin 0 ──► Gain 0 ──►┐
               ClipPlayerNode 1 ──► Plugin 1 ──► Gain 1 ──►├──► Audio Output
               ...                                          │
               ClipPlayerNode 15 ──► Plugin 15 ──► Gain 15 ─►┘
```

MIDI input connects to the selected track's ClipPlayerNode. Each ClipPlayerNode independently injects clip MIDI into its downstream plugin.

## UI Layout

```
┌──────────────────────────────────────────────────────────────────────┐
│ [MIDI: ▼___________] [Refresh]  [Plugin: ▼___________] [Editor]     │
│ [Audio Settings]  [Test Note]                                        │
├─────────────────────┬──────┬──────┬──────┬──────┬────────────────────┤
│  Track List         │ S1   │ S2   │ S3   │ S4   │                    │
│  ►[A] Trk1 [Diva]▓ │ [▶]  │ [  ] │ [  ] │ [  ] │                    │
│   [ ] Trk2 [----]▓  │ [  ] │ [  ] │ [  ] │ [  ] │                    │
│   [ ] Trk3 [Pig.]▓  │ [●]  │ [▶]  │ [  ] │ [  ] │                    │
│   ...               │      │      │      │      │                    │
├─────────────────────┴──────┴──────┴──────┴──────┴────────────────────┤
│ Transport: [●REC] [▶PLAY] [■STOP]  BPM: [120___]  Beat: 5.2         │
├──────────────────────────────────────────────────────────────────────┤
│ Status: Track 1 | Loaded: Diva | MIDI: LPMiniMK3 | 44100 Hz         │
└──────────────────────────────────────────────────────────────────────┘
```

### Track List Changes
- Add arm button [A] per track (for recording)
- Track selection still works for MIDI routing

### Clip Grid
- 4 columns next to the track list
- Each cell is a button showing state:
  - Empty: gray `[ ]`
  - Has content (stopped): dim `[■]`
  - Playing: green `[▶]`
  - Recording: red `[●]`
- Click behavior:
  - Empty + track armed + recording: start recording into this slot
  - Has content + not playing: start playing (loops)
  - Playing: stop playback
  - Recording: stop recording, finalize clip

### Transport Bar
- Record button [●REC]: toggles global record enable
- Play button [▶PLAY]: starts transport (clips begin looping from beat 0)
- Stop button [■STOP]: stops transport, stops all clips, resets position to 0
- BPM field: editable, default 120
- Beat display: shows current `positionInBeats`

## File Structure

```
src/
  Main.cpp                  — unchanged
  GainProcessor.h/.cpp      — unchanged
  TrackComponent.h/.cpp     — modify: add arm button
  PluginHost.h/.cpp         — modify: add ClipPlayerNodes, integrate SequencerEngine
  MainComponent.h/.cpp      — modify: add clip grid UI, transport bar
  MidiClip.h                — new: MidiClip + ClipSlot data structures
  ClipPlayerNode.h/.cpp     — new: per-track MIDI processor for clip playback + recording
  SequencerEngine.h/.cpp    — new: transport, BPM, beat tracking, clip state management
```

## Thread Safety

- Transport state (playing, recording, bpm, position) uses atomic variables — written from UI, read from audio thread
- ClipSlot state changes (play/stop/record) use atomics
- MidiClip data: recording appends on audio thread, UI reads for display — use SpinLock during recording only
- Clip content is immutable once recording stops — no lock needed for playback

## CMake Changes

Add new source files: `MidiClip.h`, `ClipPlayerNode.h/.cpp`, `SequencerEngine.h/.cpp`

## Error Handling

- Recording with no plugin loaded: silently ignore (MIDI goes nowhere)
- Triggering empty slot: ignore
- BPM out of range: clamp to 20-300

## Out of Scope

- Clip editing (piano roll)
- Clip copy/paste/move
- Scene launching (trigger all clips in a column)
- Audio recording
- Save/load project
- Quantization
- Undo/redo

## Success Criteria

1. Transport plays and stops, BPM controls timing
2. Arm a track, hit record, play MIDI → notes captured into clip
3. Stop recording → clip has content
4. Click clip → it plays back (loops), sends MIDI to the track's plugin → sound
5. Multiple clips playing simultaneously on different tracks
6. Stop a clip → silence on that track
7. Stop transport → all clips stop, position resets
8. Live MIDI input still works on selected track while clips play on other tracks

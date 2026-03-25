# Sub-project 3: MIDI Input — Design Spec

## Purpose

Add USB MIDI keyboard support to the sequencer. Detect connected MIDI devices, let the user select one, and route its MIDI input to the loaded VST3 plugin for live playing.

Builds on Sub-project 2 (VST3 plugin hosting with audio graph).

## Architecture Change

Sub-project 2 used a `SpinLock`-protected `MidiBuffer` in `PluginHost` for test note injection. This sub-project replaces that with `juce::MidiMessageCollector`, which is designed for exactly this use case — collecting MIDI from a device callback thread and delivering it to the audio thread.

The `MidiMessageCollector` is the standard JUCE approach for routing hardware MIDI into an `AudioProcessorGraph`.

## MIDI Device Management

- Use `juce::MidiInput::getAvailableDevices()` to list connected MIDI devices
- Populate a `juce::ComboBox` with device names
- When user selects a device:
  1. Disable the previously selected device (if any)
  2. Enable the new device via `deviceManager.setMidiInputDeviceEnabled(deviceId, true)`
  3. Add `MainComponent` as a `MidiInputCallback` via `deviceManager.addMidiInputDeviceCallback(deviceId, &midiCollector)`
- The `MidiMessageCollector` receives MIDI from the device callback and buffers it for the audio thread
- Add a "Refresh" button next to the MIDI dropdown to re-scan for devices (in case a keyboard is plugged in after launch)

## MidiMessageCollector Integration

- `PluginHost` owns a `juce::MidiMessageCollector`
- Call `midiCollector.reset(sampleRate)` during `prepareToPlay` — required before first use
- In `PluginHost::processBlock()`, call `midiCollector.removeNextBlockOfMessages(midiBuffer, numSamples)` to pull buffered MIDI into the processing chain, then call `AudioProcessorGraph::processBlock()`
- The MIDI device callback feeds into the collector via `deviceManager.addMidiInputDeviceCallback(deviceId, &midiCollector)`
- Test notes also go through the collector: `midiCollector.addMessageToQueue(msg)` with timestamp from `Time::getMillisecondCounterHiRes() * 0.001`

## UI Layout

Updated layout in `MainComponent`:

```
[MIDI Input: ▼ dropdown_______________] [Refresh]
[Plugin: ▼ dropdown_________________________]
[Open Editor]  [Play Test Note]
[Audio Settings]
[Status: Loaded "Diva" | MIDI: LPMiniMK3 | 44100 Hz | 256 samples]
```

- New: `juce::ComboBox` for MIDI input selection
- New: `juce::TextButton` "Refresh" to re-scan MIDI devices
- Status label updated to show selected MIDI device name

## File Changes

```
src/
  Main.cpp              — unchanged
  MainComponent.h       — add MIDI combo box, refresh button, device management methods
  MainComponent.cpp     — MIDI device scanning, selection, callback routing
  PluginHost.h          — replace SpinLock+MidiBuffer with MidiMessageCollector
  PluginHost.cpp        — use MidiMessageCollector in processBlock, update test note injection
```

## Thread Safety

- MIDI device callbacks arrive on a dedicated MIDI thread — `MidiMessageCollector` handles this safely
- `midiCollector.addMessageToQueue()` is thread-safe from any thread
- `midiCollector.removeNextBlockOfMessages()` is called on the audio thread inside `processBlock`
- `midiCollector.reset(sampleRate)` must be called on the message thread before audio starts

## Error Handling

- No MIDI devices found: show "No MIDI devices" in dropdown, everything else still works (test note button)
- MIDI device disconnected mid-session: JUCE handles this gracefully, device simply stops sending
- Device fails to open: log via `DBG()`, show error in status label

## Out of Scope

- MIDI output (sending to external devices)
- MIDI channel filtering
- MIDI learn / CC mapping
- Multiple simultaneous MIDI inputs

## Success Criteria

1. MIDI dropdown lists connected USB MIDI devices (LPMiniMK3, Ableton Move, etc.)
2. Selecting a device routes its MIDI to the loaded plugin
3. Playing keys on the MIDI controller produces sound from the loaded VST3
4. "Play Test Note" button still works
5. "Refresh" button detects newly connected devices
6. Switching MIDI devices works cleanly

# Sub-project 2: VST3 Plugin Hosting — Design Spec

## Purpose

Add VST3 plugin hosting to the sequencer. Scan for installed plugins, let the user pick one from a dropdown, load it as an instrument, route MIDI through it, and hear audio output. Opens the plugin's native editor GUI. Single track only — multi-track comes in Sub-project 4.

Builds on Sub-project 1 (JUCE project with working ASIO audio output).

## Architecture Change

Sub-project 1 used `juce::AudioAppComponent` with a simple sine wave generator. This sub-project replaces that with:

- `juce::AudioProcessorGraph` — JUCE's audio routing graph, handles plugin instances as nodes
- `juce::AudioProcessorPlayer` — connects the graph to the audio device
- `juce::AudioPluginFormatManager` — manages VST3 plugin format
- `juce::KnownPluginList` — stores scanned plugin descriptions

The `MainComponent` changes from an `AudioAppComponent` to a regular `juce::Component`. Audio is driven by the `AudioProcessorPlayer` connected to the `AudioDeviceManager` directly.

## Plugin Scanning

- On startup, register `juce::VST3PluginFormat` with `AudioPluginFormatManager`
- Use `AudioPluginFormat::searchPathsForPlugins()` to enumerate `.vst3` bundles from the default VST3 search path (`C:/Program Files/Common Files/VST3/`)
- For each found path, call `knownPluginList.scanAndAddFile(fileOrIdentifier, true, foundTypes, *formatManager.getFormat(0))` where `foundTypes` is an `OwnedArray<PluginDescription>` (note: format is the last argument)
- Populate a `juce::ComboBox` with plugin names from the `KnownPluginList`
- Scanning can take 5-30 seconds depending on plugin count. The window will be unresponsive during this time. Show "Scanning plugins..." in the title bar. This is acceptable for MVP — background scanning is out of scope.

## Audio Graph

`juce::AudioProcessorGraph` with three nodes:

1. **MIDI Input Node** — `AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode)`. Receives MIDI via `MidiMessageCollector`.
2. **Plugin Node** — The loaded VST3 instrument. Receives MIDI from the MIDI input node, outputs audio.
3. **Audio Output Node** — `AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode)`. Sends audio to the ASIO device.

**Connections:**
- MIDI input → plugin (MIDI channel)
- Plugin → audio output (audio channels 0 and 1, stereo)

**When the user selects a different plugin from the dropdown:**

1. **Disconnect audio**: Call `audioProcessorPlayer.setProcessor(nullptr)` to stop the audio thread from accessing the graph
2. Destroy the plugin editor window if open
3. Remove the old plugin node from the graph (if any)
4. Create new plugin instance via `AudioPluginFormatManager::createPluginInstance()` (synchronous overload with error string — simpler for MVP)
5. Add as node to graph
6. Reconnect MIDI input → new plugin → audio output
7. Call `graph.prepareToPlay(sampleRate, blockSize)` to initialize the new plugin with audio parameters
8. **Reconnect audio**: Call `audioProcessorPlayer.setProcessor(&graph)` to resume audio

This suspend/resume pattern prevents audio thread crashes during graph modification.

## MIDI Injection (Test Notes)

For MVP, use a simple lock-free approach to inject test MIDI into the plugin:

- `PluginHost` owns a `juce::MidiBuffer pendingMidi` protected by a `juce::SpinLock`
- "Play Test Note" button: acquires lock, adds note-on (C4, velocity 100, sample offset 0) to `pendingMidi`. Uses `juce::Timer::callAfterDelay(500, ...)` to add note-off the same way.
- `PluginHost` subclasses `AudioProcessorGraph` and overrides `processBlock()`: acquires lock, swaps `pendingMidi` into the incoming `MidiBuffer` (appending), clears `pendingMidi`, releases lock, then calls `AudioProcessorGraph::processBlock()`
- The `SpinLock` is appropriate here because both sides hold it very briefly (just a buffer swap)
- This approach is simple, correct, and avoids the complexity of `MidiMessageCollector` wiring for now

## Plugin Editor

- "Open Editor" button calls `plugin->createEditorIfNeeded()` to get the editor
- Wrap it in a `juce::DocumentWindow` (not modal — audio must keep running)
- The window must not resize the editor content — use `setContentNonOwned(editor, true)` so the window sizes to the editor
- **Ownership**: MainComponent owns a `std::unique_ptr<DocumentWindow>` for the editor window. When switching plugins:
  1. Delete the editor window (which releases the editor)
  2. Then remove the plugin node from the graph
  3. This order prevents dangling pointer crashes
- If `createEditorIfNeeded()` returns nullptr, disable the "Open Editor" button

## UI Layout

Minimal, functional layout in `MainComponent`:

```
[Plugin: ▼ dropdown_________________________]
[Open Editor]  [Play Test Note]
[Audio Settings]
[Status: Loaded "Diva" | 44100 Hz | 256 samples]
```

- `juce::ComboBox` — plugin selector, populated from scan results
- `juce::TextButton` — "Open Editor"
- `juce::TextButton` — "Play Test Note"
- `juce::TextButton` — "Audio Settings" (kept from Sub-project 1)
- `juce::Label` — status (device info + loaded plugin name)

## File Structure Changes

```
src/
  Main.cpp                  — unchanged
  MainComponent.h           — rewritten: Component (not AudioAppComponent), owns device manager + player
  MainComponent.cpp         — rewritten: UI, plugin editor window, delegates to PluginHost
  PluginHost.h              — AudioProcessorGraph wrapper, plugin scanning, node management
  PluginHost.cpp            — graph setup, scanning, plugin instance creation, connections
```

**PluginHost** encapsulates:
- `AudioProcessorGraph` ownership
- `AudioPluginFormatManager` + `KnownPluginList`
- Plugin scanning (returns list of plugin descriptions)
- Node management (add/remove plugin, connect nodes)
- `MidiMessageCollector` for MIDI injection
- Stores current `sampleRate` and `blockSize` (set during initial `prepareToPlay`, updated if device changes)
- `loadPlugin(PluginDescription)` — handles the full suspend/swap/resume cycle, uses stored sampleRate/blockSize for `prepareToPlay`
- `sendTestNote()` — injects note-on/off via MidiMessageCollector
- Exposes `AudioProcessor*` for current plugin (for editor creation)

**MainComponent** handles:
- UI layout and button handlers
- `AudioDeviceManager` + `AudioProcessorPlayer` ownership (passed to PluginHost)
- Plugin editor window management (DocumentWindow lifecycle)
- Audio settings dialog

## CMake Changes

- Add `juce::juce_audio_processors` to `target_link_libraries`
- Add `JUCE_PLUGINHOST_VST3=1` to `target_compile_definitions` — **required** to enable VST3 hosting support

## Thread Safety

- Plugin loading/unloading happens on the message thread
- Graph modifications require suspending audio first (`setProcessor(nullptr)` / `setProcessor(&graph)`)
- `MidiMessageCollector` is thread-safe for MIDI injection from any thread
- Plugin editor creation/destruction must happen on the message thread
- Editor window must be destroyed before plugin node is removed

## Error Handling

- Plugin scan failure: skip the plugin, log to console via `DBG()`, continue scanning
- Plugin instantiation failure: show error in status label, don't crash
- Plugin editor not available: disable "Open Editor" button, show toast

## Out of Scope

- Multiple tracks / effect chains (Sub-project 4)
- USB MIDI input (Sub-project 3)
- Plugin state save/load
- VST2 support (only VST3)
- Background plugin scanning with progress bar
- Plugin parameter automation

## Success Criteria

1. App scans and lists VST3 plugins from the system
2. Selecting a plugin from dropdown loads it into the audio graph
3. "Play Test Note" produces sound from the loaded plugin
4. "Open Editor" opens the plugin's native GUI in a window
5. Switching plugins unloads the old one and loads the new one cleanly (no crashes)
6. Works with at least one of: Diva, Pigments, Analog Lab, Jun-6 V

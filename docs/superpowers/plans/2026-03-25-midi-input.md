# MIDI Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route USB MIDI keyboard input to the loaded VST3 plugin for live playing.

**Architecture:** Replace PluginHost's SpinLock MIDI injection with MidiMessageCollector. MainComponent manages MIDI device selection and routes device callbacks through the collector into the audio graph.

**Tech Stack:** C++17, JUCE 7.0.12, MidiMessageCollector, AudioDeviceManager MIDI APIs

**Spec:** `docs/superpowers/specs/2026-03-25-midi-input-design.md`

---

## File Structure

```
C:/dev/sequencer/src/
  PluginHost.h          — modify: replace SpinLock+MidiBuffer with MidiMessageCollector
  PluginHost.cpp        — modify: use collector in processBlock, update test notes
  MainComponent.h       — modify: add MIDI combo, refresh button, device tracking
  MainComponent.cpp     — modify: MIDI device scanning, selection, callback wiring
```

---

### Task 1: Update PluginHost to use MidiMessageCollector

**Files:**
- Modify: `C:/dev/sequencer/src/PluginHost.h`
- Modify: `C:/dev/sequencer/src/PluginHost.cpp`

- [ ] **Step 1: Update PluginHost.h**

Replace the SpinLock + MidiBuffer members with MidiMessageCollector. The full updated header:

```cpp
#pragma once

#include <JuceHeader.h>

class PluginHost : public juce::AudioProcessorGraph
{
public:
    PluginHost();
    ~PluginHost() override;

    // Plugin scanning
    void scanForPlugins();
    const juce::KnownPluginList& getPluginList() const { return knownPluginList; }

    // Plugin loading — caller must suspend audio before calling
    bool loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin();

    // Current plugin access (for editor creation)
    juce::AudioProcessor* getCurrentPlugin() const { return currentPlugin; }

    // MIDI — collector receives MIDI from device callbacks and test notes
    juce::MidiMessageCollector& getMidiCollector() { return midiCollector; }

    // Test note injection (goes through the collector)
    void sendTestNoteOn(int noteNumber = 60, float velocity = 0.78f);
    void sendTestNoteOff(int noteNumber = 60);

    // Override processBlock to pull MIDI from collector
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // Store audio params for plugin init during swaps
    void setAudioParams(double sampleRate, int blockSize);

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;

    // Graph nodes
    Node::Ptr midiInputNode;
    Node::Ptr audioOutputNode;
    Node::Ptr pluginNode;

    // Current loaded plugin (raw ptr — graph owns it via the node)
    juce::AudioProcessor* currentPlugin = nullptr;

    // MIDI collector — receives from device callbacks + test notes
    juce::MidiMessageCollector midiCollector;

    // Stored audio params
    double storedSampleRate = 44100.0;
    int storedBlockSize = 512;

    void setupGraph();
    void connectPlugin();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
```

- [ ] **Step 2: Update PluginHost.cpp**

Replace the full file. Key changes from previous version:
- Constructor: no change to format registration
- `setAudioParams`: also calls `midiCollector.reset(sampleRate)`
- `processBlock`: uses `midiCollector.removeNextBlockOfMessages()` instead of SpinLock swap
- `sendTestNoteOn/Off`: uses `midiCollector.addMessageToQueue()` with proper timestamp
- Everything else (scanning, loading, graph setup) stays the same

```cpp
#include "PluginHost.h"

PluginHost::PluginHost()
{
    formatManager.addFormat(new juce::VST3PluginFormat());
    setupGraph();
}

PluginHost::~PluginHost()
{
    clear();
}

void PluginHost::setupGraph()
{
    setPlayConfigDetails(0, 2, storedSampleRate, storedBlockSize);

    midiInputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    audioOutputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
}

void PluginHost::scanForPlugins()
{
    auto* format = formatManager.getFormat(0);
    if (format == nullptr) return;

    auto searchPaths = format->getDefaultLocationsToSearch();
    auto foundFiles = format->searchPathsForPlugins(searchPaths, true, false);

    for (const auto& file : foundFiles)
    {
        juce::OwnedArray<juce::PluginDescription> foundTypes;
        knownPluginList.scanAndAddFile(file, true, foundTypes, *format);
    }
}

void PluginHost::setAudioParams(double sampleRate, int blockSize)
{
    storedSampleRate = sampleRate;
    storedBlockSize = blockSize;
    midiCollector.reset(sampleRate);
}

bool PluginHost::loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg)
{
    unloadPlugin();

    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    currentPlugin = instance.get();

    pluginNode = addNode(std::move(instance));
    if (pluginNode == nullptr)
    {
        currentPlugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectPlugin();
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin()
{
    if (pluginNode != nullptr)
    {
        auto connections = getConnections();
        for (auto& conn : connections)
        {
            if (conn.source.nodeID == pluginNode->nodeID ||
                conn.destination.nodeID == pluginNode->nodeID)
            {
                removeConnection(conn);
            }
        }

        removeNode(pluginNode->nodeID);
        pluginNode = nullptr;
        currentPlugin = nullptr;
    }
}

void PluginHost::connectPlugin()
{
    if (pluginNode == nullptr) return;

    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { pluginNode->nodeID, ch },
                        { audioOutputNode->nodeID, ch } });
    }
}

void PluginHost::sendTestNoteOn(int noteNumber, float velocity)
{
    auto msg = juce::MidiMessage::noteOn(1, noteNumber, velocity);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::sendTestNoteOff(int noteNumber)
{
    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Pull any buffered MIDI from the collector into the processing chain
    midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

    // Process the graph (routes MIDI to plugin, audio to output)
    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/PluginHost.h src/PluginHost.cpp
git commit -m "refactor: switch PluginHost to MidiMessageCollector for MIDI input"
```

---

### Task 2: Update MainComponent for MIDI device selection

**Files:**
- Modify: `C:/dev/sequencer/src/MainComponent.h`
- Modify: `C:/dev/sequencer/src/MainComponent.cpp`

- [ ] **Step 1: Update MainComponent.h**

Add MIDI combo box, refresh button, and device tracking. The full updated header:

```cpp
#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor,
                       std::function<void()> onClose)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton),
          closeCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(editor, true);
        setVisible(true);
        centreWithSize(getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
    }

private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // UI — MIDI
    juce::ComboBox midiInputSelector;
    juce::TextButton midiRefreshButton { "Refresh" };

    // UI — Plugin
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton   { "Open Editor" };
    juce::TextButton testNoteButton     { "Play Test Note" };
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::Label statusLabel;

    // Plugin editor window
    std::unique_ptr<juce::AudioProcessorEditor> currentEditor;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    // Plugin descriptions (indexed by combo box)
    juce::Array<juce::PluginDescription> pluginDescriptions;

    // MIDI device tracking
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::String currentMidiDeviceId;

    void scanMidiDevices();
    void selectMidiDevice();
    void disableCurrentMidiDevice();

    void scanPlugins();
    void loadSelectedPlugin();
    void openPluginEditor();
    void closePluginEditor();
    void playTestNote();
    void showAudioSettings();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
```

- [ ] **Step 2: Update MainComponent.cpp**

Replace the full file. Key additions:
- `scanMidiDevices()` — populates MIDI dropdown
- `selectMidiDevice()` — enables selected device, routes callback to collector
- `disableCurrentMidiDevice()` — cleans up previous device
- Constructor calls `scanMidiDevices()` after plugin scan
- Destructor calls `disableCurrentMidiDevice()`

```cpp
#include "MainComponent.h"

MainComponent::MainComponent()
{
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty())
        DBG("Audio device init error: " + result);

    audioPlayer.setProcessor(&pluginHost);
    deviceManager.addAudioCallback(&audioPlayer);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        pluginHost.setAudioParams(device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples());
        pluginHost.prepareToPlay(device->getCurrentSampleRate(),
                                 device->getCurrentBufferSizeSamples());
    }

    // MIDI UI
    addAndMakeVisible(midiInputSelector);
    midiInputSelector.onChange = [this] { selectMidiDevice(); };

    addAndMakeVisible(midiRefreshButton);
    midiRefreshButton.onClick = [this] { scanMidiDevices(); };

    // Plugin UI
    addAndMakeVisible(pluginSelector);
    pluginSelector.onChange = [this] { loadSelectedPlugin(); };

    addAndMakeVisible(openEditorButton);
    openEditorButton.onClick = [this] { openPluginEditor(); };
    openEditorButton.setEnabled(false);

    addAndMakeVisible(testNoteButton);
    testNoteButton.onClick = [this] { playTestNote(); };
    testNoteButton.setEnabled(false);

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);

    setSize(800, 600);

    scanPlugins();
    scanMidiDevices();
    updateStatusLabel();
}

MainComponent::~MainComponent()
{
    disableCurrentMidiDevice();
    closePluginEditor();
    audioPlayer.setProcessor(nullptr);
    deviceManager.removeAudioCallback(&audioPlayer);
}

// ── MIDI Device Management ───────────────────────────────────────────────────

void MainComponent::scanMidiDevices()
{
    midiInputSelector.clear(juce::dontSendNotification);
    midiDevices = juce::MidiInput::getAvailableDevices();

    midiInputSelector.addItem("-- No MIDI Input --", 1);

    int itemId = 2;
    for (const auto& device : midiDevices)
    {
        midiInputSelector.addItem(device.name, itemId++);
    }

    midiInputSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::selectMidiDevice()
{
    disableCurrentMidiDevice();

    int selectedIndex = midiInputSelector.getSelectedId() - 2;
    if (selectedIndex < 0 || selectedIndex >= midiDevices.size())
    {
        updateStatusLabel();
        return;
    }

    auto& device = midiDevices[selectedIndex];
    deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
    deviceManager.addMidiInputDeviceCallback(device.identifier, &pluginHost.getMidiCollector());
    currentMidiDeviceId = device.identifier;

    updateStatusLabel();
}

void MainComponent::disableCurrentMidiDevice()
{
    if (currentMidiDeviceId.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(currentMidiDeviceId, &pluginHost.getMidiCollector());
        deviceManager.setMidiInputDeviceEnabled(currentMidiDeviceId, false);
        currentMidiDeviceId.clear();
    }
}

// ── Plugin Management (unchanged from Sub-project 2) ─────────────────────────

void MainComponent::scanPlugins()
{
    statusLabel.setText("Scanning plugins...", juce::dontSendNotification);
    repaint();

    pluginHost.scanForPlugins();

    pluginSelector.clear(juce::dontSendNotification);
    pluginDescriptions.clear();
    pluginSelector.addItem("-- Select Plugin --", 1);

    int itemId = 2;
    for (const auto& desc : pluginHost.getPluginList().getTypes())
    {
        if (desc.isInstrument)
        {
            pluginSelector.addItem(desc.name, itemId);
            pluginDescriptions.add(desc);
            itemId++;
        }
    }

    pluginSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::loadSelectedPlugin()
{
    int selectedIndex = pluginSelector.getSelectedId() - 2;
    if (selectedIndex < 0 || selectedIndex >= pluginDescriptions.size())
    {
        openEditorButton.setEnabled(false);
        testNoteButton.setEnabled(false);
        return;
    }

    closePluginEditor();
    audioPlayer.setProcessor(nullptr);

    juce::String errorMsg;
    bool success = pluginHost.loadPlugin(pluginDescriptions[selectedIndex], errorMsg);

    audioPlayer.setProcessor(&pluginHost);

    if (success)
    {
        openEditorButton.setEnabled(true);
        testNoteButton.setEnabled(true);
        updateStatusLabel();
    }
    else
    {
        openEditorButton.setEnabled(false);
        testNoteButton.setEnabled(false);
        statusLabel.setText("Failed: " + errorMsg, juce::dontSendNotification);
    }
}

void MainComponent::openPluginEditor()
{
    auto* plugin = pluginHost.getCurrentPlugin();
    if (plugin == nullptr) return;

    closePluginEditor();

    currentEditor.reset(plugin->createEditorIfNeeded());
    if (currentEditor == nullptr)
    {
        statusLabel.setText("Plugin has no editor", juce::dontSendNotification);
        return;
    }

    editorWindow = std::make_unique<PluginEditorWindow>(
        plugin->getName(), currentEditor.get(),
        [this] { closePluginEditor(); });
}

void MainComponent::closePluginEditor()
{
    // IMPORTANT: destroy window first, then editor.
    editorWindow = nullptr;
    currentEditor = nullptr;
}

void MainComponent::playTestNote()
{
    pluginHost.sendTestNoteOn(60, 0.78f);

    juce::Timer::callAfterDelay(500, [this] {
        pluginHost.sendTestNoteOff(60);
    });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, 0, 1, 2, false, false, false, false);
    selector->setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio Settings";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = getLookAndFeel().findColour(
        juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();

    juce::Timer::callAfterDelay(500, [this] {
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            pluginHost.setAudioParams(device->getCurrentSampleRate(),
                                      device->getCurrentBufferSizeSamples());
        }
        updateStatusLabel();
    });
}

void MainComponent::updateStatusLabel()
{
    juce::String text;

    auto* plugin = pluginHost.getCurrentPlugin();
    if (plugin != nullptr)
        text += "Loaded: " + plugin->getName() + " | ";

    if (currentMidiDeviceId.isNotEmpty())
    {
        for (const auto& dev : midiDevices)
        {
            if (dev.identifier == currentMidiDeviceId)
            {
                text += "MIDI: " + dev.name + " | ";
                break;
            }
        }
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        text += device->getName()
              + " | " + juce::String(device->getCurrentSampleRate(), 0) + " Hz"
              + " | " + juce::String(device->getCurrentBufferSizeSamples()) + " samples";
    }
    else
    {
        text += "No audio device";
    }

    statusLabel.setText(text, juce::dontSendNotification);
}

// ── Component overrides ──────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    // MIDI input row
    auto midiRow = area.removeFromTop(30);
    midiRefreshButton.setBounds(midiRow.removeFromRight(80));
    midiRow.removeFromRight(5);
    midiInputSelector.setBounds(midiRow);
    area.removeFromTop(10);

    // Plugin selector
    pluginSelector.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    // Button row
    auto buttonRow = area.removeFromTop(30);
    openEditorButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2));
    testNoteButton.setBounds(buttonRow);
    area.removeFromTop(10);

    // Audio settings
    audioSettingsButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    // Status
    statusLabel.setBounds(area.removeFromTop(30));
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.h src/MainComponent.cpp
git commit -m "feat: add MIDI input device selection and routing"
```

---

### Task 3: Build and Test

- [ ] **Step 1: Kill old Sequencer, reconfigure, build**

```bash
taskkill //IM Sequencer.exe //F 2>/dev/null
cd /c/dev/sequencer
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

- [ ] **Step 2: Run and test**

Launch Sequencer.exe. Expected:
1. MIDI dropdown shows "LPMiniMK3 MIDI" and "Ableton Move MIDI"
2. Select a MIDI device
3. Load a plugin (e.g., Diva)
4. Play keys on the MIDI controller → hear sound
5. "Play Test Note" still works
6. "Refresh" button re-scans for devices

- [ ] **Step 3: Final commit**

```bash
cd /c/dev/sequencer
git add src/
git commit -m "feat: MIDI input complete — USB keyboard routing to VST3 plugins"
```

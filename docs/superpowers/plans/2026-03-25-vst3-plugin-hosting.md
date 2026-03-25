# VST3 Plugin Hosting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add VST3 plugin hosting — scan plugins, load instruments, route MIDI through them, hear audio, open native editor GUIs.

**Architecture:** Replace the AudioAppComponent + test tone with an AudioProcessorGraph-based host. PluginHost class wraps the graph, scanning, and node management. MainComponent becomes a plain Component that owns the AudioDeviceManager, AudioProcessorPlayer, and UI.

**Tech Stack:** C++17, JUCE 7.0.12, VST3 SDK (via JUCE), CMake + VS 2019

**Spec:** `docs/superpowers/specs/2026-03-25-vst3-plugin-hosting-design.md`

---

## File Structure

```
C:/dev/sequencer/
  CMakeLists.txt              — modify: add juce_audio_processors, JUCE_PLUGINHOST_VST3, new source files
  src/
    Main.cpp                  — unchanged
    MainComponent.h           — rewrite: plain Component, owns DeviceManager + Player + PluginHost
    MainComponent.cpp         — rewrite: new UI (combo, buttons), editor window, delegates to PluginHost
    PluginHost.h              — new: subclass of AudioProcessorGraph, plugin scanning, node mgmt, MIDI injection
    PluginHost.cpp            — new: implementation of scanning, graph setup, plugin loading, test notes
```

---

### Task 1: Update CMakeLists.txt

**Files:**
- Modify: `C:/dev/sequencer/CMakeLists.txt`

- [ ] **Step 1: Add VST3 hosting support to CMake**

Add `JUCE_PLUGINHOST_VST3=1` to `target_compile_definitions`, add `juce::juce_audio_processors` to `target_link_libraries`, and add the new source files to `target_sources`.

The full updated CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.22)
project(Sequencer VERSION 0.1.0)

# ASIO SDK path — JUCE needs this to compile ASIO support
set(JUCE_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk" CACHE PATH "ASIO SDK location")

add_subdirectory(libs/JUCE)

juce_add_gui_app(Sequencer
    PRODUCT_NAME "Sequencer"
    COMPANY_NAME "Dev"
)

juce_generate_juce_header(Sequencer)

target_sources(Sequencer PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp
    src/PluginHost.h
    src/PluginHost.cpp
)

target_compile_definitions(Sequencer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
)

# Point JUCE to the ASIO SDK headers
target_include_directories(Sequencer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk/common
)

target_link_libraries(Sequencer PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add CMakeLists.txt
git commit -m "chore: add VST3 hosting support to CMake config"
```

---

### Task 2: PluginHost — Header

**Files:**
- Create: `C:/dev/sequencer/src/PluginHost.h`

- [ ] **Step 1: Write PluginHost.h**

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
    // Returns true on success, false on failure (errorMsg set)
    bool loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin();

    // Current plugin access (for editor creation)
    juce::AudioProcessor* getCurrentPlugin() const { return currentPlugin; }

    // MIDI injection for test notes
    void sendTestNoteOn(int noteNumber = 60, float velocity = 0.78f);
    void sendTestNoteOff(int noteNumber = 60);

    // Override processBlock to inject pending MIDI
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

    // MIDI injection
    juce::MidiBuffer pendingMidi;
    juce::SpinLock midiLock;

    // Stored audio params
    double storedSampleRate = 44100.0;
    int storedBlockSize = 512;

    void setupGraph();
    void connectPlugin();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/PluginHost.h
git commit -m "feat: add PluginHost header"
```

---

### Task 3: PluginHost — Implementation

**Files:**
- Create: `C:/dev/sequencer/src/PluginHost.cpp`

- [ ] **Step 1: Write PluginHost.cpp**

```cpp
#include "PluginHost.h"

PluginHost::PluginHost()
{
    // Explicitly add only VST3 — don't rely on addDefaultFormats() ordering
    formatManager.addFormat(new juce::VST3PluginFormat());
    setupGraph();
}

PluginHost::~PluginHost()
{
    // Clear graph before destruction
    clear();
}

void PluginHost::setupGraph()
{
    // Set graph channel layout: 0 MIDI in, 2 audio out
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
    auto* format = formatManager.getFormat(0); // VST3 format
    if (format == nullptr) return;

    // Get default search paths for VST3
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
}

bool PluginHost::loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg)
{
    // Remove old plugin first
    unloadPlugin();

    // Create plugin instance
    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    currentPlugin = instance.get();

    // Add to graph
    pluginNode = addNode(std::move(instance));
    if (pluginNode == nullptr)
    {
        currentPlugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectPlugin();

    // Re-prepare the graph so the new plugin gets initialized
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin()
{
    if (pluginNode != nullptr)
    {
        // Copy connections first — iterating while removing invalidates the container
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

    // MIDI: midiInput -> plugin
    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    // Audio: plugin -> audioOutput (stereo)
    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { pluginNode->nodeID, ch },
                        { audioOutputNode->nodeID, ch } });
    }
}

void PluginHost::sendTestNoteOn(int noteNumber, float velocity)
{
    auto msg = juce::MidiMessage::noteOn(1, noteNumber, velocity);
    msg.setTimeStamp(0);

    const juce::SpinLock::ScopedLockType lock(midiLock);
    pendingMidi.addEvent(msg, 0);
}

void PluginHost::sendTestNoteOff(int noteNumber)
{
    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    msg.setTimeStamp(0);

    const juce::SpinLock::ScopedLockType lock(midiLock);
    pendingMidi.addEvent(msg, 0);
}

void PluginHost::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Inject any pending MIDI messages
    {
        const juce::SpinLock::ScopedLockType lock(midiLock);
        if (!pendingMidi.isEmpty())
        {
            midiMessages.addEvents(pendingMidi, 0, buffer.getNumSamples(), 0);
            pendingMidi.clear();
        }
    }

    // Process the graph
    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/PluginHost.cpp
git commit -m "feat: add PluginHost with scanning, graph, and MIDI injection"
```

---

### Task 4: Rewrite MainComponent — Header

**Files:**
- Modify: `C:/dev/sequencer/src/MainComponent.h`

- [ ] **Step 1: Rewrite MainComponent.h**

Replace the entire contents with:

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
        // Notify owner to properly destroy editor + window (prevents dangling content pointer)
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

    // UI
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

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.h
git commit -m "feat: rewrite MainComponent header for VST3 hosting"
```

---

### Task 5: Rewrite MainComponent — Implementation

**Files:**
- Modify: `C:/dev/sequencer/src/MainComponent.cpp`

- [ ] **Step 1: Rewrite MainComponent.cpp**

Replace the entire contents with:

```cpp
#include "MainComponent.h"

MainComponent::MainComponent()
{
    // Initialize audio device
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty())
        DBG("Audio device init error: " + result);

    // Set up the audio player with the plugin host graph
    audioPlayer.setProcessor(&pluginHost);
    deviceManager.addAudioCallback(&audioPlayer);

    // Store audio params in plugin host
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        pluginHost.setAudioParams(device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples());
        pluginHost.prepareToPlay(device->getCurrentSampleRate(),
                                 device->getCurrentBufferSizeSamples());
    }

    // UI setup
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

    // Scan for plugins (blocks UI — acceptable for MVP)
    scanPlugins();
    updateStatusLabel();
}

MainComponent::~MainComponent()
{
    closePluginEditor();
    audioPlayer.setProcessor(nullptr);
    deviceManager.removeAudioCallback(&audioPlayer);
}

void MainComponent::scanPlugins()
{
    // Show scanning status
    statusLabel.setText("Scanning plugins...", juce::dontSendNotification);
    repaint();

    pluginHost.scanForPlugins();

    // Populate combo box
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

    // Close editor first
    closePluginEditor();

    // Suspend audio, load plugin, resume audio
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
    // IMPORTANT: destroy window first, then editor. Reversing this order
    // causes a dangling content pointer crash in the DocumentWindow.
    editorWindow = nullptr;
    currentEditor = nullptr;
}

void MainComponent::playTestNote()
{
    pluginHost.sendTestNoteOn(60, 0.78f); // C4

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

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    pluginSelector.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    auto buttonRow = area.removeFromTop(30);
    openEditorButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2).reduced(0, 0));
    testNoteButton.setBounds(buttonRow);
    area.removeFromTop(10);

    audioSettingsButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    statusLabel.setBounds(area.removeFromTop(30));
}
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.cpp
git commit -m "feat: rewrite MainComponent with plugin hosting UI"
```

---

### Task 6: Build and Test

**Files:**
- No new files — build and verify

- [ ] **Step 1: Reconfigure CMake**

```bash
cd /c/dev/sequencer
cmake -B build -G "Visual Studio 16 2019" -A x64
```

Expected: Configures without errors.

- [ ] **Step 2: Build**

```bash
cd /c/dev/sequencer
cmake --build build --config Release 2>&1 | tail -30
```

Expected: Builds without errors. If there are compile errors, fix them.

- [ ] **Step 3: Run and test**

Find and run Sequencer.exe. Expected:
1. App shows "Scanning plugins..." briefly, then populates dropdown with VST3 instruments
2. Select a plugin (e.g., Diva, Pigments) → loads without crash
3. Click "Play Test Note" → hear sound from the plugin
4. Click "Open Editor" → plugin's native GUI appears in a separate window
5. Switch plugins → old editor closes, new plugin loads cleanly

- [ ] **Step 4: Fix any issues found**

Address any build errors or runtime bugs.

- [ ] **Step 5: Final commit**

```bash
cd /c/dev/sequencer
git add src/ CMakeLists.txt
git commit -m "feat: VST3 plugin hosting complete — scan, load, play, edit"
```

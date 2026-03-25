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

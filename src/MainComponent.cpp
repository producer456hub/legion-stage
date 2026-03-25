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

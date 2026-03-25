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

    // Track list
    setupTrackList();
    addAndMakeVisible(trackViewport);
    trackViewport.setViewedComponent(&trackListContainer, false);

    setSize(900, 700);

    scanPlugins();
    scanMidiDevices();
    selectTrack(0);
    updateStatusLabel();
}

MainComponent::~MainComponent()
{
    disableCurrentMidiDevice();
    closePluginEditor();
    audioPlayer.setProcessor(nullptr);
    deviceManager.removeAudioCallback(&audioPlayer);
}

void MainComponent::setupTrackList()
{
    trackComponents.clear();

    for (int i = 0; i < PluginHost::NUM_TRACKS; ++i)
    {
        auto* tc = new TrackComponent(i);
        tc->onSelected = [this](int idx) { selectTrack(idx); };
        tc->onVolumeChanged = [this](int idx, float vol) { onTrackVolumeChanged(idx, vol); };
        tc->onMuteChanged = [this](int idx, bool m) { onTrackMuteChanged(idx, m); };
        tc->onSoloChanged = [this](int idx, bool s) { onTrackSoloChanged(idx, s); };
        trackListContainer.addAndMakeVisible(tc);
        trackComponents.add(tc);
    }

    trackListContainer.setSize(350, PluginHost::NUM_TRACKS * 36);
}

void MainComponent::selectTrack(int index)
{
    selectedTrackIndex = index;
    pluginHost.setSelectedTrack(index);

    for (int i = 0; i < trackComponents.size(); ++i)
        trackComponents[i]->setSelected(i == index);

    // Update plugin selector to show the selected track's plugin
    auto& track = pluginHost.getTrack(index);
    if (track.plugin != nullptr)
    {
        openEditorButton.setEnabled(true);
        testNoteButton.setEnabled(true);
    }
    else
    {
        openEditorButton.setEnabled(false);
        testNoteButton.setEnabled(false);
    }

    // Close editor when switching tracks
    closePluginEditor();
    updateStatusLabel();
}

void MainComponent::onTrackVolumeChanged(int trackIndex, float volume)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor != nullptr)
        track.gainProcessor->volume.store(volume);
}

void MainComponent::onTrackMuteChanged(int trackIndex, bool muted)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor != nullptr)
        track.gainProcessor->muted.store(muted);
}

void MainComponent::onTrackSoloChanged(int trackIndex, bool soloed)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor != nullptr)
    {
        bool wasSoloed = track.gainProcessor->soloed.load();
        track.gainProcessor->soloed.store(soloed);

        if (soloed && !wasSoloed)
            pluginHost.soloCount.fetch_add(1);
        else if (!soloed && wasSoloed)
            pluginHost.soloCount.fetch_sub(1);
    }
}

// ── MIDI Device Management ───────────────────────────────────────────────────

void MainComponent::scanMidiDevices()
{
    midiInputSelector.clear(juce::dontSendNotification);
    midiDevices = juce::MidiInput::getAvailableDevices();
    midiInputSelector.addItem("-- No MIDI Input --", 1);
    int itemId = 2;
    for (const auto& device : midiDevices)
        midiInputSelector.addItem(device.name, itemId++);
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

// ── Plugin Management ────────────────────────────────────────────────────────

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
    bool success = pluginHost.loadPlugin(selectedTrackIndex, pluginDescriptions[selectedIndex], errorMsg);

    audioPlayer.setProcessor(&pluginHost);

    if (success)
    {
        openEditorButton.setEnabled(true);
        testNoteButton.setEnabled(true);
        trackComponents[selectedTrackIndex]->setPluginName(pluginDescriptions[selectedIndex].name);
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
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.plugin == nullptr) return;

    closePluginEditor();

    currentEditor.reset(track.plugin->createEditorIfNeeded());
    if (currentEditor == nullptr)
    {
        statusLabel.setText("Plugin has no editor", juce::dontSendNotification);
        return;
    }

    editorWindow = std::make_unique<PluginEditorWindow>(
        track.plugin->getName(), currentEditor.get(),
        [this] { closePluginEditor(); });
}

void MainComponent::closePluginEditor()
{
    // IMPORTANT: destroy window first, then editor
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
            pluginHost.setAudioParams(device->getCurrentSampleRate(),
                                      device->getCurrentBufferSizeSamples());
        updateStatusLabel();
    });
}

void MainComponent::updateStatusLabel()
{
    juce::String text;

    text += "Track " + juce::String(selectedTrackIndex + 1) + " | ";

    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.plugin != nullptr)
        text += "Loaded: " + track.plugin->getName() + " | ";
    else
        text += "No plugin | ";

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
    auto area = getLocalBounds().reduced(10);

    // Top controls
    auto topArea = area.removeFromTop(150);

    auto midiRow = topArea.removeFromTop(30);
    midiRefreshButton.setBounds(midiRow.removeFromRight(80));
    midiRow.removeFromRight(5);
    midiInputSelector.setBounds(midiRow);
    topArea.removeFromTop(5);

    pluginSelector.setBounds(topArea.removeFromTop(30));
    topArea.removeFromTop(5);

    auto buttonRow = topArea.removeFromTop(30);
    openEditorButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2));
    testNoteButton.setBounds(buttonRow);
    topArea.removeFromTop(5);

    audioSettingsButton.setBounds(topArea.removeFromTop(30));

    area.removeFromTop(5);

    // Status bar at bottom
    statusLabel.setBounds(area.removeFromBottom(25));
    area.removeFromBottom(5);

    // Track list takes remaining space
    trackViewport.setBounds(area);

    // Layout track components inside the container
    int trackHeight = 36;
    trackListContainer.setSize(trackViewport.getWidth() - trackViewport.getScrollBarThickness(),
                               PluginHost::NUM_TRACKS * trackHeight);
    for (int i = 0; i < trackComponents.size(); ++i)
    {
        trackComponents[i]->setBounds(0, i * trackHeight,
                                       trackListContainer.getWidth(), trackHeight);
    }
}

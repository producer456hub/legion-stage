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

    // MIDI
    addAndMakeVisible(midiInputSelector);
    midiInputSelector.onChange = [this] { selectMidiDevice(); };
    addAndMakeVisible(midiRefreshButton);
    midiRefreshButton.onClick = [this] { scanMidiDevices(); };

    // Plugin
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

    // Transport
    addAndMakeVisible(recordButton);
    recordButton.setClickingTogglesState(true);
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    recordButton.onClick = [this] { pluginHost.getEngine().toggleRecord(); };

    addAndMakeVisible(playButton);
    playButton.onClick = [this] { pluginHost.getEngine().play(); };

    addAndMakeVisible(stopButton);
    stopButton.onClick = [this] {
        pluginHost.getEngine().stop();
        // Stop all clips
        for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
        {
            auto* cp = pluginHost.getTrack(t).clipPlayer;
            if (cp) cp->stopAllSlots();
        }
        updateClipButtons();
    };

    addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(20.0, 300.0, 1.0);
    bpmSlider.setValue(120.0, juce::dontSendNotification);
    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 20);
    bpmSlider.onValueChange = [this] {
        pluginHost.getEngine().setBpm(bpmSlider.getValue());
    };

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("BPM:", juce::dontSendNotification);

    addAndMakeVisible(beatLabel);
    beatLabel.setText("Beat: 0.0", juce::dontSendNotification);

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);

    // Track list + clip grid
    setupTrackList();
    addAndMakeVisible(trackViewport);
    trackViewport.setViewedComponent(&trackListContainer, false);

    setupClipGrid();
    addAndMakeVisible(clipGridViewport);
    clipGridViewport.setViewedComponent(&clipGridContainer, false);

    setSize(950, 750);

    scanPlugins();
    scanMidiDevices();
    selectTrack(0);
    updateStatusLabel();

    // Timer for UI updates (clip states, beat display)
    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    disableCurrentMidiDevice();
    closePluginEditor();
    audioPlayer.setProcessor(nullptr);
    deviceManager.removeAudioCallback(&audioPlayer);
}

void MainComponent::timerCallback()
{
    // Update beat display
    double beat = pluginHost.getEngine().getPositionInBeats();
    beatLabel.setText("Beat: " + juce::String(beat, 1), juce::dontSendNotification);

    // Update clip button states
    updateClipButtons();
}

// ── Track List ───────────────────────────────────────────────────────────────

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
        tc->onArmChanged = [this](int idx, bool a) { onTrackArmChanged(idx, a); };
        trackListContainer.addAndMakeVisible(tc);
        trackComponents.add(tc);
    }
    trackListContainer.setSize(280, PluginHost::NUM_TRACKS * 32);
}

void MainComponent::setupClipGrid()
{
    clipButtons.clear();
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto* btn = new juce::TextButton("");
            btn->onClick = [this, t, s] { onClipButtonClicked(t, s); };
            clipGridContainer.addAndMakeVisible(btn);
            clipButtons.add(btn);
        }
    }
    clipGridContainer.setSize(ClipPlayerNode::NUM_SLOTS * 50, PluginHost::NUM_TRACKS * 32);
}

void MainComponent::onClipButtonClicked(int trackIndex, int slotIndex)
{
    auto* cp = pluginHost.getTrack(trackIndex).clipPlayer;
    if (cp == nullptr) return;

    cp->triggerSlot(slotIndex);
    updateClipButtons();
}

void MainComponent::updateClipButtons()
{
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto* cp = pluginHost.getTrack(t).clipPlayer;
        if (cp == nullptr) continue;

        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto& slot = cp->getSlot(s);
            auto* btn = clipButtons[t * ClipPlayerNode::NUM_SLOTS + s];

            auto state = slot.state.load();
            switch (state)
            {
                case ClipSlot::Empty:
                    btn->setButtonText("");
                    btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
                    break;
                case ClipSlot::Stopped:
                    btn->setButtonText(juce::String::charToString(0x25A0)); // ■
                    btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555555));
                    break;
                case ClipSlot::Playing:
                    btn->setButtonText(juce::String::charToString(0x25B6)); // ▶
                    btn->setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
                    break;
                case ClipSlot::Recording:
                    btn->setButtonText(juce::String::charToString(0x25CF)); // ●
                    btn->setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
                    break;
            }
        }
    }
}

void MainComponent::selectTrack(int index)
{
    selectedTrackIndex = index;
    pluginHost.setSelectedTrack(index);
    for (int i = 0; i < trackComponents.size(); ++i)
        trackComponents[i]->setSelected(i == index);

    auto& track = pluginHost.getTrack(index);
    openEditorButton.setEnabled(track.plugin != nullptr);
    testNoteButton.setEnabled(track.plugin != nullptr);

    // Reset plugin selector so user can load a plugin on this track
    pluginSelector.setSelectedId(1, juce::dontSendNotification);

    closePluginEditor();
    updateStatusLabel();
}

void MainComponent::onTrackVolumeChanged(int trackIndex, float volume)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor) track.gainProcessor->volume.store(volume);
}

void MainComponent::onTrackMuteChanged(int trackIndex, bool muted)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor) track.gainProcessor->muted.store(muted);
}

void MainComponent::onTrackSoloChanged(int trackIndex, bool soloed)
{
    auto& track = pluginHost.getTrack(trackIndex);
    if (track.gainProcessor)
    {
        bool was = track.gainProcessor->soloed.load();
        track.gainProcessor->soloed.store(soloed);
        if (soloed && !was) pluginHost.soloCount.fetch_add(1);
        else if (!soloed && was) pluginHost.soloCount.fetch_sub(1);
    }
}

void MainComponent::onTrackArmChanged(int trackIndex, bool armed)
{
    auto* cp = pluginHost.getTrack(trackIndex).clipPlayer;
    if (cp) cp->armed.store(armed);
}

// ── MIDI ─────────────────────────────────────────────────────────────────────

void MainComponent::scanMidiDevices()
{
    midiInputSelector.clear(juce::dontSendNotification);
    midiDevices = juce::MidiInput::getAvailableDevices();
    midiInputSelector.addItem("-- No MIDI --", 1);
    midiInputSelector.addItem("Computer Keyboard", 2);
    int id = 3;
    for (const auto& d : midiDevices) midiInputSelector.addItem(d.name, id++);
    midiInputSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::selectMidiDevice()
{
    disableCurrentMidiDevice();
    useComputerKeyboard = false;

    int selectedId = midiInputSelector.getSelectedId();

    if (selectedId == 2)
    {
        // Computer keyboard mode
        useComputerKeyboard = true;
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();
        updateStatusLabel();
        return;
    }

    int idx = selectedId - 3;
    if (idx < 0 || idx >= midiDevices.size()) { updateStatusLabel(); return; }
    auto& d = midiDevices[idx];
    deviceManager.setMidiInputDeviceEnabled(d.identifier, true);
    deviceManager.addMidiInputDeviceCallback(d.identifier, &pluginHost.getMidiCollector());
    currentMidiDeviceId = d.identifier;
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

// ── Plugin ───────────────────────────────────────────────────────────────────

void MainComponent::scanPlugins()
{
    statusLabel.setText("Scanning plugins...", juce::dontSendNotification);
    repaint();
    pluginHost.scanForPlugins();
    pluginSelector.clear(juce::dontSendNotification);
    pluginDescriptions.clear();
    pluginSelector.addItem("-- Select Plugin --", 1);
    int id = 2;
    for (const auto& desc : pluginHost.getPluginList().getTypes())
    {
        if (desc.isInstrument)
        {
            pluginSelector.addItem(desc.name, id);
            pluginDescriptions.add(desc);
            id++;
        }
    }
    pluginSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::loadSelectedPlugin()
{
    int idx = pluginSelector.getSelectedId() - 2;
    if (idx < 0 || idx >= pluginDescriptions.size())
    {
        openEditorButton.setEnabled(false);
        testNoteButton.setEnabled(false);
        return;
    }
    closePluginEditor();
    audioPlayer.setProcessor(nullptr);
    juce::String err;
    bool ok = pluginHost.loadPlugin(selectedTrackIndex, pluginDescriptions[idx], err);
    audioPlayer.setProcessor(&pluginHost);
    if (ok)
    {
        openEditorButton.setEnabled(true);
        testNoteButton.setEnabled(true);
        trackComponents[selectedTrackIndex]->setPluginName(pluginDescriptions[idx].name);
        updateStatusLabel();
    }
    else
    {
        openEditorButton.setEnabled(false);
        testNoteButton.setEnabled(false);
        statusLabel.setText("Failed: " + err, juce::dontSendNotification);
    }
}

void MainComponent::openPluginEditor()
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.plugin == nullptr) return;
    closePluginEditor();
    currentEditor.reset(track.plugin->createEditorIfNeeded());
    if (currentEditor == nullptr) { statusLabel.setText("No editor", juce::dontSendNotification); return; }
    editorWindow = std::make_unique<PluginEditorWindow>(track.plugin->getName(), currentEditor.get(),
        [this] { closePluginEditor(); });
}

void MainComponent::closePluginEditor()
{
    editorWindow = nullptr;
    currentEditor = nullptr;
}

void MainComponent::playTestNote()
{
    pluginHost.sendTestNoteOn(60, 0.78f);
    juce::Timer::callAfterDelay(500, [this] { pluginHost.sendTestNoteOff(60); });
}

void MainComponent::showAudioSettings()
{
    auto* sel = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 0, 1, 2, false, false, false, false);
    sel->setSize(500, 400);
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(sel);
    opt.dialogTitle = "Audio Settings";
    opt.componentToCentreAround = this;
    opt.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
    juce::Timer::callAfterDelay(500, [this] {
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            pluginHost.setAudioParams(dev->getCurrentSampleRate(), dev->getCurrentBufferSizeSamples());
        updateStatusLabel();
    });
}

void MainComponent::updateStatusLabel()
{
    juce::String text = "Track " + juce::String(selectedTrackIndex + 1) + " | ";
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    text += (track.plugin ? "Loaded: " + track.plugin->getName() : juce::String("No plugin")) + " | ";
    if (useComputerKeyboard)
        text += "MIDI: Computer KB (Oct " + juce::String(computerKeyboardOctave) + ") | ";
    else if (currentMidiDeviceId.isNotEmpty())
        for (const auto& d : midiDevices)
            if (d.identifier == currentMidiDeviceId) { text += "MIDI: " + d.name + " | "; break; }
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        text += dev->getName() + " | " + juce::String(dev->getCurrentSampleRate(), 0) + " Hz";
    else text += "No audio device";
    statusLabel.setText(text, juce::dontSendNotification);
}

// ── Layout ───────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    // Top controls — 2 rows
    auto row1 = area.removeFromTop(28);
    midiRefreshButton.setBounds(row1.removeFromRight(65));
    row1.removeFromRight(4);
    auto midiArea = row1.removeFromLeft(row1.getWidth() / 2);
    midiInputSelector.setBounds(midiArea);
    row1.removeFromLeft(4);
    openEditorButton.setBounds(row1.removeFromRight(80));
    row1.removeFromRight(4);
    pluginSelector.setBounds(row1);
    area.removeFromTop(4);

    auto row2 = area.removeFromTop(28);
    audioSettingsButton.setBounds(row2.removeFromLeft(110));
    row2.removeFromLeft(4);
    testNoteButton.setBounds(row2.removeFromLeft(100));
    area.removeFromTop(6);

    // Transport bar
    auto transport = area.removeFromBottom(32);
    recordButton.setBounds(transport.removeFromLeft(50));
    transport.removeFromLeft(4);
    playButton.setBounds(transport.removeFromLeft(55));
    transport.removeFromLeft(4);
    stopButton.setBounds(transport.removeFromLeft(55));
    transport.removeFromLeft(8);
    bpmLabel.setBounds(transport.removeFromLeft(35));
    bpmSlider.setBounds(transport.removeFromLeft(150));
    transport.removeFromLeft(8);
    beatLabel.setBounds(transport.removeFromLeft(100));
    area.removeFromBottom(4);

    // Status bar
    statusLabel.setBounds(area.removeFromBottom(22));
    area.removeFromBottom(4);

    // Track list (left) + clip grid (right)
    int trackListWidth = 280;
    int clipGridWidth = ClipPlayerNode::NUM_SLOTS * 50;
    int trackHeight = 32;
    int totalHeight = PluginHost::NUM_TRACKS * trackHeight;

    trackViewport.setBounds(area.removeFromLeft(trackListWidth));
    area.removeFromLeft(4);
    clipGridViewport.setBounds(area.removeFromLeft(clipGridWidth));

    trackListContainer.setSize(trackListWidth - trackViewport.getScrollBarThickness(), totalHeight);
    clipGridContainer.setSize(clipGridWidth, totalHeight);

    for (int i = 0; i < trackComponents.size(); ++i)
        trackComponents[i]->setBounds(0, i * trackHeight, trackListContainer.getWidth(), trackHeight);

    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            int idx = t * ClipPlayerNode::NUM_SLOTS + s;
            clipButtons[idx]->setBounds(s * 50, t * trackHeight + 2, 46, trackHeight - 4);
        }
    }
}

// ── Computer Keyboard MIDI ───────────────────────────────────────────────────

int MainComponent::keyToNote(int keyCode) const
{
    // QWERTY layout:
    // Black keys: W E   T Y U   O P
    // White keys: A S D F G H J K L
    // Z = octave down, X = octave up
    switch (keyCode)
    {
        case 'A': return 0;   // C
        case 'W': return 1;   // C#
        case 'S': return 2;   // D
        case 'E': return 3;   // D#
        case 'D': return 4;   // E
        case 'F': return 5;   // F
        case 'T': return 6;   // F#
        case 'G': return 7;   // G
        case 'Y': return 8;   // G#
        case 'H': return 9;   // A
        case 'U': return 10;  // A#
        case 'J': return 11;  // B
        case 'K': return 12;  // C (next octave)
        case 'O': return 13;  // C#
        case 'L': return 14;  // D
        case 'P': return 15;  // D#
        default: return -1;
    }
}

void MainComponent::sendNoteOn(int note)
{
    auto msg = juce::MidiMessage::noteOn(1, note, 0.8f);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    pluginHost.getMidiCollector().addMessageToQueue(msg);
}

void MainComponent::sendNoteOff(int note)
{
    auto msg = juce::MidiMessage::noteOff(1, note);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    pluginHost.getMidiCollector().addMessageToQueue(msg);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (!useComputerKeyboard) return false;

    int keyCode = key.getTextCharacter();
    if (keyCode >= 'a' && keyCode <= 'z') keyCode -= 32; // uppercase

    // Octave shift
    if (keyCode == 'Z') { computerKeyboardOctave = juce::jmax(0, computerKeyboardOctave - 1); updateStatusLabel(); return true; }
    if (keyCode == 'X') { computerKeyboardOctave = juce::jmin(8, computerKeyboardOctave + 1); updateStatusLabel(); return true; }

    return false; // let keyStateChanged handle note keys
}

bool MainComponent::keyStateChanged(bool /*isKeyDown*/)
{
    if (!useComputerKeyboard) return false;

    // Check all mapped keys and compare with tracked state
    const int mappedKeys[] = { 'A','W','S','E','D','F','T','G','Y','H','U','J','K','O','L','P' };

    for (int keyCode : mappedKeys)
    {
        bool isDown = juce::KeyPress::isKeyCurrentlyDown(keyCode);
        int semitone = keyToNote(keyCode);
        if (semitone < 0) continue;

        int midiNote = (computerKeyboardOctave * 12) + semitone;
        if (midiNote < 0 || midiNote > 127) continue;

        bool wasDown = keysCurrentlyDown.count(keyCode) > 0;

        if (isDown && !wasDown)
        {
            keysCurrentlyDown.insert(keyCode);
            sendNoteOn(midiNote);
        }
        else if (!isDown && wasDown)
        {
            keysCurrentlyDown.erase(keyCode);
            sendNoteOff(midiNote);
        }
    }

    return true;
}

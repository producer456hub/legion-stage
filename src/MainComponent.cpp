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

    // ── Top Bar: Transport + Track Select ──
    addAndMakeVisible(prevTrackButton);
    prevTrackButton.onClick = [this] { selectTrack(juce::jmax(0, selectedTrackIndex - 1)); };

    addAndMakeVisible(nextTrackButton);
    nextTrackButton.onClick = [this] { selectTrack(juce::jmin(PluginHost::NUM_TRACKS - 1, selectedTrackIndex + 1)); };

    addAndMakeVisible(trackNameLabel);
    trackNameLabel.setJustificationType(juce::Justification::centred);
    trackNameLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    trackNameLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(recordButton);
    recordButton.setClickingTogglesState(true);
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker());
    recordButton.onClick = [this] { pluginHost.getEngine().toggleRecord(); };

    addAndMakeVisible(playButton);
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff336633));
    playButton.onClick = [this] { pluginHost.getEngine().play(); };

    addAndMakeVisible(stopButton);
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    stopButton.onClick = [this] {
        auto& eng = pluginHost.getEngine();
        if (!eng.isPlaying())
        {
            eng.resetPosition();
            for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
            {
                auto* cp = pluginHost.getTrack(t).clipPlayer;
                if (cp) cp->stopAllSlots();
            }
        }
        else
        {
            eng.stop();
            for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
            {
                auto* cp = pluginHost.getTrack(t).clipPlayer;
                if (cp) cp->sendAllNotesOff.store(true);
            }
        }
        if (timelineComponent) timelineComponent->repaint();
    };

    addAndMakeVisible(metronomeButton);
    metronomeButton.setClickingTogglesState(true);
    metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    metronomeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff886600));
    metronomeButton.onClick = [this] { pluginHost.getEngine().toggleMetronome(); };

    addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(20.0, 300.0, 1.0);
    bpmSlider.setValue(120.0, juce::dontSendNotification);
    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 25);
    bpmSlider.onValueChange = [this] { pluginHost.getEngine().setBpm(bpmSlider.getValue()); };

    addAndMakeVisible(beatLabel);
    beatLabel.setFont(juce::Font(14.0f));

    // ── Edit Toolbar ──
    addAndMakeVisible(newClipButton);
    newClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff336655));
    newClipButton.onClick = [this] {
        if (timelineComponent) timelineComponent->createClipAtPlayhead();
    };

    addAndMakeVisible(deleteClipButton);
    deleteClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff553333));
    deleteClipButton.onClick = [this] {
        if (timelineComponent) timelineComponent->deleteSelected();
    };

    addAndMakeVisible(duplicateClipButton);
    duplicateClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff335555));
    duplicateClipButton.onClick = [this] {
        if (timelineComponent) timelineComponent->duplicateSelected();
    };

    addAndMakeVisible(splitClipButton);
    splitClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555533));
    splitClipButton.onClick = [this] {
        if (timelineComponent) timelineComponent->splitSelected();
    };

    addAndMakeVisible(editClipButton);
    editClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff335566));
    editClipButton.onClick = [this] {
        if (timelineComponent)
        {
            auto* clip = timelineComponent->getSelectedClip();
            if (clip != nullptr)
                new PianoRollWindow("Piano Roll", *clip, pluginHost.getEngine());
        }
    };

    // ── Right Panel ──
    addAndMakeVisible(pluginSelector);
    pluginSelector.onChange = [this] { loadSelectedPlugin(); };

    addAndMakeVisible(openEditorButton);
    openEditorButton.onClick = [this] { openPluginEditor(); };
    openEditorButton.setEnabled(false);

    addAndMakeVisible(midiInputSelector);
    midiInputSelector.onChange = [this] { selectMidiDevice(); };

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(testNoteButton);
    testNoteButton.onClick = [this] { playTestNote(); };
    testNoteButton.setEnabled(false);

    // ── Bottom Bar: Mix Controls ──
    addAndMakeVisible(volumeSlider);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.8, juce::dontSendNotification);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.onValueChange = [this] {
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.gainProcessor) track.gainProcessor->volume.store(static_cast<float>(volumeSlider.getValue()));
    };

    addAndMakeVisible(panSlider);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.onValueChange = [this] {
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.gainProcessor) track.gainProcessor->pan.store(static_cast<float>(panSlider.getValue()));
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(12.0f));

    // ── Timeline (arrangement view — always visible) ──
    timelineComponent = std::make_unique<TimelineComponent>(pluginHost);
    addAndMakeVisible(*timelineComponent);

    setSize(1280, 800);
    setWantsKeyboardFocus(true);

    scanPlugins();
    scanMidiDevices();
    selectTrack(0);
    updateStatusLabel();

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

// ── Timer ────────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    double beat = pluginHost.getEngine().getPositionInBeats();
    beatLabel.setText("Beat: " + juce::String(beat, 1), juce::dontSendNotification);

    // Sync if timeline changed the selected track or arm state
    int currentSelected = pluginHost.getSelectedTrack();
    if (currentSelected != selectedTrackIndex)
    {
        selectedTrackIndex = currentSelected;
        updateTrackDisplay();
        updateStatusLabel();
    }
}

// ── Track Selection ──────────────────────────────────────────────────────────

void MainComponent::selectTrack(int index)
{
    selectedTrackIndex = juce::jlimit(0, PluginHost::NUM_TRACKS - 1, index);
    pluginHost.setSelectedTrack(selectedTrackIndex);
    closePluginEditor();
    updateTrackDisplay();
    updateStatusLabel();
    pluginSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::updateTrackDisplay()
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);

    juce::String name = "Track " + juce::String(selectedTrackIndex + 1);
    if (track.plugin != nullptr)
        name += ": " + track.plugin->getName();
    trackNameLabel.setText(name, juce::dontSendNotification);

    openEditorButton.setEnabled(track.plugin != nullptr);
    testNoteButton.setEnabled(track.plugin != nullptr);

    if (track.gainProcessor)
    {
        volumeSlider.setValue(track.gainProcessor->volume.load(), juce::dontSendNotification);
        panSlider.setValue(track.gainProcessor->pan.load(), juce::dontSendNotification);
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
    pluginSelector.addItem("-- Plugin --", 1);

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
    if (idx < 0 || idx >= pluginDescriptions.size()) return;

    closePluginEditor();
    audioPlayer.setProcessor(nullptr);

    juce::String err;
    bool ok = pluginHost.loadPlugin(selectedTrackIndex, pluginDescriptions[idx], err);

    audioPlayer.setProcessor(&pluginHost);

    if (ok)
        updateTrackDisplay();
    else
        statusLabel.setText("Failed: " + err, juce::dontSendNotification);

    updateStatusLabel();
}

void MainComponent::openPluginEditor()
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.plugin == nullptr) return;
    closePluginEditor();
    currentEditor.reset(track.plugin->createEditorIfNeeded());
    if (currentEditor == nullptr) return;
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
    juce::String text;
    if (useComputerKeyboard)
        text += "KB Oct " + juce::String(computerKeyboardOctave) + " | ";
    else if (currentMidiDeviceId.isNotEmpty())
        for (const auto& d : midiDevices)
            if (d.identifier == currentMidiDeviceId) { text += d.name + " | "; break; }

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        text += dev->getName() + " | " + juce::String(dev->getCurrentSampleRate(), 0) + " Hz";
    statusLabel.setText(text, juce::dontSendNotification);
}

// ── Layout ───────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    int topBarH = 50;
    int bottomBarH = 45;
    int rightPanelW = 200;

    // ── Top Bar ──
    auto topBar = area.removeFromTop(topBarH).reduced(6, 6);

    prevTrackButton.setBounds(topBar.removeFromLeft(40));
    topBar.removeFromLeft(4);
    nextTrackButton.setBounds(topBar.removeFromLeft(40));
    topBar.removeFromLeft(8);
    trackNameLabel.setBounds(topBar.removeFromLeft(200));
    topBar.removeFromLeft(8);

    recordButton.setBounds(topBar.removeFromLeft(55));
    topBar.removeFromLeft(4);
    playButton.setBounds(topBar.removeFromLeft(60));
    topBar.removeFromLeft(4);
    stopButton.setBounds(topBar.removeFromLeft(55));
    topBar.removeFromLeft(4);
    metronomeButton.setBounds(topBar.removeFromLeft(45));
    topBar.removeFromLeft(8);

    beatLabel.setBounds(topBar.removeFromRight(90));
    bpmSlider.setBounds(topBar);

    // ── Bottom Bar ──
    auto bottomBar = area.removeFromBottom(bottomBarH).reduced(8, 6);

    volumeSlider.setBounds(bottomBar.removeFromLeft(150));
    bottomBar.removeFromLeft(8);
    panSlider.setBounds(bottomBar.removeFromLeft(100));
    bottomBar.removeFromLeft(8);
    statusLabel.setBounds(bottomBar);

    // ── Edit Toolbar ──
    auto toolbar = area.removeFromTop(40).reduced(6, 4);
    newClipButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(4);
    deleteClipButton.setBounds(toolbar.removeFromLeft(75));
    toolbar.removeFromLeft(4);
    duplicateClipButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(4);
    splitClipButton.setBounds(toolbar.removeFromLeft(65));
    toolbar.removeFromLeft(4);
    editClipButton.setBounds(toolbar.removeFromLeft(90));

    // ── Right Panel ──
    auto rightPanel = area.removeFromRight(rightPanelW).reduced(8, 4);

    pluginSelector.setBounds(rightPanel.removeFromTop(32));
    rightPanel.removeFromTop(6);
    openEditorButton.setBounds(rightPanel.removeFromTop(36));
    rightPanel.removeFromTop(6);
    testNoteButton.setBounds(rightPanel.removeFromTop(36));
    rightPanel.removeFromTop(12);
    midiInputSelector.setBounds(rightPanel.removeFromTop(32));
    rightPanel.removeFromTop(6);
    audioSettingsButton.setBounds(rightPanel.removeFromTop(36));

    // ── Timeline fills the entire center ──
    area.reduce(2, 2);
    if (timelineComponent)
        timelineComponent->setBounds(area);
}

// ── Keyboard ─────────────────────────────────────────────────────────────────

int MainComponent::keyToNote(int keyCode) const
{
    switch (keyCode)
    {
        case 'A': return 0;  case 'W': return 1;  case 'S': return 2;  case 'E': return 3;
        case 'D': return 4;  case 'F': return 5;  case 'T': return 6;  case 'G': return 7;
        case 'Y': return 8;  case 'H': return 9;  case 'U': return 10; case 'J': return 11;
        case 'K': return 12; case 'O': return 13; case 'L': return 14; case 'P': return 15;
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
    // Spacebar = play/stop
    if (key == juce::KeyPress::spaceKey)
    {
        auto& eng = pluginHost.getEngine();
        if (eng.isPlaying())
        {
            eng.stop();
            for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
            {
                auto* cp = pluginHost.getTrack(t).clipPlayer;
                if (cp) cp->sendAllNotesOff.store(true);
            }
        }
        else
        {
            double timeSinceLastStop = juce::Time::getMillisecondCounterHiRes() - lastSpaceStopTime;
            if (timeSinceLastStop < 400.0)
            {
                eng.resetPosition();
                for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
                {
                    auto* cp = pluginHost.getTrack(t).clipPlayer;
                    if (cp) cp->stopAllSlots();
                }
                if (timelineComponent) timelineComponent->repaint();
            }
            else
            {
                eng.play();
            }
        }
        lastSpaceStopTime = eng.isPlaying() ? 0.0 : juce::Time::getMillisecondCounterHiRes();
        return true;
    }

    // Arrow keys = switch tracks
    if (key == juce::KeyPress::leftKey) { selectTrack(selectedTrackIndex - 1); return true; }
    if (key == juce::KeyPress::rightKey) { selectTrack(selectedTrackIndex + 1); return true; }

    if (!useComputerKeyboard) return false;

    int keyCode = key.getTextCharacter();
    if (keyCode >= 'a' && keyCode <= 'z') keyCode -= 32;

    if (keyCode == 'Z') { computerKeyboardOctave = juce::jmax(0, computerKeyboardOctave - 1); updateStatusLabel(); return true; }
    if (keyCode == 'X') { computerKeyboardOctave = juce::jmin(8, computerKeyboardOctave + 1); updateStatusLabel(); return true; }

    return false;
}

bool MainComponent::keyStateChanged(bool /*isKeyDown*/)
{
    if (!useComputerKeyboard) return false;

    const int mappedKeys[] = { 'A','W','S','E','D','F','T','G','Y','H','U','J','K','O','L','P' };

    for (int keyCode : mappedKeys)
    {
        bool isDown = juce::KeyPress::isKeyCurrentlyDown(keyCode);
        int semitone = keyToNote(keyCode);
        if (semitone < 0) continue;

        int midiNote = (computerKeyboardOctave * 12) + semitone;
        if (midiNote < 0 || midiNote > 127) continue;

        bool wasDown = keysCurrentlyDown.count(keyCode) > 0;

        if (isDown && !wasDown) { keysCurrentlyDown.insert(keyCode); sendNoteOn(midiNote); }
        else if (!isDown && wasDown) { keysCurrentlyDown.erase(keyCode); sendNoteOff(midiNote); }
    }
    return true;
}

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
        takeSnapshot();
        if (timelineComponent) timelineComponent->createClipAtPlayhead();
    };

    addAndMakeVisible(deleteClipButton);
    deleteClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff553333));
    deleteClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->deleteSelected();
    };

    addAndMakeVisible(duplicateClipButton);
    duplicateClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff335555));
    duplicateClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->duplicateSelected();
    };

    addAndMakeVisible(splitClipButton);
    splitClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555533));
    splitClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->splitSelected();
    };

    addAndMakeVisible(quantizeButton);
    quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555544));
    quantizeButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->quantizeSelectedClip();
    };

    addAndMakeVisible(gridSelector);
    gridSelector.addItem("1/4", 1);
    gridSelector.addItem("1/8", 2);
    gridSelector.addItem("1/16", 3);
    gridSelector.addItem("1/32", 4);
    gridSelector.setSelectedId(3, juce::dontSendNotification); // default 1/16
    gridSelector.onChange = [this] {
        if (timelineComponent)
        {
            double res = 1.0;
            switch (gridSelector.getSelectedId())
            {
                case 1: res = 1.0; break;    // 1/4
                case 2: res = 0.5; break;    // 1/8
                case 3: res = 0.25; break;   // 1/16
                case 4: res = 0.125; break;  // 1/32
            }
            timelineComponent->setGridResolution(res);
        }
    };

    addAndMakeVisible(countInButton);
    countInButton.setClickingTogglesState(true);
    countInButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    countInButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff665533));
    countInButton.onClick = [this] { pluginHost.getEngine().toggleCountIn(); };

    addAndMakeVisible(zoomInButton);
    zoomInButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    zoomInButton.onClick = [this] { if (timelineComponent) timelineComponent->zoomIn(); };

    addAndMakeVisible(zoomOutButton);
    zoomOutButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    zoomOutButton.onClick = [this] { if (timelineComponent) timelineComponent->zoomOut(); };

    addAndMakeVisible(scrollLeftButton);
    scrollLeftButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    scrollLeftButton.onClick = [this] { if (timelineComponent) timelineComponent->scrollLeft(); };

    addAndMakeVisible(scrollRightButton);
    scrollRightButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444444));
    scrollRightButton.onClick = [this] { if (timelineComponent) timelineComponent->scrollRight(); };

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
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    volumeSlider.onValueChange = [this] {
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.gainProcessor) track.gainProcessor->volume.store(static_cast<float>(volumeSlider.getValue()));
    };

    addAndMakeVisible(volumeLabel);
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.setFont(juce::Font(12.0f));

    addAndMakeVisible(panSlider);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    panSlider.onValueChange = [this] {
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.gainProcessor) track.gainProcessor->pan.store(static_cast<float>(panSlider.getValue()));
    };

    addAndMakeVisible(panLabel);
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setFont(juce::Font(12.0f));

    addAndMakeVisible(saveButton);
    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff336644));
    saveButton.onClick = [this] { saveProject(); };

    addAndMakeVisible(loadButton);
    loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff444466));
    loadButton.onClick = [this] { loadProject(); };

    addAndMakeVisible(undoButton);
    undoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555544));
    undoButton.onClick = [this] {
        if (undoIndex > 0)
        {
            undoIndex--;
            restoreSnapshot(undoHistory[undoIndex]);
        }
    };

    addAndMakeVisible(redoButton);
    redoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff555544));
    redoButton.onClick = [this] {
        if (undoIndex < undoHistory.size() - 1)
        {
            undoIndex++;
            restoreSnapshot(undoHistory[undoIndex]);
        }
    };

    addAndMakeVisible(trackInfoLabel);
    trackInfoLabel.setJustificationType(juce::Justification::topLeft);
    trackInfoLabel.setFont(juce::Font(11.0f));
    trackInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    trackInfoLabel.setInterceptsMouseClicks(false, false);

    // Plugin parameter sliders
    for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
    {
        auto* slider = new juce::Slider();
        slider->setRange(0.0, 1.0, 0.001);
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        slider->setEnabled(false);

        int paramIdx = i; // capture for lambda
        slider->onValueChange = [this, slider] {
            auto& track = pluginHost.getTrack(selectedTrackIndex);
            if (track.plugin == nullptr) return;

            int realIdx = static_cast<int>(slider->getProperties().getWithDefault("paramIndex", -1));
            auto& params = track.plugin->getParameters();
            if (realIdx < 0 || realIdx >= params.size()) return;

            params[realIdx]->setValue(static_cast<float>(slider->getValue()));

            // Record automation if transport is playing + recording
            auto& eng = pluginHost.getEngine();
            if (eng.isPlaying() && eng.isRecording() && !eng.isInCountIn())
            {
                AutomationLane* lane = nullptr;
                for (auto* l : track.automationLanes)
                {
                    if (l->parameterIndex == realIdx) { lane = l; break; }
                }
                if (lane == nullptr)
                {
                    lane = new AutomationLane();
                    lane->parameterIndex = realIdx;
                    lane->parameterName = params[realIdx]->getName(20);
                    track.automationLanes.add(lane);
                }

                AutomationPoint pt;
                pt.beat = eng.getPositionInBeats();
                pt.value = static_cast<float>(slider->getValue());
                lane->points.add(pt);
            }
        };

        addAndMakeVisible(slider);
        paramSliders.add(slider);

        auto* label = new juce::Label();
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::Font(9.0f));
        label->setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        addAndMakeVisible(label);
        paramLabels.add(label);
    }

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(12.0f));

    // ── Timeline (arrangement view — always visible) ──
    timelineComponent = std::make_unique<TimelineComponent>(pluginHost);
    timelineComponent->onBeforeEdit = [this] { takeSnapshot(); };
    addAndMakeVisible(*timelineComponent);

    setSize(1280, 800);
    setWantsKeyboardFocus(true);

    scanPlugins();
    scanMidiDevices();
    selectTrack(0);
    updateStatusLabel();

    // Initial undo snapshot
    takeSnapshot();

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
    auto& eng = pluginHost.getEngine();

    if (eng.isInCountIn())
    {
        int barsLeft = static_cast<int>(std::ceil(eng.getCountInBeatsRemaining() / 4.0));
        beatLabel.setText("Count: -" + juce::String(barsLeft), juce::dontSendNotification);
    }
    else
    {
        double beat = eng.getPositionInBeats();
        beatLabel.setText("Beat: " + juce::String(beat, 1), juce::dontSendNotification);
    }

    // Auto-snapshot when recording stops (detect transition)
    static bool wasRecording = false;
    bool isRec = false;
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto* cp = pluginHost.getTrack(t).clipPlayer;
        if (cp != nullptr)
            for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
                if (cp->getSlot(s).state.load() == ClipSlot::Recording)
                    isRec = true;
    }
    if (wasRecording && !isRec)
        takeSnapshot();
    wasRecording = isRec;

    // Sync if timeline changed the selected track or arm state
    int currentSelected = pluginHost.getSelectedTrack();
    if (currentSelected != selectedTrackIndex)
    {
        selectedTrackIndex = currentSelected;
        updateTrackDisplay();
        updateStatusLabel();
    }

    // Repaint mini mixer
    if (!miniMixerBounds.isEmpty())
        repaint(miniMixerBounds);
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

    // Track info
    juce::String info;
    if (track.plugin != nullptr)
        info += "Plugin: " + track.plugin->getName() + "\n";
    else
        info += "Plugin: (none)\n";

    if (track.clipPlayer != nullptr)
    {
        int clipCount = 0;
        int totalNotes = 0;
        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto& slot = track.clipPlayer->getSlot(s);
            if (slot.hasContent())
            {
                clipCount++;
                totalNotes += slot.clip->events.getNumEvents() / 2; // note on+off pairs
            }
        }
        info += "Clips: " + juce::String(clipCount) + "\n";
        info += "Notes: " + juce::String(totalNotes) + "\n";
    }

    info += "Armed: " + juce::String(track.clipPlayer && track.clipPlayer->armed.load() ? "Yes" : "No");
    trackInfoLabel.setText(info, juce::dontSendNotification);

    updateParamSliders();

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

// ── Plugin Parameters ─────────────────────────────────────────────────────────

void MainComponent::updateParamSliders()
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);

    if (track.plugin == nullptr)
    {
        for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
        {
            paramSliders[i]->setEnabled(false);
            paramSliders[i]->setValue(0.0, juce::dontSendNotification);
            paramLabels[i]->setText("", juce::dontSendNotification);
        }
        return;
    }

    auto& allParams = track.plugin->getParameters();

    // First, try to find Macro parameters (Arturia plugins)
    juce::Array<juce::AudioProcessorParameter*> selectedParams;

    for (auto* param : allParams)
    {
        juce::String name = param->getName(30).toLowerCase();
        if (name.contains("macro") || name.contains("mcr") || name.contains("assign"))
            selectedParams.add(param);
    }

    // If no macros found, try common useful parameter names
    if (selectedParams.isEmpty())
    {
        for (auto* param : allParams)
        {
            juce::String name = param->getName(30).toLowerCase();
            if (name.contains("cutoff") || name.contains("filter") ||
                name.contains("resonance") || name.contains("attack") ||
                name.contains("release") || name.contains("volume") ||
                name.contains("drive") || name.contains("mix"))
                selectedParams.add(param);

            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }

    // If still nothing useful, just use the first N parameters
    if (selectedParams.isEmpty())
    {
        for (int i = 0; i < juce::jmin(NUM_PARAM_SLIDERS, allParams.size()); ++i)
            selectedParams.add(allParams[i]);
    }

    for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
    {
        if (i < selectedParams.size())
        {
            auto* param = selectedParams[i];
            paramSliders[i]->setEnabled(true);
            paramSliders[i]->setValue(param->getValue(), juce::dontSendNotification);
            paramLabels[i]->setText(param->getName(12), juce::dontSendNotification);

            // Store the actual parameter index for the slider callback
            paramSliders[i]->getProperties().set("paramIndex", allParams.indexOf(param));
        }
        else
        {
            paramSliders[i]->setEnabled(false);
            paramSliders[i]->setValue(0.0, juce::dontSendNotification);
            paramLabels[i]->setText("", juce::dontSendNotification);
            paramSliders[i]->getProperties().set("paramIndex", -1);
        }
    }
}

// ── Save/Load/Undo ───────────────────────────────────────────────────────────

void MainComponent::takeSnapshot()
{
    // Trim future history if we undid something
    while (undoHistory.size() > undoIndex + 1)
        undoHistory.removeLast();

    ProjectSnapshot snap;
    snap.bpm = pluginHost.getEngine().getBpm();

    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto* cp = pluginHost.getTrack(t).clipPlayer;
        if (cp == nullptr) continue;

        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto& slot = cp->getSlot(s);
            if (slot.clip != nullptr && slot.hasContent())
            {
                ProjectSnapshot::ClipData cd;
                cd.trackIndex = t;
                cd.slotIndex = s;
                cd.lengthInBeats = slot.clip->lengthInBeats;
                cd.timelinePosition = slot.clip->timelinePosition;

                for (int e = 0; e < slot.clip->events.getNumEvents(); ++e)
                    cd.events.addEvent(slot.clip->events.getEventPointer(e)->message);
                cd.events.updateMatchedPairs();

                snap.clips.add(std::move(cd));
            }
        }
    }

    undoHistory.add(std::move(snap));
    undoIndex = undoHistory.size() - 1;

    // Limit history
    if (undoHistory.size() > 50)
    {
        undoHistory.remove(0);
        undoIndex--;
    }
}

void MainComponent::restoreSnapshot(const ProjectSnapshot& snap)
{
    // Clear all clips
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto* cp = pluginHost.getTrack(t).clipPlayer;
        if (cp == nullptr) continue;

        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto& slot = cp->getSlot(s);
            slot.clip = nullptr;
            slot.state.store(ClipSlot::Empty);
        }
    }

    pluginHost.getEngine().setBpm(snap.bpm);
    bpmSlider.setValue(snap.bpm, juce::dontSendNotification);

    // Restore clips
    for (auto& cd : snap.clips)
    {
        auto* cp = pluginHost.getTrack(cd.trackIndex).clipPlayer;
        if (cp == nullptr) continue;

        auto& slot = cp->getSlot(cd.slotIndex);
        slot.clip = std::make_unique<MidiClip>();
        slot.clip->lengthInBeats = cd.lengthInBeats;
        slot.clip->timelinePosition = cd.timelinePosition;

        for (int e = 0; e < cd.events.getNumEvents(); ++e)
            slot.clip->events.addEvent(cd.events.getEventPointer(e)->message);
        slot.clip->events.updateMatchedPairs();

        slot.state.store(ClipSlot::Playing);
    }

    updateTrackDisplay();
    if (timelineComponent) timelineComponent->repaint();
}

void MainComponent::saveProject()
{
    auto chooser = std::make_shared<juce::FileChooser>("Save Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.seqproj");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        auto xml = std::make_unique<juce::XmlElement>("SequencerProject");
        xml->setAttribute("bpm", pluginHost.getEngine().getBpm());

        for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
        {
            auto& track = pluginHost.getTrack(t);

            auto* trackXml = xml->createNewChildElement("Track");
            trackXml->setAttribute("index", t);

            if (track.gainProcessor)
            {
                trackXml->setAttribute("volume", static_cast<double>(track.gainProcessor->volume.load()));
                trackXml->setAttribute("pan", static_cast<double>(track.gainProcessor->pan.load()));
                trackXml->setAttribute("muted", track.gainProcessor->muted.load());
                trackXml->setAttribute("soloed", track.gainProcessor->soloed.load());
            }

            if (track.plugin)
                trackXml->setAttribute("pluginName", track.plugin->getName());

            auto* cp = track.clipPlayer;
            if (cp == nullptr) continue;

            for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
            {
                auto& slot = cp->getSlot(s);
                if (slot.clip == nullptr || !slot.hasContent()) continue;

                auto* clipXml = trackXml->createNewChildElement("Clip");
                clipXml->setAttribute("slot", s);
                clipXml->setAttribute("length", slot.clip->lengthInBeats);
                clipXml->setAttribute("position", slot.clip->timelinePosition);

                for (int e = 0; e < slot.clip->events.getNumEvents(); ++e)
                {
                    auto* event = slot.clip->events.getEventPointer(e);
                    auto* noteXml = clipXml->createNewChildElement("Event");
                    noteXml->setAttribute("time", event->message.getTimeStamp());
                    noteXml->setAttribute("data", juce::String::toHexString(
                        event->message.getRawData(), event->message.getRawDataSize()));
                }
            }
        }

        xml->writeTo(file);
        statusLabel.setText("Saved: " + file.getFileName(), juce::dontSendNotification);
    });
}

void MainComponent::loadProject()
{
    auto chooser = std::make_shared<juce::FileChooser>("Load Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.seqproj");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;

        auto xml = juce::parseXML(file);

        if (xml == nullptr || !xml->hasTagName("SequencerProject"))
        {
            statusLabel.setText("Invalid project file", juce::dontSendNotification);
            return;
        }

        // Clear all clips first
        for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
        {
            auto* cp = pluginHost.getTrack(t).clipPlayer;
            if (cp == nullptr) continue;
            for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
            {
                cp->getSlot(s).clip = nullptr;
                cp->getSlot(s).state.store(ClipSlot::Empty);
            }
        }

        double bpm = xml->getDoubleAttribute("bpm", 120.0);
        pluginHost.getEngine().setBpm(bpm);
        bpmSlider.setValue(bpm, juce::dontSendNotification);

        for (auto* trackXml : xml->getChildWithTagNameIterator("Track"))
        {
            int t = trackXml->getIntAttribute("index", -1);
            if (t < 0 || t >= PluginHost::NUM_TRACKS) continue;

            auto& track = pluginHost.getTrack(t);

            if (track.gainProcessor)
            {
                track.gainProcessor->volume.store(static_cast<float>(trackXml->getDoubleAttribute("volume", 0.8)));
                track.gainProcessor->pan.store(static_cast<float>(trackXml->getDoubleAttribute("pan", 0.0)));
                track.gainProcessor->muted.store(trackXml->getBoolAttribute("muted", false));
                track.gainProcessor->soloed.store(trackXml->getBoolAttribute("soloed", false));
            }

            auto* cp = track.clipPlayer;
            if (cp == nullptr) continue;

            for (auto* clipXml : trackXml->getChildWithTagNameIterator("Clip"))
            {
                int s = clipXml->getIntAttribute("slot", -1);
                if (s < 0 || s >= ClipPlayerNode::NUM_SLOTS) continue;

                auto& slot = cp->getSlot(s);
                slot.clip = std::make_unique<MidiClip>();
                slot.clip->lengthInBeats = clipXml->getDoubleAttribute("length", 4.0);
                slot.clip->timelinePosition = clipXml->getDoubleAttribute("position", 0.0);

                for (auto* noteXml : clipXml->getChildWithTagNameIterator("Event"))
                {
                    double time = noteXml->getDoubleAttribute("time", 0.0);
                    auto hexData = noteXml->getStringAttribute("data");

                    juce::MemoryBlock mb;
                    mb.loadFromHexString(hexData);

                    if (mb.getSize() > 0)
                    {
                        auto msg = juce::MidiMessage(mb.getData(), static_cast<int>(mb.getSize()));
                        msg.setTimeStamp(time);
                        slot.clip->events.addEvent(msg);
                    }
                }

                slot.clip->events.updateMatchedPairs();
                slot.state.store(ClipSlot::Playing);
            }
        }

        updateTrackDisplay();
        if (timelineComponent) timelineComponent->repaint();
        statusLabel.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);

        // Take snapshot for undo
        takeSnapshot();
    });
}

// ── Layout ───────────────────────────────────────────────────────────────────

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // Check if click is in the mini mixer area
    if (!miniMixerBounds.isEmpty() && miniMixerBounds.contains(e.x, e.y))
    {
        int barWidth = juce::jmax(1, (miniMixerBounds.getWidth() - 2) / 16);
        int trackIdx = (e.x - miniMixerBounds.getX()) / barWidth;
        if (trackIdx >= 0 && trackIdx < PluginHost::NUM_TRACKS)
        {
            selectTrack(trackIdx);
            repaint(miniMixerBounds);
        }
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colour(0xff333333));
    g.drawHorizontalLine(50, 0, static_cast<float>(getWidth()));
    drawMiniMixer(g);
}

void MainComponent::drawMiniMixer(juce::Graphics& g)
{
    if (miniMixerBounds.isEmpty()) return;

    auto area = miniMixerBounds;
    int barWidth = juce::jmax(1, (area.getWidth()) / 16);
    int barH = area.getHeight();

    // Background
    g.setColour(juce::Colour(0xff1e1e1e));
    g.fillRect(area);

    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto& track = pluginHost.getTrack(t);
        int x = area.getX() + t * barWidth;
        float vol = 0.0f;

        if (track.gainProcessor)
        {
            vol = track.gainProcessor->volume.load();
            if (track.gainProcessor->muted.load()) vol = 0.0f;
        }

        int filledW = static_cast<int>(vol * (barWidth - 2));

        bool isSelected = (t == selectedTrackIndex);
        bool hasPlugin = (track.plugin != nullptr);
        bool isArmed = track.clipPlayer && track.clipPlayer->armed.load();

        // Background per track
        g.setColour(juce::Colour(0xff2a2a2a));
        g.fillRect(x + 1, area.getY() + 1, barWidth - 2, barH - 2);

        // Level fill
        if (isArmed)
            g.setColour(juce::Colours::red.darker());
        else if (isSelected)
            g.setColour(juce::Colour(0xff4488cc));
        else if (hasPlugin)
            g.setColour(juce::Colour(0xff448844));
        else
            g.setColour(juce::Colour(0xff333333));

        g.fillRect(x + 1, area.getY() + 1, filledW, barH - 2);

        // Selected border
        if (isSelected)
        {
            g.setColour(juce::Colours::white);
            g.drawRect(x, area.getY(), barWidth, barH, 1);
        }

        // Track number
        g.setColour(juce::Colour(0xffcccccc));
        g.setFont(10.0f);
        g.drawText(juce::String(t + 1), x, area.getY(), barWidth, barH, juce::Justification::centred);
    }
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
    topBar.removeFromLeft(4);
    countInButton.setBounds(topBar.removeFromLeft(75));
    topBar.removeFromLeft(8);

    beatLabel.setBounds(topBar.removeFromRight(90));
    bpmSlider.setBounds(topBar);

    // ── Bottom Bar ──
    auto bottomBar = area.removeFromBottom(bottomBarH).reduced(8, 6);

    scrollLeftButton.setBounds(bottomBar.removeFromLeft(40));
    bottomBar.removeFromLeft(2);
    scrollRightButton.setBounds(bottomBar.removeFromLeft(40));
    bottomBar.removeFromLeft(4);
    zoomOutButton.setBounds(bottomBar.removeFromLeft(60));
    bottomBar.removeFromLeft(2);
    zoomInButton.setBounds(bottomBar.removeFromLeft(60));
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
    toolbar.removeFromLeft(4);
    quantizeButton.setBounds(toolbar.removeFromLeft(80));
    toolbar.removeFromLeft(8);
    gridSelector.setBounds(toolbar.removeFromLeft(70));

    // ── Right Panel ──
    auto rightPanel = area.removeFromRight(rightPanelW).reduced(8, 4);

    pluginSelector.setBounds(rightPanel.removeFromTop(30));
    rightPanel.removeFromTop(4);
    openEditorButton.setBounds(rightPanel.removeFromTop(32));
    rightPanel.removeFromTop(4);
    testNoteButton.setBounds(rightPanel.removeFromTop(32));
    rightPanel.removeFromTop(8);
    midiInputSelector.setBounds(rightPanel.removeFromTop(30));
    rightPanel.removeFromTop(4);
    audioSettingsButton.setBounds(rightPanel.removeFromTop(32));
    rightPanel.removeFromTop(12);

    // Volume fader + Pan knob side by side
    auto mixArea = rightPanel.removeFromTop(juce::jmin(180, rightPanel.getHeight() / 2));
    auto volArea = mixArea.removeFromLeft(mixArea.getWidth() / 2);
    auto panArea = mixArea;

    volumeLabel.setBounds(volArea.removeFromTop(18));
    volumeSlider.setBounds(volArea);

    panLabel.setBounds(panArea.removeFromTop(18));
    panSlider.setBounds(panArea.reduced(10, 0));

    rightPanel.removeFromTop(8);

    // Track info
    trackInfoLabel.setBounds(rightPanel.removeFromTop(70));
    rightPanel.removeFromTop(8);

    // Save/Load
    auto saveLoadRow = rightPanel.removeFromTop(32);
    saveButton.setBounds(saveLoadRow.removeFromLeft(saveLoadRow.getWidth() / 2 - 2));
    saveLoadRow.removeFromLeft(4);
    loadButton.setBounds(saveLoadRow);
    rightPanel.removeFromTop(4);

    // Undo/Redo
    auto undoRow = rightPanel.removeFromTop(32);
    undoButton.setBounds(undoRow.removeFromLeft(undoRow.getWidth() / 2 - 2));
    undoRow.removeFromLeft(4);
    redoButton.setBounds(undoRow);
    rightPanel.removeFromTop(8);

    // Plugin parameter knobs in remaining right panel space
    if (paramSliders.size() > 0)
    {
        int knobSize = juce::jmin(70, (rightPanel.getWidth() - 8) / 3);
        for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
        {
            int col = i % 3;
            int row = i / 3;
            int kx = rightPanel.getX() + col * (knobSize + 4);
            int ky = rightPanel.getY() + row * (knobSize + 18);

            paramLabels[i]->setBounds(kx, ky, knobSize, 14);
            paramSliders[i]->setBounds(kx, ky + 14, knobSize, knobSize);
        }
    }

    // ── Mini mixer as horizontal strip above timeline ──
    area.reduce(2, 2);
    miniMixerBounds = area.removeFromTop(30).toNearestInt();
    area.removeFromTop(2);

    // ── Timeline fills the rest ──
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

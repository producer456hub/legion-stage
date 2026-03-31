#include "MainComponent.h"

MainComponent::MainComponent()
{
    themeManager.setTheme(ThemeManager::Keystage, this);

    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty())
        DBG("Audio device init error: " + result);

    audioPlayer.setProcessor(&pluginHost);
    deviceManager.addAudioCallback(&audioPlayer);

    addAndMakeVisible(spectrumDisplay);
    pluginHost.spectrumDisplay = &spectrumDisplay;

    addAndMakeVisible(lissajousDisplay);

    addAndMakeVisible(gforceDisplay);
    gforceDisplay.setVisible(false);
    pluginHost.gforceDisplay = &gforceDisplay;

    addAndMakeVisible(geissDisplay);
    geissDisplay.setVisible(false);
    pluginHost.geissDisplay = &geissDisplay;

    addAndMakeVisible(projectMDisplay);
    projectMDisplay.setVisible(false);
    pluginHost.projectMDisplay = &projectMDisplay;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        pluginHost.setAudioParams(device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples());
        pluginHost.prepareToPlay(device->getCurrentSampleRate(),
                                 device->getCurrentBufferSizeSamples());
    }

    // ── Top Bar: Transport + Track Select ──
    addAndMakeVisible(midiLearnButton);
    midiLearnButton.setClickingTogglesState(true);
    midiLearnButton.onClick = [this] {
        midiLearnActive = midiLearnButton.getToggleState();
        if (midiLearnActive)
            statusLabel.setText("MIDI Learn: click a control, then move a CC", juce::dontSendNotification);
        else
        {
            midiLearnTarget = MidiTarget::None;
            statusLabel.setText("MIDI Learn off", juce::dontSendNotification);
        }
    };

    addAndMakeVisible(trackNameLabel);
    trackNameLabel.setJustificationType(juce::Justification::centred);
    trackNameLabel.setFont(juce::Font("Consolas", 16.0f, juce::Font::bold));
    trackNameLabel.setColour(juce::Label::textColourId, juce::Colour(themeManager.getColors().amber));

    addAndMakeVisible(recordButton);
    recordButton.setClickingTogglesState(true);
    recordButton.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Record); return; }
        pluginHost.getEngine().toggleRecord();
    };

    addAndMakeVisible(playButton);
    playButton.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Play); return; }
        auto& eng = pluginHost.getEngine();
        if (eng.isPlaying())
        {
            eng.stop();
            for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
            {
                auto* cp = pluginHost.getTrack(t).clipPlayer;
                if (cp) cp->sendAllNotesOff.store(true);
            }
            if (timelineComponent) timelineComponent->repaint();
        }
        else
        {
            eng.play();
        }
    };

    addAndMakeVisible(stopButton);
    stopButton.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Stop); return; }
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
                if (cp)
                {
                    cp->stopAllSlots();
                    cp->sendAllNotesOff.store(true);
                }
            }
        }
        if (timelineComponent) timelineComponent->repaint();
    };

    addAndMakeVisible(metronomeButton);
    metronomeButton.setClickingTogglesState(true);
    metronomeButton.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Metronome); return; }
        pluginHost.getEngine().toggleMetronome();
    };

    bpmDownButton.setVisible(false);
    bpmDownButton.onClick = [this] {
        double bpm = juce::jmax(20.0, pluginHost.getEngine().getBpm() - 1.0);
        pluginHost.getEngine().setBpm(bpm);
        bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);
    };

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("120 BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centred);

    bpmUpButton.setVisible(false);
    bpmUpButton.onClick = [this] {
        double bpm = juce::jmin(300.0, pluginHost.getEngine().getBpm() + 1.0);
        pluginHost.getEngine().setBpm(bpm);
        bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);
    };

    addAndMakeVisible(bpmArrowButton);
    bpmArrowButton.onUp = [this] { bpmUpButton.triggerClick(); };
    bpmArrowButton.onDown = [this] { bpmDownButton.triggerClick(); };

    addAndMakeVisible(beatPanel);

    addAndMakeVisible(tapTempoButton);
    tapTempoButton.onClick = [this] {
        double now = juce::Time::getMillisecondCounterHiRes();
        // Reset if last tap was more than 2 seconds ago
        if (tapTimes.size() > 0 && (now - tapTimes.getLast()) > 2000.0)
            tapTimes.clear();
        tapTimes.add(now);
        if (tapTimes.size() > maxTaps)
            tapTimes.remove(0);
        if (tapTimes.size() >= 2)
        {
            double totalInterval = tapTimes.getLast() - tapTimes.getFirst();
            double avgInterval = totalInterval / (tapTimes.size() - 1);
            double bpm = juce::jlimit(20.0, 300.0, 60000.0 / avgInterval);
            pluginHost.getEngine().setBpm(bpm);
            bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);
        }
    };

    // ── Edit Toolbar ──
    addAndMakeVisible(newClipButton);
    newClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->createClipAtPlayhead();
    };

    addAndMakeVisible(deleteClipButton);
    deleteClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->deleteSelected();
    };

    addAndMakeVisible(duplicateClipButton);
    duplicateClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->duplicateSelected();
    };

    addAndMakeVisible(splitClipButton);
    splitClipButton.onClick = [this] {
        takeSnapshot();
        if (timelineComponent) timelineComponent->splitSelected();
    };

    addAndMakeVisible(quantizeButton);
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
    countInButton.onClick = [this] { pluginHost.getEngine().toggleCountIn(); };

    addAndMakeVisible(loopButton);
    loopButton.setClickingTogglesState(true);
    loopButton.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Loop); return; }
        pluginHost.getEngine().toggleLoop();
    };

    addAndMakeVisible(panicButton);
    panicButton.onClick = [this] {
        // Flag all tracks for hard note-off on the audio thread (thread-safe)
        for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
        {
            auto& track = pluginHost.getTrack(t);
            if (track.clipPlayer)
                track.clipPlayer->sendAllNotesOff.store(true);
        }
        statusLabel.setText("MIDI Panic — all notes off", juce::dontSendNotification);
        panicAnimEndTime = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
    };

    addAndMakeVisible(zoomInButton);
    zoomInButton.onClick = [this] { if (timelineComponent) timelineComponent->zoomIn(); };

    addAndMakeVisible(zoomOutButton);
    zoomOutButton.onClick = [this] { if (timelineComponent) timelineComponent->zoomOut(); };

    addAndMakeVisible(scrollLeftButton);
    scrollLeftButton.onClick = [this] { if (timelineComponent) timelineComponent->scrollLeft(); };

    addAndMakeVisible(scrollRightButton);
    scrollRightButton.onClick = [this] { if (timelineComponent) timelineComponent->scrollRight(); };

    addChildComponent(editClipButton);  // hidden — double-tap clip to edit
    editClipButton.setVisible(false);
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

    addAndMakeVisible(midiRefreshButton);
    midiRefreshButton.onClick = [this] { scanMidiDevices(); };

    // FX insert slots
    for (int i = 0; i < NUM_FX_SLOTS; ++i)
    {
        auto* selector = new juce::ComboBox();
        selector->addItem("FX " + juce::String(i + 1) + ": Empty", 1);
        selector->setSelectedId(1, juce::dontSendNotification);
        int slotIdx = i;
        selector->onChange = [this, slotIdx] { loadFxPlugin(slotIdx); };
        addAndMakeVisible(selector);
        fxSelectors.add(selector);

        auto* edBtn = new juce::TextButton("E");
        edBtn->onClick = [this, slotIdx] { openFxEditor(slotIdx); };
        addAndMakeVisible(edBtn);
        fxEditorButtons.add(edBtn);
    }

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Audio Settings...");
        menu.addItem(2, "Check for Updates...");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&audioSettingsButton),
            [this](int result) {
                if (result == 1) showAudioSettings();
                else if (result == 2) showSettingsMenu();
            });
    };

    // VIS button cycles: off -> fullscreen w/ controls -> projector (no chrome) -> off
    addAndMakeVisible(fullscreenButton);
    fullscreenButton.onClick = [this] {
        if (!visualizerFullScreen)
        {
            // Off -> fullscreen with controls
            visualizerFullScreen = true;
            projectorMode = false;
        }
        else if (!projectorMode)
        {
            // Fullscreen -> projector mode
            projectorMode = true;
        }
        else
        {
            // Projector -> off
            visualizerFullScreen = false;
            projectorMode = false;
        }
        fullscreenButton.setToggleState(visualizerFullScreen, juce::dontSendNotification);
        projectorButton.setToggleState(projectorMode, juce::dontSendNotification);
        resized();
        repaint();
    };

    // Double-click any visualizer to toggle fullscreen
    auto visDoubleClickHandler = [this] { fullscreenButton.triggerClick(); };
    spectrumDisplay.onDoubleClick = visDoubleClickHandler;
    lissajousDisplay.onDoubleClick = visDoubleClickHandler;
    gforceDisplay.onDoubleClick = visDoubleClickHandler;
    geissDisplay.onDoubleClick = visDoubleClickHandler;
    projectMDisplay.onDoubleClick = visDoubleClickHandler;

    addAndMakeVisible(visSelector);
    visSelector.addItem("Spectrum", 1);
    visSelector.addItem("Lissajous", 2);
    visSelector.addItem("G-Force", 3);
    visSelector.addItem("Geiss", 4);
    visSelector.addItem("MilkDrop", 5);
    visSelector.setSelectedId(2, juce::dontSendNotification);  // Lissajous
    currentVisMode = 1;
    visSelector.onChange = [this] {
        currentVisMode = visSelector.getSelectedId() - 1;
        resized();
        repaint();
    };

    addAndMakeVisible(visExitButton);
    visExitButton.setVisible(false);
    visExitButton.onClick = [this] {
        visualizerFullScreen = false;
        projectorMode = false;
        fullscreenButton.setToggleState(false, juce::dontSendNotification);
        projectorButton.setToggleState(false, juce::dontSendNotification);
        resized();
        repaint();
        grabKeyboardFocus();
    };

    // projectorButton kept for internal state but hidden from toolbar
    projectorButton.setClickingTogglesState(true);
    projectorButton.setVisible(false);

    // ── Geiss control buttons ──
    addAndMakeVisible(geissWaveBtn);
    geissWaveBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissWaveform); return; }
        geissDisplay.cycleWaveform();
    };

    addAndMakeVisible(geissPaletteBtn);
    geissPaletteBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissPalette); return; }
        geissDisplay.cyclePalette();
    };

    addAndMakeVisible(geissSceneBtn);
    geissSceneBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissScene); return; }
        geissDisplay.newRandomScene();
    };

    addAndMakeVisible(geissWaveUpBtn);
    geissWaveUpBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissWaveScale); return; }
        geissDisplay.waveScaleUp();
    };

    addAndMakeVisible(geissWaveDownBtn);
    geissWaveDownBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissWaveScale); return; }
        geissDisplay.waveScaleDown();
    };

    addAndMakeVisible(geissWarpLockBtn);
    geissWarpLockBtn.setClickingTogglesState(true);
    geissWarpLockBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissWarpLock); return; }
        geissDisplay.toggleWarpLock();
    };

    addAndMakeVisible(geissPalLockBtn);
    geissPalLockBtn.setClickingTogglesState(true);
    geissPalLockBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GeissPaletteLock); return; }
        geissDisplay.togglePaletteLock();
    };

    addAndMakeVisible(geissSpeedSelector);
    geissSpeedSelector.addItem("0.25x", 1);
    geissSpeedSelector.addItem("0.5x", 2);
    geissSpeedSelector.addItem("1x", 3);
    geissSpeedSelector.addItem("2x", 4);
    geissSpeedSelector.addItem("4x", 5);
    geissSpeedSelector.setSelectedId(3, juce::dontSendNotification);
    geissSpeedSelector.onChange = [this] {
        float speeds[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        int idx = geissSpeedSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < 5) geissDisplay.setSpeed(speeds[idx]);
    };

    addAndMakeVisible(geissAutoPilotBtn);
    geissAutoPilotBtn.setClickingTogglesState(true);
    geissAutoPilotBtn.onClick = [this] {
        geissDisplay.toggleAutoPilot();
    };

    addAndMakeVisible(geissBgBtn);
    geissBgBtn.setClickingTogglesState(true);
    geissBgBtn.onClick = [this] { geissDisplay.setBlackBg(geissBgBtn.getToggleState()); };

    // ── ProjectM (MilkDrop) control buttons ──
    addAndMakeVisible(pmNextBtn);
    pmNextBtn.onClick = [this] { projectMDisplay.nextScene(); };

    addAndMakeVisible(pmPrevBtn);
    pmPrevBtn.onClick = [this] { projectMDisplay.prevScene(); };

    addAndMakeVisible(pmRandBtn);
    pmRandBtn.onClick = [this] { projectMDisplay.randomScene(); };

    addAndMakeVisible(pmLockBtn);
    pmLockBtn.setClickingTogglesState(true);
    pmLockBtn.onClick = [this] { projectMDisplay.toggleLock(); };

    addAndMakeVisible(pmBgBtn);
    pmBgBtn.setClickingTogglesState(true);
    pmBgBtn.onClick = [this] { projectMDisplay.setBlackBg(pmBgBtn.getToggleState()); };

    // ── G-Force control buttons ──
    addAndMakeVisible(gfRibbonUpBtn);
    gfRibbonUpBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GForceRibbons); return; }
        gforceDisplay.moreRibbons();
    };
    addAndMakeVisible(gfRibbonDownBtn);
    gfRibbonDownBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GForceRibbons); return; }
        gforceDisplay.fewerRibbons();
    };
    addAndMakeVisible(gfTrailBtn);
    gfTrailBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::GForceTrail); return; }
        gforceDisplay.cycleTrail();
    };
    addAndMakeVisible(gfSpeedSelector);
    gfSpeedSelector.addItem("0.25x", 1);
    gfSpeedSelector.addItem("0.5x", 2);
    gfSpeedSelector.addItem("1x", 3);
    gfSpeedSelector.addItem("2x", 4);
    gfSpeedSelector.addItem("4x", 5);
    gfSpeedSelector.setSelectedId(3, juce::dontSendNotification);
    gfSpeedSelector.onChange = [this] {
        float speeds[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        int idx = gfSpeedSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < 5) gforceDisplay.setSpeed(speeds[idx]);
    };

    // ── Spectrum control buttons ──
    addAndMakeVisible(specDecayBtn);
    specDecayBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::SpecDecay); return; }
        spectrumDisplay.cycleDecay();
    };
    addAndMakeVisible(specSensUpBtn);
    specSensUpBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::SpecSensitivity); return; }
        spectrumDisplay.sensitivityUp();
    };
    addAndMakeVisible(specSensDownBtn);
    specSensDownBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::SpecSensitivity); return; }
        spectrumDisplay.sensitivityDown();
    };

    // ── Lissajous control buttons ──
    addAndMakeVisible(lissZoomInBtn);
    lissZoomInBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::LissZoom); return; }
        lissajousDisplay.zoomIn();
    };
    addAndMakeVisible(lissZoomOutBtn);
    lissZoomOutBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::LissZoom); return; }
        lissajousDisplay.zoomOut();
    };
    addAndMakeVisible(lissDotsBtn);
    lissDotsBtn.onClick = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::LissDots); return; }
        lissajousDisplay.cycleDots();
    };

    setVisControlsVisible();

    addAndMakeVisible(midi2Button);
    midi2Button.setClickingTogglesState(true);
    midi2Button.onClick = [this] {
        midi2Enabled = midi2Button.getToggleState();
        if (midi2Enabled)
        {
            auto& track = pluginHost.getTrack(selectedTrackIndex);
            midi2Handler.setPlugin(track.plugin);

            // Find matching MIDI output for the selected input
            auto midiOutputs = juce::MidiOutput::getAvailableDevices();
            juce::String outputId;

            // Try to find output with matching name
            for (auto& out : midiOutputs)
            {
                for (auto& in : midiDevices)
                {
                    if (in.identifier == currentMidiDeviceId && out.name == in.name)
                    {
                        outputId = out.identifier;
                        break;
                    }
                }
                if (outputId.isNotEmpty()) break;
            }

            // Fallback: try partial name match
            if (outputId.isEmpty())
            {
                for (auto& in : midiDevices)
                {
                    if (in.identifier == currentMidiDeviceId)
                    {
                        for (auto& out : midiOutputs)
                        {
                            if (out.name.containsIgnoreCase("keystage") ||
                                in.name.containsIgnoreCase(out.name.substring(0, 8)))
                            {
                                outputId = out.identifier;
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            // Send Discovery broadcast
            midi2Handler.sendDiscovery();

            // Open MIDI output and keep it open
            auto useId = outputId.isNotEmpty() ? outputId : currentMidiDeviceId;
            midiOutput = juce::MidiOutput::openDevice(useId);
            midiOutputId = useId;

            auto& outgoing = midi2Handler.getOutgoing();
            if (!outgoing.isEmpty() && midiOutput)
            {
                for (const auto metadata : outgoing)
                    midiOutput->sendMessageNow(metadata.getMessage());

                statusLabel.setText("MIDI 2.0: Discovery sent via " + midiOutput->getName(),
                    juce::dontSendNotification);
            }
            else if (!midiOutput)
            {
                statusLabel.setText("MIDI 2.0: No MIDI output found!", juce::dontSendNotification);
            }
            midi2Handler.clearOutgoing();
        }
        else
        {
            midiOutput = nullptr;
            statusLabel.setText("MIDI 2.0 disabled", juce::dontSendNotification);
        }
    };

    testNoteButton.setVisible(false);

    // ── MIDI Capture (always listening — press to retrieve) ──
    addAndMakeVisible(captureButton);
    captureButton.setEnabled(false);  // greyed out until data exists
    captureButton.onClick = [this] {
        if (captureBuffer.hasContent())
            retrieveCapture();
    };

    // ── Gamepad (Legion Go) ──
    addAndMakeVisible(goButton);
    goButton.setClickingTogglesState(true);
    goButton.onClick = [this] {
        bool on = goButton.getToggleState();
        gamepadHandler.setEnabled(on);
        gamepadOverlay.setVisible(on);
        if (on)
        {
            gamepadOverlay.setMode(gamepadHandler.getMode());
            gamepadOverlay.setConnected(gamepadHandler.isConnected());
        }
        repaint();
    };

    addChildComponent(gamepadOverlay);  // hidden by default

    // Gamepad note callbacks — same path as touch piano
    gamepadHandler.onNoteOn = [this](int note, float velocity) {
        auto msg = juce::MidiMessage::noteOn(1, note, velocity);
        msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        pluginHost.getMidiCollector().addMessageToQueue(msg);
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.clipPlayer && track.clipPlayer->armed.load())
            captureBuffer.addMessage(msg, selectedTrackIndex);
    };
    gamepadHandler.onNoteOff = [this](int note) {
        sendNoteOff(note);
    };

    // Gamepad virtual MIDI — feeds into MIDI learn pipeline
    gamepadHandler.onMidiMessage = [this](const juce::MidiMessage& msg) {
        if (midiLearnActive && midiLearnTarget != MidiTarget::None && msg.isController())
        {
            int ch  = msg.getChannel();
            int cc  = msg.getControllerNumber();
            int val = msg.getControllerValue();
            if (val > 10)  // threshold to avoid accidental learn from noise
                processMidiLearnCC(ch, cc, val);
            return;
        }
        // Apply existing gamepad CC mappings
        if (msg.isController())
        {
            for (auto& mapping : midiMappings)
            {
                if (mapping.channel == msg.getChannel() && mapping.ccNumber == msg.getControllerNumber())
                    applyMidiCC(mapping, msg.getControllerValue());
            }
        }
        // Pass pitch bend and mod wheel directly to the plugin
        if (msg.isPitchWheel() || (msg.isController() && msg.getControllerNumber() == 1))
            pluginHost.getMidiCollector().addMessageToQueue(msg);
    };

    // Navigation callbacks
    gamepadHandler.onTrackSelect = [this](int dir) {
        selectTrack(juce::jlimit(0, PluginHost::NUM_TRACKS - 1, selectedTrackIndex + dir));
    };
    gamepadHandler.onScroll = [this](float dx, float /*dy*/) {
        if (timelineComponent)
        {
            if (dx < -0.2f) timelineComponent->scrollLeft();
            else if (dx > 0.2f) timelineComponent->scrollRight();
        }
    };
    gamepadHandler.onZoom = [this](float factor) {
        if (timelineComponent)
        {
            if (factor > 0) timelineComponent->zoomIn();
            else if (factor < 0) timelineComponent->zoomOut();
        }
    };

    // Transport callbacks
    gamepadHandler.onPlay = [this] {
        pluginHost.getEngine().play();
    };
    gamepadHandler.onStop = [this] {
        pluginHost.getEngine().stop();
    };
    gamepadHandler.onRecord = [this] {
        pluginHost.getEngine().toggleRecord();
    };
    gamepadHandler.onUndo = [this] {
        undoButton.triggerClick();
    };
    gamepadHandler.onRedo = [this] {
        redoButton.triggerClick();
    };

    // Right controller: visualizer callbacks
    gamepadHandler.onVisCycleType = [this] {
        currentVisMode = (currentVisMode + 1) % 5;
        visSelector.setSelectedId(currentVisMode + 1, juce::sendNotification);
    };
    gamepadHandler.onVisToggleFullscreen = [this] {
        fullscreenButton.triggerClick();  // uses the same cycle logic
    };
    // Geiss
    gamepadHandler.onGeissCycleWave    = [this] { geissDisplay.cycleWaveform(); };
    gamepadHandler.onGeissCyclePalette = [this] { geissDisplay.cyclePalette(); };
    gamepadHandler.onGeissNewScene     = [this] { geissDisplay.newRandomScene(); };
    gamepadHandler.onGeissWaveScale    = [this](float delta) {
        if (delta > 0) geissDisplay.waveScaleUp();
        else           geissDisplay.waveScaleDown();
    };
    gamepadHandler.onGeissToggleWarp      = [this] { geissDisplay.toggleWarpLock(); };
    gamepadHandler.onGeissToggleAutoPilot = [this] { geissDisplay.toggleAutoPilot(); };
    // ProjectM
    gamepadHandler.onPMNext       = [this] { projectMDisplay.nextScene(); };
    gamepadHandler.onPMPrev       = [this] { projectMDisplay.prevScene(); };
    gamepadHandler.onPMRandom     = [this] { projectMDisplay.randomScene(); };
    gamepadHandler.onPMToggleLock = [this] { projectMDisplay.toggleLock(); };
    // G-Force
    gamepadHandler.onGFMoreRibbons  = [this] { gforceDisplay.moreRibbons(); };
    gamepadHandler.onGFFewerRibbons = [this] { gforceDisplay.fewerRibbons(); };
    gamepadHandler.onGFCycleTrail   = [this] { gforceDisplay.cycleTrail(); };
    // Spectrum
    gamepadHandler.onSpecCycleDecay = [this] { spectrumDisplay.cycleDecay(); };
    gamepadHandler.onSpecSensUp     = [this] { spectrumDisplay.sensitivityUp(); };
    gamepadHandler.onSpecSensDown   = [this] { spectrumDisplay.sensitivityDown(); };
    // Lissajous
    gamepadHandler.onLissZoomIn    = [this] { lissajousDisplay.zoomIn(); };
    gamepadHandler.onLissZoomOut   = [this] { lissajousDisplay.zoomOut(); };
    gamepadHandler.onLissCycleDots = [this] { lissajousDisplay.cycleDots(); };

    // ── Touch Piano ──
    addChildComponent(touchPiano);  // hidden by default
    touchPiano.onNote = [this](int note, bool isOn) {
        if (isOn) sendNoteOn(note);
        else      sendNoteOff(note);
    };

    // ── Mixer ──
    mixerComponent = std::make_unique<MixerComponent>(pluginHost);
    mixerComponent->onTrackSelected = [this](int track) { selectTrack(track); };
    addChildComponent(*mixerComponent);  // hidden by default

    addAndMakeVisible(mixerButton);
    mixerButton.setClickingTogglesState(true);
    mixerButton.onClick = [this] {
        mixerVisible = mixerButton.getToggleState();
        mixerComponent->setVisible(mixerVisible);
        resized();
        repaint();
    };

    addAndMakeVisible(pianoToggleButton);
    pianoToggleButton.setClickingTogglesState(true);
    pianoToggleButton.onClick = [this] {
        touchPianoVisible = pianoToggleButton.getToggleState();
        touchPiano.setVisible(touchPianoVisible);
        resized();
        repaint();
    };

    addAndMakeVisible(pianoOctUpButton);
    pianoOctUpButton.onClick = [this] { touchPiano.octaveUp(); };

    addAndMakeVisible(pianoOctDownButton);
    pianoOctDownButton.onClick = [this] { touchPiano.octaveDown(); };

    // ── Bottom Bar: Mix Controls ──
    addAndMakeVisible(volumeSlider);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.8, juce::dontSendNotification);
    volumeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    volumeSlider.onValueChange = [this] {
        if (midiLearnActive) { startMidiLearn(MidiTarget::Volume); return; }
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
        if (midiLearnActive) { startMidiLearn(MidiTarget::Pan); return; }
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.gainProcessor) track.gainProcessor->pan.store(static_cast<float>(panSlider.getValue()));
    };

    addAndMakeVisible(panLabel);
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setFont(juce::Font(12.0f));

    addAndMakeVisible(saveButton);
    saveButton.onClick = [this] { saveProject(); };

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { loadProject(); };

    addAndMakeVisible(undoButton);
    undoButton.onClick = [this] {
        if (undoIndex > 0)
        {
            undoIndex--;
            restoreSnapshot(undoHistory[undoIndex]);
        }
    };

    addAndMakeVisible(redoButton);
    redoButton.onClick = [this] {
        if (undoIndex < undoHistory.size() - 1)
        {
            undoIndex++;
            restoreSnapshot(undoHistory[undoIndex]);
        }
    };

    // ── Theme Selector ──
    addAndMakeVisible(themeSelector);
    for (int i = 0; i < ThemeManager::NumThemes; ++i)
        themeSelector.addItem(ThemeManager::getThemeName(static_cast<ThemeManager::Theme>(i)), i + 1);
    themeSelector.setSelectedId(ThemeManager::Keystage + 1, juce::dontSendNotification);
    themeSelector.onChange = [this] {
        auto idx = themeSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < ThemeManager::NumThemes)
        {
            themeManager.setTheme(static_cast<ThemeManager::Theme>(idx), this);
            applyThemeToControls();
            resized();
        }
    };

    trackInfoLabel.setVisible(false);

    // Plugin parameter sliders
    for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
    {
        auto* slider = new juce::Slider();
        slider->setRange(0.0, 1.0, 0.001);
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, true, 60, 14);
        slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setEnabled(false);

        int paramIdx = i;
        slider->onValueChange = [this, slider, paramIdx] {
            if (midiLearnActive) {
                startMidiLearn(static_cast<MidiTarget>(static_cast<int>(MidiTarget::Param0) + paramIdx));
                return;
            }
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

    // Apply initial theme colors to all controls
    applyThemeToControls();

    // Force layout after all controls are set up (handles side panels etc.)
    juce::MessageManager::callAsync([this] { resized(); repaint(); });

    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    pluginHost.spectrumDisplay = nullptr;
    pluginHost.gforceDisplay = nullptr;
    pluginHost.geissDisplay = nullptr;
    pluginHost.projectMDisplay = nullptr;
    // Clear Lissajous pointer from all tracks
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto& track = pluginHost.getTrack(t);
        if (track.gainProcessor) track.gainProcessor->lissajousDisplay = nullptr;
    }
    setLookAndFeel(nullptr);  // clear before ThemeManager destructs
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

    {
        juce::String beatText;
        if (eng.isInCountIn())
        {
            int barsLeft = static_cast<int>(std::ceil(eng.getCountInBeatsRemaining() / 4.0));
            beatText = "Count: -" + juce::String(barsLeft);
        }
        else
        {
            double beat = eng.getPositionInBeats();
            beatText = "Beat: " + juce::String(beat, 1);
        }
        beatPanel.setLines(beatText, cachedStatusLine1, cachedStatusLine2);
    }

    // Flash record button hazard orange when armed
    if (recordButton.getToggleState())
    {
        bool flash = (juce::Time::currentTimeMillis() / 400) % 2 == 0;
        auto flashColor = flash ? juce::Colour(0xffdd6600) : juce::Colour(themeManager.getColors().redDark);
        recordButton.setColour(juce::TextButton::buttonColourId, flashColor);
        recordButton.setColour(juce::TextButton::buttonOnColourId, flashColor);
        recordButton.repaint();
    }
    else
    {
        recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(themeManager.getColors().redDark));
        recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(themeManager.getColors().red));
    }

    // Sync transport button toggle states for animated OLED icons
    playButton.setToggleState(eng.isPlaying(), juce::dontSendNotification);
    metronomeButton.setToggleState(eng.isMetronomeOn(), juce::dontSendNotification);
    loopButton.setToggleState(eng.isLoopEnabled(), juce::dontSendNotification);
    countInButton.setToggleState(eng.isCountInEnabled(), juce::dontSendNotification);

    // Only repaint OLED buttons during playback (animations active)
    if (eng.isPlaying())
    {
        playButton.repaint();
        stopButton.repaint();
        metronomeButton.repaint();
        loopButton.repaint();
        countInButton.repaint();
    }
    {
        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        panicButton.setToggleState(now < panicAnimEndTime, juce::dontSendNotification);
    }
    panicButton.repaint();

    // Capture button: greyed out when no data, active when data ready
    {
        bool hasData = captureBuffer.hasContent();
        captureButton.setEnabled(hasData);
    }

    // Update gamepad overlay
    if (gamepadHandler.isEnabled())
    {
        gamepadHandler.setVisMode(currentVisMode);
        gamepadOverlay.setMode(gamepadHandler.getMode());
        gamepadOverlay.setConnected(gamepadHandler.isConnected());
        gamepadOverlay.setVisMode(currentVisMode);
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
        closePluginEditor();
        updateTrackDisplay();
        updateStatusLabel();

        // Update plugin selector to show current track's plugin
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.plugin != nullptr)
        {
            juce::String pluginName = track.plugin->getName();
            bool found = false;
            for (int i = 0; i < pluginDescriptions.size(); ++i)
            {
                if (pluginDescriptions[i].name == pluginName)
                {
                    pluginSelector.setSelectedId(i + 2, juce::dontSendNotification);
                    found = true;
                    break;
                }
            }
            if (!found)
                pluginSelector.setSelectedId(1, juce::dontSendNotification);
        }
        else
        {
            pluginSelector.setSelectedId(1, juce::dontSendNotification);
        }
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

    // Show the currently loaded plugin in the selector, or reset to default
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.plugin != nullptr)
    {
        juce::String pluginName = track.plugin->getName();
        bool found = false;
        for (int i = 0; i < pluginDescriptions.size(); ++i)
        {
            if (pluginDescriptions[i].name == pluginName)
            {
                pluginSelector.setSelectedId(i + 2, juce::dontSendNotification);
                found = true;
                break;
            }
        }
        if (!found)
            pluginSelector.setSelectedId(1, juce::dontSendNotification);
    }
    else
    {
        pluginSelector.setSelectedId(1, juce::dontSendNotification);
    }
}

void MainComponent::updateTrackDisplay()
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);

    juce::String name = "Track " + juce::String(selectedTrackIndex + 1);
    if (track.plugin != nullptr)
        name += ": " + track.plugin->getName();
    trackNameLabel.setText(name, juce::dontSendNotification);

    openEditorButton.setEnabled(track.plugin != nullptr);
    updateFxDisplay();

    // Point Lissajous at the selected track's gain processor
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto& tr = pluginHost.getTrack(t);
        if (tr.gainProcessor)
            tr.gainProcessor->lissajousDisplay = (t == selectedTrackIndex) ? &lissajousDisplay : nullptr;
    }

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

    // Update MIDI 2.0 handler
    if (midi2Enabled)
        midi2Handler.setPlugin(track.plugin);

}

// ── Plugin ───────────────────────────────────────────────────────────────────

void MainComponent::scanPlugins()
{
    statusLabel.setText("Scanning plugins...", juce::dontSendNotification);
    repaint();

    pluginHost.scanForPlugins();

    pluginSelector.clear(juce::dontSendNotification);
    pluginDescriptions.clear();
    fxDescriptions.clear();
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
        else
        {
            fxDescriptions.add(desc);
        }
    }
    pluginSelector.setSelectedId(1, juce::dontSendNotification);

    statusLabel.setText("Found " + juce::String(pluginDescriptions.size()) + " instruments, "
                        + juce::String(fxDescriptions.size()) + " effects",
                        juce::dontSendNotification);
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

    // Always create a fresh editor — createEditorIfNeeded can return stale cached editors
    currentEditor.reset(track.plugin->createEditor());
    if (currentEditor == nullptr) return;
    editorWindow = std::make_unique<PluginEditorWindow>(track.plugin->getName(), currentEditor.get(),
        [this] { closePluginEditor(); });
}

void MainComponent::closePluginEditor()
{
    // Destroy window first (removes editor from component tree), then release editor
    editorWindow = nullptr;
    currentEditor.reset();
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
    // Route through our callback so we can intercept CI SysEx
    deviceManager.addMidiInputDeviceCallback(d.identifier, this);
    currentMidiDeviceId = d.identifier;
    updateStatusLabel();
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& msg)
{
    // MIDI Learn — capture CC mapping
    if (midiLearnActive && midiLearnTarget != MidiTarget::None && msg.isController())
    {
        int ch = msg.getChannel();
        int cc = msg.getControllerNumber();
        int val = msg.getControllerValue();
        juce::MessageManager::callAsync([this, ch, cc, val] {
            processMidiLearnCC(ch, cc, val);
        });
        return;
    }

    // Apply learned MIDI mappings
    if (msg.isController())
    {
        int ch = msg.getChannel();
        int cc = msg.getControllerNumber();
        int val = msg.getControllerValue();

        for (auto& mapping : midiMappings)
        {
            if (mapping.channel == ch && mapping.ccNumber == cc)
            {
                juce::MessageManager::callAsync([this, mapping, val] {
                    applyMidiCC(mapping, val);
                });
            }
        }
    }

    if (midi2Enabled)
    {
        // Route CI SysEx to the handler
        if (midi2Handler.processIncoming(msg))
        {
            // Count and send CI responses back to the Keystage
            auto& outgoing = midi2Handler.getOutgoing();
            int outCount = outgoing.getNumEvents();

            if (!outgoing.isEmpty() && midiOutput)
            {
                for (const auto metadata : outgoing)
                    midiOutput->sendMessageNow(metadata.getMessage());
                midi2Handler.clearOutgoing();
            }

            // Show what CI message was received and how many responses we sent
            juce::String ciInfo;
            {
                auto sdata = msg.getSysExData();
                int ssize = msg.getSysExDataSize();
                int subId = (ssize > 3) ? sdata[3] : 0;
                ciInfo = "CI:0x" + juce::String::toHexString(subId);

                if (subId == 0x34 && ssize > 16)
                {
                    int hdrLen = sdata[14] | (sdata[15] << 7);
                    juce::String hdr;
                    for (int i = 0; i < hdrLen && (16 + i) < ssize; ++i)
                        hdr += juce::String::charToString(static_cast<char>(sdata[16 + i]));
                    ciInfo += " " + hdr;
                }
            }

            juce::MessageManager::callAsync([this, ciInfo, outCount] {
                trackNameLabel.setText(ciInfo + " sent:" + juce::String(outCount),
                    juce::dontSendNotification);
            });

            return; // Don't forward CI SysEx to the audio engine
        }

        // Handle CCs from Keystage knobs (24-31)
        if (msg.isController())
        {
            int cc = msg.getControllerNumber();
            int val = msg.getControllerValue();

            if (cc >= 0 && cc <= 7)
            {
                midi2Handler.handleCC(cc, val);

                // Send OLED updates
                auto& ciOut = midi2Handler.getOutgoing();
                if (!ciOut.isEmpty() && midiOutput)
                {
                    for (const auto metadata : ciOut)
                        midiOutput->sendMessageNow(metadata.getMessage());
                    midi2Handler.clearOutgoing();
                }
            }
        }

        // Handle Keystage transport/nav buttons
        // Log ALL CCs for debugging
        if (msg.isController())
        {
            int tcc = msg.getControllerNumber();
            int tval = msg.getControllerValue();
            int tch = msg.getChannel();

            juce::MessageManager::callAsync([this, tcc, tval, tch] {
                statusLabel.setText("CC" + juce::String(tcc) + "=" + juce::String(tval) + " ch" + juce::String(tch),
                    juce::dontSendNotification);
            });
        }

        // Transport/nav buttons — trigger on any non-zero value (button press)
        if (msg.isController() && msg.getControllerValue() > 0)
        {
            int tcc = msg.getControllerNumber();
            if (tcc == 0x29 || tcc == 41)      // PLAY
            {
                pluginHost.getEngine().play();
                juce::MessageManager::callAsync([this] { playButton.setToggleState(true, juce::dontSendNotification); });
            }
            else if (tcc == 0x2A || tcc == 42) // STOP
            {
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
                juce::MessageManager::callAsync([this] {
                    if (timelineComponent) timelineComponent->repaint();
                });
            }
            else if (tcc == 0x2D || tcc == 45) // REC
            {
                pluginHost.getEngine().toggleRecord();
                juce::MessageManager::callAsync([this] {
                    recordButton.setToggleState(pluginHost.getEngine().isRecording(), juce::dontSendNotification);
                });
            }
            else if (tcc == 0x2B || tcc == 43) // REW / SHIFT+VALUE LEFT → move playhead back
            {
                auto& eng = pluginHost.getEngine();
                double pos = eng.getPositionInBeats();
                double grid = timelineComponent ? timelineComponent->getGridResolution() : 1.0;
                eng.setPosition(juce::jmax(0.0, pos - grid));
                if (timelineComponent) timelineComponent->repaint();
            }
            else if (tcc == 0x2C || tcc == 44) // FF / SHIFT+VALUE RIGHT → move playhead forward
            {
                auto& eng = pluginHost.getEngine();
                double pos = eng.getPositionInBeats();
                double grid = timelineComponent ? timelineComponent->getGridResolution() : 1.0;
                eng.setPosition(pos + grid);
                if (timelineComponent) timelineComponent->repaint();
            }
            else if (tcc == 0x2E || tcc == 46) // LOOP → (reserved for future)
            {
                // Could toggle loop mode
            }
            else if (tcc == 0x2F || tcc == 47) // TEMPO — toggle metronome
            {
                pluginHost.getEngine().toggleMetronome();
                juce::MessageManager::callAsync([this] {
                    metronomeButton.setToggleState(pluginHost.getEngine().isMetronomeOn(), juce::dontSendNotification);
                });
            }
            else if (tcc == 58 || tcc == 0x3A) // NEXT TRACK
                selectTrack(juce::jmin(PluginHost::NUM_TRACKS - 1, selectedTrackIndex + 1));
            else if (tcc == 59 || tcc == 0x3B) // PREV TRACK
                selectTrack(juce::jmax(0, selectedTrackIndex - 1));
            else if (tcc == 32) // CC32 — Page/Value button → cycle parameter page
            {
                midi2Handler.nextPage();
                auto& ciOut = midi2Handler.getOutgoing();
                if (!ciOut.isEmpty() && midiOutput) { for (const auto metadata : ciOut) midiOutput->sendMessageNow(metadata.getMessage()); midi2Handler.clearOutgoing(); }
                juce::MessageManager::callAsync([this] {
                    trackNameLabel.setText("Page " + juce::String(midi2Handler.getCurrentPage() + 1)
                        + "/" + juce::String(midi2Handler.getNumPages()), juce::dontSendNotification);
                    updateParamSliders();
                });
            }
            else if (tcc == 60 || tcc == 0x3C) // VALUE DOWN → prev preset
            {
                midi2Handler.prevPreset();
                juce::MessageManager::callAsync([this] {
                    auto& trk = pluginHost.getTrack(selectedTrackIndex);
                    if (trk.plugin) trackNameLabel.setText("Preset: " + trk.plugin->getProgramName(trk.plugin->getCurrentProgram()), juce::dontSendNotification);
                });
            }
            else if (tcc == 61 || tcc == 0x3D) // VALUE UP → next preset
            {
                midi2Handler.nextPreset();
                juce::MessageManager::callAsync([this] {
                    auto& trk = pluginHost.getTrack(selectedTrackIndex);
                    if (trk.plugin) trackNameLabel.setText("Preset: " + trk.plugin->getProgramName(trk.plugin->getCurrentProgram()), juce::dontSendNotification);
                });
            }
            else if (tcc == 62 || tcc == 0x3E) // VALUE KNOB LEFT → prev page
            {
                midi2Handler.prevPage();
                auto& ciOut = midi2Handler.getOutgoing();
                if (!ciOut.isEmpty() && midiOutput) { for (const auto metadata : ciOut) midiOutput->sendMessageNow(metadata.getMessage()); midi2Handler.clearOutgoing(); }
                juce::MessageManager::callAsync([this] { trackNameLabel.setText("Page " + juce::String(midi2Handler.getCurrentPage() + 1), juce::dontSendNotification); });
            }
            else if (tcc == 63 || tcc == 0x3F) // VALUE KNOB RIGHT → next page
            {
                midi2Handler.nextPage();
                auto& ciOut = midi2Handler.getOutgoing();
                if (!ciOut.isEmpty() && midiOutput) { for (const auto metadata : ciOut) midiOutput->sendMessageNow(metadata.getMessage()); midi2Handler.clearOutgoing(); }
                juce::MessageManager::callAsync([this] { trackNameLabel.setText("Page " + juce::String(midi2Handler.getCurrentPage() + 1), juce::dontSendNotification); });
            }
        }

        // Auto-reconnect if connection was lost
        if (!midi2Handler.isConnected() && msg.isController())
        {
            midi2Handler.sendDiscovery();
            auto& ciOut = midi2Handler.getOutgoing();
            if (!ciOut.isEmpty() && midiOutput)
            {
                for (const auto metadata : ciOut)
                    midiOutput->sendMessageNow(metadata.getMessage());
                midi2Handler.clearOutgoing();
            }
        }
    }

    // Forward all MIDI to the collector for audio processing
    pluginHost.getMidiCollector().addMessageToQueue(msg);

    // Feed into capture buffer (always listening on armed tracks)
    if (msg.isNoteOnOrOff())
    {
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.clipPlayer && track.clipPlayer->armed.load())
            captureBuffer.addMessage(msg, selectedTrackIndex);
    }
}

void MainComponent::disableCurrentMidiDevice()
{
    if (currentMidiDeviceId.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(currentMidiDeviceId, this);
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

void MainComponent::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Check for Updates...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&settingsButton),
        [this](int result)
        {
            if (result == 1)
            {
                auto* dialog = new UpdateDialog();
                dialog->setSize(550, 420);
                juce::DialogWindow::LaunchOptions opts;
                opts.content.setOwned(dialog);
                opts.dialogTitle = "Software Update";
                opts.componentToCentreAround = this;
                opts.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
                opts.escapeKeyTriggersCloseButton = true;
                opts.useNativeTitleBar = true;
                opts.resizable = false;
                opts.launchAsync();
            }
        });
}

void MainComponent::updateStatusLabel()
{
    juce::String text;
    cachedStatusLine1 = {};
    cachedStatusLine2 = {};

    if (useComputerKeyboard)
        cachedStatusLine1 = "KB Oct " + juce::String(computerKeyboardOctave);
    else if (currentMidiDeviceId.isNotEmpty())
        for (const auto& d : midiDevices)
            if (d.identifier == currentMidiDeviceId) { cachedStatusLine1 = d.name; break; }

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        cachedStatusLine2 = dev->getName() + " " + juce::String(dev->getCurrentSampleRate(), 0) + " Hz";

    text = cachedStatusLine1;
    if (text.isNotEmpty() && cachedStatusLine2.isNotEmpty()) text += " | ";
    text += cachedStatusLine2;
    cachedStatusText = text;
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
    juce::String pluginName = track.plugin->getName().toLowerCase();

    juce::Array<juce::AudioProcessorParameter*> selectedParams;

    // ── Plugin-specific parameter mappings ──

    // u-he Diva: filter, oscillators, envelope
    if (pluginName.contains("diva"))
    {
        juce::StringArray wanted = { "cutoff", "resonance", "hpf", "vco mix",
                                      "env2 att", "env2 dec" };
        for (auto& w : wanted)
        {
            for (auto* param : allParams)
            {
                if (param->getName(30).toLowerCase().contains(w))
                { selectedParams.add(param); break; }
            }
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }
    // u-he Hive: macros then filter
    else if (pluginName.contains("hive"))
    {
        juce::StringArray wanted = { "macro 1", "macro 2", "macro 3", "macro 4",
                                      "cutoff", "resonance" };
        for (auto& w : wanted)
        {
            for (auto* param : allParams)
            {
                if (param->getName(30).toLowerCase().contains(w))
                { selectedParams.add(param); break; }
            }
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }
    // Arturia Pigments: macros
    else if (pluginName.contains("pigments"))
    {
        juce::StringArray wanted = { "macro 1", "macro 2", "macro 3",
                                      "macro 4", "macro 5", "macro 6" };
        for (auto& w : wanted)
        {
            for (auto* param : allParams)
            {
                if (param->getName(30).toLowerCase().contains(w))
                { selectedParams.add(param); break; }
            }
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }
    // Arturia Analog Lab / any Arturia — look for macros first
    else if (pluginName.contains("analog lab") || pluginName.contains("arturia") ||
             pluginName.contains("jun-6") || pluginName.contains("jup-8") ||
             pluginName.contains("mini v") || pluginName.contains("cs-80"))
    {
        for (auto* param : allParams)
        {
            juce::String name = param->getName(30).toLowerCase();
            if (name.contains("macro") || name.contains("mcr") || name.contains("assign"))
                selectedParams.add(param);
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }

    // Generic: try macros, then common synth params
    if (selectedParams.isEmpty())
    {
        for (auto* param : allParams)
        {
            juce::String name = param->getName(30).toLowerCase();
            if (name.contains("macro") || name.contains("mcr") || name.contains("assign"))
                selectedParams.add(param);
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }

    if (selectedParams.isEmpty())
    {
        juce::StringArray commonNames = { "cutoff", "filter", "resonance",
                                           "attack", "release", "drive", "mix", "volume" };
        for (auto& cn : commonNames)
        {
            for (auto* param : allParams)
            {
                if (param->getName(30).toLowerCase().contains(cn))
                { selectedParams.add(param); break; }
            }
            if (selectedParams.size() >= NUM_PARAM_SLIDERS) break;
        }
    }

    // Fallback: first N parameters
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
    bpmLabel.setText(juce::String(static_cast<int>(snap.bpm)) + " BPM", juce::dontSendNotification);

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

            // Save plugin description and state
            if (track.plugin)
            {
                auto* pluginXml = trackXml->createNewChildElement("Plugin");
                // Find matching description from known list
                for (const auto& desc : pluginHost.getPluginList().getTypes())
                {
                    if (desc.name == track.plugin->getName() && desc.isInstrument)
                    {
                        pluginXml->addChildElement(desc.createXml().release());
                        break;
                    }
                }
                // Save plugin state (presets, parameters)
                juce::MemoryBlock state;
                track.plugin->getStateInformation(state);
                pluginXml->setAttribute("state", state.toBase64Encoding());
            }

            // Save FX chains
            for (int fx = 0; fx < Track::NUM_FX_SLOTS; ++fx)
            {
                if (track.fxSlots[fx].processor != nullptr)
                {
                    auto* fxXml = trackXml->createNewChildElement("FX");
                    fxXml->setAttribute("slot", fx);
                    fxXml->setAttribute("bypassed", track.fxSlots[fx].bypassed);
                    for (const auto& desc : pluginHost.getPluginList().getTypes())
                    {
                        if (desc.name == track.fxSlots[fx].processor->getName() && !desc.isInstrument)
                        {
                            fxXml->addChildElement(desc.createXml().release());
                            break;
                        }
                    }
                    juce::MemoryBlock fxState;
                    track.fxSlots[fx].processor->getStateInformation(fxState);
                    fxXml->setAttribute("state", fxState.toBase64Encoding());
                }
            }

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

        // Clear all tracks first
        for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
        {
            pluginHost.unloadPlugin(t);
            for (int fx = 0; fx < Track::NUM_FX_SLOTS; ++fx)
                pluginHost.unloadFx(t, fx);
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
        bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);

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

            // Restore plugin
            auto* pluginXml = trackXml->getChildByName("Plugin");
            if (pluginXml != nullptr)
            {
                // Find the plugin description element
                for (auto* descXml : pluginXml->getChildIterator())
                {
                    juce::PluginDescription desc;
                    if (desc.loadFromXml(*descXml))
                    {
                        juce::String err;
                        if (pluginHost.loadPlugin(t, desc, err))
                        {
                            // Restore plugin state
                            auto stateStr = pluginXml->getStringAttribute("state");
                            if (stateStr.isNotEmpty())
                            {
                                juce::MemoryBlock state;
                                state.fromBase64Encoding(stateStr);
                                pluginHost.getTrack(t).plugin->setStateInformation(
                                    state.getData(), static_cast<int>(state.getSize()));
                            }
                        }
                        break;
                    }
                }
            }

            // Restore FX chains
            for (auto* fxXml : trackXml->getChildWithTagNameIterator("FX"))
            {
                int fxSlot = fxXml->getIntAttribute("slot", -1);
                if (fxSlot < 0 || fxSlot >= Track::NUM_FX_SLOTS) continue;

                for (auto* descXml : fxXml->getChildIterator())
                {
                    juce::PluginDescription desc;
                    if (desc.loadFromXml(*descXml))
                    {
                        juce::String err;
                        if (pluginHost.loadFx(t, fxSlot, desc, err))
                        {
                            auto stateStr = fxXml->getStringAttribute("state");
                            if (stateStr.isNotEmpty())
                            {
                                juce::MemoryBlock state;
                                state.fromBase64Encoding(stateStr);
                                pluginHost.getTrack(t).fxSlots[fxSlot].processor->setStateInformation(
                                    state.getData(), static_cast<int>(state.getSize()));
                            }
                            pluginHost.setFxBypassed(t, fxSlot, fxXml->getBoolAttribute("bypassed", false));
                        }
                        break;
                    }
                }
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
}

void MainComponent::paint(juce::Graphics& g)
{
    auto& c = themeManager.getColors();

    // Main body
    g.fillAll(juce::Colour(c.body));

    // Top bar background
    if (auto* lnf = dynamic_cast<DawLookAndFeel*>(&getLookAndFeel()))
    {
        if (lnf->getSidePanelWidth() > 0)
        {
            // Custom top bar (e.g. wood grain)
            int sidePW = lnf->getSidePanelWidth();
            lnf->drawTopBarBackground(g, sidePW, 0, getWidth() - sidePW * 2, 100);
        }
        else
        {
            g.setColour(juce::Colour(c.bodyLight));
            g.fillRect(0, 0, getWidth(), 100);
        }
    }
    else
    {
        g.setColour(juce::Colour(c.bodyLight));
        g.fillRect(0, 0, getWidth(), 100);
    }

    // Toolbar background
    g.setColour(juce::Colour(c.bodyDark));
    g.fillRect(0, 100, getWidth(), 85);

    // Panel dividers (stop at right panel edge)
    g.setColour(juce::Colour(c.border));
    g.drawHorizontalLine(100, 0, static_cast<float>(getWidth() - 180));
    g.drawHorizontalLine(185, 0, static_cast<float>(getWidth() - 180));

    // Accent stripe at top
    g.setColour(juce::Colour(c.accentStripe));
    g.fillRect(0, 0, getWidth(), 2);

    int rightPanelX = getWidth() - 180;

    // Draw decorative side panels if the theme provides them
    if (auto* lnf = dynamic_cast<DawLookAndFeel*>(&getLookAndFeel()))
    {
        if (lnf->getSidePanelWidth() > 0)
            lnf->drawSidePanels(g, getWidth(), getHeight());
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Inset for decorative side panels (e.g. Keystage wood cheeks)
    if (auto* lnf = dynamic_cast<DawLookAndFeel*>(&getLookAndFeel()))
    {
        int sidePW = lnf->getSidePanelWidth();
        if (sidePW > 0)
        {
            area.removeFromLeft(sidePW);
            area.removeFromRight(sidePW);
        }
    }

    int topBarH = 100;
    int bottomBarH = 45;
    int rightPanelW = 180;

    // ── Top Bar — all buttons in one row, square, evenly spaced ──
    auto topBar = area.removeFromTop(topBarH).reduced(4, 8);
    int fullBarX = topBar.getX();
    int fullBarW = topBar.getWidth();
    int barY = topBar.getY();
    int barH = topBar.getHeight();

    int bSz = barH;  // square button size = bar height
    int gap = 4;

    // Status/beat labels on the far right (not square buttons)
    auto rightLabels = topBar;
    beatPanel.setBounds(rightLabels.removeFromRight(280));
    statusLabel.setBounds(0, 0, 0, 0);  // hidden — info merged into beatPanel
    int labelsW = fullBarW - rightLabels.getWidth();  // space consumed by labels

    zoomOutButton.setVisible(false);
    zoomInButton.setVisible(false);
    trackNameLabel.setBounds(0, 0, 0, 0);  // hidden from layout

    // All buttons left-to-right: LEARN | PANIC | KEYS | MIX | STOP | PLAY | REC | MET | COUNT-IN | LOOP | GO | CAPTURE | <<  | >>
    int numButtons = 14;
    int availW = fullBarW - labelsW - gap;
    int stride = availW / numButtons;
    // Clamp button size: square but don't exceed stride minus gap
    int btnSz = juce::jmin(bSz, stride - gap);
    int bY = barY + (barH - btnSz) / 2;
    int tx = fullBarX;

    auto placeBtn = [&](juce::Component& btn) {
        btn.setBounds(tx + (stride - btnSz) / 2, bY, btnSz, btnSz);
        tx += stride;
    };

    placeBtn(midiLearnButton);
    placeBtn(panicButton);
    placeBtn(pianoToggleButton);
    placeBtn(mixerButton);
    placeBtn(stopButton);
    placeBtn(playButton);
    placeBtn(recordButton);
    placeBtn(captureButton);
    placeBtn(metronomeButton);
    placeBtn(countInButton);
    placeBtn(loopButton);
    placeBtn(goButton);
    placeBtn(scrollLeftButton);
    placeBtn(scrollRightButton);

    // ── Fullscreen Visualizer Mode ──
    if (visualizerFullScreen)
    {
        if (projectorMode)
        {
            // Projector mode — zero UI chrome, just the visualizer
            auto visArea = area;

            spectrumDisplay.setVisible(false);
            lissajousDisplay.setVisible(false);
            gforceDisplay.setVisible(false);
            geissDisplay.setVisible(false);
            projectMDisplay.setVisible(false);
            visExitButton.setVisible(false);
            visSelector.setVisible(false);
            setVisControlsVisible();
            projectorButton.setVisible(false);

            if (currentVisMode == 0) { spectrumDisplay.setBounds(visArea); spectrumDisplay.setAlpha(1.0f); spectrumDisplay.setVisible(true); }
            else if (currentVisMode == 1) { lissajousDisplay.setBounds(visArea); lissajousDisplay.setVisible(true); }
            else if (currentVisMode == 2) { gforceDisplay.setBounds(visArea); gforceDisplay.setVisible(true); }
            else if (currentVisMode == 3) { geissDisplay.setBounds(visArea); geissDisplay.setVisible(true); }
            else if (currentVisMode == 4) { projectMDisplay.setBounds(visArea); projectMDisplay.setVisible(true); }
        }
        else
        {
            // Fullscreen with control bar
            auto controlBar = area.removeFromTop(36).reduced(4, 2);
            visExitButton.setBounds(controlBar.removeFromLeft(55));
            visExitButton.setVisible(true);
            controlBar.removeFromLeft(6);
            visSelector.setBounds(controlBar.removeFromLeft(90));
            visSelector.setVisible(true);
            controlBar.removeFromLeft(6);
            projectorButton.setVisible(false);

            // Visualizer controls in fullscreen control bar
            controlBar.removeFromLeft(10);
            if (currentVisMode == 0) // Spectrum
            {
                specDecayBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                specSensDownBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(2);
                specSensUpBtn.setBounds(controlBar.removeFromLeft(30));
            }
            else if (currentVisMode == 1) // Lissajous
            {
                lissZoomOutBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(2);
                lissZoomInBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(3);
                lissDotsBtn.setBounds(controlBar.removeFromLeft(50));
            }
            else if (currentVisMode == 2) // G-Force
            {
                gfRibbonDownBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(2);
                gfRibbonUpBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(3);
                gfTrailBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                gfSpeedSelector.setBounds(controlBar.removeFromLeft(60));
            }
            else if (currentVisMode == 3) // Geiss
            {
                geissWaveBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                geissPaletteBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                geissSceneBtn.setBounds(controlBar.removeFromLeft(55));
                controlBar.removeFromLeft(3);
                geissWaveDownBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(2);
                geissWaveUpBtn.setBounds(controlBar.removeFromLeft(30));
                controlBar.removeFromLeft(3);
                geissWarpLockBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                geissPalLockBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                geissSpeedSelector.setBounds(controlBar.removeFromLeft(60));
                controlBar.removeFromLeft(3);
                geissAutoPilotBtn.setBounds(controlBar.removeFromLeft(50));
                controlBar.removeFromLeft(3);
                geissBgBtn.setBounds(controlBar.removeFromLeft(30));
            }
            else if (currentVisMode == 4)
            {
                pmPrevBtn.setBounds(controlBar.removeFromLeft(45));
                controlBar.removeFromLeft(3);
                pmNextBtn.setBounds(controlBar.removeFromLeft(45));
                controlBar.removeFromLeft(3);
                pmRandBtn.setBounds(controlBar.removeFromLeft(45));
                controlBar.removeFromLeft(3);
                pmLockBtn.setBounds(controlBar.removeFromLeft(45));
                controlBar.removeFromLeft(3);
                pmBgBtn.setBounds(controlBar.removeFromLeft(30));
            }
            setVisControlsVisible();

            auto visArea = area.reduced(2, 2);

            spectrumDisplay.setVisible(false);
            lissajousDisplay.setVisible(false);
            gforceDisplay.setVisible(false);
            geissDisplay.setVisible(false);
            projectMDisplay.setVisible(false);

            if (currentVisMode == 0) { spectrumDisplay.setBounds(visArea); spectrumDisplay.setAlpha(1.0f); spectrumDisplay.setVisible(true); }
            else if (currentVisMode == 1) { lissajousDisplay.setBounds(visArea); lissajousDisplay.setVisible(true); }
            else if (currentVisMode == 2) { gforceDisplay.setBounds(visArea); gforceDisplay.setVisible(true); }
            else if (currentVisMode == 3) { geissDisplay.setBounds(visArea); geissDisplay.setVisible(true); }
            else if (currentVisMode == 4) { projectMDisplay.setBounds(visArea); projectMDisplay.setVisible(true); }
        }

        // Hide everything else
        newClipButton.setVisible(false);
        deleteClipButton.setVisible(false);
        duplicateClipButton.setVisible(false);
        splitClipButton.setVisible(false);
        quantizeButton.setVisible(false);
        gridSelector.setVisible(false);
        saveButton.setVisible(false);
        loadButton.setVisible(false);
        undoButton.setVisible(false);
        redoButton.setVisible(false);
        themeSelector.setVisible(false);
        audioSettingsButton.setVisible(false);
        midi2Button.setVisible(false);
        pluginSelector.setVisible(false);
        openEditorButton.setVisible(false);
        midiInputSelector.setVisible(false);
        midiRefreshButton.setVisible(false);
        for (int i = 0; i < NUM_FX_SLOTS; ++i)
        {
            fxSelectors[i]->setVisible(false);
            fxEditorButtons[i]->setVisible(false);
        }
        for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
        {
            paramSliders[i]->setVisible(false);
            paramLabels[i]->setVisible(false);
        }
        volumeSlider.setVisible(false);
        volumeLabel.setVisible(false);
        panSlider.setVisible(false);
        panLabel.setVisible(false);
        if (timelineComponent) timelineComponent->setVisible(false);
        return;
    }

    // ── Restore visibility when not in vis mode ──
    newClipButton.setVisible(true);
    deleteClipButton.setVisible(true);
    duplicateClipButton.setVisible(true);
    splitClipButton.setVisible(true);
    quantizeButton.setVisible(true);
    gridSelector.setVisible(true);
    saveButton.setVisible(true);
    loadButton.setVisible(true);
    undoButton.setVisible(true);
    redoButton.setVisible(true);
    themeSelector.setVisible(true);
    audioSettingsButton.setVisible(true);
    midi2Button.setVisible(true);
    pluginSelector.setVisible(true);
    openEditorButton.setVisible(true);
    midiInputSelector.setVisible(true);
    midiRefreshButton.setVisible(true);
    for (int i = 0; i < NUM_FX_SLOTS; ++i)
    {
        fxSelectors[i]->setVisible(true);
        fxEditorButtons[i]->setVisible(true);
    }
    for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
    {
        paramSliders[i]->setVisible(true);
        paramLabels[i]->setVisible(true);
    }
    volumeSlider.setVisible(true);
    volumeLabel.setVisible(true);
    panSlider.setVisible(true);
    panLabel.setVisible(true);
    if (timelineComponent) timelineComponent->setVisible(true);
    spectrumDisplay.setVisible(currentVisMode == 0);
    lissajousDisplay.setVisible(currentVisMode == 1);
    gforceDisplay.setVisible(currentVisMode == 2);
    geissDisplay.setVisible(currentVisMode == 3);
    projectMDisplay.setVisible(currentVisMode == 4);
    visExitButton.setVisible(false);
    projectorButton.setVisible(false);
    visSelector.setVisible(true);
    fullscreenButton.setVisible(true);
    midi2Button.setVisible(true);
    setVisControlsVisible();

    // ── Edit Toolbar ──
    auto toolbar = area.removeFromTop(85).reduced(4, 4);
    newClipButton.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(3);
    deleteClipButton.setBounds(toolbar.removeFromLeft(95));
    toolbar.removeFromLeft(3);
    duplicateClipButton.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(3);
    splitClipButton.setBounds(toolbar.removeFromLeft(65));
    toolbar.removeFromLeft(2);
    quantizeButton.setBounds(toolbar.removeFromLeft(82));
    toolbar.removeFromLeft(4);
    gridSelector.setBounds(toolbar.removeFromLeft(75));
    toolbar.removeFromLeft(4);
    saveButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(2);
    loadButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(2);
    undoButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(2);
    redoButton.setBounds(toolbar.removeFromLeft(60));

    // Pack remaining controls at the right end of the toolbar
    tapTempoButton.setBounds(toolbar.removeFromRight(60));
    toolbar.removeFromRight(3);
    bpmArrowButton.setBounds(toolbar.removeFromRight(28));
    toolbar.removeFromRight(2);
    bpmLabel.setBounds(toolbar.removeFromRight(80));
    toolbar.removeFromRight(6);
    midi2Button.setBounds(toolbar.removeFromRight(42));
    toolbar.removeFromRight(2);
    visSelector.setBounds(toolbar.removeFromRight(82));
    toolbar.removeFromRight(2);
    projectorButton.setVisible(false);
    fullscreenButton.setBounds(toolbar.removeFromRight(46));
    toolbar.removeFromRight(2);
    audioSettingsButton.setBounds(toolbar.removeFromRight(92));
    toolbar.removeFromRight(2);
    themeSelector.setBounds(toolbar.removeFromRight(92));

    // ── Right Panel ──
    auto rightPanel = area.removeFromRight(rightPanelW).reduced(8, 4);

    // Visualizer display
    auto visPanelArea = rightPanel.removeFromTop(70);
    if (currentVisMode == 0)  // Spectrum
    {
        spectrumDisplay.setBounds(visPanelArea);
        spectrumDisplay.setAlpha(1.0f);
        spectrumDisplay.setVisible(true);
    }
    else if (currentVisMode == 1)  // Lissajous
    {
        lissajousDisplay.setBounds(visPanelArea);
        lissajousDisplay.setVisible(true);
    }
    else if (currentVisMode == 2)  // G-Force
    {
        gforceDisplay.setBounds(visPanelArea);
        gforceDisplay.setVisible(true);
    }
    else if (currentVisMode == 3)  // Geiss
    {
        geissDisplay.setBounds(visPanelArea);
        geissDisplay.setVisible(true);
    }
    else if (currentVisMode == 4)  // MilkDrop
    {
        projectMDisplay.setBounds(visPanelArea);
        projectMDisplay.setVisible(true);
    }
    rightPanel.removeFromTop(4);

    // Visualizer controls below the vis panel
    if (currentVisMode == 0) // Spectrum
    {
        auto row = rightPanel.removeFromTop(28);
        specDecayBtn.setBounds(row.removeFromLeft(50));
        row.removeFromLeft(3);
        specSensDownBtn.setBounds(row.removeFromLeft(28));
        row.removeFromLeft(2);
        specSensUpBtn.setBounds(row.removeFromLeft(28));
        rightPanel.removeFromTop(4);
    }
    else if (currentVisMode == 1) // Lissajous
    {
        auto row = rightPanel.removeFromTop(28);
        lissZoomOutBtn.setBounds(row.removeFromLeft(28));
        row.removeFromLeft(2);
        lissZoomInBtn.setBounds(row.removeFromLeft(28));
        row.removeFromLeft(3);
        lissDotsBtn.setBounds(row.removeFromLeft(50));
        rightPanel.removeFromTop(4);
    }
    else if (currentVisMode == 2) // G-Force
    {
        auto row = rightPanel.removeFromTop(28);
        gfRibbonDownBtn.setBounds(row.removeFromLeft(28));
        row.removeFromLeft(2);
        gfRibbonUpBtn.setBounds(row.removeFromLeft(28));
        row.removeFromLeft(3);
        gfTrailBtn.setBounds(row.removeFromLeft(50));
        row.removeFromLeft(3);
        gfSpeedSelector.setBounds(row.removeFromLeft(55));
        rightPanel.removeFromTop(4);
    }
    else if (currentVisMode == 3) // Geiss
    {
        auto geissRow1 = rightPanel.removeFromTop(28);
        geissWaveBtn.setBounds(geissRow1.removeFromLeft(50));
        geissRow1.removeFromLeft(3);
        geissPaletteBtn.setBounds(geissRow1.removeFromLeft(50));
        geissRow1.removeFromLeft(3);
        geissSceneBtn.setBounds(geissRow1.removeFromLeft(55));
        geissRow1.removeFromLeft(3);
        geissWaveDownBtn.setBounds(geissRow1.removeFromLeft(28));
        geissRow1.removeFromLeft(2);
        geissWaveUpBtn.setBounds(geissRow1.removeFromLeft(28));
        rightPanel.removeFromTop(3);
        auto geissRow2 = rightPanel.removeFromTop(28);
        geissWarpLockBtn.setBounds(geissRow2.removeFromLeft(50));
        geissRow2.removeFromLeft(3);
        geissPalLockBtn.setBounds(geissRow2.removeFromLeft(50));
        geissRow2.removeFromLeft(3);
        geissSpeedSelector.setBounds(geissRow2.removeFromLeft(60));
        geissRow2.removeFromLeft(3);
        geissAutoPilotBtn.setBounds(geissRow2.removeFromLeft(50));
        geissRow2.removeFromLeft(3);
        geissBgBtn.setBounds(geissRow2.removeFromLeft(28));
        rightPanel.removeFromTop(4);
    }
    else if (currentVisMode == 4) // MilkDrop
    {
        auto pmRow = rightPanel.removeFromTop(28);
        pmPrevBtn.setBounds(pmRow.removeFromLeft(45));
        pmRow.removeFromLeft(3);
        pmNextBtn.setBounds(pmRow.removeFromLeft(45));
        pmRow.removeFromLeft(3);
        pmRandBtn.setBounds(pmRow.removeFromLeft(45));
        pmRow.removeFromLeft(3);
        pmLockBtn.setBounds(pmRow.removeFromLeft(45));
        pmRow.removeFromLeft(3);
        pmBgBtn.setBounds(pmRow.removeFromLeft(28));
        rightPanel.removeFromTop(4);
    }
    setVisControlsVisible();

    pluginSelector.setBounds(rightPanel.removeFromTop(36));
    rightPanel.removeFromTop(4);
    openEditorButton.setBounds(rightPanel.removeFromTop(36));
    rightPanel.removeFromTop(4);
    auto midiRow = rightPanel.removeFromTop(34);
    midiRefreshButton.setBounds(midiRow.removeFromRight(65));
    midiRow.removeFromRight(4);
    midiInputSelector.setBounds(midiRow);
    rightPanel.removeFromTop(4);

    // FX insert slots
    for (int i = 0; i < NUM_FX_SLOTS; ++i)
    {
        auto fxRow = rightPanel.removeFromTop(32);
        fxEditorButtons[i]->setBounds(fxRow.removeFromRight(32));
        fxRow.removeFromRight(2);
        fxSelectors[i]->setBounds(fxRow);
        rightPanel.removeFromTop(3);
    }
    rightPanel.removeFromTop(4);

    // Plugin parameter knobs — 3x2 grid
    if (paramSliders.size() > 0)
    {
        int knobSize = juce::jmin(58, (rightPanel.getWidth() - 8) / 3);
        int knobAreaH = 2 * (knobSize + 16) + 4;
        auto knobArea = rightPanel.removeFromTop(knobAreaH);
        rightPanel.removeFromTop(4);

        for (int i = 0; i < NUM_PARAM_SLIDERS; ++i)
        {
            int col = i % 3;
            int row = i / 3;
            int kx = knobArea.getX() + col * (knobSize + 4);
            int ky = knobArea.getY() + row * (knobSize + 16 + 2);

            paramLabels[i]->setBounds(kx, ky, knobSize, 14);
            paramSliders[i]->setBounds(kx, ky + 14, knobSize, knobSize);
        }
    }

    // Spectrum ghosted behind volume/pan area (only when not the active visualizer)
    if (currentVisMode != 0)
    {
        spectrumDisplay.setBounds(rightPanel);
        spectrumDisplay.setAlpha(0.3f);
        spectrumDisplay.setVisible(true);
        spectrumDisplay.toBack();
    }
    else
    {
        spectrumDisplay.setAlpha(1.0f);
    }

    // Volume/Pan — fills remaining space (on top of spectrum)
    auto mixArea = rightPanel;
    auto volArea = mixArea.removeFromLeft(mixArea.getWidth() / 2);
    auto panArea = mixArea;

    volumeLabel.setBounds(volArea.removeFromTop(14));
    volumeSlider.setBounds(volArea);

    panLabel.setBounds(panArea.removeFromTop(14));
    panSlider.setBounds(panArea.reduced(8, 0));

    // ── Touch Piano (bottom of main area, when visible) ──
    if (touchPianoVisible)
    {
        auto pianoBar = area.removeFromBottom(4);  // small gap
        (void)pianoBar;
        auto pianoControlRow = area.removeFromBottom(28);
        pianoOctDownButton.setBounds(pianoControlRow.removeFromLeft(45));
        pianoControlRow.removeFromLeft(3);
        pianoOctUpButton.setBounds(pianoControlRow.removeFromLeft(45));
        pianoOctDownButton.setVisible(true);
        pianoOctUpButton.setVisible(true);

        auto pianoArea = area.removeFromBottom(100);
        touchPiano.setBounds(pianoArea);
    }
    else
    {
        pianoOctDownButton.setVisible(false);
        pianoOctUpButton.setVisible(false);
    }

    // ── Timeline / Mixer fills the rest ──
    area.reduce(2, 2);
    if (mixerVisible)
    {
        mixerComponent->setBounds(area);
        mixerComponent->setVisible(true);
        if (timelineComponent) timelineComponent->setVisible(false);
    }
    else
    {
        mixerComponent->setVisible(false);
        if (timelineComponent)
        {
            timelineComponent->setVisible(true);
            timelineComponent->setBounds(area);
        }
    }

    // Gamepad overlay covers the main content area
    gamepadOverlay.setBounds(getLocalBounds());
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

void MainComponent::retrieveCapture()
{
    auto& eng = pluginHost.getEngine();
    bool wasPlaying = eng.isPlaying();
    bool clipsExist = hasExistingClips();

    // Determine BPM: estimate from played notes if transport stopped and no clips exist
    double bpm = eng.getBpm();
    bool adjustedTempo = false;

    if (!wasPlaying && !clipsExist)
    {
        double estimated = captureBuffer.estimateTempo();
        if (estimated > 0.0)
        {
            bpm = estimated;
            eng.setBpm(bpm);
            bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);
            adjustedTempo = true;
        }
    }

    // Retrieve captured data grouped by track
    auto captures = captureBuffer.retrieve(bpm);
    if (captures.isEmpty())
    {
        statusLabel.setText("Capture: nothing to retrieve", juce::dontSendNotification);
        return;
    }

    int totalNotes = 0;
    int tracksUsed = 0;

    for (auto& tc : captures)
    {
        auto& track = pluginHost.getTrack(tc.trackIndex);
        if (!track.clipPlayer) continue;

        // Find first empty slot
        int targetSlot = -1;
        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            if (!track.clipPlayer->getSlot(s).hasContent())
            {
                targetSlot = s;
                break;
            }
        }
        if (targetSlot < 0) continue;

        auto& slot = track.clipPlayer->getSlot(targetSlot);
        if (!slot.clip)
            slot.clip = std::make_unique<MidiClip>();

        slot.clip->events.clear();
        for (int i = 0; i < tc.sequence.getNumEvents(); ++i)
            slot.clip->events.addEvent(tc.sequence.getEventPointer(i)->message);
        slot.clip->events.updateMatchedPairs();
        slot.clip->lengthInBeats = tc.lengthInBeats;

        // Place clip at position 0 (if transport was stopped) or current playhead
        if (wasPlaying)
        {
            double playhead = eng.getPositionInBeats();
            double clipStart = std::floor(playhead / 4.0) * 4.0 - tc.lengthInBeats;
            slot.clip->timelinePosition = juce::jmax(0.0, clipStart);
        }
        else
        {
            slot.clip->timelinePosition = 0.0;
        }

        slot.state.store(ClipSlot::Stopped);

        // Count notes
        for (int i = 0; i < tc.sequence.getNumEvents(); ++i)
            if (tc.sequence.getEventPointer(i)->message.isNoteOn())
                ++totalNotes;
        ++tracksUsed;
    }

    // Status message
    juce::String msg = "Captured " + juce::String(totalNotes) + " notes on "
                       + juce::String(tracksUsed) + " track"
                       + (tracksUsed != 1 ? "s" : "");
    if (adjustedTempo)
        msg += " (tempo: " + juce::String(static_cast<int>(bpm)) + " BPM)";
    statusLabel.setText(msg, juce::dontSendNotification);

    // Auto-start playback if transport was stopped
    if (!wasPlaying)
    {
        eng.resetPosition();
        eng.play();
        playButton.setToggleState(true, juce::dontSendNotification);
    }

    if (timelineComponent) timelineComponent->repaint();
    takeSnapshot();
}

bool MainComponent::hasExistingClips() const
{
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto& track = pluginHost.getTrack(t);
        if (!track.clipPlayer) continue;
        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
            if (track.clipPlayer->getSlot(s).hasContent())
                return true;
    }
    return false;
}

void MainComponent::sendNoteOn(int note)
{
    auto msg = juce::MidiMessage::noteOn(1, note, 0.8f);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    pluginHost.getMidiCollector().addMessageToQueue(msg);

    // Capture on armed tracks
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.clipPlayer && track.clipPlayer->armed.load())
        captureBuffer.addMessage(msg, selectedTrackIndex);
}

void MainComponent::sendNoteOff(int note)
{
    auto msg = juce::MidiMessage::noteOff(1, note);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    pluginHost.getMidiCollector().addMessageToQueue(msg);

    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (track.clipPlayer && track.clipPlayer->armed.load())
        captureBuffer.addMessage(msg, selectedTrackIndex);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // ESC = exit fullscreen visualizer (both modes)
    if (key == juce::KeyPress::escapeKey && visualizerFullScreen)
    {
        visualizerFullScreen = false;
        projectorMode = false;
        fullscreenButton.setToggleState(false, juce::dontSendNotification);
        projectorButton.setToggleState(false, juce::dontSendNotification);
        resized();
        repaint();
        grabKeyboardFocus();
        return true;
    }

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

void MainComponent::setVisControlsVisible()
{
    bool geiss = (currentVisMode == 3);
    geissWaveBtn.setVisible(geiss);
    geissPaletteBtn.setVisible(geiss);
    geissSceneBtn.setVisible(geiss);
    geissWaveUpBtn.setVisible(geiss);
    geissWaveDownBtn.setVisible(geiss);
    geissWarpLockBtn.setVisible(geiss);
    geissPalLockBtn.setVisible(geiss);
    geissSpeedSelector.setVisible(geiss);
    geissAutoPilotBtn.setVisible(geiss);
    geissBgBtn.setVisible(geiss);

    bool pm = (currentVisMode == 4);
    pmNextBtn.setVisible(pm);
    pmPrevBtn.setVisible(pm);
    pmRandBtn.setVisible(pm);
    pmLockBtn.setVisible(pm);
    pmBgBtn.setVisible(pm);

    bool gf = (currentVisMode == 2);
    gfRibbonUpBtn.setVisible(gf);
    gfRibbonDownBtn.setVisible(gf);
    gfTrailBtn.setVisible(gf);
    gfSpeedSelector.setVisible(gf);

    bool spec = (currentVisMode == 0);
    specDecayBtn.setVisible(spec);
    specSensUpBtn.setVisible(spec);
    specSensDownBtn.setVisible(spec);

    bool liss = (currentVisMode == 1);
    lissZoomInBtn.setVisible(liss);
    lissZoomOutBtn.setVisible(liss);
    lissDotsBtn.setVisible(liss);
}

void MainComponent::applyThemeToControls()
{
    auto& c = themeManager.getColors();
    auto fontName = themeManager.getLookAndFeel()->getUIFontName();

    // Labels
    trackNameLabel.setColour(juce::Label::textColourId, juce::Colour(c.amber));
    trackNameLabel.setFont(juce::Font(fontName, 16.0f, juce::Font::bold));
    beatPanel.setFontName(fontName);
    beatPanel.setColors(juce::Colour(c.lcdText), juce::Colour(c.lcdBg));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(c.textSecondary));
    trackInfoLabel.setColour(juce::Label::textColourId, juce::Colour(c.textSecondary));

    // BPM controls
    bpmLabel.setFont(juce::Font(fontName, 14.0f, juce::Font::bold));
    bpmLabel.setColour(juce::Label::textColourId, juce::Colour(c.lcdText));
    bpmLabel.setColour(juce::Label::backgroundColourId, juce::Colour(c.lcdBg));
    bpmDownButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    bpmUpButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));

    // Transport
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.redDark));
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.red));
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.greenDark));
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnStop));
    metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnMetronome));
    metronomeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.btnMetronomeOn));
    countInButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnCountIn));
    countInButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.btnCountInOn));
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnLoop));
    loopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.btnLoopOn));
    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffdd6600));
    midiLearnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    midiLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.amber));
    goButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    goButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.green));
    captureButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    captureButton.setColour(juce::TextButton::textColourOffId, juce::Colour(c.textSecondary));

    // Edit toolbar
    newClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNewClip));
    deleteClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnDeleteClip));
    duplicateClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnDuplicate));
    splitClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnSplit));
    quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnQuantize));
    editClipButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnEditNotes));

    // Navigation
    zoomInButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    zoomOutButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    scrollLeftButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    scrollRightButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));

    // Right panel
    midiRefreshButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));
    midi2Button.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnMidi2));
    midi2Button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(c.btnMidi2On));
    saveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnSave));
    loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnLoad));
    undoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnUndoRedo));
    redoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnUndoRedo));

    for (int i = 0; i < NUM_FX_SLOTS; ++i)
        fxEditorButtons[i]->setColour(juce::TextButton::buttonColourId, juce::Colour(c.btnNav));

    repaint();
}

void MainComponent::updateFxDisplay()
{
    for (int i = 0; i < NUM_FX_SLOTS; ++i)
    {
        auto* selector = fxSelectors[i];
        selector->clear(juce::dontSendNotification);
        selector->addItem("FX " + juce::String(i + 1) + ": Empty", 1);

        // Add all effect plugins
        for (int p = 0; p < fxDescriptions.size(); ++p)
            selector->addItem(fxDescriptions[p].name, p + 2);

        // Check if an FX is loaded in this slot
        auto& track = pluginHost.getTrack(selectedTrackIndex);
        if (track.fxSlots[i].processor != nullptr)
        {
            juce::String fxName = track.fxSlots[i].processor->getName();
            // Find the matching item
            bool found = false;
            for (int p = 0; p < fxDescriptions.size(); ++p)
            {
                if (fxDescriptions[p].name == fxName)
                {
                    selector->setSelectedId(p + 2, juce::dontSendNotification);
                    found = true;
                    break;
                }
            }
            if (!found)
                selector->setSelectedId(1, juce::dontSendNotification);

        }
        else
        {
            selector->setSelectedId(1, juce::dontSendNotification);
        }
    }
}

void MainComponent::loadFxPlugin(int slotIndex)
{
    auto* selector = fxSelectors[slotIndex];
    int id = selector->getSelectedId();

    if (id <= 1)
    {
        // "Empty" selected — unload
        pluginHost.unloadFx(selectedTrackIndex, slotIndex);
        return;
    }

    int descIdx = id - 2;
    if (descIdx < 0 || descIdx >= fxDescriptions.size()) return;

    juce::String err;
    bool ok = pluginHost.loadFx(selectedTrackIndex, slotIndex, fxDescriptions[descIdx], err);
    if (!ok)
        statusLabel.setText("FX load error: " + err, juce::dontSendNotification);
    else
        statusLabel.setText("FX " + juce::String(slotIndex + 1) + ": " + fxDescriptions[descIdx].name, juce::dontSendNotification);
}

void MainComponent::openFxEditor(int slotIndex)
{
    auto& track = pluginHost.getTrack(selectedTrackIndex);
    if (slotIndex < 0 || slotIndex >= Track::NUM_FX_SLOTS) return;

    auto* fxProc = track.fxSlots[slotIndex].processor;
    if (fxProc == nullptr || !fxProc->hasEditor()) return;

    auto* editor = fxProc->createEditor();
    if (editor == nullptr) return;

    auto name = fxProc->getName() + " (FX " + juce::String(slotIndex + 1) + ")";
    new PluginEditorWindow(name, editor, [editor] { delete editor->getParentComponent(); });
}

void MainComponent::startMidiLearn(MidiTarget target)
{
    midiLearnTarget = target;

    juce::String targetName;
    switch (target)
    {
        case MidiTarget::Volume:    targetName = "Volume"; break;
        case MidiTarget::Pan:       targetName = "Pan"; break;
        case MidiTarget::Bpm:       targetName = "BPM"; break;
        case MidiTarget::Play:      targetName = "Play"; break;
        case MidiTarget::Stop:      targetName = "Stop"; break;
        case MidiTarget::Record:    targetName = "Record"; break;
        case MidiTarget::Metronome: targetName = "Metronome"; break;
        case MidiTarget::Loop:      targetName = "Loop"; break;
        case MidiTarget::TrackNext: targetName = "Track Next"; break;
        case MidiTarget::TrackPrev: targetName = "Track Prev"; break;
        case MidiTarget::Param0: case MidiTarget::Param1: case MidiTarget::Param2:
        case MidiTarget::Param3: case MidiTarget::Param4: case MidiTarget::Param5:
            targetName = "Param " + juce::String(static_cast<int>(target) - static_cast<int>(MidiTarget::Param0) + 1);
            break;
        case MidiTarget::GeissWaveform:   targetName = "Geiss Wave"; break;
        case MidiTarget::GeissPalette:    targetName = "Geiss Palette"; break;
        case MidiTarget::GeissScene:      targetName = "Geiss Scene"; break;
        case MidiTarget::GeissWaveScale:  targetName = "Geiss Wave Scale"; break;
        case MidiTarget::GeissWarpLock:   targetName = "Geiss Warp Lock"; break;
        case MidiTarget::GeissPaletteLock:targetName = "Geiss Palette Lock"; break;
        case MidiTarget::GeissSpeed:      targetName = "Geiss Speed"; break;
        case MidiTarget::GForceRibbons:   targetName = "GF Ribbons"; break;
        case MidiTarget::GForceTrail:     targetName = "GF Trail"; break;
        case MidiTarget::GForceSpeed:     targetName = "GF Speed"; break;
        case MidiTarget::SpecDecay:       targetName = "Spec Decay"; break;
        case MidiTarget::SpecSensitivity: targetName = "Spec Sensitivity"; break;
        case MidiTarget::LissZoom:        targetName = "Liss Zoom"; break;
        case MidiTarget::LissDots:        targetName = "Liss Dots"; break;
        default: targetName = "?"; break;
    }

    statusLabel.setText("Waiting for CC -> " + targetName + "...", juce::dontSendNotification);
}

void MainComponent::processMidiLearnCC(int channel, int cc, int /*value*/)
{
    // Remove any existing mapping for this CC
    for (int i = midiMappings.size() - 1; i >= 0; --i)
    {
        if (midiMappings[i].channel == channel && midiMappings[i].ccNumber == cc)
            midiMappings.remove(i);
    }

    // Also remove any existing mapping for this target
    for (int i = midiMappings.size() - 1; i >= 0; --i)
    {
        if (midiMappings[i].target == midiLearnTarget)
            midiMappings.remove(i);
    }

    MidiMapping mapping;
    mapping.channel = channel;
    mapping.ccNumber = cc;
    mapping.target = midiLearnTarget;
    midiMappings.add(mapping);

    statusLabel.setText("Mapped CC" + juce::String(cc) + " ch" + juce::String(channel)
                        + " -> " + statusLabel.getText().fromFirstOccurrenceOf("-> ", false, false),
                        juce::dontSendNotification);

    midiLearnTarget = MidiTarget::None;
}

void MainComponent::applyMidiCC(const MidiMapping& mapping, int value)
{
    float norm = static_cast<float>(value) / 127.0f;

    switch (mapping.target)
    {
        case MidiTarget::Volume:
            volumeSlider.setValue(norm, juce::sendNotification);
            break;
        case MidiTarget::Pan:
            panSlider.setValue(norm * 2.0 - 1.0, juce::sendNotification);
            break;
        case MidiTarget::Bpm:
        {
            double bpm = 20.0 + norm * 280.0;
            pluginHost.getEngine().setBpm(bpm);
            bpmLabel.setText(juce::String(static_cast<int>(bpm)) + " BPM", juce::dontSendNotification);
            break;
        }
        case MidiTarget::Play:
            if (value > 0) pluginHost.getEngine().play();
            break;
        case MidiTarget::Stop:
            if (value > 0) pluginHost.getEngine().stop();
            break;
        case MidiTarget::Record:
            if (value > 0) pluginHost.getEngine().toggleRecord();
            break;
        case MidiTarget::Metronome:
            if (value > 0) pluginHost.getEngine().toggleMetronome();
            break;
        case MidiTarget::Loop:
            if (value > 0) pluginHost.getEngine().toggleLoop();
            break;
        case MidiTarget::TrackNext:
            if (value > 0) selectTrack(juce::jmin(PluginHost::NUM_TRACKS - 1, selectedTrackIndex + 1));
            break;
        case MidiTarget::TrackPrev:
            if (value > 0) selectTrack(juce::jmax(0, selectedTrackIndex - 1));
            break;
        case MidiTarget::Param0: case MidiTarget::Param1: case MidiTarget::Param2:
        case MidiTarget::Param3: case MidiTarget::Param4: case MidiTarget::Param5:
        {
            int idx = static_cast<int>(mapping.target) - static_cast<int>(MidiTarget::Param0);
            if (idx >= 0 && idx < NUM_PARAM_SLIDERS && paramSliders[idx]->isEnabled())
                paramSliders[idx]->setValue(norm, juce::sendNotification);
            break;
        }
        case MidiTarget::GeissWaveform:
            if (value > 0) geissDisplay.cycleWaveform();
            break;
        case MidiTarget::GeissPalette:
            if (value > 0) geissDisplay.setPaletteStyle(value % GeissComponent::NUM_PALETTE_STYLES);
            break;
        case MidiTarget::GeissScene:
            if (value > 0) geissDisplay.newRandomScene();
            break;
        case MidiTarget::GeissWaveScale:
            geissDisplay.setWaveScale(norm * 3.0f);
            break;
        case MidiTarget::GeissWarpLock:
            if (value > 0) geissDisplay.toggleWarpLock();
            break;
        case MidiTarget::GeissPaletteLock:
            if (value > 0) geissDisplay.togglePaletteLock();
            break;
        case MidiTarget::GeissSpeed:
            geissDisplay.setSpeed(0.25f + norm * 3.75f);
            break;
        case MidiTarget::GForceRibbons:
            gforceDisplay.setRibbonCount(1 + static_cast<int>(norm * 7.0f));
            break;
        case MidiTarget::GForceTrail:
            gforceDisplay.setTrailIntensity(norm);
            break;
        case MidiTarget::GForceSpeed:
            gforceDisplay.setSpeed(0.25f + norm * 3.75f);
            break;
        case MidiTarget::SpecDecay:
            spectrumDisplay.setDecaySpeed(0.5f + norm * 0.49f);
            break;
        case MidiTarget::SpecSensitivity:
            spectrumDisplay.setSensitivity(0.1f + norm * 1.9f);
            break;
        case MidiTarget::LissZoom:
            lissajousDisplay.setZoom(0.5f + norm * 9.5f);
            break;
        case MidiTarget::LissDots:
            lissajousDisplay.setDotCount(64 + static_cast<int>(norm * 960.0f));
            break;
        default: break;
    }
}

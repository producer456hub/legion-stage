#include "GamepadHandler.h"

#if JUCE_WINDOWS
 #include <Windows.h>
 #include <Xinput.h>
 #pragma comment(lib, "xinput.lib")
#endif

GamepadHandler::GamepadHandler()
{
    // Default chord degrees: I, IV, V, vi (relative to scale)
    chordDegrees.resize(4);
    chordDegrees.set(0, { 0, 2, 4 });      // I
    chordDegrees.set(1, { 3, 5, 0 });      // IV
    chordDegrees.set(2, { 4, 6, 1 });      // V
    chordDegrees.set(3, { 5, 0, 2 });      // vi
}

GamepadHandler::~GamepadHandler()
{
    stopTimer();
    releaseAllNotes();
}

void GamepadHandler::setEnabled(bool shouldEnable)
{
    enabled = shouldEnable;
    if (enabled)
        startTimer(16);  // ~60Hz
    else
    {
        stopTimer();
        releaseAllNotes();
        connected = false;
    }
}

void GamepadHandler::cycleMode()
{
    releaseAllNotes();
    switch (currentMode)
    {
        case Mode::Navigate: currentMode = Mode::Play; break;
        case Mode::Play:     currentMode = Mode::Edit; break;
        case Mode::Edit:     currentMode = Mode::Navigate; break;
    }
}

void GamepadHandler::setScale(const juce::Array<int>& intervals) { scaleIntervals = intervals; }
void GamepadHandler::setRootNote(int midiNote) { rootNote = juce::jlimit(0, 127, midiNote); }
void GamepadHandler::setChordDegrees(int idx, const juce::Array<int>& intervals)
{
    if (idx >= 0 && idx < 4) chordDegrees.set(idx, intervals);
}

int GamepadHandler::scaleNoteAt(int degree) const
{
    if (scaleIntervals.isEmpty()) return rootNote;
    int octave = degree / scaleIntervals.size();
    int idx = degree % scaleIntervals.size();
    if (idx < 0) { idx += scaleIntervals.size(); octave--; }
    return rootNote + (currentOctaveShift + octave) * 12 + scaleIntervals[idx];
}

void GamepadHandler::releaseAllNotes()
{
    for (int note : activeNotes)
        if (onNoteOff) onNoteOff(note);
    activeNotes.clear();
    for (int note : dpadNotes)
        if (onNoteOff) onNoteOff(note);
    dpadNotes.clear();
    if (leftTriggerNoteOn && leftTriggerNote >= 0)
        if (onNoteOff) onNoteOff(leftTriggerNote);
    leftTriggerNoteOn = false;
    leftTriggerNote = -1;
}

// ── XInput ──────────────────────────────────────────────────────────────────

float GamepadHandler::applyDeadzone(float value, float dz) const
{
    if (std::abs(value) < dz) return 0.0f;
    float sign = value > 0 ? 1.0f : -1.0f;
    return sign * (std::abs(value) - dz) / (1.0f - dz);
}

bool GamepadHandler::buttonPressed(uint16_t btn) const  { return (buttons & btn) && !(prevButtons & btn); }
bool GamepadHandler::buttonReleased(uint16_t btn) const { return !(buttons & btn) && (prevButtons & btn); }
bool GamepadHandler::buttonHeld(uint16_t btn) const     { return (buttons & btn) != 0; }

void GamepadHandler::pollXInput()
{
#if JUCE_WINDOWS
    XINPUT_STATE xstate;
    ZeroMemory(&xstate, sizeof(XINPUT_STATE));
    DWORD result = XInputGetState(0, &xstate);
    connected = (result == ERROR_SUCCESS);
    if (!connected) return;

    prevButtons = buttons;
    buttons = xstate.Gamepad.wButtons;
    leftStickX  = applyDeadzone(static_cast<float>(xstate.Gamepad.sThumbLX) / 32767.0f, stickDeadzone);
    leftStickY  = applyDeadzone(static_cast<float>(xstate.Gamepad.sThumbLY) / 32767.0f, stickDeadzone);
    rightStickX = applyDeadzone(static_cast<float>(xstate.Gamepad.sThumbRX) / 32767.0f, stickDeadzone);
    rightStickY = applyDeadzone(static_cast<float>(xstate.Gamepad.sThumbRY) / 32767.0f, stickDeadzone);
    leftTrigger  = static_cast<float>(xstate.Gamepad.bLeftTrigger) / 255.0f;
    rightTrigger = static_cast<float>(xstate.Gamepad.bRightTrigger) / 255.0f;
#endif
}

// ── Timer ───────────────────────────────────────────────────────────────────

void GamepadHandler::timerCallback()
{
    if (!enabled) return;
    pollXInput();
    if (!connected) return;

#if JUCE_WINDOWS
    // Mode cycle: Back + LB (left controller combo)
    if (buttonPressed(XINPUT_GAMEPAD_BACK) && buttonHeld(XINPUT_GAMEPAD_LEFT_SHOULDER))
    {
        cycleMode();
        return;
    }
#endif

    // Always send virtual CCs for MIDI learn
    sendAxisCCs();

    // Left controller: mode-dependent DAW control
    switch (currentMode)
    {
        case Mode::Navigate: handleLeftNavigate(); break;
        case Mode::Play:     handleLeftPlay(); break;
        case Mode::Edit:     handleLeftEdit(); break;
    }

    // Right controller: always-active visualizer control
    handleRightVisuals();
}

// ── Virtual MIDI ────────────────────────────────────────────────────────────

void GamepadHandler::sendVirtualCC(int cc, int value)
{
    int idx = cc - CC_LEFT_STICK_X;
    if (idx >= 0 && idx < 18)
    {
        if (prevCCValues[idx] == value) return;
        prevCCValues[idx] = value;
    }
    auto msg = juce::MidiMessage::controllerEvent(VIRTUAL_CHANNEL, cc, value);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    if (onMidiMessage) onMidiMessage(msg);
}

void GamepadHandler::sendVirtualNoteOn(int note, float velocity)
{
    note = juce::jlimit(0, 127, note);
    activeNotes.insert(note);
    if (onNoteOn) onNoteOn(note, velocity);
}

void GamepadHandler::sendVirtualNoteOff(int note)
{
    note = juce::jlimit(0, 127, note);
    activeNotes.erase(note);
    if (onNoteOff) onNoteOff(note);
}

void GamepadHandler::sendAxisCCs()
{
    sendVirtualCC(CC_LEFT_STICK_X,  static_cast<int>((leftStickX + 1.0f) * 63.5f));
    sendVirtualCC(CC_LEFT_STICK_Y,  static_cast<int>((leftStickY + 1.0f) * 63.5f));
    sendVirtualCC(CC_RIGHT_STICK_X, static_cast<int>((rightStickX + 1.0f) * 63.5f));
    sendVirtualCC(CC_RIGHT_STICK_Y, static_cast<int>((rightStickY + 1.0f) * 63.5f));
    sendVirtualCC(CC_LEFT_TRIGGER,  static_cast<int>(leftTrigger * 127.0f));
    sendVirtualCC(CC_RIGHT_TRIGGER, static_cast<int>(rightTrigger * 127.0f));

#if JUCE_WINDOWS
    auto btnCC = [&](uint16_t btn, int cc) {
        sendVirtualCC(cc, buttonHeld(btn) ? 127 : 0);
    };
    btnCC(XINPUT_GAMEPAD_DPAD_UP,        CC_DPAD_UP);
    btnCC(XINPUT_GAMEPAD_DPAD_DOWN,      CC_DPAD_DOWN);
    btnCC(XINPUT_GAMEPAD_DPAD_LEFT,      CC_DPAD_LEFT);
    btnCC(XINPUT_GAMEPAD_DPAD_RIGHT,     CC_DPAD_RIGHT);
    btnCC(XINPUT_GAMEPAD_A,              CC_BTN_A);
    btnCC(XINPUT_GAMEPAD_B,              CC_BTN_B);
    btnCC(XINPUT_GAMEPAD_X,              CC_BTN_X);
    btnCC(XINPUT_GAMEPAD_Y,              CC_BTN_Y);
    btnCC(XINPUT_GAMEPAD_LEFT_SHOULDER,  CC_BTN_LB);
    btnCC(XINPUT_GAMEPAD_RIGHT_SHOULDER, CC_BTN_RB);
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// LEFT CONTROLLER — DAW
// ═════════════════════════════════════════════════════════════════════════════

void GamepadHandler::handleLeftNavigate()
{
#if JUCE_WINDOWS
    // D-pad: track/clip selection
    if (buttonPressed(XINPUT_GAMEPAD_DPAD_UP))    if (onTrackSelect) onTrackSelect(-1);
    if (buttonPressed(XINPUT_GAMEPAD_DPAD_DOWN))   if (onTrackSelect) onTrackSelect(1);
    if (buttonPressed(XINPUT_GAMEPAD_DPAD_LEFT))   if (onClipSelect) onClipSelect(-1);
    if (buttonPressed(XINPUT_GAMEPAD_DPAD_RIGHT))  if (onClipSelect) onClipSelect(1);

    // L-stick: scroll timeline
    if (std::abs(leftStickX) > 0.0f || std::abs(leftStickY) > 0.0f)
        if (onScroll) onScroll(leftStickX * 4.0f, leftStickY * 4.0f);

    // LT: play/stop toggle (analog threshold)
    if (leftTrigger > 0.5f && !leftTriggerNoteOn)
    {
        leftTriggerNoteOn = true;
        if (onPlay) onPlay();
    }
    else if (leftTrigger < 0.2f && leftTriggerNoteOn)
    {
        leftTriggerNoteOn = false;
    }

    // LB alone (without Back): record toggle
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_SHOULDER) && !buttonHeld(XINPUT_GAMEPAD_BACK))
        if (onRecord) onRecord();

    // Back alone: undo
    if (buttonPressed(XINPUT_GAMEPAD_BACK) && !buttonHeld(XINPUT_GAMEPAD_LEFT_SHOULDER))
        if (onUndo) onUndo();

    // LS click: stop
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_THUMB))
        if (onStop) onStop();
#endif
}

void GamepadHandler::handleLeftPlay()
{
#if JUCE_WINDOWS
    // LB: octave down (without Back held)
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_SHOULDER) && !buttonHeld(XINPUT_GAMEPAD_BACK))
        currentOctaveShift = juce::jmax(-3, currentOctaveShift - 1);

    // LS click: octave up
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_THUMB))
        currentOctaveShift = juce::jmin(3, currentOctaveShift + 1);

    // L-stick X: mod wheel (CC 1)
    {
        int modVal = static_cast<int>((leftStickX + 1.0f) * 63.5f);
        auto msg = juce::MidiMessage::controllerEvent(1, 1, modVal);
        msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        if (onMidiMessage) onMidiMessage(msg);
    }

    // L-stick Y: pitch bend
    {
        int bendVal = static_cast<int>((leftStickY + 1.0f) * 8191.5f);
        auto msg = juce::MidiMessage::pitchWheel(1, bendVal);
        msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        if (onMidiMessage) onMidiMessage(msg);
    }

    // D-pad: play scale degrees
    auto dpadNote = [&](uint16_t btn, int degree) {
        int note = scaleNoteAt(degree);
        if (buttonPressed(btn))
        {
            sendVirtualNoteOn(note, 0.8f);
            dpadNotes.insert(note);
        }
        if (buttonReleased(btn))
        {
            if (dpadNotes.count(note))
            {
                sendVirtualNoteOff(note);
                dpadNotes.erase(note);
            }
        }
    };
    dpadNote(XINPUT_GAMEPAD_DPAD_UP,    4);  // 5th
    dpadNote(XINPUT_GAMEPAD_DPAD_RIGHT, 2);  // 3rd
    dpadNote(XINPUT_GAMEPAD_DPAD_DOWN,  0);  // root
    dpadNote(XINPUT_GAMEPAD_DPAD_LEFT,  6);  // 7th

    // LT: velocity-sensitive root note
    {
        int lNote = scaleNoteAt(0);
        if (leftTrigger > triggerDeadzone && !leftTriggerNoteOn)
        {
            leftTriggerNote = lNote;
            leftTriggerNoteOn = true;
            sendVirtualNoteOn(lNote, juce::jlimit(0.1f, 1.0f, leftTrigger));
        }
        else if (leftTrigger <= triggerDeadzone && leftTriggerNoteOn)
        {
            sendVirtualNoteOff(leftTriggerNote);
            leftTriggerNoteOn = false;
            leftTriggerNote = -1;
        }
    }

    // Back alone: transport stop
    if (buttonPressed(XINPUT_GAMEPAD_BACK) && !buttonHeld(XINPUT_GAMEPAD_LEFT_SHOULDER))
        if (onStop) onStop();
#endif
}

void GamepadHandler::handleLeftEdit()
{
#if JUCE_WINDOWS
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // D-pad: move cursor in piano roll
    if (now - lastCursorMoveTime > cursorRepeatDelay)
    {
        int dNote = 0;
        double dBeat = 0.0;
        if (buttonHeld(XINPUT_GAMEPAD_DPAD_UP))    dNote = 1;
        if (buttonHeld(XINPUT_GAMEPAD_DPAD_DOWN))   dNote = -1;
        if (buttonHeld(XINPUT_GAMEPAD_DPAD_RIGHT))  dBeat = 0.25;
        if (buttonHeld(XINPUT_GAMEPAD_DPAD_LEFT))   dBeat = -0.25;
        if (dNote != 0 || dBeat != 0.0)
        {
            if (onMoveCursor) onMoveCursor(dNote, dBeat);
            lastCursorMoveTime = now;
        }
    }

    // LT: place note (analog squeeze = velocity)
    if (leftTrigger > 0.5f && !leftTriggerNoteOn)
    {
        leftTriggerNoteOn = true;
        if (onPlaceNote) onPlaceNote();
    }
    else if (leftTrigger < 0.2f && leftTriggerNoteOn)
    {
        leftTriggerNoteOn = false;
    }

    // LB: delete note
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_SHOULDER) && !buttonHeld(XINPUT_GAMEPAD_BACK))
        if (onDeleteNote) onDeleteNote();

    // L-stick Y: transpose selected note
    if (std::abs(leftStickY) > 0.5f && now - lastCursorMoveTime > cursorRepeatDelay * 2)
    {
        if (onTransposeNote) onTransposeNote(leftStickY > 0 ? 1 : -1);
        lastCursorMoveTime = now;
    }

    // L-stick X: adjust note length
    if (std::abs(leftStickX) > 0.5f && now - lastCursorMoveTime > cursorRepeatDelay * 2)
    {
        if (onAdjustLength) onAdjustLength(leftStickX > 0 ? 0.25 : -0.25);
        lastCursorMoveTime = now;
    }

    // LS click: quantize
    if (buttonPressed(XINPUT_GAMEPAD_LEFT_THUMB))
        if (onQuantizeSelected) onQuantizeSelected();

    // Back alone: undo
    if (buttonPressed(XINPUT_GAMEPAD_BACK) && !buttonHeld(XINPUT_GAMEPAD_LEFT_SHOULDER))
        if (onUndo) onUndo();
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
// RIGHT CONTROLLER — VISUALS (always active, context-aware)
// ═════════════════════════════════════════════════════════════════════════════

void GamepadHandler::handleRightVisuals()
{
#if JUCE_WINDOWS
    double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Start: cycle visualizer type
    if (buttonPressed(XINPUT_GAMEPAD_START))
        if (onVisCycleType) onVisCycleType();

    // RS click: toggle fullscreen visualizer
    if (buttonPressed(XINPUT_GAMEPAD_RIGHT_THUMB))
        if (onVisToggleFullscreen) onVisToggleFullscreen();

    // Context-aware button mapping based on active visualizer
    switch (visMode)
    {
        case 0: // Spectrum
        {
            if (buttonPressed(XINPUT_GAMEPAD_A)) if (onSpecCycleDecay) onSpecCycleDecay();
            if (buttonPressed(XINPUT_GAMEPAD_X)) if (onSpecSensUp) onSpecSensUp();
            if (buttonPressed(XINPUT_GAMEPAD_B)) if (onSpecSensDown) onSpecSensDown();
            break;
        }
        case 1: // Lissajous
        {
            if (buttonPressed(XINPUT_GAMEPAD_A)) if (onLissCycleDots) onLissCycleDots();
            if (buttonPressed(XINPUT_GAMEPAD_X)) if (onLissZoomIn) onLissZoomIn();
            if (buttonPressed(XINPUT_GAMEPAD_B)) if (onLissZoomOut) onLissZoomOut();
            break;
        }
        case 2: // G-Force
        {
            if (buttonPressed(XINPUT_GAMEPAD_A)) if (onGFCycleTrail) onGFCycleTrail();
            if (buttonPressed(XINPUT_GAMEPAD_X)) if (onGFMoreRibbons) onGFMoreRibbons();
            if (buttonPressed(XINPUT_GAMEPAD_B)) if (onGFFewerRibbons) onGFFewerRibbons();
            break;
        }
        case 3: // Geiss
        {
            if (buttonPressed(XINPUT_GAMEPAD_A)) if (onGeissCycleWave) onGeissCycleWave();
            if (buttonPressed(XINPUT_GAMEPAD_B)) if (onGeissCyclePalette) onGeissCyclePalette();
            if (buttonPressed(XINPUT_GAMEPAD_X)) if (onGeissNewScene) onGeissNewScene();
            if (buttonPressed(XINPUT_GAMEPAD_Y)) if (onGeissToggleAutoPilot) onGeissToggleAutoPilot();

            // R-stick Y: wave scale
            if (std::abs(rightStickY) > 0.3f && now - lastVisAdjustTime > 0.1)
            {
                if (onGeissWaveScale) onGeissWaveScale(rightStickY * 0.2f);
                lastVisAdjustTime = now;
            }

            // RB: toggle warp lock
            if (buttonPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER))
                if (onGeissToggleWarp) onGeissToggleWarp();
            break;
        }
        case 4: // ProjectM
        {
            if (buttonPressed(XINPUT_GAMEPAD_A)) if (onPMNext) onPMNext();
            if (buttonPressed(XINPUT_GAMEPAD_B)) if (onPMPrev) onPMPrev();
            if (buttonPressed(XINPUT_GAMEPAD_X)) if (onPMRandom) onPMRandom();
            if (buttonPressed(XINPUT_GAMEPAD_Y)) if (onPMToggleLock) onPMToggleLock();
            break;
        }
    }

    // RT: general visual intensity/sensitivity (works across all vis modes)
    // Mapped as virtual CC so it can be MIDI-learned to any vis parameter
    // (already handled by sendAxisCCs)
#endif
}

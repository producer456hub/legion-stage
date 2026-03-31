#pragma once

#include <JuceHeader.h>
#include <set>

// Gamepad handler for Legion Go's split controllers via XInput.
//
// LEFT CONTROLLER (D-pad, L-stick, LB, LT, Back, LS-click):
//   DAW control with 3 modes — Navigate, Play, Edit.
//   Mode cycle: Back + LB
//
// RIGHT CONTROLLER (A/B/X/Y, R-stick, RB, RT, Start, RS-click):
//   Visualizer control — always active, no modes.
//   Context-aware per active visualizer.
//
// All inputs also generate virtual MIDI on ch16 for MIDI learn.
class GamepadHandler : public juce::Timer
{
public:
    enum class Mode { Navigate, Play, Edit };

    GamepadHandler();
    ~GamepadHandler() override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }
    bool isConnected() const { return connected; }
    Mode getMode() const { return currentMode; }
    void cycleMode();

    // Active visualizer (set by MainComponent so right controller knows context)
    void setVisMode(int mode) { visMode = mode; }

    // ── LEFT CONTROLLER CALLBACKS (DAW) ──

    // Notes (Play mode)
    std::function<void(int note, float velocity)> onNoteOn;
    std::function<void(int note)> onNoteOff;

    // Virtual MIDI messages (for MIDI learn pipeline)
    std::function<void(juce::MidiMessage)> onMidiMessage;

    // Navigation
    std::function<void(int direction)> onTrackSelect;
    std::function<void(int direction)> onClipSelect;
    std::function<void(float dx, float dy)> onScroll;
    std::function<void(float factor)> onZoom;

    // Transport (LT combos in Nav mode)
    std::function<void()> onPlay;
    std::function<void()> onStop;
    std::function<void()> onRecord;
    std::function<void()> onUndo;
    std::function<void()> onRedo;

    // Edit mode (piano roll cursor)
    std::function<void(int dNote, double dBeat)> onMoveCursor;
    std::function<void()> onPlaceNote;
    std::function<void()> onDeleteNote;
    std::function<void(int semitones)> onTransposeNote;
    std::function<void(double beats)> onAdjustLength;
    std::function<void(int amount)> onAdjustVelocity;
    std::function<void()> onStepForward;
    std::function<void()> onStepBack;
    std::function<void()> onQuantizeSelected;

    // ── RIGHT CONTROLLER CALLBACKS (Visuals) ──

    std::function<void()> onVisCycleType;       // Start: cycle visualizer
    std::function<void()> onVisToggleFullscreen; // RS-click: toggle fullscreen
    // Geiss
    std::function<void()> onGeissCycleWave;
    std::function<void()> onGeissCyclePalette;
    std::function<void()> onGeissNewScene;
    std::function<void(float)> onGeissWaveScale; // R-stick Y
    std::function<void()> onGeissToggleWarp;
    std::function<void()> onGeissToggleAutoPilot;
    // ProjectM
    std::function<void()> onPMNext;
    std::function<void()> onPMPrev;
    std::function<void()> onPMRandom;
    std::function<void()> onPMToggleLock;
    // G-Force
    std::function<void()> onGFMoreRibbons;
    std::function<void()> onGFFewerRibbons;
    std::function<void()> onGFCycleTrail;
    // Spectrum
    std::function<void()> onSpecCycleDecay;
    std::function<void()> onSpecSensUp;
    std::function<void()> onSpecSensDown;
    // Lissajous
    std::function<void()> onLissZoomIn;
    std::function<void()> onLissZoomOut;
    std::function<void()> onLissCycleDots;

    // Scale config for Play mode
    void setScale(const juce::Array<int>& intervals);
    void setRootNote(int midiNote);
    void setChordDegrees(int buttonIndex, const juce::Array<int>& intervals);
    int getRootNote() const { return rootNote; }

    // Virtual MIDI CC constants for learn system
    static constexpr int VIRTUAL_CHANNEL = 16;
    static constexpr int CC_LEFT_STICK_X  = 102;
    static constexpr int CC_LEFT_STICK_Y  = 103;
    static constexpr int CC_RIGHT_STICK_X = 104;
    static constexpr int CC_RIGHT_STICK_Y = 105;
    static constexpr int CC_LEFT_TRIGGER  = 106;
    static constexpr int CC_RIGHT_TRIGGER = 107;
    static constexpr int CC_DPAD_UP       = 108;
    static constexpr int CC_DPAD_DOWN     = 109;
    static constexpr int CC_DPAD_LEFT     = 110;
    static constexpr int CC_DPAD_RIGHT    = 111;
    static constexpr int CC_BTN_A         = 112;
    static constexpr int CC_BTN_B         = 113;
    static constexpr int CC_BTN_X         = 114;
    static constexpr int CC_BTN_Y         = 115;
    static constexpr int CC_BTN_LB        = 116;
    static constexpr int CC_BTN_RB        = 117;

    void timerCallback() override;

private:
    bool enabled = false;
    bool connected = false;
    Mode currentMode = Mode::Navigate;
    int visMode = 0;  // 0=Spectrum, 1=Lissajous, 2=GForce, 3=Geiss, 4=ProjectM

    // XInput state
    uint16_t buttons = 0;
    uint16_t prevButtons = 0;
    float leftStickX = 0, leftStickY = 0;
    float rightStickX = 0, rightStickY = 0;
    float leftTrigger = 0, rightTrigger = 0;

    int prevCCValues[18] = {};

    static constexpr float stickDeadzone = 0.18f;
    static constexpr float triggerDeadzone = 0.08f;
    float applyDeadzone(float value, float dz) const;

    bool buttonPressed(uint16_t btn) const;
    bool buttonReleased(uint16_t btn) const;
    bool buttonHeld(uint16_t btn) const;

    void pollXInput();

    // Left controller handlers (mode-dependent)
    void handleLeftNavigate();
    void handleLeftPlay();
    void handleLeftEdit();

    // Right controller handler (always active, vis-context-aware)
    void handleRightVisuals();

    // Always-active: virtual CCs for MIDI learn
    void sendAxisCCs();

    // Play mode state
    int rootNote = 60;
    juce::Array<int> scaleIntervals { 0, 2, 4, 5, 7, 9, 11 };
    int currentOctaveShift = 0;
    std::set<int> activeNotes;
    juce::Array<juce::Array<int>> chordDegrees;

    // Trigger note tracking (Play mode uses LT only now)
    bool leftTriggerNoteOn = false;
    int leftTriggerNote = -1;

    // D-pad note tracking
    std::set<int> dpadNotes;

    // Edit mode rate limiting
    double lastCursorMoveTime = 0.0;
    static constexpr double cursorRepeatDelay = 0.15;

    // Right stick vis rate limiting
    double lastVisAdjustTime = 0.0;

    int scaleNoteAt(int degree) const;
    void releaseAllNotes();

    void sendVirtualCC(int cc, int value);
    void sendVirtualNoteOn(int note, float velocity);
    void sendVirtualNoteOff(int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GamepadHandler)
};

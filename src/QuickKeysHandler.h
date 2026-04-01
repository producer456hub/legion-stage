#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>

// Xencelabs Quick Keys HID handler.
// Provides two operating modes:
//   Solo   — emulates Keystage parameter control (8 params + wheel adjust)
//   Companion — transport, track nav, page/preset scrolling alongside Keystage
//
// Communicates directly with the device over USB HID (Windows HID API).
class QuickKeysHandler : public juce::Thread
{
public:
    // Device identifiers
    static constexpr uint16_t VENDOR_ID          = 0x28BD;
    static constexpr uint16_t PRODUCT_ID_WIRED   = 0x5202;
    static constexpr uint16_t PRODUCT_ID_WIRELESS = 0x5203;

    // HID protocol constants
    static constexpr uint8_t REPORT_ID           = 0x02;
    static constexpr uint8_t CMD_SUBSCRIBE       = 0xB0;
    static constexpr uint8_t CMD_KEY_TEXT         = 0xB1;
    static constexpr uint8_t CMD_WHEEL           = 0xB4;
    static constexpr uint8_t INPUT_REPORT_KEYS   = 0xF0;
    static constexpr uint8_t INPUT_REPORT_BATTERY = 0xF2;
    static constexpr int     HID_BUFFER_SIZE     = 64;
    static constexpr int     MAX_KEY_TEXT_CHARS   = 8;
    static constexpr int     NUM_BUTTONS          = 10;
    static constexpr int     NUM_LABELED_BUTTONS  = 8;

    // Operating modes
    enum class Mode { Solo, Companion, Edit, Extras };

    // Callback types — MainComponent wires these up
    struct Callbacks
    {
        // Transport (Companion mode)
        std::function<void()> onPlay;
        std::function<void()> onStop;
        std::function<void()> onRecord;
        std::function<void()> onLoop;
        std::function<void()> onMetronome;

        // Navigation (Companion mode)
        std::function<void()> onTrackPrev;
        std::function<void()> onTrackNext;
        std::function<void()> onFxSlotPrev;
        std::function<void()> onFxSlotNext;

        // Page/Preset (both modes)
        std::function<void()> onPageNext;
        std::function<void()> onPagePrev;
        std::function<void()> onPresetNext;
        std::function<void()> onPresetPrev;

        // Playhead scrub (Companion + Edit wheel)
        std::function<void(int direction)> onScrub;  // -1 = left, +1 = right

        // Parameter control (Solo mode)
        std::function<void(int paramIndex, float value)> onParamChange;

        // Edit mode (clip operations)
        std::function<void()> onNewClip;
        std::function<void()> onDeleteClip;
        std::function<void()> onDuplicateClip;
        std::function<void()> onSplitClip;
        std::function<void()> onEditNotes;
        std::function<void()> onQuantize;
        std::function<void()> onZoomIn;
        std::function<void()> onZoomOut;

        // Extras mode (utility)
        std::function<void()> onPanic;
        std::function<void()> onMidiLearn;
        std::function<void()> onToggleKeys;
        std::function<void()> onToggleMixer;
        std::function<void()> onCapture;
        std::function<void()> onCountIn;
        std::function<void()> onUndo;
        std::function<void()> onRedo;
        std::function<void(int delta)> onBpmChange;  // Extras wheel

        // Status feedback — called on message thread
        std::function<void(const juce::String& text)> onStatus;
    };

    QuickKeysHandler();
    ~QuickKeysHandler() override;

    // Connect/disconnect
    bool openDevice();
    void closeDevice();
    bool isDeviceConnected() const { return deviceConnected.load(); }

    // Mode
    void setMode(Mode m);
    Mode getMode() const { return currentMode.load(); }
    void toggleMode();

    // Set callbacks
    void setCallbacks(const Callbacks& cb) { callbacks = cb; }

    // Update display from the DAW side
    // Solo mode: param names + current values from plugin mappings
    void setParamNames(const juce::StringArray& names, int selectedIndex);
    void setParamValues(const juce::Array<float>& values);  // sync wheel positions to plugin state
    // Solo mode: update overlay with current value text
    void showParamValue(const juce::String& text);
    // Companion mode: update page label on button 7
    void setPageLabel(const juce::String& text);
    // Set transport state (controls wheel LED color)
    void setTransportState(bool playing, bool recording);

    // Battery level (0-100, -1 if unknown/wired)
    int getBatteryLevel() const { return batteryLevel.load(); }

private:
    // Thread override — polls HID input
    void run() override;

    // HID communication
    void sendHidReport(const uint8_t* data, int size);
    void subscribeToEvents();
    void setKeyText(int keyIndex, const juce::String& text);
    void setWheelColor(uint8_t r, uint8_t g, uint8_t b);
    void setDisplayBrightness(int level); // 0=off, 1=low, 2=med, 3=full
    void showOverlayText(const juce::String& text, int durationSeconds = 2);

    // Input processing
    void processInputReport(const uint8_t* data, int size);
    void handleButtonPress(int buttonIndex);
    void handleButtonRelease(int buttonIndex);
    void handleWheel(int direction); // -1 = left, +1 = right

    // Mode-specific logic
    void handleSoloButton(int buttonIndex);
    void handleCompanionButton(int buttonIndex);
    void handleEditButton(int buttonIndex);
    void handleExtrasButton(int buttonIndex);
    void handleSoloWheel(int direction);
    void handleCompanionWheel(int direction);
    void handleEditWheel(int direction);
    void handleExtrasWheel(int direction);

    // Display updates
    void refreshDisplay();
    void refreshSoloDisplay();
    void refreshCompanionDisplay();
    void refreshEditDisplay();
    void refreshExtrasDisplay();

    // State
    std::atomic<Mode> currentMode { Mode::Solo };
    std::atomic<bool> deviceConnected { false };
    std::atomic<int> batteryLevel { -1 };
    std::atomic<bool> pendingDisplayRefresh { false };

    void* deviceHandle = nullptr;      // HANDLE for writes (sendHidReport)
    void* deviceHandleRead = nullptr;  // HANDLE for reads (run() thread only)

    // Button state tracking
    bool buttonStates[NUM_BUTTONS] = {};
    bool shiftHeld = false; // Key 8 held

    // Solo mode state
    int selectedParamIndex = 0;
    float paramValues[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    juce::StringArray currentParamNames;

    // Wheel sensitivity
    static constexpr float WHEEL_STEP = 1.0f / 64.0f; // ~2% per click

    // Rate-limiting for HID display updates during wheel turns
    juce::int64 lastWheelColorSentMs = 0;
    static constexpr juce::int64 WHEEL_COLOR_RATE_MS = 80; // max ~12 updates/sec

    // Device ID bytes (from protocol)
    uint8_t deviceId[6] = { 0xEB, 0x4F, 0x49, 0xBD, 0xD7, 0xFA };

    Callbacks callbacks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickKeysHandler)
};

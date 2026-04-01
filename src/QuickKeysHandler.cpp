#include "QuickKeysHandler.h"

// Windows HID headers
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <setupapi.h>
  #include <hidsdi.h>
  #pragma comment(lib, "hid.lib")
  #pragma comment(lib, "setupapi.lib")
#endif

QuickKeysHandler::QuickKeysHandler()
    : Thread("QuickKeysHID")
{
}

QuickKeysHandler::~QuickKeysHandler()
{
    closeDevice();
}

// ── Device Connection ───────────────────────────────────────────────────────

bool QuickKeysHandler::openDevice()
{
#ifdef _WIN32
    juce::File logFile("C:/dev/sequencer/qk-debug.log");
    logFile.replaceWithText("QuickKeys openDevice() starting\n");

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devInfo == INVALID_HANDLE_VALUE)
    {
        logFile.appendText("SetupDiGetClassDevs FAILED\n");
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    int xencelabsCount = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &interfaceData); ++i)
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

        auto detailBuf = std::make_unique<uint8_t[]>(requiredSize);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.get());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detail, requiredSize, nullptr, nullptr))
            continue;

        HANDLE h = CreateFileW(detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        if (h == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(h, &attrs))
        {
            if (attrs.VendorID == VENDOR_ID &&
                (attrs.ProductID == PRODUCT_ID_WIRED || attrs.ProductID == PRODUCT_ID_WIRELESS))
            {
                xencelabsCount++;

                PHIDP_PREPARSED_DATA preparsed = nullptr;
                if (HidD_GetPreparsedData(h, &preparsed))
                {
                    HIDP_CAPS caps;
                    if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
                    {
                        logFile.appendText("Xencelabs interface #" + juce::String(xencelabsCount)
                            + " PID=0x" + juce::String::toHexString(attrs.ProductID)
                            + " UsagePage=0x" + juce::String::toHexString(caps.UsagePage)
                            + " Usage=0x" + juce::String::toHexString(caps.Usage)
                            + " InputLen=" + juce::String(caps.InputReportByteLength)
                            + " OutputLen=" + juce::String(caps.OutputReportByteLength)
                            + " FeatureLen=" + juce::String(caps.FeatureReportByteLength)
                            + "\n");

                        // We need the interface with output report length >= 32
                        if (caps.OutputReportByteLength >= HID_BUFFER_SIZE)
                        {
                            HidD_FreePreparsedData(preparsed);

                            deviceHandle     = h;
                            deviceHandleRead = h;  // same handle — writes only from HID thread
                            deviceConnected.store(true);

                            logFile.appendText("SELECTED interface #" + juce::String(xencelabsCount) + " — sending subscribe\n");

                            // Subscribe to key events and set up display
                            subscribeToEvents();
                            setDisplayBrightness(3); // Full brightness
                            refreshDisplay();

                            // Start the input polling thread
                            startThread(juce::Thread::Priority::normal);

                            SetupDiDestroyDeviceInfoList(devInfo);

                            if (callbacks.onStatus)
                            {
                                juce::MessageManager::callAsync([this] {
                                    if (callbacks.onStatus)
                                        callbacks.onStatus("Quick Keys connected");
                                });
                            }
                            return true;
                        }
                    }
                    HidD_FreePreparsedData(preparsed);
                }
                else
                {
                    logFile.appendText("Xencelabs interface — GetPreparsedData FAILED\n");
                }
            }
        }

        CloseHandle(h);
    }

    logFile.appendText("Done scanning. Found " + juce::String(xencelabsCount) + " Xencelabs interfaces, none matched.\n");
    SetupDiDestroyDeviceInfoList(devInfo);
#endif
    return false;
}

void QuickKeysHandler::closeDevice()
{
    stopThread(1000);

#ifdef _WIN32
    if (deviceHandle != nullptr && deviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(static_cast<HANDLE>(deviceHandle));

    // Only close the read handle if it's a separate handle (not the same as write)
    // deviceHandleRead is always the same as deviceHandle, so don't double-close
#endif

    deviceHandle = nullptr;
    deviceHandleRead = nullptr;
    deviceConnected.store(false);
}

// ── Thread: HID Input Polling ───────────────────────────────────────────────

void QuickKeysHandler::run()
{
    // Input reports are 10 bytes per the device caps
    static constexpr int INPUT_BUF_SIZE = 64;
    uint8_t buffer[INPUT_BUF_SIZE];

    juce::File logFile("C:/dev/sequencer/qk-debug.log");
    logFile.appendText("Read thread started\n");

    while (!threadShouldExit())
    {
#ifdef _WIN32
        HANDLE h = static_cast<HANDLE>(deviceHandleRead);
        if (h == nullptr || h == INVALID_HANDLE_VALUE)
        {
            Thread::sleep(100);
            continue;
        }

        DWORD bytesRead = 0;
        memset(buffer, 0, sizeof(buffer));

        BOOL result = ReadFile(h, buffer, INPUT_BUF_SIZE, &bytesRead, nullptr);

        if (!result || bytesRead == 0)
        {
            DWORD err = GetLastError();
            logFile.appendText("ReadFile failed: err=" + juce::String((int)err)
                + " bytesRead=" + juce::String((int)bytesRead) + "\n");

            if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_INVALID_HANDLE)
            {
                deviceConnected.store(false);
                if (callbacks.onStatus)
                {
                    juce::MessageManager::callAsync([this] {
                        if (callbacks.onStatus)
                            callbacks.onStatus("Quick Keys disconnected");
                    });
                }
                break;
            }
            Thread::sleep(10);
            continue;
        }

        // Log first few input reports for debugging
        {
            static int inputLogCount = 0;
            if (inputLogCount < 20)
            {
                juce::String hex;
                for (DWORD b = 0; b < bytesRead && b < 16; ++b)
                    hex += juce::String::toHexString(buffer[b]) + " ";
                logFile.appendText("READ [" + juce::String((int)bytesRead) + "b]: " + hex + "\n");
                inputLogCount++;
            }
        }

        processInputReport(buffer, static_cast<int>(bytesRead));

        // Drain any pending display refresh requested by the message thread
        if (pendingDisplayRefresh.exchange(false))
            refreshDisplay();
#else
        Thread::sleep(100); // Non-Windows placeholder
#endif
    }

    logFile.appendText("Read thread exiting\n");
}

// ── HID Output ──────────────────────────────────────────────────────────────

void QuickKeysHandler::sendHidReport(const uint8_t* data, int size)
{
#ifdef _WIN32
    if (deviceHandle == nullptr || deviceHandle == INVALID_HANDLE_VALUE)
        return;

    // Ensure we always send a full 64-byte report
    uint8_t fullReport[HID_BUFFER_SIZE] = {};
    int copyLen = juce::jmin(size, HID_BUFFER_SIZE);
    memcpy(fullReport, data, static_cast<size_t>(copyLen));

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(static_cast<HANDLE>(deviceHandle), fullReport,
                        HID_BUFFER_SIZE, &bytesWritten, nullptr);

    // Debug log
    juce::String hex;
    for (int i = 0; i < juce::jmin(20, HID_BUFFER_SIZE); ++i)
        hex += juce::String::toHexString(fullReport[i]) + " ";
    juce::File("C:/dev/sequencer/qk-debug.log").appendText(
        "SEND [" + juce::String(bytesWritten) + "b] ok=" + juce::String((int)ok)
        + " err=" + juce::String((int)GetLastError())
        + " : " + hex + "...\n");
#else
    juce::ignoreUnused(data, size);
#endif
}

void QuickKeysHandler::subscribeToEvents()
{
    uint8_t buf[HID_BUFFER_SIZE] = {};
    buf[0] = REPORT_ID;
    buf[1] = CMD_SUBSCRIBE;
    buf[2] = 0x04;
    // Device ID at offset 10
    memcpy(&buf[10], deviceId, 6);
    sendHidReport(buf, HID_BUFFER_SIZE);
}

void QuickKeysHandler::setKeyText(int keyIndex, const juce::String& text)
{
    if (keyIndex < 0 || keyIndex >= NUM_LABELED_BUTTONS)
        return;

    uint8_t buf[HID_BUFFER_SIZE] = {};
    buf[0] = REPORT_ID;
    buf[1] = CMD_KEY_TEXT;
    buf[2] = 0x00;
    buf[3] = static_cast<uint8_t>(keyIndex + 1); // 1-based
    buf[4] = 0x00;

    // Encode text as UTF-16LE at offset 16, max 8 chars
    auto trimmed = text.substring(0, MAX_KEY_TEXT_CHARS);
    int charCount = trimmed.length();
    buf[5] = static_cast<uint8_t>(charCount * 2); // byte length

    // Device ID at offset 10
    memcpy(&buf[10], deviceId, 6);

    // UTF-16LE at offset 16
    for (int i = 0; i < charCount && (16 + i * 2 + 1) < HID_BUFFER_SIZE; ++i)
    {
        uint16_t ch = static_cast<uint16_t>(trimmed[i]);
        buf[16 + i * 2]     = static_cast<uint8_t>(ch & 0xFF);
        buf[16 + i * 2 + 1] = static_cast<uint8_t>((ch >> 8) & 0xFF);
    }

    sendHidReport(buf, HID_BUFFER_SIZE);
}

void QuickKeysHandler::setWheelColor(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t buf[HID_BUFFER_SIZE] = {};
    buf[0] = REPORT_ID;
    buf[1] = CMD_WHEEL;
    buf[2] = 0x01;
    buf[3] = 0x01;
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = r;
    buf[7] = g;
    buf[8] = b;
    memcpy(&buf[10], deviceId, 6);
    sendHidReport(buf, HID_BUFFER_SIZE);
}

void QuickKeysHandler::setDisplayBrightness(int level)
{
    uint8_t buf[HID_BUFFER_SIZE] = {};
    buf[0] = REPORT_ID;
    buf[1] = CMD_KEY_TEXT;
    buf[2] = 0x0A;
    buf[3] = 0x01;
    buf[4] = 0x03;
    buf[5] = static_cast<uint8_t>(juce::jlimit(0, 3, level));
    memcpy(&buf[10], deviceId, 6);
    sendHidReport(buf, HID_BUFFER_SIZE);
}

void QuickKeysHandler::showOverlayText(const juce::String& text, int durationSeconds)
{
    // Overlay text supports up to 32 chars across multiple 8-char packets
    auto trimmed = text.substring(0, 32);
    int totalChars = trimmed.length();
    int offset = 0;

    while (offset < totalChars)
    {
        int charsThisPacket = juce::jmin(MAX_KEY_TEXT_CHARS, totalChars - offset);
        bool isFirst = (offset == 0);
        bool hasMore = (offset + charsThisPacket < totalChars);

        uint8_t buf[HID_BUFFER_SIZE] = {};
        buf[0] = REPORT_ID;
        buf[1] = CMD_KEY_TEXT;
        buf[2] = isFirst ? static_cast<uint8_t>(0x05) : static_cast<uint8_t>(0x06);
        buf[3] = static_cast<uint8_t>(juce::jlimit(1, 255, durationSeconds));
        buf[4] = 0x00;
        buf[5] = static_cast<uint8_t>(charsThisPacket * 2);
        buf[6] = hasMore ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00);

        memcpy(&buf[10], deviceId, 6);

        for (int i = 0; i < charsThisPacket && (16 + i * 2 + 1) < HID_BUFFER_SIZE; ++i)
        {
            uint16_t ch = static_cast<uint16_t>(trimmed[offset + i]);
            buf[16 + i * 2]     = static_cast<uint8_t>(ch & 0xFF);
            buf[16 + i * 2 + 1] = static_cast<uint8_t>((ch >> 8) & 0xFF);
        }

        sendHidReport(buf, HID_BUFFER_SIZE);
        offset += charsThisPacket;
    }
}

// ── Input Processing ────────────────────────────────────────────────────────

void QuickKeysHandler::processInputReport(const uint8_t* data, int size)
{
    if (size < 3) return;

    // Actual format from device:
    //   Byte 0: 0x02 (report ID)
    //   Byte 1: sub-type (0xF0 = keys/wheel, 0xF2 = battery)
    //   Byte 2-3: 16-bit key bitmask (for 0xF0)
    //   Byte 7: wheel direction (for 0xF0)

    uint8_t subType = data[1];

    if (subType == INPUT_REPORT_KEYS)
    {
        // Bytes 2-3: 16-bit key bitmask
        uint16_t keyMask = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);

        // Check each button for state changes
        for (int i = 0; i < NUM_BUTTONS; ++i)
        {
            bool pressed = (keyMask & (1 << i)) != 0;
            if (pressed && !buttonStates[i])
                handleButtonPress(i);
            else if (!pressed && buttonStates[i])
                handleButtonRelease(i);
            buttonStates[i] = pressed;
        }

        // Byte 7: wheel direction
        if (size > 7)
        {
            uint8_t wheel = data[7];
            if (wheel == 0x01)
                handleWheel(1);  // right
            else if (wheel == 0x02)
                handleWheel(-1); // left
        }
    }
    else if (subType == INPUT_REPORT_BATTERY)
    {
        if (size >= 4)
            batteryLevel.store(static_cast<int>(data[3]));
    }
    // Ignore unknown sub-types silently
}

void QuickKeysHandler::handleButtonPress(int buttonIndex)
{
    // Key 8 = shift modifier
    if (buttonIndex == 8)
    {
        shiftHeld = true;
        return;
    }

    // Key 9 = mode toggle
    if (buttonIndex == 9)
    {
        toggleMode();
        return;
    }

    // Buttons 0-7: mode-specific
    switch (currentMode.load())
    {
        case Mode::Solo:      handleSoloButton(buttonIndex);      break;
        case Mode::Companion: handleCompanionButton(buttonIndex); break;
        case Mode::Edit:      handleEditButton(buttonIndex);      break;
        case Mode::Extras:    handleExtrasButton(buttonIndex);    break;
    }
}

void QuickKeysHandler::handleButtonRelease(int buttonIndex)
{
    if (buttonIndex == 8)
        shiftHeld = false;
}

void QuickKeysHandler::handleWheel(int direction)
{
    switch (currentMode.load())
    {
        case Mode::Solo:      handleSoloWheel(direction);      break;
        case Mode::Companion: handleCompanionWheel(direction); break;
        case Mode::Edit:      handleEditWheel(direction);      break;
        case Mode::Extras:    handleExtrasWheel(direction);    break;
    }
}

// ── Solo Mode ───────────────────────────────────────────────────────────────

void QuickKeysHandler::handleSoloButton(int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= NUM_LABELED_BUTTONS)
        return;

    if (shiftHeld)
    {
        // Shift + button: reserved for future use
        return;
    }

    // Select this parameter for wheel control
    selectedParamIndex = buttonIndex;
    refreshSoloDisplay();
}

void QuickKeysHandler::handleSoloWheel(int direction)
{
    if (shiftHeld)
    {
        // Shift + wheel = page navigation
        if (direction > 0 && callbacks.onPageNext)
        {
            juce::MessageManager::callAsync([this] { callbacks.onPageNext(); });
        }
        else if (direction < 0 && callbacks.onPagePrev)
        {
            juce::MessageManager::callAsync([this] { callbacks.onPagePrev(); });
        }
        return;
    }

    // Normal wheel = adjust selected parameter
    if (selectedParamIndex >= 0 && selectedParamIndex < 8)
    {
        float& val = paramValues[selectedParamIndex];
        val = juce::jlimit(0.0f, 1.0f, val + direction * WHEEL_STEP);

        if (callbacks.onParamChange)
        {
            float capturedVal = val;
            int capturedIdx = selectedParamIndex;
            juce::MessageManager::callAsync([this, capturedIdx, capturedVal] {
                if (callbacks.onParamChange)
                    callbacks.onParamChange(capturedIdx, capturedVal);
            });
        }

        // Update the selected button label to show current value — rate-limited
        juce::int64 now = juce::Time::currentTimeMillis();
        if (now - lastWheelColorSentMs >= WHEEL_COLOR_RATE_MS)
        {
            lastWheelColorSentMs = now;

            // Show value in the selected button's label, e.g. "[50%]"
            juce::String valStr = juce::String(static_cast<int>(val * 100.0f)) + "%";
            setKeyText(selectedParamIndex, "[" + valStr + "]");

            // Update wheel LED color
            uint8_t r = static_cast<uint8_t>(juce::jmin(255.0f, val * 2.0f * 255.0f));
            uint8_t g = static_cast<uint8_t>(juce::jmin(255.0f, (1.0f - std::abs(val - 0.5f) * 2.0f) * 255.0f));
            uint8_t b = static_cast<uint8_t>(juce::jmin(255.0f, (1.0f - val) * 2.0f * 255.0f));
            setWheelColor(r, g, b);
        }
    }
}

// ── Companion Mode ──────────────────────────────────────────────────────────

void QuickKeysHandler::handleCompanionButton(int buttonIndex)
{
    if (shiftHeld)
    {
        // Shift combos
        switch (buttonIndex)
        {
            case 5: // Shift+5 = prev FX slot
                if (callbacks.onFxSlotPrev)
                    juce::MessageManager::callAsync([this] { callbacks.onFxSlotPrev(); });
                break;
            case 6: // Shift+6 = next FX slot
                if (callbacks.onFxSlotNext)
                    juce::MessageManager::callAsync([this] { callbacks.onFxSlotNext(); });
                break;
            case 7: // Shift+7 = prev page (instead of next)
                if (callbacks.onPagePrev)
                    juce::MessageManager::callAsync([this] { callbacks.onPagePrev(); });
                break;
            default:
                break;
        }
        return;
    }

    // Normal button presses
    switch (buttonIndex)
    {
        case 0: // PLAY
            if (callbacks.onPlay)
                juce::MessageManager::callAsync([this] { callbacks.onPlay(); });
            break;
        case 1: // STOP
            if (callbacks.onStop)
                juce::MessageManager::callAsync([this] { callbacks.onStop(); });
            break;
        case 2: // REC
            if (callbacks.onRecord)
                juce::MessageManager::callAsync([this] { callbacks.onRecord(); });
            break;
        case 3: // LOOP
            if (callbacks.onLoop)
                juce::MessageManager::callAsync([this] { callbacks.onLoop(); });
            break;
        case 4: // METRO
            if (callbacks.onMetronome)
                juce::MessageManager::callAsync([this] { callbacks.onMetronome(); });
            break;
        case 5: // <<TRK
            if (callbacks.onTrackPrev)
                juce::MessageManager::callAsync([this] { callbacks.onTrackPrev(); });
            break;
        case 6: // TRK>>
            if (callbacks.onTrackNext)
                juce::MessageManager::callAsync([this] { callbacks.onTrackNext(); });
            break;
        case 7: // Next page
            if (callbacks.onPageNext)
                juce::MessageManager::callAsync([this] { callbacks.onPageNext(); });
            break;
        default:
            break;
    }
}

void QuickKeysHandler::handleCompanionWheel(int direction)
{
    // Normal wheel = scrub playhead
    if (callbacks.onScrub)
    {
        int dir = direction;
        juce::MessageManager::callAsync([this, dir] {
            if (callbacks.onScrub)
                callbacks.onScrub(dir);
        });
    }
}

// ── Edit Mode ───────────────────────────────────────────────────────────────

void QuickKeysHandler::handleEditButton(int buttonIndex)
{
    auto fire = [this](std::function<void()>& cb) {
        if (cb) juce::MessageManager::callAsync([this, &cb] { if (cb) cb(); });
    };
    switch (buttonIndex)
    {
        case 0: fire(callbacks.onNewClip);       break;
        case 1: fire(callbacks.onDeleteClip);    break;
        case 2: fire(callbacks.onDuplicateClip); break;
        case 3: fire(callbacks.onSplitClip);     break;
        case 4: fire(callbacks.onEditNotes);     break;
        case 5: fire(callbacks.onQuantize);      break;
        case 6: fire(callbacks.onZoomIn);        break;
        case 7: fire(callbacks.onZoomOut);       break;
        default: break;
    }
}

void QuickKeysHandler::handleEditWheel(int direction)
{
    // Wheel scrolls the playhead, same as transport mode
    if (callbacks.onScrub)
    {
        int dir = direction;
        juce::MessageManager::callAsync([this, dir] { if (callbacks.onScrub) callbacks.onScrub(dir); });
    }
}

void QuickKeysHandler::refreshEditDisplay()
{
    setKeyText(0, "NewClp");
    setKeyText(1, "Delete");
    setKeyText(2, "Dupl");
    setKeyText(3, "Split");
    setKeyText(4, "EditNt");
    setKeyText(5, "Quant");
    setKeyText(6, "Zoom+");
    setKeyText(7, "Zoom-");
    setWheelColor(255, 165, 0);  // Orange for edit mode
}

// ── Extras Mode ─────────────────────────────────────────────────────────────

void QuickKeysHandler::handleExtrasButton(int buttonIndex)
{
    auto fire = [this](std::function<void()>& cb) {
        if (cb) juce::MessageManager::callAsync([this, &cb] { if (cb) cb(); });
    };
    switch (buttonIndex)
    {
        case 0: fire(callbacks.onPanic);       break;
        case 1: fire(callbacks.onMidiLearn);   break;
        case 2: fire(callbacks.onToggleKeys);  break;
        case 3: fire(callbacks.onToggleMixer); break;
        case 4: fire(callbacks.onCapture);     break;
        case 5: fire(callbacks.onCountIn);     break;
        case 6: fire(callbacks.onUndo);        break;
        case 7: fire(callbacks.onRedo);        break;
        default: break;
    }
}

void QuickKeysHandler::handleExtrasWheel(int direction)
{
    // Wheel adjusts BPM
    if (callbacks.onBpmChange)
    {
        int dir = direction;
        juce::MessageManager::callAsync([this, dir] { if (callbacks.onBpmChange) callbacks.onBpmChange(dir); });
    }
}

void QuickKeysHandler::refreshExtrasDisplay()
{
    setKeyText(0, "PANIC");
    setKeyText(1, "LEARN");
    setKeyText(2, "KEYS");
    setKeyText(3, "MIX");
    setKeyText(4, "CAPT");
    setKeyText(5, "CNTIN");
    setKeyText(6, "Undo");
    setKeyText(7, "Redo");
    setWheelColor(180, 0, 255);  // Purple for extras mode
}

// ── Mode Switching ──────────────────────────────────────────────────────────

void QuickKeysHandler::setMode(Mode m)
{
    currentMode.store(m);
    refreshDisplay();

    juce::String modeName = (m == Mode::Solo) ? "Solo" : "Companion";

    if (callbacks.onStatus)
    {
        juce::MessageManager::callAsync([this, modeName] {
            if (callbacks.onStatus)
                callbacks.onStatus("Quick Keys: " + modeName + " mode");
        });
    }
}

void QuickKeysHandler::toggleMode()
{
    switch (currentMode.load())
    {
        case Mode::Solo:      setMode(Mode::Companion); break;
        case Mode::Companion: setMode(Mode::Edit);      break;
        case Mode::Edit:      setMode(Mode::Extras);    break;
        case Mode::Extras:    setMode(Mode::Solo);      break;
    }
}

// ── Display Updates ─────────────────────────────────────────────────────────

void QuickKeysHandler::refreshDisplay()
{
    if (!deviceConnected.load()) return;

    switch (currentMode.load())
    {
        case Mode::Solo:      refreshSoloDisplay();      break;
        case Mode::Companion: refreshCompanionDisplay(); break;
        case Mode::Edit:      refreshEditDisplay();      break;
        case Mode::Extras:    refreshExtrasDisplay();    break;
    }
}

void QuickKeysHandler::refreshSoloDisplay()
{
    for (int i = 0; i < NUM_LABELED_BUTTONS; ++i)
    {
        juce::String label;
        if (i < currentParamNames.size())
            label = currentParamNames[i];
        else
            label = "P" + juce::String(i + 1);

        // Bracket the selected param
        if (i == selectedParamIndex)
            label = "[" + label.substring(0, 6) + "]";

        setKeyText(i, label);
    }

    // Set wheel color based on selected param value
    int safeIdx = juce::jlimit(0, 7, selectedParamIndex);
    float val = paramValues[safeIdx];
    uint8_t r = static_cast<uint8_t>(juce::jmin(255.0f, val * 2.0f * 255.0f));
    uint8_t g = static_cast<uint8_t>(juce::jmin(255.0f, (1.0f - std::abs(val - 0.5f) * 2.0f) * 255.0f));
    uint8_t b = static_cast<uint8_t>(juce::jmin(255.0f, (1.0f - val) * 2.0f * 255.0f));
    setWheelColor(r, g, b);
}

void QuickKeysHandler::refreshCompanionDisplay()
{
    setKeyText(0, "PLAY");
    setKeyText(1, "STOP");
    setKeyText(2, "REC");
    setKeyText(3, "LOOP");
    setKeyText(4, "METRO");
    setKeyText(5, "<<TRK");
    setKeyText(6, "TRK>>");
    setKeyText(7, "PG");  // Will be updated by setPageLabel()

    // Green wheel in transport mode
    setWheelColor(30, 255, 30);
}

// ── Public API for DAW Integration ──────────────────────────────────────────

void QuickKeysHandler::setParamNames(const juce::StringArray& names, int selectedIndex)
{
    currentParamNames = names;
    selectedParamIndex = juce::jlimit(0, 7, selectedIndex);
    if (currentMode.load() == Mode::Solo && deviceConnected.load())
        pendingDisplayRefresh.store(true);
}

void QuickKeysHandler::setParamValues(const juce::Array<float>& values)
{
    for (int i = 0; i < 8 && i < values.size(); ++i)
        paramValues[i] = juce::jlimit(0.0f, 1.0f, values[i]);
}

void QuickKeysHandler::showParamValue(const juce::String& text)
{
    if (deviceConnected.load())
        showOverlayText(text, 2);
}

void QuickKeysHandler::setPageLabel(const juce::String& text)
{
    if (currentMode.load() == Mode::Companion && deviceConnected.load())
        setKeyText(7, text);
}

void QuickKeysHandler::setTransportState(bool playing, bool recording)
{
    if (!deviceConnected.load()) return;
    if (currentMode.load() != Mode::Companion) return;

    if (recording)
        setWheelColor(255, 30, 30);   // Red
    else if (playing)
        setWheelColor(30, 255, 30);   // Green
    else
        setWheelColor(0, 100, 255);   // Blue
}

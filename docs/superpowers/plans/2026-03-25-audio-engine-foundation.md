# Audio Engine Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get a JUCE standalone app running with ASIO audio output and a test tone to prove the full audio chain works.

**Architecture:** JUCE standalone app using AudioAppComponent. CMake build system with JUCE as git submodule. Minimal UI with audio device selection and test tone toggle.

**Tech Stack:** C++17, JUCE 7.0.12, CMake, Visual Studio 2019, ASIO SDK 2.3.4

**Spec:** `docs/superpowers/specs/2026-03-25-audio-engine-foundation-design.md`

---

## File Structure

```
C:/dev/sequencer/
  CMakeLists.txt          — CMake config: JUCE subdir, app target, ASIO flags
  .gitignore              — build artifacts, VS files
  libs/
    JUCE/                 — git submodule (already set up)
    asio-sdk/             — ASIO SDK (already in place, not tracked in git)
  src/
    Main.cpp              — JUCEApplication + MainWindow boilerplate
    MainComponent.h       — AudioAppComponent declaration
    MainComponent.cpp     — Audio device setup, test tone, UI
```

---

### Task 1: JUCE Submodule + .gitignore

**Files:**
- Create: `C:/dev/sequencer/.gitignore`

- [ ] **Step 1: Add JUCE as git submodule**

```bash
cd /c/dev/sequencer
git submodule add https://github.com/juce-framework/JUCE.git libs/JUCE
cd libs/JUCE && git checkout 7.0.12 && cd ../..
```

- [ ] **Step 2: Create .gitignore**

```
build/
.vs/
*.user
*.suo
CMakeSettings.json
out/
libs/asio-sdk/
```

Note: `libs/JUCE/` is tracked as a submodule, NOT ignored. `libs/asio-sdk/` is ignored because the ASIO SDK license prohibits redistribution.

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add .gitignore .gitmodules libs/JUCE
git commit -m "chore: add JUCE 7.0.12 submodule and .gitignore"
```

---

### Task 2: CMakeLists.txt

**Files:**
- Create: `C:/dev/sequencer/CMakeLists.txt`

- [ ] **Step 1: Write CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.22)
project(Sequencer VERSION 0.1.0)

# ASIO SDK path — JUCE needs this to compile ASIO support
set(JUCE_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk" CACHE PATH "ASIO SDK location")

add_subdirectory(libs/JUCE)

juce_add_gui_app(Sequencer
    PRODUCT_NAME "Sequencer"
    COMPANY_NAME "Dev"
)

target_sources(Sequencer PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp
)

target_compile_definitions(Sequencer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
)

# Point JUCE to the ASIO SDK headers
target_include_directories(Sequencer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk/common
)

target_link_libraries(Sequencer PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add CMakeLists.txt
git commit -m "chore: add CMakeLists.txt with JUCE and ASIO config"
```

---

### Task 3: Main.cpp — App Entry Point

**Files:**
- Create: `C:/dev/sequencer/src/Main.cpp`

- [ ] **Step 1: Create src directory and write Main.cpp**

```bash
mkdir -p /c/dev/sequencer/src
```

```cpp
#include <JuceHeader.h>
#include "MainComponent.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name)
        : DocumentWindow(name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour(ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class SequencerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Sequencer"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SequencerApplication)
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/Main.cpp
git commit -m "feat: add JUCE application entry point"
```

---

### Task 4: MainComponent — Header

**Files:**
- Create: `C:/dev/sequencer/src/MainComponent.h`

- [ ] **Step 1: Write MainComponent.h**

```cpp
#pragma once

#include <JuceHeader.h>
#include <atomic>

class MainComponent : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    // AudioAppComponent overrides
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::ToggleButton testToneButton    { "Test Tone" };
    juce::Label statusLabel;

    std::atomic<bool> testToneEnabled { false };
    double currentSampleRate = 0.0;
    double phase = 0.0;

    void showAudioSettings();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.h
git commit -m "feat: add MainComponent header"
```

---

### Task 5: MainComponent — Implementation

**Files:**
- Create: `C:/dev/sequencer/src/MainComponent.cpp`

- [ ] **Step 1: Write MainComponent.cpp**

```cpp
#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(testToneButton);
    testToneButton.onClick = [this] { testToneEnabled.store(testToneButton.getToggleState()); };

    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setText("No audio device", juce::dontSendNotification);

    setSize(800, 600);
    setAudioChannels(0, 2);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate)
{
    currentSampleRate = sampleRate;
    phase = 0.0;

    // prepareToPlay runs on the audio thread — must bounce UI update to message thread
    juce::MessageManager::callAsync([this] { updateStatusLabel(); });
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (!testToneEnabled.load())
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    const double frequency = 440.0;
    const double amplitude = 0.25;
    const double phaseIncrement = juce::MathConstants<double>::twoPi * frequency / currentSampleRate;

    auto* buffer = bufferToFill.buffer;
    const int numSamples = bufferToFill.numSamples;
    const int startSample = bufferToFill.startSample;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float value = static_cast<float>(std::sin(phase) * amplitude);
        phase += phaseIncrement;

        if (phase >= juce::MathConstants<double>::twoPi)
            phase -= juce::MathConstants<double>::twoPi;

        for (int channel = 0; channel < buffer->getNumChannels(); ++channel)
            buffer->setSample(channel, startSample + sample, value);
    }
}

void MainComponent::releaseResources()
{
    // Nothing to release
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    audioSettingsButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);

    testToneButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    statusLabel.setBounds(area.removeFromTop(30));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 0,    // min/max input channels
        1, 2,    // min/max output channels
        false,   // show MIDI inputs
        false,   // show MIDI outputs
        false,   // show channels as stereo pairs
        false    // hide advanced options
    );

    selector->setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Audio Settings";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();

    // Update status after dialog closes
    juce::Timer::callAfterDelay(500, [this] { updateStatusLabel(); });
}

void MainComponent::updateStatusLabel()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        juce::String text = device->getName()
                          + " | " + juce::String(device->getCurrentSampleRate(), 0) + " Hz"
                          + " | " + juce::String(device->getCurrentBufferSizeSamples()) + " samples";
        statusLabel.setText(text, juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText("No audio device", juce::dontSendNotification);
    }
}
```

- [ ] **Step 2: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.cpp
git commit -m "feat: add MainComponent with audio device setup and test tone"
```

---

### Task 6: Build and Test

**Files:**
- No new files — build and verify existing code

- [ ] **Step 1: Configure CMake**

```bash
cd /c/dev/sequencer
cmake -B build -G "Visual Studio 16 2019" -A x64
```

Expected: CMake configures successfully, finds JUCE, reports ASIO enabled.

- [ ] **Step 2: Build the project**

```bash
cd /c/dev/sequencer
cmake --build build --config Release
```

Expected: Builds without errors. May have some warnings — that's OK.

- [ ] **Step 3: Find and run the executable**

```bash
find /c/dev/sequencer/build -name "Sequencer.exe" -type f
```

Run the executable. Expected:
1. Window appears titled "Sequencer" (800x600)
2. "Audio Settings" button visible
3. "Test Tone" toggle visible
4. Status shows current audio device

- [ ] **Step 4: Test ASIO selection**

Click "Audio Settings" → select your ASIO driver from the dropdown → close dialog. Status label should update with device name, sample rate, and buffer size.

- [ ] **Step 5: Test tone output**

Click "Test Tone" toggle. Expected: audible 440Hz sine wave through your speakers/headphones. Toggle off — silence.

- [ ] **Step 6: Commit build artifacts exclusion and final state**

```bash
cd /c/dev/sequencer
git add src/ CMakeLists.txt .gitignore docs/
git commit -m "feat: audio engine foundation complete — ASIO + test tone working"
```

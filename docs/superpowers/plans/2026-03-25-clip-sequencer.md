# Clip Sequencer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Session-view clip sequencer with 16×4 grid, MIDI recording, clip looping, and transport with BPM.

**Architecture:** ClipPlayerNode per track handles clip playback/recording in the audio graph. SequencerEngine manages transport timing. MidiClip stores recorded MIDI data. Clip grid UI alongside existing track list.

**Tech Stack:** C++17, JUCE 7.0.12

**Spec:** `docs/superpowers/specs/2026-03-25-clip-sequencer-design.md`

---

## File Structure

```
C:/dev/sequencer/
  CMakeLists.txt                — modify: add new source files
  src/
    MidiClip.h                  — new: MidiClip + ClipSlot data structures
    ClipPlayerNode.h            — new: per-track clip playback/recording processor
    ClipPlayerNode.cpp          — new: processBlock with clip MIDI injection
    SequencerEngine.h           — new: transport state, beat tracking
    SequencerEngine.cpp         — new: advance position, manage clip states
    PluginHost.h                — modify: add ClipPlayerNodes, SequencerEngine
    PluginHost.cpp              — modify: graph wiring with ClipPlayerNodes
    TrackComponent.h            — modify: add arm button
    TrackComponent.cpp          — modify: arm button UI
    MainComponent.h             — modify: clip grid, transport bar
    MainComponent.cpp           — modify: clip grid UI, transport controls
```

---

### Task 1: CMake + MidiClip data structures

**Files:**
- Modify: `C:/dev/sequencer/CMakeLists.txt`
- Create: `C:/dev/sequencer/src/MidiClip.h`

- [ ] **Step 1: Update CMakeLists.txt target_sources**

```cmake
target_sources(Sequencer PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp
    src/PluginHost.h
    src/PluginHost.cpp
    src/GainProcessor.h
    src/GainProcessor.cpp
    src/TrackComponent.h
    src/TrackComponent.cpp
    src/MidiClip.h
    src/ClipPlayerNode.h
    src/ClipPlayerNode.cpp
    src/SequencerEngine.h
    src/SequencerEngine.cpp
)
```

- [ ] **Step 2: Write MidiClip.h**

```cpp
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

struct MidiClip
{
    juce::MidiMessageSequence events;  // timestamps in beats
    double lengthInBeats = 4.0;        // default 1 bar at 4/4
};

struct ClipSlot
{
    enum State { Empty, Stopped, Playing, Recording };

    std::unique_ptr<MidiClip> clip;
    std::atomic<State> state { Empty };

    bool hasContent() const { return clip != nullptr && clip->events.getNumEvents() > 0; }
};
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add CMakeLists.txt src/MidiClip.h
git commit -m "feat: add MidiClip and ClipSlot data structures"
```

---

### Task 2: SequencerEngine

**Files:**
- Create: `C:/dev/sequencer/src/SequencerEngine.h`
- Create: `C:/dev/sequencer/src/SequencerEngine.cpp`

- [ ] **Step 1: Write SequencerEngine.h**

```cpp
#pragma once

#include <JuceHeader.h>
#include <atomic>

class SequencerEngine
{
public:
    SequencerEngine();

    // Transport controls (called from UI thread)
    void play();
    void stop();
    void toggleRecord();
    void setBpm(double bpm);

    // State queries
    bool isPlaying() const { return playing.load(); }
    bool isRecording() const { return recording.load(); }
    double getBpm() const { return bpm.load(); }
    double getPositionInBeats() const { return positionInBeats.load(); }

    // Called from audio thread each block
    // Returns beats covered in this block
    double advancePosition(int numSamples, double sampleRate);

    // Reset position to 0
    void resetPosition();

private:
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> positionInBeats { 0.0 };
};
```

- [ ] **Step 2: Write SequencerEngine.cpp**

```cpp
#include "SequencerEngine.h"

SequencerEngine::SequencerEngine() {}

void SequencerEngine::play()
{
    playing.store(true);
}

void SequencerEngine::stop()
{
    playing.store(false);
    recording.store(false);
    resetPosition();
}

void SequencerEngine::toggleRecord()
{
    recording.store(!recording.load());
}

void SequencerEngine::setBpm(double newBpm)
{
    bpm.store(juce::jlimit(20.0, 300.0, newBpm));
}

double SequencerEngine::advancePosition(int numSamples, double sampleRate)
{
    if (!playing.load())
        return 0.0;

    double currentBpm = bpm.load();
    double beatsPerSecond = currentBpm / 60.0;
    double beatsThisBlock = beatsPerSecond * (static_cast<double>(numSamples) / sampleRate);

    double oldPos = positionInBeats.load();
    positionInBeats.store(oldPos + beatsThisBlock);

    return beatsThisBlock;
}

void SequencerEngine::resetPosition()
{
    positionInBeats.store(0.0);
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/SequencerEngine.h src/SequencerEngine.cpp
git commit -m "feat: add SequencerEngine with transport and BPM"
```

---

### Task 3: ClipPlayerNode

**Files:**
- Create: `C:/dev/sequencer/src/ClipPlayerNode.h`
- Create: `C:/dev/sequencer/src/ClipPlayerNode.cpp`

- [ ] **Step 1: Write ClipPlayerNode.h**

```cpp
#pragma once

#include <JuceHeader.h>
#include "MidiClip.h"
#include "SequencerEngine.h"
#include <array>

class ClipPlayerNode : public juce::AudioProcessor
{
public:
    static constexpr int NUM_SLOTS = 4;

    ClipPlayerNode(SequencerEngine& engine);

    const juce::String getName() const override { return "ClipPlayerNode"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void releaseResources() override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Clip slot access
    ClipSlot& getSlot(int index) { return slots[static_cast<size_t>(index)]; }

    // Trigger actions (called from UI thread)
    void triggerSlot(int slotIndex);   // play or record
    void stopSlot(int slotIndex);
    void stopAllSlots();

    // Arm for recording
    std::atomic<bool> armed { false };

private:
    SequencerEngine& engine;
    std::array<ClipSlot, NUM_SLOTS> slots;

    double currentSampleRate = 44100.0;
    double lastPositionInBeats = 0.0;

    // Recording state
    int recordingSlot = -1;
    double recordStartBeat = 0.0;

    void processClipPlayback(int slotIndex, juce::MidiBuffer& midi, int numSamples);
    void processRecording(const juce::MidiBuffer& incomingMidi, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipPlayerNode)
};
```

- [ ] **Step 2: Write ClipPlayerNode.cpp**

```cpp
#include "ClipPlayerNode.h"

ClipPlayerNode::ClipPlayerNode(SequencerEngine& eng)
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      engine(eng)
{
}

void ClipPlayerNode::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
}

void ClipPlayerNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    int numSamples = buffer.getNumSamples();

    // Handle recording — capture incoming MIDI before we add clip playback
    if (recordingSlot >= 0 && engine.isPlaying())
    {
        processRecording(midi, numSamples);
    }

    // Handle playback for all playing clips
    if (engine.isPlaying())
    {
        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            if (slots[static_cast<size_t>(i)].state.load() == ClipSlot::Playing)
            {
                processClipPlayback(i, midi, numSamples);
            }
        }

        lastPositionInBeats = engine.getPositionInBeats();
    }

    // Audio passes through unchanged (this node only handles MIDI)
}

void ClipPlayerNode::processClipPlayback(int slotIndex, juce::MidiBuffer& midi, int numSamples)
{
    auto& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.clip == nullptr) return;

    auto& clip = *slot.clip;
    double pos = engine.getPositionInBeats();
    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;

    double clipLen = clip.lengthInBeats;
    if (clipLen <= 0.0) return;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        double beatPos = pos + (sample * beatsPerSample);
        double clipPos = std::fmod(beatPos, clipLen);
        if (clipPos < 0.0) clipPos += clipLen;

        // Check previous sample position for event detection
        double prevBeatPos = beatPos - beatsPerSample;
        double prevClipPos = std::fmod(prevBeatPos, clipLen);
        if (prevClipPos < 0.0) prevClipPos += clipLen;

        // Handle wrap-around
        bool wrapped = prevClipPos > clipPos;

        for (int e = 0; e < clip.events.getNumEvents(); ++e)
        {
            auto* event = clip.events.getEventPointer(e);
            double eventBeat = event->message.getTimeStamp();

            bool shouldTrigger = false;

            if (wrapped)
            {
                // Event is between prevClipPos..end OR 0..clipPos
                if (eventBeat > prevClipPos || eventBeat <= clipPos)
                    shouldTrigger = true;
            }
            else
            {
                if (eventBeat > prevClipPos && eventBeat <= clipPos)
                    shouldTrigger = true;
            }

            if (shouldTrigger)
            {
                midi.addEvent(event->message, sample);
            }
        }
    }
}

void ClipPlayerNode::processRecording(const juce::MidiBuffer& incomingMidi, int numSamples)
{
    if (recordingSlot < 0 || recordingSlot >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(recordingSlot)];
    if (slot.clip == nullptr) return;

    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;
    double pos = engine.getPositionInBeats();

    for (const auto metadata : incomingMidi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOnOrOff())
        {
            double beatTimestamp = (pos - recordStartBeat) + (metadata.samplePosition * beatsPerSample);
            if (beatTimestamp < 0.0) beatTimestamp = 0.0;

            msg.setTimeStamp(beatTimestamp);
            slot.clip->events.addEvent(msg);

            // Extend clip length to fit the recorded content
            if (beatTimestamp > slot.clip->lengthInBeats - 0.1)
            {
                // Round up to next bar
                slot.clip->lengthInBeats = std::ceil(beatTimestamp / 4.0) * 4.0;
                if (slot.clip->lengthInBeats < 4.0) slot.clip->lengthInBeats = 4.0;
            }
        }
    }
}

void ClipPlayerNode::triggerSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(slotIndex)];

    if (slot.state.load() == ClipSlot::Empty && armed.load() && engine.isRecording())
    {
        // Start recording
        slot.clip = std::make_unique<MidiClip>();
        slot.state.store(ClipSlot::Recording);
        recordingSlot = slotIndex;
        recordStartBeat = engine.getPositionInBeats();
    }
    else if (slot.hasContent() && slot.state.load() != ClipSlot::Playing)
    {
        // Start playback
        stopAllSlots();
        slot.state.store(ClipSlot::Playing);
    }
    else if (slot.state.load() == ClipSlot::Playing)
    {
        // Stop playback
        slot.state.store(ClipSlot::Stopped);
    }
    else if (slot.state.load() == ClipSlot::Recording)
    {
        // Stop recording
        slot.state.store(ClipSlot::Stopped);
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
    }
}

void ClipPlayerNode::stopSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(slotIndex)];

    if (slot.state.load() == ClipSlot::Recording)
    {
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
    }

    if (slot.state.load() != ClipSlot::Empty)
        slot.state.store(slot.hasContent() ? ClipSlot::Stopped : ClipSlot::Empty);
}

void ClipPlayerNode::stopAllSlots()
{
    for (int i = 0; i < NUM_SLOTS; ++i)
        stopSlot(i);
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/ClipPlayerNode.h src/ClipPlayerNode.cpp
git commit -m "feat: add ClipPlayerNode for per-track clip playback and recording"
```

---

### Task 4: Update PluginHost — integrate ClipPlayerNodes

**Files:**
- Modify: `C:/dev/sequencer/src/PluginHost.h`
- Modify: `C:/dev/sequencer/src/PluginHost.cpp`

- [ ] **Step 1: Write PluginHost.h**

Add SequencerEngine, ClipPlayerNodes to Track struct, expose engine. Full header:

```cpp
#pragma once

#include <JuceHeader.h>
#include "GainProcessor.h"
#include "ClipPlayerNode.h"
#include "SequencerEngine.h"
#include <atomic>
#include <array>

struct Track {
    int index = 0;
    juce::String name;
    juce::AudioProcessorGraph::Node::Ptr pluginNode;
    juce::AudioProcessorGraph::Node::Ptr gainNode;
    juce::AudioProcessorGraph::Node::Ptr clipPlayerNode;
    GainProcessor* gainProcessor = nullptr;
    ClipPlayerNode* clipPlayer = nullptr;
    juce::AudioProcessor* plugin = nullptr;
};

class PluginHost : public juce::AudioProcessorGraph
{
public:
    static constexpr int NUM_TRACKS = 16;

    PluginHost();
    ~PluginHost() override;

    void scanForPlugins();
    const juce::KnownPluginList& getPluginList() const { return knownPluginList; }

    bool loadPlugin(int trackIndex, const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin(int trackIndex);

    Track& getTrack(int index) { return tracks[static_cast<size_t>(index)]; }
    const Track& getTrack(int index) const { return tracks[static_cast<size_t>(index)]; }

    void setSelectedTrack(int index);
    int getSelectedTrack() const { return selectedTrack; }

    juce::MidiMessageCollector& getMidiCollector() { return midiCollector; }
    void sendTestNoteOn(int noteNumber = 60, float velocity = 0.78f);
    void sendTestNoteOff(int noteNumber = 60);

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    void setAudioParams(double sampleRate, int blockSize);

    // Sequencer engine access
    SequencerEngine& getEngine() { return engine; }

    std::atomic<int> soloCount { 0 };

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::MidiMessageCollector midiCollector;
    SequencerEngine engine;

    std::array<Track, NUM_TRACKS> tracks;

    Node::Ptr midiInputNode;
    Node::Ptr audioOutputNode;

    int selectedTrack = 0;

    double storedSampleRate = 44100.0;
    int storedBlockSize = 512;

    void setupGraph();
    void connectTrackAudio(int trackIndex);
    void updateMidiRouting();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
```

- [ ] **Step 2: Write PluginHost.cpp**

Key change: setupGraph creates ClipPlayerNodes per track. Graph wiring: MIDI input → ClipPlayerNode → Plugin → Gain → Output. Full implementation:

```cpp
#include "PluginHost.h"

PluginHost::PluginHost()
{
    formatManager.addFormat(new juce::VST3PluginFormat());

    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        tracks[static_cast<size_t>(i)].index = i;
        tracks[static_cast<size_t>(i)].name = "Track " + juce::String(i + 1);
    }

    setupGraph();
}

PluginHost::~PluginHost()
{
    clear();
}

void PluginHost::setupGraph()
{
    setPlayConfigDetails(0, 2, storedSampleRate, storedBlockSize);

    midiInputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    audioOutputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    for (int i = 0; i < NUM_TRACKS; ++i)
    {
        auto& track = tracks[static_cast<size_t>(i)];

        // Gain node
        auto gainProc = std::make_unique<GainProcessor>();
        gainProc->soloCount = &soloCount;
        track.gainProcessor = gainProc.get();
        track.gainNode = addNode(std::move(gainProc));

        // ClipPlayer node
        auto clipProc = std::make_unique<ClipPlayerNode>(engine);
        track.clipPlayer = clipProc.get();
        track.clipPlayerNode = addNode(std::move(clipProc));

        // Gain -> audio output
        for (int ch = 0; ch < 2; ++ch)
        {
            addConnection({ { track.gainNode->nodeID, ch },
                            { audioOutputNode->nodeID, ch } });
        }
    }
}

void PluginHost::scanForPlugins()
{
    auto* format = formatManager.getFormat(0);
    if (format == nullptr) return;

    auto searchPaths = format->getDefaultLocationsToSearch();
    auto foundFiles = format->searchPathsForPlugins(searchPaths, true, false);

    for (const auto& file : foundFiles)
    {
        juce::OwnedArray<juce::PluginDescription> foundTypes;
        knownPluginList.scanAndAddFile(file, true, foundTypes, *format);
    }
}

void PluginHost::setAudioParams(double sampleRate, int blockSize)
{
    storedSampleRate = sampleRate;
    storedBlockSize = blockSize;
    midiCollector.reset(sampleRate);
}

bool PluginHost::loadPlugin(int trackIndex, const juce::PluginDescription& desc, juce::String& errorMsg)
{
    if (trackIndex < 0 || trackIndex >= NUM_TRACKS) return false;

    unloadPlugin(trackIndex);

    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    auto& track = tracks[static_cast<size_t>(trackIndex)];
    track.plugin = instance.get();
    track.pluginNode = addNode(std::move(instance));

    if (track.pluginNode == nullptr)
    {
        track.plugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectTrackAudio(trackIndex);
    updateMidiRouting();
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= NUM_TRACKS) return;

    auto& track = tracks[static_cast<size_t>(trackIndex)];
    if (track.pluginNode == nullptr) return;

    auto connections = getConnections();
    for (auto& conn : connections)
    {
        if (conn.source.nodeID == track.pluginNode->nodeID ||
            conn.destination.nodeID == track.pluginNode->nodeID)
        {
            removeConnection(conn);
        }
    }

    removeNode(track.pluginNode->nodeID);
    track.pluginNode = nullptr;
    track.plugin = nullptr;
}

void PluginHost::connectTrackAudio(int trackIndex)
{
    auto& track = tracks[static_cast<size_t>(trackIndex)];
    if (track.pluginNode == nullptr) return;

    // ClipPlayer MIDI -> Plugin MIDI
    addConnection({ { track.clipPlayerNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { track.pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    // Plugin audio -> Gain
    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { track.pluginNode->nodeID, ch },
                        { track.gainNode->nodeID, ch } });
    }
}

void PluginHost::setSelectedTrack(int index)
{
    if (index < 0 || index >= NUM_TRACKS) return;
    selectedTrack = index;
    updateMidiRouting();
}

void PluginHost::updateMidiRouting()
{
    // Remove all MIDI connections from MIDI input node
    auto connections = getConnections();
    for (auto& conn : connections)
    {
        if (conn.source.nodeID == midiInputNode->nodeID &&
            conn.source.channelIndex == AudioProcessorGraph::midiChannelIndex)
        {
            removeConnection(conn);
        }
    }

    // Connect MIDI input to selected track's ClipPlayerNode
    auto& track = tracks[static_cast<size_t>(selectedTrack)];
    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { track.clipPlayerNode->nodeID, AudioProcessorGraph::midiChannelIndex } });
}

void PluginHost::sendTestNoteOn(int noteNumber, float velocity)
{
    auto msg = juce::MidiMessage::noteOn(1, noteNumber, velocity);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::sendTestNoteOff(int noteNumber)
{
    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    msg.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue(msg);
}

void PluginHost::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    midiCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

    // Advance transport
    engine.advancePosition(buffer.getNumSamples(), storedSampleRate);

    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/PluginHost.h src/PluginHost.cpp
git commit -m "feat: integrate ClipPlayerNodes and SequencerEngine into PluginHost"
```

---

### Task 5: Update TrackComponent — add arm button

**Files:**
- Modify: `C:/dev/sequencer/src/TrackComponent.h`
- Modify: `C:/dev/sequencer/src/TrackComponent.cpp`

- [ ] **Step 1: Update TrackComponent.h**

Add arm button and callback:

```cpp
#pragma once

#include <JuceHeader.h>
#include <functional>

class TrackComponent : public juce::Component
{
public:
    TrackComponent(int trackIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setSelected(bool selected);
    void setPluginName(const juce::String& name);
    float getVolume() const;
    bool isMuted() const;
    bool isSoloed() const;
    bool isArmed() const;

    std::function<void(int)> onSelected;
    std::function<void(int, float)> onVolumeChanged;
    std::function<void(int, bool)> onMuteChanged;
    std::function<void(int, bool)> onSoloChanged;
    std::function<void(int, bool)> onArmChanged;

private:
    int index;
    bool selected = false;

    juce::TextButton armButton { "A" };
    juce::Label trackLabel;
    juce::Label pluginLabel;
    juce::Slider volumeSlider;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackComponent)
};
```

- [ ] **Step 2: Update TrackComponent.cpp**

```cpp
#include "TrackComponent.h"

TrackComponent::TrackComponent(int trackIndex)
    : index(trackIndex)
{
    addAndMakeVisible(armButton);
    armButton.setClickingTogglesState(true);
    armButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker());
    armButton.onClick = [this] {
        if (onArmChanged)
            onArmChanged(index, armButton.getToggleState());
    };

    addAndMakeVisible(trackLabel);
    trackLabel.setText("Trk" + juce::String(index + 1), juce::dontSendNotification);
    trackLabel.setFont(juce::Font(12.0f, juce::Font::bold));

    addAndMakeVisible(pluginLabel);
    pluginLabel.setText("----", juce::dontSendNotification);
    pluginLabel.setFont(juce::Font(11.0f));
    pluginLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(0.8, juce::dontSendNotification);
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.onValueChange = [this] {
        if (onVolumeChanged)
            onVolumeChanged(index, static_cast<float>(volumeSlider.getValue()));
    };

    addAndMakeVisible(muteButton);
    muteButton.setClickingTogglesState(true);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    muteButton.onClick = [this] {
        if (onMuteChanged)
            onMuteChanged(index, muteButton.getToggleState());
    };

    addAndMakeVisible(soloButton);
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::yellow);
    soloButton.onClick = [this] {
        if (onSoloChanged)
            onSoloChanged(index, soloButton.getToggleState());
    };
}

void TrackComponent::paint(juce::Graphics& g)
{
    if (selected)
        g.fillAll(juce::Colour(0xff3a5a8a));
    else
        g.fillAll(juce::Colour(0xff2a2a2a));

    g.setColour(juce::Colour(0xff444444));
    g.drawLine(0, static_cast<float>(getHeight()), static_cast<float>(getWidth()), static_cast<float>(getHeight()));
}

void TrackComponent::resized()
{
    auto area = getLocalBounds().reduced(2, 1);

    armButton.setBounds(area.removeFromLeft(22));
    area.removeFromLeft(2);
    trackLabel.setBounds(area.removeFromLeft(35));
    pluginLabel.setBounds(area.removeFromLeft(70));
    muteButton.setBounds(area.removeFromRight(22));
    area.removeFromRight(2);
    soloButton.setBounds(area.removeFromRight(22));
    area.removeFromRight(2);
    volumeSlider.setBounds(area);
}

void TrackComponent::mouseDown(const juce::MouseEvent& /*e*/)
{
    if (onSelected)
        onSelected(index);
}

void TrackComponent::setSelected(bool sel)
{
    selected = sel;
    repaint();
}

void TrackComponent::setPluginName(const juce::String& name)
{
    pluginLabel.setText(name.isEmpty() ? "----" : name, juce::dontSendNotification);
}

float TrackComponent::getVolume() const
{
    return static_cast<float>(volumeSlider.getValue());
}

bool TrackComponent::isMuted() const
{
    return muteButton.getToggleState();
}

bool TrackComponent::isSoloed() const
{
    return soloButton.getToggleState();
}

bool TrackComponent::isArmed() const
{
    return armButton.getToggleState();
}
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/TrackComponent.h src/TrackComponent.cpp
git commit -m "feat: add arm button to TrackComponent"
```

---

### Task 6: Rewrite MainComponent — clip grid + transport

**Files:**
- Modify: `C:/dev/sequencer/src/MainComponent.h`
- Modify: `C:/dev/sequencer/src/MainComponent.cpp`

- [ ] **Step 1: Write MainComponent.h**

```cpp
#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "TrackComponent.h"

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor,
                       std::function<void()> onClose)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton),
          closeCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(editor, true);
        setVisible(true);
        centreWithSize(getWidth(), getHeight());
    }
    void closeButtonPressed() override { if (closeCallback) closeCallback(); }
private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

class MainComponent : public juce::Component, public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // Top controls
    juce::ComboBox midiInputSelector;
    juce::TextButton midiRefreshButton { "Refresh" };
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton { "Open Editor" };
    juce::TextButton testNoteButton { "Play Test Note" };
    juce::TextButton audioSettingsButton { "Audio Settings" };

    // Track list
    juce::OwnedArray<TrackComponent> trackComponents;
    juce::Viewport trackViewport;
    juce::Component trackListContainer;
    int selectedTrackIndex = 0;

    // Clip grid buttons — 16 tracks × 4 slots
    juce::OwnedArray<juce::TextButton> clipButtons;
    juce::Component clipGridContainer;
    juce::Viewport clipGridViewport;

    // Transport
    juce::TextButton recordButton { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Label beatLabel;

    // Status
    juce::Label statusLabel;

    // Plugin editor
    std::unique_ptr<juce::AudioProcessorEditor> currentEditor;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::String currentMidiDeviceId;

    void scanMidiDevices();
    void selectMidiDevice();
    void disableCurrentMidiDevice();
    void scanPlugins();
    void loadSelectedPlugin();
    void openPluginEditor();
    void closePluginEditor();
    void playTestNote();
    void showAudioSettings();
    void updateStatusLabel();

    void selectTrack(int index);
    void onTrackVolumeChanged(int trackIndex, float volume);
    void onTrackMuteChanged(int trackIndex, bool muted);
    void onTrackSoloChanged(int trackIndex, bool soloed);
    void onTrackArmChanged(int trackIndex, bool armed);
    void setupTrackList();
    void setupClipGrid();
    void onClipButtonClicked(int trackIndex, int slotIndex);
    void updateClipButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
```

- [ ] **Step 2: Write MainComponent.cpp**

```cpp
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
    int id = 2;
    for (const auto& d : midiDevices) midiInputSelector.addItem(d.name, id++);
    midiInputSelector.setSelectedId(1, juce::dontSendNotification);
}

void MainComponent::selectMidiDevice()
{
    disableCurrentMidiDevice();
    int idx = midiInputSelector.getSelectedId() - 2;
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
    if (currentMidiDeviceId.isNotEmpty())
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
```

- [ ] **Step 3: Commit**

```bash
cd /c/dev/sequencer
git add src/MainComponent.h src/MainComponent.cpp
git commit -m "feat: add clip grid UI and transport controls"
```

---

### Task 7: Build and Test

- [ ] **Step 1: Kill, reconfigure, build**

```bash
taskkill //IM Sequencer.exe //F 2>/dev/null
cd /c/dev/sequencer
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release
```

- [ ] **Step 2: Test**

1. Launch Sequencer.exe
2. Load a plugin on Track 1
3. Select MIDI input
4. Set BPM to 120
5. Arm Track 1 (click A button)
6. Click REC then PLAY
7. Click an empty clip slot on Track 1 → should start recording (red)
8. Play some notes on MIDI controller
9. Click the recording slot again → stops recording (shows ■)
10. Click the recorded slot → plays back in a loop (green ▶)
11. Load another plugin on Track 2, record a clip there too
12. Both clips playing simultaneously
13. Click STOP → all clips stop, beat resets

- [ ] **Step 3: Final commit**

```bash
cd /c/dev/sequencer
git add src/ CMakeLists.txt
git commit -m "feat: clip sequencer complete — record, playback, transport"
```

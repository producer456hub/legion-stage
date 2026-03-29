#pragma once

#include <JuceHeader.h>
#include "MidiClip.h"
#include "SequencerEngine.h"
#include <array>
#include <set>

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
    std::atomic<bool> armLocked { false };  // stays armed when switching tracks

    // Flag to send all-notes-off on next processBlock
    std::atomic<bool> sendAllNotesOff { false };

private:
    SequencerEngine& engine;
    std::array<ClipSlot, NUM_SLOTS> slots;

    double currentSampleRate = 44100.0;
    double lastPositionInBeats = 0.0;
    std::array<bool, NUM_SLOTS> wasInsideClip {}; // track clip exit for note-off

    // Recording state
    int recordingSlot = -1;
    double recordStartBeat = 0.0;

    // Track active notes so we can send explicit note-offs on loop wrap
    // Encoded as (channel << 8) | noteNumber
    std::set<int> activePlaybackNotes;
    void killActiveNotes(juce::MidiBuffer& midi, int sampleOffset, bool hard = false);

    void processClipPlayback(int slotIndex, juce::MidiBuffer& midi, int numSamples);
    void processRecording(const juce::MidiBuffer& incomingMidi, int numSamples);
    void closeOpenNotes(MidiClip& clip);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipPlayerNode)
};

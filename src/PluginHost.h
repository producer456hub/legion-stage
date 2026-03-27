#pragma once

#include <JuceHeader.h>
#include "GainProcessor.h"
#include "ClipPlayerNode.h"
#include "SequencerEngine.h"
#include <atomic>
#include <array>

class SpectrumComponent;
class LissajousComponent;
class GForceComponent;
class GeissComponent;
class ProjectMComponent;

struct FxSlot {
    juce::AudioProcessorGraph::Node::Ptr node;
    juce::AudioProcessor* processor = nullptr;
    bool bypassed = false;
};

struct Track {
    int index = 0;
    juce::String name;
    juce::AudioProcessorGraph::Node::Ptr pluginNode;
    juce::AudioProcessorGraph::Node::Ptr gainNode;
    juce::AudioProcessorGraph::Node::Ptr clipPlayerNode;
    GainProcessor* gainProcessor = nullptr;
    ClipPlayerNode* clipPlayer = nullptr;
    juce::AudioProcessor* plugin = nullptr;
    juce::OwnedArray<AutomationLane> automationLanes;

    static constexpr int NUM_FX_SLOTS = 3;
    FxSlot fxSlots[NUM_FX_SLOTS];
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

    // FX inserts
    bool loadFx(int trackIndex, int slotIndex, const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadFx(int trackIndex, int slotIndex);
    void setFxBypassed(int trackIndex, int slotIndex, bool bypassed);

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

    // Spectrum analyzer — set from UI, read from audio thread
    SpectrumComponent* spectrumDisplay = nullptr;
    GForceComponent* gforceDisplay = nullptr;
    GeissComponent* geissDisplay = nullptr;
    ProjectMComponent* projectMDisplay = nullptr;

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

    // MIDI clock state
    bool midiClockWasPlaying = false;
    double midiClockPulseAccum = 0.0;  // fractional pulse accumulator

    void setupGraph();
    void connectTrackAudio(int trackIndex);
    void rewireTrack(int trackIndex);
    void updateMidiRouting();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

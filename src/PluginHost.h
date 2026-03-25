#pragma once

#include <JuceHeader.h>
#include "GainProcessor.h"
#include <atomic>
#include <array>

struct Track {
    int index = 0;
    juce::String name;
    juce::AudioProcessorGraph::Node::Ptr pluginNode;
    juce::AudioProcessorGraph::Node::Ptr gainNode;
    GainProcessor* gainProcessor = nullptr;  // raw ptr — graph owns it
    juce::AudioProcessor* plugin = nullptr;  // raw ptr — graph owns it
};

class PluginHost : public juce::AudioProcessorGraph
{
public:
    static constexpr int NUM_TRACKS = 16;

    PluginHost();
    ~PluginHost() override;

    // Plugin scanning
    void scanForPlugins();
    const juce::KnownPluginList& getPluginList() const { return knownPluginList; }

    // Per-track plugin loading — caller must suspend audio before calling
    bool loadPlugin(int trackIndex, const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin(int trackIndex);

    // Track access
    Track& getTrack(int index) { return tracks[static_cast<size_t>(index)]; }
    const Track& getTrack(int index) const { return tracks[static_cast<size_t>(index)]; }

    // Selected track for MIDI routing
    void setSelectedTrack(int index);
    int getSelectedTrack() const { return selectedTrack; }

    // MIDI
    juce::MidiMessageCollector& getMidiCollector() { return midiCollector; }
    void sendTestNoteOn(int noteNumber = 60, float velocity = 0.78f);
    void sendTestNoteOff(int noteNumber = 60);

    // Override processBlock to pull MIDI from collector
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    void setAudioParams(double sampleRate, int blockSize);

    // Solo management
    std::atomic<int> soloCount { 0 };

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::MidiMessageCollector midiCollector;

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

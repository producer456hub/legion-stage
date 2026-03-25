#pragma once

#include <JuceHeader.h>

class PluginHost : public juce::AudioProcessorGraph
{
public:
    PluginHost();
    ~PluginHost() override;

    // Plugin scanning
    void scanForPlugins();
    const juce::KnownPluginList& getPluginList() const { return knownPluginList; }

    // Plugin loading — caller must suspend audio before calling
    // Returns true on success, false on failure (errorMsg set)
    bool loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin();

    // Current plugin access (for editor creation)
    juce::AudioProcessor* getCurrentPlugin() const { return currentPlugin; }

    // MIDI injection for test notes
    void sendTestNoteOn(int noteNumber = 60, float velocity = 0.78f);
    void sendTestNoteOff(int noteNumber = 60);

    // Override processBlock to inject pending MIDI
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    // Store audio params for plugin init during swaps
    void setAudioParams(double sampleRate, int blockSize);

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;

    // Graph nodes
    Node::Ptr midiInputNode;
    Node::Ptr audioOutputNode;
    Node::Ptr pluginNode;

    // Current loaded plugin (raw ptr — graph owns it via the node)
    juce::AudioProcessor* currentPlugin = nullptr;

    // MIDI injection
    juce::MidiBuffer pendingMidi;
    juce::SpinLock midiLock;

    // Stored audio params
    double storedSampleRate = 44100.0;
    int storedBlockSize = 512;

    void setupGraph();
    void connectPlugin();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};

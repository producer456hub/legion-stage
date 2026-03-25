#include "PluginHost.h"

PluginHost::PluginHost()
{
    // Explicitly add only VST3 — don't rely on addDefaultFormats() ordering
    formatManager.addFormat(new juce::VST3PluginFormat());
    setupGraph();
}

PluginHost::~PluginHost()
{
    // Clear graph before destruction
    clear();
}

void PluginHost::setupGraph()
{
    // Set graph channel layout: 0 MIDI in, 2 audio out
    setPlayConfigDetails(0, 2, storedSampleRate, storedBlockSize);

    midiInputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    audioOutputNode = addNode(
        std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
            AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
}

void PluginHost::scanForPlugins()
{
    auto* format = formatManager.getFormat(0); // VST3 format
    if (format == nullptr) return;

    // Get default search paths for VST3
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
}

bool PluginHost::loadPlugin(const juce::PluginDescription& desc, juce::String& errorMsg)
{
    // Remove old plugin first
    unloadPlugin();

    // Create plugin instance
    auto instance = formatManager.createPluginInstance(desc, storedSampleRate, storedBlockSize, errorMsg);
    if (instance == nullptr)
        return false;

    currentPlugin = instance.get();

    // Add to graph
    pluginNode = addNode(std::move(instance));
    if (pluginNode == nullptr)
    {
        currentPlugin = nullptr;
        errorMsg = "Failed to add plugin to graph";
        return false;
    }

    connectPlugin();

    // Re-prepare the graph so the new plugin gets initialized
    prepareToPlay(storedSampleRate, storedBlockSize);

    return true;
}

void PluginHost::unloadPlugin()
{
    if (pluginNode != nullptr)
    {
        // Copy connections first — iterating while removing invalidates the container
        auto connections = getConnections();
        for (auto& conn : connections)
        {
            if (conn.source.nodeID == pluginNode->nodeID ||
                conn.destination.nodeID == pluginNode->nodeID)
            {
                removeConnection(conn);
            }
        }

        removeNode(pluginNode->nodeID);
        pluginNode = nullptr;
        currentPlugin = nullptr;
    }
}

void PluginHost::connectPlugin()
{
    if (pluginNode == nullptr) return;

    // MIDI: midiInput -> plugin
    addConnection({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                    { pluginNode->nodeID, AudioProcessorGraph::midiChannelIndex } });

    // Audio: plugin -> audioOutput (stereo)
    for (int ch = 0; ch < 2; ++ch)
    {
        addConnection({ { pluginNode->nodeID, ch },
                        { audioOutputNode->nodeID, ch } });
    }
}

void PluginHost::sendTestNoteOn(int noteNumber, float velocity)
{
    auto msg = juce::MidiMessage::noteOn(1, noteNumber, velocity);
    msg.setTimeStamp(0);

    const juce::SpinLock::ScopedLockType lock(midiLock);
    pendingMidi.addEvent(msg, 0);
}

void PluginHost::sendTestNoteOff(int noteNumber)
{
    auto msg = juce::MidiMessage::noteOff(1, noteNumber);
    msg.setTimeStamp(0);

    const juce::SpinLock::ScopedLockType lock(midiLock);
    pendingMidi.addEvent(msg, 0);
}

void PluginHost::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Inject any pending MIDI messages
    {
        const juce::SpinLock::ScopedLockType lock(midiLock);
        if (!pendingMidi.isEmpty())
        {
            midiMessages.addEvents(pendingMidi, 0, buffer.getNumSamples(), 0);
            pendingMidi.clear();
        }
    }

    // Process the graph
    AudioProcessorGraph::processBlock(buffer, midiMessages);
}
